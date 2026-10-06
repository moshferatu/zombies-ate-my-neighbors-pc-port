// The neighbours' loops, a frame each -- see port/neighbours.h.

#include "port/neighbours.h"

#include "port/coverage.h"

const NeighbourCycle NEIGHBOUR_CYCLES[NEIGHBOUR_CYCLE_COUNT] = {
    {0x8396c0u, 0x8396bcu, 0x8396e1u, 0x839732u, 17},
    {0x83979du, 0x839799u, 0x8397beu, 0x83980fu, 13},
    {0x839e3cu, 0x839e38u, 0x839e5du, 0x839eaeu, 4},
};

static const NeighbourCycle TOURISTS = {
    TOURISTS_PC, TOURISTS_SLEEP_PC, TOURISTS_TOLD_PC, TOURISTS_PICTURES,
    TOURISTS_PICTURE_COUNT};

const NeighbourWatch NEIGHBOUR_WATCHES[NEIGHBOUR_WATCH_COUNT] = {
    {0x839ab7u, 0x839afbu, 0x839ab3u, 0x839af7u, 0x839b1eu, 0x839b9du, 2,
     0x839ba5u, 2, false, 0x0030},
    {0x839969u, 0x8399a6u, 0x839965u, 0x8399a2u, 0x8399cau, 0x839a49u, 4,
     0x839a59u, 12, true, 0},
};

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

// The picture `which` of a table, four bytes an entry, and the frames it is
// shown for. A is left at the frames and X at the entry's offset.
static void show(Wram* w, const Rom* rom, PortCpu* c, uint32_t table,
                 uint16_t which) {
  c->x = asl16(c, asl16(c, which));
  wram_w16(w, (uint16_t)(field(w, c, NEIGHBOUR_DP_RECORD) + ACTOR_META),
           rom_word(rom, table + c->x));
  lda(c, rom_word(rom, table + 2 + c->x));
  set_field(w, c, NEIGHBOUR_DP_FRAMES, c->a);
}

// The next picture of `count`, round to the first after the last.
static void show_next(Wram* w, const Rom* rom, PortCpu* c, NeighbourWork* k,
                      uint32_t table, uint16_t count) {
  uint16_t which = (uint16_t)(field(w, c, NEIGHBOUR_DP_WHICH) + 1);
  cmp16(c, which, count);
  if (which == count) {
    k->wrapped = true;
    which = 0;
  }
  set_field(w, c, NEIGHBOUR_DP_WHICH, which);
  show(w, rom, c, table, which);
}

void neighbour_cycle_frame(Wram* w, const Rom* rom, PortCpu* c,
                           NeighbourWork* k, const NeighbourCycle* cycle) {
  show_next(w, rom, c, k, cycle->pictures, cycle->count);
  c->y = ACTOR_META;
  lda(c, field(w, c, NEIGHBOUR_DP_TOLD));
  if (c->a != 0) {
    PORT_COVER(neighbour_cycle_told);
    k->told = true;
    c->pc = cycle->told_pc;
    return;
  }
  if (k->wrapped) {
    PORT_COVER(neighbour_cycle_went_round);
  } else {
    PORT_COVER(neighbour_cycle_shown);
  }
  lda(c, field(w, c, NEIGHBOUR_DP_FRAMES));
  c->pc = cycle->sleep_pc;
}

void tourists_frame(Wram* w, const Rom* rom, PortCpu* c, NeighbourWork* k) {
  SpawnRoomRegs room;
  spawn_has_room(w, &room);
  c->a = room.a;
  c->p = (uint8_t)((c->p & ~(PORT_P_N | PORT_P_Z | PORT_P_C)) |
                   (room.n ? PORT_P_N : 0) | (room.z ? PORT_P_Z : 0) |
                   (room.c ? PORT_P_C : 0));
  k->no_room = room.c;
  if (!room.c) {
    lda(c, wram_r16(w, W_TOURISTS_TURN));
    if (c->a != 0) {
      PORT_COVER(tourists_turned);
      k->turned = true;
      c->pc = TOURISTS_TURN_PC;
      return;
    }
  }
  PORT_COVER(tourists_stayed);
  neighbour_cycle_frame(w, rom, c, k, &TOURISTS);
}

// `LDA $1E : BNE`. True when something has set it.
static bool told(const Wram* w, PortCpu* c, NeighbourWork* k,
                 const NeighbourWatch* watch) {
  lda(c, field(w, c, NEIGHBOUR_DP_TOLD));
  if (c->a == 0) return false;
  k->told = true;
  c->pc = watch->told_pc;
  return true;
}

void neighbour_watch_frame(Wram* w, const Rom* rom, PortCpu* c,
                           NeighbourWork* k, const NeighbourWatch* watch) {
  if (told(w, c, k, watch)) {
    PORT_COVER(neighbour_watch_told);
    return;
  }
  uint16_t dist = 0;
  c->y = field(w, c, NEIGHBOUR_DP_Y);
  c->x = actor_nearest_id3_counted(w, field(w, c, NEIGHBOUR_DP_X), c->y, &dist,
                                   &k->nearest);
  k->looked = true;
  cmp16(c, dist, NEIGHBOUR_WATCH_NEAR);
  if (dist < NEIGHBOUR_WATCH_NEAR) {
    k->near = true;
    if (watch->sfx != 0) {
      PORT_COVER(neighbour_watch_cried);
      ApuSfxRegs sound;
      apu_play_sfx(w, watch->sfx, c->d, &sound);
      k->cried = true;
      c->x = sound.x;
      c->y = sound.y;
      set_c(c, sound.c);
    } else {
      PORT_COVER(neighbour_watch_alarmed);
    }
    set_field(w, c, NEIGHBOUR_DP_WHICH, 0);
    set_field(w, c, NEIGHBOUR_DP_FRAMES, 1);
    lda(c, 1);
    c->pc = watch->alarm_sleep_pc;
    return;
  }

  PORT_COVER(neighbour_watch_at_ease);
  show_next(w, rom, c, k, watch->pictures, watch->count);
  c->y = field(w, c, NEIGHBOUR_DP_RECORD);
  c->pc = watch->sleep_pc;
}

void neighbour_alarm_frame(Wram* w, const Rom* rom, PortCpu* c,
                           NeighbourWork* k, const NeighbourWatch* watch) {
  if (told(w, c, k, watch)) {
    PORT_COVER(neighbour_alarm_told);
    return;
  }
  // The first shows the picture it is on and counts; the second counts and
  // shows the one it has come to.
  const uint16_t on = field(w, c, NEIGHBOUR_DP_WHICH);
  const uint16_t next = (uint16_t)(on + 1);
  const uint16_t tested = watch->counts_first ? next : on;
  lda(c, tested);
  cmp16(c, tested, watch->alarm_count);
  if (watch->counts_first ? tested >= watch->alarm_count
                          : tested == watch->alarm_count) {
    // All shown: back to looking, with the frames the last one had.
    PORT_COVER(neighbour_alarm_over);
    k->over = true;
    set_field(w, c, NEIGHBOUR_DP_WHICH, 0);
    lda(c, field(w, c, NEIGHBOUR_DP_FRAMES));
    c->pc = watch->sleep_pc;
    return;
  }
  PORT_COVER(neighbour_alarm_shown);
  set_field(w, c, NEIGHBOUR_DP_WHICH, next);
  show(w, rom, c, watch->alarm_pictures, tested);
  c->y = field(w, c, NEIGHBOUR_DP_RECORD);
  c->pc = watch->alarm_sleep_pc;
}

void neighbour_rise_frame(Wram* w, PortCpu* c, NeighbourWork* k) {
  const uint16_t z = (uint16_t)(field(w, c, NEIGHBOUR_DP_RECORD) + ACTOR_Z);
  c->y = ACTOR_Z;
  c->a = (uint16_t)(wram_r16(w, z) + 1);
  wram_w16(w, z, c->a);
  const uint16_t left = (uint16_t)(field(w, c, NEIGHBOUR_RISE_DP_LEFT) - 1);
  set_field(w, c, NEIGHBOUR_RISE_DP_LEFT, left);
  set_nz16(c, left);
  if (left == 0) {
    PORT_COVER(neighbour_risen);
    k->risen = true;
    c->pc = NEIGHBOUR_RISE_DONE_PC;
    return;
  }
  PORT_COVER(neighbour_rising);
  lda(c, NEIGHBOUR_RISE_FRAMES);
  c->pc = NEIGHBOUR_RISE_SLEEP_PC;
}

void neighbour_sign_frame(Wram* w, const Rom* rom, PortCpu* c,
                          NeighbourWork* k) {
  const uint16_t which = (uint16_t)(field(w, c, NEIGHBOUR_SIGN_DP_WHICH) + 2);
  set_field(w, c, NEIGHBOUR_SIGN_DP_WHICH, which);
  c->x = which & 2;
  c->y = field(w, c, NEIGHBOUR_SIGN_DP_RECORD);
  c->a = rom_word(rom, NEIGHBOUR_SIGN_PICTURES + c->x);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), c->a);
  const uint16_t left = (uint16_t)(field(w, c, NEIGHBOUR_SIGN_DP_LEFT) - 1);
  set_field(w, c, NEIGHBOUR_SIGN_DP_LEFT, left);
  set_nz16(c, left);
  if (left == 0) {
    PORT_COVER(neighbour_sign_over);
    k->sign_over = true;
    c->pc = NEIGHBOUR_SIGN_OVER_PC;
    return;
  }
  lda(c, field(w, c, NEIGHBOUR_DP_TOLD));
  if (c->a != 0) {
    PORT_COVER(neighbour_sign_told);
    k->told = true;
    c->pc = NEIGHBOUR_SIGN_TOLD_PC;
    return;
  }
  PORT_COVER(neighbour_sign_shown);
  lda(c, NEIGHBOUR_SIGN_TICKS);
  c->pc = NEIGHBOUR_SIGN_SLEEP_PC;
}
