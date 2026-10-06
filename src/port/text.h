// The text printer: strings of two-by-two characters, into the text map.
//
//   $82:B84A  text_print        one string at one place
//   $82:B8FB  text_print_lines  strings each with its own place, and then on
//                               to send the text map to VRAM
//   $82:AD5A  the big letters   a level's name, in two stretches: see below
//
// Every screen of words goes through one of these: the title's, the menus',
// a level's name, the counts when one is over.
//
// ## A place
//
// A place is two words. The first is a column in its low byte and a row in
// its high byte, in tiles. The second is what the characters are drawn with.
// Its low byte, times four, is put at `$1E8D`, the high byte of a word that
// is added to each tile's number, which is where a tile's palette is. Its
// high byte is kept at `$1E8E` for the job that sends the map.
//
// `text_print` adds its caller's A, high byte only, to the first word, which
// moves the string down by that many rows. `text_print_lines` adds its
// caller's Y to it instead, a column and a row.
//
// ## A string
//
// A string is bytes, ended by a zero. A character is two tiles across and
// two down. Its tile numbers are in the table at `$9E:FDF4`: two words for
// its top row and, `$100` bytes on, two for its bottom, four bytes to a
// character, indexed by the byte less `$2F`. A byte under `$2F` draws nothing
// and moves nothing.
//
// In `text_print_lines` a byte of `$FF` is not a character: a new place
// follows it, and the string goes on from there.
//
// ## Where they come from
//
// `text_print` takes its place from the bank in A's low byte, at X, and its
// string from the bank at `$4E`, at Y. `text_print_lines` takes both from
// the bank in A's low byte, at X, the first place and then the string.
//
// ## Their contracts with the ROM
//
// WRAM is written as the ROM writes it: the map, the pointers on the caller's
// direct page at `$E0` to `$E5`, the two bytes at `$1E8D`, and the row
// counter, which is `$4C` for the one and `$1F54` for the other.
//
// `text_print` returns. It leaves zero in A, the last character's index
// registers in X and Y, and N and Z from the data bank it restores. Carry and
// overflow are not followed.
//
// `text_print_lines` does not return here: it runs to `$82:B9A6`, where the
// ROM queues the job that sends the map and waits for it. So it leaves as
// that instruction finds things, with the data bank `$82` and the caller's
// under a spare byte on the stack.
//
// Port code: libc only.

#ifndef PORT_TEXT_H
#define PORT_TEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define TEXT_PRINT_PC 0x82b84au
#define TEXT_PRINT_RTL_PC 0x82b8fau
#define TEXT_PRINT_LINES_PC 0x82b8fbu
#define TEXT_PRINT_LINES_SEND_PC 0x82b9a6u
#define TEXT_BANK 0x82u  // the data bank both run under

// On the caller's direct page: a far pointer to what is being read, and one
// to where in the map the next character goes.
#define TEXT_DP_FROM 0xe0
#define TEXT_DP_TO 0xe3
// The map, a row of 32 words to a line, in bank `$7E`.
#define TEXT_MAP_AT 0x6502u
#define TEXT_MAP_BANK 0x7eu
// What a place's second word leaves: a word whose high byte is added to
// every tile, and a byte for the job.
#define W_TEXT_TILE_BASE 0x1e8cu
#define W_TEXT_JOB_BYTE 0x1e8eu
// The bank `text_print` reads its string from, and the offset
// `text_print_lines` adds to each place.
#define W_TEXT_STRING_BANK 0x004eu
#define W_TEXT_LINES_OFFSET 0x1e9au
// The two routines' row counters.
#define W_TEXT_ROWS 0x004cu
#define W_TEXT_LINES_ROWS 0x1f54u

// A character's tiles.
#define TEXT_FONT 0x9efdf4u
#define TEXT_FONT_ROW 0x0100u   // from a top row's four words to the bottom's
#define TEXT_FIRST_CHAR 0x2f
#define TEXT_NEW_PLACE 0xff
#define TEXT_TILE_FLAG 0x0200u  // set on every tile it writes

// For the harness: what the string held, which is what the ROM's instructions
// were.
typedef struct {
  int bytes;       // read from the string, its zero included
  int glyphs;      // ...of which this many were drawn
  int places;      // places read
  int slow_words;  // words read from somewhere that is not fast cartridge
} TextWork;

typedef struct {
  uint16_t a, x, y;
} TextRegs;

// Is a place or a string at `bank:at` somewhere these read at a known speed:
// the cartridge, or WRAM? A number's digits are a string in low WRAM.
bool text_source_ok(uint8_t bank, uint16_t at);

// `page` is the caller's direct page, and `a`, `x` and `y` its registers.
void text_print(Wram* w, const Rom* rom, uint16_t page, uint16_t a, uint16_t x,
                uint16_t y, TextRegs* out, TextWork* k);
void text_print_lines(Wram* w, const Rom* rom, uint16_t page, uint16_t a,
                      uint16_t x, uint16_t y, TextRegs* out, TextWork* k);

// --- $82:AD5A  the big letters -----------------------------------------------
//
// A level's name is written in letters six tiles tall. `$82:AD5A` takes a
// place and a string as `text_print_lines` does, from the bank in A's low
// byte at X, with `$FF` for a new place, and adds nothing to a place.
//
// A character is a word of the table at `$82:AF37`, indexed by the byte less
// `$20`. A negative word draws nothing. Otherwise its high byte names one of
// three sets, each with a width in bytes of map and a place in the font at
// `$96:D641`, and its low byte says which of the set. The font is 256 bytes
// to a row of tiles, so a character's six rows are `$100` apart.
//
// Where in its set's row a character starts is its number times its width,
// and the ROM asks the console's multiplier. That is two writes to the
// hardware in the middle of every character, so the routine is two stretches
// here, each ending where the next write would be:
//
//     $82:AD5A  begin   the first place, and the string up to the first
//                       character that draws
//     $82:ADE4  glyph   from the product: that character's tiles, and the
//                       string up to the next one that draws
//
// The five instructions between them stay the ROM's: `STA $4202 : STX
// $4203` and three `NOP`s. The glyph stretch does not read the product from
// the hardware. A and X still hold what was multiplied.
//
// Either leaves at `$82:AE34` when the string ends, where the ROM queues the
// job that sends the map, with the data bank `$82` and the caller's under a
// spare byte on the stack, as `text_print_lines` does.
#define TEXT_BIG_PC 0x82ad5au
#define TEXT_BIG_MULTIPLY_PC 0x82addbu  // `STA $4202`, 8-bit registers
#define TEXT_BIG_GLYPH_PC 0x82ade4u     // `REP #$30 : LDA $4216`
#define TEXT_BIG_SEND_PC 0x82ae34u
#define TEXT_BIG_CHARS 0x82af37u
#define TEXT_BIG_FIRST_CHAR 0x20
// A set's place in the font and its width, a word each, by twice the set.
#define TEXT_BIG_SET_AT 0x82ae63u
#define TEXT_BIG_SET_WIDTH 0x82ae69u
#define TEXT_BIG_FONT 0x96d641u
#define TEXT_BIG_FONT_ROW 0x0100u
#define TEXT_BIG_ROWS 6
#define TEXT_BIG_MAP_ROW 0x0040u
#define W_TEXT_BIG_WIDTH 0x1e90u
#define W_TEXT_BIG_AT 0x1e92u

typedef struct {
  TextWork text;  // bytes, places and slow words, as the others count them
  int skipped;    // characters the table draws nothing for
  int words;      // tiles written
} TextBigWork;

typedef struct {
  uint16_t a, x, y;
  bool multiply;  // it stopped for a product; otherwise the string ended
} TextBigRegs;

// One of the three widths the sets have, and X the same.
bool text_big_width_ok(uint16_t width, uint16_t x);

void text_big_begin(Wram* w, const Rom* rom, uint16_t page, uint16_t a,
                    uint16_t x, uint16_t y, TextBigRegs* out, TextBigWork* k);
// `which` and `width` are A and X, the two bytes that were multiplied.
void text_big_glyph(Wram* w, const Rom* rom, uint16_t page, uint8_t which,
                    uint8_t width, TextBigRegs* out, TextBigWork* k);

#endif
