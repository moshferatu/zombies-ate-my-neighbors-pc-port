// The purple tentacle of the bonus levels: its thread, from coming in at the
// side of the screen to being taken away.
//
// `$82:990F` is the routine records 0 and 51 name for every creature they
// place, and `password-bcdf` reaches it too. It is the zombie's small state
// machine again (`port/zombie.h`), on a page of its own layout:
//
//     walking     `$82:97E6`  a step the way it faces
//     following   `$82:9814`  along whatever stopped it, keeping it on its
//                             left
//     chasing     `$82:9845`  a step towards whoever is nearest, or two
//
// **It comes in** 8 pixels off the left of the screen or 8 off the right, by
// a draw, on the row it was spawned at. Solid ground there, or the level's
// edge, and it does not come in at all.
//
// **Each frame** it sleeps two ticks, runs its state, and every third frame
// shows the next of four pictures for the way it faces. After the fourth it
// rests twelve ticks, and about one rest in twenty-five it looks round.
//
// **Walking and following** begin by asking who is nearest. Somebody within
// `$60` and it chases, though this frame's step is still the walk's. Nobody
// within `$D0` and it asks whether either player is within the reach at
// `$82:97A9`: if not, it leaves.
//
// **Walking** is one step of 2 pixels. Refused, by the ground or by somebody
// standing there, it turns a quarter to its right and follows.
//
// **Following** first asks whether it could step to its left, and turns that
// way if so: what it was following has ended. Then it steps ahead, and turns
// right when that is refused.
//
// **Chasing** stops when nobody is within `$70`. Otherwise it steps towards
// them, twice on about half its frames. The step is taken an axis at a
// time, across and then down, each where the ground and whoever stands
// there let it. So it slides along a wall it cannot go through. Which way
// they are is asked of its record, which is where it was shown last and not
// where this frame's first step left it.
//
// **It ends** when `$0A` is set. Its handler (`enemy_9a6d_collide`, in
// `port/collide.h`) sets it for a kill and leaves what hit it at `$20`, and
// then it pays `$0200`, cries out and plays its death. Nobody near sets it
// with `$20` clear, and it goes with none of that. The award and the end
// itself are two of `port/begin.h`'s, at `$82:993F` and `$82:9969`, and a
// frame stops where the first begins.
//
// **Written as stretches**, like `port/ant_states.h`: each makes the calls
// whose cost is known to the cycle, and stops on the ones that are not. Those
// are its record, its handler, its sleep, its cry and its pictures, and the
// harness makes them. `tentacle_run` leaves the address it
// stopped on in `pc`.
//
// Port code: libc only.

#ifndef PORT_TENTACLE_H
#define PORT_TENTACLE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/wram.h"

#define TENTACLE_BANK 0x82u

// Its page.
#define TENTACLE_DP_SPAWN_X 0x00
#define TENTACLE_DP_SPAWN_Y 0x02
#define TENTACLE_DP_RECORD 0x08
#define TENTACLE_DP_LEAVE 0x0a       // set, and the thread ends
#define TENTACLE_DP_HEALTH 0x0c
#define TENTACLE_DP_X 0x0e
#define TENTACLE_DP_Y 0x10
#define TENTACLE_DP_NEXT_X 0x12      // the point being tried
#define TENTACLE_DP_NEXT_Y 0x14
#define TENTACLE_DP_WAY 0x16         // a direction doubled: 2 for up, round
                                     // by the clock to 16
#define TENTACLE_DP_LEFT_WAY 0x18    // the way to its left, while following
#define TENTACLE_DP_STATE 0x1a
#define TENTACLE_DP_TIMER 0x1c       // frames until its next picture
#define TENTACLE_DP_PHASE 0x1e       // which of the four that is
#define TENTACLE_DP_HIT_ID 0x20      // what killed it, or 0 for nothing
#define TENTACLE_DP_TARGET 0x22      // who it chases
#define TENTACLE_DP_HITS 0x7e

// The states, as `$1A` holds them.
#define TENTACLE_STATE_WALKING 0x97e6u
#define TENTACLE_STATE_FOLLOWING 0x9814u
#define TENTACLE_STATE_CHASING 0x9845u

// A new one's record, and what it is set up with.
#define TENTACLE_FIRST_PICTURE 0xeb59u
#define TENTACLE_META_BANK 0x008fu
#define TENTACLE_COLLIDE_ID 0x0003u
#define TENTACLE_ATTR 0x0c00u
#define TENTACLE_HEALTH 0x0001u
#define TENTACLE_HANDLER 0x9a6du

// What one adds to `W_SPAWN_LOAD`, and takes off as it ends.
#define TENTACLE_WEIGHT 0x0012u

// How near somebody has to be for it to chase, how far before it stops, and
// how far before it asks whether a player is about at all.
#define TENTACLE_SEES 0x0060u
#define TENTACLE_LOSES 0x0070u
#define TENTACLE_FAR 0x00d0u
// ...which it asks with the operand of `LDA #$00D0` at `$82:97A9`. Read from
// the cartridge because a wider picture widens it (`src/widescreen.h`).
#define TENTACLE_REACH_AT 0x8297aau

// Its tables, in its own bank. The two wings are words from the camera's
// column, and a wider picture moves those too.
#define TENTACLE_STEPS 0x96d9u       // a pair of words a way, by the way
                                     // times two
#define TENTACLE_WINGS 0x990bu
#define TENTACLE_PICTURES 0x99f3u    // by the way times four and the phase
                                     // times two
// From this row of the pictures on, it is drawn mirrored.
#define TENTACLE_FIRST_MIRRORED 0x0030u

#define TENTACLE_QUARTER_TURN 0x0004u
#define TENTACLE_PICTURE_FRAMES 0x0002u
#define TENTACLE_REST_TICKS 0x000cu
// A rest's draw under this, and it looks round.
#define TENTACLE_LOOKS_UNDER 0x000au
#define TENTACLE_LOOK_PICTURES 0x9a3bu

// A killed one: how many have been, its cry and its pictures.
#define W_TENTACLES_KILLED 0x1f86
#define TENTACLE_CRY 0x0021u
#define TENTACLE_DEATH_PICTURES 0x997cu

// Where each stretch begins.
#define TENTACLE_COMES_IN_PC 0x829913u     // after `JSL spawn_has_room`
#define TENTACLE_SET_UP_PC 0x82988du       // after its record
#define TENTACLE_SETTLES_PC 0x8298e4u      // after its handler
#define TENTACLE_WAKES_PC 0x829930u        // after its sleep
#define TENTACLE_SCORED_PC 0x82994au       // after the award
#define TENTACLE_CRIED_PC 0x829962u        // after its cry
#define TENTACLE_RESTED_PC 0x8299dfu       // after a rest
#define TENTACLE_LOOKED_PC 0x8299f1u       // after looking round

// Where one stops: a call or a return the harness makes.
#define TENTACLE_GONE_PC 0x82997bu         // `RTL`: there was no room
#define TENTACLE_RECORD_PC 0x829889u       // `JSL actor_slot_alloc`
#define TENTACLE_HANDLER_PC 0x8298e0u      // `JSL thread_set_handler`
#define TENTACLE_SLEEP_PC 0x82992cu        // `JSL thread_yield`
#define TENTACLE_DIES_PC 0x82993fu         // `LDX #$0200`: `port/begin.h`'s
#define TENTACLE_CRY_PC 0x82995eu          // `JSL apu_play_sfx`
#define TENTACLE_DEATH_PC 0x829965u        // `JSL pictures_play`
#define TENTACLE_REST_PC 0x8299dbu         // `JSL thread_yield`
#define TENTACLE_LOOK_PC 0x8299edu         // `JSL pictures_play`

// The straight runs of the listing a stretch is made of, each named for where
// it starts, with where it ends. `tools/price_runs.py` prices them.
#define TENTACLE_RUNS(X) \
  X(96FD, 0x829707u) \
  X(9707, 0x829713u) \
  X(9713, 0x829714u) \
  X(9715, 0x829740u) \
  X(9740, 0x82974cu) \
  X(974C, 0x829750u) \
  X(9751, 0x82975bu) \
  X(975B, 0x829767u) \
  X(9767, 0x82976bu) \
  X(976B, 0x829775u) \
  X(9775, 0x829781u) \
  X(9781, 0x829785u) \
  X(9786, 0x829793u) \
  X(9793, 0x829798u) \
  X(9798, 0x8297b7u) \
  X(97B7, 0x8297b9u) \
  X(97BA, 0x8297bdu) \
  X(97BE, 0x8297d0u) \
  X(97D0, 0x8297e0u) \
  X(97E0, 0x8297e6u) \
  X(97E6, 0x8297e9u) \
  X(97E9, 0x829800u) \
  X(9800, 0x829802u) \
  X(9802, 0x82980bu) \
  X(980B, 0x82980eu) \
  X(980E, 0x829814u) \
  X(9814, 0x829817u) \
  X(9817, 0x82981au) \
  X(981A, 0x829831u) \
  X(9831, 0x829833u) \
  X(9833, 0x82983cu) \
  X(983C, 0x82983fu) \
  X(983F, 0x829845u) \
  X(9845, 0x829854u) \
  X(9854, 0x829857u) \
  X(9857, 0x829860u) \
  X(9860, 0x829863u) \
  X(9863, 0x829883u) \
  X(9884, 0x829887u) \
  X(9887, 0x829889u) \
  X(988D, 0x8298e0u) \
  X(98E4, 0x8298e6u) \
  X(98E6, 0x829900u) \
  X(9900, 0x82990au) \
  X(9913, 0x829915u) \
  X(9915, 0x829918u) \
  X(9918, 0x82991au) \
  X(991A, 0x829927u) \
  X(9927, 0x82992cu) \
  X(9930, 0x829938u) \
  X(9938, 0x82993bu) \
  X(993B, 0x82993fu) \
  X(994A, 0x82995eu) \
  X(9962, 0x829965u) \
  X(9994, 0x829998u) \
  X(9998, 0x8299b4u) \
  X(99B4, 0x8299b9u) \
  X(99B9, 0x8299bcu) \
  X(99BC, 0x8299cbu) \
  X(99CB, 0x8299d8u) \
  X(99D8, 0x8299dbu) \
  X(99DF, 0x8299eau) \
  X(99EA, 0x8299edu) \
  X(99F1, 0x8299f3u)

typedef enum {
#define X(from, to) TENTACLE_RUN_##from,
  TENTACLE_RUNS(X)
#undef X
  TENTACLE_RUN_COUNT,
  TENTACLE_RUN_RTS = TENTACLE_RUN_9713,  // any `RTS`: they all cost the same
} TentacleRun;

// The most of each that one stretch makes: a chase of two steps asks who is
// standing there twice a step.
#define TENTACLE_MAX_AT_POINTS 4
#define TENTACLE_MAX_BEARINGS 2

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t runs[TENTACLE_RUN_COUNT];
  uint16_t taken;  // branches taken
  // The ground tests, by whether the ground was solid and how many of the
  // six tiles each looked at.
  uint16_t grounds[2][TERRAIN_PROBE_COUNT + 1];
  AtPointWork at_points[TENTACLE_MAX_AT_POINTS];  // `actor_at_point`
  int at_point_count;
  TerrainRegs room;  // `terrain_blocked`, if it `asked_room`
  bool asked_room;
  BoundsExit edge;  // `terrain_out_of_bounds`, if it `asked_edge`
  bool asked_edge;
  bool draw_twice;  // `rng_next`'s overflow, if it `drew`
  bool drew;
  ActorNearestWork nearest;  // `actor_nearest`, if it `sought`
  bool sought;
  ActorSnapRegs snap;      // `actor_snap_to` and `player_bearing`, if nobody
  PlayerPickRegs players;  // was near and it `asked_players`
  bool asked_players;
  ActorBearingRegs bearings[TENTACLE_MAX_BEARINGS];  // `actor_bearing`
  int bearing_count;
  // Overflow is the ROM's at the end, which it is not after a call that
  // does not say what it left there.
  bool v_known;
} TentacleWork;

// True if `pc` is where one of the stretches begins.
bool tentacle_begins_at(uint32_t pc);

// True if `state` is one of the three a tentacle can be in.
bool tentacle_state_known(uint16_t state);

// Run the stretch that begins at `c->pc`.
void tentacle_run(Wram* w, const Rom* rom, PortCpu* c, TentacleWork* k);

#endif
