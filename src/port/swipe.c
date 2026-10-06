// $81:E8F1 and $81:E979  a swipe, and the tiles it cuts -- see
// port/swipe.h.

#include "port/swipe.h"

#include "port/coverage.h"
#include "port/frontend.h"  // the frame count
#include "port/terrain.h"
#include "port/thread.h"

static uint16_t field(const Wram* w, const PortCpu* c, uint16_t at) {
  return wram_r16(w, (uint16_t)(c->d + at));
}

static void set_field(Wram* w, const PortCpu* c, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + at), v);
}

// A word of the thread's own bank, which is where its pointers point.
static uint16_t table_word(const Rom* rom, uint16_t at) {
  return rom_word(rom, ((uint32_t)SWIPE_BANK << 16) | at);
}

void swipe_begin(Wram* w, const Rom* rom, PortCpu* c, SwipeBeginWork* k) {
  set_field(w, c, SWIPE_DP_FLAG_OPS, SWIPE_FLAG_OPS);
  set_field(w, c, SWIPE_DP_PICTURES, SWIPE_PICTURES);
  const uint16_t owner = field(w, c, SWIPE_DP_OWNER);
  const uint16_t owner_x = wram_r16(w, (uint16_t)(owner + ACTOR_X));
  const uint16_t owner_y = wram_r16(w, (uint16_t)(owner + ACTOR_Y));
  set_field(w, c, SWIPE_DP_OWNER_X, owner_x);
  set_field(w, c, SWIPE_DP_OWNER_Y, owner_y);

  SlotAllocRegs slot;
  actor_slot_alloc(w, c->db, &slot);
  if (slot.c) {  // none free, and the ROM goes on with what is no record
    k->declined = true;
    return;
  }
  PORT_COVER(swipe_began);
  const uint16_t record = slot.a;
  k->record = record;
  set_field(w, c, SWIPE_DP_RECORD, record);
  const uint16_t parity = (uint16_t)(wram_r16(w, W_FRAME_COUNT) & 1u);
  set_field(w, c, SWIPE_DP_PARITY, parity);

  // In front of its owner, the way it faces.
  const uint16_t way = field(w, c, SWIPE_DP_WAY);
  const uint32_t reach = SWIPE_REACH + (uint16_t)(way << 1);
  const uint16_t x = (uint16_t)(owner_x + rom_word(rom, reach));
  const uint16_t y = (uint16_t)(owner_y + rom_word(rom, reach + 2));
  set_field(w, c, SWIPE_DP_X, x);
  set_field(w, c, SWIPE_DP_Y, y);
  set_field(w, c, SWIPE_DP_TILE_X, (uint16_t)(owner_x >> 3));
  set_field(w, c, SWIPE_DP_TILE_Y, (uint16_t)(owner_y >> 3));
  wram_w16(w, (uint16_t)(record + ACTOR_X), x);
  wram_w16(w, (uint16_t)(record + ACTOR_Z), 0);
  wram_w16(w, (uint16_t)(record + ACTOR_Y), y);
  wram_w16(w, (uint16_t)(record + ACTOR_META_BANK), SWIPE_META_BANK);
  wram_w16(w, (uint16_t)(record + ACTOR_META), SWIPE_META);
  wram_w16(w, (uint16_t)(record + ACTOR_THREAD), wram_r16(w, W_SWIPE_THREAD));
  wram_w16(w, (uint16_t)(record + ACTOR_COLLIDE_ID),
           rom_word(rom, SWIPE_COLLIDE_IDS + field(w, c, SWIPE_DP_PLAYER)));
  uint16_t flags =
      (uint16_t)(wram_r16(w, (uint16_t)(record + ACTOR_FLAGS)) | ACTOR_DRAW);

  // No handler.
  c->a = 0;
  c->y = 0;
  thread_set_handler(w, c);

  // `$81:FF05`: the way's bits set or cleared, and its picture of two.
  const uint16_t op = table_word(rom, (uint16_t)(SWIPE_FLAG_OPS + (way << 1)));
  if (op & 0x8000u) {
    PORT_COVER(swipe_as_drawn);
    flags &= op;
  } else {
    PORT_COVER(swipe_turned_over);
    k->turned_over = true;
    flags |= op;
  }
  wram_w16(w, (uint16_t)(record + ACTOR_FLAGS), flags);
  set_c(c, false);
  c->y = asl16(c, adc16(c, table_word(rom, (uint16_t)(SWIPE_FLAG_OPS + 2 +
                                                    (way << 1))),
                        parity));
  c->x = record;
  c->a = table_word(rom, (uint16_t)(SWIPE_PICTURES + c->y));
  set_nz16(c, c->a);
  wram_w16(w, (uint16_t)(record + ACTOR_META), c->a);
  c->pc = SWIPE_BEGIN_RTS_PC;
}

void swipe_cut(Wram* w, const Rom* rom, PortCpu* c, SwipeCutWork* k) {
  set_field(w, c, SWIPE_DP_CUT, 0);
  c->x = field(w, c, SWIPE_DP_WAY);
  const uint16_t list = rom_word(rom, SWIPE_TILE_LISTS + c->x);
  set_field(w, c, SWIPE_DP_LIST, list);
  uint16_t left = table_word(rom, list);
  set_field(w, c, SWIPE_DP_LEFT, left);
  c->y = 2;

  for (;;) {
    left--;
    set_field(w, c, SWIPE_DP_LEFT, left);
    if (left & 0x8000u) break;

    set_c(c, false);
    const uint16_t col = adc16(c, field(w, c, SWIPE_DP_TILE_X),
                               table_word(rom, (uint16_t)(list + c->y)));
    set_field(w, c, SWIPE_DP_AT_X, col);
    set_c(c, false);
    const uint16_t row = adc16(c, field(w, c, SWIPE_DP_TILE_Y),
                               table_word(rom, (uint16_t)(list + c->y + 2)));
    set_field(w, c, SWIPE_DP_AT_Y, row);
    c->y = (uint16_t)(c->y + 4);
    k->tiles++;

    TileAttrsRegs tile;
    tile_attrs_at_tile(w, col, row, &tile);
    k->overflow_unknown = true;
    set_c(c, false);  // the `ASL` of a tile's number
    c->x = 0;
    if (!(tile.a & SWIPE_ATTR_FIRST)) {
      c->x = 2;
      k->seconds++;
      if (!(tile.a & SWIPE_ATTR_SECOND)) {
        PORT_COVER(swipe_tile_left);
        continue;
      }
      PORT_COVER(swipe_cut_second);
    } else {
      PORT_COVER(swipe_cut_first);
    }

    if (k->cuts == SWIPE_CUTS_MAX) {
      k->declined = true;
      return;
    }
    map_tile_put(w, rom,
                 (uint16_t)(rom_word(rom, SWIPE_CUT_TILES + c->x) |
                            rom_word(rom, SWIPE_CUT_BITS + c->x)),
                 col, row, &k->put[k->cuts++]);
    set_field(w, c, SWIPE_DP_CUT,
              (uint16_t)(field(w, c, SWIPE_DP_CUT) + 1));
  }

  c->pc = SWIPE_CUT_RTS_PC;
  c->a = field(w, c, SWIPE_DP_CUT);
  set_nz16(c, c->a);
  if (c->a == 0) {
    PORT_COVER(swipe_cut_nothing);
    return;
  }

  // They are counted for the player, and heard.
  PORT_COVER(swipe_cut_counted);
  k->other_player =
      field(w, c, SWIPE_DP_PLAYER) != wram_r16(w, W_SWIPE_FIRST_PLAYER);
  const uint16_t counts =
      (uint16_t)(W_SWIPE_CUT_COUNTS + (k->other_player ? 2 : 0));
  wram_w16(w, counts, (uint16_t)(c->a + wram_r16(w, counts)));
  ApuSfxRegs sound;
  apu_play_sfx(w, SWIPE_SFX, c->d, &sound);
  k->played = true;
  k->overflow_unknown = true;
  c->a = sound.a;
  c->x = sound.x;
  c->y = sound.y;
  c->p = (uint8_t)((c->p & ~(PORT_P_N | PORT_P_Z | PORT_P_C)) |
                   (sound.n ? PORT_P_N : 0) | (sound.z ? PORT_P_Z : 0) |
                   (sound.c ? PORT_P_C : 0));
}
