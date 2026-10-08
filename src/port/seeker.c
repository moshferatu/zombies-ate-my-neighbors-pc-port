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
// frame its state is the one at `$82:EA35`, which is the ROM's.
static void leave_circle(Seeker* s) {
  PortCpu* c = s->c;
  PORT_COVER(seeker_circle_left);
  set_field(s, SEEKER_DP_SLEEP, 1);
  set_field(s, SEEKER_DP_STATE, SEEKER_STATE_EA35);
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

// `$82:EB13`: turn to face whoever is nearest. With nobody near enough, or
// when it has been hit, the ROM's.
static bool face_nearest(Seeker* s) {
  PortCpu* c = s->c;
  ran(s, SF_FACE);
  if (field(s, SEEKER_DP_HURT) & 0x8000u) return false;
  look(s);
  cmp16(c, c->a, SEEKER_TOO_FAR);
  if (c->a >= SEEKER_TOO_FAR) return false;
  ran(s, SF_FACE_LOOK);
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

// `$82:EB64` and `$82:EB7A`: face them, and most frames nothing else. The
// first goes on to the state at `$82:EE0E`, with its record drawn. What the
// second goes on to sleeps, and is the ROM's.
static bool watch(Seeker* s, uint16_t odds, bool goes_on) {
  PortCpu* c = s->c;
  if (!face_nearest(s)) return false;
  draw(s);
  cmp16(c, c->a, odds);
  ran(s, SF_WATCH);
  if (c->a < odds) {
    if (!goes_on) return false;
    PORT_COVER(seeker_went_on);
    set_field(s, SEEKER_DP_STATE, SEEKER_STATE_EE0E);
    show(s);
    ran(s, SF_GO_EE0E);
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
    s->k->handler_set = true;
    show(s);
    set_field(s, SEEKER_DP_STATE, SEEKER_STATE_WATCH);
    ran(s, SF_DART_OVER);
    return true;
  }
  cmp16(c, c->a, SEEKER_TOO_FAR);
  if (c->a >= SEEKER_TOO_FAR) return false;
  ran(s, SF_DART_FAR);

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
  if (ground->blocked) return false;
  PORT_COVER(seeker_hop_began);
  ran(s, SF_DART_AIM);
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
  if (!face_nearest(s)) return false;
  draw(s);
  cmp16(c, c->a, SEEKER_HOLD_ODDS);
  ran(s, SF_WATCH);
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

// `$82:EBCF`: hidden, it tries a place near whoever is nearest. Ground that
// will not do, or a place off the level, and it tries again next frame.
// Coming back sleeps, and is the ROM's. So is nobody in reach.
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
  if (!map.c) return false;
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

bool seeker_frame(Wram* w, const Rom* rom, PortCpu* c, SeekerWork* k) {
  Seeker s = {w, rom, c, k};
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
      if (!watch(&s, SEEKER_WATCH_ODDS, true)) return false;
      break;
    case SEEKER_STATE_WATCH_B:
      PORT_COVER(seeker_watched_b);
      if (!watch(&s, SEEKER_WATCH_B_ODDS, false)) return false;
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
    default:
      return false;
  }

  c->a = field(&s, SEEKER_DP_ENDED);
  set_nz16(c, c->a);
  ran(&s, SF_TAIL);
  if (c->a != 0) {
    PORT_COVER(seeker_frame_ended);
    c->pc = SEEKER_FRAME_ENDED_PC;
    return true;
  }
  ran(&s, SF_TAKEN);
  c->a = field(&s, SEEKER_DP_SLEEP);
  set_nz16(c, c->a);
  ran(&s, SF_AGAIN);
  c->pc = SEEKER_FRAME_SLEEP_PC;
  return true;
}
