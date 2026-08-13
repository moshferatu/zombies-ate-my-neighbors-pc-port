#include "port/player.h"

#include "port/apu.h"
#include "port/collide.h"  // ACTOR_DP_PLAYER, ACTOR_DP_RECORD
#include "port/coverage.h"
#include "port/floor.h"
#include "port/thread.h"
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

// --- $80:D1FF  player_state_normal ------------------------------------------

// `--twin-stick`, one word per player. Zero until a frontend arms it, and zero
// is what the game itself means by no direction, so an unarmed build cannot tell
// this is here. See `port/player.h`.
static uint16_t psn_aim[2];

void player_set_aim(uint16_t player, uint16_t dir) {
  psn_aim[(player >> 1) & 1u] = dir;
}

static void psn_nz(PlayerStateRegs* out, uint16_t v) {
  out->n = (v & 0x8000u) != 0;
  out->z = v == 0;
}

// `CPY #$0006` and `CPY #$000D`. The carry each leaves is not read by any
// branch after the one it belongs to -- but it is still in the register at the
// `RTS`, because nothing between here and the four countdowns writes carry.
static void psn_cpy(PlayerStateRegs* out, uint16_t y, uint16_t imm) {
  uint16_t r = (uint16_t)(y - imm);
  out->n = (r & 0x8000u) != 0;
  out->z = r == 0;
  out->c = y >= imm;
}

// `LDA $xx : BEQ over : DEC $xx`. The `LDA` sets N and Z from the old value and
// the `DEC` overwrites them with the new one. **Neither touches carry**, which
// is why the exit's carry belongs to whatever ran before this block.
static bool psn_countdown(Wram* w, uint16_t addr, PlayerStateRegs* out) {
  uint16_t v = wram_r16(w, addr);
  out->a = v;
  psn_nz(out, v);
  if (v == 0) return false;
  v = (uint16_t)(v - 1u);
  wram_w16(w, addr, v);
  psn_nz(out, v);
  return v == 0;
}

// An edge: set this frame, clear last. `$1C` is written outside this routine.
static bool psn_edge(const Wram* w, uint16_t dp, uint16_t buttons,
                     uint16_t mask) {
  if ((buttons & mask) == 0) return false;
  return (wram_r16(w, dp + PSN_DP_PREV) & mask) == 0;
}

bool player_state_normal_supported(const Wram* w, uint16_t dp) {
  // Bit 15 is the only bit of `$006E,X` the routine rewrites, so the raw word
  // answers for `$1A` here and the guard needs nothing the routine computes.
  uint16_t player = wram_r16(w, dp + PSN_DP_PLAYER);
  uint16_t buttons = wram_r16(w, (uint32_t)(W_JOY_RAW + player));
  return !psn_edge(w, dp, buttons, PSN_BTN_USE);
}

void player_state_normal(Wram* w, const Rom* rom, uint16_t dp,
                         PlayerStateRegs* out) {
  // $80:D1FF, and it is the first instruction: the ground, before a button.
  FloorRegs floor;
  floor_effect(w, rom, dp, &floor);
  out->a = floor.a;
  out->x = floor.x;
  out->y = floor.y;
  out->c = floor.c;

  wram_w16(w, dp + PSN_DP_FIRE_A, 0);
  wram_w16(w, dp + PSN_DP_FIRE_B, 0);

  uint16_t player = wram_r16(w, dp + PSN_DP_PLAYER);
  out->x = player;  // `LDX $0E`
  uint16_t buttons = wram_r16(w, (uint32_t)(W_JOY_RAW + player));
  wram_w16(w, dp + PSN_DP_BUTTONS, buttons);

  // $80:D20D. `LDY $1CBC,X : BMI` -- a negative weapon index skips the block,
  // and so does not holding fire.
  uint16_t weapon = wram_r16(w, (uint32_t)(W_PLAYER_WEAPON + player));
  out->y = weapon;
  if ((weapon & 0x8000u) != 0) {
    PORT_COVER(psn_weapon_none);
  } else if ((buttons & PSN_BTN_FIRE) == 0) {
    PORT_COVER(psn_not_firing);
  } else {
    // `LDA $1CBC,X : ASL A : TAY : LDA ($64),Y`. The `ASL` writes carry, and
    // is the last thing to unless one of the two `CPY`s below runs.
    uint16_t slot = (uint16_t)(weapon << 1);
    out->c = (weapon & 0x8000u) != 0;
    out->y = slot;
    uint16_t base = wram_r16(w, dp + PSN_DP_INVENTORY);
    uint16_t left = wram_r16(w, (uint32_t)(uint16_t)(base + slot));
    out->a = left;
    psn_nz(out, left);

    if (left == 0) {
      // $80:D222. The game writing back over what it read from the pad.
      PORT_COVER(psn_weapon_empty);
      buttons = (uint16_t)(buttons | PSN_EMPTY_FLAG);
      wram_w16(w, (uint32_t)(W_JOY_RAW + player), buttons);
      wram_w16(w, dp + PSN_DP_BUTTONS, buttons);
      out->a = buttons;
      psn_nz(out, buttons);
    } else {
      PORT_COVER(psn_weapon_ready);
      buttons = (uint16_t)(buttons & (uint16_t)~PSN_EMPTY_FLAG);
      wram_w16(w, (uint32_t)(W_JOY_RAW + player), buttons);
      wram_w16(w, dp + PSN_DP_BUTTONS, buttons);

      // `LDA #$4000 : LDY $1CBC,X` -- A is the value to file and Y is what
      // decides which of the two words it goes in.
      out->a = PSN_BTN_FIRE;
      out->y = weapon;
      psn_cpy(out, weapon, PSN_WEAPON_BAND_LO);
      if (weapon < PSN_WEAPON_BAND_LO) {
        PORT_COVER(psn_fire_low);
        wram_w16(w, dp + PSN_DP_FIRE_A, PSN_BTN_FIRE);
      } else {
        psn_cpy(out, weapon, PSN_WEAPON_BAND_HI);
        if (weapon >= PSN_WEAPON_BAND_HI) {
          PORT_COVER(psn_fire_high);
          wram_w16(w, dp + PSN_DP_FIRE_A, PSN_BTN_FIRE);
        } else {
          PORT_COVER(psn_fire_band);
          wram_w16(w, dp + PSN_DP_FIRE_B, PSN_BTN_FIRE);
        }
      }
    }
  }

  // $80:D250. The direction, and the last non-zero one.
  uint16_t dir = wram_r16(w, (uint32_t)(W_JOY_DIR + player));
  out->a = dir;
  psn_nz(out, dir);
  wram_w16(w, dp + PSN_DP_DIR, dir);
  if (dir != 0) {
    PORT_COVER(psn_dir_moving);
    wram_w16(w, dp + PSN_DP_DIR_HELD, dir);
  } else {
    PORT_COVER(psn_dir_still);
  }
  // ...and the aim on top of it, which is `--twin-stick`. After the store above
  // and not instead of it: `$24` keeps the walk either way, and a frame that is
  // both walking and aiming has to leave the *aim* in `$26`, or the walk would
  // win and the two would be coupled again.
  //
  // Not marked for coverage: the corpus cannot reach it, and a branch no input
  // distinguishes is exactly what the census exists to complain about.
  //
  // This is `$80:FF80`'s `LDA $80FFBC,X : BEQ +2 : STA $26` in C. The one thing
  // it does not copy is the `LDA` itself, which lands in A and the flags on the
  // stock path even when the aim is centred, where this leaves both as the
  // walk's. Nothing reads either: `$80:D259  LDA $1A` is the next instruction
  // both ways.
  const uint16_t aim = psn_aim[(player >> 1) & 1u];
  if (aim != 0) {
    out->a = aim;
    psn_nz(out, aim);
    wram_w16(w, dp + PSN_DP_DIR_HELD, aim);
  }

  // $80:D259 player_input_buttons. Four edges. The first two are independent;
  // the third and fourth are exclusive, because `$80:D28C` branches past the
  // fourth once the third has fired.
  if (psn_edge(w, dp, buttons, PSN_BTN_WEAPON)) {
    PORT_COVER(psn_press_weapon);
    WeaponSelectRegs r;
    weapon_select_next(w, rom, dp, &r);
    out->a = r.a;
    out->x = r.x;
    out->y = r.y;
    out->n = r.n;
    out->z = r.z;
    out->c = r.c;
  }
  if (psn_edge(w, dp, buttons, PSN_BTN_ITEM)) {
    PORT_COVER(psn_press_item);
    WeaponSelectRegs r;
    item_select_next(w, rom, dp, &r);
    out->a = r.a;
    out->x = r.x;
    out->y = r.y;
    out->n = r.n;
    out->z = r.z;
    out->c = r.c;
  }

  if (psn_edge(w, dp, buttons, PSN_BTN_USE)) {
    // `$80:EAE1 item_use` is not ported, so `player_state_normal_supported`
    // declined this frame and the ROM ran it. **Unreachable**, which is why
    // there is no coverage site here: the guard makes it one the harness can
    // never take, and such a site dilutes the number rather than measuring
    // anything. The branch itself has to stay, because `$80:D28C` is what makes
    // the fourth button exclusive with the third.
  } else if (psn_edge(w, dp, buttons, PSN_BTN_SPAWN)) {
    // $80:D29C. A flag that has to be clear, and is cleared when it is not, so
    // the first press after something sets it is swallowed.
    uint16_t flag = wram_r16(w, (uint32_t)(W_PLAYER_FLAG + player));
    out->x = player;  // `LDX $0E` again
    out->a = flag;
    psn_nz(out, flag);
    if (flag != 0) {
      PORT_COVER(psn_spawn_swallowed);
      wram_w16(w, (uint32_t)(W_PLAYER_FLAG + player), 0);
    } else {
      // $80:D2A8. Three words onto this page, then a thread that inherits
      // them -- `thread_spawn` copies the caller's first five.
      PORT_COVER(psn_spawn);
      uint16_t half = (uint16_t)(player >> 1);
      out->c = (player & 1u) != 0;  // `LSR A`
      out->a = half;
      psn_nz(out, half);
      wram_w16(w, dp + 0x00u, half);
      wram_w16(w, dp + 0x02u, rom_word(rom, PSN_SPAWN_ARG_TABLE + player));
      wram_w16(w, dp + 0x04u, PSN_SPAWN_ARG_COUNT);
      thread_spawn(w, rom, PSN_SPAWN_ENTRY, PSN_SPAWN_BANK, dp);

      ApuSfxRegs sfx;
      apu_play_sfx(w, PSN_SPAWN_SFX, dp, &sfx);
      out->a = sfx.a;
      out->x = sfx.x;
      out->y = sfx.y;
      out->n = sfx.n;
      out->z = sfx.z;
      out->c = sfx.c;
    }
  } else {
    PORT_COVER(psn_press_none);
  }

  // $80:D2C9. Four countdowns. They write A, N and Z and **never carry**, so
  // whatever set carry above is what the caller gets.
  psn_countdown(w, dp + PSN_DP_T0, out);
  psn_countdown(w, dp + PSN_DP_T1, out);
  psn_countdown(w, dp + PSN_DP_T2, out);
  if (psn_countdown(w, dp + PSN_DP_T3, out)) {
    // `DEC $56 : BNE : STZ $54` -- `STZ` sets no flag, so the `DEC`'s stand.
    PORT_COVER(psn_t3_expired);
    wram_w16(w, dp + PSN_DP_T3_TAIL, 0);
  }
}
