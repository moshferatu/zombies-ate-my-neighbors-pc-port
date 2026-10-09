// The video chip's registers, kept here and not in an emulated PPU.
//
// The game talks to the chip through sixty-four addresses, $2100 to $213F.
// It writes the first fifty-two: what is on the screen, where each
// background's map and characters are, the scrolls, the windows, the colour
// maths, and three doors into the chip's memories -- VRAM, the palette and
// OAM -- each an address that is set and then steps as bytes go through. It
// reads the last twelve: those memories back, a multiply, where the beam was
// when it was last latched, and two bytes of status.
//
// `video_registers_write` and `video_registers_read` are those addresses.
// Three more things happen to the chip without the game asking, once a frame
// each, and whoever runs the frame says when: `video_registers_frame_start`,
// `video_registers_overscan` and `video_registers_vblank`.
//
// What the registers say is kept decoded, as `video.h` wants it:
// `video_registers_state` fills a `VideoState`'s share of it.
//
// They are read by name as well, by whoever decides what goes beside the
// console's picture when it is widened; `video_column_empty` and
// `video_column_filled` are two things that asks which take the memory too.
//
// libc only.

#ifndef ZAMN_VIDEO_REGISTERS_H
#define ZAMN_VIDEO_REGISTERS_H

#include <stdbool.h>
#include <stdint.h>

#include "video/video.h"

// The chip's three memories.
typedef struct {
  uint16_t vram[0x8000];
  uint16_t cgram[0x100];   // 256 colours
  uint16_t oam[0x100];     // two words a sprite, 128 sprites
  uint8_t high_oam[0x20];  // two more bits a sprite, four sprites a byte
} VideoMemory;

typedef struct {
  // The chip's memories. Whoever sets this up says where they are, with
  // `video_registers_attach` or by hand.
  uint16_t* vram;     // 0x8000 words
  uint16_t* cgram;    // 256 colours
  uint16_t* oam;      // two words a sprite, 128 sprites
  uint8_t* high_oam;  // two more bits a sprite

  // $2100
  bool blank;
  uint8_t brightness;

  // $2101, and $2103's top bit: the sprite at OAM's address is looked at
  // first, where it is otherwise sprite 0.
  uint8_t obj_sizes;
  uint16_t obj_tiles_at[2];
  bool obj_from_address;

  // OAM's door, $2102 to $2104 and $2138. The address is in words, and goes
  // back to the one last written when the picture ends. A word goes in when
  // its second byte is written; the table of extra bits goes in by bytes.
  uint8_t oam_at, oam_at_written;
  bool oam_high, oam_high_written;
  bool oam_second;
  uint8_t oam_first_byte;

  // $2105 to $2114. A scroll is written a byte at a time, low then high,
  // and the chip has one latch for all eight of them.
  uint8_t mode;
  bool bg3_front;
  VideoBg bg[4];
  uint8_t mosaic_size;  // 1 to 16
  uint8_t mosaic_from;  // the line the mosaic's blocks are counted from
  uint8_t scroll_latch, hscroll_latch;

  // VRAM's door, $2115 to $2119, $2139 and $213A. Reading goes through a
  // word that is fetched ahead.
  uint16_t vram_at, vram_step;
  uint8_t vram_remap;      // which of four ways the address's low bits are turned
  bool vram_step_on_high;  // step after the high byte, and not the low
  uint16_t vram_ahead;

  // Mode 7, $211A to $2120: the matrix a, b, c, d, the centre x, y, and the
  // scroll h, v. Nothing here draws mode 7; a times b's high byte is the
  // multiply the game reads from $2134.
  int16_t m7[8];
  uint8_t m7_latch;
  bool m7_large_field, m7_fill, m7_flip_x, m7_flip_y, m7_ext_bg;

  // The palette's door, $2121, $2122 and $213B.
  uint8_t cgram_at;
  bool cgram_second;
  uint8_t cgram_first_byte;

  // $2123 to $212F: a window for each of the four backgrounds, the sprites
  // and the colour maths, the two windows' edges, and what is on each screen.
  VideoWindow window[VIDEO_LAYERS];
  uint8_t window1_left, window1_right, window2_left, window2_right;
  bool main[5], sub[5];
  bool main_windowed[5], sub_windowed[5];

  // $2130 to $2132
  bool direct_colour;
  bool add_sub, subtract, half;
  bool math[VIDEO_LAYERS];
  uint8_t prevent, clip;
  uint8_t fixed_r, fixed_g, fixed_b;

  // $2133
  bool interlace, obj_interlace, overscan, pseudo_hires;

  // The frame: which of the two fields it is, what $2133 said when it was
  // asked, and whether a line has had more sprites than there was time for.
  bool even_frame;
  bool frame_overscan, frame_interlace;
  bool range_over, time_over;

  // The beam's place when it was last latched, $2137, read a byte at a time
  // from $213C and $213D.
  uint16_t h_count, v_count;
  bool h_second, v_second;
  bool latched;

  // What a read leaves on each half of the chip's bus, which the bits a
  // later read does not drive come back as.
  uint8_t bus1, bus2;
} VideoRegisters;

// What a read needs of the rest of the console.
typedef struct {
  int dot, line;     // where the beam is
  bool pal;
  uint8_t open_bus;  // what the processor's bus was left at
} VideoBus;

// Have `r` keep its memories in `m`.
void video_registers_attach(VideoRegisters* r, VideoMemory* m);

// The chip as the console's reset leaves it, with its memories cleared.
// `vram` and the other three must be set first, and are left as they are.
void video_registers_reset(VideoRegisters* r);

// $2100 + `address`, 0 to 0x3f. `line` is the line the beam is on.
void video_registers_write(VideoRegisters* r, uint8_t address, uint8_t value, int line);
uint8_t video_registers_read(VideoRegisters* r, uint8_t address, const VideoBus* bus);

// The top of a frame, line 0.
void video_registers_frame_start(VideoRegisters* r);
// Line 225, where the chip settles how tall this frame is: true if it has
// the 239 lines of overscan, and so goes on to line 240.
bool video_registers_overscan(VideoRegisters* r);
// The end of the picture: line 225, or 240 with overscan.
void video_registers_vblank(VideoRegisters* r);

// The registers' share of what a line is drawn from. The rest of `s` -- the
// line's sprites, the widened picture, what the frontend says of each sprite
// -- is left as it was.
void video_registers_state(const VideoRegisters* r, VideoState* s);

// Four things put into the memories from outside, and not by the game
// through a door: nothing of the doors' addresses moves.
//
// A sprite whole: `x` is nine bits, `y` eight, `word` its second word as the
// game composes it. For the sprites a game drops because they are outside
// the console's 256 columns and inside a widened picture.
void video_put_sprite(VideoRegisters* r, int sprite, int x, int y, uint16_t word, bool large);
// The first sprite at or after `from` that is parked below the picture, or
// `VIDEO_SPRITES` if there is none: where one of those can go without
// disturbing a sprite the game placed.
int video_free_sprite(const VideoRegisters* r, int from);
// One word of VRAM. For the parts of a scrolling map that the game keeps up
// only as far as its own 256 columns.
void video_put_vram(VideoRegisters* r, uint16_t at, uint16_t word);
// One colour of the palette, fifteen bits. For a colour the game has none
// of, in a place it leaves unused.
void video_put_colour(VideoRegisters* r, int index, uint16_t colour);

// Whether background `layer` draws nothing at all down the column of tiles
// under column `x` of the console's picture, as it is scrolled now: every
// tile there, on each of the picture's 224 lines, a character of nothing but
// zeroes. It is about pixels and not map words. Two blank tiles in two
// palettes are two words and the same nothing, and the game uses both.
// False for a background the mode does not have.
bool video_column_empty(const VideoRegisters* r, int layer, int x);
// ...and whether every tile down that column has something in it. Of a tile
// sixteen pixels square, only the first of its four characters is looked at.
bool video_column_filled(const VideoRegisters* r, int layer, int x);

#endif
