// `src/sfx_overlay.h` against a movie: the game runs twice side by side, once
// with the overlay and once without.
//
//   * Every tick, both machines' work RAM, APU RAM and sound must be
//     identical. The overlay only reads the machine.
//   * The copy is taken once and never has to be taken again on a straight run.
//   * What the copy adds is silence until the real driver first loses an
//     effect, a note or a voice. After that, whenever a voice it has opened is
//     sounding, the tick is not silence.
//
// Given a folder, it also writes `original.wav`, `overlay.wav` and `added.wav`
// there for listening.
//
//   zamn_test_sfx_overlay [rom.sfc] [movie.zmv] [frames] [folder]

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis/movie_apply.h"
#include "sfx_overlay.h"
#include "snes.h"

#define SAMPLES 534

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

static void write_wav(const char* dir, const char* name, const int16_t* s, long frames) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
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

int main(int argc, char** argv) {
  const char* rom_path = argc > 1 ? argv[1] : "Zombies Ate My Neighbors.sfc";
  const char* movie_path = argc > 2 ? argv[2] : "movies/level9-weapons.zmv";
  const long frames = argc > 3 ? atol(argv[3]) : 0;
  const char* dir = argc > 4 ? argv[4] : NULL;

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) { printf("SKIP: no cartridge at '%s'\n", rom_path); return 0; }
  // One each: a movie is read forwards and keeps its place.
  Movie movie, movie2;
  if (!movie_load(&movie, movie_path) || !movie_load(&movie2, movie_path)) {
    printf("FAIL: cannot load '%s'\n", movie_path);
    return 1;
  }
  // By default, to ten seconds past the movie's last press.
  long last = 0;
  for (int p = 0; p < MOVIE_PORTS; p++)
    if (movie.track[p].count && movie.track[p].events[movie.track[p].count - 1].frame > last)
      last = movie.track[p].events[movie.track[p].count - 1].frame;
  const long n = frames > 0 ? frames : last + 600;

  Snes* with = snes_init();
  Snes* without = snes_init();
  if (!snes_loadRom(with, rom, rom_len) || !snes_loadRom(without, rom, rom_len)) {
    printf("FAIL: the core will not load '%s'\n", rom_path);
    return 1;
  }
  static SfxOverlay ov;
  sfx_overlay_init(&ov, with, true);

  int16_t* orig = (int16_t*)calloc((size_t)n * SAMPLES * 2, sizeof(int16_t));
  int16_t* mixed = (int16_t*)calloc((size_t)n * SAMPLES * 2, sizeof(int16_t));
  int16_t other[SAMPLES * 2];
  long first_sound = -1, silent_after = 0, diverged = -1, unheard = -1;

  for (long f = 0; f < n; f++) {
    movie_apply(&movie, with, (int)f);
    movie_apply(&movie2, without, (int)f);
    snes_runFrame(with);
    snes_runFrame(without);
    const long losses = ov.effects_dropped + ov.notes_dropped + ov.voices_stolen;
    sfx_overlay_tick(&ov);

    int16_t* o = orig + f * SAMPLES * 2;
    int16_t* m = mixed + f * SAMPLES * 2;
    snes_setSamples(with, o, SAMPLES);
    snes_setSamples(without, other, SAMPLES);
    memcpy(m, o, sizeof other);
    sfx_overlay_mix(&ov, m, SAMPLES);

    if (diverged < 0 && (memcmp(with->ram, without->ram, 0x20000) ||
                         memcmp(with->apu->ram, without->apu->ram, 0x10000) ||
                         memcmp(o, other, sizeof other))) {
      diverged = f;
    }
    bool sound = false;
    for (int i = 0; i < SAMPLES * 2; i++) sound |= m[i] != o[i];
    if (sound && first_sound < 0) first_sound = f;
    // Before any loss the copy must be silent: every one of its voices is one
    // the real APU is playing. (`losses` is from before this tick's run, so a
    // loss inside the tick is allowed to be heard in it.)
    if (sound && losses == 0 && ov.effects_dropped + ov.notes_dropped + ov.voices_stolen == 0)
      silent_after = f + 1;
    // And a voice it has opened is heard while it sounds. (Not every opened
    // voice sounds: a held note can have decayed to nothing before the music
    // takes its voice, and then there is nothing to carry on.)
    const Dsp* d = ov.copy->dsp;
    for (int v = 0; v < 8 && !sound && unheard < 0 && ov.synced; v++) {
      if (d->channelMute & (1 << v)) continue;
      if (d->channel[v].gain > 0x40 && (d->channel[v].volumeL || d->channel[v].volumeR))
        unheard = f;
    }
  }

  printf("%ld ticks of '%s'\n", n, movie_path);
  printf("real driver: %ld effects dropped, %ld notes dropped, %ld voices stolen from effects\n",
         ov.effects_dropped, ov.notes_dropped, ov.voices_stolen);
  printf("overlay: %ld voices opened, %ld slots cleared for them, copy taken %ld time(s),\n"
         "         first heard at tick %ld\n",
         ov.voices_opened, ov.made_room, ov.resyncs, first_sound);

  if (diverged >= 0) { printf("FAIL: the machines part at tick %ld\n", diverged); failures++; }
  if (ov.resyncs != 1) { printf("FAIL: the copy was taken %ld times\n", ov.resyncs); failures++; }
  if (silent_after) {
    printf("FAIL: the overlay made a sound at tick %ld with nothing lost\n", silent_after - 1);
    failures++;
  }
  if (unheard >= 0) {
    printf("FAIL: an opened voice was sounding at tick %ld and nothing was added\n", unheard);
    failures++;
  }

  if (dir) {
    int16_t* added = (int16_t*)calloc((size_t)n * SAMPLES * 2, sizeof(int16_t));
    for (long i = 0; i < n * SAMPLES * 2; i++) added[i] = (int16_t)(mixed[i] - orig[i]);
    write_wav(dir, "original.wav", orig, n);
    write_wav(dir, "overlay.wav", mixed, n);
    write_wav(dir, "added.wav", added, n);
    free(added);
  }

  sfx_overlay_free(&ov);
  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
