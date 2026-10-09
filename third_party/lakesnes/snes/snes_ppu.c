
// zamn: the console's own PPU -- a console made with one, the PPU as its
// video chip (`SnesVideo`) and the PPU's picture. Everything of the console
// that calls the PPU is in this file, and the rest of it calls nothing here,
// so that a program whose consoles are made by `snes_initWithoutPpu` links
// neither this nor `ppu.c`.

#include <stdint.h>
#include <stdbool.h>

#include "snes.h"
#include "ppu.h"
#include "statehandler.h"

static void snes_ppuReset(void* ppu) { ppu_reset((Ppu*) ppu); }
static uint8_t snes_ppuRead(void* ppu, uint8_t adr) { return ppu_read((Ppu*) ppu, adr); }
static void snes_ppuWrite(void* ppu, uint8_t adr, uint8_t val) { ppu_write((Ppu*) ppu, adr, val); }
static void snes_ppuFrameStart(void* ppu) { ppu_handleFrameStart((Ppu*) ppu); }
static bool snes_ppuCheckOverscan(void* ppu) { return ppu_checkOverscan((Ppu*) ppu); }
static void snes_ppuVblank(void* ppu) { ppu_handleVblank((Ppu*) ppu); }
static void snes_ppuRunLine(void* ppu, int line) { ppu_runLine((Ppu*) ppu, line); }
static void snes_ppuHandleState(void* ppu, StateHandler* sh) { ppu_handleState((Ppu*) ppu, sh); }
static bool snes_ppuEvenFrame(void* ppu) { return ((Ppu*) ppu)->evenFrame; }
static bool snes_ppuFrameInterlace(void* ppu) { return ((Ppu*) ppu)->frameInterlace; }

Snes* snes_init(void) {
  Snes* snes = snes_initWithoutPpu();
  snes->ppu = ppu_init(snes);
  snes->freePpu = ppu_free;
  const SnesVideo video = {
    .user = snes->ppu,
    .reset = snes_ppuReset,
    .read = snes_ppuRead,
    .write = snes_ppuWrite,
    .frameStart = snes_ppuFrameStart,
    .checkOverscan = snes_ppuCheckOverscan,
    .vblank = snes_ppuVblank,
    .runLine = snes_ppuRunLine,
    .handleState = snes_ppuHandleState,
    .evenFrame = snes_ppuEvenFrame,
    .frameInterlace = snes_ppuFrameInterlace,
  };
  snes_setVideo(snes, &video);
  return snes;
}

void snes_setPixelFormat(Snes* snes, int pixelFormat) {
  // pixelFormatXRGB, pixelFormatRGBX (default: pixelFormatRGBX)
  ppu_setPixelOutputFormat(snes->ppu, (pixelFormat) ? ppu_pixelOutputFormatBGRX : ppu_pixelOutputFormatXBGR);
}

void snes_setPixels(Snes* snes, uint8_t* pixelData) {
  // size is 4 (rgba) * snes_pixelWidth (w) * 480 (h), and that width is 512
  // unless the picture has been widened
  ppu_putPixels(snes->ppu, pixelData);
}

int snes_pixelWidth(const Snes* snes) {
  return ppu_outputWidth(snes->ppu);
}
