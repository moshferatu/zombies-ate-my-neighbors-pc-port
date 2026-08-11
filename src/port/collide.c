#include "port/collide.h"

#include <string.h>

#include "port/apu.h"
#include "port/bcd.h"
#include "port/coverage.h"
// For `weapon_select_next`: a pickup by a player holding nothing tail-calls
// into the weapon selector, which is not collision code and lives on its own.
#include "port/player.h"
// For `ACTOR_COLLIDE_ID`: `shot_collide` writes the same display-record field
// the sprite pass reads, which is the first time a handler reaches out of its
// own direct page into the game's shared data structure.
#include "port/oam.h"
// For `rng_next`: `enemy_d301_collide` is the first handler in the project that
// draws a random number, and the carry it hands the generator is one it got back
// from `enemy_survived_react`.
#include "port/rng.h"
#include "port/score.h"
#include "port/thread.h"

// ---------------------------------------------------------------------------
// $80:F950  the player's hit path
// ---------------------------------------------------------------------------

// Fifteen instructions, five words, and the entry 1,225 of the 1,225 player
// dispatches on `movies/level1-rescue.zmv` land on. Three separate ways of
// deciding the hit does not count, and then two stores that say it did.
//
// `r` comes in holding the registers as `player_collide` left them at its
// `JSR ($F808,X)`, and every exit here is a branch to the same `RTS`, so which
// one was taken is exactly what decides A, X and the flags.
static void player_collide_hurt(Wram* w, uint16_t dp, ActorHandlerRegs* r,
                                uint16_t* blk) {
  blk[HURT_BLK_RTS]++;  // every exit below reaches the same $80:F978

  // `$80:F950  LDA $70 : CMP #$0002 : BEQ : CMP #$0004 : BEQ`. Two of the
  // player's states ignore collisions outright. Either comparison that matches
  // leaves zero behind, so both exits carry the same flags — but they are
  // reached a comparison apart, so the two are counted apart.
  uint16_t state = wram_r16(w, (uint32_t)dp + ACTOR_DP_STATE);
  r->a = state;
  if (state == 2 || state == 4) {
    PORT_COVER(hurt_state_immune);
    blk[state == 2 ? HURT_BLK_STATE_A : HURT_BLK_STATE_B]++;
    r->n = false;
    r->z = true;
    return;
  }
  blk[HURT_BLK_STATE_PASS]++;

  // `$80:F95C  LDX $0E : LDA $1CBC,X : CMP #$0004 : BNE`. The absolute read is
  // unambiguous whatever the data bank holds — `sprite_build_oam` sets it to $80
  // with `PHK : PLB` and bank $80's low half is the WRAM mirror, so `$1CBC` is
  // `$7E:1CBC` either way.
  uint16_t player = wram_r16(w, (uint32_t)dp + ACTOR_DP_PLAYER);
  r->x = player;
  uint16_t weapon = wram_r16(w, W_PLAYER_WEAPON + player);
  r->a = weapon;
  if (weapon == PLAYER_WEAPON_IMMUNE) {
    blk[HURT_BLK_WEAPON_MATCH]++;
    // `$80:F966  LDA $1E : BNE`. Holding that one weapon with `$1E` set is the
    // second way out, and it is the one branch here no input has ever taken.
    uint16_t health = wram_r16(w, (uint32_t)dp + ACTOR_DP_HEALTH);
    r->a = health;
    if (health != 0) {
      PORT_COVER(hurt_weapon_immune);
      blk[HURT_BLK_HEALTH_SET]++;
      r->n = (health & 0x8000) != 0;
      r->z = false;
      return;
    }
    blk[HURT_BLK_HEALTH_ZERO]++;
  } else {
    blk[HURT_BLK_WEAPON_OTHER]++;
  }
  blk[HURT_BLK_TIMER]++;

  // `$80:F96A  LDA $52 : BPL`. The hit-recovery timer: a hit only lands once it
  // has run past zero into the sign bit.
  uint16_t timer = wram_r16(w, (uint32_t)dp + ACTOR_DP_HURT_TIMER);
  r->a = timer;
  if (!(timer & 0x8000)) {
    PORT_COVER(hurt_iframes);
    blk[HURT_BLK_IFRAMES]++;
    r->n = false;
    r->z = timer == 0;
    return;
  }

  // The two stores that are the whole point of the routine: post the event for
  // the player's own code to pick up, and start the recovery timer again.
  PORT_COVER(hurt_taken);
  blk[HURT_BLK_TAKEN]++;
  wram_w16(w, (uint32_t)dp + ACTOR_DP_EVENT, 0x8001);
  wram_w16(w, (uint32_t)dp + ACTOR_DP_HURT_TIMER, PLAYER_HURT_TIMER_RESET);
  // `LDA #$0040` is the last instruction to set a flag; `STA` sets none.
  r->a = PLAYER_HURT_TIMER_RESET;
  r->n = false;
  r->z = false;
}

// ---------------------------------------------------------------------------
// $80:F92D  the entry whose whole reaction is a noise
// ---------------------------------------------------------------------------

// `LDA #$0009 : JSL apu_play_sfx : RTS` — three instructions, and the smallest
// thing that has ever been on PROGRESS.md's work list. It writes nothing but
// the APU sequence counter and it was blocked on nothing but the decision
// `port/apu.h` now records.
static void player_sfx(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  ApuSfxRegs a;
  apu_play_sfx(w, PLAYER_SFX_TOUCH, dp, &a);
  r->a = a.a;
  r->x = a.x;
  r->y = a.y;
  // The `PLD` inside `apu_play_sfx` is the last flag-setting instruction on the
  // way to this `RTS`, so what the player's handler returns describes the
  // player's own direct page. Carry is not carried over: `$80:F806  CLC`
  // overwrites it two instructions later, which is why `player_collide` sets it
  // for every path rather than each path setting its own.
  r->n = a.n;
  r->z = a.z;
}

// ---------------------------------------------------------------------------
// $80:F87B  a pickup, from the player's side
// ---------------------------------------------------------------------------

// The other half of `object_collide`. The object manager switches the touched
// object off and queues it; *this* is what the player does about it, and the
// two run one after the other on the same collision — the two dispatches
// `actor_collide_notify` makes for one pair.
//
// `index` is the other actor's collision id **already doubled**, because
// `$80:F7FC  ASL A : TAX` did that to index the jump table and `PHX : ... :
// PLA` carries the doubled value across the sound effect and back into A. Ids
// $0C..$20 all land here, one per item type, and the item's identity is
// entirely the id: it picks both the inventory slot and how much of it arrives.
//
// This is the game's second decimal routine after `score_add`, and the reason
// the APU decision was worth making rather than working around — the amount is
// added with `SED` on, so behind the sound effect there was real arithmetic
// that no movie could diff.
static void player_pickup(Wram* w, const Rom* rom, uint16_t dp, uint16_t index,
                          ActorHandlerRegs* r) {
  // `PHX : LDA #$000E : JSL apu_play_sfx : PLA`. Nothing the sound leaves in a
  // register survives — the `PLA`, the `TAX` and the `TAY` below overwrite all
  // three — so only its WRAM effect matters here.
  ApuSfxRegs sfx;
  apu_play_sfx(w, PLAYER_SFX_PICKUP, dp, &sfx);
  (void)sfx;

  // `SEC : SBC #$0018 : TAX`. $18 is the *doubled* id of the first item type,
  // so what this produces is the item's slot as a byte offset — which is what
  // both of the two things below want, because both are arrays of words.
  //
  // **This line is checked now, and it was not.** Every movie that existed
  // before `movies/level1-pickups.zmv` picked up exactly one item and it was
  // id $0C, the first — so the slot was 0, and every wrong way of computing it
  // is also 0. Writing `index / 2 - PICKUP_ID_FIRST` instead passed all
  // 138,513 calls, and no coverage site could name it, because it is not a
  // branch. The fix was an input, not code: level 1's object 6 is id $12, so
  // the two spellings disagree ($0C against $06) and the wrong one fails on
  // its first call at `$7E:1CD3` — the *high* byte of the neighbouring
  // inventory word, because slot $06 also reads the wrong amount out of the
  // table ($0300 where $0020 belongs).
  uint16_t slot = (uint16_t)(index - PICKUP_ID_FIRST * 2);

  // `CLC : ADC $64 : TAY`, then `LDA $0000,Y`. The data bank is $80, whose low
  // half is the WRAM mirror, so this is an absolute WRAM address and `$64`
  // holds the base of *this* player's inventory. That the base really is
  // `W_PLAYER_INVENTORY` is not read off the listing — it is the two words at
  // `$80:EAA4`, which `$80:EA63` indexes with the doubled player number — and
  // the diff is what settles it, because a wrong base lands the store on the
  // wrong word of WRAM.
  uint16_t at = (uint16_t)(slot + wram_r16(w, (uint32_t)dp + ACTOR_DP_INVENTORY));

  // `SED : CLC : ADC $F8AC,X`. The table is in bank $80 above $8000, so it is
  // ROM and read as such — a Necrofy-style hack that retunes what a pickup is
  // worth works in the port for free.
  bool carry = false;
  bool adjusted = false;
  uint16_t sum = bcd_add16(wram_r16(w, at),
                           rom_word(rom, PICKUP_AMOUNT_TABLE + slot), &carry,
                           &adjusted);
  if (adjusted) PORT_COVER(pickup_digit_carry);

  // `CMP #$0999 : BCC : LDA #$0999`. Three BCD digits and a ceiling, which is
  // what an ammo counter on the HUD has room for. That `CMP` is the last thing
  // in the routine to touch carry and nothing here reads it, because
  // `$80:F806  CLC` overwrites it on the way out.
  //
  // Still transcribed rather than diffed: deleting the ceiling passes every
  // call on every movie, and `pickup_capped` says so by reading zero. Level 1
  // pays $0099 twice into a counter that starts at $0150, which is nowhere
  // near $0999 — this one wants a long session rather than a route.
  if (sum >= PICKUP_MAX) {
    PORT_COVER(pickup_capped);
    sum = PICKUP_MAX;
  }
  wram_w16(w, at, sum);

  // `CLD : LDY $0E : LDA $1CBC,Y : BPL <rts>`. Picking something up while
  // holding no weapon falls into `$80:EA63`, which finds one and selects it —
  // and *that* is the path the one pickup on `movies/level1-2p-rescue.zmv`
  // takes, so the ordinary exit below is the one no input has reached.
  //
  // A `JMP`, not a `JSR`: what `$80:EA63` returns is what this routine returns,
  // registers and flags included.
  uint16_t player = wram_r16(w, (uint32_t)dp + ACTOR_DP_PLAYER);
  uint16_t weapon = wram_r16(w, W_PLAYER_WEAPON + player);
  if (weapon & 0x8000) {
    PORT_COVER(pickup_autoselect);
    WeaponSelectRegs sel;
    weapon_select_next(w, rom, dp, &sel);
    r->a = sel.a;
    r->x = sel.x;
    r->y = sel.y;
    r->n = sel.n;
    r->z = sel.z;
    return;
  }

  PORT_COVER(pickup_taken);
  r->a = weapon;
  r->x = slot;
  r->y = player;
  // That `LDA` is the last flag-setting instruction, and the `BPL` was taken.
  r->n = false;
  r->z = weapon == 0;
}

// ---------------------------------------------------------------------------
// $80:F8D6  the other pickup — an item rather than a weapon
// ---------------------------------------------------------------------------

// Forty-nine bytes that are `$80:F87B` again with a different array, and the
// interesting part is the list of what changed: the base is `$66` rather than
// `$64`, the first id is $21 rather than $0C, the amounts come from `$80:F907`
// rather than `$80:F8AC`, the ceiling is `$0099` rather than `$0999`, and the
// tail call is into the *item* selector. Everything else — the `PHX`/`PLA`
// around the sound effect, `SED : CLC : ADC`, the `CMP`/`BCC` cap, the `CLD`,
// the `LDY $0E : LDA <selected>,Y : BPL` — is instruction for instruction the
// same. Two arrays, one routine, written twice.
//
// `index` is the collision id already doubled, exactly as next door.
static void player_item_pickup(Wram* w, const Rom* rom, uint16_t dp,
                               uint16_t index, ActorHandlerRegs* r) {
  // `PHX : LDA #$000E : JSL apu_play_sfx : PLA`, and it is the same sound id as
  // the weapon pickup's — so the two are indistinguishable by ear as well as by
  // listing.
  ApuSfxRegs sfx;
  apu_play_sfx(w, PLAYER_SFX_PICKUP, dp, &sfx);
  (void)sfx;

  // `SEC : SBC #$0042 : TAX`, the doubled first id.
  //
  // **This line is in exactly the position its twin was in before
  // `movies/level1-pickups.zmv`, and for once an input cannot get it out.**
  // Writing `index / 2 - ITEM_ID_FIRST` instead passes every call on every
  // movie, because level 1 has two ids that reach here, `$21` (three keys) and
  // `$28` (one object), and both spellings agree on `$21`, whose slot is 0. The
  // $28 is object 8 at (1208,71), which sits on the far side of a fence in a
  // part of the map the player cannot stand in — so the distinguishing input
  // does not exist in this level, rather than merely not having been written.
  // Recorded here, like the `STZ $7E` below, because a mark cannot express it.
  uint16_t slot = (uint16_t)(index - ITEM_ID_FIRST * 2);

  // `CLC : ADC $66 : TAY`, then `LDA $0000,Y` through bank $80's WRAM mirror.
  uint16_t at = (uint16_t)(slot + wram_r16(w, (uint32_t)dp + PLAYER_DP_ITEMS));

  // `SED : CLC : ADC $F907,X`.
  bool carry = false;
  bool adjusted = false;
  uint16_t sum = bcd_add16(wram_r16(w, at),
                           rom_word(rom, ITEM_AMOUNT_TABLE + slot), &carry,
                           &adjusted);
  if (adjusted) PORT_COVER(item_digit_carry);

  // `CMP #$0099 : BCC : LDA #$0099`. Two digits, and the `CMP` runs with `SED`
  // still on — which changes nothing, because comparison on the 65816 ignores
  // the decimal flag.
  if (sum >= ITEM_MAX) {
    PORT_COVER(item_pickup_capped);
    sum = ITEM_MAX;
  }
  wram_w16(w, at, sum);

  // `CLD : LDY $0E : LDA $1CC0,Y : BPL <rts>`, and the `JMP $EAA8` under it.
  // The first key a player picks up takes the auto-select; the second finds an
  // item already selected and leaves through the `RTS` — which is why one key
  // would have checked half of this and two check all of it.
  uint16_t player = wram_r16(w, (uint32_t)dp + ACTOR_DP_PLAYER);
  uint16_t item = wram_r16(w, W_PLAYER_ITEM + player);
  if (item & 0x8000) {
    PORT_COVER(item_autoselect);
    WeaponSelectRegs sel;
    item_select_next(w, rom, dp, &sel);
    r->a = sel.a;
    r->x = sel.x;
    r->y = sel.y;
    r->n = sel.n;
    r->z = sel.z;
    return;
  }

  PORT_COVER(item_taken);
  r->a = item;
  r->x = slot;
  r->y = player;
  r->n = false;
  r->z = item == 0;
}


// ---------------------------------------------------------------------------
// $80:FA26 / $80:FA4A / $80:FA79 / $80:FAA4  the four that spawn something
// ---------------------------------------------------------------------------

// One routine written four times, which is the third time this file has said
// that -- `$80:F8D6` was `$80:F87B` again and these are each other. The shared
// half is a sound, two words of position copied to the top of the player's own
// page, a **kind** under them, and `thread_spawn`; what differs is one tail.
//
// `kind` is the `$04` each one writes, and it is the only thing the spawned
// thread has to tell it which of the four items it is.
static void player_spawn(Wram* w, const Rom* rom, uint16_t dp, uint16_t kind,
                         ActorHandlerRegs* r, ApuSfxRegs* sfx_out) {
  // `LDA #$0009 : JSL apu_play_sfx`. The touch sound, not the pickup one -- so
  // these four are audibly a different kind of thing from `$80:F87B`.
  ApuSfxRegs sfx;
  apu_play_sfx(w, PLAYER_SFX_TOUCH, dp, &sfx);

  // `LDA $30 : STA $00`, `LDA $32 : STA $02`, `LDA #<kind> : STA $04`. The three
  // words `thread_spawn` will copy onto the new thread's page.
  wram_w16(w, (uint32_t)dp + 0x00, wram_r16(w, (uint32_t)dp + PLAYER_DP_SPAWN_X));
  wram_w16(w, (uint32_t)dp + 0x02, wram_r16(w, (uint32_t)dp + PLAYER_DP_SPAWN_Y));
  wram_w16(w, (uint32_t)dp + PLAYER_DP_SPAWN_ARG, kind);

  // `LDA #$E0B4 : LDY #$0082 : JSL thread_spawn`.
  int slot = thread_spawn(w, rom, PLAYER_SPAWN_BODY, PLAYER_SPAWN_BANK, dp);
  r->a = slot < 0 ? 0 : (uint16_t)slot;
  r->x = slot < 0 ? 0xfffe : (uint16_t)slot;
  r->y = (THREAD_SPAWN_ARGS - 1) * 2;
  r->n = (r->a & 0x8000) != 0;
  r->z = r->a == 0;
  // Nothing in `thread_spawn` touches carry, so what reaches the tails below is
  // still the sound effect's -- which matters, because two of them rotate it
  // into the top of a word.
  *sfx_out = sfx;
}

// `LDX #$0500 : LDA $0C : ROR A : ROR A : ROR A : JSL score_add`, and the three
// rotates are the interesting instruction in all five of these routines.
//
// `$0C` is the player's number already doubled -- 0 or 2 -- and `score_add`
// reads **bit 15 of A** to decide whose points these are. Rotating a 2 right
// three times through carry puts its bit 1 into bit 15; rotating a 0 leaves bit
// 15 clear. So three `ROR`s are how a player index becomes a sign, and the carry
// that shifts in on the way lands in bit 13, where nothing reads it. It still
// has to be reproduced exactly, because the *diff* reads it.
static void player_spawn_score(Wram* w, const Rom* rom, uint16_t dp,
                               uint16_t award, bool carry_in,
                               ActorHandlerRegs* r) {
  uint16_t a = wram_r16(w, (uint32_t)dp + PLAYER_DP_TABLE_INDEX);
  bool c = carry_in;
  for (int i = 0; i < 3; i++) {
    bool out = (a & 1) != 0;
    a = (uint16_t)((a >> 1) | (c ? 0x8000 : 0));
    c = out;
  }
  ScoreResult score;
  if (!score_add(w, rom, (a & 0x8000) != 0, award, c, &score)) return;
  r->a = score.a;
  r->x = score.x;
  // Y is untouched from the spawn: nothing in `score_add` writes it.
  r->n = score.n;
  r->z = score.z;
  r->c = score.c;
}
// ---------------------------------------------------------------------------
// $80:DC09  the tail of $80:F9AE
// ---------------------------------------------------------------------------

// Reached by `JMP`, so this is the rest of the jump-table entry's call rather
// than a call of its own — which is the whole reason it is ported here instead
// of being named as a decline. Three guards, all of which return having written
// nothing, and one store.
static void player_state_tail(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  // `$80:DC09  LDA $52 : BPL $DC1D`. Still inside the invulnerability window
  // `ACTOR_DP_HURT_TIMER` counts, so nothing happens. Note the sense: the timer
  // has to have gone *negative* for anything below to run, which is the same
  // test `$80:F96C` makes before letting a hit land at all.
  uint16_t timer = wram_r16(w, (uint32_t)dp + ACTOR_DP_HURT_TIMER);
  r->a = timer;
  r->n = (timer & 0x8000) != 0;
  r->z = timer == 0;
  if (!(timer & 0x8000)) {
    PORT_COVER(state_tail_recovering);
    return;
  }

  // `LDA $70 : BNE $DC1D`. Any state but zero declines — so between this and the
  // caller's two comparisons, only state 0 reaches the store.
  uint16_t state = wram_r16(w, (uint32_t)dp + ACTOR_DP_STATE);
  r->a = state;
  r->n = (state & 0x8000) != 0;
  r->z = state == 0;
  if (state != 0) {
    PORT_COVER(state_tail_busy);
    return;
  }

  // `LDA $10 : CMP #$FD72 : BEQ $DC1D`. A sentinel compare; see
  // `PLAYER_STATE_TAIL_SENTINEL` for how little is known about what it means.
  uint16_t word = wram_r16(w, (uint32_t)dp + PLAYER_DP_TAIL_WORD);
  uint16_t diff = (uint16_t)(word - PLAYER_STATE_TAIL_SENTINEL);
  r->a = word;
  r->n = (diff & 0x8000) != 0;
  r->z = diff == 0;
  if (word == PLAYER_STATE_TAIL_SENTINEL) {
    PORT_COVER(state_tail_sentinel);
    return;
  }

  // `LDA #$DC1E : STA $28`. The one thing this routine can do: queue what the
  // player's own code runs next. `LDA` of a constant is the last flag-setter.
  PORT_COVER(state_tail_queued);
  wram_w16(w, (uint32_t)dp + PLAYER_DP_NEXT, PLAYER_STATE_TAIL_NEXT);
  r->a = PLAYER_STATE_TAIL_NEXT;
  r->n = (PLAYER_STATE_TAIL_NEXT & 0x8000) != 0;
  r->z = false;
}

// ---------------------------------------------------------------------------
// $80:F7F7  player_collide
// ---------------------------------------------------------------------------

bool player_collide_counted(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                            ActorHandlerRegs* r, uint32_t* unported,
                            PlayerCollideWork* work) {
  memset(work, 0, sizeof *work);

  // `$80:F7F7  CMP #$005C : BCS $F806`, and `$F806` is `CLC : RTL`. An id at or
  // above the player's own side belongs to the other half of the pair, so this
  // returns having read one word and written none.
  if (arg >= COLLIDE_ID_PLAYER) {
    PORT_COVER(player_ignore);
    work->blocks[PLAYER_BLK_IGNORE]++;
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = diff == 0;
    r->c = false;
    return true;
  }

  // `$80:F7FC  ASL A : TAX : LDA $0076 : STA $58`. `$76` is absolute — it is
  // the pair `actor_collide_notify` just published — and `$58` is direct page,
  // so this files the other record on the player's own page for its per-frame
  // code to find. Both happen before the dispatch, and both survive it.
  work->blocks[PLAYER_BLK_DISPATCH]++;
  uint16_t index = (uint16_t)(arg * 2);
  uint16_t other = wram_r16(w, W_HANDLER_OTHER);
  r->x = index;
  r->a = other;
  r->n = (other & 0x8000) != 0;
  r->z = other == 0;
  wram_w16(w, (uint32_t)dp + ACTOR_DP_COLLIDER, other);

  // `$80:F803  JSR ($F808,X)`. A same-bank indirect jump, so every target is in
  // bank $80 and the table is read straight out of ROM rather than transcribed
  // — which is also what makes a ROM hack's table work.
  uint16_t target = rom_word(rom, PLAYER_COLLIDE_TABLE + (uint32_t)index);
  work->target = target;
  switch (target) {
    case PLAYER_COLLIDE_NOP:
      // `$80:F87A` is a bare `RTS`. This id does nothing to the player, and the
      // registers are whatever the two instructions above left.
      PORT_COVER(player_no_effect);
      break;
    case PLAYER_COLLIDE_HURT:
      PORT_COVER(player_hurt_entry);
      player_collide_hurt(w, dp, r, work->hurt);
      break;
    case PLAYER_COLLIDE_SFX:
      PORT_COVER(player_sfx_only);
      player_sfx(w, dp, r);
      break;
    case PLAYER_COLLIDE_PICKUP:
      PORT_COVER(player_pickup_entry);
      player_pickup(w, rom, dp, index, r);
      break;
    case PLAYER_COLLIDE_ITEM:
      PORT_COVER(player_item_entry);
      player_item_pickup(w, rom, dp, index, r);
      break;
    case PLAYER_COLLIDE_SPAWN_0: {
      // `$80:FA26`'s tail: `LDX $0E : INC $1FF0,X`, and nothing bounds it.
      PORT_COVER(player_spawn_0);
      ApuSfxRegs sfx;
      player_spawn(w, rom, dp, 0, r, &sfx);
      // The `LDX` is an output as well as an index: X comes back holding the
      // player, not the slot `thread_spawn` just handed out. Its twin below is
      // where the diff caught this — `X: ROM $0000, port $0022`.
      uint16_t player = wram_r16(w, (uint32_t)dp + ACTOR_DP_PLAYER);
      r->x = player;
      uint16_t at = (uint16_t)(W_PLAYER_SPAWN_COUNT + player);
      uint16_t n = (uint16_t)(wram_r16(w, at) + 1);
      wram_w16(w, at, n);
      // `INC` is the last flag-setting instruction and it describes the counter.
      r->n = (n & 0x8000) != 0;
      r->z = n == 0;
      break;
    }
    case PLAYER_COLLIDE_SPAWN_1: {
      // `$80:FA4A`'s tail is the same counter idea with a ceiling of five, and
      // the `CMP` that refuses is what the flags come back as.
      PORT_COVER(player_spawn_1);
      ApuSfxRegs sfx;
      player_spawn(w, rom, dp, 1, r, &sfx);
      uint16_t player = wram_r16(w, (uint32_t)dp + ACTOR_DP_PLAYER);
      r->x = player;
      uint16_t at = (uint16_t)(W_PLAYER_CAPPED_COUNT + player);
      uint16_t n = wram_r16(w, at);
      uint16_t diff = (uint16_t)(n - PLAYER_CAPPED_MAX);
      if (n >= PLAYER_CAPPED_MAX) {
        PORT_COVER(spawn_count_capped);
        r->a = n;
        r->n = (diff & 0x8000) != 0;
        r->z = diff == 0;
        break;
      }
      n = (uint16_t)(n + 1);
      wram_w16(w, at, n);
      r->a = n;
      r->n = (n & 0x8000) != 0;
      r->z = n == 0;
      break;
    }
    case PLAYER_COLLIDE_SPAWN_2: {
      PORT_COVER(player_spawn_2);
      ApuSfxRegs sfx;
      player_spawn(w, rom, dp, 2, r, &sfx);
      player_spawn_score(w, rom, dp, PLAYER_SPAWN_AWARD_2, sfx.c, r);
      return true;  // `score_add` set carry; `$80:F806  CLC` is not reached
    }
    case PLAYER_COLLIDE_SPAWN_3: {
      PORT_COVER(player_spawn_3);
      ApuSfxRegs sfx;
      player_spawn(w, rom, dp, 3, r, &sfx);
      player_spawn_score(w, rom, dp, PLAYER_SPAWN_AWARD_3, sfx.c, r);
      return true;
    }
    case PLAYER_COLLIDE_STATE_GATE: {
      // `$80:F9AE  LDA $70 : CMP #$0002 : BEQ : CMP #$0004 : BEQ : JMP $DC09`.
      // The states-2-and-4 guard, written out longhand, and the last entry in
      // this table any input has ever reached — one call in the whole corpus.
      PORT_COVER(player_state_gate);
      uint16_t state = wram_r16(w, (uint32_t)dp + ACTOR_DP_STATE);
      r->a = state;
      if (state == PLAYER_STATE_IGNORE_A || state == PLAYER_STATE_IGNORE_B) {
        // `$80:F9BD  RTS`, with the flags of whichever `CMP` matched — and a
        // match is equality, so Z is set either way and N is clear.
        PORT_COVER(player_state_ignored);
        r->n = false;
        r->z = true;
        break;
      }
      // `JMP $DC09`, not `JSR`: what follows is the rest of *this* call, so it
      // is ported here rather than declined. Flags on the way in are the second
      // `CMP`'s, and `$80:DC09` opens with an `LDA` that replaces them.
      player_state_tail(w, dp, r);
      break;
    }
    case PLAYER_COLLIDE_QUEUE: {
      // `$80:F9BE`, id `$0A`, and it is the entry above with `$80:DC09`'s one
      // store inlined instead of jumped to — same two state tests, and then
      // `LDA #$F9D0 : STA $28` unconditionally rather than behind three more
      // guards.
      PORT_COVER(player_queue_entry);
      uint16_t state = wram_r16(w, (uint32_t)dp + ACTOR_DP_STATE);
      r->a = state;
      if (state == PLAYER_STATE_IGNORE_A || state == PLAYER_STATE_IGNORE_B) {
        PORT_COVER(player_queue_ignored);
        r->n = false;
        r->z = true;
        break;
      }
      PORT_COVER(player_queue_next);
      wram_w16(w, (uint32_t)dp + PLAYER_DP_NEXT, PLAYER_QUEUE_NEXT);
      // `LDA #$F9D0` is the last instruction to set a flag; the `STA` sets none.
      r->a = PLAYER_QUEUE_NEXT;
      r->n = (PLAYER_QUEUE_NEXT & 0x8000) != 0;
      r->z = false;
      break;
    }
    case PLAYER_COLLIDE_HURT_ALT: {
      // `$80:F979`, id `$0B`. The only one of the four that writes something
      // other than a next-routine pointer, and what it writes is a hit —
      // `$80:F950`'s two stores with both constants changed.
      PORT_COVER(player_hurt_alt);
      // `LDA $52 : BPL` — the invulnerability window, tested before the state is
      // even read, which is the opposite order from `$80:DC09`'s.
      uint16_t timer = wram_r16(w, (uint32_t)dp + ACTOR_DP_HURT_TIMER);
      r->a = timer;
      if (!(timer & 0x8000)) {
        PORT_COVER(player_hurt_alt_recovering);
        r->n = false;  // `BPL` was taken, so bit 15 is clear
        r->z = timer == 0;
        break;
      }
      uint16_t state = wram_r16(w, (uint32_t)dp + ACTOR_DP_STATE);
      r->a = state;
      if (state == PLAYER_STATE_IGNORE_A || state == PLAYER_STATE_IGNORE_B ||
          state == PLAYER_STATE_IGNORE_C) {
        // Three states here rather than the other three entries' two, and the
        // third — `$0E` — appears in no other member of the group.
        PORT_COVER(player_hurt_alt_ignored);
        r->n = false;
        r->z = true;
        break;
      }
      // `LDA #$C000 : STA $50` then `LDA #$0030 : STA $52`.
      PORT_COVER(player_hurt_alt_taken);
      wram_w16(w, (uint32_t)dp + ACTOR_DP_EVENT, PLAYER_EVENT_HURT_ALT);
      wram_w16(w, (uint32_t)dp + ACTOR_DP_HURT_TIMER, PLAYER_HURT_ALT_TIMER);
      // The second `LDA` is the last flag-setter, and `$0030` is small and
      // positive — so N comes back clear even though the *event* just written is
      // negative. Worth the line: taking N from `$C000` would be the natural
      // mistake and the diff would catch it only on this branch.
      r->a = PLAYER_HURT_ALT_TIMER;
      r->n = false;
      r->z = false;
      break;
    }
    case PLAYER_COLLIDE_HEAL: {
      // `$80:FACF`, the only entry in the table that gives health back. Three
      // of it, ceilinged at the same ten `$80:EB2F` refuses to spend a kit at --
      // and refusing outright when you are already there, which is why walking
      // over one at full health is silent.
      PORT_COVER(player_heal_entry);
      uint16_t player = wram_r16(w, (uint32_t)dp + ACTOR_DP_PLAYER);
      uint16_t at = (uint16_t)(W_PLAYER_HEALTH + player);
      uint16_t health = wram_r16(w, at);
      r->x = player;
      if (health == PLAYER_HEALTH_MAX) {
        PORT_COVER(heal_at_full);
        r->a = health;
        r->n = false;
        r->z = true;
        break;
      }
      uint16_t sum = (uint16_t)(health + PLAYER_HEAL_AMOUNT);
      if (sum >= PLAYER_HEALTH_MAX) {
        PORT_COVER(heal_capped);
        sum = PLAYER_HEALTH_MAX;
      }
      wram_w16(w, at, sum);
      ApuSfxRegs sfx;
      apu_play_sfx(w, PLAYER_SFX_HEAL, dp, &sfx);
      r->a = sfx.a;
      r->x = sfx.x;
      r->y = sfx.y;
      r->n = sfx.n;
      r->z = sfx.z;
      break;
    }
    default:
      // Everything else is a routine nobody has ported, and ten of the 57
      // entries now are. Name the entry rather than the id when saying so: a
      // routine is the piece of work, however many ids share it.
      PORT_COVER(player_unported);
      if (unported) *unported = 0x800000u | target;
      return false;
  }

  r->c = false;  // the `CLC` at $80:F806, on both paths
  return true;
}

bool player_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                    ActorHandlerRegs* r, uint32_t* unported) {
  PlayerCollideWork work;
  return player_collide_counted(w, rom, dp, arg, r, unported, &work);
}

// ---------------------------------------------------------------------------
// $81:8888  enemy_collide
// ---------------------------------------------------------------------------

// `$81:8727`, the four instructions a kill is worth.
//
// It is reached only from the branch below, by `JSR`, and everything it does is
// two stores in different places: points on the *player's* side of the game, and
// one word on the dying enemy's own page which its thread body will find on its
// next pass and use to take itself apart.
static bool enemy_die(Wram* w, const Rom* rom, uint16_t dp,
                      ActorHandlerRegs* r) {
  // `$81:8727  LDX #$0100 : LDA $22 : JSL $80C7D9`. The award is a constant and
  // the argument is the id parked on entry — only its sign is read, and it says
  // which player's weapon this was.
  uint16_t id = wram_r16(w, (uint32_t)dp + ACTOR_DP_HIT_ID);
  ScoreResult score;
  if (!score_add(w, rom, (id & 0x8000) != 0, ENEMY_DEATH_AWARD, r->c, &score))
    return false;
  r->x = score.x;
  r->c = score.c;

  // `$81:8730  LDA #$F5F5 : STA $12`. The `LDA` is the last instruction here to
  // set a flag, and `STA` sets none.
  wram_w16(w, (uint32_t)dp + ACTOR_DP_DEATH_REQ, ENEMY_DEATH_REQUEST);
  r->a = ENEMY_DEATH_REQUEST;
  r->n = true;
  r->z = false;
  return true;
}

bool enemy_collide_counted(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                           ActorHandlerRegs* r, uint32_t* unported,
                           EnemyCollideWork* work) {
  memset(work->blocks, 0, sizeof work->blocks);
  if (arg < COLLIDE_ID_PLAYER) {
    // `$81:8888  CMP #$005C : BCS : CLC : RTL`. The comparison borrowed, so N is
    // set on every call that gets here and Z never is. Nothing is read and
    // nothing is written, which is the finding rather than an oversight: an
    // enemy told about a non-player collision touches nothing at all.
    PORT_COVER(enemy_ignore);
    work->blocks[ENEMY_BLK_IGNORE]++;
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$81:888F  STA $22 : AND #$7FFF`. The raw id is parked first, sign bit and
  // all, because that is what `enemy_die` reads to decide whose points these
  // are; only the lookups below use the masked value.
  PORT_COVER(enemy_act);
  wram_w16(w, (uint32_t)dp + ACTOR_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;

  // `$81:8894  CMP #$005E : BEQ` and `$81:8899  CMP #$005D : BEQ`, both `JML`s
  // into routines of their own — and the two are no longer symmetrical. `$5D`
  // goes to `enemy_freeze`, which the port has; `$5E` goes to `$81:83C6`, which
  // it does not, because **no input fires that weapon**. The two sites are
  // separate for exactly that reason: one of them is now the busiest branch in
  // this routine on `movies/level17-weapon.zmv` and the other has still never
  // been reached.
  if (id == ENEMY_HIT_SPECIAL_B) {
    PORT_COVER(enemy_hit_freeze);
    work->blocks[ENEMY_BLK_DEEP]++;
    return enemy_freeze(w, dp, r);
  }
  if (id == ENEMY_HIT_SPECIAL_A) {
    PORT_COVER(enemy_hit_special);
    work->blocks[ENEMY_BLK_DEEP]++;
    return enemy_bubble_react(w, dp, r);
  }

  // `$81:889E  SEC : SBC #$005C : ASL A : TAX`, then `SEC : LDA $1E : SBC
  // $818561,X`. Binary, not decimal — the score is the only thing in this path
  // that is BCD.
  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  uint16_t health = wram_r16(w, (uint32_t)dp + ACTOR_DP_HEALTH);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));

  // `$81:88AB  BMI $88B7`: the subtraction went negative, so this hit was the
  // last one. Note the store happens on this path too — a dead enemy is left
  // holding its negative health.
  if (left & 0x8000) {
    PORT_COVER(enemy_died);
    work->blocks[ENEMY_BLK_DEEP]++;
    wram_w16(w, (uint32_t)dp + ACTOR_DP_HEALTH, left);
    // `$81:88B9  STZ $7E`. Transcribed rather than diffed: the word is already
    // zero every time this runs — the enemy's own init cleared it and nothing
    // between then and dying writes it — so deleting this line changes nothing
    // the harness can see. Kept because it is what the ROM does, and recorded
    // here because a store the diff cannot distinguish from a no-op is exactly
    // the kind of thing that should be written down rather than assumed.
    wram_w16(w, (uint32_t)dp + ACTOR_DP_SCRATCH_7E, 0);
    r->x = index;  // what the `TAX` left, in case `enemy_die` declines
    if (!enemy_die(w, rom, dp, r)) return false;
    // `$81:88BE  SEC : RTL` — the one thing in the game that parks a thread.
    r->c = true;
    return true;
  }

  // `$81:88AD  CMP $1E : BEQ $88C8`, and `$88C8` is a bare `CLC : RTL`. The
  // difference equalling the health it came from means the damage was zero, and
  // then not even the store happens. `CMP` of two equal words: no borrow, so
  // Z is set and the `CLC` that follows is what carry ends up as.
  if (left == health) {
    PORT_COVER(enemy_no_damage);
    work->blocks[ENEMY_BLK_NO_DAMAGE]++;
    r->a = left;
    r->x = index;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  // `$81:88B1  STA $1E : JML $81:8506`. A `JML`, so what the reaction returns is
  // what this routine returns.
  PORT_COVER(enemy_survived);
  work->blocks[ENEMY_BLK_DEEP]++;
  wram_w16(w, (uint32_t)dp + ACTOR_DP_HEALTH, left);
  return enemy_survived_react(w, dp, r);
}

bool enemy_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                   ActorHandlerRegs* r, uint32_t* unported) {
  EnemyCollideWork work;
  return enemy_collide_counted(w, rom, dp, arg, r, unported, &work);
}

// ---------------------------------------------------------------------------
// $81:8506  the survivor's reaction
// ---------------------------------------------------------------------------

// **The splice, written once.** `$81:8506`, `$81:BAB3` and `$81:847E` all end
// with these ten instructions and differ only in the address they leave behind —
// and this file has been carrying two copies of them since the level-45 round,
// with a note saying that two copies of one routine are two things that can
// drift. A third would have made the point twice.
//
// What it does is the mechanism `docs/threads.md` describes: the record names
// its thread, the scheduler holds that thread's parked stack pointer, and three
// bytes of gap open under the top three words so a far return address can be
// written into a suspension nobody called.
//
// `resume` is the spliced address **already decremented**, because what resumes a
// parked thread is an `RTL` and an `RTL` adds one. The three routines' guards,
// and what each hands back when its guard refuses, stay where they are: that is
// the half of them that genuinely differs.
static void enemy_react_splice(Wram* w, uint16_t record, uint16_t resume,
                               ActorHandlerRegs* r) {
  uint16_t slot = wram_r16(w, (uint32_t)record + ACTOR_THREAD);
  uint16_t sp = wram_r16(w, (uint32_t)W_THREAD_SP + slot);
  uint16_t gap = (uint16_t)(sp - ENEMY_REACT_FRAME);
  wram_w16(w, (uint32_t)W_THREAD_SP + slot, gap);

  // `LDA $0000,X : STA $0000,Y` three times over, X = the old pointer and
  // Y = the new one. Three *words* moved three *bytes*, so the copies overlap
  // and the order they run in is the whole of why it works: lowest first, which
  // is what a downward move needs.
  for (int i = 0; i < ENEMY_REACT_FRAME; i++)
    wram_w16(w, (uint32_t)(uint16_t)(gap + i * 2),
             wram_r16(w, (uint32_t)(uint16_t)(sp + i * 2)));

  // `LDA #$0081 : XBA : STA $0006,Y`, then `LDA #<addr> : DEC A : STA $0005,Y`.
  // Two overlapping 16-bit stores that between them lay down three bytes — the
  // second writes over the first's low half.
  wram_w16(w, (uint32_t)(uint16_t)(gap + 6), (uint16_t)(ENEMY_REACT_BANK << 8));
  wram_w16(w, (uint32_t)(uint16_t)(gap + 5), resume);

  // `SEC : RTL`. Carry set is what parks the thread — the same output a death
  // produces, for a different reason. The last instruction to touch a flag is
  // the `DEC A` two above, and every address the three splice is negative.
  r->a = resume;
  r->x = sp;
  r->y = gap;
  r->n = (resume & 0x8000) != 0;
  r->z = false;
  r->c = true;
}

bool enemy_survived_react(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  // `LDY $08 : LDA $0000,Y : AND #$0010 : BNE $855F`. `$08` on an enemy's page
  // is its own display record — the same offset the player keeps one at — and
  // the bit is `ACTOR_ATTR_SET`, which is to say **already flashing**. A second
  // hit inside those two ticks reacts to nothing.
  uint16_t record = wram_r16(w, (uint32_t)dp + VICTIM_DP_RECORD);
  uint16_t flags = wram_r16(w, record);
  if (flags & ACTOR_ATTR_SET) {
    PORT_COVER(react_already);
    // `$855F  CLC : RTL`, with the `AND`'s flags still standing.
    r->a = ACTOR_ATTR_SET;
    r->y = record;
    r->n = false;
    r->z = false;
    r->c = false;
    return true;
  }

  // `LDA $000C,Y : TAX : LDA $11B0,X`. The record names its own thread and the
  // scheduler keeps that thread's parked stack pointer — so an actor can find
  // where its own suspended machine state is without being the thing running.
  PORT_COVER(react_splice);
  enemy_react_splice(w, record, ENEMY_REACT_RETURN, r);
  return true;
}

// ---------------------------------------------------------------------------
// $81:83C6  enemy_bubble_react
// ---------------------------------------------------------------------------

bool enemy_bubble_react(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  // **It is `enemy_survived_react` with one word changed**, and after four
  // rounds of the file saying the `$5E` twin was unported, that is the whole
  // finding. `$81:83C6  LDY $08 : LDA $0000,Y : AND #$0010 : BEQ` is
  // `$81:8506`'s guard with the branch polarity flipped and the same two exits
  // behind it; `$81:83D2`..`$81:8403` is the same ten-instruction splice; and the
  // address it leaves behind is `$81:8404` where the twin leaves `$81:8542`.
  //
  // What is *not* here is the reason `enemy_freeze` needed forty lines. There is
  // **no counter, no tally and no side lookup** — no `INC $7E`, no
  // `INC $1FE0,X`, nothing indexed by which player fired. PROGRESS.md described
  // this routine as "the same splice without the counter and with its tally one
  // array over at `$7E:1FDC`", and the second half of that sentence belongs to a
  // different routine: `$7E:1FDC` is incremented by `$81:9BA2`, the nine
  // instructions `enemy_9b6b_collide`'s own `$5E` branch runs *before* it
  // `JML`s here. This one is nine words of stack surgery and nothing else.
  uint16_t record = wram_r16(w, (uint32_t)dp + VICTIM_DP_RECORD);
  uint16_t flags = wram_r16(w, record);
  if (flags & ACTOR_ATTR_SET) {
    // `$81:83D0  CLC : RTL`, with the `AND`'s flags standing — identical to
    // `react_already`'s, which is why the two have sites of their own rather
    // than sharing one. A branch that is the same code twice is still two
    // branches, and only a movie that reaches both proves it.
    PORT_COVER(bubble_already);
    r->a = ACTOR_ATTR_SET;
    r->y = record;
    r->n = false;
    r->z = false;
    r->c = false;
    return true;
  }

  PORT_COVER(bubble_splice);
  enemy_react_splice(w, record, BUBBLE_REACT_RETURN, r);
  return true;
}

// ---------------------------------------------------------------------------
// $81:847E  enemy_freeze
// ---------------------------------------------------------------------------

bool enemy_freeze(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  // `$81:847E  INC $7E : LDA $7E : CMP #$0005 : BCS : BRA <CLC : RTL>`.
  //
  // **This is what `STZ $7E` is for**, and it took six rounds and a second
  // weapon to find out. Every copy of `$81:8888` in this file ends a death with
  // a store of zero to `$7E`, and every one of them carries a comment saying the
  // store is transcribed because the word is already zero and deleting it
  // changes nothing the diff can see. It was already zero because **no input in
  // the project had ever fired this weapon**; here is the only writer that ever
  // makes it non-zero, and the death path is its reset.
  uint16_t hits = (uint16_t)(wram_r16(w, (uint32_t)dp + ACTOR_DP_FREEZE_HITS) + 1);
  wram_w16(w, (uint32_t)dp + ACTOR_DP_FREEZE_HITS, hits);
  r->a = hits;
  if (hits < FREEZE_HITS_NEEDED) {
    // The flags are the `CMP #$0005`'s, and it borrowed. Nothing else on this
    // path is touched — X and Y are still the dispatcher's.
    PORT_COVER(freeze_counting);
    uint16_t diff = (uint16_t)(hits - FREEZE_HITS_NEEDED);
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;  // `$81:84D4  CLC : RTL`
    return true;
  }

  // `$81:8489  LDX $08 : LDA $0000,X : AND #$0010 : BNE`. `react_already`'s
  // guard, spelled with X where the twin uses Y — so a hit that lands while the
  // creature is still flashing from the last one is counted and then thrown
  // away.
  uint16_t record = wram_r16(w, (uint32_t)dp + VICTIM_DP_RECORD);
  uint16_t flags = wram_r16(w, record);
  r->x = record;
  if (flags & ACTOR_ATTR_SET) {
    PORT_COVER(freeze_already);
    r->a = ACTOR_ATTR_SET;  // what the `AND` left
    r->n = false;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$81:8493  TYA : ASL A : AND #$0000 : ROL A : ROL A` — bit 15 of **Y**, which
  // is the raw collision id the dispatcher put there and never took out, turned
  // into the doubled side. `collide.h` names this idiom on `$81:9B6B`'s declined
  // branch; this is the first time the port runs it.
  PORT_COVER(freeze_took);
  uint16_t side = (uint16_t)((r->y & 0x8000) ? 2 : 0);

  // `$81:849B  JSL $80:9D6A`, which is `score_slot` with one comparison instead
  // of two: `LDX #$0000 : CMP $1E84 : BEQ : INX : INX : RTL`. It cannot answer
  // "nobody" — a side that matches neither slot comes back as slot 1 — so unlike
  // `$80:C7C2` there is no discard path to reach.
  uint16_t slot = side == wram_r16(w, W_SCORE_SLOT_SIDE) ? 0 : 2;
  PORT_COVER_IF(slot == 0, freeze_slot_0, freeze_slot_1);

  // `$81:849F  INC $1FE0,X`. The counter the end-of-level tally reads:
  // `$82:CA8C  LDA $1FE0 : CMP #$0028 : BCC` decides whether to draw
  // `MONSTER/FROZEN/....////BONUS?`, and `$82:CAA5` does the same for `$1FE2`.
  // Forty freezes is a bonus.
  uint16_t at = (uint16_t)(W_MONSTERS_FROZEN + slot);
  wram_w16(w, at, (uint16_t)(wram_r16(w, at) + 1));

  // `$81:84A2` onwards is the splice, and `$81:84D6` is what a frozen creature
  // wakes up running.
  enemy_react_splice(w, record, FREEZE_REACT_RETURN, r);
  return true;
}

// ---------------------------------------------------------------------------
// $81:FE0E  shot_collide
// ---------------------------------------------------------------------------

bool shot_collide_counted(Wram* w, uint16_t dp, uint16_t arg,
                          ActorHandlerRegs* r, ShotCollideWork* work) {
  memset(work->blocks, 0, sizeof work->blocks);

  // `$81:FE0E  TAY`. A is not touched again on the way out of the ignore path,
  // so the argument is still in it at the `RTL`; Y is the id from here on.
  r->y = arg;

  // `BEQ` then `CMP #$0003 : BEQ`, `CMP #$0004 : BEQ`, `CMP #$0001 : BEQ`. The
  // first test is the `TAY`'s own Z rather than a comparison, which is the only
  // thing that makes id 0 different from the other three: **it runs no `CMP`, so
  // carry leaves as the caller's.** Nothing about the writes below depends on
  // which id got here, so without the two marks that difference would be
  // invisible — and it is a real output.
  if (arg == SHOT_STOP_ID_A) {
    PORT_COVER(shot_expire);
    PORT_COVER(shot_expire_zero);
    work->blocks[SHOT_BLK_STOP_A]++;
    // carry untouched: `r->c` is what the dispatcher handed in
  } else if (arg == SHOT_STOP_ID_B || arg == SHOT_STOP_ID_C ||
             arg == SHOT_STOP_ID_D) {
    PORT_COVER(shot_expire);
    // The chain is unrolled and these three sit at different depths in it, so
    // the block is which one matched — the one thing about a stop that costs
    // different amounts.
    work->blocks[arg == SHOT_STOP_ID_B   ? SHOT_BLK_STOP_B
                 : arg == SHOT_STOP_ID_C ? SHOT_BLK_STOP_C
                                         : SHOT_BLK_STOP_D]++;
    // Whichever of the three matched, the `CMP` that matched set carry: an
    // equal comparison never borrows.
    r->c = true;
  } else {
    // `$81:FE20  RTL`, reached by falling through all three comparisons, so the
    // flags are the last one's — `arg - 1`. Carry is set because the only id
    // that could clear it is 0, and 0 left through the branch above.
    PORT_COVER(shot_pass);
    work->blocks[SHOT_BLK_FLY]++;
    uint16_t diff = (uint16_t)(arg - SHOT_STOP_ID_D);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = true;
    return true;
  }
  work->blocks[SHOT_BLK_TAIL]++;

  // `$81:FE21  LDY $0A : LDA #$0000 : STA $000E,Y`. The absolute-indexed store
  // goes through the record's *address*, and lands in `$7E` whichever of $7E/$80
  // the data bank holds — the same reasoning as `$1CBC` in the player's hit
  // path. Clearing `ACTOR_COLLIDE_ID` is what stops `actor_overlap_pass`
  // offering this shot to anything else in the frames before it dies:
  // `overlap_no_id` is the branch that then skips it.
  uint16_t record = wram_r16(w, (uint32_t)dp + ACTOR_DP_RECORD);
  r->y = record;  // the `LDY` overwrites the `TAY`, and Y is an output
  wram_w16(w, (uint32_t)record + ACTOR_COLLIDE_ID, 0);

  // `$81:FE29  LDA #$0001 : STA $42`, and that `LDA` is the last instruction to
  // set a flag.
  wram_w16(w, (uint32_t)dp + ACTOR_DP_LIFE, SHOT_LIFE_ENDING);
  r->a = SHOT_LIFE_ENDING;
  r->n = false;
  r->z = false;
  return true;
}

bool shot_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r) {
  ShotCollideWork work;
  return shot_collide_counted(w, dp, arg, r, &work);
}

// ---------------------------------------------------------------------------
// $83:A364  victim_collide
// ---------------------------------------------------------------------------

// `$83:A39E  LDX $08 : STZ $000E,X`, on the three paths that run it.
//
// Both stores are outputs: the record's collision id goes to zero, and X — a
// register the dispatcher hands back to its caller — ends up holding the record
// address, with N and Z describing it. The `STZ` sets no flags, so the `LDX` is
// the last word on both.
static void victim_drop_collide_id(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  uint16_t record = wram_r16(w, (uint32_t)dp + VICTIM_DP_RECORD);
  r->x = record;
  r->n = (record & 0x8000) != 0;
  r->z = record == 0;
  wram_w16(w, (uint32_t)record + ACTOR_COLLIDE_ID, 0);
}

bool victim_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r) {
  // `$83:A364  LDX $1E : BNE $A3C8`, and `$A3C8` is `SEC : RTL` — the tail the
  // `$34` case falls into, shared. The latch is the whole design of the routine:
  // a victim has one fate, the first thing to reach it decides which, and every
  // collision after that is read-only. A is not touched on this path, so the
  // argument is still in it at the `RTL`; X is the latched code from here on,
  // whichever exit runs.
  //
  // And it is the one line here no diff can check. Deleting this branch
  // outright — letting a settled victim be claimed a second time — passes
  // every call on all three movies, because none of them ever dispatches to a
  // victim twice. That is the same shape as `shot_collide`'s "add a fifth id"
  // and the opposite of a store the diff cannot see: the port would be *more
  // permissive* than the ROM, and only an input that produces the distinguishing
  // case can tell. `victim_latched` is the coverage site that says so by name.
  uint16_t latched = wram_r16(w, (uint32_t)dp + VICTIM_DP_EVENT);
  r->x = latched;
  if (latched != 0) {
    PORT_COVER(victim_latched);
    r->n = (latched & 0x8000) != 0;
    r->z = false;  // it is not zero; that is why we are here
    r->c = true;
    return true;
  }

  switch (arg) {
    case VICTIM_ID_CLAIM_A:
      // `$83:A392  BRA $A397`, skipping the `LDA #$8000`. What reaches `STA
      // $18` is the accumulator the dispatcher arrived with, which is the id —
      // so this side's marker is literally `$0005`. Only bit 15 of it is ever
      // read.
      PORT_COVER(victim_claim_a);
      wram_w16(w, (uint32_t)dp + VICTIM_DP_CLAIMANT, arg);
      break;
    case VICTIM_ID_CLAIM_B:
      PORT_COVER(victim_claim_b);
      wram_w16(w, (uint32_t)dp + VICTIM_DP_CLAIMANT, 0x8000);
      break;

    case VICTIM_ID_EVENT_2:
      // `$83:A3A5  LDA #$0002 : STA $1E : SEC : RTL`. No record write, so X is
      // still the zero the entry `LDX` read.
      PORT_COVER(victim_event_2);
      wram_w16(w, (uint32_t)dp + VICTIM_DP_EVENT, VICTIM_EVENT_2);
      r->a = VICTIM_EVENT_2;
      r->n = false;
      r->z = false;
      r->c = true;
      return true;

    case VICTIM_ID_EVENT_3_A:
    case VICTIM_ID_EVENT_3_B:
    case VICTIM_ID_EVENT_3_C: {
      // `$83:A3AC  LDA #$0003 : STA $1E : LDA $26 : BNE $A3BA`. The only exit
      // that reads a second field before deciding, and the only one where the
      // collision id survives.
      PORT_COVER(victim_event_3);
      wram_w16(w, (uint32_t)dp + VICTIM_DP_EVENT, VICTIM_EVENT_3);
      uint16_t flag = wram_r16(w, (uint32_t)dp + VICTIM_DP_FLAG_26);
      r->a = flag;
      r->n = (flag & 0x8000) != 0;
      r->z = flag == 0;
      if (flag != 0) {
        PORT_COVER(victim_keep_id);
      } else {
        victim_drop_collide_id(w, dp, r);
      }
      r->c = true;
      return true;
    }

    case VICTIM_ID_EVENT_4:
      // `$83:A3C3  LDA #$0004 : STA $1E`, falling into the shared `SEC : RTL`
      // the entry guard also branches to.
      PORT_COVER(victim_event_4);
      wram_w16(w, (uint32_t)dp + VICTIM_DP_EVENT, VICTIM_EVENT_4);
      r->a = VICTIM_EVENT_4;
      r->n = false;
      r->z = false;
      r->c = true;
      return true;

    case VICTIM_ID_EVENT_FF:
      PORT_COVER(victim_event_ff);
      wram_w16(w, (uint32_t)dp + VICTIM_DP_EVENT, VICTIM_EVENT_FF);
      r->a = VICTIM_EVENT_FF;
      r->n = true;
      r->z = false;
      r->c = true;
      return true;

    default:
      // `$83:A390  CLC : RTL`, reached by falling through all eight
      // comparisons, so the flags are the last one's — `arg - $FF` — and not
      // the first's. Nothing is written and nothing but `$1E` was read: an id a
      // victim has no reaction to costs it one word of WRAM.
      PORT_COVER(victim_ignore);
      r->a = arg;
      r->n = ((uint16_t)(arg - VICTIM_ID_EVENT_FF) & 0x8000) != 0;
      r->z = false;
      r->c = false;
      return true;
  }

  // The tail both claim ids share: latch the event, then switch the victim's
  // own collision off so the pass cannot offer it to anybody else. `LDA #$0001`
  // is the last instruction to set a flag before `LDX $08` sets them again.
  wram_w16(w, (uint32_t)dp + VICTIM_DP_EVENT, VICTIM_EVENT_CLAIMED);
  victim_drop_collide_id(w, dp, r);
  r->a = VICTIM_EVENT_CLAIMED;
  r->c = true;
  return true;
}

// ---------------------------------------------------------------------------
// $80:CAEE  object_collide
// ---------------------------------------------------------------------------

bool object_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r) {
  // `$80:CAEE  LDY $0078 : LDX $000E,Y`. `$78` is the record whose handler is
  // running — the object that was touched — and Y keeps it for the rest of the
  // routine. The absolute-indexed read lands in `$7E` whichever of $7E/$80 the
  // data bank holds, the same reasoning as `$1CBC` in the player's hit path.
  uint16_t self = wram_r16(w, W_HANDLER_SELF);
  uint16_t self_id = wram_r16(w, (uint32_t)self + ACTOR_COLLIDE_ID);
  r->y = self;
  r->x = self_id;

  // `$80:CAF4  BEQ $CB05`, and `$CB05` is `CLC : RTL`. An object whose
  // collision is already off has been taken and is waiting in the queue below;
  // this is the same guard `victim_collide` opens with and the same one a spent
  // shot enforces on itself, written a third way.
  //
  // Note which register each test reads. The `BEQ` is on the `LDX` — the
  // *object's* id — and the three `CMP`s below are on A, which is the *other*
  // actor's, untouched since the dispatcher's `TYA`. Two records, three
  // instructions apart, and nothing in the listing says so.
  //
  // And this branch is the one line here no diff can check, for the third time
  // in this file: deleting it outright — letting an object already in the queue
  // be queued a second time — passes every call on every movie, because no
  // input has ever touched a spent object. Same shape as `victim_collide`'s
  // latch and `shot_collide`'s fifth `CMP`; the port would be *more permissive*
  // than the ROM. `object_spent` is the coverage site that says so by name.
  if (self_id == 0) {
    PORT_COVER(object_spent);
    r->n = false;
    r->z = true;  // the `LDX` is the last thing to set a flag before the `CLC`
    r->c = false;
    return true;
  }

  if (arg != OBJECT_ID_TAKE_A && arg != OBJECT_ID_TAKE_B &&
      arg != OBJECT_ID_TAKE_C) {
    // `$80:CB05  CLC : RTL` reached by falling through all three comparisons,
    // so the flags are the last one's — `arg - $0004` — and not the first's.
    PORT_COVER(object_ignore);
    r->a = arg;
    r->n = ((uint16_t)(arg - OBJECT_ID_TAKE_C) & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$80:CB07  LDX $0078 : STZ $000E,X`. Switching the object's own collision
  // off is what makes the queue a set rather than a bag: the next pass will
  // find no id on this record and skip it (`overlap_no_id`), so a player
  // standing on an item cannot bank it twice.
  PORT_COVER(object_taken);
  wram_w16(w, (uint32_t)self + ACTOR_COLLIDE_ID, 0);

  // `$80:CB0D  TXA : LDX $12 : STA $14,X : INC $12 : INC $12`. The record
  // address goes into the queue at the byte cursor, and the cursor advances by
  // one word. A is the address; X is the cursor as it was *before* the two
  // increments, because the `LDX` is what loaded it.
  uint16_t cursor = wram_r16(w, (uint32_t)dp + OBJECT_DP_QUEUE_LEN);
  wram_w16(w, (uint32_t)dp + OBJECT_DP_QUEUE + cursor, self);
  uint16_t advanced = (uint16_t)(cursor + 2);
  wram_w16(w, (uint32_t)dp + OBJECT_DP_QUEUE_LEN, advanced);
  r->a = self;
  r->x = cursor;
  // The second `INC $12` is the last instruction to set a flag, and it sets
  // them from the word in memory rather than from A or X.
  r->n = (advanced & 0x8000) != 0;
  r->z = advanced == 0;
  // `$80:CB16  SEC : RTL` — so the manager's thread is parked, exactly as a
  // dying enemy parks its own. The second thing in the game that does this.
  r->c = true;
  return true;
}

// ---------------------------------------------------------------------------
// $80:8480  thread_call_handler
// ---------------------------------------------------------------------------

bool thread_call_handler_counted(Wram* w, const Rom* rom, uint16_t slot,
                                 uint16_t arg, bool carry_in,
                                 ThreadCallResult* out, ThreadCallWork* work) {
  memset(work, 0, sizeof *work);
  out->unported = 0;

  // `$80:8480  LDA $1300,X : ORA $1330,X : BEQ $84B0`. A slot with no handler
  // registered makes the whole routine three instructions and no writes.
  uint16_t lo = wram_r16(w, W_THREAD_HANDLER + slot);
  uint16_t bank = wram_r16(w, W_THREAD_HANDLER_BANK + slot);
  if ((lo | bank) == 0) {
    PORT_COVER(handler_none);
    work->blocks[THREAD_CALL_BLK_NONE]++;
    out->entered = false;
    out->a = 0;  // what the `ORA` produced
    out->x = slot;
    out->y = arg;
    out->c = carry_in;  // nothing on this path touches carry
    return true;
  }

  // `$80:849E  LDA $8082DE,X : TCD` — the handler runs on *its own* thread's
  // direct page, not the caller's. Everything an actor is lives on that page.
  uint16_t dp = rom_word(rom, THREAD_DP_TABLE + slot);
  work->blocks[THREAD_CALL_BLK_ENTER]++;
  work->dp = dp;

  // `$80:84A3  TYA` then `RTL`: A is the argument, and X and Y are what the
  // dispatcher was called with, untouched.
  ActorHandlerRegs r = {.a = arg,
                        .x = slot,
                        .y = arg,
                        .n = (arg & 0x8000) != 0,
                        .z = arg == 0,
                        .c = carry_in};

  uint32_t entry = ((uint32_t)(bank & 0xff) << 16) | lo;
  work->entry = entry;
  bool served;
  if (entry == PLAYER_COLLIDE_ENTRY) {
    // The address a decline happened at is the harness's business, and it asks
    // `player_collide` directly through its own registry entry. Passing NULL
    // here is what keeps `guard_thread_call_handler` from censusing the door
    // rather than the room behind it.
    served = player_collide_counted(w, rom, dp, arg, &r, NULL, &work->player);
  } else if (entry == ENEMY_COLLIDE_ENTRY) {
    served = enemy_collide_counted(w, rom, dp, arg, &r, NULL, &work->enemy);
  } else if (entry == MONSTER_COLLIDE_ENTRY) {
    // **This one was missing, and nothing could see it.** `monster_collide` has
    // been registered on its own entry PC since the level-45 round, so `verify`
    // offered it every call the giant spider made and it passed all 1,138 of
    // them — but the *dispatcher* never routed to it, so every one of those
    // calls also declined here, one level up. The two facts are not in tension:
    // a routine reached directly is checked, and the same routine reached
    // through a caller that does not know about it is a decline. What hid it is
    // the census, which excludes the handlers the port has by address — and
    // `$81:C4A6` was on that exclusion list while not being on this one, so the
    // declines were counted and never named. On `movies/level45-carried.zmv`
    // that is 1,351 of `thread_call_handler`'s 2,800 calls.
    served = monster_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == MONSTER_C440_COLLIDE_ENTRY) {
    served = monster_c440_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == ENEMY_B41C_COLLIDE_ENTRY) {
    served = enemy_b41c_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == ENEMY_CDDE_COLLIDE_ENTRY) {
    served = enemy_cdde_collide(w, dp, arg, &r);
  } else if (entry == ENEMY_B592_COLLIDE_ENTRY) {
    served = enemy_b592_collide(w, dp, arg, &r);
  } else if (entry == ENEMY_D7F6_COLLIDE_ENTRY) {
    served = enemy_d7f6_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == ENEMY_9B6B_COLLIDE_ENTRY) {
    served = enemy_9b6b_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == ENEMY_9063_COLLIDE_ENTRY) {
    served = enemy_9063_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == ENEMY_D301_COLLIDE_ENTRY) {
    served = enemy_d301_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == ENEMY_AC92_COLLIDE_ENTRY) {
    served = enemy_ac92_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == ENEMY_E6E4_COLLIDE_ENTRY) {
    served = enemy_e6e4_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == ACTOR_845E_COLLIDE_ENTRY) {
    served = actor_845e_collide(arg, &r);
  } else if (entry == ACTOR_DEEB_COLLIDE_ENTRY) {
    served = actor_deeb_collide(w, dp, arg, &r);
  } else if (entry == ACTOR_F1C2_COLLIDE_ENTRY) {
    served = actor_f1c2_collide(w, dp, arg, &r);
  } else if (entry == ACTOR_F534_COLLIDE_ENTRY) {
    served = actor_f534_collide(w, dp, arg, &r);
  } else if (entry == VICTIM_A264_COLLIDE_ENTRY) {
    served = victim_a264_collide(w, dp, arg, &r);
  } else if (entry == BOSS_9660_COLLIDE_ENTRY) {
    served = boss_9660_collide(w, rom, dp, arg, &r);
  } else if (entry == SHOT_COLLIDE_ENTRY) {
    served = shot_collide_counted(w, dp, arg, &r, &work->shot);
  } else if (entry == SHOT_EDAA_COLLIDE_ENTRY) {
    // `r` is already exactly what `$80:84A3  TYA` left, and a bare `RTL`
    // changes none of it — so there is deliberately no assignment here. The
    // absence is the routine.
    served = shot_edaa_collide();
  } else if (entry == SHOT_F6A3_COLLIDE_ENTRY) {
    served = shot_f6a3_collide(w, dp, arg, &r);
  } else if (entry == ACTOR_F4EF_COLLIDE_ENTRY) {
    served = actor_f4ef_collide(w, dp, arg, &r);
  } else if (entry == VICTIM_COLLIDE_ENTRY) {
    served = victim_collide(w, dp, arg, &r);
  } else if (entry == OBJECT_COLLIDE_ENTRY) {
    served = object_collide(w, dp, arg, &r);
  } else {
    // Twenty-five addresses are handled above. Anything else is a routine that has
    // not been written yet, and saying so by address is what makes the remaining
    // work countable instead of vague — which is what `unported` carries out.
    PORT_COVER(handler_unported);
    out->unported = entry;
    return false;
  }
  if (!served) return false;
  PORT_COVER(handler_ported);

  out->entered = true;
  out->a = r.a;
  out->x = slot;  // `$80:84A5  PLX` puts the slot back
  out->y = r.y;
  out->c = r.c;

  // `$80:84A6  BCC $84AE` — a handler that comes back with carry set has its
  // thread put to sleep indefinitely. Exactly one thing in the game does that:
  // `$81:88BE`, an enemy that has just died. Until that branch was ported this
  // site read zero on every movie.
  if (r.c) {
    PORT_COVER(handler_park);
    work->blocks[THREAD_CALL_BLK_PARK]++;
    // `$80:84A8  LDA #$8000 : STA $1180,X`, and that `LDA` is the last thing to
    // touch A — so the parked value, not the handler's, is what the caller gets
    // back. Nothing but a dying enemy comes through here, which is why this was
    // invisible until `enemy_collide` grew its death branch.
    wram_w16(w, W_THREAD_WAIT + slot, 0x8000);
    out->a = 0x8000;
  } else {
    work->blocks[THREAD_CALL_BLK_RESUME]++;
  }
  return true;
}

bool thread_call_handler(Wram* w, const Rom* rom, uint16_t slot, uint16_t arg,
                         bool carry_in, ThreadCallResult* out) {
  ThreadCallWork work;
  return thread_call_handler_counted(w, rom, slot, arg, carry_in, out, &work);
}

// ---------------------------------------------------------------------------
// $81:C4A6  monster_collide
// ---------------------------------------------------------------------------

// `$81:BBEB`, which is `$81:8727` again at three times the money. Returns false
// only if `score_add` declines, which a stock ROM cannot produce.
//
// The guard is the interesting instruction: `LDA $20 : BEQ` skips the whole
// award when the parked id is zero, so a monster killed by something with no id
// is worth nothing and still counts down. `DEC $2A` happens either way.
static bool monster_death_award(Wram* w, const Rom* rom, uint16_t dp,
                                ActorHandlerRegs* r) {
  uint16_t id = wram_r16(w, (uint32_t)dp + MONSTER_DP_HIT_ID);
  // `LDX #$0300 : LDA $20 : BEQ` — the award is in X before the guard is even
  // read, so the path that skips the payout still returns it, and A is the id.
  r->x = MONSTER_DEATH_AWARD;
  r->a = id;
  if (id != 0) {
    PORT_COVER(monster_kill_award);
    ScoreResult score;
    if (!score_add(w, rom, (id & 0x8000) != 0, MONSTER_DEATH_AWARD, r->c, &score))
      return false;
    r->c = score.c;
    // `LDA $20 : AND #$8000 : ASL A : ROL A : ROL A : TAX` — bit 15 becomes 0 or
    // 2, the same doubled side index `score_add` searches with, and the counter
    // it indexes is absolute rather than on this page.
    //
    // **Nothing `score_add` returned in A survives this.** `LDA $20` reloads the
    // id over it and the three shifts reduce it to the side, so what the caller
    // gets back is 0 or 2 — which is what the diff said on the one death in the
    // corpus: `A: ROM $0000, port $0300`, the port having kept the award.
    //
    // **The doubling is diffed now, and it took a movie rather than a reading.**
    // Writing `1` here instead of `2` passed every call on every input for two
    // rounds, because every death in the corpus was player one's and both
    // spellings of zero are zero — `player_pickup`'s doubled-id index and
    // `enemy_b41c_collide`'s `ASL` for the third time.
    // `movies/level25-2p.zmv` is Julie killing three of these, and the same
    // perturbation now fails at **`$7E:1FD5`**: the byte *between* the two
    // counters, which is exactly where an undoubled index lands.
    uint16_t side = (uint16_t)((id & 0x8000) ? 2 : 0);
    uint16_t at = (uint16_t)(W_MONSTER_KILL_COUNT + side);
    uint16_t n = (uint16_t)(wram_r16(w, at) + 1);
    wram_w16(w, at, n);
    r->a = side;
    r->x = side;
    r->n = (n & 0x8000) != 0;
    r->z = n == 0;
  } else {
    // **This site exists because a perturbation got past the report.** Paying
    // the award unconditionally — deleting the `BEQ` — passed all 151 calls on
    // `movies/level25.zmv`, and `monster_kill_award` read 10 the whole time, so
    // the coverage table said nothing was missing. A site marks the branch it is
    // written on; a guard with a mark on its *passing* side only says the guard
    // was reached, never that it refused. Marking the skip is what makes the
    // refusal countable, and it is untaken: every monster that has died in the
    // corpus was killed by something carrying an id.
    PORT_COVER(monster_kill_free);
  }
  // `$81:BC02  DEC $2A`, on both paths, and it is the last thing to set flags.
  uint16_t count = (uint16_t)(wram_r16(w, (uint32_t)dp + MONSTER_DP_COUNT) - 1);
  wram_w16(w, (uint32_t)dp + MONSTER_DP_COUNT, count);
  r->n = (count & 0x8000) != 0;
  r->z = count == 0;
  return true;
}

// The death tail at `$81:C4DF`, shared by the negative-health path and by id
// `$5E` reaching it without subtracting anything.
static bool monster_die(Wram* w, const Rom* rom, uint16_t dp, uint16_t health,
                        ActorHandlerRegs* r) {
  wram_w16(w, (uint32_t)dp + MONSTER_DP_HEALTH, health);
  wram_w16(w, (uint32_t)dp + MONSTER_DP_SCRATCH_7E, 0);
  if (!monster_death_award(w, rom, dp, r)) return false;
  r->c = true;  // `$81:C4E6  SEC : RTL` — parks the thread
  return true;
}

// The two copies, `$81:C4A6` and `$81:C440`, and everything that differs
// between them. Three bytes in the ROM; two fields here.
//
// Sharing the body rather than transcribing it twice is the same call
// `ENEMY_REACT_FRAME` makes: two copies of one routine in C are two things that
// can drift, and the diff would only ever catch the drift on a level that runs
// both. What it costs is that the coverage sites inside this function no longer
// say *which* copy an input reached — so the two places the copies genuinely
// differ get sites of their own, below, and those do.
typedef struct {
  bool is_c440;                // which copy, for the two marks that need it
  uint32_t special_entry;      // where id `$5D` goes, and declines
} MonsterCopy;

static bool monster_collide_body(Wram* w, const Rom* rom, uint16_t dp,
                                 uint16_t arg, ActorHandlerRegs* r,
                                 uint32_t* unported, const MonsterCopy* copy) {
  // `$81:C4A6  CMP #$005C : BCS`. Everything below a weapon shot is sorted by
  // two more comparisons into three outcomes, and two of those write nothing.
  if (arg < COLLIDE_ID_PLAYER) {
    if (arg < MONSTER_OBJECT_ID_FIRST) {
      // `CMP #$000C : BCC $C50A`, and `$C50A` is a bare `CLC : RTL`. This is the
      // branch a player standing on it takes, every frame, which is why it is
      // most of the 2,039 declines the census counted.
      PORT_COVER(monster_ignore_low);
      uint16_t diff = (uint16_t)(arg - MONSTER_OBJECT_ID_FIRST);
      r->a = arg;
      r->n = (diff & 0x8000) != 0;
      r->z = diff == 0;
      r->c = false;
      return true;
    }
    if (arg >= MONSTER_OBJECT_ID_END) {
      // `CMP #$0033 : BCC` fell through to its own `CLC : RTL` two bytes later,
      // which is a *different* exit from the one above and leaves different
      // flags: this comparison did not borrow.
      //
      // **The difference is unobservable and that is not an accident of this
      // corpus.** Computing the flags from `arg - MONSTER_OBJECT_ID_FIRST` here
      // instead passes every call on every movie, because the two spellings can
      // only disagree on `arg == MONSTER_OBJECT_ID_END` — every other id in
      // `[$33,$5C)` is positive and non-zero either way. Id `$33` is not in
      // `$80:CA30`'s thirty entries, so nothing in the game carries it. Not a
      // branch, so no coverage mark can say it; written down here beside the
      // line, like the `STZ $7E` in `enemy_die`.
      PORT_COVER(monster_ignore_high);
      uint16_t diff = (uint16_t)(arg - MONSTER_OBJECT_ID_END);
      r->a = arg;
      r->n = (diff & 0x8000) != 0;
      r->z = diff == 0;
      r->c = false;
      return true;
    }

    // `$81:C4EC  LDA $26 : BNE` — the latch, and the same shape as
    // `victim_collide`'s: whatever reached this monster first has already
    // decided, and a second object is ignored.
    uint16_t latch = wram_r16(w, (uint32_t)dp + MONSTER_DP_LATCH);
    if (latch != 0) {
      PORT_COVER(monster_latched);
      r->a = latch;
      r->n = (latch & 0x8000) != 0;
      r->z = false;
      r->c = false;
      return true;
    }

    // `LDY $08 : LDA #$0003 : STA $000E,Y`. **This is the theft**: the monster
    // rewrites its own display record's collision id, which is the `$04` -> `$03`
    // transition `--records` caught the instant a bonus object vanished.
    PORT_COVER(monster_take);
    uint16_t rec = wram_r16(w, (uint32_t)dp + MONSTER_DP_RECORD);
    wram_w16(w, (uint32_t)rec + ACTOR_COLLIDE_ID, MONSTER_TAKEN_ID);

    // `LDA $0042 : CMP #$0004 : BNE +2 : LDA $0046` — both absolute. One value
    // of the first global redirects the latch to the second.
    uint16_t src = wram_r16(w, W_MONSTER_LATCH_SRC);
    if (src == MONSTER_LATCH_SRC_ALT) {
      PORT_COVER(monster_latch_alt);
      src = wram_r16(w, W_MONSTER_LATCH_ALT);
    }
    wram_w16(w, (uint32_t)dp + MONSTER_DP_LATCH, src);

    // `JSR $C04A`, which is `LDA #$C050 : STA $12 : RTS` — three instructions,
    // inlined because a routine that only ever queues one constant is not a
    // routine worth a registry entry.
    wram_w16(w, (uint32_t)dp + MONSTER_DP_NEXT, MONSTER_NEXT_TAKE_OBJECT);
    r->a = MONSTER_NEXT_TAKE_OBJECT;
    r->y = rec;
    r->n = (MONSTER_NEXT_TAKE_OBJECT & 0x8000) != 0;
    r->z = false;
    r->c = true;  // `$81:C508  SEC : RTL`
    return true;
  }

  // `$81:C4B7  STA $20 : AND #$7FFF`. A weapon shot, and from here on the
  // routine is `enemy_collide` in a different page's clothing.
  PORT_COVER(monster_hit);
  wram_w16(w, (uint32_t)dp + MONSTER_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;

  if (id == MONSTER_HIT_SPECIAL) {
    // `JML $81:BB05` on the spider, `JML $81:847E` one stage earlier — and this
    // is where the two copies genuinely part company, which is why they had
    // separate sites before either address meant anything. The earlier one goes
    // to `enemy_freeze`, which the port has; the spider's still declines.
    if (copy->is_c440) {
      PORT_COVER(c440_special);
      return enemy_freeze(w, dp, r);
    }
    PORT_COVER(monster_special);
    if (unported) *unported = copy->special_entry;
    return false;
  }

  uint16_t health = wram_r16(w, (uint32_t)dp + MONSTER_DP_HEALTH);
  if (id == MONSTER_HIT_FATAL) {
    // `CMP #$005E : BEQ $C4DF` jumps *into* the death tail without subtracting,
    // so what gets stored as health is A — and A here is the masked id, not any
    // arithmetic on health. Transcribed from the listing and worth flagging as
    // such: no input has produced id $5E.
    PORT_COVER(monster_fatal_id);
    return monster_die(w, rom, dp, id, r);
  }

  // `SEC : SBC #$005C : ASL A : TAX`, then `SEC : LDA $22 : SBC $818561,X` —
  // the same damage table `enemy_collide` indexes, off a different health field.
  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));
  r->x = index;

  if (left & 0x8000) {
    PORT_COVER(monster_died);
    return monster_die(w, rom, dp, left, r);
  }
  if (left == health) {
    // `CMP $22 : BEQ $C50A`, the shared `CLC : RTL`. Not even the store happens.
    PORT_COVER(monster_no_damage);
    r->a = left;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  // `STA $22 : JML $81:BAB3` — it lived. The store happens *before* the jump, so
  // it belongs to this routine even though the reaction does not.
  PORT_COVER(monster_survived);
  wram_w16(w, (uint32_t)dp + MONSTER_DP_HEALTH, left);
  if (copy->is_c440) {
    // ...and one stage earlier the jump is to `$81:8506` instead, which is a
    // different reaction and not a relocation of the same one: it guards on bit
    // 4 of the record's flags and splices an address that sets that bit, where
    // `$81:BAB3` guards on the whole of `ACTOR_ATTR` and splices one that writes
    // `$0C00`. This is the only difference between the two copies that changes
    // what lands in WRAM.
    PORT_COVER(c440_survived);
    return enemy_survived_react(w, dp, r);
  }
  return monster_survived_react(w, dp, r);
}

bool monster_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                     ActorHandlerRegs* r, uint32_t* unported) {
  static const MonsterCopy spider = {false, MONSTER_SPECIAL_ENTRY};
  return monster_collide_body(w, rom, dp, arg, r, unported, &spider);
}

bool monster_c440_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                          ActorHandlerRegs* r, uint32_t* unported) {
  static const MonsterCopy earlier = {true, MONSTER_C440_SPECIAL_ENTRY};
  return monster_collide_body(w, rom, dp, arg, r, unported, &earlier);
}

// ---------------------------------------------------------------------------
// $81:BAB3  the survivor's reaction, again
// ---------------------------------------------------------------------------

bool monster_survived_react(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  // `LDY $08 : LDA $0010,Y : BNE $BB03`. The twin tests bit 4 of the record's
  // *flags*; this tests the whole of `ACTOR_ATTR`, the field that bit selects.
  // Same question, asked of the answer rather than of the permission — and it
  // means what comes back on this path is whatever was in the field, where the
  // twin can hand back a constant.
  uint16_t record = wram_r16(w, (uint32_t)dp + MONSTER_DP_RECORD);
  uint16_t attr = wram_r16(w, (uint32_t)record + ACTOR_ATTR);
  if (attr != 0) {
    PORT_COVER(monster_react_already);
    r->a = attr;
    r->y = record;
    r->n = (attr & 0x8000) != 0;
    r->z = false;
    r->c = false;  // `$81:BB03  CLC : RTL`
    return true;
  }

  // From here it is `$81:8506` instruction for instruction, so it is the same
  // C: `$81:BAEC` goes in as `$81:BAEB`, for the one an `RTL` adds.
  PORT_COVER(monster_react_splice);
  enemy_react_splice(w, record, MONSTER_REACT_RETURN, r);
  return true;
}

// ---------------------------------------------------------------------------
// $81:B41C  enemy_b41c_collide — the same routine a third time
// ---------------------------------------------------------------------------

bool enemy_b41c_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported) {
  if (arg < COLLIDE_ID_PLAYER) {
    // `$81:B41C  CMP #$005C : BCS : CLC : RTL`, which is `enemy_collide`'s first
    // four instructions unchanged — and, as there, the branch that writes
    // nothing at all.
    PORT_COVER(b41c_ignore);
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$81:B423  STA $5A : AND #$7FFF`. The park happens here the way it does in
  // both twins, even though this copy never reads it back.
  PORT_COVER(b41c_act);
  wram_w16(w, (uint32_t)dp + B41C_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;

  // `$81:B428  CMP #$005E : BEQ` and `$81:B42D  CMP #$005D : BEQ` — the same two
  // ids leaving through the same two routines as `enemy_collide`'s, which is one
  // more piece of evidence that this is that routine again.
  if (id == ENEMY_HIT_SPECIAL_B) {
    PORT_COVER(b41c_hit_freeze);
    return enemy_freeze(w, dp, r);
  }
  if (id == ENEMY_HIT_SPECIAL_A) {
    PORT_COVER(b41c_hit_special);
    return enemy_bubble_react(w, dp, r);
  }

  // `$81:B432  CMP #$0061 : BEQ $B462`, the comparison neither twin has, and
  // `$B462  LDY $08 : LDX $0004,Y : BNE $B437` — one on the ground gets the
  // special answer, one in the air falls through and takes damage like anything
  // else. The `BNE` reads the `LDX`, so the flags this path leaves are the
  // record's height, not the id.
  if (id == B41C_HIT_SPECIAL) {
    uint16_t record = wram_r16(w, (uint32_t)dp + B41C_DP_RECORD);
    uint16_t z = wram_r16(w, (uint32_t)record + ACTOR_Z);
    if (z == 0) {
      // `JSR $B168`, three instructions, inlined for `monster_collide`'s reason.
      PORT_COVER(b41c_special_grounded);
      wram_w16(w, (uint32_t)dp + B41C_DP_NEXT, B41C_NEXT_ON_SPECIAL);
      r->a = B41C_NEXT_ON_SPECIAL;
      r->x = z;  // what the `LDX` left, which is zero on this side of the `BNE`
      r->y = record;
      r->n = (B41C_NEXT_ON_SPECIAL & 0x8000) != 0;
      r->z = false;
      r->c = true;  // `$81:B46C  SEC : RTL`
      return true;
    }
    // Airborne: the `BNE` goes back to `$B437` with X holding the height and Y
    // the record, and the damage path below overwrites X but never Y.
    PORT_COVER(b41c_special_airborne);
    r->x = z;
    r->y = record;
  }

  // `$81:B437  INC $4C` — the hit flag, raised before the damage is even
  // computed, so a hit that does nothing still tells the body it happened.
  PORT_COVER(b41c_hit);
  uint16_t flag = wram_r16(w, (uint32_t)dp + B41C_DP_HIT_FLAG);
  wram_w16(w, (uint32_t)dp + B41C_DP_HIT_FLAG, (uint16_t)(flag + 1));

  // `$81:B439  SEC : SBC #$005C : ASL A : TAX`, then `SEC : LDA $0C : SBC
  // $818561,X` — the same arithmetic against the same table, one page over.
  //
  // **Dropping the `ASL` passes every call on every movie**, and it is the same
  // blind spot `player_pickup`'s doubled-id index had before
  // `movies/level1-pickups.zmv`: every hit that reaches this creature in the
  // whole corpus carries id `$005C`, whose index is 0 either way. Sixteen movies
  // were instrumented to check that rather than assumed — `$5C` was the only id
  // any of them produced. Not a branch, so no coverage mark can express it;
  // written down here beside the line, like the `STZ $7E` in `enemy_die`. What
  // would settle it is an input that hits this creature with a second weapon.
  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  uint16_t health = wram_r16(w, (uint32_t)dp + B41C_DP_HEALTH);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));

  if (left & 0x8000) {
    // `$81:B452  DEC $0A : STA $0C : STZ $7E : SEC : RTL`. **No award.** Where
    // `enemy_collide` reaches `$81:8727` and pays `ENEMY_DEATH_AWARD`, this copy
    // decrements a word on its own page and returns — see `B41C_DP_COUNTER_0A`
    // for how little that is known to mean. The store of the negative health
    // happens here too, exactly as it does in the twin.
    PORT_COVER(b41c_died);
    uint16_t count = wram_r16(w, (uint32_t)dp + B41C_DP_COUNTER_0A);
    wram_w16(w, (uint32_t)dp + B41C_DP_COUNTER_0A, (uint16_t)(count - 1));
    wram_w16(w, (uint32_t)dp + B41C_DP_HEALTH, left);
    // `STZ $7E`, and the same caveat as `enemy_die`'s: transcribed rather than
    // diffed, because the word is already zero every time this runs.
    wram_w16(w, (uint32_t)dp + ACTOR_DP_SCRATCH_7E, 0);
    r->a = left;
    r->x = index;
    // The last instruction to set flags is the `STZ`'s predecessor, the `STA` —
    // and `STA` sets none. So N and Z are still the `SBC`'s, from a difference
    // this branch already knows is negative.
    r->n = true;
    r->z = false;
    r->c = true;  // `SEC : RTL`, which parks the thread
    return true;
  }

  if (left == health) {
    // `$81:B448  CMP $0C : BEQ $B46E`, and `$B46E` is a bare `CLC : RTL`. Zero
    // damage, and this copy has already raised the hit flag by the time it finds
    // that out — which is a real difference from the twin, where a zero-damage
    // hit leaves no trace at all.
    PORT_COVER(b41c_no_damage);
    r->a = left;
    r->x = index;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  // `$81:B44C  STA $0C : JML $81:8506` — the same reaction routine the twin
  // jumps to, shared rather than re-spelled, because `enemy_survived_react`
  // reads the record from `$08` and that is where this page keeps it too.
  PORT_COVER(b41c_survived);
  wram_w16(w, (uint32_t)dp + B41C_DP_HEALTH, left);
  return enemy_survived_react(w, dp, r);
}

// ---------------------------------------------------------------------------
// $81:CC0A  the reaction — a next-routine swap rather than a stack splice
// ---------------------------------------------------------------------------

void enemy_cdde_react_begin(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  PORT_COVER(cdde_react_begin);

  // `LDA $16 : STA $26` then `LDA #$CC2F : STA $16`. The routine it displaces is
  // parked next door, and `$81:CC2F` is what puts it back — so the flash is a
  // detour in the actor's own state machine rather than anything to do with
  // threads. Nothing is suspended and no stack is touched.
  wram_w16(w, (uint32_t)dp + CDDE_DP_NEXT_SAVED,
           wram_r16(w, (uint32_t)dp + CDDE_DP_NEXT));
  wram_w16(w, (uint32_t)dp + CDDE_DP_NEXT, CDDE_REACT_NEXT);

  // `LDA #$001E : STA $24` — the timer `$81:CC2F` counts down, and the thing
  // this handler reads to know it is already reacting.
  wram_w16(w, (uint32_t)dp + CDDE_DP_REACT_TIMER, CDDE_REACT_TICKS);
  // `LDA #$0014 : STA $0C` — and the countdown goes *back up*. A hit that lands
  // during the reaction is the only way this actor can be finished off, which is
  // what the branch above this one is for.
  wram_w16(w, (uint32_t)dp + CDDE_DP_COUNTDOWN, CDDE_REACT_COUNTDOWN);

  // `LDY $08 : LDA $0000,Y : ORA #$0010 : STA $0000,Y`, then `LDA #$0C00 : STA
  // $0010,Y`. The same two fields `monster_survived_react` ends with, reached
  // without any of the machinery.
  uint16_t record = wram_r16(w, (uint32_t)dp + CDDE_DP_RECORD);
  uint16_t flags = wram_r16(w, (uint32_t)record + ACTOR_FLAGS);
  wram_w16(w, (uint32_t)record + ACTOR_FLAGS, (uint16_t)(flags | ACTOR_ATTR_SET));
  wram_w16(w, (uint32_t)record + ACTOR_ATTR, CDDE_REACT_ATTR);

  // `LDA #$0C00` is the last instruction here to set a flag; the two `STA`s that
  // follow set none, and so does the `RTS`.
  r->a = CDDE_REACT_ATTR;
  r->y = record;
  r->n = false;
  r->z = false;
}

// ---------------------------------------------------------------------------
// $81:CDDE  enemy_cdde_collide
// ---------------------------------------------------------------------------

bool enemy_cdde_collide(Wram* w, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r) {
  r->c = false;  // every one of the four exits is a `CLC : RTL`

  if (arg < COLLIDE_ID_PLAYER) {
    // `$81:CDDE  CMP #$005C : BCC $CDF7`, and `$CDF7` is `STZ $22 : CLC : RTL`.
    // Unlike all three copies of `enemy_collide`, the branch that ignores an id
    // still *writes*: the parked id is cleared. The `STZ` sets no flags, so what
    // comes back is the `CMP`'s.
    //
    // **Deleting this store passes every call on every movie, and clearing the
    // wrong offset does not** — which is the pair of results that says what is
    // going on. `$22` is already zero every time this runs, because the only
    // thing that ever puts anything there is a weapon shot and no input in the
    // corpus lands one on this creature; write the zero to `$20` instead and the
    // diff fails at once, because *that* word is live. So the store is
    // transcribed rather than diffed, exactly like the `STZ $7E` in `enemy_die`
    // — and unlike that one it stops being a no-op the moment a shot arrives,
    // which is the same input the five untaken branches below are waiting for.
    PORT_COVER(cdde_ignore);
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    wram_w16(w, (uint32_t)dp + CDDE_DP_HIT_ID, 0);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    return true;
  }

  // `$81:CDE3  STA $22 : AND #$7FFF`, and from here A is the masked id — the
  // three comparisons below never touch it again, so it is also what comes back.
  wram_w16(w, (uint32_t)dp + CDDE_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;

  if (id == CDDE_HIT_COUNTED_A || id == CDDE_HIT_COUNTED_B) {
    // `$81:CE12  INC $0A : CLC : RTL`, reached from two different `BEQ`s.
    PORT_COVER(cdde_counted);
    uint16_t n = (uint16_t)(wram_r16(w, (uint32_t)dp + CDDE_DP_COUNTER_0A) + 1);
    wram_w16(w, (uint32_t)dp + CDDE_DP_COUNTER_0A, n);
    r->n = (n & 0x8000) != 0;
    r->z = n == 0;
    return true;
  }

  if (id != CDDE_HIT_DAMAGE) {
    // Everything else falls off the end of the three comparisons into the same
    // `STZ $22` the below-`$5C` branch uses — so an id it does not recognise
    // erases the one it just parked two instructions ago. The flags are the last
    // comparison's, `CMP #$006F`, not the first's.
    PORT_COVER(cdde_unmatched);
    uint16_t diff = (uint16_t)(id - CDDE_HIT_COUNTED_B);
    wram_w16(w, (uint32_t)dp + CDDE_DP_HIT_ID, 0);
    r->n = (diff & 0x8000) != 0;
    r->z = false;  // `id == $6F` went to the counted branch above
    return true;
  }

  // `$81:CE03  DEC $0C : BMI`. One hit is one decrement — there is no damage
  // table on this page and no id that costs more than any other.
  uint16_t left = (uint16_t)(wram_r16(w, (uint32_t)dp + CDDE_DP_COUNTDOWN) - 1);
  wram_w16(w, (uint32_t)dp + CDDE_DP_COUNTDOWN, left);
  if (!(left & 0x8000)) {
    PORT_COVER(cdde_survived);
    r->n = false;
    r->z = left == 0;
    return true;
  }

  // `$81:CE09  LDX $24 : BNE $CE12`. The countdown has gone under, and what
  // happens next is decided by whether this actor is *already* reacting.
  uint16_t timer = wram_r16(w, (uint32_t)dp + CDDE_DP_REACT_TIMER);
  r->x = timer;
  if (timer != 0) {
    // Already flashing, so the killing blow is counted rather than acted on —
    // and `INC $0A` is the same three instructions the two counted ids reach.
    // This is `react_already`'s guard with a consequence: the other two copies
    // return having done nothing, this one keeps a tally.
    PORT_COVER(cdde_killed_reacting);
    uint16_t n = (uint16_t)(wram_r16(w, (uint32_t)dp + CDDE_DP_COUNTER_0A) + 1);
    wram_w16(w, (uint32_t)dp + CDDE_DP_COUNTER_0A, n);
    r->n = (n & 0x8000) != 0;
    r->z = n == 0;
    return true;
  }

  // `$81:CE0D  JSR $CC0A`, then `CLC : RTL`. X is the zero the `LDX` just read.
  enemy_cdde_react_begin(w, dp, r);
  return true;
}

// ---------------------------------------------------------------------------
// $81:B592  enemy_b592_collide
// ---------------------------------------------------------------------------

bool enemy_b592_collide(Wram* w, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r) {
  r->a = arg;    // nothing here writes A; there is no `AND #$7FFF` either
  r->c = false;  // all three exits are `CLC : RTL`

  if (arg != B592_HIT_A && arg != B592_HIT_B) {
    // `$81:B59C  CLC : RTL`, reached by falling off the end of both `CMP`s — so
    // the flags are the *second* comparison's. An id of `$07` never gets here,
    // having branched at the first, which is why subtracting `B592_HIT_B` is the
    // right spelling and cannot be zero.
    PORT_COVER(b592_ignore);
    uint16_t diff = (uint16_t)(arg - B592_HIT_B);
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    return true;
  }

  // `$81:B59E  STA $1E : DEC $0C : BMI`. One touch is one decrement, with no
  // table and no distinction between the two ids that get here.
  wram_w16(w, (uint32_t)dp + B592_DP_HIT_ID, arg);
  uint16_t left = (uint16_t)(wram_r16(w, (uint32_t)dp + B592_DP_COUNTDOWN) - 1);
  wram_w16(w, (uint32_t)dp + B592_DP_COUNTDOWN, left);
  if (!(left & 0x8000)) {
    PORT_COVER(b592_survived);
    r->n = false;
    r->z = left == 0;
    return true;
  }

  // `$81:B5A6  INC $0A : CLC : RTL`.
  PORT_COVER(b592_exhausted);
  uint16_t n = (uint16_t)(wram_r16(w, (uint32_t)dp + B592_DP_COUNTER_0A) + 1);
  wram_w16(w, (uint32_t)dp + B592_DP_COUNTER_0A, n);
  r->n = (n & 0x8000) != 0;
  r->z = n == 0;
  return true;
}

// ---------------------------------------------------------------------------
// $81:D7F6  enemy_d7f6_collide
// ---------------------------------------------------------------------------

// The tail at `$81:D825`, reached both by a negative difference and by id `$5E`
// arriving without one. `health` is whatever was in A at the branch, which is
// the point of passing it rather than recomputing it.
static void d7f6_die(Wram* w, uint16_t dp, uint16_t health,
                     ActorHandlerRegs* r) {
  // `DEC $0A : STA $0C : STZ $7E`. The order matters for the flags and for
  // nothing else: the `DEC` is the last instruction here that sets any.
  uint16_t dead = (uint16_t)(wram_r16(w, (uint32_t)dp + D7F6_DP_DEAD) - 1);
  wram_w16(w, (uint32_t)dp + D7F6_DP_DEAD, dead);
  wram_w16(w, (uint32_t)dp + D7F6_DP_HEALTH, health);
  // The same store `enemy_die` makes and the same caveat: transcribed, because
  // the word is already zero — `$81:D6DC  STZ $7E` is in this actor's own init.
  wram_w16(w, (uint32_t)dp + ACTOR_DP_SCRATCH_7E, 0);
  r->a = health;
  r->n = (dead & 0x8000) != 0;
  r->z = dead == 0;
  r->c = true;  // `$81:D82B  SEC : RTL` — which parks the thread
}

bool enemy_d7f6_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported) {
  if (arg < COLLIDE_ID_PLAYER) {
    // `$81:D7F6  CMP #$005C : BCS : CLC : RTL`. Two instructions and no writes
    // at all — not even the parked id `enemy_cdde_collide` clears here.
    //
    // **And that the store is absent cannot be checked either**, which is the
    // exact mirror of the finding recorded on `enemy_cdde_collide`'s ignore
    // path. There, deleting the ROM's `STZ $22` passed every call because the
    // word was already zero. Here, *adding* a store of zero to `$20` passes
    // every call for the same reason: nothing in the corpus has ever hit this
    // creature, so its parked id has never been anything but zero. A store the
    // diff cannot see and an absent store the diff cannot see are one fact from
    // two directions, and the input that settles both is the same one.
    PORT_COVER(d7f6_ignore);
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$81:D7FD  STA $20 : AND #$7FFF`.
  PORT_COVER(d7f6_hit);
  wram_w16(w, (uint32_t)dp + D7F6_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;

  if (id == ENEMY_HIT_SPECIAL_A) {
    // `CMP #$005E : BEQ $D825` — **into the death tail, not out to a routine**.
    // Both other copies of this comparison are a `JML $81:83C6`; this one is the
    // id that kills outright, which is `monster_collide`'s reading of `$5E`
    // rather than `enemy_collide`'s. What gets stored as health is therefore the
    // masked id itself, `$005E`, and no subtraction happens.
    PORT_COVER(d7f6_fatal_id);
    d7f6_die(w, dp, id, r);
    return true;
  }

  if (id == ENEMY_HIT_SPECIAL_B) {
    // `CMP #$005D : BEQ : JML $81:847E`, the one id it hands back.
    PORT_COVER(d7f6_special);
    return enemy_freeze(w, dp, r);
  }

  // `SEC : SBC #$005C : ASL A : TAX`, then `SEC : LDA $0C : SBC $818561,X`.
  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  uint16_t health = wram_r16(w, (uint32_t)dp + D7F6_DP_HEALTH);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));
  r->x = index;

  if (left & 0x8000) {
    PORT_COVER(d7f6_died);
    d7f6_die(w, dp, left, r);
    return true;
  }

  if (left == health) {
    // `CMP $0C : BEQ $D835`, a bare `CLC : RTL` of its own — a different exit
    // from the ignore path's, and it writes nothing either. With health seeded
    // to 1 this is the only way a hit leaves this creature alive at all, and it
    // needs a damage-table entry of zero to do it.
    PORT_COVER(d7f6_no_damage);
    r->a = left;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  // `STA $0C : JML $81:8506` — shared with three of the four other copies,
  // because this page keeps its display record at `$08` like they do.
  PORT_COVER(d7f6_survived);
  wram_w16(w, (uint32_t)dp + D7F6_DP_HEALTH, left);
  return enemy_survived_react(w, dp, r);
}

// ---------------------------------------------------------------------------
// $81:9B6B  enemy_9b6b_collide
// ---------------------------------------------------------------------------

bool enemy_9b6b_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported) {
  if (arg < COLLIDE_ID_PLAYER) {
    // `$81:9B6B  CMP #$005C : BCC : CLC : RTL`, writing nothing — `$81:D7F6`'s
    // spelling rather than `enemy_cdde_collide`'s.
    PORT_COVER(d9b6b_ignore);
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  PORT_COVER(d9b6b_hit);
  wram_w16(w, (uint32_t)dp + D9B6B_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;

  if (id == ENEMY_HIT_SPECIAL_A) {
    // `$81:9BA2`, nine instructions and then `JML $81:83C6`. For four rounds
    // this handed back, because those nine were the one thing between this copy
    // and a routine the port already had.
    PORT_COVER(d9b6b_fatal_id);
    (void)unported;
    return enemy_9b6b_bubble(w, dp, r);
  }
  if (id == ENEMY_HIT_SPECIAL_B) {
    PORT_COVER(d9b6b_special);
    return enemy_freeze(w, dp, r);
  }

  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  uint16_t health = wram_r16(w, (uint32_t)dp + D9B6B_DP_HEALTH);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));
  r->x = index;

  if (left & 0x8000) {
    // `$81:9B9A  DEC $26 : STA $32 : STZ $7E : SEC : RTL`. No award, and the
    // `DEC` is the last instruction here to set a flag.
    PORT_COVER(d9b6b_died);
    uint16_t count =
        (uint16_t)(wram_r16(w, (uint32_t)dp + D9B6B_DP_COUNTER_26) - 1);
    wram_w16(w, (uint32_t)dp + D9B6B_DP_COUNTER_26, count);
    wram_w16(w, (uint32_t)dp + D9B6B_DP_HEALTH, left);
    wram_w16(w, (uint32_t)dp + ACTOR_DP_SCRATCH_7E, 0);
    r->a = left;
    r->n = (count & 0x8000) != 0;
    r->z = count == 0;
    r->c = true;
    return true;
  }

  if (left == health) {
    PORT_COVER(d9b6b_no_damage);
    r->a = left;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  PORT_COVER(d9b6b_survived);
  wram_w16(w, (uint32_t)dp + D9B6B_DP_HEALTH, left);
  return enemy_survived_react(w, dp, r);
}

// ---------------------------------------------------------------------------
// $81:9BA2  enemy_9b6b_bubble — the tally in front of the splice
// ---------------------------------------------------------------------------

bool enemy_9b6b_bubble(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  // `$81:9BA2  TYA : ASL A : AND #$0000 : ROL A : ROL A`. Bit 15 of **Y** — the
  // raw collision id the dispatcher left there, sign bit still on it — doubled
  // into a side. The same five instructions `enemy_freeze` runs at `$81:8493`,
  // and this is the second place the port runs them rather than reading them.
  uint16_t side = (uint16_t)((r->y & 0x8000) ? 2 : 0);

  // `$81:9BA9  JSL $80:9D6A` — `score_slot` with one comparison instead of two,
  // so a side matching neither slot comes back as slot 1 rather than "nobody".
  uint16_t slot = side == wram_r16(w, W_SCORE_SLOT_SIDE) ? 0 : 2;
  PORT_COVER_IF(slot == 0, d9b6b_bubble_slot_0, d9b6b_bubble_slot_1);

  // `$81:9BAD  INC $1FDC,X`, and `$82:C9AE` is the screen that reads it.
  uint16_t at = (uint16_t)(D9B6B_FATAL_COUNT + slot);
  wram_w16(w, at, (uint16_t)(wram_r16(w, at) + 1));

  // `$81:9BB0  JML $81:83C6` — a jump and not a call, so whatever the splice
  // leaves in the registers is what this branch returns.
  return enemy_bubble_react(w, dp, r);
}

// ---------------------------------------------------------------------------
// $81:F534  actor_f534_collide
// ---------------------------------------------------------------------------

bool actor_f534_collide(Wram* w, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r) {
  r->a = arg;    // nothing here writes A
  r->c = false;  // all three exits are `CLC : RTL`

  if (arg == F534_LATCH_A || arg == F534_LATCH_B || arg == F534_LATCH_C) {
    // `$81:F54F  STA $3E : CLC : RTL`. The `STA` sets no flags, so what comes
    // back is the matching `CMP`'s — and a match is equality.
    PORT_COVER(f534_latch);
    wram_w16(w, (uint32_t)dp + F534_DP_LATCH, arg);
    r->n = false;
    r->z = true;
    return true;
  }

  if (arg == F534_LATCH_GUARDED_A || arg == F534_LATCH_GUARDED_B) {
    // `$81:F553  LDX $06 : CPX #$0004 : BNE +2 : STA $3E`. The store is skipped
    // unless the guard word is exactly four, and **the flags come from the
    // `CPX` either way** — not from the `CMP` that got here — because the `STA`
    // sets none and the `BNE` only jumps over it.
    uint16_t guard = wram_r16(w, (uint32_t)dp + F534_DP_GUARD);
    r->x = guard;
    if (guard == F534_GUARD_VALUE) {
      PORT_COVER(f534_latch_guarded);
      wram_w16(w, (uint32_t)dp + F534_DP_LATCH, arg);
      r->n = false;
      r->z = true;
    } else {
      PORT_COVER(f534_guard_refused);
      uint16_t diff = (uint16_t)(guard - F534_GUARD_VALUE);
      r->n = (diff & 0x8000) != 0;
      r->z = false;
    }
    return true;
  }

  // `$81:F54D  CLC : RTL`, off the end of all five comparisons — so the flags
  // are the *last* one's, `CMP #$0006`.
  PORT_COVER(f534_ignore);
  uint16_t diff = (uint16_t)(arg - F534_LATCH_GUARDED_B);
  r->n = (diff & 0x8000) != 0;
  r->z = false;  // `arg == $06` took the branch above
  return true;
}

// ---------------------------------------------------------------------------
// $83:A264  victim_a264_collide
// ---------------------------------------------------------------------------

// `$81:8191`, nine bytes, inlined for `$81:B168`'s reason: a routine that sets
// one byte behind one guard is not a routine worth a registry entry. It is also
// the first thing in the port to write above `$7E:2000`.
static void a264_flag_set(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  uint16_t index = wram_r16(w, (uint32_t)dp + A264_DP_ARRAY_INDEX);
  r->a = index;
  if (index == A264_INDEX_NONE) {
    PORT_COVER(a264_flag_none);
    // `CMP #$FFFF : BEQ` — equality, so Z set and N clear, and X is untouched.
    r->n = false;
    r->z = true;
    return;
  }
  PORT_COVER(a264_flag_set);
  wram_w8(w, W_A264_FLAG_ARRAY + (uint32_t)index, A264_FLAG_SET);
  r->x = index;
  // `SEP #$20 : LDA #$80 : STA : REP #$30`. The `LDA` is eight bits wide, so
  // what it leaves in the accumulator is `$80` in the low byte over whatever the
  // high byte held — and the high byte here is the index's, because `TAX` did
  // not disturb A. N comes from bit 7 of an 8-bit load, and `REP` does not
  // change it.
  r->a = (uint16_t)((index & 0xff00) | A264_FLAG_SET);
  r->n = true;
  r->z = false;
}

bool victim_a264_collide(Wram* w, uint16_t dp, uint16_t arg,
                         ActorHandlerRegs* r) {
  r->a = arg;

  if (arg == A264_ID_GIVE_UP_FF || arg == A264_ID_GIVE_UP_A ||
      arg == A264_ID_GIVE_UP_B) {
    // `$83:A2A5  LDA $06 : JSL $81:8191 : LDA #$0003 : STA $1E : SEC : RTL`.
    PORT_COVER(a264_give_up);
    a264_flag_set(w, dp, r);
    wram_w16(w, (uint32_t)dp + A264_DP_EVENT, A264_EVENT_GIVE_UP);
    r->a = A264_EVENT_GIVE_UP;
    r->n = false;
    r->z = false;
    r->c = true;  // which parks the thread, as a victim's ending does
    return true;
  }

  if (arg == A264_ID_CLAIM_A || arg == A264_ID_CLAIM_B) {
    // `$83:A293  LDA #$8000` for id `$06`, falling into `$83:A296  STA $18` —
    // and id `$05` enters at the `STA` with the id still in A. **The same
    // three-byte saving `victim_collide` makes**, and the same consequence:
    // one side latches `$8000` and the other latches `$0005`, which is what
    // `score_add` reads bit 15 of.
    uint16_t claimant = arg;
    if (arg == A264_ID_CLAIM_B) {
      PORT_COVER(a264_claim_b);
      claimant = 0x8000;
    } else {
      PORT_COVER(a264_claim_a);
    }
    wram_w16(w, (uint32_t)dp + A264_DP_CLAIMANT, claimant);
    a264_flag_set(w, dp, r);
    wram_w16(w, (uint32_t)dp + A264_DP_EVENT, A264_EVENT_CLAIMED);
    r->a = A264_EVENT_CLAIMED;
    r->n = false;
    r->z = false;
    r->c = true;
    return true;
  }

  if (arg == A264_ID_IGNORE_A || arg == A264_ID_IGNORE_B) {
    // `$83:A2B2  CLC : RTL`, reached by name from two `BEQ`s rather than by
    // falling off the end — so Z is set here where the fall-through's is not.
    PORT_COVER(a264_ignore_named);
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  // `$83:A287  AND #$7FFF : CMP #$005C : BCC $A2B2`. Everything that is left is
  // sorted by whether it is a weapon shot, and this is the only handler in the
  // project where a shot is answered by *clearing* the event word.
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;
  if (id < COLLIDE_ID_PLAYER) {
    PORT_COVER(a264_ignore_low);
    uint16_t diff = (uint16_t)(id - COLLIDE_ID_PLAYER);
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$83:A28F  STZ $1E : SEC : RTL`. The `STZ` sets no flags, so the `CMP`'s
  // stand — and it did not borrow.
  PORT_COVER(a264_shot_clears);
  wram_w16(w, (uint32_t)dp + A264_DP_EVENT, 0);
  r->n = false;
  r->z = id == COLLIDE_ID_PLAYER;
  r->c = true;
  return true;
}

// ---------------------------------------------------------------------------
// $81:9063  enemy_9063_collide
// ---------------------------------------------------------------------------

bool enemy_9063_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported) {
  if (arg < COLLIDE_ID_PLAYER) {
    PORT_COVER(d9063_ignore);
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  PORT_COVER(d9063_hit);
  wram_w16(w, (uint32_t)dp + D9063_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;

  if (id == ENEMY_HIT_SPECIAL_A) {
    PORT_COVER(d9063_fatal_id);
    return enemy_bubble_react(w, dp, r);
  }
  if (id == ENEMY_HIT_SPECIAL_B) {
    PORT_COVER(d9063_special);
    return enemy_freeze(w, dp, r);
  }

  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  uint16_t health = wram_r16(w, (uint32_t)dp + D9063_DP_HEALTH);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));
  r->x = index;

  if (left & 0x8000) {
    // `$81:9092  DEC $2E : STA $22 : STZ $7E : SEC : RTL`.
    PORT_COVER(d9063_died);
    uint16_t count =
        (uint16_t)(wram_r16(w, (uint32_t)dp + D9063_DP_COUNTER_2E) - 1);
    wram_w16(w, (uint32_t)dp + D9063_DP_COUNTER_2E, count);
    wram_w16(w, (uint32_t)dp + D9063_DP_HEALTH, left);
    wram_w16(w, (uint32_t)dp + ACTOR_DP_SCRATCH_7E, 0);
    r->a = left;
    r->n = (count & 0x8000) != 0;
    r->z = count == 0;
    r->c = true;
    return true;
  }

  if (left == health) {
    PORT_COVER(d9063_no_damage);
    r->a = left;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  PORT_COVER(d9063_survived);
  wram_w16(w, (uint32_t)dp + D9063_DP_HEALTH, left);
  return enemy_survived_react(w, dp, r);
}

// ---------------------------------------------------------------------------
// $81:AC92  enemy_ac92_collide
// ---------------------------------------------------------------------------

// `$81:ACC6  DEC $10 : STA $3C : STZ $7E : SEC : RTL`, reached two ways — by a
// negative difference and by `AC92_HIT_FATAL`, which arrives with the masked id
// in A instead. `DEC` is the last instruction here that sets a flag, so N and Z
// describe the *counter* and not the health, exactly as in `enemy_9063_collide`.
static void ac92_die(Wram* w, uint16_t dp, uint16_t health,
                     ActorHandlerRegs* r) {
  uint16_t count =
      (uint16_t)(wram_r16(w, (uint32_t)dp + DAC92_DP_COUNTER_10) - 1);
  wram_w16(w, (uint32_t)dp + DAC92_DP_COUNTER_10, count);
  wram_w16(w, (uint32_t)dp + DAC92_DP_HEALTH, health);
  // The ninth `STZ $7E`, and the first one written after the store had a reader.
  // `enemy_freeze` is the only thing in the game that makes this word non-zero,
  // and level 49's bubble gun is id `$5E` rather than `$5D`, so on this creature
  // it is zero again — transcribed, not diffed, and now for a stated reason
  // rather than an unexplained one.
  wram_w16(w, (uint32_t)dp + ACTOR_DP_SCRATCH_7E, 0);
  r->a = health;
  r->n = (count & 0x8000) != 0;
  r->z = count == 0;
  r->c = true;
}

bool enemy_ac92_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported) {
  if (arg < COLLIDE_ID_PLAYER) {
    // `$81:AC97  CLC : RTL`, and — like `enemy_9063_collide`'s and unlike
    // `enemy_cdde_collide`'s — it does not even park the id first.
    PORT_COVER(dac92_ignore);
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$81:AC99  STA $3E : AND #$7FFF`.
  PORT_COVER(dac92_hit);
  wram_w16(w, (uint32_t)dp + DAC92_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;

  if (id == ENEMY_HIT_SPECIAL_A) {
    // `CMP #$005E : BEQ $ACCE`, and `$81:ACCE  JML $81:83C6` — served now.
    PORT_COVER(dac92_special);
    return enemy_bubble_react(w, dp, r);
  }
  if (id == ENEMY_HIT_SPECIAL_B) {
    // `CMP #$005D : BEQ $ACD2`, and `$81:ACD2  JML $81:847E`.
    PORT_COVER(dac92_freeze);
    return enemy_freeze(w, dp, r);
  }
  if (id == AC92_HIT_FATAL) {
    // `CMP #$0067 : BEQ $ACC6` — straight into the death tail with the id still
    // in A, so `$005E`'s trick at a different address and a different id.
    PORT_COVER(dac92_fatal_id);
    ac92_die(w, dp, id, r);
    return true;
  }

  // `SEC : SBC #$005C : ASL A : TAX`, then `SEC : LDA $3C : SBC $818561,X`.
  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  uint16_t health = wram_r16(w, (uint32_t)dp + DAC92_DP_HEALTH);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));
  r->x = index;

  if (left & 0x8000) {
    PORT_COVER(dac92_died);
    ac92_die(w, dp, left, r);
    return true;
  }

  if (left == health) {
    // `$81:ACD6  CLC : RTL`, a second bare exit and a different one from the
    // ignore path's.
    PORT_COVER(dac92_no_damage);
    r->a = left;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  PORT_COVER(dac92_survived);
  wram_w16(w, (uint32_t)dp + DAC92_DP_HEALTH, left);
  return enemy_survived_react(w, dp, r);
}

// ---------------------------------------------------------------------------
// $81:E6E4  enemy_e6e4_collide
// ---------------------------------------------------------------------------

// The tail at `$81:E71A`, and the fifth spelling of it in this family:
// `DEC $0A : STA $0C : STZ $7E : SEC : RTL`. Reached only by a borrow, so —
// unlike `d7f6_die` and `ac92_die` — there is no id-that-kills-outright path
// into it and `health` is always the negative difference.
static void e6e4_die(Wram* w, uint16_t dp, uint16_t health,
                     ActorHandlerRegs* r) {
  uint16_t count = (uint16_t)(wram_r16(w, (uint32_t)dp + E6E4_DP_COUNTER_0A) - 1);
  wram_w16(w, (uint32_t)dp + E6E4_DP_COUNTER_0A, count);
  wram_w16(w, (uint32_t)dp + E6E4_DP_HEALTH, health);
  // The tenth `STZ $7E`, and the second written with a stated reason rather than
  // an unexplained one: `enemy_freeze` is the only writer of that word, and on
  // this creature `$5D` never reaches the death tail — it leaves two
  // comparisons earlier. So it is zero on every call for the same reason
  // `enemy_ac92_collide`'s is.
  wram_w16(w, (uint32_t)dp + ACTOR_DP_SCRATCH_7E, 0);
  r->a = health;
  r->n = (count & 0x8000) != 0;
  r->z = count == 0;
  r->c = true;
}

bool enemy_e6e4_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported) {
  (void)unported;  // both of its two `JML`s are served — see the header
  // `$81:E6E4  LDY $08 : LDX $0004,Y : BNE $E72A`. Y and X are both outputs —
  // the record address and the height — and they are set on *every* path
  // through this routine, because nothing below writes Y and only the damage
  // path writes X.
  uint16_t record = wram_r16(w, (uint32_t)dp + E6E4_DP_RECORD);
  uint16_t z = wram_r16(w, (uint32_t)record + ACTOR_Z);
  r->y = record;
  r->x = z;
  if (z != 0) {
    // `$81:E72A  CLC : RTL`, shared with the no-damage exit. The `LDX` is the
    // last instruction to set a flag, so N and Z describe the height and not
    // the argument — which is the one place this routine's flags differ from
    // every other copy's, and the only reason A is left alone here.
    PORT_COVER(e6e4_airborne);
    r->n = (z & 0x8000) != 0;
    r->z = false;  // it is not zero; that is why we are here
    r->c = false;
    return true;
  }

  r->a = arg;
  if (arg < COLLIDE_ID_PLAYER) {
    // `$81:E6F0  CLC : RTL`, a second bare exit — and, like
    // `enemy_d7f6_collide`'s and unlike `enemy_cdde_collide`'s, it does not
    // park the id first.
    PORT_COVER(e6e4_ignore);
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$81:E6F2  STA $22 : AND #$7FFF`.
  PORT_COVER(e6e4_hit);
  wram_w16(w, (uint32_t)dp + E6E4_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;

  if (id == ENEMY_HIT_SPECIAL_A) {
    // `CMP #$005E : BEQ $E722`, and `$81:E722  JML $81:83C6`.
    PORT_COVER(e6e4_bubble);
    return enemy_bubble_react(w, dp, r);
  }
  if (id == ENEMY_HIT_SPECIAL_B) {
    // `CMP #$005D : BEQ $E726`, and `$81:E726  JML $81:847E`.
    PORT_COVER(e6e4_freeze);
    return enemy_freeze(w, dp, r);
  }

  // `SEC : SBC #$005C : ASL A : TAX`, then `SEC : LDA $0C : SBC $818561,X` —
  // the same arithmetic against the same table for the tenth time, and the
  // `TAX` is what overwrites the height the guard left in X.
  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  uint16_t health = wram_r16(w, (uint32_t)dp + E6E4_DP_HEALTH);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));
  r->x = index;

  if (left & 0x8000) {
    PORT_COVER(e6e4_died);
    e6e4_die(w, dp, left, r);
    return true;
  }

  if (left == health) {
    // `$81:E712  CMP $0C : BEQ $E72A` — back to the *guard's* exit rather than
    // to a bare one of its own. This is the only copy in the family where the
    // no-damage path and the "off the ground" path are the same two bytes.
    PORT_COVER(e6e4_no_damage);
    r->a = left;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  PORT_COVER(e6e4_survived);
  wram_w16(w, (uint32_t)dp + E6E4_DP_HEALTH, left);
  return enemy_survived_react(w, dp, r);
}

// ---------------------------------------------------------------------------
// $81:845E  actor_845e_collide
// ---------------------------------------------------------------------------

bool actor_845e_collide(uint16_t arg, ActorHandlerRegs* r) {
  // The three named ids come first and are matched against the *unmasked*
  // argument. An equal `CMP` is the last flag-setting instruction on each of
  // those exits, so Z set and N clear; the following `SEC` supplies the carry.
  if (arg == D845E_PARK_A || arg == D845E_PARK_B || arg == D845E_PARK_C) {
    PORT_COVER(d845e_park_named);
    r->a = arg;
    r->n = false;
    r->z = true;
    r->c = true;
    return true;
  }

  // `AND #$7FFF` — and from here on A is the masked id, on every remaining exit.
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;

  if (id < COLLIDE_ID_PLAYER) {
    // `CMP #$005C : BCC $847A`, a bare `CLC : RTL`. The comparison is what sets
    // N and Z, and it borrowed, which is what the `BCC` took.
    PORT_COVER(d845e_ignore);
    uint16_t diff = (uint16_t)(id - COLLIDE_ID_PLAYER);
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  if (id == D845E_PASS_ID) {
    // `CMP #$005E : BNE $847C` falls *through* on equal, into the same
    // `CLC : RTL` the ignore path uses. Z and N come from that comparison and
    // carry from the `CLC` that undoes its borrow-free result.
    PORT_COVER(d845e_pass);
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  // `$81:847C  SEC : RTL` — the thread parks. N and Z are the `CMP #$005E`'s.
  PORT_COVER(d845e_park);
  uint16_t diff = (uint16_t)(id - D845E_PASS_ID);
  r->n = (diff & 0x8000) != 0;
  r->z = false;
  r->c = true;
  return true;
}

// ---------------------------------------------------------------------------
// $81:EDAA  shot_edaa_collide
// ---------------------------------------------------------------------------

bool shot_edaa_collide(void) {
  // One byte, `6B`. There is nothing to write, nothing to read, nothing to
  // branch on and so no coverage site — a routine with no decision in it has no
  // untaken branch. What the shim claims is the whole of the interface, and it
  // claims all of it: see the note in `collide.h`.
  return true;
}

// ---------------------------------------------------------------------------
// $81:F6A3  shot_f6a3_collide
// ---------------------------------------------------------------------------

bool shot_f6a3_collide(Wram* w, uint16_t dp, uint16_t arg,
                       ActorHandlerRegs* r) {
  // A is never written — `CMP` does not touch it and `STA` does not either — so
  // the argument comes back in A on both exits, and X and Y are the
  // dispatcher's.
  r->a = arg;
  // Both exits are `CLC : RTL`. There is no `SEC` anywhere in the routine and,
  // unlike `$82:F1C2`, that really does settle carry: the `CLC` is executed on
  // every path, so the comparisons' carry never escapes.
  r->c = false;

  if (arg == SHOT_F6A3_RECORD_A || arg == SHOT_F6A3_RECORD_B ||
      arg == SHOT_F6A3_RECORD_C) {
    // `$81:F6B4  STA $3E`, which sets no flags — so N and Z are the matching
    // `CMP`'s, and an equal comparison is Z set and N clear whichever of the
    // three it was.
    PORT_COVER(f6a3_record);
    wram_w16(w, (uint32_t)dp + SHOT_F6A3_DP_HIT_ID, arg);
    r->n = false;
    r->z = true;
    return true;
  }

  // Fell off the end of all three comparisons, so the flags are the *last*
  // one's — `arg - $0001` — and not the first's.
  PORT_COVER(f6a3_ignore);
  uint16_t diff = (uint16_t)(arg - SHOT_F6A3_RECORD_C);
  r->n = (diff & 0x8000) != 0;
  r->z = false;
  return true;
}

// ---------------------------------------------------------------------------
// $82:F4EF  actor_f4ef_collide
// ---------------------------------------------------------------------------

bool actor_f4ef_collide(Wram* w, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r) {
  // Nothing here writes A, so the argument is what comes back on both exits.
  r->a = arg;
  // `CLC : RTL` twice, with no `SEC` in the routine at all — so this actor can
  // never park its thread either, and standing on it is a thing it notices
  // rather than a thing that stops it.
  r->c = false;

  if (arg == F4EF_LATCH_P1 || arg == F4EF_LATCH_P2) {
    // `$82:F4FB  STA $18`, which sets no flags — so N and Z are the matching
    // `CMP`'s, and an equal comparison is Z set and N clear for either id.
    PORT_COVER(f4ef_player);
    wram_w16(w, (uint32_t)dp + F4EF_DP_HIT_ID, arg);
    r->n = false;
    r->z = true;
    return true;
  }

  // Fell off the end of both comparisons, so the flags are the *second* one's —
  // `arg - $0006`. An id of `$0005` never gets here, having branched at the
  // first, which is why subtracting `F4EF_LATCH_P2` is the right spelling and
  // cannot come out zero.
  PORT_COVER(f4ef_ignore);
  uint16_t diff = (uint16_t)(arg - F4EF_LATCH_P2);
  r->n = (diff & 0x8000) != 0;
  r->z = false;
  return true;
}

// ---------------------------------------------------------------------------
// $82:DEEB  actor_deeb_collide
// ---------------------------------------------------------------------------

bool actor_deeb_collide(Wram* w, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r) {
  // `CMP #$00FF` is the only instruction in the routine that sets a flag, so
  // both exits carry its N and Z — and an equal comparison is Z set, N clear.
  r->a = arg;
  if (arg != DEEB_ID_STOP) {
    PORT_COVER(deeb_ignore);
    uint16_t diff = (uint16_t)(arg - DEEB_ID_STOP);
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }
  PORT_COVER(deeb_stop);
  wram_w16(w, (uint32_t)dp + DEEB_DP_LATCH, arg);
  r->n = false;
  r->z = true;
  r->c = true;  // `$82:DEF2  SEC` before the store — this parks the thread
  return true;
}

// ---------------------------------------------------------------------------
// $82:F1C2  actor_f1c2_collide
// ---------------------------------------------------------------------------

bool actor_f1c2_collide(Wram* w, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r) {
  // `$82:F1C2  TAY` — and nothing puts Y back, so the id is an output too.
  r->a = arg;
  r->y = arg;

  bool act = arg == F1C2_ID_A || arg == F1C2_ID_B || arg == F1C2_ID_C;
  uint16_t id = arg;
  if (!act) {
    // `AND #$7FFF : CMP #$005C : BCS`. The mask happens only on this path, so
    // what comes back in A is the raw id for the three named ids and the masked
    // one for everything else.
    id = arg & ENEMY_COLLIDE_ID_MASK;
    r->a = id;
    act = id >= COLLIDE_ID_PLAYER;
    if (!act) {
      // `$82:F1DA  RTL`, with no `CLC` in front of it — and carry is still
      // *clear*, because the `CMP #$005C` that decided this borrowed. See the
      // note on `ACTOR_F1C2_COLLIDE_ENTRY`: this is where the first reading of
      // the routine was wrong and the diff said so.
      PORT_COVER(f1c2_ignore);
      uint16_t diff = (uint16_t)(id - COLLIDE_ID_PLAYER);
      r->n = (diff & 0x8000) != 0;
      r->z = false;
      r->c = false;
      return true;
    }
    PORT_COVER(f1c2_act_shot);
  } else {
    PORT_COVER(f1c2_act_id);
  }

  // `LDY $08 : LDA #$0000 : STA $000E,Y : DEC $14 : RTL`. Switching its own
  // collision off, the way a spent shot and a claimed victim do.
  uint16_t record = wram_r16(w, (uint32_t)dp + F1C2_DP_RECORD);
  wram_w16(w, (uint32_t)record + ACTOR_COLLIDE_ID, 0);
  uint16_t count = (uint16_t)(wram_r16(w, (uint32_t)dp + F1C2_DP_COUNTER_14) - 1);
  wram_w16(w, (uint32_t)dp + F1C2_DP_COUNTER_14, count);
  // `LDY $08` replaced the `TAY`, and `LDA #$0000` replaced the id.
  r->a = 0;
  r->y = record;
  // The `DEC` is the last flag-setter, and it reads the word in memory.
  r->n = (count & 0x8000) != 0;
  r->z = count == 0;
  // All four ways in got here through a comparison that did not borrow — three
  // of them equal, the fourth the `BCS` — so carry is set on every one.
  r->c = true;
  return true;
}

// ---------------------------------------------------------------------------
// $82:9660  boss_9660_collide
// ---------------------------------------------------------------------------

bool boss_9660_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                       ActorHandlerRegs* r) {
  // `$82:9660  LDY $0078 : LDX $000E,Y`. Absolute, not direct — its own display
  // record out of the global the dispatcher published, and then that record's
  // `ACTOR_COLLIDE_ID`. Y is never touched again, so this is also what comes
  // back in Y on all seven exits.
  uint16_t record = wram_r16(w, W_HANDLER_SELF);
  uint16_t self_id = wram_r16(w, (uint32_t)record + ACTOR_COLLIDE_ID);
  r->y = record;
  r->a = arg;  // nothing before the `STA $42` writes A

  if (self_id == BOSS_9660_ID_INVULNERABLE) {
    // `CPX #$0009 : BEQ $9674`, and `$9674` is `STZ $42 : CLC : RTL`. The
    // comparison is the last thing to set N and Z, and it found them equal.
    PORT_COVER(boss_invulnerable);
    r->x = self_id;
    wram_w16(w, (uint32_t)dp + BOSS_9660_DP_HIT_ID, 0);
    r->n = false;
    r->z = true;
    r->c = false;  // `CLC`
    return true;
  }

  // `$82:966B  LDX $40 : BNE $9674` — the flash timer, which is read on every
  // call and decides two of the three ignore paths between them.
  uint16_t flash = wram_r16(w, (uint32_t)dp + BOSS_9660_DP_FLASH);
  r->x = flash;
  if (flash != 0) {
    // The `LDX` is what set N and Z here, from the timer rather than from any
    // comparison — so a flash timer with bit 15 set would come back N, which is
    // a thing the port has to reproduce even though `$82:8F86` only ever writes
    // 3.
    PORT_COVER(boss_flashing);
    wram_w16(w, (uint32_t)dp + BOSS_9660_DP_HIT_ID, 0);
    r->n = (flash & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  if (arg < COLLIDE_ID_PLAYER) {
    // `CMP #$005C : BCS $9678`, falling through into the same three
    // instructions. The comparison is unsigned and `arg` still carries bit 15,
    // so a second player's shot (`$805C`) is above the line exactly as a first
    // player's is.
    PORT_COVER(boss_ignore);
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    wram_w16(w, (uint32_t)dp + BOSS_9660_DP_HIT_ID, 0);
    r->n = (diff & 0x8000) != 0;
    r->z = false;  // `arg == $5C` took the branch
    r->c = false;
    return true;
  }

  // `$82:9678  STA $42 : AND #$7FFF`. The parked id keeps bit 15, because the
  // death sequence hands this very word to `score_add` to decide whose $2000 it
  // is; the masked copy is what the comparisons below work on.
  wram_w16(w, (uint32_t)dp + BOSS_9660_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;

  // The four rewrites. Nothing else in the game does this, and two of them are
  // decided by `LDA $0020 : AND #$0001` / `AND #$0003` — the low bits of the
  // scheduler tick, read straight rather than through `$80:9D39`. The tick is a
  // 32-bit counter at `W_SCHED_TICK` and only its low word is loaded.
  uint16_t tick = wram_r16(w, W_SCHED_TICK);
  if (id == BOSS_9660_ID_ALT_HALF || id == BOSS_9660_ID_ALT_QUARTER) {
    uint16_t mask = id == BOSS_9660_ID_ALT_HALF ? 0x0001 : 0x0003;
    if (tick & mask) {
      PORT_COVER(boss_alt_dear);
      id = BOSS_9660_ID_DEAR;
    } else {
      PORT_COVER(boss_alt_cheap);
      id = BOSS_9660_ID_CHEAP;
    }
  } else if (id == BOSS_9660_ID_REMAP_61) {
    PORT_COVER(boss_remap_61);
    id = BOSS_9660_ID_61_AS;
  } else if (id == BOSS_9660_ID_REMAP_6F) {
    PORT_COVER(boss_remap_6f);
    id = BOSS_9660_ID_6F_AS;
  }

  // `$82:96BA  SEC : SBC #$005C : ASL A : TAX`, the same index the whole enemy
  // family builds, into the same `ENEMY_DAMAGE_TABLE` — so a boss and a level-1
  // zombie read one table and differ only in what they do with the answer.
  PORT_COVER(boss_hit);
  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  r->x = index;

  // `DEC $3E : DEC $44` — both unconditional, and both before the subtraction,
  // so a hit that turns out to do no damage at all still flashes the boss and
  // still counts against its phase.
  wram_w16(w, (uint32_t)dp + BOSS_9660_DP_HIT_FLAG,
           (uint16_t)(wram_r16(w, (uint32_t)dp + BOSS_9660_DP_HIT_FLAG) - 1));
  wram_w16(w, (uint32_t)dp + BOSS_9660_DP_PHASE_COUNT,
           (uint16_t)(wram_r16(w, (uint32_t)dp + BOSS_9660_DP_PHASE_COUNT) - 1));

  uint16_t health = wram_r16(w, (uint32_t)dp + BOSS_9660_DP_HEALTH);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));
  r->a = left;
  // Every one of the three damage exits is `SEC : RTL`, so **each of them parks
  // the boss's thread** — `thread_call_handler`'s `handler_park`, which until
  // now only a dying enemy reached. That is the ROM's own arithmetic and not a
  // reading of it: `$82:96D7  38 6B`, with all three branches falling into it.
  r->c = true;

  if (left & 0x8000) {
    // `$82:96D5  DEC $3A`, and nothing else — **the negative health is not
    // stored**, which is where this parts company with `enemy_collide` and both
    // of its copies. What ends the boss is the flag, not the number.
    PORT_COVER(boss_died);
    uint16_t dead = (uint16_t)(wram_r16(w, (uint32_t)dp + BOSS_9660_DP_DEAD) - 1);
    wram_w16(w, (uint32_t)dp + BOSS_9660_DP_DEAD, dead);
    // The `DEC` is the last instruction to set a flag, so N and Z describe the
    // word in memory rather than the subtraction that got here.
    r->n = (dead & 0x8000) != 0;
    r->z = dead == 0;
    return true;
  }

  if (left == health) {
    // `CMP $3C : BEQ $96D7`. A damage-table entry of zero, and the store is
    // skipped as pointless — but `$3E` and `$44` above already moved, so unlike
    // `enemy_collide`'s equivalent this one is not invisible.
    PORT_COVER(boss_no_damage);
    r->n = false;
    r->z = true;
    return true;
  }

  // `$82:96D1  STA $3C`, which sets no flags — so N and Z are the `CMP $3C`'s,
  // comparing a difference that is smaller than the health it came from.
  PORT_COVER(boss_survived);
  wram_w16(w, (uint32_t)dp + BOSS_9660_DP_HEALTH, left);
  r->n = ((uint16_t)(left - health) & 0x8000) != 0;
  r->z = false;
  return true;
}

// ---------------------------------------------------------------------------
// $81:D301  enemy_d301_collide
// ---------------------------------------------------------------------------

// `$81:D33E`, the tail a survivor runs after `enemy_survived_react` comes back.
// It is reached by `JMP` from the instruction after the `JSL`, so it is the rest
// of this call rather than a call of its own — `$80:DC09`'s situation exactly.
//
// `carry_in` is the reaction's carry and it is a genuine input: `rng_next`'s
// `ROL` shifts it in.
static void d301_survived_tail(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  RngResult rng;
  rng_next(w, r->c, &rng);

  // `CMP #$0019 : BCS $D35E`, and `$D35E` is `SEC : RTL`. Twenty-five draws in
  // 256 do something; the other 231 leave with the comparison's flags standing
  // and the `SEC` over the top of its carry.
  if (rng.a >= D301_RESEED_CHANCE) {
    PORT_COVER(d301_no_reseed);
    r->a = rng.a;
    uint16_t diff = (uint16_t)(rng.a - D301_RESEED_CHANCE);
    r->n = (diff & 0x8000) != 0;
    r->z = rng.a == D301_RESEED_CHANCE;
    r->c = true;
    return;
  }

  // `JSR $81:D142`, three instructions — `LDA #$D148 : STA $14 : RTS` — inlined
  // for `$81:B168`'s reason: a routine that stores one constant is not a routine
  // worth a registry entry.
  PORT_COVER(d301_reseed);
  wram_w16(w, (uint32_t)dp + D301_NEXT_ROUTINE, D301_NEXT_ROUTINE_HURT);

  // `LDA $10 : STA $1A : STA $1E : STA $22 : STA $26`, then the same for `$12`
  // into the odd halves. Four of the eight trail slots put back to where the
  // actor actually is — the same four `$81:D210` seeds when it is built.
  uint16_t x = wram_r16(w, (uint32_t)dp + D301_DP_X);
  uint16_t y = wram_r16(w, (uint32_t)dp + D301_DP_Y);
  for (int i = 0; i < D301_DP_TRAIL_RESEED; i++) {
    uint32_t slot = (uint32_t)dp + D301_DP_TRAIL + i * D301_DP_TRAIL_STRIDE;
    wram_w16(w, slot, x);
    wram_w16(w, slot + 2, y);
  }

  // The `LDA $12` is the last flag-setter — the stores under it set none — and
  // then `SEC : RTL`.
  r->a = y;
  r->n = (y & 0x8000) != 0;
  r->z = y == 0;
  r->c = true;
}

bool enemy_d301_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                        ActorHandlerRegs* r, uint32_t* unported) {
  r->a = arg;

  if (arg == D301_ID_STOP) {
    // `$81:D301  CMP #$00FF : BEQ $D362`, and `$D362` is `INC $0C : SEC : RTL`.
    // The positive verdict: the body's next pass sees a non-zero, non-negative
    // `$0C` and takes itself apart without paying anybody.
    //
    // **This is the whole of what the corpus checks.** All three calls level 9
    // makes carry this id.
    PORT_COVER(d301_stop);
    uint16_t verdict =
        (uint16_t)(wram_r16(w, (uint32_t)dp + D301_DP_VERDICT) + 1);
    wram_w16(w, (uint32_t)dp + D301_DP_VERDICT, verdict);
    // The `INC` reads and writes memory, so N and Z describe the word rather
    // than the `CMP` that got here.
    r->n = (verdict & 0x8000) != 0;
    r->z = verdict == 0;
    r->c = true;
    return true;
  }

  if (arg < COLLIDE_ID_PLAYER) {
    // `$81:D306  CMP #$005C : BCC $D360` — spelled as a branch *to* the ignore
    // exit rather than past it, which changes nothing but is why the listing
    // does not rhyme with the other seven at a glance. `$D360` is `CLC : RTL`
    // and writes nothing.
    PORT_COVER(d301_ignore);
    uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$81:D30B  STA $36 : AND #$7FFF`. The raw id, sign bit and all, because the
  // body's award reads it to decide whose points these are.
  wram_w16(w, (uint32_t)dp + D301_DP_HIT_ID, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;

  if (id == COLLIDE_ID_PLAYER) {
    // `$81:D310  CMP #$005C : BEQ $D360`, and **no other copy of this routine
    // has this comparison**. The ordinary player shot is parked and then thrown
    // away: in the other seven `$5C` is index zero of `ENEMY_DAMAGE_TABLE` and
    // costs a point, and here it costs nothing at all. An equal `CMP` leaves Z
    // set and carry set, and the `CLC` at the exit takes the carry back.
    PORT_COVER(d301_shot_immune);
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  if (id == ENEMY_HIT_SPECIAL_B) {
    // `$81:D315  CMP #$005D : BEQ $D33A : JML $81:847E`, the family's one
    // hand-back. There is no `CMP #$005E` here — `$5E` falls through to the
    // damage table, where its entry is the index-2 word — so the `JML $81:83C6`
    // three bytes above `$D33A` is unreachable from this entry point.
    PORT_COVER(d301_special);
    return enemy_freeze(w, dp, r);
  }

  // `SEC : SBC #$005C : ASL A : TAX`, then `SEC : LDA $0E : SBC $818561,X`.
  uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  uint16_t health = wram_r16(w, (uint32_t)dp + D301_DP_HEALTH);
  uint16_t left =
      (uint16_t)(health - rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index));
  r->x = index;

  if (left & 0x8000) {
    // `$81:D366  DEC $0C : STA $0E : STZ $7E : SEC : RTL`. The negative verdict,
    // and the `DEC` is the last instruction here to set a flag.
    PORT_COVER(d301_died);
    uint16_t verdict =
        (uint16_t)(wram_r16(w, (uint32_t)dp + D301_DP_VERDICT) - 1);
    wram_w16(w, (uint32_t)dp + D301_DP_VERDICT, verdict);
    wram_w16(w, (uint32_t)dp + D301_DP_HEALTH, left);
    // `enemy_die`'s store, and the same caveat for the sixth time: `$81:D26D
    // STZ $7E` is in this actor's own init, so the word is already zero and
    // deleting the line would change nothing the diff can see.
    wram_w16(w, (uint32_t)dp + ACTOR_DP_SCRATCH_7E, 0);
    r->a = left;
    r->n = (verdict & 0x8000) != 0;
    r->z = verdict == 0;
    r->c = true;
    return true;
  }

  if (left == health) {
    // `$81:D329  CMP $0E : BEQ $D360` — the same bare `CLC : RTL` the two ignore
    // paths use, so three of this routine's eight exits share one instruction
    // pair and differ only in the flags they arrive with.
    PORT_COVER(d301_no_damage);
    r->a = left;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  // `$81:D32D  STA $0E : JSL $81:8506`, and then — unlike every other copy — the
  // reaction *returns* here rather than being tail-called into. This page keeps
  // its display record at `$08` (`$81:D214  STA $08`), which is what lets the
  // shared routine work on it.
  PORT_COVER(d301_survived);
  wram_w16(w, (uint32_t)dp + D301_DP_HEALTH, left);
  if (!enemy_survived_react(w, dp, r)) return false;
  d301_survived_tail(w, dp, r);
  return true;
}
