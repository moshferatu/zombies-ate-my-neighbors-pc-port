// Phase 0a headless driver: boot a SNES ROM through the vendored core, run a
// number of frames, and dump the resulting framebuffer to a PNG. This has no
// SDL / windowing dependency and exists to verify the core boots the ROM and
// renders correctly (the "reference oracle" foundation for later phases).
//
// Usage: zamn_headless <rom.sfc> <out.png> [frames]

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "snes.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// The core renders into a 512x480 (line-doubled) RGBX buffer, 2048-byte pitch.
#define FB_W 512
#define FB_H 480

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return NULL; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0) { fclose(f); fprintf(stderr, "error: empty file '%s'\n", path); return NULL; }
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
    fclose(f); free(buf); fprintf(stderr, "error: short read on '%s'\n", path); return NULL;
  }
  fclose(f);
  *out_len = (int)len;
  return buf;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <rom.sfc> <out.png> [frames]\n", argv[0]);
    return 2;
  }
  const char* rom_path = argv[1];
  const char* out_path = argv[2];
  int frames = (argc >= 4) ? atoi(argv[3]) : 180;

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) return 1;
  printf("Loaded ROM '%s' (%d bytes)\n", rom_path, rom_len);

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    free(rom);
    snes_free(snes);
    return 1;
  }
  // XRGB layout: framebuffer bytes per pixel are [B, G, R, X].
  snes_setPixelFormat(snes, pixelFormatXRGB);
  snes_reset(snes, true);

  printf("Running %d frames...\n", frames);
  for (int i = 0; i < frames; i++) {
    snes_runFrame(snes);
  }
  printf("Ran %u frames (core reports %u), %llu cpu cycles\n",
         frames, snes->frames, (unsigned long long)snes->cycles);

  uint8_t* fb = (uint8_t*)malloc(FB_W * FB_H * 4);
  snes_setPixels(snes, fb);

  // Convert XRGB(=[B,G,R,X]) -> packed RGB for the PNG.
  uint8_t* rgb = (uint8_t*)malloc(FB_W * FB_H * 3);
  for (int i = 0; i < FB_W * FB_H; i++) {
    rgb[i * 3 + 0] = fb[i * 4 + 2]; // R
    rgb[i * 3 + 1] = fb[i * 4 + 1]; // G
    rgb[i * 3 + 2] = fb[i * 4 + 0]; // B
  }

  if (!stbi_write_png(out_path, FB_W, FB_H, 3, rgb, FB_W * 3)) {
    fprintf(stderr, "error: failed to write '%s'\n", out_path);
    free(fb); free(rgb); free(rom); snes_free(snes);
    return 1;
  }
  printf("Wrote %s (%dx%d)\n", out_path, FB_W, FB_H);

  free(fb);
  free(rgb);
  free(rom);
  snes_free(snes);
  return 0;
}
