// Monster sounds the game leaves out because the level loaded another bank.
//
// The sound driver holds one of four sample sets at a time, the level's
// `+$34`, loaded with its song by `apu_play_song` (`$80:CBD9`), which keeps the
// choice in `$7E:1F52`. Seven effect ids, `$25` to `$2B`, are drawn with those
// samples, so each id is a different noise in each set, and the monsters that
// own them check the set before asking for one:
//
//     LDA $1F52 : CMP #bank : BNE + : LDA #id : JSL apu_play_sfx : +
//
// Eleven of those in the code banks, and a level only hears the ones for its
// own set. Surveyed across levels 1 to 48 (the set each loads, and whose
// thread runs near each check):
//
//   set 0   $29 and $2A, the chainsaw maniac (`$81:93D3`, `$81:9811`);
//           31 levels, among them 4, 15, 31 and 34 where he was found
//   set 1   $2B, the werewolf (`$81:AC05`); the nine levels 7, 14, 17, 19,
//           22, 27, 30, 37 and 42, but he is also on 29 (set 0) and 48 (set 3)
//   set 2   $25 (`$82:8D6C`); no level loads it, so it is never heard
//   set 3   $28, the evil doll's laugh (`$81:B2C7`), $26 (`$82:EFDE`,
//           levels 37, 39, 48), and $27 at five sites; levels 1, 3, 10, 28,
//           36, 39, 44 and 48
//
// So Monster Phobia (36, set 3) has chainsaws and werewolves that never make
// a sound, and Warehouse of the Evil Dolls (24, set 0) has dolls that never
// laugh. On a console as well: this is the game's choice, not the driver
// running out of room, which the player's wider settings are about
// (`src/sound/player.h`).
//
// ## What this does about it
//
// The player (`src/sound/player.h`) has a unit of eight voices for each set,
// its RAM holding that set, and two tracks for them. When a check fails, this
// asks the player to play the id from the set the monster wanted, and the
// sound is mixed with the rest.
//
// Each set's RAM is the game's, taken once the driver has registered its
// effects (`$DE`, which `$0D` sets after the effect bank arrives at boot), with
// a level's music loaded on it the way `apu_play_song` loads it: command `$08`
// and the song, the set's samples (data set 12 + set), and `$14`, all from
// `music_upload`, so the set gets the bytes the game would send. The song is
// one a level with that set really plays, so the pairing is one the driver was
// built for; it is never started. Set 2 has no level and borrows level 1's
// song.
//
// The game is only read: the checks are watched through `cosim_watch`, and the
// sound, WRAM and the save states are never written.
//
// **A watch fires when the 65816 is about to execute the check, and a port
// that runs through one never puts it there.** So a ported check has to be
// the first instruction of a registry row: the row before it stops on the
// check, the watch fires, and the next row begins. Three are ported, and
// each is: `doll_heard` (`$81:B2C7`), `ts_werewolf_howl_check` (`$81:AC05`)
// and `boss_spit_begins` (`$82:8D6C`). The doll's was not for a week, and
// no doll was heard on a level with another set in that time.
#ifndef ZAMN_BANK_SFX_H
#define ZAMN_BANK_SFX_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "snes.h"

#include "assets/music.h"
#include "assets/rom.h"
#include "sound/player.h"

#define BSFX_BANK_AT 0x1F52u     // $7E:1F52, the sample set `apu_play_song` loaded
#define BSFX_SETS MUSIC_SAMPLE_SET_COUNT
#define BSFX_LEVEL_TABLE 0x9F8000u  // a word per level, from 1: its record in bank $9F
#define BSFX_MAX_SITES 16
#define BSFX_CMD_PLAY_SONG 0x14

typedef struct {
  Player* player;
  Rom rom;
  bool enabled;
  bool built;
  bool failed;
  int sites;
  uint32_t site_at[BSFX_MAX_SITES];
  uint8_t site_set[BSFX_MAX_SITES], site_id[BSFX_MAX_SITES];
  long wanted;                // checks that failed once the sets were built
  long played;                // ...and were given a track
} BankSfx;

// Every `LDA $1F52 : CMP #set : BNE +7 : LDA #id : JSL $80:CC3B` in banks
// $80-$83, found by their bytes so that nothing here is an address to trust.
static inline int bsfx_find_sites(BankSfx* b) {
  static const uint8_t head[3] = {0xAD, 0x52, 0x1F};
  const uint8_t* r = b->rom.data;
  const uint32_t end = b->rom.size < 0x20000u ? b->rom.size : 0x20000u;
  b->sites = 0;
  for (uint32_t o = 0; o + 15 <= end && b->sites < BSFX_MAX_SITES; o++) {
    if (memcmp(r + o, head, 3) || r[o + 3] != 0xC9 || r[o + 5] != 0x00 || r[o + 6] != 0xD0 ||
        r[o + 7] != 0x07 || r[o + 8] != 0xA9 || r[o + 10] != 0x00 || r[o + 11] != 0x22 ||
        r[o + 12] != 0x3B || r[o + 13] != 0xCC || r[o + 14] != 0x80 || r[o + 4] >= BSFX_SETS)
      continue;
    // Within a bank's upper half: the LoROM code is at $8000-$FFFF of $80-$83.
    b->site_at[b->sites] = (0x80u + o / 0x8000u) << 16 | (0x8000u + o % 0x8000u);
    b->site_set[b->sites] = r[o + 4];
    b->site_id[b->sites] = r[o + 9];
    b->sites++;
  }
  return b->sites;
}

// `player` must have `other_sets`.
static inline void bank_sfx_init(BankSfx* b, Player* player, const uint8_t* rom, int rom_len,
                                 bool enabled) {
  memset(b, 0, sizeof *b);
  b->player = player;
  b->rom.data = rom;
  b->rom.size = (uint32_t)rom_len;
  b->enabled = enabled && player && player->set.other_sets;
  if (b->enabled && bsfx_find_sites(b) == 0) b->enabled = false;
}

// The song a level with this set plays, or level 1's for a set none has. The
// record's `+$32` and `+$34`, read here rather than through `assets/level.h`,
// whose `LEVEL_FIRST` is not the frontend's (`levelstart.h`).
static inline int bsfx_song_for(const Rom* rom, int set) {
  int fallback = -1;
  for (int level = 1; level <= 48; level++) {
    const uint16_t at = rom_word(rom, BSFX_LEVEL_TABLE + 2u * (uint32_t)level);
    if (at < 0x8000u) continue;
    const uint32_t rec = (BSFX_LEVEL_TABLE & 0xFF0000u) | at;
    const uint16_t song = rom_word(rom, rec + 0x32), sample_set = rom_word(rom, rec + 0x34);
    if (song < MUSIC_SONG_FIRST || song > MUSIC_SONG_LAST) continue;
    if (fallback < 0) fallback = song;
    if (sample_set == set) return song;
  }
  return fallback;
}

typedef struct { Player* player; int set; } BsfxUpload;

static void bsfx_upload_send(uint8_t cmd, uint8_t param, void* ctx) {
  const BsfxUpload* u = (const BsfxUpload*)ctx;
  player_set_command(u->player, u->set, cmd, param, 0);
}

// Set `set` loaded on the player's unit for it: what `apu_play_song` sends.
static inline bool bsfx_load(const Rom* rom, Player* p, int set) {
  const int song = bsfx_song_for(rom, set);
  if (song < 0) return false;
  player_build_set(p, set);
  BsfxUpload u = {p, set};
  if (music_upload(rom, song, true, bsfx_upload_send, &u) != MUSIC_OK) return false;
  if (music_upload(rom, MUSIC_SET_SAMPLES + set, false, bsfx_upload_send, &u) != MUSIC_OK)
    return false;
  // `$80:CBF3  LDX #$14`, with A as `apu_load_set` left it.
  player_set_command(p, set, BSFX_CMD_PLAY_SONG, 0x00, 0);
  player_set_done(p, set);
  return true;
}

// For `cosim_watch`, at each check.
static void bank_sfx_at(Snes* snes, void* ctx) {
  BankSfx* b = (BankSfx*)ctx;
  if (!b->built) return;
  const uint32_t pc = (uint32_t)snes->cpu->k << 16 | snes->cpu->pc;
  const uint8_t have = snes->ram[BSFX_BANK_AT];
  for (int k = 0; k < b->sites; k++) {
    if (b->site_at[k] != pc) continue;
    if (b->site_set[k] == have) return;  // the game plays this one itself
    b->wanted++;
    b->played += player_play_from_set(b->player, b->site_set[k], b->site_id[k]);
    return;
  }
}

// After each tick: load the sets once the game's sound has its effects.
static inline void bank_sfx_tick(BankSfx* b) {
  if (!b->enabled || b->built || !player_can_build(b->player)) return;
  for (int i = 0; i < BSFX_SETS; i++)
    if (!bsfx_load(&b->rom, b->player, i)) b->failed = true;
  b->built = true;
}

#endif  // ZAMN_BANK_SFX_H
