// The zombies: how they walk, follow walls, notice people and chase them.
//
// Every zombie is a thread, and its loop is the same small state machine the
// monster has (see `port/monster.h`): yield two ticks, decide, run the state
// body `$14` names, animate, and go round again. A state body is entered by a
// computed `RTS` and returns with one. This is all of it but the loop itself
// as readable C: three state bodies, the decision before them and the
// animation after them, for each of two kinds.
//
//   slow    fast     what
//   $8600   $89BF    walk         one step straight ahead
//   $8656   $8A72    follow_wall  keep a wall on one side
//   $86B3   $8AD8    chase        step towards whoever is nearest
//   $8706   $8B30    decide       start a chase, or leave
//   $8736   $8B57    animate      the walk cycle, in either kind's frames
//
// **The slow kind** is `$81:87F8`, every enemy on level 1. It walks a pixel a
// step and turns a quarter clockwise at a wall. **The fast kind** is
// `$81:88CA` and `$81:8C17`. It walks two pixels a step and turns by `$2C` at
// a wall. `$81:8C17` has four hits of health and draws from its own frames,
// `$81:8B57`'s. The other two share `$81:8736`'s.
//
// **Nothing writes `$2C`.** Not the zombie's code, and not `thread_spawn`,
// which copies five words into a new thread's page and leaves the rest as the
// page's last thread left it. So how far a fast zombie turns is an accident of
// which slot it got. A multiple of 16 is no turn at all, and such a zombie
// stands at the first wall it meets until something comes near enough to
// chase. On twelve corpus movies `$81:8A72` ran 55,422 times and never took a
// step, which is what that looks like.
//
// ## What a zombie does
//
// It walks straight on until something solid is in its way. It does not turn
// for someone standing there: it waits for them to move. At solid ground it
// turns, and from then on it follows the wall. Each step it first tries the
// heading it turned from, back towards the wall. If that is open it takes it,
// which is how it rounds a corner. Otherwise it keeps on, and turns again when
// that is blocked too. So it walks around obstacles instead of into them.
//
// Every frame, before the state body, it decides. The slow kind starts a
// chase when one of the four kinds of actor `actor_nearest` knows about comes
// within `$41`. The fast kind does at `$A0`. Otherwise it asks whether either
// player is within reach, and if neither is it leaves the level.
//
// A chase lines up with the target (`actor_snap_to`), asks which way it is
// (`actor_bearing`), and steps that way, two pixels a frame. The slow kind
// takes two one-pixel steps, each refused outright by ground or by someone.
// The fast kind takes one two-pixel step that tries each axis on its own, so
// it slides along a wall. A slow zombie gives up beyond `$46`, five pixels
// further than it noticed from, and a fast one beyond `$B4`. Giving up, or
// being stuck in a fast chase, it picks one of the four straight headings at
// random and walks.
//
// ## Headings
//
// A heading is the game's direction, 1 for up and clockwise to 8 for
// up-left, doubled. The step tables are indexed by the heading doubled again.
// A turn is arithmetic on the heading: `((h - 2 + by) & 15) + 2`, which with
// `by` = 4 is a quarter turn clockwise.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does, scratch included. Of the registers,
// carry and overflow outlive a state body: the animation leaves overflow
// alone and carry three frames in four, and `thread_yield`'s `PHP` parks both
// in the thread's status byte. Every path ends with a test or a turn, so
// carry is that test's answer or that add's carry, and overflow the last add
// in the tests or the turn. Nothing the decision leaves is read, because
// every state body sets all four before reading any. The animation's carry
// is its `CPX #$0030` on the frames it changes the picture.
//
// Port code: libc only.

#ifndef PORT_ZOMBIE_H
#define PORT_ZOMBIE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/oam.h"  // ActorNearestWork, AtPointWork
#include "port/wram.h"

typedef enum { ZOMBIE_SLOW, ZOMBIE_FAST, ZOMBIE_KINDS } ZombieKind;

// The entries, and an `RTS` of each. The tables and the state addresses are
// in bank `$81`, which is the thread's data bank.
#define ZOMBIE_BANK 0x81u

#define ZOMBIE_SLOW_WALK_PC 0x818600u
#define ZOMBIE_SLOW_WALK_RTS_PC 0x81863du
#define ZOMBIE_SLOW_FOLLOW_PC 0x818656u
#define ZOMBIE_SLOW_FOLLOW_RTS_PC 0x8186a9u
#define ZOMBIE_SLOW_CHASE_PC 0x8186b3u
#define ZOMBIE_SLOW_CHASE_RTS_PC 0x818702u
#define ZOMBIE_SLOW_DECIDE_PC 0x818706u
#define ZOMBIE_SLOW_DECIDE_RTS_PC 0x818726u
#define ZOMBIE_SLOW_ANIMATE_PC 0x818736u
#define ZOMBIE_SLOW_ANIMATE_RTS_PC 0x81876bu

#define ZOMBIE_FAST_WALK_PC 0x8189bfu
#define ZOMBIE_FAST_WALK_RTS_PC 0x8189fcu
#define ZOMBIE_FAST_FOLLOW_PC 0x818a72u
#define ZOMBIE_FAST_FOLLOW_RTS_PC 0x818ac6u
#define ZOMBIE_FAST_CHASE_PC 0x818ad8u
#define ZOMBIE_FAST_CHASE_RTS_PC 0x818b27u
#define ZOMBIE_FAST_DECIDE_PC 0x818b30u
#define ZOMBIE_FAST_DECIDE_RTS_PC 0x818b56u
#define ZOMBIE_FAST_ANIMATE_PC 0x818b57u
#define ZOMBIE_FAST_ANIMATE_RTS_PC 0x818b8cu

// The two animations' frame tables, sixteen-bit metasprite pointers indexed by
// the heading times four plus the leg. `$81:8736`'s is the slow kind's and
// `$81:88CA`'s, `$81:8B57`'s is `$81:8C17`'s.
#define ZOMBIE_FRAMES 0x8776u
#define ZOMBIE_FRAMES_8C17 0x8b97u

// Fields on the zombie's page.
#define ZOMBIE_DP_RECORD 0x08    // its display record
#define ZOMBIE_DP_TIMER 0x0a     // frames until the next leg
#define ZOMBIE_DP_LEG 0x0c       // 0, 2, 4 or 6
#define ZOMBIE_DP_HEADING 0x0e
#define ZOMBIE_DP_TRYING 0x10    // the heading back towards the wall
#define ZOMBIE_DP_LEAVE 0x12     // nonzero ends the thread
#define ZOMBIE_DP_STATE 0x14     // the next state body
#define ZOMBIE_DP_X 0x16         // where it is, which the record follows
#define ZOMBIE_DP_Y 0x18
#define ZOMBIE_DP_NEXT_X 0x1a    // the point being tried
#define ZOMBIE_DP_NEXT_Y 0x1c
#define ZOMBIE_DP_CHASE_INDEX 0x20  // the chase's heading doubled
#define ZOMBIE_DP_QUIET 0x24     // fast: decides only while this is negative
#define ZOMBIE_DP_WAS_X 0x26     // fast: where a sliding step started
#define ZOMBIE_DP_WAS_Y 0x28
#define ZOMBIE_DP_STEPS 0x2a     // fast: chase steps left this frame
#define ZOMBIE_DP_TURN 0x2c      // fast: how far it turns at a wall; see above
#define ZOMBIE_DP_TARGET 0x2e    // slow: what it is chasing

// For the harness, and only for it: what was asked, which with the path is
// what it takes to price the ROM's instructions around the calls.
typedef struct {
  bool ground;   // the ground there was solid
  int tiles;     // ...as the ground test found after this many of its six
  bool asked;    // ...it was not, so the actor test ran
  bool someone;  // ...and someone was standing there
} ZombieProbe;

#define ZOMBIE_MAX_PROBES 6

typedef struct {
  ZombieProbe probe[ZOMBIE_MAX_PROBES];
  int probes;
  AtPointWork at_point;  // summed over every probe that asked
  ActorNearestWork nearest;
  bool asked_nearest;
  bool far;          // decide: nothing near enough to chase
  bool lost;         // chase: the target too far, or right on top of it
  bool on_top;       // ...the second
  bool stuck;        // fast chase: the step went nowhere
  bool moved_across; // ...or its X changed
  bool wandered;     // picked a random heading and walked
  bool turned;       // ended with a turn at a wall
  bool quiet;        // fast decide: `$24` said not to look
  bool nobody;       // decide: neither player within reach, so it leaves
  bool new_leg;      // animate: the walk cycle moved on
  bool mirrored;     // ...to a frame drawn flipped
  bool carry;        // C as the call leaves it
  bool overflow;     // V likewise
} ZombieLog;

// One frame of each state body, for the zombie whose page is `page`. `log`
// may be NULL.
void zombie_walk(Wram* w, const Rom* rom, uint16_t page, ZombieKind kind,
                 ZombieLog* log);
void zombie_follow_wall(Wram* w, const Rom* rom, uint16_t page,
                        ZombieKind kind, ZombieLog* log);
void zombie_chase(Wram* w, const Rom* rom, uint16_t page, ZombieKind kind,
                  ZombieLog* log);

// The decision before the state body.
void zombie_decide(Wram* w, const Rom* rom, uint16_t page, ZombieKind kind,
                   ZombieLog* log);

// The walk cycle, drawn from `frames` in bank `$81`.
void zombie_animate(Wram* w, const Rom* rom, uint16_t page, uint16_t frames,
                    ZombieLog* log);

// The address of a heading's step in a kind's table, in bank `$81`. The
// heading comes off the page, so the harness checks it lands in the cartridge.
uint16_t zombie_step_at(ZombieKind kind, uint16_t heading);

#endif
