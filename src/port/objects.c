// What a level leaves on the ground, and the neighbours' list -- see
// port/objects.h.

#include "port/objects.h"

#include "port/coverage.h"
#include "port/thread.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// A word of a table in the thread's own data bank.
static uint16_t table_word(const Wram* w, const Rom* rom, const PortCpu* c,
                           uint16_t at) {
  return bus_r16(w, rom, ((uint32_t)c->db << 16) | at);
}

static bool is_record(uint16_t at) {
  return at >= W_ACTOR_SLOTS && at <= ACTOR_SLOT_LAST &&
         (at - W_ACTOR_SLOTS) % ACTOR_SLOT_STRIDE == 0;
}

// Where in the display list a record is: the ROM's unlink walks that far.
// -1 for one that is not this thread's, -2 for one not in use, -3 for one
// the list does not hold.
static int place_in_list(const Wram* w, uint16_t record) {
  if (wram_r16(w, W_SCHED_CUR_TASK) !=
      wram_r16(w, (uint16_t)(record + ACTOR_THREAD)))
    return -1;
  if (!(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) & ACTOR_ACTIVE))
    return -2;
  int place = 0;
  for (uint16_t at = wram_r16(w, W_ACTOR_LIST_HEAD); at != record;
       at = wram_r16(w, (uint16_t)(at + ACTOR_NEXT))) {
    if (!is_record(at) || ++place > ACTOR_SLOT_COUNT) return -3;
  }
  return place;
}

// One fewer on the level's load. Below nothing the ROM stops for good, which
// is its own to do.
static bool load_less(Wram* w, PortCpu* c) {
  set_c(c, true);
  c->a = sbc16(c, wram_r16(w, W_SPAWN_LOAD), 1);
  wram_w16(w, W_SPAWN_LOAD, c->a);
  return (c->a & 0x8000u) == 0;
}

void object_list_parse(Wram* w, const Rom* rom, PortCpu* c, ObjectsWork* k) {
  const uint32_t list =
      field(w, c, OBJECT_DP_LIST) |
      ((uint32_t)wram_r8(w, (uint16_t)(c->d + OBJECT_DP_LIST + 2)) << 16);
  uint16_t entry = 0;
  for (uint32_t at = list;; at += OBJECT_ENTRY_BYTES) {
    const uint16_t x = bus_r16(w, rom, at & 0xffffffu);
    if (x == 0) break;
    if (k->entries == OBJECT_SLOT_COUNT - 1) {  // no room for the end's word
      k->declined = true;
      return;
    }
    PORT_COVER(object_parsed);
    wram_w16(w, (uint32_t)(W_OBJECT_X + entry), x);
    wram_w16(w, (uint32_t)(W_OBJECT_Y + entry),
             bus_r16(w, rom, (at + 2) & 0xffffffu));
    wram_w16(w, (uint16_t)(W_OBJECT_TYPE + entry),
             bus_r8(w, rom, (at + 4) & 0xffffffu));
    wram_w16(w, (uint16_t)(W_OBJECT_STATE + entry), 0);
    entry = (uint16_t)(entry + 2);
    k->entries++;
  }
  PORT_COVER(object_list_ended);
  wram_w16(w, (uint16_t)(W_OBJECT_STATE + entry), OBJECT_ENDED);
  c->a = OBJECT_HANDLER;
  c->y = OBJECT_HANDLER_BANK;
  thread_set_handler(w, c);
  set_field(w, c, OBJECT_DP_COLLECTED, 0);
  c->pc = OBJECT_LIST_PARSE_RTS_PC;
}

void object_spawn(Wram* w, const Rom* rom, PortCpu* c, ObjectsWork* k) {
  set_c(c, false);
  wram_w16(w, W_SPAWN_LOAD, adc16(c, wram_r16(w, W_SPAWN_LOAD), 1));

  SlotAllocRegs slot;
  actor_slot_alloc(w, c->db, &slot);
  if (slot.c) {  // none free, and the ROM goes on with what is no record
    k->declined = true;
    return;
  }
  PORT_COVER(object_spawned);
  k->record = slot.a;
  const uint16_t entry = field(w, c, OBJECT_DP_ENTRY);
  const uint16_t record = slot.y;
  wram_w16(w, (uint16_t)(W_OBJECT_STATE + entry), slot.a);
  wram_w16(w, (uint16_t)(record + ACTOR_X),
           wram_r16(w, (uint32_t)(W_OBJECT_X + entry)));
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y),
           wram_r16(w, (uint32_t)(W_OBJECT_Y + entry)));
  const uint16_t type = wram_r16(w, (uint16_t)(W_OBJECT_TYPE + entry));
  wram_w16(w, (uint16_t)(record + ACTOR_META),
           table_word(w, rom, c, (uint16_t)(OBJECT_PICTURES + type)));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), OBJECT_META_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD),
           wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID),
           table_word(w, rom, c, (uint16_t)(OBJECT_COLLIDE_IDS + type)));
  c->a = (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) | ACTOR_DRAW);
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), c->a);
  set_nz16(c, c->a);
  set_c(c, slot.c);
  c->x = type;
  c->y = record;
  c->pc = OBJECT_SPAWN_RTS_PC;
}

void object_free(Wram* w, PortCpu* c, ObjectsWork* k) {
  if (!load_less(w, c)) {
    k->declined = true;
    return;
  }
  const uint16_t state = (uint16_t)(W_OBJECT_STATE + c->x);
  const uint16_t record = wram_r16(w, state);
  wram_w16(w, state, 0);
  k->place[0] = is_record(record) ? place_in_list(w, record) : -3;
  if (k->place[0] == -3) {
    k->declined = true;
    return;
  }
  PORT_COVER(object_freed);
  SlotFreeRegs r;
  actor_slot_free(w, record, c->d, c->x, c->y, &r);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  c->p = (uint8_t)((c->p & ~(PORT_P_N | PORT_P_Z | PORT_P_C)) |
                   (r.n ? PORT_P_N : 0) | (r.z ? PORT_P_Z : 0) |
                   (r.c ? PORT_P_C : 0));
  c->pc = OBJECT_FREE_RTS_PC;
}

void object_collect(Wram* w, PortCpu* c, ObjectsWork* k) {
  for (;;) {
    if (k->collected == OBJECT_COLLECT_MAX) {
      k->declined = true;
      return;
    }
    const uint16_t left = field(w, c, OBJECT_DP_COLLECTED);
    const uint16_t record =
        wram_r16(w, (uint16_t)(c->d + OBJECT_DP_COLLECTED + left));

    // Which entry has it?
    uint16_t entry = 0;
    while (wram_r16(w, (uint16_t)(W_OBJECT_STATE + entry)) != record) {
      entry = (uint16_t)(entry + 2);
      if (entry == OBJECT_SLOT_COUNT * 2) {  // none: the ROM looks on and on
        k->declined = true;
        return;
      }
    }
    k->scanned[k->collected] = entry / 2;
    wram_w16(w, (uint16_t)(W_OBJECT_STATE + entry), OBJECT_RETIRED);

    const int place = is_record(record) ? place_in_list(w, record) : -3;
    if (place == -3) {
      k->declined = true;
      return;
    }
    k->place[k->collected++] = place;
    SlotFreeRegs r;
    actor_slot_free(w, record, c->d, entry, record, &r);
    c->x = r.x;
    c->y = r.y;
    if (!load_less(w, c)) {
      k->declined = true;
      return;
    }

    const uint16_t now = (uint16_t)(left - 2);
    set_field(w, c, OBJECT_DP_COLLECTED, now);
    set_nz16(c, now);
    if (now == 0) break;
    PORT_COVER(object_collect_another);
  }
  PORT_COVER(object_collected);
  c->pc = OBJECT_COLLECT_RTS_PC;
}

void victim_list_parse(Wram* w, const Rom* rom, PortCpu* c, VictimsWork* k) {
  set_field(w, c, VICTIM_DP_LIST + 2, c->a);
  set_field(w, c, VICTIM_DP_LIST, c->x);
  wram_w16(w, W_VICTIM_COUNT, 0);
  const uint32_t bank = (uint32_t)wram_r8(w, (uint16_t)(c->d + VICTIM_DP_LIST + 2))
                        << 16;
  uint16_t place = 0;
  for (;;) {
    const uint16_t at = field(w, c, VICTIM_DP_LIST);
    const uint16_t gate =
        bus_r16(w, rom, (bank + at + VICTIM_ENTRY_GATE) & 0xffffffu);
    if (gate == 0) {
      PORT_COVER(victim_list_ended);
      k->ended_zero = true;
      break;
    }
    cmp16(c, gate, wram_r16(w, W_VICTIM_GATE));
    if (gate > wram_r16(w, W_VICTIM_GATE)) {
      PORT_COVER(victim_list_gated);
      k->ended_past = true;
      break;
    }
    if (k->entries == VICTIM_PLACES_MAX) {  // the next would be over the count
      k->declined = true;
      return;
    }
    PORT_COVER(victim_parsed);
    k->entries++;
    if (gate == wram_r16(w, W_VICTIM_GATE)) k->equal++;
    wram_w16(w, W_VICTIM_COUNT, (uint16_t)(wram_r16(w, W_VICTIM_COUNT) + 1));
    wram_w16(w, (uint32_t)(W_VICTIM_X + place),
             bus_r16(w, rom, (bank + at) & 0xffffffu));
    wram_w16(w, (uint32_t)(W_VICTIM_Y + place),
             bus_r16(w, rom, (bank + at + 2) & 0xffffffu));
    set_c(c, false);
    set_field(w, c, VICTIM_DP_LIST, adc16(c, at, VICTIM_ENTRY_BYTES));
    place = (uint16_t)(place + 4);
  }
  wram_w16(w, W_VICTIM_WORD_A, 0);
  wram_w16(w, W_VICTIM_WORD_B, 0);
  c->a = 0;
  set_nz16(c, c->a);
  c->x = place;
  c->y = VICTIM_ENTRY_GATE;
  c->pc = VICTIM_LIST_PARSE_DONE_PC;
}

void victim_tables_clear(Wram* w, PortCpu* c) {
  PORT_COVER(victim_tables_cleared);
  for (uint32_t i = 0; i < VICTIM_TABLE_BYTES; i++) {
    wram_w8(w, W_VICTIM_SPAWNED + i, 0);
    wram_w8(w, W_VICTIM_THREAD + i, 0);
  }
  c->a = 0;
  c->x = 0xfffe;
  set_nz16(c, c->x);
  c->pc = VICTIM_TABLES_CLEAR_RTL_PC;
}

void victim_start(Wram* w, const Rom* rom, PortCpu* c, VictimsWork* k) {
  set_field(w, c, VICTIM_DP_ARG_X, field(w, c, VICTIM_DP_X));
  set_field(w, c, VICTIM_DP_ARG_Y, field(w, c, VICTIM_DP_Y));
  const uint16_t entry = field(w, c, VICTIM_DP_ENTRY);
  set_field(w, c, VICTIM_DP_ARG_ENTRY, entry);
  // Twelve bytes an entry: four times, and eight times.
  const uint16_t four = asl16(c, asl16(c, entry));
  set_field(w, c, VICTIM_DP_SCRATCH, four);
  const uint16_t eight = asl16(c, four);
  set_c(c, false);
  const uint16_t at =
      (uint16_t)(field(w, c, VICTIM_DP_START_LIST) + adc16(c, eight, four));
  const uint32_t bank = (uint32_t)c->db << 16;
  set_field(w, c, VICTIM_DP_ARG_PARAM,
            bus_r16(w, rom, bank | (uint16_t)(at + VICTIM_ENTRY_PARAM)));

  const uint16_t gate =
      bus_r16(w, rom, bank | (uint16_t)(at + VICTIM_ENTRY_GATE));
  const uint16_t level = wram_r16(w, W_VICTIM_GATE);
  if (gate & 0x8000u) {
    PORT_COVER(victim_ungated);
    k->ungated = true;
  } else if (gate == level) {
    PORT_COVER(victim_at_the_gate);
    k->at_gate = true;
  } else if (gate > level) {  // not this level's: the ROM's to retire
    k->declined = true;
    return;
  } else {
    PORT_COVER(victim_within_the_gate);
  }

  const uint16_t thread =
      bus_r16(w, rom, bank | (uint16_t)(at + VICTIM_ENTRY_THREAD));
  const uint16_t thread_bank =
      bus_r16(w, rom, bank | (uint16_t)(at + VICTIM_ENTRY_BANK));
  k->slot = thread_spawn(w, rom, thread, thread_bank, c->d);
  if (k->slot < 0) {  // no thread free; it comes back as slot nothing
    k->declined = true;
    return;
  }
  // The slot's low byte, and that the entry is started. A's high byte is
  // the slot's, which is nothing.
  wram_w8(w, (uint32_t)(W_VICTIM_THREAD + entry), (uint8_t)k->slot);
  wram_w8(w, (uint32_t)(W_VICTIM_SPAWNED + entry), 1);
  c->a = (uint16_t)((k->slot & 0xff00) | 1);
  c->p = (uint8_t)(c->p & ~(PORT_P_N | PORT_P_Z));
  c->x = entry;
  c->y = (THREAD_SPAWN_ARGS - 1) * 2;
  c->pc = VICTIM_START_RTS_PC;
}
