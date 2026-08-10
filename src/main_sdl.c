// Interactive frontend: window + keyboard input + audio, driving the vendored
// SNES core — with the port substituted in, which is the point of it.
//
// This started as the Phase 0b playable baseline: boot the ROM under the core
// and show the picture. That baseline is still here (`--stock`), because it is
// the reference the substituted run is judged against. What is new is that the
// default is no longer stock.
//
// ## What "native" means here, exactly
//
// `zamn_cosim run` already substitutes the port for real — it just does it
// headless, driven by a recorded movie, next to a second core it diffs against.
// The substitution machinery has no opinion about where its input comes from or
// whether anyone is watching, so this file is the same mechanism with a keyboard
// on the front and no reference core behind it:
//
//   * `cosim_init(COSIM_NATIVE)` against the one live core, and
//   * `cosim_frame()` in place of `snes_runFrame()`.
//
// That second line is a smaller change than it looks. `cosim_frame` runs the
// identical loop `snes_runFrame` does — drain vblank, run to the next one — and
// its `snes_readBBus(snes, 0x40)` tail is what `snes_catchupApu` is; the only
// difference is that it steps through `cosim_step`, which watches the program
// counter and hands a registered routine to the C port instead of letting the
// 65816 execute it. So `--stock` is not a different code path, it is this one
// with the enable mask cleared, and F1 moves between them at a frame boundary.
//
// ## What it does not mean
//
// Twenty-five routines are ported and the rest of the game is still the ROM's,
// running under the core. The main loop, the NMI handler, player movement, the
// level and camera code and every enemy body belong to the emulator; what runs
// natively are the leaves those call — collision dispatch, the depth sort, OAM
// building, thread spawn and tick, score, fades. Phase 4 is where the port owns
// the main loop and this relationship inverts. Until then the honest description
// of this program is an emulator with about two dozen of its hot routines
// executed as C, and the title bar says so with a live count.
//
// Correctness is not this file's claim to make: `zamn_cosim verify` checks the
// port per call against the ROM, and `zamn_cosim run` checks whole runs against
// a stock core. Both should be believed ahead of anything that looks right on
// screen. What this adds is the ability to *drive* the substituted build with a
// controller rather than a recorded movie, which no other target could do.
//
// Usage: zamn [rom.sfc] [--stock] [-r routine]... [-m movie.zmv]
//             [--frames N] [--shot out.png] [--no-audio]

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL.h>

#include "snes.h"

#include "analysis/movie.h"
#include "analysis/movie_apply.h"
#include "cosim/cosim.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define FB_W 512
#define FB_H 480
#define AUDIO_FREQ 48000
#define SAMPLES_PER_FRAME (AUDIO_FREQ / 60) // 800

// Keyboard -> SNES button bit. The `BTN_*` names are `analysis/movie.h`'s, which
// is where they belong now that this file replays movies too: one definition of
// the controller's bit order, shared by everything that drives one.
static int key_to_button(SDL_Keycode k) {
  switch (k) {
    case SDLK_UP:        return BTN_UP;
    case SDLK_DOWN:      return BTN_DOWN;
    case SDLK_LEFT:      return BTN_LEFT;
    case SDLK_RIGHT:     return BTN_RIGHT;
    case SDLK_z:         return BTN_B;
    case SDLK_x:         return BTN_A;
    case SDLK_a:         return BTN_Y;
    case SDLK_s:         return BTN_X;
    case SDLK_q:         return BTN_L;
    case SDLK_w:         return BTN_R;
    case SDLK_RETURN:    return BTN_START;
    case SDLK_RSHIFT:    return BTN_SELECT;
    case SDLK_BACKSPACE: return BTN_SELECT;
    default:             return -1;
  }
}

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  if (len <= 0) { fclose(f); return NULL; }
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return NULL; }
  fclose(f); *out_len = (int)len; return buf;
}

// The pixel layout the core is asked for. `pixelFormatRGBX` is what pairs
// byte-for-byte with the `SDL_PIXELFORMAT_RGBX8888` texture below, and it is
// also the core's own default — but it is set explicitly, because `write_png`
// has to agree with it and a default is a bad thing for two places to depend on.
#define ZAMN_PIXEL_FORMAT pixelFormatRGBX

// The core's framebuffer as a PNG — how a run nobody watched gets checked
// afterwards.
//
// This is `zamn_headless`'s conversion with one byte of difference, and the byte
// is the whole reason to say anything here. `ppu_setPixelOutputFormat` does not
// reorder the channels, it *shifts* them: a pixel is `[B,G,R,X]` under
// `pixelFormatXRGB` and `[X,B,G,R]` under `pixelFormatRGBX`, the same three
// bytes one position along. Headless picks XRGB and reads from +0; this reads
// from +1, and getting that wrong is not a crash but a picture in the wrong
// palette — green grass comes out brown — which is what it did first time.
static bool write_png(Snes* snes, const char* path) {
  uint8_t* fb = (uint8_t*)malloc(FB_W * FB_H * 4);
  uint8_t* rgb = (uint8_t*)malloc(FB_W * FB_H * 3);
  if (!fb || !rgb) { free(fb); free(rgb); return false; }
  snes_setPixels(snes, fb);
  const int c = ZAMN_PIXEL_FORMAT;  // 0 for [B,G,R,X], 1 for [X,B,G,R]
  for (int i = 0; i < FB_W * FB_H; i++) {
    rgb[i * 3 + 0] = fb[i * 4 + c + 2];  // R
    rgb[i * 3 + 1] = fb[i * 4 + c + 1];  // G
    rgb[i * 3 + 2] = fb[i * 4 + c + 0];  // B
  }
  bool ok = stbi_write_png(path, FB_W, FB_H, 3, rgb, FB_W * 3) != 0;
  free(fb); free(rgb);
  return ok;
}

// A WIN32-subsystem binary has no console of its own, so `printf` goes nowhere
// even when it was launched from one. Borrowing the parent's is what makes the
// substitution report at exit readable; when there is no parent console — a
// double-click — nothing happens and the title bar carries the status instead.
static void attach_parent_console(void) {
#ifdef _WIN32
  // Only when there is nothing there already. A caller that redirected us to a
  // file or a pipe has a perfectly good handle, and reopening `CONOUT$` over it
  // would send the report to the console instead of to the file they asked for —
  // which is how the first smoke test of this frontend produced an empty log.
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out && out != INVALID_HANDLE_VALUE) return;
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
  }
#endif
}

// How many calls the port has actually served, and how many it has handed back.
// Both matter to the title bar: a decline is a call the port could not make, and
// a build whose count is climbing while its declines climb faster is a build
// that is mostly still the ROM.
static void substitution_totals(const Cosim* c, long* served, long* declined) {
  long s = 0, d = 0;
  for (int i = 0; i < c->stat_count; i++) {
    if (!cosim_mask_get(&c->enabled, i)) continue;
    s += c->stats[i].checked;
    d += c->stats[i].declined;
  }
  *served = s; *declined = d;
}

// ...and what share of the game those calls are, which is the number that
// actually says how the port is doing.
//
// A raw count of served calls only goes up, so it cannot tell you whether a
// round of porting bought anything: 13,204 calls is a big number on a level
// that makes 61,000 of them and a huge one on a level that makes 20,000. The
// harness measures both denominators as it runs — see `CosimWork` — so this
// costs a struct copy and puts the progress of the whole port on screen while
// somebody plays it. `work` leads because it is the honest one: it weights a
// routine by the cycles it costs rather than counting a 17-byte leaf and a 2 KB
// state machine alike.
static void share_summary(const Cosim* c, char* out, size_t n) {
  CosimShare s;
  cosim_share(c, &s);
  snprintf(out, n, "%.1f%% of work, %.1f%% of calls", 100.0 * s.work_share,
           100.0 * s.call_share);
}

static void usage(void) {
  printf(
    "zamn — Zombies Ate My Neighbors, with the C port substituted in\n\n"
    "  zamn [rom.sfc] [options]\n\n"
    "  --stock         Do not substitute; run the ROM under the core, as the\n"
    "                  Phase 0b baseline did. F1 still toggles at runtime.\n"
    "  -r <routine>    Substitute only this one. Repeatable; default is every\n"
    "                  routine `zamn_cosim list` reports.\n"
    "  -m <movie.zmv>  Replay a recorded movie instead of reading the keyboard.\n"
    "  --frames <N>    Run N frames and exit, uncapped rather than paced at 60 Hz.\n"
    "  --shot <a.png>  Write the final frame as a PNG on the way out.\n"
    "  --no-audio      Skip the audio device (and pace off a timer instead).\n\n"
    "Controls: Arrows=D-pad  Z=B X=A A=Y S=X  Q=L W=R  Enter=Start RShift=Select\n"
    "          F1 = toggle native substitution   Esc = quit\n");
}

int main(int argc, char** argv) {
  attach_parent_console();

  const char* rom_path = NULL;
  const char* movie_path = NULL;
  const char* shot_path = NULL;
  // Room for every routine in the registry and then some. It was 32, which
  // was more than the registry held when it was written and is not any more:
  // past the cap the `-r` was dropped and its argument fell through to the
  // positional check, so asking for "everything except two" failed with
  // `unexpected argument 'enemy_cdde'` and no hint that a limit existed.
  const char* only[128];
  int only_count = 0;
  long frame_limit = 0;
  bool native = true, want_audio = true;

  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
    else if (!strcmp(a, "--stock")) native = false;
    else if (!strcmp(a, "--no-audio")) want_audio = false;
    else if (!strcmp(a, "-r") && i + 1 < argc) {
      if (only_count == (int)(sizeof only / sizeof *only)) {
        fprintf(stderr, "error: at most %d -r options\n\n",
                (int)(sizeof only / sizeof *only));
        usage();
        return 2;
      }
      only[only_count++] = argv[++i];
    }
    else if (!strcmp(a, "-m") && i + 1 < argc) movie_path = argv[++i];
    else if (!strcmp(a, "--shot") && i + 1 < argc) shot_path = argv[++i];
    else if (!strcmp(a, "--frames") && i + 1 < argc) frame_limit = atol(argv[++i]);
    else if (a[0] == '-') {
      fprintf(stderr, "error: unknown option '%s'\n\n", a); usage(); return 2;
    }
    else if (!rom_path) rom_path = a;
    else { fprintf(stderr, "error: unexpected argument '%s'\n\n", a); usage(); return 2; }
  }
  if (!rom_path) rom_path = "Zombies Ate My Neighbors.sfc";

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) { fprintf(stderr, "error: cannot read ROM '%s'\n", rom_path); return 1; }

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) {
    fprintf(stderr, "error: core rejected ROM '%s'\n", rom_path);
    return 1;
  }

  // Attach the harness before the reset, exactly as `cosim_lockstep` does: it
  // hooks the program counter rather than the machine's state, so it neither
  // needs nor performs one. `COSIM_NATIVE` is what installs the port's APU hook
  // in its driving form — `apu_drive`, which puts real bytes on the real ports —
  // so this is the mode to be in even when starting with substitution off.
  Cosim cosim;
  cosim_init(&cosim, snes, COSIM_NATIVE);
  if (only_count == 0) {
    cosim_enable_all(&cosim);
  } else {
    for (int i = 0; i < only_count; i++) {
      if (!cosim_enable(&cosim, only[i])) {
        fprintf(stderr, "error: no ported routine named '%s'\n", only[i]);
        return 2;
      }
    }
  }
  // The selection is remembered so F1 can put it back; `enabled` is what the
  // engine reads, and clearing it is the whole of running stock.
  const CosimMask selected = cosim.enabled;
  int routine_count = 0;
  for (int i = 0; i < cosim.stat_count; i++)
    routine_count += cosim_mask_get(&selected, i) ? 1 : 0;
  if (!native) cosim_mask_none(&cosim.enabled);

  Movie movie;
  bool have_movie = false;
  if (movie_path) {
    if (!movie_load(&movie, movie_path)) {
      fprintf(stderr, "error: cannot read movie '%s'\n", movie_path);
      return 1;
    }
    have_movie = true;
  }

  snes_setPixelFormat(snes, ZAMN_PIXEL_FORMAT);
  snes_reset(snes, true);

  Uint32 init_flags = SDL_INIT_VIDEO | (want_audio ? SDL_INIT_AUDIO : 0);
  if (SDL_Init(init_flags) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* win = SDL_CreateWindow("Zombies Ate My Neighbors (native)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, FB_W, FB_H,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_RenderSetLogicalSize(ren, FB_W, FB_H);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBX8888,
      SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);

  SDL_AudioDeviceID audio = 0;
  int16_t audio_buf[SAMPLES_PER_FRAME * 2];
  if (want_audio) {
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = AUDIO_FREQ; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 2048;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);
  }

  printf("Controls: Arrows=D-pad  Z=B X=A A=Y S=X  Q=L W=R  Enter=Start RShift=Select\n"
         "          F1=toggle native substitution  Esc=Quit\n");
  printf("Substitution: %s (%d routine%s registered)%s\n",
         native ? "on" : "off (stock)", routine_count,
         routine_count == 1 ? "" : "s",
         have_movie ? ", replaying a movie" : "");

  // Frame pacing. VSync is unreliable (may be >60 Hz or driver-ignored), so we
  // pace explicitly. Primary clock is the audio device: it consumes samples at
  // a fixed 48000 Hz, so gating frame production on the queue draining locks the
  // loop to exactly 60 Hz and keeps A/V in sync. Fallback (no audio) is a timer.
  // `--frames` opts out of both — a smoke test wants to finish, not to be
  // watched — which is also what makes it a usable throughput measurement.
  const Uint32 bytes_per_frame = (Uint32)(SAMPLES_PER_FRAME * 2 * sizeof(int16_t));
  const Uint32 audio_high_water = bytes_per_frame * 4; // keep <= ~4 frames buffered
  const double target_frame_ms = 1000.0 / 60.0;
  const Uint64 perf_freq = SDL_GetPerformanceFrequency();
  Uint64 prev_frame = SDL_GetPerformanceCounter();
  const bool paced = frame_limit == 0;
  const Uint64 started = SDL_GetPerformanceCounter();

  long frame = 0;
  char title[160];
  bool running = true;
  while (running && (frame_limit == 0 || frame < frame_limit)) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        if (e.key.keysym.sym == SDLK_ESCAPE) { running = false; continue; }
        if (e.key.keysym.sym == SDLK_F1) {
          // Only on the press, and only between frames. Turning the mask off
          // stops *new* calls being intercepted; a resumable routine that is
          // parked mid-call still resumes through the port, because `cosim_step`
          // matches a suspension by its resume address rather than by the mask.
          // Finishing what was started is the correct behaviour and it is why
          // the toggle is safe to hit at any moment.
          if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            native = !native;
            if (native) cosim.enabled = selected;
            else cosim_mask_none(&cosim.enabled);
            printf("Substitution %s\n", native ? "on" : "off (stock)");
            fflush(stdout);
          }
          continue;
        }
        // A movie is driving the controller; the keyboard would fight it.
        if (!have_movie) {
          int b = key_to_button(e.key.keysym.sym);
          if (b >= 0) snes_setButtonState(snes, 1, b, e.type == SDL_KEYDOWN);
        }
      }
    }
    if (!running) break;

    if (paced) {
      if (audio) {
        while (running && SDL_GetQueuedAudioSize(audio) > audio_high_water)
          SDL_Delay(1);
      } else {
        for (;;) {
          double elapsed = (SDL_GetPerformanceCounter() - prev_frame) * 1000.0 / perf_freq;
          if (elapsed >= target_frame_ms) break;
          if (target_frame_ms - elapsed > 2.0) SDL_Delay(1); // busy-wait only the tail
        }
        prev_frame = SDL_GetPerformanceCounter();
      }
    }

    // Movies are indexed by this loop's own frame counter, which is what
    // `zamn_headless` does — and headless is the tool the corpus was fitted
    // against, so matching it is what makes a movie land in the same place here.
    // (`cosim_lockstep` indexes by `snes->frames` instead, and has to: its two
    // cores keep separate clocks, and a substituted routine returns on a cycle
    // budget rather than by executing the ROM's instructions, so their PPU frame
    // counts drift apart. There is one core here, so there is nothing to drift
    // from and the simpler counter is also the more faithful one — keying off
    // `snes->frames` put this frontend one frame away from headless at 2400.)
    if (have_movie) movie_apply(&movie, snes, (int)frame);

    // The substitution seam. Identical to `snes_runFrame` when the mask is
    // clear; when it is not, a registered routine's entry PC hands the call to
    // the C port, which runs against the core's own WRAM and returns through the
    // routine's own RTS/RTL.
    cosim_frame(&cosim);
    frame++;

    // Always queue this frame's audio (the queue is our clock; do not drop it).
    if (audio) {
      snes_setSamples(snes, audio_buf, SAMPLES_PER_FRAME);
      SDL_QueueAudio(audio, audio_buf, sizeof(audio_buf));
    }

    void* pixels; int pitch;
    if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0) {
      snes_setPixels(snes, (uint8_t*)pixels);
      SDL_UnlockTexture(tex);
    }
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);

    // The status the window can carry without a console. Twice a second is
    // often enough to read and rare enough not to matter.
    if (frame % 30 == 0) {
      long served, declined;
      substitution_totals(&cosim, &served, &declined);
      char share[64];
      share_summary(&cosim, share, sizeof share);
      if (native)
        snprintf(title, sizeof title,
                 "Zombies Ate My Neighbors — native: %s · %d routines, "
                 "%ld calls served, %ld declined",
                 share, routine_count, served, declined);
      else
        snprintf(title, sizeof title,
                 "Zombies Ate My Neighbors — stock (emulated; F1 for native)");
      SDL_SetWindowTitle(win, title);
    }
  }

  double secs = (double)(SDL_GetPerformanceCounter() - started) / (double)perf_freq;
  if (shot_path && !write_png(snes, shot_path))
    fprintf(stderr, "error: cannot write '%s'\n", shot_path);

  if (audio) SDL_CloseAudioDevice(audio);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();

  // What actually happened, in the harness's own words. `cosim_report` is the
  // same per-routine table `zamn_cosim run` prints, minus the verdicts a diff
  // would have filled in — there is no reference core here to compare against,
  // so the `checked` column means "substituted" and nothing is claimed beyond
  // that. The census names any handler a guard declined, which is the work list.
  printf("\n%ld frames in %.1f s (%.1f fps).\n", frame, secs,
         secs > 0 ? frame / secs : 0.0);
  cosim_report(&cosim);
  // The two percentages the table cannot give: 82 rows of `OK` say each ported
  // routine worked, and say nothing at all about what fraction of the game that
  // is. This does, for the session that was just played, and it is measured
  // over the whole run — including any stretch spent stock, because F1 toggling
  // to the emulator and back is exactly a stretch where the port ran nothing.
  cosim_share_report(&cosim);
  cosim_census_report();

  if (have_movie) movie_free(&movie);
  cosim_free(&cosim);
  snes_free(snes);
  free(rom);
  return 0;
}
