// A `VideoChip` as an emulated console's video chip.
//
// `video_console_attach` puts a `VideoConsole` where a console's video chip
// is (`SnesVideo`): the console tells it every write to the chip and asks it
// for every read, tells it the three things that happen to the chip once a
// frame and each line of the picture, and has it save and load the chip's
// share of a state. All of that is done to the chip (`chip.h`), which the
// frontend has from `video_chip_of`, and to nothing else. Nobody is beside
// the chip: a line it does not draw is black.
//
// Nothing here calls the emulated PPU, and a program that makes its console
// with `without_ppu.c` has none in it. `beside.h` is the PPU beside a chip,
// for a program that has one.
//
// Who draws a console's picture is one of three renderers, and which of them
// a program has is settled where it is linked. `video_snes_init` and the two
// after it are written twice: in `with_ppu.c`, which has all three, and in
// `without_ppu.c`, which has `VIDEO_NATIVE` alone.

#ifndef ZAMN_VIDEO_CONSOLE_H
#define ZAMN_VIDEO_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "snes.h"
#include "video/chip.h"

typedef struct {
  VideoChip chip;
  Snes* snes;  // once attached
} VideoConsole;

// A console's chip with nothing in it, keeping a checksum of its picture if
// `checksummed`; and that chip as `snes`'s video chip. `console` must outlive
// the console's use of it.
void video_console_init(VideoConsole* console, bool checksummed);
void video_console_attach(VideoConsole* console, Snes* snes);

// The chip of a console that has one attached.
VideoChip* video_chip_of(const Snes* snes);

// A write the console's game makes to the chip, and a read: the chip is told
// where the beam is, which the console knows. For somebody who does each
// beside another chip.
void video_console_write(VideoConsole* console, uint8_t address, uint8_t value);
uint8_t video_console_read(VideoConsole* console, uint8_t address);

// What was drawn. Returns false if any line was not, or its sprites not
// found: with nobody beside the chip that line was shown black.
bool video_console_report(const VideoConsole* console, FILE* to);

// --- Who draws the picture ---

typedef enum {
  VIDEO_NATIVE,    // the chip, and the emulated PPU told nothing
  VIDEO_EMULATED,  // the emulated PPU
  VIDEO_CHECK,     // both, and compared: see `beside.h`
} VideoRenderer;

// `native`, `emulated` or `check`; false for anything else.
bool video_renderer_named(const char* name, VideoRenderer* renderer);

// A console whose picture `renderer` draws, with a chip for `video_chip_of`
// that keeps a checksum of the picture if `checksummed`. NULL if the program
// was linked without that renderer.
Snes* video_snes_init(VideoRenderer renderer, bool checksummed);

// What was drawn, by whom, and what a check found. Returns false if a check
// found anything different, or a line was shown black for want of anybody to
// draw it.
bool video_snes_report(const Snes* snes, FILE* to);

void video_snes_free(Snes* snes);

#endif
