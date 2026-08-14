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
#include "analysis/movie_apply.h"
#include "assets/actor.h"
#include "assets/gfx.h"
#include "assets/level.h"
#include "assets/lzss.h"
#include "port/lzss.h"
#include "assets/music.h"
#include "assets/password.h"
#include "assets/rom.h"
#include "assets/sprite.h"
// For `terrain_blocked` alone, and it is the whole point of `probe`: the
// question "can the player stand here" already has an answer in this project
// that the cosim harness diffs against `$80:AE14` on every call a movie makes.
// Asking that function is not a second opinion about the game's box — it is the
// box, and `route` had been carrying a third version of it that was wrong.
#include "port/terrain.h"
// And for `floor_effect`'s four conveyor words, which are the other half of the
// same question: `terrain_blocked` says where the player may stand, and this
// says what happens to him once he is standing there. `route` had been asking
// only the first, which is why every plan it printed through an escalator was
// a plan the player rides back down.
#include "port/floor.h"
#include "port/wram.h"

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
  uint16_t a, x, y;     // the register arguments, for the WRAM transcription
  LzssRing ring_in;     // window contents the ROM call started from
} LzssCall;

static struct {
  const uint8_t* rom;
  uint32_t rom_size;

  bool active;
  LzssCall call;

  int captured, passed, skipped;
  int wram_checked, wram_passed;
  uint8_t rom_out[MAX_OUTPUT];
  uint8_t c_out[MAX_OUTPUT];
  LzssRing ring;
  // A whole copy of WRAM as the ROM call found it, so `src/port/lzss.c` can be
  // run on the same starting state and its memory effect compared.
  Wram wram_in;
} v;

// Does `src/port/lzss.c` leave memory the way `$80:CD20` left it?
//
// **This is a different question from the one above and needs a different
// comparison.** `lzss_decompress()` is the asset pipeline's: bytes in, bytes
// out, and the check is the output and the window. `lzss_decompress_wram()` is
// the game's, and its result is a memory footprint — nine words of scratch on
// direct page zero, the window, and the decompressed bytes where the caller
// asked for them.
//
// It is scoped to that footprint rather than to all of WRAM, and the reason is
// worth being precise about, because everywhere else in this project the answer
// is "all 128 KB". **One call is about 440,000 instructions, near seven frames,
// so NMIs land inside it** — that is why the routine is not in the
// co-simulation registry, and `src/cosim/routines.c` says so at length. What
// the NMI handler did in the meantime is genuinely not this routine's business.
// The footprint below is what the routine's own instructions can write, read
// off the disassembly, so anything outside it differing would be the NMI's and
// anything inside it differing is the port's.
static const char* lzss_wram_check(Snes* snes, uint32_t rom_len) {
  uint16_t written = lzss_decompress_wram(&v.wram_in, &(Rom){v.rom, v.rom_size},
                                          v.call.sp, v.call.a, v.call.x,
                                          v.call.y);
  if (written != (uint16_t)rom_len) return "WRAM (length differs)";
  if (memcmp(&v.wram_in.bytes[0x6f00], &snes->ram[0x6f00], LZSS_RING_SIZE) != 0)
    return "WRAM (window differs)";
  // $28-$41: the two long pointers, the byte count, the window position, the
  // match scratch and the destination the count is measured from.
  if (memcmp(&v.wram_in.bytes[0x28], &snes->ram[0x28], 0x1a) != 0)
    return "WRAM (scratch differs)";
  for (uint32_t i = 0; i < rom_len; i++) {
    uint32_t cur = (v.call.dst & 0xff0000) | ((v.call.dst + i) & 0xffff);
    uint32_t off;
    if (!snes_to_wram(cur, &off)) return "WRAM (destination left WRAM)";
    if (v.wram_in.bytes[off] != snes->ram[off]) return "WRAM (output differs)";
  }
  return NULL;
}

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
  v.call.a = cpu->a;
  v.call.x = cpu->x;
  v.call.y = cpu->y;
  memcpy(v.wram_in.bytes, snes->ram, WRAM_SIZE);
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

  // And the same call again, as a memory effect rather than a byte stream.
  if (!strcmp(verdict, "OK")) {
    const char* bad = lzss_wram_check(snes, rom_len);
    v.wram_checked++;
    if (bad) verdict = bad;
    else v.wram_passed++;
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
      movie_apply(&movie, snes, frame);
    }
    verify_frame(snes);
  }

  int failed = v.captured - v.passed - v.skipped;
  printf("\n%d call%s intercepted: %d byte-identical, %d failed, %d skipped.\n",
         v.captured, v.captured == 1 ? "" : "s", v.passed, failed, v.skipped);
  printf("%d of %d also checked as a memory effect — src/port/lzss.c against "
         "the same call's\nwindow, direct-page scratch and output range.\n",
         v.wram_passed, v.wram_checked);
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
//
// `reach`, when it is not NULL, is one byte per grid cell: 0 solid, 1 reached,
// 2 the start, 3 the goal, 4 open but cut off from the start, 5 water not
// reached, 6 water reached, 7 a conveyor. It is tinted over the map rather than
// drawn instead of it, because what a route needs is not "which cells are open"
// but *which opening is the way in*, and that is a question about the picture.
//
// **Solid, cut-off and water are different colours on purpose.** A search that
// answers "no route" has said nothing about which of the three it hit, and they
// want opposite things: a solid target is a target to give up on, a cut-off one
// is a door to find, and a magenta one is a shore -- which the player swims and
// `route_open` will not. Level 17's `$81:D7F6` creature lives in a pen that is
// open ground with no way into it, and one picture says so; level 1's lake reads
// as a wall on the same picture and is not one. Under `--swim` the water the
// route actually uses comes back pink, so the wet legs are visible as such.
//
// **White is a conveyor, and it overrides reached rather than sitting beside
// it.** A belt cell is green on every one of these maps ever rendered -- it is
// open ground and the flood walks straight over it -- and green is exactly the
// thing that has been wrong. The colour a reader needs there is not "you can get
// here", which is true, but "the floor moves", which decides whether the plan
// through it survives contact.
//
// It overrides reached and it does *not* override solid. Levels place belt tiles
// under scenery as freely as any other tile -- record 11 has 86 down-belt cells
// and record 35 has 39, and not one of the 125 is a floor anybody can stand on
// -- and painting those white puts a conveyor on the picture at a pixel `probe`
// calls blocked. The census the caller prints still counts every placed cell,
// because that is the number a tile table can be checked against; the picture
// shows the ones that are a floor.
static bool render_level(const LevelHeader* h, const uint16_t* map, const Rom* rom,
                         const char* path, const uint8_t* reach) {
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

  // The route grid is 8x8 like the tiles are, so one cell is one tile and the
  // overlay needs no scaling. A cell's *world* row is eight pixels below its
  // grid row (`ROUTE_Y_BIAS`), which is why the tint is drawn one tile down.
  if (reach) {
    static const uint8_t tint[8][3] = {{160, 0, 0},   {0, 200, 0},
                                       {255, 255, 0}, {0, 128, 255},
                                       {220, 110, 0}, {190, 0, 190},
                                       {255, 150, 230}, {255, 255, 255}};
    for (uint32_t ty = 0; ty + 1 < tile_rows; ty++) {
      for (uint32_t tx = 0; tx < tile_cols; tx++) {
        const uint8_t* t = tint[reach[ty * tile_cols + tx]];
        for (uint32_t y = 0; y < GFX_TILE_H; y++) {
          for (uint32_t x = 0; x < GFX_TILE_W; x++) {
            uint8_t* p = img + (((size_t)((ty + 1) * GFX_TILE_H + y)) * w
                                + tx * GFX_TILE_W + x) * 3;
            for (int c = 0; c < 3; c++) p[c] = (uint8_t)((p[c] + t[c]) / 2);
          }
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
  if (out_path) ok = render_level(&h, map, &rom, out_path, NULL);

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
      movie_apply(&movie, snes, frame);
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
  // "type", not "id": +0 is what kind of actor this is, not what it is to
  // something that runs into it. See `assets/actor.h` for what proved it.
  printf("\n  actors (%d)  type  x     y     flags  behavior\n", al->actor_count);
  for (int i = 0; i < al->actor_count; i++) {
    const ActorPlacement* a = &al->actors[i];
    printf("    %2d        $%02X  %-5u %-5u $%02X    %s\n",
           i, a->type, a->x, a->y, a->flags, addr_str(a->behavior));
  }
  printf("\n  victims (%d)  idx  x     y     behavior\n", al->victim_count);
  for (int i = 0; i < al->victim_count; i++) {
    const VictimPlacement* vv = &al->victims[i];
    printf("    %2d        %3u  %-5u %-5u %s\n", i, vv->index, vv->x, vv->y,
           addr_str(vv->behavior));
  }
  // The same list as the victims above, past the point the neighbour counter
  // stops reading it. `$81:81F6` spawns these; `$82:DB46` never sees them.
  if (al->spawn_count > 0) {
    printf("\n  spawns (%d)   x     y     behavior   (victim list tail)\n",
           al->spawn_count);
    for (int i = 0; i < al->spawn_count; i++) {
      const SpawnPlacement* s = &al->spawns[i];
      printf("    %2d       %-5u %-5u %s\n", i, s->x, s->y, addr_str(s->behavior));
    }
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
      movie_apply(&movie, snes, frame);
    }
    actors_verify_frame(snes);
  }

  int rc;
  if (!ac.have_args) {
    printf("The movie never reached a level load — nothing was verified.\n");
    rc = 1;
  } else {
    printf("\nLevel record %s: %d actors, %d victims (+%d spawns), %d objects decoded.\n",
           addr_str(ac.record), ac.al.actor_count, ac.al.victim_count,
           ac.al.spawn_count, ac.al.object_count);
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
      movie_apply(&movie, snes, frame);
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
      movie_apply(&movie, snes, used);
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
      movie_apply(&movie, snes, frame);
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


// ---------------------------------------------------------------------------
// password
// ---------------------------------------------------------------------------

static int cmd_password(int argc, char** argv) {
  if (argc < 1) {
    fprintf(stderr, "usage: zamn_assets password <rom.sfc> [<4 letters>]\n");
    return 2;
  }
  int rom_len = 0;
  uint8_t* rom_data = read_file(argv[0], &rom_len);
  if (!rom_data) return 1;
  Rom rom = {rom_data, (uint32_t)rom_len};

  if (argc >= 2) {
    char want[5] = {0};
    for (int i = 0; i < 4 && argv[1][i]; i++)
      want[i] = (char)toupper((unsigned char)argv[1][i]);
    int group = 0, variant = 0;
    if (!password_read(&rom, want, &group, &variant)) {
      printf("%s is not a password.\n", want);
      free(rom_data);
      return 1;
    }
    if (group < 0) {
      printf("%s is the one that is code rather than data ($82:B018), and it\n"
             "stores %d into $7E:1E7C where a level number goes.\n",
             want, PASSWORD_CHEAT_LEVEL);
    } else {
      printf("%s -> level %d, victim gate %d%s\n", want,
             password_level(group), password_victims(variant),
             variant == PASSWORD_VARIANTS - 1 ? " — the level as it starts" : "");
    }
    free(rom_data);
    return 0;
  }

  printf("Passwords, spelled from $82:B14A / $82:B164 / $82:B18D through the\n"
         "alphabet at $82:B178. The column is how many neighbours are left, so\n"
         "the last one in each row is that level as it starts.\n\n");
  printf("  level  ");
  for (int v = 0; v < PASSWORD_VARIANTS; v++) printf(" %4d", password_victims(v));
  printf("\n");
  for (int g = 0; g < PASSWORD_GROUPS; g++) {
    printf("   %4d  ", password_level(g));
    for (int v = 0; v < PASSWORD_VARIANTS; v++) {
      char pw[5];
      if (!password_spell(&rom, g, v, pw)) {
        fprintf(stderr, "error: table entry %d/%d does not decode\n", g, v);
        free(rom_data);
        return 1;
      }
      printf(" %s", pw);
    }
    printf("\n");
  }
  printf("\n  %s   the one that is code rather than data ($82:B018)\n",
         PASSWORD_CHEAT);

  // Non-vacuous: every spelling has to read back to the pair it came from, the
  // way $82:B018 reads one, or the two halves disagree about the tables.
  int checked = 0;
  for (int g = 0; g < PASSWORD_GROUPS; g++) {
    for (int v = 0; v < PASSWORD_VARIANTS; v++) {
      char pw[5];
      int rg = 0, rv = 0;
      if (!password_spell(&rom, g, v, pw) ||
          !password_read(&rom, pw, &rg, &rv) || rg != g || rv != v) {
        fprintf(stderr, "error: %s does not read back as %d/%d\n", pw, g, v);
        free(rom_data);
        return 1;
      }
      checked++;
    }
  }
  printf("  all %d read back to the group and variant they were spelled from.\n",
         checked);
  free(rom_data);
  return 0;
}


// ---------------------------------------------------------------------------
// route
// ---------------------------------------------------------------------------

// Cutting a route through a level by hand is the slowest thing in this project.
// It is also, since passwords, the *only* thing standing between the port and a
// dozen marked-but-untaken branches: the objects that reach them are in levels
// nobody has mapped, and every attempt so far has been a screenshot, a guess and
// a wall.
//
// The level already says where its walls are. Bit 0 of a tile's attribute word
// blocks movement (`$80:AE43  LSR A : BCS`), the expanded map gives a 9-bit tile
// index per 8x8 cell, and that is a grid to search. So this breadth-firsts from
// one point to another and prints the turns.
//
// The box is the game's own, and this round is the one that found out it was
// not. `$80:AE14` -- which every player step goes through, at `$80:E4C8` --
// turns a position into a map index as
//
//     col = (x - 9) / 8    TXA : SEC : SBC #$0009 : LSR : LSR : AND #$FFFE
//     row = (y - 8) / 8    TYA : SEC : SBC #$0008 : LSR : LSR : AND #$FFFE
//
// and then samples **six** tiles at `+0`, `+2`, `+4` and the same three a row
// down (`$80:AE43`, `$80:AE52`, `$80:AE61`, `$80:AE6F`, `$80:AE7F`,
// `$80:AE8B`). Those offsets are **bytes, not columns**: `LSR A : LSR A : AND
// #$FFFE` leaves the column index already doubled, because the expanded map is
// one *word* per cell. So they are columns c, c+1, c+2 -- a contiguous 3x2
// block, twenty-four pixels by sixteen.
//
// **Nine, not zero, and that was the bug.** This search modelled the row's bias
// and not the column's, which put its box one tile right of the game's -- two
// when x was a multiple of eight. It cleared a column the player's box never
// covers and never looked at the column the player's left edge is in, so a
// route it printed was walkable only where the corridor was wider than the
// error. Every "the movie drifts" note in this project is worth re-reading
// against that. The `probe` subcommand below asks `$80:AE14` itself and is
// what settled it.
//
// And the answer is *waypoints*, not inputs: what the movie needs is a frame to
// turn on, and turning a distance into a frame needs the walking speed, which is
// two pixels a frame and is measured rather than derived. The `--frames` form
// does that arithmetic and prints .zmv lines; without it you get coordinates.

#define ROUTE_CELL 8
#define ROUTE_SPEED 2  // pixels per frame, measured with zamn_headless --pos

// Exactly the game's box, with no margin, because there is none to be had: the
// corridors are two rows tall and adding a single row of slack disconnects
// level 1's own route -- the one this search is checked against. So a path this
// finds is walkable on the row it was planned for and on no other, which is a
// constraint on the *movie* rather than on the search. See
// `docs/analysis-tools.md`.
#define ROUTE_BOX_W 3
#define ROUTE_BOX_H 2
// The two subtractions above. A grid cell (cx,cy) is therefore the box a player
// standing anywhere in x = 8cx+9 .. 8cx+16, y = 8cy+8 .. 8cy+15 occupies, and
// the two `route_pixel_*` below pick the middle of that range rather than its
// edge -- a waypoint on the boundary is one rounding away from the next cell.
#define ROUTE_X_BIAS 9
#define ROUTE_Y_BIAS 8

// `AND #$03FF` at `$80:AE47`: ten bits of tile number. The attribute table is
// 512 words, so an index with the tenth bit set reads *past* it -- in the game
// as much as here, which is why this reports rather than guesses. Measured with
// `probe`, no level's expansion produces one.
#define ROUTE_TILE_MASK 0x03ffu

// A level loaded far enough to ask a movement question about it. `route` and
// `probe` want exactly this and nothing else, and sharing it is what keeps the
// search and the instrument that checks the search reading the same bytes.
typedef struct {
  LevelHeader h;
  uint16_t* map;  // level_map_entries(&h) entries, the expanded tilemap
  uint16_t attrs[LEVEL_BG_TILES];
  uint32_t cols, rows;
} LevelWalk;

static bool level_walk_load(const Rom* rom, int level, LevelWalk* out) {
  uint32_t addr = 0;
  memset(out, 0, sizeof *out);
  if (!level_record_addr(rom, level, &addr) ||
      level_header_read(rom, addr, &out->h) != LEVEL_OK) {
    fprintf(stderr, "error: level %d does not decode\n", level);
    return false;
  }
  uint8_t* blocks = (uint8_t*)malloc(LEVEL_BLOCK_LIB_BYTES);
  out->map = (uint16_t*)malloc(level_map_entries(&out->h) * 2);
  LzssRing ring;
  uint32_t blocks_len = 0;
  if (!blocks || !out->map ||
      level_load_blocks(rom, &out->h, blocks, &blocks_len, &ring) != LEVEL_OK ||
      level_expand(&out->h, blocks, blocks_len, rom, out->map,
                   level_map_entries(&out->h)) != LEVEL_OK) {
    fprintf(stderr, "error: level %d does not expand\n", level);
    free(blocks);
    free(out->map);
    out->map = NULL;
    return false;
  }
  free(blocks);
  uint32_t avail = 0;
  const uint8_t* attr_src = rom_ptr(rom, out->h.tile_attrs, &avail);
  if (!attr_src || avail < LEVEL_TILE_ATTR_BYTES) {
    fprintf(stderr, "error: tile attributes at %s are short\n",
            addr_str(out->h.tile_attrs));
    free(out->map);
    out->map = NULL;
    return false;
  }
  for (int i = 0; i < LEVEL_BG_TILES; i++)
    out->attrs[i] = (uint16_t)(attr_src[i * 2] | (attr_src[i * 2 + 1] << 8));
  out->cols = level_tile_cols(&out->h);
  out->rows = level_tile_rows(&out->h);
  return true;
}

static void level_walk_free(LevelWalk* lw) {
  free(lw->map);
  lw->map = NULL;
}

// Position to grid cell and back. Integer division truncates toward zero rather
// than down, so a coordinate inside the bias -- x below 9, y below 8 -- lands on
// cell 0 instead of on -1; both callers bounds-check the answer, and no actor in
// any level record sits there.
static int route_cell_x(int px) { return (px - ROUTE_X_BIAS) / ROUTE_CELL; }
static int route_cell_y(int py) { return (py - ROUTE_Y_BIAS) / ROUTE_CELL; }
static int route_pixel_x(int cx) { return cx * ROUTE_CELL + ROUTE_CELL / 2 + ROUTE_X_BIAS; }
static int route_pixel_y(int cy) { return cy * ROUTE_CELL + ROUTE_CELL / 2 + ROUTE_Y_BIAS; }

// One of the six, as `$80:AE43` and its five copies read it.
typedef struct {
  int col, row;
  bool on_map;
  uint16_t entry;  // the tilemap word...
  uint16_t index;  // ...masked to ten bits
  bool known;      // ...and whether that index is inside the attribute table
  uint16_t attr;
  bool solid;
  bool water;  // ...and blocking for a reason the player walks straight into
} ProbeTile;

static ProbeTile probe_tile(const LevelWalk* lw, int col, int row) {
  ProbeTile t;
  memset(&t, 0, sizeof t);
  t.col = col;
  t.row = row;
  t.on_map = col >= 0 && row >= 0 && (uint32_t)col < lw->cols &&
             (uint32_t)row < lw->rows;
  if (!t.on_map) return t;
  t.entry = lw->map[(uint32_t)row * lw->cols + (uint32_t)col];
  t.index = t.entry & ROUTE_TILE_MASK;
  t.known = t.index < LEVEL_BG_TILES;
  if (!t.known) return t;
  t.attr = lw->attrs[t.index];
  t.solid = (t.attr & LEVEL_ATTR_SOLID) != 0;
  t.water = (t.attr & LEVEL_ATTR_WATER_MASK) == LEVEL_ATTR_WATER;
  return t;
}

// The six, in the order the ROM tests them: left to right, then the row down.
static void probe_tiles(const LevelWalk* lw, int cx, int cy,
                        ProbeTile out[ROUTE_BOX_W * ROUTE_BOX_H]) {
  int n = 0;
  for (int dy = 0; dy < ROUTE_BOX_H; dy++)
    for (int dx = 0; dx < ROUTE_BOX_W; dx++)
      out[n++] = probe_tile(lw, cx + dx, cy + dy);
}

static bool route_open(const LevelWalk* lw, int cx, int cy) {
  ProbeTile t[ROUTE_BOX_W * ROUTE_BOX_H];
  probe_tiles(lw, cx, cy, t);
  for (int i = 0; i < ROUTE_BOX_W * ROUTE_BOX_H; i++) {
    // Off the map counts as blocked, which is also what the game does -- the
    // camera never shows past the edge. So does a tile whose attribute this
    // cannot read, because a search that guessed there would be guessing about
    // the one thing it exists to be exact about.
    if (!t[i].on_map || !t[i].known || t[i].solid) return false;
  }
  return true;
}

// Is this cell touching *water*? -- one of the six carrying `LEVEL_ATTR_WATER`,
// which is the same six and the same order, so a cell can be both this and
// blocked by ordinary scenery and the answer here is only about the water.
//
// Used for the tint and the shore count. Passability is `route_swimmable`
// below, and the two are deliberately different questions: this one says "there
// is water in the box", that one says "the box is water and nothing worse".
static bool route_water(const LevelWalk* lw, int cx, int cy) {
  ProbeTile t[ROUTE_BOX_W * ROUTE_BOX_H];
  probe_tiles(lw, cx, cy, t);
  for (int i = 0; i < ROUTE_BOX_W * ROUTE_BOX_H; i++)
    if (t[i].on_map && t[i].known && t[i].water) return true;
  return false;
}

// The same box, with water allowed through -- **because the player swims it**.
//
// The round that found `LEVEL_ATTR_WATER` got the consequence wrong. It read
// `$80:DEDE`'s branch into a scripted crossing, watched `movies/level1-keys.zmv`
// enter level 1's lake at (1214,687) and stop at (1214,657), and concluded that
// the ROM picks the destination and the player has no say -- so a search that
// flooded through water would print legs nobody can walk. That was measured on
// a movie which *releases Up at frame 3960*, and the stop at 657 was the input
// ending, not the game deciding.
//
// Held down instead (`--pos` on the same movie with the tail cut), the whole
// shape comes out, and none of it is scripted past the first thirty-two pixels:
//
//     3939..3954   sixteen frames stalled at the near shore, y=687
//     3955..3986   in, at eight pixels every nine frames, to y=655
//     4016..4040   swimming under his own input, three pixels every two
//     4041..4053   thirteen frames stalled at the far shore, y=616
//     4054..4086   climbing out, one pixel every two, to y=601
//     4087..4109   walking again at two, until ordinary scenery stops him
//
// He crosses the pond and gets out the other side. So water is passable, the
// previous round's refusal was wrong about the game, and the only true part of
// it is the timing: none of those speeds is `ROUTE_SPEED`, and the two stalls
// are worth twenty-nine frames between them. That is why this is a *separate*
// predicate behind `--swim` rather than a change to `route_open` -- a dry route
// is one `tools/fit_route.py` can time and `route_walk_check` can verify, and a
// wet one is neither until swimming is ported. Three speeds and two stalls, and
// not one of them is two pixels a frame.
static bool route_swimmable(const LevelWalk* lw, int cx, int cy) {
  ProbeTile t[ROUTE_BOX_W * ROUTE_BOX_H];
  probe_tiles(lw, cx, cy, t);
  for (int i = 0; i < ROUTE_BOX_W * ROUTE_BOX_H; i++) {
    if (!t[i].on_map || !t[i].known) return false;
    // Solid *and not water* is scenery, and scenery closes the cell however much
    // water is in the box beside it.
    if (t[i].solid && !t[i].water) return false;
  }
  return true;
}

// --- what the floor does once he is standing on it ---------------------------
//
// Everything above is `$80:AE14`'s question: six tiles, and is any of them
// solid. A conveyor is a different routine asking about a different tile, and
// the search has never asked it. `floor_effect` -- `$80:E86D`, ported in
// `src/port/floor.c` and diffed against the ROM on every call the corpus makes
// -- reads **one** attribute word through `tile_attrs_at_pixel`, masks it with
// `#$FF7F`, and for four values moves the player one pixel:
//
//     $0108  up      $0208  left
//     $0408  down    $0028  right, and only this one asks the terrain first
//
// It is called from `$80:D1FF` *before the controller is read*, so the push
// lands every frame whether a button is down or not. That is what a plan drawn
// on this grid has been leaving out: not "is there a wall" but "how long does
// this leg take", and on a belt the honest answer is one pixel a frame against
// and three with.
//
// **The two lookups do not use the same arithmetic, and that is why this is a
// separate predicate rather than a seventh field on `ProbeTile`.** The box is
// columns `(x-9)/8 .. +2` and rows `(y-8)/8 .. +1`; the floor tile is `x>>3,
// y>>3`, with no bias at all. Work the two through against each other and the
// floor tile is always *inside* the box and always the same one of the six: the
// middle of the lower row, `probe_tiles` index 4 -- except where x is a multiple
// of eight, where the box slides left and it becomes index 5. Cell centres are
// `8cx+13`, never a multiple of eight, so a waypoint's floor is index 4. Nothing
// here relies on that; it takes pixels and shifts them, the way the ROM does.
//
// The harm floors come back too. They do not move the player, so they cannot
// invalidate a plan the way a belt can, but a route that walks four hundred
// pixels across one is a route that arrives with no health, and until now
// nothing said so.
typedef struct {
  int dx, dy;      // the pixel the belt adds, per frame
  bool harm;       // ...or `$4000`/`$0400`, which take health instead
  uint16_t attr;   // the masked word that said so, for reporting
} RouteFloor;

static bool route_floor_at(const LevelWalk* lw, int px, int py, RouteFloor* out) {
  memset(out, 0, sizeof *out);
  if (px < 0 || py < 0) return false;
  const ProbeTile t = probe_tile(lw, px >> TILE_ATTRS_PIXEL_SHIFT,
                                 py >> TILE_ATTRS_PIXEL_SHIFT);
  if (!t.on_map || !t.known) return false;
  const uint16_t a = (uint16_t)(t.attr & FLOOR_ATTR_MASK);
  out->attr = a;
  switch (a) {
    case FLOOR_ATTR_BELT_UP: out->dy = -1; return true;
    case FLOOR_ATTR_BELT_DOWN: out->dy = +1; return true;
    case FLOOR_ATTR_BELT_LEFT: out->dx = -1; return true;
    case FLOOR_ATTR_BELT_RIGHT: out->dx = +1; return true;
    // `$4000` is gated on the player's weapon and `$1E`, and a static search
    // knows neither, so it is reported as harm and `attr` carries which of the
    // two words it was for a caller that wants to say. Being wrong about the
    // gated one costs a sentence in a report, not a route.
    case FLOOR_ATTR_HARM:
    case FLOOR_ATTR_HARM_GATED: out->harm = true; return true;
    default: out->attr = 0; return false;
  }
}

// The same, for a grid cell, asked at the pixel the search would put a waypoint
// on -- because a cell is eight pixels wide and the answer is a property of the
// pixel, not of the cell.
static bool route_floor(const LevelWalk* lw, int cx, int cy, RouteFloor* out) {
  return route_floor_at(lw, route_pixel_x(cx), route_pixel_y(cy), out);
}

// **A route to an object's own coordinate asks the wrong question**, and this is
// the answer to the right one.
//
// An object is picked up by *touching* it, not by standing on it, and the touch
// test is `actor_overlap_pass` (`$80:BEE1`, ported in `src/port/oam.c`): one
// subtraction per axis, re-biased by 8, taken unsigned. So a pair collides when
// each axis differs by -8..+7 -- and the sign there is the pair's order in the
// visible list, which a static search does not know, so the box this scans is
// the order-independent -7..+7 inside it.
//
// Why it matters: an object placed against a wall has scenery in its own 3x2
// box, so `route` refuses it, and the refusal has been read as the object being
// unreachable. Two of the three item-slot-1 objects in `docs/cosim.md` were
// written off exactly that way, and both are collectable. Level 25's at
// (128,694) needs no more than the route this prints: the walk lands at
// (125,700), three and six pixels off. Level 41's at (1590,123) is the case
// worth knowing about -- the topmost reachable row there spans y=128..135, the
// box wants y<=130, and a leg aimed at the row's centre stops at 133 and misses
// by three. `$80:AE14` calls (1590,129) open, so the route is not wrong, only
// its last leg, which is the wall-anchoring `docs/cosim.md` already owes.
//
// `prev` is the flood that already ran, so this costs 225 lookups and no second
// search.
static bool route_touch_point(const LevelWalk* lw, const int32_t* prev,
                              int x1, int y1, int* out_x, int* out_y) {
  int best = -1;
  for (int dy = -7; dy <= 7; dy++) {
    for (int dx = -7; dx <= 7; dx++) {
      const int px = x1 + dx, py = y1 + dy;
      const int cx = route_cell_x(px), cy = route_cell_y(py);
      if (cx < 0 || cy < 0 || (uint32_t)cx >= lw->cols || (uint32_t)cy >= lw->rows)
        continue;
      if (prev[(uint32_t)cy * lw->cols + (uint32_t)cx] == -2) continue;
      const int d = dx * dx + dy * dy;
      if (best >= 0 && d >= best) continue;
      best = d;
      *out_x = px;
      *out_y = py;
    }
  }
  return best >= 0;
}

// --- and the same level as the game's own routine wants it -------------------
//
// Everything above is an array lookup off `level_expand`. `terrain_blocked` --
// `src/port/terrain.c`, the port of `$80:AE14` that `zamn_cosim verify` diffs
// against the ROM on every call a movie makes -- reads a *WRAM* tilemap through
// the row table instead, so handing it one is what turns "the search thinks" into
// "the game says". `probe` reports both and compares them.

// Where the ROM's own loader puts the attribute table on level 1. Any free WRAM
// would do -- nothing else is in this one -- but using the game's address keeps
// a dump of it comparable with the real thing.
#define PROBE_ATTRS_AT 0x611au
#define PROBE_ATTRS_BANK 0x7eu

static Wram* level_walk_wram(const LevelWalk* lw) {
  const uint32_t bytes = lw->cols * lw->rows * 2;
  if (bytes > WRAM_SIZE - WRAM_BANK_7F) {
    fprintf(stderr, "error: a %u x %u tilemap is %u bytes and bank $7F holds %u\n",
            lw->cols, lw->rows, bytes, (uint32_t)(WRAM_SIZE - WRAM_BANK_7F));
    return NULL;
  }
  Wram* w = (Wram*)calloc(1, sizeof *w);
  if (!w) return NULL;
  const uint16_t stride = (uint16_t)(lw->cols * 2);
  wram_w16(w, W_TILEMAP_ROW_BYTES, stride);
  wram_w16(w, W_TILEMAP_ROWS, (uint16_t)lw->rows);
  wram_w16(w, W_TILE_ATTRS, PROBE_ATTRS_AT);
  wram_w8(w, W_TILE_ATTRS + 2, PROBE_ATTRS_BANK);
  for (uint32_t r = 0; r < lw->rows; r++)
    wram_w16(w, W_TILE_ROW_BASE + r * 2, (uint16_t)(r * stride));
  for (uint32_t i = 0; i < lw->cols * lw->rows; i++)
    wram_w16(w, WRAM_BANK_7F + i * 2, lw->map[i]);
  for (int i = 0; i < LEVEL_BG_TILES; i++)
    wram_w16(w, PROBE_ATTRS_AT + (uint32_t)i * 2, lw->attrs[i]);
  return w;
}

// The port's answer for a point, with this file's six tiles beside it. `first`
// receives the first probe the ROM's order would have rejected on, which is the
// one worth naming when something stops.
static bool probe_point(const LevelWalk* lw, Wram* w, int x, int y,
                        ProbeTile out[ROUTE_BOX_W * ROUTE_BOX_H], int* first) {
  probe_tiles(lw, route_cell_x(x), route_cell_y(y), out);
  *first = -1;
  for (int i = 0; i < ROUTE_BOX_W * ROUTE_BOX_H; i++) {
    if (out[i].on_map && out[i].known && !out[i].solid) continue;
    *first = i;
    break;
  }
  TerrainRegs r;
  terrain_blocked(w, (uint16_t)x, (uint16_t)y, &r);
  // The cross-check, and the reason the two are worth keeping separate: the
  // tiles above come from the search's own arithmetic and the verdict comes from
  // the game's. They are allowed to differ in exactly no cases, and the round
  // that added this is the round they did.
  const bool mine = *first >= 0;
  if (mine != r.blocked)
    printf("  ** (%d,%d): $80:AE14 says %s and this file's own box says %s.\n"
           "     The two have to agree; the one to believe is $80:AE14.\n",
           x, y, r.blocked ? "blocked" : "open", mine ? "blocked" : "open");
  return r.blocked;
}

// --- walking a printed route, without a movie --------------------------------
//
// A route that prints is not a route that walks, and until this the only way to
// find out which had been to build a movie and watch it. So the legs go through
// `terrain_blocked` here, two pixels a frame, from the caller's own start.
//
// **The perpendicular coordinate is left where the walk puts it**, not snapped
// to the next waypoint, because that is what the game does and it is the whole
// difficulty: these corridors are two tiles tall, a plan is walkable on the row
// it was planned for and on no other, and a leg that ends a pixel off-lane
// invalidates every leg after it. A check that snapped would pass routes the
// movie cannot walk, which is the check nobody needs.
//
// **And the floor moves too.** `floor_effect` runs at the top of `$80:D1FF`
// before the controller is read, so each frame here is the belt's pixel and then
// the walk's two, in that order. That is what makes this an answer about
// escalators rather than a plan with escalators left out of it: a leg walked
// against one covers a pixel a frame instead of two, a leg walked across one
// leaves the lane it was planned in, and both now show up in the landing rather
// than in a movie three rounds later.
//
// It always terminates. The leg's own axis moves by two, or by one or three
// where the belt is on that axis, and never by zero -- the only way a frame
// makes no progress along the leg is a wall, and a wall returns. Nor can a
// three-pixel frame step over the waypoint: the window the loop breaks on is
// `want-1 .. want+1`, three pixels wide, and a jump of three from outside it
// lands inside it whatever the residue.
typedef struct {
  int dx, dy;  // the direction held, one axis
  int tx, ty;  // ...and the waypoint it is held to
} RouteLeg;

// What the floor did, said once at the end rather than per frame. Silent when
// the walk never met one, which is most levels -- a report that prints "0
// conveyor frames" on every route is a report nobody reads.
//
// The two numbers worth having are separate on purpose. `belt_frames` is how
// long the player was on a belt and the displacement is where it put him, and
// they are not the same fact: a hundred frames on a leftward belt walked
// leftward is a hundred pixels of *help*, and a hundred across one is a hundred
// pixels out of the lane the rest of the plan was drawn in.
//
// `aborted` is the frame the walk stopped on. The floor runs first, so that
// frame's push has already happened and is already in the displacement -- but
// `frames` counts frames the plan *completed*, so it does not include it. The
// first draft printed both numbers against each other and could say "7 of those
// 6 frames", which is not a thing. The floor's denominator is the frames the
// floor ran on, and on a blocked walk that is one more.
static void route_walk_floor_report(const char* pre, int frames, int belt_frames,
                                    int belt_dx, int belt_dy, int belt_refused,
                                    int harm_frames, bool aborted) {
  const int ran = frames + (aborted ? 1 : 0);
  // On a belt and moved by it are not the same count -- a refused push is a
  // frame he stood on a conveyor and went nowhere -- so the frames are added up
  // and the displacement is reported separately rather than derived from them.
  if (belt_frames || belt_refused)
    printf("%sfloor: %d of those %d frames%s were on a conveyor, worth (%+d,%+d)"
           " pixels the plan did not ask for.\n",
           pre, belt_frames + belt_refused, ran,
           aborted ? " (the last of them the one the walk could not finish)" : "",
           belt_dx, belt_dy);
  if (belt_refused)
    printf("%s       On %d of them the rightward push met terrain and was"
           " refused, which is\n%s       the only direction `floor_effect` asks"
           " about before it moves him.\n",
           pre, belt_refused, pre);
  if (harm_frames)
    printf("%sfloor: %d frames on a harmful tile. `floor_effect` starts the harm"
           " once per\n%s       cooldown rather than once per frame, so that is"
           " an upper bound on the\n%s       hits and not a count of them.\n",
           pre, harm_frames, pre, pre);
}

// `pre` is what each line opens with, and in `--frames` mode it is `# ` -- that
// output is a .zmv to paste, and a report in the middle of one is a movie the
// reader rejects at line 1.
static void route_walk_check(const LevelWalk* lw, Wram* w, int x0, int y0,
                             const RouteLeg* legs, int nlegs, const char* pre) {
  int x = x0, y = y0, frames = 0;
  ProbeTile t[ROUTE_BOX_W * ROUTE_BOX_H];
  int first = -1;
  if (probe_point(lw, w, x, y, t, &first)) {
    printf("\n%swalk: (%d,%d) is not a place the player can stand.\n", pre, x, y);
    return;
  }
  int belt_frames = 0, belt_dx = 0, belt_dy = 0, belt_refused = 0, harm_frames = 0;
  for (int i = 0; i < nlegs; i++) {
    const int want = legs[i].dx ? legs[i].tx : legs[i].ty;
    for (;;) {
      const int at = legs[i].dx ? x : y;
      // One step short of the waypoint on an odd distance, which is a real
      // thing at two pixels a frame and not a rounding to hide.
      if (abs(want - at) < ROUTE_SPEED) break;
      // A frame, in the ROM's order: the floor, and then the walk. A belt
      // writes `$30` or `$32` outright in three of its four directions, so this
      // pushes into walls without a terrain test the way `floor_effect` does --
      // only the rightward one checks, and `port/floor.h` says in as many words
      // that the asymmetry is the ROM's and not worth rounding off.
      RouteFloor f;
      if (route_floor_at(lw, x, y, &f)) {
        if (f.harm) harm_frames++;
        if (f.dx > 0) {
          if (probe_point(lw, w, x + 1, y, t, &first)) {
            belt_refused++;
          } else {
            x += 1;
            belt_dx += 1;
            belt_frames++;
          }
        } else if (f.dx || f.dy) {
          x += f.dx;
          y += f.dy;
          belt_dx += f.dx;
          belt_dy += f.dy;
          belt_frames++;
        }
      }
      const int nx = x + legs[i].dx * ROUTE_SPEED, ny = y + legs[i].dy * ROUTE_SPEED;
      if (probe_point(lw, w, nx, ny, t, &first)) {
        // The waterline is not a wall, and after `--swim` this routine will meet
        // it on purpose. `terrain_blocked` cannot be taught to swim without
        // porting the crossing, so it stops here and says which kind of stop it
        // was -- the caller can walk this far and the rest is a swim.
        const bool at_water = first >= 0 && t[first].on_map && t[first].known &&
                              t[first].water;
        printf("\n%swalk: %s on leg %d at (%d,%d) after %d frames", pre,
               at_water ? "reached the waterline" : "blocked", i + 1, x, y,
               frames);
        if (first >= 0 && t[first].on_map && t[first].known)
          printf(" -- col %d row %d, tile $%03X, attribute $%04X", t[first].col,
                 t[first].row, t[first].index, t[first].attr);
        else if (first >= 0)
          printf(" -- col %d row %d, off the map", t[first].col, t[first].row);
        printf(".\n");
        // Said here too, and here it is the likelier explanation: a leg that
        // walks into a wall it was planned around is a leg that arrived in the
        // wrong lane, and a belt is one of the two things that move a player
        // sideways without asking.
        route_walk_floor_report(pre, frames, belt_frames, belt_dx, belt_dy,
                                belt_refused, harm_frames, true);
        return;
      }
      x = nx;
      y = ny;
      frames++;
    }
  }
  printf("\n%swalk: %d frames from (%d,%d) to (%d,%d)%s.\n", pre, frames, x0, y0, x, y,
         nlegs && (x != legs[nlegs - 1].tx || y != legs[nlegs - 1].ty)
             ? ", which is not the last waypoint -- two pixels a frame cannot"
               " land on an odd distance, and the lane is whatever the start was"
             : "");
  route_walk_floor_report(pre, frames, belt_frames, belt_dx, belt_dy,
                          belt_refused, harm_frames, false);
}

// --- and ending the last leg where the caller asked, not where the grid did --
//
// Every waypoint above is a cell *centre*, because the centre is the only pixel
// the search ever asks about, and for all but the last leg that is right: the
// centre is what the next leg turns from. The last leg is the one the caller
// cares about the position of, and there the centre is an answer to nobody's
// question -- a goal is a pixel, and the cell holding it is eight wide.
//
// Level 41's item is the case that cost a round. The topmost standable row
// there spans y=128..135, its centre is 132, `actor_overlap_pass` wants y<=130,
// and the walk stopped at 133 and missed the object by three pixels -- with
// `probe` calling 129 open the whole time. So the last leg is pushed along its
// own axis toward the caller's coordinate, one pixel at a time, for as long as
// `$80:AE14` calls the player's box open. The game's answer, not this file's.
//
// It only ever pushes *forward*: never back past the waypoint the search
// proved, never beyond the goal. A route that already ended on its goal is the
// route it always was, which is why the levels documented in
// `docs/analysis-tools.md` print what they printed.
//
// The perpendicular axis is left alone and has to be. One leg moves on one
// axis, and the lane it moves in is whatever the legs before it left; closing
// that residual means anchoring the whole search on walls rather than on cell
// counts, which is the debt `docs/cosim.md` names for every routed movie.
// `route_walk_check` prints the landing, and the landing is the thing to
// believe.
static void route_anchor_last(const LevelWalk* lw, Wram* w, RouteLeg* legs,
                              int nlegs, int x1, int y1) {
  if (nlegs <= 0) return;
  RouteLeg* leg = &legs[nlegs - 1];
  const int step = leg->dx ? leg->dx : leg->dy;
  const int want = leg->dx ? x1 : y1;
  int at = leg->dx ? leg->tx : leg->ty;
  if (!step || (want - at) * step <= 0) return;
  ProbeTile t[ROUTE_BOX_W * ROUTE_BOX_H];
  int first = -1;
  for (int p = at + step; (want - p) * step >= 0; p += step) {
    if (probe_point(lw, w, leg->dx ? p : leg->tx, leg->dx ? leg->ty : p, t, &first))
      break;
    at = p;
  }
  if (leg->dx)
    leg->tx = at;
  else
    leg->ty = at;
}

// Breadth-first from (sx,sy) to exhaustion rather than stopped at the goal.
// Every `prev` is set once and in the same order either way, so the path printed
// is the path that was always printed; what the extra cells buy is `--reach`,
// and twelve thousand of them cost nothing.
//
// `prev` must arrive filled with -2. Run twice per invocation when the dry grid
// fails, which is what lets the failure say whether water was the thing in the
// way -- so it takes the predicate as a flag rather than hard-coding one.
static void route_flood(const LevelWalk* lw, int32_t* prev, int32_t* queue,
                        int sx, int sy, bool swim) {
  const uint32_t cols = lw->cols, rows = lw->rows;
  int32_t head = 0, tail = 0;
  prev[(uint32_t)sy * cols + (uint32_t)sx] = -1;
  queue[tail++] = (int32_t)((uint32_t)sy * cols + (uint32_t)sx);
  const int dxs[4] = {1, -1, 0, 0}, dys[4] = {0, 0, 1, -1};
  while (head < tail) {
    int32_t cur = queue[head++];
    int cx = (int)((uint32_t)cur % cols), cy = (int)((uint32_t)cur / cols);
    for (int k = 0; k < 4; k++) {
      int nx = cx + dxs[k], ny = cy + dys[k];
      if (nx < 0 || ny < 0 || (uint32_t)nx >= cols || (uint32_t)ny >= rows) continue;
      uint32_t ni = (uint32_t)ny * cols + (uint32_t)nx;
      if (prev[ni] != -2) continue;
      if (!(swim ? route_swimmable(lw, nx, ny) : route_open(lw, nx, ny))) continue;
      prev[ni] = cur;
      queue[tail++] = (int32_t)ni;
    }
  }
}

static int cmd_route(int argc, char** argv) {
  if (argc < 6) {
    fprintf(stderr,
            "usage: zamn_assets route <rom.sfc> <level 1-56> <x0> <y0> <x1> <y1>"
            " [--frames <start>] [--reach <out.png>] [--swim]\n");
    return 2;
  }
  int rom_len = 0;
  uint8_t* rom_data = read_file(argv[0], &rom_len);
  if (!rom_data) return 1;
  Rom rom = {rom_data, (uint32_t)rom_len};
  int level = atoi(argv[1]);
  int x0 = atoi(argv[2]), y0 = atoi(argv[3]);
  int x1 = atoi(argv[4]), y1 = atoi(argv[5]);
  int frame0 = -1;
  const char* reach_path = NULL;
  bool swim = false;
  for (int i = 6; i < argc; i++) {
    if (!strcmp(argv[i], "--swim")) swim = true;
    if (i + 1 >= argc) continue;
    if (!strcmp(argv[i], "--frames")) frame0 = atoi(argv[i + 1]);
    if (!strcmp(argv[i], "--reach")) reach_path = argv[i + 1];
  }

  LevelWalk lw;
  if (!level_walk_load(&rom, level, &lw)) {
    free(rom_data);
    return 1;
  }
  const LevelHeader h = lw.h;
  const uint32_t cols = lw.cols, rows = lw.rows;
  uint32_t cells = cols * rows;
  int32_t* prev = (int32_t*)malloc(cells * sizeof(int32_t));
  int32_t* queue = (int32_t*)malloc(cells * sizeof(int32_t));
  if (!prev || !queue) { free(prev); free(queue); level_walk_free(&lw); free(rom_data); return 1; }
  for (uint32_t i = 0; i < cells; i++) prev[i] = -2;

  int sx = route_cell_x(x0), sy = route_cell_y(y0);
  int gx = route_cell_x(x1), gy = route_cell_y(y1);
  // Both ends have to be *on the map*, and this is not a formality: level 33's
  // actor list places one at (1260,1737) on a level 1280 pixels tall, and
  // without this the goal index ran off the end of `prev` and the search
  // answered "2 cells" out of whatever was past it. An off-map coordinate is a
  // real thing to find in this ROM, so the search has to say so rather than
  // read garbage.
  if (sx < 0 || sy < 0 || (uint32_t)sx >= cols || (uint32_t)sy >= rows ||
      gx < 0 || gy < 0 || (uint32_t)gx >= cols || (uint32_t)gy >= rows) {
    printf("(%d,%d) or (%d,%d) is off level %d's %u x %u map "
           "(%u x %u pixels).\n",
           x0, y0, x1, y1, level, cols, rows, level_width_px(&h),
           level_height_px(&h));
    free(prev); free(queue); level_walk_free(&lw); free(rom_data);
    return 1;
  }
  route_flood(&lw, prev, queue, sx, sy, swim);

  if (reach_path) {
    uint8_t* reach = (uint8_t*)calloc(cells, 1);
    if (reach) {
      for (uint32_t i = 0; i < cells; i++) {
        int cx = (int)(i % cols), cy = (int)(i / cols);
        // Reached-and-wet gets its own colour rather than collapsing into
        // green, because in `--swim` the interesting thing about a route is
        // exactly which part of it the player is swimming.
        const bool wet = route_water(&lw, cx, cy);
        RouteFloor f;
        const bool belt = route_floor(&lw, cx, cy, &f) && (f.dx || f.dy);
        const bool open = route_open(&lw, cx, cy);
        // White overrides reached, but it must not override *solid*. A level
        // places belt tiles under its scenery as freely as any other tile, and
        // the first draft of this let those paint white over the red -- so the
        // picture showed a conveyor at (957,180) on record 11 that `probe` calls
        // blocked. The census below still counts every placed cell, because that
        // is the number the tile tables can be checked against; the picture
        // shows only the ones that are a floor to somebody.
        reach[i] = belt && open ? 7
                   : prev[i] != -2 ? (wet ? 6 : 1)
                   : open ? 4
                   : wet  ? 5
                          : 0;
      }
      // Split by direction, because they are not one hazard. A belt along a
      // corridor is a moving walkway and costs a plan its frame counts; one
      // across it is an escalator and costs the plan its lane.
      // ...and with the box each direction lives in, in pixels, because "there
      // is an escalator on this level" is not actionable and "the up one is the
      // eight pixels at x=944..951" is. One box per direction is enough for the
      // levels that have two of them facing each other; the ones that carpet a
      // whole floor in belts are legible from the picture instead.
      uint32_t belts = 0, standable = 0, n[4] = {0, 0, 0, 0}, s[4] = {0, 0, 0, 0};
      int lo_x[4], hi_x[4], lo_y[4], hi_y[4];
      for (int k = 0; k < 4; k++) { lo_x[k] = lo_y[k] = 1 << 30; hi_x[k] = hi_y[k] = -1; }
      for (uint32_t i = 0; i < cells; i++) {
        const int cx = (int)(i % cols), cy = (int)(i / cols);
        RouteFloor f;
        if (!route_floor(&lw, cx, cy, &f) || (!f.dx && !f.dy)) continue;
        const int k = f.dy < 0 ? 0 : f.dy > 0 ? 1 : f.dx < 0 ? 2 : 3;
        belts++;
        n[k]++;
        if (reach[i] == 7) { standable++; s[k]++; }
        const int px = route_pixel_x(cx), py = route_pixel_y(cy);
        if (px < lo_x[k]) lo_x[k] = px;
        if (px > hi_x[k]) hi_x[k] = px;
        if (py < lo_y[k]) lo_y[k] = py;
        if (py > hi_y[k]) hi_y[k] = py;
      }
      if (belts) {
        static const char* dirname[4] = {"up", "down", "left", "right"};
        printf("%u of level %d's %u cells are conveyor; %u of those are a floor\n"
               "the player can stand on, and only those are drawn in white.\n",
               belts, level, cells, standable);
        for (int k = 0; k < 4; k++)
          if (n[k])
            printf("  %-5s %5u cells, %5u standable, x %d..%d, y %d..%d\n",
                   dirname[k], n[k], s[k], lo_x[k], hi_x[k], lo_y[k], hi_y[k]);
      }
      reach[(uint32_t)sy * cols + (uint32_t)sx] = 2;
      if (gy >= 0 && (uint32_t)gy < rows && gx >= 0 && (uint32_t)gx < cols)
        reach[(uint32_t)gy * cols + (uint32_t)gx] = 3;
      render_level(&h, lw.map, &rom, reach_path, reach);
      free(reach);
    }
  }

  int rc = 0;
  if (prev[(uint32_t)gy * cols + (uint32_t)gx] == -2) {
    printf("no %sroute from (%d,%d) to (%d,%d) through level %d.\n",
           swim ? "" : "dry ", x0, y0, x1, y1, level);
    printf("the 2x2-clear grid does not connect them, so either the target is\n");
    printf("inside scenery or the way in is a door rather than a gap.\n");
    // Before blaming the level, check whether the *goal* was the mistake. If
    // anything the flood reached is inside the game's own touch box, an object
    // sitting here is collectable and only the coordinate was unwalkable -- so
    // say so, and hand back a target that works.
    int tx = 0, ty = 0;
    const bool touchable = route_touch_point(&lw, prev, x1, y1, &tx, &ty);
    if (touchable)
      printf("\nbut (%d,%d) is reached, and the two are within the 16x16 box\n"
             "`actor_overlap_pass` tests -- so an *object* at (%d,%d) is\n"
             "collectable from there. Route to (%d,%d): the last leg is walked\n"
             "onto that coordinate rather than onto its cell's centre, but the\n"
             "axis that leg does not move on is whatever lane the legs before\n"
             "it left, so the landing is the thing to believe, not the plan.\n",
             tx, ty, x1, y1, tx, ty);
    // ...or it is neither, and the answer is not to be trusted. A cell that the
    // flood reached and that has water next to it is a shore, and the player
    // swims off shores. Count them -- and then, rather than leave the caller
    // with a warning and no way to act on it, *run the wet grid and say*. "No
    // route" is the one thing this tool is asked to be believed about, and the
    // difference between "nowhere to go" and "nowhere to go on foot" is the
    // whole answer.
    uint32_t shore = 0;
    for (uint32_t i = 0; i < cells; i++) {
      if (prev[i] == -2) continue;
      int cx = (int)(i % cols), cy = (int)(i / cols);
      const int dxs4[4] = {1, -1, 0, 0}, dys4[4] = {0, 0, 1, -1};
      for (int k = 0; k < 4; k++) {
        int nx = cx + dxs4[k], ny = cy + dys4[k];
        if (nx < 0 || ny < 0 || (uint32_t)nx >= cols || (uint32_t)ny >= rows) continue;
        if (route_water(&lw, nx, ny)) { shore++; break; }
      }
    }
    // Both, when both are true. They answer different questions -- one is "an
    // object here is collectable from dry land", the other "the player can get
    // *to* here" -- and a caller who wanted the second is not served by being
    // told only the first. Level 22 is the case that has both.
    if (shore && !swim) {
      printf("\n%u reachable cells are on the edge of water", shore);
      int32_t* wet = (int32_t*)malloc(cells * sizeof(int32_t));
      if (wet) {
        for (uint32_t i = 0; i < cells; i++) wet[i] = -2;
        route_flood(&lw, wet, queue, sx, sy, true);
        if (wet[(uint32_t)gy * cols + (uint32_t)gx] != -2)
          printf(", and swimming connects them:\nrerun with --swim for the route."
                 " The legs it prints are real, but its\nframe counts are not --"
                 " see `route_swimmable` for what a crossing\nactually costs.\n");
        else
          printf(", but swimming does not reach\n(%d,%d) either -- so the water"
                 " is not what is in the way here.\n", x1, y1);
        free(wet);
      } else {
        printf(".\n");
      }
    }
    rc = 1;
  } else {
    // Walk the chain back, then collapse it into axis-aligned runs -- which is
    // what a movie line is: one direction held until the next turn.
    int32_t* path = (int32_t*)malloc(cells * sizeof(int32_t));
    int n = 0;
    for (int32_t at = (int32_t)((uint32_t)gy * cols + (uint32_t)gx); at >= 0; at = prev[at])
      path[n++] = at;
    printf("level %d: (%d,%d) -> (%d,%d), %d cells\n", level, x0, y0, x1, y1, n);
    if (swim) {
      uint32_t wetcells = 0;
      for (int i = 0; i < n; i++)
        if (route_water(&lw, (int)((uint32_t)path[i] % cols),
                        (int)((uint32_t)path[i] / cols)))
          wetcells++;
      if (wetcells)
        printf("%u of them are in water. The legs are real and the frame counts\n"
               "are not: a crossing stalls sixteen frames going in and thirteen\n"
               "coming out, and runs at eight pixels per nine frames, then three\n"
               "per two, then one per two climbing out -- never %d.\n",
               wetcells, ROUTE_SPEED);
    }
    // Said before the legs rather than after them, because it changes what the
    // reader should do with the legs. The walk below reports what the belt
    // actually cost; this reports that there is one, which is the part worth
    // knowing before pasting a route into a movie.
    uint32_t beltcells = 0;
    for (int i = 0; i < n; i++) {
      RouteFloor f;
      if (route_floor(&lw, (int)((uint32_t)path[i] % cols),
                      (int)((uint32_t)path[i] / cols), &f) &&
          (f.dx || f.dy))
        beltcells++;
    }
    if (beltcells)
      printf("%u of them are conveyor. `floor_effect` moves the player one pixel a\n"
             "frame there, before the controller is read, so a leg along one runs at\n"
             "three pixels a frame and a leg against it at one -- and a leg *across*\n"
             "one ends in a different lane from the one it was planned in. The\n"
             "walk below is where the frame count is; the pixel counts are not it.\n",
             beltcells);
    printf("\n");
    // Collected before anything is printed, because the last one is still going
    // to move: `route_anchor_last` needs the whole plan and the game's own
    // terrain test before the first line of it is true. At most one per cell.
    RouteLeg* legs = (RouteLeg*)malloc((size_t)n * sizeof(RouteLeg));
    int nlegs = 0;
    for (int i = n - 1; i > 0;) {
      int cx = (int)((uint32_t)path[i] % cols), cy = (int)((uint32_t)path[i] / cols);
      int ddx = (int)((uint32_t)path[i - 1] % cols) - cx;
      int ddy = (int)((uint32_t)path[i - 1] / cols) - cy;
      int j = i;
      while (j > 0) {
        int ax = (int)((uint32_t)path[j] % cols), ay = (int)((uint32_t)path[j] / cols);
        if ((int)((uint32_t)path[j - 1] % cols) - ax != ddx) break;
        if ((int)((uint32_t)path[j - 1] / cols) - ay != ddy) break;
        j--;
      }
      if (legs) {
        legs[nlegs].dx = ddx > 0 ? 1 : ddx < 0 ? -1 : 0;
        legs[nlegs].dy = ddy > 0 ? 1 : ddy < 0 ? -1 : 0;
        legs[nlegs].tx = route_pixel_x((int)((uint32_t)path[j] % cols));
        legs[nlegs].ty = route_pixel_y((int)((uint32_t)path[j] / cols));
        nlegs++;
      }
      i = j;
    }
    // The plan, walked. Terrain only: this knows nothing about the objects,
    // actors and tether the other three of `$80:E4C1`'s tests ask about, and a
    // creature standing in a corridor will stop a movie that passes here.
    Wram* w = legs ? level_walk_wram(&lw) : NULL;
    if (w) route_anchor_last(&lw, w, legs, nlegs, x1, y1);
    int frame = frame0;
    int px = x0, py = y0;
    for (int i = 0; i < nlegs; i++) {
      const char* dir = legs[i].dx > 0   ? "Right"
                        : legs[i].dx < 0 ? "Left"
                        : legs[i].dy > 0 ? "Down"
                                         : "Up";
      const int dist = legs[i].dx ? abs(legs[i].tx - px) : abs(legs[i].ty - py);
      if (frame0 >= 0) {
        printf("%d   %s\n", frame, dir);
        // Two pixels a frame and six frames of slack. That is a *first draft*
        // and it will drift: walking is two pixels a frame except when it is
        // being pushed, slowed, or snapped to a lane, and the error compounds
        // leg by leg. `tools/fit_route.py` replays the movie and moves each
        // turn to the frame the previous leg actually finished on, which is
        // the closed loop this open one needs.
        frame += dist / ROUTE_SPEED + 6;
      } else {
        printf("  %-5s to (%d,%d)   %d px\n", dir, legs[i].tx, legs[i].ty, dist);
      }
      px = legs[i].tx; py = legs[i].ty;
    }
    if (frame0 >= 0) printf("%d   -\n", frame);
    if (w) {
      route_walk_check(&lw, w, x0, y0, legs, nlegs, frame0 >= 0 ? "# " : "  ");
      free(w);
    }
    free(legs);
    free(path);
  }
  free(prev); free(queue); level_walk_free(&lw); free(rom_data);
  return rc;
}

// ---------------------------------------------------------------------------
// probe
// ---------------------------------------------------------------------------
//
// `route` prints a path, a movie either walks it or does not, and when it does
// not there has been nothing to ask *why*. Level 29's leg 5 spent a round on
// that: the player stood on the planned row to the pixel, held `Up`, moved two
// pixels and stopped, where the search said the way was open for 248 more.
//
// So this asks the game instead of the search. The verdict comes from
// `terrain_blocked` -- `src/port/terrain.c`, the port of `$80:AE14`, which
// `zamn_cosim verify` diffs against the ROM on every call a movie makes -- run
// against a WRAM built the way the level loader leaves it: the expanded tilemap
// in bank `$7F`, one row-base word per row, and `$BA` pointing at the 512
// attribute words. The six tiles printed beside the verdict are this file's own
// arithmetic, and the two are compared on every probe. **If they disagree the
// disagreement is the finding**, and it is printed as one rather than swallowed
// -- which is exactly what happened the first time this was run, because the
// search's box was a column out.
//
// `--to` is the other half of it. Holding a direction is two pixels a frame
// through the same test, so a leg can be walked here without a movie, and "the
// player moves two pixels and stops" becomes a column, a tile and an attribute
// word.

static void probe_print_tiles(const ProbeTile t[ROUTE_BOX_W * ROUTE_BOX_H]) {
  printf("  col  row  entry  tile  attr\n");
  for (int i = 0; i < ROUTE_BOX_W * ROUTE_BOX_H; i++) {
    if (!t[i].on_map) {
      printf("  %3d  %3d  --     --    --     off the map\n", t[i].col, t[i].row);
      continue;
    }
    if (!t[i].known) {
      printf("  %3d  %3d  $%04X  $%03X  --     past the 512-word table\n",
             t[i].col, t[i].row, t[i].entry, t[i].index);
      continue;
    }
    printf("  %3d  %3d  $%04X  $%03X  $%04X%s%s\n", t[i].col, t[i].row,
           t[i].entry, t[i].index, t[i].attr, t[i].solid ? "  solid" : "",
           t[i].water ? "  water" : "");
  }
}

// Every cell of a level, both ways round: what the search believes about the
// cell against what `$80:AE14` says about a player standing in the middle of it.
// The two are a *model* and the thing modelled, and the mapping between them --
// which pixel is which cell -- is the part that was wrong for a dozen rounds and
// that nothing else checks.
//
// The edge is the honest exception. Off the map the search says blocked, while
// the game reads whatever the tilemap holds past the end of the row -- the next
// row's left-hand tiles -- and past the last row it indexes `W_TILE_ROW_BASE`
// past the end of the row table, which is a WRAM question no static reading can
// answer. So a disagreement is counted, and separated into edge and interior:
// **the interior number is the one that has to be zero.**
static int probe_sweep(const LevelWalk* lw, Wram* w, int level) {
  uint32_t open = 0, edge = 0, interior = 0;
  for (uint32_t cy = 0; cy < lw->rows; cy++) {
    for (uint32_t cx = 0; cx < lw->cols; cx++) {
      const bool mine = route_open(lw, (int)cx, (int)cy);
      TerrainRegs r;
      terrain_blocked(w, (uint16_t)route_pixel_x((int)cx),
                      (uint16_t)route_pixel_y((int)cy), &r);
      if (mine) open++;
      if (mine == !r.blocked) continue;
      const bool at_edge = cx + ROUTE_BOX_W > lw->cols || cy + ROUTE_BOX_H > lw->rows;
      if (at_edge) {
        edge++;
        continue;
      }
      if (interior++ < 8)
        printf("  ** cell (%u,%u) -> (%d,%d): the search says %s, $80:AE14 says"
               " %s\n",
               cx, cy, route_pixel_x((int)cx), route_pixel_y((int)cy),
               mine ? "open" : "blocked", r.blocked ? "blocked" : "open");
    }
  }
  printf("level %2d: %6u cells, %6u open, %4u disagree at the edge, %u inside\n",
         level, lw->cols * lw->rows, open, edge, interior);
  return interior == 0 ? 0 : 1;
}

static int cmd_probe(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr,
            "usage: zamn_assets probe <rom.sfc> <level 1-56> <x> <y>"
            " [--to <x> <y>]\n"
            "       zamn_assets probe <rom.sfc> <level 1-56> --sweep\n");
    return 2;
  }
  int rom_len = 0;
  uint8_t* rom_data = read_file(argv[0], &rom_len);
  if (!rom_data) return 1;
  Rom rom = {rom_data, (uint32_t)rom_len};
  const int level = atoi(argv[1]);
  const bool sweep = !strcmp(argv[2], "--sweep");
  if (!sweep && argc < 4) {
    fprintf(stderr, "error: probe wants <x> <y>, or --sweep for the whole map\n");
    free(rom_data);
    return 2;
  }
  const int x0 = sweep ? 0 : atoi(argv[2]), y0 = sweep ? 0 : atoi(argv[3]);
  int x1 = x0, y1 = y0;
  bool have_to = false;
  for (int i = 4; i + 2 < argc; i++) {
    if (!strcmp(argv[i], "--to")) {
      x1 = atoi(argv[i + 1]);
      y1 = atoi(argv[i + 2]);
      have_to = true;
    }
  }
  if (have_to && x1 != x0 && y1 != y0) {
    fprintf(stderr, "error: --to walks one axis at a time, as a held direction"
                    " does\n");
    free(rom_data);
    return 2;
  }

  LevelWalk lw;
  if (!level_walk_load(&rom, level, &lw)) {
    free(rom_data);
    return 1;
  }
  Wram* w = level_walk_wram(&lw);
  if (!w) {
    level_walk_free(&lw);
    free(rom_data);
    return 1;
  }

  // The tenth bit, counted rather than assumed: `$80:AE47` masks a tilemap
  // entry to ten bits and the table it indexes has 512 words, so an entry with
  // that bit set would send the game past the end of it.
  uint32_t past = 0;
  for (uint32_t i = 0; i < lw.cols * lw.rows; i++)
    if ((lw.map[i] & ROUTE_TILE_MASK) >= LEVEL_BG_TILES) past++;

  if (sweep) {
    const int rc = probe_sweep(&lw, w, level);
    if (past)
      printf("  ...and %u of %u entries index past the attribute table\n", past,
             lw.cols * lw.rows);
    free(w);
    level_walk_free(&lw);
    free(rom_data);
    return rc;
  }

  printf("level %d: %u x %u tiles, %u x %u pixels; %u of %u entries index past"
         " the attribute table\n\n",
         level, lw.cols, lw.rows, level_width_px(&lw.h), level_height_px(&lw.h),
         past, lw.cols * lw.rows);

  ProbeTile t[ROUTE_BOX_W * ROUTE_BOX_H];
  int first = -1;
  const bool blocked = probe_point(&lw, w, x0, y0, t, &first);
  printf("(%d,%d): cols %d..%d, rows %d..%d\n\n", x0, y0, route_cell_x(x0),
         route_cell_x(x0) + ROUTE_BOX_W - 1, route_cell_y(y0),
         route_cell_y(y0) + ROUTE_BOX_H - 1);
  probe_print_tiles(t);
  printf("\n  $80:AE14: %s\n", blocked ? "blocked" : "open");

  int rc = 0;
  if (have_to) {
    const int dx = x1 > x0 ? 1 : x1 < x0 ? -1 : 0;
    const int dy = y1 > y0 ? 1 : y1 < y0 ? -1 : 0;
    const char* dir = dx > 0 ? "Right" : dx < 0 ? "Left" : dy > 0 ? "Down" : "Up";
    const int want = abs(x1 - x0) + abs(y1 - y0);
    printf("\n%s from (%d,%d) to (%d,%d), %d px at %d a frame:\n\n", dir, x0, y0,
           x1, y1, want, ROUTE_SPEED);
    int x = x0, y = y0, frames = 0;
    for (;;) {
      const int nx = x + dx * ROUTE_SPEED, ny = y + dy * ROUTE_SPEED;
      if (abs(nx - x0) > want || abs(ny - y0) > want) break;
      if (probe_point(&lw, w, nx, ny, t, &first)) {
        printf("  stopped at (%d,%d) after %d frame%s, %d px short.\n", x, y,
               frames, frames == 1 ? "" : "s", want - abs(x - x0) - abs(y - y0));
        if (first >= 0)
          printf("  (%d,%d) is blocked by col %d row %d", nx, ny, t[first].col,
                 t[first].row);
        if (first >= 0 && t[first].on_map && t[first].known)
          printf(", tile $%03X, attribute $%04X", t[first].index, t[first].attr);
        printf(".\n");
        rc = 1;
        break;
      }
      x = nx;
      y = ny;
      frames++;
      if (x == x1 && y == y1) break;
    }
    // A leg that ends between two frames is a leg the walk cannot land on
    // exactly: two pixels a frame divides an odd distance into a hold that
    // stops one pixel short, which is worth saying rather than rounding away.
    if (rc == 0)
      printf("  walked it: (%d,%d) after %d frames%s.\n", x, y, frames,
             x == x1 && y == y1 ? "" : ", one pixel short of an odd distance");
  }

  free(w);
  level_walk_free(&lw);
  free(rom_data);
  return rc;
}

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
          "  zamn_assets password <rom.sfc> [<4 letters>]\n"
          "      Spell every password out of the ROM's own tables, or read one\n"
          "      back to the level and victim count it means.\n\n"
          "  zamn_assets route <rom.sfc> <level> <x0> <y0> <x1> <y1> [--frames f]\n"
          "                    [--reach out.png] [--swim]\n"
          "      Breadth-first a walkable path through a level and print the\n"
          "      turns, or .zmv lines with --frames. --swim lets the flood cross\n"
          "      water, which the player does and walking routes do not; the\n"
          "      legs are then real but the frame counts are not. --reach tints\n"
          "      the map: green reached, orange open but cut off, red solid,\n"
          "      magenta water, pink water the route swims, white a conveyor\n"
          "      the player can stand on, yellow the start, blue the goal.\n"
          "      Orange says where a missing route wants a door.\n\n"
          "  zamn_assets probe <rom.sfc> <level> <x> <y> [--to <x> <y>]\n"
          "      Ask $80:AE14 itself whether the player can stand at a point:\n"
          "      the six tiles it reads, their attributes, and the verdict out\n"
          "      of the port the cosim harness diffs against the ROM. --to\n"
          "      walks a straight leg two pixels a frame and reports the tile\n"
          "      that stops it, which is what a route that fails wants asked.\n\n"
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
  if (!strcmp(cmd, "route")) return cmd_route(argc - 2, argv + 2);
  if (!strcmp(cmd, "probe")) return cmd_probe(argc - 2, argv + 2);
  if (!strcmp(cmd, "verify-actors")) return cmd_verify_actors(argc - 2, argv + 2);
  if (!strcmp(cmd, "sprite")) return cmd_sprite(argc - 2, argv + 2);
  if (!strcmp(cmd, "frame")) return cmd_frame(argc - 2, argv + 2);
  if (!strcmp(cmd, "verify-sprites")) return cmd_verify_sprites(argc - 2, argv + 2);
  if (!strcmp(cmd, "music")) return cmd_music(argc - 2, argv + 2);
  if (!strcmp(cmd, "password")) return cmd_password(argc - 2, argv + 2);
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
