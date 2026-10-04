// The camera, and the tilemap streaming under it.
//
// `$80:A93F` decides where the camera should be — one player's position, the
// other's, or the midpoint of the two — and then moves it **one pixel** towards
// that on each axis, which is why the view drifts after the players rather than
// snapping to them. Every eighth pixel of drift a new column or row of tiles has
// to appear at the edge, and that is what the rest of this file is: the code
// that copies a strip out of the expanded map and queues it for VRAM.
//
// The chain, top down, with the share of all executed instructions each carries:
//
//   $80:A93F  camera_follow            1.6%   the target, and one step toward it
//                                             — and now the whole chain is in
//     $80:A68B / $A70A                 0.11%  X: scroll left / right
//     $80:A789 / $A816                 0.09%  Y: scroll down / up
//       $80:A5E5  tilemap_copy_column  0.35%  a vertical strip, priority applied
//         $80:AD1C tilemap_tile_addr   0.15%  where a tile lives — in terrain.h
//       $80:A61D  tilemap_copy_row     0.14%  the same, one axis over
//       $80:A401  tilemap_buffer_alloc 0.02%  21 bytes of bump allocator
//       $80:A54D  camera_window_update 0.03%  six words derived from two
//       $80:A588 / $A599               0.02%  where the 64x32 tilemap wraps
//       $80:9E6D  vram_queue_request   0.01%  ask for a transfer this frame
//
// It is built bottom up, because a substituted routine has to do everything the
// ROM's does and there is no way to call back into the ROM half-way through: the
// top of this chain cannot go in until the bottom has.
//
// Port code: libc only.

#ifndef PORT_CAMERA_H
#define PORT_CAMERA_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

// --- $80:A5E5  tilemap_copy_column — A = how many tiles ---------------------
//
// Copies `count` tile words down a column of the expanded map into the buffer
// the caller has set up, forcing background priority on the ones that need it,
// and leaves the destination pointer advanced ready for the next call.
//
//   PHA : JSL $80AD1C : STA $50      ; source = the tile at (X, Y)...
//   LDA #$007F : STA $52             ; ...in bank $7F, where the map lives
//   LDA $01,S : TAX : LDY #$0000     ; the count back off the stack
//   loop:
//     LDA [$50] : STA [$54],Y        ; one tile word, straight across
//     AND #$01FF : CMP $DC : BCS +   ; ...and the priority rule below
//     LDA #$2000 : ORA [$54],Y : STA [$54],Y
//   + LDA $50 : CLC : ADC $B2 : STA $50   ; down one row
//     INY : INY : DEX : BNE loop
//   PLA : ASL A : CLC : ADC $54 : STA $54 : RTS
//
// The source steps by `W_TILEMAP_ROW_BYTES` and the destination by two, which
// is what makes it a *column*: a vertical strip of the map laid out flat.
//
// **`$DC` again, doing its third job.** `CMP $DC : BCS` forces bit 13 — the
// PPU's BG priority bit — on every tile whose nine-bit index is below
// `W_TILE_PRIORITY_BELOW`. `src/assets/level.h` first had this field as a
// draw-time flag, `$82:90F7` showed it is also a collision threshold, and here
// is the draw-time half in the flesh: the tiles that get drawn in front of the
// player are exactly the tiles nothing is allowed to stand on, and one word in
// the level record decides both.
//
// The loop is `DEX : BNE`, so it is a do-while and a count of zero means 65,536
// iterations rather than none. Faithfully reproduced rather than guarded
// against — the ROM would do the same, and a guard here would be the port
// disagreeing with it about something no caller does.
//
// On the way out A is the new destination pointer, X is 0 because that is where
// the loop left it, Y is `count * 2`, and N, Z and C all come from the closing
// `ADC $54` — the one routine in this chain whose flags describe the value it
// actually returns.
#define TILEMAP_COPY_COLUMN_ENTRY 0x80a5e5u
#define TILEMAP_SRC_BANK 0x007fu   // `LDA #$007F : STA $52`
#define TILEMAP_PRIORITY_BIT 0x2000u
#define TILEMAP_COPY_MASK 0x01ffu  // nine bits, the same mask `$82:90F7` uses

// The four direct-page words it works through. `$50`/`$52` it sets up itself;
// `$54`/`$56` is the destination the caller points at and this routine advances.
#define CAM_DP_SRC 0x50u
#define CAM_DP_SRC_BANK 0x52u
#define CAM_DP_DST 0x54u
#define CAM_DP_DST_BANK 0x56u

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} TilemapCopyRegs;

void tilemap_copy_column(Wram* w, uint16_t count, uint16_t col, uint16_t row,
                         TilemapCopyRegs* out);

// --- $80:A401  tilemap_buffer_alloc — A = bytes; A = where they start ------
//
// Twenty-one bytes of bump allocator over the `$7E:4B28` arena, and the routine
// every tilemap strip goes through before it can be filled:
//
//   PHA
//   retry: LDA $CC : SEC : SBC $01,S : BCC retry   ; is there room?
//   STA $CC
//   LDA $CA : TAY : CLC : ADC $01,S : STA $CA      ; bump it
//   PLX : TYA : RTS
//
// **That `BCC` goes back to a reload of the same unchanged `$CC`, so it is a
// spin and not a retry** — it waits for somebody else to give the arena back,
// which the vblank flush does once a frame. In ten traces across thirteen levels
// it is never taken once: `hotbytes.py` gives every byte in the routine exactly
// 3,977 executions against 3,977 calls, so the arena has always had room.
//
// The port therefore does not implement the spin, and does not pretend the case
// away either. `tilemap_buffer_alloc_supported()` declines a call that would
// have to wait, which turns an unreachable infinite loop into an enumerated
// condition the census would count if it ever happened. A guard that has never
// fired is the cheapest possible way to be honest about a branch no input
// reaches — far better than a coverage site, which would sit permanently in
// the untaken list diluting the one number `coverage.h` exists to keep.
//
// Three registers again, three sources: A is the *old* `$CA` by way of `TAY`
// and `TYA`, X is the size argument the `PLX` pulls back, N and Z are the
// `TYA`'s, and carry is left over from the `ADC` two instructions earlier.
#define TILEMAP_BUFFER_ALLOC_ENTRY 0x80a401u

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} TilemapAllocRegs;

// False when `$CC` is smaller than the request, which is the case the ROM spins
// on. Never observed; see above.
bool tilemap_buffer_alloc_supported(const Wram* w, uint16_t size);

void tilemap_buffer_alloc(Wram* w, uint16_t size, TilemapAllocRegs* out);

// --- $80:A61D  tilemap_copy_row — X = column, Y = row; no arguments else -----
//
// `tilemap_copy_column`'s horizontal twin, and near enough the same routine:
//
//   JSL $80AD1C : STA $50 : LDA #$007F : STA $52     ; source, in bank $7F
//   LDA #$0042 : JSR $A401 : STA $54                 ; 66 bytes of its own
//   LDA #$007E : STA $56
//   LDY #$0040
//   loop:
//     LDA [$50],Y : STA [$54],Y
//     AND #$01FF : CMP $DC : BCS +
//     LDA #$2000 : ORA [$54],Y : STA [$54],Y
//   + DEY : DEY : BPL loop
//   RTS
//
// Same `$DC` priority rule, same nine-bit mask. Three things differ, and all
// three change what the shim has to say.
//
// **It walks a row, so one index does both ends.** `[$50],Y` and `[$54],Y` share
// Y because consecutive tiles in a row are two bytes apart at *both* ends;
// the column version needed a separate source pointer stepped by the row
// stride. `LDY #$0040` down to zero by twos is **33 tiles**, which is what
// `hotbytes.py` reports — 33.0x the entry, the loop count read straight off
// the profile.
//
// **It allocates its own destination**, where the column version is handed one.
// That is why it needs a guard: `$80:A401` can in principle spin, so a caller
// arriving with fewer than 66 bytes left in the arena is a call this port
// declines rather than one it answers wrongly. Never observed, like the
// allocator's own.
//
// **And two of its outputs are the last iteration's.** A holds whatever the
// thirty-third tile left — masked, or masked and OR'd with the priority bit —
// and carry is that tile's `CMP $DC`. N and Z are the closing `DEY`'s and so
// describe Y, which is always `$FFFE`; X is always `$0042`, left by the
// allocator's `PLX` rather than by anything here.
#define TILEMAP_COPY_ROW_ENTRY 0x80a61du
#define TILEMAP_ROW_ALLOC 0x0042u   // `LDA #$0042` — 33 tiles, two bytes each
#define TILEMAP_ROW_FIRST_Y 0x0040u // `LDY #$0040`, counted down by twos
#define TILEMAP_ROW_TILES 33
#define TILEMAP_DST_BANK 0x007eu    // `LDA #$007E : STA $56`

// False when the arena has fewer than `TILEMAP_ROW_ALLOC` bytes left, which is
// the `$80:A401` spin one level down. Never observed.
bool tilemap_copy_row_supported(const Wram* w);

void tilemap_copy_row(Wram* w, uint16_t col, uint16_t row,
                      TilemapCopyRegs* out);

// --- $80:A54D  camera_window_update — no arguments, no branches -------------
//
// Recomputes the six words the scroll routines index the tilemap with, from the
// two the camera actually moves. Twenty-seven instructions and not one branch:
//
//   $1B6E = $1B6A >> 3        $1B70 = that + 32     ; tile window, X
//   $1B72 = $1B6C >> 3        $1B74 = that + 28     ; ...and Y
//   $1B78 = ($1B76 + 32) & 63                       ; tilemap cursor, X
//   $1B7C = ($1B7A + 28) & 31                       ; ...and Y
//
// The two masks are the PPU's, not the level's: a background tilemap is 64x32
// and the cursor wraps inside it. The 32 and the 28 are a screen's worth of
// tiles each way — 256x224 pixels.
//
// A comes back as the last `AND`'s result and N and Z with it, but **carry is
// from the `ADC` before that**, which the `AND` does not touch. X and Y are
// never mentioned.
#define CAMERA_WINDOW_UPDATE_ENTRY 0x80a54du
#define CAMERA_WINDOW_TILES_X 0x0020
#define CAMERA_WINDOW_TILES_Y 0x001c
#define TILEMAP_CURSOR_MASK_X 0x003f
#define TILEMAP_CURSOR_MASK_Y 0x001f

typedef struct {
  uint16_t a;
  bool n, z, c;
} CameraWindowRegs;

void camera_window_update(Wram* w, CameraWindowRegs* out);

// --- $5C..$66, the six words the two splitters leave behind ------------------
//
// One block of direct page, written by whichever splitter ran and read straight
// back out by the scroll routine that called it. The two disagree about what
// the words mean, which reads as a bug until you notice that **no scroll
// routine ever calls both**:
//
//   after $80:A588 (Y)  `$5C` rows above the tilemap's wrap, `$5E` rows below —
//                       two tile counts, and nothing else is written at all
//   after $80:A599 (X)  two complete transfer descriptions, `$5C`/`$5E`/`$60`
//                       and `$62`/`$64`/`$66`, each a source offset, a
//                       destination and a length
//
// So the names below are the X splitter's, because it is the one that uses all
// six. And `$80:A68B` and `$A70A` use `$60` for a third thing again — the strip
// they just bought off the allocator — which they can only do because the Y
// splitter they called does not write it.
#define CAM_DP_RUN_A_SRC 0x5cu
#define CAM_DP_RUN_A_DST 0x5eu
#define CAM_DP_RUN_A_LEN 0x60u
#define CAM_DP_RUN_B_SRC 0x62u
#define CAM_DP_RUN_B_DST 0x64u
#define CAM_DP_RUN_B_LEN 0x66u

// --- $80:A588  camera_split_y — six instructions ------------------------------
//
//   $5E = $1B7A & 31 ; $5C = 32 - $5E
//
// Where the 32-row tilemap wraps relative to the current cursor: `$5E` rows
// below the wrap and `$5C` above it. `$80:A5E5`'s callers use the pair to split
// one column copy into two, which is why a vertical scroll can call it twice.
// A, N, Z and C are all the closing `SBC`'s, for once in agreement.
#define CAMERA_SPLIT_Y_ENTRY 0x80a588u
#define CAMERA_SPLIT_ROWS 0x0020

typedef struct {
  uint16_t a;
  bool n, z, c;
} CameraSplitRegs;

void camera_split_y(Wram* w, CameraSplitRegs* out);

// --- $80:A599  camera_split_x — the other axis, and four times the routine ---
//
// The Y splitter hands back two row counts and lets its caller work out what to
// do with them. This one does the working out itself, and the reason is the
// tilemap's shape: it is 64 columns wide but the PPU stores it as **two 32x32
// screens `$400` words apart**, so a row that crosses the seam is not one
// transfer with a wrapped address, it is two transfers to unrelated places.
//
//   LDA $1B76 : CMP #$0020 : BCS wrapped
//     $5C = 0            $5E = $1B76             $60 = (32 - $1B76) * 2
//     $62 = $60          $64 = $0400             $66 = $42 - $60
//   wrapped:
//     $5C = (64 - $1B76) * 2                     $60 = $42 - $66
//     $5E = 0
//     $62 = 0            $64 = ($1B76 & 31) | $0400
//                                                $66 = (64 - $1B76) * 2
//
// Read as (source offset, destination, length) triples, both branches say the
// same thing in a different order: the run that starts at the cursor and the
// run that picks up after the seam. Below the seam the cursor is in the left
// screen and the overflow lands at `$400`; above it the cursor is already in
// the right screen and the overflow wraps back to word 0.
//
// The lengths are bytes and the destinations are words, which is why `$42` — 33
// tiles — appears next to `$0400` without either being converted.
//
// **Both closing `SBC`s subtract the same quantity**, the run that was measured
// rather than the one that was left over, so A, N, Z and C are identical
// expressions on both paths even though they are stored to different words.
// X and Y are not mentioned in either branch.
#define CAMERA_SPLIT_X_ENTRY 0x80a599u
#define CAMERA_SPLIT_COLUMNS 0x0020  // one screen of the 64-column tilemap
#define TILEMAP_COLUMNS 0x0040
#define TILEMAP_SCREEN_MASK 0x001f   // ...within one of the two screens
#define TILEMAP_SECOND_SCREEN 0x0400u  // word offset of the right-hand 32x32

void camera_split_x(Wram* w, CameraSplitRegs* out);

// --- $80:9E6D  vram_queue_request — seven bytes, three exits -----------------
//
//   BIT $26 : BMI out          ; a transfer is already pending
//   LDA $CE : BEQ out          ; ...or there is nothing queued
//   LDA #$8000 : STA $26       ; ask for one
//   out: RTS
//
// It belongs to the VRAM queue rather than to the camera, and it is here
// because the scroll routines are its only callers so far; it should move when
// `$80:9E7B vram_queue_flush` is ported.
//
// **Its middle exit cannot be reached.** All four call sites write a nonzero
// `$CE` in the instruction before the `JSR`, so `LDA $CE : BEQ` never branches;
// it was a coverage site for exactly one corpus run, came back the only untaken
// one of the five added that round, and was removed on the rule `coverage.h`
// states. The port keeps the code and drops the claim.
//
// **`BIT` is why this needs its flags published rather than derived.** `BIT
// $26` sets N from bit 15 of `$26` — memory, not A — but Z from `A & $26`, so
// the Z this routine returns on its first exit depends on **the caller's A**,
// which the routine never loads and has no other use for. Carry is untouched on
// all three paths and passes straight through.
#define VRAM_QUEUE_REQUEST_ENTRY 0x809e6du
#define RENDER_FLAG_TRANSFER 0x8000u

typedef struct {
  uint16_t a;
  bool n, z;
} VramRequestRegs;

void vram_queue_request(Wram* w, uint16_t a, VramRequestRegs* out);

// --- $80:A68B / $A70A / $A789 / $A816 — the four scroll routines -------------
//
// One pixel of camera movement each, and everything above is what they need to
// do it. All four have the same three-part shape:
//
//   1. refuse, if the camera is already against that edge of the map;
//   2. move it one pixel, and the sub-tile remainder with it;
//   3. every eighth pixel — when the remainder wraps — advance the tilemap
//      cursor, copy the strip of map that has just come into view, and queue it.
//
// **They are two routines written twice.** `$80:A70A` and `$80:A816` are not in
// the listing as code at all — no label, no disassembly, just `.db` runs the
// tracer never proved were instructions — and decoding them by hand is what
// shows why: each is its partner byte for byte with six substitutions. So this
// file has one X scroller and one Y scroller, parameterised by exactly those
// six differences, which is the shortest honest way to write down a mirror:
//
//   direction   `DEC A`, and `BEQ` against zero          | `INC A`, `CMP $B8`
//   limit       nothing to compare against               | `$B8` / `$B6`
//   boundary    `AND #$0007 : CMP #$0007`                | `AND #$0007`
//   cursor      stepped down, then masked                | stepped up
//   edge        the near tile-window word ($1B6E/$1B72)  | the far one
//   destination the cursor itself ($1B76/$1B7A)          | the cursor's end
//
// The last two are the same fact twice: a strip appears at whichever edge the
// camera is moving towards, and it is written at whichever end of the tilemap
// cursor's wrap that edge currently maps to.
//
// **The X pair and the Y pair split their work differently, and the tilemap's
// geometry is why.** A column is 32 tiles and the tilemap is 32 rows tall, so a
// column always wraps somewhere and the X scrollers call `$80:A5E5` twice —
// once for the part below the wrap, once for the part above — into one buffer
// they allocated themselves, and queue **one** transfer. A row is 33 tiles
// across a tilemap 64 wide split into two screens `$400` apart, so the Y
// scrollers copy the row once, with `$80:A61D` allocating for them, and queue
// **two** transfers at unrelated addresses. Same job, opposite shapes.
//
// Two details of the arithmetic are worth stating because a natural
// transcription gets them wrong:
//
// **`ADC $5E : ADC $1B7E` has no `CLC` between the two adds**, so the second one
// carries the first's out. Both destinations in a Y scroll are built that way.
//
// **The unconditional column copy cannot be the 65,536-iteration case.** The
// conditional one is guarded by `LDA $5E : BEQ` because `cursor & 31` really can
// be zero; the other takes `32 - (cursor & 31)`, which is 1..32 and never zero,
// so `$80:A5E5`'s do-while is safe without a test.
//
// On the way out A, N and Z belong to `$80:9E6D` — the last thing any of the
// four calls — X is the queue's new entry count and Y the doubled cursor the
// destination table was indexed with. Carry is the last `ADC $1B7E`. The two
// early exits publish something different again, and the *first* exit of the
// two backward routines publishes carry it never set: `LDA $1B6A : BEQ` has no
// `CMP` in it, so the caller's carry goes straight back out.
#define CAMERA_SCROLL_LEFT_ENTRY 0x80a68bu
#define CAMERA_SCROLL_RIGHT_ENTRY 0x80a70au
#define CAMERA_SCROLL_DOWN_ENTRY 0x80a789u
#define CAMERA_SCROLL_UP_ENTRY 0x80a816u

// 64 bytes: 32 tiles, one whole column of a 32-row tilemap. Also the length the
// queue entry carries, because it is the same `LDA #$0040` read twice.
#define TILEMAP_COLUMN_ALLOC 0x0040u

// Where a strip goes in VRAM, as a word offset from `W_TILEMAP_VRAM_BASE`.
// Neither table needs to exist — the column one is `(i & 31) | ((i & 32) << 5)`
// and the row one is `i * 32` — and both are read from the ROM anyway, because a
// port that recomputes a table is a port that has stopped comparing against it.
#define TILEMAP_COLUMN_DEST_TABLE 0x809d77u  // 64 words, indexed by cursor X
#define TILEMAP_ROW_DEST_TABLE 0x809df7u     // 32 words, indexed by cursor Y

// `$2115` VMAIN, as the queue entry carries it: bit 7 puts the increment on the
// high byte, and the low bits pick its size. A column steps 32 words at a time
// down the tilemap; a row steps one.
#define VMAIN_STEP_COLUMN 0x0081u
#define VMAIN_STEP_ROW 0x0080u

// The three registers a scroll routine may hand straight back. X and Y survive
// both exits that queue nothing, and carry survives one of the four's first
// exit — see the last paragraph above.
typedef struct {
  uint16_t x, y;
  bool c;
} CameraScrollIn;

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} CameraScrollRegs;

// False only when this call would reach the strip *and* the arena is too short
// for it — `$80:A401`'s spin, one or two levels down. A call that stops at
// either early exit allocates nothing and is always supported, which is why
// these ask about the camera before they ask about the arena.
bool camera_scroll_left_supported(const Wram* w);
bool camera_scroll_right_supported(const Wram* w);
bool camera_scroll_down_supported(const Wram* w);
bool camera_scroll_up_supported(const Wram* w);

void camera_scroll_left(Wram* w, const Rom* rom, const CameraScrollIn* in,
                        CameraScrollRegs* out);
void camera_scroll_right(Wram* w, const Rom* rom, const CameraScrollIn* in,
                         CameraScrollRegs* out);
void camera_scroll_down(Wram* w, const Rom* rom, const CameraScrollIn* in,
                        CameraScrollRegs* out);
void camera_scroll_up(Wram* w, const Rom* rom, const CameraScrollIn* in,
                      CameraScrollRegs* out);

// --- $80:A93F  camera_follow — the top of the chain --------------------------
//
// Everything above exists to serve this. It picks the point the view should be
// centred on, and then moves the camera **one pixel** towards it on each axis:
//
//   PHD : LDA #$0000 : TCD
//   BIT $26 : BVS out                 ; bit 14 — the camera is held
//   LDA $D2 : BNE both_or_a
//   LDA $D4 : BNE only_b              ; ...and neither player is on the board
//   out: PLD : SEC : RTL
//   both_or_a: LDA $D4 : BEQ only_a
//     $1CB0 = (($D2).x + ($D4).x) >> 1 ; $1CB2 likewise for Y
//   only_a:  $1CB0 = ($D2).x ; $1CB2 = ($D2).y
//   only_b:  $1CB0 = ($D4).x ; $1CB2 = ($D4).y
//   LDA $1CB0 : SEC : SBC #$0080 : SEC : SBC $1B6A : TAX : BEQ +
//     ASL A : BCC right : JSL $80A68B : BRA +
//     right: JSL $80A70A
//   + LDA $1CB2 : SEC : SBC #$0084 : SEC : SBC $1B6C : TAX : BEQ +
//     ASL A : BCC down : JSL $80A816 : BRA +
//     down: JSL $80A789
//   + PLD : SEC : RTL
//
// **One pixel, whatever the distance**, which is why the view drifts after the
// players instead of snapping to them — the delta is computed and then only its
// *sign* is used. `ASL A : BCC` is that sign test: the shift puts bit 15 into
// carry, so carry set means the target is behind the camera. The magnitude is
// thrown away in the same instruction that reads the sign.
//
// **The midpoint truncates.** `LDA ($D2),Y : CLC : ADC ($D4),Y : LSR A` shifts
// A alone, so the carry out of a sum over `$FFFF` is discarded rather than
// shifted back in. No pair of player positions can reach that, and the port
// wraps anyway, because the day one can is not the day to find out the port
// disagreed.
//
// **`$0080` and `$0084` are not the screen's centre.** 128 across is, but 132
// down is eight pixels below the middle of a 224-line screen — the playfield
// sits under a status bar and this is where its centre actually falls.
//
// Its two `SEC`s in a row are not decoration either: `SBC #$0080` can borrow,
// so the second subtract needs carry set again before it runs.
//
// Every exit is `PLD : SEC : RTL`, so **carry is always set and N and Z always
// describe the caller's direct page** — this is a `PHD` routine like
// `step_tether_blocked`, and its flags say nothing about what it did. A comes
// back as the Y delta, or zero on the two exits that never compute one; X is
// that delta too, unless a scroll routine overwrote it; and Y is `$0006` — the
// index the record read left behind — unless one did.
#define CAMERA_FOLLOW_ENTRY 0x80a93fu

// `BIT $26 : BVS`, and `$80:9E7B vram_queue_flush` opens with the same test — so
// bit 14 is a deliberate freeze: with it set the camera does not move and the
// VRAM queue does not drain.
//
// **It is set at `$80:AB8D` and cleared at `$80:ABCC`**, by `LDA #$4000 : TSB
// $26` and `LDA #$4000 : TRB $26` around a bulk tilemap blit — one routine
// holding the view still while it rewrites what the view is looking at, and
// then asking for the transfer itself on the way out. Neither instruction is in
// `analysis/bank_80.asm` as code; the whole routine is a `.db` run, and this
// was found by scanning the ROM image for the four opcodes that can write `$26`
// rather than by reading the listing. A grep of the disassembly said the bit
// had no writer at all, which is what a grep of a disassembly is worth in a
// bank this partly-decoded.
#define RENDER_FLAG_CAMERA_HELD 0x4000u

// Where the view wants to be centred, in world coordinates. Written here and
// read back four instructions later, which is the only reason they are memory
// rather than two registers.
#define W_CAMERA_TARGET_X 0x1cb0
#define W_CAMERA_TARGET_Y 0x1cb2

#define CAMERA_CENTRE_X 0x0080
#define CAMERA_CENTRE_Y 0x0084

typedef struct {
  uint16_t a, x, y;
} CameraFollowRegs;

// False when the strips this call would ask for do not both fit in what is left
// of the arena — the same `$80:A401` spin the four scroll routines guard, asked
// once for up to two of them at a time. Never observed.
bool camera_follow_supported(const Wram* w);

void camera_follow(Wram* w, const Rom* rom, uint16_t x, uint16_t y,
                   CameraFollowRegs* out);

// --- $80:A937  camera_scroll — up to four pixels of following a frame --------
//
// A vblank job, and a thunk: `JSL $80A93B`, which is `JSL camera_follow`, and
// each of the two falls through into what it called once that returns. So one
// pass through it is four calls of `camera_follow`, each handed the registers
// the last one left, which is how the view catches up by as much as four
// pixels a frame.
#define CAMERA_SCROLL_ENTRY 0x80a937u
#define CAMERA_SCROLL_CALLS 4

// Would all four calls have room for their strips? It runs them to find out,
// so `scratch` is a copy.
bool camera_scroll_supported(Wram* scratch, const Rom* rom, uint16_t x,
                             uint16_t y);

void camera_scroll(Wram* w, const Rom* rom, uint16_t x, uint16_t y,
                   CameraFollowRegs* out);

#endif  // PORT_CAMERA_H
