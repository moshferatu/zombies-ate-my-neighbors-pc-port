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

// What a line is drawn from: the registers kept here if they are, and the
// PPU's if not, with what the frontend has said of the picture, which is in
// the PPU either way.
static void state_of(const VideoHook* hook, VideoState* s, const Ppu* ppu) {
  video_state_from_ppu(s, ppu);
  if (hook->registers_kept) video_registers_state(&hook->registers, s);
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

const VideoRegisters* video_registers_of(const Ppu* ppu) {
  return &((const VideoHook*)ppu->drawUser)->registers;
}

void video_state_of(VideoState* s, const Ppu* ppu) {
  state_of((const VideoHook*)ppu->drawUser, s, ppu);
}

static bool video_ppu_sprites(void* user, Ppu* ppu, int line) {
  VideoHook* hook = (VideoHook*)user;
  VideoRegisters* r = &hook->registers;
  VideoState s;
  state_of(hook, &s, ppu);
  if (hook->renderer == VIDEO_EMULATED || video_sprites_declines(&s)) {
    hook->sprite_declined++;
    ppu_findSprites(ppu, line);
    r->range_over = ppu->rangeOver;
    r->time_over = ppu->timeOver;
    return true;
  }
  hook->sprite_lines++;
  if (hook->renderer == VIDEO_NATIVE) {
    const int over = video_sprites(&s, line, ppu->objPixelBuffer, ppu->objPriorityBuffer);
    if (over & VIDEO_SPRITES_RANGE_OVER) ppu->rangeOver = r->range_over = true;
    if (over & VIDEO_SPRITES_TIME_OVER) ppu->timeOver = r->time_over = true;
    return true;
  }
  // Both, and the PPU's kept. A priority is compared only where there is a
  // sprite: elsewhere it is whatever an earlier line left.
  uint8_t pixel[VIDEO_MAX_WIDTH], priority[VIDEO_MAX_WIDTH];
  const int over = video_sprites(&s, line, pixel, priority);
  const bool range_over = ppu->rangeOver || (over & VIDEO_SPRITES_RANGE_OVER);
  const bool time_over = ppu->timeOver || (over & VIDEO_SPRITES_TIME_OVER);
  r->range_over = range_over;
  r->time_over = time_over;
  ppu_findSprites(ppu, line);
  bool same = range_over == ppu->rangeOver && time_over == ppu->timeOver;
  for (int c = 0; c < video_width(&s) && same; c++)
    same = pixel[c] == ppu->objPixelBuffer[c] &&
           (pixel[c] == 0 || priority[c] == ppu->objPriorityBuffer[c]);
  if (!same) {
    if (hook->sprite_differing == 0) {
      hook->first_sprites.frame = ppu->snes ? ppu->snes->frames : 0;
      hook->first_sprites.line = line;
    }
    hook->sprite_differing++;
  }
  return true;
}

static bool video_ppu_line(void* user, Ppu* ppu, int line) {
  VideoHook* hook = (VideoHook*)user;
  VideoState s;
  state_of(hook, &s, ppu);
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

// Under `VIDEO_CHECK`, after something has been done to the registers and
// to the PPU: are they the same? `misread` is for a read whose two answers
// were not. Registers that differ are made the PPU's again, so that what is
// counted is the things done wrong and not everything after the first.
static void check_registers(VideoHook* hook, Ppu* ppu, const char* what, uint8_t address,
                            uint8_t value, bool misread) {
  if (hook->renderer != VIDEO_CHECK) return;
  const char* which = video_registers_differ(&hook->registers, ppu);
  if (which == NULL && misread) which = "the value read";
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

static void video_ppu_wrote(void* user, Ppu* ppu, uint8_t address, uint8_t value) {
  VideoHook* hook = (VideoHook*)user;
  video_registers_write(&hook->registers, address, value, ppu->snes->vPos);
  hook->writes++;
  check_registers(hook, ppu, "write", address, value, false);
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
  check_registers(hook, ppu, "read", address, *value, here != *value);
  if (hook->renderer == VIDEO_NATIVE) *value = here;
}

static void video_ppu_happened(void* user, Ppu* ppu, int what) {
  VideoHook* hook = (VideoHook*)user;
  VideoRegisters* r = &hook->registers;
  const char* name = "";
  switch (what) {
    case ppu_wasReset: video_registers_reset(r); name = "reset"; break;
    case ppu_frameStarted: video_registers_frame_start(r); name = "frame's start"; break;
    case ppu_overscanChecked: video_registers_overscan(r); name = "overscan's check"; break;
    case ppu_vblankBegan: video_registers_vblank(r); name = "picture's end"; break;
    // A state is the PPU's, whole, and a scroll the frontend put in is the
    // frontend's.
    case ppu_stateLoaded: video_registers_from_ppu(r, ppu); return;
    case ppu_scrollSet:
      for (int l = 0; l < 4; l++) {
        r->bg[l].hscroll = ppu->bgLayer[l].hScroll;
        r->bg[l].vscroll = ppu->bgLayer[l].vScroll;
      }
      return;
  }
  check_registers(hook, ppu, name, 0, 0, false);
}

void video_hook_keep_registers(VideoHook* hook, Ppu* ppu) {
  VideoRegisters* r = &hook->registers;
  r->vram = ppu->vram;
  r->cgram = ppu->cgram;
  r->oam = ppu->oam;
  r->high_oam = ppu->highOam;
  video_registers_from_ppu(r, ppu);
  hook->registers_kept = true;
  ppu->wrote = video_ppu_wrote;
  ppu->didRead = video_ppu_read;
  ppu->happened = video_ppu_happened;
}

void video_hook_install(VideoHook* hook, Ppu* ppu, VideoRenderer renderer, bool checksummed) {
  memset(hook, 0, sizeof *hook);
  video_init(&hook->video);
  hook->renderer = renderer;
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
  }
  if (hook->checksummed)
    fprintf(to, "  Picture checksum %016llX over %ld lines.\n",
            (unsigned long long)hook->checksum, hook->checksum_lines);
  return hook->differing == 0 && hook->sprite_differing == 0 && hook->registers_differing == 0;
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
