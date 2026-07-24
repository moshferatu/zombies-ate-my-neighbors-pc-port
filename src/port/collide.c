#include "port/collide.h"

#include "port/coverage.h"

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

bool enemy_collide(Wram* w, uint16_t dp, uint16_t arg, ActorHandlerRegs* r) {
  // Neither is read on the branch this serves, and that is the finding rather
  // than an oversight: an enemy told about a non-player collision returns having
  // touched nothing at all. They stay in the signature because the branch below
  // them reads `ACTOR_DP_HEALTH` on that same page.
  (void)w;
  (void)dp;

  if (arg >= COLLIDE_ID_PLAYER) {
    // `$81:888F` onwards: park the id, mask it, and either jump into one of two
    // routines for the two special ids or index the damage table at `$81:8561`
    // and subtract from health. Every one of those leaves through code nobody
    // has ported, so the port stops at the door.
    PORT_COVER(enemy_act);
    return false;
  }

  // `$81:8888  CMP #$005C : BCS : CLC : RTL`. The comparison borrowed, so N is
  // set on every call that gets here and Z never is.
  PORT_COVER(enemy_ignore);
  uint16_t diff = (uint16_t)(arg - COLLIDE_ID_PLAYER);
  r->a = arg;
  r->n = (diff & 0x8000) != 0;
  r->z = false;
  r->c = false;
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
    served = player_collide(w, rom, dp, arg, &r);
  } else if (entry == ENEMY_COLLIDE_ENTRY) {
    served = enemy_collide(w, dp, arg, &r);
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
  // thread put to sleep indefinitely. Neither of the two above ever does on the
  // branches this port serves, which is why `handler_park` reads zero: the two
  // paths that would set it are `$81:88BF` and the death routine behind it.
  if (r.c) {
    PORT_COVER(handler_park);
    wram_w16(w, W_THREAD_WAIT + slot, 0x8000);
  }
  return true;
}
