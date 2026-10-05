// $80:E86D — see port/floor.h.

#include "port/floor.h"

#include "port/coverage.h"
#include "port/terrain.h"

static void set_nz(FloorRegs* out, uint16_t v) {
  out->n = (v & 0x8000u) != 0;
  out->z = v == 0;
}

// `CMP` against an immediate: N and Z from the difference, carry set when no
// borrow. It does **not** write A, which is the trap this project has now hit
// twice.
static bool from_cmp(FloorRegs* out, uint16_t a, uint16_t imm) {
  uint16_t r = (uint16_t)(a - imm);
  out->n = (r & 0x8000u) != 0;
  out->z = r == 0;
  out->c = a >= imm;
  return out->z;
}

// $80:F935, inlined — its only call site is `$80:E89F`, four instructions up.
// A on the way out is whatever the last load left, and `$80:E8A2` compares it,
// so this is not a void call however much it looks like one.
static void floor_harm(Wram* w, uint16_t dp, FloorRegs* out) {
  uint16_t mode = wram_r16(w, dp + FLOOR_DP_MODE);
  out->a = mode;
  set_nz(out, mode);
  if (from_cmp(out, mode, FLOOR_MODE_OFF_A)) {
    PORT_COVER(floor_mode_off);
    out->harm = FLOOR_HARM_OFF_A;
    return;
  }
  if (from_cmp(out, mode, FLOOR_MODE_OFF_B)) {
    PORT_COVER(floor_mode_off);
    out->harm = FLOOR_HARM_OFF_B;
    return;
  }

  // `LDA $52 : BPL` — carry survives the load, so it is still the second
  // `CMP`'s on every exit below.
  uint16_t cooldown = wram_r16(w, dp + FLOOR_DP_COOLDOWN);
  out->a = cooldown;
  set_nz(out, cooldown);
  if (!out->n) {
    PORT_COVER(floor_harm_cooling);
    out->harm = FLOOR_HARM_COOLING;
    return;
  }

  PORT_COVER(floor_harm_start);
  out->harm = FLOOR_HARM_START;
  out->a = FLOOR_STATE_START;
  set_nz(out, FLOOR_STATE_START);
  wram_w16(w, dp + FLOOR_DP_STATE, FLOOR_STATE_START);
  out->a = FLOOR_COOLDOWN_TICKS;
  set_nz(out, FLOOR_COOLDOWN_TICKS);
  wram_w16(w, dp + FLOOR_DP_COOLDOWN, FLOOR_COOLDOWN_TICKS);
}

// `DEC $32` and friends: the flags come from the value left in memory, not
// from A, and A is untouched — which matters, because the caller falls
// straight out to its `RTS` with A still holding the attribute word.
static void step_dp(Wram* w, uint16_t addr, int delta, FloorRegs* out) {
  uint16_t v = (uint16_t)(wram_r16(w, addr) + (uint16_t)delta);
  wram_w16(w, addr, v);
  set_nz(out, v);
}

void floor_effect(Wram* w, const Rom* rom, uint16_t dp, FloorRegs* out) {
  (void)rom;

  // $80:E86D-E871. `tile_attrs_at_pixel` puts X and Y back as they came in.
  uint16_t px = wram_r16(w, dp + FLOOR_DP_X);
  uint16_t py = wram_r16(w, dp + FLOOR_DP_Y);
  TileAttrsRegs tile;
  tile_attrs_at_pixel(w, px, py, &tile);
  out->x = px;
  out->y = py;
  out->c = tile.c;  // always clear; nothing below the `AND` changes it
  out->tile = FLOOR_TILE_PLAIN;
  out->gate = FLOOR_GATE_NONE;
  out->harm = FLOOR_HARM_NONE;
  out->belt = FLOOR_BELT_UNASKED;
  out->probes = 0;

  uint16_t attrs = (uint16_t)(tile.a & FLOOR_ATTR_MASK);
  out->a = attrs;
  set_nz(out, attrs);

  bool harm = false;   // reached $80:E89F
  bool belt = false;   // reached $80:E8A2, with A as whatever got it there

  if (from_cmp(out, attrs, FLOOR_ATTR_HARM_GATED)) {
    // $80:E891. The one case that looks at the player rather than the tile.
    out->tile = FLOOR_TILE_GATED;
    uint16_t player = wram_r16(w, dp + FLOOR_DP_PLAYER);
    out->x = player;  // `LDX $0E`, and X never comes back from here

    uint16_t weapon = wram_r16(w, (uint32_t)(W_PLAYER_WEAPON + player));
    out->a = weapon;
    set_nz(out, weapon);
    if (from_cmp(out, weapon, FLOOR_WEAPON_GATE)) {
      uint16_t guard = wram_r16(w, dp + FLOOR_DP_GUARD);
      out->a = guard;
      set_nz(out, guard);
      if (out->z) {
        PORT_COVER(floor_gate_open);
        out->gate = FLOOR_GATE_OPEN;
        harm = true;
      } else {
        PORT_COVER(floor_gate_shut);
        out->gate = FLOOR_GATE_SHUT;
      }
    } else {
      PORT_COVER(floor_gate_other_weapon);
      out->gate = FLOOR_GATE_OTHER_WEAPON;
      harm = true;
    }
  } else if (from_cmp(out, attrs, FLOOR_ATTR_HARM)) {
    PORT_COVER(floor_harm_plain);
    out->tile = FLOOR_TILE_HARM;
    harm = true;
  } else if (from_cmp(out, attrs, FLOOR_ATTR_CLEAR)) {
    // $80:E88D. `STZ` sets no flag, so the `CMP` above is the exit's.
    PORT_COVER(floor_clear_2a);
    out->tile = FLOOR_TILE_CLEAR;
    wram_w16(w, dp + FLOOR_DP_CLEARED, 0);
  } else {
    // $80:E887 `BIT #$0008` — immediate, so **Z only**. N and carry stay as
    // `CMP #$8000` left them.
    out->z = (attrs & FLOOR_ATTR_BELT) == 0;
    if (!out->z) {
      PORT_COVER(floor_belt);
      out->tile = FLOOR_TILE_BELT;
      belt = true;
    } else {
      PORT_COVER(floor_plain);
    }
  }

  if (harm) {
    floor_harm(w, dp, out);
    belt = true;  // $80:F935's `RTS` lands on `$80:E8A2`, not on an exit
  }
  if (!belt) return;

  // $80:E8A2. Four compares against whatever is in A — the attribute word on
  // the `BIT` path, and `$70`, `$52` or `#$0020` on the harm one.
  uint16_t v = out->a;
  if (from_cmp(out, v, FLOOR_ATTR_BELT_UP)) {
    PORT_COVER(floor_belt_up);
    out->belt = FLOOR_BELT_UP;
    step_dp(w, dp + FLOOR_DP_Y, -1, out);
  } else if (from_cmp(out, v, FLOOR_ATTR_BELT_DOWN)) {
    PORT_COVER(floor_belt_down);
    out->belt = FLOOR_BELT_DOWN;
    step_dp(w, dp + FLOOR_DP_Y, +1, out);
  } else if (from_cmp(out, v, FLOOR_ATTR_BELT_LEFT)) {
    PORT_COVER(floor_belt_left);
    out->belt = FLOOR_BELT_LEFT;
    step_dp(w, dp + FLOOR_DP_X, -1, out);
  } else if (from_cmp(out, v, FLOOR_ATTR_BELT_RIGHT)) {
    // $80:E8C2. The only direction that asks the terrain first.
    uint16_t tx = (uint16_t)(wram_r16(w, dp + FLOOR_DP_X) + 1u);
    uint16_t ty = wram_r16(w, dp + FLOOR_DP_Y);
    TerrainRegs t;
    terrain_blocked(w, tx, ty, &t);
    out->a = t.a;
    out->x = t.x;
    out->y = t.y;
    out->c = t.blocked;
    out->probes = t.probes;
    out->belt = t.blocked ? FLOOR_BELT_RIGHT_BLOCKED : FLOOR_BELT_RIGHT;
    // `$80:AE96` is `PLD : RTL`, so N and Z are this routine's own direct page
    // and not the answer — see port/terrain.h.
    set_nz(out, dp);
    if (t.blocked) {
      PORT_COVER(floor_belt_right_blocked);
    } else {
      PORT_COVER(floor_belt_right);
      step_dp(w, dp + FLOOR_DP_X, +1, out);
    }
  } else {
    PORT_COVER(floor_belt_none);
    out->belt = FLOOR_BELT_NOT;
  }
}
