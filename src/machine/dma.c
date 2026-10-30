// The console's DMA and HDMA, for `zamn`.
//
// It is what LakeSnes's `dma.h` declares, on its `Dma`, which the harness
// reads too (`cosim.c` asks whether HDMA is on). Each of the eight channels
// copies between the A bus (ROM, WRAM) and the B bus (`$2100`-`$21FF`: the
// video chip, the sound ports, `$2180`).
//
// **DMA**, started by `$420B`, takes the bus from the CPU: from the write's
// next access the console runs the copy, eight master cycles a byte, after
// eight to line up and eight more for the transfer and for each channel.
// Then the CPU's clock is lined up again.
//
// **HDMA** copies a few bytes to the B bus at the start of each visible
// line, from a table of counts and data: at the top of the frame each
// channel loads its table's first count (and, indirect, the address of its
// data), and each line it copies its unit if the count says to, then counts
// down and loads the next entry when it runs out. The console asks for both
// (`hdmaInitRequested`, `hdmaRunRequested`), and they are run here at the
// CPU's next access, or inside a DMA's copy if one is running.
//
// Every cycle is spent through the console (`snes_runCycles`), which moves
// the beam, so where each byte lands in the frame is the console's.
#include <stdlib.h>

#include "dma.h"
#include "snes.h"

// The B-bus registers a unit of each mode goes to, from the channel's.
static const uint8_t unit_offsets[8][4] = {
    {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
};

// Bytes in a unit of each mode: what HDMA copies in a line.
static const uint8_t unit_length[8] = {1, 2, 2, 4, 4, 4, 2, 4};

enum { BYTE_CYCLES = 8 };

// `dmaState`: none, started (it begins at the CPU's next access), running.
enum { DMA_IDLE = 0, DMA_STARTED = 1, DMA_DUE = 2 };

Dma* dma_init(Snes* snes) {
  Dma* dma = calloc(1, sizeof(Dma));
  if (dma) dma->snes = snes;
  return dma;
}

void dma_free(Dma* dma) { free(dma); }

// At power on every register reads all ones.
void dma_reset(Dma* dma) {
  for (int i = 0; i < 8; i++) {
    DmaChannel* ch = &dma->channel[i];
    ch->bAdr = 0xff;
    ch->aAdr = 0xffff;
    ch->aBank = 0xff;
    ch->size = 0xffff;
    ch->indBank = 0xff;
    ch->tableAdr = 0xffff;
    ch->repCount = 0xff;
    ch->unusedByte = 0xff;
    ch->dmaActive = false;
    ch->hdmaActive = false;
    ch->mode = 7;
    ch->fixed = ch->decrement = ch->indirect = ch->fromB = ch->unusedBit = true;
    ch->doTransfer = false;
    ch->terminated = false;
  }
  dma->dmaState = DMA_IDLE;
  dma->hdmaInitRequested = false;
  dma->hdmaRunRequested = false;
}

void dma_handleState(Dma* dma, StateHandler* sh) {
  sh_handleBools(sh, &dma->hdmaInitRequested, &dma->hdmaRunRequested, NULL);
  sh_handleBytes(sh, &dma->dmaState, NULL);
  for (int i = 0; i < 8; i++) {
    DmaChannel* ch = &dma->channel[i];
    sh_handleBools(sh, &ch->dmaActive, &ch->hdmaActive, &ch->fixed,
                   &ch->decrement, &ch->indirect, &ch->fromB, &ch->unusedBit,
                   &ch->doTransfer, &ch->terminated, NULL);
    sh_handleBytes(sh, &ch->bAdr, &ch->aBank, &ch->indBank, &ch->repCount,
                   &ch->unusedByte, &ch->mode, NULL);
    sh_handleWords(sh, &ch->aAdr, &ch->size, &ch->tableAdr, NULL);
  }
}

// `$43x0`-`$43xF`.
uint8_t dma_read(Dma* dma, uint16_t adr) {
  const DmaChannel* ch = &dma->channel[(adr >> 4) & 7];
  switch (adr & 0xf) {
    case 0x0:
      return (uint8_t)(ch->mode | ch->fixed << 3 | ch->decrement << 4 |
                       ch->unusedBit << 5 | ch->indirect << 6 | ch->fromB << 7);
    case 0x1: return ch->bAdr;
    case 0x2: return (uint8_t)ch->aAdr;
    case 0x3: return (uint8_t)(ch->aAdr >> 8);
    case 0x4: return ch->aBank;
    case 0x5: return (uint8_t)ch->size;
    case 0x6: return (uint8_t)(ch->size >> 8);
    case 0x7: return ch->indBank;
    case 0x8: return (uint8_t)ch->tableAdr;
    case 0x9: return (uint8_t)(ch->tableAdr >> 8);
    case 0xa: return ch->repCount;
    case 0xb:
    case 0xf: return ch->unusedByte;
    default: return dma->snes->openBus;
  }
}

static uint16_t set_lo(uint16_t w, uint8_t v) { return (uint16_t)((w & 0xff00) | v); }
static uint16_t set_hi(uint16_t w, uint8_t v) { return (uint16_t)((w & 0x00ff) | v << 8); }

void dma_write(Dma* dma, uint16_t adr, uint8_t val) {
  DmaChannel* ch = &dma->channel[(adr >> 4) & 7];
  switch (adr & 0xf) {
    case 0x0:
      ch->mode = val & 7;
      ch->fixed = val & 0x08;
      ch->decrement = val & 0x10;
      ch->unusedBit = val & 0x20;
      ch->indirect = val & 0x40;
      ch->fromB = val & 0x80;
      break;
    case 0x1: ch->bAdr = val; break;
    case 0x2: ch->aAdr = set_lo(ch->aAdr, val); break;
    case 0x3: ch->aAdr = set_hi(ch->aAdr, val); break;
    case 0x4: ch->aBank = val; break;
    case 0x5: ch->size = set_lo(ch->size, val); break;
    case 0x6: ch->size = set_hi(ch->size, val); break;
    case 0x7: ch->indBank = val; break;
    case 0x8: ch->tableAdr = set_lo(ch->tableAdr, val); break;
    case 0x9: ch->tableAdr = set_hi(ch->tableAdr, val); break;
    case 0xa: ch->repCount = val; break;
    case 0xb:
    case 0xf: ch->unusedByte = val; break;
    default: break;
  }
}

// Banks $00-$3F and $80-$BF, where the console's registers are.
static bool system_bank(uint8_t bank) {
  return bank < 0x40 || (bank >= 0x80 && bank < 0xc0);
}

// One byte across. `$2180` on the B bus, with WRAM on the A bus, gives open
// bus, and so do the B bus and the DMA registers seen from the A bus.
static void copy_byte(Dma* dma, uint16_t a_adr, uint8_t a_bank, uint8_t b_adr,
                      bool from_b) {
  Snes* snes = dma->snes;
  const bool wram = a_bank == 0x7e || a_bank == 0x7f ||
                    (system_bank(a_bank) && a_adr < 0x2000);
  const bool b_ok = !(b_adr == 0x80 && wram);
  const bool a_ok =
      !(system_bank(a_bank) &&
        (a_adr == 0x420b || a_adr == 0x420c ||
         (a_adr >= 0x4300 && a_adr < 0x4380) ||
         (a_adr >= 0x2100 && a_adr < 0x2200)));
  const uint32_t a = (uint32_t)a_bank << 16 | a_adr;
  if (from_b) {
    const uint8_t v = b_ok ? snes_readBBus(snes, b_adr) : snes->openBus;
    if (a_ok) snes_write(snes, a, v);
  } else {
    const uint8_t v = a_ok ? snes_read(snes, a) : snes->openBus;
    if (b_ok) snes_writeBBus(snes, b_adr, v);
  }
}

// A byte of HDMA's table, eight cycles.
static uint8_t table_byte(Dma* dma, DmaChannel* ch) {
  snes_runCycles(dma->snes, BYTE_CYCLES);
  return snes_read(dma->snes, (uint32_t)ch->aBank << 16 | ch->tableAdr++);
}

static void hdma_begin(Dma* dma, bool sync, int cpu_cycles);
static void hdma_line(Dma* dma, bool sync, int cpu_cycles);

// Eight cycles of a DMA's copy, with the frame's HDMA if it falls due in it.
static void dma_cycle(Dma* dma) {
  if (dma->hdmaInitRequested) hdma_begin(dma, false, 0);
  if (dma->hdmaRunRequested) hdma_line(dma, false, 0);
  snes_runCycles(dma->snes, BYTE_CYCLES);
}

static void dma_run(Dma* dma, int cpu_cycles) {
  snes_syncCycles(dma->snes, true, BYTE_CYCLES);
  dma_cycle(dma);  // the transfer's own
  for (int i = 0; i < 8; i++) {
    DmaChannel* ch = &dma->channel[i];
    if (!ch->dmaActive) continue;
    dma_cycle(dma);  // the channel's own
    for (int unit = 0; ch->dmaActive; unit = (unit + 1) & 3) {
      dma_cycle(dma);
      copy_byte(dma, ch->aAdr, ch->aBank,
                (uint8_t)(ch->bAdr + unit_offsets[ch->mode][unit]), ch->fromB);
      if (!ch->fixed) ch->aAdr = (uint16_t)(ch->aAdr + (ch->decrement ? -1 : 1));
      if (--ch->size == 0) ch->dmaActive = false;
    }
  }
  snes_syncCycles(dma->snes, false, cpu_cycles);
}

// The top of the frame: each channel on reads its first count, and an
// indirect one the address of its data. A channel on ends any DMA of its own.
static void hdma_begin(Dma* dma, bool sync, int cpu_cycles) {
  dma->hdmaInitRequested = false;
  bool any = false;
  for (int i = 0; i < 8; i++) {
    any |= dma->channel[i].hdmaActive;
    dma->channel[i].doTransfer = false;
    dma->channel[i].terminated = false;
  }
  if (!any) return;
  if (sync) snes_syncCycles(dma->snes, true, BYTE_CYCLES);
  snes_runCycles(dma->snes, BYTE_CYCLES);
  for (int i = 0; i < 8; i++) {
    DmaChannel* ch = &dma->channel[i];
    if (!ch->hdmaActive) continue;
    ch->dmaActive = false;
    ch->tableAdr = ch->aAdr;
    ch->repCount = table_byte(dma, ch);
    if (ch->repCount == 0) ch->terminated = true;
    if (ch->indirect) {
      ch->size = table_byte(dma, ch);
      ch->size |= (uint16_t)(table_byte(dma, ch) << 8);
    }
    ch->doTransfer = true;
  }
  if (sync) snes_syncCycles(dma->snes, false, cpu_cycles);
}

// A visible line: each channel copies its unit if it is due, then counts
// down. A count's top bit is "every line"; without it the unit is copied
// once and held. At nought the next entry is read. The last channel still
// running, at its table's end, reads only the high byte of an indirect
// address, and its low byte is nought.
static void hdma_line(Dma* dma, bool sync, int cpu_cycles) {
  dma->hdmaRunRequested = false;
  bool any = false;
  int last = 0;
  for (int i = 0; i < 8; i++) {
    if (!dma->channel[i].hdmaActive) continue;
    any = true;
    if (!dma->channel[i].terminated) last = i;
  }
  if (!any) return;
  if (sync) snes_syncCycles(dma->snes, true, BYTE_CYCLES);
  snes_runCycles(dma->snes, BYTE_CYCLES);
  for (int i = 0; i < 8; i++) {
    DmaChannel* ch = &dma->channel[i];
    if (ch->hdmaActive) ch->dmaActive = false;
    if (!ch->hdmaActive || ch->terminated || !ch->doTransfer) continue;
    for (int j = 0; j < unit_length[ch->mode]; j++) {
      snes_runCycles(dma->snes, BYTE_CYCLES);
      const uint8_t b = (uint8_t)(ch->bAdr + unit_offsets[ch->mode][j]);
      if (ch->indirect) copy_byte(dma, ch->size++, ch->indBank, b, ch->fromB);
      else copy_byte(dma, ch->tableAdr++, ch->aBank, b, ch->fromB);
    }
  }
  for (int i = 0; i < 8; i++) {
    DmaChannel* ch = &dma->channel[i];
    if (!ch->hdmaActive || ch->terminated) continue;
    ch->repCount--;
    ch->doTransfer = ch->repCount & 0x80;
    snes_runCycles(dma->snes, BYTE_CYCLES);
    const uint8_t next = snes_read(dma->snes, (uint32_t)ch->aBank << 16 | ch->tableAdr);
    if ((ch->repCount & 0x7f) != 0) continue;
    ch->repCount = next;
    ch->tableAdr++;
    if (ch->indirect) {
      if (ch->repCount == 0 && i == last) ch->size = 0;
      else ch->size = table_byte(dma, ch);
      ch->size |= (uint16_t)(table_byte(dma, ch) << 8);
    }
    if (ch->repCount == 0) ch->terminated = true;
    ch->doTransfer = true;
  }
  if (sync) snes_syncCycles(dma->snes, false, cpu_cycles);
}

// At each access of the CPU's, before it: HDMA that has fallen due, unless a
// DMA is about to run, which runs it in its copy; and a DMA one access after
// the write that started it. All of it is time the CPU did not have.
void dma_handleDma(Dma* dma, int cpuCycles) {
  Snes* snes = dma->snes;
  const uint64_t before = snes->cycles;
  snes->inDma = true;
  if (dma->hdmaInitRequested && dma->dmaState != DMA_DUE) hdma_begin(dma, true, cpuCycles);
  if (dma->hdmaRunRequested && dma->dmaState != DMA_DUE) hdma_line(dma, true, cpuCycles);
  if (dma->dmaState == DMA_STARTED) {
    dma->dmaState = DMA_DUE;
  } else if (dma->dmaState == DMA_DUE) {
    dma_run(dma, cpuCycles);
    dma->dmaState = DMA_IDLE;
  }
  snes->inDma = false;
  snes->stolenCycles += snes->cycles - before;
}

// `$420B` (DMA) or `$420C` (HDMA): the channels on.
void dma_startDma(Dma* dma, uint8_t val, bool hdma) {
  for (int i = 0; i < 8; i++) {
    if (hdma) dma->channel[i].hdmaActive = val & (1 << i);
    else dma->channel[i].dmaActive = val & (1 << i);
  }
  if (!hdma) dma->dmaState = val != 0 ? DMA_STARTED : DMA_IDLE;
}
