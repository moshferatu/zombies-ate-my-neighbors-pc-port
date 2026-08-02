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
// The stack pointer NMI interrupted, and the shortest-lived word in WRAM: it is
// written by `$80:819C  TSC : STA $04` on the way in and read back by
// `$80:81EB  LDA $04 : TCS` seventy-nine bytes later, and nothing else in the
// game reads it. (The disassembler labels several other `STA $04`s with this
// name — `$80:8295`, `$80:82C5`, `$80:8816` — but those run with `D` on a
// thread's own page, so they are page+4 and not this address at all.)
#define W_NMI_SAVED_SP 0x0004
#define W_SCHED_CUR_TASK 0x0008
#define W_VBL_QUEUE_A_COUNT 0x000c
#define W_VBL_QUEUE_B_COUNT 0x000e
#define W_NMI_FRAME_COUNTER 0x0016
#define W_SCHED_TICK 0x0020  // 32-bit: $20 low, $22 high
// The two bytes `$80:9D39` keeps its shift register and its clock in — named in
// `port/rng.h`, where the routine that owns them is, and repeated here only
// because every other global is.

// --- Direct page: the APU command protocol ($80:CCC8) ---
//
// One *byte*, and the only piece of game state the audio path has. `$80:CCC8`
// writes it to `$2143` after every command and the SPC copies it back, so the
// next command's `CPY $2143 : BNE` waits on it. It is on direct page zero for
// every caller — `apu_play_sfx` installs `D = $0000` before sending — which is
// what makes it a global rather than a field of the thread making the noise.
#define W_APU_SEQ 0x001e

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

// The player's health, and the reason it is here rather than only in the symbol
// file: `$80:FACF` adds three to it and ceilings it at ten, and `$80:EB2F`
// refuses to spend a first-aid kit while it is already ten. Ten is full.
#define W_PLAYER_HEALTH 0x1cb8

// The item the player currently has selected, and the exact counterpart of the
// word above: `$80:EAA8` is to `$80:EA63` what `$1CC0` is to `$1CBC`. The pair
// is saved and restored together at `$80:F38D`/`$80:F3A5`, and the two are
// selected by two different buttons — B cycles weapons, **A cycles items**
// (`$80:D26C  AND #$0080 : ... JSR $EAA8`) and **X uses the selected one**
// (`$80:D27D  AND #$0040 : ... JSR $EAE1`).
#define W_PLAYER_ITEM 0x1cc0

// --- Per-player inventory (2 x 14 x u16 BCD counters, stride $20) ---
//
// How much of each item a player is carrying, in the same BCD the score uses.
// `$80:EAA4` holds the two base addresses — `$1CCC` and `$1CEC` — indexed by a
// player number already doubled, and `$80:EA7E  CPY #$001C` is what bounds the
// walk at fourteen entries. `$80:F87B` adds to one of them on a pickup and
// `$80:EA63` searches the whole array for a non-empty slot.
#define W_PLAYER_INVENTORY 0x1ccc
#define W_PLAYER_INVENTORY_STRIDE 0x20
#define W_PLAYER_INVENTORY_SLOTS 14

// --- Per-player items (2 x 12 x u16 BCD counters, stride $20) ---
//
// The second inventory, and everything above it is duplicated one array over:
// `$80:D1E6` holds *its* two base addresses — `$1D0C` and `$1D2C` — the page
// keeps the one that applies at `PLAYER_DP_ITEMS`, `$80:F8D6` adds to a slot of
// it and `$80:EAA8` searches it for a non-empty one. Two words shorter than the
// weapons (`$80:EABE  CPY #$0018`), and its counters are capped two digits lower
// (`$80:F8F0  CMP #$0099` against the weapons' `$0999`) — which is the whole
// difference between a thing you carry and a thing you shoot.
#define W_PLAYER_ITEMS 0x1d0c
#define W_PLAYER_ITEMS_STRIDE 0x20
#define W_PLAYER_ITEMS_SLOTS 12

// --- The score (2 slots of one 32-bit BCD counter each, stride 4) ---
//
// `$80:C7EB` and `$80:C801` are the two copies of the addition — low half here,
// carry into the half four bytes up — and `$80:C0CF` is the HUD noticing the
// value changed. Decimal, not binary: see `port/score.h`.
#define W_PLAYER_SCORE 0x1e72

// Which *side* owns each score slot: 0 or 2, the value the sign bit of a
// collision id resolves to. `$80:925D` seeds them 0 and 2 — the identity — and
// `$80:C7C2` searches the pair to turn a side back into a slot.
#define W_SCORE_SLOT_SIDE 0x1e84

// --- The sprite display list (see port/oam.h) ---
#define W_ACTOR_SLOTS 0x185e     // 32 x 20-byte records
#define W_ACTOR_LIST_HEAD 0x1b5e  // offset of the first live record, or 0
#define W_CAMERA_X 0x1b6a         // world coordinate of the top-left of the screen
#define W_CAMERA_Y 0x1b6c
#define W_VISIBLE_ACTORS 0x137e   // up to 32 x u16: the records this frame draws
#define W_VISIBLE_ACTOR_COUNT 0x009c  // bytes, i.e. entries x 2
#define W_OAM_BUFFER 0x13be       // 544 bytes, DMA'd to OAMDATA every frame
#define W_SPRITE_PASS_PHASE 0x1b64  // see SPRITE_PASS_PHASE_TABLE in port/oam.h

// --- The expanded tilemap, and the scalars collision reads it with ---
//
// The level loader turns the block map into a full 16-bit tilemap in WRAM bank
// `$7F` and then never looks at the level record again; everything below is
// what it leaves behind for the collision routines in `port/terrain.h`. See
// `src/assets/level.h` for the expansion itself.
//
// On `movies/level1.zmv` these hold 352, 104 and `$7E:611A`, which is level 1's
// 22x13 blocks expanded to 176x104 tiles at two bytes each.
#define W_TILEMAP_ROW_BYTES 0x00b2  // tile columns x 2, i.e. one row in bytes
#define W_TILEMAP_ROWS 0x00b4       // tile rows
// A 24-bit pointer, low word here and bank byte at +2, to 512 attribute words —
// one per BG tile, indexed by the tilemap entry's low ten bits doubled.
#define W_TILE_ATTRS 0x00ba
// One word per tile row, holding that row's byte offset into the tilemap, so a
// row lookup is a table read rather than a multiply. Indexed by the row number
// already doubled, which is exactly what `LSR A : LSR A : AND #$FFFE` produces.
#define W_TILE_ROW_BASE 0x4328

// The two players' records, as *pointers into* `W_ACTOR_SLOTS`, or zero for a
// player who is not on the board — which is what `$D4` reads in one-player mode.
//
// `$80:A93F` is the proof and also the reason they exist: it reads `($D2),Y`
// at `Y = 2` and `Y = 6` — `ACTOR_X` and `ACTOR_Y` — adds `($D4),Y`, halves the
// sum and stores it as the point the camera centres on. Watching `$D2` on
// `movies/level1.zmv` it takes `$1AB6` at frame 1705, and `$1AB6` is the record
// the display list shows carrying collision id `$05`, which is player A.
//
// They are direct-page addresses that everything reads absolutely, because the
// routines that want them open `PEA $0000 : PLD` first.
#define W_PLAYER_A_RECORD 0x00d2
#define W_PLAYER_B_RECORD 0x00d4

// Which thread player A is. `$80:A8A4` registers a player's record with
// `STA $00D2,X`, and when `X` is zero — player A — it also files the task that
// was running at the time: `LDA $0008 : STA $00D6`. Nothing else writes it.
//
// One routine reads it, `step_tether_blocked`, and it reads it to find out
// which of the two players is asking, so that the answer can be about the
// other one. See `port/step.h`.
#define W_PLAYER_A_TASK 0x00d6

// The level record's `+$26`, copied here by `$80:86F9` at load. `level.h` calls
// it `priority_below` because `$80:A47B` forces BG priority on for every tile
// whose index is under it as the camera streams them — but `$82:90F7` reads the
// same word as a **collision** threshold, refusing any tile below it before it
// has even looked at the attribute word. The tiles the game draws in front of
// the player are the tiles it will not let something stand on.
// The bump allocator `$80:A401` hands tilemap staging buffers out of: `$CA`
// is the next free address and `$CC` how many bytes are left. `$80:A64F`
// starts them at `$4B28` and `$0900`, so the arena is `$7E:4B28-$7E:5428`.
//
// Neither had a name before, in `zamn.sym` or in `docs/wram-map.md`, which
// is why they are spelled out here rather than cited.
#define W_TILEMAP_ARENA_NEXT 0x00ca
#define W_TILEMAP_ARENA_LEFT 0x00cc
#define W_TILE_PRIORITY_BELOW 0x00dc

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

// What each thread was started as, filed by `$80:8276`/`$80:827B` when the slot
// is allocated and read by nothing any trace has seen. Two parallel arrays, both
// indexed by the same doubled slot, and the address is the entry point **minus
// one** — the same off-by-one every far address in this game's stack frames
// carries, because what reaches them is an `RTL`.
#define W_THREAD_ENTRY 0x2002
#define W_THREAD_ENTRY_BANK 0x2032

// How many threads are live. `$80:8271  INC $0006` is the only writer any trace
// has seen, and it is absolute rather than direct-page — so it is this word
// whatever page the caller was running on.
#define W_THREAD_COUNT 0x0006

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
