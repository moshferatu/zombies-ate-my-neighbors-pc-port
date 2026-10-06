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

void record_begin(Wram* w, PortCpu* c, BeginWork* k);
void zombie_begin(Wram* w, PortCpu* c, BeginWork* k);
void neighbour_begin(Wram* w, PortCpu* c, BeginWork* k);

#endif
