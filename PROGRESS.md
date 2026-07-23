# Progress Tracker

Cross-session status for the ZAMN native-port project. Update this whenever a
milestone lands. See `PLAN.md` for the full multi-phase plan.

## Current status: **Phase 2 in progress** 🔨 (2026-07-23)

Compression, graphics, level layout, the actor/victim/object placement lists
**and the sprite/OAM path** are decoded as port code, and every one of them is
**verified byte-exact against the ROM's own routines** — the level check diffs
the full expanded map (18,304 tiles), the block library, the row tables, the
scalars, the tile attributes and both palettes; the actor check diffs the victim
and object arrays the ROM's parsers build; the sprite check diffs all 4,591 OAM
emissions the movie produces, whole-buffer. All 56 levels decode and render,
their placement lists all parse (every level has exactly 10 victims), and
metasprites render as recognisable art (Zeke, the title-screen lettering).

**Music/sequence data is the only Phase 2 format left.**

**Phase 1 complete** (2026-07-23) — analysis toolchain built in-tree; the frame
skeleton and a first WRAM map documented from traced execution.

**Phase 0 complete** (2026-07-22) — native Windows build of the game running
through a vendored SNES core, headless + interactive.

## Environment (verified)
- **Toolchain:** VS2022 Community — MSVC 14.43 (`cl` 19.43), CMake 3.30.5 & Ninja
  1.12.1 (both bundled with VS), Python 3.13. None are on PATH by default; the
  build sources `vcvars64.bat` (see `tools/build.ps1`). Use `powershell`, not
  `pwsh` — PowerShell 7 is not installed.
- **ROM:** `Zombies Ate My Neighbors.sfc` — LoROM, 8 Mbit (1,048,576 B), no
  coprocessor, no SRAM (password saves), NTSC. RESET=$80AE, NMI=$816C.

## What exists now

### Phase 0 — runtime
- `third_party/lakesnes/` — vendored LakeSnes SNES core (MIT), SDL-free. Serves
  as both the reference emulator and the emulated PPU/APU going forward.
- `third_party/stb/stb_image_write.h` — PNG writer for headless frame dumps.
- `src/headless.c` → **`zamn_headless.exe`** — boots ROM, runs N frames, dumps a
  512×480 PNG.
- `src/main_sdl.c` → **`zamn.exe`** — interactive window + keyboard + audio.
  SDL2 is fetched & built from source by CMake (pinned `release-2.30.9`).

### Phase 1 — analysis
- `src/analysis/` — shared library: 65816 instruction table (`w65816.c`), CDL
  format + LoROM address mapping (`cdl.c`), input movies (`movie.c`). The Phase 3
  co-simulation harness reuses all three.
- `src/trace.c` → **`zamn_trace.exe`** — steps the core one instruction at a time
  with scripted input and emits a CDL, WRAM/register maps, a call graph, a DMA
  log, and loop-collapsed instruction traces. The core is **not** modified.
- `src/disasm.c` → **`zamn_disasm.exe`** — CDL-driven annotated 65816 listing.
- `movies/boot.zmv`, `movies/level1.zmv` — reproducible input scripts;
  `level1.zmv` reaches actual gameplay (verified by `--png` frame dump).
- `tools/symbols/zamn.sym` — evidence-backed symbol names.
- `docs/analysis-tools.md`, `docs/frame-skeleton.md`, `docs/wram-map.md`.

### Phase 2 — assets
- `src/assets/` — **port code, not tooling**: it ships in the finished game, so
  it depends on nothing but libc. `lzss.c` (a byte-exact port of `$80:CD20`),
  `gfx.c` (planar tiles + BGR555 palettes), `rom.c` (LoROM address mapping),
  `level.c` (the level record, block-library decompression and tilemap
  expansion — a port of the loader at `$80:86A2`), `actor.c` (the three
  placement lists — actors, victims, objects — a port of the parsers at
  `$81:80EC`, `$82:DB46`, `$80:C9A5`), `sprite.c` (16x16 frames, metasprites and
  OAM composition — a port of `$80:BA51` and its three flipped twins).
- `src/assets.c` → **`zamn_assets.exe`** — decodes ROM data to `.bin`/`.png`,
  and `verify-lzss` / `verify-level` / `verify-actors` / `verify-sprites` diff
  the C decoders against the ROM's own routines under the reference core.
  `level <n> [out.png]` and `actors <n>` report and render any of the 56 levels;
  `sprite <bank:addr> [out.png]` and `frame <n> [out.png]` do the same for
  sprites.
- `docs/asset-formats.md` — the LZSS stream layout, the tile/palette encoding,
  the full level format (record table, block library, expansion), the sprite
  frame/metasprite/OAM format, and how each decoder was checked.

## How to build & run
```
powershell -ExecutionPolicy Bypass -File tools\build.ps1     # add -Clean to reset
build\zamn.exe "Zombies Ate My Neighbors.sfc"                # play
build\zamn_headless.exe "Zombies Ate My Neighbors.sfc" out.png 500
build\zamn_trace.exe "Zombies Ate My Neighbors.sfc" -o analysis -f 2400 -m movies\level1.zmv
build\zamn_disasm.exe "Zombies Ate My Neighbors.sfc" analysis\zamn.cdl -b 80 -s tools\symbols\zamn.sym -o analysis\bank_80.asm
build\zamn_assets.exe verify-lzss "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe verify-level "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe verify-actors "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe verify-sprites "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe level "Zombies Ate My Neighbors.sfc" 2 out.png
build\zamn_assets.exe actors "Zombies Ate My Neighbors.sfc" 2
build\zamn_assets.exe sprite "Zombies Ate My Neighbors.sfc" 90:9172 zeke.png
build\zamn_assets.exe frame "Zombies Ate My Neighbors.sfc" 0x463 frames.png --count 24
```
Controls: Arrows=D-pad, Z=B, X=A, A=Y, S=X, Q=L, W=R, Enter=Start, RShift=Select, Esc=Quit.

## Verification done
- Phase 0: headless dumps at frames 500/800/1200 render correctly (verified
  visually). `zamn.exe` played and confirmed by hand.
- Phase 1: `movies/level1.zmv` reaches in-game level 1 — confirmed by the
  tracer's own final-frame PNG (HUD, Zeke, a victim, a chainsaw maniac on
  screen). Trace findings cross-check each other: the DMA log's OAM source
  ($7E:13BE, 544 B → $2104) matches the WRAM region report's 562-byte
  write-only buffer at the same address, and the LZSS ring buffer inferred from
  `$80:CD20` matches the 4096-byte region at `$7E:6F00`.
- Phase 2: `verify-lzss` intercepts all 5 decompression calls `level1.zmv`
  reaches and finds the C port byte-identical to `$80:CD20` — output, byte
  count, *and* the final 4 KB window. Confirmed non-vacuous: flipping one bit of
  the C output makes all 5 fail and the tool exit non-zero. Tile and palette
  decoding is an eyeball check against sources named in `dma_log.csv` — the
  hardware encoding has no ROM routine to diff against — and both a compressed
  and an uncompressed source render as legible glyphs.
- Phase 2 (level): `verify-level` lets the game load level 1 (record `$9F:9060`,
  table entry 2) and diffs 15 things against the WRAM `$80:86A2` just built — all
  pass, including the **entire 18,304-tile expanded map** in bank `$7F`, the
  32 KB block library, the two row-base tables, the derived scroll scalars, the
  1 KB tile-attribute table and both 256-byte palettes. All 56 levels decode via
  `zamn_assets level` with no errors, and maps from every tileset group render as
  coherent art (the map *values* are what `verify-level` proves exactly).
- Phase 2 (actors): `verify-actors` replays the same movie and, as each ROM
  parser finishes, diffs the flat array it built against `actors_read()`'s decode
  — all 4 checks pass for record `$9F:9060`: the victim count (`$7E:6E30`) and 10
  positions vs `$82:DB46`, and the object count (proven by the `$C000` sentinel
  landing at our count) and 9 positions+types vs `$80:C9A5`. Non-vacuous: an
  off-by-one on the object breakpoint made the sentinel check fail until fixed.
  All 56 levels' placement lists parse with no errors — every level lists exactly
  10 victims. The actor list itself is decoded and reported but its runtime spawn
  (`$81:80EC`, camera-driven) is left for Phase 3.
- Phase 2 (sprites): `verify-sprites` intercepts all four OAM emitters, and for
  each call snapshots the ROM's 544-byte OAM buffer plus the emitter's arguments,
  then re-runs the composition in C and diffs the **whole buffer** and the
  resulting OAM index. All **4,591** emissions on `level1.zmv` are byte-identical
  (3,572 unflipped, 1,019 flipped horizontally) across 40 distinct metasprites in
  `$8F:DF86..$90:CBA3`, and the three slot/OAM tables are proven to match our
  formulas for all 128 entries. Non-vacuous twice: changing the mirror constant
  from `-16` to `-15` broke exactly the 1,019 flipped calls and nothing else, and
  perturbing one palette bit broke the unflipped ones. Independent cross-check:
  the frames a metasprite names resolve to exactly the sprite-graphics addresses
  `analysis/dma_log.csv` records being uploaded, and rendering it draws the word
  "PASSWORD"; `$90:9172` draws Zeke mid-run. **Gap:** the movie never flips
  vertically, so `$80:BB30`/`$80:BBA6` are ported from their ROM bytes but not
  yet diffed against execution.

## Key findings (Phase 1)
- **ZAMN runs a 24-slot cooperative thread scheduler with per-thread stacks**
  (`thread_yield` at `$80:8353`, the most-called routine in the game). The main
  loop is a `WAI` inside it. **This is the single biggest input to Phase 3** —
  game logic suspends mid-routine, so a plain C function cannot replace a
  routine one-for-one. See `docs/frame-skeleton.md` → *Porting consequences*.
- All code runs from the `$80-$BF` FastROM mirror; direct page is pinned at
  `$0000` for the whole game; the IRQ vector is never taken.
- NMI does: frame counter → re-entrancy guard → force blank → VRAM upload queue →
  vblank job queue A → unblank → joypad read → job queue B → return.
- Graphics reach VRAM through exactly one choke point (the queue at `$7E:1B84`
  drained by `$80:9E7B`) — the natural seam for Phase 5's renderer.
- Compression is 4K-window **LZSS** (`$80:CD20`, ring buffer at `$7E:6F00`),
  consistent with what Necrofy documents.
- CDL coverage from one boot-to-level-1 movie: **18.3%** of ROM (13,767 bytes of
  code, 177,817 of data, 182 subroutines, 362 call edges).

## Next steps — Phase 2 (asset pipeline)
1. ~~Confirm our LZSS implementation against `$80:CD20` byte-for-byte.~~ ✅
2. ~~Tile and palette decoding, cross-checked against `dma_log.csv`.~~ ✅
3. ~~**Level layout** — record table at `$9F:8000`, block library, tilemap
   expansion. Ported (`src/assets/level.c`) and verified byte-exact against
   `$80:86A2` by `verify-level`; all 56 levels decode.~~ ✅
4. ~~**Actor/victim/object placements** — the `$9F`-internal record pointers
   `+$1C`/`+$1E`/`+$20`. Ported (`src/assets/actor.c`) and, for victims and
   objects, verified byte-exact against `$82:DB46`/`$80:C9A5` by `verify-actors`;
   all 56 levels' lists parse.~~ ✅
5. ~~**Sprite graphics** — 16x16 frames in a flat array at `$84:8000` (4096 of
   them), metasprites in banks `$8F`/`$90`, and the OAM composition that turns
   one into the other. Ported (`src/assets/sprite.c`) and verified byte-exact
   against `$80:BA51`/`$BABA` by `verify-sprites`.~~ ✅
6. **Music/sequence data** — the last Phase 2 format. Nothing is known about it
   yet; the DMA log has no APU traffic in it, so start from the SPC upload at
   boot and the `$80:9F8E`/`$80:9FD7` bulk transfers.
7. Extend `movies/` — password screen, level transition, a boss, two-player — to
   push CDL coverage up; every report regenerates automatically, and each new
   movie widens every `verify-*` command's coverage for free. Two known gaps it
   would close: the vertical-flip OAM emitters, and levels beyond the first.

Deferred to **Phase 3** (runtime state, not ROM formats): the actor slot tables
(`$7E:0300` stride `$100`, `$7E:1872` stride `$14`) the camera-driven spawner
`$81:80EC` fills, the animation state that chooses an actor's metasprite from
frame to frame, and the 128-slot VRAM frame cache (`$80:B9D6`).

## Known limitations / TODO (deferred, non-blocking)
- Frame pacing fixed 2026-07-22: paced by sync-to-audio, with a monotonic-timer
  fallback when no audio device (`src/main_sdl.c`).
- No gamepad mapping yet (keyboard only). No save states / config yet.
- `analysis/` and `build/` are scratch (git-ignored). `analysis/` in particular
  contains verbatim ROM bytes and must never be committed.
- Windows-only by design for now (per project scope).
