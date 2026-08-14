#include "assets/actor.h"

#include <string.h>

// The lists sit in bank $9F, right after the level records; the loader forms
// each far pointer as `$9F:<record field>`.
#define ACTOR_LIST_BANK 0x9f0000u

static uint16_t rd16(const uint8_t* p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

// Point at a list, or report it empty. A field of $0000 (or any low-half
// address) is not a bank-$9F ROM pointer, so the level simply has no such list.
static const uint8_t* list_ptr(const Rom* rom, uint16_t field, uint32_t* avail,
                               bool* present) {
  if (field < 0x8000) { *present = false; *avail = 0; return NULL; }
  *present = true;
  return rom_ptr(rom, ACTOR_LIST_BANK | field, avail);
}

int actors_read(const Rom* rom, const LevelHeader* h, ActorLists* out) {
  memset(out, 0, sizeof *out);
  bool present;
  uint32_t avail;
  const uint8_t* p;

  // Actors — 10-byte records, ended by a zero id. `$81:80EC` reads +0 (id),
  // +1 (x) and +3 (y); +5/+6/+8 fill in the rest of the record it hands on.
  p = list_ptr(rom, h->list_1c, &avail, &present);
  if (present) {
    if (!p) return ACTOR_ERR_ADDRESS;
    for (uint32_t off = 0;; off += 10) {
      if (off + 10 > avail) return ACTOR_ERR_ADDRESS;  // ran off the bank
      if (p[off] == 0) break;                          // type 0 terminates
      if (out->actor_count >= ACTOR_LIST_MAX) return ACTOR_ERR_OVERFLOW;
      ActorPlacement* a = &out->actors[out->actor_count++];
      a->type = p[off];
      a->x = rd16(p + off + 1);
      a->y = rd16(p + off + 3);
      a->flags = p[off + 5];
      a->behavior = (uint32_t)rd16(p + off + 6) | ((uint32_t)p[off + 8] << 16);
    }
  }

  // Victims and the tail behind them — 12-byte records either way. Two readers
  // walk this one list and stop on different fields, so this loop runs to
  // `$81:81F6`'s terminator (+0 zero) and hands over to `spawns` at the point
  // `$82:DB46`'s gate on +6 would have stopped. See `assets/actor.h`.
  p = list_ptr(rom, h->list_1e, &avail, &present);
  if (present) {
    if (!p) return ACTOR_ERR_ADDRESS;
    bool counting = true;  // $82:DB46 is still walking alongside us
    for (uint32_t off = 0;; off += 12) {
      if (off + 12 > avail) return ACTOR_ERR_ADDRESS;
      if (rd16(p + off + 0) == 0) break;  // x 0 terminates, as $81:81F6
      uint16_t index = rd16(p + off + 6);
      if (index == 0 || index > VICTIM_INDEX_MAX) counting = false;
      if (counting) {
        if (out->victim_count >= VICTIM_LIST_MAX) return ACTOR_ERR_OVERFLOW;
        VictimPlacement* vv = &out->victims[out->victim_count++];
        vv->x = rd16(p + off + 0);
        vv->y = rd16(p + off + 2);
        vv->field4 = rd16(p + off + 4);
        vv->index = index;
        vv->behavior = (uint32_t)rd16(p + off + 8) | ((uint32_t)p[off + 10] << 16);
      } else {
        if (out->spawn_count >= SPAWN_LIST_MAX) return ACTOR_ERR_OVERFLOW;
        SpawnPlacement* s = &out->spawns[out->spawn_count++];
        s->x = rd16(p + off + 0);
        s->y = rd16(p + off + 2);
        s->field4 = rd16(p + off + 4);
        s->behavior = (uint32_t)rd16(p + off + 8) | ((uint32_t)p[off + 10] << 16);
      }
    }
  }

  // Objects — 5-byte records, ended by a zero x. `$80:C9A5` reads +0 (x),
  // +2 (y) and +4 (type).
  p = list_ptr(rom, h->list_20, &avail, &present);
  if (present) {
    if (!p) return ACTOR_ERR_ADDRESS;
    for (uint32_t off = 0;; off += 5) {
      if (off + 5 > avail) return ACTOR_ERR_ADDRESS;
      if (rd16(p + off + 0) == 0) break;  // x 0 terminates
      if (out->object_count >= OBJECT_LIST_MAX) return ACTOR_ERR_OVERFLOW;
      ObjectPlacement* o = &out->objects[out->object_count++];
      o->x = rd16(p + off + 0);
      o->y = rd16(p + off + 2);
      o->type = p[off + 4];
    }
  }

  return ACTOR_OK;
}
