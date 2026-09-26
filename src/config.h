// The player's settings and bindings: `zamn.ini`.
//
// Every setting here had a command-line flag first, and a flag is the wrong
// place for the ones a player sets once: the ROM's path, the shape of the
// picture, which key fires. So they live in a file, the flags go on working,
// and **a flag beats the file** -- the file says how this player plays, a flag
// says how this run differs.
//
// ## Where it is
//
// `--config <file>` if one is named; otherwise `zamn.ini` in the working
// directory, otherwise `zamn.ini` beside the executable. When there is none the
// frontend writes one in the working directory, with every setting at its
// default and a comment on each, so that the file to edit is a file that
// exists (not for a movie or a `--frames` run, which are tests and leave
// nothing behind). `--no-config` reads nothing and writes nothing.
//
// A path in the file -- the ROM, the top scores -- that is not absolute is
// taken from the directory the file is in, not from wherever the program was
// started: a file that travels with its ROM keeps working.
//
// ## The format
//
// `[section]`, `key = value`, and a line that starts with `;` or `#` is a
// comment. There are no comments at the end of a line, because `;` and `#`
// are keys somebody may want to bind. Sections and keys are read without
// regard to case, and a space, a dot, a dash and an underscore are the same
// character in them, so `[keyboard player 2]` is `[keyboard_player_2]`.
//
// Nothing in the file can stop the game from starting. A line that cannot be
// read is reported with its line number, and the setting keeps its default;
// a file written for an older build, or a newer one, plays.
//
// ## Bindings
//
// A binding is a list, separated by commas: `select = Right Shift, Backspace`.
// Nothing after the `=` binds nothing. Four at most.
//
//   * **Keys** are named as SDL names them, which is what is printed on them:
//     `A`, `5`, `F5`, `Up`, `Space`, `Return`, `Left Ctrl`, `Keypad 1`. A few
//     spellings are taken as well -- `Enter`, `Esc`, `RShift`, `LCtrl` -- and
//     the three that the format needs for itself have words: `Comma`,
//     `Semicolon`, `Hash`.
//   * **Pad inputs** are named by position, because that is what is the same
//     from one pad to the next: `south east west north` for the face buttons
//     (Cross, Circle, Square, Triangle on a DualSense; A, B, X, Y on an Xbox
//     pad), `l1 r1` for the shoulders, `l2 r2` for the triggers, `l3 r3` for
//     the stick clicks, `dpup dpdown dpleft dpright`, `start`, `select`,
//     `guide`, `touchpad`, `misc1`, and `paddle1` to `paddle4`. SDL's own names
//     (`a`, `leftshoulder`, `back`...) and the usual others (`cross`, `lb`,
//     `rt`, `share`, `options`) are read too.
//
// `[keyboard]` and `[controller buttons]` bind the twelve SNES buttons, and
// `[controller buttons]` four things more that no SNES button does --
// `next_weapon`, `previous_weapon`, `next_item`, `previous_item` (`src/pad.h`,
// "The four things in the top corners"). A pad input bound to one of those is
// not also a SNES button, and `config_check` says so when a file has it as both;
// `[hotkeys]` and `[controller hotkeys]` bind what the frontend does -- quick
// save, fullscreen and the rest. One pad table serves both pads. The keyboard
// plays port 1, and `[keyboard player 2]`, which is empty unless the player
// fills it, plays port 2.
//
// A key in two places does the first thing only -- a hotkey before a button --
// and `config_check` says so at startup rather than leave it to be found.
//
// ## Why a header
//
// The same reason as `src/pad.h`: what is in here is parsing, which
// `tools/test_config.c` checks with no window and no device. In particular
// it checks that `CONFIG_DEFAULT_TEXT`, the file that gets written, reads back
// as exactly `config_defaults`, so the file cannot drift from the program.

#ifndef ZAMN_CONFIG_H
#define ZAMN_CONFIG_H

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "analysis/movie.h"
#include "pad.h"
#include "scale.h"

#define CONFIG_FILE "zamn.ini"
#define CONFIG_ROM_DEFAULT "Zombies Ate My Neighbors.sfc"
#define CONFIG_KEYS_MAX 4
#define CONFIG_PATH_MAX 1024
#define CONFIG_HITBOX_DEFAULT 150
#define CONFIG_CHEATS 6

// What the frontend does, as opposed to what the SNES pad does. The numbers are
// also the bits of `PadSet.hot_pressed` and the rows of `PadMap.hot`.
typedef enum {
  ACT_QUIT,
  ACT_TOGGLE_NATIVE,
  ACT_CYCLE_FILTER,
  ACT_CYCLE_WIDESCREEN,
  ACT_QUICK_SAVE,
  ACT_QUICK_LOAD,
  ACT_TOGGLE_SMOOTHING,
  ACT_FULLSCREEN,
  ACT_COUNT
} ConfigAction;

static const char* const config_action_names[ACT_COUNT] = {
  "quit", "toggle_native", "cycle_filter", "cycle_widescreen",
  "quick_save", "quick_load", "toggle_smoothing", "fullscreen",
};

// By `PAD_CYCLE_*`; `[controller buttons]` has these beside the SNES buttons.
static const char* const config_cycle_names[PAD_CYCLE_COUNT] = {
  "next_weapon", "previous_weapon", "next_item", "previous_item",
};
static const char* const config_cycle_short[PAD_CYCLE_COUNT] = {
  "next_weapon", "prev_weapon", "next_item", "prev_item",
};

// By `BTN_*`, which is the order of the bits in the SNES pad's word.
static const char* const config_button_names[12] = {
  "b", "y", "select", "start", "up", "down", "left", "right", "a", "x", "l", "r",
};

// [cheats], in the order of `CheatId` (`src/cheats.h`, which `src/main_sdl.c`
// holds this to): each the flag's name with underscores.
static const char* const config_cheat_names[CONFIG_CHEATS] = {
  "invincible", "invincible_neighbors", "infinite_ammo", "infinite_lives", "give_all", "always_run",
};

typedef struct {
  // [game]
  char rom[CONFIG_PATH_MAX];
  bool skip_intro;
  int level;    // -1: the game's own first level
  int hitbox;   // percent, 100..200
  bool red_blood;
  bool high_scores;
  char high_scores_file[CONFIG_PATH_MAX];  // empty: beside the ROM
  // [video]
  bool fullscreen;
  WideMode widescreen;
  ScaleMode filter;
  int window_scale;
  bool smoothing;
  int refresh;  // 0: what the system reports
  bool radar_flash;  // the console's one-at-a-time radar; src/radar.h
  // [audio]
  bool audio;
  int volume;   // percent, 0..100
  bool effect_overlay;  // src/sfx_overlay.h
  // [controller]
  bool pads;
  bool twin_stick;
  int deadzone;  // about a percent of the stick's travel; see `config_deadzone`
  PadMap pad;
  // [cheats], by `config_cheat_names`. Not under a movie, like the flags'
  // other settings that change the game.
  bool cheat[CONFIG_CHEATS];
  // [keyboard], [keyboard player 2], [hotkeys]. `SDLK_UNKNOWN` ends a list.
  SDL_Keycode key[MOVIE_PORTS][12][CONFIG_KEYS_MAX];
  SDL_Keycode hotkey[ACT_COUNT][CONFIG_KEYS_MAX];
  // Where it was read from (empty: nowhere), and how many lines were refused.
  char path[CONFIG_PATH_MAX];
  int warnings;
} Config;

// The file that is written when there is none. `tools/test_config.c` holds it
// to `config_defaults`: change one and the other has to follow.
static const char CONFIG_DEFAULT_TEXT[] =
  "; zamn.ini -- settings for Zombies Ate My Neighbors.\n"
  ";\n"
  "; Looked for in the directory the game is started from, then beside the\n"
  "; executable; --config <file> names another, --no-config reads none. An\n"
  "; option on the command line beats the same setting here. A line starting\n"
  "; with ; or # is a comment. A setting that is left out, or that cannot be\n"
  "; read, keeps its default, and the game says which line it was. Delete\n"
  "; this file and the next start writes a fresh one.\n"
  "\n"
  "[game]\n"
  "; The cartridge image. A path that is not absolute is taken from the\n"
  "; directory this file is in.\n"
  "rom = " CONFIG_ROM_DEFAULT "\n"
  "; on: start at the title menu instead of the logos and the story.\n"
  "skip_intro = off\n"
  "; off, or 0 to 55: the level a new game starts on. 1 to 48 are the numbered\n"
  "; levels, 0 and 50 to 55 the bonus rooms, 49 the credits.\n"
  "level = off\n"
  "; How far a player reaches for a pickup or a neighbour, and a weapon for a\n"
  "; creature, as a percentage of the game's own: 100 to 200.\n"
  "hitbox = 150\n"
  "; The game over's curtain: purple, as the cartridge has it, or red.\n"
  "blood = purple\n"
  "; Keep the top scores from one run to the next, beside the ROM unless a\n"
  "; file is named.\n"
  "high_scores = on\n"
  "high_scores_file =\n"
  "\n"
  "[video]\n"
  "fullscreen = on\n"
  "; off, 16:9, 16:10 or 21:9: draw more of the level either side, not a stretch.\n"
  "; auto: fullscreen, whichever fits the display; in a window, off.\n"
  "widescreen = off\n"
  "; sharp, integer or linear: how the picture is scaled to the window.\n"
  "filter = sharp\n"
  "; The size of the window when not fullscreen, in multiples of 512x480: 1 to 8.\n"
  "window_scale = 1\n"
  "; on: a picture for every refresh of a fast display, eased between the\n"
  "; game's sixty a second. off: the game's frames only.\n"
  "smoothing = on\n"
  "; auto, or the display's refresh rate in Hz when the system reports it wrong.\n"
  "refresh = auto\n"
  "; steady: the radar shows every neighbour at once. flashing: one at a time,\n"
  "; in turn, as the console does.\n"
  "radar = steady\n"
  "\n"
  "[audio]\n"
  "enabled = on\n"
  "; 0 to 100.\n"
  "volume = 100\n"
  "; on: sound effects the game's sound driver drops or cuts short when too\n"
  "; much is playing are heard anyway. off: as the console plays them.\n"
  "effect_overlay = on\n"
  "\n"
  "[controller]\n"
  "enabled = on\n"
  "; on: the aiming stick fires the held weapon the way it is pushed, while\n"
  "; the other one goes on steering.\n"
  "twin_stick = on\n"
  "; How far a stick moves before it counts, as a percentage of its travel:\n"
  "; 5 to 90.\n"
  "deadzone = 24\n"
  "; left, right or off.\n"
  "move_stick = left\n"
  "aim_stick = right\n"
  "\n"
  "; Bindings are lists, separated by commas, four at most; nothing after the =\n"
  "; binds nothing.\n"
  ";\n"
  "; Pad inputs, by position: south east west north (Cross Circle Square\n"
  "; Triangle, or A B X Y on an Xbox pad), l1 r1 (shoulders), l2 r2 (triggers),\n"
  "; l3 r3 (stick clicks), dpup dpdown dpleft dpright, start, select, guide,\n"
  "; touchpad, misc1, paddle1 paddle2 paddle3 paddle4.\n"
  ";\n"
  "; The SNES buttons, for both pads. In this game Y fires, B changes weapon,\n"
  "; A changes item, X uses the item, and L and R both bring up the radar.\n"
  "[controller buttons]\n"
  "up = dpup\n"
  "down = dpdown\n"
  "left = dpleft\n"
  "right = dpright\n"
  "b = south\n"
  "a = east\n"
  "y = west\n"
  "x = north\n"
  "l = touchpad\n"
  "r = l3\n"
  "start = start\n"
  "select = select\n"
  "; Not SNES buttons: B and A only go forwards. These go either way, and\n"
  "; work while firing. An input bound here is not also a button above.\n"
  "next_weapon = r2\n"
  "previous_weapon = l2\n"
  "next_item = r1\n"
  "previous_item = l1\n"
  "\n"
  "; What the frontend does, from the pad. Unbound by default: these fire on a\n"
  "; single press. (Start and Select held together for a second always quits.)\n"
  "[controller hotkeys]\n"
  "quick_save =\n"
  "quick_load =\n"
  "toggle_smoothing =\n"
  "cycle_widescreen =\n"
  "cycle_filter =\n"
  "fullscreen =\n"
  "toggle_native =\n"
  "quit =\n"
  "\n"
  "; Keys, by the name printed on them: A, 5, F5, Up, Space, Return, Tab,\n"
  "; Backspace, Left Shift, Right Ctrl, Keypad 1... and Comma, Semicolon and\n"
  "; Hash for those three.\n"
  ";\n"
  "; Player 1.\n"
  "[keyboard]\n"
  "up = Up\n"
  "down = Down\n"
  "left = Left\n"
  "right = Right\n"
  "b = Z\n"
  "a = X\n"
  "y = A\n"
  "x = S\n"
  "l = Q\n"
  "r = W\n"
  "start = Return\n"
  "select = Right Shift, Backspace\n"
  "\n"
  "; Player 2, for two at one keyboard. Unbound by default.\n"
  "[keyboard player 2]\n"
  "up =\n"
  "down =\n"
  "left =\n"
  "right =\n"
  "b =\n"
  "a =\n"
  "y =\n"
  "x =\n"
  "l =\n"
  "r =\n"
  "start =\n"
  "select =\n"
  "\n"
  "; What the frontend does, from the keyboard. (Alt+Return is always\n"
  "; fullscreen as well.)\n"
  "[hotkeys]\n"
  "quit = Escape\n"
  "; The C port's routines, or the cartridge's own in their place.\n"
  "toggle_native = F1\n"
  "cycle_filter = F2\n"
  "cycle_widescreen = F4\n"
  "quick_save = F5\n"
  "toggle_smoothing = F6\n"
  "quick_load = F9\n"
  "fullscreen = F11\n"
  "\n"
  "; Each off unless turned on here or by its option, --invincible and the\n"
  "; rest. While any is on the top scores are read and not written.\n"
  "[cheats]\n"
  "; Nothing hurts a player.\n"
  "invincible = off\n"
  "; Nothing hurts a neighbour, and the tourists do not turn into werewolves.\n"
  "invincible_neighbors = off\n"
  "; Weapons and items are never used up. Gives nothing.\n"
  "infinite_ammo = off\n"
  "; Dying does not cost a life.\n"
  "infinite_lives = off\n"
  "; Every weapon and every item when a game starts and when a quick save is\n"
  "; loaded. Once: they run out unless infinite_ammo is on as well.\n"
  "give_all = off\n"
  "; The running shoes, always.\n"
  "always_run = off\n";

// --- small things -------------------------------------------------------------

static inline void config_warn(Config* c, const char* name, int line, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (line > 0) printf("config: %s:%d: ", name, line);
  else printf("config: %s: ", name);
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  c->warnings++;
}

// Trim in place, both ends; returns the new start.
static inline char* config_trim(char* s) {
  while (*s && isspace((unsigned char)*s)) s++;
  char* end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
  return s;
}

// A section's or a key's name as it is compared: lower case, and a space, a
// dot and a dash are an underscore.
static inline void config_fold(char* s) {
  for (; *s; s++) {
    if (*s == ' ' || *s == '.' || *s == '-' || *s == '\t') *s = '_';
    else *s = (char)tolower((unsigned char)*s);
  }
}

static inline bool config_same(const char* a, const char* b) {
  for (; *a && *b; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return *a == *b;
}

static inline bool config_bool(const char* v, bool* out) {
  static const char* const yes[] = {"on", "yes", "true", "1"};
  static const char* const no[] = {"off", "no", "false", "0"};
  for (int i = 0; i < 4; i++) {
    if (config_same(v, yes[i])) { *out = true; return true; }
    if (config_same(v, no[i])) { *out = false; return true; }
  }
  return false;
}

static inline bool config_int(const char* v, int lo, int hi, int* out) {
  char* end = NULL;
  const long n = strtol(v, &end, 10);
  if (end == v || *end || n < lo || n > hi) return false;
  *out = (int)n;
  return true;
}

// The deadzone as counts. "A percentage of the stick's travel" to within 2%:
// the scale is chosen so that the default, 24, is exactly the 8,000 that
// `src/pad.h` measured its way to, and it is released at three quarters of
// that, which is that header's 6,000.
static inline void config_deadzone(PadMap* m, int percent) {
  m->enter = percent * 1000 / 3;
  if (m->enter > 32000) m->enter = 32000;
  m->leave = m->enter * 3 / 4;
}

// --- names ----------------------------------------------------------------------

// A key by name. SDL's names, which are the legends on the keys, and a few
// more: the common other spellings, and words for the keys this format uses.
static inline SDL_Keycode config_key(const char* name) {
  static const struct { const char* alias; const char* sdl; } aliases[] = {
    {"Enter", "Return"}, {"Esc", "Escape"}, {"Del", "Delete"}, {"Ins", "Insert"},
    {"PgUp", "PageUp"}, {"PgDn", "PageDown"}, {"Page Up", "PageUp"}, {"Page Down", "PageDown"},
    {"LShift", "Left Shift"}, {"RShift", "Right Shift"},
    {"LCtrl", "Left Ctrl"}, {"RCtrl", "Right Ctrl"},
    {"LAlt", "Left Alt"}, {"RAlt", "Right Alt"},
    {"Comma", ","}, {"Semicolon", ";"}, {"Hash", "#"}, {"Period", "."},
    {"Slash", "/"}, {"Backslash", "\\"}, {"Minus", "-"}, {"Equals", "="},
    {"Quote", "'"}, {"Backquote", "`"}, {"Left Bracket", "["}, {"Right Bracket", "]"},
  };
  for (int i = 0; i < (int)(sizeof aliases / sizeof *aliases); i++)
    if (config_same(name, aliases[i].alias)) return SDL_GetKeyFromName(aliases[i].sdl);
  return SDL_GetKeyFromName(name);
}

// A pad input by name, or `PAD_IN_NONE`.
static inline int config_pad_input(const char* name) {
  static const struct { const char* name; int in; } names[] = {
    {"south", SDL_CONTROLLER_BUTTON_A}, {"cross", SDL_CONTROLLER_BUTTON_A},
    {"east", SDL_CONTROLLER_BUTTON_B}, {"circle", SDL_CONTROLLER_BUTTON_B},
    {"west", SDL_CONTROLLER_BUTTON_X}, {"square", SDL_CONTROLLER_BUTTON_X},
    {"north", SDL_CONTROLLER_BUTTON_Y}, {"triangle", SDL_CONTROLLER_BUTTON_Y},
    {"l1", SDL_CONTROLLER_BUTTON_LEFTSHOULDER}, {"lb", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {"r1", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER}, {"rb", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {"l2", PAD_IN_LTRIGGER}, {"lt", PAD_IN_LTRIGGER}, {"lefttrigger", PAD_IN_LTRIGGER},
    {"r2", PAD_IN_RTRIGGER}, {"rt", PAD_IN_RTRIGGER}, {"righttrigger", PAD_IN_RTRIGGER},
    {"l3", SDL_CONTROLLER_BUTTON_LEFTSTICK}, {"r3", SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    {"select", SDL_CONTROLLER_BUTTON_BACK}, {"share", SDL_CONTROLLER_BUTTON_BACK},
    {"create", SDL_CONTROLLER_BUTTON_BACK}, {"view", SDL_CONTROLLER_BUTTON_BACK},
    {"options", SDL_CONTROLLER_BUTTON_START}, {"menu", SDL_CONTROLLER_BUTTON_START},
    {"home", SDL_CONTROLLER_BUTTON_GUIDE}, {"ps", SDL_CONTROLLER_BUTTON_GUIDE},
    {"mute", SDL_CONTROLLER_BUTTON_MISC1}, {"capture", SDL_CONTROLLER_BUTTON_MISC1},
  };
  for (int i = 0; i < (int)(sizeof names / sizeof *names); i++)
    if (config_same(name, names[i].name)) return names[i].in;
  // SDL's own: a b x y back guide start leftstick rightstick leftshoulder
  // rightshoulder dpup dpdown dpleft dpright misc1 paddle1..4 touchpad.
  char lower[32];
  snprintf(lower, sizeof lower, "%s", name);
  config_fold(lower);
  const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(lower);
  return b == SDL_CONTROLLER_BUTTON_INVALID ? PAD_IN_NONE : (int)b;
}

// ...and the name the file's own comments use for it, for saying what is bound.
static inline const char* config_pad_input_name(int in) {
  switch (in) {
    case SDL_CONTROLLER_BUTTON_A:             return "south";
    case SDL_CONTROLLER_BUTTON_B:             return "east";
    case SDL_CONTROLLER_BUTTON_X:             return "west";
    case SDL_CONTROLLER_BUTTON_Y:             return "north";
    case SDL_CONTROLLER_BUTTON_BACK:          return "select";
    case SDL_CONTROLLER_BUTTON_GUIDE:         return "guide";
    case SDL_CONTROLLER_BUTTON_START:         return "start";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "l3";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "r3";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "l1";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "r1";
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return "dpup";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return "dpdown";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return "dpleft";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return "dpright";
    case SDL_CONTROLLER_BUTTON_MISC1:         return "misc1";
    case SDL_CONTROLLER_BUTTON_PADDLE1:       return "paddle1";
    case SDL_CONTROLLER_BUTTON_PADDLE2:       return "paddle2";
    case SDL_CONTROLLER_BUTTON_PADDLE3:       return "paddle3";
    case SDL_CONTROLLER_BUTTON_PADDLE4:       return "paddle4";
    case SDL_CONTROLLER_BUTTON_TOUCHPAD:      return "touchpad";
    case PAD_IN_LTRIGGER:                     return "l2";
    case PAD_IN_RTRIGGER:                     return "r2";
    default:                                  return "?";
  }
}

static inline int config_name_index(const char* key, const char* const* names, int count) {
  for (int i = 0; i < count; i++)
    if (!strcmp(key, names[i])) return i;
  return -1;
}

// --- the defaults ----------------------------------------------------------------

static inline void config_defaults(Config* c) {
  memset(c, 0, sizeof *c);
  snprintf(c->rom, sizeof c->rom, "%s", CONFIG_ROM_DEFAULT);
  c->level = -1;
  c->hitbox = CONFIG_HITBOX_DEFAULT;
  c->high_scores = true;
  c->fullscreen = true;
  c->widescreen = WIDE_OFF;
  c->filter = SCALE_SHARP;
  c->window_scale = 1;
  c->smoothing = true;
  c->audio = true;
  c->volume = 100;
  c->effect_overlay = true;
  c->pads = true;
  c->twin_stick = true;
  c->deadzone = 24;
  pad_map_default(&c->pad);
  config_deadzone(&c->pad, c->deadzone);
  static const struct { int btn; SDL_Keycode k[2]; } keys[] = {
    {BTN_UP, {SDLK_UP, 0}}, {BTN_DOWN, {SDLK_DOWN, 0}},
    {BTN_LEFT, {SDLK_LEFT, 0}}, {BTN_RIGHT, {SDLK_RIGHT, 0}},
    {BTN_B, {SDLK_z, 0}}, {BTN_A, {SDLK_x, 0}}, {BTN_Y, {SDLK_a, 0}}, {BTN_X, {SDLK_s, 0}},
    {BTN_L, {SDLK_q, 0}}, {BTN_R, {SDLK_w, 0}},
    {BTN_START, {SDLK_RETURN, 0}}, {BTN_SELECT, {SDLK_RSHIFT, SDLK_BACKSPACE}},
  };
  for (int i = 0; i < (int)(sizeof keys / sizeof *keys); i++)
    for (int j = 0; j < 2; j++) c->key[0][keys[i].btn][j] = keys[i].k[j];
  c->hotkey[ACT_QUIT][0] = SDLK_ESCAPE;
  c->hotkey[ACT_TOGGLE_NATIVE][0] = SDLK_F1;
  c->hotkey[ACT_CYCLE_FILTER][0] = SDLK_F2;
  c->hotkey[ACT_CYCLE_WIDESCREEN][0] = SDLK_F4;
  c->hotkey[ACT_QUICK_SAVE][0] = SDLK_F5;
  c->hotkey[ACT_TOGGLE_SMOOTHING][0] = SDLK_F6;
  c->hotkey[ACT_QUICK_LOAD][0] = SDLK_F9;
  c->hotkey[ACT_FULLSCREEN][0] = SDLK_F11;
}

// --- reading ----------------------------------------------------------------------

// A list of keys. A name that is not a key is refused and the rest are kept;
// the list replaces the default even when it comes out empty, because "this
// binds nothing" is a thing a player says on purpose.
static inline void config_key_list(Config* c, const char* name, int line,
                                   SDL_Keycode out[CONFIG_KEYS_MAX], char* value) {
  int n = 0;
  for (int i = 0; i < CONFIG_KEYS_MAX; i++) out[i] = SDLK_UNKNOWN;
  for (char* tok = value; tok && *tok;) {
    char* comma = strchr(tok, ',');
    if (comma) *comma = 0;
    char* t = config_trim(tok);
    if (*t && !config_same(t, "none")) {
      const SDL_Keycode k = config_key(t);
      if (k == SDLK_UNKNOWN) config_warn(c, name, line, "'%s' is not a key this knows the name of", t);
      else if (n == CONFIG_KEYS_MAX) config_warn(c, name, line, "'%s' is one key too many (%d at most)", t, CONFIG_KEYS_MAX);
      else {
        bool have = false;
        for (int i = 0; i < n; i++) have = have || out[i] == k;
        if (!have) out[n++] = k;
      }
    }
    tok = comma ? comma + 1 : NULL;
  }
}

static inline void config_pad_list(Config* c, const char* name, int line,
                                   int16_t out[PAD_BIND_MAX], char* value) {
  pad_map_clear(out);
  for (char* tok = value; tok && *tok;) {
    char* comma = strchr(tok, ',');
    if (comma) *comma = 0;
    char* t = config_trim(tok);
    if (*t && !config_same(t, "none")) {
      const int in = config_pad_input(t);
      if (in == PAD_IN_NONE) config_warn(c, name, line, "'%s' is not a pad input this knows the name of", t);
      else if (!pad_map_add(out, in)) {
        bool have = false;
        for (int i = 0; i < PAD_BIND_MAX; i++) have = have || out[i] == in;
        if (!have) config_warn(c, name, line, "'%s' is one input too many (%d at most)", t, PAD_BIND_MAX);
      }
    }
    tok = comma ? comma + 1 : NULL;
  }
}

static inline bool config_stick(const char* v, int* out) {
  if (config_same(v, "left")) { *out = PAD_STICK_LEFT; return true; }
  if (config_same(v, "right")) { *out = PAD_STICK_RIGHT; return true; }
  if (config_same(v, "off") || config_same(v, "none")) { *out = PAD_STICK_NONE; return true; }
  return false;
}

// One `key = value` of one section. False for a key the section does not have;
// a value that cannot be read is reported here and is not that.
static inline bool config_set(Config* c, const char* name, int line,
                              const char* section, const char* key, char* v) {
  #define CONFIG_BAD(what) config_warn(c, name, line, "%s wants %s, not '%s'", key, what, v)
  if (!strcmp(section, "game")) {
    if (!strcmp(key, "rom")) { if (*v) snprintf(c->rom, sizeof c->rom, "%s", v); else CONFIG_BAD("a path"); }
    else if (!strcmp(key, "skip_intro")) { if (!config_bool(v, &c->skip_intro)) CONFIG_BAD("on or off"); }
    else if (!strcmp(key, "level")) {
      // Not `config_bool`: 0 is a level here, not a way of saying off.
      if (config_same(v, "off") || config_same(v, "none")) c->level = -1;
      else if (!config_int(v, 0, 55, &c->level)) CONFIG_BAD("off or 0 to 55");
    }
    else if (!strcmp(key, "hitbox")) { if (!config_int(v, 100, 200, &c->hitbox)) CONFIG_BAD("100 to 200"); }
    else if (!strcmp(key, "blood")) {
      if (config_same(v, "red")) c->red_blood = true;
      else if (config_same(v, "purple")) c->red_blood = false;
      else CONFIG_BAD("purple or red");
    }
    else if (!strcmp(key, "high_scores")) { if (!config_bool(v, &c->high_scores)) CONFIG_BAD("on or off"); }
    else if (!strcmp(key, "high_scores_file")) snprintf(c->high_scores_file, sizeof c->high_scores_file, "%s", v);
    else return false;
  } else if (!strcmp(section, "video")) {
    if (!strcmp(key, "fullscreen")) { if (!config_bool(v, &c->fullscreen)) CONFIG_BAD("on or off"); }
    else if (!strcmp(key, "widescreen")) { if (!wide_setting_parse(v, &c->widescreen)) CONFIG_BAD("off, 16:9, 16:10, 21:9 or auto"); }
    // Retired: square pixels were a choice once, and 4:3 is the only shape now.
    // Still taken, and ignored, so that a file from before does not complain.
    else if (!strcmp(key, "aspect")) {}
    else if (!strcmp(key, "filter")) { if (!scale_parse(v, &c->filter)) CONFIG_BAD("sharp, integer or linear"); }
    else if (!strcmp(key, "window_scale")) { if (!config_int(v, 1, SCALE_MAX_STAGE, &c->window_scale)) CONFIG_BAD("1 to 8"); }
    else if (!strcmp(key, "smoothing")) { if (!config_bool(v, &c->smoothing)) CONFIG_BAD("on or off"); }
    else if (!strcmp(key, "refresh")) {
      if (config_same(v, "auto")) c->refresh = 0;
      else if (!config_int(v, 24, 1000, &c->refresh)) CONFIG_BAD("auto or a rate in Hz");
    }
    else if (!strcmp(key, "radar")) {
      if (config_same(v, "flashing")) c->radar_flash = true;
      else if (config_same(v, "steady")) c->radar_flash = false;
      else CONFIG_BAD("steady or flashing");
    }
    else return false;
  } else if (!strcmp(section, "audio")) {
    if (!strcmp(key, "enabled")) { if (!config_bool(v, &c->audio)) CONFIG_BAD("on or off"); }
    else if (!strcmp(key, "volume")) { if (!config_int(v, 0, 100, &c->volume)) CONFIG_BAD("0 to 100"); }
    else if (!strcmp(key, "effect_overlay")) { if (!config_bool(v, &c->effect_overlay)) CONFIG_BAD("on or off"); }
    else return false;
  } else if (!strcmp(section, "controller")) {
    if (!strcmp(key, "enabled")) { if (!config_bool(v, &c->pads)) CONFIG_BAD("on or off"); }
    else if (!strcmp(key, "twin_stick")) { if (!config_bool(v, &c->twin_stick)) CONFIG_BAD("on or off"); }
    else if (!strcmp(key, "deadzone")) {
      if (config_int(v, 5, 90, &c->deadzone)) config_deadzone(&c->pad, c->deadzone);
      else CONFIG_BAD("5 to 90");
    }
    else if (!strcmp(key, "move_stick")) { if (!config_stick(v, &c->pad.move_stick)) CONFIG_BAD("left, right or off"); }
    else if (!strcmp(key, "aim_stick")) { if (!config_stick(v, &c->pad.aim_stick)) CONFIG_BAD("left, right or off"); }
    else return false;
  } else if (!strcmp(section, "cheats")) {
    int k = config_name_index(key, config_cheat_names, CONFIG_CHEATS);
    if (k < 0 && !strcmp(key, "invincible_neighbours")) k = 1;
    if (k < 0) return false;
    if (!config_bool(v, &c->cheat[k])) CONFIG_BAD("on or off");
  } else {
    const bool pad_buttons = !strcmp(section, "controller_buttons");
    const bool pad_hot = !strcmp(section, "controller_hotkeys");
    const bool keys1 = !strcmp(section, "keyboard") || !strcmp(section, "keyboard_player_1");
    const bool keys2 = !strcmp(section, "keyboard_player_2");
    const bool hot = !strcmp(section, "hotkeys") || !strcmp(section, "keyboard_hotkeys");
    if (pad_buttons || keys1 || keys2) {
      const int b = config_name_index(key, config_button_names, 12);
      if (b < 0) {
        int k = config_name_index(key, config_cycle_names, PAD_CYCLE_COUNT);
        if (k < 0) k = config_name_index(key, config_cycle_short, PAD_CYCLE_COUNT);
        if (k < 0 || !pad_buttons) return false;
        config_pad_list(c, name, line, c->pad.cycle[k], v);
        return true;
      }
      if (pad_buttons) config_pad_list(c, name, line, c->pad.game[b], v);
      else config_key_list(c, name, line, c->key[keys2 ? 1 : 0][b], v);
    } else if (pad_hot || hot) {
      if (!strcmp(key, "toggle_aspect")) return true;  // retired, as `aspect` is
      const int a = config_name_index(key, config_action_names, ACT_COUNT);
      if (a < 0) return false;
      if (pad_hot) config_pad_list(c, name, line, c->pad.hot[a], v);
      else config_key_list(c, name, line, c->hotkey[a], v);
    } else return false;
  }
  #undef CONFIG_BAD
  return true;
}

static inline bool config_section_known(const char* s) {
  static const char* const known[] = {
    "game", "video", "audio", "controller", "controller_buttons", "controller_hotkeys",
    "keyboard", "keyboard_player_1", "keyboard_player_2", "hotkeys", "keyboard_hotkeys", "cheats",
  };
  return config_name_index(s, known, (int)(sizeof known / sizeof *known)) >= 0;
}

// Read settings out of `text` over whatever `c` holds. `name` is only for the
// messages.
static inline void config_parse(Config* c, const char* text, const char* name) {
  char section[64] = "";
  bool section_ok = false;
  int line = 0;
  // A UTF-8 byte order mark, which Notepad may have put there.
  if (!strncmp(text, "\xef\xbb\xbf", 3)) text += 3;
  while (*text) {
    const char* eol = strchr(text, '\n');
    const size_t len = eol ? (size_t)(eol - text) : strlen(text);
    char buf[CONFIG_PATH_MAX + 128];
    const size_t take = len < sizeof buf - 1 ? len : sizeof buf - 1;
    memcpy(buf, text, take);
    buf[take] = 0;
    text += len + (eol ? 1 : 0);
    line++;
    char* s = config_trim(buf);
    if (!*s || *s == ';' || *s == '#') continue;
    if (*s == '[') {
      char* close = strchr(s, ']');
      if (!close) { config_warn(c, name, line, "a section wants a closing ]"); section_ok = false; continue; }
      *close = 0;
      char* sec = config_trim(s + 1);
      config_fold(sec);
      snprintf(section, sizeof section, "%s", sec);
      section_ok = config_section_known(section);
      if (!section_ok) config_warn(c, name, line, "there is no section [%s]; its lines are skipped", sec);
      continue;
    }
    if (!section_ok) {
      if (!section[0]) config_warn(c, name, line, "a setting before any [section]");
      continue;
    }
    char* eq = strchr(s, '=');
    if (!eq) { config_warn(c, name, line, "'%s' is not key = value", s); continue; }
    *eq = 0;
    char* key = config_trim(s);
    char* value = config_trim(eq + 1);
    config_fold(key);
    if (!config_set(c, name, line, section, key, value))
      config_warn(c, name, line, "[%s] has no setting '%s'", section, key);
  }
}

// A key in two places does the first thing only. Says which.
static inline void config_check(Config* c, const char* name) {
  // Every binding as (key, what it is), hotkeys first, in the order they win.
  struct { SDL_Keycode k; char what[40]; } seen[(ACT_COUNT + 12 * MOVIE_PORTS) * CONFIG_KEYS_MAX];
  int n = 0;
  for (int pass = 0; pass <= MOVIE_PORTS; pass++) {
    const int rows = pass == 0 ? ACT_COUNT : 12;
    for (int r = 0; r < rows; r++) {
      const SDL_Keycode* list = pass == 0 ? c->hotkey[r] : c->key[pass - 1][r];
      for (int i = 0; i < CONFIG_KEYS_MAX && list[i] != SDLK_UNKNOWN; i++) {
        char what[40];
        if (pass == 0) snprintf(what, sizeof what, "%s", config_action_names[r]);
        else snprintf(what, sizeof what, "player %d's %s", pass, config_button_names[r]);
        for (int j = 0; j < n; j++)
          if (seen[j].k == list[i])
            config_warn(c, name, 0, "the key %s is %s and %s; it will only be %s",
                        SDL_GetKeyName(list[i]), seen[j].what, what, seen[j].what);
        seen[n].k = list[i];
        snprintf(seen[n].what, sizeof seen[n].what, "%s", what);
        n++;
      }
    }
  }
}

// ...and a pad input that selects is not also a SNES button. Said once per
// input, because a file from before the shoulders selected has two of them.
static inline void config_check_pad(Config* c, const char* name) {
  for (int b = 0; b < 12; b++)
    for (int i = 0; i < PAD_BIND_MAX && c->pad.game[b][i] != PAD_IN_NONE; i++) {
      const int in = c->pad.game[b][i];
      for (int k = 0; k < PAD_CYCLE_COUNT; k++)
        for (int j = 0; j < PAD_BIND_MAX && c->pad.cycle[k][j] != PAD_IN_NONE; j++)
          if (c->pad.cycle[k][j] == in)
            config_warn(c, name, 0, "the pad's %s is %s and %s; it will only be %s",
                        config_pad_input_name(in), config_cycle_names[k],
                        config_button_names[b], config_cycle_names[k]);
    }
}

// --- files -----------------------------------------------------------------------

static inline bool config_exists(const char* path) {
  FILE* f = fopen(path, "rb");
  if (f) fclose(f);
  return f != NULL;
}

// The file to read: the one asked for, or `zamn.ini` here, or beside the
// executable. False, and `out` empty, when there is none.
static inline bool config_locate(const char* asked, char* out, size_t size) {
  out[0] = 0;
  if (asked) { snprintf(out, size, "%s", asked); return true; }
  if (config_exists(CONFIG_FILE)) { snprintf(out, size, "%s", CONFIG_FILE); return true; }
  char* base = SDL_GetBasePath();
  if (base) {
    char path[CONFIG_PATH_MAX];
    snprintf(path, sizeof path, "%s%s", base, CONFIG_FILE);
    SDL_free(base);
    if (config_exists(path)) { snprintf(out, size, "%s", path); return true; }
  }
  return false;
}

// Defaults, then the file over them. False when the file cannot be read, and
// `c` is then the defaults.
static inline bool config_load(Config* c, const char* path) {
  config_defaults(c);
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* text = len >= 0 && len < (1 << 20) ? (char*)malloc((size_t)len + 1) : NULL;
  if (!text) { fclose(f); return false; }
  const size_t got = fread(text, 1, (size_t)len, f);
  fclose(f);
  text[got] = 0;
  config_parse(c, text, path);
  free(text);
  snprintf(c->path, sizeof c->path, "%s", path);
  config_check(c, path);
  config_check_pad(c, path);
  return true;
}

static inline bool config_write_default(const char* path) {
  FILE* f = fopen(path, "w");  // text mode: the platform's own line endings
  if (!f) return false;
  const bool ok = fputs(CONFIG_DEFAULT_TEXT, f) >= 0;
  return fclose(f) == 0 && ok;
}

static inline bool config_path_absolute(const char* p) {
  if (p[0] == '/' || p[0] == '\\') return true;
  return isalpha((unsigned char)p[0]) && p[1] == ':';
}

// A path out of the file, as a path the program can open: from the directory
// the file is in, unless it is absolute or the file was in this directory.
static inline void config_resolve(const Config* c, const char* rel, char* out, size_t size) {
  const char* slash = NULL;
  for (const char* p = c->path; *p; p++)
    if (*p == '/' || *p == '\\') slash = p;
  if (!slash || config_path_absolute(rel)) { snprintf(out, size, "%s", rel); return; }
  snprintf(out, size, "%.*s%s", (int)(slash - c->path + 1), c->path, rel);
}

// --- writing ----------------------------------------------------------------------
//
// The other direction, for the launcher (`src/launcher.c`): a `Config` put
// back into a file. Not by writing the file afresh -- it is the player's, and
// may have their own comments in it -- but by setting each value where it is.
// A line that sets it is rewritten in place, one that is missing is added
// after the last setting of its section, and a missing section goes on the
// end of the file. Everything else, comments and line endings included, stays
// as it was. `tools/test_config.c` holds this to two things: the defaults
// written into the default file change not one byte of it, and whatever is
// written reads back as what was written.

// A key's name as the file spells it. False for a key the file cannot hold:
// one with no name, or one whose name does not read back as the same key.
// `Keypad ,` is one, because a list would split it in two.
static inline bool config_key_name(SDL_Keycode k, char* out, size_t size) {
  const char* name = k == SDLK_COMMA ? "Comma" : k == SDLK_SEMICOLON ? "Semicolon"
                   : k == SDLK_HASH ? "Hash" : SDL_GetKeyName(k);
  snprintf(out, size, "%s", name);
  return k != SDLK_UNKNOWN && *name && !strchr(name, ',') && config_key(name) == k;
}

static inline void config_join(char* out, size_t size, size_t* n, const char* name) {
  if (*n >= size) return;
  const int wrote = snprintf(out + *n, size - *n, "%s%s", *n ? ", " : "", name);
  if (wrote > 0) *n += (size_t)wrote;
}

static inline void config_key_list_text(const SDL_Keycode list[CONFIG_KEYS_MAX], char* out, size_t size) {
  size_t n = 0;
  out[0] = 0;
  for (int i = 0; i < CONFIG_KEYS_MAX && list[i] != SDLK_UNKNOWN; i++) {
    char name[64];
    if (config_key_name(list[i], name, sizeof name)) config_join(out, size, &n, name);
  }
}

static inline void config_pad_list_text(const int16_t list[PAD_BIND_MAX], char* out, size_t size) {
  size_t n = 0;
  out[0] = 0;
  for (int i = 0; i < PAD_BIND_MAX && list[i] != PAD_IN_NONE; i++) {
    const char* name = config_pad_input_name(list[i]);
    if (strcmp(name, "?")) config_join(out, size, &n, name);
  }
}

// The sections as the parser folds them, and as a new one is headed.
static const char* const config_section_heads[][2] = {
  {"game", "game"}, {"video", "video"}, {"audio", "audio"}, {"controller", "controller"},
  {"controller_buttons", "controller buttons"}, {"controller_hotkeys", "controller hotkeys"},
  {"keyboard", "keyboard"}, {"keyboard_player_2", "keyboard player 2"}, {"hotkeys", "hotkeys"},
  {"cheats", "cheats"},
};

// The two sections with a second name, and the cycle keys with a short one,
// as the names the writer uses.
static inline const char* config_section_canonical(const char* folded) {
  if (!strcmp(folded, "keyboard_player_1")) return "keyboard";
  if (!strcmp(folded, "keyboard_hotkeys")) return "hotkeys";
  return folded;
}

static inline const char* config_key_canonical(const char* section, const char* folded) {
  if (!strcmp(section, "controller_buttons")) {
    const int k = config_name_index(folded, config_cycle_short, PAD_CYCLE_COUNT);
    if (k >= 0) return config_cycle_names[k];
  }
  if (!strcmp(section, "cheats") && !strcmp(folded, "invincible_neighbours")) return config_cheat_names[1];
  return folded;
}

// Every value in `c`, as `section`, `key` and the text the file gives it, in
// the order the default file has them.
typedef void (*ConfigValueFn)(void* ctx, const char* section, const char* key, const char* value);

static inline void config_each_value(const Config* c, ConfigValueFn fn, void* ctx) {
  static const int buttons[12] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_B, BTN_A,
                                  BTN_Y, BTN_X, BTN_L, BTN_R, BTN_START, BTN_SELECT};
  static const int pad_hot[ACT_COUNT] = {
    ACT_QUICK_SAVE, ACT_QUICK_LOAD, ACT_TOGGLE_SMOOTHING, ACT_CYCLE_WIDESCREEN,
    ACT_CYCLE_FILTER, ACT_FULLSCREEN, ACT_TOGGLE_NATIVE, ACT_QUIT,
  };
  static const char* const stick[] = {"off", "left", "right"};
  char v[CONFIG_PATH_MAX];
  #define CONFIG_ON(b) ((b) ? "on" : "off")
  fn(ctx, "game", "rom", c->rom);
  fn(ctx, "game", "skip_intro", CONFIG_ON(c->skip_intro));
  if (c->level < 0) snprintf(v, sizeof v, "off");
  else snprintf(v, sizeof v, "%d", c->level);
  fn(ctx, "game", "level", v);
  snprintf(v, sizeof v, "%d", c->hitbox);
  fn(ctx, "game", "hitbox", v);
  fn(ctx, "game", "blood", c->red_blood ? "red" : "purple");
  fn(ctx, "game", "high_scores", CONFIG_ON(c->high_scores));
  fn(ctx, "game", "high_scores_file", c->high_scores_file);
  fn(ctx, "video", "fullscreen", CONFIG_ON(c->fullscreen));
  fn(ctx, "video", "widescreen", wide_name(c->widescreen));
  fn(ctx, "video", "filter", scale_name(c->filter));
  snprintf(v, sizeof v, "%d", c->window_scale);
  fn(ctx, "video", "window_scale", v);
  fn(ctx, "video", "smoothing", CONFIG_ON(c->smoothing));
  if (c->refresh <= 0) snprintf(v, sizeof v, "auto");
  else snprintf(v, sizeof v, "%d", c->refresh);
  fn(ctx, "video", "refresh", v);
  fn(ctx, "video", "radar", c->radar_flash ? "flashing" : "steady");
  fn(ctx, "audio", "enabled", CONFIG_ON(c->audio));
  snprintf(v, sizeof v, "%d", c->volume);
  fn(ctx, "audio", "volume", v);
  fn(ctx, "audio", "effect_overlay", CONFIG_ON(c->effect_overlay));
  fn(ctx, "controller", "enabled", CONFIG_ON(c->pads));
  fn(ctx, "controller", "twin_stick", CONFIG_ON(c->twin_stick));
  snprintf(v, sizeof v, "%d", c->deadzone);
  fn(ctx, "controller", "deadzone", v);
  fn(ctx, "controller", "move_stick", stick[c->pad.move_stick]);
  fn(ctx, "controller", "aim_stick", stick[c->pad.aim_stick]);
  #undef CONFIG_ON
  for (int i = 0; i < 12; i++) {
    config_pad_list_text(c->pad.game[buttons[i]], v, sizeof v);
    fn(ctx, "controller_buttons", config_button_names[buttons[i]], v);
  }
  for (int k = 0; k < PAD_CYCLE_COUNT; k++) {
    config_pad_list_text(c->pad.cycle[k], v, sizeof v);
    fn(ctx, "controller_buttons", config_cycle_names[k], v);
  }
  for (int i = 0; i < ACT_COUNT; i++) {
    config_pad_list_text(c->pad.hot[pad_hot[i]], v, sizeof v);
    fn(ctx, "controller_hotkeys", config_action_names[pad_hot[i]], v);
  }
  for (int p = 0; p < MOVIE_PORTS; p++)
    for (int i = 0; i < 12; i++) {
      config_key_list_text(c->key[p][buttons[i]], v, sizeof v);
      fn(ctx, p ? "keyboard_player_2" : "keyboard", config_button_names[buttons[i]], v);
    }
  for (int a = 0; a < ACT_COUNT; a++) {
    config_key_list_text(c->hotkey[a], v, sizeof v);
    fn(ctx, "hotkeys", config_action_names[a], v);
  }
  for (int k = 0; k < CONFIG_CHEATS; k++) fn(ctx, "cheats", config_cheat_names[k], c->cheat[k] ? "on" : "off");
}

// A string that grows. `failed` once an allocation has not been had, after
// which nothing more is added.
typedef struct {
  char* s;
  size_t n, cap;
  bool failed;
} ConfigText;

static inline void config_text_put(ConfigText* t, const char* s, size_t n) {
  if (t->failed) return;
  if (t->n + n + 1 > t->cap) {
    size_t cap = t->cap ? t->cap * 2 : 1024;
    while (cap < t->n + n + 1) cap *= 2;
    char* grown = (char*)realloc(t->s, cap);
    if (!grown) { t->failed = true; return; }
    t->s = grown;
    t->cap = cap;
  }
  memcpy(t->s + t->n, s, n);
  t->n += n;
  t->s[t->n] = 0;
}

static inline void config_text_puts(ConfigText* t, const char* s) { config_text_put(t, s, strlen(s)); }

// `text` with `section`'s `key` set to `value`, as a new string for the
// caller to free, or NULL when there was no memory for it. A line whose value
// is already `value` is left exactly as it was written.
static inline char* config_text_set(const char* text, const char* section, const char* key,
                                    const char* value) {
  const char* nl = strstr(text, "\r\n") ? "\r\n" : "\n";
  ConfigText out = {0};
  config_text_puts(&out, "");
  char current[64] = "";
  bool found = false;
  size_t insert_at = (size_t)-1;  // just after the section's last setting, or its heading
  for (const char* p = text; *p;) {
    const char* eol = strchr(p, '\n');
    const char* next = eol ? eol + 1 : p + strlen(p);
    const char* end = eol ? eol : next;
    if (end > p && end[-1] == '\r') end--;
    char buf[CONFIG_PATH_MAX + 128];
    const char* from = p == text && !strncmp(p, "\xef\xbb\xbf", 3) ? p + 3 : p;
    const size_t len = (size_t)(end - from) < sizeof buf - 1 ? (size_t)(end - from) : sizeof buf - 1;
    memcpy(buf, from, len);
    buf[len] = 0;
    char* s = config_trim(buf);
    bool mine = false, setting = false;
    if (*s == '[') {
      char* close = strchr(s, ']');
      current[0] = 0;
      if (close) {
        *close = 0;
        char* sec = config_trim(s + 1);
        config_fold(sec);
        snprintf(current, sizeof current, "%s", config_section_canonical(sec));
      }
    } else if (*s && *s != ';' && *s != '#' && strchr(s, '=')) {
      setting = true;
      char* eq = strchr(s, '=');
      *eq = 0;
      char* k = config_trim(s);
      char written[128];
      snprintf(written, sizeof written, "%s", k);
      config_fold(k);
      if (!strcmp(current, section) && !strcmp(config_key_canonical(current, k), key)) {
        mine = true;
        found = true;
        if (strcmp(config_trim(eq + 1), value)) {
          // The indent and the key as they were written; the value as it is now.
          const char* indent_end = from;
          while (indent_end < end && (*indent_end == ' ' || *indent_end == '\t')) indent_end++;
          config_text_put(&out, p, (size_t)(indent_end - p));
          config_text_puts(&out, written);
          config_text_puts(&out, *value ? " = " : " =");
          config_text_puts(&out, value);
          config_text_put(&out, end, (size_t)(next - end));
        } else {
          config_text_put(&out, p, (size_t)(next - p));
        }
      }
    }
    if (!mine) config_text_put(&out, p, (size_t)(next - p));
    if (!strcmp(current, section) && (setting || *s == '[')) insert_at = out.n;
    p = next;
  }
  if (!found) {
    ConfigText line = {0};
    if (insert_at == (size_t)-1) {
      // No such section: one on the end, after a blank line.
      const char* head = section;
      for (int i = 0; i < (int)(sizeof config_section_heads / sizeof *config_section_heads); i++)
        if (!strcmp(config_section_heads[i][0], section)) head = config_section_heads[i][1];
      if (out.n && out.s[out.n - 1] != '\n') config_text_puts(&line, nl);
      if (out.n) config_text_puts(&line, nl);
      config_text_puts(&line, "[");
      config_text_puts(&line, head);
      config_text_puts(&line, "]");
      config_text_puts(&line, nl);
      insert_at = out.n;
    } else if (insert_at && out.s[insert_at - 1] != '\n') {
      // The section's last setting is the file's last line, with no ending.
      config_text_puts(&line, nl);
    }
    config_text_puts(&line, key);
    config_text_puts(&line, *value ? " = " : " =");
    config_text_puts(&line, value);
    config_text_puts(&line, nl);
    ConfigText joined = {0};
    config_text_put(&joined, out.s, insert_at);
    config_text_put(&joined, line.s ? line.s : "", line.n);
    config_text_put(&joined, out.s + insert_at, out.n - insert_at);
    joined.failed = joined.failed || line.failed;
    free(line.s);
    free(out.s);
    out = joined;
  }
  if (out.failed) { free(out.s); return NULL; }
  return out.s;
}

static inline void config_update_one(void* ctx, const char* section, const char* key, const char* value) {
  char** text = (char**)ctx;
  if (!*text) return;
  char* next = config_text_set(*text, section, key, value);
  free(*text);
  *text = next;
}

// `text` with every value in `c` set in it; a new string for the caller to
// free, or NULL when there was no memory for it.
static inline char* config_update_text(const char* text, const Config* c) {
  const size_t len = strlen(text);
  char* copy = (char*)malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, text, len + 1);
  config_each_value(c, config_update_one, &copy);
  return copy;
}

// --- saying ------------------------------------------------------------------------

// What is bound, for the banner the frontend prints as it starts: the truth
// about this session, which a fixed list of keys stopped being the day they
// could be changed. Unbound things are left out.
static inline void config_print_keys(const char* label, const char* const* names, int count,
                                     const int* order, const SDL_Keycode (*lists)[CONFIG_KEYS_MAX]) {
  bool any = false;
  for (int n = 0; n < count; n++) {
    const int r = order ? order[n] : n;
    if (lists[r][0] == SDLK_UNKNOWN) continue;
    printf("%s%s=", any ? "  " : label, names[r]);
    for (int i = 0; i < CONFIG_KEYS_MAX && lists[r][i] != SDLK_UNKNOWN; i++)
      printf("%s%s", i ? "/" : "", SDL_GetKeyName(lists[r][i]));
    any = true;
  }
  if (any) printf("\n");
}

static inline void config_print_pad(const char* label, const char* const* names, int count,
                                    const int* order, const int16_t (*lists)[PAD_BIND_MAX]) {
  bool any = false;
  for (int n = 0; n < count; n++) {
    const int r = order ? order[n] : n;
    if (lists[r][0] == PAD_IN_NONE) continue;
    printf("%s%s=", any ? "  " : label, names[r]);
    for (int i = 0; i < PAD_BIND_MAX && lists[r][i] != PAD_IN_NONE; i++)
      printf("%s%s", i ? "/" : "", config_pad_input_name(lists[r][i]));
    any = true;
  }
  if (any) printf("\n");
}

static inline void config_print(const Config* c) {
  // The D-pad, then the face, then the rest: the order a person reads a pad in.
  static const int order[12] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_B, BTN_A,
                                BTN_Y, BTN_X, BTN_L, BTN_R, BTN_START, BTN_SELECT};
  static const char* const stick[] = {"off", "left", "right"};
  printf("Controls (Y fires; zamn.ini rebinds any of these):\n");
  config_print_keys("  Keys:      ", config_button_names, 12, order, c->key[0]);
  config_print_keys("  Keys, P2:  ", config_button_names, 12, order, c->key[1]);
  config_print_keys("  Hotkeys:   ", config_action_names, ACT_COUNT, NULL, c->hotkey);
  printf("             Alt+Return=fullscreen\n");
  config_print_pad("  Pad:       ", config_button_names, 12, order, c->pad.game);
  config_print_pad("             ", config_cycle_names, PAD_CYCLE_COUNT, NULL, c->pad.cycle);
  config_print_pad("  Pad hotkeys: ", config_action_names, ACT_COUNT, NULL, c->pad.hot);
  printf("             move stick=%s  aim stick=%s  Start+Select held for a second quits\n",
         stick[c->pad.move_stick], stick[c->pad.aim_stick]);
}

// --- looking up --------------------------------------------------------------------

// The action a key is bound to, or -1.
static inline int config_hotkey_of(const Config* c, SDL_Keycode k) {
  if (k == SDLK_UNKNOWN) return -1;
  for (int a = 0; a < ACT_COUNT; a++)
    for (int i = 0; i < CONFIG_KEYS_MAX && c->hotkey[a][i] != SDLK_UNKNOWN; i++)
      if (c->hotkey[a][i] == k) return a;
  return -1;
}

// What the keyboard holds. A button is held while *any* of its keys is, so one
// bit per key rather than one per button: with Select on two keys, letting go
// of one must not let go of the other.
typedef struct {
  uint8_t down[MOVIE_PORTS][12];  // bit i: the i-th key of the list is down
} ConfigKeys;

// A key went down or came up. True if it is bound to a button.
static inline bool config_key_event(const Config* c, ConfigKeys* keys, SDL_Keycode k, bool down) {
  if (k == SDLK_UNKNOWN) return false;
  for (int p = 0; p < MOVIE_PORTS; p++)
    for (int b = 0; b < 12; b++)
      for (int i = 0; i < CONFIG_KEYS_MAX && c->key[p][b][i] != SDLK_UNKNOWN; i++)
        if (c->key[p][b][i] == k) {
          if (down) keys->down[p][b] |= (uint8_t)(1u << i);
          else keys->down[p][b] &= (uint8_t)~(1u << i);
          return true;
        }
  return false;
}

static inline void config_keys_held(const ConfigKeys* keys, uint16_t held[MOVIE_PORTS]) {
  for (int p = 0; p < MOVIE_PORTS; p++) {
    held[p] = 0;
    for (int b = 0; b < 12; b++)
      if (keys->down[p][b]) held[p] |= (uint16_t)(1u << b);
  }
}

#endif
