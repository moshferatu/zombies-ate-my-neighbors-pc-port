// zamn_launcher: `zamn.ini` with a face on it, and a Play button.
//
// Everything here is a setting the file already has (`src/config.h`), and
// nothing else: the launcher is for the player who would rather not open a
// text file to change the widescreen or rebind a key. It reads the file the
// game would read, shows it a tab at a time, writes it back where it was, and
// starts the game with `--config` naming that file, so the two cannot find
// different ones.
//
// ## The file
//
// Found the way the game finds it -- `--config <file>`, then `zamn.ini` in the
// working directory, then beside the executable -- and when there is none,
// made beside the launcher from the default text. Written with
// `config_update_text`, which sets each value where it stands, so a player's
// comments and spellings survive a save; a line the parser refused keeps its
// default here, like it does in the game, and a save writes that default in.
// Nothing is written until Save or Play.
//
// Play saves and starts the game, and the launcher waits out of sight until
// the game closes, then comes back.
//
// ## The screen
//
// SDL, as the game is, drawn by hand on black with the system's font through
// stb_truetype, at the display's scale. It is worked by the mouse, the
// keyboard or a controller alike:
//
//   * Up and Down move between rows, Left and Right change a value, and past
//     the last row are the buttons. Tab, or L1 and R1, turn the tabs.
//   * A setting with a fixed list of values is a dropdown: Enter, South or a
//     click opens it, Up and Down choose, Enter, South or a click takes the
//     choice, and Escape, East or a click elsewhere closes it unchanged.
//   * A binding is set by pressing it. Enter (or South) waits for the next key
//     or pad input and adds it to the row; Backspace (or West) takes the last
//     one off, and a chip's x takes that one. Every key can be bound, Escape
//     included, so a wait for a key is ended by a click or by five seconds.
//   * A path is typed after Enter or a click, or chosen with Browse (Windows),
//     or a cartridge dropped on the window.
//   * Ctrl+S saves; Ctrl+Enter, F5 or Start plays; Escape quits at once,
//     unsaved changes or not.
//
// For checking it without a window: `--press Tab,Down,Right,Ctrl+S` hands it
// keys, by their names in the file, as if they had been pressed, and
// `--screenshot <file.png>` then draws it once into the file and leaves, the
// window never shown.
//
// A key bound in two places does the first thing only (`config_check`); the
// chip that loses is drawn red, and the row says what wins.
//
// ## The icon and the heading
//
// The icon is the title screen, drawn from the player's cartridge by
// `tools/make_icon.c` when this is built, and the heading is the title's
// logo, cut from it by `tools/make_logo.c` the same way. Without a
// cartridge at build time the launcher builds without an icon, and its
// heading is text.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "config.h"
#ifdef ZAMN_LOGO
#include "zamn_logo.h"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
// Only here: elsewhere it brings in X11, whose `Font` is not the one below.
#include <SDL_syswm.h>
#define GAME_EXE "zamn.exe"
#define SEP '\\'
#else
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define GAME_EXE "zamn"
#define SEP '/'
#endif

// --- what there is to set ------------------------------------------------------

typedef enum { K_HEAD, K_CHOICE, K_RANGE, K_PATH, K_KEYS, K_PADS } Kind;

enum {
  S_ROM, S_SKIP_INTRO, S_LEVEL, S_HITBOX, S_BLOOD, S_HIGH_SCORES, S_HISCORE_FILE,
  S_FULLSCREEN, S_WIDESCREEN, S_FILTER, S_WINDOW_SCALE, S_SMOOTHING, S_RADAR,
  S_AUDIO, S_VOLUME, S_EFFECT_OVERLAY, S_ALL_MONSTER_SOUNDS,
  S_PADS, S_TWIN_STICK, S_DEADZONE, S_MOVE_STICK, S_AIM_STICK,
  S_CHEAT,  // and the five after it, by `config_cheat_names`
};

// A binding row's list, as a group and an index into it.
enum { G_PAD_GAME = 1, G_PAD_CYCLE, G_PAD_HOT, G_KEY_1, G_KEY_2, G_KEY_HOT };
#define BIND(g, i) ((g) << 8 | (i))

typedef struct {
  int value;
  const char* label;
} Choice;

typedef struct {
  Kind kind;
  int id;  // S_* for a setting, BIND() for a binding
  const char* label;
  const char* help;
  const Choice* choices;
  int count;
  int lo, hi, step;  // K_RANGE
  bool slider;
} Row;

static const Choice on_off[] = {{0, "Off"}, {1, "On"}};
static const Choice blood_choices[] = {{0, "Purple"}, {1, "Red"}};
static const Choice radar_choices[] = {{0, "Steady"}, {1, "Flashing"}};
static const Choice wide_choices[] = {
  {WIDE_OFF, "Off"}, {WIDE_16_9, "16:9"}, {WIDE_16_10, "16:10"}, {WIDE_21_9, "21:9"}, {WIDE_AUTO, "Auto"}};
static const Choice filter_choices[] = {
  {SCALE_SHARP, "Sharp"}, {SCALE_INTEGER, "Integer"}, {SCALE_LINEAR, "Linear"}};
static const Choice stick_choices[] = {
  {PAD_STICK_LEFT, "Left"}, {PAD_STICK_RIGHT, "Right"}, {PAD_STICK_NONE, "Off"}};
// What each record's card calls it, read off the cards. By `--level` number:
// 1 to 48 the levels, 49 the credits, 0 and 50 to 55 the bonus rooms.
static const char* const level_names[56] = {
  "Day of the Tentacle", "Zombie Panic", "Evening of the Undead", "Terror in Aisle Five",
  "Chainsaw Hedgemaze Mayhem", "Weird Kids on the Block", "Pyramid of Fear",
  "Dr. Tongue's Castle of Terror", "Titanic Toddler", "Toxic Terrors", "No Assembly Required",
  "Weeds Gone Bad", "Mars Needs Cheerleaders", "Chopping Mall", "Seven Meals for Seven Zombies",
  "Dinner on Monster Island", "Ants", "Office of the Doomed", "Squidmen of the Deep",
  "Nightmare on Terror Street", "Invasion of the Snakeoids", "The Day the Earth Ran Away",
  "Revenge of Dr. Tongue", "The Caves of Mystery", "Warehouse of the Evil Dolls",
  "Look Who's Shopping", "Where the Red Fern Growls", "Dances with Werewolves",
  "Mark of the Vampire", "Zombie House Party", "The Horror of Floor Thirteen",
  "Look Who's Coming to Dinner", "Giant Ant Farm", "Fish and Crypts", "I Was a Chainsaw Maniac",
  "Boardwalk of Terrors", "Monster Phobia", "Labyrinth of Horrors", "Monsters of the Blue Lagoon",
  "Destroy All Vampires", "Pyramid of Fear 2", "Martians Go Home!", "Spikes",
  "Super Fund Cleanup Site", "The Curse of Dr. Tongue", "Danger in Picnic Park",
  "Day of the Chainsaw", "Gridiron Terror", "Curse of the Tongue", "Monsters Among Us",
  "The Son of Dr. Tongue", "Day of the Tentacle", "Someplace Very Warm", "Curse of the Pharaohs",
  "Mushroom Men", "Cheerleaders vs. the Monsters",
};
#define COUNT(a) ((int)(sizeof(a) / sizeof *(a)))

enum { TAB_GAME, TAB_VIDEO, TAB_AUDIO, TAB_CONTROLLER, TAB_KEYBOARD, TAB_CHEATS, TAB_COUNT };
static const char* const tab_names[TAB_COUNT] = {"Game", "Video", "Audio", "Controller",
                                                 "Keyboard", "Cheats"};
#define ROWS_MAX 64
static Row rows[TAB_COUNT][ROWS_MAX];
static int row_count[TAB_COUNT];

static void add(int tab, Row r) {
  if (row_count[tab] < ROWS_MAX) rows[tab][row_count[tab]++] = r;
}
static void add_choice(int tab, int id, const char* label, const Choice* c, int n, const char* help) {
  add(tab, (Row){K_CHOICE, id, label, help, c, n, 0, 0, 0, false});
}
static void add_range(int tab, int id, const char* label, int lo, int hi, int step, bool slider,
                      const char* help) {
  add(tab, (Row){K_RANGE, id, label, help, NULL, 0, lo, hi, step, slider});
}
static void add_head(int tab, const char* label, const char* help) {
  add(tab, (Row){K_HEAD, 0, label, help, NULL, 0, 0, 0, 0, false});
}

// The SNES buttons in the order the file has them, with what they do here.
static const struct { int btn; const char* label; } snes_rows[12] = {
  {BTN_UP, "Up"}, {BTN_DOWN, "Down"}, {BTN_LEFT, "Left"}, {BTN_RIGHT, "Right"},
  {BTN_B, "B (Change Weapon)"}, {BTN_A, "A (Change Item)"}, {BTN_Y, "Y (Fire)"},
  {BTN_X, "X (Use Item)"}, {BTN_L, "L (Radar)"}, {BTN_R, "R (Radar)"},
  {BTN_START, "Start"}, {BTN_SELECT, "Select"},
};
static const struct { int act; const char* label; const char* help; } hot_rows[ACT_COUNT] = {
  {ACT_QUICK_SAVE, "Quick Save", "Saves the game to zamn.quicksave."},
  {ACT_QUICK_LOAD, "Quick Load", "Loads the most recent quick save."},
  {ACT_FULLSCREEN, "Fullscreen", "Toggle between fullscreen and windowed mode."},
  {ACT_TOGGLE_SMOOTHING, "Toggle Smoothing", "Toggle smoothing on or off for comparison."},
  {ACT_QUIT, "Quit", "Leave the game."},
};
static const struct { int k; const char* label; } cycle_rows[PAD_CYCLE_COUNT] = {
  {PAD_CYCLE_NEXT_WEAPON, "Next Weapon"}, {PAD_CYCLE_PREV_WEAPON, "Previous Weapon"},
  {PAD_CYCLE_NEXT_ITEM, "Next Item"}, {PAD_CYCLE_PREV_ITEM, "Previous Item"},
};

// In the order they are shown, which is not `config_cheat_names`'s: each names
// its key there, and the row sets that one.
static const struct { const char* key; const char* label; const char* help; } cheat_rows[CONFIG_CHEATS] = {
  {"invincible", "Invincibility", "Nothing hurts a player."},
  {"invincible_neighbors", "Invincible Neighbors", "Nothing hurts a neighbor."},
  {"infinite_lives", "Infinite Lives", "Dying does not cost a life."},
  {"infinite_ammo", "Infinite Ammo / Uses", "Weapons and items never consume ammo or uses."},
  {"give_all", "Give All Weapons / Items", "Grants every weapon and every item (999 / 99 uses respectively)."},
  {"always_run", "Always Run", "Grants the running shoes effect always."},
};

static void build_rows(void) {
  add(TAB_GAME, (Row){K_PATH, S_ROM, "ROM",
      "The original game ROM file."});
  add_choice(TAB_GAME, S_SKIP_INTRO, "Skip Intro", on_off, 2,
      "Launch the game directly into the title menu.");
  add_range(TAB_GAME, S_LEVEL, "Starting Level", -1, 55, 1, false,
      "Whether to start a new game on a specific level.");
  add_range(TAB_GAME, S_HITBOX, "Hitbox Size", 100, 200, 5, true,
      "How far a player reaches for a pickup or a neighbor as a percentage of the original game.");
  add_choice(TAB_GAME, S_BLOOD, "Game Over Blood", blood_choices, 2,
      "The color of the blood on the game over screen.");
  add_choice(TAB_GAME, S_HIGH_SCORES, "Save High Scores", on_off, 2,
      "Whether to save the top scores from one run to the next.");
  add(TAB_GAME, (Row){K_PATH, S_HISCORE_FILE, "High Scores File",
      "Where the top scores are kept."});

  add_choice(TAB_VIDEO, S_FULLSCREEN, "Fullscreen", on_off, 2,
      "Start the game in fullscreen or in a window.");
  add_choice(TAB_VIDEO, S_WIDESCREEN, "Widescreen", wide_choices, 5,
      "Render the game in widescreen.");
  add_choice(TAB_VIDEO, S_FILTER, "Filter", filter_choices, 3,
      "How to upscale the game's visuals.");
  add_range(TAB_VIDEO, S_WINDOW_SCALE, "Window Size", 1, SCALE_MAX_STAGE, 1, false,
      "The size of the window when not fullscreen.");
  add_choice(TAB_VIDEO, S_SMOOTHING, "Smoothing", on_off, 2,
      "Whether to enable smoothing for the game's visuals.");
  add_choice(TAB_VIDEO, S_RADAR, "Radar", radar_choices, 2,
      "Whether survivors flash on the radar. If using smoothing, Steady will look better.");

  add_choice(TAB_AUDIO, S_AUDIO, "Sound", on_off, 2, "Whether to enable sound.");
  add_range(TAB_AUDIO, S_VOLUME, "Volume", 0, 100, 5, true, "The volume level.");
  add_choice(TAB_AUDIO, S_EFFECT_OVERLAY, "All Sound Effects", on_off, 2,
      "Plays sound effects the original game drops sometimes during gameplay.");
  add_choice(TAB_AUDIO, S_ALL_MONSTER_SOUNDS, "All Monster Sounds", on_off, 2,
      "Plays monster sounds the original game skips on some levels.");

  add_choice(TAB_CONTROLLER, S_PADS, "Controllers", on_off, 2, "Whether controllers are enabled.");
  add_choice(TAB_CONTROLLER, S_TWIN_STICK, "Twin Stick", on_off, 2,
      "Whether twin stick controls are enabled.");
  add_range(TAB_CONTROLLER, S_DEADZONE, "Deadzone", 5, 90, 1, true,
      "How far a stick moves before it registers as a percentage of its travel.");
  add_choice(TAB_CONTROLLER, S_MOVE_STICK, "Move Stick", stick_choices, 3, "The stick that steers.");
  add_choice(TAB_CONTROLLER, S_AIM_STICK, "Aim Stick", stick_choices, 3,
      "The stick that aims when twin stick is on.");
  static const char pad_help[] =
      "Binds the selected action to a controller button.";
  add_head(TAB_CONTROLLER, "Buttons", pad_help);
  for (int i = 0; i < 12; i++)
    add(TAB_CONTROLLER, (Row){K_PADS, BIND(G_PAD_GAME, snes_rows[i].btn), snes_rows[i].label, pad_help});
  static const char cycle_help[] =
      "Binds a cycling action to a controller button.";
  add_head(TAB_CONTROLLER, "Weapons and items", cycle_help);
  for (int i = 0; i < PAD_CYCLE_COUNT; i++)
    add(TAB_CONTROLLER, (Row){K_PADS, BIND(G_PAD_CYCLE, cycle_rows[i].k), cycle_rows[i].label, cycle_help});
  add_head(TAB_CONTROLLER, "Hotkeys",
      "What the game's window does, from a controller. Hold two buttons together to bind both "
      "as one, as Start and Select are for quitting; while they are held, neither reaches the game.");
  for (int i = 0; i < ACT_COUNT; i++)
    add(TAB_CONTROLLER, (Row){K_PADS, BIND(G_PAD_HOT, hot_rows[i].act), hot_rows[i].label, hot_rows[i].help});

  static const char key_help[] =
      "Binds the selected action to a key.";
  add_head(TAB_KEYBOARD, "Player 1", key_help);
  for (int i = 0; i < 12; i++)
    add(TAB_KEYBOARD, (Row){K_KEYS, BIND(G_KEY_1, snes_rows[i].btn), snes_rows[i].label, key_help});
  static const char key2_help[] = "Binds the selected action for player 2 on the same keyboard.";
  add_head(TAB_KEYBOARD, "Player 2", key2_help);
  for (int i = 0; i < 12; i++)
    add(TAB_KEYBOARD, (Row){K_KEYS, BIND(G_KEY_2, snes_rows[i].btn), snes_rows[i].label, key2_help});

  add_head(TAB_KEYBOARD, "Hotkeys", "What the game's window does, from the keyboard.");
  for (int i = 0; i < ACT_COUNT; i++)
    add(TAB_KEYBOARD, (Row){K_KEYS, BIND(G_KEY_HOT, hot_rows[i].act), hot_rows[i].label, hot_rows[i].help});

  add_head(TAB_CHEATS, "Cheats",
      "Each off unless turned on here. While any is on, the top scores are read and not written.");
  for (int i = 0; i < CONFIG_CHEATS; i++)
    for (int k = 0; k < CONFIG_CHEATS; k++)
      if (!strcmp(cheat_rows[i].key, config_cheat_names[k]))
        add_choice(TAB_CHEATS, S_CHEAT + k, cheat_rows[i].label, on_off, 2, cheat_rows[i].help);
}

static int setting_get(const Config* c, int id) {
  switch (id) {
    case S_SKIP_INTRO:   return c->skip_intro;
    case S_LEVEL:        return c->level;
    case S_HITBOX:       return c->hitbox;
    case S_BLOOD:        return c->red_blood;
    case S_HIGH_SCORES:  return c->high_scores;
    case S_FULLSCREEN:   return c->fullscreen;
    case S_WIDESCREEN:   return (int)c->widescreen;
    case S_FILTER:       return (int)c->filter;
    case S_WINDOW_SCALE: return c->window_scale;
    case S_SMOOTHING:    return c->smoothing;
    case S_RADAR:        return c->radar_flash;
    case S_AUDIO:        return c->audio;
    case S_VOLUME:       return c->volume;
    case S_EFFECT_OVERLAY: return c->effect_overlay;
    case S_ALL_MONSTER_SOUNDS: return c->all_monster_sounds;
    case S_PADS:         return c->pads;
    case S_TWIN_STICK:   return c->twin_stick;
    case S_DEADZONE:     return c->deadzone;
    case S_MOVE_STICK:   return c->pad.move_stick;
    case S_AIM_STICK:    return c->pad.aim_stick;
    default:
      return id >= S_CHEAT && id < S_CHEAT + CONFIG_CHEATS ? c->cheat[id - S_CHEAT] : 0;
  }
}

static void setting_set(Config* c, int id, int v) {
  switch (id) {
    case S_SKIP_INTRO:   c->skip_intro = v != 0; break;
    case S_LEVEL:        c->level = v; break;
    case S_HITBOX:       c->hitbox = v; break;
    case S_BLOOD:        c->red_blood = v != 0; break;
    case S_HIGH_SCORES:  c->high_scores = v != 0; break;
    case S_FULLSCREEN:   c->fullscreen = v != 0; break;
    case S_WIDESCREEN:   c->widescreen = (WideMode)v; break;
    case S_FILTER:       c->filter = (ScaleMode)v; break;
    case S_WINDOW_SCALE: c->window_scale = v; break;
    case S_SMOOTHING:    c->smoothing = v != 0; break;
    case S_RADAR:        c->radar_flash = v != 0; break;
    case S_AUDIO:        c->audio = v != 0; break;
    case S_VOLUME:       c->volume = v; break;
    case S_EFFECT_OVERLAY: c->effect_overlay = v != 0; break;
    case S_ALL_MONSTER_SOUNDS: c->all_monster_sounds = v != 0; break;
    case S_PADS:         c->pads = v != 0; break;
    case S_TWIN_STICK:   c->twin_stick = v != 0; break;
    case S_DEADZONE:     c->deadzone = v; config_deadzone(&c->pad, v); break;
    case S_MOVE_STICK:   c->pad.move_stick = v; break;
    case S_AIM_STICK:    c->pad.aim_stick = v; break;
    default:
      if (id >= S_CHEAT && id < S_CHEAT + CONFIG_CHEATS) c->cheat[id - S_CHEAT] = v != 0;
      break;
  }
}

static char* path_of(Config* c, int id) { return id == S_ROM ? c->rom : c->high_scores_file; }

static SDL_Keycode* keys_of(Config* c, int id) {
  const int i = id & 0xff;
  switch (id >> 8) {
    case G_KEY_1:   return c->key[0][i];
    case G_KEY_2:   return c->key[1][i];
    case G_KEY_HOT: return c->hotkey[i];
    default:        return NULL;
  }
}

static int16_t* pads_of(Config* c, int id) {
  const int i = id & 0xff;
  switch (id >> 8) {
    case G_PAD_GAME:  return c->pad.game[i];
    case G_PAD_CYCLE: return c->pad.cycle[i];
    case G_PAD_HOT:   return c->pad.hot[i];
    default:          return NULL;
  }
}

static int key_count(const SDL_Keycode* l) {
  int n = 0;
  while (n < CONFIG_KEYS_MAX && l[n] != SDLK_UNKNOWN) n++;
  return n;
}

static int bind_count(const int16_t* l) {
  int n = 0;
  while (n < PAD_BIND_MAX && l[n] != PAD_IN_NONE) n++;
  return n;
}

static void range_text(const Row* r, int v, char* out, size_t size) {
  switch (r->id) {
    case S_LEVEL:
      if (v < 0 || v > 55) snprintf(out, size, "Off");
      else if (v == 0 || v >= 50) snprintf(out, size, "Bonus room %d (%s)", v, level_names[v]);
      else if (v == 49) snprintf(out, size, "Credits (%s)", level_names[v]);
      else snprintf(out, size, "Level %d (%s)", v, level_names[v]);
      break;
    case S_WINDOW_SCALE: snprintf(out, size, "%dx (%d x %d)", v, 512 * v, 480 * v); break;
    default: snprintf(out, size, "%d%%", v); break;
  }
}

// A slider's next value along. A value the file had that is not on the steps
// (a hitbox of 123) is shown as it is and left for the step either side of it.
static int range_step(const Row* r, int v, int dir) {
  int n;
  if (dir > 0) n = r->lo + ((v - r->lo) / r->step + 1) * r->step;
  else n = r->lo + ((v - r->lo + r->step - 1) / r->step - 1) * r->step;
  return n < r->lo ? r->lo : n > r->hi ? r->hi : n;
}

// A row's value from one config into another: what Defaults does.
static void row_copy(Config* to, Config* from, const Row* r) {
  switch (r->kind) {
    case K_CHOICE:
    case K_RANGE: setting_set(to, r->id, setting_get(from, r->id)); break;
    case K_PATH: snprintf(path_of(to, r->id), CONFIG_PATH_MAX, "%s", path_of(from, r->id)); break;
    case K_KEYS: memcpy(keys_of(to, r->id), keys_of(from, r->id), sizeof(SDL_Keycode) * CONFIG_KEYS_MAX); break;
    case K_PADS: memcpy(pads_of(to, r->id), pads_of(from, r->id), sizeof(int16_t) * PAD_BIND_MAX); break;
    default: break;
  }
}

// --- fonts ----------------------------------------------------------------------

#define GLYPH_FIRST 32
#define GLYPH_COUNT 224  // to 255: Latin-1, which is what a path's bytes are drawn as

typedef struct {
  SDL_Texture* tex;
  stbtt_packedchar ch[GLYPH_COUNT];
  int w, h;
  float ascent, height;
} Font;

static bool font_make(Font* f, SDL_Renderer* ren, const unsigned char* ttf, float px) {
  memset(f, 0, sizeof *f);
  f->w = f->h = px > 60 ? 2048 : 1024;
  unsigned char* alpha = (unsigned char*)calloc((size_t)f->w, (size_t)f->h);
  Uint32* argb = (Uint32*)malloc((size_t)f->w * f->h * 4);
  stbtt_pack_context pc;
  bool ok = alpha && argb && stbtt_PackBegin(&pc, alpha, f->w, f->h, 0, 1, NULL);
  if (ok) {
    stbtt_PackSetOversampling(&pc, 1, 1);
    ok = stbtt_PackFontRange(&pc, ttf, 0, px, GLYPH_FIRST, GLYPH_COUNT, f->ch) != 0;
    stbtt_PackEnd(&pc);
  }
  if (ok) {
    for (int i = 0; i < f->w * f->h; i++) argb[i] = (Uint32)alpha[i] << 24 | 0xffffffu;
    f->tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, f->w, f->h);
    ok = f->tex && SDL_UpdateTexture(f->tex, NULL, argb, f->w * 4) == 0;
    if (f->tex) SDL_SetTextureBlendMode(f->tex, SDL_BLENDMODE_BLEND);
    stbtt_fontinfo info;
    if (ok && stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
      int asc, desc, gap;
      stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
      const float s = stbtt_ScaleForPixelHeight(&info, px);
      f->ascent = asc * s;
      f->height = (asc - desc + gap) * s;
    }
  }
  free(alpha);
  free(argb);
  return ok;
}

static void font_free(Font* f) {
  if (f->tex) SDL_DestroyTexture(f->tex);
  f->tex = NULL;
}

static float text_width(const Font* f, const char* s) {
  float w = 0;
  for (; *s; s++) {
    const unsigned c = (unsigned char)*s;
    if (c >= GLYPH_FIRST) w += f->ch[c - GLYPH_FIRST].xadvance;
  }
  return w;
}

// Left edge `x`, top of the line `y`. Returns where the text ended.
static float text(SDL_Renderer* ren, const Font* f, float x, float y, const char* s, SDL_Color col) {
  SDL_SetTextureColorMod(f->tex, col.r, col.g, col.b);
  SDL_SetTextureAlphaMod(f->tex, col.a);
  float px = x, py = y + f->ascent;
  for (; *s; s++) {
    const unsigned c = (unsigned char)*s;
    if (c < GLYPH_FIRST) continue;
    stbtt_aligned_quad q;
    stbtt_GetPackedQuad(f->ch, f->w, f->h, (int)(c - GLYPH_FIRST), &px, &py, &q, 1);
    const SDL_Rect src = {(int)(q.s0 * f->w + 0.5f), (int)(q.t0 * f->h + 0.5f),
                          (int)((q.s1 - q.s0) * f->w + 0.5f), (int)((q.t1 - q.t0) * f->h + 0.5f)};
    const SDL_Rect dst = {(int)q.x0, (int)q.y0, src.w, src.h};
    if (src.w > 0 && src.h > 0) SDL_RenderCopy(ren, f->tex, &src, &dst);
  }
  return px;
}

static unsigned char* read_all(const char* path, long* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char* buf = len >= 0 && len < (64 << 20) ? (unsigned char*)malloc((size_t)len + 1) : NULL;
  if (buf) {
    const size_t got = fread(buf, 1, (size_t)len, f);
    buf[got] = 0;
    if (out_len) *out_len = (long)got;
  }
  fclose(f);
  return buf;
}

// The system's own sans, regular and bold.
static unsigned char* load_font(bool bold) {
#ifdef _WIN32
  char dir[MAX_PATH];
  if (!GetWindowsDirectoryA(dir, sizeof dir)) snprintf(dir, sizeof dir, "C:\\Windows");
  const char* names[] = {bold ? "segoeuib.ttf" : "segoeui.ttf", bold ? "arialbd.ttf" : "arial.ttf"};
  for (int i = 0; i < 2; i++) {
    char path[MAX_PATH + 64];
    snprintf(path, sizeof path, "%s\\Fonts\\%s", dir, names[i]);
    unsigned char* ttf = read_all(path, NULL);
    if (ttf) return ttf;
  }
#else
  static const char* const regular[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/Library/Fonts/Arial.ttf"};
  static const char* const heavy[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/noto/NotoSans-Bold.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/Library/Fonts/Arial Bold.ttf"};
  for (int i = 0; i < COUNT(regular); i++) {
    unsigned char* ttf = read_all(bold ? heavy[i] : regular[i], NULL);
    if (ttf) return ttf;
  }
#endif
  return bold ? load_font(false) : NULL;
}

// --- the launcher's state ---------------------------------------------------------

typedef enum { P_ROW, P_DROP, P_OPTION, P_SLIDER, P_FIELD, P_BROWSE, P_CHIP, P_ADD, P_TAB, P_BUTTON } Part;

typedef struct {
  SDL_Rect r;
  Part part;
  int row, index;
} Hot;

enum { UI_DEFAULTS, UI_SAVE, UI_QUIT, UI_PLAY, UI_BUTTONS };
static const char* const button_names[UI_BUTTONS] = {"Defaults", "Save", "Quit", "Play"};

enum { STYLE_POSITION, STYLE_PLAYSTATION, STYLE_XBOX };

static const SDL_Color C_TEXT = {232, 232, 232, 255};
static const SDL_Color C_DIM = {150, 150, 150, 255};
static const SDL_Color C_FAINT = {64, 64, 64, 255};
static const SDL_Color C_GREEN = {120, 220, 64, 255};  // the logo's
static const SDL_Color C_RED = {236, 52, 44, 255};     // the spiral's
static const SDL_Color C_BLACK = {0, 0, 0, 255};

static struct {
  SDL_Window* win;
  SDL_Renderer* ren;
  int w, h;
  float scale, mouse_scale;
  unsigned char* ttf[2];
  Font body, small, bold, title;
  SDL_Texture* logo;  // the heading, when there is one, `logo_n` times the game's size
  int logo_n;
  Config cfg, def;
  char ini[CONFIG_PATH_MAX];      // absolute
  char ini_dir[CONFIG_PATH_MAX];  // with its separator
  bool ini_exists;
  char base[CONFIG_PATH_MAX];     // the launcher's own directory, with its separator
  int tab, focus, button;
  bool on_buttons;
  float scroll;
  int hover;
  bool editing;
  int edit_row, caret;
  char edit[CONFIG_PATH_MAX], edit_was[CONFIG_PATH_MAX];
  bool capturing;
  int capture_row;
  int capture_in[2], capture_n;  // a hotkey's inputs held so far
  Uint32 capture_end;
  int capture_shown;  // the countdown's seconds as last drawn
  int dragging;
  // The row whose Browse was pressed (-1: none). Its dialog opens when the
  // button comes up: opened on the press, the dialog takes the release, SDL
  // still thinks the button is down, and the next click never arrives.
  int browse_row;
  // Whether a missing cartridge is shown in red yet: not when the launcher
  // opens, only once Play has wanted it or the path has been changed.
  bool rom_checked;
  // The open dropdown's row (-1: none), the option lit in it, the first one
  // in sight, and where its box was drawn.
  int drop_row, drop_sel, drop_first, drop_shown;
  float drop_x, drop_y, drop_w, drop_h;
  Hot hot[512];
  int nhot;
  char status[CONFIG_PATH_MAX + 128];
  bool dirty;
  int pad_style;
  int held_dir, stick_x, stick_y;
  Uint32 held_next;
  // Whether the event just handled can have changed what is on screen. A
  // mouse moving within a row, a stick drifting inside its dead zone and the
  // events nothing here reads do not, and drawing and presenting on every
  // one of them kept the launcher redrawing at the display's rate while the
  // mouse moved.
  bool redraw;
} ui;

// What went wrong, beside the buttons. What went right is not said.
static void say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(ui.status, sizeof ui.status, fmt, ap);
  va_end(ap);
}

static void changed(void) { ui.dirty = true; }

static const Row* row_at(int i) {
  return i >= 0 && i < row_count[ui.tab] ? &rows[ui.tab][i] : NULL;
}

// --- layout -------------------------------------------------------------------------

#define L(v) ((v) * ui.scale)
#ifdef ZAMN_LOGO
#define HEADER_H 122  // the logo is LOGO_H of it
#else
#define HEADER_H 74
#endif
#define TABS_H 48
#define ROW_H 44
#define HEAD_H 58
#define HELP_H 84
#define BAR_H 72
#define MARGIN 28
#define LABEL_W 250

static float row_y[ROWS_MAX + 1];  // from the top of the list; the last is its height

static void layout(void) {
  float y = L(10);
  for (int i = 0; i < row_count[ui.tab]; i++) {
    row_y[i] = y;
    y += rows[ui.tab][i].kind == K_HEAD ? L(HEAD_H) : L(ROW_H);
  }
  row_y[row_count[ui.tab]] = y + L(10);
}

static float list_top(void) { return L(HEADER_H + TABS_H); }
static float list_h(void) { return ui.h - L(HELP_H + BAR_H) - list_top(); }

static void clamp_scroll(void) {
  const float most = row_y[row_count[ui.tab]] - list_h();
  if (ui.scroll > most) ui.scroll = most;
  if (ui.scroll < 0) ui.scroll = 0;
}

static void reveal(int i) {
  layout();
  const float h = rows[ui.tab][i].kind == K_HEAD ? L(HEAD_H) : L(ROW_H);
  float top = row_y[i];
  // A row just under its heading brings the heading with it.
  if (i > 0 && rows[ui.tab][i - 1].kind == K_HEAD) top = row_y[i - 1];
  if (top < ui.scroll) ui.scroll = top - L(4);
  if (row_y[i] + h > ui.scroll + list_h()) ui.scroll = row_y[i] + h - list_h() + L(4);
  clamp_scroll();
}

static int first_row(void) {
  for (int i = 0; i < row_count[ui.tab]; i++)
    if (rows[ui.tab][i].kind != K_HEAD) return i;
  return 0;
}

// --- files and the game -----------------------------------------------------------

static const char* base_name(const char* path) {
  const char* name = path;
  for (const char* p = path; *p; p++)
    if (*p == '/' || *p == '\\') name = p + 1;
  return name;
}

static bool file_exists(const char* path) {
  FILE* f = fopen(path, "rb");
  if (f) fclose(f);
  return f != NULL;
}

static void absolute(const char* path, char* out, size_t size) {
#ifdef _WIN32
  if (!_fullpath(out, path, size)) snprintf(out, size, "%s", path);
#else
  char cwd[PATH_MAX];
  if (path[0] == '/' || !getcwd(cwd, sizeof cwd)) snprintf(out, size, "%s", path);
  else snprintf(out, size, "%s/%s", cwd, path);
#endif
}

static void dir_of(const char* path, char* out, size_t size) {
  snprintf(out, size, "%s", path);
  char* cut = NULL;
  for (char* p = out; *p; p++)
    if (*p == '/' || *p == '\\') cut = p;
  if (cut) cut[1] = 0;
  else out[0] = 0;
}

static void find_base(void) {
#ifdef _WIN32
  char exe[MAX_PATH];
  const DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
  if (n > 0 && n < sizeof exe) { dir_of(exe, ui.base, sizeof ui.base); return; }
#endif
  char* b = SDL_GetBasePath();
  snprintf(ui.base, sizeof ui.base, "%s", b ? b : "");
  SDL_free(b);
}

// The path as it should go in the file: from the file's own directory when it
// is under it, so that the folder can be moved; as it is otherwise.
static void relative_to_ini(const char* path, char* out, size_t size) {
  const size_t n = strlen(ui.ini_dir);
#ifdef _WIN32
  const bool under = n && _strnicmp(path, ui.ini_dir, n) == 0;
#else
  const bool under = n && strncmp(path, ui.ini_dir, n) == 0;
#endif
  snprintf(out, size, "%s", under ? path + n : path);
}

// Text from SDL, which is UTF-8, as the bytes a path is to the C library: the
// same on most systems, the ANSI code page on Windows, which is what the
// game's `fopen` takes.
static void local_text(const char* utf8, char* out, size_t size) {
#ifdef _WIN32
  wchar_t wide[CONFIG_PATH_MAX];
  if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, CONFIG_PATH_MAX) &&
      WideCharToMultiByte(CP_ACP, 0, wide, -1, out, (int)size, NULL, NULL))
    return;
#endif
  snprintf(out, size, "%s", utf8);
}

static void load(const char* asked) {
  char found[CONFIG_PATH_MAX];
  ui.ini_exists = config_locate(asked, found, sizeof found) && file_exists(found);
  if (!ui.ini_exists && !asked) snprintf(found, sizeof found, "%s%s", ui.base, CONFIG_FILE);
  absolute(found, ui.ini, sizeof ui.ini);
  dir_of(ui.ini, ui.ini_dir, sizeof ui.ini_dir);
  config_defaults(&ui.def);
  config_defaults(&ui.cfg);
  if (ui.ini_exists) {
    char* text = (char*)read_all(ui.ini, NULL);
    if (text) {
      config_parse(&ui.cfg, text, ui.ini);
      free(text);
    }
    if (ui.cfg.warnings)
      say("%d line%s of %s could not be read. They keep their defaults, and Save writes those in.",
          ui.cfg.warnings, ui.cfg.warnings == 1 ? "" : "s", base_name(ui.ini));
  }
  if (!ui.cfg.rom[0]) snprintf(ui.cfg.rom, sizeof ui.cfg.rom, "%s", CONFIG_ROM_DEFAULT);
  snprintf(ui.cfg.path, sizeof ui.cfg.path, "%s", ui.ini);
  ui.cfg.warnings = 0;
}

static bool rom_found(char* resolved, size_t size) {
  config_resolve(&ui.cfg, ui.cfg.rom, resolved, size);
  return file_exists(resolved);
}

static void commit_edit(void);
static void drop_close(void);

// True when the cartridge is missing and that is to be shown.
static bool rom_missing(char* resolved, size_t size) {
  return !rom_found(resolved, size) && ui.rom_checked;
}

static bool save(void) {
  if (ui.editing) commit_edit();
  char* old = ui.ini_exists ? (char*)read_all(ui.ini, NULL) : NULL;
  const char* from = old;
  char* fresh = NULL;
  if (!from) {
    // The default text, with the line endings the game gives the file it writes.
#ifdef _WIN32
    fresh = (char*)malloc(sizeof CONFIG_DEFAULT_TEXT * 2);
    if (fresh) {
      char* o = fresh;
      for (const char* p = CONFIG_DEFAULT_TEXT; *p; p++) {
        if (*p == '\n') *o++ = '\r';
        *o++ = *p;
      }
      *o = 0;
    }
    from = fresh;
#else
    from = CONFIG_DEFAULT_TEXT;
#endif
  }
  char* text = from ? config_update_text(from, &ui.cfg) : NULL;
  free(old);
  free(fresh);
  if (!text) { say("Out of memory writing %s", base_name(ui.ini)); return false; }
  char tmp[CONFIG_PATH_MAX + 8];
  snprintf(tmp, sizeof tmp, "%s.tmp", ui.ini);
  FILE* f = fopen(tmp, "wb");
  bool ok = f && fwrite(text, 1, strlen(text), f) == strlen(text);
  if (f && fclose(f) != 0) ok = false;
  free(text);
#ifdef _WIN32
  ok = ok && MoveFileExA(tmp, ui.ini, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
  ok = ok && rename(tmp, ui.ini) == 0;
#endif
  if (!ok) {
    remove(tmp);
    say("Could not write %s", base_name(ui.ini));
    return false;
  }
  ui.ini_exists = true;
  ui.dirty = false;
  ui.status[0] = 0;
  return true;
}

// The game that Play started, while it runs.
#ifdef _WIN32
static HANDLE game;
#else
static pid_t game;
#endif

// True once that game has closed, with its exit code.
static bool game_closed(int* code) {
#ifdef _WIN32
  if (WaitForSingleObject(game, 0) != WAIT_OBJECT_0) return false;
  DWORD c = 0;
  GetExitCodeProcess(game, &c);
  CloseHandle(game);
  game = NULL;
  *code = (int)c;
#else
  int st;
  if (waitpid(game, &st, WNOHANG) != game) return false;
  game = 0;
  *code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
  return true;
}

// Never the launcher's end: it waits for the game, out of sight.
static bool launch(void) {
  char rom[CONFIG_PATH_MAX];
  if (!rom_found(rom, sizeof rom)) {
    // The ROM row, focused, says where it looked: nothing beside the buttons.
    ui.tab = TAB_GAME;
    ui.on_buttons = false;
    ui.focus = first_row();
    ui.scroll = 0;
    ui.rom_checked = true;
    ui.status[0] = 0;
    return false;
  }
  char exe[CONFIG_PATH_MAX + 16];
  snprintf(exe, sizeof exe, "%s%s", ui.base, GAME_EXE);
  if (!file_exists(exe)) { say("%s is not beside the launcher, in %s", GAME_EXE, ui.base); return false; }
  if (!save()) return false;
#ifdef _WIN32
  char cmd[CONFIG_PATH_MAX * 2 + 64];
  snprintf(cmd, sizeof cmd, "\"%s\" --config \"%s\"", exe, ui.ini);
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, ui.base, &si, &pi)) {
    say("Could not start %s (error %lu)", exe, (unsigned long)GetLastError());
    return false;
  }
  CloseHandle(pi.hThread);
  game = pi.hProcess;
#else
  const pid_t pid = fork();
  if (pid == 0) {
    if (chdir(ui.base) != 0) _exit(127);
    execl(exe, exe, "--config", ui.ini, (char*)NULL);
    _exit(127);
  }
  if (pid < 0) { say("Could not start %s", exe); return false; }
  game = pid;
#endif
  ui.held_dir = 0;
  drop_close();
  SDL_HideWindow(ui.win);
  return false;
}

// A file chosen in the system's dialog. Windows only; elsewhere the path is
// typed, or dropped on the window.
static bool browse(int id, char* out, size_t size) {
#ifdef _WIN32
  char file[MAX_PATH] = "";
  char dir[MAX_PATH];
  snprintf(dir, sizeof dir, "%s", ui.ini_dir);
  SDL_SysWMinfo wm;
  SDL_VERSION(&wm.version);
  OPENFILENAMEA ofn;
  ZeroMemory(&ofn, sizeof ofn);
  ofn.lStructSize = sizeof ofn;
  if (SDL_GetWindowWMInfo(ui.win, &wm)) ofn.hwndOwner = wm.info.win.window;
  ofn.lpstrFile = file;
  ofn.nMaxFile = sizeof file;
  ofn.lpstrInitialDir = dir;
  ofn.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
  bool ok;
  if (id == S_ROM) {
    ofn.lpstrTitle = "The cartridge";
    ofn.lpstrFilter = "SNES cartridges (*.sfc, *.smc)\0*.sfc;*.smc\0All files\0*.*\0";
    ofn.Flags |= OFN_FILEMUSTEXIST;
    ok = GetOpenFileNameA(&ofn) != 0;
  } else {
    ofn.lpstrTitle = "Where to keep the top scores";
    ofn.lpstrFilter = "Top scores (*.hiscore)\0*.hiscore\0All files\0*.*\0";
    ofn.lpstrDefExt = "hiscore";
    ok = GetSaveFileNameA(&ofn) != 0;
  }
  if (ok) relative_to_ini(file, out, size);
  return ok;
#else
  (void)id; (void)out; (void)size;
  return false;
#endif
}

// --- changing things -------------------------------------------------------------

static void begin_edit(int i) {
  const Row* r = row_at(i);
  ui.editing = true;
  ui.edit_row = i;
  snprintf(ui.edit, sizeof ui.edit, "%s", path_of(&ui.cfg, r->id));
  snprintf(ui.edit_was, sizeof ui.edit_was, "%s", ui.edit);
  ui.caret = (int)strlen(ui.edit);
  SDL_StartTextInput();
}

static void end_edit(void) {
  ui.editing = false;
  SDL_StopTextInput();
}

static void commit_edit(void) {
  const Row* r = row_at(ui.edit_row);
  end_edit();
  if (!r) return;
  char* t = config_trim(ui.edit);
  // "Copy as path" puts quotes round it.
  size_t n = strlen(t);
  if (n >= 2 && t[0] == '"' && t[n - 1] == '"') { t[n - 1] = 0; t++; }
  if (r->id == S_ROM && !*t) { say("The cartridge needs a path; it is as it was."); return; }
  char* dst = path_of(&ui.cfg, r->id);
  if (strcmp(dst, t)) {
    snprintf(dst, CONFIG_PATH_MAX, "%s", t);
    changed();
  }
  if (r->id == S_ROM) ui.rom_checked = true;
}

static void edit_insert(const char* s) {
  const size_t len = strlen(ui.edit), add_n = strlen(s);
  if (len + add_n >= sizeof ui.edit - 1) return;
  memmove(ui.edit + ui.caret + add_n, ui.edit + ui.caret, len - (size_t)ui.caret + 1);
  memcpy(ui.edit + ui.caret, s, add_n);
  ui.caret += (int)add_n;
}

static void set_path(int id, const char* path) {
  char* dst = path_of(&ui.cfg, id);
  if (strcmp(dst, path)) {
    snprintf(dst, CONFIG_PATH_MAX, "%s", path);
    changed();
  }
  if (id == S_ROM) ui.rom_checked = true;
}

// The whole seconds left to press something, rounded up, as the countdown
// shows them.
static int capture_left(void) { return (int)((ui.capture_end - SDL_GetTicks() + 999) / 1000); }

static void start_capture(int i) {
  const Row* r = row_at(i);
  const int n = r->kind == K_KEYS ? key_count(keys_of(&ui.cfg, r->id)) : bind_count(pads_of(&ui.cfg, r->id));
  if (n >= CONFIG_KEYS_MAX) { say("Four at most. Take one off first."); return; }
  ui.capturing = true;
  ui.capture_row = i;
  ui.capture_n = 0;
  ui.capture_end = SDL_GetTicks() + 5000;
  ui.status[0] = 0;
}

static void remove_binding(int i, int which) {
  const Row* r = row_at(i);
  if (!r) return;
  if (r->kind == K_KEYS) {
    SDL_Keycode* l = keys_of(&ui.cfg, r->id);
    const int n = key_count(l);
    if (which < 0) which = n - 1;
    if (which < 0 || which >= n) return;
    for (int k = which; k < CONFIG_KEYS_MAX - 1; k++) l[k] = l[k + 1];
    l[CONFIG_KEYS_MAX - 1] = SDLK_UNKNOWN;
  } else if (r->kind == K_PADS) {
    int16_t* l = pads_of(&ui.cfg, r->id);
    const int n = bind_count(l);
    if (which < 0) which = n - 1;
    if (which < 0 || which >= n) return;
    for (int k = which; k < PAD_BIND_MAX - 1; k++) l[k] = l[k + 1];
    l[PAD_BIND_MAX - 1] = PAD_IN_NONE;
  } else {
    return;
  }
  changed();
}

static void capture_key(SDL_Keycode k) {
  const Row* r = row_at(ui.capture_row);
  ui.capturing = false;
  char name[64];
  if (!config_key_name(k, name, sizeof name)) { say("That key has no name zamn.ini can hold."); return; }
  SDL_Keycode* l = keys_of(&ui.cfg, r->id);
  const int n = key_count(l);
  for (int i = 0; i < n; i++)
    if (l[i] == k) return;
  if (n < CONFIG_KEYS_MAX) { l[n] = k; changed(); }
}

static void capture_pad(int in) {
  const Row* r = row_at(ui.capture_row);
  ui.capturing = false;
  char name[32];
  config_pad_input_text(in, name, sizeof name);
  if (!name[0]) return;
  if (pad_map_add(pads_of(&ui.cfg, r->id), in)) changed();
}

// A pad input going down while a binding is waited for. A hotkey waits for a
// second one, and is the two together if it comes before the first is let go
// (`capture_pad_up`); anything else is bound at once.
static void capture_pad_down(int in) {
  if ((row_at(ui.capture_row)->id >> 8) != G_PAD_HOT) { capture_pad(in); return; }
  if (ui.capture_n == 1 && ui.capture_in[0] != in) capture_pad(pad_chord(ui.capture_in[0], in));
  else if (ui.capture_n == 0) ui.capture_in[ui.capture_n++] = in;
}

static void capture_pad_up(int in) {
  if (ui.capture_n == 1 && ui.capture_in[0] == in) capture_pad(in);
}

static void move_focus(int d) {
  if (ui.on_buttons) {
    if (d < 0) {
      ui.on_buttons = false;
      for (int i = row_count[ui.tab] - 1; i >= 0; i--)
        if (rows[ui.tab][i].kind != K_HEAD) { ui.focus = i; break; }
      reveal(ui.focus);
    }
    return;
  }
  for (int i = ui.focus + d; i >= 0 && i < row_count[ui.tab]; i += d)
    if (rows[ui.tab][i].kind != K_HEAD) { ui.focus = i; reveal(i); return; }
  if (d > 0) ui.on_buttons = true;
  else { ui.scroll = 0; }
}

static void set_tab(int t) {
  if (ui.editing) commit_edit();
  drop_close();
  ui.capturing = false;
  ui.tab = (t + TAB_COUNT) % TAB_COUNT;
  ui.focus = first_row();
  ui.scroll = 0;
  ui.on_buttons = false;
}

// --- dropdowns -------------------------------------------------------------------------

#define OPTIONS_MAX 64
#define DROP_SHOWN 10

// The values of a row with a fixed list, in the order the list shows them;
// 0 for a row with none. The starting level puts Off and the 48 levels
// first, then the credits and the bonus rooms.
static int row_options(const Row* r, int* out) {
  int n = 0;
  if (r->kind == K_CHOICE) {
    for (int i = 0; i < r->count; i++) out[n++] = r->choices[i].value;
    return n;
  }
  if (r->kind != K_RANGE || r->slider) return 0;
  if (r->id == S_LEVEL) {
    out[n++] = -1;
    for (int v = 1; v <= 49; v++) out[n++] = v;
    out[n++] = 0;
    for (int v = 50; v <= 55; v++) out[n++] = v;
  } else {
    for (int v = r->lo; v <= r->hi && n < OPTIONS_MAX; v += r->step) out[n++] = v;
  }
  return n;
}

static void option_text(const Row* r, int v, char* out, size_t size) {
  if (r->kind == K_CHOICE) {
    out[0] = 0;
    for (int k = 0; k < r->count; k++)
      if (r->choices[k].value == v) snprintf(out, size, "%s", r->choices[k].label);
  } else {
    range_text(r, v, out, size);
  }
}

static int option_index(const int* opts, int n, int v) {
  for (int i = 0; i < n; i++)
    if (opts[i] == v) return i;
  return 0;
}

static void drop_reveal(int n) {
  if (ui.drop_sel < ui.drop_first) ui.drop_first = ui.drop_sel;
  if (ui.drop_sel >= ui.drop_first + ui.drop_shown) ui.drop_first = ui.drop_sel - ui.drop_shown + 1;
  if (ui.drop_first > n - ui.drop_shown) ui.drop_first = n - ui.drop_shown;
  if (ui.drop_first < 0) ui.drop_first = 0;
}

static void drop_open(int i) {
  if (ui.editing) commit_edit();
  const Row* r = row_at(i);
  int opts[OPTIONS_MAX];
  const int n = r ? row_options(r, opts) : 0;
  if (!n) return;
  ui.focus = i;
  ui.on_buttons = false;
  reveal(i);
  ui.drop_row = i;
  ui.drop_sel = option_index(opts, n, setting_get(&ui.cfg, r->id));
  // The value in the middle of what shows, where there is room.
  ui.drop_shown = DROP_SHOWN;
  ui.drop_first = ui.drop_sel - DROP_SHOWN / 2;
  drop_reveal(n);
}

static void drop_close(void) { ui.drop_row = -1; }

static void drop_move(int d) {
  int opts[OPTIONS_MAX];
  const int n = row_options(row_at(ui.drop_row), opts);
  ui.drop_sel += d;
  if (ui.drop_sel < 0) ui.drop_sel = 0;
  if (ui.drop_sel >= n) ui.drop_sel = n - 1;
  drop_reveal(n);
}

static void drop_pick(void) {
  const Row* r = row_at(ui.drop_row);
  int opts[OPTIONS_MAX];
  const int n = row_options(r, opts);
  drop_close();
  if (ui.drop_sel < 0 || ui.drop_sel >= n) return;
  if (opts[ui.drop_sel] != setting_get(&ui.cfg, r->id)) {
    setting_set(&ui.cfg, r->id, opts[ui.drop_sel]);
    changed();
  }
}

static void change(int d) {
  if (ui.on_buttons) {
    ui.button = (ui.button + d + UI_BUTTONS) % UI_BUTTONS;
    return;
  }
  const Row* r = row_at(ui.focus);
  if (!r) return;
  const int v = setting_get(&ui.cfg, r->id);
  int nv = v;
  int opts[OPTIONS_MAX];
  const int n = row_options(r, opts);
  if (n) {
    // A choice goes round; a list of numbers stops at its ends.
    int to = option_index(opts, n, v) + d;
    if (r->kind == K_CHOICE) to = (to + n) % n;
    else to = to < 0 ? 0 : to >= n ? n - 1 : to;
    nv = opts[to];
  } else if (r->kind == K_RANGE) {
    nv = range_step(r, v, d);
  }
  if (nv != v) {
    setting_set(&ui.cfg, r->id, nv);
    changed();
  }
}

static bool press_button(int b) {
  switch (b) {
    case UI_DEFAULTS:
      for (int i = 0; i < row_count[ui.tab]; i++) row_copy(&ui.cfg, &ui.def, &rows[ui.tab][i]);
      changed();
      return false;
    case UI_SAVE: save(); return false;
    case UI_PLAY: return launch();
    case UI_QUIT: return true;
    default: return false;
  }
}

static bool activate(bool from_pad) {
  if (ui.drop_row >= 0) { drop_pick(); return false; }
  if (ui.on_buttons) return press_button(ui.button);
  const Row* r = row_at(ui.focus);
  if (!r) return false;
  switch (r->kind) {
    case K_CHOICE:
    case K_RANGE: drop_open(ui.focus); break;
    case K_PATH: {
      char path[CONFIG_PATH_MAX];
      if (from_pad) { if (browse(r->id, path, sizeof path)) set_path(r->id, path); }
      else begin_edit(ui.focus);
      break;
    }
    case K_KEYS:
    case K_PADS: start_capture(ui.focus); break;
    default: break;
  }
  return false;
}

// --- who wins a key -----------------------------------------------------------------

// The row a key does something for: the first in the order the game looks,
// which is `config_check`'s.
static int key_owner(SDL_Keycode k) {
  for (int a = 0; a < ACT_COUNT; a++)
    for (int i = 0; i < CONFIG_KEYS_MAX && ui.cfg.hotkey[a][i] != SDLK_UNKNOWN; i++)
      if (ui.cfg.hotkey[a][i] == k) return BIND(G_KEY_HOT, a);
  for (int p = 0; p < MOVIE_PORTS; p++)
    for (int b = 0; b < 12; b++)
      for (int i = 0; i < CONFIG_KEYS_MAX && ui.cfg.key[p][b][i] != SDLK_UNKNOWN; i++)
        if (ui.cfg.key[p][b][i] == k) return BIND(p ? G_KEY_2 : G_KEY_1, b);
  return 0;
}

// A pad input on a SNES button that also selects does the selecting only.
static int pad_owner(int id, int in) {
  if ((id >> 8) != G_PAD_GAME) return id;
  for (int k = 0; k < PAD_CYCLE_COUNT; k++)
    for (int i = 0; i < PAD_BIND_MAX && ui.cfg.pad.cycle[k][i] != PAD_IN_NONE; i++)
      if (ui.cfg.pad.cycle[k][i] == in) return BIND(G_PAD_CYCLE, k);
  return id;
}

// What a binding row is, in the middle of a sentence: "player 1's Up", "the
// quick save hotkey", "next weapon".
static const char* bind_label(int id) {
  static char out[96];
  const int g = id >> 8;
  for (int t = 0; t < TAB_COUNT; t++)
    for (int r = 0; r < row_count[t]; r++)
      if ((rows[t][r].kind == K_KEYS || rows[t][r].kind == K_PADS) && rows[t][r].id == id) {
        char lower[64];
        snprintf(lower, sizeof lower, "%s", rows[t][r].label);
        lower[0] = (char)tolower((unsigned char)lower[0]);
        if (g == G_KEY_1 || g == G_KEY_2) snprintf(out, sizeof out, "player %d's %s", g == G_KEY_2 ? 2 : 1, rows[t][r].label);
        else if (g == G_KEY_HOT || g == G_PAD_HOT) snprintf(out, sizeof out, "the %s hotkey", lower);
        else snprintf(out, sizeof out, "%s", lower);
        return out;
      }
  return "something else";
}

// What a controller calls an input: the positions the file uses, or the
// legends of the pad that was last touched.
static const char* pad_label(int in) {
  static const char* const ps[] = {"Cross", "Circle", "Square", "Triangle", "Create", "PS", "Options",
                                   "L3", "R3", "L1", "R1", "D-pad up", "D-pad down", "D-pad left",
                                   "D-pad right", "Mute", "Paddle 1", "Paddle 2", "Paddle 3",
                                   "Paddle 4", "Touchpad"};
  static const char* const xb[] = {"A", "B", "X", "Y", "View", "Xbox", "Menu", "LS", "RS", "LB",
                                   "RB", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
                                   "Share", "P1", "P2", "P3", "P4", "Touchpad"};
  static const char* const pos[] = {"South", "East", "West", "North", "Select", "Guide", "Start",
                                    "L3", "R3", "L1", "R1", "D-pad up", "D-pad down", "D-pad left",
                                    "D-pad right", "Misc", "Paddle 1", "Paddle 2", "Paddle 3",
                                    "Paddle 4", "Touchpad"};
  if (pad_is_chord(in)) {
    // Two at a time can be asked for in one sentence, so two to write into.
    static char both[2][48];
    static int turn;
    char* out = both[turn ^= 1];
    snprintf(out, sizeof both[0], "%s + %s", pad_label(pad_chord_first(in)), pad_label(pad_chord_second(in)));
    return out;
  }
  if (in == PAD_IN_LTRIGGER) return ui.pad_style == STYLE_XBOX ? "LT" : "L2";
  if (in == PAD_IN_RTRIGGER) return ui.pad_style == STYLE_XBOX ? "RT" : "R2";
  if (in < 0 || in >= COUNT(pos)) return "?";
  return ui.pad_style == STYLE_PLAYSTATION ? ps[in] : ui.pad_style == STYLE_XBOX ? xb[in] : pos[in];
}

static void note_pad(SDL_GameController* gc) {
  if (!gc) return;
  switch (SDL_GameControllerGetType(gc)) {
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4:
    case SDL_CONTROLLER_TYPE_PS5: ui.pad_style = STYLE_PLAYSTATION; break;
    case SDL_CONTROLLER_TYPE_XBOX360:
    case SDL_CONTROLLER_TYPE_XBOXONE: ui.pad_style = STYLE_XBOX; break;
    default: ui.pad_style = STYLE_POSITION; break;
  }
}

// --- drawing ------------------------------------------------------------------------

static void fill(float x, float y, float w, float h, SDL_Color c) {
  SDL_SetRenderDrawColor(ui.ren, c.r, c.g, c.b, c.a);
  const SDL_Rect r = {(int)x, (int)y, (int)w, (int)h};
  SDL_RenderFillRect(ui.ren, &r);
}

static void outline(float x, float y, float w, float h, float t, SDL_Color c) {
  fill(x, y, w, t, c);
  fill(x, y + h - t, w, t, c);
  fill(x, y, t, h, c);
  fill(x + w - t, y, t, h, c);
}

// Pointing down, or up when `up`.
static void chevron(float cx, float cy, float size, bool up, SDL_Color c) {
  SDL_Vertex v[3];
  memset(v, 0, sizeof v);
  const float hw = size, hh = size * 0.55f, dir = up ? -1.0f : 1.0f;
  v[0].position = (SDL_FPoint){cx, cy + dir * hh};
  v[1].position = (SDL_FPoint){cx - hw, cy - dir * hh};
  v[2].position = (SDL_FPoint){cx + hw, cy - dir * hh};
  for (int i = 0; i < 3; i++) v[i].color = c;
  SDL_RenderGeometry(ui.ren, NULL, v, 3, NULL, 0);
}

static void hot(float x, float y, float w, float h, Part part, int row, int index) {
  if (ui.nhot >= COUNT(ui.hot)) return;
  // Only what can be seen of a row in the list can be clicked.
  if (part != P_TAB && part != P_BUTTON && part != P_OPTION) {
    const float top = list_top(), bottom = top + list_h();
    if (y < top) { h -= top - y; y = top; }
    if (y + h > bottom) h = bottom - y;
    if (h <= 0) return;
  }
  ui.hot[ui.nhot++] = (Hot){{(int)x, (int)y, (int)w, (int)h}, part, row, index};
}

// Text cut to `w` from the left, with an ellipsis: a path's end is the part
// worth seeing.
static void text_tail(const Font* f, float x, float y, float w, const char* s, SDL_Color c) {
  if (text_width(f, s) <= w) { text(ui.ren, f, x, y, s, c); return; }
  const float dots = text_width(f, "...");
  while (*s && text_width(f, s) + dots > w) s++;
  const float e = text(ui.ren, f, x, y, "...", c);
  text(ui.ren, f, e, y, s, c);
}

// Words laid into `w`, `max_lines` at most; returns how many lines it took.
// Drawn unless `measure`.
static int wrap_lines(const Font* f, float x, float y, float w, const char* s, SDL_Color c, int max_lines,
                      bool measure) {
  char line[512];
  int lines = 0;
  while (*s && lines < max_lines) {
    int take = 0, fit = 0;
    for (;;) {
      while (s[take] && s[take] != ' ') take++;
      char try_line[512];
      snprintf(try_line, sizeof try_line, "%.*s", take, s);
      if (fit && text_width(f, try_line) > w) break;
      fit = take;
      if (!s[take]) break;
      take++;
    }
    snprintf(line, sizeof line, "%.*s", fit, s);
    if (!measure) text(ui.ren, f, x, y + lines * f->height, line, c);
    lines++;
    s += fit;
    while (*s == ' ') s++;
  }
  return lines;
}

static float wrap(const Font* f, float x, float y, float w, const char* s, SDL_Color c, int max_lines) {
  return wrap_lines(f, x, y, w, s, c, max_lines, false) * f->height;
}

// On is green and Off dim, as they always were; anything else is text.
static SDL_Color option_color(const Row* r, int v) {
  if (r->choices == on_off) return v ? C_GREEN : C_DIM;
  return C_TEXT;
}

static void draw_dropdown(int i, const Row* r, float x, float y, float w, float h, bool focused) {
  const int v = setting_get(&ui.cfg, r->id);
  const bool open = ui.drop_row == i;
  char label[128];
  option_text(r, v, label, sizeof label);
  fill(x, y, w, h, open ? (SDL_Color){18, 18, 18, 255} : C_BLACK);
  outline(x, y, w, h, L(1), open ? C_GREEN : focused ? C_DIM : C_FAINT);
  text_tail(&ui.body, x + L(14), y + (h - ui.body.height) / 2, w - L(54), label, option_color(r, v));
  chevron(x + w - L(22), y + h / 2, L(6), open, focused || open ? C_GREEN : C_DIM);
  hot(x, y, w, h, P_DROP, i, 0);
  if (open) {
    ui.drop_x = x;
    ui.drop_y = y;
    ui.drop_w = w;
    ui.drop_h = h;
  }
}

// The open list, over everything else: below its box, or above it when there
// is more room there.
static void draw_drop_list(void) {
  const Row* r = row_at(ui.drop_row);
  int opts[OPTIONS_MAX];
  const int n = r ? row_options(r, opts) : 0;
  if (!n) { drop_close(); return; }
  const float ih = L(34), pad = L(4);
  const float below = ui.h - L(8) - (ui.drop_y + ui.drop_h);
  const float above = ui.drop_y - L(8);
  int shown = n < DROP_SHOWN ? n : DROP_SHOWN;
  const bool up = shown * ih + pad * 2 > below && above > below;
  const int fits = (int)(((up ? above : below) - pad * 2) / ih);
  if (shown > fits) shown = fits < 1 ? 1 : fits;
  if (shown != ui.drop_shown) {
    ui.drop_shown = shown;
    drop_reveal(n);
  }
  if (ui.drop_first > n - shown) ui.drop_first = n - shown;
  if (ui.drop_first < 0) ui.drop_first = 0;
  const float lh = shown * ih + pad * 2;
  const float lx = ui.drop_x, lw = ui.drop_w;
  const float ly = up ? ui.drop_y - lh + L(1) : ui.drop_y + ui.drop_h - L(1);
  fill(lx, ly, lw, lh, (SDL_Color){18, 18, 18, 255});
  outline(lx, ly, lw, lh, L(1), C_GREEN);
  const int cur = setting_get(&ui.cfg, r->id);
  for (int k = 0; k < shown; k++) {
    const int o = ui.drop_first + k;
    const float iy = ly + pad + k * ih;
    if (o == ui.drop_sel) {
      fill(lx + L(1), iy, lw - L(2), ih, (SDL_Color){40, 40, 40, 255});
      fill(lx + L(1), iy, L(4), ih, C_GREEN);
    }
    char label[128];
    option_text(r, opts[o], label, sizeof label);
    SDL_Color c = opts[o] == cur ? C_GREEN : o == ui.drop_sel ? C_TEXT : (SDL_Color){205, 205, 205, 255};
    text_tail(&ui.body, lx + L(14), iy + (ih - ui.body.height) / 2, lw - L(34), label, c);
    hot(lx, iy, lw, ih, P_OPTION, ui.drop_row, o);
  }
  if (n > shown) {
    const float track = lh - pad * 2;
    const float bar_h = track * shown / n, bar_y = ly + pad + track * ui.drop_first / n;
    fill(lx + lw - L(8), bar_y, L(3), bar_h, C_DIM);
  }
}

static void draw_slider(int i, const Row* r, float x, float y, float w, float h, bool focused) {
  const int v = setting_get(&ui.cfg, r->id);
  char label[32];
  range_text(r, v, label, sizeof label);
  const float track_w = w - L(80);
  const float frac = (float)(v - r->lo) / (float)(r->hi - r->lo);
  const float mid = y + h / 2;
  fill(x, mid - L(3), track_w, L(6), C_FAINT);
  fill(x, mid - L(3), track_w * (frac < 0 ? 0 : frac > 1 ? 1 : frac), L(6), focused ? C_GREEN : C_DIM);
  const float kx = x + track_w * (frac < 0 ? 0 : frac > 1 ? 1 : frac);
  fill(kx - L(5), mid - L(11), L(10), L(22), focused ? C_TEXT : C_DIM);
  text(ui.ren, &ui.body, x + track_w + L(18), y + (h - ui.body.height) / 2, label, C_TEXT);
  hot(x - L(8), y, track_w + L(16), h, P_SLIDER, i, 0);
}

static void draw_path(int i, const Row* r, float x, float y, float w, float h, bool focused) {
#ifdef _WIN32
  const float bw = L(96);
#else
  const float bw = 0;
#endif
  const float fw = bw ? w - bw - L(8) : w;
  const bool editing = ui.editing && ui.edit_row == i;
  const char* value = editing ? ui.edit : path_of(&ui.cfg, r->id);
  bool bad = false;
  if (r->id == S_ROM && !editing) {
    char resolved[CONFIG_PATH_MAX];
    bad = rom_missing(resolved, sizeof resolved);
  }
  fill(x, y, fw, h, (SDL_Color){14, 14, 14, 255});
  outline(x, y, fw, h, L(1), editing ? C_GREEN : bad ? C_RED : focused ? C_DIM : C_FAINT);
  const float tx = x + L(10), ty = y + (h - ui.body.height) / 2, tw = fw - L(20);
  SDL_Rect clip = {(int)x, (int)y, (int)fw, (int)h};
  SDL_Rect list = {0, (int)list_top(), ui.w, (int)list_h()};
  SDL_IntersectRect(&clip, &list, &clip);
  SDL_RenderSetClipRect(ui.ren, &clip);
  if (editing) {
    // The caret kept in view: the text slides left under it.
    char before[CONFIG_PATH_MAX];
    snprintf(before, sizeof before, "%.*s", ui.caret, ui.edit);
    const float cw = text_width(&ui.body, before);
    const float shift = cw > tw ? cw - tw : 0;
    text(ui.ren, &ui.body, tx - shift, ty, ui.edit, C_TEXT);
    fill(tx - shift + cw, ty + L(2), L(2), ui.body.height - L(4), C_GREEN);
  } else if (*value) {
    text_tail(&ui.body, tx, ty, tw, value, bad ? C_RED : C_TEXT);
  } else {
    text(ui.ren, &ui.body, tx, ty, r->id == S_HISCORE_FILE ? "zamn.hiscore" : "", C_DIM);
  }
  SDL_RenderSetClipRect(ui.ren, &list);
  hot(x, y, fw, h, P_FIELD, i, 0);
  if (bw) {
    outline(x + fw + L(8), y, bw, h, L(1), focused ? C_DIM : C_FAINT);
    const float lw = text_width(&ui.body, "Browse");
    text(ui.ren, &ui.body, x + fw + L(8) + (bw - lw) / 2, ty, "Browse", C_TEXT);
    hot(x + fw + L(8), y, bw, h, P_BROWSE, i, 0);
  }
}

static void draw_bindings(int i, const Row* r, float x, float y, float w, float h, bool focused) {
  float cx = x;
  int n = 0;
  if (r->kind == K_KEYS) {
    const SDL_Keycode* l = keys_of(&ui.cfg, r->id);
    n = key_count(l);
    for (int k = 0; k < n; k++) {
      const char* shown = SDL_GetKeyName(l[k]);
      const bool lost = key_owner(l[k]) != r->id;
      const float cw = text_width(&ui.body, shown) + L(40);
      fill(cx, y, cw, h, lost ? (SDL_Color){80, 18, 16, 255} : (SDL_Color){36, 36, 36, 255});
      text(ui.ren, &ui.body, cx + L(10), y + (h - ui.body.height) / 2, shown, lost ? C_RED : C_TEXT);
      text(ui.ren, &ui.small, cx + cw - L(22), y + (h - ui.small.height) / 2, "\xd7", C_DIM);
      hot(cx + cw - L(28), y, L(28), h, P_CHIP, i, k);
      cx += cw + L(8);
    }
  } else {
    const int16_t* l = pads_of(&ui.cfg, r->id);
    n = bind_count(l);
    for (int k = 0; k < n; k++) {
      const char* shown = pad_label(l[k]);
      const bool lost = pad_owner(r->id, l[k]) != r->id;
      const float cw = text_width(&ui.body, shown) + L(40);
      fill(cx, y, cw, h, lost ? (SDL_Color){80, 18, 16, 255} : (SDL_Color){36, 36, 36, 255});
      text(ui.ren, &ui.body, cx + L(10), y + (h - ui.body.height) / 2, shown, lost ? C_RED : C_TEXT);
      text(ui.ren, &ui.small, cx + cw - L(22), y + (h - ui.small.height) / 2, "\xd7", C_DIM);
      hot(cx + cw - L(28), y, L(28), h, P_CHIP, i, k);
      cx += cw + L(8);
    }
  }
  if (ui.capturing && ui.capture_row == i) {
    const int left = capture_left();
    ui.capture_shown = left;
    char msg[64];
    snprintf(msg, sizeof msg,
             r->kind == K_KEYS ? "Press a key... %d"
             : (r->id >> 8) == G_PAD_HOT ? "Press a button, or two together... %d"
                                         : "Press a button... %d",
             left);
    const float cw = text_width(&ui.body, msg) + L(20);
    outline(cx, y, cw, h, L(2), C_GREEN);
    text(ui.ren, &ui.body, cx + L(10), y + (h - ui.body.height) / 2, msg, C_GREEN);
  } else if (n < CONFIG_KEYS_MAX) {
    if (!n) {
      const float e = text(ui.ren, &ui.body, cx, y + (h - ui.body.height) / 2, "None", C_DIM);
      cx = e + L(12);
    }
    outline(cx, y, h, h, L(1), focused ? C_DIM : C_FAINT);
    const float pw = text_width(&ui.bold, "+");
    text(ui.ren, &ui.bold, cx + (h - pw) / 2, y + (h - ui.bold.height) / 2, "+", focused ? C_GREEN : C_DIM);
    hot(cx, y, h, h, P_ADD, i, 0);
  }
  (void)w;
}

// The title's logo as the heading, if the launcher was built with it, LOGO_H
// high. It is blown up by whole pixels to the next size past that and then
// drawn smoothly down, so that its pixels stay square and sharp at any
// scale. Its width is put in `w`.
#define LOGO_H 104
static bool draw_logo(float* w) {
#ifdef ZAMN_LOGO
  const float k = L(LOGO_H) / ZAMN_LOGO_H;
  const int n = k <= 1 ? 1 : (int)k + ((float)(int)k < k);
  if (ui.logo_n != n) {
    if (ui.logo) SDL_DestroyTexture(ui.logo);
    ui.logo_n = 0;
    const int tw = ZAMN_LOGO_W * n, th = ZAMN_LOGO_H * n;
    ui.logo = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
    uint32_t* big = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)tw * th);
    if (!ui.logo || !big) {
      free(big);
      return false;
    }
    for (int y = 0; y < th; y++)
      for (int x = 0; x < tw; x++) big[(size_t)y * tw + x] = zamn_logo[(y / n) * ZAMN_LOGO_W + x / n];
    SDL_UpdateTexture(ui.logo, NULL, big, tw * 4);
    free(big);
    SDL_SetTextureBlendMode(ui.logo, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(ui.logo, SDL_ScaleModeLinear);
    ui.logo_n = n;
  }
  const int h = (int)(L(LOGO_H) + 0.5f);
  const SDL_Rect to = {(int)L(MARGIN), (int)((L(HEADER_H) - h) / 2), (int)(ZAMN_LOGO_W * k + 0.5f), h};
  SDL_RenderCopy(ui.ren, ui.logo, NULL, &to);
  *w = (float)to.w;
  return true;
#else
  (void)w;
  return false;
#endif
}

static void draw(void) {
  SDL_GetRendererOutputSize(ui.ren, &ui.w, &ui.h);
  layout();
  clamp_scroll();
  ui.nhot = 0;
  SDL_SetRenderDrawColor(ui.ren, 0, 0, 0, 255);
  SDL_RenderClear(ui.ren);
  const float W = (float)ui.w, H = (float)ui.h;

  // The heading.
  float tw;
  if (!draw_logo(&tw))
    text(ui.ren, &ui.title, L(MARGIN), L(18), "ZOMBIES ATE MY NEIGHBORS", C_GREEN);

  // The tabs.
  float x = L(MARGIN) - L(12);
  const float ty = L(HEADER_H);
  for (int t = 0; t < TAB_COUNT; t++) {
    // Measured bold whichever it is, so that the tabs keep their places.
    const Font* f = t == ui.tab ? &ui.bold : &ui.body;
    const float w = text_width(&ui.bold, tab_names[t]) + L(24);
    const float lx = x + (w - text_width(f, tab_names[t])) / 2;
    text(ui.ren, f, lx, ty + (L(TABS_H) - f->height) / 2, tab_names[t], t == ui.tab ? C_TEXT : C_DIM);
    if (t == ui.tab) fill(lx, ty + L(TABS_H) - L(3), text_width(f, tab_names[t]), L(3), C_GREEN);
    hot(x, ty, w, L(TABS_H), P_TAB, -1, t);
    x += w;
  }
  fill(0, ty + L(TABS_H) - L(1), W, L(1), C_FAINT);

  // The rows.
  const float top = list_top(), lh = list_h();
  SDL_Rect list = {0, (int)top, ui.w, (int)lh};
  SDL_RenderSetClipRect(ui.ren, &list);
  const float cx = L(MARGIN) + L(LABEL_W);
  const float cw_full = W - cx - L(MARGIN) - L(12);
  const float cw = cw_full < L(460) ? cw_full : L(460);
  for (int i = 0; i < row_count[ui.tab]; i++) {
    const Row* r = &rows[ui.tab][i];
    const float y = top + row_y[i] - ui.scroll;
    const float h = r->kind == K_HEAD ? L(HEAD_H) : L(ROW_H);
    if (y + h < top || y > top + lh) continue;
    if (r->kind == K_HEAD) {
      text(ui.ren, &ui.bold, L(MARGIN), y + h - ui.bold.height - L(8), r->label, C_GREEN);
      fill(L(MARGIN), y + h - L(4), W - L(MARGIN) * 2, L(1), C_FAINT);
      continue;
    }
    const bool focused = !ui.on_buttons && ui.focus == i;
    if (focused) {
      fill(0, y, W, h, (SDL_Color){26, 26, 26, 255});
      fill(0, y, L(4), h, C_GREEN);
    } else if (ui.hover == i) {
      fill(0, y, W, h, (SDL_Color){14, 14, 14, 255});
    }
    hot(0, y, W, h, P_ROW, i, 0);
    text(ui.ren, &ui.body, L(MARGIN), y + (h - ui.body.height) / 2, r->label, focused ? C_TEXT : (SDL_Color){205, 205, 205, 255});
    const float cy = y + L(6), ch = h - L(12);
    switch (r->kind) {
      case K_CHOICE: draw_dropdown(i, r, cx, cy, cw, ch, focused); break;
      case K_RANGE:
        if (r->slider) draw_slider(i, r, cx, cy, cw, ch, focused);
        else draw_dropdown(i, r, cx, cy, cw, ch, focused);
        break;
      case K_PATH: draw_path(i, r, cx, cy, cw_full, ch, focused); break;
      default: draw_bindings(i, r, cx, cy, cw_full, ch, focused); break;
    }
  }
  SDL_RenderSetClipRect(ui.ren, NULL);
  const float content = row_y[row_count[ui.tab]];
  if (content > lh) {
    const float bar_h = lh * lh / content, bar_y = top + (lh - bar_h) * ui.scroll / (content - lh);
    fill(W - L(6), bar_y, L(3), bar_h, C_FAINT);
  }

  // What the focused thing is, and anything wrong with it.
  const float help_y = H - L(HELP_H + BAR_H);
  fill(0, help_y, W, L(1), C_FAINT);
  const float help_w = W - L(MARGIN) * 2;
  float hy = help_y + L(10);
  if (ui.on_buttons) {
    static const char* const button_help[UI_BUTTONS] = {
      "Put this tab back to its defaults. Nothing is written until Save or Play.",
      "Write these settings to the file. Ctrl+S.",
      "Close the launcher. Escape.",
      "Save, then start the game. Ctrl+Enter or F5, or Start on a controller.",
    };
    wrap(&ui.small, L(MARGIN), hy, help_w, button_help[ui.button], C_DIM, 2);
  } else if (row_at(ui.focus)) {
    const Row* r = row_at(ui.focus);
    char warn[CONFIG_PATH_MAX + 128] = "";
    if (r->kind == K_PATH && r->id == S_ROM) {
      char resolved[CONFIG_PATH_MAX];
      if (rom_missing(resolved, sizeof resolved)) snprintf(warn, sizeof warn, "Not found: %s", resolved);
    } else if (r->kind == K_KEYS) {
      const SDL_Keycode* l = keys_of(&ui.cfg, r->id);
      for (int k = 0; k < key_count(l) && !warn[0]; k++) {
        const int owner = key_owner(l[k]);
        if (owner != r->id)
          snprintf(warn, sizeof warn, "%s is %s as well, and only does that.", SDL_GetKeyName(l[k]), bind_label(owner));
      }
    } else if (r->kind == K_PADS) {
      const int16_t* l = pads_of(&ui.cfg, r->id);
      for (int k = 0; k < bind_count(l) && !warn[0]; k++) {
        const int owner = pad_owner(r->id, l[k]);
        if (owner != r->id)
          snprintf(warn, sizeof warn, "%s is %s as well, and only does that.", pad_label(l[k]), bind_label(owner));
      }
    }
    const int warn_lines = warn[0] ? wrap_lines(&ui.small, 0, 0, help_w, warn, C_RED, 2, true) : 0;
    hy += wrap(&ui.small, L(MARGIN), hy, help_w, r->help, C_DIM, 3 - warn_lines);
    if (warn[0]) wrap(&ui.small, L(MARGIN), hy, help_w, warn, C_RED, 2);
  }

  // The buttons, and what just happened.
  const float by = H - L(BAR_H) + L(14), bh = L(BAR_H) - L(28);
  float bx[UI_BUTTONS], bw[UI_BUTTONS];
  for (int b = 0; b < UI_BUTTONS; b++) bw[b] = text_width(&ui.bold, button_names[b]) + L(44);
  bx[UI_DEFAULTS] = L(MARGIN);
  bx[UI_SAVE] = bx[UI_DEFAULTS] + bw[UI_DEFAULTS] + L(10);
  bx[UI_PLAY] = W - L(MARGIN) - bw[UI_PLAY];
  bx[UI_QUIT] = bx[UI_PLAY] - L(10) - bw[UI_QUIT];
  for (int b = 0; b < UI_BUTTONS; b++) {
    const bool f = ui.on_buttons && ui.button == b;
    const bool play = b == UI_PLAY;
    if (play) fill(bx[b], by, bw[b], bh, C_GREEN);
    outline(bx[b], by, bw[b], bh, f ? L(2) : L(1), f ? C_TEXT : play ? C_GREEN : C_FAINT);
    const float lw = text_width(&ui.bold, button_names[b]);
    text(ui.ren, &ui.bold, bx[b] + (bw[b] - lw) / 2, by + (bh - ui.bold.height) / 2, button_names[b],
         play ? C_BLACK : f ? C_TEXT : (SDL_Color){205, 205, 205, 255});
    hot(bx[b], by, bw[b], bh, P_BUTTON, -1, b);
  }
  const float sx = bx[UI_SAVE] + bw[UI_SAVE] + L(20), sw = bx[UI_QUIT] - L(20) - sx;
  if (sw > L(40) && ui.status[0])
    text_tail(&ui.small, sx, by + (bh - ui.small.height) / 2, sw, ui.status, C_RED);

  if (ui.drop_row >= 0) draw_drop_list();

}

// --- scale ------------------------------------------------------------------------

static float display_scale(void) {
  float dpi = 96;
  const int d = SDL_GetWindowDisplayIndex(ui.win);
  if (SDL_GetDisplayDPI(d < 0 ? 0 : d, &dpi, NULL, NULL) != 0 || dpi <= 0) dpi = 96;
  int ww, wh, ow, oh;
  SDL_GetWindowSize(ui.win, &ww, &wh);
  SDL_GetRendererOutputSize(ui.ren, &ow, &oh);
  ui.mouse_scale = ww > 0 ? (float)ow / ww : 1;
  float s = dpi / 96.0f;
#ifndef _WIN32
  s *= ui.mouse_scale;  // a Retina display's pixels are not in its DPI
#endif
  return s < 1 ? 1 : s > 4 ? 4 : s;
}

static bool make_fonts(void) {
  font_free(&ui.body);
  font_free(&ui.small);
  font_free(&ui.bold);
  font_free(&ui.title);
  const float s = ui.scale;
  return font_make(&ui.body, ui.ren, ui.ttf[0], 19 * s) && font_make(&ui.small, ui.ren, ui.ttf[0], 16 * s) &&
         font_make(&ui.bold, ui.ren, ui.ttf[1], 19 * s) && font_make(&ui.title, ui.ren, ui.ttf[1], 30 * s);
}

// --- input ------------------------------------------------------------------------

static const Hot* hit(int x, int y) {
  const SDL_Point p = {(int)(x * ui.mouse_scale), (int)(y * ui.mouse_scale)};
  for (int i = ui.nhot - 1; i >= 0; i--)
    if (SDL_PointInRect(&p, &ui.hot[i].r)) return &ui.hot[i];
  return NULL;
}

static void slide_to(int i, int mx) {
  const Row* r = row_at(i);
  for (int k = 0; k < ui.nhot; k++) {
    const Hot* h = &ui.hot[k];
    if (h->part != P_SLIDER || h->row != i) continue;
    const float track_x = h->r.x + L(8), track_w = h->r.w - L(16);
    float f = (mx * ui.mouse_scale - track_x) / track_w;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    int v = r->lo + (int)(f * (r->hi - r->lo) + 0.5f);
    v = r->lo + ((v - r->lo + r->step / 2) / r->step) * r->step;
    if (v > r->hi) v = r->hi;
    if (v != setting_get(&ui.cfg, r->id)) {
      setting_set(&ui.cfg, r->id, v);
      changed();
    }
    return;
  }
}

// True when the launcher is done.
static bool click(int mx, int my) {
  const Hot* h = hit(mx, my);
  if (ui.editing && !(h && h->part == P_FIELD && h->row == ui.edit_row)) commit_edit();
  // An open list takes the click: a choice, or anywhere else to close it.
  if (ui.drop_row >= 0) {
    if (h && h->part == P_OPTION) {
      ui.drop_sel = h->index;
      drop_pick();
    } else {
      drop_close();
    }
    return false;
  }
  if (!h) return false;
  if (h->row >= 0) {
    ui.focus = h->row;
    ui.on_buttons = false;
  }
  switch (h->part) {
    case P_TAB: set_tab(h->index); break;
    case P_BUTTON:
      ui.on_buttons = true;
      ui.button = h->index;
      return press_button(h->index);
    case P_DROP: drop_open(h->row); break;
    case P_SLIDER: ui.dragging = h->row; slide_to(h->row, mx); break;
    case P_FIELD:
      if (!ui.editing) begin_edit(h->row);
      break;
    case P_BROWSE: ui.browse_row = h->row; break;
    case P_CHIP: remove_binding(h->row, h->index); break;
    case P_ADD: start_capture(h->row); break;
    default: break;
  }
  return false;
}

// Up, down, left, right as 1 to 4; 0 for none.
enum { DIR_NONE, DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

static void go(int dir) {
  if (ui.drop_row >= 0) {
    if (dir == DIR_UP || dir == DIR_DOWN) drop_move(dir == DIR_UP ? -1 : 1);
    return;
  }
  switch (dir) {
    case DIR_UP: move_focus(-1); break;
    case DIR_DOWN: move_focus(1); break;
    case DIR_LEFT: change(-1); break;
    case DIR_RIGHT: change(1); break;
    default: break;
  }
}

static void hold(int dir) {
  ui.held_dir = dir;
  ui.held_next = SDL_GetTicks() + 380;
  go(dir);
}

// True when the launcher is done. Says in `ui.redraw` whether it needs drawing.
static bool handle(const SDL_Event* e) {
  ui.redraw = true;
  // A binding being waited for takes what comes first. Its countdown is
  // drawn by the loop, so only its end is a reason to draw here.
  if (ui.capturing) {
    const Row* r = row_at(ui.capture_row);
    if (e->type == SDL_MOUSEBUTTONDOWN) { ui.capturing = false; return false; }
    if (r->kind == K_KEYS) {
      if (e->type == SDL_KEYDOWN && !e->key.repeat) capture_key(e->key.keysym.sym);
      else if (e->type == SDL_CONTROLLERBUTTONDOWN) ui.capturing = false;
    } else {
      if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) ui.capturing = false;
      else if (e->type == SDL_CONTROLLERBUTTONDOWN) capture_pad_down(e->cbutton.button);
      else if (e->type == SDL_CONTROLLERBUTTONUP) capture_pad_up(e->cbutton.button);
      else if (e->type == SDL_CONTROLLERAXISMOTION) {
        const int in = e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? PAD_IN_LTRIGGER
                     : e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? PAD_IN_RTRIGGER : PAD_IN_NONE;
        if (in != PAD_IN_NONE && e->caxis.value > 16000) capture_pad_down(in);
        else if (in != PAD_IN_NONE && e->caxis.value < 8000) capture_pad_up(in);
      }
    }
    if (e->type != SDL_QUIT) {
      ui.redraw = !ui.capturing;
      return false;
    }
  }

  switch (e->type) {
    case SDL_QUIT: return true;
    case SDL_WINDOWEVENT:
      if (e->window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED || e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        const float s = display_scale();
        if (s != ui.scale) {
          ui.scale = s;
          make_fonts();
        }
      }
      return false;
    case SDL_DROPFILE: {
      char path[CONFIG_PATH_MAX], rel[CONFIG_PATH_MAX];
      local_text(e->drop.file, path, sizeof path);
      SDL_free(e->drop.file);
      const char* dot = strrchr(path, '.');
      if (dot && (config_same(dot, ".sfc") || config_same(dot, ".smc"))) {
        if (ui.editing) end_edit();
        relative_to_ini(path, rel, sizeof rel);
        set_path(S_ROM, rel);
        ui.status[0] = 0;
      } else {
        say("That is not a .sfc or .smc file.");
      }
      return false;
    }
    case SDL_MOUSEMOTION: {
      if (ui.dragging >= 0) { slide_to(ui.dragging, e->motion.x); return false; }
      const Hot* h = hit(e->motion.x, e->motion.y);
      if (ui.drop_row >= 0) {
        const int was = ui.drop_sel;
        if (h && h->part == P_OPTION) ui.drop_sel = h->index;
        ui.redraw = ui.drop_sel != was;
        return false;
      }
      const int was = ui.hover;
      ui.hover = h && h->row >= 0 ? h->row : -1;
      ui.redraw = ui.hover != was;
      return false;
    }
    case SDL_MOUSEBUTTONDOWN:
      if (e->button.button == SDL_BUTTON_LEFT) return click(e->button.x, e->button.y);
      return false;
    case SDL_MOUSEBUTTONUP:
      ui.dragging = -1;
      if (ui.browse_row >= 0 && e->button.button == SDL_BUTTON_LEFT) {
        const int row = ui.browse_row;
        ui.browse_row = -1;
        const Hot* h = hit(e->button.x, e->button.y);
        char path[CONFIG_PATH_MAX];
        if (h && h->part == P_BROWSE && h->row == row && browse(row_at(row)->id, path, sizeof path))
          set_path(row_at(row)->id, path);
        return false;
      }
      ui.redraw = false;
      return false;
    case SDL_MOUSEWHEEL:
      if (ui.drop_row >= 0) {
        ui.drop_first -= e->wheel.y * 3;  // `draw_drop_list` keeps it in range
        if (ui.drop_first < 0) ui.drop_first = 0;
        return false;
      }
      ui.scroll -= e->wheel.y * L(ROW_H) * 1.5f;
      clamp_scroll();
      return false;
    case SDL_TEXTINPUT:
      if (ui.editing) {
        char local[64];
        local_text(e->text.text, local, sizeof local);
        edit_insert(local);
      }
      return false;
    case SDL_CONTROLLERDEVICEADDED:
      note_pad(SDL_GameControllerOpen(e->cdevice.which));
      return false;
    case SDL_CONTROLLERDEVICEREMOVED: {
      SDL_GameController* gc = SDL_GameControllerFromInstanceID(e->cdevice.which);
      if (gc) SDL_GameControllerClose(gc);
      return false;
    }
    case SDL_CONTROLLERBUTTONDOWN: {
      note_pad(SDL_GameControllerFromInstanceID(e->cbutton.which));
      if (ui.editing) commit_edit();
      switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: hold(DIR_UP); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: hold(DIR_DOWN); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: hold(DIR_LEFT); break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: hold(DIR_RIGHT); break;
        case SDL_CONTROLLER_BUTTON_A: return activate(true);
        case SDL_CONTROLLER_BUTTON_B: drop_close(); break;
        case SDL_CONTROLLER_BUTTON_X:
          if (!ui.on_buttons) remove_binding(ui.focus, -1);
          break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: set_tab(ui.tab - 1); break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: set_tab(ui.tab + 1); break;
        case SDL_CONTROLLER_BUTTON_START: return launch();
        default: break;
      }
      return false;
    }
    case SDL_CONTROLLERBUTTONUP:
      if (e->cbutton.button >= SDL_CONTROLLER_BUTTON_DPAD_UP && e->cbutton.button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
        ui.held_dir = DIR_NONE;
      ui.redraw = false;
      return false;
    case SDL_CONTROLLERAXISMOTION: {
      // The left stick as a D-pad, with the same repeat. Only a move that
      // starts or ends a direction is drawn: `hold` moves the focus.
      ui.redraw = false;
      if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) ui.stick_x = e->caxis.value;
      else if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) ui.stick_y = e->caxis.value;
      else return false;
      const int ax = abs(ui.stick_x), ay = abs(ui.stick_y);
      int dir = DIR_NONE;
      if (ax > 20000 || ay > 20000)
        dir = ay >= ax ? (ui.stick_y < 0 ? DIR_UP : DIR_DOWN) : (ui.stick_x < 0 ? DIR_LEFT : DIR_RIGHT);
      else if (ax > 12000 || ay > 12000)
        dir = ui.held_dir;  // between the two thresholds: as it was
      if (dir != ui.held_dir) {
        if (dir) hold(dir);
        else ui.held_dir = DIR_NONE;
        ui.redraw = true;
      }
      return false;
    }
    case SDL_KEYDOWN: {
      const SDL_Keycode k = e->key.keysym.sym;
      const bool ctrl = (e->key.keysym.mod & KMOD_CTRL) != 0;
      const bool shift = (e->key.keysym.mod & KMOD_SHIFT) != 0;
      if (ui.editing) {
        const int len = (int)strlen(ui.edit);
        switch (k) {
          case SDLK_RETURN:
          case SDLK_KP_ENTER:
          case SDLK_TAB: commit_edit(); break;
          case SDLK_ESCAPE: end_edit(); break;
          case SDLK_LEFT: if (ui.caret > 0) ui.caret--; break;
          case SDLK_RIGHT: if (ui.caret < len) ui.caret++; break;
          case SDLK_HOME: ui.caret = 0; break;
          case SDLK_END: ui.caret = len; break;
          case SDLK_BACKSPACE:
            if (ui.caret > 0) { memmove(ui.edit + ui.caret - 1, ui.edit + ui.caret, (size_t)(len - ui.caret + 1)); ui.caret--; }
            break;
          case SDLK_DELETE:
            if (ui.caret < len) memmove(ui.edit + ui.caret, ui.edit + ui.caret + 1, (size_t)(len - ui.caret));
            break;
          case SDLK_v:
            if (ctrl && SDL_HasClipboardText()) {
              char* clip = SDL_GetClipboardText();
              char local[CONFIG_PATH_MAX];
              local_text(clip ? clip : "", local, sizeof local);
              SDL_free(clip);
              for (char* p = local; *p; p++)
                if (*p == '\r' || *p == '\n') *p = 0;
              edit_insert(local);
            }
            break;
          case SDLK_a:
            if (ctrl) { ui.edit[0] = 0; ui.caret = 0; }
            break;
          default: break;
        }
        return false;
      }
      if (ui.drop_row >= 0) {
        switch (k) {
          case SDLK_UP: drop_move(-1); break;
          case SDLK_DOWN: drop_move(1); break;
          case SDLK_PAGEUP: drop_move(-(DROP_SHOWN - 1)); break;
          case SDLK_PAGEDOWN: drop_move(DROP_SHOWN - 1); break;
          case SDLK_HOME: drop_move(-OPTIONS_MAX); break;
          case SDLK_END: drop_move(OPTIONS_MAX); break;
          case SDLK_RETURN:
          case SDLK_KP_ENTER:
          case SDLK_SPACE: drop_pick(); break;
          case SDLK_ESCAPE:
          case SDLK_TAB: drop_close(); break;
          default: break;
        }
        return false;
      }
      if (ctrl && k == SDLK_s) { save(); return false; }
      if ((ctrl && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) || k == SDLK_F5) return launch();
      switch (k) {
        case SDLK_UP: go(DIR_UP); break;
        case SDLK_DOWN: go(DIR_DOWN); break;
        case SDLK_LEFT: go(DIR_LEFT); break;
        case SDLK_RIGHT: go(DIR_RIGHT); break;
        case SDLK_TAB: set_tab(ui.tab + (shift ? -1 : 1)); break;
        case SDLK_PAGEUP: ui.scroll -= list_h() * 0.8f; clamp_scroll(); break;
        case SDLK_PAGEDOWN: ui.scroll += list_h() * 0.8f; clamp_scroll(); break;
        case SDLK_HOME: ui.on_buttons = false; ui.focus = first_row(); ui.scroll = 0; break;
        case SDLK_END: ui.on_buttons = true; break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE: return activate(false);
        case SDLK_BACKSPACE:
        case SDLK_DELETE:
          if (!ui.on_buttons) remove_binding(ui.focus, -1);
          break;
        case SDLK_ESCAPE: return press_button(UI_QUIT);
        default: break;
      }
      return false;
    }
    // Joystick events beside the controller's own, touchpad, key releases and
    // the rest: nothing here reads them.
    default: ui.redraw = false; return false;
  }
}

// Keys as if pressed: `Tab,Down,Ctrl+S`. True when the launcher is done.
static bool press_keys(const char* list) {
  char buf[1024];
  snprintf(buf, sizeof buf, "%s", list);
  for (char* tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN;
    for (;;) {
      if (!SDL_strncasecmp(tok, "Ctrl+", 5)) { e.key.keysym.mod |= KMOD_LCTRL; tok += 5; }
      else if (!SDL_strncasecmp(tok, "Shift+", 6)) { e.key.keysym.mod |= KMOD_LSHIFT; tok += 6; }
      else break;
    }
    e.key.keysym.sym = config_key(tok);
    if (e.key.keysym.sym == SDLK_UNKNOWN) { fprintf(stderr, "error: no key '%s'\n", tok); return true; }
    if (handle(&e)) return true;
  }
  return false;
}

// The launcher as it stands, drawn once into a PNG.
static bool screenshot(const char* path) {
  int w, h;
  SDL_GetRendererOutputSize(ui.ren, &w, &h);
  SDL_Texture* t = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
  uint8_t* px = (uint8_t*)malloc((size_t)w * h * 4);
  bool ok = t && px && SDL_SetRenderTarget(ui.ren, t) == 0;
  if (ok) {
    draw();
    ok = SDL_RenderReadPixels(ui.ren, NULL, SDL_PIXELFORMAT_ABGR8888, px, w * 4) == 0 &&
         stbi_write_png(path, w, h, 4, px, w * 4) != 0;
    SDL_SetRenderTarget(ui.ren, NULL);
  }
  if (t) SDL_DestroyTexture(t);
  free(px);
  if (!ok) fprintf(stderr, "error: cannot write '%s'\n", path);
  return ok;
}

int main(int argc, char** argv) {
  const char* asked = NULL;
  const char* keys = NULL;
  const char* shot = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--config") && i + 1 < argc) asked = argv[++i];
    else if (!strcmp(argv[i], "--press") && i + 1 < argc) keys = argv[++i];
    else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
    else {
      fprintf(stderr, "usage: %s [--config <file>] [--press <keys>] [--screenshot <file.png>]\n", argv[0]);
      return 2;
    }
  }
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  // SDL drops the click that brings the window forward; a click on a row or
  // a button behind another window should do what it says the first time.
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "error: %s\n", SDL_GetError());
    return 1;
  }
  find_base();
  build_rows();
  load(asked);
  ui.hover = -1;
  ui.dragging = -1;
  ui.browse_row = -1;
  ui.drop_row = -1;
  ui.button = UI_PLAY;
  ui.focus = first_row();

  ui.ttf[0] = load_font(false);
  ui.ttf[1] = load_font(true);
  if (!ui.ttf[0]) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Zombies Ate My Neighbors",
                             "The launcher could not find a font to draw with.", NULL);
    return 1;
  }

  // Sized for the display it opens on: the layout at that display's scale,
  // or as much of the display as there is.
  float dpi = 96;
  if (SDL_GetDisplayDPI(0, &dpi, NULL, NULL) != 0 || dpi <= 0) dpi = 96;
  float s = dpi / 96.0f;
  s = s < 1 ? 1 : s > 4 ? 4 : s;
  SDL_Rect usable = {0, 0, 1280, 720};
  SDL_GetDisplayUsableBounds(0, &usable);
  int ww = (int)(980 * s), wh = (int)(780 * s);
  if (ww > usable.w * 9 / 10) ww = usable.w * 9 / 10;
  if (wh > usable.h * 9 / 10) wh = usable.h * 9 / 10;
  ui.win = SDL_CreateWindow("Zombies Ate My Neighbors", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
  if (!ui.win) { fprintf(stderr, "error: %s\n", SDL_GetError()); return 1; }
  SDL_SetWindowMinimumSize(ui.win, (int)(640 * s) < usable.w ? (int)(640 * s) : usable.w,
                           (int)(480 * s) < usable.h ? (int)(480 * s) : usable.h);
  ui.ren = SDL_CreateRenderer(ui.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ui.ren) ui.ren = SDL_CreateRenderer(ui.win, -1, SDL_RENDERER_SOFTWARE);
  if (!ui.ren) { fprintf(stderr, "error: %s\n", SDL_GetError()); return 1; }
  SDL_SetRenderDrawBlendMode(ui.ren, SDL_BLENDMODE_BLEND);
  ui.scale = display_scale();
  if (!make_fonts()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Zombies Ate My Neighbors",
                             "The launcher could not make its font.", ui.win);
    return 1;
  }
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
  SDL_StopTextInput();
  draw();  // where things are, which a click or a slide reads back
  bool done = keys && press_keys(keys);
  if (shot) {
    const bool ok = screenshot(shot);
    SDL_Quit();
    return ok ? 0 : 1;
  }
  SDL_RenderPresent(ui.ren);
  if (!game) SDL_ShowWindow(ui.win);

  while (!done) {
    SDL_Event e;
    if (game) {
      // What comes while the game runs is dropped, so that a button pressed
      // in the game is not one pressed here.
      if (SDL_WaitEventTimeout(&e, 250)) {
        do done |= e.type == SDL_QUIT; while (SDL_PollEvent(&e));
      }
      int code;
      if (!done && game_closed(&code)) {
        if (code) say("The game stopped with exit code %d.", code);
        draw();
        SDL_RenderPresent(ui.ren);
        SDL_ShowWindow(ui.win);
        SDL_RaiseWindow(ui.win);
      }
      continue;
    }
    // Asleep until something happens, but awake for a countdown or a held
    // direction, which happen without an event.
    // Drawn only when something on screen may have changed.
    const int wait = ui.held_dir ? 16 : ui.capturing ? 100 : 1000;
    bool redraw = false;
    if (SDL_WaitEventTimeout(&e, wait)) {
      done = handle(&e);
      redraw |= ui.redraw;
      while (!done && !game && SDL_PollEvent(&e)) {
        done = handle(&e);
        redraw |= ui.redraw;
      }
    }
    const Uint32 now = SDL_GetTicks();
    if (ui.capturing && (Sint32)(now - ui.capture_end) >= 0) {
      ui.capturing = false;
      redraw = true;
    }
    if (ui.capturing && capture_left() != ui.capture_shown) redraw = true;
    if (ui.held_dir && (Sint32)(now - ui.held_next) >= 0) {
      go(ui.held_dir);
      ui.held_next = now + 70;
      redraw = true;
    }
    if (!done && redraw) {
      draw();
      SDL_RenderPresent(ui.ren);
    }
  }

  font_free(&ui.body);
  font_free(&ui.small);
  font_free(&ui.bold);
  font_free(&ui.title);
  if (ui.logo) SDL_DestroyTexture(ui.logo);
  free(ui.ttf[0]);
  free(ui.ttf[1]);
  SDL_DestroyRenderer(ui.ren);
  SDL_DestroyWindow(ui.win);
  SDL_Quit();
  return 0;
}
