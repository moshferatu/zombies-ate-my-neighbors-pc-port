// $82:E7C7 and $82:E807  the thing that comes at a player -- see
// port/seeker.h.

#include "port/seeker.h"

#include "port/coverage.h"
#include "port/frontend.h"  // the frame count
#include "port/oam.h"  // the display record's fields
#include "port/rng.h"
#include "port/terrain.h"
#include "port/thread.h"

bool seeker_step_supported(uint16_t way) { return way < SEEKER_WAYS; }

void seeker_step(Wram* w, const Rom* rom, PortCpu* c) {
  PORT_COVER(seeker_stepped);
  c->x = (uint16_t)(c->a << 2);
  c->y = wram_r16(w, (uint16_t)(c->d + SEEKER_DP_RECORD));

  set_c(c, false);
  const uint16_t x = adc16(c, rom_word(rom, SEEKER_STEPS + c->x),
                           wram_r16(w, (uint16_t)(c->d + SEEKER_DP_X)));
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_X), x);
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), x);

  set_c(c, false);
  c->a = adc16(c, rom_word(rom, SEEKER_STEPS + c->x + 2),
               wram_r16(w, (uint16_t)(c->d + SEEKER_DP_Y)));
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_Y), c->a);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), c->a);
  c->pc = SEEKER_STEP_RTS_PC;
}

bool seeker_flap(Wram* w, const Rom* rom, PortCpu* c) {
  const uint16_t left =
      (uint16_t)(wram_r16(w, (uint16_t)(c->d + SEEKER_DP_FRAMES)) - 1);
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_FRAMES), left);
  set_nz16(c, left);
  c->pc = SEEKER_FLAP_RTS_PC;
  if ((left & 0x8000u) == 0) {
    PORT_COVER(seeker_flap_waited);
    return false;
  }

  PORT_COVER(seeker_flapped);
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_FRAMES), SEEKER_FLAP_FRAMES);
  const uint16_t picture =
      (uint16_t)(wram_r16(w, (uint16_t)(c->d + SEEKER_DP_PICTURE)) + 1);
  wram_w16(w, (uint16_t)(c->d + SEEKER_DP_PICTURE), picture);
  c->x = (uint16_t)((picture & 1) << 1);
  set_c(c, false);  // the `ASL`
  c->y = wram_r16(w, (uint16_t)(c->d + SEEKER_DP_RECORD));
  c->a = rom_word(rom, SEEKER_PICTURES + c->x);
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), c->a);
  return true;
}

// ---------------------------------------------------------------------------
// $82:EF4F  a frame of the thread
// ---------------------------------------------------------------------------

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  SeekerWork* k;
  bool left;  // in the middle of a state, which goes on after a call
  uint16_t face_return;  // what the state's `JSR $EB13` leaves
  bool after_struck;     // the state goes on from that `JSR`
} Seeker;

static uint16_t field(const Seeker* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->c->d + at));
}

static void set_field(Seeker* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->c->d + at), v);
}

static void ran(Seeker* s, int block) { s->k->blocks[block]++; }

// `LDX $0E : LDY $10 : JSL actor_nearest`. A is how far, X is who.
static void look(Seeker* s) {
  PortCpu* c = s->c;
  static ActorNearestWork unkept;
  ActorNearestWork* work = s->k->looks < SEEKER_MAX_LOOKS
                               ? &s->k->nearest[s->k->looks++]
                               : &unkept;
  c->y = field(s, SEEKER_DP_Y);
  uint16_t dist = 0;
  c->x = actor_nearest_counted(s->w, field(s, SEEKER_DP_X), c->y, &dist, work);
  c->a = dist;
  // Its last subtraction steps a record's address down, and never overflows.
  set_v(c, false);
}

// `LDX $08 : JSL actor_bearing`: which way `whom` is from here.
static void face(Seeker* s, uint16_t whom) {
  PortCpu* c = s->c;
  c->y = whom;
  actor_bearing(s->w, s->rom, field(s, SEEKER_DP_RECORD), whom,
                &s->k->bearing);
  c->a = s->k->bearing.a;
  c->x = s->k->bearing.x;
  set_c(c, s->k->bearing.c);
  s->k->faced = true;
}

// `ASL : TAX : LDA $E89A,X`: the way opposite the one in A.
static void turn_back(Seeker* s) {
  PortCpu* c = s->c;
  c->x = asl16(c, c->a);
  c->a = rom_word(s->rom, SEEKER_BACK_WAYS + c->x);
  set_nz16(c, c->a);
}

static void step(Seeker* s) {
  seeker_step(s->w, s->rom, s->c);
  ran(s, SF_STEP);
}

static void flap(Seeker* s) {
  ran(s, SF_FLAP);
  ran(s, seeker_flap(s->w, s->rom, s->c) ? SF_FLAP_PICTURE : SF_TAKEN);
  ran(s, SF_RTS);
}

// `JSL rng_next`, with the carry the compare before it left.
static void draw(Seeker* s) {
  PortCpu* c = s->c;
  RngResult r;
  rng_next(s->w, flag(c, PORT_P_C), &r);
  c->a = r.a;
  set_c(c, r.c);
  set_v(c, r.v);
  if (s->k->draws < SEEKER_MAX_DRAWS) s->k->draw_overflow[s->k->draws++] = r.v;
}

// `LDY $08 : LDA #$8000 : ORA $0000,Y : STA $0000,Y`: its record drawn.
static void show(Seeker* s) {
  PortCpu* c = s->c;
  c->y = field(s, SEEKER_DP_RECORD);
  c->a = (uint16_t)(wram_r16(s->w, (uint16_t)(c->y + ACTOR_FLAGS)) |
                    ACTOR_DRAW);
  set_nz16(c, c->a);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
}

// Out of the frame at `pc`, a call in the middle of a state: the loop's
// return is left on the stack for the state's `RTS`.
static void leave_state(Seeker* s, uint32_t pc) {
  push16(s->w, s->c, SEEKER_FRAME_RETURN);
  s->c->pc = pc;
  s->left = true;
}

// `$82:E8AC`: begin to circle, from the place opposite the way it faced,
// and the other way round from last time. The stronger it is, the longer
// it sleeps between frames.
static void circle_begin(Seeker* s) {
  PortCpu* c = s->c;
  PORT_COVER(seeker_circle_began);
  set_c(c, false);
  const uint16_t sum = adc16(c, 0x0036, field(s, SEEKER_DP_STRENGTH));
  set_c(c, (sum & 0x0020u) != 0);  // the last of six `LSR`s
  set_field(s, SEEKER_DP_SLEEP, (uint16_t)((sum >> 6) + 1));
  set_field(s, SEEKER_DP_STATE, SEEKER_STATE_CIRCLE_ASK);

  // The way, less one, times twenty: ten places of four bytes an eighth.
  const uint16_t twice =
      asl16(c, (uint16_t)(field(s, SEEKER_DP_WAY) - 1));
  set_field(s, SEEKER_DP_SCRATCH, twice);
  const uint16_t eight = asl16(c, asl16(c, twice));
  set_c(c, false);
  set_field(s, SEEKER_DP_PLACE, asl16(c, adc16(c, eight, twice)));
  c->a = (uint16_t)(0 - field(s, SEEKER_DP_TURN));
  set_nz16(c, c->a);
  set_field(s, SEEKER_DP_TURN, c->a);
  set_field(s, SEEKER_DP_LAPS, 0);
  ran(s, SF_CIRCLE_BEGIN);
}

// `$82:E858`.
static void chase(Seeker* s) {
  PortCpu* c = s->c;
  look(s);
  set_field(s, SEEKER_DP_TARGET, c->x);
  ran(s, SF_CHASE);
  cmp16(c, c->a, SEEKER_TOO_NEAR);
  if (c->a < SEEKER_TOO_NEAR) {
    PORT_COVER(seeker_backed_off);
    ran(s, SF_TAKEN);
    face(s, c->x);
    turn_back(s);
    ran(s, SF_CHASE_BACK);
  } else {
    ran(s, SF_CHASE_MID);
    cmp16(c, c->a, SEEKER_NEAR);
    if (c->a < SEEKER_NEAR) {
      ran(s, SF_TAKEN);
      face(s, field(s, SEEKER_DP_TARGET));
      turn_back(s);
      set_field(s, SEEKER_DP_WAY, c->a);
      ran(s, SF_CHASE_CIRCLE);
      circle_begin(s);
      return;
    }
    PORT_COVER(seeker_came_on);
    face(s, c->x);
    ran(s, SF_CHASE_FAR);
  }
  step(s);
  flap(s);
  ran(s, SF_CHASE_GO);
}

// `$82:EA0C`: it stops circling. Its record gets another picture, and is no
// longer drawn in front of every layer nor sorted ahead of the rest. Next
// frame it lands.
static void leave_circle(Seeker* s) {
  PortCpu* c = s->c;
  PORT_COVER(seeker_circle_left);
  set_field(s, SEEKER_DP_SLEEP, 1);
  set_field(s, SEEKER_DP_STATE, SEEKER_STATE_LAND);
  c->y = field(s, SEEKER_DP_RECORD);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_META), SEEKER_LEAVING_PICTURE);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_META_BANK), SEEKER_PICTURE_BANK);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_FLAGS),
           (uint16_t)(wram_r16(s->w, (uint16_t)(c->y + ACTOR_FLAGS)) &
                      SEEKER_LEAVING_FLAGS));
  c->a = SEEKER_LEAVING_FRAMES;
  set_nz16(c, c->a);
  set_field(s, SEEKER_DP_FRAMES, c->a);
  set_field(s, SEEKER_DP_PICTURE, 0);
  ran(s, SF_CIRCLE_LEAVE);
  ran(s, SF_RTS);
}

// `$82:E8F9`. After a lap it may stop circling.
static bool circle(Seeker* s) {
  PortCpu* c = s->c;
  const uint16_t place = field(s, SEEKER_DP_PLACE);
  const uint16_t target = field(s, SEEKER_DP_TARGET);
  set_c(c, false);
  const uint16_t x = adc16(c, rom_word(s->rom, SEEKER_CIRCLE + place),
                           wram_r16(s->w, (uint16_t)(target + ACTOR_X)));
  set_field(s, SEEKER_DP_X, x);
  set_c(c, false);
  const uint16_t y = adc16(c, rom_word(s->rom, SEEKER_CIRCLE + 2 + place),
                           wram_r16(s->w, (uint16_t)(target + ACTOR_Y)));
  set_field(s, SEEKER_DP_Y, y);
  set_c(c, false);
  uint16_t next = adc16(c, place, field(s, SEEKER_DP_TURN));
  ran(s, SF_CIRCLE);
  if (next & 0x8000u) {
    PORT_COVER(seeker_lapped_back);
    ran(s, SF_TAKEN);
    next = SEEKER_CIRCLE_LAST;
    set_field(s, SEEKER_DP_LAPS, (uint16_t)(field(s, SEEKER_DP_LAPS) + 1));
    ran(s, SF_CIRCLE_UNDER);
  } else {
    ran(s, SF_CIRCLE_CMP);
    cmp16(c, next, SEEKER_CIRCLE_END);
    if (next < SEEKER_CIRCLE_END) {
      PORT_COVER(seeker_circled);
      ran(s, SF_TAKEN);
    } else {
      PORT_COVER(seeker_lapped);
      next = 0;
      set_field(s, SEEKER_DP_LAPS, (uint16_t)(field(s, SEEKER_DP_LAPS) + 1));
      ran(s, SF_CIRCLE_OVER);
    }
  }
  set_field(s, SEEKER_DP_PLACE, next);
  const uint16_t record = field(s, SEEKER_DP_RECORD);
  wram_w16(s->w, (uint16_t)(record + ACTOR_X), x);
  wram_w16(s->w, (uint16_t)(record + ACTOR_Y), y);
  c->y = record;
  c->x = place;
  flap(s);
  ran(s, SF_CIRCLE_PUT);
  if ((wram_r16(s->w, W_FRAME_COUNT) & 3) != 0) {
    ran(s, SF_TAKEN);
  } else {
    PORT_COVER(seeker_circle_asks);
    set_field(s, SEEKER_DP_STATE, SEEKER_STATE_CIRCLE_ASK);
    ran(s, SF_CIRCLE_ASK);
  }

  c->a = field(s, SEEKER_DP_LAPS);
  cmp16(c, c->a, 1);
  ran(s, SF_CIRCLE_LAPS);
  if (c->a == 0) {
    ran(s, SF_RTS);
    return true;
  }
  ran(s, SF_TAKEN);
  draw(s);
  cmp16(c, c->a, SEEKER_SWOOP_ODDS);
  ran(s, SF_CIRCLE_DRAW);
  if (c->a >= SEEKER_SWOOP_ODDS) {
    PORT_COVER(seeker_circle_stayed);
    ran(s, SF_TAKEN);
    ran(s, SF_RTS);
    return true;
  }

  // The draw says leave the circle. It does only from a place on the level,
  // over ground that will do; from any other it circles on.
  BoundsRegs map;
  terrain_out_of_bounds(s->w, x, y, &map);
  s->k->mapped = true;
  s->k->map_exit = map.exit;
  c->a = map.a;
  c->x = x;
  c->y = y;
  set_c(c, map.c);
  ran(s, SF_CIRCLE_MAP);
  if (map.c) {
    PORT_COVER(seeker_circle_off_level);
    ran(s, SF_TAKEN);
    ran(s, SF_RTS);
    return true;
  }
  TerrainRegs* ground = &s->k->ground;
  terrain_footprint_bit12(s->w, x, y, ground);
  s->k->grounded = true;
  c->a = ground->a;
  c->x = ground->x;
  c->y = ground->y;
  set_c(c, ground->blocked);
  set_v(c, ground->v);
  ran(s, SF_CIRCLE_GROUND);
  if (ground->blocked) {
    PORT_COVER(seeker_circle_bad_ground);
    ran(s, SF_TAKEN);
    ran(s, SF_RTS);
    return true;
  }
  leave_circle(s);
  return true;
}

// `$82:E8D9`: is whoever it circles still there, and still the one to
// circle? Then a frame of circling. If not, the ROM's.
static bool circle_ask(Seeker* s) {
  PortCpu* c = s->c;
  const uint16_t target = field(s, SEEKER_DP_TARGET);
  ran(s, SF_ASK);
  if (wram_r16(s->w, (uint16_t)(target + ACTOR_FLAGS)) == 0) return false;
  look(s);
  cmp16(c, c->x, target);
  ran(s, SF_ASK_LOOK);
  if (c->x == target) {
    PORT_COVER(seeker_ask_same);
    ran(s, SF_TAKEN);
  } else {
    cmp16(c, c->a, SEEKER_CIRCLE_LOST);
    if (c->a < SEEKER_CIRCLE_LOST) return false;
    PORT_COVER(seeker_ask_other_far);
    ran(s, SF_ASK_NEAR);
    ran(s, SF_TAKEN);
  }
  set_field(s, SEEKER_DP_STATE, SEEKER_STATE_CIRCLE);
  ran(s, SF_ASK_SET);
  return circle(s);
}

// `$82:EF03`: nothing may touch it while its pictures play, and the frame
// stops at their `JSL`, with the loop's return and, struck in a state's
// `JSR $EB13`, the state's on the stack.
static void struck_pictures(Seeker* s, bool in_face) {
  PortCpu* c = s->c;
  c->a = 0;
  c->y = 0;
  set_nz16(c, 0);
  ran(s, SF_HIDE_HANDLER);
  thread_set_handler(s->w, c);
  s->k->handlers_set++;
  c->a = SEEKER_STRUCK_PICTURES;
  set_nz16(c, c->a);
  ran(s, SF_STRUCK);
  push16(s->w, c, SEEKER_FRAME_RETURN);
  if (in_face) push16(s->w, c, s->face_return);
  c->pc = SEEKER_STRUCK_CALL_PC;
  s->left = true;
}

// `$82:EB50` and `$82:EB5B`: hit, it loses nine of its strength; with
// nobody near enough, it does not.
static void struck(Seeker* s, bool hurt) {
  PortCpu* c = s->c;
  ran(s, SF_TAKEN);
  if (hurt) {
    PORT_COVER(seeker_struck_hit);
    set_c(c, true);
    c->a = sbc16(c, field(s, SEEKER_DP_STRENGTH), SEEKER_HIT_LOSS);
    set_field(s, SEEKER_DP_STRENGTH, c->a);
    ran(s, SF_HIT);
  } else {
    PORT_COVER(seeker_struck_alone);
    ran(s, SF_LOST);
  }
  struck_pictures(s, true);
}

// `$82:ED43`: a dart that cannot go on, drawn.
static void dart_struck(Seeker* s) {
  ran(s, SF_TAKEN);
  show(s);
  ran(s, SF_DART_LOST);
  struck_pictures(s, false);
}

// `$82:EB13`: turn to face whoever is nearest. Hit, or with nobody near
// enough, it is struck instead, and false: the frame has left.
static bool face_nearest(Seeker* s) {
  PortCpu* c = s->c;
  if (s->after_struck) return true;
  ran(s, SF_FACE_CALL);
  ran(s, SF_FACE);
  if (field(s, SEEKER_DP_HURT) & 0x8000u) {
    struck(s, true);
    return false;
  }
  look(s);
  cmp16(c, c->a, SEEKER_TOO_FAR);
  ran(s, SF_FACE_LOOK);
  if (c->a >= SEEKER_TOO_FAR) {
    struck(s, false);
    return false;
  }
  set_field(s, SEEKER_DP_TARGET, c->x);
  face(s, c->x);
  set_field(s, SEEKER_DP_WAY, c->a);
  c->x = asl16(c, c->a);
  c->y = field(s, SEEKER_DP_RECORD);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_META),
           rom_word(s->rom, SEEKER_FACINGS + c->x));
  cmp16(c, c->x, SEEKER_FACES_RIGHT);
  ran(s, SF_FACE_TURN);
  const uint16_t flags = wram_r16(s->w, (uint16_t)(c->y + ACTOR_FLAGS));
  if (c->x < SEEKER_FACES_RIGHT) {
    PORT_COVER(seeker_faced_left);
    c->a = (uint16_t)(flags & ~SEEKER_FLIPPED);
    ran(s, SF_FACE_LEFT);
  } else {
    PORT_COVER(seeker_faced_right);
    ran(s, SF_TAKEN);
    c->a = (uint16_t)(flags | SEEKER_FLIPPED);
    ran(s, SF_FACE_RIGHT);
  }
  set_nz16(c, c->a);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
  ran(s, SF_FACE_END);
  return true;
}

// `$82:EB86`: nothing can touch it while it hides, and it plays its
// going.
static void hide(Seeker* s) {
  PortCpu* c = s->c;
  PORT_COVER(seeker_hid);
  c->a = 0;
  c->y = 0;
  set_nz16(c, 0);
  ran(s, SF_HIDE_HANDLER);
  thread_set_handler(s->w, c);
  s->k->handlers_set++;
  c->y = field(s, SEEKER_DP_RECORD);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_META_BANK), SEEKER_PICTURE_BANK);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), 0);
  c->a = SEEKER_HIDE_PICTURES;
  set_nz16(c, c->a);
  ran(s, SF_HIDE);
  leave_state(s, SEEKER_HIDE_CALL_PC);
}

// `$82:EB64` and `$82:EB7A`: face them, and most frames nothing else. The
// first goes on to fire, with its record drawn. The second hides.
static bool watch(Seeker* s, uint16_t odds, bool hides) {
  PortCpu* c = s->c;
  s->face_return = hides ? SEEKER_WATCH_B_RETURN : SEEKER_WATCH_RETURN;
  if (!face_nearest(s)) return true;
  draw(s);
  cmp16(c, c->a, odds);
  ran(s, SF_WATCH_ON);
  if (c->a < odds) {
    if (hides) {
      hide(s);
      return true;
    }
    PORT_COVER(seeker_went_on);
    set_field(s, SEEKER_DP_STATE, SEEKER_STATE_FIRE);
    show(s);
    ran(s, SF_GO_FIRE);
    return true;
  }
  ran(s, SF_TAKEN);
  ran(s, SF_RTS);
  return true;
}

static bool blink(Seeker* s);

// `$82:ED5C`: neither part of a hop is more than four pixels.
static uint16_t clamped(Seeker* s, uint16_t far) {
  ran(s, SF_CLAMP);
  ran(s, far < SEEKER_HOP ? SF_TAKEN : SF_CLAMP_SET);
  return far < SEEKER_HOP ? far : SEEKER_HOP;
}

// `$82:ED75`: where a hop at whoever it chose lands, at `$0A` and `$0C`, and
// the picture for the way it goes. It goes along the axis they are further
// off on, and down when they are as far both ways.
static void aim(Seeker* s) {
  const uint16_t target = field(s, SEEKER_DP_TARGET);
  const uint16_t record = field(s, SEEKER_DP_RECORD);
  const uint16_t dx = (uint16_t)(wram_r16(s->w, (uint16_t)(target + ACTOR_X)) -
                                 field(s, SEEKER_DP_X));
  const uint16_t dy = (uint16_t)(wram_r16(s->w, (uint16_t)(target + ACTOR_Y)) -
                                 field(s, SEEKER_DP_Y));
  const bool left = (dx & 0x8000u) != 0, up = (dy & 0x8000u) != 0;
  uint16_t across = left ? (uint16_t)(0 - dx) : dx;
  uint16_t down = up ? (uint16_t)(0 - dy) : dy;
  ran(s, SF_AIM_X);
  ran(s, left ? SF_NEGATE : SF_TAKEN);
  ran(s, SF_AIM_Y);
  ran(s, up ? SF_NEGATE : SF_TAKEN);
  ran(s, SF_AIM_CMP);

  uint16_t way;
  if (down < across) {
    PORT_COVER(seeker_hop_across);
    across = clamped(s, across);
    clamped(s, down);
    ran(s, SF_RTS);
    down = 0;
    way = SEEKER_WAY_ACROSS;
    ran(s, SF_AIM_ACROSS);
    if (left) {
      across = (uint16_t)(0 - across);
      way = SEEKER_WAY_BACK;
      ran(s, SF_AIM_BACK);
    } else {
      ran(s, SF_TAKEN);
    }
  } else {
    PORT_COVER(seeker_hop_down);
    ran(s, SF_TAKEN);
    clamped(s, across);
    down = clamped(s, down);
    ran(s, SF_RTS);
    across = 0;
    way = SEEKER_WAY_DOWN;
    ran(s, SF_AIM_DOWN);
    if (up) {
      down = (uint16_t)(0 - down);
      way = SEEKER_WAY_UP;
      ran(s, SF_AIM_UP);
    } else {
      ran(s, SF_TAKEN);
    }
  }
  set_field(s, SEEKER_DP_STEP_X, across);
  set_field(s, SEEKER_DP_STEP_Y, down);
  set_field(s, SEEKER_DP_AHEAD_X,
            (uint16_t)(wram_r16(s->w, (uint16_t)(record + ACTOR_X)) + across));
  set_field(s, SEEKER_DP_AHEAD_Y,
            (uint16_t)(wram_r16(s->w, (uint16_t)(record + ACTOR_Y)) + down));
  wram_w16(s->w, (uint16_t)(record + ACTOR_META),
           rom_word(s->rom, SEEKER_FACINGS + way));
  ran(s, SF_AIM_PUT);
  const uint16_t flags = wram_r16(s->w, (uint16_t)(record + ACTOR_FLAGS));
  if (way < SEEKER_FACES_RIGHT) {
    wram_w16(s->w, (uint16_t)(record + ACTOR_FLAGS),
             (uint16_t)(flags & ~SEEKER_FLIPPED));
    ran(s, SF_FACE_LEFT);
  } else {
    ran(s, SF_TAKEN);
    wram_w16(s->w, (uint16_t)(record + ACTOR_FLAGS),
             (uint16_t)(flags | SEEKER_FLIPPED));
    ran(s, SF_FACE_RIGHT);
  }
  ran(s, SF_FACE_END);
}

// `$82:ECC1`: the dart. Each frame of it that begins a hop asks who is
// nearest. Near enough, or out of hops, it goes back to watching. With
// nobody in reach, or solid ground where the hop lands, the ROM's.
static bool dart(Seeker* s) {
  PortCpu* c = s->c;
  look(s);
  cmp16(c, c->a, SEEKER_DART_NEAR);
  ran(s, SF_DART);
  if (c->a < SEEKER_DART_NEAR) {
    PORT_COVER(seeker_dart_arrived);
    set_field(s, SEEKER_DP_STATE, SEEKER_STATE_WATCH);
    ran(s, SF_DART_NEAR);
    return true;
  }
  ran(s, SF_TAKEN);
  const uint16_t hops = (uint16_t)(field(s, SEEKER_DP_HOPS) - 1);
  set_field(s, SEEKER_DP_HOPS, hops);
  set_field(s, SEEKER_DP_DIST, c->a);
  set_field(s, SEEKER_DP_TARGET, c->x);
  ran(s, SF_DART_COUNT);
  if (hops & 0x8000u) {
    // Things that touch it are told to its handler again.
    PORT_COVER(seeker_dart_spent);
    ran(s, SF_TAKEN);
    c->a = SEEKER_HANDLER;
    c->y = SEEKER_HANDLER_BANK;
    thread_set_handler(s->w, c);
    s->k->handlers_set++;
    show(s);
    set_field(s, SEEKER_DP_STATE, SEEKER_STATE_WATCH);
    ran(s, SF_DART_OVER);
    return true;
  }
  cmp16(c, c->a, SEEKER_TOO_FAR);
  ran(s, SF_DART_FAR);
  if (c->a >= SEEKER_TOO_FAR) {
    PORT_COVER(seeker_dart_lost);
    dart_struck(s);
    return true;
  }

  aim(s);
  TerrainRegs* ground = &s->k->ground;
  terrain_blocked_enemy(s->w, field(s, SEEKER_DP_AHEAD_X),
                        field(s, SEEKER_DP_AHEAD_Y), ground);
  s->k->grounded = true;
  c->a = ground->a;
  c->x = ground->x;
  c->y = ground->y;
  set_c(c, ground->blocked);
  set_v(c, ground->v);
  ran(s, SF_DART_AIM);
  if (ground->blocked) {
    PORT_COVER(seeker_dart_blocked);
    dart_struck(s);
    return true;
  }
  PORT_COVER(seeker_hop_began);
  set_field(s, SEEKER_DP_FRAMES, SEEKER_BLINK_START);
  set_field(s, SEEKER_DP_STATE, SEEKER_STATE_BLINK);
  ran(s, SF_DART_SET);
  return blink(s);
}

// `$82:ECA3`: face them, and most frames nothing else but its record set
// to be drawn. On the rest it begins to dart, for eight hops or up to
// fifteen more.
static bool hold(Seeker* s) {
  PortCpu* c = s->c;
  s->face_return = SEEKER_HOLD_RETURN;
  if (!face_nearest(s)) return true;
  draw(s);
  cmp16(c, c->a, SEEKER_HOLD_ODDS);
  ran(s, SF_WATCH_ON);
  if (c->a < SEEKER_HOLD_ODDS) {
    PORT_COVER(seeker_dart_began);
    draw(s);
    set_c(c, false);
    c->a = adc16(c, (uint16_t)(c->a & 0x000fu), SEEKER_HOPS_LEAST);
    set_field(s, SEEKER_DP_HOPS, c->a);
    set_field(s, SEEKER_DP_STATE, SEEKER_STATE_DART);
    ran(s, SF_HOLD_GO);
    return dart(s);
  }
  PORT_COVER(seeker_held);
  ran(s, SF_TAKEN);
  show(s);
  ran(s, SF_SHOW);
  return true;
}

// A step each way of 24 to 39 pixels, one way or the other by the draw's
// low bit. `ROR` halves the sum and keeps that bit.
static uint16_t place_step(Seeker* s) {
  PortCpu* c = s->c;
  draw(s);
  set_c(c, false);
  const uint16_t sum =
      adc16(c, (uint16_t)(c->a & 0x001fu), SEEKER_PLACE_LEAST);
  const bool back = (sum & 1u) != 0;
  set_c(c, back);
  ran(s, SF_PLACE_DRAW);
  ran(s, back ? SF_NEGATE : SF_TAKEN);
  return back ? (uint16_t)(0 - (sum >> 1)) : (uint16_t)(sum >> 1);
}

// `$82:EC2C`: the place will do. It is drawn there with its own picture,
// and a puff begins a pixel below it.
static void come_back(Seeker* s) {
  PortCpu* c = s->c;
  PORT_COVER(seeker_came_back);
  ran(s, SF_PLACE_MAP);
  c->y = field(s, SEEKER_DP_RECORD);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_META), SEEKER_BACK_PICTURE);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_FLAGS),
           (uint16_t)(wram_r16(s->w, (uint16_t)(c->y + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
  const uint16_t x = field(s, SEEKER_DP_STEP_X);
  set_field(s, SEEKER_DP_X, x);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_X), x);
  set_field(s, SEEKER_DP_SPAWN_X, x);
  const uint16_t y = field(s, SEEKER_DP_STEP_Y);
  set_field(s, SEEKER_DP_Y, y);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_Y), y);
  set_field(s, SEEKER_DP_SPAWN_Y, (uint16_t)(y + 1));
  c->a = SEEKER_PUFF_THREAD;
  c->y = SEEKER_BANK;
  set_nz16(c, c->y);
  ran(s, SF_BACK);
  leave_state(s, SEEKER_BACK_CALL_PC);
}

// `$82:EBCF`: hidden, it tries a place near whoever is nearest. Ground that
// will not do, or a place off the level, and it tries again next frame.
// Nobody in reach is the ROM's.
static bool place(Seeker* s) {
  PortCpu* c = s->c;
  look(s);
  cmp16(c, c->a, SEEKER_TOO_FAR);
  set_field(s, SEEKER_DP_TARGET, c->x);
  if (c->a >= SEEKER_TOO_FAR) return false;
  ran(s, SF_PLACE);
  ran(s, SF_TAKEN);
  const uint16_t target = c->x;

  uint16_t step = place_step(s);
  set_c(c, false);
  const uint16_t x =
      adc16(c, step, wram_r16(s->w, (uint16_t)(target + ACTOR_X)));
  set_field(s, SEEKER_DP_STEP_X, x);
  step = place_step(s);
  ran(s, SF_PLACE_X);
  set_c(c, false);
  const uint16_t y =
      adc16(c, step, wram_r16(s->w, (uint16_t)(target + ACTOR_Y)));
  set_field(s, SEEKER_DP_STEP_Y, y);

  TerrainRegs* ground = &s->k->ground;
  terrain_footprint_bit12(s->w, x, y, ground);
  s->k->grounded = true;
  c->a = ground->a;
  c->x = ground->x;
  c->y = ground->y;
  set_c(c, ground->blocked);
  set_v(c, ground->v);
  ran(s, SF_PLACE_Y);
  if (ground->blocked) {
    PORT_COVER(seeker_place_bad_ground);
    ran(s, SF_TAKEN);
    ran(s, SF_RTS);
    return true;
  }
  BoundsRegs map;
  terrain_out_of_bounds(s->w, x, y, &map);
  s->k->mapped = true;
  s->k->map_exit = map.exit;
  c->a = map.a;
  c->x = x;
  c->y = y;
  set_c(c, map.c);
  if (!map.c) {
    come_back(s);
    return true;
  }
  PORT_COVER(seeker_place_off_level);
  ran(s, SF_PLACE_MAP);
  ran(s, SF_TAKEN);
  ran(s, SF_RTS);
  return true;
}

// `$82:ECF7`: its record at one of two places, and drawn or not by the
// game's frame. After the third its place is the further one, and it darts
// on from there.
static bool blink(Seeker* s) {
  PortCpu* c = s->c;
  set_c(c, true);
  c->a = sbc16(c, field(s, SEEKER_DP_FRAMES), SEEKER_BLINK_FRAMES);
  ran(s, SF_BLINK);
  if (c->a & 0x8000u) {
    PORT_COVER(seeker_hopped);
    ran(s, SF_TAKEN);
    set_field(s, SEEKER_DP_X, field(s, SEEKER_DP_AHEAD_X));
    set_field(s, SEEKER_DP_Y, field(s, SEEKER_DP_AHEAD_Y));
    ran(s, SF_DART_ON);
    return dart(s);
  }
  set_field(s, SEEKER_DP_FRAMES, c->a);
  c->x = (uint16_t)(c->a & 4);
  c->y = field(s, SEEKER_DP_RECORD);
  const uint16_t flags = wram_r16(s->w, (uint16_t)(c->y + ACTOR_FLAGS));
  ran(s, SF_BLINK_WHERE);
  if ((wram_r16(s->w, W_SCHED_TICK) & 1) == 0) {
    PORT_COVER(seeker_blinked_on);
    c->a = (uint16_t)(flags | ACTOR_DRAW);
    ran(s, SF_BLINK_ON);
  } else {
    PORT_COVER(seeker_blinked_off);
    ran(s, SF_TAKEN);
    c->a = (uint16_t)(flags & ~ACTOR_DRAW);
    ran(s, SF_BLINK_OFF);
  }
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_X),
           field(s, (uint16_t)(SEEKER_DP_AHEAD_X + c->x)));
  c->a = field(s, (uint16_t)(SEEKER_DP_AHEAD_Y + c->x));
  set_nz16(c, c->a);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_Y), c->a);
  ran(s, SF_BLINK_PUT);
  return true;
}

// `$82:EE0E`: the pictures of firing the way it faces, which sleep.
static bool fire(Seeker* s) {
  PortCpu* c = s->c;
  const uint16_t way = field(s, SEEKER_DP_WAY);
  if (way >= SEEKER_WAYS) return false;
  PORT_COVER(seeker_fired);
  c->x = asl16(c, way);
  set_field(s, SEEKER_DP_TRACKER_WAY, c->x);
  c->a = rom_word(s->rom, SEEKER_FIRE_PICTURES + c->x);
  set_nz16(c, c->a);
  ran(s, SF_FIRE);
  leave_state(s, SEEKER_FIRE_CALL_PC);
  return true;
}

// `$82:EAC5`: is the place in X and Y on the screen, or no more than four
// pixels left of it? Carry clear if so, as each `CMP` leaves it.
static bool on_screen(Seeker* s, uint16_t x, uint16_t y) {
  PortCpu* c = s->c;
  const uint16_t left = wram_r16(s->w, W_SEEKER_CAMERA_X);
  const uint16_t top = wram_r16(s->w, W_SEEKER_CAMERA_Y);
  set_c(c, false);
  c->a = adc16(c, x, SEEKER_SCREEN_LEFT);
  cmp16(c, c->a, left);
  ran(s, SF_EDGE_LEFT);
  bool in = c->a >= left;
  if (in) {
    set_c(c, false);
    c->a = adc16(c, left, SEEKER_SCREEN_WIDE);
    cmp16(c, c->a, x);
    ran(s, SF_EDGE_RIGHT);
    in = c->a >= x;
  }
  if (in) {
    c->a = y;
    cmp16(c, c->a, top);
    ran(s, SF_EDGE_TOP);
    in = c->a >= top;
  }
  if (in) {
    set_c(c, false);
    c->a = adc16(c, top, SEEKER_SCREEN_HIGH);
    cmp16(c, c->a, y);
    ran(s, SF_EDGE_BOTTOM);
    in = c->a >= y;
  }
  if (in) {
    set_c(c, false);
    ran(s, SF_EDGE_IN);
  } else {
    ran(s, SF_TAKEN);
    set_c(c, true);
    ran(s, SF_EDGE_OUT);
  }
  return in;
}

// `$82:EA8E`: its strength spent, it flies off: its picture, four pixels
// the way it drew, and its loop ends once it is off the level or off the
// screen. False for a way past the table's.
static bool flee(Seeker* s) {
  PortCpu* c = s->c;
  const uint16_t way = field(s, SEEKER_DP_WAY);
  if (way >= SEEKER_FLEE_WAYS) return false;
  PORT_COVER(seeker_fled);
  ran(s, SF_FLEE_CALL);
  flap(s);
  c->x = (uint16_t)(way << 2);
  c->y = field(s, SEEKER_DP_RECORD);
  set_c(c, false);
  const uint16_t x = adc16(c, rom_word(s->rom, SEEKER_FLEE_STEPS + c->x),
                           field(s, SEEKER_DP_X));
  set_field(s, SEEKER_DP_X, x);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_X), x);
  set_c(c, false);
  const uint16_t y = adc16(c, rom_word(s->rom, SEEKER_FLEE_STEPS + 2 + c->x),
                           field(s, SEEKER_DP_Y));
  set_field(s, SEEKER_DP_Y, y);
  wram_w16(s->w, (uint16_t)(c->y + ACTOR_Y), y);
  ran(s, SF_FLEE_MOVE);

  BoundsRegs map;
  terrain_out_of_bounds(s->w, x, y, &map);
  s->k->mapped = true;
  s->k->map_exit = map.exit;
  c->a = map.a;
  c->x = x;
  c->y = y;
  set_c(c, map.c);
  ran(s, SF_FLEE_TEST);
  bool gone = map.c;
  if (!gone) {
    ran(s, SF_FLEE_ON);
    gone = !on_screen(s, x, y);
    c->x = x;
    c->y = y;
    if (!gone) {
      ran(s, SF_FLEE_STAYS);
      return true;
    }
    ran(s, SF_FLEE_TEST);
  }
  PORT_COVER(seeker_gone);
  ran(s, SF_TAKEN);
  const uint16_t ended = (uint16_t)(field(s, SEEKER_DP_ENDED) - 1);
  set_field(s, SEEKER_DP_ENDED, ended);
  set_nz16(c, ended);
  ran(s, SF_FLEE_GONE);
  return true;
}

// The loop after a state's `RTS`: its end, or the sleep.
static bool frame_end(Seeker* s) {
  PortCpu* c = s->c;
  if (s->left) return true;
  c->a = field(s, SEEKER_DP_ENDED);
  set_nz16(c, c->a);
  ran(s, SF_TAIL);
  if (c->a != 0) {
    PORT_COVER(seeker_frame_ended);
    c->pc = SEEKER_FRAME_ENDED_PC;
    return true;
  }
  ran(s, SF_TAKEN);
  c->a = field(s, SEEKER_DP_SLEEP);
  set_nz16(c, c->a);
  ran(s, SF_AGAIN);
  c->pc = SEEKER_FRAME_SLEEP_PC;
  return true;
}

bool seeker_frame(Wram* w, const Rom* rom, PortCpu* c, SeekerWork* k) {
  Seeker s = {w, rom, c, k, false};
  ran(&s, SF_HEAD);
  switch (field(&s, SEEKER_DP_STATE)) {
    case SEEKER_STATE_CHASE:
      chase(&s);
      break;
    case SEEKER_STATE_CIRCLE_ASK:
      if (!circle_ask(&s)) return false;
      break;
    case SEEKER_STATE_CIRCLE:
      if (!circle(&s)) return false;
      break;
    case SEEKER_STATE_WATCH:
      PORT_COVER(seeker_watched);
      if (!watch(&s, SEEKER_WATCH_ODDS, false)) return false;
      break;
    case SEEKER_STATE_WATCH_B:
      PORT_COVER(seeker_watched_b);
      if (!watch(&s, SEEKER_WATCH_B_ODDS, true)) return false;
      break;
    case SEEKER_STATE_HOLD:
      if (!hold(&s)) return false;
      break;
    case SEEKER_STATE_BLINK:
      if (!blink(&s)) return false;
      break;
    case SEEKER_STATE_DART:
      if (!dart(&s)) return false;
      break;
    case SEEKER_STATE_PLACE:
      if (!place(&s)) return false;
      break;
    case SEEKER_STATE_FIRE:
      if (!fire(&s)) return false;
      break;
    case SEEKER_STATE_FLEE:
      if (!flee(&s)) return false;
      break;
    case SEEKER_STATE_LAND:
      // A is what the dispatch's `DEC` left.
      PORT_COVER(seeker_landing);
      c->a = SEEKER_STATE_LAND - 1;
      set_nz16(c, c->a);
      leave_state(&s, SEEKER_LAND_PC);
      break;
    default:
      return false;
  }
  return frame_end(&s);
}

bool seeker_struck_shown(Wram* w, const Rom* rom, PortCpu* c, SeekerWork* k) {
  Seeker s = {w, rom, c, k, false};
  s.after_struck = true;
  // `LDY $08 : LDA #$0008 : ORA $0000,Y : STA $0000,Y : JMP $E827`.
  c->y = field(&s, SEEKER_DP_RECORD);
  c->a = (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) |
                    ACTOR_PRIORITY_TOP);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
  ran(&s, SF_SHOWN);
  // `$82:E827`: next frame it comes at whoever is nearest, and things that
  // touch it are told to nobody.
  set_field(&s, SEEKER_DP_SLEEP, 1);
  set_field(&s, SEEKER_DP_STATE, SEEKER_STATE_CHASE);
  set_field(&s, SEEKER_DP_FRAMES, SEEKER_FLAP_FRAMES);
  set_field(&s, SEEKER_DP_PICTURE,
            (uint16_t)(field(&s, SEEKER_DP_PICTURE) & 0x0001u));
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) |
                      ACTOR_PRIORITY_TOP));
  c->a = 0;
  c->y = 0;
  set_nz16(c, 0);
  thread_set_handler(w, c);
  k->handlers_set++;
  ran(&s, SF_RESET);
  c->a = field(&s, SEEKER_DP_STRENGTH);
  set_nz16(c, c->a);
  ran(&s, SF_RESET_ALIVE);
  if (c->a & 0x8000u) {
    // `JMP $EA7E`: its strength is spent, and it flies off from the next
    // frame, a way from one to eight by a draw. Its `RTS` goes where the
    // other would have.
    PORT_COVER(seeker_spent);
    set_field(&s, SEEKER_DP_STATE, SEEKER_STATE_FLEE);
    ran(&s, SF_SPENT);
    draw(&s);
    c->a = (uint16_t)((c->a & SEEKER_FLEE_WAY_MASK) + 1);
    set_nz16(c, c->a);
    set_field(&s, SEEKER_DP_WAY, c->a);
    ran(&s, SF_SPENT_WAY);
  } else {
    PORT_COVER(seeker_struck_shown);
    ran(&s, SF_TAKEN);
    ran(&s, SF_RTS);
  }
  // Back in the state, after its `JSR $EB13`, with the loop's return under;
  // or, struck darting, in the loop.
  const uint16_t back = pull16(w, c);
  if (back == SEEKER_FRAME_RETURN) return frame_end(&s);
  if (pull16(w, c) != SEEKER_FRAME_RETURN) return false;
  switch (back) {
    case SEEKER_WATCH_RETURN:
      if (!watch(&s, SEEKER_WATCH_ODDS, false)) return false;
      break;
    case SEEKER_WATCH_B_RETURN:
      if (!watch(&s, SEEKER_WATCH_B_ODDS, true)) return false;
      break;
    case SEEKER_HOLD_RETURN:
      if (!hold(&s)) return false;
      break;
    default:
      return false;
  }
  return frame_end(&s);
}
