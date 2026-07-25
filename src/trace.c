// zamn_trace — Phase 1 dynamic-analysis instrument.
//
// Drives the vendored SNES core one instruction at a time (instead of one
// frame at a time) with scripted input, and records everything the CPU touches:
//
//   * a Code/Data Log      — which ROM bytes are code, operands, or data
//   * a WRAM access map    — every RAM address, who reads it, who writes it
//   * a register map       — every $21xx/$42xx/$43xx access and its caller
//   * a call graph         — every JSR/JSL edge, with hit counts
//   * a DMA log            — every transfer's ROM source, destination, length
//   * optional instruction traces of reset and of a chosen NMI
//
// The core is not modified. Instrumentation works by swapping the CPU's
// read/write handler pointers for wrappers and stepping cpu_runOpcode()
// directly, both of which the core already exposes.
//
// Usage:
//   zamn_trace <rom.sfc> [options]
//     -o, --out <dir>        output directory (default: analysis/)
//     -f, --frames <n>       frames to run (default: 600)
//     -m, --movie <file>     scripted controller input
//         --trace-reset <n>  dump the first <n> instructions after reset
//         --trace-nmi <f>    dump the NMI that fires at/after frame <f>
//         --png <file>       write the final frame, to eyeball where the movie got to
//         --stats-from <f>   ignore memory accesses before frame <f>, so a boot
//                            sequence does not swamp a gameplay memory map

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"

#include "analysis/cdl.h"
#include "analysis/movie_apply.h"
#include "analysis/w65816.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define MAX_PCS 4          // distinct accessor PCs remembered per address
#define REG_BASE 0x2000    // register window tracked in banks $00-$3F/$80-$BF
#define REG_END 0x6000
#define EDGE_CAP (1 << 17)
#define DMA_CAP 8192

typedef struct {
  uint32_t reads, writes;
  uint32_t bulk;  // touched by an MVN/MVP block move (a bulk clear or copy)
  int32_t first_read_frame, first_write_frame;
  uint32_t reader_pc[MAX_PCS], writer_pc[MAX_PCS];
  uint8_t n_readers, n_writers;
  uint8_t word_low, word_high;  // seen as the low/high half of a 16-bit access
} MemStat;

typedef struct {
  uint32_t caller, callee, count;
  bool used;
} CallEdge;

// An instruction trace with loop collapsing. Without it a single MVN or an
// empty-slot scan buries the interesting instructions under thousands of
// identical lines.
#define TRACE_HIST 64
#define TRACE_MAX_PERIOD 16

typedef struct {
  FILE* f;
  uint32_t hist[TRACE_HIST];
  int hist_pos, hist_len;
  int suppress_period;
  uint64_t suppressed;
} TraceOut;

typedef struct {
  uint8_t channel, mode, b_adr;
  bool from_b;
  bool fixed;        // source address does not advance: this is a fill, not a copy
  bool decrement;
  uint32_t src;      // 24-bit A-bus address at the start of the transfer
  uint32_t length;   // bytes
  uint32_t pc;       // code address that kicked it off
  uint32_t count;    // how many times this exact transfer was seen
  int32_t first_frame;
} DmaEvent;

static struct {
  Snes* snes;
  Cdl cdl;
  const uint8_t* rom;
  uint32_t rom_size;

  MemStat* wram;  // 0x20000
  MemStat* reg;   // REG_END - REG_BASE
  uint32_t* exec_count;  // per ROM byte
  uint32_t* call_count;  // per ROM byte

  CallEdge* edges;
  uint32_t edge_count;

  DmaEvent dma[DMA_CAP];
  uint32_t dma_count;

  // current instruction, for classifying reads as fetch vs data
  uint32_t cur_pc;
  uint8_t cur_opcode;
  int cur_len;
  int frame;
  int stats_from;  // ignore memory accesses before this frame

  // 16-bit access detection
  uint32_t last_adr, last_pc;
  bool last_was_write, last_valid;

  uint64_t instructions;
  uint64_t nmi_count, irq_count, brk_count;
  uint32_t nmi_vector, irq_vector, reset_vector;

  // instruction trace state
  TraceOut reset_trace;
  int64_t trace_remaining;
  int nmi_trace_frame;
  bool nmi_tracing;
  int nmi_trace_depth;
  TraceOut nmi_trace;
} g;

// ---------------------------------------------------------------------------
// Side-effect-free memory peek, for reading instruction bytes for disassembly
// ---------------------------------------------------------------------------

static uint8_t peek(uint32_t adr) {
  uint32_t off;
  if (snes_to_rom(adr, g.rom_size, &off)) return g.rom[off];
  if (snes_to_wram(adr, &off)) return g.snes->ram[off];
  return 0;
}

static void peek_bytes(uint32_t pc, uint8_t* out, int n) {
  // Instruction bytes never wrap out of the program bank on a 65816.
  for (int i = 0; i < n; i++) out[i] = peek((pc & 0xff0000) | ((pc + i) & 0xffff));
}

// ---------------------------------------------------------------------------
// Access recording
// ---------------------------------------------------------------------------

static void add_pc(uint32_t* list, uint8_t* n, uint32_t pc) {
  for (uint8_t i = 0; i < *n; i++) {
    if (list[i] == pc) return;
  }
  if (*n < MAX_PCS) list[(*n)++] = pc;
}

static MemStat* stat_for(uint32_t adr) {
  uint32_t off;
  if (snes_to_wram(adr, &off)) return &g.wram[off];
  uint32_t bank = (adr >> 16) & 0xff;
  uint32_t low = adr & 0xffff;
  bool low_bank = bank < 0x40 || (bank >= 0x80 && bank < 0xc0);
  if (low_bank && low >= REG_BASE && low < REG_END) return &g.reg[low - REG_BASE];
  return NULL;
}

static void record_access(uint32_t adr, bool is_write) {
  if (g.frame < g.stats_from) return;
  MemStat* s = stat_for(adr);
  if (!s) return;
  // Block moves are bulk clears and table copies, not variable accesses. Left
  // in, the boot-time MVN that zeroes all of WRAM makes every byte look live
  // and drowns out the real memory map.
  if (g.cur_opcode == 0x44 || g.cur_opcode == 0x54) {  // MVP / MVN
    s->bulk++;
    return;
  }
  if (is_write) {
    if (s->writes == 0) s->first_write_frame = g.frame;
    s->writes++;
    add_pc(s->writer_pc, &s->n_writers, g.cur_pc);
  } else {
    if (s->reads == 0) s->first_read_frame = g.frame;
    s->reads++;
    add_pc(s->reader_pc, &s->n_readers, g.cur_pc);
  }
  // A 16-bit load/store shows up as two byte accesses to adr and adr+1 from the
  // same instruction. Detecting that tells us which RAM slots are word-sized.
  if (g.last_valid && g.last_pc == g.cur_pc && g.last_was_write == is_write &&
      adr == g.last_adr + 1) {
    MemStat* prev = stat_for(g.last_adr);
    if (prev) prev->word_low = 1;
    s->word_high = 1;
  }
  g.last_adr = adr;
  g.last_pc = g.cur_pc;
  g.last_was_write = is_write;
  g.last_valid = true;
}

static void mark_rom(uint32_t adr, uint8_t flags) {
  uint32_t off;
  if (snes_to_rom(adr, g.rom_size, &off)) g.cdl.flags[off] |= flags;
}

// ---------------------------------------------------------------------------
// DMA capture
// ---------------------------------------------------------------------------

static void record_dma(uint8_t enable_mask) {
  for (int ch = 0; ch < 8; ch++) {
    if (!(enable_mask & (1 << ch))) continue;
    DmaChannel* c = &g.snes->dma->channel[ch];
    uint32_t length = c->size ? c->size : 0x10000;
    uint32_t src = ((uint32_t)c->aBank << 16) | c->aAdr;

    // An A-bus -> B-bus transfer is the game shipping ROM data to the PPU, so
    // mark the whole source range as data. That is where the graphics live.
    //
    // A fixed-source transfer is a *fill* (clearing VRAM by streaming the same
    // word thousands of times), so only the handful of bytes the mode actually
    // re-reads are data. Marking the nominal length there would smear "data"
    // across tens of kilobytes of code.
    if (!c->fromB) {
      static const uint8_t kModeBytes[8] = {1, 2, 2, 4, 4, 4, 2, 4};
      uint32_t span = c->fixed ? kModeBytes[c->mode & 7] : length;
      if (span > length) span = length;
      for (uint32_t i = 0; i < span; i++) {
        uint32_t a = (src & 0xff0000) |
                     ((c->decrement ? c->aAdr - i : c->aAdr + i) & 0xffff);
        mark_rom(a, CDL_DATA);
      }
    }

    bool seen = false;
    for (uint32_t i = 0; i < g.dma_count; i++) {
      DmaEvent* e = &g.dma[i];
      if (e->channel == ch && e->src == src && e->length == length &&
          e->b_adr == c->bAdr && e->mode == c->mode && e->from_b == c->fromB &&
          e->fixed == c->fixed) {
        e->count++;
        seen = true;
        break;
      }
    }
    if (!seen && g.dma_count < DMA_CAP) {
      DmaEvent* e = &g.dma[g.dma_count++];
      e->channel = (uint8_t)ch;
      e->mode = c->mode;
      e->b_adr = c->bAdr;
      e->from_b = c->fromB;
      e->fixed = c->fixed;
      e->decrement = c->decrement;
      e->src = src;
      e->length = length;
      e->pc = g.cur_pc;
      e->count = 1;
      e->first_frame = g.frame;
    }
  }
}

// ---------------------------------------------------------------------------
// CPU read/write hooks
// ---------------------------------------------------------------------------

static bool is_instruction_fetch(uint32_t adr) {
  if (g.cur_len <= 0) return false;
  if ((adr & 0xff0000) != (g.cur_pc & 0xff0000)) return false;
  uint16_t off = (uint16_t)(adr - g.cur_pc);
  return off < (uint16_t)g.cur_len;
}

static uint8_t trace_read(void* mem, uint32_t adr) {
  if (!is_instruction_fetch(adr)) {
    uint32_t off;
    if (snes_to_rom(adr, g.rom_size, &off)) g.cdl.flags[off] |= CDL_DATA;
    else record_access(adr, false);
  }
  return snes_cpuRead(mem, adr);
}

static void trace_write(void* mem, uint32_t adr, uint8_t val) {
  record_access(adr, true);
  // $420B (MDMAEN) starts a general-purpose DMA. Snapshot the channel registers
  // now, before the transfer consumes them.
  uint32_t bank = (adr >> 16) & 0xff;
  bool low_bank = bank < 0x40 || (bank >= 0x80 && bank < 0xc0);
  if (low_bank && (adr & 0xffff) == 0x420b && val != 0) record_dma(val);
  snes_cpuWrite(mem, adr, val);
}

// ---------------------------------------------------------------------------
// Instruction trace output
// ---------------------------------------------------------------------------

static void write_trace_line(FILE* f, uint32_t pc, const uint8_t* bytes, int len,
                             const Cpu* cpu) {
  char text[64];
  w65816_disasm(text, sizeof text, bytes, pc, cpu->mf, cpu->xf);

  char raw[16] = {0};
  int n = 0;
  for (int i = 0; i < len && i < 4; i++)
    n += snprintf(raw + n, sizeof raw - (size_t)n, "%02X ", bytes[i]);

  fprintf(f,
          "$%02X:%04X  %-12s%-22s A:%04X X:%04X Y:%04X S:%04X D:%04X DB:%02X "
          "%c%c%c%c%c%c%c%c\n",
          pc >> 16, pc & 0xffff, raw, text, cpu->a, cpu->x, cpu->y, cpu->sp,
          cpu->dp, cpu->db, cpu->n ? 'N' : '.', cpu->v ? 'V' : '.',
          cpu->mf ? 'M' : '.', cpu->xf ? 'X' : '.', cpu->d ? 'D' : '.',
          cpu->i ? 'I' : '.', cpu->z ? 'Z' : '.', cpu->c ? 'C' : '.');
}

static uint32_t hist_at(const TraceOut* t, int back) {
  return t->hist[(t->hist_pos - back + TRACE_HIST) % TRACE_HIST];
}

static void hist_push(TraceOut* t, uint32_t pc) {
  t->hist[t->hist_pos] = pc;
  t->hist_pos = (t->hist_pos + 1) % TRACE_HIST;
  if (t->hist_len < TRACE_HIST) t->hist_len++;
}

// Would `pc` continue a p-instruction cycle that has already run twice?
static bool detect_loop(const TraceOut* t, uint32_t pc, int* out_period) {
  for (int p = 1; p <= TRACE_MAX_PERIOD; p++) {
    if (t->hist_len < 2 * p) break;
    if (hist_at(t, p) != pc) continue;
    bool same = true;
    for (int i = 0; i < p && same; i++)
      same = hist_at(t, 1 + i) == hist_at(t, 1 + p + i);
    if (same) { *out_period = p; return true; }
  }
  return false;
}

static void trace_flush_loop(TraceOut* t) {
  if (t->suppress_period <= 0) return;
  fprintf(t->f, "          ... %d-instruction loop, %llu further instructions elided\n",
          t->suppress_period, (unsigned long long)t->suppressed);
  t->suppress_period = 0;
  t->suppressed = 0;
}

static void trace_emit(TraceOut* t, uint32_t pc, const uint8_t* bytes, int len,
                       const Cpu* cpu) {
  if (t->suppress_period > 0) {
    if (hist_at(t, t->suppress_period) == pc) {
      hist_push(t, pc);
      t->suppressed++;
      return;
    }
    trace_flush_loop(t);
  }
  int period;
  if (detect_loop(t, pc, &period)) {
    t->suppress_period = period;
    t->suppressed = 1;
    hist_push(t, pc);
    return;
  }
  hist_push(t, pc);
  write_trace_line(t->f, pc, bytes, len, cpu);
}

static void trace_close(TraceOut* t) {
  if (!t->f) return;
  trace_flush_loop(t);
  fclose(t->f);
  t->f = NULL;
}

// ---------------------------------------------------------------------------
// Call graph
// ---------------------------------------------------------------------------

static void add_edge(uint32_t caller, uint32_t callee) {
  uint32_t h = (caller * 2654435761u) ^ (callee * 40503u);
  uint32_t i = h & (EDGE_CAP - 1);
  for (uint32_t probe = 0; probe < EDGE_CAP; probe++) {
    CallEdge* e = &g.edges[i];
    if (!e->used) {
      e->used = true;
      e->caller = caller;
      e->callee = callee;
      e->count = 1;
      g.edge_count++;
      return;
    }
    if (e->caller == caller && e->callee == callee) {
      e->count++;
      return;
    }
    i = (i + 1) & (EDGE_CAP - 1);
  }
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

static void step(Snes* snes) {
  Cpu* cpu = snes->cpu;

  // Reset dispatch, STP, and WAI all consume cycles without executing an
  // opcode at PC, so there is nothing to log or mark for them.
  if (cpu->resetWanted || cpu->stopped || cpu->waiting) {
    g.cur_len = 0;
    g.cur_opcode = 0;
    snes_runCpuCycle(snes);
    return;
  }

  // Interrupt dispatch: no opcode runs, the CPU vectors instead.
  if (cpu->intWanted) {
    g.cur_len = 0;
    g.cur_opcode = 0;
    snes_runCpuCycle(snes);
    uint32_t target = ((uint32_t)cpu->k << 16) | cpu->pc;
    if (target == g.nmi_vector) g.nmi_count++;
    else if (target == g.irq_vector) g.irq_count++;
    mark_rom(target, CDL_SUB);
    if (g.nmi_tracing) {
      g.nmi_trace_depth++;
    } else if (g.nmi_trace.f && g.frame >= g.nmi_trace_frame && target == g.nmi_vector) {
      g.nmi_tracing = true;
      g.nmi_trace_depth = 1;
    }
    return;
  }

  uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
  uint8_t bytes[4];
  peek_bytes(pc, bytes, 4);
  uint8_t opcode = bytes[0];
  bool mf = cpu->mf, xf = cpu->xf;
  int len = w65816_len(opcode, mf, xf);

  // Code/Data Log markup for this instruction.
  uint32_t off;
  if (snes_to_rom(pc, g.rom_size, &off)) {
    uint8_t* flags = &g.cdl.flags[off];
    uint8_t want = (uint8_t)(CDL_CODE | (mf ? 0 : CDL_M16) | (xf ? 0 : CDL_X16));
    // Seeing the same opcode at two different register widths means a static
    // disassembly of it is ambiguous; flag it rather than silently picking one.
    if ((*flags & CDL_CODE) &&
        (((*flags & CDL_M16) != 0) != !mf || ((*flags & CDL_X16) != 0) != !xf)) {
      want |= CDL_CONFLICT;
    }
    *flags |= want;
    for (int i = 1; i < len; i++) mark_rom((pc & 0xff0000) | ((pc + i) & 0xffff), CDL_OPERAND);
    g.exec_count[off]++;
  }

  if (g.reset_trace.f && g.trace_remaining > 0) {
    trace_emit(&g.reset_trace, pc, bytes, len, cpu);
    if (--g.trace_remaining == 0) trace_close(&g.reset_trace);
  }
  if (g.nmi_tracing && g.nmi_trace.f) trace_emit(&g.nmi_trace, pc, bytes, len, cpu);

  g.cur_pc = pc;
  g.cur_opcode = opcode;
  g.cur_len = len;
  g.last_valid = false;
  g.instructions++;

  snes_runCpuCycle(snes);

  uint32_t new_pc = ((uint32_t)cpu->k << 16) | cpu->pc;
  switch (w65816_ops[opcode].flow) {
    case FLOW_CALL:
      mark_rom(new_pc, CDL_SUB);
      if (snes_to_rom(new_pc, g.rom_size, &off)) g.call_count[off]++;
      add_edge(pc, new_pc);
      break;
    case FLOW_JUMP:
      mark_rom(new_pc, CDL_JUMP);
      break;
    case FLOW_BRANCH:
      if (new_pc != ((pc & 0xff0000) | ((pc + len) & 0xffff))) mark_rom(new_pc, CDL_JUMP);
      break;
    case FLOW_RET:
      if (opcode == 0x40 && g.nmi_tracing) {  // RTI
        if (--g.nmi_trace_depth <= 0) {
          g.nmi_tracing = false;
          trace_close(&g.nmi_trace);
          g.nmi_trace_frame = -1;
        }
      }
      break;
    case FLOW_TRAP:
      if (opcode == 0x00) g.brk_count++;
      break;
    default:
      break;
  }
  g.cur_len = 0;
  g.cur_opcode = 0;
}

// One frame, mirroring snes_runFrame()'s vblank-boundary logic but stepping
// through our instrumented path.
static void run_frame(Snes* snes) {
  while (snes->inVblank) step(snes);
  uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) step(snes);
  // snes_runFrame() ends by catching the APU up; snes_catchupApu() is private,
  // but any APU-port read does the same thing, and reading $2140 has no other
  // observable effect.
  snes_readBBus(snes, 0x40);
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return NULL; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0) { fclose(f); return NULL; }
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
    fclose(f); free(buf); return NULL;
  }
  fclose(f);
  *out_len = (int)len;
  return buf;
}

static FILE* open_out(const char* dir, const char* name) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE* f = fopen(path, "w");
  if (!f) fprintf(stderr, "error: cannot write '%s'\n", path);
  return f;
}

static void format_pcs(char* out, int out_size, const uint32_t* pcs, uint8_t n) {
  int w = 0;
  out[0] = '\0';
  for (uint8_t i = 0; i < n; i++)
    w += snprintf(out + w, (size_t)(out_size - w), "%s$%02X:%04X", i ? " " : "",
                  pcs[i] >> 16, pcs[i] & 0xffff);
}

static void report_cdl(const char* dir) {
  FILE* f = open_out(dir, "cdl_summary.txt");
  if (!f) return;
  uint32_t code = 0, data = 0, both = 0, unknown = 0, conflict = 0, subs = 0;
  for (uint32_t i = 0; i < g.cdl.size; i++) {
    uint8_t v = g.cdl.flags[i];
    bool c = (v & (CDL_CODE | CDL_OPERAND)) != 0;
    bool d = (v & CDL_DATA) != 0;
    if (c && d) both++;
    else if (c) code++;
    else if (d) data++;
    else unknown++;
    if (v & CDL_CONFLICT) conflict++;
    if (v & CDL_SUB) subs++;
  }
  uint32_t covered = code + data + both;
  fprintf(f, "Code/Data Log coverage for %u ROM bytes\n", g.cdl.size);
  fprintf(f, "  code only     %8u  (%5.2f%%)\n", code, 100.0 * code / g.cdl.size);
  fprintf(f, "  data only     %8u  (%5.2f%%)\n", data, 100.0 * data / g.cdl.size);
  fprintf(f, "  code AND data %8u  (%5.2f%%)\n", both, 100.0 * both / g.cdl.size);
  fprintf(f, "  untouched     %8u  (%5.2f%%)\n", unknown, 100.0 * unknown / g.cdl.size);
  fprintf(f, "  covered       %8u  (%5.2f%%)\n", covered, 100.0 * covered / g.cdl.size);
  fprintf(f, "  subroutines   %8u\n", subs);
  fprintf(f, "  M/X conflicts %8u\n", conflict);

  fprintf(f, "\nPer 32K LoROM bank ($xx:8000-$xx:FFFF):\n");
  fprintf(f, "bank   code   data   both  untouched\n");
  for (uint32_t b = 0; b * 0x8000 < g.cdl.size; b++) {
    uint32_t c = 0, d = 0, bo = 0, u = 0;
    for (uint32_t i = 0; i < 0x8000; i++) {
      uint8_t v = g.cdl.flags[b * 0x8000 + i];
      bool cc = (v & (CDL_CODE | CDL_OPERAND)) != 0;
      bool dd = (v & CDL_DATA) != 0;
      if (cc && dd) bo++; else if (cc) c++; else if (dd) d++; else u++;
    }
    fprintf(f, "$%02X   %6u %6u %6u     %6u\n", b, c, d, bo, u);
  }
  fclose(f);
}

typedef struct { uint32_t addr, calls, execs; } SubRow;

static int cmp_sub(const void* a, const void* b) {
  const SubRow* x = a;
  const SubRow* y = b;
  if (x->calls != y->calls) return x->calls > y->calls ? -1 : 1;
  return x->addr < y->addr ? -1 : 1;
}

static void report_subs(const char* dir) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < g.cdl.size; i++)
    if (g.cdl.flags[i] & CDL_SUB) n++;
  SubRow* rows = (SubRow*)malloc(sizeof(SubRow) * (n ? n : 1));
  if (!rows) return;
  uint32_t k = 0;
  for (uint32_t i = 0; i < g.cdl.size && k < n; i++) {
    if (!(g.cdl.flags[i] & CDL_SUB)) continue;
    rows[k].addr = rom_to_snes(i);
    rows[k].calls = g.call_count[i];
    rows[k].execs = g.exec_count[i];
    k++;
  }
  qsort(rows, k, sizeof(SubRow), cmp_sub);

  FILE* f = open_out(dir, "subroutines.csv");
  if (f) {
    fprintf(f, "address,calls,executions,callers\n");
    for (uint32_t i = 0; i < k; i++) {
      // Gather callers of this subroutine from the edge table.
      char callers[256] = "";
      int w = 0, found = 0;
      for (uint32_t e = 0; e < EDGE_CAP && found < 6; e++) {
        if (!g.edges[e].used || g.edges[e].callee != rows[i].addr) continue;
        w += snprintf(callers + w, sizeof callers - (size_t)w, "%s$%02X:%04X",
                      found ? " " : "", g.edges[e].caller >> 16,
                      g.edges[e].caller & 0xffff);
        found++;
      }
      fprintf(f, "$%02X:%04X,%u,%u,%s\n", rows[i].addr >> 16, rows[i].addr & 0xffff,
              rows[i].calls, rows[i].execs, callers);
    }
    fclose(f);
  }
  free(rows);

  f = open_out(dir, "callgraph.csv");
  if (f) {
    fprintf(f, "caller,callee,count\n");
    for (uint32_t e = 0; e < EDGE_CAP; e++) {
      if (!g.edges[e].used) continue;
      fprintf(f, "$%02X:%04X,$%02X:%04X,%u\n", g.edges[e].caller >> 16,
              g.edges[e].caller & 0xffff, g.edges[e].callee >> 16,
              g.edges[e].callee & 0xffff, g.edges[e].count);
    }
    fclose(f);
  }
}

static void report_mem(const char* dir, const char* name, const MemStat* stats,
                       uint32_t count, uint32_t base, bool wram) {
  FILE* f = open_out(dir, name);
  if (!f) return;
  fprintf(f, "address,reads,writes,bulk,first_read_frame,first_write_frame,width,writers,readers\n");
  for (uint32_t i = 0; i < count; i++) {
    const MemStat* s = &stats[i];
    if (s->reads == 0 && s->writes == 0) continue;
    char writers[128], readers[128];
    format_pcs(writers, sizeof writers, s->writer_pc, s->n_writers);
    format_pcs(readers, sizeof readers, s->reader_pc, s->n_readers);
    const char* width = s->word_low ? "word_lo" : (s->word_high ? "word_hi" : "byte");
    if (wram) {
      // $7E:0000-$7E:1FFF is also visible as $00-$3F:0000-$1FFF; report the
      // canonical bank $7E/$7F form.
      fprintf(f, "$%02X:%04X,%u,%u,%u,%d,%d,%s,%s,%s\n", 0x7e + (i >> 16), i & 0xffff,
              s->reads, s->writes, s->bulk, s->first_read_frame, s->first_write_frame,
              width, writers, readers);
    } else {
      fprintf(f, "$%04X,%u,%u,%u,%d,%d,%s,%s,%s\n", base + i, s->reads, s->writes,
              s->bulk, s->first_read_frame, s->first_write_frame, width, writers, readers);
    }
  }
  fclose(f);
}

// Collapse the per-byte WRAM stats into contiguous live regions. This is the
// readable form of the memory map: it shows where the buffers are (OAM shadow,
// palette shadow, actor tables) instead of 100k individual rows.
static void report_wram_regions(const char* dir) {
  FILE* f = open_out(dir, "wram_regions.txt");
  if (!f) return;
  const uint32_t kGap = 8;  // bytes of untouched slack that still count as one region

  uint32_t live = 0;
  for (uint32_t i = 0; i < 0x20000; i++)
    if (g.wram[i].reads || g.wram[i].writes) live++;
  fprintf(f, "WRAM regions touched during the run (%u of 131072 bytes live)\n", live);
  fprintf(f, "Regions are separated by more than %u untouched bytes.\n\n", kGap);

  uint32_t i = 0;
  while (i < 0x20000) {
    if (!g.wram[i].reads && !g.wram[i].writes) { i++; continue; }
    uint32_t start = i, end = i;
    uint32_t gap = 0;
    for (uint32_t j = i; j < 0x20000; j++) {
      if (g.wram[j].reads || g.wram[j].writes) { end = j; gap = 0; }
      else if (++gap > kGap) break;
    }

    uint64_t reads = 0, writes = 0;
    uint32_t words = 0;
    // Rank accessor PCs by how many distinct bytes of the region they touch.
    uint32_t pcs[64], hits[64];
    int n_pcs = 0;
    for (uint32_t j = start; j <= end; j++) {
      reads += g.wram[j].reads;
      writes += g.wram[j].writes;
      if (g.wram[j].word_low) words++;
      for (uint8_t k = 0; k < g.wram[j].n_writers; k++) {
        uint32_t pc = g.wram[j].writer_pc[k];
        int slot = -1;
        for (int s = 0; s < n_pcs; s++)
          if (pcs[s] == pc) { slot = s; break; }
        if (slot < 0 && n_pcs < 64) { slot = n_pcs++; pcs[slot] = pc; hits[slot] = 0; }
        if (slot >= 0) hits[slot]++;
      }
    }
    fprintf(f, "$%02X:%04X-$%02X:%04X  %6u bytes  R:%-10llu W:%-10llu  %u word slots\n",
            0x7e + (start >> 16), start & 0xffff, 0x7e + (end >> 16), end & 0xffff,
            end - start + 1, (unsigned long long)reads, (unsigned long long)writes, words);

    // Print the four broadest writers.
    fputs("    writers:", f);
    for (int rank = 0; rank < 4; rank++) {
      int best = -1;
      for (int s = 0; s < n_pcs; s++)
        if (hits[s] > 0 && (best < 0 || hits[s] > hits[best])) best = s;
      if (best < 0) break;
      fprintf(f, " $%02X:%04X(%u)", pcs[best] >> 16, pcs[best] & 0xffff, hits[best]);
      hits[best] = 0;
    }
    fputc('\n', f);
    i = end + 1;
  }
  fclose(f);
}

static void report_dma(const char* dir) {
  FILE* f = open_out(dir, "dma_log.csv");
  if (!f) return;
  fprintf(f, "first_frame,pc,channel,direction,dest_reg,mode,source,length,step,count\n");
  for (uint32_t i = 0; i < g.dma_count; i++) {
    DmaEvent* e = &g.dma[i];
    fprintf(f, "%d,$%02X:%04X,%u,%s,$21%02X,%u,$%02X:%04X,%u,%s,%u\n", e->first_frame,
            e->pc >> 16, e->pc & 0xffff, e->channel, e->from_b ? "B->A" : "A->B",
            e->b_adr, e->mode, e->src >> 16, e->src & 0xffff, e->length,
            e->fixed ? "fixed" : (e->decrement ? "dec" : "inc"), e->count);
  }
  fclose(f);
}

// Dump the final frame so a movie's end state can be checked at a glance —
// "did the Start-mashing actually reach gameplay?" is otherwise guesswork.
static void write_png(const char* path) {
  enum { FB_W = 512, FB_H = 480 };
  uint8_t* fb = (uint8_t*)malloc(FB_W * FB_H * 4);
  uint8_t* rgb = (uint8_t*)malloc(FB_W * FB_H * 3);
  if (!fb || !rgb) { free(fb); free(rgb); return; }
  snes_setPixelFormat(g.snes, pixelFormatXRGB);
  snes_setPixels(g.snes, fb);
  for (int i = 0; i < FB_W * FB_H; i++) {
    rgb[i * 3 + 0] = fb[i * 4 + 2];
    rgb[i * 3 + 1] = fb[i * 4 + 1];
    rgb[i * 3 + 2] = fb[i * 4 + 0];
  }
  if (!stbi_write_png(path, FB_W, FB_H, 3, rgb, FB_W * 3))
    fprintf(stderr, "warning: cannot write '%s'\n", path);
  free(fb);
  free(rgb);
}

static void report_summary(const char* dir, const char* rom_path, int frames,
                           const char* movie_path) {
  FILE* f = open_out(dir, "run_summary.txt");
  if (!f) return;
  fprintf(f, "zamn_trace run summary\n");
  fprintf(f, "  rom            %s (%u bytes)\n", rom_path, g.rom_size);
  fprintf(f, "  movie          %s\n", movie_path ? movie_path : "(none)");
  fprintf(f, "  frames         %d\n", frames);
  fprintf(f, "  instructions   %llu\n", (unsigned long long)g.instructions);
  fprintf(f, "  cpu cycles     %llu\n", (unsigned long long)g.snes->cycles);
  fprintf(f, "\nVectors (native mode):\n");
  fprintf(f, "  RESET          $00:%04X\n", g.reset_vector & 0xffff);
  fprintf(f, "  NMI            $00:%04X   (%llu taken)\n", g.nmi_vector & 0xffff,
          (unsigned long long)g.nmi_count);
  fprintf(f, "  IRQ            $00:%04X   (%llu taken)\n", g.irq_vector & 0xffff,
          (unsigned long long)g.irq_count);
  fprintf(f, "  BRK executed   %llu\n", (unsigned long long)g.brk_count);
  fprintf(f, "\nDistinct DMA transfers: %u\n", g.dma_count);
  fprintf(f, "Call-graph edges:       %u\n", g.edge_count);
  fclose(f);
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "usage: %s <rom.sfc> [-o dir] [-f frames] [-m movie]\n"
            "          [--trace-reset n] [--trace-nmi frame]\n",
            argv[0]);
    return 2;
  }
  const char* rom_path = argv[1];
  const char* out_dir = "analysis";
  const char* movie_path = NULL;
  const char* png_path = NULL;
  int frames = 600;
  int64_t trace_reset = 0;
  g.nmi_trace_frame = -1;

  for (int i = 2; i < argc; i++) {
    const char* a = argv[i];
    bool has_next = i + 1 < argc;
    if ((!strcmp(a, "-o") || !strcmp(a, "--out")) && has_next) out_dir = argv[++i];
    else if ((!strcmp(a, "-f") || !strcmp(a, "--frames")) && has_next) frames = atoi(argv[++i]);
    else if ((!strcmp(a, "-m") || !strcmp(a, "--movie")) && has_next) movie_path = argv[++i];
    else if (!strcmp(a, "--trace-reset") && has_next) trace_reset = atoll(argv[++i]);
    else if (!strcmp(a, "--trace-nmi") && has_next) g.nmi_trace_frame = atoi(argv[++i]);
    else if (!strcmp(a, "--png") && has_next) png_path = argv[++i];
    else if (!strcmp(a, "--stats-from") && has_next) g.stats_from = atoi(argv[++i]);
    else { fprintf(stderr, "error: unknown option '%s'\n", a); return 2; }
  }

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) return 1;

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    return 1;
  }
  g.snes = snes;
  g.rom = snes->cart->rom;
  g.rom_size = snes->cart->romSize;

  Movie movie;
  bool have_movie = false;
  if (movie_path) {
    if (!movie_load(&movie, movie_path)) {
      fprintf(stderr, "error: cannot load movie '%s'\n", movie_path);
      return 1;
    }
    have_movie = true;
  }

  if (!cdl_alloc(&g.cdl, g.rom_size)) return 1;
  g.wram = (MemStat*)calloc(0x20000, sizeof(MemStat));
  g.reg = (MemStat*)calloc(REG_END - REG_BASE, sizeof(MemStat));
  g.exec_count = (uint32_t*)calloc(g.rom_size, sizeof(uint32_t));
  g.call_count = (uint32_t*)calloc(g.rom_size, sizeof(uint32_t));
  g.edges = (CallEdge*)calloc(EDGE_CAP, sizeof(CallEdge));
  if (!g.wram || !g.reg || !g.exec_count || !g.call_count || !g.edges) {
    fprintf(stderr, "error: out of memory\n");
    return 1;
  }
  for (uint32_t i = 0; i < 0x20000; i++) {
    g.wram[i].first_read_frame = g.wram[i].first_write_frame = -1;
  }
  for (uint32_t i = 0; i < REG_END - REG_BASE; i++) {
    g.reg[i].first_read_frame = g.reg[i].first_write_frame = -1;
  }

  // Vectors live at the top of bank $00.
  g.reset_vector = peek(0x00fffc) | (peek(0x00fffd) << 8);
  g.nmi_vector = peek(0x00ffea) | (peek(0x00ffeb) << 8);
  g.irq_vector = peek(0x00ffee) | (peek(0x00ffef) << 8);

  snes_reset(snes, true);
  snes->cpu->read = trace_read;
  snes->cpu->write = trace_write;

  if (trace_reset > 0) {
    char path[512];
    snprintf(path, sizeof path, "%s/reset_trace.txt", out_dir);
    g.reset_trace.f = fopen(path, "w");
    if (!g.reset_trace.f) {
      fprintf(stderr, "error: cannot write '%s' (does the directory exist?)\n", path);
      return 1;
    }
    g.trace_remaining = trace_reset;
    fprintf(g.reset_trace.f, "; first %lld instructions from RESET ($00:%04X)\n",
            (long long)trace_reset, g.reset_vector);
  }
  if (g.nmi_trace_frame >= 0) {
    char path[512];
    snprintf(path, sizeof path, "%s/nmi_trace.txt", out_dir);
    g.nmi_trace.f = fopen(path, "w");
    if (!g.nmi_trace.f) {
      fprintf(stderr, "error: cannot write '%s' (does the directory exist?)\n", path);
      return 1;
    }
    fprintf(g.nmi_trace.f, "; NMI handler ($00:%04X) at/after frame %d\n", g.nmi_vector,
            g.nmi_trace_frame);
  }

  printf("Tracing %d frames of '%s'...\n", frames, rom_path);
  for (g.frame = 0; g.frame < frames; g.frame++) {
    if (have_movie) {
      movie_apply(&movie, snes, g.frame);
    }
    run_frame(snes);
    if ((g.frame % 200) == 0) {
      printf("  frame %5d  %10llu instructions\n", g.frame,
             (unsigned long long)g.instructions);
      fflush(stdout);
    }
  }
  g.frame = frames;

  trace_close(&g.reset_trace);
  trace_close(&g.nmi_trace);

  char cdl_path[512];
  snprintf(cdl_path, sizeof cdl_path, "%s/zamn.cdl", out_dir);
  if (!cdl_save(&g.cdl, cdl_path)) {
    fprintf(stderr, "error: cannot write '%s' (does the directory exist?)\n", cdl_path);
    return 1;
  }
  report_cdl(out_dir);
  report_subs(out_dir);
  report_mem(out_dir, "wram_map.csv", g.wram, 0x20000, 0, true);
  report_wram_regions(out_dir);
  report_mem(out_dir, "registers.csv", g.reg, REG_END - REG_BASE, REG_BASE, false);
  report_dma(out_dir);
  report_summary(out_dir, rom_path, frames, movie_path);
  if (png_path) write_png(png_path);

  printf("Done: %llu instructions, %llu NMIs. Reports in '%s/'.\n",
         (unsigned long long)g.instructions, (unsigned long long)g.nmi_count, out_dir);

  if (have_movie) movie_free(&movie);
  cdl_free(&g.cdl);
  free(g.wram);
  free(g.reg);
  free(g.exec_count);
  free(g.call_count);
  free(g.edges);
  snes_free(snes);
  free(rom);
  return 0;
}
