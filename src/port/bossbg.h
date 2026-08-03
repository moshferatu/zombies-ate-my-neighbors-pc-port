// The big-figure blitter: a boss too large for sprites, drawn as a background.
//
// A 65816 can put 128 sprites on a screen and no more than 34 on a scanline,
// which is nowhere near enough for level 25's giant baby. So the game does not
// use sprites for it at all. It keeps four animation frames of a **14x20 tile**
// figure -- 112x160 pixels -- as raw tilemap words in ROM, and every frame it
// re-uploads one of them into a background layer's tilemap and moves the layer
// with the scroll registers.
//
//   $82:8014  boss_bg_queue        0.1%   twenty rows, queued where they lie
//   $82:8069  boss_bg_queue_flip   1.1%   ...and the same figure facing the
//                                         other way, mirrored on the way past
//
// Both are called from `$82:892E`, which picks the frame, and both end by
// registering the same vblank job -- `$82:81C9`, which is the DMA loop that
// drains what they queued.
//
// ## The two of them are the same routine with one difference
//
// A stored figure is a four-byte header (width in tiles, height in rows)
// followed by `width * height` tilemap words, and both routines walk it a row
// at a time, appending one DMA job per row: source, bank, length in bytes, and
// a VRAM word address that starts at `$6800` and steps by `$0020` -- one
// tilemap row -- for each.
//
// `boss_bg_queue` points the job straight at the ROM. There is nothing to
// prepare, so it is four stores and an add per row and it costs 382
// instructions and 11,565 master cycles a call.
//
// `boss_bg_queue_flip` cannot, because the figure it wants is not in the ROM.
// It copies each row into a staging buffer at `$7E:5736` **backwards**, and
// toggles bit 14 -- the tilemap X-flip bit -- of every word on the way past:
//
//     LDY $38 : DEY : DEY          ; the last word of the row
//     LDA [$28],Y : EOR #$4000 : STA [$2C]
//     INC $2C : INC $2C : DEY : DEY : BPL
//
// Reversing the row mirrors the figure and flipping each tile mirrors its
// pixels, and you need both or you get a figure made of correctly-ordered
// backwards tiles. It costs 2,746 instructions a call -- 89,008 cycles, seven
// and a half times the other and close to a quarter of an NTSC frame -- and
// 2,240 of those instructions are that eight-instruction loop run 280 times,
// once per tile in the figure.
//
// ## What this buys the cartridge
//
// One direction of one figure is 564 bytes. Four frames is 2,256, and the
// mirror is why that is not 4,512. It buys placement as well as size: the four
// frames did not have to be stored together, and they are not -- one is at
// `$97:FD9D` and three are in bank `$83` -- because the unmirrored path DMAs
// them straight out of wherever they happen to lie.
//
// The price is that a mirrored frame costs a 560-byte WRAM round trip every
// time it is drawn, which is why this routine is 4.4% of `level25-lane` and
// **0.0% of the other nine movies in the corpus**. Nothing else in the ranking
// is that lopsided. It is not a hot routine; it is a routine that is only ever
// hot, on the levels that have one of these.
//
// ## Neither has a branch worth marking
//
// Between them they contain three conditional branches and all three are loop
// backs. There is no decision in either routine -- the figure's size decides
// how long they run and nothing decides what they do -- so this is the first
// pair in the port to add no branch-coverage sites at all. The decisions are
// one level up, in `$82:892E`, which reads the facing flag at `$36` and calls
// one or the other.
//
// Port code: libc only.

#ifndef PORT_BOSSBG_H
#define PORT_BOSSBG_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define BOSS_BG_QUEUE_ENTRY 0x828014u
#define BOSS_BG_QUEUE_FLIP_ENTRY 0x828069u

// Direct page, which both routines force to zero on entry with `PHD : PEA
// $0000 : PLD`. The caller leaves the figure's far address in `$28`, and the
// two routines then use `$2C` for different things -- the plain one for the
// VRAM word address it is filling, the mirrored one for the staging pointer it
// is writing through, with the VRAM address moved out to `$30`.
#define BOSS_BG_DP_SRC 0x28u        // the figure: address...
#define BOSS_BG_DP_SRC_BANK 0x2au   // ...and bank
#define BOSS_BG_DP_DEST 0x2cu       // $82:8014: the VRAM word address
#define BOSS_BG_DP_STAGE 0x2cu      // $82:8069: the staging write pointer...
#define BOSS_BG_DP_STAGE_BANK 0x2eu // ...and its bank, always $007E
#define BOSS_BG_DP_FLIP_DEST 0x30u  // $82:8069: the VRAM word address
#define BOSS_BG_DP_ROW_BYTES 0x38u  // width in tiles, doubled
#define BOSS_BG_DP_ROWS 0x3au       // the outer loop's countdown

// The header is two words and the data follows it.
#define BOSS_BG_HEADER_BYTES 0x0004u

// `$7E:5736` -- `W_BOSS_BG_STAGE` -- and 560 bytes of it are live for as long
// as it takes the vblank DMA to read them back, which is the rest of this
// frame, since the job the routine registers runs before the next one starts.
#define BOSS_BG_STAGE_ADDR ((uint16_t)W_BOSS_BG_STAGE)
#define BOSS_BG_STAGE_BANK 0x007eu

// BG1's tilemap starts at VRAM word `$6800` (`$82:8000` sets BG1SC to `$6B`,
// which is that base plus the 64x32 size bits), and one row of it is 32 words
// whatever the figure's width is.
#define BOSS_BG_VRAM_BASE 0x6800u
#define BOSS_BG_VRAM_ROW 0x0020u

// Bit 14 of a tilemap word. Toggled rather than set, so a figure whose artwork
// already flips a tile stays correct when the whole figure is mirrored.
#define BOSS_BG_TILE_FLIP_X 0x4000u

// `$82:81C9`, the vblank job both routines register on the way out: it walks
// the queue below downwards, pushing each entry through channel 0 into `$2118`,
// and zeroes the cursor when it is done.
#define BOSS_BG_VBL_JOB_ADDR 0x81c9u
#define BOSS_BG_VBL_JOB_BANK 0x0082u

// The three register outputs, and the carry. All four come from the
// `JSL $80:83AE` these routines end on rather than from anything they did
// themselves, so see the note on `boss_bg_queue` for what they mean. N and Z
// are the caller's direct page -- the closing `PLD` -- and the shim, not this,
// is where those come from.
typedef struct {
  uint16_t a, x, y;
  bool c;  // set: the vblank queue was full and the figure will not be drawn
} BossBgRegs;

// `$82:8014` -- append the figure at `[$28]` to the background DMA queue, one
// job per row, and ask for the queue to be drained this vblank.
//
// **It reads the header and nothing else.** The figure's 560 bytes of tilemap
// are never touched by this routine, only pointed at; whether they are
// readable at all is the vblank DMA's problem, and the guard asks about the
// four header bytes alone rather than about the figure.
//
// **A do-while, so a height of zero draws 65,536 rows.** The ROM tests `$3A`
// at the bottom of the loop only. No stored figure has a zero height, and the
// port reproduces the wrap rather than guarding it, because a guard here would
// be a difference from the ROM that no input can distinguish from a fix.
bool boss_bg_queue_supported(const Wram* w, const Rom* rom);
void boss_bg_queue(Wram* w, const Rom* rom, BossBgRegs* out);

// `$82:8069` -- the same, mirrored through `$7E:5736`.
//
// The staging copy is what the queue entries point at, so unlike the plain
// version this one has to be able to *read* the figure. `[$28],Y` is a 24-bit
// indexed read and the ROM's own pointers never leave their bank, so the guard
// below asks for the whole figure to be readable at one bank's worth of
// contiguous addresses.
bool boss_bg_queue_flip_supported(const Wram* w, const Rom* rom);
void boss_bg_queue_flip(Wram* w, const Rom* rom, BossBgRegs* out);

#endif
