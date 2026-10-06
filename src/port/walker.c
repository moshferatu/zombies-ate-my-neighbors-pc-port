// $81:D723  a thing that walks until something is in its way -- see
// port/walker.h.

#include "port/walker.h"

#include "port/coverage.h"
#include "port/rng.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  WalkerWork* k;
} Walker;

static uint16_t field(const Walker* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->c->d + at));
}

static void set_field(Walker* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->c->d + at), v);
}

static void ran(Walker* s, int block) { s->k->blocks[block]++; }

// `LDA #... : STA $1A : RTS`
static void next_state(Walker* s, uint16_t state) {
  s->c->a = state;
  set_field(s, WALKER_DP_STATE, state);
  ran(s, WK_STATE);
}

// `LDX : LDY : JSL terrain_blocked_enemy : BCS`. True when ground stops it.
static bool ground_stops(Walker* s, uint16_t x, uint16_t y) {
  PortCpu* c = s->c;
  TerrainRegs* r = &s->k->ground[s->k->grounds++];
  terrain_blocked_enemy(s->w, x, y, r);
  c->a = r->a;
  c->x = r->x;
  c->y = r->y;
  set_c(c, r->blocked);
  s->k->overflow_unknown = true;
  ran(s, WK_GROUND);
  if (r->blocked) ran(s, WK_TAKEN);
  return r->blocked;
}

// `LDA $08 : LDX : LDY : JSL actor_at_point : BCS`. True when a thing is
// there.
static bool body_there(Walker* s, uint16_t x, uint16_t y) {
  PortCpu* c = s->c;
  AtPointRegs r;
  actor_at_point_counted(s->w, field(s, WALKER_DP_RECORD), x, y, &r,
                         &s->k->body[s->k->bodies++]);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_c(c, r.found);
  if (r.v_set) {
    set_v(c, r.v);
    s->k->overflow_unknown = false;
  }
  ran(s, WK_BODY);
  if (r.found) ran(s, WK_TAKEN);
  return r.found;
}

// A step the way `way` says, from where it is, to `$12` and `$14`.
static void aim(Walker* s, uint16_t way) {
  PortCpu* c = s->c;
  c->x = asl16(c, way);
  set_c(c, false);
  set_field(s, WALKER_DP_TRY_X,
            adc16(c, rom_word(s->rom, WALKER_STEPS + c->x),
                  field(s, WALKER_DP_X)));
  set_c(c, false);
  c->a = adc16(c, rom_word(s->rom, WALKER_STEPS + 2 + c->x),
               field(s, WALKER_DP_Y));
  set_field(s, WALKER_DP_TRY_Y, c->a);
  s->k->overflow_unknown = false;
}

// `$81:D5A0`: who is about?
static void look(Walker* s) {
  PortCpu* c = s->c;
  c->y = field(s, WALKER_DP_Y);
  uint16_t dist = 0;
  c->x = actor_nearest_counted(s->w, field(s, WALKER_DP_X), c->y, &dist,
                               &s->k->nearest);
  c->a = dist;
  s->k->looked = true;
  s->k->overflow_unknown = true;
  cmp16(c, dist, WALKER_NEAR);
  ran(s, WK_LOOK);
  if (dist < WALKER_NEAR) {
    PORT_COVER(walker_saw_someone);
    ran(s, WK_TAKEN);
    ran(s, WK_JMP);
    next_state(s, WALKER_STATE_AT);
    return;
  }
  cmp16(c, dist, WALKER_FAR);
  ran(s, WK_LOOK_FAR);
  if (dist < WALKER_FAR) {
    ran(s, WK_TAKEN);
    ran(s, WK_RTS);
    return;
  }
  player_bearing(s->w, s->rom, WALKER_FAR, field(s, WALKER_DP_X),
                 field(s, WALKER_DP_Y), &s->k->players);
  s->k->asked_players = true;
  c->a = s->k->players.a;
  c->x = c->a;
  c->y = s->k->players.y;
  set_c(c, s->k->players.c);
  ran(s, WK_LOOK_PLAYER);
  if (c->a != 0) {
    PORT_COVER(walker_alone_but_watched);
    ran(s, WK_TAKEN);
  } else {
    PORT_COVER(walker_left_alone);
    set_field(s, WALKER_DP_ENDED, (uint16_t)(field(s, WALKER_DP_ENDED) - 1));
    ran(s, WK_LOOK_NONE);
  }
  ran(s, WK_RTS);
}

// `$81:D517`: may it be at `$12` and `$14`? Carry says not.
static bool stopped(Walker* s) {
  const uint16_t x = field(s, WALKER_DP_TRY_X);
  const uint16_t y = field(s, WALKER_DP_TRY_Y);
  const bool is = ground_stops(s, x, y) || body_there(s, x, y);
  ran(s, WK_RTS);
  return is;
}

// The end of both walks: the step, or a quarter turn and along.
static void step_or_turn(Walker* s) {
  PortCpu* c = s->c;
  ran(s, WK_JSR);
  const bool is = stopped(s);
  ran(s, WK_BCS);
  if (!is) {
    PORT_COVER(walker_stepped);
    set_field(s, WALKER_DP_X, field(s, WALKER_DP_TRY_X));
    c->a = field(s, WALKER_DP_TRY_Y);
    set_field(s, WALKER_DP_Y, c->a);
    ran(s, WK_STEP);
    return;
  }
  PORT_COVER(walker_turned);
  ran(s, WK_TAKEN);
  ran(s, WK_JMP);
  set_c(c, false);
  const uint16_t turned = adc16(c, (uint16_t)(field(s, WALKER_DP_WAY) - 2), 4);
  s->k->overflow_unknown = false;
  set_field(s, WALKER_DP_WAY, (uint16_t)((turned & 0x000fu) + 2));
  ran(s, WK_TURN);
  next_state(s, WALKER_STATE_ALONG);
}

// `$81:D52F`: the quarter turn back, when that way is clear.
static void try_side(Walker* s) {
  PortCpu* c = s->c;
  set_c(c, true);
  const uint16_t back = sbc16(c, (uint16_t)(field(s, WALKER_DP_WAY) - 2), 4);
  const uint16_t side = (uint16_t)((back & 0x000fu) + 2);
  set_field(s, WALKER_DP_SIDE, side);
  aim(s, side);
  ran(s, WK_SIDE);
  const uint16_t x = field(s, WALKER_DP_TRY_X);
  const uint16_t y = field(s, WALKER_DP_TRY_Y);
  if (!ground_stops(s, x, y) && !body_there(s, x, y)) {
    PORT_COVER(walker_took_the_side);
    c->a = side;
    set_field(s, WALKER_DP_WAY, side);
    ran(s, WK_TAKE);
  }
  ran(s, WK_RTS);
}

static void walk(Walker* s, bool along) {
  ran(s, WK_JSR);
  look(s);
  if (along) {
    ran(s, WK_JSR);
    try_side(s);
  }
  aim(s, field(s, WALKER_DP_WAY));
  ran(s, WK_AIM);
  step_or_turn(s);
}

// `$81:D66C`: a step at them, across and down each on its own.
static void go_at(Walker* s) {
  PortCpu* c = s->c;
  ActorBearingRegs* r = &s->k->bearing[s->k->bearings++];
  actor_bearing(s->w, s->rom, field(s, WALKER_DP_RECORD),
                field(s, WALKER_DP_WHOM), r);
  set_c(c, r->c);
  const uint16_t way = asl16(c, r->a);
  set_field(s, WALKER_DP_WAY, way);
  aim(s, way);
  ran(s, WK_AT_GO);
  ran(s, WK_JSR);

  const uint16_t x = field(s, WALKER_DP_TRY_X);
  const uint16_t y = field(s, WALKER_DP_TRY_Y);
  if (!ground_stops(s, x, field(s, WALKER_DP_Y)) &&
      !body_there(s, x, field(s, WALKER_DP_Y))) {
    PORT_COVER(walker_went_across);
    c->a = x;
    set_field(s, WALKER_DP_X, x);
    ran(s, WK_TAKE);
  }
  if (!ground_stops(s, field(s, WALKER_DP_X), y) &&
      !body_there(s, field(s, WALKER_DP_X), y)) {
    PORT_COVER(walker_went_down);
    c->a = y;
    set_field(s, WALKER_DP_Y, y);
    ran(s, WK_TAKE);
  }
  ran(s, WK_RTS);
  ran(s, WK_RTS);
}

static void at(Walker* s) {
  PortCpu* c = s->c;
  c->y = field(s, WALKER_DP_Y);
  uint16_t dist = 0;
  c->x = actor_nearest_counted(s->w, field(s, WALKER_DP_X), c->y, &dist,
                               &s->k->nearest);
  s->k->looked = true;
  s->k->overflow_unknown = true;
  set_field(s, WALKER_DP_WHOM, c->x);
  cmp16(c, dist, WALKER_LOST);
  ran(s, WK_AT);
  if (dist >= WALKER_LOST) {
    PORT_COVER(walker_lost_them);
    ran(s, WK_JMP);
    next_state(s, WALKER_STATE_WALK);
    return;
  }
  ran(s, WK_TAKEN);

  RngResult draw;
  rng_next(s->w, flag(c, PORT_P_C), &draw);
  s->k->drew = true;
  s->k->drew_overflow = draw.v;
  set_c(c, draw.c);
  set_v(c, draw.v);
  s->k->overflow_unknown = false;
  ran(s, WK_AT_DRAW);
  if (draw.a & 2u) {
    PORT_COVER(walker_one_step);
    ran(s, WK_TAKEN);
  } else {
    PORT_COVER(walker_two_steps);
    ran(s, WK_JSR);
    go_at(s);
  }
  go_at(s);
}

// `$81:D76E`
static void show(Walker* s) {
  PortCpu* c = s->c;
  const uint16_t record = field(s, WALKER_DP_RECORD);
  const uint16_t left = (uint16_t)(field(s, WALKER_DP_PICTURE_LEFT) - 1);
  set_field(s, WALKER_DP_PICTURE_LEFT, left);
  ran(s, WK_SHOW);
  if (!(left & 0x8000u)) {
    ran(s, WK_TAKEN);
  } else {
    set_field(s, WALKER_DP_PICTURE_LEFT, WALKER_PICTURE_FRAMES);
    const uint16_t which = field(s, WALKER_DP_PICTURE);
    c->x = asl16(c, (uint16_t)(asl16(c, field(s, WALKER_DP_WAY)) | which));
    wram_w16(s->w, (uint16_t)(record + ACTOR_META),
             rom_word(s->rom, WALKER_PICTURES + c->x));
    uint16_t flags = wram_r16(s->w, (uint16_t)(record + ACTOR_FLAGS));
    cmp16(c, c->x, WALKER_FLIPPED_FROM);
    ran(s, WK_PICTURE);
    if (c->x >= WALKER_FLIPPED_FROM) {
      PORT_COVER(walker_faces_left);
      ran(s, WK_TAKEN);
      flags |= 0x0002u;
      ran(s, WK_FACE_LEFT);
    } else {
      PORT_COVER(walker_faces_right);
      flags &= 0xfffdu;
      ran(s, WK_FACE_RIGHT);
    }
    wram_w16(s->w, (uint16_t)(record + ACTOR_FLAGS), flags);
    set_field(s, WALKER_DP_PICTURE, (uint16_t)((which + 1) & 3u));
    ran(s, WK_PICTURE_END);
  }
  c->y = record;
  wram_w16(s->w, (uint16_t)(record + ACTOR_X), field(s, WALKER_DP_X));
  wram_w16(s->w, (uint16_t)(record + ACTOR_Y), field(s, WALKER_DP_Y));
  ran(s, WK_PLACE);
}

bool walker_frame(Wram* w, const Rom* rom, PortCpu* c, WalkerWork* k) {
  Walker s = {w, rom, c, k};
  const uint16_t state = field(&s, WALKER_DP_STATE);
  ran(&s, WK_HEAD);
  switch (state) {
    case WALKER_STATE_WALK:
      walk(&s, false);
      break;
    case WALKER_STATE_ALONG:
      walk(&s, true);
      break;
    case WALKER_STATE_AT:
      at(&s);
      break;
    default:
      return false;
  }

  ran(&s, WK_JSR);
  show(&s);
  c->a = field(&s, WALKER_DP_ENDED);
  set_nz16(c, c->a);
  ran(&s, WK_TAIL);
  if (c->a != 0) {
    PORT_COVER(walker_ended);
    c->pc = WALKER_ENDED_PC;
    return true;
  }
  ran(&s, WK_TAKEN);
  set_field(&s, WALKER_DP_SCORED, 0);
  c->a = WALKER_SLEEP_FRAMES;
  set_nz16(c, c->a);
  ran(&s, WK_AGAIN);
  c->pc = WALKER_SLEEP_PC;
  return true;
}
