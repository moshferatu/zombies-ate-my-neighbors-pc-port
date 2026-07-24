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
// `verify-level` is the same idea one level up: it lets the game load a level,
// then diffs the entire tilemap our decoder builds against the one the ROM
// just expanded into WRAM.
//
// Usage:
//   zamn_assets verify-lzss  <rom.sfc> [-m movie] [-f frames]
//   zamn_assets verify-level <rom.sfc> [-m movie] [-f frames]
//   zamn_assets level        <rom.sfc> <level> [out.png]
//   zamn_assets decompress   <rom.sfc> <bank:addr> <out.bin>
//   zamn_assets gfx          <rom.sfc> <bank:addr> <out.png> [options]
//   zamn_assets palette      <rom.sfc> <bank:addr> <out.png> [-n colors]

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"

#include "analysis/cdl.h"
#include "analysis/movie.h"
#include "assets/actor.h"
#include "assets/gfx.h"
#include "assets/level.h"
#include "assets/lzss.h"
#include "assets/music.h"
#include "assets/rom.h"
#include "assets/sprite.h"

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

// Address mapping is `src/assets/rom.c` — port code, not tooling. This just
// saves passing the two halves of a `Rom` around everywhere.
static const uint8_t* rom_at(const uint8_t* rom, uint32_t rom_size, uint32_t addr24,
                             uint32_t* out_avail) {
  Rom r = {rom, rom_size};
  return rom_ptr(&r, addr24, out_avail);
}

// "$83:EF8C" for messages.
static const char* addr_str(uint32_t addr24) {
  static char buf[4][16];
  static int slot = 0;
  slot = (slot + 1) & 3;
  snprintf(buf[slot], sizeof buf[slot], "$%02X:%04X", addr24 >> 16, addr24 & 0xffff);
  return buf[slot];
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
  const uint8_t* src = rom_at(v.rom, v.rom_size, v.call.src, &avail);
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
  const uint8_t* src = rom_at(rom, rom_size, addr24, &avail);
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
    data = rom_at(rom, (uint32_t)rom_len, addr, &avail);
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
    const uint8_t* pal_src = rom_at(rom, (uint32_t)rom_len,
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
  const uint8_t* src = rom_at(rom, (uint32_t)rom_len, addr, &avail);
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
// level: header report, and the expanded tilemap as a picture
// ---------------------------------------------------------------------------

// `level` is 0 when the caller does not know which one this is (verify-level
// finds the record before it finds its table entry).
static void print_header(const LevelHeader* h, int level) {
  if (level) printf("Level %d — record at %s\n\n", level, addr_str(h->record_addr));
  else printf("Level record at %s\n\n", addr_str(h->record_addr));
  printf("  blocks           %u x %u  (%u x %u tiles, %u x %u pixels)\n",
         h->cols, h->rows, level_tile_cols(h), level_tile_rows(h),
         level_width_px(h), level_height_px(h));
  printf("  block library    %s   (LZSS)\n", addr_str(h->block_defs));
  printf("  block map        %s   %u bytes\n", addr_str(h->block_map),
         (uint32_t)h->cols * h->rows * 2);
  printf("  tile attributes  %s   %d bytes\n", addr_str(h->tile_attrs),
         LEVEL_TILE_ATTR_BYTES);
  printf("  BG characters    %s   %d bytes, 4bpp\n", addr_str(h->bg_tiles),
         LEVEL_BG_TILES_BYTES);
  printf("  BG palette       %s\n", addr_str(h->bg_palette));
  printf("  sprite palette   %s\n", addr_str(h->sprite_palette));
  printf("  priority below   $%04X\n", h->priority_below);
  printf("  start positions  (%u, %u) and (%u, %u)\n",
         h->start_x1, h->start_y1, h->start_x2, h->start_y2);
  printf("  max scroll       x $%04X  y $%04X\n",
         level_max_scroll_x(h), level_max_scroll_y(h));
  printf("  music            song %u, sample set %u (APU data sets %u and %u)\n",
         h->song, h->sample_set, h->song, MUSIC_SET_SAMPLES + h->sample_set);
  printf("  unidentified     +$18 $%04X  +$1A $%04X  +$1C $%04X  +$1E $%04X\n"
         "                   +$20 $%04X  +$28 $%04X\n",
         h->unknown_18, h->unknown_1a, h->list_1c, h->list_1e,
         h->list_20, h->unknown_28);
}

// Draw the expanded map with its own characters and palette. Mode 1 BG1 is
// 4bpp, and the 16 KB of characters is exactly 512 tiles — which is also how
// many entries the tile attribute table has, so a map entry's tile index is
// 9 bits even though the hardware field is 10.
static bool render_level(const LevelHeader* h, const uint16_t* map, const Rom* rom,
                         const char* path) {
  uint32_t avail = 0;
  const uint8_t* chars = rom_ptr(rom, h->bg_tiles, &avail);
  if (!chars || avail < LEVEL_BG_TILES_BYTES) {
    fprintf(stderr, "error: BG characters at %s are not %d readable bytes\n",
            addr_str(h->bg_tiles), LEVEL_BG_TILES_BYTES);
    return false;
  }
  const uint8_t* pal_src = rom_ptr(rom, h->bg_palette, &avail);
  if (!pal_src || avail < LEVEL_PALETTE_BYTES) {
    fprintf(stderr, "error: BG palette at %s is not %d readable bytes\n",
            addr_str(h->bg_palette), LEVEL_PALETTE_BYTES);
    return false;
  }
  uint8_t pal[(LEVEL_PALETTE_BYTES / 2) * 3];
  gfx_decode_palette(pal_src, LEVEL_PALETTE_BYTES / 2, pal);

  uint32_t tile_cols = level_tile_cols(h), tile_rows = level_tile_rows(h);
  uint32_t w = tile_cols * GFX_TILE_W, hgt = tile_rows * GFX_TILE_H;
  uint8_t* img = (uint8_t*)malloc((size_t)w * hgt * 3);
  if (!img) return false;

  uint8_t tile[GFX_TILE_PIXELS];
  for (uint32_t ty = 0; ty < tile_rows; ty++) {
    for (uint32_t tx = 0; tx < tile_cols; tx++) {
      uint16_t e = map[ty * tile_cols + tx];
      uint32_t index = e & 0x3ff;
      uint32_t sub = (e >> 10) & 7;
      bool flip_x = (e & 0x4000) != 0, flip_y = (e & 0x8000) != 0;

      if (index < LEVEL_BG_TILES) {
        gfx_decode_tile(chars + index * gfx_tile_bytes(4), 4, tile);
      } else {
        memset(tile, 0, sizeof tile);  // no characters exist above 511
      }
      for (uint32_t y = 0; y < GFX_TILE_H; y++) {
        for (uint32_t x = 0; x < GFX_TILE_W; x++) {
          uint32_t sx = flip_x ? GFX_TILE_W - 1 - x : x;
          uint32_t sy = flip_y ? GFX_TILE_H - 1 - y : y;
          // Colour 0 of every sub-palette is transparent on the SNES and shows
          // the backdrop; drawing it as CGRAM entry 0 matches what you see.
          uint8_t idx = tile[sy * GFX_TILE_W + sx];
          uint32_t c = idx ? sub * 16 + idx : 0;
          uint8_t* p = img + (((size_t)(ty * GFX_TILE_H + y)) * w
                              + tx * GFX_TILE_W + x) * 3;
          p[0] = pal[c * 3 + 0];
          p[1] = pal[c * 3 + 1];
          p[2] = pal[c * 3 + 2];
        }
      }
    }
  }

  bool ok = stbi_write_png(path, (int)w, (int)hgt, 3, img, (int)w * 3) != 0;
  if (ok) printf("\n  %u x %u PNG written to %s\n", w, hgt, path);
  else fprintf(stderr, "error: cannot write '%s'\n", path);
  free(img);
  return ok;
}

static int cmd_level(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: zamn_assets level <rom.sfc> <level 1-%d> [out.png]\n",
            LEVEL_COUNT);
    return 2;
  }
  int level = atoi(argv[1]);
  const char* out_path = argc > 2 ? argv[2] : NULL;

  int rom_len = 0;
  uint8_t* rom_data = read_file(argv[0], &rom_len);
  if (!rom_data) return 1;
  Rom rom = {rom_data, (uint32_t)rom_len};

  uint32_t rec = 0;
  if (!level_record_addr(&rom, level, &rec)) {
    fprintf(stderr, "error: level must be %d..%d\n", LEVEL_FIRST, LEVEL_COUNT);
    free(rom_data);
    return 2;
  }
  LevelHeader h;
  if (level_header_read(&rom, rec, &h) != LEVEL_OK) {
    fprintf(stderr, "error: the record at %s does not parse as a level\n", addr_str(rec));
    free(rom_data);
    return 1;
  }
  print_header(&h, level);

  uint8_t* blocks = (uint8_t*)malloc(LEVEL_BLOCK_LIB_BYTES);
  uint16_t* map = (uint16_t*)malloc((size_t)level_map_entries(&h) * 2);
  if (!blocks || !map) { free(blocks); free(map); free(rom_data); return 1; }

  LzssRing ring;
  lzss_ring_init(&ring);
  uint32_t block_bytes = 0;
  int rc = level_load_blocks(&rom, &h, blocks, &block_bytes, &ring);
  if (rc == LEVEL_OK) {
    printf("  block library    %u bytes = %u blocks\n", block_bytes,
           block_bytes / LEVEL_BLOCK_BYTES);
    rc = level_expand(&h, blocks, block_bytes, &rom, map, level_map_entries(&h));
  }
  if (rc != LEVEL_OK) {
    fprintf(stderr, "error: cannot build the map (status %d)\n", rc);
    free(blocks); free(map); free(rom_data);
    return 1;
  }

  bool ok = true;
  if (out_path) ok = render_level(&h, map, &rom, out_path);

  free(blocks);
  free(map);
  free(rom_data);
  return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// verify-level: diff the expanded map against the one the ROM builds
// ---------------------------------------------------------------------------

// `$80:86A2` is the level loader. These are the three points worth stopping at:
// where the record offset is in X, where the map and the tile attributes are
// finished, and where the palettes are.
#define LEVEL_LOAD_ARGS 0x8086b0  // LDA $9F0004,X — X = record offset in bank $9F
#define LEVEL_LOAD_MAP 0x8086df   // after $80:ACA2, $80:AD2B and $80:AD92
#define LEVEL_LOAD_PAL 0x80875b   // after $80:A037 and $80:A05B

// Where the ROM keeps what it builds. From docs/wram-map.md.
#define WRAM_BLOCK_ROW_TABLE 0x4228  // $A6 + row * cols*2, one per block row
#define WRAM_TILE_ROW_TABLE 0x4328   // row * cols*16, one per tile row
#define WRAM_BLOCK_LIB 0x8000        // bank $7E: the decompressed block library
#define WRAM_TILE_ATTRS 0x611a
#define WRAM_BG_PALETTE 0x5428
#define WRAM_SPRITE_PALETTE 0x5528
#define WRAM_BG_PALETTE_COPY 0x5628
#define WRAM_MAP_BANK 0x10000  // bank $7F holds the expanded map from $0000

static struct {
  Rom rom;
  bool have_args, done;
  uint32_t record;
  int checks, failures;
} lv;

static uint16_t wram_word(Snes* snes, uint32_t off) {
  return (uint16_t)(snes->ram[off] | ((uint16_t)snes->ram[off + 1] << 8));
}

static void check(const char* what, bool ok, const char* detail) {
  lv.checks++;
  if (!ok) lv.failures++;
  printf("  %-34s %s%s%s\n", what, ok ? "OK" : "FAIL",
         detail && *detail ? "  " : "", detail ? detail : "");
}

// Compare `len` bytes of WRAM against `want`, naming the first difference.
static void check_bytes(const char* what, Snes* snes, uint32_t wram_off,
                        const uint8_t* want, uint32_t len) {
  char detail[128] = "";
  bool ok = true;
  for (uint32_t i = 0; i < len; i++) {
    if (snes->ram[wram_off + i] != want[i]) {
      snprintf(detail, sizeof detail, "first difference at +%u: ROM $%02X, ours $%02X",
               i, snes->ram[wram_off + i], want[i]);
      ok = false;
      break;
    }
  }
  char label[96];
  snprintf(label, sizeof label, "%s (%u bytes)", what, len);
  check(label, ok, detail);
}

static void check_word(const char* what, Snes* snes, uint32_t wram_off, uint16_t want) {
  uint16_t got = wram_word(snes, wram_off);
  char detail[64] = "";
  if (got != want) snprintf(detail, sizeof detail, "ROM $%04X, ours $%04X", got, want);
  check(what, got == want, detail);
}

static void level_on_map(Snes* snes) {
  LevelHeader h;
  if (level_header_read(&lv.rom, lv.record, &h) != LEVEL_OK) {
    printf("error: the record the game used (%s) does not parse\n", addr_str(lv.record));
    lv.failures++;
    return;
  }
  print_header(&h, 0);
  printf("\nAgainst the WRAM the ROM just built:\n\n");

  // The scalars $80:ACA2 derives from the block dimensions.
  check_word("$AE  block map row stride", snes, 0xae, (uint16_t)(h.cols * 2));
  check_word("$B0  block rows", snes, 0xb0, h.rows);
  check_word("$B2  tilemap row stride", snes, 0xb2, level_row_stride_bytes(&h));
  check_word("$B4  tile rows", snes, 0xb4, (uint16_t)level_tile_rows(&h));
  check_word("$B6  max scroll y", snes, 0xb6, level_max_scroll_y(&h));
  check_word("$B8  max scroll x", snes, 0xb8, level_max_scroll_x(&h));

  // The two row-base tables the whole engine indexes through.
  bool ok = true;
  for (uint32_t i = 0; i < h.rows && ok; i++) {
    ok = wram_word(snes, WRAM_BLOCK_ROW_TABLE + i * 2)
         == (uint16_t)((h.block_map & 0xffff) + i * h.cols * 2);
  }
  check("$7E:4228 block row table", ok, "");
  ok = true;
  for (uint32_t i = 0; i < level_tile_rows(&h) && ok; i++) {
    ok = wram_word(snes, WRAM_TILE_ROW_TABLE + i * 2)
         == (uint16_t)(i * level_row_stride_bytes(&h));
  }
  check("$7E:4328 tile row table", ok, "");

  // The block library, exactly as the game left it in bank $7E.
  uint8_t* blocks = (uint8_t*)malloc(LEVEL_BLOCK_LIB_BYTES);
  uint16_t* map = (uint16_t*)malloc((size_t)level_map_entries(&h) * 2);
  if (!blocks || !map) { free(blocks); free(map); lv.failures++; return; }

  LzssRing ring;
  lzss_ring_init(&ring);
  uint32_t block_bytes = 0;
  if (level_load_blocks(&lv.rom, &h, blocks, &block_bytes, &ring) != LEVEL_OK) {
    check("block library decompresses", false, "");
  } else {
    check_bytes("$7E:8000 block library", snes, WRAM_BLOCK_LIB, blocks, block_bytes);
  }

  // The whole expanded tilemap — the point of the exercise.
  if (level_expand(&h, blocks, block_bytes, &lv.rom, map, level_map_entries(&h))
      != LEVEL_OK) {
    check("map expands", false, "");
  } else {
    uint32_t entries = level_map_entries(&h);
    char detail[128] = "";
    bool same = true;
    for (uint32_t i = 0; i < entries; i++) {
      uint16_t got = wram_word(snes, WRAM_MAP_BANK + i * 2);
      if (got != map[i]) {
        snprintf(detail, sizeof detail,
                 "first difference at tile (%u, %u): ROM $%04X, ours $%04X",
                 i % level_tile_cols(&h), i / level_tile_cols(&h), got, map[i]);
        same = false;
        break;
      }
    }
    char label[96];
    snprintf(label, sizeof label, "$7F:0000 expanded map (%u tiles)", entries);
    check(label, same, detail);
  }

  // The tile attribute table is a straight 1 KB copy, so this checks the
  // pointer and the length rather than a decode.
  uint32_t avail = 0;
  const uint8_t* attrs = rom_ptr(&lv.rom, h.tile_attrs, &avail);
  if (attrs && avail >= LEVEL_TILE_ATTR_BYTES) {
    check_bytes("$7E:611A tile attributes", snes, WRAM_TILE_ATTRS, attrs,
                LEVEL_TILE_ATTR_BYTES);
  } else {
    check("$7E:611A tile attributes", false, "not readable from ROM");
  }

  free(blocks);
  free(map);
}

static void level_on_palettes(Snes* snes) {
  LevelHeader h;
  if (level_header_read(&lv.rom, lv.record, &h) != LEVEL_OK) return;

  // $80:86F9 sets this one, which is past the first stop.
  check_word("$DC  priority threshold", snes, 0xdc, h.priority_below);

  uint32_t avail = 0;
  const uint8_t* bg = rom_ptr(&lv.rom, h.bg_palette, &avail);
  if (bg && avail >= LEVEL_PALETTE_BYTES) {
    check_bytes("$7E:5428 BG palette", snes, WRAM_BG_PALETTE, bg, LEVEL_PALETTE_BYTES);
    check_bytes("$7E:5628 BG palette copy", snes, WRAM_BG_PALETTE_COPY, bg,
                LEVEL_PALETTE_BYTES);
  }
  const uint8_t* sp = rom_ptr(&lv.rom, h.sprite_palette, &avail);
  if (sp && avail >= LEVEL_PALETTE_BYTES) {
    check_bytes("$7E:5528 sprite palette", snes, WRAM_SPRITE_PALETTE, sp,
                LEVEL_PALETTE_BYTES);
  }
  lv.done = true;
}

static void level_step(Snes* snes) {
  Cpu* cpu = snes->cpu;
  if (!lv.done && !cpu->resetWanted && !cpu->stopped && !cpu->waiting && !cpu->intWanted) {
    uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
    if (pc == LEVEL_LOAD_ARGS && !lv.have_args) {
      // `LDX $10` on the previous instruction put the record's offset within
      // bank $9F in X; the game addresses the record as $9F:0000 + X.
      lv.record = 0x9f0000u | (cpu->x & 0xffff);
      lv.have_args = true;
    } else if (pc == LEVEL_LOAD_MAP && lv.have_args) {
      level_on_map(snes);
    } else if (pc == LEVEL_LOAD_PAL && lv.have_args) {
      level_on_palettes(snes);
    }
  }
  snes_runCpuCycle(snes);
}

static void level_frame(Snes* snes) {
  while (snes->inVblank) level_step(snes);
  uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) level_step(snes);
  snes_readBBus(snes, 0x40);
}

static int cmd_verify_level(int argc, char** argv) {
  if (argc < 1) {
    fprintf(stderr, "usage: zamn_assets verify-level <rom.sfc> [-m movie] [-f frames]\n");
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
  uint8_t* rom_data = read_file(rom_path, &rom_len);
  if (!rom_data) return 1;

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom_data, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    return 1;
  }
  lv.rom.data = snes->cart->rom;
  lv.rom.size = snes->cart->romSize;

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
  printf("Replaying %d frames of '%s' and diffing the level the game loads.\n\n",
         frames, movie_path ? movie_path : "(no input)");

  for (int frame = 0; frame < frames && !lv.done; frame++) {
    if (have_movie) {
      uint16_t buttons = movie_state(&movie, frame);
      for (int b = 0; b < 12; b++) snes_setButtonState(snes, 1, b, (buttons >> b) & 1);
    }
    level_frame(snes);
  }

  int rc;
  if (!lv.have_args) {
    printf("The movie never reached a level load — nothing was verified.\n");
    rc = 1;
  } else {
    // Which entry of the table at $9F:8000 the game picked. Knowing this is
    // what lets the port go from a level number to a record on its own.
    int level = 0;
    for (int i = LEVEL_FIRST; i < LEVEL_FIRST + LEVEL_COUNT; i++) {
      uint32_t addr;
      if (level_record_addr(&lv.rom, i, &addr) && addr == lv.record) { level = i; break; }
    }
    printf("\nThe game loaded the record at %s", addr_str(lv.record));
    if (level) printf(" — entry %d of the table at $9F:8000", level);
    else printf(" — which is NOT in the table at $9F:8000");
    printf(".\n%d check%s, %d failed.\n", lv.checks, lv.checks == 1 ? "" : "s", lv.failures);
    rc = (lv.failures > 0 || !level) ? 1 : 0;
  }

  if (have_movie) movie_free(&movie);
  snes_free(snes);
  free(rom_data);
  return rc;
}

// ---------------------------------------------------------------------------
// actors: report a level's placement lists
// ---------------------------------------------------------------------------

static void print_actor_lists(const ActorLists* al) {
  printf("\n  actors (%d)   id  x     y     flags  behavior\n", al->actor_count);
  for (int i = 0; i < al->actor_count; i++) {
    const ActorPlacement* a = &al->actors[i];
    printf("    %2d        $%02X  %-5u %-5u $%02X    %s\n",
           i, a->id, a->x, a->y, a->flags, addr_str(a->behavior));
  }
  printf("\n  victims (%d)  idx  x     y     behavior\n", al->victim_count);
  for (int i = 0; i < al->victim_count; i++) {
    const VictimPlacement* vv = &al->victims[i];
    printf("    %2d        %3u  %-5u %-5u %s\n", i, vv->index, vv->x, vv->y,
           addr_str(vv->behavior));
  }
  printf("\n  objects (%d)  type  x     y\n", al->object_count);
  for (int i = 0; i < al->object_count; i++) {
    const ObjectPlacement* o = &al->objects[i];
    printf("    %2d        $%02X   %-5u %-5u\n", i, o->type, o->x, o->y);
  }
}

static int cmd_actors(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: zamn_assets actors <rom.sfc> <level 1-%d>\n", LEVEL_COUNT);
    return 2;
  }
  int level = atoi(argv[1]);
  int rom_len = 0;
  uint8_t* rom_data = read_file(argv[0], &rom_len);
  if (!rom_data) return 1;
  Rom rom = {rom_data, (uint32_t)rom_len};

  uint32_t rec = 0;
  if (!level_record_addr(&rom, level, &rec)) {
    fprintf(stderr, "error: level must be %d..%d\n", LEVEL_FIRST, LEVEL_COUNT);
    free(rom_data);
    return 2;
  }
  LevelHeader h;
  if (level_header_read(&rom, rec, &h) != LEVEL_OK) {
    fprintf(stderr, "error: the record at %s does not parse as a level\n", addr_str(rec));
    free(rom_data);
    return 1;
  }
  ActorLists al;
  int rc = actors_read(&rom, &h, &al);
  if (rc != ACTOR_OK) {
    fprintf(stderr, "error: cannot read the placement lists (status %d)\n", rc);
    free(rom_data);
    return 1;
  }
  printf("Level %d — record at %s\n", level, addr_str(rec));
  printf("  actor list   %s\n  victim list  %s\n  object list  %s\n",
         addr_str(0x9f0000u | h.list_1c), addr_str(0x9f0000u | h.list_1e),
         addr_str(0x9f0000u | h.list_20));
  print_actor_lists(&al);

  free(rom_data);
  return 0;
}

// ---------------------------------------------------------------------------
// verify-actors: diff the victim and object lists against the ROM's parsers
// ---------------------------------------------------------------------------

// The two placement parsers build flat working arrays in WRAM. Stopping right
// as each finishes lets us diff the whole array the ROM extracted against the
// one actors_read() decodes. `$81:80EC` (actors) spreads its work across frames
// and is left to Phase 3, so only victims and objects are checked here.
#define VICTIMS_PARSE_DONE 0x82db8b  // loop exit of $82:DB46
#define OBJECTS_PARSE_DONE 0x80c9d6  // just after $80:C9A5 writes the $C000 sentinel

// Where each parser leaves its results (bank $7E, offsets from $0000).
#define WRAM_VICTIM_COUNT 0x6e30  // $82:DB46: how many victims it kept
#define WRAM_VICTIM_X 0x6df4      // stride 4
#define WRAM_VICTIM_Y 0x6df6      // stride 4
#define WRAM_VICTIM_GATE 0x1d50   // $82:DB46 stops once an index exceeds this
#define WRAM_OBJECT_X 0x6d02      // stride 2
#define WRAM_OBJECT_Y 0x6d48      // stride 2
#define WRAM_OBJECT_TYPE 0x1f0a   // stride 2, low byte
#define WRAM_OBJECT_END 0x1ec4    // stride 2; $C000 marks the end of the array

static struct {
  Rom rom;
  bool have_args, victims_done, objects_done;
  uint32_t record;
  ActorLists al;
  int checks, failures;
} ac;

static void ac_check(const char* what, bool ok, const char* detail) {
  ac.checks++;
  if (!ok) ac.failures++;
  printf("  %-40s %s%s%s\n", what, ok ? "OK" : "FAIL",
         detail && *detail ? "  " : "", detail ? detail : "");
}

static void actors_on_victims(Snes* snes) {
  ac.victims_done = true;
  uint16_t rom_count = wram_word(snes, WRAM_VICTIM_COUNT);
  uint16_t gate = wram_word(snes, WRAM_VICTIM_GATE);

  char detail[128];
  // The parser keeps a prefix of the list, stopping at the first index past the
  // gate, so it can never keep *more* than we decoded — that would mean our
  // record stride or terminator is wrong.
  bool count_ok = rom_count <= (uint16_t)ac.al.victim_count;
  snprintf(detail, sizeof detail, "ROM kept %u of %d (gate $1D50 = $%04X)",
           rom_count, ac.al.victim_count, gate);
  ac_check("victim count", count_ok, detail);
  if (!count_ok) return;

  bool ok = true;
  detail[0] = '\0';
  for (uint16_t i = 0; i < rom_count; i++) {
    uint16_t rx = wram_word(snes, WRAM_VICTIM_X + i * 4);
    uint16_t ry = wram_word(snes, WRAM_VICTIM_Y + i * 4);
    if (rx != ac.al.victims[i].x || ry != ac.al.victims[i].y) {
      snprintf(detail, sizeof detail,
               "victim %u: ROM (%u, %u), ours (%u, %u)", i, rx, ry,
               ac.al.victims[i].x, ac.al.victims[i].y);
      ok = false;
      break;
    }
  }
  char label[64];
  snprintf(label, sizeof label, "victim positions (%u)", rom_count);
  ac_check(label, ok, detail);
}

static void actors_on_objects(Snes* snes) {
  ac.objects_done = true;
  int n = ac.al.object_count;

  // The parser writes a $C000 sentinel into $1EC4 one past the last object, so
  // that word landing exactly at our count proves the ROM stopped where we did.
  uint16_t sentinel = wram_word(snes, WRAM_OBJECT_END + n * 2);
  char detail[128];
  snprintf(detail, sizeof detail, "$1EC4[%d] = $%04X (want $C000)", n, sentinel);
  ac_check("object count", sentinel == 0xc000, detail);

  bool ok = true;
  detail[0] = '\0';
  for (int i = 0; i < n; i++) {
    uint16_t rx = wram_word(snes, WRAM_OBJECT_X + i * 2);
    uint16_t ry = wram_word(snes, WRAM_OBJECT_Y + i * 2);
    uint8_t rt = (uint8_t)wram_word(snes, WRAM_OBJECT_TYPE + i * 2);
    const ObjectPlacement* o = &ac.al.objects[i];
    if (rx != o->x || ry != o->y || rt != o->type) {
      snprintf(detail, sizeof detail,
               "object %d: ROM (%u, %u) $%02X, ours (%u, %u) $%02X", i, rx, ry,
               rt, o->x, o->y, o->type);
      ok = false;
      break;
    }
  }
  char label[64];
  snprintf(label, sizeof label, "object positions and types (%d)", n);
  ac_check(label, ok, detail);
}

static void actors_step(Snes* snes) {
  Cpu* cpu = snes->cpu;
  if (!cpu->resetWanted && !cpu->stopped && !cpu->waiting && !cpu->intWanted) {
    uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
    if (pc == LEVEL_LOAD_ARGS && !ac.have_args) {
      ac.record = 0x9f0000u | (cpu->x & 0xffff);
      ac.have_args = true;
      LevelHeader h;
      if (level_header_read(&ac.rom, ac.record, &h) == LEVEL_OK) {
        if (actors_read(&ac.rom, &h, &ac.al) != ACTOR_OK) ac.failures++;
      } else {
        ac.failures++;
      }
    } else if (pc == VICTIMS_PARSE_DONE && ac.have_args && !ac.victims_done) {
      actors_on_victims(snes);
    } else if (pc == OBJECTS_PARSE_DONE && ac.have_args && !ac.objects_done) {
      actors_on_objects(snes);
    }
  }
  snes_runCpuCycle(snes);
}

static void actors_verify_frame(Snes* snes) {
  while (snes->inVblank) actors_step(snes);
  uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) actors_step(snes);
  snes_readBBus(snes, 0x40);
}

static int cmd_verify_actors(int argc, char** argv) {
  if (argc < 1) {
    fprintf(stderr, "usage: zamn_assets verify-actors <rom.sfc> [-m movie] [-f frames]\n");
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
  uint8_t* rom_data = read_file(rom_path, &rom_len);
  if (!rom_data) return 1;

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom_data, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    return 1;
  }
  ac.rom.data = snes->cart->rom;
  ac.rom.size = snes->cart->romSize;

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
  printf("Replaying %d frames of '%s' and diffing the victim and object lists.\n\n",
         frames, movie_path ? movie_path : "(no input)");

  for (int frame = 0; frame < frames && !(ac.victims_done && ac.objects_done); frame++) {
    if (have_movie) {
      uint16_t buttons = movie_state(&movie, frame);
      for (int b = 0; b < 12; b++) snes_setButtonState(snes, 1, b, (buttons >> b) & 1);
    }
    actors_verify_frame(snes);
  }

  int rc;
  if (!ac.have_args) {
    printf("The movie never reached a level load — nothing was verified.\n");
    rc = 1;
  } else {
    printf("\nLevel record %s: %d actors, %d victims, %d objects decoded.\n",
           addr_str(ac.record), ac.al.actor_count, ac.al.victim_count,
           ac.al.object_count);
    if (!ac.victims_done) ac_check("victim list reached", false, "parser never ran");
    if (!ac.objects_done) ac_check("object list reached", false, "parser never ran");
    printf("%d check%s, %d failed.\n", ac.checks, ac.checks == 1 ? "" : "s", ac.failures);
    rc = ac.failures > 0 ? 1 : 0;
  }

  if (have_movie) movie_free(&movie);
  snes_free(snes);
  free(rom_data);
  return rc;
}

// ---------------------------------------------------------------------------
// sprite / frame: draw one metasprite or one 16x16 frame
// ---------------------------------------------------------------------------

// A metasprite says nothing about which of the sprite palette's eight 16-colour
// rows to use — that comes from the actor. `--pal-index` picks one, and
// `--level` says which level's sprite palette to take it from.
static bool load_sprite_palette(const Rom* rom, int level, uint32_t pal_addr,
                                uint8_t out_rgb[(LEVEL_PALETTE_BYTES / 2) * 3]) {
  if (!pal_addr) {
    uint32_t rec = 0;
    LevelHeader h;
    if (!level_record_addr(rom, level, &rec) ||
        level_header_read(rom, rec, &h) != LEVEL_OK) {
      fprintf(stderr, "error: level must be %d..%d\n", LEVEL_FIRST, LEVEL_COUNT);
      return false;
    }
    pal_addr = h.sprite_palette;
  }
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, pal_addr, &avail);
  if (!p || avail < LEVEL_PALETTE_BYTES) {
    fprintf(stderr, "error: no palette at %s\n", addr_str(pal_addr));
    return false;
  }
  gfx_decode_palette(p, LEVEL_PALETTE_BYTES / 2, out_rgb);
  return true;
}

// Composite the pieces onto a canvas big enough to hold all of them, at their
// authored offsets. This is not the OAM path — it is what the OAM path would
// look like on screen with nothing else around, which is the useful picture.
static bool render_meta(const Rom* rom, const SpriteMeta* meta,
                        const uint8_t* pal_rgb, int sub, int scale,
                        const char* path) {
  int min_x = 0, min_y = 0, max_x = SPRITE_W, max_y = SPRITE_H;
  for (int i = 0; i < meta->count; i++) {
    if (meta->pieces[i].x < min_x) min_x = meta->pieces[i].x;
    if (meta->pieces[i].y < min_y) min_y = meta->pieces[i].y;
    if (meta->pieces[i].x + SPRITE_W > max_x) max_x = meta->pieces[i].x + SPRITE_W;
    if (meta->pieces[i].y + SPRITE_H > max_y) max_y = meta->pieces[i].y + SPRITE_H;
  }
  int w = max_x - min_x, h = max_y - min_y;
  uint8_t* img = (uint8_t*)calloc((size_t)w * h * scale * scale, 3);
  if (!img) return false;

  // Back to front: the ROM's pieces are listed in the order they take OAM
  // slots, and on the SNES a lower OAM index wins.
  for (int i = meta->count - 1; i >= 0; i--) {
    const SpritePiece* pc = &meta->pieces[i];
    uint8_t raw[SPRITE_FRAME_BYTES], px[SPRITE_FRAME_PIXELS];
    if (!sprite_frame_read(rom, pc->frame, raw)) continue;
    sprite_frame_pixels(raw, px);
    bool flip_x = (pc->attr & 0x4000) != 0, flip_y = (pc->attr & 0x8000) != 0;
    int pal = sub >= 0 ? sub : (int)((pc->attr >> 9) & 7);
    for (int y = 0; y < SPRITE_H; y++) {
      for (int x = 0; x < SPRITE_W; x++) {
        uint8_t idx = px[(flip_y ? SPRITE_H - 1 - y : y) * SPRITE_W +
                         (flip_x ? SPRITE_W - 1 - x : x)];
        if (!idx) continue;  // colour 0 is transparent for sprites
        int cx = pc->x - min_x + x, cy = pc->y - min_y + y;
        const uint8_t* c = pal_rgb + (pal * 16 + idx) * 3;
        for (int sy = 0; sy < scale; sy++)
          for (int sx = 0; sx < scale; sx++)
            memcpy(img + (((size_t)cy * scale + sy) * w * scale + cx * scale + sx) * 3,
                   c, 3);
      }
    }
  }

  bool ok = stbi_write_png(path, w * scale, h * scale, 3, img, w * scale * 3) != 0;
  if (ok) printf("\n  %dx%d PNG written to %s\n", w * scale, h * scale, path);
  else fprintf(stderr, "error: cannot write '%s'\n", path);
  free(img);
  return ok;
}

static int cmd_sprite(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "usage: zamn_assets sprite <rom.sfc> <bank:addr> [out.png] [options]\n"
            "   --level <n>       take the sprite palette from level n (default 2)\n"
            "   --pal <bank:addr> use this palette instead\n"
            "   --pal-index <n>   force sub-palette n (default: each piece's own)\n"
            "   --scale <n>       magnify the PNG n times (default 4)\n");
    return 2;
  }
  uint32_t addr;
  if (!parse_addr24(argv[1], &addr)) {
    fprintf(stderr, "error: cannot parse address '%s'\n", argv[1]);
    return 2;
  }
  const char* out_path = (argc > 2 && argv[2][0] != '-') ? argv[2] : NULL;
  int level = 2, sub = -1, scale = 4;
  uint32_t pal_addr = 0;
  for (int i = out_path ? 3 : 2; i < argc; i++) {
    bool has_next = i + 1 < argc;
    if (!strcmp(argv[i], "--level") && has_next) level = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--pal-index") && has_next) sub = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--scale") && has_next) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--pal") && has_next && parse_addr24(argv[++i], &pal_addr)) ;
    else { fprintf(stderr, "error: unknown option '%s'\n", argv[i]); return 2; }
  }
  if (scale < 1) scale = 1;

  int rom_len = 0;
  uint8_t* rom_data = read_file(argv[0], &rom_len);
  if (!rom_data) return 1;
  Rom rom = {rom_data, (uint32_t)rom_len};

  SpriteMeta meta;
  int rc = sprite_meta_read(&rom, addr, &meta);
  if (rc != SPRITE_OK) {
    fprintf(stderr, "error: no metasprite at %s (status %d)%s\n", addr_str(addr), rc,
            rc == SPRITE_ERR_BANK ? " — metasprites live in banks $8F and $90" : "");
    free(rom_data);
    return 1;
  }

  printf("Metasprite at %s — %d piece%s, %u bytes\n\n", addr_str(addr), meta.count,
         meta.count == 1 ? "" : "s", meta.bytes);
  printf("   #   offset      attr   frame   graphics\n");
  for (int i = 0; i < meta.count; i++) {
    const SpritePiece* p = &meta.pieces[i];
    printf("  %2d  %+4d,%+4d   $%04X   $%03X    %s%s%s\n", i, p->x, p->y, p->attr,
           p->frame, addr_str(sprite_frame_addr(p->frame)),
           (p->attr & 0x4000) ? "  flip-x" : "", (p->attr & 0x8000) ? "  flip-y" : "");
  }

  bool ok = true;
  if (out_path) {
    uint8_t pal[(LEVEL_PALETTE_BYTES / 2) * 3];
    ok = load_sprite_palette(&rom, level, pal_addr, pal) &&
         render_meta(&rom, &meta, pal, sub, scale, out_path);
  }
  free(rom_data);
  return ok ? 0 : 1;
}

static int cmd_frame(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "usage: zamn_assets frame <rom.sfc> <frame 0-%d> [out.png] [options]\n"
            "   --count <n>       draw n consecutive frames as a sheet (default 1)\n"
            "   --cols <n>        frames per row (default 8)\n"
            "   --level <n>       take the sprite palette from level n (default 2)\n"
            "   --pal <bank:addr> use this palette instead\n"
            "   --pal-index <n>   sub-palette (default 0)\n"
            "   --scale <n>       magnify the PNG n times (default 4)\n",
            SPRITE_FRAME_COUNT - 1);
    return 2;
  }
  int first = (int)strtol(argv[1], NULL, 0);
  const char* out_path = (argc > 2 && argv[2][0] != '-') ? argv[2] : NULL;
  int count = 1, cols = 8, level = 2, sub = 0, scale = 4;
  uint32_t pal_addr = 0;
  for (int i = out_path ? 3 : 2; i < argc; i++) {
    bool has_next = i + 1 < argc;
    if (!strcmp(argv[i], "--count") && has_next) count = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--cols") && has_next) cols = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--level") && has_next) level = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--pal-index") && has_next) sub = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--scale") && has_next) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--pal") && has_next && parse_addr24(argv[++i], &pal_addr)) ;
    else { fprintf(stderr, "error: unknown option '%s'\n", argv[i]); return 2; }
  }
  if (first < 0 || first >= SPRITE_FRAME_COUNT || count < 1) {
    fprintf(stderr, "error: frame must be 0..%d\n", SPRITE_FRAME_COUNT - 1);
    return 2;
  }
  if (cols < 1) cols = 1;
  if (scale < 1) scale = 1;

  int rom_len = 0;
  uint8_t* rom_data = read_file(argv[0], &rom_len);
  if (!rom_data) return 1;
  Rom rom = {rom_data, (uint32_t)rom_len};

  printf("Frame $%03X at %s", first, addr_str(sprite_frame_addr((uint16_t)first)));
  if (count > 1)
    printf(" .. frame $%03X at %s", first + count - 1,
           addr_str(sprite_frame_addr((uint16_t)(first + count - 1))));
  printf("\n");

  bool ok = true;
  if (out_path) {
    uint8_t pal[(LEVEL_PALETTE_BYTES / 2) * 3];
    if (!load_sprite_palette(&rom, level, pal_addr, pal)) { free(rom_data); return 1; }
    // A 16x16 frame is four 8x8 tiles, so the existing sheet writer draws it if
    // the frames are unpacked into tile order first.
    int rows = (count + cols - 1) / cols;
    uint8_t* sheet = (uint8_t*)calloc((size_t)rows * cols * 4, GFX_TILE_PIXELS);
    if (!sheet) { free(rom_data); return 1; }
    for (int i = 0; i < count; i++) {
      uint8_t raw[SPRITE_FRAME_BYTES], px[SPRITE_FRAME_PIXELS];
      if (!sprite_frame_read(&rom, (uint16_t)(first + i), raw)) continue;
      sprite_frame_pixels(raw, px);
      int fx = (i % cols) * 2, fy = (i / cols) * 2;  // in 8x8 tiles
      for (int t = 0; t < SPRITE_FRAME_TILES; t++) {
        int tile = (fy + (t >> 1)) * (cols * 2) + fx + (t & 1);
        for (int y = 0; y < GFX_TILE_H; y++)
          for (int x = 0; x < GFX_TILE_W; x++)
            sheet[tile * GFX_TILE_PIXELS + y * GFX_TILE_W + x] =
                px[((t >> 1) * 8 + y) * SPRITE_W + (t & 1) * 8 + x];
      }
    }
    ok = write_tilesheet(out_path, sheet, (uint32_t)rows * cols * 4, cols * 2,
                         pal + sub * 16 * 3, 16, scale);
    free(sheet);
  }
  free(rom_data);
  return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// verify-sprites: diff our OAM composition against the ROM's own emitters
// ---------------------------------------------------------------------------

// The four emitters `$80:BD1F` dispatches to through `($80:BDEA,X)`, and the
// RTS that ends each one.
static const struct { uint32_t entry, ret; SpriteFlip flip; const char* name; } SP_EMIT[] = {
  {0x80ba51, 0x80bab9, SPRITE_FLIP_NONE, "$80:BA51 no flip"},
  {0x80baba, 0x80bb2f, SPRITE_FLIP_X,    "$80:BABA flip-x"},
  {0x80bb30, 0x80bba5, SPRITE_FLIP_Y,    "$80:BB30 flip-y"},
  {0x80bba6, 0x80bc22, SPRITE_FLIP_XY,   "$80:BBA6 flip-xy"},
};
#define SP_EMIT_COUNT 4

// Direct page is pinned at $0000 all game, so the emitters' scratch is just
// WRAM. Names from the disassembly of $80:BD1F and $80:BA51.
#define SP_DP_COUNT 0x86    // pieces left to draw
#define SP_DP_OAM 0x88      // byte index into the OAM buffer
#define SP_DP_META 0x8a     // pointer to the piece array (past the count byte)
#define SP_DP_META_BANK 0x8c
#define SP_DP_X 0x8e        // actor screen position
#define SP_DP_Y 0x90
#define SP_DP_ATTR_OR 0x92
#define SP_DP_ATTR_AND 0x96

#define SP_OAM_BUFFER 0x13be  // $7E:13BE, DMA'd whole to OAM by $80:B9BA
#define SP_FRAME_SLOT_MAP 0x2128  // $7E:2128, one word per frame: slot*2, or <0

// The tables the emitters index, checked against our formulas once at startup.
#define SP_TABLE_VRAM 0x80b547
#define SP_TABLE_TILE 0x80b647
#define SP_TABLE_HIGH 0x80b747

static struct {
  Rom rom;

  bool active;
  int which;
  uint16_t sp;             // stack pointer at entry, to match the right RTS
  SpriteOam before;        // OAM as the ROM found it, plus the entry index
  SpriteMeta meta;
  SpriteFlip flip;
  int16_t ox, oy;
  uint16_t attr_or, attr_and;
  uint32_t meta_addr;

  Snes* snes;             // for the frame -> tile lookup during our own emit
  int calls[SP_EMIT_COUNT];
  int captured, passed;
  int checks, failures;

  // Every distinct metasprite the movie draws, for the report.
  uint32_t seen[512];
  int seen_count;
} sp;

static void sp_check(const char* what, bool ok, const char* detail) {
  sp.checks++;
  if (!ok) sp.failures++;
  printf("  %-44s %s%s%s\n", what, ok ? "OK" : "FAIL",
         detail && *detail ? "  " : "", detail ? detail : "");
}

// The VRAM cache lookup, read out of the emulator: after the ROM's call every
// frame it drew is resident, so this returns the slot the ROM itself used.
static uint16_t sp_tile_of(uint16_t frame, void* ctx) {
  Snes* snes = (Snes*)ctx;
  uint16_t slot2 = wram_word(snes, SP_FRAME_SLOT_MAP + (uint32_t)frame * 2);
  if (slot2 & 0x8000) return 0;  // not resident — cannot happen after the call
  return sprite_slot_tile(slot2 / 2);
}

static void sprites_on_entry(Snes* snes, int which) {
  sp.active = true;
  sp.which = which;
  sp.flip = SP_EMIT[which].flip;
  sp.sp = snes->cpu->sp;
  sp.calls[which]++;

  sp.ox = (int16_t)wram_word(snes, SP_DP_X);
  sp.oy = (int16_t)wram_word(snes, SP_DP_Y);
  sp.attr_or = wram_word(snes, SP_DP_ATTR_OR);
  sp.attr_and = wram_word(snes, SP_DP_ATTR_AND);

  memset(&sp.before, 0, sizeof sp.before);
  sp.before.index = wram_word(snes, SP_DP_OAM);
  wram_block(snes, 0x7e0000u | SP_OAM_BUFFER, SPRITE_OAM_BYTES, sp.before.bytes);

  // $8A points one past the count byte ($80:BDAC), so the record starts there.
  uint32_t ptr = wram_word(snes, SP_DP_META);
  uint32_t bank = wram_word(snes, SP_DP_META_BANK) & 0xff;
  sp.meta_addr = (bank << 16) | ((ptr - 1) & 0xffff);
  if (sprite_meta_read(&sp.rom, sp.meta_addr, &sp.meta) != SPRITE_OK) sp.meta.count = -1;
}

static void sprites_on_exit(Snes* snes) {
  sp.active = false;
  sp.captured++;

  uint16_t rom_index = wram_word(snes, SP_DP_OAM);
  uint8_t rom_oam[SPRITE_OAM_BYTES];
  wram_block(snes, 0x7e0000u | SP_OAM_BUFFER, SPRITE_OAM_BYTES, rom_oam);

  SpriteOam ours = sp.before;
  sprite_emit(&ours, &sp.meta, sp.flip, sp.ox, sp.oy, sp.attr_or, sp.attr_and,
              sp_tile_of, snes, NULL);

  bool ok = ours.index == rom_index &&
            memcmp(ours.bytes, rom_oam, SPRITE_OAM_BYTES) == 0;
  if (ok) {
    sp.passed++;
    bool known = false;
    for (int i = 0; i < sp.seen_count; i++) known |= sp.seen[i] == sp.meta_addr;
    if (!known && sp.seen_count < (int)(sizeof sp.seen / sizeof sp.seen[0]))
      sp.seen[sp.seen_count++] = sp.meta_addr;
    return;
  }

  sp.failures++;
  printf("  MISMATCH  %s  metasprite %s  %d pieces  at (%d, %d)\n",
         SP_EMIT[sp.which].name, addr_str(sp.meta_addr), sp.meta.count, sp.ox, sp.oy);
  if (ours.index != rom_index)
    printf("            OAM index: ROM $%04X, ours $%04X\n", rom_index, ours.index);
  for (uint32_t i = 0, shown = 0; i < SPRITE_OAM_BYTES && shown < 8; i++) {
    if (ours.bytes[i] == rom_oam[i]) continue;
    printf("            byte $%03X (sprite %u, %s): ROM $%02X, ours $%02X\n", i,
           i / 4, (const char*[]){"x", "y", "tile", "attr"}[i & 3], rom_oam[i],
           ours.bytes[i]);
    shown++;
  }
}

static void sprites_step(Snes* snes) {
  Cpu* cpu = snes->cpu;
  if (!cpu->resetWanted && !cpu->stopped && !cpu->waiting && !cpu->intWanted) {
    uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
    if (!sp.active) {
      for (int i = 0; i < SP_EMIT_COUNT; i++)
        if (pc == SP_EMIT[i].entry) { sprites_on_entry(snes, i); break; }
    } else if (pc == SP_EMIT[sp.which].ret && cpu->sp == sp.sp) {
      sprites_on_exit(snes);
    }
  }
  snes_runCpuCycle(snes);
}

static void sprites_frame(Snes* snes) {
  while (snes->inVblank) sprites_step(snes);
  uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) sprites_step(snes);
  snes_readBBus(snes, 0x40);
}

// The three tables the emitters and the VRAM cache index are pure functions of
// the slot or sprite number, so the port computes them. Prove that here rather
// than asserting it in a comment.
static void sprites_check_tables(void) {
  bool tile_ok = true, vram_ok = true, high_ok = true;
  for (int s = 0; s < SPRITE_SLOTS; s++) {
    tile_ok &= rom_word(&sp.rom, SP_TABLE_TILE + s * 2) == sprite_slot_tile(s);
    vram_ok &= rom_word(&sp.rom, SP_TABLE_VRAM + s * 2) == sprite_slot_vram(s);
  }
  for (int n = 0; n < SPRITE_OAM_SPRITES; n++) {
    high_ok &= rom_word(&sp.rom, SP_TABLE_HIGH + n * 4) ==
               (uint16_t)(SPRITE_OAM_LOW_BYTES + (n >> 2));
    high_ok &= rom_word(&sp.rom, SP_TABLE_HIGH + n * 4 + 2) ==
               (uint16_t)(1u << ((n & 3) * 2));
  }
  sp_check("slot -> OAM tile ($80:B647, 128 entries)", tile_ok, "");
  sp_check("slot -> VRAM address ($80:B547, 128 entries)", vram_ok, "");
  sp_check("OAM high table ($80:B747, 128 entries)", high_ok, "");
}

static int cmd_verify_sprites(int argc, char** argv) {
  if (argc < 1) {
    fprintf(stderr, "usage: zamn_assets verify-sprites <rom.sfc> [-m movie] [-f frames]\n");
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
  uint8_t* rom_data = read_file(rom_path, &rom_len);
  if (!rom_data) return 1;

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom_data, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    return 1;
  }
  sp.rom.data = snes->cart->rom;
  sp.rom.size = snes->cart->romSize;

  Movie movie;
  bool have_movie = false;
  if (movie_path) {
    if (!movie_load(&movie, movie_path)) {
      fprintf(stderr, "error: cannot load movie '%s'\n", movie_path);
      return 1;
    }
    have_movie = true;
  }

  printf("Static tables:\n");
  sprites_check_tables();

  snes_reset(snes, true);
  printf("\nReplaying %d frames of '%s' and diffing every OAM emission.\n\n",
         frames, movie_path ? movie_path : "(no input)");

  for (int frame = 0; frame < frames; frame++) {
    if (have_movie) {
      uint16_t buttons = movie_state(&movie, frame);
      for (int b = 0; b < 12; b++) snes_setButtonState(snes, 1, b, (buttons >> b) & 1);
    }
    sprites_frame(snes);
  }

  printf("Calls intercepted: %d\n", sp.captured);
  for (int i = 0; i < SP_EMIT_COUNT; i++)
    printf("  %-20s %d\n", SP_EMIT[i].name, sp.calls[i]);
  char detail[96];
  snprintf(detail, sizeof detail, "%d of %d emissions byte-identical", sp.passed,
           sp.captured);
  sp_check("OAM output matches the ROM's emitters", sp.captured > 0 && sp.passed == sp.captured,
           detail);
  // Which metasprites the movie actually drew — the evidence for where this
  // data lives, and the seed for widening coverage with more movies.
  printf("\n%d distinct metasprite%s drawn:\n", sp.seen_count,
         sp.seen_count == 1 ? "" : "s");
  for (int i = 1; i < sp.seen_count; i++) {  // insertion sort; the list is tiny
    uint32_t v = sp.seen[i];
    int j = i - 1;
    for (; j >= 0 && sp.seen[j] > v; j--) sp.seen[j + 1] = sp.seen[j];
    sp.seen[j + 1] = v;
  }
  for (int i = 0; i < sp.seen_count; i++)
    printf("  %s%s", addr_str(sp.seen[i]), (i % 8 == 7 || i == sp.seen_count - 1) ? "\n" : "");

  printf("\n%d check%s, %d failed.\n", sp.checks, sp.checks == 1 ? "" : "s", sp.failures);

  int rc = sp.failures > 0 ? 1 : 0;
  if (have_movie) movie_free(&movie);
  snes_free(snes);
  free(rom_data);
  return rc;
}

// ---------------------------------------------------------------------------
// music: report the APU data sets and what each level plays
// ---------------------------------------------------------------------------

static const char* music_set_kind(int index) {
  if (index == MUSIC_SET_DRIVER) return "driver + samples";
  if (index == MUSIC_SET_SFX) return "sound effects";
  if (index >= MUSIC_SET_SAMPLES) return "sample set";
  return "song";
}

static int cmd_music(int argc, char** argv) {
  if (argc < 1) {
    fprintf(stderr, "usage: zamn_assets music <rom.sfc>\n");
    return 2;
  }
  int rom_len = 0;
  uint8_t* rom_data = read_file(argv[0], &rom_len);
  if (!rom_data) return 1;
  Rom rom = {rom_data, (uint32_t)rom_len};

  printf("APU data sets — table at %s\n\n", addr_str(MUSIC_TABLE_ADDR));

  uint8_t* image = (uint8_t*)malloc(MUSIC_DRIVER_BYTES);
  MusicSet driver;
  int err = image ? music_driver_image(&rom, image, MUSIC_DRIVER_BYTES) : MUSIC_ERR_SIZE;
  if (err == MUSIC_OK) err = music_driver_blocks(image, MUSIC_DRIVER_BYTES, &driver);
  if (err != MUSIC_OK) {
    fprintf(stderr, "error: driver image does not decode (%d)\n", err);
    free(image);
    free(rom_data);
    return 1;
  }
  printf("  [ 0] %s  %-16s  %s + %s, %u bytes staged\n",
         addr_str(MUSIC_DRIVER_STAGE_ADDR), music_set_kind(0),
         addr_str(MUSIC_DRIVER_PART0_ADDR), addr_str(MUSIC_DRIVER_PART1_ADDR),
         (unsigned)MUSIC_DRIVER_BYTES);
  for (int i = 0; i < driver.count; i++)
    printf("       block %d: %5u bytes -> SPC $%04X\n", i, driver.blocks[i].bytes,
           driver.blocks[i].dest);
  printf("       execute at SPC $%04X\n\n", driver.exec);

  for (int i = 1; i < MUSIC_SET_COUNT; i++) {
    MusicSet set;
    if (music_set_read(&rom, i, &set) != MUSIC_OK) {
      uint32_t addr = 0;
      music_set_addr(&rom, i, &addr);
      printf("  [%2d] %s  does not decode\n", i, addr_str(addr));
      continue;
    }
    printf("  [%2d] %s  %-16s  %2d block%s, %5u bytes  (ends %s)\n", i,
           addr_str(set.addr), music_set_kind(i), set.count,
           set.count == 1 ? " " : "s", set.payload_bytes,
           addr_str(set.addr + set.stream_bytes));
  }

  printf("\nPer level (record +$32 song, +$34 sample set):\n");
  for (int level = LEVEL_FIRST; level < LEVEL_FIRST + LEVEL_COUNT; level++) {
    uint32_t addr = 0;
    LevelHeader h;
    if (!level_record_addr(&rom, level, &addr)) continue;
    if (level_header_read(&rom, addr, &h) != LEVEL_OK) continue;
    printf("  level %2d  song %2u  samples %u (set %2u)%s", level, h.song,
           h.sample_set, MUSIC_SET_SAMPLES + h.sample_set,
           level % 3 == 0 ? "\n" : "   ");
  }
  printf("\n");

  free(image);
  free(rom_data);
  return 0;
}

// ---------------------------------------------------------------------------
// spc: dump a level's music as a playable .spc file
// ---------------------------------------------------------------------------

// Unlike a level or a metasprite, a song cannot be decoded to an artifact
// straight out of the ROM: the bytes go to the SPC700 one at a time through the
// driver's command port, and it is the *driver* that decides where they land.
// There is no address to point a decoder at.
//
// So this does what the verifiers do — runs the ROM under the reference core —
// and then dumps the APU. The one liberty it takes is overriding the arguments
// of the game's own `$80:CBD9` call, which is what lets any of the 56 levels'
// music be reached without playing to that level.

#define SPC_PLAY_SONG 0x80cbd9  // A = song data set, X = sample set
#define SPC_PLAY_SONG_RTL 0x80cbfc

// SPC file layout (v0.30, text ID666). 66048 bytes.
#define SPC_FILE_BYTES 0x10200
#define SPC_OFF_RAM 0x100
#define SPC_OFF_DSP 0x10100
#define SPC_OFF_IPL 0x101c0

static void spc_put(uint8_t* f, uint32_t off, const char* s, uint32_t field) {
  size_t n = strlen(s);
  if (n > field) n = field;  // ID666 text fields are padded, not terminated
  memcpy(f + off, s, n);
}

static bool spc_write_file(const char* path, Snes* snes, const char* song_title,
                           const char* comment) {
  Apu* apu = snes->apu;
  uint8_t* f = (uint8_t*)calloc(1, SPC_FILE_BYTES);
  if (!f) return false;

  memcpy(f, "SNES-SPC700 Sound File Data v0.30", 33);
  f[0x21] = 0x1a;
  f[0x22] = 0x1a;
  f[0x23] = 26;  // 26 = an ID666 tag follows
  f[0x24] = 30;  // version minor

  // SPC700 register file, exactly as the core has it.
  f[0x25] = (uint8_t)(apu->spc->pc & 0xff);
  f[0x26] = (uint8_t)(apu->spc->pc >> 8);
  f[0x27] = apu->spc->a;
  f[0x28] = apu->spc->x;
  f[0x29] = apu->spc->y;
  f[0x2a] = (uint8_t)(apu->spc->n << 7 | apu->spc->v << 6 | apu->spc->p << 5 |
                      apu->spc->b << 4 | apu->spc->h << 3 | apu->spc->i << 2 |
                      apu->spc->z << 1 | apu->spc->c);
  f[0x2b] = apu->spc->sp;

  spc_put(f, 0x2e, song_title, 32);
  spc_put(f, 0x4e, "Zombies Ate My Neighbors", 32);
  spc_put(f, 0x6e, "zamn_assets", 16);
  spc_put(f, 0x7e, comment, 32);
  spc_put(f, 0xa9, "180", 3);    // seconds before fade
  spc_put(f, 0xac, "10000", 5);  // fade length, ms
  f[0xd2] = 0;                   // emulator: unknown

  memcpy(f + SPC_OFF_RAM, apu->ram, 0x10000);
  memcpy(f + SPC_OFF_DSP, apu->dsp->ram, 0x80);

  // The boot ROM is private to the core, but its own SPC-side read handler
  // returns it while `romReadable` is set — which the driver clears once it is
  // running, so ask with the flag restored afterwards.
  bool rom_was = apu->romReadable;
  apu->romReadable = true;
  for (uint32_t i = 0; i < 0x40; i++) f[SPC_OFF_IPL + i] = apu_spcRead(apu, (uint16_t)(0xffc0 + i));
  apu->romReadable = rom_was;

  FILE* out = fopen(path, "wb");
  if (!out) { free(f); return false; }
  bool ok = fwrite(f, 1, SPC_FILE_BYTES, out) == SPC_FILE_BYTES;
  fclose(out);
  free(f);
  return ok;
}

// 48 kHz stereo, the rate `src/main_sdl.c` runs the core's audio at.
#define SPC_WAV_RATE 48000
#define SPC_WAV_FRAME_SAMPLES (SPC_WAV_RATE / 60)

static void wav_u32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static bool spc_write_wav(const char* path, const int16_t* samples, uint32_t frames) {
  uint32_t data_bytes = frames * 2 * 2;  // stereo, 16-bit
  uint8_t hdr[44];
  memcpy(hdr, "RIFF", 4);
  wav_u32(hdr + 4, 36 + data_bytes);
  memcpy(hdr + 8, "WAVEfmt ", 8);
  wav_u32(hdr + 16, 16);      // fmt chunk size
  hdr[20] = 1; hdr[21] = 0;   // PCM
  hdr[22] = 2; hdr[23] = 0;   // stereo
  wav_u32(hdr + 24, SPC_WAV_RATE);
  wav_u32(hdr + 28, SPC_WAV_RATE * 4);  // byte rate
  hdr[32] = 4; hdr[33] = 0;   // block align
  hdr[34] = 16; hdr[35] = 0;  // bits per sample
  memcpy(hdr + 36, "data", 4);
  wav_u32(hdr + 40, data_bytes);

  FILE* f = fopen(path, "wb");
  if (!f) return false;
  bool ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr &&
            fwrite(samples, 1, data_bytes, f) == data_bytes;
  fclose(f);
  return ok;
}

static struct {
  int song, sample_set;
  bool hijacked, done;
  int done_frame;
} sc;

static void spc_step(Snes* snes) {
  Cpu* cpu = snes->cpu;
  if (!cpu->resetWanted && !cpu->stopped && !cpu->waiting && !cpu->intWanted) {
    uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
    if (pc == SPC_PLAY_SONG && !sc.hijacked) {
      // The game is about to load whatever the current screen wants. Swap in
      // the sets we were asked for and let its own routine do the work.
      cpu->a = (uint16_t)sc.song;
      cpu->x = (uint16_t)sc.sample_set;
      sc.hijacked = true;
    } else if (pc == SPC_PLAY_SONG_RTL && sc.hijacked && !sc.done) {
      sc.done = true;
    }
  }
  snes_runCpuCycle(snes);
}

static void spc_frame(Snes* snes) {
  while (snes->inVblank) spc_step(snes);
  uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) spc_step(snes);
  snes_readBBus(snes, 0x40);
}

static int cmd_spc(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "usage: zamn_assets spc <rom.sfc> <level 1-56> <out.spc> [options]\n\n"
            "  -m, --movie <file>   input movie to reach the title screen\n"
            "  -f, --frames <n>     frame budget (default 1600)\n"
            "      --settle <n>     frames to run after the song starts (default 12)\n"
            "      --song <n>       override the song data set (2-11)\n"
            "      --samples <n>    override the sample set (0-3)\n"
            "      --wav <file>     also render audio to a .wav\n"
            "      --seconds <n>    how much to render (default 30)\n");
    return 2;
  }
  const char* rom_path = argv[0];
  int level = atoi(argv[1]);
  const char* out_path = argc > 2 && argv[2][0] != '-' ? argv[2] : NULL;
  const char* movie_path = NULL;
  const char* wav_path = NULL;
  int frames = 1600, settle = 12, song = -1, samples = -1, seconds = 30;

  for (int i = out_path ? 3 : 2; i < argc; i++) {
    bool has_next = i + 1 < argc;
    if ((!strcmp(argv[i], "-m") || !strcmp(argv[i], "--movie")) && has_next) movie_path = argv[++i];
    else if ((!strcmp(argv[i], "-f") || !strcmp(argv[i], "--frames")) && has_next) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--settle") && has_next) settle = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--song") && has_next) song = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--samples") && has_next) samples = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--wav") && has_next) wav_path = argv[++i];
    else if (!strcmp(argv[i], "--seconds") && has_next) seconds = atoi(argv[++i]);
    else { fprintf(stderr, "error: unknown option '%s'\n", argv[i]); return 2; }
  }
  if (!out_path && !wav_path) { fprintf(stderr, "error: no output path\n"); return 2; }
  if (seconds < 1) seconds = 1;

  int rom_len = 0;
  uint8_t* rom_data = read_file(rom_path, &rom_len);
  if (!rom_data) return 1;

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom_data, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    return 1;
  }
  Rom rom = {snes->cart->rom, snes->cart->romSize};

  // The level record names both sets; either can be overridden to reach a song
  // no level uses.
  uint32_t addr = 0;
  LevelHeader h;
  if (!level_record_addr(&rom, level, &addr) || level_header_read(&rom, addr, &h) != LEVEL_OK) {
    fprintf(stderr, "error: level %d has no record\n", level);
    return 1;
  }
  sc.song = song >= 0 ? song : h.song;
  sc.sample_set = samples >= 0 ? samples : h.sample_set;
  if (sc.song < MUSIC_SONG_FIRST || sc.song > MUSIC_SONG_LAST) {
    fprintf(stderr, "error: song %d is not a song data set (%d-%d)\n", sc.song,
            MUSIC_SONG_FIRST, MUSIC_SONG_LAST);
    return 1;
  }
  if (sc.sample_set < 0 || sc.sample_set >= MUSIC_SAMPLE_SET_COUNT) {
    fprintf(stderr, "error: sample set %d is out of range (0-%d)\n", sc.sample_set,
            MUSIC_SAMPLE_SET_COUNT - 1);
    return 1;
  }

  Movie movie;
  bool have_movie = false;
  if (movie_path) {
    if (!movie_load(&movie, movie_path)) {
      fprintf(stderr, "error: cannot load movie '%s'\n", movie_path);
      return 1;
    }
    have_movie = true;
  }

  printf("Level %d (record %s): song %d, sample set %d (APU data sets %d and %d)\n",
         level, addr_str(addr), sc.song, sc.sample_set, sc.song,
         MUSIC_SET_SAMPLES + sc.sample_set);

  snes_reset(snes, true);
  int used = 0;
  for (; used < frames; used++) {
    if (have_movie) {
      uint16_t buttons = movie_state(&movie, used);
      for (int b = 0; b < 12; b++) snes_setButtonState(snes, 1, b, (buttons >> b) & 1);
    }
    spc_frame(snes);
    if (sc.done) {
      if (sc.done_frame == 0) sc.done_frame = used;
      if (used - sc.done_frame >= settle) break;
    }
  }

  if (!sc.done) {
    fprintf(stderr,
            "error: the game never called $80:CBD9 in %d frames — raise -f, or\n"
            "       pass a movie that reaches the title screen\n",
            frames);
    return 1;
  }

  printf("Upload finished at frame %d; dumped %d frames later.\n", sc.done_frame, settle);

  if (out_path) {
    char title[64], comment[64];
    snprintf(title, sizeof title, "Level %d (song %d)", level, sc.song);
    snprintf(comment, sizeof comment, "samples %d, dumped frame %d", sc.sample_set, used);
    if (!spc_write_file(out_path, snes, title, comment)) {
      fprintf(stderr, "error: cannot write '%s'\n", out_path);
      return 1;
    }
    printf("Wrote %s (%d bytes)\n", out_path, SPC_FILE_BYTES);
  }

  // Keep running and collect what the emulated DSP actually produces. The game
  // sits on whatever screen it was on, so a stray sound effect can land in the
  // recording — the .spc is the clean artifact, this one is the audible one.
  if (wav_path) {
    uint32_t want = (uint32_t)seconds * 60;
    int16_t* pcm = (int16_t*)malloc((size_t)want * SPC_WAV_FRAME_SAMPLES * 2 * sizeof(int16_t));
    if (!pcm) { fprintf(stderr, "error: out of memory\n"); return 1; }
    for (uint32_t i = 0; i < want; i++) {
      spc_frame(snes);
      snes_setSamples(snes, pcm + (size_t)i * SPC_WAV_FRAME_SAMPLES * 2, SPC_WAV_FRAME_SAMPLES);
    }
    if (!spc_write_wav(wav_path, pcm, want * SPC_WAV_FRAME_SAMPLES)) {
      fprintf(stderr, "error: cannot write '%s'\n", wav_path);
      free(pcm);
      return 1;
    }
    printf("Wrote %s (%d seconds, %d Hz stereo)\n", wav_path, seconds, SPC_WAV_RATE);
    free(pcm);
  }

  if (have_movie) movie_free(&movie);
  snes_free(snes);
  free(rom_data);
  return 0;
}

// ---------------------------------------------------------------------------
// verify-music: diff our upload against the bytes the game puts on the APU bus
// ---------------------------------------------------------------------------

// The IPL upload in `$80:CB61`. Both stores are observed at the instruction
// that performs them, so what is captured is what reaches the ports.
#define MU_IPL_BLOCK 0x80cba9  // STA $2142 (16-bit): A = destination, X = length
#define MU_IPL_DATA 0x80cb8c   // STA $2140 (16-bit): A = data << 8 | boot counter

// The staging `MVN`s in `$80:CB1A` are done by here (`$80:CB32` is the PLB).
#define MU_STAGED 0x80cb33

// The data-set uploader `$80:CC7C` and the RTS that ends it, plus the single
// command send `$80:CCC8` — hooked at its `STX $2142`, where X is the command
// and A the parameter.
#define MU_UPLOAD_ENTRY 0x80cc7c
#define MU_UPLOAD_RET 0x80cca0
#define MU_SEND 0x80ccd1

#define MU_MAX_CMDS 16384

static struct {
  Rom rom;

  // The IPL upload, reassembled from the port writes.
  uint8_t* ipl_bytes;
  uint32_t ipl_len;
  MusicBlock ipl_blocks[MUSIC_MAX_BLOCKS];
  int ipl_count;
  uint16_t ipl_exec;
  bool ipl_done;

  bool staged_ok, staged_seen;

  // One data-set upload in flight.
  bool active;
  int index;
  uint16_t sp;
  uint16_t got[MU_MAX_CMDS];  // cmd << 8 | param
  uint32_t got_count;
  bool got_overflow;

  uint16_t want[MU_MAX_CMDS];
  uint32_t want_count;
  bool want_overflow;

  int uploads, uploads_ok;
  int seen_sets[MUSIC_SET_COUNT];

  // Every command sent outside an upload — the play/select traffic.
  uint16_t other[64];
  int other_count;

  int checks, failures;
} mu;

static void mu_check(const char* what, bool ok, const char* detail) {
  mu.checks++;
  if (!ok) mu.failures++;
  printf("  %-46s %s%s%s\n", what, ok ? "OK" : "FAIL",
         detail && *detail ? "  " : "", detail ? detail : "");
}

static void mu_want(uint8_t cmd, uint8_t param, void* ctx) {
  (void)ctx;
  if (mu.want_count >= MU_MAX_CMDS) { mu.want_overflow = true; return; }
  mu.want[mu.want_count++] = (uint16_t)(cmd << 8 | param);
}

static void mu_upload_end(void) {
  mu.active = false;
  mu.uploads++;
  if (mu.index >= 0 && mu.index < MUSIC_SET_COUNT) mu.seen_sets[mu.index]++;

  mu.want_count = 0;
  mu.want_overflow = false;
  // `select` is false: the leading $08 is sent by $80:CC6F before $80:CC7C is
  // entered, so it falls outside the window being captured here.
  int err = music_upload(&mu.rom, mu.index, false, mu_want, NULL);

  if (err != MUSIC_OK) {
    mu.failures++;
    printf("  MISMATCH  set %d does not decode (%d)\n", mu.index, err);
    return;
  }
  if (mu.got_overflow || mu.want_overflow) {
    mu.failures++;
    printf("  MISMATCH  set %d: command buffer overflowed (%u captured)\n", mu.index,
           mu.got_count);
    return;
  }
  if (mu.got_count == mu.want_count &&
      memcmp(mu.got, mu.want, mu.got_count * sizeof mu.got[0]) == 0) {
    mu.uploads_ok++;
    return;
  }

  mu.failures++;
  printf("  MISMATCH  set %d: ROM sent %u commands, ours %u\n", mu.index, mu.got_count,
         mu.want_count);
  uint32_t n = mu.got_count < mu.want_count ? mu.got_count : mu.want_count;
  for (uint32_t i = 0, shown = 0; i < n && shown < 8; i++) {
    if (mu.got[i] == mu.want[i]) continue;
    printf("            command %u: ROM $%02X/$%02X, ours $%02X/$%02X\n", i,
           mu.got[i] >> 8, mu.got[i] & 0xff, mu.want[i] >> 8, mu.want[i] & 0xff);
    shown++;
  }
}

static void music_step(Snes* snes) {
  Cpu* cpu = snes->cpu;
  if (!cpu->resetWanted && !cpu->stopped && !cpu->waiting && !cpu->intWanted) {
    uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
    switch (pc) {
      case MU_STAGED:
        if (!mu.staged_seen) {
          mu.staged_seen = true;
          uint8_t* ours = (uint8_t*)malloc(MUSIC_DRIVER_BYTES);
          uint8_t* theirs = (uint8_t*)malloc(MUSIC_DRIVER_BYTES);
          mu.staged_ok = ours && theirs &&
                         music_driver_image(&mu.rom, ours, MUSIC_DRIVER_BYTES) == MUSIC_OK &&
                         wram_block(snes, MUSIC_DRIVER_STAGE_ADDR, MUSIC_DRIVER_BYTES, theirs) &&
                         memcmp(ours, theirs, MUSIC_DRIVER_BYTES) == 0;
          free(ours);
          free(theirs);
        }
        break;

      case MU_IPL_BLOCK:
        // A is the destination word, X the length. A zero length ends the list
        // and the "destination" is the entry point instead ($80:CBAE).
        if (!mu.ipl_done) {
          if (cpu->x == 0) {
            mu.ipl_exec = (uint16_t)cpu->a;
            mu.ipl_done = true;
          } else if (mu.ipl_count < MUSIC_MAX_BLOCKS) {
            mu.ipl_blocks[mu.ipl_count].bytes = (uint16_t)cpu->x;
            mu.ipl_blocks[mu.ipl_count].dest = (uint16_t)cpu->a;
            mu.ipl_blocks[mu.ipl_count].addr = MUSIC_DRIVER_STAGE_ADDR + mu.ipl_len;
            mu.ipl_count++;
          }
        }
        break;

      case MU_IPL_DATA:
        // 16-bit store: the payload byte is A's high half, the boot ROM's
        // handshake counter its low half.
        if (mu.ipl_bytes && mu.ipl_len < MUSIC_DRIVER_BYTES)
          mu.ipl_bytes[mu.ipl_len++] = (uint8_t)(cpu->a >> 8);
        break;

      case MU_UPLOAD_ENTRY:
        mu.active = true;
        mu.index = (int)(cpu->a & 0xff);  // $80:CC7E masks it the same way
        mu.sp = cpu->sp;
        mu.got_count = 0;
        mu.got_overflow = false;
        break;

      case MU_UPLOAD_RET:
        if (mu.active && cpu->sp == mu.sp) mu_upload_end();
        break;

      case MU_SEND: {
        uint16_t pair = (uint16_t)((cpu->x & 0xff) << 8 | (cpu->a & 0xff));
        if (mu.active) {
          if (mu.got_count < MU_MAX_CMDS) mu.got[mu.got_count++] = pair;
          else mu.got_overflow = true;
        } else if (mu.other_count < (int)(sizeof mu.other / sizeof mu.other[0])) {
          mu.other[mu.other_count++] = pair;
        }
        break;
      }
      default: break;
    }
  }
  snes_runCpuCycle(snes);
}

static void music_frame(Snes* snes) {
  while (snes->inVblank) music_step(snes);
  uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) music_step(snes);
  snes_readBBus(snes, 0x40);
}

// The 15 data sets and the driver never overlap in ROM, and each one's stream
// ends exactly where the next begins. That is only true if every block length
// in every set is read correctly, so it checks the whole walk at once — without
// running anything.
static void music_check_layout(void) {
  struct { uint32_t start, end; int index; } span[MUSIC_SET_COUNT];
  int n = 0;
  bool decoded = true;

  for (int i = 1; i < MUSIC_SET_COUNT; i++) {
    MusicSet set;
    if (music_set_read(&mu.rom, i, &set) != MUSIC_OK) { decoded = false; continue; }
    span[n].start = set.addr;
    span[n].end = set.addr + set.stream_bytes;
    span[n].index = i;
    n++;
  }
  mu_check("all 15 data sets decode", decoded && n == MUSIC_SET_COUNT - 1, "");

  bool overlap = false;
  int abutting = 0;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      if (i == j) continue;
      if (span[i].start < span[j].end && span[j].start < span[i].end) overlap = true;
      if (span[i].end == span[j].start) abutting++;
    }
  }
  char detail[64];
  snprintf(detail, sizeof detail, "%d of %d sets abut the next", abutting, n);
  mu_check("data sets do not overlap in ROM", !overlap, detail);

  // The driver image's block list has to account for every staged byte: the
  // ROM copies a fixed length, so a short or long walk would leave a remainder.
  uint8_t* image = (uint8_t*)malloc(MUSIC_DRIVER_BYTES);
  MusicSet driver;
  bool ok = image && music_driver_image(&mu.rom, image, MUSIC_DRIVER_BYTES) == MUSIC_OK &&
            music_driver_blocks(image, MUSIC_DRIVER_BYTES, &driver) == MUSIC_OK &&
            driver.stream_bytes == MUSIC_DRIVER_BYTES;
  snprintf(detail, sizeof detail, "%u bytes, no remainder", (unsigned)MUSIC_DRIVER_BYTES);
  mu_check("driver image blocks consume it exactly", ok, detail);
  free(image);

  // Every level names a song in 2..11 and one of the four sample sets. Nothing
  // in the record says so — this is the range the decode implies, checked
  // against all 56 records.
  bool in_range = true;
  for (int level = LEVEL_FIRST; level < LEVEL_FIRST + LEVEL_COUNT; level++) {
    uint32_t addr = 0;
    LevelHeader h;
    if (!level_record_addr(&mu.rom, level, &addr)) { in_range = false; continue; }
    if (level_header_read(&mu.rom, addr, &h) != LEVEL_OK) { in_range = false; continue; }
    if (h.song < MUSIC_SONG_FIRST || h.song > MUSIC_SONG_LAST) in_range = false;
    if (h.sample_set >= MUSIC_SAMPLE_SET_COUNT) in_range = false;
  }
  mu_check("all 56 levels name a song and a sample set", in_range,
           "+$32 in 2..11, +$34 in 0..3");
}

static int cmd_verify_music(int argc, char** argv) {
  if (argc < 1) {
    fprintf(stderr, "usage: zamn_assets verify-music <rom.sfc> [-m movie] [-f frames]\n");
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
  uint8_t* rom_data = read_file(rom_path, &rom_len);
  if (!rom_data) return 1;

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom_data, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    return 1;
  }
  mu.rom.data = snes->cart->rom;
  mu.rom.size = snes->cart->romSize;
  mu.ipl_bytes = (uint8_t*)malloc(MUSIC_DRIVER_BYTES);
  if (!mu.ipl_bytes) { fprintf(stderr, "error: out of memory\n"); return 1; }

  Movie movie;
  bool have_movie = false;
  if (movie_path) {
    if (!movie_load(&movie, movie_path)) {
      fprintf(stderr, "error: cannot load movie '%s'\n", movie_path);
      return 1;
    }
    have_movie = true;
  }

  printf("Static layout:\n");
  music_check_layout();

  snes_reset(snes, true);
  printf("\nReplaying %d frames of '%s' and diffing every APU upload.\n\n", frames,
         movie_path ? movie_path : "(no input)");

  for (int frame = 0; frame < frames; frame++) {
    if (have_movie) {
      uint16_t buttons = movie_state(&movie, frame);
      for (int b = 0; b < 12; b++) snes_setButtonState(snes, 1, b, (buttons >> b) & 1);
    }
    music_frame(snes);
  }

  // The boot upload: the staged image, the block list, and every byte of it.
  printf("Driver upload ($80:CB61, SNES IPL protocol):\n");
  mu_check("staged image matches ours", mu.staged_seen && mu.staged_ok,
           mu.staged_seen ? "$7F:0000, 39558 bytes" : "never staged");

  uint8_t* image = (uint8_t*)malloc(MUSIC_DRIVER_BYTES);
  MusicSet driver;
  bool parsed = image && music_driver_image(&mu.rom, image, MUSIC_DRIVER_BYTES) == MUSIC_OK &&
                music_driver_blocks(image, MUSIC_DRIVER_BYTES, &driver) == MUSIC_OK;
  bool blocks_ok = parsed && mu.ipl_done && driver.count == mu.ipl_count &&
                   driver.exec == mu.ipl_exec;
  for (int i = 0; parsed && i < driver.count && i < mu.ipl_count; i++) {
    if (driver.blocks[i].bytes != mu.ipl_blocks[i].bytes ||
        driver.blocks[i].dest != mu.ipl_blocks[i].dest)
      blocks_ok = false;
  }
  char detail[96];
  snprintf(detail, sizeof detail, "%d blocks, entry $%04X", mu.ipl_count, mu.ipl_exec);
  mu_check("block list matches the writes to $2142", blocks_ok, detail);
  for (int i = 0; i < mu.ipl_count; i++)
    printf("      block %d: %5u bytes -> SPC $%04X\n", i, mu.ipl_blocks[i].bytes,
           mu.ipl_blocks[i].dest);

  // Every byte handed to the boot ROM, reassembled from the $2140 stores. The
  // block payloads are the image minus its four-byte headers.
  bool bytes_ok = parsed && mu.ipl_len == driver.payload_bytes;
  for (int i = 0, at = 0; bytes_ok && i < driver.count; i++) {
    uint32_t off = driver.blocks[i].addr - MUSIC_DRIVER_STAGE_ADDR;
    if (memcmp(mu.ipl_bytes + at, image + off, driver.blocks[i].bytes) != 0) bytes_ok = false;
    at += driver.blocks[i].bytes;
  }
  snprintf(detail, sizeof detail, "%u of %u payload bytes", mu.ipl_len,
           parsed ? driver.payload_bytes : 0);
  mu_check("every byte sent to $2141 matches ours", bytes_ok, detail);
  free(image);

  printf("\nData-set uploads ($80:CC7C, through the driver's command port):\n");
  for (int i = 1; i < MUSIC_SET_COUNT; i++) {
    if (!mu.seen_sets[i]) continue;
    MusicSet set;
    if (music_set_read(&mu.rom, i, &set) != MUSIC_OK) continue;
    printf("      set %2d  %-16s  %2d block%s  %5u bytes  x%d\n", i, music_set_kind(i),
           set.count, set.count == 1 ? " " : "s", set.payload_bytes, mu.seen_sets[i]);
  }
  snprintf(detail, sizeof detail, "%d of %d uploads byte-identical", mu.uploads_ok,
           mu.uploads);
  mu_check("command stream matches the ROM's", mu.uploads > 0 && mu.uploads_ok == mu.uploads,
           detail);

  // Not a check — the evidence for what the other commands are, and the seed
  // for naming the ones still unidentified.
  printf("\n%d command%s sent outside an upload:\n", mu.other_count,
         mu.other_count == 1 ? "" : "s");
  for (int i = 0; i < mu.other_count; i++)
    printf("      $%02X param $%02X\n", mu.other[i] >> 8, mu.other[i] & 0xff);

  printf("\n%d check%s, %d failed.\n", mu.checks, mu.checks == 1 ? "" : "s", mu.failures);

  int rc = mu.failures > 0 ? 1 : 0;
  free(mu.ipl_bytes);
  if (have_movie) movie_free(&movie);
  snes_free(snes);
  free(rom_data);
  return rc;
}

// ---------------------------------------------------------------------------

static void usage(void) {
  fprintf(stderr,
          "zamn_assets — decode ZAMN data from the ROM (Phase 2)\n\n"
          "  zamn_assets verify-lzss <rom.sfc> [-m movie] [-f frames]\n"
          "      Replay a movie and compare the C decompressor against $80:CD20\n"
          "      on every call the game makes.\n\n"
          "  zamn_assets verify-level <rom.sfc> [-m movie] [-f frames]\n"
          "      Replay a movie and diff the level it loads — the expanded\n"
          "      tilemap, the row tables, the block library and the palettes —\n"
          "      against what $80:86A2 built in WRAM.\n\n"
          "  zamn_assets level <rom.sfc> <level 1-56> [out.png]\n"
          "      Report a level's record and, with a path, draw its map.\n\n"
          "  zamn_assets actors <rom.sfc> <level 1-56>\n"
          "      Report a level's actor, victim and object placement lists.\n\n"
          "  zamn_assets verify-actors <rom.sfc> [-m movie] [-f frames]\n"
          "      Replay a movie and diff the victim and object lists against\n"
          "      what $82:DB46 and $80:C9A5 built in WRAM.\n\n"
          "  zamn_assets sprite <rom.sfc> <bank:addr> [out.png] [options]\n"
          "      Report a metasprite's pieces and, with a path, draw it\n"
          "      ('sprite' with no address for the option list).\n\n"
          "  zamn_assets frame <rom.sfc> <frame 0-4095> [out.png] [options]\n"
          "      Report and draw 16x16 sprite frames.\n\n"
          "  zamn_assets verify-sprites <rom.sfc> [-m movie] [-f frames]\n"
          "      Replay a movie and diff every OAM entry the game builds\n"
          "      against the same metasprites composed in C.\n\n"
          "  zamn_assets music <rom.sfc>\n"
          "      Report the 16 APU data sets and the song each level plays.\n\n"
          "  zamn_assets spc <rom.sfc> <level 1-56> <out.spc> [options]\n"
          "      Dump a level's music as a playable .spc ('spc' for options).\n\n"
          "  zamn_assets verify-music <rom.sfc> [-m movie] [-f frames]\n"
          "      Replay a movie and diff every byte and command the game puts\n"
          "      on the APU ports against the same uploads driven from C.\n\n"
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
  if (!strcmp(cmd, "verify-level")) return cmd_verify_level(argc - 2, argv + 2);
  if (!strcmp(cmd, "level")) return cmd_level(argc - 2, argv + 2);
  if (!strcmp(cmd, "actors")) return cmd_actors(argc - 2, argv + 2);
  if (!strcmp(cmd, "verify-actors")) return cmd_verify_actors(argc - 2, argv + 2);
  if (!strcmp(cmd, "sprite")) return cmd_sprite(argc - 2, argv + 2);
  if (!strcmp(cmd, "frame")) return cmd_frame(argc - 2, argv + 2);
  if (!strcmp(cmd, "verify-sprites")) return cmd_verify_sprites(argc - 2, argv + 2);
  if (!strcmp(cmd, "music")) return cmd_music(argc - 2, argv + 2);
  if (!strcmp(cmd, "spc")) return cmd_spc(argc - 2, argv + 2);
  if (!strcmp(cmd, "verify-music")) return cmd_verify_music(argc - 2, argv + 2);
  if (!strcmp(cmd, "decompress")) return cmd_decompress(argc - 2, argv + 2);
  if (!strcmp(cmd, "gfx")) return cmd_gfx(argc - 2, argv + 2);
  if (!strcmp(cmd, "palette")) return cmd_palette(argc - 2, argv + 2);
  if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) { usage(); return 0; }
  fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
  usage();
  return 2;
}
