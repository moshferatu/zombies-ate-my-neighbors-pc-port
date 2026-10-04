// $81:8EA8  the clones' frame -- see port/clone.h.

#include "port/clone.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/player.h"  // W_JOY_DIR
#include "port/rng.h"
#include "port/terrain.h"

typedef struct {
  uint16_t x, y;
} Point;

// How far it looks for a player to come for.
#define CLONE_REACH 0x00f0
// A picture lasts this many frames.
#define CLONE_PICTURE_FRAMES 7
// Quarter pixels, so a position is shifted down by this to get pixels.
#define CLONE_FINE_SHIFT 2
// The first of the three directions that face left, drawn mirrored.
#define CLONE_FACES_LEFT_FROM 6
#define CLONE_MIRROR 0x0002
// `$81:8E1D`: the step for each direction, in quarter pixels, across and down.
#define CLONE_STEPS 0x8e1du

// One clone's frame, and the carry and overflow the ROM would leave.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  CloneLog* log;  // never NULL here
  PortFlags flags;
} Clone;

static uint16_t field(const Clone* c, uint16_t at) {
  return wram_r16(c->w, (uint16_t)(c->page + at));
}

static void set_field(Clone* c, uint16_t at, uint16_t v) {
  wram_w16(c->w, (uint16_t)(c->page + at), v);
}

static uint16_t record_field(const Clone* c, uint16_t at) {
  return wram_r16(c->w, (uint16_t)(c->record + at));
}

static void set_record_field(Clone* c, uint16_t at, uint16_t v) {
  wram_w16(c->w, (uint16_t)(c->record + at), v);
}

static uint16_t table_word(const Clone* c, uint16_t table, uint16_t index) {
  return rom_word(c->rom, ((uint32_t)CLONE_BANK << 16) + table + index);
}

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

// May it stand at `p`? Solid ground or anyone standing there says no. Both
// tests leave carry, and whichever ran last an overflow.
static bool can_stand_at(Clone* c, Point p, CloneProbe* probe) {
  TerrainRegs ground;
  terrain_blocked_enemy(c->w, p.x, p.y, &ground);
  flags_carry(&c->flags, ground.blocked);
  flags_overflow(&c->flags, ground.v);
  probe->ground = ground.blocked;
  probe->tiles = ground.probes;
  probe->someone = false;
  if (ground.blocked) return false;

  AtPointRegs r;
  AtPointWork work;
  actor_at_point_counted(c->w, c->record, p.x, p.y, &r, &work);
  for (int i = 0; i < AT_POINT_BLOCK_COUNT; i++)
    c->log->at_point.blocks[i] += work.blocks[i];
  flags_carry(&c->flags, r.found);
  if (r.v_set) flags_overflow(&c->flags, r.v);
  probe->someone = r.found;
  return !r.found;
}

// ---------------------------------------------------------------------------
// The frame's pieces
// ---------------------------------------------------------------------------

// `$81:8E41`: every seventh frame, the next of the four pictures for the way
// it was going. The direction is last frame's: this runs before it is chosen.
static void animate(Clone* c) {
  const uint16_t timer = (uint16_t)(field(c, CLONE_DP_PICTURE_TIMER) - 1);
  set_field(c, CLONE_DP_PICTURE_TIMER, timer);
  if (timer != 0) return;
  PORT_COVER(clone_new_picture);
  c->log->new_picture = true;
  set_field(c, CLONE_DP_PICTURE_TIMER, CLONE_PICTURE_FRAMES);
  const uint16_t cycle = (uint16_t)((field(c, CLONE_DP_CYCLE) + 1) & 3);
  set_field(c, CLONE_DP_CYCLE, cycle);
  const uint16_t picture =
      (uint16_t)((field(c, CLONE_DP_DIRECTION) << 2) | cycle);
  set_record_field(c, ACTOR_META,
                   table_word(c, field(c, CLONE_DP_PICTURES),
                              flags_double(&c->flags, picture)));
}

// Which way the nearer player is, or 0 with neither near enough. It looks
// from where the clone is in pixels, which it writes down first.
static uint16_t towards_player(Clone* c) {
  const Point me = {field(c, CLONE_DP_FINE_X) >> CLONE_FINE_SHIFT,
                    field(c, CLONE_DP_FINE_Y) >> CLONE_FINE_SHIFT};
  set_field(c, CLONE_DP_X, me.x);
  set_field(c, CLONE_DP_Y, me.y);
  PlayerPickRegs* r = &c->log->players;
  player_bearing(c->w, c->rom, CLONE_REACH, me.x, me.y, r);
  flags_carry(&c->flags, r->c);
  flags_overflow_unknown(&c->flags);
  return r->a;
}

// The way its player's pad is held, as that player's own thread latched it.
static uint16_t as_player_moves(Clone* c) {
  const uint16_t held =
      wram_r16(c->w, (uint16_t)(W_JOY_DIR + field(c, CLONE_DP_PLAYER)));
  flags_carry(&c->flags, held & 1);  // the `LSR` that halves it
  return held >> 1;
}

// `$81:8DA1`: three quarters of a pixel along `direction`, each axis on its
// own, then the record moved to wherever that left it and mirrored if it
// faces left.
static void step(Clone* c, uint16_t direction) {
  const uint16_t at = (uint16_t)(direction << 2);
  const uint16_t fine_x = flags_add(&c->flags, table_word(c, CLONE_STEPS, at),
                                    field(c, CLONE_DP_FINE_X));
  set_field(c, CLONE_DP_TO_FINE_X, fine_x);
  const Point to_x = {fine_x >> CLONE_FINE_SHIFT, 0};
  set_field(c, CLONE_DP_TO_X, to_x.x);
  const uint16_t fine_y =
      flags_add(&c->flags, table_word(c, CLONE_STEPS + 2, at),
                field(c, CLONE_DP_FINE_Y));
  set_field(c, CLONE_DP_TO_FINE_Y, fine_y);
  const uint16_t to_y = fine_y >> CLONE_FINE_SHIFT;
  set_field(c, CLONE_DP_TO_Y, to_y);

  const Point across = {to_x.x, field(c, CLONE_DP_Y)};
  if (can_stand_at(c, across, &c->log->probe[0])) {
    set_field(c, CLONE_DP_FINE_X, fine_x);
    set_field(c, CLONE_DP_X, across.x);
  }
  const Point down = {field(c, CLONE_DP_X), to_y};
  if (can_stand_at(c, down, &c->log->probe[1])) {
    set_field(c, CLONE_DP_FINE_Y, fine_y);
    set_field(c, CLONE_DP_Y, down.y);
  }

  set_record_field(c, ACTOR_X, field(c, CLONE_DP_X));
  set_record_field(c, ACTOR_Y, field(c, CLONE_DP_Y));
  const uint16_t flags = record_field(c, ACTOR_FLAGS);
  const bool mirrored =
      flags_at_least(&c->flags, direction, CLONE_FACES_LEFT_FROM);
  c->log->mirrored = mirrored;
  set_record_field(c, ACTOR_FLAGS,
                   mirrored ? (uint16_t)(flags | CLONE_MIRROR)
                            : (uint16_t)(flags & ~CLONE_MIRROR));
}

// The mode ran out: the other one, for as long as the generator says.
static void change_mode(Clone* c) {
  PORT_COVER(clone_changed_mode);
  c->log->mode_changed = true;
  set_field(c, CLONE_DP_COPYING, (field(c, CLONE_DP_COPYING) & 1) ^ 1);
  RngResult r;
  rng_next(c->w, c->flags.c, &r);
  flags_carry(&c->flags, r.c);
  flags_overflow(&c->flags, r.v);
  c->log->drew_overflow = r.v;
  set_field(c, CLONE_DP_MODE_LEFT, r.a);
}

static bool frame(Clone* c) {
  if (field(c, CLONE_DP_LEAVE) != 0) {
    c->log->told_to_leave = true;
    return false;
  }
  animate(c);

  uint16_t direction;
  if (field(c, CLONE_DP_COPYING) != 0) {
    PORT_COVER(clone_copied);
    c->log->copying = true;
    direction = as_player_moves(c);
  } else {
    PORT_COVER(clone_chased);
    direction = towards_player(c);
  }
  set_field(c, CLONE_DP_DIRECTION, direction);
  step(c, direction);
  // Chasing nobody, it leaves, having stepped nowhere first.
  if (!c->log->copying && direction == 0) {
    PORT_COVER(clone_left);
    c->log->nobody = true;
    return false;
  }

  const uint16_t left = (uint16_t)(field(c, CLONE_DP_MODE_LEFT) - 1);
  set_field(c, CLONE_DP_MODE_LEFT, left);
  if (left == 0) change_mode(c);
  return true;
}

bool clone_frame(Wram* w, const Rom* rom, uint16_t page, CloneLog* log) {
  CloneLog scratch = {0};
  Clone c = {w, rom, page, wram_r16(w, (uint16_t)(page + CLONE_DP_RECORD)),
             log ? log : &scratch, {false, false, false, false}};
  const bool stays = frame(&c);
  c.log->c = c.flags.c;
  c.log->v = c.flags.v;
  c.log->c_set = c.flags.c_set;
  c.log->v_set = c.flags.v_set;
  return stays;
}
