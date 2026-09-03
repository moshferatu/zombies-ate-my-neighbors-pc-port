// zamn_cosim — Phase 3 co-simulation driver.
//
// Two ways to point the harness at the game, and they answer different
// questions. See `docs/cosim.md` for the design; `src/cosim/cosim.h` for the
// contract a ported routine has to satisfy.
//
//   verify  Does the port compute what the ROM computes?
//           The ROM runs the game exactly as it always does. Every time it
//           enters a ported routine the harness snapshots WRAM and the
//           registers, lets the ROM finish, then re-runs the C port over the
//           same input and diffs all 128 KB plus the registers. Nothing about
//           the game's execution changes, so any difference is the port's.
//
//   run     Does the game still work with the port in it?
//           Two cores on the same movie — one stock, one with the ROM's
//           instructions actually skipped in favour of the C — with all of WRAM
//           compared every frame. This is the one that can fail for reasons
//           other than a wrong answer, because a substituted routine returns on
//           a cycle budget instead of by executing the original code.
//
// Usage:
//   zamn_cosim list
//   zamn_cosim verify <rom.sfc> [-m movie] [-f frames] [-r routine]... [-v]
//                               [--twin-aim <period>[,<from>]]
//   zamn_cosim run    <rom.sfc> [-m movie] [-f frames] [-r routine]... [-v]
//
// `-x routine` is the inverse of `-r`: run everything the registry has
// except the named ones.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"

#include "analysis/movie_apply.h"
#include "cosim/cosim.h"
#include "poke.h"
#include "port/player.h"  // player_set_aim, for --twin-aim
#include "twinstick.h"

// Room for every routine in the registry and then some. It was 32, chosen when
// that was more than the registry held, and "run everything except one" — which
// is how a divergence gets pinned on a routine or cleared of it — needs one
// more than there are entries.
#define MAX_SELECTED 128

// Where `--twin-aim` starts arming, unless told otherwise. Holding fire before
// the level is up drives a movie somewhere it was not written for — every one of
// them mashes Start to 1,004, and several then type a password, which is a
// screen with its own opinion about the buttons. 2,400 is past both; a movie
// that reaches gameplay later takes `--twin-aim <period>,<from>`.
#define TWIN_AIM_FROM 2400

typedef struct {
  const char* rom_path;
  const char* movie_path;
  int frames;
  const char* selected[MAX_SELECTED];
  int selected_count;
  bool verbose;
  bool coverage;  // print every marked branch, not just the untaken ones
  // ...and the other way round. `-x` is `-r` with the selection inverted,
  // and it exists because the interesting question is now that shape: one
  // routine in the registry ends every `run` comparison in the corpus, and
  // `-x lzss_decompress` is how that was found and how it stays checkable.
  // Expanded into `selected` below, so nothing downstream knows about it.
  const char* excluded[MAX_SELECTED];
  int excluded_count;
  // `--twin-aim <period>[,<from>]`: patch the cartridge for `--twin-stick`, arm
  // the port the same way, hold the fire button, and cycle the aim through
  // left, right and centred every `period` frames from frame `from`.
  //
  // It exists because that feature is the one thing in the project written
  // **twice** — nine bytes of 65816 at `$80:D250` for the stock path, and the
  // same decision in `src/port/player.c` for the substituted one — and `verify`
  // is exactly the instrument for asking whether two implementations of one
  // routine agree. Without this the two halves can only be checked apart, which
  // is how the first version shipped working in an engine nobody plays in.
  //
  // Not a way to play: there is no stick here. It is a probe, like `--watch` in
  // `zamn_headless`.
  int twin_aim;
  int twin_from;  // ...and the frame it starts arming on
  // `--poke <frame>[+]:<addr>=<value>[.b]`: assert a word of WRAM rather than
  // playing the game into it. See `src/poke.h` for what that does and does not
  // prove; the short version is that `verify` diffs the port against the ROM on
  // whatever state exists at the call, and neither of them can tell how the
  // state got there. It is how a branch that needs a rare *state* gets taken
  // without a movie that needs a rare *sequence*.
  PokeList pokes;
} Options;

static uint8_t* read_file(const char* path, int* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return NULL; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0) { fclose(f); fprintf(stderr, "error: '%s' is empty\n", path); return NULL; }
  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
    fclose(f); free(buf);
    fprintf(stderr, "error: cannot read '%s'\n", path);
    return NULL;
  }
  fclose(f);
  if (out_len) *out_len = (int)len;
  return buf;
}

static bool parse_options(int argc, char** argv, Options* o) {
  memset(o, 0, sizeof *o);
  o->frames = 2400;
  if (argc < 1) {
    fprintf(stderr, "error: a ROM path is required\n");
    return false;
  }
  o->rom_path = argv[0];
  for (int i = 1; i < argc; i++) {
    bool has_next = i + 1 < argc;
    if ((!strcmp(argv[i], "-m") || !strcmp(argv[i], "--movie")) && has_next) {
      o->movie_path = argv[++i];
    } else if ((!strcmp(argv[i], "-f") || !strcmp(argv[i], "--frames")) && has_next) {
      o->frames = atoi(argv[++i]);
    } else if ((!strcmp(argv[i], "-r") || !strcmp(argv[i], "--routine")) && has_next) {
      if (o->selected_count >= MAX_SELECTED) {
        fprintf(stderr, "error: too many -r options\n");
        return false;
      }
      const char* name = argv[++i];
      if (strcmp(name, "none") && !cosim_find(name)) {
        fprintf(stderr, "error: no ported routine named '%s' (try `list`)\n", name);
        return false;
      }
      o->selected[o->selected_count++] = name;
    } else if ((!strcmp(argv[i], "-x") || !strcmp(argv[i], "--without")) &&
               has_next) {
      if (o->excluded_count >= MAX_SELECTED) {
        fprintf(stderr, "error: too many -x options\n");
        return false;
      }
      const char* name = argv[++i];
      if (!cosim_find(name)) {
        fprintf(stderr, "error: no ported routine named '%s' (try `list`)\n",
                name);
        return false;
      }
      o->excluded[o->excluded_count++] = name;
    } else if (!strcmp(argv[i], "--twin-aim") && has_next) {
      const char* spec = argv[++i];
      char* end = NULL;
      o->twin_aim = (int)strtol(spec, &end, 10);
      o->twin_from = *end == ',' ? (int)strtol(end + 1, &end, 10) : TWIN_AIM_FROM;
      if (end == spec || *end || o->twin_aim <= 0 || o->twin_from < 0) {
        fprintf(stderr, "error: --twin-aim wants <period>[,<from>], both frame\n"
                        "       counts and the period above 0\n");
        return false;
      }
    } else if (!strcmp(argv[i], "--poke") && has_next) {
      if (!poke_parse(&o->pokes, argv[++i])) return false;
    } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
      o->verbose = true;
    } else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--coverage")) {
      o->coverage = true;
    } else {
      fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
      return false;
    }
  }
  if (o->excluded_count > 0) {
    // Both at once has no sensible reading -- `-r a -x b` is either `a` or
    // everything-but-`b`, and guessing which would be worse than refusing.
    if (o->selected_count > 0) {
      fprintf(stderr, "error: -r selects and -x deselects; use one or the other\n");
      return false;
    }
    int count = 0;
    const CosimRoutine* all = cosim_routines(&count);
    for (int i = 0; i < count; i++) {
      bool out = false;
      for (int e = 0; e < o->excluded_count && !out; e++)
        out = !strcmp(all[i].name, o->excluded[e]);
      if (out) continue;
      if (o->selected_count >= MAX_SELECTED) {
        fprintf(stderr, "error: registry outgrew MAX_SELECTED\n");
        return false;
      }
      o->selected[o->selected_count++] = all[i].name;
    }
    // An empty selection would mean "everything" downstream, which is the
    // exact opposite of what was asked for.
    if (o->selected_count == 0) o->selected[o->selected_count++] = "none";
  }
  return true;
}

static int cmd_list(void) {
  int count = 0;
  const CosimRoutine* all = cosim_routines(&count);
  printf("%d routine%s ported so far:\n\n", count, count == 1 ? "" : "s");
  printf("  %-20s %-10s %-6s %-9s %-8s %s\n", "name", "ROM", "return", "kind",
         "covers", "notes");
  for (int i = 0; i < count; i++) {
    printf("  %-20s %-10s %-6s %-9s %-8s ", all[i].name, all[i].symbol,
           all[i].ret_kind == COSIM_RTL ? "RTL" : "RTS",
           all[i].run_yield ? "resumable" : "leaf",
           all[i].verify_only ? "verify" : all[i].supported ? "some" : "all");
    if (all[i].exclude_count == 0) {
      printf("all of WRAM compared\n");
    } else {
      for (int e = 0; e < all[i].exclude_count; e++)
        printf("%s$%02X:%04X excluded — %s", e ? ", " : "",
               all[i].excludes[e].offset < 0x10000 ? 0x7e : 0x7f,
               all[i].excludes[e].offset & 0xffff, all[i].excludes[e].why);
      printf("\n");
    }
  }
  printf("\nA `leaf` routine runs to completion and is checked once per call. A\n"
         "`resumable` one suspends inside thread_yield and is checked once per\n"
         "segment — the run between two suspensions. See docs/threads.md.\n"
         "\n`covers` is `all` when the port stands in for every call, and `some`\n"
         "when it declares a guard: the port inspects each call first and hands\n"
         "back the ones it cannot serve, which the ROM then runs itself. Both\n"
         "modes decline the same calls, and the count is the `decl.` column.\n"
         "\n`verify` means checked per call but never substituted — the routine's\n"
         "body is a hardware handshake, and only the CPU that substitution stops\n"
         "can perform one. `run` leaves it to the ROM and prints `verify only`.\n");
  return 0;
}

static int cmd_verify(const Options* o) {
  int rom_len = 0;
  uint8_t* rom_data = read_file(o->rom_path, &rom_len);
  if (!rom_data) return 1;

  Snes* snes = snes_init();
  if (!snes_loadRom(snes, rom_data, rom_len)) {
    fprintf(stderr, "error: core rejected ROM\n");
    return 1;
  }

  if (o->twin_aim && !twin_install(snes->cart->rom, snes->cart->romSize)) {
    fprintf(stderr, "error: --twin-aim: this cartridge cannot take the patch\n");
    return 1;
  }

  Cosim c;
  cosim_init(&c, snes, COSIM_VERIFY);
  c.verbose = o->verbose;
  if (o->selected_count == 0) {
    cosim_enable_all(&c);
  } else {
    for (int i = 0; i < o->selected_count; i++) cosim_enable(&c, o->selected[i]);
  }

  Movie movie;
  bool have_movie = false;
  if (o->movie_path) {
    if (!movie_load(&movie, o->movie_path)) {
      fprintf(stderr, "error: cannot load movie '%s'\n", o->movie_path);
      return 1;
    }
    have_movie = true;
  }

  snes_reset(snes, true);
  // Whether a movie drives port 2 is worth saying out loud: several coverage
  // sites below are reachable only with two players, so it changes how the
  // report at the end should be read.
  printf("Replaying %d frames of '%s'%s and checking each ported routine\n"
         "against the ROM's own, call by call.\n",
         o->frames, o->movie_path ? o->movie_path : "(no input)",
         have_movie && movie_uses_port(&movie, 1) ? " (two controllers)" : "");
  poke_report(&o->pokes);

  if (o->twin_aim)
    printf("--twin-aim: the cartridge is patched and the port armed, flipping\n"
           "left/right/centred every %d frames from frame %d, with fire held.\n"
           "Both engines make the same decision here, so any disagreement is a\n"
           "real one.\n",
           o->twin_aim, o->twin_from);

  for (int frame = 0; frame < o->frames; frame++) {
    if (have_movie) {
      movie_apply(&movie, snes, frame);
    }
    // Before the frame runs and after its inputs are set, so the game reads the
    // asserted value for the whole frame. Both engines read the same WRAM, so
    // this moves the ROM and the port together and the diff stays a diff.
    poke_apply(&o->pokes, snes->ram, frame);
    // Not before `TWIN_AIM_FROM`: a movie's boot half is 1,004 frames of
    // mashing Start through the logos and the menu, and holding fire through
    // that lands somewhere the movie was not written for — the first run of
    // this reached gameplay 26 times in 4,600 frames instead of 1,912.
    if (o->twin_aim && frame >= o->twin_from) {
      // Left, right, and nothing. The first two are the case that matters —
      // opposite directions are what a walk can never produce alongside the aim
      // — and the third is the centred stick, which has its own path through
      // both engines and so has to be in the cycle rather than assumed.
      static const uint16_t cycle[3] = {0x000e, 0x0006, 0x0000};
      const uint16_t dir = cycle[((frame - o->twin_from) / o->twin_aim) % 3];
      twin_set_aim(snes->cart->rom, 0, dir);   // what the 65816 reads
      player_set_aim(0, dir);                  // ...and what the port reads
      // After `movie_apply`, so it is not undone: the aim only means anything
      // while the player is trying to fire.
      snes_setButtonState(snes, 1, BTN_Y, true);
    }
    cosim_frame(&c);
  }

  int failures = cosim_report(&c);

  long checked = 0;
  for (int i = 0; i < c.stat_count; i++)
    if (cosim_mask_get(&c.enabled, i)) checked += c.stats[i].checked;
  printf("\n%ld call%s checked, %d routine%s diverged.\n", checked,
         checked == 1 ? "" : "s", failures, failures == 1 ? "" : "s");
  if (checked == 0)
    printf("Nothing was verified — the movie never reached any ported routine.\n");

  // Not part of the pass/fail verdict, and deliberately printed after it: what
  // follows says how much of the port this movie was in a position to check at
  // all. See `src/port/coverage.h`.
  cosim_coverage_report(o->coverage);
  // ...and where the calls it could not serve went instead.
  cosim_census_report();

  if (have_movie) movie_free(&movie);
  cosim_free(&c);
  snes_free(snes);
  free(rom_data);
  return (failures > 0 || checked == 0) ? 1 : 0;
}

static int cmd_run(const Options* o) {
  // Refused rather than ignored. `run` drives two cores and compares all of
  // WRAM between them; a poke that reached one and not the other would show up
  // as exactly the thing this pass exists to detect, and silently dropping it
  // would make a poked `run` read like a clean one.
  if (o->pokes.count > 0) {
    fprintf(stderr, "error: --poke is a verify-only flag. `run` compares two\n"
                    "       timelines, and asserting into one of them is not a\n"
                    "       comparison.\n");
    return 2;
  }
  int rom_len = 0;
  uint8_t* rom_data = read_file(o->rom_path, &rom_len);
  if (!rom_data) return 1;
  int rc = cosim_lockstep(rom_data, rom_len, o->movie_path, o->frames,
                          o->selected, o->selected_count, o->verbose);
  cosim_coverage_report(o->coverage);
  cosim_census_report();
  free(rom_data);
  return rc;
}

static void usage(void) {
  printf("zamn_cosim — Phase 3 co-simulation harness\n\n"
         "  list\n"
         "      What has been ported, and what the diff covers for each.\n\n"
         "  verify <rom.sfc> [-m movie] [-f frames] [-r routine]... [-v] [-c]\n"
         "      Let the ROM run the game, and check the port against every call\n"
         "      it makes to a ported routine: 128 KB of WRAM plus registers,\n"
         "      per call. Repeat -r to narrow it; the default is everything.\n\n"
         "  run <rom.sfc> [-m movie] [-f frames] [-r routine]... [-v] [-c]\n"
         "      Substitute the port for real and run two cores in lockstep,\n"
         "      comparing all of WRAM every frame against a stock one.\n\n"
         "Options: -m movie file, -f frames (default 2400), -v progress,\n"
         "         -x routine to run everything but that one (repeatable,\n"
         "         and not combinable with -r),\n"
         "         -c full branch-coverage table (untaken branches are always\n"
         "         listed, with or without it),\n"
         "         --poke <frame>[+]:<addr>=<value>[.b] to assert a word of\n"
         "         WRAM rather than play the game into it (verify only,\n"
         "         repeatable). A branch taken this way is checked against the\n"
         "         ROM on a state nothing proved reachable — see src/poke.h.\n");
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  const char* cmd = argv[1];
  if (!strcmp(cmd, "list")) return cmd_list();

  Options o;
  if (!strcmp(cmd, "verify") || !strcmp(cmd, "run")) {
    if (!parse_options(argc - 2, argv + 2, &o)) return 2;
    return !strcmp(cmd, "verify") ? cmd_verify(&o) : cmd_run(&o);
  }
  fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
  usage();
  return 2;
}
