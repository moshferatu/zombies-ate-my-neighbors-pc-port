#include "port/score.h"

#include "port/bcd.h"
#include "port/coverage.h"

// ---------------------------------------------------------------------------
// Decimal-mode addition
// ---------------------------------------------------------------------------

// The 65816's 16-bit decimal `ADC` now lives in `port/bcd.h`, because the score
// stopped being the only thing in the game that uses it — `$80:F87B` adds a
// pickup amount to an inventory counter the same way. What stays here is the
// coverage mark, because what an adjust *means* is the caller's to say.
//
// Decimal and binary addition only disagree once a digit runs past 9, so
// without this mark `bcd_add16` could be replaced by `a + value` and every diff
// would still pass. It does not fire on either of the two awards
// `movies/level1-rescue.zmv` produces, which is a fact about the movie's scores
// and not about the arithmetic — proven by replacing the `$6` adjust with a `$7`
// and watching all 3,740 checks still pass.
static uint16_t score_bcd_add16(uint16_t a, uint16_t value, bool* carry) {
  bool adjusted = false;
  uint16_t result = bcd_add16(a, value, carry, &adjusted);
  if (adjusted) PORT_COVER(score_digit_carry);
  return result;
}

// ---------------------------------------------------------------------------
// $80:C7C2  score_slot
// ---------------------------------------------------------------------------

// Which score slot owns this side, or `SCORE_SLOT_NONE`. Two comparisons and
// three constants; the interesting part is that it is a search rather than an
// index, so the two players can own the slots either way round.
static uint16_t score_slot(const Wram* w, uint16_t side) {
  if (side == wram_r16(w, W_SCORE_SLOT_SIDE)) return 0;
  if (side == wram_r16(w, W_SCORE_SLOT_SIDE + 2)) return 2;
  return SCORE_SLOT_NONE;
}

// ---------------------------------------------------------------------------
// $80:C7D9  score_add
// ---------------------------------------------------------------------------

bool score_add(Wram* w, const Rom* rom, bool second_side, uint16_t amount,
               bool carry_in, ScoreResult* out) {
  // `$80:C7D9  BMI $C7E0 : LDA #$0000 / LDA #$0002`. The accumulator the caller
  // arrived with is thrown away immediately; only its sign is read.
  uint16_t side = second_side ? 2 : 0;

  // `PHX : JSR $C7C2 : TAX`. The award spends the rest of the routine on the
  // stack, and X ends up holding the slot the search settled on — which is what
  // the caller gets back, whichever path ran.
  uint16_t slot = score_slot(w, side);
  out->x = slot;

  uint32_t at;
  switch (rom_word(rom, SCORE_ADD_TABLE + slot)) {
    case SCORE_ADD_SLOT0:
      PORT_COVER(score_slot_0);
      at = W_PLAYER_SCORE;
      break;
    case SCORE_ADD_SLOT1:
      PORT_COVER(score_slot_1);
      at = W_PLAYER_SCORE + 4;
      break;
    case SCORE_ADD_DISCARD:
      // `$80:C817  PLA : RTL`. The award comes back off the stack into A and
      // nothing else happens — no score belongs to this side.
      PORT_COVER(score_discard);
      out->a = amount;
      out->n = (amount & 0x8000) != 0;
      out->z = amount == 0;
      out->c = carry_in;  // nothing on this path touches it
      return true;
    default:
      // Unreachable on a stock ROM, and deliberately not marked for coverage.
      // `score_slot` returns one of exactly three values and all three of the
      // table's entries are ported, so this can only fire against a ROM whose
      // table has been repointed. The coverage report exists to name branches
      // *a movie* has not reached; a site nothing can reach would sit in it
      // forever saying nothing. A decline is still counted, by the harness.
      return false;
  }

  // `PLA : SED : CLC : ADC <slot> : STA <slot>` — the award plus the low half.
  bool carry = false;  // the `CLC`
  uint16_t lo = score_bcd_add16(amount, wram_r16(w, at), &carry);
  wram_w16(w, at, lo);
  if (!carry) {
    // `BCC $C7FF`: the high half is not touched, so the `ADC` above is the last
    // instruction to set anything. `CLD` and `RTL` set nothing.
    out->a = lo;
    out->n = (lo & 0x8000) != 0;
    out->z = lo == 0;
    out->c = false;  // the branch was taken because it was clear
    return true;
  }

  // `LDA #$0000 : ADC <slot+2> : STA <slot+2>` — the carry into the high half,
  // still in decimal mode, so the +1 is the carry itself.
  PORT_COVER(score_carry);
  uint16_t hi = score_bcd_add16(0, wram_r16(w, at + 2), &carry);
  wram_w16(w, at + 2, hi);
  out->a = hi;
  out->n = (hi & 0x8000) != 0;
  out->z = hi == 0;
  out->c = carry;
  return true;
}
