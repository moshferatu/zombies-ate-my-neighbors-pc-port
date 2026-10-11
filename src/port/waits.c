// Three threads with nothing to do but wait -- see port/waits.h.

#include "port/waits.h"

#include "port/coverage.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// A `JSL` at `at` and its `RTL`: the return is left under the stack.
static void called_long(Wram* w, PortCpu* c, uint8_t bank, uint16_t at) {
  push8(w, c, bank);
  push16(w, c, (uint16_t)(at + 3));
  (void)pull16(w, c);
  (void)pull8(w, c);
}

// What either of the two that pick a player leaves: A, X and Y, the carry,
// and N and Z from the page it pulls.
static void picked(PortCpu* c, const PlayerPickRegs* r) {
  c->a = r->a;
  c->x = r->x;
  c->y = r->y;
  set_nz16(c, c->d);
  set_c(c, r->c);
}

void near_wait(Wram* w, PortCpu* c, NearWait* did) {
  did->begun = c->pc == NEAR_WAIT_PC;
  if (did->begun) {
    set_field(w, c, NEAR_WAIT_DP_X, field(w, c, NEAR_WAIT_DP_GIVEN_X));
    set_field(w, c, NEAR_WAIT_DP_Y, field(w, c, NEAR_WAIT_DP_GIVEN_Y));
  }
  player_in_range(w, NEAR_WAIT_WITHIN, field(w, c, NEAR_WAIT_DP_X),
                  field(w, c, NEAR_WAIT_DP_Y), &did->players);
  called_long(w, c, NEAR_WAIT_BANK, NEAR_WAIT_ASK_AT);
  picked(c, &did->players);
  c->y = c->a;
  set_nz16(c, c->y);
  did->found = c->a != 0;
  if (did->found) {
    PORT_COVER(near_wait_found);
    c->pc = NEAR_WAIT_RTS_PC;
    return;
  }
  PORT_COVER(near_wait_on);
  c->a = NEAR_WAIT_TICKS;
  set_nz16(c, c->a);
  c->pc = NEAR_WAIT_YIELD_PC;
}

bool idle_wait_supported(const Wram* w, uint16_t page) {
  return wram_r16(w, (uint16_t)(page + IDLE_WAIT_DP_RECORD)) < 0x1f00 &&
         (wram_r16(w, (uint16_t)(page + IDLE_WAIT_DP_PICTURE)) & ~0x0006u) == 0;
}

// `$81:A613`: a tick of the picture it shows, and the next when its ticks
// are up.
static void shows(Wram* w, const Rom* rom, PortCpu* c, IdleWait* did) {
  const uint16_t left = (uint16_t)(field(w, c, IDLE_WAIT_DP_LEFT) - 1);
  set_field(w, c, IDLE_WAIT_DP_LEFT, left);
  set_nz16(c, left);
  did->turned = (left & 0x8000u) != 0;
  if (!did->turned) {
    PORT_COVER(idle_wait_same_picture);
    return;
  }
  PORT_COVER(idle_wait_next_picture);
  set_field(w, c, IDLE_WAIT_DP_LEFT, IDLE_WAIT_PICTURE_TICKS);
  c->x = field(w, c, IDLE_WAIT_DP_PICTURE);
  c->y = field(w, c, IDLE_WAIT_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META),
           bus_r16(w, rom, IDLE_WAIT_PICTURES + c->x));
  c->a = (uint16_t)((c->x + 2) & 0x0007u);
  set_nz16(c, c->a);
  set_field(w, c, IDLE_WAIT_DP_PICTURE, c->a);
}

void idle_wait(Wram* w, const Rom* rom, PortCpu* c, IdleWait* did) {
  // `JSR $A603`, whose return is left under the stack as the call's is.
  push16(w, c, IDLE_WAIT_LOOK_RET);
  player_bearing(w, rom, IDLE_WAIT_WITHIN, field(w, c, IDLE_WAIT_DP_X),
                 field(w, c, IDLE_WAIT_DP_Y), &did->players);
  called_long(w, c, IDLE_WAIT_BANK, IDLE_WAIT_ASK_AT);
  (void)pull16(w, c);
  picked(c, &did->players);
  c->x = c->a;
  set_nz16(c, c->x);
  did->turned = false;
  if (c->a != 0) {
    PORT_COVER(idle_wait_found);
    set_c(c, true);
    did->end = IDLE_WAIT_FOUND;
    c->pc = IDLE_WAIT_FOUND_PC;
    return;
  }
  shows(w, rom, c, did);
  set_c(c, false);
  c->a = field(w, c, IDLE_WAIT_DP_GONE);
  set_nz16(c, c->a);
  if (c->a != 0) {
    PORT_COVER(idle_wait_gone);
    did->end = IDLE_WAIT_GONE;
    c->pc = IDLE_WAIT_GONE_PC;
    return;
  }
  PORT_COVER(idle_wait_on);
  did->end = IDLE_WAIT_ON;
  c->a = IDLE_WAIT_TICKS;
  set_nz16(c, c->a);
  c->pc = IDLE_WAIT_YIELD_PC;
}

bool hits_wait_supported(const Wram* w, uint16_t page) {
  return wram_r16(w, (uint16_t)(page + HITS_WAIT_DP_RECORD)) < 0x1f00 &&
         wram_r16(w, (uint16_t)(page + HITS_WAIT_DP_PICTURE)) <
             HITS_WAIT_PICTURE_COUNT;
}

// `$82:F284`: the next picture, and the collide id that goes with it.
static void shows_next(Wram* w, const Rom* rom, PortCpu* c, HitsWait* did) {
  set_field(w, c, HITS_WAIT_DP_LEFT, HITS_WAIT_PICTURE_TICKS);
  uint16_t picture = (uint16_t)(field(w, c, HITS_WAIT_DP_PICTURE) + 1);
  did->wrapped = picture == HITS_WAIT_PICTURE_COUNT;
  if (did->wrapped) {
    PORT_COVER(hits_wait_first_picture);
    picture = 0;
  } else {
    PORT_COVER(hits_wait_next_picture);
  }
  set_field(w, c, HITS_WAIT_DP_PICTURE, picture);
  c->x = (uint16_t)(picture * 2);
  c->y = field(w, c, HITS_WAIT_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META),
           bus_r16(w, rom, HITS_WAIT_PICTURES + c->x));
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), HITS_WAIT_PICTURE_BANK);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID),
           bus_r16(w, rom, HITS_WAIT_COLLIDE_IDS + c->x));
  // The last shift's.
  set_c(c, false);
}

void hits_wait(Wram* w, const Rom* rom, PortCpu* c, HitsWait* did) {
  const uint16_t left = (uint16_t)(field(w, c, HITS_WAIT_DP_LEFT) - 1);
  set_field(w, c, HITS_WAIT_DP_LEFT, left);
  did->turned = (left & 0x8000u) != 0;
  did->wrapped = false;
  if (did->turned) {
    shows_next(w, rom, c, did);
  } else {
    PORT_COVER(hits_wait_same_picture);
  }
  c->a = field(w, c, HITS_WAIT_DP_HIT_BY);
  set_nz16(c, c->a);
  if (c->a != 0) {
    cmp16(c, c->a, HITS_WAIT_TAKEN_AWAY);
    if (c->a == HITS_WAIT_TAKEN_AWAY) {
      PORT_COVER(hits_wait_gone);
      did->end = HITS_WAIT_GONE;
      c->pc = HITS_WAIT_GONE_PC;
      return;
    }
    set_field(w, c, HITS_WAIT_DP_HIT_BY, 0);
    c->a = field(w, c, HITS_WAIT_DP_HITS);
    set_nz16(c, c->a);
    if ((c->a & 0x8000u) != 0) {
      PORT_COVER(hits_wait_broken);
      did->end = HITS_WAIT_BROKEN;
      c->pc = HITS_WAIT_BROKEN_PC;
      return;
    }
    PORT_COVER(hits_wait_hit);
    did->end = HITS_WAIT_HIT;
  } else {
    PORT_COVER(hits_wait_quiet);
    did->end = HITS_WAIT_QUIET;
  }
  c->a = HITS_WAIT_TICKS;
  set_nz16(c, c->a);
  c->pc = HITS_WAIT_YIELD_PC;
}
