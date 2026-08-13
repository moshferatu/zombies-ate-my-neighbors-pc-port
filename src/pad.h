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
// weapons, `A` cycles items, `X` uses one, `L`/`R` are an edge-triggered pair.
// So the button under the left thumb-position is the one held down all game.
// That is the standard mapping's doing and not a choice made here; the point of
// saying it is that "fire is Square" is surprising for about ten seconds and
// then is not.
//
// Both shoulders and both triggers reach L/R, since the SNES's L and R *are*
// shoulder buttons and a modern pad has four things in that corner.
//
// ## Quitting
//
// A pad has no Esc, so **Start and Select held together for one second** quits.
// On a DualSense that is Options and Share, which SDL reports as `start` and
// `back` like everything else does, so the one binding covers both spellings.
//
// Held, rather than pressed, and that is the whole of the design. These are two
// buttons the game itself uses — Start opens the map — so an instant quit on the
// pair is a session lost to a thumb that bridged them, and this game has
// passwords rather than save states. A second is long enough that it cannot
// happen by accident and short enough not to feel like a menu.
//
// It needs to be visible while it is happening, or a second of nothing reads as
// a broken button rather than as a countdown, so the frontend dims the picture
// in proportion: hold and the screen fades to black, let go and it comes
// straight back. That is `pad_quit`'s return value, which is a frame count for
// exactly that reason.
//
// While the chord is held neither button reaches the game. Pressing them is a
// gesture aimed at the frontend, and an abandoned quit that leaves the map open
// behind it would be the frontend making a mess and walking away.
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
// Triggers are one-sided, 0..32767, and are only ever L/R here — so the gate can
// be the same pair of numbers without anyone having to think about it.
#define PAD_TRIG_ENTER 8000
#define PAD_TRIG_LEAVE 6000

// sin(22.5 degrees), as a fraction. The octant boundary.
#define PAD_OCT_NUM 3827
#define PAD_OCT_DEN 10000

// The quit gesture, and how long it has to be held. Sixty frames is one second
// at the rate this game runs, and the frontend measures it in frames because
// that is what it dims the picture by.
#define PAD_QUIT_MASK ((uint16_t)((1u << BTN_START) | (1u << BTN_SELECT)))
#define PAD_QUIT_FRAMES 60

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
static inline uint16_t pad_stick(int x, int y, bool* live) {
  const long long m2 = (long long)x * x + (long long)y * y;
  const long long gate = *live ? PAD_LEAVE : PAD_ENTER;
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
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return BTN_L;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return BTN_R;
    case SDL_CONTROLLER_BUTTON_START:         return BTN_START;
    case SDL_CONTROLLER_BUTTON_BACK:          return BTN_SELECT;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return BTN_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return BTN_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return BTN_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return BTN_RIGHT;
    // Guide, touchpad, the Edge's paddles and misc buttons. Deliberately not
    // bound, and not made into hotkeys either: a single button that quit the
    // game or toggled substitution would fire the first time somebody rested a
    // palm on the touchpad. The one gesture the frontend does claim — see
    // `pad_quit` — is two buttons held for a second, which a palm cannot do.
    default:                                  return -1;
  }
}

// --- the devices ------------------------------------------------------------

typedef struct {
  SDL_GameController* gc;
  SDL_JoystickID id;
  bool stick;            // left stick was out of the deadzone last frame
  bool aim;              // ...and the right one, which no SNES pad has
  bool trig_l, trig_r;   // ...and the triggers, same hysteresis
  char name[64];
} Pad;

typedef struct {
  Pad pad[PAD_MAX];
  int quit_held;  // consecutive frames the quit chord has been down
  bool ready;     // the subsystem came up
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

// What every pad is holding, as one 12-bit SNES mask per port.
static inline void pad_poll(PadSet* s, uint16_t held[PAD_MAX]) {
  for (int i = 0; i < PAD_MAX; i++) held[i] = 0;
  if (!s->ready) return;
  SDL_GameControllerUpdate();
  for (int i = 0; i < PAD_MAX; i++) {
    Pad* p = &s->pad[i];
    if (!p->gc) continue;
    uint16_t m = 0;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
      const int snes = pad_button((SDL_GameControllerButton)b);
      if (snes >= 0 && SDL_GameControllerGetButton(p->gc, (SDL_GameControllerButton)b))
        m |= (uint16_t)(1u << snes);
    }
    m |= pad_stick(SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_LEFTX),
                   SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_LEFTY),
                   &p->stick);
    if (pad_trigger(SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT),
                    &p->trig_l))
      m |= (uint16_t)(1u << BTN_L);
    if (pad_trigger(SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT),
                    &p->trig_r))
      m |= (uint16_t)(1u << BTN_R);
    held[i] = m;
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
    aim[i] = pad_stick(SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_RIGHTX),
                       SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_RIGHTY),
                       &p->aim);
  }
}

// Start and Select together, on any pad, held. Call once a frame with what
// `pad_poll` just returned; the answer is how many consecutive frames the chord
// has been down, so `PAD_QUIT_FRAMES` or more means quit and anything between
// says how far through the player is — which is what the frontend fades the
// picture by.
//
// It takes `held` by pointer and not by value because it strips the two bits on
// the way past. Both are buttons the game uses, and a quit that was thought
// better of should not leave the map open behind it.
//
// Deliberately *not* also checking the keyboard's copy of those two buttons. The
// keyboard has Esc, this exists because a pad does not, and Enter and RShift are
// close enough together to make the chord a real hazard on a keyboard in a way
// it is not on a pad.
static inline int pad_quit(PadSet* s, uint16_t held[PAD_MAX]) {
  bool down = false;
  for (int i = 0; i < PAD_MAX; i++) {
    if (!s->pad[i].gc) continue;
    if ((held[i] & PAD_QUIT_MASK) != PAD_QUIT_MASK) continue;
    down = true;
    held[i] = (uint16_t)(held[i] & ~PAD_QUIT_MASK);
  }
  s->quit_held = down ? s->quit_held + 1 : 0;
  return s->quit_held;
}

// How black the picture should be, 0..255, for a quit that is `frames` in. The
// fade is the only feedback the gesture has, so it starts on the first frame the
// chord is down rather than after a grace period: a player who taps the pair
// sees a flicker and knows the button did something.
static inline int pad_quit_dim(int frames) {
  if (frames <= 0) return 0;
  if (frames >= PAD_QUIT_FRAMES) return 255;
  return 255 * frames / PAD_QUIT_FRAMES;
}

#endif
