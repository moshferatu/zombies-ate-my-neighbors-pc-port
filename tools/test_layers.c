// What `src/layers.h` promises, checked against the PPU over real frames.
//
// The claim is that a frame taken apart into planes and sprites and drawn
// again as a list, unmoved and at the console's own size, is the frame the
// PPU drew. That cannot be checked without a game to draw, so this one runs
// a movie and asks on every frame:
//
//   * can the frame be a draw list at all, and if not, why -- so that "how
//     often does the frontend fall back to the PPU's picture" is a measured
//     number per screen rather than a hope;
//   * is the list, drawn in software, byte-identical to the PPU's frame; and
//     where it is not, how far off and over how many pixels -- the sub screen
//     is added in eight bits where the console adds in five, which is a
//     difference of one, and anything larger is a bug.
//
// A frame the console drew with sprites dropped for want of line time
// (`rangeOver`/`timeOver`) is reported separately: the list draws every
// sprite, on purpose, and so cannot match such a frame.
//
//     zamn_test_layers <rom.sfc> <movie.zmv> <first> <last> [--png prefix frame]
//
// `--png` writes the four pictures of one tick at four times the console's
// size, for looking at what the easing does with the eye.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "snes.h"

#include "analysis/movie_apply.h"
#include "layers.h"
#include "port/oam.h"
#include "cosim/cosim.h"
#include "scale.h"
#include "widescreen.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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

#define MAX_REASONS 16

int main(int argc, char** argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: %s <rom.sfc> <movie.zmv> <first> <last> [--png prefix frame]\n", argv[0]);
    return 2;
  }
  int rom_len;
  uint8_t* rom = read_file(argv[1], &rom_len);
  if (!rom) { fprintf(stderr, "error: cannot read '%s'\n", argv[1]); return 1; }
  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) { fprintf(stderr, "error: not a ROM\n"); return 1; }
  snes_setPixelFormat(snes, pixelFormatXRGB);
  snes_reset(snes, true);
  Movie movie;
  if (!movie_load(&movie, argv[2])) { fprintf(stderr, "error: cannot load movie '%s'\n", argv[2]); return 1; }
  const int first = atoi(argv[3]), last = atoi(argv[4]);
  const char* png = NULL;
  int png_frame = -1;
  int track_first = -1, track_last = -1;
  WideMode wide = WIDE_OFF;
  // `--no-hold` pairs the owner table with the picture of the same tick, which
  // is wrong by a tick (see `SpriteOamOwners`) and is kept so that the
  // difference can be measured rather than remembered.
  bool hold = true;
  for (int i = 5; i < argc; i++) {
    if (!strcmp(argv[i], "--no-hold")) { hold = false; continue; }
    if (!strcmp(argv[i], "--png") && i + 2 < argc) { png = argv[i + 1]; png_frame = atoi(argv[i + 2]); i += 2; }
    else if (!strcmp(argv[i], "--track") && i + 2 < argc) { track_first = atoi(argv[i + 1]); track_last = atoi(argv[i + 2]); i += 2; }
    else if (!strcmp(argv[i], "--widescreen") && i + 1 < argc) {
      if (!wide_parse(argv[++i], &wide)) { fprintf(stderr, "error: --widescreen wants off, 16:9 or 16:10\n"); return 2; }
    }
  }

  // The port's sprite pass runs in the frontend by default, and its owner
  // table is what the pictures move sprites by: run it here the same way, so
  // the frames tested are the frames played.
  // The picture widened as the frontend widens it, which puts sprites of the
  // frontend's own into OAM and moves the picture's origin at the ends of a
  // map -- two things the pictures between ticks have to follow.
  static Widescreen ws;
  if (wide != WIDE_OFF) {
    snes_setWidescreen(snes, wide_margin(wide), wide_margin(wide));
    widescreen_install(snes, &ws, rom, rom_len, wide_margin(wide));
  }

  Cosim cosim;
  cosim_init(&cosim, snes, COSIM_NATIVE);
  cosim_enable_all(&cosim);

  LayersFrame* frames[2];
  frames[0] = (LayersFrame*)calloc(1, sizeof(LayersFrame));
  frames[1] = (LayersFrame*)calloc(1, sizeof(LayersFrame));
  LayersOp* ops = (LayersOp*)malloc(sizeof(LayersOp) * LAYERS_MAX_OPS);
  uint8_t* rgb = (uint8_t*)malloc((size_t)PPU_MAX_WIDTH * 4 * LAYERS_LINES * 4 * 3);
  int cur = 0;
  uint32_t last_serial = sprite_oam_owners.serial;
  static SpriteOamOwners held;
  bool held_fresh = false;
  long link_near = 0, link_origin = 0, link_looks = 0, link_none = 0, origin_moved = 0;

  long tested = 0, identical = 0, within_one = 0, differing = 0, unexpressible = 0, dropped = 0;
  long eased_ticks = 0, sprites_known = 0, sprites_drawn = 0, owners_fresh = 0;
  const char* reason[MAX_REASONS]; long reason_count[MAX_REASONS]; int reasons = 0;
  int worst_diff = 0; long worst_frame = -1;

  for (int i = 0; i < last; i++) {
    movie_apply(&movie, snes, i);
    cosim_frame(&cosim);
    if (i + 1 < first) continue;
    const int frame = i + 1;
    Ppu* ppu = snes->ppu;
    const bool fresh = sprite_oam_owners.serial != last_serial;
    last_serial = sprite_oam_owners.serial;
    LayersFrame* f = frames[cur];
    LayersFrame* prev = frames[cur ^ 1];
    // The table is a tick ahead of the picture, so the picture is paired with
    // the table as it stood a tick ago.
    const SpriteOamOwners* own = hold ? &held : &sprite_oam_owners;
    const bool own_fresh = hold ? held_fresh : fresh;
    layers_capture(f, ppu, own_fresh ? own->rec : NULL, own_fresh ? own->ox : NULL,
                   own_fresh ? own->oy : NULL);
    held = sprite_oam_owners;
    held_fresh = fresh;
    if (f->ownersFresh) owners_fresh++;
    layers_link(f, prev);
    tested++;
    if (!f->layered) {
      unexpressible++;
      int r = 0;
      for (; r < reasons; r++) if (!strcmp(reason[r], f->why)) break;
      if (r == reasons && reasons < MAX_REASONS) { reason[reasons] = f->why; reason_count[reasons] = 0; reasons++; }
      if (r < reasons) reason_count[r]++;
    } else {
      const int n = layers_list(f, 1, 1, 1, 1, ops);
      const int W = f->width;
      layers_render(f, ops, n, rgb, W, LAYERS_LINES);
      int maxd = 0; long off = 0;
      for (int y = 0; y < LAYERS_LINES; y++) {
        for (int x = 0; x < W; x++) {
          const uint8_t* a = layers_fb_pixel(f, x, y);  // B, G, R, X
          const uint8_t* b = rgb + ((size_t)y * W + x) * 3;  // R, G, B
          int d = abs(a[2] - b[0]);
          if (abs(a[1] - b[1]) > d) d = abs(a[1] - b[1]);
          if (abs(a[0] - b[2]) > d) d = abs(a[0] - b[2]);
          if (d > 0) off++;
          if (d > maxd) maxd = d;
        }
      }
      if (ppu->rangeOver || ppu->timeOver) dropped++;
      else if (maxd == 0) identical++;
      else if (maxd <= 1) within_one++;
      else {
        differing++;
        if (maxd > worst_diff) { worst_diff = maxd; worst_frame = frame; }
        if (differing <= 5) {
          printf("  frame %d: %ld pixels differ, by up to %d\n", frame, off, maxd);
          // ...and where the first few are, with both colours.
          int shown = 0;
          for (int y = 0; y < LAYERS_LINES && shown < 6; y++) {
            for (int x = 0; x < W && shown < 6; x++) {
              const uint8_t* a = layers_fb_pixel(f, x, y);
              const uint8_t* b = rgb + ((size_t)y * W + x) * 3;
              if (a[2] == b[0] && a[1] == b[1] && a[0] == b[2]) continue;
              printf("    (%d,%d) ppu %02x%02x%02x list %02x%02x%02x\n", x, y, a[2], a[1], a[0], b[0], b[1], b[2]);
              shown++;
            }
          }
        }
      }
      // How much of the picture the easing has something to say about.
      if (f->ease) eased_ticks++;
      if (f->ease) {
        link_near += f->linkNear; link_origin += f->linkOrigin;
        link_looks += f->linkLooks; link_none += f->linkNone;
        if (f->dExtraLeft != 0) origin_moved++;
      }
      for (int s = 0; s < LAYERS_SPRITES; s++) {
        if (!f->spr[s].drawn) continue;
        sprites_drawn++;
        if (f->spr[s].known) sprites_known++;
      }
      if (track_first >= 0 && frame >= track_first && frame <= track_last) {
        // Where each drawn sprite is put in each of the four pictures of this
        // tick, at four times the console: a sprite eased right advances four
        // target pixels per picture per game pixel of motion. Printed per
        // record so that an actor's pieces can be read together.
        for (int k = 1; k <= 4; k++) {
          const int m = layers_list(f, k, 4, 4, 4, ops);
          printf("  frame %d picture %d (left margin %d, moved %+d; planes", frame, k, f->extraLeft,
                 f->dExtraLeft);
          for (int o = 0; o < m; o++)
            if (ops[o].kind == LAYERS_OP_PLANE)
              printf(" %d:%+d", ops[o].plane, ops[o].dx + LAYERS_MARGIN * 4);
          printf("):");
          int shown = 0;
          for (int o = 0; o < m && shown < 12; o++) {
            if (ops[o].kind != LAYERS_OP_SPRITE) continue;
            const LayersSprite* sp = &f->spr[ops[o].slot];
            printf(" [rec %04x slot %d at %d,%d%s]", sp->rec & 0xffff, ops[o].slot, ops[o].dx, ops[o].dy,
                   sp->known ? "" : " ?");
            shown++;
          }
          printf("\n");
        }
      }
      if (png && frame == png_frame) {
        printf("  frame %d: mode %d bg3prio %d width %d; main %d%d%d%d sub %d%d%d%d raster %d%d%d%d;"
               " subAdd %d subLayer %d mathMain %d%d%d%d%d%d; cell %d; %d ops\n",
               frame, f->mode, f->bg3prio, f->width, f->main[0], f->main[1], f->main[2], f->main[3],
               f->sub[0], f->sub[1], f->sub[2], f->sub[3], f->raster[0], f->raster[1], f->raster[2],
               f->raster[3], f->subAdd, f->subLayer, f->mathMain[0], f->mathMain[1], f->mathMain[2],
               f->mathMain[3], f->mathMain[4], f->mathMain[5], f->cell, n);
        for (int k = 0; k < n && k < 24; k++)
          printf("    op %2d: %s %s plane %d slot %d src %d,%d %dx%d dst %d,%d %dx%d mask %d\n", k,
                 ops[k].kind == LAYERS_OP_PLANE ? "plane " : "sprite",
                 ops[k].blend == LAYERS_BLEND_COPY ? "copy" : ops[k].blend == LAYERS_BLEND_ADD ? "add " : "sub ",
                 ops[k].plane, ops[k].slot, ops[k].sx, ops[k].sy, ops[k].sw, ops[k].sh, ops[k].dx, ops[k].dy,
                 ops[k].dw, ops[k].dh, ops[k].mask);
        const int S = 4;
        uint8_t* big = (uint8_t*)malloc((size_t)W * S * LAYERS_LINES * S * 3);
        for (int k = 1; k <= 4; k++) {
          const int m = layers_list(f, k, 4, S, S, ops);
          layers_render(f, ops, m, big, W * S, LAYERS_LINES * S);
          char path[512];
          snprintf(path, sizeof path, "%s.%d.%d.png", png, frame, k);
          stbi_write_png(path, W * S, LAYERS_LINES * S, 3, big, W * S * 3);
          printf("  wrote %s (%d ops)\n", path, m);
        }
        free(big);
      }
    }
    cur ^= 1;
  }

  printf("%s frames %d-%d: %ld tested\n", argv[2], first, last, tested);
  printf("  as a draw list:  %ld identical to the PPU, %ld within one of it, %ld differing",
         identical, within_one, differing);
  if (differing) printf(" (worst %d at frame %ld)", worst_diff, worst_frame);
  printf("\n  sprites dropped by the console: %ld frames (not compared)\n", dropped);
  printf("  not a draw list: %ld frames\n", unexpressible);
  for (int r = 0; r < reasons; r++) printf("    %-40s %ld\n", reason[r], reason_count[r]);
  printf("  ticks that could be eased from the last: %ld;  sprites drawn %ld, of which"
         " %ld had a known last position;  owner table fresh on %ld ticks\n",
         eased_ticks, sprites_drawn, sprites_known, owners_fresh);
  printf("  pairing, over eased ticks: %ld by the nearest piece of the same record, %ld by the"
         " record's origin, %ld by looks, %ld not at all;  the picture's origin moved on %ld ticks\n",
         link_near, link_origin, link_looks, link_none, origin_moved);
  const bool ok = differing == 0;
  printf(ok ? "OK\n" : "FAIL: %ld frames differ by more than one\n", differing);
  cosim_free(&cosim);
  movie_free(&movie);
  snes_free(snes);
  free(rom); free(frames[0]); free(frames[1]); free(ops); free(rgb);
  return ok ? 0 : 1;
}
