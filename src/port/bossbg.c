#include "port/bossbg.h"

#include "port/thread.h"

// A word read through a 24-bit pointer the ROM built at run time. The two
// cases are the two halves of the SNES's map that can hold a stored figure:
// WRAM, where the bank's low bit picks which 64 KB of `Wram` it is, and the
// cartridge. Nothing else can appear here -- the guards below decline the call
// before it starts if the pointer is anything but one of those two -- so there
// is no third branch and no fallback value.
static uint16_t figure_word(const Wram* w, const Rom* rom, uint16_t bank,
                            uint16_t addr) {
  if ((bank & 0xfffeu) == 0x007eu) {
    return wram_r16(w, ((uint32_t)(bank & 1u) << 16) | addr);
  }
  return rom_word(rom, ((uint32_t)bank << 16) | addr);
}

// The header, read the same way whichever routine is asking. The guards need
// it before the routine has run, so it is its own function.
static void figure_shape(const Wram* w, const Rom* rom, uint16_t* out_src,
                         uint16_t* out_bank, uint16_t* out_row_bytes,
                         uint16_t* out_rows) {
  uint16_t src = wram_r16(w, BOSS_BG_DP_SRC);
  uint16_t bank = wram_r16(w, BOSS_BG_DP_SRC_BANK);
  *out_src = src;
  *out_bank = bank;
  *out_row_bytes = (uint16_t)(figure_word(w, rom, bank, src) << 1);
  *out_rows = figure_word(w, rom, bank, (uint16_t)(src + 2u));
}

// WRAM wraps inside its bank and so does the port's `wram_r16`, so a figure
// stored there needs nothing checked. A cartridge one does: `rom_has` stops at
// the bank wrap, which is the same place the ROM's own `[$28],Y` would stop
// meaning what the routine thinks it means.
static bool figure_readable(const Wram* w, const Rom* rom, bool whole) {
  uint16_t src, bank, row_bytes, rows;
  figure_shape(w, rom, &src, &bank, &row_bytes, &rows);
  if ((bank & 0xfffeu) == 0x007eu) return true;
  uint32_t bytes = BOSS_BG_HEADER_BYTES;
  if (whole) bytes += (uint32_t)row_bytes * rows;
  return rom_has(rom, ((uint32_t)bank << 16) | src, bytes);
}

// The four parallel arrays, written together. `at` is a byte offset that the
// ROM keeps already doubled, in `$1D54`, and hands to every one of them as an
// index -- which is why the queue holds 32 entries and not 64: `$1D56` and
// `$1D96` are `$40` bytes apart.
static void queue_append(Wram* w, uint16_t at, uint16_t src, uint16_t bank,
                         uint16_t len, uint16_t dest) {
  wram_w16(w, (uint16_t)(W_BG_DMA_SRC + at), src);
  wram_w16(w, (uint16_t)(W_BG_DMA_BANK + at), bank);
  wram_w16(w, (uint16_t)(W_BG_DMA_LEN + at), len);
  wram_w16(w, (uint16_t)(W_BG_DMA_DEST + at), dest);
}

// The four instructions both routines end on. `vbl_queue_a_add` takes the
// address and bank in A and Y and returns the slot it took, or -1; the ROM's
// version leaves the address it stored -- which is one less than the one it was
// given -- in both A and Y, the slot in X, and the carry from its own
// `CPY #$0010`, clear when there was room. On the full path nothing is stored,
// A keeps the address it went in with, the `PLY` puts the bank back in Y, and
// X is whatever the caller left there.
static void register_vbl_job(Wram* w, uint16_t x_on_full, BossBgRegs* out) {
  int slot = vbl_queue_a_add(w, BOSS_BG_VBL_JOB_ADDR, BOSS_BG_VBL_JOB_BANK);
  if (slot < 0) {
    out->a = BOSS_BG_VBL_JOB_ADDR;
    out->x = x_on_full;
    out->y = BOSS_BG_VBL_JOB_BANK;
    out->c = true;
    return;
  }
  out->a = BOSS_BG_VBL_JOB_ADDR - 1u;
  out->x = (uint16_t)slot;
  out->y = BOSS_BG_VBL_JOB_ADDR - 1u;
  out->c = false;
}

bool boss_bg_queue_supported(const Wram* w, const Rom* rom) {
  return figure_readable(w, rom, false);
}

void boss_bg_queue(Wram* w, const Rom* rom, BossBgRegs* out) {
  uint16_t src, bank, row_bytes, rows;
  figure_shape(w, rom, &src, &bank, &row_bytes, &rows);

  wram_w16(w, BOSS_BG_DP_ROW_BYTES, row_bytes);
  wram_w16(w, BOSS_BG_DP_ROWS, rows);
  src = (uint16_t)(src + BOSS_BG_HEADER_BYTES);
  wram_w16(w, BOSS_BG_DP_SRC, src);

  uint16_t dest = BOSS_BG_VRAM_BASE;
  wram_w16(w, BOSS_BG_DP_DEST, dest);

  uint16_t at = wram_r16(w, W_BG_DMA_CURSOR);
  do {
    queue_append(w, at, src, bank, row_bytes, dest);
    src = (uint16_t)(src + row_bytes);
    wram_w16(w, BOSS_BG_DP_SRC, src);
    dest = (uint16_t)(dest + BOSS_BG_VRAM_ROW);
    wram_w16(w, BOSS_BG_DP_DEST, dest);
    at = (uint16_t)(at + 2u);
    rows = (uint16_t)(rows - 1u);
    wram_w16(w, BOSS_BG_DP_ROWS, rows);
  } while (rows != 0);
  wram_w16(w, W_BG_DMA_CURSOR, at);

  register_vbl_job(w, at, out);
}

bool boss_bg_queue_flip_supported(const Wram* w, const Rom* rom) {
  return figure_readable(w, rom, true);
}

void boss_bg_queue_flip(Wram* w, const Rom* rom, BossBgRegs* out) {
  uint16_t src, bank, row_bytes, rows;
  figure_shape(w, rom, &src, &bank, &row_bytes, &rows);

  wram_w16(w, BOSS_BG_DP_ROW_BYTES, row_bytes);
  wram_w16(w, BOSS_BG_DP_ROWS, rows);
  src = (uint16_t)(src + BOSS_BG_HEADER_BYTES);
  wram_w16(w, BOSS_BG_DP_SRC, src);

  uint16_t stage = BOSS_BG_STAGE_ADDR;
  wram_w16(w, BOSS_BG_DP_STAGE, stage);
  wram_w16(w, BOSS_BG_DP_STAGE_BANK, BOSS_BG_STAGE_BANK);
  uint16_t dest = BOSS_BG_VRAM_BASE;
  wram_w16(w, BOSS_BG_DP_FLIP_DEST, dest);

  uint16_t at = wram_r16(w, W_BG_DMA_CURSOR);
  do {
    // `LDY $38 : DEY : DEY` and then straight into the body, so the first word
    // copied is the row's last and the inner loop is a do-while as well: a
    // zero-width figure would enter it with Y at `$FFFE` and copy the two
    // bytes before the row. No stored figure has one.
    int16_t y = (int16_t)(row_bytes - 2u);
    do {
      uint16_t tile = figure_word(w, rom, bank, (uint16_t)(src + (uint16_t)y));
      wram_w16(w, ((uint32_t)(BOSS_BG_STAGE_BANK & 1u) << 16) | stage,
               (uint16_t)(tile ^ BOSS_BG_TILE_FLIP_X));
      stage = (uint16_t)(stage + 2u);  // two `INC $2C`, so no carry into $2E
      wram_w16(w, BOSS_BG_DP_STAGE, stage);
      y = (int16_t)(y - 2);
    } while (y >= 0);

    src = (uint16_t)(src + row_bytes);
    wram_w16(w, BOSS_BG_DP_SRC, src);
    queue_append(w, at, (uint16_t)(stage - row_bytes), BOSS_BG_STAGE_BANK,
                 row_bytes, dest);
    dest = (uint16_t)(dest + BOSS_BG_VRAM_ROW);
    wram_w16(w, BOSS_BG_DP_FLIP_DEST, dest);
    at = (uint16_t)(at + 2u);
    rows = (uint16_t)(rows - 1u);
    wram_w16(w, BOSS_BG_DP_ROWS, rows);
  } while (rows != 0);
  wram_w16(w, W_BG_DMA_CURSOR, at);

  register_vbl_job(w, at, out);
}
