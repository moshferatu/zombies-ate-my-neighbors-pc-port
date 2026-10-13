// The neighbours' threads, between their calls -- see port/neighbour_thread.h.

#include "port/neighbour_thread.h"

#include <stddef.h>

#include "port/bcd.h"
#include "port/begin.h"       // NEIGHBOUR_HANDLER
#include "port/bodies.h"      // W_NEIGHBOURS_LEFT
#include "port/coverage.h"
#include "port/jumper.h"
#include "port/neighbours.h"
#include "port/oam.h"         // the display record's fields
#include "port/stepper.h"

const NeighbourStart NEIGHBOUR_STARTS[NEIGHBOUR_START_COUNT] = {
    {0x839699u, 0x000e, 0x0002, 0xe04d, 0x008f},
    {0x839776u, 0x0006, 0x0004, 0xe653, 0x008f},
    {0x83993du, 0x000e, 0x0008, 0xdd74, 0x008f},
    {0x839a89u, 0x000e, 0x0014, 0xf78a, 0x008f},
    {0x839c6du, 0x000a, 0x000a, 0xdfea, 0x008f},
    {0x839d00u, 0x000e, 0x000c, 0xe3c7, 0x008f},
    {0x839e15u, 0x000a, 0x000e, 0xe789, 0x008f},
    {0x839ebeu, 0x000a, 0x0010, 0xdf8f, 0x008f},
    {0x839fe2u, 0x000e, 0x0012, 0xe5c5, 0x0090},
};

const NeighbourLoopStart NEIGHBOUR_LOOP_STARTS[NEIGHBOUR_LOOP_START_COUNT] = {
    {0x8396b1u, 0x8396bcu, false, false},
    {0x83978eu, 0x839799u, false, false},
    {0x839955u, 0x839965u, true, false},
    {0x839aa1u, 0x839ab3u, true, true},
    {0x839c85u, 0x839c90u, false, false},
    {0x839e2du, 0x839e38u, false, false},
    {0x839ffau, 0x83a005u, false, false},
};

const NeighbourTold NEIGHBOUR_TOLDS[NEIGHBOUR_TOLD_COUNT] = {
    {0x8396e1u, 0x8396bcu, 0xe3ae, 0x0105, 0xe04d},
    {0x8397beu, 0x839799u, 0xe778, 0x0700, 0xe653},
    {0x839cafu, 0x839c90u, 0xe044, 0x0010, 0x0000},
    {0x839e5du, 0x839e38u, 0xe81c, 0x0500, 0xe789},
    {0x83a035u, 0x83a005u, 0xe7c6, 0x0200, 0xe5c5},
};

const WatcherTold WATCHER_TOLDS[WATCHER_TOLD_COUNT] = {
    {0x8399cau, 0x839965u, 0x8399a2u, 0xdf75, 0x0100, 0xde1f},
    {0x839b1eu, 0x839ab3u, 0x839af7u, 0xf80e, 0x0500, 0xf78a},
};

const NeighbourArgument NEIGHBOUR_ARGUMENTS[NEIGHBOUR_ARGUMENT_COUNT] = {
    {0x83a1acu, 0x0000},              // lost: `$81:8294` is told so
    {0x83a1c6u, NEIGHBOUR_SFX_LOST},
    {0x83a1cdu, 0x0001},
    {0x83a0cbu, 0x0001},              // a frame for the werewolves
    {0x839d9eu, NEIGHBOUR_SFX_LOST},
    {0x839da5u, 0x9de3},              // the stepper's last pictures
    {0x8398eau, BOUNCER_SFX},
};

static uint16_t page(const Wram* w, const PortCpu* c, uint8_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_page(Wram* w, const PortCpu* c, uint8_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

static void lda(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static void ldx(PortCpu* c, uint16_t v) {
  c->x = v;
  set_nz16(c, v);
}

static void ldy(PortCpu* c, uint16_t v) {
  c->y = v;
  set_nz16(c, v);
}

static void ran(NeighbourThreadWork* k, int block) { k->blocks[block]++; }

// `CMP # : BEQ`.
static bool is(PortCpu* c, uint16_t value, NeighbourThreadWork* k) {
  cmp16(c, c->a, value);
  ran(k, NT_CMP_BRANCH);
  if (c->a != value) return false;
  ran(k, NT_TAKEN);
  return true;
}

// `LDA $1E` and a branch after it, which the caller counts if it is taken.
static uint16_t told(const Wram* w, PortCpu* c, NeighbourThreadWork* k) {
  lda(c, page(w, c, NEIGHBOUR_DP_TOLD));
  ran(k, NT_TOLD_TEST);
  return c->a;
}

// `LDA #` and the call after it.
static void argument(PortCpu* c, uint16_t value, uint32_t call_pc,
                     NeighbourThreadWork* k) {
  lda(c, value);
  ran(k, NT_LDA_IMM);
  c->pc = call_pc;
}

// `LDA #picture : LDX #award : JSR $A1D5`.
static void save(PortCpu* c, uint16_t picture, uint16_t award, uint32_t call_pc,
                 NeighbourThreadWork* k) {
  c->a = picture;
  ldx(c, award);
  ran(k, NT_SAVED);
  c->pc = call_pc;
}

// `LDY $08 : LDA #picture : STA $0008,Y : JSR $A2B4`.
static void show_for_sign(Wram* w, PortCpu* c, uint16_t picture,
                          NeighbourThreadWork* k) {
  c->y = page(w, c, NEIGHBOUR_DP_RECORD);
  lda(c, picture);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), c->a);
  ran(k, NT_SIGN);
}

// `STZ $10 : LDA #$0001 : STA $1A`: the first picture again, for a frame.
static void first_picture(Wram* w, PortCpu* c, NeighbourThreadWork* k) {
  set_page(w, c, NEIGHBOUR_DP_WHICH, 0);
  lda(c, 0x0001);
  set_page(w, c, NEIGHBOUR_DP_FRAMES, c->a);
  ran(k, NT_RESET);
}

// `LDA $1A` and the loop's sleep.
static void sleep_again(const Wram* w, PortCpu* c, uint32_t sleep_pc,
                        NeighbourThreadWork* k) {
  lda(c, page(w, c, NEIGHBOUR_DP_FRAMES));
  ran(k, NT_LDA_DP);
  c->pc = sleep_pc;
}

// The level's load up by what a neighbour weighs, its kind, and what
// `neighbour_begin` is called with.
static void enter(Wram* w, PortCpu* c, uint16_t load, uint16_t kind,
                  uint16_t picture, uint16_t bank) {
  set_c(c, false);
  wram_w16(w, W_SPAWN_LOAD, adc16(c, wram_r16(w, W_SPAWN_LOAD), load));
  set_page(w, c, NEIGHBOUR_DP_KIND, kind);
  c->a = picture;
  ldx(c, bank);
}

bool neighbour_thread_needs_met(const Wram* w, const Rom* rom,
                                NeighbourThreadNeeds needs, const PortCpu* c) {
  const bool record = page(w, c, NEIGHBOUR_DP_RECORD) < 0x1f00;
  switch (needs) {
    case NT_NEEDS_NOTHING:
      return true;
    case NT_NEEDS_RECORD:
      return record;
    case NT_NEEDS_A:
      return c->a < 0x1f00;
    case NT_NEEDS_Y:
      return c->y < 0x1f00;
    case NT_NEEDS_KIND: {
      const uint16_t kind = page(w, c, NEIGHBOUR_DP_KIND);
      return c->y < 0x1f00 && kind >= 0x0002 && kind <= NEIGHBOUR_KIND_LAST &&
             (kind & 1) == 0;
    }
    case NT_NEEDS_MAT:
      return page(w, c, JUMPER_DP_MAT) < 0x1f00;
    case NT_NEEDS_LOAD:
      return wram_r16(w, W_SPAWN_LOAD) >= JUMPER_LOAD &&
             wram_r16(w, W_SPAWN_LOAD) < 0x8000u + JUMPER_LOAD;
    case NT_NEEDS_PICTURE:
      return record &&
             page(w, c, NEIGHBOUR_DP_WHICH) < 0x3000;
    case NT_NEEDS_SLOT: {
      const uint16_t side = flag(c, PORT_P_N) ? 0x0002 : 0x0000;
      uint16_t slot;
      if (side == wram_r16(w, W_SCORE_SLOT_SIDE)) {
        slot = 0;
      } else if (side == wram_r16(w, W_SCORE_SLOT_SIDE + 2)) {
        slot = 2;
      } else {
        return false;  // the table has no third word
      }
      const uint16_t to = bus_r16(w, rom, SAVED_SLOT_JUMPS + slot);
      return to == SAVED_SLOT_A_PC || to == SAVED_SLOT_B_PC;
    }
  }
  return false;
}

// --- how a thread starts -----------------------------------------------------

void neighbour_enter(Wram* w, const Rom* rom, PortCpu* c,
                     NeighbourThreadWork* k) {
  (void)rom;
  for (int i = 0; i < NEIGHBOUR_START_COUNT; i++) {
    const NeighbourStart* start = &NEIGHBOUR_STARTS[i];
    if (start->pc != c->pc) continue;
    enter(w, c, start->load, start->kind, start->picture, start->bank);
    ran(k, NT_ENTER);
    c->pc = start->pc + NEIGHBOUR_START_CALL;
    return;
  }
}

void neighbour_started(Wram* w, const Rom* rom, PortCpu* c,
                       NeighbourThreadWork* k) {
  (void)rom;
  for (int i = 0; i < NEIGHBOUR_LOOP_START_COUNT; i++) {
    const NeighbourLoopStart* start = &NEIGHBOUR_LOOP_STARTS[i];
    if (start->pc != c->pc) continue;
    if (start->patient) {
      PORT_COVER(neighbour_started_patient);
      set_page(w, c, NEIGHBOUR_DP_PATIENCE, NEIGHBOUR_PATIENCE);
      ran(k, NT_PATIENCE);
    }
    set_page(w, c, NEIGHBOUR_DP_TOLD, 0);
    set_page(w, c, NEIGHBOUR_DP_WHICH, 0);
    if (start->clears_34) {
      set_page(w, c, 0x34, 0);
      ran(k, NT_STZ_DP);
    }
    set_page(w, c, NEIGHBOUR_DP_FRAMES, 0x0001);
    lda(c, 0x0001);
    ran(k, NT_STARTED);
    c->pc = start->sleep_pc;
    return;
  }
}

// --- told --------------------------------------------------------------------

void neighbour_told(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  (void)rom;
  const NeighbourTold* t = NULL;
  uint32_t sign_call = 0;
  for (int i = 0; i < NEIGHBOUR_TOLD_COUNT && !t; i++) {
    const NeighbourTold* each = &NEIGHBOUR_TOLDS[i];
    sign_call = each->told_pc + NEIGHBOUR_TOLD_SIGN +
                (each->sign_picture ? 8u : 0u);
    if (c->pc == each->told_pc ||
        c->pc == each->told_pc + NEIGHBOUR_TOLD_TURNED ||
        c->pc == sign_call + 3) {
      t = each;
    }
  }
  if (!t) return;

  if (c->pc != t->told_pc) {
    // Back from the turn or the sign: has it been told anything since?
    PORT_COVER(neighbour_told_back);
    first_picture(w, c, k);
    ran(k, NT_BRA);
    if (told(w, c, k) == 0) {
      PORT_COVER(neighbour_told_nothing_more);
      ran(k, NT_TAKEN);
      sleep_again(w, c, t->sleep_pc, k);
      return;
    }
  }

  ran(k, NT_BRANCH);
  if (flag(c, PORT_P_N)) {
    PORT_COVER(neighbour_told_end);
    ran(k, NT_TAKEN);
    c->pc = t->told_pc + NEIGHBOUR_TOLD_END;
  } else if (is(c, NEIGHBOUR_TOLD_TURN, k)) {
    PORT_COVER(neighbour_told_turn);
    c->pc = t->told_pc + NEIGHBOUR_TOLD_TURN_CALL;
  } else if (is(c, NEIGHBOUR_TOLD_LOST, k)) {
    PORT_COVER(neighbour_told_lost);
    c->pc = t->told_pc + NEIGHBOUR_TOLD_LOST_CALL;
  } else if (is(c, NEIGHBOUR_TOLD_SIGN_UP, k)) {
    PORT_COVER(neighbour_told_sign);
    if (t->sign_picture) show_for_sign(w, c, t->sign_picture, k);
    c->pc = sign_call;
  } else {
    PORT_COVER(neighbour_told_saved);
    save(c, t->saved_picture, t->award, t->told_pc + NEIGHBOUR_TOLD_SAVED_CALL,
         k);
  }
}

// `LDX $06 : LDA $7E605A,X : AND #$00FF : BEQ`: is its entry in the level's
// list anything but zero? An entry of `$FFFF` reads the byte before the
// list in the next bank, as the ROM does.
static bool watcher_listed(const Wram* w, PortCpu* c, NeighbourThreadWork* k) {
  c->x = page(w, c, NEIGHBOUR_DP_ENTRY);
  lda(c, wram_r8(w, (uint32_t)W_VICTIM_SPAWNED + c->x));
  ran(k, NT_THERE);
  if (c->a != 0) return true;
  ran(k, NT_TAKEN);
  return false;
}

void watcher_told(Wram* w, const Rom* rom, PortCpu* c, NeighbourThreadWork* k) {
  (void)rom;
  const WatcherTold* t = NULL;
  for (int i = 0; i < WATCHER_TOLD_COUNT && !t; i++) {
    const WatcherTold* each = &WATCHER_TOLDS[i];
    if (c->pc == each->told_pc ||
        c->pc == each->told_pc + WATCHER_TOLD_SIGNED ||
        c->pc == each->told_pc + WATCHER_TOLD_TURNED) {
      t = each;
    }
  }
  if (!t) return;

  bool lost = false;
  if (c->pc != t->told_pc) {
    // Back from the turn or the sign. Only being lost is looked for.
    PORT_COVER(watcher_told_back);
    first_picture(w, c, k);
    lda(c, page(w, c, NEIGHBOUR_DP_TOLD));
    cmp16(c, c->a, NEIGHBOUR_TOLD_LOST);
    ran(k, NT_TOLD_CMP);
    if (c->a != NEIGHBOUR_TOLD_LOST) {
      ran(k, NT_JMP);
      sleep_again(w, c, t->sleep_pc, k);
      return;
    }
    PORT_COVER(watcher_told_lost_since);
    ran(k, NT_TAKEN);
    lost = true;
  } else {
    ran(k, NT_BRANCH);
    if (flag(c, PORT_P_N)) {
      PORT_COVER(watcher_told_end);
      ran(k, NT_TAKEN);
      c->pc = t->told_pc + WATCHER_TOLD_END;
      return;
    }
    if (is(c, NEIGHBOUR_TOLD_TURN, k)) {
      PORT_COVER(watcher_told_turn);
      c->pc = t->told_pc + WATCHER_TOLD_TURN_CALL;
      return;
    }
    if (is(c, NEIGHBOUR_TOLD_SIGN_UP, k)) {
      PORT_COVER(watcher_told_sign);
      show_for_sign(w, c, t->sign_picture, k);
      c->pc = t->told_pc + WATCHER_TOLD_SIGN_CALL;
      return;
    }
    cmp16(c, c->a, NEIGHBOUR_TOLD_LOST);
    ran(k, NT_CMP_BRANCH);
    if (c->a == NEIGHBOUR_TOLD_LOST) {
      const uint16_t patience =
          (uint16_t)(page(w, c, NEIGHBOUR_DP_PATIENCE) - 1);
      set_page(w, c, NEIGHBOUR_DP_PATIENCE, patience);
      set_nz16(c, patience);
      ran(k, NT_PATIENCE_DOWN);
      if (!(patience & 0x8000u)) {
        PORT_COVER(watcher_told_bore_it);
        set_page(w, c, NEIGHBOUR_DP_TOLD, 0);
        ran(k, NT_STZ_DP);
        ran(k, NT_BRA);
        sleep_again(w, c, t->alarm_sleep_pc, k);
        return;
      }
      PORT_COVER(watcher_told_lost);
      ran(k, NT_TAKEN);
      lost = true;
    } else {
      ran(k, NT_TAKEN);
    }
  }

  if (!watcher_listed(w, c, k)) {
    PORT_COVER(watcher_told_not_listed);
    c->pc = t->told_pc + WATCHER_TOLD_END;
  } else if (lost) {
    c->pc = t->told_pc + WATCHER_TOLD_LOST_CALL;
  } else {
    PORT_COVER(watcher_told_saved);
    save(c, t->saved_picture, t->award, t->told_pc + WATCHER_TOLD_SAVED_CALL,
         k);
  }
}

// --- $83:A18F and $83:A1B4: lost --------------------------------------------

void neighbour_lost(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  (void)rom;
  c->x = page(w, c, NEIGHBOUR_DP_RECORD);
  wram_w16(w, (uint16_t)(c->x + ACTOR_COLLIDE_ID), 0);
  lda(c, page(w, c, NEIGHBOUR_DP_ENTRY));
  ran(k, NT_LOST);
  c->pc = 0x83a196u;
}

// The stepper has cried out already.
void neighbour_lost_counted(Wram* w, const Rom* rom, PortCpu* c,
                            NeighbourThreadWork* k) {
  (void)rom;
  lda(c, page(w, c, NEIGHBOUR_DP_KIND));
  cmp16(c, c->a, NEIGHBOUR_KIND_STEPPER);
  ran(k, NT_LOST_KIND);
  if (c->a == NEIGHBOUR_KIND_STEPPER) {
    PORT_COVER(neighbour_lost_quiet);
    ran(k, NT_TAKEN);
    argument(c, 0x0000, 0x83a1afu, k);
  } else {
    PORT_COVER(neighbour_lost_cry);
    argument(c, NEIGHBOUR_SFX_LOST, 0x83a1a8u, k);
  }
}

void tourists_lost(Wram* w, const Rom* rom, PortCpu* c, NeighbourThreadWork* k) {
  (void)rom;
  c->y = page(w, c, NEIGHBOUR_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), 0);
  lda(c, page(w, c, NEIGHBOUR_DP_ENTRY));
  ran(k, NT_LOST_B);
  c->pc = 0x83a1beu;
}

void neighbour_argument(Wram* w, const Rom* rom, PortCpu* c,
                        NeighbourThreadWork* k) {
  (void)w;
  (void)rom;
  for (int i = 0; i < NEIGHBOUR_ARGUMENT_COUNT; i++) {
    if (NEIGHBOUR_ARGUMENTS[i].pc != c->pc) continue;
    argument(c, NEIGHBOUR_ARGUMENTS[i].value, c->pc + 3, k);
    return;
  }
}

// --- $83:A1D5: saved ---------------------------------------------------------

// A is the picture it rises with and X the award, kept on the stack.
void neighbour_saved(Wram* w, const Rom* rom, PortCpu* c,
                     NeighbourThreadWork* k) {
  (void)rom;
  push16(w, c, c->a);
  push16(w, c, c->x);
  lda(c, page(w, c, NEIGHBOUR_DP_ENTRY));
  ran(k, NT_SAVE_HEAD);
  c->pc = 0x83a1d9u;
}

void neighbour_saved_counted(Wram* w, const Rom* rom, PortCpu* c,
                             NeighbourThreadWork* k) {
  (void)rom;
  c->y = page(w, c, NEIGHBOUR_DP_KIND);
  lda(c, page(w, c, NEIGHBOUR_DP_SAVED_BY));
  ran(k, NT_SAVE_KIND);
  c->pc = 0x83a1e5u;
}

void neighbour_saved_listed(Wram* w, const Rom* rom, PortCpu* c,
                            NeighbourThreadWork* k) {
  (void)rom;
  c->x = pull16(w, c);
  lda(c, page(w, c, NEIGHBOUR_DP_SAVED_BY));
  ran(k, NT_SAVE_AWARD);
  c->pc = 0x83a1ecu;
}

// No handler: nothing touches it as it rises.
void neighbour_saved_paid(Wram* w, const Rom* rom, PortCpu* c,
                          NeighbourThreadWork* k) {
  (void)w;
  (void)rom;
  lda(c, 0x0000);
  c->y = c->a;
  ran(k, NT_SAVE_HANDLER);
  c->pc = 0x83a1f4u;
}

void neighbour_saved_rise(Wram* w, const Rom* rom, PortCpu* c,
                          NeighbourThreadWork* k) {
  (void)rom;
  c->y = page(w, c, NEIGHBOUR_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), pull16(w, c));
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), 0);
  set_page(w, c, NEIGHBOUR_RISE_DP_LEFT, NEIGHBOUR_RISE_PIXELS);
  lda(c, NEIGHBOUR_RISE_FRAMES);
  ran(k, NT_SAVE_RISE);
  c->pc = NEIGHBOUR_RISE_SLEEP_PC;
}

// --- $83:A21D: told 2 --------------------------------------------------------

// Its pictures' bank is kept on the stack, and its handler is another while
// it turns.
void neighbour_turn(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  (void)rom;
  const uint16_t record = page(w, c, NEIGHBOUR_DP_RECORD);
  push16(w, c, wram_r16(w, (uint16_t)(record + ACTOR_META_BANK)));
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), NEIGHBOUR_TURN_BANK);
  set_page(w, c, NEIGHBOUR_DP_TOLD, 0);
  c->a = NEIGHBOUR_TURN_HANDLER;
  ldy(c, NEIGHBOUR_BANK);
  ran(k, NT_TURN_HEAD);
  c->pc = 0x83a231u;
}

// The first pictures are played. Told nothing while they were, it waits
// to be, and is lost if it is not.
void neighbour_turn_shown(Wram* w, const Rom* rom, PortCpu* c,
                          NeighbourThreadWork* k) {
  (void)rom;
  if (told(w, c, k) != 0) {
    PORT_COVER(neighbour_turn_told);
    ran(k, NT_TAKEN);
    c->pc = 0x83a249u;
    return;
  }
  PORT_COVER(neighbour_turn_waits);
  set_page(w, c, NEIGHBOUR_DP_TOLD, NEIGHBOUR_TOLD_LOST);
  lda(c, NEIGHBOUR_TURN_FRAMES);
  ran(k, NT_TURN_WAIT);
  c->pc = 0x83a245u;
}

void neighbour_turn_over(Wram* w, const Rom* rom, PortCpu* c,
                         NeighbourThreadWork* k) {
  (void)rom;
  const uint16_t bank = pull16(w, c);
  const uint16_t record = page(w, c, NEIGHBOUR_DP_RECORD);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), bank);
  wram_w16(w, (uint16_t)(record + ACTOR_META), 0);
  c->a = NEIGHBOUR_HANDLER;
  ldy(c, NEIGHBOUR_BANK);
  ran(k, NT_TURN_TAIL);
  c->pc = 0x83a25fu;
}

// --- $83:A2B4: told 4 --------------------------------------------------------

void neighbour_sign(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  (void)rom;
  set_page(w, c, NEIGHBOUR_DP_TOLD, 0);
  ran(k, NT_STZ_DP);
  c->pc = 0x83a2b6u;
}

// The sign's record is Y, and A as well: its place is the neighbour's and
// what its kind has in a table, two pixels nearer.
void neighbour_sign_made(Wram* w, const Rom* rom, PortCpu* c,
                         NeighbourThreadWork* k) {
  const uint16_t sign = c->y;
  set_page(w, c, NEIGHBOUR_DP_SIGN, c->a);
  c->x = (uint16_t)((page(w, c, NEIGHBOUR_DP_KIND) - 2) * 2);
  const uint32_t place = NEIGHBOUR_SIGN_PLACES + c->x;
  set_c(c, false);
  wram_w16(w, (uint16_t)(sign + ACTOR_X),
           adc16(c, bus_r16(w, rom, place), page(w, c, NEIGHBOUR_DP_X)));
  set_c(c, false);
  wram_w16(w, (uint16_t)(sign + ACTOR_Z),
           adc16(c, bus_r16(w, rom, place + 2), page(w, c, 0x22)));
  wram_w16(w, (uint16_t)(sign + ACTOR_Y),
           (uint16_t)(page(w, c, NEIGHBOUR_DP_Y) + 2));
  wram_w16(w, (uint16_t)(sign + ACTOR_META), NEIGHBOUR_SIGN_PICTURE);
  wram_w16(w, (uint16_t)(sign + ACTOR_META_BANK), NEIGHBOUR_SIGN_BANK);
  wram_w16(w, (uint16_t)(sign + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(sign + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(sign + ACTOR_FLAGS),
           wram_r16(w, (uint16_t)(sign + ACTOR_FLAGS)) | ACTOR_DRAW);
  set_page(w, c, NEIGHBOUR_SIGN_DP_LEFT, NEIGHBOUR_SIGN_TURNS);
  set_page(w, c, NEIGHBOUR_SIGN_DP_WHICH, 0);
  lda(c, NEIGHBOUR_SIGN_TICKS);
  ran(k, NT_SIGN_MAKE);
  c->pc = NEIGHBOUR_SIGN_SLEEP_PC;
}

// The sign comes down: its turns are up, or the neighbour was told
// something. Told nothing, or ended, it is lost.
void neighbour_sign_end(Wram* w, const Rom* rom, PortCpu* c,
                        NeighbourThreadWork* k) {
  (void)rom;
  bool over = c->pc == NEIGHBOUR_SIGN_OVER_PC;
  if (!over) {
    ran(k, NT_BRANCH);
    if (flag(c, PORT_P_N)) {
      PORT_COVER(neighbour_sign_ended);
      ran(k, NT_TAKEN);
      over = true;
    } else {
      PORT_COVER(neighbour_sign_came_down);
      ran(k, NT_BRA);
    }
  }
  if (over) {
    set_page(w, c, NEIGHBOUR_DP_TOLD, NEIGHBOUR_TOLD_LOST);
    ran(k, NT_SIGN_OVER);
  }
  lda(c, page(w, c, NEIGHBOUR_DP_SIGN));
  ran(k, NT_LDA_DP);
  c->pc = 0x83a32fu;
}

// --- $81:8349 and $81:8377 ---------------------------------------------------

// `PHB : PHK : PLB`, and the pictures to play. The second has twelve `NOP`s
// between.
void neighbour_play(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  (void)rom;
  const bool first = c->pc == 0x818349u;
  push8(w, c, c->db);
  push8(w, c, 0x81);
  c->db = pull8(w, c);
  lda(c, first ? 0x8357 : 0x838f);
  ran(k, first ? NT_PLAY_A : NT_PLAY_B);
  c->pc = first ? 0x81834fu : 0x818389u;
}

void neighbour_play_end(Wram* w, const Rom* rom, PortCpu* c,
                        NeighbourThreadWork* k) {
  (void)rom;
  const bool first = c->pc == 0x818353u;
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  ran(k, first ? NT_PLAY_A_TAIL : NT_PLAY_B_TAIL);
  c->pc = first ? 0x818356u : 0x81838eu;
}

// --- the tourists become werewolves -----------------------------------------

void tourists_turn(Wram* w, const Rom* rom, PortCpu* c, NeighbourThreadWork* k) {
  (void)rom;
  lda(c, page(w, c, NEIGHBOUR_DP_ENTRY));
  ran(k, NT_LDA_DP);
  c->pc = 0x83a088u;
}

void tourists_gone(Wram* w, const Rom* rom, PortCpu* c, NeighbourThreadWork* k) {
  (void)rom;
  c->y = page(w, c, NEIGHBOUR_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), TOURISTS_GONE_COLLIDE);
  lda(c, TOURISTS_GONE_PICTURES);
  ran(k, NT_WOLVES_SHOW);
  c->pc = 0x83a097u;
}

// Thirteen pixels to the left of where they stood.
void tourists_werewolf_left(Wram* w, const Rom* rom, PortCpu* c,
                            NeighbourThreadWork* k) {
  (void)rom;
  set_c(c, true);
  set_page(w, c, 0x00, sbc16(c, page(w, c, NEIGHBOUR_DP_X), 0x000d));
  set_page(w, c, 0x02, page(w, c, NEIGHBOUR_DP_Y));
  c->a = WEREWOLF_THREAD;
  ldy(c, WEREWOLF_THREAD_BANK);
  ran(k, NT_WOLF_LEFT);
  c->pc = 0x83a0adu;
}

// ...and thirteen to the right, six further up.
void tourists_werewolf_right(Wram* w, const Rom* rom, PortCpu* c,
                             NeighbourThreadWork* k) {
  (void)rom;
  set_c(c, false);
  set_page(w, c, 0x00, adc16(c, page(w, c, NEIGHBOUR_DP_X), 0x000d));
  set_c(c, true);
  set_page(w, c, 0x02, sbc16(c, page(w, c, NEIGHBOUR_DP_Y), 0x0006));
  c->a = WEREWOLF_THREAD;
  ldy(c, WEREWOLF_THREAD_BANK);
  ran(k, NT_WOLF_RIGHT);
  c->pc = 0x83a0c7u;
}

// --- three they call ---------------------------------------------------------

// A is the neighbour's place in the level's list, or `$FFFF` for none.
void victim_mark_gone(Wram* w, const Rom* rom, PortCpu* c,
                      NeighbourThreadWork* k) {
  (void)rom;
  cmp16(c, c->a, 0xffff);
  ran(k, NT_GONE_TEST);
  c->pc = VICTIM_MARK_GONE_RTL_PC;
  if (c->a == 0xffff) {
    PORT_COVER(victim_mark_gone_none);
    ran(k, NT_TAKEN);
    return;
  }
  PORT_COVER(victim_mark_gone_marked);
  c->x = c->a;
  c->a = (uint16_t)((c->a & 0xff00u) | NEIGHBOUR_GONE);
  set_nz8(c, NEIGHBOUR_GONE);
  wram_w8(w, (uint32_t)W_VICTIM_SPAWNED + c->x, NEIGHBOUR_GONE);
  ran(k, NT_GONE_SET);
}

void neighbours_left_down(Wram* w, const Rom* rom, PortCpu* c,
                          NeighbourThreadWork* k) {
  (void)rom;
  bool carry = true, overflow = false;
  lda(c, bcd_sub16(wram_r16(w, W_NEIGHBOURS_LEFT), 0x0001, &carry, &overflow));
  set_c(c, carry);
  set_v(c, overflow);
  wram_w16(w, W_NEIGHBOURS_LEFT, c->a);
  ran(k, NT_LEFT_DOWN);
  if (c->a != 0) ran(k, NT_TAKEN);  // a branch to the next instruction
  c->pc = NEIGHBOURS_LEFT_DOWN_RTL_PC;
}

// Y is the kind, and the sign of what A was loaded with is the player's
// side, as it is for `score_add`.
void saved_kind_add(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  push16(w, c, c->y);
  ran(k, NT_KIND_HEAD);
  if (flag(c, PORT_P_N)) {
    PORT_COVER(saved_kind_second_side);
    ran(k, NT_TAKEN);
    lda(c, 0x0002);
    ran(k, NT_LDA_IMM);
  } else {
    lda(c, 0x0000);
    ran(k, NT_KIND_FIRST);
  }

  // `JSR $C7C2`: which of the two slots has that side.
  push16(w, c, 0xc82c);
  ran(k, NT_JSR);
  cmp16(c, c->a, wram_r16(w, W_SCORE_SLOT_SIDE));
  ran(k, NT_SLOT_TEST);
  if (c->a == wram_r16(w, W_SCORE_SLOT_SIDE)) {
    lda(c, 0x0000);
    ran(k, NT_SLOT_FIRST);
  } else {
    PORT_COVER(saved_kind_second_slot);
    ran(k, NT_TAKEN);
    cmp16(c, c->a, wram_r16(w, W_SCORE_SLOT_SIDE + 2));
    ran(k, NT_SLOT_TEST);
    lda(c, 0x0002);  // `neighbour_thread_needs_met` saw that it is
    ran(k, NT_LDA_IMM);
  }
  ran(k, NT_RTS);
  c->s = (uint16_t)(c->s + 2);

  ldx(c, c->a);
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, W_SAVED_COUNT_A), wram_r16(w, W_SAVED_COUNT_B)));
  cmp16(c, c->a, SAVED_KINDS_MOST);
  ran(k, NT_KIND_ROOM);
  if (c->a >= SAVED_KINDS_MOST) {
    PORT_COVER(saved_kind_full);
    ran(k, NT_TAKEN);
    lda(c, pull16(w, c));
    ran(k, NT_PLA);
    c->pc = 0x80c83eu;
    return;
  }

  ran(k, NT_KIND_JUMP);
  const bool second = bus_r16(w, rom, SAVED_SLOT_JUMPS + c->x) == SAVED_SLOT_B_PC;
  const uint32_t count_at = second ? W_SAVED_COUNT_B : W_SAVED_COUNT_A;
  const uint32_t kinds_at = second ? W_SAVED_KINDS_B : W_SAVED_KINDS_A;
  c->a = pull16(w, c);
  const uint16_t count = wram_r16(w, count_at);
  wram_w8(w, kinds_at + count, (uint8_t)c->a);
  ldx(c, (uint16_t)(count + 1));
  wram_w16(w, count_at, c->x);
  ran(k, NT_KIND_ADD);
  c->pc = second ? 0x80c85eu : 0x80c84eu;
}

// --- the stepper -------------------------------------------------------------

void stepper_started(Wram* w, const Rom* rom, PortCpu* c,
                     NeighbourThreadWork* k) {
  (void)rom;
  set_page(w, c, NEIGHBOUR_DP_TOLD, 0);
  set_page(w, c, 0x10, 0);
  set_page(w, c, 0x12, 0);
  set_page(w, c, 0x0c, 0x0001);
  set_page(w, c, 0x0e, 0x0001);
  lda(c, 0x0001);
  ran(k, NT_STEPPER_STARTED);
  c->pc = STEPPER_SLEEP_PC;
}

void stepper_told(Wram* w, const Rom* rom, PortCpu* c, NeighbourThreadWork* k) {
  (void)rom;
  ran(k, NT_BRANCH);
  if (flag(c, PORT_P_N)) {
    PORT_COVER(stepper_told_end);
    ran(k, NT_TAKEN);
    c->pc = STEPPER_END_PC;
  } else if (is(c, NEIGHBOUR_TOLD_LOST, k)) {
    PORT_COVER(stepper_told_lost);
    c->x = page(w, c, NEIGHBOUR_DP_RECORD);
    wram_w16(w, (uint16_t)(c->x + ACTOR_COLLIDE_ID), 0);
    lda(c, page(w, c, NEIGHBOUR_DP_ENTRY));
    ran(k, NT_LOST);
    c->pc = STEPPER_LOST_CALL_PC;
  } else if (is(c, NEIGHBOUR_TOLD_SIGN_UP, k)) {
    PORT_COVER(stepper_told_sign);
    show_for_sign(w, c, STEPPER_SIGN_PICTURE, k);
    c->pc = STEPPER_SIGN_CALL_PC;
  } else {
    PORT_COVER(stepper_told_saved);
    save(c, STEPPER_SAVED_PICTURE, STEPPER_AWARD, STEPPER_SAVED_CALL_PC, k);
  }
}

void stepper_signed(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  set_page(w, c, 0x10, 0);
  ran(k, NT_STZ_DP);
  lda(c, page(w, c, NEIGHBOUR_DP_TOLD));
  cmp16(c, c->a, NEIGHBOUR_TOLD_LOST);
  ran(k, NT_TOLD_CMP);
  if (c->a == NEIGHBOUR_TOLD_LOST) {
    PORT_COVER(stepper_signed_lost);
    c->pc = STEPPER_SIGNED_LOST_PC;
    return;
  }
  ran(k, NT_TAKEN);
  if (told(w, c, k) == 0) {
    PORT_COVER(stepper_signed_goes_on);
    ran(k, NT_TAKEN);
    argument(c, 0x0001, STEPPER_SLEEP_PC, k);
    return;
  }
  stepper_told(w, rom, c, k);
}

// --- the jumper --------------------------------------------------------------

// Its second record, which A is: where the neighbour starts, on the ground,
// and not yet drawn.
void jumper_mat_made(Wram* w, const Rom* rom, PortCpu* c,
                     NeighbourThreadWork* k) {
  (void)rom;
  set_page(w, c, JUMPER_DP_MAT, c->a);
  c->y = c->a;
  wram_w16(w, (uint16_t)(c->y + ACTOR_X), page(w, c, 0x00));
  wram_w16(w, (uint16_t)(c->y + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(c->y + ACTOR_Y), page(w, c, 0x02));
  wram_w16(w, (uint16_t)(c->y + ACTOR_THREAD), wram_r16(w, W_SCHED_CUR_TASK));
  wram_w16(w, (uint16_t)(c->y + ACTOR_COLLIDE_ID), 0);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), JUMPER_MAT_PICTURE);
  lda(c, JUMPER_MAT_BANK);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META_BANK), c->a);
  ran(k, NT_MAT_MAKE);
  c->pc = 0x839fc9u;
}

void jumper_started(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  (void)rom;
  set_page(w, c, NEIGHBOUR_DP_TOLD, 0);
  ran(k, NT_STZ_DP);
  c->pc = JUMPER_LANDED_PC;
}

void jumper_told(Wram* w, const Rom* rom, PortCpu* c, NeighbourThreadWork* k) {
  (void)rom;
  told(w, c, k);
  if (flag(c, PORT_P_N)) {
    PORT_COVER(jumper_told_end);
    ran(k, NT_TAKEN);
    c->pc = JUMPER_END_PC;
  } else if (is(c, NEIGHBOUR_TOLD_TURN, k)) {
    PORT_COVER(jumper_told_turn);
    c->pc = JUMPER_TURN_HIDE_PC;
  } else if (is(c, NEIGHBOUR_TOLD_LOST, k)) {
    PORT_COVER(jumper_told_lost);
    c->pc = JUMPER_LOST_HIDE_PC;
  } else if (is(c, NEIGHBOUR_TOLD_SIGN_UP, k)) {
    PORT_COVER(jumper_told_sign);
    c->pc = JUMPER_SIGN_CALL_PC;
  } else {
    PORT_COVER(jumper_told_saved);
    c->pc = JUMPER_SAVED_HIDE_PC;
  }
}

void jumper_mat_hide(Wram* w, const Rom* rom, PortCpu* c,
                     NeighbourThreadWork* k) {
  (void)rom;
  c->y = page(w, c, JUMPER_DP_MAT);
  lda(c, wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) & (uint16_t)~ACTOR_DRAW);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), c->a);
  ran(k, NT_MAT_HIDE);
  c->pc = 0x839fe1u;
}

void jumper_saved(Wram* w, const Rom* rom, PortCpu* c, NeighbourThreadWork* k) {
  (void)w;
  (void)rom;
  save(c, JUMPER_SAVED_PICTURE, JUMPER_AWARD, JUMPER_SAVED_CALL_PC, k);
}

// Its load given back, and its own record freed by a call: the second's
// is after.
void jumper_end(Wram* w, const Rom* rom, PortCpu* c, NeighbourThreadWork* k) {
  (void)rom;
  set_c(c, true);
  wram_w16(w, W_SPAWN_LOAD, sbc16(c, wram_r16(w, W_SPAWN_LOAD), JUMPER_LOAD));
  lda(c, page(w, c, NEIGHBOUR_DP_RECORD));
  ran(k, NT_END_CALL);
  c->pc = JUMPER_END_FREE_PC;
}

void jumper_mat_free(Wram* w, const Rom* rom, PortCpu* c,
                     NeighbourThreadWork* k) {
  (void)rom;
  lda(c, page(w, c, JUMPER_DP_MAT));
  ran(k, NT_MAT_FREE);
  c->pc = ACTOR_SLOT_FREE_ENTRY;
}

void jumper_turned(Wram* w, const Rom* rom, PortCpu* c,
                   NeighbourThreadWork* k) {
  if (told(w, c, k) != 0) {
    PORT_COVER(jumper_turned_told);
    ran(k, NT_TAKEN);
    jumper_told(w, rom, c, k);
    return;
  }
  PORT_COVER(jumper_turned_stands);
  ran(k, NT_JMP);
  c->pc = JUMPER_LANDED_PC;
}

void jumper_signed(Wram* w, const Rom* rom, PortCpu* c,
                   NeighbourThreadWork* k) {
  first_picture(w, c, k);
  ran(k, NT_BRA);
  jumper_told(w, rom, c, k);
}

// --- the bouncer -------------------------------------------------------------

static void bouncer_begin(Wram* w, PortCpu* c, NeighbourThreadWork* k) {
  wram_w16(w, W_BOUNCER_UP, (uint16_t)(wram_r16(w, W_BOUNCER_UP) + 1));
  enter(w, c, BOUNCER_LOAD, BOUNCER_KIND, BOUNCER_PICTURE, BOUNCER_BANK);
  ran(k, NT_BOUNCER_ENTER);
  c->pc = BOUNCER_BEGIN_PC;
}

static void bouncer_off(Wram* w, PortCpu* c, NeighbourThreadWork* k) {
  wram_w16(w, W_BOUNCER_UP, 0);
  ran(k, NT_BOUNCER_OFF);
  c->pc = BOUNCER_END_PC;
}

void bouncer_enter(Wram* w, const Rom* rom, PortCpu* c,
                   NeighbourThreadWork* k) {
  (void)rom;
  lda(c, wram_r16(w, W_BOUNCER_UP));
  ran(k, NT_ABS_TEST);
  if (c->a == 0) {
    PORT_COVER(bouncer_enter_first);
    ran(k, NT_TAKEN);
    bouncer_begin(w, c, k);
    return;
  }
  PORT_COVER(bouncer_enter_waits);
  argument(c, 0x0001, BOUNCER_WAIT_PC, k);
}

// A frame of waiting for the other. One that is gone from the level's list
// by now ends.
void bouncer_waited(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  (void)rom;
  c->x = page(w, c, NEIGHBOUR_DP_ENTRY);
  lda(c, wram_r8(w, (uint32_t)W_VICTIM_SPAWNED + c->x));
  cmp16(c, c->a, NEIGHBOUR_GONE);
  ran(k, NT_BOUNCER_GONE);
  if (c->a == NEIGHBOUR_GONE) {
    PORT_COVER(bouncer_waited_gone);
    ran(k, NT_TAKEN);
    c->pc = BOUNCER_GONE_PC;
    return;
  }
  lda(c, wram_r16(w, W_BOUNCER_UP));
  ran(k, NT_ABS_TEST);
  if (c->a != 0) {
    PORT_COVER(bouncer_waited_again);
    ran(k, NT_TAKEN);
    argument(c, 0x0001, BOUNCER_WAIT_PC, k);
    return;
  }
  PORT_COVER(bouncer_waited_free);
  ran(k, NT_BRA);
  bouncer_begin(w, c, k);
}

void bouncer_started(Wram* w, const Rom* rom, PortCpu* c,
                     NeighbourThreadWork* k) {
  (void)rom;
  c->y = page(w, c, NEIGHBOUR_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS),
           wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) | 0x0008u);
  set_page(w, c, NEIGHBOUR_DP_TOLD, 0);
  set_page(w, c, BOUNCER_DP_SPEED, BOUNCER_SPEED);
  set_page(w, c, BOUNCER_DP_FRAMES, 0);
  ran(k, NT_BOUNCER_STARTED);
  argument(c, 0x0001, BOUNCER_SLEEP_PC, k);
}

void bouncer_frame(Wram* w, const Rom* rom, PortCpu* c,
                   NeighbourThreadWork* k) {
  told(w, c, k);
  if (flag(c, PORT_P_N)) {
    lda(c, wram_r16(w, W_BOUNCER_LEAVE_A) | wram_r16(w, W_BOUNCER_LEAVE_B));
    ran(k, NT_BOUNCER_LEAVE);
    if (c->a != 0) {
      PORT_COVER(bouncer_frame_leaves);
      ran(k, NT_JMP);
      bouncer_off(w, c, k);
      return;
    }
    PORT_COVER(bouncer_frame_stays);
  }
  ran(k, NT_TAKEN);

  c->y = page(w, c, NEIGHBOUR_DP_RECORD);
  set_c(c, false);
  lda(c, adc16(c, wram_r16(w, (uint16_t)(c->y + ACTOR_Z)),
               page(w, c, BOUNCER_DP_SPEED)));
  wram_w16(w, (uint16_t)(c->y + ACTOR_Z), c->a);
  ran(k, NT_BOUNCER_MOVE);
  if (c->a == 0) {
    PORT_COVER(bouncer_frame_lands);
    ran(k, NT_TAKEN);
    set_page(w, c, BOUNCER_DP_SPEED, BOUNCER_SPEED);
    set_page(w, c, BOUNCER_DP_FRAMES, 0);
    set_page(w, c, NEIGHBOUR_DP_WHICH, 0);
    wram_w16(w, (uint16_t)(c->y + ACTOR_META), BOUNCER_PICTURE);
    lda(c, 0x0004);
    ran(k, NT_BOUNCER_LAND);
    c->pc = BOUNCER_LAND_PC;
    return;
  }

  const uint16_t frames = (uint16_t)(page(w, c, BOUNCER_DP_FRAMES) - 1);
  set_page(w, c, BOUNCER_DP_FRAMES, frames);
  lda(c, frames & 0x0007u);
  ran(k, NT_BOUNCER_COUNT);
  if (c->a != 0) {
    ran(k, NT_TAKEN);
  } else {
    // Every eighth frame it is a pixel a frame slower, and shows the next.
    PORT_COVER(bouncer_frame_slows);
    set_page(w, c, BOUNCER_DP_SPEED,
             (uint16_t)(page(w, c, BOUNCER_DP_SPEED) - 1));
    const uint16_t which = (uint16_t)(page(w, c, NEIGHBOUR_DP_WHICH) + 1);
    set_page(w, c, NEIGHBOUR_DP_WHICH, which);
    c->a = asl16(c, which);
    c->x = c->a;
    wram_w16(w, (uint16_t)(c->y + ACTOR_META),
             bus_r16(w, rom, BOUNCER_PICTURES + c->x));
    ran(k, NT_BOUNCER_PICTURE);
  }
  argument(c, 0x0001, BOUNCER_SLEEP_PC, k);
}

// On the ground four frames and the sound played. Told `1` it is saved;
// anything else that is not an end is forgotten.
void bouncer_landed(Wram* w, const Rom* rom, PortCpu* c,
                    NeighbourThreadWork* k) {
  (void)rom;
  c->y = page(w, c, NEIGHBOUR_DP_RECORD);
  wram_w16(w, (uint16_t)(c->y + ACTOR_META), BOUNCER_LANDED_PICTURE);
  lda(c, page(w, c, NEIGHBOUR_DP_TOLD));
  ran(k, NT_BOUNCER_LANDED);
  if (c->a == 0) {
    PORT_COVER(bouncer_landed_untold);
    ran(k, NT_TAKEN);
  } else {
    ran(k, NT_BRANCH);
    if (flag(c, PORT_P_N)) {
      PORT_COVER(bouncer_landed_ends);
      ran(k, NT_TAKEN);
      bouncer_off(w, c, k);
      return;
    }
    if (is(c, 0x0001, k)) {
      PORT_COVER(bouncer_landed_saved);
      save(c, BOUNCER_SAVED_PICTURE, BOUNCER_AWARD, BOUNCER_SAVED_CALL_PC, k);
      return;
    }
    PORT_COVER(bouncer_landed_forgets);
    set_page(w, c, NEIGHBOUR_DP_TOLD, 0);
    ran(k, NT_STZ_DP);
    ran(k, NT_BRA);
  }
  argument(c, 0x0001, BOUNCER_SLEEP_PC, k);
}

void bouncer_saved(Wram* w, const Rom* rom, PortCpu* c,
                   NeighbourThreadWork* k) {
  (void)rom;
  ran(k, NT_BRA);
  bouncer_off(w, c, k);
}
