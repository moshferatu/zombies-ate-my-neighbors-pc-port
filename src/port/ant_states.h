// The giant ant's states: how it walks when it is not chasing anyone.
//
// `port/ant_thread.h` is the thread, which goes each frame to the
// routine whose address is at `$12`. `port/chase.h` is one of those. These
// are the other three, with what they share:
//
//     walking       `$81:BE14`  a step the way it faces
//     going round   `$81:BE71`  along whatever stopped it, keeping it on
//                               its left
//     marching      `$81:BFCD`  the way it was sent, leaping what it can
//
// **Walking** is one step. When the step is refused, by the ground or by
// somebody standing there, it starts going round.
//
// **Going round** is how it gets past a wall. Each frame it first asks
// whether it could step to its left. If it can, it turns that way: the wall
// has ended. Then it steps the way it faces, and if that is refused it turns
// to its right instead and tries again next frame. Five left turns running
// with nothing in the way means it is circling something, and it wanders off
// a random way.
//
// **Marching** is what the kind that walks in from the level's edge does. A
// step is refused by the ground or by the edge. At the edge it wanders off.
// At ground it looks for something to leap, as the chase does, and waits
// where it is when there is nothing.
//
// **A leap** (`$81:BCF1`) is over in the frame it begins. It lands past the
// tile it leapt, and then 8 pixels further for as long as the ground there
// is solid. Its pictures are the ROM's call to make: the stretch stops on
// the `JSL pictures_play` when the way it faces has any, and `ant_lands`
// is where that comes back.
//
// **How one is set up** is here too, since it is what chooses the state it
// starts in. Every copy of the thread begins with `$81:B9F9`: it stands
// where it was spawned, with its first picture, holding nothing. Then it is
// made one of two kinds, the usual one at `$81:BA46` and a faster, tougher
// one at `$81:BA78`. The copy that marches comes in at the top of the
// screen, and `$81:BFA8` is whether there is room for it there.
//
// **What it picks up and puts down.** An ant that has caught somebody
// holds them as a record of its own, shown by a picture for their kind
// (`$81:C050`). A killed one puts them back as what they were (`$81:C0E5`).
// A kind with no picture, or nothing to be put back as, stops the ROM on a
// branch to itself, and that is the ROM's to do: `ant_state_supported`.
//
// **Written as stretches**, like `port/boss_thread.h`: each makes the calls
// whose cost is known to the cycle. The rest are the ROM's instructions and
// the harness makes them: a record, a handler, pictures. Every stretch stops
// on an instruction of the ROM's, and `ant_state_run` leaves that
// address in `pc`.
//
// Port code: libc only.

#ifndef PORT_ANT_STATES_H
#define PORT_ANT_STATES_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/wram.h"

#define ANT_STATES_BANK 0x81u

// Fields on the ant's page, beyond those `port/ant.h` and
// `port/chase.h` name.
#define ANT_DP_LEFT_WAY 0x16        // the way to its left, while going round
#define ANT_DP_LEFT_TURNS 0x1a      // left turns running

#define ANT_DP_SPAWN_X 0x00          // where it was spawned
#define ANT_DP_SPAWN_Y 0x02
#define ANT_DP_HEALTH 0x22
#define ANT_DP_HITS 0x7e             // counted towards a stagger

// The states, as `$12` holds them.
#define ANT_STATE_GOING_ROUND 0xbe71u
#define ANT_STATE_LEAPING 0xbd01u
#define ANT_STATE_MARCHING 0xbfcdu

// A new one's record.
#define ANT_FIRST_PICTURE 0xf883u
#define ANT_COLLIDE_ID 0x0004u

// The two kinds: the usual one, and the faster one that takes more killing.
#define ANT_USUAL_ATTR 0x0c00u
#define ANT_USUAL_HEALTH 0x0009u
#define ANT_USUAL_STEPS 0xb991u
#define ANT_USUAL_PICTURES 0xba64u
#define ANT_FAST_HEALTH 0x0013u
#define ANT_FAST_PICTURES 0xba9fu

// One that marches starts facing down.
#define ANT_WAY_DOWN 0x000au

// What it holds: a record of its own, a little off the ground. Its picture
// and what it is put back as are by its kind, less the first kind there is.
#define ANT_HELD_FIRST_KIND 0x000cu
#define ANT_HELD_HEIGHT 0x0008u
#define ANT_HELD_PICTURES 0xc095u       // 0: none, and the ROM stops
#define ANT_HELD_META_BANK 0x008fu
#define ANT_PUT_BACK_AS 0xc11bu         // `$FFFF`: nothing, likewise

// A way is a direction doubled, 2 for up and round by the clock to 16. A
// quarter turn is two directions.
#define ANT_QUARTER_TURN 0x0004u
#define ANT_LEFT_TURNS_MOST 0x0005u

// A leap, by the way it faces times two. `PICTURES` and `FLAGS` are a pair:
// the pictures to play, or none, and what to do to its record's flags, which
// is to set the bits of a positive word and keep those of a negative one.
#define ANT_LEAP_PICTURES 0xbd72u
#define ANT_LEAP_FLAGS 0xbd74u
#define ANT_LEAP_LANDING 0xbdc6u      // where it lands, from where it is
#define ANT_LEAP_FURTHER 0xbdeau      // ...and each try past that

// Where each stretch begins.
#define ANT_SET_UP_PC 0x81b9fdu           // after its record
#define ANT_HOLDS_NOTHING_PC 0x81ba34u      // after its handler
#define ANT_USUAL_KIND_PC 0x81ba46u
#define ANT_FAST_KIND_PC 0x81ba78u
#define ANT_ROOM_PC 0x81bfa8u
#define ANT_MARCH_BEGINS_PC 0x81bfc2u
#define ANT_PICKS_UP_PC 0x81c054u         // after the record for it
#define ANT_PUTS_DOWN_PC 0x81c0e5u
#define ANT_PUT_DOWN_PC 0x81c105u         // after it is put back
#define ANT_HOLDS_NONE_PC 0x81c10bu       // after its record is freed
#define ANT_WANDERS_OFF_PC 0x81bce1u
#define ANT_LEAPS_PC 0x81bcf1u
#define ANT_LANDS_PC 0x81bd1eu      // after its pictures
#define ANT_WALKING_PC 0x81be14u
#define ANT_GOING_ROUND_PC 0x81be71u
#define ANT_MARCHING_PC 0x81bfcdu

// Where one stops: a call the harness makes, or an `RTS` to the thread.
#define ANT_NO_HANDLER_PC 0x81ba30u         // `JSL thread_set_handler`
#define ANT_SET_UP_RTS_PC 0x81ba45u
#define ANT_USUAL_PICTURES_PC 0x81ba5du // `JSL pictures_play`
#define ANT_FAST_PICTURES_PC 0x81ba98u
#define ANT_ROOM_RTS_PC 0x81bfc1u
#define ANT_MARCH_BEGUN_RTS_PC 0x81bfccu
#define ANT_PUT_BACK_PC 0x81c101u           // `JSL $80C97F`
#define ANT_FREE_HELD_PC 0x81c107u          // `JSL actor_slot_free`
#define ANT_PUT_DOWN_RTS_PC 0x81c11au
#define ANT_LEAP_PICTURES_PC 0x81bd1au      // `JSL pictures_play`
#define ANT_WALK_BEGUN_RTS_PC 0x81be13u
#define ANT_WALKED_RTS_PC 0x81be3du
#define ANT_ROUND_BEGUN_RTS_PC 0x81be70u
#define ANT_WENT_ROUND_RTS_PC 0x81bed6u
#define ANT_MARCHED_RTS_PC 0x81bffbu

// The straight runs of the listing a stretch is made of, each named for where
// it starts, with where it ends. `tools/price_runs.py` prices them.
#define ANT_RUNS(X) \
  X(B9FD, 0x81ba30u) \
  X(BA34, 0x81ba45u) \
  X(BA46, 0x81ba5du) \
  X(BA78, 0x81ba98u) \
  X(BC05, 0x81bc14u) \
  X(BC14, 0x81bc22u) \
  X(BC22, 0x81bc23u) \
  X(BC23, 0x81bc32u) \
  X(BC32, 0x81bc3cu) \
  X(BC3D, 0x81bc5cu) \
  X(BC5C, 0x81bc7du) \
  X(BC7D, 0x81bc7fu) \
  X(BC7F, 0x81bc97u) \
  X(BC97, 0x81bc99u) \
  X(BCE1, 0x81bcf1u) \
  X(BCF1, 0x81bd01u) \
  X(BD01, 0x81bd0au) \
  X(BD0A, 0x81bd0fu) \
  X(BD0F, 0x81bd12u) \
  X(BD12, 0x81bd1au) \
  X(BD1E, 0x81bd34u) \
  X(BD34, 0x81bd3eu) \
  X(BD3E, 0x81bd52u) \
  X(BD52, 0x81bd72u) \
  X(BE0E, 0x81be13u) \
  X(BE14, 0x81be2bu) \
  X(BE2B, 0x81be2du) \
  X(BE2D, 0x81be3du) \
  X(BE3E, 0x81be41u) \
  X(BE41, 0x81be55u) \
  X(BE69, 0x81be70u) \
  X(BE71, 0x81be95u) \
  X(BE95, 0x81be97u) \
  X(BE97, 0x81bea0u) \
  X(BEA0, 0x81bea3u) \
  X(BEA3, 0x81beabu) \
  X(BEAB, 0x81beadu) \
  X(BEAD, 0x81bec4u) \
  X(BEC4, 0x81bec6u) \
  X(BEC6, 0x81bed6u) \
  X(BED7, 0x81bedau) \
  X(BFCD, 0x81bfe9u) \
  X(BFE9, 0x81bfebu) \
  X(BFEB, 0x81bffbu) \
  X(BFA8, 0x81bfb6u) \
  X(BFB6, 0x81bfc0u) \
  X(BFC0, 0x81bfc1u) \
  X(BFC2, 0x81bfccu) \
  X(BFFC, 0x81bfffu) \
  X(BFFF, 0x81c003u) \
  X(C003, 0x81c006u) \
  X(C006, 0x81c008u) \
  X(C008, 0x81c00bu) \
  X(C054, 0x81c07au) \
  X(C07A, 0x81c095u) \
  X(C0E5, 0x81c0fdu) \
  X(C0FD, 0x81c101u) \
  X(C105, 0x81c107u) \
  X(C10B, 0x81c11au)

// The calls and jumps between the stretches, which the harness makes: see
// `CosimRoutine::link`. The first thread's two calls to be set up, and
// `JMP $BCE1` after each kind's pictures.
#define ANT_STATE_LINKS(X) \
  X(c201_set_up, "$81:C1FB", 0x81c1fbu) \
  X(c201_kind, "$81:C1FE", 0x81c1feu) \
  X(usual_kind_wanders, "$81:BA61", 0x81ba61u) \
  X(fast_kind_wanders, "$81:BA9C", 0x81ba9cu)

typedef enum {
#define X(from, to) ANT_RUN_##from,
  ANT_RUNS(X)
#undef X
  ANT_RUN_COUNT,
  ANT_RUN_RTS = ANT_RUN_BC22,          // any `RTS`: they all cost the same
} AntRun;

// The most of each that one stretch makes.
#define ANT_MAX_AT_POINTS 2

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t runs[ANT_RUN_COUNT];
  uint16_t taken;  // branches taken
  // The ground tests, by whether the ground was solid and how many of the
  // six tiles each looked at. A leap makes as many as it takes to land.
  uint16_t grounds[2][TERRAIN_PROBE_COUNT + 1];
  AtPointWork at_points[ANT_MAX_AT_POINTS];      // `actor_at_point`
  int at_point_count;
  BoundsExit edge;  // `terrain_out_of_bounds`, if it `asked_edge`
  bool asked_edge;
  TerrainRegs room;  // `terrain_blocked`, if it `asked_room`
  bool asked_room;
  int tiles;        // `tile_attrs_at_pixel`
  bool draw_twice;  // `rng_next`'s overflow, if it `drew`
  bool drew;
  // Overflow is the ROM's at the end, which it is not after a call that
  // does not say what it left there.
  bool v_known;
} AntStatesWork;

// True if `pc` is where one of the stretches begins.
bool ant_state_begins_at(uint32_t pc);

// False where the ROM would stop on a branch to itself: the stretch at `pc`
// is about what the ant on `page` holds, and that is of a kind with no
// picture, or with nothing to be put back as.
bool ant_state_supported(const Wram* w, const Rom* rom, uint16_t page,
                         uint32_t pc);

// Run the stretch that begins at `c->pc`.
void ant_state_run(Wram* w, const Rom* rom, PortCpu* c,
                   AntStatesWork* k);

#endif
