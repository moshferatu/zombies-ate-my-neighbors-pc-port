// Music and sound: the SPC700 driver image, the data sets it plays, and the
// command protocol the 65816 side drives it with.
//
// ZAMN keeps its whole audio program on the SPC700, so — as in zelda3, and per
// PLAN.md's Phase 4 — the port never interprets a note. What it must reproduce
// is the *CPU side*: hand the APU the same driver image, upload the same data
// sets, and send the same commands in the same order. Then the emulated SPC700
// and DSP produce the original audio by construction.
//
// So "the music format" here is not a sequence format. It is:
//
//   1. a 16-entry table of far pointers at `$80:CCDE` (`music_set_addr`);
//   2. entry 0, the driver + samples, staged into WRAM and uploaded through the
//      SNES IPL boot ROM's own protocol (`music_driver_*`);
//   3. entries 1-15, uploaded a byte at a time through the running driver's
//      command port (`music_set_read`, `music_upload`);
//   4. the command protocol itself (`MusicCmd`, `music_send`-style callbacks).
//
// Which set a level plays comes out of its level record: `+$32` is the song and
// `+$34` selects the sample set. See `docs/asset-formats.md`.
//
// Port code: libc only.

#ifndef ASSETS_MUSIC_H
#define ASSETS_MUSIC_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"

// The table of far pointers `$80:CC7C` indexes (`LDA $80CCE0,X` for the bank
// and `LDA $80CCDE,X` for the address, with X = index * 4).
#define MUSIC_TABLE_ADDR 0x80ccdeu
#define MUSIC_SET_COUNT 16
#define MUSIC_SET_STRIDE 4

// What the 16 sets are. 0 is the driver; 1 is the sound-effect bank, loaded
// once at boot by `$80:8611`; 2-11 are songs, named by a level record's `+$32`;
// 12-15 are sample sets, named by `+$34` (the ROM adds the 12 at `$80:CBEB`).
#define MUSIC_SET_DRIVER 0
#define MUSIC_SET_SFX 1
#define MUSIC_SONG_FIRST 2
#define MUSIC_SONG_LAST 11
#define MUSIC_SET_SAMPLES 12
#define MUSIC_SAMPLE_SET_COUNT 4

// The driver image is not contiguous in ROM. `$80:CB1A` gathers it with two
// `MVN`s into WRAM `$7F:0000` and points the uploader there, which is why the
// table's entry 0 is a WRAM address.
#define MUSIC_DRIVER_STAGE_ADDR 0x7f0000u
#define MUSIC_DRIVER_PART0_ADDR 0x918000u
#define MUSIC_DRIVER_PART0_BYTES 0x8000u
#define MUSIC_DRIVER_PART1_ADDR 0x95ba3du
#define MUSIC_DRIVER_PART1_BYTES 0x1a86u
#define MUSIC_DRIVER_BYTES (MUSIC_DRIVER_PART0_BYTES + MUSIC_DRIVER_PART1_BYTES)

// No set comes close to this; it only bounds `MusicSet`. The sound-effect bank
// is the only set with more than one block, and it has 50.
#define MUSIC_MAX_BLOCKS 64

// The APU I/O ports, as `$80:CCC8` uses them: the command in `$2142`, its
// parameter in `$2141`, and an 8-bit sequence counter in `$2143` that the SPC
// echoes back to acknowledge. The CPU spins until the echo matches before
// writing the next command, so the stream is fully ordered.
#define MUSIC_PORT_ACK 0x2140u    // IPL upload only: the boot ROM's counter
#define MUSIC_PORT_PARAM 0x2141u
#define MUSIC_PORT_CMD 0x2142u
#define MUSIC_PORT_SEQ 0x2143u

// Commands seen driven from the 65816 side. The upload trio is what
// `music_upload()` emits; the rest are single sends from named wrappers.
typedef enum {
  MUSIC_CMD_PLAY_SFX = 0x01,     // $80:CC3B, param = effect id
  MUSIC_CMD_PLAY_SFX_ALT = 0x02, // $80:CC27, a second effect channel
  MUSIC_CMD_UPLOAD_BYTE = 0x06,  // $80:CCA9, param = one payload byte
  MUSIC_CMD_SELECT = 0x08,       // $80:CC6F, param = the set about to arrive
  MUSIC_CMD_UPLOAD_BLOCK = 0x0a, // $80:CCA1, starts a block
  MUSIC_CMD_SFX_READY = 0x0d,    // $80:CBFD, after the sound-effect bank
  MUSIC_CMD_PLAY_SONG = 0x14,    // $80:CBF3, after a song + its samples
} MusicCmd;

typedef enum {
  MUSIC_OK = 0,
  MUSIC_ERR_RANGE = -1,    // set index outside 0..15
  MUSIC_ERR_ADDRESS = -2,  // the table entry does not point at cartridge ROM
  MUSIC_ERR_FORMAT = -3,   // a block runs past its bank, or the stream is unterminated
  MUSIC_ERR_SIZE = -4,     // caller's buffer is too small
} MusicError;

// One block of a data set: `bytes` payload bytes at `addr`.
typedef struct {
  uint32_t addr;
  uint16_t bytes;
  uint16_t dest;  // driver image only: where the IPL boot ROM puts it
} MusicBlock;

// A decoded data set. `stream_bytes` counts the payload *and* the length words
// and the terminator, so `addr + stream_bytes` is where the next set begins —
// the sets are packed end to end, which is how the decode was cross-checked.
typedef struct {
  int index;
  uint32_t addr;
  int count;
  MusicBlock blocks[MUSIC_MAX_BLOCKS];
  uint32_t payload_bytes;
  uint32_t stream_bytes;
  uint16_t exec;  // driver image only: the address the IPL ROM jumps to
} MusicSet;

// Address of data set `index`, straight out of the table at `$80:CCDE`. Entry 0
// is `MUSIC_DRIVER_STAGE_ADDR`, a WRAM address, so it is not a `rom_ptr` target.
int music_set_addr(const Rom* rom, int index, uint32_t* out_addr);

// Build the driver image the way `$80:CB1A` does: two ROM runs concatenated.
// `out` must hold MUSIC_DRIVER_BYTES.
int music_driver_image(const Rom* rom, uint8_t* out, uint32_t out_size);

// Parse the driver image's IPL block list. The format is the one the SNES boot
// ROM speaks: `[len16][dest16][len bytes]` repeated, ending at a zero length
// whose "destination" is the entry point instead (`$80:CBAE` turns len != 0
// into the boot ROM's transfer/execute flag).
int music_driver_blocks(const uint8_t* image, uint32_t size, MusicSet* out);

// Parse data set `index` (1..15). These have no destination words — the running
// driver decides where the bytes land — so the format is just `[len16][len
// bytes]` repeated until a zero length.
int music_set_read(const Rom* rom, int index, MusicSet* out);

// Emit the command stream `$80:CC7C` sends for one data set, in order, to
// `send`. With `select` set, the leading `MUSIC_CMD_SELECT` that `$80:CC6F`
// prefixes is emitted too. Returns MUSIC_OK, or a negative error if the set
// does not decode.
//
// This is the port-facing entry point: pointing `send` at the APU's ports
// reproduces the original traffic exactly, which is what `verify-music` checks.
typedef void (*MusicSendFn)(uint8_t cmd, uint8_t param, void* ctx);
int music_upload(const Rom* rom, int index, bool select, MusicSendFn send, void* ctx);

// The parameter `$80:CCA1` sends with MUSIC_CMD_UPLOAD_BLOCK. The ROM reuses
// the register it tested the length with, so the byte is the two length bytes
// ORed together rather than anything the driver could use as a size. Reproduced
// because the port must put the same value on the bus, not because it means
// something.
static inline uint8_t music_block_param(uint16_t len) {
  return (uint8_t)((len & 0xff) | (len >> 8));
}

#endif
