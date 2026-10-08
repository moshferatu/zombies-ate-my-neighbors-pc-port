// `video.h`'s renderer in place of the emulated PPU's, and `registers.h`'s
// registers beside its own.
//
// The emulated PPU still holds VRAM, the palette and OAM, and still has every
// register written to it, because the frontend reads them from it. What it
// no longer does is find a line's sprites or draw it, or answer the game's
// reads. `Ppu.findSprites` and `Ppu.drawLine` are
// called where it would have: `video_ppu_sprites` finds the sprites with
// `video_sprites` into the PPU's own two rows and sets the flags the game
// reads back, and `video_ppu_line` draws the line with `video_line` into the
// PPU's own pixel buffer, so everything that reads either reads it where it
// always was.
//
// A line `video_declines`, or `video_sprites_declines`, is left to the PPU,
// and counted by what it was about the line.
//
// `VIDEO_CHECK` does both both ways and compares them, column by column. It
// is the test: a movie run under it says how many lines differed and where
// the first one was. `VIDEO_EMULATED` leaves it all to the PPU; it is there
// so that one switch has all three, and for the checksum.
//
// `video_hook_keep_registers` has every write, every read and the frame's
// three events done to a `VideoRegisters` as well, whose memories are the
// PPU's own. A line is then drawn from those registers, and the game reads
// what they answer. Under `VIDEO_CHECK` the PPU's answer is the one the game
// gets, and after every one of them the registers are compared with the
// PPU's, each by name.
//
// The checksum, when it is asked for, is over every line as it is drawn, in
// any of the three. A movie's is the same number however its picture was
// drawn, or one of them drew something else.

#ifndef ZAMN_VIDEO_PPU_HOOK_H
#define ZAMN_VIDEO_PPU_HOOK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ppu.h"
#include "video/registers.h"
#include "video/video.h"

typedef enum {
  VIDEO_NATIVE,
  VIDEO_EMULATED,
  VIDEO_CHECK,
} VideoRenderer;

#define VIDEO_HOOK_REASONS 8

typedef struct {
  Video video;
  VideoRenderer renderer;
  long lines;     // drawn by `video_line`
  long declined;  // left to the PPU, and why
  struct {
    const char* why;
    long lines;
  } reason[VIDEO_HOOK_REASONS];
  // `VIDEO_CHECK`: lines that came out different, and the first of them.
  long differing;
  struct {
    uint32_t frame;
    int line, column;
    uint32_t native, emulated;
  } first;
  // The same for the lines' sprites: found by `video_sprites`, left to the
  // PPU, and under `VIDEO_CHECK` not what the PPU found.
  long sprite_lines, sprite_declined, sprite_differing;
  struct {
    uint32_t frame;
    int line;
  } first_sprites;
  // The registers, once `video_hook_keep_registers` has been called: how
  // many writes and reads they have had, and under `VIDEO_CHECK` how many of
  // those, or of the frame's events, left them or the value read not the
  // PPU's. The first of them: what was done, and which register.
  VideoRegisters registers;
  bool registers_kept;
  long writes, reads, registers_differing;
  struct {
    uint32_t frame;
    int line;
    const char* what;  // "write", "read" or the event
    uint8_t address, value;
    const char* which;
  } first_register;
  bool checksummed;
  uint64_t checksum;
  long checksum_lines;
  long left;  // `VIDEO_EMULATED`: lines the PPU drew, which is all of them
} VideoHook;

// What `video.h` draws from, out of the PPU as it stands; and the centred
// layers' margins, which the PPU keeps for both of them. For a caller that
// reads the PPU with `video.h` itself, as `src/layers.h` does.
void video_state_from_ppu(VideoState* s, const Ppu* ppu);
void video_centre_from_ppu(VideoCentre* c, const Ppu* ppu);
void video_centre_to_ppu(const VideoCentre* c, Ppu* ppu);

// The registers as the PPU has them, into `r`, whose memories are left as
// they are; and the name of the first that is not as the PPU has it, or NULL.
void video_registers_from_ppu(VideoRegisters* r, const Ppu* ppu);
const char* video_registers_differ(const VideoRegisters* r, const Ppu* ppu);

// Draw this PPU's lines with `renderer` from now on, keeping a checksum of
// the picture if `checksummed`. `hook` must outlive the PPU's use of it.
void video_hook_install(VideoHook* hook, Ppu* ppu, VideoRenderer renderer, bool checksummed);

// ...and keep its registers in `hook` as well, from where they stand now.
// Nothing under `VIDEO_EMULATED`. The PPU must belong to a console: a write
// is told the beam's line.
void video_hook_keep_registers(VideoHook* hook, Ppu* ppu);

// What was drawn, by whom, and what a check found. Returns false if a check
// found any line different.
bool video_hook_report(const VideoHook* hook, FILE* to);

// `native`, `emulated` or `check`; false for anything else.
bool video_renderer_named(const char* name, VideoRenderer* renderer);

#endif
