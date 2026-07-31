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

There is also `verify_only`, which one routine sets: checked per call, never
substituted, because its body is a hardware handshake and the thing that performs
a handshake is the CPU. See *The uploader's 23,820 calls* below.

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
| `enemy_collide` | `$81:8888` | the ignore path — 1,225 of the 1,226 (and, since *The last decline* below, the other one too) |

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

That once was the entire remaining decline — until the next section ported it
too. `actor_overlap_pass` goes from **1,226 of 4,716** declined to **1 of
4,716**, and `sprite_build_oam` with it. Under `run`, all but three of 6,089
scheduler passes are now substituted whole, and no byte of live game state
differs on any of them.

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
between two actors with no handler), `player_ignore` and `player_no_effect` (the
player has never been told about a `≥$5C` id, and every id it *has* seen wanted
`$80:F950` rather than the table's bare `RTS`), and two of `$80:F950`'s own
exits, `hurt_state_immune` and `hurt_weapon_immune`. Those are transcribed from
the listing and have never been diffed against it, exactly like `emit_flip_y`,
and the fix for each is an input.

Native mode returns by pointing the program counter at the routine's own
`RTS`/`RTL` and letting the core execute it, rather than unwinding the stack by
hand. There is no reason to reimplement the core's stack and bank handling when
the routine already contains the instruction that does it.

#### The last decline

The one call left was an enemy taking its last hit, and it is ported. The whole
sprite pass now declines **nothing** on `movies/level1-rescue.zmv`.

`$81:889F` onwards masks the id, rules out two ids with routines of their own,
indexes the damage table at `$81:8561` and subtracts from health at `$1E`. It
then has three ways out, and the movie takes one of them:

* the difference went **negative** — the enemy died. Store it, clear `$7E`, and
  `JSR $81:8727`, which awards points and posts `$F5F5` to `$12`; then `SEC :
  RTL`. Ported.
* the difference **equals the health it came from** — the damage-table entry was
  zero, and not even the store happens. Ported, never taken.
* the enemy **survived**. `JML $81:8506`, which splices a call into the thread's
  own parked stack so that the next time the scheduler resumes it, it runs a
  reaction first. Not ported: it declines.

Two things about the death are worth having in writing.

**`$81:8888` is where a thread gets parked.** `thread_call_handler`'s
`handler_park` — carry set on the way back, thread's wait word set to `$8000` —
had read zero on every movie ever run, and this is why: `$81:88BE  SEC : RTL` is
the only thing in the game that reaches it. Porting the death branch took that
site from a transcription to a diffed one, and it found a bug on the way. The
park path ends `LDA #$8000 : STA $1180,X`, so the *parked value* is what the
caller gets back in A, not the handler's — the port was handing back `$F5F5` and
`verify` said so at call 1,405.

**`$81:8727` is where the score lives**, which is why `port/score.h` exists and
why `$80:C7D9` is registered as a routine in its own right rather than folded
into the handler. Its other caller is the victim-rescue thread at `$83:A1EC`,
which has nothing to do with collisions; registering it means both call sites
check it, and it is the only routine so far that `run` substitutes on a path the
collision chain does not reach. It is also the first ported code that is
**decimal** — the score is BCD, `SED` is on for the whole addition — and the
first whose *input* includes a flag: the `BMI` at the entry reads the caller's N,
which is bit 15 of the collision id, which is how the game knows which player's
weapon did it. Bits 0-14 are the id; bit 15 is the shooter.

Along the way the two awards fell out of the diff: a victim is worth **$1000**
and a kill **$0100**, both BCD.

Six deliberate perturbations. Three were caught, and the three that were not are
more useful than the three that were:

| Change | Result |
| --- | --- |
| award `$0200` instead of `$0100` | `$7E:1E73` at call 685 of `enemy_collide` — but `score_add` still passed, because intercepted at its own entry it takes the award from the ROM's X |
| post `$F5F4` instead of `$F5F5` as the death request | `$7E:0812` at call 685 |
| return carry **clear** from the death | flag C at call 685 of `enemy_collide`, *and* `$7E:11A6` at call 1,405 of `thread_call_handler` — the flag and the write it causes one level up, caught independently |
| search the two score slots in the wrong order | `$7E:1E73` at `score_add`'s **first** call — the victim rescue, before any collision happens |
| break the BCD decimal adjust (`+$6` → `+$7`) | **not caught.** Decimal and binary addition agree until a digit runs past 9, and neither of this movie's two awards gets there. There is now a coverage site, `score_digit_carry`, that says so by name |
| delete the `STZ $7E` on the death path | **not caught.** The word is already zero — the enemy's own init cleared it and nothing writes it in between — so the store is indistinguishable from a no-op. Kept, because it is what the ROM does, and recorded in `port/collide.c` because a store the diff cannot see is worth writing down |

That last pair is the honest shape of this instrument. A branch nobody takes is
reportable; a *store* whose value was already there is not, and the only defence
is to notice and say so.

#### Declined to *what*: the census

`decl.` counts the calls the port handed back, and the coverage report names the
branch that decided each one. Neither says **where the ROM went instead**, and
that is the number that tells you what to port next.

So a guard that knows the address records it, and the report prints the distinct
ones with their counts (`cosim_census_note` / `cosim_census_report`):

```
Declined to, by address — where the ROM went when the port stepped
aside. Each line is one unported routine, and the count is how much
of this run porting it would have bought:

reached from     address      declines
---------------- ---------- ----------
handler          $81:FE0E          616
handler          $83:A364            2
```

That is `movies/level1-2p.zmv` before the two rounds below, and it is the whole
argument for the thing: "618 dispatches to a handler the port does not have" is
a hole of unknown shape, and **two addresses, one of them 616 of the 618**, is a
morning's work. It also keeps itself honest — a routine that gets ported drops
off the list by itself, and both of those have: the same command on the same
movie now prints no census at all.

Two rules keep the addresses meaningful. A decline *through* a ported handler is
not censused at the door it came through: `thread_call_handler` only records
entries that are not one of the handlers it has, because otherwise an unported
jump-table target inside `player_collide` would be filed under `$80:F7F7` and
name the wrong routine. And `player_collide` censuses its **table entry**, not
the id that indexed it, because two ids sharing a target are one piece of work —
which is why the same report on `movies/level1-rescue.zmv` reads `player id
table $80:F92D 1`, the sound effect, rather than an id number nobody can act on.

#### Through the door again: the shot

`$81:FE0E` is twenty-one bytes, and it was 616 of the 618 declines above.

The thread at `$81:FCB2` registers it (`$81:FCCC  LDA #$FE0E : LDY #$0081 : JSL
thread_set_handler`), sets `$42` to `$14` at launch, and spends twenty frames
in `DEC $42 : BNE` — a weapon shot. Its handler is what happens when the shot
touches something: if the id is one of four (`$0000`, `$0003`, `$0004`,
`$0001`), write 0 through `$0A` into the record's `ACTOR_COLLIDE_ID` so the shot
cannot hit anything else, and write 1 to `$42` so the next pass ends it. Any
other id, it flies straight through.

It is the first ported handler that **reaches out of its own direct page into
the game's shared data structure** — `$81:FE21  LDY $0A : STA $000E,Y` writes the
very field `actor_overlap_pass` reads to decide whether a record can collide, and
`overlap_no_id` is the branch that then skips the spent shot. It is also the
first ported routine with no guard at all: there is no condition under which it
can decline, because all of it is here.

`verify` caught a real error on the first run. Y is an output, and the expire
path's `LDY $0A` overwrites the `TAY` at the entry — the port was handing back
the id where the ROM hands back the record address, and the diff said `Y: ROM
$19C6, port $0003` on 597 of 616 calls. The 19 that passed were the pass path,
where `TAY` really is the last word into Y.

Five perturbations, and the split is the usual one:

| Change | Result |
| --- | --- |
| write 2 to `$42` instead of 1 | `$7E:01C2` — the shot's own page, immediately |
| drop the `ACTOR_COLLIDE_ID` clear | `$7E:19D4`: ROM `$00`, port `$5C`. The display record, and the id it was still carrying |
| claim carry **clear** on the expire path | `$7E:1198` — not a register at all. The flag is parked on the caller's stack and the write shows up there |
| leave Y as the id on the expire path | flag-for-flag the real bug above, caught at 597 of 616 |
| also stop on id `$0002` | **not caught.** No shot in this movie ever touches an id 2, so a fifth `CMP` is unreachable code that agrees with the ROM by never running |
| take N from the id rather than from `id - 1` on the pass path | **not caught.** Every id that reaches that path is small and positive, so both are 0. Id `$8000` would tell them apart and nothing produces one |

The last two are the same lesson as `score_digit_carry` and the `STZ $7E`, from
the other side: this time the port would have been *more* permissive than the
ROM and no diff could see it, because the inputs that distinguish them do not
occur. `shot_expire_zero` is a coverage site for exactly the third row's
concern — id 0 is the one path that executes no `CMP` at all, so carry leaves as
the caller's rather than set — and it is untaken, which is the report saying that
one of the three exits is transcribed rather than diffed.

#### And the last address on the list: the victim

`$83:A364` was the other of the two, and with it ported **the census is empty of
everything except the sound effect**. On `movies/level1-2p.zmv` the report does
not print a census section at all; on `movies/level1-rescue.zmv` the single line
left is `player id table $80:F92D 1`.

It is a latch, and the smallest kind of handler there is. `LDX $1E : BNE` — if
anything has already happened to this victim, the routine is two instructions and
writes nothing. Otherwise eight `CMP`s in a row, each branching to its own two or
three instructions, and the one that matches writes a code into `$1E` for the
victim's own thread to find (`$83:A239  LDA $1E : BNE` is where it wakes). Five
of the eight also clear `ACTOR_COLLIDE_ID` in the display record — a victim
switching its own collision off so nothing can claim it twice, which is exactly
what `shot_collide` does to a spent shot, through a different field of a
different page.

Two of the eight ids differ in one word and nothing else: id 5 latches the id
*itself* into `$18`, id 6 latches `$8000`. `$83:A392  BRA` skips the `LDA #$8000`
the other falls into, which is three bytes saved and the reason the field ends up
holding `$0005` rather than a flag. `$18`'s only reader is `$83:A1EA  LDA $18 :
JSL $80C7D9` — the rescue thread on the same page — and `score_add` reads bit 15
of it and nothing else. So **the pair is the two players**, and that is measured
rather than argued: on `level1-rescue.zmv` the id-5 path runs and `score_slot_0`
is credited four times against `score_slot_1`'s zero.

Six perturbations, and the interesting one is the miss:

| Change | Result |
| --- | --- |
| latch event 2 instead of 1 on the claim path | `$7E:041E` — the victim's own page, which the diff thereby locates at `$7E:0400` |
| latch `$8000` instead of the id into `$18` | `$7E:0418`: ROM `$05`, port `$00`. `score_add` still passed at its own entry, because `verify` re-snapshots per call and the corrupted word never reaches it |
| drop the `ACTOR_COLLIDE_ID` clear | `$7E:1A60` — inside the display list, a different region of WRAM from the direct page, which is what proves the routine reaches outside its own page |
| return carry **clear** from the `$FF` exit | flag C at `victim_collide`'s own entry **and** `$7E:11A6` one level up, independently — carry set is what parks the thread |
| keep the collision id regardless of `$26` | `$7E:1A38`, in **five routines at once** — the whole six-deep collision chain sees the same byte |
| **delete the latch guard entirely** | **not caught**, on any of the three movies |

That last row is the one to keep. The latch is the entire design of the routine —
a victim has one fate and the first thing to reach it decides which — and no diff
can check it, because no movie ever dispatches to the same victim twice. A port
that let a victim be claimed a second time agrees with the ROM forever. It is the
same shape as `shot_collide`'s fifth `CMP`: the port is *more permissive* than
the ROM, and only an input that produces the distinguishing case can tell.
`victim_latched` is the coverage site that says so by name, and it is untaken.

One number went **down**, which is worth reading correctly. `handler_unported` —
"a dispatch to a handler address the port does not have" — used to fire, on the
declines to this very address. It is now untaken by every movie, because there is
no handler address left for the dispatcher to decline. A decline site is a site
like any other, and porting the thing it was counting is what makes it go quiet.
So the union across the three movies is **48 of 72**, not 46 of 63 plus three.

### Registers are outputs too

A 65816 routine's contract is its registers plus WRAM. Each routine has a *shim*
in `src/cosim/routines.c` that says what the registers meant on the way in and
what the ROM leaves in them on the way out, citing the instruction that decides
each one.

Flags are opt-in: a shim declares which of N/Z/C/V it modelled, and `verify`
compares exactly those. That keeps the claim as strong as the evidence and no
stronger — and, as it turned out, makes an unmodelled flag a visible gap rather
than an invisible one. A, X and Y work the same way, through a second mask.

**V was added to that set by `run`, not by `verify`, and the asymmetry is worth
recording.** Thirty-eight routines needed only N/Z/C, because a caller that
branches on overflow is rare and none of them had one. `rng_next` (`$80:9D39`)
does not have one either — what it has is an `ADC` whose overflow output survives
the `RTL` into a *thread suspension*, where `thread_yield`'s `PHP` turns the flag
into a byte of WRAM. `verify` cannot see that: it compares the flags a shim
claims, and a flag nobody claims is a flag nobody looks at. The whole-program
diff can, and did — `$7E:0DE8  stock $40, native $00`, one byte on two movies out
of twenty-five, `$40` being exactly bit 6 of `P`. **An unclaimed output is
invisible to the per-call diff by construction and visible to the whole-program
one by accident**, which is the clearest argument yet for running both.

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

## The APU: an output the diff could not see

For several rounds the decline census came down to two addresses and one
question. `$80:F92D` and `$80:F87B` are both entries of the player's collision
jump table, both open with `JSL apu_play_sfx`, and neither was hard — what was
missing was a decision about **how port code drives the APU**, because until
then the port had only ever touched WRAM.

`src/port/apu.h` is that decision and states it at length. The short version is
a three-way split of `$80:CCC8`:

* what it does to **memory** is one byte, `W_APU_SEQ`, and the port owns it
  exactly as it owns the rest of WRAM;
* what it puts on the **bus** goes out through `ApuPorts`, a hook the host
  installs once;
* and the **wait** — the `CPY $2143 : BNE` the CPU spins on until the SPC700
  echoes the last command — is the host's, because the port has no way to
  advance an SPC700 and no business trying.

That split is what keeps the routine verifiable: `apu_send()` is left a pure
function of WRAM plus two arguments, so `apu_play_sfx` is intercepted, rewound
and diffed exactly like every other routine. The harness installs a different
hook per mode. `verify` installs a **recorder** — the ROM already made the noise
and the port is being replayed over a snapshot of the past, so writing to the
emulated APU here would send every sound effect twice. `run` installs a
**driver**, which performs the wait by advancing the machine until the SPC
answers and then writes the three ports for real. Whether that wait can be made
to work at all turned out to be the interesting question, and the section after
next is about it.

### Why the ports are compared, and what that caught

This leaves a hole that no amount of WRAM diffing closes: **a port that computed
the sequence counter correctly and sent nothing at all would pass every byte of
every check**, because the ports are not memory. So the traffic is compared too,
by the same rule as everything else here — watch what the ROM's own instruction
put on the bus. `$80:CCD1  STX $2142` is that instruction, with the command in X
and the parameter in A, and it is where `zamn_assets verify-music` hooks for the
same reason. Each call records the window of ROM sends that happened inside it
and diffs them against what the port asked for.

Three perturbations, all caught on the first call:

| perturbation | what failed |
| --- | --- |
| send nothing to the APU at all | `APU: the ROM sent 1 command, the port 0` |
| send the sound effect id + 1 | `APU command 0: ROM $01/$31, port $01/$32` |
| take N and Z from the `INY` rather than the `PLD` | `flag N: ROM 0, port 1` |

The first is the one that justifies the mechanism: without the bus comparison it
passes 16,607 of 16,607 calls and the whole game goes silent.

The third is a finding about the routine rather than about the harness.
`$80:CC3B` ends `REP #$30 : PLD : RTL`, and **`PLD` sets N and Z from the
direct page it restores** — so the flags a caller gets back describe *its own
direct page*, not the sequence counter the visibly-last arithmetic (`INY`)
produced. It is the first ported routine whose output depends on a register the
caller never thought it was passing.

One more thing had to be got right, and it is the same shape as the coverage
counters one line above it in `guard_allows`. A guard answers "can the port
serve this call?" **by running the port** on a throwaway copy of WRAM. That is
harmless for memory and not harmless for a sound card: five nested guards asking
about one pickup would play it five times, and under `run` they would be real.
The hook is muted for the length of a dry run.

### And the two routines behind it

`$80:F92D` is three instructions and was for several rounds the shortest
unported routine in the game. `$80:F87B` is the player's side of a pickup — the
other half of `object_collide`, which was ported last round — and is the game's
second decimal routine: it turns the item's collision id into an inventory slot,
adds a BCD amount from a table at `$80:F8AC` to the counter there, and caps it
at `$0999`.

It also **tail-calls**, and that is where the round stopped being about audio.
A player who picks something up while holding no weapon falls through
`JMP $80:EA63`, which searches their inventory for one and selects it — and that
is the path the single pickup on `movies/level1-2p-rescue.zmv` actually takes.
So `$80:EA63` is ported too (`src/port/player.h`), registered in its own right
because it has a second caller that has nothing to do with pickups.

**That second caller is a correction.** An earlier round established that `Y` is
the fire button and recorded that `B` "does nothing", because holding B for 120
frames moved no counter. It does something: `$80:D259  LDA $1A : AND #$8000 :
... : JSR $EA63` is the player's input handler, edge-detecting **B**, cycling to
the next weapon. It looked like nothing because a player carrying one weapon
cycles to the one they are already holding, and the routine's first exit is
`CMP $1CBC,X : BEQ` — no store, no sound, nothing to see.

Three perturbations of the selector were caught, each on its first call:
starting the search at the slot already held (`$7E:012E`, which is how the
player's page is known to be based at `$7E:0100`), giving it fourteen tries
instead of fifteen, and never writing the countdown back. The middle one failed
at **`$7E:001E`** — the APU sequence counter — because one try short of a full
lap the search finds nothing, decides the weapon changed, and plays a sound the
ROM did not. Two subsystems ported an hour apart, and the second is what caught
the first.

### What it did not catch

Three perturbations of the pickup were **not** caught, and all three have one
cause: every input that exists picks up exactly one item.

* **Dropping the `$0999` ceiling** and **breaking the BCD decimal adjust** pass
  every call. Both are named by coverage sites — `pickup_capped` and
  `pickup_digit_carry` — which is the report saying in advance what the
  perturbations then confirmed.
* **Indexing the amount table by the id rather than the doubled id** also passes
  every call, and *nothing* names that one. The item picked up is id `$0C`, the
  first, so its slot is 0 — and every wrong way of computing zero is also zero.
  It is not a branch, so no coverage mark can express it; it is written down in
  `src/port/collide.c` beside the line, the same defence the `STZ $7E` in
  `enemy_die` gets.

### The census is empty

With those four routines ported, **no call declines anywhere on any movie.**
`verify` on `movies/level1-2p-rescue.zmv` checks 138,513 of 138,513 with a
zero in every `decl.` column and prints no census section at all; the other
three movies do the same. `run` substitutes all twenty-one routines over 5,989
scheduler passes with at most 35 bytes differing at once, every one of them
inside the stacks or a declared scratch byte. (Those totals are the figures
before `apu_send` was registered in its own right; the section after next
replaces them.)

That number will not stay at zero — it goes back up the moment an input reaches
`$81:8506` or one of the two special collision ids — and that is the point of
the instrument rather than a failure of it.

There are **88** marked sites now, up from 75, and the four movies take 26, 42,
50 and 56 of them for a **union of 59**. Three of the thirteen new sites are
already untaken by everything, and all three are the same missing input:
`pickup_taken` — the pickup's own return path, which the one pickup in the
corpus misses by tail-calling instead — plus `pickup_capped` and
`pickup_digit_carry`. `weapon_unchanged` looked like a fourth and is not: the
two-player movies press B with an empty inventory, but `level1.zmv` and
`level1-rescue.zmv` both take it.

## The uploader's 23,820 calls

`$80:CCC8` had been checked all along, but only ever through `apu_play_sfx` — a
few hundred calls, all of them from the same handful of sites, all of them
sending the same command. Meanwhile the same eleven instructions are the inner
loop of the data-set uploader, which puts **23,820 commands** through them on
every movie. Registering the routine on its own entry PC turns the largest single
body of execution in the whole corpus from unchecked into checked, and it did not
need a line of new port code — only a signature that says what the arguments are.

### The width question, answered by the routine's first instruction

The reason this waited several rounds is written down in `port/apu.h`, and it was
wrong. `$80:CCC8` has two kinds of caller and they disagree about register width:
`apu_play_sfx` arrives sixteen bits wide, the uploader eight (`$80:CC90  SEP
#$30`). `CosimRegs` has no width field, so intercepting here looked like it would
mean comparing high bytes that mean nothing, and adding a field looked like the
prerequisite.

It is not, and the reason is the routine's own first instruction. `SEP #$30`
normalises everything the contract depends on before anything else happens:

* `A` is only ever **read** (`STA $2141` takes the low byte), so the whole
  sixteen-bit register comes back exactly as it went in — high byte included,
  whatever junk it holds. On the uploader's path that high byte is the top half
  of a pointer word left over from `$80:CC84  LDA $80CCE0,X`. It is junk, and it
  is *preserved* junk, identically on both sides.
* `X` and `Y` have their high bytes **cleared** by that `SEP` itself, so a caller
  who arrived wide is narrowed before the routine looks at them, and a caller who
  arrived narrow was already there.
* `Y` is then loaded fresh from one byte of WRAM.

So the port takes `A` and `X` full width — which is what the ROM's calling
convention actually says — and there is nothing left for a width field to
disambiguate. **23,632 of 23,632 calls pass on `level1.zmv`, first run**, and
23,656 / 23,979 / 23,997 on the other three.

### What the new sample caught

Six perturbations. Four fail on **call 1**:

| perturbation | what failed |
| --- | --- |
| never write the counter back | `WRAM $7E:001E: ROM $01, port $00` |
| swap the command and the parameter | `APU command 0: ROM $08/$01, port $01/$08` |
| take N and Z from the pre-`INY` counter | `flag Z: ROM 0, port 1` |
| return carry clear | `flag C: ROM 1, port 0` |

The second is the bus comparison earning its keep for a second time — it is not
a memory difference and nothing else in the harness could see it.

The fifth is the one worth having the sample for. **Returning a sixteen-bit
`seq + 1` instead of the byte passes 251 calls and fails on call 256**, with
`Y: ROM $0000, port $0100`. The sequence counter is one byte and it wraps, and
the only thing in the game that goes round it is an uploader sending 23,820
commands in a row — which goes round it ninety-three times. A few hundred
scattered sound effects would have had to be very unlucky to catch that.

### What it did not catch

The sixth was not caught, and it is the honest limit of the width argument above.
**Deleting the `x & 0xff` mask passes every call on every movie.** The six call
sites in the ROM are `LDX #$0001`, `#$0002`, `#$0006`, `#$0008`, `#$000A` and
`#$0013` — every one a small constant whose high byte is already zero, so the one
place register width could still have shown through is a place no caller ever
puts anything. It is not a branch, so no coverage mark can express it; it is
written down in `src/port/apu.c` beside the line, the same defence the
`STZ $7E` in `enemy_die` and the doubled-id index in `player_pickup` get.

### `verify_only`: a routine whose body is a handshake

Substituting it does not work, and that is a genuine finding rather than a bug to
fix. `run` with `apu_send` substituted **desynchronises the two cores inside the
first ten scheduler passes** — `$7E:0016`, the NMI frame counter, reads `$02DC`
on the stock side and `$0595` on the native one.

Chasing it is worth recording, because the answer is structural. The first guess
was the cycle budget, and it is true that no single budget fits a routine whose
measured cost is 218..85,184 master cycles. It is also beside the point. Eight of
those eleven instructions are `CPY $2143 : BNE`, and **the spread is not work the
budget should be paying for — it is the SPC700 deciding how long the 65816 has to
sit still.** So `apu_drive`'s wait was changed to burn the machine's time
(`snes_runCycles`) rather than the APU's (`apu_runCycles`), which is right on its
own terms: fast-forwarding the APU alone moves it without moving `snes->cycles`,
putting it on a different clock from everything else.

It still deadlocks, and instrumenting the hook says why in one line: during a
driver upload the SPC700 sits in the driver's own **RAM-clear init loop**
(`$0672: MOV [$C0]+Y,A : INC Y : BNE`), and the harness watched it run **12,959
of its own cycles** there without answering, where the ROM's spin at the same
point takes about a thousand. The machine is being advanced; the CPU is not, and
during a wait that long the CPU is what the rest of the machine is waiting for.
Substitution's whole mechanism is to stop the 65816 executing, so a routine whose
body *is* a bus handshake cannot be substituted from inside it.

So the registry grew one field. `verify_only` means **checked per call, never
substituted**, and the report prints `verify only` where a verdict would go so
the exclusion is visible rather than silent:

```
apu_send                    0       -        0        0      0      0      0  -   verify only
```

Two things keep this from being a euphemism for "gave up". It is not a decline —
nothing was offered, so nothing is claimed either way, and the `decl.` column
stays honestly at zero. And `apu_play_sfx`, sitting directly on top of it, *is*
still substituted, which is the same fact from the other side: a lone sound
effect finds the SPC caught up from sounds ago, so its wait is satisfied by the
first read and no spin happens at all. That is also why its cycle budget is now
the measured **minimum** (484) rather than a mean — every call it makes is a call
that did not wait.

None of this is a limit on Phase 4. The finished port owns its own main loop and
can spin on `$2143` exactly as the ROM does. What it cannot do is spin while
impersonating one instruction inside somebody else's core.

### Where the totals stand

Twenty-two routines. `verify` checks **40,239** calls on `level1.zmv`,
**130,048** on `level1-rescue.zmv`, **150,170** on `level1-2p.zmv` and
**162,510** on `level1-2p-rescue.zmv` — every one passing, with a zero in every
`decl.` column and no census section printed on any of them. `run` still
substitutes twenty-one of the twenty-two over 2,389 / 6,089 / 5,989 / 5,989
scheduler passes with at most 18 / 31 / 34 / 35 bytes differing at once, every
one inside the stacks or a declared scratch byte, and no byte of live game state
ever differing. Branch coverage is unchanged at 88 sites, because `apu_send` has
no branch of its own to mark: its one decision point is the wait, and the wait
belongs to the host.

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
run to check**, not every `if`: 88 of them across the twenty-one routines,
chosen by hand. **Hit counts are call-weighted, not event-weighted** — under `verify` the
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
60 sites** are taken, which was 20 of 36 before the collision handlers added
fifteen more, and nine more again for the death path, that only the other movie
reaches. It reproduced all three
`sprite_build_oam` gaps found by
hand, plus `actor_depth_sort`'s `ACTOR_SORT_FIRST` and Phase 2's known
vertical-flip gap, without anybody perturbing anything. It also named ten more
nobody had listed, of which the sharpest is this: **`sprite_frame_tile` is
called 10,354 times by that movie and never once evicts a resident frame.** The
most-called ported routine in the game, and the branch that makes it a *cache*
rather than a lookup table had never run.

`movies/level1-rescue.zmv` was written against that report and takes **39 of
60**, over 106,351 checked calls with nothing diverged. It closes five of the
original ten and reaches ten of the fifteen the handlers added, plus four of the
nine the death path added — including both of the two that mattered most:

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

Twenty-one are still untaken and they are a measured backlog rather than a
suspicion. Ten are the original ones minus the five closed: `emit_flip_y` (no
shipped actor in level 1 flips vertically), `sort_key_first`, `draw_attr_set`,
`queue_full`, the two OAM-full sites, three defensive branches in
`sprite_build_oam` that a well-formed record may simply never reach, and
`collide_none` — a fact about the *game* rather than about the movie: 1,226
collisions and not one of them between two actors that were not listening. Four
more arrived with the handlers and are listed at the end of *Through the door*.

The last seven arrived with the death path, and they sort into two kinds. Two are
outcomes the game plainly has and this movie does not produce —
`enemy_survived` (every hit in it kills, because the graveyard's zombies die in
one) and `enemy_no_damage` — plus `enemy_hit_special`, the two collision ids with
routines of their own. The other two want a **second player**: `score_slot_1` and
`score_discard` are both about the search in `$80:C7C2`, which with one player is
the identity and therefore proves nothing. `score_carry` and `score_digit_carry`
want a score large enough to carry, which a longer session would give for free.
Each one is a claim this document does not get to make yet.

### Two controllers, and the button nobody was pressing

`movies/level1-2p.zmv` was written against that list, and it took two changes to
the instrument before it could be written at all.

The first is that `.zmv` had one controller. A frame may now carry a `2:` prefix
that aims the line at port 2, and the two ports are independent streams with
their own absolute semantics (`docs/analysis-tools.md` → *Two controllers*).
Every existing movie means exactly what it meant before, because a movie with no
`2:` lines holds nothing on port 2.

The second is a finding, and it invalidates a sentence in two movie files:
**`Y` is the fire button, and `B` does nothing.** `movies/level1.zmv` and
`movies/level1-rescue.zmv` both hold `B` for thousands of frames with a comment
saying they are shooting, and the ammo counter reads 150 at the start of
gameplay and 150 at the end of both. That is the real explanation for a number
recorded above as a property of the game: `level1-rescue.zmv` produces 1,226
collisions and **one** enemy taking damage not because collisions rarely hurt,
but because nothing in it ever fired. The inputs are unchanged — every count in
this document was measured against those exact bytes, and a regression baseline
that quietly changes cannot regress — but the comments now say what they do.

With both fixed, `movies/level1-2p.zmv` takes **44 of 63 sites** over 69,786
checked calls with nothing diverged, and closes six that nothing else could:

* `emit_flip_y` — **1,318**. The longest-standing gap in the project:
  `$80:BB30`/`$80:BBA6` were ported from their ROM bytes in Phase 2 and had
  never once executed. `zamn_assets verify-sprites` on this movie intercepts
  **24,183** emissions across all four emitters — 966 flip-y and 352 flip-xy
  among them — and every one is byte-identical, across 127 distinct metasprites
  against the old movie's 40. Phase 2's *Gap: the movie never flips vertically*
  is closed by execution, not by argument.
* `player_ignore` — 3,272. Two players touching, which is the only thing that
  hands a player's handler an id of its own side.
* `score_slot_1` — 18, against `score_slot_0`'s 33. With one player the search at
  `$80:C7C2` is the identity and cannot be told from a hard-coded index; this is
  the first input that tells them apart.
* `player_no_effect` — 3. An id whose jump-table entry is a bare `RTS`.
* `draw_empty_meta` — 1,272. A metasprite with no pieces, which had been zero on
  every movie before this one.

It also runs clean under substitution: `run` compares **5,989 of 5,989**
scheduler passes, at most 27 bytes differ at once, and every one of them is
inside the stacks or a declared scratch byte — no byte of live game state ever
differs.

Nineteen sites remain untaken on it, and the movies are complementary rather
than ordered: `level1-rescue.zmv` still holds `cull_offscreen` and
`player_unported`, which this one does not reach. **The union across all three
is 46 of 63**, so seventeen sites are untaken by every input that exists. What is
left wants inputs nobody has written — a tougher enemy for `enemy_survived`, a
busier scene for the three OAM-full and queue-full sites, a longer session for
the two BCD carry sites — plus `collide_none` and the three defensive branches in
`sprite_build_oam`, which may well be facts about the ROM rather than gaps in
the movies.

### The second player rescues somebody

`movies/level1-2p-rescue.zmv` was written against that list too, and it went
straight for the one entry it named as the most gettable: **`victim_claim_b`,
the second player walking into a victim.**

`victim_collide` reacts to eight collision ids, and two of them — `$0005` and
`$0006` — produce the same event and differ in one word. Id 5 latches the id
*itself* into the victim's `$18`; id 6 latches `$8000`. `score_add` reads bit 15
of that word and nothing else, so the pair is the two players, and until this
movie only the first half had ever run. The input is not subtle: give Julie the
controller, let Zeke stand still, and walk her up and left into the cheerleader
`movies/level1-rescue.zmv` rescues with Zeke. `victim_claim_b` fires, and the
credit lands in `score_slot_1` for a *rescue* rather than for a kill, which is
the other half of "bit 15 names the player" proved on the other kind of award.

Two more things came out of it that were not planned, and one of them retires a
suspicion this document has carried since `actor_collide_notify` was ported.

**`collide_none` is reachable.** It is the branch where a collision happens
between two actors *neither* of which has a handler registered, and it had read
zero on every movie ever run — enough that the note above it said it "may well be
unreachable by design rather than by the movie". It is not. Playing two players
as a pair rather than counter-phase reaches it **2,277** times. The eight words
`actor_collide_notify` writes are no longer only ever checked through a handler.

**And the decline census grew two addresses, which is the point of having one.**
Both are one event seen from its two sides: a player picked something up.

* `$80:CAEE` is the **object manager's** collision handler, and it is now ported
  (`object_collide`, the fifth handler). It was identified from the ROM rather
  than guessed at: `$80:C9D6  LDA #$CAEE : LDY #$0080 : JSL thread_set_handler`
  is the last thing `object_list_parse` does, so the routine `src/assets/actor.c`
  ports the static half of is the same routine that installs this. Every object
  in the level shares one thread, so the handler's direct page is the *manager's*
  and the object is handed to it in `W_HANDLER_SELF` — the first ported handler
  that is not an actor reacting on its own behalf. What it does is eleven
  instructions: switch the touched object's collision off, append its record
  address to a queue on the manager's page, and park. **The reaction is
  deferred, not computed here.**
* `$80:F87B` is the *player's* side of the same pickup, and it is **not** ported,
  because it opens with `LDA #$000E : JSL apu_play_sfx`. What is behind that call
  is not a sound effect this time: it indexes a table at `$80:F8AC` and adds the
  amount to a counter with `SED` on, capped at `$0999`. So the audio wall now
  blocks real arithmetic — the second decimal routine in the game — rather than
  only a noise, which raises what the APU decision is worth.

`object_collide` passes its one call whole-WRAM and registers first try, and
three deliberate perturbations were caught at the exact byte, all three failing
at `$7E:1A10` — the touched object's `ACTOR_COLLIDE_ID` in the display list,
which is also how the object's own id is known to be `$0C`: dropping the `STZ`,
advancing the queue cursor by 4 instead of 2, and queueing `W_HANDLER_OTHER`
instead of the object. **The fourth was not caught, and it is the finding.**
Deleting the entry guard — letting an object already sitting in the queue be
queued a second time — passes every call on every movie, because nothing has ever
touched a spent object. That is the same shape as `victim_collide`'s latch and
`shot_collide`'s fifth `CMP`, for the third time in one file: the port would be
*more permissive* than the ROM, and only an input can tell. `object_spent` is
the coverage site that names it.

Across 138,129 checked calls nothing diverges, and `run` compares **5,989 of
5,989** scheduler passes with at most 18 bytes differing at once, all of them
inside the stacks or a declared scratch byte. The movie takes **49 of 75** sites
— the most of any single input — and it is the only one that reaches
`collide_none`, `object_taken` and `victim_claim_b`. **The union across all four
is 51 of 75.**

`level1.zmv` and `level1-2p.zmv` now contribute nothing their siblings do not,
which is worth saying plainly: they are kept as regression baselines, not as
coverage. `level1-rescue.zmv` is down to exactly one site of its own —
`victim_claim_a`, and it keeps that only because this movie deliberately has
Zeke stand still. `cull_offscreen` and `player_unported`, which it used to hold
alone, are both reached here as well.

### Two pickups, two items

The list above is written on the assumption that an untaken site is the only
thing a movie can be missing. It is not, and `movies/level1-pickups.zmv` was
written to close the other kind of gap — the one this document calls *a store
the diff cannot see*, in its worst form so far.

`player_pickup` (`$80:F87B`) turns a collision id into an inventory slot:
`SEC : SBC #$0018`, on an id the dispatcher has already doubled. Two spellings
of that produce the same answer for the first item and different answers for
every other one, and **every input that existed picked up exactly one item and
it was the first.** Id `$0C`, slot 0. So `index / 2 - PICKUP_ID_FIRST` — which
is off by a factor of two on every other id in the game — passed all 138,513
calls, and no coverage site could name it, because it is not a branch. Two of
`player_pickup`'s four marks read zero for the same reason: the one pickup left
through the auto-select tail rather than the routine's own `RTS`, and $99 added
to an empty counter needs no decimal adjust.

**Aiming an input at that needed the object table, not the level.** A level's
object list is placement data — x, y, type — and the type is an index into
`$80:CA30`, which is what gives the object's display record its **collision
id** (`docs/asset-formats.md` → *What the type means*). Read that way, level 1's
nine objects sort themselves into a shopping list:

| obj | type | position | id | `$80:F808` entry |
| --- | --- | --- | --- | --- |
| 6 | `$16` | (251, 302) | `$12` | `$80:F87B` — pickup, **slot 6** |
| 0 | `$00` | (307, 135) | `$0C` | `$80:F87B` — pickup, slot 0 |
| 1, 2, 3 | `$08` | (128, 310), … | `$21` | `$80:F8D6` — not ported |
| 5 | `$34` | (351, 71) | `$2D` | `$80:FA26` — not ported |

So the movie is a route: walk over object 6, then over object 0, and go nowhere
near objects 1 and 5, whose ids would come back as declines. It is tighter than
it sounds, because `$80:BEE1` is a 16×16 box on the two records' own
coordinates — eight pixels either way, which is four frames of walking — and the
bench under object 0 is solid, so the last four lines of the movie go round its
right end and drop onto the item from above.

It **checks 41,233 of 41,233 calls with nothing declined**, and it settles two
of the three open questions:

* **The slot arithmetic.** Object 6's id is `$12`, so the right spelling gives
  `$0C` and the wrong one `$06`. The wrong one now fails on **call 1** at
  `$7E:1CD3` — the *high* byte of the neighbouring inventory word, because slot
  `$06` also reads the wrong entry out of the amount table at `$80:F8AC`
  (`$0300` where `$0020` belongs). A line that survived 138,513 calls falls on
  the first call of a movie written to look at it.
* **The BCD.** Slot 0 starts level 1 at `$0150` and object 0 pays `$0099`, so
  the tens digit carries: `pickup_digit_carry` is taken, and deleting that line
  of `bcd_add16` fails on **call 2** at `$7E:1CCC` — ROM `$49`, port `$E9`. The
  same perturbation still passes call 1, whose `$0000 + $0020` needs no
  correction, which is the instrument being exactly as sharp as the input is.
  You can read the whole finding off the HUD: the ammo counter goes 150 → 249.

**The third is still open, and it was named in advance.** Dropping the `$0999`
ceiling passes every call on every movie, and `pickup_capped` reads zero on all
five. Level 1 pays `$0099` twice into a counter starting at `$0150`; nothing
in reach of a route gets near `$0999`. That one wants a long session, not a
better path — and the difference between "not caught, and here is the site that
says so" and "not caught" is the whole reason the marks exist.

`run` compares **2,389 of 2,389** scheduler passes with at most 33 bytes
differing at once, every one inside the stacks or a declared scratch byte. Both
pickups happen identically on the native side — the inventory words are live
game state, and no byte of live game state ever differs. Note what the run's own
table says about how they got there: `player_collide` and the six routines above
it read *not reached*, and `sprite_build_oam` reads 2,389. Under substitution the
whole collision chain runs inside the port rather than through six separate
interceptions, so the counts are a picture of the call graph and not a gap.

The movie takes **34 of 88** sites, which is the fewest of the five: it is 2,400
frames long, it fires nothing, it rescues nobody, and it is the only input that
holds `pickup_taken` and `pickup_digit_carry`. That is the argument for writing
narrow movies. The union across all five is **61 of 88**.

### The same routine again, one array over

The table in the section above has two rows that end *not ported*, and they are
the whole of what `movies/level1-pickups.zmv` deliberately walked around.
`movies/level1-keys.zmv` walks onto the first of them.

`$80:F8D6` is the player's second pickup routine, and reading it after `$80:F87B`
is uncanny: the same `PHX` around the same sound effect, the same `PLA : SEC :
SBC` turning a doubled id into a byte offset, the same `SED : CLC : ADC` out of a
parallel table, the same `CMP`/`BCC` ceiling, the same `CLD : LDY $0E : LDA
<selected>,Y : BPL` into a selector. Five constants differ and nothing else does:
the base is `$66` not `$64`, the first id is `$21` not `$0C`, the amounts are at
`$80:F907` not `$80:F8AC`, the ceiling is `$0099` not `$0999`, and the tail goes
to `$80:EAA8` rather than `$80:EA63`. `$80:EAA8` is `$80:EA63` the same way:
twelve slots against fourteen, thirteen tries against fifteen, and no
weapon-data lookup on the way out. **Two inventories, two routines each, written
twice.**

That symmetry is why the port is small and why the *movie* is where the work
was.

#### Ordering is the movie

The player does not start level 1 empty-handed. `$1CC0` is seeded to 7 and item
slot 7 holds one first-aid kit, so the item array is never empty, and four of
the eight decisions this round adds are only reachable in a particular order:

1. **A**, standing still. Slot 7 is selected and slot 7 is the only full one, so
   the search walks the other eleven, wraps, and settles where it began —
   `item_unchanged` the long way round.
2. A zombie catches Zeke on the way down the west side; `$1CB8` goes 10 → 9.
3. **X**. This is the load-bearing press. `$80:EB2F` opens `LDA $1CB8,X : CMP
   #$000A : BEQ <rts>` — a first-aid kit refuses to be spent at full health, so
   pressing X before the hit does nothing at all, which is how `$1CB8` was
   identified as health rather than the countdown the symbol file used to call
   it. After the hit it works: heal, subtract one in BCD, and because that
   leaves zero, end `JMP $EAA8`. **That is the only way to make the item array
   empty**, and it takes `item_none_found` and stores `$FFFF` over the 7.
4. **A** again, now from that `$FFFF` — `item_none_held`, the branch nothing
   else can reach.
5. **Key 1** at (128,310), with `$FFFF` selected, so the pickup leaves through
   the auto-select tail: `item_autoselect`, and `$80:EAA8` a fourth time — from
   `$80:F903`, the third of the three call sites this movie reaches.
6. **Key 2** at (1214,694), with slot 0 now selected, so it leaves through the
   `RTS` two bytes earlier instead: `item_taken`.
7. **A** once more, for the full lap again.

Six of `$80:EAA8`'s decisions and both of `$80:F8D6`'s endings, and every one of
them is a consequence of where the kit was spent. A movie that did the same
seven things in a different order would take four of the eight.

The route itself came out of `zamn_headless --pos`, which prints where each
player actually is rather than showing you a window and letting you guess
(`docs/analysis-tools.md` → *Writing one*). Every lane named in the movie's
comments — the x=110 lane, the y=775 corridor, the gap at x=102, the gap between
the mud hole and the gravestone — is a number that tool printed.

#### What it caught

**73,024 of 73,024 calls, nothing declined**, and `item_select_next` is checked
on five calls from three of its four call sites — `$80:D278` (the A press) three
times, `$80:EB60` (the spent kit) once, `$80:F903` (the pickup) once. Only
`$80:EE7E` is unreached. Twelve deliberate perturbations,
**ten caught**:

| Perturbation | Caught at |
| --- | --- |
| amounts read from the weapon table | `$7E:1D0C`: ROM `$01`, port `$99` |
| base taken from `$64` instead of `$66` | `$7E:001E` — the APU sequence counter |
| auto-select tested `item == 0` instead of the sign | `$7E:001E` |
| the selection read from `$1CBC`, the weapon | `$7E:001E` |
| twelve tries instead of thirteen | `$7E:012E`: ROM `$0C`, port `$0B` |
| the wrap at `$001C`, the weapons' | `$7E:001E` |
| the search starting at the slot already held | `$7E:012E`: ROM `$01`, port `$0C` |
| the countdown never written back | `$7E:012E`: ROM `$0C`, port `$00` |
| the wrong sound effect id | the APU **bus** diff, not memory |
| N claimed set on the ordinary exit | flag N |

Four of those ten land on `$7E:001E`, and it is the same story every time: the
port took a different path through the selector, so it played one sound fewer
than the ROM, and the *audio sequence counter* is what says so. **The subsystem
that catches a wrong item search is the APU**, three rounds after it was ported
for unrelated reasons. Three more land on `$7E:012E` — offset `$2E` of the
player's page — which is the same address that caught the weapon search's
perturbations a round earlier, and is the evidence that the two searches share
one scratch field.

#### What it did not catch, and one of the two is the same shape as last round

**The slot arithmetic.** `index / 2 - ITEM_ID_FIRST` in place of `index -
ITEM_ID_FIRST * 2` passes all 73,024 calls, for exactly the reason the same
error passed in `player_pickup` before `movies/level1-pickups.zmv`: both keys
are id `$21`, whose slot is 0, and every wrong way of computing zero is also
zero.

**The difference is that this time there is no route.** Level 1 has one other id
that reaches `$80:F8D6` — `$28`, object 8, slot 7 — and it sits at (1208, 71),
on the far side of the fence along the top of the map. Walking under it stops at
y=129 and `$80:BEE1`'s box is sixteen pixels. So this is not "nobody has written
the movie yet", which is what the last round's version of it turned out to be;
it is a property of the level. The distinction matters, and it is the reason
both are written down beside the line in `src/port/collide.c` rather than
counted as coverage: **an index the diff cannot see is a statement about the
corpus, and sometimes the corpus is all there is.**

**The `$0099` ceiling**, which `item_pickup_capped` names by reading zero on
every movie. Level 1 pays `$01` twice. So does `item_digit_carry`: `$00 + $01`
and `$01 + $01` need no decimal adjust, and ninety-nine keys is not a route
either.

`run` compares **4,039 of 4,039** scheduler passes with at most 27 bytes
differing at once, all inside the stacks or a declared scratch byte.

The movie takes **43 of 99** sites and **holds nine of them alone** — more than
any other input in the project, and more than the other five put together
(`level1-2p-rescue.zmv` holds four, `level1-pickups.zmv` two,
`level1-rescue.zmv` one, and the two oldest hold none). The union across all six
is **70 of 99**.

### A call injected into code that is not running

`movies/level53.zmv` is the movie the password round was for, and what it reaches
is the routine this project has spent the longest time unable to look at.

`enemy_survived` had been untaken by every input ever written, and `$81:8506`
behind it was PROGRESS.md's "most interesting unported routine in the game" for
four rounds. Neither was waiting on somebody to write a movie. **Level 1 cannot
do it**: all fourteen actors its list places run `$81:87F8`, whose init is `LDA
#$0000 : STA $1E`, and the damage table's smallest non-zero entry is 1, so every
hit in level 1 is fatal by construction. Level 53's eleven actors all run
`$81:8C17`, whose init is `LDA #$0004 : STA $1E`, and the password that gets you
there is four letters (`docs/password.md`).

#### What the routine does

The scheduler parks a thread by saving its stack pointer at `W_THREAD_SP`.
`$81:8506` reads that pointer out of the table, moves it down three bytes,
slides the top three words down to meet it, and writes a 24-bit address into the
gap:

```
LDY $08 : LDA $0000,Y : AND #$0010 : BNE <rts>   ; already flashing? then nothing
LDA $000C,Y : TAX : LDA $11B0,X : PHA            ; the record names its own thread
DEC A : DEC A : DEC A : STA $11B0,X              ; and the thread's parked SP
PLX : TAY
LDA $0000,X : STA $0000,Y                        ; three words, three bytes down,
LDA $0002,X : STA $0002,Y                        ; lowest first because they
LDA $0004,X : STA $0004,Y                        ; overlap
LDA #$0081 : XBA : STA $0006,Y                   ; $81 at +7
LDA #$8542 : DEC A : STA $0005,Y                 ; $8541 at +5/+6
SEC : RTL
```

The next time the scheduler resumes that thread, the `RTL` it resumes through
returns into `$81:8542` instead — which sets `ACTOR_ATTR_SET` on the enemy's
display record, sleeps two ticks, clears it, and *then* returns into whatever the
enemy was actually doing, none the wiser. **The flash you see when you shoot
something that does not die is a call injected into code that is not running.**

That is also the answer to a coverage site this project carried untaken for six
rounds. `ACTOR_ATTR_SET` is `$0010`, `draw_attr_set` is `sprite_build_oam`
noticing it, and the reason nothing had ever taken it is that nothing had ever
survived being shot.

#### Why it turned out to be small

`docs/threads.md` has carried this routine as the hard question the port's
coroutines would eventually have to answer: the port parks a suspended routine as
plain copyable data with no machine stack, so there is nothing for a `JSL` frame
to be spliced into. That framing was right about the mechanism and wrong about
the work. **The splice is not a coroutine operation, it is twelve bytes of
WRAM** — and the thread being spliced into belongs to the ROM, because every
enemy body in the game is still the ROM's. What the port has to get right is
arithmetic on `W_THREAD_SP` and six stores, and `verify` compares every one of
them.

The open question that remains is the other direction — splicing into a thread
the *port* owns — and nothing in the game does that yet.

#### What it caught

**53,497 of 53,497 calls, nothing declined**, with twenty splices written
byte-identically. Eight deliberate perturbations, **seven caught**, and two of
them could only have been caught here:

| Perturbation | Caught at |
| --- | --- |
| the gap four bytes instead of three | `$7E:0F01`: ROM `$00`, port `$80` |
| the two overlapping stores in the other order | `$7E:0F08`: ROM `$85`, port `$00` |
| the return address not decremented for the `RTL` | `$7E:0F07`: ROM `$41`, port `$42` |
| the three words moved highest-first | `$7E:0F02`: ROM `$80`, port `$00` |
| `W_THREAD_SP` never moved | `$7E:11D2`: ROM `$02`, port `$05` |
| the thread slot read from the record's collision-id field | `$7E:0001` |
| carry claimed clear, so the thread is not parked | flag C, and `$7E:11A2` |

The two overlapping-store rows are the interesting ones. `LDA #$0081 : XBA : STA
$0006,Y` writes `$00` at +6 and `$81` at +7; `LDA #$8542 : DEC A : STA $0005,Y`
then writes `$41` at +5 and `$85` **over** the `$00` at +6. Doing those two in
the other order leaves `$00` where `$85` belongs, and the diff says so at the
byte. The same is true of the three-word move: the source and destination
overlap by three bytes, so copying highest-first destroys what has not been read
yet. Neither is a branch and no coverage mark could name either; both are caught
because the harness compares memory rather than control flow.

**Not caught: one, and it is the third of its kind.** Deleting the
already-flashing guard passes every call, because no input has ever landed a
second hit inside the two ticks a flash lasts — the weapon's own cooldown is
longer than the flash. `react_already` is the site that says so, and it joins
`victim_latched` and `object_spent` as an entry guard that is the whole design
of the routine it opens and that no diff can check.

`run` compares **3,689 of 3,689** scheduler passes with at most 31 bytes
differing at once, and this is worth one more sentence than usual: the routine
writes into a *stack*, and stacks are the one thing *What the diff forgives*
excuses. It excuses `$7E:1000-$7E:12FF`, and level 53's enemy threads park
around `$7E:0F00` — outside it. So the twenty splices are compared in both modes,
by accident of where the scheduler put those threads, and they agree.

#### And two long-standing gaps came off with it

`movies/level53.zmv` holds four sites alone, and only two of them are this
round's. The other two have been on the untaken list since long before it:
**`shot_expire_zero`** — the one of `shot_collide`'s three exits that runs no
`CMP` at all, so the only one whose carry is the caller's — and **`victim_latched`**,
which this document called the sharpest example in the project of what a diff
cannot see. Neither needed new code. Both needed a level that is not level 1.

The union across the eight movies is **75 of 101**, and twenty-six sites are
untaken by every input that exists.

### And how a thread gets its first one

`$81:8506` splices a call into a thread that already exists. `$80:825E` —
`thread_spawn` — manufactures the whole parked state of one that does not, and
the two are the same trick from opposite ends.

Everything the scheduler needs to resume a thread is nine bytes on that thread's
own stack, because `$80:8390` resumes one with

```
LDA $11B0,X : TCS : PLD : PLP : PLB : RTL
```

So a spawn writes a direct page at `sp+1`, a processor status of **zero** at
`sp+3` — native, 16-bit, decimal and interrupts all clear — a data bank at
`sp+4`, and a far return address at `sp+5` that lands on the entry point. Under
that, at `sp+8`, a second far return address to `$80:833E`, which is where a
thread body's own `RTL` goes to free the slot. **A brand-new thread and a thread
parked mid-`thread_yield` are the same nine bytes**; only the contents differ.

It writes them with `TCD` pointing the direct page *at the stack*, in six
overlapping stores, and then a seventh — `STA $01` — that lands the new thread's
direct page in the frame just before `TCD` switches to it. The last act is the
one that makes the routine an interface rather than an allocator: it copies the
**caller's first five direct-page words** onto the new thread's page, reading
them through the `D` the opening `PHD` saved (`LDA ($01,S),Y`). `$80:FA26` is
the example — fill `$00`, `$02`, `$04` with a position, then spawn.

**330 calls on `movies/level1-2p.zmv`, 13 on `level1.zmv`, 48 on `level53.zmv`,
all byte-identical.** Every shot either player fires is a spawn, so the sample
arrived for free the moment the routine was registered — the same shape as
`apu_send`.

The first run caught something on **call 1**, and it is the kind of thing only a
register diff catches: the ROM returns **Y = 8**, not the bank it was called
with. The argument copy ends `LDY #$0008 : LDA ($01,S),Y`, and nothing puts Y
back. WRAM matched perfectly; only Y did not.

Six perturbations, five caught: the slot search run upwards (`$7E:0100` on call
1 — a different slot means a different page), the `thread_exit` return address
not decremented for its `RTL` (`$7E:114D`: ROM `$3D`, port `$3E`), the two
overlapping frame stores in the other order (`$7E:114E`: ROM `$83`, port `$00`),
a new thread marked runnable this tick instead of the next (`$7E:11AE`), and
**four of the caller's five words copied instead of five — which fails on call
10, not call 1**, because the fifth word is only sometimes non-zero.

Not caught: dropping the two stores that clear the slot's handler. Every slot
handed out already has a zero there, so the diff cannot tell those stores from
no-ops. Written down beside the line, like the `STZ $7E` in `enemy_die`.

Under `run` the port now *creates* the game's threads — 330 of them on one movie
— and no byte of live game state differs on any of the eight.

## Three of the five transcribed entries, and the one that was wrong

The five jump-table entries ported a round ago went in **ahead of any input that
reached them**, which was a first for this project and was recorded here as the
thing the coverage report was keeping honest: `apu_play_sfx`, `thread_spawn` and
`score_add` underneath them were diffed on thousands of calls, but their own
half-dozen stores were transcribed from the listing and never run. Three of the
five now have an input.

| entry | id | movie | what it does |
| --- | --- | --- | --- |
| `$80:FA4A` | `$2E` | `movies/level45-bonus.zmv` | kind 1, and a counter that stops at five |
| `$80:FA79` | `$2F` | `movies/level53-bonus.zmv` | kind 2, and `$0500` of score |
| `$80:FACF` | `$27` | `movies/level29-firstaid.zmv` | three health back |

**The first call of the first one failed.** `$80:FA4A` ends

```
LDX $0E : LDA $1D4C,X : CMP #$0005 : BCS $FA78 : INC A : STA $1D4C,X
```

and **the `LDX` is an output as well as an index**. The port had been leaving X
as whatever `thread_spawn` returned four instructions earlier, so the diff read
`X: ROM $0000, port $0022` — every byte of all 128 KB matching, and one register
not. That is the same shape as `thread_spawn`'s own first run, and it is the
argument for registering these things rather than eyeballing them: nothing about
the routine's *memory* was wrong. `$80:FA26` had the identical error and still
has no input; it was fixed by reading the listing again, which is worth
distinguishing from fixing it by diffing.

### What `$82:E0B4` turned out to be

All four spawn entries end `LDA #$E0B4 : LDY #$0082 : JSL thread_spawn`, and the
body was left unported with a note that the port owns only the three words handed
to it. It is **the thing you just picked up, flying away**: a display record at
the position it was given, no collision id, a metasprite from a four-word table
at `$82:E147` indexed by the kind, one of four diagonals chosen by `$80:9D39`'s
random number, eight pixels a tick for 21 ticks, then `actor_slot_free`. The four
metasprites — `$8F:DCAA`, `$DCB3`, `$DCBC`, `$DCC5` — are the last four entries of
`$80:CA6C`, the object-type table, so the sprite that flies off is the object's
own. That is why the kind is worth passing: it says which of the four bonus
objects this was.

It is also how a pickup is legible in a `--records` dump without running the
harness at all. A bonus object's record disappears and an id-less record starts
drifting diagonally away from where it was.

### An object is contested, and the display list decides

`$80:CAEE`'s three accepting ids are `$0005`, `$0006` and `$0004`. Two are the
players. **The third is the monster side** — its entry in the player's own jump
table is `$80:F950`, the hit path — and it takes objects out from under you.

Level 45's `$80:FAA4` object at (230,1044) was lost to one three times, on three
different routes. `--records` shows the moment:

```
  frame 3790 — display list, score 00000000 / 00000000
    $1AB6  $8001    222  1050   $05   $00     <- the player
    $1A7A  $8011    230  1050   $04   $24     <- a monster, on the same object
    $1AA2  $8001    230  1044   $30   $26     <- the object
```

Both are inside the box (`$80:BEF1` passes `other - self + 8 < 16` on each axis).
`actor_overlap_pass` walks pairs from the end of the display list, so the one the
depth sort put later is asked first; `$80:CAEE` clears the object's
`ACTOR_COLLIDE_ID` on that first ask; and `actor_collide_notify` re-reads the
object's id when it comes round to the loser. The player's handler is therefore
called with `Y = $0000`, which is `$80:F87A`, a bare `RTS`. The report says this
from both ends in the same run: `object_spent` 3, `player_no_effect` 3.

So **a route to an object is a race**, and the fix is speed rather than accuracy
— which is also the one-line summary of the change to `tools/fit_route.py`. Its
arrival test was three pixels; the game's is eight. Widening it to seven took
level 45's route from 56 legs to 16 — and to 14 once the stalls went, below — and
level 45's other bonus object from a
chained detour to twelve legs. `movies/level45-bonus.zmv` is the direct route,
and it wins its race.

### Firing is free to a closed loop

`tools/fit_route.py --fire` holds Y down the whole way. The reason it is worth a
flag rather than a hand edit is the reason lane snapping was: **re-planning after
every leg measures where the player *is*,** so a shot that changes the board just
changes the next search's starting point. Patching `+Y` onto a finished movie
does not work and was tried — the trajectory diverges inside a leg or two and
every turn after it is aimed at the wrong place.

What it bought first was not the contested object. It was **`heal_capped`**, the
`$80:FACF` path where three health would overshoot the ceiling of ten.
`movies/level29-firstaid.zmv` walks to the first-aid object and takes nineteen
hits on the way, arriving on six or less, so the entry adds three and stores it.
`movies/level29-fighting.zmv` is the same target with Y held: fifteen hits,
arriving on seven, eight or nine, and the entry clamps. Two routes to one object,
and the difference between them is a branch.

It is also the highest-coverage single-player input in the corpus at **51 of
111**, against 44 for the walking version, because firing turns on a subsystem
the walking routes never touch: `enemy_collide` runs 47 times there and zero
here. Both movies are kept — the walking one holds `victim_event_4`,
`victim_ignore` and `victim_latched`, which nothing else reaches, because a
player who shoots his way across level 29 meets fewer neighbours than one who
walks into them.

### The contested object, and the 263 frames nobody was spending

Five routes reached level 45's `$80:FAA4` object at (230,1044) or its corridor and
every one found it already gone. The `--fire` route is the one that explained why:
the record vanishes at **frame 3466**, 936 frames into gameplay, with the player
still at (346,1158) — 434 pixels of walking away. **The monster that took it was
never near him**, so shooting cannot help. It is not a race lost at the object; it
is a deadline.

The first attempt to price that deadline was wrong, and the correction is the
finding. It looked like a route problem — 226 cells is about 1,800 pixels, the
player moves 2 pixels a frame, so the journey costs 904 frames at best against a
fitted 1,182 — and the 280-frame gap was written up here as lane-snap detours.
Then it was measured instead of assumed. `zamn_headless --pos` every frame, summed:

```
total path travelled: 1838 px      (optimum 1808)
frames moving: 919   frames standing still: 263
```

**Thirty pixels of detour and 263 frames of standing still.** The stalls came in
blocks of 103, 43 and 43, each one ending exactly on a leg line, because a leg
whose target the player never reached ran to the end of its padded replay window
before the fitter re-planned. `tools/fit_route.py` now ends such a leg where the
player *stopped* — eight frames without movement means he has arrived or hit a
wall, and both mean re-plan now.

That is worth **151 frames** — and it is still not enough on its own, which is the
part worth writing down. The stall fix *without* `--fire` arrives at frame **3489**,
earlier than the movie that wins, and loses anyway: at 3460 the object is live, a
monster is standing on it at (222,1046), the player is forty pixels below at
(222,1086), and by 3470 it is gone. He watches it happen from inside the corridor.

**So the clock is necessary and not sufficient.** The two changes together take it,
and the difference is visible in the display list rather than in the arithmetic. At
frame 3550 of `movies/level45-race.zmv` the object is still at (230,1044), the
player is eight pixels below it at (222,1052) — inside the box — and the two id-`$04`
monsters nearby are at (234,1052) and (244,1052), *beside him rather than on it*,
with one of his own shots at (222,1028). Shooting keeps the thief interested in the
player instead of the object.

One word of WRAM proves it landed — `score 00001000` at frame 3600, which is what
`$80:FAA4` awards and nothing else in the game does. `player_spawn_3` is taken,
76,127 calls check clean, and **four of the five transcribed entries are now diffed
rather than read**.

It is also the only movie that takes all three of `$80:CAEE`'s exits at once:
`object_taken` 19, `object_spent` 2, `object_ignore` 196. A firing route puts
bullets over objects, walks over one a monster claimed first, and collects three of
its own.

Two things learned on the way that the winning route does not show. A route that
approaches along the lane *above* the object — so the player sorts later than it
and is asked first — loses anyway, because the monster matches his row exactly: at
frame 3842 the player is at (222,1038) and the monster at (230,1038), tied on the
sort key, and the tie went to the monster. And level 45's spiders **pick Zeke up
and carry him**, which is where `movies/level45-carried.zmv` comes from.

### `$80:FA26` is not waiting for a movie. There isn't one.

The last of the five looked like the same kind of work-list entry as the others —
"no route yet" — and it is a different kind, which took three checks to establish
and is worth the space because a transcribed routine that *can never be diffed* is
a permanent hole rather than a scheduling problem.

**Its objects are on islands.** Collision id `$2D` is object type `$34`, and type
`$34` appears in exactly three level object lists: level 9 (547,72), level 17
(716,77), level 33 (365,177). Routing to any of them fails, and so does routing to
**all 225 positions in the ±7 collision box around each** — which is the thing that
matters, because two records touch when they are within eight pixels and the player
never has to stand on an object to take it. Then the decisive probe: `route` from
one of these objects *to itself* answers "1 cells", so the object's own cell is
walkable — and sampling the whole of level 33 on a 32-pixel grid from that cell
reaches **zero** of 1,680 points. It is a one-cell island.

That predicate is the game's own and was re-checked rather than trusted.
`$80:AE1F` computes the column as `LSR A : LSR A : AND #$FFFE`, which is
`(x/8)*2` — pre-doubled, because the expanded map is one *word* per cell — so the
`LDY #$0002` and `#$0004` that follow are byte offsets naming columns c+1 and c+2,
not c+2 and c+4. The footprint is a contiguous 3x2 block of cells, 24 px by 16,
which is exactly what `route_open` implements.

**And nothing drops one.** Objects can also be placed at runtime: `$82:DC57` rolls
`$80:9D39` and indexes a 256-byte table at `$81:E79F`, and anything that is not
`$FF` or `$FE` goes to `$80:C97F`, which writes a type into the object array. The
whole table holds 22 distinct types. Type `$34` is not one of them — `$38` and
`$3A` each appear six times, which is how `$80:FA79` and `$80:FAA4` could have
arrived by luck, and `$34` appears zero. The trace's WRAM map confirms those are
the only two writers: `$7E:1F0A` has writer `$80:C9C3` and reader `$80:CA0A`.

**The one lead that looked like a way in was a mislabelled column.**
`zamn_assets actors` printed the actor placement byte as `id`, and fourteen
placements across the game carry `$2D` — four of them in password levels, three
routable, one only 72 cells from level 29's start. They are actor *types*.
`--records` settles it: the record at level 29's (290,1302) carries
`ACTOR_COLLIDE_ID` **$00** where the list says `$2D`, and level 46's monsters carry
`$03` and `$04`, neither of which appears anywhere in that level's actor list. An
actor's collision id is written by its own body, not by its placement. The tool
says `type` now.

So `$80:FA26` stays transcribed, and the coverage report keeps saying so:
`player_spawn_0` is untaken and will stay untaken. That is the honest end of it —
five routines were ported ahead of their inputs, four now have one, and the fifth
has no input to have.

### All three exits of `$80:CAEE`

`object_collide` has three and every one of them arrived this round, each needing
a different kind of input.

**`object_taken`** is any of the three accepting ids. **`object_spent`** is an
object whose id was cleared earlier in the same overlap pass — the losing half of
the race above. **`object_ignore`** is the one that had no witness at all: an id
that is neither a player nor a monster, touching an object and doing nothing to
it. Two things in the game are that, and this round produced both.

The mundane one is a **shot in flight**, id `$5C`. `movies/level29-fighting.zmv`
gets it for free, because `--fire` is the first thing in the corpus that has ever
put a bullet over an object.

The other is a **player who is being carried**. `movies/level45-carried.zmv`
chains two bonus objects and collects neither, because level 45's spiders pick
Zeke up: his display record leaves the list, and when it comes back its
`ACTOR_COLLIDE_ID` reads `$38` instead of `$05`. He then walks over an object with
both records live, both ids non-zero and different, the pair well inside the box —
and `$80:CAEE` runs all three `CMP`s, matches none, and returns carry clear having
written nothing. Forty-eight times.

That second one is not needed for the coverage, and it is kept anyway: it is the
only input that shows the *player* on the wrong side of that branch, which is a
different fact from a bullet being on it.

### Where the totals stand now

Fifteen movies. The eight that existed before this round check the same call
counts they did — **40,252** on `level1.zmv` at 2400 frames, **130,092** on
`level1-rescue.zmv` at 6100, **150,500** on `level1-2p.zmv` at 6000, **162,892**
on `level1-2p-rescue.zmv` at 6100, **41,253** on `level1-pickups.zmv` at 2400,
**73,072** on `level1-keys.zmv` at 4050, **48,071** on `level33.zmv` at 3600 and
**53,545** on `level53.zmv` at 3700 — which is what says the `player_collide`
change regressed nothing. (Those frame counts are recorded here for the first
time; every total quoted in earlier rounds is at these.) The seven new ones add
**49,098** (`level53-bonus`, 3300 frames), **65,448** (`level29-firstaid`, 4420),
**67,530** (`level29-fighting`, 4250), **54,342** (`level45-bonus`, 3400),
**69,768** (`level45-contested`, 3810), **95,261** (`level45-carried`, 4400) and
**76,127** (`level45-race`, 3600). No routine diverges anywhere on any of the
fifteen.

`run` substitutes over **3,289 / 4,409 / 4,239 / 3,389 / 3,799 / 4,389 / 3,589**
scheduler passes on the seven, at most **46 / 21 / 24 / 13 / 19 / 19 / 28** bytes
differing at once, every one inside the stacks or a declared scratch byte, and no
byte of live game state ever differing.

**Branch coverage: 89 of 111, up from 76.** Thirteen sites came in with the seven
movies — `player_spawn_1`, `player_spawn_2`, `player_spawn_3`,
`player_heal_entry`, `heal_capped`, `score_digit_carry`, `victim_event_4`,
`victim_ignore`, `draw_no_meta`, `object_spent`, `object_ignore`, and the two
decline sites `handler_unported` and `collide_unported`, which had gone quiet when
the last handler level 1 dispatches to was ported and which three unmapped levels
put straight back. Twenty-two are untaken by every input that exists, down from
thirty-five.

Five of the fifteen are not load-bearing for that number, which is worth stating
rather than hiding. `level1`, `level1-rescue`, `level1-2p` and `level33` are
baselines whose call totals *are* the regression. `level45-contested` and
`level45-carried` were the only witnesses to `object_spent` and `object_ignore`
until `level45-race` collected the object and took both on the way past; they are
kept because each is the only evidence for something the docs claim — the
monster-tie dump at frame 3790, and a *player* rather than a bullet on
`object_ignore`. Every other movie holds at least one site alone; `level1-keys`
holds seven.

`draw_no_meta` is the odd one on that list and worth a sentence: it is
`sprite_build_oam`'s guard against a drawable record whose metasprite pointer is
not in cartridge ROM, it has read zero since the sprite pass was ported, and
`movies/level29-firstaid.zmv` takes it 151 times. Level 29 puts records on the
display list that are marked drawable and point at nothing.

## `$81:C4A6`: the routine this round kept describing

Every finding above is something a monster did — took an object out from under
the player, matched his row on the sort key, carried him. **`$81:C4A6` is the
code that did it**, and the census named it the moment the level-45 movies
existed: **2,039 declines**, more than everything else on the list put together.
`$81:C3B6` installs it, and `$81:C3B6` is the body of level 46's type-`$14`
actor — ten of that level's twenty placements, the giant spider.

It is a **second copy of the enemy subsystem**, not a variant of the first. The
same damage table at `$81:8561`, the same subtract-and-check-the-sign shape, and
each routine it leans on has a ported twin: `$81:BBEB` is `$81:8727` again at
three times the award, `$81:BAB3` and `$81:BB05` are `$81:8506` again. What it
does not share is the page — health is `$22` here and `$1E` there, which is the
lesson `$1E` itself taught one page further out.

Four ways out of its dispatch, and two of them write nothing:

| id | what happens |
| --- | --- |
| `< $0C` | `CLC : RTL`. A player standing on it, every frame — 1,129 of level 45's 1,138 calls |
| `$0C..$32` | **takes the object** |
| `$33..$5B` | a second, separate `CLC : RTL` |
| `>= $5C` | a weapon shot: park the id, mask it, subtract the damage |

**The object branch is the theft, in three instructions.** `LDY $08 : LDA #$0003
: STA $000E,Y` rewrites its own display record's `ACTOR_COLLIDE_ID` — which is
exactly the `$04` -> `$03` transition `--records` caught at frames 3466 and 3790
when a bonus object vanished. Reverse-engineering from a display-list dump and
reading the disassembly arrived at the same three instructions from opposite
ends.

Two of the branch's details are worth keeping. It is **latched** in
`victim_collide`'s sense — `LDA $26 : BNE` refuses outright if anything already
happened to this monster, so the first object it touches is the only one. And
`LDA $0042 : CMP #$0004 : BNE : LDA $0046` reads two **absolute** globals, not
direct-page fields: `AD 42 00` is `$7E:0042`. Every other field the routine
touches is on its page, and reading these as offsets would have put a plausible
wrong value in the latch.

### What it caught

**175 of 186 on the first run**, the eleven declines all the survive path. Then
`movies/level45-carried.zmv` — the only movie in the corpus where one of these
dies — failed a single call: **`A: ROM $0000, port $0300`**.

`$81:BBEB` ends `LDA $20 : AND #$8000 : ASL A : ROL A : ROL A : TAX : INC
$1FD4,X`, and the port had been returning what `score_add` left in A. Nothing
`score_add` returns survives: `LDA $20` reloads the parked id over it and the
three shifts reduce that to the side, 0 or 2. One death in fifteen movies, and it
was enough.

Six perturbations, five caught, each on the movie chosen for it: writing `$0004`
instead of `$0003` into the record (`$7E:1A4C`, ROM `$03` against port `$04` —
the display record itself); dropping the latch guard (`$7E:0312`); never taking
the `$46` redirect (`$7E:0726`, and `monster_latch_alt` fires exactly once in the
whole corpus, in `movies/level45-bonus.zmv`); reading health from `$1E` rather
than `$22` (`$7E:0622`, ROM `$0F` against port `$FD` — a monster with negative
health); and skipping the `DEC $2A` countdown (`$7E:062A`).

**Not caught: collapsing the two ignore exits into one.** They leave different
flags — one comparison borrowed and the other did not — but the two spellings can
only disagree on `arg == $33`, and every other id in `[$33,$5C)` is positive and
non-zero either way. `$80:CA30`'s thirty entries stop at `$30`, so nothing in the
game carries `$33`. Not a branch, so no mark can express it; written down beside
the line, like the `STZ $7E` in `enemy_die`.

### Where that leaves the numbers

Twenty-five routines. All fifteen movies verify with **no divergence anywhere**,
and the four level-45 totals grow by exactly the calls the new interception adds:
**54,478**, **70,347**, **96,395** and **76,302**. `run` substitutes it too —
3,589 and 4,389 scheduler passes, at most 28 and 21 bytes differing, all inside
the stacks or a declared scratch byte.

Twelve new coverage sites, nine of them taken.

### And then the survive path, which was `$81:8506` with different numbers

The census named `$81:BAB3` the moment `monster_collide` was registered, and it
was right about what it is: **`$81:8506` instruction for instruction** — read the
thread's parked stack pointer out of `W_THREAD_SP`, move it down three bytes,
slide the top three words down lowest-first, and write a far return address into
the gap so the scheduler resumes through a reaction it never called.
`ENEMY_REACT_FRAME` is shared rather than re-spelled.

Three differences, all at the edges:

* **The guard reads a different field with a different test.** `$81:8506` is
  `LDA $0000,Y : AND #$0010` — bit 4 of the record's flags, `ACTOR_ATTR_SET`.
  This is `LDA $0010,Y : BNE` — the whole of `ACTOR_ATTR`, the word that bit
  selects. The same question asked of the answer rather than of the permission,
  and the diff says the distinction is real: spelling it the twin's way fails at
  `$7E:0D82`, ROM `$80` against port `$83`.
* **What comes back on that path is data, not a constant.** The twin can hand
  back `ACTOR_ATTR_SET` because that is what its `AND` left; this hands back
  whatever was in the field.
* The address spliced in is `$81:BAEC`, which writes `$0C00` into `ACTOR_ATTR`,
  sleeps two ticks and clears it — where the twin sets and clears a bit.

Five perturbations, four caught: the guard field, the three words moved
highest-first (`$7E:0D82`, ROM `$80` against port `$01` — the move overlaps its
own source), the return address not decremented for the `RTL` (`$7E:0D87`, ROM
`$EB` against port `$EC`), and the two overlapping stores swapped (`$7E:0D88`).
**Not caught: dropping the already-reacting guard** — which is exactly the twin's
blind spot. `react_already` has never been taken and neither has
`monster_react_already`; an entry guard that only ever reads zero is a thing no
diff can check, and both are now recorded as such.

### Where that leaves the numbers

**Twenty-five routines, and the four level-45 movies print no census section at
all** — `monster_collide` serves every call it is offered, 186 of 186 and 1,138
of 1,138. All fifteen movies verify with no divergence; the level-45 totals are
**54,478**, **70,347**, **96,399** and **76,313**. `run` substitutes it over 3,589
and 4,389 scheduler passes, at most 28 and 21 bytes differing, all inside the
stacks or a declared scratch byte.

Fourteen new coverage sites, eleven taken. **Branch coverage is 100 of 125**, 25
untaken — four of them in this family: `monster_special` (id `$5D`, the other
splice, and the only decline left in these movies), `monster_fatal_id` (id `$5E`,
which dies without subtracting anything), `monster_no_damage`, and
`monster_react_already`.

The census that remains is level 29's: `$81:B41C` (163), `$81:CDDE` (112) and
`$81:B592` (5). Three more handlers, on a level whose actors are a third kind
again.

## A routine that was checked and never called

`monster_collide` above is the case that says why "the port passes every call"
and "the port serves every call" are different sentences, and it took four
rounds and a corpus runner to notice they had come apart.

It was registered on its own entry PC for the reason `actor_collide_notify` and
`player_collide` are: an enclosing routine that declines a dispatch never offers
its callee anything, so a handler seen only through `thread_call_handler` would
be checked on none of the calls that go somewhere else. Registered directly, it
got all 1,138 of them and passed.

**And `thread_call_handler`'s own dispatch list never had it.** The port's
dispatcher is a chain of `entry == …_COLLIDE_ENTRY` tests and `$81:C4A6` was not
among them, so every dispatch to the giant spider fell through to
`handler_unported` and declined — 1,351 of 2,800 calls on
`movies/level45-carried.zmv`, in the same run where the routine itself reported
1,351 checked and 0 declined.

Two mechanisms hid it, and both are ones this document argues for elsewhere:

* **The census excludes the handlers the port has**, by address, so that a
  decline *inside* `player_collide` names the jump-table entry rather than the
  door it came through. `$81:C4A6` was on that exclusion list — correctly, as a
  handler the port has — while not being on the dispatcher's. So the declines
  were counted in the `decl.` column and never given a name, which is exactly
  the state the census exists to prevent.
* **A decline is not a failure.** Nothing goes red when the ROM runs a routine
  itself; that is the whole design. The `decl.` column had been non-zero on the
  level-45 movies for four rounds and read as ordinary.

What found it was summing the columns across every movie at once
(`tools/verify_corpus.ps1`) and asking why four movies declined thousands of
calls while printing no census. Wiring the entry in takes
`thread_call_handler` to 2,800 of 2,800 and the level-45 movies to no declines
at all.

The lesson is narrow and worth keeping: **a routine's own verdict says nothing
about whether its callers reach it.** `verify` answers "does the port compute
what the ROM computes"; only the `decl.` column, read against the census, answers
"and does the port ever get asked".

## A harness has no harness

The instrument that says when the port is wrong is checked by nothing, and the
one time that mattered its failure mode was to keep running and measure nothing.

Registering the **thirty-third** routine made `verify` print an empty routine
table and `0 calls checked` on every movie in the corpus. Not a divergence — a
divergence is loud and names a byte. Not a decline either, which would at least
have shown up in a column. `Cosim::enabled` was a `uint32_t` bitmask over the
registry and `Cosim::stats` a fixed `CosimStat[32]`, so the 33rd routine shifted
by 32 (undefined behaviour) and indexed one past the end of an array, and what
came out the other side was a harness that examined nothing and said so only by
the absence of numbers.

Three things about it are worth keeping.

**It was a limit nobody had written down as a limit.** `cosim_enable_all` even
had `count >= 32 ? 0xffffffffu : …` — a line that had already noticed the
boundary and handled it by clamping, which is exactly the shape of a bug that
waits. Both are 64 now, and `cosim_init` **exits** rather than clamping:

```
error: 65 routines registered but COSIM_MAX_ROUTINES is 64 — widen
       Cosim::enabled and Cosim::stats together, in cosim.h.
```

**Nothing measured before it is affected**, and that is checkable rather than
hopeful: the break is at 33 and every total recorded in `PROGRESS.md` was taken
at 31 routines or fewer. The corpus was re-run at 34 and again at 37 after the
fix.

**The general form is the interesting part.** This document argues throughout
that an unclaimed output is an unchecked output and an untaken branch is an
unverified branch. The same rule applies one level out: *a harness that reports
nothing is indistinguishable from a harness with nothing to report*, and neither
`verify` nor `run` had a way to tell those apart. `tools/verify_corpus.ps1` is
what surfaced it — twenty-five movies printing `0` in the same column is a
pattern one movie cannot show — and the guard above is what makes the next one
say so directly.

## Where `run`'s clock runs out

`run` returns a substituted routine on a **fixed cycle budget** — the mean
`verify` measured — and every number in this document until now was measured on
a movie where that was close enough. `movies/level25.zmv` is the first one where
it is not.

The symptom is unambiguous and it is not a wrong answer. At pass 4371 the NMI
frame counter at `$7E:0016` reads `$0F64` on the stock side and `$0F2F` on the
native one: **fifty-three frames apart**. Everything else that differs is
downstream of that — 1,910 bytes the stack rule cannot account for, in timers
and scratch that are simply further along on one side than the other. The
substituted frontend says it from the other end, and more legibly: `zamn` and
`zamn_headless` produce **identical framebuffers for 3,950 frames of level 25
and differ from 3,980**.

Three things keep this in proportion.

**It is not any one routine's bug.** Taking the level-25 boss handler back out of
the dispatcher — so it declines exactly as it did before it was ported — makes
`run` fail *worse* (2,148 bytes unaccounted against 1,910) and leaves the
frontend diverging at the same frame. Porting a routine on this level moves the
budget closer to the truth, not further from it.

**It is the busiest level anyone has run.** `sprite_build_oam`'s measured cost
spans 5,864 to 125,014 cycles across the corpus, and a single budget cannot be
right for both ends of that. Every other movie stays inside the tolerance
because its actor counts stay in the middle of the range.

**`verify` is untouched by it.** That mode never substitutes anything: the ROM
drives, the port is replayed over a snapshot, and the comparison is of results
rather than of two clocks. Level 25 verifies 86,023 of 86,023. The failure is
`run`'s, and `run` is the weaker of the two instruments by construction —
see *What the diff forgives, and why*.

What would fix it is a budget that is a function of the call rather than a
constant, which is a change to `CosimRoutine` and a round of its own. What it
does **not** threaten is Phase 4: a finished port owns its own main loop and does
not have to impersonate an instruction stream on somebody else's clock.

## The generator, and a flag the per-call diff could not see

`$81:D301` was the last address in the census — three declines on level 9, the
only thing in twenty-five movies the ROM still had to run. Porting it needed
`$80:9D39` first, because the branch a survivor takes rolls a random number, and
that turned out to be the more interesting half.

### `$80:9D39` is twenty-two bytes and its carry is an argument

```
SEP #$20 : LDA $0024 : ROL $0024 : EOR $0024 : ROR $0024
INC $0025 : ADC $0025 : BVC +3 : INC $0025
STA $0024 : REP #$20 : AND #$00FF : RTL
```

An eight-bit shift register at `$0024`, stirred by a counter at `$0025`, with the
answer zero-extended to a word. PLAN.md lists "faithful RNG replication" as one
of the project's three top risks; the routine that carries it is smaller than
`enemy_collide`'s ignore path.

Three things about it are not obvious from the listing.

**The caller's carry is an input.** `LDA` does not touch carry, so the `ROL`
shifts in whatever flag the caller arrived with. That is not a rounding error to
be tidied away: `enemy_d301_collide` reaches the generator on two paths whose
carry differs — `enemy_survived_react` returns carry **set** from the splice and
**clear** from the already-flashing guard — so the same creature draws from a
different sequence depending on which one it took.

**It is a byte generator.** `SEP #$20` makes the state, the arithmetic and the
answer eight bits, and the `AND #$00FF` on the way out discards the accumulator's
hidden high half. So the caller's A does not leak into the result, and every
caller in the game reads it as 0..255 — `CMP #$0019`, `CMP #$0087`, `AND #$0003`.

**It was worth registering rather than inlining**, for `score_add`'s reason: one
actor body alone calls it from `$81:D04A`, `$81:D069` and `$81:D08B`, none of
which is ported. `verify` offers the port **1,868 calls on level 9 and 3,722 on
`movies/level45-carried.zmv`**, all of them from code nobody has written yet.

### And then `run` found the flag `verify` was not looking at

With the generator substituted, `verify` passed every call on every movie and
`run` diverged — by **one byte, on two movies out of twenty-five**:

```
$7E:0DE8  stock $40, native $00   *** unexplained ***
$7E:0D58  stock $41, native $01   *** unexplained ***   (level 29)
```

`$40` is bit 6 of `P`, which is **V**, and both addresses are inside a thread's
stack. The `ADC` sets overflow; the port did not model it, so `native_publish`
left the core's V as the caller's; and then `thread_yield`'s `PHP` parked the
whole status byte on the suspended thread's stack, where the whole-program diff
compares it as memory.

**The asymmetry is the finding.** Flags are opt-in in this harness precisely so
that a claim stays as strong as its evidence — and the cost of that design is
that a flag nobody claims is a flag `verify` never compares. Thirty-eight
routines had lived with N/Z/C because no caller of any of them branches on
overflow; no caller of *this* one does either. What made V observable is not a
branch, it is a suspension. So:

* the per-call diff cannot see an unclaimed output **by construction**;
* the whole-program diff can see it **by accident**, and only when the value
  happens to be written to memory;
* running both is what closed the gap, and `COSIM_FLAG_V` is now part of the
  same opt-in mask as the other three, claimed by exactly one shim.

With V published, level 9 and level 29 are clean again and `verify` checks the
overflow flag on every one of the generator's calls — which is a stronger
statement than the one that was true before the bug existed.

### `$81:D301` itself, and a counter that finally has a reader

The eighth copy of `$81:8888`, and the one worth having. Four earlier copies end
a death with a bare decrement of a word on their own page instead of paying an
award, and every one of those words is named for where it lives —
`B41C_DP_COUNTER_0A`, `D9B6B_DP_COUNTER_26`, `D9063_DP_COUNTER_2E` — because
nothing in reach reads them. Here the reader is eleven instructions away:

```
$81:D2AD  LDA $0C : BEQ <loop top>   ; zero: carry on
          BPL $D2CE                  ; positive: leave, quietly
          ; negative: LDX #$0200 : LDA $36 : BEQ : JSL score_add : ...
```

So `$0C` is a **three-way verdict** the handler writes and the body reads on its
next pass, and the award this family "does not pay" is paid — `$0200` of it — by
the actor's own main loop, out of the id the handler parked at `$36`. It is
evidence about the other four rather than proof: different pages, different
words, and `$81:B41C`'s has a second writer this one does not.

Two more things it does that no sibling does. **It is immune to the ordinary
weapon** — after masking, `CMP #$005C : BEQ` sends the player's basic shot, which
is every hit anywhere in the corpus, straight to a `CLC : RTL` — and **`$FF` is
how it is told to stop**, tested ahead of the family's `CMP #$005C` and answered
with `INC $0C`, the positive verdict. That is the same id `actor_deeb_collide`
latches on and the one `victim_a264_collide` calls `A264_ID_GIVE_UP_FF`: three
unrelated actors reading `$FF` as "you are done here".

**And that one id is all the corpus checks.** All three calls level 9 makes carry
it. One branch of eight is diffed and seven are transcribed, which is the honest
counterweight to the paragraphs above.

### A list that wanted to be a field

`guard_thread_call_handler` censuses the address a declined dispatch went to, and
it has always had to exclude the handlers that decline *internally* — an id
`enemy_collide` hands back is a decline one level down, and naming the door it
came through would put a routine the port already has at the top of the work
list. That exclusion was a list of four addresses, written when four handlers
could decline. By this round eight could.

Nothing would have reported the omission. The symptom is a census line naming a
routine the port already has — which is the same shape as the bug that hid
`monster_collide`'s missing dispatch for four rounds, and just as quiet.
`thread_call_handler` now hands the address back in `ThreadCallResult::unported`,
set only on the branch that does not recognise the handler at all, so the guard
has one thing to test instead of a list to keep in step.

### A movie that adds no coverage and proves something anyway

`movies/level25-2p.zmv` is the input this document's *Coverage the movie does not
have* section has been asking for since the level-45 round. `monster_death_award`
ends `LDA $20 : AND #$8000 : ASL A : ROL A : ROL A : TAX : INC $1FD4,X`, a
counter indexed by the side that landed the blow **already doubled**, and writing
`1` there instead of `2` passed every call on every input for two rounds: every
death in the corpus was player one's, and both spellings of zero are zero.

The movie is level 25's password prefix with four Start presses on port 2 in the
same player-select window, and two box walks mirrored so the players cover
different ground. Julie does most of the killing — `score_slot_0` 6 against
`score_slot_1` 18, and 00000300 / 00000900 on the two slots — and the
perturbation now fails at **`$7E:1FD5`**, the byte *between* the two counters,
which is exactly where an undoubled index lands.

**The branch-coverage union does not move.** It is 139 of 219 with this movie and
139 without: all 68 sites it takes were already taken by some other input, even
though 68 is the highest any single input reaches. That is not a disappointment,
it is the shape of the thing being measured. **A coverage table counts decisions
the code makes, and the doubled index is not a decision** — it is a value that has
only ever flowed through one branch one way. The port's two instruments answer
"did the port agree with the ROM on the calls this movie made" and "which
branches did any movie reach", and neither of them asks *which values* reached a
branch. This is the second finding of that kind, after `enemy_cdde_collide`'s
already-zero `STZ $22`, and the fix for both is the same: a second input that
carries a different number down a path something has already walked.

## The second weapon, and what `STZ $7E` was for

Every input in this project fired the same gun. `player_collide`'s table, eight
copies of `$81:8888`, `shot_collide` and `ENEMY_DAMAGE_TABLE` between them
recognise more than twenty weapon ids, and **every shot in every movie carried
`$5C`** — which is why `enemy_hit_special`, the branch each copy of the enemy
subsystem keeps for ids `$5D` and `$5E`, had read zero since the day it was
written.

It cost four moves to fix. `zamn_assets actors <rom> 18` lists an object of type
`$02` at (220,431) on level 17, sixty-four pixels from where the player starts;
`$80:CA30` says type `$02` is collision id `$0D`, which is inventory slot 1; and
B cycles the selection (`$80:EA63`). `zamn_assets route` walks there in ten cells
— Left is blocked at x=242, so it is Left, Down, Left — and from then on
`--records` shows every shot on screen reading `$5D` instead of `$5C`.

### The routine behind it, and the store six rounds could not check

`$81:847E` is `enemy_survived_react` a third time, with something in front of it:

```
INC $7E : LDA $7E : CMP #$0005 : BCS act : <CLC : RTL>
act:  LDX $08 : LDA $0000,X : AND #$0010 : BNE <CLC : RTL>
      TYA : ASL A : AND #$0000 : ROL A : ROL A   ; bit 15 of Y -> side
      TXY : JSL $80:9D6A                          ; side -> score slot
      INC $1FE0,X
      <the three-word splice, resuming at $81:84D6>
      SEC : RTL
```

**`$7E` is the word every death path in this file clears.** Eight copies of
`$81:8888` end with `STZ $7E`, and every one of them carries a comment saying the
store is transcribed rather than diffed because the word is already zero on every
call the corpus makes — recorded as an honest gap four separate times, and once
from the other direction when *adding* a store of zero that the ROM does not make
turned out to be equally invisible. It was always zero because nothing had ever
fired this weapon. `INC $7E` is the only instruction in the game that makes it
non-zero, and the `STZ` is its reset.

**And the weapon is identified off the ROM's own text.** `$81:849F  INC $1FE0,X`
is the only writer of `$7E:1FE0`; the only reader is `$82:CA8C  LDA $1FE0 :
CMP #$0028 : BCC`, on the end-of-level tally, deciding whether to draw the string
at `$82:CABD` — which reads `MONSTER/FROZEN/....////BONUS?`. So `$5D` is the ice
weapon, five hits freeze one thing, and forty freezes pay a bonus. Its
`ENEMY_DAMAGE_TABLE` entry being **zero** stops being a curiosity: it never
damages anything, which is the whole reason it needs a counter of its own.

`$81:83C6`, the `$5E` twin, is the same splice **without** the counter, and the
same tally one array over at `$7E:1FDC` — which `collide.h` already described
from `$81:9B6B`'s declined branch, and whose string two lines up reads
`MARTIAN/BUBBLED`. It is **not** ported: no input fires that weapon either, and a
routine written ahead of its input is transcription.

### Three copies of a splice, written once

`$81:8506`, `$81:BAB3` and `$81:847E` end with the same ten instructions and
differ only in the address they leave in the gap. The file had been carrying two
copies of that in C since the level-45 round, with a note saying two copies of one
routine are two things that can drift; a third would have made the point twice.
`enemy_react_splice` is now the body and the three keep their own guards, which is
the half of them that genuinely differs.

### And a second player, again

Eleven perturbations, ten caught on the one-player movie. The miss was
`TYA : ASL A : ...` reading the side off **A** instead of **Y** — invisible,
because with one player every collision id is positive and every wrong way of
computing zero is also zero. It is `movies/level25-2p.zmv`'s finding one routine
over, and the fix was the same shape: `movies/level17-2p-freeze.zmv` walks the
same four moves with **player two**, whose shots read `$805D`, and the same
perturbation fails at `$7E:1FE0`. The site pair `freeze_slot_0` / `freeze_slot_1`
is what names the gap, and it takes one movie each to fill.

**A note on the harness rather than the port.** That perturbation round also
produced an hour of chasing a divergence that was not in the tree: the
perturbation script restored the source and left the *binary* built from the
perturbed one, so the next `verify` reported `$7E:067E: ROM $01, port $00` — the
freeze counter, reset by a line that no longer existed. A tool that edits code to
break it on purpose has to put the build back as well as the file.

## The pass that is not only the scheduler's

**`sprite_build_oam` had a wrong register for forty-one routines, and the reason
nobody noticed is the sharpest statement of this project's own rule yet.**

The routine ends:

```
$80:BDCC  JSL $80BEC9      ; actor_overlap_pass
$80:BDD0  PLD
$80:BDD1  PLB
$80:BDD2  LDA $20
$80:BDD5  AND #$0003
$80:BDD7  TAX
$80:BDD8  LDA $BDE6,X
$80:BDDB  AND #$00FF
$80:BDDE  STA $1B64
$80:BDE1  SEC
$80:BDE2  RTL
```

`LDA $20` is a **direct-page** read, and the `PLD` one instruction above it has
already put the *caller's* page back — the `PEA $0000 : PLD` at the top of the
routine covers everything else it does, but not this. The port read absolute
`$0020`, which is `W_SCHED_TICK`, and that is right whenever the caller's page is
zero.

**It is not always zero, because the scheduler is not the only caller.** There are
three `JSL $80BD1F` in the ROM. `$80:837C` is `scheduler_idle`, whose page is zero
because `thread_yield` sets it there on the way past. The other two are `$82:DE03`
and `$82:DE4B`, inside the alert `$82:DDA7` runs when the player comes near the
actor whose handler is `actor_deeb_collide` — a thread, on its own page, driving the
sprite pass itself between two `WAI`s. At the failing call the caller's `D` was
`$0700` and `$7E:0720` held `$001B`, so the ROM's `X` came back `$0003` and the
port's `$0000`.

**And that difference reaches exactly one register.** All four entries of the table
at `$80:BDE6` are `$80`, so the index it feeds changes nothing the routine writes:
`$7E:1B64` gets `$0080` either way and so does `A`. 128 KB of WRAM agreed on the
failing call. The wrong value escaped through **X** alone, and X is compared only
because the shim claims it.

That is the mirror image of the V-flag finding one round earlier. There, an output
the shim did *not* claim was invisible to `verify` and only `run` caught it. Here,
an output the shim *did* claim was the only thing standing between a long-standing
bug and nobody ever knowing — and `run` would not have caught it, because it never
becomes a byte of WRAM. **The two findings together are the argument for claiming
every register you can justify:** the per-call diff sees exactly what it is told to
look at, and the two failure modes sit on opposite sides of that line.

The fix is one parameter. `sprite_build_oam(Wram*, const Rom*, uint16_t dp)`, with
`dp` reaching precisely one instruction — the same way a collision handler takes the
page its actor lives on.

## The other second weapon

**`$81:83C6` is ported, and with it the last of the three splices and the last
branch the `$81:8888` family kept for an id no input produced.**

### Which object gives which weapon, off the ROM

`movies/level17-weapon.zmv` established that `$5D` is the ice weapon by finding an
object whose collision id lands in an inventory slot and pressing B. Doing the same
for `$5E` needed the mapping rather than a guess, and the ROM states it plainly.
Each weapon has its own shot spawner and each spawner its own two-word
collision-id table, one entry per player with bit 15 set on the second — so
searching the ROM for `5E 00 5E 80` finds **one** address:

| id | id table | | id | id table |
| --- | --- | --- | --- | --- |
| `$5C` | `$81:FEC3` | | `$60` | `$81:EB9E` |
| `$5D` | `$81:FC0A` | | `$61` | `$81:EDAB` |
| `$5E` | `$81:F4C6` | | `$62` | `$81:FACD`, `$81:FC0E` |
| `$5F` | `$81:EA0C` | | `$63` | `$81:F6B8` |

`$80:D219  LDA $1CBC,X : ASL A : TAY : LDA ($64),Y` is the selector, so `$1CBC` is
an inventory **slot** and firing is gated on that slot's count. `$80:CA30` turns
object type `$04` into collision id `$0E`, and `$80:F87B` puts id `$0E` in slot 2.
Slot 0 is `$5C` and slot 1 is `$5D`, so slot 2 should be `$5E` — and `--records`
says every shot on screen reads `$5E`, which is what makes it a measurement.

Three password-reachable levels place a type-`$04` object. Level 21's is across
water and the router walks into it and drowns, from either side; level 41's took 43
legs of fitting and never arrived; **level 49's is 24 legs from the start**, and
that is `movies/level49-bubble.zmv`.

### What the walk found was not what it was aimed at

The census came back with **`$81:AC92`, 290 declines** — a handler no input had
reached, and nothing to do with the weapon. It is the **ninth copy of `$81:8888`**
and the plainest re-spelling yet: health `$3C`, parked id `$3E`, `DEC $10` on
death, `enemy_freeze` for `$5D`, `enemy_survived_react` for a survivor, and one
comparison no other copy has. `$81:ACA8  CMP #$0067 : BEQ` sends id `$67` straight
into the death tail with no subtraction — `MONSTER_HIT_FATAL`'s mechanism at a
different id — and `ENEMY_DAMAGE_TABLE` says what that is worth: `$67` costs 4
ordinarily, so this is a middling weapon being made lethal rather than a strong one
waved through.

**`$81:845E` is the round's oddity, and it came from the control.** Thirty-two
bytes, four comparisons, three exits — and **no stores at all**. Every other
handler in the project leaves at least one word of WRAM behind; this one writes
nothing anywhere, so carry is not merely most of its interface, it is the whole of
it. Ids `$03`, `$05` and `$06` — tested before the mask, so by name — park the
thread; anything below `COLLIDE_ID_PLAYER` does not; `$5E` does not, picked out one
comparison later; everything else at or above `COLLIDE_ID_PLAYER` does. It is the
only place in the game where `$5E` is an exception rather than a branch of its own.
It is also the first registry entry with `supported = NULL`: a routine that cannot
decline and cannot write has nothing to try on a scratch copy.

### `$81:83C6` is `enemy_survived_react` with one word changed

Once `$81:AC92` was ported, its `$5E` branch put the address on the census, and the
routine behind it is nine words of stack surgery:

```
$81:83C6  LDY $08 : LDA $0000,Y : AND #$0010 : BEQ +2 : <CLC : RTL>
          LDA $000C,Y : TAX : LDA $11B0,X : PHA : DEC A : DEC A : DEC A
          STA $11B0,X : PLX : TAY
          <three words slid down three bytes>
          LDA #$0081 : XBA : STA $0006,Y
          LDA #$8404 : DEC A : STA $0005,Y
          SEC : RTL
```

That is `$81:8506` with the guard's branch polarity flipped and `$8404` where the
twin writes `$8542`. `enemy_react_splice` already existed for the other three, so
what this cost was a guard and a constant.

**PROGRESS.md described this routine wrongly two rounds ago**, and the bytes say
so: "the same splice without the counter and with its tally one array over at
`$7E:1FDC`". The first half is right and the second half belongs to a different
routine — `$7E:1FDC` is incremented by `$81:9BA2`, the nine instructions
`enemy_9b6b_collide`'s own `$5E` branch runs *before* it `JML`s here. `$81:83C6`
has no counter, no tally and no side lookup at all.

The naming chain is one link longer than `enemy_freeze`'s and it still closes.
`$81:9BA2` increments `$7E:1FDC`; the end-of-level tally reads that word to decide
whether to draw a string reading `MARTIAN/BUBBLED`; and `$81:8404`, what a
spliced-in thread wakes up running, pushes the creature's metasprite and collision
id and then writes `$0036` over `ACTOR_COLLIDE_ID`. So `$5E` is the **bubble**
weapon, its damage-table entry is zero for the same reason `$5D`'s is, and what it
does is replace what a creature *is* rather than take anything off it. One weapon
freezes and one bubbles, and neither does a point of damage.

### The control that stopped being one

`movies/level49-corner.zmv` is the same walk with the B press removed, written to
separate "new weapon" from "new route". It does that job — `$81:AC92` appears on
both movies, so it is the route's finding, and `$81:845E` appears only on the
control — and then it stops being a control. Holding Y for eighteen hundred frames
**empties the squirt gun**, and the game moves the selection on by itself:
`weapon_select_next` is called twice with `weapon_changed` both times and no B
press anywhere in the file, and `--records` shows `$5C` up to about frame 4550 and
`$5E` from 4560. So both movies fire the bubble gun; the control just arrives late,
by a path no input had taken.

Which is why `$81:83C6`'s three diffed calls are on the *control* and not on the
movie built to reach it. By the time the bubble movie is firing `$5E` its walk is in
the north-west corner and the `$81:AC92` creature is not; by the time the control is
firing `$5E` the walk has come back. Three calls is a thin sample, all three of them
the splice, and `bubble_already` — the guard's refusal — has never been reached.

## Seven untaken sites that are not waiting for a movie

**The coverage table's largest single family is `*_no_damage`, and most of it
cannot be reached at all.** Nine copies of the enemy collision subsystem each
mark "a hit whose damage-table entry is zero", and after thirty movies every one
of the nine reads zero. The obvious reading is that the corpus is thin. It is
not, and the ROM says so in two steps.

**Step one: which ids have a zero entry.** `ENEMY_DAMAGE_TABLE` at `$81:8561` is
32 words, indexed `(id - $5C) * 2`, and exactly seven of them are zero — `$5D`,
`$5E`, `$71`, `$72`, `$73`, `$78` and `$7B`.

**Step two: which ids a shot can carry.** Each weapon has its own spawner and
each spawner a two-word collision-id table, one entry per player with bit 15 set
on the second, so every id the player can put on the screen appears in the ROM as
the four bytes `id 00 id 80`. Searching all 1 MB for that pattern and keeping the
hits in bank `$81` — the shot bank — enumerates them:

| id | table | | id | table |
| --- | --- | --- | --- | --- |
| `$5C` | `$81:FEC3` | | `$63` | `$81:F6B8` |
| `$5D` | `$81:FC0A` | | `$64` | `$81:F6BC`, `$81:F6C0` |
| `$5E` | `$81:F4C6` | | `$66` | `$81:F6C4` |
| `$5F` | `$81:EA0C` | | `$67` | `$81:F6C8` |
| `$60` | `$81:EB9E` | | `$68` | `$81:F918` |
| `$61` | `$81:EDAB` | | `$6F` | `$81:F109` |
| `$62` | `$81:FACD`, `$81:FC0E` | | | |

`$71`, `$72`, `$73` and `$7B` are not there — and they are not anywhere else
either: there is no `LDA #$0071`, `#$0072`, `#$0073` or `#$007B` in the whole
ROM. `$78` has five immediate loads, none of them near shot code.

**So the only zero-damage ids a shot can carry are `$5D` and `$5E`, and seven of
the nine copies divert both of them before the subtraction.** `enemy_collide` and
`enemy_b41c_collide` hand both to a routine of their own; `monster_collide`,
`enemy_d7f6_collide`, `enemy_9b6b_collide` and `enemy_9063_collide` divert `$5D`
and give `$5E` the death tail with no subtraction at all; `enemy_ac92_collide`
sends one to `enemy_freeze` and the other to `enemy_bubble_react`. Nothing that
reaches their `CMP` can arrive with a zero. **`enemy_no_damage`,
`monster_no_damage`, `b41c_no_damage`, `d7f6_no_damage`, `d9b6b_no_damage`,
`d9063_no_damage` and `dac92_no_damage` have no input, in the same sense
`$80:FA26` has no movie.**

**The two that are not in that list are the interesting half.**
`enemy_d301_collide` has no `CMP #$005E` — the port's own comment on that line
says so — so `$5E` falls through to the table and `d301_no_damage` is one bubble
shot away. And `boss_9660_collide` *rewrites* ids before indexing: `$62` and `$70`
become `$5C` or `$5D` on the scheduler clock's low bits, and the boss does not
divert `$5D`, so half of that weapon's hits land on a zero entry. Both are
reachable, and both need a weapon rather than a longer walk.

**What this changes is the work list rather than the port.** An untaken site is
an unverified branch either way; the difference is whether the answer is an input
or an argument. Seven of these are an argument, and writing it down is what stops
them being counted as outstanding work every round.

## The weapon the boss rewrites, and a word nothing could read

**`boss_remap_61` is taken.** `$82:9660` answers four collision ids as some other
id and no input had ever produced one of them, so four of its eleven sites had
read zero since the routine was written. `movies/level25-boss.zmv` produces one.

### Which object gives which weapon, priced

The chain from a level's object list to a number of damage is three ROM tables,
and reading all three at once turns "try a weapon" into arithmetic:

* `$80:CA30` — object type to collision id. Thirty entries; type `$26` is id `$11`.
* `$80:F87B` — id to inventory slot, `slot = id - $0C`. Slot 5 is weapon `$61`.
* `$80:F8AC` — **how much a pickup gives**, by slot. Slot 5 gives **five shots**.
* `$81:8561` — `ENEMY_DAMAGE_TABLE`. `$61` costs 20, and the boss rewrites it to
  `$60`, which costs 4.

Level 25 places five weapon objects. Two of them the router refuses and it is
right both times — the `$16` at (1243,505) is inside a display case and the `$06`
at (989,122) is in a water feature, and the map picture says so. The other three
are `$26`s, and there are four of them because two sit on the same pixel.

**So the whole of level 25's heavy ammunition is twenty shots at four damage,
against seventy health.** That is a ceiling of 80, which is why the movie
collects all four objects rather than the two on the way.

### Three things about the route that were not in any table

**The start is the top of an escalator.** It pushes the player back up the moment
Down is released, so the first leg has to be *held* through — and that is why
`tools/fit_route.py` cannot fit this route. The fitter's loop is plan a leg,
release, measure, re-plan; on an escalator it measures itself back where it
started. Three runs, of 40, 53 and 48 legs, spent about forty minutes each and
finished 0, 200 and 46 pixels from where they began. It is the first level in the
corpus with an escalator on it.

**The route planner's grid treats escalator tiles as walkable both ways**, which
they are not: the southbound leg at x=937 stops dead at y=790, at the lip. Both
of the planner's routes to the south of the level go through that tile.

**And the level's own geometry is a better guide than either.** Rendering the map
with `zamn_assets level` and marking the start and the target on it found the
open corridor at x=1017 in about a minute; the whole route is nine hand-written
legs, tuned against `--pos` four times.

### The finding is a word, and neither instrument could see it

The first run landed **47 damaging hits with no zero-damage hits among them** —
`boss_hit` 47, `boss_survived` 47, `boss_no_damage` 0 — on a health word seeded
once, to 70, by the only instruction in the ROM that writes it other than the
handler's own store. Forty-seven hits of at least one damage each cannot leave 70
health standing, and `verify` reported **0 diverged on all 9,138 calls**, so the
port and the ROM agreed about every one of them. Something was wrong with a
*reading*, and neither `--pos` nor `--records` could say what: the boss's health
is not a position and it is not in the display list.

`zamn_headless --watch <addr>[,first[,last[,step]]]` is the third instrument, and
it prints a word only when it changes, so a whole movie costs ten lines:

```
watch $7E:083C  frame  3900  $0046 (70)
watch $7E:083C  frame  4540  $0042 (66)
watch $7E:083C  frame  4544  $003A (58)
watch $7E:083C  frame  4548  $0032 (50)
watch $7E:083C  frame  4552  $002E (46)
watch $7E:083C  frame  5488  $002D (45)   ... 43, 42, 41, 40
```

Thirty damage, in about eleven hits, and the four-at-a-time drops in twelve
frames at 4540 are the `$61` volley arriving. It is repeatable over all
twenty-four thread pages at once — `--watch` may be given more than once, which
is what ruled out the theory that a second creature was soaking the rest: on
every other page the word at `+$3C` cycles 0/2/6/10/14 like an animation frame,
and only `$7E:083C` starts at 70 and falls.

**And the discrepancy was the report telling the truth about itself.** Under
`verify` the harness runs the port once per *interception*, and `boss_9660` is
reached both at its own entry PC and through `thread_call_handler`, which is
ported — so its sites are counted twice per call. `cosim_coverage_report` says
so, in a comment beside the numbers: *the hit counts are call-weighted rather
than event-weighted*. Halved, 47 hits is 23 and 17 `$61` hits is 8 or 9, and
8 × 4 = 32 against 30 measured. Everything agrees. **A "bug" that survives ten
minutes of arithmetic and dies to reading the paragraph next to the number is
worth writing down**, because the next reader of that table will do the same sum.

### What would actually kill it

Twenty `$61` shots is 80 damage against 70, so the kill exists and has almost no
slack: this movie lands about nine of the twenty and the rest fly into the
mall. Two things would close it — a route that presses B with the boss already
adjacent and stays adjacent for the whole volley, or a **two-player** movie,
which doubles the ammunition rather than the accuracy. Retiming the B press was
tried twice, at 4300 and 4450, and both are *worse* (20 damage against 30): the
volley is 600 frames long and moving it moves every monster on the level with it.
`boss_died` stays untaken, and it is now priced rather than merely open.

## One byte

**The boss movie put an address back on a census that had been empty for three
rounds, and the routine behind it is a single instruction.** `$81:EDAA`, 62
declines on `movies/level25-boss.zmv` and none anywhere else:

```
$81:EDAA  6B              ; RTL
$81:EDAB  61 00 61 80     ; weapon $61's two-player collision-id table
$81:EDAF  23 EE 47 EE     ; and its two routine pointers
```

The address and the table are one weapon's shot code laid out back to back, which
is what identifies it: **it is the `$61` shot's own collision handler**. The
squirt gun's `$81:FE0E` reads the id it was hit by, expires on some and passes
through others; this one is told about every collision it has and answers none of
them. The thing it hit still reacts — the enemy handler runs on the other side of
the pair, which is how `boss_remap_61` is reached at all — but the shot carries
on. For a weapon that costs five shots a pickup, that is the right behaviour, and
it is spelled as the absence of code.

**The shim claims every output there is, and the emptiness is what licenses it.**
`RTL` sets no flag and touches no register, so A, X, Y, N, Z, C **and V** come
back exactly as they went in; all four flags are claimed and all four are checked
on every call. Set that beside `$82:F1C2`, where the port read "no `CLC` and no
`SEC`" as "carry comes back as the caller left it" and `verify` said
`flag C: ROM 1, port 0` on the second call it ever saw — because a `CMP` on every
path had already decided carry. **The absence of a carry instruction is not the
absence of a carry output; the absence of every instruction is.** Those are
different facts and only the second one supports this shim.

It is also the second entry in the registry with no coverage site, after
`actor_845e_collide`: a routine with no decision in it has no untaken branch.

And the measured cost is worth one line. `verify` reports **42..82 cycles, mean
43**, for an instruction that takes six — because what the harness measures is
entry PC to return, and for a one-byte routine that is almost entirely the `JSL`
and the bus. Every other `cycles` figure in the registry has the same
constant folded into it; this is the entry where it is the whole number.

## A column that says which routine owns which thing on the board

**The census had been empty for four rounds, so the work list was the coverage
table — and the coverage table names branches, not inputs.** `d7f6_hit`,
`d9b6b_hit`, `d9063_survived`, `dac92_died` and thirty more say what the port has
never been asked to do; none of them says *where, on which level*, the creature
that would ask is standing. Every round before this one answered that by walking
around and looking at the picture.

`--records` answers it directly now. Each row already carried `ACTOR_REC_THREAD`,
the byte offset of the thread that owns the record; two more columns turn that
into a name:

```
    addr   flags   x     y      id   thread  page   handler
    $1A66  $8009    581   707   $03   $26    $0800  $82:9660
    $1A52  $8001    609   690   $61   $20    $0500  $81:EDAA
    $1A3E  $8001    490   674   $04   $22    $0600  $81:C440
```

`$1300,X` and `$1330,X` are the collision handler that thread installed — the
same pair `thread_call_handler` reads — so the display list stops being a list of
things and becomes **a list of routines**, every one named by the entry address
`src/port/collide.c` already uses. Pointed at `movies/level17.zmv` it finds
`$81:D7F6` at (563,513) in thirty seconds, and `zamn_assets actors` then confirms
it without running anything: actor 11, type `$23`, behavior `$81:D704` — the body
that installs the handler, `$F2` bytes ahead of it in the same routine.

**The page column had a bug in it that looked right, which is why it is worth a
paragraph.** The obvious reading of the thread arrays is that slot `n` lives at
`$0100 + n*$80`, and the first column of the real table agrees with it. It is a
table, not a formula: `$80:82DE` reads `$0100 $0280 $0380 … $0C80` for the first
thirteen slots and then starts again at `$0180 $0200 $0300 … $0C00` for the
twelve after. The formula puts level 25's boss on `$0A80` where the ROM says
`$0800`, so `--watch 0ABC` — aimed at its health with real care — printed a
steady zero and looked exactly like a boss that never gets hurt. The column reads
`$80:82DE` now, and the first thing it did after the fix was print `$0800` beside
`$82:9660`.

## The spin probe

**`$81:9B6B` shows up 96 times in 126 sampled frames of level 21's display
list — three copies alive at once for much of it — and every call it made took
the ignore path.** Five of its seven
sites had read zero since it was ported. The reason is visible only with the two
position dumps side by side: **these things walk at the player**, and
`movies/level21.zmv`'s legs are 180 frames long, so the player spends almost all
of its time facing away from the crowd it is towing. A shot leaves in the
direction you are facing; a chaser is behind it by construction.

`tools/make_spin_probe.py` rewrites the tail as the same twelve directions at 30
frames instead of 180. Four legs of 30 cancel out, so the player stays roughly
where it started and faces all four directions once a second.
`movies/level21-spin.zmv` is that, and it verifies **73,039 of 73,039 on the
first run** with `d9b6b_hit` 35, `d9b6b_died` 35 and `f534_ignore` taken as well.

**The two 35s being equal is the finding.** Every hit this creature takes kills
it, so `d9b6b_survived` needs a weapon doing less than one point of damage — and
the only damage-table entries below 1 are the zeroes, which are a different
branch. It is the previous round's `*_no_damage` argument applied to a
`*_survived` site: one more of the eighty-six untaken sites is now known to have
no input rather than merely to lack one.

**The spin is not universally better, and that is worth recording because the
obvious next move is to run it everywhere.** On levels 5, 17 and 49 it adds
nothing at all — it trades ground for aim, and a creature that does not come to
you has to be walked to. `--leg 180 --lookback 30`, a box walk with a short
reversed leg after each, is the compromise, and it does reach level 5's
`$81:9063` where the pure spin does not.

## What a level's object list says about which branches have an input

The previous round proved seven `*_no_damage` sites unreachable by reading which
ids a shot can carry. The same tables, run the other way, say for **every** level
which weapons it can hand you — and so which `*_special`, `*_freeze` and
`*_fatal_id` branches any input on that level could ever take:

* `$80:CA30` — object type / 2 to collision id.
* `slot = id - $0C`, weapon `$5C + slot` (`$80:F87B`).
* `$80:F8AC` — shots per pickup, **by slot and in BCD**. Slot 11 reads `$0020`,
  which is twenty and not thirty-two, and that difference is a boss's health.
* `$81:8561` — `ENEMY_DAMAGE_TABLE`.

| level | weapons its own object list gives | best ceiling |
| --- | --- | --- |
| 1 | `$5C`x2 (1 dmg, 198 shots), `$5D` (0, 99), `$62` (3, 20) | 198 |
| 5 | `$5C`, `$5D`, `$5F`x3 (1, 900), `$61`x2 (20, 10), `$66`, `$67` | 900 |
| 9 | `$5C`, `$5D` (0, 99), `$5F`x2 (1, 600) | 600 |
| 13 | `$5C`x2, `$61`x2, `$62`, `$64` | 200 |
| 17 | `$5C`x3, `$5D`, `$60` (4, 30), `$63` | 297 |
| 21 | `$5D`, `$5E` (0, 40), `$61`x3, `$62`x2 | 300 |
| 25 | `$5F`, `$61`x4 (20, 20), `$62`, `$64`, **`$67`x2 (4, 40)** | 400 |
| 29 | `$5C`x2, `$5D`, `$5F`, `$60`, `$61`, `$62`x2 | 300 |
| 33 | `$5C`, `$60`x2, `$62` | 240 |
| 37 | `$5F`, `$60`, `$61`, `$62` | 300 |
| 41 | `$5C`, `$5D`, `$5E`, `$5F`, `$61`, `$62`, `$66`x5 | 300 |
| 45 | `$5C`, `$5F`, `$60`, `$61`x9 (20, 45), `$62`x2 | 900 |
| 49 | `$5E` (0, 40), `$5F`, `$66` | 300 |
| 53 | — none — | — |

Read against the untaken list it turns four shrugs into work items and one into a
closed question:

* **`cdde_survived`, `cdde_killed_reacting` and `cdde_react_begin` have an
  input.** `CDDE_HIT_DAMAGE` is `$5D` — the ice weapon is the only thing that
  hurts that creature, which is why `cdde_unmatched` is taken and the damage path
  is not — and level 9 places a type `$02` object at (260,252) worth 99 shots.
  `$81:CDDE` shows up 204 times in 126 sampled frames of level 9's display list.
* **`d9b6b_special` and `d9b6b_fatal_id` have an input**: level 21 places both
  `$5D` and `$5E`, and `movies/level21-spin.zmv` already stands next to the
  creature.
* **`d7f6_special` and `d9063_special` have inputs** — levels 17 and 5 both place
  `$5D` — though level 17's creature is behind a route nobody has walked.
  *(Corrected later: level 17's creature is not behind a route, it is in a sealed
  pen, and level 5's `$5D` is in one too. The `d7f6_special` input is level 29's.
  See "Six branches nobody could reach, and the pen they were in".)*
* **`dac92_freeze` does not.** It is id `$5D`, and level 49 places `$5E` and
  nothing else that freezes. Unless `$81:AC92` turns up on another level, that
  site belongs with the seven `*_no_damage`.

## The boss, repriced — and still alive

**The previous round's ceiling for level 25 was eighty damage against seventy
health, and it was half the real number because it read one weapon.** The four
`$26` objects give `$61`: five shots each, twenty damage rewritten by
`boss_9660_collide` to `$60`'s four, so 20 × 4 = 80. Level 25's object list also
holds **two type `$22` objects**, at (1046,1045) and (1110,1077) — id `$17`, slot
11, weapon **`$67`**: twenty shots a pickup, four damage, and **not one of the
four ids the boss rewrites**. Forty shots at four is a hundred and sixty.
`movies/level25-heavy.zmv` collects both.

**So the kill stopped being ammunition-limited and became accuracy-limited, and
this round measured the accuracy.** Six attempts, all after the same detour:

| attempt | damage done |
| --- | --- |
| 60-frame spin beside the boss | 16 (4 hits of 40 shots) |
| the box walk `movies/level25-boss.zmv` uses, after the detour | 8 (12 with the squirt gun's four) |
| park and hold Left + fire, 1,800 frames | 0 |
| ...Right | 0 |
| ...Up | 0 |
| ...Down | 0 |

Four zeroes in a row is the useful measurement. **This boss does not walk into a
stream of fire.** It crosses the plaza at about ten pixels a frame against the
player's two, and between two samples forty frames apart it can be four hundred
pixels away. Every earlier theory about the fight assumed it would come to you,
because in `movies/level25-boss.zmv` it did — and that movie's player was
standing where it happened to be. `boss_died` stays untaken; what it needs is a
fight, and the ammunition to lose most of is there now.

**Two route findings came out of reaching the `$22` objects.** The south of the
mall is behind a pair of escalators eight pixels apart — **down at x≈917, up at
x≈945** — and each is one-way. `zamn_assets route` treats both as walkable both
ways, so every route it plans out of the south climbs the down one; it is the
second escalator its 2x2-clear grid has been wrong about and the first where the
error strands the player. And a pickup wants the player within about ten pixels:
passing at (1057,1032) with the object at (1046,1045) collects nothing, which is
what `--watch 1CE2` is for.

## Seventeen bytes, four weapons

**`movies/level25-heavy.zmv` put an address back on a census that had been empty
for four rounds**: `$81:F6A3`, 273 declines, and none anywhere else.

```
$81:F6A3  CMP #$0003 : BEQ $F6B4
          CMP #$0004 : BEQ $F6B4
          CMP #$0001 : BEQ $F6B4
          CLC : RTL
$81:F6B4  STA $3E : CLC : RTL
```

**It is identified off the bytes after it, the way `$81:EDAA` was, and the answer
is different in kind.** `$81:F6B8` begins `63 00 63 80 | 64 00 64 80 | 64 00 64
80 | 66 00 66 80 | 67 00 67 80` — five two-word collision-id tables in a row, and
`$81:F6B8` is the address the weapon table already gives for id `$63`. So this is
not one weapon's shot code but **four weapons sharing one handler**, the first
address in the project reached by more than one weapon, and the reason its census
entry is 273 declines rather than a handful: `$67` is only the first of the four
to be fired.

**Both exits clear carry**, so unlike `actor_845e_collide` — the same shape, three
named ids and an else — this one can never park its thread. A shot that hits
something keeps flying either way; what changes is whether the shot's *body* is
told, and `$3E` is where it is told. That word has its seed and its readers in
the same bank: `$81:F691  STZ $3E` clears it four instructions before
`$81:F69B  LDA #$F6A3 : LDY #$0081 : JSL $80:8475` installs this very handler,
and `$81:F3F6  LDA $3E : BNE` and `$81:F57B  LDA $3E : BEQ` are the body polling
it on its next pass.

**273 of 273 checked on the first run, 0 diverged**, and both sites taken:
`f6a3_record` 35, `f6a3_ignore` 1,282.

**Ten perturbations, eight caught**, and both misses are named by something the
routine or the movie already says:

| perturbation | what failed |
| --- | --- |
| store at `$3C` instead of `$3E` | `$7E:043C: ROM $00, port $03` |
| store zero instead of the id | `$7E:043E: ROM $03, port $00` |
| store on every id, guard dropped | `$7E:053E: ROM $00, port $05` |
| carry set on the record path | `$7E:119E: ROM $01, port $00` |
| A returned as the stored word | `A: ROM $0005, port $0000` |
| Z clear on the record path | `flag Z: ROM 1, port 0` |
| N set on the record path | `flag N: ROM 0, port 1` |
| Z set on the ignore path | `flag Z: ROM 0, port 1` |
| **drop the `CMP #$0001`** | *nothing* |
| **ignore-path flags from the first `CMP` rather than the last** | *nothing* |

The carry perturbation is the one worth reading twice: it fails at **`$7E:119E`,
one level up**, because carry set is what `thread_call_handler` parks a thread
on, and a shot whose thread parks stops moving. Nothing inside the routine
disagrees at all.

The first miss is the movie's: all 35 recorded hits carry `$03` or `$04`, and no
collision in 6,700 frames hands this shot an id of `$01`. The second is
arithmetic — `arg - $0001` and `arg - $0003` have the same sign for every id
except `$0002`, and `$0001` and `$0003` both take the other branch — so that
perturbation is invisible to anything except a collision with an id of exactly
two. Both are gaps in the input rather than in the port, and both are cheap to
state, which is the difference between a miss that is recorded and a miss that is
an unknown.

**And the harness's own perturbation script had a bug of exactly the kind it
exists to find.** Two of these cases first came back MISSED because the three
lines they matched — `r->n = false; r->z = true; return true;` — are identical in
an earlier routine, and `str.replace(old, new, 1)` perturbed *that* one. A tool
that breaks code on purpose has to be sure it broke the code it meant to; it
asserts the pattern occurs exactly once now. The previous round's version of this
lesson was that such a tool has to restore the *binary* as well as the file.

**Corpus after this round: 2,906,006 calls checked across 33 movies, 0 diverged;
branch coverage 159 of 242, 83 untaken by every input, census empty.** The two
new sites are both taken on the movie that added them, and the three the round
set out to take — `d9b6b_hit`, `d9b6b_died`, `f534_ignore` — are taken as well.

### And what `run` says about the new movie

`movies/level21-spin.zmv` substitutes cleanly. `movies/level25-heavy.zmv` does
not: **1,959 bytes unaccounted for**, with `$7E:0016` — the NMI frame counter —
reading `$0C` on the stock side against `$FF` on the native one. That is the
clock-drift signature this file recorded for level 25 two rounds ago, and the
control settles what it is not: run with **`-r shot_f6a3` alone**, so that the
only substituted routine is the new one, and the two cores are **identical at all
6,689 compared passes** over the same 6,700 frames. The 148-cycle budget costs
nothing; it is the other forty-four returning on fixed budgets across a long busy
level.

**But the gap is not a property of level 25, which is how this round changed the
diagnosis.** Three level-25 movies, all with all forty-five substituted:

| movie | frames | `run` |
| --- | --- | --- |
| `movies/level25.zmv` | 4,700 | 1,910 bytes unaccounted — unchanged for three rounds |
| `movies/level25-heavy.zmv` | 6,700 | 1,959 bytes unaccounted |
| `movies/level25-boss.zmv` | 7,600 | **clean**, at most 57 bytes at once |

The longest of the three is the one that holds. So "the busiest level anyone has
run" is not the whole story: whatever the drift latches onto, one route through
level 25 avoids it for 7,600 frames and two do not.

## Spending the weapon table

The previous round ended by turning `$80:CA30`, `$80:F8AC` and
`ENEMY_DAMAGE_TABLE` into a per-level list of which weapons each password level
can hand you, and reading it against the untaken sites. It named four work items
and closed one. This round spent the first of them, and what came back was
larger than the entry: **one movie, nine newly-taken sites**, and three more
that the same table says have no input at all.

The entry was one line:

> `cdde_survived`, `cdde_killed_reacting` and `cdde_react_begin` have an input.
> `CDDE_HIT_DAMAGE` is `$5D` — the ice weapon is the only thing that hurts that
> creature — and level 9 places a type `$02` object at (260,252) worth 99 shots.

`movies/level9-weapons.zmv` collects that object. It also collects the type `$06`
object at (43,69), which is `$5F`, and it fires all three weapons the level owns
at two different creatures, because once the player is standing next to
`$81:D301` with two guns it costs eighty frames to fire the other one.

### The index is the level plus one

`zamn_assets actors <rom> 10` is level **9**. The tool takes the internal record
index and the game's level numbering starts one below it, which the previous
round used correctly and this file never wrote down — so the first thing this
round did was dump `actors … 9`, find no type `$02` object anywhere in it, and
spend twenty minutes preparing to correct a table that was right. The tell is the
level-25 row: `actors … 26` has the four `$26` objects and the two `$22` objects
at (1046,1045) and (1110,1077) that `movies/level25-heavy.zmv` actually picked
up, and `actors … 25` has neither. The per-level weapon table in this file is
built from the `+1` index and stands.

### Walls, not stopwatches

Every route in this project so far has been a list of timed legs: hold Left for
540 frames, then Up for 214. That works on the levels it was invented on and it
does not work on level 9, whose corridors are one tile wide and whose actors get
in front of you. The same Up leg measured twice took the player to y=204 once and
y=299 the next time, because on the second pass something was standing in it for
sixty frames — and a leg that ends in the wrong place turns into a leg that walks
into a wall for two hundred frames.

**So this movie's turns are taken at walls.** Walk east until the corridor ends
at x=392; walk north until the ceiling stops you at y=129; walk west until x=90;
walk south until y=719. Each of those legs is held well past the point of
arrival, so a slow pass and a fast pass end in the same pixel, and the leg after
it starts from a known place instead of an estimated one. Nine of the movie's
twelve travel legs are anchored that way and the whole route is reproducible.

Two of them cannot be, and both are worth naming:

* **The descent to the ice weapon is timed**, because the object is in the middle
  of a corridor and there is no wall at (260,252). It is `Down` from y=207 for
  twenty-six frames, and the tolerance is about eight pixels: turning at 3612
  passes at (338,251) and collects nothing, turning at 3616 collects 99 shots.
* **The vertical corridor at x≈348 has no anchor of its own**, so it gets one:
  walk east into the wall at x=392, then hold `Left` for exactly twenty frames.
  Sixteen frames leaves the player at x=360 and Up is blocked; twenty-four leaves
  it at x=344 and Up is blocked. A back-off from a wall is a timed leg whose
  start is exact, which is most of what makes timing work.

### B is dropped if you press it twice too quickly

Selecting a weapon costs two presses of B when the player owns three, and the
second one does nothing if it comes too soon. Twelve frames down and twenty-eight
frames apart: the first press is swallowed. Twelve down and forty-eight apart:
still swallowed. **Sixty apart and every press lands** — `$1CBC` walks
`$0003 → $0000 → $0001 → $0003` on the nose. This cost a phase of the movie,
because the two presses meant to put the ice weapon back had put the ordinary one
there instead and the fight that followed spent eighty shots of `$5C` on a
creature that is immune to it.

That is the second time a `--watch` on `$1CBC` has been the thing that found it,
and the general shape is the one `--records` was built for: **the movie is an
input and the game's state is the output, and checking the output is cheaper than
reasoning about the input.**

### What the three weapons bought

`movies/level9-weapons.zmv`, 9,000 frames, **165,757 calls checked, 0 diverged,
and 75 of 242 sites** — which ties `movies/level25-boss.zmv` for the most any
single input has ever reached.

| site | count | what it needed |
| --- | --- | --- |
| `d301_shot_immune` | 60 | the ordinary `$5C`, which this creature is the only one in the family to refuse |
| `d301_special` | 380 | `$5D`, which leaves through `enemy_freeze` |
| `d301_survived` | 125 | `$5F`, the one id in reach that costs it a point |
| `d301_died` | 20 | ...enough of them |
| `d301_reseed` | 20 | the 25-in-256 draw on a death |
| `d301_no_reseed` | 105 | and the 231 that are not |
| `cdde_survived` | 100 | `$5D` again, one page over |
| `cdde_react_begin` | 5 | five of them in a row |
| `d301_ignore` | 7,564 | anything below a shot — untaken only because nothing had walked up to this creature |

**`$81:D301` was the routine with one branch of eight diffed and seven
transcribed**, which this file said in the header when it was written. **Seven of
the eight are diffed now**, on an input that hits the same creature with three
different guns in ninety seconds, and the eighth is argued below to have no input
at all. That is the largest single change in the standing of one routine since
the census went empty.

The freeze family came free: `freeze_counting`, `freeze_already`, `freeze_took`
and `freeze_slot_0` all read on the ice phase, because `d301_special` *is* a tail
call into `enemy_freeze` and 380 hits is more than enough to walk its counter
past five.

### Range is not the same as facing

The `$5F` phase failed twice before it worked, and the failure is worth recording
because it looks like a bug in the port and is not.

Fired **up**, from (98,331) at a creature sitting at (107,315) — sixteen pixels
away and nine to the side — two hundred and eighteen shots produced **zero**
calls on the damage path. Fired **sideways**, from (90,313) at the same creature
seventeen pixels to the right, the same weapon produced **145** — 125 it survived
and 20 that killed it. The same gun, the same creature, a comparable distance.
What `--records` shows in the failing case is a `$5F` record at (97,339) living for a single frame, which
is a shot that spawned and expired without touching anything.

The working shape is the one the ice phase had already found by accident, and the
movie now uses it for both weapons: **stand level with the thing and fire along
the row.** Whether the cause is the shot's hitbox, the spawn offset or the
facing, the input that distinguishes them is one this project can now write, and
the cheapest way to find it was to fire the same gun from two places.

### Three sites that are not waiting for a movie

The same table that produced the movie closes three more of the eighty-three, by
the argument the seven `*_no_damage` sites established two rounds ago: *name the
id the branch needs, then ask whether any reachable level places the object that
carries it.*

* **`d301_no_damage`** needs a hit whose damage-table entry is zero. Those ids are
  `$5D`, `$5E`, `$71`, `$72` and `$73`; `$5D` never gets there, because
  `d301_special` catches it two comparisons earlier. That leaves `$5E`, which is
  object type `$04` — and type `$04` is placed on eight levels, of which
  **21, 41 and 49** are password-reachable, and `$81:D301` appears in no sampled
  frame of any of the six movies that walk them. It is level 9's creature, and
  level 9 has no type `$04`.
* **`cdde_counted`** needs `$64` or `$6F`. `$64` is object type `$1A`, which
  level 9 does not place; `$6F` is not a pickup at all — no entry of `$80:CA30`
  maps to its inventory slot, and the only thing in the ROM known to produce it
  is `boss_9660_collide`'s remap.
* **`cdde_killed_reacting`** needs the countdown to go under **while the flash is
  still running**, and `enemy_cdde_react_begin` is what makes that hard: it sets
  the timer to 30 frames and puts the countdown *back up* to 20. So it is
  twenty-one damaging hits inside thirty frames, from the one weapon that damages
  this creature.

That last one is arithmetic rather than a shrug. `$7E:1CCE` drains eighty shots
in 607 frames of held fire — **one shot every eight frames** — and one shot is
worth about four hits — `$070C` walks `4 → 3 → 2 → 1 → 0` on four consecutive
frames from a single hit and then stops. Thirty frames is therefore about
**fifteen hits** at the very best, against twenty-one needed, and the shortfall is
not the sort a better route closes.

Two players would close it, and **no level in the game places two type `$02`
objects** — all sixteen that place one place exactly one. Both players cannot
carry the ice weapon, and no other id damages this creature. The site is
one player's fire rate short of reachable, and there is no second player to
borrow from.

### Corpus, and what `run` says about a 9,000-frame movie

**3,071,763 calls checked across 34 movies, 0 diverged; branch coverage 168 of
242, 74 untaken by every input, census empty** — from 159 of 242 and 83. All
165,757 of the new calls are the one movie's, and nine of the nine sites it added
are ones no other input reaches.

`zamn_cosim run` substitutes all forty-five routines on it and finds **no byte of
live game state differing on any of 8,989 compared passes**, at most 79 bytes at
once and every one of those inside the stacks or a declared scratch byte. That is
worth one line in the level-25 clock-drift file: `movies/level9-weapons.zmv` is
**9,000 frames**, longer than any movie in the corpus and 1,400 frames longer than
`movies/level25-boss.zmv`, and it is clean. Length is not the trigger, which is
the same thing the three level-25 movies said from the other direction.

## The bubble gun, and two addresses on an empty census

The previous section spent one entry off the weapon table and got nine sites.
This one spends the next — *`d9b6b_special` and `d9b6b_fatal_id` have an input:
level 21 places both `$5D` and `$5E`, and `movies/level21-spin.zmv` already
stands next to the creature* — and gets something the table did not predict:
**two addresses on a census that had been empty for five rounds, and both of them
ported by the end of the round.**

`movies/level21-bubble.zmv` climbs to the type `$04` object at (485,1340), which
is inventory slot 2 and weapon **`$5E`**, and shoots the `$81:9B6B` creatures
with it. It checks **128,558 calls, 0 diverged**, and takes `d9b6b_hit`,
`d9b6b_fatal_id`, `d9b6b_died`, `d9b6b_bubble_slot_0`, `bubble_splice` and
`f4ef_player`.

### `$81:9BA2` — nine instructions, and four rounds of declining

`enemy_9b6b_collide`'s `$5E` branch is the only one in the family that did not
go straight to `enemy_bubble_react`, and this file has said why since the routine
was written:

```
$81:9BA2  TYA : ASL A : AND #$0000 : ROL A : ROL A   ; bit 15 of Y -> side
          JSL $80:9D6A                               ; side -> score slot
          INC $1FDC,X
          JML $81:83C6
```

Nine instructions in front of a routine the port already had. **They are the
whole reason the branch declined**, and the reason nobody had ported them is
that nothing could reach them: `$5E` is the bubble gun, no movie had ever fired
it, and a routine with no input is a routine nobody can check. That is the shape
this project keeps finding — the work item was never the code, it was the movie.

**The counter names the weapon, and its threshold is not the one next door.**
`$82:C9AE  LDA $1FDC : CMP #$000A : BCC` is the end-of-level screen deciding
whether to draw `MARTIAN/BUBBLED`, exactly as `$82:CA8C  LDA $1FE0 : CMP #$0028
: BCC` decides `MONSTER/FROZEN`. The two counters are two words apart because
they are two lines of the same screen — but **ten bubbled martians earn the bonus
and it takes forty frozen monsters**, which is the ROM's own estimate of which
gun is scarcer. `$1FDE` is player 2's, at the same ten.

### `$82:F4EF` — sixteen bytes, and the first handler outside bank `$81`

The climb also put a second address on the census, and it is the smallest kind
of find this project gets:

```
$82:F4EF  CMP #$0005 : BEQ $F4FB
          CMP #$0006 : BEQ $F4FB
          CLC : RTL
$82:F4FB  STA $18 : CLC : RTL
```

`shot_f6a3_collide`'s shape one comparison shorter, and **the first ported
handler that lives in bank `$82`** — the level and UI bank rather than the actor
bank. What makes it worth a paragraph is *which* ids it names. `$05` and `$06`
are the two players' own collision ids: the numbers every other handler in the
game sees at the bottom of its `CMP #$005C` and throws away as "below a shot's".
This one throws away everything else. No weapon, no monster and no shot can make
it store anything; its entire collision interface is **"a player is standing on
me"**.

`$18` has its seed and its reader in the same routine, the pattern `$81:F6A3`'s
`$3E` established last round: `$82:F3A7  STZ $18` is four instructions ahead of
`$82:F3A9  LDA #$F4EF : LDY #$0082 : JSL $80:8475`, and `$82:F4C6  LDA $18 : BEQ
$F4AF` is the body waiting to be stood on.

**And it is never drawn.** `--records` does not show it in any sampled frame of
the movie that calls it seven times, and neither does a sweep of all 24 live
handler words at six different frames. Every other handler in the registry
belongs to something with a display record; this one belongs to a trigger. That
is also why `f4ef_ignore` is untaken and hard to aim at — with nothing on screen
to stand next to, there is no way yet to make a shot fly over it.

Its cycle budget is **118 exactly, on all seven calls** — the only entry in the
registry with no spread at all, because sixteen bytes of comparisons have nothing
to be slow about.

### The route, and three things level 21 does that nothing else did

The wall-anchoring from the previous section carried over and was needed again:
north to the ceiling at y=2280, east to x=504, north to y=2112, west to the wall
at x=242, and the shaft at x=364 entered by backing off the x=504 wall for
exactly seventy frames. Three new obstacles came with it.

**A pickup parks `$7E:1CBC` at `$000F` for about 460 frames.** With no buttons
pressed at all the word goes `$0000 → $000F` on the frame the object is collected
and back to `$0000` 462 frames later, and for that whole window the player can
neither fire nor change weapons. Six presses of B spaced sixty frames apart put
five of them inside it and only the last did anything, which reads exactly like
the input-timing problem the previous section found — and is not. Wait for the
window and one press is enough.

**The shaft at x=364 is one-way.** The player climbs through y=1438 going up and
is stopped dead by it coming down: `Down` held for a thousand frames does not
move. That is the second one-way passage the project has found after level 25's
escalators, and the first that would have stranded a movie — this one never
returns south, and does not need to, because there are three `$81:9B6B` creatures
at (400,1220) and (308,1102), a hundred pixels from the weapon.

**Two tiles north of (404,1334) the player stops firing entirely.** Held `Up`
from there reaches (404,1302) and no shot ever leaves — the ammo word does not
move across 550 frames at step 1, with either weapon. It is not the weapon and it
is not the ammo; it is the two tiles. So the fight is not a spin but a **tap**:
three frames of a direction to turn, then `Y` held for thirty-seven, four times
around. That is a third fight shape after the box walk and the spin, and the one
to reach for when the ground the player can shoot from is a single tile.

### Ten of thirteen, and the three misses have one cause

| perturbation | result |
| --- | --- |
| `INC` the frozen counter instead of the bubbled one | CAUGHT |
| no increment at all | CAUGHT |
| increment by two | CAUGHT |
| skip the splice | CAUGHT |
| `$18` stored one word over | CAUGHT |
| `$18` stored `arg + 1` | CAUGHT |
| no store | CAUGHT |
| carry set on the store path | CAUGHT |
| Z clear on the store path | CAUGHT |
| A rewritten on the way out | CAUGHT |
| slot forced to the first player's | **MISSED** |
| side taken from bit 14 instead of bit 15 | **MISSED** |
| `$82:F4EF` answering `$05` only, not `$06` | **MISSED** |

**All three misses are the same missing input**, which is the useful part: only
one player ever fires in this movie, so the side arithmetic has one value to
produce and the id `$06` never arrives. A two-player level-21 movie catches all
three at once, and it is the same input `freeze_slot_1` has been waiting for
since `enemy_freeze` was written. Three lines that cannot be checked by any input
in the corpus, priced at one movie.

### Corpus, and the count that changed shape

**3,200,321 calls checked across 35 movies, 0 diverged; branch coverage 174 of
246, 72 untaken by every input, census empty** — from 168 of 242 and 74.

The denominator moved this round, which it has not done often, so the accounting
is worth spelling out. Four sites the *whole corpus* had never taken are taken
now: `d9b6b_fatal_id`, the one the movie was built for, and three the route
collected on the way — `a264_flag_set` and `a264_give_up`, which are a victim
being claimed and then giving up on level 21's long climb, and
`player_hurt_alt_ignored`, which is the player being hit by the second kind of
hit while already in the window for one. Four sites are new: `f4ef_player` and
`d9b6b_bubble_slot_0` arrive taken, `f4ef_ignore` and `d9b6b_bubble_slot_1`
arrive untaken and are both priced above.

`zamn_cosim run` substitutes all forty-six routines on the movie and finds **no
byte of live game state differing on any of 6,689 compared passes**, at most 39
bytes at once and all of them in the stacks or a declared scratch byte.

## The player who was never player one

The previous section ends with three perturbations that no input could
distinguish and one sentence saying what would: *a two-player level-21 movie*.
That sentence was wrong about the movie and right about everything else. There
is no such movie — the level cannot hold two players — and the input that
catches all three is a **one-player game whose one player is player two**.

No port code changed this round. What changed is that three lines of
`src/port/collide.c` which had never been checked by anything are now checked,
and the exercise that finds such lines is a script instead of a habit.

### Why the obvious movie does not exist

Two reasons, and the second is the one that closes the question rather than
merely blocking the attempt.

**The camera tethers the players to about 176 pixels of each other**, and level
21's route north is a sequence of one-tile shafts. That number is measured rather
than assumed: give both players the same inputs and the one that spawns thirty
pixels east is stopped by a wall at the first shaft mouth while the other climbs,
and the climber halts at `296,2648` with its partner at `326,2824` — 176 pixels
apart, and neither moves again for four hundred frames.

Aligning the second player and staggering the two by sixteen frames gets both
into the shaft and fails differently. The one in front ends the run **stranded at
`x=378` beside a shaft at `x=364`**, having reached the entrance at the same
moment the one behind it did, while the one behind climbs away. Reading that as a
shove is a reading and not a measurement — nothing here watched a push happen —
but whatever the mechanism, two players cannot both use this staircase.

**And level 21 places exactly one type `$04` object.** `zamn_assets actors <rom>
22` lists nineteen objects and one of them is the bubble gun, at (485,1340). No
arrangement of two players puts that weapon in the second one's hands, so even a
route that solved the tether could not have produced a `$805E` shot.

### Answer the player-select window on port 2 alone

The password goes in on port 1 exactly as every password movie types it. Then
the four Start presses that dismiss the player-select screen are aimed at
**port 2 and nothing else**, and Zeke never joins. `--pos` prints `--,--` in the
first column for all 6,700 frames; the player it does print is the page whose
`$64` holds `$1CEC`, which is the second inventory base.

That one change reaches further than the three lines it was aimed at, because
the game asks *which player* in more places than anybody had listed:

| what the game reads | with player one | here |
| --- | --- | --- |
| the player record's `ACTOR_COLLIDE_ID` | `$05` | `$06` |
| a shot's collision id | `$5E` | `$805E` |
| `score_slot`'s answer | slot 0 | slot 2 |
| `$81:9BA2`'s side, `$81:8493`'s side | `$0000` | `$0002` |

Every handler that names a player by id is offered the other one, for the whole
movie, without two players ever having to be in the same place at the same time.
`f4ef_player` reads 30 here and every one of them is `$06`; on
`movies/level21-bubble.zmv` every one of them is `$05`, and that is the whole
difference between a site being taken and a branch being checked.

### Three frames, and each of them is thirty pixels

Julie spawns at (288,2836) where Zeke spawns at (258,2836), so the route is
`movies/level21-bubble.zmv`'s with three frames moved. What is worth recording is
which three, because it is the same rule the level-9 round arrived at from the
other direction: **a leg that ends at a wall survives a thirty-pixel offset and a
leg that ends on a stopwatch does not.**

* **2704, not 2719.** The opening leg east is four frames rather than nineteen.
  Both end at `x=296`, the mouth of the first shaft, and every wall-anchored leg
  after it keeps its original frame unchanged.
* **3985, not 4010.** The climb up the shaft at `x=364` covers about 24 more
  pixels in the same time — the two position traces are identical to the pixel
  as far as `y=1814`, where Zeke is held up by something Julie walks past — so
  the turn east comes 25 frames sooner. This one is worth a frame of care: at
  3990 the player finishes at (463,1304) against a wall with the object
  uncollected, and at 3985 it finishes at (504,1338) with `$7E:1CF0` reading
  `$0040`. Twenty-five frames of slack, and five of tolerance inside them.
* **`Down` held from 4790.** The tap-and-fire phase **drifts north about two
  pixels a cycle** and above `y≈1310` the player stops firing altogether, which
  is the same two-tile dead zone the previous section measured. Starting from
  `y=1338` instead of `y=1336` was enough to lose 35 of the 40 shots. `Down`
  held for the twenty frames before the first tap seats the player on the floor
  at `y≈1372` and all forty land. An anchor is cheaper than a retimed fight.

### Three misses, three catches

| perturbation | result | where |
| --- | --- | --- |
| slot forced to the first player's | CAUGHT | `$7E:1FDC`: ROM `$00`, port `$01` |
| side taken from bit 14 instead of bit 15 | CAUGHT | `$7E:1FDC`: ROM `$00`, port `$01` |
| `$82:F4EF` answering `$05` only, not `$06` | CAUGHT | `$7E:0618`: ROM `$06`, port `$00` |

The first two fail at the **same byte**, which is the clearest statement of what
they were: `$7E:1FDC` is the *first* player's MARTIAN/BUBBLED tally and
`$7E:1FDE` is the second's, so both wrong spellings of the side put the count in
the wrong array slot, and with only Zeke on the board both wrong spellings and
the right one landed in the same place. The third fails on the trigger's own
direct page — `$7E:0618` is `$18` at page `$0600` — which also locates a routine
that has never appeared in a display list.

### The exercise is a script now

`tools/perturb.py` runs the table above: apply one edit, rebuild, `verify`,
restore, rebuild. It exists because the hand-done version acquired the same two
bugs more than once, and both are now impossible rather than merely known:

* **It anchors uniquely.** `str.replace(old, new, 1)` edits the first match in
  the file, and this port has nine near-identical copies of the collision
  subsystem — three lines that look unique inside one routine are verbatim the
  same three lines four hundred lines above it. Two perturbations came back
  MISSED that way, having broken a routine the movie never runs. Every anchor is
  counted first and a count that is not one is a hard error with that sentence
  in it.
* **It restores the binary, not just the file.** An earlier round spent an hour
  on a divergence in a correct tree, because the source had been put back and
  `build/zamn_cosim.exe` had not. The rebuild is in a `finally`, so an
  interrupted run leaves the tree and the binary agreeing too.
* **And it restores the file byte for byte.** The edit is done in `bytes`,
  because Python's text mode turns CRLF into LF on the way in and would write LF
  back out — a whole-file reformat, from a tool whose entire job is to make one
  line wrong and then put it back exactly. The check is a hash of the file before
  and after, and it matches.

```
python tools/perturb.py --list
python tools/perturb.py f4ef_p2_only
```

A perturbation is CAUGHT when `verify` exits non-zero, and the first divergence
it printed is echoed beside the verdict, because *where* it failed is most of
what the exercise is for.

### Corpus

**3,336,969 calls checked across 36 movies, 0 diverged; branch coverage 175 of
246, 71 untaken by every input, and no census section on any movie** — from
3,200,321 across 35 and 174 of 246.

One site moved, and it is the one the movie was built for:
`d9b6b_bubble_slot_1`. The new input adds no site of its own, because a port with
no new routine in it has no new decision to mark, and it takes 70 of the 246 —
every one of the other 69 already taken by something else. That is the same shape
`movies/level25-2p.zmv` had and the same reading: **a coverage table counts
decisions the code makes, and cannot count a value that flows through a decision
only one way.** Which is exactly why the perturbations are run as well.

`run` on the new movie substitutes all forty-six routines with **no byte of live
game state differing on any of 6,689 compared passes**, and `-r none` — two stock
cores, the control that has to pass before anything else here means anything — is
**identical at all 6,689 of them**.

## The neighbour nobody had rescued

`victim_a264_collide` is `$83:A264`, and four of its eight branches had been
taken by the corpus for several rounds: `a264_give_up`, `a264_ignore_low`,
`a264_shot_clears` and `a264_flag_set`. The two that had not were the two that
matter — `a264_claim_a` and `a264_claim_b`, the ids `$05` and `$06`, which is to
say **nothing in the project had ever rescued one of these neighbours.** Both are
taken now, by two movies that differ in one thing.

### Finding the level, which is a question about data and not about play

A neighbour's collision handler is not a property of the neighbour, it is a
property of its *behaviour*, and the behaviour is in the level's own victim list.
`zamn_assets actors <rom> <record>` prints it, so the search is a sweep of the
data rather than a sweep of the movies:

```
for i in $(seq 1 56); do zamn_assets actors rom.sfc $i | sed -n '/victims/,/objects/p'; done
```

Nine of level 21's ten neighbours run `$83:993D`, `$83:9776`, `$83:9A89` or
`$83:9EBE`. Victim list entry **3, at (237,2726), runs `$83:9699`** — and that is
the one whose collisions arrive at `$83:A264`. The other direction confirms it:
`--records` over sixteen movies, one per level, finds `$83:A264` on **level 21
and nowhere else** — six sightings on that sweep, against fourteen of the
ordinary `$83:A364` on the same movie.

**And the handler is a state, not a label.** The same display record `$1A8E`, at
the same coordinates, on page `$0600`, answers at `$83:A364` at frame 2700 and at
`$83:A264` from 2740. So a level that places a `$83:9699` neighbour is necessary
and not sufficient; the movie has to arrive while it is in the second state.

### Seventy frames

The corpus had reached this routine before. `movies/level21.zmv` — a probe with
no route in it — walks the column at x=242 and passes (242,2728) at frame **3180**,
which is one pixel-lane from the neighbour. The record disappears at **3110**.
Seventy frames, and that is the whole difference between the two branches the
corpus had and the two it did not: the probe takes `a264_give_up`, because the
level got there first.

The route that beats it is eighty frames long, and both of its legs end at a wall:

```
2700  Left ten frames from the spawn at (258,2836) to x=238
2710  Up the open column, into the neighbour at (237,2726)
```

### What a claim writes, and the one word that differs

`movies/level21-rescue.zmv` is that walk with a firing phase in the middle;
`movies/level21-p2-rescue.zmv` is the same walk made by **the other player**,
using the port-2-only trick the previous round found. Julie spawns thirty pixels
east, so the opening leg is twenty-six frames rather than ten and the climb is
unchanged — a leg that ends at a wall survives the offset.

Each takes its own claim branch **25 times**, the same count, over the same
twenty-five frames of overlap. Three words are written, and only the first of
them differs:

| | `$7E:0618` claimant | `$7E:061E` event | `$7E:605D` flag |
|---|---|---|---|
| `level21-rescue.zmv` (id `$05`) | `$0005` | `$0001` | `$80` |
| `level21-p2-rescue.zmv` (id `$06`) | `$8000` | `$0001` | `$80` |

The claimant is the three-byte saving `victim_collide` makes one page over:
`$83:A293  LDA #$8000` falls into `$83:A296  STA $18`, and id `$05` enters at the
`STA` with the id still in the accumulator. One side latches a bit and the other
latches its own id, and `score_add` reads bit 15 of the word, so both spellings
work.

The flag byte is the same in both because it is not the player's, it is the
neighbour's: `$81:8191` writes one byte of `$80` at `$7E:605A` indexed by the
victim's own list index, and this neighbour is entry **3**, so the byte is
`$7E:605D`. Watching the word at `$605C` shows it go `$0100` → `$8000`, which is
that byte and its neighbour and not a word write.

### A shot that cleared a give-up

The first movie stands still for 136 frames before walking in, and the reason is
in `$7E:061E`. Standing still lets the level catch up with the neighbour, and the
event word goes to **`$0003` at frame 2776** — the give-up value. A shot lands at
**2788** and `a264_shot_clears` puts it back to `$0000`. The claim at **2924**
then writes `$0001` over that.

The control is the same route without the firing phase: the player arrives at
2788 and the word goes straight from `$0000` to `$0001`, never touching `$0003`.
Reading the three transitions as *a shot undoing a give-up already in flight* is
a reading and not a measurement — `a264_give_up` counts **0** on both movies, so
the `$0003` was written by the neighbour's own unported behaviour and not by the
handler — but the transitions themselves are measured, and this is the only
handler in the project where a weapon shot *clears* a verdict instead of casting
one.

### Three perturbations, and the one that disagrees with itself

| perturbation | `level21-rescue.zmv` | `level21-p2-rescue.zmv` |
|---|---|---|
| claimant latched `$8000` for both ids | **CAUGHT** `$7E:0618`: ROM `$05`, port `$00` | **MISSED** |
| the claim writing the give-up event | **CAUGHT** `$7E:061E`: ROM `$01`, port `$03` | **CAUGHT** same bytes |
| the flag byte written at index 0 | **CAUGHT** `$7E:605A`: ROM `$00`, port `$80` | **CAUGHT** same bytes |

The first row is the point. **One broken line, two inputs, opposite verdicts** —
because the perturbation makes the port do what the second player's claim already
does correctly, so the movie where Julie rescues the neighbour cannot see it. That
is the coverage report's whole thesis stated as a pair of verdicts rather than as
a count, and it is why `tools/perturb.py` now takes a *list* of inputs per
perturbation and runs all of them. A perturbation caught by everything it is
offered says less than one caught by exactly one thing.

`$7E:0618` is also where the previous round's `$82:F4EF` perturbation failed, on
a different movie. It is `$18` on page `$0600`, and page `$0600` is a thread
page — level 21's `$83:9699` neighbour is its tenant here, and something else was
its tenant there. The address names an offset on a page, not a routine.

### Corpus

**3,429,877 calls checked across 38 movies, 0 diverged; branch coverage 177 of
246, 69 untaken by every input, and no census section on any movie** — from
3,336,969 across 36 and 175 of 246. The two movies add 46,885 and 46,023 calls,
which is the whole difference to the digit.

Neither movie adds a site of its own — no port code changed this round — and
between them they take 58 of 246, two of which nothing else has. `run` substitutes
all forty-six routines on both and finds **no byte of live game state differing on
any of 2,989 compared passes** on either, and `-r none` is **identical at all
2,989** on both.

Two of the handler's eight branches are still untaken and each names its own
problem. `a264_flag_none` wants a neighbour carrying `$FFFF` in the index word at
`$06`, and nothing the corpus has put on the board carries one. `a264_ignore_named`
wants id `$02` or id `$5E` — `$5E` is a bubble-gun shot from the first player, so
it wants a level with both a `$83:9699` neighbour and a type `$04` object. Of the
fourteen levels the corpus can start on — level 1 and the thirteen a password
reaches — exactly two have both: level 21, where the gun is up a one-way shaft
north of a neighbour that dies long before, and **level 41**, where the gun at
(1044,599) is on the same side of the map as the neighbour at (1580,548). That is
the next round's route, and it is not a cheap one: `tools/fit_route.py` stopped at
(1242,861) after 43 legs, 500 pixels short.

### What the level-41 route needed first, which nobody had checked

That plan has a prerequisite the paragraph above does not mention, and it is not
met. **`$83:A264` is a state and something has to put the neighbour into it.**
`$83:9699`'s idle loop reads its event word and dispatches; the `$0002` arm is
`$83:9712  JSR $A21D`, and `$83:A21D` swaps the record's sprite for `$0090`,
installs `$83:A264` over `$83:A364`, and — if nothing answers — writes the give-up
value itself at `$83:A23D  LDA #$0003 : STA $1E` and waits `$012C` frames before
putting `$83:A364` back. So the window is bounded, it is entered from one
place, and what opens it is a collision carrying `VICTIM_ID_EVENT_2` — id `$0B`,
the one arm of `victim_collide` with an ending to itself. The only 16-bit
immediate `$000B` in the actor bank is `$81:FD5B`, which puts a record on the
board carrying it, reached from `$81:FCC9` behind a `JSL $80:AF2C` position test — which reads as *something walking onto the neighbour*, though
that last step is a reading and the rest is not.

`verify -c` counts that from the other end and the two levels disagree
completely:

| movie | `victim_event_2` |
|---|---|
| `movies/level21.zmv` | 5 |
| `movies/level41.zmv` | **0** |

and `--records` agrees over the whole 4,700 frames — level 41's neighbour at
(1580,548) answers at `$83:A364` in every sampled frame from 2580 to 4440, on
three different pages as the camera culls and rebuilds it, and never once at
`$83:A264`. **The route was never the expensive part of that plan; the level
simply never menaces this neighbour.** Level 21 does, and there the gun is up a
one-way shaft 1,300 frames away from a window that lasts about 340. So
`a264_ignore_named` is not a route waiting to be cut, and the `$5E` half of it is
closed on every level a password reaches.

## Six branches nobody could reach, and the pen they were in

`enemy_d7f6_collide` is `$81:D7F6`, and **six of its seven branches had read zero
on every input ever recorded.** The routine has been in the registry since the
round that added `--records`, which found it on level 17 at (563,513) and
identified the actor from the level list — actor 11, type `$23`, behaviour
`$81:D704`. Four rounds of level-17 movies have walked past it since. Not one of
them ever hit it, and the reason is not a route that nobody cut.

### The instrument that says why a route does not exist

`route` answers "no route" for two completely different reasons and had no way to
say which: the goal is inside scenery, or the goal is open ground with nothing
leading to it. `--reach` tints the map per grid cell — green reached, **orange
open but cut off**, red solid — and level 17 is the case the distinction was
missing for:

```
 496 ######################################
 504 #######ooooooooo###ooooooooo##########
 512     ###ooooooooo###oGooooooo######
 520     ###ooooooooo###ooooooooo######
 528     ##############################
```

Two alcoves of clear floor, solid on all four sides, with the creature in the
right-hand one and the level's objects at (521,505) and (577,505) inside them
too. **The creature is not merely hard to reach; there is no way in.**

Nor can it be shot over the wall. Standing at (586,462), the closest the player
can get, `--records` shows the same thing every frame: a `$5C` record spawning at
(586,462) with handler `$81:FE0E` and sitting at (586,468) with handler
`$00:0000` on the next — a shot that travels six pixels and splashes — while the
creature, which chases, sits at (586,504) with `d7f6_ignore` counting 90 and
`d7f6_hit` counting nothing.

### The level the handler was on all along

The handler belongs to **behaviour `$81:D704`**, not to level 17, and the actor
sweep finds six levels that place one: 5, 17, 29, 31, 49 and 54. Of the four a
password reaches, `--reach` puts level 49's creature at (651,314) in a pen of
exactly the same shape as level 17's, and level 29's is 430 cells from the spawn.
**Level 5's, at (985,120), is standing in the open, 166 cells away.**

`tools/fit_route.py` walks it in **nine legs on its first run with no hand-tuning
at all**, arriving at (926,123) on frame 3208 — which is what an open level costs
against the twenty-three and forty-three legs the maze levels have wanted. Then
Right and Y held, and the fight is over in fifty frames.

### One health, and the ratio that says the counts are right

`--watch 050C` reads the creature's health word straight off its thread page:

```
  3140  $0001      seeded
  3235  $0000      d7f6_survived   — 1 - 1 does not borrow, and 0 != 1
  3246  $FFFF      d7f6_died
  3258  $FFFE      and again
  3270  $FFFD      and again
```

Four hits, one survivor, three deaths, and the squirt gun's damage-table entry is
1. The report says `d7f6_hit` **20**, `d7f6_died` **15**, `d7f6_survived` **5**,
against **4** calls on the routine's own row — five interceptions per call, which
is the note beside the coverage table doing exactly what it says. The *ratio* is
what is measured, and it is 1:3 in both places.

`d7f6_survived` is the odd one of the three: with one health the only way a hit
leaves this creature alive is a damage of exactly one, so "survived" here means
the hit that took it to zero, and the kill is the hit after.

### And two of the seven are now known to have no input

Same argument as the seven `*_no_damage` sites: name the id the branch needs,
then ask whether any reachable level places the object that carries it.

* **`d7f6_fatal_id`** wants `$5E`, object type `$04`. Of the six levels that place
  behaviour `$81:D704`, **exactly one also places a type `$04`** — level 49, at
  (105,86) — and level 49's creature is the one in the pen. Levels 31 and 54 place
  neither a `$04` nor a password.
* **`d7f6_no_damage`** was already closed two rounds ago, for the reason all seven
  of its family were: no shot can carry a zero-damage id here.

**`d7f6_special` is the one that is still work rather than closed.** It wants
`$5D`, object type `$02`, and four of the six levels place one — but level 5's is
sealed in a basement pocket at (667,1068), level 17's is reachable next to a
creature that is not, and level 31 has no password. **Level 29 has both**: the
type `$02` at (756,730) is on a blocked cell with reachable ground beside it at
x=751, and both of its `$81:D704` creatures route from the spawn.

### Corpus

**3,482,740 calls checked across 39 movies, 0 diverged; branch coverage 180 of
246, 66 untaken by every input, and no census section on any movie** — from
3,429,877 across 38 and 177 of 246. `movies/level5-d7f6.zmv` adds 52,863 calls,
which is the whole difference, and it reaches 49 sites of which **exactly three
are sites nothing else in the corpus has**: intersecting its untaken list with the
corpus's names `d7f6_hit`, `d7f6_died` and `d7f6_survived` and nothing else. No
port code changed this round. `run` substitutes all forty-six routines on it and
finds **no byte of live game state differing on any of 3,489 compared passes**,
and `-r none` is **identical at all 3,489**.
