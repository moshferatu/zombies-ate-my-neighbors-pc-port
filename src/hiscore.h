// Keeping the top scores from one run to the next.
//
// The cartridge cannot: its header declares no save RAM (type $00, size 0), and
// the console game starts every power-on from the ten developers' scores in
// the ROM. The table is work RAM and nothing else:
//
//   $7E:2064  ten rows of 15 bytes -- fourteen characters in the game's own
//             text encoding (the name, '/' padding, the score's digits) and a
//             terminating 0. `$82:B9FD` is the table of the rows' addresses.
//   $7E:20FA  ten scores, best first: 32-bit BCD, low word then high, the
//             encoding the live score at `$7E:1E72` uses.
//   $7E:2124  non-zero once the table has been set up.
//
// `$80:85F6` reads that flag on the way into the title, and if it is zero has
// `$82:BB0D` copy both halves out of the ROM (`$82:BB2E`, `$82:BBC4`) and sets
// it. `$82:BBED` is the game over asking whether either side's score beats the
// tenth, and `$82:BC41` the insertion: the name asked for (`$82:B232`), the row
// built at `$7E:1EA0` with the gaps filled with '/', rows and scores moved down
// one and the new ones stored -- the storing all in one go, with no yield in it.
//
// So this is a frontend feature and the game is not told. Between ticks, with
// the machine stopped:
//
//   * once the flag is up, the saved table is put over the game's -- once a
//     run, and only ever after the game's own copy, which would otherwise
//     land on top of it;
//   * from then on, a table that differs from what is on disk is written out.
//     That is a 190-byte compare a tick, and it means a score is on disk
//     within a sixtieth of a second of the game filing it, whatever happens to
//     the process afterwards.
//
// Nothing is written that the game could not have produced, and nothing is
// read that it could not have either: rows must be fourteen characters and a
// 0, scores BCD and in order. A file that fails that is left alone and
// reported, not overwritten until the game has a table of its own to replace
// it with -- which it then does.
//
// It is off under `-m`: a movie is a fixed run from boot, and what it shows
// should not depend on what was played yesterday, nor should playing one put
// its scores among the player's. (`--high-scores <file>` turns it on there
// too, against that file: a real game's game over under `--poke` is the test.
// Not the attract mode's -- `$80:9B87` runs the same level loop and goes from
// its game over straight to the top scores, without `$82:BBED`.)
#ifndef ZAMN_HISCORE_H
#define ZAMN_HISCORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define HISCORE_ROWS 10
#define HISCORE_ROW_BYTES 15
#define HISCORE_TEXT 0x2064   // ten rows of text
#define HISCORE_SCORES 0x20FA // ten 32-bit BCD scores
#define HISCORE_END 0x2122
#define HISCORE_BYTES (HISCORE_END - HISCORE_TEXT)
#define HISCORE_READY 0x2124 // non-zero once `$82:BB0D` has run

static const char hiscore_magic[8] = {'Z', 'A', 'M', 'N', 'H', 'S', '1', '\n'};

typedef struct {
  bool enabled;
  bool read_only;  // put the file's table in place, and write nothing: a cheat is on
  bool restored;  // the file has been looked at and, if good, put in place
  char path[1024];
  uint8_t saved[HISCORE_BYTES];  // what is on disk, as far as is known
  bool have_saved;
} Hiscore;

static inline uint32_t hiscore_score(const uint8_t* table, int row) {
  const uint8_t* s = table + (HISCORE_SCORES - HISCORE_TEXT) + row * 4;
  return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
}

// Whether `table` is something the game could have made.
static inline bool hiscore_valid(const uint8_t* table) {
  for (int r = 0; r < HISCORE_ROWS; r++) {
    const uint8_t* row = table + r * HISCORE_ROW_BYTES;
    for (int c = 0; c < HISCORE_ROW_BYTES - 1; c++)
      if (row[c] == 0) return false;  // `$82:BC4E` leaves no gap in a row
    if (row[HISCORE_ROW_BYTES - 1] != 0) return false;
    const uint32_t s = hiscore_score(table, r);
    for (int n = 0; n < 8; n++)
      if (((s >> (n * 4)) & 0xf) > 9) return false;
    // BCD orders as binary does.
    if (r > 0 && s > hiscore_score(table, r - 1)) return false;
  }
  return true;
}

// The file beside the ROM, with its extension swapped: the ROM is the one
// thing whose whereabouts the player has already decided.
static inline void hiscore_init(Hiscore* h, const char* rom_path, const char* path) {
  memset(h, 0, sizeof *h);
  h->enabled = true;
  if (path) {
    snprintf(h->path, sizeof h->path, "%s", path);
    return;
  }
  snprintf(h->path, sizeof h->path - 8, "%s", rom_path);
  char* dot = strrchr(h->path, '.');
  const char* slash = strrchr(h->path, '/');
  const char* back = strrchr(h->path, '\\');
  if (back && (!slash || back > slash)) slash = back;
  if (dot && (!slash || dot > slash)) *dot = 0;
  strcat(h->path, ".hiscore");
}

static inline bool hiscore_read(Hiscore* h, uint8_t* table) {
  FILE* f = fopen(h->path, "rb");
  if (!f) return false;  // no file yet: the first run, and not worth a word
  uint8_t buf[sizeof hiscore_magic + HISCORE_BYTES + 1];
  const size_t n = fread(buf, 1, sizeof buf, f);
  fclose(f);
  if (n != sizeof hiscore_magic + HISCORE_BYTES || memcmp(buf, hiscore_magic, sizeof hiscore_magic) ||
      !hiscore_valid(buf + sizeof hiscore_magic)) {
    fprintf(stderr, "note : '%s' is not a top scores table; ignored\n", h->path);
    return false;
  }
  memcpy(table, buf + sizeof hiscore_magic, HISCORE_BYTES);
  return true;
}

// Written beside itself and renamed over, so that a run killed half way
// through a write leaves the old table and not half of the new one.
static inline bool hiscore_write(Hiscore* h, const uint8_t* table) {
  char tmp[1040];
  snprintf(tmp, sizeof tmp, "%s.tmp", h->path);
  FILE* f = fopen(tmp, "wb");
  if (!f) return false;
  const bool ok = fwrite(hiscore_magic, 1, sizeof hiscore_magic, f) == sizeof hiscore_magic &&
                  fwrite(table, 1, HISCORE_BYTES, f) == HISCORE_BYTES;
  if (fclose(f) != 0 || !ok) { remove(tmp); return false; }
#ifdef _WIN32
  // `rename` does not replace on Windows, and removing first would leave a
  // moment with no table at all.
  if (!MoveFileExA(tmp, h->path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    remove(tmp);
    return false;
  }
#else
  if (rename(tmp, h->path) != 0) { remove(tmp); return false; }
#endif
  return true;
}

// Once a tick, with the machine stopped. `ram` is the console's work RAM.
static inline void hiscore_tick(Hiscore* h, uint8_t* ram) {
  if (!h->enabled) return;
  if (!(ram[HISCORE_READY] | ram[HISCORE_READY + 1])) return;  // the game's copy is still to come
  uint8_t* table = ram + HISCORE_TEXT;
  if (!h->restored) {
    h->restored = true;
    if (hiscore_read(h, h->saved)) {
      h->have_saved = true;
      memcpy(table, h->saved, HISCORE_BYTES);
      printf("Top scores: restored from '%s' (best %08x).\n", h->path, (unsigned)hiscore_score(table, 0));
      fflush(stdout);
    } else if (hiscore_valid(table)) {
      // Nothing to restore. The game's own table is what a file would hold,
      // so it is not written until somebody earns a place in it.
      memcpy(h->saved, table, HISCORE_BYTES);
      h->have_saved = true;
    }
    return;
  }
  if (h->have_saved && !memcmp(h->saved, table, HISCORE_BYTES)) return;
  if (h->read_only) return;
  if (!hiscore_valid(table)) return;
  if (hiscore_write(h, table)) {
    memcpy(h->saved, table, HISCORE_BYTES);
    h->have_saved = true;
    printf("Top scores: saved to '%s'.\n", h->path);
  } else {
    fprintf(stderr, "error: cannot write top scores to '%s'\n", h->path);
    h->enabled = false;  // once, not sixty times a second
  }
  fflush(stdout);
}

#endif  // ZAMN_HISCORE_H
