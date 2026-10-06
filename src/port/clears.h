// What the game forgets between one thing and the next.
//
// Two routines, called one after the other when a game ends and when the
// demo does, put WRAM back to nothing:
//
//   $80:895A  game_clear     everything a game keeps, in three stretches,
//                            of which the first two are here
//   $80:8992  threads_clear  the threads' pages and stacks
//
// ## The game's three stretches
//
// `$7E:2128-$6E31` is everything above the top-scores table, which is what a
// reset with the table intact keeps (`port/sched.h`, the reset's clear): the
// level's maps, the text layer's, the lists. `$28-$EE` is page zero from the
// first word that is not the scheduler's. `$137E-$1FFB` is the game's own
// variables. The vblank queues, the scroll shadows and the scheduler's
// tables lie between them and are left.
//
// Each is cleared as the reset clears: one zero word stored, and the stretch
// copied onto itself a byte along with `MVN`. The first takes two and a half
// frames. At a game's end no interrupt lands in it. After a demo the NMI is
// on and lands in it every time, and now and then in the third. That is why
// the routine is a stretch for each copy here, and not one: what the handler
// writes into the second and third while the first is being cleared is
// cleared after it, as in the ROM.
//
// **The third stretch is the ROM's.** A port clears a stretch at once, where
// the ROM takes its time, and the NMI's handler reads a word of this one:
// `$7E:1EB4`, which holds the random numbers still while it is set. The ROM
// reaches that word nine tenths of the way through. An NMI that lands before
// then finds it set and leaves the numbers alone; with the stretch cleared
// at once it steps them. One step is enough to change which demo comes next.
// Run in lockstep, a port of this stretch left `$7E:0024` one ahead on two
// movies, and one of the two went on to a different game.
//
// ## The threads
//
// `$0100-$0BFF` and `$0C80-$0CFF`, a word at a time. `$0C00-$0C7F`, between
// them, is left.
//
// ## Their contract with the ROM
//
// WRAM as the ROM leaves it. The registers are the copy's: A at `$FFFF`, X
// on the last byte and Y one past it, and the data bank the copy's own:
// `$7E` after the first stretch and `$00` after the second.
//
// Port code: libc only.

#ifndef PORT_CLEARS_H
#define PORT_CLEARS_H

#include <stdint.h>

#include "port/wram.h"

#define GAME_CLEAR_PC 0x80895au
#define GAME_CLEAR_PAGE_PC 0x80896du  // the second stretch
#define GAME_CLEAR_VARS_PC 0x80897fu  // the third, which is the ROM's
#define THREADS_CLEAR_PC 0x808992u
#define THREADS_CLEAR_RTL_PC 0x8089afu

typedef struct {
  uint32_t first, last;  // inclusive
} ClearSpan;

#define GAME_CLEAR_HIGH ((ClearSpan){0x2128u, 0x6e31u})
#define GAME_CLEAR_PAGE ((ClearSpan){0x0028u, 0x00eeu})
#define THREADS_CLEAR_PAGES ((ClearSpan){0x0100u, 0x0bffu})
#define THREADS_CLEAR_LAST ((ClearSpan){0x0c80u, 0x0cffu})

static inline uint32_t clear_span_bytes(ClearSpan s) {
  return s.last - s.first + 1;
}

void clear_span(Wram* w, ClearSpan s);

// `$80:8992`, both of its spans.
void threads_clear(Wram* w);

// `$80:BDF2`: every display record is free again. The flags word of each of
// the thirty-two is cleared, and the list has no first record. Nothing else
// of a record is touched.
#define ACTOR_SLOTS_CLEAR_PC 0x80bdf2u
#define ACTOR_SLOTS_CLEAR_RTL_PC 0x80be0bu
void actor_slots_clear(Wram* w);

#endif
