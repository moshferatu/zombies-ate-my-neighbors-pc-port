// ZAMN's LZSS decompressor — a byte-exact C port of `$80:CD20`.
//
// The format is textbook Okumura LZSS (N=4096, F=18, THRESHOLD=2): a 4 KB
// sliding window prefilled with spaces, flag bytes whose bits select literal
// (1) or match (0), and 16-bit match tokens carrying a 12-bit window position
// and a 4-bit length. The only ZAMN-specific parts are the 16-bit byte count
// that heads every stream (there is no end marker — the stream ends when that
// count is exhausted) and the window's persistence across calls.
//
// This is the first piece of the native port proper, not an analysis tool, so
// it depends on nothing but the C standard library. `zamn_assets verify-lzss`
// checks it against the ROM routine by running both on the same input.
//
// Original register interface, for reference:
//   push  <16-bit source address>
//   A = source bank, X = destination bank, Y = destination address
//   JSL $80CD20   ->   Y = bytes written

#ifndef ASSETS_LZSS_H
#define ASSETS_LZSS_H

#include <stdint.h>

// The sliding window lives at WRAM $7E:6F00 in the original.
#define LZSS_RING_SIZE 0x1000

// Where the window position starts, and how much of the window the ROM
// actually prefills. See lzss_decompress() for why those differ.
#define LZSS_RING_START 0x0FEE

typedef struct {
  uint8_t bytes[LZSS_RING_SIZE];
} LzssRing;

typedef enum {
  LZSS_OK = 0,
  LZSS_ERR_TRUNCATED = -1,  // the stream ran past the end of `src`
  LZSS_ERR_OVERFLOW = -2,   // the output did not fit in `dst_cap`
} LzssStatus;

typedef struct {
  int32_t status;    // LzssStatus
  uint32_t written;  // bytes produced (the ROM returns this in Y)
  uint32_t read;     // bytes consumed from `src`, including the 2-byte header
} LzssResult;

// Zero the window, matching WRAM at cold boot (the boot code clears all of it).
void lzss_ring_init(LzssRing* ring);

// Decompress the stream at `src` into `dst`.
//
// `ring` is caller-owned and persists across calls *on purpose*: the ROM
// refills only 4079 of the window's 4096 bytes, so a stream that referenced
// the unfilled tail would see whatever the previous call left there. No real
// ZAMN stream does — a well-formed LZSS stream never reads ahead of the write
// position — but reproducing the ROM byte-for-byte means reproducing that too.
LzssResult lzss_decompress(const uint8_t* src, uint32_t src_avail,
                           uint8_t* dst, uint32_t dst_cap, LzssRing* ring);

#endif
