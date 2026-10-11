// The Snakeoid: the mound that tunnels about levels 20, 40 and 47, as far as
// its coming up to bite.
//
// `$82:A8BB` and `$82:A8C3` are the routines those records name, and they
// differ in one word: which four tiles it churns the ground into. A picture
// of record 20 shows it: a mound of earth beside the player, and a track of
// churned ground behind it.
//
// **It waits** where it was put until a player is within `$DC`, asking every
// four ticks.
//
// **Each frame** after that it sleeps a tick and runs its state:
//
//     wandering   `$82:A451`  the way it faces, until somebody is near
//     chasing     `$82:A4C3`  towards them, and up at them when it is there
//
// **Both** begin by ageing its track. A piece of track is a record of its
// own, of four, and lasts 32 frames: every eighth it is shown flatter, and
// then it is freed.
//
// **It steps one frame in seven.** Every other step lays a piece of track
// where it is, if one of the four is free, and churns the four tiles about
// it that can be churned. Then it goes 6 pixels its way, an axis at a time,
// each where the ground lets it.
//
// **Wandering**, a step that was stopped on either axis ends one of two ways
// by a draw: a new way, or a hop. A hop looks along its way, 8 pixels at a
// time from 24 off, for the first place it could stand, and comes up there
// over 32 ticks. One step in seventeen, by another draw, it comes up beside
// a player instead.
//
// **Chasing** somebody nearer than `$A0`, it steps about twice as often. A
// stopped step is always a hop, and a hop with nowhere to land is the end
// of the chase. With them within 12, or beside one of the four places it
// bites from, it comes up: `$82:A538` and `$82:A56E`.
//
// **Who it goes for** is asked at `$82:9F16`, three ways by the two words at
// `$6E` and `$70`, which `port/frontend.h` knows as the two pads. Neither
// set, and it is the nearest record of a kind it eats. One set, and it is
// that player. Both, and it is the nearer player.
//
// **Not here**: its coming up and biting, and how it starts and dies. No
// session of the survey reaches the first, and the stretches stop where it
// begins. Its handler is `boss_aa2e_collide`, in `port/collide.h`.
//
// **Written as stretches**, like `port/tentacle.h`. The calls a stretch
// stops on are a record asked for, a record freed, and its sleeps. Two of
// those are inside routines its states call, so a stretch that begins after
// one finds its way back by the return addresses on the stack, as the ROM
// does.
//
// Port code: libc only.

#ifndef PORT_SNAKEOID_H
#define PORT_SNAKEOID_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/tile_put.h"
#include "port/wram.h"

#define SNAKEOID_BANK 0x82u

// Its page.
#define SNAKEOID_DP_RECORD 0x08
#define SNAKEOID_DP_TRACK 0x0a       // four pieces: a record, or `$FFFF`...
#define SNAKEOID_DP_TRACK_AGE 0x0c   // ...and the frames it has left
#define SNAKEOID_DP_DROP_IN 0x1a     // steps until it may drop a piece
#define SNAKEOID_DP_LANDS_X 0x24
#define SNAKEOID_DP_LANDS_Y 0x26
#define SNAKEOID_DP_X 0x28
#define SNAKEOID_DP_Y 0x2a
#define SNAKEOID_DP_AIM_X 0x2c       // where a step would take it, or a
#define SNAKEOID_DP_AIM_Y 0x2e       // hop's stride
#define SNAKEOID_DP_TRY_X 0x30       // the point being asked about
#define SNAKEOID_DP_TRY_Y 0x32
#define SNAKEOID_DP_COUNT 0x34
#define SNAKEOID_DP_WAY 0x36         // a direction doubled: 2 for up, round
                                     // by the clock to 16
#define SNAKEOID_DP_STATE 0x38
#define SNAKEOID_DP_SIDE 0x3a        // which place it bites from, or negative
#define SNAKEOID_DP_TARGET 0x3c
#define SNAKEOID_DP_PAUSE 0x42       // frames until its next step
#define SNAKEOID_DP_DYING 0x46
#define SNAKEOID_DP_HIT_BY 0x4e
#define SNAKEOID_DP_GROUND 0x50      // 0 or `$20`: which tiles it churns
#define SNAKEOID_DP_GAP 0x54

#define SNAKEOID_TRACK_PIECES 4
#define SNAKEOID_TRACK_STRIDE 4
#define SNAKEOID_NO_PIECE 0xffffu

// The states, as `$38` holds them.
#define SNAKEOID_STATE_WANDERING 0xa451u
#define SNAKEOID_STATE_CHASING 0xa4c3u

// A piece of track.
#define SNAKEOID_TRACK_FRAMES 0x0020u
#define SNAKEOID_MOUND_PICTURE 0xee42u
#define SNAKEOID_META_BANK 0x008fu
#define SNAKEOID_TRACK_PICTURES 0x9debu  // by its age over four, less two
// The list of tile changes is this long, in bytes, and it churns nothing.
#define SNAKEOID_CHURN_LIST_FULL 0x0014u
// Four rows of four words: a tile across and down from the last, and what
// it becomes. The second four are for the other ground.
#define SNAKEOID_CHURN 0x9eb1u
#define SNAKEOID_CHURN_LAST_ROW 0x0018u
#define SNAKEOID_CHURNS 4
// A tile that can be churned has this bit.
#define SNAKEOID_TILE_SOFT 0x0080u

// Its tables, in its own bank.
#define SNAKEOID_STEPS 0x9aaeu       // a pair of words a way, by the way
                                     // times two
#define SNAKEOID_HOP_FROM 0xa10eu    // where a hop begins looking...
#define SNAKEOID_HOP_STRIDE 0xa132u  // ...and how far apart
#define SNAKEOID_HOP_TRIES 0x0019u
#define SNAKEOID_RISE 0xa200u        // a picture and how long, four times
#define SNAKEOID_BESIDE 0x9fb7u      // where it makes for, by the way to who
#define SNAKEOID_WAYS 0x82a026u      // a way, by the two compares
#define SNAKEOID_BITES_FROM 0xa382u  // the four places beside it
#define SNAKEOID_BITE_LAST 0x000cu

// How near a player has to be to wake it, how near anybody for it to chase,
// and for it to go on chasing.
#define SNAKEOID_WAKES_AT 0x00dcu
#define SNAKEOID_SEES 0x00b4u
#define SNAKEOID_KEEPS 0x00dcu
#define SNAKEOID_BITES_AT 0x000cu
#define SNAKEOID_BESIDE_AT 0x000eu
#define SNAKEOID_HURRIES_AT 0x00a0u
#define SNAKEOID_STEP_FRAMES 0x0006u
// The draws: a step is a leap at a player under the first, and a stopped
// step is a hop under the second.
#define SNAKEOID_LEAPS_UNDER 0x000fu
#define SNAKEOID_HOPS_UNDER 0x0064u
// The kinds of record it goes for with neither word set, and the kinds a
// player's record can be for it to leap at them.
#define SNAKEOID_EATS_A 0x0038u
#define SNAKEOID_EATS_B 0x0001u
#define SNAKEOID_PLAYER_KIND_A 0x0005u
#define SNAKEOID_PLAYER_KIND_B 0x0006u

// A word for each player.
#define W_SNAKEOID_SIDE_A 0x006eu
#define W_SNAKEOID_SIDE_B 0x0070u
// Where `$82:9FFB` keeps the point it is asked the way to.
#define W_SNAKEOID_TO_X 0x00a2u
#define W_SNAKEOID_TO_Y 0x00a4u

// Where each stretch begins.
#define SNAKEOID_READY_PC 0x82a8bau      // the `RTS` after its handler is set
#define SNAKEOID_WAITED_PC 0x82a43au     // after a sleep of its wait
#define SNAKEOID_WAKES_PC 0x82a8efu      // after a frame's sleep
#define SNAKEOID_FREED_PC 0x829de7u      // after a piece of track is freed
#define SNAKEOID_DROPPED_PC 0x829e18u    // after a record for a piece
#define SNAKEOID_ROSE_PC 0x82a1f8u       // after a tick of its coming up

// Where one stops: a call the harness makes, or what is not ported.
#define SNAKEOID_WAIT_PC 0x82a436u       // `JSL thread_yield`
#define SNAKEOID_SLEEP_PC 0x82a8ebu      // `JSL thread_yield`
#define SNAKEOID_RISE_PC 0x82a1f4u       // `JSL thread_yield`
#define SNAKEOID_FREE_PC 0x829de3u       // `JSL actor_slot_free`
#define SNAKEOID_RECORD_PC 0x829e14u     // `JSL actor_slot_alloc`
#define SNAKEOID_DIES_PC 0x82a8f3u       // `LDX #$2000`: `port/begin.h`'s
#define SNAKEOID_BITES_PC 0x82a538u      // it comes up under them
#define SNAKEOID_BITES_BESIDE_PC 0x82a56eu

// The return addresses a stretch can find on the stack, as `RTS` pulls them.
#define SNAKEOID_RET_WAIT 0xa8dau        // `JSR $A425`
#define SNAKEOID_RET_READY 0xa8d7u       // `JSR $A82F`
#define SNAKEOID_RET_WAY 0xa8ddu         // `JSR $A43D`
#define SNAKEOID_RET_STATE 0xa8e7u       // the `PEA` before its state
#define SNAKEOID_RET_AGED 0xa423u        // `JSR $9DA7`, of a frame
#define SNAKEOID_RET_AGED_RISING 0xa1fau // ...and of a tick of coming up
#define SNAKEOID_RET_TICK_WANDER 0xa453u // `JSR $A418`
#define SNAKEOID_RET_TICK_CHASE 0xa4c5u
#define SNAKEOID_RET_DROP_WANDER 0xa476u // `JSR $9DF7`
#define SNAKEOID_RET_DROP_CHASE 0xa4f9u
#define SNAKEOID_RET_RISE_HOP 0xa105u    // `JSR $A1D4`, of a hop
#define SNAKEOID_RET_RISE_LEAP 0xa1adu   // ...and of a leap
#define SNAKEOID_RET_HOP_WANDER 0xa4b2u  // `JSR $A0C7`
#define SNAKEOID_RET_HOP_CHASE 0xa52cu
#define SNAKEOID_RET_LEAP 0xa470u        // `JSR $A156`

// The straight runs of the listing a stretch is made of, each named for where
// it starts, with where it ends. `tools/price_runs.py` prices them.
#define SNAKEOID_RUNS(X) \
  X(9DA7, 0x829dadu) \
  X(9DAD, 0x829db4u) \
  X(9DB4, 0x829dbbu) \
  X(9DBB, 0x829dc0u) \
  X(9DC0, 0x829dc8u) \
  X(9DC8, 0x829dc9u) \
  X(9DC9, 0x829ddbu) \
  X(9DDB, 0x829de3u) \
  X(9DE7, 0x829debu) \
  X(9DF7, 0x829dfbu) \
  X(9DFB, 0x829dfeu) \
  X(9DFE, 0x829e05u) \
  X(9E05, 0x829e0du) \
  X(9E0E, 0x829e14u) \
  X(9E18, 0x829e56u) \
  X(9E57, 0x829e5fu) \
  X(9E5F, 0x829e72u) \
  X(9E72, 0x829e95u) \
  X(9E95, 0x829ea6u) \
  X(9EA6, 0x829eb0u) \
  X(9F16, 0x829f1au) \
  X(9F1A, 0x829f25u) \
  X(9F25, 0x829f2fu) \
  X(9F2F, 0x829f34u) \
  X(9F34, 0x829f36u) \
  X(9F36, 0x829f59u) \
  X(9F59, 0x829f5fu) \
  X(9F5F, 0x829f6cu) \
  X(9F6C, 0x829f74u) \
  X(9F74, 0x829f7cu) \
  X(9F7C, 0x829f84u) \
  X(9F84, 0x829f89u) \
  X(9F89, 0x829f8eu) \
  X(9F8E, 0x829f91u) \
  X(9F91, 0x829f99u) \
  X(9F99, 0x829f9du) \
  X(9F9D, 0x829fa7u) \
  X(9FA7, 0x829fabu) \
  X(9FAB, 0x829fb5u) \
  X(9FB5, 0x829fb7u) \
  X(9FFB, 0x82a00bu) \
  X(A00B, 0x82a012u) \
  X(A012, 0x82a019u) \
  X(A019, 0x82a01eu) \
  X(A01E, 0x82a026u) \
  X(A088, 0x82a096u) \
  X(A096, 0x82a0a0u) \
  X(A0A0, 0x82a0a9u) \
  X(A0A9, 0x82a0b3u) \
  X(A0B3, 0x82a0bdu) \
  X(A0BD, 0x82a0c6u) \
  X(A0C7, 0x82a0eau) \
  X(A0EA, 0x82a103u) \
  X(A103, 0x82a106u) \
  X(A106, 0x82a108u) \
  X(A108, 0x82a10cu) \
  X(A10C, 0x82a10eu) \
  X(A156, 0x82a163u) \
  X(A163, 0x82a16cu) \
  X(A16C, 0x82a171u) \
  X(A172, 0x82a182u) \
  X(A182, 0x82a186u) \
  X(A186, 0x82a19cu) \
  X(A19C, 0x82a1a0u) \
  X(A1A0, 0x82a1abu) \
  X(A1AB, 0x82a1aeu) \
  X(A1AF, 0x82a1b9u) \
  X(A1B9, 0x82a1c3u) \
  X(A1C3, 0x82a1d3u) \
  X(A1D4, 0x82a1d8u) \
  X(A1D8, 0x82a1deu) \
  X(A1DE, 0x82a1ecu) \
  X(A1EC, 0x82a1f0u) \
  X(A1F0, 0x82a1f4u) \
  X(A1F8, 0x82a1fbu) \
  X(A1FB, 0x82a1fdu) \
  X(A1FD, 0x82a1ffu) \
  X(A34A, 0x82a34fu) \
  X(A34F, 0x82a36eu) \
  X(A36E, 0x82a378u) \
  X(A378, 0x82a37bu) \
  X(A37B, 0x82a382u) \
  X(A418, 0x82a41cu) \
  X(A41C, 0x82a421u) \
  X(A421, 0x82a424u) \
  X(A425, 0x82a433u) \
  X(A433, 0x82a436u) \
  X(A43A, 0x82a43cu) \
  X(A43D, 0x82a44bu) \
  X(A44B, 0x82a451u) \
  X(A451, 0x82a454u) \
  X(A454, 0x82a461u) \
  X(A461, 0x82a465u) \
  X(A465, 0x82a46eu) \
  X(A46E, 0x82a471u) \
  X(A471, 0x82a474u) \
  X(A474, 0x82a477u) \
  X(A477, 0x82a498u) \
  X(A498, 0x82a49au) \
  X(A49A, 0x82a4a0u) \
  X(A4A0, 0x82a4a2u) \
  X(A4A2, 0x82a4a6u) \
  X(A4A7, 0x82a4b0u) \
  X(A4B0, 0x82a4b3u) \
  X(A4B3, 0x82a4b5u) \
  X(A4B5, 0x82a4b8u) \
  X(A4B8, 0x82a4bdu) \
  X(A4BD, 0x82a4c3u) \
  X(A4C3, 0x82a4c6u) \
  X(A4C6, 0x82a4d5u) \
  X(A4D5, 0x82a4dfu) \
  X(A4DF, 0x82a4e7u) \
  X(A4E7, 0x82a4ecu) \
  X(A4EC, 0x82a4f0u) \
  X(A4F0, 0x82a4f3u) \
  X(A4F3, 0x82a4f7u) \
  X(A4F7, 0x82a4fau) \
  X(A4FA, 0x82a51bu) \
  X(A51B, 0x82a51du) \
  X(A51D, 0x82a523u) \
  X(A523, 0x82a525u) \
  X(A525, 0x82a529u) \
  X(A52A, 0x82a52du) \
  X(A52D, 0x82a52fu) \
  X(A52F, 0x82a532u) \
  X(A532, 0x82a535u) \
  X(A535, 0x82a538u) \
  X(A8D8, 0x82a8dbu) \
  X(A8DB, 0x82a8deu) \
  X(A8DE, 0x82a8e8u) \
  X(A8E8, 0x82a8ebu) \
  X(A8EF, 0x82a8f3u)

typedef enum {
#define X(from, to) SNAKEOID_RUN_##from,
  SNAKEOID_RUNS(X)
#undef X
  SNAKEOID_RUN_COUNT,
  SNAKEOID_RUN_RTS = SNAKEOID_RUN_9DC8,  // any `RTS`: they all cost the same
} SnakeoidRun;

// The most of each that one stretch makes. A chase asks who is near once
// for itself and once for each place it bites from, and the stretch that
// ends its wait has asked once before that.
#define SNAKEOID_MAX_PLAYER_ASKS 6
#define SNAKEOID_MAX_NEAREST 5

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t runs[SNAKEOID_RUN_COUNT];
  uint16_t taken;  // branches taken
  // The ground tests, by whether the footprint was off the ground it keeps
  // to and how many of the six tiles each looked at.
  uint16_t grounds[2][TERRAIN_PROBE_COUNT + 1];
  uint16_t edges[BOUNDS_LAST_COMPARE + 1];  // `terrain_out_of_bounds`
  uint16_t draws[2];  // `rng_next`, by its overflow
  uint16_t tiles_read;  // `tile_attrs_at_tile`
  TilePutRegs churned[SNAKEOID_CHURNS];  // `map_tile_put`
  int churn_count;
  PlayerPickRegs players[SNAKEOID_MAX_PLAYER_ASKS];  // `player_in_range`
  int player_count;
  ActorNearestWork nearest[SNAKEOID_MAX_NEAREST];  // `actor_nearest`
  int nearest_count;
  // Overflow is the ROM's at the end, which it is not after a call that
  // does not say what it left there.
  bool v_known;
} SnakeoidWork;

// True if `state` is one of the two it can be in.
bool snakeoid_state_known(uint16_t state);

// True if the stretch that begins at `pc`, with the stack at `s`, comes back
// through return addresses this port knows.
bool snakeoid_stack_known(const Wram* w, uint32_t pc, uint16_t s);

// Run the stretch that begins at `c->pc`.
void snakeoid_run(Wram* w, const Rom* rom, PortCpu* c, SnakeoidWork* k);

#endif
