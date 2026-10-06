// The neighbours' loops, a frame each.
//
// A neighbour is a thread, one of eleven in bank `$83`, begun by
// `neighbour_begin` (`port/begin.h`). Each shows its pictures until
// something sets the word at `$1E` on its page, and what it does then is by
// what was set: see `port/blinker.h`, `port/stepper.h` and `port/jumper.h`
// for three others. Six are here, of three kinds.
//
// **Three only go round their pictures**, each for the frames a table gives:
//
//   $83:96C0  the neighbour of `$83:9699`, seventeen pictures
//   $83:979D  ...of `$83:9776`, thirteen
//   $83:9E3C  ...of `$83:9E15`, four
//
// **The tourists do that and ask a question first.**
//
//   $83:A009  tourists_frame   the tourists, `$83:9FE2`
//
// They have eighteen pictures. Each frame they ask whether the level has
// room for another thing, and if it has and the word at `$1F94` is set they
// leave at `$83:A086`, where the ROM puts two werewolves in their place.
// That is what makes them the tourists.
//
// **Two look about them.** Each time they have shown a picture they look
// for the nearest thing with collide id 3, and with one within a hundred
// they show other pictures before they look again.
//
//   $83:9AB7 and $83:9AFB  the neighbour of `$83:9A89`
//   $83:9969 and $83:99A6  ...of `$83:993D`
//
// The first has two pictures at ease and two alarmed, and cries out as it
// begins: sound `$30`. The second has four at ease and eleven alarmed, and
// makes no sound.
//
// I have not seen which neighbour is which, the tourists apart.
//
// **What a neighbour leaves** is the record itself with another picture:
// `$83:A1D5` counts the neighbour for the player and sets it rising.
//
//   $83:A210  neighbour_rise_frame
//
// It goes up a pixel every other frame, twenty times, and the ROM takes it
// from there.
//
// **A sign beside them.** Told `4`, ten of the eleven call `$83:A2B4`, which
// makes a second record beside the neighbour and sleeps inside itself: eight
// ticks at a time it shows one of two pictures and then the other, up to
// seventy-five times, or until something sets the word again.
//
//   $83:A30A  neighbour_sign_frame
//
// I have not seen what the sign is.
//
// ## Their contract with the ROM
//
// A frame is from the return of the thread's sleep to the next sleep, with
// the frames in A, or to where the ROM deals with the word. The search
// leaves an overflow the port does not follow; nothing else in a frame
// changes it. A frame with the cry in it is checked and not priced: a sound
// has no price.
//
// Port code: libc only.

#ifndef PORT_NEIGHBOURS_H
#define PORT_NEIGHBOURS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/apu.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/thread.h"
#include "port/wram.h"

#define NEIGHBOUR_BANK 0x83
#define NEIGHBOUR_DP_RECORD 0x08
#define NEIGHBOUR_DP_WHICH 0x10   // which picture
#define NEIGHBOUR_DP_FRAMES 0x1a  // ...and how long it is shown
#define NEIGHBOUR_DP_TOLD 0x1e
#define NEIGHBOUR_DP_X 0x20
#define NEIGHBOUR_DP_Y 0x24

// A loop that goes round its pictures: where a frame of it begins and
// leaves, and its table of a picture and its frames.
typedef struct {
  uint32_t pc;
  uint32_t sleep_pc;  // `JSL`, A already the frames
  uint32_t told_pc;
  uint32_t pictures;
  uint16_t count;
} NeighbourCycle;

#define NEIGHBOUR_CYCLE_COUNT 3
extern const NeighbourCycle NEIGHBOUR_CYCLES[NEIGHBOUR_CYCLE_COUNT];

#define TOURISTS_PC 0x83a009u
#define TOURISTS_SLEEP_PC 0x83a005u
#define TOURISTS_TURN_PC 0x83a086u  // into werewolves
#define TOURISTS_TOLD_PC 0x83a035u
#define TOURISTS_PICTURES 0x83a0f6u
#define TOURISTS_PICTURE_COUNT 0x0012
#define W_TOURISTS_TURN 0x1f94u

// One that looks about it: its two loops, at ease and alarmed.
typedef struct {
  uint32_t pc, alarm_pc;
  uint32_t sleep_pc, alarm_sleep_pc;  // each loop's `JSL`
  uint32_t told_pc;
  uint32_t pictures;
  uint16_t count;
  uint32_t alarm_pictures;
  uint16_t alarm_count;
  // The first counts its alarm after showing, from the first picture; the
  // second before, from the second.
  bool counts_first;
  uint16_t sfx;  // the cry, or none
} NeighbourWatch;

#define NEIGHBOUR_WATCH_COUNT 2
extern const NeighbourWatch NEIGHBOUR_WATCHES[NEIGHBOUR_WATCH_COUNT];
#define NEIGHBOUR_WATCH_NEAR 0x0064

#define NEIGHBOUR_RISE_PC 0x83a210u
#define NEIGHBOUR_RISE_SLEEP_PC 0x83a20cu  // `JSL`, A already 2
#define NEIGHBOUR_RISE_DONE_PC 0x83a21cu   // the `RTS`
#define NEIGHBOUR_RISE_DP_LEFT 0x10
#define NEIGHBOUR_RISE_FRAMES 2

#define NEIGHBOUR_SIGN_PC 0x83a30au
#define NEIGHBOUR_SIGN_SLEEP_PC 0x83a306u  // `JSL`, A already 8
#define NEIGHBOUR_SIGN_OVER_PC 0x83a328u   // its turns are up
#define NEIGHBOUR_SIGN_TOLD_PC 0x83a324u
#define NEIGHBOUR_SIGN_PICTURES 0x83a334u  // two
#define NEIGHBOUR_SIGN_DP_RECORD 0x2c
#define NEIGHBOUR_SIGN_DP_LEFT 0x2e
#define NEIGHBOUR_SIGN_DP_WHICH 0x30       // goes up by two; bit 1 is which
#define NEIGHBOUR_SIGN_TICKS 8

typedef struct {
  // The tourists.
  bool no_room;   // the level is full, so the night is not asked about
  bool turned;
  bool wrapped;   // the pictures went round, in any loop
  bool told;
  // One that looks about it.
  bool looked;
  ActorNearestWork nearest;
  bool near;      // one of them within a hundred
  bool cried;     // ...and the sound was played
  bool over;      // the alarm's pictures are done
  // What they leave.
  bool risen;
  // The sign: its turns are up. `told` is the other way it ends.
  bool sign_over;
} NeighbourWork;

void neighbour_cycle_frame(Wram* w, const Rom* rom, PortCpu* c,
                           NeighbourWork* k, const NeighbourCycle* cycle);
void tourists_frame(Wram* w, const Rom* rom, PortCpu* c, NeighbourWork* k);
void neighbour_watch_frame(Wram* w, const Rom* rom, PortCpu* c,
                           NeighbourWork* k, const NeighbourWatch* watch);
void neighbour_alarm_frame(Wram* w, const Rom* rom, PortCpu* c,
                           NeighbourWork* k, const NeighbourWatch* watch);
void neighbour_rise_frame(Wram* w, PortCpu* c, NeighbourWork* k);
void neighbour_sign_frame(Wram* w, const Rom* rom, PortCpu* c,
                          NeighbourWork* k);

#endif
