// Cheats, each behind a flag of its own.
//
//   --invincible            nothing hurts a player
//   --invincible-neighbors  nothing hurts a neighbour
//   --infinite-ammo         weapons and items are never used up, and the HUD
//                           shows the most the game lets anybody carry
//   --infinite-lives        dying does not cost a life
//   --give-all              every weapon and every item, from the start
//   --always-run            the running shoes, always
//
// They are made of three things, and which thing a cheat is made of was decided
// by who runs the code it has to change -- because a routine the port has
// taken over is C and no longer reads the cartridge's instructions, while one
// it has not is still the 65816's, in a native run as much as under `--stock`.
//
//   * **A byte or two of the loaded image** (never the file), checked against
//     what this ROM has before it is touched, as `--red-blood`, `--level` and
//     the twin stick do. This is how a cheat cartridge works, and it is the
//     whole of a cheat whose code the port has not taken over.
//   * **A word of WRAM, once a tick**, written where `--poke` writes: with the
//     tick's input in and before the tick runs. Whoever runs the code, it reads
//     the same WRAM.
//   * **A flag in the port**, `port_cheats` (`src/port/cheat.h`), for the two
//     places the code is the port's own: a neighbour's collision handlers and
//     one entry of the player's. The same cheat patches the cartridge's copy
//     as well, so F1 and `--stock` play the same.
//
// ## What each one is
//
// **Invincible.** Every way a collision hurts a player but two opens with the
// same test -- `LDA $52 : BPL <rts>`, the recovery timer, which a hit sets to
// `$40` and the player's frame counts down to -1: `$80:F950` (ids 3, 4 and 9,
// which is nearly everything), `$80:F979` (`$0B`, a Martian's bubble),
// `$80:DC09` (`$34`) and `$80:F935` (the floor: `port/floor.h`). Nothing else
// reads the timer -- it is not what makes a hurt player flash -- so holding it
// at `$40` is a player who is always recovering and never was hit: no flinch,
// no sound, no health lost. The two that do not ask are `$80:F9BE` (`$0A`,
// which queues `$80:F9D0`: a point of health and 75 ticks held still) and
// `$80:F999` (`$35`, `JMP $E331`); the first loses its `STA $28` and the
// port's copy its store, the second becomes an `RTS`. Health is held at ten
// besides, for what is not a collision: a potion that turns out to be poison
// (`$80:EE23`) still shows the player hurt, and costs nothing.
//
// **Neighbours.** A neighbour has one fate and the first collision to reach it
// decides (`victim_collide`, `$83:A364`): ids 5 and 6 are the two players,
// `$FF` takes it off the board without a word, and the other five -- 3, 4, 9,
// `$0B`, `$34` -- are the ways it dies. Those five are made to fall through to
// the routine's own `CLC : RTL`, as an id it has no reaction to does; so are 3
// and 4 in the handler a bubbled neighbour has (`$83:A264`), which only a save
// from before the cheat can reach. And the tourists, who turn into werewolves
// when the moon comes up (`$83:A00F  LDA $1F94 : BNE`), do not: that is a
// neighbour lost like any other. The turn (`$83:A086`) retires the entry
// (`$81:8191`) and counts down the neighbours left (`$80:C863`), which a death
// does too, and never reaches `$80:C81F`, which puts a rescued one on the
// saved list the next level's gate is read from.
//
// **Except in bonus room 50**, off level 22, where the tourists are placed at
// (22,653) in a pocket no walk reaches, even with every door open (`zamn_assets
// route` and `keys`), and turning is how they come out. There they must turn,
// or the room keeps a neighbour nobody can rescue; found in play-testing. So
// that one patch is taken back out while the loaded level record is room 50's,
// which `$80:8871  STA $10` leaves on the game thread's page at `$7E:0C10`
// from the load to the next one, and put back in anywhere else.
//
// **Ammo.** The game spends in five places, every one of them `SED : SEC :
// SBC #$0001 : STA`, and none of them the port's: `$80:ED46` a weapon fired,
// `$80:EB57` and `$80:EE75` an item used, `$80:E90F` a key in a door and
// `$80:E97F` a skeleton key. Each `#$0001` becomes `#$0000`, so the count is
// stored back as it was and the "was that the last one?" that follows still
// reads a true answer. Nothing is ever spent, so the HUD never sees a count
// dip and come back. Once a tick every count that is not zero is raised to
// the most the game allows -- `$0999` for a weapon, `$0099` for an item, the
// ceilings `$80:F87B` and `$80:F8D6` pick up to -- which is what the HUD then
// shows. A slot with none stays at none: this cheat gives nothing.
//
// **Lives.** `$80:CEC5  DEC $1D4C,X : BMI <game over>`. The `DEC` goes, and
// the `BMI` reads the `LDX $0E` before it, which is 0 or 2.
//
// **Give all.** Fourteen weapons and ten of the twelve items, at those same
// ceilings. The two left out are item slots 6 and 11 -- an orange flask and a
// thing with an aerial -- which have icons and no way to be picked up (the ids
// that would be theirs, `$27` and `$2C`, are the first-aid pickup and a dead
// player's keys) and do nothing when used (`$80:EB07`'s entries for them are
// a bare `RTS`). Given when the game is: whenever a player's inventory is
// exactly what `$80:8874` hands a new game -- a squirt gun of 150 and one
// first-aid kit -- and the first tick a player is on the board after the
// program starts or a quick save is loaded. Once, so without `--infinite-ammo`
// they run out. And only to a player who is in the game, which the HUD's
// panel flags (`$7E:1E88`/`$1E8A`) say: the game seeds player two's inventory
// whether or not anybody is playing them, and the ghost potion's HUD trick
// (`$80:DACB` selects weapon 17 and item 15, past both inventories, to draw
// its blue flames) reads player two's second weapon slot through player one's
// table. The HUD only redraws a count when that word changes, so a 999 put
// there was the old count staying under the flame, where the console, reading
// a nought, blanks it. Found in play-testing.
//
// **Always run.** The shoes are `$54 = $8000` on the player's page with a
// countdown beside it at `$56` (`$80:EB23`); `$80:E4BA  BIT $54 : BPL` moves
// the player twice a tick and `$80:D72D` steps the animation to match. A `$54`
// of zero is set to `$8000` every tick, with no countdown, so there is nothing
// to run out. `$C000` is left alone: `$80:D3A8` sets it in the state one of the
// mystery potion's draws puts a player in (`$80:DB42`), which is not the shoes.
// The monster (`$80:D9A3`) clears `$54`, and so runs too.
//
// ## Finding a player's page
//
// `$D2`/`$D4` hold each player's display record while they are on the board
// and zero when they are not (`$80:CF22` clears it on the way to a game over).
// The record's `+$0C` is the thread that made it and `$80:82DE` is each
// thread's direct page. What is found is believed only if it says so itself:
// `$0E` is the player, and `$64`/`$66` are that player's two inventories
// (`$80:D1C3`), which no other thread's page holds.
//
// ## The demo is left alone
//
// The title's demo is a recording played into a real level, and it ends when
// the last neighbour is gone -- eaten, most of them. Neighbours that cannot be
// would leave it running until somebody pressed a button, and a player who
// cannot die is not the one the recording was made with. So while the demo is
// on every patch is taken back out, the port is told nothing, and no word is
// held; when it ends they go back. The demo is known by the job that plays the
// recording: `$80:9B7D  LDA #$9CB2 : LDY #$0080 : JSL $80:8418` files it in the
// table of eight at `$7E:12E0`, as the address less one and the bank, and it
// stays there until the demo is over. Give all is not spent on the demo: what
// is owed is still owed when a game starts.
//
// ## What they are not for
//
// A movie is its input and nothing else, so a cheat that changes what the
// input meets makes a different game of it; the frontend says so and carries
// on. And the top scores are read but not written while any cheat is on.

#ifndef ZAMN_CHEATS_H
#define ZAMN_CHEATS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "port/cheat.h"

typedef enum {
  CHEAT_INVINCIBLE,
  CHEAT_NEIGHBORS,
  CHEAT_AMMO,
  CHEAT_LIVES,
  CHEAT_GIVE_ALL,
  CHEAT_RUN,
  CHEAT_COUNT,
} CheatId;

typedef struct {
  bool on[CHEAT_COUNT];
  // Named by a flag, on or off, which beats `zamn.ini`'s [cheats].
  bool asked[CHEAT_COUNT];
  // The image has the patches in it and the port has been told. False during
  // the demo.
  bool installed;
  // Give all: owed to this player the next time they are on the board.
  bool owed[2];
} Cheats;

// The flag, without its `--`, and what the startup banner calls it.
static const char* const cheat_flags[CHEAT_COUNT] = {
  "invincible", "invincible-neighbors", "infinite-ammo", "infinite-lives", "give-all", "always-run",
};
static const char* const cheat_names[CHEAT_COUNT] = {
  "invincible", "invincible neighbours", "infinite ammo", "infinite lives",
  "every weapon and item", "always running",
};

static inline void cheats_init(Cheats* c) {
  memset(c, 0, sizeof *c);
  c->owed[0] = c->owed[1] = true;
}

static inline bool cheats_any(const Cheats* c) {
  for (int i = 0; i < CHEAT_COUNT; i++) if (c->on[i]) return true;
  return false;
}

// `--<flag>` and `--no-<flag>`, and the other spelling of a neighbour. True if
// `arg` was one of them.
static inline bool cheats_flag(Cheats* c, const char* arg) {
  if (strncmp(arg, "--", 2) != 0) return false;
  arg += 2;
  bool to = true;
  if (!strncmp(arg, "no-", 3)) { to = false; arg += 3; }
  if (!strcmp(arg, "invincible-neighbours")) arg = cheat_flags[CHEAT_NEIGHBORS];
  for (int i = 0; i < CHEAT_COUNT; i++)
    if (!strcmp(arg, cheat_flags[i])) { c->on[i] = to; c->asked[i] = true; return true; }
  return false;
}

// --- the cartridge's copy -----------------------------------------------------

#define CHEAT_PATCH_MAX 7

typedef struct {
  CheatId cheat;
  uint32_t addr;  // a LoROM address, bank and all
  uint8_t len;
  uint8_t was[CHEAT_PATCH_MAX];
  uint8_t now[CHEAT_PATCH_MAX];
} CheatPatch;

static const CheatPatch cheat_patches[] = {
  // `STA $28`, the store that queues `$80:F9D0`, and the `JMP $E331`.
  {CHEAT_INVINCIBLE, 0x80f9cd, 2, {0x85, 0x28}, {0xea, 0xea}},
  {CHEAT_INVINCIBLE, 0x80f9aa, 3, {0x4c, 0x31, 0xe3}, {0x60, 0x31, 0xe3}},
  // `CMP #$000B : BEQ : CMP #$0003` becomes `CMP #$00FF : BEQ $A3BC : CLC :
  // RTL`: the two players are tested above it, and `$FF` is all that is left.
  {CHEAT_NEIGHBORS, 0x83a372, 7, {0xc9, 0x0b, 0x00, 0xf0, 0x2e, 0xc9, 0x03},
                                 {0xc9, 0xff, 0x00, 0xf0, 0x45, 0x18, 0x6b}},
  // The bubbled neighbour's `CMP #$0003 : BEQ` and `CMP #$0004 : BEQ`.
  {CHEAT_NEIGHBORS, 0x83a26c, 2, {0xf0, 0x37}, {0xea, 0xea}},
  {CHEAT_NEIGHBORS, 0x83a271, 2, {0xf0, 0x32}, {0xea, 0xea}},
  // The tourists' `LDA $1F94 : BNE`.
  {CHEAT_NEIGHBORS, 0x83a012, 2, {0xd0, 0x72}, {0xea, 0xea}},
  // Five `SBC #$0001`s.
  {CHEAT_AMMO, 0x80ed46, 3, {0xe9, 0x01, 0x00}, {0xe9, 0x00, 0x00}},
  {CHEAT_AMMO, 0x80eb57, 3, {0xe9, 0x01, 0x00}, {0xe9, 0x00, 0x00}},
  {CHEAT_AMMO, 0x80ee75, 3, {0xe9, 0x01, 0x00}, {0xe9, 0x00, 0x00}},
  {CHEAT_AMMO, 0x80e90f, 3, {0xe9, 0x01, 0x00}, {0xe9, 0x00, 0x00}},
  {CHEAT_AMMO, 0x80e97f, 3, {0xe9, 0x01, 0x00}, {0xe9, 0x00, 0x00}},
  // `DEC $1D4C,X`.
  {CHEAT_LIVES, 0x80cec5, 3, {0xde, 0x4c, 0x1d}, {0xea, 0xea, 0xea}},
};
#define CHEAT_PATCH_COUNT ((int)(sizeof cheat_patches / sizeof cheat_patches[0]))

static inline size_t cheat_rom_offset(uint32_t addr) {
  return (size_t)(((addr >> 16) & 0x7f) << 15) | (addr & 0x7fff);
}

// The demo's job, as `$80:8418` files it, and the `LDA #$9CB2` that says this
// ROM's is that one.
#define CHEAT_DEMO_JOB_LDA 0x809b7du
#define CHEAT_DEMO_JOB 0x9cb1
#define CHEAT_DEMO_JOB_BANK 0x0080
#define CHEAT_W_JOBS 0x12e0u  // eight of four bytes
#define CHEAT_JOB_COUNT 8

// Does this image have, at every place a cheat that is on would change, what
// this ROM has there? Asked of all of them before any is written, so a
// cartridge that is not the one known is left entirely alone. Returns the
// first cheat that cannot be had, or `CHEAT_COUNT`.
static inline CheatId cheats_rom_check(const Cheats* c, const uint8_t* rom, size_t size) {
  static const uint8_t demo_lda[3] = {0xa9, 0xb2, 0x9c};
  for (int i = 0; i < CHEAT_COUNT; i++) {
    if (!c->on[i]) continue;
    const size_t at = cheat_rom_offset(CHEAT_DEMO_JOB_LDA);
    if (at + sizeof demo_lda > size || memcmp(rom + at, demo_lda, sizeof demo_lda) != 0) return (CheatId)i;
    break;
  }
  for (int i = 0; i < CHEAT_PATCH_COUNT; i++) {
    const CheatPatch* p = &cheat_patches[i];
    if (!c->on[p->cheat]) continue;
    const size_t at = cheat_rom_offset(p->addr);
    if (at + p->len > size || memcmp(rom + at, p->was, p->len) != 0) return p->cheat;
  }
  return CHEAT_COUNT;
}

// Put the patches in or take them out, and tell the port which. Only after
// `cheats_install` has said the image is the one known.
static inline void cheats_patch(Cheats* c, uint8_t* rom, bool in) {
  for (int i = 0; i < CHEAT_PATCH_COUNT; i++) {
    const CheatPatch* p = &cheat_patches[i];
    if (c->on[p->cheat]) memcpy(rom + cheat_rom_offset(p->addr), in ? p->now : p->was, p->len);
  }
  port_cheats.invincible = in && c->on[CHEAT_INVINCIBLE];
  port_cheats.neighbors = in && c->on[CHEAT_NEIGHBORS];
  c->installed = in;
}

// Write them, and tell the port. `rom` is the loaded image, which is the one
// the 65816 fetches from and not the file's. False, and nothing done, if
// `cheats_rom_check` finds something else there.
static inline bool cheats_install(Cheats* c, uint8_t* rom, size_t size) {
  if (cheats_rom_check(c, rom, size) != CHEAT_COUNT) return false;
  cheats_patch(c, rom, true);
  return true;
}

// --- the tick -------------------------------------------------------------------

#define CHEAT_W_HEALTH 0x1cb8u         // by player, doubled
#define CHEAT_W_WEAPON 0x1cbcu
#define CHEAT_W_ITEM 0x1cc0u
#define CHEAT_W_INVENTORY 0x1cccu      // 14 words, and $20 further on for player 2
#define CHEAT_W_ITEMS 0x1d0cu          // 12 words, likewise
#define CHEAT_W_PLAYER_RECORD 0x00d2u  // by player, doubled
#define CHEAT_W_PANEL_ON 0x1e88u       // by player, doubled: is this player in the game
#define CHEAT_W_ACTOR_SLOTS 0x185eu
#define CHEAT_ACTOR_SLOTS_END (CHEAT_W_ACTOR_SLOTS + 32 * 0x14)
#define CHEAT_ACTOR_THREAD 0x0c
#define CHEAT_THREAD_DP_TABLE 0x8082deu  // 24 words
#define CHEAT_THREAD_SLOTS 24
#define CHEAT_DP_PLAYER 0x0e
#define CHEAT_DP_HURT_TIMER 0x52
#define CHEAT_DP_SHOES 0x54
#define CHEAT_DP_INVENTORY 0x64
#define CHEAT_DP_ITEMS 0x66

#define CHEAT_WEAPON_SLOTS 14
#define CHEAT_ITEM_SLOTS 12
#define CHEAT_WEAPON_MAX 0x0999  // BCD, the ceiling `$80:F895  CMP #$0999` keeps
#define CHEAT_ITEM_MAX 0x0099    // ...and `$80:F8F0  CMP #$0099`
#define CHEAT_HEALTH_MAX 0x000a
#define CHEAT_HURT_TIMER 0x0040  // what `$80:F973` re-arms it to
#define CHEAT_SHOES_ON 0x8000
// What `$80:8874` hands a new game.
#define CHEAT_SEED_WEAPON_0 0x0150
#define CHEAT_SEED_ITEM_SLOT 7
#define CHEAT_SEED_ITEM_COUNT 0x0001
// The two item slots nothing in the game can fill, and nothing uses.
#define CHEAT_ITEM_UNUSED(slot) ((slot) == 6 || (slot) == 11)

static inline uint16_t cheat_r16(const uint8_t* ram, uint32_t at) {
  return (uint16_t)(ram[at] | ram[at + 1] << 8);
}
static inline void cheat_w16(uint8_t* ram, uint32_t at, uint16_t v) {
  ram[at] = (uint8_t)v;
  ram[at + 1] = (uint8_t)(v >> 8);
}

// The direct page of player `p` (0 or 1), or 0 if they are not on the board.
static inline uint16_t cheat_player_dp(const uint8_t* ram, const uint8_t* rom, int p) {
  const uint16_t rec = cheat_r16(ram, CHEAT_W_PLAYER_RECORD + (uint32_t)p * 2);
  if (rec < CHEAT_W_ACTOR_SLOTS || rec >= CHEAT_ACTOR_SLOTS_END) return 0;
  const uint16_t slot = cheat_r16(ram, (uint32_t)rec + CHEAT_ACTOR_THREAD);
  if ((slot & 1) || slot >= CHEAT_THREAD_SLOTS * 2) return 0;
  const size_t at = cheat_rom_offset(CHEAT_THREAD_DP_TABLE) + slot;
  const uint16_t dp = (uint16_t)(rom[at] | rom[at + 1] << 8);
  if (dp < 0x0100 || dp > 0x1f00) return 0;
  if (cheat_r16(ram, (uint32_t)dp + CHEAT_DP_PLAYER) != p * 2) return 0;
  if (cheat_r16(ram, (uint32_t)dp + CHEAT_DP_INVENTORY) != CHEAT_W_INVENTORY + p * 0x20) return 0;
  if (cheat_r16(ram, (uint32_t)dp + CHEAT_DP_ITEMS) != CHEAT_W_ITEMS + p * 0x20) return 0;
  return dp;
}

// Is this player's inventory exactly a new game's?
static inline bool cheat_seeded(const uint8_t* ram, int p) {
  const uint32_t weapons = CHEAT_W_INVENTORY + (uint32_t)p * 0x20;
  const uint32_t items = CHEAT_W_ITEMS + (uint32_t)p * 0x20;
  for (int s = 0; s < CHEAT_WEAPON_SLOTS; s++)
    if (cheat_r16(ram, weapons + s * 2) != (s == 0 ? CHEAT_SEED_WEAPON_0 : 0)) return false;
  for (int s = 0; s < CHEAT_ITEM_SLOTS; s++)
    if (cheat_r16(ram, items + s * 2) != (s == CHEAT_SEED_ITEM_SLOT ? CHEAT_SEED_ITEM_COUNT : 0)) return false;
  return true;
}

// Is the title's demo playing?
static inline bool cheat_demo(const uint8_t* ram) {
  for (int j = 0; j < CHEAT_JOB_COUNT; j++)
    if (cheat_r16(ram, CHEAT_W_JOBS + j * 4) == CHEAT_DEMO_JOB &&
        cheat_r16(ram, CHEAT_W_JOBS + j * 4 + 2) == CHEAT_DEMO_JOB_BANK) return true;
  return false;
}

// The tourists' `BNE`, and the room where it has to stay the cartridge's. The
// room's record is looked up in the game's own table (`$80:886D  LDA
// $9F8002,X`) rather than written down.
#define CHEAT_TOURIST_TURN 0x83a012u
#define CHEAT_LEVEL_TABLE 0x9f8002u
#define CHEAT_W_LEVEL_RECORD 0x0c10u
#define CHEAT_ROOM_TOURISTS_TURN 50

// Is the level being played bonus room 50?
static inline bool cheat_tourists_turn(const uint8_t* ram, const uint8_t* rom) {
  const size_t at = cheat_rom_offset(CHEAT_LEVEL_TABLE) + CHEAT_ROOM_TOURISTS_TURN * 2;
  return cheat_r16(ram, CHEAT_W_LEVEL_RECORD) == (uint16_t)(rom[at] | rom[at + 1] << 8);
}

// Put the tourists' patch in or take it out, on its own.
static inline void cheat_tourists_patch(uint8_t* rom, bool in) {
  for (int i = 0; i < CHEAT_PATCH_COUNT; i++) {
    const CheatPatch* p = &cheat_patches[i];
    if (p->addr == CHEAT_TOURIST_TURN) memcpy(rom + cheat_rom_offset(p->addr), in ? p->now : p->was, p->len);
  }
}

// A quick load has put another game in the machine: it is owed what a start is.
static inline void cheats_loaded(Cheats* c) { c->owed[0] = c->owed[1] = true; }

// Once a tick, with the machine stopped, where `poke_apply` is called from.
// `ram` is the console's work RAM and `rom` the loaded image, which
// `cheats_install` has been asked about.
static inline void cheats_tick(Cheats* c, uint8_t* ram, uint8_t* rom) {
  if (!cheats_any(c)) return;
  const bool demo = cheat_demo(ram);
  if (demo == c->installed) cheats_patch(c, rom, !demo);
  if (demo) return;
  if (c->on[CHEAT_NEIGHBORS]) cheat_tourists_patch(rom, !cheat_tourists_turn(ram, rom));
  for (int p = 0; p < 2; p++) {
    const uint32_t weapons = CHEAT_W_INVENTORY + (uint32_t)p * 0x20;
    const uint32_t items = CHEAT_W_ITEMS + (uint32_t)p * 0x20;
    const uint16_t dp = cheat_player_dp(ram, rom, p);

    if (c->on[CHEAT_GIVE_ALL]) {
      // A new game's inventory is given to at once, before the level's title
      // card has gone and the player is on the board, so that the HUD's first
      // count is not the squirt gun's 150. Anything else -- a save, or a game
      // that was running when the cheat's first tick came -- waits for a player
      // to give to. Either way only a player who is in the game: the panel
      // flag is raised at the character select, before `$80:8874` seeds the
      // inventories, and player two's is seeded in a one-player game too --
      // see the header for what filling it did to the ghost potion's HUD.
      const bool in_game = cheat_r16(ram, CHEAT_W_PANEL_ON + (uint32_t)p * 2) != 0;
      const bool seeded = cheat_seeded(ram, p);
      if (in_game && (seeded || (c->owed[p] && dp))) {
        c->owed[p] = false;
        for (int s = 0; s < CHEAT_WEAPON_SLOTS; s++) cheat_w16(ram, weapons + s * 2, CHEAT_WEAPON_MAX);
        for (int s = 0; s < CHEAT_ITEM_SLOTS; s++)
          if (!CHEAT_ITEM_UNUSED(s)) cheat_w16(ram, items + s * 2, CHEAT_ITEM_MAX);
      }
    }
    if (c->on[CHEAT_AMMO]) {
      for (int s = 0; s < CHEAT_WEAPON_SLOTS; s++)
        if (cheat_r16(ram, weapons + s * 2)) cheat_w16(ram, weapons + s * 2, CHEAT_WEAPON_MAX);
      for (int s = 0; s < CHEAT_ITEM_SLOTS; s++)
        if (cheat_r16(ram, items + s * 2)) cheat_w16(ram, items + s * 2, CHEAT_ITEM_MAX);
    }
    if (!dp) continue;
    if (c->on[CHEAT_INVINCIBLE]) {
      cheat_w16(ram, (uint32_t)dp + CHEAT_DP_HURT_TIMER, CHEAT_HURT_TIMER);
      // Not from zero: that is a player already on the way down, which only a
      // save from before the cheat can be.
      const uint16_t health = cheat_r16(ram, CHEAT_W_HEALTH + (uint32_t)p * 2);
      if (health != 0 && health != CHEAT_HEALTH_MAX) cheat_w16(ram, CHEAT_W_HEALTH + (uint32_t)p * 2, CHEAT_HEALTH_MAX);
    }
    if (c->on[CHEAT_RUN] && cheat_r16(ram, (uint32_t)dp + CHEAT_DP_SHOES) == 0)
      cheat_w16(ram, (uint32_t)dp + CHEAT_DP_SHOES, CHEAT_SHOES_ON);
  }
}

// What the startup banner says.
static inline void cheats_print(const Cheats* c) {
  if (!cheats_any(c)) return;
  printf("Cheats: ");
  bool first = true;
  for (int i = 0; i < CHEAT_COUNT; i++)
    if (c->on[i]) { printf("%s%s", first ? "" : ", ", cheat_names[i]); first = false; }
  printf(".\n");
}

#endif  // ZAMN_CHEATS_H
