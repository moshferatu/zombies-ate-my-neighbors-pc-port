
#ifndef SNES_H
#define SNES_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Snes Snes;

// Called at (0,0), after the vblank the frame follows and before its first
// line is drawn. That is the only moment at which the machine's state is the
// state this frame will be drawn from: the game's vblank has uploaded its
// tilemaps and DMA'd its OAM, and nothing has been rendered yet. Anything that
// wants to add to the picture rather than change the game belongs here.
typedef void (*SnesFrameHook)(Snes* snes, void* ctx);

// Called after every write the CPU makes, with `cpuClock` the CPU's own clock
// (see `stolenCycles`) as it stood when the access began. The write has
// happened by then. DMA's writes are not the CPU's and do not call it.
typedef void (*SnesWriteHook)(Snes* snes, uint32_t adr, uint8_t val,
                              uint64_t cpuClock, void* ctx);

// ...and after every read the CPU makes, with the value it got. Same clock.
// DMA's reads do not call it either.
typedef SnesWriteHook SnesReadHook;

#include "cpu.h"
#include "apu.h"
#include "dma.h"
#include "ppu.h"
#include "cart.h"
#include "input.h"
#include "statehandler.h"

// zamn: the video chip, as the console drives it. It is told of every write
// to $2100-$213F and asked for every read, told the three things that happen
// to the chip once a frame and each line of the picture as the beam is
// halfway along it, and asked two things about the frame that decide how
// long a line and a frame are. `handleState` saves or loads the chip's share
// of a state, in the layout `ppu_handleState` has. Each is passed `user`.
//
// A console starts with its own PPU behind this. `snes_setVideo` puts
// somebody else there: `src/video` is the one user, and has the PPU beside
// it only to be checked against.
typedef struct SnesVideo {
  void* user;
  void (*reset)(void* user);
  uint8_t (*read)(void* user, uint8_t adr);
  void (*write)(void* user, uint8_t adr, uint8_t val);
  void (*frameStart)(void* user);            // at (0,0)
  bool (*checkOverscan)(void* user);         // at (0,225): true for a frame of 239 lines
  void (*vblank)(void* user);                // at (0,225), or (0,240) with overscan
  void (*runLine)(void* user, int line);     // lines 1-224/239
  void (*handleState)(void* user, StateHandler* sh);
  bool (*evenFrame)(void* user);
  bool (*frameInterlace)(void* user);
} SnesVideo;

struct Snes {
  Cpu* cpu;
  Apu* apu;
  // The PPU is always there, and is the console's video chip unless
  // `snes_setVideo` has said otherwise: `video` is who is asked.
  Ppu* ppu;
  SnesVideo video;
  Dma* dma;
  Cart* cart;
  bool palTiming;
  // input
  Input* input1;
  Input* input2;
  // ram
  uint8_t ram[0x20000];
  uint32_t ramAdr;
  // frame timing
  uint16_t hPos;
  uint16_t vPos;
  uint32_t frames;
  uint64_t cycles;
  uint64_t syncCycle;
  // cpu handling
  double apuCatchupCycles;
  // nmi / irq
  bool hIrqEnabled;
  bool vIrqEnabled;
  bool nmiEnabled;
  uint16_t hTimer;
  uint16_t vTimer;
  bool inNmi;
  bool irqCondition;
  bool inIrq;
  bool inVblank;
  // joypad handling
  uint16_t portAutoRead[4]; // as read by auto-joypad read
  bool autoJoyRead;
  uint16_t autoJoyTimer; // times how long until reading is done
  bool ppuLatch;
  // multiplication/division
  uint8_t multiplyA;
  uint16_t multiplyResult;
  uint16_t divideA;
  uint16_t divideResult;
  // misc
  bool fastMem;
  uint8_t openBus;
  // called at the top of every frame, before any of it is drawn
  SnesFrameHook frameHook;
  void* frameHookCtx;
  // Cycles the CPU did not spend on its own accesses: the DRAM refresh, and
  // every DMA and HDMA transfer with the alignment around it. So `cycles -
  // stolenCycles` is a clock that moves only while the CPU is executing, and
  // an instruction costs the same on it wherever it lands. Only differences
  // of it mean anything, so it is not part of a saved state.
  uint64_t stolenCycles;
  bool inDma;  // inside `dma_handleDma`, whose refreshes it counts itself
  SnesWriteHook writeHook;
  void* writeHookCtx;
  SnesReadHook readHook;
  void* readHookCtx;
};

Snes* snes_init(void);
void snes_free(Snes* snes);
void snes_reset(Snes* snes, bool hard);
void snes_handleState(Snes* snes, StateHandler* sh);
void snes_runFrame(Snes* snes);
// used by dma, cpu
void snes_runCycles(Snes* snes, int cycles);
void snes_syncCycles(Snes* snes, bool start, int syncCycles);
uint8_t snes_readBBus(Snes* snes, uint8_t adr);
void snes_writeBBus(Snes* snes, uint8_t adr, uint8_t val);
uint8_t snes_read(Snes* snes, uint32_t adr);
void snes_write(Snes* snes, uint32_t adr, uint8_t val);
void snes_cpuIdle(void* mem, bool waiting);
uint8_t snes_cpuRead(void* mem, uint32_t adr);
void snes_cpuWrite(void* mem, uint32_t adr, uint8_t val);
// debugging
void snes_runCpuCycle(Snes* snes);
void snes_runSpcCycle(Snes* snes);

// snes_other.c functions:

enum { pixelFormatXRGB = 0, pixelFormatRGBX = 1 };

bool snes_loadRom(Snes* snes, const uint8_t* data, int length);
void snes_setButtonState(Snes* snes, int player, int button, bool pressed);
// These three are the PPU's picture, for a console whose video chip it is.
void snes_setPixelFormat(Snes* snes, int pixelFormat);
void snes_setPixels(Snes* snes, uint8_t* pixelData);
// Widescreen is said to `src/video` (`video_set_margins` and the rest), which
// tells the PPU. `snes_pixelWidth` is the width of what `snes_setPixels` then
// writes, in output pixels, and its row pitch is that times four bytes.
int snes_pixelWidth(const Snes* snes);
// See `SnesVideo`. Pass NULL to have the console's own PPU again.
void snes_setVideo(Snes* snes, const SnesVideo* video);
// See `SnesFrameHook`. Pass NULL to remove it.
void snes_setFrameHook(Snes* snes, SnesFrameHook hook, void* ctx);
// See `SnesWriteHook`. Pass NULL to remove it.
void snes_setWriteHook(Snes* snes, SnesWriteHook hook, void* ctx);
// See `SnesReadHook`. Pass NULL to remove it.
void snes_setReadHook(Snes* snes, SnesReadHook hook, void* ctx);
void snes_setSamples(Snes* snes, int16_t* sampleData, int samplesPerFrame);
int snes_saveBattery(Snes* snes, uint8_t* data);
bool snes_loadBattery(Snes* snes, uint8_t* data, int size);
int snes_saveState(Snes* snes, uint8_t* data);
bool snes_loadState(Snes* snes, uint8_t* data, int size);

#endif
