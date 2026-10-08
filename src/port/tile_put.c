// $80:ABD3  one tile changed in the map -- see port/tile_put.h.

#include "port/tile_put.h"

#include "port/coverage.h"
#include "port/dma.h"  // the runs' numbers
#include "port/terrain.h"
#include "port/thread.h"

// `$80:A9F3`: is the tile in the camera's window? The compares in the ROM's
// order, and which of them decided.
static TilePutWindow window(const Wram* w, uint16_t col, uint16_t row) {
  if (col < wram_r16(w, W_WINDOW_LEFT)) return TILE_PUT_LEFT_OF;
  if (col >= wram_r16(w, W_WINDOW_RIGHT)) return TILE_PUT_RIGHT_OF;
  if (row < wram_r16(w, W_WINDOW_TOP)) return TILE_PUT_ABOVE;
  const uint16_t bottom = wram_r16(w, W_WINDOW_BOTTOM);
  if (row == bottom) return TILE_PUT_LAST_ROW;
  if (row > bottom) return TILE_PUT_BELOW;
  return TILE_PUT_INSIDE;
}

// Where in VRAM the tile is: an offset for its column in the layer, one for
// its row, and the layer's base. The second sum takes the first's carry.
static uint16_t vram_address(Wram* w, const Rom* rom, uint16_t col,
                             uint16_t row) {
  const uint16_t layer_x =
      (uint16_t)(col - wram_r16(w, W_WINDOW_LEFT) +
                 wram_r16(w, W_WINDOW_LAYER_X)) & 0x003fu;
  const uint16_t layer_y =
      (uint16_t)(row - wram_r16(w, W_WINDOW_TOP) +
                 wram_r16(w, W_WINDOW_LAYER_Y)) & 0x001fu;
  const uint16_t across = rom_word(rom, TILE_PUT_COLUMNS + 2 * layer_x);
  wram_w16(w, 0x003e, across);
  const uint32_t sum = (uint32_t)rom_word(rom, TILE_PUT_ROWS + 2 * layer_y) +
                       across;
  return (uint16_t)((sum & 0xffffu) + wram_r16(w, W_WINDOW_VRAM) +
                    (sum > 0xffffu ? 1 : 0));
}

void map_tile_put(Wram* w, const Rom* rom, uint16_t tile, uint16_t col,
                  uint16_t row, TilePutRegs* out) {
  wram_w16(w, W_TILE_PUT_BUSY, (uint16_t)(wram_r16(w, W_TILE_PUT_BUSY) + 1));
  out->over_sprites =
      (tile & TILE_NUMBER_MASK) < wram_r16(w, W_TILE_OVER_SPRITES_BELOW);
  if (out->over_sprites) {
    PORT_COVER(tile_put_over_sprites);
    tile |= TILE_OVER_SPRITES;
  }
  wram_w16(w, 0x0038, tile);
  wram_w16(w, 0x003a, col);
  wram_w16(w, 0x003c, row);

  TilemapAddrRegs at;
  tilemap_tile_addr(w, col, row, &at);
  wram_w16(w, 0x0028, at.a);
  wram_w16(w, 0x002a, TERRAIN_MAP_BANK);
  wram_w16(w, ((uint32_t)(TERRAIN_MAP_BANK & 1) << 16) + at.a, tile);

  out->a = tile;
  out->x = col;
  out->y = row;
  out->c = false;
  out->asked = false;
  out->slot = -1;
  out->window = window(w, col, row);
  if (out->window == TILE_PUT_LAST_ROW || out->window == TILE_PUT_INSIDE) {
    PORT_COVER(tile_put_on_screen);
    const uint16_t vram = vram_address(w, rom, col, row);
    uint16_t bytes = wram_r16(w, W_TILE_PUT_BYTES);
    wram_w16(w, (uint16_t)(W_TILE_PUT_VRAM + bytes), vram);
    wram_w16(w, (uint16_t)(W_TILE_PUT_WORDS + bytes), tile);
    bytes = (uint16_t)(bytes + 2);
    wram_w16(w, W_TILE_PUT_BYTES, bytes);
    out->x = bytes;
    out->c = bytes >= 2;
    if (bytes == 2) {
      PORT_COVER(tile_put_asked);
      const uint16_t job = MAP_TILE_JOB_PC & 0xffffu;
      const uint16_t bank = MAP_TILE_JOB_PC >> 16;
      out->asked = true;
      out->slot = vbl_queue_a_add(w, job, bank);
      VblQueueFlags f;
      vbl_queue_flags(w, W_VBL_QUEUE_A_COUNT, bank, out->slot >= 0, &f);
      out->c = f.c;
      if (out->slot < 0) {
        out->a = job;
        out->y = bank;
      } else {
        out->a = (uint16_t)(job - 1);
        out->y = out->a;
        out->x = (uint16_t)out->slot;
      }
    }
  } else {
    PORT_COVER(tile_put_off_screen);
  }
  wram_w16(w, W_TILE_PUT_BUSY, 0);
}

// --- $80:AC55  the list sent --------------------------------------------------

#define REG_VMAIN 0x2115u   // how VRAM's address steps
#define REG_VMADD 0x2116u   // VRAM's address, in words
#define REG_VMDATA 0x2118u  // a word to it
#define VMAIN_STEP_ON_HIGH 0x80u

bool tile_put_job_supported(const Wram* w) {
  const uint16_t bytes = wram_r16(w, W_TILE_PUT_BYTES);
  return (bytes & 1) == 0 && bytes <= TILE_PUT_LIST_BYTES;
}

TileJobFate tile_put_job(Wram* w, HwTrace* t, uint16_t* a_out) {
  const uint16_t busy = wram_r16(w, W_TILE_PUT_BUSY);
  hw_run(t, TPJ_HEAD);
  if (busy != 0) {
    PORT_COVER(tile_job_busy);
    hw_run(t, DMA_TAKEN);
    hw_run(t, DMA_FLAG_RTL);
    *a_out = busy;
    return TILE_JOB_BUSY;
  }
  hw_run(t, TJ_HEAD);
  hw_w8(t, REG_VMAIN, VMAIN_STEP_ON_HIGH);
  const uint16_t bytes = wram_r16(w, W_TILE_PUT_BYTES);
  hw_run(t, TPJ_COUNT);
  if (bytes == 0) {
    PORT_COVER(tile_job_empty);
    hw_run(t, DMA_TAKEN);
    hw_run(t, DMA_FLAG_RTL);
    *a_out = VMAIN_STEP_ON_HIGH;
    return TILE_JOB_EMPTY;
  }

  PORT_COVER(tile_job_sent);
  uint16_t word = 0;
  for (int at = bytes - 2; at >= 0; at -= 2) {
    hw_run(t, at == bytes - 2 ? TPJ_FIRST : TPJ_LOAD);
    hw_w16(t, REG_VMADD, wram_r16(w, (uint16_t)(W_TILE_PUT_VRAM + at)));
    word = wram_r16(w, (uint16_t)(W_TILE_PUT_WORDS + at));
    hw_run(t, TPJ_LOAD);
    hw_w16(t, REG_VMDATA, word);
    hw_run(t, TPJ_NEXT);
    if (at != 0) hw_run(t, DMA_TAKEN);
  }
  wram_w16(w, W_TILE_PUT_BYTES, 0);
  hw_run(t, TPJ_TAIL);
  *a_out = word;
  return TILE_JOB_SENT;
}
