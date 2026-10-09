// See `console.h`.

#include "video/console.h"

#include <string.h>

#include "video/state.h"

void video_console_init(VideoConsole* console, bool checksummed) {
  memset(console, 0, sizeof *console);
  video_chip_init(&console->chip, checksummed);
}

// --- The console's video chip ---------------------------------------------------
//
// What a console asks of its chip (`SnesVideo`).

void video_console_write(VideoConsole* console, uint8_t address, uint8_t value) {
  const Snes* snes = console->snes;
  video_chip_write(&console->chip, address, value, snes->vPos,
                   !snes->inVblank && snes->vPos > 0);
}

uint8_t video_console_read(VideoConsole* console, uint8_t address) {
  const Snes* snes = console->snes;
  const VideoBus bus = {
      .dot = snes->hPos / 4,
      .line = snes->vPos,
      .pal = snes->palTiming,
      .open_bus = snes->openBus,
  };
  return video_chip_read(&console->chip, address, &bus);
}

static VideoChip* chip_of(void* user) { return &((VideoConsole*)user)->chip; }

static void console_write(void* user, uint8_t address, uint8_t value) {
  video_console_write((VideoConsole*)user, address, value);
}

static uint8_t console_read(void* user, uint8_t address) {
  return video_console_read((VideoConsole*)user, address);
}

static void console_reset(void* user) { video_chip_reset(chip_of(user)); }
static void console_frame_start(void* user) { video_chip_frame_start(chip_of(user)); }
static bool console_overscan(void* user) { return video_chip_overscan(chip_of(user)); }
static void console_vblank(void* user) { video_chip_vblank(chip_of(user)); }
static void console_line(void* user, int line) { video_chip_run_line(chip_of(user), line); }

// Which of the two fields the frame is, and whether it is interlaced: an odd
// field that is not is a little shorter, and an even one that is has a line
// more.
static bool console_even_frame(void* user) { return chip_of(user)->registers.even_frame; }

static bool console_frame_interlace(void* user) {
  return chip_of(user)->registers.frame_interlace;
}

// The chip's share of a saved state (`state.h`).
static void console_state(void* user, StateHandler* sh) {
  VideoChip* chip = chip_of(user);
  video_state_handle(&chip->registers, chip->obj_pixel, chip->obj_priority, sh, NULL);
}

void video_console_attach(VideoConsole* console, Snes* snes) {
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

// Whoever is a console's video chip here has a `VideoConsole` first in what
// the console is given to pass back.
VideoChip* video_chip_of(const Snes* snes) { return chip_of(snes->video.user); }

bool video_console_report(const VideoConsole* console, FILE* to) {
  const VideoChip* chip = &console->chip;
  fprintf(to, "Drawing: native, with no PPU; %ld lines drawn here, %ld shown black.\n",
          chip->lines, chip->declined);
  for (int i = 0; i < VIDEO_CHIP_REASONS && chip->reason[i].why; i++)
    fprintf(to, "  %ld lines not drawn for %s\n", chip->reason[i].lines, chip->reason[i].why);
  fprintf(to, "Sprites: %ld lines' found here, %ld lines' not found.\n", chip->sprite_lines,
          chip->sprite_declined);
  fprintf(to, "Registers: %ld writes and %ld reads kept here.\n", chip->writes, chip->reads);
  fprintf(to, "Notes: %ld lines' scrolls and windows kept here.\n", chip->noted_lines);
  if (chip->checksummed)
    fprintf(to, "  Picture checksum %016llX over %ld lines.\n",
            (unsigned long long)chip->checksum, chip->checksum_lines);
  return chip->declined == 0 && chip->sprite_declined == 0;
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
