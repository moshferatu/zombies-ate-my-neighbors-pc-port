// See `ppu_hook.h`.

#include "video/ppu_hook.h"

#include <string.h>

// The policies are the PPU's by value.
_Static_assert(VIDEO_WIDE_AUTO == (int)ppu_wideAuto &&
                   VIDEO_WIDE_STRETCH == (int)ppu_wideStretch &&
                   VIDEO_WIDE_ANCHOR == (int)ppu_wideAnchor &&
                   VIDEO_WIDE_CLIP == (int)ppu_wideClip &&
                   VIDEO_WIDE_CLAMP_EDGE == (int)ppu_wideClampEdge &&
                   VIDEO_WIDE_CENTRE == (int)ppu_wideCentre &&
                   VIDEO_WIDE_TILE == (int)ppu_wideTile &&
                   VIDEO_WIDE_SWEEP == (int)ppu_wideSweep &&
                   VIDEO_WIDE_CENTRE_CLIP == (int)ppu_wideCentreClip,
               "the widescreen policies are numbered as the PPU numbers them");
_Static_assert(VIDEO_EXTRA_MAX == PPU_EXTRA_MAX && VIDEO_LINES == PPU_LINES,
               "the picture is as wide and as tall as the PPU's");

void video_state_from_ppu(VideoState* s, const Ppu* ppu) {
  s->vram = ppu->vram;
  s->cgram = ppu->cgram;
  s->blank = ppu->forcedBlank;
  s->brightness = ppu->brightness;
  s->mode = ppu->mode;
  s->bg3_front = ppu->bg3priority;
  s->pseudo_hires = ppu->pseudoHires;
  s->overscan = ppu->frameOverscan;
  s->mosaic_size = ppu->mosaicSize;
  for (int l = 0; l < 4; l++) {
    const BgLayer* from = &ppu->bgLayer[l];
    s->bg[l] = (VideoBg){
        .hscroll = from->hScroll,
        .vscroll = from->vScroll,
        .map_at = from->tilemapAdr,
        .tiles_at = from->tileAdr,
        .map_wide = from->tilemapWider,
        .map_high = from->tilemapHigher,
        .big_tiles = from->bigTiles,
        .mosaic = from->mosaicEnabled,
    };
    s->edge_empty[l] = ppu->layerEdgeEmpty[l] != 0;
    s->raster[l] = ppu->layerRaster[l] != 0;
    s->scrolled[l] = ppu->layerScrolled[l] != 0;
  }
  for (int l = 0; l < 5; l++) {
    s->main[l] = ppu->layer[l].mainScreenEnabled;
    s->sub[l] = ppu->layer[l].subScreenEnabled;
    s->main_windowed[l] = ppu->layer[l].mainScreenWindowed;
    s->sub_windowed[l] = ppu->layer[l].subScreenWindowed;
    s->wide[l] = ppu->layerWide[l];
  }
  for (int l = 0; l < VIDEO_LAYERS; l++) s->math[l] = ppu->mathEnabled[l];
  s->add_sub = ppu->addSubscreen;
  s->subtract = ppu->subtractColor;
  s->half = ppu->halfColor;
  s->fixed_r = ppu->fixedColorR;
  s->fixed_g = ppu->fixedColorG;
  s->fixed_b = ppu->fixedColorB;
  s->prevent = ppu->preventMathMode;
  s->clip = ppu->clipMode;
  const WindowLayer* w = &ppu->windowLayer[5];
  s->colour_window = (VideoWindow){
      .one = w->window1enabled,
      .two = w->window2enabled,
      .one_inverted = w->window1inversed,
      .two_inverted = w->window2inversed,
      .logic = w->maskLogic,
  };
  s->window1_left = ppu->window1left;
  s->window1_right = ppu->window1right;
  s->window2_left = ppu->window2left;
  s->window2_right = ppu->window2right;
  s->obj_pixel = ppu->objPixelBuffer;
  s->obj_priority = ppu->objPriorityBuffer;
  s->extra_left = ppu->extraLeft;
  s->extra_right = ppu->extraRight;
  s->clamp_lo = ppu->wideClampLo;
  s->clamp_hi = ppu->wideClampHi;
  s->line_hscroll = ppu->lineHScroll;
  s->line_vscroll = ppu->lineVScroll;
}

// The centred layers' margins are searched for by whichever of the two draws
// a line, and kept in the PPU, so that each finds what the other left.
void video_centre_from_ppu(VideoCentre* c, const Ppu* ppu) {
  memcpy(c->fill_col, ppu->centreFillCol, sizeof c->fill_col);
  memcpy(c->fill_from, ppu->centreFillFrom, sizeof c->fill_from);
  memcpy(c->fill_line, ppu->centreFillLine, sizeof c->fill_line);
  memcpy(c->fill_h, ppu->centreFillH, sizeof c->fill_h);
  memcpy(c->fill_v, ppu->centreFillV, sizeof c->fill_v);
  memcpy(c->fill_wrap, ppu->centreFillWrap, sizeof c->fill_wrap);
  memcpy(c->last_full, ppu->centreLastFull, sizeof c->last_full);
  memcpy(c->last_full_h, ppu->centreLastFullH, sizeof c->last_full_h);
  memcpy(c->last_full_v, ppu->centreLastFullV, sizeof c->last_full_v);
}

void video_centre_to_ppu(const VideoCentre* c, Ppu* ppu) {
  memcpy(ppu->centreFillCol, c->fill_col, sizeof c->fill_col);
  memcpy(ppu->centreFillFrom, c->fill_from, sizeof c->fill_from);
  memcpy(ppu->centreFillLine, c->fill_line, sizeof c->fill_line);
  memcpy(ppu->centreFillH, c->fill_h, sizeof c->fill_h);
  memcpy(ppu->centreFillV, c->fill_v, sizeof c->fill_v);
  memcpy(ppu->centreFillWrap, c->fill_wrap, sizeof c->fill_wrap);
  memcpy(ppu->centreLastFull, c->last_full, sizeof c->last_full);
  memcpy(ppu->centreLastFullH, c->last_full_h, sizeof c->last_full_h);
  memcpy(ppu->centreLastFullV, c->last_full_v, sizeof c->last_full_v);
}

// A line's place in the PPU's pixel buffer. Every column is eight bytes there:
// the pixel twice, each as blue, green, red and a byte that is never written,
// which `pixelOutputFormat` puts first or last.
static uint8_t* ppu_row(Ppu* ppu, int line) {
  const int row = (line - 1) + (ppu->evenFrame ? 0 : 239);
  return &ppu->pixelBuffer[row * PPU_ROW_BYTES];
}

static uint32_t row_colour(const Ppu* ppu, const uint8_t* row, int column) {
  const uint8_t* p = row + column * 8 + ppu->pixelOutputFormat;
  return (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

// Whether both of a column's pixels are `colour`.
static bool row_is(const Ppu* ppu, const uint8_t* row, int column, uint32_t colour) {
  const uint8_t* p = row + column * 8 + ppu->pixelOutputFormat;
  return row_colour(ppu, row, column) == colour &&
         ((uint32_t)p[6] << 16 | (uint32_t)p[5] << 8 | p[4]) == colour;
}

static void row_put(const Ppu* ppu, uint8_t* row, const uint32_t* colours, int width) {
  for (int c = 0; c < width; c++) {
    uint8_t* p = row + c * 8 + ppu->pixelOutputFormat;
    p[0] = p[4] = (uint8_t)colours[c];
    p[1] = p[5] = (uint8_t)(colours[c] >> 8);
    p[2] = p[6] = (uint8_t)(colours[c] >> 16);
  }
}

// FNV-1a, a line at a time: the line's number and each column's colour.
static void checksum_line(VideoHook* hook, Ppu* ppu, int line, int width) {
  if (!hook->checksummed) return;
  const uint8_t* row = ppu_row(ppu, line);
  uint64_t h = hook->checksum;
  h = (h ^ (uint64_t)line) * 1099511628211ull;
  for (int c = 0; c < width; c++) h = (h ^ row_colour(ppu, row, c)) * 1099511628211ull;
  hook->checksum = h;
  hook->checksum_lines++;
}

static void note_decline(VideoHook* hook, const char* why) {
  hook->declined++;
  for (int i = 0; i < VIDEO_HOOK_REASONS; i++) {
    if (hook->reason[i].why == NULL) hook->reason[i].why = why;
    // The reasons are string literals of `video_declines`, one address each.
    if (hook->reason[i].why == why) {
      hook->reason[i].lines++;
      return;
    }
  }
}

static bool video_ppu_line(void* user, Ppu* ppu, int line) {
  VideoHook* hook = (VideoHook*)user;
  VideoState s;
  video_state_from_ppu(&s, ppu);
  const int width = video_width(&s);
  const char* why = hook->renderer == VIDEO_EMULATED ? NULL : video_declines(&s);
  if (hook->renderer == VIDEO_EMULATED || why != NULL) {
    if (why != NULL) note_decline(hook, why);
    else hook->left++;
    ppu_drawLine(ppu, line);
    checksum_line(hook, ppu, line, width);
    return true;
  }
  uint32_t colours[VIDEO_MAX_WIDTH];
  VideoCentre centre;
  if (hook->renderer == VIDEO_CHECK) ppu_drawLine(ppu, line);
  video_centre_from_ppu(&centre, ppu);
  video_line(&hook->video, &s, &centre, line, colours);
  video_centre_to_ppu(&centre, ppu);
  hook->lines++;
  uint8_t* row = ppu_row(ppu, line);
  if (hook->renderer == VIDEO_CHECK) {
    for (int c = 0; c < width; c++) {
      if (row_is(ppu, row, c, colours[c])) continue;
      if (hook->differing == 0) {
        hook->first.frame = ppu->snes ? ppu->snes->frames : 0;
        hook->first.line = line;
        hook->first.column = c - s.extra_left;
        hook->first.native = colours[c];
        hook->first.emulated = row_colour(ppu, row, c);
      }
      hook->differing++;
      break;
    }
  } else {
    row_put(ppu, row, colours, width);
  }
  checksum_line(hook, ppu, line, width);
  return true;
}

void video_hook_install(VideoHook* hook, Ppu* ppu, VideoRenderer renderer, bool checksummed) {
  memset(hook, 0, sizeof *hook);
  video_init(&hook->video);
  hook->renderer = renderer;
  hook->checksummed = checksummed;
  hook->checksum = 14695981039346656037ull;
  ppu->drawLine = video_ppu_line;
  ppu->drawUser = hook;
}

bool video_hook_report(const VideoHook* hook, FILE* to) {
  static const char* const NAME[] = {"native", "emulated", "check"};
  fprintf(to, "Drawing: %s; %ld lines drawn here, %ld left to the PPU.\n", NAME[hook->renderer],
          hook->lines, hook->renderer == VIDEO_EMULATED ? hook->left : hook->declined);
  for (int i = 0; i < VIDEO_HOOK_REASONS && hook->reason[i].why; i++)
    fprintf(to, "  %ld lines left for %s\n", hook->reason[i].lines, hook->reason[i].why);
  if (hook->renderer == VIDEO_CHECK) {
    fprintf(to, "  %ld of them differ from the PPU's.\n", hook->differing);
    if (hook->differing)
      fprintf(to, "  The first: frame %u, line %d, column %d, %06X here and %06X from the PPU.\n",
              hook->first.frame, hook->first.line, hook->first.column, hook->first.native,
              hook->first.emulated);
  }
  if (hook->checksummed)
    fprintf(to, "  Picture checksum %016llX over %ld lines.\n",
            (unsigned long long)hook->checksum, hook->checksum_lines);
  return hook->differing == 0;
}

bool video_renderer_named(const char* name, VideoRenderer* renderer) {
  static const char* const NAME[] = {"native", "emulated", "check"};
  for (int i = 0; i < 3; i++)
    if (!strcmp(name, NAME[i])) {
      *renderer = (VideoRenderer)i;
      return true;
    }
  return false;
}
