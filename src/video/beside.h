// The emulated PPU beside a console's `VideoChip`, to draw in its place, to
// stand in for it, and to be checked against.
//
// `video_beside_attach` puts a `VideoBeside` where a console's PPU was
// (`SnesVideo`), as `video_console_attach` puts a chip there alone
// (`console.h`). Everything the console asks is done to the chip. What is
// done to the console's PPU as well is the renderer's to say.
//
// Under `VIDEO_NATIVE` nothing is: no write, no line, nothing the frontend
// says. The PPU is there for the one thing left to it. A line the chip does
// not draw, or whose sprites it does not find, is drawn or found by the PPU,
// which is first told everything at once, and what it drew or found is
// copied to the chip, so that whoever reads a line reads it in one place.
// Nothing this game shows is such a line.
//
// Under `VIDEO_CHECK` everything is done to the PPU as well, and first, and
// compared. A line and its sprites are compared column by column; after
// every write, every read and each of the frame's events the registers are
// compared, each by name, and a read's two answers with them; the memories
// are compared whole at each of a frame's three events; the notes after
// every write, line and frame's top; and a state as it would be saved from
// the chip with the state the PPU saves. The answers the console gets, the
// picture shown and the state saved are the PPU's. It is the test: a movie
// run under it says how many of each differed and where the first was.
//
// Under `VIDEO_EMULATED` everything is done to the PPU as well and nothing
// is compared: the PPU finds the sprites, draws the lines and answers the
// console, and what the chip keeps is for the frontend. It is there so that
// one switch has all three, and for the checksum.
//
// A `VideoBeside` that is installed and not attached is for a PPU with no
// console, which is the noise test's: whoever fills the PPU says when the
// chip is to take what is in it (`video_beside_take`), and the PPU's lines
// are found and drawn both ways as it runs them.
//
// The checksum, when it is asked for, is over every line as it is drawn, in
// any of the three. A movie's is the same number however its picture was
// drawn, or one of them drew something else.

#ifndef ZAMN_VIDEO_BESIDE_H
#define ZAMN_VIDEO_BESIDE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ppu.h"
#include "snes.h"
#include "video/console.h"

typedef struct {
  VideoConsole console;  // first: see `video_chip_of`
  VideoRenderer renderer;
  Ppu* ppu;
  long left;  // `VIDEO_EMULATED`: lines the PPU drew, which is all of them
  // `VIDEO_CHECK`: lines that came out different, and the first of them.
  long differing;
  struct {
    uint32_t frame;
    int line, column;
    uint32_t native, emulated;
  } first;
  // ...lines whose sprites were not what the PPU found.
  long sprite_differing;
  struct {
    uint32_t frame;
    int line;
  } first_sprites;
  // ...writes, reads and frame's events that left a register or a memory,
  // or the value read, not the PPU's, with a state saved that is not the
  // PPU's. The first of them: what was done, and which register.
  long registers_differing;
  struct {
    uint32_t frame;
    int line;
    const char* what;  // "write", "read" or the event
    uint8_t address, value;
    const char* which;
  } first_register;
  // ...and writes, lines and frames' tops that left a note, or the picture
  // as the frontend said it, not as the PPU had it.
  long notes_differing;
  struct {
    uint32_t frame;
    int line;
    const char* which;
  } first_note;
} VideoBeside;

// Draw this PPU's lines with `renderer` from now on, keeping a checksum of
// the picture if `checksummed`. `beside` must outlive the PPU's use of it.
// A PPU with no console is drawn under `VIDEO_CHECK` or `VIDEO_EMULATED`.
void video_beside_install(VideoBeside* beside, Ppu* ppu, VideoRenderer renderer,
                          bool checksummed);

// Have the chip take what is in the PPU as it stands: the registers, the
// memories, the notes and what it has been told of the picture.
void video_beside_take(VideoBeside* beside);

// ...and be `snes`'s video chip in its PPU's place, which `beside` must have
// been installed on. The chip starts from where the PPU stands.
void video_beside_attach(VideoBeside* beside, Snes* snes);

// What was drawn, by whom, and what a check found. Returns false if a check
// found anything different.
bool video_beside_report(const VideoBeside* beside, FILE* to);

// The registers as the PPU has them, into `r`, whose memories are left as
// they are; and the name of the first that is not as the PPU has it, or NULL.
void video_registers_from_ppu(VideoRegisters* r, const Ppu* ppu);
const char* video_registers_differ(const VideoRegisters* r, const Ppu* ppu);

// ...and the same of what is noted of a frame: every line's and the rest.
// `line`'s scrolls and windows are compared if it is a line, 1 to 239. The
// centred layers' margins are no part of either: each searches for its own.
void video_notes_from_ppu(VideoFrame* f, const Ppu* ppu);
const char* video_notes_differ(const VideoFrame* f, const Ppu* ppu, int line);

#endif
