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
```

`-r <name>` narrows to one routine and can be repeated; the default is all of
them. `-r none` substitutes nothing, which is the control: two stock cores must
stay byte-identical for the whole run or nothing else measured here means
anything. Both commands exit non-zero on a divergence.

## What a ported routine has to declare

`CosimRoutine` in `src/cosim/cosim.h`. Beyond the entry address and the C
function, four fields carry the weight:

| Field | What it is | Where the value comes from |
| --- | --- | --- |
| `ret_kind` | `RTS` or `RTL` | whether callers reach it by `JSR` or `JSL` |
| `ret_op` | an address holding that opcode | native mode jumps here so the *core* performs the return |
| `cycles` | what a substituted call burns | the mean `verify` measured |
| `stack_bytes` | how much stack the ROM's version pushes and abandons | the `stack` column `verify` measured |

The last two are measured, never guessed. `verify` prints the real distribution
of both, so the loop is: port the routine, run `verify`, copy the numbers it
reports into the registry, then run `run`.

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
than an invisible one.

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
three routines that push, 0 for the two that do not).

**Declared scratch.** `sprite_frame_tile` opens with `STX $38` and closes with
`LDX $38` — with one index register, spilling the caller's X is the only way to
use X for the lookup. The port keeps it in a C local, so `$7E:0038` is declared
as a place the port does not write. `zamn_cosim list` prints every such
declaration. It is the only one, and it is the only honest use of the mechanism:
anything else appearing here would be a porting bug in disguise.

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

The five routines here are leaves — they never call `thread_yield`. That was
deliberate, to get the harness working against routines whose contract is
simple. The hard part is still ahead and `docs/frame-skeleton.md` states it:

> ZAMN's routines suspend mid-body via `thread_yield` and resume with their
> stack intact, so a naive C function cannot stand in for one.

Porting a *thread* needs either an explicit resume-point state machine or a real
coroutine, and that decision shapes every thread ported after it. The harness
does not answer it — but the harness is what will prove the answer right.
