#include "port/collide.h"

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
#include "port/score.h"

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
static void player_collide_hurt(Wram* w, uint16_t dp, ActorHandlerRegs* r) {
  // `$80:F950  LDA $70 : CMP #$0002 : BEQ : CMP #$0004 : BEQ`. Two of the
  // player's states ignore collisions outright. Either comparison that matches
  // leaves zero behind, so both exits carry the same flags.
  uint16_t state = wram_r16(w, (uint32_t)dp + ACTOR_DP_STATE);
  r->a = state;
  if (state == 2 || state == 4) {
    PORT_COVER(hurt_state_immune);
    r->n = false;
    r->z = true;
    return;
  }

  // `$80:F95C  LDX $0E : LDA $1CBC,X : CMP #$0004 : BNE`. The absolute read is
  // unambiguous whatever the data bank holds — `sprite_build_oam` sets it to $80
  // with `PHK : PLB` and bank $80's low half is the WRAM mirror, so `$1CBC` is
  // `$7E:1CBC` either way.
  uint16_t player = wram_r16(w, (uint32_t)dp + ACTOR_DP_PLAYER);
  r->x = player;
  uint16_t weapon = wram_r16(w, W_PLAYER_WEAPON + player);
  r->a = weapon;
  if (weapon == PLAYER_WEAPON_IMMUNE) {
    // `$80:F966  LDA $1E : BNE`. Holding that one weapon with `$1E` set is the
    // second way out, and it is the one branch here no input has ever taken.
    uint16_t health = wram_r16(w, (uint32_t)dp + ACTOR_DP_HEALTH);
    r->a = health;
    if (health != 0) {
      PORT_COVER(hurt_weapon_immune);
      r->n = (health & 0x8000) != 0;
      r->z = false;
      return;
    }
  }

  // `$80:F96A  LDA $52 : BPL`. The hit-recovery timer: a hit only lands once it
  // has run past zero into the sign bit.
  uint16_t timer = wram_r16(w, (uint32_t)dp + ACTOR_DP_HURT_TIMER);
  r->a = timer;
  if (!(timer & 0x8000)) {
    PORT_COVER(hurt_iframes);
    r->n = false;
    r->z = timer == 0;
    return;
  }

  // The two stores that are the whole point of the routine: post the event for
  // the player's own code to pick up, and start the recovery timer again.
  PORT_COVER(hurt_taken);
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
// $80:F7F7  player_collide
// ---------------------------------------------------------------------------

bool player_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                    ActorHandlerRegs* r, uint32_t* unported) {
  // `$80:F7F7  CMP #$005C : BCS $F806`, and `$F806` is `CLC : RTL`. An id at or
  // above the player's own side belongs to the other half of the pair, so this
  // returns having read one word and written none.
  if (arg >= COLLIDE_ID_PLAYER) {
    PORT_COVER(player_ignore);
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
  switch (target) {
    case PLAYER_COLLIDE_NOP:
      // `$80:F87A` is a bare `RTS`. This id does nothing to the player, and the
      // registers are whatever the two instructions above left.
      PORT_COVER(player_no_effect);
      break;
    case PLAYER_COLLIDE_HURT:
      PORT_COVER(player_hurt_entry);
      player_collide_hurt(w, dp, r);
      break;
    case PLAYER_COLLIDE_SFX:
      PORT_COVER(player_sfx_only);
      player_sfx(w, dp, r);
      break;
    case PLAYER_COLLIDE_PICKUP:
      PORT_COVER(player_pickup_entry);
      player_pickup(w, rom, dp, index, r);
      break;
    default:
      // Everything else is a routine nobody has ported, and four of the 57
      // entries now are. Name the entry rather than the id when saying so:
      // `$80:F8D6` is one routine whatever the thirteen ids reaching it are.
      PORT_COVER(player_unported);
      if (unported) *unported = 0x800000u | target;
      return false;
  }

  r->c = false;  // the `CLC` at $80:F806, on both paths
  return true;
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

bool enemy_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                   ActorHandlerRegs* r) {
  if (arg < COLLIDE_ID_PLAYER) {
    // `$81:8888  CMP #$005C : BCS : CLC : RTL`. The comparison borrowed, so N is
    // set on every call that gets here and Z never is. Nothing is read and
    // nothing is written, which is the finding rather than an oversight: an
    // enemy told about a non-player collision touches nothing at all.
    PORT_COVER(enemy_ignore);
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
  // into routines of their own. Neither is ported and neither is reached by any
  // movie, so what they are is still an open question rather than an answer.
  if (id == ENEMY_HIT_SPECIAL_A || id == ENEMY_HIT_SPECIAL_B) {
    PORT_COVER(enemy_hit_special);
    return false;
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
    r->a = left;
    r->x = index;
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }

  // `$81:88B1  STA $1E : JML $81:8506`. The survivor's reaction — it walks the
  // thread's own parked stack to splice a call into it — is not ported, so the
  // port declines rather than write the health and stop half way.
  PORT_COVER(enemy_survived);
  return false;
}

// ---------------------------------------------------------------------------
// $81:FE0E  shot_collide
// ---------------------------------------------------------------------------

bool shot_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r) {
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
    // carry untouched: `r->c` is what the dispatcher handed in
  } else if (arg == SHOT_STOP_ID_B || arg == SHOT_STOP_ID_C ||
             arg == SHOT_STOP_ID_D) {
    PORT_COVER(shot_expire);
    // Whichever of the three matched, the `CMP` that matched set carry: an
    // equal comparison never borrows.
    r->c = true;
  } else {
    // `$81:FE20  RTL`, reached by falling through all three comparisons, so the
    // flags are the last one's — `arg - 1`. Carry is set because the only id
    // that could clear it is 0, and 0 left through the branch above.
    PORT_COVER(shot_pass);
    uint16_t diff = (uint16_t)(arg - SHOT_STOP_ID_D);
    r->a = arg;
    r->n = (diff & 0x8000) != 0;
    r->z = false;
    r->c = true;
    return true;
  }

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

bool thread_call_handler(Wram* w, const Rom* rom, uint16_t slot, uint16_t arg,
                         bool carry_in, ThreadCallResult* out) {
  // `$80:8480  LDA $1300,X : ORA $1330,X : BEQ $84B0`. A slot with no handler
  // registered makes the whole routine three instructions and no writes.
  uint16_t lo = wram_r16(w, W_THREAD_HANDLER + slot);
  uint16_t bank = wram_r16(w, W_THREAD_HANDLER_BANK + slot);
  if ((lo | bank) == 0) {
    PORT_COVER(handler_none);
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

  // `$80:84A3  TYA` then `RTL`: A is the argument, and X and Y are what the
  // dispatcher was called with, untouched.
  ActorHandlerRegs r = {.a = arg,
                        .x = slot,
                        .y = arg,
                        .n = (arg & 0x8000) != 0,
                        .z = arg == 0,
                        .c = carry_in};

  uint32_t entry = ((uint32_t)(bank & 0xff) << 16) | lo;
  bool served;
  if (entry == PLAYER_COLLIDE_ENTRY) {
    // The address a decline happened at is the harness's business, and it asks
    // `player_collide` directly through its own registry entry. Passing NULL
    // here is what keeps `guard_thread_call_handler` from censusing the door
    // rather than the room behind it.
    served = player_collide(w, rom, dp, arg, &r, NULL);
  } else if (entry == ENEMY_COLLIDE_ENTRY) {
    served = enemy_collide(w, rom, dp, arg, &r);
  } else if (entry == SHOT_COLLIDE_ENTRY) {
    served = shot_collide(w, dp, arg, &r);
  } else if (entry == VICTIM_COLLIDE_ENTRY) {
    served = victim_collide(w, dp, arg, &r);
  } else if (entry == OBJECT_COLLIDE_ENTRY) {
    served = object_collide(w, dp, arg, &r);
  } else {
    // The list of handlers the port has is exactly five. Anything else is a
    // routine that has not been written yet, and saying so by address is what
    // makes the remaining work countable instead of vague.
    PORT_COVER(handler_unported);
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
    // `$80:84A8  LDA #$8000 : STA $1180,X`, and that `LDA` is the last thing to
    // touch A — so the parked value, not the handler's, is what the caller gets
    // back. Nothing but a dying enemy comes through here, which is why this was
    // invisible until `enemy_collide` grew its death branch.
    wram_w16(w, W_THREAD_WAIT + slot, 0x8000);
    out->a = 0x8000;
  }
  return true;
}
