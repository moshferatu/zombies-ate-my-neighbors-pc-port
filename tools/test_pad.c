// Does an analog stick become the right D-pad?
//
// `src/pad.h` is mostly SDL calls, and those are checked by plugging a pad in.
// The part underneath them is arithmetic, it is the part that decides whether
// the game is pleasant to play, and every property below is one that a plausible
// implementation gets wrong:
//
//   * a stick at rest must produce nothing, and "at rest" is not zero — a real
//     DualSense Edge measured 908 counts of drift over ten seconds;
//   * the deadzone must be released lower than it is entered, or a stick held
//     near the gate chatters and the player stutters instead of walking;
//   * a direction held must be one direction, never a cardinal and its
//     opposite, and never nothing at all while the stick is out;
//   * the eight directions must divide the circle into eight *equal* parts. The
//     obvious implementation — each axis against a fixed threshold — does not:
//     it makes the diagonals wide and the cardinals narrow, and if the threshold
//     is set high enough to be usable it makes the cardinals unreachable.
//
// The last one is why this file sweeps the circle a tenth of a degree at a time
// rather than checking eight points: eight points cannot tell an octant from a
// sector twice as wide, and the sector widths are the whole question.
//
// ## And the device layer, without a device
//
// Everything above runs on plain integers with no SDL subsystem started. The
// second half of this file starts one, because SDL can supply the pad as well as
// read it: `SDL_JoystickAttachVirtual` makes a controller that exists only
// inside the process and whose buttons are set by calling a function. Pointed at
// `pad_open`/`pad_poll` it exercises the whole path — SDL's own generated
// mapping, the port assignment, the deadzone, the button table — with nothing
// plugged in and nobody pressing anything.
//
// It is the only way to check the quit chord, too, which cannot be tried by hand
// without quitting.
//
// It is also the only way to check the thing the polled design exists for. A
// pad unplugged mid-press sends no release, and the claim in `src/pad.h` is that
// this cannot leave a button stuck because the state is rebuilt from scratch
// every frame. `SDL_JoystickDetachVirtual` with a button held is that exact
// event, on demand, and asserting the mask goes to zero is the difference
// between having reasoned it and having checked it.
//
// The real-device drivers are turned off by hint before the subsystem starts, so
// a pad plugged into the machine running this cannot join in and change the
// answer. If the build has no virtual joystick driver the whole section says so
// and is skipped rather than failing.

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "pad.h"

static int failures;

static void fail(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  printf("FAIL ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  failures++;
}

#define UP    (1u << BTN_UP)
#define DOWN  (1u << BTN_DOWN)
#define LEFT  (1u << BTN_LEFT)
#define RIGHT (1u << BTN_RIGHT)

// A fresh stick, so a case cannot inherit the previous one's hysteresis.
static uint16_t once(int x, int y) {
  bool live = false;
  return pad_stick(x, y, &live);
}

// The eight legal answers, in order round the circle from due right. Anything
// `pad_stick` returns that is not one of these is a bug by definition.
static const uint16_t sets[8] = {RIGHT, RIGHT | DOWN, DOWN, DOWN | LEFT,
                                 LEFT,  LEFT | UP,    UP,   UP | RIGHT};

static const char* bits_name(uint16_t b) {
  switch (b) {
    case RIGHT:        return "right";
    case RIGHT | DOWN: return "down-right";
    case DOWN:         return "down";
    case DOWN | LEFT:  return "down-left";
    case LEFT:         return "left";
    case LEFT | UP:    return "up-left";
    case UP:           return "up";
    case UP | RIGHT:   return "up-right";
    case 0:            return "centre";
    default:           return "impossible";
  }
}

// --- the cases --------------------------------------------------------------

static void test_rest(void) {
  // Dead centre, and then the two things a real pad actually reports when
  // nobody is touching it: the resting sample this pad gave on the bench
  // (a0=-643, a1=642) and the worst drift seen over 600 samples (908).
  const struct { int x, y; const char* what; } rest[] = {
    {0, 0, "dead centre"},
    {-643, 642, "a DualSense Edge at rest"},
    {908, 0, "the worst drift measured over ten seconds"},
    {642, -642, "that drift on both axes"},
    {PAD_ENTER, 0, "exactly on the gate"},
    {PAD_ENTER - 1, 0, "one count inside the gate"},
  };
  for (int i = 0; i < (int)(sizeof rest / sizeof *rest); i++)
    if (once(rest[i].x, rest[i].y) != 0)
      fail("%s (%d,%d) moved the player", rest[i].what, rest[i].x, rest[i].y);

  // ...and one count past it does not.
  if (once(PAD_ENTER + 1, 0) != RIGHT)
    fail("one count past the gate did not register as right");
}

static void test_hysteresis(void) {
  bool live = false;
  // Out past the gate, then back to between the two thresholds: still held.
  if (pad_stick(20000, 0, &live) != RIGHT) fail("stick out did not register");
  if (!live) fail("stick out did not latch");
  if (pad_stick(PAD_LEAVE + 1, 0, &live) != RIGHT)
    fail("a held stick was dropped above the release gate");
  if (pad_stick(PAD_LEAVE - 1, 0, &live) != 0)
    fail("a held stick was not released below the release gate");
  if (live) fail("release did not clear the latch");
  // ...and from rest, the same position does nothing, which is the whole point.
  if (once(PAD_LEAVE + 1, 0) != 0) fail("the release gate was used from rest");

  // The gap has to be a gap. A release threshold at or above the entry one is
  // the bug this exists to prevent, and it would silently pass everything else.
  if (PAD_LEAVE >= PAD_ENTER) fail("the deadzone has no hysteresis");
}

static void test_cardinals(void) {
  const struct { int x, y; uint16_t want; } dirs[] = {
    { 32767,      0, RIGHT},
    {-32767,      0, LEFT},
    {     0,  32767, DOWN},   // SDL's Y axis points down, and so does the SNES's
    {     0, -32767, UP},
    { 23170,  23170, RIGHT | DOWN},
    {-23170,  23170, LEFT | DOWN},
    { 23170, -23170, RIGHT | UP},
    {-23170, -23170, LEFT | UP},
  };
  for (int i = 0; i < (int)(sizeof dirs / sizeof *dirs); i++) {
    const uint16_t got = once(dirs[i].x, dirs[i].y);
    if (got != dirs[i].want)
      fail("(%6d,%6d) gave %s, want %s", dirs[i].x, dirs[i].y, bits_name(got),
           bits_name(dirs[i].want));
  }
}

// The one that needs a sweep: are the eight sectors actually eighths?
static void test_octants(void) {
  // A tenth of a degree, at three radii — just past the gate, mid-throw, and
  // full deflection — because a threshold taken as a fraction of the stick's
  // own magnitude must give the same answer at every radius, and one taken as a
  // fixed number cannot.
  const int radii[] = {PAD_ENTER + 200, 18000, 32767};
  for (int r = 0; r < 3; r++) {
    int width[9];
    for (int i = 0; i < 9; i++) width[i] = 0;
    int impossible = 0, empty = 0;
    for (int t = 0; t < 3600; t++) {
      const double a = t * (3.14159265358979323846 / 1800.0);
      const int x = (int)lround(cos(a) * radii[r]);
      const int y = (int)lround(sin(a) * radii[r]);
      const uint16_t b = once(x, y);
      if ((b & (LEFT | RIGHT)) == (LEFT | RIGHT) ||
          (b & (UP | DOWN)) == (UP | DOWN)) {
        fail("(%d,%d) asked for two opposite directions at once", x, y);
        return;
      }
      if (b == 0) { empty++; continue; }
      int k = -1;
      for (int i = 0; i < 8; i++)
        if (sets[i] == b) k = i;
      if (k < 0) { impossible++; continue; }
      width[k]++;
    }
    if (empty)
      fail("radius %d: %d of 3600 angles past the deadzone gave no direction",
           radii[r], empty);
    if (impossible)
      fail("radius %d: %d angles gave a combination that is not a direction",
           radii[r], impossible);
    // 3600 tenths of a degree over eight sectors is 450 each. The eight
    // boundaries land exactly on a sample — 22.5 degrees plus a multiple of 45
    // is a whole number of tenths — and the comparison there is `>` against a
    // threshold the sample sits fractionally under, so each boundary sample
    // falls to the cardinal side. That is 451 for a cardinal and 449 for a
    // diagonal, which is why this is 450 give or take one rather than exact.
    for (int i = 0; i < 8; i++)
      if (abs(width[i] - 450) > 1)
        fail("radius %d: the %s sector is %d tenths of a degree wide, want 450",
             radii[r], bits_name(sets[i]), width[i]);
    // Reported separately from the per-sector check so a systematic skew is
    // legible rather than eight near-identical lines.
    int min = width[0], max = width[0];
    for (int i = 1; i < 8; i++) {
      if (width[i] < min) min = width[i];
      if (width[i] > max) max = width[i];
    }
    if (max - min > 2)
      fail("radius %d: sectors range from %d to %d tenths of a degree wide",
           radii[r], min, max);
  }
}

static void test_triggers(void) {
  bool live = false;
  if (pad_trigger(0, &live)) fail("a released trigger read as pressed");
  if (pad_trigger(PAD_TRIG_ENTER, &live))
    fail("a trigger exactly on the gate read as pressed");
  if (!pad_trigger(PAD_TRIG_ENTER + 1, &live))
    fail("a trigger past the gate did not read as pressed");
  if (!pad_trigger(PAD_TRIG_LEAVE + 1, &live))
    fail("a held trigger was dropped above the release gate");
  if (pad_trigger(PAD_TRIG_LEAVE, &live))
    fail("a held trigger was not released at the release gate");
}

static void test_buttons(void) {
  // Positional: the SNES diamond onto the pad's diamond. Getting this wrong is
  // not a crash, it is a game where the fire button rotates.
  const struct { SDL_GameControllerButton b; int want; const char* what; } m[] = {
    {SDL_CONTROLLER_BUTTON_A, BTN_B, "bottom face button -> B"},
    {SDL_CONTROLLER_BUTTON_B, BTN_A, "right face button -> A"},
    {SDL_CONTROLLER_BUTTON_X, BTN_Y, "left face button -> Y (fire)"},
    {SDL_CONTROLLER_BUTTON_Y, BTN_X, "top face button -> X"},
    {SDL_CONTROLLER_BUTTON_START, BTN_START, "start"},
    {SDL_CONTROLLER_BUTTON_BACK, BTN_SELECT, "back -> select"},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, BTN_UP, "d-pad up"},
    {SDL_CONTROLLER_BUTTON_TOUCHPAD, BTN_L, "touchpad -> L (the radar)"},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, BTN_R, "L3 -> R (the radar)"},
    // Not SNES buttons: they select, which the SNES pad cannot say.
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, -1, "L1 is not a SNES button"},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, -1, "R1 is not a SNES button"},
  };
  for (int i = 0; i < (int)(sizeof m / sizeof *m); i++)
    if (pad_button(m[i].b) != m[i].want)
      fail("%s: got %d, want %d", m[i].what, pad_button(m[i].b), m[i].want);

  // Every SNES button the game has must be reachable, and no two pad buttons may
  // reach the same one — a collision would make one of them dead without any
  // single case above failing.
  int seen[12];
  for (int i = 0; i < 12; i++) seen[i] = 0;
  for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
    const int s = pad_button((SDL_GameControllerButton)b);
    if (s < 0) continue;
    if (s >= 12) { fail("pad button %d maps outside the SNES pad", b); continue; }
    if (seen[s]++) fail("two pad buttons both map to SNES button %d", s);
  }
  for (int i = 0; i < 12; i++)
    if (!seen[i]) fail("SNES button %d is not reachable from a pad", i);
}

// --- the device layer, against a controller SDL invents ---------------------

// Drain the queue into the PadSet, which is what the frontend's event loop does
// with these and the only way a hot-plug reaches `pad_open`.
static void pump(PadSet* s) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) pad_event(s, &e);
}

static uint16_t poll1(PadSet* s, int port) {
  uint16_t held[PAD_MAX];
  pad_poll(s, held);
  return held[port];
}

// The frontend's quit action (`ACT_QUIT` in src/config.h), and whether it was
// pressed since last asked, as the frontend asks.
enum { QUIT = 0 };
static bool quit_pressed(PadSet* s) {
  const bool was = (s->hot_pressed & (1u << QUIT)) != 0;
  s->hot_pressed = 0;
  return was;
}

// A virtual gamepad's buttons and axes are its SDL_GameController indices, so
// these take the same names the mapping does.
static void press(SDL_GameController* gc, SDL_GameControllerButton b, int on) {
  SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(gc), b, (Uint8)on);
  SDL_JoystickUpdate();
}
static void move(SDL_GameController* gc, SDL_GameControllerAxis a, int v) {
  SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(gc), a, (Sint16)v);
  SDL_JoystickUpdate();
}

static void test_devices(void) {
  // Real controllers out of the way first: a pad plugged into the machine
  // running this must not be able to take port 1 and move everything below by
  // one. These have to be set before the subsystem starts.
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
  SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
  SDL_SetHint(SDL_HINT_DIRECTINPUT_ENABLED, "0");
  SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
  SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "0");
  if (SDL_Init(SDL_INIT_JOYSTICK) != 0) {
    printf("note: no joystick subsystem (%s); device checks skipped.\n",
           SDL_GetError());
    return;
  }
  // Six axes, fifteen buttons, one hat: a standard gamepad, which is what makes
  // SDL generate the standard mapping for it.
  const int one = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 1);
  if (one < 0) {
    printf("note: no virtual joystick driver (%s); device checks skipped.\n",
           SDL_GetError());
    SDL_Quit();
    return;
  }

  PadSet s;
  if (!pad_init(&s)) { fail("pad_init refused to start"); SDL_Quit(); return; }
  if (pad_count(&s) != 1) fail("a single attached pad gave pad_count %d", pad_count(&s));
  if (pad_ignored(&s) != 0) fail("one pad and two ports, yet %d ignored", pad_ignored(&s));
  SDL_GameController* gc = s.pad[0].gc;
  if (!gc) { fail("the attached pad did not reach port 1"); SDL_Quit(); return; }

  if (poll1(&s, 0) != 0) fail("an untouched virtual pad held %03x", poll1(&s, 0));

  // Every button in the table, one at a time, through SDL's own mapping rather
  // than through `pad_button` directly — which is what makes this a different
  // check from `test_buttons` and not a slower copy of it.
  const struct { SDL_GameControllerButton b; int want; } all[] = {
    {SDL_CONTROLLER_BUTTON_A, BTN_B},
    {SDL_CONTROLLER_BUTTON_B, BTN_A},
    {SDL_CONTROLLER_BUTTON_X, BTN_Y},
    {SDL_CONTROLLER_BUTTON_Y, BTN_X},
    {SDL_CONTROLLER_BUTTON_START, BTN_START},
    {SDL_CONTROLLER_BUTTON_BACK, BTN_SELECT},
    // (The touchpad is L, which `test_buttons` has; SDL's invented pad has no
    // touchpad to press.)
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, BTN_R},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, BTN_UP},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, BTN_DOWN},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, BTN_LEFT},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, BTN_RIGHT},
  };
  for (int i = 0; i < (int)(sizeof all / sizeof *all); i++) {
    press(gc, all[i].b, 1);
    const uint16_t got = poll1(&s, 0);
    if (got != (uint16_t)(1u << all[i].want))
      fail("%s gave %03x, want %03x",
           SDL_GameControllerGetStringForButton(all[i].b), got,
           1u << all[i].want);
    press(gc, all[i].b, 0);
  }
  if (poll1(&s, 0) != 0) fail("releasing every button left %03x held", poll1(&s, 0));

  // The stick, and the stick together with the D-pad: two sources of the same
  // four bits, which have to combine rather than fight.
  move(gc, SDL_CONTROLLER_AXIS_LEFTX, 30000);
  if (poll1(&s, 0) != (1u << BTN_RIGHT)) fail("stick right gave %03x", poll1(&s, 0));
  move(gc, SDL_CONTROLLER_AXIS_LEFTY, -30000);
  if (poll1(&s, 0) != ((1u << BTN_RIGHT) | (1u << BTN_UP)))
    fail("stick up-right gave %03x", poll1(&s, 0));
  press(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 1);
  if (poll1(&s, 0) != ((1u << BTN_RIGHT) | (1u << BTN_UP) | (1u << BTN_DOWN)))
    fail("stick and D-pad together gave %03x", poll1(&s, 0));
  press(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0);
  move(gc, SDL_CONTROLLER_AXIS_LEFTX, 0);
  move(gc, SDL_CONTROLLER_AXIS_LEFTY, 0);
  if (poll1(&s, 0) != 0) fail("centring the stick left %03x held", poll1(&s, 0));

  // The right stick, which is `--twin-stick`'s and nothing else's. Two claims,
  // and the second is the one that would ruin a game rather than merely fail to
  // improve one: it snaps the same eight ways the left stick does, and the two
  // sticks do not leak into each other. A right stick that reached `pad_poll`
  // would walk the player; a left stick that reached `pad_aim` would make the
  // whole flag a no-op with extra steps.
  {
    uint16_t aim[PAD_MAX];
    pad_aim(&s, aim);
    if (aim[0] != 0) fail("an untouched right stick aimed %03x", aim[0]);

    move(gc, SDL_CONTROLLER_AXIS_RIGHTX, -30000);
    pad_aim(&s, aim);
    if (aim[0] != (1u << BTN_LEFT)) fail("right stick left aimed %03x", aim[0]);
    if (poll1(&s, 0) != 0) fail("the right stick reached the D-pad: %03x", poll1(&s, 0));

    // Walking one way and aiming the other, which is the whole point of it.
    move(gc, SDL_CONTROLLER_AXIS_LEFTX, 30000);
    pad_aim(&s, aim);
    if (aim[0] != (1u << BTN_LEFT)) fail("the left stick moved the aim to %03x", aim[0]);
    if (poll1(&s, 0) != (1u << BTN_RIGHT))
      fail("the right stick moved the walk to %03x", poll1(&s, 0));

    move(gc, SDL_CONTROLLER_AXIS_RIGHTY, 30000);
    pad_aim(&s, aim);
    if (aim[0] != ((1u << BTN_LEFT) | (1u << BTN_DOWN)))
      fail("right stick down-left aimed %03x", aim[0]);

    move(gc, SDL_CONTROLLER_AXIS_RIGHTX, 0);
    move(gc, SDL_CONTROLLER_AXIS_RIGHTY, 0);
    move(gc, SDL_CONTROLLER_AXIS_LEFTX, 0);
    pad_aim(&s, aim);
    if (aim[0] != 0) fail("centring the right stick left %03x aimed", aim[0]);
    if (poll1(&s, 0) != 0) fail("centring both sticks left %03x held", poll1(&s, 0));
  }

  // The shoulders and the triggers select: the next item and the one before,
  // the next weapon and the one before. None of the four is a SNES button, each
  // is reported once for a press and not again while it is held, and they are
  // the port's own.
  //
  // A released trigger is -32768 and not 0, which is worth stating because
  // writing 0 here is the obvious thing and it holds the button down. SDL maps a
  // plain `righttrigger:aN` binding from the axis's full range onto the 0..32767
  // a trigger reports, so raw 0 arrives as 16,383 — half pressed. That is not a
  // quirk of the virtual device: the DualSense Edge measured for this file rests
  // its two trigger axes at -32768 as well.
  #define TRIGGER_RELEASED (-32768)
  move(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT, TRIGGER_RELEASED);
  move(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, TRIGGER_RELEASED);
  if (poll1(&s, 0) != 0) fail("released triggers held %03x", poll1(&s, 0));
  if (s.cycle_pressed[0] || s.cycle_pressed[1]) fail("a selection was pressed by nobody");
  move(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000);
  if (poll1(&s, 0) != 0) fail("the right trigger held %03x", poll1(&s, 0));
  if (s.cycle_pressed[0] != (1u << PAD_CYCLE_NEXT_WEAPON))
    fail("the right trigger selected %x", s.cycle_pressed[0]);
  s.cycle_pressed[0] = 0;
  for (int i = 0; i < 10; i++) poll1(&s, 0);
  if (s.cycle_pressed[0]) fail("a held trigger selected again: %x", s.cycle_pressed[0]);
  move(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000);
  if (poll1(&s, 0) != 0) fail("both triggers held %03x", poll1(&s, 0));
  if (s.cycle_pressed[0] != (1u << PAD_CYCLE_PREV_WEAPON))
    fail("the left trigger selected %x", s.cycle_pressed[0]);
  s.cycle_pressed[0] = 0;
  move(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT, TRIGGER_RELEASED);
  move(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, TRIGGER_RELEASED);
  if (poll1(&s, 0) != 0) fail("releasing the triggers left %03x held", poll1(&s, 0));
  press(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
  press(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1);
  if (poll1(&s, 0) != 0) fail("the shoulders held %03x", poll1(&s, 0));
  if (s.cycle_pressed[0] != ((1u << PAD_CYCLE_NEXT_ITEM) | (1u << PAD_CYCLE_PREV_ITEM)))
    fail("the shoulders selected %x", s.cycle_pressed[0]);
  if (s.cycle_pressed[1]) fail("port 1's shoulders selected for port 2: %x", s.cycle_pressed[1]);
  s.cycle_pressed[0] = 0;
  press(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
  press(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0);
  poll1(&s, 0);
  press(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
  poll1(&s, 0);
  if (s.cycle_pressed[0] != (1u << PAD_CYCLE_NEXT_ITEM))
    fail("a shoulder pressed again selected %x", s.cycle_pressed[0]);
  s.cycle_pressed[0] = 0;
  press(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
  poll1(&s, 0);

  // A `zamn.ini` from before the shoulders selected says `r = r1, r2`. An input
  // in both places selects and does not also bring up the radar.
  {
    const PadMap was = s.map;
    pad_map_add(s.map.game[BTN_R], SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    pad_map_add(s.map.game[BTN_R], PAD_IN_RTRIGGER);
    press(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
    move(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000);
    if (poll1(&s, 0) != 0) fail("an input that selects was a SNES button as well: %03x", poll1(&s, 0));
    press(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK, 1);
    if (poll1(&s, 0) != (1u << BTN_R)) fail("...and took the rest of its list with it: %03x", poll1(&s, 0));
    press(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK, 0);
    press(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
    move(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, TRIGGER_RELEASED);
    poll1(&s, 0);
    s.cycle_pressed[0] = 0;
    s.map = was;
  }

  // A table that is not the default one, which is what a player's `zamn.ini`
  // makes of `PadSet.map`. Everything above ran on the table `pad_init` built
  // from `pad_button`, so it has already shown that the default plays as it did
  // before there was a table; this is the other half, that the table is what
  // is read and not decoration.
  {
    const PadMap was = s.map;
    // Fire on the right trigger and nowhere else; the bottom face button does
    // nothing; the sticks change places; quick save (action 5, say) on the left
    // stick's click and on the left trigger; nothing selects.
    for (int c = 0; c < PAD_CYCLE_COUNT; c++) pad_map_clear(s.map.cycle[c]);
    pad_map_clear(s.map.game[BTN_Y]);
    pad_map_add(s.map.game[BTN_Y], PAD_IN_RTRIGGER);
    pad_map_clear(s.map.game[BTN_R]);
    pad_map_clear(s.map.game[BTN_B]);
    pad_map_clear(s.map.game[BTN_L]);
    s.map.move_stick = PAD_STICK_RIGHT;
    s.map.aim_stick = PAD_STICK_LEFT;
    pad_map_add(s.map.hot[5], SDL_CONTROLLER_BUTTON_LEFTSTICK);
    pad_map_add(s.map.hot[5], PAD_IN_LTRIGGER);
    if (pad_map_add(s.map.hot[5], PAD_IN_LTRIGGER)) fail("an input was added to a list twice");

    press(gc, SDL_CONTROLLER_BUTTON_X, 1);
    if (poll1(&s, 0) != 0) fail("the left face button still fires after being unbound: %03x", poll1(&s, 0));
    press(gc, SDL_CONTROLLER_BUTTON_X, 0);
    press(gc, SDL_CONTROLLER_BUTTON_A, 1);
    if (poll1(&s, 0) != 0) fail("an unbound button held %03x", poll1(&s, 0));
    press(gc, SDL_CONTROLLER_BUTTON_A, 0);
    move(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000);
    if (poll1(&s, 0) != (1u << BTN_Y)) fail("the right trigger, bound to Y, gave %03x", poll1(&s, 0));
    move(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, TRIGGER_RELEASED);

    uint16_t aim[PAD_MAX];
    move(gc, SDL_CONTROLLER_AXIS_RIGHTX, 30000);
    pad_aim(&s, aim);
    if (poll1(&s, 0) != (1u << BTN_RIGHT) || aim[0] != 0)
      fail("the right stick, steering now, walked %03x and aimed %03x", poll1(&s, 0), aim[0]);
    move(gc, SDL_CONTROLLER_AXIS_RIGHTX, 0);
    move(gc, SDL_CONTROLLER_AXIS_LEFTY, -30000);
    pad_aim(&s, aim);
    if (poll1(&s, 0) != 0 || aim[0] != (1u << BTN_UP))
      fail("the left stick, aiming now, walked %03x and aimed %03x", poll1(&s, 0), aim[0]);
    s.map.aim_stick = PAD_STICK_NONE;
    pad_aim(&s, aim);
    if (aim[0] != 0) fail("a stick that is off aimed %03x", aim[0]);
    move(gc, SDL_CONTROLLER_AXIS_LEFTY, 0);

    // The deadzone is the table's: 20,000 is well out at the default gate and
    // is nothing at a gate of 24,000.
    s.map.enter = 24000;
    s.map.leave = 18000;
    move(gc, SDL_CONTROLLER_AXIS_RIGHTX, 20000);
    if (poll1(&s, 0) != 0) fail("a stick inside a wide deadzone walked %03x", poll1(&s, 0));
    move(gc, SDL_CONTROLLER_AXIS_RIGHTX, 26000);
    if (poll1(&s, 0) != (1u << BTN_RIGHT)) fail("a stick outside a wide deadzone gave %03x", poll1(&s, 0));
    move(gc, SDL_CONTROLLER_AXIS_RIGHTX, 0);
    poll1(&s, 0);

    // An action is a press, not a hold: once when it goes down, not again
    // while it stays down, again after it has come up; and two inputs of one
    // action overlapping are one press.
    s.hot_pressed = 0;
    poll1(&s, 0);
    if (s.hot_pressed) fail("an action was pressed by nobody: %x", s.hot_pressed);
    press(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK, 1);
    poll1(&s, 0);
    if (s.hot_pressed != (1u << 5)) fail("the action's button gave %x", s.hot_pressed);
    s.hot_pressed = 0;
    for (int i = 0; i < 10; i++) poll1(&s, 0);
    if (s.hot_pressed) fail("a held action repeated: %x", s.hot_pressed);
    move(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000);
    poll1(&s, 0);
    if (s.hot_pressed) fail("a second input of a held action pressed it again: %x", s.hot_pressed);
    if (poll1(&s, 0) != 0) fail("an action's input reached the game: %03x", poll1(&s, 0));
    press(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK, 0);
    move(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT, TRIGGER_RELEASED);
    poll1(&s, 0);
    move(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000);
    poll1(&s, 0);
    if (s.hot_pressed != (1u << 5)) fail("the action's trigger, pressed again, gave %x", s.hot_pressed);
    move(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT, TRIGGER_RELEASED);
    poll1(&s, 0);
    s.hot_pressed = 0;

    s.map = was;
    press(gc, SDL_CONTROLLER_BUTTON_X, 1);
    if (poll1(&s, 0) != (1u << BTN_Y)) fail("the default table, put back, gave %03x", poll1(&s, 0));
    press(gc, SDL_CONTROLLER_BUTTON_X, 0);
    if (poll1(&s, 0) != 0 || s.hot_pressed) fail("the default table has an action bound");
  }

  // The quit chord, bound as zamn.ini binds it by default: a hotkey of two
  // inputs at once. It must not fire on one button, must fire on the first
  // frame both are down, must not let Start or Select through to the game on
  // that frame, and must let go the moment either comes up.
  pad_map_add(s.map.hot[QUIT], pad_chord(SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK));
  if (pad_chord(SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK) !=
      pad_chord(SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START))
    fail("a chord depends on the order it is written in");
  press(gc, SDL_CONTROLLER_BUTTON_START, 1);
  {
    uint16_t held[PAD_MAX];
    pad_poll(&s, held);
    if (quit_pressed(&s)) fail("Start alone quit");
    if (held[0] != (1u << BTN_START)) fail("Start alone was eaten (%03x)", held[0]);
  }
  press(gc, SDL_CONTROLLER_BUTTON_BACK, 1);
  {
    uint16_t held[PAD_MAX];
    pad_poll(&s, held);
    if (!quit_pressed(&s)) fail("Start+Select did not quit on the first frame");
    if (held[0] != 0) fail("the quit chord leaked %03x to the game", held[0]);
  }
  press(gc, SDL_CONTROLLER_BUTTON_BACK, 0);
  {
    uint16_t held[PAD_MAX];
    pad_poll(&s, held);
    if (quit_pressed(&s)) fail("releasing Select quit again");
    if (s.pad[0].hot_down & (1u << QUIT)) fail("releasing Select did not end the chord");
    if (held[0] != (1u << BTN_START)) fail("Start was eaten after the chord");
  }
  press(gc, SDL_CONTROLLER_BUTTON_START, 0);
  if (poll1(&s, 0) != 0) fail("the quit chord left %03x held", poll1(&s, 0));

  // A second pad, arriving the way a real one does: as an event, mid-session.
  const int two = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 1);
  if (two < 0) {
    fail("could not attach a second virtual pad (%s)", SDL_GetError());
  } else {
    pump(&s);
    if (pad_count(&s) != 2)
      fail("a second pad plugged in mid-session gave pad_count %d", pad_count(&s));
    else {
      // The two ports must be independent, which is the whole point of there
      // being two of them.
      press(s.pad[1].gc, SDL_CONTROLLER_BUTTON_A, 1);
      uint16_t held[PAD_MAX];
      pad_poll(&s, held);
      if (held[0] != 0) fail("port 2's button showed up on port 1 (%03x)", held[0]);
      if (held[1] != (1u << BTN_B)) fail("port 2's button gave %03x", held[1]);
      press(s.pad[1].gc, SDL_CONTROLLER_BUTTON_A, 0);

      // Either player can quit, so the chord works on port 2 as well — and it
      // must strip the bits from the pad that pressed it and not from port 1.
      press(s.pad[1].gc, SDL_CONTROLLER_BUTTON_START, 1);
      press(s.pad[1].gc, SDL_CONTROLLER_BUTTON_BACK, 1);
      press(gc, SDL_CONTROLLER_BUTTON_START, 1);
      pad_poll(&s, held);
      if (!quit_pressed(&s)) fail("the quit chord did not quit on port 2");
      if (held[1] != 0) fail("port 2's quit chord leaked %03x", held[1]);
      if (held[0] != (1u << BTN_START))
        fail("port 2's chord ate port 1's Start (%03x)", held[0]);
      press(gc, SDL_CONTROLLER_BUTTON_START, 0);
      press(s.pad[1].gc, SDL_CONTROLLER_BUTTON_START, 0);
      press(s.pad[1].gc, SDL_CONTROLLER_BUTTON_BACK, 0);
      pad_poll(&s, held);
      quit_pressed(&s);

      press(s.pad[1].gc, SDL_CONTROLLER_BUTTON_A, 1);
      pad_poll(&s, held);

      // ...and now unplug it with that button still down. Nothing may stick,
      // and the port has to come free for the next pad.
      SDL_JoystickDetachVirtual(two);
      pump(&s);
      pad_poll(&s, held);
      if (held[1] != 0)
        fail("a pad unplugged mid-press left %03x held on port 2", held[1]);
      if (pad_count(&s) != 1)
        fail("unplugging one of two pads left pad_count %d", pad_count(&s));
      if (s.pad[1].gc) fail("port 2 was not released");
    }
  }

  // The same for the last one, which also checks a port-1 pad is released
  // rather than only a port-2 one.
  press(gc, SDL_CONTROLLER_BUTTON_A, 1);
  SDL_JoystickDetachVirtual(one);
  pump(&s);
  if (poll1(&s, 0) != 0) fail("the last pad left buttons held after unplugging");
  if (pad_count(&s) != 0) fail("unplugging every pad left pad_count %d", pad_count(&s));

  pad_free(&s);
  SDL_Quit();
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  test_rest();
  test_hysteresis();
  test_cardinals();
  test_octants();
  test_triggers();
  test_buttons();
  test_devices();
  if (failures) {
    printf("\n%d failure%s.\n", failures, failures == 1 ? "" : "s");
    return 1;
  }
  printf("pad: all checks passed.\n");
  return 0;
}
