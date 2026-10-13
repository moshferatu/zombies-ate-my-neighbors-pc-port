// The two screens before a game -- see port/menus.h.

#include "port/menus.h"

#include <stddef.h>

#include "port/coverage.h"
#include "port/flags.h"
#include "port/frontend.h"  // the portraits' scroll job's count
#include "port/oam.h"
#include "port/password_check.h"
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
static bool out_of_patience(Menus* m, uint32_t over_pc) {
  MenusLog* log = m->log;
  const uint16_t passes = (uint16_t)(field(m, MENUS_DP_PASSES) + 1);
  set_field(m, MENUS_DP_PASSES, passes);
  if (!flags_at_least(&m->flags, passes, MENUS_PATIENCE)) return false;
  log->over = true;
  log->pc = over_pc;
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
  return down == 0 ? MENUS_PAD_IDLE : MENUS_PAD_PRESSED;
}

static uint16_t taken_from(const Menus* m, int pad) {
  return field(m, pad == 0 ? MENUS_DP_PAD_A : MENUS_DP_PAD_B);
}

// The pass stops on a call, with what the call is given in A.
static void stop_on(Menus* m, uint32_t pc, uint16_t a) {
  MenusLog* log = m->log;
  log->pc = pc;
  log->a = a;
  log->n = false;
  log->z = false;
  log->c = m->flags.c;
  log->v = m->flags.v;
  log->v_set = m->flags.v_set;
}

static void pressed(Menus* m, int pad, MenusPress press) {
  m->log->press = press;
  m->log->presser = pad;
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

// A button that confirms takes the choice, by running the count out. Any
// other moves to the other choice, which blinks at once.
static void title_menu_press(Menus* m, int pad) {
  if ((taken_from(m, pad) & MENUS_CONFIRM_BUTTONS) != 0) {
    PORT_COVER(title_menu_confirm);
    pressed(m, pad, MENUS_PRESS_CONFIRM);
    stop_on(m, TITLE_MENU_CONFIRM_PC, MENUS_SOUND_CONFIRM);
    return;
  }
  PORT_COVER(title_menu_move);
  pressed(m, pad, MENUS_PRESS_MOVE);
  set_field(m, TITLE_MENU_DP_CHOICE,
            field(m, TITLE_MENU_DP_CHOICE) ^ TITLE_MENU_CHOICE_PASSWORD);
  set_field(m, TITLE_MENU_DP_BLINK_WAIT, 0);
  stop_on(m, TITLE_MENU_MOVE_PC, MENUS_SOUND_MOVE);
}

static void title_menu_turn(Menus* m, const Rom* rom) {
  const uint16_t wait = (uint16_t)(field(m, TITLE_MENU_DP_BLINK_WAIT) - 1);
  set_field(m, TITLE_MENU_DP_BLINK_WAIT, wait);
  if ((wait & 0x8000u) != 0) title_menu_blink(m, rom);

  for (int pad = 0; pad < 2; pad++) {
    m->log->pad[pad] = take_pad(m, pad);
    if (m->log->pad[pad] == MENUS_PAD_PRESSED) {
      title_menu_press(m, pad);
      return;
    }
  }
  PORT_COVER(title_menu_waited);
  stop_on(m, TITLE_MENU_YIELD_PC, MENUS_YIELD_TICKS);
}

void title_menu_pass(Wram* w, const Rom* rom, uint16_t page, MenusLog* log) {
  MenusLog scratch;
  if (log == NULL) log = &scratch;
  *log = (MenusLog){0};
  Menus m = {w, page, log, {0}};
  PORT_COVER(title_menu_first_pass);
  title_menu_turn(&m, rom);
}

bool title_menu_frame(Wram* w, const Rom* rom, uint16_t page, MenusLog* log) {
  MenusLog scratch;
  if (log == NULL) log = &scratch;
  *log = (MenusLog){0};
  Menus m = {w, page, log, {0}};

  if (out_of_patience(&m, TITLE_MENU_OVER_PC)) {
    PORT_COVER(title_menu_over);
    return false;
  }
  title_menu_turn(&m, rom);
  return true;
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

static uint16_t player_record(const Menus* m, int player) {
  return field(m, player == 0 ? MENUS_DP_FIRST : MENUS_DP_SECOND);
}

// Start alone joins: the player's blinking sprite is hidden for good, and
// the screen has two seconds left.
static void players_screen_join(Menus* m, int player) {
  PORT_COVER(players_screen_join);
  pressed(m, player, MENUS_PRESS_JOIN);
  wram_w16(m->w, W_HUD_PANEL_ON + 2u * (unsigned)player, 1);
  const uint16_t flags_at = (uint16_t)(player_record(m, player) + ACTOR_FLAGS);
  wram_w16(m->w, flags_at, wram_r16(m->w, flags_at) & (uint16_t)~ACTOR_DRAW);
  set_field(m, MENUS_DP_PASSES, MENUS_PATIENCE_JOINED);
  stop_on(m, player == 0 ? PLAYERS_SCREEN_JOIN_PC : PLAYERS_SCREEN_JOIN_2_PC,
          MENUS_SOUND_CONFIRM);
}

// The two characters change places, and `$80:9A31` is to draw them again.
// It is called with what `LDA : LDX` left: the first's old character, the
// second's, and the flags of that.
static void players_screen_swap(Menus* m, int player) {
  PORT_COVER(players_screen_swap);
  pressed(m, player, MENUS_PRESS_SWAP);
  set_field(m, MENUS_DP_PASSES, 0);
  const uint16_t first = wram_r16(m->w, W_PLAYERS_CHARACTERS);
  const uint16_t second = wram_r16(m->w, W_PLAYERS_CHARACTERS + 2);
  wram_w16(m->w, W_PLAYERS_CHARACTERS, second);
  wram_w16(m->w, W_PLAYERS_CHARACTERS + 2, first);
  stop_on(m, PLAYERS_SCREEN_SWAP_PC, first);
  m->log->x = second;
  m->log->n = (second & 0x8000u) != 0;
  m->log->z = second == 0;
}

static void players_screen_press(Menus* m, int player) {
  if (flags_same(&m->flags, taken_from(m, player), MENUS_START_BUTTON)) {
    players_screen_join(m, player);
  } else if (has_joined(m, 0) || has_joined(m, 1)) {
    PORT_COVER(players_screen_ignored);
    pressed(m, player, MENUS_PRESS_IGNORED);
    stop_on(m, PLAYERS_SCREEN_YIELD_PC, MENUS_YIELD_TICKS);
  } else {
    players_screen_swap(m, player);
  }
}

static void players_screen_turn(Menus* m) {
  MenusLog* log = m->log;
  const uint16_t count =
      (uint16_t)(wram_r16(m->w, W_PLAYERS_SCREEN_BLINK) + 1);
  wram_w16(m->w, W_PLAYERS_SCREEN_BLINK, count);
  log->joined[0] = has_joined(m, 0);
  log->joined[1] = has_joined(m, 1);
  if (flags_at_least(&m->flags, count, MENUS_BLINK_EVERY))
    players_screen_blink(m);

  // A player who has joined is not asked again, and the second is asked only
  // when the first's pad did nothing.
  for (int player = 0; player < 2; player++) {
    if (log->joined[player]) continue;
    log->pad[player] = take_pad(m, player);
    if (log->pad[player] == MENUS_PAD_PRESSED) {
      players_screen_press(m, player);
      return;
    }
  }
  PORT_COVER(players_screen_waited);
  stop_on(m, PLAYERS_SCREEN_YIELD_PC, MENUS_YIELD_TICKS);
}

void players_screen_first_pass(Wram* w, uint16_t page, uint16_t job,
                               MenusLog* log) {
  MenusLog scratch;
  if (log == NULL) log = &scratch;
  *log = (MenusLog){0};
  Menus m = {w, page, log, {0}};
  PORT_COVER(players_screen_first_pass);
  set_field(&m, PLAYERS_SCREEN_DP_SCROLL_JOB, job);
  players_screen_turn(&m);
}

bool players_screen_frame(Wram* w, uint16_t page, MenusLog* log) {
  MenusLog scratch;
  if (log == NULL) log = &scratch;
  *log = (MenusLog){0};
  Menus m = {w, page, log, {0}};

  if (out_of_patience(&m, PLAYERS_SCREEN_OVER_PC)) {
    PORT_COVER(players_screen_over);
    return false;
  }
  players_screen_turn(&m);
  return true;
}

// ---------------------------------------------------------------------------
// How each screen ends
// ---------------------------------------------------------------------------

static void load_a(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static uint16_t on_page(const Wram* w, const PortCpu* c, uint8_t dp) {
  return wram_r16(w, (uint16_t)(c->d + dp));
}

TitleMenuResult title_menu_result(Wram* w, PortCpu* c, uint16_t* pad) {
  // `PLP : BEQ`: the count's last test was equal only when it ran out by
  // itself. A choice sets it to its end, and the pass after is one past.
  set_p(c, pull8(w, c));
  if (flag(c, PORT_P_Z)) {
    PORT_COVER(title_menu_timed_out);
    c->pc = TITLE_MENU_GAME_PC;
    return TITLE_MENU_TIMED_OUT;
  }
  load_a(c, on_page(w, c, TITLE_MENU_DP_CHOICE));
  cmp16(c, c->a, TITLE_MENU_CHOICE_PASSWORD);
  if (!flag(c, PORT_P_C)) {
    PORT_COVER(title_menu_game);
    c->pc = TITLE_MENU_GAME_PC;
    return TITLE_MENU_GAME;
  }
  // `LDX #$0000 : LDA $62 : BIT #$D0C0 : BNE : LDX #$0002`. The pad that
  // confirmed is the first when that is what it had down.
  PORT_COVER(title_menu_password);
  load_a(c, on_page(w, c, MENUS_DP_PAD_A));
  const bool first = (c->a & MENUS_CONFIRM_BUTTONS) != 0;
  c->x = first ? 0 : 2;
  if (first) {
    c->p = (uint8_t)(c->p & ~PORT_P_Z);
  } else {
    set_nz16(c, c->x);
  }
  push16(w, c, c->x);
  *pad = c->x;
  c->pc = TITLE_MENU_PASSWORD_PC;
  return TITLE_MENU_PASSWORD;
}

bool players_screen_over(Wram* w, PortCpu* c) {
  load_a(c, wram_r16(w, W_HUD_PANEL_ON) | wram_r16(w, W_HUD_PANEL_ON + 2));
  if (c->a != 0) {
    PORT_COVER(players_screen_left);
    c->pc = PLAYERS_SCREEN_LEAVE_PC;
    return true;
  }
  // Both sprites drawn, and the second with the first one's flags.
  PORT_COVER(players_screen_nobody);
  c->y = on_page(w, c, MENUS_DP_FIRST);
  c->x = on_page(w, c, MENUS_DP_SECOND);
  const uint16_t flags =
      wram_r16(w, (uint16_t)(c->y + ACTOR_FLAGS)) | ACTOR_DRAW;
  wram_w16(w, (uint16_t)(c->y + ACTOR_FLAGS), flags);
  wram_w16(w, (uint16_t)(c->x + ACTOR_FLAGS), flags);
  load_a(c, MENUS_YIELD_TICKS);
  c->pc = PLAYERS_SCREEN_IDLE_PC;
  return false;
}

// ---------------------------------------------------------------------------
// The stretches
// ---------------------------------------------------------------------------

// A word of the thread's page, for the call that follows.
static void load_field(const Wram* w, PortCpu* c, uint8_t dp, uint32_t pc) {
  load_a(c, on_page(w, c, dp));
  c->pc = pc;
}

static void load_word(PortCpu* c, uint16_t v, uint32_t pc) {
  load_a(c, v);
  c->pc = pc;
}

static void set_page(Wram* w, const PortCpu* c, uint8_t dp, uint16_t v) {
  wram_w16(w, (uint16_t)(c->d + dp), v);
}

void menus_title_menu_begin(Wram* w, PortCpu* c) {
  PORT_COVER(title_menu_begin);
  set_page(w, c, MENUS_DP_PASSES, 0);
  set_page(w, c, TITLE_MENU_DP_BLINK_WAIT, 0);
  set_page(w, c, TITLE_MENU_DP_BLINK, 0);
  set_page(w, c, TITLE_MENU_DP_CHOICE, 0);
  c->pc = 0x8096c1u;
}

// After the sound of a move: the wait starts again.
void menus_title_menu_moved(Wram* w, PortCpu* c) {
  PORT_COVER(title_menu_moved);
  set_page(w, c, MENUS_DP_PASSES, 0);
  load_word(c, MENUS_YIELD_TICKS, TITLE_MENU_YIELD_PC);
}

// After the sound of a choice: the wait is over.
void menus_title_menu_confirmed(Wram* w, PortCpu* c) {
  PORT_COVER(title_menu_confirmed);
  set_page(w, c, MENUS_DP_PASSES, MENUS_PATIENCE);
  load_word(c, MENUS_YIELD_TICKS, TITLE_MENU_YIELD_PC);
}

// `PHP`: the flags of the count's last test, kept for `title_menu_result`.
void menus_title_menu_over(Wram* w, PortCpu* c) {
  PORT_COVER(title_menu_flags_kept);
  push8(w, c, c->p);
  c->pc = 0x809755u;
}

void menus_title_menu_fade_sleep(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(title_menu_fade_sleep);
  load_word(c, MENUS_FADE_FRAMES, 0x80975bu);
}

void menus_title_menu_drift_stop(Wram* w, PortCpu* c) {
  PORT_COVER(title_menu_drift_stop);
  load_field(w, c, TITLE_MENU_DP_DRIFT_JOB, 0x809761u);
}

void menus_title_menu_free_first(Wram* w, PortCpu* c) {
  PORT_COVER(title_menu_free_first);
  load_field(w, c, MENUS_DP_FIRST, 0x809767u);
}

void menus_title_menu_free_second(Wram* w, PortCpu* c) {
  PORT_COVER(title_menu_free_second);
  load_field(w, c, MENUS_DP_SECOND, 0x80976du);
}

// `PLX`: the pad `title_menu_result` pushed, for the password's screen.
void menus_title_menu_password_pad(Wram* w, PortCpu* c) {
  PORT_COVER(title_menu_password_pad);
  c->x = pull16(w, c);
  set_nz16(c, c->x);
  c->pc = 0x809792u;
}

// After the password's screen. Carry set, and on to the players' screen,
// if the check found a level: it leaves 1 for a password it turned down and
// 0 for the one it tests first.
#define PASSWORD_LEVELS_FROM 0x0002u

void menus_title_menu_password_checked(Wram* w, PortCpu* c) {
  PORT_COVER(title_menu_password_checked);
  c->a = wram_r16(w, W_PASSWORD_LEVEL);
  cmp16(c, c->a, PASSWORD_LEVELS_FROM);
  c->pc = 0x80979du;
}

// Carry set: on to the players' screen.
void menus_title_menu_go(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(title_menu_go);
  set_c(c, true);
  c->pc = 0x8097a3u;
}

void menus_players_screen_begin(Wram* w, PortCpu* c) {
  PORT_COVER(players_screen_begin);
  set_page(w, c, MENUS_DP_PASSES, 0);
  wram_w16(w, W_PORTRAIT_SCROLL_COUNT, 0);
  wram_w16(w, W_PLAYERS_SCREEN_BLINK, 0);
  c->a = PORTRAIT_SCROLL_JOB;
  c->y = MENUS_JOB_BANK;
  set_nz16(c, c->y);
  c->pc = 0x8098f8u;
}

// After the sound of a join: `BRA`, and a tick to sleep.
void menus_players_screen_joined(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(players_screen_joined);
  load_word(c, MENUS_YIELD_TICKS, PLAYERS_SCREEN_YIELD_PC);
}

void menus_players_screen_joined_2(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(players_screen_joined_2);
  load_word(c, MENUS_YIELD_TICKS, PLAYERS_SCREEN_YIELD_PC);
}

// The portraits are drawn again, and the swap's sound is next.
void menus_players_screen_swapped(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(players_screen_swapped);
  load_word(c, MENUS_SOUND_MOVE, 0x8099b6u);
}

void menus_players_screen_sleep(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(players_screen_sleep);
  load_word(c, MENUS_YIELD_TICKS, PLAYERS_SCREEN_YIELD_PC);
}

void menus_players_screen_fade_sleep(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(players_screen_fade_sleep);
  load_word(c, MENUS_FADE_FRAMES, 0x8099dbu);
}

void menus_players_screen_scroll_stop(Wram* w, PortCpu* c) {
  PORT_COVER(players_screen_scroll_stop);
  load_field(w, c, PLAYERS_SCREEN_DP_SCROLL_JOB, 0x8099e1u);
}

void menus_players_screen_free_first(Wram* w, PortCpu* c) {
  PORT_COVER(players_screen_free_first);
  load_field(w, c, MENUS_DP_FIRST, 0x8099eau);
}

void menus_players_screen_free_second(Wram* w, PortCpu* c) {
  PORT_COVER(players_screen_free_second);
  load_field(w, c, MENUS_DP_SECOND, 0x8099f0u);
}

// Carry clear: somebody joined, and the game is next.
void menus_players_screen_go(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(players_screen_go);
  set_c(c, false);
  c->pc = 0x8099f5u;
}

void menus_players_screen_idle_free_first(Wram* w, PortCpu* c) {
  PORT_COVER(players_screen_idle_free_first);
  load_field(w, c, MENUS_DP_FIRST, 0x809a0fu);
}

void menus_players_screen_idle_free_second(Wram* w, PortCpu* c) {
  PORT_COVER(players_screen_idle_free_second);
  load_field(w, c, MENUS_DP_SECOND, 0x809a15u);
}

// Carry set: nobody joined, and the demonstration is next.
void menus_players_screen_idle(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(players_screen_idle);
  set_c(c, true);
  c->pc = 0x809a1au;
}

// What `portrait_copy` is called with: the character doubled in X, and in Y
// which side of the screen.
static void portrait_ask(const Wram* w, PortCpu* c, int player, uint32_t pc) {
  c->a = asl16(c, wram_r16(w, W_PLAYERS_CHARACTERS + 2u * (unsigned)player));
  c->x = c->a;
  c->y = (uint16_t)(2 * player);
  set_nz16(c, c->y);
  c->pc = pc;
}

void menus_portraits_first(Wram* w, PortCpu* c) {
  PORT_COVER(portraits_first);
  portrait_ask(w, c, 0, 0x809a39u);
}

void menus_portraits_second(Wram* w, PortCpu* c) {
  PORT_COVER(portraits_second);
  portrait_ask(w, c, 1, 0x809a44u);
}

// The job that sends the text map they were drawn on.
void menus_portraits_send_ask(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(portraits_send_ask);
  c->a = PORTRAITS_SEND_JOB;
  c->y = MENUS_JOB_BANK;
  set_nz16(c, c->y);
  c->pc = 0x809a4du;
}

// ...and that job's end: `PLA : CLC`, to run once.
void menus_portraits_sent(Wram* w, PortCpu* c) {
  PORT_COVER(portraits_sent);
  load_a(c, pull16(w, c));
  set_c(c, false);
  c->pc = 0x809aa3u;
}
