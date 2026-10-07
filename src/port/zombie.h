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
#include "port/begin.h"
#include "port/cpu.h"
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
  PlayerPickRegs players;  // decide: what `player_bearing` did, when asked
  ActorSnapRegs snap;      // chase: what the snap and the bearing did
  ActorBearingRegs bearing;
  bool far;          // decide: nothing near enough to chase
  bool lost;         // chase: the target too far, or right on top of it
  bool on_top;       // ...the second
  bool stuck;        // fast chase: the step went nowhere
  bool moved_across; // ...or its X changed
  bool wandered;     // picked a random heading and walked
  bool drew_overflow;  // ...on a draw that overflowed, which costs more
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

// --- A whole frame -----------------------------------------------------------
//
// The three threads' loops, from where `thread_yield` returns to the next
// yield:
//
//     $81:8834  `$87F8`: JSR decide, the state body by computed RTS, JSR animate
//     $81:890B  `$88CA`: the same with the fast kind's, after a look at `$22`
//     $81:8C58  `$8C17`: the fast kind's again, and its own frames
//
// and then `LDA $12 : BEQ` back to `LDA #$0002 : JSL thread_yield`. A zombie
// with `$12` set is leaving, and the loop goes on to free its record instead.
//
// A `$88CA` zombie with `$22` set drops bit 4 of its record's flags, clears
// `$22`, and does not decide that frame. Nothing ported sets `$22`.
//
// Only the frames whose state body is one of the three here are the port's.
// A zombie in any other state, one a hit installs, say, is the ROM's for that
// frame, piece by piece as before.
typedef enum {
  ZOMBIE_THREAD_87F8,
  ZOMBIE_THREAD_88CA,
  ZOMBIE_THREAD_8C17,
  ZOMBIE_THREADS
} ZombieThread;

#define ZOMBIE_87F8_FRAME_PC 0x818834u
#define ZOMBIE_87F8_YIELD_PC 0x818830u  // `JSL thread_yield`, A already 2
#define ZOMBIE_87F8_LEAVE_PC 0x818846u
#define ZOMBIE_88CA_FRAME_PC 0x81890bu
#define ZOMBIE_88CA_YIELD_PC 0x818907u
#define ZOMBIE_88CA_LEAVE_PC 0x818930u
#define ZOMBIE_8C17_FRAME_PC 0x818c58u
#define ZOMBIE_8C17_YIELD_PC 0x818c54u
#define ZOMBIE_8C17_LEAVE_PC 0x818c6au

#define ZOMBIE_YIELD_TICKS 2
#define ZOMBIE_DP_UNMARK 0x22    // fast: nonzero drops the record's bit 4
#define ZOMBIE_RECORD_MARK 0x0010

typedef enum { ZOMBIE_WALKING, ZOMBIE_FOLLOWING, ZOMBIE_CHASING } ZombieState;

// For the harness: the three pieces' logs, and which state body ran.
typedef struct {
  ZombieLog decide, act, animate;
  ZombieState state;
  bool unmarked;  // fast: `$22` was set, so it did not decide
} ZombieFrameLog;

// Which kind of zombie a thread runs.
ZombieKind zombie_thread_kind(ZombieThread thread);

// Is the state `$14` names one of the three here?
bool zombie_frame_supported(const Wram* w, uint16_t page, ZombieThread thread);

// One frame. False when the zombie is leaving. `log` may be NULL.
bool zombie_frame(Wram* w, const Rom* rom, uint16_t page, ZombieThread thread,
                  ZombieFrameLog* log);

// The address of a heading's step in a kind's table, in bank `$81`. The
// heading comes off the page, so the harness checks it lands in the cartridge.
uint16_t zombie_step_at(ZombieKind kind, uint16_t heading);

// --- The start ---------------------------------------------------------------
//
// A zombie's thread begins by coming up out of the ground, which is a list
// of pictures it sleeps through. These are what it does before that and
// after, for `$81:87F8` and `$81:88CA`:
//
//     $81:87F8  $81:88CA  spawn   to where the pictures are played
//     $81:8811  $81:88E3  risen   from there to the loop's first sleep
//
// **Spawned**, it is charged to the level's load (`W_SPAWN_LOAD`), twenty of
// it, and a record is made a zombie's at the place the thread was handed
// (`port/begin.h`). The leg's timer starts from seven.
//
// **Risen**, it can be touched: its record takes collide id 3. It sets off
// one of the four straight ways by a draw and takes that way's first step.
// Its handler is `enemy_collide`, and what a hit is taken from is nothing,
// so the first that does any damage kills it. The fast kind decides from
// its first frame.
//
// Each ends at a `JSL` with that routine's argument in A: the list of
// pictures, or the two ticks the loop sleeps.
#define ZOMBIE_SLOW_SPAWN_PC 0x8187f8u
#define ZOMBIE_SLOW_RISE_PC 0x81880du  // `JSL pictures_play`, A the list
#define ZOMBIE_SLOW_RISEN_PC 0x818811u
#define ZOMBIE_FAST_SPAWN_PC 0x8188cau
#define ZOMBIE_FAST_RISE_PC 0x8188dfu
#define ZOMBIE_FAST_RISEN_PC 0x8188e3u

#define ZOMBIE_LOAD 0x0014
#define ZOMBIE_FIRST_TIMER 7
#define ZOMBIE_SLOW_RISE_PICTURES 0x886cu
#define ZOMBIE_FAST_RISE_PICTURES 0x8956u
#define ZOMBIE_COLLIDE_ID 0x0003
#define ZOMBIE_HANDLER 0x8888u  // `enemy_collide`, in this bank
#define ZOMBIE_DP_HEALTH 0x1e   // what a hit is taken from

// With no record free the ROM goes on with what is no record, and that is
// the ROM's to do: `k->declined`.
void zombie_spawn(Wram* w, PortCpu* c, ZombieKind kind, BeginWork* k);
void zombie_risen(Wram* w, const Rom* rom, PortCpu* c, ZombieKind kind,
                  ZombieLog* log);

#endif
