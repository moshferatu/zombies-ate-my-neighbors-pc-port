// The slimes: the red blobs of levels 9, 29 and 41, which lunge along the
// ground a body's length at a time.
//
// A slime is a thread, `$81:CCCF`. Its loop sleeps four ticks, runs one of a
// handful of state bodies, and then shows the next picture. This is one pass
// of that loop as readable C, from where `thread_yield` returns to the next
// yield:
//
//   $81:CCE8  frame   the state body, then the picture and the touch
//
// Of the thread's setup, `slime_begin` is the record and the page, as far
// as the pictures it plays first. What is not here is the rest of that and
// its death, and of its attack only the stretches between its sleeps are. The attack plays two animations
// that sleep inside the call. A pass that begins one ends there, at the
// `JSL` that plays the first, with the state's return still on the stack:
// `SlimeLog::attack_began` says so.
//
// ## What a slime does
//
// **It moves in lunges.** It faces one of four ways. The thread keeps where
// the lunge will end; the record on screen stays where the lunge began, and
// five pictures stretch the body from one to the other. On the fifth the
// record is moved up to the thread's position and the next lunge begins. A
// lunge is 14 pixels up or down and 24 across.
//
// **It turns when it cannot go on.** Solid ground or anyone standing where
// the lunge would end turns it a quarter clockwise, and from then on it feels
// its way: each pass it tries the turn back anticlockwise first, and takes it
// if that way is clear, so it follows the wall it met.
//
// **Between lunges it thinks.** After every step it spends the rest of the
// lunge deciding what comes next, once a pass, and the last decision before
// the lunge ends is the one that stands. It finds whoever `actor_nearest`
// knows and draws a number: about three times in ten it turns to face them,
// one in five it thinks of attacking, and otherwise it carries on, unless
// neither player is within `$140`, when it leaves the level.
//
// **It attacks less than it thinks of it.** A second draw has to come in
// under `$23`, about one in seven. Otherwise it sets off a random way. One
// that does attack is told of nothing until the attack is over: its handler
// is taken away first.
//
// **Facing someone** is `actor_bearing`'s answer made into one of the four
// ways. On a diagonal a table picks between the two axes by which gap is the
// greater, and for two of the four diagonals it picks the lesser. That is the
// ROM's table and it is read from the cartridge.
//
// **It hurts by touch.** Once a pass it tells everything inside a box around
// its body, by `actor_notify_box` with id 3. The box is measured from the
// record at the start of each lunge and kept for the rest of it.
//
// **A hit makes it flash** for thirty passes, during which it neither moves
// nor touches. `enemy_cdde_collide` in `port/collide.h` starts that; the
// state that counts it down is here.
//
// ## Directions
//
// The thread's own numbers: 2 up, 6 right, 10 down, 14 left. A quarter turn
// is 4. `actor_bearing` answers 1 for up and clockwise to 8 for up-left, and
// 0 for the same spot, which leaves a slime facing no way at all: its lunge
// is then nowhere, and the tables are read as the ROM reads them.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. A frame ends at the `JSL
// thread_yield` with the tick count in A, or with the slime's fate there when
// that is no longer zero. Carry and overflow are the picture's
// arithmetic, and the thread's own on a pass spent flashing. A frame that
// begins an attack ends at `$81:CBBC` with the list of pictures in A, the
// thread's slot in X, zero in Y, carry clear and the draw's overflow.
//
// Port code: libc only.

#ifndef PORT_SLIME_H
#define PORT_SLIME_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/begin.h"
#include "port/oam.h"  // the works and registers of what it asks
#include "port/terrain.h"
#include "port/wram.h"

// The tables are in bank `$81`, which is the thread's data bank.
#define SLIME_BANK 0x81u

#define SLIME_FRAME_PC 0x81cce8u
#define SLIME_YIELD_PC 0x81cce4u  // `JSL thread_yield`, A already 4
#define SLIME_FATE_PC 0x81ccf7u   // `BMI`, on a fate in A that is not zero
#define SLIME_ATTACK_PC 0x81cbbcu // `JSL pictures_play`, A the attack's list
#define SLIME_ATTACK_PICTURES 0xcbeau
// Where a state's `RTS` goes back to, less one: what the loop's `PEA` put on
// the stack, and still there when a pass ends inside the state.
#define SLIME_STATE_RETURN 0xccefu
#define SLIME_YIELD_TICKS 4
#define SLIME_BEGIN_PC 0x81cc4fu       // `JSL`, for a record
#define SLIME_BEGIN_PLAY_PC 0x81cca8u  // `JSL pictures_play`, A their list
#define SLIME_FIRST_PICTURES 0xccb7u
#define SLIME_DP_ARG_X 0x00            // where the thread was started
#define SLIME_DP_ARG_Y 0x02
#define SLIME_START_PICTURE 0xbf11u
#define SLIME_PICTURE_BANK 0x0090u
#define SLIME_COLLIDE_ID 0x0003
#define SLIME_START_DIRECTION 10       // down
#define SLIME_HITS 4

// The state bodies, by the address the thread keeps in `$16`.
#define SLIME_STATE_CRAWL 0xc9fau
#define SLIME_STATE_FEEL 0xca51u
#define SLIME_STATE_THINK 0xcab0u
#define SLIME_STATE_FACE 0xcafbu
#define SLIME_STATE_ATTACK 0xcba0u
#define SLIME_STATE_FLASH 0xcc2fu

// Fields on the slime's page.
#define SLIME_DP_RECORD 0x08
#define SLIME_DP_FATE 0x0a          // 0 alive, above 0 killed, below 0 leaving
#define SLIME_DP_HITS_LEFT 0x0c
#define SLIME_DP_X 0x0e             // where this lunge ends
#define SLIME_DP_Y 0x10
#define SLIME_DP_TO_X 0x12          // where the next would
#define SLIME_DP_TO_Y 0x14
#define SLIME_DP_STATE 0x16
#define SLIME_DP_NEXT_STATE 0x18    // the state thinking gives way to
#define SLIME_DP_DIRECTION 0x1a
#define SLIME_DP_OTHER_WAY 0x1c     // feeling: the turn back it tried
#define SLIME_DP_TOUCHES 0x1e       // 1 once it has shown a picture
#define SLIME_DP_PHASE 0x20         // 0 to 4 through a lunge
#define SLIME_DP_HIT_BY 0x22        // cleared every pass
#define SLIME_DP_FLASH_LEFT 0x24
#define SLIME_DP_FLASH_RESUMES 0x26 // the state the flash interrupted
#define SLIME_DP_BOX_LEFT 0x28
#define SLIME_DP_BOX_RIGHT 0x2a
#define SLIME_DP_BOX_TOP 0x2c
#define SLIME_DP_BOX_BOTTOM 0x2e
#define SLIME_DP_TARGET 0x30        // the record `actor_nearest` found
#define SLIME_DP_BEARING 0x32

// For the harness, and only for it: what happened, which with the path is
// what it takes to price the ROM's instructions around the calls.
typedef struct {
  bool asked;        // this probe ran
  TerrainRegs ground;
  bool someone;      // the ground was clear, and someone was standing there
} SlimeProbe;

typedef enum {
  SLIME_THOUGHT_NONE,     // it did not think this pass
  SLIME_THOUGHT_RESUMED,  // the lunge was over, so the next state ran
  SLIME_THOUGHT_ATTACK_LOW,  // a draw under `$19`
  SLIME_THOUGHT_FACE,     // ...under `$50`
  SLIME_THOUGHT_ATTACK,   // ...under `$78`
  SLIME_THOUGHT_CARRY_ON, // otherwise, with a player near
  SLIME_THOUGHT_LEAVE,    // ...or with neither
} SlimeThought;

typedef struct {
  SlimeThought thought;
  ActorNearestWork nearest;   // thinking: what `actor_nearest` did
  bool drew_overflow;         // ...the draw overflowed, which costs more
  PlayerPickRegs players;     // ...and what `player_bearing` did
  uint16_t state;             // the body that ran, after any thinking
  bool faced;                 // SLIME_STATE_FACE ran...
  ActorBearingRegs bearing;   // ...on this answer
  bool diagonal;              // ...which was not one of the four ways
  int gaps_negated;           // ...and of its two gaps, how many were negative
  SlimeProbe probe[2];        // a step's tests, or feeling's two
  AtPointWork at_point;       // summed over both
  bool took_other_way;        // feeling: the turn back was clear
  bool attack_weighed;        // SLIME_STATE_ATTACK ran...
  bool attack_wanted;         // ...and its draw said yes
  bool attack_began;          // ......and so did the word at `$0006`
  uint16_t attack_slot;       // .........the thread's slot, which X is left as
  bool draws_overflowed[2];   // ...that draw, and the random way's
  bool flash_ended;           // flashing: this was the last pass of it
  bool flashing;              // the picture was left alone
  bool lunge_began;           // the picture was a lunge's first
  bool touched;               // the box was told...
  bool measured;              // ...having been measured anew
  ActorNotifyWork touch;      // ...and what `actor_notify_box` did
  bool declined;              // the ROM's: the touch reached a handler
                              // the port lacks
  bool mirrored;              // the picture faces left
  bool lunge_ended;           // the record moved up
  bool c, v;                  // carry and overflow as the frame leaves them
  bool flags_set;             // ...or it wrote neither
} SlimeLog;

// ---------------------------------------------------------------------------
// The glob
// ---------------------------------------------------------------------------
//
// An attack throws a glob of slime, a thread of its own at `$81:CF10`. It
// goes straight up from the slime, slowing as it rises. At the top it is
// moved across to above where it was aimed, a spot within 16 pixels of the
// target, and it comes down faster and faster until it lands. This is a
// pass of that, every second tick:
//
//   $81:CF2A  frame   rise or fall by this pass's speed
//
// The splash it lands with, which tells everything near and plays an
// animation that sleeps inside the call, is the ROM's, but for the box it
// tells of: see `slime_glob_splash`.
#define SLIME_GLOB_FRAME_PC 0x81cf2au
#define SLIME_GLOB_YIELD_PC 0x81cf26u  // `JSL thread_yield`, A already 2
#define SLIME_GLOB_LANDED_PC 0x81cf36u // the splash
#define SLIME_GLOB_YIELD_TICKS 2

#define SLIME_GLOB_STATE_RISE 0xce88u
#define SLIME_GLOB_STATE_FALL 0xceadu

#define SLIME_GLOB_DP_RECORD 0x08
#define SLIME_GLOB_DP_STATE 0x0a
#define SLIME_GLOB_DP_LANDED 0x0c  // not zero once it is down
#define SLIME_GLOB_DP_AIM_X 0x0e   // where it comes down
#define SLIME_GLOB_DP_AIM_Y 0x10
#define SLIME_GLOB_DP_SPEED 0x12   // pixels a pass, counting down through zero

typedef enum {
  SLIME_GLOB_ROSE,
  SLIME_GLOB_TURNED,   // the top: moved across, and falling from now
  SLIME_GLOB_FELL,
  SLIME_GLOB_FELL_TO_GROUND,  // ...past the ground, so put on it
  SLIME_GLOB_LANDED,   // it was on the ground already
} SlimeGlobPass;

typedef struct {
  SlimeGlobPass pass;
  bool c, v;       // carry and overflow as the pass leaves them
  bool flags_set;  // ...or it wrote neither
} SlimeGlobLog;

bool slime_glob_frame_supported(const Wram* w, uint16_t page);

// One pass, for the glob whose page is `page`. False once it has landed.
// `log` may be NULL.
bool slime_glob_frame(Wram* w, uint16_t page, SlimeGlobLog* log);

// Can `slime_frame` take this pass? Not a state it does not know, nor a
// slime facing a way the tables do not reach. It only looks.
bool slime_frame_supported(const Wram* w, uint16_t page);

// One pass, for the slime whose page is `page`. `carry` is the thread's own,
// as it woke with it: the attack's draw begins from it. False when its fate
// is no longer zero, and the thread is to end. `log` may be NULL.
//
// `log->declined` says the pass is the ROM's after all. WRAM is then part
// written, which is why a guard asks on a scratch copy first.
// `log->attack_began` says it ended at `SLIME_ATTACK_PC` and not at the
// yield.
bool slime_frame(Wram* w, const Rom* rom, uint16_t page, bool carry,
                 SlimeLog* log);

// ---------------------------------------------------------------------------
// The attack, between its sleeps
// ---------------------------------------------------------------------------
//
// A pass that begins an attack ends at the first of the two lists of
// pictures the attack shows. It sleeps inside each, so it is not a pass but
// several. These are the stretches of it between the sleeps:
//
//   $81:CBC0  throw   the glob's thread started, with where the slime is
//                     and whom it found
//   $81:CBDD  rise    its own handler put back, and off a random way
//   $81:CCF0  end     the picture and the touch, and the pass's end: what
//                     `slime_frame` does after a state, for a state that
//                     was the ROM's
//
// And two of the glob's own, before its first pass:
//
//   $81:CECC  dress   a record taken, and put where the slime is
//   $81:CF1D  aim     where it will come down, within sixteen pixels of
//                     whom the slime found
#define SLIME_THROW_PC 0x81cbc0u
#define SLIME_THROW_PICTURES_PC 0x81cbd9u  // `JSL pictures_play`, A the list
#define SLIME_THROW_PICTURES 0xcbfau
#define SLIME_RISE_PC 0x81cbddu
#define SLIME_RISE_RTS_PC 0x81c9f9u  // the `RTS`, which is the ROM's
#define SLIME_END_PC 0x81ccf0u
#define SLIME_HIT_HANDLER 0xcddeu

#define SLIME_GLOB_THREAD 0xcf10u
#define SLIME_GLOB_DRESS_PC 0x81ceccu
#define SLIME_GLOB_DRESS_RTS_PC 0x81cf0fu  // the `RTS`, which is the ROM's
#define SLIME_GLOB_AIM_PC 0x81cf1du
// What the glob's thread is started with, and where it keeps the third.
#define SLIME_GLOB_DP_ARG_X 0x00
#define SLIME_GLOB_DP_ARG_Y 0x02
#define SLIME_GLOB_DP_ARG_TARGET 0x04
#define SLIME_GLOB_DP_TARGET 0x14
#define SLIME_GLOB_HEIGHT 0x000f
#define SLIME_GLOB_PICTURE 0xc3bdu
#define SLIME_GLOB_PICTURE_BANK 0x0090u
#define SLIME_GLOB_ID 0x0036
#define SLIME_GLOB_FLAGS 0x8008u
#define SLIME_GLOB_FIRST_SPEED 0x0018
#define SLIME_GLOB_AIM_MASK 0x001f
#define SLIME_GLOB_AIM_HALF 0x0010

typedef struct {
  int slot;               // throw: the thread `thread_spawn` took, or -1
  bool drew_overflow[2];  // rise's draw, or aim's two
  bool declined;          // dress: no record was free, which is the ROM's
  uint16_t record;        // ...or the one it took
} SlimeAttackWork;

void slime_attack_throw(Wram* w, const Rom* rom, PortCpu* c,
                        SlimeAttackWork* k);
void slime_attack_rise(Wram* w, PortCpu* c, SlimeAttackWork* k);

// Can `slime_frame_end` take this one? It only looks.
bool slime_frame_end_supported(const Wram* w, uint16_t page);

// As `slime_frame`, from after the state. `log->declined` when the touch
// reached a handler the port lacks.
bool slime_frame_end(Wram* w, const Rom* rom, uint16_t page, SlimeLog* log);

void slime_glob_dress(Wram* w, PortCpu* c, SlimeAttackWork* k);

// `$81:CC4F`: a record where the thread was started, facing down, with
// four hits to take, and everything else on its page that a pass reads
// zeroed. It ends at the `JSL` that plays its first pictures. With no
// record free, which the ROM does not test for, it says so in `k`.
void slime_begin(Wram* w, PortCpu* c, SlimeAttackWork* k);
void slime_glob_aim(Wram* w, PortCpu* c, SlimeAttackWork* k);

// **The four stretches of the glob's thread that are a few instructions
// each**, between its sound, its calls and its sleeps:
//
//   $81:CF10  begin   its weight on the level's load, to its first sound
//   $81:CF36  landed  down: to its second sound
//   $81:CE6A  told    whatever it came down on told: to its last pictures
//   $81:CE71  done    those shown: `port/begin.h`'s end, from inside the
//                     `JSR` the landing made
#define SLIME_GLOB_BEGIN_PC 0x81cf10u
#define SLIME_GLOB_BEGIN_SOUND_PC 0x81cec8u   // `JSL apu_play_sfx`
#define SLIME_GLOB_LANDED_SOUND_PC 0x81ce3cu  // `JSL apu_play_sfx`
#define SLIME_GLOB_TOLD_PC 0x81ce6au
#define SLIME_GLOB_PLAY_PC 0x81ce6du          // `JSL pictures_play`
#define SLIME_GLOB_DONE_PC 0x81ce71u
#define SLIME_GLOB_SFX 0x001a
#define SLIME_GLOB_LOAD 0x0004
#define SLIME_GLOB_LAST_PICTURES 0xce72u
// What the thread's two `JSR`s push.
#define SLIME_GLOB_BEGIN_RETURN 0xcf1cu
#define SLIME_GLOB_LANDED_RETURN 0xcf38u

void slime_glob_begin(Wram* w, PortCpu* c);
void slime_glob_landed(Wram* w, PortCpu* c);
void slime_glob_told(PortCpu* c);
// False unless the stack is as the landing left it, and for what
// `record_end` turns down. `*place` is where in the display list its
// record was.
bool slime_glob_done(Wram* w, PortCpu* c, int* place);

// `$81:CE40`, from the return of the splash's sound: the box that
// `actor_notify_box` is asked about, as far as the `JSL` that asks.
#define SLIME_GLOB_SPLASH_PC 0x81ce40u
#define SLIME_GLOB_SPLASH_TELL_PC 0x81ce66u
#define SLIME_SPLASH_HALF_WIDTH 0x0018
#define SLIME_SPLASH_HALF_HEIGHT 0x0010
#define SLIME_SPLASH_ID 0x0034

void slime_glob_splash(Wram* w, PortCpu* c);

#endif
