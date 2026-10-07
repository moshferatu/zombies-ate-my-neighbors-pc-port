// $82:B018  a password checked -- see port/password_check.h.

#include "port/password_check.h"

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
