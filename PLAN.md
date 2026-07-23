# Native PC Port of *Zombies Ate My Neighbors* (SNES) — Plan

## Context

The working directory contains a single artifact: `Zombies Ate My Neighbors.sfc` (an 8 Mbit SNES ROM). The
goal is a **native Windows port** of the game — not an emulator wrapper, but real native code — built as a
**clean, maintainable C reimplementation** in the style of the well-known `snesrev/zelda3` project. The payoff
features you care about are **widescreen / higher internal resolution** and **framerate + quality-of-life
improvements**. This is a personal / learning project, so distribution constraints are relaxed, but the design
still loads all game data from your own ROM at runtime (no asset redistribution needed, and it makes mods work).

### ROM technical profile (verified from the ROM header)
- **Mapping:** LoROM, SlowROM (`$FFD5 = $20`)
- **Size:** 1,048,576 bytes = 8 Mbit, no copier header
- **Chipset:** `$FFD6 = $00` → **ROM only, no coprocessor** (no SA-1 / Super FX / DSP). This is the *simplest*
  possible SNES profile to reimplement.
- **Save:** RAM size `$00` → **no SRAM/battery**; the game saves via **passwords**.
- **Region:** NTSC (USA), stock 65816 CPU + SPC700 audio.
- **Entry points:** RESET = `$80AE`, NMI = `$816C` (these anchor the main loop / vblank handler).

### Why the zelda3 model fits
`zelda3` keeps the SNES **PPU and SPC700/DSP emulated** (they render/produce audio from VRAM/OAM/DSP state),
but reimplements the **CPU-side game logic as hand-written native C**. Correctness is guaranteed by a
**co-simulation harness**: the original ROM runs in an embedded 65816 emulator in lockstep with the C code, and
**WRAM is compared every frame** — any divergence pinpoints an unported or buggy routine. ZAMN is a top-down
run-and-gun that is *simpler and smaller than Zelda 3* (no Mode 7 overworld, no coprocessor, no SRAM), so this
approach is very tractable here.

### Head start: the community has already reverse-engineered ZAMN's data formats
`Piranhaplant/Necrofy` (the modern ZAMN level editor, open-source C#) already decodes ZAMN's **compression,
tilesets, level layout, sprites, palettes, and music**. We reuse its format knowledge to load assets directly
from the ROM — which also means Necrofy-authored level/graphics/music hacks load in the port for free.

---

## Recommended Approach

A staged, correctness-first build that reaches your widescreen/QoL goals last (they require the game logic to
already be native). Each phase leaves a runnable artifact.

### Phase 0 — Foundations & reference oracle
- New repo. Build system: **CMake + Ninja**, MSVC or clang-cl. Windowing/input/audio/GPU blit via **SDL2/SDL3**.
- **Vendor a small, readable SNES core** to serve two jobs: (a) the *reference emulator* for the diff harness,
  and (b) the *emulated PPU + SPC700/DSP* the finished port keeps using. Recommended: **LakeSNES** (compact C,
  the same core `zelda3` and `snesrecomp` build on). Alternatively lift `zelda3`'s bundled `snes/` directory.
- Boot the ROM under this core in an SDL window. **Deliverable:** the game runs (emulated) natively on Windows —
  a playable baseline *and* the ground-truth oracle for everything that follows.

### Phase 1 — Static/dynamic analysis & memory map
- Disassemble and trace with **Mesen2** (trace logger, Code/Data Logger, Lua scripting) as the primary dynamic
  tool, and **DiztinGUIsh** (LoROM-aware interactive disassembler) to grow an annotated disassembly. Optionally
  `AndreaOrru/gilgamesh` for whole-program static flow.
- Build a **CDL** (code-vs-data map) by playing through the whole game in Mesen.
- Trace the frame skeleton from RESET `$80AE` and NMI `$816C`: vblank-driven update, main game-state loop.
- Produce a **WRAM map** (player state, enemy/actor slots, level/tile state, timers, RNG, HUD, password state).
  Seed it from Necrofy's source, the Data Crystal wiki, and romhacking.net ZAMN maps.

### Phase 2 — Asset pipeline (reuse Necrofy formats)
- Port the *format-decoding logic* from Necrofy (C#) into C, and **load assets at runtime from the user's ROM**
  (graphics, tilesets, palettes, level definitions, sprites, music/sequence data). No extraction/redistribution
  step; matches the zelda3 model and keeps mod compatibility.

### Phase 3 — Incremental logic reimplementation under the diff harness
- Stand up **co-simulation**: reference ROM (LakeSNES) and the C reimplementation share an identical WRAM
  layout. Start with the C side delegating everything to the emulator, then **replace one routine at a time**,
  asserting the per-frame **WRAM (+ VRAM/OAM/DSP) diff stays byte-identical** to the reference.
- Suggested port order (each step validated by a clean diff):
  boot/init → main loop & NMI → input → player movement → weapons/items → enemy AI (slot-based actors) →
  collision → level/tile streaming & camera → HUD → victim/rescue logic → doors/warps → **password system** →
  bosses → title/menus.
- Keep the **emulated PPU and SPC700** the whole time; feed them exactly as the original code did, so rendering
  and audio "just work" once the C code's VRAM/OAM/DSP writes match the reference.

### Phase 4 — Cut the reference loose
- When a full recorded playthrough diffs clean, remove the reference emulator from the *runtime* (keep it as a
  test-only harness). CPU-side logic is now 100% native C; only PPU + SPC700 remain emulated (as in zelda3).

### Phase 5 — Enhancements (your actual goal)
- **Widescreen / hi-res:** with logic native, widen the camera and object culling to spawn/track actors beyond
  the original 256 px field (zelda3 does exactly this for 16:9). Either drive the emulated PPU's region-export
  path or add a native tile/sprite renderer for higher internal resolution and clean scaling.
- **Framerate & QoL:** decouple the update step from 60 Hz where safe; **save-states become trivial** (WRAM is
  now a C struct — dump/restore it), plus remappable controls, fast-forward, quick pause/resume.
- **Modding (free byproduct):** because assets load from the ROM via Necrofy's formats, Necrofy-made hacks run.

---

## Key resources (external — there is no local codebase to modify yet)
- **`snesrev/zelda3`** — architecture + co-simulation harness pattern to mirror.
- **`Piranhaplant/Necrofy`** — ZAMN compression, level, tileset, sprite, palette, and music formats.
- **LakeSNES** (or `zelda3`'s `snes/`) — compact C SNES core for the reference + emulated PPU/APU.
- **`mstan/snesrecomp` / `sp00nznet/snesrecomp`** — a working SNES static-recompiler ecosystem; useful as a
  cross-reference and a possible fast-path fallback (it auto-translated SMW and Mega Man X to native C).
- **Mesen2** (debug/trace/CDL/Lua), **DiztinGUIsh** (disassembly), **gilgamesh** (static analysis).
- **Data Crystal / romhacking.net** — ZAMN RAM & ROM maps.

## Verification
- **Primary (automated):** per-frame **WRAM/VRAM/OAM/DSP equality** between the reference ROM and the C
  reimplementation. Drive both with the *same recorded input movie* (TAS-style); assert zero divergence. This is
  the zelda3 gold standard and it makes correctness objective rather than eyeballed.
- **Milestone playthroughs:** title → level 1 clear → password save/restore → a boss, compared against Mesen.
- **Regression:** store recorded input movies; a headless CI job replays them and compares WRAM hashes.

## Honest risk assessment
- **Effort:** months of focused work. ZAMN is materially smaller/simpler than Zelda 3, which helps.
- **Top risks:** SPC700 audio-driver quirks, faithful **RNG** replication, and undocumented enemy/boss AI.
- **Mitigation:** keeping the APU emulated sidesteps most audio risk; the per-frame diff harness surfaces any
  RNG/AI divergence the instant it happens, so bugs are localized to the one routine you just ported.

## First concrete step after approval
Scaffold the Phase 0 repo (CMake + SDL + vendored LakeSNES) and get `Zombies Ate My Neighbors.sfc` booting in a
native window as the reference oracle — the foundation every later phase builds on.
