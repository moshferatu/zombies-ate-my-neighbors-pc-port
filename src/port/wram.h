// The game's RAM, as the port sees it.
//
// This is the single most important decision in Phase 3, so it is worth being
// explicit about: **the port keeps the SNES's WRAM layout, byte for byte.**
// Ported routines do not operate on tidy C structs of their own invention; they
// read and write the same 128 KB the 65816 code does, at the same offsets.
//
// That is what makes the co-simulation harness possible at all. `Wram` is
// layout-compatible with the reference core's `snes->ram`, so the harness can
// point a ported routine straight at the emulator's memory, or at a snapshot of
// it, and then diff the result byte-for-byte against what the ROM's own routine
// produced. A port that invented its own state layout could only be checked by
// eye.
//
// It costs less than it sounds. The addresses are already named — every symbol
// below is one from `tools/symbols/zamn.sym`, established from traced execution
// (see `docs/wram-map.md`) — and once the whole of Phase 3 is done, "the game
// state" is exactly this array, which is what makes PLAN.md's Phase 5 save
// states a one-line `fwrite`.
//
// Addressing: WRAM is banks `$7E` and `$7F`. Offset `$00000-$0FFFF` is
// `$7E:0000-$7E:FFFF` and `$10000-$1FFFF` is `$7F:0000-$7F:FFFF`. Direct page
// is pinned at `$0000` for the entire game (see `docs/frame-skeleton.md`), so a
// direct-page operand `$xx` in any listing is literally offset `$00xx` here.
//
// Port code: libc only.

#ifndef PORT_WRAM_H
#define PORT_WRAM_H

#include <stdint.h>

#define WRAM_SIZE 0x20000

// Layout-compatible with `snes->ram`. Deliberately a bare array in a struct:
// the type exists to stop a raw `uint8_t*` from being passed by accident, not
// to add structure the hardware does not have.
typedef struct {
  uint8_t bytes[WRAM_SIZE];
} Wram;

// `$7F:xxxx` as a `Wram` offset. `$7E:xxxx` needs no helper — it is `xxxx`.
#define WRAM_BANK_7F 0x10000

static inline uint8_t wram_r8(const Wram* w, uint32_t off) {
  return w->bytes[off & (WRAM_SIZE - 1)];
}

static inline void wram_w8(Wram* w, uint32_t off, uint8_t v) {
  w->bytes[off & (WRAM_SIZE - 1)] = v;
}

// 16-bit access, little-endian, as every `LDA`/`STA` in the game does it with
// `M`/`X` clear. Wrapping is per byte so a read at `$1FFFF` behaves like the
// hardware's bank wrap rather than running off the end of the array.
static inline uint16_t wram_r16(const Wram* w, uint32_t off) {
  return (uint16_t)(wram_r8(w, off) | ((uint16_t)wram_r8(w, off + 1) << 8));
}

static inline void wram_w16(Wram* w, uint32_t off, uint16_t v) {
  wram_w8(w, off, (uint8_t)v);
  wram_w8(w, off + 1, (uint8_t)(v >> 8));
}

// ---------------------------------------------------------------------------
// Named addresses
//
// Every one of these is a symbol from tools/symbols/zamn.sym, which only holds
// names backed by traced execution. Add to both, together, or to neither.
// ---------------------------------------------------------------------------

// --- Direct page: scheduler and NMI state ---
#define W_SCHED_CUR_TASK 0x0008
#define W_VBL_QUEUE_A_COUNT 0x000c
#define W_VBL_QUEUE_B_COUNT 0x000e
#define W_NMI_FRAME_COUNTER 0x0016
#define W_SCHED_TICK 0x0020  // 32-bit: $20 low, $22 high

// --- Direct page: sprite build scratch ---
#define W_SPRITE_UPLOAD_COUNT 0x007c  // bytes, i.e. entries x 2
#define W_SPRITE_FRAME_BASE 0x007e    // $7E = address, $80 = bank
#define W_SPRITE_FRAME_BANK 0x0080
#define W_SPRITE_SCRATCH_X 0x0038  // $80:B9D6 parks the caller's X here
#define W_SPRITE_SCRATCH_F 0x003a  // ...and the frame index x2 here
#define W_SPRITE_LRU_SLOT 0x009e   // x2; the slot the cache evicts next
#define W_SPRITE_TICK 0x00a0       // sched_tick snapshot for this frame's draw

// --- Screen ---
#define W_BRIGHTNESS_SHADOW 0x136c  // NMI restores this into INIDISP

// --- Thread scheduler tables (24 slots of one word each) ---
#define W_THREAD_WAIT 0x1180  // bit 15 = live, low bits = ticks remaining
#define W_THREAD_SP 0x11b0
#define WRAM_THREAD_SLOTS 24

// --- Vblank job queues: {u16 address-1, u8 bank, u8 pad} per slot ---
#define W_VBL_QUEUE_A 0x12a0
#define W_VBL_QUEUE_A_SLOTS 16
#define W_VBL_QUEUE_B 0x12e0
#define W_VBL_QUEUE_B_SLOTS 8

// --- Sprite frame cache (128 slots of one 16x16 frame each) ---
#define W_SPRITE_UPLOAD_SRC 0x15de   // 128 x u16
#define W_SPRITE_UPLOAD_BANK 0x165e  // 128 x u16
#define W_SPRITE_UPLOAD_DEST 0x16de  // 128 x u16, VRAM word address
#define W_SPRITE_SLOT_TICK 0x175e    // 128 x u16, the LRU key
#define W_FRAME_SLOT 0x2128          // 4096 x u16: slot x2, or negative if absent
#define W_SLOT_FRAME 0x4128          // 128 x u16: frame x2, or negative if empty

#endif
