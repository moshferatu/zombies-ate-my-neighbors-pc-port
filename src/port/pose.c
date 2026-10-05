// The player's pose handlers -- see port/pose.h.

#include "port/pose.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/oam.h"     // the display record's fields
#include "port/player.h"  // PSN_DP_*: the buttons, the weapon words, the facing
#include "port/step.h"    // where the player is
#include "port/thread.h"

// Tables in `POSE_BANK`.
#define POSE_PICTURE_TABLES 0xfb52u  // a picture table for each `$6A` and `$0C`
#define POSE_MOVES 0xd74fu           // a movement handler for each state
#define POSE_WEAPON_FACINGS 0xed12u  // the weapon's picture for each facing
#define POSE_INVENTORIES 0xed88u     // each player's rounds, a word a weapon
// Six bytes a weapon: its shot's thread, the bank of it, and the frames
// until the next shot.
#define POSE_SHOTS 0xed8cu
#define POSE_SHOT_BYTES 6
#define POSE_SHOT_BANK 2
#define POSE_SHOT_DELAY 4
#define POSE_WEAPONS 14
// The weapon whose shot goes on to a pose of its own, `$80:DE96`.
#define POSE_WEAPON_OF_ITS_OWN 5

#define POSE_PICTURE_BANK 0x0090
// How long a standing picture is held before the timer means anything.
#define POSE_STAND_TIMER 8
// A walking picture's time: usually, and with bit 15 of `$54` set, by bit 14.
#define POSE_PACE 4
#define POSE_PACE_SLOW 6
#define POSE_PACE_FAST 2
#define POSE_PACE_CHANGED 0x8000u
#define POSE_PACE_SLOWER 0x4000u
// `LDA #$0005 : STA $16`, which the step straight after it overwrites.
#define POSE_FIRING_TIMER 5
// The state in which the hand weapon is left as it is.
#define POSE_STATE_KEEPS_WEAPON 0x000c
// What `$18` holds through the `$6C` walk until its first step.
#define POSE_CYCLE_UNSTARTED 0xffffu

// One player's frame, and the carry and overflow the ROM would leave.
typedef struct {
  Wram* w;
  const Rom* rom;
  uint16_t page;
  PoseLog* log;  // never NULL here
  PortFlags flags;
} Pose;

static uint16_t field(const Pose* p, uint16_t at) {
  return wram_r16(p->w, (uint16_t)(p->page + at));
}

static void set_field(Pose* p, uint16_t at, uint16_t v) {
  wram_w16(p->w, (uint16_t)(p->page + at), v);
}

// A word of a table in `POSE_BANK`, `index` bytes in.
static uint16_t table_word(const Pose* p, uint16_t table, uint16_t index) {
  return rom_word(p->rom, ((uint32_t)POSE_BANK << 16) + table + index);
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }

// The way the player faces: the last direction held, as the game numbers
// them, doubled, from 2 for up. Less two, it indexes the tables.
static uint16_t facing_index(const Pose* p) {
  return (uint16_t)(field(p, PSN_DP_DIR_HELD) - 2);
}

// Something only the ROM does. On the copy `pose_supported` runs on, that is
// the answer; a frame that gets here for real was not asked about.
static void leave_to_rom(Pose* p) { p->log->unported = true; }

// ---------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------

// `$80:F300`: show the picture `at` bytes into the pose table.
static void show(Pose* p, uint16_t at) {
  const uint16_t frames = field(p, POSE_DP_FRAMES);
  const uint16_t mask = table_word(p, frames, at);
  const uint16_t number = table_word(p, frames, (uint16_t)(at + 2));
  const uint16_t record = field(p, POSE_DP_RECORD);
  const uint16_t flags = wram_r16(p->w, (uint16_t)(record + ACTOR_FLAGS));
  wram_w16(p->w, (uint16_t)(record + ACTOR_FLAGS),
           negative(mask) ? (uint16_t)(flags & mask)
                          : (uint16_t)(flags | mask));
  wram_w16(p->w, (uint16_t)(record + ACTOR_META),
           table_word(p, field(p, POSE_DP_PICTURES),
                      flags_double(&p->flags, number)));
  wram_w16(p->w, (uint16_t)(record + ACTOR_META_BANK), POSE_PICTURE_BANK);
  p->log->frames++;
  if (negative(mask)) p->log->frames_masked++;
}

// `$80:ECFD`: put the hand weapon away, except in the one state that keeps it.
static void hide_weapon(Pose* p) {
  p->log->hides++;
  if (flags_same(&p->flags, field(p, POSE_DP_STATE), POSE_STATE_KEEPS_WEAPON)) {
    p->log->hides_kept++;
    return;
  }
  const uint16_t weapon = field(p, POSE_DP_WEAPON);
  wram_w16(p->w, (uint16_t)(weapon + ACTOR_FLAGS),
           (uint16_t)(wram_r16(p->w, (uint16_t)(weapon + ACTOR_FLAGS)) &
                      ~ACTOR_DRAW));
}

// `$80:ECD4`: show the hand weapon the way the player faces, if one is out
// and that facing has a picture of it. It is drawn with the player's flags.
static void show_weapon(Pose* p) {
  if (field(p, PSN_DP_FIRE_A) == 0) {
    p->log->weapon = POSE_WEAPON_NOT_OUT;
    hide_weapon(p);
    return;
  }
  if (field(p, PSN_DP_DIR_HELD) == 0) {
    p->log->weapon = POSE_WEAPON_NO_FACING;
    hide_weapon(p);
    return;
  }
  const uint16_t picture = table_word(p, POSE_WEAPON_FACINGS,
                                      flags_double(&p->flags, facing_index(p)));
  if (negative(picture)) {
    p->log->weapon = POSE_WEAPON_NO_PICTURE;
    hide_weapon(p);
    return;
  }
  PORT_COVER(pose_weapon_shown);
  p->log->weapon = POSE_WEAPON_SHOWN;
  const uint16_t weapon = field(p, POSE_DP_WEAPON);
  const uint16_t record = field(p, POSE_DP_RECORD);
  wram_w16(p->w, (uint16_t)(weapon + ACTOR_META),
           table_word(p, field(p, POSE_DP_WEAPON_PICTURES),
                      flags_double(&p->flags, picture)));
  wram_w16(p->w, (uint16_t)(weapon + ACTOR_FLAGS),
           (uint16_t)(wram_r16(p->w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
}

// The picture table for this player as it is now: `$6A` times four, plus
// `$0C`. The sum takes the carry the second doubling left.
static void pick_pictures(Pose* p) {
  const uint16_t twice =
      flags_double(&p->flags, field(p, POSE_DP_PICTURES_SET));
  const uint16_t set = flags_double(&p->flags, twice);
  const uint16_t which =
      flags_adc(&p->flags, set, field(p, POSE_DP_PICTURES_ROW), p->flags.c);
  set_field(p, POSE_DP_PICTURES, table_word(p, POSE_PICTURE_TABLES, which));
}

// ---------------------------------------------------------------------------
// Starting a pose
// ---------------------------------------------------------------------------

// `$80:D4F4`: stand still, with the hand weapon out if there is one.
static void stand_begin(Pose* p) {
  PORT_COVER(pose_stood);
  p->log->stood = true;
  set_field(p, PSN_DP_RESUME, POSE_HANDLER_STAND);
  set_field(p, POSE_DP_CYCLE, 0);
  set_field(p, POSE_DP_MOVE, 0);
  pick_pictures(p);

  if (field(p, PSN_DP_FIRE_A) != 0) {
    p->log->stand_firing = true;
    set_field(p, POSE_DP_FRAMES, POSE_FRAMES_STAND_FIRING);
  } else {
    // A second-band weapon ready to fire, and `$6C`, have poses of their own.
    if (field(p, PSN_DP_FIRE_B) != 0) {
      p->log->stand_band_b = true;
      if (field(p, POSE_DP_SHOT_DELAY) == 0) {
        leave_to_rom(p);
        return;
      }
    }
    if (field(p, POSE_DP_6C) != 0) {
      leave_to_rom(p);
      return;
    }
    set_field(p, POSE_DP_FRAMES, POSE_FRAMES_STAND);
  }

  set_field(p, POSE_DP_TIMER, POSE_STAND_TIMER);
  // One picture for each facing, four bytes apart, less the doubling `show`'s
  // caller owes: the cycle is zero, so the `ORA` adds nothing.
  show(p, flags_double(&p->flags,
                       (uint16_t)(facing_index(p) | field(p, POSE_DP_CYCLE))));
  show_weapon(p);
}

// `$80:D65B`: walk, in whichever of four ways the weapon words ask for. Two
// of them are the ROM's, which the next frame finds out.
static void walk_begin(Pose* p) {
  PORT_COVER(pose_walked);
  p->log->walked = true;
  set_field(p, POSE_DP_MOVE,
            table_word(p, POSE_MOVES, field(p, POSE_DP_STATE)));
  set_field(p, POSE_DP_TIMER, 0);
  pick_pictures(p);

  PoseWalk walk = POSE_WALK_PLAIN;
  if (field(p, PSN_DP_FIRE_B) != 0) {
    p->log->walk_band_b = true;
    walk = field(p, POSE_DP_PICTURES_SET) != 0 ? POSE_WALK_6C
                                               : POSE_WALK_BAND_B;
  } else if (field(p, PSN_DP_FIRE_A) != 0) {
    walk = POSE_WALK_FIRING;
  } else if (field(p, POSE_DP_6C) != 0) {
    walk = POSE_WALK_6C;
  }
  p->log->walk = walk;

  static const struct {
    uint16_t handler, frames;
  } WALKS[] = {
      [POSE_WALK_PLAIN] = {POSE_HANDLER_WALK, POSE_FRAMES_WALK},
      [POSE_WALK_FIRING] = {POSE_HANDLER_WALK_FIRING, POSE_FRAMES_WALK_FIRING},
      [POSE_WALK_BAND_B] = {POSE_HANDLER_WALK_BAND_B, POSE_FRAMES_WALK_BAND_B},
      [POSE_WALK_6C] = {POSE_HANDLER_WALK_6C, POSE_FRAMES_WALK_6C},
  };
  if (walk == POSE_WALK_6C) set_field(p, POSE_DP_CYCLE, POSE_CYCLE_UNSTARTED);
  set_field(p, PSN_DP_RESUME, WALKS[walk].handler);
  set_field(p, POSE_DP_FRAMES, WALKS[walk].frames);
}

// `$80:D4E9`: the buttons changed, so start the pose the direction asks for.
static void start_again(Pose* p) {
  PORT_COVER(pose_changed);
  p->log->changed = true;
  p->log->moving = field(p, PSN_DP_DIR) != 0;
  if (p->log->moving) walk_begin(p);
  else stand_begin(p);
}

static bool buttons_changed(Pose* p) {
  return !flags_same(&p->flags, field(p, PSN_DP_BUTTONS),
                     field(p, PSN_DP_PREV));
}

// ---------------------------------------------------------------------------
// The handlers
// ---------------------------------------------------------------------------

// One less, in decimal. `rounds` is not zero.
static uint16_t decimal_less_one(uint16_t rounds) {
  if (rounds & 0x000fu) return (uint16_t)(rounds - 0x0001u);
  if (rounds & 0x00f0u) return (uint16_t)(rounds - 0x0010u + 0x0009u);
  if (rounds & 0x0f00u) return (uint16_t)(rounds - 0x0100u + 0x0099u);
  return (uint16_t)(rounds - 0x1000u + 0x0999u);
}

// `$80:ED30`, from where it has found the last shot's delay run out: take a
// round of the weapon held and start its shot.
static void fire(Pose* p) {
  const uint16_t player = field(p, PSN_DP_PLAYER);
  const uint16_t weapon = wram_r16(p->w, (uint16_t)(W_PLAYER_WEAPON + player));
  if (weapon >= POSE_WEAPONS) {
    leave_to_rom(p);  // nothing held: the ROM reads past the table
    return;
  }
  const uint16_t inventory = table_word(p, POSE_INVENTORIES, player);
  set_field(p, PLAYER_DP_PTR, inventory);
  const uint16_t at = (uint16_t)(inventory + flags_double(&p->flags, weapon));
  const uint16_t rounds = wram_r16(p->w, at);
  if (rounds == 0) {
    PORT_COVER(pose_fire_empty);
    p->log->fire = POSE_FIRE_EMPTY;
    return;
  }
  if (weapon == POSE_WEAPON_OF_ITS_OWN) {
    leave_to_rom(p);
    return;
  }
  PORT_COVER(pose_fired);
  wram_w16(p->w, at, decimal_less_one(rounds));

  // What the shot's thread starts with: the first words of this page, which
  // `thread_spawn` copies to the new one.
  set_field(p, 0x00, field(p, STEP_DP_X));
  set_field(p, 0x02, field(p, STEP_DP_Y));
  set_field(p, 0x04, field(p, PSN_DP_DIR_HELD));
  set_field(p, 0x06, field(p, POSE_DP_PICTURES_ROW));
  const uint16_t shot = (uint16_t)(weapon * POSE_SHOT_BYTES);
  set_field(p, POSE_DP_SHOT_DELAY,
            table_word(p, POSE_SHOTS + POSE_SHOT_DELAY, shot));
  p->log->fire = POSE_FIRE_SHOT;
  p->log->shot_slot =
      thread_spawn(p->w, p->rom, table_word(p, POSE_SHOTS, shot),
                   table_word(p, POSE_SHOTS + POSE_SHOT_BANK, shot), p->page);
  // The index's add never overflows, and the last compare is against the
  // weapon with a pose of its own.
  flags_overflow(&p->flags, false);
  flags_carry(&p->flags, weapon >= POSE_WEAPON_OF_ITS_OWN);
}

static void stand(Pose* p) {
  if (buttons_changed(p)) {
    start_again(p);
    return;
  }
  if (field(p, POSE_DP_SHOT_DELAY) != 0) {
    p->log->delayed = true;
    return;
  }
  if (field(p, PSN_DP_FIRE_A) != 0) {
    fire(p);
    return;
  }
  if ((field(p, PSN_DP_FIRE_B) | field(p, POSE_DP_6C)) == 0) return;
  p->log->restood = true;
  stand_begin(p);
}

// `$80:D72A`: the walk cycle's next picture, and the timer started again.
static void step(Pose* p) {
  PORT_COVER(pose_stepped);
  const uint16_t pace_bits = field(p, POSE_DP_PACE);
  flags_overflow(&p->flags, (pace_bits & POSE_PACE_SLOWER) != 0);  // the `BIT`
  uint16_t pace = POSE_PACE;
  if (pace_bits & POSE_PACE_CHANGED)
    pace = (pace_bits & POSE_PACE_SLOWER) ? POSE_PACE_SLOW : POSE_PACE_FAST;
  set_field(p, POSE_DP_TIMER, pace);
  p->log->stepped = true;
  p->log->pace = pace;

  const uint16_t cycle = (uint16_t)((field(p, POSE_DP_CYCLE) + 1) & 3);
  set_field(p, POSE_DP_CYCLE, cycle);
  // Four pictures for each facing: the facing, then the cycle, then four
  // bytes a picture.
  const uint16_t picture =
      (uint16_t)(flags_double(&p->flags, facing_index(p)) | cycle);
  show(p, flags_double(&p->flags, flags_double(&p->flags, picture)));
}

static void walk(Pose* p) {
  if (buttons_changed(p)) {
    start_again(p);
    return;
  }
  if (field(p, POSE_DP_TIMER) != 0) {
    p->log->waiting = true;
    return;
  }
  step(p);
  hide_weapon(p);
}

static void walk_firing(Pose* p) {
  if (buttons_changed(p)) {
    if (field(p, PSN_DP_FIRE_A) == 0) {
      p->log->hid_first = true;
      hide_weapon(p);
    }
    start_again(p);
    return;
  }
  if (field(p, POSE_DP_SHOT_DELAY) == 0) {
    fire(p);
    if (p->log->unported) return;
  } else {
    p->log->delayed = true;
  }
  if (field(p, POSE_DP_TIMER) != 0) {
    p->log->waiting = true;
    return;
  }
  set_field(p, POSE_DP_TIMER, POSE_FIRING_TIMER);
  step(p);
  show_weapon(p);
}

// ---------------------------------------------------------------------------

static void run(void (*handler)(Pose*), Wram* w, const Rom* rom, uint16_t page,
                PoseLog* log) {
  PoseLog scratch = {0};
  Pose p = {w, rom, page, log ? log : &scratch, {false, false, false, false}};
  handler(&p);
  p.log->c = p.flags.c;
  p.log->v = p.flags.v;
  p.log->c_set = p.flags.c_set;
  p.log->v_set = p.flags.v_set;
}

void pose_stand(Wram* w, const Rom* rom, uint16_t page, PoseLog* log) {
  run(stand, w, rom, page, log);
}

void pose_walk(Wram* w, const Rom* rom, uint16_t page, PoseLog* log) {
  run(walk, w, rom, page, log);
}

void pose_walk_firing(Wram* w, const Rom* rom, uint16_t page, PoseLog* log) {
  run(walk_firing, w, rom, page, log);
}

bool pose_supported(Wram* w, const Rom* rom, uint16_t page, uint16_t handler) {
  PoseLog log = {0};
  switch (handler) {
    case POSE_HANDLER_STAND: run(stand, w, rom, page, &log); break;
    case POSE_HANDLER_WALK: run(walk, w, rom, page, &log); break;
    case POSE_HANDLER_WALK_FIRING: run(walk_firing, w, rom, page, &log); break;
    default: return false;
  }
  return !log.unported;
}
