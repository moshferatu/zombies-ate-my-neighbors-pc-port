// The logo screens' routine, between its calls and its waits.
//
// `$83:8000` shows the logo screens. Its eight loops are `port/logo.h`, the
// tilemap it begins with and the colours it copies are `port/frontend.h`,
// and what it calls is ported where each of those is. This is the rest: the
// instructions that set out what a call is made with, and the ones that put
// something in memory before a wait.
//
//     $83:8000  begin              its data bank, page zero for a direct
//                                  page, and the brightness shadow's top bit
//                                  set: the screen off
//     $83:8060  map_send_ask       the tilemap made at `$7E:8000`, to VRAM
//                                  at `$4000`
//     $83:8071  map_2_unpack_ask   the second layer's map, to be unpacked
//                                  there
//     $83:8083  map_2_send_ask     ...and sent to VRAM at `$6000`
//     $83:8095  tiles_unpack_ask   the tiles' pictures, likewise
//     $83:80A7  tiles_send_ask     ...to VRAM at 0
//     $83:80B9  job_ask            the screen's vblank job, to be queued
//     $83:80C4  job_kept           the slot it was given, kept
//     $83:80F4  slide_begin        full brightness, and the slide's tile
//     $83:8172  sweep_colours_ask  64 bytes of colours for the sweep
//     $83:817E  sweep_begin        ten times round
//     $83:81C5  sweep_2_begin      the same for the second sweep
//     $83:822A  job_remove_ask     the job's slot, to be given back
//     $83:8230  job_forgotten      ...and forgotten
//     $83:8233  blank              the screen off again
//     $83:823E  end                the caller's direct page and data bank
//
// The two sends and the unpacking take one argument on the stack, a word
// pushed before the call and pulled after it. A stretch that follows such a
// call begins by pulling the last one's.
//
// `$83:8015`, which sets the layers up, and `$83:80C7`, which turns them
// on, write to the picture hardware. They are in `port/dma.h`.
//
// ## Their contract with the ROM
//
// Each leaves the registers, the flags and the stack as the ROM's
// instructions do, and stops on the instruction named: a call, a `WAI` or
// the `RTL`. All but the first need the routine's own data bank and a
// direct page that is memory.
//
// Port code: libc only.

#ifndef PORT_INTRO_H
#define PORT_INTRO_H

#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

#define INTRO_PC 0x838000u
#define INTRO_BEGUN_PC 0x838011u      // `JSL hold_vram_cleared`
#define INTRO_DATA_BANK 0x83u
#define INTRO_SCRATCH_AT 0x8000u      // in bank `$7E`
#define INTRO_SCRATCH_BANK 0x007eu
#define INTRO_MAP_VRAM 0x4000u        // word addresses
#define INTRO_MAP_2_VRAM 0x6000u
#define INTRO_TILES_VRAM 0x0000u
#define INTRO_MAP_BYTES 0x0800u
#define INTRO_TILES_BYTES 0x2000u
#define INTRO_MAP_2_PACKED 0x830eu    // in the routine's bank
#define INTRO_TILES_PACKED 0x8598u
#define INTRO_JOB 0x8255u             // `INTRO_SCREEN_JOB_PC`
#define INTRO_SCREEN_OFF 0x0080u
#define INTRO_FULL_BRIGHTNESS 0x000fu
#define INTRO_SLIDE_FIRST_TILE 0x0006u
#define INTRO_SWEEP_COLOURS 0x9290u   // in the routine's bank
#define INTRO_SWEEP_COLOUR_BYTES 0x0040u
#define INTRO_SWEEP_FIRST_BYTE 0x0020u  // of the colours shown
#define INTRO_SWEEP_TIMES 0x000au
#define INTRO_DP_JOB_SLOT 0x40

// The first, which is come to with the caller's data bank and direct page.
void intro_begin(Wram* w, PortCpu* c);

// The rest: a name, where it starts and the instruction it stops on.
#define INTRO_STRETCHES(X) \
  X(map_send_ask, "$83:8060", 0x838060u, 0x83806du) \
  X(map_2_unpack_ask, "$83:8071", 0x838071u, 0x83807fu) \
  X(map_2_send_ask, "$83:8083", 0x838083u, 0x838091u) \
  X(tiles_unpack_ask, "$83:8095", 0x838095u, 0x8380a3u) \
  X(tiles_send_ask, "$83:80A7", 0x8380a7u, 0x8380b5u) \
  X(job_ask, "$83:80B9", 0x8380b9u, 0x8380c0u) \
  X(job_kept, "$83:80C4", 0x8380c4u, 0x8380c6u) \
  X(slide_begin, "$83:80F4", 0x8380f4u, 0x838101u) \
  X(sweep_colours_ask, "$83:8172", 0x838172u, 0x83817bu) \
  X(sweep_begin, "$83:817E", 0x83817eu, 0x838183u) \
  X(sweep_2_begin, "$83:81C5", 0x8381c5u, 0x8381cau) \
  X(job_remove_ask, "$83:822A", 0x83822au, 0x83822cu) \
  X(job_forgotten, "$83:8230", 0x838230u, 0x838232u) \
  X(blank, "$83:8233", 0x838233u, 0x838239u) \
  X(end, "$83:823E", 0x83823eu, 0x838240u)

#define X(name, sym, entry, exit) void intro_##name(Wram* w, PortCpu* c);
INTRO_STRETCHES(X)
#undef X

#endif
