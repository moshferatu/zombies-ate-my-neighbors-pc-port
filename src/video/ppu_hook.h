// `video.h`'s renderer in place of the emulated PPU's, and `registers.h`'s
// registers and `frame.h`'s notes beside its own.
//
// The emulated PPU still has every register written to it and is still told
// what the frontend says of the picture, because under two of the three
// renderers it draws lines, and they are what `src/video` is checked against.
// What it no longer does is find a line's sprites or draw it, answer the
// game's reads, or answer the frontend, which asks `video_registers_of`,
// `video_picture_of` and `video_frame_of` and takes the picture with
// `video_put_pixels`. `Ppu.findSprites` and `Ppu.drawLine` are called where
// it would have: `video_ppu_sprites` finds the sprites with `video_sprites`
// and sets the flags the game reads back, and `video_ppu_line` draws the line
// with `video_line`. The sprites' two rows and the picture's buffer they go
// into are kept here, and the PPU's own are not written.
//
// A line `video_declines`, or `video_sprites_declines`, is left to the PPU,
// and counted by what it was about the line. What the PPU found or drew is
// then copied here, so that whoever reads a line reads it in one place.
//
// `VIDEO_CHECK` does both both ways and compares them, column by column. It
// is the test: a movie run under it says how many lines differed and where
// the first one was. The picture it shows is the PPU's. `VIDEO_EMULATED`
// leaves it all to the PPU; it is there so that one switch has all three,
// and for the checksum.
//
// `video_hook_keep_registers` has every write, every read and the frame's
// three events done to a `VideoRegisters` as well, with memories of its own:
// VRAM, the palette and OAM are kept here, and the PPU keeps a second set. A
// line is then drawn from those registers and memories, and the game reads
// what they answer. Under `VIDEO_CHECK` the PPU's answer is the one the game
// gets, and after every one of them the registers are compared with the
// PPU's, each by name; the memories are compared whole at each of a frame's
// three events. Under `VIDEO_EMULATED` they are kept for the frontend alone.
//
// What is noted of a frame is kept with them, in a `VideoFrame`: each line's
// scrolls and windows as it begins, the writes made while the picture is
// drawn, and what a layer's policy is worked out from at a frame's top. Under
// `VIDEO_CHECK` those are compared with the PPU's own notes after every
// write, line and frame's top.
//
// What the frontend says of the picture it says with `video_set_margins` and
// the rest, which keep it in a `VideoPicture` and tell the PPU.
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
#include "video/frame.h"
#include "video/registers.h"
#include "video/video.h"

typedef enum {
  VIDEO_NATIVE,
  VIDEO_EMULATED,
  VIDEO_CHECK,
} VideoRenderer;

// How a pixel of the picture is handed over, as a 32-bit word: its bytes in
// memory are blue, green, red and one unused, or the unused one first.
typedef enum {
  VIDEO_PIXELS_XRGB,
  VIDEO_PIXELS_RGBX,
} VideoPixels;

// The picture as it is kept: two fields of 239 lines, of which a frame that
// is not interlaced draws one. A column is eight bytes, the pixel twice, so
// a line is handed over twice as wide as the game drew it.
#define VIDEO_ROW_BYTES (VIDEO_MAX_WIDTH * 8)
#define VIDEO_FIELD_ROWS (VIDEO_LINES - 1)

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
  VideoMemory memory;
  bool registers_kept;
  long writes, reads, registers_differing;
  struct {
    uint32_t frame;
    int line;
    const char* what;  // "write", "read" or the event
    uint8_t address, value;
    const char* which;
  } first_register;
  // What the frontend says of the picture, and what is noted of each frame.
  // The notes are kept once `video_hook_keep_registers` has been called:
  // how many lines have been noted, and under `VIDEO_CHECK` how many writes,
  // lines and frames' tops left a note, or the picture, not as the PPU had
  // it. The first of them, and which.
  VideoPicture picture;
  VideoFrame frame;
  long noted_lines, notes_differing;
  struct {
    uint32_t frame;
    int line;
    const char* which;
  } first_note;
  // The sprites on the line being drawn, whoever found them: a palette
  // index for each column of the picture, or 0, and that sprite's priority.
  uint8_t obj_pixel[VIDEO_MAX_WIDTH], obj_priority[VIDEO_MAX_WIDTH];
  // The picture, whoever drew it.
  VideoPixels format;
  uint8_t pixels[2 * VIDEO_FIELD_ROWS * VIDEO_ROW_BYTES];
  bool checksummed;
  uint64_t checksum;
  long checksum_lines;
  long left;  // `VIDEO_EMULATED`: lines the PPU drew, which is all of them
} VideoHook;

// What `video.h` draws from, all of it out of the PPU as it stands: its
// memories, its registers, its notes and what it has been told of the
// picture. For a PPU whose registers are not kept, which is the noise test's.
void video_state_from_ppu(VideoState* s, const Ppu* ppu);

// The registers as the PPU has them, into `r`, whose memories are left as
// they are; and the name of the first that is not as the PPU has it, or NULL.
void video_registers_from_ppu(VideoRegisters* r, const Ppu* ppu);
const char* video_registers_differ(const VideoRegisters* r, const Ppu* ppu);

// ...and the same of what is noted of a frame: every line's and the rest.
// `line`'s scrolls and windows are compared if it is a line, 1 to 239. The
// centred layers' margins are no part of either: each searches for its own.
void video_notes_from_ppu(VideoFrame* f, const Ppu* ppu);
const char* video_notes_differ(const VideoFrame* f, const Ppu* ppu, int line);

// Draw this PPU's lines with `renderer` from now on, keeping a checksum of
// the picture if `checksummed`. `hook` must outlive the PPU's use of it.
void video_hook_install(VideoHook* hook, Ppu* ppu, VideoRenderer renderer, bool checksummed);

// ...and keep its registers in `hook` as well, with the memories and what is
// noted of a frame, from where they stand now. The PPU must belong to a
// console: a write is told the beam's line.
void video_hook_keep_registers(VideoHook* hook, Ppu* ppu);

// For the frontend, of a PPU whose registers are kept: the registers, what
// it has said of the picture, what was noted of the frame, and what a line
// is drawn from, which is the three together.
const VideoRegisters* video_registers_of(const Ppu* ppu);
const VideoPicture* video_picture_of(const Ppu* ppu);
const VideoFrame* video_frame_of(const Ppu* ppu);
void video_state_of(VideoState* s, const Ppu* ppu);

// What the frontend says of the picture, of a PPU with a hook: see
// `VideoPicture`. Each takes effect on the next line drawn, so they are said
// between frames. A margin is held to `VIDEO_EXTRA_MAX`; a layer is 0 to 3
// for a background and 4 for the sprites.
void video_set_margins(Ppu* ppu, int left, int right);
void video_set_wide(Ppu* ppu, int layer, VideoWide policy);
void video_set_clamp(Ppu* ppu, int lo, int hi);
void video_set_sprite_place(Ppu* ppu, int sprite, VideoSpritePlace place);
void video_set_sprite_shift(Ppu* ppu, int sprite, int shift);
void video_set_sprite_front(Ppu* ppu, int sprite, bool front);
void video_set_sprite_remapped(Ppu* ppu, int sprite, bool remapped);
void video_set_remap(Ppu* ppu, const uint8_t remap[16]);

// ...and one thing it puts into a register: a background's scroll, ten bits
// each way, for a layer it draws where the game did not. That is the big
// figure's plane, which the game parks out of sight of a picture narrower
// than the one being drawn. Not a write of the game's, and not noted as one.
void video_set_scroll(Ppu* ppu, int layer, int h, int v);

// ...and three it puts into the memories, of a PPU whose registers are kept:
// see `video_put_sprite`, `video_put_vram` and `video_put_colour`. The
// frontend reads the memories through `video_registers_of` and writes them
// only with these, because the PPU has a set of its own to be told.
void video_set_sprite(Ppu* ppu, int sprite, int x, int y, uint16_t word, bool large);
void video_set_vram(Ppu* ppu, uint16_t at, uint16_t word);
void video_set_colour(Ppu* ppu, int index, uint16_t colour);

// The picture of the frame last drawn, of a PPU whose registers are kept.
// `video_output_width` pixels across, which is two for each of the picture's
// columns, and 480 rows, each of the frame's lines twice: 224 lines leave
// sixteen rows of black above and below them, and 239 leave two above. Four
// bytes a pixel, as `video_set_pixel_format` last said, and no gap between
// rows.
void video_set_pixel_format(Ppu* ppu, VideoPixels format);
int video_output_width(const Ppu* ppu);
void video_put_pixels(const Ppu* ppu, uint8_t* pixels);

// What was drawn, by whom, and what a check found. Returns false if a check
// found anything different.
bool video_hook_report(const VideoHook* hook, FILE* to);

// `native`, `emulated` or `check`; false for anything else.
bool video_renderer_named(const char* name, VideoRenderer* renderer);

#endif
