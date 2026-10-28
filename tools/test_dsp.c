// The port's DSP (`src/sound/dsp.c`) beside LakeSnes's, on the game's own
// sound: each movie is played on the stock console, and every register write
// the SPC700 makes is made to both. On every tick of the DSP's clock:
//
//   * the stereo sample must be the same;
//   * so must all 128 registers, which is what the driver reads back
//     (ENVX, OUTX, ENDX among them).
//
// And once a frame, the 64 KB of sound RAM, which the echo writes into, and
// the frame's audio as the frontend asks for it.
//
// The port's DSP has its own copy of the sound RAM, kept by mirroring every
// write the SPC700 makes, so the echo's writes are checked rather than shared.
//
// `--break` changes one register write on the port's side, once, to show the
// check sees it. It must then fail.
//
// Also counts what the movies ask of the chip: whether any voice ever plays
// noise or is pitch-modulated, which docs/PLAN.md lists as not established.
//
// `--spc-trace file` writes, for every SPC700 instruction that ran, its
// address and how many times, for `tools/dis700.py --trace`: the sound
// driver jumps through addresses it computes, and this is how the code they
// reach is found.
//
//   zamn_test_dsp [--break] [--spc-trace file] <rom.sfc> <movie.zmv>... [-f frames]

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/movie_apply.h"
#include "snes.h"
#include "sound/dsp.h"

#define SAMPLES 534

typedef struct {
  Apu* apu;
  SoundDsp dsp;
  uint8_t ram[0x10000];
  long ticks;
  long frame;
  bool failed;
  char why[256];
  bool breaking;  // --break, not yet done
  // What the movie asked for.
  long key_ons, noise_ons, pmod_ons, echo_ticks;
} Shadow;

static Shadow g;
static int trace_reg = -1;  // --trace $xx: every write to one register

// --spc-trace. An instruction's bytes are all fetched at the SPC700's program
// counter, so a fetch is a read of `pc - 1`. It starts an instruction unless
// it is one of the operand bytes after the last start.
static uint32_t* spc_runs;
static uint16_t insn_at, insn_end;
static const uint8_t spc_size[256] = {
    // x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 3, 1,  // 0x
    2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 3, 3,  // 1x
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 3, 2,  // 2x
    2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 2, 3,  // 3x
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 3, 2,  // 4x
    2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 3, 3,  // 5x
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 3, 1,  // 6x
    2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 2, 1,  // 7x
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 2, 1, 3,  // 8x
    2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 1, 1,  // 9x
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 2, 1, 1,  // Ax
    2, 1, 2, 3, 2, 3, 3, 2, 3, 1, 2, 2, 1, 1, 1, 1,  // Bx
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 2, 1, 1,  // Cx
    2, 1, 2, 3, 2, 3, 3, 2, 2, 2, 2, 2, 1, 1, 3, 1,  // Dx
    1, 1, 2, 3, 2, 3, 1, 2, 2, 3, 3, 2, 3, 1, 1, 1,  // Ex
    2, 1, 2, 3, 2, 3, 3, 2, 2, 2, 3, 2, 1, 1, 2, 1,  // Fx
};
static void spc_fetch(uint16_t adr, uint8_t byte) {
  if (adr > insn_at && adr < insn_end) return;  // an operand
  insn_at = adr;
  insn_end = (uint16_t)(adr + spc_size[byte]);
  spc_runs[adr]++;
}

static uint8_t (*lake_read)(void*, uint16_t);
static void (*lake_write)(void*, uint16_t, uint8_t);
static void (*lake_idle)(void*, bool);

static void fail(const char* fmt, ...) {
  if (g.failed) return;
  g.failed = true;
  char what[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(what, sizeof what, fmt, ap);
  va_end(ap);
  snprintf(g.why, sizeof g.why, "frame %ld, tick %ld: %s", g.frame, g.ticks, what);
}

// After LakeSnes's DSP has made a sample, ours makes one and they are compared.
static void compare_tick(void) {
  const Dsp* lake = g.apu->dsp;
  const int at = (lake->sampleOffset - 1) & 0x3FF;
  const int16_t l = lake->sampleBuffer[at * 2], r = lake->sampleBuffer[at * 2 + 1];
  if (l != g.dsp.out_l || r != g.dsp.out_r)
    fail("sample: LakeSnes %d,%d, port %d,%d", l, r, g.dsp.out_l, g.dsp.out_r);
  for (int i = 0; i < 128 && !g.failed; i++)
    if (lake->ram[i] != g.dsp.reg[i])
      fail("register $%02X: LakeSnes $%02X, port $%02X", i, lake->ram[i], g.dsp.reg[i]);
  for (int v = 0; v < 8; v++) {
    const DspVoice* x = &g.dsp.voice[v];
    if (x->start_delay == 4) {  // keyed on at this tick
      g.key_ons++;
      if (x->noise) g.noise_ons++;
      if (x->pitch_mod && v > 0) g.pmod_ons++;
    }
  }
  if (g.dsp.echo_writes) g.echo_ticks++;
  g.ticks++;
}

// LakeSnes's `apu_cycle` runs its DSP on every 32nd SPC700 cycle, at the top
// of the cycle, before the access the cycle is for. So ours runs first, then
// LakeSnes's cycle and access, then the access is mirrored, and only then are
// the two compared: a register written in the same cycle is in both by then.
static bool dsp_due(void) { return (g.apu->cycles & 0x1F) == 0; }

static uint8_t shadow_read(void* mem, uint16_t adr) {
  const bool due = dsp_due();
  if (due) sound_dsp_sample(&g.dsp);
  const uint8_t v = lake_read(mem, adr);
  if (due) compare_tick();
  if (spc_runs && adr == (uint16_t)(g.apu->spc->pc - 1)) spc_fetch(adr, v);
  return v;
}

static void shadow_write(void* mem, uint16_t adr, uint8_t val) {
  const bool due = dsp_due();
  if (due) sound_dsp_sample(&g.dsp);
  const uint8_t dsp_adr = g.apu->dspAdr;
  lake_write(mem, adr, val);
  g.ram[adr] = val;
  if (adr == 0xF3 && dsp_adr == trace_reg)
    printf("  tick %ld: $%02X <- $%02X (LakeSnes now $%02X)\n", g.ticks, dsp_adr, val,
           g.apu->dsp->ram[dsp_adr]);
  if (adr == 0xF3 && dsp_adr < 0x80) {
    uint8_t mine = val;
    if (g.breaking && dsp_adr == DSP_MVOL_L && val) {
      mine = (uint8_t)(val + 1);
      g.breaking = false;
      printf("  --break: MVOL_L written as $%02X, not $%02X, at tick %ld\n", mine, val, g.ticks);
    }
    sound_dsp_write(&g.dsp, dsp_adr, mine);
  }
  if (due) compare_tick();
}

static void shadow_idle(void* mem, bool waiting) {
  const bool due = dsp_due();
  if (due) sound_dsp_sample(&g.dsp);
  lake_idle(mem, waiting);
  if (due) compare_tick();
}

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

static bool run_movie(const uint8_t* rom, int rom_len, const char* path, long frames,
                      bool breaking) {
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
  memset(&g, 0, sizeof g);
  g.apu = snes->apu;
  g.breaking = breaking;
  memcpy(g.ram, g.apu->ram, sizeof g.ram);
  sound_dsp_reset(&g.dsp, g.ram);
  Spc* spc = g.apu->spc;
  lake_read = spc->read;
  lake_write = spc->write;
  lake_idle = spc->idle;
  spc->read = shadow_read;
  spc->write = shadow_write;
  spc->idle = shadow_idle;

  int16_t lake_audio[SAMPLES * 2], port_audio[SAMPLES * 2];
  for (g.frame = 0; g.frame < n && !g.failed; g.frame++) {
    movie_apply(&movie, snes, (int)g.frame);
    snes_runFrame(snes);
    if (memcmp(g.ram, g.apu->ram, sizeof g.ram)) {
      for (int i = 0; i < 0x10000; i++)
        if (g.ram[i] != g.apu->ram[i]) {
          fail("sound RAM $%04X: LakeSnes $%02X, port $%02X", i, g.apu->ram[i], g.ram[i]);
          break;
        }
    }
    snes_setSamples(snes, lake_audio, SAMPLES);
    sound_dsp_get_samples(&g.dsp, port_audio, SAMPLES);
    if (!g.failed && memcmp(lake_audio, port_audio, sizeof lake_audio))
      fail("the frame's audio differs");
  }
  printf("%s %s: %ld frames, %ld ticks, %ld key-ons, %ld with noise, %ld pitch-modulated, "
         "echo written on %ld ticks\n",
         g.failed ? "FAIL" : "ok", path, g.frame, g.ticks, g.key_ons, g.noise_ons, g.pmod_ons,
         g.echo_ticks);
  if (g.failed) printf("  %s\n", g.why);
  snes_free(snes);
  movie_free(&movie);
  return !g.failed;
}

int main(int argc, char** argv) {
  bool breaking = false;
  long frames = 0;
  const char* trace_path = NULL;
  const char* rom_path = NULL;
  const char* movies[256];
  int count = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--break")) breaking = true;
    else if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atol(argv[++i]);
    else if (!strcmp(argv[i], "--spc-trace") && i + 1 < argc) trace_path = argv[++i];
    else if (!strcmp(argv[i], "--trace") && i + 1 < argc)
      trace_reg = (int)strtol(argv[++i] + (argv[i][0] == '$'), NULL, 16);
    else if (!rom_path) rom_path = argv[i];
    else if (count < 256) movies[count++] = argv[i];
  }
  if (!rom_path || !count) {
    fprintf(stderr, "usage: zamn_test_dsp [--break] <rom.sfc> <movie.zmv>... [-f frames]\n");
    return 2;
  }
  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) { printf("SKIP: no cartridge at '%s'\n", rom_path); return 0; }
  if (trace_path) spc_runs = (uint32_t*)calloc(0x10000, sizeof *spc_runs);
  int bad = 0;
  for (int i = 0; i < count; i++)
    if (!run_movie(rom, rom_len, movies[i], frames, breaking)) bad++;
  free(rom);
  if (trace_path) {
    FILE* f = fopen(trace_path, "w");
    if (!f) { printf("FAIL: cannot write '%s'\n", trace_path); return 1; }
    for (int a = 0; a < 0x10000; a++)
      if (spc_runs[a]) fprintf(f, "%04X %u\n", a, spc_runs[a]);
    fclose(f);
  }
  if (breaking) {
    // Under --break a pass is the failure.
    printf(bad == count ? "--break: caught in every movie\n" : "--break: NOT CAUGHT\n");
    return bad == count ? 0 : 1;
  }
  printf(bad ? "%d of %d movies FAILED\n" : "all %d movies match\n", bad ? bad : count, count);
  return bad ? 1 : 0;
}
