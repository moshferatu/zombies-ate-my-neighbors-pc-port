#include "port/collide.h"

#include "port/coverage.h"
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
// $80:F7F7  player_collide
// ---------------------------------------------------------------------------

bool player_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                    ActorHandlerRegs* r) {
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
    default:
      // Everything else is a routine nobody has ported. `$80:F92D` — the only
      // other entry any input has reached — is `LDA #$0009 : JSL apu_play_sfx`,
      // which writes no WRAM at all but does talk to the APU and spin on its
      // acknowledgement. That belongs with the audio path, not here, and
      // pretending the port had served it would be claiming a sound effect that
      // never played.
      PORT_COVER(player_unported);
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
    served = player_collide(w, rom, dp, arg, &r);
  } else if (entry == ENEMY_COLLIDE_ENTRY) {
    served = enemy_collide(w, rom, dp, arg, &r);
  } else {
    // The list of handlers the port has is exactly two. Anything else is a
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
