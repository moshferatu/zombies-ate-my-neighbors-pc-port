// A hit taken from a player's health.
//
// `$80:D01B` is called on every pass of a player's thread, and with the top
// bit of `$50` set it goes on to here, `$80:D02D`. Whoever hit the player
// set that bit. This is what is done about it:
//
//   * nothing, if the player's pictures at `$10` are the set the table at
//     `$80:FCF6` has for them. What that set is I have not seen;
//   * nothing, if they have no health;
//   * otherwise one from their health at `$7E:1CB8`, and their other
//     record, the one at `$0A`, no longer drawn.
//
// Either way the request at `$50` is cleared. After a hit that took health
// the next bit down of `$50` says how it shows: clear, and it is the sound
// and the pictures of `port/flinch.h`, at `$80:D082`. Set, and it is
// `$80:D056`, which is the ROM's.
//
// Port code: libc only.

#ifndef PORT_HIT_H
#define PORT_HIT_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define HIT_PC 0x80d02du
#define HIT_RTS_PC 0x80d081u     // the `RTS`: nothing more to do
#define HIT_FLINCH_PC 0x80d082u  // the sound and the pictures
#define HIT_OTHER_PC 0x80d056u   // the other showing of it: the ROM's

#define HIT_BANK 0x80
#define HIT_DP_OTHER_RECORD 0x0a
#define HIT_DP_WHO 0x0c       // which of the two they are, doubled
#define HIT_DP_PLAYER 0x0e    // which player, doubled
#define HIT_DP_PICTURES 0x10
#define HIT_DP_REQUEST 0x50
#define HIT_REQUEST_OTHER 0x4000u
#define HIT_UNHURT_PICTURES 0xfcf6u  // by `$0C`
#define HIT_WHO_COUNT 2
#define HIT_PLAYER_COUNT 2
#define W_HIT_HEALTH 0x1cb8

typedef enum {
  HIT_SHRUGGED,   // the pictures in which nothing is taken
  HIT_NO_HEALTH,
  HIT_FLINCHED,
  HIT_OTHER,
} HitEnd;

enum {
  HT_WHO,     // $D02D-$D035
  HT_NONE,    // STZ $50 : BRA
  HT_HEALTH,  // $D03A-$D040
  HT_TAKE,    // $D041-$D055
  HT_TAKEN,
  HT_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[HT_BLOCK_COUNT];
  HitEnd end;
} HitWork;

void player_hit(Wram* w, const Rom* rom, PortCpu* c, HitWork* k);

#endif
