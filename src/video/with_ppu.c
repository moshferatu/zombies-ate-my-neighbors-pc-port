// A console drawn by any of the three renderers: one with its emulated PPU,
// and that PPU beside its chip (`beside.h`). See `console.h`.

#include <stdlib.h>

#include "video/beside.h"

Snes* video_snes_init(VideoRenderer renderer, bool checksummed) {
  VideoBeside* beside = malloc(sizeof *beside);
  if (beside == NULL) return NULL;
  Snes* snes = snes_init();
  video_beside_install(beside, snes->ppu, renderer, checksummed);
  video_beside_attach(beside, snes);
  return snes;
}

bool video_snes_report(const Snes* snes, FILE* to) {
  return video_beside_report((const VideoBeside*)snes->video.user, to);
}

void video_snes_free(Snes* snes) {
  VideoBeside* beside = (VideoBeside*)snes->video.user;
  snes_free(snes);
  free(beside);
}
