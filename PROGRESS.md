# Progress Tracker

Cross-session status for the ZAMN native-port project. Update this whenever a
milestone lands. See `PLAN.md` for the full multi-phase plan.

## Current status: **Phase 3 underway** 🔨 (2026-07-24)

The co-simulation harness PLAN.md calls for is **built and load-bearing**,
eighteen routines are ported under it, **the coroutine problem is solved**,
**the whole per-frame sprite pass is native**, and **the game can now be played
by two people under it.** `zamn_cosim verify` checks the port against the ROM's
own code call by call — all 128 KB of WRAM plus registers — and passes **106,357
of 106,357** on `level1-rescue.zmv` and **69,793 of 69,793** on the two-player
movie. `zamn_cosim run` then *substitutes* the C for real and runs two
cores in lockstep: over 6,089 and 5,989 scheduler passes respectively, no byte of
live game state ever differs, the only differences being inside the stacks and
one declared scratch slot.

**The decline census is empty of everything but a sound effect.** `$83:A364` —
the victim's handler, and the last address any movie's census named — is ported,
verified and substituted. On `movies/level1-2p.zmv` the report now prints **no
census section at all**: nothing declines, anywhere, and `thread_call_handler`,
`actor_collide_notify`, `actor_overlap_pass` and `sprite_build_oam` all serve
**every call the movie makes**. On `level1-rescue.zmv` the one line left is
`player id table $80:F92D 1`, which is a single `JSL apu_play_sfx` — it writes no
WRAM at all, so it is not a WRAM-diff problem and never was. **Everything the
collision path does to memory is now native.**

`victim_collide` is a latch, and the smallest kind of handler there is: read
`$1E`, and if anything is already there this victim's fate is settled and the
routine is two instructions. Otherwise eight `CMP`s, each with its own two or
three instructions behind it, writing a code into `$1E` for the victim's own
thread to wake on. Five of the eight also clear `ACTOR_COLLIDE_ID` in the display
record — a victim switching its own collision off so nothing can claim it twice,
which is what `shot_collide` does to a spent shot through a different field of a
different page.

**Two of the eight ids are the two players, and the diff proved it.** They differ
in one word: id 5 latches the id *itself* into `$18` (`$83:A392  BRA` skips the
`LDA #$8000` the other falls into — three bytes saved), id 6 latches `$8000`.
`$18`'s reader is `$83:A1EA  LDA $18 : JSL $80C7D9`, the rescue thread on the same
page, and `score_add` reads bit 15 of it and nothing else. On
`level1-rescue.zmv` the id-5 path runs five times and `score_slot_0` is credited
four times against `score_slot_1`'s zero. A perturbation pinned the rest:
latching `$8000` for id 5 failed at `$7E:0418` with ROM `$05` against port `$00`.

Six perturbations, and **the one that was not caught is the finding.** Deleting
the latch guard outright — letting a settled victim be claimed a second time —
passes every call on all three movies, because none of them ever dispatches to
the same victim twice. That is the same shape as `shot_collide`'s fifth `CMP`:
the port would be *more permissive* than the ROM and no diff can see it.
`victim_latched` is the coverage site that names it, and it is untaken. The five
that were caught landed at `$7E:041E` (which is how the victim's page is known to
be based at `$7E:0400`), `$7E:0418`, `$7E:1A60` and `$7E:1A38` inside the display
list — proving the routine reaches outside its own page — and on flag C both at
its own entry and at `$7E:11A6` one level up, independently, because carry set is
what parks the thread.

**One coverage number went down, and that is correct.** `handler_unported` used
to fire on the declines to `$83:A364`; it is now untaken by every movie, because
there is no handler address left for the dispatcher to decline. A decline site is
a site like any other. So there are **72** marked sites now and their union across
the three movies is **48**, not 46 plus three.

**The round before this was about what the movies could not reach**, and it started with a
finding that costs a sentence in two files: **`Y` is the fire button, and `B`
does nothing.** `movies/level1.zmv` and `movies/level1-rescue.zmv` both hold `B`
for thousands of frames under comments saying they are shooting, and the ammo
counter reads 150 at the first frame of gameplay and 150 at the last of both. So
the number recorded here as a property of the game — 1,226 collisions containing
exactly **one** enemy taking damage — was a property of a movie in which nothing
was ever fired. Checked by holding each of `B Y A X L R` for 120 frames on a
standing player: only `Y` moves the counter. Both movies keep their exact input
bytes (every count in this file was measured against them, and a baseline that
quietly changes cannot regress) and their comments now say what they do.

**`.zmv` grew a second controller.** A frame may carry a `2:` prefix that aims
the line at port 2; the two ports are independent event streams with their own
absolute semantics, so a stretch where one player does nothing costs no lines,
and frames are checked for ascending order *within* a port rather than silently
dropped. A movie with no `2:` lines means precisely what it always did, which
both older movies prove to the call: their totals were 16,599 and 106,351 then
and the format change moved neither. (They read 16,601 and 106,357 now — the
difference is routines ported since, not the movies.) One
`movie_apply()` in `src/analysis/movie_apply.h` is now the only place a movie
meets the core, across all five tools that replay one.

**`movies/level1-2p.zmv` is the payoff**, and it closes the longest-standing gap
in the project. `emit_flip_y` fires **1,318** times, and `zamn_assets
verify-sprites` on it intercepts **24,183 emissions across all four emitters** —
966 flip-y and 352 flip-xy among them — every one byte-identical, over **127
distinct metasprites** against `level1.zmv`'s 40. `$80:BB30`/`$80:BBA6` were
transcribed from ROM bytes in Phase 2 and had never once executed; they are now
diffed against execution. It also takes `player_ignore` (3,272 — two players
touching is the only thing that hands a player's handler an id of its own side),
`score_slot_1` (18, against `score_slot_0`'s 33 — with one player the slot search
at `$80:C7C2` is the identity and proves nothing), `player_no_effect` and
`draw_empty_meta`. It took **44 of the 63 sites** that existed then; with
`victim_collide`'s nine added and two decline sites gone quiet it still reads 44,
now of **72**, and the union across all three movies is **48**.

**`decl.` now says where the ROM went.** A guard that declines records the
address it declined *to*, and both modes print the distinct ones with counts. On
the two-player movie that turned "618 dispatches to a handler the port does not
have" into two addresses, one of them 616 of the 618 — which is a morning's work
rather than a hole of unknown shape. `level1-rescue.zmv`'s reads `$83:A364 ×3`
and `player id table $80:F92D ×1`, which is exactly what the previous round had
to establish by hand.

**And the 616 is ported.** `$81:FE0E` is twenty-one bytes: a weapon shot's
handler. Four collision ids stop a shot — write 0 through `$0A` into the display
record's `ACTOR_COLLIDE_ID` so it cannot hit anything else, and 1 to `$42` so its
own `DEC $42 : BNE` loop ends it on the next pass — and every other id it flies
straight through. It passes **616 of 616**, takes `thread_call_handler` from 618
declines to **2** and `actor_collide_notify` from 617 to **1**, and it is the
first ported handler that reaches out of its own direct page into the game's
shared data structure. `verify` caught a real error on the first run: the expire
path's `LDY $0A` overwrites the entry `TAY`, so Y comes back as the record
address, not the id — `Y: ROM $19C6, port $0003` on 597 of 616 calls, with the
19 that passed being exactly the other exit.

`$81:889F` onwards masks the id, rules out two ids with routines of their own,
indexes the damage table at `$81:8561` and subtracts from health at `$1E`. Three
ways out; the movie takes the first:

* **died** — store the negative result, clear `$7E`, and `JSR $81:8727`, which
  awards points and posts `$F5F5` to `$12`, then `SEC : RTL`. Ported.
* **zero damage** — the difference equals the health it came from, and not even
  the store happens. Ported, never taken by any input.
* **survived** — `JML $81:8506`, which splices a call into the thread's own
  parked stack so the scheduler runs a reaction when it next resumes it. Not
  ported: declines, by name.

Two findings came with it, and both are the kind the harness exists to produce.

**`$81:8888` is where a thread gets parked**, and it is the only thing in the
game that is. `thread_call_handler`'s `handler_park` had read zero on every
movie ever run for exactly that reason. Diffing it immediately found a bug: the
park path ends `LDA #$8000 : STA $1180,X`, so the caller gets the *parked value*
back in A, not the handler's — the port was returning `$F5F5` and `verify` said
so at call 1,405.

**`$81:8727` is where the score lives**, so `src/port/score.c` exists and
`$80:C7D9` is registered as a routine in its own right rather than folded into
the handler. Its other caller is the victim-rescue thread at `$83:A1EC`, which
has nothing to do with collisions, so both call sites check it — and under `run`
it is the one routine substituted on a path the collision chain never reaches.
It is the first ported code that is **decimal** (the score is BCD, `SED` is on
for the whole addition) and the first whose *input* includes a flag: the `BMI` at
its entry reads the caller's N, which is bit 15 of the collision id — so **bit 15
says which player's weapon it was and bits 0-14 say what the weapon was.** Two
constants fell out of the diff: a victim rescued is worth `$1000`, an enemy
killed `$0100`.

Five deliberate perturbations of `shot_collide`, and the same split as ever.
Caught: writing 2 to `$42` instead of 1 (its own page, immediately), dropping the
`ACTOR_COLLIDE_ID` clear (`$7E:19D4`: ROM `$00`, port `$5C` — the display record,
still carrying the id), and claiming carry clear on the expire path, which failed
at `$7E:1198` rather than on a register, because the flag is parked on the
caller's stack and the *write* is what shows. **Not** caught: also stopping on id
`$0002` (nothing in the movie ever hits an id 2, so a fifth `CMP` is unreachable
code that agrees with the ROM by never running) and taking N from the id rather
than from `id - 1` on the pass path (every id that reaches it is small and
positive, so both are 0). Those two are the mirror image of the `STZ $7E` finding
below: the port would have been *more permissive* than the ROM, and no diff can
see it because the distinguishing input does not occur. `shot_expire_zero` is a
coverage site for the third row's concern — id 0 is the one exit that runs no
`CMP` at all, so carry leaves as the caller's — and it is untaken, which is the
report saying one of three exits is transcribed rather than diffed.

Six deliberate perturbations of the death path before it, and the two that were
**not** caught are worth more
than the four that were. Breaking the BCD decimal adjust changes nothing, because
neither award on this movie pushes a digit past 9 — there is now a coverage site,
`score_digit_carry`, that says so by name. Deleting the `STZ $7E` on the death
path changes nothing either, because the word is already zero; that one cannot be
made visible by a coverage mark at all, so it is written down in
`src/port/collide.c` instead. A store the diff cannot distinguish from a no-op is
a real limit on what "byte-identical" means, and the only defence is to notice.

Everything before this round still holds. The dispatcher and both handlers are
ported (`$80:8480`, `$80:F7F7` with its `$80:F950` hit path, `$81:8888`); they
are the first ported code whose direct page is not `$0000`, which is why
`CosimRegs` carries a `d` and a `db`; and 1,226 collisions are only **17 hits the
player actually took**, the rest landing inside the recovery window.

The map correction from the previous round is what made this possible. **A
thread does not run on direct page `$0000` — it runs on its own 128-byte page**,
installed from a 24-entry table at `$80:82DE`, and `$7E:0100-$7E:0CFF` is 24 of
them. That supersedes both `docs/frame-skeleton.md`'s "direct page pinned at
`$0000` for the whole game" and `docs/wram-map.md`'s "array of ten objects at
stride `$100`", and it answers where actor state lives: **an actor's state is its
thread's direct page.** Fifteen of those pages' fields are named now
(`docs/wram-map.md`), in **three tables rather than one** — because the pages do
not agree with each other. `$1E` is health to an enemy and a latched event code
to a victim; a shot keeps its display record at `$0A` and a victim keeps its at
`$08`. There is no struct here, only what each actor's own code does with its own
128 bytes.

The branch-coverage instrument is what made all of this legible. **The port marks
its decision points and the harness counts them** (`src/port/coverage.h`,
`zamn_cosim verify -c`), on the same principle as the shims' flag masks — an
unclaimed output is an unchecked output, and **an untaken branch is an unverified
branch.** There are **72** marked sites now; `movies/level1.zmv` takes 23,
`movies/level1-rescue.zmv` **40** and `movies/level1-2p.zmv` **44**, and their
**union is 48**. Twenty-four are untaken by every input that exists — a measured
backlog rather than a suspicion. The three movies are complementary rather than
ordered: `level1-rescue.zmv` is still the only one that reaches `cull_offscreen`,
`player_unported` and `victim_claim_a`.

**`sprite_build_oam` (`$80:BD1F`) is the one the previous round was about.** It is the
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
  with dispatches into actor handlers (`$80:BE8F` → `$80:8480`), so the port
  declares a **guard**: it inspects each call first and hands back the ones it
  cannot serve, which the ROM then runs itself, in both modes, counted and
  printed. `level1.zmv` declines 0 of 1,016 — no two visible actors in it ever
  come within 16 pixels — and `level1-rescue.zmv`, which used to decline 1,226
  and then 1, now declines **none**. See `docs/cosim.md` → *Half a routine,
  honestly*, *Through the door* and *The last decline*.

Eight of the eighteen are leaves that never yield and never touch actor state:
`sprite_frame_tile` (the 128-slot VRAM frame cache Phase 2 explicitly deferred),
`sprite_cache_age`, `thread_tick_waits`, both vblank-queue adders, and the three
routines `sprite_build_oam` opens with — `actor_depth_sort`, `actor_cull` and
`oam_buffer_clear`. Those three plus `actor_overlap_pass` and
`actor_collide_notify` are the port code that walks the game's own data
structure, the 32-record sprite display list at `$7E:185E`, rather than a table
the scheduler owns.

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
- `movies/boot.zmv`, `movies/level1.zmv`, `movies/level1-rescue.zmv`,
  `movies/level1-2p.zmv` — reproducible input scripts. `level1.zmv` reaches
  actual gameplay; `level1-rescue.zmv` goes on to rescue a victim and get mobbed
  in the graveyard, which is what makes actors touch; `level1-2p.zmv` is the
  first that uses **controller 2** and the first in which anything is actually
  **fired** (`Y`, not `B` — see the status section). A frame may carry a `2:`
  prefix to aim the line at port 2, and the two ports are independent streams, so
  every earlier movie means exactly what it always did. `zamn_headless -m <movie>
  --at f,f,...` replays one and dumps a PNG per named frame, which is how a movie
  gets aimed.
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
  at `$7E:185E` they all walk, plus `$80:BE8F`, **the collision dispatch a hit
  ends with**), `collide.c` (**the door into actor behaviour** — `$80:8480`, the
  callback dispatcher that installs a thread's own direct page, plus **all four
  handlers a collision on the current movies reaches**: `$80:F7F7` with its
  `$80:F950` hit path, `$81:8888` with its death path at `$81:889F`/`$81:8727`,
  `$81:FE0E`, **a weapon shot's handler** and the first ported code that writes
  the display list from inside a handler, and `$83:A364`, **a victim's** — eight
  ids, eight endings, latched on the first one to arrive; it also names fifteen
  direct-page fields those routines touch, across three different pages that
  disagree about every offset), `score.c` (**the score** — `$80:C7D9`
  and the slot search at `$80:C7C2`; BCD, and the port's only decimal
  arithmetic), `coroutine.h` (**how a ported routine suspends** — one `resume`
  index plus a context struct, ~40 lines), `fade.c` (`$80:891A`, the first
  routine ported that uses it) and `coverage.h`/`.c` (**which of the port's
  branches any input has actually taken** — 72 marked decision points across all
  eighteen routines; with `PORT_COVERAGE` undefined every mark compiles to
  nothing at all, which is how the shipped game builds).
- `src/cosim/` — the harness (tooling, not port code; it goes away in Phase 4).
  `cosim.c` is the engine — snapshot, intercept, diff, substitute, lockstep —
  and `routines.c` is the registry plus one *shim* per routine that translates
  the 65816 calling convention. The shim/port split is deliberate: without it,
  "port code" drifts into 65816 written in C. A routine may also declare a
  **guard**, which is how a port that covers only part of a routine says so: the
  engine asks it before every call and hands the ones it declines back to the
  ROM, in both modes, counted in the report's `decl.` column — and, when the
  guard knows the address, **named**: a *census* collects the distinct addresses
  the ROM went to instead, so the `decl.` count comes with a work list.
- `src/cosim.c` → **`zamn_cosim.exe`** — `verify` (ROM drives, port is checked
  per call — per *segment*, for a routine that suspends), `run` (port drives,
  two cores diffed per scheduler pass), `list`. Both modes end with a
  **branch-coverage report**: which of the port's marked decision points this
  movie was in a position to check at all, and by name the ones it was not.
  `-c` prints the full table with hit counts. Then the **decline census**: the
  distinct addresses declined calls went to, most-declined first.
- `docs/cosim.md` — the design, what each mode proves, what the diff forgives
  and why, the carry-flag bug that only one of the two modes could catch, what
  branch coverage measures that the diff cannot, what the census adds to that,
  and what a second controller reached that nothing else could.
- `docs/threads.md` — **the coroutine decision**: why resume points and not
  fibers, how a suspending routine is checked segment by segment, how native
  mode suspends by jumping to the routine's own `JSL thread_yield`, and what is
  still open.

## How to build & run
```
powershell -ExecutionPolicy Bypass -File tools\build.ps1     # add -Clean to reset
build\zamn.exe "Zombies Ate My Neighbors.sfc"                # play
build\zamn_headless.exe "Zombies Ate My Neighbors.sfc" out.png 500
build\zamn_headless.exe "Zombies Ate My Neighbors.sfc" shot.png 6100 -m movies\level1-rescue.zmv --at 1980,3000,4200
build\zamn_trace.exe "Zombies Ate My Neighbors.sfc" -o analysis -f 2400 -m movies\level1.zmv
build\zamn_disasm.exe "Zombies Ate My Neighbors.sfc" analysis\zamn.cdl -b 80 -s tools\symbols\zamn.sym -o analysis\bank_80.asm
mkdir analysis2                                             # a second CDL, from the longer movie:
build\zamn_trace.exe "Zombies Ate My Neighbors.sfc" -o analysis2 -f 6100 -m movies\level1-rescue.zmv
build\zamn_disasm.exe "Zombies Ate My Neighbors.sfc" analysis2\zamn.cdl --from 80F7F7 --to 80F980 -s tools\symbols\zamn.sym
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
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1-rescue.zmv -f 6100 -c
build\zamn_cosim.exe run "Zombies Ate My Neighbors.sfc" -m movies\level1-rescue.zmv -f 6100
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1-2p.zmv -f 4100 -c
build\zamn_cosim.exe run "Zombies Ate My Neighbors.sfc" -m movies\level1-2p.zmv -f 6000
build\zamn_assets.exe verify-sprites "Zombies Ate My Neighbors.sfc" -m movies\level1-2p.zmv -f 4100
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
  "PASSWORD"; `$90:9172` draws Zeke mid-run. **That gap is closed.**
  `movies/level1-2p.zmv` — two players, both actually firing — intercepts
  **24,183** emissions and diffs every one of them byte-identical: 17,310
  unflipped, 5,555 flip-x, **966 flip-y and 352 flip-xy**, across **127** distinct
  metasprites in `$8F:DF86..$90:DEF4`. `$80:BB30`/`$80:BBA6` were ported from
  their ROM bytes in Phase 2 and had never executed once in any movie; they are
  now diffed against execution, which was the longest-standing gap in the
  project.
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

- Phase 3 (verify): `zamn_cosim verify` replays a movie and checks each of
  the eighteen ported routines against the ROM's own on **every call the game
  makes** — the whole 128 KB of WRAM plus A/X/Y and the flags each shim claims.
  On `level1-2p.zmv` — two players, both firing — **69,793 of 69,793 pass, and
  nothing at all declines** (46,967 `sprite_frame_tile`, 2,716 each for the five
  per-frame routines and `sprite_build_oam`, 2,509 `thread_call_handler` —
  **0 declined**, where it was 618 before `shot_collide` and 2 before
  `victim_collide` — 1,254 `actor_collide_notify`,
  1,242 `player_collide`, **616 `shot_collide`**, 429 `enemy_collide`, 342
  `vbl_queue_a_add`, 11 `score_add`, **2 `victim_collide`**, and `fade_in`'s 16
  segments).
  On `level1-rescue.zmv`, **106,357 of 106,357 pass** (71,694
  `sprite_frame_tile`, 4,716 each for `thread_tick_waits`, `actor_depth_sort`,
  `actor_cull`, `oam_buffer_clear`, `actor_overlap_pass` and `sprite_build_oam`
  — the last two with **1 declined**, down from 1,226 — 2,516
  `thread_call_handler` with 1 declined, 1,226 each for `actor_collide_notify`,
  `player_collide` and `enemy_collide` — the first two with 1 declined apiece and
  `enemy_collide` with **none** — 107 `vbl_queue_b_add`, 48
  `vbl_queue_a_add`, **3 `victim_collide`**, 2 each for `sprite_cache_age` and
  `score_add`, and `fade_in`'s 16 segments. All five declines are the
  same single event — `$80:F92D`, the sound effect — seen once at each level of
  the chain that encloses it).
  On `level1.zmv`, **16,601 of 16,601 pass** (10,354 `sprite_frame_tile`, 1,016
  each for `thread_tick_waits`, `actor_depth_sort`, `actor_cull`,
  `oam_buffer_clear`, `actor_overlap_pass` and `sprite_build_oam`, 107
  `vbl_queue_b_add`, 24 `vbl_queue_a_add`, 2 `sprite_cache_age`, 1 each for
  `thread_call_handler` and `victim_collide`, and `fade_in`'s
  16 segments; that movie produces no actor-to-actor overlap at all, so
  `actor_collide_notify`, the other three handlers and `score_add` are never
  reached — the one dispatch it does make comes from outside the collision
  path). The
  only WRAM waived is
  derived, not declared: per call — per *segment*, for `fade_in` — it is the
  window between the deepest the stack pointer went and where it started,
  **2 bytes** for the leaf routines that push, **0** for the six that do
  not, **3** for `fade_in` (exactly the return address its own `JSL
  thread_yield` pushes), and 4 through **24** for the collision chain, which
  nests six deep from `sprite_build_oam` down through a handler to
  `score_add` — plus one declared
  2-byte scratch slot (`$7E:0038`, which
  `sprite_frame_tile`, `actor_depth_sort` and `sprite_build_oam` all declare,
  for the caller's X and the sort's walk-predecessor respectively).
  Non-vacuous five times:
  starting the LRU eviction scan one slot late failed on the very first call at
  `sprite_lru_slot`; returning the wrong register in a shim failed on Y after
  4 calls; a wrong VRAM destination failed at `sprite_upload_dest`; narrowing the
  camera cull window from 384 px to 128 failed on `visible_actor_count` at 695 of
  1,016 calls; and inverting the depth sort's Y key failed after 129 calls at a
  display record. A sixth perturbation was instructive rather than caught:
  forcing the `ACTOR_SORT_FIRST` branch of the sort still passed all 1,016 calls,
  because `level1.zmv` never presents two records that disagree on that bit — and
  it still does not, which is now the coverage report saying so by name rather
  than a perturbation nobody would rerun.
  (Correction to an earlier entry here: *widening* the cull window by one pixel
  was recorded as failing after 446 calls. It does not — it passes all 1,016,
  and it has to, because the coverage report shows `level1.zmv` never puts a
  record outside that window at all. Only a narrowing can be caught on this
  movie. `level1-rescue.zmv` culls 248, so the widening direction is now
  reachable too.)
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
  and each is a branch `level1.zmv` never takes — the `ACTOR_ATTR_SET` palette
  override (disabling the whole branch still passed), `ACTOR_PRIORITY_TOP`
  (only the low-priority side is exercised), and the overlap test's box size
  (see below). All three are now reported by name rather than found by hand, and
  `level1-rescue.zmv` closes two: `draw_priority_top` fires 182 times and
  `overlap_hit` 2,452. `ACTOR_ATTR_SET` is still untaken by any movie.
  On that movie `sprite_build_oam` passes **3,490 of 3,490** served calls.
- Phase 3 (**half a routine, honestly**): `actor_overlap_pass` (`$80:BEC9`) is
  the first port that covers only part of what the ROM's version does. Its walk
  is ported; what it does on a hit is not, because `$80:BE8F` hands the pair to
  `$80:8480`, which `RTL`s into an actor's own handler. So the routine declares
  a **guard**: the harness asks it before every call, on a throwaway copy of
  WRAM, and a `false` means the ROM's own instructions run instead — in both
  modes, counted in the report's `decl.` column. The rule that keeps this
  honest is that a decline is an enumerated condition the port detects, never a
  fallback for a failed diff. Non-vacuous
  three ways: writing the wrong walk cursor failed after 129 calls; **dropping
  the geometry test entirely made 673 of 1,016 calls decline** — which proves
  the walk really does reach the box test, and that the decline path works end
  to end — and widening the box from 16 px to 32 produced no declines at all.
  **The gap this had is closed.** `level1.zmv` declines **0 of 1,016**, which
  said the port served every call *that movie made* and nothing more.
  `level1-rescue.zmv` rescues a victim and then fights in the graveyard, and
  declines **1,226 of 4,716** — so the box test now fires on real game input
  rather than only under a 128-px perturbation, both modes decline the same
  calls, and `run` still reaches the end of the movie with no byte of live game
  state differing. The one number in the harness that was a property of the
  movie rather than of the game is measured.
- Phase 3 (**splitting that hole in two**): `actor_collide_notify` (`$80:BE8F`)
  is ported and registered on its own entry PC, which separates the dispatch
  *plumbing* — six direct-page words read out of both records, the pair written
  to `$76`/`$78`, two calls to `$80:8480` — from the actor logic `$80:8480`
  enters. Registering it separately is the point: the enclosing pass declines
  every call containing a collision, so without its own entry the plumbing would
  only ever be offered the passes where nothing happened. Offered all of them, it
  declined **1,226 of 1,226** and `collide_none` was never reached — **every
  collision in ordinary play enters a handler**, which is a property of the game
  and not of the movie. A one-off census sized what was left: **two handlers**,
  `$80:F7F7` (player) and `$81:8888` (enemy), 1,225 of the 1,226 pairs, split at
  collision id `$5C` so exactly one side of each collision acts — the enemy's
  acting branch runs **once** in 1,226, and the player's lands on the same
  jump-table entry `$80:F950` **1,225** times. `collide_none` is still at zero,
  which is now the one thing about this routine that is transcribed rather than
  diffed, and the report says so by name.
- Phase 3 (**through the door**): the dispatcher and both handlers are ported —
  `thread_call_handler` (`$80:8480`), `player_collide` (`$80:F7F7`, including
  the `$80:F950` hit path its every dispatch lands on) and `enemy_collide`
  (`$81:8888`). On `level1-rescue.zmv` **1,225 of 1,226** collisions are served
  end to end, so `actor_overlap_pass` and `sprite_build_oam` go from **1,226 of
  4,716 declined to 1 of 4,716**, and `run` substitutes all but three of 6,089
  scheduler passes whole. The one decline is the single call where an enemy
  actually takes damage and leaves through unported code; `$80:F92D`, a sound
  effect, is the other declining entry and it belongs with the audio path
  because it writes no WRAM but does talk to the APU.
  This is the first ported code whose **direct page is not `$0000`** — a
  handler runs on its thread's own page, so `CosimRegs` grew a `d` and a `db`.
  A real finding fell out of the hit path: 1,225 collisions are only **17 hits
  the player actually took**, the rest landing inside the recovery window at
  `$52`. Non-vacuous five times, each caught at the exact byte or flag: running
  a handler on `D = $0000` failed at `$7E:0058` on call 6 and in that routine
  only; publishing `$76`/`$78` the same way round for both dispatches failed at
  `$7E:0158` after 30 calls; resetting the recovery timer to `$41` instead of
  `$40` failed at `$7E:0152` on the first hit in all five routines at once;
  serving `enemy_collide`'s acting branch instead of declining failed at
  `$7E:0812` after 685 calls; and claiming `enemy_collide` returns N clear
  failed on flag N at call 1 **and nowhere else** — the dispatcher's `PLB`
  overwrites it, so no enclosing routine could ever have caught it. That last
  one is the argument for registering a handler in its own right.
- Phase 3 (**the last decline**): the enemy's acting branch is ported through
  the outcome the movie reaches, so `enemy_collide` declines **0 of 1,226** and
  `actor_overlap_pass`, `sprite_build_oam` and `actor_collide_notify` decline
  nothing that a collision causes. `$81:889F` masks the id, rules out the two
  ids with routines of their own, indexes `$81:8561` and subtracts from `$1E`;
  a negative result stores it, clears `$7E`, runs `$81:8727` and returns
  **carry set**. What remains declined is one `$80:F92D` (a sound effect) and
  the survivor's path at `$81:8506`, which splices a call into a suspended
  thread's own stack.
  Two routines came out of it. `$81:8727` awards points and posts `$F5F5` to the
  enemy's `$12`, which its own thread body (`$81:8842  LDA $12 : BEQ <loop>`)
  reads once a pass to know it has been killed. `$80:C7D9` — **`score_add`**,
  registered in its own right because `$83:A1EC` calls it too — is the port's
  first **decimal** arithmetic and the first routine whose *input* includes a
  flag: its `BMI` reads the caller's N, which is bit 15 of the collision id, so
  **bit 15 names the player whose weapon it was** and bits 0-14 name the weapon.
  The diff handed over both awards: a victim is `$1000`, a kill `$0100`.
  Porting the death path also made `handler_park` fire for the first time —
  `$81:88BE  SEC : RTL` is the only thing in the game that parks a thread — and
  that immediately caught a bug: `$80:84A8  LDA #$8000 : STA $1180,X` means the
  caller gets the *parked value* back in A, not the handler's.
  Non-vacuous four times, each at the exact byte or flag: awarding `$0200`
  instead of `$0100` failed at `$7E:1E73` after 685 calls (and `score_add`
  itself still passed, because at its own entry the award comes from the ROM's
  X); posting `$F5F4` failed at `$7E:0812`; returning carry clear from the death
  failed on flag C *and* at `$7E:11A6` one level up, independently; and
  searching the two score slots in the wrong order failed on `score_add`'s very
  first call — the victim rescue, before any collision. **Two perturbations were
  not caught, and both are findings:** breaking the BCD decimal adjust changes
  nothing because neither award pushes a digit past 9 (now named by the
  `score_digit_carry` coverage site), and deleting the `STZ $7E` changes nothing
  because the word is already zero — a store the diff cannot tell from a no-op,
  which no coverage mark can express, so it is written down in
  `src/port/collide.c` instead.
- Phase 3 (**the direct page is not pinned**): getting to `$80:8480` corrected
  two documents. `$80:82A4` installs a per-thread direct page from a 24-entry
  table at `$80:82DE` when a thread is spawned; the resume path restores it with
  `PLD`; `$80:8480` swaps to the *target* thread's page to call its handler. So
  `docs/frame-skeleton.md`'s "direct page pinned at `$0000` for the whole game"
  holds only for the scheduler, NMI and the per-frame housekeeping — which is
  every routine ported so far, which is why it survived this long. The 24 pages
  tile `$7E:0100-$7E:0CFF` at stride `$80`, every live region the trace found in
  that range falls inside one of them, and they are what `docs/wram-map.md`
  recorded as "ten ~36-byte structures at stride `$100`". **An actor's state is
  its thread's direct page**, which is item 3 below answered before it was
  started. `$7E:1300`/`$7E:1330`, *unidentified* until now, are the handler
  callbacks `$80:8475` registers. Reading listings needs the same caveat:
  `zamn_disasm` resolves direct-page operands as though `D` were `$0000`.
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
  eighteen substituted, at most **18 bytes of 131,072** ever differ and every
  one of them, on all 2,389 passes where anything differed, is inside the stacks
  (`$7E:1000-$7E:12FF`) or the declared scratch slot — **no byte of live game
  state ever differs**, and the run reaches gameplay. The same holds on the
  longer movie: **6,089 compared passes**, same 18-byte ceiling, same verdict,
  with **3** of them declined by the collision guard (667 before the handlers
  were ported). And on the two-player movie: **5,989 compared passes**, same
  verdict, and now `sprite_build_oam` substituted on **all** of them — the 11
  that used to decline to `$83:A364` are served. The ceiling there rose from 27
  bytes to **32**, which is what going native on eleven more passes costs in
  stack residue and is exactly the kind of difference the waiver exists for. Two
  things in that report
  need reading correctly. First: with `sprite_build_oam` substituted, the ROM never
  reaches its callees, so they report **not reached** or single-digit call counts
  under `run`. They are
  running — the port calls the port's versions — they are just no longer
  intercepted. `verify` still exercises every one of them on every call. This is
  what porting upwards looks like and it will keep happening. Second, and newly
  visible now that a rare event is ported: **`run` and `verify` do not play a
  movie on the same timeline.** `verify` counts PPU frames and `run` counts
  scheduler passes, so `-f 6100` means different things to the two of them, and
  `run` reaches a different part of the movie — it sees ~670 collisions where
  `verify` sees 1,226, and even at 9,000 passes it never reaches the enemy death
  at all. That is not the substitution: lockstep's reference side is a stock
  core and the two agree byte for byte throughout. It does bound what `run`
  proves, and it is why `verify` is the correctness instrument. The cycle budget
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
   songs/sample sets no upload has driven yet. **Two of the three are still
   open, and the first is closed.** `movies/level1-rescue.zmv` was the first
   movie written against a measured coverage report rather than a guess, and
   `movies/level1-2p.zmv` is the first with a **second controller** and the first
   in which anything is actually **fired** — it diffs all four OAM emitters
   against 24,183 real emissions, vertical flips included. Levels beyond the
   first and the undriven songs are what a level-transition movie would get.

## Next steps — Phase 3 (continued)

The harness works, the coroutine question is answered (`docs/threads.md`), the
sprite pass is native, the collision path is served end to end for every outcome
any input has produced, eighteen routines are through it — seventeen leaves and
one that suspends — and how much of them any given input actually exercises is
now measured rather than guessed at, per movie *and* by address for what it
declines. What is left is scale rather than shape.

1. **The rest of what a collision does.** Every collision on the current movies
   is served whole, and on `movies/level1-2p.zmv` **nothing declines at all**.
   What remains is a short, named list rather than a subsystem —
   which is the whole point of having gone through the door with a guard rather
   than around it — and only the first of these is still a WRAM problem. In rough
   order of size:
   * **`$81:8506`** — the enemy's *survivor* reaction, and the most interesting
     unported routine in the game right now: it walks the thread's parked stack
     (`$7E:11B0,X` is the saved SP), moves the top three words down by three
     bytes and writes a `JSL` frame into the gap, so that when the scheduler
     next resumes that thread it runs a reaction routine first and then carries
     on where it left off. **The game injects calls into suspended threads.**
     That has a direct bearing on `docs/threads.md`: the port's coroutines park
     as plain copyable data with no machine stack, so there is nothing for this
     to splice into, and porting it means deciding what the equivalent is. No
     movie reaches it yet (`enemy_survived` is untaken), so there is time.
   * **`$80:F92D`** — the player's other reachable table entry, which is one
     `JSL apu_play_sfx`, and **the only address left in the census on any
     movie** (1 decline on `level1-rescue.zmv`, none anywhere else). It writes no
     WRAM, so it is not a WRAM-diff problem at
     all: it belongs with the audio path, and porting it means deciding how the
     port drives the APU rather than how it computes. That decision is now the
     single thing standing between the collision path and zero declines
     everywhere.
   * **`$81:83C6` and `$81:847E`** — the two collision ids (`$5D`, `$5E`) with
     routines of their own. Nothing has ever reached them, so what they are is
     still an open question.
   * **The untaken branches** the coverage report names inside what is already
     ported — `collide_none`, `hurt_state_immune`, `hurt_weapon_immune`,
     `enemy_no_damage`, `shot_expire_zero`, three of the five `score_add`
     sites, and six of `victim_collide`'s nine. Each is transcribed from the
     listing and waiting for an input, not
     for code. (`player_ignore`, `player_no_effect` and `score_slot_1` were on
     this list until `movies/level1-2p.zmv`.)
2. **Port a routine that yields from inside a call it makes.** `fade_in` yields
   at its own top level, which is the easy half. The nested case needs the callee
   resumable too, with its own `PortCoro` in the caller's context, and it is the
   one part of the decision that is designed but not yet exercised. The harness
   will not paper over it: a yield whose return address is outside the ported
   routine's body is deliberately left unmatched rather than misattributed.
   `$80:8516` (the gameplay thread, yields at two sites and calls six unported
   routines) is the realistic target once more of its callees exist.
3. **Then the actor state itself**, and the animation state that picks an
   actor's metasprite frame to frame. Deferred from Phase 2 as runtime state
   rather than ROM format; the harness is what will check it. Two tables listed
   here in earlier rounds are now identified and neither is what it looked like:
   the `$7E:1872` stride-`$14` one is the display list at `$7E:185E`, and the
   "`$7E:0300` stride `$100`" one is the **24 per-thread direct pages** tiling
   `$7E:0100-$7E:0CFF` at stride `$80` (`docs/wram-map.md`). So there is no
   separate actor slot table to find — the camera-driven spawner `$81:80EC`
   allocates a *thread*, and its page is the actor's state.
4. **Extend `movies/`** (Phase 2 checklist item 7). Still the binding constraint
   on most of what is ported — but no longer an open-ended one, because
   `zamn_cosim verify -c` now says what a movie is worth. The work is: read the
   untaken list, write an input that does that thing, watch the list shrink.
   `movies/level1-rescue.zmv` did that for five sites and takes **40 of 72**;
   `movies/level1-2p.zmv` did it for six more and takes **44**, against
   `level1.zmv`'s 23. The three are complementary rather than ordered — only
   `level1-rescue.zmv` reaches `cull_offscreen`, `player_unported` and
   `victim_claim_a` — so the
   number that matters is their **union: 48 of 72**. **Twenty-four are untaken by
   every input that exists**, in rough order of how gettable they look:
   * `victim_claim_b` — the *second* player walking into a victim. The first
     player's side of it runs five times on `level1-rescue.zmv`; this is the
     same input with the other controller, which makes it the most gettable
     untaken site in the project. It is also the only thing that would prove the
     `$8000` half of "bit 15 names the player" on a victim rather than on a kill.
   * `victim_latched`, `victim_ignore`, `victim_event_2`, `victim_event_4`,
     `victim_keep_id` — the rest of `victim_collide`'s chain. `victim_latched`
     is the one that matters: the latch guard is the whole design of the routine
     and **deleting it passes every diff**, because no movie ever dispatches to
     the same victim twice.
   * `hurt_state_immune`, `hurt_weapon_immune` — two branches inside the
     player's hit path. They want a specific player state and the one weapon
     `$80:F950` singles out.
   * `enemy_no_damage`, `enemy_survived`, `enemy_hit_special` — the enemy's other
     three outcomes. Every hit so far kills in one, so `enemy_survived` wants a
     tougher enemy — which is also the input that would force `$81:8506` to be
     ported.
   * `shot_expire_zero` — a shot that stops on collision id 0 rather than on one
     of the other three. It is the only one of `shot_collide`'s three exits that
     runs no `CMP`, so it is the only one whose carry is the caller's, and until
     something produces it that exit is transcribed rather than diffed.
   * `score_slot_1`'s neighbours: `score_discard` (points earned by a side no
     slot owns — two players was not enough), and `score_carry` /
     `score_digit_carry`, which want a score large enough to carry out of four
     BCD digits and one whose digits need the decimal adjust. A longer session
     gets the last two for free; without them the BCD arithmetic is transcribed
     rather than diffed (proven: breaking the adjust changes nothing).
   * `sort_key_first` (`ACTOR_SORT_FIRST`) and `draw_attr_set`
     (`ACTOR_ATTR_SET`) — two of the four originally hand-found gaps, both
     needing a record type level 1 does not spawn. They are now the two
     longest-standing untaken sites in the project.
   * `queue_full`, `emit_oam_full`, `draw_oam_full` — all three want a scene
     busy enough to saturate a queue or all 128 sprites. A boss, probably.
   * `collide_none` — a collision between two actors with no handler registered.
     Zero across every movie, so this one may well be unreachable by design
     rather than by the movie. If it stays at zero across a movie that reaches
     the whole game, that is a fact about the ROM, and the eight words
     `actor_collide_notify` writes will only ever be checked through a handler.
   * `draw_no_meta`, `draw_bad_bank` — defensive branches in `sprite_build_oam`
     that a well-formed record may never reach at all, with the same caveat.
     (`draw_empty_meta` was here until the two-player movie hit it 1,272 times,
     which is a useful reminder that "defensive" is a guess until it is not.)
   * `handler_unported`, `collide_unported` — the two **decline** sites, and the
     only ones on this list that would be a step backwards to take. They went
     untaken when `victim_collide` landed, because there is no longer a handler
     address for the dispatcher to hand back. A decline site is a site like any
     other, so porting a routine can shrink the coverage number; that is the
     report being honest rather than a regression.
   Separately, `fade_in`'s entire sample is still one call — sixteen segments is
   all there is — and `fade_out` (`$80:8933`) is never reached by any movie. The
   next targets after that are the ones item 7 named, minus the one now done:
   the password screen, a level transition, a boss.

## Known limitations / TODO (deferred, non-blocking)
- **`run` proves nothing inside `$7E:1000-$7E:12FF`** (the stacks). A
  substituted routine does not push what the ROM's version pushed, and that
  residue reshuffles as later calls push and pop at different depths. Everything
  outside that range — all 127 KB of game state — is compared byte for byte, and
  `verify` covers the routines' own stack effects exactly. Rationale in
  `docs/cosim.md` → *What the diff forgives*.
- **A substituted call returns on a measured cycle budget, not the real cost.**
  Nothing has diverged because of it yet, but the budget is a per-routine mean
  and the ROM's own cost varies with its input (`sprite_frame_tile`: 288..1590).
  It is also a slight over-count, because the measured figure includes the
  `RTS`/`RTL` (or, for a suspension, the `JSL thread_yield`) that native mode
  then makes the core execute for real — about 24 master cycles inside a
  ~57,000-cycle frame. This stops mattering in Phase 4, when the reference is
  cut loose.
- **Everything the harness proves is only as good as the movie.** `verify` and
  `run` decline exactly the same calls, and both print the count, so a decline is
  never silent — but "0 declined" on one movie says the port served every call
  *that movie made*, not that it can serve every call. That is now measured
  rather than merely admitted: `zamn_cosim verify -c` reports which of the port's
  72 marked branches an input reached, and twenty-four are untaken by every movie
  that exists. A decline now also says **where the ROM went instead**, by
  address, so what is missing is a work list rather than a count — and that list
  is down to one entry, `$80:F92D`, which is an audio routine rather than a
  computation. See
  `docs/cosim.md` → *Half a routine, honestly*, *Through the door*, *The last
  decline*, *Declined to what: the census*, *And the last address on the list:
  the victim* and *Coverage the movie does not have*.
- **A branch can be marked; a store cannot.** Coverage says whether a line ran.
  It cannot say whether running it *changed* anything — and the death path's
  `STZ $7E` writes a word that is already zero, so deleting it passes every diff.
  Found by perturbation, recorded in `src/port/collide.c`, and there is no
  instrument for the general case: it is a floor on what "byte-identical" means.
- **Branch coverage counts port executions, not game events.** Under `verify` the
  port runs once per interception, so a routine reached both directly and through
  a ported caller has its sites counted once for each. That chain is six deep on
  the collision path, so a site inside `$80:F950` reading 6,125 means 1,225
  collisions, and `overlap_hit` reads 2,452 for 1,226 passes, exactly twice.
  Whether a site was reached at all, which is the only thing the report claims,
  is unaffected.
- **A marked branch is one somebody thought to mark.** The 63 sites are chosen by
  hand, one per decision the diff would have to run to check, not generated. A
  branch with no mark on it is invisible to the report, so this is a floor on
  coverage rather than a measurement of it.
- **A branch mark cannot catch a port that is too *permissive*.** Coverage says
  a line ran; nothing says a line should never have been reachable. Adding a
  fifth id to `shot_collide`'s four passes every diff, because no input produces
  it — the port would accept something the ROM rejects and the movie cannot tell.
  Same floor as the `STZ $7E` above, approached from the other side. The sharpest
  example so far is `victim_collide`: **deleting its latch guard entirely passes
  every call on all three movies**, because none of them ever dispatches to the
  same victim twice — and the latch is the whole design of the routine. A
  coverage site (`victim_latched`) names it; nothing can check it but an input.
- **Two activations of the same ported routine at once are not distinguished.**
  A suspension is attributed to the innermost in-flight call whose body contains
  the yield's return address; if two threads were ever inside the same ported
  routine simultaneously that would be ambiguous. No routine ported so far can
  be. See `docs/threads.md` → *What is not settled yet*.
- 11 passes go uncompared by `run` on each of the three movies: one before the
  scheduler exists, and ten where a side never returned to the `WAI` within the
  step.
- **The census is capped at 64 distinct addresses.** One is in use, and only on
  one of the three movies. If it ever
  overflows, the report says so on its own line rather than truncating quietly —
  and that would itself be the finding, because the point of the list is that it
  is short enough to work through.
- Frame pacing fixed 2026-07-22: paced by sync-to-audio, with a monotonic-timer
  fallback when no audio device (`src/main_sdl.c`).
- No gamepad mapping yet (keyboard only). No save states / config yet.
- `analysis/` and `build/` are scratch (git-ignored). `analysis/` in particular
  contains verbatim ROM bytes and must never be committed.
- Windows-only by design for now (per project scope).
