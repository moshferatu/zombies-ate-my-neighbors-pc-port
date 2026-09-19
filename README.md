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
Controls: Arrows = D-pad · Z=B X=A A=Y S=X · Q=L W=R · Enter=Start · RShift=Select · Esc=Quit
· **F1 = toggle native substitution** · **F2 = cycle scaling**
· **F3 = toggle aspect** · **F4 = cycle widescreen** · **F5 = toggle smoothing**
· **F6 = toggle even motion** · **F11 / Alt+Enter = fullscreen**

**Game controllers** work too, and are the way to actually play it: any pad SDL
recognises — which is most of them, and a `gamecontrollerdb.txt` beside the
executable covers the rest — hot-pluggable, with the first two taking the two
SNES ports. So two pads is two players, and a pad plus the keyboard is also two
players. Face buttons are positional: the bottom one is B, the right one A, the
left one Y, the top one X. Worth knowing before you start rather than after,
because it is not where a modern game would put it: **Y is this game's fire
button**, and it is held rather than tapped — B and A cycle weapons and items, X
uses one. Shoulders and triggers are both L and R; the left stick steers as well
as the D-pad, snapped to eight ways with a deadzone that has to be crossed
further to enter than to leave.

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

### Skipping the intro

```
build\zamn.exe --skip-intro
```

Konami, LucasArts, a story screen and then the title: **19.2 seconds** before the
menu is up, which is a long time to sit through once and an absurd one to sit
through on every launch of a build you are testing. `--skip-intro` runs those
frames as fast as the machine can — 3.55 s here, so about 5x — and hands over
with START/PASSWORD on screen and no button held.

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

The intro runs through the same substitution the game does, so those frames
count toward the figures reported at exit. `--skip-intro` cannot be combined
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
make it 8:7 — visibly narrow, and 11% less screen. `--aspect` picks, **4:3 by
default**, `square` for the framebuffer's own shape; F3 toggles them on the same
frame, which is the only way to judge it. At 3840x2160:

| | picture | of the screen |
| --- | --- | --- |
| whole buffer, square pixels | 2304x2160, 144px of it blank top and bottom | 56% |
| cropped, square pixels (8:7) | 2468x2160 | 64% |
| cropped, 4:3 (default) | **2880x2160** | **75%** |

### `--widescreen`

`off` (default), `16:9` or `16:10`. **F4** cycles them while the game runs.

Not a stretch and not a crop: the PPU draws columns either side of the
console's 256, so a wider screen shows *more of the level* at the same size.
That is possible because almost nothing had to move to allow it. The game's
coordinates are untouched — column 0 is still column 0 — and the three things
that would normally break turn out already to be in the port's favour:

* **The map is already there.** BG2's tilemap is 64 tiles across, twice the
  screen, and the streamer keeps the columns around the camera valid rather
  than only the visible ones. 16:9 wants 43 columns a side and the ring has 128.
* **The actors are already alive.** `actor_cull` keeps anything from 128 px
  behind the camera to 383 px ahead of it (`src/port/oam.c`) — a 512-pixel
  window around a 256-pixel screen, which is twice what 16:9 asks for. So
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
`ratio × 192` columns: 4:3 gives back exactly 256, 16:9 wants 342 and 16:10
wants 308. `src/scale.h` states the pixel shape once and derives all three,
which is why the 4:3 cases pinned in `tools/test_scale.c` did not move.

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
| `integer` | only whole multiples, letterbox the rest — and therefore square pixels, so it ignores `--aspect` | perfectly uniform; 1920x1080 fits 2x and leaves 17% of the height black |
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

Every run prints this at exit; `--paced` keeps 60 Hz pacing under `--frames` so a
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
game, F5 toggles it, `--no-smooth` starts without it, and on a 60 Hz panel it
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

**Uneven steps are evened out**, and F6 or `--no-even` turns that off. The
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
and has to be counted in every delta. A move
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
nobody steers is not felt; it is under `even` (F6), it is only backgrounds,
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
16:9 picture bare. `widescreen_frame` tells the two apart by what the game
says about the panel: `hud_panel_on` (`W_HUD_PANEL_ON`, one word per side)
is whether that player is in the game, and both are zero through a game
over, from before the mask's first drip until the next game puts the panel
up again. (Two other tells were tried and were wrong: the scroll the game
over counts BG3 down with, `$136A`, stays where it stopped into the next
game, whose panel was then centred and carried out to the edges, half a
health bar and all; and the layer's own columns -- the panel keeps the
middle empty, the mask fills it -- missed the mask's first hundred frames,
whose drips come in over the panel's own columns.) While the mask is up
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
shows what is behind. The test counts the frames the policy fires on: 831
of the poked game over movie, from the first drip to the top scores, and
none of `level1`, `level1-2p`, `level21-spin`, `level25-boss` or the radar
movie; a movie that mashes Start into a new game after the game over has
the panel anchored again. To reach a game over without playing one,
`zamn_test_layers` and `zamn.exe` take `--poke` now
(`--poke 1330+:1CB8=0000` holds the player's health at zero from frame 1330;
adding `--poke 2000+:1B6A=0000` pins the camera to the map's left edge, the
uneven-margins case). `--dump-pictures prefix,frame`
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
third_party/stb       stb_image_write.h (public domain)
tools/build.ps1       Sets up MSVC env, configures + builds with Ninja
tools/verify_corpus.ps1  Runs `verify` over every movie — or `run`, with
                              -Lockstep; owns the frame counts
tools/make_spin_probe.py Rewrites a probe movie's tail as short legs, so the
                              player faces every direction instead of towing a
                              crowd it never turns to shoot
tools/native_share.py What share of the work the game does runs natively, and a
                              ranking of what is left by the same measure — reads
                              `profile.bin` from the tracer, and discounts the
                              busy-waits, which are 10.9% of the instruction count
                              and none of the work. Reports "written" and
                              "actually substituted" separately, because a
                              `verify_only` routine is the first and not the
                              second; the second is what the game itself prints
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
tools/test_layers.c   Runs a movie and asks, on every frame, whether the draw
                              list drawn unmoved is the frame the PPU drew, and
                              if not why -- so the fallback rate is a number.
                              Needs the ROM and a movie
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
