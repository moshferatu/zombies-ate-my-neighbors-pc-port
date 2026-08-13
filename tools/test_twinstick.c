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
  expect(rom, TWIN_STUB + 13, 0xf0, "BEQ");
  expect(rom, TWIN_STUB + 14, 0x02, "BEQ over the store");
  expect(rom, TWIN_STUB + 15, 0x85, "STA dp");
  expect(rom, TWIN_STUB + 16, 0x26, "STA $26");
  expect(rom, TWIN_STUB + 17, 0x60, "RTS");
  if (TWIN_STUB_LEN != 18) fail("TWIN_STUB_LEN is %d, not 18", (int)TWIN_STUB_LEN);

  // Centred, not $FF. $FF is not a code the table can produce and the stub would
  // read it as one.
  for (uint32_t i = 0; i < TWIN_AIM_LEN; i++)
    expect(rom, TWIN_AIM + i, 0x00, "aim starts centred");

  // Nothing else moved — in particular not the eleven bytes at the front of the
  // pad, which is where `--level` puts its own stub.
  for (uint32_t i = 0; i < ROM_SIZE; i++) {
    const bool mine = (i >= TWIN_LATCH && i < TWIN_LATCH + TWIN_LATCH_LEN) ||
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
