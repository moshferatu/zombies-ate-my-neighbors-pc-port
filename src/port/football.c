// The footballers' state bodies -- see port/football.h.

#include "port/football.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/rng.h"

typedef struct {
  uint16_t x, y;
} Point;

// How near a player has to be for it to go at them, and to stay at all.
#define FOOTBALLER_REACH 0x0020
#define FOOTBALLER_STAYS_WITHIN 0x0140
// What a player's record collides as.
#define PLAYER_A_ID 0x0005
#define PLAYER_B_ID 0x0006

// A picture lasts three passes, and there are four.
#define FOOTBALLER_PICTURE_PASSES 2
#define FOOTBALLER_PICTURES 4

// Running loose it veers on a draw under the first. A veer lasts sixteen
// passes and a draw of up to fifteen more, and then goes on to another on a
// draw of the second or more.
#define FOOTBALLER_VEER_ODDS 0x0a
#define FOOTBALLER_VEER_LEAST 0x0010
#define FOOTBALLER_VEER_DRAW 0x000f
#define FOOTBALLER_VEER_AGAIN_ODDS 0x46
// Which way it veers is by this bit of the game's count of frames.
#define FOOTBALLER_VEER_SIDE_BIT 0x0002
#define FOOTBALLER_EIGHTHS_MASK 0x0007

#define FOOTBALLER_LAST_WAY 0x0010

// Tables in `FOOTBALLER_BANK`, each by the way it faces, doubled, unless it
// says otherwise.
#define FOOTBALLER_STEER_WAYS 0xc548u    // by bearing, doubled: the way to go
#define FOOTBALLER_TURNED_BACK 0xc591u   // the way after the ground stops it
#define FOOTBALLER_STEPS 0xc5a3u         // doubled again: a step across and down
#define FOOTBALLER_STRAIGHT 0xc62bu      // the way it straightens out to
#define FOOTBALLER_VEER_SIDES 0xc691u    // by the frame's bit: one eighth, either way
#define FOOTBALLER_VEER_ON_TO 0xc695u    // the way a second veer takes, or none
#define FOOTBALLER_PICTURE_AT 0xc861u    // by picture, doubled
#define FOOTBALLER_FLAGS 0xc869u         // a word for its record's flags

// The game's count of frames.
#define W_FRAMES 0x0020u

// One footballer's pass.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  uint16_t record;
  FootballerLog* log;  // never NULL here
  PortFlags flags;
} Footballer;

static uint16_t field(const Footballer* f, uint16_t at) {
  return wram_r16(f->w, (uint16_t)(f->page + at));
}

static void set_field(Footballer* f, uint16_t at, uint16_t v) {
  wram_w16(f->w, (uint16_t)(f->page + at), v);
}

static uint16_t record_field(const Footballer* f, uint16_t at) {
  return wram_r16(f->w, (uint16_t)(f->record + at));
}

static void set_record_field(Footballer* f, uint16_t at, uint16_t v) {
  wram_w16(f->w, (uint16_t)(f->record + at), v);
}

static uint16_t table_word(const Footballer* f, uint16_t table,
                           uint16_t index) {
  return rom_word(f->rom, ((uint32_t)FOOTBALLER_BANK << 16) +
                              (uint16_t)(table + index));
}

static Point position(const Footballer* f) {
  return (Point){field(f, FOOTBALLER_DP_X), field(f, FOOTBALLER_DP_Y)};
}

static void set_state(Footballer* f, uint16_t body) {
  set_field(f, FOOTBALLER_DP_STATE, body);
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

// A record the thread's data bank can reach, which is all of them.
static bool is_record(uint16_t record) { return record < 0x1f00u; }

// ---------------------------------------------------------------------------
// What it asks the rest of the game
// ---------------------------------------------------------------------------

// The overflow `player_bearing` leaves. With a player found off its row or
// its column it is clear, from the sums that made the answer. Otherwise it
// is that of the last gap measured: down, to the second player if there is
// one, or else to the first.
static void players_overflow(Footballer* f, Point from,
                             const PlayerPickRegs* r) {
  static const uint16_t LAST_FIRST[2] = {W_PLAYER_B_RECORD, W_PLAYER_A_RECORD};
  if (r->a != 0 && !(r->same_x && r->same_y)) {
    flags_overflow(&f->flags, false);
    return;
  }
  for (int i = 0; i < 2; i++) {
    const uint16_t player = wram_r16(f->w, LAST_FIRST[i]);
    if (player == 0) continue;
    PortFlags gap = {0};
    flags_sub(&gap, wram_r16(f->w, (uint16_t)(player + ACTOR_Y)), from.y);
    flags_overflow(&f->flags, gap.v);
    return;
  }
}

// Which way the nearer player is, within `reach`: 1 to 8, or 0 for neither.
static const PlayerPickRegs* nearer_player(Footballer* f, uint16_t reach) {
  FootballerLog* log = f->log;
  PlayerPickRegs* r = &log->players[log->player_asks & 1];
  log->player_asks++;
  const Point me = position(f);
  player_bearing(f->w, f->rom, reach, me.x, me.y, r);
  flags_carry(&f->flags, r->c);
  players_overflow(f, me, r);
  return r;
}

// A draw begins from the carry before it.
static uint16_t random_byte(Footballer* f) {
  RngResult r;
  rng_next(f->w, f->flags.c, &r);
  flags_carry(&f->flags, r.c);
  flags_overflow(&f->flags, r.v);
  if (f->log->draws < FOOTBALLER_MAX_DRAWS)
    f->log->draw_overflow[f->log->draws] = r.v;
  f->log->draws++;
  return r.a;
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

// `$81:C51F`: go at a player in reach, if they are to one side or the other.
// The ROM asks what the test's answer collides as before it trusts it, and
// with no player near that answer is the second player's record, or nothing.
static void steer(Footballer* f) {
  FootballerLog* log = f->log;
  const PlayerPickRegs* r = nearer_player(f, FOOTBALLER_REACH);
  if (!is_record(r->y)) {
    log->declined = true;
    return;
  }
  const uint16_t id = wram_r16(f->w, (uint16_t)(r->y + ACTOR_COLLIDE_ID));
  log->first_player = flags_same(&f->flags, id, PLAYER_A_ID);
  if (!log->first_player && !flags_same(&f->flags, id, PLAYER_B_ID)) {
    log->steer = FOOTBALLER_STEER_NOT_A_PLAYER;
    return;
  }
  flags_carry(&f->flags, false);  // the bearing, doubled
  if (r->a == 0) {
    log->steer = FOOTBALLER_STEER_NOBODY;
    return;
  }
  const uint16_t way =
      table_word(f, FOOTBALLER_STEER_WAYS, (uint16_t)(r->a << 1));
  if (way == 0) {
    log->steer = FOOTBALLER_STEER_NOT_ASIDE;
    return;
  }
  PORT_COVER(footballer_went_at_them);
  log->steer = FOOTBALLER_STEER_AT_THEM;
  set_field(f, FOOTBALLER_DP_WAY, way);
}

// `$81:C55A`: a pass of its run.
static void run(Footballer* f) {
  FootballerLog* log = f->log;
  log->ran = true;
  steer(f);
  if (log->declined) return;

  const uint16_t count = (uint16_t)(field(f, FOOTBALLER_DP_COUNT) - 1);
  set_field(f, FOOTBALLER_DP_COUNT, count);
  if (negative(count)) {
    log->new_picture = true;
    const uint16_t next = (uint16_t)(
        (((field(f, FOOTBALLER_DP_PICTURE) >> 1) + 1) & (FOOTBALLER_PICTURES - 1))
        << 1);
    set_field(f, FOOTBALLER_DP_PICTURE, next);
    set_field(f, FOOTBALLER_DP_COUNT, FOOTBALLER_PICTURE_PASSES);
  }

  const uint16_t way = field(f, FOOTBALLER_DP_WAY);
  const uint16_t at = (uint16_t)(way << 1);
  const Point me = position(f);
  Point to;
  to.x = flags_add(&f->flags, table_word(f, FOOTBALLER_STEPS, at), me.x);
  set_field(f, FOOTBALLER_DP_TRY_X, to.x);
  to.y = flags_add(&f->flags, table_word(f, FOOTBALLER_STEPS + 2, at), me.y);
  set_field(f, FOOTBALLER_DP_TRY_Y, to.y);

  terrain_blocked_enemy(f->w, to.x, to.y, &log->ground);
  flags_carry(&f->flags, log->ground.blocked);
  flags_overflow(&f->flags, log->ground.v);
  if (log->ground.blocked) {
    PORT_COVER(footballer_turned_back);
    set_field(f, FOOTBALLER_DP_WAY, table_word(f, FOOTBALLER_TURNED_BACK, way));
    return;
  }
  set_field(f, FOOTBALLER_DP_X, to.x);
  set_field(f, FOOTBALLER_DP_Y, to.y);
}

// `$81:C60F`: run loose from the next pass, straight across the field.
static void straighten_out(Footballer* f) {
  set_state(f, FOOTBALLER_STATE_RUN_LOOSE);
  set_field(f, FOOTBALLER_DP_WAY,
            table_word(f, FOOTBALLER_STRAIGHT, field(f, FOOTBALLER_DP_WAY)));
}

static uint16_t veer_passes(Footballer* f) {
  const uint16_t passes = (uint16_t)((random_byte(f) & FOOTBALLER_VEER_DRAW) +
                                     FOOTBALLER_VEER_LEAST);
  flags_carry(&f->flags, false);
  flags_overflow(&f->flags, false);
  return passes;
}

// `$81:C63D`: an eighth of a turn, one way or the other by the frame, for a
// while that is drawn.
static void begin_veer(Footballer* f) {
  PORT_COVER(footballer_veered);
  set_state(f, FOOTBALLER_STATE_VEER);
  const uint16_t side = wram_r16(f->w, W_FRAMES) & FOOTBALLER_VEER_SIDE_BIT;
  const uint16_t way = field(f, FOOTBALLER_DP_WAY);
  const uint16_t eighth =
      flags_adc(&f->flags, (uint16_t)((way >> 1) - 1),
                table_word(f, FOOTBALLER_VEER_SIDES, side), (way & 1) != 0);
  set_field(f, FOOTBALLER_DP_WAY,
            (uint16_t)(((eighth & FOOTBALLER_EIGHTHS_MASK) + 1) << 1));
  flags_carry(&f->flags, false);  // the way, doubled
  set_field(f, FOOTBALLER_DP_COUNT, veer_passes(f));
}

// ---------------------------------------------------------------------------
// The state bodies
// ---------------------------------------------------------------------------

// `$81:C5FF`: stand for the passes it was given, shifting from foot to foot.
static void stand(Footballer* f) {
  const uint16_t count = (uint16_t)(field(f, FOOTBALLER_DP_COUNT) - 1);
  set_field(f, FOOTBALLER_DP_COUNT, count);
  if (negative(count)) {
    PORT_COVER(footballer_set_off);
    straighten_out(f);
    return;
  }
  f->log->stood_on = true;
  set_field(f, FOOTBALLER_DP_PICTURE, (uint16_t)((count & 1) << 1));
  flags_carry(&f->flags, false);
}

// `$81:C61C`: run, veering now and then on a draw.
static void run_loose(Footballer* f) {
  if (!flags_at_least(&f->flags, random_byte(f), FOOTBALLER_VEER_ODDS)) {
    f->log->veered = true;
    begin_veer(f);
  }
  run(f);
}

// `$81:C665`: run on a veer until its passes are up, and then straighten out
// or veer again, by a draw.
static void run_veering(Footballer* f) {
  FootballerLog* log = f->log;
  const uint16_t count = (uint16_t)(field(f, FOOTBALLER_DP_COUNT) - 1);
  set_field(f, FOOTBALLER_DP_COUNT, count);
  if (!negative(count)) {
    log->veer = FOOTBALLER_VEER_ON;
    run(f);
    return;
  }
  const uint16_t on_to =
      table_word(f, FOOTBALLER_VEER_ON_TO, field(f, FOOTBALLER_DP_WAY));
  if (!flags_at_least(&f->flags, random_byte(f), FOOTBALLER_VEER_AGAIN_ODDS)) {
    PORT_COVER(footballer_straightened);
    log->veer = FOOTBALLER_VEER_STRAIGHTEN;
    straighten_out(f);
  } else if (on_to == 0) {
    log->veer = FOOTBALLER_VEER_NO_WAY;
    straighten_out(f);
  } else {
    PORT_COVER(footballer_veered_again);
    log->veer = FOOTBALLER_VEER_AGAIN;
    set_field(f, FOOTBALLER_DP_WAY, on_to);
    set_field(f, FOOTBALLER_DP_COUNT, veer_passes(f));
  }
  run(f);
}

// ---------------------------------------------------------------------------
// The pass
// ---------------------------------------------------------------------------

// `$81:C824`: its picture, mirrored or not by the way it faces, where it now
// is. With neither player near, it leaves.
static void show(Footballer* f) {
  FootballerLog* log = f->log;
  const uint16_t word =
      table_word(f, FOOTBALLER_FLAGS, field(f, FOOTBALLER_DP_WAY));
  const uint16_t flags = record_field(f, ACTOR_FLAGS);
  log->mask_clears = negative(word);
  set_record_field(f, ACTOR_FLAGS,
                   negative(word) ? (uint16_t)(flags & word)
                                  : (uint16_t)(flags | word));
  set_record_field(f, ACTOR_META,
                   table_word(f, FOOTBALLER_PICTURE_AT,
                              field(f, FOOTBALLER_DP_PICTURE)));
  const Point me = position(f);
  set_record_field(f, ACTOR_X, me.x);
  set_record_field(f, ACTOR_Z, 0);
  set_record_field(f, ACTOR_Y, me.y);

  if (nearer_player(f, FOOTBALLER_STAYS_WITHIN)->a == 0) {
    PORT_COVER(footballer_left);
    log->gone = true;
    set_field(f, FOOTBALLER_DP_FATE,
              (uint16_t)(field(f, FOOTBALLER_DP_FATE) + 1));
  }
}

bool footballer_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t state = wram_r16(w, (uint16_t)(page + FOOTBALLER_DP_STATE));
  const uint16_t way = wram_r16(w, (uint16_t)(page + FOOTBALLER_DP_WAY));
  const uint16_t picture =
      wram_r16(w, (uint16_t)(page + FOOTBALLER_DP_PICTURE));
  if (way > FOOTBALLER_LAST_WAY || (way & 1) != 0) return false;
  if (picture >= FOOTBALLER_PICTURES * 2 || (picture & 1) != 0) return false;
  return state == FOOTBALLER_STATE_RUN || state == FOOTBALLER_STATE_STAND ||
         state == FOOTBALLER_STATE_RUN_LOOSE ||
         state == FOOTBALLER_STATE_VEER;
}

FootballerFate footballer_frame(Wram* w, const Rom* rom, uint16_t page,
                                bool carry, FootballerLog* log) {
  FootballerLog scratch;
  if (log == NULL) log = &scratch;
  *log = (FootballerLog){0};
  Footballer f = {w, rom, page,
                  wram_r16(w, (uint16_t)(page + FOOTBALLER_DP_RECORD)), log,
                  {carry, false, false, false}};

  log->state = field(&f, FOOTBALLER_DP_STATE);
  switch (log->state) {
    case FOOTBALLER_STATE_RUN:
      run(&f);
      break;
    case FOOTBALLER_STATE_STAND:
      PORT_COVER(footballer_stood);
      stand(&f);
      break;
    case FOOTBALLER_STATE_RUN_LOOSE:
      PORT_COVER(footballer_ran_loose);
      run_loose(&f);
      break;
    default:
      PORT_COVER(footballer_ran_veering);
      run_veering(&f);
      break;
  }
  if (log->declined) return FOOTBALLER_SLEEPS;
  show(&f);

  log->c = f.flags.c;
  log->v = f.flags.v;
  log->c_set = f.flags.c_set;
  log->v_set = f.flags.v_set;
  return field(&f, FOOTBALLER_DP_FATE) != 0 ? FOOTBALLER_ENDS
                                            : FOOTBALLER_SLEEPS;
}
