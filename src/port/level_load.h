// A level loaded from its record: what `$80:86A2` does between its calls.
//
// `level_load` is a list of calls, and nearly every routine it calls has a
// port. What is here is the rest: the few instructions before each call that
// read the level's record and put what the callee wants where it wants it.
// Each is a stretch that stops on the call.
//
// ## The record
//
// A level's record is in bank `$9F`, and `level_record_find` leaves its
// address on the thread's page at `$10`. These are the words `level_load`
// reads, by their offsets:
//
//     +$04  the map of blocks, a far pointer
//     +$08  the tiles' attributes, a far pointer
//     +$0C  the tiles' pictures, a far pointer
//     +$10  the colours, a far pointer
//     +$14  the sprites' colours, a far pointer
//     +$18  a thread of the level's own, or zero for none
//     +$1C  the list `$81:80EC` spawns from
//     +$1E  the victims
//     +$20  the list `$80:C8F6` spawns from
//     +$22  the map's size in blocks, across and down
//     +$26  the tile under which a tile is drawn over the sprites
//     +$28  the last tile of the pictures
//     +$2A  where each player starts: x, y, x, y
//     +$3C  more threads, eight bytes each, ended by a zero address
//
// ## The stretches, in order
//
//   * **The map.** The library of blocks is at `$7E:8000`. The map's first
//     row and size are the record's. Then `tilemap_row_tables` and
//     `blockmap_expand`.
//   * **The attributes**, for `tile_attrs_load`.
//   * **The pictures.** Their far pointer goes to page zero and to
//     `W_TILE_ANIM_BASE`, and all of them are sent to VRAM at `$2000`.
//   * **The camera.** It is put halfway between where the two players
//     start, less half a screen, whether or not both are playing.
//   * **The screen.** The first layer's map is at `$7800`, and
//     `screen_tiles_fill` gets that page and a 1.
//   * **The colours**, twice, and a vblank job to send them.
//   * **The level's own thread**, where the record has one.
//   * **Three threads every level has**: the two spawners and the victims',
//     each given its list as a far pointer on the page. Between them the
//     victims' list is parsed.
//   * **The record's other threads.** Their table's address is kept on the
//     stack. Three frames go by before each is looked at. An entry is a
//     thread's far address and two words for its page.
//
// ## Three routines it calls
//
// `tiles_upload_begin` and `tiles_upload_end` are `$80:9F29` on either side
// of its wait. It sends tiles `first` to `last` of the pictures page zero
// points at, 32 bytes each, to VRAM from `vram`. The first stretch works out
// where and how many and stops on the call that queues the job at `$80:9ED0`.
// The wait is `port/hold.h`'s. The second takes the direct page back.
//
// `camera_place` is `$80:A9CC`: the camera at X and Y, kept on the map. A
// place before the map is zero. One past the furthest the camera may go is
// that furthest. But X past its own limit gets the limit of Y. I take that
// for the ROM's mistake, and the port makes it too.
//
// `palette_job_ask` is `$80:A079` as far as its call: the job at `$80:A084`,
// which sends the colours.
//
// ## Their contract with the ROM
//
// Each leaves the registers and the flags as the ROM's instructions do, and
// stops on the instruction named. The stores to low memory go through the
// data bank, which has to see it.
//
// Port code: libc only.

#ifndef PORT_LEVEL_LOAD_H
#define PORT_LEVEL_LOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define LEVEL_DP_RECORD 0x10
#define LEVEL_RECORD_BANK 0x9fu
#define LEVEL_BLOCK_LIBRARY_AT 0x8000u
#define LEVEL_BLOCK_LIBRARY_BANK 0x007eu
#define LEVEL_TILES_VRAM 0x2000u
#define LEVEL_MAP_VRAM 0x7800u
#define LEVEL_EXTRA_THREADS 0x3c  // in the record
#define LEVEL_EXTRA_THREAD_BYTES 8
#define W_TILE_PICTURES 0x0028u  // a far pointer, on page zero

// The simple stretches: a name, where it starts and the call it stops on.
#define LEVEL_LOAD_STRETCHES(X) \
  X(map_ask, "$80:86A2", 0x8086a2u, 0x8086c8u) \
  X(attrs_ask, "$80:86D0", 0x8086d0u, 0x8086dbu) \
  X(tiles_ask, "$80:86DF", 0x8086dfu, 0x808707u) \
  X(camera_ask, "$80:870B", 0x80870bu, 0x80872bu) \
  X(screen_ask, "$80:872F", 0x80872fu, 0x808739u) \
  X(palette_ask, "$80:873D", 0x80873du, 0x808748u) \
  X(sprite_palette_ask, "$80:874C", 0x80874cu, 0x808757u) \
  X(spawner_ask, "$80:8778", 0x808778u, 0x80878bu) \
  X(victims_thread_ask, "$80:878F", 0x80878fu, 0x8087a2u) \
  X(victims_ask, "$80:87A6", 0x8087a6u, 0x8087b0u) \
  X(objects_ask, "$80:87B4", 0x8087b4u, 0x8087c7u) \
  X(extras_begin, "$80:87CB", 0x8087cbu, 0x8087d2u)

#define X(name, sym, entry, exit) \
  void level_##name(Wram* w, const Rom* rom, PortCpu* c);
LEVEL_LOAD_STRETCHES(X)
#undef X

// The level's own thread: false, and stopped at `$80:8778`, where the
// record has none.
#define LEVEL_THREAD_ASK_PC 0x80875fu
#define LEVEL_THREAD_SPAWN_PC 0x808774u
#define LEVEL_THREAD_NONE_PC 0x808778u
bool level_thread_ask(Wram* w, const Rom* rom, PortCpu* c);

// The next of the record's other threads: false, and stopped on the `RTL`,
// at the table's end.
#define LEVEL_EXTRA_NEXT_PC 0x8087d5u
#define LEVEL_EXTRA_SPAWN_PC 0x8087fbu
#define LEVEL_LOAD_RTL_PC 0x808802u
bool level_extra_next(Wram* w, const Rom* rom, PortCpu* c);

// The record has to be in the cartridge, and so does the table's entry the
// stack holds.
bool level_record_supported(const Wram* w, uint16_t page);
bool level_extra_supported(const Wram* w, uint16_t s);

#define TILES_UPLOAD_PC 0x809f29u
#define TILES_UPLOAD_QUEUE_PC 0x809f58u  // `JSL vbl_queue_a_add`
#define TILES_UPLOAD_END_PC 0x809f60u
#define TILES_UPLOAD_RTL_PC 0x809f61u
#define TILES_UPLOAD_JOB 0x9ed0u
#define TILES_UPLOAD_JOB_BANK 0x0080u
#define TILE_PICTURE_BYTES 32
// Page zero: the job's source, its bank, how much is left and where to.
#define UPLOAD_DP_FIRST 0x38
#define UPLOAD_DP_FIRST_WORDS 0x3a
#define UPLOAD_DP_FROM 0xc2
#define UPLOAD_DP_BANK 0xc4
#define UPLOAD_DP_LEFT 0xc6
#define UPLOAD_DP_VRAM 0xc8

// A is the first tile, X the last and Y the VRAM address of tile zero.
void tiles_upload_begin(Wram* w, PortCpu* c);
void tiles_upload_end(const Wram* w, PortCpu* c);

#define CAMERA_PLACE_PC 0x80a9ccu
#define CAMERA_PLACE_RTL_PC 0x80a9f2u

typedef enum {
  CAMERA_PLACE_BEFORE,  // negative: zero
  CAMERA_PLACE_ON,
  CAMERA_PLACE_PAST,    // the limit or more: the limit of Y
} CameraPlace;

typedef struct {
  CameraPlace x, y;
} CameraPlaced;

CameraPlaced camera_place(Wram* w, uint16_t x, uint16_t y);

#define PALETTE_JOB_ASK_PC 0x80a079u
#define PALETTE_JOB_QUEUE_PC 0x80a07fu  // `JSL vbl_queue_a_add`
#define PALETTE_JOB 0xa084u
#define PALETTE_JOB_BANK 0x0080u
void palette_job_ask(PortCpu* c);

#endif  // PORT_LEVEL_LOAD_H
