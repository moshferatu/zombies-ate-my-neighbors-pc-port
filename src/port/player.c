#include "port/player.h"

#include "port/apu.h"
#include "port/collide.h"  // ACTOR_DP_PLAYER, ACTOR_DP_RECORD
#include "port/coverage.h"
#include "port/oam.h"      // ACTOR_META_BANK
#include "port/wram.h"

// A word read through bank $80, which is what `LDA ($2C),Y` does here.
//
// The bank is half one thing and half another — `$0000-$1FFF` mirrors WRAM
// `$7E:0000-$1FFF` and `$8000-$FFFF` is the LoROM window — and these two
// routines use the *same instruction* on both halves: the inventory walk reads
// `$1CCC`, and the weapon-data lookup three instructions later reads `$FDA0`.
// Splitting on the address is not a shortcut, it is what the address decoder
// does.
static uint16_t bank80_word(const Wram* w, const Rom* rom, uint16_t addr) {
  return addr < 0x2000 ? wram_r16(w, addr) : rom_word(rom, 0x800000u | addr);
}

// ---------------------------------------------------------------------------
// $80:EA4B  the newly selected weapon's data
// ---------------------------------------------------------------------------

// Nine instructions with an early-out on the front. `weapon` is what
// `$80:EA96` has just stored, still in A.
static void weapon_apply(Wram* w, const Rom* rom, uint16_t dp, uint16_t weapon) {
  // `ASL A : BCS $EA62`. The shift is only there to test bit 15 — nothing uses
  // the doubled value on this path — so `WEAPON_NONE` leaves at once and the
  // three stores below do not happen.
  if (weapon & 0x8000) {
    PORT_COVER(weapon_no_data);
    return;
  }

  // `LDX $0C : LDA $FD9C,X : STA $2C : LDA ($2C),Y : STA $12`.
  uint16_t index = wram_r16(w, (uint32_t)dp + PLAYER_DP_TABLE_INDEX);
  uint16_t table = rom_word(rom, WEAPON_DATA_TABLES + index);
  wram_w16(w, (uint32_t)dp + PLAYER_DP_PTR, table);
  wram_w16(w, (uint32_t)dp + PLAYER_DP_WEAPON_DATA,
           bank80_word(w, rom, (uint16_t)(table + weapon * 2)));

  // `LDY $0A : LDA #$0090 : STA $000A,Y`. The player's own display record, and
  // the same field `sprite_build_oam` reads to find a metasprite — so this is
  // the third handler-side routine to reach out of its own direct page into the
  // display list, and the first that does it to change how something *looks*.
  uint16_t record = wram_r16(w, (uint32_t)dp + ACTOR_DP_RECORD);
  wram_w16(w, (uint32_t)record + ACTOR_META_BANK, WEAPON_META_BANK);
}

// ---------------------------------------------------------------------------
// $80:EA63  weapon_select_next
// ---------------------------------------------------------------------------

void weapon_select_next(Wram* w, const Rom* rom, uint16_t dp,
                        WeaponSelectRegs* out) {
  // `LDA #$000F : STA $2E : LDX $0E : LDA $EAA4,X : STA $2C`.
  uint16_t tries = WEAPON_SCAN_TRIES;
  uint16_t player = wram_r16(w, (uint32_t)dp + ACTOR_DP_PLAYER);
  uint16_t base = rom_word(rom, PLAYER_INVENTORY_BASES + player);
  wram_w16(w, (uint32_t)dp + PLAYER_DP_PTR, base);

  // `LDA $1CBC,X : BMI $EA83`. Holding nothing starts the walk at slot 0;
  // holding something starts it at the slot *after* that one, which is what
  // makes this "next" rather than "first".
  uint16_t held = wram_r16(w, W_PLAYER_WEAPON + player);
  uint16_t y;
  if (held & 0x8000) {
    PORT_COVER(weapon_none_held);
    y = 0;
  } else {
    // `ASL A : TAY : BRA $EA7C`, and `$EA7C` is the advance.
    y = (uint16_t)(held * 2 + 2);
    if (y == WEAPON_SCAN_WRAP) {
      PORT_COVER(weapon_scan_wrap);
      y = 0;
    }
  }

  // `$EA86  DEC $2E : BNE $EA78` is the top of the loop, so the counter comes
  // down *before* the first slot is looked at and fourteen slots get fifteen
  // decrements — the walk can start anywhere and still come back to where it
  // began.
  uint16_t found;
  for (;;) {
    tries = (uint16_t)(tries - 1);
    if (tries == 0) {
      // `LDA #$FFFF`. Fifteen tries and nothing in any of them.
      PORT_COVER(weapon_none_found);
      found = WEAPON_NONE;
      break;
    }
    if (bank80_word(w, rom, (uint16_t)(base + y)) != 0) {
      // `$EA8F  TYA : LSR A` — back from a byte offset to a slot number.
      found = (uint16_t)(y >> 1);
      break;
    }
    PORT_COVER(weapon_scan_empty);
    y = (uint16_t)(y + 2);
    if (y == WEAPON_SCAN_WRAP) {
      PORT_COVER(weapon_scan_wrap);
      y = 0;
    }
  }
  wram_w16(w, (uint32_t)dp + PLAYER_DP_SCAN, tries);

  // `CMP $1CBC,X : BEQ $EAA3`. The one-weapon case, and the reason holding B
  // looked like it did nothing.
  if (found == held) {
    PORT_COVER(weapon_unchanged);
    out->a = found;
    out->x = player;
    out->y = y;
    out->n = false;  // the CMP's difference is zero
    out->z = true;
    out->c = true;   // ...and equal sets carry
    return;
  }

  PORT_COVER(weapon_changed);
  wram_w16(w, W_PLAYER_WEAPON + player, found);
  weapon_apply(w, rom, dp, found);

  // `LDA #$0012 : JSL apu_play_sfx : RTS`. Everything the caller gets back is
  // the sound effect's, including N and Z — which describe the player's own
  // direct page, because that is what `apu_play_sfx`'s `PLD` restores.
  ApuSfxRegs sfx;
  apu_play_sfx(w, WEAPON_SFX_SWITCH, dp, &sfx);
  out->a = sfx.a;
  out->x = sfx.x;
  out->y = sfx.y;
  out->n = sfx.n;
  out->z = sfx.z;
  out->c = sfx.c;
}

// ---------------------------------------------------------------------------
// $80:EAA8  item_select_next
// ---------------------------------------------------------------------------

void item_select_next(Wram* w, const Rom* rom, uint16_t dp,
                      WeaponSelectRegs* out) {
  // `LDA #$000D : STA $2E : LDX $0E`. No `STA $2C` here: the weapon search parks
  // its base on the page because it is about to overwrite `$2C` with a
  // weapon-data pointer and needs it back; this one reads `$66` in place and the
  // page keeps it between calls, which is why `$66` is a field and `$2C` is
  // scratch.
  uint16_t tries = ITEM_SCAN_TRIES;
  uint16_t player = wram_r16(w, (uint32_t)dp + ACTOR_DP_PLAYER);
  uint16_t base = wram_r16(w, (uint32_t)dp + PLAYER_DP_ITEMS);

  // `LDA $1CC0,X : BMI $EAC3`, and `$EAC3` is the wrap's own `LDY #$0000`, so
  // holding nothing starts at slot 0 and holding something starts at the slot
  // after it. Identical to `$80:EAB2`'s twin twenty-nine bytes up.
  uint16_t held = wram_r16(w, W_PLAYER_ITEM + player);
  uint16_t y;
  if (held & 0x8000) {
    PORT_COVER(item_none_held);
    y = 0;
  } else {
    y = (uint16_t)(held * 2 + 2);
    if (y == ITEM_SCAN_WRAP) {
      PORT_COVER(item_scan_wrap);
      y = 0;
    }
  }

  uint16_t found;
  for (;;) {
    tries = (uint16_t)(tries - 1);
    if (tries == 0) {
      PORT_COVER(item_none_found);
      found = ITEM_NONE;
      break;
    }
    // `LDA ($66),Y` — a direct-page *pointer*, where the weapon search uses
    // `($2C),Y`. The same addressing mode through the same data bank, so it gets
    // the same decode: `$1D0C` and `$1D2C` are both below `$2000` and land in
    // the WRAM mirror, and a hack that moved the array into ROM would still read
    // right.
    if (bank80_word(w, rom, (uint16_t)(base + y)) != 0) {
      // `$EACF  TYA : LSR A`.
      found = (uint16_t)(y >> 1);
      break;
    }
    PORT_COVER(item_scan_empty);
    y = (uint16_t)(y + 2);
    if (y == ITEM_SCAN_WRAP) {
      PORT_COVER(item_scan_wrap);
      y = 0;
    }
  }
  wram_w16(w, (uint32_t)dp + PLAYER_DP_SCAN, tries);

  // `CMP $1CC0,X : BEQ $EAE0`.
  if (found == held) {
    PORT_COVER(item_unchanged);
    out->a = found;
    out->x = player;
    out->y = y;
    out->n = false;
    out->z = true;
    out->c = true;
    return;
  }

  // `STA $1CC0,X : LDA #$0012 : JSL apu_play_sfx : RTS`. No `JSR $EA4B` on this
  // side — an item has no data table and does not change what the player is
  // drawn as — so the store and the noise are the whole of it.
  PORT_COVER(item_changed);
  wram_w16(w, W_PLAYER_ITEM + player, found);

  ApuSfxRegs sfx;
  apu_play_sfx(w, ITEM_SFX_SWITCH, dp, &sfx);
  out->a = sfx.a;
  out->x = sfx.x;
  out->y = sfx.y;
  out->n = sfx.n;
  out->z = sfx.z;
  out->c = sfx.c;
}
