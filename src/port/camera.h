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
//     $80:A68B / $A70A                 0.11%  X: scroll left / right
//     $80:A789 / $A816                 0.09%  Y: scroll up / down
//       $80:A5E5  tilemap_copy_column  0.35%  a vertical strip, priority applied
//         $80:AD1C tilemap_tile_addr   0.15%  where a tile lives — in terrain.h
//       $80:A401, $A54D, $A588, $9E6D         still to read
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

#endif  // PORT_CAMERA_H
