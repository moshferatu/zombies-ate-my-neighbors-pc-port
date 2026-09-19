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
// Picture 0 is the unmodified PPU frame at the same size. `--png-range`
// captures a sequence; `--stock` runs the ROM without native substitutions.
// `--check-frame-step` also fails if a frontend tick skips video frames in the
// selected interval. This catches the title's decompression stall, which a
// pixel comparison alone misses: all returned pictures were correct, but the
// previous picture stayed on screen while 75 unreturned frames were rendered.
// Use a title interval (e.g. level1.zmv 1250 1620); other intervals can include
// the core's separate, atomic DMA transfers that also span video boundaries.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "snes.h"

#include "analysis/movie_apply.h"
#include "layers.h"
#include "poke.h"
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
    fprintf(stderr, "usage: %s <rom.sfc> <movie.zmv> <first> <last> [--png prefix frame]\n"
                    "       [--png-range prefix first last] [--stock] [--check-frame-step]\n"
                    "       [--track first last] [--motion first last] [--widescreen off|16:9|16:10]\n"
                    "       [--no-even] [--no-hold] [--poke frame[+]:addr=value[.b]]...\n"
                    "       [--hitbox percent] [--watch addr]...\n"
                    "  <last> is a frame number, and the run goes on to it past the movie's end.\n", argv[0]);
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
  int png_frame = -1, png_last = -1;
  bool stock = false;
  bool check_frame_step = false;
  int track_first = -1, track_last = -1;
  PokeList pokes = {{{0}}, 0};
  int watch[8], watches = 0, watched[8] = {0};
  WideMode wide = WIDE_OFF;
  // `--no-hold` pairs the owner table with the picture of the same tick, which
  // is wrong by a tick (see `SpriteOamOwners`) and is kept so that the
  // difference can be measured rather than remembered.
  bool hold = true;
  // `--no-even` traces and draws the pictures eased straight from tick to
  // tick, as the frontend does under `--no-even`.
  bool even = true;
  // `--motion first last`: one line a tick of how far the view and each
  // record moved, on the screen and (adding the view's move back) in the
  // world -- the numbers the pictures between ticks are made from, which is
  // where a motion that is uneven in the game itself shows up.
  int motion_first = -1, motion_last = -1;
  for (int i = 5; i < argc; i++) {
    if (!strcmp(argv[i], "--stock")) { stock = true; continue; }
    if (!strcmp(argv[i], "--check-frame-step")) { check_frame_step = true; continue; }
    if (!strcmp(argv[i], "--png-range") && i + 3 < argc) {
      png = argv[++i]; png_frame = atoi(argv[++i]); png_last = atoi(argv[++i]); continue;
    }
    if (!strcmp(argv[i], "--no-hold")) { hold = false; continue; }
    if (!strcmp(argv[i], "--no-even")) { even = false; continue; }
    // `--hitbox pct`, as the frontend has it (`actor_overlap_reach`), and
    // `--watch addr`: a line whenever that WRAM word changes -- between them,
    // the frame a pickup lands on at one reach and at another.
    if (!strcmp(argv[i], "--hitbox") && i + 1 < argc) {
      actor_overlap_reach = (OVERLAP_REACH_STOCK * atoi(argv[++i]) + 50) / 100;
      continue;
    }
    if (!strcmp(argv[i], "--watch") && i + 1 < argc && watches < 8) {
      watch[watches++] = (int)strtol(argv[++i], NULL, 16) & 0x1fffe;
      continue;
    }
    if (!strcmp(argv[i], "--poke") && i + 1 < argc) { if (!poke_parse(&pokes, argv[++i])) return 2; continue; }
    if (!strcmp(argv[i], "--png") && i + 2 < argc) { png = argv[i + 1]; png_frame = atoi(argv[i + 2]); i += 2; }
    else if (!strcmp(argv[i], "--track") && i + 2 < argc) { track_first = atoi(argv[i + 1]); track_last = atoi(argv[i + 2]); i += 2; }
    else if (!strcmp(argv[i], "--motion") && i + 2 < argc) { motion_first = atoi(argv[i + 1]); motion_last = atoi(argv[i + 2]); i += 2; }
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
  if (!stock) cosim_enable_all(&cosim);

  LayersFrame* frames[2];
  frames[0] = (LayersFrame*)calloc(1, sizeof(LayersFrame));
  frames[1] = (LayersFrame*)calloc(1, sizeof(LayersFrame));
  LayersOp* ops = (LayersOp*)malloc(sizeof(LayersOp) * LAYERS_MAX_OPS);
  uint8_t* rgb = (uint8_t*)malloc((size_t)PPU_MAX_WIDTH * 4 * LAYERS_LINES * 4 * 3);
  int cur = 0;
  uint32_t last_serial = sprite_oam_owners.serial;
  static SpriteOamOwners held;
  bool held_fresh = false;
  long link_near = 0, link_origin = 0, link_looks = 0, link_none = 0, link_jump = 0, origin_moved = 0, window_held = 0, steps_spread = 0, steps_cut = 0, lines_moved = 0, lines_spread = 0, bg3_anchored = 0, bg3_mask = 0;

  long tested = 0, identical = 0, within_one = 0, differing = 0, unexpressible = 0, dropped = 0;
  long skipped_frames = 0;
  long eased_ticks = 0, sprites_known = 0, sprites_drawn = 0, owners_fresh = 0;
  const char* reason[MAX_REASONS]; long reason_count[MAX_REASONS]; int reasons = 0;
  int worst_diff = 0; long worst_frame = -1;

  for (int i = 0; i < last; i++) {
    movie_apply(&movie, snes, i);
    poke_apply(&pokes, snes->ram, i);
    const uint32_t before_frame = snes->frames;
    cosim_frame(&cosim);
    for (int k = 0; k < watches; k++) {
      const int v = snes->ram[watch[k]] | (snes->ram[watch[k] + 1] << 8);
      if (v != watched[k]) printf("  watch %05x: frame %d: %04x -> %04x\n", watch[k], i + 1, watched[k], v);
      watched[k] = v;
    }
    // Native calls must return each video frame even while NMI is disabled.
    // Otherwise the last bouncing title frame stays up while decompression
    // silently renders the entire stationary hold in one frontend tick.
    const uint32_t advanced = snes->frames - before_frame;
    if (i + 1 >= first && advanced > 1) {
      skipped_frames += advanced - 1;
      if (check_frame_step)
        fprintf(stderr, "frame %d: skipped %u video frames\n", i + 1, advanced - 1);
    }
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
      if (motion_first >= 0 && frame >= motion_first && frame <= motion_last) {
        printf("  frame %d: %s", frame, f->why);
        if (ppu->midFrameWrite)
          printf(" -- first $21%02x on line %d, %d writes", ppu->midFrameAdr, ppu->midFrameLine,
                 ppu->midFrameWrites);
        printf("\n");
        if (frame == motion_first) {
          // The windowing and maths registers as the frame ended, once, for
          // working out what a screen that cannot be a draw list is doing.
          printf("    w1 %d..%d w2 %d..%d clip %d preventMath %d addSub %d subtract %d half %d fixed %d,%d,%d\n",
                 ppu->window1left, ppu->window1right, ppu->window2left, ppu->window2right, ppu->clipMode,
                 ppu->preventMathMode, ppu->addSubscreen, ppu->subtractColor, ppu->halfColor,
                 ppu->fixedColorR, ppu->fixedColorG, ppu->fixedColorB);
          for (int l = 0; l < 6; l++)
            printf("    %s: w1 %d%s w2 %d%s logic %d%s\n",
                   l < 4 ? (const char*[]){"bg1", "bg2", "bg3", "bg4"}[l] : l == 4 ? "sprites" : "colour",
                   ppu->windowLayer[l].window1enabled, ppu->windowLayer[l].window1inversed ? " inv" : "",
                   ppu->windowLayer[l].window2enabled, ppu->windowLayer[l].window2inversed ? " inv" : "",
                   ppu->windowLayer[l].maskLogic, l < 4 && ppu->layer[l].mainScreenEnabled ? " main" : "");
          for (int l = 0; l < 4; l++)
            printf("    bg%d: main %d sub %d mainWindowed %d subWindowed %d math %d\n", l + 1,
                   ppu->layer[l].mainScreenEnabled, ppu->layer[l].subScreenEnabled,
                   ppu->layer[l].mainScreenWindowed, ppu->layer[l].subScreenWindowed, ppu->mathEnabled[l]);
          printf("    sprites math %d backdrop math %d\n", ppu->mathEnabled[4], ppu->mathEnabled[5]);
        }
      }
    } else {
      const int n = layers_list(f, 1, 1, 1, 1, false, ops);
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
        link_looks += f->linkLooks; link_none += f->linkNone; link_jump += f->linkJump;
        window_held += f->mathHeld;
        if (f->anchored[2]) bg3_anchored++;
        if (ppu->layerWide[2] == ppu_wideCentre) bg3_mask++;
        for (int l = 0; l < 4; l++) if (f->stepK[l]) steps_spread++;
        lines_moved += f->linesMoved;
        lines_spread += f->linesSpread;
        // A spread that a move arrived in the middle of was a wrong guess,
        // and shows as a crawl and a leap.
        for (int l = 0; l < 4; l++)
          if (f->ease && prev->stepK[l] && prev->stepI[l] + 1 < prev->stepK[l] && (f->dScrollX[l] - (f->anchored[l] ? 0 : f->dExtraLeft) || f->dScrollY[l])) {
            steps_cut++;
            printf("  frame %d: background %d moved %d ticks into a step spread over %d\n", frame, l + 1,
                   prev->stepI[l] + 1, prev->stepK[l]);
          }
        if (f->dExtraLeft != 0) origin_moved++;
      }
      for (int s = 0; s < LAYERS_SPRITES; s++) {
        if (!f->spr[s].drawn) continue;
        sprites_drawn++;
        if (f->spr[s].known) sprites_known++;
      }
      if (motion_first >= 0 && frame >= motion_first && frame <= motion_last) {
        printf("  frame %d%s: view %+d,%+d;", frame, f->ease ? "" : " (not eased)", f->dScrollX[1], f->dScrollY[1]);
        // Every background's scroll from the last tick, with the ones scrolled
        // per line (which the draw list cannot move) marked.
        printf(" bg");
        for (int l = 0; l < 4; l++) {
          if (!f->main[l] && !f->sub[l]) { printf(" -"); continue; }
          printf(" %+d,%+d%s[%u,%u]", f->dScrollX[l], f->dScrollY[l], f->raster[l] ? "r" : "", f->scrollX[l], f->scrollY[l]);
          if (f->stepK[l]) printf("(step %+d,%+d %d/%d)", f->stepDX[l], f->stepDY[l], f->stepI[l], f->stepK[l]);
        }
        printf(";");
        for (int r = 0; r < f->mathRects; r++)
          printf(" maths %d,%d %dx%d shown %d,%d %dx%d;", f->mathRect[r].x, f->mathRect[r].y, f->mathRect[r].w,
                 f->mathRect[r].h, f->mathShown[r].x, f->mathShown[r].y, f->mathShown[r].w, f->mathShown[r].h);
        int last_rec = -2;
        for (int s = 0; s < LAYERS_SPRITES; s++) {
          const LayersSprite* sp = &f->spr[s];
          if (!sp->drawn || !sp->known || sp->rec == last_rec) continue;
          last_rec = sp->rec;
          // A record's own origin where it has one, which an animation frame
          // changing the pieces does not move; the piece itself where not.
          int dx = sp->x - sp->px + f->dExtraLeft, dy = sp->y - sp->py;
          if (sp->rec >= 0)
            for (int q = 0; q < LAYERS_SPRITES; q++)
              if (prev->spr[q].drawn && prev->spr[q].rec == sp->rec) {
                dx = sp->ox - prev->spr[q].ox;
                dy = sp->oy - prev->spr[q].oy;
                break;
              }
          printf(" %04x screen %+d,%+d world %+d,%+d;", sp->rec & 0xffff, dx, dy, dx + f->dScrollX[1],
                 dy + f->dScrollY[1]);
        }
        for (int s = 0; s < LAYERS_SPRITES; s++)
          if (f->spr[s].placed)
            printf("    placed: rec %04x slot %d at %d,%d moved %+d,%+d\n", f->spr[s].rec & 0xffff, s,
                   f->spr[s].x, f->spr[s].y, f->spr[s].mx, f->spr[s].my);
        printf("\n");
      }
      if (track_first >= 0 && frame >= track_first && frame <= track_last) {
        // Where each drawn sprite is put in each of the four pictures of this
        // tick, at four times the console: a sprite eased right advances four
        // target pixels per picture per game pixel of motion. Printed per
        // record so that an actor's pieces can be read together.
        for (int k = 1; k <= 4; k++) {
          const int m = layers_list(f, k, 4, 4, 4, even, ops);
          printf("  frame %d picture %d (left margin %d, moved %+d; planes", frame, k, f->extraLeft,
                 f->dExtraLeft);
          for (int o = 0; o < m; o++)
            if (ops[o].kind == LAYERS_OP_PLANE)
              printf(" %d:%+d,%+d", ops[o].plane, ops[o].dx + LAYERS_MARGIN * 4, ops[o].dy + LAYERS_MARGIN * 4);
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
      if (png && frame >= png_frame && frame <= (png_last < 0 ? png_frame : png_last)) {
        printf("  frame %d: mode %d bg3prio %d width %d; main %d%d%d%d sub %d%d%d%d raster %d%d%d%d;"
               " subAdd %d subLayer %d mathMain %d%d%d%d%d%d; cell %d; %d ops\n",
               frame, f->mode, f->bg3prio, f->width, f->main[0], f->main[1], f->main[2], f->main[3],
               f->sub[0], f->sub[1], f->sub[2], f->sub[3], f->raster[0], f->raster[1], f->raster[2],
               f->raster[3], f->subAdd, f->subLayer, f->mathMain[0], f->mathMain[1], f->mathMain[2],
               f->mathMain[3], f->mathMain[4], f->mathMain[5], f->cell, n);
        if (f->mathGated) {
          // The maths gate column by column on one line of the window, with
          // the edges that line had, for when a rectangle is not the box.
          printf("    line 80: windows %d..%d %d..%d, wide policy %d%d%d%d, gate from column %d:", ppu->lineWindow[80][0],
                 ppu->lineWindow[80][1], ppu->lineWindow[80][2], ppu->lineWindow[80][3], ppu->layerWide[0],
                 ppu->layerWide[1], ppu->layerWide[2], ppu->layerWide[3], -ppu->extraLeft);
          for (int x = -ppu->extraLeft; x < 256 + ppu->extraRight; x++) printf("%c", ppu_mathAllowedAt(ppu, x, 80) ? '#' : '.');
          printf("\n");
        }
        // Which of BG3's columns are empty on every line, as runs -- what
        // `widescreen_frame` tells the status panel from the game over's
        // mask by.
        printf("    hud_panel_on: %d %d; BG3 empty columns:", snes->ram[0x1e88] | (snes->ram[0x1e89] << 8),
               snes->ram[0x1e8a] | (snes->ram[0x1e8b] << 8));
        for (int c = 0, from = -1; c <= 256; c++) {
          const bool empty = c < 256 && ppu_columnEmptyAt(ppu, 2, c);
          if (empty && from < 0) from = c;
          if (!empty && from >= 0) { printf(" %d-%d", from, c - 1); from = -1; }
        }
        printf("\n");
        {
          int anchored = 0, centred = 0;
          for (int s = 0; s < LAYERS_SPRITES; s++) {
            if (!f->spr[s].drawn) continue;
            anchored += f->spr[s].place == ppu_spriteAnchored;
            centred += f->spr[s].place == ppu_spriteCentred;
          }
          printf("    sprites placed: %d anchored, %d centred\n", anchored, centred);
        }
        {
          // Video memory as it stands, and where each background's map is in
          // it -- for asking what a map holds beyond the console's 256.
          char vpath[600];
          snprintf(vpath, sizeof vpath, "%s.%d.vram.bin", png, frame);
          FILE* vf = fopen(vpath, "wb");
          if (vf) { fwrite(ppu->vram, sizeof ppu->vram[0], 0x8000, vf); fclose(vf); }
          // ...the scroll of every line of a background that has a raster
          // effect on it, one "line h v" a row...
          for (int l = 0; l < 4; l++) {
            if (!f->raster[l]) continue;
            snprintf(vpath, sizeof vpath, "%s.%d.scroll%d.txt", png, frame, l);
            vf = fopen(vpath, "w");
            if (!vf) continue;
            for (int y = 1; y <= 224; y++) fprintf(vf, "%d %d %d" "\n", y, ppu->lineHScroll[l][y], ppu->lineVScroll[l][y]);
            fclose(vf);
          }
          // ...and work RAM, for finding where the game keeps something.
          snprintf(vpath, sizeof vpath, "%s.%d.wram.bin", png, frame);
          vf = fopen(vpath, "wb");
          if (vf) { fwrite(snes->ram, 1, 0x20000, vf); fclose(vf); }
          printf("    maps:");
          for (int l = 0; l < 4; l++)
            printf(" %04x%s%s%s/%04x", ppu->bgLayer[l].tilemapAdr, ppu->bgLayer[l].tilemapWider ? "w" : "",
                   ppu->bgLayer[l].tilemapHigher ? "h" : "", ppu->bgLayer[l].bigTiles ? "b" : "",
                   ppu->bgLayer[l].tileAdr);
          printf("\n");
        }
        if (f->mathGated)
          for (int r = 0; r < f->mathRects; r++)
            printf("    maths window rectangle %d: %d,%d %dx%d\n", r, f->mathRect[r].x, f->mathRect[r].y,
                   f->mathRect[r].w, f->mathRect[r].h);
        for (int k = 0; k < n && k < 24; k++)
          printf("    op %2d: %s %s plane %d slot %d src %d,%d %dx%d dst %d,%d %dx%d mask %d\n", k,
                 ops[k].kind == LAYERS_OP_PLANE ? "plane " : "sprite",
                 ops[k].blend == LAYERS_BLEND_COPY ? "copy" : ops[k].blend == LAYERS_BLEND_ADD ? "add " : "sub ",
                 ops[k].plane, ops[k].slot, ops[k].sx, ops[k].sy, ops[k].sw, ops[k].sh, ops[k].dx, ops[k].dy,
                 ops[k].dw, ops[k].dh, ops[k].mask);
        const int S = 4;
        uint8_t* big = (uint8_t*)malloc((size_t)W * S * LAYERS_LINES * S * 3);
        for (int y = 0; y < LAYERS_LINES * S; y++)
          for (int x = 0; x < W * S; x++) {
            const uint8_t* p = layers_fb_pixel(f, x / S, y / S);
            uint8_t* out = big + ((size_t)y * W * S + x) * 3;
            out[0] = p[2]; out[1] = p[1]; out[2] = p[0];
          }
        char raw_path[512];
        snprintf(raw_path, sizeof raw_path, "%s.%d.0.png", png, frame);
        stbi_write_png(raw_path, W * S, LAYERS_LINES * S, 3, big, W * S * 3);
        for (int k = 1; k <= 4; k++) {
          const int m = layers_list(f, k, 4, S, S, even, ops);
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
         " record's origin, %ld by looks, %ld not at all, and %ld of those paired were placed, not"
         " moved;  the picture's origin moved on %ld ticks\n",
         link_near, link_origin, link_looks, link_none, link_jump, origin_moved);
  if (window_held) printf("  the maths window's rectangles were held where they wandered on %ld ticks\n", window_held);
  if (bg3_anchored) printf("  BG3 was anchored to the picture's edges on %ld frames\n", bg3_anchored);
  if (wide != WIDE_OFF)
    printf("  screen-space sprites: the pass on screen was not the newest on %ld frames, and none of those kept on %ld\n",
           ws.place_behind, ws.place_unmatched);
  if (bg3_mask) printf("  BG3 carried the game over mask on %ld frames\n", bg3_mask);
  if (lines_moved)
    printf("  a background waved a line at a time: %ld line-ticks eased, %ld of a step being spread\n",
           lines_moved, lines_spread);
  if (steps_spread) printf("  a background stepping every few ticks had its step spread on %ld background-ticks, %ld spreads cut short by a move\n", steps_spread, steps_cut);
  const bool ok = differing == 0 && (!check_frame_step || skipped_frames == 0);
  printf("  skipped video frames: %ld\n", skipped_frames);
  printf(ok ? "OK\n" : "FAIL: %ld differing frames, %ld skipped video frames\n", differing, skipped_frames);
  cosim_free(&cosim);
  movie_free(&movie);
  snes_free(snes);
  free(rom); free(frames[0]); free(frames[1]); free(ops); free(rgb);
  return ok ? 0 : 1;
}
