// DMA to the picture hardware, and three vblank jobs -- see port/dma.h.

#include "port/dma.h"

#include "port/coverage.h"
#include "port/thread.h"

// The registers.
#define REG_BG3SC 0x2109u      // where the third layer's map is
#define REG_BG34NBA 0x210cu    // ...and its tiles
#define REG_VMAIN 0x2115u      // how VRAM's address steps
#define REG_VMADD 0x2116u      // VRAM's address, in words
#define REG_CGADD 0x2121u      // which colour
#define REG_MDMAEN 0x420bu     // start the channels whose bits are set
#define REG_DMA_MODE 0x4300u   // channel 0: how it writes
#define REG_DMA_DEST 0x4301u   // ...to which register of `$21xx`
#define REG_DMA_SOURCE 0x4302u
#define REG_DMA_BANK 0x4304u
#define REG_DMA_BYTES 0x4305u

#define DEST_VRAM 0x18u    // `$2118`
#define DEST_CGRAM 0x22u   // `$2122`
#define MODE_ONE_REG 0x00u   // every byte to the one register
#define MODE_TWO_REGS 0x01u  // low and high bytes to it and the next
#define MODE_FIXED_TWO_REGS 0x09u  // ...from a source that stays put
#define VMAIN_STEP_ON_HIGH 0x80u

static void store8(HwTrace* t, int run, uint16_t reg, uint8_t v) {
  hw_run(t, run);
  hw_w8(t, reg, v);
}

static void store16(HwTrace* t, int run, uint16_t reg, uint16_t v) {
  hw_run(t, run);
  hw_w16(t, reg, v);
}

void dma_to_cgram(HwTrace* t, uint8_t bank, uint16_t at, uint16_t bytes) {
  PORT_COVER(dma_to_cgram);
  store8(t, DC_HEAD, REG_DMA_BANK, bank);
  store16(t, DMA_STORE, REG_DMA_SOURCE, at);
  store16(t, DMA_STORE, REG_DMA_BYTES, bytes);
  store8(t, DMA_STORE, REG_CGADD, 0);
  store8(t, DMA_IMM, REG_DMA_DEST, DEST_CGRAM);
  store8(t, DMA_STORE, REG_DMA_MODE, MODE_ONE_REG);
  store8(t, DMA_IMM, REG_MDMAEN, 0x01);
  hw_run(t, DMA_RTL);
}

void dma_to_cgram_at(HwTrace* t, uint8_t bank, uint16_t at, uint16_t bytes,
                     uint8_t first) {
  PORT_COVER(dma_to_cgram_at);
  store8(t, DC_HEAD, REG_DMA_BANK, bank);
  store8(t, DCA_FIRST, REG_CGADD, first);
  store16(t, DMA_STORE, REG_DMA_SOURCE, at);
  store16(t, DMA_STORE, REG_DMA_BYTES, bytes);
  store8(t, DMA_IMM, REG_DMA_DEST, DEST_CGRAM);
  store8(t, DMA_STORE, REG_DMA_MODE, MODE_ONE_REG);
  store8(t, DMA_IMM, REG_MDMAEN, 0x01);
  hw_run(t, DMA_RTL);
}

void dma_to_vram(HwTrace* t, uint8_t bank, uint16_t at, uint16_t vram,
                 uint16_t bytes) {
  PORT_COVER(dma_to_vram);
  store16(t, DV_HEAD, REG_DMA_SOURCE, at);
  store8(t, DV_BANK, REG_DMA_BANK, bank);
  store16(t, DMA_STORE, REG_VMADD, vram);
  store16(t, DMA_STORE, REG_DMA_BYTES, bytes);
  store8(t, DMA_IMM, REG_DMA_DEST, DEST_VRAM);
  store8(t, DMA_IMM, REG_DMA_MODE, MODE_TWO_REGS);
  store8(t, DMA_IMM, REG_MDMAEN, 0x01);
  hw_run(t, DMA_RTL);
}

void palette_job(HwTrace* t) {
  PORT_COVER(palette_job);
  hw_run(t, PJ_HEAD);
  dma_to_cgram(t, PALETTE_BANK, PALETTE_AT, PALETTE_BYTES);
  hw_run(t, PJ_TAIL);
}

void background_job(HwTrace* t) {
  PORT_COVER(background_job);
  hw_run(t, PJ_HEAD);
  dma_to_cgram(t, PALETTE_BANK, PALETTE_SHOWN_AT, PALETTE_SHOWN_BYTES);
  hw_run(t, PJ_TAIL);
}

// --- Three jobs that fill in the channel themselves --------------------------

void hud_tiles_job(HwTrace* t) {
  PORT_COVER(hud_tiles_job);
  store16(t, DMA_IMM16, REG_DMA_MODE, (DEST_VRAM << 8) | MODE_TWO_REGS);
  store16(t, DMA_IMM16, REG_DMA_SOURCE, HUD_TILES_AT);
  store16(t, DMA_IMM16, REG_DMA_BANK, HUD_TILES_BANK);
  store16(t, DMA_IMM16, REG_DMA_BYTES, HUD_TILES_BYTES);
  store16(t, DMA_IMM16, REG_VMADD, HUD_TILES_VRAM);
  store8(t, DMA_SEP_IMM, REG_VMAIN, VMAIN_STEP_ON_HIGH);
  store8(t, DMA_IMM, REG_MDMAEN, 0x01);
  // `REP #$20 : REP #$21 : RTL`, which costs what the other's tail does.
  hw_run(t, HJ_TAIL);
}

void hud_layer_set(HwTrace* t) {
  PORT_COVER(hud_layer_set);
  store8(t, DMA_SEP_IMM, REG_BG3SC, HUD_LAYER_MAP);
  store8(t, DMA_IMM, REG_BG34NBA, HUD_LAYER_TILES);
  hw_run(t, DMA_REP);
}

void hud_upload_job(HwTrace* t) {
  PORT_COVER(hud_upload_job);
  store16(t, DMA_IMM16, REG_DMA_MODE, (DEST_VRAM << 8) | MODE_TWO_REGS);
  store16(t, DMA_IMM16, REG_DMA_SOURCE, HUD_SHADOW_AT);
  store16(t, DMA_IMM16, REG_DMA_BANK, PALETTE_BANK);
  store16(t, DMA_IMM16, REG_DMA_BYTES, HUD_SHADOW_BYTES);
  store16(t, DMA_IMM16, REG_VMADD, HUD_VRAM_AT);
  store8(t, DMA_SEP_IMM, REG_VMAIN, VMAIN_STEP_ON_HIGH);
  store8(t, DMA_IMM, REG_MDMAEN, 0x01);
  hw_run(t, HJ_TAIL);
}

uint16_t text_map_job(const Wram* w, HwTrace* t) {
  PORT_COVER(text_map_job);
  // `ASL : ASL : XBA`: the page times 1,024.
  const uint16_t page = (uint16_t)(wram_r16(w, W_TEXT_MAP_JOB_PAGE) << 2);
  const uint16_t vram =
      (uint16_t)((uint16_t)(page << 8 | page >> 8) + TEXT_MAP_JOB_VRAM);
  hw_run(t, TMJ_HEAD);
  dma_to_vram(t, PALETTE_BANK, TEXT_MAP_JOB_AT, vram, TEXT_MAP_JOB_BYTES);
  hw_run(t, TMJ_TAIL);
  return vram;
}

void colours_112_job(HwTrace* t) {
  PORT_COVER(colours_112_job);
  store8(t, DMA_SEP_IMM, REG_CGADD, COLOURS_112_FIRST);
  store8(t, DMA_STORE, REG_MDMAEN, 0x00);
  store16(t, DMA_REP_IMM16, REG_DMA_MODE, (DEST_CGRAM << 8) | MODE_ONE_REG);
  store16(t, DMA_IMM16, REG_DMA_SOURCE, COLOURS_112_AT);
  store16(t, DMA_IMM16, REG_DMA_BANK, PALETTE_BANK);
  store16(t, DMA_IMM16, REG_DMA_BYTES, COLOURS_112_BYTES);
  store8(t, DMA_SEP_IMM, REG_MDMAEN, 0x01);
  hw_run(t, DMA_REP_RTL);
}

void vram_wipe(HwTrace* t) {
  PORT_COVER(vram_wiped);
  store16(t, DMA_IMM16, REG_VMADD, VRAM_WIPE_AT);
  store16(t, DMA_IMM16, REG_DMA_MODE, (DEST_VRAM << 8) | MODE_FIXED_TWO_REGS);
  store16(t, DMA_IMM16, REG_DMA_SOURCE, VRAM_WIPE_SOURCE);
  store16(t, DMA_IMM16, REG_DMA_BANK, VRAM_CLEAR_SOURCE_BANK);
  store16(t, DMA_IMM16, REG_DMA_BYTES, VRAM_WIPE_BYTES);
  store8(t, DMA_SEP_IMM, REG_VMAIN, VMAIN_STEP_ON_HIGH);
  store8(t, DMA_IMM, REG_MDMAEN, 0x01);
  hw_run(t, DMA_RTL);
}

// One of `colours_job`'s two transfers: `bytes` from `at` to the colours
// from `first`. The first begins with a `SEP` the second does not need.
static void send_colours(HwTrace* t, int first_run, uint8_t first, uint16_t at,
                         uint16_t bytes) {
  store8(t, first_run, REG_CGADD, first);
  store8(t, DMA_STORE, REG_MDMAEN, 0x00);
  store16(t, DMA_REP_IMM16, REG_DMA_MODE, (DEST_CGRAM << 8) | MODE_ONE_REG);
  store16(t, DMA_IMM16, REG_DMA_SOURCE, at);
  store16(t, DMA_IMM16, REG_DMA_BANK, PALETTE_BANK);
  store16(t, DMA_IMM16, REG_DMA_BYTES, bytes);
  store8(t, DMA_SEP_IMM, REG_MDMAEN, 0x01);
}

void colours_job(HwTrace* t) {
  PORT_COVER(colours_job);
  send_colours(t, DMA_SEP_IMM, 0, PALETTE_AT, COLOURS_LOW_BYTES);
  send_colours(t, DMA_IMM, COLOURS_HIGH_FIRST,
               PALETTE_AT + 2 * COLOURS_HIGH_FIRST, COLOURS_HIGH_BYTES);
  hw_run(t, DMA_REP_RTL);
}

int colours_112_ask(Wram* w) {
  PORT_COVER(colours_112_asked);
  return vbl_queue_a_add(w, COLOURS_112_JOB_PC & 0xffffu,
                         COLOURS_112_JOB_PC >> 16);
}

uint16_t vram_clear_job(Wram* w, HwTrace* t) {
  const uint16_t at = wram_r16(w, W_VRAM_CLEAR_AT);
  const uint16_t next = (uint16_t)(at + VRAM_CLEAR_WORDS);
  store16(t, VC_HEAD, REG_VMADD, at);
  wram_w16(w, W_VRAM_CLEAR_AT, next);
  // Mode 9: both bytes of a word, from an address that does not move.
  store16(t, VC_STEP, REG_DMA_MODE, (DEST_VRAM << 8) | MODE_FIXED_TWO_REGS);
  store16(t, DMA_IMM16, REG_DMA_SOURCE, VRAM_CLEAR_SOURCE);
  store16(t, DMA_IMM16, REG_DMA_BANK, VRAM_CLEAR_SOURCE_BANK);
  store16(t, DMA_IMM16, REG_DMA_BYTES, VRAM_CLEAR_BYTES);
  store8(t, DMA_SEP_IMM, REG_VMAIN, VMAIN_STEP_ON_HIGH);
  store8(t, DMA_IMM, REG_MDMAEN, 0x01);
  hw_run(t, VC_TEST);
  if (next & 0x8000u) {
    PORT_COVER(vram_clear_done);
    hw_run(t, DMA_TAKEN);
  } else {
    PORT_COVER(vram_clear_more);
  }
  hw_run(t, DMA_FLAG_RTL);
  return next;
}

// --- $82:D88C  the animated tiles ---------------------------------------------

// A 24-bit pointer on page zero, and `[dp],Y` through it.
static uint32_t far_pointer(const Wram* w, uint16_t dp, uint16_t y) {
  return (((uint32_t)wram_r8(w, (uint16_t)(dp + 2)) << 16) | wram_r16(w, dp)) +
         y;
}

static bool in_wram(uint32_t far) {
  return far >= 0x7e0000u && far + 1 <= 0x7fffffu;
}

static bool in_cartridge(uint32_t far) {
  return far >= 0x800000u && far + 1 <= 0x9fffffu && (far & 0xffffu) >= 0x8000u &&
         (far & 0xffffu) != 0xffffu;
}

static bool changed(uint16_t bits, int slot) {
  return (bits & (0x0100u << slot)) != 0;
}

bool tile_anim_job_supported(const Wram* w) {
  const uint16_t bits = wram_r16(w, W_TILE_ANIM_CHANGED);
  for (int slot = 0; slot < TILE_ANIM_SLOTS; slot++) {
    if (!changed(bits, slot)) continue;
    const uint16_t frame = wram_r16(w, W_TILE_ANIM_FRAME_ATTR + 2 * slot);
    const uint16_t tile = wram_r16(w, W_TILE_ANIM_ATTR + 2 * slot);
    const uint32_t from = far_pointer(w, W_TILE_ATTRS_LEVEL, frame);
    if (!in_cartridge(from)) return false;
    if (!in_wram(far_pointer(w, W_TILE_ATTRS, tile))) return false;
  }
  return true;
}

// The tile in `slot` takes its frame's attributes, and its frame's picture
// goes to VRAM.
static void send_tile(Wram* w, const Rom* rom, HwTrace* t, int slot) {
  const uint16_t frame = wram_r16(w, W_TILE_ANIM_FRAME_ATTR + 2 * slot);
  const uint16_t tile = wram_r16(w, W_TILE_ANIM_ATTR + 2 * slot);
  const uint32_t from = far_pointer(w, W_TILE_ATTRS_LEVEL, frame);
  hw_run(t, TJ_SEND);
  wram_w16(w, far_pointer(w, W_TILE_ATTRS, tile) - 0x7e0000u,
           rom_word(rom, from));

  dma_to_vram(t, wram_r8(w, W_TILE_ANIM_SOURCE_BANK),
              wram_r16(w, W_TILE_ANIM_SOURCE + 2 * slot),
              wram_r16(w, W_TILE_ANIM_VRAM + 2 * slot), TILE_ANIM_BYTES);
  hw_run(t, TJ_SENT);
}

int tile_anim_job(Wram* w, const Rom* rom, HwTrace* t) {
  store8(t, TJ_HEAD, REG_VMAIN, VMAIN_STEP_ON_HIGH);
  hw_run(t, TJ_START);

  const uint16_t bits = wram_r16(w, W_TILE_ANIM_CHANGED);
  int sent = 0;
  for (int slot = TILE_ANIM_SLOTS - 1; slot >= 0; slot--) {
    hw_run(t, TJ_TEST);
    if (changed(bits, slot)) {
      send_tile(w, rom, t, slot);
      sent++;
    } else {
      hw_run(t, DMA_TAKEN);
    }
    hw_run(t, TJ_NEXT);
    if (slot > 0) hw_run(t, DMA_TAKEN);
  }
  // Eight shifts: the low byte is where the bits were.
  wram_w16(w, W_TILE_ANIM_CHANGED, (uint16_t)(bits << 8));
  if (sent) {
    PORT_COVER(tile_anim_job_sent);
  }
  hw_run(t, TJ_TAIL);
  return sent;
}

// --- What a screen's sends are called with -----------------------------------

const SendArgs SEND_ARGS[SEND_ARGS_COUNT] = {
#define X(at, sym, pc, exit, pull, pushed, a, x, y) \
  [SEND_ARGS_AT_##at] = {pc, exit, pull, pushed, a, x, y},
    SEND_ARGS_BY_ADDRESS(X)
#undef X
};

void send_args(Wram* w, PortCpu* c, const SendArgs* args) {
  PORT_COVER(send_args);
  if (args->pull) {
    c->a = pull16(w, c);
    set_nz16(c, c->a);
  }
  if (args->pushed >= 0) push16(w, c, (uint16_t)args->pushed);
  if (args->a >= 0) {
    c->a = (uint16_t)args->a;
    c->x = (uint16_t)args->x;
    set_nz16(c, c->x);
  }
  if (args->y >= 0) {
    c->y = (uint16_t)args->y;
    set_nz16(c, c->y);
  }
  c->pc = args->exit;
}
