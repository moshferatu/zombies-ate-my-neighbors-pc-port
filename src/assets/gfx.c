#include "assets/gfx.h"

#include <string.h>

uint32_t gfx_tile_bytes(int bpp) {
  if (bpp != 2 && bpp != 4 && bpp != 8) return 0;
  return (uint32_t)(GFX_TILE_H * bpp);
}

void gfx_decode_tile(const uint8_t* src, int bpp, uint8_t out[GFX_TILE_PIXELS]) {
  memset(out, 0, GFX_TILE_PIXELS);
  if (gfx_tile_bytes(bpp) == 0) return;

  for (int pair = 0; pair < bpp / 2; pair++) {
    const uint8_t* plane = src + pair * 16;
    for (int y = 0; y < GFX_TILE_H; y++) {
      uint8_t lo = plane[y * 2];
      uint8_t hi = plane[y * 2 + 1];
      for (int x = 0; x < GFX_TILE_W; x++) {
        int bit = 7 - x;
        uint8_t px = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
        out[y * GFX_TILE_W + x] |= (uint8_t)(px << (pair * 2));
      }
    }
  }
}

uint32_t gfx_decode_tiles(const uint8_t* src, uint32_t len, int bpp,
                          uint8_t* out, uint32_t max_tiles) {
  uint32_t stride = gfx_tile_bytes(bpp);
  if (stride == 0) return 0;
  uint32_t tiles = len / stride;
  if (tiles > max_tiles) tiles = max_tiles;
  for (uint32_t i = 0; i < tiles; i++) {
    gfx_decode_tile(src + i * stride, bpp, out + i * GFX_TILE_PIXELS);
  }
  return tiles;
}

void gfx_decode_palette(const uint8_t* src, uint32_t n_colors, uint8_t* out_rgb) {
  for (uint32_t i = 0; i < n_colors; i++) {
    uint16_t c = (uint16_t)(src[i * 2] | ((uint16_t)src[i * 2 + 1] << 8));
    uint8_t r = (uint8_t)(c & 0x1f);
    uint8_t g = (uint8_t)((c >> 5) & 0x1f);
    uint8_t b = (uint8_t)((c >> 10) & 0x1f);
    out_rgb[i * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
    out_rgb[i * 3 + 1] = (uint8_t)((g << 3) | (g >> 2));
    out_rgb[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
  }
}
