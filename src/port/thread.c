#include "port/thread.h"

#include "port/coverage.h"

void thread_tick_waits(Wram* w) {
  // Downwards from the last slot, exactly as `LDX #$002E ... DEX DEX BPL` does.
  // The direction is not observable in the result, but keeping it means the
  // listing and the port read the same way.
  for (int slot = WRAM_THREAD_SLOTS - 1; slot >= 0; slot--) {
    uint32_t at = W_THREAD_WAIT + (uint32_t)slot * 2;
    uint16_t wait = wram_r16(w, at);
    // Slot empty; then: expired, and stays that way.
    if (!(wait & 0x8000)) { PORT_COVER(wait_empty); continue; }
    if (wait == 0x8000) { PORT_COVER(wait_expired); continue; }
    PORT_COVER(wait_tick);
    wram_w16(w, at, (uint16_t)(wait - 1));
  }
}

// Both adders are the same routine with different table bases, slot counts and
// caps. The one real difference is the order of the two stores, which is not
// observable — the ROM's queue A pulls the bank off the stack first and queue B
// pulls it second — so they share an implementation.
static int vbl_queue_add(Wram* w, uint32_t table, uint32_t count_at, int slots,
                         int scan_from, uint16_t addr, uint16_t bank) {
  if (wram_r16(w, count_at) >= (uint16_t)slots) { PORT_COVER(queue_full); return -1; }

  int off = 0;
  for (int x = scan_from; x != 0; x -= 4) {
    if (wram_r16(w, table + (uint32_t)x) == 0) { off = x; break; }
  }

  // The stores are 16-bit, as the ROM does them, so the bank's high byte lands
  // in the slot's pad byte. Every caller passes a bank below $100, which is why
  // the pad reads as zero in a trace.
  wram_w16(w, table + (uint32_t)off, (uint16_t)(addr - 1));
  wram_w16(w, table + (uint32_t)off + 2, bank);
  wram_w16(w, count_at, (uint16_t)(wram_r16(w, count_at) + 1));
  return off;
}

int vbl_queue_a_add(Wram* w, uint16_t addr, uint16_t bank) {
  return vbl_queue_add(w, W_VBL_QUEUE_A, W_VBL_QUEUE_A_COUNT, W_VBL_QUEUE_A_SLOTS,
                       0x38, addr, bank);
}

int vbl_queue_b_add(Wram* w, uint16_t addr, uint16_t bank) {
  return vbl_queue_add(w, W_VBL_QUEUE_B, W_VBL_QUEUE_B_COUNT, W_VBL_QUEUE_B_SLOTS,
                       0x1c, addr, bank);
}

// ---------------------------------------------------------------------------
// $80:825E  thread_spawn
// ---------------------------------------------------------------------------

int thread_spawn(Wram* w, const Rom* rom, uint16_t entry, uint16_t bank,
                 uint16_t caller_dp) {
  // `PHD : DEC A : PHA`. The decrement is the same off-by-one every far address
  // in this game's stack frames carries: what reaches it is an `RTL`, and an
  // `RTL` adds one.
  uint16_t target = (uint16_t)(entry - 1);

  // `LDX #$002E`, then `LDA $001180,X : BPL <found> : DEX DEX BPL`. Downwards,
  // and slot 0 is tested like any other — the loop's `BPL` fires on X = 0 and
  // only fails at $FFFE. So a full board falls out of the bottom.
  int slot = -1;
  for (int x = (WRAM_THREAD_SLOTS - 1) * 2; x >= 0; x -= 2) {
    if (!(wram_r16(w, W_THREAD_WAIT + (uint32_t)x) & 0x8000)) {
      slot = x;
      break;
    }
  }
  if (slot < 0) {
    // `$80:82D8  PLA : PLD : LDA #$0000 : RTL`.
    PORT_COVER(spawn_full);
    return -1;
  }
  PORT_COVER(spawn_took);

  // `INC $0006`, and the two arrays that record what this thread was started
  // as. Neither is read by anything traced; they are written because the ROM
  // writes them, and the diff is what says so.
  wram_w16(w, W_THREAD_COUNT, (uint16_t)(wram_r16(w, W_THREAD_COUNT) + 1));
  wram_w16(w, W_THREAD_ENTRY + (uint32_t)slot, target);
  wram_w16(w, W_THREAD_ENTRY_BANK + (uint32_t)slot, bank);

  // A new thread has no handler registered. `thread_call_handler` reads both
  // halves and treats zero as "nobody home", so this is what should make a
  // recycled slot forget the last actor that used it.
  //
  // Transcribed rather than diffed: deleting both stores passes all 330 calls on
  // `movies/level1-2p.zmv`, because every slot handed out is already zero here —
  // something on the way out of a thread clears it first, and this is belt and
  // braces. Same shape as the `STZ $7E` in `enemy_die`: a store the diff cannot
  // tell from a no-op, kept because it is what the ROM does and written down
  // because noticing is the only defence.
  wram_w16(w, W_THREAD_HANDLER + (uint32_t)slot, 0);
  wram_w16(w, W_THREAD_HANDLER_BANK + (uint32_t)slot, 0);

  // `LDA $80830E,X : STA $0011B0,X : TCD`. The stack pointer this slot always
  // starts at, out of ROM — and then the direct page is pointed *at the stack*,
  // which is how the nine bytes below get written with direct-page stores.
  uint16_t sp = rom_word(rom, THREAD_SP_TABLE + (uint32_t)slot);
  wram_w16(w, W_THREAD_SP + (uint32_t)slot, sp);

  // The frame, in the order the ROM writes it, because four of these six stores
  // overlap and the last one to touch a byte wins. What it adds up to is
  // exactly what `$80:8390  TCS : PLD : PLP : PLB : RTL` expects: a direct page
  // at +1, a processor status of **zero** at +3 — native, 16-bit, decimal and
  // interrupts clear — a data bank at +4, and a far return address at +5 that
  // lands on `entry`. Under it, at +8, a second far return address to
  // `thread_exit`, which is where the thread body's own `RTL` goes.
  uint16_t dp = rom_word(rom, THREAD_DP_TABLE + (uint32_t)slot);
  wram_w16(w, (uint32_t)(uint16_t)(sp + 3), 0);            // STZ $03
  wram_w16(w, (uint32_t)(uint16_t)(sp + 4), bank);         // STY $04
  wram_w16(w, (uint32_t)(uint16_t)(sp + 7), bank);         // STY $07
  wram_w16(w, (uint32_t)(uint16_t)(sp + 5), target);       // STA $05
  wram_w16(w, (uint32_t)(uint16_t)(sp + 9), 0);            // STZ $09
  wram_w16(w, (uint32_t)(uint16_t)(sp + 8), THREAD_EXIT_RETURN);  // STA $08
  wram_w16(w, (uint32_t)(uint16_t)(sp + 1), dp);           // STA $01

  // `LDA #$8001 : STA $001180,X`. Live, and runnable on the next tick.
  wram_w16(w, W_THREAD_WAIT + (uint32_t)slot, THREAD_WAIT_NEW);

  // `LDY #$0000 : LDA ($01,S),Y : STA $00` and four more. `($01,S)` is the `D`
  // the opening `PHD` saved, so what this copies is the *caller's* first five
  // direct-page words onto the new thread's page — which is the only way a
  // spawner has of telling the thread anything.
  for (int i = 0; i < THREAD_SPAWN_ARGS; i++)
    wram_w16(w, (uint32_t)(uint16_t)(dp + i * 2),
             wram_r16(w, (uint32_t)(uint16_t)(caller_dp + i * 2)));

  return slot;
}
