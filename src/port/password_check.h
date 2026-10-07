// A password checked.
//
// `$82:B018` is what the password screen calls when the player is done:
// four letters at `$7E:1EA0` in, and a level and a count of neighbours out.
// The screen reaches it through the far pointer in its own table.
//
// How four letters name a level is in `assets/password.h` and
// `docs/password.md`, and the tables' names here are that header's. That
// one reads a password for the launcher. This is the ROM's routine, with
// what it leaves in memory and in the registers:
//
//   * the level at `$1E7C`: 5 and every fourth to 53, from the middle two
//     letters;
//   * the count at `$1D50` and `$1D52`, from the outer two: one to nine,
//     and ten as `$0010`, since the game counts neighbours in decimal;
//   * carry clear.
//
// **A password it turns down is left changed.** The routine swaps the first
// and third letters, to make a word of each pair, and swaps them back only
// when it has found both. Turned down, the level is 1 and carry is set.
//
// One password is tested first, by its letters and not by the tables:
// level 0, and the count left as it was.
//
// Its contract with the ROM: all of it, from its first instruction to
// whichever of its four `RTL`s it comes to, which is the ROM's to make.
//
// Port code: libc only.

#ifndef PORT_PASSWORD_CHECK_H
#define PORT_PASSWORD_CHECK_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/password.h"
#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define PASSWORD_CHECK_PC 0x82b018u
#define PASSWORD_CHEAT_RTL_PC 0x82b02cu
#define PASSWORD_NO_LEVEL_RTL_PC 0x82b076u
#define PASSWORD_NO_COUNT_RTL_PC 0x82b0bau
#define PASSWORD_CHECK_RTL_PC 0x82b0ddu

#define PASSWORD_BANK 0x82
#define PASSWORD_CHEAT_FIRST 0x4342u   // its first two letters, as the word
#define PASSWORD_CHEAT_SECOND 0x4644u  // ...and its last two
#define PASSWORD_TURNED_DOWN_LEVEL 1

#define W_PASSWORD 0x1ea0u         // four letters
#define W_PASSWORD_LEVEL 0x1e7cu   // the level a game starts at
#define W_PASSWORD_GATE 0x1d50u    // `W_VICTIM_GATE`
#define W_PASSWORD_LEFT 0x1d52u    // `W_NEIGHBOURS_LEFT`
#define W_PASSWORD_GROUP 0x0038u   // twice which level's pair it was
#define W_PASSWORD_TRIED 0x003au   // the count being tried

enum {
  PW_FIRST,       // LDA $1EA0 : CMP # : BNE
  PW_SECOND,      // LDA $1EA2 : CMP # : BNE
  PW_CHEAT,       // STZ $1E7C : CLC
  PW_SWAP,        // $B02D-$B045
  PW_LEVEL_TEST,  // $B046-$B05B
  PW_LEVEL_NEXT,  // $B05C-$B06E
  PW_FAIL,        // LDA #$0001 : STA $1E7C : SEC, at either place
  PW_COUNT_HEAD,  // $B077-$B082
  PW_COUNT_TEST,  // $B083-$B0A8
  PW_COUNT_NEXT,  // $B0A9-$B0B2
  PW_FOUND,       // LDA $003A : CMP #$000A : BNE
  PW_TEN,         // LDA #$0010
  PW_TAIL,        // $B0C6-$B0DC
  PW_TAKEN,
  PW_BLOCK_COUNT
};

typedef enum {
  PASSWORD_IS_CHEAT,  // the one tested first: level 0
  PASSWORD_GOOD,      // a level and a count
  PASSWORD_NO_LEVEL,  // the middle two letters are no level's
  PASSWORD_NO_COUNT,  // ...or the outer two no count's on that level
} PasswordFate;

typedef struct {
  uint16_t blocks[PW_BLOCK_COUNT];
} PasswordWork;

PasswordFate password_check(Wram* w, const Rom* rom, PortCpu* c,
                            PasswordWork* k);

#endif
