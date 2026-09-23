// The launcher's heading, drawn by the game: the title screen's logo, as it
// is laid out there.
//
// It is made from the player's cartridge when the launcher is built, not kept
// in the repository, for the reason nothing else from the cartridge is kept
// there (`tools/make_icon.c` does the same for the icon). The core boots the
// ROM with nothing held and runs it to frame 2180, the still of the title the
// icon is taken from. The logo is BG1 there, alone: the spiral is BG3, and
// the menu's sprites are not up yet. The last frame is drawn twice, with
// every other layer off and the backdrop black and then white, and a pixel
// that is the same in both is the logo's. So its black outline stays, and
// what is behind it goes, with no colour taken to mean nothing.
//
// On the title the logo is three lines down a slope: ZOMBIES, then ATE MY,
// then NEIGHBORS with its TM. They are found as the pixels that touch one
// another (5 of them: ZOMBIES, ATE, MY, NEIGHBORS, TM), and sorted into the
// lines by where their middles are to be sure that is what they are. The
// picture is cut to them, with nothing moved. Nothing is scaled: the
// launcher sizes it.
//
// Usage: zamn_logo <rom.sfc> <out.h> [out.png]
//
// The header holds ZAMN_LOGO_W, ZAMN_LOGO_H and zamn_logo[], ARGB, top row
// first. The PNG, if named, is the same picture, for looking at.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "snes.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TITLE_FRAME 2180
#define SRC_W 256
#define SRC_H 224
#define PIECES 5

static uint8_t* read_file(const char* path, long* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* buf = len > 0 ? (uint8_t*)malloc((size_t)len) : NULL;
  if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); buf = NULL; }
  fclose(f);
  *out_len = len;
  return buf;
}

// The title at TITLE_FRAME with BG1 alone over a backdrop of `backdrop`, a
// SNES colour, as XRGB.
static bool draw_title(const uint8_t* rom, long rom_len, uint16_t backdrop, uint32_t* out) {
  Snes* snes = snes_init();
  if (!snes_loadRom(snes, (uint8_t*)rom, (int)rom_len)) { snes_free(snes); return false; }
  snes_setPixelFormat(snes, pixelFormatXRGB);
  snes_reset(snes, true);
  for (int i = 0; i < TITLE_FRAME - 1; i++) snes_runFrame(snes);
  Ppu* ppu = snes->ppu;
  for (int l = 1; l < 5; l++) ppu->layer[l].mainScreenEnabled = ppu->layer[l].subScreenEnabled = false;
  ppu->cgram[0] = backdrop;
  snes_runFrame(snes);
  // The core's buffer is 512x480, doubled both ways, with the 224 lines of
  // the picture from row 16.
  uint8_t* fb = (uint8_t*)malloc(512 * 480 * 4);
  snes_setPixels(snes, fb);
  for (int y = 0; y < SRC_H; y++)
    for (int x = 0; x < SRC_W; x++) {
      const uint8_t* p = fb + ((size_t)(16 + y * 2) * 512 + x * 2) * 4;  // B, G, R, X
      out[y * SRC_W + x] = (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
    }
  free(fb);
  snes_free(snes);
  return true;
}

typedef struct {
  int x0, y0, x1, y1;  // inclusive
} Box;

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <rom.sfc> <out.h> [out.png]\n", argv[0]);
    return 2;
  }
  long rom_len = 0;
  uint8_t* rom = read_file(argv[1], &rom_len);
  if (!rom) { fprintf(stderr, "error: cannot read '%s'\n", argv[1]); return 1; }
  static uint32_t dark[SRC_W * SRC_H], light[SRC_W * SRC_H];
  if (!draw_title(rom, rom_len, 0x0000, dark) || !draw_title(rom, rom_len, 0x7fff, light)) {
    fprintf(stderr, "error: the core rejected '%s'\n", argv[1]);
    return 1;
  }

  // Which piece each of the logo's pixels is in (0: none), by the eight
  // around it.
  static int piece[SRC_W * SRC_H];
  static int stack[SRC_W * SRC_H];
  Box box[PIECES];
  int pieces = 0;
  for (int i = 0; i < SRC_W * SRC_H; i++) {
    if (dark[i] != light[i] || piece[i]) continue;
    if (++pieces > PIECES) break;
    Box* b = &box[pieces - 1];
    b->x0 = b->x1 = i % SRC_W;
    b->y0 = b->y1 = i / SRC_W;
    int n = 0;
    stack[n++] = i;
    piece[i] = pieces;
    while (n) {
      const int at = stack[--n], x = at % SRC_W, y = at / SRC_W;
      if (x < b->x0) b->x0 = x;
      if (x > b->x1) b->x1 = x;
      if (y < b->y0) b->y0 = y;
      if (y > b->y1) b->y1 = y;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          const int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= SRC_W || ny >= SRC_H) continue;
          const int j = ny * SRC_W + nx;
          if (dark[j] == light[j] && !piece[j]) {
            piece[j] = pieces;
            stack[n++] = j;
          }
        }
    }
  }
  // A cartridge that is somewhere else at this frame -- another release, or
  // not this game -- is said so rather than made into a heading of something
  // else.
  if (pieces != PIECES) {
    fprintf(stderr, "error: '%s' is not at the title at frame %d (the logo there is %s%d pieces, not %d)\n",
            argv[1], TITLE_FRAME, pieces > PIECES ? "more than " : "", pieces > PIECES ? PIECES : pieces,
            PIECES);
    return 1;
  }

  // The three lines by their middles: ZOMBIES is above 100, ATE MY above 140,
  // and NEIGHBORS and its TM below. And the box around all five.
  int lines[3] = {0}, x0 = SRC_W, y0 = SRC_H, x1 = -1, y1 = -1;
  for (int p = 0; p < PIECES; p++) {
    const Box* b = &box[p];
    const int mid = (b->y0 + b->y1) / 2;
    lines[mid < 100 ? 0 : mid < 140 ? 1 : 2]++;
    if (b->x0 < x0) x0 = b->x0;
    if (b->y0 < y0) y0 = b->y0;
    if (b->x1 > x1) x1 = b->x1;
    if (b->y1 > y1) y1 = b->y1;
  }
  if (lines[0] != 1 || lines[1] != 2 || lines[2] != 2) {
    fprintf(stderr, "error: '%s' is not at the title at frame %d (its logo is not ZOMBIES / ATE MY / NEIGHBORS)\n",
            argv[1], TITLE_FRAME);
    return 1;
  }

  const int w = x1 - x0 + 1, h = y1 - y0 + 1;
  uint32_t* out = (uint32_t*)calloc((size_t)w * h, sizeof *out);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      const int i = y * SRC_W + x;
      if (piece[i]) out[(size_t)(y - y0) * w + x - x0] = 0xff000000u | dark[i];
    }

  FILE* f = fopen(argv[2], "w");
  if (!f) { fprintf(stderr, "error: cannot write '%s'\n", argv[2]); return 1; }
  fprintf(f, "// Made by tools/make_logo.c from the cartridge; not kept in the repository.\n");
  fprintf(f, "#define ZAMN_LOGO_W %d\n#define ZAMN_LOGO_H %d\n", w, h);
  fprintf(f, "static const uint32_t zamn_logo[%d] = {\n", w * h);
  for (int i = 0; i < w * h; i++) fprintf(f, "0x%08x,%s", out[i], i % 8 == 7 ? "\n" : "");
  fprintf(f, "};\n");
  if (fclose(f) != 0) { fprintf(stderr, "error: cannot write '%s'\n", argv[2]); return 1; }

  if (argc > 3) {
    uint8_t* rgba = (uint8_t*)malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
      rgba[i * 4 + 0] = (uint8_t)(out[i] >> 16);
      rgba[i * 4 + 1] = (uint8_t)(out[i] >> 8);
      rgba[i * 4 + 2] = (uint8_t)out[i];
      rgba[i * 4 + 3] = (uint8_t)(out[i] >> 24);
    }
    if (!stbi_write_png(argv[3], w, h, 4, rgba, w * 4)) {
      fprintf(stderr, "error: cannot write '%s'\n", argv[3]);
      return 1;
    }
    free(rgba);
  }
  printf("Wrote %s: the title's logo at frame %d, %dx%d\n", argv[2], TITLE_FRAME, w, h);
  free(out);
  free(rom);
  return 0;
}
