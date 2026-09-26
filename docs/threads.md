# How a ported routine suspends

The decision `docs/frame-skeleton.md` flagged at the end of Phase 1, deferred
through all of Phase 2, and named in PROGRESS.md as the thing that "cannot be
deferred again". This is the answer, the argument for it, and what has been
built and proved on top of it.

## The problem

ZAMN is not a flat state machine. It runs a 24-slot cooperative scheduler
(`thread_yield` at `$80:8353`, the most-called routine in the game), and its
game logic is written as coroutines:

```
fade_in:                          ; $80:891A
  LDA #$0000 : STA $136C          ; brightness_shadow = 0
loop:
  LDA #$0001 : JSL thread_yield   ; sleep one tick — and one frame passes here
  INC $136C
  LDA $136C : CMP #$000F : BNE loop
  RTL
```

That `JSL` does not return for a frame. The scheduler parks the thread's stack
pointer in `thread_sp`, picks somebody else, and only later restores the stack
and lets `thread_yield` return. Between the `JSL` and the `INC`, the whole rest
of the game ran.

A plain C function cannot stand in for that. So: how does the port's version of
this routine stop in the middle?

## The answer

**An explicit resume point, and the suspended state is plain data.** No fibers,
no saved machine stacks, no `ucontext`, no per-ABI assembly. `src/port/fade.c`
is the whole of the routine above:

```c
PortStep fade_in(Wram* w, FadeCtx* c, uint16_t* ticks) {
  switch (c->co.resume) {
    case PORT_CORO_ENTRY:
      wram_w16(w, W_BRIGHTNESS_SHADOW, 0);
      return port_yield(&c->co, 1, ticks, FADE_IN_RESUME);
    case FADE_IN_RESUME:
      wram_w16(w, W_BRIGHTNESS_SHADOW, wram_r16(w, W_BRIGHTNESS_SHADOW) + 1);
      if (wram_r16(w, W_BRIGHTNESS_SHADOW) != 0x000f)
        return port_yield(&c->co, 1, ticks, FADE_IN_RESUME);
      break;
  }
  return PORT_RETURNED;
}
```

The mechanism is `src/port/coroutine.h`, and it is about forty lines. A routine
that suspends is a function over a context struct whose first member is a
`PortCoro` — one `uint16_t` saying where to re-enter. Anything that has to
survive a suspension goes in that struct instead of being a C local; that is the
promotion the 65816 version gets for free by leaving it on its parked stack, and
doing it by hand is the entire cost of this approach.

## Why not fibers

Fibers are the obvious alternative and they are genuinely nicer to write: the C
would read exactly like the original, straight down the page, with real calls to
`thread_yield` in the middle and no context struct at all. Three things rule
them out, in increasing order of weight.

**`src/port/` depends on nothing but libc.** That rule has held since the first
line of Phase 2 and it is what makes the port portable. Fibers are `CreateFiber`
on Windows, `ucontext` on POSIX (deprecated on macOS), or hand-written assembly
per ABI. None of those is libc.

**Phase 5's save states stop being a `fwrite`.** PLAN.md's payoff feature list
includes save states, and `src/port/wram.h` was designed so that "the game state"
is one 128 KB array plus, now, a handful of small context structs. Where each
thread is parked stays a number. With fibers it is a native call stack — not
portable across builds, not portable across compilers, and not something you can
write to a file and read back next week.

**And the decisive one: `verify` could not check them.** This is the argument
that actually settles it, and it comes from the harness that already exists.

`zamn_cosim verify` works by *rewinding*. It snapshots WRAM as a routine is
entered, lets the ROM's own instructions run, and then rewinds its copy and runs
the C port over the same input to compare the two. That is the instrument this
whole phase rests on — 11,519 calls checked, and every bug found so far found by
it.

You cannot rewind a native call stack. A suspended fiber's state is a stack
pointer into memory the C runtime owns, with return addresses, spilled
registers, and red zones in it; there is no defined way to copy it, replay it, or
run it twice. A suspended coroutine whose state is `{ resume, locals }` in a
struct is a `memcpy`. **Choosing fibers would mean choosing a representation the
harness cannot inspect** — which is to say, porting the hardest part of the game
with the checking turned off, exactly where it is needed most.

The cost of the decision is real and worth stating: a routine that yields inside
a routine it *calls* needs the callee to be resumable too, with its own
`PortCoro`, and the caller's resume point has to re-enter it. That nesting is
manual, and for a deeply nested yield it will be tedious. It is still the right
trade, because the alternative is untestable.

## How the harness checks one

A suspending routine is not one comparison but a chain of them. Its execution
decomposes into **segments**: entry to the first `thread_yield`, then each
resumption to the next suspension, then the last resumption to the `RTL`. Every
segment is straight-line code that terminates — which is exactly the shape the
existing engine already handled.

So `verify` does per segment what it used to do per call:

| At | What happens |
| --- | --- |
| entry | snapshot WRAM and registers; zero the port's context |
| a `JSL thread_yield` in the routine's body | rewind, run one segment of the port, diff WRAM + A/X/Y + the claimed flags. A *is* the sleep count. |
| the resumption | **re-**snapshot WRAM and registers |
| the `RTL` | rewind, run the last segment, diff WRAM and the registers |

The re-snapshot at each resumption is the part that matters. Between a
suspension and its resumption arbitrary other threads ran and moved WRAM
underneath the routine, so the state it picks up with is emphatically not the
state it left. Diffing the next segment against the entry snapshot would compare
the port against a world that no longer exists.

Two smaller things fall out of the same observation:

**The dead-stack window is per segment.** `verify` waives differences between
the deepest the stack pointer went during a call and where it started. While a
routine is parked the scheduler switches to other threads' stacks entirely, so
the stack pointer goes hundreds of bytes below anything the routine touched.
Carrying that low-water mark across a suspension would waive most of a kilobyte
of WRAM for free. It is reset at every resumption, and suspended calls are
skipped while the mark is being tracked at all. `fade_in` reports a waived window
of **3 bytes** — precisely the return address its own `JSL thread_yield` pushes.

**An interrupt while parked is not a problem, it is the point.** The engine
abandons a call when an NMI lands inside it, because the handler moves WRAM the
port does not model. A suspended routine is parked across an NMI by definition,
so the walk stops at the first suspended call and leaves it alone. A resumable
routine caught *mid-segment* is not abandoned either — dropping it would strand
its context a segment behind the ROM and make every later comparison
meaningless — so the segment is marked spoiled, the port is still run to keep the
two in step, and the result is counted as interrupted rather than diffed.

## How the harness substitutes one

`run` skips the ROM's instructions and runs the C for real. For a leaf routine it
publishes the result registers and jumps the program counter to the routine's own
`RTS`/`RTL`, so the *core* performs the return. Suspending works the same way,
in mirror image: **when the port yields, native mode puts the sleep count in A
and jumps to the routine's own `JSL thread_yield`.**

The port therefore never models parking a stack pointer, choosing the next
thread, or coming back. The core executes the real `JSL`, the real scheduler
parks the real frame, and the real scheduler resumes it — at which point the
harness recognises the resume address and runs the next segment. Because it is
literally the ROM's own instruction, the stack footprint of a substituted
suspension is not an approximation of the ROM's, it *is* the ROM's, which is why
`fade_in` declares `stack_bytes = 0` and leaves nothing stale behind.

This is the same reasoning as `ret_op`, and it is the reason the coroutine
problem turned out to be tractable at all: the hard part of suspending is the
65816 part, and the 65816 part is already written.

## What this proved, and the bug it caught

`fade_in` is the smallest routine in the game that suspends. It calls nothing but
`thread_yield`, so its entire observable effect is one word of WRAM and there is
no unported subroutine inside a segment to muddy the diff.

* **`verify`** — 1 activation, 15 suspensions, **16 of 16 segments** identical:
  all 128 KB of WRAM, plus A, X, Y and N/Z/C, at every suspension *and* at the
  return. Waived: 3 bytes of dead stack per segment, derived, nothing declared.
* **`run`** — the ROM's instructions never execute, and all sixteen segments are
  substituted. Over 2,389 compared scheduler passes no byte of live game state
  ever differs, and the run reaches gameplay with all six ported routines
  substituted at once.

Non-vacuous four times. Yielding for 2 ticks instead of 1 failed at segment 0 on
the sleep count; ending the loop at 14 instead of 15 failed at segment 14 with
"the ROM suspended, the port returned", after 14 segments had passed; returning
`$000E` in A failed on A; and:

**Carry, again.** The first version of the engine did not compare registers at a
suspension at all — the reasoning being that `thread_yield` clobbers A, X and Y,
so nothing at the `JSL` is an output anybody reads. That reasoning is wrong, and
wrong in exactly the way the queue-adder bug in `docs/cosim.md` was wrong.
`thread_yield` opens with `PHP`. The flags at the `JSL` are parked *with the
thread* and handed back by `PLP` when it resumes, and under substitution nothing
else would ever set them.

Comparing them found a real error the same hour it was added. The shim claimed
carry passed through the suspension untouched, which is what the last instruction
before the `JSL` — `LDA #$0001` — implies. But the loop reaches that `LDA` by
falling through `CMP #$000F`, which borrows for every brightness below 15 and
leaves carry *clear*. Only the very first suspension, entered from the top of the
routine, never executes that `CMP`. One segment in sixteen behaves differently
from the other fifteen, and the simple answer is right about that one and wrong
about the rest.

The lesson from `docs/cosim.md` generalises, so it is worth restating in its
stronger form: **an unclaimed output is an unchecked output, and a suspension is
an exit like any other.**

## What is not settled yet

* **One activation is a thin sample.** `movies/level1.zmv` calls `fade_in` once.
  Sixteen segments is enough to prove the mechanism and it caught a real bug, but
  it is not enough to claim the routine is exercised. This is the same gap
  PROGRESS.md's Phase 2 checklist item 7 names, and the same fix: more movies.
* **Nested yields are unimplemented, not just unwritten.** No routine ported so
  far yields from inside a call. The harness deliberately refuses to match a
  yield whose return address is outside the routine's own body rather than
  quietly attributing it, so the first nested case will show up as an unmatched
  yield rather than as a silent wrong answer.
* **The game splices calls into suspended threads' stacks — and the port now
  does it too, without needing a stack of its own.** `$81:8506`, the reaction an
  enemy runs when it survives a hit, reads the thread's parked stack pointer
  from `$7E:11B0,X`, moves the top three words down three bytes, and writes
  `$81:8541` into the gap. The next time the scheduler resumes that thread the
  `RTL` it resumes through returns into `$81:8542` first, which sets
  `ACTOR_ATTR_SET` on the enemy's display record, sleeps two ticks, clears it,
  and only then returns into whatever the enemy was actually doing. The flash
  you see when you shoot something that does not die is a call injected into
  code that is not running.

  **It is ported** (`src/port/collide.c`), and the thing that made it tractable
  is a distinction this page did not draw when it wrote the paragraph above: the
  splice is not a *coroutine* operation, it is twelve bytes of WRAM. The thread
  being spliced into belongs to the ROM — an enemy body nobody has ported — and
  what `$81:8506` does to it is arithmetic on `W_THREAD_SP` and six stores.
  `verify` compares all of it, and `movies/level53.zmv` splices twenty frames
  byte-identically. Seven of eight deliberate perturbations were caught, two of
  them on things only this routine could get wrong: moving the three overlapping
  words highest-first instead of lowest-first, and swapping the two overlapping
  stores that lay down the 24-bit address.

  **What is still open is the other direction**: what happens when the routine
  being spliced *into* is a ported one. Nothing in the game does that yet — every
  thread `$81:8506` has ever touched is an enemy body running from ROM — and the
  answer when it comes is still the one this page guessed: a one-entry
  pending-call slot in `PortCoro`, "run this, then resume where you were",
  designed against the first real caller rather than in advance. The difference
  is that the mechanism is now understood rather than only observed.
* **Two activations of the same routine at once are not distinguished.** The
  yield-site test finds the innermost in-flight call whose body contains the
  return address. If two threads were ever inside the same ported routine
  simultaneously, that is ambiguous. No routine ported so far can be.
* **`fade_out` at `$80:8933` is not ported.** It is the mirror image — `DEC`
  instead of `INC`, ending in a forced blank and a bare `WAI` — and
  `movies/level1.zmv` never executes it. A routine the harness cannot reach is a
  routine the harness cannot check, so it waits for a movie that reaches it.
* **A substituted suspension over-burns its cycle budget slightly.** The measured
  mean (201 cycles for `fade_in`) covers the segment *including* its `JSL`, and
  the core then executes that `JSL` for real. It is the same shape as the
  existing over-count of the `RTS`/`RTL` for leaf routines, it is about 24 master
  cycles inside a ~57,000-cycle frame, and it stops mattering in Phase 4 when the
  reference is cut loose.

## The scheduler itself (2026-09-24)

Everything above ports routines that *call* `thread_yield`. This ports
`thread_yield`, and with it the rest of the frame's machinery: the scan after
each frame, the two vblank dispatchers and the NMI handler. Together they were
the largest family in the live residue, 44% of what the 65816 still ran over
all 56 level records. The code is `src/port/sched.c`.

**None of them returns, and that was the whole obstacle.** The harness
substituted a routine at its entry and handed the core its `RTS` or `RTL`.
`thread_yield` leaves into another thread, on another stack. A dispatcher
leaves into a job by pushing an address and executing `RTL`, and the job comes
back to an address in the middle of the dispatcher. So a routine may now name
its **exits**, the instructions of its own that control leaves it by
(`CosimRoutine::exits`). The port runs from the entry to one of them over the
whole register set: A, X, Y, the status byte, the stack pointer, the direct
page, the data bank, and which exit. The core then executes the exit
instruction, exactly as it executes `ret_op`.

An entry no longer has to be a subroutine's. `$80:8401` is where a queue A job
returns to. `$80:8372` is the instruction after the scheduler's `WAI`. Any
instruction the ROM reaches is an entry the harness can take:

| Entry | Name | Leaves by |
|---|---|---|
| `$80:8353` | `thread_yield` | `RTL` into the thread picked at `$8397`, or the `WAI` at `$8371` |
| `$00:833E` | `thread_exit` | the same two, in bank `$00` |
| `$80:8372`, `$00:8372` | `sched_wake` | the `JSL sprite_build_oam` at `$837C` |
| `$80:8380`, `$00:8380` | `sched_rescan` | `$8397` or `$8371` |
| `$80:83E0`, `$80:8401` | `vbl_queue_a_run`, `_resume` | the `RTL` into a job at `$8400`, or the `RTS` at `$8417` |
| `$80:843D`, `$80:845E` | `vbl_queue_b_run`, `_resume` | `$845D`, or `$8474` |
| `$80:8179` | `nmi_enter` | `$818F`, before `LDA $4210`; or `$81F8` when an NMI is already running |
| `$80:8199` | `nmi_stack` | the `JSL vram_queue_flush` at `$81A2` |
| `$80:81BB` | `nmi_input` | the `JSR vbl_queue_b_run` at `$81E1` |
| `$80:81E4` | `nmi_leave` | the `RTL` at `$81F8` |

**The stack is WRAM, and the port writes it.** Every push the ROM makes is
made, including the ones dead the moment they land, such as `PEA $0000 : PLD`.
So `verify` compares these with no dead-stack allowance at all, where every
other routine is allowed the bytes under its own pushes. Under `run` nothing is
left stale.

**The instructions that touch the hardware stay the ROM's.** The NMI is cut in
four around them: `LDA $4210`, both `STA $2100`s, and the wait for the
auto-joypad read. The pads are the exception. `LDA $4218` reads a latch, so the
harness hands the two words in with the registers (`CosimRegs::joy`).

### Bank `$00`

`thread_exit` was registered at `$80:833E` first and `verify` saw no call to it
on any of the 56 records, although every traced profile executes it. The
reason is in `thread_spawn`. The second return address it builds under a new
thread, the one the body's own `RTL` goes to, has a bank of zero, so a thread
ends through `$00:833E`. That is the same code through the slow mirror, and the
scheduler goes on running there: the scan leaves by `$00:8397`, and when no
later thread is ready, by the `WAI` at `$00:8371`. The next frame's wake-up and
rescan then run in bank `$00` as well, until some thread's `JSL $808353`.

So the port takes its bank from where it was entered, every `PHK` pushes that
bank, and code fetched through `$00` is priced slow whatever `$420D` says. The
lockstep sync point was matching only the `$80` `WAI`, and now takes either.

### Interrupts while the budget is spent

A substituted call spends its budget with the CPU parked on its entry, and an
interrupt that falls due is taken from there. For a jump, everything but the
program counter is published before the budget starts. An NMI that lands while
`thread_yield` is paying therefore pushes its frame below the stack the
routine left, after the thread's status, page and bank are parked, which is
where the ROM's own instructions would have met it. Publishing at the end
instead would have let the NMI's pushes overwrite them.

It also keeps the one timing effect of the scan that the game can see. If the
NMI falls due while the scheduler is scanning an empty table, the `WAI` it then
reaches waits for the *next* one, and a frame is lost. That happens under
substitution for the same reason it happens on the console: the budget is the
scan's own cost.

## Thread bodies (2026-09-25)

A thread body is the code the scheduler resumes. Nothing calls it, so until
the scheduler's round nothing could substitute it, and `native_share.py`
blocked all of them. With exits it is ported the way the scheduler is: as
stretches over the whole register set, each from an address control arrives
at to the next place control leaves. The code is `src/port/bodies.c`, and the
65816 pieces it shares with `sched.c` are in `src/port/cpu.h`.

**A stretch stops at every call, not only at its yields.** The `JSR` or `JSL`
is the stretch's exit and stays the ROM's, like every exit. The routine it
reaches is the harness's business, and most of them are registered routines
of their own. The instruction after the call is the next stretch's entry. So
no stretch contains another routine's stack traffic, `verify` compares them
with no dead-stack allowance, and a body never has to know whether what it
calls is the port's or the ROM's. The cost is one ROM instruction per call,
and the bodies ported here were chosen because they loop between calls.

| Body | Entries | What it does |
|---|---|---|
| `$81:81F6` the victims | `$81F6`, `$8206`, `$8263`, `$828F` | starts each neighbour's thread when the camera comes within `$A0`, and stops it when the camera leaves |
| `$80:C8F6` the objects | `$C911`, `$C918`, `$C967`, `$C971` | every fourth frame, gives an object in range an actor and frees one out of range |
| `$81:80EC` the actor list | `$8113`, `$814B`, `$817C` | steps one entry a frame, and starts the nearest ready one at the end of each pass |
| `$82:D7CF` animated tiles | `$D881`, `$D87A` | counts down up to eight tile sequences and queues their upload |
| `$80:CDF4` the player's frame | `$CDF7`, `$CE04`, `$CE0C`, `$CE19`, `$CE23` | yields a tick, then makes the seven calls that are a player's frame |

An entry reached by a branch from inside another stretch is fine. `$80:C918`
is where `JSR $CABF` returns, and also where `$80:C911` branches when there is
nothing to serve. Under `verify` the ROM reaches it inside the first stretch's
window, the harness starts a second check there, and both end at the same exit.
So a stretch that runs on into another names that one's exits as well as its
own.

Two of the bodies read level lists in bank `$9F` through `($0C),Y`, and the
tile body reads its sequences through `[$00],Y` and a bit table through the
data bank. The runs are priced as if every such byte were fast ROM, and the
port counts the bytes it actually read. A byte that was not fast ROM costs 2
more whatever `$420D` says. `tools/cycles816.py` learned `(dp)` and `(dp),Y`
for this.

The reset's WRAM clear went in with them, as `reset_clear` at `$80:80C1`, from
where `JSR init_ppu_regs` returns to just before `STA $4200` turns the NMI on.
It is two block moves, 131,071 bytes on a cold start, and every byte is an
instruction. Nothing interrupts it, so `verify` checks it like any call.

## The player's frame (2026-09-26)

`$80:CDF4` was taken for the level's main body when it was first declared. It
is the player's frame. Each player's thread runs it, it calls
`actor_publish_pos`, and it reads the player's health at `$1CB8` by the
doubled index at `$0E`. After `JSR $D13A` builds the page, each pass yields a
tick and then calls, in order:

| Call | What it is |
|---|---|
| `JSR $D1EA` | `LDX $70 : JMP ($D1EF,X)`: the state machine, whose first entry is `player_state_normal` |
| `JSR $D01B` | the hit recovery count at `$52`, unless `$6A` holds it, and an event request in bit 15 of `$50` |
| `($28)` | the state handler, by `PEA : LDA : DEC : PHA : RTS` |
| `($2A)` | the movement handler the same way, when there is one |
| `JSR $F327` | `actor_publish_pos` |
| `JSR $CE25` | `LDA $1D52 : BNE`: any neighbours left? |
| `JSR $CE72` | `LDA $1CB8,X : BEQ`: any health left? |

The only work of its own is `$1C = $1A`, this frame's buttons kept as the next
frame's last. So it is five stretches, at each place a call comes back to
that has something before the next call. The other five return points go
straight into the next `JSR`, and the ROM executes that itself. `$D1EA`,
`$D01B`, `$CE25` and `$CE72` are stretches too, entered by `JSR`. Each runs to
its `RTS` or to where its rare path starts, and that path is the ROM's.

**A stretch may be entered by a call and not only by a return.** Those four
are called, so they serve a call, and they are not `uncalled` in the registry.
The movement handler at `$80:E4BA` is entered by the frame's `RTS`, the
return address its own `RTS` goes back to already pushed. To the harness that
is a routine like any called one, and it is ported as one: `port/walk.h`, in
readable C, which asks the four tests as C functions. It is `uncalled`. It
was fourteen stretches for one round, and `$80:E739` a fifteenth.

**A stretch may leave before a path it does not port.** Everything no input
has taken, or that belongs to another subsystem, is left by naming its first
instruction as an exit: the event request at `$D02D`, the level's end at
`$CE2A` and a death at `$CE7A`. The ROM carries on from there with the
registers the stretch leaves. That is the same move as leaving by a call, and
it needs no guard. The walk, which is not stretches, declines its two instead:
the double step and a tile with a reaction of its own.
