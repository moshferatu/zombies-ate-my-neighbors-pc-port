// The player's poses: standing, walking, and walking with a weapon out.
//
// The player's thread runs a small state machine of its own every frame, after
// the buttons are read (`player_state_normal`, in `port/player.h`). The word
// at `$28` names a pose handler, which the loop enters by a computed `RTS`. A
// handler mostly waits: for the buttons to change, or for the pose timer to
// run out. Then it picks the next picture. These are the three the player
// spends nearly all of a level in, as readable C:
//
//   $80:D53D  stand         standing still
//   $80:D6A8  walk          walking
//   $80:D704  walk_firing   walking with a hand weapon out
//
// and with them what they jump to when the buttons change, `$80:D4E9`, which
// starts whichever pose the direction now asks for.
//
// ## What a pose handler does
//
// **When this frame's buttons differ from last frame's, the pose starts
// again.** Holding a direction starts a walk, and holding none starts a stand.
// Starting a pose picks the table its pictures come from, by whether a weapon
// is out, and shows the first.
//
// **Standing shows one picture and waits.** Nothing changes until the buttons
// do.
//
// **Walking steps through four pictures**, one every time the pose timer runs
// out. The timer starts from 4, or with bit 15 of `$54` set from 6 or 2 by its
// bit 14. `player_state_normal` is what counts it down.
//
// **A hand weapon is a second picture**, in a display record of its own. A
// pose with one out shows it, by the way the player faces, and every other
// pose hides it.
//
// **A handler with a weapon out fires**, once nothing is left of the last
// shot's delay (`$80:ED30`). Firing takes a round of the weapon, in decimal,
// and starts the shot: a thread of its own, handed where the player is and
// which way they face. The weapon says which thread and how long until the
// next. With no rounds left nothing happens.
//
// ## What is the ROM's
//
// Weapon 5's shot, which goes on to a pose of its own that sleeps. The poses
// for the weapons in the second band (`$80:EE82`), whatever sets `$6C`
// (`$80:EF67`), and the two walks those use, `$80:D6B8` and `$80:D6DC`.
// `pose_supported` says which frames those are.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. Of the registers, carry and
// overflow can outlive a handler. The loop goes on to `LDA $2A`, and with no
// movement handler to run, nothing between there and `thread_yield` writes
// overflow, and only a compare in `$80:CE25` writes carry, which that
// routine does not always reach. So the yield's `PHP` parks what the handler
// left. See `port/flags.h`. A, X and Y are loaded before anything reads them.
//
// Port code: libc only.

#ifndef PORT_POSE_H
#define PORT_POSE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

// The tables are in bank `$80`, which is the player thread's data bank.
#define POSE_BANK 0x80u

// The handlers, as `$28` names them, and an `RTS` of each.
#define POSE_STAND_PC 0x80d53du
#define POSE_STAND_RTS_PC 0x80d557u
#define POSE_WALK_PC 0x80d6a8u
#define POSE_WALK_RTS_PC 0x80d700u
#define POSE_WALK_FIRING_PC 0x80d704u
#define POSE_WALK_FIRING_RTS_PC 0x80d700u

#define POSE_HANDLER_STAND 0xd53du
#define POSE_HANDLER_WALK 0xd6a8u
#define POSE_HANDLER_WALK_BAND_B 0xd6b8u  // not ported
#define POSE_HANDLER_WALK_6C 0xd6dcu      // not ported
#define POSE_HANDLER_WALK_FIRING 0xd704u

// The pose tables `$14` points at: four bytes a picture, a mask for the
// record's flags and a picture number. A mask with bit 15 is ANDed in and any
// other is ORed in, which is how a picture mirrors or does not. A standing
// table has one picture for each way of facing, and a walking table four.
#define POSE_FRAMES_STAND 0xd55bu
#define POSE_FRAMES_STAND_FIRING 0xd57bu
#define POSE_FRAMES_WALK 0xd75fu
#define POSE_FRAMES_WALK_FIRING 0xd7dfu
#define POSE_FRAMES_WALK_BAND_B 0xd85fu
#define POSE_FRAMES_WALK_6C 0xd8dfu

// Fields on the player's page, beyond those `port/player.h` names.
#define POSE_DP_RECORD 0x08       // the player's display record
#define POSE_DP_WEAPON 0x0a       // ...and the hand weapon's
#define POSE_DP_PICTURES_ROW 0x0c // with `$6A`, which picture table is `$10`
#define POSE_DP_PICTURES 0x10     // picture numbers index this, in bank `$90`
#define POSE_DP_WEAPON_PICTURES 0x12  // ...and the weapon's, likewise
#define POSE_DP_FRAMES 0x14       // the pose table in use
#define POSE_DP_TIMER 0x16        // frames until the next picture
#define POSE_DP_CYCLE 0x18        // the walk cycle, 0 to 3
#define POSE_DP_MOVE 0x2a         // the movement handler, or 0 standing still
#define POSE_DP_SHOT_DELAY 0x4c   // frames until the weapon may fire again
#define POSE_DP_PACE 0x54         // bits 15 and 14 change how fast it walks
#define POSE_DP_PICTURES_SET 0x6a // with `$0C`, which picture table is `$10`
#define POSE_DP_6C 0x6c           // nonzero is a pose of its own, `$80:EF67`
#define POSE_DP_STATE 0x70        // the player's state: picks the movement

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions.
typedef enum {
  POSE_WEAPON_UNTOUCHED,
  POSE_WEAPON_NOT_OUT,     // hidden: no hand weapon out
  POSE_WEAPON_NO_FACING,   // hidden: the player faces nowhere
  POSE_WEAPON_NO_PICTURE,  // hidden: this facing has no picture of it
  POSE_WEAPON_SHOWN,
} PoseWeapon;

typedef enum {
  POSE_FIRE_NONE,
  POSE_FIRE_EMPTY,  // asked to, with no rounds left
  POSE_FIRE_SHOT,
} PoseFire;

typedef enum {
  POSE_WALK_PLAIN,
  POSE_WALK_FIRING,
  POSE_WALK_BAND_B,
  POSE_WALK_6C,
} PoseWalk;

typedef struct {
  bool changed;        // the buttons changed, so the pose started again
  bool delayed;        // the last shot's delay was still running
  bool waiting;        // a walk: the pose timer was still running
  bool hid_first;      // walk_firing: the weapon was put away before the change
  bool restood;        // stand: a second-band weapon or `$6C` restarted it
  bool moving;         // a pose started with a direction held
  bool stood;          // a stand started...
  bool stand_firing;   // ...with a hand weapon out
  bool stand_band_b;   // ...or with a second-band weapon held
  bool walked;         // a walk started...
  PoseWalk walk;       // ...of this kind
  bool walk_band_b;    // ...asked for by a second-band weapon
  bool stepped;        // the walk cycle moved on...
  int pace;            // ...and the timer started again from this
  int frames, frames_masked;  // pictures shown, and of those with an AND
  PoseWeapon weapon;
  PoseFire fire;
  int shot_slot;       // the thread slot the shot took, doubled, or -1 for none
  int hides, hides_kept;  // the weapon hidden, and left alone in state `$0C`
  bool unported;       // the frame reached something that is the ROM's
  bool c, v;           // carry and overflow as the handler leaves them
  bool c_set, v_set;   // ...and whether it wrote each at all
} PoseLog;

// Would the port run this frame of the handler `handler` names the way the
// ROM does? False when it would fire weapon 5, or reach a pose that is not
// here. It runs the handler to find out, so `w` is a copy.
bool pose_supported(Wram* w, const Rom* rom, uint16_t page, uint16_t handler);

// One frame of each handler, for the player whose page is `page`. `log` may
// be NULL.
void pose_stand(Wram* w, const Rom* rom, uint16_t page, PoseLog* log);
void pose_walk(Wram* w, const Rom* rom, uint16_t page, PoseLog* log);
void pose_walk_firing(Wram* w, const Rom* rom, uint16_t page, PoseLog* log);

#endif
