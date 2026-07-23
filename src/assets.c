// zamn_assets — Phase 2 asset pipeline: decode ZAMN's data straight from the
// user's ROM, and prove the decoders match the ROM's own code.
//
// The interesting subcommand is `verify-lzss`. Rather than testing our
// decompressor against hand-picked inputs, it plays an input movie under the
// reference core, intercepts every call the game makes to `$80:CD20`, and runs
// the C port on the exact same bytes — comparing the output *and* the final
// state of the 4 KB sliding window. Whatever the game decompresses is what gets
// tested, and a mismatch is reported against a real call site.
//
// That is the Phase 3 co-simulation pattern in miniature: same core, same
// movie, per-call equality assertion. Only the scope differs.
//
// Usage:
//   zamn_assets verify-lzss <rom.sfc> [-m movie] [-f frames]
//   zamn_assets decompress  <rom.sfc> <bank:addr> <out.bin>
//   zamn_assets gfx         <rom.sfc> <bank:addr> <out.png> [options]
//   zamn_assets palette     <rom.sfc> <bank:addr> <out.png> [-n colors]

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"

#include "analysis/cdl.h"
#include "analysis/movie.h"
#include "assets/gfx.h"
#include "assets/lzss.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Entry point of lzss_decompress, and the RTL that ends it. See
// tools/symbols/zamn.sym and docs/asset-formats.md.
#define LZSS_ENTRY 0x80cd20
#define LZSS_RTL 0x80cdd9

// Nothing the game decompresses comes close to this; it only bounds the buffers.
#define MAX_OUTPUT (256 * 1024)

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return NULL; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0) { fclose(f); fprintf(stderr, "error: '%s' is empty\n", path); return NULL; }
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
    fclose(f); free(buf);
    fprintf(stderr, "error: cannot read '%s'\n", path);
    return NULL;
  }
  fclose(f);
  if (out_len) *out_len = (int)len;
  return buf;
}

// "94:A300", "$94:A300" or "94A300" -> 0x94A300.
static bool parse_addr24(const char* s, uint32_t* out) {
  if (*s == '$') s++;
  char* end = NULL;
  unsigned long bank = strtoul(s, &end, 16);
  if (end == s) return false;
  if (*end == ':') {
    const char* p = end + 1;
    if (*p == '$') p++;
    char* end2 = NULL;
    unsigned long addr = strtoul(p, &end2, 16);
    if (end2 == p || *end2 != '\0' || bank > 0xff || addr > 0xffff) return false;
    *out = (uint32_t)((bank << 16) | addr);
    return true;
  }
  if (*end != '\0' || bank > 0xffffff) return false;
  *out = (uint32_t)bank;
  return true;
}

// A pointer to ROM for a SNES address, plus how many bytes can be read from it
// before the 65816 would wrap to $0000 of the same bank. The game's pointers
// are 16-bit increments inside one bank, so that wrap is the real limit — not
// the end of the ROM file.
static const uint8_t* rom_ptr(const uint8_t* rom, uint32_t rom_size, uint32_t addr24,
                              uint32_t* out_avail) {
  uint32_t off;
  if (!snes_to_rom(addr24, rom_size, &off)) return NULL;
  uint32_t avail = 0x10000u - (addr24 & 0xffff);
  if (avail > rom_size - off) avail = rom_size - off;
  if (out_avail) *out_avail = avail;
  return rom + off;
}

// ---------------------------------------------------------------------------
// verify-lzss: intercept the game's own decompression calls
// ---------------------------------------------------------------------------

typedef struct {
  uint32_t src, dst;    // 24-bit SNES addresses
  uint16_t sp;          // stack pointer at entry, to match the matching RTL
  LzssRing ring_in;     // window contents the ROM call started from
} LzssCall;

static struct {
  const uint8_t* rom;
  uint32_t rom_size;

  bool active;
  LzssCall call;

  int captured, passed, skipped;
  uint8_t rom_out[MAX_OUTPUT];
  uint8_t c_out[MAX_OUTPUT];
  LzssRing ring;
} v;

// WRAM only: everything the game decompresses lands there, and reading it
// directly avoids the side effects snes_read() has on hardware registers.
static bool wram_block(Snes* snes, uint32_t addr24, uint32_t len, uint8_t* out) {
  uint32_t bank = addr24 >> 16, addr = addr24 & 0xffff;
  for (uint32_t i = 0; i < len; i++) {
    uint32_t cur = (bank << 16) | ((addr + i) & 0xffff);  // 16-bit wrap, as [$2C] does
    uint32_t off;
    if (!snes_to_wram(cur, &off)) return false;
    out[i] = snes->ram[off];
  }
  return true;
}

static void lzss_on_entry(Snes* snes) {
  Cpu* cpu = snes->cpu;
  // Arguments: A = source bank, X = destination bank, Y = destination address,
  // and the caller pushed the 16-bit source address before the JSL. At the
  // entry point the stack still holds only the 3-byte return address, so the
  // pushed word sits at S+4.
  uint32_t sp = cpu->sp;
  uint8_t lo, hi;
  uint32_t off;
  if (!snes_to_wram((sp + 4) & 0xffff, &off) ||
      (lo = snes->ram[off], !snes_to_wram((sp + 5) & 0xffff, &off))) {
    // The game's thread stacks all live in low WRAM, so this should not happen.
    printf("--  call at SP=$%04X ignored: its arguments are not in WRAM\n", cpu->sp);
    return;
  }
  hi = snes->ram[off];

  v.call.src = ((uint32_t)(cpu->a & 0xff) << 16) | lo | ((uint32_t)hi << 8);
  v.call.dst = ((uint32_t)(cpu->x & 0xff) << 16) | cpu->y;
  v.call.sp = cpu->sp;
  // Snapshot the window so the C port starts from exactly the same state,
  // including the 17 bytes the ROM's refill never touches.
  memcpy(v.call.ring_in.bytes, &snes->ram[0x6f00], LZSS_RING_SIZE);
  v.active = true;
}

static void lzss_on_exit(Snes* snes) {
  Cpu* cpu = snes->cpu;
  v.active = false;
  v.captured++;

  uint32_t rom_len = cpu->y;  // the routine returns the byte count in Y
  char src_s[16], dst_s[16];
  snprintf(src_s, sizeof src_s, "$%02X:%04X", v.call.src >> 16, v.call.src & 0xffff);
  snprintf(dst_s, sizeof dst_s, "$%02X:%04X", v.call.dst >> 16, v.call.dst & 0xffff);

  if (rom_len > MAX_OUTPUT || !wram_block(snes, v.call.dst, rom_len, v.rom_out)) {
    printf("%-3d %-11s %-11s %8s %8s  SKIP (destination is not WRAM)\n",
           v.captured, src_s, dst_s, "-", "-");
    v.skipped++;
    return;
  }

  uint32_t avail = 0;
  const uint8_t* src = rom_ptr(v.rom, v.rom_size, v.call.src, &avail);
  if (!src) {
    printf("%-3d %-11s %-11s %8s %8s  SKIP (source is not ROM)\n",
           v.captured, src_s, dst_s, "-", "-");
    v.skipped++;
    return;
  }

  v.ring = v.call.ring_in;
  LzssResult r = lzss_decompress(src, avail, v.c_out, MAX_OUTPUT, &v.ring);

  const char* verdict = "OK";
  if (r.status != LZSS_OK) {
    verdict = r.status == LZSS_ERR_TRUNCATED ? "FAIL (truncated)" : "FAIL (overflow)";
  } else if (r.written != rom_len) {
    verdict = "FAIL (length differs)";
  } else if (memcmp(v.c_out, v.rom_out, rom_len) != 0) {
    verdict = "FAIL (output differs)";
  } else if (memcmp(v.ring.bytes, &snes->ram[0x6f00], LZSS_RING_SIZE) != 0) {
    // The window is carried into the next call, so a difference here would
    // corrupt a later decompression even though this one looked right.
    verdict = "FAIL (window differs)";
  } else {
    v.passed++;
  }

  printf("%-3d %-11s %-11s %8u %8u  %s\n", v.captured, src_s, dst_s, r.read, rom_len, verdict);
}

static void verify_step(Snes* snes) {
  Cpu* cpu = snes->cpu;
  // Reset dispatch, WAI/STP and interrupt vectoring run no opcode at PC.
  if (!cpu->resetWanted && !cpu->stopped && !cpu->waiting && !cpu->intWanted) {
    uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
    if (pc == LZSS_ENTRY) {
      lzss_on_entry(snes);
    } else if (pc == LZSS_RTL && v.active && cpu->sp == v.call.sp) {
      // Y and the destination are final by the time the RTL is reached, so
      // read them before it pops the frame.
      lzss_on_exit(snes);
    }
  }
  snes_runCpuCycle(snes);
}

static void verify_frame(Snes* snes) {
  while (snes->inVblank) verify_step(snes);
  uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) verify_step(snes);
  snes_readBBus(snes, 0x40);  // catch the APU up, as snes_runFrame() does
}

static int cmd_verify_lzss(int argc, char** argv) {
  if (argc < 1) {
    fprintf(stderr, "usage: zamn_assets verify-lzss <rom.sfc> [-m movie] [-f frames]\n");
    return 2;
  }
  const char* rom_path = argv[0];
  const char* movie_path = NULL;
  int frames = 2400;

  for (int i = 1; i < argc; i++) {
    bool has_next = i + 1 < argc;
    if ((!strcmp(argv[i], "-m") || !strcmp(argv[i], "--movie")) && has_next) movie_path = argv[++i];
    else if ((!strcmp(argv[i], "-f") || !strcmp(argv[i], "--frames")) && has_next) frames = atoi(argv[++i]);
    else { fprintf(stderr, "error: unknown option '%s'\n", argv[i]); return 2; }
  }

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) return 1;

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    return 1;
  }
  v.rom = snes->cart->rom;
  v.rom_size = snes->cart->romSize;

  Movie movie;
  bool have_movie = false;
  if (movie_path) {
    if (!movie_load(&movie, movie_path)) {
      fprintf(stderr, "error: cannot load movie '%s'\n", movie_path);
      return 1;
    }
    have_movie = true;
  }

  snes_reset(snes, true);

  printf("Verifying the C LZSS port against $80:CD20 over %d frames of '%s'.\n\n",
         frames, movie_path ? movie_path : "(no input)");
  printf("#   source      destination        in      out  result\n");

  for (int frame = 0; frame < frames; frame++) {
    if (have_movie) {
      uint16_t buttons = movie_state(&movie, frame);
      for (int b = 0; b < 12; b++) snes_setButtonState(snes, 1, b, (buttons >> b) & 1);
    }
    verify_frame(snes);
  }

  int failed = v.captured - v.passed - v.skipped;
  printf("\n%d call%s intercepted: %d byte-identical, %d failed, %d skipped.\n",
         v.captured, v.captured == 1 ? "" : "s", v.passed, failed, v.skipped);
  if (v.captured == 0) {
    printf("The movie never reached a decompression call — nothing was verified.\n");
  }

  if (have_movie) movie_free(&movie);
  snes_free(snes);
  free(rom);
  return (failed > 0 || v.captured == 0) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// decompress
// ---------------------------------------------------------------------------

static uint8_t* decompress_at(const uint8_t* rom, uint32_t rom_size, uint32_t addr24,
                              uint32_t* out_len, uint32_t* out_read) {
  uint32_t avail = 0;
  const uint8_t* src = rom_ptr(rom, rom_size, addr24, &avail);
  if (!src) {
    fprintf(stderr, "error: $%02X:%04X is not a cartridge ROM address\n",
            addr24 >> 16, addr24 & 0xffff);
    return NULL;
  }
  uint8_t* out = (uint8_t*)malloc(MAX_OUTPUT);
  if (!out) return NULL;

  LzssRing ring;
  lzss_ring_init(&ring);
  LzssResult r = lzss_decompress(src, avail, out, MAX_OUTPUT, &ring);
  if (r.status != LZSS_OK) {
    fprintf(stderr, "error: decompression failed at $%02X:%04X (%s)\n",
            addr24 >> 16, addr24 & 0xffff,
            r.status == LZSS_ERR_TRUNCATED ? "stream runs past the end of the bank"
                                           : "output too large");
    free(out);
    return NULL;
  }
  if (out_len) *out_len = r.written;
  if (out_read) *out_read = r.read;
  return out;
}

static int cmd_decompress(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: zamn_assets decompress <rom.sfc> <bank:addr> <out.bin>\n");
    return 2;
  }
  uint32_t addr;
  if (!parse_addr24(argv[1], &addr)) {
    fprintf(stderr, "error: cannot parse address '%s' (expected e.g. $94:A300)\n", argv[1]);
    return 2;
  }
  int rom_len = 0;
  uint8_t* rom = read_file(argv[0], &rom_len);
  if (!rom) return 1;

  uint32_t len = 0, consumed = 0;
  uint8_t* out = decompress_at(rom, (uint32_t)rom_len, addr, &len, &consumed);
  if (!out) { free(rom); return 1; }

  FILE* f = fopen(argv[2], "wb");
  if (!f) {
    fprintf(stderr, "error: cannot write '%s'\n", argv[2]);
    free(out); free(rom);
    return 1;
  }
  fwrite(out, 1, len, f);
  fclose(f);
  printf("$%02X:%04X  %u compressed bytes -> %u bytes  (%.1fx)  written to %s\n",
         addr >> 16, addr & 0xffff, consumed, len,
         consumed ? (double)len / consumed : 0.0, argv[2]);
  free(out);
  free(rom);
  return 0;
}

// ---------------------------------------------------------------------------
// gfx / palette
// ---------------------------------------------------------------------------

// Lay decoded tiles out as a sheet, `cols` tiles wide, and colour them.
// `scale` nearest-neighbours the result, because 8x8 tiles are unreadable at 1x.
static bool write_tilesheet(const char* path, const uint8_t* indices, uint32_t tiles,
                            int cols, const uint8_t* pal_rgb, int pal_colors, int scale) {
  if (tiles == 0) { fprintf(stderr, "error: no tiles to write\n"); return false; }
  int rows = (int)((tiles + cols - 1) / cols);
  int w = cols * GFX_TILE_W * scale, h = rows * GFX_TILE_H * scale;
  uint8_t* img = (uint8_t*)calloc((size_t)w * h, 3);
  if (!img) return false;

  for (uint32_t t = 0; t < tiles; t++) {
    int tx = (int)(t % cols) * GFX_TILE_W;
    int ty = (int)(t / cols) * GFX_TILE_H;
    for (int y = 0; y < GFX_TILE_H; y++) {
      for (int x = 0; x < GFX_TILE_W; x++) {
        uint8_t idx = indices[t * GFX_TILE_PIXELS + y * GFX_TILE_W + x];
        if (idx >= pal_colors) continue;
        for (int sy = 0; sy < scale; sy++) {
          for (int sx = 0; sx < scale; sx++) {
            uint8_t* p = img + (((size_t)(ty + y) * scale + sy) * w
                                + (size_t)(tx + x) * scale + sx) * 3;
            p[0] = pal_rgb[idx * 3 + 0];
            p[1] = pal_rgb[idx * 3 + 1];
            p[2] = pal_rgb[idx * 3 + 2];
          }
        }
      }
    }
  }
  bool ok = stbi_write_png(path, w, h, 3, img, w * 3) != 0;
  if (!ok) fprintf(stderr, "error: cannot write '%s'\n", path);
  else printf("%u tiles -> %dx%d PNG, %d per row, written to %s\n", tiles, w, h, cols, path);
  free(img);
  return ok;
}

static int cmd_gfx(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr,
            "usage: zamn_assets gfx <rom.sfc> <bank:addr> <out.png> [options]\n"
            "   --bpp <2|4|8>     bit depth (default 4)\n"
            "   --lzss            the source is a compressed stream\n"
            "   --bytes <n>       raw bytes to decode (default: all of an LZSS\n"
            "                     stream, or 8 KB of uncompressed data)\n"
            "   --tiles <n>       stop after n tiles\n"
            "   --cols <n>        tiles per row in the sheet (default 16)\n"
            "   --pal <bank:addr> palette source in ROM (BGR555)\n"
            "   --pal-index <n>   which sub-palette of 2^bpp colours to use\n"
            "   --scale <n>       magnify the PNG n times (default 1)\n");
    return 2;
  }
  const char* rom_path = argv[0];
  const char* out_path = argv[2];
  uint32_t addr;
  if (!parse_addr24(argv[1], &addr)) {
    fprintf(stderr, "error: cannot parse address '%s'\n", argv[1]);
    return 2;
  }

  int bpp = 4, cols = 16, pal_index = 0, scale = 1;
  uint32_t want_bytes = 0, want_tiles = 0;
  bool compressed = false, have_pal = false;
  uint32_t pal_addr = 0;

  for (int i = 3; i < argc; i++) {
    bool has_next = i + 1 < argc;
    if (!strcmp(argv[i], "--bpp") && has_next) bpp = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--lzss")) compressed = true;
    else if (!strcmp(argv[i], "--bytes") && has_next) want_bytes = (uint32_t)strtoul(argv[++i], NULL, 0);
    else if (!strcmp(argv[i], "--tiles") && has_next) want_tiles = (uint32_t)strtoul(argv[++i], NULL, 0);
    else if (!strcmp(argv[i], "--cols") && has_next) cols = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--scale") && has_next) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--pal") && has_next) {
      if (!parse_addr24(argv[++i], &pal_addr)) {
        fprintf(stderr, "error: cannot parse palette address '%s'\n", argv[i]);
        return 2;
      }
      have_pal = true;
    } else if (!strcmp(argv[i], "--pal-index") && has_next) pal_index = atoi(argv[++i]);
    else { fprintf(stderr, "error: unknown option '%s'\n", argv[i]); return 2; }
  }
  if (gfx_tile_bytes(bpp) == 0) {
    fprintf(stderr, "error: --bpp must be 2, 4 or 8\n");
    return 2;
  }
  if (cols < 1) cols = 16;
  if (scale < 1) scale = 1;

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) return 1;

  const uint8_t* data = NULL;
  uint8_t* owned = NULL;
  uint32_t data_len = 0;

  if (compressed) {
    owned = decompress_at(rom, (uint32_t)rom_len, addr, &data_len, NULL);
    if (!owned) { free(rom); return 1; }
    data = owned;
  } else {
    uint32_t avail = 0;
    data = rom_ptr(rom, (uint32_t)rom_len, addr, &avail);
    if (!data) {
      fprintf(stderr, "error: $%02X:%04X is not a cartridge ROM address\n",
              addr >> 16, addr & 0xffff);
      free(rom);
      return 1;
    }
    data_len = want_bytes ? want_bytes : 8192;
    if (data_len > avail) data_len = avail;
  }
  if (want_bytes && want_bytes < data_len) data_len = want_bytes;

  uint32_t tile_stride = gfx_tile_bytes(bpp);
  uint32_t tiles = data_len / tile_stride;
  if (want_tiles && want_tiles < tiles) tiles = want_tiles;

  uint8_t* indices = (uint8_t*)malloc((size_t)tiles * GFX_TILE_PIXELS + 1);
  if (!indices) { free(owned); free(rom); return 1; }
  tiles = gfx_decode_tiles(data, data_len, bpp, indices, tiles);

  // Colours: the requested sub-palette from the ROM, or a grey ramp so the
  // tile shapes are still readable when the right palette is not known yet.
  int pal_colors = 1 << bpp;
  uint8_t pal_rgb[256 * 3];
  if (have_pal) {
    uint32_t pal_off = (uint32_t)(pal_index * pal_colors * 2);
    uint32_t avail = 0;
    const uint8_t* pal_src = rom_ptr(rom, (uint32_t)rom_len,
                                     (pal_addr & 0xff0000) | ((pal_addr + pal_off) & 0xffff), &avail);
    if (!pal_src || avail < (uint32_t)pal_colors * 2) {
      fprintf(stderr, "error: palette at $%06X does not hold %d colours\n", pal_addr, pal_colors);
      free(indices); free(owned); free(rom);
      return 1;
    }
    gfx_decode_palette(pal_src, (uint32_t)pal_colors, pal_rgb);
  } else {
    for (int i = 0; i < pal_colors; i++) {
      uint8_t g = (uint8_t)(i * 255 / (pal_colors - 1));
      pal_rgb[i * 3 + 0] = pal_rgb[i * 3 + 1] = pal_rgb[i * 3 + 2] = g;
    }
  }

  printf("$%02X:%04X  %s  %u bytes  %dbpp  ", addr >> 16, addr & 0xffff,
         compressed ? "LZSS" : "raw", data_len, bpp);
  bool ok = write_tilesheet(out_path, indices, tiles, cols, pal_rgb, pal_colors, scale);

  free(indices);
  free(owned);
  free(rom);
  return ok ? 0 : 1;
}

static int cmd_palette(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: zamn_assets palette <rom.sfc> <bank:addr> <out.png> [-n colors]\n");
    return 2;
  }
  uint32_t addr;
  if (!parse_addr24(argv[1], &addr)) {
    fprintf(stderr, "error: cannot parse address '%s'\n", argv[1]);
    return 2;
  }
  int n = 256;
  for (int i = 3; i < argc; i++) {
    if ((!strcmp(argv[i], "-n") || !strcmp(argv[i], "--colors")) && i + 1 < argc) n = atoi(argv[++i]);
    else { fprintf(stderr, "error: unknown option '%s'\n", argv[i]); return 2; }
  }
  if (n < 1 || n > 256) { fprintf(stderr, "error: -n must be 1..256\n"); return 2; }

  int rom_len = 0;
  uint8_t* rom = read_file(argv[0], &rom_len);
  if (!rom) return 1;
  uint32_t avail = 0;
  const uint8_t* src = rom_ptr(rom, (uint32_t)rom_len, addr, &avail);
  if (!src || avail < (uint32_t)n * 2) {
    fprintf(stderr, "error: $%02X:%04X does not hold %d colours\n", addr >> 16, addr & 0xffff, n);
    free(rom);
    return 1;
  }

  uint8_t pal[256 * 3];
  gfx_decode_palette(src, (uint32_t)n, pal);

  // 16 swatches per row, 16 px each, so a 256-colour CGRAM dump reads as the
  // 16 sub-palettes the PPU actually indexes.
  const int sw = 16, cols = 16;
  int rows = (n + cols - 1) / cols;
  int w = cols * sw, h = rows * sw;
  uint8_t* img = (uint8_t*)calloc((size_t)w * h, 3);
  if (!img) { free(rom); return 1; }
  for (int i = 0; i < n; i++) {
    int x0 = (i % cols) * sw, y0 = (i / cols) * sw;
    for (int y = 0; y < sw; y++) {
      for (int x = 0; x < sw; x++) {
        uint8_t* p = img + (((size_t)(y0 + y) * w) + x0 + x) * 3;
        p[0] = pal[i * 3 + 0]; p[1] = pal[i * 3 + 1]; p[2] = pal[i * 3 + 2];
      }
    }
  }
  bool ok = stbi_write_png(argv[2], w, h, 3, img, w * 3) != 0;
  if (ok) printf("$%02X:%04X  %d colours -> %s\n", addr >> 16, addr & 0xffff, n, argv[2]);
  else fprintf(stderr, "error: cannot write '%s'\n", argv[2]);

  free(img);
  free(rom);
  return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------

static void usage(void) {
  fprintf(stderr,
          "zamn_assets — decode ZAMN data from the ROM (Phase 2)\n\n"
          "  zamn_assets verify-lzss <rom.sfc> [-m movie] [-f frames]\n"
          "      Replay a movie and compare the C decompressor against $80:CD20\n"
          "      on every call the game makes.\n\n"
          "  zamn_assets decompress <rom.sfc> <bank:addr> <out.bin>\n"
          "      Decompress one LZSS stream.\n\n"
          "  zamn_assets gfx <rom.sfc> <bank:addr> <out.png> [options]\n"
          "      Decode planar tiles to a PNG sheet ('gfx' with no options for help).\n\n"
          "  zamn_assets palette <rom.sfc> <bank:addr> <out.png> [-n colors]\n"
          "      Decode a BGR555 palette to a swatch PNG.\n");
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  const char* cmd = argv[1];
  if (!strcmp(cmd, "verify-lzss")) return cmd_verify_lzss(argc - 2, argv + 2);
  if (!strcmp(cmd, "decompress")) return cmd_decompress(argc - 2, argv + 2);
  if (!strcmp(cmd, "gfx")) return cmd_gfx(argc - 2, argv + 2);
  if (!strcmp(cmd, "palette")) return cmd_palette(argc - 2, argv + 2);
  if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) { usage(); return 0; }
  fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
  usage();
  return 2;
}
