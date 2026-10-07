// The werewolf's state bodies -- see port/werewolf.h.

#include "port/werewolf.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/rng.h"

typedef struct {
  uint16_t x, y;
} Point;

// A target exactly this far away and it leaves. Nothing else makes it.
#define WEREWOLF_LEAVES_AT 0x0168

// It steps twice on a draw without this bit.
#define WEREWOLF_STEP_ONCE_BIT 0x0002

// A pounce is made on a draw under the first, at a target nearer than the
// second and no nearer than the third by the sum of its two gaps.
#define WEREWOLF_POUNCE_ODDS 0x0f
#define WEREWOLF_NOTICE_WITHIN 0x0145
#define WEREWOLF_POUNCE_NO_NEARER 0x0046
// Whoever it pounces at with this id it comes down nearer to. `port/oam.h`
// has `$01` and `$02` as the neighbours'.
#define WEREWOLF_NEAR_ID 0x0001
// A hop: this and up to 31 more, across and down.
#define WEREWOLF_HOP_MASK 0x001f
#define WEREWOLF_HOP_ACROSS 0x0014
#define WEREWOLF_HOP_DOWN 0x000f
#define WEREWOLF_HOP_ACROSS_BACK 0x0001  // bits of the first draw
#define WEREWOLF_HOP_DOWN_BACK 0x0002
// A spot exactly this far across from it is never taken.
#define WEREWOLF_SPOT_NEVER_ACROSS 0x0013

// A strike is made at a target this near its row, and this near across.
#define WEREWOLF_STRIKE_ROW_WITHIN 0x0004
#define WEREWOLF_STRIKE_ACROSS_WITHIN 0x001d
// The ways a strike is given, which its tables are laid out for: they are not
// the ways those numbers are when it runs.
#define WEREWOLF_STRIKE_WAY_RIGHT 7
#define WEREWOLF_STRIKE_WAY_LEFT 3

// Its running pictures: four in the cycle, of four passes each.
#define WEREWOLF_RUN_PICTURES 4
#define WEREWOLF_RUN_PICTURE_PASSES 3
#define WEREWOLF_PICTURE_BANK 0x0090
// A strike's: three passes each, and the blow is drawn from this one on.
#define WEREWOLF_STRIKE_PICTURE_PASSES 2
#define WEREWOLF_BLOW_FROM 0x000c
#define WEREWOLF_BLOW_ID 0x0003

// It lands beside whoever it pounced at if they are within this of where it
// meant to come down.
#define WEREWOLF_LAND_BESIDE_WITHIN 0x0009

// Tables in `WEREWOLF_BANK`.
#define WEREWOLF_STEPS 0xa7c2u  // by way, in fours: a step across and down
// By way and place in the cycle: a word for its record's flags, and a picture.
#define WEREWOLF_RUN_PICTURE_AT 0xa6c1u
#define WEREWOLF_STRIKE_PICTURE_AT 0xab0au
#define WEREWOLF_STRIKE_FLAGS 0xab20u     // by way, doubled
#define WEREWOLF_BLOW_ASIDE 0xab6bu       // by way, doubled: the blow, from it
#define WEREWOLF_LAND_BESIDE 0xaa49u      // by way, in fours
#define WEREWOLF_POUNCE_PAST 0xa97cu      // by way, doubled: past its quarry
#define WEREWOLF_POUNCE_FURTHER 0xa96au   // ...and further, past most

// One werewolf's pass.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  WerewolfLog* log;  // never NULL here
  PortFlags flags;
} Werewolf;

static uint16_t field(const Werewolf* p, uint16_t at) {
  return wram_r16(p->w, (uint16_t)(p->page + at));
}

static void set_field(Werewolf* p, uint16_t at, uint16_t v) {
  wram_w16(p->w, (uint16_t)(p->page + at), v);
}

static uint16_t record_field(const Werewolf* p, uint16_t at) {
  return wram_r16(p->w, (uint16_t)(p->record + at));
}

static void set_record_field(Werewolf* p, uint16_t at, uint16_t v) {
  wram_w16(p->w, (uint16_t)(p->record + at), v);
}

static uint16_t table_word(const Werewolf* p, uint16_t table, uint16_t index) {
  return rom_word(p->rom, ((uint32_t)WEREWOLF_BANK << 16) +
                              (uint16_t)(table + index));
}

static Point position(const Werewolf* p) {
  return (Point){field(p, WEREWOLF_DP_X), field(p, WEREWOLF_DP_Y)};
}

static Point place_of(const Werewolf* p, uint16_t record) {
  return (Point){wram_r16(p->w, (uint16_t)(record + ACTOR_X)),
                 wram_r16(p->w, (uint16_t)(record + ACTOR_Y))};
}

static void set_state(Werewolf* p, uint16_t body) {
  set_field(p, WEREWOLF_DP_STATE, body);
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

static uint16_t magnitude(uint16_t v) {
  return negative(v) ? (uint16_t)(0u - v) : v;
}

// A record the thread's data bank can reach, which is all of them.
static bool is_record(uint16_t record) { return record < 0x1f00u; }

// A table's word for its record's flags: bits to set, or with the top one
// set, bits to keep.
static bool apply_to_flags(Werewolf* p, uint16_t word) {
  const uint16_t flags = record_field(p, ACTOR_FLAGS);
  set_record_field(p, ACTOR_FLAGS,
                   negative(word) ? (uint16_t)(flags & word)
                                  : (uint16_t)(flags | word));
  return negative(word);
}

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

// Which way `target` is, 1 to 8, or 0 on the same spot. Its record is first
// put exactly on the target's row or column if it is within a pixel of it.
//
// Overflow is clear when the two were apart, from the sums that made the
// answer. On the same spot it is still the snap's last difference's.
static uint16_t bearing_to(Werewolf* p, uint16_t target) {
  WerewolfLog* log = p->log;
  ActorSnapRegs snap_scratch;
  ActorBearingRegs bearing_scratch;
  const bool kept = log->bearings < WEREWOLF_MAX_STEPS;
  ActorSnapRegs* snap = kept ? &log->snap[log->bearings] : &snap_scratch;
  ActorBearingRegs* r = kept ? &log->bearing[log->bearings] : &bearing_scratch;
  log->bearings++;

  flags_sub(&p->flags, record_field(p, ACTOR_Y), place_of(p, target).y);
  actor_snap_to(p->w, p->record, target, snap);
  actor_bearing(p->w, p->rom, p->record, target, r);
  flags_carry(&p->flags, r->c);
  if (r->a != 0) flags_overflow(&p->flags, false);
  return r->a;
}

// A draw begins from the carry before it.
static uint16_t random_byte(Werewolf* p) {
  RngResult r;
  rng_next(p->w, p->flags.c, &r);
  flags_carry(&p->flags, r.c);
  flags_overflow(&p->flags, r.v);
  if (p->log->draws < WEREWOLF_MAX_DRAWS)
    p->log->draw_overflow[p->log->draws] = r.v;
  p->log->draws++;
  return r.a;
}

// May it stand at `at`? The ground and whoever is there each have a say, in
// that order. Carry is the answer and overflow the last test's that wrote
// one.
static bool can_stand_at(Werewolf* p, Point at, WerewolfProbe* probe) {
  WerewolfLog* log = p->log;
  TerrainRegs scratch;
  TerrainRegs* ground = log->grounds < WEREWOLF_MAX_GROUNDS
                            ? &log->ground[log->grounds]
                            : &scratch;
  log->grounds++;
  terrain_blocked_enemy(p->w, at.x, at.y, ground);
  flags_carry(&p->flags, ground->blocked);
  flags_overflow(&p->flags, ground->v);
  probe->blocked = ground->blocked;
  if (ground->blocked) return false;

  AtPointRegs r;
  AtPointWork work;
  actor_at_point_counted(p->w, p->record, at.x, at.y, &r, &work);
  for (int i = 0; i < AT_POINT_BLOCK_COUNT; i++)
    log->at_point.blocks[i] += work.blocks[i];
  probe->someone_asked = true;
  probe->someone = r.found;
  flags_carry(&p->flags, r.found);
  if (r.v_set) flags_overflow(&p->flags, r.v);
  return !r.found;
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

// `$81:A74D`: a pixel towards its target, each axis by itself. The step is
// measured from its record, which finding the way may just have moved.
static void run_step(Werewolf* p, WerewolfStep* log) {
  if (flags_same(&p->flags, field(p, WEREWOLF_DP_TARGET_DIST),
                 WEREWOLF_LEAVES_AT)) {
    PORT_COVER(werewolf_left);
    log->left = true;
    set_field(p, WEREWOLF_DP_FATE, (uint16_t)(field(p, WEREWOLF_DP_FATE) - 1));
    set_field(p, WEREWOLF_DP_KILLED, 0);
    return;
  }
  const uint16_t way = bearing_to(p, field(p, WEREWOLF_DP_TARGET));
  set_field(p, WEREWOLF_DP_WAY, way);
  if (way == 0) {
    PORT_COVER(werewolf_on_them);
    log->on_them = true;
    return;
  }
  const uint16_t at = (uint16_t)(way << 2);
  Point to;
  to.x = flags_add(&p->flags, record_field(p, ACTOR_X),
                   table_word(p, WEREWOLF_STEPS, at));
  set_field(p, WEREWOLF_DP_TRY_X, to.x);
  to.y = flags_add(&p->flags, record_field(p, ACTOR_Y),
                   table_word(p, WEREWOLF_STEPS + 2, at));
  set_field(p, WEREWOLF_DP_TRY_Y, to.y);

  Point me = position(p);
  if (can_stand_at(p, (Point){to.x, me.y}, &log->axis[0])) {
    me.x = to.x;
    set_field(p, WEREWOLF_DP_X, me.x);
  }
  if (can_stand_at(p, (Point){me.x, to.y}, &log->axis[1])) {
    me.y = to.y;
    set_field(p, WEREWOLF_DP_Y, me.y);
  }
  set_record_field(p, ACTOR_X, me.x);
  set_record_field(p, ACTOR_Y, me.y);
}

// `$81:A694`: the picture for the way it faces and its place in the cycle.
static void show_running(Werewolf* p) {
  WerewolfLog* log = p->log;
  const uint16_t way = field(p, WEREWOLF_DP_WAY);
  if (way == 0) {
    log->no_way = true;
    return;
  }
  const uint16_t at = (uint16_t)(
      ((((way - 1) << 2) | field(p, WEREWOLF_DP_PICTURE)) << 2));
  flags_carry(&p->flags, false);  // the shifts
  set_record_field(p, ACTOR_META,
                   table_word(p, WEREWOLF_RUN_PICTURE_AT + 2, at));
  set_record_field(p, ACTOR_META_BANK, WEREWOLF_PICTURE_BANK);
  log->mask_clears =
      apply_to_flags(p, table_word(p, WEREWOLF_RUN_PICTURE_AT, at));
}

// `$81:A7E6`: may it come down on the spot it chose? If so the flight is
// measured out: so many passes, by the longer of the two gaps.
static bool spot_is_good(Werewolf* p) {
  WerewolfLog* log = p->log;
  const Point spot = {field(p, WEREWOLF_DP_LAND_X),
                      field(p, WEREWOLF_DP_LAND_Y)};
  const Point me = position(p);
  log->spot_same_row = record_field(p, ACTOR_Y) == spot.y;
  log->spot_same_column = record_field(p, ACTOR_X) == spot.x;
  ActorBearingRegs way;
  actor_bearing_point(p->w, p->rom, p->record, spot.x, spot.y, &way);
  set_field(p, WEREWOLF_DP_SPOT_WAY, way.a);

  const uint16_t across = flags_sub(&p->flags, spot.x, me.x);
  log->spot_negative[0] = negative(across);
  set_field(p, WEREWOLF_DP_SIGN_X, negative(across) ? 0xffffu : 1);
  set_field(p, WEREWOLF_DP_GAP_X, magnitude(across));
  if (flags_same(&p->flags, magnitude(across), WEREWOLF_SPOT_NEVER_ACROSS)) {
    PORT_COVER(werewolf_spot_nineteen);
    log->spot = WEREWOLF_SPOT_NINETEEN;
    flags_carry(&p->flags, false);
    return false;
  }
  const uint16_t down = flags_sub(&p->flags, spot.y, me.y);
  log->spot_negative[1] = negative(down);
  set_field(p, WEREWOLF_DP_SIGN_Y, negative(down) ? 0xffffu : 1);
  set_field(p, WEREWOLF_DP_GAP_Y, magnitude(down));
  log->spot_down_longer =
      flags_at_least(&p->flags, magnitude(down), magnitude(across));
  const uint16_t longer =
      log->spot_down_longer ? magnitude(down) : magnitude(across);
  set_field(p, WEREWOLF_DP_SPAN, (uint16_t)(longer >> 2));
  set_field(p, WEREWOLF_DP_RISE, (uint16_t)(longer >> 3));

  terrain_footprint_bit12(p->w, spot.x, spot.y, &log->spot_ground);
  flags_overflow(&p->flags, log->spot_ground.v);
  flags_carry(&p->flags, false);  // each way out but the last is a `CLC`
  if (log->spot_ground.blocked) {
    PORT_COVER(werewolf_spot_no_ground);
    log->spot = WEREWOLF_SPOT_NO_GROUND;
    return false;
  }
  AtPointRegs someone;
  AtPointWork work;
  actor_at_point_counted(p->w, p->record, spot.x, spot.y, &someone, &work);
  for (int i = 0; i < AT_POINT_BLOCK_COUNT; i++)
    log->at_point.blocks[i] += work.blocks[i];
  if (someone.v_set) flags_overflow(&p->flags, someone.v);
  if (someone.found) {
    PORT_COVER(werewolf_spot_taken);
    log->spot = WEREWOLF_SPOT_TAKEN;
    return false;
  }
  BoundsRegs edge;
  terrain_out_of_bounds(p->w, spot.x, spot.y, &edge);
  log->spot_edge = edge.exit;
  if (edge.c) {
    PORT_COVER(werewolf_spot_off_level);
    log->spot = WEREWOLF_SPOT_OFF_LEVEL;
    return false;
  }
  PORT_COVER(werewolf_crouched);
  log->spot = WEREWOLF_SPOT_GOOD;
  set_field(p, WEREWOLF_DP_PART_X, 0);
  set_field(p, WEREWOLF_DP_PART_Y, 0);
  set_field(p, WEREWOLF_DP_WAY, way.a);
  set_state(p, WEREWOLF_STATE_CROUCH);
  flags_carry(&p->flags, true);
  return true;
}

// `$81:A913`: hurt, it hops to somewhere near, at nobody.
static void hop(Werewolf* p) {
  WerewolfLog* log = p->log;
  const uint16_t coin = random_byte(p);
  set_field(p, WEREWOLF_DP_GAP_ACROSS, coin);
  uint16_t across = flags_add(&p->flags, random_byte(p) & WEREWOLF_HOP_MASK,
                              WEREWOLF_HOP_ACROSS);
  uint16_t down = flags_add(&p->flags, random_byte(p) & WEREWOLF_HOP_MASK,
                            WEREWOLF_HOP_DOWN);
  log->hop_flipped[0] = (coin & WEREWOLF_HOP_ACROSS_BACK) != 0;
  log->hop_flipped[1] = (coin & WEREWOLF_HOP_DOWN_BACK) != 0;
  if (log->hop_flipped[0]) across = (uint16_t)(0u - across);
  if (log->hop_flipped[1]) down = (uint16_t)(0u - down);
  const Point me = position(p);
  set_field(p, WEREWOLF_DP_LAND_X, flags_add(&p->flags, me.x, across));
  set_field(p, WEREWOLF_DP_LAND_Y, flags_add(&p->flags, me.y, down));
  set_field(p, WEREWOLF_DP_HURT_SEEN, field(p, WEREWOLF_DP_HURT));
  set_field(p, WEREWOLF_DP_QUARRY, 0xffffu);
}

// `$81:A8A7`: pounce, on a draw, at a target far enough and near enough.
static void maybe_pounce(Werewolf* p) {
  WerewolfLog* log = p->log;
  if (!flags_same(&p->flags, field(p, WEREWOLF_DP_HURT_SEEN),
                  field(p, WEREWOLF_DP_HURT))) {
    PORT_COVER(werewolf_hopped_hurt);
    log->pounce = WEREWOLF_POUNCE_HURT;
    hop(p);
    spot_is_good(p);
    return;
  }
  if (flags_at_least(&p->flags, random_byte(p), WEREWOLF_POUNCE_ODDS)) {
    log->pounce = WEREWOLF_POUNCE_NO_DRAW;
    return;
  }
  if (flags_at_least(&p->flags, field(p, WEREWOLF_DP_TARGET_DIST),
                     WEREWOLF_NOTICE_WITHIN)) {
    PORT_COVER(werewolf_pounce_too_far);
    log->pounce = WEREWOLF_POUNCE_TOO_FAR;
    return;
  }
  const uint16_t target = field(p, WEREWOLF_DP_TARGET);
  const Point me = position(p);
  const Point them = place_of(p, target);
  set_field(p, WEREWOLF_DP_QUARRY, target);
  set_field(p, WEREWOLF_DP_LAND_X, them.x);
  set_field(p, WEREWOLF_DP_LAND_Y, them.y);

  const uint16_t across = flags_sub(&p->flags, them.x, me.x);
  log->gap_negative[0] = negative(across);
  set_field(p, WEREWOLF_DP_GAP_ACROSS, magnitude(across));
  const uint16_t down = flags_sub(&p->flags, them.y, me.y);
  log->gap_negative[1] = negative(down);
  const uint16_t apart =
      flags_add(&p->flags, magnitude(down), magnitude(across));
  if (!flags_at_least(&p->flags, apart, WEREWOLF_POUNCE_NO_NEARER)) {
    PORT_COVER(werewolf_pounce_too_near);
    log->pounce = WEREWOLF_POUNCE_TOO_NEAR;
    return;
  }
  PORT_COVER(werewolf_pounced);
  log->pounce = WEREWOLF_POUNCE_CHOOSES;
  const uint16_t way = (uint16_t)(field(p, WEREWOLF_DP_WAY) << 1);
  uint16_t x =
      flags_add(&p->flags, them.x, table_word(p, WEREWOLF_POUNCE_PAST, way));
  set_field(p, WEREWOLF_DP_LAND_X, x);
  log->quarry_near =
      flags_same(&p->flags,
                 wram_r16(p->w, (uint16_t)(target + ACTOR_COLLIDE_ID)),
                 WEREWOLF_NEAR_ID);
  if (!log->quarry_near) {
    x = flags_add(&p->flags, x, table_word(p, WEREWOLF_POUNCE_FURTHER, way));
    set_field(p, WEREWOLF_DP_LAND_X, x);
  }
  spot_is_good(p);
}

// `$81:AB7D`: is its target beside it, to strike? It then faces them.
static bool within_reach(Werewolf* p) {
  WerewolfLog* log = p->log;
  if (flags_at_least(&p->flags, field(p, WEREWOLF_DP_TARGET_DIST),
                     WEREWOLF_NOTICE_WITHIN)) {
    log->reach = WEREWOLF_REACH_TOO_FAR;
    flags_carry(&p->flags, false);
    return false;
  }
  const Point me = position(p);
  const Point them = place_of(p, field(p, WEREWOLF_DP_TARGET));
  const uint16_t down = flags_sub(&p->flags, them.y, me.y);
  log->reach_negative[0] = negative(down);
  if (flags_at_least(&p->flags, magnitude(down), WEREWOLF_STRIKE_ROW_WITHIN)) {
    log->reach = WEREWOLF_REACH_OFF_ROW;
    flags_carry(&p->flags, false);
    return false;
  }
  const uint16_t across = flags_sub(&p->flags, them.x, me.x);
  log->reach_negative[1] = negative(across);
  if (flags_at_least(&p->flags, magnitude(across),
                     WEREWOLF_STRIKE_ACROSS_WITHIN)) {
    log->reach = WEREWOLF_REACH_OFF_SIDE;
    flags_carry(&p->flags, false);
    return false;
  }
  log->reach = WEREWOLF_REACH_WITHIN;
  set_field(p, WEREWOLF_DP_WAY, negative(across) ? WEREWOLF_STRIKE_WAY_LEFT
                                                : WEREWOLF_STRIKE_WAY_RIGHT);
  flags_carry(&p->flags, true);
  return true;
}

// `$81:AAAE`: begin a strike. The blow is a record of its own, to one side
// of it, which is not drawn yet.
static void begin_strike(Werewolf* p) {
  WerewolfLog* log = p->log;
  set_state(p, WEREWOLF_STATE_STRIKE);

  SlotAllocRegs r;
  actor_slot_alloc(p->w, WEREWOLF_BANK, &r);
  if (r.c) {  // none free, and the ROM goes on with what is no record
    log->declined = true;
    return;
  }
  PORT_COVER(werewolf_struck);
  const uint16_t blow = r.a;
  log->blow = blow;
  const uint16_t way = (uint16_t)(field(p, WEREWOLF_DP_WAY) << 1);
  wram_w16(p->w, (uint16_t)(blow + ACTOR_X),
           flags_add(&p->flags, table_word(p, WEREWOLF_BLOW_ASIDE, way),
                     record_field(p, ACTOR_X)));
  wram_w16(p->w, (uint16_t)(blow + ACTOR_Z), 0);
  wram_w16(p->w, (uint16_t)(blow + ACTOR_Y), record_field(p, ACTOR_Y));
  wram_w16(p->w, (uint16_t)(blow + ACTOR_COLLIDE_ID), WEREWOLF_BLOW_ID);
  wram_w16(p->w, (uint16_t)(blow + ACTOR_THREAD),
           wram_r16(p->w, W_SCHED_CUR_TASK));
  wram_w16(p->w, (uint16_t)(blow + ACTOR_META), 0);
  wram_w16(p->w, (uint16_t)(blow + ACTOR_META_BANK), 0);

  set_field(p, WEREWOLF_DP_BLOW, blow);
  set_field(p, WEREWOLF_DP_PICTURE_WAIT, 0);
  set_field(p, WEREWOLF_DP_PICTURE, 0);
  flags_carry(&p->flags, false);  // the way, doubled
  log->blow_mask_clears =
      apply_to_flags(p, table_word(p, WEREWOLF_STRIKE_FLAGS, way));
}

// `$81:ABC6`: at whoever is nearest, a step or two, and then a pounce or a
// strike if they are placed for one.
static void run(Werewolf* p) {
  WerewolfLog* log = p->log;
  const Point me = position(p);
  ActorNearestWork work;
  uint16_t dist;
  const uint16_t target = actor_nearest_counted(p->w, me.x, me.y, &dist, &work);
  log->nearest = work;
  flags_carry(&p->flags, false);
  flags_overflow(&p->flags, false);
  set_field(p, WEREWOLF_DP_TARGET_DIST, dist);
  set_field(p, WEREWOLF_DP_TARGET, target);
  if (!is_record(target)) {  // nobody, and what it is handed is no record
    log->declined = true;
    return;
  }

  log->steps = (random_byte(p) & WEREWOLF_STEP_ONCE_BIT) != 0 ? 1 : 2;
  for (int i = 0; i < log->steps; i++) run_step(p, &log->step[i]);

  const uint16_t wait = (uint16_t)(field(p, WEREWOLF_DP_PICTURE_WAIT) - 1);
  set_field(p, WEREWOLF_DP_PICTURE_WAIT, wait);
  if (negative(wait)) {
    log->new_picture = true;
    set_field(p, WEREWOLF_DP_PICTURE_WAIT, WEREWOLF_RUN_PICTURE_PASSES);
    set_field(p, WEREWOLF_DP_PICTURE,
              (uint16_t)((field(p, WEREWOLF_DP_PICTURE) + 1) &
                         (WEREWOLF_RUN_PICTURES - 1)));
    show_running(p);
  }
  maybe_pounce(p);
  if (log->declined) return;
  if (within_reach(p)) begin_strike(p);
}

// ---------------------------------------------------------------------------
// A strike
// ---------------------------------------------------------------------------

// `$81:AACF`: its next picture when this one has had its passes. The pass
// they run out on gives the blow's record back, and is the ROM's.
static void strike(Werewolf* p) {
  WerewolfLog* log = p->log;
  const uint16_t wait = (uint16_t)(field(p, WEREWOLF_DP_PICTURE_WAIT) - 1);
  set_field(p, WEREWOLF_DP_PICTURE_WAIT, wait);
  if (!negative(wait)) {
    log->strike = WEREWOLF_STRIKE_WAITED;
    return;
  }
  set_field(p, WEREWOLF_DP_PICTURE_WAIT, WEREWOLF_STRIKE_PICTURE_PASSES);
  const uint16_t at = (uint16_t)(field(p, WEREWOLF_DP_PICTURE) + 2);
  set_field(p, WEREWOLF_DP_PICTURE, at);
  const uint16_t picture = table_word(p, WEREWOLF_STRIKE_PICTURE_AT, at);
  if (picture == 0) {
    PORT_COVER(werewolf_strike_over);
    log->strike = WEREWOLF_STRIKE_OVER;
    log->declined = true;
    return;
  }
  set_record_field(p, ACTOR_META, picture);
  log->strike = WEREWOLF_STRIKE_SHOWN;
  if (!flags_at_least(&p->flags, at, WEREWOLF_BLOW_FROM)) return;

  const uint16_t blow = field(p, WEREWOLF_DP_BLOW);
  if (!is_record(blow)) {
    log->declined = true;
    return;
  }
  PORT_COVER(werewolf_blow_shown);
  log->strike = WEREWOLF_STRIKE_BLOW_SHOWN;
  wram_w16(p->w, (uint16_t)(blow + ACTOR_FLAGS),
           wram_r16(p->w, (uint16_t)(blow + ACTOR_FLAGS)) | ACTOR_DRAW);
}

// ---------------------------------------------------------------------------
// A pounce
// ---------------------------------------------------------------------------

// One axis of a pounce: so many pixels this pass, by how its gap compares
// with the longer one.
static uint16_t glide(Werewolf* p, uint16_t place_at, uint16_t part_at,
                      uint16_t gap_at, uint16_t sign_at, int* steps) {
  const uint16_t span = field(p, WEREWOLF_DP_SPAN);
  uint16_t place = field(p, place_at);
  uint16_t part = flags_add(&p->flags, field(p, part_at), field(p, gap_at));
  while (flags_at_least(&p->flags, part, span)) {
    place = flags_add(&p->flags, place, field(p, sign_at));
    set_field(p, place_at, place);
    part = flags_sub(&p->flags, part, span);
    ++*steps;
  }
  set_field(p, part_at, part);
  return place;
}

// Down: where it meant to, or beside whoever it pounced at if they are still
// about there.
static void land(Werewolf* p) {
  WerewolfLog* log = p->log;
  Point to = {field(p, WEREWOLF_DP_LAND_X), field(p, WEREWOLF_DP_LAND_Y)};
  const uint16_t quarry = field(p, WEREWOLF_DP_QUARRY);
  log->flight = WEREWOLF_FLIGHT_DOWN_ALONE;
  if (!negative(quarry)) {
    if (!is_record(quarry)) {
      log->declined = true;
      return;
    }
    const uint16_t gap = flags_sub(&p->flags, place_of(p, quarry).x, to.x);
    log->quarry_gap_negative = negative(gap);
    log->flight = WEREWOLF_FLIGHT_DOWN_MISSED;
    if (!flags_at_least(&p->flags, magnitude(gap),
                        WEREWOLF_LAND_BESIDE_WITHIN)) {
      PORT_COVER(werewolf_landed_beside);
      log->flight = WEREWOLF_FLIGHT_DOWN_BESIDE;
      const uint16_t at = (uint16_t)(field(p, WEREWOLF_DP_WAY) << 2);
      to.x = flags_add(&p->flags, table_word(p, WEREWOLF_LAND_BESIDE, at),
                       to.x);
      set_field(p, WEREWOLF_DP_LAND_X, to.x);
      to.y = flags_add(&p->flags, table_word(p, WEREWOLF_LAND_BESIDE + 2, at),
                       to.y);
      set_field(p, WEREWOLF_DP_LAND_Y, to.y);
    }
  }
  set_field(p, WEREWOLF_DP_X, to.x);
  set_field(p, WEREWOLF_DP_Y, to.y);
  set_record_field(p, ACTOR_X, to.x);
  set_record_field(p, ACTOR_Y, to.y);
  set_state(p, WEREWOLF_STATE_LANDED);
}

// `$81:A9F0`: a pass of a pounce.
static void fly(Werewolf* p) {
  WerewolfLog* log = p->log;
  const uint16_t count = field(p, WEREWOLF_DP_RISE);
  log->falling = negative(count);
  const uint16_t rise =
      (uint16_t)((count >> 2) | (negative(count) ? 0xc000u : 0u));
  set_record_field(p, ACTOR_Z,
                   flags_add(&p->flags, rise, record_field(p, ACTOR_Z)));
  set_field(p, WEREWOLF_DP_RISE, (uint16_t)(count - 1));

  Point me;
  me.x = glide(p, WEREWOLF_DP_X, WEREWOLF_DP_PART_X, WEREWOLF_DP_GAP_X,
               WEREWOLF_DP_SIGN_X, &log->glide_steps[0]);
  me.y = glide(p, WEREWOLF_DP_Y, WEREWOLF_DP_PART_Y, WEREWOLF_DP_GAP_Y,
               WEREWOLF_DP_SIGN_Y, &log->glide_steps[1]);
  set_record_field(p, ACTOR_X, me.x);
  set_record_field(p, ACTOR_Y, me.y);

  if (record_field(p, ACTOR_Z) != 0) {
    log->flight = WEREWOLF_FLIGHT_UP;
    return;
  }
  PORT_COVER(werewolf_came_down);
  land(p);
}

// ---------------------------------------------------------------------------
// The pass
// ---------------------------------------------------------------------------

bool werewolf_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + WEREWOLF_DP_STATE));
  if (wram_r16(w, (uint16_t)(page + WEREWOLF_DP_WAY)) > BEARING_UP_LEFT)
    return false;
  switch (state) {
    case WEREWOLF_STATE_RUN:
      return wram_r16(w, (uint16_t)(page + WEREWOLF_DP_PICTURE)) <
             WEREWOLF_RUN_PICTURES;
    case WEREWOLF_STATE_STRIKE:
      return wram_r16(w, (uint16_t)(page + WEREWOLF_DP_PICTURE)) < 0x0100;
    case WEREWOLF_STATE_FLIGHT:
      // A pounce over no distance would never finish its pass.
      return wram_r16(w, (uint16_t)(page + WEREWOLF_DP_SPAN)) != 0;
    default:
      return false;
  }
}

WerewolfFate werewolf_frame(Wram* w, const Rom* rom, uint16_t page,
                          WerewolfLog* log) {
  WerewolfLog scratch;
  if (log == NULL) log = &scratch;
  *log = (WerewolfLog){0};
  Werewolf p = {w, rom, page,
                wram_r16(w, (uint16_t)(page + WEREWOLF_DP_RECORD)), log, {0}};

  log->state = field(&p, WEREWOLF_DP_STATE);
  switch (log->state) {
    case WEREWOLF_STATE_RUN:
      PORT_COVER(werewolf_ran);
      run(&p);
      break;
    case WEREWOLF_STATE_STRIKE:
      PORT_COVER(werewolf_striking);
      strike(&p);
      break;
    default:
      PORT_COVER(werewolf_flew);
      fly(&p);
      break;
  }

  log->c = p.flags.c;
  log->v = p.flags.v;
  log->c_set = p.flags.c_set;
  log->v_set = p.flags.v_set;
  return field(&p, WEREWOLF_DP_FATE) != 0 ? WEREWOLF_ENDS : WEREWOLF_SLEEPS;
}
