// $81:CCE8  the slimes' frame -- see port/slime.h.

#include "port/slime.h"

#include <stddef.h>

#include "port/collide.h"  // ThreadCallResult
#include "port/coverage.h"
#include "port/flags.h"
#include "port/rng.h"
#include "port/thread.h"

typedef struct {
  uint16_t x, y;
} Point;

// The draw's bands: under the first it attacks, under the second it turns to
// face whoever is nearest, under the third it attacks.
#define SLIME_DRAW_ATTACK_LOW 0x19
#define SLIME_DRAW_FACE 0x50
#define SLIME_DRAW_ATTACK 0x78
// The attack's own draw has to come in under this...
#define SLIME_DRAW_STRIKE 0x23
// ...and this word under that. It is page zero's `$06`, which is nothing of
// the slime's: scratch that whoever ran last left there.
#define SLIME_STRIKE_WORD 0x0006u
#define SLIME_STRIKE_WORD_LIMIT 0x0010
// How far it looks for a player before it gives up and leaves.
#define SLIME_REACH 0x0140
// A lunge is this many pictures.
#define SLIME_PHASES 5
// The hits it takes after a flash.
#define SLIME_HITS_AFTER_FLASH 4
// The id its touch is told with.
#define SLIME_TOUCH_ID 0x0003
#define SLIME_MIRROR 0x0002
#define SLIME_QUARTER_TURN 4
// The widest direction the tables are read with: 2 after a turn from 16.
#define SLIME_DIRECTION_MAX 17

// `$81:C913`: the lunge for each direction, across and down, at twice the
// direction.
#define SLIME_STEPS 0xc913u
// `$81:C99C`: the box around the body for each direction, as left, top,
// right and bottom from the record, at four times the direction.
#define SLIME_BOXES 0xc99cu
// `$81:CB76`: the way to face for a diagonal bearing, the wider gap's first.
#define SLIME_DIAGONALS 0xcb76u
// `$81:CD84`: the pictures. A direction's five are together, and those at
// `SLIME_FIRST_MIRRORED` and after are drawn mirrored.
#define SLIME_PICTURES 0xcd84u
#define SLIME_FIRST_MIRRORED 0x3c

typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  bool carry;     // the thread's, as it woke
  SlimeLog* log;  // never NULL here
} Slime;

static uint16_t field(const Slime* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->page + at));
}

static void set_field(Slime* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->page + at), v);
}

static uint16_t record_field(const Slime* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->record + at));
}

static void set_record_field(Slime* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->record + at), v);
}

static uint16_t table_word(const Slime* s, uint16_t table, uint16_t index) {
  return rom_word(s->rom, ((uint32_t)SLIME_BANK << 16) +
                              (uint16_t)(table + index));
}

static Point position(const Slime* s) {
  return (Point){field(s, SLIME_DP_X), field(s, SLIME_DP_Y)};
}

static uint16_t distance(uint16_t a, uint16_t b, int* negated) {
  const uint16_t d = (uint16_t)(a - b);
  if (!(d & 0x8000u)) return d;
  (*negated)++;
  return (uint16_t)(0u - d);
}

// ---------------------------------------------------------------------------
// Turning
// ---------------------------------------------------------------------------

static uint16_t turned(uint16_t direction, int quarters) {
  return (uint16_t)(
      ((direction - 2 + quarters * SLIME_QUARTER_TURN) & 0x000f) + 2);
}

// `$81:CA39`: a quarter clockwise, and feel along from there.
static void turn(Slime* s) {
  set_field(s, SLIME_DP_DIRECTION, turned(field(s, SLIME_DP_DIRECTION), 1));
  set_field(s, SLIME_DP_STATE, SLIME_STATE_FEEL);
}

// `$81:CAA6`: think for the rest of this lunge, and then do what the state
// was going to.
static void think_first(Slime* s) {
  set_field(s, SLIME_DP_NEXT_STATE, field(s, SLIME_DP_STATE));
  set_field(s, SLIME_DP_STATE, SLIME_STATE_THINK);
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

// Where a lunge along `direction` would end. It is written down as the step
// being tried.
static Point lunge_end(Slime* s, uint16_t direction) {
  const uint16_t at = (uint16_t)(direction << 1);
  const Point from = position(s);
  const Point to = {(uint16_t)(from.x + table_word(s, SLIME_STEPS, at)),
                    (uint16_t)(from.y + table_word(s, SLIME_STEPS + 2, at))};
  set_field(s, SLIME_DP_TO_X, to.x);
  set_field(s, SLIME_DP_TO_Y, to.y);
  return to;
}

static bool ground_is_solid(Slime* s, Point p, SlimeProbe* probe) {
  probe->asked = true;
  terrain_blocked_enemy(s->w, p.x, p.y, &probe->ground);
  return probe->ground.blocked;
}

static bool someone_at(Slime* s, Point p, SlimeProbe* probe) {
  AtPointRegs r;
  AtPointWork work;
  actor_at_point_counted(s->w, s->record, p.x, p.y, &r, &work);
  for (int i = 0; i < AT_POINT_BLOCK_COUNT; i++)
    s->log->at_point.blocks[i] += work.blocks[i];
  probe->someone = r.found;
  return r.found;
}

static void move_to(Slime* s, Point p) {
  set_field(s, SLIME_DP_X, p.x);
  set_field(s, SLIME_DP_Y, p.y);
}

// The step every state but feeling ends on. Solid ground turns it at once.
// Someone in the way turns it too, but it thinks before it feels along.
static void lunge(Slime* s) {
  SlimeProbe* probe = &s->log->probe[0];
  const Point to = lunge_end(s, field(s, SLIME_DP_DIRECTION));
  if (ground_is_solid(s, to, probe)) {
    PORT_COVER(slime_met_ground);
    turn(s);
    return;
  }
  if (someone_at(s, to, probe)) {
    PORT_COVER(slime_met_someone);
    turn(s);
  } else {
    move_to(s, to);
  }
  think_first(s);
}

// ---------------------------------------------------------------------------
// The state bodies
// ---------------------------------------------------------------------------

// `$81:C9FA`: straight on.
static void crawl(Slime* s) {
  PORT_COVER(slime_crawled);
  lunge(s);
}

// `$81:CA51`: along whatever turned it. The turn back is taken if that way
// is clear, and then a lunge the way it faces, or another turn.
static void feel(Slime* s) {
  PORT_COVER(slime_felt);
  const uint16_t other = turned(field(s, SLIME_DP_DIRECTION), -1);
  set_field(s, SLIME_DP_OTHER_WAY, other);
  const Point back = lunge_end(s, other);
  if (!ground_is_solid(s, back, &s->log->probe[0]) &&
      !someone_at(s, back, &s->log->probe[0])) {
    PORT_COVER(slime_turned_back);
    s->log->took_other_way = true;
    set_field(s, SLIME_DP_DIRECTION, other);
  }

  const Point to = lunge_end(s, field(s, SLIME_DP_DIRECTION));
  if (!ground_is_solid(s, to, &s->log->probe[1]) &&
      !someone_at(s, to, &s->log->probe[1])) {
    move_to(s, to);
  } else {
    turn(s);
  }
  think_first(s);
}

// `$81:CAFB`: towards whoever it found, and a lunge that way.
static void face(Slime* s) {
  PORT_COVER(slime_faced);
  s->log->faced = true;
  const uint16_t target = field(s, SLIME_DP_TARGET);
  actor_bearing(s->w, s->rom, s->record, target, &s->log->bearing);
  uint16_t bearing = s->log->bearing.a;
  set_field(s, SLIME_DP_BEARING, bearing);
  if (!(bearing & 1)) {
    PORT_COVER(slime_faced_diagonal);
    s->log->diagonal = true;
    const Point me = position(s);
    const uint16_t across =
        distance(wram_r16(s->w, (uint16_t)(target + ACTOR_X)), me.x,
                 &s->log->gaps_negated);
    set_field(s, SLIME_DP_TO_X, across);
    const uint16_t down =
        distance(wram_r16(s->w, (uint16_t)(target + ACTOR_Y)), me.y,
                 &s->log->gaps_negated);
    set_field(s, SLIME_DP_TO_Y, down);
    const uint16_t at = (uint16_t)((bearing << 1) | (down >= across ? 1 : 0));
    bearing = table_word(s, SLIME_DIAGONALS, (uint16_t)(at << 1));
    set_field(s, SLIME_DP_BEARING, bearing);
  }
  set_field(s, SLIME_DP_DIRECTION, (uint16_t)(bearing << 1));
  lunge(s);
}

// `$81:C9E4`: one of the four ways, by a draw, and straight on from there.
static void set_off_at_random(Slime* s, bool carry) {
  RngResult draw;
  rng_next(s->w, carry, &draw);
  s->log->draws_overflowed[1] = draw.v;
  set_field(s, SLIME_DP_DIRECTION,
            (uint16_t)(((draw.a & 3) * SLIME_QUARTER_TURN) + 2));
  set_field(s, SLIME_DP_STATE, SLIME_STATE_CRAWL);
}

// `$81:CBA0`: an attack, if the draw allows one. The attack itself is the
// ROM's. Either comparison that refuses one leaves carry set for the next
// draw.
static void weigh_attack(Slime* s) {
  s->log->attack_weighed = true;
  RngResult draw;
  rng_next(s->w, s->carry, &draw);
  s->log->draws_overflowed[0] = draw.v;
  if (draw.a < SLIME_DRAW_STRIKE) {
    s->log->attack_wanted = true;
    if (wram_r16(s->w, SLIME_STRIKE_WORD) < SLIME_STRIKE_WORD_LIMIT) {
      PORT_COVER(slime_attacked);
      s->log->declined = true;
      return;
    }
  }
  PORT_COVER(slime_set_off);
  set_off_at_random(s, true);
}

// `$81:CC2F`: hit, and waiting. When the flash ends it is what it was, with
// its hits counted from the top.
static void flash(Slime* s) {
  const uint16_t left = (uint16_t)(field(s, SLIME_DP_FLASH_LEFT) - 1);
  set_field(s, SLIME_DP_FLASH_LEFT, left);
  if (left != 0) return;
  PORT_COVER(slime_flash_ended);
  s->log->flash_ended = true;
  set_field(s, SLIME_DP_STATE, field(s, SLIME_DP_FLASH_RESUMES));
  set_field(s, SLIME_DP_HITS_LEFT, SLIME_HITS_AFTER_FLASH);
  set_record_field(s, ACTOR_FLAGS,
                   (uint16_t)(record_field(s, ACTOR_FLAGS) & ~ACTOR_ATTR_SET));
  set_record_field(s, ACTOR_ATTR, 0);
}

static void run_state(Slime* s, uint16_t state);

// `$81:CAB0`: what next? Decided again on every pass of the lunge, and acted
// on when the lunge is over.
static void think(Slime* s) {
  if (field(s, SLIME_DP_PHASE) == 0) {
    s->log->thought = SLIME_THOUGHT_RESUMED;
    const uint16_t next = field(s, SLIME_DP_NEXT_STATE);
    set_field(s, SLIME_DP_STATE, next);
    run_state(s, next);
    return;
  }

  const Point me = position(s);
  uint16_t dist = 0;
  set_field(s, SLIME_DP_TARGET,
            actor_nearest_counted(s->w, me.x, me.y, &dist, &s->log->nearest));
  RngResult draw;
  rng_next(s->w, false, &draw);  // `actor_nearest` leaves carry clear
  s->log->drew_overflow = draw.v;

  if (draw.a < SLIME_DRAW_ATTACK_LOW ||
      (draw.a >= SLIME_DRAW_FACE && draw.a < SLIME_DRAW_ATTACK)) {
    PORT_COVER(slime_chose_attack);
    s->log->thought = draw.a < SLIME_DRAW_ATTACK_LOW ? SLIME_THOUGHT_ATTACK_LOW
                                                     : SLIME_THOUGHT_ATTACK;
    set_field(s, SLIME_DP_STATE, SLIME_STATE_ATTACK);
    think_first(s);
  } else if (draw.a < SLIME_DRAW_FACE) {
    PORT_COVER(slime_chose_face);
    s->log->thought = SLIME_THOUGHT_FACE;
    set_field(s, SLIME_DP_STATE, SLIME_STATE_FACE);
    think_first(s);
  } else {
    player_bearing(s->w, s->rom, SLIME_REACH, me.x, me.y, &s->log->players);
    if (s->log->players.a != 0) {
      s->log->thought = SLIME_THOUGHT_CARRY_ON;
    } else {
      PORT_COVER(slime_left);
      s->log->thought = SLIME_THOUGHT_LEAVE;
      set_field(s, SLIME_DP_FATE, (uint16_t)(field(s, SLIME_DP_FATE) - 1));
    }
  }
}

static void run_state(Slime* s, uint16_t state) {
  s->log->state = state;
  switch (state) {
    case SLIME_STATE_CRAWL: crawl(s); break;
    case SLIME_STATE_FEEL: feel(s); break;
    case SLIME_STATE_THINK: think(s); break;
    case SLIME_STATE_FACE: face(s); break;
    case SLIME_STATE_ATTACK: weigh_attack(s); break;
    case SLIME_STATE_FLASH: flash(s); break;
    default: break;  // `slime_frame_supported` keeps the rest out
  }
}

// ---------------------------------------------------------------------------
// The picture and the touch
// ---------------------------------------------------------------------------

// `$81:C94E`: tell everything inside the box around the body. The box is
// measured from the record when a lunge begins.
static void touch(Slime* s) {
  s->log->touched = true;
  bool carry = false;
  if (field(s, SLIME_DP_PHASE) == 0) {
    s->log->measured = true;
    const uint16_t at = (uint16_t)(field(s, SLIME_DP_DIRECTION) << 2);
    const uint16_t x = record_field(s, ACTOR_X);
    const uint16_t y = record_field(s, ACTOR_Y);
    set_field(s, SLIME_DP_BOX_LEFT,
              (uint16_t)(x + table_word(s, SLIME_BOXES, at)));
    set_field(s, SLIME_DP_BOX_RIGHT,
              (uint16_t)(x + table_word(s, SLIME_BOXES + 4, at)));
    set_field(s, SLIME_DP_BOX_TOP,
              (uint16_t)(y + table_word(s, SLIME_BOXES + 2, at)));
    const uint32_t bottom = (uint32_t)y + table_word(s, SLIME_BOXES + 6, at);
    set_field(s, SLIME_DP_BOX_BOTTOM, (uint16_t)bottom);
    carry = bottom > 0xffffu;
  }
  wram_w16(s->w, NOTIFY_BOX_DP_X0, field(s, SLIME_DP_BOX_LEFT));
  wram_w16(s->w, NOTIFY_BOX_DP_X1, field(s, SLIME_DP_BOX_RIGHT));
  wram_w16(s->w, NOTIFY_BOX_DP_Y0, field(s, SLIME_DP_BOX_TOP));
  wram_w16(s->w, NOTIFY_BOX_DP_Y1, field(s, SLIME_DP_BOX_BOTTOM));
  wram_w16(s->w, NOTIFY_BOX_DP_ID, SLIME_TOUCH_ID);

  ThreadCallResult tail = {.c = carry};
  ActorNotifyRegs r;
  if (!actor_notify_box_counted(s->w, s->rom, SLIME_TOUCH_ID, carry, &tail, &r,
                                &s->log->touch))
    s->log->declined = true;
}

// `$81:CD33`: the touch, and the next of the lunge's five pictures. After the
// fifth the record moves up to where the thread already is.
static void show(Slime* s) {
  if (field(s, SLIME_DP_FLASH_LEFT) != 0) {
    s->log->flashing = true;
    return;
  }
  // Not on its first pass, which has shown nothing to touch with.
  const uint16_t touches = (uint16_t)(field(s, SLIME_DP_TOUCHES) - 1);
  set_field(s, SLIME_DP_TOUCHES, touches);
  if (!(touches & 0x8000u)) touch(s);
  set_field(s, SLIME_DP_TOUCHES, 1);

  const uint16_t direction = field(s, SLIME_DP_DIRECTION);
  const uint16_t phase = field(s, SLIME_DP_PHASE);
  s->log->lunge_began = phase == 0;
  const uint16_t picture =
      (uint16_t)(((((direction << 1) | phase) << 1)) + direction);
  set_record_field(s, ACTOR_META, table_word(s, SLIME_PICTURES, picture));
  const uint16_t flags = record_field(s, ACTOR_FLAGS);
  const bool mirrored = picture >= SLIME_FIRST_MIRRORED;
  s->log->mirrored = mirrored;
  set_record_field(s, ACTOR_FLAGS,
                   mirrored ? (uint16_t)(flags | SLIME_MIRROR)
                            : (uint16_t)(flags & ~SLIME_MIRROR));

  uint16_t next = (uint16_t)(phase + 1);
  const bool ended = next >= SLIME_PHASES;
  if (ended) {
    PORT_COVER(slime_lunge_ended);
    s->log->lunge_ended = true;
    set_record_field(s, ACTOR_X, field(s, SLIME_DP_X));
    set_record_field(s, ACTOR_Y, field(s, SLIME_DP_Y));
    next = 0;
  }
  set_field(s, SLIME_DP_PHASE, next);

  // The last arithmetic was the picture's sum, which cannot overflow, and the
  // comparison of the phase.
  s->log->flags_set = true;
  s->log->c = ended;
  s->log->v = false;
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------

static bool state_supported(uint16_t state) {
  return state == SLIME_STATE_CRAWL || state == SLIME_STATE_FEEL ||
         state == SLIME_STATE_FACE || state == SLIME_STATE_ATTACK;
}

bool slime_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + SLIME_DP_STATE));
  const uint16_t next = wram_r16(w, (uint16_t)(page + SLIME_DP_NEXT_STATE));
  const uint16_t phase = wram_r16(w, (uint16_t)(page + SLIME_DP_PHASE));
  const uint16_t direction =
      wram_r16(w, (uint16_t)(page + SLIME_DP_DIRECTION));
  const bool flashing =
      wram_r16(w, (uint16_t)(page + SLIME_DP_FLASH_LEFT)) != 0;
  if (direction > SLIME_DIRECTION_MAX || phase >= SLIME_PHASES) return false;
  // A flash is its own state, and nothing else runs while one counts down.
  if (flashing != (state == SLIME_STATE_FLASH)) return false;
  if (flashing) return true;
  if (state == SLIME_STATE_THINK)
    // Thinking gives way to the next state only when the lunge is over.
    return phase != 0 || state_supported(next);
  return state_supported(state);
}

bool slime_frame(Wram* w, const Rom* rom, uint16_t page, bool carry,
                 SlimeLog* log) {
  SlimeLog scratch = {0};
  Slime s = {w, rom, page, wram_r16(w, (uint16_t)(page + SLIME_DP_RECORD)),
             carry, log ? log : &scratch};
  run_state(&s, field(&s, SLIME_DP_STATE));
  if (s.log->declined) return true;
  show(&s);
  if (field(&s, SLIME_DP_FATE) != 0) return false;
  set_field(&s, SLIME_DP_HIT_BY, 0);
  return true;
}

bool slime_frame_end_supported(const Wram* w, uint16_t page) {
  return wram_r16(w, (uint16_t)(page + SLIME_DP_DIRECTION)) <=
             SLIME_DIRECTION_MAX &&
         wram_r16(w, (uint16_t)(page + SLIME_DP_PHASE)) < SLIME_PHASES;
}

bool slime_frame_end(Wram* w, const Rom* rom, uint16_t page, SlimeLog* log) {
  SlimeLog scratch = {0};
  Slime s = {w, rom, page, wram_r16(w, (uint16_t)(page + SLIME_DP_RECORD)),
             false, log ? log : &scratch};
  PORT_COVER(slime_frame_ended_for_the_rom);
  show(&s);
  if (s.log->declined) return true;
  if (field(&s, SLIME_DP_FATE) != 0) return false;
  set_field(&s, SLIME_DP_HIT_BY, 0);
  return true;
}

// ---------------------------------------------------------------------------
// The glob
// ---------------------------------------------------------------------------

bool slime_glob_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_STATE));
  return state == SLIME_GLOB_STATE_RISE || state == SLIME_GLOB_STATE_FALL;
}

bool slime_glob_frame(Wram* w, uint16_t page, SlimeGlobLog* log) {
  SlimeGlobLog scratch;
  if (!log) log = &scratch;
  PortFlags flags = {false, false, false, false};
  const uint16_t record = wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_RECORD));
  const uint16_t speed_at = (uint16_t)(page + SLIME_GLOB_DP_SPEED);
  const uint16_t height_at = (uint16_t)(record + ACTOR_Z);
  const uint16_t speed = wram_r16(w, speed_at);
  const uint16_t height = wram_r16(w, height_at);

  if (wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_STATE)) ==
      SLIME_GLOB_STATE_RISE) {
    // `$81:CE88`: up by the speed, which then drops by one. When it has run
    // out the glob is at the top, and is moved to above where it will land.
    wram_w16(w, height_at, flags_add(&flags, height, speed));
    wram_w16(w, speed_at, (uint16_t)(speed - 1));
    log->pass = SLIME_GLOB_ROSE;
    if ((uint16_t)(speed - 1) & 0x8000u) {
      PORT_COVER(slime_glob_turned);
      log->pass = SLIME_GLOB_TURNED;
      wram_w16(w, (uint16_t)(record + ACTOR_X),
               wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_AIM_X)));
      wram_w16(w, (uint16_t)(record + ACTOR_Y),
               wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_AIM_Y)));
      wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_STATE),
               SLIME_GLOB_STATE_FALL);
    }
  } else {
    // `$81:CEAD`: the speed goes on dropping, below zero now, and the glob
    // comes down by it until it is on the ground.
    const uint16_t faster = (uint16_t)(speed - 1);
    wram_w16(w, speed_at, faster);
    if (height == 0) {
      PORT_COVER(slime_glob_landed);
      log->pass = SLIME_GLOB_LANDED;
      const uint16_t landed_at = (uint16_t)(page + SLIME_GLOB_DP_LANDED);
      wram_w16(w, landed_at, (uint16_t)(wram_r16(w, landed_at) - 1));
    } else {
      uint16_t lower = flags_add(&flags, height, faster);
      log->pass = SLIME_GLOB_FELL;
      if (lower & 0x8000u) {
        log->pass = SLIME_GLOB_FELL_TO_GROUND;
        lower = 0;
      }
      wram_w16(w, height_at, lower);
    }
  }

  log->c = flags.c;
  log->v = flags.v;
  log->flags_set = flags.c_set;
  return wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_LANDED)) == 0;
}

// ---------------------------------------------------------------------------
// The attack, between its sleeps
// ---------------------------------------------------------------------------

// `$81:CBC0`: the glob is a thread of its own, started with where the slime
// is and whom it found. Then the second list of pictures, which is the
// ROM's to show.
void slime_attack_throw(Wram* w, const Rom* rom, PortCpu* c,
                        SlimeAttackWork* k) {
  const uint16_t page = c->d;
  PORT_COVER(slime_threw);
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_ARG_X),
           wram_r16(w, (uint16_t)(page + SLIME_DP_X)));
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_ARG_Y),
           wram_r16(w, (uint16_t)(page + SLIME_DP_Y)));
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_ARG_TARGET),
           wram_r16(w, (uint16_t)(page + SLIME_DP_TARGET)));
  k->slot = thread_spawn(w, rom, SLIME_GLOB_THREAD, SLIME_BANK, page);
  // `thread_spawn` leaves the slot in X, or `$FFFE` with none free, and in
  // Y the last of the page it copied. Carry and overflow it does not touch.
  c->x = k->slot < 0 ? 0xfffe : (uint16_t)k->slot;
  c->y = k->slot < 0 ? SLIME_BANK : (THREAD_SPAWN_ARGS - 1) * 2;
  c->a = SLIME_THROW_PICTURES;
  set_nz16(c, c->a);
  c->pc = SLIME_THROW_PICTURES_PC;
}

// `$81:CBDD`: the attack is over. The handler that takes its hits, which
// the attack took away, is put back, and it sets off a random way.
void slime_attack_rise(Wram* w, PortCpu* c, SlimeAttackWork* k) {
  PORT_COVER(slime_rose);
  c->a = SLIME_HIT_HANDLER;
  c->y = SLIME_BANK;
  thread_set_handler(w, c);

  RngResult draw;
  rng_next(w, flag(c, PORT_P_C), &draw);
  k->drew_overflow[0] = draw.v;
  set_v(c, draw.v);
  const uint16_t direction =
      (uint16_t)(asl16(c, asl16(c, (uint16_t)(draw.a & 3))) + 2);
  wram_w16(w, (uint16_t)(c->d + SLIME_DP_DIRECTION), direction);
  wram_w16(w, (uint16_t)(c->d + SLIME_DP_STATE), SLIME_STATE_CRAWL);
  c->a = SLIME_STATE_CRAWL;
  set_nz16(c, c->a);
  c->pc = SLIME_RISE_RTS_PC;
}

// ---------------------------------------------------------------------------
// The glob, before its first pass
// ---------------------------------------------------------------------------

// `$81:CECC`: a record, put where the slime is and fifteen up.
void slime_glob_dress(Wram* w, PortCpu* c, SlimeAttackWork* k) {
  const uint16_t page = c->d;
  SlotAllocRegs r;
  actor_slot_alloc(w, c->db, &r);
  if (r.c) {  // none free, and the ROM goes on with what is no record
    k->declined = true;
    return;
  }
  PORT_COVER(slime_glob_dressed);
  const uint16_t record = r.a;
  k->record = record;
  // The search steps down a record at a time with an `SBC`, which leaves
  // overflow clear. With the last record free it runs none.
  if (record != ACTOR_SLOT_LAST) set_v(c, false);
  set_c(c, r.c);
  c->x = r.x;
  c->y = record;
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_RECORD), record);
  wram_w16(w, (uint16_t)(record + ACTOR_X),
           wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_ARG_X)));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), SLIME_GLOB_HEIGHT);
  wram_w16(w, (uint16_t)(record + ACTOR_Y),
           wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_ARG_Y)));
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_TARGET),
           wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_ARG_TARGET)));
  wram_w16(w, (uint16_t)(record + ACTOR_META), SLIME_GLOB_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), SLIME_GLOB_PICTURE_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), SLIME_GLOB_ID);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      SLIME_GLOB_FLAGS));
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_SPEED), SLIME_GLOB_FIRST_SPEED);
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_LANDED), 0);
  c->a = SLIME_GLOB_FIRST_SPEED;
  set_nz16(c, c->a);
  c->pc = SLIME_GLOB_DRESS_RTS_PC;
}

// A draw of 0 to 31 less 16, from the word at `place`. The second sum has
// no `CLC`, so it takes the first's carry.
//
// The word is read after the draw. A slime that found nobody hands the glob
// a target that is no record, and `$001E` is one it hands: six bytes on
// from there are the generator's own two.
static uint16_t glob_near(Wram* w, PortCpu* c, bool* overflow,
                          uint16_t place) {
  RngResult draw;
  rng_next(w, flag(c, PORT_P_C), &draw);
  *overflow = draw.v;
  set_c(c, false);
  const uint16_t off = adc16(c, (uint16_t)(draw.a & SLIME_GLOB_AIM_MASK),
                             (uint16_t)(0u - SLIME_GLOB_AIM_HALF));
  return adc16(c, off, wram_r16(w, place));
}

// `$81:CF1D`: where it comes down, and the state it begins in.
void slime_glob_aim(Wram* w, PortCpu* c, SlimeAttackWork* k) {
  const uint16_t page = c->d;
  PORT_COVER(slime_glob_aimed);
  c->y = wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_TARGET));
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_AIM_X),
           glob_near(w, c, &k->drew_overflow[0],
                     (uint16_t)(c->y + ACTOR_X)));
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_AIM_Y),
           glob_near(w, c, &k->drew_overflow[1],
                     (uint16_t)(c->y + ACTOR_Y)));
  wram_w16(w, (uint16_t)(page + SLIME_GLOB_DP_STATE), SLIME_GLOB_STATE_RISE);
  c->a = SLIME_GLOB_YIELD_TICKS;
  set_nz16(c, c->a);
  c->pc = SLIME_GLOB_YIELD_PC;
}

// ---------------------------------------------------------------------------
// The glob, landed
// ---------------------------------------------------------------------------

// `$81:CE40`: the box it tells of, forty-eight across and thirty-two down
// about where it came down.
void slime_glob_splash(Wram* w, PortCpu* c) {
  PORT_COVER(slime_glob_splashed);
  const uint16_t page = c->d;
  set_c(c, true);
  const uint16_t left = sbc16(
      c, wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_AIM_X)),
      SLIME_SPLASH_HALF_WIDTH);
  wram_w16(w, NOTIFY_BOX_DP_X0, left);
  set_c(c, false);
  wram_w16(w, NOTIFY_BOX_DP_X1, adc16(c, left, 2 * SLIME_SPLASH_HALF_WIDTH));
  set_c(c, true);
  const uint16_t top = sbc16(
      c, wram_r16(w, (uint16_t)(page + SLIME_GLOB_DP_AIM_Y)),
      SLIME_SPLASH_HALF_HEIGHT);
  wram_w16(w, NOTIFY_BOX_DP_Y0, top);
  set_c(c, false);
  wram_w16(w, NOTIFY_BOX_DP_Y1, adc16(c, top, 2 * SLIME_SPLASH_HALF_HEIGHT));
  c->a = SLIME_SPLASH_ID;
  set_nz16(c, c->a);
  wram_w16(w, NOTIFY_BOX_DP_ID, c->a);
  c->pc = SLIME_GLOB_SPLASH_TELL_PC;
}
