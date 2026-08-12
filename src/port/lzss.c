#include "port/lzss.h"

#include <string.h>

#include "port/coverage.h"

// --- the bus, as much of it as this routine touches -------------------------
//
// Source is `[$28]` and destination is `[$2C]`, both 24-bit. In practice the
// source is ROM and the destination is WRAM, but the routine does not know
// that and neither does this: it dispatches on the bank the same way the
// cartridge mapping does.

static bool addr_is_wram(uint32_t addr24, uint32_t* off) {
  uint32_t bank = (addr24 >> 16) & 0xffu;
  uint32_t addr = addr24 & 0xffffu;
  if (bank == 0x7e || bank == 0x7f) {
    *off = ((bank - 0x7e) << 16) | addr;
    return true;
  }
  // The low 8 KB of banks $00-$3F and $80-$BF mirror WRAM $0000-$1FFF.
  if (addr < 0x2000u && ((bank & 0x7fu) <= 0x3fu)) {
    *off = addr;
    return true;
  }
  return false;
}

// Is a byte at this address one of the ones `$420D` makes cheap? Not a question
// about correctness at all — it decides 4 master cycles on every `LDA [$28]`,
// and there are hundreds of thousands of those in one call. `snes_getAccessTime`
// is the authority and `tools/cycles816.py`'s `fast_rom` is the same predicate
// written for the pricing tool; this is the third copy and the only one inside
// the port, which is why it is here beside `addr_is_wram` rather than folded
// into it. A SlowROM mirror below bank $80 answers false and costs what WRAM
// costs, which is the whole reason the question is about the *price* and not
// about the cartridge.
static bool addr_is_fastrom(uint32_t addr24) {
  uint32_t bank = (addr24 >> 16) & 0xffu;
  uint32_t addr = addr24 & 0xffffu;
  if ((bank < 0x40u || (bank >= 0x80u && bank < 0xc0u)) && addr < 0x8000u)
    return false;
  return bank >= 0x80u;
}

static uint8_t bus_r8(const Wram* w, const Rom* rom, uint32_t addr24) {
  uint32_t off;
  if (addr_is_wram(addr24, &off)) return wram_r8(w, off);
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, addr24, &avail);
  return (p && avail >= 1) ? p[0] : 0;
}

static void bus_w8(Wram* w, uint32_t addr24, uint8_t v) {
  uint32_t off;
  if (addr_is_wram(addr24, &off)) wram_w8(w, off, v);
}

// --- the routine ------------------------------------------------------------

// `$80:CDDA`. Returns false when `$38` is exhausted, which is the ROM's `SEC`
// and the normal end of a stream — there is no end marker.
//
// `k` is the caller's tally or null. Only the reads that *return* a byte reach
// `LDA [$28]`, so only those can be fast or slow; the spent path is one price.
static bool read_byte(Wram* w, const Rom* rom, uint8_t* out, LzssWork* k) {
  uint16_t remain = wram_r16(w, LZSS_DP_REMAIN);
  if (remain == 0) return false;
  wram_w16(w, LZSS_DP_REMAIN, (uint16_t)(remain - 1));
  uint16_t addr = wram_r16(w, LZSS_DP_SRC);
  uint8_t bank = wram_r8(w, LZSS_DP_SRC_BANK);
  if (k && addr_is_fastrom(((uint32_t)bank << 16) | addr)) k->reads_fast++;
  *out = bus_r8(w, rom, ((uint32_t)bank << 16) | addr);
  // `INC $28` is a 16-bit increment of the address word alone: a source that
  // runs off the top of its bank wraps inside it rather than carrying.
  wram_w16(w, LZSS_DP_SRC, (uint16_t)(addr + 1));
  return true;
}

// `$80:CDEB`. Note the same lack of a bank carry on the way out.
static void write_byte(Wram* w, uint8_t v) {
  uint16_t addr = wram_r16(w, LZSS_DP_DST);
  uint8_t bank = wram_r8(w, LZSS_DP_DST_BANK);
  bus_w8(w, ((uint32_t)bank << 16) | addr, v);
  wram_w16(w, LZSS_DP_DST, (uint16_t)(addr + 1));
}

// --- the same two, as registry entries --------------------------------------
//
// Thin wrappers rather than a second copy: the body above is what the
// decompressor calls, so registering these checks the code that actually runs
// and not a paraphrase of it. All they add is what the ROM leaves in the
// registers, which the internal callers have no use for. See `port/lzss.h`.

void lzss_read_byte_regs(Wram* w, const Rom* rom, LzssReadRegs* out) {
  uint8_t byte = 0;
  if (!read_byte(w, rom, &byte, NULL)) {
    // `LDA $38 : BEQ : SEC : RTS` — A holds the zero the branch tested.
    PORT_COVER(lzss_read_spent);
    out->a = 0;
    out->n = false;
    out->z = true;
    out->spent = true;
    return;
  }
  // `AND #$00FF` puts the byte in A, and then `INC $28` overwrites N and Z with
  // the pointer it just advanced. Read after the call, because that is the
  // increment the ROM's flags describe.
  uint16_t src = wram_r16(w, LZSS_DP_SRC);
  out->a = byte;
  out->n = (src & 0x8000u) != 0;
  out->z = src == 0;
  out->spent = false;
}

void lzss_write_byte_regs(Wram* w, uint16_t a, LzssWriteRegs* out) {
  write_byte(w, (uint8_t)a);
  // `STA [$2C]` is 8-bit and never touches A's high half, so A comes back whole.
  uint16_t dst = wram_r16(w, LZSS_DP_DST);
  out->a = a;
  out->n = (dst & 0x8000u) != 0;
  out->z = dst == 0;
}

bool lzss_decompress_supported(const Wram* w, const Rom* rom, uint16_t s,
                               uint16_t a, uint16_t x) {
  (void)w;
  uint16_t src_addr = wram_r16(w, (uint32_t)(uint16_t)(s + 4));
  uint32_t src24 = ((uint32_t)(a & 0xffu) << 16) | src_addr;
  uint32_t off;
  if (!addr_is_wram(src24, &off) && !rom_has(rom, src24, 2)) return false;
  // The destination is written a byte at a time and never read, so all that is
  // needed is somewhere for the bytes to go.
  uint32_t dst24 = ((uint32_t)(x & 0xffu) << 16);
  return addr_is_wram(dst24 | 0x0000u, &off) ||
         addr_is_wram(dst24 | 0xffffu, &off);
}

uint16_t lzss_decompress_wram(Wram* w, const Rom* rom, uint16_t s, uint16_t a,
                              uint16_t x, uint16_t y) {
  LzssDecompressRegs r;
  LzssWork work;
  lzss_decompress_wram_counted(w, rom, s, a, x, y, &r, &work);
  return r.y;
}

void lzss_decompress_wram_counted(Wram* w, const Rom* rom, uint16_t s,
                                  uint16_t a, uint16_t x, uint16_t y,
                                  LzssDecompressRegs* out, LzssWork* k) {
  memset(k, 0, sizeof *k);

  // $80:CD25-CD2F. The stores are 16-bit, so each one lays down two bytes and
  // the high halves matter to the diff even where they mean nothing: `STA $2A`
  // writes the source bank *and* whatever the caller left in A's high byte.
  uint16_t src_addr = wram_r16(w, (uint32_t)(uint16_t)(s + 4));
  wram_w16(w, LZSS_DP_SRC_BANK, a);
  wram_w16(w, LZSS_DP_SRC, src_addr);
  wram_w16(w, LZSS_DP_DST_BANK, x);
  wram_w16(w, LZSS_DP_DST, y);
  wram_w16(w, LZSS_DP_DST_START, y);

  // $80:CD32-CD45. Spaces across the window, short of its top by 17 bytes.
  for (uint32_t i = 0; i < W_LZSS_RING_FILL; i++)
    wram_w8(w, W_LZSS_RING + i, 0x20);

  // $80:CD46-CD53. The stream heads with the count of the bytes after it.
  uint16_t src_bank = (uint16_t)(a & 0xffu);
  k->head_fast = addr_is_fastrom(((uint32_t)src_bank << 16) | src_addr);
  wram_w16(w, LZSS_DP_REMAIN,
           (uint16_t)(bus_r8(w, rom, ((uint32_t)src_bank << 16) | src_addr) |
                      ((uint16_t)bus_r8(w, rom, ((uint32_t)src_bank << 16) |
                                                    (uint16_t)(src_addr + 1))
                       << 8)));
  wram_w16(w, LZSS_DP_SRC, (uint16_t)(src_addr + 2));
  wram_w16(w, LZSS_DP_RING_POS, W_LZSS_RING_START);

  uint16_t bits = 0;
  uint8_t flags = 0;
  uint16_t head_bits = 0;  // `TYX` at $CD56, which is what X is left holding

  for (;;) {
    // $80:CD56. Out of flag bits, so fetch another byte of them. Running dry
    // here is how a well-formed stream ends.
    head_bits = bits;
    if (bits == 0) {
      k->refills++;
      if (!read_byte(w, rom, &flags, k)) break;  // `end` stays 0: see LzssWork
      bits = 8;
    }
    bits--;
    bool literal = (flags & 1) != 0;
    flags = (uint8_t)(flags >> 1);

    if (literal) {
      // $80:CD65-CD80.
      uint8_t b;
      k->tokens_lit++;
      if (!read_byte(w, rom, &b, k)) {
        k->end = 1;
        break;
      }
      write_byte(w, b);
      uint16_t pos = wram_r16(w, LZSS_DP_RING_POS);
      wram_w8(w, W_LZSS_RING + pos, b);
      wram_w16(w, LZSS_DP_RING_POS, (uint16_t)((pos + 1) & W_LZSS_RING_MASK));
      continue;
    }

    // $80:CD82-CDCE. Two bytes: twelve bits of window position and four of
    // length, and the length is biased by three rather than two because the
    // `DEC $3E : BPL` runs the body once more than the count it stores.
    uint8_t lo, hi;
    k->tokens_match++;
    if (!read_byte(w, rom, &lo, k)) {
      k->end = 2;
      break;
    }
    if (!read_byte(w, rom, &hi, k)) {
      k->end = 3;
      break;
    }
    uint16_t match = (uint16_t)(lo | (((uint16_t)hi << 4) & 0x0f00u));
    uint16_t run = (uint16_t)((hi & 0x0fu) + 2);
    wram_w16(w, LZSS_DP_MATCH, match);
    wram_w16(w, LZSS_DP_RUN, run);

    uint16_t from = match;
    uint16_t to = wram_r16(w, LZSS_DP_RING_POS);
    for (uint16_t i = 0; i <= run; i++) {
      k->run_bytes++;
      uint8_t b = wram_r8(w, W_LZSS_RING + from);
      wram_w8(w, W_LZSS_RING + to, b);
      write_byte(w, b);
      to = (uint16_t)((to + 1) & W_LZSS_RING_MASK);
      from = (uint16_t)((from + 1) & W_LZSS_RING_MASK);
    }
    // $80:CDC6. The counter is left at $FFFF by the loop that ran it down.
    wram_w16(w, LZSS_DP_RUN, 0xffffu);
    wram_w16(w, LZSS_DP_RING_POS, to);
  }

  // $80:CDD2. Y is how far the destination pointer moved, A holds it too, and
  // the carry is the `SEC : SBC` not borrowing — which it only could if the
  // destination pointer had wrapped its bank on the way.
  uint16_t dst = wram_r16(w, LZSS_DP_DST);
  uint16_t dst_start = wram_r16(w, LZSS_DP_DST_START);
  out->a = (uint16_t)(dst - dst_start);
  out->y = out->a;
  out->x = head_bits;
  out->c = dst >= dst_start;
}
