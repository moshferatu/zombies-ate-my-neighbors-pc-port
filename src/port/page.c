// $80:D13A  a player's page, begun -- see port/page.h.

#include "port/page.h"

#include "port/coverage.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

void player_page_begin(Wram* w, PortCpu* c, PageWork* k) {
  PORT_COVER(player_page_begun);
  set_c(c, false);
  wram_w16(w, W_SPAWN_LOAD,
           adc16(c, wram_r16(w, W_SPAWN_LOAD), PAGE_PLAYER_WEIGHT));
  k->blocks[PG_HEAD]++;

  for (uint16_t at = PAGE_CLEAR_TO; at >= PAGE_CLEAR_FROM; at -= 2) {
    set_field(w, c, at, 0);
    k->blocks[PG_CLEAR]++;
    if (at != PAGE_CLEAR_FROM) k->blocks[PG_TAKEN]++;
  }

  const uint16_t slot = (uint16_t)(field(w, c, PAGE_DP_SLOT) << 1);
  c->x = slot;
  set_field(w, c, PAGE_DP_SIDE,
            wram_r16(w, (uint16_t)(W_SCORE_SLOT_SIDE + slot)));
  wram_w16(w, (uint16_t)(W_HUD_PANEL_ON + slot), 1);
  c->a = asl16(c, field(w, c, PAGE_DP_PLAYER_IN));
  set_field(w, c, PAGE_DP_PLAYER, c->a);
  c->y = c->a;
  k->blocks[PG_TAIL]++;
  c->pc = PAGE_BEGIN_ALLOC_PC;
}

// A `JSR` and its `RTS`: the return is left under the stack.
static void called_near(Wram* w, PortCpu* c, uint16_t ret) {
  push16(w, c, ret);
  (void)pull16(w, c);
}

bool player_page_records_supported(const Wram* w, uint16_t page,
                                   uint16_t record) {
  const uint16_t side = wram_r16(w, (uint16_t)(page + PAGE_DP_SIDE));
  const uint16_t player = wram_r16(w, (uint16_t)(page + PAGE_DP_PLAYER));
  if (side > 2 || (side & 1u) || player > 2 || (player & 1u)) return false;
  if (record >= 0x1f00 ||
      wram_r16(w, (uint16_t)(page + PAGE_DP_WEAPON_RECORD)) >= 0x1f00)
    return false;
  const uint16_t weapon = wram_r16(w, (uint16_t)(W_PLAYER_WEAPON + player));
  return (weapon & 0x8000u) != 0 || weapon < 0x10;
}

void player_record_file(Wram* w, PortCpu* c) {
  wram_w16(w, (uint16_t)(W_PLAYER_A_RECORD + c->x), c->a);
  cmp16(c, c->x, 0);
  if (c->x != 0) return;
  PORT_COVER(player_record_filed_first);
  c->a = wram_r16(w, W_SCHED_CUR_TASK);
  set_nz16(c, c->a);
  wram_w16(w, W_PLAYER_A_TASK, c->a);
}

void player_page_records(Wram* w, const Rom* rom, PortCpu* c,
                         PageRecords* did) {
  PORT_COVER(player_page_records);
  const uint16_t record = c->a;
  const uint16_t player = field(w, c, PAGE_DP_PLAYER);
  const uint16_t side = field(w, c, PAGE_DP_SIDE);
  const uint16_t weapon_record = field(w, c, PAGE_DP_WEAPON_RECORD);
  const uint16_t thread = wram_r16(w, W_SCHED_CUR_TASK);
  set_field(w, c, PAGE_DP_RECORD, record);

  // `$80:A8A4`.
  push8(w, c, PAGE_BANK);
  push16(w, c, (uint16_t)(PAGE_FILE_AT + 3));
  wram_w16(w, (uint16_t)(W_PLAYER_A_RECORD + player), record);
  did->first_player = player == 0;
  if (did->first_player) wram_w16(w, W_PLAYER_A_TASK, thread);
  (void)pull16(w, c);
  (void)pull8(w, c);

  const uint16_t x = field(w, c, PAGE_DP_GIVEN_X);
  const uint16_t y = field(w, c, PAGE_DP_GIVEN_Y);
  set_field(w, c, PAGE_DP_X, x);
  set_field(w, c, PAGE_DP_Y, y);
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), thread);
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID),
           rom_word(rom, PAGE_COLLIDE_IDS + side));
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS),
           (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) |
                      ACTOR_DRAW));

  wram_w16(w, (uint16_t)(weapon_record + ACTOR_THREAD), thread);
  wram_w16(w, (uint16_t)(weapon_record + ACTOR_COLLIDE_ID),
           rom_word(rom, PAGE_COLLIDE_IDS + player));
  set_field(w, c, PAGE_DP_FACING, PAGE_FACING_DOWN);
  set_field(w, c, PAGE_DP_76, 0);
  set_field(w, c, PAGE_DP_INVENTORY, rom_word(rom, PAGE_INVENTORIES + player));
  set_field(w, c, PAGE_DP_ITEMS, rom_word(rom, PAGE_ITEMS + player));

  // `$80:EA42`: the hand weapon collides with nothing.
  called_near(w, c, PAGE_UNARM_RET);
  wram_w16(w, (uint16_t)(weapon_record + ACTOR_COLLIDE_ID), 0);

  // `$80:EA4B`, with the weapon chosen in A.
  called_near(w, c, PAGE_ARM_RET);
  c->x = player;
  c->y = weapon_record;
  const uint16_t weapon = wram_r16(w, (uint16_t)(W_PLAYER_WEAPON + player));
  c->a = asl16(c, weapon);
  did->armed = (weapon & 0x8000u) == 0;
  if (did->armed) {
    PORT_COVER(player_page_records_armed);
    const uint16_t pictures = rom_word(rom, PAGE_WEAPON_PICTURES + side);
    set_field(w, c, PAGE_DP_PICTURES_AT, pictures);
    set_field(w, c, PAGE_DP_WEAPON_PICTURES,
              bus_r16(w, rom,
                      ((uint32_t)PAGE_BANK << 16) |
                          (uint16_t)(pictures + c->a)));
    wram_w16(w, (uint16_t)(weapon_record + ACTOR_META_BANK),
             PAGE_WEAPON_PICTURE_BANK);
    c->x = side;
    c->a = PAGE_WEAPON_PICTURE_BANK;
    set_nz16(c, c->a);
  } else {
    PORT_COVER(player_page_records_unarmed);
  }
  c->pc = PAGE_RECORDS_STAND_PC;
}
