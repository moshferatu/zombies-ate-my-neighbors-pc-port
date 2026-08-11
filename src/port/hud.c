// $80:C07F, $80:C0A3, $80:C139 and the sixteen routines under them — see
// port/hud.h.

#include "port/hud.h"

#include "port/coverage.h"
#include "port/thread.h"

// `LDA $C5B6,X`, `LDA $C612,Y` and their four siblings are absolute indexed with
// a data bank of `$80`, which is what makes this file's two kinds of read look
// identical in the listing: under `$2000` the same instruction is the WRAM
// mirror and over `$8000` it is ROM. Which one a given site is never changes, so
// the port picks the accessor statically rather than dispatching on the address.
static uint16_t table_word(const Rom* rom, uint16_t base, uint16_t index) {
  return rom_word(rom, HUD_TABLE_BANK | (uint16_t)(base + index));
}

// Player 2's copy of any tilemap column is player 1's plus sixteen words. The
// ROM writes both out as constants; this is the relation between them, and the
// only place in the file where arithmetic on `side` produces an offset.
static uint16_t hud_column(uint16_t base, uint16_t side) {
  return (uint16_t)(base + side * (HUD_PANEL2_SHIFT / 2u));
}

// A tilemap cell: `STA $7E5F36,X` and the seven other bases in this file, all of
// which are `W_HUD_TILEMAP` plus a multiple of a row.
static void hud_put(Wram* w, uint16_t x, uint16_t row, uint16_t tile) {
  wram_w16(w, W_HUD_TILEMAP + (uint32_t)row * HUD_TILEMAP_ROW + x, tile);
}

// What a drawing routine leaves behind, threaded down the tree. Carry is an
// input as well as an output: several paths set it nowhere, and what the caller
// gets back is then what it arrived with. See the header.
//
// `work` rides along here for the same reason the flags do: every routine in the
// file already takes one of these, so a straight-line run can say it happened
// without a second parameter reaching sixteen functions deep.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
  HudWork* work;
} HudRegs;

// "This run of the listing executed." See `HudBlock`.
static void tally(HudRegs* r, HudBlock block) { r->work->blocks[block]++; }

static void hud_nz(HudRegs* r, uint16_t v) {
  r->a = v;
  r->n = (v & 0x8000u) != 0;
  r->z = v == 0;
}

// One `CMP`, whose carry the rest of the panel may well inherit.
static void hud_cmp(HudRegs* r, uint16_t v, uint16_t against) {
  const uint16_t d = (uint16_t)(v - against);
  r->a = v;
  r->n = (d & 0x8000u) != 0;
  r->z = d == 0;
  r->c = v >= against;
}

// ---------------------------------------------------------------------------
// $80:C4EC  hud_digit — one digit, with leading-zero suppression
// ---------------------------------------------------------------------------

// `digit` is the nibble the caller isolated; the ROM branches on the Z flag that
// `AND #$000F` left, which is the same test. `r->x` is the byte cursor and comes
// back advanced by two.
static void hud_digit(Wram* w, uint16_t dp, uint16_t digit, HudRegs* r) {
  const uint32_t lead_at = dp + HUD_DP_LEADING;
  const uint16_t lead = wram_r16(w, lead_at);

  uint16_t tile;
  if (digit != 0 || (lead & 0x8000u) != 0) {
    PORT_COVER(hud_digit_printed);
    // The two ways in are one branch apart: a nonzero digit leaves at the first
    // `BNE`, a zero has to go through the `BIT $1E` that finds something already
    // printed. Same tile, different price.
    tally(r, digit != 0 ? HUD_BLK_DIGIT_NONZERO : HUD_BLK_DIGIT_LEAD);
    // `CLC : ADC #$3C07`. A nibble plus `$3C07` cannot carry, so the `ADC` is
    // the one arithmetic instruction here that does *not* decide the carry.
    tile = (uint16_t)(digit + HUD_DIGIT_TILE);
    // `SEC : ROR $1E` — bit 15 in, bit 0 out into carry, and everything already
    // there slides down one. The stored word is the whole history of the
    // number, which is why it is rotated rather than set; see the header.
    r->c = (lead & 1u) != 0;
    wram_w16(w, lead_at, (uint16_t)(0x8000u | (lead >> 1)));
  } else {
    PORT_COVER(hud_digit_blank);
    tally(r, HUD_BLK_DIGIT_BLANK);
    // A leading zero: a blank tile, and nothing on this path touches carry.
    tile = 0;
  }

  hud_put(w, r->x, 0, tile);
  r->a = tile;
  r->x = (uint16_t)(r->x + 2u);
  // `INX : INX` is the last flag-setting instruction, so N and Z describe the
  // cursor and not the digit.
  r->n = (r->x & 0x8000u) != 0;
  r->z = r->x == 0;
}

// The coda both number renderers end on: if nothing printed, force a literal
// zero into the column the last digit went into — one word *below* the cursor,
// which is where `$7E:5F34,X` lands after the loop.
static void hud_digits_end(Wram* w, uint16_t dp, HudRegs* r) {
  const uint16_t tile = r->a;
  const uint16_t lead = wram_r16(w, dp + HUD_DP_LEADING);
  if ((lead & 0x8000u) != 0) {
    PORT_COVER(hud_digits_printed);
    tally(r, HUD_BLK_DIGITS_END_PRINTED);
    // `BIT $1E` and nothing after it: N is bit 15 of the operand and Z is the
    // *accumulator* ANDed with it, so the flags a caller sees here describe the
    // last tile and the rotate together rather than either one alone.
    r->n = true;
    r->z = (tile & lead) == 0;
    return;
  }
  PORT_COVER(hud_digits_all_zero);
  tally(r, HUD_BLK_DIGITS_END_ZERO);
  hud_nz(r, HUD_DIGIT_TILE);
  wram_w16(w, W_HUD_TILEMAP - 2u + r->x, HUD_DIGIT_TILE);
}

// ---------------------------------------------------------------------------
// $80:C519  hud_digits8 — the score
// ---------------------------------------------------------------------------

// Four bytes of BCD, top down, two digits each. `at` is the counter's address;
// the two dead instructions at `$80:C526` are not here because they leave
// nothing behind (see the header).
static void hud_digits8(Wram* w, uint16_t dp, uint16_t at, HudRegs* r) {
  tally(r, HUD_BLK_DIGITS8);

  // `TYA : CLC : ADC #$0003 : TAY`. The address plus three cannot carry out of
  // sixteen bits from any counter this game has, but it is the first thing to
  // touch carry and so it is written down rather than assumed.
  uint32_t sum = (uint32_t)at + 3u;
  uint16_t y = (uint16_t)sum;
  r->c = sum > 0xffffu;

  wram_w16(w, dp + HUD_DP_LEADING, 0);  // STZ $1E
  wram_w16(w, dp + HUD_DP_SCRATCH, 4);  // LDA #$0004 : STA $20

  for (uint16_t left = 4; left != 0; left--) {
    const uint16_t word = wram_r16(w, y);
    // `LSR` four times over: each one puts the bit it drops into carry, so what
    // survives into the digit below is bit 3 of the word — which belongs to the
    // *other* nibble.
    r->c = (word & 0x0008u) != 0;
    hud_digit(w, dp, (uint16_t)((word >> 4) & 0x000fu), r);
    // Re-read, because the ROM does. `hud_digit` cannot have written this word
    // — the tilemap is nowhere near it — but the reload is an instruction and
    // this is a transcription.
    hud_digit(w, dp, (uint16_t)(wram_r16(w, y) & 0x000fu), r);
    y = (uint16_t)(y - 1u);                                   // DEY
    wram_w16(w, dp + HUD_DP_SCRATCH, (uint16_t)(left - 1u));  // DEC $20
  }

  hud_digits_end(w, dp, r);
  r->y = y;
}

// ---------------------------------------------------------------------------
// $80:C553  hud_digits3 — an inventory count
// ---------------------------------------------------------------------------

// Three digits out of one word plus the byte above it. Y is only ever an index
// here, so it comes back as the address the adapter put there.
static void hud_digits3(Wram* w, uint16_t dp, uint16_t at, HudRegs* r) {
  tally(r, HUD_BLK_DIGITS3);
  wram_w16(w, dp + HUD_DP_LEADING, 0);

  // `LDA $0001,Y : AND #$000F` — the hundreds digit is the low nibble of the
  // counter's high byte, and the load that fetches it is a word read straddling
  // the byte above the counter.
  hud_digit(w, dp, (uint16_t)(wram_r16(w, (uint16_t)(at + 1u)) & 0x000fu), r);

  const uint16_t word = wram_r16(w, at);
  r->c = (word & 0x0008u) != 0;  // the four `LSR`s again
  hud_digit(w, dp, (uint16_t)((word >> 4) & 0x000fu), r);

  hud_digit(w, dp, (uint16_t)(wram_r16(w, at) & 0x000fu), r);

  hud_digits_end(w, dp, r);
  r->y = at;
}

// ---------------------------------------------------------------------------
// $80:C580  hud_blank — six tiles cleared
// ---------------------------------------------------------------------------

// Three columns on each of the first two rows. Nothing here touches carry or Y,
// so a caller that reaches this and nothing else comes back with both exactly as
// it arrived.
static void hud_blank(Wram* w, uint16_t x, HudRegs* r) {
  tally(r, HUD_BLK_BLANK);
  for (uint16_t row = 0; row < 2; row++)
    for (uint16_t i = 0; i < 3; i++) hud_put(w, (uint16_t)(x + i * 2u), row, 0);
  // `LDA #$0000` set these, six stores ago.
  hud_nz(r, 0);
  r->x = x;
}

// ---------------------------------------------------------------------------
// $80:C379  hud_bar — the health bar
// ---------------------------------------------------------------------------

// Ten tiles, five across and two deep, straight out of an eleven-row table with
// no bounds check. `attr` is the palette, which arrives through direct-page
// `$1E` — the same word `hud_digit` uses for its rotate, borrowed here for a
// completely different purpose because no number is being drawn at the time.
static void hud_bar(Wram* w, const Rom* rom, uint16_t dp, uint16_t health,
                    uint16_t x, uint16_t attr, HudRegs* r) {
  // `ASL : ASL : STA $20 : ASL : ASL : CLC : ADC $20` — health times twenty,
  // built out of shifts. All four shifts set carry and the `ADC` sets it again,
  // so this is where the routine's carry comes from.
  //
  // The `STA` is not scratch that stays private: `$20` is a live direct-page
  // word and health-times-four is still sitting in it when the routine returns,
  // on top of the side `$80:C59C` put there four instructions earlier. The
  // harness found this by diffing `$7E:0C20` on call 119.
  const uint16_t four = (uint16_t)(health * 4u);
  wram_w16(w, dp + HUD_DP_SCRATCH, four);
  const uint32_t sum = (uint32_t)(uint16_t)(health * 16u) + four;
  r->c = sum > 0xffffu;
  const uint16_t row = (uint16_t)sum;

  uint16_t tile = 0;
  for (int i = 0; i < HUD_BAR_TILES; i++) {
    tile = (uint16_t)(table_word(rom, HUD_BAR_TABLE, (uint16_t)(row + i * 2)) |
                      attr);
    hud_put(w, (uint16_t)(x + (i % 5) * 2u), (uint16_t)(i / 5), tile);
  }

  // The last `ORA $1E` is the last flag-setting instruction; X was never
  // touched, and Y is the row index the shifts produced.
  hud_nz(r, tile);
  r->x = x;
  r->y = row;
}

// ---------------------------------------------------------------------------
// $80:C59C  hud_health
// ---------------------------------------------------------------------------

static void hud_health(Wram* w, const Rom* rom, uint16_t dp, uint16_t side,
                       HudRegs* r) {
  // `$80:C59C` and the `$80:C379` it jumps into: ten tiles either way, so the
  // pair has one price rather than two.
  tally(r, HUD_BLK_HEALTH);
  wram_w16(w, dp + HUD_DP_SCRATCH, side);  // STA $20

  // The palette is chosen by score slot and the column by side — two different
  // lookups that agree on a stock boot. See the header.
  const uint16_t slot = wram_r16(w, (uint16_t)(W_SCORE_SLOT_SIDE + side));
  const uint16_t attr = table_word(rom, HUD_HEALTH_ATTR, slot);
  wram_w16(w, dp + HUD_DP_LEADING, attr);  // STA $1E

  // `LDX $C5BE,Y : LDA $0000,X` — the table holds the *address* of the health
  // word rather than an offset, which is the one place a per-player word in this
  // file is not reached as `base + side`.
  const uint16_t at = table_word(rom, HUD_HEALTH_AT, side);
  const uint16_t health = wram_r16(w, at);

  hud_bar(w, rom, dp, health, table_word(rom, HUD_HEALTH_COLUMN, side), attr, r);
}

// ---------------------------------------------------------------------------
// $80:C5C2 / $80:C666  the two icons
// ---------------------------------------------------------------------------

// Four consecutive tiles in a 2x2 block, from a table the score slot picks.
// `$80:C5C2` and `$80:C666` differ only in which three tables they read and in
// a "nothing selected" path that neither of them can reach — see the header —
// so they are one function here.
static void hud_icon(Wram* w, const Rom* rom, uint16_t dp, uint16_t side,
                     uint16_t column_table, uint16_t tile_table,
                     uint16_t selected_at, HudRegs* r) {
  wram_w16(w, dp + HUD_DP_SCRATCH, side);
  const uint16_t x = table_word(rom, column_table, side);

  const uint16_t slot = wram_r16(w, (uint16_t)(W_SCORE_SLOT_SIDE + side));
  const uint16_t base = table_word(rom, tile_table, slot);
  wram_w16(w, dp + HUD_DP_TABLE, base);  // STA $22

  // `LDA $1CBC,Y : BMI` — the branch the adapter above has already taken.
  const uint16_t sel = wram_r16(w, (uint16_t)(selected_at + side));

  // `ASL : CLC : ADC $22`, and both of them set carry.
  r->c = (sel & 0x8000u) != 0;
  const uint32_t sum = (uint32_t)(uint16_t)(sel * 2u) + base;
  r->c = sum > 0xffffu;
  const uint16_t y = (uint16_t)sum;

  const uint16_t tile = rom_word(rom, HUD_TABLE_BANK | y);
  hud_put(w, x, 0, tile);
  hud_put(w, (uint16_t)(x + 2u), 0, (uint16_t)(tile + 1u));
  hud_put(w, x, 1, (uint16_t)(tile + 2u));
  hud_put(w, (uint16_t)(x + 2u), 1, (uint16_t)(tile + 3u));

  // The third `INC` is the last flag-setting instruction, so N and Z belong to
  // the bottom-right tile rather than to the one the table held.
  hud_nz(r, (uint16_t)(tile + 3u));
  r->x = x;
  r->y = y;
}

// ---------------------------------------------------------------------------
// The twelve adapters, $80:C6E4 .. $80:C7AB
// ---------------------------------------------------------------------------

// `$80:C766` / `$80:C794` (weapon) and `$80:C77D` / `$80:C7AB` (item). The blank
// path clears the icon block *and* the count beside it, which is more than the
// count's own adapter would do.
static void hud_icon_adapter(Wram* w, const Rom* rom, uint16_t dp, uint16_t side,
                             uint16_t selected_at, uint16_t column_table,
                             uint16_t tile_table, uint16_t blank_column,
                             uint16_t count_column, bool weapon, HudRegs* r) {
  const uint16_t sel = wram_r16(w, (uint16_t)(selected_at + side));
  hud_nz(r, sel);
  if ((sel & 0x8000u) != 0) {
    if (weapon)
      PORT_COVER(hud_weapon_none);
    else
      PORT_COVER(hud_item_none);
    tally(r, HUD_BLK_ADAPT_ICON_NONE);
    hud_blank(w, hud_column(blank_column, side), r);
    hud_blank(w, hud_column(count_column, side), r);
    return;
  }
  if (weapon)
    PORT_COVER(hud_weapon_shown);
  else
    PORT_COVER(hud_item_shown);
  tally(r, HUD_BLK_ADAPT_ICON_SHOWN);
  // The two icon routines are one function above and two prices here: the
  // weapon's `$80:C5C2` reloads the side at `$80:C5C8` and the item's `$80:C666`
  // does not, which is one `LDY $20` and 28 cycles.
  tally(r, weapon ? HUD_BLK_ICON_WEAPON : HUD_BLK_ICON_ITEM);
  hud_icon(w, rom, dp, side, column_table, tile_table, selected_at, r);
}

// `$80:C702` / `$80:C734` (weapon) and `$80:C71B` / `$80:C74D` (item). Two ways
// to refuse — nothing selected, or a slot past the end of the inventory — and
// both of them blank the three columns rather than leaving them.
static void hud_count_adapter(Wram* w, uint16_t dp, uint16_t side,
                              uint16_t selected_at, uint16_t counts_at,
                              uint16_t slots, uint16_t column, HudRegs* r) {
  // `LDX #$0090` comes before the load, so the cursor is set even on the paths
  // that never draw a digit.
  r->x = hud_column(column, side);

  const uint16_t sel = wram_r16(w, (uint16_t)(selected_at + side));
  hud_nz(r, sel);
  if ((sel & 0x8000u) != 0) {
    PORT_COVER(hud_count_none);
    tally(r, HUD_BLK_ADAPT_COUNT_NONE);
    hud_blank(w, r->x, r);
    return;
  }
  hud_cmp(r, sel, slots);
  if (sel >= slots) {
    PORT_COVER(hud_count_over);
    tally(r, HUD_BLK_ADAPT_COUNT_OVER);
    hud_blank(w, r->x, r);
    return;
  }
  PORT_COVER(hud_count_shown);
  tally(r, HUD_BLK_ADAPT_COUNT_SHOWN);

  r->c = (sel & 0x8000u) != 0;  // the `ASL`, which cannot carry by now
  const uint32_t sum =
      (uint32_t)(uint16_t)(sel * 2u) + hud_column(counts_at, side);
  r->c = sum > 0xffffu;
  hud_digits3(w, dp, (uint16_t)sum, r);
}

// ---------------------------------------------------------------------------
// $80:C0A3 / $80:C139  hud_panel
// ---------------------------------------------------------------------------

// `INC $1E7A`, which is an absolute increment and so leaves A, X and Y alone.
static void hud_dirty(Wram* w, HudRegs* r) {
  const uint16_t n = (uint16_t)(wram_r16(w, W_HUD_DIRTY) + 1u);
  wram_w16(w, W_HUD_DIRTY, n);
  r->n = (n & 0x8000u) != 0;
  r->z = n == 0;
}

// The inventory word a selection points at. `sel` can be `$FFFF`, and then the
// ROM's `LDA $1D0C,X` indexes with `$FFFE` — which on hardware carries into the
// next bank and lands right back in the WRAM mirror, so a 16-bit wrap reads the
// same address the hardware does.
static uint16_t hud_count_word(const Wram* w, uint16_t base, uint16_t side,
                               uint16_t sel) {
  return wram_r16(w, (uint16_t)(hud_column(base, side) + (uint16_t)(sel * 2u)));
}

void hud_panel(Wram* w, const Rom* rom, uint16_t dp, uint16_t side,
               HudPanelRegs* io) {
  HudRegs r = {.a = io->a, .x = io->x, .y = io->y,
               .n = io->n, .z = io->z, .c = io->c, .work = io->work};

  // `LDA $1E88 / $1E8A : BNE : RTS` — the panel a player who is not in the game
  // does not have.
  const uint16_t on = wram_r16(w, (uint16_t)(W_HUD_PANEL_ON + side));
  hud_nz(&r, on);
  if (on == 0) {
    PORT_COVER(hud_panel_off);
    tally(&r, HUD_BLK_PANEL_OFF);
    io->a = r.a;  // X, Y and carry are untouched on this path
    io->n = r.n;
    io->z = r.z;
    return;
  }
  PORT_COVER(hud_panel_on);
  tally(&r, HUD_BLK_PANEL_ON);

  // 1. Health.
  uint16_t live = wram_r16(w, (uint16_t)(W_PLAYER_HEALTH + side));
  uint16_t shadow = wram_r16(w, (uint16_t)(W_HUD_SHADOW_HEALTH + side));
  hud_cmp(&r, live, shadow);
  PORT_COVER_IF(live != shadow, hud_health_changed, hud_health_same);
  tally(&r, live != shadow ? HUD_BLK_FIELD_CHANGED : HUD_BLK_FIELD_SAME);
  if (live != shadow) {
    wram_w16(w, (uint16_t)(W_HUD_SHADOW_HEALTH + side), live);
    tally(&r, HUD_BLK_ADAPT_HEALTH);
    hud_health(w, rom, dp, side, &r);
    hud_dirty(w, &r);
  }

  // 2. The item icon.
  live = wram_r16(w, (uint16_t)(W_PLAYER_ITEM + side));
  shadow = wram_r16(w, (uint16_t)(W_HUD_SHADOW_ITEM + side));
  hud_cmp(&r, live, shadow);
  PORT_COVER_IF(live != shadow, hud_item_changed, hud_item_same);
  tally(&r, live != shadow ? HUD_BLK_FIELD_CHANGED : HUD_BLK_FIELD_SAME);
  if (live != shadow) {
    wram_w16(w, (uint16_t)(W_HUD_SHADOW_ITEM + side), live);
    hud_icon_adapter(w, rom, dp, side, W_PLAYER_ITEM, HUD_ITEM_ICON_COLUMN,
                     HUD_ITEM_ICON_TABLE, HUD_ITEM_ICON_BLANK_COLUMN,
                     HUD_ITEM_COUNT_COLUMN, false, &r);
    hud_dirty(w, &r);
  }

  // 3. The score, which is the only 32-bit test and the only one that draws
  //    before it updates its shadows.
  const uint16_t score_at = (uint16_t)(W_PLAYER_SCORE + side * 2u);
  const uint16_t score_shadow = (uint16_t)(W_HUD_SHADOW_SCORE + side * 2u);
  hud_cmp(&r, wram_r16(w, score_at), wram_r16(w, score_shadow));
  bool changed = !r.z;
  if (changed) tally(&r, HUD_BLK_SCORE_LOW);
  if (!changed) {
    // The low halves matched, so the high halves decide — and this is the `CMP`
    // whose flags survive whenever the score has not moved at all, which is
    // most of the time.
    hud_cmp(&r, wram_r16(w, (uint16_t)(score_at + 2u)),
            wram_r16(w, (uint16_t)(score_shadow + 2u)));
    changed = !r.z;
    if (changed) PORT_COVER(hud_score_high_only);
    tally(&r, changed ? HUD_BLK_SCORE_HIGH : HUD_BLK_SCORE_SAME);
  }
  PORT_COVER_IF(changed, hud_score_changed, hud_score_same);
  if (changed) {
    tally(&r, HUD_BLK_SCORE_TAIL);
    tally(&r, HUD_BLK_ADAPT_SCORE);
    // `JSR $C6E4`: `LDY #$1E72 : LDX #$007E : JMP $C519`.
    r.x = hud_column(HUD_SCORE_COLUMN, side);
    hud_digits8(w, dp, score_at, &r);
    wram_w16(w, score_shadow, wram_r16(w, score_at));
    const uint16_t hi = wram_r16(w, (uint16_t)(score_at + 2u));
    wram_w16(w, (uint16_t)(score_shadow + 2u), hi);
    r.a = hi;  // the last `LDA` before the `INC`, which does not touch A
    hud_dirty(w, &r);
  }

  // 4. The item count. The index is computed whether or not anything changed,
  //    so X is written on every pass through a live panel.
  uint16_t sel = wram_r16(w, (uint16_t)(W_PLAYER_ITEM + side));
  r.c = (sel & 0x8000u) != 0;  // the `ASL` before the `TAX`
  r.x = (uint16_t)(sel * 2u);
  live = hud_count_word(w, W_PLAYER_ITEMS, side, sel);
  shadow = wram_r16(w, (uint16_t)(W_HUD_SHADOW_ITEM_COUNT + side));
  hud_cmp(&r, live, shadow);
  PORT_COVER_IF(live != shadow, hud_item_count_changed, hud_item_count_same);
  tally(&r, live != shadow ? HUD_BLK_COUNT_CHANGED : HUD_BLK_COUNT_SAME);
  if (live != shadow) {
    wram_w16(w, (uint16_t)(W_HUD_SHADOW_ITEM_COUNT + side), live);
    hud_count_adapter(w, dp, side, W_PLAYER_ITEM, W_PLAYER_ITEMS,
                      W_PLAYER_ITEMS_SLOTS, HUD_ITEM_COUNT_COLUMN, &r);
    hud_dirty(w, &r);
  }

  // 5. The weapon icon.
  live = wram_r16(w, (uint16_t)(W_PLAYER_WEAPON + side));
  shadow = wram_r16(w, (uint16_t)(W_HUD_SHADOW_WEAPON + side));
  hud_cmp(&r, live, shadow);
  PORT_COVER_IF(live != shadow, hud_weapon_changed, hud_weapon_same);
  tally(&r, live != shadow ? HUD_BLK_FIELD_CHANGED : HUD_BLK_FIELD_SAME);
  if (live != shadow) {
    wram_w16(w, (uint16_t)(W_HUD_SHADOW_WEAPON + side), live);
    hud_icon_adapter(w, rom, dp, side, W_PLAYER_WEAPON, HUD_WEAPON_ICON_COLUMN,
                     HUD_WEAPON_ICON_TABLE, HUD_WEAPON_ICON_BLANK_COLUMN,
                     HUD_WEAPON_COUNT_COLUMN, true, &r);
    hud_dirty(w, &r);
  }

  // 6. The weapon count.
  sel = wram_r16(w, (uint16_t)(W_PLAYER_WEAPON + side));
  r.c = (sel & 0x8000u) != 0;
  r.x = (uint16_t)(sel * 2u);
  live = hud_count_word(w, W_PLAYER_INVENTORY, side, sel);
  shadow = wram_r16(w, (uint16_t)(W_HUD_SHADOW_WEAPON_COUNT + side));
  hud_cmp(&r, live, shadow);
  PORT_COVER_IF(live != shadow, hud_weapon_count_changed, hud_weapon_count_same);
  tally(&r, live != shadow ? HUD_BLK_COUNT_CHANGED : HUD_BLK_COUNT_SAME);
  if (live != shadow) {
    wram_w16(w, (uint16_t)(W_HUD_SHADOW_WEAPON_COUNT + side), live);
    hud_count_adapter(w, dp, side, W_PLAYER_WEAPON, W_PLAYER_INVENTORY,
                      W_PLAYER_INVENTORY_SLOTS, HUD_WEAPON_COUNT_COLUMN, &r);
    hud_dirty(w, &r);
  }

  tally(&r, HUD_BLK_PANEL_RTS);
  io->a = r.a;
  io->x = r.x;
  io->y = r.y;
  io->n = r.n;
  io->z = r.z;
  io->c = r.c;
}

// ---------------------------------------------------------------------------
// $80:C07F  hud_refresh
// ---------------------------------------------------------------------------

void hud_refresh(Wram* w, const Rom* rom, uint16_t dp, HudRefreshRegs* io) {
  // `LDA $24 : EOR #$0001 : STA $24 : BNE`. The whole word decides, not the bit
  // the `EOR` flipped — so a `$24` that ever held anything but 0 or 1 would stop
  // alternating and refresh player 2 forever. Nothing puts anything else there;
  // the port branches on the word regardless, because the ROM does.
  const uint16_t phase =
      (uint16_t)(wram_r16(w, (uint16_t)(dp + HUD_DP_PHASE)) ^ 1u);
  wram_w16(w, (uint16_t)(dp + HUD_DP_PHASE), phase);
  PORT_COVER_IF(phase == 0, hud_phase_p1, hud_phase_p2);
  io->work->blocks[phase == 0 ? HUD_BLK_REFRESH_P1 : HUD_BLK_REFRESH_P2]++;

  HudPanelRegs p = {.a = phase,
                    .x = io->x,
                    .y = io->y,
                    .n = (phase & 0x8000u) != 0,
                    .z = phase == 0,
                    .c = io->c,
                    .work = io->work};
  hud_panel(w, rom, dp, phase == 0 ? HUD_SIDE_P1 : HUD_SIDE_P2, &p);

  // `LDA $1E7A : BEQ $C0A2` — a count of how many of the six tests fired, read
  // only as a flag.
  const uint16_t dirty = wram_r16(w, W_HUD_DIRTY);
  io->x = p.x;
  io->y = p.y;
  io->c = p.c;
  if (dirty == 0) {
    PORT_COVER(hud_upload_idle);
    io->work->blocks[HUD_BLK_REFRESH_IDLE]++;
    // The `LDA` that found zero is the last thing to have set anything, and it
    // does not touch carry — so what comes back is whatever the panel left.
    io->a = 0;
    io->n = false;
    io->z = true;
    return;
  }
  PORT_COVER(hud_upload_queued);
  io->work->blocks[HUD_BLK_REFRESH_QUEUE]++;

  wram_w16(w, W_HUD_DIRTY, 0);
  // `LDA #$C34A : LDY #$0080 : JML $8083AE` — a tail jump, so from here on the
  // registers are the queue adder's and not this routine's. Its flags come from
  // `port/thread.h` rather than being written out a second time; they are the
  // same three `shim_vbl_queue_a_add` publishes, from the same function.
  const int slot = vbl_queue_a_add(w, HUD_UPLOAD_JOB, HUD_UPLOAD_JOB_BANK);
  VblQueueFlags f;
  vbl_queue_flags(w, W_VBL_QUEUE_A_COUNT, W_VBL_QUEUE_A_SLOTS, slot >= 0, &f);
  // The tail jump's cost is this call's, so the queue adder's search is priced
  // here too. `slot` is the ROM's X, counted down from `$0038` in fours: a zero
  // means the scan found nothing free and took slot 0 anyway, which is a
  // different straight line from stopping on a free one.
  if (slot < 0) {
    io->work->blocks[HUD_BLK_QUEUE_FULL]++;
  } else if (slot == 0) {
    io->work->blocks[HUD_BLK_QUEUE_FELL]++;
  } else {
    io->work->blocks[HUD_BLK_QUEUE_ACCEPTED]++;
    io->work->blocks[HUD_BLK_QUEUE_BUSY] +=
        (uint16_t)((0x38 - slot) / 4);
  }

  if (slot < 0) {
    PORT_COVER(hud_upload_refused);
    // `PLY : RTL` puts everything back, so A and Y are the two constants above
    // and X is the panel's.
    io->a = HUD_UPLOAD_JOB;
    io->y = HUD_UPLOAD_JOB_BANK;
  } else {
    PORT_COVER(hud_upload_accepted);
    // `DEC A : TAY` leaves the stored address in both A and Y, and X is the
    // slot the search stopped on.
    io->a = (uint16_t)(HUD_UPLOAD_JOB - 1u);
    io->y = io->a;
    io->x = (uint16_t)slot;
  }
  io->n = f.n;
  io->z = f.z;
  io->c = f.c;
}
