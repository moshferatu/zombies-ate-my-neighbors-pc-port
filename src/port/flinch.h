// A player's pictures, a list of them.
//
// `$80:D082` makes a sound and then shows a short list of the player's
// pictures, each for as many frames as the list says. The list is by the way
// the player faces. An entry is a place in the player's picture table and a
// number of frames; the entry after the last has no place and no frames. In
// state `$0C` the pictures are `$16` further on in the table.
//
// This is the loop from the return of its sleep at `$80:D0BF`: the next
// entry shown, and then the sleep again or, after the last, `$80:D0DD`.
//
// It is where the code that takes one from a player's health goes on to,
// at `$80:D054`.
//
// Port code: libc only.

#ifndef PORT_FLINCH_H
#define PORT_FLINCH_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define FLINCH_PC 0x80d0bfu
#define FLINCH_SLEEP_PC 0x80d0bbu  // `JSL`, A already the frames
#define FLINCH_DONE_PC 0x80d0ddu

#define FLINCH_BANK 0x80
#define FLINCH_DP_RECORD 0x08
#define FLINCH_DP_PICTURES 0x10
#define FLINCH_DP_AT 0x18       // how far down the list
#define FLINCH_DP_LIST 0x5e
#define FLINCH_DP_STATE 0x70
#define FLINCH_STATE_OTHER 0x000c  // the state with pictures of its own
#define FLINCH_OTHER_PICTURES 0x0016
#define FLINCH_ENTRY_BYTES 4

enum {
  FN_NEXT,   // $D0BF-$D0C8
  FN_HEAD,   // $D09C-$D0A6
  FN_OTHER,  // CLC : ADC #$0016
  FN_SHOW,   // $D0AB-$D0BA
  FN_TAKEN,
  FN_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[FN_BLOCK_COUNT];
} FlinchWork;

// False unless the list, its next entry and the picture it names are in
// the cartridge.
bool flinch_frame_supported(const Wram* w, const Rom* rom, uint16_t page);
void flinch_frame(Wram* w, const Rom* rom, PortCpu* c, FlinchWork* k);

#endif
