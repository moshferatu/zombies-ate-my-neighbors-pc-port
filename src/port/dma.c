// DMA to the picture hardware, and three vblank jobs -- see port/dma.h.

#include "port/dma.h"

#include "port/coverage.h"
#include "port/logo.h"
#include "port/thread.h"

// The registers.
#define REG_INIDISP 0x2100u    // the brightness, and the picture blanked
#define REG_BGMODE 0x2105u     // the mode, and whose tiles are 16 across
#define REG_BG1HOFS 0x210du    // the first layer's scroll across, written twice
#define REG_BG1VOFS 0x210eu    // ...and down
#define REG_BG1SC 0x2107u      // where the first layer's map is, and how big
#define REG_BG2SC 0x2108u      // ...and the second's
#define REG_BG3SC 0x2109u      // where the third layer's map is
#define REG_BG12NBA 0x210bu    // where the first two layers' tiles are
#define REG_BG34NBA 0x210cu    // ...and the third's
#define REG_VMAIN 0x2115u      // how VRAM's address steps
#define REG_VMADD 0x2116u      // VRAM's address, in words
#define REG_CGADD 0x2121u      // which colour
#define REG_WOBJSEL 0x2125u    // which windows the colour arithmetic has
#define REG_WH0 0x2126u        // the windows' four edges, from here
#define REG_TM 0x212cu         // which layers are on
#define REG_TS 0x212du         // ...and which are on the second screen
#define REG_CGWSEL 0x2130u     // where the colour arithmetic is done
#define REG_CGADSUB 0x2131u    // ...to what, and which way
#define REG_COLDATA 0x2132u    // the fixed colour
#define REG_MDMAEN 0x420bu     // start the channels whose bits are set
#define REG_HDMAEN 0x420cu     // the channels that write every line
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
#define DEST_WINDOW_1 0x26u  // `$2126`, a window's left edge, and its right
#define DEST_WINDOW_2 0x28u
#define BG1_MAP_6800_64_BY_64 0x6bu

// A channel's registers, as HDMA reads them.
enum {
  HDMA_MODE = 0,
  HDMA_DEST = 1,
  HDMA_TABLE = 2,
  HDMA_TABLE_BANK = 4,
  HDMA_INDIRECT_BANK = 7,
};

static uint16_t hdma_reg(int channel, int which) {
  return (uint16_t)(REG_DMA_MODE + channel * 0x10 + which);
}

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

void window_hdma_setup(HwTrace* t) {
  PORT_COVER(window_hdma_setup);
  store8(t, DMA_SEP_STORE, REG_HDMAEN, 0);
  store8(t, DMA_IMM, hdma_reg(5, HDMA_MODE), MODE_TWO_REGS);
  store8(t, DMA_STORE, hdma_reg(6, HDMA_MODE), MODE_TWO_REGS);
  store8(t, DMA_STORE, hdma_reg(7, HDMA_MODE), MODE_TWO_REGS);
  store8(t, DMA_IMM, hdma_reg(5, HDMA_DEST), DEST_WINDOW_1);
  store8(t, DMA_STORE, hdma_reg(6, HDMA_DEST), DEST_WINDOW_1);
  store8(t, DMA_IMM, hdma_reg(7, HDMA_DEST), DEST_WINDOW_2);
  store8(t, DMA_IMM, hdma_reg(6, HDMA_TABLE_BANK), WINDOW_TABLE_BANK);
  store8(t, DMA_STORE, hdma_reg(7, HDMA_TABLE_BANK), WINDOW_TABLE_BANK);
  store8(t, DMA_IMM, hdma_reg(5, HDMA_TABLE_BANK), 0x7e);
  for (int channel = 5; channel <= 7; channel++)
    store8(t, DMA_STORE, hdma_reg(channel, HDMA_INDIRECT_BANK), 0);
  store16(t, DMA_REP_IMM16, hdma_reg(5, HDMA_TABLE), WINDOW_TABLE_WRAM);
  store16(t, DMA_IMM16, hdma_reg(6, HDMA_TABLE), WINDOW_TABLE_A);
  store16(t, DMA_IMM16, hdma_reg(7, HDMA_TABLE), WINDOW_TABLE_B);
}

void camera_job_begin(HwTrace* t) {
  PORT_COVER(camera_job_begin);
  store8(t, DMA_SEP_IMM, REG_BG1SC, BG1_MAP_6800_64_BY_64);
  hw_run(t, DMA_REP_LOADS);
}

#define COLOUR_IN_BOTH_WINDOWS 0xa0u
#define MATH_INSIDE_THE_WINDOW 0x10u  // ...with the fixed colour
#define SUBTRACT_FROM_BG2_AND_BACKDROP 0xa2u

void colour_window_reset(Wram* w, HwTrace* t) {
  PORT_COVER(colour_window_reset);
  store8(t, DMA_SEP_IMM, REG_WOBJSEL, COLOUR_IN_BOTH_WINDOWS);
  store8(t, DMA_IMM, REG_CGWSEL, MATH_INSIDE_THE_WINDOW);
  store8(t, DMA_IMM, REG_CGADSUB, SUBTRACT_FROM_BG2_AND_BACKDROP);
  store8(t, DMA_IMM, REG_COLDATA, COLOUR_WINDOW_FIXED);
  for (uint16_t edge = REG_WH0; edge < REG_WH0 + 4; edge++)
    store8(t, DMA_STORE, edge, 0);
  store8(t, DMA_STORE, REG_HDMAEN, 0);
  hw_run(t, DMA_REP_STZ16);
  wram_w16(w, W_HDMA_CHANNELS, 0);
}

#define MODE_1_BG1_TILES_16 0x19u
#define MAP_AT_4000_FOUR_SCREENS 0x43u
#define MAP_AT_6000 0x60u
#define FIRST_TWO_LAYERS 0x03u

#define LAYER_SETUP_PC 0x82acd6u
#define LAYER_SETUP_RETURN 0xac2au   // what the `JSL` to it pushes
#define MAP_AT_6800_TWO_DOWN 0x6au
#define BG1_TILES_AT_2000 0x02u
#define STEP_ON_HIGH_BYTE 0x80u
#define FULL_BRIGHTNESS 0x000fu
#define FIRST_LAYER 0x0001u
#define WORDS_BANK 0x009fu
#define THREE_LAYERS_AND_SPRITES 0x0017u
#define MODE_1_BG3_IN_FRONT 0x0009u
#define BG1_TILES_AT_5000_BG2_AT_2000 0x25u

#define W_LAYER_SCROLLS 0x1360u  // across and down, for three layers
#define THIRD_LAYER_SCROLL 4     // ...of which the third's are these two
#define FIRST_AND_THIRD_LAYERS 0x05u
#define THIRD_LAYER 0x0004u
#define MODE_1_BG3_IN_FRONT_TILES_16 0x49u
#define WAVE_CHANNEL 6
#define WAVE_CHANNEL_ON 0x40u
#define MODE_TWO_BYTES_ONE_REG 0x02u
#define DEST_BG1_SCROLL_X 0x0du  // `$210D`
#define WAVE_TABLE_BANK 0x7eu

// `$82:ACD6`, as far as its `REP`.
static void layer_setup(HwTrace* t) {
  // A word each, so the second byte of each goes to the scroll down.
  store16(t, DMA_STORE, REG_BG1HOFS, 0);
  store16(t, DMA_STORE, REG_BG1HOFS, 0);
  store8(t, DMA_SEP_IMM, REG_BG1SC, MAP_AT_6800_TWO_DOWN);
  store8(t, DMA_IMM, REG_BG12NBA, BG1_TILES_AT_2000);
  store8(t, DMA_IMM, REG_VMAIN, STEP_ON_HIGH_BYTE);
}

uint16_t level_card_layer(Wram* w, HwTrace* t, uint16_t s) {
  PORT_COVER(level_card_layer);
  hw_run(t, DMA_JSL);
  wram_w8(w, s, LAYER_SETUP_PC >> 16);
  wram_w16(w, (uint16_t)(s - 2), LAYER_SETUP_RETURN);
  layer_setup(t);
  hw_run(t, DMA_RTL);
  hw_run(t, LC_BRIGHT);
  wram_w16(w, W_LOGO_BRIGHTNESS, FULL_BRIGHTNESS);
  // A word again: nothing on the second screen.
  store16(t, DMA_IMM16, REG_TM, FIRST_LAYER);
  hw_run(t, LC_WORDS);
  return wram_r16(w, (uint16_t)(s + 1));
}

uint8_t level_card_layers_back(const Wram* w, HwTrace* t, uint16_t s) {
  PORT_COVER(level_card_layers_back);
  store16(t, DMA_IMM16, REG_TM, THREE_LAYERS_AND_SPRITES);
  // ...and the mosaic off, the word's other byte.
  store16(t, DMA_IMM16, REG_BGMODE, MODE_1_BG3_IN_FRONT);
  store8(t, DMA_SEP_IMM, REG_BG12NBA, BG1_TILES_AT_5000_BG2_AT_2000);
  store8(t, DMA_IMM, REG_BG1VOFS, 0);
  store8(t, DMA_STORE, REG_BG1VOFS, 0);
  store8(t, DMA_STORE, REG_BG1HOFS, 0);
  store8(t, DMA_STORE, REG_BG1HOFS, 0);
  hw_run(t, DMA_REP_PLB2);
  return wram_r8(w, (uint16_t)(s + 2));
}

void intro_layers_setup(HwTrace* t) {
  PORT_COVER(intro_layers_setup);
  store8(t, DMA_SEP_IMM, REG_BGMODE, MODE_1_BG1_TILES_16);
  store8(t, DMA_IMM_ORA, REG_BG1SC, MAP_AT_4000_FOUR_SCREENS);
  store8(t, DMA_IMM, REG_BG2SC, MAP_AT_6000);
  store8(t, DMA_IMM, REG_BG12NBA, 0);
}

static void shadow_set(Wram* w, HwTrace* t, uint16_t at, uint16_t v) {
  hw_run(t, DMA_IMM16_MEM);
  wram_w16(w, at, v);
}

void intro_layers_on(Wram* w, HwTrace* t) {
  PORT_COVER(intro_layers_on);
  shadow_set(w, t, W_LOGO_SCROLL_X, 0x0100);
  shadow_set(w, t, W_LOGO_SCROLL_Y, 0x0400);
  shadow_set(w, t, W_LOGO_SCROLL_2_X, 0x0100);
  shadow_set(w, t, W_LOGO_SCROLL_2_Y, 0);
  store8(t, DMA_SEP_IMM, REG_TM, FIRST_TWO_LAYERS);
  hw_run(t, DMA_REP_LOADS3);
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

void opening_begin(Wram* w, HwTrace* t) {
  PORT_COVER(opening_begin);
  for (int word = 0; word < 6; word++) {
    wram_w16(w, (uint16_t)(W_LAYER_SCROLLS + 2 * word), 0);
  }
  store8(t, OB_HEAD, REG_TM, FIRST_AND_THIRD_LAYERS);
  hw_run(t, OB_TAIL);
  wram_w16(w, W_BRIGHTNESS_SHADOW, 0);
}

void title_menu_layers(HwTrace* t) {
  PORT_COVER(title_menu_layers);
  // A word: nothing on the second screen.
  store16(t, DMA_IMM16, REG_TM, TITLE_MENU_LAYERS_ON);
}

#define NO_COLOUR_WINDOWS 0x00u
#define FIXED_COLOUR_BLACK 0xe0u
#define SECOND_THIRD_AND_SPRITES 0x16u
#define FIRST_LAYER_ONLY 0x01u
#define ADD_SECOND_SCREEN 0x02u
#define ADD_TO_THIRD_LAYER 0x04u

void portraits_layers(Wram* w, HwTrace* t) {
  PORT_COVER(portraits_layers);
  wram_w16(w, W_LAYER_SCROLLS, PORTRAITS_SCROLL_X);
  wram_w16(w, W_LAYER_SCROLLS + 4, PORTRAITS_SCROLL_X);
  wram_w16(w, W_LAYER_SCROLLS + 2 * THIRD_LAYER_SCROLL, 0);
  wram_w16(w, W_LAYER_SCROLLS + 2 * THIRD_LAYER_SCROLL + 2, 0);
  store8(t, PL_HEAD, REG_WOBJSEL, NO_COLOUR_WINDOWS);
  store8(t, DMA_IMM, REG_COLDATA, FIXED_COLOUR_BLACK);
  store8(t, DMA_IMM, REG_TM, SECOND_THIRD_AND_SPRITES);
  store8(t, DMA_IMM, REG_TS, FIRST_LAYER_ONLY);
  store8(t, DMA_IMM, REG_CGWSEL, ADD_SECOND_SCREEN);
  store8(t, DMA_IMM, REG_CGADSUB, ADD_TO_THIRD_LAYER);
  store8(t, DMA_IMM, REG_BG1SC, PORTRAITS_MAP_AT_6800);
  hw_run(t, DMA_REP);
}

#define FORCED_BLANK 0x80u
#define MAP_AT_6800 0x68u
#define MAP_AT_6400 0x64u

void cursor_screen_layers(Wram* w, HwTrace* t) {
  PORT_COVER(cursor_screen_layers);
  wram_w16(w, W_CAMERA_X, 0);
  wram_w16(w, W_CAMERA_Y, 0);
  // A word each, so the second byte of each goes to the scroll down.
  store16(t, CS_HEAD, REG_BG1HOFS, 0);
  store16(t, DMA_STORE, REG_BG1HOFS, 0);
  store8(t, DMA_SEP_IMM, REG_INIDISP, FORCED_BLANK);
  store8(t, DMA_IMM, REG_BG1SC, MAP_AT_6800);
  store8(t, DMA_IMM, REG_BG12NBA, BG1_TILES_AT_2000);
  store8(t, DMA_IMM, REG_TM, CURSOR_SCREEN_LAYERS_ON);
  hw_run(t, DMA_REP);
}

void backdrop_layers(HwTrace* t) {
  PORT_COVER(backdrop_layers);
  store8(t, DMA_SEP_IMM, REG_BG3SC, MAP_AT_6400);
  store8(t, DMA_IMM, REG_BG34NBA, BACKDROP_TILES_AT_2000);
  hw_run(t, DMA_REP);
}

void words_layer_setup(HwTrace* t) {
  PORT_COVER(words_layer_setup);
  layer_setup(t);
  hw_run(t, DMA_REP);
}

void scores_layers(HwTrace* t) {
  PORT_COVER(scores_layers);
  store8(t, DMA_SEP_IMM, REG_TM, SCORES_LAYERS_ON);
  hw_run(t, DMA_REP);
}

void screen_show_begin(Wram* w, HwTrace* t, uint16_t s) {
  PORT_COVER(screen_show_begin);
  // A word: nothing on the second screen.
  store16(t, DMA_IMM16, REG_TM, THIRD_LAYER);
  hw_run(t, SS_SEND);
  wram_w16(w, W_LAYER_SCROLLS + 2 * THIRD_LAYER_SCROLL, 0);
  wram_w16(w, W_LAYER_SCROLLS + 2 * THIRD_LAYER_SCROLL + 2, 0);
  wram_w16(w, (uint16_t)(s - 1), SCREEN_SHOW_MAP_BANK);
}

void screen_show_end(HwTrace* t) {
  PORT_COVER(screen_show_end);
  store8(t, DMA_SEP_STORE, REG_TM, 0);
  hw_run(t, DMA_REP);
}

void title_hdma_setup(Wram* w, HwTrace* t) {
  PORT_COVER(title_hdma_setup);
  store8(t, DMA_SEP_STORE, REG_HDMAEN, 0);
  wram_w8(w, W_HDMA_CHANNELS, 0);
  wram_w8(w, W_HDMA_CHANNELS + 1, 0);
  store8(t, TH_HEAD, hdma_reg(WAVE_CHANNEL, HDMA_MODE), MODE_TWO_BYTES_ONE_REG);
  store8(t, DMA_IMM, hdma_reg(WAVE_CHANNEL, HDMA_DEST), DEST_BG1_SCROLL_X);
  store8(t, DMA_IMM, hdma_reg(WAVE_CHANNEL, HDMA_TABLE_BANK), WAVE_TABLE_BANK);
  store8(t, DMA_STORE, hdma_reg(WAVE_CHANNEL, HDMA_INDIRECT_BANK), 0);
  store16(t, DMA_REP_IMM16, hdma_reg(WAVE_CHANNEL, HDMA_TABLE),
          TITLE_WAVE_TABLE);
}

void title_layers(HwTrace* t) {
  PORT_COVER(title_layers);
  store8(t, DMA_SEP_IMM, REG_BGMODE, MODE_1_BG3_IN_FRONT_TILES_16);
  // A word: nothing on the second screen.
  store16(t, DMA_REP_IMM16, REG_TM, TITLE_LAYERS_ON);
}

void title_hdma_on(HwTrace* t) {
  PORT_COVER(title_hdma_on);
  store8(t, DMA_SEP_IMM, REG_HDMAEN, WAVE_CHANNEL_ON);
  hw_run(t, DMA_REP);
  hw_run(t, DMA_LOAD);
}

void title_hdma_off(Wram* w, HwTrace* t) {
  PORT_COVER(title_hdma_off);
  store8(t, DMA_SEP_STORE, REG_HDMAEN, 0);
  hw_run(t, DMA_REP_STZ16);
  wram_w16(w, W_LAYER_SCROLLS, 0);
  hw_run(t, DMA_LOAD);
}

// --- $82:DC4F  the channels that write every line -----------------------------

uint16_t hdma_channels_job(const Wram* w, HwTrace* t) {
  const uint16_t channels = wram_r16(w, W_HDMA_CHANNELS);
  store16(t, HEJ_LOAD, REG_HDMAEN, channels);
  // The second byte was the cartridge's speed, and the two bytes left are
  // fetched at it.
  hw_run(t, (channels & HDMA_CHANNELS_FAST) ? HEJ_RTL_FAST : HEJ_RTL_SLOW);
  return channels;
}

// --- $82:DC0C  a rectangle of tiles -------------------------------------------

#define REG_VMDATA 0x2118u  // a word to VRAM
#define TILE_RECT_MOST_ROW_BYTES 0x0080u
#define TILE_RECT_MOST_ROWS 0x0020u
#define TILE_RECT_MOST_WORDS 0x0200u

static uint32_t tile_rect_source(const Wram* w) {
  return wram_r16(w, W_TILE_RECT_SOURCE) |
         (uint32_t)wram_r8(w, W_TILE_RECT_SOURCE + 2) << 16;
}

// The row's loop ends when Y equals the bytes, two at a time, and the rows'
// when the count reaches zero: an odd width or a zero of either never ends.
bool tile_rect_job_supported(const Wram* w) {
  const uint16_t bytes = wram_r16(w, W_TILE_RECT_ROW_BYTES);
  const uint16_t rows = wram_r16(w, W_TILE_RECT_ROWS);
  if ((bytes & 1) != 0 || bytes == 0 || bytes > TILE_RECT_MOST_ROW_BYTES)
    return false;
  if (rows == 0 || rows > TILE_RECT_MOST_ROWS) return false;
  if ((uint32_t)(bytes / 2) * rows > TILE_RECT_MOST_WORDS) return false;
  const uint32_t source = tile_rect_source(w);
  return (source >> 16) >= 0x80 && (source & 0xffffu) >= 0x8000 &&
         (source & 0xffffu) + (uint32_t)bytes * rows <= 0x10000u;
}

uint16_t tile_rect_job(Wram* w, const Rom* rom, HwTrace* t) {
  store8(t, TJ_HEAD, REG_VMAIN, VMAIN_STEP_ON_HIGH);
  const uint16_t bytes = wram_r16(w, W_TILE_RECT_ROW_BYTES);
  uint16_t word = 0;
  for (bool first = true;; first = false) {
    store16(t, first ? RECT_FIRST : RECT_ROW, REG_VMADD,
            wram_r16(w, W_TILE_RECT_AT));
    const uint32_t source = tile_rect_source(w);
    for (uint16_t at = 0; at != bytes; at += 2) {
      word = bus_r16(w, rom, source + at);
      store16(t, RECT_WORD, REG_VMDATA, word);
      hw_run(t, RECT_NEXT);
      if (at + 2 != bytes) hw_run(t, DMA_TAKEN);
    }
    const uint16_t rows = (uint16_t)(wram_r16(w, W_TILE_RECT_ROWS) - 1);
    wram_w16(w, W_TILE_RECT_ROWS, rows);
    hw_run(t, RECT_ROW_END);
    if (rows == 0) break;
    PORT_COVER(tile_rect_next_row);
    wram_w16(w, W_TILE_RECT_AT,
             (uint16_t)(wram_r16(w, W_TILE_RECT_AT) + TILE_RECT_VRAM_ROW));
    wram_w16(w, W_TILE_RECT_SOURCE, (uint16_t)(source + bytes));
    hw_run(t, RECT_ADVANCE);
  }
  hw_run(t, DMA_TAKEN);
  hw_run(t, DMA_FLAG_RTL);
  return word;
}

// --- $82:DAA0 and $82:DADE  the count under a radar ---------------------------

uint16_t radar_count_job(const Wram* w, HwTrace* t, int player) {
  store8(t, TJ_HEAD, REG_VMAIN, VMAIN_STEP_ON_HIGH);
  store16(t, RCJ_AT, REG_VMADD,
          wram_r16(w, (uint16_t)(W_RADAR_COUNT_AT + 2 * player)));
  uint16_t left = wram_r16(w, W_RADAR_COUNT);
  hw_run(t, RCJ_TENS);
  if (left > RADAR_COUNT_MOST) {
    PORT_COVER(radar_count_most);
    left = RADAR_COUNT_MOST;
    hw_run(t, DMA_LOAD);
  } else {
    hw_run(t, DMA_TAKEN);
  }
  hw_run(t, RCJ_TEST);
  if (left & RADAR_COUNT_MOST) {
    PORT_COVER(radar_count_sixteen);
    hw_run(t, DMA_TAKEN);
    store16(t, DMA_STORE16, REG_VMDATA, RADAR_COUNT_ONE);
  } else {
    PORT_COVER(radar_count_one_digit);
    store16(t, DMA_STORE16, REG_VMDATA, 0);
    hw_run(t, DMA_BRA);
  }
  const uint16_t units = (uint16_t)((left & 0x000fu) + RADAR_COUNT_ZERO);
  store16(t, RCJ_UNITS, REG_VMDATA, units);
  hw_run(t, DMA_FLAG_RTL);
  return units;
}
