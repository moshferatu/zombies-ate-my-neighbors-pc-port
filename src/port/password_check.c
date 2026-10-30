// $82:B018  a password checked, and $82:B0DE one written -- see
// port/password_check.h.

#include "port/password_check.h"

#include "port/bcd.h"
#include "port/coverage.h"

static uint8_t table(const Rom* rom, uint32_t at) {
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, at, &avail);
  return p && avail ? *p : 0;
}

// The letter at a place in the alphabet. The ROM's index is a byte.
static uint8_t letter(const Rom* rom, uint8_t place) {
  return table(rom, PASSWORD_ALPHABET + place);
}

// The first letter and the third change places, which makes a word of the
// middle two and a word of the outer two.
static void swap_ends(Wram* w, PortCpu* c) {
  const uint8_t first = wram_r8(w, W_PASSWORD);
  const uint8_t third = wram_r8(w, W_PASSWORD + 2);
  wram_w8(w, W_PASSWORD, third);
  wram_w8(w, W_PASSWORD + 2, first);
  c->x = first;
  c->a = (uint16_t)((c->a & 0xff00u) | third);
  set_nz8(c, third);
}

// `LDA #$0001 : STA $1E7C : SEC`.
static void turned_down(Wram* w, PortCpu* c, uint16_t x) {
  c->x = x;
  c->a = PASSWORD_TURNED_DOWN_LEVEL;
  set_nz16(c, c->a);
  wram_w16(w, W_PASSWORD_LEVEL, c->a);
  set_c(c, true);
}

PasswordFate password_check(Wram* w, const Rom* rom, PortCpu* c,
                            PasswordWork* k) {
  c->a = wram_r16(w, W_PASSWORD);
  cmp16(c, c->a, PASSWORD_CHEAT_FIRST);
  k->blocks[PW_FIRST]++;
  if (c->a == PASSWORD_CHEAT_FIRST) {
    c->a = wram_r16(w, W_PASSWORD + 2);
    cmp16(c, c->a, PASSWORD_CHEAT_SECOND);
    k->blocks[PW_SECOND]++;
    if (c->a == PASSWORD_CHEAT_SECOND) {
      PORT_COVER(password_cheat);
      wram_w16(w, W_PASSWORD_LEVEL, PASSWORD_CHEAT_LEVEL);
      set_c(c, false);
      k->blocks[PW_CHEAT]++;
      c->pc = PASSWORD_CHEAT_RTL_PC;
      return PASSWORD_IS_CHEAT;
    }
  }
  k->blocks[PW_TAKEN]++;

  swap_ends(w, c);
  k->blocks[PW_SWAP]++;

  // The middle two letters, against each level's pair.
  const uint16_t middle = wram_r16(w, W_PASSWORD);
  int group = -1;
  for (int i = 0; i < PASSWORD_GROUPS && group < 0; i++) {
    const uint32_t at = PASSWORD_GROUP_TABLE + 2u * (uint32_t)i;
    c->y = table(rom, at);
    c->a = (uint16_t)(letter(rom, (uint8_t)c->y) |
                      letter(rom, table(rom, at + 1)) << 8);
    cmp16(c, c->a, middle);
    k->blocks[PW_LEVEL_TEST]++;
    if (c->a == middle) {
      group = i;
      k->blocks[PW_TAKEN]++;
    } else {
      k->blocks[PW_LEVEL_NEXT]++;
      if (i + 1 < PASSWORD_GROUPS) k->blocks[PW_TAKEN]++;
    }
  }
  if (group < 0) {
    PORT_COVER(password_no_level);
    turned_down(w, c, 2 * PASSWORD_GROUPS);
    k->blocks[PW_FAIL]++;
    c->pc = PASSWORD_NO_LEVEL_RTL_PC;
    return PASSWORD_NO_LEVEL;
  }
  wram_w16(w, W_PASSWORD_LEVEL, (uint16_t)password_level(group));

  // The outer two, against each count's pair moved along the alphabet by
  // the level's own two steps.
  wram_w16(w, W_PASSWORD_GROUP, (uint16_t)(2 * group));
  k->blocks[PW_COUNT_HEAD]++;
  const uint16_t outer = wram_r16(w, W_PASSWORD + 2);
  const uint32_t steps = PASSWORD_GROUP_OFFSETS + 2u * (uint32_t)group;
  const uint8_t step = table(rom, steps);
  int count = 0;
  for (int j = 0; j < PASSWORD_VARIANTS && count == 0; j++) {
    const uint32_t at = PASSWORD_VARIANT_TABLE + 2u * (uint32_t)j;
    const uint8_t base = table(rom, at);
    const uint8_t place = (uint8_t)(base + step);
    // An 8-bit `CLC : ADC`, the last thing to write V.
    set_v(c, ((base ^ place) & (step ^ place) & 0x80u) != 0);
    c->y = place;
    c->a = (uint16_t)(
        letter(rom, place) |
        letter(rom, (uint8_t)(table(rom, at + 1) + table(rom, steps + 1)))
            << 8);
    cmp16(c, c->a, outer);
    k->blocks[PW_COUNT_TEST]++;
    if (c->a == outer) {
      count = j + 1;
      k->blocks[PW_TAKEN]++;
    } else {
      k->blocks[PW_COUNT_NEXT]++;
      if (j + 1 < PASSWORD_VARIANTS) k->blocks[PW_TAKEN]++;
    }
  }
  wram_w16(w, W_PASSWORD_TRIED,
           (uint16_t)(count ? count : PASSWORD_VARIANTS + 1));
  if (count == 0) {
    PORT_COVER(password_no_count);
    turned_down(w, c, 2 * PASSWORD_VARIANTS);
    k->blocks[PW_FAIL]++;
    c->pc = PASSWORD_NO_COUNT_RTL_PC;
    return PASSWORD_NO_COUNT;
  }

  c->a = (uint16_t)count;
  k->blocks[PW_FOUND]++;
  if (count == PASSWORD_FULL_VARIANT) {
    PORT_COVER(password_ten);
    c->a = PASSWORD_FULL_VICTIMS;
    k->blocks[PW_TEN]++;
  } else {
    PORT_COVER(password_good);
    k->blocks[PW_TAKEN]++;
  }
  wram_w16(w, W_PASSWORD_LEFT, c->a);
  wram_w16(w, W_PASSWORD_GATE, c->a);
  swap_ends(w, c);
  set_c(c, false);
  k->blocks[PW_TAIL]++;
  c->pc = PASSWORD_CHECK_RTL_PC;
  return PASSWORD_GOOD;
}

// A byte of bank `$82` read at `base` by an 8-bit index, counting a page
// crossed.
static uint8_t indexed(const Rom* rom, uint32_t base, uint8_t index,
                       int* crossed) {
  if ((base & 0xffu) + index > 0xffu) (*crossed)++;
  return table(rom, base + index);
}

// `CLC : ADC` of two bytes, as the 8-bit accumulator does it.
static uint8_t add8(PortCpu* c, uint8_t a, uint8_t b) {
  const unsigned sum = (unsigned)a + b;
  const uint8_t r = (uint8_t)sum;
  set_c(c, sum > 0xffu);
  set_v(c, ((a ^ r) & (b ^ r) & 0x80u) != 0);
  return r;
}

#define PW_SAVED_A 0x1f9cu  // the neighbours each player saved
#define PW_SAVED_B 0x1f9eu

int password_write(Wram* w, const Rom* rom, PortCpu* c) {
  int crossed = 0;
  // `PHB : PEA $0082 : PLB`: the pushed `$00` stays on the stack until the
  // end.
  const uint8_t bank = c->db;
  push8(w, c, bank);
  push16(w, c, PASSWORD_BANK);
  c->db = pull8(w, c);

  // The level's pair: `LDA $1E7C : LSR : DEC : DEC : TAX : STX $0038`.
  const uint16_t group = (uint16_t)((wram_r16(w, W_PASSWORD_LEVEL) >> 1) - 2);
  wram_w16(w, W_PASSWORD_GROUP, group);
  const uint8_t g = (uint8_t)group;  // `SEP #$30`
  const uint8_t second =
      indexed(rom, PASSWORD_ALPHABET,
              indexed(rom, PASSWORD_GROUP_TABLE + 1, g, &crossed), &crossed);
  const uint8_t first =
      indexed(rom, PASSWORD_ALPHABET,
              indexed(rom, PASSWORD_GROUP_TABLE, g, &crossed), &crossed);
  const uint8_t middle[2] = {first, second};

  // The count's: `SED : LDA $1F9C : CLC : ADC $1F9E : SEC : SBC #1 : CLD :
  // ASL : TAX`.
  bool carry = false, adjusted = false, overflow = false;
  uint16_t left = bcd_add16(wram_r16(w, PW_SAVED_A), wram_r16(w, PW_SAVED_B),
                            &carry, &adjusted);
  carry = true;
  left = bcd_sub16(left, 1, &carry, &overflow);
  const uint8_t v = (uint8_t)(left << 1);  // `SEP #$30` keeps the low byte
  const uint8_t gs = (uint8_t)wram_r16(w, W_PASSWORD_GROUP);  // `LDY $0038`
  uint8_t place = add8(c,
                       indexed(rom, PASSWORD_VARIANT_TABLE + 1, v, &crossed),
                       indexed(rom, PASSWORD_GROUP_OFFSETS + 1, gs, &crossed));
  const uint8_t fourth = indexed(rom, PASSWORD_ALPHABET, place, &crossed);
  place = add8(c, indexed(rom, PASSWORD_VARIANT_TABLE, v, &crossed),
               indexed(rom, PASSWORD_GROUP_OFFSETS, gs, &crossed));
  const uint8_t third = indexed(rom, PASSWORD_ALPHABET, place, &crossed);

  // Stored as two words, `$1EA4` cleared, and the first and third letters
  // swapped by bytes: the same swap `$82:B018` undoes.
  wram_w8(w, W_PASSWORD, third);
  wram_w8(w, W_PASSWORD + 1, middle[1]);
  wram_w8(w, W_PASSWORD + 2, middle[0]);
  wram_w8(w, W_PASSWORD + 3, fourth);
  wram_w16(w, W_PASSWORD + 4, 0);
  PORT_COVER(password_written);

  c->a = (uint16_t)(fourth << 8 | third);
  c->x = middle[0];
  c->y = place;
  // `PLB : PLB`: the `$00`, then the caller's bank, which sets N and Z.
  (void)pull8(w, c);
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  c->pc = PASSWORD_WRITE_RTL_PC;
  return crossed;
}
