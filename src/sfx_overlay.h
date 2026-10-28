// Sound effects the driver drops or cuts short, played through a second copy
// of the sound chip.
//
// The SPC700 program is David Warhol's 1992 driver (`AUDIO DRIVER (C) 1992
// DAVID WARHOL` at `$0603`), which `src/sound/driver.c` is. It plays
// MIDI-like sequences: the song is one and each sound effect is another, and
// they share four sequence slots and the DSP's eight voices. Two things lose
// an effect, and both happen whenever a fight gets busy:
//
//   * **The effect is not started.** Command `$01` (and `$11`) lands in
//     `$0B8A`, which looks for a free slot from 3 down to 0 and gives up at
//     `$0B9B` when there is none. The song holds one slot, so a fourth effect
//     is dropped whole. `level9-weapons.zmv` drops effect `$14` 121 times.
//   * **A note loses its voice.** A note-on (`$098C`) takes a free voice if
//     there is one (`$0D60`). If there is none, it takes the least recently
//     used voice whose priority (`$B8+v`) is no higher than its own, or is
//     itself dropped (`$0D84`). The song's lead channels are priority `$78`
//     and effects are `$64`, so the music takes voices from the effects,
//     and effects take them from each other.
//
// So this runs a second sound chip beside the real one. It is a copy of the
// real one, taken once the driver is running, and it gets every command the
// game sends except "play the song": the same uploads, effects and stops, on
// the same cycles. It plays the effects and never the music, with all four
// slots and all eight voices to itself. Its voices run all the time but are
// muted (`SoundDsp.voice_mute`) while the real one is playing the same note.
// A voice becomes audible when the real driver drops its effect, drops its
// note, or gives its voice to something else. Because the voice has been
// running in step all along, the sound carries on from where the real one
// stopped rather than starting again. The idea is Devil's Crush's music voice
// overlay turned round: there the effects cost the music its channels, here
// the music and the other effects cost the effects their voices.
//
// It only reads the real machine. It watches the real driver through the
// sound's watcher (`Sound.watch`), at the places `src/sound/driver.c` marks
// for it, and it never writes the real sound, WRAM or a save state. With it
// on or off, the game runs identically.
//
// ## Keeping the two in step
//
// The real driver runs ahead of the game, and every event it has is logged
// with its cycle: a command picked up (`$0B1D`), an effect started or dropped,
// a voice stolen or a note dropped. After each tick the copy is run up to the
// game's cycle, event by event: up to an event's cycle, then the event. Each
// command is put on the copy's ports once the copy has taken the last one,
// and the real driver's losses are applied at the cycle they happened on. So
// the copy is never ahead of the real one, and whatever the real driver did
// to a command is known before the copy sees the command.
//
// The copy has only four slots too, and in a busy fight they fill up with
// effects that the real driver is also playing, held muted in case it loses
// one of their voices. So an effect the real driver dropped can take the slot
// of the oldest of those (`sfxo_make_room`). A dropped effect is not brought
// back while it is already heard three times between the two (`sfxo_enough`):
// a weapon that fills the driver with its own sound sounds the way it always
// did.
//
// ## Limits
//
//   * The copy starts from the real one's state, so an effect already lost
//     when it is taken (at boot, and after a load or a reset) stays lost.
//   * An effect the real driver started and the copy had to let go, or dropped
//     itself, cannot be brought back if the real one loses a voice of it later.
//   * The copy's echo is fed only by its open voices. A voice opened part way
//     through a note echoes from then on.
//   * The copy's DSP keeps step with its driver, which runs ahead of the game
//     to its next read of a port. So a voice opened at the cycle the real
//     driver lost it can have had a few more samples made muted: it is heard
//     from up to one pass of the driver's loop late, under 2 ms but for one
//     with a tick in it.
//
// Effects and voices are matched through a serial number given to each play
// command. It is recorded against the slot the command gets on each side, and
// against the voice each of that slot's notes gets on the copy. A lost note
// is found by serial, MIDI channel and note number.
//
// A load or a reset breaks the copy's timeline. The clock shows it
// (`sfx_overlay_tick`), and the frontend also says so after a quick load
// (`sfx_overlay_resync`). Either way the copy is taken again, and the song
// is stopped on it with command `$02`.
#ifndef ZAMN_SFX_OVERLAY_H
#define ZAMN_SFX_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sound/sound.h"

// Where the driver does the things this watches. `src/sound/driver.c` marks
// each, with the register said.
#define SFXO_PICKUP 0x0B1Du     // MOV $F7,X: the command in $C6/$C7/$C8, its counter in X
#define SFXO_NO_SLOT 0x0B9Bu    // SETC: no free slot, or no such sequence
#define SFXO_SLOT 0x0B9Du       // a free slot, in X
#define SFXO_STEAL_PASS 0x0D70u // no free voice; the search by priority starts
#define SFXO_NO_VOICE 0x0D84u   // ...and found nothing
#define SFXO_VOICE 0x0D87u      // a voice, at $E1+X in the use order
#define SFXO_KEY_ON 0x0D97u     // voice Y claimed for note $CB of sequence $D6

// The driver's zero page.
#define SFXO_SEQ_ID 0x3C     // +slot: the sequence each slot plays
#define SFXO_SEQ_CH 0x34     // +slot: its current MIDI channel
#define SFXO_VOICE_ON 0x48   // +voice: note held
#define SFXO_VOICE_SEQ 0x50  // +voice: the slot playing it
#define SFXO_VOICE_CH 0x58   // +voice: the MIDI channel
#define SFXO_VOICE_NOTE 0x60 // +voice: the note
#define SFXO_CMD 0xC6
#define SFXO_PARAM 0xC7
#define SFXO_PARAM0 0xC8
#define SFXO_LAST_SEQ 0xC9   // the counter of the last command taken
#define SFXO_SEQ_COUNT 0xCA  // sequences registered
#define SFXO_NOTE 0xCB
#define SFXO_CUR_SEQ 0xD6
#define SFXO_EFFECTS 0xDE    // sequences registered when command $0D came: the effects
#define SFXO_USE_ORDER 0xE1  // eight voices, least recently used first

#define SFXO_CMD_PLAY 0x01
#define SFXO_CMD_STOP 0x02
#define SFXO_CMD_PLAY_LOOP 0x11
#define SFXO_CMD_NOTHING 0x00 // `$0B5E`, an RTS

#define SFXO_QUEUE 8192       // events between two ticks; an upload sends thousands
#define SFXO_SERIALS 256      // play commands whose outcome is remembered
#define SFXO_MAX_BEHIND 60000 // cycles the copy may trail by before it is taken again
#define SFXO_PENDING 8        // dropped notes the copy has not reached yet
#define SFXO_PENDING_LIFE 8192
#define SFXO_NONE 0xFFFFFFFFu  // no play command
#define SFXO_MAX_SAME 3        // instances of one effect heard at once (`sfxo_enough`)

enum { SFXO_EV_COMMAND, SFXO_EV_STEAL, SFXO_EV_DROP_NOTE };
enum { SFXO_UNKNOWN, SFXO_STARTED, SFXO_DROPPED, SFXO_SONG };

typedef struct {
  uint64_t at;     // the real driver's cycle
  uint8_t type;
  uint8_t cmd, param, param0;
  uint32_t serial; // a play command's, or the lost note's sequence's
  uint8_t ch, note;
} SfxoEvent;

typedef struct {
  uint32_t serial;
  uint64_t until;
  uint8_t ch, note;
  bool live;
} SfxoPending;

typedef struct {
  Sound* real;
  Sound* copy;
  bool enabled;
  bool synced;
  bool overflow;

  SfxoEvent queue[SFXO_QUEUE];
  int head, tail;

  uint32_t next_serial;
  uint8_t outcome[SFXO_SERIALS];      // what the real driver did with each play command
  uint32_t real_slot[4];              // the serial each real slot plays
  uint32_t real_cur;                  // the play command the real driver is handling
  bool real_steal;                    // between $0D70 and its outcome

  uint32_t copy_slot[4];
  uint32_t copy_cur;
  uint32_t copy_voice[8];             // the serial of the note each voice was given
  uint8_t copy_ch[8], copy_note[8];
  SfxoPending pending[SFXO_PENDING];

  // For `--verbose` and the test.
  long effects_dropped, notes_dropped, voices_stolen, voices_opened, made_room, resyncs;
  const char* lost;  // why the copy was last taken again
} SfxOverlay;

static inline void sfxo_push(SfxOverlay* o, SfxoEvent e) {
  int next = (o->tail + 1) % SFXO_QUEUE;
  if (next == o->head) { o->overflow = true; return; }
  o->queue[o->tail] = e;
  o->tail = next;
}

// The real driver, at `pc`, with `reg` the register the mark carries.
static inline void sfxo_real_at(SfxOverlay* o, uint16_t pc, uint8_t reg, uint64_t cycle) {
  const uint8_t* r = o->real->ram;
  switch (pc) {
  case SFXO_PICKUP: {
    SfxoEvent e = {cycle, SFXO_EV_COMMAND, r[SFXO_CMD], r[SFXO_PARAM], r[SFXO_PARAM0],
                   0, 0, 0};
    if (e.cmd == SFXO_CMD_PLAY || e.cmd == SFXO_CMD_PLAY_LOOP) {
      e.serial = o->real_cur = o->next_serial++;
      o->outcome[e.serial % SFXO_SERIALS] =
          e.param >= r[SFXO_EFFECTS] ? SFXO_SONG : SFXO_UNKNOWN;
    }
    sfxo_push(o, e);
    return;
  }
  case SFXO_SLOT:
    o->real_slot[reg & 3] = o->real_cur;
    if (o->outcome[o->real_cur % SFXO_SERIALS] == SFXO_UNKNOWN)
      o->outcome[o->real_cur % SFXO_SERIALS] = SFXO_STARTED;
    return;
  case SFXO_NO_SLOT:
    if (r[SFXO_PARAM] < r[SFXO_SEQ_COUNT] &&
        o->outcome[o->real_cur % SFXO_SERIALS] == SFXO_UNKNOWN) {
      o->outcome[o->real_cur % SFXO_SERIALS] = SFXO_DROPPED;
      o->effects_dropped++;
    }
    return;
  case SFXO_STEAL_PASS:
    o->real_steal = true;
    return;
  case SFXO_NO_VOICE: {
    if (!o->real_steal) return;
    o->real_steal = false;
    const uint8_t slot = r[SFXO_CUR_SEQ] & 3;
    const uint32_t k = o->real_slot[slot];
    if (o->outcome[k % SFXO_SERIALS] == SFXO_SONG) return;
    SfxoEvent e = {cycle, SFXO_EV_DROP_NOTE, 0, 0, 0, k, r[SFXO_SEQ_CH + slot], r[SFXO_NOTE]};
    sfxo_push(o, e);
    o->notes_dropped++;
    return;
  }
  case SFXO_VOICE: {
    if (!o->real_steal) return;
    o->real_steal = false;
    const uint8_t v = r[SFXO_USE_ORDER + (reg & 7)] & 7;
    const uint32_t k = o->real_slot[r[SFXO_VOICE_SEQ + v] & 3];
    if (o->outcome[k % SFXO_SERIALS] == SFXO_SONG) return;
    SfxoEvent e = {cycle, SFXO_EV_STEAL, 0, 0, 0, k, r[SFXO_VOICE_CH + v],
                   r[SFXO_VOICE_NOTE + v]};
    sfxo_push(o, e);
    o->voices_stolen++;
    return;
  }
  }
}

static inline void sfxo_open(SfxOverlay* o, int v) {
  if (o->copy->dsp.voice_mute & (1 << v)) o->voices_opened++;
  o->copy->dsp.voice_mute &= (uint8_t)~(1 << v);
}

// The copy has four slots, and most of what is in them the real driver is
// playing too, muted and only held in case the real one loses a voice of it.
// An effect the real driver dropped outranks that. If there is no free slot
// for one, the oldest sequence of that kind with none of its voices open is
// let go, the way the driver would forget it: slot and voices marked free. Its
// voices are muted, so their notes can be left to run until they are reused.
//
// Only at the pickup, from the driver's main loop between two of its ticks,
// so nothing is part way through the slot.
static inline void sfxo_make_room(SfxOverlay* o) {
  uint8_t* r = o->copy->ram;
  int oldest = -1;
  for (int i = 0; i < 4; i++) {
    if (!(r[0x28 + i] | r[0x2C + i])) return;  // one is free
    if (o->outcome[o->copy_slot[i] % SFXO_SERIALS] != SFXO_STARTED) continue;
    bool heard = false;
    for (int v = 0; v < 8; v++)
      heard |= r[SFXO_VOICE_ON + v] && (r[SFXO_VOICE_SEQ + v] & 3) == i &&
               !(o->copy->dsp.voice_mute & (1 << v));
    if (heard) continue;
    if (oldest < 0 || (int32_t)(o->copy_slot[i] - o->copy_slot[oldest]) < 0) oldest = i;
  }
  if (oldest < 0) return;
  r[0x28 + oldest] = r[0x2C + oldest] = 0;
  for (int v = 0; v < 8; v++)
    if (r[SFXO_VOICE_ON + v] && (r[SFXO_VOICE_SEQ + v] & 3) == oldest) {
      r[SFXO_VOICE_ON + v] = 0;
      o->copy_voice[v] = SFXO_NONE;
    }
  o->made_room++;
}

// The copy.
static inline void sfxo_copy_at(SfxOverlay* o, uint16_t pc, uint8_t reg) {
  const uint8_t* r = o->copy->ram;
  switch (pc) {
  case SFXO_PICKUP:
    if (o->copy_cur != SFXO_NONE && o->outcome[o->copy_cur % SFXO_SERIALS] == SFXO_DROPPED)
      sfxo_make_room(o);
    return;
  case SFXO_SLOT:
    o->copy_slot[reg & 3] = o->copy_cur;
    return;
  case SFXO_KEY_ON: {
    const int v = reg & 7;
    const uint8_t slot = r[SFXO_CUR_SEQ] & 3;
    const uint32_t k = o->copy_slot[slot];
    o->copy_voice[v] = k;
    o->copy_ch[v] = r[SFXO_SEQ_CH + slot];
    o->copy_note[v] = r[SFXO_NOTE];
    bool open = k != SFXO_NONE && o->outcome[k % SFXO_SERIALS] == SFXO_DROPPED;
    for (int i = 0; i < SFXO_PENDING && !open; i++) {
      SfxoPending* p = &o->pending[i];
      if (p->live && p->serial == k && p->ch == o->copy_ch[v] && p->note == o->copy_note[v]) {
        p->live = false;
        open = true;
      }
    }
    if (open) sfxo_open(o, v);
    else o->copy->dsp.voice_mute |= (uint8_t)(1 << v);
    return;
  }
  }
}

static void sfxo_real_watch(void* ctx, SoundSeen what, uint16_t a, uint8_t v, uint64_t cycle) {
  SfxOverlay* o = (SfxOverlay*)ctx;
  if (what == SOUND_SEEN_AT && o->synced) sfxo_real_at(o, a, v, cycle);
}

static void sfxo_copy_watch(void* ctx, SoundSeen what, uint16_t a, uint8_t v, uint64_t cycle) {
  (void)cycle;
  if (what == SOUND_SEEN_AT) sfxo_copy_at((SfxOverlay*)ctx, a, v);
}

// The driver's banner, as the loader left it.
static inline bool sfxo_driver_ok(const uint8_t* ram) {
  static const uint8_t banner[6] = {'A', 'U', 'D', 'I', 'O', ' '};
  return !memcmp(ram + 0x0603, banner, sizeof banner);
}

static inline void sfx_overlay_init(SfxOverlay* o, Sound* real, bool enabled) {
  memset(o, 0, sizeof *o);
  o->real = real;
  o->enabled = enabled;
  if (!enabled) return;
  o->copy = (Sound*)malloc(sizeof *o->copy);
  if (!o->copy) { o->enabled = false; return; }
  sound_reset(o->copy);
  real->watch = sfxo_real_watch;
  real->watch_ctx = o;
}

static inline void sfx_overlay_free(SfxOverlay* o) {
  if (!o->copy) return;
  if (o->real->watch == sfxo_real_watch) {
    o->real->watch = NULL;
    o->real->watch_ctx = NULL;
  }
  free(o->copy);
  o->copy = NULL;
}

// After a load: take the copy again at the next tick.
static inline void sfx_overlay_resync(SfxOverlay* o) { o->synced = false; }

// Take the copy, stop the song on it and mute everything it was playing, which
// the real one goes on playing.
static inline void sfxo_take(SfxOverlay* o) {
  sound_copy(o->copy, o->real);
  o->copy->watch = sfxo_copy_watch;
  o->copy->watch_ctx = o;
  memset(o->copy->ring, 0, sizeof o->copy->ring);
  o->copy->dsp.voice_mute = 0xFF;

  o->head = o->tail = 0;
  o->overflow = false;
  o->real_steal = false;
  memset(o->pending, 0, sizeof o->pending);
  const uint8_t* r = o->real->ram;
  for (int i = 0; i < 4; i++) {
    const uint32_t k = o->next_serial++;
    o->real_slot[i] = o->copy_slot[i] = k;
    o->outcome[k % SFXO_SERIALS] =
        r[SFXO_SEQ_ID + i] >= r[SFXO_EFFECTS] ? SFXO_SONG : SFXO_STARTED;
  }
  o->copy_cur = o->real_cur;
  for (int v = 0; v < 8; v++) {
    o->copy_voice[v] = o->copy_slot[r[SFXO_VOICE_SEQ + v] & 3];
    o->copy_ch[v] = r[SFXO_VOICE_CH + v];
    o->copy_note[v] = r[SFXO_VOICE_NOTE + v];
  }
  // The song, stopped on the copy. Its slot is found by id, as the game would.
  for (int i = 0; i < 4; i++) {
    if (!(r[0x28 + i] | r[0x2C + i]) || r[SFXO_SEQ_ID + i] < r[SFXO_EFFECTS]) continue;
    SfxoEvent e = {o->real->host, SFXO_EV_COMMAND, SFXO_CMD_STOP, r[SFXO_SEQ_ID + i], 0,
                   0, 0, 0};
    sfxo_push(o, e);
  }
  o->synced = true;
  o->resyncs++;
}

// Find the copy's voice for a note the real driver has lost.
static inline int sfxo_find(const SfxOverlay* o, uint32_t k, uint8_t ch, uint8_t note) {
  int found = -1;
  for (int v = 0; v < 8; v++) {
    if (o->copy_voice[v] != k || o->copy_ch[v] != ch || o->copy_note[v] != note) continue;
    if (o->copy->ram[SFXO_VOICE_ON + v]) return v;
    found = v;
  }
  return found;
}

// Whether an effect the real driver dropped is already heard as often as it
// can be on a console with nothing else playing: three times, in the three
// slots the song leaves. A weapon that fires faster than its sound ends fills
// them all with itself. That is its sound on the console too, so the copy does
// not stack a fourth and fifth on top of it.
static inline bool sfxo_enough(const SfxOverlay* o, const SfxoEvent* e) {
  if (o->outcome[e->serial % SFXO_SERIALS] != SFXO_DROPPED) return false;
  const uint8_t* r = o->real->ram;
  const uint8_t* c = o->copy->ram;
  int heard = 0;
  for (int i = 0; i < 4; i++) {
    if ((r[0x28 + i] | r[0x2C + i]) && r[SFXO_SEQ_ID + i] == e->param) heard++;
    if ((c[0x28 + i] | c[0x2C + i]) && c[SFXO_SEQ_ID + i] == e->param &&
        o->outcome[o->copy_slot[i] % SFXO_SERIALS] == SFXO_DROPPED)
      heard++;
  }
  return heard >= SFXO_MAX_SAME;
}

// One event, the copy brought up to its cycle first. False if it is a
// command and the copy has not taken the last one by `limit`.
static inline bool sfxo_apply(SfxOverlay* o, const SfxoEvent* e, uint64_t limit) {
  Sound* c = o->copy;
  if (e->at > c->host) sound_run(c, e->at);
  if (e->type == SFXO_EV_COMMAND) {
    // One at a time: the last one has to have been taken.
    // The driver is stopped at a read of a port; it is let make that read,
    // and the next, until it has. So it takes the command when it would have.
    while (c->written[3] != c->ram[SFXO_LAST_SEQ]) {
      if (c->now >= limit || c->state == SOUND_HALTED) return false;
      sound_run(c, c->now + 1);
    }
    uint8_t cmd = e->cmd;
    bool play = cmd == SFXO_CMD_PLAY || cmd == SFXO_CMD_PLAY_LOOP;
    if (play && (e->param >= c->ram[SFXO_EFFECTS] || sfxo_enough(o, e))) {
      cmd = SFXO_CMD_NOTHING;
      play = false;
    }
    o->copy_cur = play ? e->serial : SFXO_NONE;
    sound_write_port(c, 0, e->param0);
    sound_write_port(c, 1, e->param);
    sound_write_port(c, 2, cmd);
    sound_write_port(c, 3, (uint8_t)(c->written[3] + 1));
    return true;
  }
  const int v = sfxo_find(o, e->serial, e->ch, e->note);
  if (v >= 0) {
    sfxo_open(o, v);
  } else if (e->type == SFXO_EV_DROP_NOTE) {
    // The copy has not got to this note yet.
    for (int i = 0; i < SFXO_PENDING; i++)
      if (!o->pending[i].live) {
        o->pending[i] = (SfxoPending){e->serial, c->host + SFXO_PENDING_LIFE, e->ch, e->note, true};
        break;
      }
  }
  return true;
}

// After each tick: bring the copy up to the game's cycle, and through every
// event the real driver had before it.
static inline void sfx_overlay_tick(SfxOverlay* o) {
  if (!o->enabled) return;
  Sound* r = o->real;
  Sound* c = o->copy;
  if (o->synced) {
    o->lost = NULL;
    // A load or a reset puts the real clock back; the copy is then ahead.
    if (c->host > r->host) o->lost = "the real clock went back";
    else if (r->host - c->host > SFXO_MAX_BEHIND) o->lost = "the copy fell behind";
    else if (o->overflow) o->lost = "too many events in one tick";
    else if (c->state == SOUND_HALTED) o->lost = c->fault;
    if (o->lost) o->synced = false;
  }
  if (!o->synced) {
    // Taken only with nothing on the ports the real driver has not taken, so
    // every command from here on is one the copy is given. (After the loader,
    // port 3 holds what it was left with, which is not a command; the game's
    // first one overwrites it before the driver's first poll.)
    if (!sound_driver_running(r) || !sfxo_driver_ok(r->ram) ||
        r->written[3] != r->ram[SFXO_LAST_SEQ])
      return;
    sfxo_take(o);
  }
  const uint64_t now = r->host;
  while (o->head != o->tail && o->queue[o->head].at <= now) {
    if (!sfxo_apply(o, &o->queue[o->head], now)) break;
    o->head = (o->head + 1) % SFXO_QUEUE;
  }
  if (now > c->host) sound_run(c, now);
  for (int i = 0; i < SFXO_PENDING; i++)
    if (o->pending[i].live && c->host >= o->pending[i].until) o->pending[i].live = false;
}

// Add what the copy has to say to a tick's samples, before the volume.
static inline void sfx_overlay_mix(SfxOverlay* o, int16_t* buf, int samples) {
  if (!o->enabled || !o->synced) return;
  int16_t add[2048 * 2];
  if (samples > 2048) samples = 2048;
  sound_get_samples(o->copy, add, samples);
  for (int i = 0; i < samples * 2; i++) {
    const int v = buf[i] + add[i];
    buf[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
  }
}

#endif  // ZAMN_SFX_OVERLAY_H
