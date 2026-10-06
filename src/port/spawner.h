// Something started from a list, at its place or near it.
//
// `$81:807E` starts a thread from an entry of a list of ten bytes each:
// a byte, where across and down, how far it may be scattered, and the
// thread and its bank. `$0C` on the caller's page is the list and `$12`
// which entry.
//
// With nothing to scatter by, the thread is started at the place. With a
// spread, two random bytes move the place by up to half of it either way,
// and the thread is started only if the ground there is clear and inside
// the level. The spread is a mask: a power of two less one.
//
// The thread is given the place, and two words of nothing, on the first
// eight bytes of the caller's page, which `thread_spawn` copies.
//
// Port code: libc only.

#ifndef PORT_SPAWNER_H
#define PORT_SPAWNER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/rng.h"
#include "port/terrain.h"
#include "port/wram.h"

#define SPAWN_ENTRY_PC 0x81807eu
#define SPAWN_ENTRY_RTS_PC 0x8180ebu

#define SPAWN_ENTRY_BYTES 10
#define SPAWN_ENTRY_X 1
#define SPAWN_ENTRY_Y 3
#define SPAWN_ENTRY_SPREAD 5
#define SPAWN_ENTRY_THREAD 6
#define SPAWN_ENTRY_BANK 8
#define SPAWN_DP_X 0x00
#define SPAWN_DP_Y 0x02
#define SPAWN_DP_ARG_2 0x04
#define SPAWN_DP_ARG_3 0x06
#define SPAWN_DP_TWICE 0x0a
#define SPAWN_DP_LIST 0x0c
#define SPAWN_DP_ENTRY 0x0e
#define SPAWN_DP_INDEX 0x12
#define SPAWN_DP_SPREAD 0x1a
#define SPAWN_DP_HALF 0x1c

typedef enum {
  SPAWN_AT_PLACE,
  SPAWN_SCATTERED,
  SPAWN_GROUND_IN_THE_WAY,
  SPAWN_OFF_THE_LEVEL,
} SpawnOutcome;

// For the harness: what happened, and what each call under it did.
typedef struct {
  SpawnOutcome outcome;
  bool drew_overflow[2];
  TerrainRegs ground;
  BoundsRegs bounds;
  int slot;  // the thread's, doubled, or -1 for none free
} SpawnWork;

// False unless the entry is somewhere the data bank shows whole.
bool spawn_entry_supported(const Wram* w, const PortCpu* c);
void spawn_entry(Wram* w, const Rom* rom, PortCpu* c, SpawnWork* k);

#endif
