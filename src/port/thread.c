#include "port/thread.h"

void thread_tick_waits(Wram* w) {
  // Downwards from the last slot, exactly as `LDX #$002E ... DEX DEX BPL` does.
  // The direction is not observable in the result, but keeping it means the
  // listing and the port read the same way.
  for (int slot = WRAM_THREAD_SLOTS - 1; slot >= 0; slot--) {
    uint32_t at = W_THREAD_WAIT + (uint32_t)slot * 2;
    uint16_t wait = wram_r16(w, at);
    if (!(wait & 0x8000)) continue;  // slot empty
    if (wait == 0x8000) continue;    // expired, and stays that way
    wram_w16(w, at, (uint16_t)(wait - 1));
  }
}

// Both adders are the same routine with different table bases, slot counts and
// caps. The one real difference is the order of the two stores, which is not
// observable — the ROM's queue A pulls the bank off the stack first and queue B
// pulls it second — so they share an implementation.
static int vbl_queue_add(Wram* w, uint32_t table, uint32_t count_at, int slots,
                         int scan_from, uint16_t addr, uint16_t bank) {
  if (wram_r16(w, count_at) >= (uint16_t)slots) return -1;

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
