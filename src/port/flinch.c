// $80:D089, $80:D0BF  a player's pictures, a list of them -- see
// port/flinch.h.

#include "port/flinch.h"

#include "port/coverage.h"
#include "port/oam.h"  // the display record's fields

static uint16_t field(const Wram* w, uint16_t page, uint16_t at) {
  return wram_r16(w, (uint16_t)(page + at));
}

static uint16_t table(const Rom* rom, uint16_t at) {
  return rom_word(rom, ((uint32_t)FLINCH_BANK << 16) | at);
}

static uint16_t place_of(const Wram* w, const Rom* rom, uint16_t page,
                         uint16_t list, uint16_t at) {
  uint16_t place = table(rom, (uint16_t)(list + at));
  if (field(w, page, FLINCH_DP_STATE) == FLINCH_STATE_OTHER)
    place = (uint16_t)(place + FLINCH_OTHER_PICTURES);
  return place;
}

// Are the entry `at` of `list` and the picture it names in the cartridge?
static bool entry_ok(const Wram* w, const Rom* rom, uint16_t page,
                     uint16_t list, uint16_t at) {
  const uint16_t pictures = field(w, page, FLINCH_DP_PICTURES);
  if (list < 0x8000u || pictures < 0x8000u ||
      at > 0x0100 || (uint32_t)list + at + 3 > 0xffffu)
    return false;
  return (uint32_t)pictures + place_of(w, rom, page, list, at) + 1 <= 0xffffu;
}

bool flinch_frame_supported(const Wram* w, const Rom* rom, uint16_t page) {
  return entry_ok(w, rom, page, field(w, page, FLINCH_DP_LIST),
                  (uint16_t)(field(w, page, FLINCH_DP_AT) +
                             FLINCH_ENTRY_BYTES));
}

bool flinch_begin_supported(const Wram* w, const Rom* rom, uint16_t page) {
  if (field(w, page, FLINCH_DP_PICTURES) == FLINCH_LONE_PICTURES) {
    const uint16_t who = field(w, page, FLINCH_DP_WHO);
    return who < FLINCH_WHO_BYTES && (who & 1) == 0;
  }
  const uint16_t facing = field(w, page, FLINCH_DP_FACING);
  if (facing >= FLINCH_FACING_BYTES || (facing & 1) != 0) return false;
  return entry_ok(w, rom, page,
                  table(rom, (uint16_t)(FLINCH_LISTS + facing)), 0);
}

static void show(Wram* w, const Rom* rom, PortCpu* c, FlinchWork* k,
                 uint16_t at);

void flinch_frame(Wram* w, const Rom* rom, PortCpu* c, FlinchWork* k) {
  const uint16_t page = c->d;
  set_c(c, false);
  const uint16_t at =
      adc16(c, field(w, page, FLINCH_DP_AT), FLINCH_ENTRY_BYTES);
  wram_w16(w, (uint16_t)(page + FLINCH_DP_AT), at);
  k->blocks[FN_NEXT]++;
  show(w, rom, c, k, at);
}

void flinch_begin(Wram* w, const Rom* rom, PortCpu* c, FlinchWork* k) {
  const uint16_t page = c->d;
  const uint16_t pictures = field(w, page, FLINCH_DP_PICTURES);
  cmp16(c, pictures, FLINCH_LONE_PICTURES);
  k->blocks[FN_WHICH]++;
  if (pictures == FLINCH_LONE_PICTURES) {
    PORT_COVER(flinch_lone);
    k->blocks[FN_TAKEN]++;
    c->y = table(rom, (uint16_t)(FLINCH_LONE_PLACES +
                                 field(w, page, FLINCH_DP_WHO)));
    c->x = field(w, page, FLINCH_DP_RECORD);
    wram_w16(w, (uint16_t)(c->x + ACTOR_META),
             table(rom, (uint16_t)(pictures + c->y)));
    c->a = FLINCH_LONE_FRAMES;
    set_nz16(c, c->a);
    k->blocks[FN_LONE]++;
    c->pc = FLINCH_LONE_SLEEP_PC;
    return;
  }
  PORT_COVER(flinch_begun);
  wram_w16(w, (uint16_t)(page + FLINCH_DP_LIST),
           table(rom, (uint16_t)(FLINCH_LISTS +
                                 field(w, page, FLINCH_DP_FACING))));
  wram_w16(w, (uint16_t)(page + FLINCH_DP_AT), 0);
  k->blocks[FN_LIST]++;
  show(w, rom, c, k, 0);
}

// `$80:D09C`: the entry `at` of the list shown, and A its frames.
static void show(Wram* w, const Rom* rom, PortCpu* c, FlinchWork* k,
                 uint16_t at) {
  const uint16_t page = c->d;
  const uint16_t list = field(w, page, FLINCH_DP_LIST);
  uint16_t place = table(rom, (uint16_t)(list + at));
  c->x = field(w, page, FLINCH_DP_STATE);
  cmp16(c, c->x, FLINCH_STATE_OTHER);
  k->blocks[FN_HEAD]++;
  if (c->x == FLINCH_STATE_OTHER) {
    PORT_COVER(flinch_other_state);
    set_c(c, false);
    place = adc16(c, place, FLINCH_OTHER_PICTURES);
    k->blocks[FN_OTHER]++;
  } else {
    k->blocks[FN_TAKEN]++;
  }

  c->x = field(w, page, FLINCH_DP_RECORD);
  wram_w16(w, (uint16_t)(c->x + ACTOR_META),
           table(rom, (uint16_t)(field(w, page, FLINCH_DP_PICTURES) + place)));
  c->y = (uint16_t)(at + 2);
  c->a = table(rom, (uint16_t)(list + c->y));
  set_nz16(c, c->a);
  k->blocks[FN_SHOW]++;
  if (c->a == 0) {
    PORT_COVER(flinch_done);
    k->blocks[FN_TAKEN]++;
    c->pc = FLINCH_DONE_PC;
    return;
  }
  PORT_COVER(flinch_next);
  c->pc = FLINCH_SLEEP_PC;
}
