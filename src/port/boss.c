// $82:8F93 — see port/boss.h.

#include "port/boss.h"

#include "port/coverage.h"
#include "port/terrain.h"

// `LDA $9035,Y` and `LDA $905D,X` are absolute indexed with a data bank of
// `$82`, so the index wraps inside the bank rather than carrying out of it.
// `LDA $82906F,X` is long indexed and does not, which is why the delta table
// gets its address whole and these two do not.
static uint16_t table_word(const Rom* rom, uint16_t base, uint16_t index) {
  return rom_word(rom, BOSS_STEP_TABLE_BANK | (uint16_t)(base + index));
}

// One pass: two points off the leading edge, both through
// `terrain_blocked_wide`, and the axis committed only if both come back clear.
// `last` collects the registers of whichever probe ran most recently, because
// they are what the routine returns in X and Y.
static void boss_step_pass(Wram* w, const Rom* rom, uint16_t probe,
                           uint16_t try_x, uint16_t try_y, TerrainRegs* last) {
  terrain_blocked_wide(w, (uint16_t)(table_word(rom, BOSS_STEP_PROBES, probe) + try_x),
                       (uint16_t)(table_word(rom, BOSS_STEP_PROBES + 2u, probe) + try_y),
                       last);
  if (last->blocked) {
    PORT_COVER(boss_lead_blocked);
    return;
  }
  PORT_COVER(boss_lead_clear);

  terrain_blocked_wide(w, (uint16_t)(table_word(rom, BOSS_STEP_PROBES + 4u, probe) + try_x),
                       (uint16_t)(table_word(rom, BOSS_STEP_PROBES + 6u, probe) + try_y),
                       last);
  if (last->blocked) {
    PORT_COVER(boss_trail_blocked);
    return;
  }

  // `LDA $0A : AND #$0008` — the index that chose the probes chooses the axis.
  if (probe & BOSS_STEP_AXIS_X) {
    PORT_COVER(boss_commit_x);
    wram_w16(w, W_BOSS_X, try_x);
  } else {
    PORT_COVER(boss_commit_y);
    wram_w16(w, W_BOSS_Y, try_y);
  }
}

void boss_step(Wram* w, const Rom* rom, uint16_t dp, uint16_t a_in,
               BossStepRegs* out) {
  uint16_t dir = wram_r16(w, dp + BOSS_STEP_DP_DIR);

  // `LDX $16 : CMP #$6969` — X is loaded before the test, so the single-step
  // path is the direction untouched and the double-step path is the same
  // direction half a table further on.
  uint16_t at = dir;
  if (a_in == BOSS_STEP_FAST) {
    PORT_COVER(boss_step_double);
    at = (uint16_t)(at + BOSS_STEP_FAST_BIAS);
    wram_w16(w, dp + BOSS_STEP_DP_TICK,
             (uint16_t)(wram_r16(w, dp + BOSS_STEP_DP_TICK) - 1u));
  } else {
    PORT_COVER(boss_step_single);
  }

  uint16_t was_x = wram_r16(w, W_BOSS_X);
  uint16_t was_y = wram_r16(w, W_BOSS_Y);
  wram_w16(w, dp + BOSS_STEP_DP_WAS_X, was_x);
  wram_w16(w, dp + BOSS_STEP_DP_WAS_Y, was_y);

  uint16_t try_x = (uint16_t)(rom_word(rom, BOSS_STEP_DELTAS + at) + was_x);
  uint16_t try_y = (uint16_t)(rom_word(rom, BOSS_STEP_DELTAS + 2u + at) + was_y);
  wram_w16(w, dp + BOSS_STEP_DP_TRY_X, try_x);
  wram_w16(w, dp + BOSS_STEP_DP_TRY_Y, try_y);
  wram_w16(w, dp + BOSS_STEP_DP_FACING,
           rom_word(rom, BOSS_STEP_DELTAS + 4u + at));

  uint16_t pass = rom_word(rom, BOSS_STEP_DELTAS + 6u + at);
  wram_w16(w, dp + BOSS_STEP_DP_PASS, pass);
  PORT_COVER_IF(pass != 0, boss_diagonal, boss_cardinal);

  // `LDA $16 : LSR : LSR : TAX` — the direction, in eight-byte units, indexing
  // a table of words. Two shifts and not three, so the base moves one entry per
  // *pair* of directions: north and north-east share one, east and south-east
  // the next. A diagonal starts at its own leading edge and finishes on the
  // one before it.
  uint16_t base = table_word(rom, BOSS_STEP_BASES, (uint16_t)(dir >> 2));
  wram_w16(w, dp + BOSS_STEP_DP_PROBE_BASE, base);

  // A do-while: the counter is tested at the bottom, so a cardinal's `$0000`
  // still gets its one pass before `$0000 - 8` ends it.
  TerrainRegs last;
  do {
    uint16_t probe = (uint16_t)(base + pass);
    wram_w16(w, dp + BOSS_STEP_DP_PROBE, probe);
    boss_step_pass(w, rom, probe, try_x, try_y, &last);
    pass = (uint16_t)(pass - 8u);
    wram_w16(w, dp + BOSS_STEP_DP_PASS, pass);
  } while ((pass & 0x8000u) == 0);

  // `LDA $1E62 : CMP $10 : BNE` then the same for Y. `CMP` does not write A,
  // so what comes out is the coordinate that was loaded, not the difference —
  // and N and Z are the difference's, which is the pair the flags and the
  // accumulator disagree about.
  uint16_t now_x = wram_r16(w, W_BOSS_X);
  if (now_x != was_x) {
    PORT_COVER(boss_moved_x);
    out->a = now_x;
    out->n = ((uint16_t)(now_x - was_x) & 0x8000u) != 0;
    out->z = false;
    out->c = false;
  } else {
    uint16_t now_y = wram_r16(w, W_BOSS_Y);
    out->a = now_y;
    if (now_y != was_y) {
      PORT_COVER(boss_moved_y);
      out->n = ((uint16_t)(now_y - was_y) & 0x8000u) != 0;
      out->z = false;
      out->c = false;
    } else {
      PORT_COVER(boss_stuck);
      out->n = false;
      out->z = true;
      out->c = true;
    }
  }

  out->x = last.x;
  out->y = last.y;
}
