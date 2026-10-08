// The chainsaw maniac's state bodies -- see port/chainsaw.h.

#include "port/chainsaw.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/rng.h"

typedef struct {
  uint16_t x, y;
} Point;

// How near whoever is nearest has to be for each thing it does.
#define CHAINSAW_CHASE_WITHIN 0x012c
#define CHAINSAW_KEEP_CHASING_WITHIN 0x00fa
#define CHAINSAW_SWING_WITHIN 0x0020
// A swing is made on a draw under this.
#define CHAINSAW_SWING_ODDS 0xd7

// A refused step that got it less than this is counted, and this many of
// them end a chase.
#define CHAINSAW_SHORT_STEP 0x0002
#define CHAINSAW_GIVE_UP 0x0078

// A quarter turn, in doubled bearings, and the draw's bit that picks left.
#define CHAINSAW_QUARTER_TURN 0x0004
#define CHAINSAW_TURN_LEFT_BIT 0x0002
#define CHAINSAW_WAYS_MASK 0x000f
#define CHAINSAW_FIRST_WAY 0x0002
#define CHAINSAW_LAST_WAY 0x0010

// A stride tries no turn on a frame whose count has none of these bits.
#define CHAINSAW_FRAME_MASK 0x0003

// A picture lasts this many passes. The ones for the ways from here on are
// drawn mirrored.
#define CHAINSAW_PICTURE_PASSES 7
#define CHAINSAW_PICTURES_IN_CYCLE 4
#define CHAINSAW_PICTURE_BANK 0x0090
#define CHAINSAW_FIRST_MIRRORED 0x0030
#define CHAINSAW_MIRROR 0x0002

// A tile it can cut has this in its attributes. It looks sixteen above, to
// the right and to the left, and one below.
#define CHAINSAW_CUTTABLE 0x0040
#define CHAINSAW_HEDGE_REACH 0x0010
#define CHAINSAW_HEDGE_BELOW 0x0001
#define CHAINSAW_HEDGE_SIDES 4
// `$80:B0BB`'s scratch, on page zero.
#define HEDGE_DP_X 0x38
#define HEDGE_DP_Y 0x3a
#define HEDGE_DP_SIDE 0x3c

// Tables in `CHAINSAW_BANK`.
#define CHAINSAW_STEPS 0x90a4u     // by way, doubled: a step across and down
#define CHAINSAW_PICTURES 0x97c9u  // by way and place in the cycle

// The game's count of frames.
#define W_FRAMES 0x0020u

// One maniac's pass.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  ChainsawLog* log;  // never NULL here
  PortFlags flags;
} Chainsaw;

static uint16_t field(const Chainsaw* c, uint16_t at) {
  return wram_r16(c->w, (uint16_t)(c->page + at));
}

static void set_field(Chainsaw* c, uint16_t at, uint16_t v) {
  wram_w16(c->w, (uint16_t)(c->page + at), v);
}

static uint16_t record_field(const Chainsaw* c, uint16_t at) {
  return wram_r16(c->w, (uint16_t)(c->record + at));
}

static void set_record_field(Chainsaw* c, uint16_t at, uint16_t v) {
  wram_w16(c->w, (uint16_t)(c->record + at), v);
}

static uint16_t table_word(const Chainsaw* c, uint16_t table, uint16_t index) {
  return rom_word(c->rom, ((uint32_t)CHAINSAW_BANK << 16) +
                              (uint16_t)(table + index));
}

static Point position(const Chainsaw* c) {
  return (Point){field(c, CHAINSAW_DP_X), field(c, CHAINSAW_DP_Y)};
}

static void set_state(Chainsaw* c, uint16_t body) {
  set_field(c, CHAINSAW_DP_STATE, body);
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

// Quarters of a pixel to pixels. The second `LSR`'s carry is left behind.
static uint16_t whole(Chainsaw* c, uint16_t fine) {
  flags_carry(&c->flags, (fine & 2) != 0);
  return (uint16_t)(fine >> 2);
}

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

// Whoever `actor_nearest` knows, and how far. It leaves carry and overflow
// clear.
static uint16_t nearest_actor(Chainsaw* c, uint16_t* dist) {
  const Point me = position(c);
  ActorNearestWork work;
  const uint16_t found = actor_nearest_counted(c->w, me.x, me.y, dist, &work);
  for (int i = 0; i < NEAREST_BLOCK_COUNT; i++)
    c->log->nearest.blocks[i] += work.blocks[i];
  c->log->nearests++;
  flags_carry(&c->flags, false);
  flags_overflow(&c->flags, false);
  return found;
}

// Which way `target` is, 1 to 8, or 0 on the same spot. Its record is first
// put exactly on the target's row or column if it is within a pixel of it.
//
// Overflow is clear when the two were apart, from the sums that made the
// answer. On the same spot it is still the snap's last difference's.
static uint16_t bearing_to(Chainsaw* c, uint16_t target) {
  flags_sub(&c->flags, record_field(c, ACTOR_Y),
            wram_r16(c->w, (uint16_t)(target + ACTOR_Y)));
  c->log->bearing_asked = true;
  actor_snap_to(c->w, c->record, target, &c->log->snap);
  ActorBearingRegs* r = &c->log->bearing;
  actor_bearing(c->w, c->rom, c->record, target, r);
  flags_carry(&c->flags, r->c);
  if (r->a != 0) flags_overflow(&c->flags, false);
  return r->a;
}

// A draw begins from the carry before it.
static uint16_t random_byte(Chainsaw* c) {
  RngResult r;
  rng_next(c->w, c->flags.c, &r);
  flags_carry(&c->flags, r.c);
  flags_overflow(&c->flags, r.v);
  c->log->draws++;
  c->log->draw_overflow = r.v;
  return r.a;
}

// May it stand at `p`? The ground and whoever is there each have a say, in
// that order. Carry is the answer and overflow the last test's that wrote
// one.
static bool can_stand_at(Chainsaw* c, Point p, ChainsawProbe* probe) {
  ChainsawLog* log = c->log;
  TerrainRegs scratch;
  TerrainRegs* ground = log->grounds < CHAINSAW_MAX_GROUNDS
                            ? &log->ground[log->grounds]
                            : &scratch;
  log->grounds++;
  terrain_blocked_enemy(c->w, p.x, p.y, ground);
  flags_carry(&c->flags, ground->blocked);
  flags_overflow(&c->flags, ground->v);
  probe->blocked = ground->blocked;
  if (ground->blocked) return false;

  AtPointRegs r;
  AtPointWork work;
  actor_at_point_counted(c->w, c->record, p.x, p.y, &r, &work);
  for (int i = 0; i < AT_POINT_BLOCK_COUNT; i++)
    log->at_point.blocks[i] += work.blocks[i];
  probe->someone_asked = true;
  probe->someone = r.found;
  flags_carry(&c->flags, r.found);
  if (r.v_set) flags_overflow(&c->flags, r.v);
  return !r.found;
}

// `$80:8475`: where the scheduler sends this thread's hits.
static void set_hit_handler(Chainsaw* c, uint16_t handler, uint16_t bank) {
  const uint16_t thread = wram_r16(c->w, W_SCHED_CUR_TASK);
  wram_w16(c->w, (uint16_t)(W_THREAD_HANDLER + thread), handler);
  wram_w16(c->w, (uint16_t)(W_THREAD_HANDLER_BANK + thread), bank);
}

// ---------------------------------------------------------------------------
// Hedges
// ---------------------------------------------------------------------------

// Can the tile at `p` be cut? The carry is clear after, and the overflow is
// that of the sum that found the tile in the map.
static bool cuttable(Chainsaw* c, Point p) {
  TileAttrsRegs r;
  tile_attrs_at_pixel(c->w, p.x, p.y, &r);
  const uint16_t row = (uint16_t)((p.y >> TILE_ATTRS_PIXEL_SHIFT) << 1);
  const uint16_t column = (uint16_t)((p.x >> TILE_ATTRS_PIXEL_SHIFT) << 1);
  flags_add(&c->flags, wram_r16(c->w, W_TILE_ROW_BASE + row), column);
  flags_carry(&c->flags, false);
  return (r.a & CHAINSAW_CUTTABLE) != 0;
}

// `$80:B0BB`: is there a hedge beside `me`? Above, right, below, left, in
// that order, and the answer is 1, 3, 5 or 7 for the side, or 0. `at` is
// the point that was looked at.
static uint16_t hedge_beside(Chainsaw* c, Point me, Point* at,
                             ChainsawHedge* log) {
  const Point beside[CHAINSAW_HEDGE_SIDES] = {
      {me.x, (uint16_t)(me.y - CHAINSAW_HEDGE_REACH)},
      {(uint16_t)(me.x + CHAINSAW_HEDGE_REACH), me.y},
      {me.x, (uint16_t)(me.y + CHAINSAW_HEDGE_BELOW)},
      {(uint16_t)(me.x - CHAINSAW_HEDGE_REACH), me.y},
  };
  log->asked = true;
  wram_w16(c->w, HEDGE_DP_X, me.x);
  wram_w16(c->w, HEDGE_DP_Y, me.y);
  for (int i = 0; i < CHAINSAW_HEDGE_SIDES; i++) {
    const uint16_t side = (uint16_t)(2 * i + 1);
    wram_w16(c->w, HEDGE_DP_SIDE, side);
    log->tiles = i + 1;
    if (cuttable(c, beside[i])) {
      log->found = true;
      *at = beside[i];
      return side;
    }
  }
  return 0;
}

// `$81:92C2`: from the next pass it cuts, and takes no hits while it does.
static void begin_cutting(Chainsaw* c, uint16_t side, Point at) {
  PORT_COVER(chainsaw_began_cutting);
  c->log->cuts_begun++;
  set_field(c, CHAINSAW_DP_CUT_SIDE, side);
  set_field(c, CHAINSAW_DP_CUT_X, at.x);
  set_field(c, CHAINSAW_DP_CUT_Y, at.y);
  set_state(c, CHAINSAW_STATE_CUT);
  set_hit_handler(c, 0, 0);
}

static void look_for_hedge(Chainsaw* c, ChainsawHedge* log) {
  Point at = {0, 0};
  const uint16_t side = hedge_beside(c, position(c), &at, log);
  flags_carry(&c->flags, true);  // the `CMP #$0000` of the answer
  if (side != 0) begin_cutting(c, side, at);
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

// Where a step from here along the way at `at` in the table would put it.
// Across is summed first, so the flags left are those of the sum down.
static Point fine_step(Chainsaw* c, uint16_t at) {
  Point fine;
  fine.x = flags_add(&c->flags, table_word(c, CHAINSAW_STEPS, at),
                     field(c, CHAINSAW_DP_FINE_X));
  fine.y = flags_add(&c->flags, table_word(c, CHAINSAW_STEPS + 2, at),
                     field(c, CHAINSAW_DP_FINE_Y));
  return fine;
}

// Write down the step along the way at `at` as the one to try.
static Point try_step(Chainsaw* c, uint16_t at) {
  const Point fine = fine_step(c, at);
  const Point to = {(uint16_t)(fine.x >> 2), whole(c, fine.y)};
  set_field(c, CHAINSAW_DP_TRY_FINE_X, fine.x);
  set_field(c, CHAINSAW_DP_TRY_X, to.x);
  set_field(c, CHAINSAW_DP_TRY_FINE_Y, fine.y);
  set_field(c, CHAINSAW_DP_TRY_Y, to.y);
  return to;
}

// `$81:9107`: take the step being tried, an axis at a time, the second from
// wherever the first left it. True when either axis was refused.
static bool take_step(Chainsaw* c, ChainsawStep* log) {
  Point me = position(c);
  const Point to = {field(c, CHAINSAW_DP_TRY_X), field(c, CHAINSAW_DP_TRY_Y)};
  int refused = 2;
  const uint16_t before = flags_add(&c->flags, me.x, me.y);

  if (can_stand_at(c, (Point){to.x, me.y}, &log->axis[0])) {
    refused--;
    me.x = to.x;
    set_field(c, CHAINSAW_DP_X, me.x);
    set_field(c, CHAINSAW_DP_FINE_X, field(c, CHAINSAW_DP_TRY_FINE_X));
  }
  if (can_stand_at(c, (Point){me.x, to.y}, &log->axis[1])) {
    refused--;
    me.y = to.y;
    set_field(c, CHAINSAW_DP_Y, me.y);
    set_field(c, CHAINSAW_DP_FINE_Y, field(c, CHAINSAW_DP_TRY_FINE_Y));
  }
  set_field(c, CHAINSAW_DP_REFUSED, (uint16_t)refused);

  uint16_t moved =
      flags_sub(&c->flags, flags_add(&c->flags, me.x, me.y), before);
  if (negative(moved)) {
    log->backwards = true;
    moved = (uint16_t)(0u - moved);
  }
  set_field(c, CHAINSAW_DP_MOVED, moved);
  log->refused = flags_at_least(&c->flags, (uint16_t)refused, 1);
  return log->refused;
}

// A way is 2 to 16. `turned` is one with 2 taken off and a turn put on, and
// this brings it back round.
static uint16_t way_of(uint16_t turned) {
  return (uint16_t)((turned & CHAINSAW_WAYS_MASK) + CHAINSAW_FIRST_WAY);
}

// `$81:9169`: a quarter turn.
static void turn(Chainsaw* c) {
  const uint16_t way =
      (uint16_t)(field(c, CHAINSAW_DP_WAY) - CHAINSAW_FIRST_WAY);
  set_field(c, CHAINSAW_DP_WAY,
            way_of(flags_add(&c->flags, way, field(c, CHAINSAW_DP_TURN))));
}

// `$81:90C8`: turn the other way, if the next step that way is clear.
static void turn_if_clear(Chainsaw* c, ChainsawProbe* log) {
  const uint16_t way =
      (uint16_t)(field(c, CHAINSAW_DP_WAY) - CHAINSAW_FIRST_WAY);
  const uint16_t other =
      way_of(flags_sub(&c->flags, way, field(c, CHAINSAW_DP_TURN)));
  set_field(c, CHAINSAW_DP_TRY_WAY, other);

  const Point fine = fine_step(c, (uint16_t)(other << 1));
  const Point to = {(uint16_t)(fine.x >> 2), whole(c, fine.y)};
  set_field(c, CHAINSAW_DP_TRY_X, to.x);
  set_field(c, CHAINSAW_DP_TRY_Y, to.y);
  if (can_stand_at(c, to, log)) {
    PORT_COVER(chainsaw_turned_at_opening);
    set_field(c, CHAINSAW_DP_WAY, other);
  }
}

// `$81:91E5`: a step the way it faces, turning at an opening before it and
// back at a wall after it.
static void stride(Chainsaw* c) {
  ChainsawStride* log = &c->log->stride;
  log->asked = true;
  if ((wram_r16(c->w, W_FRAMES) & CHAINSAW_FRAME_MASK) != 0) {
    log->turn_asked = true;
    turn_if_clear(c, &log->turn);
  }
  try_step(c, (uint16_t)(field(c, CHAINSAW_DP_WAY) << 1));
  if (take_step(c, &log->step)) {
    PORT_COVER(chainsaw_turned_back);
    log->turned_back = true;
    turn(c);
  }
}

// `$81:9178`: a quarter turn left or right, by a draw.
static void turn_at_random(Chainsaw* c) {
  const bool left = (random_byte(c) & CHAINSAW_TURN_LEFT_BIT) != 0;
  c->log->turned_left = left;
  set_field(c, CHAINSAW_DP_TURN,
            left ? (uint16_t)(0u - CHAINSAW_QUARTER_TURN)
                 : CHAINSAW_QUARTER_TURN);
  turn(c);
  set_state(c, CHAINSAW_STATE_TURNED);
}

// ---------------------------------------------------------------------------
// The state bodies
// ---------------------------------------------------------------------------

// `$81:9195`: straight on until the ground stops it.
static void charge(Chainsaw* c) {
  ChainsawLog* log = c->log;
  const Point to = try_step(c, (uint16_t)(field(c, CHAINSAW_DP_WAY) << 1));
  if (can_stand_at(c, to, &log->ahead)) {
    set_field(c, CHAINSAW_DP_X, to.x);
    set_field(c, CHAINSAW_DP_Y, to.y);
    set_field(c, CHAINSAW_DP_FINE_X, field(c, CHAINSAW_DP_TRY_FINE_X));
    set_field(c, CHAINSAW_DP_FINE_Y, field(c, CHAINSAW_DP_TRY_FINE_Y));
    return;
  }
  if (!log->ahead.blocked) return;  // somebody is in the way: it waits

  if (negative(field(c, CHAINSAW_DP_COUNT))) {
    PORT_COVER(chainsaw_charge_wandered);
    log->count_negative = true;
    turn(c);
    set_state(c, CHAINSAW_STATE_WANDER);
  } else {
    PORT_COVER(chainsaw_charge_turned);
    turn_at_random(c);
  }
}

// `$81:921B`: stride on, until somebody is near enough to chase.
static void wander(Chainsaw* c) {
  uint16_t dist;
  nearest_actor(c, &dist);
  if (flags_at_least(&c->flags, dist, CHAINSAW_CHASE_WITHIN)) {
    stride(c);
    return;
  }
  PORT_COVER(chainsaw_gave_chase);
  c->log->someone_near = true;
  set_state(c, CHAINSAW_STATE_CHASE);
}

// `$81:9235`: stride for the passes it has left, cutting any hedge it comes
// beside.
static void stride_turned(Chainsaw* c) {
  const uint16_t left = (uint16_t)(field(c, CHAINSAW_DP_COUNT) - 1);
  set_field(c, CHAINSAW_DP_COUNT, left);
  if (negative(left)) {
    PORT_COVER(chainsaw_turn_over);
    c->log->time_up = true;
    set_state(c, CHAINSAW_STATE_WANDER);
    return;
  }
  stride(c);
  look_for_hedge(c, &c->log->hedge);
}

// `$81:9530`: swing at somebody beside it, on a draw.
static void maybe_swing(Chainsaw* c) {
  ChainsawLog* log = c->log;
  if (flags_at_least(&c->flags, random_byte(c), CHAINSAW_SWING_ODDS)) return;
  log->swing_drawn = true;
  uint16_t dist;
  nearest_actor(c, &dist);
  if (flags_at_least(&c->flags, dist, CHAINSAW_SWING_WITHIN)) return;
  PORT_COVER(chainsaw_swung);
  log->swung = true;
  set_state(c, CHAINSAW_STATE_SWING);
}

// `$81:927D`: one step of a chase. True when the chase is given up.
static bool chase_step(Chainsaw* c, ChainsawChaseStep* log) {
  try_step(c, field(c, CHAINSAW_DP_STEP_AT));
  if (!take_step(c, &log->step)) return false;

  if (!flags_at_least(&c->flags, field(c, CHAINSAW_DP_MOVED),
                      CHAINSAW_SHORT_STEP)) {
    log->short_of = true;
    const uint16_t count = (uint16_t)(field(c, CHAINSAW_DP_COUNT) + 1);
    set_field(c, CHAINSAW_DP_COUNT, count);
    if (flags_at_least(&c->flags, count, CHAINSAW_GIVE_UP)) {
      PORT_COVER(chainsaw_chase_gave_up);
      log->gave_up = true;
      return true;
    }
  }
  look_for_hedge(c, &log->hedge);
  return false;
}

// `$81:9256`: at whoever is nearest, two steps a pass. A step that gives up
// only says what the next pass is to do: the other is still taken.
static void chase(Chainsaw* c) {
  ChainsawLog* log = c->log;
  maybe_swing(c);

  uint16_t dist;
  const uint16_t target = nearest_actor(c, &dist);
  if (flags_at_least(&c->flags, dist, CHAINSAW_KEEP_CHASING_WITHIN)) {
    PORT_COVER(chainsaw_lost_them);
    log->nobody = true;
    set_state(c, CHAINSAW_STATE_CHARGE);
    return;
  }
  const uint16_t bearing = bearing_to(c, target);
  if (bearing == 0) {
    PORT_COVER(chainsaw_on_them);
    log->on_them = true;
    set_state(c, CHAINSAW_STATE_CHARGE);
    return;
  }
  set_field(c, CHAINSAW_DP_WAY, (uint16_t)(bearing << 1));
  set_field(c, CHAINSAW_DP_STEP_AT, (uint16_t)(bearing << 2));
  flags_carry(&c->flags, false);  // the two `ASL`s

  log->chase_steps = CHAINSAW_CHASE_STEPS;
  for (int i = 0; i < CHAINSAW_CHASE_STEPS; i++)
    if (chase_step(c, &log->chase[i])) set_state(c, CHAINSAW_STATE_CHARGE);
}

// ---------------------------------------------------------------------------
// The pass
// ---------------------------------------------------------------------------

// `$81:977D`: its next picture when this one has had its passes, and its
// record put where it now is.
static void show(Chainsaw* c) {
  ChainsawLog* log = c->log;
  const uint16_t wait = (uint16_t)(field(c, CHAINSAW_DP_PICTURE_WAIT) - 1);
  set_field(c, CHAINSAW_DP_PICTURE_WAIT, wait);
  if (wait == 0) {
    log->new_picture = true;
    set_field(c, CHAINSAW_DP_PICTURE_WAIT, CHAINSAW_PICTURE_PASSES);
    const uint16_t picture = (uint16_t)((field(c, CHAINSAW_DP_PICTURE) + 1) &
                                        (CHAINSAW_PICTURES_IN_CYCLE - 1));
    set_field(c, CHAINSAW_DP_PICTURE, picture);
    const uint16_t at =
        (uint16_t)(((field(c, CHAINSAW_DP_WAY) << 1) | picture) << 1);
    set_record_field(c, ACTOR_META, table_word(c, CHAINSAW_PICTURES, at));
    set_record_field(c, ACTOR_META_BANK, CHAINSAW_PICTURE_BANK);

    const uint16_t flags = record_field(c, ACTOR_FLAGS);
    log->mirrored = flags_at_least(&c->flags, at, CHAINSAW_FIRST_MIRRORED);
    set_record_field(c, ACTOR_FLAGS,
                     log->mirrored ? (uint16_t)(flags | CHAINSAW_MIRROR)
                                   : (uint16_t)(flags & ~CHAINSAW_MIRROR));
  }
  const Point me = position(c);
  set_record_field(c, ACTOR_X, me.x);
  set_record_field(c, ACTOR_Y, me.y);
}

static bool way_known(const Wram* w, uint16_t page) {
  const uint16_t way = wram_r16(w, (uint16_t)(page + CHAINSAW_DP_WAY));
  return way >= CHAINSAW_FIRST_WAY && way <= CHAINSAW_LAST_WAY &&
         (way & 1) == 0;
}

// Its picture's place in the cycle is not asked about: a swing leaves it
// anywhere, and the next picture masks it.
bool chainsaw_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + CHAINSAW_DP_STATE));
  if (!way_known(w, page)) return false;
  return state == CHAINSAW_STATE_CHARGE || state == CHAINSAW_STATE_WANDER ||
         state == CHAINSAW_STATE_TURNED || state == CHAINSAW_STATE_CHASE ||
         state == CHAINSAW_STATE_SWING;
}

// `$81:9880`: the end of every pass.
static ChainsawFate finish(Chainsaw* c) {
  ChainsawLog* log = c->log;
  show(c);

  log->c = c->flags.c;
  log->v = c->flags.v;
  log->c_set = c->flags.c_set;
  log->v_set = c->flags.v_set;
  if (negative(field(c, CHAINSAW_DP_HEALTH))) {
    PORT_COVER(chainsaw_died);
    return CHAINSAW_ENDS;
  }
  return CHAINSAW_SLEEPS;
}

ChainsawFate chainsaw_frame(Wram* w, const Rom* rom, uint16_t page, bool carry,
                            ChainsawLog* log) {
  ChainsawLog scratch;
  if (log == NULL) log = &scratch;
  *log = (ChainsawLog){0};
  Chainsaw c = {w, rom, page,
                wram_r16(w, (uint16_t)(page + CHAINSAW_DP_RECORD)), log,
                {carry, false, false, false}};

  log->state = field(&c, CHAINSAW_DP_STATE);
  switch (log->state) {
    case CHAINSAW_STATE_CHARGE:
      PORT_COVER(chainsaw_charged);
      charge(&c);
      break;
    case CHAINSAW_STATE_WANDER:
      PORT_COVER(chainsaw_wandered);
      wander(&c);
      break;
    case CHAINSAW_STATE_TURNED:
      PORT_COVER(chainsaw_strode_turned);
      stride_turned(&c);
      break;
    default:
      PORT_COVER(chainsaw_chased);
      chase(&c);
      break;
  }
  return finish(&c);
}

// ---------------------------------------------------------------------------
// The swing
// ---------------------------------------------------------------------------

// By the way it faces, four words: its picture, and where the saw is from
// it, across and down. The turn goes back through the table.
#define CHAINSAW_SWING_TABLE 0x95bau
#define CHAINSAW_SWING_ENTRY 8
#define CHAINSAW_SWING_PICTURES 8
#define CHAINSAW_SWING_LAST_AT \
  ((CHAINSAW_SWING_PICTURES - 1) * CHAINSAW_SWING_ENTRY)
#define CHAINSAW_SAW_COLLIDE_ID 0x0003
#define CHAINSAW_NO_SAW 0xffffu

// `$81:956D`: the picture for this eighth of the turn, and the saw where
// that holds it. Then three ticks.
static void swing_show(Chainsaw* c, PortCpu* cpu) {
  const uint16_t at = field(c, CHAINSAW_DP_PICTURE);
  const uint16_t saw = field(c, CHAINSAW_DP_SAW);
  const Point me = position(c);
  set_record_field(c, ACTOR_META, table_word(c, CHAINSAW_SWING_TABLE, at));
  set_c(cpu, false);
  wram_w16(c->w, (uint16_t)(saw + ACTOR_X),
           adc16(cpu, table_word(c, CHAINSAW_SWING_TABLE + 2, at), me.x));
  set_c(cpu, false);
  wram_w16(c->w, (uint16_t)(saw + ACTOR_Y),
           adc16(cpu, table_word(c, CHAINSAW_SWING_TABLE + 4, at), me.y));
  wram_w16(c->w, (uint16_t)(saw + ACTOR_COLLIDE_ID), CHAINSAW_SAW_COLLIDE_ID);
  cpu->a = CHAINSAW_SWING_TICKS;
  set_nz16(cpu, cpu->a);
  cpu->x = at;
  cpu->y = saw;
  cpu->pc = CHAINSAW_SWING_YIELD_PC;
}

void chainsaw_swing_begin(Wram* w, const Rom* rom, PortCpu* cpu,
                          ChainsawSwingLog* log) {
  *log = (ChainsawSwingLog){0};
  Chainsaw c = {w, rom, cpu->d,
                wram_r16(w, (uint16_t)(cpu->d + CHAINSAW_DP_RECORD)),
                &log->pass, {false, false, false, false}};
  push16(w, cpu, CHAINSAW_PASS_RETURN);

  set_field(&c, CHAINSAW_DP_SWING_LEFT, CHAINSAW_SWING_PICTURES);
  set_field(&c, CHAINSAW_DP_PICTURE,
            (uint16_t)((field(&c, CHAINSAW_DP_WAY) - CHAINSAW_FIRST_WAY) << 2));
  set_record_field(&c, ACTOR_FLAGS,
                   (uint16_t)(record_field(&c, ACTOR_FLAGS) & ~CHAINSAW_MIRROR));

  // `$81:95FA`: a record for the saw, where it stands, with no picture.
  SlotAllocRegs taken;
  actor_slot_alloc(w, cpu->db, &taken);
  if (taken.c) {
    log->declined = true;
    return;
  }
  PORT_COVER(chainsaw_swing_began);
  const uint16_t saw = taken.a;
  const Point me = position(&c);
  log->record = saw;
  wram_w16(w, (uint16_t)(saw + ACTOR_X), me.x);
  wram_w16(w, (uint16_t)(saw + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(saw + ACTOR_Y), me.y);
  wram_w16(w, (uint16_t)(saw + ACTOR_META), 0);
  wram_w16(w, (uint16_t)(saw + ACTOR_META_BANK), 0);
  wram_w16(w, (uint16_t)(saw + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(saw + ACTOR_COLLIDE_ID), CHAINSAW_SAW_COLLIDE_ID);
  wram_w16(w, (uint16_t)(saw + ACTOR_FLAGS),
           (uint16_t)(0x8000u | wram_r16(w, (uint16_t)(saw + ACTOR_FLAGS))));
  set_field(&c, CHAINSAW_DP_SAW, saw);
  swing_show(&c, cpu);
}

bool chainsaw_swing_next_supported(const Wram* w, uint16_t page, uint16_t s) {
  const uint16_t at = wram_r16(w, (uint16_t)(page + CHAINSAW_DP_PICTURE));
  const uint16_t left = wram_r16(w, (uint16_t)(page + CHAINSAW_DP_SWING_LEFT));
  const uint16_t saw = wram_r16(w, (uint16_t)(page + CHAINSAW_DP_SAW));
  return way_known(w, page) && at <= CHAINSAW_SWING_LAST_AT &&
         at % CHAINSAW_SWING_ENTRY == 0 && left >= 1 &&
         left <= CHAINSAW_SWING_PICTURES && saw >= W_ACTOR_SLOTS &&
         saw <= ACTOR_SLOT_LAST && actor_list_place(w, saw) != -3 &&
         wram_r16(w, (uint16_t)(s + 1)) == CHAINSAW_PASS_RETURN;
}

void chainsaw_swing_next(Wram* w, const Rom* rom, PortCpu* cpu,
                         ChainsawSwingLog* log) {
  *log = (ChainsawSwingLog){0};
  Chainsaw c = {w, rom, cpu->d,
                wram_r16(w, (uint16_t)(cpu->d + CHAINSAW_DP_RECORD)),
                &log->pass, {false, false, false, false}};

  // On round the turn.
  set_c(cpu, true);
  uint16_t at = sbc16(cpu, field(&c, CHAINSAW_DP_PICTURE), CHAINSAW_SWING_ENTRY);
  if (negative(at)) {
    PORT_COVER(chainsaw_swing_wrapped);
    log->wrapped = true;
    at = CHAINSAW_SWING_LAST_AT;
  }
  set_field(&c, CHAINSAW_DP_PICTURE, at);
  const uint16_t left = (uint16_t)(field(&c, CHAINSAW_DP_SWING_LEFT) - 1);
  set_field(&c, CHAINSAW_DP_SWING_LEFT, left);
  if (left != 0) {
    PORT_COVER(chainsaw_swing_turned);
    log->more = true;
    swing_show(&c, cpu);
    return;
  }

  // The turn is made. The saw's record goes, and it chases again -- and
  // may swing again at once.
  PORT_COVER(chainsaw_swing_ended);
  const uint16_t saw = field(&c, CHAINSAW_DP_SAW);
  log->free_place = actor_list_place(w, saw);
  SlotFreeRegs freed;
  actor_slot_free(w, saw, cpu->d, cpu->x, cpu->y, &freed);
  set_field(&c, CHAINSAW_DP_SAW, CHAINSAW_NO_SAW);
  set_state(&c, CHAINSAW_STATE_CHASE);
  c.flags = (PortFlags){freed.c, flag(cpu, PORT_P_V), true, true};
  maybe_swing(&c);
  cpu->s = (uint16_t)(cpu->s + 2);  // the `RTS` back to the pass
  // The pass's `JSR` to its picture puts its own return where that one was.
  push16(w, cpu, CHAINSAW_PASS_SHOWN_RETURN);
  cpu->s = (uint16_t)(cpu->s + 2);
  log->fate = finish(&c);
}
