#include "assets/password.h"

#include <string.h>

// The four tables, read one byte at a time so a hacked ROM works and a
// truncated one is caught rather than walked off the end.
static bool rom_byte_at(const Rom* rom, uint32_t addr, uint8_t* out) {
  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, addr, &avail);
  if (!p || avail < 1) return false;
  *out = *p;
  return true;
}

// `LDY $B14A,X : LDA $B178,Y` and its three twins: every character in a password
// is one alphabet lookup through one index byte.
static bool alphabet(const Rom* rom, uint8_t index, char* out) {
  uint8_t c;
  if (index >= PASSWORD_ALPHABET_SIZE) return false;
  if (!rom_byte_at(rom, PASSWORD_ALPHABET + index, &c)) return false;
  *out = (char)c;
  return true;
}

int password_level(int group) {
  return PASSWORD_FIRST_LEVEL + PASSWORD_LEVEL_STEP * group;
}

int password_victims(int variant) {
  int n = variant + 1;
  return n == PASSWORD_FULL_VARIANT ? PASSWORD_FULL_VICTIMS : n;
}

bool password_spell(const Rom* rom, int group, int variant, char out[5]) {
  if (group < 0 || group >= PASSWORD_GROUPS) return false;
  if (variant < 0 || variant >= PASSWORD_VARIANTS) return false;

  // `$82:B046`'s pair, which is the half of a password that names the level.
  uint8_t g_lo, g_hi;
  if (!rom_byte_at(rom, PASSWORD_GROUP_TABLE + (uint32_t)group * 2, &g_lo) ||
      !rom_byte_at(rom, PASSWORD_GROUP_TABLE + (uint32_t)group * 2 + 1, &g_hi))
    return false;

  // `$82:B083`'s pair, offset by the group's own two bytes. The `CLC : ADC`
  // wraps in 8 bits — `SEP #$30` is on — and the result is an alphabet index
  // like any other, so a sum that lands past the end is a malformed table
  // rather than a different letter.
  uint8_t v_lo, v_hi, o_lo, o_hi;
  if (!rom_byte_at(rom, PASSWORD_VARIANT_TABLE + (uint32_t)variant * 2, &v_lo) ||
      !rom_byte_at(rom, PASSWORD_VARIANT_TABLE + (uint32_t)variant * 2 + 1, &v_hi) ||
      !rom_byte_at(rom, PASSWORD_GROUP_OFFSETS + (uint32_t)group * 2, &o_lo) ||
      !rom_byte_at(rom, PASSWORD_GROUP_OFFSETS + (uint32_t)group * 2 + 1, &o_hi))
    return false;

  // `$1EA0` = (c2, c1) and `$1EA2` = (c0, c3) after the swap at `$82:B02D`, so
  // unpicking that swap is the whole of turning table entries into typing.
  char c0, c1, c2, c3;
  if (!alphabet(rom, g_lo, &c2) || !alphabet(rom, g_hi, &c1) ||
      !alphabet(rom, (uint8_t)(v_lo + o_lo), &c0) ||
      !alphabet(rom, (uint8_t)(v_hi + o_hi), &c3))
    return false;

  out[0] = c0;
  out[1] = c1;
  out[2] = c2;
  out[3] = c3;
  out[4] = '\0';
  return true;
}

bool password_read(const Rom* rom, const char in[4], int* group, int* variant) {
  if (!memcmp(in, PASSWORD_CHEAT, 4)) {
    *group = -1;
    *variant = -1;
    return true;
  }
  for (int g = 0; g < PASSWORD_GROUPS; g++) {
    for (int v = 0; v < PASSWORD_VARIANTS; v++) {
      char spelled[5];
      if (!password_spell(rom, g, v, spelled)) return false;
      if (!memcmp(spelled, in, 4)) {
        *group = g;
        *variant = v;
        return true;
      }
    }
  }
  return false;
}
