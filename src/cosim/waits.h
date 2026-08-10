// The ROM's busy-wait loops: instructions the 65816 executes that are not work.
//
// This table exists because *every* measure of "how much of the game runs
// natively" needs a denominator, and the naive denominator is wrong in a way
// that flatters nobody. A 65816 going round `BIT $00C8 : BPL` a hundred
// thousand times is a CPU with nothing to do until the next VBlank. Left in the
// denominator it makes the port's share look smaller than it is; left in a
// ranking of what to port next it puts a two-instruction loop at the top, where
// porting it would achieve exactly nothing — the C would have to spin on the
// same flag.
//
// Each site was found by reading the disassembly of a routine that a ranking had
// put near the top, and each is checked against a profile rather than assumed:
// `$80:9F9D` is nine instructions long and 19,289,582 of the 19,289,827
// instructions credited to it are the first two below.
//
// **This is the one copy.** `tools/native_share.py` parses this file rather than
// keeping its own list — it used to keep one, and a table maintained in two
// languages is a table that disagrees with itself. The regex it uses wants the
// `{0x......, N, "..."}` shape below, one site per line, so keep that shape.
//
// `span` is how many ROM bytes the loop occupies, counted from `addr`. An
// instruction belongs to the site when its opcode byte falls in
// `[addr, addr + span)`, which is the same rule the profiler's byte-range sum
// applies, so the two measures mean the same thing.
//
// Harness code, not port code — this is a fact about the ROM the port is
// replacing, and nothing in `src/port/` may know it.

#ifndef COSIM_WAITS_H
#define COSIM_WAITS_H

#include <stdint.h>

typedef struct {
  uint32_t addr;
  uint32_t span;
  const char* what;
} CosimWaitSite;

static const CosimWaitSite cosim_wait_sites[] = {
    {0x809FAA, 5, "BIT $00C8 : BPL   -- waiting for a VBL callback to fire"},
    {0x80CB6C, 5, "CMP $2140 : BNE   -- SPC700 IPL, waiting for $BBAA"},
    {0x80CB84, 5, "CMP $2140 : BNE   -- SPC700 IPL, waiting per byte"},
    {0x80CB94, 5, "CMP $2140 : BNE   -- SPC700 IPL, waiting per block"},
    {0x80CB99, 4, "ADC #$03 : BEQ    -- the IPL delay loop after it"},
    {0x82AC65, 8, "CMP #$0078 : BCC  -- the level intro, holding for 120 frames"},
    {0x82AC92, 8, "CMP #$0078 : BCC  -- ...and again after the block library"},
    // `$80:91F7` spends 4,406,310 of its 4,406,610 instructions in these two
    // loops -- 99.993% -- and the 300 that are left are 30 real instructions a
    // call. Both spin on `$136C`, which the main CPU never touches: `$80:9C52`
    // zeroes it and queues the vblank job `$80:9C63` to `INC` it fifteen times,
    // and `$80:9C72` queues `$80:9C7D` to `DEC` it back past zero. So this is a
    // fade counted on the vblank side with the CPU held against it, and porting
    // either loop would replace a spin with a spin.
    {0x80923A, 8, "CMP #$000F : BNE  -- a screen fading in, counted by a VBL job"},
    {0x80924C, 8, "AND #$0080 : BEQ  -- ...and the same screen fading back out"},
    // The same story one row down. `$80:9F29` queues the vblank job `$80:9ED0`
    // to push a tilemap into VRAM and then holds here until `$C6`, the job's
    // remaining byte count, reaches zero: 2,149,252 of its 2,149,592
    // instructions, 99.98%, leaving 34 a call that are real.
    {0x809F5C, 4, "LDA $C6 : BNE     -- waiting for a queued VRAM upload to drain"},
};

#define COSIM_WAIT_SITE_COUNT \
  ((int)(sizeof cosim_wait_sites / sizeof *cosim_wait_sites))

// Is the instruction whose opcode byte is at `pc` part of one of them?
static inline bool cosim_is_wait_site(uint32_t pc) {
  for (int i = 0; i < COSIM_WAIT_SITE_COUNT; i++)
    if (pc >= cosim_wait_sites[i].addr &&
        pc < cosim_wait_sites[i].addr + cosim_wait_sites[i].span)
      return true;
  return false;
}

#endif
