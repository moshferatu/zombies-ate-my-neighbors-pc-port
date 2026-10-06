// What a level's start copies into WRAM, and one thing it clears.
//
//   $80:AD92  tile_attrs_load     a kilobyte of the level's tile attributes
//   $80:A037  palette_load        the background's 128 colours, to both copies
//   $80:A05B  palette_sprites_load  the sprites' 128
//   $80:C2F7  hud_reset           the HUD's shadow emptied, and what it last
//                                 drew forgotten
//
// The first three are the same loop: a far pointer on page zero, and words
// copied from the last down. Each leaves the pointer behind, which other code
// reads: the level's attributes at `$BE` are what `tile_anim_job` looks a
// frame's up in (`port/dma.h`).
//
// ## The colours are kept three times
//
// `palette_load` writes each of the background's colours to `$7E:5428` and
// again to `$7E:5628`. The second copy is the one the colour animations write
// and `background_job` sends. `palette_sprites_load` writes the sprites' to
// `$7E:5528`, the second half of the first table, which has no second copy.
// The word after the second copy, `$7E:5728`, is zeroed by the first load
// and by nothing in the second.
//
// ## The reset clears a word of its caller's page
//
// `hud_reset` ends its list of stores with `STZ $24`, on its caller's direct
// page. For a caller on page zero that is the random numbers' state
// (`port/rng.h`). Whether that is meant is not something the ROM says. The
// port clears the word its caller's page puts there.
//
// Port code: libc only.

#ifndef PORT_LOADS_H
#define PORT_LOADS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define TILE_ATTRS_LOAD_PC 0x80ad92u
#define TILE_ATTRS_LOAD_RTL_PC 0x80adb3u
#define PALETTE_LOAD_PC 0x80a037u
#define PALETTE_LOAD_RTL_PC 0x80a05au
#define PALETTE_SPRITES_LOAD_PC 0x80a05bu
#define PALETTE_SPRITES_LOAD_RTL_PC 0x80a078u
#define HUD_RESET_PC 0x80c2f7u
#define HUD_RESET_RTL_PC 0x80c349u

// The working copy of the attributes, and the two pointers on page zero: to
// the level's own table and to the copy.
#define LOADS_TILE_ATTRS_AT 0x611au
#define LOADS_TILE_ATTRS_BYTES 0x0400u
#define LOADS_DP_ATTRS_LEVEL 0x00beu
#define LOADS_DP_ATTRS_COPY 0x00bau
// The colours, and the pointer a load leaves.
#define LOADS_PALETTE_AT 0x5428u
#define LOADS_PALETTE_SPRITES_AT 0x5528u
#define LOADS_PALETTE_SHOWN_AT 0x5628u
#define LOADS_PALETTE_BYTES 0x0100u
#define LOADS_DP_PALETTE 0x00d8u
// The HUD: its shadow tilemap, the fifteen words after it that say what it
// last drew, and the word that says it has changed.
#define LOADS_HUD_SHADOW_AT 0x5f36u
#define LOADS_HUD_SHADOW_BYTES 0x0100u
#define LOADS_HUD_DRAWN_AT 0x6036u
#define LOADS_HUD_DRAWN_WORDS 15
#define LOADS_HUD_CHANGED 0x1e7au
#define LOADS_HUD_DP_CLEARED 0x24

// Is `bytes` from `bank:at` all in the cartridge, through a bank whose reads
// are fast when the cartridge's are?
bool loads_from_cartridge(uint8_t bank, uint16_t at, uint16_t bytes);

// `$80:AD92`. A is the table's address and Y its bank. Returns the last word
// read, the table's first, which is what the ROM leaves in A.
uint16_t tile_attrs_load(Wram* w, const Rom* rom, uint16_t at, uint16_t bank);

// `$80:A037` and `$80:A05B`. A is the bank and Y the address. The same
// return.
uint16_t palette_load(Wram* w, const Rom* rom, uint16_t bank, uint16_t at);
uint16_t palette_sprites_load(Wram* w, const Rom* rom, uint16_t bank,
                              uint16_t at);

// `$80:C2F7`. `page` is the caller's direct page.
void hud_reset(Wram* w, uint16_t page);

#endif
