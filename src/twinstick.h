// Twin-stick shooting: the right stick fires the held weapon in the direction it
// is pushed, and the left stick goes on steering.
//
// The two halves of that are not equally hard. Firing is a button the game
// already has — `Y`, held — so the right stick leaving its deadzone can simply
// press it, and `src/main_sdl.c` ORs the bit in beside the pad's own. Aiming is
// the interesting half, and it turns out to be one word.
//
// ## The game already separates moving from facing
//
// The NMI reads `$4218`, keeps it whole in `$006E,X`, and puts its D-pad nibble
// through a sixteen-byte table at `$80:81F9` into `$0072,X` — a *direction code*
// rather than four bits. The codes are the eight compass points doubled and
// offset by two, which is what makes them an index into the nine `(dx,dy)` pairs
// at `$82:B7FC`:
//
//     nibble  U D L R  ->  $00 06 0E 00 0A 08 0C 00 02 04 10 00 00 00 00 00
//     so up=$02, up-right=$04, right=$06, down-right=$08, down=$0A,
//        down-left=$0C, left=$0E, up-left=$10, and nothing at all=$00
//
// The player's own frame then splits that one code into two, and this is the
// whole basis of the feature — `$80:D250`, in `player_state_normal`:
//
//     $80:D250  LDA $0072,X : STA $24 : BEQ + : STA $26 : +
//
// `$24` is **this frame's** direction and `$26` is the **last non-zero** one, and
// the game reads them for different things:
//
//   * `$80:D4E9  LDA $24 : BEQ` picks standing still from walking, and
//     `$80:E450  LDA $24 : TAY : ... LDA $E486,Y : ADC $30` is the step itself —
//     so `$24` is **movement**;
//   * `$80:ED54  LDA $26 : STA $04` is the argument `$80:ED30` hands to
//     `thread_spawn` when it spawns a shot — so `$26` is **where the shot goes**.
//     `$80:D526` and `$80:D743` build the idle and walking poses out of it and
//     `$80:ECD8` builds the firing one, so it is also **which way the player is
//     drawn**.
//
// Aim and facing are already the same word, and they are already not the word
// movement uses. Twin-stick is therefore not a new mechanism: it is `$26`
// getting its value from somewhere other than `$24`, and everything downstream —
// the shot's direction, the sprite, the firing pose — follows for free because
// all three were reading that word to begin with.
//
// ## So the patch is a store, and there is one place to make it
//
// It has to land after `$80:D250`'s own conditional store, or a frame in which
// the player is also walking would overwrite the aim with the walk. It has to
// land before `$80:ED30` reads it, which is later in the same thread step. And
// the game gives the two players separate direct pages, so it has to happen
// once per player — which the latch already does, with the doubled player index
// in `X`, and nothing else in the routine touches `X` between `$80:D206  LDX
// $0E` and here.
//
// That is an argument for patching the latch itself rather than for finding a
// seam around it. Nine bytes at `$80:D250` become a `JSR` and six `NOP`s; the
// nine displaced bytes are copied — not transcribed — into a stub in the
// end-of-bank pad, which then adds the override:
//
//     LDA $0072,X : STA $24 : BEQ + : STA $26     the nine that were there
//     +  LDA $80FFBC,X : BEQ ++ : STA $26         and the aim on top
//     ++ RTS
//
// The copied `BEQ` is a relative branch over the two bytes that follow it, and
// those two bytes move with it, so it means the same thing in the pad as it did
// at `$80:D250`. `LDA` long-indexed rather than absolute because the stub cannot
// know what the data bank holds and does not need to care.
//
// ## Two engines, not one — and this is where it first went wrong
//
// **`$80:D1FF` is a substituted routine.** In the playable build the C port runs
// the player's frame and the nine bytes below are never executed; they are what
// `--stock` and F1 hand back to the 65816. So patching the cartridge is half the
// feature, and on its own it is the half nobody plays.
//
// That is not a hypothetical. The first version of this was the ROM patch alone,
// and it passed everything it was shown: `zamn_headless` walked left and shot
// right, because headless has no port in it to substitute. The game did nothing
// at all. `src/port/player.c` now makes the same decision from `player_set_aim`,
// the frontend arms both from one value — `twin_apply` writes the cartridge word
// and `twin_aim_of` reads it straight back out for the port — and
// `tools/test_twinstick.c` drives `player_state_normal` directly so that the
// path the game actually takes is the path something checks.
//
// ## One state, and only one
//
// `$80:D1EF` is eight player states and two of them latch the direction: state 0
// — `player_state_normal`, where 26,972 of the routine's 27,698 entries land and
// the only one that walks and shoots — and state 1 at `$80:D2FD`, which has
// **executed zero times in all ten profiles** and which the tracer has therefore
// never disassembled. Patching a second site to match would be unverifiable code
// standing next to verified code, so it is left alone: state 0 aims, and if
// anything ever does reach state 1 it inherits whatever `$26` was last set to,
// which is where a twin-stick aim already is.
//
// ## The aim word lives in the cartridge
//
// `$80:FFBC` is two words, one per port, and the frontend writes them each frame
// before the frame runs. That is a strange place for input to live and it is the
// right one: `cart_load` mallocs the ROM and the 65816 fetches from that buffer,
// so writing it is exactly as effective as writing WRAM and needs no argument
// about which WRAM address the game will never use — an argument that would have
// to hold for all 56 levels, both players and the attract demo.
//
// Zero means nothing is asked for, and that is the game's own convention for
// this word rather than one invented here: a centred stick snaps to no octant,
// no octant is nibble 0, and nibble 0 is the table's own `$00`. So the value
// written is a direction code the ROM produced, and the stub's `BEQ` past it is
// the same test `$80:D250` makes.
//
// ## What it costs when nobody is using it
//
// A `JSR`, an `RTS`, a long `LDA`, a taken `BEQ` and six `NOP`s, twice a frame —
// about sixty cycles against the frame's 1,364,000. Nothing else changes: with
// both words zero the stub is byte for byte the routine that was there.
//
// It shares the pad at `$80:FF68` with `--level`'s bonus-room stub, which sits at
// `$FF68`..`$FF72` and is checked for the same way. The two regions do not
// overlap and each refuses unless its own bytes are still `$FF`, so the flags
// compose in either order.

#ifndef ZAMN_TWINSTICK_H
#define ZAMN_TWINSTICK_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "analysis/movie.h"

// `$80:81F9`, the D-pad nibble to direction code table. Bank $80 is LoROM file
// offset $00000. Read rather than transcribed, for the same reason `--level`
// reads its door table: this is the game's answer, not a copy of it.
#define TWIN_DIR_TABLE 0x001f9u

// `$80:D250`, and the nine bytes of it that get displaced.
#define TWIN_LATCH 0x05250u
#define TWIN_LATCH_LEN 9

// The stub, in the end-of-bank pad — in the file, and as the 65816 sees it,
// which is what the `JSR` takes.
#define TWIN_STUB 0x07f80u
#define TWIN_STUB_ADDR 0xff80u
// The nine copied bytes, then `LDA long,X` (4), `BEQ` (2), `STA $26` (2), `RTS`.
#define TWIN_STUB_LEN (TWIN_LATCH_LEN + 9)

// Two words of aim, one per port, indexed by the doubled player number the
// latch already has in `X`. The last four bytes of the pad: the cartridge header
// starts at `$80:FFC0`.
#define TWIN_AIM 0x07fbcu
#define TWIN_AIM_ADDR 0xffbcu
#define TWIN_AIM_LEN 4

// The furthest byte any of this reads or writes.
#define TWIN_ROM_MIN (TWIN_AIM + TWIN_AIM_LEN)

// What has to be at `$80:D250` for this to be a cartridge the patch understands.
// The check is exact, which is what makes copying the bytes below safe rather
// than merely tidy.
static const uint8_t twin_latch_was[TWIN_LATCH_LEN] = {
    0xbd, 0x72, 0x00,  // LDA $0072,X
    0x85, 0x24,        // STA $24
    0xf0, 0x02,        // BEQ +2
    0x85, 0x26};       // STA $26

// The D-pad bits `pad_stick` produces, as the nibble `$80:81C3` builds: the low
// four bits of `$4218`'s high byte, which are up, down, left, right in that
// order.
static inline int twin_nibble(uint16_t dpad) {
  return (int)((((dpad >> BTN_UP) & 1u) << 3) | (((dpad >> BTN_DOWN) & 1u) << 2) |
               (((dpad >> BTN_LEFT) & 1u) << 1) | ((dpad >> BTN_RIGHT) & 1u));
}

// ...and the direction code the game would have made of it. Zero for a centred
// stick, and zero for the four nibbles that name a direction and its opposite —
// which octant snapping never produces, but the table answers for anyway.
static inline uint16_t twin_dir(const uint8_t* rom, uint16_t dpad) {
  return rom[TWIN_DIR_TABLE + twin_nibble(dpad)];
}

// Install the stub. False — and nothing written — unless `$80:D250` is the
// latch this knows and both pad regions are still pad, so a refusal leaves the
// cartridge as it came off disk.
static inline bool twin_install(uint8_t* rom, uint32_t rom_size) {
  if (!rom || rom_size < TWIN_ROM_MIN) return false;
  if (memcmp(rom + TWIN_LATCH, twin_latch_was, TWIN_LATCH_LEN) != 0) return false;
  for (uint32_t i = 0; i < TWIN_STUB_LEN; i++)
    if (rom[TWIN_STUB + i] != 0xff) return false;
  for (uint32_t i = 0; i < TWIN_AIM_LEN; i++)
    if (rom[TWIN_AIM + i] != 0xff) return false;

  uint8_t* stub = rom + TWIN_STUB;
  memcpy(stub, rom + TWIN_LATCH, TWIN_LATCH_LEN);
  uint8_t* p = stub + TWIN_LATCH_LEN;
  p[0] = 0xbf;                             // LDA $80FFBC,X -- long, because the
  p[1] = (uint8_t)TWIN_AIM_ADDR;           // stub has no claim on the data bank
  p[2] = (uint8_t)(TWIN_AIM_ADDR >> 8);
  p[3] = 0x80;
  p[4] = 0xf0;                             // BEQ +2 -- no stick, no override
  p[5] = 0x02;
  p[6] = 0x85;                             // STA $26
  p[7] = 0x26;
  p[8] = 0x60;                             // RTS

  rom[TWIN_LATCH] = 0x20;                            // JSR $FF80
  rom[TWIN_LATCH + 1] = (uint8_t)TWIN_STUB_ADDR;
  rom[TWIN_LATCH + 2] = (uint8_t)(TWIN_STUB_ADDR >> 8);
  for (int i = 3; i < TWIN_LATCH_LEN; i++) rom[TWIN_LATCH + i] = 0xea;  // NOP
  // Centred, until somebody pushes something. The pad was `$FF`, which is not a
  // direction the table can produce and would be read as one.
  memset(rom + TWIN_AIM, 0, TWIN_AIM_LEN);
  return true;
}

// Where port `port`'s shots go this frame. `dir` is a code out of `twin_dir`,
// and zero hands the direction back to the game.
static inline void twin_set_aim(uint8_t* rom, int port, uint16_t dir) {
  uint8_t* p = rom + TWIN_AIM + 2u * (uint32_t)port;
  p[0] = (uint8_t)dir;
  p[1] = (uint8_t)(dir >> 8);
}

// One port's whole frame of it: `stick` is the right stick as `pad_aim` returns
// it, the aim goes where the stub will read it, and what comes back is the SNES
// buttons to OR into whatever the pad is already holding.
//
// Both halves in one call because they are one decision — a stick that aims but
// does not fire is a stick that does nothing, and a fire bit without an aim is
// the `Y` button. Keeping them together is also what makes the pair checkable:
// `tools/test_twinstick.c` asserts that a pushed stick presses `Y` and a centred
// one presses nothing, which is a claim about the frontend that would otherwise
// live in one un-testable line of it.
static inline uint16_t twin_apply(uint8_t* rom, int port, uint16_t stick) {
  const uint16_t dir = twin_dir(rom, stick);
  twin_set_aim(rom, port, dir);
  return dir ? (uint16_t)(1u << BTN_Y) : (uint16_t)0u;
}

// What the 65816 would read for this port, read back the same way. The frontend
// arms the port's copy of the latch from this rather than by working the stick
// out a second time — the two paths have to agree, and the cheapest way to make
// them agree is to give them one value.
static inline uint16_t twin_aim_of(const uint8_t* rom, int port) {
  const uint8_t* p = rom + TWIN_AIM + 2u * (uint32_t)port;
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

#endif
