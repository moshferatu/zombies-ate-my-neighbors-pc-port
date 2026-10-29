// The port's sound (`src/sound`: the boot loader and the game's driver in C,
// on the port's DSP) beside the stock console's SPC700 running the original,
// on each movie. The console plays the game; every write the game makes to
// the sound ports goes to both, at the console's cycle. Checked:
//
//   * every read the game makes of the ports: the same byte, at the same
//     cycle. This is the timing the game sees;
//   * the program's path: each routine the C marks, entered on the same
//     cycle as the SPC700 enters it;
//   * every DSP write: register, value and cycle;
//   * every port write the driver makes, in order;
//   * the driver's RAM at every tick, and all of the sound RAM every 64;
//   * every sample, and each frame's audio as the frontend asks for it.
//
// With `--player` the program on the port's chip is the player the game runs
// (`src/sound/player.h`) and not `driver.c`. It keeps the driver's variables
// in structs, so the RAM checked is all of it every 64 ticks but for $0000 to
// $05FF, the songs' table there excepted; and the marks are those it makes:
// the loader's, the start, the poll, the tick and the pickup. All the rest is
// checked as for the driver.
//
// `--break N` makes the port's program one cycle late at its Nth poll, to
// show the check sees it. It must then fail.
//
// `--spc-cycles` prints LakeSnes's cycles for each SPC700 opcode, branch
// taken and not, for checking the costs `src/sound/driver.c` was written with.
//
//   zamn_test_sound [--player] [--break N] <rom.sfc> <movie.zmv>... [-f frames]

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/movie_apply.h"
#include "snes.h"
#include "sound/chip.h"
#include "sound/player.h"
#include "sound/sound.h"

#define SAMPLES 534

// The addresses `src/sound` marks with `chip_at`.
static const uint16_t marked[] = {
    0xFFC0, 0xFFEF, 0x0600, 0x0AFF, 0x06B5, 0x0D03, 0x0705, 0x0941, 0x098C, 0x0ACD,
    0x075C, 0x0938, 0x07A9, 0x0B8A, 0x0BE1, 0x0C5C, 0x0CBA,
    // ...and where a command is picked up, an effect gets a track or none, and
    // a note a voice or none, with a register each. `src/sound/player.c`
    // follows the first, and `zamn_test_player` counts the rest.
    0x0B1D, 0x0B9B, 0x0B9D, 0x0D70, 0x0D84, 0x0D87, 0x0D97,
};

// ...and those the player marks.
static const uint16_t marked_by_player[] = {0xFFC0, 0xFFEF, 0x0600, 0x0AFF, 0x06B5, 0x0B1D};

// --player: the program is the player.
static Player* lead;

// The register each of those carries.
static uint8_t marked_reg(const Spc* spc, uint16_t adr) {
  switch (adr) {
    case 0x0B1D: case 0x0B9D: case 0x0D87: return spc->x;
    case 0x0D97: return spc->y;
    default: return 0;
  }
}
static bool is_marked[0x10000];

// One side's events, waiting for the other's.
typedef struct {
  SoundSeen what;
  uint16_t a;
  uint8_t v;
  uint64_t cycle;
  uint8_t* ram;  // at the tick's entry, or NULL
  bool full;     // ...all of it, not just the driver's
} Event;

#define QUEUE 65536
typedef struct {
  Event e[QUEUE];
  int head, count;
} Queue;

typedef struct {
  Snes* snes;
  Apu* apu;
  Sound sound;
  Queue lake, port;
  uint64_t lake_hi;     // the console's cycle count above 32 bits
  uint32_t lake_last;
  long frame;
  bool failed;
  char why[512];
  // Counts.
  long marks, dsp_writes, port_writes, port_reads, rams, fulls, ticks_lake, ticks_port;
  uint64_t samples_checked;
  long break_at, polls;  // --break
  uint16_t insn_at, insn_end;
  // The last few marks matched, for a report.
  Event recent[8];
  int recent_n;
} Check;

static Check g;

// The console's samples, kept as they are made. LakeSnes keeps only its last
// 1,024, and a call of `snes_runFrame` can make more than that.
static int16_t lake_ring[16384 * 2];
static long full_every = 64;  // --full N: all of the sound RAM every N ticks

static uint8_t (*lake_read)(void*, uint16_t);
static void (*lake_write)(void*, uint16_t, uint8_t);
static void (*lake_idle)(void*, bool);

static void fail(const char* fmt, ...) {
  if (g.failed) return;
  g.failed = true;
  char what[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(what, sizeof what, fmt, ap);
  va_end(ap);
  snprintf(g.why, sizeof g.why, "frame %ld: %s", g.frame, what);
}

// The console's cycle now, 64 bits.
static uint64_t lake_now(void) {
  const uint32_t c = g.apu->cycles;
  if (c < g.lake_last) g.lake_hi += 1ull << 32;
  g.lake_last = c;
  return g.lake_hi | c;
}

static const char* seen_name(SoundSeen w) {
  return w == SOUND_SEEN_AT ? "at" : w == SOUND_SEEN_DSP ? "DSP" : "port";
}

// The driver's own RAM: the direct page below the registers, and pages 2 to
// $11 (its variables, its code and its tables). Page 1 is the stack, which
// the C does not keep.
#define DRIVER_RAM (0xF0 + 0x1000)
static void driver_ram(const uint8_t* ram, uint8_t* out) {
  memcpy(out, ram, 0xF0);
  memcpy(out + 0xF0, ram + 0x200, 0x1000);
}

// Where the player keeps nothing the driver keeps: its variables.
static bool player_skips(int i) {
  return i < 0x600 && !(i >= 0x540 && i < 0x5A6);
}

static void compare_ram(const Event* a, const Event* b) {
  const uint8_t* lake = a->ram;
  const uint8_t* port = b->ram;
  if (a->full && b->full) {
    g.fulls++;
    for (int i = 0; i < 0x10000; i++) {
      if ((i >= 0xF0 && i < 0x200)) continue;  // the registers, the stack
      if (lead && player_skips(i)) continue;
      if (lake[i] != port[i]) {
        fail("sound RAM $%04X at the tick on cycle %llu: console $%02X, port $%02X", i,
             (unsigned long long)a->cycle, lake[i], port[i]);
        return;
      }
    }
    return;
  }
  g.rams++;
  for (int i = 0; i < DRIVER_RAM; i++) {
    if (lake[i] == port[i]) continue;
    const int adr = i < 0xF0 ? i : i - 0xF0 + 0x200;
    fail("driver RAM $%04X at the tick on cycle %llu: console $%02X, port $%02X", adr,
         (unsigned long long)a->cycle, lake[i], port[i]);
    return;
  }
}

static void report_recent(void) {
  char buf[512] = "";
  for (int i = 0; i < g.recent_n; i++) {
    char one[48];
    snprintf(one, sizeof one, " $%04X@%llu", g.recent[i].a, (unsigned long long)g.recent[i].cycle);
    strncat(buf, one, sizeof buf - strlen(buf) - 1);
  }
  printf("  last marks matched:%s\n", buf);
}

// An event from one side: matched against the other side's oldest, or kept.
static void seen(Queue* mine, Queue* theirs, bool from_lake, const Event* e) {
  if (g.failed) return;
  if (theirs->count) {
    Event* t = &theirs->e[theirs->head];
    const Event* lake = from_lake ? e : t;
    const Event* port = from_lake ? t : e;
    if (lake->what != port->what || lake->a != port->a || lake->v != port->v ||
        (lake->what != SOUND_SEEN_PORT && lake->cycle != port->cycle)) {
      fail("console %s $%04X=$%02X on %llu, port %s $%04X=$%02X on %llu", seen_name(lake->what),
           lake->a, lake->v, (unsigned long long)lake->cycle, seen_name(port->what), port->a,
           port->v, (unsigned long long)port->cycle);
      report_recent();
    } else {
      if (lake->what == SOUND_SEEN_AT) {
        g.marks++;
        if (g.recent_n < 8) g.recent[g.recent_n++] = *lake;
        else { memmove(g.recent, g.recent + 1, 7 * sizeof *g.recent); g.recent[7] = *lake; }
      } else if (lake->what == SOUND_SEEN_DSP) {
        g.dsp_writes++;
      } else {
        g.port_writes++;
      }
      if (lake->ram && port->ram) compare_ram(lake, port);
    }
    free(t->ram);
    theirs->head = (theirs->head + 1) % QUEUE;
    theirs->count--;
    free(e->ram);
    return;
  }
  if (mine->count == QUEUE) {
    fail("one side is %d events ahead of the other", QUEUE);
    return;
  }
  mine->e[(mine->head + mine->count) % QUEUE] = *e;
  mine->count++;
}

static uint8_t* take_ram(const uint8_t* ram, bool full) {
  uint8_t* r = (uint8_t*)malloc(full ? 0x10000 : DRIVER_RAM);
  if (full) memcpy(r, ram, 0x10000);
  else driver_ram(ram, r);
  return r;
}

// --- The console's side ------------------------------------------------------

static const uint8_t spc_size[256] = {
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 3, 1, 2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 3, 3,
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 3, 2, 2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 2, 3,
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 3, 2, 2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 3, 3,
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 3, 1, 2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 2, 1,
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 2, 1, 3, 2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 1, 1,
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 2, 1, 1, 2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 1, 1,
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 2, 1, 1, 2, 1, 2, 3, 2, 3, 3, 2, 2, 2, 2, 2, 1, 1, 3, 1,
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 1, 1, 2, 1, 2, 3, 2, 3, 3, 2, 2, 2, 3, 2, 1, 1, 2, 1,
};

// The SPC700 fetches an instruction's bytes at its program counter. One not
// among the last instruction's operands starts a new one.
static void lake_fetch(uint16_t adr, uint8_t byte, uint64_t cycle) {
  if (adr > g.insn_at && adr < g.insn_end) return;
  g.insn_at = adr;
  g.insn_end = (uint16_t)(adr + spc_size[byte]);
  if (!is_marked[adr]) return;
  Event e = {SOUND_SEEN_AT, adr, marked_reg(g.apu->spc, adr), cycle, NULL, false};
  if (adr == 0x06B5) {
    g.ticks_lake++;
    e.full = g.ticks_lake % full_every == 0;
    if (e.full || !lead) e.ram = take_ram(g.apu->ram, e.full);
  }
  seen(&g.lake, &g.port, true, &e);
}

// After an access on cycle `c`: the sample LakeSnes made on it, if it did.
static void lake_sample(uint64_t c) {
  if (c & 31) return;
  const Dsp* d = g.apu->dsp;
  const int at = (d->sampleOffset - 1) & 0x3FF;
  const uint64_t k = c / 32;
  lake_ring[(k & 16383) * 2] = d->sampleBuffer[at * 2];
  lake_ring[(k & 16383) * 2 + 1] = d->sampleBuffer[at * 2 + 1];
}

static uint8_t hook_read(void* mem, uint16_t adr) {
  const uint64_t c = lake_now();
  const uint8_t v = lake_read(mem, adr);
  lake_sample(c);
  if (adr == (uint16_t)(g.apu->spc->pc - 1)) lake_fetch(adr, v, c);
  return v;
}

static void hook_write(void* mem, uint16_t adr, uint8_t val) {
  const uint64_t c = lake_now();
  const uint8_t dsp_adr = g.apu->dspAdr;
  lake_write(mem, adr, val);
  lake_sample(c);
  if (adr == 0xF3 && dsp_adr < 0x80) {
    Event e = {SOUND_SEEN_DSP, dsp_adr, val, c, NULL, false};
    seen(&g.lake, &g.port, true, &e);
  } else if (adr >= 0xF4 && adr <= 0xF7) {
    Event e = {SOUND_SEEN_PORT, (uint16_t)(adr - 0xF4), val, c, NULL, false};
    seen(&g.lake, &g.port, true, &e);
  }
}

static void hook_idle(void* mem, bool waiting) {
  const uint64_t c = lake_now();
  lake_idle(mem, waiting);
  lake_sample(c);
}

// --- The port's side -----------------------------------------------------------

// --around C: the port's events from 200,000 cycles before cycle C to 3,000
// after, as they come, but for the poll and the ticks.
static uint64_t around;

static void port_watch(void* ctx, SoundSeen what, uint16_t a, uint8_t v, uint64_t cycle) {
  Sound* s = (Sound*)ctx;
  if (around && cycle + 200000 > around && cycle < around + 3000 &&
      !(what == SOUND_SEEN_AT && (a == 0x0AFF || a == 0x06B5 || a == 0x0D03)))
    printf("  port %s $%04X=$%02X on %llu\n", seen_name(what), a, v, (unsigned long long)cycle);
  Event e = {what, a, v, cycle, NULL, false};
  if (what == SOUND_SEEN_AT && a == 0x06B5) {
    g.ticks_port++;
    e.full = g.ticks_port % full_every == 0;
    // The echo writes into the RAM, and the port's DSP keeps step only with
    // what the driver does. The console's has made its tick on this cycle by
    // the time the instruction is fetched, so this one makes it too.
    if (lead) {
      if (e.full) {
        player_sync(lead, cycle);
        e.ram = take_ram(lead->unit[0].ram, true);
      }
    } else {
      if (e.full) chip_sync(s, cycle);
      e.ram = take_ram(s->ram, e.full);
    }
  }
  if (what == SOUND_SEEN_AT && a == 0x0AFF && ++g.polls == g.break_at) {
    s->now++;
    printf("  --break: the port's poll %ld made a cycle late\n", g.polls);
  }
  seen(&g.port, &g.lake, false, &e);
}

// --- The game's side -----------------------------------------------------------

static bool is_port(uint32_t adr) {
  const uint32_t a = adr & 0xFFFF;
  return (adr >> 16 & 0x7F) < 0x40 && a >= 0x2140 && a < 0x2180;
}

static void cpu_wrote(Snes* snes, uint32_t adr, uint8_t val, uint64_t clock, void* ctx) {
  (void)snes; (void)clock; (void)ctx;
  if (!is_port(adr) || g.failed) return;
  sound_run(&g.sound, lake_now());
  sound_write_port(&g.sound, (int)(adr & 3), val);
}

static void cpu_read(Snes* snes, uint32_t adr, uint8_t val, uint64_t clock, void* ctx) {
  (void)snes; (void)clock; (void)ctx;
  if (!is_port(adr) || g.failed) return;
  const uint64_t t = lake_now();
  sound_run(&g.sound, t);
  g.port_reads++;
  const uint8_t mine = sound_read_port(&g.sound, (int)(adr & 3));
  if (mine != val)
    fail("the game read $%02X from port %d on cycle %llu; the port's sound gave $%02X", val,
         (int)(adr & 3), (unsigned long long)t, mine);
}

// --- Running ---------------------------------------------------------------------

static uint8_t* read_file(const char* path, int* len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *len = (int)ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* b = (uint8_t*)malloc((size_t)*len);
  if (b && fread(b, 1, (size_t)*len, f) != (size_t)*len) { free(b); b = NULL; }
  fclose(f);
  return b;
}

static long movie_end(const Movie* m) {
  long last = 0;
  for (int p = 0; p < MOVIE_PORTS; p++)
    if (m->track[p].count && m->track[p].events[m->track[p].count - 1].frame > last)
      last = m->track[p].events[m->track[p].count - 1].frame;
  return last + 600;
}

static void drain(Queue* q) {
  while (q->count) {
    free(q->e[q->head].ram);
    q->head = (q->head + 1) % QUEUE;
    q->count--;
  }
}

// The port's DSP, its RAM and its samples: the chip's, or the player's.
static const SoundDsp* port_dsp(void) { return lead ? &lead->unit[0].dsp : &g.sound.dsp; }
static const uint8_t* port_ram(void) { return lead ? lead->unit[0].ram : g.sound.ram; }
static const int16_t* port_ring(void) { return lead && g.sound.dsp_idle ? lead->ring : g.sound.ring; }

// When the samples part: where the two DSPs, and the RAM they read, differ.
static void report_dsp(void) {
  const Dsp* d = g.apu->dsp;
  const SoundDsp* p = port_dsp();
  const uint8_t* ram = port_ram();
  for (int v = 0; v < 8; v++) {
    const DspVoice* x = &p->voice[v];
    const uint16_t dir = (uint16_t)(p->dir + 4 * x->srcn);
    printf("  voice %d: srcn $%02X (start $%04X loop $%04X) block $%04X level $%03X phase %d\n", v,
           x->srcn, ram[dir] | ram[dir + 1] << 8, ram[(uint16_t)(dir + 2)] | ram[(uint16_t)(dir + 3)] << 8,
           x->block, x->level, (int)x->phase);
  }
  for (int v = 0; v < 8; v++) {
    const DspChannel* c = &d->channel[v];
    const DspVoice* x = &p->voice[v];
    if (c->decodeOffset != x->block || c->pitchCounter != x->pitch_counter ||
        c->gain != x->level || c->srcn != x->srcn || c->sampleOut != x->out)
      printf("  voice %d: console block $%04X counter $%04X level $%03X srcn $%02X out %d; "
             "port block $%04X counter $%04X level $%03X srcn $%02X out %d\n",
             v, c->decodeOffset, c->pitchCounter, c->gain, c->srcn, c->sampleOut, x->block,
             x->pitch_counter, x->level, x->srcn, x->out);
  }
  printf("  console: echo at $%04X+%d length %d fir_at %d counter %d mvol %d,%d evol %d,%d "
         "efb %d; ticks %u\n",
         d->echoBufferAdr, d->echoBufferIndex, d->echoLength, d->firBufferIndex, d->counter,
         d->masterVolumeL, d->masterVolumeR, d->echoVolumeL, d->echoVolumeR, d->feedbackVolume,
         d->sampleOffset);
  printf("  port:    echo at $%04X+%d length %d fir_at %d counter %d mvol %d,%d evol %d,%d "
         "efb %d; ticks %u\n",
         p->echo_start, p->echo_at, p->echo_length, p->fir_at, p->counter, p->main_vol_l,
         p->main_vol_r, p->echo_vol_l, p->echo_vol_r, p->feedback,
         (unsigned)(uint16_t)(lead ? lead->ticks : g.sound.ticks));
  for (int i = 0; i < 8; i++)
    if (d->firBufferL[i] != p->fir_l[i] || d->firBufferR[i] != p->fir_r[i])
      printf("  fir %d: console %d,%d port %d,%d\n", i, d->firBufferL[i], d->firBufferR[i],
             p->fir_l[i], p->fir_r[i]);
  int shown = 0;
  for (int i = 0; i < 0x10000 && shown < 12; i++) {
    if (i >= 0xF0 && i < 0x200) continue;
    if (lead && player_skips(i)) continue;
    if (g.apu->ram[i] != ram[i]) {
      printf("  RAM $%04X: console $%02X, port $%02X\n", i, g.apu->ram[i], ram[i]);
      shown++;
    }
  }
}

static bool run_movie(const uint8_t* rom, int rom_len, const char* path, long frames, long break_at) {
  Movie movie;
  if (!movie_load(&movie, path)) {
    printf("FAIL %s: cannot load\n", path);
    return false;
  }
  const long n = frames > 0 ? frames : movie_end(&movie);
  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) {
    printf("FAIL: the core will not load the cartridge\n");
    return false;
  }
  drain(&g.lake);
  drain(&g.port);
  memset(&g.marks, 0, (size_t)((char*)&g.recent_n - (char*)&g.marks) + sizeof g.recent_n);
  g.snes = snes;
  g.apu = snes->apu;
  g.failed = false;
  g.lake_hi = 0;
  g.lake_last = 0;
  g.break_at = break_at;
  g.insn_at = 0;
  g.insn_end = 0;
  if (lead) player_lead(lead, &g.sound);
  else sound_use_driver(&g.sound);
  sound_reset(&g.sound);
  memset(lake_ring, 0, sizeof lake_ring);
  g.sound.watch = port_watch;
  g.sound.watch_ctx = &g.sound;
  Spc* spc = g.apu->spc;
  lake_read = spc->read;
  lake_write = spc->write;
  lake_idle = spc->idle;
  spc->read = hook_read;
  spc->write = hook_write;
  spc->idle = hook_idle;
  snes_setWriteHook(snes, cpu_wrote, NULL);
  snes_setReadHook(snes, cpu_read, NULL);

  int16_t lake_audio[SAMPLES * 2], port_audio[SAMPLES * 2];
  for (g.frame = 0; g.frame < n && !g.failed; g.frame++) {
    movie_apply(&movie, snes, (int)g.frame);
    snes_runFrame(snes);
    const uint64_t t = lake_now();
    sound_run(&g.sound, t);
    if (g.sound.state == SOUND_HALTED) fail("the port's sound halted: %s", g.sound.fault);
    // Every sample made by now.
    const uint64_t made = sound_ticks_before(t);
    long parted = 0;
    uint64_t last_parted = 0;
    for (uint64_t k = g.samples_checked; k < made; k++) {
      const int16_t* lake = &lake_ring[(k & 16383) * 2];
      const int16_t* port = &port_ring()[(k & 16383) * 2];
      if (lake[0] == port[0] && lake[1] == port[1]) continue;
      if (!g.failed)
        fail("sample %llu (cycle %llu): console %d,%d, port %d,%d", (unsigned long long)k,
             (unsigned long long)(k * 32), lake[0], lake[1], port[0], port[1]);
      parted++;
      last_parted = k;
    }

    if (parted)
      printf("  %ld samples of the frame part, the last %llu\n", parted,
             (unsigned long long)last_parted);
    g.samples_checked = made;
    if (g.failed) report_dsp();
    snes_setSamples(snes, lake_audio, SAMPLES);
    sound_get_samples(&g.sound, port_audio, SAMPLES);
    if (!g.failed && memcmp(lake_audio, port_audio, sizeof lake_audio))
      fail("the frame's audio differs");
  }
  printf("%s %s: %ld frames; %ld marks, %ld DSP writes, %ld port writes, %ld port reads, "
         "%ld ticks' RAM, %ld full, %llu samples\n",
         g.failed ? "FAIL" : "ok", path, g.frame, g.marks, g.dsp_writes, g.port_writes,
         g.port_reads, g.rams, g.fulls, (unsigned long long)g.samples_checked);
  if (g.failed) printf("  %s\n", g.why);
  snes_free(snes);
  movie_free(&movie);
  return !g.failed;
}

// --- --spc-cycles ----------------------------------------------------------------

static int cyc_count;
static uint8_t cyc_mem[0x10000];
static uint8_t cyc_r(void* m, uint16_t a) { (void)m; cyc_count++; return cyc_mem[a]; }
static void cyc_w(void* m, uint16_t a, uint8_t v) { (void)m; (void)a; (void)v; cyc_count++; }
static void cyc_i(void* m, bool w) { (void)m; (void)w; cyc_count++; }

static void spc_cycles(void) {
  for (int op = 0; op < 256; op++) {
    int n[2];
    for (int flags = 0; flags < 2; flags++) {
      Spc* spc = spc_init(NULL, cyc_r, cyc_w, cyc_i);
      spc_reset(spc, true);
      memset(cyc_mem, flags ? 0xFF : 0x00, sizeof cyc_mem);
      cyc_mem[0xFFFE] = 0x00;
      cyc_mem[0xFFFF] = 0x10;
      spc_runOpcode(spc);  // the reset
      cyc_mem[0x1000] = (uint8_t)op;
      // Operands that make a compare-and-branch go each way, and a counter
      // that a DBNZ takes to zero or not.
      cyc_mem[0x1001] = 0x02;
      cyc_mem[0x1002] = 0x02;
      spc->x = spc->y = spc->a = flags ? 0x02 : 0x01;
      spc->c = spc->z = spc->n = spc->v = flags;
      cyc_count = 0;
      spc_runOpcode(spc);
      n[flags] = cyc_count;
      spc_free(spc);
    }
    printf("%02X %d %d\n", op, n[0], n[1]);
  }
}

int main(int argc, char** argv) {
  long frames = 0, break_at = 0;
  const char* rom_path = NULL;
  const char* movies[256];
  int count = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--spc-cycles")) { spc_cycles(); return 0; }
    else if (!strcmp(argv[i], "--break") && i + 1 < argc) break_at = atol(argv[++i]);
    else if (!strcmp(argv[i], "--player")) lead = (Player*)malloc(sizeof *lead);
    else if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atol(argv[++i]);
    else if (!strcmp(argv[i], "--full") && i + 1 < argc) full_every = atol(argv[++i]);
    else if (!strcmp(argv[i], "--around") && i + 1 < argc) around = strtoull(argv[++i], NULL, 10);
    else if (!rom_path) rom_path = argv[i];
    else if (count < 256) movies[count++] = argv[i];
  }
  if (!rom_path || !count) {
    fprintf(stderr, "usage: zamn_test_sound [--player] [--break N] <rom.sfc> <movie.zmv>... [-f frames]\n"
                    "       zamn_test_sound --spc-cycles\n");
    return 2;
  }
  if (lead)
    for (size_t i = 0; i < sizeof marked_by_player / sizeof *marked_by_player; i++)
      is_marked[marked_by_player[i]] = true;
  else
    for (size_t i = 0; i < sizeof marked / sizeof *marked; i++) is_marked[marked[i]] = true;
  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) { printf("SKIP: no cartridge at '%s'\n", rom_path); return 0; }
  int bad = 0;
  for (int i = 0; i < count; i++)
    if (!run_movie(rom, rom_len, movies[i], frames, break_at)) bad++;
  free(rom);
  if (break_at) {
    printf(bad == count ? "--break: caught in every movie\n" : "--break: NOT CAUGHT\n");
    return bad == count ? 0 : 1;
  }
  printf(bad ? "%d of %d movies FAILED\n" : "all %d movies match\n", bad ? bad : count, count);
  return bad ? 1 : 0;
}
