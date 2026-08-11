// $80:AE14, $80:AE97, $80:B422 — see port/terrain.h.

#include "port/terrain.h"

#include "port/coverage.h"

// The six probes, as byte offsets from the tile the point lands on. The lower
// row is `LDY $B2`, then `INY : INY`, then `LDA $B2 : CLC : ADC #$0004 : TAY` —
// three ways of writing the same addition, which is the ROM saving a byte
// rather than three different intentions.
static uint16_t probe_offset(const Wram* w, int i) {
  uint16_t col = (uint16_t)((i % TERRAIN_PROBE_COLS) * 2);
  if (i < TERRAIN_PROBE_COLS) return col;
  return (uint16_t)(wram_r16(w, W_TILEMAP_ROW_BYTES) + col);
}

// `LDA [$28],Y : AND #$03FF : ASL A : TAY : LDA [$BA],Y`.
//
// Both indirections are 24-bit and both land in WRAM, so both are a bank bit
// and a 16-bit address. The tilemap is bank `$7F` because the routine just
// stored `$007F` itself; the attribute table's bank is whatever the level
// loader put at `$BC`, which is `$7E` everywhere it has been looked at — hence
// the coverage site rather than an assumption.
//
// Neither add is masked to 16 bits here, and neither is on the 65816: `[dp],Y`
// adds Y across the whole 24-bit pointer. It cannot matter for the tilemap —
// the largest level expands to under 40 KB — and the attribute table is 1 KB.
static uint16_t probe_attrs(const Wram* w, uint16_t map, uint16_t yoff,
                            uint16_t* tile) {
  uint32_t entry_off = ((uint32_t)(TERRAIN_MAP_BANK & 1) << 16) + map + yoff;
  uint16_t entry = wram_r16(w, entry_off);
  *tile = (uint16_t)((entry & TILEMAP_INDEX_MASK) << 1);

  uint16_t attrs = wram_r16(w, W_TILE_ATTRS);
  uint8_t bank = wram_r8(w, W_TILE_ATTRS + 2);
  return wram_r16(w, ((uint32_t)(bank & 1) << 16) + attrs + *tile);
}

// The part `$80:AE14` and `$80:AE97` share, which is everything except the mask
// and how the answer is spelled.
//
// `out->a` is the raw attribute word; each caller then applies its own exit
// convention, because `LSR A` leaves a shifted value in A and `BIT #$0002`
// leaves the word alone.
static bool terrain_footprint(Wram* w, uint16_t x, uint16_t y, uint16_t mask,
                              TerrainRegs* out) {
  // $80:AE14-AE30. Two coordinates in, one tilemap offset out.
  uint16_t row = (uint16_t)(((uint16_t)(y - TERRAIN_ORIGIN_Y) >>
                             TERRAIN_TILE_SHIFT) & TERRAIN_TILE_MASK);
  uint16_t col = (uint16_t)(((uint16_t)(x - TERRAIN_ORIGIN_X) >>
                             TERRAIN_TILE_SHIFT) & TERRAIN_TILE_MASK);
  uint16_t map = (uint16_t)(col + wram_r16(w, W_TILE_ROW_BASE + row));

  wram_w16(w, TERRAIN_DP_MAP, map);
  wram_w16(w, TERRAIN_DP_MAP_BANK, TERRAIN_MAP_BANK);

  out->x = row;  // `TAX` at $80:AE29, and nothing touches X again

  if ((wram_r8(w, W_TILE_ATTRS + 2) & 1) != 0) {
    PORT_COVER(terrain_attrs_bank_7f);
  } else {
    PORT_COVER(terrain_attrs_bank_7e);
  }

  for (int i = 0; i < TERRAIN_PROBE_COUNT; i++) {
    uint16_t tile = 0;
    out->a = probe_attrs(w, map, probe_offset(w, i), &tile);
    out->y = tile;
    if (out->a & mask) {
      // Which probe decided is worth a site of its own: the lower row is only
      // ever reached by three tiles of clear terrain above it, and the sixth is
      // the one the ROM leaves without a branch.
      if (i < TERRAIN_PROBE_COLS) {
        PORT_COVER(terrain_hit_upper);
      } else if (i == TERRAIN_PROBE_COUNT - 1) {
        PORT_COVER(terrain_hit_last);
      } else {
        PORT_COVER(terrain_hit_lower);
      }
      return true;
    }
  }
  // $80:AE95 / $80:AF26. The last probe is the only one with no branch after
  // it: falling out of the loop *is* the negative answer.
  PORT_COVER(terrain_clear);
  return false;
}

void terrain_blocked(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out) {
  out->blocked = terrain_footprint(w, x, y, TERRAIN_MASK_SOLID, out);
  // `LSR A` is both the test and the last thing done to A, so what the caller
  // sees is the attribute word shifted down one — on every path, including the
  // one that found nothing.
  out->a = (uint16_t)(out->a >> 1);
}

void terrain_blocked_enemy(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out) {
  // `BIT #$0002` does not disturb A, so this one hands back the word itself.
  out->blocked = terrain_footprint(w, x, y, TERRAIN_MASK_ENEMY, out);
}

// ---------------------------------------------------------------------------
// $82:90F7  terrain_blocked_wide
// ---------------------------------------------------------------------------

// The offset each of the ten probes indexes the map with, and — for the first
// one — the fact that it does not index at all.
//
// `$82:911F  LDA [$28]` has no `,Y`, so the Y register still holds the caller's
// argument when the first probe rejects. Every other probe has set it. That is
// why this returns the register value rather than the offset: they are the same
// thing nine times out of ten, and the tenth is the point.
static uint16_t wide_probe(const Wram* w, int i, uint16_t entry_y,
                           uint16_t* yreg) {
  uint16_t col = (uint16_t)((i % TERRAIN_WIDE_COLS) * 2);
  uint16_t off = i < TERRAIN_WIDE_COLS
                     ? col
                     : (uint16_t)(wram_r16(w, W_TILEMAP_ROW_BYTES) + col);
  *yreg = i == 0 ? entry_y : off;
  return off;
}

void terrain_blocked_wide(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out) {
  // $82:90F8-$82:9118, and it is `$80:AE14`'s opening with one constant
  // changed: `SBC #$0011` where that one has `SBC #$0009`.
  uint16_t row = (uint16_t)(((uint16_t)(y - TERRAIN_WIDE_ORIGIN_Y) >>
                             TERRAIN_TILE_SHIFT) & TERRAIN_TILE_MASK);
  uint16_t col = (uint16_t)(((uint16_t)(x - TERRAIN_WIDE_ORIGIN_X) >>
                             TERRAIN_TILE_SHIFT) & TERRAIN_TILE_MASK);
  uint16_t map = (uint16_t)(col + wram_r16(w, W_TILE_ROW_BASE + row));

  wram_w16(w, TERRAIN_DP_MAP, map);
  wram_w16(w, TERRAIN_DP_MAP_BANK, TERRAIN_MAP_BANK);

  out->x = row;  // `TAX` at $82:910C, and nothing touches X again
  out->y = y;

  uint16_t floor = wram_r16(w, W_TILE_PRIORITY_BELOW);
  uint16_t attrs_ptr = wram_r16(w, W_TILE_ATTRS);
  uint32_t attrs_bank = (uint32_t)(wram_r8(w, W_TILE_ATTRS + 2) & 1) << 16;

  for (int i = 0; i < TERRAIN_WIDE_PROBE_COUNT; i++) {
    uint16_t yreg;
    uint16_t off = wide_probe(w, i, y, &yreg);
    uint16_t entry =
        wram_r16(w, ((uint32_t)(TERRAIN_MAP_BANK & 1) << 16) + map + off);
    uint16_t idx = (uint16_t)(entry & TILEMAP_INDEX_MASK_9);

    // `CMP $00DC : BCC`. The number alone is enough, and A keeps the index
    // rather than an attribute word on this exit.
    if (idx < floor) {
      PORT_COVER_IF(i == 0, wide_floor_first, wide_floor_other);
      out->a = idx;
      out->y = yreg;
      out->blocked = true;
      return;
    }

    uint16_t tile = (uint16_t)(idx << 1);
    out->y = tile;  // `ASL A : TAY`
    uint16_t attrs = wram_r16(w, attrs_bank + attrs_ptr + tile);
    out->a = (uint16_t)(attrs >> 2);  // `LSR A : LSR A`, the test and the value

    if (attrs & TERRAIN_MASK_ENEMY) {
      PORT_COVER_IF(i < TERRAIN_WIDE_COLS, wide_attr_upper, wide_attr_lower);
      out->blocked = true;
      return;
    }
  }

  // $82:91FE. The tenth probe's `LSR A` is the only one with no branch after
  // it, so falling into the `PLD` with its carry clear *is* the answer.
  PORT_COVER(wide_clear);
  out->blocked = false;
}

// ---------------------------------------------------------------------------
// $80:B422  terrain_out_of_bounds
// ---------------------------------------------------------------------------

// `CMP` sets all three, and every exit here is one comparison or one transfer.
static void from_cmp(BoundsRegs* out, uint16_t a, uint16_t m) {
  uint16_t d = (uint16_t)(a - m);
  out->n = (d & 0x8000u) != 0;
  out->z = d == 0;
  out->c = a >= m;
}

// `TXA` / `TYA` set N and Z from the value moved and leave carry alone — but
// every exit reached from one is an explicit `SEC`, so carry is not in doubt.
static void from_move(BoundsRegs* out, uint16_t a) {
  out->n = (a & 0x8000u) != 0;
  out->z = a == 0;
  out->c = true;
}

void terrain_out_of_bounds(Wram* w, uint16_t x, uint16_t y, BoundsRegs* out) {
  out->a = x;
  if (x & 0x8000u) {
    // $80:B423. A negative coordinate is off the map before any arithmetic.
    PORT_COVER(bounds_x_negative);
    from_move(out, x);
    return;
  }
  uint16_t cx = (uint16_t)(x >> 2);
  out->a = cx;
  if (cx < TERRAIN_BOUNDS_MIN_X) {
    PORT_COVER(bounds_x_low);
    from_cmp(out, cx, TERRAIN_BOUNDS_MIN_X);
    out->c = true;  // the `SEC` at $80:B445, after the compare that branched
    return;
  }
  uint16_t hx = (uint16_t)(cx + TERRAIN_BOUNDS_PAD_X);
  out->a = hx;
  uint16_t stride = wram_r16(w, W_TILEMAP_ROW_BYTES);
  if (hx >= stride) {
    // $80:B432. This one branches straight to the `RTL` and keeps the compare's
    // own carry, rather than going by way of the `SEC`.
    PORT_COVER(bounds_x_high);
    from_cmp(out, hx, stride);
    return;
  }

  out->a = y;
  if (y & 0x8000u) {
    PORT_COVER(bounds_y_negative);
    from_move(out, y);
    return;
  }
  uint16_t cy = (uint16_t)(y >> 3);
  out->a = cy;
  if (cy < TERRAIN_BOUNDS_MIN_Y) {
    PORT_COVER(bounds_y_low);
    from_cmp(out, cy, TERRAIN_BOUNDS_MIN_Y);
    out->c = true;
    return;
  }
  uint16_t hy = (uint16_t)(cy + TERRAIN_BOUNDS_PAD_Y);
  out->a = hy;
  // $80:B441. The last compare is the answer: no branch, straight into the
  // `RTL`, so an inside point leaves carry clear and an outside one sets it.
  from_cmp(out, hy, wram_r16(w, W_TILEMAP_ROWS));
  if (out->c) {
    PORT_COVER(bounds_y_high);
  } else {
    PORT_COVER(bounds_inside);
  }
}

// --- $80:AD1C  tilemap_tile_addr -------------------------------------------

void tilemap_tile_addr(const Wram* w, uint16_t x, uint16_t y,
                       TilemapAddrRegs* out) {
  uint16_t col = (uint16_t)(x << 1);   // TXA : ASL A : PHA
  uint16_t row = (uint16_t)(y << 1);   // TYA : ASL A : TAX
  uint16_t base = wram_r16(w, (uint32_t)(W_TILE_ROW_BASE + row));
  uint32_t sum = (uint32_t)base + col;  // CLC : ADC $01,S
  out->a = (uint16_t)sum;
  out->c = sum > 0xffffu;
  // `PLX` last, so N and Z are the doubled column's and not the address's.
  out->x = col;
  out->n = (col & 0x8000u) != 0;
  out->z = col == 0;
}

// --- $80:ADC8 / $80:ADF3  tile_attrs_at_pixel / tile_attrs_at_tile ----------

void tile_attrs_at_tile(const Wram* w, uint16_t col, uint16_t row,
                        TileAttrsRegs* out) {
  // `JSL $80AD1C : TAX`, then `LDA $0000,X` with the data bank the `PEA $007F`
  // put there. The doubled column `tilemap_tile_addr` leaves in X is its own
  // business; this caller only wants A.
  TilemapAddrRegs addr;
  tilemap_tile_addr(w, col, row, &addr);
  uint16_t entry =
      wram_r16(w, ((uint32_t)(TERRAIN_MAP_BANK & 1) << 16) + addr.a);

  // `AND #$03FF : ASL A : TAY : LDA [$BA],Y` -- the same two lines as
  // `probe_attrs` above, and the same 24-bit pointer out of `W_TILE_ATTRS`.
  uint16_t tile = (uint16_t)((entry & TILEMAP_INDEX_MASK) << 1);
  uint16_t attrs = wram_r16(w, W_TILE_ATTRS);
  uint8_t bank = wram_r8(w, W_TILE_ATTRS + 2);
  out->a = wram_r16(w, ((uint32_t)(bank & 1) << 16) + attrs + tile);

  // The `ASL` shifted a ten-bit value, so nothing can have come out of the top.
  out->c = false;
}

void tile_attrs_at_pixel(const Wram* w, uint16_t x, uint16_t y,
                         TileAttrsRegs* out) {
  tile_attrs_at_tile(w, (uint16_t)(x >> TILE_ATTRS_PIXEL_SHIFT),
                     (uint16_t)(y >> TILE_ATTRS_PIXEL_SHIFT), out);
}

// ---------------------------------------------------------------------------
// $80:AF2C / $80:AF66 / $80:B03B / $80:B05F — the rest of the attribute word
// ---------------------------------------------------------------------------

// The opening these four share with `terrain_footprint`, minus the origin bias
// two of them do not apply: `LSR A : LSR A : AND #$FFFE` on each axis, the row
// through `W_TILE_ROW_BASE`, and the 24-bit pointer left behind in `$28`.
//
// Returns the tilemap offset and, through `row`, the value `TAX` puts in X.
static uint16_t attr_map_ptr(Wram* w, uint16_t x, uint16_t y, uint16_t* row) {
  *row = (uint16_t)((y >> TERRAIN_TILE_SHIFT) & TERRAIN_TILE_MASK);
  uint16_t col = (uint16_t)((x >> TERRAIN_TILE_SHIFT) & TERRAIN_TILE_MASK);
  uint16_t map = (uint16_t)(col + wram_r16(w, W_TILE_ROW_BASE + *row));
  wram_w16(w, TERRAIN_DP_MAP, map);
  wram_w16(w, TERRAIN_DP_MAP_BANK, TERRAIN_MAP_BANK);
  return map;
}

// One probe at the map offset already in `$28`, which is `probe_attrs` with the
// Y register published: `AND #$03FF : ASL A : TAY` is what these leave behind.
static uint16_t attr_one(const Wram* w, uint16_t map, TerrainRegs* out) {
  uint16_t tile = 0;
  uint16_t attrs = probe_attrs(w, map, 0, &tile);
  out->y = tile;
  return attrs;
}

void terrain_point_bit2(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out) {
  // $80:AF2D. A real `JSL`, and the only thing in this family that calls
  // anything. Its exit leaves A its own and never touches X or Y, so a point
  // rejected here comes back with the caller's own arguments in both.
  BoundsRegs bounds;
  terrain_out_of_bounds(w, x, y, &bounds);
  if (bounds.c) {
    PORT_COVER(bit2_outside);
    out->a = bounds.a;
    out->x = x;
    out->y = y;
    out->blocked = true;
    return;
  }

  uint16_t map = attr_map_ptr(w, x, y, &out->x);
  // `BIT #$0004` does not disturb A, so what comes back is the whole word.
  out->a = attr_one(w, map, out);
  out->blocked = (out->a & TERRAIN_MASK_BIT2) != 0;
  PORT_COVER_IF(out->blocked, bit2_hit, bit2_clear);
}

void terrain_point_bit8(Wram* w, uint16_t x, uint16_t y, TerrainRegs* out) {
  uint16_t map = attr_map_ptr(w, x, y, &out->x);
  uint16_t attrs = attr_one(w, map, out);
  // `AND #$0100`, so A is the mask or it is zero.
  out->a = (uint16_t)(attrs & TERRAIN_MASK_BIT8);
  out->blocked = out->a != 0;
  PORT_COVER_IF(out->blocked, bit8_hit, bit8_clear);
}

void terrain_tile_bit3(Wram* w, uint16_t col, uint16_t row, TerrainRegs* out) {
  // $80:B040. The one that reaches for the shared leaf instead of inlining it —
  // and inherits its `PLX`, so X comes back as the doubled column rather than
  // as the argument.
  TilemapAddrRegs addr;
  tilemap_tile_addr(w, col, row, &addr);
  out->x = addr.x;

  wram_w16(w, TERRAIN_DP_MAP, addr.a);
  wram_w16(w, TERRAIN_DP_MAP_BANK, TERRAIN_MAP_BANK);

  uint16_t attrs = attr_one(w, addr.a, out);
  out->a = (uint16_t)(attrs & TERRAIN_MASK_BIT3);
  out->blocked = out->a != 0;
  PORT_COVER_IF(out->blocked, bit3_hit, bit3_clear);
}

void terrain_footprint_bit12(Wram* w, uint16_t x, uint16_t y,
                             TerrainRegs* out) {
  // $80:AF67-AF7B. `terrain_footprint`'s opening exactly, bias and all.
  uint16_t row = (uint16_t)(((uint16_t)(y - TERRAIN_ORIGIN_Y) >>
                             TERRAIN_TILE_SHIFT) & TERRAIN_TILE_MASK);
  uint16_t col = (uint16_t)(((uint16_t)(x - TERRAIN_ORIGIN_X) >>
                             TERRAIN_TILE_SHIFT) & TERRAIN_TILE_MASK);
  uint16_t map = (uint16_t)(col + wram_r16(w, W_TILE_ROW_BASE + row));

  wram_w16(w, TERRAIN_DP_MAP, map);
  wram_w16(w, TERRAIN_DP_MAP_BANK, TERRAIN_MAP_BANK);

  out->x = row;

  // The loop runs the other way round from every other one here: a probe
  // *without* the bit is what ends it, and getting to the end is the answer.
  for (int i = 0; i < TERRAIN_PROBE_COUNT; i++) {
    uint16_t tile = 0;
    uint16_t attrs = probe_attrs(w, map, probe_offset(w, i), &tile);
    out->y = tile;
    out->a = (uint16_t)(attrs & TERRAIN_MASK_BIT12);
    if (out->a == 0) {
      PORT_COVER_IF(i < TERRAIN_PROBE_COLS, bit12_gap_upper, bit12_gap_lower);
      out->blocked = true;
      return;
    }
  }
  // $80:AFF5. All six carried it, and A is still `$1000` from the last `AND`.
  PORT_COVER(bit12_all);
  out->blocked = false;
}
