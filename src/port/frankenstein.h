// Frankenstein's monster, once a player has come near it: it walks, it
// throws, and it dies.
//
// The name is the game's. Each kill of it counts in a tally for the killer
// at `$1FE8`, and the screen after a level pays a bonus for two of them, by
// `$82:CE17` (`port/level_end.h`), whose words are FRANKENSTEIN DESTROY
// BONUS. It is the thing in record 48, and that level's session is where it
// was first seen.
//
// Its start and its wait are ported already (`frankenstein_enter` and
// `frankenstein_begun` in `port/thread_stretches.h`, `frankenstein_wait` in
// `port/waits.h`). The wait ends at `$81:A484` with a player within 80, and
// from there the 65816 ran it all until now.
//
// **It wakes**: its handler becomes `frankenstein_collide` (`port/collide.h`),
// its entry in the level's list is marked (`$81:8191`), and it takes a way
// at random and a step. It can be hit now (collide id 3), with 150 health,
// and it is no longer drawn over everything.
//
// **Each frame** after that it sleeps a tick and then:
//
//   * by a draw of 12 in 256 it throws one of its four things, if one of
//     them is free, and if it is on the screen a sound goes with it, thrown
//     or not;
//   * every other frame it runs its state, an address at `$14`, by a pushed
//     return as the Snakeoid does;
//   * it shows the next picture of its walk every ninth frame, by its way;
//   * one of its four things moves, a different one each frame.
//
// **Its states:**
//
//     walking   `$81:A1B4`  a step its way, two pixels, if the ground and
//                           nobody is in it; if the ground is, a quarter
//                           turn and...
//     turning   `$81:A20D`  ...an eighth back each frame, until it can
//                           step one of the two
//     deciding  `$81:A26D`  a step towards whoever is nearest, two pixels,
//                           an axis at a time where the ground lets it;
//                           nearer than 80 it attacks, and 300 or further
//                           it wanders off
//     attacking `$81:A2F1`  pictures from a list by its way, one every
//                           third time it runs, until they are 96 away
//
// The walk and the turn look round first (`$81:A362`): anybody within 320
// and it decides instead. A hundred steps that went nowhere while deciding,
// and it walks for as many frames before it looks round again.
//
// **A thing it throws** is a record of its own, with collide id 9, at one
// of eight places about it by a way drawn at random, which it keeps for a
// few frames before it turns towards whoever is nearest that thing. It
// lives 85 of its moves, four frames apart, two pixels a move, and is freed
// when it runs out, when it meets the ground, or when nobody is anywhere.
//
// **Its death.** The frame its handler sets `$2A`, its things' lives are
// ended, and it waits a frame at a time until all four are freed. Whoever
// killed it is paid `$1000` (`port/begin.h`), its kind's count goes up, so
// does the killer's tally at `$1FE8`, it can no longer be touched, and it is
// heard. Its last pictures are played (`pictures_play`), and its thread ends
// as any does (`record_end`).
//
// **Written as stretches**, like `port/snakeoid.h`. A stretch stops on its
// sleeps, a record asked for or freed, its sounds, the mark in the list,
// what pays for it and its last pictures. It makes every other call itself,
// and they are the ones the harness knows the price of: a draw, who is
// nearest, which way, the ground and who is at a point.
//
// Port code: libc only.

#ifndef PORT_FRANKENSTEIN_H
#define PORT_FRANKENSTEIN_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/rng.h"
#include "port/terrain.h"
#include "port/wram.h"

#define FRANKENSTEIN_CODE_BANK 0x81u

// Its page.
#define FRANKENSTEIN_DP_LISTED 0x06     // its entry in the level's list
#define FRANKENSTEIN_DP_RECORD 0x08
#define FRANKENSTEIN_DP_FRAMES 0x0a     // until its next picture
#define FRANKENSTEIN_DP_PICTURE 0x0c    // which, doubled; or an attack's
#define FRANKENSTEIN_DP_WAY 0x0e        // doubled: 2 for up, round by the clock
#define FRANKENSTEIN_DP_TURN 0x10       // the way a turn tries, or a way
                                       // times four
#define FRANKENSTEIN_DP_STUCK 0x12      // steps that went nowhere; negative:
                                   // frames of walking before it looks
#define FRANKENSTEIN_DP_STATE 0x14
#define FRANKENSTEIN_DP_X 0x16
#define FRANKENSTEIN_DP_Y 0x18
#define FRANKENSTEIN_DP_TRY_X 0x1a
#define FRANKENSTEIN_DP_TRY_Y 0x1c
#define FRANKENSTEIN_DP_HEALTH 0x1e     // `port/collide.h`'s, as its handler
#define FRANKENSTEIN_DP_HIT_ID 0x28     // ...takes it
#define FRANKENSTEIN_DP_DEAD 0x2a
#define FRANKENSTEIN_DP_MOVED 0x20      // or an attack's frame of a thing's
#define FRANKENSTEIN_DP_THING 0x22      // the record of the thing it moves
#define FRANKENSTEIN_DP_ATTACK 0x24     // the list of an attack's pictures
#define FRANKENSTEIN_DP_ATTACK_TICKS 0x26
#define FRANKENSTEIN_DP_THINGS_OUT 0x2c
// Its four things, eight bytes apart: a record or `$FFFF`, its moves
// before it turns, its way times four, and its life.
#define FRANKENSTEIN_DP_THINGS 0x2e
#define FRANKENSTEIN_DP_THING_TURNS 0x30
#define FRANKENSTEIN_DP_THING_WAY 0x32
#define FRANKENSTEIN_DP_THING_LIFE 0x34
#define FRANKENSTEIN_DP_THING_AT 0x4e   // which of them, as an offset
#define FRANKENSTEIN_THING_STRIDE 0x0008u
#define FRANKENSTEIN_THING_LAST 0x0018u
#define FRANKENSTEIN_NO_THING 0xffffu

// Its states, as `$14` holds them.
#define FRANKENSTEIN_WALKING 0xa1b4u
#define FRANKENSTEIN_TURNING 0xa20du
#define FRANKENSTEIN_DECIDING 0xa26du
#define FRANKENSTEIN_ATTACKING 0xa2f1u

// Where each stretch begins.
#define FRANKENSTEIN_FOUND_PC 0x81a484u    // the wait over: a player near
#define FRANKENSTEIN_MARKED_PC 0x81a496u   // after the mark in the list
#define FRANKENSTEIN_WOKE_PC 0x81a4bau     // after a frame's sleep
#define FRANKENSTEIN_MADE_PC 0x819feau     // a record for a thing, in A
#define FRANKENSTEIN_HEARD_PC 0x819fceu    // after the sound of a throw
#define FRANKENSTEIN_FREED_PC 0x81a11bu    // after a thing is freed
#define FRANKENSTEIN_DIES_WOKE_PC 0x81a4e6u  // a frame on in its death
#define FRANKENSTEIN_SCORED_PC 0x81a4f5u   // after it is paid for
#define FRANKENSTEIN_CRIED_PC 0x81a51cu    // after its death's sound

// Where one stops: a call the harness makes.
#define FRANKENSTEIN_MARK_PC 0x81a492u     // `JSL $81:8191`
#define FRANKENSTEIN_SLEEP_PC 0x81a4b6u    // `JSL thread_yield`
#define FRANKENSTEIN_RECORD_PC 0x819fe6u   // `JSL actor_slot_alloc`
#define FRANKENSTEIN_THROW_SOUND_PC 0x819fcau  // `JSL apu_play_sfx`
#define FRANKENSTEIN_FREE_PC 0x81a117u     // `JSL actor_slot_free`
#define FRANKENSTEIN_DIES_SLEEP_PC 0x81a4e2u  // `JSL thread_yield`
#define FRANKENSTEIN_DIES_PC 0x81a4eau     // `LDX #$1000`: `port/begin.h`'s
#define FRANKENSTEIN_CRY_PC 0x81a518u      // `JSL apu_play_sfx`
#define FRANKENSTEIN_LAST_PC 0x81a51fu     // `JSL pictures_play`

// The return addresses a stretch can find on the stack, as `RTS` pulls them.
#define FRANKENSTEIN_RET_THROWS 0xa4bcu    // `JSR $9F9D`
#define FRANKENSTEIN_RET_MAKES 0x9fa8u     // `JSR $9FCF`
#define FRANKENSTEIN_RET_MOVES 0xa4d2u     // `JSR $A06B`
#define FRANKENSTEIN_RET_MOVE 0xa07au      // `JSR $A084`

#define FRANKENSTEIN_HANDLER_AWAKE 0xa594u
#define FRANKENSTEIN_HEALTH 0x0096u
#define FRANKENSTEIN_COLLIDE_AWAKE 0x0003u
#define FRANKENSTEIN_NOT_ON_TOP 0xfff7u    // `ACTOR_SORT_FIRST` off

// Its throw.
#define FRANKENSTEIN_THROWS_UNDER 0x000cu
#define FRANKENSTEIN_THROW_SOUND 0x002fu
#define FRANKENSTEIN_THING_LIVES 0x0055u
#define FRANKENSTEIN_THING_COLLIDE 0x0009u
#define FRANKENSTEIN_THING_BANK 0x0090u
#define FRANKENSTEIN_THING_DRAWN 0x8000u
#define FRANKENSTEIN_THING_FLIPS 0xfff9u   // `ACTOR_FLIP` off, before its way's
#define W_FRANKENSTEIN_OWNER 0x0008u       // what a thing keeps as its thread
#define W_FRANKENSTEIN_SCREEN_X 0x1b6au
#define W_FRANKENSTEIN_SCREEN_Y 0x1b6cu
#define FRANKENSTEIN_SCREEN_W 0x0140u
#define FRANKENSTEIN_SCREEN_H 0x00f0u

// How far away it looks, decides and gives up.
#define FRANKENSTEIN_LOOKS 0x0140u
#define FRANKENSTEIN_LOOKS_PLAYER 0x0168u
#define FRANKENSTEIN_GIVES_UP 0x012cu
#define FRANKENSTEIN_ATTACKS 0x0050u
#define FRANKENSTEIN_ATTACK_ENDS 0x0060u
#define FRANKENSTEIN_STUCK_LIMIT 0x0064u
#define FRANKENSTEIN_PICTURE_FRAMES 0x0008u
#define FRANKENSTEIN_ATTACK_FRAMES 0x0002u
#define FRANKENSTEIN_MIRRORED_FROM 0x0030u  // a picture past this is mirrored
#define FRANKENSTEIN_MIRROR 0x0002u
#define FRANKENSTEIN_PICTURE_BANK_AWAKE 0x0090u

// Its death.
#define FRANKENSTEIN_KILLS 0x1f7eu        // how many of its kind were killed
#define W_FRANKENSTEIN_TALLY 0x1fe8u      // ...and by whom, by the score slot
#define FRANKENSTEIN_DEAD_BANK 0x008fu
#define FRANKENSTEIN_CRY 0x0021u
#define FRANKENSTEIN_LAST_PICTURES 0xa57eu

// Its tables, in its own bank.
#define FRANKENSTEIN_STEPS 0x9ecbu        // a pair of words a way, by the way
                                     // times two
#define FRANKENSTEIN_THINGS_LOOK 0x9f0fu  // a picture and flags, by a way
                                         // times four
#define FRANKENSTEIN_THINGS_FROM 0xa047u  // where one is thrown from, likewise
#define FRANKENSTEIN_THING_STEPS 0xa125u  // ...and its move
#define FRANKENSTEIN_THING_PICTURES 0xa149u
#define FRANKENSTEIN_THING_ORDER 0xa07cu  // which moves, by the frame
#define FRANKENSTEIN_ATTACKS_BY_WAY 0xa326u
#define FRANKENSTEIN_PICTURES 0xa3c8u     // by the way times four and the
                                         // picture

// The straight runs of the listing a stretch is made of, each named for where
// it starts, with where it ends. `tools/price_runs.py` prices them. Three
// are `$80:9D6A`'s.
#define FRANKENSTEIN_RUNS(X) \
  X(9D6A, 0x809d72u) \
  X(9D72, 0x809d74u) \
  X(9D74, 0x809d75u) \
  X(9F33, 0x819f3du) \
  X(9F3D, 0x819f48u) \
  X(9F48, 0x819f50u) \
  X(9F50, 0x819f59u) \
  X(9F59, 0x819f65u) \
  X(9F65, 0x819f70u) \
  X(9F70, 0x819f76u) \
  X(9F76, 0x819f7fu) \
  X(9F7F, 0x819f8bu) \
  X(9F8B, 0x819f96u) \
  X(9F96, 0x819f9au) \
  X(9F9A, 0x819f9du) \
  X(9F9D, 0x819fa6u) \
  X(9FA6, 0x819fa9u) \
  X(9FA9, 0x819fb0u) \
  X(9FB0, 0x819fb8u) \
  X(9FB8, 0x819fbfu) \
  X(9FBF, 0x819fc7u) \
  X(9FC7, 0x819fcau) \
  X(9FCF, 0x819fd2u) \
  X(9FD2, 0x819fdau) \
  X(9FDA, 0x819fe1u) \
  X(9FE2, 0x819fe6u) \
  X(9FEA, 0x81a047u) \
  X(A06B, 0x81a07bu) \
  X(A084, 0x81a08du) \
  X(A08D, 0x81a090u) \
  X(A090, 0x81a096u) \
  X(A096, 0x81a0b8u) \
  X(A0B8, 0x81a0cdu) \
  X(A0CD, 0x81a0d4u) \
  X(A0D4, 0x81a108u) \
  X(A108, 0x81a115u) \
  X(A115, 0x81a117u) \
  X(A11B, 0x81a125u) \
  X(A19F, 0x81a1afu) \
  X(A1AF, 0x81a1b4u) \
  X(A1B4, 0x81a1b7u) \
  X(A1B7, 0x81a1d5u) \
  X(A1D5, 0x81a1d8u) \
  X(A1D8, 0x81a1e4u) \
  X(A1E4, 0x81a1f5u) \
  X(A1F5, 0x81a207u) \
  X(A207, 0x81a20du) \
  X(A20D, 0x81a210u) \
  X(A210, 0x81a234u) \
  X(A234, 0x81a236u) \
  X(A236, 0x81a23au) \
  X(A23A, 0x81a251u) \
  X(A251, 0x81a253u) \
  X(A253, 0x81a264u) \
  X(A264, 0x81a267u) \
  X(A267, 0x81a26du) \
  X(A26D, 0x81a27au) \
  X(A27A, 0x81a27fu) \
  X(A27F, 0x81a282u) \
  X(A284, 0x81a292u) \
  X(A292, 0x81a2a1u) \
  X(A2A1, 0x81a2a9u) \
  X(A2A9, 0x81a2c0u) \
  X(A2C0, 0x81a2c7u) \
  X(A2C7, 0x81a2d0u) \
  X(A2D1, 0x81a2dcu) \
  X(A2DC, 0x81a2f1u) \
  X(A2F1, 0x81a2f9u) \
  X(A2F9, 0x81a308u) \
  X(A308, 0x81a30cu) \
  X(A30C, 0x81a311u) \
  X(A311, 0x81a31eu) \
  X(A31E, 0x81a325u) \
  X(A362, 0x81a366u) \
  X(A366, 0x81a373u) \
  X(A373, 0x81a376u) \
  X(A376, 0x81a384u) \
  X(A384, 0x81a385u) \
  X(A385, 0x81a388u) \
  X(A388, 0x81a38cu) \
  X(A38C, 0x81a3b4u) \
  X(A3B4, 0x81a3beu) \
  X(A3BE, 0x81a3c8u) \
  X(A484, 0x81a492u) \
  X(A496, 0x81a499u) \
  X(A499, 0x81a4b1u) \
  X(A4B1, 0x81a4b6u) \
  X(A4BA, 0x81a4bdu) \
  X(A4BD, 0x81a4c5u) \
  X(A4C5, 0x81a4cdu) \
  X(A4CD, 0x81a4d0u) \
  X(A4D0, 0x81a4d3u) \
  X(A4D3, 0x81a4d7u) \
  X(A4D7, 0x81a4e2u) \
  X(A4E6, 0x81a4eau) \
  X(A4F5, 0x81a504u) \
  X(A504, 0x81a518u) \
  X(A51C, 0x81a51fu)

typedef enum {
#define X(from, to) FRANKENSTEIN_RUN_##from,
  FRANKENSTEIN_RUNS(X)
#undef X
  FRANKENSTEIN_RUN_COUNT,
  // Any `RTS`: they all cost the same.
  FRANKENSTEIN_RUN_RTS = FRANKENSTEIN_RUN_A384,
} FrankensteinRun;

// The most of each call that one stretch makes.
#define FRANKENSTEIN_MAX_CALLS 4

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t runs[FRANKENSTEIN_RUN_COUNT];
  uint16_t taken;     // branches taken
  uint16_t draws[2];  // `rng_next`, by its overflow
  ActorNearestWork nearest[FRANKENSTEIN_MAX_CALLS];  // `actor_nearest`
  int nearest_count;
  ActorBearingRegs bearings[FRANKENSTEIN_MAX_CALLS];  // `actor_bearing`
  int bearing_count;
  PlayerPickRegs players[FRANKENSTEIN_MAX_CALLS];  // `player_bearing`
  int player_count;
  ActorSnapRegs snaps[FRANKENSTEIN_MAX_CALLS];  // `actor_snap_to`
  int snap_count;
  TerrainRegs grounds[FRANKENSTEIN_MAX_CALLS];  // `terrain_blocked_enemy`
  int ground_count;
  AtPointWork at_points[FRANKENSTEIN_MAX_CALLS];  // `actor_at_point`
  int at_point_count;
  TerrainRegs thing_grounds[FRANKENSTEIN_MAX_CALLS];  // `terrain_point_bit2`
  int thing_ground_count;
  uint16_t handlers_set;  // `thread_set_handler`
  // Overflow is the ROM's at the end, which it is not after a call that
  // does not say what it left there.
  bool v_known;
} FrankensteinWork;

// True if `state` is one of its four.
bool frankenstein_state_known(uint16_t state);

// True if the stretch that begins at `pc`, with the stack at `s`, comes back
// through return addresses this port knows.
bool frankenstein_stack_known(const Wram* w, uint32_t pc, uint16_t s);

// Run the stretch that begins at `c->pc`.
void frankenstein_run(Wram* w, const Rom* rom, PortCpu* c, FrankensteinWork* k);

#endif
