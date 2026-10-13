// The neighbours' threads, between their calls.
//
// `port/neighbours.h`, `port/blinker.h`, `port/stepper.h` and
// `port/jumper.h` are the loops a neighbour shows its pictures in, a frame
// each. This is the rest of each thread: how it starts, what it does when
// the word at `$1E` on its page is set, and the routines in `$83:A18F` to
// `$83:A333` it does that with. A stretch is from where the thread starts
// or a call comes back to the next call, sleep or return.
//
// ## A thread
//
// Nine of the eleven start the same way: the level's load goes up by what
// the neighbour weighs, its kind is put at `$1C`, and `neighbour_begin` is
// called with its first picture. Then each goes to its loop.
//
// What sets `$1E` is the neighbour's handler, `$83:A364`, and what the
// thread does is by the value:
//
//   negative  it ends, and gives its load back
//   2         `$83:A21D`: it shows another set of pictures, and sleeps up
//             to three hundred frames
//   3         `$83:A18F`: it is lost. Its entry in the level's list is
//             marked, the count of neighbours goes down, and it ends
//   4         `$83:A2B4`: a sign beside it, see `port/neighbours.h`
//   other     `$83:A1D5`: it is saved. The same and its kind is added to
//             the player's list, the player is given its award, and it
//             rises: `neighbour_rise_frame`
//
// The two that look about them (`NEIGHBOUR_WATCHES`) are told `3` up to
// twenty-five times before they are lost, and are saved or lost only while
// their entry in the list is not zero.
//
// ## Three that differ
//
// The stepper, `$83:9D00`, shows pictures of its own as it is lost, and
// has no `2`. The jumper, `$83:9EBE`, has a second record, which it makes
// as it starts and hides and frees as it ends.
//
// The bouncer, `$83:9843`, goes up and comes down: its height changes each
// frame by a speed that starts at four and goes down one every eight frames,
// and it shows the next of nine pictures as the speed changes. Where it
// lands it sleeps four frames, plays sound `$1B` and begins again. There is
// one at a time: `W_BOUNCER_UP` is set while one is in the level, and a
// second waits for it, a frame at a time. It is saved when told `1` as it
// lands, and told anything negative it leaves only if a word at `$1FB8` or
// `$1FBA` is set. I have not seen what sets those.
//
// ## Three they call
//
//   $81:8191  the neighbour's entry in the level's list marked `$80`
//   $80:C863  one fewer neighbour, in decimal
//   $80:C81F  a kind added to a player's list of those saved, while the two
//             lists have fewer than ten between them
//
// And `$81:8349` and `$81:8377`, which each play a set of pictures with the
// data bank their own.
//
// I have not seen which neighbour is which.
//
// Port code: libc only.

#ifndef PORT_NEIGHBOUR_THREAD_H
#define PORT_NEIGHBOUR_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

// How a thread starts: what it weighs, its kind, and its first picture with
// the bank its pictures are in.
typedef struct {
  uint32_t pc;
  uint16_t load;
  uint16_t kind;
  uint16_t picture;
  uint16_t bank;
} NeighbourStart;

#define NEIGHBOUR_START_COUNT 9
extern const NeighbourStart NEIGHBOUR_STARTS[NEIGHBOUR_START_COUNT];
#define NEIGHBOUR_START_CALL 0x15  // from where it starts to its `JSR`
#define NEIGHBOUR_DP_KIND 0x1c

// What follows `neighbour_begin`, as far as the loop's first sleep.
typedef struct {
  uint32_t pc;
  uint32_t sleep_pc;
  bool patient;      // a watcher: it is told `3` this many times first
  bool clears_34;
} NeighbourLoopStart;

#define NEIGHBOUR_LOOP_START_COUNT 7
extern const NeighbourLoopStart NEIGHBOUR_LOOP_STARTS[NEIGHBOUR_LOOP_START_COUNT];
#define NEIGHBOUR_PATIENCE 0x0019
#define NEIGHBOUR_DP_PATIENCE 0x26

// One that only goes round its pictures, told.
typedef struct {
  uint32_t told_pc;    // the `BMI` after `LDA $1E : BEQ`
  uint32_t sleep_pc;   // its loop's `JSL`
  uint16_t saved_picture;
  uint16_t award;      // in decimal
  uint16_t sign_picture;  // what it shows beside its sign, or 0: as it is
} NeighbourTold;

#define NEIGHBOUR_TOLD_COUNT 5
extern const NeighbourTold NEIGHBOUR_TOLDS[NEIGHBOUR_TOLD_COUNT];
// From `told_pc`.
#define NEIGHBOUR_TOLD_SAVED_CALL 0x17  // `JSR $A1D5`
#define NEIGHBOUR_TOLD_SAVED 0x1a       // `BRA`, a link
#define NEIGHBOUR_TOLD_LOST_CALL 0x1c   // `JSR $A18F`, the tourists' `$A1B4`
#define NEIGHBOUR_TOLD_END 0x1f
#define NEIGHBOUR_TOLD_TURN_CALL 0x31   // `JSR $A21D`
#define NEIGHBOUR_TOLD_TURNED 0x34
#define NEIGHBOUR_TOLD_SIGN 0x3d

// One that looks about it, told.
typedef struct {
  uint32_t told_pc;
  uint32_t sleep_pc, alarm_sleep_pc;
  uint16_t saved_picture;
  uint16_t award;
  uint16_t sign_picture;
} WatcherTold;

#define WATCHER_TOLD_COUNT 2
extern const WatcherTold WATCHER_TOLDS[WATCHER_TOLD_COUNT];
#define WATCHER_TOLD_SAVED_CALL 0x2a
#define WATCHER_TOLD_SAVED 0x2d  // `BRA`, a link
#define WATCHER_TOLD_SIGN_CALL 0x37
#define WATCHER_TOLD_SIGNED 0x3a
#define WATCHER_TOLD_LOST_CALL 0x56
#define WATCHER_TOLD_END 0x59
#define WATCHER_TOLD_TURN_CALL 0x6b
#define WATCHER_TOLD_TURNED 0x6e

// What `$1E` is set to.
#define NEIGHBOUR_TOLD_TURN 0x0002
#define NEIGHBOUR_TOLD_LOST 0x0003
#define NEIGHBOUR_TOLD_SIGN_UP 0x0004

// `LDA #value` and a call: the whole of a stretch.
typedef struct {
  uint32_t pc;
  uint16_t value;
} NeighbourArgument;

#define NEIGHBOUR_ARGUMENT_COUNT 7
extern const NeighbourArgument NEIGHBOUR_ARGUMENTS[NEIGHBOUR_ARGUMENT_COUNT];

#define NEIGHBOUR_DP_ENTRY 0x06     // its place in the level's list, or $FFFF
#define NEIGHBOUR_DP_SAVED_BY 0x18  // negative: the second player
#define NEIGHBOUR_DP_SIGN 0x2c      // the sign's record
#define NEIGHBOUR_GONE 0x80         // ...in `W_VICTIM_SPAWNED`
#define NEIGHBOUR_KIND_STEPPER 0x000c
#define NEIGHBOUR_SFX_LOST 0x0024
#define NEIGHBOUR_TURN_BANK 0x0090
#define NEIGHBOUR_TURN_HANDLER 0xa264u
#define NEIGHBOUR_TURN_FRAMES 0x012c
#define NEIGHBOUR_RISE_PIXELS 0x0014
#define NEIGHBOUR_SIGN_PICTURE 0xa54bu
#define NEIGHBOUR_SIGN_BANK 0x0090
#define NEIGHBOUR_SIGN_PLACES 0x83a338u  // a kind: across and up, from 2
#define NEIGHBOUR_SIGN_TURNS 0x004b
#define NEIGHBOUR_KIND_LAST 0x0014

// The tourists become two werewolves: where each is put from where they
// stood, and the thread it is.
#define WEREWOLF_THREAD 0xabf5u
#define WEREWOLF_THREAD_BANK 0x0081
#define TOURISTS_GONE_PICTURES 0xa0e8u
#define TOURISTS_GONE_COLLIDE 0x0003

// The lists of those saved, a byte a kind.
#define W_SAVED_COUNT_A 0x1f9cu
#define W_SAVED_COUNT_B 0x1f9eu
#define W_SAVED_KINDS_A 0x1fa0u
#define W_SAVED_KINDS_B 0x1fabu
#define SAVED_KINDS_MOST 0x000a
#define SAVED_SLOT_JUMPS 0x80c85fu  // two words: where a slot's list is added to
#define SAVED_SLOT_A_PC 0xc83fu
#define SAVED_SLOT_B_PC 0xc84fu

#define VICTIM_MARK_GONE_PC 0x818191u
#define VICTIM_MARK_GONE_RTL_PC 0x8181a1u
#define NEIGHBOURS_LEFT_DOWN_PC 0x80c863u
#define NEIGHBOURS_LEFT_DOWN_RTL_PC 0x80c871u
#define SAVED_KIND_ADD_PC 0x80c81fu

// The stepper.
#define STEPPER_STARTED_PC 0x839d18u
#define STEPPER_SAVED_PICTURE 0xe639u
#define STEPPER_AWARD 0x0100
#define STEPPER_SIGN_PICTURE 0xe3f8u
#define STEPPER_END_PC 0x839dc9u
#define STEPPER_SAVED_CALL_PC 0x839d8eu
#define STEPPER_LOST_CALL_PC 0x839d9au    // `JSL $818191`
#define STEPPER_SIGN_CALL_PC 0x839dbau
#define STEPPER_SIGNED_PC 0x839dbdu
#define STEPPER_SIGNED_LOST_PC 0x839dc6u  // `JSR $A18F`

// The jumper.
#define JUMPER_DP_MAT 0x0a  // its second record
#define JUMPER_MAT_PICTURE 0xdf86u
#define JUMPER_MAT_BANK 0x008f
#define JUMPER_SAVED_PICTURE 0xdfd9u
#define JUMPER_AWARD 0x1000
#define JUMPER_LOAD 0x000a
#define JUMPER_END_PC 0x839f68u
#define JUMPER_END_FREE_PC 0x839f76u  // `JSL actor_slot_free`
#define JUMPER_SAVED_HIDE_PC 0x839f54u  // three `JSR $9FD6`
#define JUMPER_LOST_HIDE_PC 0x839f62u
#define JUMPER_TURN_HIDE_PC 0x839f80u
#define JUMPER_SIGN_CALL_PC 0x839f8du
#define JUMPER_SAVED_CALL_PC 0x839f5du

// The bouncer.
#define W_BOUNCER_UP 0x1ff6u
#define W_BOUNCER_LEAVE_A 0x1fb8u
#define W_BOUNCER_LEAVE_B 0x1fbau
#define BOUNCER_LOAD 0x000a
#define BOUNCER_KIND 0x0006
#define BOUNCER_PICTURE 0xdccfu
#define BOUNCER_BANK 0x008f
#define BOUNCER_LANDED_PICTURE 0xdcf0u
#define BOUNCER_SAVED_PICTURE 0xdd63u
#define BOUNCER_AWARD 0x0300
#define BOUNCER_PICTURES 0x83992bu  // nine, the first not shown from here
#define BOUNCER_PICTURE_COUNT 9
#define BOUNCER_SPEED 0x0004
#define BOUNCER_DP_SPEED 0x28
#define BOUNCER_DP_FRAMES 0x2a
#define BOUNCER_SFX 0x001b
#define BOUNCER_WAIT_PC 0x83984bu     // `JSL thread_yield`, A already 1
#define BOUNCER_GONE_PC 0x839864u     // `RTL`
#define BOUNCER_BEGIN_PC 0x83987du    // `JSR neighbour_begin`
#define BOUNCER_SLEEP_PC 0x839897u    // `JSL thread_yield`, A already 1
#define BOUNCER_FRAME_PC 0x83989bu
#define BOUNCER_LAND_PC 0x8398e6u     // `JSL thread_yield`, A already 4
#define BOUNCER_SAVED_CALL_PC 0x83990eu
#define BOUNCER_END_PC 0x839919u

// name, first instruction, the one after its last, and how many branches
// in it are always taken. `tools/cycles816.py` prices each.
#define NT_BLOCKS(X) \
  X(NT_ENTER, 0x839699, 0x8396ae, 0)       /* CLC ... LDX # */ \
  X(NT_STARTED, 0x8396b1, 0x8396bc, 0)     /* STZ $1E ... LDA $1A */ \
  X(NT_PATIENCE, 0x839955, 0x83995a, 0)    /* LDA # : STA $26 */ \
  X(NT_LDA_DP, 0x8396ba, 0x8396bc, 0) \
  X(NT_LDA_IMM, 0x8396b5, 0x8396b8, 0) \
  X(NT_STZ_DP, 0x8396b1, 0x8396b3, 0) \
  X(NT_BRANCH, 0x8396e1, 0x8396e3, 0)      /* not taken */ \
  X(NT_BRA, 0x83971c, 0x83971e, 1) \
  X(NT_JMP, 0x839a12, 0x839a15, 0) \
  X(NT_CMP_BRANCH, 0x8396e3, 0x8396e8, 0)  /* CMP # : Bxx */ \
  X(NT_SAVED, 0x8396f2, 0x8396f8, 0)       /* LDA # : LDX # */ \
  X(NT_SIGN, 0x83971e, 0x839726, 0)        /* LDY $08 : LDA # : STA $0008,Y */ \
  X(NT_RESET, 0x839715, 0x83971c, 0)       /* STZ $10 : LDA # : STA $1A */ \
  X(NT_TOLD_TEST, 0x8396dd, 0x8396e1, 0)   /* LDA $1E : Bxx */ \
  X(NT_PATIENCE_DOWN, 0x8399db, 0x8399df, 0)  /* DEC $26 : BMI */ \
  X(NT_THERE, 0x8399e3, 0x8399ee, 0)       /* LDX $06 ... BEQ */ \
  X(NT_TOLD_CMP, 0x839a0b, 0x839a12, 0)    /* LDA $1E : CMP # : Bxx */ \
  X(NT_LOST, 0x83a18f, 0x83a196, 0) \
  X(NT_LOST_KIND, 0x83a19e, 0x83a1a5, 0) \
  X(NT_LOST_B, 0x83a1b4, 0x83a1be, 0) \
  X(NT_SAVE_HEAD, 0x83a1d5, 0x83a1d9, 0) \
  X(NT_SAVE_KIND, 0x83a1e1, 0x83a1e5, 0) \
  X(NT_SAVE_AWARD, 0x83a1e9, 0x83a1ec, 0) \
  X(NT_SAVE_HANDLER, 0x83a1f0, 0x83a1f4, 0) \
  X(NT_SAVE_RISE, 0x83a1f8, 0x83a20c, 0) \
  X(NT_TURN_HEAD, 0x83a21d, 0x83a231, 0) \
  X(NT_TURN_WAIT, 0x83a23d, 0x83a245, 0) \
  X(NT_TURN_TAIL, 0x83a24d, 0x83a25f, 0) \
  X(NT_SIGN_MAKE, 0x83a2ba, 0x83a306, 0) \
  X(NT_SIGN_OVER, 0x83a328, 0x83a32d, 0) \
  X(NT_PLAY_A, 0x818349, 0x81834f, 0) \
  X(NT_PLAY_A_TAIL, 0x818353, 0x818356, 0) \
  X(NT_PLAY_B, 0x818377, 0x818389, 0) \
  X(NT_PLAY_B_TAIL, 0x81838d, 0x81838e, 0) \
  X(NT_WOLVES_SHOW, 0x83a08c, 0x83a097, 0) \
  X(NT_WOLF_LEFT, 0x83a09b, 0x83a0ad, 0) \
  X(NT_WOLF_RIGHT, 0x83a0b1, 0x83a0c7, 0) \
  X(NT_GONE_TEST, 0x818191, 0x818196, 0) \
  X(NT_GONE_SET, 0x818196, 0x8181a1, 0) \
  X(NT_LEFT_DOWN, 0x80c863, 0x80c871, 0) \
  X(NT_KIND_HEAD, 0x80c81f, 0x80c822, 0)   /* PHY : BMI */ \
  X(NT_KIND_FIRST, 0x80c822, 0x80c827, 1)  /* LDA # : BRA */ \
  X(NT_JSR, 0x80c82a, 0x80c82d, 0) \
  X(NT_SLOT_TEST, 0x80c7c2, 0x80c7c7, 0)   /* CMP abs : BNE */ \
  X(NT_SLOT_FIRST, 0x80c7c7, 0x80c7cc, 1) \
  X(NT_RTS, 0x80c7d4, 0x80c7d5, 0) \
  X(NT_KIND_ROOM, 0x80c82d, 0x80c83a, 0)   /* TAX ... BCS */ \
  X(NT_KIND_JUMP, 0x80c83a, 0x80c83d, 0) \
  X(NT_KIND_ADD, 0x80c83f, 0x80c84e, 0) \
  X(NT_PLA, 0x80c83d, 0x80c83e, 0) \
  X(NT_STEPPER_STARTED, 0x839d18, 0x839d28, 0) \
  X(NT_MAT_MAKE, 0x839f9d, 0x839fc9, 0) \
  X(NT_MAT_HIDE, 0x839fd6, 0x839fe1, 0) \
  X(NT_END_CALL, 0x839f68, 0x839f76, 0)    /* SEC ... LDA $08 */ \
  X(NT_MAT_FREE, 0x839f7a, 0x839f80, 0)    /* LDA $0A : JML */ \
  X(NT_ABS_TEST, 0x839843, 0x839848, 0)    /* LDA abs : Bxx */ \
  X(NT_BOUNCER_GONE, 0x83984f, 0x83985d, 0) \
  X(NT_BOUNCER_ENTER, 0x839865, 0x83987d, 0) \
  X(NT_BOUNCER_STARTED, 0x839880, 0x839894, 0) \
  X(NT_BOUNCER_LEAVE, 0x83989f, 0x8398a7, 0)  /* LDA abs : ORA abs : BEQ */ \
  X(NT_BOUNCER_MOVE, 0x8398aa, 0x8398b7, 0) \
  X(NT_BOUNCER_COUNT, 0x8398b7, 0x8398c0, 0) \
  X(NT_BOUNCER_PICTURE, 0x8398c0, 0x8398d2, 1) \
  X(NT_BOUNCER_LAND, 0x8398d2, 0x8398e6, 0) \
  X(NT_BOUNCER_LANDED, 0x8398f1, 0x8398fd, 0) \
  X(NT_BOUNCER_OFF, 0x839916, 0x839919, 0)  /* STZ $1FF6 */

enum {
#define X(name, from, to, taken) name,
  NT_BLOCKS(X)
#undef X
  NT_TAKEN,  // a branch taken
  NT_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[NT_BLOCK_COUNT];
} NeighbourThreadWork;

// What a stretch needs of where it starts.
typedef enum {
  NT_NEEDS_NOTHING,
  NT_NEEDS_RECORD,   // the neighbour's record on its page, in low WRAM
  NT_NEEDS_A,        // a record in A
  NT_NEEDS_Y,        // ...or in Y
  NT_NEEDS_KIND,     // and a kind the sign has a place for
  NT_NEEDS_MAT,      // the jumper's second record as well
  NT_NEEDS_LOAD,     // the level's load is no less than the jumper's
  NT_NEEDS_PICTURE,  // the bouncer's next picture is read from the cartridge
  NT_NEEDS_SLOT,     // the player has one of the two lists
} NeighbourThreadNeeds;

bool neighbour_thread_needs_met(const Wram* w, const Rom* rom,
                                NeighbourThreadNeeds needs, const PortCpu* c);

// name, symbol, entry, the port, its exits and what it needs.
#define NEIGHBOUR_THREAD_STRETCHES(X) \
  X(enter_a, "$83:9699", 0x839699u, neighbour_enter, ENTER, NT_NEEDS_NOTHING) \
  X(enter_b, "$83:9776", 0x839776u, neighbour_enter, ENTER, NT_NEEDS_NOTHING) \
  X(enter_watch_b, "$83:993D", 0x83993du, neighbour_enter, ENTER, NT_NEEDS_NOTHING) \
  X(enter_watch_a, "$83:9A89", 0x839a89u, neighbour_enter, ENTER, NT_NEEDS_NOTHING) \
  X(enter_blinker, "$83:9C6D", 0x839c6du, neighbour_enter, ENTER, NT_NEEDS_NOTHING) \
  X(enter_stepper, "$83:9D00", 0x839d00u, neighbour_enter, ENTER, NT_NEEDS_NOTHING) \
  X(enter_c, "$83:9E15", 0x839e15u, neighbour_enter, ENTER, NT_NEEDS_NOTHING) \
  X(enter_jumper, "$83:9EBE", 0x839ebeu, neighbour_enter, ENTER, NT_NEEDS_NOTHING) \
  X(enter_tourists, "$83:9FE2", 0x839fe2u, neighbour_enter, ENTER, NT_NEEDS_NOTHING) \
  X(started_a, "$83:96B1", 0x8396b1u, neighbour_started, STARTED, NT_NEEDS_NOTHING) \
  X(started_b, "$83:978E", 0x83978eu, neighbour_started, STARTED, NT_NEEDS_NOTHING) \
  X(started_watch_b, "$83:9955", 0x839955u, neighbour_started, STARTED, NT_NEEDS_NOTHING) \
  X(started_watch_a, "$83:9AA1", 0x839aa1u, neighbour_started, STARTED, NT_NEEDS_NOTHING) \
  X(started_blinker, "$83:9C85", 0x839c85u, neighbour_started, STARTED, NT_NEEDS_NOTHING) \
  X(started_c, "$83:9E2D", 0x839e2du, neighbour_started, STARTED, NT_NEEDS_NOTHING) \
  X(started_tourists, "$83:9FFA", 0x839ffau, neighbour_started, STARTED, NT_NEEDS_NOTHING) \
  X(told_a, "$83:96E1", 0x8396e1u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(turned_a, "$83:9715", 0x839715u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(signed_a, "$83:9729", 0x839729u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(told_b, "$83:97BE", 0x8397beu, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(turned_b, "$83:97F2", 0x8397f2u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(signed_b, "$83:9806", 0x839806u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(told_blinker, "$83:9CAF", 0x839cafu, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(turned_blinker, "$83:9CE3", 0x839ce3u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(signed_blinker, "$83:9CEF", 0x839cefu, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(told_c, "$83:9E5D", 0x839e5du, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(turned_c, "$83:9E91", 0x839e91u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(signed_c, "$83:9EA5", 0x839ea5u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(told_tourists, "$83:A035", 0x83a035u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(turned_tourists, "$83:A069", 0x83a069u, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(signed_tourists, "$83:A07D", 0x83a07du, neighbour_told, TOLD, NT_NEEDS_RECORD) \
  X(told_watch_b, "$83:99CA", 0x8399cau, watcher_told, WATCHER, NT_NEEDS_RECORD) \
  X(signed_watch_b, "$83:9A04", 0x839a04u, watcher_told, WATCHER, NT_NEEDS_RECORD) \
  X(turned_watch_b, "$83:9A38", 0x839a38u, watcher_told, WATCHER, NT_NEEDS_RECORD) \
  X(told_watch_a, "$83:9B1E", 0x839b1eu, watcher_told, WATCHER, NT_NEEDS_RECORD) \
  X(signed_watch_a, "$83:9B58", 0x839b58u, watcher_told, WATCHER, NT_NEEDS_RECORD) \
  X(turned_watch_a, "$83:9B8C", 0x839b8cu, watcher_told, WATCHER, NT_NEEDS_RECORD) \
  X(lost, "$83:A18F", 0x83a18fu, neighbour_lost, LOST, NT_NEEDS_RECORD) \
  X(lost_counted, "$83:A19E", 0x83a19eu, neighbour_lost_counted, LOST, NT_NEEDS_NOTHING) \
  X(tourists_lost, "$83:A1B4", 0x83a1b4u, tourists_lost, LOST, NT_NEEDS_RECORD) \
  X(saved, "$83:A1D5", 0x83a1d5u, neighbour_saved, SAVED, NT_NEEDS_NOTHING) \
  X(saved_counted, "$83:A1E1", 0x83a1e1u, neighbour_saved_counted, SAVED, NT_NEEDS_NOTHING) \
  X(saved_listed, "$83:A1E9", 0x83a1e9u, neighbour_saved_listed, SAVED, NT_NEEDS_NOTHING) \
  X(saved_paid, "$83:A1F0", 0x83a1f0u, neighbour_saved_paid, SAVED, NT_NEEDS_NOTHING) \
  X(saved_rise, "$83:A1F8", 0x83a1f8u, neighbour_saved_rise, SAVED, NT_NEEDS_RECORD) \
  X(turn, "$83:A21D", 0x83a21du, neighbour_turn, TURN, NT_NEEDS_RECORD) \
  X(turn_shown, "$83:A239", 0x83a239u, neighbour_turn_shown, TURN, NT_NEEDS_NOTHING) \
  X(turn_over, "$83:A24D", 0x83a24du, neighbour_turn_over, TURN, NT_NEEDS_RECORD) \
  X(sign, "$83:A2B4", 0x83a2b4u, neighbour_sign, SIGN, NT_NEEDS_NOTHING) \
  X(sign_made, "$83:A2BA", 0x83a2bau, neighbour_sign_made, SIGN, NT_NEEDS_KIND) \
  X(sign_told, "$83:A324", 0x83a324u, neighbour_sign_end, SIGN, NT_NEEDS_NOTHING) \
  X(sign_over, "$83:A328", 0x83a328u, neighbour_sign_end, SIGN, NT_NEEDS_NOTHING) \
  X(play_a, "$81:8349", 0x818349u, neighbour_play, PLAY, NT_NEEDS_NOTHING) \
  X(play_a_end, "$81:8353", 0x818353u, neighbour_play_end, PLAY, NT_NEEDS_NOTHING) \
  X(play_b, "$81:8377", 0x818377u, neighbour_play, PLAY, NT_NEEDS_NOTHING) \
  X(play_b_end, "$81:838D", 0x81838du, neighbour_play_end, PLAY, NT_NEEDS_NOTHING) \
  X(tourists_turn, "$83:A086", 0x83a086u, tourists_turn, WOLVES, NT_NEEDS_NOTHING) \
  X(tourists_gone, "$83:A08C", 0x83a08cu, tourists_gone, WOLVES, NT_NEEDS_RECORD) \
  X(werewolf_left, "$83:A09B", 0x83a09bu, tourists_werewolf_left, WOLVES, NT_NEEDS_NOTHING) \
  X(werewolf_right, "$83:A0B1", 0x83a0b1u, tourists_werewolf_right, WOLVES, NT_NEEDS_NOTHING) \
  X(argument_lost, "$83:A1AC", 0x83a1acu, neighbour_argument, ARGUMENT, NT_NEEDS_NOTHING) \
  X(argument_tourists_cry, "$83:A1C6", 0x83a1c6u, neighbour_argument, ARGUMENT, NT_NEEDS_NOTHING) \
  X(argument_tourists_lost, "$83:A1CD", 0x83a1cdu, neighbour_argument, ARGUMENT, NT_NEEDS_NOTHING) \
  X(argument_werewolves, "$83:A0CB", 0x83a0cbu, neighbour_argument, ARGUMENT, NT_NEEDS_NOTHING) \
  X(argument_stepper_cry, "$83:9D9E", 0x839d9eu, neighbour_argument, ARGUMENT, NT_NEEDS_NOTHING) \
  X(argument_stepper_lost, "$83:9DA5", 0x839da5u, neighbour_argument, ARGUMENT, NT_NEEDS_NOTHING) \
  X(argument_bouncer, "$83:98EA", 0x8398eau, neighbour_argument, ARGUMENT, NT_NEEDS_NOTHING) \
  X(victim_mark_gone, "$81:8191", VICTIM_MARK_GONE_PC, victim_mark_gone, LEAF, NT_NEEDS_NOTHING) \
  X(neighbours_left_down, "$80:C863", NEIGHBOURS_LEFT_DOWN_PC, neighbours_left_down, LEAF, NT_NEEDS_NOTHING) \
  X(saved_kind_add, "$80:C81F", SAVED_KIND_ADD_PC, saved_kind_add, LEAF, NT_NEEDS_SLOT) \
  X(stepper_started, "$83:9D18", STEPPER_STARTED_PC, stepper_started, STEPPER, NT_NEEDS_NOTHING) \
  X(stepper_told, "$83:9D7C", 0x839d7cu, stepper_told, STEPPER, NT_NEEDS_RECORD) \
  X(stepper_signed, "$83:9DBD", STEPPER_SIGNED_PC, stepper_signed, STEPPER, NT_NEEDS_RECORD) \
  X(jumper_mat_made, "$83:9F9D", 0x839f9du, jumper_mat_made, JUMPER, NT_NEEDS_A) \
  X(jumper_started, "$83:9ED9", 0x839ed9u, jumper_started, JUMPER, NT_NEEDS_NOTHING) \
  X(jumper_told, "$83:9F41", 0x839f41u, jumper_told, JUMPER, NT_NEEDS_NOTHING) \
  X(jumper_mat_hide, "$83:9FD6", 0x839fd6u, jumper_mat_hide, JUMPER, NT_NEEDS_MAT) \
  X(jumper_saved, "$83:9F57", 0x839f57u, jumper_saved, JUMPER, NT_NEEDS_NOTHING) \
  X(jumper_end, "$83:9F68", JUMPER_END_PC, jumper_end, JUMPER_END, NT_NEEDS_LOAD) \
  X(jumper_mat_free, "$83:9F7A", 0x839f7au, jumper_mat_free, JUMPER_END, NT_NEEDS_NOTHING) \
  X(jumper_turned, "$83:9F86", 0x839f86u, jumper_turned, JUMPER, NT_NEEDS_NOTHING) \
  X(jumper_signed, "$83:9F90", 0x839f90u, jumper_signed, JUMPER, NT_NEEDS_NOTHING) \
  X(bouncer_enter, "$83:9843", 0x839843u, bouncer_enter, BOUNCER, NT_NEEDS_NOTHING) \
  X(bouncer_waited, "$83:984F", 0x83984fu, bouncer_waited, BOUNCER, NT_NEEDS_NOTHING) \
  X(bouncer_started, "$83:9880", 0x839880u, bouncer_started, BOUNCER, NT_NEEDS_RECORD) \
  X(bouncer_frame, "$83:989B", BOUNCER_FRAME_PC, bouncer_frame, BOUNCER, NT_NEEDS_PICTURE) \
  X(bouncer_landed, "$83:98F1", 0x8398f1u, bouncer_landed, BOUNCER, NT_NEEDS_RECORD) \
  X(bouncer_saved, "$83:9911", 0x839911u, bouncer_saved, BOUNCER, NT_NEEDS_NOTHING)

#define X(name, sym, entry, port, exits, needs) \
  void port(Wram* w, const Rom* rom, PortCpu* c, NeighbourThreadWork* k);
// Several stretches share a port, so each is declared as often.
NEIGHBOUR_THREAD_STRETCHES(X)
#undef X

#endif
