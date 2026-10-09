// See `registers.h`.

#include "video/registers.h"

#include <string.h>

void video_registers_reset(VideoRegisters* r) {
  uint16_t* const vram = r->vram;
  uint16_t* const cgram = r->cgram;
  uint16_t* const oam = r->oam;
  uint8_t* const high_oam = r->high_oam;
  memset(r, 0, sizeof *r);
  r->vram = vram;
  r->cgram = cgram;
  r->oam = oam;
  r->high_oam = high_oam;
  memset(vram, 0, 0x8000 * sizeof *vram);
  memset(cgram, 0, 0x100 * sizeof *cgram);
  memset(oam, 0, 0x100 * sizeof *oam);
  memset(high_oam, 0, 0x20);
  r->blank = true;
  r->vram_step = 1;
  r->mosaic_size = 1;
  r->mosaic_from = 1;
}

// ---------------------------------------------------------------------------
// The three doors
// ---------------------------------------------------------------------------

// The table of extra bits is 32 bytes, and the address counts them in twos.
static uint8_t* high_oam_byte(const VideoRegisters* r) {
  return &r->high_oam[(r->oam_at & 0xf) << 1 | r->oam_second];
}

// The address steps after a second byte. It is eight bits, and going round
// is what takes it from the sprites' words into the extra bits and back.
static void oam_write(VideoRegisters* r, uint8_t value) {
  if (r->oam_high) {
    *high_oam_byte(r) = value;
    if (r->oam_second && ++r->oam_at == 0) r->oam_high = false;
  } else if (!r->oam_second) {
    r->oam_first_byte = value;
  } else {
    r->oam[r->oam_at] = (uint16_t)(value << 8 | r->oam_first_byte);
    if (++r->oam_at == 0) r->oam_high = true;
  }
  r->oam_second = !r->oam_second;
}

static uint8_t oam_read(VideoRegisters* r) {
  uint8_t value;
  if (r->oam_high) {
    value = *high_oam_byte(r);
    if (r->oam_second && ++r->oam_at == 0) r->oam_high = false;
  } else if (!r->oam_second) {
    value = (uint8_t)r->oam[r->oam_at];
  } else {
    value = (uint8_t)(r->oam[r->oam_at] >> 8);
    if (++r->oam_at == 0) r->oam_high = true;
  }
  r->oam_second = !r->oam_second;
  return value;
}

// The word of VRAM the address means. Three of the four ways turn its low
// eight, nine or ten bits left by three, for writing a bitmap a column at a
// time.
static uint16_t* vram_word(const VideoRegisters* r) {
  const uint16_t at = r->vram_at;
  uint16_t turned = at;
  switch (r->vram_remap) {
    case 1: turned = (uint16_t)((at & 0xff00) | (at & 0xe0) >> 5 | (at & 0x1f) << 3); break;
    case 2: turned = (uint16_t)((at & 0xfe00) | (at & 0x1c0) >> 6 | (at & 0x3f) << 3); break;
    case 3: turned = (uint16_t)((at & 0xfc00) | (at & 0x380) >> 7 | (at & 0x7f) << 3); break;
  }
  return &r->vram[turned & 0x7fff];
}

// ---------------------------------------------------------------------------
// Writes
// ---------------------------------------------------------------------------

// Half of $2123, $2124 or $2125: one layer's use of the two windows.
static void window_use(VideoWindow* w, uint8_t bits) {
  w->one_inverted = bits & 1;
  w->one = bits & 2;
  w->two_inverted = bits & 4;
  w->two = bits & 8;
}

// A bit each for the four backgrounds and the sprites.
static void five_layers(bool on[5], uint8_t value) {
  for (int l = 0; l < 5; l++) on[l] = value >> l & 1;
}

void video_registers_write(VideoRegisters* r, uint8_t address, uint8_t value, int line) {
  switch (address) {
    case 0x00:
      r->brightness = value & 0xf;
      r->blank = value & 0x80;
      break;
    case 0x01:
      r->obj_sizes = value >> 5;
      r->obj_tiles_at[0] = (uint16_t)((value & 7) << 13);
      r->obj_tiles_at[1] = (uint16_t)(r->obj_tiles_at[0] + (((value & 0x18) + 8) << 9));
      break;
    case 0x02:
      r->oam_at = r->oam_at_written = value;
      r->oam_high = r->oam_high_written;
      r->oam_second = false;
      break;
    case 0x03:
      r->obj_from_address = value & 0x80;
      r->oam_high = r->oam_high_written = value & 1;
      r->oam_at = r->oam_at_written;
      r->oam_second = false;
      break;
    case 0x04:
      oam_write(r, value);
      break;
    case 0x05:
      r->mode = value & 7;
      r->bg3_front = value & 8;
      for (int l = 0; l < 4; l++) r->bg[l].big_tiles = value >> (4 + l) & 1;
      break;
    case 0x06:
      for (int l = 0; l < 4; l++) r->bg[l].mosaic = value >> l & 1;
      r->mosaic_size = (uint8_t)((value >> 4) + 1);
      r->mosaic_from = (uint8_t)line;
      break;
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0a: {
      VideoBg* bg = &r->bg[address - 0x07];
      bg->map_wide = value & 1;
      bg->map_high = value & 2;
      bg->map_at = (uint16_t)((value & 0xfc) << 8);
      break;
    }
    case 0x0b:
      r->bg[0].tiles_at = (uint16_t)((value & 0xf) << 12);
      r->bg[1].tiles_at = (uint16_t)((value & 0xf0) << 8);
      break;
    case 0x0c:
      r->bg[2].tiles_at = (uint16_t)((value & 0xf) << 12);
      r->bg[3].tiles_at = (uint16_t)((value & 0xf0) << 8);
      break;
    // The scrolls. $210D and $210E are mode 7's as well, with a latch of
    // its own. A horizontal scroll's low three bits come from one write
    // further back than the rest.
    case 0x0d:
      r->m7[6] = (int16_t)((value << 8 | r->m7_latch) & 0x1fff);
      r->m7_latch = value;
      // fallthrough
    case 0x0f:
    case 0x11:
    case 0x13:
      r->bg[(address - 0x0d) / 2].hscroll =
          (uint16_t)((value << 8 | (r->scroll_latch & 0xf8) | (r->hscroll_latch & 7)) & 0x3ff);
      r->scroll_latch = r->hscroll_latch = value;
      break;
    case 0x0e:
      r->m7[7] = (int16_t)((value << 8 | r->m7_latch) & 0x1fff);
      r->m7_latch = value;
      // fallthrough
    case 0x10:
    case 0x12:
    case 0x14:
      r->bg[(address - 0x0e) / 2].vscroll = (uint16_t)((value << 8 | r->scroll_latch) & 0x3ff);
      r->scroll_latch = value;
      break;
    case 0x15:
      r->vram_step = (value & 3) == 0 ? 1 : (value & 3) == 1 ? 32 : 128;
      r->vram_remap = (value & 0xc) >> 2;
      r->vram_step_on_high = value & 0x80;
      break;
    case 0x16:
      r->vram_at = (uint16_t)((r->vram_at & 0xff00) | value);
      r->vram_ahead = *vram_word(r);
      break;
    case 0x17:
      r->vram_at = (uint16_t)((r->vram_at & 0x00ff) | value << 8);
      r->vram_ahead = *vram_word(r);
      break;
    case 0x18: {
      uint16_t* word = vram_word(r);
      *word = (uint16_t)((*word & 0xff00) | value);
      if (!r->vram_step_on_high) r->vram_at += r->vram_step;
      break;
    }
    case 0x19: {
      uint16_t* word = vram_word(r);
      *word = (uint16_t)((*word & 0x00ff) | value << 8);
      if (r->vram_step_on_high) r->vram_at += r->vram_step;
      break;
    }
    case 0x1a:
      r->m7_large_field = value & 0x80;
      r->m7_fill = value & 0x40;
      r->m7_flip_y = value & 2;
      r->m7_flip_x = value & 1;
      break;
    case 0x1b:
    case 0x1c:
    case 0x1d:
    case 0x1e:
      r->m7[address - 0x1b] = (int16_t)(value << 8 | r->m7_latch);
      r->m7_latch = value;
      break;
    case 0x1f:
    case 0x20:
      r->m7[address - 0x1b] = (int16_t)((value << 8 | r->m7_latch) & 0x1fff);
      r->m7_latch = value;
      break;
    case 0x21:
      r->cgram_at = value;
      r->cgram_second = false;
      break;
    case 0x22:
      if (!r->cgram_second) r->cgram_first_byte = value;
      else r->cgram[r->cgram_at++] = (uint16_t)(value << 8 | r->cgram_first_byte);
      r->cgram_second = !r->cgram_second;
      break;
    case 0x23:
    case 0x24:
    case 0x25:
      window_use(&r->window[(address - 0x23) * 2], value & 0xf);
      window_use(&r->window[(address - 0x23) * 2 + 1], value >> 4);
      break;
    case 0x26: r->window1_left = value; break;
    case 0x27: r->window1_right = value; break;
    case 0x28: r->window2_left = value; break;
    case 0x29: r->window2_right = value; break;
    case 0x2a:
      for (int l = 0; l < 4; l++) r->window[l].logic = value >> (2 * l) & 3;
      break;
    case 0x2b:
      r->window[VIDEO_OBJ].logic = value & 3;
      r->window[VIDEO_BACKDROP].logic = value >> 2 & 3;
      break;
    case 0x2c: five_layers(r->main, value); break;
    case 0x2d: five_layers(r->sub, value); break;
    case 0x2e: five_layers(r->main_windowed, value); break;
    case 0x2f: five_layers(r->sub_windowed, value); break;
    case 0x30:
      r->direct_colour = value & 1;
      r->add_sub = value & 2;
      r->prevent = (value & 0x30) >> 4;
      r->clip = (value & 0xc0) >> 6;
      break;
    case 0x31:
      r->subtract = value & 0x80;
      r->half = value & 0x40;
      for (int l = 0; l < VIDEO_LAYERS; l++) r->math[l] = value >> l & 1;
      break;
    // One write can set any of the fixed colour's three parts.
    case 0x32:
      if (value & 0x80) r->fixed_b = value & 0x1f;
      if (value & 0x40) r->fixed_g = value & 0x1f;
      if (value & 0x20) r->fixed_r = value & 0x1f;
      break;
    case 0x33:
      r->interlace = value & 1;
      r->obj_interlace = value & 2;
      r->overscan = value & 4;
      r->pseudo_hires = value & 8;
      r->m7_ext_bg = value & 0x40;
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------

// A counter is nine bits, read low byte first. The second read drives one
// bit; the rest is what the bus was left at.
static uint8_t counter_byte(uint16_t count, bool* second, uint8_t bus) {
  const uint8_t value = *second ? (uint8_t)((count >> 8 & 1) | (bus & 0xfe)) : (uint8_t)count;
  *second = !*second;
  return value;
}

uint8_t video_registers_read(VideoRegisters* r, uint8_t address, const VideoBus* bus) {
  switch (address) {
    // Write-only registers that leave the first half's bus as it was.
    case 0x04: case 0x14: case 0x24:
    case 0x05: case 0x15: case 0x25:
    case 0x06: case 0x16: case 0x26:
    case 0x08: case 0x18: case 0x28:
    case 0x09: case 0x19: case 0x29:
    case 0x0a: case 0x1a: case 0x2a:
      return r->bus1;
    // Mode 7's a times the high byte of its b, 24 bits.
    case 0x34:
    case 0x35:
    case 0x36: {
      const int product = r->m7[0] * (r->m7[1] >> 8);
      return r->bus1 = (uint8_t)(product >> (8 * (address - 0x34)));
    }
    case 0x37:
      r->h_count = (uint16_t)bus->dot;
      r->v_count = (uint16_t)bus->line;
      r->latched = true;
      return bus->open_bus;
    case 0x38:
      return r->bus1 = oam_read(r);
    case 0x39: {
      const uint16_t word = r->vram_ahead;
      if (!r->vram_step_on_high) {
        r->vram_ahead = *vram_word(r);
        r->vram_at += r->vram_step;
      }
      return r->bus1 = (uint8_t)word;
    }
    case 0x3a: {
      const uint16_t word = r->vram_ahead;
      if (r->vram_step_on_high) {
        r->vram_ahead = *vram_word(r);
        r->vram_at += r->vram_step;
      }
      return r->bus1 = (uint8_t)(word >> 8);
    }
    // A colour is fifteen bits; the sixteenth is the bus's.
    case 0x3b: {
      uint8_t value;
      if (!r->cgram_second) value = (uint8_t)r->cgram[r->cgram_at];
      else value = (uint8_t)((r->cgram[r->cgram_at++] >> 8 & 0x7f) | (r->bus2 & 0x80));
      r->cgram_second = !r->cgram_second;
      return r->bus2 = value;
    }
    case 0x3c:
      return r->bus2 = counter_byte(r->h_count, &r->h_second, r->bus2);
    case 0x3d:
      return r->bus2 = counter_byte(r->v_count, &r->v_second, r->bus2);
    // The first half's status: its version, 1, and the sprites' two flags.
    case 0x3e:
      return r->bus1 = (uint8_t)(1 | (r->bus1 & 0x10) | r->range_over << 6 | r->time_over << 7);
    // The second half's: its version, 3, PAL or not, whether the counters
    // have been latched, and the field. Reading it lets them be latched again
    // and read from their low bytes.
    case 0x3f: {
      const uint8_t value =
          (uint8_t)(3 | bus->pal << 4 | (r->bus2 & 0x20) | r->latched << 6 | r->even_frame << 7);
      r->latched = false;
      r->h_second = r->v_second = false;
      return r->bus2 = value;
    }
    default:
      return bus->open_bus;
  }
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------

void video_registers_frame_start(VideoRegisters* r) {
  r->mosaic_from = 1;
  r->range_over = false;
  r->time_over = false;
  r->even_frame = !r->even_frame;
}

bool video_registers_overscan(VideoRegisters* r) {
  r->frame_overscan = r->overscan;
  return r->frame_overscan;
}

// OAM's address goes back to the one the game wrote, unless the screen is
// blanked.
void video_registers_vblank(VideoRegisters* r) {
  if (!r->blank) {
    r->oam_at = r->oam_at_written;
    r->oam_high = r->oam_high_written;
    r->oam_second = false;
  }
  r->frame_interlace = r->interlace;
}

void video_registers_state(const VideoRegisters* r, VideoState* s) {
  s->vram = r->vram;
  s->cgram = r->cgram;
  s->blank = r->blank;
  s->brightness = r->brightness;
  s->mode = r->mode;
  s->bg3_front = r->bg3_front;
  s->pseudo_hires = r->pseudo_hires;
  s->overscan = r->frame_overscan;
  s->mosaic_size = r->mosaic_size;
  memcpy(s->bg, r->bg, sizeof s->bg);
  memcpy(s->main, r->main, sizeof s->main);
  memcpy(s->sub, r->sub, sizeof s->sub);
  memcpy(s->main_windowed, r->main_windowed, sizeof s->main_windowed);
  memcpy(s->sub_windowed, r->sub_windowed, sizeof s->sub_windowed);
  memcpy(s->math, r->math, sizeof s->math);
  s->add_sub = r->add_sub;
  s->subtract = r->subtract;
  s->half = r->half;
  s->fixed_r = r->fixed_r;
  s->fixed_g = r->fixed_g;
  s->fixed_b = r->fixed_b;
  s->prevent = r->prevent;
  s->clip = r->clip;
  s->colour_window = r->window[VIDEO_BACKDROP];
  s->window1_left = r->window1_left;
  s->window1_right = r->window1_right;
  s->window2_left = r->window2_left;
  s->window2_right = r->window2_right;
  s->obj.oam = r->oam;
  s->obj.high_oam = r->high_oam;
  s->obj.sizes = r->obj_sizes;
  s->obj.first = (uint8_t)(r->obj_from_address ? r->oam_at >> 1 : 0);
  s->obj.tiles_at[0] = r->obj_tiles_at[0];
  s->obj.tiles_at[1] = r->obj_tiles_at[1];
  s->obj.interlace = r->obj_interlace;
}

// ---------------------------------------------------------------------------
// A column of a background
// ---------------------------------------------------------------------------

// The bits a pixel of each background has in each mode, and 0 for a
// background the mode does not have.
static const uint8_t BG_DEPTH[8][4] = {
    {2, 2, 2, 2}, {4, 4, 2, 0}, {4, 4, 0, 0}, {8, 4, 0, 0},
    {8, 2, 0, 0}, {4, 2, 0, 0}, {4, 0, 0, 0}, {8, 0, 0, 0},
};

// The map's word for the tile at pixel (x, y) of a background.
static uint16_t map_word(const VideoRegisters* r, int layer, int x, int y) {
  const VideoBg* bg = &r->bg[layer];
  // Modes 5 and 6 have tiles sixteen wide whatever the register says.
  const bool wide_tiles = bg->big_tiles || r->mode == 5 || r->mode == 6;
  const int across = wide_tiles ? 4 : 3, down = bg->big_tiles ? 4 : 3;
  x &= 0x3ff;
  y &= 0x3ff;
  uint16_t at = (uint16_t)(bg->map_at + (((y >> down) & 0x1f) << 5 | ((x >> across) & 0x1f)));
  // Past the 32nd tile is the next screen of the map, if it has one.
  if ((x & (0x20 << across)) && bg->map_wide) at += 0x400;
  if ((y & (0x20 << down)) && bg->map_high) at += bg->map_wide ? 0x800 : 0x400;
  return r->vram[at & 0x7fff];
}

// Whether a character is eight rows of nothing: `depth` bits a pixel is
// four words a bit.
static bool character_empty(const VideoRegisters* r, int layer, int character, int depth) {
  const int words = 4 * depth;
  const uint16_t at = (uint16_t)(r->bg[layer].tiles_at + (character & 0x3ff) * words);
  for (int i = 0; i < words; i++)
    if (r->vram[(at + i) & 0x7fff] != 0) return false;
  return true;
}

static int bg_depth(const VideoRegisters* r, int layer) {
  return layer < 0 || layer > 3 ? 0 : BG_DEPTH[r->mode][layer];
}

bool video_column_empty(const VideoRegisters* r, int layer, int x) {
  const int depth = bg_depth(r, layer);
  if (depth == 0) return false;
  const VideoBg* bg = &r->bg[layer];
  const int step = bg->big_tiles ? 16 : 8;
  // Every row of tiles the picture touches, and one more for the row the
  // scroll leaves half on it.
  for (int y = bg->vscroll; y < bg->vscroll + 224 + step; y += step) {
    const int tile = map_word(r, layer, x + bg->hscroll, y) & 0x3ff;
    if (!character_empty(r, layer, tile, depth)) return false;
    // A tile sixteen square is four characters: the one named, the next,
    // and the two a row of sixteen below them.
    if (bg->big_tiles &&
        !(character_empty(r, layer, tile + 1, depth) &&
          character_empty(r, layer, tile + 0x10, depth) &&
          character_empty(r, layer, tile + 0x11, depth)))
      return false;
  }
  return true;
}

bool video_column_filled(const VideoRegisters* r, int layer, int x) {
  const int depth = bg_depth(r, layer);
  if (depth == 0) return false;
  const VideoBg* bg = &r->bg[layer];
  const int step = bg->big_tiles ? 16 : 8;
  for (int y = bg->vscroll; y < bg->vscroll + 224 + step; y += step) {
    const int tile = map_word(r, layer, x + bg->hscroll, y) & 0x3ff;
    if (character_empty(r, layer, tile, depth)) return false;
  }
  return true;
}
