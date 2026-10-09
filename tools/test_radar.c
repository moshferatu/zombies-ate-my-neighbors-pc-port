// `src/radar.h` against a movie: the game is run twice, once with the
// console's radar and once with every square drawn, and on every tick
//
//   * the two machines' work RAM is identical: the squares are only a picture;
//   * wherever the console's marker is, one of the squares is, to a pixel;
//   * the two pictures differ only within a sprite's width of a square or of
//     the console's marker.
//
// It also says how many squares there were: the console shows one at a time,
// so a radar with five neighbours in reach should be five squares on nearly
// every tick it is up.
//
//   zamn_test_radar [rom.sfc] [movie.zmv] [frames] [--widescreen 16:9]
//                   [--poke frame[+]:addr=value]...

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "snes.h"

#include "analysis/movie_apply.h"
#include "cosim/cosim.h"
#include "poke.h"
#include "scale.h"
#include "widescreen.h"

#define FB_H 480

static int failures;
// From the run with every square: squares covered in part by another sprite,
// and pixels of theirs that were not drawn, and the ticks that were.
static long overlaps, hidden, hidden_ticks, hidden_first = -1;
static long frames_static;
static PokeList pokes;

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

static uint64_t hash(const uint8_t* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

// Is a radar thread live in this memory?
static bool radar_live(const uint8_t* mem) {
  for (int s = 0; s < WRAM_THREAD_SLOTS; s++)
    if ((radar_r16(mem, W_THREAD_WAIT + s * 2u) & 0x8000) &&
        radar_r16(mem, W_THREAD_ENTRY + s * 2u) == RADAR_THREAD_ENTRY)
      return true;
  return false;
}

typedef struct {
  int x, y;
} Spot;

#define SPOTS_MAX 64

// One tick of one run: what was in RAM, and while a radar is up the picture
// and where the squares (or the marker) were.
typedef struct {
  uint64_t ram;
  bool radar;
  uint8_t* fb;
  int width, extra_left;
  int spots;
  Spot spot[SPOTS_MAX];
} Tick;

// The squares' tile and attributes, as the marker's metasprite gives them
// once the pass has put it in the cache: the entries in OAM with this word.
static void find_spots(const Snes* snes, uint16_t word, Tick* t) {
  const VideoRegisters* reg = &video_chip_of(snes)->registers;
  t->spots = 0;
  for (int e = 0; e < OAM_ENTRIES && t->spots < SPOTS_MAX; e++) {
    const uint16_t lo = reg->oam[e * 2];
    if ((lo >> 8) >= 0xe0 && (lo >> 8) <= 0xf0) continue;
    if (reg->oam[e * 2 + 1] != word) continue;
    const int x = (lo & 0xff) | ((reg->high_oam[e >> 2] >> ((e & 3) * 2)) & 1) << 8;
    t->spot[t->spots].x = x >= 256 ? x - 512 : x;
    t->spot[t->spots].y = lo >> 8;
    t->spots++;
  }
}

// The marker's OAM word, from the first tick of the flashing run that shows
// it: the entry at the marker record's origin plus its one piece.
static bool marker_word(const Snes* snes, const Rom* rom, const uint8_t* mem, uint16_t* word) {
  SpriteMeta meta;
  if (sprite_meta_read(rom, ((uint32_t)RADAR_META_BANK << 16) | RADAR_META, &meta) != SPRITE_OK) return false;
  for (int rec = W_ACTOR_SLOTS; rec < W_ACTOR_SLOTS + ACTOR_SLOT_COUNT * ACTOR_SLOT_STRIDE; rec += ACTOR_SLOT_STRIDE) {
    if (radar_r16(mem, rec + ACTOR_META) != RADAR_META || !(radar_r16(mem, rec) & ACTOR_DRAW)) continue;
    const int x = ((int16_t)radar_r16(mem, rec + ACTOR_X) + meta.pieces[0].x) & 0x1ff;
    const int y = ((int16_t)radar_r16(mem, rec + ACTOR_Y) + meta.pieces[0].y) & 0xff;
    for (int e = 0; e < OAM_ENTRIES; e++) {
      const VideoRegisters* reg = &video_chip_of(snes)->registers;
      const uint16_t lo = reg->oam[e * 2];
      const int ex = (lo & 0xff) | ((reg->high_oam[e >> 2] >> ((e & 3) * 2)) & 1) << 8;
      if (ex == x && (lo >> 8) == y) { *word = reg->oam[e * 2 + 1]; return true; }
    }
  }
  return false;
}

// Sprite `s` put on line `y` and otherwise left as it is, in the OAM the
// registers keep and in the PPU's own, which it draws a frame again from.
static void sprite_to_line(VideoChip* chip, int s, int y) {
  const VideoRegisters* reg = &chip->registers;
  const int extra = reg->high_oam[s >> 2] >> ((s & 3) * 2);
  video_set_sprite(chip, s, (reg->oam[s * 2] & 0xff) | (extra & 1) << 8, y, reg->oam[s * 2 + 1],
                   (extra & 2) != 0);
}

// Whether the squares are drawn over everything else, on a picture `fb` just
// taken: the frame is drawn again with the squares alone and with no sprites
// at all, which says which pixels are the squares', and each of those must
// be the squares' in `fb` too. `*overlaps` counts squares some other sprite
// on screen covers part of. Returns the pixels that were not the squares'.
//
// The PPU draws the frame again (`ppu_renderFrame`), from OAM as it is left
// here and each line's scrolls as they were noted.
static long squares_hidden(Snes* snes, const Radar* r, const uint8_t* fb, int width, long* overlaps) {
  VideoChip* chip = video_chip_of(snes);
  const VideoRegisters* reg = &chip->registers;
  const VideoFrame* noted = &chip->frame;
  VideoState vs;
  video_chip_state(chip, &vs);
  // Only a frame that can be drawn again from its end.
  if (noted->mid_frame_write || reg->mode == 7) return 0;
  // The entries the radar wrote, less the marker it parked.
  bool square[OAM_ENTRIES] = {false}, any = false;
  for (int i = 0; i < r->written; i++) {
    const int s = r->slot[i], y = reg->oam[s * 2] >> 8;
    if (y < 0xe0 || y > 0xf0) square[s] = any = true;
  }
  if (!any) return 0;
  frames_static++;
  const size_t bytes = (size_t)width * 4 * FB_H;
  uint8_t* alone = (uint8_t*)malloc(bytes);
  uint8_t* none = (uint8_t*)malloc(bytes);
  uint16_t oam[OAM_ENTRIES * 2];
  memcpy(oam, reg->oam, sizeof oam);
  for (int s = 0; s < OAM_ENTRIES; s++) {
    if (!square[s]) continue;
    const int sx = video_sprite_x(&vs, s), sy = reg->oam[s * 2] >> 8;
    const int size = video_obj_size(&vs.obj, s);
    for (int e = 0; e < OAM_ENTRIES; e++) {
      const int ey = reg->oam[e * 2] >> 8;
      if (square[e] || (ey >= 0xe0 && ey <= 0xf0)) continue;
      const int ex = video_sprite_x(&vs, e), esize = video_obj_size(&vs.obj, e);
      if (ex < sx + size && sx < ex + esize && ey < sy + size && sy < ey + esize) { (*overlaps)++; break; }
    }
  }
  for (int pass = 0; pass < 2; pass++) {
    for (int s = 0; s < OAM_ENTRIES; s++)
      if (pass == 1 || !square[s]) sprite_to_line(chip, s, 0xe0);
    ppu_renderFrame(snes->ppu, noted->line_hscroll, noted->line_vscroll);
    video_put_pixels(chip, pass ? none : alone);
  }
  for (int s = 0; s < OAM_ENTRIES; s++) sprite_to_line(chip, s, oam[s * 2] >> 8);
  ppu_renderFrame(snes->ppu, noted->line_hscroll, noted->line_vscroll);
  long covered = 0;
  for (size_t o = 0; o < bytes; o += 4)
    if (memcmp(alone + o, none + o, 4) && memcmp(alone + o, fb + o, 4)) covered++;
  free(alone);
  free(none);
  return covered;
}

static Tick* run(const uint8_t* rom, int rom_len, const char* movie_path, long n, int margin,
                 bool steady, Radar* out, uint16_t* word, bool* have_word) {
  Movie movie;
  if (!movie_load(&movie, movie_path)) return NULL;
  // The frontend reads the chip's registers from `src/video`, which keeps
  // them beside the PPU's. The PPU draws the picture here.
  Snes* snes = video_snes_init(VIDEO_EMULATED, false);
  if (!snes_loadRom(snes, rom, rom_len)) return NULL;
  static Widescreen ws;
  video_set_margins(video_chip_of(snes), margin, margin);
  widescreen_install(snes, &ws, rom, rom_len, margin);
  ws.radar.steady = steady;
  Cosim cosim;
  cosim_init(&cosim, snes, COSIM_NATIVE);
  cosim_enable_all(&cosim);
  cosim_watch(&cosim, WS_PASS_DONE_AT, widescreen_pass_done, &ws);
  cosim_watch(&cosim, WS_OAM_SENT_AT, widescreen_oam_sent, &ws);

  Tick* ticks = (Tick*)calloc((size_t)n, sizeof(Tick));
  for (long f = 0; f < n; f++) {
    movie_apply(&movie, snes, (int)f);
    poke_apply(&pokes, snes->ram, (int)f);
    cosim_frame(&cosim);
    Tick* t = &ticks[f];
    t->ram = hash(snes->ram, 0x20000);
    t->radar = radar_live(ws_sprite_mem(&ws));
    if (!t->radar) continue;
    if (!*have_word) *have_word = marker_word(snes, &ws.rom, ws_sprite_mem(&ws), word);
    if (*have_word) find_spots(snes, *word, t);
    t->width = video_output_width(video_chip_of(snes));
    t->extra_left = video_chip_of(snes)->picture.extra_left;
    t->fb = (uint8_t*)malloc((size_t)t->width * 4 * FB_H);
    video_put_pixels(video_chip_of(snes), t->fb);
    if (steady) {
      const long h = squares_hidden(snes, &ws.radar, t->fb, t->width, &overlaps);
      if (h && hidden_ticks++ == 0) hidden_first = f;
      hidden += h;
    }
  }
  *out = ws.radar;
  cosim_free(&cosim);
  video_snes_free(snes);
  return ticks;
}

// Within a sprite's width of any spot of either run, in the console's
// columns or, for a picture widened with the panel pinned to its edges, in
// the columns the panel's sprites are moved to.
static bool near_spot(const Tick* a, const Tick* b, int gx, int gy) {
  const Tick* both[2] = {a, b};
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < both[k]->spots; i++) {
      const Spot* s = &both[k]->spot[i];
      if (gy < s->y - 1 || gy > s->y + 17) continue;
      // Pinned to the panel, a sprite in the console's left half keeps its
      // distance from the picture's left edge, one in the right half from its
      // right edge.
      const int extra_right = a->width / 2 - 256 - a->extra_left;
      const int pinned = s->x < 128 ? s->x - a->extra_left : s->x + extra_right;
      if ((gx >= s->x - 1 && gx <= s->x + 17) || (gx >= pinned - 1 && gx <= pinned + 17)) return true;
    }
  return false;
}

int main(int argc, char** argv) {
  const char* rom_path = "Zombies Ate My Neighbors.sfc";
  const char* movie_path = "movies/level1-map.zmv";
  long frames = 0;
  WideMode wide = WIDE_OFF;
  for (int i = 1, at = 0; i < argc; i++) {
    if (!strcmp(argv[i], "--poke") && i + 1 < argc) {
      if (!poke_parse(&pokes, argv[++i])) return 2;
    } else if (!strcmp(argv[i], "--widescreen") && i + 1 < argc) {
      if (!wide_parse(argv[++i], &wide)) {
        printf("--widescreen wants off, 16:9, 16:10 or 21:9\n");
        return 2;
      }
    } else if (at == 0) {
      rom_path = argv[i], at++;
    } else if (at == 1) {
      movie_path = argv[i], at++;
    } else {
      frames = atol(argv[i]);
    }
  }

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) { printf("SKIP: no cartridge at '%s'\n", rom_path); return 0; }
  Movie movie;
  if (!movie_load(&movie, movie_path)) { printf("FAIL: cannot load '%s'\n", movie_path); return 1; }
  long last = 0;
  for (int p = 0; p < MOVIE_PORTS; p++)
    if (movie.track[p].count && movie.track[p].events[movie.track[p].count - 1].frame > last)
      last = movie.track[p].events[movie.track[p].count - 1].frame;
  const long n = frames > 0 ? frames : last + 600;

  uint16_t word = 0;
  bool have_word = false;
  Radar flashing, steady;
  Tick* a = run(rom, rom_len, movie_path, n, wide_margin(wide), false, &flashing, &word, &have_word);
  if (!a) { printf("FAIL: the core will not run '%s'\n", rom_path); return 1; }
  Tick* b = run(rom, rom_len, movie_path, n, wide_margin(wide), true, &steady, &word, &have_word);
  if (!b) { printf("FAIL: the core will not run '%s'\n", rom_path); return 1; }

  long parted = -1, radar_ticks = 0, shown_ticks = 0, missed = 0, missed_at = -1;
  long stray = 0, stray_at = -1, counts[SPOTS_MAX + 1] = {0};
  for (long f = 0; f < n; f++) {
    if (parted < 0 && a[f].ram != b[f].ram) parted = f;
    if (!a[f].radar || !b[f].radar) continue;
    radar_ticks++;
    counts[b[f].spots]++;
    // Every place the console's marker is, a square is.
    for (int i = 0; i < a[f].spots; i++) {
      shown_ticks++;
      bool found = false;
      for (int j = 0; j < b[f].spots && !found; j++)
        found = a[f].spot[i].x == b[f].spot[j].x && a[f].spot[i].y == b[f].spot[j].y;
      if (!found && missed++ == 0) missed_at = f;
    }
    // And nothing else in the picture changed.
    if (a[f].width != b[f].width) { if (stray++ == 0) stray_at = f; continue; }
    const int w = a[f].width;
    for (int row = 16; row < FB_H - 16; row++)
      for (int col = 0; col < w; col++) {
        const size_t o = ((size_t)row * w + col) * 4;
        if (!memcmp(a[f].fb + o, b[f].fb + o, 4)) continue;
        if (near_spot(&a[f], &b[f], col / 2 - a[f].extra_left, (row - 16) / 2)) continue;
        if (stray++ == 0) stray_at = f;
      }
  }

  printf("%ld ticks of '%s', %s\n", n, movie_path, wide == WIDE_OFF ? "4:3" : "widened");
  printf("radar up on %ld ticks; the console's marker on %ld of them\n", radar_ticks, shown_ticks);
  printf("squares drawn: %ld on %ld frames, at most %d at once\n", steady.squares, steady.frames, steady.most);
  printf("frames the squares were looked for on top: %ld\n", frames_static);
  printf("squares with another sprite over part of them: %ld\n", overlaps);
  printf("squares per tick:");
  for (int k = 0; k <= SPOTS_MAX; k++)
    if (counts[k]) printf(" %d x%ld", k, counts[k]);
  printf("\n");

  if (!have_word) { printf("FAIL: the movie never shows the radar's marker\n"); failures++; }
  if (parted >= 0) { printf("FAIL: the machines part at tick %ld\n", parted); failures++; }
  if (missed) {
    printf("FAIL: the console's marker was where no square was %ld times, first at tick %ld\n", missed, missed_at);
    failures++;
  }
  if (stray) {
    printf("FAIL: %ld pixels changed away from the squares, first at tick %ld\n", stray, stray_at);
    failures++;
  }
  if (hidden) {
    printf("FAIL: %ld pixels of squares were drawn over on %ld ticks, first at tick %ld\n", hidden,
           hidden_ticks, hidden_first);
    failures++;
  }
  if (flashing.squares) { printf("FAIL: the console's radar drew %ld squares\n", flashing.squares); failures++; }

  for (long f = 0; f < n; f++) { free(a[f].fb); free(b[f].fb); }
  free(a);
  free(b);
  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
