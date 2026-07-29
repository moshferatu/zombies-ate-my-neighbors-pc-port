// Phase 0a headless driver: boot a SNES ROM through the vendored core, run a
// number of frames, and dump the resulting framebuffer to a PNG. This has no
// SDL / windowing dependency and exists to verify the core boots the ROM and
// renders correctly (the "reference oracle" foundation for later phases).
//
// Usage: zamn_headless <rom.sfc> <out.png> [frames] [-m movie] [--at f,f,...]
//                      [--pos first[,last[,step]]] [--records first[,last[,step]]]
//
// `-m` replays a movie file (`src/analysis/movie.c`) instead of holding no
// buttons, and `--at` writes one PNG per listed frame — `out.01860.png` and so
// on — rather than only the last. That pair is the movie-authoring loop: run a
// candidate script, look at where it actually got to, adjust. It is much faster
// than `zamn_trace --png` because it does not step instruction by instruction.
//
// `--pos` is the same loop without the eyeballing. A route through a level is
// aimed at coordinates — `zamn_assets actors` prints the objects' — and a PNG
// only says whether you arrived, in a 256x224 window, at the frames you thought
// to ask about. This prints where each player actually is, in the level's own
// coordinates, every `step` frames: read the numbers, adjust the turn frames,
// re-run. It reads the same two words `sprite_build_oam` draws from, so it is
// the position the game is using rather than an inference from the picture.
//
// `--records` is `--pos` for everything else on the board. `--pos` answers "did
// I get there"; when the answer is yes and nothing happened, the next question
// is what was actually *at* there — and that is the display list, the same 32
// records `actor_overlap_pass` tests pairwise, printed with the three fields
// that decide whether a pair collides: `ACTOR_X`, `ACTOR_Y` and
// `ACTOR_COLLIDE_ID`. A record with a zero id is in the picture and not in the
// game; a thing with no record at all was never spawned.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "snes.h"

#include "analysis/movie_apply.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// The core renders into a 512x480 (line-doubled) RGBX buffer, 2048-byte pitch.
#define FB_W 512
#define FB_H 480

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return NULL; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0) { fclose(f); fprintf(stderr, "error: empty file '%s'\n", path); return NULL; }
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
    fclose(f); free(buf); fprintf(stderr, "error: short read on '%s'\n", path); return NULL;
  }
  fclose(f);
  *out_len = (int)len;
  return buf;
}

// Pull the core's framebuffer out as a PNG. Shared by the `--at` snapshots and
// the final frame, which are the same operation at different times.
static bool write_png(Snes* snes, const char* path) {
  uint8_t* fb = (uint8_t*)malloc(FB_W * FB_H * 4);
  uint8_t* rgb = (uint8_t*)malloc(FB_W * FB_H * 3);
  if (!fb || !rgb) { free(fb); free(rgb); return false; }
  snes_setPixels(snes, fb);
  // Convert XRGB(=[B,G,R,X]) -> packed RGB for the PNG.
  for (int i = 0; i < FB_W * FB_H; i++) {
    rgb[i * 3 + 0] = fb[i * 4 + 2]; // R
    rgb[i * 3 + 1] = fb[i * 4 + 1]; // G
    rgb[i * 3 + 2] = fb[i * 4 + 0]; // B
  }
  bool ok = stbi_write_png(path, FB_W, FB_H, 3, rgb, FB_W * 3) != 0;
  free(fb);
  free(rgb);
  return ok;
}

#define MAX_SNAPSHOTS 64

// An actor's state is its thread's own 128-byte direct page, and the 24 of them
// tile `$7E:0100-$7E:0CFF` (`docs/wram-map.md`). Which page is a player's is not
// fixed — the scheduler allocates threads — so it is found rather than assumed,
// by the one field on that page that names a player and nothing else: `$64`,
// the base of that player's `player_inventory` array, which `$80:F88A` adds to
// and which is one of exactly two words, `$1CCC` and `$1CEC` (`$80:EAA4`).
#define THREAD_PAGE_FIRST 0x0100
#define THREAD_PAGE_STRIDE 0x80
#define THREAD_PAGE_COUNT 24
#define PLAYER_DP_INVENTORY 0x64
#define PLAYER_DP_RECORD 0x08
#define PLAYER_INVENTORY_BASE_0 0x1CCC
#define PLAYER_INVENTORY_BASE_1 0x1CEC

// The 32-record sprite display list `sprite_build_oam` walks, and the fields of
// a record this tool reads. `src/port/oam.h` documents the rest.
#define ACTOR_LIST_BASE 0x185E
#define ACTOR_LIST_STRIDE 0x14
#define ACTOR_LIST_COUNT 32
#define ACTOR_LIST_HEAD 0x1B5E
#define ACTOR_REC_FLAGS 0x00
#define ACTOR_REC_X 0x02
#define ACTOR_REC_Y 0x06
#define ACTOR_REC_THREAD 0x0C
#define ACTOR_REC_ID 0x0E
#define ACTOR_REC_NEXT 0x12

// The two score slots: one 32-bit BCD counter each, stride 4 (`src/port/wram.h`).
// They ride on the record dump because they are the cheapest answer to the
// question a record dump raises — a bonus object vanished, and who got it.
#define SCORE_BASE 0x1E72
#define SCORE_STRIDE 4

static uint16_t wram16(Snes* snes, uint16_t addr) {
  return (uint16_t)(snes->ram[addr] | (snes->ram[(uint16_t)(addr + 1)] << 8));
}

// Prints where player `n` is, or `--,--` when there is no such player on the
// board — before a level loads, and for player 2 in a one-player game. Nothing
// here is inferred from the picture: `$08` is the player's own display record
// and its X/Y are the level coordinates `zamn_assets actors` prints for the
// things you are trying to walk onto.
static void print_player_pos(Snes* snes, int n) {
  uint16_t want = n == 0 ? PLAYER_INVENTORY_BASE_0 : PLAYER_INVENTORY_BASE_1;
  for (int p = 0; p < THREAD_PAGE_COUNT; p++) {
    uint16_t pg = (uint16_t)(THREAD_PAGE_FIRST + p * THREAD_PAGE_STRIDE);
    if (wram16(snes, (uint16_t)(pg + PLAYER_DP_INVENTORY)) != want) continue;
    uint16_t rec = wram16(snes, (uint16_t)(pg + PLAYER_DP_RECORD));
    if (rec < ACTOR_LIST_BASE ||
        rec >= ACTOR_LIST_BASE + ACTOR_LIST_COUNT * ACTOR_LIST_STRIDE)
      break;
    printf("  %5u,%-5u", wram16(snes, (uint16_t)(rec + ACTOR_REC_X)),
           wram16(snes, (uint16_t)(rec + ACTOR_REC_Y)));
    return;
  }
  printf("     --,--  ");
}

// Prints the live display list at this frame: one line per record, in list
// order, which is the order the depth sort left it in. The list is walked
// through `ACTOR_NEXT` rather than over the 32 slots, so a record that appears
// here is one the game considers live.
static void print_records(Snes* snes, int frame) {
  printf("  frame %d — display list, score %04X%04X / %04X%04X\n", frame,
         wram16(snes, SCORE_BASE + 2), wram16(snes, SCORE_BASE),
         wram16(snes, SCORE_BASE + SCORE_STRIDE + 2),
         wram16(snes, SCORE_BASE + SCORE_STRIDE));
  printf("    addr   flags   x     y      id   thread\n");
  int n = 0;
  for (uint16_t rec = wram16(snes, ACTOR_LIST_HEAD);
       rec != 0 && n < ACTOR_LIST_COUNT; n++) {
    if (rec < ACTOR_LIST_BASE ||
        rec >= ACTOR_LIST_BASE + ACTOR_LIST_COUNT * ACTOR_LIST_STRIDE) {
      printf("    $%04X  (off the list)\n", rec);
      break;
    }
    printf("    $%04X  $%04X  %5u %5u   $%02X   $%02X\n", rec,
           wram16(snes, (uint16_t)(rec + ACTOR_REC_FLAGS)),
           wram16(snes, (uint16_t)(rec + ACTOR_REC_X)),
           wram16(snes, (uint16_t)(rec + ACTOR_REC_Y)),
           wram16(snes, (uint16_t)(rec + ACTOR_REC_ID)),
           wram16(snes, (uint16_t)(rec + ACTOR_REC_THREAD)));
    rec = wram16(snes, (uint16_t)(rec + ACTOR_REC_NEXT));
  }
  if (n == 0) printf("    (empty)\n");
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr,
            "usage: %s <rom.sfc> <out.png> [frames] [-m movie] [--at f,f,...]\n",
            argv[0]);
    return 2;
  }
  const char* rom_path = argv[1];
  const char* out_path = argv[2];
  int frames = 180;
  const char* movie_path = NULL;
  int snap_at[MAX_SNAPSHOTS];
  int snap_count = 0;
  int pos_first = -1, pos_last = -1, pos_step = 10;
  int rec_first = -1, rec_last = -1, rec_step = 10;

  for (int i = 3; i < argc; i++) {
    bool has_next = i + 1 < argc;
    if ((!strcmp(argv[i], "-m") || !strcmp(argv[i], "--movie")) && has_next) {
      movie_path = argv[++i];
    } else if (!strcmp(argv[i], "--at") && has_next) {
      for (const char* p = argv[++i]; *p && snap_count < MAX_SNAPSHOTS;) {
        snap_at[snap_count++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
      }
    } else if ((!strcmp(argv[i], "--pos") || !strcmp(argv[i], "--records")) &&
               has_next) {
      bool is_pos = !strcmp(argv[i], "--pos");
      int v[3] = {0, -1, 10};
      int n = 0;
      for (const char* p = argv[++i]; *p && n < 3;) {
        v[n++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
      }
      if (is_pos) {
        pos_first = v[0];
        pos_last = v[1];
        pos_step = v[2] > 0 ? v[2] : 1;
      } else {
        rec_first = v[0];
        rec_last = v[1];
        rec_step = v[2] > 0 ? v[2] : 1;
      }
    } else if (argv[i][0] != '-') {
      frames = atoi(argv[i]);
    } else {
      fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
      return 2;
    }
  }

  int rom_len = 0;
  uint8_t* rom = read_file(rom_path, &rom_len);
  if (!rom) return 1;
  printf("Loaded ROM '%s' (%d bytes)\n", rom_path, rom_len);

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    free(rom);
    snes_free(snes);
    return 1;
  }
  // XRGB layout: framebuffer bytes per pixel are [B, G, R, X].
  snes_setPixelFormat(snes, pixelFormatXRGB);
  snes_reset(snes, true);

  Movie movie;
  bool have_movie = false;
  if (movie_path) {
    if (!movie_load(&movie, movie_path)) {
      fprintf(stderr, "error: cannot load movie '%s'\n", movie_path);
      free(rom); snes_free(snes);
      return 1;
    }
    have_movie = true;
    printf("Replaying '%s'%s\n", movie_path,
           movie_uses_port(&movie, 1) ? " (two controllers)" : "");
  }

  printf("Running %d frames...\n", frames);
  if (pos_first >= 0) {
    if (pos_last < 0) pos_last = frames;
    printf("  frame        p1 x,y        p2 x,y\n");
  }
  if (rec_first >= 0 && rec_last < 0) rec_last = frames;
  int next_snap = 0;
  for (int i = 0; i < frames; i++) {
    if (have_movie) {
      movie_apply(&movie, snes, i);
    }
    snes_runFrame(snes);
    // `--at` frames are requested in whatever order they were typed, but they
    // are almost always ascending; walking a cursor keeps the common case free
    // and a stray out-of-order one simply does not fire.
    while (next_snap < snap_count && snap_at[next_snap] == i + 1) {
      char path[512];
      snprintf(path, sizeof path, "%.*s.%05d.png",
               (int)(strlen(out_path) - (strlen(out_path) > 4 &&
                                         !strcmp(out_path + strlen(out_path) - 4, ".png")
                                             ? 4 : 0)),
               out_path, i + 1);
      if (write_png(snes, path)) printf("  frame %d -> %s\n", i + 1, path);
      next_snap++;
    }
    if (pos_first >= 0 && i + 1 >= pos_first && i + 1 <= pos_last &&
        (i + 1 - pos_first) % pos_step == 0) {
      printf("  %5d", i + 1);
      print_player_pos(snes, 0);
      print_player_pos(snes, 1);
      printf("\n");
    }
    if (rec_first >= 0 && i + 1 >= rec_first && i + 1 <= rec_last &&
        (i + 1 - rec_first) % rec_step == 0) {
      print_records(snes, i + 1);
    }
  }
  printf("Ran %u frames (core reports %u), %llu cpu cycles\n",
         frames, snes->frames, (unsigned long long)snes->cycles);

  if (!write_png(snes, out_path)) {
    fprintf(stderr, "error: failed to write '%s'\n", out_path);
    if (have_movie) movie_free(&movie);
    free(rom); snes_free(snes);
    return 1;
  }
  printf("Wrote %s (%dx%d)\n", out_path, FB_W, FB_H);

  if (have_movie) movie_free(&movie);
  free(rom);
  snes_free(snes);
  return 0;
}
