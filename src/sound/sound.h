// The sound chip as the game sees it, with nothing emulated: the console's
// boot loader is C (`sound.c`), and so is what it starts at `$0600`. That is
// a program of the caller's choosing (`SoundProgram`): the player of our own
// (`player.h`), which is what the game has, or the game's driver as it was
// (`driver.c`, for the tests). Either drives the port's DSP (`dsp.c`).
//
// ## Time
//
// The game waits on the sound chip. It sends a command and spins until the
// driver has answered, and a level's songs and samples go over one byte, one
// answer, at a time. So when the driver answers moves everything the game does
// after it, and a movie only stays in step with the console if the answers come
// on the same cycles. The C therefore counts the cycles of the instructions it
// stands for, every path of them, and the clock below is the SPC700's: 1.024 MHz,
// 32 cycles to a sample.
//
// The driver never waits for the game except at its ports, so it runs ahead of
// the console and stops only where it reads one: there it needs to know what the
// game had written by that cycle, and the game may not have got there yet.
// What it writes to the ports is kept with its cycle and shown to the game once
// the game's clock passes it. So the CPU's side drives it like this:
//
//   sound_run(s, t);            // everything the chip does before cycle t
//   v = sound_read_port(s, p);  // what the game reads at t
//   sound_write_port(s, p, v);  // the game writes at t
//
// with `t` never going back. An instruction that starts before `t` has been
// done, and one that starts at `t` or after has not: the same line LakeSnes
// draws when it catches its SPC700 up an instruction at a time.
//
// The DSP keeps step with the driver, which can be ahead of the game: what the
// game has been given of the sound is only what was made before `t`.
//
// Port code: libc only.

#ifndef SOUND_SOUND_H
#define SOUND_SOUND_H

#include <stdbool.h>
#include <stdint.h>

#include "sound/dsp.h"

// What the game has written to one port, by cycle. A read at cycle `c` sees
// the last entry whose key is at most `2c`: the game's writes have key `2t`,
// and the driver clearing the port (CONTROL bits 4 and 5) has `2c + 1`, so a
// write the game made at the cycle the clearing instruction began comes first.
#define SOUND_PORT_LOG 64
typedef struct {
  uint64_t key[SOUND_PORT_LOG];
  uint8_t val[SOUND_PORT_LOG];
  int count;
} SoundPortLog;

// What the driver has written to one port and not yet shown, by the cycle its
// instruction began.
#define SOUND_PORT_QUEUE 64
typedef struct {
  uint64_t at[SOUND_PORT_QUEUE];
  uint8_t val[SOUND_PORT_QUEUE];
  int head, count;
  uint8_t shown;  // what the game sees
} SoundPortQueue;

// Where the program is. The loader and the driver each stop only at a read of
// a port, so these name those reads; everything between them runs whole.
typedef enum {
  SOUND_IPL_START,       // $FFC0: power on
  SOUND_IPL_WAIT_CC,     // $FFCF: CMP $F4, #$CC -- the game's first knock
  SOUND_IPL_ADDR,        // $FFEF: MOVW YA, $F6 -- a block's address
  SOUND_IPL_ADDR_INDEX,  // $FFF3: MOVW YA, $F4 -- ...and whether to run it
  SOUND_IPL_WAIT_ZERO,   // $FFD6: MOV Y, $F4 -- the first byte's index
  SOUND_IPL_BYTE,        // $FFDA: CMP Y, $F4 -- the next byte's index
  SOUND_IPL_DATA,        // $FFDE: MOV A, $F5 -- the byte
  SOUND_IPL_RECHECK,     // $FFEB: CMP Y, $F4 -- the end of a block?
  SOUND_DRIVER_START,    // $0600, jumped to by the loader
  SOUND_DRIVER_POLL,     // $0AFF: the first of the driver's reads
  SOUND_DRIVER_POLL_2,   // $0B05
  SOUND_DRIVER_POLL_3,   // $0B0B
  SOUND_DRIVER_POLL_4,   // $0B11
  SOUND_DRIVER_POLL_5,   // $0B17
  SOUND_HALTED,          // somewhere the C does not go; see `fault`
} SoundState;

// What the test build and the frontend are told, as it happens.
typedef enum {
  SOUND_SEEN_AT,    // the program is at ROM address `a`, an instruction starting at
                    // `cycle`; `v` is a register there, where `driver.c` says which
  SOUND_SEEN_DSP,   // DSP register `a` written with `v` on `cycle`
  SOUND_SEEN_PORT,  // port `a` written with `v` by the instruction starting at `cycle`
} SoundSeen;
typedef void (*SoundWatchFn)(void* ctx, SoundSeen what, uint16_t a, uint8_t v, uint64_t cycle);

struct Sound;

// What runs from `$0600`, where the loader jumps. `run` goes on from where the
// program stopped until it stops again at a read of a port, as `driver_run`
// does (see `chip.h`): `s->state` says where, from `SOUND_DRIVER_START` on.
// A program with DSPs of its own sets `dsp_idle` on the chip as it starts, and
// then `samples` is what is heard.
typedef struct {
  void (*run)(void* ctx, struct Sound* s);
  void (*samples)(const void* ctx, int16_t* out, int count);
  void* ctx;
} SoundProgram;

typedef struct Sound {
  // The 64 KB of sound RAM, the driver's variables too. A program with DSPs of
  // its own takes it as it starts, and keeps its own from then on.
  uint8_t ram[0x10000];
  SoundDsp dsp;

  uint64_t now;   // the program's clock: where its next instruction begins
  uint64_t host;  // the last `sound_run`'s `t`
  uint64_t dsp_at;  // the next DSP tick: a multiple of 32

  // Timer 0, the only one the driver starts. Ticks every 128 cycles, counts
  // up one when it has ticked `target` times.
  bool t0_on;
  uint8_t t0_target, t0_divider, t0_count;
  uint64_t t0_seen;  // the last cycle it has been brought up to

  SoundPortLog in[4];
  SoundPortQueue out[4];
  uint8_t written[4];  // the last the game wrote to each port

  SoundState state;
  uint8_t a, x, y;     // what the program carries from one stop to the next
  const char* fault;   // why it is SOUND_HALTED

  // What the game has been given, as of `host`: the samples made before it.
  int16_t ring[16384 * 2];
  uint64_t ticks;  // DSP ticks so far, all of them
  bool dsp_idle;   // the program has DSPs of its own, and this one stopped

  SoundProgram program;
  SoundWatchFn watch;
  void* watch_ctx;
} Sound;

// Power on. The loader starts at cycle 0. The program and the watcher are
// kept: they are the caller's wiring, not the machine's.
void sound_reset(Sound* s);

// The game's driver as it was, routine by routine (`driver.c`), as `s`'s
// program. Only the tests have it: it is what they check the player against.
void sound_use_driver(Sound* s);

// `dst` becomes a copy of `src`, from here on a machine of its own. Nothing
// watches it.
void sound_copy(Sound* dst, const Sound* src);

// The driver is running: started, and stopped at its poll.
static inline bool sound_driver_running(const Sound* s) {
  return s->state >= SOUND_DRIVER_POLL && s->state <= SOUND_DRIVER_POLL_5;
}

// Everything the chip does before cycle `t`.
void sound_run(Sound* s, uint64_t t);

// The game's side of a port, at the last `sound_run`'s `t`.
uint8_t sound_read_port(Sound* s, int port);
void sound_write_port(Sound* s, int port, uint8_t val);

// The last 534 samples made before the last `sound_run`'s `t` (one NTSC frame),
// stretched or squeezed to `count` pairs, as LakeSnes's `dsp_getSamples` does.
// The program's, once it has DSPs of its own.
void sound_get_samples(const Sound* s, int16_t* out, int count);

// ...the same, from a ring of 16,384 pairs, `ticks` of them made, as of `host`.
void sound_ring_samples(const int16_t* ring, uint64_t ticks, uint64_t host, int16_t* out,
                        int count);

// DSP ticks made before cycle `t`: the one at cycle 32k is made before
// anything else the chip does in that cycle.
static inline uint64_t sound_ticks_before(uint64_t t) { return (t + 31) / 32; }

#endif
