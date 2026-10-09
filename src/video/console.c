// See `console.h`.

#include "video/console.h"

#include <string.h>

#include "video/state.h"

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

static void registers_to_ppu(const VideoRegisters* r, Ppu* ppu) {
#define GIVE(here, theirs) ppu->theirs = r->here;
  REGISTERS(GIVE)
#undef GIVE
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

static void notes_to_ppu(const VideoFrame* f, Ppu* ppu) {
#define GIVE(here, theirs) ppu->theirs = f->here;
  NOTES(GIVE)
#undef GIVE
  ppu->midFrameAdr = f->mid_frame_address;
  ppu->midFrameLine = f->mid_frame_line;
  memcpy(ppu->lineHScroll, f->line_hscroll, sizeof f->line_hscroll);
  memcpy(ppu->lineVScroll, f->line_vscroll, sizeof f->line_vscroll);
  memcpy(ppu->lineWindow, f->line_window, sizeof f->line_window);
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

static void picture_to_ppu(const VideoPicture* p, Ppu* ppu) {
  ppu->extraLeft = p->extra_left;
  ppu->extraRight = p->extra_right;
  memcpy(ppu->layerWide, p->wide, sizeof p->wide);
  ppu->wideClampLo = p->clamp_lo;
  ppu->wideClampHi = p->clamp_hi;
  memcpy(ppu->spritePlace, p->place, sizeof p->place);
  memcpy(ppu->spriteShift, p->shift, sizeof p->shift);
  memcpy(ppu->objFront, p->front, sizeof p->front);
  memcpy(ppu->objRemapOn, p->remap_on, sizeof p->remap_on);
  memcpy(ppu->objRemap, p->remap, sizeof p->remap);
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

static void memory_to_ppu(const VideoMemory* m, Ppu* ppu) {
  memcpy(ppu->vram, m->vram, sizeof m->vram);
  memcpy(ppu->cgram, m->cgram, sizeof m->cgram);
  memcpy(ppu->oam, m->oam, sizeof m->oam);
  memcpy(ppu->highOam, m->high_oam, sizeof m->high_oam);
}

void video_console_take(VideoConsole* console) {
  VideoChip* chip = &console->chip;
  const Ppu* ppu = console->ppu;
  memory_from_ppu(&chip->memory, ppu);
  video_registers_from_ppu(&chip->registers, ppu);
  video_notes_from_ppu(&chip->frame, ppu);
  picture_from_ppu(&chip->picture, ppu);
}

// The PPU, if it is kept up beside the chip: told every write as it is made
// and everything the frontend says. NULL for a console's chip under
// `VIDEO_NATIVE`, whose PPU is told nothing until a line is left to it.
static Ppu* beside(const VideoConsole* console) {
  return console->renderer == VIDEO_NATIVE && console->snes != NULL ? NULL : console->ppu;
}

// A PPU that has been told nothing, told everything at once: the registers,
// the memories, the notes and the picture as the chip has them. For a line
// that is left to it after all. Nothing this game shows is, so what it costs
// -- the memories are copied whole -- is paid by no frame of it.
static void ppu_catch_up(const VideoChip* chip, Ppu* ppu) {
  registers_to_ppu(&chip->registers, ppu);
  memory_to_ppu(&chip->memory, ppu);
  notes_to_ppu(&chip->frame, ppu);
  picture_to_ppu(&chip->picture, ppu);
}

static uint32_t frame_of(const VideoConsole* console) {
  return console->snes != NULL ? console->snes->frames : 0;
}

// --- What the chip asks of somebody beside it (`VideoOther`) --------------------

// The frontend said or put something. The PPU is told if it is kept up: it
// draws the lines the chip's are checked against. How a pixel is handed over
// it is told either way: a line left to it is copied out of its picture as
// it is.
static void ppu_told(void* user, VideoSaid what, int which) {
  VideoConsole* console = (VideoConsole*)user;
  const VideoRegisters* r = &console->chip.registers;
  const VideoPicture* p = &console->chip.picture;
  if (what == VIDEO_SAID_PIXEL_FORMAT) {
    ppu_setPixelOutputFormat(console->ppu, console->chip.format);
    return;
  }
  Ppu* ppu = beside(console);
  if (ppu == NULL) return;
  switch (what) {
    case VIDEO_SAID_MARGINS:
      ppu_setWidescreen(ppu, p->extra_left, p->extra_right);
      break;
    case VIDEO_SAID_WIDE:
      ppu_setLayerWide(ppu, which, p->wide[which]);
      break;
    case VIDEO_SAID_CLAMP:
      ppu_setWideClamp(ppu, p->clamp_lo, p->clamp_hi);
      break;
    case VIDEO_SAID_OF_SPRITE:
      ppu->spritePlace[which] = p->place[which];
      ppu->spriteShift[which] = p->shift[which];
      ppu->objFront[which] = p->front[which];
      ppu->objRemapOn[which] = p->remap_on[which];
      break;
    case VIDEO_SAID_REMAP:
      memcpy(ppu->objRemap, p->remap, sizeof p->remap);
      break;
    case VIDEO_SAID_SCROLL:
      ppu_setScroll(ppu, which, r->bg[which].hscroll, r->bg[which].vscroll);
      break;
    case VIDEO_SAID_SPRITE:
      ppu->oam[which * 2] = r->oam[which * 2];
      ppu->oam[which * 2 + 1] = r->oam[which * 2 + 1];
      ppu->highOam[which >> 2] = r->high_oam[which >> 2];
      break;
    case VIDEO_SAID_VRAM:
      ppu->vram[which & 0x7fff] = r->vram[which & 0x7fff];
      break;
    case VIDEO_SAID_COLOUR:
      ppu->cgram[which & 0xff] = r->cgram[which & 0xff];
      break;
    default:
      break;
  }
}

// The sprites the PPU found on its last line, and the two flags as that left
// them, to the chip.
static void sprites_from_ppu(VideoChip* chip, const Ppu* ppu) {
  const size_t width = (size_t)video_chip_width(chip);
  memcpy(chip->obj_pixel, ppu->objPixelBuffer, width);
  memcpy(chip->obj_priority, ppu->objPriorityBuffer, width);
  chip->registers.range_over = ppu->rangeOver;
  chip->registers.time_over = ppu->timeOver;
}

// The line the PPU last drew, to the chip's row for it.
static void line_from_ppu(VideoChip* chip, const Ppu* ppu, int line) {
  memcpy(video_chip_row(chip, line), &ppu->pixelBuffer[video_row_at(ppu->evenFrame, line)],
         (size_t)video_chip_width(chip) * 8);
}

// Under `VIDEO_NATIVE`, a line whose sprites the chip does not find: the PPU
// finds them, once it has been told everything.
static void ppu_stands_in_for_sprites(void* user, int line) {
  VideoConsole* console = (VideoConsole*)user;
  VideoChip* chip = &console->chip;
  Ppu* ppu = console->ppu;
  ppu_catch_up(chip, ppu);
  memset(ppu->objPixelBuffer, 0, (size_t)video_chip_width(chip));
  ppu_findSprites(ppu, line);
  sprites_from_ppu(chip, ppu);
}

// ...and a line the chip does not draw. The PPU draws from its own two rows
// of sprites, which it may not have found.
static void ppu_stands_in_for_line(void* user, int line) {
  VideoConsole* console = (VideoConsole*)user;
  VideoChip* chip = &console->chip;
  Ppu* ppu = console->ppu;
  const size_t width = (size_t)video_chip_width(chip);
  ppu_catch_up(chip, ppu);
  memcpy(ppu->objPixelBuffer, chip->obj_pixel, width);
  memcpy(ppu->objPriorityBuffer, chip->obj_priority, width);
  ppu_drawLine(ppu, line);
  line_from_ppu(chip, ppu, line);
}

// --- The check ------------------------------------------------------------------

// Under `VIDEO_CHECK`, something done to the chip that was not as the PPU
// did it: what was done, and which register or memory it left different.
static void note_difference(VideoConsole* console, const char* what, uint8_t address,
                            uint8_t value, const char* which) {
  if (console->registers_differing++ != 0) return;
  console->first_register.frame = frame_of(console);
  console->first_register.line = console->snes != NULL ? console->snes->vPos : 0;
  console->first_register.what = what;
  console->first_register.address = address;
  console->first_register.value = value;
  console->first_register.which = which;
}

// Under `VIDEO_CHECK`, after something has been done to the chip and to the
// PPU: are their registers the same? `misread` is for a read whose two
// answers were not, and `memories` has the three memories compared whole as
// well. What differs is made the PPU's again, so that what is counted is the
// things done wrong and not everything after the first.
static void check_registers(VideoConsole* console, const char* what, uint8_t address,
                            uint8_t value, bool misread, bool memories) {
  if (console->renderer != VIDEO_CHECK) return;
  VideoChip* chip = &console->chip;
  const Ppu* ppu = console->ppu;
  const char* which = video_registers_differ(&chip->registers, ppu);
  if (which == NULL && misread) which = "the value read";
  if (which == NULL && memories) {
    which = memory_differs(&chip->memory, ppu);
    if (which != NULL) memory_from_ppu(&chip->memory, ppu);
  }
  if (which == NULL) return;
  note_difference(console, what, address, value, which);
  video_registers_from_ppu(&chip->registers, ppu);
}

// ...and the same of the notes, and at a frame's top of the picture, which
// the PPU is told as it is said. `line` is the line that began, 0 for a
// frame's top, or -1.
static void check_notes(VideoConsole* console, int line) {
  if (console->renderer != VIDEO_CHECK) return;
  VideoChip* chip = &console->chip;
  const Ppu* ppu = console->ppu;
  const char* which = video_notes_differ(&chip->frame, ppu, line);
  if (which == NULL && line == 0) which = picture_differs(&chip->picture, ppu);
  if (which == NULL) return;
  if (console->notes_differing++ == 0) {
    console->first_note.frame = frame_of(console);
    console->first_note.line = console->snes != NULL ? console->snes->vPos : 0;
    console->first_note.which = which;
  }
  video_notes_from_ppu(&chip->frame, ppu);
  picture_from_ppu(&chip->picture, ppu);
}

// --- A line the PPU runs --------------------------------------------------------
//
// Under `VIDEO_CHECK` and `VIDEO_EMULATED` the PPU runs each line, and calls
// these three as it does: the line begins, its sprites are to be found into
// the PPU's row for them, which it has cleared, and the line is to be drawn.

static void ppu_began_line(void* user, Ppu* ppu, int line) {
  (void)ppu;
  VideoConsole* console = (VideoConsole*)user;
  video_chip_begin_line(&console->chip, line);
  check_notes(console, line);
}

// A line that is blanked has no sprites and none are looked for. Which lines
// those are is decided by the chip from its registers, and for the PPU's
// finding from the PPU's.
static bool ppu_sprites(void* user, Ppu* ppu, int line) {
  VideoConsole* console = (VideoConsole*)user;
  VideoChip* chip = &console->chip;
  const int width = video_chip_width(chip);
  if (console->renderer == VIDEO_EMULATED) {
    memset(chip->obj_pixel, 0, (size_t)width);
    if (ppu->forcedBlank) return true;
    chip->sprite_declined++;
    ppu_findSprites(ppu, line);
    sprites_from_ppu(chip, ppu);
    return true;
  }
  if (!video_chip_find_sprites(chip, line)) {
    ppu_findSprites(ppu, line);
    sprites_from_ppu(chip, ppu);
    return true;
  }
  // Both. A priority is compared only where there is a sprite: elsewhere it
  // is whatever an earlier line left.
  if (!ppu->forcedBlank) ppu_findSprites(ppu, line);
  bool same = chip->registers.range_over == ppu->rangeOver &&
              chip->registers.time_over == ppu->timeOver;
  for (int c = 0; c < width && same; c++)
    same = chip->obj_pixel[c] == ppu->objPixelBuffer[c] &&
           (chip->obj_pixel[c] == 0 || chip->obj_priority[c] == ppu->objPriorityBuffer[c]);
  if (!same) {
    if (console->sprite_differing == 0) {
      console->first_sprites.frame = frame_of(console);
      console->first_sprites.line = line;
    }
    console->sprite_differing++;
    // The line is then drawn from the PPU's, so that a line counted as
    // differing is one that was drawn wrong and not one whose sprites were.
    memcpy(chip->obj_pixel, ppu->objPixelBuffer, (size_t)width);
    memcpy(chip->obj_priority, ppu->objPriorityBuffer, (size_t)width);
  }
  return true;
}

// The line shown, and the one the checksum is of, is the PPU's.
static bool ppu_line(void* user, Ppu* ppu, int line) {
  VideoConsole* console = (VideoConsole*)user;
  VideoChip* chip = &console->chip;
  const bool emulated = console->renderer == VIDEO_EMULATED;
  const bool drawn = !emulated && video_chip_draw_line(chip, line) == NULL;
  if (emulated) console->left++;
  ppu_drawLine(ppu, line);
  if (drawn) {
    const uint8_t* row = video_chip_row(chip, line);
    const uint8_t* ppu_row = &ppu->pixelBuffer[video_row_at(ppu->evenFrame, line)];
    const int width = video_chip_width(chip);
    for (int c = 0; c < width; c++) {
      const uint32_t colour = video_row_colour(chip->format, row, c);
      if (video_row_is(chip->format, ppu_row, c, colour)) continue;
      if (console->differing == 0) {
        console->first.frame = frame_of(console);
        console->first.line = line;
        console->first.column = c - chip->picture.extra_left;
        console->first.native = colour;
        console->first.emulated = video_row_colour(chip->format, ppu_row, c);
      }
      console->differing++;
      break;
    }
  }
  line_from_ppu(chip, ppu, line);
  video_chip_count_line(chip, line);
  return true;
}

// --- The console's video chip ---------------------------------------------------
//
// What a console asks of its chip (`SnesVideo`). Under `VIDEO_CHECK` and
// `VIDEO_EMULATED` each thing is done to the PPU first, and where the two
// could answer differently the PPU's answer is the one the console gets.

static void console_write(void* user, uint8_t address, uint8_t value) {
  VideoConsole* console = (VideoConsole*)user;
  const Snes* snes = console->snes;
  Ppu* ppu = beside(console);
  if (ppu != NULL) ppu_write(ppu, address, value);
  video_chip_write(&console->chip, address, value, snes->vPos,
                   !snes->inVblank && snes->vPos > 0);
  check_registers(console, "write", address, value, false, false);
  check_notes(console, -1);
}

static uint8_t console_read(void* user, uint8_t address) {
  VideoConsole* console = (VideoConsole*)user;
  const Snes* snes = console->snes;
  const VideoBus bus = {
      .dot = snes->hPos / 4,
      .line = snes->vPos,
      .pal = snes->palTiming,
      .open_bus = snes->openBus,
  };
  Ppu* ppu = beside(console);
  if (ppu == NULL) return video_chip_read(&console->chip, address, &bus);
  const uint8_t theirs = ppu_read(ppu, address);
  const uint8_t here = video_chip_read(&console->chip, address, &bus);
  check_registers(console, "read", address, theirs, here != theirs, false);
  return theirs;
}

static void console_reset(void* user) {
  VideoConsole* console = (VideoConsole*)user;
  Ppu* ppu = beside(console);
  if (ppu != NULL) ppu_reset(ppu);
  video_chip_reset(&console->chip);
  check_registers(console, "reset", 0, 0, false, true);
}

static void console_frame_start(void* user) {
  VideoConsole* console = (VideoConsole*)user;
  Ppu* ppu = beside(console);
  if (ppu != NULL) ppu_handleFrameStart(ppu);
  video_chip_frame_start(&console->chip);
  check_notes(console, 0);
  check_registers(console, "frame's start", 0, 0, false, true);
}

static bool console_overscan(void* user) {
  VideoConsole* console = (VideoConsole*)user;
  Ppu* ppu = beside(console);
  const bool here = video_chip_overscan(&console->chip);
  if (ppu == NULL) return here;
  const bool theirs = ppu_checkOverscan(ppu);
  check_registers(console, "overscan's check", 0, 0, false, true);
  return theirs;
}

static void console_vblank(void* user) {
  VideoConsole* console = (VideoConsole*)user;
  Ppu* ppu = beside(console);
  if (ppu != NULL) ppu_handleVblank(ppu);
  video_chip_vblank(&console->chip);
  check_registers(console, "picture's end", 0, 0, false, true);
}

// A line of the picture. The PPU, where it is kept up, runs it and calls
// back for each part of it; otherwise it is the chip's alone.
static void console_line(void* user, int line) {
  VideoConsole* console = (VideoConsole*)user;
  Ppu* ppu = beside(console);
  if (ppu != NULL) ppu_runLine(ppu, line);
  else video_chip_run_line(&console->chip, line);
}

// Which of the two fields the frame is, and whether it is interlaced: an odd
// field that is not is a little shorter, and an even one that is has a line
// more.
static bool console_even_frame(void* user) {
  const VideoConsole* console = (const VideoConsole*)user;
  const Ppu* ppu = beside(console);
  return ppu != NULL ? ppu->evenFrame : console->chip.registers.even_frame;
}

static bool console_frame_interlace(void* user) {
  const VideoConsole* console = (const VideoConsole*)user;
  const Ppu* ppu = beside(console);
  return ppu != NULL ? ppu->frameInterlace : console->chip.registers.frame_interlace;
}

// The chip's share of a saved state (`state.h`). Saved from the chip under
// `VIDEO_NATIVE`. Under the other two the PPU's is what is saved, and under
// `VIDEO_CHECK` what would have been saved from the chip is compared with
// it. A load is into both, each from the same bytes, and the check then
// compares the two as it does after anything else.
static void console_state(void* user, StateHandler* sh) {
  VideoConsole* console = (VideoConsole*)user;
  VideoChip* chip = &console->chip;
  Ppu* ppu = beside(console);
  if (ppu == NULL) {
    video_state_handle(&chip->registers, chip->obj_pixel, chip->obj_priority, sh, NULL);
    return;
  }
  const int from = sh->offset;
  ppu_handleState(ppu, sh);
  const int size = sh->offset - from;
  if (!sh->saving) {
    StateHandler again = *sh;
    again.offset = from;
    video_state_handle(&chip->registers, chip->obj_pixel, chip->obj_priority, &again, NULL);
    check_registers(console, "state's load", 0, 0, false, true);
  } else if (console->renderer == VIDEO_CHECK) {
    StateHandler* here = sh_init(true, NULL, 0);
    VideoStateParts parts;
    video_state_handle(&chip->registers, chip->obj_pixel, chip->obj_priority, here, &parts);
    const char* which = here->offset != size
                            ? "the state's size"
                            : video_states_differ(here->data, sh->data + from, size, &parts);
    if (which != NULL) note_difference(console, "state's save", 0, 0, which);
    sh_free(here);
  }
}

void video_console_attach(VideoConsole* console, Snes* snes) {
  video_console_take(console);
  console->snes = snes;
  const SnesVideo video = {
      .user = console,
      .reset = console_reset,
      .read = console_read,
      .write = console_write,
      .frameStart = console_frame_start,
      .checkOverscan = console_overscan,
      .vblank = console_vblank,
      .runLine = console_line,
      .handleState = console_state,
      .evenFrame = console_even_frame,
      .frameInterlace = console_frame_interlace,
  };
  snes_setVideo(snes, &video);
}

VideoChip* video_chip_of(const Snes* snes) { return &((VideoConsole*)snes->video.user)->chip; }

void video_console_install(VideoConsole* console, Ppu* ppu, VideoRenderer renderer,
                           bool checksummed) {
  memset(console, 0, sizeof *console);
  VideoChip* chip = &console->chip;
  video_chip_init(chip, checksummed);
  chip->format = (VideoPixels)ppu->pixelOutputFormat;
  chip->other = (VideoOther){
      .user = console,
      .said = ppu_told,
      .sprites = ppu_stands_in_for_sprites,
      .line = ppu_stands_in_for_line,
  };
  console->renderer = renderer;
  console->ppu = ppu;
  ppu->beganLine = ppu_began_line;
  ppu->findSprites = ppu_sprites;
  ppu->drawLine = ppu_line;
  ppu->drawUser = console;
}

bool video_console_report(const VideoConsole* console, FILE* to) {
  static const char* const NAME[] = {"native", "emulated", "check"};
  const VideoChip* chip = &console->chip;
  const bool check = console->renderer == VIDEO_CHECK;
  fprintf(to, "Drawing: %s; %ld lines drawn here, %ld left to the PPU.\n",
          NAME[console->renderer], chip->lines,
          console->renderer == VIDEO_EMULATED ? console->left : chip->declined);
  for (int i = 0; i < VIDEO_CHIP_REASONS && chip->reason[i].why; i++)
    fprintf(to, "  %ld lines left for %s\n", chip->reason[i].lines, chip->reason[i].why);
  if (check) {
    fprintf(to, "  %ld of them differ from the PPU's.\n", console->differing);
    if (console->differing)
      fprintf(to, "  The first: frame %u, line %d, column %d, %06X here and %06X from the PPU.\n",
              console->first.frame, console->first.line, console->first.column,
              console->first.native, console->first.emulated);
  }
  fprintf(to, "Sprites: %ld lines' found here, %ld left to the PPU.\n", chip->sprite_lines,
          chip->sprite_declined);
  if (check) {
    fprintf(to, "  %ld of those are not what the PPU found.\n", console->sprite_differing);
    if (console->sprite_differing)
      fprintf(to, "  The first: frame %u, line %d.\n", console->first_sprites.frame,
              console->first_sprites.line);
  }
  // A PPU with no console has had nothing written or read through here.
  if (console->snes != NULL) {
    fprintf(to, "Registers: %ld writes and %ld reads kept here.\n", chip->writes, chip->reads);
    if (check)
      fprintf(to, "  %ld of them, or of a frame's events or a state's saving, were not as the"
                  " PPU had them.\n",
              console->registers_differing);
    if (console->registers_differing) {
      const char* what = console->first_register.what;
      fprintf(to, "  The first: frame %u, line %d, `%s` after ", console->first_register.frame,
              console->first_register.line, console->first_register.which);
      if (!strcmp(what, "write"))
        fprintf(to, "%02X was written to $21%02X.\n", console->first_register.value,
                console->first_register.address);
      else if (!strcmp(what, "read"))
        fprintf(to, "$21%02X was read, as %02X by the PPU.\n", console->first_register.address,
                console->first_register.value);
      else
        fprintf(to, "the %s.\n", what);
    }
  }
  fprintf(to, "Notes: %ld lines' scrolls and windows kept here.\n", chip->noted_lines);
  if (check)
    fprintf(to, "  %ld writes, lines or frames' tops left them not as the PPU had its own.\n",
            console->notes_differing);
  if (console->notes_differing)
    fprintf(to, "  The first: frame %u, line %d, `%s`.\n", console->first_note.frame,
            console->first_note.line, console->first_note.which);
  if (chip->checksummed)
    fprintf(to, "  Picture checksum %016llX over %ld lines.\n",
            (unsigned long long)chip->checksum, chip->checksum_lines);
  return console->differing == 0 && console->sprite_differing == 0 &&
         console->registers_differing == 0 && console->notes_differing == 0;
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
