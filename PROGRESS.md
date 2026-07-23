# Progress Tracker

Cross-session status for the ZAMN native-port project. Update this whenever a
milestone lands. See `PLAN.md` for the full multi-phase plan.

## Current status: **Phase 0 complete** ✅ (2026-07-22)

Native Windows build of the game running through a vendored SNES core, with both
a headless (deterministic, for verification) and an interactive (playable) path.

## Environment (verified)
- **Toolchain:** VS2022 Community — MSVC 14.43 (`cl` 19.43), CMake 3.30.5 & Ninja
  1.12.1 (both bundled with VS), Python 3.13. None are on PATH by default; the
  build sources `vcvars64.bat` (see `tools/build.ps1`).
- **ROM:** `Zombies Ate My Neighbors.sfc` — LoROM, 8 Mbit (1,048,576 B), no
  coprocessor, no SRAM (password saves), NTSC. RESET=$80AE, NMI=$816C.

## What exists now
- `third_party/lakesnes/` — vendored LakeSnes SNES core (MIT), SDL-free. Serves
  as both the reference emulator and the emulated PPU/APU going forward.
- `third_party/stb/stb_image_write.h` — PNG writer for headless frame dumps.
- `src/headless.c` → **`zamn_headless.exe`** — boots ROM, runs N frames, dumps a
  512×480 PNG. Phase 0a verification tool.
- `src/main_sdl.c` → **`zamn.exe`** — interactive window + keyboard + audio.
  SDL2 is fetched & built from source by CMake (pinned `release-2.30.9`);
  `SDL2.dll` is copied next to the exe.
- `CMakeLists.txt`, `tools/build.ps1` (`-Clean` to wipe `build/`).

## How to build & run
```
pwsh tools/build.ps1            # configure + build (add -Clean to reset)
build\zamn.exe "Zombies Ate My Neighbors.sfc"          # play
build\zamn_headless.exe "Zombies Ate My Neighbors.sfc" out.png 500   # dump frame
```
Controls: Arrows=D-pad, Z=B, X=A, A=Y, S=X, Q=L, W=R, Enter=Start, RShift=Select, Esc=Quit.

## Verification done
- Headless dumps at frames 500/800/1200 render correctly (verified visually):
  Konami logo (gradient), LucasArts logo, animated intro spiral — colors, layers,
  and animation all correct. Confirms core + PPU + pixel path are sound.
- `zamn.exe` smoke-tested: launches, creates window, loads ROM, runs 3s, no crash.
  (Full interactive video/audio needs a human at the machine to eyeball/hear.)

## Next steps — Phase 1 (analysis & memory map)
1. **Human check:** actually play `zamn.exe` — confirm video is smooth, audio
   plays, controls respond, and pacing feels right (vsync-based; may need a
   proper frame clock if the display isn't ~60 Hz).
2. Install/point to **Mesen2** (trace logger, CDL, Lua) and **DiztinGUIsh**.
3. Play through in Mesen to build a **Code/Data Log**; start an annotated disasm.
4. Trace the frame skeleton from RESET `$80AE` and NMI `$816C`.
5. Begin the **WRAM map** (player, actor slots, level state, timers, RNG, HUD,
   password). Seed from Necrofy source + Data Crystal + romhacking.net.

## Known limitations / TODO (deferred, non-blocking)
- Frame pacing: **fixed 2026-07-22.** Was too fast (relied on vsync, which is
  >60 Hz / ignored on many displays). Now paced by **sync-to-audio** (audio queue
  is the clock → locks to 60 Hz + keeps A/V synced), with a monotonic-timer
  fallback when no audio device. `src/main_sdl.c` main loop.
- No gamepad mapping yet (keyboard only). No save states / config yet.
- `build/` PNG dumps are scratch artifacts (git-ignored).
- Windows-only by design for now (per project scope).
