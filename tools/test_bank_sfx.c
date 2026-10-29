// `src/bank_sfx.h`, the monster sounds of the sample sets a level did not load.
//
//   * The eleven checks are where they were surveyed, each asking for the set
//     and id it was found asking for.
//   * A movie with checks the level's set fails runs twice side by side, once
//     watched and once not. Every tick, both machines' work RAM, sound RAM and
//     the game's sound must be identical: this only reads the machine. The
//     sets are loaded on a follower, every failed check is played from its
//     set, and each one is heard.
//   * The follower plays what the game's sound plays for the same id on a
//     level that has that set loaded: the sets are loaded on a set 3 level and
//     compared with the player on the chip on a set 0 and a set 1 level, song
//     stopped, the two lined up to the sample.
//
// Given a folder, it also writes each gated sound, as the player plays it, as
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
#include "sound/console.h"
#include "sound/player.h"

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

// A console with the port's sound in it, as the game has: the chip and the
// player on it. With `heard`, a follower that holds the other sets.
typedef struct {
  Snes* snes;
  Sound* sound;
  Player* lead;
  Player* player;
} Machine;

static Machine machine(uint8_t* rom, int rom_len, bool heard) {
  Machine m = {snes_init(), (Sound*)malloc(sizeof(Sound)), (Player*)malloc(sizeof(Player)), NULL};
  if (!snes_loadRom(m.snes, rom, rom_len)) { printf("FAIL: the core will not load the ROM\n"); exit(1); }
  sound_attach(m.snes, m.sound, m.lead);
  snes_reset(m.snes, true);
  if (heard) {
    m.player = (Player*)malloc(sizeof(Player));
    PlayerSettings set = player_console;
    set.other_sets = true;
    player_init(m.player, set);
    player_follow(m.player, m.lead);
  }
  return m;
}

static void machine_free(Machine* m) {
  if (m->player) player_unfollow(m->player);
  snes_free(m->snes);
  free(m->sound);
  free(m->lead);
  free(m->player);
}

// --- Driving a sound with nothing on the 65816 side -------------------------

static bool run_until_taken(Machine* m) {
  // The player is stopped at a read of a port; it is let make that read, and
  // the next, until it has taken the command.
  Sound* a = m->sound;
  const uint64_t start = a->host;
  while (a->written[3] != m->lead->seq) {
    if (a->host - start > 400000 || a->state == SOUND_HALTED) return false;
    sound_run(a, a->now + 1);
  }
  return true;
}

// One command, the way `apu_send` puts it on the ports.
static bool send(Machine* m, uint8_t cmd, uint8_t param) {
  if (!run_until_taken(m)) return false;
  Sound* a = m->sound;
  sound_write_port(a, 2, cmd);
  sound_write_port(a, 1, param);
  sound_write_port(a, 3, (uint8_t)(a->written[3] + 1));
  return run_until_taken(m);
}

// Every track playing a song (a sequence at or past the effects) stopped.
static void stop_song(Machine* m) {
  const Player* l = m->lead;
  for (int i = 0; i < 4; i++)
    if (l->track[i].wait && l->track[i].song >= l->bank[0].saved_songs)
      send(m, 0x02, l->track[i].song);
}

// A frame of the machine, and of `p` following it, which is heard if given.
static void frame(Machine* m, Player* p, int16_t* out) {
  Sound* a = m->sound;
  sound_run(a, a->host + 17088);
  if (m->player) player_run(m->player, a->host);
  if (p) player_get_samples(p, out, SAMPLES);
  else sound_get_samples(a, out, SAMPLES);
}

// Play a movie to its end, for its sound; with a player, the sets are loaded
// on it.
static bool play_to_end(Machine* m, const char* path) {
  Movie mv;
  if (!movie_load(&mv, path)) { printf("FAIL: cannot load '%s'\n", path); failures++; return false; }
  Cosim c;
  cosim_init(&c, m->snes, COSIM_NATIVE);
  cosim_enable_all(&c);
  static BankSfx b;
  bank_sfx_init(&b, m->player, m->snes->cart->rom, (int)m->snes->cart->romSize, m->player != NULL);
  int16_t audio[SAMPLES * 2];
  for (long f = 0, n = movie_end(&mv); f < n; f++) {
    movie_apply(&mv, m->snes, (int)f);
    cosim_frame(&c);
    if (m->player) {
      player_run(m->player, m->sound->host);
      bank_sfx_tick(&b);
    }
    snes_setSamples(m->snes, audio, SAMPLES);
  }
  if (m->player && (!b.built || b.failed)) {
    printf("FAIL: the sets were not all loaded on the follower\n");
    failures++;
  }
  cosim_free(&c);
  movie_free(&mv);
  return true;
}

// `id` from set `set` on `m`'s follower, or (`set` < 0) on the player on the
// chip, with the song stopped and everything rung out first.
static void render(Machine* m, int set, uint8_t id, int16_t* out) {
  static int16_t scratch[SAMPLES * 2];
  stop_song(m);
  for (int f = 0; f < 240; f++) frame(m, m->player, scratch);
  if (set >= 0) {
    if (!player_play_from_set(m->player, set, id)) {
      printf("FAIL: the follower would not play $%02X from set %d\n", id, set);
      failures++;
    }
  } else {
    send(m, 0x01, id);
  }
  for (int f = 0; f < RENDER_FRAMES; f++)
    frame(m, set >= 0 ? m->player : NULL, out + f * SAMPLES * 2);
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

// The machine after a tick: work RAM, sound RAM and the game's sound.
static uint64_t tick_hash(const Machine* m, const int16_t* audio) {
  uint64_t h = fnv(0xcbf29ce484222325ull, m->snes->ram, sizeof m->snes->ram);
  h = fnv(h, m->lead->unit[0].ram, sizeof m->lead->unit[0].ram);
  return fnv(h, audio, SAMPLES * 4);
}

// Run a movie unwatched and with no player, keeping a hash of every tick.
static uint64_t* unwatched_run(uint8_t* rom, int rom_len, const char* path, long* out_n) {
  Movie mv;
  if (!movie_load(&mv, path)) return NULL;
  Machine m = machine(rom, rom_len, false);
  Cosim c;
  cosim_init(&c, m.snes, COSIM_NATIVE);
  cosim_enable_all(&c);
  const long n = movie_end(&mv);
  uint64_t* h = (uint64_t*)malloc(sizeof *h * (size_t)n);
  int16_t audio[SAMPLES * 2];
  for (long f = 0; f < n; f++) {
    movie_apply(&mv, m.snes, (int)f);
    cosim_frame(&c);
    snes_setSamples(m.snes, audio, SAMPLES);
    h[f] = tick_hash(&m, audio);
  }
  cosim_free(&c);
  machine_free(&m);
  movie_free(&mv);
  *out_n = n;
  return h;
}

// Whether any of the other sets' voices is sounding.
static bool sets_sounding(const Player* p) {
  for (int s = 0; s < PLAYER_SETS; s++) {
    const PlayerUnit* u = &p->unit[p->bank[1 + s].first_unit];
    for (int v = 0; v < 8; v++)
      if (u->dsp.voice[v].level > 0x40) return true;
  }
  return false;
}

// A movie watched, against the same movie unwatched.
static void side_by_side(uint8_t* rom, int rom_len, const char* path, long want_min) {
  long n = 0;
  uint64_t* plain = unwatched_run(rom, rom_len, path, &n);
  Movie mv;
  if (!plain || !movie_load(&mv, path)) {
    printf("FAIL: cannot load '%s'\n", path);
    failures++;
    free(plain);
    return;
  }
  Machine with = machine(rom, rom_len, true);
  Cosim cw;
  cosim_init(&cw, with.snes, COSIM_NATIVE);
  cosim_enable_all(&cw);
  static BankSfx b;
  bank_sfx_init(&b, with.player, rom, rom_len, true);
  bool watched = true;
  for (int k = 0; k < b.sites; k++) watched = watched && cosim_watch(&cw, b.site_at[k], bank_sfx_at, &b);
  if (!watched) { printf("FAIL: no room for the watches\n"); failures++; }

  int16_t aw[SAMPLES * 2];
  long diverged = -1, built_at = -1;
  long played_before = 0, pending_since = -1, heard = 0, unheard = 0;
  for (long f = 0; f < n; f++) {
    movie_apply(&mv, with.snes, (int)f);
    cosim_frame(&cw);
    player_run(with.player, with.sound->host);
    bank_sfx_tick(&b);
    if (b.built && built_at < 0) built_at = f;
    snes_setSamples(with.snes, aw, SAMPLES);
    if (diverged < 0 && tick_hash(&with, aw) != plain[f]) diverged = f;
    // Whether the sets' voices sound within ten ticks of being handed a
    // sound. Two in quick succession are one wait.
    if (b.played > played_before && pending_since < 0) pending_since = f;
    played_before = b.played;
    if (pending_since >= 0 && sets_sounding(with.player)) { heard++; pending_since = -1; }
    if (pending_since >= 0 && f - pending_since > 10) { unheard++; pending_since = -1; }
  }
  const Player* p = with.player;
  printf("%s: %ld ticks, sets loaded at %ld%s; %ld checks failed, %ld played (%ld, %ld, %ld, %ld), "
         "heard %ld times, %ld unheard\n",
         path, n, built_at, b.failed ? " (not all)" : "", b.wanted, b.played, p->other_played[0],
         p->other_played[1], p->other_played[2], p->other_played[3], heard, unheard);
  if (diverged >= 0) { printf("FAIL: the machines differ from tick %ld\n", diverged); failures++; }
  if (built_at < 0 || b.failed) { printf("FAIL: the sets were not all loaded\n"); failures++; }
  if (b.wanted < want_min) { printf("FAIL: fewer failed checks than %ld\n", want_min); failures++; }
  if (b.played != b.wanted) { printf("FAIL: %ld failed checks but %ld played\n", b.wanted, b.played); failures++; }
  if (unheard || !heard) { printf("FAIL: %ld sounds handed to the follower were not heard\n", unheard); failures++; }
  cosim_free(&cw);
  machine_free(&with);
  movie_free(&mv);
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
    Machine m = machine(rom, rom_len, true);
    static BankSfx b;
    bank_sfx_init(&b, m.player, rom, rom_len, true);
    check_sites(&b);
    machine_free(&m);
  }

  // Level 25 asks for set 2's $25 on set 0, and level 37 for set 3's $26
  // and $27 on set 1.
  snprintf(path, sizeof path, "%s/level25.zmv", movies);
  side_by_side(rom, rom_len, path, 20);
  snprintf(path, sizeof path, "%s/level37.zmv", movies);
  side_by_side(rom, rom_len, path, 3);

  // The sets, loaded on the follower on level 1 (set 3), against the player
  // on the chip on level 5 (set 0) and level 17 (set 1).
  snprintf(path, sizeof path, "%s/level1.zmv", movies);
  Machine base = machine(rom, rom_len, true);
  const bool have_base = play_to_end(&base, path);
  if (have_base) printf("sets loaded on the follower on set %d\n", base.snes->ram[BSFX_BANK_AT]);
  static const struct { const char* movie; int set; uint8_t ids[2]; } refs[] = {
    {"level5.zmv", 0, {0x29, 0x2A}},
    {"level17.zmv", 1, {0x2B, 0x2B}},
  };
  static int16_t x[RENDER_FRAMES * SAMPLES * 2], y[RENDER_FRAMES * SAMPLES * 2];
  for (int r = 0; have_base && r < 2; r++) {
    snprintf(path, sizeof path, "%s/%s", movies, refs[r].movie);
    Machine s = machine(rom, rom_len, false);
    if (!play_to_end(&s, path)) { machine_free(&s); continue; }
    if (s.snes->ram[BSFX_BANK_AT] != refs[r].set) {
      printf("FAIL: %s ends on set %d, not %d\n", path, s.snes->ram[BSFX_BANK_AT], refs[r].set);
      failures++;
    }
    for (int k = 0; k < 2 && (k == 0 || refs[r].ids[1] != refs[r].ids[0]); k++) {
      render(&s, -1, refs[r].ids[k], y);
      render(&base, refs[r].set, refs[r].ids[k], x);
      double ex, ey;
      const double res = residual(x, y, &ex, &ey);
      printf("$%02X from set %d on the follower against %s: energy %.3g and %.3g, %.1f%% left over\n",
             refs[r].ids[k], refs[r].set, refs[r].movie, ex, ey, res * 100);
      if (ex <= 0 || res > 0.10 || ex < 0.8 * ey || ex > 1.25 * ey) {
        printf("FAIL: not the same sound\n");
        failures++;
      }
    }
    machine_free(&s);
  }

  if (dir && have_base) {
    static BankSfx b;
    bank_sfx_init(&b, base.player, rom, rom_len, true);
    static const uint8_t ids[] = {0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B};
    for (size_t i = 0; i < sizeof ids; i++) {
      int set = -1;
      for (int k = 0; k < b.sites; k++) if (b.site_id[k] == ids[i]) set = b.site_set[k];
      if (set < 0) continue;
      render(&base, set, ids[i], x);
      write_wav(dir, ids[i], x, RENDER_FRAMES);
    }
  }

  machine_free(&base);
  free(rom);
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("PASS\n");
  return 0;
}
