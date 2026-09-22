// Does the right stick become the right nine bytes of 65816?
//
// `src/twinstick.h` is a ROM patch, and a ROM patch has the unpleasant property
// that a wrong byte is not a crash but a game that misbehaves twenty minutes in.
// So the two halves of it are checked here, separately, with no emulator:
//
//   * the **direction**, which is a stick's eight-way bits becoming the code the
//     game's own table at `$80:81F9` would have made of them. Nine inputs, nine
//     answers, and the eight that are directions have to be eight *different*
//     directions — a table read with the nibble built the wrong way round still
//     produces plausible-looking codes, and would aim up when asked for right;
//   * the **patch**, which is exact bytes. Asserting them one at a time is
//     tedious and it is the only check that means anything: `20 80 FF` is a
//     `JSR` to the stub and `20 68 FF` is a `JSR` into `--level`'s stub, and
//     nothing short of the byte tells the two apart.
//
// The refusals matter as much as the writes. `twin_install` promises that a
// cartridge it does not understand is left exactly as it came off disk, and a
// patch that half-applies before noticing is worse than one that does not apply,
// so every refusal below is checked for having written nothing at all.
//
// Everything runs against a synthetic cartridge — the table, the latch and a pad
// of `$FF` at the offsets the header names — so this needs no ROM and no
// copyrighted bytes. Hand it the real one as an argument and it repeats the
// install against that as well, which is the check that the offsets in the
// header are still where the game keeps those things.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/collide.h"  // ACTOR_DP_RECORD
#include "port/oam.h"      // ACTOR_META_BANK
#include "port/player.h"
#include "port/wram.h"
#include "twinstick.h"

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
// The end-of-bank pad `--level` and `--twin-stick` both take from: 88 bytes of
// `$FF` between the last code in bank $80 and the cartridge header at $80:FFC0.
#define PAD_AT 0x07f68u
#define PAD_LEN 88u
// ...and the eleven of it `--level` claims, which this must not touch.
#define LEVEL_STUB_AT 0x07f68u
#define LEVEL_STUB_LEN 11u

// The real table, which the synthetic cartridge carries so that `twin_dir` is
// answering the same question here as it does in the game.
static const uint8_t dir_table[16] = {0x00, 0x06, 0x0e, 0x00, 0x0a, 0x08,
                                      0x0c, 0x00, 0x02, 0x04, 0x10, 0x00,
                                      0x00, 0x00, 0x00, 0x00};

static uint8_t* make_rom(void) {
  uint8_t* rom = (uint8_t*)calloc(ROM_SIZE, 1);
  memcpy(rom + TWIN_DIR_TABLE, dir_table, sizeof dir_table);
  memcpy(rom + TWIN_LATCH, twin_latch_was, TWIN_LATCH_LEN);
  memcpy(rom + TWIN_LATCH1, twin_latch1_was, TWIN_LATCH1_LEN);
  // `$80:D4E9  LDA $24`, the state re-entry the stub sends the player to.
  rom[TWIN_REENTER] = 0xa5;
  rom[TWIN_REENTER + 1] = 0x24;
  memset(rom + PAD_AT, 0xff, PAD_LEN);
  return rom;
}

// --- the direction ----------------------------------------------------------

#define UP (1u << BTN_UP)
#define DOWN (1u << BTN_DOWN)
#define LEFT (1u << BTN_LEFT)
#define RIGHT (1u << BTN_RIGHT)

static void test_dir(void) {
  uint8_t* rom = make_rom();
  const struct {
    uint16_t bits;
    uint16_t code;
    const char* name;
  } want[] = {
      {0, 0x00, "centred"},        {UP, 0x02, "up"},
      {UP | RIGHT, 0x04, "up-right"}, {RIGHT, 0x06, "right"},
      {DOWN | RIGHT, 0x08, "down-right"}, {DOWN, 0x0a, "down"},
      {DOWN | LEFT, 0x0c, "down-left"}, {LEFT, 0x0e, "left"},
      {UP | LEFT, 0x10, "up-left"},
  };
  for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
    const uint16_t got = twin_dir(rom, want[i].bits);
    if (got != want[i].code)
      fail("%s should aim $%02X, got $%02X", want[i].name, want[i].code, got);
  }
  // Eight directions, eight answers. The point of this is the nibble: swap two
  // of its bits and every code above is still a legal direction, so only their
  // being all different and all in the right place catches it.
  for (size_t i = 1; i < sizeof want / sizeof *want; i++)
    for (size_t j = i + 1; j < sizeof want / sizeof *want; j++)
      if (twin_dir(rom, want[i].bits) == twin_dir(rom, want[j].bits))
        fail("%s and %s aim the same way", want[i].name, want[j].name);

  // The four the table answers zero for. Octant snapping cannot produce a
  // direction and its opposite, but nothing here depends on that being true:
  // the table says nothing, and nothing is what the stub declines to store.
  const uint16_t both[] = {LEFT | RIGHT, UP | DOWN, UP | DOWN | LEFT,
                           UP | DOWN | LEFT | RIGHT};
  for (size_t i = 0; i < sizeof both / sizeof *both; i++)
    if (twin_dir(rom, both[i]) != 0)
      fail("opposed directions $%03X should aim nowhere, got $%02X", both[i],
           twin_dir(rom, both[i]));

  // Buttons are not directions. A whole pad held down still aims by its D-pad.
  if (twin_dir(rom, 0xfff & ~(UP | DOWN | LEFT | RIGHT)) != 0)
    fail("every button but the D-pad still aimed somewhere");
  if (twin_dir(rom, 0xfff & ~(UP | DOWN | LEFT)) != 0x06)
    fail("every button and right should still aim right");
  free(rom);
}

// --- the patch --------------------------------------------------------------

static void expect(const uint8_t* rom, uint32_t at, uint8_t b, const char* what) {
  if (rom[at] != b)
    fail("$%05X should be $%02X (%s), is $%02X", at, b, what, rom[at]);
}

static void test_install(void) {
  uint8_t* rom = make_rom();
  uint8_t* was = (uint8_t*)malloc(ROM_SIZE);
  memcpy(was, rom, ROM_SIZE);

  if (!twin_install(rom, ROM_SIZE)) {
    fail("install refused a cartridge built to its own description");
    free(rom);
    free(was);
    return;
  }

  // The call site: JSR $FF80, then the six bytes it displaced, blanked.
  expect(rom, TWIN_LATCH + 0, 0x20, "JSR");
  expect(rom, TWIN_LATCH + 1, (uint8_t)TWIN_STUB_ADDR, "JSR low");
  expect(rom, TWIN_LATCH + 2, (uint8_t)(TWIN_STUB_ADDR >> 8), "JSR high");
  for (uint32_t i = 3; i < TWIN_LATCH_LEN; i++)
    expect(rom, TWIN_LATCH + i, 0xea, "NOP");
  // The monster's latch calls the same stub, and all eleven of its bytes go:
  // a `BRA +0` left behind would be harmless, but a `STA $26` would not.
  expect(rom, TWIN_LATCH1 + 0, 0x20, "monster JSR");
  expect(rom, TWIN_LATCH1 + 1, (uint8_t)TWIN_STUB_ADDR, "monster JSR low");
  expect(rom, TWIN_LATCH1 + 2, (uint8_t)(TWIN_STUB_ADDR >> 8), "monster JSR high");
  for (uint32_t i = 3; i < TWIN_LATCH1_LEN; i++)
    expect(rom, TWIN_LATCH1 + i, 0xea, "monster NOP");

  // The stub: the nine displaced bytes, verbatim...
  if (memcmp(rom + TWIN_STUB, twin_latch_was, TWIN_LATCH_LEN) != 0)
    fail("the stub does not open with the nine bytes it displaced");
  // ...and the copied `BEQ +2` still skipping exactly the `STA $26` that
  // follows it, which is the one thing about copying code that can go wrong.
  if (rom[TWIN_STUB + 5] != 0xf0 || rom[TWIN_STUB + 6] != 0x02 ||
      rom[TWIN_STUB + 7] != 0x85 || rom[TWIN_STUB + 8] != 0x26)
    fail("the copied branch no longer skips the store it was skipping");

  // ...then the override.
  expect(rom, TWIN_STUB + 9, 0xbf, "LDA long,X");
  expect(rom, TWIN_STUB + 10, (uint8_t)TWIN_AIM_ADDR, "aim low");
  expect(rom, TWIN_STUB + 11, (uint8_t)(TWIN_AIM_ADDR >> 8), "aim high");
  expect(rom, TWIN_STUB + 12, 0x80, "aim bank");
  expect(rom, TWIN_STUB + 13, 0xf0, "BEQ end");
  expect(rom, TWIN_STUB + 15, 0x48, "PHA");
  // `EOR` and not `CMP`, which is load-bearing: the routine's carry at the
  // `RTS` belongs to a `CPY` much further up, `CMP` would overwrite it, and the
  // port does not — so a `CMP` here is two engines disagreeing about a flag.
  // `zamn_cosim verify --twin-aim` is what says so.
  expect(rom, TWIN_STUB + 16, 0x45, "EOR dp");
  expect(rom, TWIN_STUB + 17, 0x26, "EOR $26");
  expect(rom, TWIN_STUB + 18, 0xf0, "BEQ pull");
  expect(rom, TWIN_STUB + 20, 0xa5, "LDA dp");
  expect(rom, TWIN_STUB + 21, 0x24, "LDA $24");
  expect(rom, TWIN_STUB + 22, 0xd0, "BNE pull");
  expect(rom, TWIN_STUB + 24, 0xa9, "LDA #");
  expect(rom, TWIN_STUB + 25, (uint8_t)TWIN_REENTER_ADDR, "re-entry low");
  expect(rom, TWIN_STUB + 26, (uint8_t)(TWIN_REENTER_ADDR >> 8), "re-entry high");
  expect(rom, TWIN_STUB + 27, 0x85, "STA dp");
  expect(rom, TWIN_STUB + 28, TWIN_DP_RESUME, "STA $28");
  expect(rom, TWIN_STUB + 29, 0x68, "PLA");
  expect(rom, TWIN_STUB + 30, 0x85, "STA dp");
  expect(rom, TWIN_STUB + 31, 0x26, "STA $26");
  expect(rom, TWIN_STUB + 32, 0x60, "RTS");
  if (TWIN_STUB_LEN != 33) fail("TWIN_STUB_LEN is %d, not 33", (int)TWIN_STUB_LEN);

  // The three branch offsets, worked out from where the labels landed rather
  // than restated — an off-by-one here is a `PLA` that never happens and a
  // stack that unwinds into whatever called the player.
  //
  //   13 BEQ -> end (32), from a next-PC of 15
  //   18 BEQ -> pull (29), from 20
  //   22 BNE -> pull (29), from 24
  if (rom[TWIN_STUB + 14] != 32 - 15) fail("BEQ end is +%d", rom[TWIN_STUB + 14]);
  if (rom[TWIN_STUB + 19] != 29 - 20) fail("BEQ pull is +%d", rom[TWIN_STUB + 19]);
  if (rom[TWIN_STUB + 23] != 29 - 24) fail("BNE pull is +%d", rom[TWIN_STUB + 23]);
  // The stack has to balance. The `PHA` at 15 is passed only by the branch at
  // 13, which leaves before pushing; every other way out goes through the `PLA`
  // at 29. A stub that returned with a word still on the stack would `RTS` into
  // the middle of the player.
  if (15 + (int)rom[TWIN_STUB + 14] < 32)
    fail("the BEQ at 13 lands before the RTS, having skipped the PHA");
  if (20 + (int)rom[TWIN_STUB + 19] != 29)
    fail("the BEQ at 18 does not land on the PLA");
  if (24 + (int)rom[TWIN_STUB + 23] != 29)
    fail("the BNE at 22 does not land on the PLA");

  // Centred, not $FF. $FF is not a code the table can produce and the stub would
  // read it as one.
  for (uint32_t i = 0; i < TWIN_AIM_LEN; i++)
    expect(rom, TWIN_AIM + i, 0x00, "aim starts centred");

  // Nothing else moved — in particular not the eleven bytes at the front of the
  // pad, which is where `--level` puts its own stub.
  for (uint32_t i = 0; i < ROM_SIZE; i++) {
    const bool mine = (i >= TWIN_LATCH && i < TWIN_LATCH + TWIN_LATCH_LEN) ||
                      (i >= TWIN_LATCH1 && i < TWIN_LATCH1 + TWIN_LATCH1_LEN) ||
                      (i >= TWIN_STUB && i < TWIN_STUB + TWIN_STUB_LEN) ||
                      (i >= TWIN_AIM && i < TWIN_AIM + TWIN_AIM_LEN);
    if (!mine && rom[i] != was[i]) {
      fail("install wrote $%05X, which is not one of its own bytes", i);
      break;
    }
  }
  for (uint32_t i = 0; i < LEVEL_STUB_LEN; i++)
    if (rom[LEVEL_STUB_AT + i] != 0xff)
      fail("install took a byte of the pad --level's stub needs ($%05X)",
           LEVEL_STUB_AT + i);
  if (TWIN_STUB < LEVEL_STUB_AT + LEVEL_STUB_LEN)
    fail("the stub starts inside --level's");
  if (TWIN_AIM < TWIN_STUB + TWIN_STUB_LEN)
    fail("the aim words start inside the stub");
  if (TWIN_AIM + TWIN_AIM_LEN != PAD_AT + PAD_LEN)
    fail("the aim words are not the last of the pad");

  // Installing twice is not a thing the frontend does, and it must not be a
  // thing that silently half-works: the second one sees a call site that is no
  // longer the latch and refuses.
  memcpy(was, rom, ROM_SIZE);
  if (twin_install(rom, ROM_SIZE)) fail("install accepted an already-patched ROM");
  if (memcmp(rom, was, ROM_SIZE) != 0) fail("the refused second install wrote");

  free(rom);
  free(was);
}

// Every way of not being the cartridge this understands, and each one has to
// leave the bytes alone.
static void test_refusals(void) {
  struct {
    const char* what;
    uint32_t at;
    uint8_t to;
    uint32_t size;
  } cases[] = {
      {"a latch that is not the latch", TWIN_LATCH, 0x22, ROM_SIZE},
      {"a latch storing somewhere else", TWIN_LATCH + 8, 0x28, ROM_SIZE},
      {"a monster whose latch is not the latch", TWIN_LATCH1 + 9, 0x60, ROM_SIZE},
      {"a monster whose latch stores somewhere else", TWIN_LATCH1 + 8, 0x28, ROM_SIZE},
      {"a state that re-enters somewhere else", TWIN_REENTER, 0x4c, ROM_SIZE},
      {"a pad with code in it", TWIN_STUB + 4, 0x60, ROM_SIZE},
      {"a pad whose last byte is taken", TWIN_STUB + TWIN_STUB_LEN - 1, 0x00,
       ROM_SIZE},
      {"aim words that are not pad", TWIN_AIM + 3, 0x12, ROM_SIZE},
      {"a ROM that stops short", 0, 0, TWIN_ROM_MIN - 1},
  };
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    uint8_t* rom = make_rom();
    uint8_t* was = (uint8_t*)malloc(ROM_SIZE);
    if (cases[i].size == ROM_SIZE) rom[cases[i].at] = cases[i].to;
    memcpy(was, rom, ROM_SIZE);
    if (twin_install(rom, cases[i].size))
      fail("install accepted %s", cases[i].what);
    if (memcmp(rom, was, ROM_SIZE) != 0)
      fail("install wrote before refusing %s", cases[i].what);
    free(rom);
    free(was);
  }
  if (twin_install(NULL, ROM_SIZE)) fail("install accepted a null ROM");
}

// --- the aim word ------------------------------------------------------------

static void test_aim(void) {
  uint8_t* rom = make_rom();
  twin_install(rom, ROM_SIZE);
  // Little-endian, and indexed the way the latch's own `X` indexes: port 1 at
  // the base, port 2 two bytes on, which is the doubled player number.
  twin_set_aim(rom, 0, 0x0010);
  twin_set_aim(rom, 1, 0x0006);
  if (rom[TWIN_AIM + 0] != 0x10 || rom[TWIN_AIM + 1] != 0x00)
    fail("port 1's aim is $%02X%02X", rom[TWIN_AIM + 1], rom[TWIN_AIM + 0]);
  if (rom[TWIN_AIM + 2] != 0x06 || rom[TWIN_AIM + 3] != 0x00)
    fail("port 2's aim is $%02X%02X", rom[TWIN_AIM + 3], rom[TWIN_AIM + 2]);
  // ...and one port is not the other.
  twin_set_aim(rom, 0, 0);
  if (rom[TWIN_AIM + 2] != 0x06) fail("centring port 1 centred port 2");
  free(rom);
}

// The frontend's whole per-port step, which is a stick becoming two things at
// once: an aim the stub will read, and the `Y` this game fires with. A stick
// that aims without firing does nothing at all, so the two are asserted
// together.
static void test_apply(void) {
  uint8_t* rom = make_rom();
  twin_install(rom, ROM_SIZE);

  const uint16_t fire = (uint16_t)(1u << BTN_Y);
  if (twin_apply(rom, 0, LEFT) != fire) fail("a pushed stick did not press Y");
  if (rom[TWIN_AIM] != 0x0e) fail("a stick pushed left aimed $%02X", rom[TWIN_AIM]);

  // Centred: no fire, and the aim handed back to the game rather than left at
  // the last direction, which would fire the player's next walk sideways.
  if (twin_apply(rom, 0, 0) != 0) fail("a centred stick pressed something");
  if (rom[TWIN_AIM] != 0x00) fail("a centred stick left $%02X armed", rom[TWIN_AIM]);

  // Opposed bits name no direction, so they must not press the trigger either.
  if (twin_apply(rom, 0, (uint16_t)(LEFT | RIGHT)) != 0)
    fail("a stick pushed two ways at once pressed Y");

  // Two ports, independently: the second one's stick must not fire the first.
  twin_apply(rom, 0, 0);
  if (twin_apply(rom, 1, UP) != fire) fail("port 2's stick did not press Y");
  if (rom[TWIN_AIM] != 0x00 || rom[TWIN_AIM + 2] != 0x02)
    fail("port 2's stick aimed port 1");

  // And the read-back the frontend arms the port from, which is the one value
  // both paths have to share.
  if (twin_aim_of(rom, 0) != 0x0000 || twin_aim_of(rom, 1) != 0x0002)
    fail("read-back is $%04X/$%04X", twin_aim_of(rom, 0), twin_aim_of(rom, 1));
  twin_apply(rom, 0, (uint16_t)(DOWN | LEFT));
  if (twin_aim_of(rom, 0) != 0x000c)
    fail("read-back of down-left is $%04X", twin_aim_of(rom, 0));
  free(rom);
}

// --- the half the cartridge patch does not cover -----------------------------
//
// `$80:D1FF` is a **substituted** routine: in the playable build the C port runs
// the player's frame and the nine bytes at `$80:D250` are never executed. So the
// ROM patch above is the `--stock` path and `player_set_aim` is the default one,
// and a flag that arms only the first works in a mode nobody plays in.
//
// That is not a hypothetical — it is the bug this section was written for, and
// it survived a headless run that walked left and shot right, because
// `zamn_headless` has no port in it to substitute.
//
// So this drives `player_state_normal` directly, which is the exact function the
// game calls, and asserts on the two words the whole feature is about: `$24`
// keeps the walk and `$26` takes the aim.
static void test_port(const uint8_t* rom_bytes, uint32_t rom_size) {
  Wram* w = (Wram*)calloc(1, sizeof *w);
  const Rom rom = {rom_bytes, rom_size};
  // Player 1's thread page, out of the table at `$80:82DE`.
  const uint16_t dp = 0x0100;
  // No weapon selected, which is the one thing that has to be arranged: it is
  // what `$80:D210  BMI` skips the whole ammunition block on, and the block is
  // not what is under test.
  wram_w16(w, W_PLAYER_WEAPON, WEAPON_NONE);
  wram_w16(w, dp + PSN_DP_PLAYER, 0);

  PlayerStateRegs r;
  const uint16_t left = 0x000e, right = 0x0006;

  // Unarmed: the routine the ROM runs. Walk left, face left, shoot left.
  player_set_aim(0, 0);
  wram_w16(w, W_JOY_DIR, left);
  player_state_normal(w, &rom, dp, &r);
  if (wram_r16(w, dp + PSN_DP_DIR) != left)
    fail("port: unarmed, $24 is $%04X not left", wram_r16(w, dp + PSN_DP_DIR));
  if (wram_r16(w, dp + PSN_DP_DIR_HELD) != left)
    fail("port: unarmed, $26 is $%04X not left", wram_r16(w, dp + PSN_DP_DIR_HELD));

  // Armed the other way: the same walk, the opposite aim. This is the whole
  // feature in two assertions.
  player_set_aim(0, right);
  wram_w16(w, W_JOY_DIR, left);
  player_state_normal(w, &rom, dp, &r);
  if (wram_r16(w, dp + PSN_DP_DIR) != left)
    fail("port: the aim moved the walk — $24 is $%04X, should still be left",
         wram_r16(w, dp + PSN_DP_DIR));
  if (wram_r16(w, dp + PSN_DP_DIR_HELD) != right)
    fail("port: walking left and aiming right, $26 is $%04X not right",
         wram_r16(w, dp + PSN_DP_DIR_HELD));

  // Standing still and aiming: `$0072` is zero, so the game would have left
  // `$26` at whatever it was. The aim has to win there too, or a player who
  // stops walking goes on shooting the way they last walked.
  wram_w16(w, W_JOY_DIR, 0);
  player_state_normal(w, &rom, dp, &r);
  if (wram_r16(w, dp + PSN_DP_DIR) != 0)
    fail("port: a centred D-pad left $24 at $%04X", wram_r16(w, dp + PSN_DP_DIR));
  if (wram_r16(w, dp + PSN_DP_DIR_HELD) != right)
    fail("port: standing still and aiming right, $26 is $%04X",
         wram_r16(w, dp + PSN_DP_DIR_HELD));

  // ...and the pose. Standing still, `$80:D53D` fires and never redraws, so an
  // aim that changed has to ask the state to re-enter or the player goes on
  // being drawn facing the way they were. Three cases, and only the first is
  // allowed to touch `$28`.
  const uint16_t idle_resume = 0xd53d;  // what the idle state parks there

  //   1. standing still, aim flipped: re-enter.
  wram_w16(w, dp + PSN_DP_RESUME, idle_resume);
  wram_w16(w, W_JOY_DIR, 0);
  player_set_aim(0, left);
  player_state_normal(w, &rom, dp, &r);
  if (wram_r16(w, dp + PSN_DP_RESUME) != PSN_STATE_REENTER)
    fail("port: flicking the aim did not ask for a redraw — $28 is $%04X",
         wram_r16(w, dp + PSN_DP_RESUME));

  //   2. standing still, aim unchanged: leave it alone, or the frame that would
  //      have fired is spent turning to face where it already faces.
  wram_w16(w, dp + PSN_DP_RESUME, idle_resume);
  player_state_normal(w, &rom, dp, &r);
  if (wram_r16(w, dp + PSN_DP_RESUME) != idle_resume)
    fail("port: a steady aim redrew anyway — $28 is $%04X",
         wram_r16(w, dp + PSN_DP_RESUME));

  //   3. walking, aim flipped: leave it alone. The walk rebuilds its own pose
  //      every five frames, and restarting the state under it would reset the
  //      animation phase on every frame that both walks and aims.
  wram_w16(w, dp + PSN_DP_RESUME, idle_resume);
  wram_w16(w, W_JOY_DIR, left);
  player_set_aim(0, right);
  player_state_normal(w, &rom, dp, &r);
  if (wram_r16(w, dp + PSN_DP_RESUME) != idle_resume)
    fail("port: aiming while walking restarted the state — $28 is $%04X",
         wram_r16(w, dp + PSN_DP_RESUME));
  if (wram_r16(w, dp + PSN_DP_DIR_HELD) != right)
    fail("port: walking, the aim still has to reach $26");

  //   4. unarmed, whatever happens: `$28` is not this feature's word.
  wram_w16(w, dp + PSN_DP_RESUME, idle_resume);
  wram_w16(w, W_JOY_DIR, 0);
  player_set_aim(0, 0);
  player_state_normal(w, &rom, dp, &r);
  if (wram_r16(w, dp + PSN_DP_RESUME) != idle_resume)
    fail("port: unarmed, $28 became $%04X", wram_r16(w, dp + PSN_DP_RESUME));
  player_set_aim(0, right);
  wram_w16(w, W_JOY_DIR, 0);
  player_state_normal(w, &rom, dp, &r);

  // Disarmed again: back to the ROM's own behaviour, with nothing left behind.
  player_set_aim(0, 0);
  wram_w16(w, W_JOY_DIR, left);
  player_state_normal(w, &rom, dp, &r);
  if (wram_r16(w, dp + PSN_DP_DIR_HELD) != left)
    fail("port: disarming left $26 at $%04X", wram_r16(w, dp + PSN_DP_DIR_HELD));

  // Player 2 is a separate word, and arming one must not aim the other.
  wram_w16(w, W_PLAYER_WEAPON + 2, WEAPON_NONE);
  const uint16_t dp2 = 0x0280;  // slot 1's page
  wram_w16(w, dp2 + PSN_DP_PLAYER, 2);
  player_set_aim(2, right);
  wram_w16(w, W_JOY_DIR + 2, left);
  wram_w16(w, W_JOY_DIR, left);
  player_state_normal(w, &rom, dp2, &r);
  player_state_normal(w, &rom, dp, &r);
  if (wram_r16(w, dp2 + PSN_DP_DIR_HELD) != right)
    fail("port: player 2's aim did not take — $26 is $%04X",
         wram_r16(w, dp2 + PSN_DP_DIR_HELD));
  if (wram_r16(w, dp + PSN_DP_DIR_HELD) != left)
    fail("port: player 2's aim reached player 1 — $26 is $%04X",
         wram_r16(w, dp + PSN_DP_DIR_HELD));
  player_set_aim(2, 0);
  free(w);
}

// --- the other thing the port does that the cartridge does not --------------
//
// The next weapon or item and the one before, asked for by a frontend and done
// in the player's frame: `player_cycle_request`, in `src/port/player.h`. Here
// because this is the test that drives `player_state_normal`, and against the
// cartridge because the weapon search reads its bases and its data out of it.
static void test_cycle(const uint8_t* rom_bytes, uint32_t rom_size) {
  Wram* w = (Wram*)calloc(1, sizeof *w);
  const Rom rom = {rom_bytes, rom_size};
  const uint16_t dp = 0x0100;
  PlayerStateRegs r;
  player_set_aim(0, 0);
  wram_w16(w, dp + PSN_DP_PLAYER, 0);
  wram_w16(w, dp + ACTOR_DP_RECORD, 0x1ab6);
  wram_w16(w, dp + PLAYER_DP_ITEMS, W_PLAYER_ITEMS);
  wram_w16(w, dp + PSN_DP_INVENTORY, W_PLAYER_INVENTORY);
  #define WEAPON() wram_r16(w, W_PLAYER_WEAPON)
  #define ITEM() wram_r16(w, W_PLAYER_ITEM)

  // Weapons 0, 3 and 7; holding 3.
  wram_w16(w, W_PLAYER_INVENTORY + 0 * 2, 0x0099);
  wram_w16(w, W_PLAYER_INVENTORY + 3 * 2, 0x0005);
  wram_w16(w, W_PLAYER_INVENTORY + 7 * 2, 0x0001);
  wram_w16(w, W_PLAYER_WEAPON, 3);
  wram_w16(w, W_PLAYER_ITEM, ITEM_NONE);

  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 3) fail("cycle: nothing asked for and the weapon became %u", WEAPON());

  player_cycle_request(0, PSN_CYCLE_WEAPON, -1);
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 0) fail("cycle: the weapon before 3 is 0, not %u", WEAPON());
  if (wram_r16(w, 0x1ab6 + ACTOR_META_BANK) != WEAPON_META_BANK)
    fail("cycle: going backwards did not look the weapon's data up");
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 0) fail("cycle: one request was answered twice (%u)", WEAPON());
  player_cycle_request(0, PSN_CYCLE_WEAPON, -1);
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 7) fail("cycle: the weapon before 0 wraps to 7, not %u", WEAPON());
  player_cycle_request(0, PSN_CYCLE_WEAPON, +1);
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 0) fail("cycle: the weapon after 7 wraps to 0, not %u", WEAPON());

  // Two presses before the player's frame comes round are two steps, a frame
  // each; and it is player 1's alone.
  player_cycle_request(0, PSN_CYCLE_WEAPON, +1);
  player_cycle_request(0, PSN_CYCLE_WEAPON, +1);
  if (player_cycle_pending(2, PSN_CYCLE_WEAPON)) fail("cycle: player 1's request reached player 2");
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 3) fail("cycle: the first of two steps gave %u", WEAPON());
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 7) fail("cycle: the second of two steps gave %u", WEAPON());

  // The claim the header makes: with fire held on a weapon that is not empty,
  // B does nothing -- and a request does.
  wram_w16(w, W_JOY_RAW, PSN_BTN_FIRE | PSN_BTN_WEAPON);
  wram_w16(w, dp + PSN_DP_PREV, PSN_BTN_FIRE);
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 7) fail("cycle: B changed weapon under a held fire button (%u), which the header says it cannot", WEAPON());
  wram_w16(w, W_JOY_RAW, PSN_BTN_FIRE);
  player_cycle_request(0, PSN_CYCLE_WEAPON, -1);
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 3) fail("cycle: a request under a held fire button gave %u", WEAPON());
  wram_w16(w, W_JOY_RAW, 0);

  // One weapon comes back to itself, and none finds none.
  wram_w16(w, W_PLAYER_INVENTORY + 0 * 2, 0);
  wram_w16(w, W_PLAYER_INVENTORY + 7 * 2, 0);
  player_cycle_request(0, PSN_CYCLE_WEAPON, -1);
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != 3) fail("cycle: the only weapon became %u", WEAPON());
  wram_w16(w, W_PLAYER_INVENTORY + 3 * 2, 0);
  player_cycle_request(0, PSN_CYCLE_WEAPON, -1);
  player_state_normal(w, &rom, dp, &r);
  if (WEAPON() != WEAPON_NONE) fail("cycle: no weapons at all left %u selected", WEAPON());

  // Items 2 and 11, holding nothing: backwards starts from the top.
  wram_w16(w, W_PLAYER_ITEMS + 2 * 2, 0x0001);
  wram_w16(w, W_PLAYER_ITEMS + 11 * 2, 0x0002);
  player_cycle_request(0, PSN_CYCLE_ITEM, -1);
  player_state_normal(w, &rom, dp, &r);
  if (ITEM() != 11) fail("cycle: the item before none is 11, not %u", ITEM());
  player_cycle_request(0, PSN_CYCLE_ITEM, -1);
  player_state_normal(w, &rom, dp, &r);
  if (ITEM() != 2) fail("cycle: the item before 11 is 2, not %u", ITEM());
  player_cycle_request(0, PSN_CYCLE_ITEM, +1);
  player_state_normal(w, &rom, dp, &r);
  if (ITEM() != 11) fail("cycle: the item after 2 is 11, not %u", ITEM());
  if (WEAPON() != WEAPON_NONE) fail("cycle: an item request moved the weapon");

  // A request nobody answers goes away.
  player_cycle_request(0, PSN_CYCLE_ITEM, -1);
  for (int i = 0; i < PSN_CYCLE_TTL - 1; i++) player_cycle_age();
  if (player_cycle_pending(0, PSN_CYCLE_ITEM) != -1) fail("cycle: a request expired early");
  player_cycle_age();
  if (player_cycle_pending(0, PSN_CYCLE_ITEM) != 0) fail("cycle: a request outlived its welcome");
  player_state_normal(w, &rom, dp, &r);
  if (ITEM() != 11) fail("cycle: an expired request was answered (%u)", ITEM());
  #undef WEAPON
  #undef ITEM
  free(w);
}

// --- and the same thing against the cartridge, if there is one ---------------

static void test_real_rom(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    printf("twin-stick: no cartridge at '%s'; synthetic checks only.\n", path);
    return;
  }
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* rom = (uint8_t*)malloc((size_t)n);
  if (fread(rom, 1, (size_t)n, f) != (size_t)n) {
    fail("could not read all of '%s'", path);
    fclose(f);
    free(rom);
    return;
  }
  fclose(f);

  // The three claims the header makes about where the game keeps things. Each
  // is a fact about this cartridge, and each is why the patch is legal.
  if (memcmp(rom + TWIN_DIR_TABLE, dir_table, sizeof dir_table) != 0)
    fail("$80:81F9 is not the direction table this expects");
  if (memcmp(rom + TWIN_LATCH, twin_latch_was, TWIN_LATCH_LEN) != 0)
    fail("$80:D250 is not the direction latch this expects");
  for (uint32_t i = 0; i < PAD_LEN; i++)
    if (rom[PAD_AT + i] != 0xff) fail("$%05X is not pad", PAD_AT + i);

  if (!twin_install(rom, (uint32_t)n))
    fail("install refused the real cartridge");
  else
    printf("twin-stick: installed against '%s' (%ld bytes).\n", path, n);

  test_port(rom, (uint32_t)n);
  test_cycle(rom, (uint32_t)n);
  free(rom);
}

int main(int argc, char** argv) {
  test_dir();
  test_install();
  test_refusals();
  test_aim();
  test_apply();
  test_real_rom(argc > 1 ? argv[1] : "Zombies Ate My Neighbors.sfc");
  if (failures) {
    printf("\n%d failure%s.\n", failures, failures == 1 ? "" : "s");
    return 1;
  }
  printf("twin-stick: all checks passed.\n");
  return 0;
}
