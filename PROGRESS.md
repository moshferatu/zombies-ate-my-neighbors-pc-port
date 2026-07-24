# Progress Tracker

Cross-session status for the ZAMN native-port project. Update this whenever a
milestone lands. See `PLAN.md` for the full multi-phase plan.

## Current status: **Phase 3 underway** 🔨 (2026-07-24)

The co-simulation harness PLAN.md calls for is **built and load-bearing**,
eleven routines are ported under it, **the coroutine problem is solved**, and
**the whole per-frame sprite pass is native.** `zamn_cosim verify` checks the
port against the ROM's own code call by call — all 128 KB of WRAM plus
registers — and passes **16,599 of 16,599**. `zamn_cosim run` then *substitutes*
the C for real and runs two cores in lockstep: over 2,389 scheduler passes no
byte of live game state ever differs, the only differences being inside the
stacks and one declared scratch slot.

**`sprite_build_oam` (`$80:BD1F`) is the one this round was about.** It is the
routine `scheduler_idle` calls once a frame and the one the whole sprite path
hangs off: sort the display list, cull it to the camera, blank OAM, walk the
survivors and emit each one's metasprite, then test every visible pair for an
overlap. All 1,016 calls pass, whole-WRAM, first try. Two things make it a
different kind of milestone from the nine before it:

* It is the first ported routine that **calls other ported routines**, and the
  first place Phase 2's work is load-bearing inside Phase 3. The OAM composition
  it runs is `sprite_emit()` — unchanged, the same function `verify-sprites`
  proved byte-exact against 4,591 real emissions. What was missing was the
  caller, and that is what this adds.
* It is the first port that is **honestly partial**. The overlap pass it ends
  with dispatches into actor handlers (`$80:BE8F` → `$80:8480`) that nobody has
  ported, so the port declares a **guard**: it inspects each call first and
  hands back the ones it cannot serve, which the ROM then runs itself, in both
  modes, counted and printed. `level1.zmv` declines 0 of 1,016 — no two visible
  actors in it ever come within 16 pixels. See `docs/cosim.md` → *Half a
  routine, honestly*.

Eight of the eleven are leaves that never yield: `sprite_frame_tile` (the
128-slot VRAM frame cache Phase 2 explicitly deferred), `sprite_cache_age`,
`thread_tick_waits`, both vblank-queue adders, and the three routines
`sprite_build_oam` opens with — `actor_depth_sort`, `actor_cull` and
`oam_buffer_clear`. Those three plus `actor_overlap_pass` are the port code that
walks the game's own data structure, the 32-record sprite display list at
`$7E:185E`, rather than a table the scheduler owns.

The last one is the one that mattered most. **`fade_in` (`$80:891A`) suspends
inside `thread_yield` and resumes fifteen times, and it is ported, verified and
substituted.** The decision deferred since Phase 1 is made: a ported routine
suspends at an **explicit resume point, with its parked state as plain copyable
data** — not fibers. The deciding argument came from the harness itself: `verify`
works by rewinding WRAM and replaying the port over it, and a fiber's parked
machine stack cannot be rewound, so choosing fibers would mean porting the
hardest part of the game with the checking turned off. See **`docs/threads.md`**
for the full argument, the mechanism, and the carry bug the stronger checking
found.

## Phase 2 complete ✅ (2026-07-23)

Compression, graphics, level layout, the actor/victim/object placement lists,
the sprite/OAM path **and the audio upload path** are decoded as port code, and
every one of them is **verified byte-exact against the ROM's own routines** —
the level check diffs the full expanded map (18,304 tiles), the block library,
the row tables, the scalars, the tile attributes and both palettes; the actor
check diffs the victim and object arrays the ROM's parsers build; the sprite
check diffs all 4,591 OAM emissions the movie produces, whole-buffer; the music
check diffs every byte and command the game puts on the APU ports. All 56 levels
decode and render, their placement lists all parse (every level has exactly 10
victims), and metasprites render as recognisable art (Zeke, the title-screen
lettering).

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
  OAM composition — a port of `$80:BA51` and its three flipped twins),
  `music.c` (the APU data-set table, the driver image, both upload protocols and
  the command interface — a port of `$80:CB1A`/`$80:CB61`/`$80:CC7C`).
- `src/assets.c` → **`zamn_assets.exe`** — decodes ROM data to `.bin`/`.png`,
  and `verify-lzss` / `verify-level` / `verify-actors` / `verify-sprites` /
  `verify-music` diff the C decoders against the ROM's own routines under the
  reference core. `level <n> [out.png]` and `actors <n>` report and render any of
  the 56 levels; `sprite <bank:addr> [out.png]` and `frame <n> [out.png]` do the
  same for sprites; `music` reports the 16 APU data sets and what each level
  plays, and `spc <n> [out.spc] [--wav f]` dumps any level's music as a
  playable `.spc` (or plain audio) by capturing the APU after the ROM's own
  upload — a song has no static decode, so this is the only route to one.
- `docs/asset-formats.md` — the LZSS stream layout, the tile/palette encoding,
  the full level format (record table, block library, expansion), the sprite
  frame/metasprite/OAM format, the audio upload path, and how each decoder was
  checked.

### Phase 3 — native game logic + co-simulation
- `src/port/` — **port code**, libc only, the native game logic itself.
  `wram.h` is the load-bearing decision: the port keeps the SNES's WRAM layout
  byte for byte, so a ported routine reads and writes the same 128 KB at the
  same offsets the 65816 code does. That is what makes the diff possible at all,
  and it makes Phase 5's save states a one-line `fwrite`. `sprite_cache.c` (the
  128-slot VRAM frame cache — `$80:B9D6`/`$80:B9C7`), `thread.c`
  (`$80:8398` and both vblank-queue adders), `oam.c` (**the whole per-frame
  sprite pass** — `$80:BD1F` and the four routines it calls,
  `$80:BC7F`/`$80:BCE2`/`$80:BC23`/`$80:BEC9` — plus the 32-record display list
  at `$7E:185E` they all walk), `coroutine.h` (**how a ported routine
  suspends** — one `resume` index plus a context struct, ~40 lines) and `fade.c`
  (`$80:891A`, the first routine ported that uses it).
- `src/cosim/` — the harness (tooling, not port code; it goes away in Phase 4).
  `cosim.c` is the engine — snapshot, intercept, diff, substitute, lockstep —
  and `routines.c` is the registry plus one *shim* per routine that translates
  the 65816 calling convention. The shim/port split is deliberate: without it,
  "port code" drifts into 65816 written in C. A routine may also declare a
  **guard**, which is how a port that covers only part of a routine says so: the
  engine asks it before every call and hands the ones it declines back to the
  ROM, in both modes, counted in the report's `decl.` column.
- `src/cosim.c` → **`zamn_cosim.exe`** — `verify` (ROM drives, port is checked
  per call — per *segment*, for a routine that suspends), `run` (port drives,
  two cores diffed per scheduler pass), `list`.
- `docs/cosim.md` — the design, what each mode proves, what the diff forgives
  and why, and the carry-flag bug that only one of the two modes could catch.
- `docs/threads.md` — **the coroutine decision**: why resume points and not
  fibers, how a suspending routine is checked segment by segment, how native
  mode suspends by jumping to the routine's own `JSL thread_yield`, and what is
  still open.

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
build\zamn_assets.exe verify-music "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_assets.exe music "Zombies Ate My Neighbors.sfc"
build\zamn_assets.exe spc "Zombies Ate My Neighbors.sfc" 2 level2.spc --wav level2.wav
build\zamn_assets.exe level "Zombies Ate My Neighbors.sfc" 2 out.png
build\zamn_assets.exe actors "Zombies Ate My Neighbors.sfc" 2
build\zamn_assets.exe sprite "Zombies Ate My Neighbors.sfc" 90:9172 zeke.png
build\zamn_assets.exe frame "Zombies Ate My Neighbors.sfc" 0x463 frames.png --count 24
build\zamn_cosim.exe list
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_cosim.exe run "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
build\zamn_cosim.exe run "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -r none
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
- Phase 2 (music): `verify-music` watches the instructions that store to the APU
  ports, so what it compares is what the hardware sees. All 8 checks pass: the
  39,558-byte driver image the ROM stages in WRAM `$7F:0000` is byte-identical
  to ours, its IPL block list matches the destinations written to `$2142`, all
  39,546 payload bytes handed to the boot ROM match, and all 5 data-set uploads
  `level1.zmv` performs are byte-identical command streams (23,820 commands).
  The 15 ROM-resident sets are packed end to end and provably non-overlapping,
  and all 56 levels name a song in `2..11` and a sample set in `0..3`.
  Independent cross-check: those 5 uploads carry **23,766** payload bytes, which
  is exactly the `$80:CCAB → $80:CCC8` call count in `analysis/callgraph.csv` for
  the same movie. Non-vacuous twice: simplifying the odd `$0A` parameter to the
  low length byte broke exactly the four sets with blocks over 255 bytes and left
  the sound-effect bank passing, and moving the driver's second staging source by
  one byte broke all three driver checks. **Gap:** the movie only drives sets 1,
  3, 6 and 15; the other songs and sample sets decode statically but have not
  been diffed against an upload.

- Phase 3 (verify): `zamn_cosim verify` replays `level1.zmv` and checks each of
  the eleven ported routines against the ROM's own on **every call the game
  makes** — the whole 128 KB of WRAM plus A/X/Y and the flags each shim claims.
  **16,599 of 16,599 pass** (10,354 `sprite_frame_tile`, 1,016 each for
  `thread_tick_waits`, `actor_depth_sort`, `actor_cull`, `oam_buffer_clear`,
  `actor_overlap_pass` and `sprite_build_oam`, 107 `vbl_queue_b_add`, 24
  `vbl_queue_a_add`, 2 `sprite_cache_age`, and `fade_in`'s 16 segments). The
  only WRAM waived is
  derived, not declared: per call — per *segment*, for `fade_in` — it is the
  window between the deepest the stack pointer went and where it started,
  **2 bytes** for the leaf routines that push, **0** for the five that do
  not, **3** for `fade_in` (exactly the return address its own `JSL
  thread_yield` pushes) and **9** for `sprite_build_oam`, which pushes B and D
  and then nests — plus one declared 2-byte scratch slot (`$7E:0038`, which
  `sprite_frame_tile`, `actor_depth_sort` and `sprite_build_oam` all declare,
  for the caller's X and the sort's walk-predecessor respectively).
  Non-vacuous five times:
  starting the LRU eviction scan one slot late failed on the very first call at
  `sprite_lru_slot`; returning the wrong register in a shim failed on Y after
  4 calls; a wrong VRAM destination failed at `sprite_upload_dest`; widening the
  camera cull window by one pixel failed on `visible_actor_count` after 446
  calls; and inverting the depth sort's Y key failed after 129 calls at a display
  record. A sixth perturbation was instructive rather than caught: forcing the
  `ACTOR_SORT_FIRST` branch of the sort still passed all 1,016 calls, because
  `level1.zmv` never presents two records that disagree on that bit — a real
  coverage gap the movie work (item 4 below) would close.
- Phase 3 (the sprite pass): `sprite_build_oam` (`$80:BD1F`) passes **all 1,016
  calls** whole-WRAM, and it is the first ported routine that calls other ported
  routines — all three of its openers, `sprite_frame_tile` through the emitters,
  and `actor_overlap_pass` at the end. It is also where Phase 2 becomes
  load-bearing inside Phase 3: the composition it runs is `sprite_emit()`,
  unchanged, the function `verify-sprites` already proved against 4,591
  emissions. Non-vacuous five times, each failing at the exact byte: dropping
  the `ACTOR_Z` subtraction failed at `sprite_origin_y` after 421 calls; a wrong
  base priority constant failed at `sprite_attr_or` after 129; never flipping
  failed inside the OAM buffer after 359; a one-pixel screen-space origin failed
  at `sprite_origin_x` after 129; and advancing the metasprite pointer by the
  *emitted* piece count instead of the *walked* one failed at
  `sprite_pieces_left` after 325 — which is what proves pieces really are
  dropped off-screen mid-metasprite, and validates the one thing this work
  added to Phase 2's emitter (a trace of the direct-page walk state the ROM
  leaves behind). **Gaps:** three deliberate perturbations were *not* caught,
  and each is a branch the movie never takes — the `ACTOR_ATTR_SET` palette
  override (disabling the whole branch still passed), `ACTOR_PRIORITY_TOP`
  (only the low-priority side is exercised), and the overlap test's box size
  (see below).
- Phase 3 (**half a routine, honestly**): `actor_overlap_pass` (`$80:BEC9`) is
  the first port that covers only part of what the ROM's version does. Its walk
  is ported; what it does on a hit is not, because `$80:BE8F` hands the pair to
  `$80:8480`, which `RTL`s into an actor's own handler. So the routine declares
  a **guard**: the harness asks it before every call, on a throwaway copy of
  WRAM, and a `false` means the ROM's own instructions run instead — in both
  modes, counted in the report's `decl.` column. The rule that keeps this
  honest is that a decline is an enumerated condition the port detects, never a
  fallback for a failed diff. `level1.zmv` declines **0 of 1,016**. Non-vacuous
  three ways: writing the wrong walk cursor failed after 129 calls; **dropping
  the geometry test entirely made 673 of 1,016 calls decline** — which proves
  the walk really does reach the box test, and that the decline path works end
  to end — and widening the box from 16 px to 32 produced no declines at all.
  Only at 128 px do 413 passes find a pair. **Gap:** the box test's threshold is
  transcribed from the listing and unexercised, like `ACTOR_SORT_FIRST`.
- Phase 3 (the coroutine): `fade_in` (`$80:891A`) is the first ported routine
  that does not run to completion — it sets brightness to 0 and then sleeps a
  frame between each of fifteen increments. It was chosen because it is the
  smallest routine in the game that suspends and calls nothing but
  `thread_yield`, so its whole observable effect is one word of WRAM.
  `verify` checks it **once per segment** — the run between two suspensions,
  with WRAM and the registers re-snapshotted at every resumption because
  arbitrary other threads ran in between — and **16 of 16 segments** are
  identical, registers and flags included. `run` substitutes all sixteen and
  the game reaches gameplay unchanged. Non-vacuous four times: sleeping 2 ticks
  instead of 1 failed at segment 0 on the sleep count; ending the loop at 14
  instead of 15 failed at segment 14 with "the ROM suspended, the port
  returned"; a wrong A on return failed on A; and see below.
- Phase 3 (**carry, again**): the engine at first compared no registers at a
  suspension, reasoning that `thread_yield` clobbers A/X/Y so nothing at the
  `JSL` is readable. Wrong, and wrong the same way the queue-adder bug was:
  `thread_yield` opens with `PHP`, so the flags at the `JSL` are parked *with the
  thread* and handed back by `PLP`. Comparing them found a real error the same
  hour: the shim claimed carry passed through untouched, which is what the
  preceding `LDA #$0001` implies — but the loop reaches that `LDA` by falling
  through `CMP #$000F`, which borrows for every brightness below 15 and clears
  carry. Only the *first* suspension, entered from the top of the routine, skips
  that `CMP`. One segment in sixteen differs from the other fifteen. **An
  unclaimed output is an unchecked output, and a suspension is an exit like any
  other.**
- Phase 3 (run): `zamn_cosim run` actually substitutes the C — the ROM's
  instructions never execute — and diffs two cores' full WRAM once per scheduler
  pass. **Control first:** with nothing substituted the two cores are identical
  at all 2,389 compared passes, so the machinery is deterministic. With all
  eleven substituted, at most **14 bytes of 131,072** ever differ and every one
  of them, on all 2,389 passes where anything differed, is inside the stacks
  (`$7E:1000-$7E:12FF`) or the declared scratch slot — **no byte of live game
  state ever differs**, and the run reaches gameplay. One thing in that report
  needs reading correctly: with `sprite_build_oam` substituted, the ROM never
  reaches its five callees, so they report **not reached** under `run`. They are
  running — the port calls the port's versions — they are just no longer
  intercepted. `verify` still exercises every one of them on every call. This is
  what porting upwards looks like and it will keep happening. The cycle budget
  each
  substituted call burns is measured by `verify`, not guessed. A substituted
  *suspension* costs nothing extra to get right: native mode puts the sleep count
  in A and jumps to the routine's own `JSL thread_yield`, so the core performs
  the real suspension with the real stack footprint, which is why `fade_in`
  leaves no stale bytes at all.
- Phase 3 (the bug the harness earned its keep on): the two vblank-queue adders
  return their verdict **in the carry flag**, and their shims first modelled
  only N and Z. `verify` passed all 131 calls — correctly, since it compares
  only the flags a shim claims. Under `run`, the caller at `$82:AE3A`
  (`JSL : WAI : BCS <back>`) spun forever and the routine was entered **147,405
  times instead of 107**. Modelling carry fixed it, and `verify` then passed
  again with carry compared — which also proved `sprite_frame_tile` returns
  carry clear on all 10,354 calls. **An unclaimed flag is an unchecked output.**
- Phase 3 (comparing at the right instant): `docs/frame-skeleton.md` predicted
  that the harness must compare at the `WAI` in `scheduler_idle`, not at a PPU
  frame boundary, and it was right twice. Comparing at vblank caught the cores
  mid-way through a bulk table fill and blamed the port; and once substitution
  shifts timing, "compare if both happen to be at the `WAI`" silently fell to
  **320 of 2,400 boundaries (13%)** while still reading as a pass. Stepping each
  core by one scheduler pass instead compares 2,389 of 2,400.

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

## Phase 2 checklist — asset pipeline (complete)
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
6. ~~**Music/sequence data** — the data-set table at `$80:CCDE`, the staged
   driver image and both upload protocols. Ported (`src/assets/music.c`) and
   verified byte-exact against the APU port writes by `verify-music`. The port
   never interprets a note: the SPC700 stays emulated, so reproducing the
   upload traffic reproduces the audio.~~ ✅
7. Extend `movies/` — password screen, level transition, a boss, two-player — to
   push CDL coverage up; every report regenerates automatically, and each new
   movie widens every `verify-*` command's coverage for free. Three known gaps it
   would close: the vertical-flip OAM emitters, levels beyond the first, and the
   songs/sample sets no upload has driven yet.

## Next steps — Phase 3 (continued)

The harness works, the coroutine question is answered (`docs/threads.md`), the
sprite pass is native, and eleven routines are through it — ten leaves and one
that suspends. What is left is scale rather than shape.

1. **The collision dispatch, `$80:BE8F` → `$80:8480`.** This is now the single
   named hole in an otherwise complete sprite path, and it is the door into
   actor behaviour rather than a routine: `$80:8480` reads an actor's thread
   slot (`$7E:1300`/`$7E:1330`), builds a call frame out of it and `RTL`s into
   the actor's own handler, which may itself yield. Porting it means porting
   what "an actor's handler" is, which is the same machinery item 2 and item 3
   need. Until then `actor_overlap_pass` and `sprite_build_oam` declare a guard
   and hand those calls back — 0 of 1,016 in `level1.zmv`, but that number is a
   property of the movie, not of the game, and a movie with two actors touching
   would raise it immediately. **That makes item 4 a prerequisite for measuring
   this, not just for coverage.**
2. **Port a routine that yields from inside a call it makes.** `fade_in` yields
   at its own top level, which is the easy half. The nested case needs the callee
   resumable too, with its own `PortCoro` in the caller's context, and it is the
   one part of the decision that is designed but not yet exercised. The harness
   will not paper over it: a yield whose return address is outside the ported
   routine's body is deliberately left unmatched rather than misattributed.
   `$80:8516` (the gameplay thread, yields at two sites and calls six unported
   routines) is the realistic target once more of its callees exist.
3. **Then the actor slot tables** (`$7E:0300` stride `$100`) that the
   camera-driven spawner `$81:80EC` fills, and the animation state that picks an
   actor's metasprite frame to frame. Deferred from Phase 2 as runtime state
   rather than ROM format; the harness is what will check them. (The `$7E:1872`
   stride-`$14` table listed here before is now identified — it is the display
   list at `$7E:185E` this work ported, `docs/wram-map.md`.)
4. **Extend `movies/`** (Phase 2 checklist item 7, still open). Every new movie
   widens `verify` and `run` for free, exactly as it does the `verify-*`
   commands. **This is now the binding constraint on most of what is ported, not
   a nice-to-have.** One movie's worth of gameplay leaves four branches
   transcribed-but-unexercised, each demonstrated by a perturbation that the
   diff should have caught and did not: the `ACTOR_SORT_FIRST` sort key
   (`actor_depth_sort`), `ACTOR_ATTR_SET` and `ACTOR_PRIORITY_TOP`
   (`sprite_build_oam`), and the overlap box threshold (`actor_overlap_pass`).
   Separately, `fade_in`'s entire sample is one call — sixteen segments is all
   there is — and `fade_out` (`$80:8933`) is never reached at all. A movie in
   which two actors touch would also be the first to make the collision guard in
   item 1 decline anything.

## Known limitations / TODO (deferred, non-blocking)
- **`run` proves nothing inside `$7E:1000-$7E:12FF`** (the stacks). A
  substituted routine does not push what the ROM's version pushed, and that
  residue reshuffles as later calls push and pop at different depths. Everything
  outside that range — all 127 KB of game state — is compared byte for byte, and
  `verify` covers the routines' own stack effects exactly. Rationale in
  `docs/cosim.md` → *What the diff forgives*.
- **A substituted call returns on a measured cycle budget, not the real cost.**
  Nothing has diverged because of it yet, but the budget is a per-routine mean
  and the ROM's own cost varies with its input (`sprite_frame_tile`: 288..1190).
  It is also a slight over-count, because the measured figure includes the
  `RTS`/`RTL` (or, for a suspension, the `JSL thread_yield`) that native mode
  then makes the core execute for real — about 24 master cycles inside a
  ~57,000-cycle frame. This stops mattering in Phase 4, when the reference is
  cut loose.
- **A guarded routine's coverage is only as good as the movie.** `verify` and
  `run` decline exactly the same calls, and both print the count, so a decline is
  never silent — but "0 declined" on one movie says the port served every call
  *that movie made*, not that it can serve every call. See `docs/cosim.md` →
  *Half a routine, honestly*.
- **Two activations of the same ported routine at once are not distinguished.**
  A suspension is attributed to the innermost in-flight call whose body contains
  the yield's return address; if two threads were ever inside the same ported
  routine simultaneously that would be ambiguous. No routine ported so far can
  be. See `docs/threads.md` → *What is not settled yet*.
- 11 of 2,400 passes go uncompared by `run`: one before the scheduler exists,
  and ten where a side never returned to the `WAI` within the step.
- Frame pacing fixed 2026-07-22: paced by sync-to-audio, with a monotonic-timer
  fallback when no audio device (`src/main_sdl.c`).
- No gamepad mapping yet (keyboard only). No save states / config yet.
- `analysis/` and `build/` are scratch (git-ignored). `analysis/` in particular
  contains verbatim ROM bytes and must never be committed.
- Windows-only by design for now (per project scope).
