// The big figure's sixteen colours.
//
// A boss too large for sprites is drawn as a background (`port/bossbg.h`),
// and its colours are the background palette's eighth row. This sets that
// row from sixteen colours somewhere else, and asks for it to be sent to the
// screen:
//
//   $82:8138  figure_colours_set   A the colours' address, Y their bank
//
// The row is kept twice, at `$7E:5508` and at `$7E:5708`, as the rest of the
// background's colours are (`port/palfade.h`), and both are written. The job
// it queues, `$82:8163`, queues the one that does the sending.
//
// ## Its contract with the ROM
//
// It writes WRAM exactly as the ROM does. Its registers at the `RTL` are
// what `vbl_queue_b_add` leaves, which is the last thing it calls.
//
// Port code: libc only.

#ifndef PORT_FIGURE_COLOURS_H
#define PORT_FIGURE_COLOURS_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define FIGURE_COLOURS_PC 0x828138u
#define FIGURE_COLOURS_RTL_PC 0x828162u

#define FIGURE_COLOURS_ROW 0x5508u       // the eighth row of the background
#define FIGURE_COLOURS_ROW_COPY 0x5708u  // ...and of its second copy
#define FIGURE_COLOURS_COUNT 16

// The job that has the row sent, `$82:8163`.
#define FIGURE_COLOURS_JOB 0x8163u
#define FIGURE_COLOURS_JOB_BANK 0x0082u

// Can `figure_colours_set` read these? Only from the cartridge.
bool figure_colours_supported(uint16_t at, uint8_t bank);

// Set the row from the sixteen colours at `bank:at`. Returns where the job
// went in the queue, or -1 when the queue was full.
int figure_colours_set(Wram* w, const Rom* rom, uint16_t at, uint8_t bank);

#endif
