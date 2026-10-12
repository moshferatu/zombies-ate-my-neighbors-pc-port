// The screen before a level, between its calls and its waits.
//
// `$82:AC07` shows two strings of big letters before a level, one after the
// other. Each comes down the screen and bounces, and is held for two
// seconds. The level's song starts under the first, and the level's blocks
// are unpacked under the second. It is jumped to, with the first string's
// address in Y, the level's record in A and the second string's in X. Both
// strings are in bank `$9F`, where the record is.
//
// What it calls is ported where each of those is: the big letters in
// `port/text.h`, the coming down in `port/card.h`, the two holds in
// `port/hold.h`, the dimming in `port/dim.h`. This is the rest: the
// instructions that set out what a call is made with, and the ones that put
// something in memory before a wait.
//
//     $82:AC07  begin                its data bank, the three it was given
//                                    pushed, and the brightness shadow's top
//                                    bit set: the screen off
//     $82:AC1F  colours_ask          into `$82:ACF0`: 256 bytes of colours
//     $82:ACFD  tiles_unpack_ask     the letters' pictures, to be unpacked
//     $82:AD0E  tiles_send_ask       ...and sent to VRAM at `$2000`
//     $82:AD1D  map_clear_ask        back out, to clear the letters' map
//     $82:AC48  first_drop_begin     into `$82:AE6F`: sixteen frames to come
//                                    down in
//     $82:AC4F  song_ask             the frame count zeroed, the record
//                                    kept, and its song
//     $82:AC6D  second_words_ask     the second string
//     $82:AC75  second_drop_begin    as the first
//     $82:AC78  blocks_unpack_ask    the frame count zeroed again, and the
//                                    record's blocks, to be unpacked
//     $82:AC91  blocks_unpacked      the argument pulled
//     $82:ACA6  blank                the screen off again
//
// The unpacking and the sends take one argument on the stack, a word pushed
// before the call and pulled after it. A stretch that follows such a call
// begins by pulling the last one's.
//
// `$82:AC27`, which sets the letters' layer up and asks for the first
// string, and `$82:ACB0`, which puts the level's layers back, write to the
// picture hardware. They are in `port/dma.h`. The big letters' own end,
// `$82:AE3F`, is `text_big_sent` in `port/text.h`.
//
// ## Their contract with the ROM
//
// Each leaves the registers, the flags and the stack as the ROM's
// instructions do, and stops on the instruction named: a call, a `WAI` or
// the first of a hold. All but the first need the routine's own data bank.
// The two that read the record need it in the cartridge.
//
// Port code: libc only.

#ifndef PORT_LEVEL_CARD_H
#define PORT_LEVEL_CARD_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define LEVEL_CARD_PC 0x82ac07u
#define LEVEL_CARD_BEGUN_PC 0x82ac1au        // `WAI`
#define LEVEL_CARD_SONG_ASK_PC 0x82ac4fu
#define LEVEL_CARD_SONG_ASKED_PC 0x82ac61u   // `JSL $80:CBD9`
#define LEVEL_CARD_BLOCKS_ASK_PC 0x82ac78u
#define LEVEL_CARD_BLOCKS_ASKED_PC 0x82ac8du // `JSL lzss_decompress`

#define LEVEL_CARD_DATA_BANK 0x82u
#define LEVEL_CARD_WORDS_BANK 0x009fu        // ...and the record's
#define LEVEL_CARD_SCREEN_OFF 0x0080u
#define LEVEL_CARD_COLOURS 0xf08cu           // in bank `$83`
#define LEVEL_CARD_COLOURS_BANK 0x0083u
#define LEVEL_CARD_COLOUR_BYTES 0x0100u
#define LEVEL_CARD_TILES_PACKED 0xcf80u      // in bank `$92`
#define LEVEL_CARD_TILES_BANK 0x0092u
#define LEVEL_CARD_TILES_VRAM 0x2000u        // a word address
#define LEVEL_CARD_SCRATCH_AT 0x8000u        // in bank `$7E`
#define LEVEL_CARD_SCRATCH_BANK 0x007eu
#define LEVEL_CARD_DROP_FRAMES 0x000fu       // counted down past zero
#define W_LEVEL_CARD_RECORD 0x0038u

// The first, which is come to with the caller's data bank.
void level_card_begin(Wram* w, PortCpu* c);

// Is the record a stretch is about to read in the cartridge? `record` is
// the word `song_ask` is about to pull, or the one `blocks_unpack_ask` finds
// at `W_LEVEL_CARD_RECORD`.
bool level_card_record_supported(uint16_t record);
void level_card_song_ask(Wram* w, const Rom* rom, PortCpu* c);
void level_card_blocks_unpack_ask(Wram* w, const Rom* rom, PortCpu* c);

// The rest: a name, where it starts and the instruction it stops on.
#define LEVEL_CARD_STRETCHES(X) \
  X(colours_ask, "$82:AC1F", 0x82ac1fu, 0x82acf9u) \
  X(tiles_unpack_ask, "$82:ACFD", 0x82acfdu, 0x82ad0au) \
  X(tiles_send_ask, "$82:AD0E", 0x82ad0eu, 0x82ad19u) \
  X(map_clear_ask, "$82:AD1D", 0x82ad1du, 0x82ac23u) \
  X(first_drop_begin, "$82:AC48", 0x82ac48u, 0x82ae75u) \
  X(second_words_ask, "$82:AC6D", 0x82ac6du, 0x82ac71u) \
  X(second_drop_begin, "$82:AC75", 0x82ac75u, 0x82ae75u) \
  X(blocks_unpacked, "$82:AC91", 0x82ac91u, 0x82ac92u) \
  X(blank, "$82:ACA6", 0x82aca6u, 0x82acafu)

#define X(name, sym, entry, exit) void level_card_##name(Wram* w, PortCpu* c);
LEVEL_CARD_STRETCHES(X)
#undef X

#endif
