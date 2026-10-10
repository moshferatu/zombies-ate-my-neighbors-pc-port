// The big monster's thread, between its calls -- see port/monster_thread.h.

#include "port/monster_thread.h"

#include "port/coverage.h"
#include "port/monster.h"
#include "port/oam.h"

const MonsterThread MONSTER_THREADS_BY[MONSTER_THREAD_COUNT] = {
#define X(at, loaded, handler, sleep, woke, forgets, turned, heard, \
          untouchable, end) \
  [MONSTER_THREAD_AT_##at] = {loaded, handler, sleep, woke, forgets, turned, \
                              heard, untouchable, end},
    MONSTER_THREADS(X)
#undef X
};

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void load_a(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

void monster_loaded(Wram* w, PortCpu* c, const MonsterThread* t) {
  set_c(c, false);
  wram_w16(w, W_SPAWN_LOAD,
           adc16(c, wram_r16(w, W_SPAWN_LOAD), MONSTER_WEIGHT));
  c->a = t->handler;
  c->y = MONSTER_THREAD_BANK;
  set_nz16(c, c->y);
  c->pc = monster_handler_call_pc(t);
}

void monster_sleeps(PortCpu* c, const MonsterThread* t) {
  load_a(c, 1);
  c->pc = monster_sleep_call_pc(t);
}

void monster_woke(Wram* w, PortCpu* c, const MonsterThread* t) {
  if (t->forgets) wram_w16(w, (uint16_t)(c->d + MONSTER_DP_FORGOTTEN), 0);
  // An `RTS` goes to one past what it pulls, so each address is one short.
  push16(w, c, (uint16_t)(monster_where_pc(t) - 1));
  load_a(c, (uint16_t)(field(w, c, MONSTER_DP_STATE) - 1));
  push16(w, c, c->a);
  c->pc = monster_state_pc(t);
}

MonsterFate monster_turned(const Wram* w, PortCpu* c, const MonsterThread* t,
                           bool* holding) {
  const uint16_t leave = field(w, c, MONSTER_DP_LEAVE);
  const uint16_t kind = field(w, c, MONSTER_DP_HELD_KIND);
  *holding = false;

  MonsterFate fate;
  load_a(c, leave);
  if (leave == 0) {
    PORT_COVER(monster_goes_on);
    c->pc = t->sleep_pc;
    return MONSTER_GOES_ON;
  }
  c->a = kind;
  cmp16(c, kind, MONSTER_FATAL_KIND_A);
  if (kind == MONSTER_FATAL_KIND_A) {
    fate = MONSTER_HELD_FATAL_A;
  } else {
    cmp16(c, kind, MONSTER_FATAL_KIND_B);
    if (kind == MONSTER_FATAL_KIND_B) {
      fate = MONSTER_HELD_FATAL_B;
    } else {
      load_a(c, leave);
      if ((leave & 0x8000u) == 0) {
        PORT_COVER(monster_leaves);
        c->pc = t->end_pc;
        return MONSTER_LEAVES;
      }
      fate = MONSTER_TOLD;
    }
  }

  PORT_COVER(monster_killed);
  const uint16_t held = field(w, c, MONSTER_DP_CARRIED);
  c->a = held;
  cmp16(c, held, MONSTER_CARRY_NONE);
  *holding = held != MONSTER_CARRY_NONE;
  c->pc = *holding ? monster_drop_pc(t) : monster_dropped_pc(t);
  return fate;
}

void monster_dropped(Wram* w, PortCpu* c, const MonsterThread* t) {
  c->y = field(w, c, MONSTER_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), MONSTER_META_BANK);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), 0);
  load_a(c, 0);
  if (t->untouchable) {
    // No handler: the address and its bank are both nothing.
    c->y = 0;
    c->pc = monster_untouch_pc(t);
    return;
  }
  load_a(c, MONSTER_KILLED_SFX);
  c->pc = monster_sound_pc(t);
}

void monster_untouched(PortCpu* c, const MonsterThread* t) {
  load_a(c, MONSTER_KILLED_SFX);
  c->pc = monster_sound_pc(t);
}

bool monster_ends(Wram* w, PortCpu* c, const MonsterThread* t) {
  const uint16_t load = wram_r16(w, W_SPAWN_LOAD);
  if ((uint16_t)(load - MONSTER_WEIGHT) & 0x8000u) return false;
  set_c(c, true);
  wram_w16(w, W_SPAWN_LOAD, sbc16(c, load, MONSTER_WEIGHT));
  load_a(c, field(w, c, MONSTER_DP_RECORD));
  c->pc = monster_free_pc(t);
  return true;
}

bool monster_freed(const Wram* w, PortCpu* c, const MonsterThread* t) {
  const uint16_t held = field(w, c, MONSTER_DP_CARRIED);
  c->a = held;
  cmp16(c, held, MONSTER_CARRY_NONE);
  const bool holding = held != MONSTER_CARRY_NONE;
  if (holding) PORT_COVER(monster_frees_held);
  c->pc = holding ? monster_free_held_pc(t) : monster_over_pc(t);
  return holding;
}

bool monster_has_room(PortCpu* c) {
  const bool room = !flag(c, PORT_P_C);
  c->pc = room ? MONSTER_C321_BEGIN_PC : MONSTER_C321_NO_ROOM_PC;
  return room;
}
