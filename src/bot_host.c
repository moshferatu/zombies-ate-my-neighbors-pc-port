// The emulator, held open for a program that plays the game.
//
// `tools/bot.py` plays levels to their end: it reads where the player, the
// neighbours and the exit are, plans a way through the level's own tiles,
// presses the pad a few frames at a time, and backs up when something goes
// wrong. Every one of those steps needs the machine as it stands, and
// `zamn_headless` starts from frame 0 each time it is asked anything. A level
// takes thousands of steps, so this keeps one machine and answers questions
// about it on stdin instead.
//
// The ROM plays here, not the port: what the bot makes is a movie, and a movie
// is only worth having if it is what the game does. `zamn.exe` and
// `zamn_cosim verify` replay it afterwards, and that is where the port is
// tested.
//
//   zamn_bot_host <rom.sfc> [--level n] [-m prefix.zmv --until frame]
//                 [--render] [poke and cheat flags as zamn_headless takes them]
//
// One command a line, and one line back for each, beginning `ok` or `error`:
//
//   run <frames> <pad1> [<pad2>]   hold these (hex, `BTN_*` bits) for that many
//                                  frames; answers `ok <frame>`
//   save <slot>                    keep the machine in memory, 0..255
//   load <slot>                    go back to it; the inputs after it are dropped
//   peek <addr> <len>              WRAM bytes as hex, `addr` 0..1FFFF
//   frame                          `ok <frame>`
//   write <path> [comment]         the inputs from frame 0 to now, as a .zmv
//   png <path>                     the picture, with `--render` only
//   quit
//
// Pictures are not drawn unless `--render` asks for them. Nothing the game does
// reads back what the picture chip drew, so the run is the same without it and
// faster by the time drawing took; `tools/bot.py` replays what it wrote under
// `zamn_headless` to make sure.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "snes.h"

#include "analysis/movie_apply.h"
#include "poke.h"
#include "cheats.h"
#include "levelstart.h"
#include "video/console.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define MAX_FRAMES 1000000
#define SLOTS 256

typedef struct {
  uint8_t* data;
  int size;
  int frame;
} Slot;

static uint16_t* pad_log[MOVIE_PORTS];  // what each pad held on each frame
static Slot slots[SLOTS];

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "error: cannot open '%s'\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  *out_len = (int)len;
  return buf;
}

static void no_line(void* user, int line) {
  (void)user;
  (void)line;
}

// One frame with these pads held, logged so that `write` can say so.
static void step(Snes* snes, PokeList* pokes, Cheats* cheats, int* frame,
                 const uint16_t pad[MOVIE_PORTS]) {
  for (int p = 0; p < MOVIE_PORTS; p++) {
    for (int b = 0; b < 12; b++) snes_setButtonState(snes, p + 1, b, (pad[p] >> b) & 1);
    if (*frame < MAX_FRAMES) pad_log[p][*frame] = pad[p];
  }
  poke_apply(pokes, snes->ram, *frame);
  cheats_tick(cheats, snes->ram, snes->cart->rom);
  snes_runFrame(snes);
  (*frame)++;
}

// Each pad's changes, in the order `src/analysis/movie.h` reads them.
static bool write_movie(const char* path, const char* comment, int frames) {
  FILE* f = fopen(path, "w");
  if (!f) return false;
  if (comment && *comment) fprintf(f, "# %s\n", comment);
  for (int p = 0; p < MOVIE_PORTS; p++) {
    int held = -1;
    for (int i = 0; i < frames && i < MAX_FRAMES; i++) {
      if (pad_log[p][i] == held) continue;
      if (p == 1 && held < 0 && pad_log[p][i] == 0) {
        held = 0;  // a pad never pressed needs no lines
        continue;
      }
      held = pad_log[p][i];
      char names[128];
      movie_format((uint16_t)held, names, sizeof names);
      fprintf(f, "%s%d   %s\n", p ? "2:" : "", i, held ? names : "-");
    }
  }
  // The last frame, so that a replay knows how long the movie is meant to be.
  fprintf(f, "%d   -\n", frames);
  fclose(f);
  return true;
}

static bool write_png(Snes* snes, const char* path) {
  static uint8_t fb[512 * 480 * 4], rgb[512 * 480 * 3];
  video_put_pixels(video_chip_of(snes), fb);
  for (int i = 0; i < 512 * 480; i++) {  // XRGB in memory is B, G, R, X
    rgb[i * 3] = fb[i * 4 + 2];
    rgb[i * 3 + 1] = fb[i * 4 + 1];
    rgb[i * 3 + 2] = fb[i * 4];
  }
  return stbi_write_png(path, 512, 480, 3, rgb, 512 * 3) != 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "usage: %s <rom.sfc> [--level n] [-m prefix.zmv --until frame] [--render]\n"
            "       [--poke frame[+|-last]:addr=value[.b]]... [cheat flags]\n",
            argv[0]);
    return 2;
  }
  const char* movie_path = NULL;
  int until = 0;
  int start_level = -1;
  bool render = false;
  PokeList pokes = {{{0}}, 0};
  Cheats cheats;
  cheats_init(&cheats);
  for (int i = 2; i < argc; i++) {
    bool has_next = i + 1 < argc;
    if ((!strcmp(argv[i], "-m") || !strcmp(argv[i], "--movie")) && has_next) {
      movie_path = argv[++i];
    } else if (!strcmp(argv[i], "--until") && has_next) {
      until = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--render")) {
      render = true;
    } else if (!strcmp(argv[i], "--poke") && has_next) {
      if (!poke_parse(&pokes, argv[++i])) return 2;
    } else if (!strcmp(argv[i], "--level") && has_next) {
      start_level = atoi(argv[++i]);
      if (start_level < LEVEL_FIRST || start_level > LEVEL_LAST) {
        fprintf(stderr, "error: --level wants %d..%d\n", LEVEL_FIRST, LEVEL_LAST);
        return 2;
      }
    } else if (cheats_flag(&cheats, argv[i])) {
    } else {
      fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
      return 2;
    }
  }

  int rom_len = 0;
  uint8_t* rom = read_file(argv[1], &rom_len);
  if (!rom) return 1;
  Snes* snes = video_snes_init(VIDEO_EMULATED, false);
  if (!snes_loadRom(snes, rom, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    return 1;
  }
  video_set_pixel_format(video_chip_of(snes), VIDEO_PIXELS_XRGB);
  if (!render) {
    SnesVideo v = snes->video;
    v.runLine = no_line;
    snes_setVideo(snes, &v);
  }
  if (!cheats_install(&cheats, snes->cart->rom, (size_t)snes->cart->romSize)) {
    fprintf(stderr, "error: this is not a cartridge the cheats know how to change\n");
    return 1;
  }
  if (start_level >= 0 && !start_at_level(snes, start_level)) {
    fprintf(stderr, "error: --level: this is not a cartridge it can change\n");
    return 1;
  }
  snes_reset(snes, true);
  for (int p = 0; p < MOVIE_PORTS; p++)
    pad_log[p] = (uint16_t*)calloc(MAX_FRAMES, sizeof(uint16_t));

  int frame = 0;
  if (movie_path) {
    Movie movie;
    if (!movie_load(&movie, movie_path)) {
      fprintf(stderr, "error: cannot load movie '%s'\n", movie_path);
      return 1;
    }
    while (frame < until) {
      MovieInput in = movie_state(&movie, frame);
      step(snes, &pokes, &cheats, &frame, in.port);
    }
    movie_free(&movie);
  }
  printf("ok %d\n", frame);
  fflush(stdout);

  char line[4096];
  while (fgets(line, sizeof line, stdin)) {
    char cmd[16] = {0};
    if (sscanf(line, "%15s", cmd) != 1) continue;
    const char* rest = line + strlen(cmd);
    while (*rest == ' ') rest++;
    if (!strcmp(cmd, "run")) {
      int n = 0;
      unsigned a = 0, b = 0;
      if (sscanf(rest, "%d %x %x", &n, &a, &b) < 2) {
        printf("error: run <frames> <pad1> [<pad2>]\n");
      } else {
        const uint16_t pad[MOVIE_PORTS] = {(uint16_t)a, (uint16_t)b};
        for (int i = 0; i < n; i++) step(snes, &pokes, &cheats, &frame, pad);
        printf("ok %d\n", frame);
      }
    } else if (!strcmp(cmd, "save") || !strcmp(cmd, "load")) {
      int k = atoi(rest);
      if (k < 0 || k >= SLOTS) {
        printf("error: slot 0..%d\n", SLOTS - 1);
      } else if (cmd[0] == 's') {
        Slot* s = &slots[k];
        const int size = snes_saveState(snes, NULL);
        if (s->size != size) {
          free(s->data);
          s->data = (uint8_t*)malloc((size_t)size);
          s->size = size;
        }
        snes_saveState(snes, s->data);
        s->frame = frame;
        printf("ok %d\n", frame);
      } else if (!slots[k].data) {
        printf("error: slot %d is empty\n", k);
      } else {
        snes_loadState(snes, slots[k].data, slots[k].size);
        frame = slots[k].frame;
        printf("ok %d\n", frame);
      }
    } else if (!strcmp(cmd, "peek")) {
      unsigned addr = 0, len = 0;
      if (sscanf(rest, "%x %x", &addr, &len) != 2 || addr + len > 0x20000) {
        printf("error: peek <addr> <len>, inside 0..1FFFF\n");
      } else {
        static char hex[0x40001];
        static const char digits[] = "0123456789abcdef";
        for (unsigned i = 0; i < len; i++) {
          hex[i * 2] = digits[snes->ram[addr + i] >> 4];
          hex[i * 2 + 1] = digits[snes->ram[addr + i] & 15];
        }
        hex[len * 2] = 0;
        printf("ok %s\n", hex);
      }
    } else if (!strcmp(cmd, "frame")) {
      printf("ok %d\n", frame);
    } else if (!strcmp(cmd, "write")) {
      char path[1024] = {0};
      if (sscanf(rest, "%1023s", path) != 1) {
        printf("error: write <path> [comment]\n");
      } else {
        const char* comment = rest + strlen(path);
        while (*comment == ' ') comment++;
        char text[1024];
        snprintf(text, sizeof text, "%s", comment);
        text[strcspn(text, "\r\n")] = 0;
        printf(write_movie(path, text, frame) ? "ok %d\n" : "error: cannot write\n",
               frame);
      }
    } else if (!strcmp(cmd, "png")) {
      char path[1024] = {0};
      if (!render) {
        printf("error: png wants --render\n");
      } else if (sscanf(rest, "%1023s", path) != 1 || !write_png(snes, path)) {
        printf("error: png <path>\n");
      } else {
        printf("ok\n");
      }
    } else if (!strcmp(cmd, "quit")) {
      printf("ok\n");
      break;
    } else {
      printf("error: unknown command '%s'\n", cmd);
    }
    fflush(stdout);
  }
  return 0;
}
