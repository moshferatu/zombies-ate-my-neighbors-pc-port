#include "port/bodies.h"

#include "port/coverage.h"

// ---------------------------------------------------------------------------
// The addressing modes these bodies use
// ---------------------------------------------------------------------------

// A thread's direct page is its own 128-byte page in bank 0, so `$0C` is
// `D + $0C` in WRAM.
static uint16_t dp_r16(const Wram* w, const PortCpu* c, uint8_t off) {
  return wram_r16(w, (uint16_t)(c->d + off));
}

static void dp_w16(Wram* w, const PortCpu* c, uint8_t off, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + off), v);
}

// A word of data from outside the thread's page, counted for the price. The
// runs are priced as if every such byte were fast ROM, which the level data
// is; `BodyWork` says how many were and were not.
static uint16_t data_r16(const Wram* w, const Rom* rom, uint32_t at,
                         BodyWork* k) {
  for (uint32_t i = 0; i < 2; i++) {
    if (bus_fast((at + i) & 0xffffffu))
      k->fast_data++;
    else
      k->slow_data++;
  }
  return bus_r16(w, rom, at);
}

// `LDA ($0C),Y`: the pointer on the direct page, the bank the data bank's, and
// a carry out of the offset goes into the bank. The level lists are in `$9F`.
static uint16_t ind_y(const Wram* w, const Rom* rom, const PortCpu* c,
                      uint8_t off, BodyWork* k) {
  return data_r16(
      w, rom, (((uint32_t)c->db << 16) + dp_r16(w, c, off) + c->y) & 0xffffffu,
      k);
}

// `LDA [$00],Y`: the same with the bank on the page too.
static uint16_t ind_long_y(const Wram* w, const Rom* rom, const PortCpu* c,
                           uint8_t off, BodyWork* k) {
  const uint32_t ptr =
      dp_r16(w, c, off) |
      (uint32_t)wram_r8(w, (uint16_t)(c->d + off + 2)) << 16;
  return data_r16(w, rom, (ptr + c->y) & 0xffffffu, k);
}

// `$38,X` and the like: the thread's page, indexed, still in bank 0.
static uint16_t dpx(const PortCpu* c, uint8_t off) {
  return (uint16_t)(c->d + off + c->x);
}

// `LDA #imm` and the loads that only set N and Z.
static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

// `EOR #$FFFF : INC`, the absolute value every one of these distance tests
// takes, and only when `BPL` falls through.
static void negate(PortCpu* c) {
  lda(c, (uint16_t)(c->a ^ 0xffffu));
  lda(c, (uint16_t)(c->a + 1));
}

// `SEP #$20 : STA long,X : REP #$xx` -- one byte out of the accumulator.
static void sta8_long_x(Wram* w, PortCpu* c, uint32_t base, uint8_t rep) {
  c->p |= PORT_P_M;
  wram_w8(w, base + c->x, (uint8_t)c->a);
  c->p = (uint8_t)(c->p & ~rep);
}

// ---------------------------------------------------------------------------
// $81:81F6  victims_body
// ---------------------------------------------------------------------------

// `$81:81FF  LDA #$0003`, and the `JSL thread_yield` after it.
static void victims_yield(PortCpu* c, BodyWork* k) {
  lda(c, 0x0003);
  k->blocks[VICTIMS_TICKS]++;
  c->pc = VICTIMS_YIELD_PC;
}

void victims_start(Wram* w, PortCpu* c, BodyWork* k) {
  // LDA $00 : STA $0C
  lda(c, dp_r16(w, c, 0x00));
  dp_w16(w, c, 0x0c, c->a);
  // PEI $02 : PLB -- the list's bank. The high byte of `$02` stays on the
  // stack for good: nothing pulls it, and the body never returns.
  push16(w, c, dp_r16(w, c, 0x02));
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  k->blocks[VICTIMS_START]++;
  // STZ $10
  dp_w16(w, c, 0x10, 0);
  k->blocks[VICTIMS_STZ]++;
  victims_yield(c, k);
}

// `$81:8263` and `$81:828F`: `INC $10`, and back to the yield.
static void victims_next_frame(Wram* w, PortCpu* c, BodyWork* k, int block) {
  const uint16_t v = (uint16_t)(dp_r16(w, c, 0x10) + 1);
  dp_w16(w, c, 0x10, v);
  set_nz16(c, v);
  k->blocks[block]++;
  victims_yield(c, k);
}

void victims_started(Wram* w, PortCpu* c, BodyWork* k) {
  victims_next_frame(w, c, k, VICTIMS_NEXT);
}

void victims_stopped(Wram* w, PortCpu* c, BodyWork* k) {
  victims_next_frame(w, c, k, VICTIMS_NEXT_JMP);
}

// The window's four immediates, read from the cartridge rather than written
// here. Widescreen rewrites the X pair in the ROM every frame so that the
// window follows the picture (`ws_widen_window` in `widescreen.h`), and a port
// that kept the stock `$0080` and `$00A0` would start neighbours only once they
// were inside the margin, where they can be seen appearing.
#define VICTIMS_MIDDLE_X_AT 0x81820bu  // `ADC #$0080` at `$81:820A`
#define VICTIMS_MIDDLE_Y_AT 0x818214u  // `ADC #$0070` at `$81:8213`
#define VICTIMS_REACH_X_AT 0x81823du   // `CMP #$00A0` at `$81:823C`
#define VICTIMS_REACH_Y_AT 0x818251u   // `CMP #$00A0` at `$81:8250`

// `$81:8231`/`$81:8241`: one axis of the distance test, from `SBC` to `BCS`.
// True when the entry is out of range on it.
static bool victims_far(PortCpu* c, uint8_t middle, uint16_t reach,
                        const Wram* w, BodyWork* k) {
  set_c(c, true);
  c->a = sbc16(c, c->a, dp_r16(w, c, middle));
  if (c->a & 0x8000u) {
    negate(c);
    k->blocks[VICTIMS_NEG]++;
  } else {
    k->blocks[VICTIMS_TAKEN]++;
  }
  cmp16(c, c->a, reach);
  k->blocks[VICTIMS_CMP]++;
  if (flag(c, PORT_P_C)) k->blocks[VICTIMS_TAKEN]++;
  return flag(c, PORT_P_C);
}

// `LDX $10 : LDA $7E605A,X : AND #mask`, with Z left for the branch.
static uint16_t victims_state(const Wram* w, PortCpu* c, uint16_t mask,
                             BodyWork* k) {
  c->x = dp_r16(w, c, 0x10);
  lda(c, (uint16_t)(wram_r16(w, W_VICTIM_STATE + c->x) & mask));
  k->blocks[VICTIMS_FLAG]++;
  return c->a;
}

void victims_resume(Wram* w, const Rom* rom, PortCpu* c, BodyWork* k) {
  // $8206: the screen's middle. `LDA $1B6A` is through the data bank, `$9F`,
  // which shows low WRAM.
  const uint16_t reach_x = rom_word(rom, VICTIMS_REACH_X_AT);
  const uint16_t reach_y = rom_word(rom, VICTIMS_REACH_Y_AT);
  set_c(c, false);
  c->a = adc16(c, wram_r16(w, W_CAMERA_X), rom_word(rom, VICTIMS_MIDDLE_X_AT));
  dp_w16(w, c, 0x1a, c->a);
  set_c(c, false);
  c->a = adc16(c, wram_r16(w, W_CAMERA_Y), rom_word(rom, VICTIMS_MIDDLE_Y_AT));
  dp_w16(w, c, 0x1c, c->a);
  k->blocks[VICTIMS_HEAD]++;

  for (;;) {
    // $8218: an entry retired for good is stepped over.
    if (victims_state(w, c, 0x0080, k) != 0) {
      PORT_COVER(victims_retired);
      k->blocks[VICTIMS_TAKEN]++;
      goto next;
    }
    // $8223: Y = X * 12
    lda(c, c->x);
    c->a = asl16(c, c->a);
    c->a = asl16(c, c->a);
    dp_w16(w, c, 0x0a, c->a);
    c->a = asl16(c, c->a);
    set_c(c, false);
    c->a = adc16(c, c->a, dp_r16(w, c, 0x0a));
    c->y = c->a;
    set_nz16(c, c->y);
    lda(c, ind_y(w, rom, c, 0x0c, k));
    k->blocks[VICTIMS_INDEX]++;
    if (c->a == 0) {
      // $81FD: the end of the list, and the next walk starts at the top.
      PORT_COVER(victims_list_end);
      k->blocks[VICTIMS_TAKEN]++;
      dp_w16(w, c, 0x10, 0);
      k->blocks[VICTIMS_STZ]++;
      victims_yield(c, k);
      return;
    }
    // $8231: x
    dp_w16(w, c, 0x16, c->a);
    k->blocks[VICTIMS_DX]++;
    if (victims_far(c, 0x1a, reach_x, w, k)) goto far;
    // $8241: y
    c->y = (uint16_t)(c->y + 1);
    c->y = (uint16_t)(c->y + 1);
    set_nz16(c, c->y);
    lda(c, ind_y(w, rom, c, 0x0c, k));
    dp_w16(w, c, 0x18, c->a);
    k->blocks[VICTIMS_DY]++;
    if (victims_far(c, 0x1c, reach_y, w, k)) goto far;
    // $8255: near, and started already?
    if (victims_state(w, c, 0x00ff, k) != 0) {
      k->blocks[VICTIMS_TAKEN]++;
      goto next;
    }
    PORT_COVER(victims_start_one);
    c->pc = VICTIMS_START_CALL_PC;  // JSR $81A2
    return;

  far:
    // $826B: far, and live?
    if (victims_state(w, c, 0x00ff, k) == 0) {
      k->blocks[VICTIMS_TAKEN]++;
      goto next;
    }
    // $8276: forget it, and hand its thread the stop.
    PORT_COVER(victims_stop_one);
    c->p |= PORT_P_M;
    c->a &= 0xff00u;
    set_nz8(c, 0);
    wram_w8(w, W_VICTIM_STATE + c->x, 0);
    c->p = (uint8_t)(c->p & ~PORT_P_M);
    lda(c, (uint16_t)(wram_r16(w, W_VICTIM_SLOT + c->x) & 0x00ffu));
    c->x = c->a;
    set_nz16(c, c->x);
    c->y = 0x00ff;
    set_nz16(c, c->y);
    k->blocks[VICTIMS_STOP]++;
    c->pc = VICTIMS_STOP_CALL_PC;  // JSL thread_call_handler
    return;

  next: {
    // $8267  INC $10 : BRA $8218
    const uint16_t v = (uint16_t)(dp_r16(w, c, 0x10) + 1);
    dp_w16(w, c, 0x10, v);
    set_nz16(c, v);
    k->blocks[VICTIMS_NEXT]++;
  }
  }
}

// ---------------------------------------------------------------------------
// $80:C8F6  object_spawner_body
// ---------------------------------------------------------------------------

// `$80:C90A  LDA #$0001`, and the `JSL thread_yield` after it.
static void object_yield(PortCpu* c, BodyWork* k) {
  lda(c, 0x0001);
  k->blocks[OBJECT_TICKS]++;
  c->pc = OBJECT_YIELD_PC;
}

// `INC $0C : INC $0C`
static void object_step(Wram* w, PortCpu* c, BodyWork* k) {
  for (int i = 0; i < 2; i++) {
    const uint16_t v = (uint16_t)(dp_r16(w, c, 0x0c) + 1);
    dp_w16(w, c, 0x0c, v);
    set_nz16(c, v);
  }
  k->blocks[OBJECT_STEP]++;
}

// One axis, from `LDA $7E6Dxx,X` to `BCS`. True when out of range on it.
static bool object_far(const Wram* w, PortCpu* c, uint32_t coord, uint8_t middle,
                       BodyWork* k) {
  lda(c, wram_r16(w, coord + c->x));
  set_c(c, true);
  c->a = sbc16(c, c->a, dp_r16(w, c, middle));
  k->blocks[OBJECT_D]++;
  if (c->a & 0x8000u) {
    negate(c);
    k->blocks[OBJECT_NEG]++;
  } else {
    k->blocks[OBJECT_TAKEN]++;
  }
  cmp16(c, c->a, 0x0090);
  k->blocks[OBJECT_CMP]++;
  if (flag(c, PORT_P_C)) k->blocks[OBJECT_TAKEN]++;
  return flag(c, PORT_P_C);
}

// Everything below reads `$0020`, `$1B6A` and `$1EC4,X` through the data bank,
// which the harness has checked shows low WRAM.
void object_polled(Wram* w, PortCpu* c, BodyWork* k) {
  // $C918: every fourth frame
  lda(c, (uint16_t)(wram_r16(w, 0x0020) & 0x0003));
  k->blocks[OBJECT_TICK]++;
  if (c->a != 0) {
    k->blocks[OBJECT_TAKEN]++;
    object_yield(c, k);
    return;
  }
  // $C920: the screen's middle
  set_c(c, false);
  c->a = adc16(c, wram_r16(w, W_CAMERA_X), 0x0080);
  dp_w16(w, c, 0x0e, c->a);
  set_c(c, false);
  c->a = adc16(c, wram_r16(w, W_CAMERA_Y), 0x0070);
  dp_w16(w, c, 0x10, c->a);
  k->blocks[OBJECT_HEAD]++;

  for (;;) {
    // $C932
    c->x = dp_r16(w, c, 0x0c);
    set_nz16(c, c->x);
    bit16(c, wram_r16(w, W_OBJECT_STATE + c->x));
    k->blocks[OBJECT_SCAN]++;
    if (flag(c, PORT_P_V)) {
      // $C908: the end, and the next walk starts at the top.
      PORT_COVER(object_list_end);
      k->blocks[OBJECT_TAKEN]++;
      dp_w16(w, c, 0x0c, 0);
      k->blocks[OBJECT_STZ]++;
      object_yield(c, k);
      return;
    }
    k->blocks[OBJECT_LIVE]++;
    if (flag(c, PORT_P_N)) {
      k->blocks[OBJECT_TAKEN]++;
      object_step(w, c, k);
      continue;
    }
    const bool far = object_far(w, c, W_OBJECT_X, 0x0e, k) ||
                     object_far(w, c, W_OBJECT_Y, 0x10, k);
    // $C95F / $C969
    lda(c, wram_r16(w, W_OBJECT_STATE + c->x));
    k->blocks[OBJECT_STATE]++;
    if (!far && c->a == 0) {
      PORT_COVER(object_give);
      c->pc = OBJECT_GIVE_CALL_PC;  // JSR $C9E3
      return;
    }
    if (far && c->a != 0) {
      PORT_COVER(object_free);
      c->pc = OBJECT_FREE_CALL_PC;  // JSR $CAA8
      return;
    }
    k->blocks[OBJECT_TAKEN]++;
    object_step(w, c, k);
  }
}

void object_resume(Wram* w, PortCpu* c, BodyWork* k) {
  // $C911  LDA $12 : BEQ $C918
  lda(c, dp_r16(w, c, 0x12));
  k->blocks[OBJECT_POLL]++;
  if (c->a != 0) {
    PORT_COVER(object_requests);
    c->pc = OBJECT_POLL_CALL_PC;  // JSR $CABF
    return;
  }
  k->blocks[OBJECT_TAKEN]++;
  object_polled(w, c, k);
}

// `$80:C967` and `$80:C971`: `BRA $C979`, the step, and the yield.
void object_acted(Wram* w, PortCpu* c, BodyWork* k) {
  k->blocks[OBJECT_BRA]++;
  object_step(w, c, k);
  object_yield(c, k);
}

// ---------------------------------------------------------------------------
// $81:80EC  actor_list_spawn
// ---------------------------------------------------------------------------

// `$81:8108  LDA #$0001`, and the `JSL thread_yield` after it.
static void actors_yield(PortCpu* c, BodyWork* k) {
  lda(c, 0x0001);
  k->blocks[ACTORS_TICKS]++;
  c->pc = ACTORS_YIELD_PC;
}

// `$81:8155  INC $10 : BRA $8108`
static void actors_next(Wram* w, PortCpu* c, BodyWork* k) {
  const uint16_t v = (uint16_t)(dp_r16(w, c, 0x10) + 1);
  dp_w16(w, c, 0x10, v);
  set_nz16(c, v);
  k->blocks[ACTORS_NEXT]++;
  actors_yield(c, k);
}

// `$81:8101  LDA #$FFFF : STA $14 : STZ $10`: a new pass.
static void actors_reset(Wram* w, PortCpu* c, BodyWork* k) {
  lda(c, 0xffff);
  dp_w16(w, c, 0x14, c->a);
  dp_w16(w, c, 0x10, 0);
  k->blocks[ACTORS_RESET]++;
  actors_yield(c, k);
}

// `ASL : STA $0A : ASL : ASL : CLC : ADC $0A : TAY`, from A: ten bytes an entry.
static void actors_times10(Wram* w, PortCpu* c) {
  c->a = asl16(c, c->a);
  dp_w16(w, c, 0x0a, c->a);
  c->a = asl16(c, c->a);
  c->a = asl16(c, c->a);
  set_c(c, false);
  c->a = adc16(c, c->a, dp_r16(w, c, 0x0a));
  c->y = c->a;
  set_nz16(c, c->y);
}

void actors_checked(Wram* w, const Rom* rom, PortCpu* c, BodyWork* k) {
  // $8113  BCS $8108: `$80:9D5B` says wait.
  k->blocks[ACTORS_CHECK]++;
  if (flag(c, PORT_P_C)) {
    PORT_COVER(actors_held);
    k->blocks[ACTORS_TAKEN]++;
    actors_yield(c, k);
    return;
  }
  // $8115: resting?
  c->x = dp_r16(w, c, 0x10);
  set_nz16(c, c->x);
  lda(c, (uint16_t)(wram_r16(w, W_ACTORS_COUNT + c->x) & 0x00ffu));
  k->blocks[ACTORS_COUNT]++;
  if (c->a != 0) {
    // $8120  DEC : SEP : STA : REP #$30 : BRA $8155
    lda(c, (uint16_t)(c->a - 1));
    sta8_long_x(w, c, W_ACTORS_COUNT, PORT_P_M | PORT_P_X);
    k->blocks[ACTORS_TICK]++;
    actors_next(w, c, k);
    return;
  }
  k->blocks[ACTORS_TAKEN]++;
  // $812B
  lda(c, dp_r16(w, c, 0x10));
  actors_times10(w, c);
  k->blocks[ACTORS_INDEX]++;
  lda(c, (uint16_t)(ind_y(w, rom, c, 0x0c, k) & 0x00ffu));
  k->blocks[ACTORS_TYPE]++;
  if (c->a != 0) {
    // $813D: its position, for `JSR $8024`
    c->y = (uint16_t)(c->y + 1);
    set_nz16(c, c->y);
    lda(c, ind_y(w, rom, c, 0x0c, k));
    dp_w16(w, c, 0x16, c->a);
    c->y = (uint16_t)(c->y + 1);
    c->y = (uint16_t)(c->y + 1);
    set_nz16(c, c->y);
    lda(c, ind_y(w, rom, c, 0x0c, k));
    dp_w16(w, c, 0x18, c->a);
    k->blocks[ACTORS_XY]++;
    c->pc = ACTORS_MEASURE_CALL_PC;
    return;
  }
  // $8159: the end of the list. Anyone near enough?
  k->blocks[ACTORS_TAKEN]++;
  lda(c, dp_r16(w, c, 0x14));
  cmp16(c, c->a, 0x0100);
  k->blocks[ACTORS_END]++;
  if (flag(c, PORT_P_C)) {
    PORT_COVER(actors_none_near);
    k->blocks[ACTORS_TAKEN]++;
    actors_reset(w, c, k);
    return;
  }
  // $8160: set it resting, and start it.
  PORT_COVER(actors_pick);
  lda(c, dp_r16(w, c, 0x12));
  c->x = c->a;
  set_nz16(c, c->x);
  actors_times10(w, c);
  k->blocks[ACTORS_PICK]++;
  lda(c, (uint16_t)(ind_y(w, rom, c, 0x0c, k) & 0x00ffu));
  sta8_long_x(w, c, W_ACTORS_COUNT, PORT_P_M | PORT_P_X);
  k->blocks[ACTORS_ARM]++;
  c->pc = ACTORS_START_CALL_PC;  // JSR $807E
}

void actors_measured(Wram* w, PortCpu* c, BodyWork* k) {
  // $814B  CMP $14 : BCS $8155
  cmp16(c, c->a, dp_r16(w, c, 0x14));
  k->blocks[ACTORS_BEST]++;
  if (flag(c, PORT_P_C)) {
    k->blocks[ACTORS_TAKEN]++;
  } else {
    PORT_COVER(actors_nearer);
    dp_w16(w, c, 0x14, c->a);
    lda(c, dp_r16(w, c, 0x10));
    dp_w16(w, c, 0x12, c->a);
    k->blocks[ACTORS_NEW_BEST]++;
  }
  actors_next(w, c, k);
}

void actors_started(Wram* w, PortCpu* c, BodyWork* k) {
  k->blocks[ACTORS_BACK]++;  // BRA $8101
  actors_reset(w, c, k);
}

// ---------------------------------------------------------------------------
// $82:D7CF  level_tile_anim
// ---------------------------------------------------------------------------

// `$82:D87A  LDA #$0001`, and the `JSL thread_yield` after it.
void tile_anim_queued(Wram* w, PortCpu* c, BodyWork* k) {
  (void)w;
  lda(c, 0x0001);
  k->blocks[TANIM_TICKS]++;
  c->pc = TILE_ANIM_YIELD_PC;
}

// `$1E80`, `$1F56` and the bit table are all through the data bank, which the
// harness has checked shows low WRAM.
void tile_anim_resume(Wram* w, const Rom* rom, PortCpu* c, BodyWork* k) {
  // $D881  LDA $48 : BNE $D81C
  lda(c, dp_r16(w, c, 0x48));
  k->blocks[TANIM_HEAD]++;
  if (c->a == 0) {
    PORT_COVER(tile_anim_ended);
    c->pc = TILE_ANIM_END_PC;
    return;
  }
  k->blocks[TANIM_TAKEN]++;
  c->x = 0xfffe;
  set_nz16(c, c->x);
  k->blocks[TANIM_START]++;

  for (;;) {
    // $D81F
    c->x = (uint16_t)(c->x + 2);
    set_nz16(c, c->x);
    lda(c, wram_r16(w, dpx(c, 0x38)));
    k->blocks[TANIM_SLOT]++;
    if (!(c->a & 0x8000u)) {
      cmp16(c, c->x, 0x0010);
      k->blocks[TANIM_IDLE]++;
      if (c->x == 0x0010) {
        k->blocks[TANIM_TAKEN]++;
        break;
      }
      k->blocks[TANIM_BRA]++;
      continue;
    }
    k->blocks[TANIM_TAKEN]++;
    // $D82C  DEC $28,X : BNE $D81F
    const uint16_t left = (uint16_t)(wram_r16(w, dpx(c, 0x28)) - 1);
    wram_w16(w, dpx(c, 0x28), left);
    set_nz16(c, left);
    k->blocks[TANIM_COUNT]++;
    if (left != 0) {
      k->blocks[TANIM_TAKEN]++;
      continue;
    }
    // $D830: the next frame
    PORT_COVER(tile_anim_stepped);
    lda(c, wram_r16(w, dpx(c, 0x18)));
    c->y = c->a;
    set_nz16(c, c->y);
    lda(c, wram_r16(w, dpx(c, 0x08)));
    dp_w16(w, c, 0x00, c->a);
    lda(c, ind_long_y(w, rom, c, 0x00, k));
    c->a = asl16(c, c->a);
    wram_w16(w, W_TILE_ANIM_FRAME + c->x, c->a);
    for (int i = 0; i < 4; i++) c->a = asl16(c, c->a);
    set_c(c, false);
    c->a = adc16(c, c->a, wram_r16(w, W_TILE_ANIM_BASE));
    wram_w16(w, W_TILE_ANIM_TILE + c->x, c->a);
    lda(c, data_r16(w, rom,
                    ((uint32_t)c->db << 16) + (TILE_ANIM_BITS & 0xffffu) + c->x,
                    k));
    lda(c, (uint16_t)(c->a | wram_r16(w, W_TILE_ANIM_DIRTY)));
    wram_w16(w, W_TILE_ANIM_DIRTY, c->a);
    c->y = (uint16_t)(c->y + 2);
    set_nz16(c, c->y);
    lda(c, ind_long_y(w, rom, c, 0x00, k));
    k->blocks[TANIM_FRAME]++;
    if (c->a & 0x8000u) {
      cmp16(c, c->a, 0xfffe);
      k->blocks[TANIM_WRAP]++;
      if (c->a == 0xfffe) {
        // $D886  STZ $38,X : DEC $48
        PORT_COVER(tile_anim_stopped);
        k->blocks[TANIM_TAKEN]++;
        wram_w16(w, dpx(c, 0x38), 0);
        const uint16_t live = (uint16_t)(dp_r16(w, c, 0x48) - 1);
        dp_w16(w, c, 0x48, live);
        set_nz16(c, live);
        k->blocks[TANIM_END]++;
        continue;
      }
      // $D85E: from the top
      PORT_COVER(tile_anim_looped);
      c->y = 0x0000;
      set_nz16(c, c->y);
      lda(c, ind_long_y(w, rom, c, 0x00, k));
      k->blocks[TANIM_RESTART]++;
    } else {
      k->blocks[TANIM_TAKEN]++;
    }
    // $D863
    wram_w16(w, dpx(c, 0x28), c->a);
    c->y = (uint16_t)(c->y + 2);
    set_nz16(c, c->y);
    wram_w16(w, dpx(c, 0x18), c->y);
    k->blocks[TANIM_NEXT]++;
  }

  // $D86B: anything to upload?
  lda(c, wram_r16(w, W_TILE_ANIM_DIRTY));
  k->blocks[TANIM_DONE]++;
  if (c->a == 0) {
    k->blocks[TANIM_TAKEN]++;
    tile_anim_queued(w, c, k);
    return;
  }
  PORT_COVER(tile_anim_upload);
  lda(c, 0xd88c);
  c->y = 0x0082;
  set_nz16(c, c->y);
  k->blocks[TANIM_QUEUE]++;
  c->pc = TILE_ANIM_QUEUE_CALL_PC;
}

// ---------------------------------------------------------------------------
// $80:CDF4  player_body
// ---------------------------------------------------------------------------

// `$80:CDF7  LDA #$0001`, and the `JSL thread_yield` after it.
void player_ticks(PortCpu* c, BodyWork* k) {
  lda(c, 0x0001);
  k->blocks[PBODY_TICKS]++;
  c->pc = PLAYER_YIELD_PC;
}

// `$80:CE23  BRA $CDF7`: the frame is done.
void player_loop(PortCpu* c, BodyWork* k) {
  k->blocks[PBODY_BRA]++;
  player_ticks(c, k);
}

// `$80:CE04  PEA $CE0B : LDA $28 : DEC : PHA`, and the `RTS` after it calls
// the state handler, which comes back to `$CE0C`.
void player_state(Wram* w, PortCpu* c, BodyWork* k) {
  push16(w, c, 0xce0b);
  lda(c, (uint16_t)(dp_r16(w, c, 0x28) - 1));
  push16(w, c, c->a);
  k->blocks[PBODY_STATE]++;
  c->pc = PLAYER_STATE_CALL_PC;
}

// `$80:CE0C  LDA $2A : BEQ $CE16`, and the movement handler the same way. It
// comes back to `$CE16`, the `JSR $F327` a zero goes straight to.
void player_move(Wram* w, PortCpu* c, BodyWork* k) {
  lda(c, dp_r16(w, c, 0x2a));
  k->blocks[PBODY_MOVE]++;
  if (c->a == 0) {
    PORT_COVER(player_still);
    k->blocks[PBODY_TAKEN]++;
    c->pc = PLAYER_PUBLISH_PC;
    return;
  }
  push16(w, c, 0xce15);
  lda(c, (uint16_t)(c->a - 1));
  push16(w, c, c->a);
  k->blocks[PBODY_MOVE_CALL]++;
  c->pc = PLAYER_MOVE_CALL_PC;
}

// `$80:CE19  LDA $1A : STA $1C`: this frame's buttons are next frame's last.
void player_buttons(Wram* w, PortCpu* c, BodyWork* k) {
  lda(c, dp_r16(w, c, 0x1a));
  dp_w16(w, c, 0x1c, c->a);
  k->blocks[PBODY_BUTTONS]++;
  c->pc = PLAYER_WON_CALL_PC;
}

// `$80:D1EA  LDX $70`, in front of `JMP ($D1EF,X)`.
void player_branch(Wram* w, PortCpu* c, BodyWork* k) {
  c->x = dp_r16(w, c, 0x70);
  set_nz16(c, c->x);
  k->blocks[PBODY_BRANCH]++;
  c->pc = PLAYER_BRANCH_JMP_PC;
}

// `$80:D01B`. Bit 15 of `$50` is an event request, and what it does is the
// ROM's. Otherwise, unless `$6A` is set, the hit recovery count at `$52` steps
// down, and once it is negative it is held at `$FFFF`.
void player_hurt(Wram* w, PortCpu* c, BodyWork* k) {
  bit16(c, dp_r16(w, c, 0x50));
  k->blocks[PBODY_EVENT]++;
  if (flag(c, PORT_P_N)) {
    PORT_COVER(player_event);
    k->blocks[PBODY_TAKEN]++;
    c->pc = PLAYER_HURT_EVENT_PC;
    return;
  }
  lda(c, dp_r16(w, c, 0x6a));
  k->blocks[PBODY_SKIP]++;
  if (c->a != 0) {
    PORT_COVER(player_no_recovery);
    k->blocks[PBODY_TAKEN]++;
    c->pc = PLAYER_HURT_RTS_PC;
    return;
  }
  const uint16_t left = (uint16_t)(dp_r16(w, c, 0x52) - 1);
  dp_w16(w, c, 0x52, left);
  set_nz16(c, left);
  k->blocks[PBODY_RECOVER]++;
  if (!(left & 0x8000u)) {
    k->blocks[PBODY_TAKEN]++;
    c->pc = PLAYER_HURT_RTS_PC;
    return;
  }
  PORT_COVER(player_recovered);
  lda(c, 0xffff);
  dp_w16(w, c, 0x52, c->a);
  k->blocks[PBODY_RECOVERED]++;
  c->pc = PLAYER_HURT_RESET_RTS_PC;
}

// `$80:CE25  LDA $1D52 : BNE $CE6D`. With no neighbours left the level is
// over, and ending it is the ROM's.
void player_won(Wram* w, PortCpu* c, BodyWork* k) {
  lda(c, wram_r16(w, W_NEIGHBOURS_LEFT));
  k->blocks[PBODY_WON]++;
  if (c->a != 0) {
    k->blocks[PBODY_TAKEN]++;
    c->pc = PLAYER_WON_RTS_PC;
    return;
  }
  PORT_COVER(player_level_won);
  c->pc = PLAYER_WON_END_PC;
}

// `$80:CE72  LDX $0E : LDA $1CB8,X : BEQ $CE7A`. With no health left the
// player dies, and that is the ROM's.
void player_dead(Wram* w, PortCpu* c, BodyWork* k) {
  c->x = dp_r16(w, c, 0x0e);
  set_nz16(c, c->x);
  lda(c, wram_r16(w, (uint16_t)(W_PLAYER_HEALTH + c->x)));
  k->blocks[PBODY_DEAD]++;
  if (c->a != 0) {
    c->pc = PLAYER_DEAD_RTS_PC;
    return;
  }
  PORT_COVER(player_died);
  k->blocks[PBODY_TAKEN]++;
  c->pc = PLAYER_DEAD_END_PC;
}

// ---------------------------------------------------------------------------
// $80:E4BA  player_walk
// ---------------------------------------------------------------------------

// `LDX dp : LDY dp`, the point the next call is asked about.
static void walk_point(Wram* w, PortCpu* c, uint8_t x, uint8_t y) {
  c->x = dp_r16(w, c, x);
  set_nz16(c, c->x);
  c->y = dp_r16(w, c, y);
  set_nz16(c, c->y);
}

// `LDA $08` first, for `$BFC8`: the player's own record, which it skips.
static void walk_self(Wram* w, PortCpu* c, uint8_t x, uint8_t y) {
  lda(c, dp_r16(w, c, 0x08));
  walk_point(w, c, x, y);
}

// A `BCC` or `BCS` on what the last call said, and whether it was taken.
static bool walk_branch(PortCpu* c, BodyWork* k, bool on_carry) {
  k->blocks[WALK_BRANCH]++;
  const bool taken = flag(c, PORT_P_C) == on_carry;
  if (taken) k->blocks[WALK_TAKEN]++;
  return taken;
}

// `$E4D3`, `$E4F5`, `$E503`, `$E512` and `$E534`: the next question.
static void walk_x_ask(Wram* w, PortCpu* c, BodyWork* k, uint32_t call) {
  walk_point(w, c, 0x34, 0x32);
  k->blocks[WALK_AT_X]++;
  c->pc = call;
}

static void walk_y_ask(Wram* w, PortCpu* c, BodyWork* k, uint32_t call) {
  walk_point(w, c, 0x30, 0x36);
  k->blocks[WALK_AT_Y]++;
  c->pc = call;
}

// `$E503`: x is settled one way or the other, and y starts.
static void walk_to_y(Wram* w, PortCpu* c, BodyWork* k) {
  walk_y_ask(w, c, k, WALK_Y_TERRAIN_CALL_PC);
}

void walk_start(Wram* w, PortCpu* c, BodyWork* k) {
  // $E4BA  BIT $54 : BPL $E4C1
  bit16(c, dp_r16(w, c, 0x54));
  k->blocks[WALK_BOOST]++;
  if (flag(c, PORT_P_N)) {
    PORT_COVER(walk_twice);
    c->pc = WALK_TWICE_PC;
    return;
  }
  k->blocks[WALK_TAKEN]++;
  c->pc = WALK_PROPOSE_CALL_PC;
}

void walk_proposed(Wram* w, PortCpu* c, BodyWork* k) {
  walk_x_ask(w, c, k, WALK_X_TERRAIN_CALL_PC);
}

void walk_x_terrain(Wram* w, PortCpu* c, BodyWork* k) {
  // $E4CC  BCC $E4D3
  if (walk_branch(c, k, false)) {
    walk_x_ask(w, c, k, WALK_X_TETHER_CALL_PC);
    return;
  }
  PORT_COVER(walk_x_solid);
  c->pc = WALK_X_REACT_CALL_PC;
}

void walk_x_reacted(Wram* w, PortCpu* c, BodyWork* k) {
  // $E4D1  BCS $E503
  if (walk_branch(c, k, true)) {
    PORT_COVER(walk_x_stopped);
    walk_to_y(w, c, k);
    return;
  }
  walk_x_ask(w, c, k, WALK_X_TETHER_CALL_PC);
}

void walk_x_tether(Wram* w, PortCpu* c, BodyWork* k) {
  // $E4DB  BCS $E503
  if (walk_branch(c, k, true)) {
    PORT_COVER(walk_x_tethered);
    walk_to_y(w, c, k);
    return;
  }
  walk_self(w, c, 0x34, 0x32);
  k->blocks[WALK_SELF_X]++;
  c->pc = WALK_X_OBSTACLE_CALL_PC;
}

void walk_x_obstacle(Wram* w, PortCpu* c, BodyWork* k) {
  // $E4E7  BCC $E4F5
  if (walk_branch(c, k, false)) {
    walk_x_ask(w, c, k, WALK_X_BOUNDS_CALL_PC);
    return;
  }
  PORT_COVER(walk_x_obstacle);
  walk_self(w, c, 0x30, 0x32);
  k->blocks[WALK_SELF_HERE]++;
  c->pc = WALK_X_ASK_CALL_PC;
}

void walk_x_asked(Wram* w, PortCpu* c, BodyWork* k) {
  // $E4F3  BCC $E503
  if (walk_branch(c, k, false)) {
    walk_to_y(w, c, k);
    return;
  }
  PORT_COVER(walk_x_overlap);
  walk_x_ask(w, c, k, WALK_X_BOUNDS_CALL_PC);
}

void walk_x_bounds(Wram* w, PortCpu* c, BodyWork* k) {
  // $E4FD  BCS $E503
  if (walk_branch(c, k, true)) {
    PORT_COVER(walk_x_off_map);
    walk_to_y(w, c, k);
    return;
  }
  lda(c, dp_r16(w, c, 0x34));
  dp_w16(w, c, 0x30, c->a);
  k->blocks[WALK_TAKE_X]++;
  walk_to_y(w, c, k);
}

void walk_y_terrain(Wram* w, PortCpu* c, BodyWork* k) {
  // $E50B  BCC $E512
  if (walk_branch(c, k, false)) {
    walk_y_ask(w, c, k, WALK_Y_TETHER_CALL_PC);
    return;
  }
  PORT_COVER(walk_y_solid);
  c->pc = WALK_Y_REACT_CALL_PC;
}

void walk_y_reacted(Wram* w, PortCpu* c, BodyWork* k) {
  // $E510  BCS $E542
  if (walk_branch(c, k, true)) {
    PORT_COVER(walk_y_stopped);
    c->pc = WALK_RTS_PC;
    return;
  }
  walk_y_ask(w, c, k, WALK_Y_TETHER_CALL_PC);
}

void walk_y_tether(Wram* w, PortCpu* c, BodyWork* k) {
  // $E51A  BCS $E542
  if (walk_branch(c, k, true)) {
    PORT_COVER(walk_y_tethered);
    c->pc = WALK_RTS_PC;
    return;
  }
  walk_self(w, c, 0x30, 0x36);
  k->blocks[WALK_SELF_Y]++;
  c->pc = WALK_Y_OBSTACLE_CALL_PC;
}

void walk_y_obstacle(Wram* w, PortCpu* c, BodyWork* k) {
  // $E526  BCC $E534
  if (walk_branch(c, k, false)) {
    walk_y_ask(w, c, k, WALK_Y_BOUNDS_CALL_PC);
    return;
  }
  PORT_COVER(walk_y_obstacle);
  walk_self(w, c, 0x30, 0x32);
  k->blocks[WALK_SELF_HERE]++;
  c->pc = WALK_Y_ASK_CALL_PC;
}

void walk_y_asked(Wram* w, PortCpu* c, BodyWork* k) {
  // $E532  BCC $E542
  if (walk_branch(c, k, false)) {
    c->pc = WALK_RTS_PC;
    return;
  }
  PORT_COVER(walk_y_overlap);
  walk_y_ask(w, c, k, WALK_Y_BOUNDS_CALL_PC);
}

void walk_y_bounds(Wram* w, PortCpu* c, BodyWork* k) {
  // $E53C  BCS $E542
  if (walk_branch(c, k, true)) {
    PORT_COVER(walk_y_off_map);
    c->pc = WALK_RTS_PC;
    return;
  }
  lda(c, dp_r16(w, c, 0x36));
  dp_w16(w, c, 0x32, c->a);
  k->blocks[WALK_TAKE_Y]++;
  c->pc = WALK_RTS_PC;
}

// `$80:E739`, what solid ground does.
const uint16_t WALK_SOLID_VALUE[WALK_SOLID_REACTIONS] = {
    0x0100, 0x0200, 0x0010, 0x0020, 0x0800, 0x8008};
const uint32_t WALK_SOLID_REACT_PC[WALK_SOLID_REACTIONS] = {
    0x80e742u, 0x80e74fu, 0x80e760u, 0x80e76au, 0x80e774u, 0x80e789u};

void walk_solid(PortCpu* c, BodyWork* k) {
  c->a = asl16(c, c->a);
  lda(c, (uint16_t)(c->a & 0xcb38u));
  k->blocks[WALK_SOLID_HEAD]++;
  for (int i = 0; i < WALK_SOLID_REACTIONS; i++) {
    if (i) k->blocks[WALK_SOLID_NEXT]++;
    cmp16(c, c->a, WALK_SOLID_VALUE[i]);
    if (c->a == WALK_SOLID_VALUE[i]) {
      PORT_COVER(walk_solid_reacts);
      c->pc = WALK_SOLID_REACT_PC[i];
      return;
    }
    k->blocks[WALK_TAKEN]++;
  }
  set_c(c, true);
  k->blocks[WALK_SOLID_SEC]++;
  c->pc = WALK_SOLID_RTS_PC;
}
