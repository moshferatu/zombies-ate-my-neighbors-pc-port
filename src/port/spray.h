// What the fire extinguisher and the flamethrower send out: a puff that stays
// in front of whoever holds it, and grows.
//
// `$81:FAF5` is the thread `player_fire` starts for weapons 1 and 13, the
// two rows of the table at `$80:ED8C` that name it. Which of the two it is
// for is read once, from the weapon its owner has in hand: 13 is the
// flamethrower and anything else is the extinguisher. That chooses its
// pictures and what it is to whatever it touches, and nothing else. The
// extinguisher's foam has been looked at. That 13 is the flamethrower has
// not: no movie holds it.
//
// **It is made** in front of its owner, so far across and down for the way
// they face, and charges 2 to `W_SPAWN_LOAD`.
//
// **Each frame** it goes a step further out, 4 pixels straight or 3 each way
// on a diagonal. The step is added to how far out it is, and that to where
// its owner is now. So it is carried along with them, and a puff sent out by
// somebody running stays ahead of them.
//
// **It grows** by showing the next of its pictures: the first for three
// frames, then four more for 2, 3, 4 and 5. The frame after the last it
// ends, which is 17 frames from its making.
//
// **Ground stops it.** The place it would be shown is asked of
// `terrain_point_bit2` first, and it ends without being shown there if that
// refuses.
//
// **Its end** takes its 2 off `W_SPAWN_LOAD` and frees its record. The ROM
// then stops for good if the load went below nothing (`BMI` to itself at
// `$81:FB71`), so the harness does not hand over a frame that could.
//
// **Written as stretches**, like `port/tentacle.h`: its sound, its record,
// its sleep and its record's freeing are calls the harness makes, and a
// stretch stops on each. `spray_run` leaves the address it stopped on in
// `pc`.
//
// Port code: libc only.

#ifndef PORT_SPRAY_H
#define PORT_SPRAY_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/terrain.h"
#include "port/wram.h"

#define SPRAY_BANK 0x81u

// Its page. The first five are `player_fire`'s.
#define SPRAY_DP_OWNER_X 0x00
#define SPRAY_DP_OWNER_Y 0x02
#define SPRAY_DP_WAY 0x04         // a direction doubled: 2 for up, round by
                                  // the clock to 16
#define SPRAY_DP_SIDE 0x06        // 0 or 2: whose it is
#define SPRAY_DP_OWNER 0x08       // their record
#define SPRAY_DP_RECORD 0x0a
#define SPRAY_DP_OUT_X 0x0c       // how far out from its owner it is
#define SPRAY_DP_OUT_Y 0x10
#define SPRAY_DP_STEP_X 0x3c
#define SPRAY_DP_STEP_Y 0x3e
#define SPRAY_DP_FRAMES_LEFT 0x40  // of the picture it shows
#define SPRAY_DP_PICTURE 0x46      // which that is, doubled
#define SPRAY_DP_SIZE 0x48         // 2 to 6: how many frames its next lasts
#define SPRAY_DP_NEXT_X 0x4a       // the place being tried
#define SPRAY_DP_NEXT_Y 0x4c
#define SPRAY_DP_KIND 0x4e         // 0 for foam, 2 for flame

#define SPRAY_FOAM 0x0000u
#define SPRAY_FLAME 0x0002u
#define SPRAY_FLAMETHROWER 0x000du  // as `W_PLAYER_WEAPON` holds it

// What one adds to `W_SPAWN_LOAD`, and takes off as it ends.
#define SPRAY_WEIGHT 0x0002u

#define SPRAY_SOUND 0x000au
#define SPRAY_FIRST_FRAMES 0x0004u
#define SPRAY_FIRST_SIZE 0x0002u
#define SPRAY_LAST_SIZE 0x0006u

// A new one's record. Its first picture is the same for both kinds, and is
// shown until the first of its own.
#define SPRAY_FIRST_PICTURE 0xa26bu
#define SPRAY_META_BANK 0x0090u

// Its tables, in its own bank.
#define SPRAY_HEIGHTS 0xfc02u      // by the side
#define SPRAY_PLACES 0xfc06u       // by the side, where that side's table of
                                   // places is: four words a way, across and
                                   // down from its owner and then its step
#define SPRAY_COLLIDE_IDS 0xfc0au  // by the kind doubled and the side
#define SPRAY_PICTURES 0xfca2u     // by the kind times four and the picture

// Where each stretch begins.
#define SPRAY_STARTS_PC 0x81faf5u  // the thread's first instruction
#define SPRAY_MADE_PC 0x81fb84u    // after its record
#define SPRAY_WAKES_PC 0x81fb65u   // after its sleep

// Where one stops: a call the harness makes.
#define SPRAY_SOUND_PC 0x81fb7cu   // `JSL apu_play_sfx`
#define SPRAY_SLEEP_PC 0x81fb61u   // `JSL thread_yield`
#define SPRAY_FREE_PC 0x81fb75u    // `JML actor_slot_free`

// What `JSR $FB79` at `$81:FB02` leaves on the stack: a stretch stops inside
// that routine and the next one returns from it.
#define SPRAY_MADE_RETURN 0xfb04u

// The straight runs of the listing a stretch is made of, each named for where
// it starts, with where it ends. `tools/price_runs.py` prices them. Three
// are `$80:9D6A`'s.
#define SPRAY_RUNS(X) \
  X(9D6A, 0x809d72u) \
  X(9D72, 0x809d74u) \
  X(9D74, 0x809d75u) \
  X(FAF5, 0x81faf8u) \
  X(FAF8, 0x81fb05u) \
  X(FB05, 0x81fb0au) \
  X(FB0A, 0x81fb0eu) \
  X(FB0E, 0x81fb18u) \
  X(FB18, 0x81fb2eu) \
  X(FB2E, 0x81fb52u) \
  X(FB52, 0x81fb61u) \
  X(FB65, 0x81fb67u) \
  X(FB67, 0x81fb73u) \
  X(FB73, 0x81fb75u) \
  X(FB79, 0x81fb7cu) \
  X(FB84, 0x81fbeau) \
  X(FBEA, 0x81fbf0u) \
  X(FBF0, 0x81fbfdu) \
  X(FBFD, 0x81fc01u) \
  X(FC01, 0x81fc02u)

typedef enum {
#define X(from, to) SPRAY_RUN_##from,
  SPRAY_RUNS(X)
#undef X
  SPRAY_RUN_COUNT,
} SprayRun;

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t runs[SPRAY_RUN_COUNT];
  uint16_t taken;      // branches taken
  TerrainRegs ground;  // `terrain_point_bit2`, if it `tested`
  bool tested;
  // Overflow is the ROM's at the end, which it is not after a call that
  // does not say what it left there.
  bool v_known;
} SprayWork;

// True if `pc` is where one of the stretches begins.
bool spray_begins_at(uint32_t pc);

// Run the stretch that begins at `c->pc`.
void spray_run(Wram* w, const Rom* rom, PortCpu* c, SprayWork* k);

#endif
