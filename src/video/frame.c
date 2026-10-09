// See `frame.h`.

#include "video/frame.h"

#include <string.h>

void video_picture_init(VideoPicture* p) {
  memset(p, 0, sizeof *p);
  p->clamp_lo = -VIDEO_EXTRA_MAX;
  p->clamp_hi = 255 + VIDEO_EXTRA_MAX;
}

void video_picture_reset(VideoPicture* p) {
  memset(p->front, 0, sizeof p->front);
  memset(p->remap_on, 0, sizeof p->remap_on);
  memset(p->remap, 0, sizeof p->remap);
}

void video_frame_init(VideoFrame* f) {
  memset(f, 0, sizeof *f);
  // No line and no scroll has been searched for.
  memset(f->centre.fill_col, 0xff, sizeof f->centre.fill_col);
  memset(f->centre.fill_from, 0xff, sizeof f->centre.fill_from);
  memset(f->centre.fill_line, 0xff, sizeof f->centre.fill_line);
  for (int l = 0; l < 4; l++) f->centre.last_full[l] = -2;
}

// Which background's scroll across an address is, or -1.
static int hscroll_of(uint8_t address) {
  return address >= 0x0d && address <= 0x13 && (address & 1) ? (address - 0x0d) / 2 : -1;
}

void video_frame_write(VideoFrame* f, VideoRegisters* r, uint8_t address, uint8_t value, int line,
                       bool drawing) {
  // The scrolls, $210D to $2114, are noted line by line and are no reason a
  // frame cannot be taken apart. Nor are the windows' edges, $2126 to $2129.
  if (drawing && address >= 0x26 && address <= 0x29) {
    f->window_raster = true;
  } else if (drawing && (address < 0x0d || address > 0x14)) {
    if (!f->mid_frame_write) {
      f->mid_frame_address = address;
      f->mid_frame_line = (uint16_t)line;
      f->mid_frame_writes = 0;
    }
    f->mid_frame_write = true;
    f->mid_frame_writes++;
  }
  const int layer = hscroll_of(address);
  const uint16_t before = layer < 0 ? 0 : r->bg[layer].hscroll;
  video_registers_write(r, address, value, line);
  // Counted when it changes, and not when it is written: a game that sets
  // the same scroll before every line is not drawing a line at a time.
  if (layer >= 0 && r->bg[layer].hscroll != before && f->hscroll_writes[layer] < 255)
    f->hscroll_writes[layer]++;
}

void video_frame_start(VideoFrame* f, const VideoRegisters* r, const VideoPicture* p) {
  f->mid_frame_write = false;
  f->mid_frame_writes = 0;
  f->window_raster = false;
  // Every screen of this game is put up behind a blanked or a faded-out
  // picture, so a dark frame is where one screen's answers end.
  const bool dark = r->blank || r->brightness == 0;
  for (int l = 0; l < 4; l++) {
    // Across, and only across: it is the seam down the map's side that a
    // margin would repeat, and a card whose writing slides up the screen has
    // never crossed it.
    if (dark) f->scrolled[l] = false;
    else if (r->bg[l].hscroll != f->last_hscroll[l]) f->scrolled[l] = true;
    f->last_hscroll[l] = r->bg[l].hscroll;
    if (dark) f->raster[l] = false;
    else if (f->hscroll_writes[l] >= VIDEO_RASTER_WRITES) f->raster[l] = true;
    f->hscroll_writes[l] = 0;
  }
  // Once a frame, so that a layer cannot change its mind half way down, and
  // here, because the vblank just past is when this frame's maps went up.
  // Only a widened picture asks.
  if (p->extra_left == 0 && p->extra_right == 0) return;
  for (int l = 0; l < 4; l++)
    f->edge_empty[l] = r->mode != 7 && p->wide[l] == VIDEO_WIDE_AUTO &&
                       video_column_empty(r, l, 0) && video_column_empty(r, l, 255);
}

void video_frame_line(VideoFrame* f, const VideoRegisters* r, int line) {
  if (line < 0 || line >= VIDEO_LINES) return;
  for (int l = 0; l < 4; l++) {
    f->line_hscroll[l][line] = r->bg[l].hscroll;
    f->line_vscroll[l][line] = r->bg[l].vscroll;
  }
  f->line_window[line][0] = r->window1_left;
  f->line_window[line][1] = r->window1_right;
  f->line_window[line][2] = r->window2_left;
  f->line_window[line][3] = r->window2_right;
}

void video_frame_state(const VideoFrame* f, const VideoPicture* p, VideoState* s) {
  s->extra_left = p->extra_left;
  s->extra_right = p->extra_right;
  memcpy(s->wide, p->wide, sizeof s->wide);
  s->clamp_lo = p->clamp_lo;
  s->clamp_hi = p->clamp_hi;
  s->obj.place = p->place;
  s->obj.shift = p->shift;
  s->obj.front = p->front;
  s->obj.remap_on = p->remap_on;
  s->obj.remap = p->remap;
  memcpy(s->edge_empty, f->edge_empty, sizeof s->edge_empty);
  memcpy(s->raster, f->raster, sizeof s->raster);
  memcpy(s->scrolled, f->scrolled, sizeof s->scrolled);
  s->line_hscroll = f->line_hscroll;
  s->line_vscroll = f->line_vscroll;
  s->line_window = f->line_window;
}
