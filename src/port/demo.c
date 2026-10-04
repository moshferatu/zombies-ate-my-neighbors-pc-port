// $80:9CB2  the demo's playback -- see port/demo.h.

#include "port/demo.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/rng.h"

// The pads as the NMI leaves them, and the first's direction.
#define W_PAD_1 0x006eu
#define W_PAD_2 0x0070u
#define W_PAD_1_DIRECTION 0x0072u

#define PAIR_BYTES 4

static uint32_t recording_at(const Wram* w) {
  return (((uint32_t)wram_r8(w, W_DEMO_RECORDING + 2) << 16) |
          wram_r16(w, W_DEMO_RECORDING)) +
         wram_r16(w, W_DEMO_AT);
}

static bool in_cartridge(uint32_t far, uint32_t bytes) {
  return far >= 0x800000u && far + bytes <= 0xa00000u &&
         (far & 0xffffu) >= 0x8000u && (far & 0xffffu) + bytes <= 0x10000u;
}

bool demo_job_supported(const Wram* w, uint16_t joy1, uint16_t joy2) {
  if ((joy1 | joy2) != 0) return true;
  if (wram_r16(w, W_DEMO_FRAMES_LEFT) != 0) return true;
  return in_cartridge(recording_at(w), PAIR_BYTES);
}

static void end_demo(Wram* w, DemoLog* log) {
  PORT_COVER(demo_job_ended);
  wram_w16(w, W_DEMO_HUD, 0);
  wram_w16(w, W_DEMO_HUD_2, 0);
  wram_w16(w, W_PAD_1, 0);
  wram_w16(w, W_PAD_2, 0);
  const uint16_t count = (uint16_t)(wram_r16(w, W_DEMO_ENDED) + 1);
  wram_w16(w, W_DEMO_ENDED, count);
  log->ended = true;
  log->n = (count & 0x8000u) != 0;
  log->z = count == 0;
}

static void read_pair(Wram* w, const Rom* rom, DemoLog* log) {
  PORT_COVER(demo_job_read_pair);
  const uint32_t at = recording_at(w);
  wram_w16(w, DEMO_DP_POINTER, wram_r16(w, W_DEMO_RECORDING));
  wram_w16(w, DEMO_DP_POINTER + 2, wram_r16(w, W_DEMO_RECORDING + 2));
  wram_w16(w, W_DEMO_PAD, rom_word(rom, at));
  wram_w16(w, W_DEMO_FRAMES_LEFT, rom_word(rom, at + 2));
  wram_w16(w, W_DEMO_AT, (uint16_t)(wram_r16(w, W_DEMO_AT) + PAIR_BYTES));
  log->read_pair = true;
}

void demo_job(Wram* w, const Rom* rom, uint16_t joy1, uint16_t joy2,
              uint16_t y, DemoLog* log) {
  DemoLog scratch;
  if (log == NULL) log = &scratch;
  *log = (DemoLog){0};
  log->y = y;

  const uint16_t frame = (uint16_t)(wram_r16(w, W_DEMO_FRAME) + 1);
  wram_w16(w, W_DEMO_FRAME, frame);
  wram_w16(w, W_RNG_STATE, frame);

  log->a = (uint16_t)(joy1 | joy2);
  if (log->a != 0) {
    end_demo(w, log);
    return;
  }

  if (wram_r16(w, W_DEMO_FRAMES_LEFT) == 0) {
    read_pair(w, rom, log);
  } else {
    PORT_COVER(demo_job_held);
  }
  const uint16_t pad = wram_r16(w, W_DEMO_PAD);
  const uint16_t dpad = (pad >> 8) & 0x000f;
  const uint16_t direction = rom_word(rom, DEMO_DIRECTIONS + dpad) & 0x00ff;
  wram_w16(w, W_PAD_1, pad);
  wram_w16(w, W_PAD_1_DIRECTION, direction);

  const uint16_t left = (uint16_t)(wram_r16(w, W_DEMO_FRAMES_LEFT) - 1);
  wram_w16(w, W_DEMO_FRAMES_LEFT, left);
  log->a = direction;
  log->y = dpad;
  log->n = (left & 0x8000u) != 0;
  log->z = left == 0;
}
