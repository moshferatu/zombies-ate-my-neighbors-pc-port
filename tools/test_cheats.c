// `src/cheats.h`, without the frontend: the flags, the patches going into an
// image and coming back out of it, what a tick holds and what it leaves alone,
// the demo, and the two routines of the port that a cheat reaches into.
//
// Against a cartridge built here to the header's description, so it runs with
// no ROM; and against the real one if it is there (or named as the argument),
// which is the only thing that can say the twelve patches are aimed at what
// they claim to be.

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/rom.h"
#include "cheats.h"
#include "port/collide.h"
#include "port/oam.h"
#include "port/wram.h"

static int failures;

static void fail(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  printf("FAIL: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  failures++;
}

#define ROM_SIZE 0x100000u

// What the header says this ROM has, and nothing else: every patch's `was`,
// the demo's `LDA #$9CB2`, the 24 direct pages, and the one entry of the
// player's jump table the port's half of `--invincible` is behind.
static uint8_t* synthetic_rom(void) {
  uint8_t* rom = (uint8_t*)calloc(1, ROM_SIZE);
  for (int i = 0; i < CHEAT_PATCH_COUNT; i++)
    memcpy(rom + cheat_rom_offset(cheat_patches[i].addr), cheat_patches[i].was, cheat_patches[i].len);
  static const uint8_t lda[3] = {0xa9, 0xb2, 0x9c};
  memcpy(rom + cheat_rom_offset(CHEAT_DEMO_JOB_LDA), lda, sizeof lda);
  for (int s = 0; s < CHEAT_THREAD_SLOTS; s++) {
    const uint16_t dp = (uint16_t)(0x0100 + s * 0x80);
    rom[cheat_rom_offset(CHEAT_THREAD_DP_TABLE) + s * 2] = (uint8_t)dp;
    rom[cheat_rom_offset(CHEAT_THREAD_DP_TABLE) + s * 2 + 1] = (uint8_t)(dp >> 8);
  }
  const size_t entry = cheat_rom_offset(0x80f808) + 0x0a * 2;
  rom[entry] = (uint8_t)PLAYER_COLLIDE_QUEUE;
  rom[entry + 1] = (uint8_t)(PLAYER_COLLIDE_QUEUE >> 8);
  return rom;
}

static void test_flags(void) {
  Cheats c;
  cheats_init(&c);
  if (cheats_any(&c)) fail("a cheat is on before anybody asked");
  for (int i = 0; i < CHEAT_COUNT; i++) {
    char flag[64];
    snprintf(flag, sizeof flag, "--%s", cheat_flags[i]);
    if (!cheats_flag(&c, flag) || !c.on[i]) fail("%s did not turn its cheat on", flag);
    snprintf(flag, sizeof flag, "--no-%s", cheat_flags[i]);
    if (!cheats_flag(&c, flag) || c.on[i]) fail("%s did not turn its cheat off", flag);
  }
  if (!cheats_flag(&c, "--invincible-neighbours") || !c.on[CHEAT_NEIGHBORS])
    fail("the other spelling of a neighbour is not known");
  if (c.on[CHEAT_INVINCIBLE]) fail("--invincible-neighbours turned --invincible on");
  if (cheats_flag(&c, "--invincible-zombies") || cheats_flag(&c, "invincible") || cheats_flag(&c, "--frames"))
    fail("something that is not a cheat was taken for one");
}

static void test_patches(const uint8_t* pristine, size_t size, const char* what) {
  uint8_t* rom = (uint8_t*)malloc(size);
  memcpy(rom, pristine, size);
  Cheats c;
  cheats_init(&c);
  for (int i = 0; i < CHEAT_COUNT; i++) c.on[i] = true;
  const CheatId bad = cheats_rom_check(&c, rom, size);
  if (bad != CHEAT_COUNT) {
    fail("%s: --%s is aimed at something this image does not have", what, cheat_flags[bad]);
    free(rom);
    return;
  }
  if (!cheats_install(&c, rom, size)) fail("%s: the install refused an image the check passed", what);
  if (!c.installed || !port_cheats.invincible || !port_cheats.neighbors)
    fail("%s: installed, and the port was not told", what);
  // Every byte that changed is one a patch names, and every patch changed its.
  size_t changed = 0, named = 0;
  for (size_t i = 0; i < size; i++) changed += rom[i] != pristine[i];
  for (int i = 0; i < CHEAT_PATCH_COUNT; i++) {
    const CheatPatch* p = &cheat_patches[i];
    if (memcmp(rom + cheat_rom_offset(p->addr), p->now, p->len) != 0)
      fail("%s: the patch at $%06X is not in", what, (unsigned)p->addr);
    for (int b = 0; b < p->len; b++) named += p->was[b] != p->now[b];
  }
  if (changed != named) fail("%s: %u bytes changed and the patches name %u", what, (unsigned)changed, (unsigned)named);
  cheats_patch(&c, rom, false);
  if (memcmp(rom, pristine, size) != 0) fail("%s: taking the patches out did not give the image back", what);
  if (c.installed || port_cheats.invincible || port_cheats.neighbors)
    fail("%s: taken out, and the port still thinks otherwise", what);

  // One cheat on: only its patches.
  cheats_init(&c);
  c.on[CHEAT_LIVES] = true;
  cheats_install(&c, rom, size);
  changed = 0;
  for (size_t i = 0; i < size; i++) changed += rom[i] != pristine[i];
  if (changed != 3) fail("%s: --infinite-lives alone changed %u bytes, want 3", what, (unsigned)changed);
  if (port_cheats.invincible || port_cheats.neighbors) fail("%s: --infinite-lives told the port something", what);
  cheats_patch(&c, rom, false);

  // An image that is not this one is refused whole.
  rom[cheat_rom_offset(0x80e90f)] ^= 0xff;
  uint8_t* before = (uint8_t*)malloc(size);
  memcpy(before, rom, size);
  cheats_init(&c);
  for (int i = 0; i < CHEAT_COUNT; i++) c.on[i] = true;
  if (cheats_install(&c, rom, size)) fail("%s: an image with one site wrong was patched", what);
  if (cheats_rom_check(&c, rom, size) != CHEAT_AMMO) fail("%s: the wrong site was not laid at --infinite-ammo's door", what);
  if (memcmp(rom, before, size) != 0) fail("%s: a refused image was written to", what);
  if (c.installed || port_cheats.invincible) fail("%s: refused, and half on", what);
  // ...unless the cheat whose site it is has not been asked for.
  c.on[CHEAT_AMMO] = false;
  if (!cheats_install(&c, rom, size)) fail("%s: a site nobody asked for stopped the rest", what);
  cheats_patch(&c, rom, false);
  free(before);
  free(rom);
}

// Player `p` on the board: a record, the thread that owns it, and a page that
// says whose it is.
static uint16_t put_player(uint8_t* ram, const uint8_t* rom, int p, int slot) {
  const uint16_t rec = (uint16_t)(CHEAT_W_ACTOR_SLOTS + (30 - p) * 0x14);
  const size_t at = cheat_rom_offset(CHEAT_THREAD_DP_TABLE) + (size_t)slot * 2;
  const uint16_t dp = (uint16_t)(rom[at] | rom[at + 1] << 8);
  cheat_w16(ram, CHEAT_W_PLAYER_RECORD + p * 2, rec);
  cheat_w16(ram, rec + CHEAT_ACTOR_THREAD, (uint16_t)(slot * 2));
  cheat_w16(ram, dp + CHEAT_DP_PLAYER, (uint16_t)(p * 2));
  cheat_w16(ram, dp + CHEAT_DP_INVENTORY, (uint16_t)(CHEAT_W_INVENTORY + p * 0x20));
  cheat_w16(ram, dp + CHEAT_DP_ITEMS, (uint16_t)(CHEAT_W_ITEMS + p * 0x20));
  cheat_w16(ram, dp + CHEAT_DP_HURT_TIMER, 0xffff);
  return dp;
}

static void seed(uint8_t* ram, int p) {
  memset(ram + CHEAT_W_INVENTORY + p * 0x20, 0, CHEAT_WEAPON_SLOTS * 2);
  memset(ram + CHEAT_W_ITEMS + p * 0x20, 0, CHEAT_ITEM_SLOTS * 2);
  cheat_w16(ram, CHEAT_W_INVENTORY + p * 0x20, CHEAT_SEED_WEAPON_0);
  cheat_w16(ram, CHEAT_W_ITEMS + p * 0x20 + CHEAT_SEED_ITEM_SLOT * 2, CHEAT_SEED_ITEM_COUNT);
  cheat_w16(ram, CHEAT_W_HEALTH + p * 2, CHEAT_HEALTH_MAX);
}

static void test_tick(const uint8_t* pristine, size_t size, const char* what) {
  uint8_t* rom = (uint8_t*)malloc(size);
  memcpy(rom, pristine, size);
  uint8_t* ram = (uint8_t*)calloc(1, 0x20000);
  uint8_t* was = (uint8_t*)malloc(0x20000);
  Cheats c;

  // Nothing on: nothing written, whatever is there.
  cheats_init(&c);
  seed(ram, 0);
  put_player(ram, rom, 0, 3);
  memcpy(was, ram, 0x20000);
  cheats_tick(&c, ram, rom);
  if (memcmp(was, ram, 0x20000) != 0 || memcmp(rom, pristine, size) != 0) fail("%s: a tick with no cheat on wrote something", what);

  // Nobody on the board and no new game: every cheat on writes nothing.
  memset(ram, 0, 0x20000);
  cheat_w16(ram, CHEAT_W_INVENTORY + 4, 0x0020);  // so it is not a new game's
  for (int i = 0; i < CHEAT_COUNT; i++) c.on[i] = true;
  c.on[CHEAT_AMMO] = false;
  cheats_install(&c, rom, size);
  memcpy(was, ram, 0x20000);
  cheats_tick(&c, ram, rom);
  if (memcmp(was, ram, 0x20000) != 0) fail("%s: a tick with nobody on the board wrote something", what);

  // A page that does not say it is the player's is not believed.
  uint16_t dp = put_player(ram, rom, 0, 3);
  cheat_w16(ram, dp + CHEAT_DP_ITEMS, 0x1234);
  if (cheat_player_dp(ram, rom, 0) != 0) fail("%s: a page with the wrong inventory was taken for the player's", what);
  put_player(ram, rom, 0, 3);
  if (cheat_player_dp(ram, rom, 0) != dp) fail("%s: player 1's page is $%04X, not $%04X", what, cheat_player_dp(ram, rom, 0), dp);
  if (cheat_player_dp(ram, rom, 1) != 0) fail("%s: player 2 is not on the board and has a page", what);

  // On the board but not yet in the game, as the HUD's panel flag has it:
  // what is owed is kept, and nothing is given.
  cheats_tick(&c, ram, rom);
  if (cheat_r16(ram, CHEAT_W_INVENTORY) != 0) fail("%s: a player not in the game was given to", what);

  // Invincible and the shoes. Owed at the start, so give all as well.
  cheat_w16(ram, CHEAT_W_PANEL_ON, 1);
  cheat_w16(ram, CHEAT_W_HEALTH, 7);
  cheats_tick(&c, ram, rom);
  if (cheat_r16(ram, dp + CHEAT_DP_HURT_TIMER) != CHEAT_HURT_TIMER) fail("%s: the recovery timer is not held", what);
  if (cheat_r16(ram, CHEAT_W_HEALTH) != CHEAT_HEALTH_MAX) fail("%s: health is not held at ten", what);
  if (cheat_r16(ram, dp + CHEAT_DP_SHOES) != CHEAT_SHOES_ON) fail("%s: the shoes are not on", what);
  for (int s = 0; s < CHEAT_WEAPON_SLOTS; s++)
    if (cheat_r16(ram, CHEAT_W_INVENTORY + s * 2) != CHEAT_WEAPON_MAX) fail("%s: weapon %d was not given", what, s);
  for (int s = 0; s < CHEAT_ITEM_SLOTS; s++)
    if (cheat_r16(ram, CHEAT_W_ITEMS + s * 2) != (CHEAT_ITEM_UNUSED(s) ? 0 : CHEAT_ITEM_MAX)) fail("%s: item %d: %04x", what, s, cheat_r16(ram, CHEAT_W_ITEMS + s * 2));
  if (cheat_r16(ram, CHEAT_W_INVENTORY + 0x20) != 0) fail("%s: player 2, who is not there, was given something", what);
  // Health of nought is a player on the way down, and the $C000 one of the
  // mystery potion's draws sets (`$80:D3A8`) is not the shoes.
  cheat_w16(ram, CHEAT_W_HEALTH, 0);
  cheat_w16(ram, dp + CHEAT_DP_SHOES, 0xc000);
  cheats_tick(&c, ram, rom);
  if (cheat_r16(ram, CHEAT_W_HEALTH) != 0) fail("%s: a dead player was given health", what);
  if (cheat_r16(ram, dp + CHEAT_DP_SHOES) != 0xc000) fail("%s: the potion's $C000 was taken for the shoes", what);

  // Given once: what is spent stays spent...
  cheat_w16(ram, CHEAT_W_INVENTORY + 2, 0);
  cheat_w16(ram, CHEAT_W_INVENTORY + 4, 0x0123);
  cheats_tick(&c, ram, rom);
  if (cheat_r16(ram, CHEAT_W_INVENTORY + 2) != 0 || cheat_r16(ram, CHEAT_W_INVENTORY + 4) != 0x0123)
    fail("%s: give all gave twice", what);
  // ...until a save is loaded, or a new game is dealt -- with nobody on the
  // board yet, and to player 2 only once they are in the game: the game seeds
  // their inventory either way, and a filled one is what the ghost potion's
  // HUD reads for its count.
  cheats_loaded(&c);
  cheats_tick(&c, ram, rom);
  if (cheat_r16(ram, CHEAT_W_INVENTORY + 2) != CHEAT_WEAPON_MAX) fail("%s: a load was not given to", what);
  cheat_w16(ram, CHEAT_W_PLAYER_RECORD, 0);
  seed(ram, 0);
  seed(ram, 1);
  cheats_tick(&c, ram, rom);
  if (cheat_r16(ram, CHEAT_W_INVENTORY + 2) != CHEAT_WEAPON_MAX) fail("%s: a new game was not given to", what);
  if (cheat_r16(ram, CHEAT_W_INVENTORY + 0x20) != CHEAT_SEED_WEAPON_0 || cheat_r16(ram, CHEAT_W_INVENTORY + 0x20 + 26) != 0)
    fail("%s: player 2, who is not in the game, was given a new game's", what);
  cheat_w16(ram, CHEAT_W_PANEL_ON + 2, 1);
  cheats_tick(&c, ram, rom);
  if (cheat_r16(ram, CHEAT_W_INVENTORY + 0x20 + 26) != CHEAT_WEAPON_MAX || cheat_r16(ram, CHEAT_W_ITEMS + 0x20 + 18) != CHEAT_ITEM_MAX)
    fail("%s: player 2, once in the game, was not given to", what);

  // Ammo: what is held is raised, what is not is not.
  cheats_patch(&c, rom, false);
  cheats_init(&c);
  c.on[CHEAT_AMMO] = true;
  cheats_install(&c, rom, size);
  seed(ram, 0);
  cheats_tick(&c, ram, rom);
  if (cheat_r16(ram, CHEAT_W_INVENTORY) != CHEAT_WEAPON_MAX || cheat_r16(ram, CHEAT_W_INVENTORY + 2) != 0)
    fail("%s: infinite ammo: %04x %04x", what, cheat_r16(ram, CHEAT_W_INVENTORY), cheat_r16(ram, CHEAT_W_INVENTORY + 2));
  if (cheat_r16(ram, CHEAT_W_ITEMS + 14) != CHEAT_ITEM_MAX || cheat_r16(ram, CHEAT_W_ITEMS) != 0)
    fail("%s: infinite uses: %04x %04x", what, cheat_r16(ram, CHEAT_W_ITEMS + 14), cheat_r16(ram, CHEAT_W_ITEMS));
  cheats_patch(&c, rom, false);

  // The demo: everything out for as long as its job is filed, and back after.
  cheats_init(&c);
  for (int i = 0; i < CHEAT_COUNT; i++) c.on[i] = true;
  cheats_install(&c, rom, size);
  memset(ram, 0, 0x20000);
  seed(ram, 0);
  dp = put_player(ram, rom, 0, 5);
  cheat_w16(ram, CHEAT_W_PANEL_ON, 1);
  cheat_w16(ram, CHEAT_W_HEALTH, 4);
  cheat_w16(ram, CHEAT_W_JOBS + 5 * 4, CHEAT_DEMO_JOB);
  cheat_w16(ram, CHEAT_W_JOBS + 5 * 4 + 2, CHEAT_DEMO_JOB_BANK);
  memcpy(was, ram, 0x20000);
  cheats_tick(&c, ram, rom);
  if (memcmp(was, ram, 0x20000) != 0) fail("%s: the demo's WRAM was written to", what);
  if (memcmp(rom, pristine, size) != 0) fail("%s: the demo is not playing the cartridge's own code", what);
  if (port_cheats.invincible || port_cheats.neighbors) fail("%s: the demo is not playing the port's own code", what);
  cheat_w16(ram, CHEAT_W_JOBS + 5 * 4 + 2, 0x0081);  // the same address in another bank is another job
  cheats_tick(&c, ram, rom);
  if (!c.installed || !port_cheats.neighbors || memcmp(rom, pristine, size) == 0) fail("%s: the cheats did not come back after the demo", what);
  if (cheat_r16(ram, CHEAT_W_HEALTH) != CHEAT_HEALTH_MAX || cheat_r16(ram, CHEAT_W_INVENTORY + 2) != CHEAT_WEAPON_MAX)
    fail("%s: ...nor what they hold, nor what was owed", what);
  cheats_patch(&c, rom, false);

  free(was);
  free(ram);
  free(rom);
}

// The port's half. `victim_collide` and its sibling need no ROM; the player's
// entry is reached through the jump table, which the image here has one word of.
static void test_port(const uint8_t* rom_bytes, size_t size, const char* what) {
  Wram* w = (Wram*)calloc(1, sizeof *w);
  const Rom rom = {rom_bytes, (uint32_t)size};
  const uint16_t dp = 0x0400, rec = 0x1a38;
  static const uint16_t deaths[] = {VICTIM_ID_EVENT_2, VICTIM_ID_EVENT_3_A, VICTIM_ID_EVENT_3_B, VICTIM_ID_EVENT_3_C, VICTIM_ID_EVENT_4};
  for (int on = 0; on < 2; on++) {
    port_cheats.neighbors = on != 0;
    for (int i = 0; i < 5; i++) {
      ActorHandlerRegs r = {0};
      memset(w, 0, sizeof *w);
      wram_w16(w, dp + VICTIM_DP_RECORD, rec);
      wram_w16(w, rec + ACTOR_COLLIDE_ID, 1);
      r.a = deaths[i];
      victim_collide(w, dp, deaths[i], &r);
      const uint16_t event = wram_r16(w, dp + VICTIM_DP_EVENT);
      if (on ? event != 0 : event == 0) fail("%s: id $%02X %s a neighbour with the cheat %s", what, deaths[i], event ? "reached" : "spared", on ? "on" : "off");
      if (on && (r.c || wram_r16(w, rec + ACTOR_COLLIDE_ID) != 1)) fail("%s: id $%02X was not simply ignored", what, deaths[i]);
    }
    // The players still rescue, and `$FF` still takes it off the board.
    ActorHandlerRegs r = {0};
    memset(w, 0, sizeof *w);
    wram_w16(w, dp + VICTIM_DP_RECORD, rec);
    victim_collide(w, dp, VICTIM_ID_CLAIM_A, &r);
    if (wram_r16(w, dp + VICTIM_DP_EVENT) != VICTIM_EVENT_CLAIMED) fail("%s: player 1 cannot rescue (cheat %d)", what, on);
    wram_w16(w, dp + VICTIM_DP_EVENT, 0);
    victim_collide(w, dp, VICTIM_ID_CLAIM_B, &r);
    if (wram_r16(w, dp + VICTIM_DP_EVENT) != VICTIM_EVENT_CLAIMED || wram_r16(w, dp + VICTIM_DP_CLAIMANT) != 0x8000)
      fail("%s: player 2 cannot rescue (cheat %d)", what, on);
    wram_w16(w, dp + VICTIM_DP_EVENT, 0);
    victim_collide(w, dp, VICTIM_ID_EVENT_FF, &r);
    if (wram_r16(w, dp + VICTIM_DP_EVENT) != VICTIM_EVENT_FF) fail("%s: $FF does not clear a neighbour away (cheat %d)", what, on);

    // The bubbled one.
    static const uint16_t give_up[] = {A264_ID_GIVE_UP_A, A264_ID_GIVE_UP_B};
    for (int i = 0; i < 2; i++) {
      memset(w, 0, sizeof *w);
      wram_w16(w, dp + A264_DP_ARRAY_INDEX, 0xffff);
      victim_a264_collide(w, dp, give_up[i], &r);
      const uint16_t event = wram_r16(w, dp + A264_DP_EVENT);
      if (on ? event != 0 : event != A264_EVENT_GIVE_UP) fail("%s: a bubbled neighbour and id %d, cheat %d: event %d", what, give_up[i], on, event);
    }
    memset(w, 0, sizeof *w);
    wram_w16(w, dp + A264_DP_ARRAY_INDEX, 0xffff);
    victim_a264_collide(w, dp, A264_ID_GIVE_UP_FF, &r);
    if (wram_r16(w, dp + A264_DP_EVENT) != A264_EVENT_GIVE_UP) fail("%s: a bubbled neighbour cannot be cleared away (cheat %d)", what, on);
    victim_a264_collide(w, dp, A264_ID_CLAIM_A, &r);
    if (wram_r16(w, dp + A264_DP_EVENT) != A264_EVENT_CLAIMED) fail("%s: a bubbled neighbour cannot be rescued (cheat %d)", what, on);
  }
  port_cheats.neighbors = false;

  for (int on = 0; on < 2; on++) {
    port_cheats.invincible = on != 0;
    ActorHandlerRegs r = {0};
    uint32_t unported = 0;
    memset(w, 0, sizeof *w);
    r.a = 0x000a;
    if (!player_collide(w, &rom, 0x0100, 0x000a, &r, &unported)) { fail("%s: the port declined id $0A", what); break; }
    const uint16_t next = wram_r16(w, 0x0100 + PLAYER_DP_NEXT);
    if (on ? next != 0 : next != PLAYER_QUEUE_NEXT) fail("%s: id $0A queued $%04X with --invincible %s", what, next, on ? "on" : "off");
  }
  port_cheats.invincible = false;
  free(w);
}

int main(int argc, char** argv) {
  test_flags();
  uint8_t* synth = synthetic_rom();
  test_patches(synth, ROM_SIZE, "synthetic");
  test_tick(synth, ROM_SIZE, "synthetic");
  test_port(synth, ROM_SIZE, "synthetic");
  free(synth);

  const char* path = argc > 1 ? argv[1] : "Zombies Ate My Neighbors.sfc";
  FILE* f = fopen(path, "rb");
  if (!f) {
    printf("cheats: no cartridge at '%s'; synthetic checks only.\n", path);
  } else {
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* rom = (uint8_t*)malloc((size_t)n);
    if (fread(rom, 1, (size_t)n, f) != (size_t)n) fail("could not read all of '%s'", path);
    else {
      test_patches(rom, (size_t)n, "cartridge");
      test_tick(rom, (size_t)n, "cartridge");
      test_port(rom, (size_t)n, "cartridge");
      // The ceilings give all and infinite ammo use are the pickups' own.
      if (rom[cheat_rom_offset(0x80f896)] != 0x99 || rom[cheat_rom_offset(0x80f897)] != 0x09)
        fail("$80:F895 does not cap a weapon at $0999");
      if (rom[cheat_rom_offset(0x80f8f1)] != 0x99 || rom[cheat_rom_offset(0x80f8f2)] != 0x00)
        fail("$80:F8F0 does not cap an item at $0099");
    }
    fclose(f);
    free(rom);
  }

  if (failures) {
    printf("cheats: %d failure%s\n", failures, failures == 1 ? "" : "s");
    return 1;
  }
  printf("cheats: all checks passed\n");
  return 0;
}
