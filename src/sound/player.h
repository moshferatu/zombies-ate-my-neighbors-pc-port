// The game's sound driver as a player of our own: the same sequencer, written
// as plain C with no cycles counted, that can be given more of everything than
// the console had.
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
// With `player_console` (four, eight, no limit, no other sets) the player
// makes the driver's DSP writes on the driver's cycles, and so the same
// samples: `zamn_test_player` checks that on every movie.
//
// ## How it is driven
//
// The game still talks to `Sound`, the driver in C to the cycle, because it
// waits on the driver's answers. The player follows that driver. It is told,
// through `Sound.watch`, each command the driver picks up and each tick of its
// timer, with the cycle, and does the same at the same cycle. So uploads,
// songs, effects, the pause and the volume all reach it as they reach the
// driver. The frontend hears the player instead of the driver.
//
// It takes its start from the driver: its RAM, its tracks and voices, its DSP
// as it stands, and the samples it has made so far (`player_import`). That is
// done when the driver is first found running, and again after a quick load
// or a reset, which put the driver somewhere the player was not.
//
// The player counts the cycles of the driver's routines, the costs `driver.c`
// has, and makes each DSP write on its cycle: a note's volumes, sample and
// pitch over the thousand cycles the driver takes to key it on. So two notes
// in one tick are keyed on far enough apart for the DSP to take both, as on
// the console. With more tracks or voices than the driver, its loops take
// longer, as the driver's would. A tick or command starts on the driver's
// cycle, unless the player is still busy with the last: the driver runs at
// most one tick each time round its loop, so a tick that comes while it is
// busy waits for it, and two that come lose one, and the song slows. That is
// the console's way, and the player keeps it whether it is busy for its own
// reasons or the driver's.
//
// Port code: libc only.

#ifndef SOUND_PLAYER_H
#define SOUND_PLAYER_H

#include <stdbool.h>
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

// One DSP of the port's own, with eight voices and a sound RAM. A unit's RAM
// is a copy of the driver's: the live units' get every upload the game makes,
// and the other sets' units each hold that set (`player_build_set`). The echo
// writes its delay line into it, as on the console.
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
  uint64_t busy;                  // where its last tick or command ended
  bool tick_owed;                 // a tick came while it was busy
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
  PlayerSettings set;
  int units, voices;  // in use
  int tracks;         // in use: the game's, then two for each other set
  PlayerUnit unit[PLAYER_UNITS_MAX];
  PlayerBank bank[1 + PLAYER_SETS];
  PlayerTrack track[PLAYER_TRACKS_MAX];
  PlayerChannel channel[PLAYER_TRACKS_MAX][16];
  PlayerVoice voice[PLAYER_VOICES_MAX];
  uint8_t flg, mono, shift, paused;

  const Sound* real;  // the driver it follows
  bool synced;
  const char* lost;   // why it last had to import again

  uint64_t at;        // the clock of the bank playing, as the player counts it
  uint64_t host;      // the last `player_run`'s `t`
  uint64_t dsp_at;    // the next DSP tick, a multiple of 32
  uint64_t ticks;     // DSP ticks so far
  int16_t ring[PLAYER_RING * 2];

  PlayerWriteFn on_write;
  void* write_ctx;

  // Counts, for `--verbose` and the tests.
  long commands, effects_started, effects_dropped, effects_held, notes_dropped,
       voices_stolen, imports, other_played[PLAYER_SETS];
} Player;

// Set up, following `real`. Nothing is played until the driver is running.
// `p` is large (a megabyte): give it static or allocated storage.
void player_init(Player* p, const Sound* real, PlayerSettings set);

// What `real` tells its watcher: pass every call on. (`player_attach` makes the
// player the watcher outright.)
void player_seen(Player* p, SoundSeen what, uint16_t a, uint8_t v, uint64_t cycle);
void player_attach(Player* p, Sound* real);
void player_detach(Player* p, Sound* real);

// Bring the player up to `real`'s clock: import it if it must, then make the
// samples before `t`.
void player_run(Player* p, uint64_t t);

// After a quick load: import the driver again at the next run.
static inline void player_resync(Player* p) { p->synced = false; }

// The last 534 samples before the last `player_run`'s `t`, stretched or
// squeezed to `count` pairs: `sound_get_samples`, from the player.
void player_get_samples(const Player* p, int16_t* out, int count);

// The other sets, for the monster sounds. Once the driver has its effects
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
