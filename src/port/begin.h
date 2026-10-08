// A display record begun: three routines a thread's start calls.
//
//   $81:8000  record_begin     a record at the place on the thread's page
//   $81:87BE  zombie_begin     ...and a zombie's made of it
//   $83:A13E  neighbour_begin  a neighbour's record, and their handler
//
// **`record_begin`** takes a free record and puts it at the place in the
// page's first two words, on the ground, with nobody's collide id and this
// thread as its owner. It keeps the record at `$08`. Fourteen kinds of thing
// in bank `$81` begin with it.
//
// **`zombie_begin`** calls that and goes on: the place kept at `$16` and
// `$18`, the first picture of the two kinds of zombie, the record to be
// drawn, and the page's own words for its walk cleared.
//
// **`neighbour_begin`** is the same for the eleven kinds of neighbour, with
// the picture and its bank handed in. A neighbour's collide id is 1, their
// place is kept at `$20` and `$24`, and their handler is `$83:A364`, which
// is how whatever touches them tells their thread.
//
// None of the three tests whether a record was free. With none free the ROM
// goes on with what is no record, and that is the ROM's to do.
//
// ## And ended
//
// **A thing's thread ends the same way whatever it was.** It takes what it
// added to the level's load off again, loads its record, and jumps to
// `actor_slot_free`, whose return ends the thread. Seven instructions, a
// copy for each kind of thing. `record_end` is those and the free they jump
// to, as far as `thread_exit`, where the free's `RTL` goes.
// A load that would go below nothing stops the ROM where it stands, on a
// branch to itself, and that is the ROM's to do.
//
//   $81:83A3  death_pictures   a killed thing's last pictures begun
//
// **A killed thing is heard, and can no longer be touched.** Its thread
// calls this with a list of pictures in A, their bank in Y, and in X the
// word that says a player killed it. It plays a sound, takes the record's
// collide id away, and goes on to `pictures_play`. With anything else in X
// it does nothing at all.
//
// It is two stretches, one either side of the sound. A sound waits until
// the sound chip has taken the last one, and how long that is is not the
// port's to say. So the first stretch ends at the `JSL` that plays it, with
// the list and the bank on the stack, and the second begins where that
// comes back and ends at the `JSL pictures_play`.
//
// Port code: libc only.

#ifndef PORT_BEGIN_H
#define PORT_BEGIN_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/oam.h"
#include "port/wram.h"

#define RECORD_BEGIN_PC 0x818000u
#define RECORD_BEGIN_RTL_PC 0x818023u
#define ZOMBIE_BEGIN_PC 0x8187beu
#define ZOMBIE_BEGIN_RTS_PC 0x8187f7u
#define NEIGHBOUR_BEGIN_PC 0x83a13eu
#define NEIGHBOUR_BEGIN_RTS_PC 0x83a18eu

#define BEGIN_DP_PLACE_X 0x00
#define BEGIN_DP_PLACE_Y 0x02
#define BEGIN_DP_RECORD 0x08

#define ZOMBIE_BEGIN_PICTURE 0xc98au
#define ZOMBIE_BEGIN_META_BANK 0x0090
#define ZOMBIE_BEGIN_ATTR 0x0c00

#define NEIGHBOUR_HANDLER 0xa364u
#define NEIGHBOUR_HANDLER_BANK 0x0083
#define NEIGHBOUR_COLLIDE_ID 0x0001

typedef struct {
  bool declined;    // no record free
  uint16_t record;
} BeginWork;

// The record the search for a free one begins at. Passing over one in use,
// the search subtracts, which leaves overflow clear; taking this one, it
// leaves overflow as it found it.
#define RECORD_SEARCHED_FIRST 0x1acau

void record_begin(Wram* w, PortCpu* c, BeginWork* k);
void zombie_begin(Wram* w, PortCpu* c, BeginWork* k);
void neighbour_begin(Wram* w, PortCpu* c, BeginWork* k);

// A thread's end: where its seven instructions are, what it gives back,
// and which word of its page is its record.
typedef struct {
  uint32_t pc;       // `SEC`
  uint32_t free_pc;  // `JML actor_slot_free`, the record in A
  uint16_t load;
  uint8_t record_at;
  bool calls;        // ...or a `JSL` to it and an `RTL` of its own after
} RecordEnd;

enum {
  RECORD_END_ZOMBIE_SLOW,
  RECORD_END_ZOMBIE_FAST,
  RECORD_END_ZOMBIE_THIRD,
  RECORD_END_SLIME_GLOB,
  RECORD_END_SHOT_5,
  RECORD_END_MARTIAN,          // the loop of one that began on the ground
  RECORD_END_MARTIAN_ARRIVAL,  // ...and of one that came in over the top
  RECORD_END_COUNT
};
extern const RecordEnd RECORD_ENDS[RECORD_END_COUNT];

// Where a thread's last `RTL` goes: `thread_exit`.
#define RECORD_END_EXITED_PC 0x00833eu

// False if the load would go below nothing, if its record is not one the
// display list holds, or if the stack is not as a thread's is begun. It only
// looks, then. `place` is where in the list the record was, which is how
// far the free walks.
bool record_end(Wram* w, PortCpu* c, const RecordEnd* end, int* place);

#define DEATH_PICTURES_PC 0x8183a3u
#define DEATH_PICTURES_SOUND_PC 0x8183adu  // `JSL apu_play_sfx`
#define DEATH_PICTURES_HEARD_PC 0x8183b1u
#define DEATH_PICTURES_PLAY_PC 0x8183bfu   // `JSL pictures_play`
#define DEATH_KILLED 0xf5f5u               // in X: a player killed it
#define DEATH_SFX 0x0021
// False for anything else in X, which is the ROM's.
bool death_pictures(Wram* w, PortCpu* c);
void death_pictures_heard(Wram* w, PortCpu* c);

#endif
