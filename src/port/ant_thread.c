// The giant ant's thread, between its calls -- see port/ant_thread.h.

#include "port/ant_thread.h"

#include "port/coverage.h"
#include "port/ant.h"
#include "port/oam.h"

const AntThread ANT_THREADS_BY[ANT_THREAD_COUNT] = {
#define X(at, loaded, handler, sleep, woke, forgets, turned, heard, \
          untouchable, end) \
  [ANT_THREAD_AT_##at] = {loaded, handler, sleep, woke, forgets, turned,     \
                          heard, untouchable, end},
    ANT_THREADS(X)
#undef X
};

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void load_a(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

void ant_loaded(Wram* w, PortCpu* c, const AntThread* t) {
  set_c(c, false);
  wram_w16(w, W_SPAWN_LOAD,
           adc16(c, wram_r16(w, W_SPAWN_LOAD), ANT_WEIGHT));
  c->a = t->handler;
  c->y = ANT_THREAD_BANK;
  set_nz16(c, c->y);
  c->pc = ant_handler_call_pc(t);
}

void ant_sleeps(PortCpu* c, const AntThread* t) {
  load_a(c, 1);
  c->pc = ant_sleep_call_pc(t);
}

void ant_woke(Wram* w, PortCpu* c, const AntThread* t) {
  if (t->forgets) wram_w16(w, (uint16_t)(c->d + ANT_DP_FORGOTTEN), 0);
  // An `RTS` goes to one past what it pulls, so each address is one short.
  push16(w, c, (uint16_t)(ant_where_pc(t) - 1));
  load_a(c, (uint16_t)(field(w, c, ANT_DP_STATE) - 1));
  push16(w, c, c->a);
  c->pc = ant_state_pc(t);
}

AntFate ant_turned(const Wram* w, PortCpu* c, const AntThread* t,
                   bool* holding) {
  const uint16_t leave = field(w, c, ANT_DP_LEAVE);
  const uint16_t kind = field(w, c, ANT_DP_HELD_KIND);
  *holding = false;

  AntFate fate;
  load_a(c, leave);
  if (leave == 0) {
    PORT_COVER(ant_goes_on);
    c->pc = t->sleep_pc;
    return ANT_GOES_ON;
  }
  c->a = kind;
  cmp16(c, kind, ANT_FATAL_KIND_A);
  if (kind == ANT_FATAL_KIND_A) {
    fate = ANT_HELD_FATAL_A;
  } else {
    cmp16(c, kind, ANT_FATAL_KIND_B);
    if (kind == ANT_FATAL_KIND_B) {
      fate = ANT_HELD_FATAL_B;
    } else {
      load_a(c, leave);
      if ((leave & 0x8000u) == 0) {
        PORT_COVER(ant_leaves);
        c->pc = t->end_pc;
        return ANT_LEAVES;
      }
      fate = ANT_TOLD;
    }
  }

  PORT_COVER(ant_killed);
  const uint16_t held = field(w, c, ANT_DP_CARRIED);
  c->a = held;
  cmp16(c, held, ANT_CARRY_NONE);
  *holding = held != ANT_CARRY_NONE;
  c->pc = *holding ? ant_drop_pc(t) : ant_dropped_pc(t);
  return fate;
}

void ant_dropped(Wram* w, PortCpu* c, const AntThread* t) {
  c->y = field(w, c, ANT_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), ANT_META_BANK);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), 0);
  load_a(c, 0);
  if (t->untouchable) {
    // No handler: the address and its bank are both nothing.
    c->y = 0;
    c->pc = ant_untouch_pc(t);
    return;
  }
  load_a(c, ANT_KILLED_SFX);
  c->pc = ant_sound_pc(t);
}

void ant_untouched(PortCpu* c, const AntThread* t) {
  load_a(c, ANT_KILLED_SFX);
  c->pc = ant_sound_pc(t);
}

bool ant_ends(Wram* w, PortCpu* c, const AntThread* t) {
  const uint16_t load = wram_r16(w, W_SPAWN_LOAD);
  if ((uint16_t)(load - ANT_WEIGHT) & 0x8000u) return false;
  set_c(c, true);
  wram_w16(w, W_SPAWN_LOAD, sbc16(c, load, ANT_WEIGHT));
  load_a(c, field(w, c, ANT_DP_RECORD));
  c->pc = ant_free_pc(t);
  return true;
}

bool ant_freed(const Wram* w, PortCpu* c, const AntThread* t) {
  const uint16_t held = field(w, c, ANT_DP_CARRIED);
  c->a = held;
  cmp16(c, held, ANT_CARRY_NONE);
  const bool holding = held != ANT_CARRY_NONE;
  if (holding) PORT_COVER(ant_frees_held);
  c->pc = holding ? ant_free_held_pc(t) : ant_over_pc(t);
  return holding;
}

bool ant_has_room(PortCpu* c) {
  const bool room = !flag(c, PORT_P_C);
  c->pc = room ? ANT_C321_BEGIN_PC : ANT_C321_NO_ROOM_PC;
  return room;
}
