// $80:CD20 — the in-game LZSS decompressor, on the SNES's own memory.
//
// **`src/assets/lzss.c` already had this algorithm, byte-exact and verified,
// and it is the wrong shape to substitute with.** That one is the asset
// pipeline's: hand it a pointer and a buffer and it hands back bytes, which is
// what `zamn_assets` wants and what `verify-lzss` checks. The game's copy is a
// 65816 routine with a memory footprint — nine words of scratch on direct page
// zero, a 4 KB sliding window at `$7E:6F00`, a long pointer it advances in
// place — and co-simulation compares all 128 KB of WRAM, so standing in for it
// means reproducing every one of those, not just the output bytes.
//
// So this is a transcription rather than a wrapper. The two will not be merged:
// the asset one is allowed to be a clean decompressor, and this one has to be
// the ROM's.
//
// **It is the first routine in the registry whose argument arrives on the
// stack** (`PEA <src address>` and then `JSL`), which is why `CosimRegs` grew an
// `s`. See the note there.
//
// Register interface, from the ROM:
//
//   push  <16-bit source address>
//   A = source bank, X = destination bank, Y = destination address
//   JSL $80:CD20   ->   Y = bytes written
//
// Port code: libc only.

#ifndef PORT_LZSS_H
#define PORT_LZSS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define LZSS_DECOMPRESS_ENTRY 0x80cd20u

// The sliding window, and the scratch the routine keeps on direct page zero.
// `PEA $0000 : PLD` at `$80:CD21` is what makes these absolute rather than
// relative to whatever page the caller was on.
#define W_LZSS_RING 0x6f00u    // $7E:6F00, 4 KB
#define LZSS_DP_SRC 0x28u      // long pointer: $28/$29 address, $2A bank
#define LZSS_DP_SRC_BANK 0x2au
#define LZSS_DP_DST 0x2cu      // long pointer: $2C/$2D address, $2E bank
#define LZSS_DP_DST_BANK 0x2eu
#define LZSS_DP_REMAIN 0x38u   // bytes of stream still to read
#define LZSS_DP_RING_POS 0x3au
#define LZSS_DP_MATCH 0x3cu    // 12-bit window offset of a match
#define LZSS_DP_RUN 0x3eu      // match length counter, run down with DEC/BPL
#define LZSS_DP_DST_START 0x40u

// The window is prefilled with spaces — but only as far as `$7E:7EEE`.
// `LDA #$0FED : MVN` moves `$0FEE` bytes from `$6F00` to `$6F01`, leaving the
// 17 bytes above the initial write position holding whatever the last call did.
// Okumura's original fills exactly the same range, so it is faithful rather
// than a bug, and `src/assets/lzss.h` says the same thing at more length.
//
// `W_` on the names because this ring is a WRAM address rather than a struct
// field: `src/assets/lzss.h` names the same three numbers for its own standalone
// decoder, and the two headers meet in `src/assets.c`.
#define W_LZSS_RING_FILL 0x0fefu
#define W_LZSS_RING_START 0x0feeu
#define W_LZSS_RING_MASK 0x0fffu

// True when the port can stand in for this call: the source has to be readable
// and the destination has to be WRAM. Every call the game makes satisfies both
// — level graphics out of ROM into a work buffer — but a decline is an
// enumerated condition rather than a fallback, so it is checked and counted.
bool lzss_decompress_supported(const Wram* w, const Rom* rom, uint16_t s,
                               uint16_t a, uint16_t x);

// Run it. `s` is the stack pointer on entry; the source address is the word at
// `s + 4`, above the three bytes of return address the `JSL` pushed. Returns
// the bytes written, which the ROM leaves in Y.
uint16_t lzss_decompress_wram(Wram* w, const Rom* rom, uint16_t s, uint16_t a,
                              uint16_t x, uint16_t y);

// --- the two byte helpers, which *can* be checked --------------------------
//
// The body above cannot be co-simulated, for a reason that is a property of the
// routine: one call is about seven frames long, so an NMI always lands inside
// it and there is no instant at which the two sides' WRAM is comparable. Its
// two leaves have the opposite shape. They are eight instructions each, they
// are called 1,071,108 times between them across the corpus — **2.6% of every
// instruction the game executes** — and a call is far too short for an
// interrupt to land in. So they are registered on their own even though nothing
// that calls them is, which is the first time that has been worth doing.
//
// Both are `JSR` leaves private to `$80:CD20`: the six call sites in the trace
// are all inside its body, so neither needs a guard.

// `$80:CDDA  lzss_read_byte` — no arguments; A = the byte, carry set when the
// stream is spent.
//
// **N and Z do not describe A.** The last flag-setting instruction before the
// `CLC : RTS` is `INC $28`, so they describe the *incremented source pointer* —
// the `PLD` trap in a different costume, and the reason this struct carries
// them rather than letting the shim derive them. On the spent path they come
// from `LDA $38`, which loaded the zero that got it there.
typedef struct {
  uint16_t a;
  bool n, z, spent;
} LzssReadRegs;
#define LZSS_READ_BYTE_ENTRY 0x80cddau

void lzss_read_byte_regs(Wram* w, const Rom* rom, LzssReadRegs* out);

// `$80:CDEB  lzss_write_byte` — A = the byte; the low 8 bits are stored and A
// comes back untouched.
//
// `SEP #$20 : STA [$2C] : REP #$20` stores one byte and leaves A's high half
// alone, and `INC $2C` is back in 16-bit mode, so N and Z describe the
// incremented destination pointer. Carry is never touched at all, which the
// shim asserts by passing the caller's through — a claim `verify` then checks
// on every one of the 697,920 calls.
typedef struct {
  uint16_t a;
  bool n, z;
} LzssWriteRegs;
#define LZSS_WRITE_BYTE_ENTRY 0x80cdebu

void lzss_write_byte_regs(Wram* w, uint16_t a, LzssWriteRegs* out);

#endif
