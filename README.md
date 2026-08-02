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

Phase 3 is the logic port. The co-simulation harness is built, sixty-one routines
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
passes **9,811,421 of 9,811,421 across the whole movie corpus**, thirteen levels
deep. Across all forty-two movies the ROM is **no longer asked to run a single
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
the wrong reason. 77 of 317 are still untaken by every input, and they are the
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
· **F1 = toggle native substitution**

**This is the substituted build, not the emulated baseline.** It installs the
same `COSIM_NATIVE` interception `zamn_cosim run` uses, against the one live
core: every call the game makes to a ported routine is executed by the C port
instead of by the 65816, and the window title carries a live count of how many.
`--stock` clears the enable mask to get the Phase 0 emulated baseline back, and
F1 moves between the two at a frame boundary while the game is running.

What this is *not* is a native game yet. Sixty-one routines are ported; the
main loop, the NMI handler, the movement thread, level code, the camera itself
and every enemy body still belong to the ROM under the emulated core. What runs
natively are the leaves those call — the sprite/OAM pass, the depth sort,
collision dispatch, the step proposer and every test a step is checked against,
thread spawn and tick, score, fades, and now the bottom of the tilemap streamer
the camera scrolls with. Phase 4 is where that inverts.

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
level 45's. `level25-lane.zmv` at 9,400 frames is the one that does **not**
match, and for a reason that is not a wrong answer: on that level a scheduler
pass sits close enough to the vblank boundary that the two builds eventually
disagree about whether one overran it, after which they are showing different
moments of the same game. `zamn_cosim run` detects and reports exactly that —
see `docs/cosim.md`. On exit the frontend prints the same per-routine table `zamn_cosim`
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
src/analysis/         Phase 1: 65816 table, CDL format, input movies (shared)
src/trace.c           Phase 1: instruction-level tracer -> CDL, memory map, call graph
src/disasm.c          Phase 1: CDL-driven annotated disassembler
src/assets/           Phase 2: LZSS, tiles, palettes, levels, placements, sprites, audio
                              (port code — ships in the game, libc only)
src/assets.c          Phase 2: asset decoding CLI + the five verifiers
src/port/             Phase 3: native game logic, on the SNES's own WRAM layout,
                              plus coverage.h — which of its branches ran
                              (port code — ships in the game, libc only)
src/cosim/            Phase 3: the co-simulation harness + per-routine shims
                              (tooling — goes away in Phase 4)
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
                              and none of the work
tools/hotbytes.py     Where inside a routine the instructions went. Run it on a
                              row of that ranking before porting it: a routine's
                              first byte is its entry, so nothing in a loop-free
                              routine can run more often — three rows near the top
                              have turned out to be spin loops wearing a name
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
