// $80:9748, $80:99C1  the two screens before a game -- see port/menus.h.

#include "port/menus.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/oam.h"
#include "port/player.h"  // the pads

// A blink is every tenth pass.
#define MENUS_BLINK_EVERY 10

// The title menu's records are on the screen and in use, and by turns the
// chosen one has its attributes set: a byte of flags for each, by the half of
// the blink and the choice.
#define TITLE_MENU_FLAGS 0x8097a4u
#define TITLE_MENU_SHOWN (ACTOR_DRAW | ACTOR_SCREEN_SPACE | ACTOR_ACTIVE)

typedef struct {
  Wram* w;
  uint16_t page;
  MenusLog* log;  // never NULL here
  PortFlags flags;
} Menus;

static uint16_t field(const Menus* m, uint16_t at) {
  return wram_r16(m->w, (uint16_t)(m->page + at));
}

static void set_field(Menus* m, uint16_t at, uint16_t v) {
  wram_w16(m->w, (uint16_t)(m->page + at), v);
}

static bool is_record(uint16_t record) { return record < 0x1f00u; }

static bool records_ok(const Wram* w, uint16_t page) {
  return is_record(wram_r16(w, (uint16_t)(page + MENUS_DP_FIRST))) &&
         is_record(wram_r16(w, (uint16_t)(page + MENUS_DP_SECOND)));
}

// Count the pass. True when the screen has waited long enough, and then the
// ROM goes on with the count in A and the flags of the compare.
static bool out_of_patience(Menus* m) {
  MenusLog* log = m->log;
  const uint16_t passes = (uint16_t)(field(m, MENUS_DP_PASSES) + 1);
  set_field(m, MENUS_DP_PASSES, passes);
  if (!flags_at_least(&m->flags, passes, MENUS_PATIENCE)) return false;
  log->over = true;
  log->a = passes;
  log->n = ((uint16_t)(passes - MENUS_PATIENCE) & 0x8000u) != 0;
  log->z = passes == MENUS_PATIENCE;
  log->c = true;
  return true;
}

// Take a pad: see "A press" in the header. `BIT` leaves bit 14 of what was
// remembered in the overflow, and the compare with nothing leaves carry set.
static MenusPad take_pad(Menus* m, int pad) {
  const uint16_t seen_at = pad == 0 ? MENUS_DP_PAD_A : MENUS_DP_PAD_B;
  const uint16_t down = wram_r16(m->w, W_JOY_RAW + 2u * (unsigned)pad);
  const uint16_t seen = field(m, seen_at);
  flags_overflow(&m->flags, (seen & 0x4000u) != 0);
  if ((down & seen) != 0) return MENUS_PAD_HELD;
  set_field(m, seen_at, down);
  flags_carry(&m->flags, true);
  if (down == 0) return MENUS_PAD_IDLE;
  m->log->declined = true;
  return MENUS_PAD_PRESSED;
}

// The pass sleeps a tick.
static bool sleep_a_tick(Menus* m) {
  MenusLog* log = m->log;
  log->a = MENUS_YIELD_TICKS;
  log->n = false;
  log->z = false;
  log->c = m->flags.c;
  log->v = m->flags.v;
  log->v_set = m->flags.v_set;
  return true;
}

// ---------------------------------------------------------------------------
// $80:9748  the title's menu
// ---------------------------------------------------------------------------

bool title_menu_frame_supported(const Wram* w, uint16_t page) {
  const uint16_t blink = wram_r16(w, (uint16_t)(page + TITLE_MENU_DP_BLINK));
  const uint16_t choice = wram_r16(w, (uint16_t)(page + TITLE_MENU_DP_CHOICE));
  return records_ok(w, page) && blink <= 1 && (choice == 0 || choice == 4);
}

// Every tenth pass: the other half of the blink.
static void title_menu_blink(Menus* m, const Rom* rom) {
  PORT_COVER(title_menu_blinked);
  m->log->blinked = true;
  set_field(m, TITLE_MENU_DP_BLINK_WAIT, MENUS_BLINK_EVERY - 1);
  const uint16_t half = field(m, TITLE_MENU_DP_BLINK) ^ 1;
  set_field(m, TITLE_MENU_DP_BLINK, half);
  const uint16_t at = flags_add(&m->flags, (uint16_t)(half << 1),
                                field(m, TITLE_MENU_DP_CHOICE));
  const uint16_t flags = rom_word(rom, TITLE_MENU_FLAGS + at);
  wram_w16(m->w, (uint16_t)(field(m, MENUS_DP_FIRST) + ACTOR_FLAGS),
           (flags & 0x00ffu) | TITLE_MENU_SHOWN);
  wram_w16(m->w, (uint16_t)(field(m, MENUS_DP_SECOND) + ACTOR_FLAGS),
           (uint16_t)(flags >> 8) | TITLE_MENU_SHOWN);
}

bool title_menu_frame(Wram* w, const Rom* rom, uint16_t page, MenusLog* log) {
  MenusLog scratch;
  if (log == NULL) log = &scratch;
  *log = (MenusLog){0};
  Menus m = {w, page, log, {0}};

  if (out_of_patience(&m)) {
    PORT_COVER(title_menu_over);
    return false;
  }
  const uint16_t wait = (uint16_t)(field(&m, TITLE_MENU_DP_BLINK_WAIT) - 1);
  set_field(&m, TITLE_MENU_DP_BLINK_WAIT, wait);
  if ((wait & 0x8000u) != 0) title_menu_blink(&m, rom);

  log->pad[0] = take_pad(&m, 0);
  if (log->pad[0] != MENUS_PAD_PRESSED) log->pad[1] = take_pad(&m, 1);
  if (!log->declined) PORT_COVER(title_menu_waited);
  return sleep_a_tick(&m);
}

// ---------------------------------------------------------------------------
// $80:99C1  the players' screen
// ---------------------------------------------------------------------------

bool players_screen_frame_supported(const Wram* w, uint16_t page) {
  return records_ok(w, page);
}

static bool has_joined(const Menus* m, int player) {
  return wram_r16(m->w, W_HUD_PANEL_ON + 2u * (unsigned)player) != 0;
}

// Every tenth pass: hide or show each player who has not joined.
static void players_screen_blink(Menus* m) {
  PORT_COVER(players_screen_blinked);
  m->log->blinked = true;
  wram_w16(m->w, W_PLAYERS_SCREEN_BLINK, 0);
  for (int player = 0; player < 2; player++) {
    if (has_joined(m, player)) continue;
    const uint16_t record =
        field(m, player == 0 ? MENUS_DP_FIRST : MENUS_DP_SECOND);
    wram_w16(m->w, (uint16_t)(record + ACTOR_FLAGS),
             wram_r16(m->w, (uint16_t)(record + ACTOR_FLAGS)) ^ ACTOR_DRAW);
  }
}

bool players_screen_frame(Wram* w, uint16_t page, MenusLog* log) {
  MenusLog scratch;
  if (log == NULL) log = &scratch;
  *log = (MenusLog){0};
  Menus m = {w, page, log, {0}};

  if (out_of_patience(&m)) {
    PORT_COVER(players_screen_over);
    return false;
  }
  const uint16_t count = (uint16_t)(wram_r16(w, W_PLAYERS_SCREEN_BLINK) + 1);
  wram_w16(w, W_PLAYERS_SCREEN_BLINK, count);
  log->joined[0] = has_joined(&m, 0);
  log->joined[1] = has_joined(&m, 1);
  if (flags_at_least(&m.flags, count, MENUS_BLINK_EVERY))
    players_screen_blink(&m);

  // A player who has joined is not asked again, and the second is asked only
  // when the first's pad did nothing.
  for (int player = 0; player < 2 && !log->declined; player++) {
    if (!log->joined[player]) log->pad[player] = take_pad(&m, player);
  }
  if (!log->declined) PORT_COVER(players_screen_waited);
  return sleep_a_tick(&m);
}
