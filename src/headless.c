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
//
// `--watch <addr>[,first[,last[,step]]]` is the third of these, and it exists
// because the two above answer questions about *things* and some questions are
// about one word. It prints `$7E:addr` as a word, and only when the value
// changes, so a run over a whole movie costs a handful of lines.
//
// What asked for it: level 25's boss takes 47 damaging hits from a health word
// seeded once to 70, and does not die. Every one of those calls passes the
// per-call diff, so the port and the ROM agree about all of it — which means the
// disagreement is between the ROM and somebody's *reading* of the ROM, and the
// only way to tell those apart is to look at the word. Neither `--pos` nor
// `--records` can: it is not a position and it is not in the display list.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ppu.h"
#include "snes.h"

#include "analysis/movie_apply.h"
#include "poke.h"
#include "cheats.h"
#include "scale.h"
#include "twinstick.h"
#include "widescreen.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// The core renders into a 512x480 (line-doubled) RGBX buffer, 2048-byte pitch —
// unless the picture has been widened, in which case the width and the pitch
// grow with it and everything here reads `fb_w` instead. `FB_W` stays as the
// widest it can be, because the buffers are allocated once and the option is
// parsed after them.
#define FB_W (PPU_MAX_WIDTH * 2)
#define FB_H 480
static int fb_w = 512;

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
  uint8_t* fb = (uint8_t*)malloc((size_t)FB_W * FB_H * 4);
  uint8_t* rgb = (uint8_t*)malloc((size_t)FB_W * FB_H * 3);
  if (!fb || !rgb) { free(fb); free(rgb); return false; }
  snes_setPixels(snes, fb);
  // Convert XRGB(=[B,G,R,X]) -> packed RGB for the PNG. The core packs its rows
  // at the live width, so this is a straight run of `fb_w * FB_H` pixels.
  for (int i = 0; i < fb_w * FB_H; i++) {
    rgb[i * 3 + 0] = fb[i * 4 + 2]; // R
    rgb[i * 3 + 1] = fb[i * 4 + 1]; // G
    rgb[i * 3 + 2] = fb[i * 4 + 0]; // B
  }
  bool ok = stbi_write_png(path, fb_w, FB_H, 3, rgb, fb_w * 3) != 0;
  free(fb);
  free(rgb);
  return ok;
}

#define MAX_SNAPSHOTS 64
#define MAX_WATCH 32  // one per thread page, which is what the option is for

// `--aim frame[+]:dirs` — the right stick, for the stub `src/twinstick.h`
// installs, which is the half of twin-stick aiming that runs on the 65816 and
// so the half this tool can show. `dirs` is any of U, D, L and R, or `-` for
// centred; `+` holds it from that frame on, and without it the stick is out
// for that one frame. Port 1 only, and given in ascending order. It presses
// `Y` for as long as it is out, as the frontend's stick does, on top of
// whatever the movie holds. Asking for one installs the patch; `--twin-stick`
// installs it without asking for an aim.
#define MAX_AIMS 32
typedef struct {
  int frame;
  bool hold;
  uint16_t dpad;
} AimSpec;

static bool aim_parse(const char* s, AimSpec* out) {
  char* end;
  const long f = strtol(s, &end, 10);
  if (end == s || f < 0) return false;
  out->frame = (int)f;
  out->hold = false;
  if (*end == '+') {
    out->hold = true;
    end++;
  }
  if (*end++ != ':') return false;
  out->dpad = 0;
  if (!strcmp(end, "-")) return true;
  if (!*end) return false;
  for (; *end; end++) {
    switch (*end | 0x20) {
      case 'u': out->dpad |= (uint16_t)(1u << BTN_UP); break;
      case 'd': out->dpad |= (uint16_t)(1u << BTN_DOWN); break;
      case 'l': out->dpad |= (uint16_t)(1u << BTN_LEFT); break;
      case 'r': out->dpad |= (uint16_t)(1u << BTN_RIGHT); break;
      default: return false;
    }
  }
  return true;
}

// What the stick holds on `frame`: the latest spec at or before it, if that
// one holds or is for this very frame; centred otherwise.
static uint16_t aim_at(const AimSpec* a, int n, int frame) {
  int best = -1;
  for (int i = 0; i < n; i++)
    if (a[i].frame <= frame) best = i;
  if (best < 0) return 0;
  return (a[best].hold || a[best].frame == frame) ? a[best].dpad : 0;
}

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

// The collision handler a thread has installed, split across two arrays that
// `thread_call_handler` reads together: `$1300,X` is the low word and `$1330,X`
// the bank, indexed by the *byte* offset a record carries in `ACTOR_REC_THREAD`.
// That pair is what turns the display list from a list of things into a list of
// **routines**: every handler in `src/port/collide.c` is named by its entry
// address, and this is the only place in the running game that says which thing
// on the board belongs to which of them.
#define W_THREAD_HANDLER 0x1300
#define W_THREAD_HANDLER_BANK 0x1330

// And the page that handler runs on: `$80:84A3  LDA $8082DE,X : TCD`, indexed by
// the same byte offset. **It is a table and not a formula**, which is worth
// stating because the obvious formula is wrong and looks right: the first
// thirteen slots are `$0100` then `$0280` in steps of `$80`, and the twelve
// after them start again at `$0180` in steps of `$0100`. Computing
// `$0100 + slot/2 * $80` agrees with the table for slot `$00` and for nothing
// else that matters -- it puts the level-25 boss on `$0A80` when `$80:82DE`
// says `$0800`, so a `--watch` aimed at its health reads a word of somebody
// else's page and prints a steady zero.
#define THREAD_DP_TABLE_BANK 0x80
#define THREAD_DP_TABLE_ADDR 0x82de

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
// LoROM: bank `$80` is the first half of the file, mirrored from `$8000`.
//
// Named for the file rather than for the cartridge because `assets/rom.h`
// already has a `rom_word`, which takes a decoded `Rom` and a 24-bit address.
// This one predates that and reads the bytes as they came off disk; two
// functions of the same name and different signatures compiled quietly until
// this file came to include a header that declares the other.
static uint16_t rom_file_word(const uint8_t* rom, int rom_len, uint8_t bank,
                              uint16_t addr) {
  int o = ((bank & 0x7f) << 15) | (addr - 0x8000);
  if (o < 0 || o + 1 >= rom_len) return 0;
  return (uint16_t)(rom[o] | (rom[o + 1] << 8));
}

static void print_records(Snes* snes, const uint8_t* rom, int rom_len,
                          int frame) {
  printf("  frame %d — display list, score %04X%04X / %04X%04X\n", frame,
         wram16(snes, SCORE_BASE + 2), wram16(snes, SCORE_BASE),
         wram16(snes, SCORE_BASE + SCORE_STRIDE + 2),
         wram16(snes, SCORE_BASE + SCORE_STRIDE));
  printf("    addr   flags   x     y      id   thread  page   handler\n");
  int n = 0;
  for (uint16_t rec = wram16(snes, ACTOR_LIST_HEAD);
       rec != 0 && n < ACTOR_LIST_COUNT; n++) {
    if (rec < ACTOR_LIST_BASE ||
        rec >= ACTOR_LIST_BASE + ACTOR_LIST_COUNT * ACTOR_LIST_STRIDE) {
      printf("    $%04X  (off the list)\n", rec);
      break;
    }
    uint16_t slot = wram16(snes, (uint16_t)(rec + ACTOR_REC_THREAD));
    // A record's thread is a byte offset into the scheduler's arrays, and the
    // page it runs on is that offset looked up in the ROM's own table — which
    // is what makes `--watch $page+$3C` an aimed instrument rather than a guess.
    printf("    $%04X  $%04X  %5u %5u   $%02X   $%02X    $%04X  $%02X:%04X\n",
           rec, wram16(snes, (uint16_t)(rec + ACTOR_REC_FLAGS)),
           wram16(snes, (uint16_t)(rec + ACTOR_REC_X)),
           wram16(snes, (uint16_t)(rec + ACTOR_REC_Y)),
           wram16(snes, (uint16_t)(rec + ACTOR_REC_ID)), slot,
           rom_file_word(rom, rom_len, THREAD_DP_TABLE_BANK,
                    (uint16_t)(THREAD_DP_TABLE_ADDR + slot)),
           wram16(snes, (uint16_t)(W_THREAD_HANDLER_BANK + slot)) & 0xff,
           wram16(snes, (uint16_t)(W_THREAD_HANDLER + slot)));
    rec = wram16(snes, (uint16_t)(rec + ACTOR_REC_NEXT));
  }
  if (n == 0) printf("    (empty)\n");
}

// A saved machine, plus the frame it was saved on. The frame is the point: a
// movie is a list of inputs indexed by frame number, so a state that does not
// know its own frame cannot be replayed into.
//
// The reason this exists is arithmetic. `tools/fit_route.py` replays the whole
// movie once per leg, and a movie that has already finished three levels is ten
// thousand frames of that before the leg being fitted even starts — 53 seconds
// a leg, against about two seconds of the part anybody is fitting. Level 24's
// walk is forty-odd legs. Saving the machine at the level load turns each of
// those replays into the part that changes.
#define STATE_MAGIC 0x314e4d5au  // 'ZMN1'

static bool state_save(Snes* snes, const char* path, int frame) {
  const int size = snes_saveState(snes, NULL);
  uint8_t* data = (uint8_t*)malloc((size_t)size);
  if (!data) return false;
  snes_saveState(snes, data);
  FILE* f = fopen(path, "wb");
  if (!f) { free(data); return false; }
  const uint32_t head[3] = {STATE_MAGIC, (uint32_t)frame, (uint32_t)size};
  bool ok = fwrite(head, sizeof head, 1, f) == 1 &&
            fwrite(data, (size_t)size, 1, f) == 1;
  fclose(f);
  free(data);
  return ok;
}

static bool state_load(Snes* snes, const char* path, int* frame) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  uint32_t head[3] = {0, 0, 0};
  if (fread(head, sizeof head, 1, f) != 1 || head[0] != STATE_MAGIC) {
    fclose(f);
    return false;
  }
  uint8_t* data = (uint8_t*)malloc(head[2]);
  bool ok = data && fread(data, head[2], 1, f) == 1 &&
            snes_loadState(snes, data, (int)head[2]);
  fclose(f);
  free(data);
  if (ok) *frame = (int)head[1];
  return ok;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr,
            "usage: %s <rom.sfc> <out.png> [frames] [-m movie] [--at f,f,...]\n"
            "       [--pos first[,last[,step]]] [--records first[,last[,step]]]\n"
            "       [--watch addr[,first[,last[,step]]]]...\n"
            "       [--save frame,file] [--load file]\n"
            "       [--poke frame[+]:addr=value[.b]]...\n"
            "       [--twin-stick] [--aim frame[+]:U|D|L|R...|-]...\n"
            "       [--widescreen off|16:9|16:10|21:9] [--flashing-radar]\n"
            "       [--invincible] [--invincible-neighbors] [--infinite-ammo]\n"
            "       [--infinite-lives] [--give-all] [--always-run]\n",
            argv[0]);
    return 2;
  }
  const char* rom_path = argv[1];
  const char* out_path = argv[2];
  WideMode wide = WIDE_OFF;
  bool radar_flash = false;  // src/radar.h
  int frames = 180;
  const char* movie_path = NULL;
  int snap_at[MAX_SNAPSHOTS];
  int snap_count = 0;
  int pos_first = -1, pos_last = -1, pos_step = 10;
  PokeList pokes = {{{0}}, 0};
  Cheats cheats;
  cheats_init(&cheats);
  AimSpec aims[MAX_AIMS];
  int aim_count = 0;
  bool twin = false;
  int rec_first = -1, rec_last = -1, rec_step = 10;
  // `--watch` may be given more than once. One word is the usual question; the
  // question that produced this option was "which of the twenty-four thread
  // pages is this word on", and answering that with twenty-four runs of a
  // three-minute replay is not answering it.
  int watch_addr[MAX_WATCH];
  int watch_prev[MAX_WATCH];
  int watch_count = 0;
  int watch_first = 0, watch_last = -1, watch_step = 1;
  const char* save_path = NULL;
  const char* load_path = NULL;
  int save_frame = -1;

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
    } else if (!strcmp(argv[i], "--watch") && has_next) {
      // `<addr>[,first[,last[,step]]]`, the address in hex with or without a
      // leading `$`, because every other tool in the project prints it that way.
      const char* p = argv[++i];
      if (*p == '$') p++;
      if (watch_count == MAX_WATCH) {
        fprintf(stderr, "error: at most %d --watch addresses\n", MAX_WATCH);
        return 2;
      }
      watch_prev[watch_count] = -1;
      watch_addr[watch_count++] = (int)strtol(p, NULL, 16);
      int v[3] = {0, -1, 1};
      int n = 0;
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
      for (; *p && n < 3;) {
        v[n++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
      }
      // The window is the last one given, so `--watch a,f,l --watch b` reads b
      // over the same frames rather than over all of them.
      if (n > 0) {
        watch_first = v[0];
        watch_last = v[1];
        watch_step = v[2] > 0 ? v[2] : 1;
      }
    } else if (cheats_flag(&cheats, argv[i])) {
      // The frontend's cheats (`src/cheats.h`), here so that `--watch` can be
      // aimed at what they hold -- and under the stock core, which is the half
      // of them the frontend's own runs do not show.
    } else if (!strcmp(argv[i], "--poke") && has_next) {
      // The same flag `zamn_cosim verify` takes, so a state worked out with
      // pictures here transfers to the run that checks it verbatim.
      if (!poke_parse(&pokes, argv[++i])) return 2;
    } else if (!strcmp(argv[i], "--twin-stick")) {
      twin = true;
    } else if (!strcmp(argv[i], "--aim") && has_next) {
      if (aim_count == MAX_AIMS) {
        fprintf(stderr, "error: at most %d --aim specs\n", MAX_AIMS);
        return 2;
      }
      if (!aim_parse(argv[++i], &aims[aim_count])) {
        fprintf(stderr, "error: --aim wants frame[+]:dirs, dirs being U, D, L, R or -\n");
        return 2;
      }
      aim_count++;
      twin = true;
    } else if (!strcmp(argv[i], "--save") && has_next) {
      const char* p = argv[++i];
      save_frame = atoi(p);
      while (*p && *p != ',') p++;
      if (*p == ',') save_path = p + 1;
    } else if (!strcmp(argv[i], "--load") && has_next) {
      load_path = argv[++i];
    } else if (!strcmp(argv[i], "--widescreen") && has_next) {
      if (!wide_parse(argv[++i], &wide)) {
        fprintf(stderr, "error: --widescreen wants off, 16:9, 16:10 or 21:9\n");
        return 2;
      }
    } else if (!strcmp(argv[i], "--flashing-radar")) {
      radar_flash = true;
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
  // The frontend reads the chip's registers from `src/video`, which keeps
  // them beside the PPU's. The PPU draws the picture here.
  static VideoHook video_hook;
  video_hook_install(&video_hook, snes->ppu, VIDEO_EMULATED, false);
  video_hook_keep_registers(&video_hook, snes->ppu);
  // Widen the picture before the first frame is drawn. `fb_w` is what the core
  // will pack its rows at from here on, and every buffer above was allocated at
  // the widest it could be. The hook then does the per-frame half of it, at the
  // top of each frame rather than from here — see `SnesFrameHook`.
  snes_setWidescreen(snes, wide_margin(wide), wide_margin(wide));
  fb_w = snes_pixelWidth(snes);
  static Widescreen ws;
  widescreen_install(snes, &ws, rom, rom_len, wide_margin(wide));
  ws.radar.steady = !radar_flash;
  if (wide != WIDE_OFF) printf("Widescreen %s: %d columns\n", wide_name(wide), fb_w / 2);
  // XRGB layout: framebuffer bytes per pixel are [B, G, R, X].
  snes_setPixelFormat(snes, pixelFormatXRGB);
  if (!cheats_install(&cheats, snes->cart->rom, (size_t)snes->cart->romSize)) {
    fprintf(stderr, "error: this is not a cartridge the cheats know how to change\n");
    return 1;
  }
  cheats_print(&cheats);
  if (twin) {
    if (!twin_install(snes->cart->rom, snes->cart->romSize)) {
      fprintf(stderr, "error: this is not a cartridge twin stick knows how to change\n");
      return 1;
    }
    printf("Twin stick: the right stick is --aim's, and it presses Y.\n");
  }
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

  int start = 0;
  if (load_path) {
    if (!state_load(snes, load_path, &start)) {
      fprintf(stderr, "error: cannot load state '%s'\n", load_path);
      if (have_movie) movie_free(&movie);
      free(rom); snes_free(snes);
      return 1;
    }
    printf("Loaded state '%s' at frame %d\n", load_path, start);
  }

  printf("Running %d frames...\n", frames);
  if (pos_first >= 0) {
    if (pos_last < 0) pos_last = frames;
    printf("  frame        p1 x,y        p2 x,y\n");
  }
  if (rec_first >= 0 && rec_last < 0) rec_last = frames;
  int next_snap = 0;
  for (int i = start; i < frames; i++) {
    if (have_movie) {
      movie_apply(&movie, snes, i);
    }
    poke_apply(&pokes, snes->ram, i);
    cheats_tick(&cheats, snes->ram, snes->cart->rom);
    if (twin) {
      // Written after the movie, so the `Y` lands on top of it and is not
      // taken back by the next frame's replay only if the stick is still out.
      const uint16_t y = twin_apply(snes->cart->rom, 0, aim_at(aims, aim_count, i));
      if (y) snes_setButtonState(snes, 1, BTN_Y, 1);
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
      print_records(snes, rom, rom_len, i + 1);
    }
    if (save_path && i + 1 == save_frame) {
      if (!state_save(snes, save_path, i + 1)) {
        fprintf(stderr, "error: cannot write state '%s'\n", save_path);
        if (have_movie) movie_free(&movie);
        free(rom); snes_free(snes);
        return 1;
      }
      printf("Saved state at frame %d -> %s\n", i + 1, save_path);
    }
    if (watch_count > 0 && i + 1 >= watch_first &&
        (watch_last < 0 || i + 1 <= watch_last) &&
        (i + 1 - watch_first) % watch_step == 0) {
      for (int w = 0; w < watch_count; w++) {
        int v = wram16(snes, (uint16_t)watch_addr[w]);
        if (v != watch_prev[w]) {
          printf("  watch $7E:%04X  frame %5d  $%04X (%d)\n", watch_addr[w],
                 i + 1, v, v);
          watch_prev[w] = v;
        }
      }
    }
  }
  printf("Ran %u frames (core reports %u), %llu cpu cycles\n",
         frames - start, snes->frames, (unsigned long long)snes->cycles);

  if (!write_png(snes, out_path)) {
    fprintf(stderr, "error: failed to write '%s'\n", out_path);
    if (have_movie) movie_free(&movie);
    free(rom); snes_free(snes);
    return 1;
  }
  printf("Wrote %s (%dx%d)\n", out_path, fb_w, FB_H);

  if (have_movie) movie_free(&movie);
  free(rom);
  snes_free(snes);
  return 0;
}
