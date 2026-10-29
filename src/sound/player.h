// The game's sound driver as a player of our own: the same sequencer, written
// as plain C with its state in structs, that can be given more of everything
// than the console had. It is what the game runs on the sound chip.
//
// On the console the driver has four tracks and eight voices for the song and
// every sound effect between them, and one sample set at a time. A busy fight
// runs out: an effect with no free track is not started, and a note with no
// free voice takes one from something else or is not played. And seven effect
// ids are drawn with the level's sample set, so a monster whose sound is in
// another set is silent (see `src/bank_sfx.h`). The player has settings for
// each of these:
//
//   * `tracks`: four to eight. The song keeps one, and the rest are for effects.
//   * `live_voices`: eight to a DSP of the port's own (`dsp.c`), up to 32. The
//     units' outputs are added.
//   * `max_same`: an effect is not started while it is already playing on
//     this many tracks. A weapon that fires faster than its sound ends fills
//     every free track with itself; on the console that is three, so with
//     three here it sounds as it always did.
//   * `other_sets`: for each sample set, a driver of its own beside the game's
//     with two tracks and eight voices, on a DSP whose RAM holds that set, so
//     the frontend can play a monster's sound from a set the level did not
//     load. The game's songs and effects lose nothing to them: not a track, a
//     voice or a cycle.
//
// ## Leading: the program on the chip
//
// The game waits on the sound chip's answers, a command and a byte at a time,
// songs and samples included, so when each answer comes moves everything the
// game does after it (see `sound.h`). A player with the console's settings
// (`player_lead`) is the program the chip's loader starts at `$0600`: it polls
// the ports, answers, takes each command and ticks on the timer, all on the
// driver's cycles, so the game sees what it saw on the console. It counts the
// cycles of the driver's routines as `driver.c` has them, every path, and
// makes each DSP write on its cycle: a note's volumes, sample and pitch over
// the thousand cycles the driver takes to key it on. `zamn_test_sound
// --player` checks it against the SPC700 running the original: every port
// read and write, every DSP write and every sample.
//
// ## Following: more than the console had
//
// More tracks or voices would take more cycles, and move the answers. So the
// wider settings are a second player (`player_follow`) that follows the
// leader: it is told, through `Sound.watch`, each command the leader picks up
// and each tick of the timer, with the cycle, and does the same at the same
// cycle. It is what is heard, and the leader makes no samples (`silent`).
//
// A follower takes its start from the leader: its RAM, tracks and voices, its
// DSP, and the samples it has made (`player_import`). That is done when the
// leader is first found running, and again after a reset, or a quick load
// that did not keep the follower. A tick or command starts on the leader's
// cycle, unless the follower is still busy with the last: the driver runs at
// most one tick each time round its loop, so a tick that comes while it is
// busy waits, and two that come lose one, and the song slows. That is the
// console's way, and the follower keeps it whether it is busy for its own
// reasons or the leader's.
//
// With `player_console` a follower makes the leader's DSP writes on the
// leader's cycles, and so the same samples: `zamn_test_player` checks that.
//
// Port code: libc only.

#ifndef SOUND_PLAYER_H
#define SOUND_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sound/dsp.h"
#include "sound/sound.h"

#define PLAYER_GAME_TRACKS_MAX 8
#define PLAYER_SET_TRACKS 2
#define PLAYER_SETS 4
#define PLAYER_TRACKS_MAX (PLAYER_GAME_TRACKS_MAX + PLAYER_SETS * PLAYER_SET_TRACKS)
#define PLAYER_LIVE_UNITS_MAX 4
#define PLAYER_UNITS_MAX (PLAYER_LIVE_UNITS_MAX + PLAYER_SETS)
#define PLAYER_VOICES_MAX (PLAYER_UNITS_MAX * 8)
#define PLAYER_RING 16384

typedef struct {
  int tracks;       // the game's, 4 to PLAYER_GAME_TRACKS_MAX; the console has 4
  int live_voices;  // 8 to 32, a multiple of 8; the console has 8
  int max_same;     // 0 for no limit
  bool other_sets;
} PlayerSettings;

extern const PlayerSettings player_console;
// Eight tracks, 24 voices, three of an effect at once; no other sets.
extern const PlayerSettings player_wide;

// One DSP of the port's own, with eight voices and a sound RAM. The live
// units' RAM gets every upload the game makes, and the other sets' units each
// hold that set (`player_build_set`). The echo writes its delay line into it,
// as on the console.
typedef struct {
  SoundDsp dsp;
  uint8_t ram[0x10000];
  int bank;                // 0 for the game's, or 1 + the sample set
  uint8_t eon, non;        // what EON and NON were last given
  uint32_t quiet;          // ticks with every voice silent; past two seconds it sleeps
} PlayerUnit;

// A bank: the game's driver, or one of the other sets'. Each is a driver as
// the console's is, with its own tracks, voices and clock, its uploads, and
// the units its voices are on.
typedef struct {
  uint16_t upload, saved_upload;  // where the next byte goes; command $0D's
  uint8_t songs, saved_songs;     // songs registered; command $0D's
  int first_unit, units;          // its voices are the units', eight each
  int first_track, tracks;
  uint8_t order[PLAYER_LIVE_UNITS_MAX * 8];  // its voices, least recently used first
  uint64_t busy;                  // following: where its last tick or command ended
  bool tick_owed;                 // ...a tick came while it was busy
  bool ready;                     // an other set's: built
} PlayerBank;

typedef struct {
  uint16_t ptr, loop;     // where it is read from; where control $15 goes back to
  uint32_t tempo, clock;  // 24 bits each
  uint16_t wait;          // beats to the next event; 0 when the track is free
  uint8_t status, channel, volume, song, loops;
  bool shifted;           // started by command $11
  int bank;
} PlayerTrack;

// Sixteen to a track, set by its controllers.
typedef struct {
  uint8_t program, pan, volume, bend, bend_range, flags, tune;
  uint8_t vib_depth, vib_range, vib_rate, vib_wave, priority, mute;
} PlayerChannel;

typedef struct {
  bool on;
  uint8_t track, channel, note, key, velocity, priority;
  uint16_t pitch;  // as keyed, before the wheel and the vibrato
  uint8_t vib_depth, vib_range, vib_rate, vib_time, vib_step, vib_wave;
  bool echo, noise;
} PlayerVoice;

// Each DSP write the player makes, and its cycle: for the test. `unit` is -1
// for a write made to every unit.
typedef void (*PlayerWriteFn)(void* ctx, int unit, uint8_t reg, uint8_t val, uint64_t cycle);

typedef struct Player {
  // --- State: what a quick save keeps ---
  PlayerSettings set;
  int units, voices;  // in use
  int tracks;         // in use: the game's, then two for each other set
  PlayerBank bank[1 + PLAYER_SETS];
  PlayerTrack track[PLAYER_TRACKS_MAX];
  PlayerChannel channel[PLAYER_TRACKS_MAX][16];
  PlayerVoice voice[PLAYER_VOICES_MAX];
  uint8_t flg, mono, shift, paused;
  uint8_t midi_count;               // command $03's bytes so far
  uint8_t cmd, param, param0, seq;  // the last command picked up, and its count

  bool silent;  // makes no samples: a follower is heard instead
  bool synced;  // leading: started at $0600; following: in step with the leader

  uint64_t at;        // the clock of the bank playing, as the player counts it
  uint64_t host;      // the chip's `host` as of the last run
  uint64_t dsp_at;    // the next DSP tick, a multiple of 32
  uint64_t ticks;     // DSP ticks so far
  int16_t ring[PLAYER_RING * 2];

  // Counts, for `--verbose` and the tests.
  long commands, effects_started, effects_dropped, effects_held, notes_dropped,
       voices_stolen, imports, other_played[PLAYER_SETS];

  // --- Wiring: what a quick save leaves out ---
  Sound* chip;                // leading: the chip it is the program of
  const struct Player* lead;  // following: the player it follows
  const char* lost;           // following: why it last had to import again
  PlayerWriteFn on_write;
  void* write_ctx;

  // Last, so that a quick save keeps only those in use.
  PlayerUnit unit[PLAYER_UNITS_MAX];
} Player;

// Set up with settings `set`, neither leading nor following yet. `p` is large
// (a megabyte): give it static or allocated storage.
void player_init(Player* p, PlayerSettings set);

// `p`, set up afresh with the console's settings, becomes `s`'s program: from
// `$0600` on it is the sound chip's. It takes the chip's RAM, DSP and samples
// as it starts, and the chip's own DSP stops.
void player_lead(Player* p, Sound* s);

// The player `s` runs, if it runs one.
Player* player_leading(const Sound* s);

// `p` follows `lead`, as its chip's watcher. Nothing is played until the
// leader has started. `player_unfollow` lets it go.
void player_follow(Player* p, Player* lead);
void player_unfollow(Player* p);

// What the leader's chip tells its watcher: pass every call on, if something
// else is the watcher (`player_follow` makes the follower the watcher outright).
void player_seen(Player* p, SoundSeen what, uint16_t a, uint8_t v, uint64_t cycle);

// A follower brought up to its leader: imported if it must be, then the
// samples before `t` made. (A leader runs inside `sound_run`.)
void player_run(Player* p, uint64_t t);

// A follower imports the leader again at the next run.
static inline void player_resync(Player* p) { p->synced = false; }

// A leader's DSP ticks up to and including cycle `c`, for a test that looks
// at its RAM.
void player_sync(Player* p, uint64_t c);

// The last 534 samples before the last run's `t`, stretched or squeezed to
// `count` pairs: `sound_get_samples`, from the player.
void player_get_samples(const Player* p, int16_t* out, int count);

// A quick save's share: the state, and the units in use, without the wiring.
// `player_save` writes `player_state_size` bytes; `player_state_max` is the
// most any settings take. `player_load` takes them back, and is false, leaving
// `p` as it was, if they were saved with other settings or none.
size_t player_state_size(const Player* p);
#define player_state_max (offsetof(Player, chip) + sizeof(PlayerUnit) * PLAYER_UNITS_MAX)
void player_save(const Player* p, uint8_t* out);
bool player_load(Player* p, const uint8_t* in, size_t len);

// The other sets, for the monster sounds. Once the game has its effects
// (`player_can_build`), `player_build_set` copies the game's RAM into set
// `set`'s units, and the frontend sends it the upload commands of a song with
// that set (`player_set_command`), as `apu_play_song` would; then
// `player_set_done`.
bool player_can_build(const Player* p);
void player_build_set(Player* p, int set);
void player_set_command(Player* p, int set, uint8_t cmd, uint8_t param, uint8_t param0);
void player_set_done(Player* p, int set);

// Play effect `id` drawn with set `set`'s samples. False if the set is not
// built or the player has no track for it.
bool player_play_from_set(Player* p, int set, uint8_t id);

#endif
