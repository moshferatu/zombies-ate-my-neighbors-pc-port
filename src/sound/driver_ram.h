// Where the game's sound driver keeps its variables in the sound RAM, by
// address. `driver.c` keeps them there, as the original did, and the player
// (`player.c`) reads them to take the driver's place. Private to `src/sound`.

#ifndef SOUND_DRIVER_RAM_H
#define SOUND_DRIVER_RAM_H

// --- The driver's variables, by address ------------------------------------

// Four tracks ("slots"), each a byte of these at +slot, or a word at +slot*2.
enum {
  TRACK_PTR = 0x00,      // word: where the track is read from
  TRACK_LOOP = 0x08,     // word: where control $15 jumps back to
  TRACK_TEMPO = 0x10,    // 24 bits, a byte at +$00, +$04, +$08: added per beat
  TRACK_CLOCK = 0x1C,    // 24 bits likewise: counts down by $039378 a tick
  TRACK_WAIT = 0x28,     // 16 bits, at +$00 and +$04: beats to the next event
  TRACK_STATUS = 0x30,   // the running status, $80 to $F0
  TRACK_CHANNEL = 0x34,  // ...and its channel
  TRACK_VOLUME = 0x38,
  TRACK_SONG = 0x3C,     // which song the track plays
  TRACK_LOOPS = 0x40,    // control $14's count
  TRACK_SHIFTED = 0x44,  // started by command $11: notes moved by SHIFT
};

// Eight voices, a byte each at +voice.
enum {
  VOICE_ON = 0x48,
  VOICE_TRACK = 0x50,
  VOICE_CHANNEL = 0x58,
  VOICE_NOTE = 0x60,       // the note the track asked for
  VOICE_KEY = 0x68,        // ...after the instrument's transposition
  VOICE_PITCH_LO = 0x70,   // the pitch it was keyed at
  VOICE_PITCH_HI = 0x78,
  VOICE_VELOCITY = 0x80,
  VOICE_VIB_DEPTH = 0x88,
  VOICE_VIB_RANGE = 0x90,  // in semitones
  VOICE_VIB_RATE = 0x98,
  VOICE_VIB_TIME = 0xA0,   // overflows at the rate's pace
  VOICE_VIB_STEP = 0xA8,   // 0 to $3F through the wave
  VOICE_VIB_WAVE = 0xB0,   // which of four waves: 0, $40, $80, $C0
  VOICE_PRIORITY = 0xB8,
};

// Single bytes.
enum {
  T0 = 0xC0, T1 = 0xC1, T2 = 0xC2, T3 = 0xC3,  // scratch
  UPLOAD_LO = 0xC4, UPLOAD_HI = 0xC5,          // where uploaded bytes go
  CMD = 0xC6, PARAM = 0xC7, PARAM0 = 0xC8,     // the command, from ports 2, 1, 0
  SEQ = 0xC9,                                  // ...and its count, from port 3
  SONGS = 0xCA,                                // songs registered
  EV1 = 0xCB, EV2 = 0xCC,                      // the event's two data bytes
  CUR_VOICE = 0xCD, CUR_VOICE_REG = 0xCE, CUR_VOICE_BIT = 0xCF,
  NOTE_ECHO = 0xD0, NOTE_NOISE = 0xD1,         // the bits to key on with
  CHAN = 0xD2,                                 // track * 16 + channel
  EON = 0xD3, NON = 0xD4, FLG = 0xD5,          // what the DSP was last given
  CUR_TRACK = 0xD6,
  MUL_LO = 0xD7, MUL_HI = 0xD8,                // the 16-bit factor
  PROD_0 = 0xD9, PROD_1 = 0xDA, PROD_2 = 0xDB, // the product, 24 bits
  PROD_SIGN = 0xDC,
  PAUSED = 0xDD,
  SAVED_SONGS = 0xDE, SAVED_LO = 0xDF, SAVED_HI = 0xE0,  // command $0D
  VOICE_ORDER = 0xE1,                          // eight voices, least recent first
  ECHO_DELAY = 0xE9, ECHO_E9 = 0xEA, ECHO_FLG = 0xEB,
};

// Sixteen channels to a track, a byte each at +CHAN. Set by the controllers.
enum {
  CH_PROGRAM = 0x0200,
  CH_PAN = 0x0240,
  CH_VOLUME = 0x0280,
  CH_BEND = 0x02C0,       // the pitch wheel's top byte, signed
  CH_BEND_RANGE = 0x0300, // semitones
  CH_FLAGS = 0x0340,      // bit 0 echo, bit 1 noise
  CH_TUNE = 0x0380,       // $40 is in tune
  CH_VIB_DEPTH = 0x03C0,
  CH_VIB_RANGE = 0x0400,
  CH_VIB_RATE = 0x0440,
  CH_VIB_WAVE = 0x0480,
  CH_PRIORITY = 0x04C0,
  CH_MUTE = 0x0500,       // bit 6: no new notes
};

// Elsewhere.
enum {
  SONG_LO = 0x0540, SONG_HI = 0x0573,  // registered songs' starts
  MONO = 0x05A6,
  MIDI_COUNT = 0x05A7, MIDI_BYTES = 0x05A8,  // command $03
  SHIFT = 0x05B0,                            // command $12
  // The instruments, uploaded with a sample set: for up to 32, the highest
  // note each covers and the program it belongs to, then its sample, ADSR
  // and transposition.
  INS_TOP = 0x1340, INS_PROGRAM = 0x1360, INS_ADSR1 = 0x1380, INS_ADSR2 = 0x13A0,
  INS_SHIFT = 0x13C0, INS_SAMPLE = 0x13E0,
  DIRECTORY = 0x1400,
  // The driver's own tables, uploaded with it.
  ECHO_PAGES = 0x08E9, ECHO_CLEARS = 0x0FA3, VIB_WAVES = 0x0FAE, VOICE_BITS = 0x10AE,
  PITCHES = 0x10B6, DSP_INIT = 0x118E,
};

#endif
