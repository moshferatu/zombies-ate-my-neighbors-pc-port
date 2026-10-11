// What weapons 7 to 11 send out: a thing that flies the way it was sent, at
// a height, until it lands, meets ground or hits something.
//
// `$81:F55E` is the thread `player_fire` starts for five rows of the table
// at `$80:ED8C`. Which of the five it is for is read once, from the weapon
// its owner has in hand, and that chooses a row of sixteen bytes at
// `$81:F6CC`: how long each picture is shown and how many there are, how
// fast it rises and how often that slows, and where its pictures, its steps
// and the pictures of its end are. The port has not seen the five, and says
// only what their rows do.
//
// A weapon before 7 there, and the thread ends at once: its owner changed
// weapons between firing and its first frame.
//
// **It is made** where `player_fire` said, 30 above the ground, and charges
// 2 to `W_SPAWN_LOAD`. Its handler is `shot_f6a3_collide`
// (`port/collide.h`), which leaves what it hit at `$3E`.
//
// **Each frame**, in this order:
//   * something hit: it bursts;
//   * its 60 frames are up: it ends;
//   * further from a player than a thing may go (`step_tether_blocked`):
//     it ends;
//   * its height below nothing: it has landed, and bursts;
//   * lower than 31 and on ground that stops a shot
//     (`terrain_point_bit2`): it bursts. Higher than that it is over
//     whatever is there;
//   * otherwise it rises by its speed, which goes down by one every so many
//     frames and so becomes a fall; takes its step for the way it goes; and
//     shows its next picture when this one's frames are up.
//
// Two of the five rows never slow, with a speed of nothing: those fly level
// until their frames are up.
//
// **A burst** stops it touching anything and plays the pictures of its end
// through `pictures_play`. Then it ends, as it does without a burst: its 2
// comes off `W_SPAWN_LOAD` and its record is freed. That end is
// `port/begin.h`'s, at `$81:F5C7`, and a frame stops where it begins.
//
// **Written as stretches**, like `port/spray.h`: its record, its handler,
// its sound, its sleep and its pictures are calls the harness makes, and a
// stretch stops on each. `thrown_run` leaves the address it stopped on in
// `pc`.
//
// Port code: libc only.

#ifndef PORT_THROWN_H
#define PORT_THROWN_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/step.h"
#include "port/terrain.h"
#include "port/wram.h"

#define THROWN_BANK 0x81u

// Its page. The first four are `player_fire`'s.
#define THROWN_DP_FROM_X 0x00
#define THROWN_DP_FROM_Y 0x02
#define THROWN_DP_WAY 0x04           // a direction doubled: 2 for up, round
                                     // by the clock to 16
#define THROWN_DP_SIDE 0x06          // 0 or 2: whose it is
#define THROWN_DP_OWN_RECORD 0x08    // its record again: `pictures_play`'s
#define THROWN_DP_RECORD 0x0a
#define THROWN_DP_PICTURE 0x12       // which of its pictures it shows
#define THROWN_DP_PICTURES 0x14      // where they are: a word to set or
                                     // clear in its flags, and the picture
#define THROWN_DP_FRAMES_LEFT 0x18   // of the picture it shows
#define THROWN_DP_PICTURE_FRAMES 0x1a
#define THROWN_DP_RISE 0x1e          // what its height gains a frame
#define THROWN_DP_AGE 0x20
#define THROWN_DP_SLOWS 0x22         // its rise loses one when its age has
                                     // none of these bits
#define THROWN_DP_LIFE 0x26          // frames until it ends
#define THROWN_DP_HIT 0x3e           // what it hit, from its handler
#define THROWN_DP_STEPS 0x40         // where its steps are: across and down,
                                     // at twice the way
#define THROWN_DP_END_PICTURES 0x42
#define THROWN_DP_PICTURE_COUNT 0x44

// The weapons it is for, as `W_PLAYER_WEAPON` holds them.
#define THROWN_FIRST_WEAPON 0x0007u
#define THROWN_LAST_WEAPON 0x000bu

// What one adds to `W_SPAWN_LOAD`. Its end takes it off.
#define THROWN_WEIGHT 0x0002u

#define THROWN_SOUND 0x0015u
#define THROWN_HEIGHT 0x001eu
#define THROWN_LIFE 0x003cu
// At this height and above, the ground under it is not asked about.
#define THROWN_CLEARS_GROUND 0x001fu
#define THROWN_META_BANK 0x0090u
#define THROWN_FLAGS 0x8002u
#define THROWN_HANDLER 0xf6a3u
#define THROWN_HANDLER_BANK 0x0081u

// Its tables, in its own bank.
#define THROWN_COLLIDE_IDS 0xf6b8u  // by the weapon times four and the side
#define THROWN_KINDS 0xf6ccu        // sixteen bytes a weapon:
#define THROWN_KIND_PICTURE_FRAMES 0x00
#define THROWN_KIND_RISE 0x02
#define THROWN_KIND_SLOWS 0x04
#define THROWN_KIND_PICTURE_COUNT 0x06
#define THROWN_KIND_PICTURES 0x0a
#define THROWN_KIND_STEPS 0x0c
#define THROWN_KIND_END_PICTURES 0x0e

// Where each stretch begins.
#define THROWN_STARTS_PC 0x81f55eu  // the thread's first instruction
#define THROWN_MADE_PC 0x81f623u    // after its record
#define THROWN_READY_PC 0x81f6a2u   // after its handler
#define THROWN_CRIED_PC 0x81f572u   // after its sound
#define THROWN_WAKES_PC 0x81f579u   // after its sleep

// Where one stops: a call the harness makes, or `port/begin.h`'s end.
#define THROWN_QUIT_PC 0x81f61du     // `RTL`: not one of the five
#define THROWN_RECORD_PC 0x81f61fu   // `JSL actor_slot_alloc`
#define THROWN_HANDLER_PC 0x81f69eu  // `JSL thread_set_handler`
#define THROWN_SOUND_PC 0x81f56eu    // `JSL apu_play_sfx`
#define THROWN_SLEEP_PC 0x81f575u    // `JSL thread_yield`
#define THROWN_BURST_PC 0x81f5c3u    // `JSL pictures_play`
#define THROWN_END_PC 0x81f5c7u      // `SEC`

// What `JSR $F60D` at `$81:F55E` leaves on the stack: two stretches stop
// inside that routine and the third returns from it.
#define THROWN_READY_RETURN 0xf560u

// The straight runs of the listing a stretch is made of, each named for where
// it starts, with where it ends. `tools/price_runs.py` prices them. Three
// are `$80:9D6A`'s.
#define THROWN_RUNS(X) \
  X(9D6A, 0x809d72u) \
  X(9D72, 0x809d74u) \
  X(9D74, 0x809d75u) \
  X(F55E, 0x81f561u) \
  X(F561, 0x81f56eu) \
  X(F572, 0x81f575u) \
  X(F579, 0x81f57fu) \
  X(F57F, 0x81f582u) \
  X(F582, 0x81f586u) \
  X(F586, 0x81f594u) \
  X(F594, 0x81f59bu) \
  X(F59B, 0x81f5a0u) \
  X(F5A0, 0x81f5aeu) \
  X(F5AE, 0x81f5b1u) \
  X(F5B1, 0x81f5b4u) \
  X(F5B4, 0x81f5b7u) \
  X(F5B7, 0x81f5b9u) \
  X(F5B9, 0x81f5c3u) \
  X(F5D9, 0x81f5f4u) \
  X(F5F4, 0x81f5f8u) \
  X(F5F8, 0x81f603u) \
  X(F603, 0x81f606u) \
  X(F606, 0x81f60cu) \
  X(F60C, 0x81f60du) \
  X(F60D, 0x81f613u) \
  X(F613, 0x81f61cu) \
  X(F61C, 0x81f61du) \
  X(F61E, 0x81f61fu) \
  X(F623, 0x81f69eu) \
  X(F6A2, 0x81f6a3u) \
  X(FEEF, 0x81ff02u) \
  X(FF02, 0x81ff04u) \
  X(FF04, 0x81ff05u) \
  X(FF2A, 0x81ff32u) \
  X(FF32, 0x81ff3au) \
  X(FF3A, 0x81ff40u) \
  X(FF40, 0x81ff48u)

typedef enum {
#define X(from, to) THROWN_RUN_##from,
  THROWN_RUNS(X)
#undef X
  THROWN_RUN_COUNT,
} ThrownRun;

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t runs[THROWN_RUN_COUNT];
  uint16_t taken;      // branches taken
  TetherRegs leash;    // `step_tether_blocked`, if it was `leashed`
  bool leashed;
  TerrainRegs ground;  // `terrain_point_bit2`, if it `tested`
  bool tested;
  // Overflow is the ROM's at the end, which it is not after a call that
  // does not say what it left there.
  bool v_known;
} ThrownWork;

// True if `pc` is where one of the stretches begins.
bool thrown_begins_at(uint32_t pc);

// The weapon the player on `side` holds, as the thread's first stretch
// reads it.
uint16_t thrown_weapon_of(const Wram* w, uint16_t side);

// Run the stretch that begins at `c->pc`.
void thrown_run(Wram* w, const Rom* rom, PortCpu* c, ThrownWork* k);

#endif
