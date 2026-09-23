// The launcher's icon, drawn by the game: the title screen once the logo has
// come to rest, before START and PASSWORD come up under it.
//
// It is made from the player's cartridge when the launcher is built, not kept
// in the repository, for the reason nothing else from the cartridge is kept
// there. The core boots the ROM with nothing held and runs it to frame 2180,
// which is a still of the title with no menu in it: the logo lands at about
// 2140, and the menu's sprites are first drawn at about 2260 (frames 2140 to
// 2240 all qualify; 2180 has the spiral's centre nearest the middle). Nothing
// is taken out of the picture, so nothing can be left half in it.
//
// The picture is shown at 4:3, the shape the game was drawn for and the
// launcher's default, with the bars above and below it transparent. Every
// size is an average of the pixels it covers, taken in linear light, so the
// red of the spiral does not darken as it shrinks. 256 is stored as a PNG and
// the others as bitmaps, which is what Windows asks of an icon.
//
// Usage: zamn_icon <rom.sfc> <out.ico> [out.png]
//
// The PNG, if named, is the 256 image, for looking at.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TITLE_FRAME 2180
#define SRC_W 256
#define SRC_H 224

static const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
#define SIZE_COUNT ((int)(sizeof sizes / sizeof *sizes))

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

static float to_linear(uint8_t v) {
  const float c = v / 255.0f;
  return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static uint8_t to_srgb(float c) {
  c = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
  const int v = (int)(c * 255.0f + 0.5f);
  return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

// `n` samples of width `sw` down to `dw`, each the average of what it covers.
static void shrink_line(const float* src, int sw, int stride, float* dst, int dw, int dstride) {
  const double step = (double)sw / dw;
  for (int d = 0; d < dw; d++) {
    const double a = d * step, b = a + step;
    double sum = 0;
    for (int s = (int)a; s < sw && s < b; s++) {
      const double lo = s < a ? a : s, hi = s + 1 > b ? b : s + 1;
      sum += src[s * stride] * (hi - lo);
    }
    dst[d * dstride] = (float)(sum / step);
  }
}

// The picture at `size` square: 4:3, centred, transparent above and below.
// RGBA, top row first.
static void render_size(const float* lin, int size, uint8_t* rgba) {
  const int pw = size, ph = size * 3 / 4, top = (size - ph) / 2;
  float* wide = (float*)malloc(sizeof(float) * (size_t)pw * SRC_H * 3);
  float* out = (float*)malloc(sizeof(float) * (size_t)pw * ph * 3);
  for (int y = 0; y < SRC_H; y++)
    for (int ch = 0; ch < 3; ch++)
      shrink_line(lin + (y * SRC_W) * 3 + ch, SRC_W, 3, wide + (y * pw) * 3 + ch, pw, 3);
  for (int x = 0; x < pw; x++)
    for (int ch = 0; ch < 3; ch++)
      shrink_line(wide + x * 3 + ch, SRC_H, pw * 3, out + x * 3 + ch, ph, pw * 3);
  memset(rgba, 0, (size_t)size * size * 4);
  for (int y = 0; y < ph; y++)
    for (int x = 0; x < pw; x++) {
      uint8_t* p = rgba + ((size_t)(top + y) * size + x) * 4;
      for (int ch = 0; ch < 3; ch++) p[ch] = to_srgb(out[(y * pw + x) * 3 + ch]);
      p[3] = 255;
    }
  free(wide);
  free(out);
}

static void put16(uint8_t* p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

// One icon image as a bitmap: the header, BGRA from the bottom row up, and the
// one-bit mask older renderers want, set where the picture is transparent.
static uint8_t* bitmap_entry(const uint8_t* rgba, int size, int* out_len) {
  const int mask_row = ((size + 31) / 32) * 4;
  const int len = 40 + size * size * 4 + mask_row * size;
  uint8_t* b = (uint8_t*)calloc(1, (size_t)len);
  put32(b, 40);
  put32(b + 4, (uint32_t)size);
  put32(b + 8, (uint32_t)size * 2);  // the colour and the mask, one above the other
  put16(b + 12, 1);
  put16(b + 14, 32);
  put32(b + 20, (uint32_t)(len - 40));
  uint8_t* px = b + 40;
  uint8_t* mask = px + size * size * 4;
  for (int y = 0; y < size; y++) {
    const uint8_t* row = rgba + (size_t)(size - 1 - y) * size * 4;
    for (int x = 0; x < size; x++) {
      uint8_t* p = px + ((size_t)y * size + x) * 4;
      p[0] = row[x * 4 + 2];
      p[1] = row[x * 4 + 1];
      p[2] = row[x * 4 + 0];
      p[3] = row[x * 4 + 3];
      if (!row[x * 4 + 3]) mask[y * mask_row + x / 8] |= (uint8_t)(0x80 >> (x % 8));
    }
  }
  *out_len = len;
  return b;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <rom.sfc> <out.ico> [out.png]\n", argv[0]);
    return 2;
  }
  long rom_len = 0;
  uint8_t* rom = read_file(argv[1], &rom_len);
  if (!rom) { fprintf(stderr, "error: cannot read '%s'\n", argv[1]); return 1; }
  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, (int)rom_len)) {
    fprintf(stderr, "error: the core rejected '%s'\n", argv[1]);
    return 1;
  }
  snes_setPixelFormat(snes, pixelFormatXRGB);
  snes_reset(snes, true);
  for (int i = 0; i < TITLE_FRAME; i++) snes_runFrame(snes);

  // The core's buffer is 512x480, doubled both ways, with the 224 lines of
  // the picture from row 16. Every other pixel of every other row is the
  // picture, as linear RGB.
  uint8_t* fb = (uint8_t*)malloc(512 * 480 * 4);
  float* lin = (float*)malloc(sizeof(float) * SRC_W * SRC_H * 3);
  snes_setPixels(snes, fb);
  int lit = 0;
  for (int y = 0; y < SRC_H; y++)
    for (int x = 0; x < SRC_W; x++) {
      const uint8_t* p = fb + ((size_t)(16 + y * 2) * 512 + x * 2) * 4;  // B, G, R, X
      float* o = lin + (y * SRC_W + x) * 3;
      o[0] = to_linear(p[2]);
      o[1] = to_linear(p[1]);
      o[2] = to_linear(p[0]);
      lit += (p[0] | p[1] | p[2]) != 0;
    }
  // The title is most of the screen. A cartridge that is somewhere else at
  // this frame -- another release, or not this game -- is said so rather than
  // made into an icon of a black square.
  if (lit < SRC_W * SRC_H / 4) {
    fprintf(stderr, "error: '%s' is not at the title at frame %d (%d of %d pixels lit)\n",
            argv[1], TITLE_FRAME, lit, SRC_W * SRC_H);
    return 1;
  }

  uint8_t* images[SIZE_COUNT];
  int lens[SIZE_COUNT];
  for (int i = 0; i < SIZE_COUNT; i++) {
    const int s = sizes[i];
    uint8_t* rgba = (uint8_t*)malloc((size_t)s * s * 4);
    render_size(lin, s, rgba);
    if (s == 256) {
      images[i] = stbi_write_png_to_mem(rgba, s * 4, s, s, 4, &lens[i]);
      if (argc > 3 && !stbi_write_png(argv[3], s, s, 4, rgba, s * 4)) {
        fprintf(stderr, "error: cannot write '%s'\n", argv[3]);
        return 1;
      }
    } else {
      images[i] = bitmap_entry(rgba, s, &lens[i]);
    }
    free(rgba);
    if (!images[i]) { fprintf(stderr, "error: out of memory\n"); return 1; }
  }

  FILE* f = fopen(argv[2], "wb");
  if (!f) { fprintf(stderr, "error: cannot write '%s'\n", argv[2]); return 1; }
  uint8_t head[6] = {0};
  put16(head + 2, 1);
  put16(head + 4, SIZE_COUNT);
  bool ok = fwrite(head, 1, sizeof head, f) == sizeof head;
  uint32_t offset = 6 + 16 * SIZE_COUNT;
  for (int i = 0; i < SIZE_COUNT; i++) {
    uint8_t e[16] = {0};
    e[0] = (uint8_t)(sizes[i] & 0xff);  // 0 is 256
    e[1] = (uint8_t)(sizes[i] & 0xff);
    put16(e + 4, 1);
    put16(e + 6, 32);
    put32(e + 8, (uint32_t)lens[i]);
    put32(e + 12, offset);
    offset += (uint32_t)lens[i];
    ok = ok && fwrite(e, 1, sizeof e, f) == sizeof e;
  }
  for (int i = 0; i < SIZE_COUNT; i++) ok = ok && fwrite(images[i], 1, (size_t)lens[i], f) == (size_t)lens[i];
  if (fclose(f) != 0 || !ok) { fprintf(stderr, "error: cannot write '%s'\n", argv[2]); return 1; }
  printf("Wrote %s: the title at frame %d, %d sizes from 16 to 256\n", argv[2], TITLE_FRAME, SIZE_COUNT);
  for (int i = 0; i < SIZE_COUNT; i++) free(images[i]);
  free(fb);
  free(lin);
  free(rom);
  snes_free(snes);
  return 0;
}
