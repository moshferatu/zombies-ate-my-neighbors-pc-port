// A player's frame.
//
// Each player in the game has a thread, started once a level, that loops for
// good at `$80:CDF7`. Every pass sleeps a tick and then does seven things in
// a fixed order. This is one pass as readable C, from where the sleep comes
// back to where the next begins:
//
//   $80:CDFE  player_frame   a frame of a player
//
// The seven are each in a file of their own, and this is what joins them:
//
//   the state      `$70` picks one of eight. The ordinary one reads the pad
//                  (`port/player.h`), and the one for a player stuck in slime
//                  is `port/stuck.h`. Two more share a handler in which the
//                  pad only turns the player. The potion's monster has one,
//                  and so has a player who flashes and cannot be hurt.
//   the hurt timer counts down to the next time the player can be hurt
//   the pose       `$28` names a handler, which shows the next picture
//                  (`port/pose.h`)
//   the movement   `$2A` names another, or nothing for a player standing
//                  still. On ordinary ground it is the walk, and in water
//                  the swim (`port/walk.h`).
//   the position   is copied to the display record (`port/step.h`)
//   this frame's buttons are kept, to tell a press from a hold next frame
//   has the level been won, or the player died?
//
// ## What a frame does not do
//
// Most frames are a player standing or walking, and those are here. The rest
// are left to the ROM, which runs them as it always did, a piece at a time:
//
//   * either of the other two states
//   * the monster's last frame, when the potion wears off, and the last
//     frame of the flashing
//   * a frame on which a button is pressed that changes the weapon or the
//     item or uses the item, or a shoulder button is, and one with a
//     frontend's request for such a change waiting
//   * a frame that tells the player they were hit
//   * a pose or a movement that is not one of those named, and the frames of
//     those that their own files leave to the ROM: a shot that goes on to a
//     pose of its own, a tile with a reaction the walk does not have, a
//     swimmer reaching the bank
//   * the frame the game is lost on, with nobody left to rescue and nobody
//     rescued, and the one the player dies on
//
// `player_frame_supported` says which frames those are.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. The frame ends at the thread's
// `JSL thread_yield` with one tick in A, and of the status only carry and
// overflow outlive it: the yield's `PHP` parks them with the thread. Carry
// is the state's, or the pose's when it wrote one, or the walk's last answer.
// A frame in which none of the three writes it leaves the carry the thread
// woke with, and `PlayerFrameLog::c_set` says which kind it was.
// Overflow is bit 14 of the hit event, which the hurt timer's `BIT` reads,
// or the pose's, or the walk's last add.
//
// Port code: libc only.

#ifndef PORT_PLAYER_FRAME_H
#define PORT_PLAYER_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/floor.h"
#include "port/oam.h"
#include "port/player.h"
#include "port/pose.h"
#include "port/stuck.h"
#include "port/walk.h"
#include "port/wram.h"

// Where the sleep comes back to, and the `JSL` the next one is.
#define PLAYER_FRAME_PC 0x80cdfeu
#define PLAYER_FRAME_YIELD_PC 0x80cdfau

// The tables the state and the pose read are in bank `$80`, which is the
// player thread's data bank.
#define PLAYER_FRAME_BANK 0x80u

// Fields on the player's page, beyond those the seven's own files name.
#define PLAYER_DP_INDEX 0x0e        // which player, doubled
#define PLAYER_DP_BUTTONS 0x1a      // this frame's buttons
#define PLAYER_DP_LAST_BUTTONS 0x1c // ...and the last frame's
#define PLAYER_DP_POSE 0x28         // the pose handler
#define PLAYER_DP_MOVEMENT 0x2a     // the movement handler, or 0
#define PLAYER_DP_HIT_EVENT 0x50    // bit 15: the player is to be told of a hit
#define PLAYER_DP_HURT_TIMER 0x52   // negative once it has run out
#define PLAYER_DP_HURT_HELD 0x6a    // nonzero holds the timer still
#define PLAYER_DP_STATE 0x70

#define PLAYER_STATE_NORMAL 0x0000u
#define PLAYER_STATE_MONSTER 0x0002u    // `$80:D2EA`: the potion's monster
#define PLAYER_STATE_TURNING 0x0006u    // these two share a handler,
#define PLAYER_STATE_TURNING_B 0x000eu  // `$80:D343`
#define PLAYER_STATE_FLASHING 0x000au   // `$80:D404`
#define PLAYER_STATE_STUCK 0x000cu

// The monster: the pad as it was when a button to punch with was down, and
// the frames the potion has left.
#define MONSTER_DP_PUNCH 0x6c
#define MONSTER_DP_FRAMES_LEFT 0x56
#define MONSTER_PUNCH_BUTTONS 0xc0c0u

// The flashing: the frames it has left, and a word that says the state under
// it is the turning one when it holds this.
#define FLASHING_DP_FRAMES_LEFT 0x74
#define FLASHING_DP_UNDER 0x10
#define FLASHING_UNDER_TURNING 0xfd72u
#define FLASHING_REACH 8  // pixels each way that are told of the player
#define FLASHING_HURT_TIMER 2
// What each player tells them, a word a player.
#define FLASHING_IDS 0x80d461u
// The box `actor_notify_box` is given, on page zero.
#define W_BOX_LEFT 0x0038u
#define W_BOX_RIGHT 0x003au
#define W_BOX_TOP 0x003cu
#define W_BOX_BOTTOM 0x003eu
#define W_BOX_ID 0x0040u

// Neighbours rescued on this level, a word for each player. A level with
// nobody left to rescue goes on while either is not zero.
#define W_RESCUED 0x1f9cu
#define PLAYER_MOVEMENT_WALK 0xe4bau
#define PLAYER_MOVEMENT_MONSTER_WALK 0xe595u
#define PLAYER_MOVEMENT_STUCK_WALK 0xe6c2u
#define PLAYER_MOVEMENT_SWIM 0xe543u

typedef enum {
  PLAYER_HURT_TIMER_HELD,
  PLAYER_HURT_TIMER_RUNNING,
  PLAYER_HURT_TIMER_OUT,
} PlayerHurtTimer;

// For the harness, and only for it: what each of the seven did, which is what
// it takes to price the ROM's instructions, and the two flags the frame
// leaves.
typedef struct {
  uint16_t state;          // the state that ran...
  PlayerStateRegs normal;  // ...the ordinary one
  FloorRegs floor;         // ...or stuck: the ground under them, and then it
  StuckLog stuck;
  bool turned;             // ...or turning: a direction was held
  bool timer_ran;          // ......and the pose timer had not run out
  bool punched;            // ...or the monster: a button to punch with is down
  bool potion_ran;         // ......and the potion has frames left
  bool under_turning;      // ...or flashing: over the turning state
  ActorNotifyWork told;    // ......what was in reach, and told
  bool flickered;          // ......an even frame, and the picture's flag flipped
  PlayerHurtTimer hurt_timer;
  uint16_t pose;           // the pose handler that ran
  PoseLog pose_log;
  bool walked;
  WalkKind walk_kind;      // ...by which of the walks
  WalkLog walk;
  bool two_part;           // the position went to two display records
  bool nobody_left;        // nobody left to rescue, and somebody was rescued
  bool c, v;
  bool c_set;              // false: carry is as the thread woke with it
  bool unported;           // the frame reached something that is the ROM's
} PlayerFrameLog;

// Would the port run this frame the way the ROM does? It runs the frame to
// find out, so `w` is a copy.
bool player_frame_supported(Wram* w, const Rom* rom, uint16_t page);
// ...and what the frame did, for a caller with more to ask of it.
bool player_frame_tried(Wram* w, const Rom* rom, uint16_t page,
                        PlayerFrameLog* log);

// One frame of the player whose page is `page`. `log` may be NULL.
void player_frame(Wram* w, const Rom* rom, uint16_t page, PlayerFrameLog* log);

#endif
