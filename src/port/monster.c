// $81:C16B, $81:C00B, $81:BB75 and $81:BBA4 — see port/monster.h.

#include "port/monster.h"

#include "port/coverage.h"
#include "port/oam.h"  // ACTOR_FLAGS / ACTOR_X / ACTOR_Y / ACTOR_META

void monster_place_carried(Wram* w, const Rom* rom, uint16_t dp, uint16_t index,
                           uint16_t in_x, MonsterCarryRegs* out) {
  const uint16_t held = wram_r16(w, (uint16_t)(dp + MONSTER_DP_CARRIED));

  // `LDY $28 : CPY #$FFFF : BEQ`. The compare's own flags are what a caller
  // reads back here, and they are the same three every time it fires.
  if (held == MONSTER_CARRY_NONE) {
    PORT_COVER(monster_empty_handed);
    out->a = index;
    out->x = in_x;
    out->y = MONSTER_CARRY_NONE;
    out->n = false;
    out->z = true;
    out->c = true;
    return;
  }
  PORT_COVER(monster_carrying);

  const uint16_t ox = rom_word(rom, MONSTER_CARRY_TABLE + index);
  const uint16_t oy = rom_word(rom, MONSTER_CARRY_TABLE + index + 2);

  const uint16_t x = (uint16_t)(wram_r16(w, (uint16_t)(dp + MONSTER_DP_X)) + ox);
  wram_w16(w, (uint16_t)(held + ACTOR_X), x);

  const uint32_t sum = (uint32_t)wram_r16(w, (uint16_t)(dp + MONSTER_DP_Y)) + oy;
  const uint16_t y = (uint16_t)sum;
  wram_w16(w, (uint16_t)(held + ACTOR_Y), y);

  // `CLC : ADC` both times, so the two axes never chain; the flags that survive
  // to the `RTS` are the second one's.
  out->a = y;
  out->x = index;
  out->y = held;
  out->n = (y & 0x8000u) != 0;
  out->z = y == 0;
  out->c = sum > 0xffffu;
}

void monster_anim(Wram* w, const Rom* rom, uint16_t dp, uint16_t in_x,
                  MonsterAnimRegs* out) {
  // `DEC $1E : BPL`. The decrement happens whether or not the branch is taken,
  // so the store comes before the test rather than inside the arm.
  const uint16_t timer = (uint16_t)(wram_r16(w, (uint16_t)(dp + MONSTER_DP_TIMER)) - 1);
  wram_w16(w, (uint16_t)(dp + MONSTER_DP_TIMER), timer);

  if (!(timer & 0x8000u)) {
    // Still on this leg. `$2C` is whatever the last advancing frame left, which
    // is the whole of the one-frame lag the header describes.
    PORT_COVER(monster_anim_hold);
    monster_place_carried(w, rom, dp, wram_r16(w, (uint16_t)(dp + MONSTER_DP_FRAME_INDEX)),
                          in_x, out);
    return;
  }
  PORT_COVER(monster_anim_advance);
  wram_w16(w, (uint16_t)(dp + MONSTER_DP_TIMER), MONSTER_ANIM_PERIOD);

  const uint16_t phase =
      (uint16_t)((wram_r16(w, (uint16_t)(dp + MONSTER_DP_PHASE)) + 1) & MONSTER_PHASE_MASK);
  wram_w16(w, (uint16_t)(dp + MONSTER_DP_PHASE), phase);

  const uint16_t facing = wram_r16(w, (uint16_t)(dp + MONSTER_DP_FACING));
  const uint16_t index = (uint16_t)(facing * 2);
  wram_w16(w, (uint16_t)(dp + MONSTER_DP_FRAME_INDEX), index);
  // `ORA $1C : ASL`. An `ORA` because the two fields do not overlap; see the
  // header on why that is the same statement as "the facing is doubled".
  const uint16_t frame = (uint16_t)((index | phase) * 2);

  const uint16_t rec = wram_r16(w, (uint16_t)(dp + MONSTER_DP_RECORD));
  wram_w16(w, (uint16_t)(rec + ACTOR_META), rom_word(rom, MONSTER_FRAME_TABLE + frame));
  wram_w16(w, (uint16_t)(rec + ACTOR_META_BANK), MONSTER_META_BANK);

  const uint16_t flags = wram_r16(w, (uint16_t)(rec + ACTOR_FLAGS));
  if (frame >= MONSTER_FLIP_FROM) {
    // West-facing: draw the east-facing metasprite mirrored, and return without
    // moving whatever is being carried.
    PORT_COVER(monster_anim_mirror);
    const uint16_t set = (uint16_t)(flags | MONSTER_FLIP_X);
    wram_w16(w, (uint16_t)(rec + ACTOR_FLAGS), set);
    out->a = set;
    out->x = frame;
    out->y = rec;
    out->n = (set & 0x8000u) != 0;
    out->z = set == 0;
    out->c = true;  // the `CPX #$0030` the branch was taken on
    return;
  }
  PORT_COVER(monster_anim_plain);
  wram_w16(w, (uint16_t)(rec + ACTOR_FLAGS), (uint16_t)(flags & ~MONSTER_FLIP_X));

  monster_place_carried(w, rom, dp, index, frame, out);
}

// `$81:BBD6`, `monster_deliver`'s tail: clear the target, scan, and take
// anything inside `$B4` by installing the chase. `monster_seek` has the same
// four instructions with the `$26` test wedged into the middle of them, which is
// why it is written out again below rather than calling this.
//
// The scan's answer is the routine's answer too: A is the distance on the exit
// that declines and `$BEE3` on the one that chases, X is the winning record
// either way, and Y is the point's own Y, which `actor_nearest` never touches.
static void monster_take_target(Wram* w, uint16_t dp, MonsterSeekRegs* out) {
  wram_w16(w, (uint16_t)(dp + MONSTER_DP_TARGET), 0);

  const uint16_t x = wram_r16(w, (uint16_t)(dp + MONSTER_DP_X));
  const uint16_t y = wram_r16(w, (uint16_t)(dp + MONSTER_DP_Y));
  uint16_t dist = 0;
  const uint16_t found = actor_nearest(w, x, y, &dist);

  out->x = found;
  out->y = y;

  // `CMP #$00B4 : BCS`. Nothing matched comes back as `$FFFF`, which is far.
  if (dist >= MONSTER_SEEK_NEAR) {
    PORT_COVER(monster_nothing_near);
    out->a = dist;
    out->n = ((uint16_t)(dist - MONSTER_SEEK_NEAR) & 0x8000u) != 0;
    out->z = dist == MONSTER_SEEK_NEAR;
    out->c = true;
    return;
  }
  PORT_COVER(monster_gives_chase);

  wram_w16(w, (uint16_t)(dp + MONSTER_DP_TARGET), found);
  // The `JMP $BEDA` tail: install the next state body and return through its
  // `RTS`. A is the constant it loaded, which is why N is set here and nowhere
  // else in either routine.
  wram_w16(w, (uint16_t)(dp + MONSTER_DP_STATE), MONSTER_STATE_CHASE);
  out->a = MONSTER_STATE_CHASE;
  out->n = true;
  out->z = false;
  out->c = false;
}

void monster_seek(Wram* w, const Rom* rom, uint16_t dp, MonsterSeekRegs* out) {
  wram_w16(w, (uint16_t)(dp + MONSTER_DP_TARGET), 0);

  const uint16_t x = wram_r16(w, (uint16_t)(dp + MONSTER_DP_X));
  const uint16_t y = wram_r16(w, (uint16_t)(dp + MONSTER_DP_Y));
  uint16_t dist = 0;
  const uint16_t found = actor_nearest(w, x, y, &dist);

  // `CMP #$00D0 : BCS`. The far half is a different question of a different
  // population; see the header.
  if (dist >= MONSTER_SEEK_FAR) {
    PORT_COVER(monster_board_far);
    PlayerPickRegs p;
    player_in_range(w, rom_word(rom, MONSTER_SEEK_REACH_AT), x, y, &p);

    out->y = p.y;
    out->c = p.c;
    if (p.a != 0) {
      // `TAX : BNE`. Somebody is still about, so nothing happens at all.
      PORT_COVER(monster_player_about);
      out->a = p.a;
      out->x = p.a;
      out->n = (p.a & 0x8000u) != 0;
      out->z = false;
      return;
    }
    PORT_COVER(monster_gives_up);
    const uint16_t leave =
        (uint16_t)(wram_r16(w, (uint16_t)(dp + MONSTER_DP_LEAVE)) + 1);
    wram_w16(w, (uint16_t)(dp + MONSTER_DP_LEAVE), leave);
    out->a = 0;  // `player_in_range`'s own zero, and the `TAX` after it
    out->x = 0;
    out->n = (leave & 0x8000u) != 0;  // `INC $2A`
    out->z = leave == 0;
    return;
  }

  out->x = found;
  out->y = y;

  // `CMP #$00B4 : BCS`. The dead band: too far to chase, too near to leave.
  if (dist >= MONSTER_SEEK_NEAR) {
    PORT_COVER(monster_dead_band);
    out->a = dist;
    out->n = ((uint16_t)(dist - MONSTER_SEEK_NEAR) & 0x8000u) != 0;
    out->z = dist == MONSTER_SEEK_NEAR;
    out->c = true;
    return;
  }

  // `LDA $26 : BNE`. Already holding something, so it does not start again.
  const uint16_t kind = wram_r16(w, (uint16_t)(dp + MONSTER_DP_HELD_KIND));
  if (kind != 0) {
    PORT_COVER(monster_hands_full);
    out->a = kind;
    out->n = (kind & 0x8000u) != 0;
    out->z = false;
    out->c = false;  // the `CMP #$00B4` this path arrived on
    return;
  }
  PORT_COVER(monster_hands_free);

  wram_w16(w, (uint16_t)(dp + MONSTER_DP_TARGET), found);
  wram_w16(w, (uint16_t)(dp + MONSTER_DP_STATE), MONSTER_STATE_CHASE);
  out->a = MONSTER_STATE_CHASE;
  out->n = true;
  out->z = false;
  out->c = false;
}

// `LDA $0A : SEC : SBC $2E : BPL + : EOR #$FFFF : INC`. An absolute value taken
// off bit 15 rather than off the carry, so `$8000` away comes back as `$8000`
// and stays far — which the `CMP #$0010` then agrees with.
static uint16_t monster_axis_gap(uint16_t here, uint16_t home) {
  const uint16_t d = (uint16_t)(here - home);
  return (d & 0x8000u) ? (uint16_t)(0u - d) : d;
}

void monster_deliver(Wram* w, uint16_t dp, MonsterDeliverRegs* out) {
  // Three tests, all landing on the same instruction when they fail, and each
  // short-circuiting the reads of the next — `BCS $BBD6` twice and `BEQ $BBD6`.
  bool home = monster_axis_gap(wram_r16(w, (uint16_t)(dp + MONSTER_DP_X)),
                               wram_r16(w, (uint16_t)(dp + MONSTER_DP_HOME_X))) <
              MONSTER_LAIR_RADIUS;
  if (home)
    home = monster_axis_gap(wram_r16(w, (uint16_t)(dp + MONSTER_DP_Y)),
                            wram_r16(w, (uint16_t)(dp + MONSTER_DP_HOME_Y))) <
           MONSTER_LAIR_RADIUS;

  if (home && wram_r16(w, (uint16_t)(dp + MONSTER_DP_CARRIED)) != MONSTER_CARRY_NONE) {
    PORT_COVER(monster_drops_victim);
    // X and Y at the `JSL` are the caller's, untouched since entry, and every
    // register the free hands back is thrown away by the `LDA #$FFFF` under it.
    SlotFreeRegs freed;
    actor_slot_free(w, wram_r16(w, (uint16_t)(dp + MONSTER_DP_CARRIED)), dp, 0, 0,
                    &freed);
    wram_w16(w, (uint16_t)(dp + MONSTER_DP_CARRIED), MONSTER_CARRY_NONE);
    wram_w16(w, (uint16_t)(dp + MONSTER_DP_HELD_KIND), 0);
  } else {
    PORT_COVER(monster_still_travelling);
  }

  monster_take_target(w, dp, out);
}
