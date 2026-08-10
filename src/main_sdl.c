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
//             [--windowed] [--scale N] [--filter sharp|integer|linear]

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL.h>

#include "snes.h"

#include "analysis/movie.h"
#include "analysis/movie_apply.h"
#include "cosim/cosim.h"
#include "pace.h"
#include "present.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define FB_W 512
#define FB_H 480
// The live part of it. `ppu_putPixels` doubles the game's 224 scanlines into
// rows 16..463 and zeroes the sixteen above and below, so a third of a
// megapixel of every frame is blank by construction. Scaling that with the rest
// spends 6.7% of the screen's height enlarging black — measured on a real
// frame: first non-blank row 16, last 463 — so only this rectangle is drawn.
#define FB_TOP 16
#define FB_LIVE_H 448
#define AUDIO_FREQ 48000
#define SAMPLES_PER_FRAME (AUDIO_FREQ / 60) // 800
// How far a single frame's sample count may stray from that, for rate control.
// Four in eight hundred is half a percent — the same ceiling RetroArch uses for
// the same job, and about a tenth of the smallest pitch change a listener can
// pick out. It is a drift correction and must never become a pitch bend.
#define AUDIO_MAX_ADJUST 4
// How many samples the device asks for at a time. This was 2048, and 2048 is
// 42.7 ms — which was invisible while the queue was only feeding a speaker, and
// catastrophic once the loop paced itself off the queue's depth. It no longer
// does, but a shorter buffer still means a shallower queue and lower latency.
#define AUDIO_DEVICE_SAMPLES 512

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

// This frame, from the core to the screen. The scaling lives in `src/scale.h`
// (where it goes and how) and `src/present.h` (the SDL that does it), both so
// that `zamn_test_scale` and `zamn_test_present` can check them without a
// window between them.
static void present_frame(Present* p, Snes* snes) {
  void* pixels; int pitch;
  if (SDL_LockTexture(p->frame, NULL, &pixels, &pitch) == 0) {
    snes_setPixels(snes, (uint8_t*)pixels);
    SDL_UnlockTexture(p->frame);
  }
  present_draw(p);
  SDL_RenderPresent(p->ren);
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

// Push `bytes` of silence at the device. Used to establish the backlog at
// startup and to refill it if it ever collapses — in both cases the alternative
// is not "no silence", it is the device running dry and repeating or clicking.
static void queue_silence(SDL_AudioDeviceID dev, long bytes) {
  int16_t zeros[512];
  memset(zeros, 0, sizeof zeros);
  while (bytes > 0) {
    const long chunk = bytes < (long)sizeof zeros ? bytes : (long)sizeof zeros;
    SDL_QueueAudio(dev, zeros, (Uint32)chunk);
    bytes -= chunk;
  }
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
    "  --paced         Keep 60 Hz pacing under --frames. Turns a bounded run\n"
    "                  from a throughput measurement into a cadence one.\n"
    "  --shot <a.png>  Write the final frame as a PNG on the way out.\n"
    "  --no-audio      Skip the audio device (and pace off a timer instead).\n"
    "  --windowed      Start in a window. The default is fullscreen; F11 or\n"
    "                  Alt+Enter moves between them at any time.\n"
    "  --scale <N>     Size the window at N times 512x480, and start in it.\n"
    "                  Default 1, which is also the size F11 returns to.\n"
    "  --aspect <how>  4:3 (default) is the shape the game was composed for and\n"
    "                  what every emulator shows it in; square is 8:7, the\n"
    "                  framebuffer's own shape, narrower by 11%%. F3 toggles.\n"
    "  --filter <how>  How to fill a window that is not a whole multiple:\n"
    "                    sharp   (default) nearest up to the next whole\n"
    "                            multiple, then one bilinear step down. Uniform\n"
    "                            pixels, no shimmer, and the window is filled.\n"
    "                    integer only whole multiples; letterbox the remainder.\n"
    "                    linear  one bilinear step from 512x480. The blurry one.\n\n"
    "Controls: Arrows=D-pad  Z=B X=A A=Y S=X  Q=L W=R  Enter=Start RShift=Select\n"
    "          F1 = toggle native substitution   F2 = cycle scaling\n"
    "          F3 = toggle aspect ratio          F11/Alt+Enter = fullscreen\n"
    "          Esc = quit\n");
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
  ScaleMode scale_mode = SCALE_SHARP;
  // 4:3 by default, because that is the shape the game was composed for and the
  // shape every emulator shows it in. Square pixels are 8:7 — visibly narrow,
  // and about 11% less screen.
  AspectMode aspect_mode = ASPECT_43;
  int window_scale = 1;
  // Fullscreen is what playing it looks like, so it is the default and the flags
  // below are the ways of saying "not now". `--windowed` is the explicit one;
  // `--scale N` is asking for a window of a particular size, which is not a
  // request one can honour fullscreen; and `--frames N` is a batch run — a smoke
  // test or a throughput measurement — which has no business seizing the display
  // of whoever started it.
  bool fullscreen = true;
  // `--frames N` means "run N and stop", and it turns pacing off because a
  // throughput measurement wants to finish rather than to be watched. Those are
  // two decisions in one flag, and measuring the *cadence* needs the first
  // without the second: a bounded run, at real speed, that exits with a report.
  bool force_pacing = false;

  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
    else if (!strcmp(a, "--stock")) native = false;
    else if (!strcmp(a, "--no-audio")) want_audio = false;
    else if (!strcmp(a, "--windowed")) fullscreen = false;
    else if (!strcmp(a, "--paced")) force_pacing = true;
    else if (!strcmp(a, "-r") && i + 1 < argc) {
      if (only_count == (int)(sizeof only / sizeof *only)) {
        fprintf(stderr, "error: at most %d -r options\n\n",
                (int)(sizeof only / sizeof *only));
        usage();
        return 2;
      }
      only[only_count++] = argv[++i];
    }
    else if (!strcmp(a, "--aspect") && i + 1 < argc) {
      if (!aspect_parse(argv[++i], &aspect_mode)) {
        fprintf(stderr, "error: unknown aspect '%s' — want 4:3 or square\n\n",
                argv[i]);
        usage();
        return 2;
      }
    }
    else if (!strcmp(a, "--filter") && i + 1 < argc) {
      if (!scale_parse(argv[++i], &scale_mode)) {
        fprintf(stderr, "error: unknown filter '%s' — want sharp, integer or linear\n\n",
                argv[i]);
        usage();
        return 2;
      }
    }
    else if (!strcmp(a, "--scale") && i + 1 < argc) {
      // Capped at 8 for the same reason the stage is: past that the window is
      // larger than any display it could be on, and a typo should not open a
      // 32,768-pixel window that has to be killed from a task manager.
      window_scale = atoi(argv[++i]);
      if (window_scale < 1 || window_scale > SCALE_MAX_STAGE) {
        fprintf(stderr, "error: --scale wants 1..%d, got '%s'\n\n",
                SCALE_MAX_STAGE, argv[i]);
        usage();
        return 2;
      }
      fullscreen = false;
    }
    else if (!strcmp(a, "-m") && i + 1 < argc) movie_path = argv[++i];
    else if (!strcmp(a, "--shot") && i + 1 < argc) shot_path = argv[++i];
    else if (!strcmp(a, "--frames") && i + 1 < argc) {
      frame_limit = atol(argv[++i]);
      fullscreen = false;
    }
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
  // `FULLSCREEN_DESKTOP` rather than `FULLSCREEN`: it borrows the display at the
  // resolution it is already in instead of asking for a mode change. No black
  // screen while the monitor re-syncs, no windows on other displays getting
  // rearranged, and alt-tab comes straight back — none of which is worth trading
  // for an exclusive mode this game cannot use. The core hands over 512x480
  // whatever the display is, so fullscreen is a question about the destination
  // rectangle only, and `scale_plan` already answers that for arbitrary sizes.
  //
  // The size passed here is still the windowed size even when starting
  // fullscreen: it is what SDL restores on the way back out, so `--scale` sets
  // it whether or not the window is shown at that size first.
  Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  if (fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  // `--scale N` means N times the *picture*, which is 448 rows and whatever
  // width the chosen aspect makes of them — not N times the 512x480 buffer.
  // Sizing from the buffer would ask for a window 480N tall to show a 448N-tall
  // picture, so at 4:3 the frontend would letterbox its own window and then
  // shrink the game below 1:1 to fit the leftover, which is a blurrier picture
  // than 1x from a window that never claimed to be scaling at all.
  int win_h = FB_LIVE_H * window_scale, win_w = FB_W * window_scale;
  {
    int aw = 0, ah = 0;
    aspect_ratio(aspect_mode, FB_W, FB_LIVE_H, &aw, &ah);
    win_w = (int)((long)win_h * aw / ah);
  }
  SDL_Window* win = SDL_CreateWindow("Zombies Ate My Neighbors (native)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, win_flags);
  if (!win) {
    fprintf(stderr, "error: cannot open a window: %s\n", SDL_GetError());
    return 1;
  }
  // There is no mouse input in this game, so the pointer is only ever something
  // sitting on top of the picture. This is a process-wide SDL setting rather
  // than a per-window one, but SDL only draws the cursor while it is over a
  // window of its own — so windowed, it reappears the moment it leaves the
  // client area, and the title bar and close button keep theirs.
  SDL_ShowCursor(SDL_DISABLE);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  // ...and if there is no accelerated renderer, whatever SDL has. There is one
  // under the `dummy` video driver, which is how `--frames --shot` runs on a
  // machine with no display, and on a box with no GPU at all this is the
  // difference between a software-rendered game and none.
  //
  // Neither this nor the window was checked before the scaling work, and the
  // failure was silent rather than absent: every later call took a NULL
  // renderer, did nothing, and the frontend ran a whole movie showing an empty
  // window and reporting success. `present_init` returning false is what
  // surfaced it.
  if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
  if (!ren) {
    fprintf(stderr, "error: cannot create a renderer: %s\n", SDL_GetError());
    return 1;
  }
  // No `SDL_RenderSetLogicalSize`. It letterboxes to the right shape, which is
  // what this used to want, but it also owns the scale factor — and the whole
  // of `Present` is about choosing that factor deliberately. The letterboxing
  // it did is reproduced exactly, in output pixels, by `present_frame`.
  Present present;
  const SDL_Rect live = {0, FB_TOP, FB_W, FB_LIVE_H};
  if (!present_init(&present, ren, FB_W, FB_H, live, scale_mode, aspect_mode)) {
    fprintf(stderr, "error: cannot create the frame texture: %s\n", SDL_GetError());
    return 1;
  }
  if (scale_mode == SCALE_SHARP && !present.can_target)
    printf("note: this renderer cannot draw into a texture, so --filter sharp\n"
           "      falls back to nearest on a fractional window size.\n");

  SDL_AudioDeviceID audio = 0;
  // Sized for the largest frame rate control can ask for, not for the nominal
  // one: `snes_setSamples` writes exactly as many stereo pairs as it is told to
  // and asks nothing about the buffer behind the pointer.
  int16_t audio_buf[(SAMPLES_PER_FRAME + AUDIO_MAX_ADJUST) * 2];
  if (want_audio) {
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = AUDIO_FREQ; want.format = AUDIO_S16SYS; want.channels = 2;
    want.samples = AUDIO_DEVICE_SAMPLES;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);
  }

  // Frame pacing. This used to gate frame production on the audio queue
  // draining, on the reasoning that the device consumes samples at a fixed
  // 48000 Hz and so makes an exact clock. It does — on average, and only on
  // average. The device pulls `want.samples` at a time, so the queue does not
  // drain continuously, it drops in one lump per period; the loop then emitted
  // two or three frames as fast as it could and stalled until the next lump.
  // Measured over 600 frames that was a flawless 60.3 fps of which *no frame at
  // all* landed within a millisecond of the period: bursts 2-8 ms apart
  // separated by 26-50 ms of nothing, which the eye reads as about 23 fps.
  //
  // So the clock is now a deadline on the high-resolution timer, at a period
  // locked to the display where the display allows it (see `pace_period_ms`),
  // and the audio queue is corrected *to* that rather than the other way round.
  // `--frames` still opts out, because a throughput measurement wants to finish
  // rather than to be watched; `--paced` is how a bounded run keeps real time.
  const Uint32 bytes_per_frame = (Uint32)(SAMPLES_PER_FRAME * 2 * sizeof(int16_t));
  // Bytes of queued audio to milliseconds of sound: stereo 16-bit at 48 kHz is
  // 192 bytes per millisecond.
  const double audio_bytes_per_ms = (double)AUDIO_FREQ * 2.0 * sizeof(int16_t) / 1000.0;
  // Three frames of slack in front of the device. Enough that an ordinary
  // scheduling hiccup cannot empty it, small enough that the added latency is
  // under the frame period the game already costs.
  const long audio_target = (long)bytes_per_frame * 3;
  // ...and a ceiling, for the case rate control cannot fix: a display whose
  // refresh has no relationship to the console's rate at all. Half a percent
  // per frame cannot absorb that, and unbounded growth would end as seconds of
  // delay between a shot being fired and it being heard.
  const long audio_ceiling = (long)bytes_per_frame * 12;
  // Start the queue at that depth rather than climbing to it. Every frame puts
  // in as many samples as the device takes out, so an empty queue stays empty
  // and only rate control fills it — at four samples a frame, which is ten
  // seconds of running one hiccup away from underrun. (Measured: `audioQ min`
  // was 0.00 ms over a 30-second run.) Half a frame of silence at a moment when
  // nothing is happening yet costs nothing and skips the whole ramp.
  if (audio) queue_silence(audio, audio_target);
  // ...and a floor under it, because priming alone does not hold. Measured: the
  // device takes a large first bite as it starts its own pipeline, which drops
  // the backlog to about 4 ms — and since rate control moves at four samples a
  // frame it then needs some five hundred frames to climb back, all of them one
  // hiccup from silence. Below one frame of sound, refill to the target outright
  // rather than creep toward it.
  const long audio_floor = (long)bytes_per_frame;
  long audio_refills = 0;

  int refresh_hz = 0;
  {
    SDL_DisplayMode mode;
    const int idx = SDL_GetWindowDisplayIndex(win);
    if (idx >= 0 && SDL_GetCurrentDisplayMode(idx, &mode) == 0)
      refresh_hz = mode.refresh_rate;
  }
  const double content_hz = snes->palTiming ? PACE_FPS_PAL : PACE_FPS_NTSC;
  const double target_frame_ms = pace_period_ms(refresh_hz, content_hz);
  Pacer pacer;
  pacer_init(&pacer, target_frame_ms);

  printf("Controls: Arrows=D-pad  Z=B X=A A=Y S=X  Q=L W=R  Enter=Start RShift=Select\n"
         "          F1=toggle native substitution  F2=cycle scaling\n"
         "          F3=toggle aspect ratio         F11 or Alt+Enter=fullscreen\n"
         "          Esc=Quit\n");
  {
    // What the picture is actually being drawn into, which fullscreen makes a
    // question worth answering: the display's size, not the window size asked
    // for. `SDL_GetRendererOutputSize` is the same call `present_draw` scales
    // by, so this line and the picture cannot disagree.
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    int aw = 0, ah = 0;
    aspect_ratio(aspect_mode, FB_W, FB_LIVE_H, &aw, &ah);
    const ScalePlan plan = scale_plan(scale_mode, FB_W, FB_LIVE_H, aw, ah, ow,
                                      oh, present.can_target);
    printf("Display: %s, %dx%d, scaling %s, aspect %s\n",
           fullscreen ? "fullscreen" : "windowed", ow, oh,
           scale_name(scale_mode), aspect_name(aspect_mode));
    printf("Picture: %dx%d at %d,%d from %dx%d live pixels (%.0f%% of the"
           " screen)%s\n",
           plan.dst.w, plan.dst.h, plan.dst.x, plan.dst.y, FB_W, FB_LIVE_H,
           ow > 0 && oh > 0
               ? 100.0 * plan.dst.w * plan.dst.h / ((double)ow * oh)
               : 0.0,
           plan.stage_x ? "" : ", pixel-exact");
  if (!fullscreen)
    printf("Window: %dx%d (--scale %d)\n", win_w, win_h, window_scale);
  }
  printf("Pacing: %.3f ms/frame (%.2f fps)%s, display %d Hz, content %.4f Hz\n",
         target_frame_ms, 1000.0 / target_frame_ms,
         target_frame_ms == 1000.0 / content_hz ? " from the console"
                                                : " locked to the display",
         refresh_hz, content_hz);
  if (audio)
    printf("Audio: %d Hz, %d-sample device buffer, backlog target %.0f ms"
           " (primed to %.0f ms)\n",
           AUDIO_FREQ, AUDIO_DEVICE_SAMPLES,
           (double)audio_target / audio_bytes_per_ms,
           (double)SDL_GetQueuedAudioSize(audio) / audio_bytes_per_ms);
  else
    printf("Audio: none%s\n", want_audio ? " (device would not open)" : "");
  printf("Substitution: %s (%d routine%s registered)%s\n",
         native ? "on" : "off (stock)", routine_count,
         routine_count == 1 ? "" : "s",
         have_movie ? ", replaying a movie" : "");
  // Redirected to a file, this is block-buffered, and everything above it
  // describes the session that is about to start — so it wants to be readable
  // *during* the session and not only after a clean exit. A run that is killed
  // or crashes is exactly the run whose settings someone wants to look up.
  fflush(stdout);

  const Uint64 perf_freq = SDL_GetPerformanceFrequency();
  const bool paced = frame_limit == 0 || force_pacing;
  const Uint64 started = SDL_GetPerformanceCounter();

  // What the loop spends each frame on, and — the point of the exercise — how
  // evenly the frames come out the far end. See `src/pace.h` for why the mean
  // is not the interesting statistic.
  PaceHist h_interval, h_wait, h_emulate, h_draw, h_audio;
  pace_reset(&h_interval); pace_reset(&h_wait);
  pace_reset(&h_emulate);  pace_reset(&h_draw);
  pace_reset(&h_audio);
  Uint64 last_arrival = 0;
  #define PACE_MS(a, b) ((double)((b) - (a)) * 1000.0 / (double)perf_freq)

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
        if (e.key.keysym.sym == SDLK_F3) {
          // Aspect, on its own key, because the only honest way to judge it is
          // to flip between the two on the same frame of the same scene.
          if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            present.aspect =
                (AspectMode)((present.aspect + 1) % ASPECT_MODE_COUNT);
            printf("Aspect: %s\n", aspect_name(present.aspect));
            fflush(stdout);
          }
          continue;
        }
        if (e.key.keysym.sym == SDLK_F2) {
          // Cycling rather than a set of three keys, because the only way to
          // judge these is to watch one turn into the next on the same frame.
          if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            present.mode = (ScaleMode)((present.mode + 1) % SCALE_MODE_COUNT);
            printf("Scaling: %s\n", scale_name(present.mode));
            fflush(stdout);
          }
          continue;
        }
        // Fullscreen on F11, and on the Alt+Enter that every emulator has had
        // since DOS. That second spelling has to be recognised here rather than
        // in `key_to_button`, because Enter on its own is Start — and it has to
        // be matched *before* the key reaches that mapping, or toggling the
        // display would also press Start.
        {
          const bool alt_enter = e.key.keysym.sym == SDLK_RETURN &&
                                 (e.key.keysym.mod & KMOD_ALT) != 0;
          if (e.key.keysym.sym == SDLK_F11 || alt_enter) {
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
              const bool want = !fullscreen;
              if (SDL_SetWindowFullscreen(
                      win, want ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
                // Report it and keep the flag on what is actually on screen. A
                // toggle that silently believed it had worked would put the key
                // out of phase with the display for the rest of the session.
                fprintf(stderr, "cannot change display mode: %s\n", SDL_GetError());
              } else {
                fullscreen = want;
                printf("Display: %s\n", fullscreen ? "fullscreen" : "windowed");
              }
              fflush(stdout);
            }
            continue;
          }
        }
        // A movie is driving the controller; the keyboard would fight it.
        if (!have_movie) {
          int b = key_to_button(e.key.keysym.sym);
          if (b >= 0) snes_setButtonState(snes, 1, b, e.type == SDL_KEYDOWN);
        }
      }
    }
    if (!running) break;

    const Uint64 t_wait0 = SDL_GetPerformanceCounter();
    if (paced) {
      const double deadline = pacer_next(&pacer, PACE_MS(started, t_wait0));
      for (;;) {
        const double left = deadline - PACE_MS(started, SDL_GetPerformanceCounter());
        if (left <= 0.0) break;
        // Sleep away the bulk and spin the tail. `SDL_Delay(1)` cannot resolve
        // better than the scheduler's tick, and overshooting the deadline is
        // the jitter we came here to remove — so the last two milliseconds are
        // worth a busy-wait, which on this loop is about a tenth of one core.
        if (left > 2.0) SDL_Delay(1);
      }
    }
    const Uint64 t_wait1 = SDL_GetPerformanceCounter();
    pace_add(&h_wait, PACE_MS(t_wait0, t_wait1));

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

    // This frame's audio, resampled by however much it takes to hold the queue
    // at `audio_target`. `dsp_getSamples` already resamples the DSP's native
    // 534 samples per frame to whatever is asked for, so the count *is* the
    // rate-control knob and asking for 802 instead of 800 costs nothing.
    if (audio) {
      long queued = (long)SDL_GetQueuedAudioSize(audio);
      pace_add(&h_audio, (double)queued / audio_bytes_per_ms);
      if (queued < audio_floor) {
        queue_silence(audio, audio_target - queued);
        queued = audio_target;
        audio_refills++;
      }
      if (queued < audio_ceiling) {
        const int want_samples = pace_audio_samples(
            SAMPLES_PER_FRAME, queued, audio_target, 4, AUDIO_MAX_ADJUST);
        snes_setSamples(snes, audio_buf, want_samples);
        SDL_QueueAudio(audio, audio_buf,
                       (Uint32)want_samples * 2 * sizeof(int16_t));
      }
      // Over the ceiling, this frame's sound is dropped rather than deepening a
      // backlog that is already past what rate control can pull back. It is
      // audible, and it is the lesser of the two.
    }
    const Uint64 t_emul = SDL_GetPerformanceCounter();
    pace_add(&h_emulate, PACE_MS(t_wait1, t_emul));

    present_frame(&present, snes);

    // Measured after `SDL_RenderPresent` has returned, which is the moment the
    // frame is the screen's problem rather than ours — so `arrival` is the
    // cadence a player sees, not the cadence the loop intended.
    const Uint64 t_drawn = SDL_GetPerformanceCounter();
    pace_add(&h_draw, PACE_MS(t_emul, t_drawn));
    if (last_arrival) pace_add(&h_interval, PACE_MS(last_arrival, t_drawn));
    last_arrival = t_drawn;

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
  present_free(&present);
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
  // ...and the line above is exactly the statistic that cannot see a stutter,
  // so it is immediately followed by the one that can.
  pace_report(&h_interval, &h_wait, &h_emulate, &h_draw, &h_audio,
              target_frame_ms, (double)audio_target / audio_bytes_per_ms, paced);
  // One refill is the device starting up. More than that, in a run of any
  // length, means rate control is losing and the sound is being patched with
  // silence to cover it — which is worth saying out loud rather than leaving to
  // be noticed as an occasional click.
  if (audio_refills > 1)
    printf("  audio backlog refilled %ld times — rate control is not keeping up\n",
           audio_refills);
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
