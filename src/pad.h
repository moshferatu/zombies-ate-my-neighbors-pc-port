// Game controllers: any pad SDL can be made to understand, driving either SNES
// port.
//
// The frontend has read the keyboard since Phase 0b, which was fine for testing
// and is not how anyone plays this game. This is the other input path. It is a
// header of static inline functions, in the same shape and for the same reason
// as `src/present.h`: the part of it that is arithmetic — a stick position
// becoming a D-pad, a deadzone with hysteresis, the button table — is checked by
// `tools/test_pad.c` with no device and no window, and code living privately
// inside `main_sdl.c` would be code nothing tested.
//
// ## Polled, not event-driven
//
// The keyboard path sets the core's buttons from `SDL_KEYDOWN`/`KEYUP` as they
// arrive. That works for a keyboard and is the wrong shape for a pad, because a
// pad can be unplugged mid-press: the KEYUP that would have released the button
// never comes, and the player runs into a wall until they plug it back in.
//
// So `pad_poll` reads the whole state of every open pad each frame and returns
// what each port should hold, which the frontend ORs with the keyboard and
// pushes once. A pad that has gone away contributes nothing by construction —
// there is no release to miss — and the same call sites that made
// `movie_apply` work for two ports work here. It also means keyboard and pad
// are simultaneous rather than exclusive, so one player on each is a session
// that runs without anything special being done about it.
//
// ## What the buttons are
//
// Positional, which is what every SNES emulator does and what the pad's own
// labels imply. SDL's `A/B/X/Y` are the Xbox *positions* whatever the device
// prints on them, and the SNES face layout is the same diamond, so:
//
//        X                    Y (top)
//      Y   A       <-       X       B (right)
//        B                    A (bottom)
//
// reads as: bottom->B, right->A, left->Y, top->X. On the DualSense that is
// Cross->B, Circle->A, Square->Y, Triangle->X.
//
// Worth knowing before playing rather than after: **`Y` is this game's fire
// button** and it is held, not tapped — `src/port/player.h` has it from the ROM,
// `PSN_BTN_FIRE 0x4000`, gating the empty-weapon check every frame. `B` cycles
// weapons, `A` cycles items, `X` uses one, and `L` and `R` both bring up the
// survivor radar.
// So the button under the left thumb-position is the one held down all game.
// That is the standard mapping's doing and not a choice made here; the point of
// saying it is that "fire is Square" is surprising for about ten seconds and
// then is not.
//
// ## The four things in the top corners
//
// They were L and R, since the SNES's L and R *are* shoulder buttons -- four
// inputs for one radar. They are now the thing a modern pad has them for:
// **R1 is the next item and L1 the one before, R2 the next weapon and L2 the
// one before**. Backwards is not something the cartridge can do, and neither is
// changing weapon while firing, so these are not SNES buttons and do not reach
// the port as buttons: `pad_poll` reports the presses, per pad, in
// `PadSet.cycle_pressed`, and the frontend hands them to
// `player_cycle_request` (`src/port/player.h`).
//
// The radar moved to the **touchpad's click**, which is where a PlayStation
// game keeps its map, and to **L3** for a pad that has no touchpad. The radar
// is a picture over the game that goes away again, so the argument below
// against binding the touchpad -- a palm, and a session lost to it -- is not
// one against this.
//
// ## Quitting
//
// A pad has no Esc, so **Start and Select pressed together** quit, the moment
// both are down. On a DualSense that is Options and Share, which SDL reports as
// `start` and `back` like everything else does, so the one binding covers both
// spellings.
//
// It is the quit hotkey's default and not a gesture of its own: a hotkey can be
// two inputs at once (`pad_chord`), written `start+select`, and one so bound
// can be rebound or cleared like any other.
//
// Pressed, not held. It was a one-second hold with the picture fading to black,
// on the argument that both are buttons the game uses and a thumb bridging them
// would lose the session; in play the wait read as a quit that had not
// happened, and the pair is not one a thumb lands on by accident.
//
// While a chord is down neither of its inputs reaches the game, so the frame
// it quits on does not open the map behind it.
//
// ## Deadzone
//
// Measured, on the pad this was written for. A DualSense Edge left untouched for
// ten seconds — 600 samples, the frame rate the game runs at — peaks 908 counts
// from centre on the left stick and 654 on the right, 2.8% and 2.0% of full
// scale. So the gate is not about noise: at 8,000 counts (24%) it is nine times
// the measured drift, and the number is chosen for how an eight-way game feels
// rather than for what the hardware does. It is SDL's own default gate to within
// 2%, which is the other reason to keep it.
//
// It is released at 6,000 rather than 8,000. Without that gap a stick held near
// the edge of the gate chatters between "centred" and "held", which on this game
// is a player who stutters instead of walking.
//
// ## Eight ways, and only eight
//
// The SNES D-pad has eight states and a stick has a circle of them, so the
// circle is cut into octants: an axis counts once it is past sin(22.5 degrees)
// of the stick's own magnitude. That is exact octant snapping — a boundary at
// 22.5 degrees, a 45-degree band per direction, diagonals available across a
// full eighth of the circle rather than at one point — and it is done in
// integers, squared on both sides, so there is no sqrt and no rounding to argue
// about. Comparing each axis against a *fixed* threshold instead is the obvious
// alternative and it is worse: it makes the diagonals the easiest thing to hit
// and the cardinals nearly unreachable, because a stick pushed fully right sits
// at 32,767 and one pushed diagonally sits at 23,170 on both axes, which clears
// any fixed gate low enough to be usable.
//
// ## The other stick
//
// A SNES pad has one stick's worth of directions and a modern pad has two, so
// `pad_aim` reads the right one through the identical snap and returns the
// identical eight-way bits. It is deliberately not part of `pad_poll`: there is
// no SNES button it could be, so there is nothing for `pad_poll` to say about
// it. Twin-stick aiming is what gives it a meaning — see `src/twinstick.h` —
// and it is on unless `--no-twin-stick` says otherwise, in which case nothing
// calls this and the stick does nothing, which is what the stock game does with
// a stick it cannot read.
//
// ## Bindings
//
// Everything above is the *default*, and `PadMap` is what is actually read: for
// each SNES button a short list of pad inputs, any of which holds it; the same
// for a handful of frontend actions ("hot" inputs -- quick save on a paddle,
// say), which are bound to nothing unless the player binds them, for the reason
// given at `pad_button`; which stick steers and which aims; and the deadzone.
// `pad_map_default` builds the table from `pad_button` and the four selections,
// so a frontend that never touches `PadSet.map` plays as the header comment
// says. `src/config.h` fills it from the player's `zamn.ini`.
//
// A trigger is an input like any other in that table, read through the same
// hysteresis it always was. A hot input is reported on the press and not while
// held: `PadSet.hot_pressed` collects the presses and the frontend takes them.
// The selections are reported the same way, by port: `PadSet.cycle_pressed`.
//
// ## Devices
//
// SDL 2.30 knows several thousand controllers by GUID and this program adds
// three things on top:
//
//   * `gamecontrollerdb.txt`, if one is sitting next to the executable or in the
//     working directory. The community database, and the escape hatch for a pad
//     SDL maps wrongly.
//   * `SDL_GAMECONTROLLERCONFIG`, which SDL reads by itself; nothing here has to
//     help, but it is worth knowing it works.
//   * a guessed mapping for anything still unrecognised — see `pad_teach`. The
//     alternative for those devices is not a worse mapping, it is no input at
//     all, so a guess that gets the sticks and the hat right and may scramble
//     the face buttons is strictly the better failure.

#ifndef ZAMN_PAD_H
#define ZAMN_PAD_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "analysis/movie.h"

// One pad per SNES port. A third pad plugged in is counted and ignored, which
// is better than silently shadowing player 2.
#define PAD_MAX MOVIE_PORTS

// Past this from centre the stick is held; below `PAD_LEAVE` it is centred
// again. See the header comment: 8,000 is nine times the measured resting drift
// of a real pad, and the gap between the two numbers is what stops a chatter.
#define PAD_ENTER 8000
#define PAD_LEAVE 6000
// Triggers are one-sided, 0..32767, and are only ever buttons here — so the gate
// can be the same pair of numbers without anyone having to think about it.
#define PAD_TRIG_ENTER 8000
#define PAD_TRIG_LEAVE 6000

// sin(22.5 degrees), as a fraction. The octant boundary.
#define PAD_OCT_NUM 3827
#define PAD_OCT_DEN 10000

// A pad input, as the binding table names one: an `SDL_GameControllerButton`,
// or one of the two triggers, which SDL has as axes and a player has as buttons,
// or two of those held together (`pad_chord`).
#define PAD_IN_NONE (-1)
#define PAD_IN_LTRIGGER 100
#define PAD_IN_RTRIGGER 101
#define PAD_IN_CHORD 0x4000

// Two inputs at once, as one input: seven bits each under `PAD_IN_CHORD`, the
// larger first, so that `select+start` and `start+select` are the same one.
static inline int pad_chord(int a, int b) {
  if (a < b) { const int t = a; a = b; b = t; }
  return PAD_IN_CHORD | a << 7 | b;
}
static inline bool pad_is_chord(int in) { return in >= PAD_IN_CHORD; }
static inline int pad_chord_first(int in) { return in >> 7 & 127; }
static inline int pad_chord_second(int in) { return in & 127; }

// A single input as a bit, for saying which a held chord has taken.
static inline uint32_t pad_input_bit(int in) {
  if (in == PAD_IN_LTRIGGER) return 1u << 30;
  if (in == PAD_IN_RTRIGGER) return 1u << 31;
  return in >= 0 && in < 30 ? 1u << in : 0;
}
// Inputs per SNES button or per action, and how many actions a frontend may
// have.
#define PAD_BIND_MAX 4
#define PAD_HOT_MAX 16
// The selections, which are a pad's and not the SNES's: see "The four things
// in the top corners". Bit 0 of the number is the direction and bit 1 the list.
enum {
  PAD_CYCLE_NEXT_WEAPON,
  PAD_CYCLE_PREV_WEAPON,
  PAD_CYCLE_NEXT_ITEM,
  PAD_CYCLE_PREV_ITEM,
  PAD_CYCLE_COUNT
};
enum { PAD_STICK_NONE, PAD_STICK_LEFT, PAD_STICK_RIGHT };

// --- the arithmetic, which is the part `tools/test_pad.c` checks -------------

// A stick position as D-pad bits (`1 << BTN_UP` and friends), with `*live`
// carrying the hysteresis across calls: pass the same bool back in every frame
// and this reads the deadzone as 6,000 while the stick is out and 8,000 while it
// is home.
//
// All of it in 64-bit integers because it does not fit in 32: the squared
// magnitude of a stick at full deflection on both axes is 2,147,352,578, which
// is thirty-two bits with 1,069 to spare, and the octant comparison then
// multiplies that by 14,645,929.
static inline uint16_t pad_stick_gated(int x, int y, bool* live, int enter, int leave) {
  const long long m2 = (long long)x * x + (long long)y * y;
  const long long gate = *live ? leave : enter;
  if (m2 <= gate * gate) { *live = false; return 0; }
  *live = true;
  const long long ax = (long long)(x < 0 ? -x : x) * PAD_OCT_DEN;
  const long long ay = (long long)(y < 0 ? -y : y) * PAD_OCT_DEN;
  // |axis| > sin(22.5) * |v|, both sides squared so neither needs a root.
  const long long t2 = (long long)PAD_OCT_NUM * PAD_OCT_NUM * m2;
  uint16_t bits = 0;
  if (ax * ax > t2) bits |= (uint16_t)(1u << (x > 0 ? BTN_RIGHT : BTN_LEFT));
  if (ay * ay > t2) bits |= (uint16_t)(1u << (y > 0 ? BTN_DOWN : BTN_UP));
  return bits;
}

// ...at the gates the header comment measures, which are the default.
static inline uint16_t pad_stick(int x, int y, bool* live) {
  return pad_stick_gated(x, y, live, PAD_ENTER, PAD_LEAVE);
}

// The same hysteresis on one axis, for the triggers.
static inline bool pad_trigger(int v, bool* live) {
  *live = v > (*live ? PAD_TRIG_LEAVE : PAD_TRIG_ENTER);
  return *live;
}

// A pad button as an SNES button index, or -1 for one this game has no use for.
// The face four are positional; see the diagram in the header comment.
static inline int pad_button(SDL_GameControllerButton b) {
  switch (b) {
    case SDL_CONTROLLER_BUTTON_A:             return BTN_B;
    case SDL_CONTROLLER_BUTTON_B:             return BTN_A;
    case SDL_CONTROLLER_BUTTON_X:             return BTN_Y;
    case SDL_CONTROLLER_BUTTON_Y:             return BTN_X;
    // The radar, twice: see "The four things in the top corners".
    case SDL_CONTROLLER_BUTTON_TOUCHPAD:      return BTN_L;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return BTN_R;
    case SDL_CONTROLLER_BUTTON_START:         return BTN_START;
    case SDL_CONTROLLER_BUTTON_BACK:          return BTN_SELECT;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return BTN_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return BTN_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return BTN_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return BTN_RIGHT;
    // Guide, the Edge's paddles and misc buttons. Deliberately not bound, and
    // not made into hotkeys either: a single button that quit the game or
    // toggled substitution would fire the first time somebody rested a palm on
    // one. The quit hotkey's default — see "Quitting" — is two buttons at
    // once, which a palm resting on one cannot do. The shoulders are not
    // here because they are not SNES buttons any more: `pad_map_default`.
    default:                                  return -1;
  }
}

// --- the bindings -----------------------------------------------------------

// What is read, for every pad alike: see "Bindings" in the header comment.
typedef struct {
  int16_t game[12][PAD_BIND_MAX];          // by `BTN_*`; `PAD_IN_NONE` ends a list
  int16_t hot[PAD_HOT_MAX][PAD_BIND_MAX];  // by the frontend's action number
  int16_t cycle[PAD_CYCLE_COUNT][PAD_BIND_MAX];  // by `PAD_CYCLE_*`
  int move_stick, aim_stick;               // `PAD_STICK_*`
  int enter, leave;                        // the sticks' deadzone, in counts
} PadMap;

// Add an input to a list. False when the list is full or has it already.
static inline bool pad_map_add(int16_t list[PAD_BIND_MAX], int in) {
  for (int i = 0; i < PAD_BIND_MAX; i++) {
    if (list[i] == in) return false;
    if (list[i] == PAD_IN_NONE) { list[i] = (int16_t)in; return true; }
  }
  return false;
}

static inline void pad_map_clear(int16_t list[PAD_BIND_MAX]) {
  for (int i = 0; i < PAD_BIND_MAX; i++) list[i] = PAD_IN_NONE;
}

// Whether an input is one of the selections'. Such an input is not also a SNES
// button, whatever the table says: a thing in two places does the first thing
// only, as a key does, and a `zamn.ini` written when the shoulders were L and R
// still says they are.
static inline bool pad_map_cycles(const PadMap* m, int in) {
  for (int c = 0; c < PAD_CYCLE_COUNT; c++)
    for (int i = 0; i < PAD_BIND_MAX && m->cycle[c][i] != PAD_IN_NONE; i++)
      if (m->cycle[c][i] == in) return true;
  return false;
}

// The table the header comment describes: `pad_button`, the shoulders and the
// triggers selecting, the left stick steering and the right one aiming, no
// action bound.
static inline void pad_map_default(PadMap* m) {
  for (int b = 0; b < 12; b++) pad_map_clear(m->game[b]);
  for (int a = 0; a < PAD_HOT_MAX; a++) pad_map_clear(m->hot[a]);
  for (int c = 0; c < PAD_CYCLE_COUNT; c++) pad_map_clear(m->cycle[c]);
  for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
    const int snes = pad_button((SDL_GameControllerButton)b);
    if (snes >= 0) pad_map_add(m->game[snes], b);
  }
  pad_map_add(m->cycle[PAD_CYCLE_NEXT_ITEM], SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
  pad_map_add(m->cycle[PAD_CYCLE_PREV_ITEM], SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
  pad_map_add(m->cycle[PAD_CYCLE_NEXT_WEAPON], PAD_IN_RTRIGGER);
  pad_map_add(m->cycle[PAD_CYCLE_PREV_WEAPON], PAD_IN_LTRIGGER);
  m->move_stick = PAD_STICK_LEFT;
  m->aim_stick = PAD_STICK_RIGHT;
  m->enter = PAD_ENTER;
  m->leave = PAD_LEAVE;
}

// --- the devices ------------------------------------------------------------

typedef struct {
  SDL_GameController* gc;
  SDL_JoystickID id;
  bool stick;            // the steering stick was out of the deadzone last frame
  bool aim;              // ...and the aiming one, which no SNES pad has
  bool trig_l, trig_r;   // ...and the triggers, same hysteresis
  uint32_t hot_down;     // the actions whose inputs were down last frame
  uint8_t cycle_down;    // ...and the selections, a bit per `PAD_CYCLE_*`
  char name[64];
} Pad;

typedef struct {
  Pad pad[PAD_MAX];
  bool ready;     // the subsystem came up
  PadMap map;     // what is read; `pad_init` makes it the default
  // Actions pressed since the frontend last looked, a bit each. Only ever
  // added to here: whoever acts on them clears them.
  uint32_t hot_pressed;
  // The same for the selections, which are a player's and so are kept by port:
  // a bit per `PAD_CYCLE_*`.
  uint8_t cycle_pressed[PAD_MAX];
} PadSet;

static inline int pad_count(const PadSet* s) {
  int n = 0;
  for (int i = 0; i < PAD_MAX; i++) n += s->pad[i].gc ? 1 : 0;
  return n;
}

// Pads attached with nowhere to put them. Asked of SDL rather than counted as
// they are refused, because a device present at startup is offered twice — once
// by the enumeration in `pad_init` and once by the event SDL queued for it — and
// a counter would say two pads were ignored when one was.
static inline int pad_ignored(const PadSet* s) {
  if (!s->ready) return 0;
  int n = 0;
  for (int i = 0; i < SDL_NumJoysticks(); i++)
    n += SDL_IsGameController(i) ? 1 : 0;
  n -= pad_count(s);
  return n > 0 ? n : 0;
}

// Teach SDL a device it does not recognise, by guessing the layout from what the
// HID report says it has.
//
// This is the shape almost every plain USB pad reports — four face buttons, two
// shoulders, back and start, two stick clicks, two sticks on axes 0..3, and the
// D-pad as a hat — and it is what the entries in the community database mostly
// say, which is the argument for guessing it. Only the fields the device
// actually has are claimed, so a six-button pad does not get told it has a
// Start button at index 7.
//
// It can be wrong about which face button is which. It cannot be wrong about the
// stick or the hat, and those are the ones that make a game playable rather than
// merely responsive.
static inline bool pad_teach(int index) {
  if (SDL_IsGameController(index)) return true;
  SDL_Joystick* j = SDL_JoystickOpen(index);
  if (!j) return false;
  const int axes = SDL_JoystickNumAxes(j);
  const int buttons = SDL_JoystickNumButtons(j);
  const int hats = SDL_JoystickNumHats(j);
  char guid[33];
  SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), guid, sizeof guid);
  const char* jname = SDL_JoystickName(j);
  char name[64];
  snprintf(name, sizeof name, "%s", jname ? jname : "Generic Controller");
  SDL_JoystickClose(j);
  // A comma in the name would end the field and corrupt every entry after it.
  for (char* p = name; *p; p++)
    if (*p == ',') *p = ' ';

  // A device with no stick and no hat has nothing that could steer, and a
  // mapping for it would only turn "no controller" into "a controller that does
  // not work", which is harder to diagnose and not more useful.
  if (axes < 2 && hats < 1) return false;

  char map[640];
  int n = snprintf(map, sizeof map, "%s,%s,", guid, name);
  static const char* face[4] = {"a:b0,", "b:b1,", "x:b2,", "y:b3,"};
  for (int i = 0; i < 4 && i < buttons; i++)
    n += snprintf(map + n, sizeof map - (size_t)n, "%s", face[i]);
  if (buttons >= 6)
    n += snprintf(map + n, sizeof map - (size_t)n,
                  "leftshoulder:b4,rightshoulder:b5,");
  if (buttons >= 8)
    n += snprintf(map + n, sizeof map - (size_t)n, "back:b6,start:b7,");
  if (buttons >= 10)
    n += snprintf(map + n, sizeof map - (size_t)n,
                  "leftstick:b8,rightstick:b9,");
  if (axes >= 2) n += snprintf(map + n, sizeof map - (size_t)n, "leftx:a0,lefty:a1,");
  if (axes >= 4) n += snprintf(map + n, sizeof map - (size_t)n, "rightx:a2,righty:a3,");
  if (hats >= 1)
    n += snprintf(map + n, sizeof map - (size_t)n,
                  "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,");
  snprintf(map + n, sizeof map - (size_t)n, "platform:%s,", SDL_GetPlatform());

  if (SDL_GameControllerAddMapping(map) < 0) return false;
  return SDL_IsGameController(index) != SDL_FALSE;
}

// Open device `index` into the first free port. Idempotent by instance id, which
// matters because a device that is present at startup is both enumerated
// directly and announced by a queued event, and both paths land here.
static inline bool pad_open(PadSet* s, int index) {
  if (!pad_teach(index)) return false;
  const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
  for (int i = 0; i < PAD_MAX; i++)
    if (s->pad[i].gc && s->pad[i].id == id) return false;
  int slot = -1;
  for (int i = 0; i < PAD_MAX && slot < 0; i++)
    if (!s->pad[i].gc) slot = i;
  // Both SNES ports are taken. Silently, because this is reached again for every
  // device every time one is offered, and `pad_ignored` is where it gets said.
  if (slot < 0) return false;
  SDL_GameController* gc = SDL_GameControllerOpen(index);
  if (!gc) return false;
  Pad* p = &s->pad[slot];
  memset(p, 0, sizeof *p);
  p->gc = gc;
  p->id = id;
  const char* nm = SDL_GameControllerName(gc);
  snprintf(p->name, sizeof p->name, "%s", nm ? nm : "Controller");
  printf("Controller: %s on port %d\n", p->name, slot + 1);
  fflush(stdout);
  return true;
}

static inline void pad_close(PadSet* s, SDL_JoystickID id) {
  for (int i = 0; i < PAD_MAX; i++) {
    Pad* p = &s->pad[i];
    if (!p->gc || p->id != id) continue;
    printf("Controller: %s removed from port %d\n", p->name, i + 1);
    fflush(stdout);
    SDL_GameControllerClose(p->gc);
    memset(p, 0, sizeof *p);
  }
}

// Bring the subsystem up and take whatever is already plugged in. Returns false
// if there is no joystick support at all, which is not a reason to refuse to
// run — it is a reason to say so and read the keyboard.
static inline bool pad_init(PadSet* s) {
  memset(s, 0, sizeof *s);
  pad_map_default(&s->map);
  if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Controllers: unavailable (%s)\n", SDL_GetError());
    return false;
  }
  s->ready = true;
  // The community database, if someone has put one here. `AddMappingsFromFile`
  // returns the number of entries read, or -1 for a file that is not there —
  // which is the ordinary case and not worth a word.
  int added = SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
  char* base = SDL_GetBasePath();
  if (base) {
    char path[1024];
    snprintf(path, sizeof path, "%sgamecontrollerdb.txt", base);
    const int more = SDL_GameControllerAddMappingsFromFile(path);
    if (more > 0) added = added > 0 ? added + more : more;
    SDL_free(base);
  }
  if (added > 0) printf("Controllers: %d extra mappings loaded\n", added);
  for (int i = 0; i < SDL_NumJoysticks(); i++) pad_open(s, i);
  return true;
}

static inline void pad_free(PadSet* s) {
  for (int i = 0; i < PAD_MAX; i++)
    if (s->pad[i].gc) SDL_GameControllerClose(s->pad[i].gc);
  memset(s, 0, sizeof *s);
}

// Hot-plug. `SDL_JOYDEVICEADDED` carries a device index and arrives for every
// device including ones SDL already understands; `SDL_CONTROLLERDEVICEREMOVED`
// carries an instance id. The added case is taken from the joystick event rather
// than the controller one because that is the only one an unrecognised device
// produces, and `pad_teach` has to run before there is a controller to announce.
static inline void pad_event(PadSet* s, const SDL_Event* e) {
  if (!s->ready) return;
  if (e->type == SDL_JOYDEVICEADDED) pad_open(s, e->jdevice.which);
  else if (e->type == SDL_CONTROLLERDEVICEREMOVED) pad_close(s, e->cdevice.which);
}

// Whether an input is down. The triggers are read from the pad's own
// hysteresis bits, which `pad_poll` has just brought up to date.
static inline bool pad_input_down(const Pad* p, int in) {
  if (pad_is_chord(in))
    return pad_input_down(p, pad_chord_first(in)) && pad_input_down(p, pad_chord_second(in));
  if (in == PAD_IN_LTRIGGER) return p->trig_l;
  if (in == PAD_IN_RTRIGGER) return p->trig_r;
  return in >= 0 && in < SDL_CONTROLLER_BUTTON_MAX &&
         SDL_GameControllerGetButton(p->gc, (SDL_GameControllerButton)in);
}

// Whether any input of a list is down, leaving out the single inputs in
// `taken` (`pad_input_bit`).
static inline bool pad_list_down_but(const Pad* p, const int16_t list[PAD_BIND_MAX], uint32_t taken) {
  for (int i = 0; i < PAD_BIND_MAX && list[i] != PAD_IN_NONE; i++)
    if (!(pad_input_bit(list[i]) & taken) && pad_input_down(p, list[i])) return true;
  return false;
}

static inline bool pad_list_down(const Pad* p, const int16_t list[PAD_BIND_MAX]) {
  return pad_list_down_but(p, list, 0);
}

// A SNES button's list, less whatever of it selects (`pad_map_cycles`) and
// whatever a held chord has taken.
static inline bool pad_game_down(const PadMap* m, const Pad* p, int b, uint32_t taken) {
  int16_t list[PAD_BIND_MAX];
  int n = 0;
  for (int i = 0; i < PAD_BIND_MAX && m->game[b][i] != PAD_IN_NONE; i++)
    if (!pad_map_cycles(m, m->game[b][i])) list[n++] = m->game[b][i];
  if (n < PAD_BIND_MAX) list[n] = PAD_IN_NONE;
  return pad_list_down_but(p, list, taken);
}

// One stick of a pad as eight-way bits, at the map's deadzone; nothing for
// `PAD_STICK_NONE`.
static inline uint16_t pad_stick_of(const PadMap* m, Pad* p, int which, bool* live) {
  if (which != PAD_STICK_LEFT && which != PAD_STICK_RIGHT) { *live = false; return 0; }
  const bool left = which == PAD_STICK_LEFT;
  return pad_stick_gated(
      SDL_GameControllerGetAxis(p->gc, left ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_RIGHTX),
      SDL_GameControllerGetAxis(p->gc, left ? SDL_CONTROLLER_AXIS_LEFTY : SDL_CONTROLLER_AXIS_RIGHTY),
      live, m->enter, m->leave);
}

// What every pad is holding, as one 12-bit SNES mask per port -- and, on the
// way past, which of the frontend's actions were pressed (`hot_pressed`).
static inline void pad_poll(PadSet* s, uint16_t held[PAD_MAX]) {
  for (int i = 0; i < PAD_MAX; i++) held[i] = 0;
  if (!s->ready) return;
  SDL_GameControllerUpdate();
  for (int i = 0; i < PAD_MAX; i++) {
    Pad* p = &s->pad[i];
    if (!p->gc) continue;
    pad_trigger(SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT), &p->trig_l);
    pad_trigger(SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), &p->trig_r);
    // The actions first, because a chord among them that is held takes its two
    // inputs from the game and the selections for as long as it is.
    uint32_t hot = 0, taken = 0;
    for (int a = 0; a < PAD_HOT_MAX; a++)
      for (int k = 0; k < PAD_BIND_MAX && s->map.hot[a][k] != PAD_IN_NONE; k++) {
        const int in = s->map.hot[a][k];
        if (!pad_input_down(p, in)) continue;
        hot |= 1u << a;
        if (pad_is_chord(in))
          taken |= pad_input_bit(pad_chord_first(in)) | pad_input_bit(pad_chord_second(in));
      }
    s->hot_pressed |= hot & ~p->hot_down;
    p->hot_down = hot;
    uint16_t m = 0;
    for (int b = 0; b < 12; b++)
      if (pad_game_down(&s->map, p, b, taken)) m |= (uint16_t)(1u << b);
    m |= pad_stick_of(&s->map, p, s->map.move_stick, &p->stick);
    held[i] = m;
    uint8_t cyc = 0;
    for (int c = 0; c < PAD_CYCLE_COUNT; c++)
      if (pad_list_down_but(p, s->map.cycle[c], taken)) cyc |= (uint8_t)(1u << c);
    s->cycle_pressed[i] |= (uint8_t)(cyc & ~p->cycle_down);
    p->cycle_down = cyc;
  }
}

// The right stick, as the same eight-way bits the left one produces.
//
// Separate from `pad_poll` rather than folded into it, because `pad_poll`'s
// answer is *what the SNES port holds* and a second stick is not something a
// SNES port can hold. What it means is a decision for whoever asked — see
// `src/twinstick.h`, where it becomes an aim direction and a fire button — and
// this end of it is only the same octant snapping and the same hysteresis on the
// other pair of axes.
//
// A port with no pad reads centred, so a caller can walk both without checking.
static inline void pad_aim(PadSet* s, uint16_t aim[PAD_MAX]) {
  for (int i = 0; i < PAD_MAX; i++) aim[i] = 0;
  if (!s->ready) return;
  SDL_GameControllerUpdate();
  for (int i = 0; i < PAD_MAX; i++) {
    Pad* p = &s->pad[i];
    if (!p->gc) continue;
    aim[i] = pad_stick_of(&s->map, p, s->map.aim_stick, &p->aim);
  }
}

#endif
