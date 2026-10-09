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
_Static_assert(VIDEO_SPRITE_WORLD == (int)ppu_spriteWorld &&
                   VIDEO_SPRITE_ANCHORED == (int)ppu_spriteAnchored &&
                   VIDEO_SPRITE_CENTRED == (int)ppu_spriteCentred,
               "the sprites' places are numbered as the PPU numbers them");
_Static_assert(VIDEO_EXTRA_MAX == PPU_EXTRA_MAX && VIDEO_LINES == PPU_LINES &&
                   VIDEO_ROW_BYTES == PPU_ROW_BYTES,
               "the picture is as wide and as tall as the PPU's");
_Static_assert(VIDEO_PIXELS_XRGB == (int)ppu_pixelOutputFormatXBGR &&
                   VIDEO_PIXELS_RGBX == (int)ppu_pixelOutputFormatBGRX,
               "a pixel's unused byte is where the PPU puts it");

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
  s->obj = (VideoObj){
      .oam = ppu->oam,
      .high_oam = ppu->highOam,
      .sizes = ppu->objSize,
      .first = (uint8_t)(ppu->objPriority ? ppu->oamAdr >> 1 : 0),
      .tiles_at = {ppu->objTileAdr1, ppu->objTileAdr2},
      .interlace = ppu->objInterlace,
      .front = ppu->objFront,
      .remap_on = ppu->objRemapOn,
      .place = ppu->spritePlace,
      .shift = ppu->spriteShift,
      .remap = ppu->objRemap,
  };
  s->obj_pixel = ppu->objPixelBuffer;
  s->obj_priority = ppu->objPriorityBuffer;
  s->extra_left = ppu->extraLeft;
  s->extra_right = ppu->extraRight;
  s->clamp_lo = ppu->wideClampLo;
  s->clamp_hi = ppu->wideClampHi;
  s->line_hscroll = ppu->lineHScroll;
  s->line_vscroll = ppu->lineVScroll;
  s->line_window = ppu->lineWindow;
}

// Every register, as it is kept here and as the PPU keeps it.
#define REGISTERS(X)                                                                        \
  X(blank, forcedBlank) X(brightness, brightness)                                           \
  X(obj_sizes, objSize) X(obj_tiles_at[0], objTileAdr1) X(obj_tiles_at[1], objTileAdr2)     \
  X(obj_from_address, objPriority)                                                          \
  X(oam_at, oamAdr) X(oam_at_written, oamAdrWritten)                                        \
  X(oam_high, oamInHigh) X(oam_high_written, oamInHighWritten)                              \
  X(oam_second, oamSecondWrite) X(oam_first_byte, oamBuffer)                                \
  X(mode, mode) X(bg3_front, bg3priority)                                                   \
  X(mosaic_size, mosaicSize) X(mosaic_from, mosaicStartLine)                                \
  X(scroll_latch, scrollPrev) X(hscroll_latch, scrollPrev2)                                 \
  X(vram_at, vramPointer) X(vram_step, vramIncrement) X(vram_remap, vramRemapMode)          \
  X(vram_step_on_high, vramIncrementOnHigh) X(vram_ahead, vramReadBuffer)                   \
  X(m7_latch, m7prev) X(m7_large_field, m7largeField) X(m7_fill, m7charFill)                \
  X(m7_flip_x, m7xFlip) X(m7_flip_y, m7yFlip) X(m7_ext_bg, m7extBg)                         \
  X(cgram_at, cgramPointer) X(cgram_second, cgramSecondWrite)                               \
  X(cgram_first_byte, cgramBuffer)                                                          \
  X(window1_left, window1left) X(window1_right, window1right)                               \
  X(window2_left, window2left) X(window2_right, window2right)                               \
  X(direct_colour, directColor) X(add_sub, addSubscreen) X(subtract, subtractColor)         \
  X(half, halfColor) X(prevent, preventMathMode) X(clip, clipMode)                          \
  X(fixed_r, fixedColorR) X(fixed_g, fixedColorG) X(fixed_b, fixedColorB)                   \
  X(interlace, interlace) X(obj_interlace, objInterlace) X(overscan, overscan)              \
  X(pseudo_hires, pseudoHires)                                                              \
  X(even_frame, evenFrame) X(frame_overscan, frameOverscan)                                 \
  X(frame_interlace, frameInterlace) X(range_over, rangeOver) X(time_over, timeOver)        \
  X(h_count, hCount) X(v_count, vCount) X(h_second, hCountSecond)                           \
  X(v_second, vCountSecond) X(latched, countersLatched)                                     \
  X(bus1, ppu1openBus) X(bus2, ppu2openBus)                                                 \
  for (int i = 0; i < 4; i++) {                                                             \
    X(bg[i].hscroll, bgLayer[i].hScroll) X(bg[i].vscroll, bgLayer[i].vScroll)               \
    X(bg[i].map_at, bgLayer[i].tilemapAdr) X(bg[i].tiles_at, bgLayer[i].tileAdr)            \
    X(bg[i].map_wide, bgLayer[i].tilemapWider) X(bg[i].map_high, bgLayer[i].tilemapHigher)  \
    X(bg[i].big_tiles, bgLayer[i].bigTiles) X(bg[i].mosaic, bgLayer[i].mosaicEnabled)       \
  }                                                                                         \
  for (int i = 0; i < 5; i++) {                                                             \
    X(main[i], layer[i].mainScreenEnabled) X(sub[i], layer[i].subScreenEnabled)             \
    X(main_windowed[i], layer[i].mainScreenWindowed)                                        \
    X(sub_windowed[i], layer[i].subScreenWindowed)                                          \
  }                                                                                         \
  for (int i = 0; i < VIDEO_LAYERS; i++) {                                                  \
    X(window[i].one, windowLayer[i].window1enabled)                                         \
    X(window[i].two, windowLayer[i].window2enabled)                                         \
    X(window[i].one_inverted, windowLayer[i].window1inversed)                               \
    X(window[i].two_inverted, windowLayer[i].window2inversed)                               \
    X(window[i].logic, windowLayer[i].maskLogic) X(math[i], mathEnabled[i])                 \
  }                                                                                         \
  for (int i = 0; i < 8; i++) { X(m7[i], m7matrix[i]) }

void video_registers_from_ppu(VideoRegisters* r, const Ppu* ppu) {
#define TAKE(here, theirs) r->here = ppu->theirs;
  REGISTERS(TAKE)
#undef TAKE
}

const char* video_registers_differ(const VideoRegisters* r, const Ppu* ppu) {
#define SAME(here, theirs) \
  if (r->here != ppu->theirs) return #here;
  REGISTERS(SAME)
#undef SAME
  return NULL;
}

// What is noted of a frame other than its lines, as it is kept here and as
// the PPU keeps it.
#define NOTES(X)                                                                  \
  X(window_raster, windowRaster)                                                  \
  X(mid_frame_write, midFrameWrite) X(mid_frame_writes, midFrameWrites)           \
  for (int i = 0; i < 4; i++) {                                                   \
    X(edge_empty[i], layerEdgeEmpty[i]) X(scrolled[i], layerScrolled[i])          \
    X(raster[i], layerRaster[i])                                                  \
    X(last_hscroll[i], lastHScroll[i]) X(hscroll_writes[i], hScrollWrites[i])     \
  }

void video_notes_from_ppu(VideoFrame* f, const Ppu* ppu) {
#define TAKE(here, theirs) f->here = ppu->theirs;
  NOTES(TAKE)
#undef TAKE
  f->mid_frame_address = ppu->midFrameAdr;
  f->mid_frame_line = ppu->midFrameLine;
  memcpy(f->line_hscroll, ppu->lineHScroll, sizeof f->line_hscroll);
  memcpy(f->line_vscroll, ppu->lineVScroll, sizeof f->line_vscroll);
  memcpy(f->line_window, ppu->lineWindow, sizeof f->line_window);
}

const char* video_notes_differ(const VideoFrame* f, const Ppu* ppu, int line) {
#define SAME(here, theirs) \
  if (f->here != ppu->theirs) return #here;
  NOTES(SAME)
#undef SAME
  // Which write was the first means something only once there has been one.
  if (f->mid_frame_write && f->mid_frame_address != ppu->midFrameAdr) return "mid_frame_address";
  if (f->mid_frame_write && f->mid_frame_line != ppu->midFrameLine) return "mid_frame_line";
  if (line < 1 || line >= VIDEO_LINES) return NULL;
  for (int l = 0; l < 4; l++) {
    if (f->line_hscroll[l][line] != ppu->lineHScroll[l][line]) return "line_hscroll";
    if (f->line_vscroll[l][line] != ppu->lineVScroll[l][line]) return "line_vscroll";
  }
  if (memcmp(f->line_window[line], ppu->lineWindow[line], sizeof f->line_window[line]))
    return "line_window";
  return NULL;
}

static void picture_from_ppu(VideoPicture* p, const Ppu* ppu) {
  p->extra_left = ppu->extraLeft;
  p->extra_right = ppu->extraRight;
  memcpy(p->wide, ppu->layerWide, sizeof p->wide);
  p->clamp_lo = ppu->wideClampLo;
  p->clamp_hi = ppu->wideClampHi;
  memcpy(p->place, ppu->spritePlace, sizeof p->place);
  memcpy(p->shift, ppu->spriteShift, sizeof p->shift);
  memcpy(p->front, ppu->objFront, sizeof p->front);
  memcpy(p->remap_on, ppu->objRemapOn, sizeof p->remap_on);
  memcpy(p->remap, ppu->objRemap, sizeof p->remap);
}

// ...and what of the picture the PPU was not told as it is said here.
static const char* picture_differs(const VideoPicture* p, const Ppu* ppu) {
  if (p->extra_left != ppu->extraLeft || p->extra_right != ppu->extraRight) return "the margins";
  if (memcmp(p->wide, ppu->layerWide, sizeof p->wide)) return "a layer's policy";
  if (p->clamp_lo != ppu->wideClampLo || p->clamp_hi != ppu->wideClampHi) return "the clamp";
  if (memcmp(p->place, ppu->spritePlace, sizeof p->place)) return "a sprite's place";
  if (memcmp(p->shift, ppu->spriteShift, sizeof p->shift)) return "a sprite's shift";
  if (memcmp(p->front, ppu->objFront, sizeof p->front)) return "a sprite in front";
  if (memcmp(p->remap_on, ppu->objRemapOn, sizeof p->remap_on)) return "a sprite's colours";
  if (memcmp(p->remap, ppu->objRemap, sizeof p->remap)) return "the colours a sprite is drawn in";
  return NULL;
}

// What a line is drawn from: the registers, the memories, the notes and the
// picture kept here if they are, and the PPU's if not. The line's sprites
// are in the two rows kept here either way.
static void state_of(const VideoHook* hook, VideoState* s, const Ppu* ppu) {
  if (hook->registers_kept) {
    video_registers_state(&hook->registers, s);
    video_frame_state(&hook->frame, &hook->picture, s);
  } else {
    video_state_from_ppu(s, ppu);
  }
  s->obj_pixel = hook->obj_pixel;
  s->obj_priority = hook->obj_priority;
}

// Which of the two fields the frame being drawn is.
static bool even_frame(const VideoHook* hook, const Ppu* ppu) {
  return hook->registers_kept ? hook->registers.even_frame : ppu->evenFrame;
}

// A line's place in a picture's buffer, this one's or the PPU's: they are
// laid out the same. Every column is eight bytes, the pixel twice, each as
// blue, green, red and a byte that is never written, which the format puts
// first or last.
static size_t row_at(bool even, int line) {
  return (size_t)((line - 1) + (even ? 0 : VIDEO_FIELD_ROWS)) * VIDEO_ROW_BYTES;
}

static uint32_t row_colour(VideoPixels format, const uint8_t* row, int column) {
  const uint8_t* p = row + column * 8 + format;
  return (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

// Whether both of a column's pixels are `colour`.
static bool row_is(VideoPixels format, const uint8_t* row, int column, uint32_t colour) {
  const uint8_t* p = row + column * 8 + format;
  return row_colour(format, row, column) == colour &&
         ((uint32_t)p[6] << 16 | (uint32_t)p[5] << 8 | p[4]) == colour;
}

static void row_put(VideoPixels format, uint8_t* row, const uint32_t* colours, int width) {
  for (int c = 0; c < width; c++) {
    uint8_t* p = row + c * 8 + format;
    p[0] = p[4] = (uint8_t)colours[c];
    p[1] = p[5] = (uint8_t)(colours[c] >> 8);
    p[2] = p[6] = (uint8_t)(colours[c] >> 16);
  }
}

// FNV-1a, a line at a time: the line's number and each column's colour.
static void checksum_line(VideoHook* hook, const uint8_t* row, int line, int width) {
  if (!hook->checksummed) return;
  uint64_t h = hook->checksum;
  h = (h ^ (uint64_t)line) * 1099511628211ull;
  for (int c = 0; c < width; c++)
    h = (h ^ row_colour(hook->format, row, c)) * 1099511628211ull;
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

static VideoHook* hook_of(const Ppu* ppu) { return (VideoHook*)ppu->drawUser; }

const VideoRegisters* video_registers_of(const Ppu* ppu) { return &hook_of(ppu)->registers; }
const VideoPicture* video_picture_of(const Ppu* ppu) { return &hook_of(ppu)->picture; }
const VideoFrame* video_frame_of(const Ppu* ppu) { return &hook_of(ppu)->frame; }

void video_state_of(VideoState* s, const Ppu* ppu) { state_of(hook_of(ppu), s, ppu); }

// --- What the frontend says of the picture --------------------------------------
//
// Said here, and to the PPU: it draws the lines `src/video` is checked
// against.

void video_set_scroll(Ppu* ppu, int layer, int h, int v) {
  if (layer < VIDEO_BG1 || layer > VIDEO_BG4) return;
  VideoBg* bg = &hook_of(ppu)->registers.bg[layer];
  bg->hscroll = (uint16_t)(h & 0x3ff);
  bg->vscroll = (uint16_t)(v & 0x3ff);
  ppu_setScroll(ppu, layer, bg->hscroll, bg->vscroll);
}

void video_set_sprite(Ppu* ppu, int sprite, int x, int y, uint16_t word, bool large) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  VideoRegisters* r = &hook_of(ppu)->registers;
  video_put_sprite(r, sprite, x, y, word, large);
  ppu->oam[sprite * 2] = r->oam[sprite * 2];
  ppu->oam[sprite * 2 + 1] = r->oam[sprite * 2 + 1];
  ppu->highOam[sprite >> 2] = r->high_oam[sprite >> 2];
}

void video_set_vram(Ppu* ppu, uint16_t at, uint16_t word) {
  video_put_vram(&hook_of(ppu)->registers, at, word);
  ppu->vram[at & 0x7fff] = word;
}

void video_set_colour(Ppu* ppu, int index, uint16_t colour) {
  VideoRegisters* r = &hook_of(ppu)->registers;
  video_put_colour(r, index, colour);
  ppu->cgram[index & 0xff] = r->cgram[index & 0xff];
}

void video_set_margins(Ppu* ppu, int left, int right) {
  VideoPicture* p = &hook_of(ppu)->picture;
  p->extra_left = left < 0 ? 0 : left > VIDEO_EXTRA_MAX ? VIDEO_EXTRA_MAX : left;
  p->extra_right = right < 0 ? 0 : right > VIDEO_EXTRA_MAX ? VIDEO_EXTRA_MAX : right;
  ppu_setWidescreen(ppu, p->extra_left, p->extra_right);
}

void video_set_wide(Ppu* ppu, int layer, VideoWide policy) {
  if (layer < VIDEO_BG1 || layer > VIDEO_OBJ) return;
  hook_of(ppu)->picture.wide[layer] = (uint8_t)policy;
  ppu_setLayerWide(ppu, layer, policy);
}

void video_set_clamp(Ppu* ppu, int lo, int hi) {
  VideoPicture* p = &hook_of(ppu)->picture;
  p->clamp_lo = lo;
  p->clamp_hi = hi;
  ppu_setWideClamp(ppu, lo, hi);
}

void video_set_sprite_place(Ppu* ppu, int sprite, VideoSpritePlace place) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  hook_of(ppu)->picture.place[sprite] = ppu->spritePlace[sprite] = (uint8_t)place;
}

void video_set_sprite_shift(Ppu* ppu, int sprite, int shift) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  hook_of(ppu)->picture.shift[sprite] = ppu->spriteShift[sprite] = (int16_t)shift;
}

void video_set_sprite_front(Ppu* ppu, int sprite, bool front) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  hook_of(ppu)->picture.front[sprite] = ppu->objFront[sprite] = front;
}

void video_set_sprite_remapped(Ppu* ppu, int sprite, bool remapped) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  hook_of(ppu)->picture.remap_on[sprite] = ppu->objRemapOn[sprite] = remapped;
}

void video_set_remap(Ppu* ppu, const uint8_t remap[16]) {
  memcpy(hook_of(ppu)->picture.remap, remap, 16);
  memcpy(ppu->objRemap, remap, 16);
}

// Called for every line of the picture, with the PPU's row of pixels
// cleared. A line that is blanked has no sprites and none are looked for, so
// neither flag can be set by it. Which lines those are is decided here from
// the registers kept here, and for the PPU's finding from its own.
static bool video_ppu_sprites(void* user, Ppu* ppu, int line) {
  VideoHook* hook = (VideoHook*)user;
  VideoRegisters* r = &hook->registers;
  VideoState s;
  state_of(hook, &s, ppu);
  const int width = video_width(&s);
  memset(hook->obj_pixel, 0, (size_t)width);
  if (hook->renderer == VIDEO_EMULATED || video_sprites_declines(&s)) {
    if (hook->renderer == VIDEO_EMULATED ? ppu->forcedBlank : s.blank) return true;
    hook->sprite_declined++;
    ppu_findSprites(ppu, line);
    memcpy(hook->obj_pixel, ppu->objPixelBuffer, (size_t)width);
    memcpy(hook->obj_priority, ppu->objPriorityBuffer, (size_t)width);
    r->range_over = ppu->rangeOver;
    r->time_over = ppu->timeOver;
    return true;
  }
  int over = 0;
  if (!s.blank) {
    hook->sprite_lines++;
    over = video_sprites(&s, line, hook->obj_pixel, hook->obj_priority);
  }
  if (hook->renderer == VIDEO_NATIVE) {
    if (over & VIDEO_SPRITES_RANGE_OVER) ppu->rangeOver = r->range_over = true;
    if (over & VIDEO_SPRITES_TIME_OVER) ppu->timeOver = r->time_over = true;
    return true;
  }
  // Both. A priority is compared only where there is a sprite: elsewhere it
  // is whatever an earlier line left.
  const bool range_over = ppu->rangeOver || (over & VIDEO_SPRITES_RANGE_OVER);
  const bool time_over = ppu->timeOver || (over & VIDEO_SPRITES_TIME_OVER);
  r->range_over = range_over;
  r->time_over = time_over;
  if (!ppu->forcedBlank) ppu_findSprites(ppu, line);
  bool same = range_over == ppu->rangeOver && time_over == ppu->timeOver;
  for (int c = 0; c < width && same; c++)
    same = hook->obj_pixel[c] == ppu->objPixelBuffer[c] &&
           (hook->obj_pixel[c] == 0 || hook->obj_priority[c] == ppu->objPriorityBuffer[c]);
  if (!same) {
    if (hook->sprite_differing == 0) {
      hook->first_sprites.frame = ppu->snes ? ppu->snes->frames : 0;
      hook->first_sprites.line = line;
    }
    hook->sprite_differing++;
    // The line is then drawn from the PPU's, so that a line counted as
    // differing is one that was drawn wrong and not one whose sprites were.
    memcpy(hook->obj_pixel, ppu->objPixelBuffer, (size_t)width);
    memcpy(hook->obj_priority, ppu->objPriorityBuffer, (size_t)width);
  }
  return true;
}

static bool video_ppu_line(void* user, Ppu* ppu, int line) {
  VideoHook* hook = (VideoHook*)user;
  VideoState s;
  state_of(hook, &s, ppu);
  const int width = video_width(&s);
  const bool even = even_frame(hook, ppu);
  uint8_t* row = &hook->pixels[row_at(even, line)];
  const uint8_t* ppu_row = &ppu->pixelBuffer[row_at(ppu->evenFrame, line)];
  const char* why = hook->renderer == VIDEO_EMULATED ? NULL : video_declines(&s);
  if (hook->renderer == VIDEO_EMULATED || why != NULL) {
    if (why != NULL) note_decline(hook, why);
    else hook->left++;
    ppu_drawLine(ppu, line);
    memcpy(row, ppu_row, (size_t)width * 8);
    checksum_line(hook, row, line, width);
    return true;
  }
  uint32_t colours[VIDEO_MAX_WIDTH];
  video_line(&hook->video, &s, &hook->frame.centre, line, colours);
  hook->lines++;
  if (hook->renderer == VIDEO_CHECK) {
    ppu_drawLine(ppu, line);
    for (int c = 0; c < width; c++) {
      if (row_is(hook->format, ppu_row, c, colours[c])) continue;
      if (hook->differing == 0) {
        hook->first.frame = ppu->snes ? ppu->snes->frames : 0;
        hook->first.line = line;
        hook->first.column = c - s.extra_left;
        hook->first.native = colours[c];
        hook->first.emulated = row_colour(hook->format, ppu_row, c);
      }
      hook->differing++;
      break;
    }
    memcpy(row, ppu_row, (size_t)width * 8);
  } else {
    row_put(hook->format, row, colours, width);
  }
  checksum_line(hook, row, line, width);
  return true;
}

void video_set_pixel_format(Ppu* ppu, VideoPixels format) {
  hook_of(ppu)->format = format;
  ppu_setPixelOutputFormat(ppu, format);
}

int video_output_width(const Ppu* ppu) {
  const VideoPicture* p = &hook_of(ppu)->picture;
  return 2 * (256 + p->extra_left + p->extra_right);
}

void video_put_pixels(const Ppu* ppu, uint8_t* pixels) {
  const VideoHook* hook = hook_of(ppu);
  const VideoRegisters* r = &hook->registers;
  const size_t pitch = (size_t)video_output_width(ppu) * 4;
  const int lines = r->frame_overscan ? 239 : 224, top = r->frame_overscan ? 2 : 16;
  for (int line = 1; line <= lines; line++) {
    // An interlaced frame is both fields, a row from each. Any other is the
    // field just drawn, each row twice.
    const bool first = r->frame_interlace || r->even_frame;
    const bool second = !r->frame_interlace && r->even_frame;
    uint8_t* to = pixels + (size_t)(top + (line - 1) * 2) * pitch;
    memcpy(to, &hook->pixels[row_at(first, line)], pitch);
    memcpy(to + pitch, &hook->pixels[row_at(second, line)], pitch);
  }
  memset(pixels, 0, (size_t)top * pitch);
  if (!r->frame_overscan) memset(pixels + 464 * pitch, 0, 16 * pitch);
}

static void memory_from_ppu(VideoMemory* m, const Ppu* ppu) {
  memcpy(m->vram, ppu->vram, sizeof m->vram);
  memcpy(m->cgram, ppu->cgram, sizeof m->cgram);
  memcpy(m->oam, ppu->oam, sizeof m->oam);
  memcpy(m->high_oam, ppu->highOam, sizeof m->high_oam);
}

static const char* memory_differs(const VideoMemory* m, const Ppu* ppu) {
  if (memcmp(m->vram, ppu->vram, sizeof m->vram)) return "VRAM";
  if (memcmp(m->cgram, ppu->cgram, sizeof m->cgram)) return "the palette";
  if (memcmp(m->oam, ppu->oam, sizeof m->oam)) return "OAM";
  if (memcmp(m->high_oam, ppu->highOam, sizeof m->high_oam)) return "OAM's extra bits";
  return NULL;
}

// Under `VIDEO_CHECK`, after something has been done to the registers and
// to the PPU: are they the same? `misread` is for a read whose two answers
// were not, and `memories` has the three memories compared whole as well.
// What differs is made the PPU's again, so that what is counted is the
// things done wrong and not everything after the first.
static void check_registers(VideoHook* hook, Ppu* ppu, const char* what, uint8_t address,
                            uint8_t value, bool misread, bool memories) {
  if (hook->renderer != VIDEO_CHECK) return;
  const char* which = video_registers_differ(&hook->registers, ppu);
  if (which == NULL && misread) which = "the value read";
  if (which == NULL && memories) {
    which = memory_differs(&hook->memory, ppu);
    if (which != NULL) memory_from_ppu(&hook->memory, ppu);
  }
  if (which == NULL) return;
  if (hook->registers_differing++ == 0) {
    hook->first_register.frame = ppu->snes->frames;
    hook->first_register.line = ppu->snes->vPos;
    hook->first_register.what = what;
    hook->first_register.address = address;
    hook->first_register.value = value;
    hook->first_register.which = which;
  }
  video_registers_from_ppu(&hook->registers, ppu);
}

// ...and the same of the notes, and at a frame's top of the picture, which
// the PPU is told as it is said. `line` is the line that began, 0 for a
// frame's top, or -1.
static void check_notes(VideoHook* hook, Ppu* ppu, int line) {
  if (hook->renderer != VIDEO_CHECK) return;
  const char* which = video_notes_differ(&hook->frame, ppu, line);
  if (which == NULL && line == 0) which = picture_differs(&hook->picture, ppu);
  if (which == NULL) return;
  if (hook->notes_differing++ == 0) {
    hook->first_note.frame = ppu->snes->frames;
    hook->first_note.line = ppu->snes->vPos;
    hook->first_note.which = which;
  }
  video_notes_from_ppu(&hook->frame, ppu);
  picture_from_ppu(&hook->picture, ppu);
}

static void video_ppu_wrote(void* user, Ppu* ppu, uint8_t address, uint8_t value) {
  VideoHook* hook = (VideoHook*)user;
  const Snes* snes = ppu->snes;
  video_frame_write(&hook->frame, &hook->registers, address, value, snes->vPos,
                    !snes->inVblank && snes->vPos > 0);
  hook->writes++;
  check_registers(hook, ppu, "write", address, value, false, false);
  check_notes(hook, ppu, -1);
}

static void video_ppu_began_line(void* user, Ppu* ppu, int line) {
  VideoHook* hook = (VideoHook*)user;
  video_frame_line(&hook->frame, &hook->registers, line);
  hook->noted_lines++;
  check_notes(hook, ppu, line);
}

static void video_ppu_read(void* user, Ppu* ppu, uint8_t address, uint8_t* value) {
  VideoHook* hook = (VideoHook*)user;
  const VideoBus bus = {
      .dot = ppu->snes->hPos / 4,
      .line = ppu->snes->vPos,
      .pal = ppu->snes->palTiming,
      .open_bus = ppu->snes->openBus,
  };
  const uint8_t here = video_registers_read(&hook->registers, address, &bus);
  hook->reads++;
  check_registers(hook, ppu, "read", address, *value, here != *value, false);
  if (hook->renderer == VIDEO_NATIVE) *value = here;
}

static void video_ppu_happened(void* user, Ppu* ppu, int what) {
  VideoHook* hook = (VideoHook*)user;
  VideoRegisters* r = &hook->registers;
  const char* name = "";
  switch (what) {
    case ppu_wasReset:
      video_registers_reset(r);
      video_picture_reset(&hook->picture);
      memset(hook->obj_pixel, 0, sizeof hook->obj_pixel);
      memset(hook->obj_priority, 0, sizeof hook->obj_priority);
      memset(hook->pixels, 0, sizeof hook->pixels);
      name = "reset";
      break;
    case ppu_frameStarted:
      video_registers_frame_start(r);
      video_frame_start(&hook->frame, r, &hook->picture);
      check_notes(hook, ppu, 0);
      name = "frame's start";
      break;
    case ppu_overscanChecked: video_registers_overscan(r); name = "overscan's check"; break;
    case ppu_vblankBegan: video_registers_vblank(r); name = "picture's end"; break;
    // A state is the PPU's, whole.
    case ppu_stateLoaded:
      video_registers_from_ppu(r, ppu);
      memory_from_ppu(&hook->memory, ppu);
      return;
  }
  check_registers(hook, ppu, name, 0, 0, false, true);
}

void video_hook_keep_registers(VideoHook* hook, Ppu* ppu) {
  VideoRegisters* r = &hook->registers;
  video_registers_attach(r, &hook->memory);
  memory_from_ppu(&hook->memory, ppu);
  video_registers_from_ppu(r, ppu);
  video_notes_from_ppu(&hook->frame, ppu);
  picture_from_ppu(&hook->picture, ppu);
  hook->registers_kept = true;
  ppu->wrote = video_ppu_wrote;
  ppu->didRead = video_ppu_read;
  ppu->happened = video_ppu_happened;
  ppu->beganLine = video_ppu_began_line;
}

void video_hook_install(VideoHook* hook, Ppu* ppu, VideoRenderer renderer, bool checksummed) {
  memset(hook, 0, sizeof *hook);
  video_init(&hook->video);
  video_picture_init(&hook->picture);
  video_frame_init(&hook->frame);
  hook->renderer = renderer;
  hook->format = (VideoPixels)ppu->pixelOutputFormat;
  hook->checksummed = checksummed;
  hook->checksum = 14695981039346656037ull;
  ppu->drawLine = video_ppu_line;
  ppu->findSprites = video_ppu_sprites;
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
  fprintf(to, "Sprites: %ld lines' found here, %ld left to the PPU.\n", hook->sprite_lines,
          hook->sprite_declined);
  if (hook->renderer == VIDEO_CHECK) {
    fprintf(to, "  %ld of those are not what the PPU found.\n", hook->sprite_differing);
    if (hook->sprite_differing)
      fprintf(to, "  The first: frame %u, line %d.\n", hook->first_sprites.frame,
              hook->first_sprites.line);
  }
  if (hook->registers_kept) {
    fprintf(to, "Registers: %ld writes and %ld reads kept here.\n", hook->writes, hook->reads);
    if (hook->renderer == VIDEO_CHECK)
      fprintf(to, "  %ld of them, or of a frame's events, were not as the PPU had them.\n",
              hook->registers_differing);
    if (hook->registers_differing) {
      const char* what = hook->first_register.what;
      fprintf(to, "  The first: frame %u, line %d, `%s` after ", hook->first_register.frame,
              hook->first_register.line, hook->first_register.which);
      if (!strcmp(what, "write"))
        fprintf(to, "%02X was written to $21%02X.\n", hook->first_register.value,
                hook->first_register.address);
      else if (!strcmp(what, "read"))
        fprintf(to, "$21%02X was read, as %02X by the PPU.\n", hook->first_register.address,
                hook->first_register.value);
      else
        fprintf(to, "the %s.\n", what);
    }
    fprintf(to, "Notes: %ld lines' scrolls and windows kept here.\n", hook->noted_lines);
    if (hook->renderer == VIDEO_CHECK)
      fprintf(to, "  %ld writes, lines or frames' tops left them not as the PPU had its own.\n",
              hook->notes_differing);
    if (hook->notes_differing)
      fprintf(to, "  The first: frame %u, line %d, `%s`.\n", hook->first_note.frame,
              hook->first_note.line, hook->first_note.which);
  }
  if (hook->checksummed)
    fprintf(to, "  Picture checksum %016llX over %ld lines.\n",
            (unsigned long long)hook->checksum, hook->checksum_lines);
  return hook->differing == 0 && hook->sprite_differing == 0 &&
         hook->registers_differing == 0 && hook->notes_differing == 0;
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
