#include "port/fade.h"

#include "port/coverage.h"

// The resume labels are the ROM addresses they stand for: `case 0x8927` is the
// instruction after the `JSL thread_yield` at `$80:8923`. Nothing in C cares
// what the numbers are, so they may as well point at the listing.
#define FADE_IN_RESUME 0x8927

PortStep fade_in(Wram* w, FadeCtx* c, uint16_t* ticks) {
  switch (c->co.resume) {
    case PORT_CORO_ENTRY:
      // `LDA #$0000 : STA $136C`. The store is 16-bit, as everything in this
      // game is — `M` is clear from RESET onwards — so it clears the pad byte
      // at $136D too. That matters: the ROM's version does, and the diff is
      // byte for byte.
      wram_w16(w, W_BRIGHTNESS_SHADOW, 0);
      return port_yield(&c->co, 1, ticks, FADE_IN_RESUME);

    case FADE_IN_RESUME:
      // `INC $136C`, then `LDA $136C : CMP #$000F : BNE loop`. The compare is
      // against the value just stored, so the loop ends the moment the ramp
      // reaches full brightness rather than one step later.
      wram_w16(w, W_BRIGHTNESS_SHADOW,
               (uint16_t)(wram_r16(w, W_BRIGHTNESS_SHADOW) + 1));
      PORT_COVER_IF(wram_r16(w, W_BRIGHTNESS_SHADOW) != 0x000f, fade_in_step,
                    fade_in_done);
      if (wram_r16(w, W_BRIGHTNESS_SHADOW) != 0x000f)
        return port_yield(&c->co, 1, ticks, FADE_IN_RESUME);
      break;
  }
  return PORT_RETURNED;
}
