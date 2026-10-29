// The game's sound driver as a player of our own: see `player.h`.
//
// Each routine is the driver's, as `driver.c` has it, with the state in
// structs instead of the sound RAM and the arithmetic kept to the bit: the
// 8-bit wraps, the order things are written in, and the quirks. The cycles are
// `driver.c`'s too, spent the same way: a cost written as a sum is the
// instructions in order, `+ 8` before a call is the CALL, and every routine
// spends its own RET. Where a routine is the driver's, its address is given.
//
// Port code: libc only.

#include "sound/player.h"

#include <string.h>

#include "sound/driver_ram.h"

const PlayerSettings player_console = {4, 8, 0, false};
const PlayerSettings player_wide = {8, 24, 3, false};

#define DRIVER_PICKUP 0x0B1D  // `driver.c` marks the command picked up here...
#define DRIVER_TICK 0x06B5    // ...each tick of its timer here...
#define DRIVER_START 0x0600   // ...and the driver starting here
#define PICKUP_TO_COMMAND 35  // from the mark to the command's handler
#define BUSY_TO_TICK 44       // from the end of a tick to the next, round the loop
#define BUSY_TO_PICKUP 11     // ...and to the poll
#define ECHO_CLEAR_FROM 0xE7  // the page command $08 clears from
#define SLEEP_TICKS 64000     // two seconds of nothing from a unit, echo and all

static void spend(Player* p, int cycles) { p->at += (uint64_t)cycles; }

// The last of a loop's iterations ends on a branch not taken.
static int loop_end(bool more) { return more ? 4 : 2; }

// ---------------------------------------------------------------------------
// The units: a DSP and a RAM each
// ---------------------------------------------------------------------------

static int clamp16(int v) { return v < -0x8000 ? -0x8000 : v > 0x7FFF ? 0x7FFF : v; }

static bool unit_silent(const PlayerUnit* n) {
  for (int v = 0; v < 8; v++)
    if (n->dsp.voice[v].level || n->dsp.voice[v].start_delay || n->dsp.voice[v].key_on)
      return false;
  return true;
}

// One tick of every unit, added. Unit 0 always runs, as the console's DSP
// does; the others sleep after two seconds of silence and wake at a key-on.
static void dsp_tick(Player* p) {
  int l = 0, r = 0;
  for (int u = 0; u < p->units; u++) {
    PlayerUnit* n = &p->unit[u];
    if (u > 0 && n->quiet >= SLEEP_TICKS) continue;
    sound_dsp_sample(&n->dsp);
    l += n->dsp.out_l;
    r += n->dsp.out_r;
    if (!unit_silent(n)) n->quiet = 0;
    else if (n->quiet < SLEEP_TICKS) n->quiet++;
  }
  const size_t k = (size_t)(p->ticks % PLAYER_RING);
  p->ring[k * 2] = (int16_t)clamp16(l);
  p->ring[k * 2 + 1] = (int16_t)clamp16(r);
  p->ticks++;
  p->dsp_at += 32;
}

// The DSP's ticks up to and including cycle `c`.
static void catch_up(Player* p, uint64_t c) {
  while (p->dsp_at <= c) dsp_tick(p);
}

static uint8_t* bank_ram(Player* p, int b) { return p->unit[p->bank[b].first_unit].ram; }

// A byte of a bank's RAM, in every unit of it, on the current instruction's
// cycle `at`: the game's bank is being played from as it is written.
static void bank_put(Player* p, int b, int at, uint16_t adr, uint8_t val) {
  const PlayerBank* k = &p->bank[b];
  if (b == 0) catch_up(p, p->at + (uint64_t)at);
  for (int u = k->first_unit; u < k->first_unit + k->units; u++) p->unit[u].ram[adr] = val;
}

static int unit_of(int v) { return v >> 3; }
static uint8_t bit_of(int v) { return (uint8_t)(1 << (v & 7)); }

// $0EF1: MOV $F2, Y : MOV $F3, A : RET -- to unit `u`, or every unit for -1.
static void dsp_set(Player* p, int u, uint8_t reg, uint8_t val) {
  spend(p, 4);
  const uint64_t c = p->at + 3;
  catch_up(p, c);
  if (p->on_write) p->on_write(p->write_ctx, u, reg, val, c);
  for (int i = u < 0 ? 0 : u; i < (u < 0 ? p->units : u + 1); i++) {
    sound_dsp_write(&p->unit[i].dsp, reg, val);
    if (reg == DSP_KON && val) p->unit[i].quiet = 0;
  }
  spend(p, 4 + 5);
}

// $0EEB: one of voice `v`'s registers.
static void voice_set(Player* p, int v, uint8_t reg, uint8_t val) {
  spend(p, 4 + 2 + 3 + 2 + 4);
  dsp_set(p, unit_of(v), (uint8_t)((v & 7) << 4 | reg), val);
}

static const uint8_t global_regs[] = {
    DSP_MVOL_L, DSP_MVOL_R, DSP_EVOL_L, DSP_EVOL_R, DSP_EFB, DSP_PMON,
    DSP_DIR,    DSP_ESA,    DSP_EDL,    DSP_FLG,    0x0F,   0x1F,
    0x2F,       0x3F,       0x4F,       0x5F,       0x6F,   0x7F,
};

// A unit started afresh with the chip's registers as `from` has them, no voice
// playing, and its echo's delay line empty.
static void unit_fresh(PlayerUnit* n, const SoundDsp* from) {
  sound_dsp_reset(&n->dsp, n->ram);
  for (size_t i = 0; i < sizeof global_regs; i++)
    sound_dsp_write(&n->dsp, global_regs[i], from->reg[global_regs[i]]);
  const uint32_t len = n->dsp.echo_delay ? n->dsp.echo_delay : 4;
  for (uint32_t i = 0; i < len; i++) n->ram[(uint16_t)(n->dsp.echo_start + i)] = 0;
  n->eon = n->non = 0;
  n->quiet = SLEEP_TICKS;
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

// $0EF6: the signed 16-bit `m` times the signed 8-bit `a`, in 24 bits, of
// which the driver uses the top two bytes.
static uint16_t mul_hi(Player* p, uint16_t m, uint8_t a) {
  int sign = 0;
  spend(p, 4 + 2 + 4 + 3);
  if (m & 0x8000) {
    spend(p, 2 + 4 + 2 + 2 + 3 + 4 + 2 + 3 + 4);
    sign++;
    m = (uint16_t)-m;
  } else {
    spend(p, 4);
  }
  spend(p, 4 + 2);
  if (a >= 0x80) {
    spend(p, 2 + 4 + 2 + 2);
    sign++;
    a = (uint8_t)-a;
  } else {
    spend(p, 4);
  }
  uint32_t prod = (uint32_t)a * (m & 0xFF) + ((uint32_t)a * (m >> 8) << 8);
  spend(p, 4 + 3 + 9 + 4 + 4 + 4 + 3 + 9 + 2 + 3 + 4 + 2 + 2 + 4 + 3 + 2);
  if (sign & 1) {
    spend(p, 2 + 2 + 2 + 3 + 4 + 2 + 3 + 4 + 2 + 3 + 4);
    prod = (uint32_t)-(int32_t)prod;
  } else {
    spend(p, 4);
  }
  spend(p, 5);
  return (uint16_t)(prod >> 8);
}

// The pitch table, a semitone to the word, at byte `index`. A key past the
// table reads what follows it.
static uint16_t pitch_at(const uint8_t* ram, int index) {
  return (uint16_t)(ram[(uint16_t)(PITCHES + index)] | ram[(uint16_t)(PITCHES + index + 1)] << 8);
}

// $0E1B: a bend of `amount` over `range` semitones, both negated if the bend
// is down.
static void bend_sign(Player* p, uint8_t* range, uint8_t* amount) {
  spend(p, 4);
  if (*amount & 0x80) {
    spend(p, 2 + 2 + 2 + 3 + 4 + 2 + 2 + 3 + 4);
    *range = (uint8_t)-*range;
    *amount = (uint8_t)-*amount;
  } else {
    spend(p, 4);
  }
  spend(p, 5);
}

// $0E13: a channel's pitch wheel, as a range and a bend.
static void channel_bend(Player* p, const PlayerChannel* c, uint8_t* range, uint8_t* amount) {
  spend(p, 5 + 4 + 5);
  *range = c->bend_range;
  *amount = c->bend;
  bend_sign(p, range, amount);
}

// $0E2E: how far a voice keyed at `key` moves for a bend of `amount` over
// `range` semitones, doubled.
static uint16_t bend_pitch(Player* p, const uint8_t* ram, uint8_t key, uint8_t range,
                           uint8_t amount) {
  spend(p, 4 + 4 + 2 + 5 + 4);
  const uint8_t far = (uint8_t)((uint8_t)(key + range) << 1), near = (uint8_t)(key << 1);
  const uint16_t d = (uint16_t)(pitch_at(ram, far) - pitch_at(ram, near));
  spend(p, 5 + 2 + 3 + 2 + 2 + 5 + 4 + 5 + 4);
  spend(p, 5 + 2 + 2 + 2 + 3 + 5 + 4 + 3 + 5 + 4);
  spend(p, 3 + 8);
  const uint16_t bend = (uint16_t)(mul_hi(p, d, amount) << 1);
  spend(p, 2 + 4 + 4 + 4 + 4 + 5);
  return bend;
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------

static PlayerChannel* channel_of(Player* p, int t) {
  return &p->channel[t][p->track[t].channel];
}

static uint8_t* voice_ram(Player* p, int v) { return p->unit[unit_of(v)].ram; }

// A bank's voices are its units', in order.
static int first_voice(const PlayerBank* k) { return k->first_unit * 8; }
static int voice_count(const PlayerBank* k) { return k->units * 8; }
static PlayerBank* bank_of(Player* p, int t) { return &p->bank[p->track[t].bank]; }

// $0D57: the track's current channel picked out. Only its cost is left.
static void track_channel(Player* p) { spend(p, 2 + 5 + 2 + 4 + 2 + 4 + 5); }

// $0D9E: a voice made the current one. Only its cost is left.
static void voice_select(Player* p) { spend(p, 4 + 5 + 4 + 2 + 5 + 4 + 5); }

// $0DAA
static void voice_free(Player* p, int v) {
  p->voice[v].on = false;
  spend(p, 2 + 6 + 5);
}

// $0DDD
static void voice_key_off(Player* p, int v) {
  spend(p, 3 + 2 + 8);
  dsp_set(p, unit_of(v), DSP_KOFF, bit_of(v));
  spend(p, 5);
}

// Voice `v` let go and keyed off, as four routines do it: selected, freed,
// keyed off.
static void voice_off(Player* p, int v) {
  voice_select(p);
  spend(p, 8);
  voice_free(p, v);
  spend(p, 8);
  voice_key_off(p, v);
}

// $0DB0: voice `v` keyed on, with its echo and noise bits.
static void voice_key_on(Player* p, int v) {
  PlayerUnit* n = &p->unit[unit_of(v)];
  const uint8_t bit = bit_of(v);
  spend(p, 2 + 2 + 8);
  dsp_set(p, unit_of(v), DSP_KOFF, 0);
  spend(p, 3 + 2 + 8);
  dsp_set(p, unit_of(v), DSP_KON, bit);
  n->eon = (uint8_t)((n->eon & ~bit) | (p->voice[v].echo ? bit : 0));
  spend(p, 3 + 2 + 3 + 3 + 4 + 2 + 8);
  dsp_set(p, unit_of(v), DSP_EON, n->eon);
  n->non = (uint8_t)((n->non & ~bit) | (p->voice[v].noise ? bit : 0));
  spend(p, 3 + 2 + 3 + 3 + 4 + 2 + 8);
  dsp_set(p, unit_of(v), DSP_NON, n->non);
  spend(p, 5);
}

// $0D60: a voice of bank `bank`'s units for a note of priority `priority`: the
// least recently used free one, or the least recent whose priority is no
// higher. -1 for none; else the voice, with its place in the order.
static int voice_find(Player* p, const PlayerBank* k, uint8_t priority, int* place) {
  const int n = voice_count(k);
  spend(p, 4 + 2);
  for (int i = 0; i < n; i++) {
    const int v = k->order[i];
    spend(p, 4 + 2 + 5);
    if (!p->voice[v].on) {
      spend(p, 4 + 2 + 4 + 2 + 5);
      *place = i;
      return v;
    }
    spend(p, 2 + 2 + 2 + loop_end(i < n - 1));
  }
  spend(p, 2);
  for (int i = 0; i < n; i++) {
    const int v = k->order[i];
    spend(p, 4 + 2 + 3 + 5);
    if (priority >= p->voice[v].priority) {
      spend(p, 4 + 2 + 4 + 2 + 5);
      *place = i;
      p->voices_stolen++;
      return v;
    }
    spend(p, 2 + 4 + 2 + 2 + loop_end(i < n - 1));
  }
  spend(p, 4 + 2 + 5);
  return -1;
}

// $0D8B: voice `v`, at `place`, goes to the end of the order, is marked on, and
// becomes the current voice.
static void voice_claim(Player* p, PlayerBank* k, int place, int v) {
  const int last = voice_count(k) - 1;
  spend(p, 2);
  for (int i = place; i != last; i++) {
    k->order[i] = k->order[i + 1];
    spend(p, 2 + 2 + 4 + 5 + 2 + 4);
  }
  spend(p, 2 + 4);
  k->order[last] = (uint8_t)v;
  p->voice[v].on = true;
  spend(p, 4 + 2 + 6);
  voice_select(p);
}

// Is voice `v` playing for track `t`'s current channel? The test four handlers
// make, $0823 and its kin. Spends the test.
static bool voice_is(Player* p, int v, int t) {
  const PlayerVoice* vo = &p->voice[v];
  spend(p, 5);
  if (!vo->on) { spend(p, 4); return false; }
  spend(p, 2 + 2 + 5);
  if (vo->track != t) { spend(p, 4); return false; }
  spend(p, 2 + 4 + 5);
  if (vo->channel != p->track[t].channel) { spend(p, 4); return false; }
  spend(p, 2);
  return true;
}

// $0E92: every voice of track `t` let go.
static void track_release(Player* p, int t) {
  const int first = first_voice(bank_of(p, t));
  spend(p, 2 + 4);
  for (int v = first + voice_count(bank_of(p, t)) - 1; v >= first; v--) {
    spend(p, 5);
    if (p->voice[v].on) {
      spend(p, 2 + 5 + 3);
      if (p->voice[v].track == t) {
        spend(p, 2 + 4 + 8);
        voice_off(p, v);
        spend(p, 4);
      } else {
        spend(p, 4);
      }
    } else {
      spend(p, 4);
    }
    spend(p, 2 + loop_end(v > first));
  }
  spend(p, 5);
}

// $0DE5: one side's volume, from a pan of `pan` (0 to $7F, $40 the middle).
// The carry it leaves is the other side's borrow.
static uint8_t pan_volume(Player* p, int v, uint8_t pan, bool* carry) {
  const PlayerVoice* vo = &p->voice[v];
  if (p->mono) pan = 0x40;
  const uint8_t side = pan >= 0x40 ? (uint8_t)(pan - 0x40) : 0;
  const uint8_t y = (uint8_t)(0x3F - side);
  uint16_t m = (uint16_t)(y * p->track[vo->track].volume);
  m = (uint16_t)((uint8_t)(m >> 8 << 2) * vo->velocity);
  m = (uint16_t)((uint8_t)(m >> 8) * p->channel[vo->track][vo->channel].volume);
  *carry = m & 0x8000;
  // The same on every path.
  spend(p, 4 + 2 + 4 + 2 + 2 + 4);
  spend(p, 4 + 2 + 2 + 3 + 2 + 4 + 9 + 2 + 2 + 2 + 3 + 9 + 2 + 4 + 3 + 5 + 4 + 9 + 2 + 2 + 2 + 5);
  return (uint8_t)(m >> 7);
}

// $09FC and $0885: voice `v`'s two volumes from its channel's pan. The right
// is from 127 - pan, less the carry the left left.
static void voice_volumes(Player* p, int v) {
  const uint8_t pan = p->channel[p->voice[v].track][p->voice[v].channel].pan;
  bool carry;
  spend(p, 5 + 4 + 8);
  const uint8_t left = pan_volume(p, v, pan, &carry);
  spend(p, 2 + 8);
  voice_set(p, v, DSP_VOL_L, left);
  spend(p, 4 + 4 + 2 + 3 + 8);
  const uint8_t right = pan_volume(p, v, (uint8_t)(0x7F - pan - !carry), &carry);
  spend(p, 2 + 8);
  voice_set(p, v, DSP_VOL_R, right);
}

// ---------------------------------------------------------------------------
// Tracks
// ---------------------------------------------------------------------------

static uint16_t song_start(Player* p, int b, uint8_t song) {
  const uint8_t* ram = bank_ram(p, b);
  return (uint16_t)(ram[(uint16_t)(SONG_LO + song)] | ram[(uint16_t)(SONG_HI + song)] << 8);
}

// $0E84
static uint8_t track_byte(Player* p, int t) {
  PlayerTrack* tr = &p->track[t];
  const uint8_t b = bank_ram(p, tr->bank)[tr->ptr++];
  spend(p, 2 + 2 + 2 + 6 + 5);
  spend(p, tr->ptr & 0xFF ? 4 : 2 + 5);
  spend(p, 3 + 5);
  return b;
}

// $0E67: the delta time to track `t`'s next event, one byte or two, into its
// wait. True if it is not zero.
static bool track_delta(Player* p, int t) {
  PlayerTrack* tr = &p->track[t];
  spend(p, 8);
  uint8_t b = track_byte(p, t);
  spend(p, 4 + 2 + 4);
  if (b & 0x80) {
    spend(p, 2 + 2 + 2 + 2 + 5 + 2 + 2 + 5 + 8);
    tr->wait = (uint16_t)(((b & 0x7F) >> 1) << 8 | (uint8_t)(b << 7));
    b = track_byte(p, t);
  } else {
    spend(p, 4);
  }
  tr->wait |= b;
  spend(p, 4 + 5 + 4 + 5);
  return tr->wait != 0;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// $0ACD: note off for `note` on track `t`'s current channel: the least
// recently used voice playing it.
static void note_off(Player* p, int t, uint8_t note) {
  const PlayerBank* k = bank_of(p, t);
  const int n = voice_count(k);
  spend(p, 2);
  for (int i = 0; i < n; i++) {
    const int v = k->order[i];
    const PlayerVoice* vo = &p->voice[v];
    spend(p, 4 + 2 + 5);
    bool match = false;
    if (!vo->on) {
      spend(p, 4);
    } else {
      spend(p, 2 + 3 + 5);
      if (vo->note != note) {
        spend(p, 4);
      } else {
        spend(p, 2 + 3 + 5);
        if (vo->track != t) {
          spend(p, 4);
        } else {
          spend(p, 2 + 4 + 2 + 4 + 5 + 4);
          if (vo->channel != p->track[t].channel) spend(p, 4);
          else { spend(p, 2); match = true; }
        }
      }
    }
    if (match) {
      spend(p, 8);
      voice_off(p, v);
      spend(p, 5);
      return;
    }
    spend(p, 2 + 2 + loop_end(i < n - 1));
  }
  spend(p, 5);
}

// $098C: note on, `note` at `velocity`.
static void note_on(Player* p, int t, uint8_t note, uint8_t velocity) {
  spend(p, 3);
  if (!velocity) {
    spend(p, 2 + 3);
    note_off(p, t, note);
    return;
  }
  const PlayerTrack* tr = &p->track[t];
  const PlayerChannel* c = channel_of(p, t);
  spend(p, 4 + 8);
  track_channel(p);
  spend(p, 5 + 4 + 5 + 2);
  if (c->mute & 0x40) {
    spend(p, 4 + 5);
    return;
  }
  spend(p, 2 + 8);
  int place = 0;
  const int v = voice_find(p, bank_of(p, t), c->priority, &place);
  if (v < 0) {
    spend(p, 4 + 5);
    p->notes_dropped++;
    return;
  }
  spend(p, 2 + 8);
  voice_claim(p, bank_of(p, t), place, v);
  PlayerVoice* vo = &p->voice[v];
  vo->track = (uint8_t)t;
  vo->channel = tr->channel;
  vo->note = note;
  vo->velocity = velocity;
  spend(p, 3 + 6 + 2 + 4 + 6 + 3 + 6 + 3 + 6);
  vo->echo = c->flags & 1;
  vo->noise = c->flags & 2;
  spend(p, 3 + 4 + 3 + 5 + 2 + 4 + 4 + 3 + 5 + 2 + 4);
  vo->vib_time = vo->vib_step = 0;
  vo->vib_depth = c->vib_depth;
  vo->vib_wave = c->vib_wave;
  vo->vib_rate = c->vib_rate;
  vo->vib_range = c->vib_range;
  vo->priority = c->priority;
  spend(p, 4 + 2 + 3 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 4);
  voice_volumes(p, v);

  // The instrument: the program's first entry, then the first of it whose top
  // note is at or above this one. A program with none plays the first's.
  const uint8_t* ram = voice_ram(p, v);
  spend(p, 3 + 5 + 2);
  uint8_t i = 0;
  for (;;) {
    spend(p, 5);
    if (ram[INS_PROGRAM + i] == c->program) { spend(p, 4); break; }
    spend(p, 2 + 2 + 2);
    if (++i < 0x20) { spend(p, 4); continue; }
    spend(p, 2 + 2);
    i = 0;
    break;
  }
  spend(p, 3);
  for (;;) {
    spend(p, 5);
    const uint8_t top = ram[(uint16_t)(INS_TOP + i)];
    if (note == top) { spend(p, 4); break; }
    spend(p, 2);
    if (note < top) { spend(p, 4); break; }
    spend(p, 2 + 2 + 4);
    i++;
  }
  spend(p, 2 + 2 + 5 + 2 + 8);
  voice_set(p, v, DSP_SRCN, ram[(uint16_t)(INS_SAMPLE + i)]);
  spend(p, 5 + 2 + 8);
  voice_set(p, v, DSP_ADSR1, ram[(uint16_t)(INS_ADSR1 + i)]);
  spend(p, 5 + 2 + 8);
  voice_set(p, v, DSP_ADSR2, ram[(uint16_t)(INS_ADSR2 + i)]);

  uint8_t key = (uint8_t)(ram[(uint16_t)(INS_SHIFT + i)] + note);
  spend(p, 5 + 2 + 3 + 3 + 4 + 5 + 4);
  if (tr->shifted) {
    spend(p, 2 + 2 + 4);
    key = (uint8_t)(key + p->shift);
  } else {
    spend(p, 4);
  }
  vo->key = key;
  // The semitone's pitch, tuned by (TUNE - $40) * 2 / 256 of the step to the
  // next, then bent by the channel's wheel.
  const uint8_t k2 = (uint8_t)(key << 1);
  uint16_t base = pitch_at(ram, k2);
  const uint16_t step = (uint16_t)(pitch_at(ram, k2 + 2) - base);
  spend(p, 3 + 6 + 2 + 2 + 5 + 4 + 5 + 4 + 2 + 5 + 5 + 4 + 5 + 5 + 4);
  spend(p, 3 + 5 + 2 + 2 + 2 + 8);
  base = (uint16_t)(base + mul_hi(p, step, (uint8_t)((uint8_t)(c->tune - 0x40) << 1)));
  vo->pitch = base;
  spend(p, 3 + 2 + 3 + 3 + 4 + 5 + 3 + 3 + 4 + 5);
  spend(p, 3 + 8);
  uint8_t range, amount;
  channel_bend(p, c, &range, &amount);
  spend(p, 3 + 8);
  const uint16_t pitch = (uint16_t)(base + bend_pitch(p, ram, key, range, amount));
  spend(p, 2 + 3 + 3 + 4 + 3 + 3 + 4 + 3 + 2 + 8);
  voice_set(p, v, DSP_PITCH_H, (uint8_t)(pitch >> 8));
  spend(p, 3 + 2 + 8);
  voice_set(p, v, DSP_PITCH_L, (uint8_t)pitch);
  spend(p, 8);
  voice_key_on(p, v);
  spend(p, 5);
}

// $075C: the pitch wheel, its two 7-bit halves kept as the top eight bits,
// signed about the middle, for every voice the channel is playing.
static void pitch_wheel(Player* p, int t, uint8_t lo, uint8_t hi) {
  PlayerChannel* c = channel_of(p, t);
  spend(p, 4 + 4 + 4 + 8);
  track_channel(p);
  uint8_t bend = (uint8_t)((uint8_t)(hi << 1 | (lo >> 6 & 1)) - 0x80);
  spend(p, 3 + 2 + 2 + 2);
  if (bend == 0x80) {
    spend(p, 2 + 2);
    bend = 0x81;
  } else {
    spend(p, 4);
  }
  c->bend = bend;
  spend(p, 6 + 8);
  uint8_t range, amount;
  channel_bend(p, c, &range, &amount);
  const int first = first_voice(bank_of(p, t));
  spend(p, 2);
  for (int v = first + voice_count(bank_of(p, t)) - 1; v >= first; v--) {
    if (voice_is(p, v, t)) {
      spend(p, 8);
      const uint16_t pitch = (uint16_t)(p->voice[v].pitch +
                                        bend_pitch(p, voice_ram(p, v), p->voice[v].key, range, amount));
      spend(p, 4 + 2 + 5 + 3 + 4 + 5 + 3 + 2 + 8);
      voice_set(p, v, DSP_PITCH_H, (uint8_t)(pitch >> 8));
      spend(p, 4 + 2 + 8);
      voice_set(p, v, DSP_PITCH_L, (uint8_t)pitch);
      spend(p, 4);
    }
    spend(p, 2 + loop_end(v > first));
  }
  spend(p, 5);
}

// $0938
static void program_change(Player* p, int t, uint8_t program) {
  spend(p, 8);
  track_channel(p);
  channel_of(p, t)->program = program;
  spend(p, 3 + 6 + 5);
}

// $0821: the channel's vibrato, given again to every voice it is playing.
static void vibrato_update(Player* p, int t) {
  const PlayerChannel* c = channel_of(p, t);
  const int first = first_voice(bank_of(p, t));
  spend(p, 2);
  for (int v = first + voice_count(bank_of(p, t)) - 1; v >= first; v--) {
    if (voice_is(p, v, t)) {
      spend(p, 4 + 4 + 3 + 8);
      track_channel(p);
      p->voice[v].vib_depth = c->vib_depth;
      p->voice[v].vib_wave = c->vib_wave;
      p->voice[v].vib_range = c->vib_range;
      p->voice[v].vib_rate = c->vib_rate;
      spend(p, 4 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 4 + 3);
    }
    spend(p, 2 + loop_end(v > first));
  }
  spend(p, 5);
}

// $0864: the channel's pan and volume, given again to every voice it is
// playing.
static void volume_update(Player* p, int t) {
  const int first = first_voice(bank_of(p, t));
  spend(p, 2);
  for (int v = first + voice_count(bank_of(p, t)) - 1; v >= first; v--) {
    if (voice_is(p, v, t)) {
      spend(p, 4 + 5 + 4 + 2 + 5 + 4 + 8);
      track_channel(p);
      voice_volumes(p, v);
      spend(p, 4);
    }
    spend(p, 2 + loop_end(v > first));
  }
  spend(p, 5);
}

// $08BF: a register of the chip's straight from the controller's value.
static void control_dsp(Player* p, uint8_t reg, uint8_t val) {
  spend(p, 3 + 8);
  dsp_set(p, -1, reg, val);
  spend(p, 5);
}

static void control(Player* p, int t, uint8_t number, uint8_t val) {
  PlayerTrack* tr = &p->track[t];
  PlayerChannel* c = channel_of(p, t);
  switch (number) {
    case 0x00:  // $0801: vibrato rate
      c->vib_rate = (uint8_t)(val << 1);
      spend(p, 3 + 2 + 6 + 4);
      vibrato_update(p, t);
      return;
    case 0x01:  // $0809: vibrato depth
      c->vib_depth = val;
      spend(p, 3 + 6 + 4);
      vibrato_update(p, t);
      return;
    case 0x02:  // $0810: vibrato range
      c->vib_range = val;
      spend(p, 3 + 6 + 4);
      vibrato_update(p, t);
      return;
    case 0x03:  // $0817: vibrato wave
      c->vib_wave = (uint8_t)((val & 3) << 6);
      spend(p, 3 + 2 + 5 + 2 + 2 + 6);
      vibrato_update(p, t);
      return;
    case 0x04: c->priority = val; spend(p, 3 + 6 + 5); return;
    case 0x05: c->mute = (uint8_t)(c->mute + val); spend(p, 3 + 2 + 5 + 6 + 5); return;
    case 0x06: spend(p, 5); return;
    case 0x07:  // $0858: volume
      c->volume = val;
      spend(p, 3 + 6 + 4);
      volume_update(p, t);
      return;
    case 0x08:  // $085F: pan
    case 0x0A:
      c->pan = val;
      spend(p, 3 + 6);
      volume_update(p, t);
      return;
    case 0x09: c->flags = val; spend(p, 3 + 6 + 5); return;
    case 0x0B: c->tune = val; spend(p, 3 + 6 + 5); return;
    case 0x0C: spend(p, 2 + 4); control_dsp(p, DSP_MVOL_L, val); return;
    case 0x0D: spend(p, 2 + 4); control_dsp(p, DSP_MVOL_R, val); return;
    case 0x0E: spend(p, 2 + 4); control_dsp(p, DSP_EVOL_L, val); return;
    case 0x0F: spend(p, 2); control_dsp(p, DSP_EVOL_R, val); return;
    case 0x10:  // $08C5: the echo's buffer and delay, and its writes on
      spend(p, 4 + 3 + 2 + 2 + 5 + 2 + 8);
      dsp_set(p, -1, DSP_ESA, bank_ram(p, 0)[ECHO_PAGES + (val & 3)]);
      spend(p, 3 + 2 + 2 + 8);
      dsp_set(p, -1, DSP_EDL, val & 3);
      // The driver ANDs with the byte command $0D saves the upload address's
      // low half in, where an immediate $DF must have been meant.
      p->flg &= (uint8_t)p->bank[0].saved_upload;
      spend(p, 3 + 3 + 4 + 2 + 8);
      dsp_set(p, -1, DSP_FLG, p->flg);
      spend(p, 4 + 5);
      return;
    case 0x11: spend(p, 2 + 4); control_dsp(p, DSP_EFB, val); return;
    case 0x12:  // $08F1: the noise's rate
      p->flg = (uint8_t)((p->flg & 0xE0) | (val & 0x1F));
      spend(p, 3 + 2 + 4 + 3 + 2 + 3 + 4 + 2 + 8);
      dsp_set(p, -1, DSP_FLG, p->flg);
      spend(p, 5);
      return;
    case 0x13: tr->volume = (uint8_t)(val << 1); spend(p, 3 + 2 + 5 + 5); return;
    case 0x14:  // $090B: a loop starts here, to be played `val` more times
      tr->loops = val;
      tr->loop = tr->ptr;
      spend(p, 3 + 5 + 2 + 2 + 2 + 5 + 6 + 5 + 6 + 5);
      return;
    case 0x15:  // $091F: ...and ends here. A count of 0 loops for ever.
      spend(p, 4);
      if (tr->loops) {
        spend(p, 2 + 5);
        if (!--tr->loops) {
          spend(p, 2 + 5);
          return;
        }
        spend(p, 4);
      } else {
        spend(p, 4);
      }
      tr->ptr = tr->loop;
      spend(p, 2 + 2 + 2 + 5 + 6 + 5 + 6 + 5);
      return;
  }
}

// $07A9: event $B0, controller `number`. Those past $15 are ignored.
static void controller(Player* p, int t, uint8_t number, uint8_t val) {
  spend(p, 8);
  track_channel(p);
  spend(p, 3 + 2);
  if (number >= 0x16) {
    spend(p, 4 + 5);
    return;
  }
  spend(p, 2 + 4 + 2 + 5 + 5 + 5 + 5 + 4 + 3);
  control(p, t, number, val);
}

// $0941: event $FF. False for the end of the track.
static bool meta_event(Player* p, int t) {
  PlayerTrack* tr = &p->track[t];
  spend(p, 8);
  const uint8_t type = track_byte(p, t);
  spend(p, 2);
  if (type == 0x2F) {  // the end
    spend(p, 4 + 4 + 4 + 3);
    return false;
  }
  spend(p, 2 + 2);
  if (type == 0x51) {  // the tempo: three bytes after a length
    spend(p, 4 + 8);
    track_byte(p, t);
    spend(p, 8);
    uint32_t tempo = (uint32_t)track_byte(p, t) << 16;
    spend(p, 5 + 8);
    tempo |= (uint32_t)track_byte(p, t) << 8;
    spend(p, 5 + 8);
    tr->tempo = tempo | track_byte(p, t);
    spend(p, 5 + 5);
    return true;
  }
  spend(p, 2 + 2);
  if (type == 0x30) {  // back to the song's start
    tr->ptr = song_start(p, tr->bank, tr->song);
    spend(p, 4 + 3 + 2 + 2 + 2 + 4 + 2 + 5 + 6 + 5 + 6 + 5);
    return true;
  }
  // Anything else is skipped by its length. (The driver reads those bytes
  // from the wrong track, and would not come back; no song has one.)
  spend(p, 2 + 8);
  for (uint8_t len = track_byte(p, t); len; len--) {
    spend(p, 8);
    track_byte(p, t);
  }
  spend(p, 2 + 5);
  return true;
}

// $0705: track `t`'s next event. False for the end of the track.
static bool track_event(Player* p, int t) {
  PlayerTrack* tr = &p->track[t];
  spend(p, 8);
  uint8_t b = track_byte(p, t);
  spend(p, 4 + 2 + 4);
  if (b & 0x80) {  // a status byte, or the running status if not
    tr->channel = b & 0x0F;
    tr->status = b & 0xF0;
    spend(p, 2 + 4 + 2 + 5 + 4 + 2 + 5 + 2);
    if (tr->status == 0xF0) {
      spend(p, 2 + 3);
      return meta_event(p, t);
    }
    spend(p, 4 + 8);
    b = track_byte(p, t);
  } else {
    spend(p, 4);
  }
  spend(p, 4 + 4 + 2);
  if (tr->status == 0xC0) {
    spend(p, 2 + 3);
    program_change(p, t, b);
    return true;
  }
  spend(p, 4 + 2);
  if (tr->status == 0x80) {  // one data byte only
    spend(p, 2 + 3);
    note_off(p, t, b);
    return true;
  }
  spend(p, 4 + 8);
  const uint8_t b2 = track_byte(p, t);
  spend(p, 4 + 4 + 2 + 2 + 5 + 2 + 5 + 5 + 5 + 5 + 3);
  switch (tr->status) {
    case 0x90: note_on(p, t, b, b2); break;
    case 0xB0: controller(p, t, b, b2); break;
    case 0xE0: pitch_wheel(p, t, b, b2); break;
    // $A0 and $D0: the driver jumps to $0000. No song has one.
    default: break;
  }
  return true;
}

// $06B5: a tick of the driver's timer, 500 a second. Each playing track's
// clock runs down by $039378; when it runs out it is wound back by the tempo
// and the track is a beat on, and when the beats to its next event have
// passed, that event and any after it with no wait between are played.
static void tick(Player* p, const PlayerBank* k) {
  spend(p, 2 + 4);
  for (int t = k->first_track + k->tracks - 1; t >= k->first_track; t--) {
    PlayerTrack* tr = &p->track[t];
    spend(p, 4 + 4);
    if (tr->wait) {
      spend(p, 2 + 2 + 4 + 2 + 5 + 4 + 2 + 5 + 4 + 2 + 5);
      const bool beat = tr->clock < 0x039378u;
      tr->clock = (tr->clock - 0x039378u) & 0xFFFFFF;
      if (beat) {
        spend(p, 2 + 2 + 4 + 4 + 5 + 4 + 4 + 5 + 4 + 4 + 5);
        tr->clock = (tr->clock + tr->tempo) & 0xFFFFFF;
        spend(p, 4);
        spend(p, tr->wait & 0xFF ? 4 : 2 + 5);
        tr->wait--;
        spend(p, 2 + 5 + 4);
        if (tr->wait) {
          spend(p, 4);
        } else {
          spend(p, 2);
          for (;;) {
            spend(p, 8);
            if (!track_event(p, t)) break;
            spend(p, 3 + 8);
            if (track_delta(p, t)) {
              spend(p, 2);
              break;
            }
            spend(p, 4);
          }
        }
      } else {
        spend(p, 4);
      }
    } else {
      spend(p, 4);
    }
    spend(p, 4 + 3);
    spend(p, t > k->first_track ? 4 : 2 + 5);
  }
}

// $0D03: each voice's vibrato. When its time overflows it takes the next of 64
// steps through its wave, and its pitch is bent by the wave times the depth,
// over the range.
static void vibrato(Player* p, const PlayerBank* k) {
  const int first = first_voice(k);
  spend(p, 2);
  for (int v = first + voice_count(k) - 1; v >= first; v--) {
    PlayerVoice* vo = &p->voice[v];
    spend(p, 4);
    if (vo->on) {
      const unsigned sum = vo->vib_rate + vo->vib_time;
      vo->vib_time = (uint8_t)sum;
      spend(p, 2 + 2 + 4 + 4 + 5);
      if (sum > 0xFF) {
        uint8_t range = vo->vib_range;
        spend(p, 2 + 4 + 4 + 5 + 4 + 2);
        if (++vo->vib_step == 0x40) {
          spend(p, 2 + 2 + 5);
          vo->vib_step = 0;
        } else {
          spend(p, 4);
        }
        const uint8_t* ram = voice_ram(p, v);
        uint8_t amount = ram[VIB_WAVES + (uint8_t)(vo->vib_step + vo->vib_wave)];
        spend(p, 2 + 4 + 2 + 5 + 8);
        bend_sign(p, &range, &amount);
        spend(p, 2 + 2 + 8);
        const uint16_t bend = bend_pitch(p, ram, vo->key, range, amount);
        spend(p, 3 + 4 + 3 + 4 + 4 + 8);
        const uint16_t pitch = (uint16_t)(mul_hi(p, bend, vo->vib_depth) + vo->pitch);
        spend(p, 2 + 3 + 4 + 4 + 3 + 4 + 2 + 8);
        voice_set(p, v, DSP_PITCH_H, (uint8_t)(pitch >> 8));
        spend(p, 4 + 2 + 8);
        voice_set(p, v, DSP_PITCH_L, (uint8_t)pitch);
      } else {
        spend(p, 4);
      }
    } else {
      spend(p, 4);
    }
    spend(p, 2 + loop_end(v > first));
  }
  spend(p, 5);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

// $0EB1: a track's sixteen channels, as a song starts. The wave is kept.
static void channels_reset(Player* p, int t) {
  for (int i = 0; i < 16; i++) {
    PlayerChannel* c = &p->channel[t][i];
    c->pan = c->tune = c->priority = 0x40;
    c->volume = 0x7F;
    c->program = c->flags = c->bend = c->vib_depth = c->mute = 0;
    c->vib_rate = 0x20;
    c->vib_range = 0x02;
    c->bend_range = 0x0C;
  }
}

// How many tracks are playing `song` of bank `b`.
static int playing(const Player* p, int b, uint8_t song) {
  int n = 0;
  for (int t = 0; t < p->tracks; t++)
    n += p->track[t].wait && p->track[t].bank == b && p->track[t].song == song;
  return n;
}

// $0B8A, commands $01 and $11: song `song` of bank `b` on a free track, the
// highest. False if it is not a song, or it is an effect playing `max_same`
// times already, or no track is free. (It makes no DSP writes, so its cycles
// are not counted: they are over by the next tick.)
static bool start_song(Player* p, int b, uint8_t song, bool shifted) {
  if (song >= p->bank[b].songs) return false;
  const bool effect = song < p->bank[0].saved_songs;
  if (effect && p->set.max_same && playing(p, b, song) >= p->set.max_same) {
    p->effects_held++;
    return false;
  }
  const int low = p->bank[b].first_track;
  int t = low + p->bank[b].tracks - 1;
  while (t >= low && p->track[t].wait) t--;
  if (t < low) {
    if (effect) p->effects_dropped++;
    return false;
  }
  if (effect) p->effects_started++;
  PlayerTrack* tr = &p->track[t];
  tr->bank = b;
  tr->song = song;
  tr->clock = (uint32_t)song * 0x010101u;  // as the driver sets it
  tr->loops = 0;
  tr->shifted = shifted;
  tr->tempo = 0x0927C0;
  tr->volume = 0xFF;
  channels_reset(p, t);
  tr->ptr = tr->loop = song_start(p, b, song);
  if (!track_delta(p, t)) tr->wait = 1;
  return true;
}

// $0BE1, command $02: the game's song or effect `song` stopped, on the first
// track playing it.
static void stop_song(Player* p, uint8_t song) {
  spend(p, 2);
  for (int t = p->set.tracks - 1; t >= 0; t--) {
    PlayerTrack* tr = &p->track[t];
    spend(p, 4 + 4);
    if (tr->wait) {
      spend(p, 2 + 3 + 4);
      if (tr->bank == 0 && tr->song == song) {
        spend(p, 4 + 2);
        tr->wait = 0;
        spend(p, 5 + 5 + 8);
        track_release(p, t);
        spend(p, 5);
        return;
      }
      spend(p, 2);
    } else {
      spend(p, 4);
    }
    spend(p, 2 + loop_end(t > 0));
  }
  spend(p, 5);
}

static void upload_next(Player* p, int b) {
  p->bank[b].upload++;
  spend(p, 4);
  spend(p, p->bank[b].upload & 0xFF ? 4 : 2 + 4);
  spend(p, 5);
}

// $0C3C
static void upload_byte(Player* p, int b, uint8_t val) {
  bank_put(p, b, 6, p->bank[b].upload, val);
  spend(p, 7 + 4);
  upload_next(p, b);
}

// $0CBA, command $14: the last song registered is a sound with its own
// sample: six bytes of instrument at its start go in as instrument $18, for
// program $15, and its sample, at +$0A with its loop at +$08, as sample $15.
static void sample_song(Player* p, int b) {
  const uint8_t* ram = bank_ram(p, b);
  const uint16_t at = song_start(p, b, (uint8_t)(p->bank[b].songs - 1));
  spend(p, 3 + 2 + 5 + 4 + 5 + 4 + 2 + 2);
  for (int y = 0; y < 6; y++) {
    bank_put(p, b, 0, (uint16_t)(0x1358 + y * 0x20), ram[(uint16_t)(at + y)]);
    spend(p, 6 + 6 + 2 + 2 + 2 + 2 + 2 + 2 + loop_end(y < 5));
  }
  bank_put(p, b, 0, 0x13F8, 0x15);
  spend(p, 2 + 5 + 2 + 3 + 2);
  const uint16_t start = (uint16_t)(at + 0x0A);
  const uint16_t loop =
      (uint16_t)(start + (ram[(uint16_t)(at + 8)] | ram[(uint16_t)(at + 9)] << 8));
  bank_put(p, b, 4, DIRECTORY + 0x54, (uint8_t)start);
  spend(p, 5 + 3 + 2);
  bank_put(p, b, 4, DIRECTORY + 0x55, (uint8_t)(start >> 8));
  spend(p, 5 + 2 + 2 + 4 + 6);
  bank_put(p, b, 4, DIRECTORY + 0x56, (uint8_t)loop);
  spend(p, 5 + 2 + 4 + 6);
  bank_put(p, b, 4, DIRECTORY + 0x57, (uint8_t)(loop >> 8));
  spend(p, 5 + 5);
}

// $0C5C's share of RAM: the echo's end of it cleared, a byte at a time.
static void echo_clear(Player* p, int b) {
  spend(p, 2 + 4 + 2 + 4 + 2);
  for (int page = ECHO_CLEAR_FROM; page <= 0xFF; page++) {
    for (int y = 0; y < 256; y++) {
      bank_put(p, b, 6, (uint16_t)(page << 8 | y), 0);
      spend(p, 7 + 2 + loop_end(y < 255));
    }
    spend(p, 4);
    spend(p, page < 0xFF ? 4 : 2);
  }
}

// The commands that upload, for bank `b`, from their handlers. True if `cmd`
// was one.
static bool bank_command(Player* p, int b, uint8_t cmd, uint8_t param, uint8_t param0) {
  PlayerBank* k = &p->bank[b];
  switch (cmd) {
    case 0x04: k->upload = (uint16_t)((k->upload & 0xFF00) | param); return true;
    case 0x05: k->upload = (uint16_t)((k->upload & 0x00FF) | param << 8); return true;
    case 0x06: spend(p, 3 + 2); upload_byte(p, b, param); return true;
    case 0x07: k->upload++; return true;  // a byte read back
    case 0x08:  // the uploads put back
      k->upload = k->saved_upload;
      k->songs = k->saved_songs;
      return true;
    case 0x0A:  // a song starts at the upload address
      bank_put(p, b, 0, (uint16_t)(SONG_LO + k->songs), (uint8_t)k->upload);
      bank_put(p, b, 0, (uint16_t)(SONG_HI + k->songs), (uint8_t)(k->upload >> 8));
      k->songs++;
      return true;
    case 0x0B:  // $0C2A: two bytes, PARAM then PARAM0
      bank_put(p, b, 2 + 3 + 6, k->upload, param);
      spend(p, 3 + 2 + 7 + 8);
      upload_next(p, b);
      spend(p, 3 + 3);
      upload_byte(p, b, param0);
      return true;
    case 0x0D: k->saved_upload = k->upload; k->saved_songs = k->songs; return true;
    case 0x14: sample_song(p, b); return true;
    default: return false;
  }
}

// $0C5C, command $08: the echo off and its end of RAM cleared, every track
// stopped, and the uploads put back to where command $0D saved them.
static void reset_songs(Player* p) {
  p->flg |= 0x20;
  spend(p, 3 + 2 + 4 + 2 + 8);
  dsp_set(p, -1, DSP_FLG, p->flg);
  spend(p, 2 + 2 + 8);
  dsp_set(p, -1, DSP_EFB, 0);
  spend(p, 2 + 8);
  dsp_set(p, -1, DSP_EVOL_L, 0);
  spend(p, 2 + 8);
  dsp_set(p, -1, DSP_EVOL_R, 0);
  echo_clear(p, 0);
  spend(p, 2);
  for (int t = p->set.tracks - 1; t >= 0; t--) {
    spend(p, 4 + 4);
    if (p->track[t].wait) {
      spend(p, 2 + 2 + 5 + 5 + 4 + 8);
      p->track[t].wait = 0;
      track_release(p, t);
      spend(p, 4);
    } else {
      spend(p, 4);
    }
    spend(p, 2 + loop_end(t > 0));
  }
  bank_command(p, 0, 0x08, 0, 0);
}

// A command the driver has picked up, from its handler.
static void command(Player* p, uint8_t cmd, uint8_t param, uint8_t param0) {
  p->commands++;
  switch (cmd) {
    case 0x01: start_song(p, 0, param, false); return;
    case 0x11: start_song(p, 0, param, true); return;
    case 0x02: stop_song(p, param); return;
    case 0x08: reset_songs(p); return;
    case 0x09: p->synced = false; p->lost = "the driver started again"; return;
    case 0x0C: p->paused = param; return;
    case 0x0E: p->mono = param; return;
    case 0x12: p->shift = param; return;
    case 0x13:  // $0B6D: the main volume
      spend(p, 3 + 2 + 8);
      dsp_set(p, -1, DSP_MVOL_L, param);
      spend(p, 2 + 8);
      dsp_set(p, -1, DSP_MVOL_R, param);
      return;
    default: bank_command(p, 0, cmd, param, param0); return;
  }
}

// ---------------------------------------------------------------------------
// Following the driver
// ---------------------------------------------------------------------------

void player_init(Player* p, const Sound* real, PlayerSettings set) {
  memset(p, 0, sizeof *p);
  if (set.tracks < 4) set.tracks = 4;  // the driver's, which it takes over
  if (set.tracks > PLAYER_GAME_TRACKS_MAX) set.tracks = PLAYER_GAME_TRACKS_MAX;
  int live = set.live_voices / 8;
  live = live < 1 ? 1 : live > PLAYER_LIVE_UNITS_MAX ? PLAYER_LIVE_UNITS_MAX : live;
  set.live_voices = live * 8;
  p->set = set;
  p->real = real;
  p->units = live + (set.other_sets ? PLAYER_SETS : 0);
  p->tracks = set.tracks + (set.other_sets ? PLAYER_SETS * PLAYER_SET_TRACKS : 0);
  p->voices = p->units * 8;
  p->bank[0].units = live;
  p->bank[0].tracks = set.tracks;
  for (int s = 0; s < PLAYER_SETS && set.other_sets; s++) {
    PlayerBank* k = &p->bank[1 + s];
    k->first_unit = live + s;
    k->units = 1;
    k->first_track = set.tracks + s * PLAYER_SET_TRACKS;
    k->tracks = PLAYER_SET_TRACKS;
  }
  for (int b = 0; b <= PLAYER_SETS; b++) {
    PlayerBank* k = &p->bank[b];
    for (int u = k->first_unit; u < k->first_unit + k->units; u++) p->unit[u].bank = b;
    for (int i = 0; i < voice_count(k); i++) k->order[i] = (uint8_t)(first_voice(k) + i);
  }
  for (int u = 0; u < p->units; u++) sound_dsp_reset(&p->unit[u].dsp, p->unit[u].ram);
}

// Take the driver as it stands: its RAM, tracks, voices, DSP and samples.
static void player_import(Player* p) {
  const Sound* s = p->real;
  const uint8_t* r = s->ram;
  for (int u = 0; u < p->bank[0].units; u++) memcpy(p->unit[u].ram, r, sizeof p->unit[u].ram);
  PlayerUnit* first = &p->unit[0];
  first->dsp = s->dsp;
  first->dsp.ram = first->ram;
  first->dsp.voice_mute = 0;
  first->eon = r[EON];
  first->non = r[NON];
  first->quiet = 0;
  for (int u = 1; u < p->units; u++) unit_fresh(&p->unit[u], &s->dsp);

  PlayerBank* k = &p->bank[0];
  k->upload = (uint16_t)(r[UPLOAD_LO] | r[UPLOAD_HI] << 8);
  k->saved_upload = (uint16_t)(r[SAVED_LO] | r[SAVED_HI] << 8);
  k->songs = r[SONGS];
  k->saved_songs = r[SAVED_SONGS];

  memset(p->track, 0, sizeof p->track);
  memset(p->channel, 0, sizeof p->channel);
  memset(p->voice, 0, sizeof p->voice);
  for (int t = 0; t < 4; t++) {
    PlayerTrack* tr = &p->track[t];
    tr->ptr = (uint16_t)(r[TRACK_PTR + 2 * t] | r[TRACK_PTR + 2 * t + 1] << 8);
    tr->loop = (uint16_t)(r[TRACK_LOOP + 2 * t] | r[TRACK_LOOP + 2 * t + 1] << 8);
    tr->tempo = (uint32_t)(r[TRACK_TEMPO + t] | r[TRACK_TEMPO + 4 + t] << 8 | r[TRACK_TEMPO + 8 + t] << 16);
    tr->clock = (uint32_t)(r[TRACK_CLOCK + t] | r[TRACK_CLOCK + 4 + t] << 8 | r[TRACK_CLOCK + 8 + t] << 16);
    tr->wait = (uint16_t)(r[TRACK_WAIT + t] | r[TRACK_WAIT + 4 + t] << 8);
    tr->status = r[TRACK_STATUS + t];
    tr->channel = r[TRACK_CHANNEL + t];
    tr->volume = r[TRACK_VOLUME + t];
    tr->song = r[TRACK_SONG + t];
    tr->loops = r[TRACK_LOOPS + t];
    tr->shifted = r[TRACK_SHIFTED + t] != 0;
    for (int i = 0; i < 16; i++) {
      PlayerChannel* c = &p->channel[t][i];
      const int y = t << 4 | i;
      c->program = r[CH_PROGRAM + y];
      c->pan = r[CH_PAN + y];
      c->volume = r[CH_VOLUME + y];
      c->bend = r[CH_BEND + y];
      c->bend_range = r[CH_BEND_RANGE + y];
      c->flags = r[CH_FLAGS + y];
      c->tune = r[CH_TUNE + y];
      c->vib_depth = r[CH_VIB_DEPTH + y];
      c->vib_range = r[CH_VIB_RANGE + y];
      c->vib_rate = r[CH_VIB_RATE + y];
      c->vib_wave = r[CH_VIB_WAVE + y];
      c->priority = r[CH_PRIORITY + y];
      c->mute = r[CH_MUTE + y];
    }
  }
  for (int v = 0; v < 8; v++) {
    PlayerVoice* vo = &p->voice[v];
    vo->on = r[VOICE_ON + v] != 0;
    vo->track = r[VOICE_TRACK + v];
    vo->channel = r[VOICE_CHANNEL + v];
    vo->note = r[VOICE_NOTE + v];
    vo->key = r[VOICE_KEY + v];
    vo->pitch = (uint16_t)(r[VOICE_PITCH_LO + v] | r[VOICE_PITCH_HI + v] << 8);
    vo->velocity = r[VOICE_VELOCITY + v];
    vo->vib_depth = r[VOICE_VIB_DEPTH + v];
    vo->vib_range = r[VOICE_VIB_RANGE + v];
    vo->vib_rate = r[VOICE_VIB_RATE + v];
    vo->vib_time = r[VOICE_VIB_TIME + v];
    vo->vib_step = r[VOICE_VIB_STEP + v];
    vo->vib_wave = r[VOICE_VIB_WAVE + v];
    vo->priority = r[VOICE_PRIORITY + v];
    vo->echo = first->eon & (1 << v);
    vo->noise = first->non & (1 << v);
  }
  // The voices the driver does not have are the least recently used.
  int n = 0;
  for (int v = 8; v < voice_count(k); v++) k->order[n++] = (uint8_t)v;
  for (int i = 0; i < 8; i++) k->order[n++] = r[VOICE_ORDER + i] & 7;
  for (int b = 1; b <= PLAYER_SETS; b++)
    for (int i = 0; i < voice_count(&p->bank[b]); i++)
      p->bank[b].order[i] = (uint8_t)(first_voice(&p->bank[b]) + i);
  p->flg = r[FLG];
  p->mono = r[MONO];
  p->shift = r[SHIFT];
  p->paused = r[PAUSED];

  p->at = s->now;
  for (int b = 0; b <= PLAYER_SETS; b++) {
    p->bank[b].busy = s->now;
    p->bank[b].tick_owed = false;
  }
  p->host = s->host;
  p->dsp_at = s->dsp_at;
  p->ticks = s->ticks;
  memcpy(p->ring, s->ring, sizeof p->ring < sizeof s->ring ? sizeof p->ring : sizeof s->ring);
  p->synced = true;
  p->imports++;
}

// $06A4 to the RET of $0D03: bank `k`'s tick and the vibrato after it.
static void tick_at(Player* p, PlayerBank* k, uint64_t at) {
  p->at = at;
  tick(p, k);
  spend(p, 8);
  vibrato(p, k);
  k->busy = p->at;
}

// A tick owed since bank `k` was busy, played once its driver would have come
// round its loop to it, if that is before `c`.
static void settle(Player* p, PlayerBank* k, uint64_t c) {
  if (k->tick_owed && c >= k->busy + BUSY_TO_TICK) {
    k->tick_owed = false;
    tick_at(p, k, k->busy + BUSY_TO_TICK);
  }
}

// A tick of the driver's timer, for every bank: the game's first, so that its
// writes are made on their cycles.
static void tick_all(Player* p, uint64_t c) {
  for (int b = 0; b <= PLAYER_SETS; b++) {
    PlayerBank* k = &p->bank[b];
    if (!k->tracks) continue;
    settle(p, k, c);
    if (c >= k->busy) tick_at(p, k, c);
    else k->tick_owed = true;  // a second while busy is lost
  }
}

void player_seen(Player* p, SoundSeen what, uint16_t a, uint8_t v, uint64_t cycle) {
  (void)v;
  if (what != SOUND_SEEN_AT) return;
  if (a == DRIVER_START) {
    p->synced = false;
    p->lost = "the driver started again";
    return;
  }
  if (!p->synced) return;
  if (a == DRIVER_PICKUP) {
    PlayerBank* k = &p->bank[0];
    settle(p, k, cycle);
    const uint8_t* r = p->real->ram;
    const uint64_t free = k->busy + BUSY_TO_PICKUP;
    p->at = (cycle > free ? cycle : free) + PICKUP_TO_COMMAND;
    if (r[CMD] < 0x15) command(p, r[CMD], r[PARAM], r[PARAM0]);
    if (p->at > k->busy) k->busy = p->at;
  } else if (a == DRIVER_TICK) {
    tick_all(p, cycle);
  }
}

static void player_watch(void* ctx, SoundSeen what, uint16_t a, uint8_t v, uint64_t cycle) {
  player_seen((Player*)ctx, what, a, v, cycle);
}

void player_attach(Player* p, Sound* real) {
  real->watch = player_watch;
  real->watch_ctx = p;
}

void player_detach(Player* p, Sound* real) {
  if (real->watch == player_watch && real->watch_ctx == p) {
    real->watch = NULL;
    real->watch_ctx = NULL;
  }
}

void player_run(Player* p, uint64_t t) {
  const Sound* s = p->real;
  if (p->synced) {
    if (s->host < p->host) p->lost = "the driver's clock went back";
    else if (s->state == SOUND_HALTED) p->lost = s->fault;
    else p->lost = NULL;
    if (p->lost) p->synced = false;
  }
  if (!p->synced) {
    p->host = t;
    if (!sound_driver_running(s)) return;
    player_import(p);
  }
  p->host = t;
  for (int b = 0; b <= PLAYER_SETS; b++) settle(p, &p->bank[b], t);
  if (t) catch_up(p, t - 1);
}

void player_get_samples(const Player* p, int16_t* out, int count) {
  if (!p->synced) {
    sound_get_samples(p->real, out, count);
    return;
  }
  // `sound_get_samples`'s arithmetic, on the player's ring.
  const uint64_t made = sound_ticks_before(p->host);
  const uint16_t made16 = (uint16_t)made;
  const float per_frame = 534.0f;
  const double step = per_frame / count;
  double at = made16 - per_frame;
  for (int i = 0; i < count; i++) {
    const int j = (int)at & 0x3FF;
    const int64_t k = (int64_t)made - 1 - (int64_t)((made - 1 - (uint64_t)j) & 0x3FF);
    if (made == 0 || k < 0 || (uint64_t)k >= p->ticks) {
      out[i * 2] = out[i * 2 + 1] = 0;
    } else {
      out[i * 2] = p->ring[(k % PLAYER_RING) * 2];
      out[i * 2 + 1] = p->ring[(k % PLAYER_RING) * 2 + 1];
    }
    at += step;
  }
}

// ---------------------------------------------------------------------------
// The other sets
// ---------------------------------------------------------------------------

bool player_can_build(const Player* p) {
  return p->synced && p->set.other_sets && p->bank[0].saved_songs != 0;
}

void player_build_set(Player* p, int set) {
  if (set < 0 || set >= PLAYER_SETS || !p->set.other_sets) return;
  PlayerBank* k = &p->bank[1 + set];
  PlayerUnit* n = &p->unit[k->first_unit];
  memcpy(n->ram, p->unit[0].ram, sizeof n->ram);
  k->upload = k->saved_upload = p->bank[0].saved_upload;
  k->songs = k->saved_songs = p->bank[0].saved_songs;
  k->ready = false;
  unit_fresh(n, &p->unit[0].dsp);
}

void player_set_command(Player* p, int set, uint8_t cmd, uint8_t param, uint8_t param0) {
  if (set < 0 || set >= PLAYER_SETS || !p->set.other_sets) return;
  // Not the game's: its clock is left as it was.
  const uint64_t at = p->at;
  bank_command(p, 1 + set, cmd, param, param0);
  if (cmd == 0x08) echo_clear(p, 1 + set);
  p->at = at;
}

void player_set_done(Player* p, int set) {
  if (set < 0 || set >= PLAYER_SETS || !p->set.other_sets) return;
  PlayerUnit* n = &p->unit[p->bank[1 + set].first_unit];
  // The uploads went through the echo's end of RAM: start its line afresh.
  unit_fresh(n, &p->unit[0].dsp);
  p->bank[1 + set].ready = true;
}

bool player_play_from_set(Player* p, int set, uint8_t id) {
  if (set < 0 || set >= PLAYER_SETS || !p->synced || !p->bank[1 + set].ready) return false;
  // Not on the driver's clock: the next tick starts its notes.
  const uint64_t at = p->at;
  const bool ok = start_song(p, 1 + set, id, false);
  p->at = at;
  if (ok) p->other_played[set]++;
  return ok;
}
