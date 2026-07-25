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
//   zamn_cosim run    <rom.sfc> [-m movie] [-f frames] [-r routine]... [-v]

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"

#include "analysis/movie_apply.h"
#include "cosim/cosim.h"

#define MAX_SELECTED 32

typedef struct {
  const char* rom_path;
  const char* movie_path;
  int frames;
  const char* selected[MAX_SELECTED];
  int selected_count;
  bool verbose;
  bool coverage;  // print every marked branch, not just the untaken ones
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
    } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
      o->verbose = true;
    } else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--coverage")) {
      o->coverage = true;
    } else {
      fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
      return false;
    }
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
           all[i].supported ? "some" : "all");
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
         "modes decline the same calls, and the count is the `decl.` column.\n");
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

  for (int frame = 0; frame < o->frames; frame++) {
    if (have_movie) {
      movie_apply(&movie, snes, frame);
    }
    cosim_frame(&c);
  }

  int failures = cosim_report(&c);

  long checked = 0;
  for (int i = 0; i < c.stat_count; i++)
    if (c.enabled & (1u << i)) checked += c.stats[i].checked;
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
         "         -c full branch-coverage table (untaken branches are always\n"
         "         listed, with or without it).\n");
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
