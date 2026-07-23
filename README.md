# Zombies Ate My Neighbors — native PC port

A personal project to build a native Windows port of the SNES game *Zombies Ate
My Neighbors*, following the `snesrev/zelda3` model: keep the SNES PPU/APU
emulated, but progressively reimplement the game logic as clean native C,
verified against the original ROM. See **`PLAN.md`** for the full plan and
**`PROGRESS.md`** for current status.

> You supply your own legally-obtained ROM (`Zombies Ate My Neighbors.sfc`).
> No game data is redistributed here.

## Status
**Phase 2 in progress** — the game boots and runs natively through a vendored
SNES core (Phase 0), an in-tree analysis toolchain produces a Code/Data Log,
memory map and annotated disassembly from reproducible traced runs (Phase 1),
and the asset pipeline has begun: the game's LZSS decompressor is reimplemented
in C and **verified byte-exact against the ROM's own routine**.

See **`docs/frame-skeleton.md`** for how the game's main loop works,
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
Check the C decompressor against the ROM's own routine on every call the game
makes while a movie plays, then decode data out of the ROM:
```
build\zamn_assets.exe verify-lzss "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe gfx "Zombies Ate My Neighbors.sfc" 94:A300 tiles.png --lzss --pal 83:EE8C --pal-index 1 --scale 3
```

## Layout
```
src/headless.c        Phase 0a: boot ROM -> PNG (no SDL)
src/main_sdl.c        Phase 0b: interactive window + input + audio
src/analysis/         Phase 1: 65816 table, CDL format, input movies (shared)
src/trace.c           Phase 1: instruction-level tracer -> CDL, memory map, call graph
src/disasm.c          Phase 1: CDL-driven annotated disassembler
src/assets/           Phase 2: LZSS + tile/palette decoders (port code, libc only)
src/assets.c          Phase 2: asset decoding CLI + the LZSS verifier
movies/               Reproducible input scripts driving the tracer
docs/                 Frame skeleton, WRAM map, asset formats, tool reference
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
