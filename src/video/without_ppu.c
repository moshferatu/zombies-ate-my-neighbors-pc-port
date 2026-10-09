// A console with no emulated PPU, whose chip draws its picture alone. The
// program this is linked into has `VIDEO_NATIVE` and nothing of the PPU's.
// See `console.h`.

#include <stdlib.h>

#include "video/console.h"

Snes* video_snes_init(VideoRenderer renderer, bool checksummed) {
  if (renderer != VIDEO_NATIVE) return NULL;
  VideoConsole* console = malloc(sizeof *console);
  if (console == NULL) return NULL;
  Snes* snes = snes_initWithoutPpu();
  video_console_init(console, checksummed);
  video_console_attach(console, snes);
  return snes;
}

bool video_snes_report(const Snes* snes, FILE* to) {
  return video_console_report((const VideoConsole*)snes->video.user, to);
}

void video_snes_free(Snes* snes) {
  VideoConsole* console = (VideoConsole*)snes->video.user;
  snes_free(snes);
  free(console);
}
