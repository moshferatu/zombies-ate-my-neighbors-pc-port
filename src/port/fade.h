// Screen fades — the first ported routine that suspends.
//
// Everything in `src/port/` before this was a leaf that ran to completion, and
// that was deliberate: it got the harness working against routines whose
// contract is "arguments in, answer out". `fade_in` is the other kind. It runs
// for fifteen frames, sleeping between each one, and the 65816 version does that
// by parking its stack pointer inside `thread_yield` and being resumed later.
//
// It was chosen to go first because it is the smallest routine in the game that
// does this. It calls nothing but `thread_yield`, so its whole observable effect
// is one word of WRAM, and there is no unported subroutine hiding inside a
// segment to muddy the diff. What is being proved here is the *mechanism* — see
// `docs/threads.md` — not the arithmetic.
//
// Port code: libc only.

#ifndef PORT_FADE_H
#define PORT_FADE_H

#include <stdint.h>

#include "port/coroutine.h"
#include "port/wram.h"

// `$80:891A` — ramp the screen from black to full brightness, one step per
// frame.
//
//     fade_in:                          ; $80:891A
//       LDA #$0000 : STA $136C          ; brightness_shadow = 0
//     loop:                             ; $80:8920
//       LDA #$0001 : JSL thread_yield   ; sleep one tick
//       INC $136C
//       LDA $136C : CMP #$000F : BNE loop
//       RTL                             ; $80:8932
//
// `brightness_shadow` is what the NMI handler restores into `INIDISP` when it
// lifts forced blank (step 9 in `docs/frame-skeleton.md`), so each of the
// fifteen suspensions is one frame the player sees one shade brighter.
//
// Fifteen yields, then it returns. Note the ramp reaches 15 but the routine only
// sleeps *before* each increment, so the frame that displays full brightness is
// the caller's, not this routine's.
//
// There is a mirror-image `fade_out` at `$80:8933` — same shape, `DEC` instead
// of `INC`, ending with `STA #$0080` (forced blank) and a bare `WAI`. It is not
// ported because `movies/level1.zmv` never executes it, and a routine the
// harness cannot reach is a routine the harness cannot check.
typedef struct {
  PortCoro co;
} FadeCtx;

PortStep fade_in(Wram* w, FadeCtx* c, uint16_t* ticks);

#endif
