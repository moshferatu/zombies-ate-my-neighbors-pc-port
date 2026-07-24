// Phase 0a headless driver: boot a SNES ROM through the vendored core, run a
// number of frames, and dump the resulting framebuffer to a PNG. This has no
// SDL / windowing dependency and exists to verify the core boots the ROM and
// renders correctly (the "reference oracle" foundation for later phases).
//
// Usage: zamn_headless <rom.sfc> <out.png> [frames] [-m movie] [--at f,f,...]
//
// `-m` replays a movie file (`src/analysis/movie.c`) instead of holding no
// buttons, and `--at` writes one PNG per listed frame — `out.01860.png` and so
// on — rather than only the last. That pair is the movie-authoring loop: run a
// candidate script, look at where it actually got to, adjust. It is much faster
// than `zamn_trace --png` because it does not step instruction by instruction.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "snes.h"

#include "analysis/movie.h"

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

// Pull the core's framebuffer out as a PNG. Shared by the `--at` snapshots and
// the final frame, which are the same operation at different times.
static bool write_png(Snes* snes, const char* path) {
  uint8_t* fb = (uint8_t*)malloc(FB_W * FB_H * 4);
  uint8_t* rgb = (uint8_t*)malloc(FB_W * FB_H * 3);
  if (!fb || !rgb) { free(fb); free(rgb); return false; }
  snes_setPixels(snes, fb);
  // Convert XRGB(=[B,G,R,X]) -> packed RGB for the PNG.
  for (int i = 0; i < FB_W * FB_H; i++) {
    rgb[i * 3 + 0] = fb[i * 4 + 2]; // R
    rgb[i * 3 + 1] = fb[i * 4 + 1]; // G
    rgb[i * 3 + 2] = fb[i * 4 + 0]; // B
  }
  bool ok = stbi_write_png(path, FB_W, FB_H, 3, rgb, FB_W * 3) != 0;
  free(fb);
  free(rgb);
  return ok;
}

#define MAX_SNAPSHOTS 64

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr,
            "usage: %s <rom.sfc> <out.png> [frames] [-m movie] [--at f,f,...]\n",
            argv[0]);
    return 2;
  }
  const char* rom_path = argv[1];
  const char* out_path = argv[2];
  int frames = 180;
  const char* movie_path = NULL;
  int snap_at[MAX_SNAPSHOTS];
  int snap_count = 0;

  for (int i = 3; i < argc; i++) {
    bool has_next = i + 1 < argc;
    if ((!strcmp(argv[i], "-m") || !strcmp(argv[i], "--movie")) && has_next) {
      movie_path = argv[++i];
    } else if (!strcmp(argv[i], "--at") && has_next) {
      for (const char* p = argv[++i]; *p && snap_count < MAX_SNAPSHOTS;) {
        snap_at[snap_count++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
      }
    } else if (argv[i][0] != '-') {
      frames = atoi(argv[i]);
    } else {
      fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
      return 2;
    }
  }

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

  Movie movie;
  bool have_movie = false;
  if (movie_path) {
    if (!movie_load(&movie, movie_path)) {
      fprintf(stderr, "error: cannot load movie '%s'\n", movie_path);
      free(rom); snes_free(snes);
      return 1;
    }
    have_movie = true;
    printf("Replaying '%s'\n", movie_path);
  }

  printf("Running %d frames...\n", frames);
  int next_snap = 0;
  for (int i = 0; i < frames; i++) {
    if (have_movie) {
      uint16_t buttons = movie_state(&movie, i);
      for (int b = 0; b < 12; b++) snes_setButtonState(snes, 1, b, (buttons >> b) & 1);
    }
    snes_runFrame(snes);
    // `--at` frames are requested in whatever order they were typed, but they
    // are almost always ascending; walking a cursor keeps the common case free
    // and a stray out-of-order one simply does not fire.
    while (next_snap < snap_count && snap_at[next_snap] == i + 1) {
      char path[512];
      snprintf(path, sizeof path, "%.*s.%05d.png",
               (int)(strlen(out_path) - (strlen(out_path) > 4 &&
                                         !strcmp(out_path + strlen(out_path) - 4, ".png")
                                             ? 4 : 0)),
               out_path, i + 1);
      if (write_png(snes, path)) printf("  frame %d -> %s\n", i + 1, path);
      next_snap++;
    }
  }
  printf("Ran %u frames (core reports %u), %llu cpu cycles\n",
         frames, snes->frames, (unsigned long long)snes->cycles);

  if (!write_png(snes, out_path)) {
    fprintf(stderr, "error: failed to write '%s'\n", out_path);
    if (have_movie) movie_free(&movie);
    free(rom); snes_free(snes);
    return 1;
  }
  printf("Wrote %s (%dx%d)\n", out_path, FB_W, FB_H);

  if (have_movie) movie_free(&movie);
  free(rom);
  snes_free(snes);
  return 0;
}
