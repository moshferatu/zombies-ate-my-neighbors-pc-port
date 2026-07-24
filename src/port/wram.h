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
// `$7E:0000-$7E:FFFF` and `$10000-$1FFFF` is `$7F:0000-$7F:FFFF`.
//
// Direct page is `$0000` for the scheduler, NMI and the per-frame housekeeping,
// so a direct-page operand `$xx` in one of those routines is literally offset
// `$00xx` here — which is every address named below. It is **not** zero inside a
// thread: each of the 24 runs on its own 128-byte page in `$7E:0100-$7E:0CFF`,
// and an actor's state is that page. Those fields are named in
// `port/collide.h`, as offsets rather than addresses. See
// `docs/frame-skeleton.md` and `docs/wram-map.md`.
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
#define W_SPRITE_PIECES_LEFT 0x0086   // metasprite pieces still to emit
#define W_OAM_INDEX 0x0088            // byte offset of the next free OAM entry
#define W_SPRITE_META_PTR 0x008a      // $8A = address, $8C = bank
#define W_SPRITE_META_BANK 0x008c
#define W_SPRITE_ORIGIN_X 0x008e  // the actor's position, camera already out
#define W_SPRITE_ORIGIN_Y 0x0090
#define W_SPRITE_ATTR_OR 0x0092   // OAM attribute bits the actor forces on
#define W_SPRITE_ATTR_MASKED 0x0094  // one piece's attr, after the AND
#define W_SPRITE_ATTR_AND 0x0096  // mask applied to each piece's attribute word
#define W_OAM_PASS_CURSOR 0x009a  // byte index into visible_actors the pass is at
#define W_SPRITE_LRU_SLOT 0x009e  // x2; the slot the cache evicts next
#define W_SPRITE_TICK 0x00a0      // sched_tick snapshot for this frame's draw
#define W_SPRITE_SCRATCH_X 0x0038  // $80:B9D6 parks the caller's X here
#define W_SPRITE_SCRATCH_F 0x003a  // ...and the frame index x2 here

// --- Direct page: the overlap pass's scratch ($80:BEC9) ---
//
// $38 and $3A are the same two words above, and that is not a clash: $30-$4F is
// a common scratch pool that every routine using it re-establishes on entry and
// nobody reads across a call. They are named twice because what they hold
// during an overlap test has nothing to do with what they hold during a sprite
// lookup, and a name that covered both would say nothing. Neither is in
// `zamn.sym` for the same reason — the address has no one meaning to record.
#define W_OVERLAP_X 0x0038       // the outer record's position, the pair's origin
#define W_OVERLAP_Y 0x003a
#define W_OVERLAP_CURSOR 0x003c  // where the outer walk is, parked over the inner
#define W_OVERLAP_ID 0x004a      // the outer record's ACTOR_COLLIDE_ID

// --- Direct page: the collision dispatch's scratch ($80:BE8F) ---
//
// The same pool again, and the same caveat: these six words mean this only
// between `$80:BE8F` and the `RTS` at `$80:BEC8`. `A` is the pair's outer
// record — the one the overlap walk was holding — and `B` is the inner one.
// Both get told about the other, so everything is read out of both records
// before either handler runs.
#define W_NOTIFY_REC_A 0x003e
#define W_NOTIFY_ID_A 0x0042
#define W_NOTIFY_THREAD_A 0x0044
#define W_NOTIFY_REC_B 0x0040
#define W_NOTIFY_ID_B 0x0046
#define W_NOTIFY_THREAD_B 0x0048

// The pair as the handler that is *running* sees it, swapped between the two
// dispatches. These two are not scratch in the sense above: they are the
// argument an actor handler reads, so they outlive the dispatch by design.
#define W_HANDLER_SELF 0x0078   // the record whose handler is running
#define W_HANDLER_OTHER 0x0076  // the record it collided with

// --- Screen ---
#define W_BRIGHTNESS_SHADOW 0x136c  // NMI restores this into INIDISP

// --- Per-player state (2 x u16, indexed by a player number already doubled) ---
//
// The weapon the player currently has selected. `$80:8874` seeds it to 0 for
// each player that is in the game, `$80:F38D`/`$80:F3A5` save and restore it in
// a pair with `$7E:1CC0`, and `$80:D219` uses it as an index into a table of
// them. `$80:F950` is why it is here: one weapon changes what a collision does.
#define W_PLAYER_WEAPON 0x1cbc

// --- The sprite display list (see port/oam.h) ---
#define W_ACTOR_SLOTS 0x185e     // 32 x 20-byte records
#define W_ACTOR_LIST_HEAD 0x1b5e  // offset of the first live record, or 0
#define W_CAMERA_X 0x1b6a         // world coordinate of the top-left of the screen
#define W_CAMERA_Y 0x1b6c
#define W_VISIBLE_ACTORS 0x137e   // up to 32 x u16: the records this frame draws
#define W_VISIBLE_ACTOR_COUNT 0x009c  // bytes, i.e. entries x 2
#define W_OAM_BUFFER 0x13be       // 544 bytes, DMA'd to OAMDATA every frame
#define W_SPRITE_PASS_PHASE 0x1b64  // see SPRITE_PASS_PHASE_TABLE in port/oam.h

// --- Thread scheduler tables (24 slots of one word each) ---
#define W_THREAD_WAIT 0x1180  // bit 15 = live, low bits = ticks remaining
#define W_THREAD_SP 0x11b0
// The callback a thread registers with `$80:8475` and `$80:8480` enters — see
// `thread_call_handler` in port/collide.h. Two words rather than one far pointer
// because the ROM stores them with separate tables; the bank word's high byte
// is always zero.
#define W_THREAD_HANDLER 0x1300
#define W_THREAD_HANDLER_BANK 0x1330
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
