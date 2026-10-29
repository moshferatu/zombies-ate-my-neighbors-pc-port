#include "sound/sound.h"

#include <string.h>

#include "sound/chip.h"

// ---------------------------------------------------------------------------
// The chip
// ---------------------------------------------------------------------------

void chip_sync(Sound* s, uint64_t c) {
  if (s->dsp_idle) return;
  while (s->dsp_at <= c) {
    sound_dsp_sample(&s->dsp);
    const size_t k = (size_t)(s->ticks & 16383);
    s->ring[k * 2] = s->dsp.out_l;
    s->ring[k * 2 + 1] = s->dsp.out_r;
    s->ticks++;
    s->dsp_at += 32;
  }
}

void chip_halt(Sound* s, const char* why) {
  s->state = SOUND_HALTED;
  if (!s->fault) s->fault = why;
}

// One port's log: the last entry at or before `key`, and those after it.
static void log_insert(Sound* s, SoundPortLog* log, uint64_t key, uint8_t val) {
  if (log->count == SOUND_PORT_LOG) {
    chip_halt(s, "the game wrote a port more often than the driver read it");
    return;
  }
  int i = log->count;
  while (i > 0 && log->key[i - 1] > key) {
    log->key[i] = log->key[i - 1];
    log->val[i] = log->val[i - 1];
    i--;
  }
  log->key[i] = key;
  log->val[i] = val;
  log->count++;
}

bool chip_port_in(Sound* s, int port, uint8_t* val) {
  if (s->now >= s->host) return false;
  SoundPortLog* log = &s->in[port];
  const uint64_t key = s->now * 2;
  int last = -1;
  while (last + 1 < log->count && log->key[last + 1] <= key) last++;
  *val = last >= 0 ? log->val[last] : 0;
  // Reads only go forward, so nothing before the entry just read is wanted.
  if (last > 0) {
    memmove(log->key, log->key + last, (size_t)(log->count - last) * sizeof *log->key);
    memmove(log->val, log->val + last, (size_t)(log->count - last));
    log->count -= last;
  }
  return true;
}

void chip_port_out(Sound* s, int port, uint8_t val) {
  SoundPortQueue* q = &s->out[port];
  if (q->count == SOUND_PORT_QUEUE) {
    chip_halt(s, "the driver wrote a port too far ahead of the game");
    return;
  }
  const int k = (q->head + q->count) % SOUND_PORT_QUEUE;
  q->at[k] = s->now;
  q->val[k] = val;
  q->count++;
  if (s->watch) s->watch(s->watch_ctx, SOUND_SEEN_PORT, (uint16_t)port, val, s->now);
}

// What the game sees of the driver's port writes: those made by instructions
// that began before `host`.
static void show_ports(Sound* s) {
  for (int p = 0; p < 4; p++) {
    SoundPortQueue* q = &s->out[p];
    while (q->count && q->at[q->head] < s->host) {
      q->shown = q->val[q->head];
      q->head = (q->head + 1) % SOUND_PORT_QUEUE;
      q->count--;
    }
  }
}

void chip_dsp_write(Sound* s, int at, uint8_t reg, uint8_t val) {
  chip_sync(s, s->now + (uint64_t)at);
  if (reg < 0x80) sound_dsp_write(&s->dsp, reg, val);
  if (s->watch) s->watch(s->watch_ctx, SOUND_SEEN_DSP, reg, val, s->now + (uint64_t)at);
}

uint8_t chip_dsp_read(Sound* s, int at, uint8_t reg) {
  chip_sync(s, s->now + (uint64_t)at);
  return sound_dsp_read(&s->dsp, reg & 0x7F);
}

void chip_ram_put(Sound* s, int at, uint16_t adr, uint8_t val) {
  chip_sync(s, s->now + (uint64_t)at);
  s->ram[adr] = val;
}

// Timer 0's ticks up to and including cycle `c`: one at every multiple of 128.
static void t0_advance(Sound* s, uint64_t c) {
  if (c <= s->t0_seen) return;
  const uint64_t ticks = c / 128 - s->t0_seen / 128;
  s->t0_seen = c;
  if (!s->t0_on) return;
  const uint64_t period = s->t0_target ? s->t0_target : 256;
  const uint64_t total = s->t0_divider + ticks;
  s->t0_count = (uint8_t)((s->t0_count + total / period) & 0x0F);
  s->t0_divider = (uint8_t)(total % period);
}

void chip_control(Sound* s, int at, uint8_t val) {
  t0_advance(s, s->now + (uint64_t)at);
  if (!s->t0_on && (val & 1)) s->t0_divider = s->t0_count = 0;
  s->t0_on = val & 1;
  if (val & 0x06) chip_halt(s, "timers 1 and 2 are not the driver's");
  const uint64_t key = s->now * 2 + 1;
  if (val & 0x10) {
    log_insert(s, &s->in[0], key, 0);
    log_insert(s, &s->in[1], key, 0);
  }
  if (val & 0x20) {
    log_insert(s, &s->in[2], key, 0);
    log_insert(s, &s->in[3], key, 0);
  }
}

void chip_t0_target(Sound* s, uint8_t val) { s->t0_target = val; }

uint8_t chip_t0_read(Sound* s, int at) {
  t0_advance(s, s->now + (uint64_t)at);
  const uint8_t v = s->t0_count;
  s->t0_count = 0;
  return v;
}

// ---------------------------------------------------------------------------
// The loader: the console's 64-byte boot ROM at $FFC0
// ---------------------------------------------------------------------------
//
// It clears the direct page, knocks ($AA, $BB on ports 0 and 1), and waits
// for the game to answer $CC. Then blocks: the game puts an address on ports 2
// and 3 and a byte on port 1, with its index on port 0, and the loader echoes
// the index once the byte is stored. An index that jumps ahead ends the block;
// with port 1 zero, the address on ports 2 and 3 is run instead.

static uint8_t ipl_compare(uint8_t a, uint8_t b) { return (uint8_t)(a - b); }

// Each case is one stretch between reads of a port. False when it stops.
static bool ipl_step(Sound* s) {
  uint8_t v, hi;
  switch (s->state) {
    case SOUND_IPL_START:
      chip_at(s, 0xFFC0);
      spend(s, 2 + 2 + 2);  // MOV X, #$EF : MOV SP, X : MOV A, #$00
      for (int x = 0xEF; x != 0; x--) {
        chip_ram_put(s, 3, (uint16_t)x, 0);  // MOV (X), A
        spend(s, 4 + 2);                     // ...: DEC X
        spend(s, x - 1 ? 4 : 2);             // BNE
      }
      chip_port_out(s, 0, 0xAA);  // MOV $F4, #$AA
      spend(s, 5);
      chip_port_out(s, 1, 0xBB);  // MOV $F5, #$BB
      spend(s, 5);
      s->state = SOUND_IPL_WAIT_CC;
      return true;

    case SOUND_IPL_WAIT_CC:
      if (!chip_port_in(s, 0, &v)) return false;  // CMP $F4, #$CC
      spend(s, 5);
      if (v != 0xCC) {
        spend(s, 4);  // BNE, back
        return true;
      }
      spend(s, 2 + 4);  // BNE : BRA $FFEF
      s->state = SOUND_IPL_ADDR;
      return true;

    case SOUND_IPL_ADDR:
      if (!chip_port_in(s, 2, &v) || !chip_port_in(s, 3, &hi)) return false;
      chip_at(s, 0xFFEF);
      spend(s, 5);  // MOVW YA, $F6
      chip_ram_put(s, 3, 0x00, v);  // MOVW $00, YA
      chip_ram_put(s, 4, 0x01, hi);
      spend(s, 5);
      s->state = SOUND_IPL_ADDR_INDEX;
      return true;

    case SOUND_IPL_ADDR_INDEX:
      if (!chip_port_in(s, 0, &s->a) || !chip_port_in(s, 1, &s->y)) return false;
      spend(s, 5);                 // MOVW YA, $F4
      chip_port_out(s, 0, s->a);  // MOV $F4, A
      spend(s, 4 + 2 + 2);        // ...: MOV A, Y : MOV X, A
      if (s->y) {
        spend(s, 4);  // BNE $FFD6: a block
        s->state = SOUND_IPL_WAIT_ZERO;
        return true;
      }
      spend(s, 2 + 6);  // BNE : JMP [$0000+X]
      if ((s->ram[0] | s->ram[1] << 8) != 0x0600) {
        chip_halt(s, "the loader was told to run something other than the driver");
        return false;
      }
      s->state = SOUND_DRIVER_START;
      return true;

    case SOUND_IPL_WAIT_ZERO:
      if (!chip_port_in(s, 0, &s->y)) return false;  // MOV Y, $F4
      spend(s, 3);
      if (s->y) {
        spend(s, 4);  // BNE, back
        return true;
      }
      spend(s, 2);
      s->state = SOUND_IPL_BYTE;
      return true;

    case SOUND_IPL_BYTE:
      if (!chip_port_in(s, 0, &v)) return false;  // CMP Y, $F4
      spend(s, 3);
      if (v == s->y) {
        spend(s, 2);  // BNE
        s->state = SOUND_IPL_DATA;
        return true;
      }
      // BNE $FFE9 : BPL $FFDA. Still the last index: the game has not sent
      // the next byte yet. Past it: perhaps the end of the block.
      spend(s, 4);
      if (!(ipl_compare(s->y, v) & 0x80)) {
        spend(s, 4);
        return true;
      }
      spend(s, 2);
      s->state = SOUND_IPL_RECHECK;
      return true;

    case SOUND_IPL_DATA:
      if (!chip_port_in(s, 1, &v)) return false;  // MOV A, $F5
      spend(s, 3);
      chip_port_out(s, 0, s->y);  // MOV $F4, Y
      spend(s, 4);
      chip_ram_put(s, 6, (uint16_t)((s->ram[0] | s->ram[1] << 8) + s->y), v);  // MOV [$00]+Y, A
      spend(s, 7);
      s->y++;  // INC Y
      spend(s, 2);
      if (s->y) {
        spend(s, 4);  // BNE $FFDA
      } else {
        spend(s, 2);
        chip_ram_put(s, 3, 0x01, (uint8_t)(s->ram[1] + 1));  // INC $01
        spend(s, 4);
        if (!(s->ram[1] & 0x80)) {
          spend(s, 4);  // BPL $FFDA
        } else {
          spend(s, 2);
          s->state = SOUND_IPL_RECHECK;
          return true;
        }
      }
      s->state = SOUND_IPL_BYTE;
      return true;

    case SOUND_IPL_RECHECK:
      if (!chip_port_in(s, 0, &v)) return false;  // CMP Y, $F4
      spend(s, 3);
      if (!(ipl_compare(s->y, v) & 0x80)) {
        spend(s, 4);  // BPL $FFDA
        s->state = SOUND_IPL_BYTE;
        return true;
      }
      spend(s, 2);
      s->state = SOUND_IPL_ADDR;
      return true;

    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// The game's side
// ---------------------------------------------------------------------------

void sound_reset(Sound* s) {
  const SoundProgram program = s->program;
  const SoundWatchFn watch = s->watch;
  void* ctx = s->watch_ctx;
  memset(s, 0, sizeof *s);
  s->program = program;
  s->watch = watch;
  s->watch_ctx = ctx;
  sound_dsp_reset(&s->dsp, s->ram);
  s->state = SOUND_IPL_START;
  // The SPC700 comes out of reset as from a BRK that writes nothing: five
  // reads, an idle, and the vector at $FFFE.
  s->now = 8;
}

void sound_run(Sound* s, uint64_t t) {
  if (t < s->host) return;
  s->host = t;
  for (;;) {
    if (s->state == SOUND_HALTED) break;
    if (s->state < SOUND_DRIVER_START) {
      if (!ipl_step(s)) break;
    } else if (s->program.run) {
      s->program.run(s->program.ctx, s);  // until it stops at a read, or halts
      break;
    } else {
      chip_halt(s, "the loader started a program, and there is none");
      break;
    }
  }
  // The ticks before `t`. The program is stopped at a read at `t` or later,
  // or has halted, so it will not touch anything they read before then.
  if (t) chip_sync(s, t - 1);
  show_ports(s);
}

uint8_t sound_read_port(Sound* s, int port) {
  show_ports(s);
  return s->out[port & 3].shown;
}

void sound_write_port(Sound* s, int port, uint8_t val) {
  log_insert(s, &s->in[port & 3], s->host * 2, val);
  s->written[port & 3] = val;
}

void sound_copy(Sound* dst, const Sound* src) {
  if (dst == src) return;
  memcpy(dst, src, sizeof *dst);
  dst->dsp.ram = dst->ram;
  dst->watch = NULL;
  dst->watch_ctx = NULL;
}

void sound_get_samples(const Sound* s, int16_t* out, int count) {
  if (s->dsp_idle && s->program.samples) {
    s->program.samples(s->program.ctx, out, count);
    return;
  }
  sound_ring_samples(s->ring, s->ticks, s->host, out, count);
}

void sound_ring_samples(const int16_t* ring, uint64_t ticks, uint64_t host, int16_t* out,
                        int count) {
  // LakeSnes's arithmetic to the bit: its sample counter is 16 bits wide and
  // its ring 1,024 samples, and the float is taken of that.
  const uint64_t made = sound_ticks_before(host);
  const uint16_t made16 = (uint16_t)made;
  const float per_frame = 534.0f;
  const double step = per_frame / count;
  double at = made16 - per_frame;
  for (int i = 0; i < count; i++) {
    const int j = (int)at & 0x3FF;
    // The tick LakeSnes's ring holds at `j`: the last one made with that index.
    const int64_t k = (int64_t)made - 1 - (int64_t)((made - 1 - (uint64_t)j) & 0x3FF);
    if (made == 0 || k < 0 || (uint64_t)k >= ticks) {
      out[i * 2] = out[i * 2 + 1] = 0;
    } else {
      out[i * 2] = ring[(k & 16383) * 2];
      out[i * 2 + 1] = ring[(k & 16383) * 2 + 1];
    }
    at += step;
  }
}
