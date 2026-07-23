// SNES graphics formats: planar tiles and BGR555 palettes.
//
// Nothing here is ZAMN-specific — it is the hardware's own encoding, and the
// bytes are exactly what the game DMAs to VRAM ($2118) and CGRAM ($2122). The
// ROM addresses that feed those two ports are listed in `analysis/dma_log.csv`,
// which is how a decode gets cross-checked: point `zamn_assets gfx` at a source
// the DMA log names and the picture has to come out right.
//
// Like lzss.c this is port code, not tooling: no dependencies beyond libc.

#ifndef ASSETS_GFX_H
#define ASSETS_GFX_H

#include <stdint.h>

#define GFX_TILE_W 8
#define GFX_TILE_H 8
#define GFX_TILE_PIXELS (GFX_TILE_W * GFX_TILE_H)

// Bytes one tile occupies: 8 rows x `bpp` bitplanes, one byte each.
uint32_t gfx_tile_bytes(int bpp);

// Decode one tile into 64 palette indices, row-major. `src` must hold
// gfx_tile_bytes(bpp) bytes.
//
// The planes are stored in pairs: bytes 0-15 are bitplanes 0 and 1 interleaved
// by row, bytes 16-31 planes 2 and 3, and so on. That layout is why a 4bpp tile
// can be handed to a 2bpp-reading PPU mode and still show the low two planes.
void gfx_decode_tile(const uint8_t* src, int bpp, uint8_t out[GFX_TILE_PIXELS]);

// Decode a run of tiles. Returns how many were decoded — `len / tile bytes`,
// capped at `max_tiles`. `out` needs 64 bytes per tile.
uint32_t gfx_decode_tiles(const uint8_t* src, uint32_t len, int bpp,
                          uint8_t* out, uint32_t max_tiles);

// Decode `n_colors` BGR555 words into 3-bytes-per-colour RGB888.
//
// Each 5-bit channel is expanded by replicating its top bits into the low
// ones (c << 3 | c >> 2), so $1F maps to 255 rather than 248.
void gfx_decode_palette(const uint8_t* src, uint32_t n_colors, uint8_t* out_rgb);

#endif
