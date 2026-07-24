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

Phase 3 is the logic port. The co-simulation harness is built, and the first
five routines are through it: `zamn_cosim verify` checks the C against the ROM's
own code on every call the game makes — 128 KB of WRAM plus registers — and
passes **11,503 of 11,503**, while `zamn_cosim run` substitutes the C for real
and finds **no byte of live game state differing** across 2,389 scheduler
passes. Porting a routine that *suspends* inside the thread scheduler is the
next real problem.

See **`docs/cosim.md`** for the harness, **`docs/frame-skeleton.md`** for how
the game's main loop works, **`docs/wram-map.md`** for the memory map,
**`docs/analysis-tools.md`** for the analysis tools, and
**`docs/asset-formats.md`** for the data formats.

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

Headless frame dump (for verification / debugging):
```
build\zamn_headless.exe "Zombies Ate My Neighbors.sfc" frame.png 500
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
build\zamn_assets.exe music  "Zombies Ate My Neighbors.sfc"
build\zamn_assets.exe spc    "Zombies Ate My Neighbors.sfc" 2 level2.spc --wav level2.wav
build\zamn_assets.exe sprite "Zombies Ate My Neighbors.sfc" 90:9172 zeke.png
build\zamn_assets.exe gfx    "Zombies Ate My Neighbors.sfc" 94:A300 tiles.png --lzss --pal 83:EE8C --pal-index 1 --scale 3
```

## Port game logic
`verify` lets the ROM run the game and checks the C port against every call it
makes to a ported routine. `run` skips the ROM's instructions entirely and diffs
two cores per scheduler pass; `-r none` is the control, and must always pass:
```
build\zamn_cosim.exe list
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_cosim.exe run    "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_cosim.exe run    "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -r none
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
src/port/             Phase 3: native game logic, on the SNES's own WRAM layout
                              (port code — ships in the game, libc only)
src/cosim/            Phase 3: the co-simulation harness + per-routine shims
                              (tooling — goes away in Phase 4)
src/cosim.c           Phase 3: verify / run / list CLI
movies/               Reproducible input scripts driving the tracer
docs/                 Co-simulation, frame skeleton, WRAM map, asset formats, tools
tools/symbols/        Symbol names for the disassembler
third_party/lakesnes  Vendored SNES core (MIT) — reference emulator + PPU/APU
third_party/stb       stb_image_write.h (public domain)
tools/build.ps1       Sets up MSVC env, configures + builds with Ninja
```

`analysis/` is git-ignored: it is derived from your ROM and quotes it verbatim.

## Credits
- SNES core: [LakeSnes](https://github.com/elzo-d/LakeSnes) (MIT)
- ZAMN data-format reverse engineering: [Necrofy](https://github.com/Piranhaplant/Necrofy)
- Architecture model: [zelda3](https://github.com/snesrev/zelda3)
