// The sound chip's mixer, the S-DSP: eight voices that play BRR samples out of
// the sound RAM, each with its own pitch, envelope and volume, summed with an
// echo that reads and writes a delay line in the same RAM. One call of
// `sound_dsp_sample` is one tick of its 32,000 Hz clock and makes one stereo
// sample.
//
// It is written to give the same samples as LakeSnes's `dsp.c` — the check is
// `tools/test_dsp.c`, which runs both on the same register writes and compares
// every sample, every register the driver can read back and the echo's writes
// to the RAM. So where the two could differ by a design choice, this one
// follows LakeSnes: the whole sample is computed at once, not one of 32 steps
// per tick, and key-on and key-off are taken on every other tick.
//
// What the chip is, register by register, is the specification everyone works
// from (Anomie's and nocash's notes); the names below are its names.
//
// Port code: libc only. The RAM is the caller's, because the sound driver
// owns it too.

#ifndef SOUND_DSP_H
#define SOUND_DSP_H

#include <stdbool.h>
#include <stdint.h>

// The 128 registers, by their usual names. A voice's are at `v * 16 + reg`.
enum {
  DSP_VOL_L = 0x00, DSP_VOL_R = 0x01,
  DSP_PITCH_L = 0x02, DSP_PITCH_H = 0x03,
  DSP_SRCN = 0x04,
  DSP_ADSR1 = 0x05, DSP_ADSR2 = 0x06, DSP_GAIN = 0x07,
  DSP_ENVX = 0x08, DSP_OUTX = 0x09,
  DSP_FIR = 0x0F,  // the echo filter's eight taps, one per voice's row
  DSP_MVOL_L = 0x0C, DSP_MVOL_R = 0x1C,
  DSP_EVOL_L = 0x2C, DSP_EVOL_R = 0x3C,
  DSP_KON = 0x4C, DSP_KOFF = 0x5C,
  DSP_FLG = 0x6C, DSP_ENDX = 0x7C,
  DSP_EFB = 0x0D, DSP_PMON = 0x2D, DSP_NON = 0x3D, DSP_EON = 0x4D,
  DSP_DIR = 0x5D, DSP_ESA = 0x6D, DSP_EDL = 0x7D,
};

// Where a voice's envelope is.
typedef enum {
  ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE,
} EnvPhase;

typedef struct {
  // --- set by the registers ---
  int8_t vol_l, vol_r;
  uint16_t pitch;       // 14 bits: $1000 plays the sample at 32 kHz
  uint8_t srcn;         // which entry of the sample directory
  bool pitch_mod;       // pitch bent by the voice below's output
  bool noise;           // plays the noise generator instead of its sample
  bool echo;            // also fed to the echo
  // ADSR1, ADSR2 and GAIN, kept as the rates the envelope steps at (an index
  // into `dsp_rates`), not as the bits they came in.
  bool use_gain;        // ADSR1 bit 7 clear: GAIN drives the envelope
  uint8_t attack_rate, decay_rate, sustain_rate, gain_rate;
  uint8_t sustain_level;       // ADSR2's top three bits
  uint8_t gain_sustain_level;  // GAIN's top three bits, used in their place
  bool direct_gain;            // GAIN bit 7 clear: the level is set outright
  uint8_t gain_mode;           // otherwise: 0 down, 1 down exp, 2 up, 3 bent up
  uint16_t gain_value;         // the level a direct GAIN sets, 0 to $7F0
  bool key_on, key_off;

  // --- running ---
  uint16_t pitch_counter;  // 4.12: which of the decoded samples, and how far
  int16_t decoded[12];     // three groups of four samples, a ring
  uint8_t decoded_at;      // where the next four go
  uint16_t block;          // the BRR block being played
  uint8_t block_byte;      // the next byte of it to decode, 1 to 7
  uint8_t header;          // the block's header byte
  uint8_t start_delay;     // the ticks left before a keyed-on voice sounds
  EnvPhase phase;
  uint16_t level;          // the envelope, 0 to $7FF
  uint16_t unclamped;      // the last level before it was clamped (bent GAIN)
  int16_t out;             // the last sample, before volume
} DspVoice;

typedef struct SoundDsp {
  uint8_t* ram;  // the 64 KB of sound RAM; the samples and the echo are there
  uint8_t reg[128];
  DspVoice voice[8];

  int8_t main_vol_l, main_vol_r;
  int8_t echo_vol_l, echo_vol_r;
  int8_t feedback;
  bool reset, mute, echo_writes;
  uint8_t noise_rate;
  uint16_t dir;          // the sample directory, a page of RAM
  uint16_t echo_start;   // the echo's delay line, a page of RAM
  uint16_t echo_delay;   // its length in bytes, from EDL
  uint16_t echo_length;  // the length in force, taken when the line wraps
  uint16_t echo_at;      // the byte offset into the line
  int8_t fir[8];
  int16_t fir_l[8], fir_r[8];  // the last eight echo samples read, a ring
  uint8_t fir_at;
  int16_t noise_value;
  uint16_t counter;      // the clock every rate is a fraction of
  bool even_tick;

  // The tick's result.
  int16_t out_l, out_r;    // to the speakers
  int16_t echo_l, echo_r;  // the voices fed to the echo, this tick

  // The last 1,024 samples, a ring, for `sound_dsp_get_samples`.
  int16_t ring[1024 * 2];
  uint16_t ring_at;

  // Voices left out of the mix and the echo, a bit each. They keep running.
  // Not the chip's: for the tests.
  uint8_t voice_mute;
} SoundDsp;

// Power on, with `ram` as the sound RAM.
void sound_dsp_reset(SoundDsp* d, uint8_t* ram);

// The driver's side: `$F3` with `$F2` pointing at `adr`.
void sound_dsp_write(SoundDsp* d, uint8_t adr, uint8_t val);
uint8_t sound_dsp_read(const SoundDsp* d, uint8_t adr);

// One tick: one stereo sample, left in `out_l` and `out_r` and in the ring.
void sound_dsp_sample(SoundDsp* d);

// The frame's audio: the last 534 samples (one NTSC frame's worth) stretched
// or squeezed to `count`, as LakeSnes's `dsp_getSamples` does it. `out` takes
// `count` pairs.
void sound_dsp_get_samples(const SoundDsp* d, int16_t* out, int count);

#endif
