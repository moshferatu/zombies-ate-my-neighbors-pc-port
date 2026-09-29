// `src/bank_sfx.h`, the monster sounds of the sample sets a level did not load.
//
//   * The eleven checks are where they were surveyed, each asking for the set
//     and id it was found asking for.
//   * A movie with checks the level's set fails runs twice side by side, once
//     watched and once not. Every tick, both machines' work RAM, APU RAM and
//     sound must be identical: this only reads the machine. The copies are
//     built, every failed check is played on the copy holding its set, and
//     each one is heard.
//   * A copy plays what the real APU plays for the same id on a level that has
//     that set loaded: the copies are built on a set 3 level and compared with
//     the real APU on a set 0 and a set 1 level, song stopped, the two lined up
//     to the sample.
//
// Given a folder, it also writes each gated sound, as the copies play it, as
// `bank_sfx_<id>.wav` there for listening.
//
//   zamn_test_bank_sfx [rom.sfc] [movies folder] [folder]

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/movie_apply.h"
#include "bank_sfx.h"
#include "cosim/cosim.h"
#include "snes.h"

#define SAMPLES 534
#define RENDER_FRAMES 180

static int failures;

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

static void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void write_wav(const char* dir, int id, const int16_t* s, long frames) {
  char path[1024];
  snprintf(path, sizeof path, "%s/bank_sfx_%02X.wav", dir, id);
  FILE* f = fopen(path, "wb");
  if (!f) { printf("cannot write %s\n", path); return; }
  const uint32_t bytes = (uint32_t)(frames * SAMPLES * 4);
  fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
  put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 32040); put32(f, 32040 * 4);
  put16(f, 4); put16(f, 16); fwrite("data", 1, 4, f); put32(f, bytes);
  fwrite(s, 4, (size_t)(frames * SAMPLES), f);
  fclose(f);
  printf("wrote %s\n", path);
}

static long movie_end(const Movie* m) {
  long last = 0;
  for (int p = 0; p < MOVIE_PORTS; p++)
    if (m->track[p].count && m->track[p].events[m->track[p].count - 1].frame > last)
      last = m->track[p].events[m->track[p].count - 1].frame;
  return last + 600;
}

static Snes* machine(uint8_t* rom, int rom_len) {
  Snes* s = snes_init();
  if (!snes_loadRom(s, rom, rom_len)) { printf("FAIL: the core will not load the ROM\n"); exit(1); }
  snes_reset(s, true);
  return s;
}

// Play a movie to its end on a bare machine, for its APU.
static Snes* play_to_end(uint8_t* rom, int rom_len, const char* path) {
  Movie m;
  if (!movie_load(&m, path)) { printf("FAIL: cannot load '%s'\n", path); failures++; return NULL; }
  Snes* s = machine(rom, rom_len);
  Cosim c;
  cosim_init(&c, s, COSIM_NATIVE);
  cosim_enable_all(&c);
  int16_t audio[SAMPLES * 2];
  for (long f = 0, n = movie_end(&m); f < n; f++) {
    movie_apply(&m, s, (int)f);
    cosim_frame(&c);
    snes_setSamples(s, audio, SAMPLES);
  }
  cosim_free(&c);
  movie_free(&m);
  return s;
}

static void render(Apu* a, uint8_t id, int16_t* out) {
  bsfx_send(a, BSFX_CMD_PLAY, id);
  for (int f = 0; f < RENDER_FRAMES; f++) {
    const uint32_t s = a->cycles;
    while (a->cycles - s < BSFX_FRAME_CYCLES) spc_runOpcode(a->spc);
    dsp_getSamples(a->dsp, out + f * SAMPLES * 2, SAMPLES);
  }
}

// A copy of a machine's APU with its song stopped and everything rung out.
static Apu* reference(Snes* snes) {
  StateHandler* save = sh_init(true, NULL, 0);
  apu_handleState(snes->apu, save);
  Apu* a = apu_init(snes);
  apu_reset(a);
  StateHandler* load = sh_init(false, save->data, save->offset);
  apu_handleState(a, load);
  sh_free(load);
  sh_free(save);
  bsfx_stop_song(a);
  const uint32_t s = a->cycles;
  while (a->cycles - s < 8 * BSFX_SETTLE) spc_runOpcode(a->spc);
  return a;
}

// How much of `a` is left once `b`, moved by the lag that fits best, is taken
// away, as a share of `a`'s energy. The left channel.
static double residual(const int16_t* a, const int16_t* b, double* energy_a, double* energy_b) {
  const int n = RENDER_FRAMES * SAMPLES;
  double ea = 0, eb = 0;
  for (int i = 0; i < n; i++) {
    ea += (double)a[2 * i] * a[2 * i];
    eb += (double)b[2 * i] * b[2 * i];
  }
  int best = 0;
  double best_c = -1e300;
  for (int lag = -800; lag <= 800; lag++) {
    double c = 0;
    for (int i = 0; i < n; i++)
      if (i + lag >= 0 && i + lag < n) c += (double)a[2 * i] * b[2 * (i + lag)];
    if (c > best_c) { best_c = c; best = lag; }
  }
  double err = 0;
  for (int i = 0; i < n; i++) {
    const double y = i + best >= 0 && i + best < n ? b[2 * (i + best)] : 0;
    err += (a[2 * i] - y) * (a[2 * i] - y);
  }
  *energy_a = ea;
  *energy_b = eb;
  return ea > 0 ? err / ea : 1.0;
}

static void check_sites(BankSfx* b) {
  static const struct { uint32_t at; uint8_t set, id; } want[] = {
    {0x8193D3, 0, 0x29}, {0x819811, 0, 0x2A}, {0x81AC05, 1, 0x2B}, {0x81B2C7, 3, 0x28},
    {0x828D6C, 2, 0x25}, {0x82EA35, 3, 0x27}, {0x82EFDE, 3, 0x26}, {0x83AF43, 3, 0x27},
    {0x83B5A4, 3, 0x27}, {0x83BF55, 3, 0x27}, {0x83C7D7, 3, 0x27},
  };
  const int n = (int)(sizeof want / sizeof want[0]);
  if (b->sites != n) { printf("FAIL: %d checks found, not %d\n", b->sites, n); failures++; return; }
  for (int k = 0; k < n; k++)
    if (b->site_at[k] != want[k].at || b->site_set[k] != want[k].set || b->site_id[k] != want[k].id) {
      printf("FAIL: check %d is $%06X set %d id $%02X, not $%06X set %d id $%02X\n", k,
             b->site_at[k], b->site_set[k], b->site_id[k], want[k].at, want[k].set, want[k].id);
      failures++;
    }
  printf("checks: %d, each where it was surveyed\n", b->sites);
}

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
  return h;
}

// The machine after a tick: work RAM, APU RAM and the tick's sound.
static uint64_t tick_hash(const Snes* s, const int16_t* audio) {
  uint64_t h = fnv(0xcbf29ce484222325ull, s->ram, sizeof s->ram);
  h = fnv(h, s->apu->ram, sizeof s->apu->ram);
  return fnv(h, audio, SAMPLES * 4);
}

// Run a movie unwatched, keeping a hash of every tick. One machine at a time:
// the port's APU hook is one global (`apu_attach`), so two harnesses in one
// process would send each other's sound effects to the wrong APU.
static uint64_t* unwatched_run(uint8_t* rom, int rom_len, const char* path, long* out_n) {
  Movie m;
  if (!movie_load(&m, path)) return NULL;
  Snes* s = machine(rom, rom_len);
  Cosim c;
  cosim_init(&c, s, COSIM_NATIVE);
  cosim_enable_all(&c);
  const long n = movie_end(&m);
  uint64_t* h = (uint64_t*)malloc(sizeof *h * (size_t)n);
  int16_t audio[SAMPLES * 2];
  for (long f = 0; f < n; f++) {
    movie_apply(&m, s, (int)f);
    cosim_frame(&c);
    snes_setSamples(s, audio, SAMPLES);
    h[f] = tick_hash(s, audio);
  }
  cosim_free(&c);
  snes_free(s);
  movie_free(&m);
  *out_n = n;
  return h;
}

// A movie watched, against the same movie unwatched.
static void side_by_side(uint8_t* rom, int rom_len, const char* path, long want_min) {
  long n = 0;
  uint64_t* plain = unwatched_run(rom, rom_len, path, &n);
  Movie m1;
  if (!plain || !movie_load(&m1, path)) {
    printf("FAIL: cannot load '%s'\n", path);
    failures++;
    free(plain);
    return;
  }
  Snes* with = machine(rom, rom_len);
  Cosim cw;
  cosim_init(&cw, with, COSIM_NATIVE);
  cosim_enable_all(&cw);
  static BankSfx b;
  bank_sfx_init(&b, with, rom, rom_len, true);
  bool watched = cosim_watch(&cw, BSFX_SEND_AT, bank_sfx_at, &b);
  for (int k = 0; k < b.sites; k++) watched = watched && cosim_watch(&cw, b.site_at[k], bank_sfx_at, &b);
  if (!watched) { printf("FAIL: no room for the watches\n"); failures++; }

  int16_t aw[SAMPLES * 2], mix[SAMPLES * 2];
  long diverged = -1, built_at = -1;
  long played_before = 0, pending_since = -1, heard = 0, unheard = 0;
  for (long f = 0; f < n; f++) {
    movie_apply(&m1, with, (int)f);
    cosim_frame(&cw);
    bank_sfx_tick(&b);
    if (b.built && built_at < 0) built_at = f;
    snes_setSamples(with, aw, SAMPLES);
    if (diverged < 0 && tick_hash(with, aw) != plain[f]) diverged = f;
    // Whether the copies add anything within ten ticks of being handed a
    // sound. Two in quick succession are one wait.
    long played = 0;
    for (int i = 0; i < BSFX_SETS; i++) played += b.set[i].played;
    if (played > played_before && pending_since < 0) pending_since = f;
    played_before = played;
    memcpy(mix, aw, sizeof mix);
    bank_sfx_mix(&b, mix, SAMPLES);
    if (pending_since >= 0 && memcmp(mix, aw, sizeof mix)) { heard++; pending_since = -1; }
    if (pending_since >= 0 && f - pending_since > 10) { unheard++; pending_since = -1; }
  }
  long played = 0;
  for (int i = 0; i < BSFX_SETS; i++) played += b.set[i].played;
  printf("%s: %ld ticks, copies built at %ld%s; %ld checks failed, %ld played (%ld, %ld, %ld, %ld), "
         "heard on %ld ticks, %ld unheard\n",
         path, n, built_at, b.failed ? " (not all)" : "", b.wanted, played, b.set[0].played,
         b.set[1].played, b.set[2].played, b.set[3].played, heard, unheard);
  if (diverged >= 0) { printf("FAIL: the machines differ from tick %ld\n", diverged); failures++; }
  if (built_at < 0 || b.failed) { printf("FAIL: the copies were not all built\n"); failures++; }
  if (b.wanted < want_min) { printf("FAIL: fewer failed checks than %ld\n", want_min); failures++; }
  if (played != b.wanted) { printf("FAIL: %ld failed checks but %ld played\n", b.wanted, played); failures++; }
  if (unheard || !heard) { printf("FAIL: %ld sounds handed to a copy were not heard\n", unheard); failures++; }
  bank_sfx_free(&b);
  cosim_free(&cw);
  snes_free(with);
  movie_free(&m1);
  free(plain);
}

int main(int argc, char** argv) {
  const char* rom_path = argc > 1 ? argv[1] : "Zombies Ate My Neighbors.sfc";
  const char* movies = argc > 2 ? argv[2] : "movies";
  const char* dir = argc > 3 ? argv[3] : NULL;

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) { printf("SKIP: no cartridge at '%s'\n", rom_path); return 0; }
  char path[1024];

  {
    Snes* s = machine(rom, rom_len);
    static BankSfx b;
    bank_sfx_init(&b, s, rom, rom_len, true);
    check_sites(&b);
    snes_free(s);
  }

  // Level 25 asks for set 2's $25 on set 0, and level 37 for set 3's $26
  // and $27 on set 1.
  snprintf(path, sizeof path, "%s/level25.zmv", movies);
  side_by_side(rom, rom_len, path, 20);
  snprintf(path, sizeof path, "%s/level37.zmv", movies);
  side_by_side(rom, rom_len, path, 3);

  // The copies, built on level 1 (set 3), against the real APU on level 5
  // (set 0) and level 17 (set 1).
  snprintf(path, sizeof path, "%s/level1.zmv", movies);
  Snes* base = play_to_end(rom, rom_len, path);
  static BankSfx b;
  if (base) {
    bank_sfx_init(&b, base, rom, rom_len, true);
    bank_sfx_tick(&b);
    printf("copies built on set %d: %s\n", base->ram[BSFX_BANK_AT],
           b.built && !b.failed ? "all four" : "not all");
    if (!b.built || b.failed) failures++;
  }
  static const struct { const char* movie; int set; uint8_t ids[2]; } refs[] = {
    {"level5.zmv", 0, {0x29, 0x2A}},
    {"level17.zmv", 1, {0x2B, 0x2B}},
  };
  static int16_t x[RENDER_FRAMES * SAMPLES * 2], y[RENDER_FRAMES * SAMPLES * 2];
  for (int r = 0; base && b.built && r < 2; r++) {
    snprintf(path, sizeof path, "%s/%s", movies, refs[r].movie);
    Snes* s = play_to_end(rom, rom_len, path);
    if (!s) continue;
    if (s->ram[BSFX_BANK_AT] != refs[r].set) {
      printf("FAIL: %s ends on set %d, not %d\n", path, s->ram[BSFX_BANK_AT], refs[r].set);
      failures++;
    }
    for (int k = 0; k < 2 && (k == 0 || refs[r].ids[1] != refs[r].ids[0]); k++) {
      Apu* ref = reference(s);
      render(ref, refs[r].ids[k], y);
      apu_free(ref);
      render(b.set[refs[r].set].apu, refs[r].ids[k], x);
      double ex, ey;
      const double res = residual(x, y, &ex, &ey);
      printf("$%02X on the set %d copy against %s: energy %.3g and %.3g, %.1f%% left over\n",
             refs[r].ids[k], refs[r].set, refs[r].movie, ex, ey, res * 100);
      if (ex <= 0 || res > 0.10 || ex < 0.8 * ey || ex > 1.25 * ey) {
        printf("FAIL: not the same sound\n");
        failures++;
      }
    }
    snes_free(s);
  }

  if (dir && base && b.built) {
    static const uint8_t ids[] = {0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B};
    for (size_t i = 0; i < sizeof ids; i++) {
      int set = -1;
      for (int k = 0; k < b.sites; k++) if (b.site_id[k] == ids[i]) set = b.site_set[k];
      if (set < 0 || !b.set[set].apu) continue;
      render(b.set[set].apu, ids[i], x);
      write_wav(dir, ids[i], x, RENDER_FRAMES);
    }
  }

  if (base) { bank_sfx_free(&b); snes_free(base); }
  free(rom);
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("PASS\n");
  return 0;
}
