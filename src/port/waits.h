// Three threads with nothing to do but wait.
//
//   $83:AAB2  near_wait   until a player is within 250 of a point
//   $81:A478  idle_wait   ...within 80 of the thing in record 48, which
//                         shows the next of four pictures while it waits
//   $82:F280  hits_wait   until the thing in record 36 has been hit six
//                         times, and it shows four pictures in turn too
//
// Each is a loop round a sleep, and a tick of one is from where the sleep
// comes back to the next sleep or to the end of the wait. The sleep itself
// is `thread_yield`'s, as ever.
//
// **`near_wait`** is called by the thread at `$83:AD33`, which two level
// records have. It copies the point it was given, `$00` and `$02` on its
// page, to `$3C` and `$3E`, and asks `player_in_range` about it every four
// ticks. It returns with the player's record in A and in Y.
//
// **`idle_wait`** is in the thread of record 48's own, `$81:A455`. Every
// two ticks it calls `$81:A603`: `player_bearing` from the point at `$16`
// and `$18`, and with nobody within 80 the count at `$0A` goes down. Under
// zero it is 4 again and the record's picture is the next of the four at
// `$81:A630`. So a picture is shown for ten ticks. The wait ends with a
// player found, at `$81:A484`, or with `$2A` set, at `$81:A523`: that is
// what its collide handler counts down when the record is taken away, and
// the thread ends there.
//
// **`hits_wait`** is in the thread at `$82:F25D`. Every two ticks the
// count at `$0C` goes down, and under zero it is 2 again and the record has
// the next of the four pictures at `$82:F2DC`, with the collide id that goes
// with it from `$82:F2E4`. So a picture is shown for six ticks. Its collide
// handler, `actor_f330_collide`, leaves what hit it at `$16` and counts `$10`
// down from 5 for four of the weapons. The tick clears `$16`, and with the
// count under zero the wait is over, at `$82:F2C2`. `$FF` there is the
// record taken away, and the thread ends at `$82:F2CA`.
//
// I have not seen any of them on a screen.
//
// Port code: libc only.

#ifndef PORT_WAITS_H
#define PORT_WAITS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/wram.h"

// --- $83:AAB2 ----------------------------------------------------------------

#define NEAR_WAIT_BANK 0x83u
#define NEAR_WAIT_PC 0x83aab2u
#define NEAR_WAIT_WOKEN_PC 0x83aacfu  // where its sleep comes back
#define NEAR_WAIT_YIELD_PC 0x83aacbu  // `JSL thread_yield`, A already 4
#define NEAR_WAIT_RTS_PC 0x83aad1u
#define NEAR_WAIT_ASK_AT 0xaac1u      // its `JSL player_in_range`

#define NEAR_WAIT_DP_GIVEN_X 0x00
#define NEAR_WAIT_DP_GIVEN_Y 0x02
#define NEAR_WAIT_DP_X 0x3c
#define NEAR_WAIT_DP_Y 0x3e
#define NEAR_WAIT_WITHIN 0x00fa
#define NEAR_WAIT_TICKS 0x0004

typedef struct {
  bool begun;              // entered at the top, which copies the point
  PlayerPickRegs players;  // what `player_in_range` did
  bool found;
} NearWait;

// Entered at `NEAR_WAIT_PC` or `NEAR_WAIT_WOKEN_PC`, by `c->pc`. It stops at
// `NEAR_WAIT_YIELD_PC` or at `NEAR_WAIT_RTS_PC`.
void near_wait(Wram* w, PortCpu* c, NearWait* did);

// --- $81:A478 ----------------------------------------------------------------

#define IDLE_WAIT_BANK 0x81u
#define IDLE_WAIT_PC 0x81a478u        // where its sleep comes back
#define IDLE_WAIT_YIELD_PC 0x81a474u  // `JSL thread_yield`, A already 2
#define IDLE_WAIT_FOUND_PC 0x81a484u
#define IDLE_WAIT_GONE_PC 0x81a523u
#define IDLE_WAIT_LOOK_RET 0xa47au    // what `JSR $A603` pushes
#define IDLE_WAIT_ASK_AT 0xa60au      // its `JSL player_bearing`
#define IDLE_WAIT_PICTURES 0x81a630u  // four words

#define IDLE_WAIT_DP_RECORD 0x08
#define IDLE_WAIT_DP_LEFT 0x0a     // ticks of this picture left
#define IDLE_WAIT_DP_PICTURE 0x0c  // which is next, doubled
#define IDLE_WAIT_DP_X 0x16
#define IDLE_WAIT_DP_Y 0x18
#define IDLE_WAIT_DP_GONE 0x2a
#define IDLE_WAIT_WITHIN 0x0050
#define IDLE_WAIT_PICTURE_TICKS 0x0004
#define IDLE_WAIT_TICKS 0x0002

typedef enum {
  IDLE_WAIT_ON,     // another two ticks
  IDLE_WAIT_FOUND,  // a player is near
  IDLE_WAIT_GONE,   // its record was taken away
} IdleWaitEnd;

typedef struct {
  PlayerPickRegs players;  // what `player_bearing` did
  bool turned;             // the next picture was put up
  IdleWaitEnd end;
} IdleWait;

// False for a page the port will not follow: a record or a picture that is
// not one.
bool idle_wait_supported(const Wram* w, uint16_t page);
void idle_wait(Wram* w, const Rom* rom, PortCpu* c, IdleWait* did);

// --- $82:F280 ----------------------------------------------------------------

#define HITS_WAIT_BANK 0x82u
#define HITS_WAIT_PC 0x82f280u         // where its sleep comes back
#define HITS_WAIT_YIELD_PC 0x82f27cu   // `JSL thread_yield`, A already 2
#define HITS_WAIT_BROKEN_PC 0x82f2c2u
#define HITS_WAIT_GONE_PC 0x82f2cau
#define HITS_WAIT_PICTURES 0x82f2dcu   // four words
#define HITS_WAIT_COLLIDE_IDS 0x82f2e4u  // ...and the id that goes with each
#define HITS_WAIT_PICTURE_BANK 0x008f

#define HITS_WAIT_DP_RECORD 0x08
#define HITS_WAIT_DP_PICTURE 0x0a  // which is shown
#define HITS_WAIT_DP_LEFT 0x0c     // ticks of it left
#define HITS_WAIT_DP_HITS 0x10     // hits it can still take
#define HITS_WAIT_DP_HIT_BY 0x16
#define HITS_WAIT_PICTURE_COUNT 4
#define HITS_WAIT_PICTURE_TICKS 0x0002
#define HITS_WAIT_TAKEN_AWAY 0x00ff
#define HITS_WAIT_TICKS 0x0002

typedef enum {
  HITS_WAIT_QUIET,   // nothing hit it: another two ticks
  HITS_WAIT_HIT,     // ...or something did, and it can take more
  HITS_WAIT_BROKEN,  // ...or it cannot
  HITS_WAIT_GONE,    // its record was taken away
} HitsWaitEnd;

typedef struct {
  bool turned;   // the next picture was put up
  bool wrapped;  // ...and it was the first again
  HitsWaitEnd end;
} HitsWait;

// False for a page the port will not follow: a record or a picture that is
// not one.
bool hits_wait_supported(const Wram* w, uint16_t page);
void hits_wait(Wram* w, const Rom* rom, PortCpu* c, HitsWait* did);

#endif
