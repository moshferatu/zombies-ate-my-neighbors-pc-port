// $80:E450 and $80:A8B3 — see port/step.h.

#include "port/step.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/cpu.h"  // add16_overflows
#include "port/oam.h"  // ACTOR_X / ACTOR_Y, the record layout the tether reads
#include "port/terrain.h"  // ...and the two tests $81:9BF3 puts a step through

// --- $80:E450 ---------------------------------------------------------------

// One axis of the proposal. `base` is `$30` or `$32`, `delta` the table word,
// `twice` the mask-and-tick verdict shared by both axes. `c` may be NULL: the
// X axis computes a carry the routine then throws away.
//
// `CLC : ADC` both times — the ROM never chains the carry between the two adds,
// which is what makes a double step exactly two single ones.
static uint16_t step_axis(uint16_t base, uint16_t delta, bool twice, bool* c,
                          bool* ovf) {
  uint32_t sum = (uint32_t)delta + base;
  uint16_t v = (uint16_t)sum;
  if (ovf) *ovf = add16_overflows(delta, base);
  if (twice) {
    if (ovf) *ovf = add16_overflows(v, delta);
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
           step_axis(wram_r16(w, dp + STEP_DP_X), dx, twice != 0, NULL, NULL));
  uint16_t ny =
      step_axis(wram_r16(w, dp + STEP_DP_Y), dy, twice != 0, &cy, &out->v);
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
  out->v_set = true;

  if (ref == 0) {
    // Nobody to be tethered to, which is every frame of a one-player game.
    PORT_COVER(tether_alone);
    out->v_set = false;
    out->v = false;
    out->a = 0;  // the `LDA #$0000` that set the direct page, still in A
    out->y = 0;
    return;
  }

  out->y = ref;

  uint16_t wx = (uint16_t)((uint16_t)(x - wram_r16(w, (uint16_t)(ref + ACTOR_X))) +
                           TETHER_BIAS_X);
  if (wx < TETHER_SPAN_X) {
    const uint16_t dy = (uint16_t)(y - wram_r16(w, (uint16_t)(ref + ACTOR_Y)));
    uint16_t wy = (uint16_t)(dy + TETHER_BIAS_Y);
    out->v = add16_overflows(dy, TETHER_BIAS_Y);
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
  const uint16_t apart_y =
      abs_diff(w, wram_r16(w, (uint16_t)(a_rec + ACTOR_Y)), b_rec, ACTOR_Y);
  uint16_t apart = (uint16_t)(apart_x + apart_y);
  out->v = add16_overflows(apart_y, apart_x);  // `CLC : ADC $38`

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

// --- $80:AFFB ---------------------------------------------------------------

// `CMP #$0080`, and every exit that is not a `BIT` is one of these.
static void partner_cmp(PartnerRegs* out, uint16_t d) {
  uint16_t r = (uint16_t)(d - PARTNER_NEAR_SPAN);
  out->a = d;
  out->n = (r & 0x8000u) != 0;
  out->z = r == 0;
}

// `BIT $00D2` / `BIT $00D4`: Z from A AND memory, N from bit 15 of memory. The
// branch is `BEQ`, so an exit through here has Z set whatever else is true.
static bool partner_absent(PartnerRegs* out, uint16_t rec, uint16_t other) {
  if ((uint16_t)(rec & other) != 0) return false;
  out->n = (other & 0x8000u) != 0;
  out->z = true;
  return true;
}

void partner_near(Wram* w, uint16_t rec, uint16_t x, uint16_t y,
                  PartnerRegs* out) {
  const uint16_t a_rec = wram_r16(w, W_PLAYER_A_RECORD);
  const uint16_t b_rec = wram_r16(w, W_PLAYER_B_RECORD);

  // Nothing has been written yet on either of these paths, so A, X and Y are
  // all still the arguments.
  out->a = rec;
  out->x = x;
  out->y = y;
  if (partner_absent(out, rec, a_rec)) {
    PORT_COVER(partner_no_a);
    out->c = true;
    return;
  }
  if (partner_absent(out, rec, b_rec)) {
    PORT_COVER(partner_no_b);
    out->c = true;
    return;
  }

  wram_w16(w, PARTNER_DP_Y, y);

  // `CMP $00D2 : BEQ` — am I player A? Then the other one is B.
  const bool is_a = rec == a_rec;
  PORT_COVER_IF(is_a, partner_mover_a, partner_mover_b);
  const uint16_t ref = is_a ? b_rec : a_rec;
  out->y = ref;

  partner_cmp(out, abs_diff(w, x, ref, ACTOR_X));
  if (out->a >= PARTNER_NEAR_SPAN) {
    PORT_COVER(partner_far_x);
    out->c = false;
    return;
  }

  // `LDA $0038` — the Y argument comes back out of the scalar it was parked in.
  partner_cmp(out, abs_diff(w, wram_r16(w, PARTNER_DP_Y), ref, ACTOR_Y));
  if (out->a >= PARTNER_NEAR_SPAN) {
    PORT_COVER(partner_far_y);
    out->c = false;
    return;
  }
  PORT_COVER(partner_close);
  out->c = true;
}

// --- $80:F327 ---------------------------------------------------------------

void actor_publish_pos(Wram* w, uint16_t dp, uint16_t in_x, uint16_t in_y,
                       PublishRegs* out) {
  const uint16_t px = wram_r16(w, (uint16_t)(dp + STEP_DP_X));
  const uint16_t py = wram_r16(w, (uint16_t)(dp + STEP_DP_Y));
  const uint16_t rec = wram_r16(w, (uint16_t)(dp + PUBLISH_DP_RECORD));

  if (wram_r16(w, (uint16_t)(dp + PUBLISH_DP_TWO_PART)) == 0) {
    // `LDX $08 : STA $0002,X` — the record as an index, which is the same
    // address the other path dereferences.
    PORT_COVER(publish_one);
    wram_w16(w, (uint16_t)(rec + ACTOR_X), px);
    wram_w16(w, (uint16_t)(rec + ACTOR_Y), py);
    out->a = py;  // `LDA $32`, and the `STA` after it sets no flags
    out->x = rec;
    out->y = in_y;
    out->n = (py & 0x8000u) != 0;
    out->z = py == 0;
    return;
  }

  PORT_COVER(publish_two);
  const uint16_t up = wram_r16(w, (uint16_t)(dp + PUBLISH_DP_SECOND));
  wram_w16(w, (uint16_t)(rec + ACTOR_X), px);
  wram_w16(w, (uint16_t)(up + ACTOR_X), px);
  wram_w16(w, (uint16_t)(rec + ACTOR_Y), py);
  // `INC A` between the two stores: the upper half sits one pixel lower, which
  // is the seam between the two metasprites being closed by hand.
  wram_w16(w, (uint16_t)(up + ACTOR_Y), (uint16_t)(py + 1));

  // ...and one Z in front, read back out of the record rather than off the
  // thread, so it tracks whatever else moved the lower half this frame.
  uint16_t z = (uint16_t)(wram_r16(w, (uint16_t)(rec + ACTOR_Z)) - 1);
  wram_w16(w, (uint16_t)(up + ACTOR_Z), z);

  out->a = z;  // `DEC A`, and the store after it sets no flags either
  out->x = in_x;
  out->y = ACTOR_Z;  // `LDY #$0004`, still loaded
  out->n = (z & 0x8000u) != 0;
  out->z = z == 0;
}

// --- $81:8024 ---------------------------------------------------------------

// `SEC : SBC $0002,X : BPL +4 : EOR #$FFFF : INC A`, twice, and then the larger
// of the two — which is `max(|dx|, |dy|)`, the Chebyshev distance.
//
// The subtraction runs the other way round from `abs_diff`'s (`record - point`
// rather than `point - record`) and the magnitude is the same either way,
// including at `$8000`, where both formulations negate to `$8000` again.
static uint16_t chebyshev(const Wram* w, uint16_t x, uint16_t y, uint16_t rec) {
  uint16_t dx = abs_diff(w, x, rec, ACTOR_X);
  uint16_t dy = abs_diff(w, y, rec, ACTOR_Y);
  return dy >= dx ? dy : dx;
}

void nearest_player_dist(Wram* w, uint16_t dp, uint16_t in_y,
                         NearestRegs* out) {
  const uint16_t x = wram_r16(w, (uint16_t)(dp + NEAREST_PLAYER_DP_X));
  const uint16_t y = wram_r16(w, (uint16_t)(dp + NEAREST_PLAYER_DP_Y));

  // `LDA #$FFFF : STA $1E : STA $20`. Primed rather than skipped: an absent
  // player enters the comparison as the largest distance there is.
  wram_w16(w, (uint16_t)(dp + NEAREST_PLAYER_DP_A), NEAREST_PLAYER_NONE);
  wram_w16(w, (uint16_t)(dp + NEAREST_PLAYER_DP_B), NEAREST_PLAYER_NONE);

  const uint16_t a_rec = wram_r16(w, W_PLAYER_A_RECORD);
  const uint16_t b_rec = wram_r16(w, W_PLAYER_B_RECORD);

  // The intermediate `STA $1E` the ROM does before it knows which axis wins is
  // not reproduced separately: only the final value is observable, because
  // nothing between the two stores can read it.
  PORT_COVER_IF(a_rec != 0, nearest_has_a, nearest_no_a);
  if (a_rec != 0)
    wram_w16(w, (uint16_t)(dp + NEAREST_PLAYER_DP_A), chebyshev(w, x, y, a_rec));
  PORT_COVER_IF(b_rec != 0, nearest_has_b, nearest_no_b);
  if (b_rec != 0)
    wram_w16(w, (uint16_t)(dp + NEAREST_PLAYER_DP_B), chebyshev(w, x, y, b_rec));

  const uint16_t da = wram_r16(w, (uint16_t)(dp + NEAREST_PLAYER_DP_A));
  const uint16_t db = wram_r16(w, (uint16_t)(dp + NEAREST_PLAYER_DP_B));

  // `LDX $00D4` was the last thing to touch X, on both paths through the
  // second half — including the one where it was zero and the `BEQ` fired.
  out->x = b_rec;
  out->y = in_y;

  // `LDA $1E : CMP $20 : BCC out : LDA $20`.
  out->c = da >= db;
  if (!out->c) {
    // A is nearer, and the flags are the discarded subtraction's.
    PORT_COVER(nearest_a_wins);
    uint16_t r = (uint16_t)(da - db);
    out->a = da;
    out->n = (r & 0x8000u) != 0;
    out->z = r == 0;
    return;
  }
  // B is nearer, or they tie. `LDA $20` takes N and Z and leaves carry alone.
  PORT_COVER_IF(da == db, nearest_tie, nearest_b_wins);
  out->a = db;
  out->n = (db & 0x8000u) != 0;
  out->z = db == 0;
}

// --- $81:9BF3 ---------------------------------------------------------------

// One axis's three tests, in the ROM's order and with its short-circuit. `keep`
// is where the leftover X ends up, which is the routine's only awkward output;
// see the header.
static bool bearing_free(Wram* w, uint16_t self, uint16_t x, uint16_t y,
                         uint16_t* keep) {
  TerrainRegs t;
  terrain_blocked_enemy(w, x, y, &t);
  if (t.blocked) {
    PORT_COVER(bearing_terrain);
    *keep = t.x;
    return false;
  }

  BoundsRegs b;
  terrain_out_of_bounds(w, x, y, &b);
  if (b.c) {
    // `$80:B422` reads X and Y and writes neither, so the candidate the caller
    // loaded is still there.
    PORT_COVER(bearing_bounds);
    *keep = x;
    return false;
  }

  AtPointRegs p;
  actor_at_point(w, self, x, y, &p);
  *keep = p.x;
  PORT_COVER_IF(p.found, bearing_occupied, bearing_clear);
  return !p.found;
}

void actor_step_bearing(Wram* w, const Rom* rom, uint16_t dp, uint16_t dir2,
                        uint16_t in_y, StepBearingRegs* out) {
  // `ASL : TAX` happens before the rest-frame test, so the index survives it —
  // and so does the shift's carry, which is the only thing that writes carry on
  // the path that returns here.
  const uint16_t idx = (uint16_t)(dir2 * 2);
  const bool shifted_out = (dir2 & 0x8000u) != 0;

  const uint16_t rest = (uint16_t)(wram_r16(w, W_SCHED_TICK) & STEP_BEARING_REST_MASK);
  if (rest == 0) {
    PORT_COVER(bearing_rest);
    out->a = 0;
    out->x = idx;
    out->y = in_y;
    out->n = false;
    out->z = true;
    // Neither `LDA` nor `AND` touches carry, so what a caller reads here is the
    // `ASL` from four instructions earlier — and a direction is at most eight,
    // so it is a constant zero dressed up as a shift.
    out->c = shifted_out;
    return;
  }
  PORT_COVER(bearing_move);

  const uint16_t self = wram_r16(w, (uint16_t)(dp + BEARING_DP_RECORD));
  uint16_t px = wram_r16(w, (uint16_t)(dp + BEARING_DP_X));
  uint16_t py = wram_r16(w, (uint16_t)(dp + BEARING_DP_Y));

  const uint16_t try_x = (uint16_t)(px + rom_word(rom, STEP_BEARING_TABLE + idx));
  const uint16_t try_y = (uint16_t)(py + rom_word(rom, STEP_BEARING_TABLE + idx + 2));
  wram_w16(w, (uint16_t)(dp + BEARING_DP_TRY_X), try_x);
  wram_w16(w, (uint16_t)(dp + BEARING_DP_TRY_Y), try_y);

  uint16_t keep = 0;
  if (bearing_free(w, self, try_x, py, &keep)) {
    PORT_COVER(bearing_took_x);
    px = try_x;
    wram_w16(w, (uint16_t)(dp + BEARING_DP_X), px);
  }
  // ...and the second axis from wherever the first one left the actor.
  bool refused = !bearing_free(w, self, px, try_y, &keep);
  if (!refused) {
    PORT_COVER(bearing_took_y);
    py = try_y;
    wram_w16(w, (uint16_t)(dp + BEARING_DP_Y), py);
  }

  wram_w16(w, (uint16_t)(self + ACTOR_X), px);
  wram_w16(w, (uint16_t)(self + ACTOR_Y), py);

  // `LDY $08 : ... : LDA $0E : STA $0006,Y`.
  out->a = py;
  out->x = keep;
  out->y = self;
  out->n = (py & 0x8000u) != 0;
  out->z = py == 0;
  out->c = refused;
}
