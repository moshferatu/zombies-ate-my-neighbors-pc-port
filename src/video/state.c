// See `state.h`.

#include "video/state.h"

#include <string.h>

void video_state_handle(VideoRegisters* r, uint8_t* obj_pixel, uint8_t* obj_priority,
                        StateHandler* sh, VideoStateParts* parts) {
  const int from = sh->offset;
  sh_handleBools(sh,
    &r->vram_step_on_high, &r->cgram_second, &r->oam_high, &r->oam_high_written, &r->oam_second,
    &r->obj_from_address, &r->time_over, &r->range_over, &r->obj_interlace, &r->m7_large_field,
    &r->m7_fill, &r->m7_flip_x, &r->m7_flip_y, &r->m7_ext_bg, &r->add_sub, &r->subtract, &r->half,
    &r->math[0], &r->math[1], &r->math[2], &r->math[3], &r->math[4], &r->math[5],
    &r->blank, &r->bg3_front, &r->even_frame, &r->pseudo_hires, &r->overscan,
    &r->frame_overscan, &r->interlace, &r->frame_interlace, &r->direct_colour, &r->h_second,
    &r->v_second, &r->latched, NULL);
  sh_handleBytes(sh,
    &r->vram_remap, &r->cgram_at, &r->cgram_first_byte, &r->oam_at, &r->oam_at_written,
    &r->oam_first_byte, &r->obj_sizes, &r->scroll_latch, &r->hscroll_latch, &r->mosaic_size,
    &r->mosaic_from, &r->m7_latch, &r->window1_left, &r->window1_right, &r->window2_left,
    &r->window2_right, &r->clip, &r->prevent, &r->fixed_r, &r->fixed_g, &r->fixed_b,
    &r->brightness, &r->mode, &r->bus1, &r->bus2, NULL);
  sh_handleWords(sh,
    &r->vram_at, &r->vram_step, &r->vram_ahead, &r->obj_tiles_at[0], &r->obj_tiles_at[1],
    &r->h_count, &r->v_count, NULL);
  sh_handleWordsS(sh,
    &r->m7[0], &r->m7[1], &r->m7[2], &r->m7[3], &r->m7[4], &r->m7[5], &r->m7[6], &r->m7[7], NULL);
  int32_t mode7_x = 0, mode7_y = 0;
  if (parts != NULL) parts->mode7_working = sh->offset - from;
  sh_handleIntsS(sh, &mode7_x, &mode7_y, NULL);
  for (int i = 0; i < 4; i++) {
    VideoBg* bg = &r->bg[i];
    sh_handleBools(sh, &bg->map_wide, &bg->map_high, &bg->big_tiles, &bg->mosaic, NULL);
    sh_handleWords(sh, &bg->hscroll, &bg->vscroll, &bg->map_at, &bg->tiles_at, NULL);
  }
  for (int i = 0; i < 5; i++)
    sh_handleBools(sh, &r->main[i], &r->sub[i], &r->main_windowed[i], &r->sub_windowed[i], NULL);
  for (int i = 0; i < VIDEO_LAYERS; i++) {
    VideoWindow* w = &r->window[i];
    sh_handleBools(sh, &w->one, &w->one_inverted, &w->two, &w->two_inverted, NULL);
    sh_handleBytes(sh, &w->logic, NULL);
  }
  sh_handleWordArray(sh, r->vram, 0x8000);
  sh_handleWordArray(sh, r->cgram, 0x100);
  sh_handleWordArray(sh, r->oam, 0x100);
  sh_handleByteArray(sh, r->high_oam, 0x20);
  if (parts != NULL) parts->sprite_rows = sh->offset - from;
  sh_handleByteArray(sh, obj_pixel, 256);
  sh_handleByteArray(sh, obj_priority, 256);
}

const char* video_states_differ(const uint8_t* here, const uint8_t* theirs, int size,
                                const VideoStateParts* parts) {
  const int after_working = parts->mode7_working + 8, rows = parts->sprite_rows;
  if (size != rows + 512) return "the state's size";
  if (memcmp(here, theirs, (size_t)parts->mode7_working) ||
      memcmp(here + after_working, theirs + after_working, (size_t)(rows - after_working)))
    return "the state as saved";
  if (memcmp(here + rows, theirs + rows, 256)) return "the sprites' row as saved";
  for (int c = 0; c < 256; c++)
    if (here[rows + c] != 0 && here[rows + 256 + c] != theirs[rows + 256 + c])
      return "the sprites' priorities as saved";
  return NULL;
}
