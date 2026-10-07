// The player's walk: where a player ends up this frame on ordinary ground.
//
// `$80:E4BA` is the movement handler the player's frame calls through `$2A`
// (see `port/bodies.h`, "player_body"). This is that routine as readable C,
// the first piece of the port written that way rather than transliterated.
// It is a function of the player's page and the board, and it calls the
// other movement code as C functions, not through the ROM.
//
// ## What the walk does
//
// The pad has already set a direction and a speed. The walk asks where that
// takes the player (`step_propose`, `port/step.h`), and then tries the move
// one axis at a time: across first, then up or down. Trying the axes apart is
// what lets a player slide along a wall instead of stopping dead against it.
//
// Each axis is put to four questions, in this order, and the first that
// refuses stops that axis for this frame:
//
//   1. Is the ground there solid?            `terrain_blocked`
//   2. Is it too far from the other player?  `step_tether_blocked`
//   3. Is someone standing there?            `actor_obstacle_at_point`
//   4. Is it off the map?                    `terrain_out_of_bounds`
//
// Two of them have a second part.
//
// * **Solid ground can have a reaction of its own.** The tile's attribute
//   word names one of six (`walk_tile_reaction`). Those belong to other parts
//   of the game, such as doors, and this file does not handle them. Ground
//   without one is only solid, and that is 98% of the solid tiles in play.
// * **Someone standing there only blocks a player who is standing clear.**
//   If someone is also standing where the player is now, the step goes ahead.
//   Two actors that already overlap can walk apart, and nothing holds a player
//   in place.
//
// ## Its contract with the ROM
//
// The port still runs inside the emulator, so what the walk hands back must
// be what the ROM's routine would have left. It writes WRAM exactly as the
// ROM does, including the scratch the four tests use. Of the registers, the
// frame after the walk keeps only carry and overflow, which `thread_yield`
// pushes into the thread's saved status byte. `WalkLog` carries those two,
// and what the call cost the 65816, to the harness. The walk itself never
// reads the log.
//
// Neither is tidy. Carry is the answer to the last question asked, which is
// clear after "someone is standing here too" and set after every other stop.
// Overflow is whatever the last add in the tests left, and on the big maps of
// levels 19 and 25 the tilemap address `terrain_blocked` adds up sets it.
//
// Two paths are the ROM's, and `walk_supported` says when:
//
// * the double step, bit 15 of `$54`, which runs the whole walk twice. No
//   input has taken it.
// * a solid tile with a reaction of its own, but for the two below. The
//   reaction takes over from inside the walk.
//
// ## Two reactions the walk has
//
// **A door**, the third. The walk looks at the tile a little way off the
// way the player faces, up or down: `$80:E8D3`. If that is a door it wants
// one of the first of the player's items, which I take to be keys. With none
// a thread is begun at `$82:DEFB`, every frame the player goes on pushing.
// That thread plays sound `$13` twice, thirty ticks in all, and one begun
// meanwhile does nothing: the word at `$7E:1FF4` says one is at it. With
// one the door opens, and that is the ROM's. Either way the tile is solid.
//
// **The fifth** is ground the walk goes on over. `$82:F4FF` keeps five
// squares of 64 pixels at `$7E:572C`, and begins a thread at `$82:F6EB` for
// the square the player is in unless it is one of the five already. Levels
// 13 and 41 have it. What the thread shows I have not seen.
//
// ## The monster's walk
//
// A player the potion has made a monster moves by `$80:E595` instead. It is
// the same walk with three differences:
//
// * the ground is tested as an enemy's is, `terrain_blocked_enemy`
// * solid ground of two kinds breaks under it. That is `$80:F1A5`, not
//   ported, and `monster_walk_checked` says so. Any other solid ground only
//   stops the axis.
// * someone standing where the step lands stops it, wherever the monster
//   stands
//
// ## The walk of a player stuck in slime
//
// A player the slime covers moves by `$80:E6C2`, slowly, and what covers
// them moves with them: it is the record at `$0A`, a pixel lower than the
// player. It differs from the ordinary walk too:
//
// * solid ground with either of two attribute bits can be walked on. Any
//   other stops the axis, and none has a reaction.
// * someone standing where the step lands stops it, as it does the monster
//
// ## The swim
//
// A player in water moves by `$80:E543`. Each axis is put to three questions:
//
//   1. Is it water there?                    `terrain_point_bit8`
//   2. Is it too far from the other player?  `step_tether_blocked`
//   3. Is it off the map?                    `terrain_out_of_bounds`
//
// Nobody is asked after, so a swimmer goes through whoever is there.
//
// Where it is not water the walk looks at the bank, `$80:E7FE`: a point 15
// or 16 pixels off the way the player faces, when that is straight up, down
// or across. Clear ground there ends the swim, and that is the ROM's.
//
// Port code: libc only.

#ifndef PORT_WALK_H
#define PORT_WALK_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // ObstacleWork
#include "port/step.h"     // TetherRegs
#include "port/terrain.h"  // the ground test's probes, the map test's exits
#include "port/wram.h"

#define PLAYER_WALK_PC 0x80e4bau  // `$80:E4BA`, entered by the frame's `RTS`
#define PLAYER_WALK_RTS_PC 0x80e542u
#define MONSTER_WALK_PC 0x80e595u
#define MONSTER_WALK_RTS_PC 0x80e5feu
#define STUCK_WALK_PC 0x80e6c2u
#define STUCK_WALK_RTS_PC 0x80e738u
#define SWIM_WALK_PC 0x80e543u
#define SWIM_WALK_RTS_PC 0x80e594u

typedef enum {
  WALK_ORDINARY,
  WALK_OF_MONSTER,
  WALK_STUCK,
  WALK_SWIM,
} WalkKind;

// The door: how far up or down to look for each way faced, and what begins
// when the player has nothing for it.
#define WALK_DOOR_REACH 0x80ea1eu
#define WALK_DOOR_BITS 0x8b38u
#define WALK_DOOR 0x0010u
#define WALK_DOOR_THREAD 0xdefbu
#define WALK_DOOR_THREAD_BANK 0x0082u
#define WALK_DP_FACING 0x26   // the way faced, doubled
#define WALK_FACING_MAX 0x10
#define WALK_DP_ITEMS 0x66    // where the player's items are
#define WALK_DP_LOOK_X 0x60   // scratch: the point looked at
#define WALK_DP_LOOK_Y 0x62

// The fifth reaction's squares, and its thread.
#define W_WALK_SQUARES 0x572cu
#define WALK_SQUARES_LAST 8   // five words, looked at from the last
#define WALK_SQUARE_THREAD 0xf6ebu
#define WALK_SQUARE_THREAD_BANK 0x0082u
#define W_WALK_SQUARE_KEY 0x0038   // page zero scratch
#define W_WALK_SQUARE_X 0x003a
#define W_WALK_SQUARE_Y 0x003c

// The swim: the bank looked at, a step across and a step down for each way.
#define SWIM_BANK_STEPS 0x80e831u
#define SWIM_DP_STATE 0x70
#define SWIM_STATE 0x000eu
#define SWIM_DP_PLAYER 0x0e
#define W_SWIM_HEALTH 0x1cb8

// Solid ground a stuck player can still cross: bits of the attribute word.
#define STUCK_WALK_CROSSES 0x0c00u
#define STUCK_WALK_DP_COVER 0x0a  // the record of what covers the player

// `$80:E790`: the attribute bits that say what solid ground is to the
// monster, and the two values that break.
#define MONSTER_WALL_BITS 0x8b38u
#define MONSTER_WALL_BREAKS_A 0x0100u
#define MONSTER_WALL_BREAKS_B 0x0200u

// Fields on the player's page. `$30`/`$32` is where the player is, and
// `step_propose` leaves where the pad wants them at `$34`/`$36`.
#define WALK_DP_RECORD 0x08  // the player's actor record
#define WALK_DP_X 0x30
#define WALK_DP_Y 0x32
#define WALK_DP_WANT_X 0x34
#define WALK_DP_WANT_Y 0x36
#define WALK_DP_MODE 0x54  // bit 15: walk twice
#define WALK_TWICE 0x8000u

// `$80:E739`: the attribute bits that name a tile's reaction, and the six
// values that have one.
#define WALK_REACTION_BITS 0xcb38u
#define WALK_REACTIONS 6

// The questions, as the log counts them.
typedef enum {
  WALK_ASK_GROUND,      // `terrain_blocked`
  WALK_ASK_REACTION,    // `$80:E739`, only after solid ground
  WALK_ASK_TETHER,      // `step_tether_blocked`
  WALK_ASK_THERE,       // `actor_obstacle_at_point`, where the step lands
  WALK_ASK_HERE,        // ...and where the player stands, after a yes
  WALK_ASK_MAP,         // `terrain_out_of_bounds`
  WALK_ASK_COUNT
} WalkQuestion;

enum { WALK_REACTION_DOOR = 3, WALK_REACTION_SQUARE = 5 };

// How a look through the five squares ended.
typedef enum {
  WALK_SQUARE_KNOWN,  // it is one of the five
  WALK_SQUARE_BEGUN,  // a free place: taken, and the thread begun
  WALK_SQUARE_FULL,   // five others
} WalkSquareEnd;

// A reaction the walk took itself.
typedef struct {
  uint8_t kind;
  bool reached;   // a door: a tile was looked at
  bool locked;    // ...and it is a door, with nothing to open it
  int slot;       // the thread begun, as `thread_spawn` answers
  uint8_t looked; // the fifth: squares looked at
  WalkSquareEnd end;
} WalkReaction;

// How a swimmer's look at the bank ended.
typedef enum {
  SWIM_BANK_DEAD,         // no health
  SWIM_BANK_LESS,         // ...or less than none
  SWIM_BANK_NOT_SWIMMING, // the state is not the swim's
  SWIM_BANK_NO_WAY,       // facing nowhere
  SWIM_BANK_SOLID,        // the ground there is solid
  SWIM_BANK_COUNT
} SwimBank;

// For the harness, and only for it. `asked[q][yes]` counts how often each
// question was asked and how it answered, which is all it takes to price the
// ROM's instructions around the calls. `obstacle` is the obstacle test's own
// count, summed over both times it can be asked. `last_yes` is the carry the
// last question left, which is the carry the walk returns, and `overflow` the
// V the last add left.
//
// The tests are priced by what each did, so the log keeps that too: how often
// each of the ground test's six probes ran and how many asks a probe before
// the last one ended, how each tether test left, and which of the map test's
// exits each ask took. A walk asks each at most once an axis.
#define WALK_AXES 2

typedef struct {
  uint16_t asked[WALK_ASK_COUNT][2];
  uint16_t taken;  // axes the player moved along
  ObstacleWork obstacle;
  bool doubled;    // the step proposed was two
  uint16_t probes[TERRAIN_PROBE_COUNT];
  uint16_t ground_cut_short;
  TetherRegs tether[WALK_AXES];
  int tethers;
  uint16_t map_exits[BOUNDS_LAST_COMPARE + 1];
  bool last_yes;
  bool overflow;
  // The monster's ground tests, each as it answered.
  TerrainRegs enemy_ground[WALK_AXES];
  int enemy_grounds;
  bool took[WALK_AXES];  // which axes the player moved along: across, down
  // The reactions it took itself, at most one an axis.
  WalkReaction reaction[WALK_AXES];
  int reactions;
  // The swim's: "is it water?" by its answer, and each look at the bank.
  uint16_t water[2];
  uint16_t bank[SWIM_BANK_COUNT];
} WalkLog;

// Would the port walk this frame the way the ROM does? False for the two
// paths above. It may run the walk to find out, so `w` is a copy.
bool walk_supported(Wram* w, const Rom* rom, uint16_t page);

// Walk the player whose page is `page` for one frame. `log` may be NULL.
void player_walk(Wram* w, const Rom* rom, uint16_t page, WalkLog* log);

// The same, for a caller that has not asked `walk_supported`: false is one of
// the two paths that are the ROM's, and then `w` is not what the ROM leaves.
bool player_walk_checked(Wram* w, const Rom* rom, uint16_t page, WalkLog* log);

// `$80:E595`, the monster's walk. False is a wall that breaks, and then `w`
// is not what the ROM leaves. `log` may be NULL.
bool monster_walk_checked(Wram* w, const Rom* rom, uint16_t page, WalkLog* log);

// `$80:E6C2`, the walk of a player stuck in slime. `log` may be NULL.
void stuck_walk(Wram* w, const Rom* rom, uint16_t page, WalkLog* log);

// `$80:E543`, the swim. False is a swimmer who reaches the bank, and then
// `w` is not what the ROM leaves. `log` may be NULL.
bool swim_walk_checked(Wram* w, const Rom* rom, uint16_t page, WalkLog* log);

// The reaction a solid tile has, 1 to 6, or 0 for none. `attrs` is the
// attribute word as `terrain_blocked` leaves it, shifted right one.
int walk_tile_reaction(uint16_t attrs);

#endif
