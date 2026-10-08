// `video.h`'s renderer in place of the emulated PPU's.
//
// The emulated PPU still holds the registers, VRAM, the palette and OAM,
// because the ROM's code is what writes them, and it still finds each line's
// sprites. What it no longer does is draw: `Ppu.drawLine` is called where it
// would have, and `video_ppu_line` draws the line with `video_line` into the
// PPU's own pixel buffer, so everything that reads the picture reads it where
// it always was.
//
// A line `video_declines` is left to the PPU, and counted by what it was
// about the line.
//
// `VIDEO_CHECK` draws every line both ways and compares them, column by
// column. It is the test: a movie run under it says how many lines differed
// and where the first one was. `VIDEO_EMULATED` leaves the drawing to the
// PPU; it is there so that one switch has all three, and for the checksum.
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

// Draw this PPU's lines with `renderer` from now on, keeping a checksum of
// the picture if `checksummed`. `hook` must outlive the PPU's use of it.
void video_hook_install(VideoHook* hook, Ppu* ppu, VideoRenderer renderer, bool checksummed);

// What was drawn, by whom, and what a check found. Returns false if a check
// found any line different.
bool video_hook_report(const VideoHook* hook, FILE* to);

// `native`, `emulated` or `check`; false for anything else.
bool video_renderer_named(const char* name, VideoRenderer* renderer);

#endif
