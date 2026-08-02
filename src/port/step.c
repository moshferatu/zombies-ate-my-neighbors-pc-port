// $80:E450 and $80:A8B3 — see port/step.h.

#include "port/step.h"

#include "port/coverage.h"
#include "port/oam.h"  // ACTOR_X / ACTOR_Y, the record layout the tether reads

// --- $80:E450 ---------------------------------------------------------------

// One axis of the proposal. `base` is `$30` or `$32`, `delta` the table word,
// `twice` the mask-and-tick verdict shared by both axes. `c` may be NULL: the
// X axis computes a carry the routine then throws away.
//
// `CLC : ADC` both times — the ROM never chains the carry between the two adds,
// which is what makes a double step exactly two single ones.
static uint16_t step_axis(uint16_t base, uint16_t delta, bool twice, bool* c) {
  uint32_t sum = (uint32_t)delta + base;
  uint16_t v = (uint16_t)sum;
  if (twice) {
    sum = (uint32_t)v + delta;
    v = (uint16_t)sum;
  }
  if (c) *c = sum > 0xffffu;
  return v;
}

void step_propose(Wram* w, const Rom* rom, uint16_t dp, StepProposeRegs* out) {
  uint16_t dir = wram_r16(w, dp + STEP_DP_DIR);

  // `AND #$0002 : ASL A : ASL A : CLC : ADC $76` — the row, then the class.
  uint16_t index = (uint16_t)(((dir & STEP_DIR_CARDINAL) << 2) +
                              wram_r16(w, dp + STEP_DP_SPEED_CLASS));
  uint16_t mask = rom_word(rom, STEP_SPEED_MASKS + index);
  uint16_t twice = (uint16_t)(mask & wram_r16(w, W_SCHED_TICK));

  PORT_COVER_IF(dir == 0, speed_dir_still, speed_dir_moving);
  PORT_COVER_IF(twice != 0, speed_double, speed_single);
  // There is no site for "mask was $FFFF and it still came out single". It is
  // a real frame — `W_SCHED_TICK` is zero one frame in 65,536, and on that one
  // the fastest thing in the game takes a half-speed step — but it is not a
  // separate branch, only a separate reason for `speed_single`, and a site no
  // corpus can reach would dilute the one number that file exists to keep
  // honest. `port/step.h` records the quirk in prose instead.

  uint16_t dx = rom_word(rom, STEP_DELTA_X + dir);
  uint16_t dy = rom_word(rom, STEP_DELTA_Y + dir);

  bool cy;
  wram_w16(w, dp + STEP_DP_NEXT_X,
           step_axis(wram_r16(w, dp + STEP_DP_X), dx, twice != 0, NULL));
  uint16_t ny = step_axis(wram_r16(w, dp + STEP_DP_Y), dy, twice != 0, &cy);
  wram_w16(w, dp + STEP_DP_NEXT_Y, ny);

  out->a = ny;
  out->x = twice;
  out->y = dir;

  // The flags are not the store's. `CPX #$0000 : BEQ` sits *between* the first
  // add and the second, so a single step leaves the compare's flags — always
  // `Z` set, `C` set, `N` clear, whatever the position turned out to be — and
  // only a double step returns the arithmetic's.
  if (twice != 0) {
    out->n = (ny & 0x8000u) != 0;
    out->z = ny == 0;
    out->c = cy;
  } else {
    out->n = false;
    out->z = true;
    out->c = true;
  }
}

// --- $80:A8B3 ---------------------------------------------------------------

// `SEC : SBC $0002,Y : BPL +4 : EOR #$FFFF : INC A` — an absolute difference
// written out longhand, twice per point. `rec` may legitimately be zero on the
// far path when there is no second player, and then this reads `$7E:0002` and
// `$7E:0006` exactly as the ROM does.
static uint16_t abs_diff(const Wram* w, uint16_t v, uint16_t rec,
                         uint16_t field) {
  uint16_t d = (uint16_t)(v - wram_r16(w, (uint16_t)(rec + field)));
  return (d & 0x8000u) ? (uint16_t)(~d + 1u) : d;
}

// The Manhattan distance from `(x, y)` to a record, which is what both halves
// of the far path compute.
static uint16_t manhattan(const Wram* w, uint16_t x, uint16_t y, uint16_t rec) {
  uint16_t dx = abs_diff(w, x, rec, ACTOR_X);
  uint16_t dy = abs_diff(w, y, rec, ACTOR_Y);
  return (uint16_t)(dx + dy);
}

void step_tether_blocked(Wram* w, uint16_t x, uint16_t y, TetherRegs* out) {
  wram_w16(w, TETHER_DP_Y, y);
  wram_w16(w, TETHER_DP_X, x);

  // `LDY $08 : CPY $D6` — am I player A's thread? Then the reference is the
  // other player, and if I am not, the reference is player A.
  bool is_a = wram_r16(w, W_SCHED_CUR_TASK) == wram_r16(w, W_PLAYER_A_TASK);
  uint16_t ref = is_a ? wram_r16(w, W_PLAYER_B_RECORD)
                      : wram_r16(w, W_PLAYER_A_RECORD);
  PORT_COVER_IF(is_a, tether_mover_a, tether_mover_b);

  out->x = x;
  out->blocked = false;

  if (ref == 0) {
    // Nobody to be tethered to, which is every frame of a one-player game.
    PORT_COVER(tether_alone);
    out->a = 0;  // the `LDA #$0000` that set the direct page, still in A
    out->y = 0;
    return;
  }

  out->y = ref;

  uint16_t wx = (uint16_t)((uint16_t)(x - wram_r16(w, (uint16_t)(ref + ACTOR_X))) +
                           TETHER_BIAS_X);
  if (wx < TETHER_SPAN_X) {
    uint16_t wy =
        (uint16_t)((uint16_t)(y - wram_r16(w, (uint16_t)(ref + ACTOR_Y))) +
                   TETHER_BIAS_Y);
    if (wy < TETHER_SPAN_Y) {
      PORT_COVER(tether_inside);
      out->a = wy;  // the `CMP` left the biased offset in A
      return;
    }
    PORT_COVER(tether_outside_y);
  } else {
    PORT_COVER(tether_outside_x);
  }

  // Outside the window. `$34` and `$3A` become working space, in that order,
  // and both survive the routine.
  uint16_t want = manhattan(w, x, y, ref);
  wram_w16(w, TETHER_DP_X, want);

  // `LDX $D2 : LDY $D4` — the two players, never the mover and its reference,
  // so this half of the test is the same for both of them.
  uint16_t a_rec = wram_r16(w, W_PLAYER_A_RECORD);
  uint16_t b_rec = wram_r16(w, W_PLAYER_B_RECORD);
  uint16_t apart_x = abs_diff(w, wram_r16(w, (uint16_t)(a_rec + ACTOR_X)), b_rec,
                              ACTOR_X);
  wram_w16(w, TETHER_DP_Y, apart_x);
  uint16_t apart = (uint16_t)(apart_x +
                              abs_diff(w, wram_r16(w, (uint16_t)(a_rec + ACTOR_Y)),
                                       b_rec, ACTOR_Y));

  out->x = a_rec;
  out->y = b_rec;
  out->a = apart;

  // `CMP $3A : BEQ blocked : BCS allowed`. Strictly closer than the players
  // already are, or it does not happen — equal is refused.
  if (apart > want) {
    PORT_COVER(tether_closing);
    return;
  }
  PORT_COVER_IF(apart == want, tether_equal, tether_leashed);
  out->blocked = true;
}
