# Co-simulation

How a ZAMN routine gets replaced by C without anyone having to take it on
trust. This is the Phase 3 machinery: `src/cosim/` (the harness),
`src/port/` (the native game logic it drives) and `zamn_cosim` (the driver).

Phase 2 built this pattern five times over without naming it. Each
`zamn_assets verify-*` command plays a movie under the reference core,
intercepts one ROM routine, re-runs the C port on the arguments the ROM was
called with, and diffs the result. `zamn_cosim` is that generalised: a registry
of ported routines and one engine that can drive them three ways.

## The two questions

Replacing a routine raises two different questions, and conflating them is how
a port convinces itself it is correct when it is not.

**Does the port compute what the ROM computes?** — `zamn_cosim verify`.

The ROM runs the game exactly as it always does; nothing is substituted. Every
time the CPU enters a ported routine, the harness snapshots all 128 KB of WRAM
and the registers, lets the ROM's own instructions run to the return, snapshots
again, then rewinds its copy and runs the C port over the same input. It diffs
all of WRAM plus A, X, Y and whichever of N/Z/C the routine's shim claims to
model.

Because the ROM is driving throughout, the game's execution and timing are
untouched. Any difference is the port's, which is what makes the signal clean.
This is the instrument that means the most.

**Does the game still work with the port in it?** — `zamn_cosim run`.

Two cores on the same movie, one stock and one where the ROM's instructions are
genuinely skipped in favour of the C, with all of WRAM compared once per
scheduler pass. Unlike `verify`, this one is sensitive to things other than a
wrong answer — a substituted routine returns on a cycle budget rather than by
executing the original code — and it exercises the *callers*, which `verify`
never does.

Both matter, and they catch different bugs. See [What went wrong](#what-went-wrong-and-what-caught-it).

## Running it

```
zamn_cosim list
zamn_cosim verify "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
zamn_cosim run    "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
zamn_cosim run    "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -r none
zamn_cosim verify "Zombies Ate My Neighbors.sfc" -m movies\level1-rescue.zmv -f 6100 -c
```

`-r <name>` narrows to one routine and can be repeated; the default is all of
them. `-r none` substitutes nothing, which is the control: two stock cores must
stay byte-identical for the whole run or nothing else measured here means
anything. `-c` prints the full branch-coverage table (see
[Coverage the movie does not have](#coverage-the-movie-does-not-have)). Both
commands exit non-zero on a divergence.

## What a ported routine has to declare

`CosimRoutine` in `src/cosim/cosim.h`. Beyond the entry address and the C
function, four fields carry the weight:

| Field | What it is | Where the value comes from |
| --- | --- | --- |
| `ret_kind` | `RTS` or `RTL` | whether callers reach it by `JSR` or `JSL` |
| `ret_op` | an address holding that opcode | native mode jumps here so the *core* performs the return |
| `cycles` | what a substituted call burns | the mean `verify` measured |
| `stack_bytes` | how much stack the ROM's version pushes and abandons | the `stack` column `verify` measured |

A routine that *suspends* sets `run_yield` instead of `run`, plus three more
fields — `end`, `yield_op` and `ctx_size`. `docs/threads.md` covers what those
mean and why the coroutine problem turned out to be tractable; the short version
is that a suspending routine is checked once per **segment** (the run between two
suspensions) rather than once per call, and that native mode suspends by jumping
to the routine's own `JSL thread_yield` for the same reason it returns by jumping
to the routine's own `RTS`.

The last two are measured, never guessed. `verify` prints the real distribution
of both, so the loop is: port the routine, run `verify`, copy the numbers it
reports into the registry, then run `run`.

### Half a routine, honestly

A routine may also declare a **guard**, `supported`. That is how a port that
covers most of a routine but not all of it stays truthful.

`actor_overlap_pass` (`$80:BEC9`) is the case that forced it. Its whole body is
a pairwise 16x16 overlap test over the visible actors — mechanical, worth
porting, and the sort of loop the diff is good at checking. But when two records
*do* touch, it calls `$80:BE8F`, which hands both to `$80:8480`, which builds a
call frame out of an actor's own thread slot and `RTL`s into its handler. That
is the entry point to actor behaviour: not a routine, a subsystem, and none of
it is ported. `sprite_build_oam` inherits the same limit, because the overlap
pass is the last thing it does.

The dishonest options were both available and both bad. Excluding the addresses
the handler would have touched would have waived most of WRAM. Porting the walk
and quietly not dispatching would have been correct on every call in
`movies/level1.zmv` — where no pair ever touches — and silently wrong the first
time one did.

So the port answers a third question first: *can I serve this call at all?* The
engine asks the guard at the entry PC, before anything runs. A `false` and the
harness steps aside completely — the ROM's own instructions execute, in **both**
modes, and the call is counted in the `decl.` column rather than in `checked`.
Nothing is claimed about a call the port did not make.

The guard runs the port itself, on a throwaway copy of WRAM. "Can the port
handle this?" and "what does the port do with this?" are the same question here,
and asking it any other way would mean writing the pairwise test a second time
in the harness, where it could drift from the one in `src/port/`.

Two rules keep this from becoming a way to make failures disappear:

* **A decline is an enumerated condition the port detects, never a fallback.**
  There is no path from "the diff failed" to "declined".
* **It is visible.** `zamn_cosim list` prints `covers: some` for a guarded
  routine, and both `verify` and `run` print the count.

`level1.zmv` declines **0 of 1,016** calls, which is the movie saying no two
visible actors ever come within 16 pixels of each other. That was a gap as much
as a result, and it is closed: `movies/level1-rescue.zmv` rescues a victim and
then fights zombies in the graveyard, and declined **1,226 of 4,716** the first
time it ran. Both modes decline the same calls, and `run` still reaches the end
of the movie with no byte of live game state differing.

The two sections below are what happened to that 1,226. It is **1** now, and the
route from one number to the other is the whole method: split the hole, measure
each half, port the half that turns out to be small.

#### Splitting the hole in two

That 1,226 was one number covering two different things — the dispatch *plumbing*
and the actor logic it dispatches into — and until they were separated there was
no way to tell which one was the wall. `$80:BE8F` is now ported and registered in
its own right (`actor_collide_notify`), which splits them:

* **The plumbing** reads both records' collision id and thread slot into six
  direct-page words, writes the pair into `$76`/`$78`, and calls `$80:8480`
  twice — once per actor, each told the other's id. All of that is transcribable.
* **`$80:8480`** is where it stops. Given a slot with a handler registered it
  installs that thread's direct page and `RTL`s into arbitrary game logic. Given
  a slot with *no* handler it is three instructions that write nothing, and that
  case the port serves exactly.

Registering `$80:BE8F` separately matters even though `actor_overlap_pass`
already calls it: the pass declines every call containing a collision, so
without its own entry the plumbing would only ever be exercised on the passes
where nothing happened. Intercepted at its own entry PC, it is offered **every**
collision the movie produces.

And the answer was a flat one. With the plumbing ported and the handlers not,
the port declined **1,226 of 1,226** — the `collide_none` coverage site was never
reached, so *every* collision in ordinary play enters a handler. That is not the
result the split was hoping for, but it is the one worth having, because it
replaces a guess about where the wall is with a measurement. (`collide_none` is
still at zero, and still ported: see the end of the next section.) Sizing it
further took a one-off census of which
handlers those calls reach, and the shape is much narrower than "actor
behaviour" suggested:

| Pair | Calls |
| --- | --- |
| `$80:F7F7` (player) + `$81:8888` (enemy) | 1,225 |
| `$80:F7F7` + `$83:A364` | 1 |

Two handlers, and they split at collision id `$5C`: `$81:8888` acts on ids `≥$5C`
and ignores everything else, `$80:F7F7` acts on ids `<$5C` through a jump table
at `$80:F808` and ignores the rest. Complementary, so exactly one side of each
collision does real work — which is why serving only the ignore paths would not
buy a single call. In this movie the enemy's side takes its `≥$5C` branch exactly
**once** in 1,226, and the player's side lands on the same jump table entry,
`$80:F950`, **1,225** times. So the remaining work is not a subsystem, it is four
small routines and the per-thread direct pages they run on
(`docs/wram-map.md`).

#### Through the door

Those four routines are ported, and the wall has moved from 1,226 to **1**.

| Routine | Entry | What the port covers |
| --- | --- | --- |
| `thread_call_handler` | `$80:8480` | the whole dispatcher, for a handler it has |
| `player_collide` | `$80:F7F7` | the ignore path, and the two ported jump-table entries |
| `enemy_collide` | `$81:8888` | the ignore path — 1,225 of the 1,226 |

`$80:8480` is the interesting one, because it is where the direct page stops
being `$0000`. It reads the handler's far address out of
`$7E:1300`/`$7E:1330`, installs the *target thread's* page from the 24-entry
table at `$80:82DE`, and `RTL`s in — so the handler's `LDA $70` is offset `$70`
into that thread's own 128 bytes. The port therefore takes `D` as an argument,
which is why `CosimRegs` grew a `d` and a `db`: direct page and data bank are as
much a part of a 65816 routine's calling convention as A/X/Y, and this is the
first pair of routines where either matters. (`db` matters because `$80:8480`'s
exit flags come from the `PLB` that restores it.)

Where the dispatcher stops is now a **list of addresses**, not a subsystem. Two
entries long; anything else declines by name, and the coverage report counts it.
That is what makes the remaining work countable: on `level1-rescue.zmv`,
`handler_unported` fires 6 times, all from the two call sites outside the
collision path.

The two handlers are ported as far as the branch that does nothing, which on this
movie is nearly all of it:

* **`player_collide`** files the other record on its own page and jumps through
  the table. Every one of the 1,225 dispatches lands on `$80:F950`, which is
  ported in full — three ways of deciding the hit does not count (a state that
  ignores collisions, one weapon held with `$1E` set, and the recovery timer)
  and then the two stores that say it did. The recovery timer turns 1,225
  collisions into **17 hits the player actually took** — `hurt_taken` reads 85,
  and five registered routines now sit on the path, so every site along it is
  counted five times over (see *Coverage the movie does not have*). The
  `$80:F92D` entry —
  a sound effect — declines, because it writes no WRAM at all but does talk to
  the APU and spin on its acknowledgement; that belongs with the audio path.
* **`enemy_collide`** returns having read and written nothing at all for an id
  below `$5C`. Its acting branch subtracts a damage-table entry from `$1E` and
  leaves through unported code, and on this movie it runs **once**.

That once is the entire remaining decline. `actor_overlap_pass` goes from
**1,226 of 4,716** declined to **1 of 4,716**, and `sprite_build_oam` with it.
Under `run`, all but three of 6,089 scheduler passes are now substituted whole,
and no byte of live game state differs on any of them.

Five deliberate perturbations, each caught at the exact byte or flag:

| Change | Caught |
| --- | --- |
| run the handler on `D = $0000` instead of the thread's page | `$7E:0058` at call 6 of `thread_call_handler` — and nowhere else, because only that routine reads the table |
| publish `$76`/`$78` the same way round for both dispatches | `$7E:0158` at call 30 — the player filed the wrong record |
| reset the recovery timer to `$41` instead of `$40` | `$7E:0152` at the first hit, in all five routines at once |
| claim `enemy_collide` returns N clear | flag N at call 1 — **and only there**, because the dispatcher's `PLB` overwrites it, so no enclosing routine could ever have caught it |
| serve `enemy_collide`'s acting branch instead of declining | `$7E:0812` at call 685 — the decline is load-bearing |

The fourth is the one that justifies the registry's shape. A handler's flags do
not survive the routine that calls it, so registering it separately is the only
way they are ever checked at all.

What is left untaken, and named as such by the coverage report rather than
implied: `collide_none` (still zero — no input has ever produced a collision
between two actors with no handler), `handler_park` (neither ported handler
returns carry set on a served path), `player_ignore` and `player_no_effect` (the
player has never been told about a `≥$5C` id, and every id it *has* seen wanted
`$80:F950` rather than the table's bare `RTS`), and two of `$80:F950`'s own
exits, `hurt_state_immune` and `hurt_weapon_immune`. Those are transcribed from
the listing and have never been diffed against it, exactly like `emit_flip_y`,
and the fix for each is an input.

Native mode returns by pointing the program counter at the routine's own
`RTS`/`RTL` and letting the core execute it, rather than unwinding the stack by
hand. There is no reason to reimplement the core's stack and bank handling when
the routine already contains the instruction that does it.

### Registers are outputs too

A 65816 routine's contract is its registers plus WRAM. Each routine has a *shim*
in `src/cosim/routines.c` that says what the registers meant on the way in and
what the ROM leaves in them on the way out, citing the instruction that decides
each one.

Flags are opt-in: a shim declares which of N/Z/C it modelled, and `verify`
compares exactly those. That keeps the claim as strong as the evidence and no
stronger — and, as it turned out, makes an unmodelled flag a visible gap rather
than an invisible one. A, X and Y work the same way, through a second mask.

Declining one is allowed, but it is a claim in itself, so a shim that declines
has to name the instruction that makes the output dead. `actor_depth_sort` is
the case that forced the rule: its `RTS` leaves in A, and in carry, an
intermediate of whichever comparison happened to end the pass — a different one
on each of its three exit paths. Reproducing that would mean writing the
comparison a second time inside the shim, which is the drift the shim/port split
exists to stop. So both are declined and the shim shows its work: the only
caller is `$80:BD27`, and the next thing it runs is `JSR $80:BCE2`, which opens
`LDY #$0000 : LDX $1B5E : BEQ` and reaches its own first use of carry through a
`SEC`. `run` is what audits that reasoning, because it is the mode where an
unclaimed output really does keep the caller's value.

A suspension is an exit and is checked exactly as hard as a return — WRAM plus
A/X/Y plus the claimed flags, with A being the sleep count. That is not
symmetry for its own sake: `thread_yield` opens with `PHP`, so the flags at the
`JSL` are parked with the thread and given back by `PLP` on resume. Adding that
check found a real carry bug within the hour. See `docs/threads.md`.

### The shim/port split

`src/port/` is port code: ordinary C with ordinary signatures
(`sprite_frame_tile(Wram*, uint16_t frame)`), depending on nothing but libc, and
knowing nothing about registers, stacks or emulators. Everything about *how the
ROM called it* lives in the shim, in the harness, and dies with the harness in
Phase 4.

Without that line, "port code" would slowly become 65816 written in C.

### One WRAM, byte for byte

`src/port/wram.h` is the other half of the bargain. The port keeps the SNES's
WRAM layout exactly — ported routines read and write the same 128 KB at the same
offsets the 65816 code does. That is what lets the harness point a C function at
the emulator's memory, or at a snapshot of it, and diff the result. A port with
its own state layout could only ever be checked by eye. It also makes PLAN.md's
Phase 5 save states a one-line `fwrite`.

## What the diff forgives, and why

Three things, in descending order of rigour. All three are reported, none are
silent.

**Abandoned stack, per call.** A routine that pushes leaves the pushed bytes
behind — `PHY` writes them, `PLA` reads them back, nothing erases them — and the
C port has no stack in WRAM at all. So `verify` ignores differences between the
deepest the stack pointer got during the call and where it started. The window
is *derived*, not declared: a routine that pushes nothing gets no leeway
whatsoever, and the report prints the widest window it used (2 bytes for the
four routines that push, 0 for the four that do not, 3 for `fade_in`).

**Declared scratch.** `sprite_frame_tile` opens with `STX $38` and closes with
`LDX $38` — with one index register, spilling the caller's X is the only way to
use X for the lookup. The port keeps it in a C local, so `$7E:0038` is declared
as a place the port does not write. `actor_depth_sort` declares the same two
bytes for the same reason: with one index register, the record in front of the
one being examined has nowhere to live but scratch. `sprite_build_oam` declares
the same two bytes a third time, because it calls both of those routines and
inherits both spills. `zamn_cosim list` prints every such declaration. One
address, three routines, one reason — and it is the only honest use of the
mechanism. Anything else appearing here would be a porting bug in disguise.

Two things keep `sprite_build_oam`'s version from being a free pass. It
reproduces the emitter's spill anyway, because that one is derivable — the last
frame lookup of a pass leaves the OAM index of the last piece drawn, four bytes
back from where the buffer ends — and with the exclude switched off, writing it
moves the first divergence from call 130 to call 192. What is left is the depth
sort's spill on passes that draw nothing. And `actor_overlap_pass` writes `$38`
too, after everything else in the pass, so whenever it has a record to test the
byte is checked exactly — by that routine, registered separately and compared on
all 1,016 of its own calls.

**The stack area, in `run` only.** This is a *region* rule and deliberately the
weakest thing in the harness. Stack residue compounds: a substituted call leaves
two bytes stale, and every later push and pop at a different depth reshuffles
which other dead bytes the two machines disagree about. Modelling that exactly
would mean tracking all 24 thread stacks' live extents to prove something no
code can observe, since nothing reads below its own stack pointer.

The cost is worth stating plainly: **inside `$7E:1000-$7E:12FF`, `run` proves
nothing.** Everything outside it — all 127 KB of actual game state — is compared
byte for byte, and `verify` covers the routines' own behaviour exactly, stack
included.

## Comparing at the right instant

`run` compares once per **scheduler pass**, not once per PPU frame.
`docs/frame-skeleton.md` called this in advance:

> **`WAI` is the frame boundary.** The co-simulation harness should compare WRAM
> at the `WAI` in `scheduler_idle`, not at an arbitrary instruction count.

It matters twice over.

Comparing at the PPU's vblank catches the two cores part-way through whatever
bulk WRAM work happens to straddle it and reports a transient as a divergence.
The first attempt did exactly that and blamed the port for a half-finished
table fill.

And once a substitution shifts timing, the two cores stop reaching that `WAI` at
the same instant, so a rule of "compare if both happen to be there" quietly stops
comparing. Fixing the instant but keeping the PPU frame as the step compared 320
of 2400 boundaries — 13% coverage that still read as a pass. Stepping each core
by one scheduler pass instead, and letting each replay the movie against its own
frame counter, compares 2389 of 2400.

Note that a pass and a PPU frame are not interchangeable units of *progress*: the
scheduler does not run during long loads, so 2400 passes reach considerably
further into the game than 2400 frames. `verify` counts frames and `run` counts
passes, which is why their call totals differ.

## What went wrong, and what caught it

Worth recording, because it is the argument for having both modes.

`vbl_queue_a_add` and `vbl_queue_b_add` return their verdict **in the carry
flag** — clear if the job was queued, set if the queue was full. The first
version of their shims modelled N and Z but not carry. `verify` passed all 131
calls, correctly: it compares only the flags a shim claims.

Under `run`, `$82:AE3A` does `JSL vbl_queue_b_add : WAI : BCS <back>` — it spins
until the job is accepted. With carry left at whatever the caller happened to
hold, the loop never exited: the routine was entered **147,405 times instead of
107** and the game hung. Modelling carry fixed it, and `verify` then still
passed with carry compared — a strictly stronger check that also confirmed
`sprite_frame_tile` returns carry clear on all 10,354 of its calls.

The lesson is small and sharp: **an unclaimed flag is not a small omission, it is
an unchecked output.** `verify` alone would have shipped it.

## Where this is going

The first five routines here are leaves — they never call `thread_yield`. That
was deliberate, to get the harness working against routines whose contract is
simple. The sixth, `fade_in`, is the first that suspends, and the question
`docs/frame-skeleton.md` posed in Phase 1 —

> ZAMN's routines suspend mid-body via `thread_yield` and resume with their
> stack intact, so a naive C function cannot stand in for one.

— is now answered: **explicit resume points, with the suspended state as plain
copyable data.** The argument, the mechanism and what it proved are in
`docs/threads.md`. The deciding reason is this harness itself: `verify` works by
rewinding WRAM and replaying the port over it, and a fiber's parked machine stack
cannot be rewound. Picking a representation the harness cannot inspect would mean
porting the hardest part of the game with the checking turned off.

What is left is scale rather than shape — nested yields, more than one activation
of a routine at a time, and enough movies to exercise any of it properly.

The three routines added after `fade_in` are the first here that are not
infrastructure. `actor_depth_sort`, `actor_cull` and `oam_buffer_clear` are the
three calls `sprite_build_oam` opens with, and they are leaves again — which is
the point. What is new about them is that they are the first port code to walk
the game's own data structure, the 32-record display list at `$7E:185E`, rather
than a table the scheduler owns.

`sprite_build_oam` (`$80:BD1F`) itself closes that loop. It is the first ported
routine that *calls other ported routines* — the three above, plus
`sprite_frame_tile` through the emitters and `actor_overlap_pass` at the end —
and the first place Phase 2's work is load-bearing inside Phase 3: the OAM
composition it runs is `sprite_emit()`, unchanged, the same function
`verify-sprites` proved byte-exact against 4,591 real emissions before any of
this existed. What was missing was the caller. Which records to draw, in what
order, at what screen position, with which attributes — that is what this adds,
and it is checked on all 128 KB of WRAM on all 1,016 calls the movie makes.

One consequence is visible in `run` and worth not misreading: with the caller
substituted, the ROM never reaches its callees, so `sprite_frame_tile`,
`actor_depth_sort`, `actor_cull`, `oam_buffer_clear` and `actor_overlap_pass`
all report **not reached** there. They are running — the port calls the port's
versions directly — they are simply no longer *intercepted*. `verify` still
exercises every one of them on every call, because there the ROM is driving.
This is what porting upwards looks like, and it will keep happening — the
collision handlers made the chain five deep, and under `run` the whole of it
below `sprite_build_oam` now reports single-digit call counts for the same
reason.

The three routines after that are the first here that are not the port's own
plumbing at all. `thread_call_handler`, `player_collide` and `enemy_collide`
(*Through the door*) are **game behaviour** — what happens to the player when a
zombie touches them — and they are the first routines whose direct page is not
`$0000`. That is the boundary Phase 3's remaining work is on the other side of.

## Coverage the movie does not have

Five perturbations of `sprite_build_oam` were caught (the `ACTOR_Z` subtraction,
the base priority bits, the flip selection, the screen-space origin, and using
the emitted-piece count where the walked-piece count belongs — each failing
within 130 to 421 calls, at the exact byte). Three deliberate ones were **not**,
and each turned out to be a branch `movies/level1.zmv` never takes. Finding them
that way — change the port on purpose, run `verify`, notice it still passes —
works, but it is manual, destructive, and nobody would remember to redo all of
it each time a movie is added, which is exactly when the answer changes.

So the port now marks its decision points and the harness counts them.
`src/port/coverage.h` is the instrument and the argument for it; the rule is the
one the flag masks already follow, one step further out: an unclaimed output is
an unchecked output, and **an untaken branch is an unverified branch.**

```
zamn_cosim verify <rom> -m movies\level1-rescue.zmv -f 6100 -c
```

Both modes print it. The untaken sites are always listed; `-c` adds the full
table with hit counts. It is never a failure — an untaken branch is a movie that
has not been written yet, and saying so is the whole job.

Three things about the numbers. **A site is a decision the diff would have to
run to check**, not every `if`: 51 of them across the fifteen routines, chosen by
hand. **Hit counts are call-weighted, not event-weighted** — under `verify` the
port runs once per interception, so a routine reached both directly and through
a ported caller is counted once for each. That weighting is now five deep on the
collision path (`sprite_build_oam` → `actor_overlap_pass` →
`actor_collide_notify` → `thread_call_handler` → `player_collide`), so a site
inside `$80:F950` reading 6,125 means 1,225 collisions. Whether a site was
reached at all, the only thing the report claims, is unaffected. And **a guard's
dry run counts only
when it declines**: `actor_overlap_pass`'s guard answers by running the port on
a throwaway copy, and a declined call is never run again, so that pass is the
only record there will be; a call it allows is run for real a moment later and
the dry run's marks are rolled back.

The instrument was checked the way everything else here is. Narrowing
`actor_cull`'s camera window from 384 px to 128 moves `cull_offscreen` from 0 to
3,072 and fails the diff at 695 of 1,016 calls, so a zero in that column is a
finding rather than a dead counter.

### What it found

On `movies/level1.zmv` — the movie most numbers above are measured on — **21 of
51 sites** are taken, which was 20 of 36 before the collision handlers added
fifteen more that only the other movie reaches. It reproduced all three
`sprite_build_oam` gaps found by
hand, plus `actor_depth_sort`'s `ACTOR_SORT_FIRST` and Phase 2's known
vertical-flip gap, without anybody perturbing anything. It also named ten more
nobody had listed, of which the sharpest is this: **`sprite_frame_tile` is
called 10,354 times by that movie and never once evicts a resident frame.** The
most-called ported routine in the game, and the branch that makes it a *cache*
rather than a lookup table had never run.

`movies/level1-rescue.zmv` was written against that report and takes **36 of
51**, over 106,347 checked calls with nothing diverged. It closes five of the
original ten and reaches ten of the fifteen the handlers added, including both of
the two that mattered most:

* `overlap_hit` — 2,452 marks and **1,226 of 4,716 calls declined**. Until this
  movie, "0 declined" was the only number the guard had ever produced. See
  *Half a routine, honestly*: that section's coverage caveat is no longer
  hypothetical, and the decline path is now exercised end to end by a game doing
  something ordinary rather than by a perturbation.
* `cache_evict` and `cache_scan` — 296 and 8. The eviction path above.
* `draw_priority_top` — 182. One of the three hand-found gaps, closed.
* `cull_offscreen` — 248, and only after the long directional runs at the end of
  the movie: a camera that scrolls hard enough to leave an actor behind is the
  only thing that makes `actor_cull` reject anything.

Fifteen are still untaken and they are a measured backlog rather than a
suspicion. Ten are the original ones minus the five closed: `emit_flip_y` (no
shipped actor in level 1 flips vertically), `sort_key_first`, `draw_attr_set`,
`queue_full`, the two OAM-full sites, three defensive branches in
`sprite_build_oam` that a well-formed record may simply never reach, and
`collide_none` — a fact about the *game* rather than about the movie: 1,226
collisions and not one of them between two actors that were not listening. The
other five arrived with the handlers and are listed at the end of *Through the
door*. Each one is a claim this document does not get to make yet.
