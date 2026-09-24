# Zombies Ate My Neighbors — native PC port

A personal project to build a native Windows port of the SNES game *Zombies Ate
My Neighbors*, following the `snesrev/zelda3` model: keep the SNES PPU/APU
emulated, but progressively reimplement the game logic as clean native C,
verified against the original ROM. See **`PLAN.md`** for the full plan and
**`PROGRESS.md`** for current status.

> You supply your own legally-obtained ROM (`Zombies Ate My Neighbors.sfc`).
> No game data is redistributed here.

## Status
**Phase 3 underway.** The game boots and runs natively through a vendored SNES
core (Phase 0); an in-tree analysis toolchain produces a Code/Data Log, memory
map and annotated disassembly from reproducible traced runs (Phase 1); the
asset pipeline is done — compression, graphics, level layout, the placement
lists, the sprite/OAM path and the audio upload path all reimplemented in C and
**verified byte-exact against the ROM's own routines** (Phase 2).

Phase 3 is the logic port. The co-simulation harness is built, eighty-two routines
are through it — including **the whole per-frame sprite pass**, the collision
dispatch and the twenty-six actor handlers it routes to, the game's **random
number generator**, the two searches every enemy uses to pick who to chase
and to ask what is in its way, and **the whole of the movement step validator**:
where a mover wants to go, and all four tests that decide whether it may — what
is in the way, what the terrain under it is, whether it is still on the map, and
whether it has reached the end of its co-op leash — and the coroutine problem is solved: a ported
routine that suspends inside the thread scheduler does it at an explicit resume
point, with its parked state as plain copyable data. `zamn_cosim verify` checks
the C against the
ROM's own code on every call the game makes — 128 KB of WRAM plus registers — and
passes **15,801,281 of 15,801,281 across the whole movie corpus**, thirteen levels
deep. `tools/verify_corpus.ps1 -Coverage` prints the census of what the ROM was
asked to run and the port did not have, and it stands at **fifty-nine declines
against three addresses** — all of them reached by the two movies that finish a
level, because until an input did that the exit door and everything past it were
code no test could get to. The census has been empty twice and the two emptinesses
are not worth the same. Before `movies/level29-990b.zmv` was cut it was empty for
the weak reason: nothing in the
corpus had ever shot the creature that swaps in `$81:96E4`. Two movies written to
stand and fight took it to 430 declines against that one address, and porting the
eleven bytes behind it took it back to nothing. That is the loop the census
exists for: it is a work list, it is only as good as the inputs that run it, and
an empty one means something different depending on which of the two you moved
last.

`zamn_cosim run` goes further and substitutes the C for real, diffing two whole
machines every scheduler pass, and **finds no byte of live game state differing
on any movie tried** — `level1`, `level9-weapons` at 9,000 frames,
`level25-boss` at 7,600, `level29-fighting` at 6,000, every substitutable routine
at once. On the heaviest level it stops early and says why: a scheduler pass
whose work overruns vblank takes two frames instead of one, whether it does is
decided by a few hundred cycles, and a substituted core spends a different
number of cycles by construction — so the two machines end up on different
frames of the same game and nothing after that is comparable. `docs/cosim.md`
has the account, including the two harness bugs that made this look for months
like a divergence in the port.

The port also reports **which of its own branches an input actually reached**,
because a branch no movie takes is one the diff agrees with the ROM about for
the wrong reason. 107 of 551 are still untaken by every input, and they are the
backlog — a list that grows when a routine is ported and shrinks only when an
input is written. The two measures disagree on purpose, and three rounds
moved both in opposite directions: an input that reaches code the port lacks puts
a line in the census *and* lights up the two sites whose only job is to say a
handler was missing, and porting that code empties the census *and* puts both
sites back in the backlog. The clause about inputs is the one that had never been
shown on its own, and `movies/level29-990b-2p.zmv` is it: nothing ported, no site
added, two taken — a second player one frame out of phase with the first, because
the squirt gun fires every twelve frames and the flash a hit leaves lasts two.

See **`docs/cosim.md`** for the harness and what coverage measures that the diff
cannot, **`docs/threads.md`** for how a ported routine suspends,
**`docs/frame-skeleton.md`** for how the game's main loop works,
**`docs/wram-map.md`** for the memory map, **`docs/analysis-tools.md`** for the
analysis tools, and **`docs/asset-formats.md`** for the data formats.

## Build (Windows)
Requires Visual Studio 2022 (with the C++ workload — provides MSVC, CMake, Ninja).
```
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```
SDL2 is fetched and built automatically the first time.

## Run
```
build\zamn.exe "Zombies Ate My Neighbors.sfc"
```
The ROM's path, how the picture is shown and every key and pad binding can be
set once in **`zamn.ini`** -- see [the settings file](#the-settings-file-zamnini)
below -- or in **`zamn_launcher.exe`**, which ships beside the game and edits
the same file ([the launcher](#the-launcher-zamn_launcherexe)). The controls
that follow are the defaults.

Controls: Arrows = D-pad · Z=B X=A A=Y S=X · Q=L W=R · Enter=Start · RShift=Select · Esc=Quit
· **F1 = toggle native substitution** · **F2 = cycle scaling**
· **F4 = cycle widescreen** · **F5 = quick save**
· **F6 = toggle smoothing** · **F9 = quick load** · **F11 / Alt+Enter = fullscreen**

**Game controllers** work too, and are the way to actually play it: any pad SDL
recognises — which is most of them, and a `gamecontrollerdb.txt` beside the
executable covers the rest — hot-pluggable, with the first two taking the two
SNES ports. So two pads is two players. The keyboard plays port 1 alongside
the first pad, and port 2 as well once `[keyboard player 2]` in `zamn.ini` has
keys in it, which makes a pad and a keyboard two players. Face buttons are positional: the bottom one is B, the right one A, the
left one Y, the top one X. Worth knowing before you start rather than after,
because it is not where a modern game would put it: **Y is this game's fire
button**, and it is held rather than tapped — B and A cycle weapons and items, X
uses one. The left stick steers as well as the D-pad, snapped to eight ways
with a deadzone that has to be crossed further to enter than to leave.

**The shoulders and the triggers select, both ways**: R1 is the next item and
L1 the one before, R2 the next weapon and L2 the one before. The cartridge
only goes forwards -- thirteen presses of B to reach the weapon just passed --
and it cannot change weapon at all while a loaded one is firing, because the
player's frame clears B out of the button word for as long as fire is held.
So these are not SNES buttons. `pad_poll` reports the presses by port, the
frontend asks the port for a step (`player_cycle_request`, in
`src/port/player.h`), and `player_state_normal` takes one step a frame where
it handles B and A: forwards with the game's own search, backwards with the
same search walked the other way, the same store, weapon data and sound, and
the same silence when there is nothing else to select. A request the player's
frame does not come round for within twelve frames -- paused, or not on their
feet -- is dropped. They work while firing, which with the right stick firing
is most of the time. `--stock` and F1 hand that routine back to the 65816,
which knows none of this, and there they do nothing.

**The radar moved** to make room: the SNES's L and R, which both bring it up,
are the touchpad's click -- where a PlayStation game keeps its map -- and L3.
All of it is `[controller buttons]` in `zamn.ini`: `l`, `r`, `next_weapon`,
`previous_weapon`, `next_item`, `previous_item`. An input bound to a selection
is not also a SNES button, so a `zamn.ini` from before this, which still says
`l = l1, l2` and `r = r1, r2`, selects with all four, says so as it starts,
and has no radar until `l` and `r` are given something else.
Checked by `zamn_test_twinstick`, which drives `player_state_normal` against
the cartridge (backwards, wrapping, two queued presses, one weapon, none, the
items, a request expiring, and that B under a held fire button does nothing
where a request does), by `zamn_test_pad` (one press per press, by port, and
an input in both places) and `zamn_test_config`; and in `zamn.exe` on level 1
with requests put in by hand: weapon 0 to 7 to 3 to 0, item 7 to 4, and the
HUD following.

**Start and Select held together for a second quits** — Options and Share on a
DualSense — because a pad has no Esc. Held rather than pressed, since both are
buttons the game itself uses and an instant quit on the pair is a session lost to
a thumb that bridged them; the picture fades to black while you hold it, so the
second is visible rather than a button that appears not to work. Neither button
reaches the game while the chord is down, so a quit thought better of leaves
nothing behind. `--no-pads` turns the whole thing off. The reasoning, and the
measurements the deadzone came from, are in `src/pad.h`.

The **right stick aims and fires** — twin-stick shooting, on by default and
described below. `--no-twin-stick` gives it back to the stock game, which reads
no second stick at all.

It starts **fullscreen**, with the mouse cursor hidden — there is no mouse input
in this game, so the pointer is only ever something on top of the picture. F11
and Alt+Enter move between fullscreen and a window at any time, and Esc quits
from either. Fullscreen is the borderless desktop kind rather than an exclusive
mode change: no resync while the monitor changes mode, and alt-tab comes
straight back.

Three ways to start windowed instead: `--windowed`; `--scale N`, which sizes the
window at N times the picture (1–8) and starts in it; and `--frames N`, because a
batch run is a smoke test or a throughput measurement and has no business
seizing the display of whoever started it. Whichever way, `--scale` is also the
size F11 comes back to.

The console is quiet. A session that is played prints the controller it found,
the cheats and the starting level if any, a line for anything that could not be
had or went wrong, and at exit one line with the frames and the rate.
`--verbose` says everything: the ROM, the renderer, every binding, the display,
the pacing, the audio device and the substitution at the start, the top scores
file as it is read and written, and the frame cadence, the per-routine table and
the native share at exit. A `--frames` or `-m` run is a measurement and prints
all of that without being asked.

### The settings file: `zamn.ini`

Every setting had a flag first, and a flag is the wrong place for the ones a
player sets once. `src/config.h` reads them from a file, and **an option on
the command line beats the file**: the file is how this player plays, an
option is how this run differs.

It is `--config <file>` if one is named, otherwise `zamn.ini` in the
directory the game is started from, otherwise `zamn.ini` beside the
executable. When there is none, the first start writes one where it was
started, with every setting at its default and a comment on each (a movie or
a `--frames` run writes nothing; `--no-config` reads and writes nothing). A
path in the file that is not absolute is taken from the file's directory.
Nothing in the file can stop the game starting: a line that cannot be read is
reported with its line number and the setting keeps its default. `/zamn.ini`
is in `.gitignore`, since it names the player's ROM.

| Section | Settings |
|---|---|
| `[game]` | `rom`, `skip_intro`, `level` (off, 0-55), `hitbox` (100-200), `blood` (purple, red), `high_scores`, `high_scores_file` |
| `[video]` | `fullscreen`, `widescreen` (off, 16:9, 16:10, 21:9, auto), `filter` (sharp, integer, linear), `window_scale` (1-8), `smoothing`, `refresh` (auto, Hz) |
| `[audio]` | `enabled`, `volume` (0-100) |
| `[controller]` | `enabled`, `twin_stick`, `deadzone` (5-90, percent), `move_stick` and `aim_stick` (left, right, off) |
| `[controller buttons]` | the twelve SNES buttons, as lists of pad inputs, for both pads |
| `[controller hotkeys]` | what the frontend does, from the pad; nothing is bound by default |
| `[keyboard]`, `[keyboard player 2]` | the twelve SNES buttons, as lists of keys; player 2's are unbound by default |
| `[hotkeys]` | `quit`, `toggle_native`, `cycle_filter`, `cycle_widescreen`, `quick_save`, `quick_load`, `toggle_smoothing`, `fullscreen` |
| `[cheats]` | `invincible`, `invincible_neighbors`, `infinite_ammo`, `infinite_lives`, `give_all`, `always_run`; all off by default ([cheats](#cheats)) |

A binding is a list of up to four, separated by commas, and nothing after the
`=` binds nothing:

```ini
[keyboard]
y = Space, Left Ctrl

[controller buttons]
y = west, r2

[controller hotkeys]
quick_save = paddle1
quick_load = paddle2
```

Keys are named as SDL names them, which is the legend on the key (`A`, `F5`,
`Up`, `Return`, `Left Ctrl`, `Keypad 1`), with `Enter`, `Esc`, `RShift` and
the like read too, and `Comma`, `Semicolon` and `Hash` for the three the format
uses itself -- which is also why there are no comments at the end of a line.
Pad inputs are named by position, since that is what is the same from pad to
pad: `south east west north`, `l1 r1` (shoulders), `l2 r2` (triggers), `l3 r3`
(stick clicks), `dpup dpdown dpleft dpright`, `start`, `select`, `guide`,
`touchpad`, `misc1`, `paddle1`-`paddle4`. SDL's own names and the usual others
(`cross`, `lb`, `rt`, `share`, `options`) are read as well. A key bound twice
does the first thing only, a hotkey before a button, and the game says so as
it starts. `--verbose` lists at startup what is actually bound.

Three of the file's settings are not applied under `-m`: `skip_intro`, `level`
and `hitbox`. A movie was recorded from reset against the game as it shipped.
The options that say no to the file for one run are `--no-skip-intro`,
`--smooth`, `--no-red-blood`, `--pads`, `--audio` and the existing
`--windowed`, `--widescreen off` and the rest; `--volume N` is new with the
setting.

What changed underneath: the pad's button `switch` became `PadMap`, a table
`pad_poll` reads, whose default is built from the old `switch` so that an
untouched table plays exactly as before; the triggers are inputs in it like any
other; a pad hotkey is reported on the press and not while held. The
keyboard's `switch` became `Config.key`, and what is held is tracked a bit per
key rather than per button, so Select on two keys is held until both are let
go (it used to be let go with the first). The function keys became named
actions, noted in the event loop and done once below it, which is what lets a
pad button do them too.

Checked by `zamn_test_config`: the file that is written reads back as exactly
the defaults (so the two cannot drift), every setting sets, 19 bad lines
are each reported and change nothing, lists and unbinding and the three
worded keys, keys bound twice, the two-keys-one-button hold, and paths.
`zamn_test_pad` gained a rebound table on its virtual controller: an unbound
button, fire on a trigger, the sticks exchanged and turned off, a wider
deadzone, and an action that fires once per press across two overlapping
inputs. And through the real event loop with `--key-at frame:key`, which
puts key events on SDL's queue: with `start = T` in the file, T starts the
game from the title and gives the same picture at frame 900 as Return does
with the defaults, Return then does nothing, and a hotkey moved to F8 works
there and no longer on F3.

### The launcher: `zamn_launcher.exe`

A window for `zamn.ini`, for the player who would rather not open a text
file, and a Play button. It ships beside `zamn.exe` and holds exactly the
file's settings, no more: seven tabs (Game, Video, Audio, Controller, Keyboard,
Hotkeys), drawn on black at the display's scale in the system's font.

- **The file** is found the way the game finds it (`--config <file>`, then
  the working directory, then beside the executable), or made beside the
  launcher from the default text if there is none. Save writes each value
  where it stands (`config_update_text` in `src/config.h`), so a player's
  own comments, spellings and line endings survive; nothing is written until
  Save or Play. **Play** saves and starts `zamn.exe --config <that file>`
  from the launcher's folder, so the game cannot read a different file. The
  launcher hides while the game runs and comes back when it closes. It will
  not start the game while the cartridge is missing, and says where it
  looked.
- **Values** with a fixed list are dropdowns: Enter, South or a click opens
  one, Up and Down choose, and Escape or East closes it unchanged. Left and
  Right change the value without opening it. The starting level's list names
  every level as its card does. The three sliders are dragged. The cartridge and the top scores file are typed after
  Enter, chosen with Browse, or the cartridge dropped on the window. Browse
  stores a path under the file's folder relative to it.
- **Bindings are set by pressing them.** Enter, a click on `+`, or South waits
  five seconds for the next key or pad input and adds it to the row. Every key
  can be bound, Escape included, so a wait for a key ends with a click or the
  timeout. Backspace or West removes the last one, and a chip's x removes that
  one. Four at most, as in the file. A key or pad input that loses to another
  row, in the order the game decides it (`config_check`), is drawn red, and
  the row says what wins. Pad inputs are named for the pad last touched:
  Cross and Circle on a DualSense, A and B on an Xbox pad, positions
  otherwise. The file always gets positions.
- **Keyboard or controller:** Up and Down move, Tab or L1/R1 turn the tabs,
  past the last row are the buttons; Ctrl+S saves; Ctrl+Enter, F5 or Start
  plays; Escape quits at once, unsaved changes or not.

**Its icon is the title screen** as the logo comes to rest, before START and
PASSWORD appear: `zamn_icon` (`tools/make_icon.c`) boots the cartridge in the
core and takes frame 2180, which has the whole logo and no menu. The picture
is 4:3 with transparent bars, averaged in linear light at 16, 24, 32, 48, 64,
128 and 256. It is drawn from the player's own cartridge when the launcher is
built, since nothing of the cartridge's is kept in the repository.
`ZAMN_ICON_ROM` in CMake names the cartridge and defaults to the one in the
source root. Without it the launcher builds without an icon.

**Its heading is the title's logo**, laid out as on the title: `zamn_logo`
(`tools/make_logo.c`) takes the same frame with only BG1, the logo's layer,
drawn over a black backdrop and then a white one, and keeps the pixels that
are the same in both. The launcher draws it 104 points high, in a header
made taller for it, blown up by whole pixels before being smoothed down so they stay
sharp. It comes from the cartridge at build time as the icon does. Without
one the heading is text.

Checked by `zamn_test_config`: the defaults written into the default file do
not change a byte of it; every setting off its default, written into the
CRLF default file and into an empty one, reads back as itself, with every line
still CRLF; a file of the player's own keeps its comments, its
`[Keyboard Player 1]` and `prev_weapon` spellings, a `fullscreen=on` whose
value did not change, and a last line with no ending. The launcher itself
takes `--press <keys>` and `--screenshot <file.png>` to be driven without a
window. That is how it was checked: a value changed and saved touches that
line alone, a key is captured (F5 included, which would otherwise play),
added, removed and shown red where a hotkey wins, a first start with no file
writes the 169-line default in CRLF, and Play from another folder, against a
stand-in `zamn.exe`, saved first and then started it in the launcher's folder
with `--config` and the file's full path. `zamn.exe` reads the file the
launcher wrote without a complaint.

### Skipping the intro

```
build\zamn.exe --skip-intro
```

Konami, LucasArts, a story screen and then the title: **19.2 seconds** before the
menu is up, which is a long time to sit through once and an absurd one to sit
through on every launch of a build you are testing. `--skip-intro` used to run
those frames as fast as the machine can, which had become 5.4 s of a black
window (1,150 frames at about 210 a second in 16:9).

**Now it does not run them: the game has its own way past its logos**
(`src/skipintro.h`). The main thread is a loop -- the opening, a game, the
game over, and round again -- and the opening, `$80:9126`, shows the logos
only the first time round: `$80:9136  LDA $7C : BNE $9152` jumps the region
check, Konami, LucasArts and the story screen and lands on the title, which is
how a game over comes back to the title and not to Konami. `intro_bypass`
makes that `BNE` a `BRA` in the cartridge's copy of the image (never the
file), so the first time round is like every other. What the jumped calls do
is presentation -- each decompresses its own pictures, fades up, waits and
fades out -- and the title sets up all it uses, since its other way in is
from a level.

What is left of the boot is what the game does with the screen off: clearing
memory (17 frames), sending the sound driver to the audio processor (64), and
two more uploads of music and samples (45 and 89). Those 222 frames are
run at full speed, **0.6 s**, and the hand-over is the first frame the game turns the
screen on -- so what comes up in the window is the title fading in, logo
waving, as it does on the console. A whole windowed launch is 1.1 s. No
button is pressed and nothing is kept on disk.

Checked two ways with `zamn_test_layers --bypass-logos`, which makes the
same boot. With no input, the 500 pictures from the title's first lit frame
are the same files as the 500 from a boot that was left alone (frame 222
against frame 1,156). And a movie that mashes Start from frame 260 is in
level 1 by frame 471 with full health, walks and fires, and the draw-list
test passes over all 1,700 frames.

For a ROM whose `$80:9136` is not that test the older way is still there:
run the whole intro at full speed with Start mashed, and hand over with
START/PASSWORD on screen and no button held.
The input is a rule rather than a recorded table, which is worth saying because
it looked like a table for years: every movie in `movies/` mashes Start at frame
180 and every 24 frames after, held 8 and released 16, up to frame 1004. Checked
against `movies/level1-pickups.zmv` — 71 events, no deviation — so the frontend
reproduces the corpus's boot half from four constants and needs no movie file at
runtime.

It stops at frame 1150, the same figure `tools/make_password_movie.py` uses for
"up and idle", so the two cannot drift apart about when the menu is ready. The
margin is wide (the menu is drawn by 1050 and still sitting there at 1600) and
it is verified rather than assumed: a single Start at exactly 1150 takes you to
the player-select screen.

The frames that are run go through the same substitution the game does, so
they count toward the figures reported at exit. `--skip-intro` cannot be combined
with `-m`: a movie is indexed from reset and carries its own boot half, so doing
both would run the logos twice.

### Starting on a level

```
build\zamn.exe --skip-intro --level 30
```

The game already knows how to start anywhere — that is what a password is — and
it keeps the answer in one word. `$7E:1E7C` is the level number: `$80:885B`
indexes the record table `$9F:8002` with twice it, `$80:8909` steps it between
levels, and `$80:84DB` compares it against 49 to decide the game is over instead
of stepping again. So there is nothing here to implement, only one instruction to
change:

```
$80:85F0  A9 01 00   LDA #$0001
$80:85F3  8D 7C 1E   STA $1E7C
```

That is inside `$80:85CF`, which the main game thread at `$80:84B1` calls once,
above the per-level loop that starts at `$80:84C1`. Change the immediate and the
level card, the load, the victim gate and the step to the next level are all the
game's own and all unchanged, because none of them knows where the number came
from. Two bytes, in `snes->cart->rom` after the load — `cart_load` mallocs its
own copy, so the buffer read off disk is not the one the 65816 fetches from.

**It is two sites, not one, and the second one is the whole story.** Patching
`$80:85F0` alone works, and then stops working for anyone who leaves the menu
sitting there: `$80:9126` runs the attract demo at `$80:9AB0`, the demo plays
real levels off its own list at `$80:9BCF`, and it has to put the number back
when it is done —

```
$80:9BBD  A9 01 00   LDA #$0001
$80:9BC0  8D 7C 1E   STA $1E7C
```

— which happens *after* `$80:85CF` has run and *before* the player presses Start,
and it does not take long: the demo is playing by frame 2000, fourteen seconds
after `--skip-intro` hands over. Measured rather than reasoned about — a movie
that boots to the menu, idles to frame 7000 so the demo plays through, and only
then mashes Start lands on level 1 with one site patched and on the level you
asked for with both. Those two are
every `LDA #$0001 : STA $1E7C` in the cartridge. The other five writes are the
level step, the password routine and the demo's own list, and none of them is
touched — so a password typed at the menu still wins, which is right, because it
is the player asking second. (One exception, below: a bonus room is armed with a
second flag that outlives the password, so `--level 50` and then a password gives
you the room first and the password's level after it.)

**The range is 0..55, which is every record there is**, and the eight that are
not numbered levels are worth knowing about because the cards name them
themselves.

**49 reads CREDIT LEVEL.** `$80:84DB  CMP #$0031 : BEQ $8500` sends the game to
its ending rather than to `$80:8909` when *that* is the level just finished, so
it plays, the game ends, and the title comes back. It is where finishing 48 takes
you, and starting on it needs nothing special.

**0 and 50..55 read BONUS LEVEL** — the seven bonus rooms. 0 is the one the
`BCDF` password loads and it behaves like any other number: `$80:8909` steps 0 to
1, so level 1 follows it, which is what the password does too. The other six are
not in the chain at all. They sit past the count at `$9F:8000` (`$0032`, what
`$80:8909` wraps on), and they are reached a way of their own:

```
$82:D118  LDA $1E7C : ASL : TAX : LDA $D17E,X : BEQ .none
$82:D122  STA $1F50
```

`$82:D17E` is 56 words indexed by level, six of them non-zero: **level 1's door
leads to room 51, 9's to 54, 12's to 55, 17's to 52, 22's to 50 and 33's to 53.**
Walking it puts the room's number in `$1F50`, the level ends, `$80:8909` steps
`$1E7C` to N+1 — and then the loop head reads the flag:

```
$80:885B  LDA $1F50 : BEQ .plain : STZ $1F50 : DEC $1E7C : BRA .index
$80:8868  .plain  LDA $1E7C
$80:886B  .index  ASL : TAX : LDA $9F8002,X
```

The subtlety is in what is *not* there. `STZ` and `DEC` do not touch A, so the
accumulator at `.index` is still the room number this loaded out of `$1F50`: **the
record it loads is the bonus room, and the `DEC` is bookkeeping.** It puts `$1E7C`
back to N so that the step at the end of the room lands on N+1 a second time. A
bonus room costs the chain nothing, which is exactly why it can be dropped
between two levels.

So *"do what it would normally do"* turns out to be one more pair of numbers
rather than a different mechanism. To start on room B: put N+1 in `$1E7C` and arm
`$1F50` with B, and the game's own `$80:885B` does the rest — the room loads,
`$1E7C` comes back to N, and finishing it goes to N+1 exactly as walking the door
would have.

Arming `$1F50` needs somewhere to arm it *from*, and the window is narrow. It has
to be after the title menu, because the attract demo lives inside the menu and
goes through `$80:885B` itself — a flag set before it would be eaten by the demo,
which would play the bonus room and leave the player with a stale one. And it has
to be before the first pass of the per-level loop. There is exactly one
instruction in that window, and it is a call:

```
$80:84B1  JSL $8085CF   the init above -- level number, victim gate
$80:84B5  JSL $809126   the title menu, and the demo inside it
$80:84B9  JSL $8088A9
$80:84BD  JSL $808618   <- here
$80:84C1  JSL $80885B   the loop head, which reads $1F50
```

`$80:84BD` is redirected to an eleven-byte stub that makes the call it displaced
— copied from the call site rather than written out — and then does the store.
The stub goes at `$80:FF68`, which is 88 bytes of `$FF` between the last code in
the bank and the cartridge header at `$80:FFC0`. It is the only thing written
outside the two immediates, it is written only when a bonus room is asked for,
and it is refused unless the pad is still a pad.

Three things were checked rather than assumed, all with `$80:8516` — play the
level — stubbed down to `CLC : RTL` in a scratch ROM so that every level completes
the instant it loads, and `$1E7C` watched the whole way:

| asked for | `$1E7C` goes |
| --- | --- |
| `--level 30` | 30 → 31 → 32 |
| `--level 47` | 47 → 48, into the level-48 finale at `$80:8BBB` |
| `--level 50` (room off 22) | **23 → 22** as the room loads → 23 → 24 → 25 … |
| `--level 55` (room off 12) | **13 → 12** as the room loads → 13 → 14 → 15 … |
| `--level 0` | 0 → 1 → 2 → 3 … |
| `--level 49` | 49 → the ending → the title → 49 again |

The two bonus rows are the whole claim in one line each: the room is visited
once, `$1E7C` lands back on the level its door would have led out to, and the
chain carries on from there. **And it costs nothing when it is not asked for** —
`--level 1` writes the stock value back and installs no stub, and 2400 frames of
`movies/level1.zmv` come out a byte-identical PNG with the flag and without it.

`--level` applies to every new game the session starts, including the one after a
game over — `$80:8514` branches back to `$80:84B1`, so a game over puts you on
your level rather than on level 1, which is what a flag for looking at level 30
should do.

### The line along the bottom as the game over begins

Reported in play-testing: the bottom of the screen flashes as the game over
comes up. It is one frame, one line -- the last of the 224, orange from edge
to edge -- and it is the game's own: `--stock` in 4:3 draws it too. The
game over's mask is a second tilemap for BG3, and `$80:8A78` sets it up a
vblank job a frame: the map, then BG3's map register and the tiles
(`$8B2B`), then the rest of the map, then the job that writes BG3's scroll
(`$8B70`, to -32), then the colours. Between the second and the fourth, BG3
is the mask's map at the status panel's scroll of 0, where the screen ends
one line into row 28 of it -- the top edge of the mask's field, in the
panel's colours. A television of 1993 had that line in its overscan.

`maskline_fix` (`src/maskline.h`) exchanges the operands of those two
`LDA`s in the cartridge's copy of the image, so the scroll job is running
before BG3 is given the mask's map. There is nothing for the early scroll
to move: the panel's own map is blank by then, every word of it. The same
five jobs run on the same five frames. Always on in the frontend;
`zamn_test_layers --mask-line` makes the same change, and over frames
2,540-2,760 of the poked game over in 16:9 (1,105 pictures) the only ones
that differ are the five of frame 2,559, in their last line.

### The game over's blood, and `--red-blood`

On the Super NES the game over is "GAME OVER" cut out of a curtain of purple
slime; on the Mega Drive the curtain is red. `--red-blood` makes it red here,
after the ROM hack *Bloody Disgusting Edition* (romhacking.net, hack 4306),
of which only the description was read. It is off unless asked for, changes
the picture and nothing else, and so may be given with `-m` too.

The curtain is two things, and they are reddened two ways (`src/blood.h`):

  * **The mask on BG3.** Its colours are in no palette: `$80:8B82`, a vblank
    job the game over installs, writes CGRAM 25-27 every frame from six
    immediates -- `$5953`, `$348A`, `$1C26`. The six operands are rewritten
    in the cartridge's copy of the image when the machine is built (never
    the file; and only if each `LDA #` and each operand is this ROM's).
  * **The drips, which are sprites**: the metasprite `$8F:E9A7`, frames
    `$A63`-`$A65` in sprite palette 5, using its colours 9, 11 and 12 -- the
    same three words. Those are other things' purple too (a scan of the
    metasprite banks finds them in 183 of the 209 frames drawn in palette
    5; `$B31`-`$B34` are a spider), so
    reddening the palette would redden whatever walked behind the letters,
    and palette 5 has no blood red of its own to repaint the tiles in. So
    the drips are recoloured a sprite at a time. `blood_frame`, from the
    frame hook, marks the OAM entries that are palette 5 with a tile the
    frame cache (`W_FRAME_SLOT`) says holds one of the three frames; for a
    marked entry `Ppu.objRemap` sends pixels 9, 11 and 12 to CGRAM `$C0`,
    `$D0` and `$E0`, and `blood_frame` puts the reds there. Those are colour
    0 of sprite palettes 4-6, which is transparent, so no pixel ever reads
    them. `ppu_evaluateSprites` and the draw list's sprite atlas both honour
    the remap, so the pictures between ticks are red as well.

The red is one function for both (`blood_red`): the larger of red and blue
becomes the red, times 13/8, and the green is halved -- `$348A` (10,4,13)
comes out (21,2,2). Measured on `level1.zmv` with the player's health poked
to zero (`--poke 1900+:1CB8=0000`), stock against `--red-blood`: of the
1,705 pictures of frames 2,560-2,900 (16:9 to 2,760, 4:3 after) 1,610 differ,
from frame 2,579 on, and of 455 more at frames 3,200-3,290 all do; in every
one of them each pixel that was one of the three purples is its red, no
purple is left, and no other pixel differs -- the zombies in the margins
are as they were. The drips were marked on 394 frames. `zamn_test_layers` takes
`--red-blood` and still finds the draw list's pictures equal to the PPU's,
and `--png` now also writes `prefix.frame.cgram.bin` (CGRAM, OAM and its
high table), which is how the colours were found.

### A longer reach for pickups and weapons, and `--hitbox`

The game decides who is touching whom with a 16x16 box: two records touch
when they are within 8 pixels on both axes (`$80:BEC9`, the overlap pass, at
`$80:BEF1`), and that is tight for walking over a first aid kit and for a
shot that visibly clips a zombie. The frontend plays at **150%** of it by
default -- 12 pixels -- and `--hitbox <100..200>` says otherwise; 100 is the
game's own, and is the default under `-m`, since every movie was made at it
and picks things up a step sooner at any other.

*Zombies Ate My Neighbors DX* does this by patching the two constants in
`$80:BEF1`, which widens the box for every pair there is -- so a zombie also
touches the player, and a neighbour, from half as far again. Here the longer
reach (`actor_overlap_reach`, `src/port/oam.h`) is for the pairs a player
wants to touch and the rest keep the game's 8:

  * a player (`$05`, `$06`) and a pickup -- the thirty collision ids of
    `$80:CA30`, the table that makes an object's type its id;
  * a player and a neighbour (`$01`, `$02`);
  * a player's weapon (`$5C` and up, under the `$7FFF` mask) and a creature,
    which is whatever is none of the above.

The ids were sorted by logging every pair the overlap pass dispatched over
the fifty movies. And the overlap pass turned out not to be how a weapon
usually finds a creature: a shot asks `$80:BF1B` who is in the 16x16 box
around it every tick (`$80:D413`), and a weapon held in the hand asks about
a box in front of the player (`$80:F055`) -- the first kill of
`level1-rescue.zmv` never goes through the overlap pass at all. So in
`actor_notify_box`, when the asker is a weapon, a *creature* is tested
against the box grown by the same number of pixels on every side, and
anybody else against the box as asked for.

It lives in the port's two routines and the ROM's are untouched: `--stock`
plays at 100, a call the port declines is the ROM's at 8 for that frame,
and every tool but the frontend leaves the reach at 8, where both routines
are the ROM's to the byte (`verify_corpus.ps1` unchanged). Measured with
`zamn_test_layers --hitbox <pct> --watch <addr>`: in `level1-pickups` both
pickups land two frames sooner at 150 (walking straight at them; the gain
that matters is the pass that would have missed by a pixel), the first kill
of `level1-rescue` two frames sooner, and in `level9-weapons` the first
kill lands at frame 3,484, where at 100 there is none until 5,368.
The rescue in `level1-rescue` lands on the same frame at both.

### Quick save and quick load: F5 and F9

F5 saves the game where it stands and F9 goes back to it. There is one save,
the latest: `<rom>.quicksave` beside the ROM (400 KB), so it is still there
the next time the game is started. The word SAVED, LOADED, NO SAVE or SAVE
FAILED shows at the top right for a moment -- five-by-seven letters out of
rectangles, drawn last on whatever drew the picture -- because a save
changes nothing on screen and the console is behind a fullscreen window.

The core could already save and load a machine (`snes_saveState`). What is
played here is more than the core, and `src/quicksave.h` carries the rest:

  * **The harness.** A substituted routine whose cycle budget is part spent
    when a frame ends is resumed by the harness, and a save state knows
    nothing of it. A save waits for a tick that `cosim_idle` says ends clean,
    which nearly all do, and a load makes the harness forget what it held.
  * **The widescreen's books** -- its tick-old copy of work RAM, and the
    sprite cache slots it has borrowed and owes back. The saved video memory
    has the borrowed graphics in it; with the list restored, the next frame's
    hook returns them as it always does.
  * **The sprite pass's owner tables**, which the smoothing pairs sprites by
    and the widescreen places the radar's and the game over's sprites by.

A key only asks. The machine may be running its next tick on the emulation
thread when the key comes, so both are done in the tick block, after the
last tick has been collected and before the next is started. The smoothing
does not ease across a load: the tick that was on screen is marked as
nothing to ease from when the first loaded tick comes to be linked to it.
What belongs to the launch is not in the file -- the patches in the
cartridge's copy (`--level`, `--red-blood`, the logo bypass), the margin,
the scaling -- so a save made in 4:3 loads in 16:9. The top scores are not
rolled back: after a load the file's table is put over the machine's. A
file that is not a save of this ROM by this build loads nothing, and both
keys are off while a movie plays.

Checked with `--quick-at frame:save|load[:file]`, which presses the keys
from the command line. `level1.zmv` saved at frame 1,900 and again at 2,000;
a second run, of a movie that presses nothing, loads the first save at frame
300 -- in the middle of the Konami logo -- and saves at 400. The two later
saves are the same 407,448 bytes, which is the core, the widescreen's books
and the owner tables all at once: in 16:9 with the smoothing on and the
machine on its thread, in 16:9 without, and in 4:3 with `--stock`. The
pictures either side of that load are the logo and then the level, with no
picture between that is both. A `--stock` 4:3 save of `level9-weapons` loads
into a native 16:9 `--red-blood` run and plays on.

### The top scores are kept, and `--no-high-scores`

The cartridge has no save RAM (header type `$00`, size 0), so on the console
the top scores are the ten developers' again at every power-on. The frontend
keeps them: `<rom name>.hiscore` beside the ROM, 198 bytes, written the tick
the game files a score and put back at the next launch.

The table is work RAM and nothing else -- ten 15-byte rows of text at
`$7E:2064` (name, `/` padding, the score's digits, a 0), ten 32-bit BCD scores
at `$7E:20FA`, and a word at `$7E:2124` that `$80:85F6` tests on the way into
the title: zero, and `$82:BB0D` copies the table out of the ROM and sets it.
`$82:BBED` is the game over asking whether a score beats the tenth, `$82:BC41`
the name entry and the insertion. `src/hiscore.h` works between ticks with the
machine stopped and the game is not told: once the flag is up the saved table
goes over the game's (after the game's own copy, never before it, and once a
run), and from then on a table that differs from the file is written -- beside
itself and moved over, so a killed run leaves the old table. Only what the
game could have made is read or written: fourteen characters and a 0 a row,
scores BCD and in order; a file that fails that is reported and left.

Off under `-m` -- a movie's picture should not depend on yesterday's play, nor
its scores join the player's -- unless `--high-scores <file>` names a file,
which is the test: `-m movies/level1.zmv --poke 1900+:1CB8=0000` (health held at
zero) with the score poked to 7,654,321 reaches the real game over, waits out
the name entry and saves at about frame 5,000; the next launch prints
`Top scores: restored ... (best 07654321)`. Holding health at zero on a movie
that only sits on the menus kills the *attract mode's* player, and that game
over (`$80:9B87`) goes to the top scores without asking `$82:BBED` anything.

### Cheats

Six, each off unless asked for by its flag or by `zamn.ini`'s `[cheats]`, where
the name is the flag's with underscores (`src/cheats.h`). A flag beats the file,
and `--no-<flag>` turns one off that the file has on. The launcher's Cheats tab
is the same six. The file's are not taken under a movie:

| flag | what it does |
| --- | --- |
| `--invincible` | Nothing hurts a player: no flinch, no sound, no health lost. |
| `--invincible-neighbors` | Nothing hurts a neighbour, and the tourists do not turn into werewolves. They can still be rescued. (`--invincible-neighbours` is the same flag.) |
| `--infinite-ammo` | Weapons and items are never used up, keys included, and the HUD always shows the most the game lets anybody carry: 999 of a weapon, 99 of an item. It gives nothing: a weapon not held stays not held. |
| `--infinite-lives` | Dying does not cost a life. |
| `--give-all` | All fourteen weapons and ten items, 999 and 99 of them, when a game starts and when a quick save is loaded. Once: they run out unless `--infinite-ammo` is on as well. |
| `--always-run` | The running shoes, always. |

**Three kinds of thing, chosen by who runs the code.** A routine the port has
taken over is C and no longer reads the cartridge's instructions; one it has
not is the 65816's in a native run as much as under `--stock`. So most of a
cheat is what a cheat cartridge does -- a byte or two of the *loaded* image,
checked against what this ROM has before any is touched, all or none -- or a
word of WRAM written once a tick where `--poke` writes. Two routines are the
port's own, a neighbour's collision handlers and one entry of the player's,
and for those there is a flag in the port (`src/port/cheat.h`) beside the patch
to the cartridge's copy, so F1 and `--stock` play the same.

* **Invincible.** Every way a collision hurts a player but two opens with
  `LDA $52 : BPL <rts>`, the recovery timer a hit sets to `$40`: `$80:F950`
  (ids 3, 4, 9), `$80:F979` (a Martian's bubble), `$80:DC09` and the floor's
  `$80:F935`. Nothing else reads it -- it is not what makes a hurt player
  flash -- so it is held at `$40` and no hit ever lands. The two that do not
  ask, `$80:F9BE` and `$80:F999`, lose a `STA` and become an `RTS`. Health is
  held at ten besides, for a potion that turns out to be poison.
* **Neighbours.** `$83:A364` latches a neighbour's fate from the first
  collision to reach it: 5 and 6 are the players, `$FF` clears it away, and 3,
  4, 9, `$0B` and `$34` are its deaths. Those five fall through to the
  routine's own `CLC : RTL`; the same for a bubbled neighbour's handler at
  `$83:A264`; and `$83:A012`, the tourists' `BNE` on the moon, goes.
* **Ammo.** The game spends in five places, all `SED : SEC : SBC #$0001 : STA`
  and none of them the port's -- a weapon fired, an item used (twice), a key
  and a skeleton key. Each `#$0001` becomes `#$0000`, so nothing is ever spent
  and the HUD never sees a count dip; once a tick what is held is raised to
  `$0999` or `$0099`, the ceilings the pickups themselves keep.
* **Lives.** `$80:CEC5  DEC $1D4C,X` goes. `$7E:1D4C` is the lives, two at the
  start and counted down to a `BMI`.
* **Give all.** Two of the twelve item slots are left out, 6 and 11 -- an
  orange flask and a thing with an aerial. They have icons, no id that picks
  them up (`$27` and `$2C` are the first-aid pickup and a dead player's keys)
  and a bare `RTS` for a use. Given the tick a player's inventory is exactly a
  new game's (`$80:8874`: a squirt gun of 150 and a first-aid kit), and the
  first tick a player is on the board after the start or a quick load -- and
  only to a player who is in the game, by the HUD's panel flags
  (`$7E:1E88`/`$1E8A`, raised at the character select, before the seeding).
  The game seeds player two's inventory in a one-player game too, and filling
  it reached the HUD: the ghost potion (`$80:DACB`) selects weapon 17 and item
  15, past both inventories, to draw its blue flames, and the count the HUD
  then tests for the weapon is player two's second weapon slot read through
  player one's table. The HUD redraws a count only when that word changes, so
  a 999 there left the old 999 under the flame, where the console reads a
  nought and blanks it. Reported in play-testing; `zamn_test_cheats` covers
  the flag.
* **Always run.** `$54 = $8000` on the player's page is the shoes and `$56`
  their countdown; a `$54` of zero is set every tick and the countdown left at
  nothing. `$C000` is left: `$80:D3A8` sets it in the state one of the mystery
  potion's draws puts a player in (`$80:DB42`), which is not the shoes. The
  monster itself (`$80:D9A3`) clears `$54`, and so runs too.

A player's page is found from `$D2`/`$D4` (their display record, zero when
they are off the board), the record's thread at `+$0C` and the table of pages
at `$80:82DE`, and believed only if `$0E`, `$64` and `$66` there say it is
that player's.

**The demo is left alone.** The title's demo is a recording played into a real
level and it ends when the last neighbour is gone -- eaten, most of them. With
the neighbours safe it ran until a button was pressed (16,000 frames and
counting, against 12,113). So while the job that plays the recording is filed
(`$9CB1`/`$0080` in the eight at `$7E:12E0`) the patches come back out, the
port is told nothing and no word is held, and what `--give-all` owes is kept
for the game that follows.

**The top scores are read and not written** while any cheat is on, and a movie
played with one says that it will not meet the game it was made against.

Checked by `zamn_test_cheats` -- the patches into an image and byte for byte
back out of it, a wrong image refused whole, every hold and every thing a tick
must leave alone, the demo, and the port's handlers with the flag on and off,
against a cartridge built to the header's description and against the real
one -- and in play, under the stock core (`zamn_headless` takes the same six
flags, for `--watch`) and natively: `level5` with `--invincible` never sees
`$1CB8` leave ten where the plain run loses three lives; with
`--infinite-lives` it dies five times on two lives; `level9` loses a neighbour
at frame 4224 and `level21-bubble` one at 2869, and neither does with the
cheat, while `level1-rescue` still rescues; the squirt gun reads 999 from the
tick the game is dealt and never moves; and with the shoes `level1`'s player
is at x 114 by frame 1800 where the plain run's is at 232. `verify` on
`player_collide`, `victim_collide` and `victim_a264` still finds nothing, the
flags being off.

### Twin-stick shooting, and `--no-twin-stick`

```
build\zamn.exe                    # right stick aims and fires
build\zamn.exe --no-twin-stick    # right stick does nothing, as on the console
```

The right stick fires the held weapon in the direction it is pushed, and the left
stick goes on steering — so you can walk one way and shoot the other. It needs a
controller; the keyboard has one D-pad and is unaffected either way.

**On by default**, which widescreen is not, and the difference is what each one
takes away. Widescreen draws columns the console never drew, so it is on screen
whether or not anyone wanted it. This claims a stick the stock game does not read
at all: leave it centred and the cartridge is byte for byte the one that shipped
— a 4,600-frame movie renders to the same PNG patched and unpatched — and the
keyboard never writes an aim. So the player who wants none of it pays sixty
cycles a frame and no behaviour, and does not have to know the flag exists.
`--no-twin-stick` skips the patch entirely.

`--twin-stick` still parses, and it is not a synonym for the default: asking for
it makes a cartridge that cannot take the patch an error, where the default
settles for a note on stderr and starts the game anyway. A default has no
business refusing to run a ROM it was never told to change.

**The game already separates walking from facing**, which is what makes this one
word rather than a rewrite. The NMI puts the D-pad nibble through a sixteen-byte
table at `$80:81F9` into `$0072,X` — not four bits but a *direction code*, the
eight compass points doubled and offset by two so they index the nine `(dx,dy)`
pairs at `$82:B7FC`. Then the player's own frame splits that one code in two:

```
$80:D250  LDA $0072,X : STA $24 : BEQ + : STA $26 : +
```

`$24` is this frame's direction, `$26` is the last non-zero one, and they are
read for different things:

| word | who reads it | what it decides |
| --- | --- | --- |
| `$24` | `$80:D4E9  LDA $24 : BEQ`, and `$80:E450  LDA $24 : TAY : … ADC $30` | standing still or walking, and the step itself — **movement** |
| `$26` | `$80:ED54  LDA $26 : STA $04`, the argument `$80:ED30` hands `thread_spawn` | **where the shot goes** |
| `$26` | `$80:D526`, `$80:D743`, `$80:ECD8` | the idle, walking and firing poses — **which way you are drawn** |

So aim and facing are already the same word, and it is already not the word
movement uses. Twin-stick is `$26` getting its value from somewhere other than
`$24`; the shot direction, the sprite and the firing pose all follow for free,
because all three were reading that word to begin with.

**The store has one place it can go.** After `$80:D250`'s own conditional store,
or a frame spent walking would overwrite the aim with the walk; before `$80:ED30`
reads it, which is later in the same thread step; and once per player, because
the two have separate direct pages. The latch is all three at once — it already
runs per player with the doubled index in `X`, and nothing touches `X` between
`$80:D206  LDX $0E` and there. So the nine bytes at `$80:D250` become a `JSR` and
six `NOP`s, and the nine displaced bytes are *copied* into a stub in the
end-of-bank pad, which adds the override:

```
LDA $0072,X : STA $24 : BEQ + : STA $26     the nine that were there
+  LDA $80FFBC,X : BEQ ++ : STA $26         and the aim on top
++ RTS
```

**And the cartridge is only half of it, which is the thing this got wrong first.**
`$80:D1FF` is one of the 114 **substituted** routines, so in the playable build
the C port runs the player's frame and those nine bytes are never executed — they
are the `--stock` path and the F1 path. The first version of this was the ROM
patch alone and it passed everything it was shown: `zamn_headless` walked left
and shot right, because headless has no port in it to substitute. In the game it
did nothing. `src/port/player.c` now makes the same decision from
`player_set_aim`, the frontend arms both from one value, and the two agree by
being the same sentence twice — store the aim in `$26` instead of the walk.
Unarmed, the port is bit for bit the routine it was: `zamn_cosim verify` checks
`player_state_normal` 1,912 times over `level21-spin.zmv` and passes 1,912.

`$80:FFBC` is two words, one per port, and the frontend writes them before each
frame. That is a strange place to keep input and it is the right one: `cart_load`
mallocs the ROM and the 65816 fetches out of that buffer, so writing it works
exactly as writing WRAM would and needs no argument about which WRAM address the
game will never use — an argument that would have to hold for all 56 levels, both
players and the attract demo. Zero means nothing is asked for, which is the
game's own convention for this word and not one invented here: a centred stick
snaps to no octant, no octant is nibble 0, and nibble 0 is the table's own `$00`.
It shares the pad with `--level`'s bonus-room stub at `$FF68`, does not overlap
it, and each refuses unless its own bytes are still `$FF`, so the two flags
compose in either order.

**Measured** on `movies/level21-spin.zmv`, which walks the four directions in
30-frame legs with `Y` held, against a scratch ROM with the aim word forced to
`$02` (up):

| frame | leg | `$24` (walk) | `$26` stock | `$26` aimed up |
| --- | --- | --- | --- | --- |
| 3175 | left | `$0E` | `$0E` | **`$02`** |
| 3205 | up | `$02` | `$02` | **`$02`** |
| 3235 | right | `$06` | `$06` | **`$02`** |
| 3385 | down | `$0A` | `$0A` | **`$02`** |

Stock, the two columns are the same word all the way down — that is the coupling.
Aimed, `$24` still tracks every leg and `$26` never moves. Positions are
identical either way: x goes 316 → 268 in both, so the walk is untouched. The
picture agrees — one frame of the left-walking leg, with the aim forced right,
draws Zeke still moving left while facing right, pistol out to the right and the
squirt travelling right.

That is the ROM path. The port path is checked where it lives, by calling
`player_state_normal` directly with the aim armed: walking left and aiming right
leaves `$24` left and `$26` right, standing still and aiming leaves `$26` on the
aim rather than the last walk, disarming puts it straight back, and player 2's
stick does not aim player 1.

**And then the two are checked against each other**, which is what `verify` is
for and what two implementations of one routine deserve. `zamn_cosim verify
--twin-aim <period>[,<from>]` patches the cartridge, arms the port the same way,
holds fire and cycles the aim through left, right and centred — so the ROM runs
the stub, the port runs its branch, and the harness diffs all 128 KB and the
registers on every call:

| movie | `player_state_normal` calls | passed |
| --- | --- | --- |
| `level1.zmv` | 2,448 | 2,448 |
| `level21-spin.zmv` | 1,977 | 1,977 |
| `level17-weapon.zmv` | 2,007 | 2,007 |
| `level29-fighting.zmv` | 2,023 | 2,023 |
| `level33.zmv` | 2,079 | 2,079 |

The first run of that probe **failed**, and on one bit: the stub compared with
`CMP $26`, which writes carry, and the routine's carry at the `RTS` belongs to a
`CPY` a long way further up that the port reproduces faithfully. `EOR $26` sets
Z without touching carry, and a `PHA`/`PLA` around it puts A and N/Z back to the
aim on every path out. Two engines that agree except in the flags do not agree,
and nothing but this check was ever going to say so.

**The monster punches where the stick points as well.** The monster potion (and
the mystery potion, one draw in eight) puts the player in state 1 of the eight
at `$80:D1EF`, `$80:D2EA`: the same idle and walk code as state 0, sharing `$28`,
`$24` and `$26`, but a frame that reads no weapon and latches `Y`, `B`, `A` and
`X` into `$6C` instead, which `$80:D51F` and `$80:D678` make a punch of --
`$80:EF67` standing, `$80:D6DC` walking, both drawn and landed from `$26`. Its
latch at `$80:D2FD` is the nine bytes above with a `BRA +0` after them, and now
they become a `JSR` to the same stub and eight `NOP`s. Before this the stick
pressed `Y`, the monster punched, and it punched the way the D-pad had last
pointed. State 1 runs on the 65816 in every build -- `$80:D1FF` is the one
player state the port has -- so the cartridge patch is the whole of it, and
`zamn_headless --aim frame[+]:dirs` now drives the stub under the stock core.
On a movie that drinks the potion under `--give-all`, the monster stands facing
down (`$26 = $0A`) until the stick goes right at frame 2960, faces right on 2961
and punches that way, faces up on 3105 when the stick does, and walking right
into the hedge with the stick up keeps `$24` at `$06` and `$26` at `$02`. The
frontend, fed the same stick, agrees frame for frame.

### Standing still, the pose is drawn once

The last thing to go wrong, and the neatest illustration of what a second stick
breaks. The idle state builds the player's pose from `$26` **on entry** and then
parks on a resume routine that only rebuilds it when the *button word* changes:

```
$80:D53D  LDA $1A : CMP $1C : BNE $D558   -> JMP $D4E9, which rebuilds
$80:D543  LDA $4C : BNE rts               -- the cooldown
$80:D547  LDA $1E : BNE $D554             -> JSR $ED30, and nothing else
```

Stock that is airtight: the only way to change `$26` is to press a direction, and
that is a button. With a right stick it is not, so a player standing still
flicking the aim from left to right shot right while still drawn facing left.
Walking hid it — `$80:D72A` rebuilds the pose from `$26` every five frames to run
the walk cycle, so it is right again inside 83ms.

The repair takes the game's own route: `$28` is the word the thread loop
dispatches through each frame, the idle state parks `$D53D` there, and `$D4E9` —
what `$80:D558` jumps to when the input changes — put there instead re-enters the
state and rebuilds the pose. It is done only when **`$24` is zero and `$26` is
not already the aim**: walking refreshes itself and would stutter if the state
restarted under it, and `$26` already equalling the aim means the sprite is
already right, which is what makes the test stateless — `$26` *is* the facing, so
comparing against it asks the question directly instead of remembering last
frame's aim.

**And it costs nothing when nobody is using it.** A `JSR`, an `RTS`, a long
`LDA`, a taken `BEQ` and six `NOP`s twice a frame, against 1,364,000 cycles — with
both words zero the stub is byte for byte the routine that was there, and 4,600
frames of that movie through the frontend come out a byte-identical PNG with the
flag and without it.

`tools/test_twinstick.c` checks the two halves separately and needs no ROM: nine
stick positions becoming nine direction codes, all eight of them different — a
nibble built the wrong way round still produces plausible codes and would aim up
when asked for right — and then the patch as exact bytes against a synthetic
cartridge, including that every refusal writes nothing at all. Hand it the real
cartridge as an argument and it checks the offsets against that too. The stick
becoming an aim *and* a fire button is one call, `twin_apply`, so that pair is
checkable rather than living in a line of the frontend nothing could reach: a
pushed stick presses `Y`, a centred one presses nothing and hands `$26` back to
the game, and port 2's stick does not fire port 1. `tools/test_pad.c` covers the
other end through a virtual controller — the right stick must not reach the
D-pad and the left stick must not reach the aim, which is the leak that would
turn walking one way and shooting the other back into walking and shooting the
same way.

### What actually reaches the screen

The core hands over 512x480, but **only 512x448 of it is picture**.
`ppu_putPixels` doubles the game's 224 scanlines into rows 16..463 and zeroes
sixteen rows top and bottom, so scaling the whole buffer spends 6.7% of the
screen enlarging black and then letterboxes *that*. Only the live rectangle is
drawn.

Those 448 rows are not square pixels either. The console puts 256 across a frame
a television showed at 4:3, so the game is composed for 4:3 and square pixels
make it 8:7 — visibly narrow, and 11% less screen. So the picture is always
shown at 4:3. Square pixels used to be a choice, `--aspect square` and F3, and
were taken out as not worth having; `aspect` and `toggle_aspect` in an old
`zamn.ini` are read and ignored. At 3840x2160:

| | picture | of the screen |
| --- | --- | --- |
| whole buffer, square pixels | 2304x2160, 144px of it blank top and bottom | 56% |
| cropped, square pixels (8:7) | 2468x2160 | 64% |
| cropped, 4:3 | **2880x2160** | **75%** |

### `--widescreen`

`off` (default), `16:9`, `16:10`, `21:9` or `auto`. **F4** cycles them while
the game runs.

`auto` is whichever of the four fills the display best while fullscreen, and
off in a window: 16:9 on a 16:9 panel, 16:10 on a 16:10 one, off on a 4:3 one,
21:9 on an ultrawide, and 21:9 on anything wider (`wide_for_display` in
`src/scale.h`, pinned in
`tools/test_scale.c`). It is asked every frame, so F11 takes the picture to
the console's 256 columns on the way into a window and back out to the edges
on the way into fullscreen, and a fullscreen window sent to another monitor
takes that monitor's shape. A window stays at 256 because it is sized from the
picture, so following its shape would only follow itself. Mind that the width
changes the game a little as well as the picture (a neighbour in the margin
is real; see *The sprites the game throws away* below), so a movie played
under `auto` plays at whatever width the display gives it.

Not a stretch and not a crop: the PPU draws columns either side of the
console's 256, so a wider screen shows *more of the level* at the same size.
That is possible because almost nothing had to move to allow it. The game's
coordinates are untouched — column 0 is still column 0 — and the three things
that would normally break turn out already to be in the port's favour:

* **The map is already there.** BG2's tilemap is 64 tiles across, twice the
  screen, and the streamer keeps the columns around the camera valid rather
  than only the visible ones. 16:9 wants 43 columns a side and the ring has 128.
  21:9 wants 96, and 192 on one side at the end of a map, which still fits
  once only what the picture reaches is filled (see *21:9* below).
* **The actors are already alive.** `actor_cull` keeps anything from 128 px
  behind the camera to 383 px ahead of it (`src/port/oam.c`) — a 512-pixel
  window around a 256-pixel screen, which is twice what 16:9 asks for (21:9
  asks for more at the end of a map; see *21:9* below). So
  nothing pops in at the new edges and no culling, animation or collision code
  changed at all. Checked rather than assumed: dumping the display list through
  `zamn_headless --records` on level 29 catches a monster walking from world x
  905 to 1007 across twenty frames while the camera sits at 737 — from inside
  the console's 256 to 14 pixels beyond it — still listed, still flagged to
  draw, still moving. Being alive is not the same as being drawn, though; see
  *the sprites the game throws away* below.
* **The sprites can reach.** OAM's X is nine bits spent as −256..255, so a
  sprite at 256 is one hanging off the *left* edge. Widescreen moves that wrap
  point out to the new right edge and leaves everything past it wrapping, which
  is what keeps a sprite walking off one side doing so.

A game pixel is 7:6 — 256 across a 4:3 frame over 224 rows — so 224 rows want
`ratio × 192` columns: 4:3 gives back exactly 256, 16:9 wants 342, 16:10
wants 308 and 21:9 exactly 448. `src/scale.h` states the pixel shape once and
derives them all, which is why the 4:3 cases pinned in `tools/test_scale.c` did
not move.

**21:9** is 96 columns a side, and it is the widest there is. At the end of a
map one side takes both margins, 192, so `PPU_EXTRA_MAX` is 192 rather than
128. Two of the game's distances that were slack for 16:9 run out there, and
`src/widescreen.h` moves them ("21:9 is wider than the game's own reach"). The
world's tilemap ring is 64 columns, and filling the widest either margin can
get on both sides at once came to 80, so only what the picture reaches is
filled now, with the smoothing's 16 columns either side: 62 at most. And
`actor_cull`'s 128 behind the camera stopped short of a picture whose left edge
was 192 behind it, so its two horizontal words in the ROM are moved out to 32
past the picture's edge when the edge passes them. 16:9 never reaches them, and
200 frames over five movies at 16:9 are byte-identical to the build before.
32:9 would be 684 columns, wider than one lap of a sprite's nine-bit X and of
the game's 512-pixel planes, so it would take more than a bigger number.

**The status panel moves to the edges.** It is drawn on BG3, whose tilemap is
32 tiles wide — one screen exactly — so continuing it into the margins could
only repeat it, and it did: a second health bar at each edge. But the panel is
already two half-width halves, player 1 in the left sixteen columns and player 2
in the right sixteen, so the answer is to pin each half to its own edge and
leave the gap between them empty. That is `ppu_wideAnchor`, and it needs no
change to the shadow tilemap the co-simulation checks byte for byte.

The same layer carries the title, the password screen and the story cards, and
those must *not* be torn in half. What tells them apart is the layer next door:
a level sets BG2's tilemap to 64 tiles wide because the world scrolls, and a
fixed screen does not. That sentence, the map's own width and the sprites below
are the whole of what `src/widescreen.h` knows about this game, and no screen is
named anywhere in it.

**A tilemap's width outlives the screen that asked for it**, though, which is
why that is half the test rather than all of it. `$80:9E5C` sets BG2SC when a
level loads and nothing puts it back to 32 when the level ends, so the LEVEL
COMPLETE tally and the card naming the next level are drawn with a 64-column BG2
still configured behind them. Asking only about the width called them levels,
and a fixed screen leaves `$1B6A` at zero, where the sliding margins below give
the left margin's whole share to the right one: the first card of a run was
centred and every card after the first completed level was jammed against the
left edge of a 16:9 frame. The other half of the test is that a fixed screen
also switches BG2 off the *main screen* (`$212C`) while a level leaves it on --
through the map screen, through a boss, through every frame of all 49 movies in
the corpus, where BG2 is wide-but-unshown in exactly the two movies that finish
a level and only across their cards. So both registers are asked, and the answer
is about the picture being drawn rather than about a register left over from the
last one. At 4:3 nothing moves: the same seven frames of `level21-exit.zmv`
render byte-identical either side of the change, and at 16:9 so does every frame
outside the 665 the cards occupy -- including the last frame of level 21 and two
from level 22.

**Everything else runs on `ppu_wideAuto`, which asks the layer.** Its default
answer is the honest one: draw what the hardware would have drawn if the
scanline were longer. A background is a tilemap and a scroll, both defined at
any x; the console stops at 256 because it runs out of time, not because the map
runs out. Four things take a layer off that default, and they are asked in
this order.

**Is it being drawn a line at a time?** A layer whose horizontal scroll is
rewritten on every scanline is not scrolling; it is a raster effect, and each
line is a window on the map at its own offset. Every question below reads one
scroll for the whole frame and would answer it for the wrong line, so none of
them is asked. This is the title logo, which sweeps in on a per-line scroll and
is twice as wide as the console: read one scroll for it and its edge columns
come out empty, which had the whole animation clipped at the console's edge with
the spiral carrying on past it either side. Continuing each line along its own
map is the only answer that means anything, and it is also the honest one.

Measured rather than assumed: over the intro and a level, an ordinary layer's
scroll changes at most four times in a frame and the title logo's changes on all
224 lines, so the threshold sits in a gap two orders of magnitude wide. Latched
per screen and cleared behind a forced blank, exactly like the scroll test
below.

**Has it anything at the console's edges at all?** A layer whose outermost
columns are entirely transparent is a picture composed to be seen at one place —
a logo, a card, a screenful of legal text — and the margins beside it belong to
whatever is behind it, which is what its own edge column is already showing.
Those are clipped. The test is made in *pixels*, not in tilemap words, and that
distinction is the whole of it: two different blank tiles are two different
words and the same nothing, and this game uses both. The LucasArts logo layer
pads its edges with palette 2's blank tile and the legal screen pads its edges
with palette 7's, so a test that compared words called them textured and
repeated them — which is how a screenful of legal text came to be printed three
times.

That same test is what stops the LucasArts caption repeating. The wall and the
words are not one layer after all: the wall is BG3 and the logo with its
"LucasArts Entertainment Company" line is BG1, whose edge columns are blank. The
wall fills the margins and the caption stays where it was written, once.

**Is its map 64 columns with only 32 maintained?** Then reading further along it
reads whatever that VRAM was last used for — the LucasArts wall came out
shredded that way — so those repeat the 256 columns the game does maintain. A
level's world is 64 columns *and* maintained, and is told to stretch from
outside rather than reaching this.

**Has the game ever scrolled it sideways?** This one only matters for a map that
is 256 pixels across, the width of the console exactly, where "carry on reading"
means "wrap". That is right for a layer the game scrolls, because then the
console is already wrapping it in plain sight and its seam is one an artist has
had to make look right: the stone wall drifting diagonally behind the LucasArts
logo, the wallpaper behind the character select. It is wrong for a layer that
has sat still since the screen went up — the card that announces the level does
exactly that, `hScroll` nailed to 0 while `vScroll` runs, and wrapping it
printed the tail of its last line down both sides of the picture. Those are
clipped. The answer is latched per screen and cleared behind the forced blank
every screen change goes through, so a wall that moves one pixel every fourth
frame does not flicker between the two.

So: Konami's white field, LucasArts' stone wall, the character select's monster
wallpaper, the title's spiral and the logo sweeping across it all reach the
edges of a 16:9 frame; the legal text, the story card and the level card keep
their black, because black is what their background is out there.

**Off by default, and every measurement in this file is made without it.** At
zero margins each widened expression reduces to the one the vendored core
always had: same 2048-byte rows, same 32-sprite and 34-tile ceilings, same
window edges. The check is that `zamn_headless` renders a byte-identical PNG
against a build of the commit before any of this existed — both the boot frame
and 2,400 frames of level 1 — and that `zamn_cosim` is unmoved: `run -r none`
still identical at all 2,389 passes of level 1, `verify` still 191,614 calls
and none diverged.

**The margins hold the map, and getting there took one more step.** BG2's
tilemap ring is 64 columns but the game only ever *maintains* 32 of them: it
writes one fresh column at the leading edge each time the camera crosses an
eight-pixel boundary and never touches the rest. A ring slot therefore holds the
right map column only where the camera has already been — correct behind it,
stale ahead of it. Level 29's camera wanders, so its margins were right by
accident; level 1 walks steadily east and showed a band of leftover tiles at its
leading edge.

Those columns are not recoverable by looking harder, because they were never
written. So `src/widescreen.h` writes them, each frame, from the same map and by
the same priority rule the ROM's own column copy uses — into ring slots the game
does not read, does not write, and will overwrite with exactly these values if
the camera ever carries them into view. It is not a change to the game's tilemap;
it is the rest of the tilemap.

**At the ends of a level the margins go somewhere else instead.** The camera
stops at the edges of the map, because it was written for a 256-pixel window and
that window is all the game thinks is on screen; hang 43 more pixels off each
side and the picture leaves the world, with nothing to fill it and no actors out
there to draw. Blacking that out is what the first attempt did, and it was one
of the two reasons the extra width looked like it was *removing* enemies — at
the west end of a level the whole left margin was outside the map, so everything
in it was correctly, uselessly, culled to the backdrop. (The other reason is
below, and it was the larger one.)

The fix is to stop insisting the two margins be equal. The picture is always the
same width; when the left margin cannot have its 43 pixels the right margin
takes the remainder. Walking west, the view slides to a stop against the world's
edge while the player carries on to it — which is what every game with a camera
does at the end of a level, and it costs only the picture no longer being
centred on a camera that was never centred on the player either. No camera
limit moved and no actor was culled differently; the window simply stays inside
the world.

Nothing in the game was touched for any of this. No ported routine changed, no
co-simulated constant moved, and `verify` and `run` are unaware of it.

### The sprites the game throws away

The cull is generous and was never the problem. The last thing that happens to a
record is. `sprite_emit` (`$80:BA51`, and its three flipped twins) works out
where each 16x16 piece lands and then does this:

```
CMP #$0100 : BCC keep      ; on screen
CMP #$FFF1 : BCC drop      ; ...or off it, and there is no third case
```

A piece whose screen X is 256 or more, or 16 or more to the left of zero, is
dropped on the floor — not parked, not clipped, never written to OAM at all,
because the console cannot show it and OAM is 128 entries the game has better
uses for. That band is exactly what widening turns into picture, which is why a
zombie vanished a body's width before the edge of a widescreen frame while
walking about quite happily in the game's own memory.

`src/widescreen.h` puts them back. At the top of each frame — after the game's
vblank, before a line is drawn — it walks the same visible list the game's own
pass walked, reads the same records and metasprites, composes them exactly as
`sprite_emit` does, and keeps the pieces the ROM dropped for being outside the
console's 256 and inside the widened picture. They go into OAM entries the
game's pass left parked, so nothing it placed moves, and not one byte of the
game's own memory is written.

Getting that to hold still took two more things, and both of them were visible
as flicker at the edges before they were understood.

**The picture is a tick behind the memory.** The game composes an OAM buffer and
a queue of graphics uploads as it runs, and the NMI at the top of the next frame
DMAs both into the hardware before letting the game run on. So at the moment the
margins are drawn, the OAM and VRAM the console is about to read are one tick
older than the actor records and the camera in WRAM. Compose the margins from
the live memory and every piece lands one frame's motion away from the same
actor's on-screen pieces — a survivor is torn along the seam whenever the camera
moves, and near an eight-pixel boundary the extra tilemap columns go to the
wrong ring slots as well. So `Widescreen` keeps a copy of WRAM as it stood at
the previous frame start and reads that instead. The check is exact and it is
the one that settled this: recompose every piece the ROM *did* emit and compare
it against the OAM entry it actually produced, in order, and **13,641 pieces
over 926 frames match to the word** — position, tile, palette, priority and
flip. Against the live memory, 7,629 of them did not.

**The graphics are not loaded, because nothing asked for them.** A 16x16 frame
is only in VRAM if something drew it: the lookup that resolves a frame to a tile
(`$80:B9D6`) is what uploads it, and a piece dropped by the test above never
reaches the lookup. So an actor walking off the side of the screen stops
refreshing the frames of whatever part of it is already past the edge, and the
LRU reclaims them a few frames later — which is exactly what survivors and
pickups losing half of themselves at the margin looked like.

The cache has 128 slots and the fix is to borrow one. A slot the game has never
allocated is free outright: no frame maps to it, so nothing the game can emit
points at it. Early in a level there are dozens, but the cache only ever fills,
so in a long level there are none — which is why this got worse the longer you
played. The second answer is what makes it hold: a slot whose graphics no sprite
in *this frame's* OAM reads from is free for exactly the length of this picture.
The OAM being drawn is right there to be read, every sprite in it is a whole
16x16 frame, so the slots it uses are known exactly. Such a slot is borrowed for
one frame and given back at the top of the next, before a line of it is drawn,
by putting back whatever the game's own cache map says belongs there. The game's
tables are never written; it is not told a slot has changed, because by the time
it could look, it has not.

Measured over nine movies and 13,869 frames of play: **15,583 pieces the ROM
dropped, and all 15,583 drawn** — none skipped for want of graphics, where the
first version of this skipped one in five. 5,423 frames put at least one piece
back, the busiest puts twelve, and no picture ever borrows more than five slots.
The invariant behind the borrowing was checked directly too: on every frame,
every cache slot the console is about to read from still holds the graphics the
game's own map says it holds — 10.9 million VRAM words compared against the ROM,
and the only mismatch in the whole run is on a frame that borrowed nothing, where
the game's own upload had not landed yet.

**And some of them are not there to be dropped at all.** Two more kinds of
thing vanish at a widened edge, and neither is a piece an emitter threw away:
they are things the game has taken out of the world altogether.
`object_spawner_body` (`$80:C8F6`) walks the level's list of pickups every
fourth tick and measures each against the middle of the camera's window — inside
`$90` of it on both axes the object holds an actor record, outside it
`$80:CAA8` takes the record back. On the horizontal that window is the console's
256 plus sixteen pixels either side: exactly one sprite's width of slack, which
is enough for a pickup to be gone by the time the last of it has left the
console and not one pixel more. The neighbours are the same idea with their own
list, their own thread (`$81:81F6`) and their own constant — `CMP #$00A0`, so 32
pixels. Monsters have no such window; they are spawned by their own threads and
roam. Which is why a zombie walks calmly off the side of a widened frame while
the first-aid kit beside him blinks out of existence.

A pickup can be put back exactly, because it is a row in a table. `W_OBJECT_X`,
`W_OBJECT_Y`, `W_OBJECT_TYPE` and `W_OBJECT_STATE` all survive the despawn, and
`object_spawn` builds every record it ever makes out of those and the metasprite
table at `$80:CA6C` — thirty types, every one a single 16x16. So
`ws_object_sprites` draws any entry whose state says it has no record, at the
position the list gives it, with the metasprite its type names. When the camera
comes back the game puts the record at that same position out of that same list,
so this is not a guess at where the thing would be; it is where it is. It fixes
the leading edge as much as the trailing one, an object the camera is walking
towards not having been spawned yet either. Over fourteen movies **947 frames
draw at least one** of these and the busiest draws three.

A neighbour cannot be. She is a thread, not a row. *Where* she is survives her —
she stands where her list entry says she stands, and sixteen neighbours followed
for 2,659 ticks are exactly on it for 2,520 of them and never more than two
pixels off — but what she *looks* like is wherever her own thread had got to in
whatever script it runs, and that dies with the thread. Keeping the last record
the game had of her and drawing it on is easy; making it live is not, and two
rounds of trying is what made the shape of the problem clear. A copy stops the
moment the game lets go, and a player who stops walking leaves her stopped for as
long as they like. Reading a loop off the poses she was seen in gets her
breathing again and no further — that is what she was doing a moment ago, not
what she was going to do next. And neither touches the far end at all, because
when the camera comes back the spawner starts her thread again and the thread
starts its script from the top, a pose or two from wherever the copy had reached.

None of which is a failure of the copy. The window is the thing that is wrong,
and it is wrong in exactly one respect: it is 256 pixels wide because the picture
was. So `ws_widen_window` writes `#$00A0 + 2 * margin` over the immediate at
`$81:823D` and the spawner goes on doing precisely what it always did — one
comparison, against the picture that is actually being drawn. Twice the margin
because the two margins slide: at the end of a map the side with no world left
gives its pixels to the other, so either can be the whole 86 at once, and one
figure that covers the worst case is a figure that does not move as they trade.
(That was the first version, and it was unfair to her: see *The window follows
the picture* below for what it cost and what replaced it.)
She is spawned before she reaches the edge of the picture and taken away 32
pixels past the other one, which is the stock game's relationship to its own edge
to the pixel. Her thread runs the whole time she is in view, so she animates
because she **is** animating: nothing follows her poses, remembers them or
replays them, and everything that used to is gone -- the hold, the pose tracker
and the two structs behind them, 177 lines out of `widescreen.h` against 83 in.

Counted over the whole corpus at 16:9 — every tick of every movie, against every
neighbour the level's list still has — frames in which a neighbour is inside the
drawn picture with nothing drawing her fall from **8,492 to 269**, and
neighbour-frames drawn rise from 34,862 to **43,469**. Every one of those 269 is
between tick 26 and tick 45 of a level, before the spawner thread has walked its
list for the first time; outside a level's opening second there is no residue at
all, at either edge.

This is the one thing the widescreen does to the game rather than to the picture,
and the cost is real: a neighbour in the margin is a *neighbour*. She can be
rescued out there, and a monster standing next to her can reach her out there,
where on a console she would have been lifted out of the world and been safe
until the camera came back. Parked in a level 17 library with the camera stopped
and a neighbour 40 pixels off the left edge, that is exactly what happens: she
goes on animating on the four-pose 48/80/48/160 loop the game has her on, and 172
ticks after the last button press a monster that had been walking at her the
whole time covers the last 44 pixels and takes her. In the stock game she would
have been despawned before it arrived. The rule the widened window actually
enforces is *if you can see her, she is real*, which is arguably the fairer of the
two, but it is a change and it is not hidden. Across the corpus it is not a
rounding error either: the camera path -- which the players drive, so it is a fair
proxy for the run having gone the same way -- stays identical tick for tick in 31
of the 42 movies, and **12 neighbours end a movie gone who did not before, against
2 who no longer do**. Every one of the 12 has a monster within 15 pixels on the
tick it happens and goes into the same `fcd5 fcde fd07 fd48` sequence, so they are
losses rather than rescues. There is no smaller window that would avoid this,
because the exposure is exactly co-extensive with being visible: it is the same
fact as the fix. She also holds an actor slot for longer; `$80:825E` already returns empty-handed when the thread table is
full and `$81:81A2` already gives up quietly when it does, so that pressure has
the failure mode the game shipped with.

Nothing else is written — and in particular the pickups' `#$0090` is left exactly
where it is. The same one-byte trick would work there and would buy nothing: a
pickup can already be drawn from its list to the pixel, so changing the game to
put a record behind it would only be changing the game. It is the neighbour who
cannot be drawn from anything, and she is the only one who gets this. The
vertical half of her own test at `$81:8250` keeps its `#$00A0` too, no rows
having been added, and at margin zero the stock figure goes back — which is why
`--widescreen off` is still byte-identical to the build from before any of this
existed. The co-simulation never sees it either: `widescreen.h`
is included by the two frontends and by nothing the gates run.

And the console's own 256 columns are still untouched by any of it — but the
check for that had to be sharpened, because the reference moved. Comparing a 16:9
frame against a plain 4:3 one of the same input now compares two runs that are no
longer the same run, and the diff fills up with the game legitimately doing
something else. Compared instead against a 4:3 frame of a run with the *same*
spawner window, so that the only difference left is how much of it is drawn,
**every row outside the status panel differs in exactly one column** — 48 frames,
three movies, both aspects each against its own reference — and that column is the
next section.

### The bosses that are backgrounds

Reported in play-testing: the giant baby and the flying saucer are cut in
half at the margins and turn up on the other side of the screen. Neither is
a sprite. A figure that size is tiles written into BG1's map
(`src/port/bossbg.h`), and BG1 is scrolled to where it stands; in every
level BG1 is a 64x64 map, 512 pixels square, with the figure in its first 14
or 20 columns and nothing else. Two things were wrong, and the report is
both of them.

BG1 was left to `ppu_wideAuto`, which guesses, and guessed wrong both ways.
With nothing in the console's edge columns it clipped the layer at the
console's 256, so a figure across the edge was cut off flat. With something
in them it took the 64-column map for one the game half maintains and
repeated the console's columns outward: the half of the baby at the left
edge drawn again in the right margin, and nothing in the margin he was
standing in. In a level BG1 is now `ppu_wideStretch`, as the world is: the
plane is the game's own and all of it is maintained.

And the game parks it. The vblank job that scrolls BG1 (`$82:8209`) writes
`$0100` to both scrolls, the blank quarter of the plane, once the plane's
origin is 256 or more right of the console's left edge. That is the sprite
cull again: a baby whose left edge is in the right margin has been put
away. `ws_boss_plane` writes the scroll the job would have written, from the
same words (`$1E6E`, `$1E70`, the camera), when the scroll is the parked one
and the origin is inside the right margin. To the left the game only parks a
figure already past the widest margin. PPU registers only; the game writes
them again at the next vblank and never reads them.

That much left him flashing at the sides, reported again, and it was two
more things, with a third found on the way to them.

  * **The plane comes round.** It repeats every 512 pixels, which the console
    never sees, because the game keeps the figure's origin within 255 columns
    of the console's left edge and 255 + 256 is one short of 512. The picture
    is wider by its margins: with the baby 214 or more columns off to the
    left, the right margin was reading the plane's next lap, and his bottle
    and arm stood in it while he walked about off the other side. In that band
    `ws_boss_plane` parks the plane itself.
  * **The job reads what the tick left, and sometimes the tick has moved on.**
    Putting the plane back needs the words the job read. Nearly always those
    are the machine's memory as it stands at line 0 (4,446 of 4,491 frames on
    `level25-lane`), but when the figure turns round its origin jumps, and
    now and then the next tick has already made that jump: the live words say
    "on the console" of a plane the job parked. The figure was gone from the
    margin for one frame in every few hundred. It is the first of the live
    words and the tick-old copy that agrees with the job having parked it.
  * **A frame the game drops.** The job does not run, and the register still
    holds what was written here the frame before, for an origin past 255. Read
    with the plane's nine bits that is an origin far off to the left, which
    the first of these fixes would park: gone for a frame whenever the game
    was late while he stood in the margin. Caught by the check below before
    it was ever played; the register is read with all ten bits.

Both decisions are made from where the figure is in its plane
(`ws_boss_extent`) and against 16 columns more than the picture, which is
what the smoothing captures of every plane and slides into view as it eases
a scroll.

Checked on every frame of four level 25 movies, in the hook, by asking which
columns of the picture the plane has a tile in. With the routine off the
figure is somewhere it is not standing on 44 to 107 frames of each; with it
on, on none. The frames with nothing between two frames with something are
down to four, all the same and all the game's own, inside the console's
columns: the figure coming on over the top or bottom edge, 16 columns, none,
then 24. On `level25-lane` in 16:9 the frame of the first
report (5,300) has the whole baby standing in the left margin and no second
one; frame 2,506, which had his bottle in the right margin with him off to
the left, has not, in the frontend's own picture; and he walks in from the
picture's right edge (frames 3,992 on) where he used to appear at the
console's. The draw-list test passes on the boss movies in 16:9, 16:10 and
4:3. The saucer's levels have no movie; it is the same plane, job and rule,
and the rule measures the figure rather than assuming the baby's width.

### The monsters that come on from the wings

Reported in play-testing: on level 12 the football players can be seen
appearing on the field. Most monsters are started at whichever of the
level's spawn points is nearest a player and come out of the ground or a
door, and being seen arriving is what they do. The football player is not
one of those. His thread (`$81:C87B`) throws the spawn point's column away
and puts him down in the wings instead, 8 pixels left of the console or 72
right of it, facing in; he gets set there, and charges across. Eight pixels
left of the console is 35 pixels inside a 16:9 picture, so he appeared out
of the air in the left margin and stood in it.

It is the neighbours' problem over again and gets the neighbours' answer: a
thread cannot be drawn from outside the game, and the columns are not wrong,
only the console's. `ws_widen_window` is now a table of words in the ROM
image, each moved out by twice the margin (twice, because at the end of a map
either margin can be all of it) and each put back at margin zero. The
football player has five: the two wings, the two sides of the window he may
stand in while he gets set, and how far from the player he may run before he
is taken away, which has to grow with the wings or the far one is outside it.

Looking for the idiom rather than the monster (every read of the camera's
column in the four code banks, 38 of them) found two more. The purple
tentacle of the bonus rooms (`$82:990F`) picks a wing at random from a table
that is the same two numbers, and a creature at `$82:EAC5` runs off and is
taken away 4 pixels past the console's left edge. Both are in the table. The
rest of the 38 are the camera's own arithmetic, the two windows already dealt
with, the bosses' plane, two monsters that come on over the top edge, where
there is no margin, and one test that only decides whether to play a sound.

Measured on level 12, walking to the middle of the field with `--key-at` and
logging every football player's first position against the camera. Before:
32 arrivals, ten at column -8 or -6 and 22 at 328. After, in 16:9: 35
arrivals, twelve at -92 to -98 and 23 at 414 to 418, against a picture that
is never wider than -86 to 342, and a frame taken while one was waiting in
the wings shows only the ones already running. The tentacle, on record 51,
arrives at -94 and 414 and still reaches the player. The creature at
`$82:EAC5` has not been reached by anything here and has not been seen. The
draw-list test passes on `level1` (16:9 and 4:3), `level21` and
`level25-lane` (16:9) and `level49` (16:10).

### The window follows the picture

Reported in play-testing: neighbours dying where they cannot be seen, on many
levels. Some of that is the game. A monster touches anything in the visible
list, which `actor_cull` fills from 128 pixels behind the camera to 383 ahead
of it, and a neighbour exists 32 pixels past either edge of the console's
screen (48 rows past the top and bottom), so on a console there is a strip she
can be taken in unseen. But the first widened window made the strip much
bigger. It left the window's middle at the camera's and made its reach
`#$00A0 + 2 * margin`, to cover a margin that is all on one side at the end of
a map, so in 16:9 she existed 75 pixels past each edge of a centred picture
and 118 past the short edge at the end of a map.

The reason given for not moving the middle, that nothing should spawn and
unspawn as the margins trade, was not one: they trade a pixel at a time as the
camera closes on the map's end, so a window that follows the picture moves no
faster than the console's follows the camera. The window is two immediates,
`ADC #$0080` at `$81:820A` for the middle and `CMP #$00A0` for the reach, and
`ws_widen_window` now writes both every frame: the middle of the picture as it
stands (from the machine's memory as it is, since it is for the tick about to
run) and half the picture plus the same 32. The strip is the console's 32
wherever the margins are, and she is still a neighbour in every column drawn.

Measured in the hook on every frame of `level1` and `level25-lane` in 16:9,
old rule against new, from the game's own table of which neighbours have a
thread: the furthest outside the picture any was alive falls from 73 and 71
pixels to 2 and 37 (32 and the few ticks the spawner takes to come round), and
on neither movie, under either rule, was one without a thread more than 16
pixels inside the picture -- with each margin at its full 86 during
`level25-lane`. The draw-list test passes on `level1` (16:9 and 4:3),
`level25-lane` (16:9) and `level1-rescue` (16:10).

### The one that was a real emulation bug

The left margin came out a shade darker than the picture it was continuing, in
every level, and that one was not a widescreen policy at all. This game switches
colour maths *off* by pointing both windows at the single column `0..0` and
asking for maths *inside* them. The first version of the widened window test
treated each edge on its own — a window starting at 0 starts at the left of the
screen — which turned that degenerate window 43 pixels wide and subtracted the
fixed colour from the entire left margin. Only a window covering the console's
whole line means "everywhere"; a window with one edge at 0 and the other in the
middle is a *place*, and places stay put.

Fixing that left one column of it. A window from 0 to 0 does contain column 0,
so the console really does subtract the fixed colour there — one column, at the
extreme left of a picture no television showed the extreme left of. Widen the
frame and it is 43 pixels in from the edge, in plain view, as a thin dark line
down the left of every level. So a window one column wide sitting on either edge
of the console is read as what it is, a parked window, and only when there are
margins: at 4:3 the column stays dark, because that is what the hardware does.
Measured across the seam on level 1, the column read 36.2 against neighbours at
74–75; it now reads 64.8, which is what the grass either side of it reads.

### `--filter`

Decides what happens when the output is not a whole multiple of 512x448 — which
fullscreen usually is not:

| `--filter` | what it does | trade |
| --- | --- | --- |
| `sharp` (default) | nearest up to the next whole multiple offscreen, then one bilinear step down to fit | uniform pixels, fills the window, a sub-pixel seam at each block edge |
| `integer` | only whole multiples, letterbox the rest — and therefore square pixels, so it is not 4:3 | perfectly uniform; 1920x1080 fits 2x and leaves 17% of the height black |
| `linear` | one bilinear step from 512x448 | blurry — kept so the difference can be seen rather than argued |

Nearest-neighbour on its own is **not** one of the options, because on its own
it is the problem: scale 512 into a 1000-pixel window and the factor is 1.953,
so nearest drops one source pixel in 21 and most game pixels land 2 screen
pixels wide while some land 1. On a moving sprite that narrow column crawls
across it. The artifact is the fractional factor, not the filter, and `sharp`
and `integer` are the two ways of not having one. F2 cycles the three while the
game runs.

Aspect correction and sharp scaling genuinely fight: at 4:3 the two axes
magnify by different amounts, so one whole-multiple intermediate cannot serve
both and the offscreen stage is a whole multiple **per axis**. That also buys
something the square-pixel version could not have — an output that is a whole
multiple of the source on each axis independently is pixel-exact even though its
pixels are oblong, so 4:3 is not automatically the blurry choice. All of the
below are pinned as named cases in `tools/test_scale.c`:

| display | 4:3 `sharp` does | why |
| --- | --- | --- |
| 3840x2160 | 2880x2160, stage **6 across by 5 down** | 5.625x and 4.821x — the case one stage number cannot express |
| 2560x1440 | 1920x1440, stage 4x4 | 3.75x and 3.214x |
| 1920x1080 | 1440x1080, stage 3x3 | 2.8125x and 2.411x |
| 1366x768 | 1024x768, stage 2x2 | exact across, 1.714x down |
| 3584x2688 | **nothing at all — pixel-exact** | 7 source widths by 6 source heights is exactly 4:3 |

### Frame pacing

"Runs at 60 fps" and "looks smooth" are different claims, and the mean frame
rate cannot tell them apart. This used to gate frame production on the audio
queue draining — the device consumes 48000 samples a second, so the queue looked
like an exact clock. It is exact *on average only*: the device pulls its whole
buffer at once, so the queue fell in 42.7 ms lumps and the loop emitted two or
three frames as fast as it could and then stalled. Measured over 600 frames:

```
arrival  mean 16.57  p50  4.25  p90 40.75  max 50.36 ms
within 1 ms of the period: 0.0%
```

A flawless 60.3 fps in which **not one frame of 599 arrived on cadence** — about
23 visible updates a second. The clock is now a deadline on the high-resolution
timer, at a period locked to the display refresh where the display is a sensible
multiple of the console's rate (60.0988 Hz NTSC, which no monitor offers) -- or,
with smoothing on, any rate above it, see below -- and
the audio is corrected to *that* by resampling each frame by up to half a percent
— the same dynamic rate control emulator frontends use. Same machine, same movie:

```
arrival  mean 16.66  p50 16.75  p90 16.75  max 19.60 ms
within 1 ms of the period: 98.7%
```

Every `--frames` or `-m` run prints this at exit, and a played one under
`--verbose`; `--paced` keeps 60 Hz pacing under `--frames` so a
bounded run measures cadence instead of throughput. `src/pace.h` has the details,
including why the audio backlog is reported alongside — video no longer depends
on it, so nothing but the correction stops it drifting, and a `min` near zero is
that correction failing.

**This is the substituted build, not the emulated baseline.** It installs the
same `COSIM_NATIVE` interception `zamn_cosim run` uses, against the one live
core: every call the game makes to a ported routine is executed by the C port
instead of by the 65816, and the window title carries a live count of how many.
`--stock` clears the enable mask to get the Phase 0 emulated baseline back, and
F1 moves between the two at a frame boundary while the game is running.

The title bar and the exit report also carry **what share of the game that is**,
measured over the session you just played, because a count of served calls only
ever goes up and cannot tell you whether a round of porting bought anything:

```
  work        296,719,746 of 654,074,274      45.4%
  calls            55,225 of 318,792          17.3%
```

`work` is SNES cycles — the budget each substituted routine burns in place of
the instructions the ROM no longer executes, over the cycles the CPU spent
*working*, with the scheduler's `WAI` and the ROM's ten declared busy-wait loops
(`src/cosim/waits.h`) out of the denominator, since porting a spin gives a spin.
`calls` is `JSR`/`JSL`: every subroutine call the game made against the ones the
port served. Both denominators shrink as the port grows — a call made inside a
substituted routine never executes at all — which is the property that makes the
ratio mean something.

The two rows are far apart on purpose. `calls` weights a 17-byte leaf and a 2 KB
state machine alike; `work` weights each by what it costs, and is the one to
read. Neither counts a thread body or a vblank job as a call, because the
scheduler and the vblank dispatcher reach those by `RTL` and there is no call to
intercept.

The number depends heavily on what the game is doing. Level 1's movie is
**23.6%** of work over 2,400 frames because most of it is boot — the APU upload,
LZSS decompression and the fades, none of it substituted. Actually playing a
level is where the ported routines are: **45.4%** on `level29-fighting`, 45.6%
on `level9-weapons`, **56.9%** on `level25-boss`.

`tools/native_share.py` measures the same quantity offline by a completely
different method — a traced instruction profile and a call-graph closure, rather
than cycle budgets at the substitution seam — and its `...substituted only`
line reads 24.0% where the live figure for the same movie reads 23.6%. Two
independent measurements agreeing is the check on both.

What this is *not* is a native game yet. Eighty-two routines are ported; the
main loop, the NMI handler, the movement thread, level code and every enemy body
still belong to the ROM under the emulated core. What runs natively are the
leaves those call — the sprite/OAM pass, the depth sort, collision dispatch, the
step proposer and every test a step is checked against, thread spawn and tick,
score, fades, **the camera and the whole tilemap streamer under it** — where the
view should be, the one pixel a frame it moves towards it, and every strip of
map that appears at the edge as it does — and the blitter that draws a boss too
big for sprites as a background layer, mirroring it a tile at a time when it
turns around, and the mover that walks that boss into walls a single axis at a
time so it slides along them, and **the two leaves the rest of the cartridge
asks the map through** — one tile in, one attribute word out, twenty-six call
sites across four banks — and **what the floor under the player does to them**:
the conveyor belts, and the harmful tiles with the cooldown that stops one
hurting you twice — and, on top of those, **the player's ordinary frame**: the
ground checked before a button is, the held weapon against how much of it is
left, and the four button edges that cycle a weapon, cycle an item or open the
map. Phase 4 is where that inverts.

**What is left to port, from any session.** `--profile <dir>` counts what the
65816 still executes while the port is substituted and adds it to the profile
in `<dir>` at exit, so one directory can collect a week of play-tests. Rank it
with `python tools\native_share.py --residue <dir>`. The tracer takes `--level
N` as well, so the offline corpus can reach every record, not only the ones a
password starts. See `docs/analysis-tools.md`.

Other options — `-m <movie.zmv>` replays a recorded movie instead of reading the
keyboard, `--frames N` runs N frames uncapped and exits, `--shot out.png` writes
the final frame, `--no-audio` skips the audio device, `--no-pads` ignores game
controllers. The three together are how
the substituted frontend gets checked against the baseline without anyone
playing it:
```
build\zamn.exe "Zombies Ate My Neighbors.sfc" -m movies\level29-fighting.zmv ^
    --frames 6000 --no-audio --shot native.png
build\zamn.exe "Zombies Ate My Neighbors.sfc" -m movies\level29-fighting.zmv ^
    --frames 6000 --no-audio --shot stock.png --stock
```
Those two framebuffers are identical, as are level 1's, level 1 two-player and
level 45's. Two movies do **not** match — `level25-lane.zmv` and
`level21-bubble.zmv` — and for a reason that is not a wrong answer: on those
levels a scheduler pass sits close enough to the vblank boundary that the two
builds eventually disagree about whether one overran it, after which they are
showing different moments of the same game. `zamn_cosim run` detects and reports exactly that —
see `docs/cosim.md`. Stopping the same two runs at frame 2,600, before the pass
where they part, gives byte-identical framebuffers, which is the check that
tells a timing divergence from a wrong answer. On exit the frontend prints the same per-routine table `zamn_cosim`
does — minus the verdict column, since there is no reference core here to diff
against — plus the decline census naming whatever the ROM still had to run.
(It is a `WIN32` binary, so it borrows the parent console for that; run it from
a terminal to see it.)

Headless frame dump (for verification / debugging, and for aiming a movie):
```
build\zamn_headless.exe "Zombies Ate My Neighbors.sfc" frame.png 500
build\zamn_headless.exe "Zombies Ate My Neighbors.sfc" shot.png 6100 -m movies\level1-rescue.zmv --at 1980,3000,4200
```
Three options make it the movie-authoring loop rather than a screenshot tool:
`--pos` prints where each player is, `--records` prints the whole display list
with the fields that decide a collision, and `--watch <addr>[,first[,last[,step]]]`
prints one WRAM word whenever it changes. `--watch` may be repeated.
`--twin-stick` installs the aiming stub (`src/twinstick.h`), and
`--aim frame[+]:dirs` pushes port 1's right stick -- `U`, `D`, `L` and `R` in
any combination, or `-` for centred, `+` holding it from that frame on -- which
presses `Y` for as long as it is out, as the frontend's stick does.

`--records` also names the **collision handler** each thing on the board is
running, and the direct page it runs on — so the display list doubles as a map
from what is on screen to the routines in `src/port/collide.c`, and the page is
what to point `--watch` at.
```
build\zamn_headless.exe "Zombies Ate My Neighbors.sfc" shot.png 7600 ^
    -m movies\level25-boss.zmv --watch 083C,3900,7580,1
```

### Smoothing, and `--no-smooth`

The game moves at 60.0988 ticks a second and nothing in it can move faster: a
thread sleeps for a whole number of ticks, a walker covers a pixel a tick and
takes a second step on the ticks a mask picks out, the camera drifts a pixel a
tick after the players. A 240 Hz display shows each of those pictures four
times, and the three repeats between one tick and the next are refreshes the
console never had anything to put on. **Smoothing puts something on them**: the
pictures in between show every background and every sprite part of the way
back toward where it was a tick ago. At 240 Hz that is four distinct pictures
per game frame; at 120 Hz two. On by default on any display faster than the
game, F6 toggles it, `--no-smooth` starts without it, and on a 60 Hz panel it
is off because there is nothing for it to fill.

**The display need not be a whole multiple of 60.** Every refresh is a picture
of its own, and the game is a fixed fraction of a tick further on in each than
in the last -- 1/4 at 240 Hz, 5/12 at 144, 4/11 at 165, 4/5 at 75
(`pace_lock_ratio`). A new tick is taken whenever that sum passes one, so at
144 Hz a tick is shown as two pictures or as three, and each is drawn where
its own moment falls between the two ticks rather than at a quarter or a
half. Motion is as even as at 240; what differs is only that the game's own
changes -- an animation frame, a colour -- land on refreshes 2 or 3 apart. The
tick rate is locked to the display as it is at a whole multiple (60.000 a
second at 144 Hz) and the audio corrected to that. `--refresh <hz>` overrides
the rate the system reports, which is how 144, 165 and 75 were measured on a
240 Hz panel: 2.40, 2.75 and 1.25 pictures per frame, 59.3-59.5 frames a
second over a run with four level loads in it.

**Uneven steps are evened out**, and `--no-even` turns that off (it had a
key, F6, which is the smoothing's now). The
game keeps positions in whole pixels and moves things at speeds that are not:
the player walks a pixel and a half a tick, which comes out as 2, 1, 2, 1, and
a zombie giving chase is moved on every other tick, 2, 0, 2, 0. Eased straight
from tick to tick that is a speed that changes by a third, or stops dead,
thirty times a second -- and the pictures in between are what make it
visible: the floor shimmers under a walking player, a chasing zombie moves for
four pictures and stands for four, and on a diagonal both axes do it together.
Averaging the last two ticks cures it and shows everything half a tick late;
that was built first, and the delay was felt at once. What is drawn instead is
the steady line those steps stand either side of, *now*: each thing sits
`(d - q) / 4` pixels back from where it is, `d` being this tick's move and `q`
the one before -- a quarter pixel for the walk, half for the chase. That is
exactly where it is for anything moving steadily, so there is no delay, and it
cancels a two-tick alternation entirely (`zamn_test_layers --track` on a
diagonal chase: one target pixel per picture, every picture, where it was 2,
2, 2, 2, 0, 0, 0, 0 -- and ending each tick half a pixel either side of the
truth rather than a pixel behind it). The cost is that a change of speed is a
quarter answered late: a thing that stops from 2 a tick is drawn half a pixel
past its stop for one tick. A change of more than 4 is taken for a jump and
left alone. A thing at rest is
exactly where it is. `zamn_test_layers --motion first last` prints the moves
themselves, tick by tick, which is how the patterns were found.

**The game is not touched.** It runs at exactly its own rate, tick for tick, on
the same inputs; `zamn_cosim`, `zamn_headless` and the whole movie corpus never
see any of this, and neither does the ROM. What changes is only what is put on
the refreshes between two of its frames.

**The picture is taken apart and put back a fraction of a pixel over.** The
first version of this moved the PPU's own sprites and scroll registers and had
the PPU draw the frame again, and it looked worse than no smoothing at all, for
two reasons that are worth recording. The PPU draws in whole pixels, so a thing
that moves a pixel a tick -- the camera on most levels, a walking player, the
film strips on the character select -- cannot be shown anywhere between one
pixel and the next: at 240 Hz it stepped once and stood still for three
refreshes, exactly as it did at 60. And things moving at different speeds
rounded to different phases: a two-pixel move stepped at the first and third
picture of a tick and a one-pixel move at the second, so a player walking
across a scrolling floor was drawn moving *against* the floor and then with it,
four times a tick. That was the jitter. (A redraw also cost the PPU three
milliseconds, three times a tick, which at a four-millisecond refresh was the
whole budget.)

So `src/layers.h` asks the PPU what the frame is made of instead -- one plane
of pixels per background and priority, the backdrop, every sprite as its own
little bitmap -- once per tick, on the emulation thread, and hands the frontend
a *draw list*: this plane here, that sprite there, this one added on top.
`src/present_layers.h` draws the list with the GPU onto a target several times
the console's size -- four times at 240 Hz on a 4K panel, six at 1080p in 4:3
-- so a quarter of a game pixel is a whole pixel of the target, and the eye
sees a sprite move by a quarter of a pixel. Building a list costs microseconds.

**What moves is what the game moved.** A background moves by the change in its
scroll since the last tick. A sprite moves by the change in its *actor's*
position: the port's sprite pass leaves beside the OAM buffer which display
record each entry came from and where that record was drawn
(`sprite_oam_owners`), so every piece of a zombie moves with the zombie and two
zombies standing close do not trade pieces -- which the old matching-by-looks
did on 2% of sprite-ticks, measured, and which is a shimmer. The record says
*which* actor; where a piece is eased *from* is where the nearest piece of
that actor actually was, not the actor's origin moved back, because an
animation frame puts its pieces at their own offsets and a walker whose frame
just changed has pieces a pixel from where the last frame's were. Eased from
the origin alone, every walker jumped a pixel at every frame of its walk
cycle -- found by tracing one zombie's piece through twenty pictures: 151,
154, 157, 160, then 166 -- and that was the jitter the first play-test still
saw. Under `--stock` the pass runs in the ROM and sprites are matched by
looks as before. Two details took a play-test in widescreen to find. The
pass's table describes the OAM buffer the game has just built, and the picture
on screen was drawn from the one before it, so the table is held a tick before
it is believed. And a widened picture has sprites of the frontend's own in its
margins -- the pieces the pass drops outside the console's 256, the items and
neighbours with no actor behind them -- which have no record and are paired
with the nearest recordless sprite of the same looks; and at the ends of a
map its margins trade width, which moves the picture's origin under everything
and has to be counted in every delta.

**The margins' sprites have owners too, and had to say so.** Paired by looks
alone, a piece crossing the console's edge had nobody to be paired with: it was
a record's sprite on one side of column 256 and nobody's on the other, and the
recordless were only ever looked for among the recordless. So for one tick it
was drawn where it had got to while the ground under it was still being eased
there -- a jump ahead of the ground and back on to it, at the inner edge of a
margin, once for every thing that crossed. Reported in play-testing as the keys
and the other pickups flickering in the margins while walking, and never in the
middle of the screen; a thing on the ground shows it best because it has no
motion of its own to hide it in. A walker out there did it again at every
change of animation frame, which moves a piece further than "nearest" was
allowed to look. `src/widescreen.h` composes those pieces from the record and
knows whose each one is, so it now keeps that (`ws_owners`), and the table the
picture is taken apart with is the game's with the margins' entries added: a
piece is a record's wherever it is drawn. What is left to pair by looks is the
things on the ground that have no record, and those are paired with a sprite
whose record has *gone* as well as with the recordless, because the spawner
gives such a thing a record as the camera comes up to it and takes it away as
the camera leaves, and it is the same thing in the same place. Over
`level1-pickups` in 16:9, 1,776 eased ticks: sprites with no last position 149
before and 78 after, every one of the 78 a first appearance -- none now within
eight pixels of where a sprite of the same looks had just been, which 17 were
-- and pairings by looks 1,919 before and 41 after.

**And one tick in which a key was nobody's at all.** That helped and did not
finish it: the keys of level 7 still flashed, walking from side to side with
one in a margin. This time the sprite really was missing. A thing on the
ground with no record is drawn from the level's list, but only outside the
console's 256 columns, because inside them the console's picture was taken to
be right. The spawner that gives the thing its record looks every fourth tick,
and its window reaches sixteen pixels past the console's edge, which a camera
at two pixels a tick crosses in eight -- so, with the picture a tick behind the
memory, a pickup can be at column -15 with no record yet. The list had stopped
drawing it and the game had not started: out for a tick and back, once for
every pass of the camera, in plain sight because the margin had been showing
it all along. On the console the same tick is a sliver at the edge of the
glass turning up late. `ws_object_sprites` now draws a thing with no record
wherever in the picture it is. Walking level 7 from side to side with a
pickup at the left margin's inner edge, 1,414 ticks, run twice each way: it
was missing on 7 of them before, one in every 84, and on none after.
A pickup rocked across the right margin's inner edge in the same way was
never missing, before or after.

**A record's pieces go back together, or the thing comes apart.** Easing each
piece from where its own predecessor was is right for a piece and wrong for a
body. When a frame of animation moves the pieces by different amounts -- a
slime rearing up to strike, its top going one way while its foot stays put --
two pieces that join on this tick and joined on the last do not join in the
pictures between them, and the ground shows through: reported in play-testing
as horizontal seams across the slimes of level 9 while they attack. `even` did
it too, on a smaller scale: a quarter of a pixel between two pieces whose
*last* moves had differed, which at nine times the console's size is a line
two pixels thick.
So the record votes. The move most of its pieces made is the move every piece
is taken back by, the origin's breaking a tie; the pieces that did something
else are where the new frame puts them from the tick's first picture, which is
when the console changes the frame as well. A walker whose whole frame shifts
by a pixel is still eased piece by piece from where it was, because there the
vote is unanimous, so the jitter the nearest-piece rule was written for stays
gone. What the record moved by before, `even`, and whether the move is a
placement are judged once for the record, against one sprite of the last tick.
`zamn_test_layers` counts the pieces taken back by a different amount from the
first of their record and fails on any: over `level9` in 16:9 there were 2,271
and there are none, with 646 pieces of 35,634 moved by their record's vote
instead of their own.

**A unanimous vote is not always a walk.** The potion's monster stands still
and its second punch frame draws the whole body three pixels lower; its first
walking frame is two pixels from its standing one, and walking down, its
frames bob the body two pixels every fourth tick. Every piece moves by the
same amount, so the vote was carried, and the body slid there over the tick
and, under `even`, overshot by three quarters of a pixel and came back --
where the console swaps the frame in one step, and where the player, whose
frames move their pieces by different amounts, is placed by the tie. Reported
in play-testing as the monster not animating as smoothly as the rest. The
actor's own move says which it is: a frame that shifts by a pixel about it is
the jitter the nearest-piece rule eases, and a shift further than that
(`LAYERS_POSE_MAX`) is a new pose, placed where the frame puts it by taking
the actor's move instead. On a movie that drinks the potion, the monster's
eased ticks over its punches went from 33 to none, over its first steps from 3
to none, and over four directions of walking from 27 to 9 -- the nine being
the pixel shifts still eased on purpose. It reaches other actors too: a walker
whose frames shift its body by exactly two pixels now snaps between frames as
the console does rather than sliding -- 266 records over 2,200 ticks of
`level1`, 356 over `level25-boss` -- and the pictures at the ends of ticks are
unchanged, so `zamn_test_layers` still finds every frame identical to the
PPU's. It counts the records placed this way.

The count found one more way apart, in `level25-lane`. A tick the game cannot
finish in a frame leaves a frame its pass did not run for, so the picture after
it had no owner table, and the picture after that nothing to be paired with by
record: two ticks of pairing by looks, a piece at a time, for every slow tick,
in the busiest moments a level has. But the picture after a tick that did not
finish is the picture before it -- the console is sent the same sprites again
-- so the table the last picture was taken apart with stands for this one too,
if every entry it accounts for is unchanged (`layers_owners_stand`). 21 pieces
apart in that movie before, none after, and none in `level25-heavy`.

A move
further than a tick could carry a thing is a cut and is not eased, so a spawn
does not slide across the screen to where it was put. Which tiles, which
palette, which animation frame, what the text says: those are the newer frame's.

**What is checked.** `zamn_test_layers` runs a movie and, on every frame, draws
the list in software, unmoved and at the console's own size, and compares it
with the frame the PPU drew:

```
build\zamn_test_layers.exe "Zombies Ate My Neighbors.sfc" movies\level1.zmv 1 3000
  as a draw list:  2451 identical to the PPU, 112 within one of it, 0 differing
  not a draw list: 437 frames
    forced blank                             393
    sub screen in a fade                     44
```

"Within one" is the character select and the level card behind it, where the
console adds the sub screen to the wallpaper in five bits per channel and the
GPU adds it in eight: a difference of one, everywhere the film strips are.
Every movie in the corpus comes out with zero frames differing. "Not a draw
list" is a frame the list has no op for -- forced blank, a fade while the sub
screen is being added, anything written mid-frame (the map screen's HDMA), mode
7 -- and such a frame is shown as the PPU drew it, as many times as there are
refreshes, with easing resuming on the next.

**The survivor radar is a window, and windows are drawn as clips.** The box a
shoulder button brings up is the colour-maths window: inside it the game
subtracts a fixed grey from the world and the backdrop, and since a window is
only a pair of columns, it moves the edges on the line where the box starts
and again where it ends -- a register written mid-frame, which made every
frame with the radar up "not a draw list", and the radar is a toggle that
stays up. The PPU now records the window edges per line as it does the
scroll, and a frame that moved only those is drawable: each plane the maths
applies to is baked twice, plain and mathed, and the mathed twin is drawn
over the plain one clipped to the rectangles where the window allows maths
(`mathRect`, one for the box). Clipping is on the target, so the box stays
put while the world eases under it, which baking the window into the plane
could not have done. Exact over the radar movie in 4:3 and 16:9, 341 frames
of 341. A window that moved on more lines than `LAYERS_MAX_MATH_RECTS` can
hold, or that gates a layer or the sprites rather than the maths, still
falls back.

**The radar's markers are one sprite.** The console draws every survivor's
marker with a single OAM entry, placed on a different survivor each tick, so
that six markers flash in turn -- and eased from one tick to the next that
one sprite swept the box. A sprite is now eased only if this tick's move
continues the last: a move that differs from the move before by more than
`LAYERS_JUMP_MAX` (6 px) on either axis is a placement, and the sprite is
drawn where it is for that tick. The move judged is the actor's origin where
the port's pass knows it, because a piece's own move has the animation in it.
Over `level25-boss` in 16:9 that refuses 44 of some 100,000 paired
sprite-ticks, 43 of them margin sprites paired by looks at seven pixels or
more; the markers still flash as the console flashes them, but each stays
where the game put it.

**And in widescreen the box was 43 columns right of its frame** -- the PPU's
own doing, not the list's, which is why the list matched it to the pixel.
The frame is on the status layer, which `ppu_wideAnchor` pins to the edges
of the wider picture, while the window kept the console's coordinates, and
console column 22 is picture column 65. `ppu_windowTest` now moves a window's
edges the way the anchored layer's columns moved whenever a layer is
anchored: an edge in the left half is that many columns from the picture's
left edge, one in the right half that many from its right. Off a level
nothing is anchored and nothing is windowed but the parked pair, so nothing
else changes; the radar movie is exact in both aspects.

**The markers go with the panel too.** They are sprites, and in a level
widescreen draws every sprite as a world thing, in the console's coordinates
-- so they sat 43 columns right of the box as the box had sat right of its
frame. The record they are drawn from is a *screen-space* record
(`ACTOR_SCREEN_SPACE`: the game lays it out on the screen, not in the
world), and the pass's owner table says which OAM entries came from it, so
`widescreen_frame` places those entries with the panel (`snes_setSpritePlace`,
`ppu_spriteAnchored`) and the PPU draws them where an anchored layer's
column of the same number goes. A place per slot rather than a moved X, so a
frame the game did not redraw is not moved twice.

**And the box's edges wander.** The game moves the window's edges from an
interrupt whose line is not the same from one tick to the next: the box
began on line 49, 50, 51 or 52 and ended on 107 to 110, tick by tick, which
at sixty pictures a second is a line of the world at the foot of the box
flickering in and out of the dimming. With `even` on, a rectangle that
differs from the last tick's by no more than `LAYERS_WINDOW_WANDER` (3)
lines at either edge is held where it was (`mathShown`); the console's own
frame is unchanged, so the exactness test still compares against it. 155 of
the radar movie's 341 ticks are held.

**It was not the game's interrupt, and three lines was not enough.**
Reported again in play-testing: the dimmed box visibly moving and flashing
under the radar's frame, badly on levels 7 and 8. The game has no such
interrupt. The box is a window opened and closed by HDMA from a table in ROM
(`$82:E691`: 48 lines shut, 58 open), the same on every frame, and with
`--stock` it is on the same lines on every tick. Substituted, its top was on
lines 49 to 51 on level 1, which the hold above was papering over, and
anywhere from 49 to 67 on level 7, which it could not.

The core does a scanline's HDMA from `dma_handleDma`, which every CPU access
calls, on a request that is a single bool raised once a line. A substituted
routine's time is burned without CPU accesses (`burn_slice`), and the
frontend burns each budget in one piece: a budget spanning twelve lines got
one line's transfer and the table was eleven lines behind for the rest of
the frame. Busy levels have the long budgets. There was a second door into
the same hole: `dma_handleDma` runs the clock on to the end of "the access
it interrupted", whose length it is given, and it was given the whole
budget. While any channel is doing HDMA, `burn_slice` now spends its slice a
scanline at a time, each piece ending just past the point where the core
raises the request, and hands `dma_handleDma` a real access's twelve cycles.
With no HDMA on it does exactly what it did, so the timing of everything
else is to the cycle what it was.

Counted in the core on levels 7 and 1 with the radar up, 1,095 ticks each:
requests overwritten before they were answered were 2,434 on level 1 with
only the first of the two mended, and are none on either level with both;
the box is on lines 48 to 105, the `--stock` lines, on every tick of both. The draw-list test is exact on the radar movie in
4:3 and 16:9 and on `level1` and `level25-lane`. The
one number that moved is the logos' wave, 1,114 line-ticks eased to 1,113:
that is HDMA too.

**And a third door: the wait for the sound chip.** Reported again, on
level 11 with the weed whacker cutting plants. `apu_play_sfx` (`$80:CC3B`)
is ported, and before it sends a sound effect it waits for the SPC to take
the last one. The port's wait (`apu_drive`) ran the clock with
`snes_runCycles` and answered no HDMA request, where each of the ROM's
`CPY $2143` reads answers one. The SPC is nearly always ready, so the wait
nearly never happens. With the weed whacker going it is not, and the wait
ran for dozens of lines. Each piece of the wait now calls `dma_handleDma`
as a read would. It changes nothing when there is no wait. Measured with the
draw-list test on 6,996 ticks of level 11 in 16:9, radar up, weed whacker
cutting: the box was off its lines on 94 ticks before, pushed down or
stretched as far as 94 lines tall, and is at line 48, 58 lines high, on all
of them now.

**Backdrops that step every few ticks.** The screens before the game are
layered and exact, and were not smooth: the LucasArts screen's textured
backdrop moves a pixel diagonally every *fourth* tick, the character
select's wallpaper every fifth, and the title's backdrop goes round a circle
eleven pixels at a time every fourth (`zamn_test_layers --motion` prints
every background's move per tick now, marked `r` where it is scrolled per
line). Eased from tick to tick that is a step over one tick and three or
four standing still -- fifteen pictures a second, on any display. A
background that has stepped at the same interval twice running
(`LAYERS_STEP_MIN` 3 to `LAYERS_STEP_MAX` 8 ticks) is taken to be stepping,
and each step is spread over the ticks up to the next (`stepK`, `stepI`,
`layers_spread`), arriving exactly as the next one lands: the title's
backdrop moves 2.75 pixels a picture at 240 Hz instead of 11 and then
nothing. That shows the backdrop a few ticks late, which on a backdrop
nobody steers is not felt; it is under `even` (`--no-even`), it is only backgrounds,
and it never fires in a level: over `level1` and `level25-boss` the only
spread ticks are on the title and the select screen.

Twice running is what a level needs -- there the background is the view,
and a player who stands five ticks and then walks has not stepped -- but it
cost every one of those screens its first fifth of a second: the LucasArts
backdrop stepped three times (twelve ticks) before it was believed, the
title's likewise, seen each time the screen came up. Outside a level
(`world`: BG2 a 64-column map on the main screen, the test `widescreen.h`
uses) a move after a rest of the right length is a step at once, unless the
interval before it is known and was different. Not the first move since
the screen went up (`fresh`): its rest is only how long the picture has
been there, and the card naming a level rests two ticks in sight and then
scrolls sixteen pixels a tick -- spread, its first tick crawled and its
second leapt. So in general a screen's first step is drawn as it comes and
its second is spread -- and that was still a delay one could see. But the
backdrops in question are all BG3, and outside a level the game moves BG3
one way only: on a counter, every fourth tick on the LucasArts screen
(`$80:938E`, `$38 & 3`) and the title (`$80:953B`, `sched_tick & 3`),
every fifth on the character select (`$80:9A1B`, `$5C`). So BG3's first
move since the screen went up is a step whatever came before it
(`LAYERS_BACKDROP_BG`), spread over `LAYERS_STEP_FIRST` (4) ticks at least,
its rest being the picture's age and not the interval. On the movies the
LucasArts backdrop is spread from its first step (frame 729, was 737), the
title's from its first (1159, was 1167) and the select's from its first
(1084, was 1094; that one takes four ticks of a five-tick interval and
waits one, a quarter of a pixel's worth). In the frontend as played
(`--widescreen 16:9 --skip-intro`), measured from its own pictures, the
title's backdrop stands for the three ticks before the game first moves
it and from then moves the same distance in every picture.
`zamn_test_layers` counts spreads that a move arrived in the middle of --
a wrong guess, which shows -- and names the frame.

**The game over mask.** A game over scrolls a 256-wide mask up over the
level on BG3 -- "GAME OVER" cut out of a purple field, the level showing
through the letters, which is the game's own look and not a transparency
bug -- and in a level BG3 is the status panel, split down the middle and
pinned to the picture's edges. Split, the mask left the middle third of a
16:9 picture bare. `widescreen_frame` tells the two apart by what the main
game thread is doing (`ws_game_over`): the thread that runs a level
(`$80:84B1`, whose spawn the entry table records as `$84B0`) parks in the
scheduler's wait, `$80:8353`, which pushes B, P and D over the `JSL`'s
return address and keeps the stack pointer in `thread_sp`; so the return
address read off that stack says where the thread is, and inside the game
over routine `$80:8A00` -- from the wait after the mask's tilemap has gone
up (`$8A11`) to the 300-tick wait with the mask fully up (`$8A45`), with
the scroll shadow `$136A` non-zero -- BG3 is the mask. There are two ways
into that routine, and the level loop `$80:8516` takes them differently: a
player's last life clears their panel flag (`hud_panel_on`, `$80:CEDA`) and
the loop returns when both are down; the last neighbour lost with none
rescued returns too, and the player's flag stays up. The flags were the
tell at first, and the second way -- the only one open under
`--invincible` -- split the mask again, found in play-testing on level 13.
(Two other tells were tried before that and were wrong: the scroll shadow
alone, which stays where it stopped into the next game, whose panel was
then centred and carried out to the edges, half a health bar and all; and
the layer's own columns -- the panel keeps the middle empty, the mask fills
it -- which missed the mask's first hundred frames, whose drips come in
over the panel's own columns.) While the mask is up
BG3 gets `ppu_wideCentre`, a policy of its own because neither of the old
ones was right for a whole screen: carried out from the console's place
(`ppu_wideClampEdge`) the mask sat off to one side wherever the two margins
were not the same width, which is at either end of a map, so
`ppu_wideCentre` puts the layer's middle at the picture's middle. Its
margins are drawn two ways. Beside the field and the letters they are the
field: the edge column carried out, or where the edge pixel is a gap, the
edge column a few lines into the nearest run of opaque pixels at or above
(the field the drips hang from, past a drip's dark outline). Beside the
drips at the mask's foot they are more drips -- the layer's 256 columns
repeated, as a 32-tile map repeats on the hardware -- so that the curtain
goes on to the edge of the picture rather than turning into a slab; a line
is beside the drips when it is below the last line the layer is opaque all
the way across and is a gap within 16 columns of both edges, which the
letters never are. A line with nothing opaque on it is below the mask and
shows what is behind. The test counts the frames the policy fires on: 782
of the poked game over movie, from the mask's upload to the fade, 782 of a
level 13 run whose last neighbour is taken at frame 3000, and none of
`level1`, `level1-2p`, `level21-spin`, `level25-boss` or the radar movie; a
movie that mashes Start into a new game after either game over has the
panel anchored again. To reach a game over without playing one,
`zamn_test_layers` and `zamn.exe` take `--poke` now
(`--poke 1330+:1CB8=0000` holds the player's health at zero from frame 1330;
`--poke 3000:1D52=0000` on `level13` takes the last neighbour, with none
rescued, for the other way in; adding `--poke 2000+:1B6A=0000` pins the
camera to the map's left edge, the uneven-margins case). The test's `--png`
prints where the main thread is parked and whether that is the game over,
and so does `--dump-pictures prefix,frame`, which
writes the four pictures of one tick as the renderer drew them and as
`layers_render` draws them, which is how the GPU path was found to be
pixel-identical to the software one on both Direct3D 9 and 11 -- and how a
draw list that named the wrong mask was found.

**The drips are sprites.** With the mask centred and its margins right, the
drips still went wrong: trunks that ended flat instead of in a drop, and
drops hanging in the air 43 columns from any trunk. Compared column by
column with the 4:3 picture, the mask's own columns differed in five-column
strips 64 lines tall, present in one and 43 columns away in the other --
which is what anchoring does to a sprite. The mask on BG3 draws only the
upper part of each drip; the hanging part and the drop at its end are
sprites of screen-space records, the same kind as the radar's markers, and
`widescreen_frame` was anchoring them with the panel while the mask they
hang from was centred. `ws_place_screen_sprites` (`ws_anchor_screen_sprites`
before) now places a screen-space sprite with whatever BG3 is carrying:
`ppu_spriteAnchored` with the panel, `ppu_spriteCentred` with the mask. A
centred sprite is drawn where the centred layer's column of the same number
is drawn, clipped to the layer's 256 columns, and again 256 columns either
side, clipped to that margin -- the margins repeat the layer's columns, so
the drips they repeat get their ends too (`ppu_evaluateSprites` finds such a
sprite up to three times, the draw list emits it up to three times with a
clip, and the exactness test holds). In 16:9 the mask's 256 columns now
match the 4:3 picture pixel for pixel in purple, and the margins match the
wrapped columns but for a world sprite's pixel, which rightly does not
repeat.

**Which sprites those are is read off the OAM, not assumed.** Bringing the
radar up flashed the player's head 43 columns to his left for a frame, and
now and then a marker 43 columns to the right of the box. The places are
given out at line 0 from the sprite pass's table of which record owns which
OAM entry, on the understanding that the table describes the OAM the vblank
just DMA'd. It mostly does not: the game starts its next tick straight
after that NMI, inside vblank, and by line 0 the pass has usually run
*again* -- on 497 of the 585 level frames of the radar movie the table was
a tick ahead of the screen. Nobody can tell while the same records keep the
same entries. The radar's marker takes entry 0 and moves everything else
along one, and is itself one entry multiplexed over the survivors, so for a
frame the entry the table called the marker's was still a piece of the
player on screen, pinned to the panel, and the marker was in an entry the
table still called the world's. The pass now keeps its last eight tables
with the bytes each wrote (`sprite_oam_history`, `src/port/oam.h`), and
`ws_pass_on_screen` takes the newest whose owned entries are what the PPU
holds; none matching is the newest, as before. `zamn_test_layers` reports
how often it was not the newest and how often none matched (0 over the
corpus in a level).

**The Konami star.** The first thing on the screen is a star drawn across it
with a line behind, and that is BG1: 16x16 tiles, a 64-column map, whose
second row is sixteen tiles of line, the star (its tile animated), and
black; the game scrolls it from 256 down to 0. `ppu_wideAuto` takes a
64-column map outside a level for one of which the game maintains the 32
the console shows, and repeats the console's 256: the right margin had a
second line through it from the start and a second star at the end, and the
left margin the black from the console's right, so the line began 43
columns in. The map itself is no better -- it has the line for the left
margin but nothing left of column 0 once the sweep ends, and its star stops
at column 256, short of a wider picture's edge. `ppu_wideSweep`, which
`widescreen_frame` sets on BG1 when the map's second row is that row
(`ws_konami_sweep`; it is in video memory from before the screen is lit
until the logo is gone), draws the layer shifted: by the left margin's
width leftward when nothing has been swept in, by the right margin's
rightward when all of it has, in proportion between
(`ppu_layerShiftX`). The star enters at the picture's left edge and leaves
by its right in the time it crossed the console, 10.75 pixels a tick in
16:9 rather than 8, and left of the map's column 0 the map is read as
column 0, so the line reaches the picture's edge as it reached the
console's. The draw list follows the layer by its scroll less the shift, so
the star is eased at the speed it is drawn at: 2.7 pixels a picture at
240 Hz. Off the widescreen the shift is 0 and nothing changes.

**The wallpaper is there from the first frame.** `ppu_wideAuto` continues a
256-pixel map into the margins only once it has seen the game scroll it --
a map that has never moved has a seam nobody has seen, and the card naming
a level printed the end of its last line down the far side. The character
select's wallpaper (BG3) comes up at scroll 0 and first steps on its fifth
tick, so for the first five ticks of the fade-in both margins were the flat
colour behind it, lighter than the wallpaper: a flash down both edges, on
frames drawn by the PPU (a faded sub screen is not a draw list), which is
why no picture of the list showed it. Outside a level, a BG3 with
something in every tile down both of the console's edge columns
(`ppu_columnFilledAt`) is a field and not a card with writing on it, and
`widescreen_frame` gives it `ppu_wideTile` from the first frame. The
LucasArts backdrop's first four ticks are covered the same way; the
title's spiral is not a field at its edges and is left to `ppu_wideAuto`,
as is the level card, which is BG1. Found by playing a movie made the way
a player gets there (idle to the title, then Start -- `level1.zmv` mashes
Start through the intro and the select comes up differently) in the
frontend itself, with `--dump-pictures` across the fade.

**A fade with the sub screen in it is a draw list too.** The character
select adds its film strips to the wallpaper through the sub screen, and
fades in over fifteen ticks. The console adds in five bits, clamps, and
then applies brightness; planes with the brightness baked in, added, clamp
at white instead of at the dimmed white and come out too bright wherever
the sum clamps -- so such a frame was refused ("sub screen in a fade") and
shown as the PPU drew it. That was the select's whole fade-in, a quarter of
a second at sixty pictures a second with nothing eased, and then the
smoothing cut in: the delay at the start of that screen that outlived the
step-spreading fixes above. Now such a frame is composed at full brightness
and the finished picture is dimmed (`layers_dim_late`, `LayersFrame.dim`:
a multiply at the end of `layers_render`, a colour modulation on the
target's last copy in `present_layers_draw`), which is the console's order.
The select is a draw list from its first lit frame, eased from its second,
and its fade-out likewise; the frontend's own pictures through the fade-in
ramp 17, 34 ... 255 as the PPU's did and differ picture to picture within
every tick from the second.

**The title's logo is eased a line at a time.** The logo is BG1, and the
game waves it in: `$80:9570` fills an HDMA table at `$7E:8000` with a sine a
line -- 127 pixels of it, four degrees a line, the phase a degree on every
tick -- until Start is pressed or it settles from the foot up. A background
whose scroll is rewritten down the frame was a raster effect, baked into
its plane and not eased at all, so the logo moved sixty times a second at
best. And mostly not that: the top third of the table changes every tick
and the rest only every fourth (the same on the stock ROM), so most of the
logo moved at 15 Hz, in steps of up to 9 pixels. Now a background whose
scroll varies *across only* is eased with each line a background of its own
(`LayersFrame.lineX`, `lineEase`): the plane already holds every line as
its scroll left it, a line drawn a little to one side is that line at a
scroll a little different, so the list draws the plane in strips, each back
by the part of its own move not yet made -- and a line that steps every few
ticks has its step spread like a stepping backdrop's. Strips that come out
the same are one op; a picture of the title is 40 to 150 ops. Not for a
layer with the sub screen added to it or a maths twin, which are drawn
against a whole plane's op. Unmoved it is still the PPU's frame (1145 of
1145 title frames identical, 4:3 and 16:9), and an edge of the logo
followed through 64 pictures now moves two target pixels a picture on a
line of the top third and one to three on a line of the rest, where it was
eight every fourth picture and twenty-eight every sixteenth.

**The menus move too.** The character select scrolls its film strips a pixel a
tick on one background and adds them, translucent, onto the wallpaper on
another; the list eases both, and the strips now advance a quarter of a pixel
per refresh at 240 Hz where before they stepped once per tick. The title
logo's sweep is a raster effect -- a scroll rewritten every line -- and is
drawn as the console drew it, unmoved.

**What it costs.** Taking a tick apart is about 1.3 ms, on the emulation
thread, which has a whole period to spare; drawing a picture is a few dozen
renderer calls. The machine still runs one tick ahead on its own thread, so
the latency is as it was: input read at the start of one period is fully on
screen at the end of the next, roughly a frame more than the plain loop. The
audio holds one more frame in its queue while smoothing is on, as before.

Measured, `--frames 1500 --paced` on `level1.zmv` at 240 Hz, windowed
(`--fullscreen` after `--frames` measures the screen as played; a fullscreen
run at 3840x2160 came out at 99.1% within a millisecond):

```
1500 frames in 25.3 s (59.4 fps).
  shown as 5997 pictures (4.00 per frame, 237.3 per second).
  394 ticks were shown as the PPU drew them, 1106 as layers; taking a tick apart took 1.24 ms (max 4.51).
  arrival  mean   4.21   min   2.07  p50   4.25  p90   4.75  p99   5.25  max 215.28 ms
  waiting  mean   1.25   min   0.00  p50   0.25  p90   4.25  p99   4.25  max   4.55 ms
  within 1 ms of the period: 98.7%   (100% is a perfectly even cadence)
```

The loop now idles between pictures (`waiting`), where the redraw used to fill
the period to the brim. The one 215 ms arrival is the level load, a single tick
of 226 ms on the emulation thread; the plain loop has the same stall on the
same frame, at 500 ms, so it is the game's and not the smoothing's.

Two more things were found by measuring on the screen rather than in a
window. The frontend was not DPI-aware, so on a 3840x2160 panel at 125%
scaling it rendered at 3072x1728 and Windows stretched that up; it now asks
for per-monitor DPI awareness and renders at the panel's size. And fullscreen
on Windows 10 and later is flipped straight to the panel rather than
composited, which the compositor line in the report says by staying silent.

Two things the list does not reproduce, on purpose. The console drops sprites
past 32 on a line and 34 tile slivers, and the list draws them all -- the same
choice widescreen already made, and no frame in the corpus trips it. And where
a sprite of one priority sits behind (in OAM order) a sprite of another, the
console hides it by OAM order alone; the list takes those pixels out of the
hinder sprite at the tick's own positions, so between ticks the hole can sit a
fraction of a pixel off. On the tick itself it is exact.

## Analyse
```
build\zamn_trace.exe  "Zombies Ate My Neighbors.sfc" -o analysis -f 2400 -m movies\level1.zmv
build\zamn_disasm.exe "Zombies Ate My Neighbors.sfc" analysis\zamn.cdl -b 80 -s tools\symbols\zamn.sym -o analysis\bank_80.asm
```

## Decode assets
Each `verify-*` command replays a movie under the reference core and diffs the C
decoders against the ROM's own routines as they run; the rest decode data out of
the ROM directly:
```
build\zamn_assets.exe verify-lzss    "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe verify-level   "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe verify-actors  "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe verify-sprites "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe verify-music   "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe level  "Zombies Ate My Neighbors.sfc" 2 level1.png
build\zamn_assets.exe route  "Zombies Ate My Neighbors.sfc" 18 284 420 563 513 --reach reach18.png
build\zamn_assets.exe probe  "Zombies Ate My Neighbors.sfc" 30 973 1475 --to 973 1227
build\zamn_assets.exe music  "Zombies Ate My Neighbors.sfc"
build\zamn_assets.exe spc    "Zombies Ate My Neighbors.sfc" 2 level2.spc --wav level2.wav
build\zamn_assets.exe sprite "Zombies Ate My Neighbors.sfc" 90:9172 zeke.png
build\zamn_assets.exe gfx    "Zombies Ate My Neighbors.sfc" 94:A300 tiles.png --lzss --pal 83:EE8C --pal-index 1 --scale 3
```

`route` breadth-firsts a walkable path through a level's own tile attributes;
`probe` asks `$80:AE14` — through `src/port/terrain.c`, the port the cosim
harness diffs against the ROM — whether the player can stand at a point, prints
the six tiles it reads, and with `--to` walks a leg two pixels a frame and names
the tile that stops it. `route` now walks its own plan that way before printing
it. See `docs/analysis-tools.md`.

## Port game logic
`verify` lets the ROM run the game and checks the C port against every call it
makes to a ported routine. `run` skips the ROM's instructions entirely and diffs
two cores per scheduler pass; `-r none` is the control, and must always pass.
Both modes end by reporting which of the port's branches the movie reached, and
`-c` prints the full table:
```
build\zamn_cosim.exe list
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_cosim.exe run    "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_cosim.exe run    "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -r none
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1-rescue.zmv -f 6100 -c
build\zamn_cosim.exe run    "Zombies Ate My Neighbors.sfc" -m movies\level1-rescue.zmv -f 6100
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level21-spin.zmv -f 4600 --twin-aim 20
```

`--twin-aim <period>[,<from>]` is a probe rather than a way to play: it patches
the cartridge the way twin-stick aiming does, arms the port the same way, holds
fire and cycles the aim through left, right and centred. Twin-stick is the one
thing in the project implemented **twice** — nine bytes of 65816 for the stock path and
the same decision in `src/port/player.c` for the substituted one — and `verify`
is the instrument for asking whether two implementations of a routine agree.
Without it the halves can only be checked apart, which is how the first version
of the flag came to work in an engine nobody plays in. It arms from frame 2,400
by default, past every movie's Start-mashing and any password it types.

`verify` over the **whole corpus** — every movie at the frame count it wants,
which is a table in the script rather than a property of the `.zmv` — plus the
two numbers no single run can produce: the branch-coverage union, and the
decline census summed across every input. This is what `PROGRESS.md`'s totals
are measured with:
```
powershell -ExecutionPolicy Bypass -File tools\verify_corpus.ps1
```

`-Lockstep` puts the same table through the stronger check: the port substituted
for real, and all 128 KB of WRAM compared once per scheduler pass rather than one
routine's answer per call. It costs about forty seconds a movie against one, and
it leaves `lzss_decompress` and `camera_follow` to the ROM — those two end the
comparison inside the first level load on nearly every movie, for a reason no
cost model can fix. `-Without none` runs it without that concession, which is
what the concession is measured against:
```
powershell -ExecutionPolicy Bypass -File tools\verify_corpus.ps1 -Lockstep
```

`-Only <wildcard>` narrows either pass to one movie, and says how many movies
its totals are totals over.

## Layout
```
src/headless.c        Phase 0a: boot ROM -> PNG (no SDL)
src/main_sdl.c        Phase 0b: interactive window + input + audio
src/scale.h           Where the framebuffer lands on the screen and how it gets
                              there — arithmetic only, no SDL, so it can be
                              checked without a window
src/present.h         ...and the SDL that carries that out
src/pad.h             Game controllers: the deadzone, the eight-way snap and the
                              button table — the half of it that decides how the
                              game feels, and is arithmetic
src/config.h          The player's `zamn.ini`: the settings that had only flags,
                              and every key and pad binding; the file it writes
                              when there is none, and the values written back
                              into one that is there
src/launcher.c        zamn_launcher: zamn.ini in a window, and a Play button
src/cheats.h          The six cheats: patches to the loaded image, words held
                              in WRAM once a tick, and two flags in the port
                              (`src/port/cheat.h`)
src/twinstick.h       Twin-stick shooting: what the right stick becomes — an aim
                              direction, and nine bytes of 65816 at `$80:D250`
src/pace.h            Frame cadence: measuring how evenly frames arrive, and
                              the deadline clock and audio rate control that
                              make them arrive evenly. No SDL either
src/layers.h          The frame taken apart -- planes, backdrop, sprites -- and
                              the draw list that puts it back a fraction of a
                              pixel from where the game left it. No SDL; draws
                              its own list in software for the test
src/present_layers.h  ...and the renderer that draws that list
src/smooth.h          What is left of the first smoothing: the ring arithmetic
                              and matching sprites by looks, for `--stock`
src/analysis/         Phase 1: 65816 table, CDL format, input movies (shared)
src/trace.c           Phase 1: instruction-level tracer -> CDL, memory map, call graph
src/disasm.c          Phase 1: CDL-driven annotated disassembler
src/assets/           Phase 2: LZSS, tiles, palettes, levels, placements, sprites, audio
                              (port code — ships in the game, libc only)
src/assets.c          Phase 2: asset decoding CLI + the five verifiers
src/port/             Phase 3: native game logic, on the SNES's own WRAM layout,
                              plus coverage.h — which of its branches ran
                              (port code — ships in the game, libc only)
src/cosim/            Phase 3: the co-simulation harness + per-routine shims,
                              plus waits.h — the ROM's busy-wait loops, which
                              are instructions but not work, and which every
                              measure of native share takes out of its
                              denominator (tooling — goes away in Phase 4)
src/cosim.c           Phase 3: verify / run / list CLI
movies/               Reproducible input scripts driving the tracer and harness
docs/                 Co-simulation, frame skeleton, WRAM map, asset formats, tools
tools/symbols/        Symbol names for the disassembler
third_party/lakesnes  Vendored SNES core (MIT) — reference emulator + PPU/APU
third_party/stb       stb_image_write.h, stb_truetype.h (public domain or MIT)
tools/make_icon.c     zamn_icon: the launcher's icon, the title screen drawn
                              from the cartridge at build time
tools/make_logo.c     zamn_logo: the launcher's heading, the title's logo,
                              from the cartridge at build time
tools/build.ps1       Sets up MSVC env, configures + builds with Ninja
tools/verify_corpus.ps1  Runs `verify` over every movie — or `run`, with
                              -Lockstep; owns the frame counts
tools/make_spin_probe.py Rewrites a probe movie's tail as short legs, so the
                              player faces every direction instead of towing a
                              crowd it never turns to shoot
tools/native_share.py What share of the work the game does runs natively, and a
                              ranking of what is left by the same measure — reads
                              `profile.bin` from the tracer, and discounts the
                              busy-waits, which are 23.9% of the instruction count
                              and none of the work. Reports "written" and
                              "actually substituted" separately, because a
                              `verify_only` routine is the first and not the
                              second; the second is what the game itself prints.
                              `--residue` ranks a profile the game wrote with
                              `--profile` instead: what is left, from real play
tools/hotbytes.py     Where inside a routine the instructions went. Run it on a
                              row of that ranking before porting it: a routine's
                              first byte is its entry, so nothing in a loop-free
                              routine can run more often — three rows near the top
                              have turned out to be spin loops wearing a name
tools/test_scale.c    Sweeps every window size from 1x1 to 5K and asserts what
                              `src/scale.h` promises. No SDL, no ROM
tools/test_present.c  Draws through `src/present.h` into a software renderer,
                              reads the pixels back and measures them — which is
                              the only way to show that `sharp` is sharp and
                              that the two filters are not the wrong way round
tools/test_pad.c      Sweeps the circle a tenth of a degree at a time, and drives
                              the device layer through a virtual controller — the
                              only way to check a quit chord or a pad unplugged
                              mid-press
tools/test_config.c   Holds `src/config.h` to its header: above all that the
                              file it writes reads back as exactly the defaults
tools/test_layers.c   Runs a movie and asks, on every frame, whether the draw
                              list drawn unmoved is the frame the PPU drew, and
                              if not why -- so the fallback rate is a number.
                              Needs the ROM and a movie
tools/test_cheats.c   The cheats' patches into an image and back out, what a
                              tick holds and what it must not, the demo, and the
                              port's handlers with the flag on and off. No SDL;
                              the ROM if it is there
tools/test_twinstick.c Nine stick positions becoming nine direction codes, and
                              the ROM patch as exact bytes against a synthetic
                              cartridge — including that every refusal writes
                              nothing at all. No SDL, no ROM
tools/perturb.py      Breaks one line of the port on purpose, rebuilds, runs
                              `verify` against every input listed for it, and puts
                              both back — a branch no input distinguishes, or one
                              only a single input does, is what it is looking for
```

`analysis/` is git-ignored: it is derived from your ROM and quotes it verbatim.

## Credits
- SNES core: [LakeSnes](https://github.com/elzo-d/LakeSnes) (MIT)
- ZAMN data-format reverse engineering: [Necrofy](https://github.com/Piranhaplant/Necrofy)
- Architecture model: [zelda3](https://github.com/snesrev/zelda3)
- PNG writing and font rendering: [stb](https://github.com/nothings/stb) (public domain or MIT)
