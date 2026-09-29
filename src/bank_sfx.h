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
// running out of room, which is what `src/sfx_overlay.h` is about.
//
// ## What this does about it
//
// It keeps an APU for each of the other sets, each running the same driver
// with that set loaded, and when a check fails, it asks the one that has the
// set to play the id. Its sound is added to the real APU's.
//
// Each is built from a copy of the real APU, taken once the driver is running
// and has registered its effects (`$DE`, which `$0D` sets after the effect bank
// arrives at boot). A level's music is then loaded on the copy the way
// `apu_play_song` loads it: command `$08` and the song, the set's samples
// (data set 12 + set), and `$14`, all from `music_upload`, so the copy gets
// the bytes the game would send. The song is one a level with that set really
// plays, so the pairing is one the driver was built for, and it is stopped as
// soon as it starts. Set 2 has no level and borrows level 1's song.
//
// The copies are independent of the real APU from then on: they are not kept
// in step with it, a load or a reset leaves them as they are, and nothing about
// them is saved. A copy that has nothing to play is not run.
//
// The game is only read: the checks are watched through `cosim_watch`, and the
// real APU, WRAM and the save states are never written. It follows one command
// of the game's: `$13`, the master volume, which the pause menu turns down.
// That is the only other command the game sends while a level runs; `$02`
// only ever stops the song, and `$09` is never sent.
#ifndef ZAMN_BANK_SFX_H
#define ZAMN_BANK_SFX_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "apu.h"
#include "dsp.h"
#include "snes.h"
#include "spc.h"
#include "statehandler.h"

#include "assets/music.h"
#include "assets/rom.h"

#define BSFX_BANK_AT 0x1F52u     // $7E:1F52, the sample set `apu_play_song` loaded
#define BSFX_SETS MUSIC_SAMPLE_SET_COUNT
#define BSFX_LEVEL_TABLE 0x9F8000u  // a word per level, from 1: its record in bank $9F
#define BSFX_MAX_SITES 16
// `$80:CCD1  STX $2142` in `apu_send`: the ROM putting a command on the bus,
// the command in X and its parameter in A, both 8 bits wide.
#define BSFX_SEND_AT 0x80CCD1u
#define BSFX_CMD_PLAY 0x01
#define BSFX_CMD_STOP 0x02
#define BSFX_CMD_VOLUME 0x13
#define BSFX_CMD_PLAY_SONG 0x14

// The driver's zero page, as `src/sfx_overlay.h` names it.
#define BSFX_SEQ_ID 0x3C      // +slot: the sequence each slot plays
#define BSFX_VOICE_ON 0x48    // +voice: note held
#define BSFX_LAST_SEQ 0xC9    // the counter of the last command taken
#define BSFX_EFFECTS 0xDE     // effect sequences registered; 0 until `$0D`
#define BSFX_DRIVER_BANNER 0x0603u

#define BSFX_QUEUE 8
#define BSFX_FRAME_CYCLES 17088   // 534 samples at 32 cycles each
#define BSFX_COMMAND_LIMIT 400000 // cycles a command may take to be picked up
#define BSFX_SETTLE 512000        // cycles run muted after the song is stopped
#define BSFX_TAIL_FRAMES 90       // run on this long after the last activity

typedef struct {
  Apu* apu;
  bool ready;
  uint8_t kind[BSFX_QUEUE];   // commands waiting for the driver: play or volume...
  uint8_t queue[BSFX_QUEUE];  // ...and the id or the volume
  int queued;
  int idle;                   // frames with nothing sounding
  long played;
} BsfxSet;

typedef struct {
  Snes* snes;
  Rom rom;
  bool enabled;
  bool built;
  bool failed;
  int sites;
  uint32_t site_at[BSFX_MAX_SITES];
  uint8_t site_set[BSFX_MAX_SITES], site_id[BSFX_MAX_SITES];
  BsfxSet set[BSFX_SETS];
  uint32_t last_cycles;
  uint8_t volume;             // the last `$13`, or 0 for none yet
  long wanted;                // checks that failed while this was ready
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

static inline void bank_sfx_init(BankSfx* b, Snes* snes, const uint8_t* rom, int rom_len,
                                 bool enabled) {
  memset(b, 0, sizeof *b);
  b->snes = snes;
  b->rom.data = rom;
  b->rom.size = (uint32_t)rom_len;
  b->enabled = enabled;
  if (enabled && bsfx_find_sites(b) == 0) b->enabled = false;
}

static inline void bank_sfx_free(BankSfx* b) {
  for (int i = 0; i < BSFX_SETS; i++)
    if (b->set[i].apu) { apu_free(b->set[i].apu); b->set[i].apu = NULL; }
  b->built = false;
}

// --- Building a set's APU ---------------------------------------------------

static inline bool bsfx_run_until_taken(Apu* a) {
  uint32_t start = a->cycles;
  while (a->inPorts[3] != a->ram[BSFX_LAST_SEQ]) {
    if (a->cycles - start > BSFX_COMMAND_LIMIT) return false;
    spc_runOpcode(a->spc);
  }
  return true;
}

// One command, the way `apu_send` puts it on the ports, once the driver has
// taken the last one.
static inline bool bsfx_send(Apu* a, uint8_t cmd, uint8_t param) {
  if (!bsfx_run_until_taken(a)) return false;
  a->inPorts[1] = param;
  a->inPorts[2] = cmd;
  a->inPorts[3] = (uint8_t)(a->inPorts[3] + 1);
  return true;
}

typedef struct { Apu* apu; bool ok; } BsfxUpload;

static void bsfx_upload_send(uint8_t cmd, uint8_t param, void* ctx) {
  BsfxUpload* u = (BsfxUpload*)ctx;
  if (u->ok) u->ok = bsfx_send(u->apu, cmd, param);
}

// Stop every slot playing a sequence at or past the effects, which is the song.
static inline bool bsfx_stop_song(Apu* a) {
  for (int i = 0; i < 4; i++)
    if ((a->ram[0x28 + i] | a->ram[0x2C + i]) && a->ram[BSFX_SEQ_ID + i] >= a->ram[BSFX_EFFECTS])
      if (!bsfx_send(a, BSFX_CMD_STOP, a->ram[BSFX_SEQ_ID + i])) return false;
  return bsfx_run_until_taken(a);
}

static inline bool bsfx_song_playing(const Apu* a) {
  for (int i = 0; i < 4; i++)
    if ((a->ram[0x28 + i] | a->ram[0x2C + i]) && a->ram[BSFX_SEQ_ID + i] >= a->ram[BSFX_EFFECTS])
      return true;
  return false;
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

// Load set `set` on `a`, a copy of the real APU: stop whatever song it has,
// then what `apu_play_song` sends, then stop that song too.
static inline bool bsfx_load(const Rom* rom, Apu* a, int set) {
  const int song = bsfx_song_for(rom, set);
  if (song < 0 || !bsfx_stop_song(a)) return false;
  BsfxUpload u = {a, true};
  if (music_upload(rom, song, true, bsfx_upload_send, &u) != MUSIC_OK || !u.ok) return false;
  if (music_upload(rom, MUSIC_SET_SAMPLES + set, false, bsfx_upload_send, &u) != MUSIC_OK || !u.ok)
    return false;
  // `$80:CBF3  LDX #$14`, with A as `apu_load_set` left it: the zero that
  // ended the set.
  if (!bsfx_send(a, BSFX_CMD_PLAY_SONG, 0x00) || !bsfx_run_until_taken(a)) return false;
  const uint32_t start = a->cycles;
  while (!bsfx_song_playing(a) && a->cycles - start < BSFX_COMMAND_LIMIT) spc_runOpcode(a->spc);
  if (!bsfx_stop_song(a)) return false;
  const uint32_t settle = a->cycles;
  while (a->cycles - settle < BSFX_SETTLE) spc_runOpcode(a->spc);
  return !bsfx_song_playing(a);
}

// The real APU is running the driver with its effects registered, and has
// taken every command it was given.
static inline bool bsfx_real_ready(const Apu* r) {
  static const uint8_t banner[6] = {'A', 'U', 'D', 'I', 'O', ' '};
  return !memcmp(r->ram + BSFX_DRIVER_BANNER, banner, 6) && r->spc->pc >= 0x0600 &&
         r->spc->pc < 0x1200 && r->ram[BSFX_EFFECTS] != 0 &&
         r->inPorts[3] == r->ram[BSFX_LAST_SEQ];
}

static inline void bsfx_build(BankSfx* b) {
  Apu* r = b->snes->apu;
  StateHandler* save = sh_init(true, NULL, 0);
  apu_handleState(r, save);
  for (int i = 0; i < BSFX_SETS; i++) {
    BsfxSet* s = &b->set[i];
    s->apu = apu_init(b->snes);
    apu_reset(s->apu);
    StateHandler* load = sh_init(false, save->data, save->offset);
    apu_handleState(s->apu, load);
    sh_free(load);
    s->apu->dsp->channelMute = 0xFF;
    s->ready = bsfx_load(&b->rom, s->apu, i);
    if (!s->ready) {
      apu_free(s->apu);
      s->apu = NULL;
      b->failed = true;
      continue;
    }
    memset(s->apu->dsp->sampleBuffer, 0, sizeof s->apu->dsp->sampleBuffer);
    s->apu->dsp->channelMute = 0;
    s->idle = BSFX_TAIL_FRAMES;
  }
  sh_free(save);
  b->built = true;
}

// --- While the game runs ----------------------------------------------------

static inline void bsfx_queue(BsfxSet* s, uint8_t kind, uint8_t value) {
  if (!s->apu || s->queued == BSFX_QUEUE) return;
  s->kind[s->queued] = kind;
  s->queue[s->queued++] = value;
}

// For `cosim_watch`, at each check and at `BSFX_SEND_AT`.
static void bank_sfx_at(Snes* snes, void* ctx) {
  BankSfx* b = (BankSfx*)ctx;
  if (!b->built) return;
  const uint32_t pc = (uint32_t)snes->cpu->k << 16 | snes->cpu->pc;
  if (pc == BSFX_SEND_AT) {
    if ((snes->cpu->x & 0xff) != BSFX_CMD_VOLUME) return;
    b->volume = (uint8_t)snes->cpu->a;
    for (int i = 0; i < BSFX_SETS; i++) bsfx_queue(&b->set[i], BSFX_CMD_VOLUME, b->volume);
    return;
  }
  const uint8_t have = snes->ram[BSFX_BANK_AT];
  for (int k = 0; k < b->sites; k++) {
    if (b->site_at[k] != pc) continue;
    if (b->site_set[k] == have) return;  // the game plays this one itself
    b->wanted++;
    bsfx_queue(&b->set[b->site_set[k]], BSFX_CMD_PLAY, b->site_id[k]);
    return;
  }
}

static inline bool bsfx_active(const Apu* a) {
  for (int i = 0; i < 4; i++)
    if (a->ram[0x28 + i] | a->ram[0x2C + i]) return true;
  for (int v = 0; v < 8; v++)
    if (a->ram[BSFX_VOICE_ON + v]) return true;
  return false;
}

// After each tick: build the copies once the real APU can be copied, then give
// each one its commands and run it for as long as the real one ran.
static inline void bank_sfx_tick(BankSfx* b) {
  if (!b->enabled) return;
  Apu* r = b->snes->apu;
  if (!b->built) {
    if (!bsfx_real_ready(r)) return;
    bsfx_build(b);
    b->last_cycles = r->cycles;
    return;
  }
  const int32_t d = (int32_t)(r->cycles - b->last_cycles);
  b->last_cycles = r->cycles;
  const uint32_t run = d > 0 && d < 2 * BSFX_FRAME_CYCLES ? (uint32_t)d : BSFX_FRAME_CYCLES;
  for (int i = 0; i < BSFX_SETS; i++) {
    BsfxSet* s = &b->set[i];
    if (!s->apu) continue;
    if (s->queued) s->idle = 0;
    if (s->idle >= BSFX_TAIL_FRAMES) continue;  // silent: not run until asked
    Apu* a = s->apu;
    const uint32_t start = a->cycles;
    while (a->cycles - start < run) {
      // One at a time, as the game sends them.
      if (s->queued && a->inPorts[3] == a->ram[BSFX_LAST_SEQ]) {
        a->inPorts[1] = s->queue[0];
        a->inPorts[2] = s->kind[0];
        a->inPorts[3] = (uint8_t)(a->inPorts[3] + 1);
        if (s->kind[0] == BSFX_CMD_PLAY) s->played++;
        memmove(s->queue, s->queue + 1, (size_t)--s->queued);
        memmove(s->kind, s->kind + 1, (size_t)s->queued);
      }
      spc_runOpcode(a->spc);
    }
    s->idle = bsfx_active(a) || s->queued ? 0 : s->idle + 1;
  }
}

// Add the copies' sound to a tick's samples, before the volume.
static inline void bank_sfx_mix(BankSfx* b, int16_t* buf, int samples) {
  if (!b->enabled || !b->built) return;
  int16_t add[2048 * 2];
  if (samples > 2048) samples = 2048;
  for (int i = 0; i < BSFX_SETS; i++) {
    BsfxSet* s = &b->set[i];
    if (!s->apu || s->idle >= BSFX_TAIL_FRAMES) continue;
    dsp_getSamples(s->apu->dsp, add, samples);
    for (int k = 0; k < samples * 2; k++) {
      const int v = buf[k] + add[k];
      buf[k] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
  }
}

#endif  // ZAMN_BANK_SFX_H
