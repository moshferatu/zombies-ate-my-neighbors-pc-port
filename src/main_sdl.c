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
// with the enable mask cleared.
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
//             [--frames N] [--shot out.png] [--no-audio] [--no-pads]
//             [--windowed] [--scale N] [--filter sharp|integer|linear]
//             [--level N] [--no-twin-stick] [--no-smooth] [--no-even]
//             [--dump-pictures prefix,frame]
//             [--no-high-scores] [--high-scores file] [--hitbox percent]
//             [--red-blood] [--flashing-radar] [--quick-at frame:save|load[:file]]...
//             [--invincible] [--invincible-neighbors] [--infinite-ammo]
//             [--infinite-lives] [--give-all] [--always-run]
//             [--config file] [--no-config] [--volume N] [--no-effect-overlay]
//             [--all-monster-sounds]
//             [--key-at frame:key[:frames]]... [--profile dir]
//
// The settings a player sets once, and every key and pad binding, are read
// from `zamn.ini` (`src/config.h`); an option here beats the file.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL.h>

#include "snes.h"

#include "analysis/movie.h"
#include "analysis/movie_apply.h"
#include "poke.h"
#include "cheats.h"
#include "hiscore.h"
#include "cosim/cosim.h"
#include "cosim/profile.h"
// For `player_set_aim` alone: `$80:D1FF` is substituted, so twin-stick aiming
// has to reach the port as well as the cartridge. See `src/twinstick.h`.
#include "port/player.h"
#include "layers.h"
#include "pace.h"
#include "pad.h"
#include "port/oam.h"
#include "present.h"
#include "present_layers.h"
#include "smooth.h"
#include "twinstick.h"
#include "widescreen.h"
#include "skipintro.h"
#include "levelstart.h"
#include "maskline.h"
#include "quicksave.h"
#include "config.h"
#include "sfx_overlay.h"
#include "bank_sfx.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

// What the desktop compositor says it put on the screen: how many of the
// frames handed to it were displayed, dropped, or missed their refresh. The
// frontend can time its own presents to the microsecond and still not know
// whether the picture reached the panel on the refresh it was meant for; this
// is the one report from the other side of that gap, and it is desktop-wide
// rather than per window, so it is read as a difference over the run with
// nothing else animating.
typedef struct {
  bool ok;
  unsigned long long frames, displayed, dropped, missed, refreshes;
} DwmStats;

static DwmStats dwm_stats(void) {
  DwmStats st;
  memset(&st, 0, sizeof st);
#ifdef _WIN32
  DWM_TIMING_INFO ti;
  memset(&ti, 0, sizeof ti);
  ti.cbSize = sizeof ti;
  if (DwmGetCompositionTimingInfo(NULL, &ti) == S_OK) {
    st.ok = true;
    st.frames = ti.cFrame;
    st.displayed = ti.cFramesDisplayed;
    st.dropped = ti.cFramesDropped;
    st.missed = ti.cFramesMissed;
    st.refreshes = ti.cRefreshesDisplayed;
  }
#endif
  return st;
}

// The buffer the core hands over is 512 wide unless the picture has been
// widened, in which case it is 2 output pixels per game column and everything
// here reads `fb_w`. `FB_W_MAX` is what gets allocated, once, at the widest the
// PPU will ever go — see `ppu_setWidescreen`.
#define FB_W_MAX (PPU_MAX_WIDTH * 2)
#define FB_H 480
static int fb_w = 512;
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

// The player's settings and bindings, from `zamn.ini` or the defaults: see
// `src/config.h`. The keyboard's table was here once, as a `switch`; it is
// `Config.key` now, the `BTN_*` names it is indexed by are still
// `analysis/movie.h`'s, and it has a second port's worth that is empty unless
// the player fills it.
//
// File-wide because the intro skip reads the quit key out of it as well as the
// main loop, and static because it carries two paths and every table.
static Config g_cfg;

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
  uint8_t* fb = (uint8_t*)malloc((size_t)FB_W_MAX * FB_H * 4);
  uint8_t* rgb = (uint8_t*)malloc((size_t)FB_W_MAX * FB_H * 3);
  if (!fb || !rgb) { free(fb); free(rgb); return false; }
  snes_setPixels(snes, fb);
  const int c = ZAMN_PIXEL_FORMAT;  // 0 for [B,G,R,X], 1 for [X,B,G,R]
  for (int i = 0; i < fb_w * FB_H; i++) {
    rgb[i * 3 + 0] = fb[i * 4 + c + 2];  // R
    rgb[i * 3 + 1] = fb[i * 4 + c + 1];  // G
    rgb[i * 3 + 2] = fb[i * 4 + c + 0];  // B
  }
  bool ok = stbi_write_png(path, fb_w, FB_H, 3, rgb, fb_w * 3) != 0;
  free(fb); free(rgb);
  return ok;
}

// This frame, from a PPU to the screen. The scaling lives in `src/scale.h`
// (where it goes and how) and `src/present.h` (the SDL that does it), both so
// that `zamn_test_scale` and `zamn_test_present` can check them without a
// window between them.
// `ppu` is the machine's own; `ppu_putPixels` is what `snes_setPixels` is.
//
// And a word over it for a moment -- SAVED, LOADED -- because a quick save
// changes nothing on screen and the console is behind a fullscreen window.
// Five-by-seven letters out of filled rectangles, only the ones the four
// messages use; drawn last, on whatever the picture was drawn with.
static struct {
  const char* text;
  Uint64 until;
  Uint8 r, g, b;
} g_notice;

static void notice_show(const char* text, Uint8 r, Uint8 g, Uint8 b) {
  g_notice.text = text;
  g_notice.until = SDL_GetPerformanceCounter() + SDL_GetPerformanceFrequency() * 5 / 4;
  g_notice.r = r; g_notice.g = g; g_notice.b = b;
}

static const uint8_t* notice_glyph(char ch) {
  static const struct { char ch; uint8_t rows[7]; } glyphs[] = {
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
  };
  for (size_t i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++)
    if (glyphs[i].ch == ch) return glyphs[i].rows;
  return NULL;  // a space
}

static void notice_draw(SDL_Renderer* ren) {
  if (!g_notice.text || SDL_GetPerformanceCounter() >= g_notice.until) return;
  int ow = 0, oh = 0;
  if (SDL_GetRendererOutputSize(ren, &ow, &oh) != 0) return;
  int px = oh / 300;  // 7 px at 2160 lines, 3 at 1080
  if (px < 2) px = 2;
  const int len = (int)strlen(g_notice.text);
  const int x0 = ow - (len * 6 + 3) * px, y0 = 4 * px;
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
  for (int pass = 0; pass < 2; pass++) {
    // A black copy a pixel down and right first, so it reads on any scene.
    if (pass == 0) SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    else SDL_SetRenderDrawColor(ren, g_notice.r, g_notice.g, g_notice.b, 255);
    const int off = pass == 0 ? px : 0;
    for (int i = 0; i < len; i++) {
      const uint8_t* rows = notice_glyph(g_notice.text[i]);
      if (!rows) continue;
      for (int y = 0; y < 7; y++)
        for (int x = 0; x < 5; x++)
          if (rows[y] & (0x10 >> x)) {
            const SDL_Rect r = {x0 + (i * 6 + x) * px + off, y0 + y * px + off, px, px};
            SDL_RenderFillRect(ren, &r);
          }
    }
  }
}

static void present_finish(Present* p) {
  notice_draw(p->ren);
  SDL_RenderPresent(p->ren);
}

// The same, from a picture already taken out of a PPU (`LayersFrame.fb`):
// what the smoothing shows on a frame it cannot draw as layers, where the
// machine itself is busy with the next tick. Does not present.
static void present_pixels(Present* p, const uint8_t* fb, int width) {
  void* pixels; int pitch;
  if (SDL_LockTexture(p->frame, NULL, &pixels, &pitch) == 0) {
    for (int y = 0; y < FB_H; y++)
      memcpy((uint8_t*)pixels + (size_t)y * pitch, fb + (size_t)y * width * 4,
             (size_t)width * 4);
    SDL_UnlockTexture(p->frame);
  }
  present_draw(p);
}

static void present_frame(Present* p, Ppu* ppu) {
  void* pixels; int pitch;
  if (SDL_LockTexture(p->frame, NULL, &pixels, &pitch) == 0) {
    // The core packs its rows tightly at the live width, so it can only write
    // straight into the texture when SDL agrees about the pitch. It always has
    // — the width is a multiple of four pixels — but "always has" is not a
    // guarantee SDL makes, and the failure would be a sheared picture rather
    // than anything that says what went wrong.
    if (pitch == fb_w * 4) {
      ppu_putPixels(ppu, (uint8_t*)pixels);
    } else {
      static uint8_t* scratch = NULL;
      if (!scratch) scratch = (uint8_t*)malloc((size_t)FB_W_MAX * FB_H * 4);
      if (scratch) {
        ppu_putPixels(ppu, scratch);
        for (int y = 0; y < FB_H; y++)
          memcpy((uint8_t*)pixels + (size_t)y * pitch,
                 scratch + (size_t)y * fb_w * 4, (size_t)fb_w * 4);
      }
    }
    SDL_UnlockTexture(p->frame);
  }
  present_draw(p);
}

// Capture the actual output before presenting, including plain frames and
// frames that fall back from the layer renderer during a fade.
static void dump_output(Present* p, const char* prefix, long frame, int phase) {
  int w, h;
  if (SDL_GetRendererOutputSize(p->ren, &w, &h) != 0 || w <= 0 || h <= 0) return;
  uint8_t* pixels = (uint8_t*)malloc((size_t)w * h * 3);
  if (pixels && SDL_RenderReadPixels(p->ren, NULL, SDL_PIXELFORMAT_RGB24, pixels, w * 3) == 0) {
    char path[600];
    snprintf(path, sizeof path, "%s.%ld.%d.gpu.png", prefix, frame, phase);
    stbi_write_png(path, w, h, 3, pixels, w * 3);
  }
  free(pixels);
}

// --- Pictures between ticks: the frontend's half of `src/layers.h` ----------
//
// The arithmetic is in the header. This holds two of its frames, alternating:
// the tick just finished and the one before it. The emulation thread takes a
// tick's picture apart the moment the tick is done, while the machine is
// stopped and before it is told to go again -- so the planes are read from
// the PPU by the thread that owns it, and the main thread never touches the
// machine. The main thread then links the new frame to the old one, uploads
// its planes once, and draws the list once per refresh.
//
// Two frames and not three, because a frame carries everything its pictures
// need from the one before it (`layers_link`): once linked, the older frame
// can be overwritten by the next tick while this one is being shown.
typedef struct {
  LayersFrame* frame[2];
  int cur;          // which of the two holds the latest tick
  LayersOp* ops;
  // The port's owner table as it stood at the end of the *last* tick, and
  // whether a pass had written it during that tick. Held for a tick because
  // the table describes the OAM buffer the game has just built, and the
  // picture being taken apart was drawn from the one before it -- see the
  // note on `SpriteOamOwners`.
  SpriteOamOwners held;
  bool held_fresh;
  uint32_t serial;  // the table's serial, as of the last capture
  // The widened picture's own sprites, which have owners too: `ws_owners`.
  const Widescreen* ws;
  // The game's table the last picture was taken apart with, which stands for
  // a picture the pass did not run for if its sprites are the same ones:
  // `layers_owners_stand`.
  SpriteOamOwners used;
  bool used_ok;
} Layers;

static bool layers_init(Layers* s) {
  memset(s, 0, sizeof *s);
  s->frame[0] = (LayersFrame*)calloc(1, sizeof(LayersFrame));
  s->frame[1] = (LayersFrame*)calloc(1, sizeof(LayersFrame));
  s->ops = (LayersOp*)malloc(sizeof(LayersOp) * LAYERS_MAX_OPS);
  return s->frame[0] && s->frame[1] && s->ops;
}

static void layers_free(Layers* s) {
  free(s->frame[0]);
  free(s->frame[1]);
  free(s->ops);
  memset(s, 0, sizeof *s);
}

// The machine has just finished a tick: take its picture apart, into the
// frame that is not the current one. Called with the emulation stopped. The
// port's owner table is passed along only if its pass ran this tick, which
// its serial says.
static void layers_take(Layers* s, Ppu* ppu) {
  SpriteOamOwners with;
  const SpriteOamOwners* game =
      s->held_fresh ? &s->held
      : s->used_ok && layers_owners_stand(s->frame[s->cur], ppu, s->used.rec) ? &s->used : NULL;
  if (game && game != &s->used) s->used = *game;
  s->used_ok = game != NULL;
  const SpriteOamOwners* own = ws_owners(s->ws, game, &with);
  layers_capture(s->frame[s->cur ^ 1], ppu, own ? own->rec : NULL, own ? own->ox : NULL,
                 own ? own->oy : NULL);
  s->held_fresh = sprite_oam_owners.serial != s->serial;
  s->serial = sprite_oam_owners.serial;
  s->held = sprite_oam_owners;
}

// ...and make that frame the current one, linked to the last. Main thread,
// between ticks.
static void layers_advance(Layers* s) {
  s->cur ^= 1;
  layers_link(s->frame[s->cur], s->frame[s->cur ^ 1]);
}

// The thread that runs the machine while the pictures are being shown.
//
// One tick takes about four milliseconds here and a 240 Hz refresh is four
// and a sixth, so a loop that emulated and then drew four pictures would miss
// the first refresh of every tick and show the cadence it was built to remove.
// The machine runs one tick ahead instead: told to go as soon as the last tick
// has been captured, and waited for when its pictures have all been shown.
// Nothing else touches the machine while it runs — the input is written before
// `go`, the audio is read after `done`, and the pictures come from the copy.
typedef struct {
  Cosim* cosim;
  Snes* snes;
  SfxOverlay* sfx;
  BankSfx* bank;
  Layers* layers;
  SDL_sem* go;
  SDL_sem* done;
  bool quit;
  bool capture;    // take the tick's picture apart when it is done
  double last_ms;  // how long the last tick took, for the report
  double take_ms;  // ...and how long taking it apart took
} EmuThread;

static int emu_thread_main(void* arg) {
  EmuThread* t = (EmuThread*)arg;
  const double freq = (double)SDL_GetPerformanceFrequency();
  for (;;) {
    SDL_SemWait(t->go);
    if (t->quit) return 0;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    cosim_frame(t->cosim);
    sfx_overlay_tick(t->sfx);
    bank_sfx_tick(t->bank);
    const Uint64 t1 = SDL_GetPerformanceCounter();
    t->last_ms = (double)(t1 - t0) * 1000.0 / freq;
    t->take_ms = 0.0;
    if (t->capture) {
      layers_take(t->layers, t->snes->ppu);
      t->take_ms = (double)(SDL_GetPerformanceCounter() - t1) * 1000.0 / freq;
    }
    SDL_SemPost(t->done);
  }
}

// A WIN32-subsystem binary has no console of its own, so `printf` goes nowhere
// even when it was launched from one. Borrowing the parent's is what makes
// anything this prints readable -- and it prints little unless `--verbose` asks
// for the whole banner and the report at exit; when there is no parent console
// — a double-click — nothing happens and the title bar carries the status
// instead.
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

// Boot to the title menu without making anybody watch it.
//
// The intro is Konami, LucasArts, a story screen and then the title — about
// nineteen seconds before the menu is up, which is a long time to sit through
// once and an absurd one to sit through on every launch of a build you are
// testing.
//
// **`--skip-intro` does not show them, and mostly does not run them either.**
// The game has a way past its own logos -- it is how a game over comes back
// to the title -- and `intro_bypass` (`src/skipintro.h`) takes it from the
// first boot. What is left is the game loading its sound with the screen off,
// about 220 frames, which are run as fast as the machine can; the hand-over
// is the first frame the game turns the screen on, so what comes up in the
// window is the title fading in.
//
// What follows is the older way, kept for a ROM whose opening is not the one
// `intro_bypass` knows: run all of the intro at full speed with Start mashed,
// and hand over with START/PASSWORD on screen.
//
// The input is not a recorded table but a rule, which is worth stating because
// it looked like a table for a long time: every movie in `movies/` mashes Start
// at frame 180 and every 24 frames after, held 8 and released 16, up to 1004.
// Checked against `movies/level1-pickups.zmv` — 71 events, no deviation — so
// the four constants below reproduce the corpus's boot half exactly, and a
// movie is not needed at runtime to do it.
#define INTRO_FIRST_PRESS  180
#define INTRO_PRESS_PERIOD 24
#define INTRO_PRESS_HOLD   8
#define INTRO_LAST_PRESS   1004
// Where to stop. The menu is drawn by 1050 and the screen is still sitting
// there at 1600, so there is a wide margin either side; 1150 is the figure the
// corpus already uses for "up and idle" — `tools/make_password_movie.py` waits
// until 1200 before it touches the D-pad — and taking the same number means the
// frontend and the movie generator cannot drift apart about when the menu is
// ready for input.
#define INTRO_TITLE_FRAME  1150
// With the logos bypassed: the screen is off until the title (222 here), and
// is not looked at before the boot's memory clear is over; and a machine that
// never turns it on is handed over anyway.
#define INTRO_BYPASS_FIRST 30
#define INTRO_BYPASS_LIMIT 900

// Runs the intro through `cosim_frame`, exactly as the main loop would, rather
// than through a bare core. Two reasons: the frames genuinely execute, so they
// belong in the substitution figures; and ported routines keep state, so
// booting stock and then switching to native would hand the port a machine it
// had not been watching.
static long skip_intro(Cosim* cosim, Snes* snes, SDL_Window* win, PadSet* pads,
                       bool* bypassed) {
  long f = 0;
  *bypassed = intro_bypass(snes->cart);
  const long last = *bypassed ? INTRO_BYPASS_LIMIT : INTRO_TITLE_FRAME;
  for (; f < last; f++) {
    // The title, about to fade in: the game has turned the screen on.
    if (*bypassed && f >= INTRO_BYPASS_FIRST && !snes->ppu->forcedBlank) break;
    const bool down = !*bypassed &&
        f >= INTRO_FIRST_PRESS && f <= INTRO_LAST_PRESS &&
        (f - INTRO_FIRST_PRESS) % INTRO_PRESS_PERIOD < INTRO_PRESS_HOLD;
    snes_setButtonState(snes, 1, BTN_START, down);
    cosim_frame(cosim);
    // Nothing is drawn — the whole point is not to see it — but the window
    // still has to answer the compositor, or a few seconds of not pumping gets
    // the process marked unresponsive and greyed out. The quit key still aborts, which
    // matters most on a machine slow enough for this to take a while.
    if ((f & 63) == 0) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        // Including the device events: this is several seconds of real time, and
        // a pad plugged in during it would otherwise be announced to an empty
        // queue and never seen again.
        pad_event(pads, &e);
        if (e.type == SDL_QUIT ||
            (e.type == SDL_KEYDOWN &&
             config_hotkey_of(&g_cfg, e.key.keysym.sym) == ACT_QUIT)) {
          snes_setButtonState(snes, 1, BTN_START, false);
          return f;
        }
      }
      SDL_SetWindowTitle(win, "Zombies Ate My Neighbors — skipping the intro…");
    }
  }
  // Never hand the player a held button.
  snes_setButtonState(snes, 1, BTN_START, false);
  return f;
}

// Push `bytes` of silence at the device. Used to establish the backlog at
// startup and to refill it if it ever collapses — in both cases the alternative
// is not "no silence", it is the device running dry and repeating or clicking.
// Everything the game reads at the top of a tick, written into the machine:
// the movie's frame, or the pads and the keyboard and the twin-stick aim.
//
// Movies are indexed by the loop's own frame counter, which is what
// `zamn_headless` does — and headless is the tool the corpus was fitted
// against, so matching it is what makes a movie land in the same place here.
// (`cosim_lockstep` indexes by `snes->frames` instead, and has to: its two
// cores keep separate clocks, and a substituted routine returns on a cycle
// budget rather than by executing the ROM's instructions, so their PPU frame
// counts drift apart. There is one core here, so there is nothing to drift
// from and the simpler counter is also the more faithful one — keying off
// `snes->frames` put this frontend one frame away from headless at 2400.)
static void tick_input(Snes* snes, Movie* movie, bool have_movie, long frame,
                       PadSet* pads, const uint16_t key_held[MOVIE_PORTS],
                       bool twin_stick) {
  if (have_movie) {
    movie_apply(movie, snes, (int)frame);
    return;
  }
  // Both controllers, from both kinds of input, written once. The keyboard
  // is port 1 unless the player has bound keys for port 2 as well; a pad takes
  // the lowest free port, so one pad plays alone, two play together, and a
  // pad plus the keyboard share port 1. Writing the whole 12-bit state every frame — the
  // same thing `movie_apply` does — is what makes an unplugged pad release
  // its buttons rather than leave them held.
  uint16_t held[PAD_MAX];
  // A pad's hotkeys, quit's Start+Select among them, are noted on the way
  // past, and a chord held has already been taken out of `held`.
  pad_poll(pads, held);
  // The next weapon or item and the one before, which are not buttons the
  // SNES has: asked of the port, which does them in the player's own frame
  // (`player_cycle_request`). A request nothing answers is let go of.
  player_cycle_age();
  for (int p = 0; p < MOVIE_PORTS; p++) {
    const uint8_t pressed = pads->cycle_pressed[p];
    pads->cycle_pressed[p] = 0;
    for (int c = 0; c < PAD_CYCLE_COUNT; c++)
      if (pressed & (1u << c))
        player_cycle_request((uint16_t)(p * 2), c >> 1 ? PSN_CYCLE_ITEM : PSN_CYCLE_WEAPON,
                             c & 1 ? -1 : +1);
  }
  for (int p = 0; p < MOVIE_PORTS; p++) held[p] |= key_held[p];
  // The right stick, unless it was turned off: an aim direction into the
  // cartridge for the stub at `$80:D250` to pick up, and `Y` — this game's
  // fire button — pressed for as long as the stick is out. Written before
  // the frame that reads it, like every other input here, and left at zero
  // for a port with no pad, which hands `$26` straight back to the game.
  //
  // **Both places, because there are two of them.** `$80:D1FF` is a
  // substituted routine, so the nine patched bytes at `$80:D250` are the
  // path `--stock` takes and `player_set_aim` is the path the default
  // build takes. Arming one and not the other is a flag that works in one
  // mode and silently does nothing in the other, which is worse than a flag
  // that does not work at all.
  if (twin_stick) {
    uint16_t aim[PAD_MAX];
    pad_aim(pads, aim);
    for (int p = 0; p < MOVIE_PORTS; p++) {
      held[p] |= twin_apply(snes->cart->rom, p, aim[p]);
      // Read back rather than worked out again, so the two paths cannot
      // disagree: the port is armed with the exact word the 65816 would
      // have fetched. The player index is doubled, as the routine's own is.
      player_set_aim((uint16_t)(p * 2), twin_aim_of(snes->cart->rom, p));
    }
  }
  for (int p = 0; p < MOVIE_PORTS; p++)
    for (int b = 0; b < 12; b++)
      snes_setButtonState(snes, p + 1, b, (held[p] >> b) & 1);
}

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
    "  --config <file> Read the settings and the bindings from this file and not\n"
    "                  from zamn.ini, which is looked for here and then beside\n"
    "                  the executable, and written with the defaults when there\n"
    "                  is none. An option beats the file. See src/config.h.\n"
    "  --no-config     Read no file and write none: the defaults and the options.\n"
    "  --no-skip-intro --smooth --no-red-blood --pads --audio\n"
    "                  No to what the file says, for this run.\n"
    "  --volume <N>    0 to 100, default 100.\n"
    "  --stock         Do not substitute; run the ROM under the core, as the\n"
    "                  Phase 0b baseline did.\n"
    "  -r <routine>    Substitute only this one. Repeatable; default is every\n"
    "                  routine `zamn_cosim list` reports.\n"
    "  -m <movie.zmv>  Replay a recorded movie instead of reading the keyboard.\n"
    "  --frames <N>    Run N frames and exit, uncapped rather than paced at 60 Hz.\n"
    "  --paced         Keep 60 Hz pacing under --frames. Turns a bounded run\n"
    "                  from a throughput measurement into a cadence one.\n"
    "  --verbose       Say everything: the ROM, the renderer, every binding,\n"
    "                  the display, the pacing, the audio device and the\n"
    "                  substitution at the start, the top scores file as it\n"
    "                  is read and written, and the frame cadence, the\n"
    "                  per-routine table and the native share at exit.\n"
    "                  Without it the console carries what changes the game\n"
    "                  or went wrong, and one line at exit. On by itself\n"
    "                  under --frames and -m, which are measurements.\n"
    "  --shot <a.png>  Write the final frame as a PNG on the way out.\n"
    "  --no-audio      Skip the audio device (and pace off a timer instead).\n"
    "  --no-effect-overlay\n"
    "                  Let the sound driver drop and cut short sound effects when\n"
    "                  too much is playing, as the console does. --effect-overlay\n"
    "                  is the other way. See src/sfx_overlay.h.\n"
    "  --no-all-monster-sounds\n"
    "                  Leave out the monster sounds the game drops when the\n"
    "                  level has another monster's samples loaded (the chainsaw\n"
    "                  maniacs and werewolves on Monster Phobia, the dolls on\n"
    "                  their own warehouse), as the console does.\n"
    "                  --all-monster-sounds is the other way. See src/bank_sfx.h.\n"
    "  --no-pads       Ignore game controllers and read only the keyboard.\n"
    "  --skip-intro    Run the logos and the story screen at full speed and\n"
    "                  hand over at the title menu. Cannot be combined with -m:\n"
    "                  a movie drives from reset and contains its own boot.\n"
    "  --level <N>     Start a new game on record N (0..55) instead of level 1,\n"
    "                  and carry on from there as the game would. 1..48 are the\n"
    "                  numbered levels; 49 is the credit roll, after which the\n"
    "                  title comes back; 0 and 50..55 are the seven bonus rooms,\n"
    "                  and finishing one of those goes to the level its door\n"
    "                  would have led out to. Applies to every new game the\n"
    "                  session starts, including after a game over.\n"
    "  --no-twin-stick Give the right stick back to the game, which is to say to\n"
    "                  nothing: stock, it reads no second stick. On by default,\n"
    "                  the right stick fires the held weapon in the direction it\n"
    "                  is pushed while the left one goes on steering, so you can\n"
    "                  walk one way and shoot the other. A pad-only feature —\n"
    "                  the keyboard has one D-pad and is unaffected either way.\n"
    "  --no-smooth     Show the game's frames only, as the console did. On by\n"
    "                  default on any display faster than the game, every\n"
    "                  refresh is a picture of its own, with the scrolling and\n"
    "                  the sprites eased to where that refresh falls between\n"
    "                  the last frame and this one — four pictures per frame at\n"
    "                  240 Hz, twelve per five frames at 144 — at the cost of\n"
    "                  showing each frame about one frame late. The game itself\n"
    "                  runs at its own speed either way. F6 toggles. See\n"
    "                  src/layers.h.\n"
    "  --no-even       Ease the pictures straight from one frame to the next.\n"
    "                  By default uneven steps are evened out, because the game\n"
    "                  moves in whole pixels at speeds that are not — a walk of\n"
    "                  2, 1, 2, 1 pixels, a chase of 2, 0, 2, 0 — and eased\n"
    "                  straight that is a speed that changes thirty times a\n"
    "                  second, which shows as a shimmer, worst on diagonals.\n"
    "                  Each thing is drawn up to half a pixel from where it is,\n"
    "                  on the steady line its steps stand either side of. Adds\n"
    "                  no delay; a thing that stops settles by that half pixel\n"
    "                  one frame later.\n"
    "  --dump-pictures <prefix,frame[,last]>\n"
    "                  Write the pictures of a frame, or a range, as PNGs, twice: as the\n"
    "                  renderer drew them, read back, and as src/layers.h\n"
    "                  draws the same list in software. With smoothing off or\n"
    "                  during a fallback, writes only the rendered output.\n"
    "  --trace-frames <csv>\n"
    "                  Log presentation times, emulation times, core frame\n"
    "                  numbers and BG1 scroll without capturing screenshots.\n"
    "  --refresh <hz>  Believe this refresh rate rather than the one the system\n"
    "                  reports, for a display it reports wrongly — and for\n"
    "                  trying the pacing of a display that is not attached.\n"
    "  --fullscreen    Fullscreen even for a --frames or --scale run, which\n"
    "                  otherwise open a window: how to measure the screen as\n"
    "                  it is played.\n"
    "  --poke <spec>   frame[+]:addr=value[.b], as zamn_headless takes it: write\n"
    "                  a WRAM word (or byte) at a frame, or from it on with +.\n"
    "  --quick-at <frame:save|load[:file]>\n"
    "                  F5 or F9 from the command line, at a frame, to a file of\n"
    "                  its own if one is named. For testing; up to 8.\n"
    "  --key-at <frame:key[:frames]>\n"
    "                  A key from the command line, down at a frame and up 8\n"
    "                  frames later unless said, through SDL's event queue and\n"
    "                  so through the bindings. For testing; up to 64.\n"
    "  --profile <dir> Count what the 65816 still executes, instruction by\n"
    "                  instruction, and add it to the profile in <dir> at exit,\n"
    "                  in zamn_trace's formats. What the port serves never runs,\n"
    "                  so this is what is left to port, from real play; rank it\n"
    "                  with tools/native_share.py --residue <dir>. The directory\n"
    "                  accumulates across sessions. See src/cosim/profile.h.\n"
    "  --red-blood     The game over's curtain of purple slime is red, and blood,\n"
    "                  as it is on the Mega Drive. Nothing else changes colour.\n"
    "  --flashing-radar\n"
    "                  The survivor radar shows one neighbour at a time, in turn,\n"
    "                  as the console does, instead of all of them at once.\n"
    "                  --steady-radar is the other way. See src/radar.h.\n"
    "  --hitbox <pct>  How far a player reaches for a pickup or a neighbour, and\n"
    "                  a weapon for a creature, as a percentage of the game's\n"
    "                  own 16-pixel box: 100 to 200, default 100 (and 100 under\n"
    "                  -m whatever zamn.ini says, as the movie was made at 100).\n"
    "                  Creatures reach for players and neighbours as far as they\n"
    "                  ever did.\n"
    "  --no-high-scores  Do not keep the top scores from run to run. They are\n"
    "                  kept by default in zamn.hiscore in the working\n"
    "                  directory, which the cartridge could not do. Not\n"
    "                  under -m.\n"
    "  --high-scores <file>  Keep them in this file instead -- under -m too,\n"
    "                  which is how the feature is tested.\n"
    "  --windowed      Start in a window. The default is fullscreen; F11 or\n"
    "                  Alt+Enter moves between them at any time.\n"
    "  --scale <N>     Size the window at N times 512x480, and start in it.\n"
    "                  Default 1, which is also the size F11 returns to.\n"
    "  --widescreen <r> off (default), 16:9, 16:10, 21:9 or auto. Draws columns\n"
    "                  either side of the console's 256 instead of stretching them:\n"
    "                  more level is visible, and the status panels move to\n"
    "                  the two edges. auto is whichever fits the display when\n"
    "                  fullscreen, and off in a window.\n"
    "  --filter <how>  How to fill a window that is not a whole multiple:\n"
    "                    sharp   (default) nearest up to the next whole\n"
    "                            multiple, then one bilinear step down. Uniform\n"
    "                            pixels, no shimmer, and the window is filled.\n"
    "                    integer only whole multiples; letterbox the remainder.\n"
    "                    linear  one bilinear step from 512x480. The blurry one.\n\n"
    "Cheats, each off unless asked for here or in zamn.ini's [cheats]. They work under\n"
    "--stock as well. While any is on the top scores are read and not written.\n"
    "  --invincible    Nothing hurts a player: no flinch, no health lost.\n"
    "  --invincible-neighbors  Nothing hurts a neighbour, and the tourists do\n"
    "                  not turn into werewolves. They can still be rescued.\n"
    "  --infinite-ammo Weapons and items are never used up -- keys too -- and\n"
    "                  the HUD shows the most the game lets anybody carry, 999\n"
    "                  and 99. Gives nothing: a weapon not held stays not held.\n"
    "  --infinite-lives  Dying does not cost a life.\n"
    "  --give-all      Every weapon and every item, 999 and 99 of them, when a\n"
    "                  game starts and when a quick save is loaded. Once: they\n"
    "                  run out unless --infinite-ammo is on as well.\n"
    "  --always-run    The running shoes, always.\n"
    "                  See src/cheats.h.\n\n"
    "Controls, unless zamn.ini binds them otherwise, which it can for every one\n"
    "of them, pad inputs included:\n"
    "          Arrows=D-pad  Z=B X=A A=Y S=X  Q=L W=R  Enter=Start RShift=Select\n"
    "          F5 = quick save   F9 = quick load   F6 = toggle smoothing\n"
    "          F11/Alt+Enter = fullscreen        Esc = quit\n\n"
    "Controllers: any pad SDL recognises, hot-pluggable, first two take the two\n"
    "          SNES ports. Face buttons are positional — the bottom one is B,\n"
    "          the left one is Y, which is this game's fire button. R1 and L1\n"
    "          are the next item and the one before, R2 and L2 the next weapon\n"
    "          and the one before; the touchpad's click or L3 brings up the\n"
    "          radar (the SNES's L and R). Left stick or D-pad steers.\n"
    "          Start+Select quits (Options+Share on a DualSense) unless\n"
    "          zamn.ini rebinds it. The right\n"
    "          stick aims and fires while the left one still steers, unless\n"
    "          --no-twin-stick takes that back. Drop a\n"
    "          gamecontrollerdb.txt beside the executable for anything SDL maps\n"
    "          wrongly. See src/pad.h.\n");
}

int main(int argc, char** argv) {
  attach_parent_console();

  // The player's settings (`src/config.h`), read before the options so that
  // every option below starts from what the file says and overrules it.
  const char* config_asked = NULL;
  bool config_off = false, config_found = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--no-config")) config_off = true;
    else if (!strcmp(argv[i], "--config") && i + 1 < argc) config_asked = argv[++i];
  }
  config_defaults(&g_cfg);
  {
    char config_path[CONFIG_PATH_MAX];
    if (!config_off && config_locate(config_asked, config_path, sizeof config_path)) {
      config_found = config_load(&g_cfg, config_path);
      if (!config_found) {
        fprintf(stderr, "error: cannot read the config '%s'\n", config_path);
        return 1;
      }
    }
  }

  const char* rom_path = NULL;
  const char* movie_path = NULL;
  const char* shot_path = NULL;
  // `--dump-pictures prefix,frame`: the pictures of one tick, read back from
  // the renderer after they were drawn, and the same list drawn in software
  // at the same scale, as PNGs -- the check that the GPU draws the list the
  // way `layers_render` does, which no headless test can make.
  const char* dump_prefix = NULL;
  long dump_first = -1, dump_last = -1;
  // `--poke`, as the headless tool has it: a way to a state -- a game over --
  // that no movie in the corpus reaches, for looking at how it is drawn.
  PokeList pokes = {{{0}}, 0};
  // `--invincible` and the rest: see `src/cheats.h`. And `zamn.ini`'s [cheats],
  // which are in the same order.
  _Static_assert(CONFIG_CHEATS == CHEAT_COUNT, "[cheats] is not the cheats");
  Cheats cheats;
  cheats_init(&cheats);
  // `--quick-at frame:save|load[:file]`: F5 or F9 pressed by the command line
  // at a frame, to a file of its own if one is named. For testing the quick
  // save without a keyboard, and the one way to it under a movie.
  struct { long frame; bool load; const char* file; } quick_at[8];
  int quick_ats = 0;
  // `--key-at frame:key[:frames]`: a key pressed by the command line at a
  // frame and let go some frames later (8 unless said), put on SDL's own event
  // queue -- so it goes through everything a real key does: the event loop,
  // the bindings, the actions. How the bindings are tested without a hand.
  struct { long frame, hold; SDL_Keycode key; int state; } key_at[64];
  int key_ats = 0;
  // The top scores, kept from run to run -- see hiscore.h.
  // `--hitbox`: see `actor_overlap_reach` in port/oam.h. 0 is "not said".
  int hitbox_pct = 0;
  // `--red-blood`: see `src/blood.h`.
  bool red_blood = g_cfg.red_blood;
  // `--flashing-radar`: see `src/radar.h`.
  bool radar_flash = g_cfg.radar_flash;
  bool hiscore_on = g_cfg.high_scores;
  const char* hiscore_path = NULL;
  // Room for every routine in the registry and then some. It was 32, which
  // was more than the registry held when it was written and is not any more:
  // past the cap the `-r` was dropped and its argument fell through to the
  // positional check, so asking for "everything except two" failed with
  // `unexpected argument 'enemy_cdde'` and no hint that a limit existed. Then
  // it was 128, which the registry passed too; it is the registry's own cap now.
  const char* only[COSIM_MAX_ROUTINES];
  int only_count = 0;
  long frame_limit = 0;
  bool native = true, want_audio = g_cfg.audio, want_pads = g_cfg.pads;
  int volume = g_cfg.volume;
  bool effect_overlay = g_cfg.effect_overlay;
  bool all_monster_sounds = g_cfg.all_monster_sounds;
  ScaleMode scale_mode = g_cfg.filter;
  // Off by default. Widescreen is the PPU drawing columns the console never
  // drew, and however good it looks it is not what the game is — so it is asked
  // for, and every measurement this project makes is made without it. This is
  // the setting, which may be `auto`; `wide`, further down, is the width.
  WideMode wide_setting = g_cfg.widescreen;
  int window_scale = g_cfg.window_scale;
  // Fullscreen is what playing it looks like, so it is the default and the flags
  // below are the ways of saying "not now". `--windowed` is the explicit one;
  // `--scale N` is asking for a window of a particular size, which is not a
  // request one can honour fullscreen; and `--frames N` is a batch run — a smoke
  // test or a throughput measurement — which has no business seizing the display
  // of whoever started it.
  bool fullscreen = g_cfg.fullscreen;
  bool fullscreen_asked = false;
  int refresh_asked = 0;
  bool frames_given = false;
  // `--frames N` means "run N and stop", and it turns pacing off because a
  // throughput measurement wants to finish rather than to be watched. Those are
  // two decisions in one flag, and measuring the *cadence* needs the first
  // without the second: a bounded run, at real speed, that exits with a report.
  bool force_pacing = false;
  // The whole banner and the whole report, or a console that says only what
  // changes the game or went wrong. Off unless asked for, and on by itself for
  // a bounded run or a movie, which are measurements and want the figures.
  bool verbose = false;
  // The file's `skip_intro` is looked at after the options, because under a
  // movie it is ignored where the option is an error.
  bool skip_the_intro = false, skip_intro_refused = false;
  // -1, not 0: 0 is a level — the bonus room the BCDF password loads — so it
  // cannot double as "not asked for". Without the flag the ROM is left exactly
  // as it came off disk.
  int start_level = -1;
  // On by default, which widescreen is not, and the difference is what the
  // feature takes away. Widescreen draws columns the console never drew, so it
  // is on screen whether or not anyone wanted it. This claims a stick the stock
  // game does not read at all: leave it centred and the cartridge is byte for
  // byte the one that shipped — measured, a 4,600-frame movie renders to the
  // same PNG with it and without — and the keyboard never reaches it. So the
  // player who does not want it pays nothing and need say nothing, and the flag
  // is the way to say no.
  bool twin_stick = g_cfg.twin_stick;
  // ...but `--twin-stick` still parses, and it is not a synonym. Asking for it
  // makes a cartridge that cannot take the patch an error; the default settles
  // for a warning, because a default has no business refusing to start a ROM it
  // was never told to change.
  bool twin_asked = false;
  // On by default, like twin stick, and for the same reason: it changes what
  // is shown and not what the game does, the machine runs at its own rate to
  // the tick, and it costs nothing on a display that cannot use it. The flag
  // is for the player who would rather see each frame the moment it exists.
  bool smooth = g_cfg.smoothing;
  bool even = true;
  const char* trace_frames_path = NULL;
  const char* profile_dir = NULL;

  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
    else if (!strcmp(a, "--no-config")) {}
    else if (!strcmp(a, "--config") && i + 1 < argc) i++;  // both read above
    else if (!strcmp(a, "--stock")) native = false;
    else if (!strcmp(a, "--no-audio")) want_audio = false;
    else if (!strcmp(a, "--audio")) want_audio = true;
    else if (!strcmp(a, "--no-effect-overlay")) effect_overlay = false;
    else if (!strcmp(a, "--effect-overlay")) effect_overlay = true;
    else if (!strcmp(a, "--all-monster-sounds")) all_monster_sounds = true;
    else if (!strcmp(a, "--no-all-monster-sounds")) all_monster_sounds = false;
    else if (!strcmp(a, "--volume") && i + 1 < argc) {
      if (!config_int(argv[++i], 0, 100, &volume)) {
        fprintf(stderr, "error: --volume wants 0..100, got '%s'\n\n", argv[i]);
        usage();
        return 2;
      }
    }
    else if (!strcmp(a, "--no-pads")) want_pads = false;
    else if (!strcmp(a, "--pads")) want_pads = true;
    else if (!strcmp(a, "--windowed")) fullscreen = false;
    // ...and the way to measure a bounded run on the screen as it is played:
    // `--frames` and `--scale` say windowed, and this, given after them, says
    // fullscreen after all.
    else if (!strcmp(a, "--fullscreen")) fullscreen_asked = true;
    else if (!strcmp(a, "--refresh") && i + 1 < argc) refresh_asked = atoi(argv[++i]);
    else if (!strcmp(a, "--skip-intro")) skip_the_intro = true;
    else if (!strcmp(a, "--no-skip-intro")) skip_intro_refused = true;
    else if (!strcmp(a, "--paced")) force_pacing = true;
    else if (!strcmp(a, "--verbose")) verbose = true;
    else if (!strcmp(a, "--no-twin-stick")) twin_stick = false;
    else if (!strcmp(a, "--twin-stick")) twin_stick = twin_asked = true;
    else if (!strcmp(a, "--no-smooth")) smooth = false;
    else if (!strcmp(a, "--smooth")) smooth = true;
    else if (!strcmp(a, "--no-even")) even = false;
    else if (!strcmp(a, "--trace-frames") && i + 1 < argc) trace_frames_path = argv[++i];
    else if (!strcmp(a, "--profile") && i + 1 < argc) profile_dir = argv[++i];
    else if (!strcmp(a, "-r") && i + 1 < argc) {
      if (only_count == (int)(sizeof only / sizeof *only)) {
        fprintf(stderr, "error: at most %d -r options\n\n",
                (int)(sizeof only / sizeof *only));
        usage();
        return 2;
      }
      only[only_count++] = argv[++i];
    }
    else if (!strcmp(a, "--widescreen") && i + 1 < argc) {
      if (!wide_setting_parse(argv[++i], &wide_setting)) {
        fprintf(stderr, "error: unknown widescreen '%s' — want off, 16:9, 16:10, 21:9 or auto\n\n",
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
    else if (!strcmp(a, "--level") && i + 1 < argc) {
      // Not `atoi`: it reads "abc" as 0, and 0 is a level here rather than a
      // way of saying nothing, so the argument has to be a number and no more.
      char* end = NULL;
      const long v = strtol(argv[++i], &end, 10);
      if (end == argv[i] || *end || v < LEVEL_FIRST || v > LEVEL_LAST) {
        fprintf(stderr, "error: --level wants %d..%d, got '%s'\n\n",
                LEVEL_FIRST, LEVEL_LAST, argv[i]);
        usage();
        return 2;
      }
      start_level = (int)v;
    }
    else if (!strcmp(a, "-m") && i + 1 < argc) movie_path = argv[++i];
    else if (!strcmp(a, "--poke") && i + 1 < argc) { if (!poke_parse(&pokes, argv[++i])) return 2; }
    else if (!strcmp(a, "--quick-at") && i + 1 < argc) {
      char* end = NULL;
      const long at = strtol(argv[++i], &end, 10);
      const bool save = end && !strncmp(end, ":save", 5), load = end && !strncmp(end, ":load", 5);
      if (quick_ats == 8 || at <= 0 || !(save || load) || (end[5] != 0 && end[5] != ':')) {
        fprintf(stderr, "error: --quick-at wants frame:save or frame:load, and a :file if any (8 at most), got '%s'\n\n", argv[i]);
        usage();
        return 2;
      }
      quick_at[quick_ats].frame = at;
      quick_at[quick_ats].load = load;
      quick_at[quick_ats].file = end[5] == ':' ? end + 6 : NULL;
      quick_ats++;
    }
    else if (!strcmp(a, "--key-at") && i + 1 < argc) {
      char spec[96];
      snprintf(spec, sizeof spec, "%s", argv[++i]);
      char* colon = strchr(spec, ':');
      char* colon2 = colon ? strchr(colon + 1, ':') : NULL;
      if (colon) *colon = 0;
      if (colon2) *colon2 = 0;
      const long at = atol(spec), hold = colon2 ? atol(colon2 + 1) : 8;
      const SDL_Keycode k = colon ? config_key(colon + 1) : SDLK_UNKNOWN;
      if (key_ats == (int)(sizeof key_at / sizeof *key_at) || at <= 0 || hold <= 0 || k == SDLK_UNKNOWN) {
        fprintf(stderr, "error: --key-at wants frame:key or frame:key:frames (%d at most), got '%s'\n\n",
                (int)(sizeof key_at / sizeof *key_at), argv[i]);
        usage();
        return 2;
      }
      key_at[key_ats].frame = at;
      key_at[key_ats].hold = hold;
      key_at[key_ats].key = k;
      key_at[key_ats].state = 0;
      key_ats++;
    }
    else if (!strcmp(a, "--hitbox") && i + 1 < argc) {
      char* end = NULL;
      const long v = strtol(argv[++i], &end, 10);
      if (end == argv[i] || *end || v < 100 || v > 200) {
        fprintf(stderr, "error: --hitbox wants 100..200, got '%s'\n\n", argv[i]);
        usage();
        return 2;
      }
      hitbox_pct = (int)v;
    }
    else if (!strcmp(a, "--red-blood")) red_blood = true;
    else if (!strcmp(a, "--no-red-blood")) red_blood = false;
    else if (!strcmp(a, "--flashing-radar")) radar_flash = true;
    else if (!strcmp(a, "--steady-radar")) radar_flash = false;
    else if (cheats_flag(&cheats, a)) {}
    else if (!strcmp(a, "--no-high-scores")) hiscore_on = false;
    else if (!strcmp(a, "--high-scores") && i + 1 < argc) hiscore_path = argv[++i];
    else if (!strcmp(a, "--shot") && i + 1 < argc) shot_path = argv[++i];
    else if (!strcmp(a, "--dump-pictures") && i + 1 < argc) {
      static char dump_buf[512];
      snprintf(dump_buf, sizeof dump_buf, "%s", argv[++i]);
      // prefix,frame or prefix,first,last
      char* comma = strchr(dump_buf, ',');
      if (comma) {
        *comma = 0;
        dump_first = dump_last = atol(comma + 1);
        char* comma2 = strchr(comma + 1, ',');
        if (comma2) dump_last = atol(comma2 + 1);
      }
      dump_prefix = dump_buf;
    }
    else if (!strcmp(a, "--frames") && i + 1 < argc) {
      frame_limit = atol(argv[++i]);
      fullscreen = false;
      frames_given = true;
    }
    else if (a[0] == '-') {
      fprintf(stderr, "error: unknown option '%s'\n\n", a); usage(); return 2;
    }
    else if (!rom_path) rom_path = a;
    else { fprintf(stderr, "error: unexpected argument '%s'\n\n", a); usage(); return 2; }
  }  if (fullscreen_asked) fullscreen = true;
  if (frames_given || movie_path) verbose = true;

  // What the file says and no option did. Paths out of the file are taken
  // from the file's directory. Three of its settings are for playing and not
  // for a movie, which was recorded against the game as it shipped and from
  // reset: the intro skip, the starting level, the cheats and (below) the
  // hitbox; and its top scores file is not a way to ask for top scores under
  // one.
  static char rom_from_config[CONFIG_PATH_MAX], hiscore_from_config[CONFIG_PATH_MAX];
  if (!rom_path) {
    config_resolve(&g_cfg, g_cfg.rom, rom_from_config, sizeof rom_from_config);
    rom_path = rom_from_config;
  }
  if (!movie_path) {
    if (g_cfg.skip_intro && !skip_intro_refused) skip_the_intro = true;
    if (start_level < 0) start_level = g_cfg.level;
    for (int i = 0; i < CHEAT_COUNT; i++)
      if (!cheats.asked[i]) cheats.on[i] = g_cfg.cheat[i];
    if (!hiscore_path && g_cfg.high_scores_file[0]) {
      config_resolve(&g_cfg, g_cfg.high_scores_file, hiscore_from_config, sizeof hiscore_from_config);
      hiscore_path = hiscore_from_config;
    }
  }
  if (skip_intro_refused) skip_the_intro = false;
  // No file anywhere: write the one to edit. Not for a test -- a movie or a
  // bounded run -- which should leave nothing behind it.
  if (config_found) {
    if (verbose || g_cfg.warnings)
      printf("Config: %s%s\n", g_cfg.path,
             g_cfg.warnings ? " (with the lines above left at their defaults)" : "");
  } else if (config_off) {
    if (verbose) printf("Config: none read (--no-config)\n");
  } else if (movie_path || frames_given) {
    if (verbose) printf("Config: none found; the defaults\n");
  } else if (config_write_default(CONFIG_FILE))
    printf("Config: none found, so %s was written here with the defaults. Edit it to taste.\n", CONFIG_FILE);
  else
    printf("Config: none found, and %s could not be written here; the defaults\n", CONFIG_FILE);
  // A movie is indexed from reset and carries its own boot half — the same
  // Start mashing `skip_intro` performs — so doing both would run the logos
  // twice and land the movie 1150 frames into a game it thinks has not begun.
  // Refusing is better than silently picking one.
  if (skip_the_intro && movie_path) {
    fprintf(stderr, "error: --skip-intro and -m cannot be combined; a movie\n"
                    "       drives from reset and already contains its boot.\n");
    return 2;
  }

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) {
    fprintf(stderr, "error: cannot read ROM '%s'\n"
                    "       Name it on the command line, or as rom = in %s.\n",
            rom_path, g_cfg.path[0] ? g_cfg.path : CONFIG_FILE);
    return 1;
  }

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) {
    fprintf(stderr, "error: core rejected ROM '%s'\n", rom_path);
    return 1;
  }
  if (verbose) {
    static const char* const cart_types[4] = {"(none)", "LoROM", "HiROM", "ExHiROM"};
    printf("ROM: '%s', %s %s, %u KB\n", rom_path,
           cart_types[snes->cart->type < 4 ? snes->cart->type : 0],
           snes->palTiming ? "PAL" : "NTSC", (unsigned)(snes->cart->romSize / 1024));
  }
  // After the load, not before it: `cart_load` mallocs its own copy and memcpys
  // into it, so the buffer read off disk is not the one the 65816 fetches from.
  // Once, not per frame — unlike the widescreen window, nothing at runtime can
  // change the answer.
  if (start_level >= 0 && !start_at_level(snes, start_level)) {
    fprintf(stderr,
            "error: --level: '%s' is not a cartridge this can change — it wants\n"
            "       `LDA #` at $80:85F0 and $80:9BBD, a `JSL` at $80:84BD, and\n"
            "       an untouched end-of-bank pad at $80:FF68.\n",
            rom_path);
    return 1;
  }
  // The same buffer and the same pad, and the two claim different parts of it —
  // so the order of these does not matter and neither has to know about the
  // other. See `src/twinstick.h` for what nine bytes at $80:D250 become.
  if (twin_stick && !twin_install(snes->cart->rom, snes->cart->romSize)) {
    fprintf(stderr,
            "%s: twin stick: '%s' is not a cartridge this can change — it\n"
            "       wants the direction latch `LDA $0072,X : STA $24 : BEQ +2 :\n"
            "       STA $26` at $80:D250 and an untouched pad at $80:FF80.\n",
            twin_asked ? "error" : "note ", rom_path);
    // Asked for and impossible is an error; on by default and impossible is a
    // line of output and a game that still starts. A ROM this cannot patch is
    // one it can leave entirely alone, and refusing to run it would make the
    // default the strictest thing in the program.
    if (twin_asked) return 1;
    twin_stick = false;
  }
  // The cheats, which are asked for by name and so are an error if they cannot
  // be had. All of them or none: a cartridge that is not the one known is not
  // half changed.
  if (!cheats_install(&cheats, snes->cart->rom, (size_t)snes->cart->romSize)) {
    const CheatId which = cheats_rom_check(&cheats, snes->cart->rom, (size_t)snes->cart->romSize);
    char named[64];
    if (cheats.asked[which]) snprintf(named, sizeof named, "--%s", cheat_flags[which]);
    else snprintf(named, sizeof named, "%s = on in [cheats]", config_cheat_names[which]);
    fprintf(stderr,
            "error: %s: '%s' is not a cartridge this can change -- the code\n"
            "       the cheat rewrites is not where this ROM has it. See src/cheats.h.\n",
            named, rom_path);
    return 1;
  }

  // Attach the harness before the reset, exactly as `cosim_lockstep` does: it
  // hooks the program counter rather than the machine's state, so it neither
  // needs nor performs one. `COSIM_NATIVE` is what installs the port's APU hook
  // in its driving form — `apu_drive`, which puts real bytes on the real ports —
  // so this is the mode to be in even when starting with substitution off.
  Cosim cosim;
  cosim_init(&cosim, snes, COSIM_NATIVE);
  if (profile_dir && !(cosim.profile = cosim_profile_new(cosim.rom.size))) {
    fprintf(stderr, "error: no memory for --profile\n");
    return 2;
  }
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
  // `enabled` is what the engine reads, and clearing it is the whole of
  // running stock.
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

  // Not under a movie: what a movie shows should not depend on what was
  // played yesterday, and its scores are not the player's. Unless a file is
  // named, which is asking for it -- and is the only way to test this with a
  // game over that was not played by hand.
  // A longer reach for pickups, rescues and the player's weapons. Not under a
  // movie unless asked for: every movie there is was made at the game's own
  // reach, and picks things up a step sooner at any other.
  if (hitbox_pct == 0) hitbox_pct = have_movie ? 100 : g_cfg.hitbox;
  actor_overlap_reach = (OVERLAP_REACH_STOCK * hitbox_pct + 50) / 100;
  if (!native && hitbox_pct != 100)
    printf("note : --stock runs the ROM's own collision pass; --hitbox has no effect.\n");
  else if (verbose && hitbox_pct != 100)
    printf("Hitboxes: pickups, rescues and the players' weapons reach %d px (the game's 8).\n",
           actor_overlap_reach);

  static Hiscore hiscore;
  hiscore_init(&hiscore, hiscore_path);
  hiscore.enabled = hiscore_on && (!have_movie || hiscore_path);
  hiscore.verbose = verbose;
  // A score made with a cheat on is not one for the file: the table is put in
  // place as ever, and nothing is written back.
  hiscore.read_only = cheats_any(&cheats);

  snes_setPixelFormat(snes, ZAMN_PIXEL_FORMAT);
  // Before the window is sized, because the window is sized from the picture.
  // `auto` starts off, which is what it is in a window and so the size the
  // window comes back to; fullscreen, the loop widens it before the first
  // frame, once the display can be asked.
  WideMode wide = wide_setting == WIDE_AUTO ? WIDE_OFF : wide_setting;
  WideMode wide_shown = wide;  // the width the console last reported
  bool wide_told = false;      // past the first pass of the loop
  snes_setWidescreen(snes, wide_margin(wide), wide_margin(wide));
  fb_w = snes_pixelWidth(snes);
  // ...and the per-frame half of it runs at the top of each frame, from the
  // machine itself, because that is the only moment the game's vblank is over
  // and none of the picture has been drawn yet. See `SnesFrameHook`.
  // Static because it carries a copy of the machine's whole 128 KB of work RAM
  // — see `widescreen.h` on why the margins are drawn from a tick-old memory.
  static Widescreen ws;
  widescreen_install(snes, &ws, rom, rom_len, wide_margin(wide));
  cosim_watch(&cosim, WS_PASS_DONE_AT, widescreen_pass_done, &ws);
  cosim_watch(&cosim, WS_OAM_SENT_AT, widescreen_oam_sent, &ws);
  // The survivor radar's squares, all of them or the console's one at a time
  // (`src/radar.h`). Only the picture, so a movie may have either.
  ws.radar.steady = !radar_flash;
  // The flash along the bottom edge as the game over begins (`src/maskline.h`):
  // the game's own, and only ever hidden by a television's overscan. The same
  // jobs on the same frames in another order, so a movie plays the same.
  if (!maskline_fix(snes->cart))
    printf("note : this ROM's game over is not the one known; its first frames are left as they are.\n");
  // The game over's blood: the mask's colours in the cartridge's copy of the
  // image, and the drips from the frame hook. Only the picture, so a movie
  // may have it too.
  if (red_blood) {
    if (blood_patch_rom(snes->cart->rom, (size_t)snes->cart->romSize)) {
      ws.blood.on = true;
      if (verbose) printf("Blood: the game over's is red.\n");
    } else {
      printf("note : --red-blood does not know this ROM's game over, and is off.\n");
    }
  }
  snes_reset(snes, true);

  // The sound effects the driver drops or cuts short (`src/sfx_overlay.h`). It
  // watches the machine from here on and takes its copy of the APU once the
  // driver is running.
  static SfxOverlay sfx;
  sfx_overlay_init(&sfx, snes, want_audio && effect_overlay);
  // The monster sounds the game skips when the level has another sample set
  // loaded (`src/bank_sfx.h`): the checks are watched from here on, and the
  // copies holding the other sets are built once the driver is running.
  static BankSfx bank;
  bank_sfx_init(&bank, snes, snes->cart->rom, (int)snes->cart->romSize,
                want_audio && all_monster_sounds);
  if (bank.enabled) {
    bool watched = cosim_watch(&cosim, BSFX_SEND_AT, bank_sfx_at, &bank);
    for (int k = 0; k < bank.sites && watched; k++)
      watched = cosim_watch(&cosim, bank.site_at[k], bank_sfx_at, &bank);
    if (!watched) {
      printf("note : no room to watch the monster sounds' checks; --all-monster-sounds is off.\n");
      bank.enabled = false;
    }
  }

  Uint32 init_flags = SDL_INIT_VIDEO | (want_audio ? SDL_INIT_AUDIO : 0);
  // Per-monitor DPI awareness, so that on a scaled desktop the window and the
  // backbuffer are the panel's own pixels. Without it a 3840x2160 panel at 125%
  // gives a 3072x1728 backbuffer that Windows stretches back up, which blurs
  // every picture and makes the sharp scaling's arithmetic about the wrong
  // pixels. The window already asks for `SDL_WINDOW_ALLOW_HIGHDPI`; on Windows
  // SDL needs this hint too before it will honour it.
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
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
  // width 4:3 pixels make of them — not N times the 512x480 buffer.
  // Sizing from the buffer would ask for a window 480N tall to show a 448N-tall
  // picture, so at 4:3 the frontend would letterbox its own window and then
  // shrink the game below 1:1 to fit the leftover, which is a blurrier picture
  // than 1x from a window that never claimed to be scaling at all.
  int win_h = FB_LIVE_H * window_scale, win_w = fb_w * window_scale;
  {
    int aw = 0, ah = 0;
    aspect_ratio(fb_w, FB_LIVE_H, &aw, &ah);
    // Rounded *up*. Rounding down leaves the window fractionally too narrow for
    // the picture it was sized for, so the fit becomes width-constrained and
    // the height comes back a pixel short — at `--scale 1` that was a 597x447
    // picture in a 597x448 window, which is a *reduction* below 1:1 and gets
    // filtered as one. A pixel of pillarbox is the cheaper rounding error.
    win_w = (int)(((long)win_h * aw + ah - 1) / ah);
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
  SDL_Rect live = {0, FB_TOP, fb_w, FB_LIVE_H};
  if (!present_init(&present, ren, fb_w, FB_H, live, scale_mode)) {
    fprintf(stderr, "error: cannot create the frame texture: %s\n", SDL_GetError());
    return 1;
  }
  {
    SDL_RendererInfo rinfo;
    if (verbose && SDL_GetRendererInfo(ren, &rinfo) == 0)
      printf("Renderer: %s%s\n", rinfo.name,
             (rinfo.flags & SDL_RENDERER_PRESENTVSYNC) ? ", vsync" : "");
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
  const long audio_target_base = (long)bytes_per_frame * 3;
  // ...and one frame more while smoothing is on. Smoothing shows a tick about
  // a frame later than the plain loop does — the tick is emulated a period
  // ahead and its last picture lands three refreshes after its first — and
  // the sound of it was going to be heard at the old time, a frame before the
  // picture. Holding a frame more of it puts the two back together, and is a
  // frame of slack against the dips that the plain loop used to get for free
  // by queueing its audio four milliseconds after the deadline rather than on
  // it. Measured: without this the backlog bottomed at 13 ms and refilled six
  // times in fifteen seconds; the plain loop's floor was 18.
  long audio_target = audio_target_base;
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
  if (refresh_asked > 0) refresh_hz = refresh_asked;
  const double content_hz = snes->palTiming ? PACE_FPS_PAL : PACE_FPS_NTSC;
  const double target_frame_ms = pace_period_ms(refresh_hz, content_hz);
  Pacer pacer;
  pacer_init(&pacer, target_frame_ms);

  // What is bound, which is the player's to say now: `src/config.h`.
  if (verbose) config_print(&g_cfg);
  if (verbose) {
    // What the picture is actually being drawn into, which fullscreen makes a
    // question worth answering: the display's size, not the window size asked
    // for. `SDL_GetRendererOutputSize` is the same call `present_draw` scales
    // by, so this line and the picture cannot disagree.
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    int aw = 0, ah = 0;
    aspect_ratio(fb_w, FB_LIVE_H, &aw, &ah);
    const ScalePlan plan = scale_plan(scale_mode, fb_w, FB_LIVE_H, aw, ah, ow,
                                      oh, present.can_target);
    printf("Display: %s, %dx%d, scaling %s\n",
           fullscreen ? "fullscreen" : "windowed", ow, oh,
           scale_name(scale_mode));
    printf("Picture: %dx%d at %d,%d from %dx%d live pixels (%.0f%% of the"
           " screen)%s\n",
           plan.dst.w, plan.dst.h, plan.dst.x, plan.dst.y, fb_w, FB_LIVE_H,
           ow > 0 && oh > 0
               ? 100.0 * plan.dst.w * plan.dst.h / ((double)ow * oh)
               : 0.0,
           // Nearest with no intermediate — which is not the same as "no
           // intermediate", since a reduction and a past-the-cap magnification
           // both skip the stage too and neither is exact.
           !plan.linear && !plan.stage_x ? ", pixel-exact" : "");
  if (!fullscreen)
    printf("Window: %dx%d (--scale %d)\n", win_w, win_h, window_scale);
  }
  // Controllers. Opened here rather than beside `SDL_Init` so that what they
  // print lands in this banner with everything else the session is about to run
  // with — and skipped entirely under `-m`, where a movie is the input and a pad
  // would have nothing to do but be listed.
  PadSet pads;
  memset(&pads, 0, sizeof pads);
  if (!want_pads) { if (verbose) printf("Controllers: disabled (--no-pads, or the config)\n"); }
  else if (have_movie) { if (verbose) printf("Controllers: not read (a movie is driving)\n"); }
  else if (pad_init(&pads) && pad_count(&pads) == 0)
    printf("Controllers: none attached — keyboard, or plug one in at any time\n");
  // The player's bindings over the default table `pad_init` made.
  pads.map = g_cfg.pad;
  if (pad_ignored(&pads))
    printf("Controllers: %d more attached than the SNES has ports; ignored\n",
           pad_ignored(&pads));
  if (verbose)
    printf("Pacing: %.3f ms/frame (%.2f fps)%s, display %d Hz, content %.4f Hz\n",
           target_frame_ms, 1000.0 / target_frame_ms,
           target_frame_ms == 1000.0 / content_hz ? " from the console"
                                                  : " locked to the display",
           refresh_hz, content_hz);
  // A device that would not open is worth a line whether or not anybody asked
  // for the banner: it is the run with no sound and no explanation otherwise.
  if (audio) {
    if (verbose)
      printf("Audio: %d Hz, %d-sample device buffer, backlog target %.0f ms\n",
             AUDIO_FREQ, AUDIO_DEVICE_SAMPLES,
             (double)audio_target / audio_bytes_per_ms);
  } else if (verbose || want_audio) {
    printf("Audio: none%s\n", want_audio ? " (device would not open)" : "");
  }
  // ...and so is `--stock`, which is the one way to run this and have the port
  // do nothing.
  if (verbose || !native)
    printf("Substitution: %s (%d routine%s registered)%s\n",
           native ? "on" : "off (stock)", routine_count,
           routine_count == 1 ? "" : "s",
           have_movie ? ", replaying a movie" : "");
  cheats_print(&cheats);
  if (cheats_any(&cheats)) {
    if (hiscore.enabled) printf("Cheats: the top scores are read and not written while one is on.\n");
    if (have_movie) printf("note : a movie was recorded without cheats, and will not meet the game it was made against.\n");
  }
  // Announced because it is the one option here that changes what the *game*
  // does rather than how it is shown, and a run that starts on level 30 should
  // say so in its own log rather than leave somebody wondering.
  if (start_level >= 0) {
    const LevelStart plan = level_start_plan(snes->cart->rom, start_level);
    if (plan.arm)
      printf("Start level: %d (--level; the bonus room off level %d, then"
             " level %d)\n", start_level, plan.next - 1, plan.next);
    else if (plan.next < 0)
      printf("Start level: %d (--level; the credit roll, then the title)\n",
             start_level);
    else
      printf("Start level: %d (--level; %son to %d from there)\n", start_level,
             start_level == 0 ? "a bonus room, " : "", plan.next);
  }
  // Both ways round under `--verbose`, and the off way round without it: this
  // one is on unless told otherwise, so a session where it was turned off — or
  // could not be installed — should say that rather than look like a pad that
  // has stopped working.
  if (twin_stick) {
    if (verbose)
      printf("Twin stick: on (right stick aims and fires%s)\n",
             have_movie          ? ", but a movie is driving"
             : !want_pads        ? ", but the controllers are off"
                                 : "");
  } else
    printf("Twin stick: off (right stick does nothing)\n");
  // Redirected to a file, this is block-buffered, and everything above it
  // describes the session that is about to start — so it wants to be readable
  // *during* the session and not only after a clean exit. A run that is killed
  // or crashes is exactly the run whose settings someone wants to look up.
  fflush(stdout);

  const Uint64 perf_freq = SDL_GetPerformanceFrequency();
  FILE* trace_frames = trace_frames_path ? fopen(trace_frames_path, "w") : NULL;
  if (trace_frames_path && !trace_frames) perror(trace_frames_path);
  if (trace_frames) {
    setvbuf(trace_frames, NULL, _IOFBF, 65536);
    fprintf(trace_frames, "frame,phase,core_frame,scroll_y,present_ms,emulate_ms\n");
  }
  uint32_t trace_core_frame = 0;
  int trace_scroll_y = 0;
  double trace_emulate_ms = 0;
  const bool paced = frame_limit == 0 || force_pacing;

  // Pictures between ticks — see `src/layers.h`, and `Layers` above for the
  // frontend's half. Possible on any display faster than the game
  // (`pace_lock_ratio`), and only in a paced run: an uncapped one is a
  // throughput measurement with no refresh to fill. Every refresh is one
  // picture, and the game is `pic_p / pic_q` of a tick further on in each than
  // in the last — 1/4 at 240 Hz, 5/12 at 144, and 1/1 when smoothing is off or
  // impossible, which is the plain loop. `phase` is how far through the
  // current tick the last picture was, in `pic_q`-ths; F6 changes the fraction,
  // between ticks.
  int lock_p = 1, lock_q = 1;
  const bool smooth_possible =
      paced && pace_lock_ratio(refresh_hz, content_hz, &lock_p, &lock_q);
  // Paced per picture rather than per tick — one refresh — so that each
  // picture lands on its own refresh instead of several being released
  // together; and the tick is then however many refreshes the fraction says,
  // which locks the game to the display at any refresh rate as
  // `pace_period_ms` does at a whole multiple.
  const double smooth_picture_ms = refresh_hz > 0 ? 1000.0 / refresh_hz : target_frame_ms;
  const double smooth_tick_ms = smooth_picture_ms * lock_q / lock_p;
  int pic_p = 1, pic_q = 1;
  if (smooth_possible && smooth) {
    pic_p = lock_p;
    pic_q = lock_q;
  }
  int phase = pic_q;
  if (pic_q > 1) audio_target = audio_target_base + (long)bytes_per_frame;
  pacer_init(&pacer, pic_q > 1 ? smooth_picture_ms : target_frame_ms);
  pacer.slack = pic_q > 1 ? smooth_tick_ms : target_frame_ms;
  Layers lay;
  PresentLayers plyr;
  memset(&lay, 0, sizeof lay);
  memset(&plyr, 0, sizeof plyr);
  EmuThread emu;
  memset(&emu, 0, sizeof emu);
  SDL_Thread* emu_thread = NULL;
  if (smooth_possible) {
    emu.cosim = &cosim;
    emu.snes = snes;
    emu.sfx = &sfx;
    emu.bank = &bank;
    emu.layers = &lay;
    emu.go = SDL_CreateSemaphore(0);
    emu.done = SDL_CreateSemaphore(0);
    if (!layers_init(&lay) || !present_layers_init(&plyr, ren) || !emu.go || !emu.done ||
        !(emu_thread = SDL_CreateThread(emu_thread_main, "emulate", &emu))) {
      fprintf(stderr, "error: cannot set up smoothing: %s\n", SDL_GetError());
      return 1;
    }
    lay.ws = &ws;
    if (!plyr.blends_ok)
      printf("note: this renderer has no custom blend modes, so a frame that adds\n"
             "      the sub screen (the character select) is shown as the PPU drew it.\n");
  }
  if (!verbose) {}
  else if (smooth_possible && lock_p == 1)
    printf("Smoothing: %s (F6 toggles; %d pictures per frame at %d Hz when on;"
           " motion %s)\n",
           smooth ? "on" : "off", lock_q, refresh_hz,
           even ? "evened" : "eased frame to frame");
  else if (smooth_possible)
    printf("Smoothing: %s (F6 toggles; %d pictures per %d frames at %d Hz when"
           " on, the game at %.3f fps; motion %s)\n",
           smooth ? "on" : "off", lock_q, lock_p, refresh_hz,
           1000.0 / smooth_tick_ms,
           even ? "evened" : "eased frame to frame");
  else if (!paced)
    printf("Smoothing: off (--frames runs uncapped)\n");
  else
    printf("Smoothing: off (a %d Hz display has no refreshes between the game's"
           " frames to fill)\n", refresh_hz);
  fflush(stdout);
  // Not const: `--skip-intro` runs a few seconds of emulation before the loop,
  // and folding that into the elapsed time would report the session at 44 fps
  // when every frame of it arrived on cadence. The clock starts when play does.
  Uint64 started = SDL_GetPerformanceCounter();
  const DwmStats dwm0 = dwm_stats();

  // What the loop spends each frame on, and — the point of the exercise — how
  // evenly the frames come out the far end. See `src/pace.h` for why the mean
  // is not the interesting statistic.
  // Last thing before the loop, so the frames it burns are not paced, not
  // drawn and not heard — and so the audio priming below lands on a device that
  // is about to be fed rather than one about to sit idle for a few seconds.
  if (skip_the_intro) {
    const Uint64 t0 = SDL_GetPerformanceCounter();
    bool bypassed = false;
    const long ran = skip_intro(&cosim, snes, win, &pads, &bypassed);
    if (verbose)
      printf("Skipped the intro: %ld frames (%.1f s of game) in %.2f s%s.\n", ran,
             ran / 60.0,
             (double)(SDL_GetPerformanceCounter() - t0) / (double)perf_freq,
             bypassed ? ", the logos bypassed" : "");
    fflush(stdout);
    started = SDL_GetPerformanceCounter();
  }
  if (audio) queue_silence(audio, audio_target);

  PaceHist h_interval, h_wait, h_emulate, h_draw, h_audio, h_take;
  pace_reset(&h_interval); pace_reset(&h_wait);
  pace_reset(&h_emulate);  pace_reset(&h_draw);
  pace_reset(&h_audio);
  pace_reset(&h_take);
  // Ticks the smoothing had to show as the PPU drew them, because the frame
  // was not one a draw list can express — see `layers_unexpressible`.
  long unlayered = 0;
  Uint64 last_arrival = 0;
  #define PACE_MS(a, b) ((double)((b) - (a)) * 1000.0 / (double)perf_freq)

  long frame = 0;
  char title[160];
  bool running = true;
  // What the keyboard is holding, as an SNES button mask rather than as core
  // state. The core is written once a frame from this ORed with what the pads
  // report, because a pad cannot be read from key events and a keyboard cannot
  // be polled — see `src/pad.h` for why the pad half has to be the polled one.
  // A bit per *key* and not per button, so that a button on two keys is held
  // until both are let go: `ConfigKeys`.
  ConfigKeys keys;
  memset(&keys, 0, sizeof keys);
  uint16_t key_held[MOVIE_PORTS] = {0};
  // Whether the emulation thread is working on a tick; and how many pictures
  // have been shown, which is more than `frame` by exactly the smoothing.
  bool in_flight = false;
  long pictures = 0;
  // Quick save (F5) and quick load (F9): `src/quicksave.h`. A key only asks;
  // the tick block does it, with the machine stopped.
  enum { QUICK_NOTHING, QUICK_SAVE, QUICK_LOAD } quick_want = QUICK_NOTHING;
  int quick_waited = 0;
  static QuickSave quick;
  quicksave_init(&quick, rom, (size_t)rom_len);
  uint32_t owners_serial = sprite_oam_owners.serial;
  int lay_cut = 0;
  while (running && (frame_limit == 0 || frame < frame_limit)) {
    // What the frontend has been asked to do since the last time round, a bit
    // per `ConfigAction`, by a key or by a pad.
    uint32_t actions = 0;
    SDL_Event e;
    // `--key-at`: down at its frame, up `hold` frames on, once each.
    for (int k = 0; k < key_ats; k++) {
      const bool press = key_at[k].state == 0 && frame >= key_at[k].frame;
      const bool release = key_at[k].state == 1 && frame >= key_at[k].frame + key_at[k].hold;
      if (!press && !release) continue;
      key_at[k].state++;
      memset(&e, 0, sizeof e);
      e.type = press ? SDL_KEYDOWN : SDL_KEYUP;
      e.key.state = press ? SDL_PRESSED : SDL_RELEASED;
      e.key.keysym.sym = key_at[k].key;
      e.key.keysym.scancode = SDL_GetScancodeFromKey(key_at[k].key);
      SDL_PushEvent(&e);
    }
    while (SDL_PollEvent(&e)) {
      pad_event(&pads, &e);
      if (e.type == SDL_QUIT) running = false;
      // Alt-tabbing away with a key down never delivers its KEYUP, and the
      // player comes back to a character walking into a wall. The pads have no
      // equivalent bug — they are read fresh every frame — so this is the one
      // place a release can go missing and the one place it has to be forced.
      else if (e.type == SDL_WINDOWEVENT &&
               e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        memset(&keys, 0, sizeof keys);
      else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        // A key is an action of the frontend's, or a button of the game's, or
        // nothing, and `g_cfg` says which. An action is only noted here and is
        // done below, once, where a pad's press of the same thing is done too.
        //
        // Fullscreen is also the Alt+Enter that every emulator has had since
        // DOS. That spelling has to be recognised before the key is looked up,
        // because Enter on its own is Start by default, and toggling the
        // display should not also press it.
        const SDL_Keycode sym = e.key.keysym.sym;
        const bool down = e.type == SDL_KEYDOWN;
        const bool alt_enter = sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT) != 0;
        const int act = alt_enter ? (int)ACT_FULLSCREEN : config_hotkey_of(&g_cfg, sym);
        // A release is always a release: Enter pressed as Start and let go
        // with Alt down by then must not leave Start held.
        if (!down) config_key_event(&g_cfg, &keys, sym, false);
        if (act >= 0) {
          if (down && !e.key.repeat) actions |= 1u << act;
          continue;
        }
        // A movie is driving the controller; the keyboard would fight it.
        if (down && !have_movie) config_key_event(&g_cfg, &keys, sym, true);
      }
    }
    config_keys_held(&keys, key_held);
    // ...and what the pads pressed, which `pad_poll` noted during the last tick.
    actions |= pads.hot_pressed;
    pads.hot_pressed = 0;

    if (actions & (1u << ACT_QUIT)) running = false;
    if (running && (actions & ((1u << ACT_QUICK_SAVE) | (1u << ACT_QUICK_LOAD)))) {
      // Quick save and quick load. Only asked for here: the machine may be
      // running its next tick on the emulation thread at this moment, and both
      // are done where it is known to be stopped (see `quick_want`).
      if (have_movie) {
        printf("Quick save and load are off while a movie plays.\n");
        fflush(stdout);
      } else {
        quick_want = (actions & (1u << ACT_QUICK_SAVE)) ? QUICK_SAVE : QUICK_LOAD;
        quick_waited = 0;
      }
    }
    if (running && (actions & (1u << ACT_TOGGLE_SMOOTHING))) {
      // Smoothing, on a key for the same reason the others are: the only way
      // to judge it is to flip it on the same scene. Takes effect at the next
      // tick, which is where the pacer is re-armed.
      smooth = !smooth;
      if (smooth_possible)
        printf("Smoothing: %s\n", smooth ? "on" : "off");
      else
        printf("Smoothing: %s, but not possible on this display\n",
               smooth ? "on" : "off");
      fflush(stdout);
    }
    if (running && (actions & (1u << ACT_FULLSCREEN))) {
      const bool want = !fullscreen;
      if (SDL_SetWindowFullscreen(
              win, want ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        // Report it and keep the flag on what is actually on screen. A toggle
        // that silently believed it had worked would put the key out of phase
        // with the display for the rest of the session.
        fprintf(stderr, "cannot change display mode: %s\n", SDL_GetError());
      } else {
        fullscreen = want;
        printf("Display: %s\n", fullscreen ? "fullscreen" : "windowed");
      }
      fflush(stdout);
    }
    if (running) {
      // The width the setting comes to. For `auto` that is asked every frame
      // and not only when a key is pressed, because the display can change
      // under it: F11 takes effect when the system gets round to it, and a
      // fullscreen window can be sent to another monitor. The output size is
      // the one `present_frame` fits the picture to.
      WideMode want = wide_setting;
      if (want == WIDE_AUTO) {
        int ow = 0, oh = 0;
        want = fullscreen && SDL_GetRendererOutputSize(ren, &ow, &oh) == 0
                   ? wide_for_display(FB_LIVE_H, ow, oh)
                   : WIDE_OFF;
      }
      if (want != wide) {
        // This one costs a texture, because the picture changes *size* rather
        // than shape, and the frame texture and its offscreen stage were both
        // made at the old width. Between frames, so nothing is half-drawn at
        // one width and finished at the other.
        wide = want;
        snes_setWidescreen(snes, wide_margin(wide), wide_margin(wide));
        ws.margin = wide_margin(wide);
        fb_w = snes_pixelWidth(snes);
        const ScaleMode m = present.mode;
        present_free(&present);
        live.w = fb_w;
        if (!present_init(&present, ren, fb_w, FB_H, live, m)) {
          fprintf(stderr, "error: cannot resize the frame texture: %s\n",
                  SDL_GetError());
          running = false;
        }
      }
      if (running && wide != wide_shown) {
        // Not on the first pass, unless asked: `auto` settling on the display
        // it started on is not news to a quiet console.
        if (wide_told || verbose) {
          if (wide_setting == WIDE_AUTO)
            printf("Widescreen: auto, %s (%d columns)\n", wide_name(wide), fb_w / 2);
          else
            printf("Widescreen: %s (%d columns)\n", wide_name(wide), fb_w / 2);
          fflush(stdout);
        }
        wide_shown = wide;
      }
      wide_told = true;
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

    // This picture is `pic_p / pic_q` of a tick on from the last, and when
    // that passes the end of the tick being shown, the next one is taken.
    phase += pic_p;
    const bool tick = phase > pic_q;
    if (tick) {
      phase -= pic_q;
      // A tick: collected from the emulation thread if it has one, run right
      // here if not. Either way the machine is stopped from this point until
      // it is told to go again, and everything that touches it — the audio it
      // made, the copy of its picture, the input for its next tick — happens
      // in between.
      bool taken = false;  // has this tick's picture been taken apart already
      if (in_flight) {
        SDL_SemWait(emu.done);
        in_flight = false;
        pace_add(&h_emulate, emu.last_ms);
        trace_emulate_ms = emu.last_ms;
        pace_add(&h_take, emu.take_ms);
        taken = emu.capture;
      } else {
        tick_input(snes, &movie, have_movie, frame, &pads, key_held, twin_stick);
        hiscore_tick(&hiscore, snes->ram);
        poke_apply(&pokes, snes->ram, (int)frame);
        cheats_tick(&cheats, snes->ram, snes->cart->rom);
        // The substitution seam. Identical to `snes_runFrame` when the mask
        // is clear; when it is not, a registered routine's entry PC hands the
        // call to the C port, which runs against the core's own WRAM and
        // returns through the routine's own RTS/RTL.
        const Uint64 t0 = SDL_GetPerformanceCounter();
        cosim_frame(&cosim);
        sfx_overlay_tick(&sfx);
        bank_sfx_tick(&bank);
        trace_emulate_ms = PACE_MS(t0, SDL_GetPerformanceCounter());
        pace_add(&h_emulate, trace_emulate_ms);
      }
      trace_core_frame = snes->frames;
      trace_scroll_y = snes->ppu->lineVScroll[0][1];
      frame++;

      // This frame's audio, resampled by however much it takes to hold the
      // queue at `audio_target`. `dsp_getSamples` already resamples the DSP's
      // native 534 samples per frame to whatever is asked for, so the count
      // *is* the rate-control knob and asking for 802 instead of 800 costs
      // nothing.
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
          sfx_overlay_mix(&sfx, audio_buf, want_samples);
          bank_sfx_mix(&bank, audio_buf, want_samples);
          if (volume != 100)
            for (int i = 0; i < want_samples * 2; i++)
              audio_buf[i] = (int16_t)(audio_buf[i] * volume / 100);
          SDL_QueueAudio(audio, audio_buf,
                         (Uint32)want_samples * 2 * sizeof(int16_t));
        }
        // Over the ceiling, this frame's sound is dropped rather than
        // deepening a backlog that is already past what rate control can pull
        // back. It is audible, and it is the lesser of the two.
      }

      // The port's sprite pass: did it run in the tick that has just ended?
      // Its serial says. (What `Layers.held_fresh` is, kept here as well
      // because a quick save needs it whether or not the smoothing is on.)
      const bool owners_fresh = sprite_oam_owners.serial != owners_serial;
      owners_serial = sprite_oam_owners.serial;

      for (int k = 0; k < quick_ats; k++)
        if (quick_at[k].frame == frame) {
          quick_want = quick_at[k].load ? QUICK_LOAD : QUICK_SAVE;
          quick_waited = 0;
          if (quick_at[k].file) snprintf(quick.path, sizeof quick.path, "%s", quick_at[k].file);
        }
      // A quick save or load that was asked for, here because the machine is
      // stopped: its last tick has been collected and its next not started.
      if (quick_want != QUICK_NOTHING) {
        uint8_t fresh = owners_fresh ? 1 : 0;
        const QuickPart parts[] = {
          {ws.mem, sizeof ws.mem}, {&ws.have_mem, sizeof ws.have_mem},
          {ws.lent_frame, sizeof ws.lent_frame}, {ws.lent_slot, sizeof ws.lent_slot},
          {&ws.lent_count, sizeof ws.lent_count},
          {ws.back_slot, sizeof ws.back_slot}, {&ws.back_count, sizeof ws.back_count},
          {ws.slot_drawn, sizeof ws.slot_drawn},
          {&sprite_oam_owners, sizeof sprite_oam_owners},
          {sprite_oam_history, sizeof sprite_oam_history},
          {&fresh, sizeof fresh},
        };
        const int part_count = (int)(sizeof parts / sizeof parts[0]);
        if (quick_want == QUICK_SAVE) {
          // Not while the harness is part way through a routine of its own:
          // wait for a tick that ends clean, which is nearly all of them.
          if (cosim_idle(&cosim)) {
            const bool ok = quicksave_write(&quick, snes, parts, part_count);
            printf(ok ? "Quick save: '%s'.\n" : "Quick save: cannot write '%s'.\n", quick.path);
            if (ok) notice_show("SAVED", 120, 255, 120);
            else notice_show("SAVE FAILED", 255, 90, 90);
            quick_want = QUICK_NOTHING;
          } else if (++quick_waited > 300) {
            printf("Quick save: the machine did not come to rest; not saved.\n");
            notice_show("SAVE FAILED", 255, 90, 90);
            quick_want = QUICK_NOTHING;
          }
        } else {
          const QuickLoad got = quicksave_read(&quick, snes, parts, part_count);
          if (got == QUICKLOAD_OK) {
            cosim_forget_calls(&cosim);
            sfx_overlay_resync(&sfx);
            widescreen_forget_passes(&ws);
            // The smoothing: the tick being shown and the first one after the
            // load are not neighbours, so the link between them is cut when it
            // comes to be made (`lay_cut`), and the owner table the next
            // picture is taken apart with is the restored one.
            lay.held = sprite_oam_owners;
            lay.held_fresh = fresh != 0;
            lay.serial = owners_serial = sprite_oam_owners.serial;
            lay.used_ok = false;
            lay_cut = 2;
            // The top scores are not rolled back: the file's table goes over
            // the machine's, as after a boot.
            hiscore.restored = false;
            // ...and a save from before `--give-all` is given what a start is.
            cheats_loaded(&cheats);
            memset(&keys, 0, sizeof keys);
            memset(key_held, 0, sizeof key_held);
            printf("Quick load: '%s'.\n", quick.path);
            notice_show("LOADED", 120, 200, 255);
          } else {
            printf(got == QUICKLOAD_NONE ? "Quick load: there is no '%s'.\n"
                                         : "Quick load: '%s' is not a save of this ROM by this build.\n",
                   quick.path);
            notice_show(got == QUICKLOAD_NONE ? "NO SAVE" : "LOAD FAILED", 255, 90, 90);
          }
          quick_want = QUICK_NOTHING;
        }
        fflush(stdout);
      }

      // How many pictures this tick is shown as. Asked again every tick
      // because F6 changes it, and acted on only here, between ticks, where
      // the pacer can be re-armed at the new period with nothing in flight.
      const bool want = smooth_possible && smooth;
      if (want != (pic_q > 1)) {
        pic_p = want ? lock_p : 1;
        pic_q = want ? lock_q : 1;
        phase = pic_p;
        pacer_init(&pacer, want ? smooth_picture_ms : target_frame_ms);
        pacer.slack = want ? smooth_tick_ms : target_frame_ms;
        if (smooth_possible) lay.frame[0]->valid = lay.frame[1]->valid = false;
        // Rate control walks the queue to the new depth at four samples a
        // frame, which is a few seconds and inaudible.
        audio_target = audio_target_base + (want ? (long)bytes_per_frame : 0);
      }
      if (pic_q > 1) {
        // This tick's picture, taken apart -- by the thread as the tick
        // ended, or here if the tick ran here -- becomes the current frame,
        // and its planes go to the renderer once for all its pictures.
        if (!taken) {
          const Uint64 t0 = SDL_GetPerformanceCounter();
          layers_take(&lay, snes->ppu);
          pace_add(&h_take, PACE_MS(t0, SDL_GetPerformanceCounter()));
        }
        // After a quick load, the tick that was on screen is no neighbour of
        // the first one loaded: by the time they come to be linked it has been
        // shown, and is marked as nothing to ease from.
        if (lay_cut && --lay_cut == 0) lay.frame[lay.cur]->valid = false;
        layers_advance(&lay);
        if (lay.frame[lay.cur]->layered) present_layers_upload(&plyr, lay.frame[lay.cur]);
        else unlayered++;
        // ...and the next tick starts now, so that it is done by the time this
        // one's pictures have all been shown. Not past the frame limit, so a
        // bounded run's screenshot is of the frame it asked for.
        if (running && (frame_limit == 0 || frame < frame_limit)) {
          tick_input(snes, &movie, have_movie, frame, &pads, key_held, twin_stick);
          hiscore_tick(&hiscore, snes->ram);
          poke_apply(&pokes, snes->ram, (int)frame);
          cheats_tick(&cheats, snes->ram, snes->cart->rom);
          emu.capture = true;
          SDL_SemPost(emu.go);
          in_flight = true;
        }
      }
    }
    const Uint64 t_emul = SDL_GetPerformanceCounter();

    // The picture: the machine's own, or the one `phase` parts in `pic_q` of
    // the way from the last tick to this one, drawn as layers on a target the
    // window's size; or, for a frame that cannot be drawn that way, the PPU's
    // own picture of it, as many times as there are refreshes.
    if (pic_q > 1) {
      const LayersFrame* f = lay.frame[lay.cur];
      bool drawn = false;
      if (f->valid && f->layered) {
        ScalePlan plan;
        int sx, sy;
        bool exact;
        if (present_layers_plan(&present, f->width, &plan, &sx, &sy, &exact)) {
          const int n = layers_list(f, phase, pic_q, sx, sy, even, lay.ops);
          drawn = present_layers_draw(&present, &plyr, &plan, sx, sy, exact,
                                      lay.ops, n, f->width, f->dim);
          if (drawn && dump_prefix && frame >= dump_first && frame <= dump_last) {
            const int tw = f->width * sx, th = LAYERS_LINES * sy;
            uint8_t* gpu = (uint8_t*)malloc((size_t)plan.dst.w * plan.dst.h * 3);
            uint8_t* sw = (uint8_t*)malloc((size_t)tw * th * 3);
            char path[600];
            const SDL_Rect r = {plan.dst.x, plan.dst.y, plan.dst.w, plan.dst.h};
            if (gpu && SDL_RenderReadPixels(ren, &r, SDL_PIXELFORMAT_RGB24, gpu, r.w * 3) == 0) {
              snprintf(path, sizeof path, "%s.%ld.%d.gpu.png", dump_prefix, frame, phase);
              printf("dump frame %ld: main thread at $%06X, game over %d; hud_panel_on ws.mem %d %d, ram %d %d; BG3 policy %d; in_level %d\n", frame,
                     ws_main_thread_at(ws.mem), ws_game_over(ws.mem),
                     ws.mem[0x1e88] | (ws.mem[0x1e89] << 8), ws.mem[0x1e8a] | (ws.mem[0x1e8b] << 8),
                     snes->ram[0x1e88] | (snes->ram[0x1e89] << 8), snes->ram[0x1e8a] | (snes->ram[0x1e8b] << 8),
                     snes->ppu->layerWide[2], snes_bgTilemapWider(snes, 1) && snes_bgOnMainScreen(snes, 1));
              stbi_write_png(path, r.w, r.h, 3, gpu, r.w * 3);
            }
            if (sw) {
              layers_render(f, lay.ops, n, sw, tw, th);
              snprintf(path, sizeof path, "%s.%ld.%d.sw.png", dump_prefix, frame, phase);
              stbi_write_png(path, tw, th, 3, sw, tw * 3);
            }
            int ow = 0, oh = 0;
            SDL_GetRendererOutputSize(ren, &ow, &oh);
            printf("dumped picture %d of frame %ld: %dx%d on a %dx%d output, list at %dx%d (%s)\n",
                   phase, frame, plan.dst.w, plan.dst.h, ow, oh, tw, th, exact ? "exact" : "resampled");
            fflush(stdout);
            free(gpu);
            free(sw);
          }
        }
      }
      if (!drawn && f->valid) present_pixels(&present, f->fb, f->fbWidth);
      if (!drawn && f->valid && dump_prefix && frame >= dump_first && frame <= dump_last) {
        dump_output(&present, dump_prefix, frame, phase);
      }
      present_finish(&present);
    } else {
      present_frame(&present, snes->ppu);
      if (dump_prefix && frame >= dump_first && frame <= dump_last)
        dump_output(&present, dump_prefix, frame, phase);
      present_finish(&present);
    }
    pictures++;

    // Measured after `SDL_RenderPresent` has returned, which is the moment the
    // frame is the screen's problem rather than ours — so `arrival` is the
    // cadence a player sees, not the cadence the loop intended.
    const Uint64 t_drawn = SDL_GetPerformanceCounter();
    if (trace_frames)
      fprintf(trace_frames, "%ld,%d,%u,%d,%.3f,%.3f\n", frame, phase,
              trace_core_frame, trace_scroll_y, PACE_MS(started, t_drawn), trace_emulate_ms);
    pace_add(&h_draw, PACE_MS(t_emul, t_drawn));
    if (last_arrival) pace_add(&h_interval, PACE_MS(last_arrival, t_drawn));
    last_arrival = t_drawn;

    // The status the window can carry without a console. Twice a second is
    // often enough to read and rare enough not to matter.
    if (tick && frame % 30 == 0) {
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
                 "Zombies Ate My Neighbors — stock (emulated)");
      SDL_SetWindowTitle(win, title);
    }
  }

  // A tick the thread was still running is finished before anything reads
  // the machine — the screenshot, the reports, the teardown.
  if (in_flight) {
    SDL_SemWait(emu.done);
    in_flight = false;
  }
  double secs = (double)(SDL_GetPerformanceCounter() - started) / (double)perf_freq;
  if (trace_frames) fclose(trace_frames);
  if (shot_path && !write_png(snes, shot_path))
    fprintf(stderr, "error: cannot write '%s'\n", shot_path);

  if (audio) SDL_CloseAudioDevice(audio);
  pad_free(&pads);
  present_free(&present);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();

  // What actually happened, in the harness's own words. `cosim_report` is the
  // same per-routine table `zamn_cosim run` prints, minus the verdicts a diff
  // would have filled in — there is no reference core here to compare against,
  // so the `checked` column means "substituted" and nothing is claimed beyond
  // that. The census names any handler a guard declined, which is the work list.
  // All of it under `--verbose` and for a measurement; a session that was
  // played gets the one line, and the two below it that say something went
  // wrong, so that quitting the game does not scroll a report past it.
  printf("\n%ld frames in %.1f s (%.1f fps).\n", frame, secs,
         secs > 0 ? frame / secs : 0.0);
  if (verbose && pictures != frame) {
    printf("  shown as %ld pictures (%.2f per frame, %.1f per second).\n",
           pictures, frame > 0 ? (double)pictures / (double)frame : 0.0,
           secs > 0 ? pictures / secs : 0.0);
    printf("  %ld ticks were shown as the PPU drew them, %ld as layers;"
           " taking a tick apart took %.2f ms (max %.2f).\n",
           unlayered, frame - unlayered, pace_mean(&h_take), h_take.max);
  }
  // ...and the line above is exactly the statistic that cannot see a stutter,
  // so it is immediately followed by the one that can. The period is the
  // pacer's own, which is a picture's and not a frame's when smoothing is on.
  if (verbose)
    pace_report(&h_interval, &h_wait, &h_emulate, &h_draw, &h_audio,
                pacer.period, (double)audio_target / audio_bytes_per_ms, paced);
  if (verbose) {
    const DwmStats dwm1 = dwm_stats();
    // Silent when the compositor never touched the frames -- fullscreen on
    // Windows 10 and later flips the picture straight to the panel.
    if (dwm0.ok && dwm1.ok && dwm1.refreshes > dwm0.refreshes)
      printf("  compositor: %llu frames composed, %llu displayed, %llu dropped,"
             " %llu missed, over %llu refreshes\n",
             dwm1.frames - dwm0.frames, dwm1.displayed - dwm0.displayed,
             dwm1.dropped - dwm0.dropped, dwm1.missed - dwm0.missed,
             dwm1.refreshes - dwm0.refreshes);
  }
  // Time the pacer gave up on. Every millisecond here is a millisecond the
  // game stood still, which `fps` above rounds away.
  if (paced && pacer.lost_count > 0)
    printf("  time written off: %ld stall%s, %.1f ms in all (%.2f%% of the run)\n",
           pacer.lost_count, pacer.lost_count == 1 ? "" : "s", pacer.lost_ms,
           secs > 0 ? 100.0 * pacer.lost_ms / (secs * 1000.0) : 0.0);
  // One refill is the device starting up. More than that, in a run of any
  // length, means rate control is losing and the sound is being patched with
  // silence to cover it — which is worth saying out loud rather than leaving to
  // be noticed as an occasional click.
  if (audio_refills > 1)
    printf("  audio backlog refilled %ld times — rate control is not keeping up\n",
           audio_refills);
  if (verbose && sfx.enabled)
    printf("  sound effects: the driver dropped %ld and lost %ld notes and %ld voices;\n"
           "                 the overlay opened %ld voices, and took its copy %ld times\n",
           sfx.effects_dropped, sfx.notes_dropped, sfx.voices_stolen, sfx.voices_opened,
           sfx.resyncs);
  if (verbose && bank.enabled)
    printf("  monster sounds: %ld asked for with another sample set loaded; played %ld,\n"
           "                 %ld, %ld and %ld on the copies holding sets 0 to 3%s\n",
           bank.wanted, bank.set[0].played, bank.set[1].played, bank.set[2].played,
           bank.set[3].played, bank.failed ? " (one or more could not be built)" : "");
  if (verbose) {
    cosim_report(&cosim);
    // The two percentages the table cannot give: 82 rows of `OK` say each
    // ported routine worked, and say nothing at all about what fraction of the
    // game that is. This does, for the session that was just played, and it is
    // measured over the whole run.
    cosim_share_report(&cosim);
    cosim_census_report();
  }

  if (emu_thread) {
    emu.quit = true;
    SDL_SemPost(emu.go);
    SDL_WaitThread(emu_thread, NULL);
  }
  if (emu.go) SDL_DestroySemaphore(emu.go);
  if (emu.done) SDL_DestroySemaphore(emu.done);
  if (smooth_possible) {
    layers_free(&lay);
    present_layers_free(&plyr);
  }
  if (have_movie) movie_free(&movie);
  // After the emulation thread is gone, so nothing is still counting into it.
  if (cosim.profile) {
    if (cosim_profile_save(cosim.profile, profile_dir))
      printf("Profile added to %s; rank it with tools/native_share.py --residue.\n",
             profile_dir);
    cosim_profile_free(cosim.profile);
    cosim.profile = NULL;
  }
  cosim_free(&cosim);
  sfx_overlay_free(&sfx);
  bank_sfx_free(&bank);
  snes_free(snes);
  free(rom);
  return 0;
}
