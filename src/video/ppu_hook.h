// The console's video chip, kept and drawn by `src/video`, with the emulated
// PPU beside it to be checked against.
//
// `video_hook_attach` puts a `VideoHook` where a console's PPU was
// (`SnesVideo`): the console tells it every write to the chip and asks it
// for every read, tells it the three things that happen to the chip once a
// frame and each line of the picture, and has it save and load the chip's
// share of a state. It keeps the registers (`registers.h`) with memories of
// its own -- VRAM, the palette and OAM -- and what is noted of each frame
// (`frame.h`), finds each line's sprites and draws the line (`video.h`) into
// a picture of its own. The frontend asks `video_registers_of`,
// `video_picture_of` and `video_frame_of`, says what it has to say of the
// picture with `video_set_margins` and the rest, and takes the picture with
// `video_put_pixels`.
//
// Under `VIDEO_NATIVE` that is all of it, and the console's PPU is told
// nothing: no write, no line, nothing the frontend says. It is there for the
// one thing left to it. A line `video_declines`, or whose sprites
// `video_sprites_declines`, is drawn or found by the PPU, which is first
// told everything at once, and what it drew or found is copied here, so that
// whoever reads a line reads it in one place. Such lines are counted by what
// it was about them. Nothing this game shows is one.
//
// Under `VIDEO_CHECK` everything is done to the PPU as well, and first, and
// compared. A line and its sprites are compared column by column; after
// every write, every read and each of the frame's events the registers are
// compared, each by name, and a read's two answers with them; the memories
// are compared whole at each of a frame's three events; the notes after
// every write, line and frame's top; and a state as it would be saved from
// here with the state the PPU saves. The answers the console gets, the
// picture shown and the state saved are the PPU's. It is the test: a movie
// run under it says how many of each differed and where the first was.
//
// Under `VIDEO_EMULATED` everything is done to the PPU as well and nothing
// is compared: the PPU finds the sprites, draws the lines and answers the
// console, and what is kept here is for the frontend. It is there so that
// one switch has all three, and for the checksum.
//
// A hook that is installed and not attached is for a PPU with no console,
// which is the noise test's: the PPU keeps the registers, `Ppu.findSprites`
// and `Ppu.drawLine` are called as the PPU runs a line, and under
// `VIDEO_CHECK` both are done both ways.
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
#include "snes.h"
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
  Ppu* ppu;
  Snes* snes;     // once attached
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
  // The registers, once `video_hook_attach` has been called: how many
  // writes and reads they have had, and under `VIDEO_CHECK` how many of
  // those, or of the frame's events, left them or the value read not the
  // PPU's, with a state saved that is not the PPU's. The first of them: what
  // was done, and which register.
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
  // The notes are kept once `video_hook_attach` has been called: how many
  // lines have been noted, and under `VIDEO_CHECK` how many writes, lines
  // and frames' tops left a note, or the picture, not as the PPU had it.
  // The first of them, and which.
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

// The chip's share of a saved state, saved from or loaded into `r`, its
// memories and the two rows of the last line's sprites, in the layout the
// emulated PPU has always saved it in, so that a state saved by either is
// loaded by either: the registers a field at a time in the PPU's order, then
// the memories, then the rows as far as the console's 256 columns.
//
// Two things in it are nobody's to read after a load. The PPU saves where it
// had got to in a line of mode 7, which nothing here draws: zero is saved
// for it. And a sprite's priority is in its row only where a sprite is.
// `parts`, if given, is told where those are in what was saved, and
// `video_states_differ` leaves them out: what of two saved states, `size`
// bytes each, is not the same, or NULL.
typedef struct {
  int mode7_working;  // eight bytes
  int sprite_rows;    // 256 bytes of pixels, then 256 of priorities
} VideoStateParts;

void video_state_handle(VideoRegisters* r, uint8_t* obj_pixel, uint8_t* obj_priority,
                        StateHandler* sh, VideoStateParts* parts);
const char* video_states_differ(const uint8_t* here, const uint8_t* theirs, int size,
                                const VideoStateParts* parts);

// ...and the same of what is noted of a frame: every line's and the rest.
// `line`'s scrolls and windows are compared if it is a line, 1 to 239. The
// centred layers' margins are no part of either: each searches for its own.
void video_notes_from_ppu(VideoFrame* f, const Ppu* ppu);
const char* video_notes_differ(const VideoFrame* f, const Ppu* ppu, int line);

// Draw this PPU's lines with `renderer` from now on, keeping a checksum of
// the picture if `checksummed`. `hook` must outlive the PPU's use of it. A
// PPU with no console is drawn under `VIDEO_CHECK` or `VIDEO_EMULATED`.
void video_hook_install(VideoHook* hook, Ppu* ppu, VideoRenderer renderer, bool checksummed);

// ...and be `snes`'s video chip in its place, whose PPU `hook` must have
// been installed on: the registers, the memories, the notes and the picture
// are kept here from where the PPU's stand now.
void video_hook_attach(VideoHook* hook, Snes* snes);

// For the frontend, of a console's PPU whose hook is attached: the
// registers, what it has said of the picture, what was noted of the frame,
// and what a line is drawn from, which is the three together.
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

// ...and three it puts into the memories, of a PPU whose hook is attached:
// see `video_put_sprite`, `video_put_vram` and `video_put_colour`. The
// frontend reads the memories through `video_registers_of` and writes them
// only with these, because the PPU has a set of its own that may want
// telling.
void video_set_sprite(Ppu* ppu, int sprite, int x, int y, uint16_t word, bool large);
void video_set_vram(Ppu* ppu, uint16_t at, uint16_t word);
void video_set_colour(Ppu* ppu, int index, uint16_t colour);

// The picture of the frame last drawn, of a PPU whose hook is attached.
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
