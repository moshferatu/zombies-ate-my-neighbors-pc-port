// The flying saucer's state bodies -- see port/saucer.h.

#include "port/saucer.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/figure_colours.h"
#include "port/flags.h"
#include "port/rng.h"
#include "port/thread.h"

typedef struct {
  uint16_t x, y;
} Point;

// How near the spot its quarry has to be for each thing it does.
#define SAUCER_ON_IT 0x000c
#define SAUCER_CLOSE 0x0040
#define SAUCER_FAR 0x00fa
// How far `player_bearing` looks: anywhere, and for holding over someone.
#define SAUCER_ANYWHERE 0x7fff
#define SAUCER_HOLD_REACH 0x000e

// A shot is fired on a draw under this, and not again for this many passes.
#define SAUCER_SHOT_ODDS 0x1e
#define SAUCER_SHOT_WAIT 0x0064
#define SAUCER_SHOTS 2
#define SAUCER_SHOT_THREAD 0xf49eu

// Out of shots, a draw of this or more swoops. Under it, it opens for this
// long, or until its quarry is this near.
#define SAUCER_SPENT_SWOOPS 0x7d
#define SAUCER_SPENT_OPEN 0x0078
#define SAUCER_SPENT_REACH 0x0010
// After a swoop it opens for longer, and gives up further off.
#define SAUCER_SWOOPED_OPEN 0x00c8
#define SAUCER_SWOOPED_REACH 0x00af
// A swoop lasts this long and a draw of up to fifteen more.
#define SAUCER_SWOOP_LEAST 0x0010
#define SAUCER_SWOOP_DRAW 0x000f

// The level's left margin, for a saucer: it tries no move to the left of it.
#define SAUCER_LEFT_MARGIN 0x0080

// A slanted way is skipped on frames that are a multiple of four, and a
// wobble is made on those only.
#define SAUCER_FRAME_MASK 0x0003
#define SAUCER_WOBBLE_STEPS 8

// The hatch: where it is from the saucer, what it collides as, and the
// handler the thread takes hits on while it is open.
#define SAUCER_HATCH_DX 0xffdeu  // -34
#define SAUCER_HATCH_DY 0xff9au  // -102
#define SAUCER_HATCH_ID 0x0036
#define SAUCER_HATCH_PICTURE 0xd7cau
#define SAUCER_HATCH_PICTURE_BANK 0x008f
#define SAUCER_HATCH_FLAGS (ACTOR_DRAW | ACTOR_SORT_FIRST | ACTOR_PRIORITY_TOP)
#define SAUCER_HIT_HANDLER 0x84acu
#define SAUCER_NO_HATCH 0xffffu
#define SAUCER_HATCH_PICTURES 4

// Its picture, from itself.
#define SAUCER_PICTURE_DX 0x004f
#define SAUCER_PICTURE_DY 0x00c4

// The lights change every fifth pass, between two sets of colours `$20`
// apart. A flash has its own, and holds the lights for four passes.
#define SAUCER_LIGHTS_EVERY 5
#define SAUCER_LIGHTS_STEP 0x0020
#define SAUCER_COLOURS 0xffabu
#define SAUCER_COLOURS_BANK 0x94
#define SAUCER_FLASH_COLOURS 0xffd1u
#define SAUCER_FLASH_COLOURS_BANK 0x97
#define SAUCER_FLASH_PASSES 4
#define SAUCER_FULL_HEALTH 9
// The jobs that set the mosaic of a flash, and clear it.
#define SAUCER_MOSAIC_JOB 0x8425u
#define SAUCER_MOSAIC_OFF_JOB 0x882cu

// Tables in `SAUCER_BANK`.
#define SAUCER_SPOT_ASIDE 0x85bdu  // by side: where the spot is from it
#define SAUCER_LINE_ASIDE 0x85c1u  // ...and the line its quarry crosses
#define SAUCER_STEPS 0x85c5u       // by heading: a hunting move
#define SAUCER_ON_TARGET_WOBBLE 0x8637u
#define SAUCER_SWOOP_STEPS 0x86adu  // by heading: a swooping move
#define SAUCER_OPEN_WOBBLE 0x871cu
#define SAUCER_HATCH_CYCLE 0x8415u  // a picture and how long it is held

// The game's count of frames.
#define W_FRAMES 0x0020u

// A bearing is 1 to 8, and the thread keeps it doubled.
#define SAUCER_MAX_HEADING 0x0010

// One saucer's pass.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  SaucerLog* log;  // never NULL here
  PortFlags flags;
} Saucer;

static uint16_t field(const Saucer* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->page + at));
}

static void set_field(Saucer* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->page + at), v);
}

static uint16_t record_field(const Saucer* s, uint16_t record, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(record + at));
}

static void set_record_field(Saucer* s, uint16_t record, uint16_t at,
                             uint16_t v) {
  wram_w16(s->w, (uint16_t)(record + at), v);
}

static Point position(const Saucer* s) {
  return (Point){wram_r16(s->w, W_BOSS_X), wram_r16(s->w, W_BOSS_Y)};
}

// The spot it flies to put on its quarry: to one side of it, and level.
static Point spot(const Saucer* s) {
  return (Point){field(s, SAUCER_DP_AIM_X), wram_r16(s->w, W_BOSS_Y)};
}

static uint16_t table_word(const Saucer* s, uint16_t table, uint16_t index) {
  return rom_word(s->rom, ((uint32_t)SAUCER_BANK << 16) + table + index);
}

static void set_state(Saucer* s, uint16_t body) {
  set_field(s, SAUCER_DP_STATE, body);
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

static uint16_t frame_count(const Saucer* s) {
  return wram_r16(s->w, W_FRAMES);
}

// A record the thread's data bank can reach, which is all of them.
static bool is_record(uint16_t record) { return record < 0x1f00u; }

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

// Whoever `actor_nearest` knows nearest to `from`, and how far.
static uint16_t nearest_actor(Saucer* s, Point from, uint16_t* dist) {
  ActorNearestWork work;
  const uint16_t found =
      actor_nearest_counted(s->w, from.x, from.y, dist, &work);
  for (int i = 0; i < NEAREST_BLOCK_COUNT; i++)
    s->log->nearest.blocks[i] += work.blocks[i];
  return found;
}

// Which way the nearer player is from the spot, within `reach`: 1 to 8, or
// zero for neither.
static uint16_t player_way(Saucer* s, uint16_t reach) {
  const Point from = spot(s);
  PlayerPickRegs* r = &s->log->players;
  player_bearing(s->w, s->rom, reach, from.x, from.y, r);
  s->log->bearing_asked = true;
  flags_carry(&s->flags, r->c);
  return r->a;
}

// A draw begins from the carry before it.
static uint16_t random_byte(Saucer* s) {
  RngResult r;
  rng_next(s->w, s->flags.c, &r);
  flags_carry(&s->flags, r.c);
  flags_overflow(&s->flags, r.v);
  if (s->log->draws < SAUCER_MAX_DRAWS)
    s->log->draw_overflow[s->log->draws] = r.v;
  s->log->draws++;
  return r.a;
}

static bool off_the_level(Saucer* s, Point p, SaucerEdge* edge) {
  BoundsRegs r;
  terrain_out_of_bounds(s->w, p.x, p.y, &r);
  flags_carry(&s->flags, r.c);
  *edge = (SaucerEdge){r.exit, r.c};
  return r.c;
}

static void set_colours(Saucer* s, uint16_t at, uint8_t bank) {
  s->log->colours_asked = true;
  s->log->colours_slot = figure_colours_set(s->w, s->rom, at, bank);
  flags_carry(&s->flags, s->log->colours_slot < 0);
}

static void queue_job(Saucer* s, uint16_t job) {
  s->log->job_asked = true;
  s->log->job_slot = vbl_queue_a_add(s->w, job, SAUCER_BANK);
  flags_carry(&s->flags, s->log->job_slot < 0);
}

// `$80:8475`: where the scheduler sends this thread's hits.
static void set_hit_handler(Saucer* s, uint16_t handler, uint16_t bank) {
  const uint16_t thread = wram_r16(s->w, W_SCHED_CUR_TASK);
  wram_w16(s->w, (uint16_t)(W_THREAD_HANDLER + thread), handler);
  wram_w16(s->w, (uint16_t)(W_THREAD_HANDLER_BANK + thread), bank);
}

// ---------------------------------------------------------------------------
// The spot, and moving
// ---------------------------------------------------------------------------

// `$82:84CE`: choose the side its quarry is on, and hunt.
static void take_aim(Saucer* s) {
  s->log->aimed = true;
  set_state(s, SAUCER_STATE_HUNT);

  const Point me = position(s);
  uint16_t dist;
  const uint16_t quarry = nearest_actor(s, me, &dist);
  if (!is_record(quarry)) {
    s->log->declined = true;
    return;
  }
  const bool quarry_left = me.x >= record_field(s, quarry, ACTOR_X);
  const uint16_t side = quarry_left ? 2 : 0;
  set_field(s, SAUCER_DP_AIM_X,
            (uint16_t)(table_word(s, SAUCER_SPOT_ASIDE, side) + me.x));
  set_field(s, SAUCER_DP_LINE_X,
            (uint16_t)(table_word(s, SAUCER_LINE_ASIDE, side) + me.x));
  set_field(s, SAUCER_DP_REAIM_WAIT, 1);
}

// `$82:8337`: take the move being tried, an axis at a time. Across is tested
// one to the right of where it would go, and brings the spot along. True
// when either axis was not taken.
static bool move(Saucer* s) {
  SaucerLog* log = s->log;
  log->moved = true;
  int refused = 2;

  uint16_t test_x = (uint16_t)(field(s, SAUCER_DP_TRY_X) + 1);
  if (test_x < SAUCER_LEFT_MARGIN) {
    PORT_COVER(saucer_clamped);
    log->clamped = true;
    test_x = SAUCER_LEFT_MARGIN;
    set_field(s, SAUCER_DP_TRY_X, SAUCER_LEFT_MARGIN);
  }
  set_field(s, SAUCER_DP_REFUSED, 2);
  if (!off_the_level(s, (Point){test_x, wram_r16(s->w, W_BOSS_Y)},
                     &log->edge[0])) {
    wram_w16(s->w, W_BOSS_X, field(s, SAUCER_DP_TRY_X));
    set_field(s, SAUCER_DP_AIM_X, field(s, SAUCER_DP_TRY_AIM_X));
    refused--;
  } else {
    PORT_COVER(saucer_edge_across);
  }
  const Point down = {wram_r16(s->w, W_BOSS_X), field(s, SAUCER_DP_TRY_Y)};
  if (!off_the_level(s, down, &log->edge[1])) {
    wram_w16(s->w, W_BOSS_Y, down.y);
    refused--;
  } else {
    PORT_COVER(saucer_edge_down);
  }
  set_field(s, SAUCER_DP_REFUSED, (uint16_t)refused);
  return flags_at_least(&s->flags, (uint16_t)refused, 1);
}

// The next step of a small circle about where it is.
static void try_wobble(Saucer* s, uint16_t table) {
  const uint16_t step = (uint16_t)(field(s, SAUCER_DP_WOBBLE) + 1);
  set_field(s, SAUCER_DP_WOBBLE, step);
  const uint16_t at = (uint16_t)((step & (SAUCER_WOBBLE_STEPS - 1)) << 2);
  const Point me = position(s);
  set_field(s, SAUCER_DP_TRY_X, (uint16_t)(table_word(s, table, at) + me.x));
  set_field(s, SAUCER_DP_TRY_Y,
            (uint16_t)(table_word(s, table + 2, at) + me.y));
}

// ---------------------------------------------------------------------------
// The shot
// ---------------------------------------------------------------------------

// `$82:8375`: shoot at the spot, on a draw, if the last shot was long enough
// ago. The shot is told how far aside the spot is.
static void maybe_shoot(Saucer* s) {
  SaucerLog* log = s->log;
  log->shoot_asked = true;
  if (random_byte(s) >= SAUCER_SHOT_ODDS) return;
  log->shot_drawn = true;
  if (field(s, SAUCER_DP_SHOT_WAIT) != 0) {
    PORT_COVER(saucer_shot_waited);
    return;
  }
  PORT_COVER(saucer_shot);
  log->shot = true;
  set_field(s, SAUCER_DP_SHOT_ASIDE,
            (uint16_t)(field(s, SAUCER_DP_AIM_X) - position(s).x));
  log->shot_slot =
      thread_spawn(s->w, s->rom, SAUCER_SHOT_THREAD, SAUCER_BANK, s->page);
  set_field(s, SAUCER_DP_SHOT_WAIT, SAUCER_SHOT_WAIT);
  set_field(s, SAUCER_DP_SHOTS_LEFT,
            (uint16_t)(field(s, SAUCER_DP_SHOTS_LEFT) - 1));
}

// ---------------------------------------------------------------------------
// The hatch
// ---------------------------------------------------------------------------

static Point hatch_place(Saucer* s) {
  const Point me = position(s);
  return (Point){(uint16_t)(me.x + SAUCER_HATCH_DX),
                 flags_add(&s->flags, me.y, SAUCER_HATCH_DY)};
}

// `$82:8439`: open it, if it is shut. From now on the thread can be hit.
static void open_hatch(Saucer* s) {
  SaucerLog* log = s->log;
  log->hatch_asked = true;
  if (field(s, SAUCER_DP_HATCH) != SAUCER_NO_HATCH) return;

  SlotAllocRegs r;
  actor_slot_alloc(s->w, SAUCER_BANK, &r);
  if (r.c) {  // none free, and the ROM goes on with what is no record
    log->declined = true;
    return;
  }
  PORT_COVER(saucer_hatch_opened);
  log->hatch_opened = true;
  log->hatch_record = r.a;
  const uint16_t hatch = r.a;
  const Point at = hatch_place(s);
  set_field(s, SAUCER_DP_HATCH, hatch);
  set_record_field(s, hatch, ACTOR_X, at.x);
  set_record_field(s, hatch, ACTOR_Z, 0);
  set_record_field(s, hatch, ACTOR_Y, at.y);
  set_record_field(s, hatch, ACTOR_THREAD, wram_r16(s->w, W_SCHED_CUR_TASK));
  set_record_field(s, hatch, ACTOR_COLLIDE_ID, SAUCER_HATCH_ID);
  set_record_field(s, hatch, ACTOR_META, SAUCER_HATCH_PICTURE);
  set_record_field(s, hatch, ACTOR_META_BANK, SAUCER_HATCH_PICTURE_BANK);
  set_record_field(s, hatch, ACTOR_FLAGS,
                   record_field(s, hatch, ACTOR_FLAGS) | SAUCER_HATCH_FLAGS);
  set_field(s, SAUCER_DP_HATCH_HOLD, 0);
  set_field(s, SAUCER_DP_HATCH_CYCLE, 0);
  set_field(s, SAUCER_DP_FLASH, 0xffffu);
  set_hit_handler(s, SAUCER_HIT_HANDLER, SAUCER_BANK);
}

// Where in the display list a record is, for the harness: the ROM's unlink
// walks that far. -1 for one that is not this thread's and -2 for one not in
// use, neither of which is freed, and -3 for one the list does not hold.
static int place_in_list(const Saucer* s, uint16_t record) {
  if (wram_r16(s->w, W_SCHED_CUR_TASK) != record_field(s, record, ACTOR_THREAD))
    return -1;
  if (!(record_field(s, record, ACTOR_FLAGS) & ACTOR_ACTIVE)) return -2;
  int place = 0;
  for (uint16_t at = wram_r16(s->w, W_ACTOR_LIST_HEAD); at != record;
       at = record_field(s, at, ACTOR_NEXT)) {
    if (at == 0 || !is_record(at) || ++place > ACTOR_SLOT_COUNT) return -3;
  }
  return place;
}

// `$82:8496`: shut it, and take no more hits.
static void shut_hatch(Saucer* s) {
  PORT_COVER(saucer_hatch_shut);
  SaucerLog* log = s->log;
  const uint16_t hatch = field(s, SAUCER_DP_HATCH);
  log->hatch_shut = true;
  log->shut_place = place_in_list(s, hatch);
  if (log->shut_place == -3) {  // a list the port does not follow
    log->declined = true;
    return;
  }
  SlotFreeRegs r;
  actor_slot_free(s->w, hatch, s->page, 0, 0, &r);
  set_field(s, SAUCER_DP_HATCH, SAUCER_NO_HATCH);
  set_hit_handler(s, 0, 0);
}

// What a flash does to the colours, on the pass it begins and the pass it
// is over.
static void flash(Saucer* s) {
  SaucerLog* log = s->log;
  const uint16_t left = field(s, SAUCER_DP_FLASH);
  if (negative(left)) {
    log->flash = SAUCER_FLASH_NONE;
  } else if (left == 0) {
    PORT_COVER(saucer_flash_over);
    log->flash = SAUCER_FLASH_OVER;
    set_colours(s, SAUCER_COLOURS, SAUCER_COLOURS_BANK);
  } else if (left == SAUCER_FLASH_PASSES) {
    PORT_COVER(saucer_flash_began);
    log->flash = SAUCER_FLASH_BEGINS;
    set_field(s, SAUCER_DP_LIGHTS_WAIT, SAUCER_FLASH_PASSES);
    set_colours(s, SAUCER_FLASH_COLOURS, SAUCER_FLASH_COLOURS_BANK);
    wram_w16(s->w, W_SAUCER_MOSAIC,
             (uint16_t)(SAUCER_FULL_HEALTH - field(s, SAUCER_DP_HEALTH)));
    queue_job(s, SAUCER_MOSAIC_JOB);
  } else {
    log->flash = SAUCER_FLASH_RUNNING;
  }
  set_field(s, SAUCER_DP_FLASH, (uint16_t)(left - 1));
}

// `$82:839C`: the flash, the hatch's next picture when this one has been
// held long enough, and the hatch put where the saucer now is. The ROM
// sleeps a tick here, and so the pass ends.
static void show_hatch(Saucer* s) {
  SaucerLog* log = s->log;
  log->showed = true;
  flash(s);

  const uint16_t hatch = field(s, SAUCER_DP_HATCH);
  if (!is_record(hatch)) {
    log->declined = true;
    return;
  }
  const uint16_t hold = (uint16_t)(field(s, SAUCER_DP_HATCH_HOLD) - 1);
  set_field(s, SAUCER_DP_HATCH_HOLD, hold);
  if (negative(hold)) {
    PORT_COVER(saucer_hatch_turned);
    log->next_picture = true;
    const uint16_t cycle = (uint16_t)((field(s, SAUCER_DP_HATCH_CYCLE) + 4) &
                                      ((SAUCER_HATCH_PICTURES - 1) << 2));
    set_field(s, SAUCER_DP_HATCH_CYCLE, cycle);
    set_record_field(s, hatch, ACTOR_META,
                     table_word(s, SAUCER_HATCH_CYCLE, cycle));
    set_field(s, SAUCER_DP_HATCH_HOLD,
              table_word(s, SAUCER_HATCH_CYCLE + 2, cycle));
  }
  const Point at = hatch_place(s);
  set_record_field(s, hatch, ACTOR_X, at.x);
  set_record_field(s, hatch, ACTOR_Y, at.y);
}

// ---------------------------------------------------------------------------
// The state bodies. Each answers whether the pass ended showing the hatch.
// ---------------------------------------------------------------------------

// `$82:86D1`: hang open until its time is up or its quarry is near enough.
static bool hang_open(Saucer* s) {
  SaucerLog* log = s->log;
  open_hatch(s);
  if (log->declined) return false;
  take_aim(s);
  if (log->declined) return false;

  const uint16_t left = (uint16_t)(field(s, SAUCER_DP_TIMER) - 1);
  set_field(s, SAUCER_DP_TIMER, left);
  if (negative(left)) {
    PORT_COVER(saucer_open_timed_out);
    log->open = SAUCER_OPEN_TIMED_OUT;
    shut_hatch(s);
    return false;
  }
  uint16_t dist;
  nearest_actor(s, spot(s), &dist);
  if (dist < field(s, SAUCER_DP_REACH)) {
    PORT_COVER(saucer_open_reached);
    log->open = SAUCER_OPEN_REACHED;
    shut_hatch(s);
    return false;
  }
  set_state(s, SAUCER_STATE_OPEN);
  if ((frame_count(s) & SAUCER_FRAME_MASK) != 0) {
    log->open = SAUCER_OPEN_WAITED;
    return false;
  }
  PORT_COVER(saucer_open_wobbled);
  log->open = SAUCER_OPEN_WOBBLED;
  try_wobble(s, SAUCER_OPEN_WOBBLE);
  move(s);
  show_hatch(s);
  return true;
}

// `$82:8657`: swoop at the nearer player, for a while that is drawn.
static void begin_swoop(Saucer* s) {
  s->log->swoop_began = true;
  set_state(s, SAUCER_STATE_SWOOP);
  set_field(s, SAUCER_DP_HEADING,
            flags_double(&s->flags, player_way(s, SAUCER_ANYWHERE)));
  set_field(s, SAUCER_DP_TIMER,
            (uint16_t)((random_byte(s) & SAUCER_SWOOP_DRAW) +
                       SAUCER_SWOOP_LEAST));
  set_field(s, SAUCER_DP_SHOTS_LEFT, SAUCER_SHOTS);
}

// `$82:85E9`: open over its quarry.
static void begin_on_target(Saucer* s) {
  set_state(s, SAUCER_STATE_ON_TARGET);
  set_field(s, SAUCER_DP_WOBBLE, 0);
  open_hatch(s);
}

// Has its quarry crossed to the other side of it? The line is 16 past its
// middle, on the side away from the spot.
static bool quarry_crossed(Saucer* s, uint16_t quarry) {
  SaucerLog* log = s->log;
  const uint16_t quarry_x = record_field(s, quarry, ACTOR_X);
  const uint16_t line = field(s, SAUCER_DP_LINE_X);
  log->aim_left = field(s, SAUCER_DP_AIM_X) < position(s).x;
  log->crossed = log->aim_left ? quarry_x >= line : quarry_x < line;
  return log->crossed;
}

// The move a heading makes while hunting. A way with no part across leaves
// the last move's across as the one to try.
static void try_step(Saucer* s, uint16_t heading) {
  SaucerLog* log = s->log;
  const uint16_t at = (uint16_t)(heading << 1);
  const Point me = position(s);
  const uint16_t dx = table_word(s, SAUCER_STEPS, at);
  if (dx != 0) {
    log->across = true;
    log->leftwards = negative(dx);
    set_field(s, SAUCER_DP_FACING, negative(dx) ? 0 : 0xffffu);
    set_field(s, SAUCER_DP_TRY_X, (uint16_t)(dx + me.x));
    set_field(s, SAUCER_DP_TRY_AIM_X,
              (uint16_t)(dx + field(s, SAUCER_DP_AIM_X)));
  }
  set_field(s, SAUCER_DP_TRY_Y,
            (uint16_t)(table_word(s, SAUCER_STEPS + 2, at) + me.y));
}

// `$82:8505`: fly to put the spot on its quarry.
static bool hunt(Saucer* s) {
  SaucerLog* log = s->log;
  uint16_t dist;
  const uint16_t quarry = nearest_actor(s, spot(s), &dist);
  set_field(s, SAUCER_DP_QUARRY, quarry);

  if (dist < SAUCER_ON_IT) {
    PORT_COVER(saucer_on_target);
    log->range = SAUCER_RANGE_ON_IT;
    begin_on_target(s);
    return false;
  }
  if (dist >= SAUCER_FAR) {
    PORT_COVER(saucer_far);
    log->range = SAUCER_RANGE_FAR;
    begin_swoop(s);
    return false;
  }
  log->range = SAUCER_RANGE_MIDDLE;
  if (dist < SAUCER_CLOSE) {
    log->range = SAUCER_RANGE_CLOSE;
    const uint16_t shots = flags_add(&s->flags, field(s, SAUCER_DP_SHOTS_LEFT),
                                     field(s, SAUCER_DP_SHOT_WAIT));
    if (negative(shots)) {
      log->spent = true;
      if (random_byte(s) >= SAUCER_SPENT_SWOOPS) {
        PORT_COVER(saucer_spent_swooped);
        log->spent_swoops = true;
        begin_swoop(s);
        return false;
      }
      PORT_COVER(saucer_spent_opened);
      set_field(s, SAUCER_DP_TIMER, SAUCER_SPENT_OPEN);
      set_field(s, SAUCER_DP_REACH, SAUCER_SPENT_REACH);
      return hang_open(s);
    }
    maybe_shoot(s);
  }

  // Every other pass it looks again: for its quarry's side, and for the way
  // to the nearer player.
  const uint16_t wait = (uint16_t)(field(s, SAUCER_DP_REAIM_WAIT) - 1);
  set_field(s, SAUCER_DP_REAIM_WAIT, wait);
  if (negative(wait)) {
    log->looked = true;
    set_field(s, SAUCER_DP_REAIM_WAIT, 1);
    if (!is_record(quarry)) {
      log->declined = true;
      return false;
    }
    if (quarry_crossed(s, quarry)) {
      PORT_COVER(saucer_changed_sides);
      take_aim(s);
      if (log->declined) return false;
    }
    set_field(s, SAUCER_DP_HEADING,
              (uint16_t)(player_way(s, SAUCER_ANYWHERE) << 1));
  }

  const uint16_t heading = field(s, SAUCER_DP_HEADING);
  log->straight = (heading & 2) != 0;
  if (!log->straight && (frame_count(s) & SAUCER_FRAME_MASK) == 0) {
    PORT_COVER(saucer_held);
    log->held = true;
    return false;
  }
  try_step(s, heading);
  move(s);
  return false;
}

// `$82:85F4`: hold over its quarry, open, until no player is under the spot.
static bool on_target(Saucer* s) {
  SaucerLog* log = s->log;
  if (player_way(s, SAUCER_HOLD_REACH) == 0) {
    PORT_COVER(saucer_lost_them);
    log->lost = true;
    shut_hatch(s);
    if (log->declined) return false;
    take_aim(s);
    return false;
  }
  maybe_shoot(s);
  if ((frame_count(s) & SAUCER_FRAME_MASK) != 0) {
    log->waited = true;
    return false;
  }
  PORT_COVER(saucer_on_target_wobbled);
  try_wobble(s, SAUCER_ON_TARGET_WOBBLE);
  move(s);
  show_hatch(s);
  return true;
}

// `$82:867E`: one pass of a swoop. The two sums run on from the doubling of
// the heading without a `CLC`, so the move down is one more whenever the move
// across carried.
static bool swoop(Saucer* s) {
  SaucerLog* log = s->log;
  const uint16_t left = (uint16_t)(field(s, SAUCER_DP_TIMER) - 1);
  set_field(s, SAUCER_DP_TIMER, left);
  if (negative(left)) {
    PORT_COVER(saucer_swoop_over);
    log->swoop_over = true;
    set_field(s, SAUCER_DP_TIMER, SAUCER_SWOOPED_OPEN);
    set_field(s, SAUCER_DP_REACH, SAUCER_SWOOPED_REACH);
    return hang_open(s);
  }
  const uint16_t at = flags_double(&s->flags, field(s, SAUCER_DP_HEADING));
  const Point me = position(s);
  const uint16_t x =
      flags_adc(&s->flags, table_word(s, SAUCER_SWOOP_STEPS, at), me.x,
                s->flags.c);
  set_field(s, SAUCER_DP_TRY_X, x);
  set_field(s, SAUCER_DP_TRY_Y,
            flags_adc(&s->flags, table_word(s, SAUCER_SWOOP_STEPS + 2, at),
                      me.y, s->flags.c));
  if (move(s)) {
    PORT_COVER(saucer_swoop_stopped);
    log->swoop_stopped = true;
    set_field(s, SAUCER_DP_TIMER, 0);
  }
  return false;
}

// ---------------------------------------------------------------------------
// The lights, and the pass
// ---------------------------------------------------------------------------

// `$82:87E8`: count down to the next shot, say where the picture is drawn,
// and every fifth pass change the lights. Coming back to the first set of
// colours clears the mosaic a flash left.
static void lights(Saucer* s) {
  SaucerLog* log = s->log;
  log->lights = true;
  const uint16_t shot_wait = field(s, SAUCER_DP_SHOT_WAIT);
  if (shot_wait != 0) {
    log->shot_waiting = true;
    set_field(s, SAUCER_DP_SHOT_WAIT, (uint16_t)(shot_wait - 1));
  }
  const Point me = position(s);
  wram_w16(s->w, W_SAUCER_PICTURE_X, (uint16_t)(me.x - SAUCER_PICTURE_DX));
  wram_w16(s->w, W_SAUCER_PICTURE_Y,
           flags_sub(&s->flags, me.y, SAUCER_PICTURE_DY));

  const uint16_t wait = (uint16_t)(field(s, SAUCER_DP_LIGHTS_WAIT) - 1);
  set_field(s, SAUCER_DP_LIGHTS_WAIT, wait);
  if (wait != 0) return;
  PORT_COVER(saucer_lights_changed);
  log->blinked = true;
  set_field(s, SAUCER_DP_LIGHTS_WAIT, SAUCER_LIGHTS_EVERY);
  const uint16_t phase = field(s, SAUCER_DP_LIGHTS_PHASE) ^ SAUCER_LIGHTS_STEP;
  set_field(s, SAUCER_DP_LIGHTS_PHASE, phase);
  set_colours(s, flags_add(&s->flags, phase, SAUCER_COLOURS),
              SAUCER_COLOURS_BANK);
  if (phase == 0) queue_job(s, SAUCER_MOSAIC_OFF_JOB);
}

static SaucerFate end_of_pass(Saucer* s) {
  lights(s);
  s->log->c = s->flags.c;
  s->log->v = s->flags.v;
  if (negative(field(s, SAUCER_DP_HEALTH))) {
    PORT_COVER(saucer_shot_down);
    return SAUCER_ENDS;
  }
  return SAUCER_SLEEPS;
}

bool saucer_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + SAUCER_DP_STATE));
  const uint16_t heading = wram_r16(w, (uint16_t)(page + SAUCER_DP_HEADING));
  const uint16_t phase =
      wram_r16(w, (uint16_t)(page + SAUCER_DP_LIGHTS_PHASE));
  if (heading > SAUCER_MAX_HEADING || (heading & 1) != 0) return false;
  if (phase != 0 && phase != SAUCER_LIGHTS_STEP) return false;
  return state == SAUCER_STATE_HUNT || state == SAUCER_STATE_ON_TARGET ||
         state == SAUCER_STATE_SWOOP || state == SAUCER_STATE_OPEN;
}

SaucerFate saucer_frame(Wram* w, const Rom* rom, uint16_t page,
                        SaucerLog* log) {
  SaucerLog scratch;
  if (log == NULL) log = &scratch;
  *log = (SaucerLog){0};
  Saucer s = {w, rom, page, log, {0}};

  log->state = field(&s, SAUCER_DP_STATE);
  bool showing;
  switch (log->state) {
    case SAUCER_STATE_HUNT:
      PORT_COVER(saucer_hunted);
      showing = hunt(&s);
      break;
    case SAUCER_STATE_ON_TARGET:
      showing = on_target(&s);
      break;
    case SAUCER_STATE_SWOOP:
      PORT_COVER(saucer_swooped);
      showing = swoop(&s);
      break;
    default:
      showing = hang_open(&s);
      break;
  }
  if (log->declined) return SAUCER_SLEEPS;
  if (showing) {
    log->c = s.flags.c;
    log->v = s.flags.v;
    return SAUCER_SLEEPS_SHOWING;
  }
  return end_of_pass(&s);
}

SaucerFate saucer_frame_shown(Wram* w, const Rom* rom, uint16_t page,
                              SaucerLog* log) {
  SaucerLog scratch;
  if (log == NULL) log = &scratch;
  *log = (SaucerLog){0};
  Saucer s = {w, rom, page, log, {0}};
  PORT_COVER(saucer_shown);
  return end_of_pass(&s);
}
