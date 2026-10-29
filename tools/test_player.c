// The player (`src/sound/player.h`) beside the driver it follows, on each
// movie. The game runs on the stock console with the port's sound in it, as
// `zamn` has, and the player is told what the driver is told.
//
// With the console's settings (`player_console`), every DSP write the driver
// makes must be the next one the player made: register, value and cycle, with
// none left over at the end of a tick. And every frame's audio must be the
// same, from the first the player makes.
//
// `--wide` gives the player the effect overlay's settings (`player_wide`) and
// counts instead: what the driver dropped and what the player did.
//
// `--break` gives the player a ninth voice the driver does not have, which
// changes which voice a note gets: it must then fail.
//
//   zamn_test_player [--wide | --break] <rom.sfc> <movie.zmv>... [-f frames]

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/movie_apply.h"
#include "snes.h"
#include "sound/console.h"
#include "sound/player.h"

#define SAMPLES 534
#define FIFO 65536

typedef struct {
  uint8_t reg, val;
  uint64_t cycle;
} Write;

static struct {
  Player* player;
  Sound* sound;
  bool compare;
  Write fifo[FIFO];
  int head, count;
  bool failed;
  char why[256];
  long frame, writes;
  // What the driver lost.
  long real_no_track, real_no_voice, real_steal_pass;
} g;

static void on_write(void* ctx, int unit, uint8_t reg, uint8_t val, uint64_t cycle) {
  (void)ctx;
  (void)unit;
  if (!g.compare) return;
  if (g.count == FIFO) {
    if (!g.failed) snprintf(g.why, sizeof g.why, "the player is %d writes ahead", FIFO);
    g.failed = true;
    return;
  }
  g.fifo[(g.head + g.count++) % FIFO] = (Write){reg, val, cycle};
}

static void watch(void* ctx, SoundSeen what, uint16_t a, uint8_t v, uint64_t cycle) {
  (void)ctx;
  player_seen(g.player, what, a, v, cycle);
  const uint8_t* r = g.sound->ram;
  if (what == SOUND_SEEN_AT) {
    if (a == 0x0B9B && r[0xC7] < r[0xCA]) g.real_no_track++;
    if (a == 0x0D84) g.real_no_voice++;
    if (a == 0x0D70) g.real_steal_pass++;
    return;
  }
  if (what != SOUND_SEEN_DSP || !g.compare || !g.player->synced || g.failed) return;
  g.writes++;
  if (!g.count) {
    snprintf(g.why, sizeof g.why,
             "frame %ld, cycle %llu: the driver wrote $%02X to DSP $%02X, and the player nothing",
             g.frame, (unsigned long long)cycle, v, a);
    g.failed = true;
    return;
  }
  const Write w = g.fifo[g.head];
  g.head = (g.head + 1) % FIFO;
  g.count--;
  if (w.reg != a || w.val != v || w.cycle != cycle) {
    snprintf(g.why, sizeof g.why,
             "frame %ld: the driver wrote $%02X to DSP $%02X on cycle %llu, the player $%02X to "
             "$%02X on %llu",
             g.frame, v, a, (unsigned long long)cycle, w.val, w.reg, (unsigned long long)w.cycle);
    g.failed = true;
  }
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

typedef enum { MODE_CONSOLE, MODE_WIDE, MODE_BREAK } Mode;

static bool run_movie(const uint8_t* rom, int rom_len, const char* path, long frames, Mode mode) {
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
  sound_attach(snes, g.sound);
  snes_reset(snes, true);
  PlayerSettings set = mode == MODE_WIDE ? player_wide : player_console;
  if (mode == MODE_BREAK) set.live_voices = 16;
  player_init(g.player, g.sound, set);
  g.player->on_write = on_write;
  g.compare = mode != MODE_WIDE;
  g.sound->watch = watch;
  g.head = g.count = 0;
  g.failed = false;
  g.writes = g.real_no_track = g.real_no_voice = g.real_steal_pass = 0;

  int16_t real[SAMPLES * 2], heard[SAMPLES * 2];
  double e_real = 0, e_heard = 0;
  long imports = 0, same = 0, heard_frames = 0;
  for (g.frame = 0; g.frame < n && !g.failed; g.frame++) {
    movie_apply(&movie, snes, (int)g.frame);
    snes_runFrame(snes);
    player_run(g.player, g.sound->host);
    if (g.player->imports != imports) {
      imports = g.player->imports;
      g.head = g.count = 0;
    }
    if (g.compare && g.count && !g.failed) {
      snprintf(g.why, sizeof g.why, "frame %ld: the player made %d writes the driver did not",
               g.frame, g.count);
      g.failed = true;
    }
    snes_setSamples(snes, real, SAMPLES);
    player_get_samples(g.player, heard, SAMPLES);
    if (g.player->synced) {
      heard_frames++;
      if (!memcmp(real, heard, sizeof real)) same++;
      else if (g.compare && !g.failed) {
        snprintf(g.why, sizeof g.why, "frame %ld: the player's audio differs", g.frame);
        g.failed = true;
      }
    }
    for (int i = 0; i < SAMPLES * 2; i++) {
      e_real += (double)real[i] * real[i];
      e_heard += (double)heard[i] * heard[i];
    }
  }
  const double ratio = e_real > 0 ? e_heard / e_real : 1.0;
  const Player* p = g.player;
  if (mode != MODE_WIDE && !g.failed && (ratio < 0.9 || ratio > 1.1)) {
    snprintf(g.why, sizeof g.why, "the player's sound has %.3f of the driver's energy", ratio);
    g.failed = true;
  }
  if (mode != MODE_WIDE && !g.failed && !g.writes) {
    snprintf(g.why, sizeof g.why, "the driver made no DSP writes after the player took over");
    g.failed = true;
  }
  printf("%s %s: %ld frames, %ld DSP writes the same, %ld imports; energy %.4f of the "
         "driver's, %ld of %ld frames identical\n",
         g.failed ? "FAIL" : "ok", path, g.frame, g.writes, p->imports, ratio, same, heard_frames);
  printf("    driver: %ld effects with no track, %ld notes with no voice, %ld taken\n",
         g.real_no_track, g.real_no_voice, g.real_steal_pass - g.real_no_voice);
  printf("    player: %ld effects started, %ld with no track, %ld held at %d, %ld notes "
         "with no voice, %ld taken\n",
         p->effects_started, p->effects_dropped, p->effects_held, p->set.max_same,
         p->notes_dropped, p->voices_stolen);
  if (g.failed) printf("  %s\n", g.why);
  player_detach(g.player, g.sound);
  g.sound->watch = NULL;
  snes_free(snes);
  movie_free(&movie);
  return !g.failed;
}

int main(int argc, char** argv) {
  long frames = 0;
  Mode mode = MODE_CONSOLE;
  const char* rom_path = NULL;
  const char* movies[256];
  int count = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--wide")) mode = MODE_WIDE;
    else if (!strcmp(argv[i], "--break")) mode = MODE_BREAK;
    else if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atol(argv[++i]);
    else if (!rom_path) rom_path = argv[i];
    else if (count < 256) movies[count++] = argv[i];
  }
  if (!rom_path || !count) {
    fprintf(stderr, "usage: zamn_test_player [--wide | --break] <rom.sfc> <movie.zmv>... [-f frames]\n");
    return 2;
  }
  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) { printf("SKIP: no cartridge at '%s'\n", rom_path); return 0; }
  g.player = (Player*)malloc(sizeof *g.player);
  g.sound = (Sound*)malloc(sizeof *g.sound);
  int bad = 0;
  for (int i = 0; i < count; i++)
    if (!run_movie(rom, rom_len, movies[i], frames, mode)) bad++;
  free(rom);
  if (mode == MODE_BREAK) {
    printf(bad == count ? "--break: caught in every movie\n" : "--break: NOT CAUGHT in %d\n",
           count - bad);
    return bad == count ? 0 : 1;
  }
  if (mode == MODE_WIDE) return bad ? 1 : 0;
  printf(bad ? "%d of %d movies FAILED\n" : "all %d movies match\n", bad ? bad : count, count);
  return bad ? 1 : 0;
}
