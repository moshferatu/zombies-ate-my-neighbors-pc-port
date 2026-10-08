// zamn_test_registers [steps] [seed]
//
// `src/video/registers` against the emulated PPU's, on what no game would do.
//
// A movie checks the writes the game makes, in the order it makes them. This
// checks the rest: an emulated PPU and a `VideoRegisters`, each with memories
// of its own, have the same things done to them at random -- a write of any
// byte to any of the sixty-four addresses, a read of any of them, the beam
// moved, a frame's start, its overscan check and its end, the sprites' two
// flags raised, now and then a reset. After each, every register is compared
// by name, and a read's two answers with them; every so often, and at the
// end, the three memories are compared whole.
//
// The doors into the memories are where most of the state is, so a third of
// the writes and reads go to them and to their addresses.
//
// No ROM and no window. Exits 1 if anything differed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "snes.h"
#include "video/ppu_hook.h"

static uint64_t rng_state;

static uint32_t rnd(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return (uint32_t)(rng_state >> 16);
}

static int below(int n) { return (int)(rnd() % (uint32_t)n); }
static bool one_in(int n) { return below(n) == 0; }

// OAM's, VRAM's and the palette's doors, with what sets their addresses.
static const uint8_t DOORS[] = {0x02, 0x03, 0x04, 0x04, 0x04, 0x38, 0x38, 0x15, 0x16, 0x17, 0x18,
                                0x18, 0x19, 0x19, 0x39, 0x3a, 0x21, 0x22, 0x22, 0x3b, 0x3b};

static uint8_t any_address(void) {
  return one_in(3) ? DOORS[below((int)sizeof DOORS)] : (uint8_t)below(0x40);
}

typedef struct {
  uint16_t vram[0x8000];
  uint16_t cgram[0x100];
  uint16_t oam[0x100];
  uint8_t high_oam[0x20];
} Memories;

static const char* memory_differs(const Memories* m, const Ppu* ppu) {
  if (memcmp(m->vram, ppu->vram, sizeof m->vram)) return "VRAM";
  if (memcmp(m->cgram, ppu->cgram, sizeof m->cgram)) return "the palette";
  if (memcmp(m->oam, ppu->oam, sizeof m->oam)) return "OAM";
  if (memcmp(m->high_oam, ppu->highOam, sizeof m->high_oam)) return "OAM's extra bits";
  return NULL;
}

int main(int argc, char** argv) {
  const long steps = argc > 1 ? atol(argv[1]) : 2000000;
  const uint64_t seed = argc > 2 ? strtoull(argv[2], NULL, 0) : 1;
  rng_state = seed * 0x9e3779b97f4a7c15ull | 1;

  // The PPU asks its console three things: where the beam is, whether the
  // picture is being drawn, and what is on the bus. This one is only those.
  Snes* snes = calloc(1, sizeof *snes);
  Ppu* ppu = ppu_init(snes);
  ppu_reset(ppu);
  static Memories memories;
  static VideoRegisters registers;
  VideoRegisters* r = &registers;
  r->vram = memories.vram;
  r->cgram = memories.cgram;
  r->oam = memories.oam;
  r->high_oam = memories.high_oam;
  video_registers_reset(r);

  long writes = 0, reads = 0, events = 0, differing = 0;
  long first_step = -1;
  char first[160] = "";
  for (long step = 0; step < steps; step++) {
    char did[96];
    bool misread = false;
    const int what = below(100);
    if (what < 60) {
      const uint8_t address = any_address(), value = (uint8_t)rnd();
      ppu_write(ppu, address, value);
      video_registers_write(r, address, value, snes->vPos);
      snprintf(did, sizeof did, "%02X was written to $21%02X", value, address);
      writes++;
    } else if (what < 85) {
      const uint8_t address = any_address();
      const VideoBus bus = {snes->hPos / 4, snes->vPos, snes->palTiming, snes->openBus};
      const uint8_t theirs = ppu_read(ppu, address);
      const uint8_t here = video_registers_read(r, address, &bus);
      misread = here != theirs;
      snprintf(did, sizeof did, "$21%02X was read, as %02X here and %02X by the PPU", address,
               here, theirs);
      reads++;
    } else if (what < 93) {
      snes->hPos = (uint16_t)below(1364);
      snes->vPos = (uint16_t)below(262);
      snes->inVblank = one_in(2);
      snes->openBus = (uint8_t)rnd();
      snes->palTiming = one_in(8);
      continue;
    } else if (what < 95) {
      ppu_handleFrameStart(ppu);
      video_registers_frame_start(r);
      snprintf(did, sizeof did, "the frame's start");
      events++;
    } else if (what < 97) {
      const bool theirs = ppu_checkOverscan(ppu);
      misread = video_registers_overscan(r) != theirs;
      snprintf(did, sizeof did, "the overscan's check");
      events++;
    } else if (what < 99) {
      ppu_handleVblank(ppu);
      video_registers_vblank(r);
      snprintf(did, sizeof did, "the picture's end");
      events++;
    } else if (one_in(2)) {
      // A line with more sprites than there is time for.
      ppu->rangeOver = r->range_over = one_in(2);
      ppu->timeOver = r->time_over = one_in(2);
      continue;
    } else if (one_in(200)) {
      ppu_reset(ppu);
      video_registers_reset(r);
      snprintf(did, sizeof did, "the reset");
      events++;
    } else {
      continue;
    }
    // A memory is found different some steps after it was made so.
    const char* which = video_registers_differ(r, ppu);
    const char* when = "after";
    if (which == NULL && misread) which = "the answer";
    if (which == NULL && step % 64 == 0) {
      which = memory_differs(&memories, ppu);
      when = "by the time";
    }
    if (which == NULL) continue;
    if (differing++ == 0) {
      first_step = step;
      snprintf(first, sizeof first, "%s %s %s", which, when, did);
    }
    // Made the same again, so that what is counted is the things done wrong.
    video_registers_from_ppu(r, ppu);
    memcpy(memories.vram, ppu->vram, sizeof memories.vram);
    memcpy(memories.cgram, ppu->cgram, sizeof memories.cgram);
    memcpy(memories.oam, ppu->oam, sizeof memories.oam);
    memcpy(memories.high_oam, ppu->highOam, sizeof memories.high_oam);
  }
  const char* memory = memory_differs(&memories, ppu);
  if (memory != NULL && differing++ == 0) snprintf(first, sizeof first, "%s at the end", memory);

  printf("%ld steps at random, seed %llu: %ld writes, %ld reads, %ld of a frame's events.\n", steps,
         (unsigned long long)seed, writes, reads, events);
  printf("  %ld of them were not as the PPU had them.\n", differing);
  if (differing) printf("  The first, at step %ld: %s.\n", first_step, first);
  ppu_free(ppu);
  free(snes);
  return differing ? 1 : 0;
}
