// Start a new game somewhere other than level 1.
//
// `$7E:1E7C` is the level number, and everything downstream of it is already
// general: `$80:885B` indexes the record table `$9F:8002` with twice it,
// `$80:8909` steps it between levels, and `$80:84DB` compares it against 49 to
// decide the game is over rather than stepping again. So there is nothing to
// implement — only one instruction to change, the one place a fresh game says
// where it begins:
//
//     $80:85F0  A9 01 00   LDA #$0001
//     $80:85F3  8D 7C 1E   STA $1E7C
//
// That is inside `$80:85CF`, which the main game thread at `$80:84B1` calls
// once, above the per-level loop that starts at `$80:84C1`. Change the immediate
// and the level card, the load, the victim gate and the step to the next level
// are all the game's own and all unchanged, because none of them knows where the
// number came from.
//
// **Two sites, not one.** `$80:9126` — the title menu — runs the attract demo at
// `$80:9AB0`, and the demo plays real levels off its own list at `$80:9BCF`, so
// it has to put the number back when it is done:
//
//     $80:9BBD  A9 01 00   LDA #$0001
//     $80:9BC0  8D 7C 1E   STA $1E7C
//
// The demo runs *after* `$80:85CF` and before the player presses Start, so
// patching only the first site works until somebody leaves the menu alone —
// which does not take long: the demo is playing by frame 2000, fourteen seconds
// after `--skip-intro` hands over. Those two are every `LDA #$0001 : STA
// $1E7C` in the cartridge; the other five writes are the level step, the
// password and the demo's own list, and none of them should be touched. So a
// password typed at the menu still overrides the number, which is right — it is
// the player asking second. (The one exception is a bonus room: the flag it is
// armed with below outlives the password, so `--level 50` and then a password
// gives you the room first and the password's level after it.)
//
// Shared by the game (`--level`) and the tracer (`zamn_trace --level`), so
// that a profile of level N is a profile of the level N the game plays.
#ifndef ZAMN_LEVELSTART_H
#define ZAMN_LEVELSTART_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "snes.h"
#include "cart.h"

// Bank $80 is LoROM file offset $00000, so the two bytes of each immediate are
// at $005F1 and $01BBE.
#define LEVEL_SITES 2
static const uint32_t level_site[LEVEL_SITES] = {0x005f0u, 0x01bbdu};
// All 56 records, and where the eight that are not numbered levels sit.
//
// `$9F:8002` holds 56 of them and every one loads and draws its own card, but
// only 1..48 are the levels you walk from one to the next. The cards sort the
// other eight themselves.
//
// **49 is the credits.** Its card reads CREDIT LEVEL. `$80:84DB  CMP #$0031 :
// BEQ $8500` sends the game to its ending when *that* is the level just
// finished, so 49 plays, the game ends and the title comes back. It is where
// finishing 48 takes you, and starting on it needs nothing special.
//
// **0 and 50..55 are the seven bonus rooms**, all seven cards reading BONUS
// LEVEL. 0 is the one the `BCDF` password loads, and it behaves like any other
// number: `$80:8909` steps 0 to 1, so it is followed by level 1, which is what
// the password does too. The other six are not in the chain at all — they sit
// past the count at `$9F:8000` (`$0032`, which is what `$80:8909` wraps on) —
// and they are reached a way of their own:
//
//     $82:D118  LDA $1E7C : ASL : TAX : LDA $D17E,X : BEQ .none
//     $82:D122  STA $1F50
//
// `$82:D17E` is 56 words indexed by level, six of them non-zero: **level 1 leads
// to room 51, 9 to 54, 12 to 55, 17 to 52, 22 to 50 and 33 to 53**. Walking the
// door on level N puts the room's number in `$1F50`, the level ends, `$80:8909`
// steps `$1E7C` to N+1 — and then the loop head reads the flag:
//
//     $80:885B  LDA $1F50 : BEQ .plain : STZ $1F50 : DEC $1E7C : BRA .index
//     $80:8868  .plain  LDA $1E7C
//     $80:886B  .index  ASL : TAX : LDA $9F8002,X
//
// The subtlety is that `DEC` and `STZ` do not touch A, so the accumulator at
// `.index` is still the room number this loaded from `$1F50`: **the record it
// loads is the bonus room, and the `DEC` is bookkeeping** — it puts `$1E7C` back
// to N so that the step at the end of the room lands on N+1 again. A bonus room
// costs the chain nothing, which is why it can be dropped between two levels.
//
// So "do what it would normally do" is one more pair of numbers rather than a
// different mechanism: to start on room B, set `$1E7C` to N+1 and arm `$1F50`
// with B, and the game's own `$80:885B` does the rest — the room loads, `$1E7C`
// comes back to N, and finishing it goes to N+1 exactly as it would have.
#define LEVEL_FIRST 0
#define LEVEL_LAST 55
#define LEVEL_RECORDS 56
#define LEVEL_CREDITS 49
// `$82:D17E`. Bank $82 is LoROM file offset $10000, so the table starts at
// $1517E. Read rather than transcribed: change it in a ROM hack and this changes
// with it.
#define LEVEL_DOOR_TABLE 0x1517eu
// The last byte any of this reads or writes, and the door table is the furthest
// in of them — so one bound covers the immediates, the stub and the table both.
#define LEVEL_ROM_MIN (LEVEL_DOOR_TABLE + 2u * LEVEL_RECORDS)

// Arming `$1F50` needs somewhere to arm it *from*. It has to be after the title
// menu — the attract demo at `$80:9AB0` runs inside the menu and goes through
// `$80:885B` itself, so a flag set before it would be eaten by the demo — and
// before the first pass of the per-level loop. There is exactly one instruction
// in that window, and it is a call:
//
//     $80:84B1  JSL $8085CF   the init above -- level number, victim gate
//     $80:84B5  JSL $809126   the title menu, and the demo inside it
//     $80:84B9  JSL $8088A9
//     $80:84BD  JSL $808618   <- here
//     $80:84C1  JSL $80885B   the loop head, which reads $1F50
//
// So `$80:84BD` is redirected to a stub that makes the call it displaced and
// then does the store. The stub goes at `$80:FF68`, which is 88 bytes of `$FF`
// between the last code in the bank and the cartridge header at `$80:FFC0` — the
// end-of-bank pad, and the only thing written outside the two immediates.
#define LEVEL_DOOR_CALL 0x004bdu
#define LEVEL_STUB 0x07f68u       // in the file...
#define LEVEL_STUB_ADDR 0xff68u   // ...and to the 65816, which is what the JSL takes
#define LEVEL_STUB_LEN 11

// Which level's door leads to bonus room `level`, or -1 if it is not one of the
// six — which includes room 0, whose only way in is the password and which needs
// none of this.
static int level_door_owner(const uint8_t* rom, int level) {
  if (level <= 0) return -1;
  for (int i = 0; i < LEVEL_RECORDS; i++) {
    const uint32_t e = LEVEL_DOOR_TABLE + 2u * (uint32_t)i;
    if ((rom[e] | (rom[e + 1] << 8)) == level) return i;
  }
  return -1;
}

// What `--level N` will actually do, worked out from the ROM so the banner and
// the patch cannot disagree. `number` is what goes in `$1E7C`; `arm` is the
// bonus room to put in `$1F50`, or 0 for none; `next` is the level the game goes
// to when this one is finished, or -1 for "the game ends and the title returns".
typedef struct {
  int number, arm, next;
} LevelStart;

static LevelStart level_start_plan(const uint8_t* rom, int level) {
  const int owner = level_door_owner(rom, level);
  LevelStart p;
  if (owner >= 0) {
    p.arm = level;
    // `$80:885B` DECs its way back to the owner as it loads the room, so the
    // step at the end of the room lands on the owner's successor — which is
    // where the number started, and where the room would have led anyway.
    p.number = p.next = owner + 1;
  } else {
    p.arm = 0;
    p.number = level;
    p.next = level == LEVEL_CREDITS ? -1 : level + 1;
  }
  return p;
}

// False if the ROM is not the one this knows how to change — both immediates
// have to be `LDA #`, `$80:84BD` has to be the `JSL` it is displacing, and the
// end-of-bank pad has to still be a pad. Nothing is written unless all of that
// holds, so a refusal leaves the cartridge as it came off disk.
static bool start_at_level(Snes* snes, int level) {
  Cart* cart = snes->cart;
  if (!cart || !cart->rom || cart->romSize < LEVEL_ROM_MIN) return false;
  const LevelStart plan = level_start_plan(cart->rom, level);
  for (int i = 0; i < LEVEL_SITES; i++)
    if (cart->rom[level_site[i]] != 0xa9) return false;
  if (plan.arm) {
    if (cart->rom[LEVEL_DOOR_CALL] != 0x22) return false;
    for (int i = 0; i < LEVEL_STUB_LEN; i++)
      if (cart->rom[LEVEL_STUB + i] != 0xff) return false;
  }

  for (int i = 0; i < LEVEL_SITES; i++) {
    cart->rom[level_site[i] + 1] = (uint8_t)plan.number;
    cart->rom[level_site[i] + 2] = (uint8_t)(plan.number >> 8);
  }
  if (plan.arm) {
    uint8_t* stub = cart->rom + LEVEL_STUB;
    // The call this is standing in front of, carried over rather than written
    // out, so the stub stays right if `$80:84BD` ever points somewhere else.
    memcpy(stub, cart->rom + LEVEL_DOOR_CALL, 4);
    stub[4] = 0xa9;                          // LDA #$00xx  (16-bit here: the
    stub[5] = (uint8_t)plan.arm;             // routine it just called opens
    stub[6] = (uint8_t)(plan.arm >> 8);      // `LDX #$0000` three bytes wide)
    stub[7] = 0x8d;                          // STA $1F50
    stub[8] = 0x50;
    stub[9] = 0x1f;
    stub[10] = 0x6b;                         // RTL
    cart->rom[LEVEL_DOOR_CALL + 1] = (uint8_t)LEVEL_STUB_ADDR;         // JSL
    cart->rom[LEVEL_DOOR_CALL + 2] = (uint8_t)(LEVEL_STUB_ADDR >> 8);  // $80FF68
    cart->rom[LEVEL_DOOR_CALL + 3] = 0x80;
  }
  return true;
}

#endif
