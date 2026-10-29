// The game's sound driver, David Warhol's of 1992, as C.
//
// It plays songs that are MIDI files in all but name: four tracks at once,
// each a stream of delta times and events, sixteen channels to a track, and
// eight voices shared between them by priority. The game talks to it through
// the ports, a command and a byte at a time (`poll`), and loads songs and
// samples into its RAM the same way.
//
// The C keeps the driver's variables where the driver kept them, in the sound
// RAM, under the names below; the test build compares that RAM with the real
// driver's byte for byte. Each routine is the original's, at its address, with
// the cycles each path takes: the game waits on this driver, so when it
// answers matters (see `sound.h`). A cost written as a sum is the instructions
// in order; `+ 8` before a call is the CALL, and every routine here spends its
// own RET.
//
// Port code: libc only.

#include "sound/chip.h"
#include "sound/driver_ram.h"

#define R(a) s->ram[(uint16_t)(a)]

static void w(Sound* s, uint16_t adr, uint8_t v) { s->ram[adr] = v; }

// ---------------------------------------------------------------------------
// The DSP
// ---------------------------------------------------------------------------

// $0EF1: MOV $F2, Y : MOV $F3, A : RET
static void dsp_set(Sound* s, uint8_t reg, uint8_t val) {
  spend(s, 4);
  chip_dsp_write(s, 3, reg, val);
  spend(s, 4 + 5);
}

// $0EEB: the current voice's register.
static void voice_set(Sound* s, uint8_t reg, uint8_t val) {
  spend(s, 4 + 2 + 3 + 2 + 4);
  dsp_set(s, (uint8_t)(reg | R(CUR_VOICE_REG)), val);
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

// $0EF6: PROD = MUL * A, both signed, the product 24 bits. MUL is left as its
// magnitude.
static void mul_signed(Sound* s, uint8_t a) {
  spend(s, 4 + 2 + 4 + 3);
  w(s, PROD_SIGN, 0);
  if (R(MUL_HI) & 0x80) {
    spend(s, 2 + 4 + 2 + 2 + 3 + 4 + 2 + 3 + 4);
    w(s, PROD_SIGN, (uint8_t)(R(PROD_SIGN) + 1));
    const uint16_t m = (uint16_t)-(R(MUL_LO) | R(MUL_HI) << 8);
    w(s, MUL_LO, (uint8_t)m);
    w(s, MUL_HI, (uint8_t)(m >> 8));
  } else {
    spend(s, 4);
  }
  spend(s, 4 + 2);  // POP A : CMP A, #$80
  if (a >= 0x80) {
    spend(s, 2 + 4 + 2 + 2);
    w(s, PROD_SIGN, (uint8_t)(R(PROD_SIGN) + 1));
    a = (uint8_t)-a;
  } else {
    spend(s, 4);
  }
  const uint32_t lo = (uint32_t)a * R(MUL_LO);
  const uint32_t hi = (uint32_t)a * R(MUL_HI);
  uint32_t p = lo + (hi << 8);
  spend(s, 4 + 3 + 9 + 4 + 4 + 4 + 3 + 9 + 2 + 3 + 4 + 2 + 2 + 4 + 3 + 2);
  if (R(PROD_SIGN) & 1) {
    spend(s, 2 + 2 + 2 + 3 + 4 + 2 + 3 + 4 + 2 + 3 + 4);
    p = (uint32_t)-(int32_t)p;
  } else {
    spend(s, 4);
  }
  w(s, PROD_0, (uint8_t)p);
  w(s, PROD_1, (uint8_t)(p >> 8));
  w(s, PROD_2, (uint8_t)(p >> 16));
  spend(s, 5);
}

// The pitch table, at byte `index`: a semitone to the word. Indexed by an 8-bit
// register, so a key past the table reads what follows it.
static uint16_t pitch_at(Sound* s, int index) {
  return (uint16_t)(R(PITCHES + index) | R(PITCHES + index + 1) << 8);
}

// $0E1B: T2 = A, and if it is negative, T3 and T2 both negated.
static void bend_sign(Sound* s, uint8_t a) {
  spend(s, 4);
  w(s, T2, a);
  if (a & 0x80) {
    spend(s, 2 + 2 + 2 + 3 + 4 + 2 + 2 + 3 + 4);
    w(s, T3, (uint8_t)-R(T3));
    w(s, T2, (uint8_t)-R(T2));
  } else {
    spend(s, 4);
  }
  spend(s, 5);
}

// $0E13: the channel's pitch wheel, into T3 (its range) and T2.
static void channel_bend(Sound* s, uint8_t chan) {
  spend(s, 5 + 4 + 5);
  w(s, T3, R(CH_BEND_RANGE + chan));
  bend_sign(s, R(CH_BEND + chan));
}

// $0E2E: how far voice `v`'s pitch moves for a bend of T2 over T3 semitones,
// into PROD_1 and PROD_2, doubled. Leaves CUR_VOICE_REG at the voice's.
static void bend_pitch(Sound* s, uint8_t v) {
  spend(s, 4 + 4 + 2 + 5 + 4);
  w(s, CUR_VOICE_REG, (uint8_t)(v << 4 | v >> 4));
  const uint8_t key = R(VOICE_KEY + v);
  const uint8_t far = (uint8_t)((uint8_t)(key + R(T3)) << 1);
  const uint8_t near = (uint8_t)(key << 1);
  const uint16_t d = (uint16_t)(pitch_at(s, far) - pitch_at(s, near));
  w(s, MUL_LO, (uint8_t)d);
  w(s, MUL_HI, (uint8_t)(d >> 8));
  spend(s, 5 + 2 + 3 + 2 + 2 + 5 + 4 + 5 + 4);
  spend(s, 5 + 2 + 2 + 2 + 3 + 5 + 4 + 3 + 5 + 4);
  spend(s, 3 + 8);
  mul_signed(s, R(T2));
  // CLRC : ROL $DA : ROL $DB
  const uint16_t p = (uint16_t)((R(PROD_1) | R(PROD_2) << 8) << 1);
  w(s, PROD_1, (uint8_t)p);
  w(s, PROD_2, (uint8_t)(p >> 8));
  spend(s, 2 + 4 + 4 + 4 + 4 + 5);
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------

// $0D57: CHAN, the current track's channel. Returns it, as Y.
static uint8_t track_channel(Sound* s, uint8_t track) {
  const uint8_t c = (uint8_t)((uint8_t)(track << 4 | track >> 4) + R(TRACK_CHANNEL + track));
  w(s, CHAN, c);
  spend(s, 2 + 5 + 2 + 4 + 2 + 4 + 5);
  return c;
}

// $0D9E: make voice `v` the current one.
static void voice_select(Sound* s, uint8_t v) {
  w(s, CUR_VOICE, v);
  w(s, CUR_VOICE_BIT, R(VOICE_BITS + v));
  w(s, CUR_VOICE_REG, (uint8_t)(v << 4 | v >> 4));
  spend(s, 4 + 5 + 4 + 2 + 5 + 4 + 5);
}

// $0DAA
static void voice_free(Sound* s, uint8_t v) {
  w(s, VOICE_ON + v, 0);
  spend(s, 2 + 6 + 5);
}

// $0DDD: key the current voice off.
static void voice_key_off(Sound* s) {
  spend(s, 3 + 2 + 8);
  dsp_set(s, 0x5C, R(CUR_VOICE_BIT));
  spend(s, 5);
}

// $0DB0: key the current voice on, with its echo and noise bits.
static void voice_key_on(Sound* s) {
  spend(s, 2 + 2 + 8);
  dsp_set(s, 0x5C, 0);
  spend(s, 3 + 2 + 8);
  dsp_set(s, 0x4C, R(CUR_VOICE_BIT));
  w(s, EON, (uint8_t)((~R(CUR_VOICE_BIT) & R(EON)) | R(NOTE_ECHO)));
  spend(s, 3 + 2 + 3 + 3 + 4 + 2 + 8);
  dsp_set(s, 0x4D, R(EON));
  w(s, NON, (uint8_t)((~R(CUR_VOICE_BIT) & R(NON)) | R(NOTE_NOISE)));
  spend(s, 3 + 2 + 3 + 3 + 4 + 2 + 8);
  dsp_set(s, 0x3D, R(NON));
  spend(s, 5);
}

// $0D60: a voice for a note of priority T1: the least recently used free
// one, or the least recent whose priority is no higher. -1 for none; else its
// place in VOICE_ORDER, and the voice in `*voice`.
static int voice_find(Sound* s, uint8_t* voice) {
  spend(s, 4 + 2);
  for (int i = 0; i < 8; i++) {
    const uint8_t v = R(VOICE_ORDER + i);
    spend(s, 4 + 2 + 5);
    if (!R(VOICE_ON + v)) {
      spend(s, 4);
      chip_mark(s, 0x0D87, (uint8_t)i);
      spend(s, 2 + 4 + 2 + 5);
      *voice = v;
      return i;
    }
    spend(s, 2 + 2 + 2 + (i < 7 ? 4 : 2));
  }
  // None free: the least recent of no higher priority.
  chip_at(s, 0x0D70);
  spend(s, 2);
  for (int i = 0; i < 8; i++) {
    const uint8_t v = R(VOICE_ORDER + i);
    spend(s, 4 + 2 + 3 + 5);
    if (R(T1) >= R(VOICE_PRIORITY + v)) {
      spend(s, 4);
      chip_mark(s, 0x0D87, (uint8_t)i);
      spend(s, 2 + 4 + 2 + 5);
      *voice = v;
      return i;
    }
    spend(s, 2 + 4 + 2 + 2 + (i < 7 ? 4 : 2));
  }
  chip_at(s, 0x0D84);
  spend(s, 4 + 2 + 5);
  return -1;
}

// $0D8B: voice `v`, at `place` in VOICE_ORDER, is used: it goes to the end of
// the order, is marked on, and becomes the current voice.
static void voice_claim(Sound* s, int place, uint8_t v) {
  spend(s, 2);
  for (int i = place; i != 7; i++) {
    w(s, VOICE_ORDER + i, R(VOICE_ORDER + i + 1));
    spend(s, 2 + 2 + 4 + 5 + 2 + 4);
  }
  spend(s, 2 + 4);
  chip_mark(s, 0x0D97, v);
  w(s, VOICE_ORDER + 7, v);
  w(s, VOICE_ON + v, 1);
  spend(s, 4 + 2 + 6);
  voice_select(s, v);
}

// Does voice `v` belong to channel `chan` of track `track`? The test four
// handlers make, $0823 and its kin: `MOV A, !$0048+Y : BEQ` and so on.
// Spends the test; true if it falls through to the voice's work.
static bool voice_is(Sound* s, uint8_t v, uint8_t track) {
  spend(s, 5);
  if (!R(VOICE_ON + v)) { spend(s, 4); return false; }
  spend(s, 2 + 2 + 5);
  if (R(VOICE_TRACK + v) != track) { spend(s, 4); return false; }
  spend(s, 2 + 4 + 5);
  if (R(VOICE_CHANNEL + v) != R(TRACK_CHANNEL + track)) { spend(s, 4); return false; }
  spend(s, 2);
  return true;
}

// $0E92: release every voice of track `track`.
static void track_release(Sound* s, uint8_t track) {
  spend(s, 2 + 4);
  w(s, T0, track);
  for (int v = 7; v >= 0; v--) {
    spend(s, 5);
    if (R(VOICE_ON + v)) {
      spend(s, 2 + 5 + 3);
      if (R(VOICE_TRACK + v) == R(T0)) {
        spend(s, 2 + 4 + 8);
        voice_select(s, (uint8_t)v);
        spend(s, 8);
        voice_free(s, (uint8_t)v);
        spend(s, 8);
        voice_key_off(s);
        spend(s, 4);
      } else {
        spend(s, 4);
      }
    } else {
      spend(s, 4);
    }
    spend(s, 2 + (v > 0 ? 4 : 2));
  }
  spend(s, 5);
}

// $0DE5: a volume for a voice of track `track`, from a pan of `pan` (0 to
// $7F, $40 the middle), the velocity in EV2, the track's volume and the
// channel's. The carry it leaves is the next one's borrow: see note_on.
static uint8_t pan_volume(Sound* s, uint8_t track, uint8_t pan, bool* carry) {
  spend(s, 4 + 2);
  if (R(MONO)) {
    spend(s, 2 + 2);
    pan = 0x40;
  } else {
    spend(s, 4);
  }
  spend(s, 2 + 2);
  uint8_t side;
  if (pan >= 0x40) {
    side = (uint8_t)(pan - 0x40);
    spend(s, 4);
  } else {
    side = 0;
    spend(s, 2 + 2);
  }
  w(s, T0, side);
  const uint8_t y = (uint8_t)(0x3F - side);
  uint16_t p = (uint16_t)(y * R(TRACK_VOLUME + track));
  p = (uint16_t)((uint8_t)(p >> 8 << 2) * R(EV2));
  p = (uint16_t)((uint8_t)(p >> 8) * R(CH_VOLUME + R(CHAN)));
  // ASL A : MOV A, Y : ROL A -- the top byte, doubled, with the carry of the
  // bottom one.
  *carry = p & 0x8000;
  spend(s, 4 + 2 + 2 + 3 + 2 + 4 + 9 + 2 + 2 + 2 + 3 + 9 + 2 + 4 + 3 + 5 + 4 + 9 + 2 + 2 + 2 + 5);
  return (uint8_t)(p >> 7);
}

// The two volumes of a voice from its channel's pan: $09FC, and the same at
// $0885. The right side is 127 - pan, less the carry the left one left.
static void voice_volumes(Sound* s, uint8_t track) {
  const uint8_t pan = R(CH_PAN + R(CHAN));
  bool carry;
  spend(s, 5 + 4 + 8);
  const uint8_t left = pan_volume(s, track, pan, &carry);
  spend(s, 2 + 8);
  voice_set(s, 0x00, left);
  w(s, T0, pan);
  spend(s, 4 + 4 + 2 + 3 + 8);
  const uint8_t right = pan_volume(s, track, (uint8_t)(0x7F - pan - !carry), &carry);
  spend(s, 2 + 8);
  voice_set(s, 0x01, right);
}

// ---------------------------------------------------------------------------
// Tracks
// ---------------------------------------------------------------------------

// $0E84: the next byte of track `x`'s stream. Leaves X as CUR_TRACK.
static uint8_t track_byte(Sound* s, uint8_t x) {
  const uint8_t p = (uint8_t)(x << 1);
  const uint16_t at = (uint16_t)(R(p) | R(p + 1) << 8);
  const uint8_t b = R(at);
  spend(s, 2 + 2 + 2 + 6 + 5);
  w(s, p, (uint8_t)(R(p) + 1));
  if (R(p)) {
    spend(s, 4);
  } else {
    spend(s, 2 + 5);
    w(s, (uint8_t)(p + 1), (uint8_t)(R((uint8_t)(p + 1)) + 1));
  }
  spend(s, 3 + 5);
  return b;
}

// $0E67: the delta time before track `t`'s next event, one byte or two, into
// TRACK_WAIT. True if it is not zero.
static bool track_delta(Sound* s, uint8_t t) {
  spend(s, 8);
  uint8_t b = track_byte(s, t);
  spend(s, 4 + 2 + 4);
  if (b & 0x80) {
    spend(s, 2 + 2 + 2 + 2 + 5 + 2 + 2 + 5 + 8);
    w(s, TRACK_WAIT + 4 + t, (uint8_t)((b & 0x7F) >> 1));
    w(s, TRACK_WAIT + t, (uint8_t)(b << 7));
    b = track_byte(s, t);
  } else {
    spend(s, 4);
  }
  w(s, TRACK_WAIT + t, (uint8_t)(b | R(TRACK_WAIT + t)));
  spend(s, 4 + 5 + 4 + 5);
  return R(TRACK_WAIT + t) | R(TRACK_WAIT + 4 + t);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// $0ACD: note off for note EV1 on the current track's channel.
static void note_off(Sound* s, uint8_t track) {
  chip_at(s, 0x0ACD);
  spend(s, 2);
  for (int i = 0; i < 8; i++) {
    const uint8_t v = R(VOICE_ORDER + i);
    spend(s, 4 + 2 + 5);
    bool match = false;
    if (!R(VOICE_ON + v)) {
      spend(s, 4);
    } else {
      spend(s, 2 + 3 + 5);
      if (R(EV1) != R(VOICE_NOTE + v)) {
        spend(s, 4);
      } else {
        spend(s, 2 + 3 + 5);
        if (R(CUR_TRACK) != R(VOICE_TRACK + v)) {
          spend(s, 4);
        } else {
          spend(s, 2 + 4 + 2 + 4 + 5 + 4);
          if (R(TRACK_CHANNEL + R(CUR_TRACK)) != R(VOICE_CHANNEL + v)) {
            spend(s, 4);
          } else {
            spend(s, 2);
            match = true;
          }
        }
      }
    }
    if (match) {
      spend(s, 8);
      voice_select(s, v);
      spend(s, 8);
      voice_free(s, v);
      spend(s, 8);
      voice_key_off(s);
      spend(s, 5);
      return;
    }
    spend(s, 2 + 2 + (i < 7 ? 4 : 2));
  }
  spend(s, 5);
  (void)track;
}

// $098C: note on: EV1 the note, EV2 the velocity.
static void note_on(Sound* s, uint8_t track) {
  chip_at(s, 0x098C);
  spend(s, 3);
  if (!R(EV2)) {
    spend(s, 2 + 3);
    note_off(s, track);
    return;
  }
  spend(s, 4 + 8);
  uint8_t chan = track_channel(s, track);
  w(s, T1, R(CH_PRIORITY + chan));
  spend(s, 5 + 4 + 5 + 2);
  if (R(CH_MUTE + chan) & 0x40) {
    spend(s, 4 + 5);
    return;
  }
  spend(s, 2 + 8);
  uint8_t v = 0;
  const int place = voice_find(s, &v);
  if (place < 0) {
    spend(s, 4 + 5);
    return;
  }
  spend(s, 2 + 8);
  voice_claim(s, place, v);

  w(s, VOICE_TRACK + v, R(CUR_TRACK));
  track = R(CUR_TRACK);
  w(s, VOICE_CHANNEL + v, R(TRACK_CHANNEL + track));
  w(s, VOICE_NOTE + v, R(EV1));
  w(s, VOICE_VELOCITY + v, R(EV2));
  spend(s, 3 + 6 + 2 + 4 + 6 + 3 + 6 + 3 + 6);

  chan = R(CHAN);
  spend(s, 3 + 4 + 3 + 5 + 2);
  uint8_t bit = R(CUR_VOICE_BIT);
  if (R(CH_FLAGS + chan) & 1) {
    spend(s, 4);
  } else {
    spend(s, 2 + 2);
    bit = 0;
  }
  w(s, NOTE_ECHO, bit);
  spend(s, 4 + 3 + 5 + 2);
  bit = R(CUR_VOICE_BIT);
  if (R(CH_FLAGS + chan) & 2) {
    spend(s, 4);
  } else {
    spend(s, 2 + 2);
    bit = 0;
  }
  w(s, NOTE_NOISE, bit);
  const uint8_t cv = R(CUR_VOICE);
  w(s, VOICE_VIB_TIME + cv, 0);
  w(s, VOICE_VIB_STEP + cv, 0);
  w(s, VOICE_VIB_DEPTH + cv, R(CH_VIB_DEPTH + chan));
  w(s, VOICE_VIB_WAVE + cv, R(CH_VIB_WAVE + chan));
  w(s, VOICE_VIB_RATE + cv, R(CH_VIB_RATE + chan));
  w(s, VOICE_VIB_RANGE + cv, R(CH_VIB_RANGE + chan));
  w(s, VOICE_PRIORITY + cv, R(CH_PRIORITY + chan));
  spend(s, 4 + 2 + 3 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 4);

  voice_volumes(s, track);

  // The instrument: the program's first entry, then the first of it whose
  // top note is at or above this one. A program with no entry plays the
  // first instrument's.
  spend(s, 3 + 5 + 2);
  const uint8_t program = R(CH_PROGRAM + chan);
  uint8_t i = 0;
  for (;;) {
    spend(s, 5);
    if (program == R(INS_PROGRAM + i)) { spend(s, 4); break; }
    spend(s, 2 + 2 + 2);
    i++;
    if (i < 0x20) { spend(s, 4); continue; }
    spend(s, 2 + 2);
    i = 0;
    break;
  }
  spend(s, 3);
  for (;;) {
    spend(s, 5);
    const uint8_t top = R(INS_TOP + i);
    if (R(EV1) == top) { spend(s, 4); break; }
    spend(s, 2);
    if (R(EV1) < top) { spend(s, 4); break; }
    spend(s, 2 + 2 + 4);
    i++;
  }
  spend(s, 2 + 2 + 5 + 2 + 8);
  voice_set(s, 0x04, R(INS_SAMPLE + i));
  spend(s, 5 + 2 + 8);
  voice_set(s, 0x05, R(INS_ADSR1 + i));
  spend(s, 5 + 2 + 8);
  voice_set(s, 0x06, R(INS_ADSR2 + i));

  uint8_t key = (uint8_t)(R(INS_SHIFT + i) + R(EV1));
  spend(s, 5 + 2 + 3 + 3 + 4 + 5 + 4);
  if (R(TRACK_SHIFTED + R(CUR_TRACK))) {
    spend(s, 2 + 2 + 4);
    key = (uint8_t)(key + R(SHIFT));
  } else {
    spend(s, 4);
  }
  const uint8_t v2 = R(CUR_VOICE);
  w(s, VOICE_KEY + v2, key);
  const uint8_t k2 = (uint8_t)(key << 1);
  w(s, T0, R(PITCHES + k2));
  w(s, T1, R(PITCHES + k2 + 1));
  const uint16_t step = (uint16_t)(pitch_at(s, k2 + 2) - pitch_at(s, k2));
  w(s, MUL_LO, (uint8_t)step);
  w(s, MUL_HI, (uint8_t)(step >> 8));
  spend(s, 3 + 6 + 2 + 2 + 5 + 4 + 5 + 4 + 2 + 5 + 5 + 4 + 5 + 5 + 4);

  // Fine tuning: (TUNE - $40) * 2 of a semitone's step, over 256.
  spend(s, 3 + 5 + 2 + 2 + 2 + 8);
  mul_signed(s, (uint8_t)((uint8_t)(R(CH_TUNE + R(CHAN)) - 0x40) << 1));
  uint16_t base = (uint16_t)((R(T0) | R(T1) << 8) + (R(PROD_1) | R(PROD_2) << 8));
  w(s, T0, (uint8_t)base);
  w(s, T1, (uint8_t)(base >> 8));
  w(s, VOICE_PITCH_LO + v2, (uint8_t)base);
  w(s, VOICE_PITCH_HI + v2, (uint8_t)(base >> 8));
  spend(s, 3 + 2 + 3 + 3 + 4 + 5 + 3 + 3 + 4 + 5);

  spend(s, 3 + 8);
  channel_bend(s, R(CHAN));
  spend(s, 3 + 8);
  bend_pitch(s, R(CUR_VOICE));
  const uint16_t pitch = (uint16_t)(base + (R(PROD_1) | R(PROD_2) << 8));
  w(s, T0, (uint8_t)pitch);
  w(s, T1, (uint8_t)(pitch >> 8));
  spend(s, 2 + 3 + 3 + 4 + 3 + 3 + 4 + 3 + 2 + 8);
  voice_set(s, 0x03, (uint8_t)(pitch >> 8));
  spend(s, 3 + 2 + 8);
  voice_set(s, 0x02, (uint8_t)pitch);
  spend(s, 8);
  voice_key_on(s);
  spend(s, 5);
}

// $075C: the pitch wheel. EV1 and EV2 are its two 7-bit halves; the top
// eight bits are kept, signed about the middle, for every voice the channel
// is playing. (No song the movies play moves it.)
static void pitch_wheel(Sound* s, uint8_t track) {
  chip_at(s, 0x075C);
  // ASL $CB : ROL $CB : ROL $CC
  const uint8_t lo = R(EV1);
  w(s, EV1, (uint8_t)(lo << 2 | lo >> 7));
  w(s, EV2, (uint8_t)(R(EV2) << 1 | (lo >> 6 & 1)));
  spend(s, 4 + 4 + 4 + 8);
  const uint8_t chan = track_channel(s, track);
  uint8_t bend = (uint8_t)(R(EV2) - 0x80);
  spend(s, 3 + 2 + 2 + 2);
  if (bend == 0x80) {
    spend(s, 2 + 2);
    bend = 0x81;
  } else {
    spend(s, 4);
  }
  w(s, CH_BEND + chan, bend);
  spend(s, 6 + 8);
  channel_bend(s, chan);
  spend(s, 2);
  for (int v = 7; v >= 0; v--) {
    if (voice_is(s, (uint8_t)v, track)) {
      spend(s, 8);
      bend_pitch(s, (uint8_t)v);
      const uint16_t p = (uint16_t)((R(VOICE_PITCH_LO + v) | R(VOICE_PITCH_HI + v) << 8) +
                                    (R(PROD_1) | R(PROD_2) << 8));
      spend(s, 4 + 2 + 5 + 3 + 4 + 5 + 3 + 2 + 8);
      voice_set(s, 0x03, (uint8_t)(p >> 8));
      spend(s, 4 + 2 + 8);
      voice_set(s, 0x02, (uint8_t)p);
      spend(s, 4);
    }
    spend(s, 2 + (v > 0 ? 4 : 2));
  }
  spend(s, 5);
}

// $0938: program change.
static void program_change(Sound* s, uint8_t track) {
  chip_at(s, 0x0938);
  spend(s, 8);
  const uint8_t chan = track_channel(s, track);
  w(s, CH_PROGRAM + chan, R(EV1));
  spend(s, 3 + 6 + 5);
}

// ---------------------------------------------------------------------------
// Controllers: event $B0, by controller number. Y is CHAN, X the track.
// ---------------------------------------------------------------------------

// $0821: the channel's vibrato, given again to every voice it is playing.
static void vibrato_update(Sound* s, uint8_t track) {
  spend(s, 2);
  for (int v = 7; v >= 0; v--) {
    if (voice_is(s, (uint8_t)v, track)) {
      spend(s, 4 + 4 + 3 + 8);
      const uint8_t chan = track_channel(s, R(CUR_TRACK));
      w(s, VOICE_VIB_DEPTH + v, R(CH_VIB_DEPTH + chan));
      w(s, VOICE_VIB_WAVE + v, R(CH_VIB_WAVE + chan));
      w(s, VOICE_VIB_RANGE + v, R(CH_VIB_RANGE + chan));
      w(s, VOICE_VIB_RATE + v, R(CH_VIB_RATE + chan));
      spend(s, 4 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 5 + 4 + 3);
    }
    spend(s, 2 + (v > 0 ? 4 : 2));
  }
  spend(s, 5);
}

// $0864: the channel's pan and volume, given again to every voice it is
// playing. (EV2 is left holding the last one's velocity.)
static void volume_update(Sound* s, uint8_t track) {
  spend(s, 2);
  for (int v = 7; v >= 0; v--) {
    if (voice_is(s, (uint8_t)v, track)) {
      w(s, EV2, R(VOICE_VELOCITY + v));
      w(s, CUR_VOICE_REG, (uint8_t)(v << 4));
      spend(s, 4 + 5 + 4 + 2 + 5 + 4 + 8);
      track_channel(s, track);
      voice_volumes(s, track);
      spend(s, 4);
    }
    spend(s, 2 + (v > 0 ? 4 : 2));
  }
  spend(s, 5);
}

// $08BF: a DSP register straight from the controller's value.
static void control_dsp(Sound* s, uint8_t reg) {
  spend(s, 3 + 8);
  dsp_set(s, reg, R(EV2));
  spend(s, 5);
}

static void control(Sound* s, uint8_t track, uint8_t number) {
  const uint8_t chan = R(CHAN);
  const uint8_t val = R(EV2);
  switch (number) {
    case 0x00:  // $0801: vibrato rate
      w(s, CH_VIB_RATE + chan, (uint8_t)(val << 1));
      spend(s, 3 + 2 + 6 + 4);
      vibrato_update(s, track);
      return;
    case 0x01:  // $0809: vibrato depth
      w(s, CH_VIB_DEPTH + chan, val);
      spend(s, 3 + 6 + 4);
      vibrato_update(s, track);
      return;
    case 0x02:  // $0810: vibrato range
      w(s, CH_VIB_RANGE + chan, val);
      spend(s, 3 + 6 + 4);
      vibrato_update(s, track);
      return;
    case 0x03:  // $0817: vibrato wave
      w(s, CH_VIB_WAVE + chan, (uint8_t)((val & 3) << 6));
      spend(s, 3 + 2 + 5 + 2 + 2 + 6);
      vibrato_update(s, track);
      return;
    case 0x04:  // $07F0: priority
      w(s, CH_PRIORITY + chan, val);
      spend(s, 3 + 6 + 5);
      return;
    case 0x05:  // $07F6: added to the mute byte
      w(s, CH_MUTE + chan, (uint8_t)(val + R(CH_MUTE + chan)));
      spend(s, 3 + 2 + 5 + 6 + 5);
      return;
    case 0x06:  // $0800
      spend(s, 5);
      return;
    case 0x07:  // $0858: volume
      w(s, CH_VOLUME + chan, val);
      spend(s, 3 + 6 + 4);
      volume_update(s, track);
      return;
    case 0x08:  // $085F: pan
    case 0x0A:
      w(s, CH_PAN + chan, val);
      spend(s, 3 + 6);
      volume_update(s, track);
      return;
    case 0x09:  // $08A5: echo and noise
      w(s, CH_FLAGS + chan, val);
      spend(s, 3 + 6 + 5);
      return;
    case 0x0B:  // $08AB: tuning
      w(s, CH_TUNE + chan, val);
      spend(s, 3 + 6 + 5);
      return;
    case 0x0C:  // $08B1: the main volume, left
      spend(s, 2 + 4);
      control_dsp(s, 0x0C);
      return;
    case 0x0D:  // $08B5: ...right
      spend(s, 2 + 4);
      control_dsp(s, 0x1C);
      return;
    case 0x0E:  // $08B9: the echo's volume, left
      spend(s, 2 + 4);
      control_dsp(s, 0x2C);
      return;
    case 0x0F:  // $08BD: ...right
      spend(s, 2);
      control_dsp(s, 0x3C);
      return;
    case 0x10:  // $08C5: the echo's buffer and delay, and echo writes on
      spend(s, 4 + 3 + 2 + 2 + 5 + 2 + 8);
      dsp_set(s, 0x6D, R(ECHO_PAGES + (val & 3)));
      spend(s, 3 + 2 + 2 + 8);
      dsp_set(s, 0x7D, val & 3);
      // AND A, $DF: the byte command $0D saves the upload address's low half
      // in. An immediate $DF (echo writes on) must have been meant.
      w(s, FLG, (uint8_t)(R(FLG) & R(SAVED_LO)));
      spend(s, 3 + 3 + 4 + 2 + 8);
      dsp_set(s, 0x6C, R(FLG));
      spend(s, 4 + 5);
      return;
    case 0x11:  // $08ED: the echo's feedback
      spend(s, 2 + 4);
      control_dsp(s, 0x0D);
      return;
    case 0x12:  // $08F1: the noise's rate
      w(s, EV2, val & 0x1F);
      w(s, FLG, (uint8_t)((R(FLG) & 0xE0) | R(EV2)));
      spend(s, 3 + 2 + 4 + 3 + 2 + 3 + 4 + 2 + 8);
      dsp_set(s, 0x6C, R(FLG));
      spend(s, 5);
      return;
    case 0x13:  // $0905: the track's volume
      w(s, TRACK_VOLUME + track, (uint8_t)(val << 1));
      spend(s, 3 + 2 + 5 + 5);
      return;
    case 0x14: {  // $090B: a loop starts here, to be played `val` more times
      w(s, TRACK_LOOPS + track, val);
      const uint8_t p = (uint8_t)(track << 1);
      w(s, TRACK_LOOP + p, R(TRACK_PTR + p));
      w(s, TRACK_LOOP + p + 1, R(TRACK_PTR + p + 1));
      spend(s, 3 + 5 + 2 + 2 + 2 + 5 + 6 + 5 + 6 + 5);
      return;
    }
    case 0x15: {  // $091F: ...and ends here. A count of 0 loops for ever.
      spend(s, 4);
      if (R(TRACK_LOOPS + track)) {
        spend(s, 2 + 5);
        w(s, TRACK_LOOPS + track, (uint8_t)(R(TRACK_LOOPS + track) - 1));
        if (!R(TRACK_LOOPS + track)) {
          spend(s, 2 + 5);
          return;
        }
        spend(s, 4);
      } else {
        spend(s, 4);
      }
      const uint8_t p = (uint8_t)(track << 1);
      w(s, TRACK_PTR + p, R(TRACK_LOOP + p));
      w(s, TRACK_PTR + p + 1, R(TRACK_LOOP + p + 1));
      spend(s, 2 + 2 + 2 + 5 + 6 + 5 + 6 + 5);
      return;
    }
  }
}

// $07A9: event $B0, a controller. Those past $15 are ignored.
static void controller(Sound* s, uint8_t track) {
  chip_at(s, 0x07A9);
  spend(s, 8);
  track_channel(s, track);
  const uint8_t number = R(EV1);
  spend(s, 3 + 2);
  if (number >= 0x16) {
    spend(s, 4 + 5);
    return;
  }
  // The handler's address goes into the JMP at $07C1.
  w(s, 0x07C2, R(0x07C4 + number));
  w(s, 0x07C3, R(0x07DA + number));
  spend(s, 2 + 4 + 2 + 5 + 5 + 5 + 5 + 4 + 3);
  control(s, track, number);
}

// ---------------------------------------------------------------------------
// The tracks
// ---------------------------------------------------------------------------

// $0941: event $FF, a meta event. False for the end of the track.
static bool meta_event(Sound* s, uint8_t track) {
  chip_at(s, 0x0941);
  spend(s, 8);
  const uint8_t type = track_byte(s, track);
  spend(s, 2);
  if (type == 0x2F) {  // the end
    spend(s, 4 + 4 + 4 + 3);
    return false;
  }
  spend(s, 2 + 2);
  if (type == 0x51) {  // the tempo, three bytes after a length
    spend(s, 4 + 8);
    track_byte(s, track);
    spend(s, 8);
    w(s, TRACK_TEMPO + 8 + track, track_byte(s, track));
    spend(s, 5 + 8);
    w(s, TRACK_TEMPO + 4 + track, track_byte(s, track));
    spend(s, 5 + 8);
    w(s, TRACK_TEMPO + track, track_byte(s, track));
    spend(s, 5 + 5);
    return true;
  }
  spend(s, 2 + 2);
  if (type == 0x30) {  // back to the song's start
    const uint8_t t = R(CUR_TRACK);
    const uint8_t p = (uint8_t)(t << 1);
    const uint8_t song = R(TRACK_SONG + t);
    w(s, TRACK_PTR + p, R(SONG_LO + song));
    w(s, TRACK_PTR + p + 1, R(SONG_HI + song));
    spend(s, 4 + 3 + 2 + 2 + 2 + 4 + 2 + 5 + 6 + 5 + 6 + 5);
    return true;
  }
  // Anything else is skipped, by its length. (Not as written: each byte is
  // read with X counting down, and $0E84 takes X for the track. No song the
  // movies play has one.)
  spend(s, 2 + 8);
  const uint8_t len = track_byte(s, track);
  spend(s, 2);
  if (!len) {
    spend(s, 4 + 5);
    return true;
  }
  spend(s, 2 + 2);
  uint8_t x = len;
  for (;;) {
    spend(s, 8);
    track_byte(s, x);
    x = (uint8_t)(R(CUR_TRACK) - 1);
    spend(s, 2);
    if (!x) break;
    spend(s, 4);
  }
  spend(s, 2 + 5);
  return true;
}

// $0705: the track's next event. False for the end of the track.
static bool track_event(Sound* s, uint8_t track) {
  chip_at(s, 0x0705);
  spend(s, 8);
  uint8_t b = track_byte(s, track);
  spend(s, 4 + 2 + 4);
  if (b & 0x80) {  // a status byte, or running status if not
    w(s, TRACK_CHANNEL + track, b & 0x0F);
    w(s, TRACK_STATUS + track, b & 0xF0);
    spend(s, 2 + 4 + 2 + 5 + 4 + 2 + 5 + 2);
    if ((b & 0xF0) == 0xF0) {
      spend(s, 2 + 3);
      return meta_event(s, track);
    }
    spend(s, 4 + 8);
    b = track_byte(s, track);
  } else {
    spend(s, 4);
  }
  w(s, EV1, b);
  const uint8_t status = R(TRACK_STATUS + track);
  spend(s, 4 + 4 + 2);
  if (status == 0xC0) {
    spend(s, 2 + 3);
    program_change(s, track);
    return true;
  }
  spend(s, 4 + 2);
  if (status == 0x80) {  // one data byte only
    spend(s, 2 + 3);
    note_off(s, track);
    return true;
  }
  spend(s, 4 + 8);
  w(s, EV2, track_byte(s, track));
  const uint8_t index = (uint8_t)((uint8_t)(status - 0x80) << 4 | (uint8_t)(status - 0x80) >> 4);
  // The handler's address goes into the JMP at $074B.
  w(s, 0x074C, R(0x074E + index));
  w(s, 0x074D, R(0x0755 + index));
  spend(s, 4 + 4 + 2 + 2 + 5 + 2 + 5 + 5 + 5 + 5 + 3);
  switch (index) {
    case 1: note_on(s, track); return true;
    case 3: controller(s, track); return true;
    case 4: program_change(s, track); return true;
    case 6: pitch_wheel(s, track); return true;
    default:
      // $A0 and $D0 jump to $0000.
      chip_halt(s, "a song used event $A0 or $D0, which the driver jumps to $0000 for");
      return true;
  }
}

// $06B5: a tick of timer 0, 500 a second. Each playing track's clock runs
// down by $039378; when it runs out it is wound back by the tempo and the
// track is a beat on, and when the beats to its next event have passed, that
// event and any after it with no wait between are played.
static void tick(Sound* s) {
  chip_at(s, 0x06B5);
  spend(s, 2 + 4);
  w(s, CUR_TRACK, 3);
  for (;;) {
    const uint8_t t = R(CUR_TRACK);
    spend(s, 4 + 4);
    if (R(TRACK_WAIT + t) | R(TRACK_WAIT + 4 + t)) {
      spend(s, 2 + 2 + 4 + 2 + 5 + 4 + 2 + 5 + 4 + 2 + 5);
      const uint32_t clock =
          (uint32_t)(R(TRACK_CLOCK + t) | R(TRACK_CLOCK + 4 + t) << 8 | R(TRACK_CLOCK + 8 + t) << 16);
      uint32_t c = (clock - 0x039378u) & 0xFFFFFF;
      bool beat = clock < 0x039378u;
      if (beat) {
        spend(s, 2 + 2 + 4 + 4 + 5 + 4 + 4 + 5 + 4 + 4 + 5);
        const uint32_t tempo =
            (uint32_t)(R(TRACK_TEMPO + t) | R(TRACK_TEMPO + 4 + t) << 8 | R(TRACK_TEMPO + 8 + t) << 16);
        c = (c + tempo) & 0xFFFFFF;
      } else {
        spend(s, 4);
      }
      w(s, TRACK_CLOCK + t, (uint8_t)c);
      w(s, TRACK_CLOCK + 4 + t, (uint8_t)(c >> 8));
      w(s, TRACK_CLOCK + 8 + t, (uint8_t)(c >> 16));
      if (beat) {
        uint8_t lo = R(TRACK_WAIT + t);
        spend(s, 4);
        if (lo) {
          spend(s, 4);
        } else {
          spend(s, 2 + 5);
          w(s, TRACK_WAIT + 4 + t, (uint8_t)(R(TRACK_WAIT + 4 + t) - 1));
        }
        lo--;
        w(s, TRACK_WAIT + t, lo);
        spend(s, 2 + 5 + 4);
        if (lo | R(TRACK_WAIT + 4 + t)) {
          spend(s, 4);
        } else {
          spend(s, 2);
          for (;;) {
            spend(s, 8);
            if (!track_event(s, R(CUR_TRACK))) break;
            if (s->state == SOUND_HALTED) return;
            spend(s, 3 + 8);
            if (track_delta(s, R(CUR_TRACK))) {
              spend(s, 2);
              break;
            }
            spend(s, 4);
          }
        }
      }
    } else {
      spend(s, 4);
    }
    // $06FE
    w(s, CUR_TRACK, (uint8_t)(R(CUR_TRACK) - 1));
    spend(s, 4 + 3);
    if (R(CUR_TRACK) & 0x80) {
      spend(s, 2 + 5);
      return;
    }
    spend(s, 4);
  }
}

// $0D03: each voice's vibrato. When its time overflows, it takes the next of
// 64 steps through its wave, and its pitch is bent by the wave times the
// depth over the range.
static void vibrato(Sound* s) {
  chip_at(s, 0x0D03);
  spend(s, 2);
  for (int v = 7; v >= 0; v--) {
    spend(s, 4);
    if (R(VOICE_ON + v)) {
      const unsigned sum = R(VOICE_VIB_RATE + v) + R(VOICE_VIB_TIME + v);
      w(s, VOICE_VIB_TIME + v, (uint8_t)sum);
      spend(s, 2 + 2 + 4 + 4 + 5);
      if (sum > 0xFF) {
        w(s, T3, R(VOICE_VIB_RANGE + v));
        w(s, VOICE_VIB_STEP + v, (uint8_t)(R(VOICE_VIB_STEP + v) + 1));
        uint8_t step = R(VOICE_VIB_STEP + v);
        spend(s, 2 + 4 + 4 + 5 + 4 + 2);
        if (step == 0x40) {
          spend(s, 2 + 2 + 5);
          step = 0;
          w(s, VOICE_VIB_STEP + v, 0);
        } else {
          spend(s, 4);
        }
        const uint8_t y = (uint8_t)(step + R(VOICE_VIB_WAVE + v));
        spend(s, 2 + 4 + 2 + 5 + 8);
        bend_sign(s, R(VIB_WAVES + y));
        spend(s, 2 + 2 + 8);
        bend_pitch(s, (uint8_t)v);
        w(s, MUL_LO, R(PROD_1));
        w(s, MUL_HI, R(PROD_2));
        spend(s, 3 + 4 + 3 + 4 + 4 + 8);
        mul_signed(s, R(VOICE_VIB_DEPTH + v));
        const uint16_t p = (uint16_t)((R(PROD_1) | R(PROD_2) << 8) +
                                      (R(VOICE_PITCH_LO + v) | R(VOICE_PITCH_HI + v) << 8));
        spend(s, 2 + 3 + 4 + 4 + 3 + 4 + 2 + 8);
        voice_set(s, 0x03, (uint8_t)(p >> 8));
        spend(s, 4 + 2 + 8);
        voice_set(s, 0x02, (uint8_t)p);
      } else {
        spend(s, 4);
      }
    } else {
      spend(s, 4);
    }
    spend(s, 2 + (v > 0 ? 4 : 2));
  }
  spend(s, 5);
}

// ---------------------------------------------------------------------------
// Commands from the game
// ---------------------------------------------------------------------------

// $0EB1: the sixteen channels of track `t`, as a song starts.
static void channels_reset(Sound* s, uint8_t t) {
  spend(s, 2 + 5 + 2 + 2);
  uint8_t y = (uint8_t)(t << 4 | t >> 4);
  for (int i = 16; i > 0; i--, y++) {
    w(s, CH_PAN + y, 0x40);
    w(s, CH_TUNE + y, 0x40);
    w(s, CH_PRIORITY + y, 0x40);
    w(s, CH_VOLUME + y, 0x7F);
    w(s, CH_PROGRAM + y, 0);
    w(s, CH_FLAGS + y, 0);
    w(s, CH_BEND + y, 0);
    w(s, CH_VIB_DEPTH + y, 0);
    w(s, CH_MUTE + y, 0);
    w(s, CH_VIB_RATE + y, 0x20);
    w(s, CH_VIB_RANGE + y, 0x02);
    w(s, CH_BEND_RANGE + y, 0x0C);
    spend(s, 88 + (i > 1 ? 4 : 2));
  }
  spend(s, 5);
}

// $0B8A, command $01: start song PARAM on a free track. False if it is not a
// song, or no track is free. The track is left in `*track`.
static bool start_song(Sound* s, uint8_t* track) {
  chip_at(s, 0x0B8A);
  const uint8_t song = R(PARAM);
  spend(s, 3 + 3);
  if (song >= R(SONGS)) {
    spend(s, 4);
    chip_at(s, 0x0B9B);
    spend(s, 2 + 5);
    return false;
  }
  spend(s, 2 + 2);
  int t = 3;
  for (; t >= 0; t--) {
    spend(s, 4 + 4);
    if (!(R(TRACK_WAIT + t) | R(TRACK_WAIT + 4 + t))) break;
    spend(s, 2 + 2 + (t > 0 ? 4 : 2));
  }
  if (t < 0) {
    chip_at(s, 0x0B9B);
    spend(s, 2 + 5);
    return false;
  }
  spend(s, 4);
  chip_mark(s, 0x0B9D, (uint8_t)t);
  w(s, TRACK_SONG + t, song);
  w(s, TRACK_CLOCK + t, song);
  w(s, TRACK_CLOCK + 4 + t, song);
  w(s, TRACK_CLOCK + 8 + t, song);
  w(s, TRACK_LOOPS + t, 0);
  w(s, TRACK_SHIFTED + t, 0);
  w(s, TRACK_TEMPO + t, 0xC0);
  w(s, TRACK_TEMPO + 4 + t, 0x27);
  w(s, TRACK_TEMPO + 8 + t, 0x09);
  w(s, TRACK_VOLUME + t, 0xFF);
  spend(s, 3 + 5 + 5 + 5 + 5 + 2 + 5 + 5 + 2 + 5 + 2 + 5 + 2 + 5 + 2 + 5 + 4 + 8);
  channels_reset(s, (uint8_t)t);
  const uint8_t p = (uint8_t)(t << 1);
  w(s, TRACK_PTR + p, R(SONG_LO + song));
  w(s, TRACK_LOOP + p, R(SONG_LO + song));
  w(s, TRACK_PTR + p + 1, R(SONG_HI + song));
  w(s, TRACK_LOOP + p + 1, R(SONG_HI + song));
  w(s, CUR_TRACK, (uint8_t)t);
  spend(s, 4 + 4 + 2 + 2 + 3 + 5 + 5 + 5 + 5 + 5 + 5 + 4 + 4 + 8);
  if (track_delta(s, (uint8_t)t)) {
    spend(s, 4);
  } else {
    spend(s, 2 + 5);
    w(s, TRACK_WAIT + t, (uint8_t)(R(TRACK_WAIT + t) + 1));
  }
  spend(s, 2 + 5);
  *track = (uint8_t)t;
  return true;
}

// $0BE1, command $02: stop song PARAM, on the first track playing it.
static void stop_song(Sound* s) {
  spend(s, 2);
  for (int t = 3; t >= 0; t--) {
    spend(s, 4 + 4);
    if (R(TRACK_WAIT + t) | R(TRACK_WAIT + 4 + t)) {
      spend(s, 2 + 3 + 4);
      if (R(PARAM) == R(TRACK_SONG + t)) {
        spend(s, 4 + 2);
        w(s, TRACK_WAIT + t, 0);
        w(s, TRACK_WAIT + 4 + t, 0);
        spend(s, 5 + 5 + 8);
        track_release(s, (uint8_t)t);
        spend(s, 5);
        return;
      }
      spend(s, 2);
    } else {
      spend(s, 4);
    }
    spend(s, 2 + (t > 0 ? 4 : 2));
  }
  spend(s, 5);
}

// $0C46: the upload address, one on.
static void upload_next(Sound* s) {
  w(s, UPLOAD_LO, (uint8_t)(R(UPLOAD_LO) + 1));
  spend(s, 4);
  if (R(UPLOAD_LO)) {
    spend(s, 4);
  } else {
    spend(s, 2 + 4);
    w(s, UPLOAD_HI, (uint8_t)(R(UPLOAD_HI) + 1));
  }
  spend(s, 5);
}

// $0C3C: MOV [$C4]+Y, A with Y zero : BRA $0C46
static void upload_byte(Sound* s, uint8_t val) {
  chip_ram_put(s, 6, (uint16_t)(R(UPLOAD_LO) | R(UPLOAD_HI) << 8), val);
  spend(s, 7 + 4);
  upload_next(s);
}

// $0C5C, command $08: the echo off and its buffer cleared, every track
// stopped, and the uploads put back to where command $0D saved them.
static void reset_songs(Sound* s) {
  w(s, FLG, R(FLG) | 0x20);
  spend(s, 3 + 2 + 4 + 2 + 8);
  dsp_set(s, 0x6C, R(FLG));
  spend(s, 2 + 2 + 8);
  dsp_set(s, 0x0D, 0);
  spend(s, 2 + 8);
  dsp_set(s, 0x2C, 0);
  spend(s, 2 + 8);
  dsp_set(s, 0x3C, 0);
  w(s, T1, 0xE7);
  w(s, T0, 0);
  spend(s, 2 + 4 + 2 + 4 + 2);
  for (;;) {
    const uint16_t page = (uint16_t)(R(T1) << 8);
    for (int y = 0; y < 256; y++) {
      chip_ram_put(s, 6, (uint16_t)(page + y), 0);
      spend(s, 7 + 2 + (y < 255 ? 4 : 2));
    }
    w(s, T1, (uint8_t)(R(T1) + 1));
    spend(s, 4);
    if (!R(T1)) { spend(s, 2); break; }
    spend(s, 4);
  }
  spend(s, 2);
  for (int t = 3; t >= 0; t--) {
    spend(s, 4 + 4);
    if (R(TRACK_WAIT + t) | R(TRACK_WAIT + 4 + t)) {
      spend(s, 2 + 2 + 5 + 5 + 4 + 8);
      w(s, TRACK_WAIT + t, 0);
      w(s, TRACK_WAIT + 4 + t, 0);
      track_release(s, (uint8_t)t);
      spend(s, 4);
    } else {
      spend(s, 4);
    }
    spend(s, 2 + (t > 0 ? 4 : 2));
  }
  w(s, UPLOAD_LO, R(SAVED_LO));
  w(s, UPLOAD_HI, R(SAVED_HI));
  w(s, SONGS, R(SAVED_SONGS));
  spend(s, 3 + 4 + 3 + 4 + 3 + 4 + 5);
}

// $0CBA, command $14: the last song registered is a sound with its own
// sample: six bytes of instrument at its start go in as instrument $18, for
// program $15, and its sample, at +$0A with its loop at +$08, as sample $15.
static void sample_song(Sound* s) {
  const uint8_t last = (uint8_t)(R(SONGS) - 1);
  w(s, T0, R(SONG_LO + last));
  w(s, T1, R(SONG_HI + last));
  const uint16_t at = (uint16_t)(R(T0) | R(T1) << 8);
  spend(s, 3 + 2 + 5 + 4 + 5 + 4 + 2 + 2);
  uint8_t x = 0;
  for (int y = 0; y < 6; y++) {
    w(s, (uint16_t)(0x1358 + x), R(at + y));
    x = (uint8_t)(x + 0x20);
    spend(s, 6 + 6 + 2 + 2 + 2 + 2 + 2 + 2 + (y < 5 ? 4 : 2));
  }
  w(s, 0x13F8, 0x15);
  spend(s, 2 + 5 + 2 + 3 + 2);
  const uint16_t start = (uint16_t)(at + 0x0A);
  chip_ram_put(s, 4, DIRECTORY + 0x54, (uint8_t)start);
  spend(s, 5 + 3 + 2);
  chip_ram_put(s, 4, DIRECTORY + 0x55, (uint8_t)(start >> 8));
  spend(s, 5 + 2 + 2 + 4 + 6);
  const uint16_t loop = (uint16_t)(start + (R(at + 8) | R(at + 9) << 8));
  chip_ram_put(s, 4, DIRECTORY + 0x56, (uint8_t)loop);
  spend(s, 5 + 2 + 4 + 6);
  chip_ram_put(s, 4, DIRECTORY + 0x57, (uint8_t)(loop >> 8));
  spend(s, 5 + 5);
}

// $0BFE, command $03: bytes of a MIDI message, gathered. Nothing reads them.
static void midi_byte(Sound* s) {
  uint8_t y = R(MIDI_COUNT);
  spend(s, 4 + 3);
  if (R(PARAM) & 0x80) {
    spend(s, 2 + 2);
    y = 0;
  } else {
    spend(s, 4);
  }
  w(s, (uint16_t)(MIDI_BYTES + y), R(PARAM));
  y++;
  w(s, MIDI_COUNT, y);
  spend(s, 6 + 2 + 5 + 2);
  if (y == 1) {
    spend(s, 4 + 5);
    return;
  }
  spend(s, 2 + 4 + 2 + 2);
  if ((R(MIDI_BYTES) & 0xF0) == 0xC0) {
    spend(s, 4 + 5);
    return;
  }
  spend(s, 2 + 2 + (y == 2 ? 4 : 2) + 5);
}

// Command `cmd`, jumped to from the poll. False if it restarts the driver.
static bool command(Sound* s, uint8_t cmd) {
  const uint8_t param = R(PARAM);
  switch (cmd) {
    case 0x01: {
      uint8_t t;
      start_song(s, &t);
      return true;
    }
    case 0x02:
      chip_at(s, 0x0BE1);
      stop_song(s);
      return true;
    case 0x03:
      midi_byte(s);
      return true;
    case 0x04:  // $0C20: the upload address
      w(s, UPLOAD_LO, param);
      spend(s, 3 + 4 + 5);
      return true;
    case 0x05:  // $0C25
      w(s, UPLOAD_HI, param);
      spend(s, 3 + 4 + 5);
      return true;
    case 0x06:  // $0C38: a byte uploaded
      spend(s, 3 + 2);
      upload_byte(s, param);
      return true;
    case 0x07:  // $0C40: a byte read back, on port 0
      spend(s, 2 + 6);
      chip_port_out(s, 0, R((uint16_t)(R(UPLOAD_LO) | R(UPLOAD_HI) << 8)));
      spend(s, 4);
      upload_next(s);
      return true;
    case 0x08:
      chip_at(s, 0x0C5C);
      reset_songs(s);
      return true;
    case 0x09:  // $0B5F: start again
      spend(s, 3);
      return false;
    case 0x0A: {  // $0C4D: a song starts at the upload address
      const uint8_t n = R(SONGS);
      w(s, (uint16_t)(SONG_LO + n), R(UPLOAD_LO));
      w(s, (uint16_t)(SONG_HI + n), R(UPLOAD_HI));
      w(s, SONGS, (uint8_t)(n + 1));
      spend(s, 3 + 3 + 6 + 3 + 6 + 4 + 5);
      return true;
    }
    case 0x0B:  // $0C2A: two bytes uploaded, PARAM then PARAM0
      chip_ram_put(s, 2 + 3 + 6, (uint16_t)(R(UPLOAD_LO) | R(UPLOAD_HI) << 8), param);
      spend(s, 3 + 2 + 7 + 8);
      upload_next(s);
      spend(s, 3 + 3);
      upload_byte(s, R(PARAM0));
      return true;
    case 0x0C:  // $0B62: pause the tracks
      w(s, PAUSED, param);
      spend(s, 3 + 4 + 5);
      return true;
    case 0x0D:  // $0CAD: remember the uploads so far
      w(s, SAVED_LO, R(UPLOAD_LO));
      w(s, SAVED_HI, R(UPLOAD_HI));
      w(s, SAVED_SONGS, R(SONGS));
      spend(s, 3 + 4 + 3 + 4 + 3 + 4 + 5);
      return true;
    case 0x0E:  // $0B67: mono
      w(s, MONO, param);
      spend(s, 3 + 5 + 5);
      return true;
    case 0x11: {  // $0B80: a song started with its notes shifted
      uint8_t t;
      spend(s, 8);
      if (start_song(s, &t)) {
        spend(s, 2 + 2 + 5);
        w(s, TRACK_SHIFTED + t, 1);
      } else {
        spend(s, 4);
      }
      spend(s, 5);
      return true;
    }
    case 0x12:  // $0B7A: ...by this much
      w(s, SHIFT, param);
      spend(s, 3 + 5 + 5);
      return true;
    case 0x13:  // $0B6D: the main volume
      spend(s, 3 + 2 + 8);
      dsp_set(s, 0x0C, param);
      spend(s, 2 + 8);
      dsp_set(s, 0x1C, param);
      spend(s, 5);
      return true;
    case 0x14:
      chip_at(s, 0x0CBA);
      sample_song(s);
      return true;
    default:  // $00, $0F and $10
      spend(s, 5);
      return true;
  }
}

// ---------------------------------------------------------------------------
// Starting, and the main loop
// ---------------------------------------------------------------------------

// $0F47: the DSP's registers from the table of pairs at $118E.
static void dsp_defaults(Sound* s) {
  spend(s, 2);
  for (int x = 0;; x += 2) {
    const uint8_t reg = R(DSP_INIT + x), val = R(DSP_INIT + x + 1);
    spend(s, 5 + 2 + 2 + 5 + 2 + 2);
    if (reg == 0xFF) break;
    spend(s, 2 + 8);
    dsp_set(s, reg, val);
    spend(s, 4);
  }
  spend(s, 4 + 5);
}

// $0F5C: the echo set up for a delay of 1, if it is not already. A delay
// longer than the one in force needs its buffer cleared first; the echo is
// muted, and given time, before it is changed. The buffer goes below $D000.
static void echo_setup(Sound* s) {
  w(s, ECHO_DELAY, 1);
  spend(s, 2 + 4 + 2 + 5);
  uint8_t a = chip_dsp_read(s, 3, 0x7D);
  spend(s, 4 + 3);
  if (a != R(ECHO_DELAY)) {
    spend(s, 2 + 2 + 2);
    a = (uint8_t)~(a & 0x0F);
    if (R(ECHO_E9) & 0x80) {
      spend(s, 5 + 2 + 3);
      a = (uint8_t)(a + R(ECHO_E9));
    } else {
      spend(s, 7);
    }
    w(s, ECHO_E9, a);
    spend(s, 4 + 2);
    for (int y = 4; y > 0; y--) {
      spend(s, 5 + 5 + 2);
      chip_dsp_write(s, 4, R(ECHO_CLEARS + y), 0);
      spend(s, 5 + (y > 1 ? 6 : 4));
    }
    spend(s, 3 + 2 + 2 + 8);
    dsp_set(s, 0x6C, R(ECHO_FLG) | 0x20);
    a = R(ECHO_DELAY);
    spend(s, 3 + 2 + 8);
    dsp_set(s, 0x7D, a);
  } else {
    spend(s, 4);
  }
  spend(s, 2 + 2 + 2 + 2 + 2 + 2 + 2 + 3);
  dsp_set(s, 0x6D, (uint8_t)((uint8_t)~(a << 3) + 0xD1));
}

// $0600: the driver starts. Its RAM cleared, the ports zeroed, the echo set
// up, everything above $A203 cleared, the DSP set, and timer 0 started at
// 500 Hz. Then $A2 on port 3: ready.
static void driver_start(Sound* s) {
  chip_at(s, 0x0600);
  spend(s, 3 + 2 + 2 + 2 + 2 + 2);
  for (int x = 0; x < 0xF0; x++) {
    w(s, (uint16_t)x, 0);
    spend(s, 4 + 2 + (x < 0xEF ? 4 : 2));
  }
  spend(s, 2);
  for (int x = 0; x < 256; x++) {
    for (int page = 1; page <= 5; page++) w(s, (uint16_t)(page << 8 | x), 0);
    spend(s, 6 * 5 + 2 + (x < 255 ? 4 : 2));
  }
  for (int p = 0; p < 4; p++) {
    chip_port_out(s, p, 0);
    spend(s, 4);
  }
  spend(s, 8);
  echo_setup(s);
  spend(s, 2);
  chip_control(s, 3, 0x30);
  spend(s, 4);
  w(s, MONO, 0x30);
  w(s, T1, 0xA2);
  spend(s, 5 + 2 + 2 + 4 + 2);
  uint8_t y = 3;
  for (;;) {
    chip_ram_put(s, 6, (uint16_t)((R(T0) | R(T1) << 8) + y), 0);
    y++;
    spend(s, 7 + 2);
    if (y) { spend(s, 4); continue; }
    w(s, T1, (uint8_t)(R(T1) + 1));
    spend(s, 2 + 4);
    if (R(T1)) { spend(s, 4); continue; }
    spend(s, 2);
    break;
  }
  spend(s, 2 + 2 + 8);
  dsp_set(s, 0x5D, 0x14);
  spend(s, 8);
  dsp_defaults(s);
  spend(s, 2);
  for (int x = 7; x >= 0; x--) {
    w(s, (uint16_t)(VOICE_ORDER + x), (uint8_t)x);
    spend(s, 2 + 5 + 2 + (x > 0 ? 4 : 2));
  }
  spend(s, 2);
  chip_t0_target(s, 0x10);
  spend(s, 4 + 2);
  chip_control(s, 3, 0x01);
  spend(s, 4);
  w(s, SAVED_LO, 3);
  w(s, SAVED_HI, 0xA2);
  spend(s, 2 + 4 + 2 + 4);
  chip_port_out(s, 3, 0xA2);
  spend(s, 4);
  w(s, SEQ, 0xA2);
  spend(s, 4);
}

// $0AFF and $06A4: the poll, then the tick if one is due. The poll is the
// driver's only read of the ports, and it can stop at each of its five reads;
// the rest runs whole.
void driver_run(Sound* s) {
  uint8_t v;
  for (;;) {
    switch (s->state) {
      case SOUND_DRIVER_START:
        driver_start(s);
        spend(s, 8);  // $06A1: CALL $0AFF
        s->state = SOUND_DRIVER_POLL;
        continue;

      case SOUND_DRIVER_POLL:  // MOV X, $F7 : CMP X, $C9 : BEQ
      case SOUND_DRIVER_POLL_2:
        if (!chip_port_in(s, 3, &s->x)) return;
        if (s->state == SOUND_DRIVER_POLL) chip_at(s, 0x0AFF);
        spend(s, 3 + 3);
        if (s->x == R(SEQ)) {
          spend(s, 4 + 5);
          break;
        }
        spend(s, 2);
        // Read twice: the game writes the count last, but the port may be
        // caught as it changes.
        s->state = s->state == SOUND_DRIVER_POLL ? SOUND_DRIVER_POLL_2 : SOUND_DRIVER_POLL_3;
        continue;

      case SOUND_DRIVER_POLL_3:  // each port read, echoed, and kept
      case SOUND_DRIVER_POLL_4: {
        const int port = s->state == SOUND_DRIVER_POLL_3 ? 0 : 1;
        if (!chip_port_in(s, port, &v)) return;
        spend(s, 3);
        chip_port_out(s, port, v);
        spend(s, 4);
        w(s, port ? PARAM : PARAM0, v);
        spend(s, 4);
        s->state = s->state == SOUND_DRIVER_POLL_3 ? SOUND_DRIVER_POLL_4 : SOUND_DRIVER_POLL_5;
        continue;
      }

      case SOUND_DRIVER_POLL_5:
        if (!chip_port_in(s, 2, &v)) return;
        spend(s, 3);
        chip_port_out(s, 2, v);
        spend(s, 4);
        w(s, CMD, v);
        spend(s, 4);
        chip_mark(s, 0x0B1D, s->x);
        chip_port_out(s, 3, s->x);  // the answer
        spend(s, 4);
        w(s, SEQ, s->x);
        spend(s, 4 + 2);
        if (v >= 0x15) {
          spend(s, 4 + 5);
          break;
        }
        // The handler's address goes into the JMP at $0B31.
        w(s, 0x0B32, R(0x0B34 + v));
        w(s, 0x0B33, R(0x0B49 + v));
        spend(s, 2 + 5 + 5 + 5 + 5 + 3);
        if (!command(s, v)) {
          s->state = SOUND_DRIVER_START;
          continue;
        }
        if (s->state == SOUND_HALTED) return;
        break;

      default:
        return;
    }

    // $06A4: MOV A, $FD : BEQ $06A1 -- a tick due? -- then paused?
    s->state = SOUND_DRIVER_POLL;
    const uint8_t due = chip_t0_read(s, 2);
    spend(s, 3);
    if (!due) {
      spend(s, 4 + 8);
      continue;
    }
    spend(s, 2 + 3);
    if (R(PAUSED)) {
      spend(s, 4 + 8);
      continue;
    }
    spend(s, 2 + 8);
    tick(s);
    if (s->state == SOUND_HALTED) return;
    spend(s, 8);
    vibrato(s);
    spend(s, 3 + 8);
  }
}
