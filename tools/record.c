// Records a movie as video: the frontend's pictures, drawn in software at any
// size, as raw RGB on stdout for ffmpeg, and the game's sound as a WAV.
//
//     zamn_record <rom.sfc> <movie.zmv> <first> <last> [options] > frames.rgb
//
// Frames `first` to `last` are drawn; the machine runs from reset to get there,
// as `zamn_test_layers` does. Every tick is `--pictures N` pictures (1 unless
// said), each `N`th of the way from the last tick, as the frontend draws them
// on a display N times the game's rate -- so 4 pictures a tick played back at
// 60 a second is the smoothing at 240 Hz, slowed to a quarter. `--no-smooth`
// draws every picture of a tick as the tick itself, which is the console.
//
// Each picture is `(256 + 2 * margin) * sx` by `224 * sy`, where the margin is
// `--widescreen`'s and `--scale sx,sy` defaults to 12,10: 4104x2240 at 16:9,
// which ffmpeg then shrinks to 3840x2160 the way `sharp` does. A tick the draw
// list cannot express is the picture as it was drawn, enlarged nearest.
//
//   --wav <file>      The sound of frames `first` to `last`, 48 kHz stereo, 800
//                     samples a tick: one tick is exactly 1/60 s, as in video.
//   --red-blood, --flashing-radar, --hitbox <pct>, --poke <spec>, --stock,
//   --bypass-logos, --no-even, --no-effect-overlay: as the frontend has them.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#endif

#include "ppu.h"
#include "snes.h"

#include "analysis/movie_apply.h"
#include "blood.h"
#include "cosim/cosim.h"
#include "layers.h"
#include "maskline.h"
#include "poke.h"
#include "port/oam.h"
#include "scale.h"
#include "sfx_overlay.h"
#include "skipintro.h"
#include "widescreen.h"

#define SAMPLES_PER_TICK 800

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return NULL; }
  fclose(f);
  *out_len = (int)len;
  return buf;
}

static void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void wav_header(FILE* f, uint32_t samples) {
  const uint32_t bytes = samples * 4;
  fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes);
  fwrite("WAVEfmt ", 1, 8, f); put32(f, 16);
  put16(f, 1); put16(f, 2); put32(f, 48000); put32(f, 48000 * 4); put16(f, 4); put16(f, 16);
  fwrite("data", 1, 4, f); put32(f, bytes);
}

// The PPU's picture, enlarged nearest: for a tick the list cannot draw.
static void render_fb(const LayersFrame* f, int W, int sx, int sy, uint8_t* rgb) {
  const int tw = W * sx;
  for (int y = 0; y < LAYERS_LINES; y++) {
    uint8_t* row = rgb + (size_t)y * sy * tw * 3;
    for (int x = 0; x < W; x++) {
      const uint8_t* p = layers_fb_pixel(f, x, y);  // B, G, R, X
      for (int k = 0; k < sx; k++) {
        uint8_t* o = row + ((size_t)x * sx + k) * 3;
        o[0] = p[2]; o[1] = p[1]; o[2] = p[0];
      }
    }
    for (int k = 1; k < sy; k++) memcpy(row + (size_t)k * tw * 3, row, (size_t)tw * 3);
  }
}

int main(int argc, char** argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: %s <rom.sfc> <movie.zmv> <first> <last> [--pictures N] [--no-smooth]\n"
                    "       [--widescreen off|16:9|16:10|21:9] [--scale sx,sy] [--wav file]\n"
                    "       [--red-blood] [--flashing-radar] [--hitbox pct] [--poke spec]...\n"
                    "       [--stock] [--bypass-logos] [--no-even] [--no-effect-overlay]\n", argv[0]);
    return 2;
  }
  int rom_len;
  uint8_t* rom = read_file(argv[1], &rom_len);
  if (!rom) { fprintf(stderr, "error: cannot read '%s'\n", argv[1]); return 1; }
  Movie movie;
  if (!movie_load(&movie, argv[2])) { fprintf(stderr, "error: cannot load movie '%s'\n", argv[2]); return 1; }
  const int first = atoi(argv[3]), last = atoi(argv[4]);
  int pictures = 1, sx = 12, sy = 10, hitbox = 0;
  bool smooth = true, even = true, stock = false, red_blood = false, radar_flash = false;
  bool bypass_logos = false, overlay = true;
  const char* wav_path = NULL;
  WideMode wide = WIDE_OFF;
  PokeList pokes = {{{0}}, 0};
  for (int i = 5; i < argc; i++) {
    const char* a = argv[i];
    if (!strcmp(a, "--pictures") && i + 1 < argc) pictures = atoi(argv[++i]);
    else if (!strcmp(a, "--no-smooth")) smooth = false;
    else if (!strcmp(a, "--no-even")) even = false;
    else if (!strcmp(a, "--stock")) stock = true;
    else if (!strcmp(a, "--red-blood")) red_blood = true;
    else if (!strcmp(a, "--flashing-radar")) radar_flash = true;
    else if (!strcmp(a, "--bypass-logos")) bypass_logos = true;
    else if (!strcmp(a, "--no-effect-overlay")) overlay = false;
    else if (!strcmp(a, "--hitbox") && i + 1 < argc) hitbox = atoi(argv[++i]);
    else if (!strcmp(a, "--wav") && i + 1 < argc) wav_path = argv[++i];
    else if (!strcmp(a, "--scale") && i + 1 < argc) {
      if (sscanf(argv[++i], "%d,%d", &sx, &sy) != 2 || sx < 1 || sy < 1) { fprintf(stderr, "error: --scale sx,sy\n"); return 2; }
    } else if (!strcmp(a, "--widescreen") && i + 1 < argc) {
      if (!wide_parse(argv[++i], &wide)) { fprintf(stderr, "error: --widescreen wants off, 16:9, 16:10 or 21:9\n"); return 2; }
    } else if (!strcmp(a, "--poke") && i + 1 < argc) {
      if (!poke_parse(&pokes, argv[++i])) return 2;
    } else { fprintf(stderr, "error: unknown option '%s'\n", a); return 2; }
  }
  if (pictures < 1) pictures = 1;
  if (hitbox) actor_overlap_reach = (OVERLAP_REACH_STOCK * hitbox + 50) / 100;

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) { fprintf(stderr, "error: not a ROM\n"); return 1; }
  // The frontend reads the chip's registers from `src/video`, which keeps
  // them beside the PPU's, and takes the picture from it. The PPU draws the
  // picture here.
  static VideoHook video_hook;
  video_hook_install(&video_hook, snes->ppu, VIDEO_EMULATED, false);
  video_hook_keep_registers(&video_hook, snes->ppu);
  video_set_pixel_format(snes->ppu, VIDEO_PIXELS_XRGB);
  // As the frontend sets the machine up: the widened picture and its hook,
  // which also draws the radar and the blood, and the game over's line.
  static Widescreen ws;
  video_set_margins(snes->ppu, wide_margin(wide), wide_margin(wide));
  widescreen_install(snes, &ws, rom, rom_len, wide_margin(wide));
  ws.radar.steady = !radar_flash;
  maskline_fix(snes->cart);
  if (bypass_logos && !intro_bypass(snes->cart)) { fprintf(stderr, "error: --bypass-logos does not know this ROM\n"); return 1; }
  if (red_blood) {
    if (!blood_patch_rom(snes->cart->rom, (size_t)snes->cart->romSize)) { fprintf(stderr, "error: --red-blood does not know this ROM\n"); return 1; }
    ws.blood.on = true;
  }
  snes_reset(snes, true);
  static SfxOverlay sfx;
  sfx_overlay_init(&sfx, snes, overlay);

  Cosim cosim;
  cosim_init(&cosim, snes, COSIM_NATIVE);
  if (!stock) cosim_enable_all(&cosim);
  cosim_watch(&cosim, WS_PASS_DONE_AT, widescreen_pass_done, &ws);
  cosim_watch(&cosim, WS_OAM_SENT_AT, widescreen_oam_sent, &ws);

  const int W = 256 + 2 * wide_margin(wide), tw = W * sx, th = LAYERS_LINES * sy;
  fprintf(stderr, "zamn_record: %dx%d, %d picture%s a tick, frames %d-%d\n", tw, th, pictures,
          pictures == 1 ? "" : "s", first, last);
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  FILE* wav = NULL;
  if (wav_path) {
    wav = fopen(wav_path, "wb");
    if (!wav) { fprintf(stderr, "error: cannot write '%s'\n", wav_path); return 1; }
    wav_header(wav, 0);
  }
  uint32_t wav_samples = 0;
  int16_t audio[SAMPLES_PER_TICK * 2];

  LayersFrame* frames[2];
  frames[0] = (LayersFrame*)calloc(1, sizeof(LayersFrame));
  frames[1] = (LayersFrame*)calloc(1, sizeof(LayersFrame));
  LayersOp* ops = (LayersOp*)malloc(sizeof(LayersOp) * LAYERS_MAX_OPS);
  uint8_t* rgb = (uint8_t*)malloc((size_t)tw * th * 3);
  int cur = 0;
  uint32_t last_serial = sprite_oam_owners.serial;
  static SpriteOamOwners held, used;
  bool held_fresh = false, used_ok = false;
  long fallbacks = 0;

  for (int i = 0; i < last; i++) {
    movie_apply(&movie, snes, i);
    poke_apply(&pokes, snes->ram, i);
    cosim_frame(&cosim);
    sfx_overlay_tick(&sfx);
    snes_setSamples(snes, audio, SAMPLES_PER_TICK);
    sfx_overlay_mix(&sfx, audio, SAMPLES_PER_TICK);
    // Taken every tick, drawn or not, so the first drawn has a last to ease from.
    const bool fresh = sprite_oam_owners.serial != last_serial;
    last_serial = sprite_oam_owners.serial;
    LayersFrame* f = frames[cur];
    LayersFrame* prev = frames[cur ^ 1];
    const SpriteOamOwners* game =
        held_fresh ? &held : used_ok && layers_owners_stand(prev, snes->ppu, used.rec) ? &used : NULL;
    if (game && game != &used) used = *game;
    used_ok = game != NULL;
    SpriteOamOwners with;
    const SpriteOamOwners* own = ws_owners(&ws, game, &with);
    layers_capture(f, snes->ppu, own ? own->rec : NULL, own ? own->ox : NULL, own ? own->oy : NULL);
    held = sprite_oam_owners;
    held_fresh = fresh;
    layers_link(f, prev);
    cur ^= 1;
    if (i + 1 < first) continue;
    if (wav) { fwrite(audio, 4, SAMPLES_PER_TICK, wav); wav_samples += SAMPLES_PER_TICK; }
    for (int k = 1; k <= pictures; k++) {
      const int n = f->layered ? layers_list(f, smooth ? k : pictures, pictures, sx, sy, smooth && even, ops) : 0;
      if (n > 0 && f->width == W) {
        layers_render(f, ops, n, rgb, tw, th);
      } else {
        render_fb(f, W, sx, sy, rgb);
        fallbacks += k == 1;
      }
      if (fwrite(rgb, 1, (size_t)tw * th * 3, stdout) != (size_t)tw * th * 3) {
        fprintf(stderr, "error: the reader went away at frame %d\n", i + 1);
        return 1;
      }
    }
  }
  fflush(stdout);
  if (wav) { fseek(wav, 0, SEEK_SET); wav_header(wav, wav_samples); fclose(wav); }
  fprintf(stderr, "zamn_record: %d ticks, %ld drawn from the PPU's picture\n", last - first + 1, fallbacks);
  sfx_overlay_free(&sfx);
  cosim_free(&cosim);
  movie_free(&movie);
  snes_free(snes);
  return 0;
}
