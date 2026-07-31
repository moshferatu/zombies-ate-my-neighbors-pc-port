// $80:9D39 — the game's random number generator.
//
// PLAN.md names "faithful RNG replication" as one of the project's three top
// risks, alongside the audio driver and undocumented boss AI. This is that risk,
// and it is twenty-two bytes:
//
//     rng_next:                    ; $80:9D39
//       SEP #$20                   ; 8-bit A for the whole body
//       LDA $0024                  ; the state byte
//       ROL $0024                  ; ...shifted left through the caller's carry
//       EOR $0024                  ; A = state ^ that
//       ROR $0024                  ; ...and back down through the bit that fell out
//       INC $0025                  ; a plain counter, bumped every call
//       ADC $0025
//       BVC +3 : INC $0025         ; ...and bumped a second time when that overflowed
//       STA $0024
//       REP #$20 : AND #$00FF      ; the answer is one byte, zero-extended
//       RTL                        ; $80:9D5A
//
// **The caller's carry is an input.** `LDA` does not touch it, so the `ROL` two
// instructions later shifts in whatever flag the caller happened to arrive with —
// and the `ADC` further down adds it again through the `ROR`'s output. That is
// not a quirk the port may round off: `enemy_d301_collide` reaches this routine
// on two different paths whose carry differs (a spliced reaction returns carry
// set, an already-flashing one returns it clear), so the same creature draws from
// a different sequence depending on which one it took.
//
// **It is a byte generator, not a word one.** `SEP #$20` means the whole state,
// the whole arithmetic and the whole answer are eight bits; the `AND #$00FF` on
// the way out throws away the accumulator's hidden high half, which is why the
// caller's A does *not* leak into the result. Every user in the game reads it as
// 0..255 — `CMP #$0019`, `CMP #$0087`, `AND #$0003` — and the port's callers
// should too.
//
// **The `ADC` assumes binary.** Decimal mode is set only inside `score_add`'s
// `SED`/`CLD` pair, and nothing calls this from between them, so `D` is clear on
// every call the harness has ever seen. Recorded rather than asserted: if that
// ever stops being true the diff says so on the first call.
//
// The `SEP`/`REP` pair carries the same caveat one flag over, and this one the
// diff genuinely cannot see: `verify` compares N, Z, C and V but not `M`, and
// substitution jumps to the `RTL` without running either instruction. A caller
// arriving with `M` **set** would get it cleared by the ROM and left set by the
// port. Nothing in the game does — `M` has been clear since RESET, which is why
// every `LDA` in `src/port/` is sixteen bits — so this is a note, not a bug.
//
// **The entropy comes from the NMI, not from the draws.** `$80:81E7  LDA $1EB4 :
// BNE : INC $24` sits in the vblank handler's tail and bumps the *word* at
// `$0024` once per frame while the game is running — one sixteen-bit increment
// across both state bytes, not one per byte. That is the only writer outside this
// routine that any trace has seen, and it is what makes the sequence depend on
// *when* something asked rather than only on how many times. The port does not
// have to reproduce it: the NMI is still the ROM's.
//
// Registered as a routine of its own rather than inlined into its callers, for
// `score_add`'s reason: it has many call sites in code nobody has ported —
// `$81:D04A`, `$81:D069`, `$81:D08B` in one actor body alone — and every one of
// them is a call `verify` can check.
//
// Port code: libc only.

#ifndef PORT_RNG_H
#define PORT_RNG_H

#include <stdbool.h>
#include <stdint.h>

#include "port/wram.h"

#define RNG_NEXT_ENTRY 0x809d39u

// The two state bytes, absolute in bank `$7E` — not direct page, so they are the
// same two bytes whichever thread's page the caller was running on.
#define W_RNG_STATE 0x0024
#define W_RNG_COUNTER 0x0025

// What the routine leaves behind. X and Y are not here: nothing between the
// entry and the `RTL` mentions either.
//
// **`v` is, and it is the first overflow flag the port has ever had to model.**
// The `ADC` sets it, nothing after it touches it, and it survives the `RTL` —
// which would be an academic point except that the scheduler pushes `P` onto a
// thread's stack, so the bit becomes a byte of WRAM. `zamn_cosim run` found it
// as `$7E:0DE8  stock $40, native $00` and nothing else on the movie differing.
typedef struct {
  uint16_t a;  // 0..255
  bool n, z, c, v;
} RngResult;

// Draw the next byte. `carry_in` is the caller's carry, which the `ROL` and the
// `ADC` both consume.
void rng_next(Wram* w, bool carry_in, RngResult* out);

#endif
