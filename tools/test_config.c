// Does `zamn.ini` say what the program hears?
//
// `src/config.h` is parsing, and parsing is the kind of code that is right for
// the file its author wrote and wrong for the next one. So, with no window and
// no device (SDL is linked for its names of keys and pad buttons, which are
// tables and want no subsystem started):
//
//   * **the file that is written is the defaults.** `CONFIG_DEFAULT_TEXT` must
//     read back, without a complaint, as exactly `config_defaults` -- which
//     also holds the default pad table to `pad_map_default` and the deadzone
//     to the 8,000 and 6,000 that `src/pad.h` measured its way to. Nobody can
//     change a default in one place and not the other;
//   * every setting can be set, and is the thing that was set;
//   * a line that cannot be read is reported, changes nothing, and does not
//     stop the lines after it;
//   * a binding is a list, an empty one binds nothing, and the keys that the
//     format needs for itself can still be bound by their words;
//   * a key in two places is reported, and the first thing it is wins;
//   * one button on two keys is held until *both* are let go;
//   * a path in the file is taken from the file's directory;
//   * what the launcher writes reads back as what it wrote, into the default
//     file without changing a byte of it, and into a file of the player's
//     own without losing its comments, its names or its line endings.

#include <stdio.h>
#include <string.h>

#include "config.h"

static int failures = 0;

static void fail(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  printf("FAIL: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  failures++;
}

// Parse quietly where complaints are expected: they go to stdout, and a test
// log full of "config:" lines that are meant to be there hides one that is not.
static void parse(Config* c, const char* text) { config_parse(c, text, "test"); }

static void test_default_text(void) {
  Config want, got;
  config_defaults(&want);
  config_defaults(&got);
  // Knock every binding and a few settings off their defaults first, so that
  // the text has to *put them there* and cannot pass by saying nothing.
  memset(got.key, 0, sizeof got.key);
  memset(got.hotkey, 0, sizeof got.hotkey);
  for (int b = 0; b < 12; b++) pad_map_clear(got.pad.game[b]);
  for (int k = 0; k < PAD_CYCLE_COUNT; k++) pad_map_clear(got.pad.cycle[k]);
  got.hitbox = 123; got.volume = 5; got.fullscreen = false; got.smoothing = false;
  got.pads = false; got.twin_stick = false; got.audio = false; got.high_scores = false;
  got.pad.move_stick = got.pad.aim_stick = PAD_STICK_NONE;
  got.pad.enter = got.pad.leave = 1; got.deadzone = 50; got.window_scale = 3;
  got.rom[0] = 0;
  parse(&got, CONFIG_DEFAULT_TEXT);
  if (got.warnings) fail("the default file was complained about %d times", got.warnings);
  got.warnings = 0;
  if (memcmp(&want, &got, sizeof want)) fail("the default file does not read back as the defaults");
  if (want.pad.enter != PAD_ENTER || want.pad.leave != PAD_LEAVE)
    fail("the default deadzone is %d/%d, not pad.h's %d/%d", want.pad.enter,
         want.pad.leave, PAD_ENTER, PAD_LEAVE);
  PadMap plain;
  memset(&plain, 0, sizeof plain);
  pad_map_default(&plain);
  PadMap from_config = want.pad;
  if (memcmp(&plain, &from_config, sizeof plain)) fail("the default pad table is not pad_map_default's");
  // Every button and every action has a line in the file, so a player can see
  // what there is to bind.
  for (int b = 0; b < 12; b++) {
    char line[32];
    snprintf(line, sizeof line, "\n%s =", config_button_names[b]);
    const char* first = strstr(CONFIG_DEFAULT_TEXT, line);
    const char* second = first ? strstr(first + 1, line) : NULL;
    const char* third = second ? strstr(second + 1, line) : NULL;
    if (!third) fail("the default file does not list '%s' in all three button sections", config_button_names[b]);
  }
  for (int a = 0; a < ACT_COUNT; a++) {
    char line[48];
    snprintf(line, sizeof line, "\n%s =", config_action_names[a]);
    const char* first = strstr(CONFIG_DEFAULT_TEXT, line);
    if (!first || !strstr(first + 1, line))
      fail("the default file does not list '%s' in both hotkey sections", config_action_names[a]);
  }
}

static void test_settings(void) {
  Config c;
  config_defaults(&c);
  parse(&c,
    "\xef\xbb\xbf; a byte order mark, and Windows line endings\r\n"
    "[Game]\r\n"
    "ROM = C:\\Games\\zamn.sfc\r\n"
    "Skip-Intro = yes\r\n"
    "level = 0\r\n"
    "hitbox=200\r\n"
    "  blood   =   RED  \r\n"
    "high scores = off\r\n"
    "high_scores_file = scores.bin\r\n"
    "[VIDEO]\r\n"
    "fullscreen = off\r\n"
    "widescreen = 16:9\r\n"
    "aspect = square\r\n"  // retired: taken and ignored, not complained about
    "filter = integer\r\n"
    "window.scale = 4\r\n"
    "smoothing = false\r\n"
    "refresh = 240\r\n"
    "radar = Flashing\r\n"
    "[audio]\r\n"
    "enabled = 0\r\n"
    "volume = 35\r\n"
    "[controller]\r\n"
    "enabled = no\r\n"
    "twin_stick = off\r\n"
    "deadzone = 30\r\n"
    "move_stick = right\r\n"
    "aim_stick = off\r\n");
  if (c.warnings) fail("a good file was complained about %d times", c.warnings);
  if (strcmp(c.rom, "C:\\Games\\zamn.sfc")) fail("rom: '%s'", c.rom);
  if (!c.skip_intro) fail("skip_intro");
  if (c.level != 0) fail("level 0 is a level, got %d", c.level);
  if (c.hitbox != 200) fail("hitbox %d", c.hitbox);
  if (!c.red_blood) fail("blood");
  if (c.high_scores) fail("high_scores");
  if (strcmp(c.high_scores_file, "scores.bin")) fail("high_scores_file '%s'", c.high_scores_file);
  if (c.fullscreen) fail("fullscreen");
  if (c.widescreen != WIDE_16_9) fail("widescreen %d", (int)c.widescreen);
  if (c.filter != SCALE_INTEGER) fail("filter");
  if (c.window_scale != 4) fail("window_scale %d", c.window_scale);
  if (c.smoothing) fail("smoothing");

  // The aspect's hotkey is retired with it, and a file from before still has it.
  Config h;
  config_defaults(&h);
  parse(&h, "[hotkeys]\ntoggle_aspect = F3\n[controller hotkeys]\ntoggle_aspect =\n");
  if (h.warnings) fail("a retired toggle_aspect was complained about %d times", h.warnings);
  if (c.refresh != 240) fail("refresh %d", c.refresh);
  if (!c.radar_flash) fail("radar");
  if (c.audio) fail("audio");
  if (c.volume != 35) fail("volume %d", c.volume);
  if (c.pads) fail("controller enabled");
  if (c.twin_stick) fail("twin_stick");
  if (c.pad.enter != 10000 || c.pad.leave != 7500) fail("deadzone 30 is %d/%d", c.pad.enter, c.pad.leave);
  if (c.pad.move_stick != PAD_STICK_RIGHT || c.pad.aim_stick != PAD_STICK_NONE) fail("sticks");
  for (int k = 0; k < CONFIG_CHEATS; k++)
    if (c.cheat[k]) fail("cheat %s is on without being asked for", config_cheat_names[k]);

  parse(&c, "[Cheats]\ninvincible = on\nInvincible Neighbours = yes\ninfinite-ammo = 1\n"
            "give_all = on\n");
  if (c.warnings) fail("the cheats were complained about %d times", c.warnings);
  if (!c.cheat[0] || !c.cheat[1] || !c.cheat[2] || c.cheat[3] || !c.cheat[4] || c.cheat[5])
    fail("cheats read as %d%d%d%d%d%d", c.cheat[0], c.cheat[1], c.cheat[2], c.cheat[3], c.cheat[4], c.cheat[5]);

  parse(&c, "[game]\nlevel = off\n[video]\nrefresh = auto\n");
  if (c.level != -1 || c.refresh != 0) fail("level off / refresh auto: %d, %d", c.level, c.refresh);
}

static void test_bad_lines(void) {
  Config c, was;
  config_defaults(&c);
  was = c;
  printf("-- complaints from here to the next rule are the test's own --\n");
  parse(&c,
    "hitbox = 170\n"              // before any section
    "[game]\n"
    "hitbox = 99\n"               // out of range
    "hitbox = lots\n"
    "level = 56\n"
    "blood = green\n"
    "skip_intro = maybe\n"
    "colour = blue\n"             // no such setting
    "just some words\n"           // not key = value
    "[vidoe]\n"                   // no such section...
    "fullscreen = off\n"          // ...so this is skipped, silently
    "[video\n"                    // no ]
    "fullscreen = off\n"
    "[video]\n"
    "widescreen = 32:9\n"
    "window_scale = 9\n"
    "[audio]\n"
    "volume = 101\n"
    "[controller]\n"
    "deadzone = 4\n"
    "move_stick = both\n"
    "[keyboard]\n"
    "fire = Space\n"              // not a button
    "y = Spacebar, Space\n"       // one bad name; the good one is kept
    "[hotkeys]\n"
    "quick_save = F5, F6, F7, F8, F10\n"  // one too many
    "[controller buttons]\n"
    "y = west, elbow\n");
  printf("-- end --\n");
  const int want_warnings = 19;
  if (c.warnings != want_warnings) fail("%d complaints, want %d", c.warnings, want_warnings);
  if (c.hitbox != was.hitbox || c.level != was.level || c.red_blood || c.skip_intro)
    fail("a refused [game] line changed something");
  if (!c.fullscreen) fail("a line in a section that does not exist was obeyed");
  if (c.widescreen != was.widescreen || c.window_scale != 1 || c.volume != 100 ||
      c.deadzone != 24 || c.pad.move_stick != PAD_STICK_LEFT)
    fail("a refused line changed something");
  if (c.key[0][BTN_Y][0] != SDLK_SPACE || c.key[0][BTN_Y][1] != SDLK_UNKNOWN)
    fail("'Spacebar, Space' should bind Space alone");
  if (c.hotkey[ACT_QUICK_SAVE][3] != SDLK_F8) fail("the first four keys of five should be kept");
  if (c.pad.game[BTN_Y][0] != SDL_CONTROLLER_BUTTON_X || c.pad.game[BTN_Y][1] != PAD_IN_NONE)
    fail("'west, elbow' should bind west alone");
}

static void test_bindings(void) {
  Config c;
  config_defaults(&c);
  parse(&c,
    "[keyboard]\n"
    "y = Space, Left Ctrl\n"
    "b = comma\n"
    "a = SEMICOLON\n"
    "x = Hash\n"
    "start = Enter\n"
    "select =\n"
    "l = none\n"
    "up = w\n"
    "r = Keypad 1, kp_does_not_matter_this_line_is_checked_below\n"
    "[keyboard player 2]\n"
    "up = I\n"
    "y = RShift\n"
    "[hotkeys]\n"
    "quit = Esc, F12\n"
    "fullscreen =\n"
    "[controller buttons]\n"
    "y = r2\n"
    "l = L1\n"
    "r = RightShoulder\n"
    "b = cross, a\n"              // the same button twice, by two of its names
    "x = Triangle, paddle1, touchpad, misc1\n"
    "next weapon = paddle3\n"
    "prev_item = none\n"
    "[controller hotkeys]\n"
    "quick_save = paddle2\n"
    "quick_load = L3, r3\n");
  if (c.warnings != 1) fail("%d complaints, want the 1 for the long name", c.warnings);
  if (c.key[0][BTN_Y][0] != SDLK_SPACE || c.key[0][BTN_Y][1] != SDLK_LCTRL) fail("y = Space, Left Ctrl");
  if (c.key[0][BTN_B][0] != SDLK_COMMA) fail("Comma");
  if (c.key[0][BTN_A][0] != SDLK_SEMICOLON) fail("Semicolon");
  if (c.key[0][BTN_X][0] != SDLK_HASH) fail("Hash");
  if (c.key[0][BTN_START][0] != SDLK_RETURN) fail("Enter is Return");
  if (c.key[0][BTN_SELECT][0] != SDLK_UNKNOWN) fail("an empty list should bind nothing");
  if (c.key[0][BTN_L][0] != SDLK_UNKNOWN) fail("none should bind nothing");
  if (c.key[0][BTN_UP][0] != SDLK_w) fail("a letter in lower case");
  if (c.key[0][BTN_R][0] != SDLK_KP_1) fail("Keypad 1");
  if (c.key[0][BTN_DOWN][0] != SDLK_DOWN) fail("a button the file left alone lost its default");
  if (c.key[1][BTN_UP][0] != SDLK_i || c.key[1][BTN_Y][0] != SDLK_RSHIFT) fail("player 2's keys");
  if (c.hotkey[ACT_QUIT][0] != SDLK_ESCAPE || c.hotkey[ACT_QUIT][1] != SDLK_F12) fail("quit = Esc, F12");
  if (c.hotkey[ACT_FULLSCREEN][0] != SDLK_UNKNOWN) fail("fullscreen should be unbound");
  if (c.hotkey[ACT_QUICK_SAVE][0] != SDLK_F5) fail("a hotkey the file left alone lost its default");
  if (c.pad.game[BTN_Y][0] != PAD_IN_RTRIGGER || c.pad.game[BTN_Y][1] != PAD_IN_NONE) fail("y = r2");
  if (c.pad.game[BTN_L][0] != SDL_CONTROLLER_BUTTON_LEFTSHOULDER || c.pad.game[BTN_L][1] != PAD_IN_NONE) fail("l = L1");
  if (c.pad.game[BTN_R][0] != SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) fail("r = RightShoulder");
  if (c.pad.cycle[PAD_CYCLE_NEXT_WEAPON][0] != SDL_CONTROLLER_BUTTON_PADDLE3 ||
      c.pad.cycle[PAD_CYCLE_NEXT_WEAPON][1] != PAD_IN_NONE) fail("next weapon = paddle3");
  if (c.pad.cycle[PAD_CYCLE_PREV_ITEM][0] != PAD_IN_NONE) fail("prev_item = none");
  if (c.pad.cycle[PAD_CYCLE_PREV_WEAPON][0] != PAD_IN_LTRIGGER ||
      c.pad.cycle[PAD_CYCLE_NEXT_ITEM][0] != SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
    fail("a selection the file left alone lost its default");
  // `l = L1` is the file from before the shoulders selected, and here L1 no
  // longer does (`prev_item = none`), so only `r = RightShoulder` is in two
  // places -- which is said, once.
  {
    const int before = c.warnings;
    config_check_pad(&c, "test");
    if (c.warnings != before + 1) fail("an input in two places was complained about %d times, want 1", c.warnings - before);
  }
  if (c.pad.game[BTN_B][0] != SDL_CONTROLLER_BUTTON_A || c.pad.game[BTN_B][1] != PAD_IN_NONE) fail("b = cross, a");
  if (c.pad.game[BTN_X][0] != SDL_CONTROLLER_BUTTON_Y || c.pad.game[BTN_X][1] != SDL_CONTROLLER_BUTTON_PADDLE1 ||
      c.pad.game[BTN_X][2] != SDL_CONTROLLER_BUTTON_TOUCHPAD || c.pad.game[BTN_X][3] != SDL_CONTROLLER_BUTTON_MISC1)
    fail("x = Triangle, paddle1, touchpad, misc1");
  if (c.pad.hot[ACT_QUICK_SAVE][0] != SDL_CONTROLLER_BUTTON_PADDLE2) fail("quick_save = paddle2");
  if (c.pad.hot[ACT_QUICK_LOAD][0] != SDL_CONTROLLER_BUTTON_LEFTSTICK ||
      c.pad.hot[ACT_QUICK_LOAD][1] != SDL_CONTROLLER_BUTTON_RIGHTSTICK) fail("quick_load = L3, r3");
  if (c.pad.hot[ACT_QUIT][0] != PAD_IN_NONE) fail("a pad action is bound that nobody bound");

  // The name that is printed for an input is a name that is read.
  for (int in = 0; in < SDL_CONTROLLER_BUTTON_MAX; in++)
    if (config_pad_input(config_pad_input_name(in)) != in)
      fail("pad input %d is printed as '%s', which does not read back", in, config_pad_input_name(in));
  if (config_pad_input(config_pad_input_name(PAD_IN_LTRIGGER)) != PAD_IN_LTRIGGER ||
      config_pad_input(config_pad_input_name(PAD_IN_RTRIGGER)) != PAD_IN_RTRIGGER)
    fail("a trigger's printed name does not read back");
  // ...and the same for every key the defaults use.
  Config d;
  config_defaults(&d);
  for (int a = 0; a < ACT_COUNT; a++)
    if (config_key(SDL_GetKeyName(d.hotkey[a][0])) != d.hotkey[a][0])
      fail("the key of %s is printed as '%s', which does not read back", config_action_names[a], SDL_GetKeyName(d.hotkey[a][0]));

  if (config_hotkey_of(&c, SDLK_F12) != ACT_QUIT) fail("F12 should be quit");
  if (config_hotkey_of(&c, SDLK_F11) != -1) fail("F11 was unbound and still does something");
  if (config_hotkey_of(&c, SDLK_UNKNOWN) != -1) fail("no key at all is an action");
}

static void test_duplicates(void) {
  Config c;
  config_defaults(&c);
  config_check(&c, "test");
  if (c.warnings) fail("the defaults bind a key twice (%d)", c.warnings);
  printf("-- complaints from here to the next rule are the test's own --\n");
  parse(&c, "[keyboard]\ny = F5\n[keyboard player 2]\nup = Up\n");
  config_check(&c, "test");
  printf("-- end --\n");
  if (c.warnings != 2) fail("%d complaints about keys bound twice, want 2", c.warnings);
  if (config_hotkey_of(&c, SDLK_F5) != ACT_QUICK_SAVE) fail("the hotkey should win");
  // ...and player 1 wins a key both players claim.
  ConfigKeys keys;
  uint16_t held[MOVIE_PORTS];
  memset(&keys, 0, sizeof keys);
  config_key_event(&c, &keys, SDLK_UP, true);
  config_keys_held(&keys, held);
  if (held[0] != (1u << BTN_UP) || held[1] != 0) fail("Up claimed twice: %03x %03x", held[0], held[1]);
}

static void test_held(void) {
  Config c;
  config_defaults(&c);
  parse(&c, "[keyboard player 2]\ny = G\n");
  ConfigKeys keys;
  uint16_t held[MOVIE_PORTS];
  memset(&keys, 0, sizeof keys);
  const uint16_t select = (uint16_t)(1u << BTN_SELECT);
  if (!config_key_event(&c, &keys, SDLK_RSHIFT, true)) fail("Right Shift is bound and was not taken");
  config_key_event(&c, &keys, SDLK_BACKSPACE, true);
  config_key_event(&c, &keys, SDLK_RSHIFT, false);
  config_keys_held(&keys, held);
  if (held[0] != select) fail("Select on two keys was let go with one still down: %03x", held[0]);
  config_key_event(&c, &keys, SDLK_BACKSPACE, false);
  config_keys_held(&keys, held);
  if (held[0] != 0) fail("Select is stuck: %03x", held[0]);
  if (config_key_event(&c, &keys, SDLK_p, true)) fail("an unbound key was taken");
  config_key_event(&c, &keys, SDLK_g, true);
  config_key_event(&c, &keys, SDLK_a, true);
  config_keys_held(&keys, held);
  if (held[0] != (1u << BTN_Y) || held[1] != (1u << BTN_Y)) fail("two players: %03x %03x", held[0], held[1]);
}

static void test_paths(void) {
  Config c;
  char out[CONFIG_PATH_MAX];
  config_defaults(&c);
  config_resolve(&c, "game.sfc", out, sizeof out);
  if (strcmp(out, "game.sfc")) fail("no file read: '%s'", out);
  snprintf(c.path, sizeof c.path, "zamn.ini");
  config_resolve(&c, "game.sfc", out, sizeof out);
  if (strcmp(out, "game.sfc")) fail("a file in this directory: '%s'", out);
  snprintf(c.path, sizeof c.path, "D:\\Games\\zamn\\zamn.ini");
  config_resolve(&c, "roms\\game.sfc", out, sizeof out);
  if (strcmp(out, "D:\\Games\\zamn\\roms\\game.sfc")) fail("a file elsewhere: '%s'", out);
  config_resolve(&c, "C:\\roms\\game.sfc", out, sizeof out);
  if (strcmp(out, "C:\\roms\\game.sfc")) fail("an absolute path: '%s'", out);
  config_resolve(&c, "/roms/game.sfc", out, sizeof out);
  if (strcmp(out, "/roms/game.sfc")) fail("a rooted path: '%s'", out);
  snprintf(c.path, sizeof c.path, "../cfg/zamn.ini");
  config_resolve(&c, "game.sfc", out, sizeof out);
  if (strcmp(out, "../cfg/game.sfc")) fail("forward slashes: '%s'", out);
}

// The file as it is written and read back through the file system, which is
// where a text-mode write turns every line ending into the platform's.
static void test_round_trip(const char* path) {
  if (!config_write_default(path)) { fail("cannot write '%s'", path); return; }
  Config want, got;
  config_defaults(&want);
  if (!config_load(&got, path)) { fail("cannot read '%s' back", path); return; }
  if (got.warnings) fail("the written file was complained about %d times", got.warnings);
  if (strcmp(got.path, path)) fail("the path was not kept");
  got.path[0] = 0;
  memset(got.path, 0, sizeof got.path);
  if (memcmp(&want, &got, sizeof want)) fail("the written file does not read back as the defaults");
  remove(path);
  Config none;
  if (config_load(&none, path)) fail("a file that is not there was read");
  if (memcmp(&want, &none, sizeof want)) fail("a failed load did not leave the defaults");
}

// The launcher's half: a `Config` written into a file that may already say
// things of its own.
static char* crlf(const char* text) {
  char* out = (char*)malloc(strlen(text) * 2 + 1);
  char* o = out;
  for (const char* p = text; *p; p++) {
    if (*p == '\n') *o++ = '\r';
    *o++ = *p;
  }
  *o = 0;
  return out;
}

static bool reads_as(const char* text, const Config* want, const char* what) {
  Config got;
  config_defaults(&got);
  parse(&got, text);
  if (got.warnings) { fail("%s: the written file was complained about %d times", what, got.warnings); return false; }
  if (memcmp(want, &got, sizeof got)) { fail("%s: the written file does not read back as what was written", what); return false; }
  return true;
}

static void test_writing(void) {
  Config def;
  config_defaults(&def);
  char* same = config_update_text(CONFIG_DEFAULT_TEXT, &def);
  if (!same || strcmp(same, CONFIG_DEFAULT_TEXT)) fail("writing the defaults changed the default file");
  free(same);

  // Everything off its default, the keys the format needs words for among it.
  Config c = def;
  snprintf(c.rom, sizeof c.rom, "roms/zamn (usa).sfc");
  c.skip_intro = true; c.level = 0; c.hitbox = 175; c.red_blood = true; c.high_scores = false;
  snprintf(c.high_scores_file, sizeof c.high_scores_file, "C:\\scores\\zamn.hiscore");
  c.fullscreen = false; c.widescreen = WIDE_AUTO; c.filter = SCALE_LINEAR;
  c.window_scale = 3; c.smoothing = false; c.refresh = 144; c.radar_flash = true; c.audio = false; c.volume = 35;
  c.pads = false; c.twin_stick = false; c.deadzone = 40;
  config_deadzone(&c.pad, c.deadzone);
  c.pad.move_stick = PAD_STICK_RIGHT; c.pad.aim_stick = PAD_STICK_NONE;
  pad_map_clear(c.pad.game[BTN_Y]);
  pad_map_add(c.pad.game[BTN_Y], SDL_CONTROLLER_BUTTON_X);
  pad_map_add(c.pad.game[BTN_Y], SDL_CONTROLLER_BUTTON_PADDLE2);
  pad_map_clear(c.pad.cycle[PAD_CYCLE_NEXT_ITEM]);
  pad_map_add(c.pad.hot[ACT_QUICK_SAVE], SDL_CONTROLLER_BUTTON_PADDLE1);
  c.key[0][BTN_START][0] = SDLK_SPACE; c.key[0][BTN_START][1] = SDLK_COMMA;
  c.key[0][BTN_START][2] = SDLK_SEMICOLON; c.key[0][BTN_START][3] = SDLK_HASH;
  c.key[1][BTN_B][0] = SDLK_KP_1;
  memset(c.hotkey[ACT_QUIT], 0, sizeof c.hotkey[ACT_QUIT]);
  c.hotkey[ACT_FULLSCREEN][1] = SDLK_f;
  for (int k = 0; k < CONFIG_CHEATS; k++) c.cheat[k] = true;

  // Into the default file as Windows writes it: every line still ends CRLF.
  char* dos = crlf(CONFIG_DEFAULT_TEXT);
  char* text = config_update_text(dos, &c);
  if (!text) fail("no text came back");
  else {
    reads_as(text, &c, "every setting");
    for (const char* p = text; *p; p++)
      if (*p == '\n' && (p == text || p[-1] != '\r')) { fail("a line lost its CR"); break; }
    if (!strstr(text, "start = Space, Comma, Semicolon, Hash\r\n")) fail("the start keys were not written by their words");
    if (strstr(text, "[game]") != strstr(text, "[game]\r\n")) fail("a section heading moved");
    free(text);
  }
  free(dos);

  // Into nothing: every section is made.
  text = config_update_text("", &c);
  if (text) reads_as(text, &c, "an empty file");
  free(text);

  // Into a file of the player's own: their names for things, their comments
  // and their spacing are kept, and nothing is said twice.
  text = config_update_text("; mine\n[Keyboard Player 1]\nB=Space\n; still mine\n"
                            "[controller buttons]\nprev_weapon = r1\n[video]\nfullscreen=on", &def);
  if (text) {
    reads_as(text, &def, "a file of the player's");
    if (!strstr(text, "; mine\n[Keyboard Player 1]\nB = Z\n")) fail("the player's keyboard section was not kept");
    if (!strstr(text, "; still mine\n")) fail("a comment was lost");
    if (strstr(text, "[keyboard]")) fail("a second keyboard section was made");
    if (!strstr(text, "prev_weapon = l2\n") || strstr(text, "previous_weapon")) fail("the short cycle name was not kept");
    if (!strstr(text, "fullscreen=on\n")) fail("a line whose value did not change was rewritten");
    if (strstr(text, "fullscreen=onwidescreen") || strstr(text, "fullscreen=on widescreen"))
      fail("a line was added onto the end of the last one");
  }
  free(text);

  char name[64];
  if (!config_key_name(SDLK_COMMA, name, sizeof name) || strcmp(name, "Comma")) fail("the comma key is not 'Comma'");
  if (config_key_name(SDLK_KP_COMMA, name, sizeof name)) fail("'%s' was called writable", name);
  if (config_key_name(SDLK_UNKNOWN, name, sizeof name)) fail("no key was called writable");
}

int main(int argc, char** argv) {
  test_default_text();
  test_settings();
  test_bad_lines();
  test_bindings();
  test_duplicates();
  test_held();
  test_paths();
  test_writing();
  if (argc > 1) test_round_trip(argv[1]);
  else printf("note: no scratch file named, so the file round trip was skipped.\n");
  if (failures) { printf("%d FAILED\n", failures); return 1; }
  printf("config: all checks passed\n");
  return 0;
}
