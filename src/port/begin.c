// $81:8000, $81:87BE and $83:A13E  a display record begun -- see
// port/begin.h.

#include "port/begin.h"

#include "port/coverage.h"
#include "port/thread.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// A free record at the page's place, on the ground. False with none free.
static bool take(Wram* w, PortCpu* c, BeginWork* k, uint16_t* record) {
  SlotAllocRegs slot;
  actor_slot_alloc(w, c->db, &slot);
  if (slot.c) {
    k->declined = true;
    return false;
  }
  k->record = slot.a;
  *record = slot.a;
  set_c(c, slot.c);
  c->x = slot.x;
  c->y = slot.y;
  set_field(w, c, BEGIN_DP_RECORD, slot.a);
  wram_w16(w, (uint16_t)(slot.a + ACTOR_X), field(w, c, BEGIN_DP_PLACE_X));
  wram_w16(w, (uint16_t)(slot.a + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(slot.a + ACTOR_Y), field(w, c, BEGIN_DP_PLACE_Y));
  return true;
}

static void drawn(Wram* w, uint16_t record) {
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));
}

void record_begin(Wram* w, PortCpu* c, BeginWork* k) {
  uint16_t record;
  if (!take(w, c, k, &record)) return;
  PORT_COVER(record_begun);
  c->x = record;
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  c->a = wram_r16(w, W_SCHED_CUR_TASK);
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), c->a);
  c->pc = RECORD_BEGIN_RTL_PC;
}

void zombie_begin(Wram* w, PortCpu* c, BeginWork* k) {
  record_begin(w, c, k);
  if (k->declined) return;
  PORT_COVER(zombie_begun);
  const uint16_t record = k->record;
  set_field(w, c, 0x0c, 0);
  set_field(w, c, 0x16, field(w, c, BEGIN_DP_PLACE_X));
  set_field(w, c, 0x18, field(w, c, BEGIN_DP_PLACE_Y));
  wram_w16(w, (uint16_t)(record + ACTOR_META), ZOMBIE_BEGIN_PICTURE);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), ZOMBIE_BEGIN_META_BANK);
  drawn(w, record);
  wram_w16(w, (uint16_t)(record + ACTOR_ATTR), ZOMBIE_BEGIN_ATTR);
  set_field(w, c, 0x22, 0);
  set_field(w, c, 0x7e, 0);
  c->a = ZOMBIE_BEGIN_ATTR;
  set_nz16(c, c->a);
  c->y = record;
  c->pc = ZOMBIE_BEGIN_RTS_PC;
}

void neighbour_begin(Wram* w, PortCpu* c, BeginWork* k) {
  const uint16_t picture = c->a, bank = c->x;
  uint16_t record;
  if (!take(w, c, k, &record)) return;
  PORT_COVER(neighbour_begun);
  set_field(w, c, 0x20, field(w, c, BEGIN_DP_PLACE_X));
  set_field(w, c, 0x22, 0);
  set_field(w, c, 0x24, field(w, c, BEGIN_DP_PLACE_Y));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), bank);
  wram_w16(w, (uint16_t)(record + ACTOR_META), picture);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), NEIGHBOUR_COLLIDE_ID);
  drawn(w, record);
  set_field(w, c, 0x14, 0);
  set_field(w, c, 0x10, 0);
  set_field(w, c, 0x0c, 0);
  set_field(w, c, 0x1e, 0);
  set_field(w, c, 0x26, 0);
  c->a = NEIGHBOUR_HANDLER;
  c->y = NEIGHBOUR_HANDLER_BANK;
  thread_set_handler(w, c);
  c->pc = NEIGHBOUR_BEGIN_RTS_PC;
}

// ---------------------------------------------------------------------------
// ...and ended
// ---------------------------------------------------------------------------

const RecordEnd RECORD_ENDS[RECORD_END_COUNT] = {
    [RECORD_END_ZOMBIE_SLOW] = {0x81885au, 0x818868u, 0x0014, 0x08},
    [RECORD_END_ZOMBIE_FAST] = {0x818944u, 0x818952u, 0x0014, 0x08},
    [RECORD_END_ZOMBIE_THIRD] = {0x818c7eu, 0x818c8cu, 0x0014, 0x08},
    [RECORD_END_SLIME_GLOB] = {0x81cf39u, 0x81cf47u, 0x0004, 0x08},
    [RECORD_END_SHOT_5] = {0x81ec60u, 0x81ec6eu, 0x0006, 0x0a},
    [RECORD_END_MARTIAN] = {0x819a17u, 0x819a25u, 0x001e, 0x08, true},
    [RECORD_END_MARTIAN_ARRIVAL] = {0x819a7bu, 0x819a89u, 0x001e, 0x08},
    [RECORD_END_FISHMAN_SPLASH] = {0x81e775u, 0x81e783u, 0x0007, 0x08},
    [RECORD_END_WEREWOLF] = {0x81ac67u, 0x81ac75u, 0x001c, 0x08},
    [RECORD_END_SLIME] = {0x81cd0cu, 0x81cd1au, 0x0020, 0x08},
    [RECORD_END_WEED] = {0x81d2ceu, 0x81d2dcu, 0x0023, 0x08},
#define X(at, sym, pc, load, record_at, calls) \
  [RECORD_END_AT_##at] = {pc, pc + 14, load, record_at, calls},
    RECORD_ENDS_BY_ADDRESS(X)
#undef X
};

bool record_end(Wram* w, PortCpu* c, const RecordEnd* end, int* place) {
  set_c(c, true);
  const uint16_t load = sbc16(c, wram_r16(w, W_SPAWN_LOAD), end->load);
  if (load & 0x8000u) return false;
  const uint16_t record = field(w, c, end->record_at);
  if (record < W_ACTOR_SLOTS || record > ACTOR_SLOT_LAST) return false;
  *place = actor_list_place(w, record);
  if (*place == -3) return false;
  if (wram_r16(w, (uint16_t)(c->s + 1)) != (RECORD_END_EXITED_PC & 0xffffu) - 1 ||
      wram_r8(w, (uint16_t)(c->s + 3)) != (RECORD_END_EXITED_PC >> 16))
    return false;
  PORT_COVER(record_ended);
  wram_w16(w, W_SPAWN_LOAD, load);

  // `JML actor_slot_free`: its `RTL` is the thread's.
  SlotFreeRegs r;
  actor_slot_free(w, record, c->d, c->x, c->y, &r);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  if (r.n) c->p |= PORT_P_N;
  if (r.z) c->p |= PORT_P_Z;
  set_c(c, r.c);
  c->s = (uint16_t)(c->s + 3);
  c->pc = RECORD_END_EXITED_PC;
  return true;
}

bool death_pictures(Wram* w, PortCpu* c) {
  if (c->x != DEATH_KILLED) return false;
  PORT_COVER(death_pictures_begun);
  cmp16(c, c->x, DEATH_KILLED);
  push16(w, c, c->a);  // the list
  push16(w, c, c->y);  // its bank
  c->a = DEATH_SFX;
  set_nz16(c, c->a);
  c->pc = DEATH_PICTURES_SOUND_PC;
  return true;
}

void death_pictures_heard(Wram* w, PortCpu* c) {
  PORT_COVER(death_pictures_heard);
  const uint16_t bank = pull16(w, c);
  const uint16_t record = field(w, c, BEGIN_DP_RECORD);
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), bank);
  c->x = record;
  c->y = bank;
  c->a = pull16(w, c);
  set_nz16(c, c->a);
  c->pc = DEATH_PICTURES_PLAY_PC;
}
