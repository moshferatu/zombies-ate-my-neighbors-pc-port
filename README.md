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
dispatch and the twenty-five actor handlers it routes to, the game's **random
number generator**, the two searches every enemy uses to pick who to chase
and to ask what is in its way, and **the whole of the movement step validator**:
where a mover wants to go, and all four tests that decide whether it may — what
is in the way, what the terrain under it is, whether it is still on the map, and
whether it has reached the end of its co-op leash — and the coroutine problem is solved: a ported
routine that suspends inside the thread scheduler does it at an explicit resume
point, with its parked state as plain copyable data. `zamn_cosim verify` checks
the C against the
ROM's own code on every call the game makes — 128 KB of WRAM plus registers — and
passes **11,094,719 of 11,094,719 across the whole movie corpus**, thirteen levels
deep. Across all forty-three movies the ROM is **no longer asked to run a single
routine the port does not have**.

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
the wrong reason. 87 of 407 are still untaken by every input, and they are the
backlog.

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
· **F3 = toggle aspect** · **F11 / Alt+Enter = fullscreen**

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
multiple of the console's rate (60.0988 Hz NTSC, which no monitor offers), and
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
the final frame, `--no-audio` skips the audio device. The three together are how
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
build\zamn_assets.exe music  "Zombies Ate My Neighbors.sfc"
build\zamn_assets.exe spc    "Zombies Ate My Neighbors.sfc" 2 level2.spc --wav level2.wav
build\zamn_assets.exe sprite "Zombies Ate My Neighbors.sfc" 90:9172 zeke.png
build\zamn_assets.exe gfx    "Zombies Ate My Neighbors.sfc" 94:A300 tiles.png --lzss --pal 83:EE8C --pal-index 1 --scale 3
```

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
```

`verify` over the **whole corpus** — every movie at the frame count it wants,
which is a table in the script rather than a property of the `.zmv` — plus the
two numbers no single run can produce: the branch-coverage union, and the
decline census summed across every input. This is what `PROGRESS.md`'s totals
are measured with:
```
powershell -ExecutionPolicy Bypass -File tools\verify_corpus.ps1
```

## Layout
```
src/headless.c        Phase 0a: boot ROM -> PNG (no SDL)
src/main_sdl.c        Phase 0b: interactive window + input + audio
src/scale.h           Where the framebuffer lands on the screen and how it gets
                              there — arithmetic only, no SDL, so it can be
                              checked without a window
src/present.h         ...and the SDL that carries that out
src/pace.h            Frame cadence: measuring how evenly frames arrive, and
                              the deadline clock and audio rate control that
                              make them arrive evenly. No SDL either
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
tools/verify_corpus.ps1  Runs `verify` over every movie; owns the frame counts
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
