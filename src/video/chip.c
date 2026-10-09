// See `chip.h`.

#include "video/chip.h"

#include <string.h>

void video_chip_init(VideoChip* chip, bool checksummed) {
  memset(chip, 0, sizeof *chip);
  video_init(&chip->video);
  video_registers_attach(&chip->registers, &chip->memory);
  video_picture_init(&chip->picture);
  video_frame_init(&chip->frame);
  chip->checksummed = checksummed;
  chip->checksum = 14695981039346656037ull;
}

void video_chip_reset(VideoChip* chip) {
  video_registers_reset(&chip->registers);
  video_picture_reset(&chip->picture);
  memset(chip->obj_pixel, 0, sizeof chip->obj_pixel);
  memset(chip->obj_priority, 0, sizeof chip->obj_priority);
  memset(chip->pixels, 0, sizeof chip->pixels);
}

int video_chip_width(const VideoChip* chip) {
  return 256 + chip->picture.extra_left + chip->picture.extra_right;
}

void video_chip_state(const VideoChip* chip, VideoState* s) {
  video_registers_state(&chip->registers, s);
  video_frame_state(&chip->frame, &chip->picture, s);
  s->obj_pixel = chip->obj_pixel;
  s->obj_priority = chip->obj_priority;
}

// --- The game's, and the frame's events -----------------------------------------

void video_chip_write(VideoChip* chip, uint8_t address, uint8_t value, int line, bool drawing) {
  video_frame_write(&chip->frame, &chip->registers, address, value, line, drawing);
  chip->writes++;
}

uint8_t video_chip_read(VideoChip* chip, uint8_t address, const VideoBus* bus) {
  chip->reads++;
  return video_registers_read(&chip->registers, address, bus);
}

void video_chip_frame_start(VideoChip* chip) {
  video_registers_frame_start(&chip->registers);
  video_frame_start(&chip->frame, &chip->registers, &chip->picture);
}

bool video_chip_overscan(VideoChip* chip) { return video_registers_overscan(&chip->registers); }

void video_chip_vblank(VideoChip* chip) { video_registers_vblank(&chip->registers); }

// --- A line ---------------------------------------------------------------------

// Every column of a row is eight bytes, the pixel twice, each as blue, green,
// red and a byte that is never written, which the format puts first or last.
uint32_t video_row_colour(VideoPixels format, const uint8_t* row, int column) {
  const uint8_t* p = row + column * 8 + format;
  return (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

bool video_row_is(VideoPixels format, const uint8_t* row, int column, uint32_t colour) {
  const uint8_t* p = row + column * 8 + format;
  return video_row_colour(format, row, column) == colour &&
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

uint8_t* video_chip_row(VideoChip* chip, int line) {
  return &chip->pixels[video_row_at(chip->registers.even_frame, line)];
}

void video_chip_begin_line(VideoChip* chip, int line) {
  video_frame_line(&chip->frame, &chip->registers, line);
  chip->noted_lines++;
}

// A line that is blanked has no sprites and none are looked for, so neither
// flag can be set by it.
bool video_chip_find_sprites(VideoChip* chip, int line) {
  VideoRegisters* r = &chip->registers;
  VideoState s;
  video_chip_state(chip, &s);
  memset(chip->obj_pixel, 0, (size_t)video_width(&s));
  if (s.blank) return true;
  if (video_sprites_declines(&s)) {
    chip->sprite_declined++;
    return false;
  }
  chip->sprite_lines++;
  const int over = video_sprites(&s, line, chip->obj_pixel, chip->obj_priority);
  if (over & VIDEO_SPRITES_RANGE_OVER) r->range_over = true;
  if (over & VIDEO_SPRITES_TIME_OVER) r->time_over = true;
  return true;
}

static void note_decline(VideoChip* chip, const char* why) {
  chip->declined++;
  for (int i = 0; i < VIDEO_CHIP_REASONS; i++) {
    if (chip->reason[i].why == NULL) chip->reason[i].why = why;
    // The reasons are string literals of `video_declines`, one address each.
    if (chip->reason[i].why == why) {
      chip->reason[i].lines++;
      return;
    }
  }
}

const char* video_chip_draw_line(VideoChip* chip, int line) {
  VideoState s;
  video_chip_state(chip, &s);
  const char* why = video_declines(&s);
  if (why != NULL) {
    note_decline(chip, why);
    return why;
  }
  uint32_t colours[VIDEO_MAX_WIDTH];
  video_line(&chip->video, &s, &chip->frame.centre, line, colours);
  chip->lines++;
  row_put(chip->format, video_chip_row(chip, line), colours, video_width(&s));
  return NULL;
}

// FNV-1a, a line at a time: the line's number and each column's colour.
void video_chip_count_line(VideoChip* chip, int line) {
  if (!chip->checksummed) return;
  const uint8_t* row = video_chip_row(chip, line);
  const int width = video_chip_width(chip);
  uint64_t h = chip->checksum;
  h = (h ^ (uint64_t)line) * 1099511628211ull;
  for (int c = 0; c < width; c++)
    h = (h ^ video_row_colour(chip->format, row, c)) * 1099511628211ull;
  chip->checksum = h;
  chip->checksum_lines++;
}

void video_chip_run_line(VideoChip* chip, int line) {
  const VideoOther* other = &chip->other;
  video_chip_begin_line(chip, line);
  if (!video_chip_find_sprites(chip, line) && other->sprites != NULL)
    other->sprites(other->user, line);
  if (video_chip_draw_line(chip, line) != NULL) {
    if (other->line != NULL) other->line(other->user, line);
    else memset(video_chip_row(chip, line), 0, (size_t)video_chip_width(chip) * 8);
  }
  video_chip_count_line(chip, line);
}

// --- What the frontend says and puts ----------------------------------------------

static void said(const VideoChip* chip, VideoSaid what, int which) {
  if (chip->other.said != NULL) chip->other.said(chip->other.user, what, which);
}

void video_set_margins(VideoChip* chip, int left, int right) {
  VideoPicture* p = &chip->picture;
  p->extra_left = left < 0 ? 0 : left > VIDEO_EXTRA_MAX ? VIDEO_EXTRA_MAX : left;
  p->extra_right = right < 0 ? 0 : right > VIDEO_EXTRA_MAX ? VIDEO_EXTRA_MAX : right;
  said(chip, VIDEO_SAID_MARGINS, 0);
}

void video_set_wide(VideoChip* chip, int layer, VideoWide policy) {
  if (layer < VIDEO_BG1 || layer > VIDEO_OBJ) return;
  chip->picture.wide[layer] = (uint8_t)policy;
  said(chip, VIDEO_SAID_WIDE, layer);
}

void video_set_clamp(VideoChip* chip, int lo, int hi) {
  chip->picture.clamp_lo = lo;
  chip->picture.clamp_hi = hi;
  said(chip, VIDEO_SAID_CLAMP, 0);
}

void video_set_sprite_place(VideoChip* chip, int sprite, VideoSpritePlace place) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  chip->picture.place[sprite] = (uint8_t)place;
  said(chip, VIDEO_SAID_OF_SPRITE, sprite);
}

void video_set_sprite_shift(VideoChip* chip, int sprite, int shift) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  chip->picture.shift[sprite] = (int16_t)shift;
  said(chip, VIDEO_SAID_OF_SPRITE, sprite);
}

void video_set_sprite_front(VideoChip* chip, int sprite, bool front) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  chip->picture.front[sprite] = front;
  said(chip, VIDEO_SAID_OF_SPRITE, sprite);
}

void video_set_sprite_remapped(VideoChip* chip, int sprite, bool remapped) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  chip->picture.remap_on[sprite] = remapped;
  said(chip, VIDEO_SAID_OF_SPRITE, sprite);
}

void video_set_remap(VideoChip* chip, const uint8_t remap[16]) {
  memcpy(chip->picture.remap, remap, 16);
  said(chip, VIDEO_SAID_REMAP, 0);
}

void video_set_scroll(VideoChip* chip, int layer, int h, int v) {
  if (layer < VIDEO_BG1 || layer > VIDEO_BG4) return;
  VideoBg* bg = &chip->registers.bg[layer];
  bg->hscroll = (uint16_t)(h & 0x3ff);
  bg->vscroll = (uint16_t)(v & 0x3ff);
  said(chip, VIDEO_SAID_SCROLL, layer);
}

void video_set_sprite(VideoChip* chip, int sprite, int x, int y, uint16_t word, bool large) {
  if (sprite < 0 || sprite >= VIDEO_SPRITES) return;
  video_put_sprite(&chip->registers, sprite, x, y, word, large);
  said(chip, VIDEO_SAID_SPRITE, sprite);
}

void video_set_vram(VideoChip* chip, uint16_t at, uint16_t word) {
  video_put_vram(&chip->registers, at, word);
  said(chip, VIDEO_SAID_VRAM, at);
}

void video_set_colour(VideoChip* chip, int index, uint16_t colour) {
  video_put_colour(&chip->registers, index, colour);
  said(chip, VIDEO_SAID_COLOUR, index);
}

// --- The picture ------------------------------------------------------------------

void video_set_pixel_format(VideoChip* chip, VideoPixels format) {
  chip->format = format;
  said(chip, VIDEO_SAID_PIXEL_FORMAT, 0);
}

int video_output_width(const VideoChip* chip) { return 2 * video_chip_width(chip); }

void video_put_pixels(const VideoChip* chip, uint8_t* pixels) {
  const VideoRegisters* r = &chip->registers;
  const size_t pitch = (size_t)video_output_width(chip) * 4;
  const int lines = r->frame_overscan ? 239 : 224, top = r->frame_overscan ? 2 : 16;
  for (int line = 1; line <= lines; line++) {
    // An interlaced frame is both fields, a row from each. Any other is the
    // field just drawn, each row twice.
    const bool first = r->frame_interlace || r->even_frame;
    const bool second = !r->frame_interlace && r->even_frame;
    uint8_t* to = pixels + (size_t)(top + (line - 1) * 2) * pitch;
    memcpy(to, &chip->pixels[video_row_at(first, line)], pitch);
    memcpy(to + pitch, &chip->pixels[video_row_at(second, line)], pitch);
  }
  memset(pixels, 0, (size_t)top * pitch);
  if (!r->frame_overscan) memset(pixels + 464 * pitch, 0, 16 * pitch);
}
