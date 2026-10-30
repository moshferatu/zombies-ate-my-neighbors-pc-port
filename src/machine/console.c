// The console, for `zamn`: its clock, its bus, WRAM and its own registers.
//
// It is what LakeSnes's `snes.h` declares, on its `Snes`, which the harness,
// `src/video` and `src/sound` all use, so it is the same machine to them in
// either build. It is made without a PPU (`src/video` is the video chip) and
// without a sound chip (the port's sound is attached before it is reset), and
// it keeps NTSC's timing alone, which is the cartridge's.
//
// **The clock** counts master cycles, 21.477 MHz. A line is 1,364 of them,
// a frame 262 lines, and the beam's place is `hPos`, `vPos`. The clock moves
// two cycles at a time, and at four places in a line something happens:
//
//   * 0: a new line. Line 0 ends the vertical blank and starts the frame;
//     line 225 (or 240, with overscan) starts the blank: the NMI, and the
//     pads read for `$4218`-`$421F`.
//   * 16, on line 0: HDMA loads its tables.
//   * 512: the video chip draws the line.
//   * 1104: HDMA copies the line's bytes.
//
// Once a line, when the clock passes 536, the DRAM is refreshed: 40 cycles
// that are nobody's. The H and V timers raise the IRQ when the beam meets
// them (the game never sets them). Stretches of a line where none of that
// can happen are counted in one go, which is what keeps the clock cheap.
//
// **The sound** is told how many of its cycles have passed whenever the game
// touches its ports and at the end of a frame: the fraction 32,040 * 32 /
// (1,364 * 262 * 60) of every master cycle, added two cycles at a time, as
// a double, so that its rounding is the same however the clock is stepped.
//
// **The bus**: WRAM at `$7E`-`$7F` and mirrored low in the system banks;
// the B bus at `$2100`-`$21FF` (the video chip, the sound's four ports and
// `$2180`); the pads at `$4016`-`$4017`; the console's registers at
// `$4200`-`$421F`; DMA's at `$4300`-`$437F`; and the cartridge behind it
// all. An access takes 6, 8 or 12 cycles by where it is, and `$420D` makes
// ROM in banks `$80`-`$FF` the quicker one. DMA due goes first.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"

enum {
  LINE_CYCLES = 1364,
  FRAME_LINES = 262,
  REFRESH_AT = 536,
  REFRESH_CYCLES = 40,
  HDMA_BEGIN_AT = 16,
  DRAW_AT = 512,
  HDMA_LINE_AT = 1104,
  VBLANK_LINE = 225,
  VBLANK_LINE_OVERSCAN = 240,
  AUTO_JOY_CYCLES = 4224,
};

static const double SOUND_CYCLES_PER_MASTER =
    (32040 * 32) / (LINE_CYCLES * FRAME_LINES * 60.0);

// -- made, freed and reset -------------------------------------------------

void snes_setVideo(Snes* snes, const SnesVideo* video) { snes->video = *video; }

void snes_setSound(Snes* snes, const SnesSound* sound) {
  if (sound) snes->sound = *sound;
  else memset(&snes->sound, 0, sizeof(snes->sound));
}

Snes* snes_initWithoutPpu(void) {
  Snes* snes = calloc(1, sizeof(Snes));
  if (!snes) return NULL;
  snes->cpu = cpu_init(snes, snes_cpuRead, snes_cpuWrite, snes_cpuIdle);
  snes->dma = dma_init(snes);
  snes->cart = cart_init(snes);
  snes->input1 = input_init(snes);
  snes->input2 = input_init(snes);
  return snes;
}

void snes_free(Snes* snes) {
  cpu_free(snes->cpu);
  dma_free(snes->dma);
  cart_free(snes->cart);
  input_free(snes->input1);
  input_free(snes->input2);
  free(snes);
}

static void no_sound(void) {
  fprintf(stderr, "zamn: the console was run with no sound attached.\n");
  abort();
}

// Loading the ROM resets the console before the sound is attached, so the
// sound is reset here only once it is.
void snes_reset(Snes* snes, bool hard) {
  cpu_reset(snes->cpu, hard);
  if (snes->sound.reset) snes->sound.reset(snes->sound.user);
  dma_reset(snes->dma);
  snes->video.reset(snes->video.user);
  input_reset(snes->input1);
  input_reset(snes->input2);
  cart_reset(snes->cart);
  if (hard) memset(snes->ram, 0, sizeof(snes->ram));
  snes->ramAdr = 0;
  snes->hPos = snes->vPos = 0;
  snes->frames = 0;
  snes->cycles = snes->syncCycle = 0;
  snes->apuCatchupCycles = 0.0;
  snes->hIrqEnabled = snes->vIrqEnabled = snes->nmiEnabled = false;
  snes->hTimer = snes->vTimer = 0x1ff;
  snes->inNmi = snes->irqCondition = snes->inIrq = snes->inVblank = false;
  memset(snes->portAutoRead, 0, sizeof(snes->portAutoRead));
  snes->autoJoyRead = false;
  snes->autoJoyTimer = 0;
  snes->ppuLatch = false;
  snes->multiplyA = 0xff;
  snes->multiplyResult = 0xfe01;
  snes->divideA = 0xffff;
  snes->divideResult = 0x101;
  snes->fastMem = false;
  snes->openBus = 0;
}

// The order is the saved state's, and LakeSnes's.
void snes_handleState(Snes* snes, StateHandler* sh) {
  sh_handleBools(sh, &snes->palTiming, &snes->hIrqEnabled, &snes->vIrqEnabled,
                 &snes->nmiEnabled, &snes->inNmi, &snes->irqCondition,
                 &snes->inIrq, &snes->inVblank, &snes->autoJoyRead,
                 &snes->ppuLatch, &snes->fastMem, NULL);
  sh_handleBytes(sh, &snes->multiplyA, &snes->openBus, NULL);
  sh_handleWords(sh, &snes->hPos, &snes->vPos, &snes->hTimer, &snes->vTimer,
                 &snes->portAutoRead[0], &snes->portAutoRead[1],
                 &snes->portAutoRead[2], &snes->portAutoRead[3],
                 &snes->autoJoyTimer, &snes->multiplyResult, &snes->divideA,
                 &snes->divideResult, NULL);
  sh_handleInts(sh, &snes->ramAdr, &snes->frames, NULL);
  sh_handleLongLongs(sh, &snes->cycles, &snes->syncCycle, NULL);
  sh_handleDoubles(sh, &snes->apuCatchupCycles, NULL);
  sh_handleByteArray(sh, snes->ram, sizeof(snes->ram));
  cpu_handleState(snes->cpu, sh);
  dma_handleState(snes->dma, sh);
  snes->video.handleState(snes->video.user, sh);
  if (!snes->sound.handleState) no_sound();
  snes->sound.handleState(snes->sound.user, sh);
  input_handleState(snes->input1, sh);
  input_handleState(snes->input2, sh);
  cart_handleState(snes->cart, sh);
}

// -- the clock -------------------------------------------------------------

static void catch_up_sound(Snes* snes) {
  if (!snes->sound.run) no_sound();
  const int owed = (int)snes->apuCatchupCycles;
  const int ran = owed > 0 ? snes->sound.run(snes->sound.user, owed) : 0;
  snes->apuCatchupCycles -= (double)ran;
}

// After the frame's blank begins: both pads read into `$4218`-`$421F`, which
// shows as busy in `$4212` for a while.
static void read_pads(Snes* snes) {
  memset(snes->portAutoRead, 0, sizeof(snes->portAutoRead));
  input_latch(snes->input1, true);
  input_latch(snes->input2, true);
  input_latch(snes->input1, false);
  input_latch(snes->input2, false);
  for (int i = 0; i < 16; i++) {
    const uint8_t a = input_read(snes->input1);
    const uint8_t b = input_read(snes->input2);
    snes->portAutoRead[0] |= (uint16_t)((a & 1) << (15 - i));
    snes->portAutoRead[2] |= (uint16_t)(((a >> 1) & 1) << (15 - i));
    snes->portAutoRead[1] |= (uint16_t)((b & 1) << (15 - i));
    snes->portAutoRead[3] |= (uint16_t)(((b >> 1) & 1) << (15 - i));
  }
}

// The start of a line.
static void line_begins(Snes* snes) {
  bool blank_begins = false;
  if (snes->vPos == 0) {
    snes->inVblank = false;
    snes->inNmi = false;
    if (snes->frameHook) snes->frameHook(snes, snes->frameHookCtx);
    snes->video.frameStart(snes->video.user);
  } else if (snes->vPos == VBLANK_LINE) {
    blank_begins = !snes->video.checkOverscan(snes->video.user);
  } else if (snes->vPos == VBLANK_LINE_OVERSCAN) {
    blank_begins = !snes->inVblank;
  }
  if (!blank_begins) return;
  snes->video.vblank(snes->video.user);
  snes->inVblank = true;
  snes->inNmi = true;
  if (snes->autoJoyRead) {
    snes->autoJoyTimer = AUTO_JOY_CYCLES;
    read_pads(snes);
  }
  if (snes->nmiEnabled) cpu_nmi(snes->cpu);
}

// Two master cycles.
static void step(Snes* snes) {
  snes->apuCatchupCycles += SOUND_CYCLES_PER_MASTER * 2.0;
  snes->cycles += 2;
  const bool irq_now = (snes->vIrqEnabled || snes->hIrqEnabled) &&
                       (snes->vPos == snes->vTimer || !snes->vIrqEnabled) &&
                       (snes->hPos == snes->hTimer * 4 || !snes->hIrqEnabled);
  if (!snes->irqCondition && irq_now) {
    snes->inIrq = true;
    cpu_setIrq(snes->cpu, true);
  }
  snes->irqCondition = irq_now;
  switch (snes->hPos) {
    case 0:
      line_begins(snes);
      break;
    case HDMA_BEGIN_AT:
      if (snes->vPos == 0) snes->dma->hdmaInitRequested = true;
      break;
    case DRAW_AT:
      if (!snes->inVblank && snes->vPos > 0)
        snes->video.runLine(snes->video.user, snes->vPos);
      break;
    case HDMA_LINE_AT:
      if (!snes->inVblank) snes->dma->hdmaRunRequested = true;
      break;
    default:
      break;
  }
  if (snes->autoJoyTimer > 0) snes->autoJoyTimer -= 2;
  snes->hPos += 2;
  // Line 240 of an odd frame without interlace is four cycles short, and an
  // even frame with interlace has a line more.
  const SnesVideo* v = &snes->video;
  if (snes->hPos == LINE_CYCLES ||
      (snes->hPos == LINE_CYCLES - 4 && snes->vPos == 240 &&
       !v->evenFrame(v->user) && !v->frameInterlace(v->user))) {
    snes->hPos = 0;
    snes->vPos++;
    if ((snes->vPos == FRAME_LINES &&
         (!v->frameInterlace(v->user) || !v->evenFrame(v->user))) ||
        snes->vPos == FRAME_LINES + 1) {
      snes->vPos = 0;
      snes->frames++;
    }
  }
}

// How many of the next `most` steps would only move the counters: none on
// a place in the line that acts, none that ends a line, and none where the
// timers' condition would change.
static int quiet_steps(const Snes* snes, int most) {
  const int h = snes->hPos;
  if ((h & 1) || h == 0 || h == HDMA_BEGIN_AT || h == DRAW_AT || h == HDMA_LINE_AT)
    return 0;
  // An odd count would go below nought and on; a step at a time, then.
  if (snes->autoJoyTimer & 1) return 0;
  // The last step that cannot end a line starts at 1356.
  int stop = h < HDMA_BEGIN_AT ? HDMA_BEGIN_AT
             : h < DRAW_AT     ? DRAW_AT
             : h < HDMA_LINE_AT ? HDMA_LINE_AT
                                : 1358;
  bool condition = false;
  if (snes->vIrqEnabled || snes->hIrqEnabled) {
    const int at = snes->hTimer * 4;
    if (snes->hIrqEnabled && at == h) return 0;
    if (snes->hIrqEnabled && at > h && at < stop) stop = at;
    condition = !snes->hIrqEnabled && snes->vPos == snes->vTimer;
  }
  if (condition != snes->irqCondition || h >= stop) return 0;
  const int steps = (stop - h) / 2;
  return steps < most ? steps : most;
}

void snes_runCycles(Snes* snes, int cycles) {
  if (snes->hPos < REFRESH_AT && snes->hPos + cycles >= REFRESH_AT) {
    cycles += REFRESH_CYCLES;
    if (!snes->inDma) snes->stolenCycles += REFRESH_CYCLES;
  }
  int steps = cycles > 0 ? (cycles + 1) / 2 : 0;
  while (steps > 0) {
    const int quiet = quiet_steps(snes, steps);
    if (quiet == 0) {
      step(snes);
      steps--;
      continue;
    }
    // The sound's share a step at a time, as `step` adds it, to round the same.
    for (int i = 0; i < quiet; i++) snes->apuCatchupCycles += SOUND_CYCLES_PER_MASTER * 2.0;
    snes->cycles += 2 * (uint64_t)quiet;
    if (snes->autoJoyTimer > 0) {
      const int left = (snes->autoJoyTimer + 1) / 2;
      snes->autoJoyTimer = (uint16_t)(snes->autoJoyTimer - 2 * (quiet < left ? quiet : left));
    }
    snes->hPos = (uint16_t)(snes->hPos + 2 * quiet);
    steps -= quiet;
  }
}

// To the next multiple of `sync` cycles: of the clock, as DMA starts, or of
// the time since then, as it ends.
void snes_syncCycles(Snes* snes, bool start, int sync) {
  if (start) {
    snes->syncCycle = snes->cycles;
    snes_runCycles(snes, sync - (int)(snes->cycles % (uint64_t)sync));
  } else {
    snes_runCycles(snes, sync - (int)((snes->cycles - snes->syncCycle) % (uint64_t)sync));
  }
}

// A frame as the 65816 would run it. The game runs its frames through the
// harness (`cosim_frame`); this is here for the console's whole interface.
void snes_runFrame(Snes* snes) {
  while (snes->inVblank) cpu_runOpcode(snes->cpu);
  const uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) cpu_runOpcode(snes->cpu);
  catch_up_sound(snes);
}

// -- the B bus -------------------------------------------------------------

uint8_t snes_readBBus(Snes* snes, uint8_t adr) {
  if (adr < 0x40) return snes->video.read(snes->video.user, adr);
  if (adr < 0x80) {
    catch_up_sound(snes);
    return snes->sound.read(snes->sound.user, adr & 3);
  }
  if (adr == 0x80) {
    const uint8_t v = snes->ram[snes->ramAdr++];
    snes->ramAdr &= 0x1ffff;
    return v;
  }
  return snes->openBus;
}

void snes_writeBBus(Snes* snes, uint8_t adr, uint8_t val) {
  if (adr < 0x40) {
    snes->video.write(snes->video.user, adr, val);
    return;
  }
  if (adr < 0x80) {
    catch_up_sound(snes);
    snes->sound.write(snes->sound.user, adr & 3, val);
    return;
  }
  switch (adr) {
    case 0x80:
      snes->ram[snes->ramAdr++] = val;
      snes->ramAdr &= 0x1ffff;
      break;
    case 0x81: snes->ramAdr = (snes->ramAdr & 0x1ff00) | val; break;
    case 0x82: snes->ramAdr = (snes->ramAdr & 0x100ff) | (uint32_t)val << 8; break;
    case 0x83: snes->ramAdr = (snes->ramAdr & 0x0ffff) | (uint32_t)(val & 1) << 16; break;
    default: break;
  }
}

// -- the console's registers, $4200-$421F ----------------------------------

static uint8_t register_read(Snes* snes, uint16_t adr) {
  switch (adr) {
    case 0x4210: {  // the NMI's flag, read and cleared, and the CPU's version
      const uint8_t v = (uint8_t)(snes->inNmi << 7 | 0x02);
      snes->inNmi = false;
      return v | (snes->openBus & 0x70);
    }
    case 0x4211: {  // the IRQ's flag, read and cleared
      const uint8_t v = (uint8_t)(snes->inIrq << 7);
      snes->inIrq = false;
      cpu_setIrq(snes->cpu, false);
      return v | (snes->openBus & 0x7f);
    }
    case 0x4212: {  // the blanks, and the pads being read
      const bool hblank = snes->hPos < 4 || snes->hPos >= 1096;
      const uint8_t v = (uint8_t)((snes->autoJoyTimer > 0) | hblank << 6 | snes->inVblank << 7);
      return v | (snes->openBus & 0x3e);
    }
    case 0x4213: return (uint8_t)(snes->ppuLatch << 7);
    case 0x4214: return (uint8_t)snes->divideResult;
    case 0x4215: return (uint8_t)(snes->divideResult >> 8);
    case 0x4216: return (uint8_t)snes->multiplyResult;
    case 0x4217: return (uint8_t)(snes->multiplyResult >> 8);
    case 0x4218: case 0x421a: case 0x421c: case 0x421e:
      return (uint8_t)snes->portAutoRead[(adr - 0x4218) / 2];
    case 0x4219: case 0x421b: case 0x421d: case 0x421f:
      return (uint8_t)(snes->portAutoRead[(adr - 0x4219) / 2] >> 8);
    default: return snes->openBus;
  }
}

static void register_write(Snes* snes, uint16_t adr, uint8_t val) {
  switch (adr) {
    case 0x4200:  // the pads' reading, the timers' IRQ and the NMI
      snes->autoJoyRead = val & 0x01;
      if (!snes->autoJoyRead) snes->autoJoyTimer = 0;
      snes->hIrqEnabled = val & 0x10;
      snes->vIrqEnabled = val & 0x20;
      if (!snes->hIrqEnabled && !snes->vIrqEnabled) {
        snes->inIrq = false;
        cpu_setIrq(snes->cpu, false);
      }
      // The NMI turned on inside the blank comes at once.
      if (!snes->nmiEnabled && (val & 0x80) && snes->inNmi) cpu_nmi(snes->cpu);
      snes->nmiEnabled = val & 0x80;
      break;
    case 0x4201:  // the I/O port; its top bit going low latches the beam
      if (!(val & 0x80) && snes->ppuLatch) snes->video.read(snes->video.user, 0x37);
      snes->ppuLatch = val & 0x80;
      break;
    case 0x4202: snes->multiplyA = val; break;
    case 0x4203: snes->multiplyResult = (uint16_t)(snes->multiplyA * val); break;
    case 0x4204: snes->divideA = (uint16_t)((snes->divideA & 0xff00) | val); break;
    case 0x4205: snes->divideA = (uint16_t)((snes->divideA & 0x00ff) | val << 8); break;
    case 0x4206:  // by nought: all ones, and the dividend left over
      if (val == 0) {
        snes->divideResult = 0xffff;
        snes->multiplyResult = snes->divideA;
      } else {
        snes->divideResult = (uint16_t)(snes->divideA / val);
        snes->multiplyResult = (uint16_t)(snes->divideA % val);
      }
      break;
    case 0x4207: snes->hTimer = (uint16_t)((snes->hTimer & 0x100) | val); break;
    case 0x4208: snes->hTimer = (uint16_t)((snes->hTimer & 0x0ff) | (val & 1) << 8); break;
    case 0x4209: snes->vTimer = (uint16_t)((snes->vTimer & 0x100) | val); break;
    case 0x420a: snes->vTimer = (uint16_t)((snes->vTimer & 0x0ff) | (val & 1) << 8); break;
    case 0x420b: dma_startDma(snes->dma, val, false); break;
    case 0x420c: dma_startDma(snes->dma, val, true); break;
    case 0x420d: snes->fastMem = val & 0x01; break;
    default: break;
  }
}

// -- the A bus -------------------------------------------------------------

static bool system_bank(uint8_t bank) {
  return bank < 0x40 || (bank >= 0x80 && bank < 0xc0);
}

static uint8_t bus_read(Snes* snes, uint32_t adr24) {
  const uint8_t bank = (uint8_t)(adr24 >> 16);
  const uint16_t adr = (uint16_t)adr24;
  if (bank == 0x7e || bank == 0x7f) return snes->ram[(bank & 1) << 16 | adr];
  if (system_bank(bank)) {
    if (adr < 0x2000) return snes->ram[adr];
    if (adr >= 0x2100 && adr < 0x2200) return snes_readBBus(snes, (uint8_t)adr);
    if (adr == 0x4016) return input_read(snes->input1) | (snes->openBus & 0xfc);
    if (adr == 0x4017) return input_read(snes->input2) | (snes->openBus & 0xe0) | 0x1c;
    if (adr >= 0x4200 && adr < 0x4220) return register_read(snes, adr);
    if (adr >= 0x4300 && adr < 0x4380) return dma_read(snes->dma, adr);
  }
  return cart_read(snes->cart, bank, adr);
}

uint8_t snes_read(Snes* snes, uint32_t adr) {
  snes->openBus = bus_read(snes, adr);
  return snes->openBus;
}

// A write goes to everything at the address, the cartridge too.
void snes_write(Snes* snes, uint32_t adr24, uint8_t val) {
  snes->openBus = val;
  const uint8_t bank = (uint8_t)(adr24 >> 16);
  const uint16_t adr = (uint16_t)adr24;
  if (bank == 0x7e || bank == 0x7f) snes->ram[(bank & 1) << 16 | adr] = val;
  if (system_bank(bank)) {
    if (adr < 0x2000) snes->ram[adr] = val;
    if (adr >= 0x2100 && adr < 0x2200) snes_writeBBus(snes, (uint8_t)adr, val);
    if (adr == 0x4016) {
      input_latch(snes->input1, val & 1);
      input_latch(snes->input2, val & 1);
    }
    if (adr >= 0x4200 && adr < 0x4220) register_write(snes, adr, val);
    if (adr >= 0x4300 && adr < 0x4380) dma_write(snes->dma, adr, val);
  }
  cart_write(snes->cart, bank, adr, val);
}

// Master cycles an access of the CPU's takes.
static int access_cycles(const Snes* snes, uint32_t adr24) {
  const uint8_t bank = (uint8_t)(adr24 >> 16);
  const uint16_t adr = (uint16_t)adr24;
  if (system_bank(bank) && adr < 0x8000) {
    if (adr < 0x2000 || adr >= 0x6000) return 8;
    if (adr < 0x4000 || adr >= 0x4200) return 6;
    return 12;  // $4000-$41FF, the pads' old ports
  }
  return snes->fastMem && bank >= 0x80 ? 6 : 8;
}

// -- the CPU's side --------------------------------------------------------

void snes_cpuIdle(void* mem, bool waiting) {
  (void)waiting;
  Snes* snes = mem;
  dma_handleDma(snes->dma, 6);
  snes_runCycles(snes, 6);
}

uint8_t snes_cpuRead(void* mem, uint32_t adr) {
  Snes* snes = mem;
  const uint64_t cpu_clock = snes->cycles - snes->stolenCycles;
  const int cycles = access_cycles(snes, adr);
  dma_handleDma(snes->dma, cycles);
  snes_runCycles(snes, cycles);
  const uint8_t v = snes_read(snes, adr);
  if (snes->readHook) snes->readHook(snes, adr, v, cpu_clock, snes->readHookCtx);
  return v;
}

void snes_cpuWrite(void* mem, uint32_t adr, uint8_t val) {
  Snes* snes = mem;
  const uint64_t cpu_clock = snes->cycles - snes->stolenCycles;
  const int cycles = access_cycles(snes, adr);
  dma_handleDma(snes->dma, cycles);
  snes_runCycles(snes, cycles);
  snes_write(snes, adr, val);
  if (snes->writeHook) snes->writeHook(snes, adr, val, cpu_clock, snes->writeHookCtx);
}

void snes_runCpuCycle(Snes* snes) { cpu_runOpcode(snes->cpu); }

// -- the frontend's side ---------------------------------------------------

void snes_setButtonState(Snes* snes, int player, int button, bool pressed) {
  Input* pad = player == 1 ? snes->input1 : snes->input2;
  if (pressed) pad->currentState |= (uint16_t)(1 << button);
  else pad->currentState &= (uint16_t)~(1 << button);
}

void snes_setFrameHook(Snes* snes, SnesFrameHook hook, void* ctx) {
  snes->frameHook = hook;
  snes->frameHookCtx = ctx;
}

void snes_setWriteHook(Snes* snes, SnesWriteHook hook, void* ctx) {
  snes->writeHook = hook;
  snes->writeHookCtx = ctx;
}

void snes_setReadHook(Snes* snes, SnesReadHook hook, void* ctx) {
  snes->readHook = hook;
  snes->readHookCtx = ctx;
}

// The frame's audio: two channels of 16 bits, `samplesPerFrame` of each.
void snes_setSamples(Snes* snes, int16_t* sampleData, int samplesPerFrame) {
  if (!snes->sound.getSamples) no_sound();
  snes->sound.getSamples(snes->sound.user, sampleData, samplesPerFrame);
}

// -- saved states ------------------------------------------------------------

// LakeSnes's layout, so that a quick save loads in either build: "LSSF", the
// version, the length, the cartridge's kind and sizes, then the machine.
enum { STATE_ID = 0x4653534c, STATE_VERSION = 2 };

int snes_saveState(Snes* snes, uint8_t* data) {
  StateHandler* sh = sh_init(true, NULL, 0);
  uint32_t id = STATE_ID, version = STATE_VERSION, length = 0;
  sh_handleInts(sh, &id, &version, &length, NULL);
  cart_handleTypeState(snes->cart, sh);
  snes_handleState(snes, sh);
  sh_placeInt(sh, 8, (uint32_t)sh->offset);
  if (data) memcpy(data, sh->data, (size_t)sh->offset);
  const int size = sh->offset;
  sh_free(sh);
  return size;
}

bool snes_loadState(Snes* snes, uint8_t* data, int size) {
  StateHandler* sh = sh_init(false, data, size);
  uint32_t id = 0, version = 0, length = 0;
  sh_handleInts(sh, &id, &version, &length, NULL);
  const bool same_cart = cart_handleTypeState(snes->cart, sh);
  const bool ok = id == STATE_ID && version == STATE_VERSION &&
                  length == (uint32_t)size && same_cart;
  if (ok) snes_handleState(snes, sh);
  sh_free(sh);
  return ok;
}

int snes_saveBattery(Snes* snes, uint8_t* data) {
  int size = 0;
  cart_handleBattery(snes->cart, true, data, &size);
  return size;
}

bool snes_loadBattery(Snes* snes, uint8_t* data, int size) {
  return cart_handleBattery(snes->cart, false, data, &size);
}
