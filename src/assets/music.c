#include "assets/music.h"

#include <string.h>

int music_set_addr(const Rom* rom, int index, uint32_t* out_addr) {
  if (index < 0 || index >= MUSIC_SET_COUNT) return MUSIC_ERR_RANGE;
  uint32_t entry = MUSIC_TABLE_ADDR + (uint32_t)index * MUSIC_SET_STRIDE;
  if (!rom_has(rom, entry, MUSIC_SET_STRIDE)) return MUSIC_ERR_ADDRESS;

  // `$80:CC84`/`$80:CC8A` load the bank and the address as two words, so the
  // entry's fourth byte is the high half of the bank word and is always 0.
  uint32_t addr = (uint32_t)rom_word(rom, entry + 2) << 16 | rom_word(rom, entry);
  if (out_addr) *out_addr = addr;
  return MUSIC_OK;
}

int music_driver_image(const Rom* rom, uint8_t* out, uint32_t out_size) {
  if (out_size < MUSIC_DRIVER_BYTES) return MUSIC_ERR_SIZE;

  static const struct { uint32_t addr, bytes; } part[2] = {
    {MUSIC_DRIVER_PART0_ADDR, MUSIC_DRIVER_PART0_BYTES},
    {MUSIC_DRIVER_PART1_ADDR, MUSIC_DRIVER_PART1_BYTES},
  };
  uint32_t at = 0;
  for (int i = 0; i < 2; i++) {
    uint32_t avail = 0;
    const uint8_t* src = rom_ptr(rom, part[i].addr, &avail);
    if (!src || avail < part[i].bytes) return MUSIC_ERR_ADDRESS;
    memcpy(out + at, src, part[i].bytes);
    at += part[i].bytes;
  }
  return MUSIC_OK;
}

int music_driver_blocks(const uint8_t* image, uint32_t size, MusicSet* out) {
  memset(out, 0, sizeof *out);
  out->index = MUSIC_SET_DRIVER;
  out->addr = MUSIC_DRIVER_STAGE_ADDR;

  uint32_t at = 0;
  for (;;) {
    if (size - at < 4) return MUSIC_ERR_FORMAT;
    uint16_t len = (uint16_t)(image[at] | image[at + 1] << 8);
    uint16_t dest = (uint16_t)(image[at + 2] | image[at + 3] << 8);
    at += 4;

    // A zero length ends the list; the word that would be a destination is the
    // address the boot ROM jumps to instead.
    if (len == 0) {
      out->exec = dest;
      out->stream_bytes = at;
      return MUSIC_OK;
    }
    if (out->count >= MUSIC_MAX_BLOCKS) return MUSIC_ERR_FORMAT;
    if (size - at < len) return MUSIC_ERR_FORMAT;

    out->blocks[out->count].addr = MUSIC_DRIVER_STAGE_ADDR + at;
    out->blocks[out->count].bytes = len;
    out->blocks[out->count].dest = dest;
    out->count++;
    out->payload_bytes += len;
    at += len;
  }
}

int music_set_read(const Rom* rom, int index, MusicSet* out) {
  if (index == MUSIC_SET_DRIVER) return MUSIC_ERR_RANGE;  // use music_driver_*

  uint32_t addr = 0;
  int err = music_set_addr(rom, index, &addr);
  if (err != MUSIC_OK) return err;

  memset(out, 0, sizeof *out);
  out->index = index;
  out->addr = addr;

  // The stream is walked with a 16-bit pointer inside one bank (`$80:CCBF`
  // increments `$18`/`$19` only), so running past the bank is malformed data
  // rather than a read that wraps into the next bank.
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, addr, &avail);
  if (!p) return MUSIC_ERR_ADDRESS;

  uint32_t at = 0;
  for (;;) {
    if (avail - at < 2) return MUSIC_ERR_FORMAT;
    uint16_t len = (uint16_t)(p[at] | p[at + 1] << 8);
    at += 2;
    if (len == 0) {
      out->stream_bytes = at;
      return MUSIC_OK;
    }
    if (out->count >= MUSIC_MAX_BLOCKS) return MUSIC_ERR_FORMAT;
    if (avail - at < len) return MUSIC_ERR_FORMAT;

    out->blocks[out->count].addr = addr + at;
    out->blocks[out->count].bytes = len;
    out->count++;
    out->payload_bytes += len;
    at += len;
  }
}

int music_upload(const Rom* rom, int index, bool select, MusicSendFn send, void* ctx) {
  MusicSet set;
  int err = music_set_read(rom, index, &set);
  if (err != MUSIC_OK) return err;

  if (select) send(MUSIC_CMD_SELECT, (uint8_t)index, ctx);

  for (int i = 0; i < set.count; i++) {
    const MusicBlock* b = &set.blocks[i];
    uint32_t avail = 0;
    const uint8_t* p = rom_ptr(rom, b->addr, &avail);
    if (!p || avail < b->bytes) return MUSIC_ERR_ADDRESS;

    send(MUSIC_CMD_UPLOAD_BLOCK, music_block_param(b->bytes), ctx);
    for (uint32_t n = 0; n < b->bytes; n++) send(MUSIC_CMD_UPLOAD_BYTE, p[n], ctx);
  }
  return MUSIC_OK;
}
