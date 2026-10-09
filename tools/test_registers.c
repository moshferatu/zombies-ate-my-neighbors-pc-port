// zamn_test_registers [steps] [seed]
//
// `src/video/registers` and `src/video/frame` against the emulated PPU's, on
// what no game would do.
//
// A movie checks the writes the game makes, in the order it makes them. This
// checks the rest: an emulated PPU and a `VideoRegisters`, each with memories
// of its own, have the same things done to them at random -- a write of any
// byte to any of the sixty-four addresses, a read of any of them, the beam
// moved, a frame's start, its overscan check and its end, a line of the
// picture begun, the sprites' two flags raised, now and then a reset. After
// each, every register is compared by name, and a read's two answers with
// them; every so often, and at the end, the three memories are compared
// whole.
//
// A `VideoFrame` notes what the PPU notes of each frame, and after each of
// those things the two sets of notes are compared as well. Now and then the
// picture is widened, or said to be something else, to both.
//
// Every eighth step both are asked five things: whether a column of a
// background is empty, and whether it is filled; where a sprite is drawn;
// whether colour maths is allowed at a column of a line; and how far a
// background is shifted on a line.
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

// The picture said to be something at random, to both: the console a third
// of the time, each layer left to be worked out half the time, and a few of
// the sprites placed somewhere other than with the world.
static void say_picture(Ppu* ppu, VideoPicture* p) {
  static const int MARGIN[] = {35, 43, 71, 96, 192};
  p->extra_left = p->extra_right = 0;
  if (!one_in(3)) {
    p->extra_left = one_in(2) ? MARGIN[below(5)] : below(VIDEO_EXTRA_MAX + 1);
    p->extra_right = one_in(2) ? p->extra_left : below(VIDEO_EXTRA_MAX + 1);
  }
  ppu_setWidescreen(ppu, p->extra_left, p->extra_right);
  for (int l = 0; l < 5; l++) {
    p->wide[l] = (uint8_t)(one_in(2) ? VIDEO_WIDE_AUTO : below(9));
    ppu_setLayerWide(ppu, l, p->wide[l]);
  }
  p->clamp_lo = one_in(2) ? -VIDEO_EXTRA_MAX : below(300) - VIDEO_EXTRA_MAX;
  p->clamp_hi = one_in(2) ? 255 + VIDEO_EXTRA_MAX : 150 + below(300);
  ppu_setWideClamp(ppu, p->clamp_lo, p->clamp_hi);
  for (int s = 0; s < VIDEO_SPRITES; s++) {
    p->place[s] = ppu->spritePlace[s] = (uint8_t)(one_in(4) ? below(3) : 0);
    p->shift[s] = ppu->spriteShift[s] = (int16_t)(one_in(16) ? below(64) - 32 : 0);
  }
}

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
  // A line is begun and its sprites found, and nothing is drawn.
  ppu->noPixels = true;
  static Memories memories;
  static VideoRegisters registers;
  VideoRegisters* r = &registers;
  r->vram = memories.vram;
  r->cgram = memories.cgram;
  r->oam = memories.oam;
  r->high_oam = memories.high_oam;
  video_registers_reset(r);
  static VideoFrame frame;
  static VideoPicture picture;
  video_frame_init(&frame);
  video_picture_init(&picture);

  long writes = 0, reads = 0, events = 0, lines = 0, differing = 0;
  long columns = 0, columns_empty = 0, columns_filled = 0;
  long maths_allowed = 0, shifted = 0;
  long first_step = -1;
  char first[160] = "";
  for (long step = 0; step < steps; step++) {
    char did[96];
    bool misread = false;
    int line = -1;
    const int what = below(100);
    if (what < 55) {
      // One write in four is to a scroll or to a window's edge, which are
      // what is noted of a frame's lines.
      const uint8_t address = one_in(4) ? (uint8_t)(one_in(2) ? 0x0d + below(8) : 0x26 + below(4))
                                        : any_address();
      const uint8_t value = (uint8_t)rnd();
      ppu_write(ppu, address, value);
      video_frame_write(&frame, r, address, value, snes->vPos, !snes->inVblank && snes->vPos > 0);
      snprintf(did, sizeof did, "%02X was written to $21%02X", value, address);
      writes++;
    } else if (what < 62) {
      // The same scroll again and again is what a raster effect looks like:
      // enough changes of one scroll across to be read as one, some frames.
      const uint8_t address = (uint8_t)(0x0d + 2 * below(4));
      for (int i = below(40); i > 0; i--) {
        const uint8_t value = (uint8_t)rnd();
        ppu_write(ppu, address, value);
        video_frame_write(&frame, r, address, value, snes->vPos, !snes->inVblank && snes->vPos > 0);
        writes++;
      }
      snprintf(did, sizeof did, "$21%02X was written over and over", address);
    } else if (what < 70) {
      line = 1 + below(239);
      ppu_runLine(ppu, line);
      video_frame_line(&frame, r, line);
      // Finding the line's sprites is the PPU's here, and so are its flags.
      r->range_over = ppu->rangeOver;
      r->time_over = ppu->timeOver;
      snprintf(did, sizeof did, "line %d began", line);
      lines++;
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
      video_frame_start(&frame, r, &picture);
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
      video_picture_reset(&picture);
      snprintf(did, sizeof did, "the reset");
      events++;
    } else if (one_in(4)) {
      say_picture(ppu, &picture);
      continue;
    } else {
      continue;
    }
    // A memory is found different some steps after it was made so.
    const char* which = video_registers_differ(r, ppu);
    const char* when = "after";
    if (which == NULL && misread) which = "the answer";
    if (which == NULL) which = video_notes_differ(&frame, ppu, line);
    if (which == NULL && step % 64 == 0) {
      which = memory_differs(&memories, ppu);
      if (which != NULL) when = "by the time";
    }
    if (which == NULL && step % 8 == 0) {
      const int layer = below(4), x = below(256);
      const bool empty = video_column_empty(r, layer, x);
      const bool filled = video_column_filled(r, layer, x);
      columns++;
      columns_empty += empty;
      columns_filled += filled;
      if (empty != ppu_columnEmptyAt(ppu, layer, x)) which = "whether a column is empty";
      else if (filled != ppu_columnFilledAt(ppu, layer, x)) which = "whether a column is filled";
    }
    if (which == NULL && step % 8 == 0) {
      // Of the frame as it was noted, and the picture as it was said. A
      // column may be off either edge of the picture, and a line above it.
      VideoState s;
      video_registers_state(r, &s);
      video_frame_state(&frame, &picture, &s);
      const int sprite = below(VIDEO_SPRITES), layer = below(4);
      const int x = below(video_width(&s) + 32) - s.extra_left - 16, at = below(240) - 15;
      const bool allowed = video_math_allowed(&s, x, at);
      const int shift_line = 1 + below(224);
      const int shift = video_sweep_shift(&s, layer, shift_line);
      maths_allowed += allowed;
      shifted += shift != 0;
      if (video_sprite_x(&s, sprite) != ppu_spriteXOf(ppu, sprite))
        which = "where a sprite is drawn";
      else if (allowed != ppu_mathAllowedAt(ppu, x, at)) which = "whether maths is allowed";
      else if (shift != ppu_layerShiftX(ppu, layer, shift_line))
        which = "how far a background is shifted";
    }
    if (which == NULL) continue;
    if (differing++ == 0) {
      first_step = step;
      snprintf(first, sizeof first, "%s %s %s", which, when, did);
    }
    // Made the same again, so that what is counted is the things done wrong.
    video_registers_from_ppu(r, ppu);
    video_notes_from_ppu(&frame, ppu);
    memcpy(memories.vram, ppu->vram, sizeof memories.vram);
    memcpy(memories.cgram, ppu->cgram, sizeof memories.cgram);
    memcpy(memories.oam, ppu->oam, sizeof memories.oam);
    memcpy(memories.high_oam, ppu->highOam, sizeof memories.high_oam);
  }
  const char* memory = memory_differs(&memories, ppu);
  if (memory != NULL && differing++ == 0) snprintf(first, sizeof first, "%s at the end", memory);

  printf("%ld steps at random, seed %llu: %ld writes, %ld reads, %ld of a frame's events.\n", steps,
         (unsigned long long)seed, writes, reads, events);
  printf("  %ld lines begun and noted.\n", lines);
  printf("  %ld columns asked about: %ld empty, %ld filled.\n", columns, columns_empty,
         columns_filled);
  printf("  As many sprites, columns of a line and backgrounds asked about:"
         " maths allowed at %ld, %ld shifted.\n",
         maths_allowed, shifted);
  printf("  %ld of them were not as the PPU had them.\n", differing);
  if (differing) printf("  The first, at step %ld: %s.\n", first_step, first);
  ppu_free(ppu);
  free(snes);
  return differing ? 1 : 0;
}
