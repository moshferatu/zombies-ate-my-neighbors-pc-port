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

The cost is worth stating plainly: **inside the stack area, `run` proves
nothing.** Everything outside it — 126 KB of actual game state — is compared
byte for byte, and `verify` covers the routines' own behaviour exactly, stack
included.

The area is `$7E:0D00-$7E:12FF`, and it is read out of the ROM rather than
declared: `$80:830E` holds the 24 initial thread stack pointers, and they turn
out to be 24 values 48 bytes apart. This document used to say `$7E:1000` here,
which left sixteen of the twenty-four stacks outside the excuse — see *What it
actually was* at the end.

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

### A claimed flag can be claimed from the wrong instruction

The rule above says to name the instruction each output comes from. The
targeting family — `$80:B093` and the five routines around it — is where that
stopped being a documentation habit and started catching things.

`actor_bearing` (`$80:B22A`) ends by turning a comparison into a table index:
`CMP $38 : BEQ : TXA : ADC #$0001 : TAX : LDA <table>,X : AND #$00FF : PLD :
RTL`. Reading the last comparison and calling it the routine's carry is the
obvious move and it is wrong on every path but one. **`ADC` writes carry whether
or not anybody wanted an arithmetic answer from it**, and the sum here is an
index of at most ten, which never carries — so the comparison's verdict survives
only on the `BEQ` path that skips the addition. Carry out means *the record
shares the point's X*, which is the opposite of what the `CMP` under it appears
to say. 2,078 calls on `movies/level17-weapon.zmv`, 2,078 diverging, `flag C:
ROM 0, port 1`.

Three things about this are worth keeping:

* it is the same shape as the `PLD` trap that `actor_nearest`'s shim records —
  an output taken from the instruction that *computed* it rather than from the
  last one to *write* it — arrived at through a different opcode, which suggests
  the shape rather than the opcode is the thing to look for;
* the four sibling routines that share the same tail all passed on the first
  run, including `player_bearing`, whose carry comes from an `ASL` two
  instructions further on and is clear for exactly the same reason. Getting one
  of six wrong is what a per-call diff is for;
* nothing in the game reads it. The bug was unobservable, in the sense that no
  input could have produced a wrong pixel from it, and it was still a wrong
  statement about what the ROM does. That is the standard the flag masks exist
  to hold, and lowering it to "does it matter" would have cost the check.

### An existence check that works by accident of where WRAM is

`$80:AFFB` opens by making sure both players exist, and writes it as

```
$80:AFFB  BIT $00D2
$80:AFFE  BEQ <no such player>
$80:B000  BIT $00D4
$80:B003  BEQ <no such player>
```

`W_PLAYER_A_RECORD` and `W_PLAYER_B_RECORD` are pointers into the actor slot
table, or zero for a player who is not in the game, so reading that as *test
each against zero* takes about a second. It is not what the instruction does.
**`BIT abs` sets Z from A AND memory**, not from memory — and A here is the
caller's own record pointer, which it was handed in `$08`. What is actually
being asked is whether the caller's record and the player's record have a bit in
common.

It gives the right answer every time, and the reason is the address map.
`W_ACTOR_SLOTS` is `$185E` and there are 32 records of `$14` bytes, so every
record pointer in the game lies in `$185E..$1ACA` — entirely inside `$1800`, and
so every one of them has bits 11 and 12 set. Any two therefore share at least
`$1800` and can never AND to zero. The only zero available is the literal
`$0000` that means *no such player*, which is exactly the case the branch is
looking for.

So the check is correct, and it is correct for a reason that is nowhere near
the code. Move the slot table to a page that does not straddle `$1800` and this
routine starts reporting a missing player at random, in two-player only, ten
frames after somebody stepped on a particular tile. The port reproduces the
`AND` rather than the intent, and `port/step.h` says why at the site.

### The eleven-movie profile set is not the 43-movie corpus

Two of this round's five routines executed **zero times** across all eleven
profiles the ranking tool reads. That is what the profiles are for, so it was
tempting to conclude the corpus could not reach them either — and this document
briefly said so.

It is wrong, and the branch-coverage list is what said so. Eleven of this
round's seventeen new sites came back taken corpus-wide, including both of
`terrain_tile_bit3`'s. Hunting the movies down: `level5`, `level21` and
`level21-spin` call it 426 times between them, and none of those three is in the
profile set. `partner_near` is called three times.

The lesson is about which measurement answers which question. The profile set is
eleven movies chosen to be *representative of work*, and `native_share.py` ranks
against it because a routine's share of instructions is what makes it worth
porting. The corpus is 43 movies chosen to be *representative of behaviour*, and
it is the only thing that can say whether a path is reachable. Reading a zero in
the first as an answer to the second is a category error, and it cost the two
cheapest cycle budgets in this round being guessed instead of measured.

### One mechanic, seen from both ends, and a funnel that measures itself

`terrain_tile_bit3` and `partner_near` are not two routines that happen to be
rarely called. They are the two ends of the same thing:

1. `$80:E855` re-divides a mover's position after a step commits and asks
   `terrain_tile_bit3` about the tile it landed on.
2. Bit 3 set, so `$80:E021` installs the state body `$80:E035` with `$16 = 10`
   — ten frames of nothing at all.
3. `$80:E035` computes a point **100 pixels ahead in the facing direction**
   (`$80:E06C`, `$FF9C` and `$0064` on each axis) and hands it to `partner_near`.
4. Near, and the mover enters state `$06`; far, and it is as if nothing had been
   asked.

A hundred pixels forward, vetoable by the co-op leash *before* it happens rather
than after. 410 tiles across the 55 levels carry bit 3, about seven per level,
and the call counts are the funnel: **426 steps onto one of those tiles, three
that got as far as the jump.**

The veto is only real in two-player, because with `$D4` zero the `BIT` at the
top answers *near* and a lone player always goes — and all three corpus calls
take exactly that exit, eleven instructions in, measuring nothing. Hence
`partner_near` at 165 cycles, the cheapest entry in the registry, and hence six
of its seven sites untaken: the corpus has five two-player movies and not one of
them has ever stood on one of those tiles. That is a coverage gap with a name
and a recipe, which is a better thing for the untaken list to carry than
silence.

### A correction to what this document said about `$80:AF2C`

The round that ported `$80:AE97` looked ahead and wrote:

> Two more copies of the same loop exist and are not ported: `$80:AF2C` tests
> bit 2 (after calling `$80:B422` first), and `$80:AF66` tests bit 12.

Half right. `$80:AF66` is the loop, six probes and the same `(9, 8)` origin bias
and the same offsets. `$80:AF2C` is **not a loop at all** — it is a single probe
on the tile the point lands in, with no origin bias, which makes it
`tile_attrs_at_pixel` with a mask rather than `terrain_blocked` with a mask.
Two of the four routines this round are that shape and a third is halfway there,
so the family is smaller and flatter than the earlier note implies: 1,046 to
1,086 cycles for `$80:AF2C` against `terrain_blocked`'s 678 to 1,776.

### The ranking's top row was 90% something the harness cannot call

`native_share.py` attributes every executed instruction to the **nearest
preceding declared subroutine entry**, which is the only thing it can do with a
profile and a symbol file. It works, and once a round it does not.

This round's top portable row was `$81:BC3D` — 581,911 instructions of work over
1,775 calls, which reads as 328 instructions per call. The routine is 92 bytes
and has no loop in it. Both facts cannot be true, and the way to find out which
is to stop trusting the attribution and go back to the profile:

```
$ region.py 81 BC3D C000        # every offset with a non-zero *call* count
entry             calls    exec(sum)      per
$81:BC3D         1,775      581,911      327
$81:BFA8            32          359       11
```

Only one entry in 900 bytes, so the attribution had nowhere else to put
anything. Then the per-byte histogram:

```
total 581,911 over 301 live bytes
  $81:BEE2   7,465  1.3%      $81:BF82   7,436  1.3%
  $81:BEE0   7,465  1.3%      $81:BF7F   7,436  1.3%
  ...
```

Three hundred bytes each executed about 7,400 times, flat, with no entry point
in front of them. That is not a subroutine — it is an actor **thread body**,
reached by `RTL` from the scheduler, and it is in exactly the category the
`BLOCKED` list at the top of `native_share.py` exists for: nothing calls it, so
there is no call to intercept. `$81:BC3D` itself is a cheap little terrain probe
that happens to be the last declared symbol before it.

The fix is a second ranking that does not trust the symbol file at all. Call
counts in a profile are non-zero **only at real entry points**, because that is
what a `JSR`/`JSL` target is, so:

* take every offset with a non-zero call count that is not already in the
  registry;
* charge it the instructions from there to the next such offset, capped;
* rank by that.

Every row this produces is a routine somebody actually calls, and the top of it
looks nothing like the top of the other one:

| entry | work | calls | per call |
| --- | --- | --- | --- |
| `$80:CCBF` | 1,024,442 | 255,859 | 4 |
| `$81:8024` | 540,347 | 22,283 | 24 |
| `$80:9C90` | 364,925 | 29,436 | 12 |
| `$80:F327` | 346,216 | 31,971 | 10 |
| `$80:9D5B` | 208,402 | 37,151 | 5 |

Those are this round: five routines, 2.48M instructions, **0.86% of everything
the game does that is not a wait** — four times the share the previous round
moved, for less code. None of them was visible on the old ranking's first
thirty rows, and the reason is structural rather than accidental: a small
routine called a quarter of a million times is invisible to nearest-preceding
attribution whenever it happens to sit downstream of a big declared symbol.

The prediction is worth keeping because it came true to the decimal. 2,484,332
of 333,472,542 instructions is 0.745%, and the dynamic share afterwards moved
**61.2% → 61.9%**; on the waits-removed denominator the predicted 0.86% showed
up as 70.7% → 71.6%. When a ranking says what a round will be worth and the
round is worth that, the ranking is measuring the thing it claims to.

The two rankings are not competitors. The old one answers *where is the work*
and will keep surfacing thread bodies and dispatchers, which is correct and
useful — they really are where the work is. The new one answers *where is the
work the harness can take*, which is a different question and the one a porting
round is actually asking.

### `$00DE` is a weighted census, and every spawn in the game waits on it

`$80:9D5B` is six instructions and fifteen bytes:

```
LDA $00DE : CMP #$008A : BCS out : LDA $0006 : CMP #$0012
out: RTL
```

`$0006` is `W_THREAD_COUNT` and the ceiling is 18 of the scheduler's 24 slots.
`$00DE` had no name: absent from `docs/wram-map.md` entirely, and present in
`zamn.sym` only as a raw address inside somebody else's comment
(`$82:9569 level_intro_9569  # 3 levels: adds #$0028 to $00DE`). Finding out
what it is meant reading its **writers** rather than its readers. There are 137
of them — 6 in bank `$80`, 74 in `$81`, 22 in `$82`, 35 in `$83` — and every one
has the same shape:

```
$81:87F8  CLC : LDA $00DE : ADC #$0014 : STA $00DE
$81:885B  SEC : LDA $00DE : SBC #$0014 : STA $00DE
$81:C202  CLC : LDA $00DE : ADC #$001C : STA $00DE
$80:C9E4  CLC : LDA $00DE : ADC #$0001 : STA $00DE
```

Each kind of actor charges its own weight on the way in and refunds it on the
way out. Tabulating the immediates settles that it is a ledger rather than a
coincidence: **21 distinct weights from `$01` to `$28`, and the charge and
refund histograms match value for value to within one site each.** Nothing else
writes the word — the only other access in the ROM is the single `STZ $00DE` at
`$80:867B` in the level-init chain.

So `$00DE` is not a population, it is a **load**: 138 is a budget for how much
the board is *worth* rather than how much of it there is, and the heaviest
single actor spends 40 of it.

What makes this worth a section is the shape of the caller. `$81:80EC
actor_list_spawn` does not check-and-give-up:

```
$81:8108  LDA #$0001 : JSL thread_yield : JSL $809D5B : BCS $8108
```

— a frame at a time, forever, until there is room. 37,151 calls over eleven
movies, and **most of them are refusals**: a level's spawn list is not a
schedule, it is a queue that drains at whatever rate the players clear the
board. The busier the screen, the slower the next thing arrives, which is a
difficulty curve implemented as back-pressure and nowhere written down as one.

The comment that turned out to be the clue is also the sharpest illustration:
three levels open by charging `#$0028` — the heaviest weight there is — against
a budget of 138, so those levels begin with nearly a third of the board already
spent and spawn more slowly from the first frame. It is a difficulty knob with
no counter of its own, spelled as an actor that is not there.

### The sine table's one impossible byte

`$80:9C90` reads `$83:9431` — 360 signed bytes, one per whole **degree**, of
`round(128 · sin d)`. A degree table rather than the power-of-two-turn table
almost every SNES game uses, which is what makes the index register sixteen bits
wide: 359 does not fit in eight, and that is the only evidence anywhere for the
width. The harness's measured stack column agrees, at 2 bytes for the `PHX`.

One entry is `$FF`, at index 90, and it is a sentinel meaning **+128** — the one
value a signed byte cannot hold:

```
LDA $839431,X : CMP #$FF : BNE + : LDA #$0080 : BRA out
```

Spending `$FF` is free, and the table says why. One degree of arc is
`128 · sin 1° = 2.23`, so the first step either side of a zero crossing is ±2 and
**±1 never occurs anywhere in the circle**: there is no `$01` byte in the table
and no `$FF` but the sentinel. Both honest extremes are present — `$7F` fourteen
times across the plateau, `$80` once at 270 — so the one value the encoding
could not represent is the one value that is not needed. Two's complement's
asymmetry pays for one extreme and the sentinel pays for the other.

Three smaller things came out of it:

* **The flags belong to the `PHX` at the top.** Every path converges on `PLX :
  RTS`, and `PLX` sets N and Z from what it pulls, so a caller reads back flags
  describing *the index register it passed in*. Third routine in the registry
  with that shape, after `apu_play_sfx`'s `PLD` and `terrain_point_bit2`'s.
  Carry, meanwhile, survives from the `CMP #$FF` — so carry set means *this call
  hit the sentinel*, a fact no caller reads and the routine advertises anyway.
* **The caller sign-extends the answer a second time.** `$80:959A` follows the
  `JSL` with `BIT #$8000 : BEQ +2 : ORA #$FF00`, and bit 15 is set only on the
  path where the callee has already done exactly that `ORA`. Four instructions
  in an inner loop that cannot change a bit.
* **What the game wants a sine for is a screen wobble.** The one caller builds an
  HDMA table at `$7E:8000`, three bytes an entry, four degrees of arc per
  scanline, phase advancing a degree a frame. That is the whole of the game's
  trigonometry.

And the call count is the tell. `sin_deg` is called **2,676 times, to the call,
on every movie measured** — different levels, different lengths, different
inputs. Bisecting `level1` finds nothing before frame 900 and nothing after
frame 1,200: all 2,676 are one burst in the level-entry transition. A routine
whose call count does not depend on the input is a routine no input is driving,
and that is a thing very few rows in the registry can say about themselves.

### A routine whose cost is a straight function of who is playing

`$81:8024 nearest_player_dist` answers *how far is this point from whichever
player is closer*, as `max(|dx|, |dy|)`. That makes three distance metrics in
`port/step.h` and no two of them agree about which of two points is nearer:
`step_tether_blocked` uses the Manhattan sum, `partner_near` uses two
independent per-axis tests, and this one is Chebyshev. None is Euclidean.

The measured cost separates the corpus cleanly in two:

| movie | calls | mean cycles |
| --- | --- | --- |
| `level1` | 290 | 603 |
| `level25-lane` | 2,617 | 598 |
| `level9-weapons` | 2,205 | 609 |
| `level29-fighting` | 2,075 | 602 |
| `level1-2p` | 1,053 | **918** |

Four one-player movies inside eleven cycles of each other, and the two-player
one half again as expensive, because the second player *is* the second half of
the routine. Nothing else in the registry reads its input that plainly.

The absent player is not skipped, either: both scratch words are primed with
`$FFFF` first, so a missing player enters the comparison as the largest distance
there is and a one-player game falls through to A's answer without a branch
anywhere. That is why `nearest_no_b` is taken on every one-player movie and
`nearest_b_wins` needs two players to exist at all.

Its one caller is `$81:80EC actor_list_spawn` again, taking a linear minimum
over the level's spawn points: what spawns, spawns at **the eligible point
closest to a player**, decided fresh on every spawn and never by the level.

### One caller, 31,971 calls, and the second record is placed rather than copied

`$80:F327 actor_publish_pos` is the far end of the pipeline `port/step.h` opens
with `step_propose`. `$30`/`$32` are a thread's *own* idea of where it is; until
this routine copies them into the actor record, the mover has moved only in its
own opinion. It is the last line of `$80:CDF4`, the generic actor body, so the
order of an actor's frame is **sleep, think, publish**.

When `$1E` is non-zero the actor is two stacked records, and the upper one is
not a copy of the lower:

| field | lower (`$08`) | upper (`$0A`) |
| --- | --- | --- |
| `ACTOR_X` | `$30` | `$30` |
| `ACTOR_Y` | `$32` | `$32 + 1` |
| `ACTOR_Z` | untouched | *lower's* `ACTOR_Z` − 1 |

One pixel down closes the seam between two metasprites that ought to abut; one Z
in front is a tie the depth sort cannot break the wrong way. And the Z is read
back **out of the record**, not off the thread, so the upper half follows
whatever else moved the lower one this frame.

The two paths also use `$08` two different ways — `LDX $08 : STA $0002,X` on one
and `STA ($08),Y` on the other — which is free on the 65816 and is why they
share no code. The budget makes the split visible: 244 cycles for one record and
614 for two, so the call-weighted mean reads as *what fraction of this movie's
board is two records tall*, and it moves further between movies than anything
else in the registry (251 on `level25-lane`, 413 on `level1-2p`).

### Five instructions and a quarter of a million calls

`$80:CCBF apu_next_byte` — 255,859 calls across eleven movies, third in the
registry behind `sprite_frame_tile` (577,573) and `apu_send` (257,114) and ahead
of `thread_yield` (240,307). Landing within 1,255 calls of `apu_send` is the
shape of the caller rather than a coincidence: the uploader fetches a byte
through here and hands it straight to `apu_send`, so the two run in lockstep.

```
LDA [$18] : INC $18 : BNE +2 : INC $19
```

It is the byte source of the APU data-set uploader, and the only thing about it
worth recording is that the increment is done **eight bits at a time**. A 16-bit
`INC $18` would do the same arithmetic in one fewer instruction; because the ROM
does not use one, `$1A` is never touched, and a data set that runs off the end
of its bank wraps to `$xx:0000` rather than crossing into the next.

Its flags therefore describe the *cursor*, not the byte: 255 calls in 256 return
the low byte's N with Z clear, and the 256th returns the high byte's. Nothing
reads them — the caller's next instruction is `STA $1C` — and at roughly 23,900
calls a movie the wrap comes round some ninety times in each one, so an
unclaimed flag here would have been an unchecked output on a quarter of a
million calls.

### What the round measured

| | before | after |
| --- | --- | --- |
| registry entries | 93 | 98 |
| calls checked, 43 movies | 11,408,676 | **12,930,985** |
| routines diverged | 0 | **0** |
| coverage sites taken | 352 / 446 | **370 / 464** |
| ...untaken by every input | 94 | **94** |
| static share | 23.8% | 24.2% |
| dynamic share | 61.0% | **61.9%** |
| ...substituted only | 47.8% | **48.8%** |
| ...substituted, waits removed | 55.5% | **56.4%** |

The untaken column is the one to read twice. It did not move: eighteen sites
were added and eighteen more came back taken, so every branch this round wrote
is a branch some input in the corpus exercises. That has not happened before —
the previous round added seventeen and left six of them unreached.

`zamn_cosim run` reproduces the documented baselines to the pass on all four
movies — `level1` 2389/2389, `level29-fighting` 5989/5989, `level25-boss`
7589/7589, `level9-weapons` 8989/8989 — with every difference inside the stacks
or a declared scratch byte and no byte of live game state differing anywhere.
Framebuffers against `--stock` are identical on `level1`, `level1-2p`,
`level29-fighting` and `level45-race` at 6,000 frames, and on `level21-bubble`
and `level25-lane` at 2,600.

It is also the flattest thing in the registry: 134..202 cycles, **mean 138 on
every movie measured**, because there is one branch in it and it is taken once
in 256. Cheaper entries exist — the collision dispatchers bottom out at 40 — but
none of them is called a quarter of a million times.

### The thread body was not a thread body, and the fix took two rounds

The section above worked out that `$81:BC3D`'s 581,911 instructions were four
fifths somebody else's, called it "an actor thread body", and moved on to build
a second ranking rather than fix the first. Both halves of that were half right.

It is not a thread body. `$81:BEDA` is three instructions —

```
$81:BEDA  JMP $BEDD
$81:BEDD  LDA #$BEE3 : STA $12 : RTS
```

— reached by `JMP` from `$81:BB8F` and `$81:BBE7`, which are two of the
monster's own routines, and by nothing that *calls* it, so the CDL never marks
it as a subroutine and attribution-by-nearest-entry
walks straight back past it to `$81:BC3D`. What it installs in `$12` is the
monster's **next state**, which its thread then enters with

```
$81:C21E  PEA $C225 : LDA $12 : DEC A : PHA : RTS
```

— a computed `RTS` through a WRAM word. So this is a fourth family of code that
runs and is never called, after the vblank jobs, the thread bodies and the
one-off jump entries, and it is the only one of the four that cannot be found by
grepping the cartridge for an idiom: there is no `JSL thread_spawn`, no
`JSL vbl_queue_a_add`, nothing but the state word itself.

Declaring `$81:BEDA` in `JUMP_ENTRIES` splits the row where it belongs:

| | instructions | share |
| --- | --- | --- |
| `$81:BC3D` | 581,911 → **112,743** | 0.20% → 0.04% |
| `$81:BEDA` | — → **469,168** | — → 0.16% |

63 instructions per call over 1,775 calls is what a 92-byte leaf should look
like, and it now does. The state bodies *below* `$81:BEDA` are still charged to
it, because finding them means reading the states rather than matching a
pattern, so the row is a lower bound and is labelled as one.

### ...and the same thing again, caught by the check rather than by reading

`native_share.py` has carried an instruction since the `$80:A937` round:
**port a neighbour, and see whether the number moves by more than the neighbour
is worth.** This round is the first time following it caught something.

Registering `$81:C16B monster_anim` moved the native total by **291,771**
instructions where the routine itself executes 145,410. The other 146,361 starts
at `$81:C1FB`, and it is another thread body:

```
$81:C1FB  JSR $B9F9 : JSR $BA46
$81:C201  CLC : LDA $00DE : ADC #$001C : STA $00DE   ; the spawn charge
          LDA #$C440 : LDY #$0081 : JSL $808475      ; its collision handler
          LDA #$0001 : JSL thread_yield
```

**No instruction in the cartridge names that address.** Nothing `JSR`s or `JSL`s
it; no `LDA #imm : LDY #imm : JSL thread_spawn` matches it; it is in none of the
three tables `THREAD_BODIES` is built from. It is one of the four the comment
there already calls a lower bound — spawned by `$81:80E7` or `$81:81D7`, which
read a body's address out of WRAM — and it executes four times across the eleven
profiles, once per placement of the creature.

Without the check, this round would have claimed 146,361 instructions of a
thread body nobody had written a line of C for, which is 0.05% of the game and
about a third of what the routine above it is actually worth. Declared, and the
round's headline number is smaller and correct.

The general lesson is the one `$80:A937` taught and these are the fourth and
fifth instances of it: **a routine missing from the entry list does not score
zero, it scores somebody else's work** — and the somebody else is always the
nearest declared symbol above it, which is by construction a routine small
enough that nobody looked twice.

### A `memset` that outlives its frame

`$80:C05A` clears the sprite cache: `$FFFF` over 4,096 `frame_slot` words and
128 `slot_frame` words, then two globals naming the frame array. Five call
sites — three in the loader in bank `$80`, two in the boss code in `$82` — 31
calls across the corpus, and **16,900 instructions a call**: half a million
master cycles, which is a frame and a half.

So an NMI lands inside every single call, and the harness abandons all of them.
Two calls on `boot.zmv`, two on `level1.zmv`, four interrupted, none checked. It
is the third routine to be unregisterable for that reason rather than for want
of anyone writing it — `$80:CD20 lzss_decompress` and `$80:AD2B blockmap_expand`
are the other two — and the only one of the three that is not a loop over data.
It is 524,241 instructions of the corpus, a fifth of a per cent of everything
the game does, spent writing the same constant 4,225 times.

The C is written and the header carries the contract; `routines.c` says what the
shim would be and `native_share.py` lists the address in `BLOCKED`, so the share
it earns is zero on both sides of the report rather than zero on one. Two things
came out of reading it that are worth keeping either way.

Its first loop runs `$1001` times, not `$1000`: `DEX : DEX : BPL` runs the body
with `X` at zero too, so it clears `$2002` bytes and steps one word into
`slot_frame`, which the second loop rewrites four instructions later. And its
exit is `PLB : PLB : RTL` — `PEA $007E` pushes two bytes where `PLB` pulls one,
so there is a stray `$00` under the saved bank for the whole routine and the
last `PLB` is the one that restores the caller's own. **N and Z therefore
describe the caller's data bank** — one more routine in a list this document
keeps extending, after `apu_play_sfx`'s `PLD`, `sin_deg`'s and
`terrain_point_bit2`'s `PLX`, and `thread_call_handler`'s `PLB`.

### The whole of the game's trigonometry is half a frame of work

`$80:9570` is `sin_deg`'s only caller and the round's payoff for having ported
it. It rebuilds `$7E:8000` from scratch every frame: a `$F8` repeat header, 120
sixteen-bit parameters at four degrees of arc per scanline, a second header, and
a `$0000` to stop. The phase advances one degree a frame, so the wave slides
down the screen on a 90-frame cycle.

It is by a wide margin the most expensive substitutable routine in the registry.
`sin_deg` itself is 286 master cycles; the loop around it measures **115,606**
over 2,021 calls on seven movies, which is a third of a frame's CPU budget in
one call — ahead of `$82:8069 boss_bg_queue_flip` at 89,008 and
`sprite_build_oam` at 43,111, and it is neither a DMA nor the whole sprite pass.
The distribution is two
populations: every level movie makes exactly 12 calls at 163,164..163,514, and
`boot.zmv` makes 1,077 with a floor of 1,176, because the title's wobble spends
most of its life retracted almost to nothing.

Three things in it are worth writing down.

**The second header is written over a parameter.** `LDA #$F800 : STA $7E7FFF,X`
at `X = $F1` puts the `$F8` at `$80F1`, where the next repeat block begins — and
its low byte, `$00`, at `$80F0`, which is the high byte of the 120th parameter.
One scanline of the wave loses its sign every frame, on the seam between the two
blocks, and it is a scroll offset so nobody has ever seen it.

**The effect ends as a boundary condition.** The table only shrinks on a frame
whose *last* parameter came out exactly zero — the bottom of the wave sitting on
the axis — and then only once a hold counter has run out. Two bytes, one
scanline, at the one moment in each 90-frame cycle when removing that line
changes nothing on screen. A fade-out written as an `if`.

**And the caller can be skipped.** The thread's loop is
`thread_yield(1) : JSR $9570` until the length goes negative *or either player
presses Start*, which is what says the wobble is a screen the player waits
through. That also corrects something this document implied last round. The
2,676 `sin_deg` calls that are identical on every level movie are one burst in
the level-entry transition — but `boot.zmv` reaches the same routine **1,077
times**, because the title sequence holds the same wobble for as long as nobody
touches the controller. The count is input-independent *within a level movie*;
across the whole corpus it is a function of how long you sit on the title.

### A step validator that is not the player's

`port/step.h` has held the two ends of `$80:E4C1`, the movement step validator,
since the round that ported `step_propose` and `step_tether_blocked`: propose a
candidate, put it through four tests, commit it, and then do the whole thing
again for the other axis so a mover slides along a wall instead of stopping
against it. The sequencing in the middle was still the ROM's.

`$81:9BF3` is that same shape written for something that is not a player, and it
is short enough to take whole:

```
if (sched_tick & 3) == 0: return                 ; one frame in four is a rest
$10 = $0C + dx ; $12 = $0E + dy                  ; from a nine-entry table
if blocked_enemy($10,$0E) || out_of_bounds($10,$0E) || at_point($10,$0E): keep $0C
else: $0C = $10
if blocked_enemy($0C,$12) || out_of_bounds($0C,$12) || at_point($0C,$12): keep $0E
else: $0E = $12
record.X = $0C ; record.Y = $0E
```

Three tests where the player's validator has four, and not the same three: the
tether is a two-player rule that does not apply, and where `$80:E4C1` asks
`terrain_blocked` and `actor_obstacle_at_point` this asks `terrain_blocked_enemy`
and `actor_at_point` — the same two questions about the board, put with the
other attribute bit and the other id filter. The commit also goes straight into
the actor record rather than into `$30`/`$32` for `actor_publish_pos` to carry
across later.

All three tests were already ported, so what this adds is the *order*, and the
order is the whole mechanic: the second axis is tested from the column the first
one just committed.

Speed is a duty cycle again, and one scale up. `step_propose`'s table steps one
pixel per axis and doubles the step on frames a mask selects; this one steps
**two** and skips one frame in four. 1.5 pixels a frame either way, reached from
opposite directions, and the two tables are otherwise the same object — nine
entries, clockwise from up, indexed by a direction already doubled.

#### The carry belonged to the first instruction

This is the round's one divergence, and it is the same shape as every other one
this document records: a flag claimed from the wrong instruction.

The routine's three tests each end in a `BCS`, so the obvious reading is that
carry comes back as the second axis's verdict — set when the step was refused.
That is right on three calls in four. On the fourth it is a rest frame, and the
routine never reaches a test at all:

```
$81:9BF3  ASL A : TAX
          LDA $0020 : AND #$0003 : BEQ out
```

Neither `LDA` nor `AND` touches carry, so what a caller reads on that path is
**the `ASL`** — bit 15 of a doubled direction, which is a constant zero. Passing
the caller's carry through was wrong on 741 of 2,955 calls, and `verify` said so
at call 564 of `level21-bubble` with A, X, Y, N and Z all matching.

Worth noting how the isolation went, because the obvious reading of the harness
output is wrong: `passed` counts the calls *before* the first divergence, not
the calls that matched. Four experiments — carry dropped, carry always clear,
carry passed through, carry as the axis verdict — gave 2,955, 314, 15 and 563,
which read as a distribution and are actually four first-failure indices.
Dropping the claim entirely and getting a clean 2,955/2,955 is what proved the
rest of the routine and left carry as the only thing to explain.

It is also the round's one routine whose X output had to be *composed* rather
than written. The register that survives to the `RTS` is whichever of the three
ported callees ended the second axis: `terrain_blocked_enemy` and
`actor_at_point` each model their own X, and `terrain_out_of_bounds` provably
touches neither index register, so chaining the three reproduces a register the
port never chose. It is checked on all 16,569 calls `level21-bubble` makes.

### Four sprite sets, a mirror, and one frame of lag on half the compass

`port/collide.h` has the big monster's two collision handlers and calls it "the
monster side, the one that takes objects out from under the player". `$81:C16B`
and `$81:C00B` are the other half of its frame: the walk cycle, and what it does
with what it has taken.

The walk is a three-frame leg and a four-leg stride, and the frame table has
nine groups of four for nine directions — but they are not nine drawings. Up has
its own, down and standing-still share one, and the six remaining directions
share a third, with the three west-facing groups holding *the same pointers* as
the three east-facing ones. What separates them is one bit:

```
CPX #$0030 : BCS +      ; the sixth group
LDA $0000,Y : AND #$FFFD : STA $0000,Y      ; ...and fall into the placement
+ : LDA $0000,Y : ORA #$0002 : STA $0000,Y  ; ...and return
```

Four sprite sets and a mirror is an eight-way walk. And the asymmetry at the two
exits is real: the mirrored path returns **without** calling `$81:C00B`, so on
the frame a west-facing monster advances its cycle, whatever it is carrying is
not repositioned. It catches up the next frame, when the leg timer has not
expired and the routine takes a shortcut into the placement with the *previous*
frame's facing still in `$2C`. The carried record therefore trails the monster
by up to one frame, in one half of the compass and not the other. That is what
the ROM does; why is not recorded anywhere and is not guessed at here.

The placement itself is a nine-entry table of offsets — 24 pixels to whichever
side it faces and 14 up, 24 up facing north, 8 down facing south — and `$FFFF`
in `$28` for empty-handed. Note the third scale: the facing lives doubled in
`$14`, doubled again in `$2C` to index a four-byte table, and doubled a third
time with the phase `ORA`d in to index an eight-byte one. The `ORA` is only a
concatenation because `$14 * 2` has its low two bits clear, which is the ROM
asserting that the facing is already doubled.

### What clears `ACTOR_ACTIVE`

`port/oam.h` has carried this note since the round that ported `actor_nearest`:

> Bit 0: set on every live record in every display list sampled so far... Two
> readers are now known and they test it identically... **What clears it has
> still not been established**, and the name says where it is rather than
> claiming to know more than that.

`$80:BE41` clears it. `LDA #$0000 : STA $0000,Y` — the whole flags word, as the
first thing the routine does once it has decided the free is allowed. And
`$80:BE0C` is the only thing that sets it, with `LDA #$0001`: a record's flags
start at exactly `$0001` and everything else about it, `ACTOR_DRAW` included, is
written afterwards by whoever asked for it.

So the bit is not a property of a drawn record at all. **It is the allocator's
free mark**, read by `LSR : BCC` in those two routines and by nothing else, and
the two coverage sites that test it during a list *walk* are untaken by all 43
movies for the sound reason that a record on the list always has it. A question
that had been open for four rounds turned out to be answered by the two
routines nobody had read yet, and they were sitting at 143 call sites — 80 and
63 — which is more than any pair the port has taken.

The rest of them is worth a paragraph each.

**The list is a stack and the array is scanned backwards.** The allocator takes
the first free slot going *down* from `$7E:1ACA`, the last of the 32, and pushes
it onto the *head* of the list at `$7E:1B5E`. Two orders, opposite directions,
neither the other's inverse — so the list's order says nothing about the array's
and every walk in `port/oam.c` sees the newest record first.

**A free needs the caller's permission slip.** `LDA $0008 : CMP $000C,Y : BNE
out` — `W_SCHED_CUR_TASK` against `ACTOR_THREAD`, so a record can only be freed
by the thread that owns it and passing somebody else's does nothing, silently.
It is the only ownership check anywhere in the port so far.

**Three exits, three sources of N and Z.** The "not yours" exit leaves the
`CMP`'s flags; the "already free" exit leaves the `LSR`'s; and the working exit
ends `PLD : RTL`, so N and Z come off the caller's own direct page. The `PHD` is
there because the unlink walk addresses `$7E:0038` and `$7E:0012,X` through a
direct page it forces to zero itself — which is how a `JSL` from any of 63 sites
reaches two fixed words without knowing where it came from. `$80:BE0C` does the
same trick one register over and pays for it in a stray stack byte.

### What this round measured

| | before | after |
| --- | --- | --- |
| registry entries | 98 | **104** |
| calls checked, 43 movies | 12,930,985 | **13,032,692** |
| routines diverged | 0 | **0** |
| coverage sites taken | 370 / 464 | **394 / 494** |
| ...untaken by every input | 94 | **100** |
| static share | 24.2% | **25.2%** |
| dynamic share | 61.9% | **62.3%** |
| ...substituted only | 48.8% | **49.1%** |
| ...substituted, waits removed | 56.4% | **56.8%** |

Thirty coverage sites went in and **six of them came back untaken**, which is
worse than the previous round's zero and is the honest shape of a round that
went looking for rarer code. Five are the answer arriving in the negative and
are worth having: `slot_alloc_full` says **no input in 43 movies has ever filled
all 32 display records**; `slot_free_not_mine` and `slot_free_already` say no
thread has ever tried to free a record it did not own or had already freed, so
the two guards `$80:BE41` opens with have never once fired; `slot_free_unlisted`
says the unlink walk has never run off the end; and `bearing_bounds` says the
one actor that uses `$81:9BF3` never reaches the edge of its level.

The sixth is different. **`wave_over` is unreachable from the ROM's only
caller.** `$80:9570` opens `LDA $70 : BMI out` — the effect is finished, do
nothing — but the thread around it tests the same word at the bottom of its own
loop and exits on it, so the routine is never entered with a negative length. It
is a guard against a state its caller has already excluded, and the corpus is
what says so rather than a reading of the two routines.

The `dynamic` row is the one to read for what the round was worth: **0.38% of
every instruction the game executes**, against the previous round's 0.745%. That
is not a worse round, it is a flatter list — the callable ranking's top row is
now `$80:CC7C apu_load_set` at 0.8%, which drives the APU bus and would have to
be `verify_only`, and everything under it is a tenth of a per cent at a time.

### The top of the list was a wall, and going through it took a round

The previous round ended by naming `$80:CC7C apu_load_set` as the next target
and everything below it as "a flat tail of tenth-of-a-per-cent rows". This round
took the top row and three rows out of the tail — `$82:9265`, `$82:92D6` and
`$81:BB75` — and picked up a fourth routine, `$81:BBA4`, that is not on the
ranking at all because it has never once executed.

The top row turned out not to be registerable. What follows is in the order the
work happened, because the order is the argument.

### `$80:CC7C apu_load_set` is written and cannot be checked

It is the largest single item the ranking still offers: 1,247,196 instructions
over the six profiled movies, 0.7% of everything the game does, in **twelve
calls**. That ratio is the whole story — about 104,000 instructions per call.

The routine itself is small and completely legible. A set id is masked to a
byte, scaled by four, and used to index a table of `{address, bank}` pairs at
`$80:CCDE`; then the whole body runs under one `SEP #$30` and does nothing but

```
block := u16 count, count bytes
set   := block* $0000
```

fetching each byte through `apu_next_byte` and handing it to `apu_send`. Both of
those are already in the registry, which is why they are the second- and
third-most-called routines in it: a quarter of a million calls each, and almost
all of them come from here.

Two details are worth the reading. The count lives in `$1C`/`$1D` and is
decremented **a byte at a time with the borrow written out by hand** — `LDA $1C :
BNE +2 : DEC $1D : + DEC $1C` — because eight bits wide is all the routine has.
And the parameter that goes out with the block header, command `$0A`, is
`$1C ORA $1D`: the two count bytes folded together, which is not the count and
not either half of it. It is the accumulator the `BNE` two instructions earlier
tested and left behind. Any nonzero count gives a nonzero byte and that is all
the value can mean.

**None of which can be verified.** 104,000 instructions is roughly eight frames,
and the harness abandons any call an interrupt lands inside. Measured rather
than assumed, with the routine registered and then unregistered:

```
routine                 calls  yields  checked   passed   int.  result
apu_load_set                5       -        0        0      5  not reached   (level25-lane)
apu_load_set                3       -        0        0      3  not reached   (boot.zmv)
```

`boot.zmv` was the movie worth trying, because the load there happens with the
screen off and before the title — if NMI were disabled anywhere it would be
there. It is not. So this is the **fourth** written-but-unregisterable routine,
after `lzss_decompress`, `blockmap_expand` and `sprite_cache_init`, and all four
are in `BLOCKED` for the same reason: a call longer than a frame has an NMI in
it.

It is the only one of the four with a *second* disqualification. It would have
needed `verify_only` as well, for `apu_send`'s reason one level up: substituting
it would take 23,800 SPC handshakes off the 65816 in one go. Either reason alone
is enough. The C stands in `port/apu.c`, unchecked, for Phase 4 — the day the
port owns its own main loop, this is the routine that loads the music.

### The boss's four hitboxes, and the box it walks with

`$82:9265` and `$82:92D6` are the two instructions after `boss_step` in the same
thread's loop, and they are the other two things a figure drawn out of
background tiles needs.

A big figure has no actor record: `port/bossbg.h` blits it into BG tiles and its
position lives in two fixed words, `W_BOSS_X`/`W_BOSS_Y`. So there is nothing
for `actor_overlap_pass` to find. The thread's setup buys four records instead
— `JSR $94B4 : STX $24` four times — and **`$82:9265` is what keeps them under
the drawing**: once a frame it writes `ACTOR_X` and `ACTOR_Y` of all four from
the figure's position plus a fixed offset.

| part | dx facing east | dx facing west | dy |
| --- | --- | --- | --- |
| `$24` | −14 | +14 | −6 |
| `$26` | −4 | +4 | 0 |
| `$28` | +10 | −10 | +4 |
| `$2A` | +20 | −20 | +12 |

Four hitboxes on a line running down and to the right, and the mirror flips the
line about the vertical without touching the heights. The X offsets come from a
table and the Y offsets are immediates in the instruction stream, which is why
one axis is read from ROM and the other is transcribed. **The mirror is a second
table rather than a negation** — `LDY #$0000` or `LDY #$0008` into eight words
laid back to back — and the port reads all eight even though they are four exact
negative pairs, because a hack that gave the figure a lopsided reach by editing
four bytes would still work.

The flag that selects it is `$36`, which is the same word `boss_step` writes out
of its own delta table. Three routines, one facing.

`$82:92D6` is five stores and a `JSL`: the rectangle under the figure, handed to
`actor_notify_box`. It fires **every frame the thread runs**, not on contact and
not on a timer, so anything standing inside is told once a frame for as long as
it stands there. The box is

```
x0 = W_BOSS_X - 34    x1 = x0 + 56      ->  -34 .. +22
y0 = W_BOSS_Y -  8    y1 = y0 + 32      ->   -8 .. +24
```

against a body `boss_step` describes as 36 wide from the top centre and about 20
tall — so the walked-on box sits six pixels left of the thing it belongs to and
stands eight proud of the top of it. That is what the ROM does. Why is not
recorded anywhere and is not guessed at here.

`boss_place_parts` has the **flattest distribution in the registry**: 1,090 to
1,142 master cycles over 6,560 calls, a 52-cycle spread on a routine that costs
eleven hundred. There is exactly one branch in it and both arms are an `LDY` of
a constant; everything else is about forty straight-line instructions with no
call, no loop and no early exit.

Next to it, on the same frames and the same two movies, `boss_stomp` runs 1,112
to 13,054 over 6,549. The two routines differ by one `JSL`, so the difference
between the two ranges is that one call's whole distribution, isolated: at the
floor it costs about twenty cycles, which is `actor_notify_box` finding an empty
visible list and leaving, and at the ceiling it is 91% of the call. That is a
cleaner reading of what a walk costs than the callee's own row can give, because
the caller here is a constant.

#### Neither register the `JSL` passes on is the caller's

`boss_stomp` failed on call 1 of `level25-lane`:

```
boss_stomp  108  -  108  0  0  0  21  1152..6460, mean 2389  FAIL
            A: ROM $000A, port $021D
```

The shim had been written the obvious way — `boss_stomp(w, rom, in->a, in->c,
&r)` — on the reasoning that a routine which is five stores and a `JSL` passes
its caller's registers straight through. It does not. Look at the two
instructions before the `JSL`:

```
$82:92F1  CLC : ADC #$0020 : STA $003E     the bottom edge
$82:92F8  LDA #$000A : STA $0040           the id
$82:92FE  JSL $80BF1B
```

A is `$000A` — the id, handed over **twice**, once in memory where
`actor_notify_box` reads it as `NOTIFY_BOX_DP_ID` and once in a register where
it reads it as the argument. And the carry is not the caller's either: it is the
`ADC #$0020` at `$82:92F2` that produced the bottom edge, set only when that
addition wrapped sixteen bits, and nothing between there and the `JSL` touches
it.

The ROM's own answer was the tell. `$000A` coming back in A is what
`actor_notify_box` returns when its walk found nothing to look at — the input,
passed straight through — so the value the callee received had to be the one the
`LDA` two instructions up had just loaded. This is the same lesson as
`apu_play_sfx`'s `PLD` and `actor_slot_free`'s, arriving from the other end: **a
routine's register inputs are whatever is in the registers at the call, and a
wrapper computes some of them.** Both are recomputed in the port now, and
neither function takes them.

### The monster's other half

`port/monster.h` already had the half of the creature's states that draws.
`$81:BB75` and `$81:BBA4` are the half that decides where to go — and the shape
is exact, because four of its state bodies open with a pair of `JSR`s and
nothing else:

```
$81:C226  JSR $BB75 : JSR $C16B     the wander
$81:C2BB  JSR $BBA4 : JSR $C16B     ...carrying somebody home
$81:C355  JSR $BB75 : JSR $C16B
$81:C3DF  JSR $BB75 : JSR $C16B
```

`$81:BB75` asks one 32-slot scan two questions at two ranges, and **the two
ranges are asking about different populations**. `actor_nearest` looks at four
collision ids — the two players, `$38` and `$01`; `player_in_range` looks at
exactly the two players. So:

* inside `$B4`, and not already holding something → install the chase;
* between `$B4` and `$D0` → do nothing;
* beyond `$D0` → is *either player* within `$D0`? If not, `INC $2A`, and the
  thread ends at the bottom of its next loop.

The gap between `$B4` and `$D0` is not hysteresis. Nothing here is a state with
an exit condition — the routine is re-entered from scratch every frame — so it
is a dead band: something 180 to 207 pixels away neither starts a chase nor
counts as an empty board, and the creature stands there. And the asymmetry
between the two populations is the behaviour: it will walk towards any of the
four, and it gives up and leaves only when **both players** are far. A board with
a player at `$D1` and a `$38` at `$40` takes the near exit and never reaches the
give-up test at all.

`$81:BBA4` is the carrying state's, and it opens with a question the other never
asks: **am I standing where I started?** The thread's own setup writes the spawn
point into `$2E`/`$30` at the same time as into `$0A`/`$0C`:

```
$81:B9FD  LDA $00 : STA $0A : STA $2E
$81:BA03  LDA $02 : STA $0C : STA $30
```

Within sixteen pixels on both axes — a square, tested as two independent
absolute differences and not a radius — whatever is being carried is freed and
`$28` goes back to `$FFFF`. That is the monster reaching its lair and dropping
the victim in it.

From there the two routines are the same four instructions, and the differences
are both tests `$81:BBA4` leaves out. It does not test `$26`, so a monster still
carrying somebody across the level will drop into the chase state the moment
anything comes within `$B4` — with `$28` still pointing at whoever it is holding.
And it has no far test, so `$2A` is never incremented from this state and a
monster carrying a victim never gives up and leaves. Both omissions are
reproduced; why they are there is not recorded anywhere.

#### A routine with its `RTS` in three places

Both of these end by installing the next state and returning, through a stub
they share:

```
$81:BEDA  JMP $BEDD
$81:BEDD  LDA #$BEE3 : STA $12 : RTS
```

reached by `JMP` and not `JSR`, so the `RTS` at `$81:BEE2` is the one that
returns to whoever called `$81:BB75`. A routine with three exits therefore has
its `RTS` in three different addresses, two of which are outside it.

This costs the harness nothing, and the reason is worth stating because it looks
like it should: **`ret_op` is where a *substituted* call is sent, not how a
returning one is recognised.** A return is detected by program counter and stack
pointer (`cosim.c`'s `snes->cpu->sp != return_sp(...)` check), so any `RTS`
inside the routine will do for the teleport. Both entries name the plain one.

#### A registry entry no movie reaches

`monster_deliver` is checked on zero calls, on all 43 movies, and the profiler
agrees: `$81:BBA4` has a call count of **zero in every one of the eleven
traces**. The creature has to actually pick somebody up and set off home with
them, and no input in the corpus has ever made it do that.

The entry stays. It is checked the moment any input reaches it, which is what a
registry entry is for, and the project already carries seven ported routines
that executed nothing in the last profiling run. What it cannot have is a
measured budget: `.cycles` is `monster_seek`'s figure, which is defensible
rather than measured — the two make the same single `JSL actor_nearest` and that
call is nearly all of the cost — and the comment on the entry says exactly that.

### The chase, found by porting its installer

`$81:BEDA` was declared a jump entry two rounds ago, when it turned out that
four fifths of what the ranking credited to `$81:BC3D` was really a
three-instruction state installer nothing calls. The note left behind said the
bodies under it "stay charged to it, and that is a lower bound again".

Writing `monster_seek` meant reading the constant that installer stores, which
is the address of the body: **`$81:BEE3`**, the monster's chase. Declaring it
splits 469,168 instructions into an installer's four and a chase's 439,308, and
the chase now has a row of its own at 0.2%. It is still not portable, for the
reason the older note gives — nothing calls it, it is entered by a computed
`RTS` through `$12` — but the ranking is now telling the truth about where the
work is, which is the point of the tool.

That is a fourth address found not by grepping for an idiom but by porting its
neighbour and reading what the neighbour writes.

### What this round measured

| | before | after |
| --- | --- | --- |
| registry entries | 104 | **108** |
| calls checked, 43 movies | 13,032,692 | **13,106,971** |
| routines diverged | 0 | **0** |
| coverage sites taken | 394 / 494 | **402 / 506** |
| ...untaken by every input | 100 | **104** |
| static share | 25.2% | **25.6%** |
| dynamic share | 62.3% | **62.5%** |
| ...substituted only | 49.1% | **49.3%** |
| ...substituted, waits removed | 56.8% | **57.0%** |

Lockstep against a stock core is clean on `level1` (2,389 passes),
`level25-lane` (3,485), `level45-race` (6,089) and `level25-2p` (1,220): every
difference is inside the stacks or a declared scratch byte, and no byte of live
game state ever differed. Framebuffers against `--stock` are identical on eight
movies, two of which — `level25-2p` and `level45-carried` — were added to that
set for this round because they run the boss and the monster.

**Sixteen coverage sites were declared and four were taken back out**, and
taking them out is the part worth recording. The four were `apu_load_set`'s. A
site that no input can ever reach does not belong in a list whose stated meaning
is *an input is missing*: `$80:CC7C` has no shim, so nothing in this harness
will ever execute its C, and four permanently-untaken rows would have made the
corpus report quietly dishonest about its own gaps. The branches are described
in `port/apu.c` instead. **Sites are a harness mechanism and they belong only to
code the harness runs.**

Of the twelve that stayed, **eight were taken and four were not**, and the four
are all `monster_deliver`'s. Those are the good kind of untaken: they name
something the game does that no input in the corpus has made it do — pick a
victim up and set off home with them — and the fix for them is a movie, exactly
as the report says it is.

The `dynamic` row is worth **0.158% of every instruction the game executes**,
against the previous round's 0.38% and the one before that's 0.745%. The list is
flattening, and this is the round that shows why: the largest item left on it
could not be registered at all, and the three that could are a tenth of a per
cent each. What the round bought that the numbers do not carry is two things —
a shim convention that was silently wrong for wrapper routines, and a state body
worth 439,308 instructions that the ranking had been crediting to a
four-instruction stub.

## The panel that is six comparisons (2026-08-10)

The ranking after the last round offered one large registerable row and then a
cliff: `$80:CB61 apu_ipl_upload` at 1.5%, and after it nothing above 0.25%. This
round went past the cliff on purpose and took the **status panel** —
twenty-three routines between `$80:C07F` and `$80:C7BF` that draw both players'
health bars, weapon and item icons, inventory counts and scores.

Three of the twenty-three are registered. The other twenty come with them,
because every one of them is reached only from inside the tree: searched for
`JSR`, `JMP`, `JSL` and `JML` across the whole ROM, and the two panels are the
only entries with an outside caller at all.

| routine | what it is | budget | stack |
| --- | --- | --- | --- |
| `$80:C07F hud_refresh` | alternates the two panels, then queues the upload | 1,367 | 6 |
| `$80:C0A3 hud_panel1` | player 1's six change tests | 1,247 | 4 |
| `$80:C139 hud_panel2` | ...and player 2's, at different addresses | 694 | 4 |

`hud_panel2`'s budget is half its twin's only because player 2 is usually
absent and the panel-off exit is two instructions. On `level1-2p` the two means
are 1,342 and 1,331.

The whole corpus verifies clean: **13,209,637 calls checked across 43 movies,
0 diverged**. Lockstep `run` is clean on four movies — `level1` 2,389 passes,
`level1-2p` 5,989, `level25-2p` 1,220, `level45-race` 6,089 — with every
difference inside the stacks or a declared scratch byte and **no byte of live
game state ever differing**.

Under lockstep the two panel entries mostly read `not reached`, because
`hud_refresh` is substituted above them and the port does not call the ROM. They
are not idle: `level45-race` enters `hud_panel1` twice, from the transition sites
`$80:C1D1`/`$80:C1F1`, which is precisely the traffic registering them
separately was for.

### A HUD is not the last thing a WRAM diff can check — it is one of the easiest

The instinct is that a HUD is display, and display is hardware, and hardware is
where this harness stops. It is not: every routine in the cluster writes a
**shadow tilemap in WRAM** at `$7E:5F36`, four rows of 32 tiles, and a separate
vblank job uploads that later. So the whole thing is memory, and the harness
checks it exactly as it checks a collision.

That also gives the WRAM map two new named ranges out of a 2,560-byte block that
had been `*unidentified*` since Phase 1 — the tilemap, and the eleven words of
change detection sitting immediately above it at `$7E:6036`.

### Two lookups that agree, and stay two

Every per-player word in the cluster is indexed by a **side**: 0 or 2, already
doubled, the same value `port/score.h` describes. Every shadow word and every
tilemap column follows the same rule, and player 2's column is always player 1's
plus `$20` — sixteen words, half a tilemap row. The panel is one layout drawn
twice into the two halves of the same four rows.

The *colour* does not follow that rule. `$80:C59C`, `$80:C5C2` and `$80:C666`
each fetch their palette or their tile table through `$7E:1E84 + side` — the
score-slot pairing — and index a two-entry ROM table with what they find. So
where a bar is drawn is fixed by which half of the panel you are, and what
colour it is drawn in is fixed by which score slot you own. `$80:925D` seeds the
pairing as the identity, so on a stock boot the two lookups always agree and no
diff can tell them apart. The port keeps them two anyway, for the same reason
`score_add` reads its jump table out of ROM: a ROM hack that repoints one of
them works, and a port that collapsed them would be a port of a coincidence.

### Leading-zero suppression is a rotate, and it is diffed

`$80:C4EC` prints one digit and remembers whether anything has printed yet, in
direct-page `$1E`:

    emit: CLC : ADC #$3C07 : SEC : ROR $1E

Only bit 15 is ever tested, so a port could store a flag and satisfy every
branch in the game. It cannot store a flag, because `ROR` **shifts what is
already there**: `$1E` is `$8000` after one digit, `$F000` after four and
`$FF00` after eight, and it is a live direct-page word the harness diffs along
with the other 131,071. This is the sort of thing the register-and-WRAM contract
catches for free and a screenshot comparison would never see.

The same word does double duty. `$80:C59C` puts the health bar's **palette** in
`$1E` before calling `$80:C379`, which ORs it into ten tiles. One direct-page
word, two unrelated meanings, distinguished only by which routine is running.

### The bug, and the one line that found it

`hud_panel1` failed on call 119 of `movies/level1-rescue.zmv` with

    WRAM $7E:0C20: ROM $24, port $00

`$0C20` is the running thread's page plus `$20`, and `$24` is 36, and 36 is
nine times four. The health bar builds its table row as `A*4` then `A*16 + A*4`,
out of shifts — and the intermediate is parked in `$20` by an `STA` that reads
like scratch and is not. `$20` is a live direct-page word; health-times-four is
still sitting in it when the routine returns, on top of the side `$80:C59C` put
there four instructions earlier.

Nothing in the game reads it. Every branch, every register and every visible
tile was already correct. It is exactly the class of thing a port gets wrong
forever without a byte-for-byte diff, and the diff found it on the first movie.

### Carry, claimed rather than shrugged off

Nothing in this cluster returns a value in carry, and leaving it unclaimed would
have been easy and quiet. It is claimed on all three entries, because the
`$80:83AE` episode earlier in this document is what an unclaimed flag costs: 107
calls passed while the substitution left carry at whatever the caller had, and
the caller's retry loop then ran 147,405 times under `run`.

Claiming it here means threading it through the whole tree, and the chain is not
short: a `CMP` against a shadow sets it six times per panel, and under that so do
the four shifts that build a health row, the `ASL` that doubles an inventory
index, the `CMP #$000E` that rejects one, the four `LSR`s that shift a nibble
down, and the `ROR $1E` inside every printed digit — where what lands in carry is
the bit rotated *out*, which is bit 0 of the leading-zero history. Whichever ran
last is what the caller gets, and on the paths where none ran it is the caller's
own.

### Two branches that exist, run, and cannot be reached

`$80:C5C2` and `$80:C666` each open by testing the selected slot for `BMI` and
branching to a "nothing selected" path. The weapon's clears six tiles — three
rows of two, including a row its drawing path never writes. The item's is a bare
`RTS` that clears nothing at all.

Neither can run. `$80:C5C2` is reached only from `$80:C76E` and `$80:C79C`, and
`$80:C666` only from `$80:C785` and `$80:C7B3`, and all four of those sit on the
far side of a `BMI` **on the same word**, in adapters that do their own clearing
when it is negative. The inner test is always false by the time it executes.

The port implements neither, and neither carries a coverage site. That is the
`apu_load_set` lesson applied before it cost anything rather than after: a site
no input can reach would sit in the untaken list forever describing a thing the
game cannot do, and the untaken list's whole meaning is *an input is missing*.

`$80:C505`, a two-digit renderer sitting between `$C4EC` and `$C519`, is dead in
the stronger sense — no `JSR`, `JMP`, `JSL` or `JML` anywhere in the four banks
reaches it. It is recorded in the symbol file as dead and not ported.

### A duplicated fact, removed rather than duplicated again

`$80:C07F` does not end with an `RTS`. It ends `JML $8083AE`, a tail jump into
the vblank queue adder — so on its interesting path the registers and the carry
a caller sees are **that routine's**, not its own.

`$80:83AE` is already registered, and its exit flags were already written down
once, in `shim_vbl_queue_a_add`. Writing them a second time in `port/hud.c` would
have been ten lines of the kind of duplication that is correct on the day it is
written and wrong a year later. They moved instead: `vbl_queue_flags` now lives
in `port/thread.c`, the shim calls it, and so does the HUD. One copy, two
callers, and the 13-million-call corpus checks both.

### Results

| | before | after |
| --- | --- | --- |
| registry entries | 108 | **111** |
| static share | 25.6% | **26.5%** |
| dynamic share | 62.5% | **62.6%** |
| ...substituted only | 49.3% | **49.4%** |
| ...as the game reports it | 57.0% | **57.2%** |
| coverage sites | 506 | **538** |
| ...taken by some input | 402 | **432** |
| calls checked, whole corpus | 13,106,971 | **13,209,637** |

Thirty of the thirty-two new sites were taken. The two that were not are
`hud_score_high_only` — a score carrying past `$9999` between two consecutive
looks at the same panel — and `hud_upload_refused`, which needs queue A to be
holding sixteen jobs at the moment the HUD wants to add one. Both are the good
kind of untaken: real branches naming something the game can do that no input in
the corpus has made it do.

Both columns come from the same tool run over the same eleven profiles, with the
before column produced by stripping the three new `.name` records out of a copy
of `routines.c`; it reproduces the last round's published figures exactly.

The dynamic row is **481,056 instructions, or 0.144%** of everything the game
executes — the smallest round yet, after 0.158%, 0.38% and 0.745%. The static
row is the one to look at instead: **+0.9 points, 173 distinct code bytes**, the
largest static gain per dynamic point of any round so far, and that is simply
what twenty small routines look like. The cliff in the ranking is real, and past
it the useful measure stops being "how much work did this take over" and starts
being "how much of the cartridge is now written down".

The whole cluster is gone from the ranking — not one `$80:C0xx`–`$80:C7xx` row
survives — which is the subsumption closure confirming that the tree really is
closed, including the seven leaves that are reached by `JMP` and so have no call
edge for the closure to follow.

### The screenshot check had not been checking anything

The framebuffer comparison — run the port build and a `--stock` build over the
same movie, screenshot both at the same frame, compare the PNGs — is the last
line of evidence in every round here, and it had a bug in it that made seven of
its eight results vacuous.

**PowerShell variable names are case-insensitive.** The script held its output
directory in `$S` and then wrote

    $s = Join-Path $S "$name-stock.png"

which is the same variable. From the second movie onward the directory was a
PNG path, the emulator refused to write into it, `ReadAllBytes` threw, `$a` and
`$b` still held the **previous** movie's bytes, and the comparison dutifully
reported them identical. Every run said "8 of 8 identical" and meant "1 of 8
checked, and the other seven compared movie one against itself".

Fixed — distinct names, an existence check before comparing, and the byte counts
printed alongside the verdict so a vacuous pass cannot look like a real one. The
corrected run says something different:

| movie | frame | |
| --- | --- | --- |
| `level1`, `level1-2p`, `level29-fighting`, `level45-race` | 6,000 | identical |
| `level21-bubble`, `level25-lane` | 2,600 | identical |
| `level45-carried` | 6,000 | identical |
| **`level25-2p`** | 6,000 | **differs** |

`level25-2p` parts from stock between frames 2,600 and 2,700 and stays parted.
It is **not this round's doing**, and the test for that is exact: re-run the
same frame with all 111 routines substituted *except* the three new ones, and
the port's framebuffer is **byte-identical** to the full-substitution one. The
divergence belongs to something already in the registry, and the broken script
is why nobody had seen it.

### ...and what it was hiding is the cycle budget, not a bug

A bisection over the 108 pre-existing entries — substitute a prefix, screenshot
frame 2,700, halve — landed on the sixth routine in the registry. Substituting
**`actor_depth_sort` and nothing else** reproduces the divergence *exactly*: the
same 24,549-byte frame, byte for byte, as substituting all 111.

It is not a bug in the port. Verify on the same movie: **1,223 calls, 1,223
checked, 1,223 passed**, and `run` reports no byte of live game state ever
differing. What the same report shows is the cause, in the column nobody reads:

    actor_depth_sort   1223 calls   92..7524, mean 958   OK

The registry burns a **fixed 1,605** master cycles for every one of those calls.
The real routine costs between 92 and 7,524 — an eighty-fold spread, because it
is a sort and its cost is the length of the actor list — and 1,605 is not even
this movie's mean; it is `level1-rescue`'s, from the round that added it.

So each substituted call puts the machine as much as 5,900 cycles out of step
with where the ROM would have been, which is several scanlines. Every byte of
state still agrees. What moves is *when* the frame's work lands relative to the
beam, and on a movie with two players and a boss on screen that is eventually
enough to change a frame.

This reframes what the framebuffer check can and cannot prove. It was never a
check on the port's logic — `verify` and `run` are that, at 13.2 million calls
and every byte of WRAM. It is a check on the *substituted build as a whole*, and
that build deliberately approximates one thing: how long a routine took. A
fixed budget standing in for an 80× range is an approximation with a visible
limit, and this is the first movie busy enough to find it.

`CosimRoutine::cycles` says it is "the mean cost of the ROM's own instructions".
The honest next step is not to retune 1,605 — that would move the divergence
rather than remove it — but to let a routine whose cost is a function of its
input **report** what it did, the way `run_yield` lets one report that it
suspended. That is a change to the harness, and it belongs in its own round.

### The largest row that is left, and why it is not next

`$80:CB61 apu_ipl_upload` is 1.5% plus another 1.95% of the machine waiting
inside it, and it is the last big registerable-looking row. It is out on two
independent grounds, and both were established rather than argued.

**Measured.** It writes no WRAM at all, which makes an empty shim a clean probe:
register it temporarily against one, and whatever the harness says about
interruption is uncontaminated by any question of whether the port is right. It
says `1 call, 1 interrupted, 0 checked` — on `boot.zmv`, where the screen is off
and NMI has its best chance of being disabled, and again on `level25-lane`.
About 986,000 instructions per call is many frames, and an interrupt lands in
every one. `$80:CB1A apu_boot` calls it and reports the same.

**Structural**, and this is the one that would still hold if the frame problem
went away: the routine's entire observable effect is on the SPC700, through
`$2140`-`$2143`, one byte at a time, gated on the SPC's replies. There is
nothing in WRAM for a diff to compare. A substituted port would have to drive
the real handshake through the host — which is what `apu_send` does, and why
`apu_send` is `verify_only` and can never be substituted. `$80:CB61` could at
best be the same thing, on a call the harness cannot reach the end of.

`$80:CDF4`, the other row that looked registerable, is settled from the listing:
`JSR $D13A : LDA #$0001 : JSL thread_yield : ... : BRA $CDF7`. It is a loop with
a yield in it and **no exit** — the level's main body. Nothing calls it, it
never returns, and the profile agrees at zero calls in eleven traces. All three
are now in `tools/native_share.py`'s `BLOCKED` with their reasons.

## A routine that says what it cost (2026-08-10)

The previous round ended with a finding and a refusal. `actor_depth_sort`
substituted against a fixed 1,605 cycles was parting `level25-2p`'s framebuffer
from the stock core's, the port's logic was provably not at fault, and retuning
the constant was named as the wrong fix:

> The honest next step is not to retune 1,605 — that would move the divergence
> rather than remove it — but to let a routine whose cost is a function of its
> input **report** what it did, the way `run_yield` lets one report that it
> suspended. That is a change to the harness, and it belongs in its own round.

This is that round. The mechanism is one function, the checking is what makes it
worth having, and applying it to four routines found one wrong assumption about
the machine that had been sitting in this project's head unexamined.

### One function, and the two rules that keep it a measurement

```c
void cosim_cost(int cycles);
```

A shim calls it from inside `run` with what the call it just served would have
cost the 65816, and native mode burns that instead of `CosimRoutine::cycles`. A
shim that says nothing gets the constant, exactly as before; a shim that can
price some of its paths and not others reports on those and stays quiet on the
rest. Nothing else in the registry changed, and 107 of the 111 routines do not
know this exists.

What makes it a measurement rather than a nicer-looking guess is the second
half. **`verify` already knows what the ROM's instructions really cost** — it
has been printing the range and the mean in a column nobody read for eleven
rounds — so a reported cost is diffed against the real one on every single call
and the report prints the error:

```
cost models (error is what the ROM took, less what the port said it
would; a correct model is short by one 40-cycle DRAM refresh per
scanline the call crossed)

  routine                  priced         of  error                     refresh-exact under HDMA
  actor_depth_sort           1518       1518  +0..+240, mean +53            1518/1518          0
```

A model nobody checks is a guess with a struct around it.

### Why a correct model's error is not zero

The core adds 40 master cycles for a DRAM refresh whenever a run of cycles
crosses the end of a scanline. Where those land is a property of the machine's
clock at the moment of the call, not of the call's arguments, so no function of
the input can predict them — and a model that tried would be fitting noise.

So the contract is that a reported cost is the routine's **own instruction
cycles and excludes refresh**, and the residual is read as the diagnostic it is:
a correct model is short by exactly 40 cycles per scanline the call crossed, and
by nothing else. That gives the report a column with a yes-or-no in it.
`+0..+240` on a routine whose calls run to 7,642 cycles is five and a bit
scanlines, and **1,518 of 1,518 refresh-exact** says every one of those errors
was a whole number of refreshes. One call that was not would be printed as
`MODEL WRONG`, and several were, which is the story below.

Refresh is not the only thing the machine adds. **An armed HDMA channel
transfers at the start of every scanline**, and those cycles land inside the
measurement in amounts that depend on how many channels are running and what
they are moving — as unpredictable from the routine's input as refresh, and not
as tidy. Three movies in the corpus run HDMA at all: level 1's map screen and
both level 49 probes. Calls made while a channel was armed are counted
separately and held only to the direction — the model must never claim a call
cost *more* than the ROM took — and the sharp test is the other forty movies'.

The other half of that contract is on the burning side. `snes_runCycles` adds
its 40 once per *call*, so a 7,000-cycle budget handed over in one piece gets
one refresh where the ROM executing the same work in twelve-cycle bites gets
five. A reported cost is therefore burned in pieces no longer than a single
memory access, and the core inserts refresh wherever the clock says it is due. A
declared `cycles` constant is still burned in one piece, because it was
*measured* refresh-inclusive and chunking it would count refresh twice.

### `tools/cycles816.py`, because summing a listing by hand is how this goes wrong

A cost model is a table of constants and every constant is the sum of one
straight-line run of the listing. `$80:BC7F` alone needs eleven of them. Doing
that arithmetic by hand eleven times, and then forty more times for the HUD, is
an invitation to a silent off-by-six, so it is done by a tool that prices the
ROM instead:

```
$80:BC7F  AE 5E 1B     LDX  $1B5E         34     34
$80:BC82  F0 5D        BEQ  $BCE1         12     46   (18 taken)
$80:BC84  B4 12        LDY  $12,X         34     80
```

It reuses `dis816.py`'s opcode table, follows `SEP`/`REP`, prices both sides of
every branch, and refuses — rather than guesses — at anything whose cost it
cannot see, such as an indirect jump or an indexed mode with 8-bit indices where
a page crossing decides. `LDX $1B5E : BEQ : RTS` comes out at 34 + 18 + 40 = 92,
which is exactly the minimum `verify` had already measured for the empty-list
path. That agreement, before a line of model code was written, is what made the
rest of it worth doing.

### The sort, exact on the first attempt

`$80:BC7F` is a bubble pass over the display list, and its shape is four
compares, two relinks, one advance and one loop step, each a straight line of a
different length. The port already walked the list in exactly the ROM's order —
that was the whole point of transcribing the second look a swap causes — so
`actor_depth_sort_counted` returns the counts and `src/cosim/routines.c` prices
them. The port learns nothing about cycles: it counts branch outcomes, which is
the same kind of fact `PORT_COVER` records.

Refresh-exact on every call of every movie it was tried on, first run. And the
framebuffer test the previous round left behind now passes: substituting
`actor_depth_sort` alone on `level25-2p` at frame 2,700 gives a frame
**byte-identical** to the stock core's, where before it was the exact
24,549-byte frame that had been parting.

### The HUD, and a fact about the cartridge that was wrong

`hud_refresh` costs 1,084 cycles when the six comparisons all match their
shadows and 11,950 when the score, both counts and both icons have all moved. It
was declared at 1,367. Same disease, worse ratio, and the bisection said so:
with the sort fixed, `hud_refresh` became the first routine in the registry that
parted the frame.

The tree is sixteen routines, so the port's side is a `HudWork` — one counter
per straight-line run, riding along inside the `HudRegs` that every routine in
the file already threads — and the harness's side is a table of 37 constants.
Thirty-four of them were right. The three that were not are the interesting
part:

```
  hud_refresh                 231        231  +0..+428, mean +59       228/231  <-- MODEL WRONG
  hud_panel1                  116        116  +0..+388, mean +44       114/116  <-- MODEL WRONG
```

Three calls out of 233, all of them the ones where the health bar was redrawn.
Everything else refresh-exact. A per-instruction trace of one such call, next to
the model:

    $80:C5A3  LDA $C5B6,X    modelled 36   measured 40
    $80:C383  LDA $C3DE,Y    modelled 36   measured 40

**A LoROM cartridge appears twice in the address space and only the `$80`+ copy
is fast.** `$80:C5A3` is an instruction in bank `$80` — fetched at 6 cycles a
byte, because the boot code sets `$420D` — reading a table through the *data
bank register*, which the HUD's callers leave holding a low bank. So the table
read costs 8 a byte and not 6. `port/hud.h` had been asserting a data bank of
`$80` since the round that wrote it; the assertion was about which *addresses*
the tables live at, and it quietly carried a claim about their speed that
nothing had ever tested.

Twelve cycles across the three table reads in `$80:C59C`, forty across the ten
in `$80:C379`, eight in each icon routine. Corrected, and the shim now declines
to price any call whose data bank is `$80` or above, so the assumption is a
condition rather than a hope. `tools/cycles816.py` grew a `--db` for the same
reason, and its docstring leads with this because it is the trap the tool is
most likely to hand somebody else.

Nothing about the port's *behaviour* was wrong here, and nothing in eleven
rounds of WRAM diffing could have found it: this is a fact about how long an
instruction takes, and the only instrument that asks that question is the one
this round built.

### ...and a second one, which the corpus found and one movie could not

Four movies of five said the models were now exact. The corpus said otherwise —
and the number that mattered was not the aggregate but the three movies it
isolated: `level1-map`, `level49-bubble` and `level49-corner`. HDMA explained
the first. It did not explain the other two, where no channel was armed at all
and the shortest path in the whole cluster was still wrong:

    hud_panel2   model 86, ROM 98, out by 12 — D=$0C00, DB=$00

Eighty-six cycles is `LDA $1E8A : BNE : RTS` and there is nothing in it to get
wrong. A trace of that PC, on that movie, priced each instruction twice over:

    $80:C139  LDA $1E8A   34 cycles ... or 40
    $80:C13C  BNE         12        ... or 16
    $80:C13E  RTS         40        ... or 42

Six, four and two — **two cycles per byte of the three instructions**, which is
one thing: the opcodes and operands are being fetched at 8 cycles a byte instead
of 6. **`$420D` is not always set.** The boot code turns FastROM on at
`$80:80A2`, that is the only write to the register in the image, and this
project has been assuming since Phase 1 that it therefore stays on. On level 49
it does not.

The fix generalises rather than patching: a modelled run is now a `CosimRun` —
what it costs with FastROM on, and **its length in program bytes**, since every
byte of an instruction is fetched from the program stream exactly once. The cost
is `cycles + 2 * bytes` while the register is clear, and `$420D`'s state travels
to the shim in `CosimRegs::fastrom` alongside the direct page and the data bank,
which is where it belongs: it is part of the machine the call was made on.
`tools/cycles816.py` prints the byte count next to the cycle count for the same
reason.

Two assumptions about this cartridge, both a decade older than this port, both
wrong, both found in one round by the same instrument — and neither of them
findable by comparing memory, because neither of them changes a byte.

### Results

Over the whole 43-movie corpus, with everything in the registry enabled:

* **13,209,637 calls checked, 0 diverged** — unchanged, which is the point: the
  port's behaviour was not touched.
* **Branch coverage 432 of 538**, also unchanged.
* **248,498 calls priced themselves.** Of the 246,275 made while the PPU was
  not stealing cycles, **246,275 were refresh-exact** — every error a whole
  number of 40-cycle refreshes, on every call of every movie. The remaining
  2,223 were made under HDMA and every one of them errs in the right direction.

| routine | refresh-exact / priced, PPU quiet | priced under HDMA |
| --- | --- | --- |
| `actor_depth_sort` | 143,929 / 143,929 | 1,915 |
| `hud_refresh` | 51,172 / 51,172 | 154 |
| `hud_panel2` | 25,603 / 25,603 | 75 |
| `hud_panel1` | 25,571 / 25,571 | 79 |

Lockstep `run` on `level1`, `level25-2p` and `level45-race` agrees with every
previous round: differences confined to stacks and declared scratch, and no byte
of live game state ever differing.

`tools/verify_corpus.ps1` now sums the cost-model columns and fails the run if
any model is not exact, so this is a claim the corpus re-checks rather than one
this document asserts.

### What it fixed, and what it did not

Seven of the eight framebuffer probes were already identical and still are.
`level25-2p` still differs, and the bisection now says why: with the four
modelled routines in place the first prefix that parts it is the tenth,
`actor_cull` — another walk over the same display list, 138 to 8,688 cycles,
declared at one number.

That is not a disappointment, it is the shape of the remaining work, and it is
now measurable rather than mysterious. Ranked by how much clock error each
routine can inject over one movie — its spread times its call count, on
`level25-2p` at 3,000 frames:

| routine | calls | min | max | mean |
| --- | --- | --- | --- | --- |
| `apu_send` | 23,373 | 218 | 13,470 | 2,639 |
| `sprite_build_oam` | 1,518 | 5,864 | 118,252 | 34,527 |
| `actor_overlap_pass` | 1,518 | 88 | 45,954 | 8,011 |
| `camera_follow` | 2,168 | 266 | 13,264 | 897 |
| `actor_cull` | 1,518 | 138 | 8,688 | 2,523 |
| `boss_step` | 910 | 2,118 | 15,616 | 12,995 |

`sprite_build_oam` is the one that matters and the one that cannot be done in an
afternoon: a 20× spread over the whole sprite pass, whose cost is four emitters,
a pairwise overlap test and the three list walks above it. `apu_send` is
`verify_only` and never substituted, so its spread costs nothing today. The rest
are each about the size of the sort.

The mechanism is general, the tool makes each one mechanical, and the report
will refuse to let a wrong one through. What is left is doing them.

## Two more walks, and the reason they change nothing yet (2026-08-11)

The previous round ended by naming `actor_cull` as the routine that parts
`level25-2p`, so this round priced it, and priced `actor_overlap_pass` beside it.
Both models are exact. Neither moves a single framebuffer, and the reason why is
the most useful thing this round found.

### The two models

`$80:BCE2` is a walk with five decisions in it, and `$80:BEC9` is two nested
walks with five more. Both were done the way the sort was: `src/port/oam.c`
counts branch outcomes into an `ActorCullWork` / `ActorOverlapWork`, and
`src/cosim/routines.c` multiplies those counts by what each straight-line run of
65816 instructions costs. Seventeen blocks each, every constant off
`tools/cycles816.py`.

Each got the same free check before any of it ran. The cull's empty-list path is
`LDY #$0000 : LDX $1B5E : BEQ` taken plus `STY $9C : RTS` — 70 + 68 = **138**,
and 138 is exactly the minimum `verify` had already measured for it over the
corpus. The overlap pass's is `LDY $9C : BEQ` taken plus `RTL` — 28 + 18 + 42 =
**88**, against a measured minimum of 88. Two numbers arrived at from the listing
agreeing with two the harness measured from the ROM, before the models were
wired up at all.

Both then came out refresh-exact on the first attempt.

### The first model that declines

`actor_overlap_pass` is the first one that cannot price all of its own calls.
Sixteen of its seventeen blocks are straight lines, but `$80:BF0D PHY : JSR
$BE8F : PLY` enters a dispatch into two actor handlers, and what *those* cost is
a tree this port does not walk. Pricing the pass at 102 cycles for that block
would be a model that is wrong by however long the collision took.

So a pass with any hit in it reports **no cost at all** and falls back to its
declared mean. That is not a special case bolted on: it is the same shape as the
direct-page guard the sort already had, and it shows up the same way, as a
`priced` column below `checked`:

```
  actor_overlap_pass         2581       4420  +0..+1920, mean +436    2569/2569    12
```

2,581 of 4,420 priced on `level25-2p`, and all 2,569 of those made with the PPU
quiet refresh-exact. The other 1,839 dispatched.

### ...and then: all three walks have exactly one caller

Scanning the ROM for the three call sites settles something the previous round
assumed rather than checked:

| routine | callers |
| --- | --- |
| `$80:BC7F actor_depth_sort` | one — `$80:BD27` |
| `$80:BCE2 actor_cull` | one — `$80:BD2A` |
| `$80:BEC9 actor_overlap_pass` | one — `$80:BDCC` |

All three of those addresses are inside `$80:BD1F`..`$80:BDE2`, which is
`sprite_build_oam`. The sort, the cull and the overlap pass are not three
routines the game calls; they are three parts of one routine, and that routine is
itself in the registry.

Which means that with the full registry active, **none of these three models is
ever consulted.** `sprite_build_oam` is substituted first, the walks underneath
it never trap, and the clock advances by the outer routine's single declared
constant. The framebuffer probes say so: seven of eight identical before this
round and the same seven after, with `level25-2p` differing exactly as it did.

The models are not wrong, they are unreachable. Substituting each one *alone*,
where nothing sits above it, is the demonstration — `level25-2p` at frame 6,000
against `--stock`:

| substituted alone | frame 6,000 |
| --- | --- |
| `actor_depth_sort` | identical to stock |
| `actor_cull` | identical to stock |
| `actor_overlap_pass` | differs — 42% of its calls still unpriced |
| `sprite_frame_tile` | differs — unpriced |
| `sprite_build_oam` | differs — unpriced |

### The instrument that should have been built first

The previous round ranked what was left by *spread* — a routine's range times
its call count. That is the wrong quantity, and ranking by it is what put
`actor_cull` at the top of the list.

What actually parts a framebuffer is **drift**: how far a substituted routine
pushes the emulated clock away from where the ROM's own instructions would have
left it. That is `calls × (mean actual − declared)`, per routine, and it is
measurable from a `verify` run plus the registry's declared constants
(`scratchpad/drift.py`). A frame is 357,366 master cycles. On `level25-2p` at
6,000 frames, after this round:

| routine | calls | declared | mean | drift, in frames |
| --- | --- | --- | --- | --- |
| `sprite_build_oam` | 4,420 | 43,111 | 74,055 | **382.7** |
| `actor_overlap_pass` | 4,420 | 4,426 | 21,405 | 87.4 |
| `actor_obstacle_at_point` | 5,575 | 3,630 | 6,190 | 39.9 |
| `actor_notify_box` | 3,657 | 5,545 | 8,288 | 28.1 |
| `actor_nearest` | 5,442 | 7,195 | 8,653 | 22.2 |
| `actor_at_point` | 2,734 | 2,645 | 5,474 | 21.6 |

Two things fall out of it immediately. `apu_send` looked like the third-worst
offender by spread and contributes **nothing**, because it is `verify_only` and
never substituted. And `sprite_frame_tile` — 73,204 calls, the most-called
routine in the registry — drifts by 1.02 frames over the whole movie, which is
why the *bisection* blamed it: at frame 6,000 one frame of drift is enough to
part a screenshot, so a prefix test finds whichever routine comes first in
registry order, not whichever matters. The bisection was answering a different
question than the one being asked of it.

Ranking by drift also explains why the framebuffers did not move. Because the
inner walks are subsumed, the drift they used to inject was already being counted
inside `sprite_build_oam`'s 382.7 frames — the two numbers were never additive.

### Results

13,209,637 calls checked across the 43 movies, **0 diverged**, and branch
coverage holds at 432 of 538 — both unchanged from before the round, which is
what "no behaviour changed" looks like when it is measured rather than asserted.

Six cost models now, all exact over the corpus. `tools/verify_corpus.ps1` fails
the run if any of them is not, so this is re-derived on every sweep:

| routine | refresh-exact / priced, PPU quiet | priced under HDMA |
| --- | --- | --- |
| `actor_depth_sort` | 143,929 / 143,929 | 1,915 |
| `actor_cull` | 143,929 / 143,929 | 1,915 |
| `actor_overlap_pass` | 113,629 / 113,629 | 1,915 |
| `hud_refresh` | 51,172 / 51,172 | 154 |
| `hud_panel2` | 25,603 / 25,603 | 75 |
| `hud_panel1` | 25,571 / 25,571 | 79 |

503,833 calls priced with the PPU quiet, every one of them short by an exact
multiple of the 40-cycle refresh; 6,053 more priced under HDMA, none of them
over-claiming. The three sprite-pass walks share their HDMA count of 1,915,
which is another way of noticing they are all one routine.

Lockstep `run` on `level1`, `level25-2p` and `level45-race` still ends with no
byte of live game state ever differing.

Measured on `level25-2p` at 6,000 frames, total injected drift across every
substituted routine falls from 793.7 frames to 635.7 — a 20% cut that is
currently invisible, and will stop being invisible the moment the pass above it
is priced.

### What is left, precisely

`sprite_build_oam` is the whole ballgame: 382.7 frames of the remaining 635.7.
It is five parts, and three of them are now done:

| part | state |
| --- | --- |
| `$80:BC7F` the depth sort | **priced** |
| `$80:BCE2` the cull | **priced** |
| `$80:BC23 oam_buffer_clear` | not priced, but 4,790..4,830 — a fixed loop, one scanline of refresh wide |
| the emit walk, `$80:BD30`..`$80:BDCB` | not priced: needs `sprite_emit` per piece and `$80:B9D6 sprite_frame_tile` per lookup |
| `$80:BEC9` the overlap pass | **priced, except calls that dispatched** |

`sprite_frame_tile` is the one with real structure left in it: a cache lookup
whose hit path is eleven bytes and whose miss path walks a 256-entry ring at
`$80:B9F6` looking for a free slot, which is the whole of its 288..2,414 spread.
It is modelable the same way everything here has been — count the iterations —
and it is a round of its own.

## The pass, priced (2026-08-11)

The previous round ended with a table of five parts and three ticks. This round
fills in the other two and then prices the routine they are parts of, which is
the first time any of the six models on the sprite path is actually consulted.

### The constant that was not a constant

`$80:BC23 oam_buffer_clear` looked like nothing worth a round: `verify` measures
it at 4,790..4,830, a spread of exactly one refresh, and it was declared at its
mean of 4,814. It is genuinely constant — `PHD : LDA #$13BE : TCD` puts the
caller's page out of reach, `LDX #$0008` is the whole of its control flow, and
every address it touches is under `$2000`. Its instruction cost is 4,670 cycles
over 372 bytes and it never varies.

Which is 144 short of what it was declared at, and the difference is worth
naming because it applies to **every** routine still being burned as a constant.
`.cycles` is a mean of what `verify` *measured*, and a measurement is elapsed
time — it contains the three or four DRAM refreshes the call crossed.
`cycles_burn` then hands that number to the core in a single piece, and
`snes_runCycles` adds one refresh per call however many scanlines the call
spans. So the declared path burns `mean + 40`: for this routine 4,854 against a
real 4,790..4,830, about 43 cycles a call too slow, on every pass of every
movie. The modelled path reports instruction cycles only and hands them over
twelve at a time, so the core puts each refresh back exactly where the scanlines
are.

Pricing a constant is not redundant when the constant was arrived at by
measuring. It is also the easiest kind of drift to leave in place forever,
because a routine whose measured spread is 40 wide looks like one there is
nothing left to say about.

### `sprite_frame_tile`, and the byte column growing up

The frame cache came out exactly as the previous round predicted: eight blocks,
a hit path of 288 cycles and a miss path whose spread is entirely the ring walk
at `$80:B9F6`. 288 is also the minimum `verify` measures across the corpus, so
the model had a witness before it had a test.

The step has two flavours, which is the only part that is not mechanical.
`INX : INX : CPX #$0100 : BNE` falls through to `LDX #$0000 : BRA` on the 128th
slot, so wrapping costs one branch-not-taken and one taken `BRA` more than an
ordinary step. It happens about once every 64 misses: often enough to matter,
rare enough that a model which forgot it would still look right on a short
movie.

What this one changed was `CosimRun::bytes`. The routine reads its slot geometry
out of three ROM tables through the data bank — `LDA $B447,X`, `$B547,X`,
`$B647,X` — and with the data bank at `$80` those are fast ROM reads: 6 master
cycles a byte while `$420D` is set and 8 while it is clear, exactly like an
opcode fetch. `bytes` has always meant "bytes that cost 2 more with FastROM
off", so those data bytes belong in it. Every model before this one either
touched WRAM only or reached its table through a low bank, where the two counts
are the same number, so the distinction had never come up.
`tools/cycles816.py` now has a `fast_rom()` beside its `access()` and calls the
column FastROM bytes rather than program bytes.

### The four emitters are one table and three deltas

`assets/sprite.h` has claimed since Phase 2 that `$80:BA51` and its three
flipped twins "differ only in how a piece offset is negated and which OAM flip
bits get toggled, so they are one function here". Pricing them is a chance to
check that claim against the bytes rather than the prose, and it holds — with
one addition nobody had noticed.

Stripped of the mirror sequence `EOR #$FFFF : SEC : SBC #$000F`, the four
emitters are 105, 111, 111 and 111 bytes. Not equal. The extra six bytes are two
things:

* `EOR #$4000` / `#$8000` / `#$C000` on the finished OAM word — the flip toggle,
  one instruction with three operands, present in all three flipped emitters and
  absent from the unflipped one.
* `BEQ : JMP` where `$80:BAB5` has a `BNE`. Adding three bytes of body per piece
  pushed the loop-back target out of a relative branch's reach, so the flipped
  emitters pay two instructions to go round and a taken branch to stop.

So the model is `EMIT_COST[]` plus `EMIT_MIRROR` on the y blocks when the actor
is flipped vertically, the same on the x blocks when horizontally,
`EMIT_FLIP_EOR` on the emit blocks for any flip at all, and `EMIT_FAR_NEXT` /
`EMIT_FAR_DONE` on the loop tail. Twelve blocks and four deltas describe all
four emitters, which is the same claim `sprite_emit` makes by existing.

### Composing removes the guards

Every model so far has had to ask about its caller before it could price
anything. `actor_cull`, `actor_depth_sort` and `actor_overlap_pass` all check
`(d & $ff) == 0`, because an unaligned direct page costs an extra internal cycle
on every direct-page instruction; `sprite_frame_tile` checks the data bank as
well, for the three table reads above.

Reached from `sprite_build_oam` they need neither. `$80:BD21 PEA $0000 : PLD`
and `$80:BD25 PHK : PLB` establish page zero and bank `$80` for the whole pass,
so every precondition its parts have is satisfied by construction. Exactly two
instructions run outside that window — the `LDA $20` and `LDA $BDE6,X` after
`$80:BDD0 PLD : PLB` — and they are the only reason the pass's own model asks
about the caller at all. One of them is why `BUILD_EPILOGUE_SLOW_TABLE` exists:
the four-byte phase table is read through whatever data bank the `PLB` restored,
and a low bank costs 8 a byte rather than 6.

### The one thing that was wrong, and how it was found

The composed model was refresh-exact on `level1` — all 1,016 calls — and wrong
on 27% of `level25-2p`, with errors up to +16,770 that were not multiples of 40.

Guessing at that from the aggregate report went nowhere: a run declining on
each suspicious block in turn narrowed it a little and pointed at nothing. What
worked was making the harness print the block counts of the first few calls
whose error was not a refresh multiple, which took a temporary `zzz_dump` in
`record_model` and about ten minutes. The answer was in the second line of the
first dump:

    BAD actual=53540 model=46836 err=6704
      ovl: ... 10=2 12=2 ...

`OVL_BLK_Y_NEAR = 2`. Two pairs of actors were touching, and
`$80:BF0E JSR $BE8F` had dispatched twice into the collision handler tree.
`actor_overlap_pass`'s own shim has declined on exactly that since the round it
was written; composing it into `build_cycles` had quietly dropped the check.
Calls with one hit were short by about 4,300, calls with two by 6,704.

The fix is one clause, and the corpus proves it is the right one arithmetically:
`sprite_build_oam` prices 113,629 calls and `actor_overlap_pass` prices 113,629
calls, out of 143,929 passes. The two routines decline on precisely the same
30,300 — 21% of all passes contain a collision.

### Results

13,209,637 calls across the 43 movies, **0 diverged**; branch coverage 432 of
538, unchanged. Nine cost models now, all exact over the whole corpus:

| routine | refresh-exact / priced, PPU quiet | priced under HDMA |
| --- | --- | --- |
| `sprite_frame_tile` | 1,707,880 / 1,707,880 | 19,607 |
| `actor_depth_sort` | 143,929 / 143,929 | 1,915 |
| `actor_cull` | 143,929 / 143,929 | 1,915 |
| `oam_buffer_clear` | 143,929 / 143,929 | 1,915 |
| `actor_overlap_pass` | 113,629 / 113,629 | 1,915 |
| `sprite_build_oam` | 113,629 / 113,629 | 1,915 |
| `hud_refresh` | 51,172 / 51,172 | 154 |
| `hud_panel2` | 25,603 / 25,603 | 75 |
| `hud_panel1` | 25,571 / 25,571 | 79 |

2,469,271 calls priced with the PPU quiet, every one short by an exact multiple
of the 40-cycle refresh; 29,490 more under HDMA, none over-claiming. Lockstep
`run` on `level1`, `level25-2p` and `level45-race` still ends with no byte of
live game state ever differing.

Drift on `level25-2p` at 6,000 frames falls from 635.7 frames to **411.2** as
`drift.py` reports it, and to about **324** once its one blind spot is
subtracted: the instrument does not know about subsumption, so it still charges
`actor_overlap_pass` 87.4 frames for calls on which it is never substituted at
all. `sprite_build_oam` itself is down from 382.7 to 159.2, and all of what is
left is the 2,139 passes of 4,420 that contained a collision.

The framebuffers have not moved, and this time that was checked rather than
assumed: with the three new models gated behind an environment variable, the
same eight movies compared against `--stock` at 6,000 frames give the same
answers before and after. Five identical, three differing, no flips in either
direction. The three that differ — `level21-bubble`, `level25-2p`,
`level25-lane` — are the busy ones, which is to say the ones with collisions in
them.

### What is left, precisely

The sprite path is finished except for one thing, and it is not on the sprite
path:

| part | state |
| --- | --- |
| `$80:BC7F` the depth sort | **priced** |
| `$80:BCE2` the cull | **priced** |
| `$80:BC23` the buffer clear | **priced** — a constant, and 144 off the declared one |
| `$80:B9D6` the frame cache | **priced** |
| `$80:BA51` and its three twins | **priced** — one table, three deltas |
| `$80:BD30`..`$80:BDCB` the walk | **priced** |
| `$80:BEC9` the overlap pass | **priced, except the 21% of passes that dispatched** |

Everything above the line is done. What stops the last 159 frames is
`$80:BE8F`, the collision handler tree, and that is a different kind of problem
from every model in this document: it is not a walk whose iterations can be
counted, it is a dispatch into a hundred and something handlers, most of which
are ported and none of which has ever been asked what it cost. The next round is
either that, or it is the next routine down the drift table —
`actor_obstacle_at_point` at 39.9 frames, `actor_notify_box` at 28.1,
`actor_nearest` at 22.2 — none of which is subsumed by anything, so unlike this
round's work, pricing them would move a framebuffer the day it landed.

## Into the handler tree (2026-08-11)

The last round ended by naming `$80:BE8F` as the thing standing between the
sprite pass and a price, and by describing it as "not a walk whose iterations
can be counted, it is a dispatch into a hundred and something handlers, most of
which are ported and none of which has ever been asked what it cost." Two of
those clauses were wrong, and the round started by finding that out rather than
by believing it.

### What the census actually said

`$80:BE8F` is twenty-six instructions of straight line with two `JSL $80:8480`
in it. `$80:8480` is `thread_call_handler`, the general dispatcher, and it is
that routine — not the collision code — that opens onto the whole game.

So the question is how many handlers a *collision* can reach.
`src/port/collide.h` opened with a census that answered "five". Its own dispatch
chain, forty lines further down, has twenty-five. The header's list was written
when it was complete and the chain grew past it without anybody editing the
prose above — which is the ordinary way a comment goes stale, and a reason to
check the code under a comment before planning a round around it. The header now
says so itself.

Neither number is the one that matters. Counting the addresses that actually
came up, per movie, gives a much smaller and much more lopsided answer:

| movie | dispatches to a handler with no price |
| --- | --- |
| `level25-2p` | `$82:9660` 9,802 · `$81:C440` 2,700 · `$80:CAEE` 1,666 · `$81:CDDE` 212 |
| `level1-rescue` | `$81:8888` 2,451 · `$83:A364` 4 |
| `level45-carried` | `$81:C4A6` 4,269 · `$80:CAEE` 86 · `$83:A364` 2 |
| `level53` | `$81:8888` 530 · `$83:A364` 4 |

Four or five addresses a movie, one of them dominant. That is a work list, and
it is the reason this round priced four handlers rather than despairing at
twenty-five.

### A third column, because a handler is not on page zero

Every routine priced before this one ran with `D` at `$0000`, so every model in
this file quietly assumed direct-page instructions cost what the manual's base
figure says. Handlers do not: `$80:84A2  TCD` installs the *target thread's*
page, read from the 24-entry table at `$80:82DE`, and that table tiles
`$7E:0100-$7E:0CFF` at stride `$80`:

    slot  0  D=$0100   aligned
    slot  1  D=$0280   UNALIGNED
    ...
    slot 12  D=$0180   UNALIGNED
    slot 13  D=$0200   aligned

Twelve aligned, twelve not. A non-zero low byte costs one extra internal cycle
on *every* direct-page instruction, and `$80:F950` — the entry nearly every
collision in the game lands on — is six of those out of seventeen. A model that
ignored this would have been exactly right on half the actors in the game and
36 cycles out on the other half, which is close enough to look like a refresh
and wrong in a way that would have taken a long time to see.

So `CosimRun` grew a third field: how many of the run's instructions address
through `D`. `cosim_run_cycles_dp` adds six per instruction when the caller says
the page is unaligned, and the old two-argument `cosim_run_cycles` is now that
function with `false` — which is why none of the seventeen existing call sites
had to change. `tools/cycles816.py` reports the count alongside the other two.

### The harness caught the tool again

The dispatcher's frame is four blocks and no table: its control flow is two
branches. It priced at 140 cycles for the no-handler path — which is *exactly*
the minimum `verify` measures for `thread_call_handler` across the corpus — and
504 for the path that enters a handler.

504 on the second attempt. The first said 510, and the first report was
`+0..+40` on the 394 calls that entered no handler and **`-6`** on all 641 that
did. A negative error is the one thing a cost model is never allowed to produce,
and it localised the bug to the frame in one run. Reading `cpu.c` against a
hand-count of the twenty instructions found two mistakes in
`tools/cycles816.py`, both mine, and both older than this round:

* **`PHA` and `PLA` ignored `m`; `PHX`/`PHY`/`PLX`/`PLY` ignored `x`.** The
  table hardcoded two bytes of stack traffic. `case 0x48` in `cpu.c` pushes one
  byte when `cpu->mf` is set, and `$80:848F` and `$80:8496` are both `PHA` under
  `SEP #$20` — so the tool charged 8 cycles too many, twice.
* **`PEA` was priced as a *read* of its operand.** It decodes as an absolute
  address, fell through to the generic memory path, and got charged for loading
  `$84A4` from ROM instead of pushing it onto the stack: 24 where the answer is
  34.

The two errors ran in opposite directions and cancelled to within 6 cycles,
which is the interesting part. A tool wrong in two places by 16 and 10 looks
like a tool wrong by 6, and 6 is small enough to be mistaken for a modelling
subtlety rather than an arithmetic bug. What made it findable is that the
harness compares against the real hardware timing on every call, so the residual
had a sign, and the sign was impossible.

This is the second round in a row where a cost model's first report found a bug
in the instrument rather than in the model — `XBA` last time, the stack widths
this time. Six models were refresh-exact over the whole corpus while both bugs
were live, which says only that none of them contained an 8-bit push.

### Pricing a routine that cannot be priced

`thread_call_handler` is the first routine here that is genuinely unpriceable as
a whole. It enters twenty-five different handlers and the general case is every
behaviour in the game, so there is no table that describes it.

What it *can* do is price its own frame and add the handler's cost when the
handler is one that has a table — and decline, by name, when it is not. That is
the same bargain `actor_overlap_pass` has been making since it was written, one
level further down, and it generalises: `ThreadCallWork` carries the entry
address it dispatched to and the work struct of whichever handler ran, and
`thread_call_cycles` is a `switch` with a `default: return false`.

Four handlers went in:

| handler | what it is | witness |
| --- | --- | --- |
| `$81:FE0E  shot_collide` | four `CMP`s and a five-instruction tail | fly-through prices at 156; measured minimum 156 |
| `$80:F7F7  player_collide` | a 92-entry jump table on the other actor's id | out-of-range exit prices at 90; measured minimum 90 |
| `$80:F950  player_collide_hurt` | the entry nearly every collision reaches | — |
| `$81:8888  enemy_collide` | the enemy mirror, two flat exits priced | ignore path prices at 84; measured minimum 84 |

Each of the four unrolls a `CMP` chain, and in each case the chain is the whole
cost model: what an id costs is *where it sits in the chain* and nothing else.
`shot_collide`'s four stop ids are four blocks for that reason and no other —
they do identical work and are reached 30, 60, 90 and 120 cycles in. The same
goes for the two state exits of `$80:F950`, which the port tests with a single
`state == 2 || state == 4` and which the model has to tell apart.

`enemy_collide` is the one that declines a lot. Its two flat exits are priced;
its other four leave through a `JML` or a `JSR` into `enemy_freeze`,
`enemy_bubble_react`, `enemy_die` (and `score_add` under it) or
`enemy_survived_react` (and sometimes `rng_next`). Each is a tree of its own, so
the model prices what returns from inside `$81:8888` and says nothing about the
rest.

### Composing, and the veto moving down

With handlers priced, the chain above them falls in order:

`actor_collide_notify` has no branches at all — twenty-six instructions and two
`JSL`s — so its own cost is one constant, 856 cycles over 58 bytes. Its model is
that constant plus two dispatches, and **856 + 2 × 140 = 1,136, which is exactly
the minimum `verify` measures for it.** Three of the five new models had a
witness before they had a test.

`actor_overlap_pass` had refused to price any pass containing a hit since the
round it was written. That refusal is now narrower rather than absolute: each
hit records the two dispatches it made, and a pass is priced when every handler
it entered has a table. The `hits` field survives, but it means something
different — it is a bound check now, not a veto, because a pass with more hits
than `ActorOverlapWork` can describe has to decline for a different reason and
the two should not be confused.

`sprite_build_oam` inherits that, exactly as it inherited the refusal. The clause
added last round — `work.overlap.hits == 0` — is gone, and `build_cycles`
returns a bool instead.

### Results

The nine models from last round still hold, and there are five more. On the
43-movie corpus: 13,209,637 calls checked, **0 diverged**; branch coverage 432 of
538. Both unchanged from before the round.

| routine | refresh-exact / priced, PPU quiet | priced under HDMA |
| --- | --- | --- |
| `sprite_frame_tile` | 1,707,880 / 1,707,880 | 19,607 |
| `actor_depth_sort` | 143,929 / 143,929 | 1,915 |
| `actor_cull` | 143,929 / 143,929 | 1,915 |
| `oam_buffer_clear` | 143,929 / 143,929 | 1,915 |
| `actor_overlap_pass` | 131,922 / 131,922 | 1,915 |
| `sprite_build_oam` | 131,922 / 131,922 | 1,915 |
| `thread_call_handler` | 57,336 / 57,336 | 0 |
| `hud_refresh` | 51,172 / 51,172 | 154 |
| `player_collide` | 32,771 / 32,771 | 0 |
| `hud_panel2` | 25,603 / 25,603 | 75 |
| `hud_panel1` | 25,571 / 25,571 | 79 |
| `actor_collide_notify` | 22,361 / 22,361 | 0 |
| `shot_collide` | 8,052 / 8,052 | 0 |
| `enemy_collide` | 5,602 / 5,602 | 0 |

2,631,979 calls priced with the PPU quiet, up from 2,469,271, every one short by
an exact multiple of the 40-cycle refresh; 29,490 more under HDMA, none
over-claiming. No `MODEL WRONG` anywhere. `actor_overlap_pass` and
`sprite_build_oam` still price the identical set of passes, and that set has
grown from 113,629 to **131,922** of 143,929 — the 18,293 passes whose
collisions all landed on one of the four priced handlers.

Lockstep `run` on `level1`, `level25-2p` and `level45-race` still ends with no
byte of live game state ever differing.

Drift on `level25-2p` at 6,000 frames falls from 411.2 frames to **341.4** as
`drift.py` reports it. `sprite_build_oam` itself goes from 159.2 to 115.2, and
it now prices 3,090 of the movie's 4,420 passes rather than 2,281. On
`level1-rescue`, where the collisions are the ordinary kind rather than a boss
fight, it prices **4,615 of 4,616**.

The instrument's blind spot is unchanged and worth restating: it does not know
about subsumption, so it still charges `actor_overlap_pass` 63.2 frames for
calls on which it is never substituted at all. Subtracting that, the real figure
is about 278 frames, down from about 324.

### What is left, precisely

The handler tree is no longer the wall it was described as; it is a list, and
the list is short. In corpus order of what it would buy:

| handler | where it dominates |
| --- | --- |
| `$82:9660  boss_9660_collide` | `level25-2p`, 9,802 dispatches — far the largest single item |
| `$81:C4A6  monster_collide` | `level45-carried`, 4,269 |
| `$81:C440  monster_c440_collide` | `level25-2p`, 2,700 |
| `$80:CAEE  object_collide` | `level25-2p`, 1,666 |
| `$81:8888`'s four deep exits | `level1-rescue` and `level53` |

Below those, the drift table is unchanged and still names
`actor_obstacle_at_point` at 39.9 frames, `actor_notify_box` at 28.1,
`actor_nearest` at 22.2 and `actor_at_point` at 21.6 — four routines that are
subsumed by nothing, so unlike the sprite pass they would move a framebuffer the
day they landed.

## The boss handler, and the first routine `verify` cannot check (2026-08-11)

This round has two halves and they are not the same kind of work. The first
finished the handler tree's largest item and found out that finishing it did not
buy what the last round predicted it would. The second is the first piece of
Phase 4 in this file.

### The hypothesis, and what measuring it cost

The last round left `$82:9660 boss_9660_collide` at the top of the work list —
9,802 dispatches on `level25-2p`, far the largest single item — and `run`'s
lockstep on that same movie stops early:

    The two timelines part at pass 1231: stock is on frame 410 and
    native on frame 409, so one of them overran vblank on a pass the
    other did not.

One movie, one handler, and the movie is the handler's own level. That is a
strong enough coincidence to be worth an hour, and the hour is what settled it:
**it was wrong.** The handler is priced, the model is exact, and `level25-2p`
parts at pass 1231 exactly as it did before.

Why it was wrong is the useful part. `actor_overlap_pass` vetoes a whole pass
when *any* handler it entered has no table, and `level25-2p`'s census has four
addresses on it, not one. Pricing the biggest of the four took
`thread_call_handler`'s own priced set from 57,336 calls to **103,161** — nearly
double — and moved `sprite_build_oam` from 3,090 priced passes to **3,103**.
Thirteen. The dispatcher is priced on far more calls and the pass above it is
priced on thirteen more, because the passes that were declining were declining
for `$81:C440` and `$80:CAEE` as well, and an all-or-nothing veto does not care
which of its reasons you remove.

So the drift on that movie fell from 341.4 frames to 338.6, and the number that
was supposed to move — `sprite_build_oam`'s 114 frames — did not move at all.

**The honest conclusion is that the lockstep parting is not a one-round fix.**
Even with all four handlers priced, the four largest unsubsumed routines below
them are still unpriced — `actor_obstacle_at_point` at 39.9 frames,
`actor_notify_box` at 28.1, `actor_nearest` at 22.2, `actor_at_point` at 21.6 —
and any of them can tip a marginal pass over a vblank boundary. Buying back
whole-corpus lockstep means clearing most of the drift table, not the top of it.

### What the parting is actually worth, which is less than the table says

Worth stating plainly, because this document has been over-charging it for
several rounds. `drift.py` reports 341 frames of injected error per 6,000. What
`run` exhibits is **zero frame-level divergence for 1,230 passes and then one
frame**. The gap between those two numbers is in the same report:

      work        180,236,938 of 431,832,408   41.7%
                  702,727,814 more spent halted on the scheduler's WAI
                  104,548,578 going round the declared busy-wait loops

The CPU is **halted on `WAI` for about 57% of every cycle the machine runs**. The
frame is self-synchronising: charge a routine too little and the scheduler sits
on the `WAI` longer and nothing observable happens. Mis-pricing only becomes
behaviour on a pass heavy enough to cross the vblank boundary — a threshold
event, not an accumulation. The drift table measures injected cycles; the `WAI`
absorbs nearly all of them.

That does not make the table useless. It makes it a ranking rather than a
budget, and it means the sentence "341 frames of drift" should never again be
read as "341 frames wrong".

### The model, and the four blocks that are one decision

`$82:9660` is fourteen blocks, and two features of it are worth the space.

**Four ids are answered as some other id, and two of them on a coin toss.** `$62`
and `$70` both come out as `$5C` or `$5D` depending on `LDA $0020 : AND #$0001`
and `AND #$0003` — the scheduler tick read straight. They give the same two
answers, so the port tests them together, and **the model has to tell them
apart**, because `$70` is tested at `$82:968F` and `$62` at `$82:967D`: one
comparison and one taken branch further down the chain, for an identical result.
That is `shot_collide`'s four stop ids again, and it is the third time the same
shape has come up — what a path costs in an unrolled `CMP` chain is where it sits
in the chain and nothing else.

**It is the first handler priced whose costs are not all direct-page.** `LDY
$0078`, `LDX $000E,Y` and `LDA $0020` are absolute reads of low WRAM through the
data bank; `$3A` through `$44` are on the thread's own page. The third column
separates them, and `$82:948F` writing absolute `$003C` as a *coordinate* in the
routine that seeds direct `$3C` to 70 is what makes confusing them plausible.

It also has no `verify` minimum to check against — the handler is not in the
registry and is only ever reached through `$80:8480` — so the witness is the
dispatcher's own residual: `+0..+80, mean +27` on `level25-2p`, **12,773 of
12,773 refresh-exact**, and 103,161 of 103,161 across the corpus. A table wrong
anywhere makes that negative somewhere, which is the property the last two rounds
have been leaning on.

### And then: a routine `verify` structurally cannot check

`$80:C05A sprite_cache_init` is 16,900 instructions of `STA $2128,X : DEX : DEX
: BPL` — a `$2002`-byte `memset`, 0.97 of a frame — so an NMI lands inside
virtually every call. `verify` abandons all of them: two calls on `boot.zmv`, two
on `level1.zmv`, zero checked, four interrupted. For three rounds this file and
`tools/native_share.py` both recorded that as **unregisterable**, alongside
`lzss_decompress` and `blockmap_expand`.

That reading was one instrument too narrow, and noticing it is this round's real
result. **An interrupt breaks rewind-and-replay. It breaks nothing about
substitution.** What `verify` cannot survive is that the ROM's NMI handler wrote
WRAM inside the call window, and the port models the routine rather than the
handler — so the diff would report the harness's problem as the port's. Under
`run` the ROM never executes the routine at all: the core waits at the
instruction after the `JSL` while the budget is burned, and takes any NMI that
falls due exactly as it would have.

So there is a new flag, `CosimRoutine::run_only`, and it is the mirror of
`verify_only` in mechanism and in honesty:

| | `verify_only` | `run_only` |
| --- | --- | --- |
| what it means | checked on every call, never substituted | substituted, never checked on a call |
| why | the body is a bus handshake — `$80:CCC8` | an interrupt lands inside the call window |
| what stands behind it | 128 KB diffed per call | 128 KB diffed per **scheduler pass** |

The last row is the point. A `run_only` routine is not unchecked; it is checked
by a claim about a *stretch* rather than about a call — which is the only claim
Phase 4 can be built on, because a port that owns its own main loop has no
per-call boundary left to rewind to. This is the first routine in the project
whose correctness rests on it.

Two conditions before anything else gets the flag, and both are load-bearing:

* **`verify` must be unable to score it, not merely unwilling** — structurally,
  with the interrupted count in a report as the evidence.
* **`.cycles` must be a count, not a mean.** Every other entry's budget is an
  average `verify` observed; there is nothing here to have observed. `$80:C05A`
  has no data dependence and one loop with a known trip count, so
  `tools/cycles816.py` prices it exactly:

      prologue $C05A..$C06A                                       184
      loop 1   4,097 x (STA abs,X + DEX + DEX) + 4,096 taken BPL  335,948
      LDX #$00FE                                                   18
      loop 2   128 x the same + 127 taken BPL                   10,490
      epilogue PLB : PLB : RTL                                      94
      -------------------------------------------------------------------
                                                                346,734

  `BPL` and not `BNE` is why the trip counts are 4,097 and 128 — the same
  off-by-one that makes the routine write `$2002` bytes rather than `$2000`,
  which `port/sprite_cache.h` records from the other direction. A routine whose
  cost varies with its input could not honestly be given a constant nobody
  watched.

**0.97 of a frame is a margin, not a coincidence.** The core takes at most one
pending interrupt when it resumes, so a substituted call spanning two NMI
boundaries would leave one NMI un-taken that the ROM took — which is what
"outlives a frame" should have meant all along, and it is a real limit rather
than a bookkeeping one. At 346,734 the call cannot straddle two. With FastROM
*off* the same routine costs 405,930, which can. Measured, `run` burns 357,194
master cycles a call including the refreshes the core adds — 346,734 plus 261 of
them — so `$420D` is set on every call any movie makes, and the margin is
measured rather than assumed.

### Results

The corpus is unchanged where it should be and moved where it should have:
**13,209,637 calls checked across 43 movies, 0 diverged; branch coverage 432 of
538.** Fifteen cost models, every priced call refresh-exact, `thread_call_handler`
at 103,161 from 57,336. `run` compares 1,989 of 1,989 passes on `level1`,
`level1-rescue`, `level45-race`, `level49-corner` and `level53` with no byte of
live game state ever differing, and `level25-2p` still parts at pass 1231.

The native share moves for the first time in several rounds, because for several
rounds nothing had been added to the registry:

| | before | after |
| --- | --- | --- |
| registry entries | 111 | **112** |
| dynamic share written | 64.6% | **64.7%** |
| dynamic share substituted | 51.6% | **51.8%** |

`native_share.py` reports the new category in its own right rather than folding
it into the substituted figure silently, for the same reason it reports
`verify_only` separately: the two numbers answer different questions and this one
now has three parts.

### What is left, precisely

The handler tree still has `$81:C4A6 monster_collide` (4,269 dispatches on
`level45-carried`), `$81:C440` (2,700), `$80:CAEE object_collide` (1,666) and
`$81:8888`'s four deep exits — but this round is the reason to stop there. They
buy accuracy in a number the `WAI` is already absorbing, they do not buy back
lockstep on their own, and the cut makes them moot.

What is worth doing next is the rest of what `run_only` opened. Three addresses
sat in `BLOCKED` for the same wrong reason and are worth **7.1% of everything the
game does** between them:

| | share | what it needs |
| --- | --- | --- |
| `$80:CD20 lzss_decompress` | 5.8% | written; spans many frames, so more than one NMI is due — the margin `$80:C05A` has and this does not |
| `$80:CC7C apu_load_set` | 0.8% | written; every command waits on the SPC700, so it needs `apu.h`'s split as well |
| `$80:AD2B blockmap_expand` | 0.5% | written; six calls, and the same multi-frame problem |

All three are already in C. None of them clears the one-NMI margin, which makes
the next question concrete rather than architectural: **what does a substituted
routine do when more than one interrupt falls due inside it?** The answer is
almost certainly to let the core take them — park the CPU at the return, burn the
budget in pieces, and run the pending NMI handler between pieces — and that is
the same primitive the scheduler will need in the other direction when the port
owns the frame and has to resume a thread body that is still the ROM's.

## Parking a burn, and the second routine `verify` cannot check (2026-08-11)

The last round opened `run_only` and closed on a question rather than a result:
*what does a substituted routine do when more than one interrupt falls due
inside it?* This round answers it, and then spends the answer on the routine
that needed it.

### The bug that was hiding behind `$80:C05A`'s margin

Burning a cycle budget executes no instructions. `snes_runCycles` drives the
PPU, the APU and the timers; the CPU is driven separately by `snes_runCpuCycle`,
and a substituted call never touches it. So an NMI that falls due *inside* a burn
is not taken inside it — it waits, and `cpu->nmiWanted` is a single `bool`.

Last round drew the right conclusion from that and drew it too narrowly. The
conclusion was that a call spanning two vblank boundaries would drop one of the
game's NMIs, and that `$80:C05A sprite_cache_init` at 0.97 of a frame safely
cannot. Both true. What went unnoticed is the *other* half of the same fact:
even a call that spans only one boundary hands the interrupt over **late**, by
however much of the budget was left when it fell due. For a 92-cycle dispatcher
that is nothing. Measured on `boot.zmv`, `$80:C05A` was handing the core its NMI
**18,126 cycles late** — thirteen scanlines into a vblank that had already
started. It worked because the screen is off while the sprite cache is built. It
was luck, and the margin that made the NMI *count* right was doing nothing at all
about *when*.

### What replaced it

`CosimBurn`, in `src/cosim/cosim.c`. A substituted call's budget is now owed
rather than spent, and the CPU is parked on the routine's own entry instruction
while it is paid off:

* the budget is spent in pieces, and stopped the moment an interrupt comes due;
* the core then takes it **from that instruction** — which is exactly where the
  ROM's version of the routine would have been standing;
* the handler runs through the harness's normal loop, so its own calls are
  intercepted and counted like anyone else's;
* the `RTI` lands back on the entry instruction, and the rest of the budget is
  spent.

The one liberty it takes with the core is a store to `cpu->intWanted`, and it is
the honest one: that flag is a latch only `cpu_checkInt` refreshes, from inside
an executing instruction — which during a burn is precisely what is not
happening. The ROM's version of the routine *was* executing instructions, and one
of them would have latched exactly this. Everything else about taking the
interrupt — the pushes, the vector, the handler, the `RTI` — is the core's own.

**Nothing about this is scoped to long routines**, and that is the point. It is
the same code path for a 92-cycle call, which simply never stops early: the
budget is spent in full before the call returns and no state is parked at all.
The corpus is the evidence, and it is unchanged to the digit — **13,209,637 calls
across 43 movies, 0 diverged, branch coverage 432 of 538**, and all fourteen cost
models still refresh-exact on every priced call, `thread_call_handler` at
103,161 of 103,161. No second code path was needed to keep them safe, which is
the argument for there not being one.

Two things stayed out of it deliberately. A resumable routine's *segments* still
burn on the spot, because a segment is bounded by the `thread_yield` it reaches
this frame, so at most one interrupt can fall due in one and the core's single
latch already handles that; parking one would mean deferring the choice between
suspending and returning as well, for no question about NMI counting. And a
declared *mean* is still burned whole rather than in twelve-cycle pieces, because
a mean is what `verify` watched the ROM's elapsed cycles do, refreshes included,
and chopping it up would add a second set on top of the ones already inside the
average.

`run` reports the parks, and that number is not a diagnostic. It is the evidence
for the one thing about a long substitution that comparing memory cannot check:
how many NMIs the port owed the game and paid.

### `$80:AD2B blockmap_expand` — ten frames of it

The routine that needed all of the above. It walks the block map once at level
load and expands every cell into 64 tiles of the real map in bank `$7F` — the map
`port/terrain.h` reads for the rest of the level and `port/camera.h` scrolls
across. One call is about ten frames, so `verify` reported **one call, one
interruption, nothing checked** on `level1`, `level1-rescue`, `level9`,
`level25-boss` and `level53` alike, and this file recorded it as unregisterable
for several rounds.

That was the same error `$80:C05A`'s entry was: a fact about rewind-and-replay
reported as a fact about substitution. It is `run_only` now, and ten frames is
ten NMIs, so it is also the first routine that could not have been substituted
honestly before this round.

**Its cost is a count, which `run_only` requires and which is available here for
an unusually strong reason: no branch in the routine depends on the data it
reads.** The nest is `$B0` rows x `$AE >> 1` cells x 8 block rows x 8 words, and
every trip count falls out of the level record. So the port counts the four loops
and the two questions about which memory the operands came from
(`BlockExpandWork`), and `routines.c` prices them — the ordinary `_counted`
pattern, with the difference that the answer is exact rather than an average.

It is counted rather than computed in the shim from `$AE` and `$B0`, and that is
not fussiness. Both loops are do-whiles, so a map whose `$AE >> 1` is zero is
walked 65,536 times and not none; a shim reading the same two words would price
such a call at nothing. Counting what the loop actually did cannot make that
mistake.

### Three independent checks on one model

Worth setting out, because a `run_only` routine has no per-call residual and the
temptation is to assert the model instead of testing it.

**The shape.** Level 1 is `$AE`=44, so 22 columns, and `$B0`=13 rows: **286
cells**. `verify` independently measures `blockmap_cell_ptr` taking **286 calls**
on `level1.zmv` — a count made by a different mechanism, of a routine registered
in its own right, that the model never consults.

**The unit price.** That same row reports `334..374, mean 345` master cycles. Its
floor, 334, is to the cycle what `tools/cycles816.py` prices its 21 bytes at, and
the spread above it is 40 — one DRAM refresh. So the tool that priced every block
of this model is checked against a real measurement taken *inside this very
call*.

**The total.** `verify` cannot score the call, but that is a limit of
rewind-and-replay and not of the clock, so the ROM's own execution was timed
directly: cycles spent with the program counter inside `$80:ACF6..$80:AD91`, with
the eleven interrupts that land in the middle attributed to the handler where
they belong.

      measured, the ROM's own instructions      3,713,172
      less 2,722 DRAM refreshes at 40             -108,880
      ------------------------------------------------------
                                                3,604,292
      the model                                 3,604,294
      ------------------------------------------------------
      residual                                         -2

Two cycles on three and a half million. Two is not zero and the difference is the
probe's rather than the model's — a `snes_runCycle` granule is two master cycles,
so which side of a boundary a step is attributed to is worth exactly this much.
It is stated as a measurement and not as the refresh-exact residual the per-call
models are held to, because that standard needs `verify` and `verify` is the
thing this routine cannot have.

Ten point zero nine frames, which is where the parked burn stops being an
argument and starts being a requirement.

### Results

**Six levels' maps, built in C, byte-exact.** `run` reaches `blockmap_expand` on
every movie tried but `boot.zmv`, and every one of them compares clean:

| movie | passes | `blockmap_expand` | parked burns |
| --- | --- | --- | --- |
| `level1` | 2,389 of 2,389 | 1 call, OK | 14 |
| `level1-rescue` | 1,989 of 1,989 | 1 call, OK | 14 |
| `level45-race` | 1,989 of 1,989 | 1 call, OK | 17 |
| `level49-corner` | 1,989 of 1,989 | 1 call, OK | 12 |
| `level53` | 1,989 of 1,989 | 1 call, OK | 10 |
| `level25-2p` | 1,220 of 1,220 | 1 call, OK | — |
| `boot` | 1,990 of 1,990 | not reached | 1 |

No byte of live game state ever differed on any of them, and `level25-2p` still
parts at pass 1231 exactly as it did before — the boss-handler round's finding is
untouched. That is the whole tile map of six different levels compared against
the ROM's, 128 KB at a time, on every scheduler pass of every movie, and it is
**the first check this routine has ever had**: `verify` never completed a call of
it.

The park counts are worth a second look, because they are not noise. `boot.zmv`
reaches no map build at all and parks once — that one is `sprite_cache_init`, the
call this round found was thirteen scanlines late. The rest sit between 10 and
17, and what varies is the number of vblank boundaries that level's map build
crosses, which is the model answering to the geometry in the level record.
Before this round every one of these numbers was zero, and not because it was
true.

An earlier reading of the corpus said only the seven `level1` movies reach a
level load, and that was an artefact of asking `verify`: its `-f` counts PPU
frames and `run`'s counts scheduler passes, so the same budget buys much more of
a movie under `run`. The coverage here is six distinct maps, not one.

| | before | after |
| --- | --- | --- |
| registry entries | 112 | **113** |
| dynamic share written | 62.8% | **63.2%** |
| dynamic share substituted | 49.6% | **50.1%** |
| distinct code bytes executed | 26.6% | **26.9%** |

Measured against a baseline registry run for the purpose rather than quoted from
the last round, and that is worth a word: the 64.6%/51.6% pair this file's
previous section reports does not reproduce against the eleven-movie profile set,
so the delta above is the number to trust and the absolutes there are not.

`tools/cycles816.py` grew stack-relative addressing along the way — `ADC $01,S`,
which both of the pointer helpers use to reach past the return address a `JSL`
just pushed. The mode's whole point is that its address is built from S rather
than D, so it has no direct-page penalty; the file already said so in a comment
next to `DP_MODES` and then raised `Unpriced` when it met one.

### What is left, and what it now costs

Two addresses, and neither is blocked by the harness any more:

| | share | what it still needs |
| --- | --- | --- |
| `$80:CD20 lzss_decompress` | 5.8% | a counted cost model — its branches *do* depend on the stream, so this is the first `run_only` whose price cannot be exact |
| `$80:CC7C apu_load_set` | 0.8% | `apu.h`'s split, because every one of its ~23,800 commands waits on the SPC700 |

`lzss_decompress` is the interesting one, and not only for its size. Every entry
in the registry so far is priced either by a mean `verify` measured or by a count
nothing had to watch. This would be the first that is neither: a model of a
data-dependent routine with no per-call residual to check it against. The two
leaves under it, `lzss_read_byte` and `lzss_write_byte`, are registered and
`verify` scores both — so the honest construction is to price those against their
own measured residuals and let the body carry only what is left, which is the
loop structure the stream decides. That is a smaller unchecked claim than pricing
the whole thing, and it is worth building rather than assuming.

One coverage gap to state plainly. Six maps are checked, but every level record
the corpus reaches keeps its block map in the cartridge and its block library in
`$7E`. So `blockmap_expand`'s WRAM-map branch and its cartridge-library branch
are both modelled and neither is exercised. `cells_rom` and `words_rom` are
counted rather than assumed so that the day one of them is reached, the model
prices it instead of quietly pricing it wrong.

## The price a stream decides, and the instruction that was charged twice (2026-08-11)

The last section left `$80:CD20 lzss_decompress` on the ranking with a
prediction attached: that it would be *the first `run_only` whose price cannot be
exact*, because its branches depend on the compressed stream rather than on
anything a model can see in advance. It is registered now, and the prediction was
wrong in a way worth keeping on the page, because the reasoning behind it was
the kind that sounds careful.

### What "data-dependent" was worth as a reason

Every branch in the routine really is decided by the stream. How many tokens
there are, which of them are literals, how far each match reaches back and how
long it runs — none of that is in the arguments, none of it is in a table, and
nothing carries over between calls. So there is no function from the routine's
inputs to its cost.

That is true and it is not an obstacle, because **the port decompresses the same
stream.** It takes the same branches, in the same order, for the same reasons —
that is what being a transcription means — so it can count them. `LzssWork` in
`port/lzss.h` is the tally: two token counts, the flag refills, the bytes copied
out of the window, and how many source reads landed in the cartridge rather than
in WRAM. `lzss_cycles` in `cosim/routines.c` multiplies them by block prices.
The ordinary `_counted` pattern, and the only thing new about it is what it
answers.

**A price nobody can predict is not the same thing as a price nobody can
compute.** The distinction had never come up because every model before this one
was on the easy side of it.

Three things do have to be handled separately, and finding them was most of the
work:

* the `MVN $7E,$7E` at `$CD42`, which is one instruction paid for `$0FEE` times
  — `cpu.c` moves a byte and then rewinds PC by three rather than looping inside
  the opcode, so every byte re-fetches all three program bytes and then reads
  one, writes one and idles twice: 46 master cycles a byte, 187,588 for the
  window fill, better than half a frame before the routine has read anything;
* `LDA [$28]`, which is 48 master cycles against a cartridge stream and 52
  against one in WRAM — hence `reads_fast`, counted rather than assumed, and
  `head_fast` for the length word the prologue reads the same way;
* the three ways a stream can stop in the middle of a token, which pop different
  numbers of bytes on the way out.

That last one turned out not to be defensive coding. **Every stream this game
contains ends inside a match** — `end == 2` in the tally, the `BCS` at `$CD87`,
31 of 31 measured across fifteen movies — and the reason is the format rather
than the data: a flag byte carries eight bits and the stream runs out before all
eight are spent, so the leftover zero bits read as matches and the first of them
finds it empty. The tidy end-at-the-refill case the model was written around
first has not occurred once. Had the three partial exits been left out as
unreachable, every call in the game would have been priced short by the block the
routine really ran and by two bytes of stack it really popped — and X would have
come back as the `0` that case leaves rather than the 1, 3, 4 or 6 the real ones
do.

### It is exact

Not "refresh-exact", not "within a residual" — equal.

The check is a direct one. `verify` cannot score this routine, so the ROM's own
execution was timed instead: every instruction between `$80:CD20` and
`$80:CDF3`, with the interrupts that land in the middle attributed to the handler
where they belong, and DRAM refresh subtracted so that what is left is what
`cosim_cost` is defined to report.

| call | the ROM | the model |
| --- | --- | --- |
| 1 | 1,591,640 | 1,591,640 |
| 2 | 6,101,318 | 6,101,318 |
| 3 | 9,449,390 | 9,449,390 |
| 4 | 12,907,460 | 12,907,460 |
| 5 | 25,492,862 | 25,492,862 |

Five calls, 55.5 million master cycles, no residual anywhere. The instruction
counts agree too — 369,871 predicted against 369,871 executed on call 3 — and
`zamn_assets verify-lzss` independently reports 649 bytes in and 2,048 out for
the first stream, which is the tally's read and write counts arrived at by a
tool that knows nothing about cycles.

Getting there took a per-instruction histogram rather than an argument, and that
is the lesson from the two things it caught. Both were in the pricing, not in the
counting; both were invisible in aggregate and obvious per instruction.

**`tools/cycles816.py` was sizing `PHX`/`PHY`/`PLX`/`PLY` from `m`.** `cpu.c`
sizes them from `xf`, the file's own comment beside `STACK_OPS` says so, and
`operand_width` did not implement it because the four opcodes were missing from
`INDEX_OPS`. It only shows where the two widths differ, which is why it survived
this long — the game is 16-bit nearly everywhere. `$80:CDAE  PHX` and
`$80:CDB4  PLX` sit inside `SEP #$20 ... REP #$20` in the match loop, 8-bit A and
16-bit X, so each was priced 8 master cycles light: 16 per byte of every match
expanded, 27,376 on level 1's first stream alone.

**And a block was assembled out of the wrong column.** The tool prints a running
total, and the match head at `$CD82` follows the literal block at `$CD65` in
address order but not in execution order, so subtracting the cumulative figure at
`$CD63` charged every match for a literal it never ran. 332 cycles a match, in
the opposite direction from the `PLX` error and roughly cancelling it on the
first call, which is exactly how a plausible-looking total hides two mistakes.

### The instruction that was charged twice

The other thing the histogram turned up is not about this routine.

`native_return` finishes a substituted call by parking the CPU **on** the
routine's `ret_op` — the real `RTS` or `RTL` — and letting the core execute it,
so that the 65816 half of returning is done by the code that already does it
correctly. That is sound. What it means is that the return instruction's cycles
are spent by the machine, on top of whatever the budget was.

And `verify` measures a call from its entry to the return address, so what it
compares a reported cost against **includes** that same instruction. Every
checked model in `routines.c` is built to match `verify`, so every substituted
call has been overspending by one return instruction: 40 cycles for an `RTS`,
which is a DRAM refresh to the cycle and has therefore never looked like
anything, and 42 for an `RTL`.

The measurement is unambiguous — burn-end to back-at-the-caller ran 42 cycles
longer than the ROM's entry-to-`RTL`, on three consecutive calls, before anything
was changed. `LZSS_EPILOGUE` and `BLOCK_EPILOGUE` both drop their `RTL` now, and
both say why. Reconciling the two modes properly is a change to the harness
rather than to a model, and it belongs in a round of its own; these two are where
the difference is large enough to be worth not waiting for it.

### Results

| | before | after |
| --- | --- | --- |
| registry entries | 113 | **114** |
| dynamic share written | 63.2% | **68.4%** |
| dynamic share substituted | 50.1% | **57.3%** |
| substituted, waits out of the denominator | 57.9% | **66.3%** |
| distinct code bytes executed | 26.9% | **27.4%** |

Measured against a baseline registry built for the purpose rather than quoted
from the section above.

The substituted figure moves further than the written one, and the gap is the
interesting part. `lzss_read_byte` and `lzss_write_byte` have been in the
registry for several rounds as `verify_only` — written, checked on every one of
their 1,071,108 calls, and never substituted, because a mean standing in for a
98-to-298-cycle spread cannot survive a million calls packed inside one
decompression. That has not changed and will not. What changed is that their only
caller is substituted now, so **they never execute at all**, and 2.1% of the
corpus moved from "written but still running on the 65816" into the substituted
column without a line being altered in either of them.

`tools/native_share.py` was already right about this in its arithmetic and wrong
in its printing — the `verify_only` deduction counted them out and the list under
it named them anyway. It now lists only the ones a substituted build still runs,
and says how many it dropped and why.

The corpus is unchanged to the digit, which is what it has to be: `verify` never
intercepts a `run_only` routine, so registering this one cannot move it.
**13,209,637 calls across 43 movies, 0 diverged, branch coverage 432 of 538**,
and all fourteen cost models still refresh-exact on every priced call.

`run` compares all 128 KB of WRAM against a stock core once per scheduler pass,
and on all seven movies swept — `boot`, `level1`, `level1-rescue`, `level45-race`,
`level49-corner`, `level53`, `level25-2p` — **no byte of live game state ever
differed**, with every one of each movie's decompressions substituted — five on
the level movies, three on `boot`. Level 1's graphics are now built in C, checked
against the ROM's byte for byte, on the way into a level that then plays.

### What it cost, which is not nothing

The lockstep window shrank. Measured this round: `level1` compares 317 passes,
`level45-race` 1,045, `level49-corner` 1,141, `level53` 1,173, `level25-2p`
1,023, and `boot` the full 2,390 without parting at all. The figures the last
section recorded for the first five were 2,389, 1,989, 1,989, 1,989 and 1,231, so
between a half and seven eighths of each window has gone. (`boot`'s number does
not line up with the 1,990 recorded there and no attempt is made here to
reconcile them, for the same reason the share absolutes were not reconciled last
time: the like-for-like figures are the ones above.) Every one of them still
covers the level load in full, and the comparison stops for a timing reason
rather than a correctness one — but several hundred passes of gameplay that used
to be compared are not being compared any more, and that is evidence lost.

The cause is worth being precise about, because it is not the cost model.
Substituting these calls hands over 55 million master cycles in a single
scheduler pass, and during a burn the CPU is parked: an NMI is taken at a
twelve-cycle boundary rather than at the end of whatever instruction the ROM
would have been in the middle of. The handler then runs at a slightly different
phase, and its own duration depends on that phase — DMA aligns to eight-cycle
boundaries and a DRAM refresh falls on one side of a scanline end or the other.
Measured, the two timelines end an 8.2-million-cycle stretch **36 cycles** apart.
Sixteen frames later a scheduler pass lands across a vblank boundary on one side
and not on the other, and `run` stops comparing because nothing past there is
comparable.

Thirty-six cycles in eight million is the floor of what this mechanism can do
without knowing where the ROM's instruction boundaries were, which is the thing
substitution exists not to need. What would buy the window back is not a better
model but a harness that can resynchronise two timelines that have parted, rather
than giving up at the first pass where they disagree about which frame it is.
That is the largest single thing `run` could gain, and it is now the largest
thing standing between the corpus and a full-length lockstep of a level.

### What is left

| | share | what it needs |
| --- | --- | --- |
| `$80:CC7C apu_load_set` | 0.8% | `apu.h`'s split, because every one of its ~23,800 commands waits on the SPC700 |

One address left that is written and not registered, and it is the small half of
the sound problem. The large half is `$80:CCC8 apu_send`, which *is* registered,
is 11.0% of the corpus, and is `verify_only` and staying that way: a per-byte
handshake with a second processor is not something a cycle budget can stand in
for. Between them they are most of what separates the 68.4% written from the
57.3% substituted, and closing that gap is a question about how the port talks to
the SPC700 rather than about another routine.

Everything above them on the unnative ranking is structural rather than
unwritten — `thread_yield` at 4.4%, the two vblank-queue dispatchers at 3.2% and
1.1%, the NMI entry, the reset vector, and seven thread bodies that are resumed
by `RTL` from a parked frame and never called at all. Those are Phase 4's problem
by definition: they are the scheduler, and a scheduler cannot be substituted one
call at a time because nothing calls it.

The coverage gap from the last section stands unchanged and is worth repeating
because this round added one of its own. `blockmap_expand`'s WRAM-map branch and
its cartridge-library branch are modelled and neither is exercised; the same is
now true of `lzss_decompress`'s WRAM-source price — `head_fast` and `reads_fast`
came back saying *every* byte of *every* stream in fifteen movies was read out of
the cartridge with FastROM on — and of all three of its truncated-stream exits
other than `end == 2`. In both routines those are counted
rather than assumed, so the day a stream or a map takes one, the model prices it
instead of quietly pricing it wrong.

## The instruction that was charged twice, and what the parting is made of (2026-08-11)

Two things were owed from the last round and both are paid here, and paying the
second one turned up an answer to a question this file has been asking since
`run` existed. It is not the answer that was planned for.

### The instruction that was charged twice

`native_return` does not synthesise a return. It publishes the port's registers,
points the program counter at an `RTS` or `RTL` **belonging to the routine**, and
lets the core execute it -- borrowing the core's own stack and bank handling
rather than writing a second copy of it. That is a good decision and it stays.

What went with it and should not have is that the budget also contained that
instruction. `verify` measures a call from its entry PC to the caller's return
address, so what it hands a model to match *includes* the routine's own return;
every cost model in `routines.c` is built to that window because it is the only
window a measurement can be taken over. Under `run` the same models therefore
paid for the return and then the core executed it. Twice, every substituted call.

It hid for a long time behind a coincidence. An `RTS` here costs exactly 40
cycles -- a fetch at 6, three idles at 6, two stack reads at 8 -- and 40 is a
DRAM refresh, the one residual the model check is built to forgive. `RTL` costs
42 and that is what finally showed up, as an exact +42 on three consecutive
calls of `lzss_decompress` last round. The fix then was to take the `RTL` out of
that one model, with a note saying the general case was a harness change and
wanted doing on purpose.

It is done on purpose now. `tail_cycles()` prices the instruction the core is
about to be handed and `burn_plan` subtracts it, once, for every substituted
call -- and the two models that had been hand-corrected are put back to the
`verify` convention, so there is one rule and no routine has to remember it.
There are three tails, not one: `RTS` at 40, `RTL` at 42, and the
`JSL thread_yield` a suspending segment is handed at 54. The costs are not a
table copied out of the core; they are the core's own sequence of accesses,
priced with the two access times a registry entry can have -- every `ret_op` and
`yield_op` in the registry is in a ROM bank at $8000 or above, and the stack is
always in low WRAM.

On `level1.zmv` that is **2,881,698 cycles across 67,157 substituted calls**,
and it moves the native work share from 57.0% down to 56.6%. Down, because the
old number was crediting the port with cycles the 65816 had actually spent.

### What a mean is worth, measured in cycles

`verify` already reported whether a cost model was *right*: the error against the
ROM, and whether it was a whole number of refreshes. What it never reported was
what being slightly wrong is *worth*. Those are different questions -- a model
can be wrong by six cycles and be called a million times, or wrong by six
thousand and be called ten -- and only the second one is denominated in the
thing `run` actually loses.

So `verify` now prints a **budget drift** table: for every routine, the signed
total a substituted run would have paid for the calls this movie made, less what
the ROM spent on them. Positive is over-payment -- a native core arriving at the
same point in the game later than a stock one. Refresh comes off a reported cost
first, because the burn spends its budget in twelve-cycle pieces and the core
puts the refreshes back, so an exact model scores exactly zero rather than 40 per
scanline it happened to cross.

It ranks by size of debt rather than by size of error, which is the point: six
cycles a call is not a wrong model, and on a routine called a million times it is
six million cycles. On `level25-boss.zmv` the whole registry comes to **+38,892
cycles over the movie, 0.11 of a frame** -- and that total is flattered by two
errors cancelling, which is exactly the sort of thing a table shows and a summary
statistic does not:

```
  routine                      cycles      calls     per call
  apu_send                  -56084434      23093      -2428.6   (verify only: never paid)
  thread_tick_waits           +605704       1016       +596.2
  wave_hdma_build             -572436         12     -47703.0
```

`apu_send` is the largest number in the table and is not anybody's problem: it is
`verify_only`, so its budget is never spent. It gets a row because leaving it out
would hide the size of what the port computes and never stands in for, and a tag
because a reader who saw the number without one would go and fix the wrong thing.
The three `run_only` routines are missing from the table and cannot be added --
`verify` never checks them, so there is no `actual` to subtract -- which the
footer counts and says rather than leaving to be noticed.

### The hole in the frame where HDMA should have been

Chasing the drift turned up something the burn had been getting wrong since it
was written. Every access the 65816 makes goes through `snes_cpuRead`,
`snes_cpuWrite` or `snes_cpuIdle`, and all three call `dma_handleDma` first --
which is the **only** place HDMA is ever performed. A burn called `snes_runCycles`
directly, so for its whole duration HDMA did not happen. `hdmaRunRequested` is a
single bool set once per scanline, so a substituted routine spanning 200 lines
suppressed 199 of them; `lzss_decompress` spans four to seventy-one *frames*.

The burn now advances the machine the way a busy CPU advances it. On
`level49-corner.zmv`, which is one of the three movies that runs HDMA at all,
that is 2,640 cycles the frame gets back. On `level1.zmv` it is exactly nothing,
because no channel is armed while anything long is substituted -- which is why
this was invisible rather than why it was harmless.

### What the parting is made of

`run` stops comparing when the two cores stop agreeing about what frame it is, and
this file has said for several sections that what would buy that window back is
either better cost models or a harness that can resynchronise. The drift readout
now prints how far apart the clocks were when it happened, and on `level1.zmv`
that is **+395,322 cycles, 1.11 of a frame**. It also prints where it came from,
and 394,676 of it arrives in **one pass** -- the pass that parts. The 317 passes
before it accumulated 646 cycles between them.

Substituting one routine at a time says which. `-r none` is identical at all 389
passes, as it must be. `-r lzss_decompress` alone reproduces **390,222** of the
395,322 and parts at the same pass. So the whole of it is one routine -- and that
routine's five calls, timed on both sides from the entry PC to the caller's
return address, went:

| call | stock | native | out by |
| --- | --- | --- | --- |
| 1 | 1,692,786 | 1,692,780 | -6 |
| 2 | 6,476,668 | 6,476,638 | -30 |
| 3 | 10,032,038 | 10,031,990 | -48 |
| 4 | 13,661,174 | 13,661,234 | +60 |
| 5 | 26,981,744 | 26,981,846 | +102 |

**Six cycles out on the first and a hundred and two on the last, on windows of up
to twenty-seven million.** One part in 209,000 at its worst, which is the third
call rather than the largest one -- the error does not grow with the window,
because it is not the model's. And that was
enough: a scheduler pass sitting on the vblank boundary went the other way, the
pass had to wait for the next NMI, and the frame it waited is the entire drift.

So the answer is that better cost models will not buy the window back, and that
is worth knowing precisely because improving the models was the plan. The models
are already two orders of magnitude finer than the thing that decides it. What is
left is a coin landing, and there is no accuracy at which a coin stops landing.

Nor does the other half work. Comparing past the parting anyway -- which the
harness refuses to do, so this was a one-off -- gives 162 bytes at the parting
against 16 before it, rising to 255 later. Not thousands, and recognisably a
frame rather than a fault: `$16` off by one and the counters downstream of it off
by one with it. But it is a real divergence and not an artefact. An extra NMI is
an extra `thread_tick_waits`, so a sleeping thread wakes a tick early and the
game genuinely goes on differing from there. Aligning on `$16` instead costs the
lagging side an extra scheduler pass and puts the thread clock out by one in
place of the frame clock. There is no filter that keeps the dropped frame out and
lets a wrong answer through, and that is what resynchronisation would need to be.

**What `run` measures is a window, and the window is set by how long two
timelines can stay on the same side of a vblank deadline.** That is a property of
the game filling most of a frame, not of the port. It is worth stating plainly
rather than carrying another round as an open problem with a plan attached to it.

### Results

The corpus is unchanged and has to be: `verify` never burns a budget, so nothing
in this round can touch it. **13,209,637 calls across 43 movies, 0 diverged,
branch coverage 432 of 538.**

Every lockstep window is unchanged too, and every one of them ends the same way
-- which is the honest result rather than a disappointing one, and the section
above is why:

| movie | passes compared | drift at the parting |
| --- | --- | --- |
| `level1` | 317 | +395,322 (1.11 frames) |
| `level1-rescue` | 317 | +395,322 (1.11) |
| `level45-race` | 1,045 | +405,924 (1.14) |
| `level49-corner` | 1,141 | +397,376 (1.11) |
| `level53` | 1,173 | +398,118 (1.11) |
| `level25-2p` | 1,023 | +427,802 (1.20) |
| `boot` | 2,390 | never parted |

Six movies, six different levels, six different lengths of window, and all six
part within a tenth of a frame of the same number. That is not six cost models
each happening to be wrong by the same amount; it is one frame plus the phase
left over, six times, because the frame is what a dropped pass costs and the
phase is all the models were ever out by. Live state is clean on every one: no
byte outside the stacks and the declared scratch ever differed.

What did move is the accounting. `level1.zmv`'s native work share is 56.6% where
it was 57.0%, and the 0.4 points is the return instruction the port used to be
paid for twice. `level49-corner.zmv`'s work rises by 2,640 instead of falling,
because HDMA now runs during its burns and those cycles are the game's.

### Next

The drift table picks the next target and it is not the one the work ranking
would have picked. **`$80:9570 wave_hdma_build` is out by more per call than
anything else in the registry**, by two orders of magnitude: -47,703 cycles a
call, twelve calls a level movie, 572,436 cycles. Only `thread_tick_waits`
outruns it on a total, and only by being called a thousand times as often -- at
+596 a call on `level25-boss` and +147 on `level1`, which is a routine whose cost
is the length of a list, not a routine with the wrong number written down.

`wave_hdma_build`'s declared mean is 115,606 and it is not wrong -- it is the
call-weighted mean of two populations that do not overlap. Every level movie
measures exactly twelve calls at 163,164..163,514, the full-length table built
during the level-entry transition, while `boot.zmv` makes 1,077 at a mean of
96,406 as the title sequence retracts the wobble two bytes at a time. A single
number cannot be right about both, and the spread inside each population is 350
cycles, so a counted model would be nearly exact. It is the clearest case in the
registry for the `_counted` pattern, and the only reason it was not obvious
before is that nothing measured what the mean was costing.

## The mean that described neither half (2026-08-11)

The drift table built last round named its own next target, and this is it.
`$80:9570 wave_hdma_build` now prices itself per call, out of counts. What the
old number was costing is worth stating before anything else: on `boot.zmv` it
was **20.7 million cycles, thirty times every other routine a substituted run
pays for put together**, and on every level movie it was wrong by about the same
amount in the opposite direction.

### Two populations and one number

The registry entry said `.cycles = 115606`. Nothing ever cost that. Every level
movie makes exactly twelve calls, all of them within 350 cycles of 163,300,
because the level-entry transition builds the table at its full `$01C0` bytes
and then leaves. `boot.zmv` makes 1,077 at a mean of 96,406 and a floor of
1,176, because the title screen holds the same wobble for as long as nobody
presses Start while the retraction takes the table apart two bytes at a time.
115,606 is the call-weighted mean of the two and it sits in the gap between them.

What that was worth, in the drift table's own terms:

| movie | calls | budget drift, before | after |
| --- | --- | --- | --- |
| any level movie | 12 | **-572,436** | **-292** |
| `boot.zmv` | 1,077 | **+20,678,400** | **-20,590** |

Both directions at once, out of one wrong number, which is what a mean between
two populations does. On `boot.zmv` the port was being paid 19,200 cycles a call
for work it had not done -- 6.9% of that movie's entire 300M-cycle work
denominator, against a `--- substituted` total of about a million for everything
else in the table. On every level movie it was under-paying by 47,703 a call
instead. The new figures are a thousandth of the old and the sign no longer
depends on the input.

### What the count is

Sixteen blocks, and the routine makes them easy: almost the whole cost is the
loop, the loop runs once per two bytes of table, and everything inside it is a
branch whose outcome the port already computes. Five of the sixteen number the
iterations independently -- the head with and without its arc wrap, the three
arms of `sin_deg`, the seam test, the loop-back test, and the per-iteration
constant -- so a mistake in any one of them disagrees with four others.

**Three of the blocks price a routine that has a registry entry of its own**, and
that is not a duplicate. `$80:9C90 sin_deg` is called 223 times from inside this
loop, and when `wave_hdma_build` is substituted its body never runs -- so the
`JSL` never happens and the shim that would have priced those 223 calls is never
entered. `sin_deg`'s own entry is what `verify` checks it with; these three
blocks are what `run` pays for it. The cost is 82 cycles for the
`$80:9C8C JSR : RTL` trampoline, 118 for the shared prologue down to the sentinel
compare, one of three arms, 74 for `PLX : RTS`, and then the caller's own
`BIT #$8000 : BEQ : ORA #$FF00` -- which is decided by the same bit as the arm,
so the three arms of the callee and the two of the caller are one count and not
two. The arms differ by 36 cycles and the negative one is taken over half the
circle, which is about 2,700 cycles on a full-length call: forty times the spread
the twelve level-movie calls measure between them, and so not roundable away.

The two entries agreeing is the check, and they agree three times.

* `sin_deg`'s row measures a **floor of 240 cycles**. 118 + 48 + 74 -- prologue,
  sentinel arm, `PLX : RTS` -- is 240 exactly, arrived at from the listing rather
  than fitted to the measurement.
* Its call count on a level movie is **2,676, which is 12 x 223**: twelve calls
  to `wave_hdma_build`, 223 iterations of a full-length table each. That number
  has been in the registry since `sin_deg` was ported, identical on five
  different movies, and nothing had ever had a reason to factorise it.
* On `boot.zmv` it is **140,938** over 1,077 calls, a mean of 131 iterations,
  which is the retraction taking the table down two bytes at a time exactly as
  described. `WAVE_BLK_ITER` is a second, independent count of the same
  quantity.

### It is exact, and the residual says so

`verify` cannot certify this one refresh-exact, and the reason is the routine's
own name. It builds an HDMA table and its caller arms channel 6 before entering
the loop (`$80:9504  LDA #$40 : STA $420C`), so **every call it ever makes is
made under HDMA** -- and HDMA steals cycles per scanline that no static model can
know. Calls under HDMA are held only to direction: the model must never claim
more than the ROM took.

It never does, and the shape of the residual is what says the model is right
rather than merely low:

| movie | calls | error against the ROM |
| --- | --- | --- |
| `level1` | 12 | +9,250..+9,624, mean +9,374 |
| `level45-race` | 384 | +9,074..+9,664, mean +9,323 |
| `boot` | 1,077 | **+40**..+9,632, mean +5,371 |

The floor is the whole argument. Over `boot.zmv`'s 1,077 calls, which run the
loop anywhere from once to 223 times, the smallest error the model makes is
**exactly 40** -- one DRAM refresh, and nothing else. So on at least one of them
the model is right to the cycle. At the other end the error is 9,600 on a call of
163,000: about 120 scanlines of refresh at 40 and 120 scanlines of one HDMA
channel at about 38. Both of those grow with the call, which is why the error
does and the model does not.

Under `run` neither is owed by the budget. `burn_slice` advances the machine
through `dma_handleDma` and `snes_runCycles`, so the core inserts its own
refreshes and runs its own HDMA during a burn, exactly as it does under the CPU.
The budget is meant to be the quiet cost, and the quiet cost is what this reports.

### Results

**13,209,637 calls across 43 movies, 0 diverged, branch coverage 432 of 538** --
identical to the previous corpus line for line, which is required: `verify` runs
the ROM and checks the port against it, so a cost model cannot move it. The whole
diff against the last run is one new row in the cost-model table:

```
  wave_hdma_build                   0 / 0              1581 HDMA
```

1,581 priced calls over the corpus, none of them refresh-exact and all of them
under HDMA -- which is not a failure but the one thing this routine can never be,
for the reason above.

Every lockstep window is unchanged -- 317, 317, 1,045, 1,141, 1,173, 1,023 and
`boot`'s full 2,390 -- and on the level movies it could not have been anything
else: the wobble runs during the level-entry transition around frames 900-1,200,
and every level movie parts before frame 210. The drifts at the parting are
+395,332 (`level1` and `level1-rescue`), +405,924 (`level45-race`), +397,376
(`level49-corner`), +398,118 (`level53`) and +427,802 (`level25-2p`) -- the same
six numbers as last round to within ten cycles, and for last round's reason,
which is that they are a dropped frame and not a cost model.

`boot.zmv` is the one movie where this round's work falls inside the compared
window, all 1,077 calls of it, and it still runs all 2,390 passes without
parting. Live state is clean on every movie: no byte outside the stacks ever
differed.

Boot is also where the accounting moves, and it is worth giving both numbers,
measured by leaving the model in place and only suppressing the report:

|  | work numerator | denominator | share |
| --- | --- | --- | --- |
| against the mean | 200,916,426 | 321,176,122 | 62.6% |
| against the count | 180,539,666 | 300,723,564 | **60.0%** |
| `-r none` control | 0 | 298,930,120 | -- |

The denominator moved too, and that is the part worth looking at. Substituting
this one routine against a mean made the whole session **22,246,002 cycles --
7.4% -- longer than the game actually is**. It is now 1,793,444, or 0.60%, and
almost none of that is this routine.

### The total got bigger, and that is the result

On `level25-boss.zmv` the drift table's `--- substituted` line now reads
**+611,036** where it read **+38,892**. The difference is exactly the 572,144
`wave_hdma_build` stopped owing, and the total went *up* because what it had been
owing was negative.

Last round's section called the +38,892 "flattered by two errors cancelling" and
left it there. It is worth being blunter now. `thread_tick_waits` was over-paying
by 605,704 cycles and `wave_hdma_build` was under-paying by 572,436, and the two
happen in different minutes of the same movie -- one of them 1,016 times spread
across the whole of it, the other twelve times inside a transition three hundred
frames long. **They could not cancel in time, only in a column.** A signed total
over a registry is the one number in that table with no operational meaning, and
what this round did to it was remove a coincidence.

### What the work numerator actually is

Measuring the above turned up a stale claim in `cosim_report`. It said the work
numerator is "the measured *mean* cost of each routine", and that the row reads
high by about 16% -- both true when written and neither true now. Sixteen of the
registry's routines price themselves per call, including most of the expensive
ones, so a mean is the fallback and not the rule.

The check is a control run: `-r none` substitutes nothing, so its work
denominator is what the game costs undisturbed. `boot.zmv` is the honest movie to
use, because it never parts and both sides therefore play the same game for all
2,390 passes:

```
  boot.zmv       -r none   298,930,120      substituted   300,723,564
  level1.zmv     -r none   461,037,142      substituted   461,307,102
```

**0.99% on `boot`, 0.10% on `level1`** -- 98 cycles on each of boot's 18,285
substituted calls, where the paragraph claimed 16%. The report says so now.

### Next, found by reading the table off the end of a movie

Every drift table above was read at `verify`'s default of 2,400 PPU frames.
Reading `level25-boss.zmv` at its full 7,600 -- far enough in to reach the boss
-- produces a different table and a much larger number:

```
  routine                      cycles      calls     per call
  sprite_build_oam          -41732304       6042      -6907.0
  boss_stomp                +13759236       4991      +2756.8
  actor_overlap_pass        -10901026       6042      -1804.2
```

`sprite_build_oam` prices **5,014 of its 6,042 calls** and falls back to its
declared 43,111 on the other 1,028 -- and those are not a random 17%. A call
declines when its collisions land on a handler with no cost table, which is what
a boss fight is made of, so the calls that decline are the crowded ones and the
fallback is worst exactly where it is used. That movie's measured ceiling is
137,160 cycles, against a registry comment that still said 66,412: no input had
ever put that much on the board.

So the largest debt in the harness is now a **guard to widen rather than a model
to write** -- the model exists and is exact on the calls it accepts; it is the
pricing that gives up. Second to it is `$82:92D6 boss_stomp` at +2,757 a call
over 4,991, which is a mean where a count should be, and the same again for
`actor_overlap_pass`. None of the three was visible until the table was read past
where the default stops, which is a thing worth remembering about the default.

## Two handlers, one of them counted twice (2026-08-11)

Last round's section ended by naming three targets off the end of a movie, and
this round took the first two. Both turned out to be the same kind of job and
neither was the job the table appeared to describe.

### The largest debt was not a model to write

`sprite_build_oam` owed **-41,732,304 cycles over 6,042 calls** on
`level25-boss.zmv` at its full 7,600 frames, which was more than everything else
in the table put together. But its model is exact on the calls it accepts. What
it did was *decline* 1,028 of the 6,042 and fall back to a declared mean of
43,111 on those -- and, as recorded last round, the calls that decline are the
crowded ones, so the fallback is worst exactly where it is used.

So the question was never "what does this routine cost". It was "which handler
is it giving up on", and that is a question a `printf` answers. A temporary
census, keyed on the entry address `thread_call_cycles` refused, counted once
per declining call rather than once per dispatch:

```
  81C440  calls    984
  81EDAA  calls     28
  80CAEE  calls     15
  000000  calls      1
```

**984 of 1,028 is one address.** The four rows sum to 1,028 exactly, which is
the arithmetic check that the census counts what the drift table counts.

### One table for two copies of one routine

`$81:C440` is the giant spider one stage before `$81:C4A6 monster_collide`, and
the port has shared a body between them since the level-45 round -- three bytes
differ in the ROM. Two of the three are `JML` targets on paths that decline
anyway, and the third is the order of the `CMP #$005D`/`CMP #$005E` pair, which
cannot cost anything different because both comparisons run and neither branch
is taken on any path that reaches them.

So there is one `MonsterCollideBlock` table and it prices both entries. Writing
it twice would have been two things that can drift, which is the same argument
that made the port share the body in the first place.

Six blocks: the two ignore exits, the latch refusal, the theft and its
alternate, and the zero-damage exit. The four deep paths -- id `$5D`, id `$5E`,
a death, a survival -- decline, exactly as `enemy_cycles` declines its own.

**And the zero-damage block agrees with a routine eleven kilobytes away.**
`$81:C4A6` is `$81:8888` on a different page, and its no-damage exit prices to
**384 cycles over 43 bytes with 3 direct-page instructions** -- which is
`ENEMY_BLK_NO_DAMAGE`, digit for digit. The two were transcribed from separate
listings rounds apart and had never been put next to each other. Neither has
ever been measured and neither ever will be by these movies: no shot in the game
carries a damage-table entry of zero, so `enemy_no_damage` and
`monster_no_damage` are both untaken on every input. Two independent
transcriptions agreeing is the only check available for a branch nothing
reaches, and it is a real one.

### What it was worth

| | before | after |
| --- | --- | --- |
| `sprite_build_oam` budget drift | **-41,732,304** | **-4,448,875** |
| ...per call, over 6,042 | -6,907 | -736 |
| declining calls | 1,028 | **105** |

`verify` measures `monster_c440`'s cheapest call at **exactly 120 cycles**, which
is `MON_BLK_IGNORE_LOW` -- `CMP : BCS : CMP : BCC` into the shared `CLC : RTL` --
arrived at from the listing and not fitted to anything. Over the 1,311 priced
calls the error is **+0..+40, mean +4**, and all 1,311 are refresh-exact with no
HDMA anywhere near them. A model whose minimum error is zero is not approximately
right.

The remaining 105 declines are a different composition from the original 1,028,
and that is worth noticing: `$83:A364 victim_collide` now appears with 26 where
it did not appear at all before. It had been there the whole time, hidden --
a declining call records only the *first* handler that refuses, so the biggest
one masks everything behind it. Removing `$81:C440` did not only remove
`$81:C440`; it revealed what it had been standing in front of.

### The blast radius, and the boss that is only its caller

The table's next two rows were `boss_stomp` at **+13,759,236 over 4,991 calls**
and `actor_notify_box` at **+3,391,533 over 5,245**. They are one job. `$82:92D6
boss_stomp` is five stores and a `JSL` into `$80:BF1B`, so it has no control flow
of its own at all: 376 cycles of arithmetic, 54 for the `JSL`, 40 for the `RTS`,
and everything that varies call to call varies inside the callee.

This makes `boss_stomp` the second registry entry priced by a table it does not
own -- the first being the three blocks of `wave_hdma_build` that pay for
`sin_deg` -- and it is not a duplicate for the same reason: when `$82:92D6` is
substituted its `JSL` never happens, so `actor_notify_box`'s shim is never
entered on that call.

`$80:BF1B` itself is one screen of listing, 76 bytes and 14 direct-page
instructions, and seventeen blocks describe it exactly. Two things about the
shape are worth recording.

**The bound loop is four blocks, not a constant.** `LDX #$0006 : BIT $38,X :
BPL : STZ $38,X : DEX DEX : BPL` always runs exactly four times, so its cost
could have been folded into the prologue. Counting the clamp, the keep and both
outcomes of the loop-back separately makes the fixed part self-checking:
`BOUND_NEXT` must come out at three times `BOUND_DONE` on every call, and the
two bound blocks must sum to four times it. A fixed loop is the cheapest place
in a model to hide an arithmetic error, and this is what stops it being free.

**The seven per-record blocks are nested prefixes, and their spacings are the
check.** They climb 92, 132, 206, 246, 320, 360, 572, and the differences are
**40, 74, 40, 74, 40** and then the dispatch preamble's 212. The alternation is
not decoration: a test that reuses what is already in A costs `CMP` plus the
branch's six, which is 40, and one that has to fetch a fresh word out of the
record first costs `LDA $xx,X` on top, which is 74. So the id test and the two
*upper* bounds are 40, and the two *lower* bounds -- which open each axis -- are
74. Any table where those five numbers are not in that order has an instruction
in the wrong block.

### Two floors that were already written down

Both halves of this model reproduce a number the harness had recorded before the
model existed, and in neither case was one fitted to the other.

`actor_notify_box`'s registry entry has said `642..11,272` for rounds. The model
says the cheapest possible call is all four bounds kept and then the one-record
refusal at `$80:BF33`:

```
  114 + 4x76 + 3x18 + 12 + 158  =  642
```

That also corrects the entry's own description of its floor. It said 642 was "a
box that found nothing to tell", which is loose in a way that matters: **the
empty-list exit two instructions earlier costs 606, and 606 has never been
measured**, because no call in the corpus has ever found `$9C` at zero.
`notify_no_actors` is untaken on every movie. The floor is not the cheapest exit
in the routine; it is the cheapest exit the game has ever taken.

And `boss_stomp`'s entry has said `1,112..13,054` over 6,549 calls. Its own 470
cycles plus the 642 above is **1,112** exactly -- one number from the corpus
reporting the cheapest call it ever saw, the other from two listings added up.
The cheapest stomp in the game is a boss standing on a board that holds one
visible actor, and it costs 1,112 cycles with no DRAM refresh in it at all.

### Results

**13,209,637 calls across 43 movies, 0 diverged, branch coverage 432 of 538** --
identical to the previous corpus, which is required: `verify` runs the ROM and
checks the port against it, so a cost model cannot move it. What did move is the
*priced* column, in six places at once:

```
  actor_collide_notify   22453 -> 28840
  actor_overlap_pass    131994 -> 137432
  sprite_build_oam      131994 -> 137432
  thread_call_handler   103161 -> 109865
  monster_c440               -> 4152      (new)
  monster_collide            -> 2552      (new)
```

Both new models are refresh-exact on every call they accept, which is the
strongest verdict the harness gives:

| routine | priced | error | refresh-exact |
| --- | --- | --- | --- |
| `monster_c440` | 1,311 of 1,343 | +0..+40, mean **+4** | 1,311 / 1,311 |
| `actor_notify_box` | 5,138 of 5,245 | +0..+360, mean +141 | 5,138 / 5,138 |
| `boss_stomp` | 4,884 of 4,991 | +40..+360, mean +158 | 4,884 / 4,884 |

Every lockstep window is unchanged -- 317 (`level1`), 1,045 (`level45-race`),
1,141 (`level49-corner`), 1,173 (`level53`), 1,023 (`level25-2p`) and `boot`'s
full 2,390, which still never parts. Live state is clean on every movie: no byte
outside the stacks differed on any compared pass. Native work share reads 56.7%
on `level1` and 57.6% on `level25-boss`. Both unit tests exit 0, `verify-lzss` is
3 of 3 byte-identical, and `zamn_headless` renders the giant spider -- which is
`$81:C440`'s own creature -- correctly at frame 3,000 of `level25-boss.zmv`.

### The total moved twice, in opposite directions

On `level25-boss.zmv` at 7,600 frames the `--- substituted` line read
**-5,125,320** after the monster round and **-22,900,382** after the notify-box
round. It got worse, and for the reason last round set out at length: the signed
total over a registry is the one number in that table with no operational
meaning. `boss_stomp` was over-paying by 13.8M and `sprite_build_oam` was
under-paying by 41.7M, and removing the positive one leaves the column looking
worse while the machine is strictly closer to the ROM on every call either
routine makes.

The number that means something is the per-routine one, and by that measure the
three largest debts in the harness at the start of this round are now the
sixth, and gone, and gone.

### Next

The same table, same movie, same 7,600 frames:

```
  routine                      cycles      calls     per call
  actor_nearest              -6422607      11721       -548.0
  boss_step                  +5428166      11593       +468.2
  actor_at_point             -5159541       5985       -862.1
```

All three are means where counts should be, and none of them is a guard problem
-- which makes them the ordinary version of this work rather than the
interesting version. `actor_nearest` and `actor_at_point` are both walks over
`visible_actors` with per-record branches, so they are `actor_notify_box` again
without the dispatch, and the model above is most of the shape they need.

## Two searches, and the branch that gave one of them away (2026-08-11)

Last round ended by naming three rows off the end of `level25-boss.zmv`, and
this round took the first and the third. Each of them turned out to have a
sibling sitting next to it in the same bank, so four rows came out of the drift
table for the work of two models:

```
  actor_nearest              -6422607      11721       -548.0
  actor_at_point             -5159541       5985       -862.1
  actor_obstacle_at_point    -1606186       5674       -283.1
  actor_nearest_id3                 -          -            -   (too rare to rank)
```

Neither model can decline. A fixed walk, no dispatch, no leaf call and a block
for every branch means there is no such thing as a call these cannot price,
which is the first time that has been true of anything in this file since
`oam_buffer_clear`.

### One table for two routines, and this time the ROM means it

`$80:B123 actor_nearest` and `$80:B18F actor_nearest_id3` are the same routine
written twice: the same prologue, the same two flag gates, the same `|dx| +
|dy|` into the same seven scratch words, the same strictly-nearer test, the same
loop tail, the same epilogue. What differs is four `CMP : BEQ` pairs where the
other has one `CMP #$0003 : BNE`.

Last round's monster pair shared one table because the three bytes that differed
were provably cost-neutral. This is the other case: the difference is real, it
costs a different number of cycles, and the table holds it as **two extra blocks
that only one of the two routines ever touches**. Twelve blocks shared, two each
side, and a reader can see at a glance which is which.

**The four id blocks climb 152, 182, 212, 236 — spacings 30, 30, and then 24.**
The 24 is not a slip. A test that fails costs `CMP` plus an untaken branch = 30
either way, but the first three *succeed* on a taken `BEQ` (36) while the fourth
succeeds by falling through its `BNE` (30). The same asymmetry read from the
other side is why `NEAR_BLK_WRONG_ID` is exactly six more than
`NEAR_BLK_ID_D`: one branch, taken instead of not. Two numbers that have to
agree, arrived at from opposite ends of the same instruction.

And because the walk is a fixed 32 slots whatever the board holds, nearly the
whole table checks itself on every call:

```
  UNDRAWN + INACTIVE + WRONG_ID + (the id blocks)  ==  32
  (the id blocks)  ==  DX_POS + DX_NEG  ==  DY_POS + DY_NEG  ==  KEPT + CLOSER
  LOOP_NEXT == 31,   LOOP_DONE == 1,   FIXED == 1
```

Splitting a loop that always runs the same number of times is the point and not
an oversight, for the reason `actor_notify_box`'s bound loop was split last
round: a fixed count folded into the prologue is the cheapest place in a model
to hide an arithmetic error.

### The band that leaves two ways

`$80:BF67 actor_at_point` and `$80:BFC8 actor_obstacle_at_point` walk the same
visible list backwards and ask different questions about it. Two things about
the shape are worth recording, and both are places where the port's coverage
table and the ROM's control flow do not line up one to one.

**The id band leaves on two different instructions.** Both routines run
`CMP #$0033 : BEQ` and then `BCC` under it, so the id at the *top* of the band
goes out twelve cycles earlier than every id below it. The port has one branch
there and the coverage table calls it `at_point_id_band`; the model has to have
two, and `$33` is the cheapest way out of the range that contains it.

**The chain is not a prefix walk.** `CMP #$000C : BCC $BF96` sends the *low* ids
forward to the named comparisons, and then the band test drops the *high* ones
into exactly the same place. So the two arrivals are their own blocks and the
named tests are counted one at a time on top of whichever arrived — additive
rather than nested. In `actor_at_point` that saves two blocks; in
`actor_obstacle_at_point`, which has seven named comparisons, it is the
difference between thirteen blocks and twenty-eight.

The two tables are separate, unlike the pair above, and the reason is the same
reason stated the other way round: there the ROM holds one routine twice, here
it holds two routines written from one sketch. But everything from the window
tests down really is byte for byte the same — `$80:C021`-`$80:C03E` is
`$80:BFA0`-`$80:BFBD` — so **the last seven entries of the two tables must be
equal**, and they were counted off their own listings rather than copied. Two
tables disagreeing there would be a transcription error in one of them and
nothing else.

### 132 is not a multiple of 40

The first run of the obstacle model came back like this:

```
  actor_obstacle_at_point   5674   5674   +0..+276, mean +127   2869/5674   <-- MODEL WRONG
                            first wrong: model 3450, ROM 3582, out by 132
```

**That the error is 132 is the whole diagnosis.** A model that is merely
missing DRAM refreshes is short by a multiple of 40 and nothing else, so the
refresh-exact column is the one check in the harness that can tell "the error is
the bus" from "the error is an instruction". 132 is 120 and 12, and 12 is an
untaken branch. The worst call in the run was out by 276, which is 240 and
three of them.

The missing instruction was `$80:BFF7  BCC $C042`, not taken, in the two blocks
that walk past it: the `$5C` ceiling exit and the `$34`..`$5B` arrival. Both had
the band's `BEQ` above it and the ceiling's `CMP`/`BCS` below it and neither had
the branch in between. `actor_at_point` next door was exact on its first run
because its chain stops one instruction before that one exists.

It is worth being precise about what caught this, because it was not the thing
that usually catches things. The port's *answers* were right the whole time —
5,674 of 5,674 calls agreed with the ROM on every byte of WRAM and every
register, and a `verify` that only checked behaviour would have passed. What
failed was the claim about how long the behaviour took.

### A floor that was already written down, and three that never will be

`actor_obstacle_at_point`'s registry entry has said `426..7,794` for rounds. The
model's cheapest possible call is the prologue, one record in the visible list
that turns out to be player A, and the loop running out:

```
  216 + 86 + 124  =  426
```

Not close to it — it. And this is a stronger version of the same check than last
round's, because 426 is the model's *absolute minimum* over calls that enter the
walk at all, so there was nothing to choose: `verify` on `level1.zmv` reports
exactly 426 for the cheapest of its 311 calls, and the model cannot produce a
smaller number to be fitted to it.

**The cheaper exits below it have still never been measured, and that is now
three of them.** `obstacle_empty` costs 286 and `at_point_empty` costs 314 — the
count at `$9C` found zero, no walk at all, straight to `PLD : CLC : RTL` — and
neither is taken by any input in the corpus. Last round found the same about
`notify_no_actors` at 606. Three routines that walk the visible list, three
empty-list exits, and in 13.2 million calls the game has never handed any of
them an empty board. That is a fact about the game rather than about the port:
something is always on screen.

There is a fourth block in the same position, and it is the one that makes the
duplication between the two tables worth having. `at_point_id_named` — id `$07`
or `$08`, the two exceptions in that routine's named chain — is untaken by every
movie, so `AT_BLK_NAME_HIT`'s 36 cycles have never been measured. The
identically-derived `OBST_BLK_NAME_HIT` **is** measured, thousands of times,
because that routine's chain has seven ids in it and the board keeps handing it
one. Two tables, written separately from two listings, and the block one of them
cannot check is the block the other one checks constantly.

### Results

All four models are refresh-exact on every call, and every call is priced:

| routine | priced | error | refresh-exact |
| --- | --- | --- | --- |
| `actor_nearest` | 11,721 of 11,721 | +160..+280, mean +228 | 11,721 / 11,721 |
| `actor_nearest_id3` | 24 of 24 | +200..+240, mean +233 | 24 / 24 |
| `actor_at_point` | 5,985 of 5,985 | +0..+200, mean **+103** | 5,985 / 5,985 |
| `actor_obstacle_at_point` | 5,674 of 5,674 | +0..+240, mean +115 | 5,674 / 5,674 |

`actor_nearest`'s floor of **+160** is the only one of the four with no zero in
it, and the reason is structural rather than a defect: it is the only one of the
four that cannot exit early. Thirty-two slots at 6,120 cycles minimum is four
and a half scanlines, and a call that crosses four scanlines crosses four
refreshes whatever else it does. The three that can stop at the first record
they like all reach 0.

Over the whole corpus the four together are **387,577 priced calls, every one
of them refresh-exact**:

```
  actor_at_point               117544 / 117544          449 HDMA
  actor_nearest                134983 / 134983          447 HDMA
  actor_nearest_id3               338 / 338               0 HDMA
  actor_obstacle_at_point      134712 / 134712            0 HDMA
```

The corpus is otherwise **13,209,637 calls across 43 movies, 0 diverged, branch
coverage 432 of 538**, identical to the last one line for line. The entire diff
against it is six new rows in the cost-model table: these four, and
`actor_notify_box` and `boss_stomp` from last round, which the previous corpus
predates.

**Every lockstep window moved, and every one of them moved by exactly eleven
frames:**

```
  level1           317 -> 328
  level45-race    1045 -> 1056
  level49-corner  1141 -> 1152
  level53         1173 -> 1184
  level25-2p      1023 -> 1034
  boot                 never parts, still
```

Eleven on all five is worth staring at rather than celebrating. Those are five
different levels, two of them two-player, parting at five different points for
five different reasons — and four models that only make the substituted timeline
more accurate moved all of them by the same amount. The reading that fits is
that what finally parts a run is dominated by one accumulating source none of
this touched, and that the four models bought the same fixed head start against
it everywhere. Which is a lead worth following later: a per-frame drift that
uniform has one cause, and it is not in the registry.

Live state is clean everywhere. On `level1` the first difference is at pass 8
and all eleven bytes of it are inside `$7E:1100`-`$7E:12FF`, which is stack.
Native work share reads **57.0%** on `level1` and **57.6%** on `level25-boss`.
Both unit tests exit 0, `verify-lzss` is 2 of 2 byte-identical and both also
check as a memory effect, and `zamn_headless` still renders the giant spider
correctly at frame 3,000 of `level25-boss.zmv`.

### The total moved the right way, and that is luck about signs

On `level25-boss.zmv` at 7,600 frames the `--- substituted` line went from
**-22,900,382** to **-9,686,354**, and it is worth saying plainly that this is
not evidence of anything. All four rows removed this round happened to be
negative, so taking them out shrank the column; last round removed a positive
one and the same column got worse while the machine got strictly better. The
signed total over a registry is the one number in that table that means nothing,
in either direction.

The per-routine numbers are the ones that moved: three of the four largest
debts in the harness at the start of this round, and the ninth, are gone.

### Next

`boss_step` is what is left of last round's three, and it is now the largest
thing in the table by a factor of two:

```
  routine                      cycles      calls     per call
  boss_step                  +5428166      11593       +468.2
  camera_follow              -3151348      20608       -152.9
  monster_seek               -2606030       6817       -382.3
```

It is also the first of these in a while that is not a walk. `camera_follow` and
`monster_seek` under it are both single passes with a handful of branches, which
makes them the cheapest models left rather than the most interesting ones.

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
excuses. It excused `$7E:1000-$7E:12FF` when this was written, and level 53's
enemy threads park around `$7E:0F00` — outside it. So the twenty splices were
compared in both modes, by accident of where the scheduler put those threads,
and they agreed.

> **That accident is gone.** The stack area was later corrected to
> `$7E:0D00-$7E:12FF`, which is where the 24 stacks actually are, and `$0F00` is
> inside it — so `run` no longer checks these splices and this paragraph's happy
> accident does not repeat. That is a real loss and it is the right trade: the
> old boundary was excusing residue in eight stacks and reporting it in sixteen.
> `verify` checks the splices on every call either way.

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

**`d7f6_special` was the one that was work rather than closed, and it is taken.**
It wants `$5D`, object type `$02`, and four of the six levels place one — but
level 5's is sealed in a basement pocket at (667,1068), level 17's is reachable
next to a creature that is not, and level 31 has no password. **Level 29 has
both**, and the section below is the route. Five of the seven branches are diffed
now and the two that are not are the two argued to have no input at all, which
leaves nothing on this routine that an input could still reach.

### The one level that has both, and the 2,800 frames across it

`movies/level29-ice.zmv` takes `d7f6_special`, and what it cost is the whole
argument for `--reach` stated as a route. The branch wants `$5D` and a
`$81:D704` actor on the same level; four levels place a type `$02` object and six
place the behaviour, and the intersection that survives *reachability* is one:

| level | ice weapon | creature |
|---|---|---|
| 5 | sealed in a basement pocket at (667,1068) | open at (985,120) |
| 17 | open at (220,431) | sealed in an alcove at (563,513) |
| 31 | both | no password |
| **29** | **(756,730), on a blocked cell with standable ground beside it** | **open at (140,240)** |

The route is 2,800 frames and it was fitted in four goes:

```
2560 -> 3672   spawn (277,1435) to the object at (756,730)      fitter, ~35 legs
3730 -> 4325   the south loop out of the cul-de-sac to (773,461) fitter, 24 legs
4370 -> 4610   Left to the wall at x=721, then Down to y=591     by hand
4620 -> 5344   (721,591) to (203,241)                            fitter, 27 legs
```

**The hand-walked leg is the one worth keeping.** At (773,461) the way west is a
56-pixel detour south, and the fitter spent **41 legs** stepping 28 pixels down,
28 back up, and asking the same blocked question again — its unstick nudge was one
fixed length. Walking on to the wall at (721,591), 130 pixels away, cut the
remaining route from 277 cells to 147 and it fitted first time.
`tools/fit_route.py`'s `STUCK_NUDGE` is `(14, 28, 56)` now, shortest first, which
is that fix without the hand-walking; it reproduces the level-5 route byte for
byte, because a route that never gets stuck never reaches it.

Two smaller tolls, both measured rather than assumed. The object is **on a
blocked cell** — no route reaches (756,730) and every cell around it is reachable
— so the player stands at x=751 and the eight-pixel touch box does the rest;
`--watch 1CCE` says `$0000` → `$0099` at frame 3679. And **B is pressed at 5390**,
1,700 frames after the pickup rather than the 460 the lockout needs, because the
walk pays for it anyway: `$7E:1CBC` goes `$0000` → `$0001` at 5392 and stays.

The fight is a **retreat** rather than a stand — this creature chases, so the
player walks west with Y held and it follows. Every shot on screen reads `$5D`:
69 of them across 200 consecutive sampled frames, against no other weapon-range
id at all. `d7f6_special` counts 5, and `freeze_counting` 30, `freeze_already`
102 and `freeze_took` 6 come with it off a ninth caller.

**`d7f6_hit` 15 and `d7f6_died` 10 also count on this movie, and the second of
those is not explained.** `$5D` is diverted two comparisons before the damage
table, so a lethal hit needs some other id at or above `$5C` — and the display
list over the frames the hits land in holds only `$03`, `$36`, `$38`, `$00`,
`$01` and `$5D`. Whatever carried them has no display record, which the project
has seen before (`$82:F4EF` is reached seven times by a trigger that `--records`
never shows). Both sites were already taken by `movies/level5-d7f6.zmv`, so
nothing here rests on it; it is written down because it is unaccounted for.

### An address on an empty census

The corpus has had **no census section on any movie** for several rounds, and
this one puts `$81:E6E4` on it — one decline, which is also why
`collide_unported` and `handler_unported` are taken for the first time. It is a
**tenth copy of `$81:8888`**, and its two differences from
`enemy_d7f6_collide` are both at the ends:

```
$81:E6E4  LDY $08 : LDX $0004,Y : BNE $E72A     ; a guard no other copy has
$81:E6EB  CMP #$005C : BCC <CLC : RTL>
$81:E6F2  STA $22 : AND #$7FFF
$81:E6F7  CMP #$005E : BEQ $E722                ; -> JML $81:83C6, the bubble tail
$81:E6FC  CMP #$005D : BEQ $E726                ; -> JML $81:847E, enemy_freeze
$81:E701  SEC : SBC #$005C : ASL A : TAX
          SEC : LDA $0C : SBC $81:8561,X
          BMI $E71A : CMP $0C : BEQ $E72A
          STA $0C : JML $81:8506                ; enemy_survived_react
$81:E71A  DEC $0A : STA $0C : STZ $7E : SEC : RTL
```

The guard is the interesting half: `$0004,Y` is a word on the *display record*
rather than on the thread page, and a non-zero one makes this creature ignore
every collision it has, sharing the `CLC : RTL` at `$E72A` with the
no-damage exit. And `$5E` here is a `JML $81:83C6` — the bubble tail — where
`enemy_d7f6_collide` sends the same id straight into its death tail. Nine copies
of this subsystem and no two of them answer `$5E` the same way.

**It is not ported this round, on purpose.** One call cannot reach more than one
of eleven branches, and a routine written against one call is transcription with
a diff attached to a corner of it. What it needs is an input that stands in front
of this thing and shoots it, and the address plus the shape above is what makes
that cheap to aim.

### Corpus

**3,588,215 calls checked across 40 movies, 0 diverged; branch coverage 183 of
246, 63 untaken by every input** — from 3,429,877 across 38 and 177 of 246. The
two movies add 52,863 and 105,475, which is the whole difference to the digit.

Four sites move and each is attributed by intersecting a movie's own untaken list
with the corpus's, which is stronger than a count: `movies/level5-d7f6.zmv` has
`d7f6_hit`, `d7f6_died` and `d7f6_survived` to itself, and
`movies/level29-ice.zmv` has `d7f6_special` — plus `collide_unported` and
`handler_unported`, which are taken for the first time by anything because
**the census is no longer empty**: `$81:E6E4`, one decline, summed over the whole
corpus.

No port code changed this round. `run` substitutes all forty-six routines on both
movies and finds **no byte of live game state differing on any of 3,489 and 5,989
compared passes**, and `-r none` — two stock cores, the control that has to pass
before anything else here means anything — is **identical at all of them** on
both.

## The address the census named, and the input that came before the port

`$81:E6E4` went on the census with **one** decline and came off it ported, and
the order of the two halves is the whole point. One call names an address and
nothing else: it cannot reach more than one of eight branches, and a routine
written against it is transcription with a diff attached to a corner. So the
input came first, and `movies/level37-e6e4.zmv` reaches the same routine **367
times**.

### Finding the level was three ROM reads and no play

The address is installed once, and the installer says everything:

```
$81:E413  JSR from two behaviours — the actor init
$81:E455  LDA #$0003 : STA $0C : STA $0E        ; health, and health again
$81:E476  LDA #$E6E4 : LDY #$0081 : JSL $80:8475 ; install the handler
```

`A9 E4 E6` occurs **once in the whole ROM**. Two behaviours reach the init by a
plain `JSR $E413` — `$81:E481` and `$81:E51A` — and that is the first thing that
makes this copy unlike the other nine: **every other member of the `$81:8888`
family belongs to a single behaviour, and this one is shared.** It turns up on
levels 15, 29, 31, 33, 35, 37, 38, 43 and 44 with nine different actor *types*
standing in front of it.

Three of those levels a password reaches, and `--reach` sorted them in seconds:

| level | actor | verdict |
|---|---|---|
| 29 | type `$14` at (644,1143) | **inside solid scenery** — the whole block from (512,1088) to (736,1208) is red |
| 33 | seven of them | every one cut off from the spawn at (1231,923) |
| 37 | type `$28` at (1057,204) | **61 cells from the spawn**, with (1007,204) standable on the same row |

`tools/fit_route.py` walked it in **two legs**, first run.

### A bug the sweep found in the search itself

Level 33's actor list places one at **(1260,1737) on a level 1280 pixels tall**,
and `route` answered "2 cells" for it — from a spawn 814 pixels away. The goal
index was computed as `(y - 8) / 8 = 216` against 160 rows and used to subscript
`prev` without a bounds check, so the search was reading past the end of its own
array and reporting whatever it found. Both ends are range-checked now and an
off-map coordinate is reported as one.

**An off-map actor is a real thing to find in this ROM**, which is why this is
worth a paragraph rather than a one-line fix: the whole reachability method rests
on that predicate, and it had a way of answering "yes" that had nothing to do
with the level.

### The routine: the tenth copy, and the only one that can be switched off

```
$81:E6E4  LDY $08 : LDX $0004,Y : BNE $E72A     ; ACTOR_Z — off the ground?
$81:E6EB  CMP #$005C : BCC <CLC : RTL>
$81:E6F2  STA $22 : AND #$7FFF
$81:E6F7  CMP #$005E : BEQ $E722                ; JML $81:83C6, enemy_bubble_react
$81:E6FC  CMP #$005D : BEQ $E726                ; JML $81:847E, enemy_freeze
$81:E701  SEC : SBC #$005C : ASL A : TAX
          SEC : LDA $0C : SBC $81:8561,X
          BMI $E71A : CMP $0C : BEQ $E72A
          STA $0C : JML $81:8506                ; enemy_survived_react
$81:E71A  DEC $0A : STA $0C : STZ $7E : SEC : RTL
$81:E72A  CLC : RTL                             ; the guard's exit and no-damage's
```

**The opening guard is `enemy_b41c_collide`'s, applied to everything.**
`$81:B462` uses the same three instructions — record `+$04` is `ACTOR_Z`, the
height off the ground — but there it decides the answer to **one** id and the
creature takes the damage either way. Here a non-zero height sends *every*
collision to a bare `CLC : RTL`.

That is worth more than the routine. `port/oam.h` describes `ACTOR_Z` as a
drawing concern: an actor that jumps or is thrown keeps its Y, "and so its depth
sort order and its collision box", and only draws higher up. The overlap pass
really does ignore height. So **height immunity is not a property of ZAMN's
collision system; it is something individual handlers opt into** — two of ten do,
with the same three instructions, to different extents.

And `$5E` and `$5D` go to *different* routines here, which is
`enemy_ac92_collide`'s pair rather than `enemy_d7f6_collide`'s, where the same
`$5E` falls into the death tail with no subtraction at all. Ten copies of this
subsystem and no two of them answer `$5E` alike.

**It is the registry's first entry with no guard for a positive reason.**
`actor_845e_collide` has none because it cannot write, so there is nothing to try
on a scratch copy. This one writes plenty — it simply has no argument it declines,
because both of its `JML`s are served.

### What the movie measures

`e6e4_ignore` 1720, `e6e4_hit` 46, `e6e4_died` 46 — **and no survivors**, on a
creature the init seeds to three health. `--watch` says why, one page at a time:
`$7E:050C` goes `$0003` → `$FFFF` in a single hit at frame 4200, and `$7E:0522`
— the id this routine parks — reads **`$0060`** on the same frame. Damage 4
against health 3 is one hit, every time.

**It is not the player's weapon.** `$7E:1CBC` is parked at `$0012` by a pickup
from frame 2980 to 4158 and reads 0 after, so the player's own shots carry `$5C`
and do 1. Something else on level 37 lands the `$60`s, and this movie does not
say what.

The fight is a **spin** rather than a stand — 30-frame legs, four directions, Y
held — because four of these crowd the player at once from three sides.
`movies/level21-spin.zmv` established that shape for a chaser; this is the first
time it was chosen for a *crowd*.

### Five perturbations, four caught, and the pair that disagree

| perturbation | verdict |
|---|---|
| park the hit id over the health word | **CAUGHT** `$7E:050A`: ROM `$FF`, port `$00` |
| the death tail not stepping `$0A` down | **CAUGHT** same bytes |
| the death tail clearing carry instead of setting it | **CAUGHT** `$7E:11A0`: ROM `$00`, port `$01` — one level up, because carry is what parks the thread |
| the guard reading `ACTOR_Y` instead of `ACTOR_Z` | **CAUGHT** `$7E:050A` |
| **the guard never refusing at all** | **MISSED** |

The last two are the same two instructions and they disagree, which is the
useful result. `e6e4_airborne` reads **0** on this movie — nothing it collides
with is ever off the ground — so deleting the guard's *decision* is invisible,
while changing the *field it reads* is caught immediately, because reading
`ACTOR_Y` makes it refuse everything. **The guard demonstrably executes on all
367 calls and its branch is taken on none of them**, and only running both
perturbations says so.

The fourth one also cost a lesson the script already knew: its first anchor
occurs twice, because `enemy_b41c_collide` reads the same field with the same
line four hundred lines above. The anchor check refused it rather than breaking
the wrong routine — which is exactly what the previous round's two MISSED results
asked for.

### Corpus

**Forty-seven routines. 3,674,068 calls checked across 41 movies, 0 diverged;
branch coverage 184 of 254, 70 untaken by every input, and the census is empty
again** — from 3,588,215 across 40 and 183 of 246.

The denominator moved because the round added eight sites, and both directions
of the count are worth reading. Three of the eight are taken
(`e6e4_ignore` 1720, `e6e4_hit` 46, `e6e4_died` 46) and five are not. And
`collide_unported` and `handler_unported` went *back* to untaken, which is the
census being empty stated as coverage: with `$81:E6E4` ported there is no input
in the corpus that makes the dispatcher meet a handler the port does not have.

`movies/level29-ice.zmv` also gained five checked calls without changing a
frame — 105,475 to 105,480 — because the one call it used to decline is now one
the harness checks.

`run` substituting **only** `enemy_e6e4` is **identical at all 5,989 compared
scheduler passes** of `movies/level37-e6e4.zmv`, so its 192-cycle budget costs
nothing. All forty-seven together find **no byte of live game state differing**
on the same 5,989, and `-r none` is identical at all of them.

## The stream of fire that was a wall

**The previous round fired 596 shots at level 25's boss, did one point of damage,
and concluded that the boss does not walk into a stream of fire. There was no
stream of fire.** Every one of those shots died six pixels from the barrel.

The measurement that says so takes one command. `movies/level25-heavy.zmv` ends
with an eight-leg spin at (639..663, 561), and `--records` sampled every frame
rather than every fourth — the fire rate is 12 frames and the old sampling was
aliased against it — shows the whole life of a shot:

```
frame 6015    $1A52  $8003    665   559   $5C   $22    $0600  $81:FE0E
frame 6016    $1A52  $8003    671   559   $5C   $22    $0600  $81:FE0E
frame 6017    (gone)
```

Two frames, six pixels, and then nothing. That is not a shot missing a boss 200
pixels away; it is `movies/level17.zmv`'s signature exactly — the six-pixel death
that proved the creature there was in a sealed pen and not behind an uncut route.
The player was pressed against a wall and shooting into it.

**`--reach` says where the wall isn't.** `zamn_assets route --reach <rom> 26`
puts a clean **408-pixel corridor at y=588**, running x=748 to x=1156, straight
through the band the boss spends about 40% of its time in. The five failed
experiments all stood at y≈561: twenty-seven pixels high, on the wrong side of
the wall that bounds it.

Twenty-seven pixels is the whole difference:

| row the player fights on | where the walk ends | boss health 54 → |
| --- | --- | --- |
| y=577 | stuck at (759,577) | 54 |
| **y=585** | (1207,585) | **46** |
| y=593 | (1271,593) | 48 |
| **y=601** | (1203,601) | **46** |
| y=609 | (1271,609) | 51 |
| y=617 | (1271,617) | 54 |

Same gun, same movie, same boss. In the lane the player crosses the whole 408
pixels and lands eight hits in about four hundred frames; a row up or two rows
down it lands none at all.

**It also explains the four zeroes.** The previous round parked and held Left,
Right, Up and Down for 1,800 frames each and read four zeroes as evidence about
the boss's pathing. Re-run with a gun that is not empty — and one press of **B**
was needed, because the movie's `$67` runs dry at frame 5351 and 150 shots of
`$5C` sit unused in slot 0 — they are still four zeroes, because all four
directions walk the player into geometry and hold him there. `park-left` fires
nothing for twenty frames at a stretch; `park-down` puts a shot 6 px and stops.
The experiment never had a line of fire to measure.

### The weapon that was spent before the fight

`movies/level25-heavy.zmv` collects both `$22` objects — forty shots of `$67`, at
four damage each and not one of the four ids the boss rewrites — and then fires
every one of them walking north with Y held. The pickups land at frames 3308 and
3387; the first shot goes at 3823 and the last at 5351, all of it in transit, all
of it at about ten percent.

Holding fire from 3387 and spending them in the lane instead is the same movie
with the same route:

| | boss health at the fight | damage done |
| --- | --- | --- |
| as recorded | 54 (16 already lost in transit) | 16 |
| fire held to the lane | **70** | **26** |

Twenty-six is not seventy, so `boss_died` is still untaken. But it is the first
number in this fight that came from aiming the experiment rather than from
lengthening it.

### What the arena can actually hand you

The reason twenty-six is where it stops is ammunition, and the census is short
enough to write out. Running `$80:CA30` backwards — it is a **word** table, and
type/2 indexes it rather than being the id — gives each of level 25's six weapon
objects, and `--reach` says which of them are on the boss's side of the map:

| weapon | object | where | reachable from the lane? |
| --- | --- | --- | --- |
| `$5C` | — | starting inventory | yes, 150 shots |
| `$67` | `$22` ×2 | (1046,1045), (1110,1077) | yes, 40 shots, 4 damage |
| `$61` | `$26` ×4 | (550,700), (700,650), (1081,288) ×2 | yes, 20 shots — but rewritten to `$60` |
| `$5F` | `$06` | (989,122) | **no** — solid, nearest reached ground 48 px off |
| `$64` | `$1A` | (369,1083) | **no** — solid, nothing reachable within 40 px |
| `$62` | `$16` | (1243,505) | **no** — a sealed pocket |

So the reachable arsenal is 390 damage of ammunition against 70 of health, which
sounds ample and is not: at the fifth-or-so accuracy the lane gives, 390 buys
about 78, and nothing in the corpus has yet held that accuracy across a whole
load. `$5F` is the one that would settle it — 300 shots — and it is 48 pixels
inside scenery, with the band above it drawn `o`: open ground the search can
reach no more than the player can. The third sealed pen in the project, after
level 17's creature and level 5's `$5D`.

### The three sites the pocket holds

`boss_alt_cheap`, `boss_alt_dear` and `boss_no_damage` are one weapon between
them. All three need id `$62` or `$70` fired at this boss — `$62` is rewritten on
`LDA $0020 : AND #$0001`, a coin toss off the scheduler clock, to `$5C` (1
damage) or `$5D` (**0**), so a dozen hits takes all three sites at once and no
kill is required. The boss exists on level 25 and nowhere else, and level 25's
one `$62` is object `$16` at (1243,505).

`--reach` draws it as a five-by-three block of `o` walled on all four sides:

```
  489 ################.###.
  497 ########ooooo###.###.
  505 ########ooooo###.###.     <- the $62 sits at the middle of this row
  513 ########ooooo###.###.
  521 ################.###.
```

The nearest reached cell is 48 px east. Two rows above it there is a neck —
`o` at (1243,465) and (1243,473), one cell wide, blocked at 481 — and that is
exactly the shape the 2×2-clear box refuses on ground the game itself allows, so
it was worth walking rather than assuming. `tools/fit_route.py` put the player at
(1237,452), on the lip of it, in 15 legs.

**He goes in and stops at y=470.** Held Down from the lip, and from a column
either side of it:

| entered at | came to rest | `$62` collected |
| --- | --- | --- |
| x=1227 | (1227,470) | no |
| x=1237 | (1237,470) | no |
| x=1247 | (1247,470) | no |

Three columns, one answer, and it is the row the overlay draws as `#`. The neck
is real and it is a dead end: the player fits into the two `o` rows and the wall
at 481 stops him, with the pocket and the weapon in it sixteen pixels further
down. `$7E:1CD8` never leaves zero.

**So the three sites have no input, and not for want of a route.** The only
weapon that can take `boss_alt_cheap`, `boss_alt_dear` or `boss_no_damage` is
walled off from the only level that has the boss. They join `boss_remap_6f`,
which is unreachable for a different and simpler reason, and the seven
`*_no_damage` sites: four of the boss's five untaken branches are now accounted
for rather than merely outstanding, and `boss_died` is the one left that an input
could still close.

### And the fifth site, which no level can reach

`boss_remap_6f` wants id `$6F`, and **no object in the game can hand a player
that weapon.** `$80:CA30`'s 30 entries are ids `$0C`–`$19` and `$21`–`$30`; the
first fourteen are the weapon slots, `slot = id - $0C` and `weapon = $5C + slot`,
so the pickup weapons run `$5C` to `$69` and stop. `$6F` is above the top of the
table. Whatever carries that id into `boss_9660_collide` — `enemy_cdde_collide`
tallies the same `$6F` alongside `$64` — it is not something a player picks up,
and no route or movie will change that. It belongs with the seven `*_no_damage`
sites: named, understood, and not reachable by any input.

### Corpus

**Forty-seven routines. 3,931,989 calls checked across 42 movies, 0 diverged;
branch coverage 184 of 254, 70 untaken by every input, and the census is still
empty** -- from 3,674,068 across 41.

**The whole of that gain is one movie and none of it is coverage.**
`movies/level25-lane.zmv` contributes 257,921 checked calls and 0 declines, and
takes no site the corpus did not already have: every one of the 70 untaken names
appears in its own never-reached list, which is what says the union did not move
rather than an assumption that it did not. Worth having anyway -- a quarter of a
million more chances for the port to disagree with the ROM, on a boss handler
that had one busy input and now has two -- but the backlog is unchanged, and the
backlog is what the round was aimed at.

What did change is the shape of that backlog. Four of `boss_9660_collide`'s five
untaken branches are now closed questions rather than open ones: three of them
want a weapon sealed behind a wall, and the fourth wants an id no object in the
game can hand a player. `boss_died` is the one an input could still take.
## The first routine the profiler picked, and the flag that was not its own

**`$80:B123` is the first routine in this project chosen by measurement rather
than by the census.** The decline census names what the ROM ran and the port
lacked, and it has never named this one — it is not a collision handler, so the
dispatcher never meets it. `tools/native_share.py` put it at the top of the
first execution-weighted ranking the project has had: **5.2% of every
instruction the game executes, over 44,248 calls**, second only to a level-load
routine and a decompressor that cannot be substituted.

It is 375 instructions a call, which matters for a reason the previous round
established the hard way: `verify` abandons any call an interrupt lands inside,
so a routine has to fit inside a frame to be checkable at all. This one fits
about a hundred times over.

### What it is

A nearest-thing search, by **Manhattan distance**, over the raw 32-slot record
table rather than the linked list:

```
$80:B131  LDX #$1ACA          ; the top slot, and it walks downwards
$80:B134  LDA $0000,X : BPL   ; ACTOR_DRAW
$80:B139  LSR A : BCC         ; ...and flag bit 0
$80:B13C  LDA $000E,X         ; ACTOR_COLLIDE_ID against $05, $06, $38, $01
$80:B153  LDA $0002,X : SEC : SBC $3A   ; |dx|
$80:B163  LDA $0006,X : SEC : SBC $3C   ; + |dy|
$80:B176  CMP $38 : BCS       ; strictly nearer wins
$80:B184  CPX #$185E : BCS    ; 32 iterations, whatever the board holds
```

**The ids say what it is for.** `$05` and `$06` are the two players — the same
two numbers `victim_a264_collide` claims a victim by — and its callers are all
inside enemy bodies: `$81:86B7`, `$81:870A`, `$81:8ADC`, `$81:8B3A`. This is an
enemy choosing who to go after, and 44,248 calls is how often the game asks.

Two details the listing gives away and a summary would not. **Ties go to the
higher slot**, because the test is `BCS` on a strict comparison and the walk
runs downwards. And **`$44` is never seeded**: when nothing matches — no player
drawn, or none wearing one of the four ids — the routine hands back whatever
record the last successful search left there, alongside a distance of `$FFFF`.
The port reproduces both, because reproducing the second is the only way a
caller that ignores the distance behaves the same.

### The flag that was not its own

The first version passed **1,006 of 1,006 calls on `movies/level1.zmv`** and
then failed on four movies at once, with one line of diff:

```
flag N: ROM 0, port 1
```

Memory matched everywhere, A/X/Y matched everywhere. The shim had read N and Z
off `$80:B18B  LDA $38`, which is the last instruction that looks like it
computes anything — and the routine ends `LDA $38 : PLD : RTL`. **`PLD` sets N
and Z from the value it pulls.** What a caller sees is the sign and zeroness of
its own direct page, restored one instruction before the return, and the
distance's flags are overwritten before anybody can read them.

Level 1 hid it perfectly. Every search on that movie found something, and a
distance under `$8000` has its sign bit clear exactly like a thread's direct
page does. The disagreement needs a search that comes up empty and leaves
`$FFFF` in A — which is 2,463 of level 21's 3,503 calls, and none of level 1's.

**The project had already learned this twice.** `shim_oam_buffer_clear` carries
the note "`PLD` is the last flag-setting instruction, so N and Z describe the
direct page it restores rather than anything the routine computed", and
`src/port/apu.c` says of the same thing "N and Z are the `PLD`'s, not the
`INY`'s. That is easy to get wrong from the listing." Both were written before
this round and neither was consulted. So the rule is worth stating as a rule
rather than as three separate discoveries: **a shim's flags come from the
routine's last flag-setting instruction, which is not usually the one that
computes its result** — and on any routine that opens `PHD`, that instruction is
the `PLD`, not whatever precedes it.

Carry is the exception here and it is worth knowing why: `PLD` does not touch
it, so carry is still what the `CPX #$185E` that ended the walk left. Always
clear, because the walk always ends the same way.

An audit of the registry for the same shape found four routines that open `PHD`
or end `PLD` and claim N or Z. Two of them — `oam_buffer_clear` and
`apu_play_sfx` — already model it correctly, which is where the two notes above
came from. `thread_spawn` opens `PHD` but does not restore it at its return
opcode, so it does not qualify.

### One flag bit, named for where it is

`ACTOR_FLAGS` bit 0 had no name. Every live record in every display list sampled
carries it — the values seen are `$8000`, `$8001`, `$8003`, `$8005`, `$8009` —
and `$80:B123` is the only reader found, requiring it alongside `ACTOR_DRAW`.
It is `ACTOR_ACTIVE` in `port/oam.h` now, with a comment that says what clears
it has not been established, because it has not.

`nearest_inactive` — drawn but without that bit — is untaken by every input in
the corpus, which is the coverage table saying the same thing from the other
side.

### Corpus

**Forty-eight routines. 4,066,151 calls checked across 42 movies, 0 diverged;
branch coverage 188 of 259, 71 untaken by every input, and the census is still
empty** -- from 3,931,989 and 184 of 254.

`actor_nearest` brought 134,162 checked calls and five sites, of which **four
are taken and one is not**: `nearest_inactive`, a record with `ACTOR_DRAW` set
and flag bit 0 clear. No input in the corpus produces one, which is the coverage
table agreeing with what the display lists already showed -- every live record
carries that bit -- and is the reason `port/oam.h` names it for its position
rather than claiming to know what clears it.

**The share of the game's work this moved is the number worth keeping.** One
routine took the native share of executed instructions from 41.4% to **46.6%**,
and the call-weighted estimate from 55.1% to **60.0%**. For contrast, the same
ranking says porting every actor and victim behaviour the fourteen
password-reachable levels place -- 31 addresses, 291 actors and 140 victims --
would move it by **0.15%**. Twenty rounds of following the decline census had
been an accidental profile-guided ordering; this is the first round that had the
profile.

## The routine next door, and a lesson that paid immediately

**`$80:BF67` is `$80:B123`'s neighbour in every sense**: eighty bytes further
down bank `$80`, called by the same four enemy bodies (`$81:85E3`, `$81:8627`,
`$81:89E6`, `$81:8A15`), and second on the execution-weighted ranking at **1.8%
of every instruction the game executes over 43,605 calls**. Where `actor_nearest`
asks *who is closest*, this asks **is anything standing within six pixels of this
point**, and answers in the carry.

```
$80:BF72  LDX $9C : BEQ          ; visible_actor_count, in bytes
$80:BF78  LDY $137E,X            ; backwards through visible_actors
$80:BF7B  CPY $38 : BEQ          ; the record the caller wants ignored
$80:BF7F  LDA $0000,Y : LSR A : BCC     ; ACTOR_ACTIVE
$80:BF85  LDA $000E,Y : BEQ             ; no collision id
$80:BF8A  CMP #$000C : BCC / CMP #$0033 : BEQ / BCC   ; the band it steps over
$80:BF96  CMP #$0007 : BEQ / CMP #$0008 : BEQ
$80:BFA0  LDA $0002,Y : SEC : SBC $3A : CLC : ADC #$0006 : CMP #$000C : BCS
```

It walks `visible_actors` rather than the record table, so it sees only what
this frame's cull kept and costs whatever the board is wide instead of a fixed
32. The window is the same `ADC #$0006 : CMP #$000C` trick `actor_overlap_pass`
uses at 8 and 16: **-6 <= d <= +5** in two instructions, a pixel wider to the
left than to the right.

**The id filter is written down nowhere but the disassembly** — `$00` never, the
whole band `$0C`-`$33` never, `$07` and `$08` never, and everything else, which
is `$01`-`$06`, `$09`-`$0B` and everything above `$33`. Three comparisons and
four ways out of them, and reading the two `BCC`s as one range is the only way
to get it right.

### The part that did not go wrong

Both exits are `PLD` and *then* an explicit `SEC` or `CLC`. So **carry is
genuinely this routine's own and N and Z are the `PLD`'s** — the same split
`actor_nearest` produced, reached from the opposite direction, and the reason
the rule from that round is worth stating as a rule: read the last three
instructions, not the last one that computes something.

Written that way from the start it passed on the first run, on every movie,
including the four that caught the previous round out: **26,796 calls across
seven movies, 0 diverged, no second attempt.**

A, X and Y are all claimed and none is tidy, because the ROM never tidies them.
It falls out of the loop with the last comparison's arithmetic in A, the loop
index in X — `$FFFE` when the walk ran out, the count being a byte count and so
always even — and the last entry examined in Y, which on the found path is the
record that matched.

### Corpus

**Forty-nine routines. 4,182,751 calls checked across 42 movies, 0 diverged;
branch coverage 196 of 269, 73 untaken by every input, and the census is still
empty** -- from 4,066,151 and 188 of 259.

Eight of the ten sites it added are taken. The two that are not say something:
`at_point_empty` means no input ever calls it with nothing visible at all, and
`at_point_id_named` means nothing wearing id `$07` or `$08` has ever stood in
front of a caller — the band test at `$0C`-`$33` covers the common case and
those two are a separate pair of comparisons for ids that no movie has put in
the way.

`run` substituting only this routine differs on 1,963 of 2,389 passes, and that
is the declared model working rather than failing: `$7E:0F00` and `$7E:0F02`
hold the `PHD` and the `PEA` the port never pushed, which is exactly what
`stack_bytes = 4` exists to say. **No byte of live game state ever differed.**
The cycle figure is 2,645, weighted across all 26,796 calls rather than taken
from one movie — the spread is 698 to 6,958, because unlike `actor_nearest`'s
fixed 32 slots this one stops as soon as it finds something.

**The native share is 48.4% of executed instructions and 61.8% call-weighted**,
from 46.6% and 60.0%. Two rounds of following the profile have moved it seven
points.

## The routine after the routine next door, and 6% of the game doing nothing

**`$80:BFC8` is `actor_at_point` again, eighty bytes further down bank `$80`,
asking a different question about the same board.** Third on the profiler's
list at **2.2% of every instruction the game executes over 41,232 calls**, and
the first of these three that does not belong to an enemy.

### What it is

Both callers are inside `$80:E4C1`, and reading that routine is what names this
one. It is the **movement step validator**: something wants to be at a new
position, and `$80:E4C1` puts the candidate through four tests in a row —

```
$80:E4C8  JSL $80AE14      ; the terrain under the point
$80:E4D7  JSL $80A8B3
$80:E4E3  JSL $80BFC8      ; <- this one
$80:E4F9  JSL $80B422
$80:E4FF  LDA $34 : STA $30  ; ...and only now is the step taken
```

— committing the candidate only if all four agree. So a set carry here means
**the step is blocked**, and this is a collision test rather than a search:
nothing about *which* record it found is ever used, which is why the routine
can afford to stop at the first one.

Structurally it is `actor_at_point` line for line: the same backwards walk of
`visible_actors`, the same `ACTOR_ACTIVE` test, the same six-pixel window on
both axes, and the same `PLD` then explicit `SEC`/`CLC` at both exits. Two
things differ, and they are the whole of the semantics.

**It takes no self argument.** Where `actor_at_point` skips one record the
caller names in A, this skips `$D2` and `$D4` — *both players, always*. A
walker may walk through a player; deciding what that costs is somebody else's
job. Which also means A is untouched on the way in, and that matters: see
below.

**And the id filter is much narrower**, which is the fiddly part and is written
down nowhere but the disassembly:

```
LDA $000E,Y : BEQ skip            ; $00 never
CMP #$000C  : BCC named           ; under $0C -> straight to the named chain
  CMP #$0033 : BEQ skip : BCC skip  ; ...so $0C..$33 goes out wholesale
  CMP #$005C : BCS skip             ; ...and $5C and up with it
named:                              ; <- and $34..$5B FALLS THROUGH TO HERE
CMP #$0005 : BEQ skip
CMP #$0006 : BEQ skip
CMP #$0007 : BEQ skip
CMP #$0008 : BEQ skip
CMP #$0002 : BEQ skip
CMP #$0001 : BEQ skip
CMP #$0037 : BEQ skip
```

The fall-through is the trap. The high half does not decide on its own: an id
between `$34` and `$5B` drops out of the bottom of it and is then put through
all seven singleton comparisons as well — which is the only reason the
`CMP #$0037` is live, since the other six are all below `$0C` and can only be
reached down the `BCC`. Reading the two branches at `$BFF5`/`$BFF7` as
anything other than one range, or missing that the block has no exit of its
own, gets a different accept set.

What survives: `$03`, `$04`, `$09`, `$0A`, `$0B`, and `$34`..`$5B` except
`$37`. Note `$05` and `$06` in that chain — the two players again, this time by
id, so a player is refused twice over.

### The argument the routine never reads

`$80:BFC8` opens `PHD : PEA $0000 : PLD : STX $3A : STY $3C` and there is no
`STA`. A is never stored and never read, and on the `LDX $9C : BEQ` path it is
never written either — so **a caller that asks about an empty board gets its
own accumulator back**, and a shim that published a constant would diverge on
the first frame with nothing on screen. `$80:E4DD  LDA $08` loads one anyway,
which is the caller filing the thread slot for a routine that does not want it.

So `in->a` is threaded through the port, and it is not passed because the
routine wants it. It is passed because the routine's *silence* about it is part
of the contract — which is the same class of fact as `PLD` setting N and Z, and
worth looking for in the same place.

### It passed first time

Both exits are `PLD` and *then* an explicit `SEC` or `CLC`, so carry is
genuinely the routine's own while N and Z are the caller's direct page coming
back off the stack. That is the split `actor_nearest` cost four movies to
learn and `actor_at_point` confirmed from the opposite direction; written that
way from the start, this one was right on the first run.

**311 of 311 calls on `movies/level1.zmv`, 0 diverged, no second attempt.** And
`run` substituting only this routine is **identical at all 2,389 compared
scheduler passes** — where `actor_at_point` differed on 1,963 of them. The
difference is not correctness but traffic: `stack_bytes = 4` describes the same
two unpushed words either way, and at 311 calls across 2,400 frames a pass
boundary rarely lands on them.

The cycle figure is **3,630, call-weighted across 34,211 calls on eleven
movies** rather than taken from one; the spread is 426 to 7,794. Borrowing
`actor_at_point`'s 2,645 would have been 27% low, and the reason is the id
filter: the same loop with a much narrower accept set gets far fewer early
exits, so the walk usually runs all the way to the end of the list.

### Corpus

**Fifty routines. 4,317,463 calls checked across 42 movies, 0 diverged; branch
coverage 208 of 283, 75 untaken by every input, and the census is still empty**
— from 4,182,751 and 196 of 269.

**Twelve of the fourteen sites it added are taken**, which is the best ratio any
round here has managed, and the two that are not are the two that need a board
state no movie has produced: `obstacle_empty` wants a call with nothing visible
at all, and `obstacle_inactive` wants a record that survived the cull but has
lost flag bit 0. Both are reachable in principle; neither is worth an input of
its own yet.

Two of the fourteen exist only because the ROM's control flow is worth pinning
down rather than because a caller cares. `obstacle_id_below_band` and
`obstacle_id_above_band` mark the two ways into the named chain, and having both
taken is what proves the fall-through above is real rather than a misreading —
if `$34`..`$5B` did *not* fall through, `obstacle_id_above_band` would be dead.

## The 6% that was a CPU doing nothing

Then the ranking that picked this routine was asked what to do next, and the
answer was wrong.

`$80:9F9D` sat at the top with **6.0% of every instruction the game executes,
over 49 calls** — 394,000 instructions a call, which for a routine that is nine
instructions long should have been the tell. It is:

```
$80:9F9D  STZ $00C8
$80:9FA0  LDA #$9F62 : LDY #$0080 : JSL vbl_queue_a_add
$80:9FAA  BIT $00C8 : BPL $809FAA      ; <- wait for the callback to fire
$80:9FAF  RTL
```

A **busy-wait**. Summing the profile over those five bytes: **19,289,582 of the
19,289,827 instructions credited to the routine are the two-instruction spin**,
99.999% of it, and the same reading finds 1.85% more in `apu_ipl_upload`'s three
`CMP $2140 : BNE` handshakes with the SPC700.

That is **7.9% of the game's executed instructions spent waiting**, and it was
sitting in the denominator of the completion estimate and at the top of the list
of what to port next. Porting it would have moved the number six points and
achieved nothing whatever, because the C would have to spin on the same flag.

So `tools/native_share.py` now carries a table of wait sites, checked against the
profile rather than assumed, and reports three things it did not before: what
share of the run is waiting, the completion estimate with that out of the
denominator, and a **ranking by work** in which a routine is charged only for
what it does. `$80:9F9D` disappears from that ranking entirely;
`apu_ipl_upload` drops from 3.1% to 1.3% with the wait annotated beside it.

This is the second time in three rounds that following the profile has meant
first repairing the profile, and both repairs were the same shape: the tool
measured exactly what it claimed to and the claim was the wrong one. Thread
bodies were invisible because attribution followed the call graph; spins looked
like work because instructions were the unit. Neither was a bug.

### And one entry that is still wrong

`$82:AB5B` is third on the repaired ranking at 3.3%, and it did not execute a
single instruction in any of the ten traces — CDL flag `00`, exec count 0. Its
3.3% is a bucket: the first executed byte after it is `$82:AC07`, 172 bytes
further on, which is **jump-entered** and so is not a boundary the attribution
knows about, and everything from there to the next known entry is filed under
the label above it.

That is the documented caveat behaving exactly as documented, and the fix is not
another hand-written line in `behaviours.txt`. It is a tracer change: `CDL_JUMP`
is currently set by both `FLOW_JUMP` and `FLOW_BRANCH`, so it marks every branch
target and cannot be used as an entry list without shattering every routine into
basic blocks. Marking *long and indirect* jump targets separately would find
jump-entered bodies exactly, at the cost of re-tracing all ten profiles.

## Finishing the step validator, and a field the asset pipeline had left blank

**Three routines this round, because they are one thing.** `$80:E4C1` puts a
proposed step through four tests and commits it only if all four agree; last
round took the third, and these are the other two plus the enemy's copy of the
first.

```
$80:E4C8  JSL $80AE14      terrain_blocked          <- this round
$80:E4D7  JSL $80A8B3
$80:E4E3  JSL $80BFC8      actor_obstacle_at_point  <- last round
$80:E4F9  JSL $80B422      terrain_out_of_bounds    <- this round
$80:E4FF  LDA $34 : STA $30
```

`$80:AE97` is not in that list; it is `$80:AE14` copied with one instruction
changed, and its three callers are enemy bodies. Together the three are 3.8% of
the work the game does.

### What a footprint test actually reads

The game does not consult the level record at run time and never looks at the
block map at all. It expands the level once into a 16-bit tilemap in WRAM bank
`$7F` — which `src/assets/level.h` already reproduces byte for byte — and from
then on collision is two indirections and a mask:

```
LDA [$28],Y : AND #$03FF : ASL A : TAY     ; tilemap entry -> BG tile number
LDA [$BA],Y : LSR A : BCS blocked          ; -> its attribute word -> bit 0
```

Neither pointer is a constant. `$7E:4328` holds one word per tile row — that
row's byte offset into the map — so a row lookup is a table read rather than a
multiply, and `[$BA]` is a 24-bit pointer the level loader fills in. Three
scalars fall out of watching level 1 load: `$B2` is 352, `$B4` is 104, and
`[$BA]` is `$7E:611A`. Level 1's record says 22 by 13 blocks, and 22 x 8 x 2 is
352 while 13 x 8 is 104, so those two are the row stride in bytes and the row
count in tiles, exactly.

The address arithmetic is the same two instructions on both axes:
`LSR A : LSR A : AND #$FFFE`. Dividing by four and then clearing the low bit is
dividing by eight and doubling — so one idiom produces a byte offset along a row
of 16-bit entries *and* a word index into the row table, and the ROM never has
to say which it meant.

Six probes: three tiles across, two rows down, a 24x16 box against an origin of
`(x - 9, y - 8)`. Nine and eight, not eight and eight, and nothing says why.

### The bit nobody had identified

`src/assets/level.h` has carried this comment since Phase 2:

> Bit 0 of a tile attribute word blocks movement. **The rest of the word is not
> yet identified.**

`$80:AE97` identifies the next one. It is `$80:AE14` byte for byte over the same
six tiles with `BIT #$0002 : BNE` where the other has `LSR A : BCS`, and its
callers — `$81:80CB`, `$81:85D7`, `$81:8618` — are all enemy bodies, the last
two being the same routines that call `actor_nearest` and `actor_at_point`
twelve and fifteen bytes further on to pick who to chase.

So the obvious reading is "bit 1 blocks enemies", and the obvious follow-up
question is whether it is simply a stricter or looser bit 0. **It is neither.**
Reading all 55 levels' attribute tables straight out of the ROM:

| | tiles |
| --- | --- |
| bit 0 set | 15,166 |
| bit 1 set | 15,469 |
| **bit 0 without bit 1** | **216** |
| **bit 1 without bit 0** | **519** |
| both | 14,950 |

Neither set contains the other. They are two independent classes of blocking
terrain, and 735 tiles across the game stop one kind of mover and not the other
— which is a design decision visible nowhere except in these two masks.

Two more copies of the same loop exist and are not ported: `$80:AF2C` tests bit
2 (after calling `$80:B422` first), and `$80:AF66` tests bit 12. The comment in
`level.h` now says that too, instead of "not yet identified".

### The first routine here with no `PHD`

`$80:B422` is the level-extents test, and it is the interesting one.

Every other shim in this project that publishes N and Z takes them from the
`PLD` on the way out, because a pull sets them from the value pulled — the trap
`actor_nearest` cost four movies to learn. **This routine never touches the
direct page**, so there is nothing to take them from, and they are simply
whatever the instruction that decided the answer left behind.

There are six such instructions and they do not agree:

| exit | reached by | N and Z from |
| --- | --- | --- |
| `$B445 SEC : RTL` | `BMI` on X | the `TXA` |
| `$B445` | `BCC #$0004` | that compare |
| `$B444 RTL` | `BCS $00B2` | that compare, **and its carry, not the `SEC`'s** |
| `$B445` | `BMI` on Y | the `TYA` |
| `$B445` | `BCC #$0002` | that compare |
| `$B444` fall-through | — | `CMP $00B4`, which is the whole answer |

Reading the routine's last instruction and publishing that everywhere would be
right on one path in six. The `SEC` four of them share does not touch N or Z, so
it hides nothing and fixes nothing; the fifth exit skips it entirely and keeps
its own compare's carry, which is the only reason it can afford to.

That is the same lesson as the `PLD` one from the opposite side: **the flags a
routine returns come from the last instruction that ran, not the last one
written**, and a routine with several exits has several answers.

### All three passed first time

**2,122 calls on `movies/level1.zmv` — 312, 1,494 and 316 — 0 diverged, no
second attempt on any of them.** `run` substituting each alone: `terrain_blocked`
and `terrain_out_of_bounds` are identical at all 2,389 compared passes, and
`terrain_blocked_enemy` differs on 22 of them, all of it declared stack.

The cycle figures are **1,591**, **1,578** and **359**, each call-weighted
across eleven movies rather than taken from one -- 41,635, 45,015 and 59,422
calls. The two footprint tests have the same shape because they are the same
loop, and their spread (678..1,776 and 696..1,842) is simply how many of the
six probes run before one blocks. `terrain_out_of_bounds` has the narrowest
spread of anything in this registry, 138 to 390, because it has six exits and
not one of them is a loop.

### Corpus

**Fifty-three routines. 4,888,163 calls checked across 42 movies, 0 diverged;
branch coverage 219 of 296, 77 untaken by every input, and the census is still
empty** -- from 4,317,463 and 208 of 283.

**Eleven of the thirteen new sites are taken**, and which eleven is the point.
Five of `$80:B422`'s six exits are among them, so the table above is checked
rather than argued: the corpus really does drive a proposed step off the left
edge, off the right edge, off the top, off the bottom, and onto the map, and the
port publishes a different pair of flags for each. Only `bounds_y_negative` is
missing -- a negative Y reaches the `TYA` only after X has already passed three
tests, and nothing in the corpus has been that far up while still being that far
in.

The other one untaken is `terrain_attrs_bank_7f`, which is untaken on purpose;
see below.

**The native share is 54.1% of executed instructions and 67.3% call-weighted --
58.7% and 73.0% with the waiting out of the denominator** -- from 50.6% and
64.1%. Three routines in one round is the largest single step this measurement
has recorded, and it is the step-validator hypothesis paying off: they were
picked as a *unit* rather than off the top of the list, and the unit was worth
more than its parts' ranking suggested.

### And a bank that has never been seen

One of the thirteen new sites is there to make an assumption falsifiable rather
than to cover a branch. The attribute table's bank byte at `$BC` is `$7E` every
time anyone has looked, and the port handles `$7F` correctly anyway — so
`terrain_attrs_bank_7f` exists to say so out loud. If it is ever taken, that is
a level doing something no level has been seen to do, and the port will already
have got it right.

## The step validator, closed — and a number that was inflating itself

**Two routines this round, and they are the two ends of the same thing.**
`$80:E4C1` opens by asking where the mover wants to go and then spends the rest
of itself asking whether it may. The last two rounds took three of the four
permission tests. This one takes the fourth, and the proposer:

```
$80:E4C1  JSR $E450        step_propose             <- this round
$80:E4C4  LDX $34 : LDY $32
$80:E4C8  JSL $80AE14      terrain_blocked
$80:E4D7  JSL $80A8B3      step_tether_blocked      <- this round
$80:E4E3  JSL $80BFC8      actor_obstacle_at_point
$80:E4F9  JSL $80B422      terrain_out_of_bounds
$80:E4FF  LDA $34 : STA $30
...and then the whole thing again for the other axis
```

Everything `$80:E4C1` calls is now C. Only the sequencing is the ROM's — which
is also why a mover slides along a wall instead of stopping against it: the two
axes are proposed together and validated separately, so a diagonal into a wall
keeps whichever half of itself was legal.

### Speed is a table of how often to step twice

`$80:E450` is thirteen instructions and three tables, and the tables are the
part worth having. `$24` on the mover's page is a direction **already doubled**,
so it indexes two nine-word delta tables directly:

| `$24` | dx | dy | |
| --- | --- | --- | --- |
| `$00` | 0 | 0 | not moving |
| `$02` | 0 | -1 | up |
| `$04` | +1 | -1 | up-right |
| `$06` | +1 | 0 | right |
| `$08` | +1 | +1 | down-right |
| `$0A` | 0 | +1 | down |
| `$0C` | -1 | +1 | down-left |
| `$0E` | -1 | 0 | left |
| `$10` | -1 | -1 | up-left |

Clockwise from up, zero meaning still, and **every delta is one pixel**. There
is no speed field anywhere in the arithmetic. Speed is expressed entirely by
`$80:E45C  LDA $E4AA,X : AND $0020` — a mask ANDed with the frame counter, and
if the result is non-zero the delta is added a second time. The mask is picked
by `((dir & 2) << 2) + $76`, which is two rows of four:

| `$76` | diagonal | cardinal | pixels per frame |
| --- | --- | --- | --- |
| `$00` | `$0001` | `$FFFF` | 1.5 diagonal, 2 cardinal |
| `$02` | `$0000` | `$0001` | 1, 1.5 |
| `$04` | `$0000` | `$0000` | 1, 1 |
| `$06` | `$0000` | `$0000` | 1, 1 |

`dir & 2` is bit 0 of the undoubled direction, so the **odd** directions — the
four cardinals — take the second row and the diagonals take the first. That is
the whole of ZAMN's diagonal normalisation: 1.5 against 2 is 0.75 where the
right answer is 0.707, so moving diagonally covers about 6% more ground per
frame than moving straight. It is one table lookup and no multiply, which in
1993 was the entire argument.

A mask of `$FFFF` is "always", except that it is ANDed with a counter and the
counter is zero one frame in 65,536. About once every eighteen minutes the
fastest thing in the game takes a single half-speed step. There is no coverage
site for it: it is a distinct *reason* for the single-step path, not a distinct
path, and a site no corpus could ever reach would dilute the one number that
file exists to keep honest.

**And the flags are not the store's.** `CPX #$0000 : BEQ` sits between the first
add and the second, so the single-step path — most of them — returns that
compare's flags: `Z` set, `C` set, `N` clear, whatever the mover's new position
turned out to be. Only a double step returns the arithmetic's. It is `$80:B422`'s
lesson from the other side, and this time it cost nothing, because it was
expected.

### The leash

`$80:A8B3` is the only movement test that reads state belonging to somebody
other than the mover, and it is the co-op tether.

`$D6` is new here. `$80:A8A4` registers a player's record with `STA $00D2,X`,
and when `X` is zero — player A — it also files the task that was running at the
time. Nothing else writes it, and one routine reads it: this one, to find out
which of the two players is asking, so that the answer can be about **the other
one**.

The rule is two rules. A candidate inside a window around the other player is
allowed: `-$E0 <= dx < $E0` and `-$B0 <= dy < $B0`, so 224 by 176 pixels, a
little under two screens. Outside the window there is a second chance, and it is
the generous one — the candidate is allowed anyway if the Manhattan distance
from the mover to the other player is **strictly less** than the two players'
current separation. You can always walk toward your partner; you can only walk
away until the leash runs out. A tie is refused, by a `BEQ` placed one
instruction before the `BCS` that would otherwise have allowed it.

The `PHD` is back, so N and Z are the caller's direct page again, and there is
nothing to add to what `actor_nearest` cost four movies to learn. The register
outputs are the interesting part instead, because all three are live and all
three differ per exit: the alone exit leaves `A` at zero, still holding the
`LDA #$0000` that set the direct page eleven instructions earlier, and `Y` at
the zero record it had just read.

### Both passed first run, and half of one had never been read before

**467 calls on `movies/level1.zmv` — 156 and 311 — 0 diverged, no second
attempt on either.** The 2:1 is `$80:E4C1`'s shape: one proposal, two axes, two
tethers. `run` substituting each alone is identical at all 2,389 compared
scheduler passes.

Level 1 covers `$80:A8B3` badly on purpose, though, and it is worth saying why.
**Every CDL this project has ever produced marks the whole of the tether's far
path as data.** `$80:A8CC` through `$80:A936` — 107 bytes, two thirds of the
routine, including both windows and the entire distance comparison — appears as
`.db` in `analysis/bank_80.asm` and in all four of its successors, because no
traced movie had ever reached it. The reason is one line of the routine: in a
one-player game the reference record is zero and it returns eleven instructions
in. The C for those 107 bytes was written by hand out of a hex dump.

`movies/level1-2p.zmv` settles it. **11,100 calls, 0 diverged, and the only two
sites it misses are `speed_dir_still` and `tether_alone`** — so the far path is
not merely reached but exhausted: both movers, both window edges, the closing
case, the leash, and the tie the `BEQ` refuses. Two thirds of a routine that
existed only as a hex dump, checked against the ROM 7,013 times.

That is the corpus paying for itself in a way the coverage number does not show.
The two-player movies were cut four rounds ago for `actor_obstacle_at_point`'s
`obstacle_player_b` site, which is a single branch. What they were actually
worth was this.

### The number was inflating itself

This is the part of the round that matters most, and it is not about the game.

`native_share.py` went from 54.6% to **57.8%** when these two were registered,
and the two of them together are 0.8% of the work the game does. Three points
had come from somewhere.

They had come from `$80:A937`: four bytes, a `JSL $80A93B`, beginning one byte
past `$80:A8B3`'s `RTL`, and called by nothing. Attribution is by nearest
preceding entry, so `$80:A937` belonged to `$80:A8B3` — and `owner_of` maps a
**call site** to the entry above it, so the edge `$80:A937 -> $80:A93B` was
filed as *`$80:A8B3` calls `$80:A93B`*. The subsumption closure marks a routine
native when all of its callers are native. `$80:A8B3` had just become native.
So `$80:A93B` did, and then `$80:A93F`, and then the four tilemap scroll
routines under it — **1.6 points of native share with no C behind it**, and
`$80:A93F` is the routine last round's write-up had explicitly set aside as a
subsystem rather than a leaf.

The documented caveat has always been that a jump-entered routine scores zero
and its neighbour scores too much. That is the loud shape of the problem, and
somebody checks it, because a row that is too big looks too big. This is the
quiet shape: the neighbour does not inherit the orphan's *work*, it inherits the
orphan's *outgoing calls*, and what those buy appears in no column of the
report.

`JUMP_ENTRIES` now holds `$80:A937` and `$82:AC07`, next to `WAIT_SITES` and for
the same reasons — found by reading, checked against the profile, written down
rather than remembered. With it the round measures **54.6% -> 55.4% strict and
68.0% -> 68.8% weighted**: 0.8 points for 0.8% of the work, the arithmetic
agreeing with itself, which is the only evidence available that the fix is
right.

The general check is cheap and belongs in every round from here: **port a
routine, and see whether the number moves by more than that routine is worth.**

### And a to-do list that was three deep in things this phase cannot finish

While the ranking was being distrusted it was worth asking what else it had been
saying. Its top three rows — 12.6% of everything the game does — cannot go
through the harness at all, and five of the top thirty cannot:

| | share | why not |
| --- | --- | --- |
| `$80:CD20 lzss_decompress` | 5.3% | written; outlives a frame, so unregisterable |
| `$80:8353 thread_yield` | 4.3% | the primitive the harness measures passes against |
| `$80:83E0 vbl_queue_a_run` | 3.0% | a dispatcher |
| `$80:816C nmi_entry` | 1.5% | an interrupt vector; nothing calls it |
| `$80:843D vbl_queue_b_run` | 1.1% | the other dispatcher |

`$82:AB5B` was on this list when the round started, at 3.3%, and is not on it
now. Adding `$82:AC07` to `JUMP_ENTRIES` moved that work to the routine that
actually does it, and that routine is ordinary. **The blocked total is 15.2%,
not the 18.5% the first pass reported** — the difference was a labelling bug,
and no code changed.

The dispatchers were the surprise, because the last write-up named
`vbl_queue_a_run` as *"the first of those that is a genuine, self-contained,
per-frame leaf"*. It is nothing of the kind. It pushes a far return address,
pushes a job's address out of a WRAM table and `RTL`s into it, so porting it
means porting every job that can be in the table, and queue A has thirteen
distinct enqueue sites. What the profiler credits to it is not the jobs —
attribution stops at the next entry — it is the **scan**: sixteen slots from the
top down, every call, whether two are live or none. 146 instructions a call over
60,940 calls, and as unavoidable as it sounds.

### ...which is not the same as saying they cannot be written

Worth being exact about, because the short version of that table reads like a
wall in front of the project and it is not one.

`src/port/lzss.c` is a complete transcription of `$80:CD20` and exists today; it
cannot be *registered* because it outlives a frame and the harness diffs at
frame boundaries. `thread_yield` is the primitive the harness measures scheduler
passes **with** — substituting it is substituting the ruler — and the port has
had its own coroutine machinery since the round that solved that problem.
The two dispatchers are twenty lines of C
each, and a native renderer will not want a vblank job queue that `RTL`s into
ROM in the first place; `nmi_entry` is an interrupt vector rather than a
subroutine.

**None of the 15.2% is un-reimplementable. All of it is outside the reach of one
instrument** — and that instrument verifies leaves called within a frame, so
what it cannot see is precisely what *is* the frame: the scheduler, the
dispatchers, the decompressor that spans a level load. They were never going to
fit through it. Phase 4 is defined as the point where that inverts, the port
takes the main loop, and these stop being unverifiable and become the port's own
skeleton — checked by whole-movie replay instead of per-call diffing.

What the table does mean is a **ceiling**: while Phase 3's definition of
"ported" is "registered and diffed per call", strict native share cannot pass
about **85%**, and the steps get smaller from here. Better to write that down
now than to discover it at 82% and wonder what went wrong.

The ranking prints `!` against those rows and says why. They stay in the
denominator, because a completion estimate that quietly excluded the hard parts
would be worth nothing.

## The third footprint test, and the loader that was mostly asleep

With the walls marked, the top *portable* row of the ranking was `$82:90F7` at
2.2% over 46,563 calls. Two things came out of taking it, and the second one is
worth more than the routine.

### $82:AC07 was 97.8% waiting

`$82:AC07` sat third on the ranking at 3.3%, and the round before had already
established what it is: the level loader, the real code behind the `$82:AB5B`
label that never executes. Before porting it, it was worth summing the profile
over its bytes the way `$80:9F9D` was — and 9,673,566 of its 9,893,468
instructions, **97.8%**, are two three-instruction loops:

```
$82:AC65  LDA $0016 : CMP #$0078 : BCC $82AC65     ; 3,120,651
$82:AC92  LDA $0016 : CMP #$0078 : BCC $82AC92     ; 6,553,566
```

`$0016` is `nmi_frame_counter`. Both are the loader holding for **120 frames** —
two seconds each — once for the level-intro screen and once after the block
library is decompressed. That is 3.0% of every instruction the corpus executes,
spent on a deliberate pause, and it was fourth on the list of things to port
next.

Into `WAIT_SITES`, and `$82:AC07` leaves the top thirty entirely. **The waiting
is now 10.93% of every instruction the game executes**, up from 7.89%, and the
honest denominator moved with it. That is the third time this table has been
extended by reading one row of the ranking, and the third time the row turned
out to be a spin.

### ...and $82:AB5B is now fully accounted for, three rounds late

That closes a row this project has been carrying since the profiler was built,
and closes it against what was written down about it. `docs/analysis-tools.md`
has said for several rounds that `$82:AB5B` *"never executed at all: CDL flag
`00`, exec count 0, in every one of the ten traces"*. It executes 520 times.

The mistake is a specific and repeatable one: **the flag was read out of one
trace and the count asserted of another.** `analysis/zamn.cdl` is a level 1
trace and level 1 genuinely never reaches `$82:AB5B`, so its flag there is `00`.
The ten profile traces are a different set of runs, and in those the flag is
`69` — code, and a `JSR`/`JSL` target. Force-disassembling it shows an ordinary
sixteen-iteration loop over the sprite palette at `$7E:5528`.

So the whole of that 3.3% row, finally, is:

| | |
| --- | --- |
| `$82:AB5B` itself, a palette loop over 16 colours | **0.03%** |
| `$82:AC07`'s two 120-frame holds | **3.03%** |
| everything else `$82:AC07` does | **0.07%** |

Three rounds of "the third biggest thing on the list" was one real routine
thirty times smaller than advertised, plus four seconds a level of deliberate
silence. Neither was ever worth porting. `hotbytes.py` prints the 520 in one
second and would have said so the first time.

### $82:90F7 is $80:AE14 with two more columns and one more rule

Everything from `port/terrain.h` is here unchanged: the same two coordinates,
the same `LSR A : LSR A : AND #$FFFE`, the same row table at `$7E:4328`, the
same pointer built in `$28`, the same attribute table through `[$BA]`, and the
same attribute bit 1. Four things differ.

**Ten probes, not six.** Five tiles across instead of three, so a 40x16 box
instead of 24x16, and the origin moves left to match — `SBC #$0011` where
`$80:AE14` has `SBC #$0009`. The two rows are still one row-stride apart.

**The loop is written out ten times.** That is why the routine is 406 bytes for
what `$80:AE14` says in 130, and it is also what made it the right row to take:
**no byte in it runs more than once per call.** 46,563 calls, and the busiest
byte in its 269-byte span ran 46,563 times. Compare `$82:AC07` two rows above
it, where the entry ran ten times and one byte ran 2,184,522 — 218,452 times per
call, which is a spin and not a routine.

That comparison is now `tools/hotbytes.py`, because it had been done by hand
three times. The test is *not* "is the profile flat", which was the first thing
tried and is wrong: this routine is not flat, since each of the ten unrolled
probes sheds the callers that exited at the one before it, so the counts descend
steadily from 46,563 down to 17 at the rarest exit. That descent is healthy. The
invariant that actually separates work from waiting is that **the first byte of
a routine is its entry, so nothing in a loop-free routine can run more often
than that byte does.**

**`AND #$01FF` — nine bits of tile number, not ten.** There are 512 BG tiles and
512 attribute words, so this is the mask that matches the data.
`TILEMAP_INDEX_MASK`'s tenth bit is the odd one out, and `$80:AE14` can index
past the end of a 1 KB table where this one cannot.

**And a tile can block on its number alone.** Before the attribute word is
fetched at all:

```
LDA [$28],Y : AND #$01FF : CMP $00DC : BCC blocked
```

`$00DC` is written once, at load, from the level record's `+$26`. That is the
field `src/assets/level.h` has called `priority_below` since Phase 2, with the
note that it is *"a draw-time flag, not part of the expanded map"*: `$80:A47B`
forces BG priority on for every tile whose index is under it as the camera
streams them. It is also a collision threshold. **The tiles the game draws in
front of the player are exactly the tiles this routine will not let something
stand on**, and one 16-bit field in the level record does both jobs.

The caller is `$82:8F93`, and it is a placement search: it walks candidate
offsets out of tables at `$82:9035` and `$82:906F`, puts each through this test,
and writes the first one that passes to `$1E62`/`$1E64`. Refusing an overhead
tile is exactly what a placement search should do — a thing put down there
would be invisible.

### One caution, and it has a coverage site

Nine of the ten probes load through `Y`. The first does not:

```
$82:911F  LDA [$28]        ; no ,Y
```

So a point rejected by the very first tile returns with `Y` still holding the
caller's own argument, where every other rejection leaves the probe's map
offset. It is one addressing mode different from the nine below it and it
changes what the routine returns. `wide_floor_first` and `wide_floor_other` are
separate sites for that reason, and both are taken.

### Reached only on level 25

**46,560 of 46,563 calls on `movies/level25-lane.zmv`, 0 diverged, all five new
sites taken, first run.** The three not checked are interrupts landing mid-call,
which the harness excludes by design.

Across the corpus it is 127,455 calls on five movies — and **every one of the
five is a level 25 movie**. Nothing on levels 1, 5, 9, 13, 17, 21, 29, 33, 37,
41, 45, 49 or 53 enters it at all. Whatever `$82:8F93` is placing exists only
there, which is worth writing down for the round that eventually asks what
`$82:8F93` is: the answer will be something level 25 has and the other thirteen
sampled levels do not.

It also means the cycle budget is a level-25 average rather than a game-wide
one, and at **3,167** it is the most expensive leaf in the registry — 634 when
the first tile fails the priority test, 3,268 when all ten probes come back
clear. Because the loop is unrolled, that ceiling is a straight line and not an
iteration count.

### `run` proves less than this document has been claiming

Checking the new routine under substitution turned up something that is not
about the new routine.

Every round's `run` check, for as long as there has been one, has been
`movies/level1.zmv` — and the sentence *"identical at all 2,389 compared
scheduler passes"* has appeared in this document a dozen times. It is true.
`level1.zmv` is also 2,400 frames long and gives most routines a few hundred
calls. `movies/level9-weapons.zmv` at 9,000 frames is identical at all 8,989
passes too, so length alone is not the issue.

`movies/level25-lane.zmv` is a different answer, and it gives the same answer
for **every routine tried, including ones that shipped rounds ago**:

| substituted | passes differing | worst |
| --- | --- | --- |
| `-r none` (control) | **0 of 9,389** | — |
| `terrain_blocked` (two rounds old) | 5,935 | **7,135 bytes** |
| `terrain_blocked_wide` (this round) | 6,923 | 25 bytes |
| `terrain_out_of_bounds` | 5,898 | 41 bytes |
| `step_propose` | 5,898 | 39 bytes |
| `step_tether_blocked` | 5,898 | 38 bytes |

It starts small and grows. `terrain_blocked` on the same movie differs by **2
bytes** at 2,000 frames and at 3,000, by 4 at 5,000, and by 7,135 at 9,400 — and
by then `$7E:0016`, the NMI frame counter itself, reads `$5B` on one side and
`$6A` on the other, so the two cores are running different frames and every
byte after that is noise rather than evidence.

The two bytes it starts with are `$7E:0F04` and `$7E:0F05`, and the harness
names them itself: *"dead: a push the port never made, or declared scratch."*

**It is not the cycle budget**, which was the first guess and the wrong one.
Changing `terrain_blocked_wide`'s budget by 5x — 3,167 down to 634 — did not
move the onset by a single pass. Correcting `terrain_blocked`'s by the 2% its
own measurement on this movie suggests, 1,591 to 1,623, changed the blast radius
from 7,135 bytes to 7,126.

> **The section below is kept as it was written, and it is wrong in its
> conclusions.** Both "causes" it separates turned out to be one measurement
> mistake and one thing that is not a cause at all; the next section is the
> resolved account. It stays because the way it is wrong is the useful part —
> every experiment in it is sound, and every one of them measures the wrong
> number.

### There are two causes, and one of them is now certain

**Cause A: the pushes a routine abandons.** `terrain_out_of_bounds` declares
`stack_bytes = 0` because it pushes nothing, and it is clean at 2,000 frames
where `terrain_blocked` — four declared bytes — is already differing. Under
`verify` those bytes are waived by construction and the waiver is sound, because
the ROM runs too and re-establishes them. Under `run` nothing does.

That was a hypothesis, so it was tested rather than filed. `$80:AE14` opens
`PHD` then `PHA` of `X - 9`, and `CosimRegs::s` gives a shim the entry stack
pointer, so the shim can do exactly what the ROM does:

```c
uint16_t s = in->s, pushed = (uint16_t)(in->x - 9);
wram_w8(w, s,     (uint8_t)(in->d >> 8));   // PHD
wram_w8(w, s - 1, (uint8_t)in->d);
wram_w8(w, s - 2, (uint8_t)(pushed >> 8));  // PHA
wram_w8(w, s - 3, (uint8_t)pushed);
```

**It works.** `terrain_blocked`'s first difference moves from pass 1466 to pass
3496, which is to say its routine-specific early divergence disappears entirely
and it falls in with the routines that push nothing. So Cause A is real, it is
understood, and the repair is four lines.

**Cause B is what is left, and it is not routine-specific.** Every substituted
routine — including the ones that push nothing at all — first differs at pass
~3,500 on this movie, and it looks the same each time: `$7E:0004` and a few
bytes around `$7E:0FB3`, marked `*** unexplained ***` rather than dead. From
there it grows, and by pass 8,356 `$7E:0016` differs, which is the NMI frame
counter, after which the two cores are running different frames and nothing
downstream is evidence of anything.

Cause B is **not** the cycle budget and **not** the pushes; both were tested and
both are excluded above.

**What none of this puts in question is the port's arithmetic.** `verify`
compares all 128 KB after every single call with the ROM's own result standing
beside it, and it passes 5,244,269 times across 42 movies.

The Cause A repair is **not** applied. Doing it in one shim of fifty-six makes
the registry inconsistent, and the principled version belongs in
`native_return`, which already knows `stack_bytes` but not what values to write
— so it wants a per-routine description of the pushes rather than a count. That
is a harness change, and it is worth making only once Cause B is understood,
because otherwise it buys a cleaner first-difference report and nothing else.

### What it actually was: two bugs in the harness and one law of physics

`run` on `movies/level25-lane.zmv` now reports **3,485 of 3,485 compared passes,
no byte of live game state differing, with all fifty-six routines substituted**.
Nothing in `src/port/` changed to get there. Three things were wrong, and none
of them was what the section above concluded.

#### 1. The stack region was missing sixteen of the twenty-four stacks

`accounted_for()` waived `$7E:1000-$7E:12FF` as "the stacks", a constant copied
out of `docs/wram-map.md`, which said *"`$7E:1120-$7E:114F`, 48 B, active thread
stacks"*. That note came from a trace, and a trace only sees the slots the run
spawned: it had seen **one** stack and described it as all of them.

`$80:830E` is a 24-entry table of initial stack pointers and settles it in one
line of Python — 24 values 48 bytes apart, so the block is `$7E:0CF6-$7E:1175`.
**Sixteen of the twenty-four stacks are below `$1000`**, so every dead byte in
them was being reported as an unexplained divergence. That is the whole of
"Cause B": `$7E:0FB3`-`$0FB5` and `$7E:0F04`-`$0F05` are stack residue in slots
19 and 17, exactly as inert as the residue at `$1021` the harness was already
waiving, and the only difference between them was a wrong constant.

`cosim_init()` now reads the table out of the ROM. The low end is clamped to
`$0D00` because the block's last ten bytes overlap slot 11's direct page, which
is live — see `docs/wram-map.md`, which no longer claims otherwise.

#### 2. `nmi_saved_sp` is not state

That leaves `$7E:0004`, and it is the shortest-lived word in WRAM: `$80:819C`
writes it with `TSC : STA $04` and `$80:81EB` reads it back with `LDA $04 : TCS`
seventy-nine bytes later, and **nothing else in the game reads it**. (The
disassembler labels `$80:8295`, `$80:82C5` and `$80:8816` with the same name;
all three run with `D` on a thread's page, so they are page+4. The direct-page
trap, again.) Between two NMIs it is not live state, it is a record of where the
last NMI happened to land.

It is waived — and *counted*, because it is the only visible measure of the
cycle budget being an estimate. `run` now reports how many passes it differed on
and by how much stack, and the answer is that it is rare and small:
`terrain_out_of_bounds` 6 passes of 9,389, `terrain_blocked` 2 of 6,057, both by
at most 7 bytes of stack — one `JSL` deep.

#### 3. The two cores really do run different timelines, and nothing can fix it

With those two out of the way, one thing was left, and it is not a bug. On
`level25-lane` at pass 3,496 the harness reported:

```
The two timelines part at pass 3496: stock is on frame 2685 and
native on frame 2684, so one of them overran vblank on a pass the
other did not.
```

A scheduler pass is *nearly* a frame. A pass whose work overruns vblank takes
two NMIs instead of one, and on a level heavy enough to fill most of a frame,
whether that happens is decided by a few hundred cycles. A substituted core
spends a different number of cycles doing the same work — **that is what
substitution is** — so eventually one side overruns a pass the other does not,
and from then on "pass 6068" means frame 6068 on one side and 6069 on the other.
Everything the old report called a divergence after that point was frame *N*
compared against frame *N+1*.

Realigning does not work, and the failed attempt is the proof: syncing on `$16`
costs the lagging side an extra scheduler pass, which puts `sched_tick` out by
one instead. The two clocks genuinely disagree, because one core really did run
a pass in two frames and the other in one. They are not computing different
answers; they are running different timelines of the same game, the way a
console that dropped a frame differs from one that did not.

So `run` stops comparing there and says so, and keeps both cores running for the
call counts and coverage. `-r none` is still clean at all 9,389 passes, which is
what makes the parting attributable to substitution rather than to the harness.

#### What the earlier experiments actually measured

Both exclusions in the section above are sound experiments aimed at the wrong
number. They measured **the first difference**, which is dead stack — inert,
routine-specific, and genuinely independent of everything. What decides the
outcome is **the first pass the timelines part**, and re-run against that:

| `terrain_blocked` `.cycles` | first difference | timelines part |
| --- | --- | --- |
| 1,400 | pass 1,466 | pass 6,068 |
| 1,591 (declared) | pass 1,466 | pass 6,068 |
| 1,900 | pass 1,466 | pass 6,068 |
| 3,000 | pass 1,466 | pass 6,068 |
| 1,591 + the four-line push repair | pass **3,496** | pass 6,068 |

The budget does not move it. **The push repair does not move it either** — it
moves the first difference exactly as reported above, and changes nothing about
where the run ends. So Cause A is not a cause; it is cosmetic, and the
"principled repair belongs in `native_return`" plan is withdrawn rather than
deferred. There is nothing to repair.

The standing lesson is the same one `$80:A937` taught and is worth stating in
its general form: **an experiment that does not move the number is only evidence
if that number is the one that matters.** Three rounds of "it is not the cycle
budget" rested on a metric that could not have moved.

#### Where `run` stands now

| movie | frames | compared | live state differing |
| --- | --- | --- | --- |
| `level1.zmv` | 2,400 | 2,389 of 2,389 | none |
| `level9-weapons.zmv` | 9,000 | 8,989 of 8,989 | none |
| `level25-boss.zmv` | 7,600 | 7,589 of 7,589 | none |
| `level29-fighting.zmv` | 6,000 | 5,989 of 5,989 | none |
| `level25-lane.zmv` | 9,400 | 3,485, then the timelines part | none |

All fifty-six routines substituted, all of WRAM compared every pass. The only
movie that parts is the one on the heaviest level, which is the expected place
for it and the reason it was the movie that found all of this.

The frontend agrees, which is the check that matters most because it involves no
harness at all: `zamn.exe` against `--stock` produces a byte-identical final
framebuffer on `level1`, `level1-2p`, `level29-fighting` and `level45-race` at
6,000 frames — and **not** on `level25-lane` at 9,400, where by then the two
builds are drawing different moments of the same game. That is the same event
`run` names at pass 3,496, arrived at independently.

### And the check from the last round earned its keep immediately

Registering it moved the native share from 55.4% to **57.5%**. The routine is
6,595,659 instructions; the native total moved by 6,595,659. Nothing came along
for the ride, which is what the last round's `$80:A937` failure was supposed to
teach the next one to verify — and did, on the first opportunity.

**57.5% strict, 70.9% call-weighted; 64.5% and 79.6% with the waiting out.**

## The two LZSS leaves, and the first routines registered without their caller

`$80:CD20 lzss_decompress` has been written since the round that made
`CosimRegs` carry a stack pointer, and deliberately unregistered ever since: one
call is about seven frames long, so an NMI always lands inside it and there is
no instant at which the two sides' WRAM is comparable. That argument is about
the *body*. It says nothing about its two leaves, and they turn out to be the
opposite shape in every respect that matters.

| | `$80:CD20` | `$80:CDDA` / `$80:CDEB` |
| --- | --- | --- |
| instructions per call | ~440,000 | 8 and 5 |
| calls in the corpus | 50 | 373,188 and 697,920 |
| interrupt lands inside | always | 78 calls in 107,678 |
| share of all work | 5.5% | **2.6%** |

So they are registered on their own, which is the first time a routine has gone
in without its caller. That is sound because interception is per call site: the
ROM runs `$80:CD20` and the port answers each `JSR` out of it, exactly as it
answers a `JSR` out of any unported routine. The six call sites in the trace are
all inside that one body, so neither needs a guard.

**Both pass on the first run** — 107,678 calls, 0 diverged — and the one branch
either of them has, the `SEC` exit at the end of a stream, is taken. Neither
ends where it looks like it does: `$80:CDDA` closes on `INC $28` and `$80:CDEB`
on `INC $2C`, so in both cases N and Z describe *the pointer the routine just
advanced* rather than the byte it just handled. The `PLD` trap in a third
costume, and by now the thing to check first rather than last.

### Registering them broke the framebuffer test on every movie

`zamn.exe` against `--stock` had been byte-identical on `level1`, `level1-2p`,
`level29-fighting` and `level45-race`. With these two substituted, all four
differ — and `run` on `level1` parts the timelines at pass 187, with stock on
frame 1,049 and native on 1,052.

That is the previous section's law arriving with a much bigger lever.
`cycles` is one constant standing in for a distribution — `lzss_read_byte`
really costs anywhere from 98 to 298 — and a million calls packed inside a
single multi-frame decompression give the error no chance to cancel. A level
load lands three frames off, and a `.zmv` applies its inputs by frame index, so
everything after that is a different game.

**No budget can fix it, and the failed attempt is the argument.** The mean is
the mean by construction: if some constant could make the totals agree, the
measured mean already would. What is left over is variance, and a constant has
none. Trying it anyway — both budgets down by the cost of the return
instruction, on the suspicion that `native_return` burns the budget and *then*
executes the `RTS` the measurement already included — moved nothing.

So both are **`verify_only`**: checked on every call, all 128 KB, and never
substituted. That is the flag's second reason and quite a different one from
`apu_send`'s — that routine cannot be substituted because it has to *wait*,
these two because they cannot keep *time* — and `CosimRoutine::verify_only` now
carries both arguments. The framebuffer check goes back to identical on all four
movies, which is the point: it is the only end-to-end evidence in the project
that involves no harness at all, and it is worth more than a substitution that
provably cannot work.

### ...and the metric inflated again, in exactly the documented way

Registering them moved the native total by **8,382,014** instructions where the
two routines execute **7,113,522**. The check the `$80:A937` round installed —
*port a routine and see whether the number moves by more than that routine is
worth* — caught it on its second real use.

The cause is the same quiet failure mode, and it is worth seeing twice.
`lzss_write_byte` is five instructions and ends at its `RTS` on `$80:CDF3`.
`$80:CDF4` begins a different routine, reached by a jump, and it opens
`JSR $D13A` and then `JSL thread_yield`. Attribution is by nearest preceding
entry, so **those call edges were credited to `lzss_write_byte`** — and the
moment a five-byte leaf became "ported", `$80:D13A`, `$80:D1EA`, `$80:D01B` and
their closures all looked like routines every caller of which was ported. 13
routines and 1.28% of the game moved into the native column for free.

One line in `JUMP_ENTRIES` fixes it. With the boundary declared on both sides of
the comparison, the total moves by **6,474,904**, and 697,920 × 5 + 373,188 × 8
is **6,475,104**. That is the whole of it, to within two hundred instructions of
attribution edge effects, and the subsumed count does not move at all.

It also restates the previous round's figures slightly, because that boundary
was missing then too: the call-weighted estimate was **70.2%**, not the 70.9%
recorded above, and 78.8% rather than 79.6% with the waits out. The strict
number, 57.5%, is unaffected — it counts the entries themselves, which the
boundary does not touch.

**59.5% strict, 71.0% call-weighted; 66.8% and 79.7% with the waiting out.**

## Starting up the camera chain from the bottom

`$80:A93F` is the top portable row on the work ranking — 1.6% over 146,000
calls, and `hotbytes.py` gives it a clean bill: every byte in it runs exactly
once per call, so all of that 1.6% is work. It is camera centring, and reading
it is quick:

```
BIT $26 : BVS out          ; render_flags bit 6 — nothing to do
LDA $D2 : BNE both         ; player A's record
LDA $D4 : BNE b_only       ; ...or only player B's
out: PLD : SEC : RTL
both:   the midpoint — LDA ($D2),Y : CLC : ADC ($D4),Y : LSR A
a_only: LDA ($D2),Y                        -> $1CB0 / $1CB2
LDA $1CB0 : SEC : SBC #$0080 : SEC : SBC $1B6A : TAX
BEQ  ...  ASL A : BCC  ->  JSL $80A70A  /  JSL $80A68B
LDA $1CB2 : SEC : SBC #$0084 : SEC : SBC $1B6C : TAX
BEQ  ...  ASL A : BCC  ->  JSL $80A789  /  JSL $80A816
```

The target is one player's position, the other's, or the midpoint of the two;
the deltas are against `$1B6A`/`$1B6C`, the live scroll; and each axis
dispatches into one of two tilemap scroll routines by the sign. Those move the
camera **one pixel** and push a new column or row into the VRAM queue every
eighth, which is why the game's camera drifts after the players rather than
snapping to them.

**And that is why it cannot be taken yet.** A substituted routine has to do
everything the ROM's does, and there is no way to call back into the ROM
half-way through, so `$80:A93F` needs all four scroll routines — which are only
0.20% between them, but `$80:A68B` alone calls five more (`$A401`, `$A54D`,
`$A588`, `$A5E5`, `$9E6D`). The alternative is a guard that serves the calls
needing no scroll at all, which is somewhere between 41% and 69% of them; that
would put the first entry in a decline census that has been empty for several
rounds, and would credit the ranking with 1.6% for a routine doing half of it.
Bottom-up is the honest order.

### $80:AD1C tilemap_tile_addr — routine fifty-nine, and the floor of that stack

Fifteen bytes, no calls, 42,167 of them, no loop:

```
TXA : ASL A : PHA          ; column x 2
TYA : ASL A : TAX
LDA $7E4328,X              ; the row's byte offset, out of the row table
CLC : ADC $01,S            ; ...plus the column
PLX : RTL
```

`$7E:4328` is `W_TILE_ROW_BASE`, which `port/terrain.h` has documented since the
first terrain routine: one word per tile row, so a row lookup is a table read
rather than a multiply. Everything in that header turns a *pixel* into a tile
and then does this; this is the same lookup for callers that already hold tile
coordinates, which is why it lives there rather than in a file of its own.

**Three registers come back and no two of them come from the same place.** A is
the `ADC`'s sum. X is a `PLX` of the column *already doubled* — not a restore,
and `$80:A5E5` depends on it. N and Z are that `PLX`'s, so they describe the
doubled column rather than the address; carry is the `ADC`'s and survives the
`PLX` untouched. Every routine in this registry that ends on a pull has had this
shape and it is now the first thing to check rather than the last.

**1,037 calls on `movies/level1.zmv`, 0 diverged, first run** — and 9,754 on
`level25-lane`, 6,410 on `level45-race`, 7,695 on `level1-2p`, all clean. The
declared `stack_bytes = 2` is the `PHA` it reads back through `$01,S`, and
`verify` measured exactly 2. `run` substituting it alone on `level25-lane` is
clean at all 9,389 passes with no timeline parting, and the frontend's four
framebuffers stay identical.

Registering it moved the native total by **463,837** instructions. `$80:AD1C`
executes 463,837. Exact, and the subsumed count did not move — the check passing
cleanly for once, on the round after it caught something.

**59.6% strict, 71.1% call-weighted; 67.0% and 79.9% with the waiting out.**

### $80:A5E5 tilemap_copy_column — routine sixty, and one step up

The next link, and the one that does the work when the camera has drifted far
enough to need new map on screen:

```
PHA : JSL $80AD1C : STA $50      ; source = the tile at (X, Y)...
LDA #$007F : STA $52             ; ...in bank $7F, where the map lives
LDA $01,S : TAX : LDY #$0000     ; the count back off the stack
loop:
  LDA [$50] : STA [$54],Y        ; one tile word, straight across
  AND #$01FF : CMP $DC : BCS +
  LDA #$2000 : ORA [$54],Y : STA [$54],Y
+ LDA $50 : CLC : ADC $B2 : STA $50   ; down one row
  INY : INY : DEX : BNE loop
PLA : ASL A : CLC : ADC $54 : STA $54 : RTS
```

The source steps by `W_TILEMAP_ROW_BYTES` and the destination by two, which is
what makes it a *column* rather than a row: a vertical strip of the expanded map
laid out flat. The closing add advances the caller's destination pointer, so
successive calls append.

**`$DC` turns up doing its third job, and this is the one it was named for.**
`CMP $DC : BCS` forces bit 13 — the PPU's background priority bit — on every
tile whose nine-bit index is below `W_TILE_PRIORITY_BELOW`. `src/assets/level.h`
had that field as a draw-time flag since Phase 2; `$82:90F7` showed earlier
today that it is *also* a collision threshold; and here is the draw-time half in
the flesh, three rounds after the note was written. One word in the level record
decides both, which is exactly why the tiles drawn in front of the player are
the tiles nothing is allowed to stand on — it is not two rules that happen to
agree, it is one number read twice.

Two details worth transcribing rather than tidying. The loop is `DEX : BNE`, a
do-while, so **a count of zero means 65,536 iterations and not none**; the port
reproduces that, because a guard there would be the port disagreeing with the
ROM about something no caller does. And its flags, for once, are the ones a
reader would guess: `ADC $54` is the last thing before the `RTS`, so N, Z and C
all describe the pointer it returns. The chain's other two links both end on a
pull, and this one not doing so is worth noticing precisely because it breaks
the pattern.

**303 calls on `movies/level45-race.zmv`, 0 diverged, both new sites taken,
first run** — and 1,285 on `level25-lane`, 90 on `level1-2p`, 30 on `level1`,
all clean. `stack_bytes` is 7 and that number was predicted before it was
measured: its own `PHA` is 2, the `JSL` is 3, and `$80:AD1C`'s `PHA` under it is
2 more.

Its cycle spread is 1,526..14,250, which is unlike every other spread in this
registry: not the bus and not a branch, but the count. The loop body is fixed,
so the cost is linear in how many tiles the caller asked for, and the mean is a
mean over strip lengths rather than over code paths — the same shape of number
that made the LZSS leaves unsubstitutable, at a thousandth of the call volume.
At 303 calls it is harmless, and the checks agree: `run` substituting it alone
on `level45-race` is clean at all 5,989 passes, `run` with everything on
`level1` at all 2,389, and the frontend's four framebuffers stay identical.

Registering it moved the native total by **1,115,377**, and `$80:A5E5` executes
1,115,377. Exact, with the subsumed count unmoved — twice in a row now.

**60.0% strict, 71.5% call-weighted; 67.4% and 80.3% with the waiting out.**

What is left of the camera chain is the four scroll routines that call this one,
and `$80:A401`, `$80:A54D`, `$80:A588` and `$80:9E6D` under them. `$80:A61D`
sits immediately after this routine and is its horizontal twin — same loop, same
priority rule, `LDY #$0040` counting down instead of a caller's count up — so it
is the obvious next one and it will not need reading twice.

### $80:A401 tilemap_buffer_alloc — routine sixty-one, and a guard used as a statement

The arena `$80:A5E5` fills. Twenty-one bytes:

```
PHA
retry: LDA $CC : SEC : SBC $01,S : BCC retry   ; is there room?
STA $CC
LDA $CA : TAY : CLC : ADC $01,S : STA $CA      ; bump it
PLX : TYA : RTS
```

A bump allocator over `$7E:4B28`, which `$80:A64F` starts at `$0900` bytes.
Neither `$CA` nor `$CC` had a name before this — not in `zamn.sym`, not in
`docs/wram-map.md` — so they are now `W_TILEMAP_ARENA_NEXT` and
`W_TILEMAP_ARENA_LEFT`.

**That `BCC` branches back to a reload of the same unchanged `$CC`, so it is a
spin and not a retry.** It waits for somebody else to hand the arena back. And
in ten traces across thirteen levels it is never taken once: `hotbytes.py` gives
*every* byte in the routine exactly 3,977 executions against 3,977 calls, which
is the entry-count invariant saying the loop has no back-edge traffic at all.

That is an awkward branch to own. Implementing the spin faithfully in C would be
an infinite loop, because nothing inside the port can change `$CC`. Ignoring it
would be the port quietly disagreeing with the ROM. A coverage site would be
honest but expensive — it could never be taken, and `coverage.h` says in as many
words that a site no corpus can reach dilutes the number the file exists to
keep.

**So it is a guard.** `tilemap_buffer_alloc_supported()` declines a call that
would have to wait, and the decline census counts it if it ever happens. A guard
that never fires costs nothing, keeps the census empty, and states the condition
in the one place a reader will look for it. The `decl.` column reads **0** on
every movie tried, which is the same fact the profile gives from the other
direction.

**227 calls on `movies/level45-race.zmv`, 0 diverged, 0 declined, first run** —
and 864 on `level25-lane`, 120 on `level1-2p`, 41 on `level1`. Its cycle spread
is 342..382, forty cycles of bus and nothing else; the spin would have put the
ceiling in the thousands. `run` substituting it alone is clean at all 5,989
passes and with everything at all 2,389, and the four framebuffers hold.

Registering it moved the native total by **55,678**, and its span executes
55,678. **Three routines in a row now where that check has come out exact**, on
the round after it caught an inflation — which is the useful pattern: the check
is worth having precisely because it is usually boring.

**60.0% strict, 71.5% call-weighted; 67.4% and 80.3% with the waiting out.**

### Where the camera chain stands

| routine | share | calls | state |
| --- | --- | --- | --- |
| `$80:A93F` camera_follow | 1.6% | 146,000 | needs all four below |
| `$80:A68B` / `$A70A` scroll X | 0.11% | 44,561 | needs `$A54D`, `$A588`, `$9E6D` |
| `$80:A789` / `$A816` scroll Y | 0.09% | 40,218 | needs `$80:A61D` |
| `$80:A61D` copy row | 0.14% | 1,535 | needs nothing further |
| `$80:A5E5` copy column | 0.35% | 4,452 | **done** |
| `$80:A401` arena alloc | 0.02% | 3,977 | **done** |
| `$80:AD1C` tile address | 0.15% | 42,167 | **done** |
| `$80:A54D`, `$A588`, `$9E6D` | 0.06% | | all loop-free, unread |

`$80:A61D` is `$80:A5E5`'s horizontal twin — the same loop, the same `$DC`
priority rule, `LDY #$0040` counting down instead of a caller's count up — and
its only outstanding dependency was `$80:A401`, which is now in. It is the next
one, and it will not need reading twice.

### The rest of the leaf layer, and the registry outgrowing its mask

Four more, and with them everything the four scroll routines stand on is in.

**`$80:A61D tilemap_copy_row`** is `$80:A5E5`'s twin: same `$DC` rule, same
nine-bit mask, `LDY #$0040` counting down by twos for 33 tiles — a number
`hotbytes.py` had already given as *33.0x the entry*, the loop count read off
the profile before the routine was read at all. Two things differ and both
matter. It walks a row, so **one index does both ends** where the column version
needed a separate source stepped by the row stride. And it buys its own 66-byte
strip from the arena instead of being handed one, which is why it carries the
allocator's guard as well.

Its outputs are the least summary-like in the registry: A and carry are
*whatever the thirty-third tile happened to be*, N and Z belong to a `DEY` that
has already run off the end to `$FFFE`, and X belongs to the allocator's `PLX`
three instructions before the loop started. Four registers, four unrelated
origins, and nothing it returns describes its work. One subtlety was nearly
transcribed wrong: on the priority path `LDA #$2000 : ORA [$54],Y` ORs against
the word *already stored*, which is the whole tile — so the `AND #$01FF` is gone
from A again by the time it returns.

**23 calls on `level45-race`, 0 diverged, first run**, and 219 on `level25-lane`.
The two new sites took 238 and 521 hits, which sum to 759 = 23 × 33: the loop
count confirming itself from a third direction.

**`$80:A54D camera_window_update`** recomputes the six tile-window words from
the two the camera actually moves — twenty-seven instructions, no branches. The
masks are `$3F` and `$1F`, which are the PPU's 64×32 tilemap and not the level's
size, and the 32 and 28 are a screen of tiles each way. **`$80:A588
camera_split_y`** is six instructions saying where that 32-row tilemap wraps
relative to the cursor, which is how one column copy becomes two across the
seam. **`$80:9E6D vram_queue_request`** is seven bytes and three exits, and it
is the one worth pausing on: it opens `BIT $26`, and `BIT` against memory sets N
from **bit 15 of the operand** but Z from **A AND the operand** — so the Z it
returns on that exit is a fact about *the caller's accumulator*, which the
routine never loads and has no other use for. A shim deriving Z from anything
the routine computes would be wrong on every call taking that path and right by
accident on the rest.

All three pass first run: 227, 203 and 227 calls, 0 diverged.

#### The mask that had been quietly too narrow, and the one that wasn't

Registering the sixty-fifth routine stopped the harness dead:

```
error: 65 routines registered but COSIM_MAX_ROUTINES is 64 — widen
       Cosim::enabled and Cosim::stats together, in cosim.h.
```

That assert exists because the registry outgrew a **32**-bit mask once and the
symptom was `verify` reporting zero calls checked on every movie — a harness
that had stopped measuring rather than a port that had stopped working. It did
its job: `Cosim::enabled` is now a small bitset rather than a machine word, so
the next raise is one line.

And widening it turned up the same bug the assert was written for, still live
somewhere else. `main_sdl.c` counted its own enabled routines with

```c
for (uint32_t m = selected; m; m >>= 1) routine_count += (int)(m & 1);
```

— a `uint32_t` cursor over a 64-bit mask. The frontend has been undercounting
the routines it has had since the registry passed 32, in the line it prints at
startup. Nothing depended on the number, which is exactly why nobody noticed.

#### Five exact checks, and one of them nearly wasn't

The native total moved by **154,674** for the three. The prediction was 184,357,
and the prediction was wrong: `$80:A588`'s share had been read off a
`hotbytes.py` span of `$A588..$A5E5`, which swallows `sub_80A599` entirely. Its
real span is `$A588..$A599` and 19,456 instructions, and

    107,379 + 19,456 + 27,839 = 154,674

exactly. So the nearest-preceding-entry hazard this document has now caught
twice in the tooling caught it a third time **in the arithmetic used to check
the tooling**, which is a good argument for the check being cheap enough to run
every time rather than clever enough to skip.

Running totals for the five routines this chain has added: 463,837 / 1,115,377 /
55,678 / 450,376 / 154,674, against measured spans of exactly the same. The
subsumed count has not moved once.

**60.2% strict, 71.7% call-weighted; 67.6% and 80.5% with the waiting out.**

#### And one of the new sites came back unreachable

Of the five coverage sites those four routines added, four were taken across the
corpus and one — `request_empty`, the `LDA $CE : BEQ` exit — was not. That is
the prompt to go and look rather than to shrug, and looking settles it: all four
of `$80:9E6D`'s call sites store a **nonzero** `$CE` in the instruction
immediately before the `JSR`. `$80:A540` is `TXA : CLC : ADC #$0004 : STA $CE`;
`$80:A70x`, `$80:A810` and `$80:A89E` are each `INX : INX : STX $CE`. The queue
is never empty when this routine is asked whether it is.

So the exit is unreachable from anywhere the game calls it, and the site came
out on the rule this file states about itself: **a site no corpus can reach
dilutes the number it exists to keep.** The *code* stays — it is three correct
lines and the ROM has them — and only the claim goes. That is the second
unreachable branch this chain has produced and the second different answer to
it: the allocator's spin became a guard because the port could not implement it,
and this one becomes a comment because the port implements it perfectly well and
nothing will ever call it.

What is left of the camera is the four scroll routines — `$80:A68B`, `$A70A`,
`$A789`, `$A816`, 0.20% between them and every callee now ported — and then
`$80:A93F` itself, which is the 1.6% the whole exercise was for.

## The four scroll routines, and two routines that were never in the listing

Routines sixty-six to seventy, and the layer `$80:A93F` has been waiting on
since this chain started. Every one of them passed first run.

| routine | share | calls | cycles |
| --- | --- | --- | --- |
| `$80:A599` camera_split_x | 0.01% | 1,535 | 452..522 |
| `$80:A68B` scroll left | 0.06% | 30,150 | 94..17,032 |
| `$80:A70A` scroll right | 0.05% | 14,411 | 116..18,476 |
| `$80:A789` scroll down | 0.05% | 25,608 | 116..14,578 |
| `$80:A816` scroll up | 0.04% | 14,610 | 250..15,032 |

All four scrollers have the same three-part shape: refuse if the camera is
already against that edge of the map; otherwise move it **one pixel** and the
sub-tile remainder with it; and every eighth pixel — when the remainder wraps —
step the tilemap cursor, copy the strip of map that has just come into view, and
put it on the VRAM queue.

### Two of the four were never disassembled

`$80:A70A` and `$80:A816` are not in `analysis/bank_80.asm` as code. They have
no label and no mnemonics, only `.db` runs — nothing ever proved those bytes
were instructions. Decoding them by hand is what shows why they were worth the
trouble: **each is its partner byte for byte with six substitutions.**

| | `$80:A68B` / `$A816` | `$80:A70A` / `$A789` |
| --- | --- | --- |
| direction | `DEC A`, and `BEQ` against zero | `INC A`, and `CMP $B8` / `$B6` |
| limit | nothing to compare against | `$B8` across, `$B6` down |
| boundary | `AND #$0007 : CMP #$0007` | `AND #$0007` alone |
| cursor | stepped down, then masked | stepped up |
| edge | the near tile-window word | the far one |
| destination | the cursor itself | the cursor's end |

The last two are one fact twice: a strip appears at whichever edge the camera is
moving towards, written at whichever end of the tilemap cursor's own wrap that
edge currently maps to. `src/port/camera.c` therefore holds one X scroller and
one Y scroller and a table of exactly those six differences, which is the
shortest way to write a mirror down such that the mirroring is *checkable*
rather than something a reader has to diff by eye.

The two boundary tests are the pair worth reading twice. Going forwards a pixel
crosses a tile boundary when the low three bits come out zero; going backwards,
when they come out seven. The ROM asks the first with `AND #$0007 : BNE` and the
second with `AND #$0007 : CMP #$0007 : BNE`, so **the two exits publish
completely different flags for the same event** — N clear and carry left over
from the limit compare on one, N set and carry clear on the other.

### `$80:A599` is `$80:A588` with four times the work, and the tilemap's shape is why

`camera_split_y` hands back two row counts and lets its caller decide what to do
with them. `camera_split_x` does the deciding itself, because a background
tilemap is 64 columns wide but the PPU stores it as **two 32x32 screens `$400`
words apart**. A row crossing the seam is therefore not one transfer with a
wrapped address; it is two transfers to unrelated places. So the routine hands
back two complete descriptions — `$5C`/`$5E`/`$60` and `$62`/`$64`/`$66`, each a
source offset, a destination and a length — and its two branches are the same
pair of runs written in opposite orders.

Six direct-page words, written by whichever splitter ran, and the two splitters
disagree about what they mean. `$80:A588` writes `$5C` and `$5E` as tile counts
and touches nothing else; `$80:A599` writes all six as transfer descriptions;
and `$80:A68B` and `$A70A` use `$60` for a third thing again, the strip they
just bought off the allocator — which they can only do *because* the Y splitter
they called does not write it. It reads as an aliasing bug until one notices
that no scroll routine ever calls both splitters.

It would be easy to publish two different flag expressions for the two branches,
to match the two store orders. They are the same expression: both close on `LDA
#$0042 : SEC : SBC <the run this branch measured>`, so A, N, Z and carry agree
even though the word underneath them does not.

### The X pair and the Y pair split their work in opposite shapes

A column is 32 tiles and the tilemap is 32 rows tall, so a column *always* wraps
somewhere. The X scrollers therefore call `$80:A5E5` **twice** — once for the
part below the wrap, once for the part above — into one buffer they allocated
themselves, and queue **one** transfer.

A row is 33 tiles across a tilemap 64 wide split into two screens. The Y
scrollers therefore copy the row **once**, with `$80:A61D` doing its own
allocating, and queue **two** transfers at unrelated addresses.

Same job, opposite shapes, and two details of the arithmetic that a natural
transcription gets wrong:

**`ADC $5E : ADC $1B7E` has no `CLC` between the two adds.** Both destinations in
a Y scroll are built that way, so the base add carries the offset add's out.
Neither can overflow with a `$7800` base and a table that stops at `$3E0` — but
the ROM chains them, and a port that quietly did not would be right until the day
it mattered.

**The unconditional column copy cannot be the 65,536-iteration case.** The
conditional one is guarded by `LDA $5E : BEQ` because `cursor & 31` really can be
zero; the other takes `32 - (cursor & 31)`, which is 1..32 and never zero, so
`$80:A5E5`'s `DEX : BNE` do-while is safe there without a test and the ROM does
not write one.

### The first routines in the registry that take carry as an *input*

`$80:A68B` and `$A816` open `LDA $1B6A : BEQ out`, with no `CMP` anywhere on that
path. A camera already against the near edge of the map therefore returns **the
caller's carry, untouched** — an output the routine never computed. So all four
take carry in, and the forward pair are handed it and ignore it, because a shim
passing `false` would be asserting something about the caller rather than about
the routine.

On the exits that do reach a strip, the registers come from three places again:
A, N and Z from `$80:9E6D`, the last call any of them makes; X from `STX $CE`, so
it is the VRAM queue's new length; Y from the `TAY` that indexed the destination
table, so it is the tilemap cursor doubled; and carry from the `ADC $1B7E` that
built the last destination.

### A 180x cycle spread, and why it did not behave like the LZSS leaves

`camera_scroll_left` costs anywhere from **94 cycles to 17,032**. That is the
widest spread in the registry by a distance — the next worst is
`tilemap_copy_column`'s 9x — and three exits of wildly different lengths are only
half of it. The other half is that *which* exit a movie takes is a property of
the movie: `movies/level49.zmv` holds the camera against the left edge for its
whole length and contributes **13,560 calls at 94 cycles each**, which drags the
call-weighted mean down to 394, a value no single call has ever cost.

That is the shape of the argument that made the two LZSS leaves `verify_only`,
and it was reasonable to expect the same answer here. It is not the same answer,
and the difference is worth stating because it is what the flag actually turns
on. The LZSS leaves are called a million times **inside one multi-frame
decompression**, so their errors accumulate against a single deadline and a level
load finishes three frames off. These are called a few thousand times across a
whole movie, spread over thousands of independent frames, each one ending at a
`WAI` that resynchronises the machine. The errors have nowhere to accumulate.

`run` says so directly: substituting all five on `movies/level45-race.zmv` gives
no live-state difference at any of 3,989 compared passes and **no timeline
parting at all**, and the frontend's four framebuffers stay byte-identical
against `--stock`. A spread is not the problem; a spread with a deadline is.

### The standing check, exact for the sixth time

The native total moved from **192,146,816 to 192,833,966** — 687,150
instructions, against measured spans of

    202,382 + 160,618 + 155,691 + 138,776 + 29,683 = 687,150

exactly. The subsumed count did not move (175 routines) and neither did the
both-sides count (40), which is the expected answer here rather than a lucky one:
these five have exactly one caller between them, `$80:A93F`, and it is not
ported, so there was no subsumption to collect.

**60.4% strict, 71.9% call-weighted; 67.8% and 80.7% with the waiting out.**

### Where the camera chain stands now

Everything under `$80:A93F` is in. What is left is `$80:A93F` itself — 1.6% over
146,000 calls, no loop, and the whole reason the chain was read bottom-up.

## $80:A93F camera_follow — the top of the chain, and the 1.6% it was all for

Routine seventy-one, and the one eleven routines were read bottom-up to reach.
It passed first run: **5,280 calls on `level45-race`, 0 diverged**, and 94,784
across seven movies with nothing to fix. Across the whole corpus the totals are
**10,480,450 calls on 42 movies, 0 diverged**, branch coverage **262 of 340**
with 78 untaken, and the decline census still empty.

It is short, and almost all of it is deciding what to aim at:

```
PHD : LDA #$0000 : TCD
BIT $26 : BVS out                 ; bit 14 — the camera is held
LDA $D2 : BNE both_or_a
LDA $D4 : BNE only_b              ; ...and neither player is on the board
out: PLD : SEC : RTL
both_or_a: LDA $D4 : BEQ only_a
  $1CB0 = (($D2).x + ($D4).x) >> 1 ; $1CB2 likewise for Y
only_a:  $1CB0 = ($D2).x ; $1CB2 = ($D2).y
only_b:  $1CB0 = ($D4).x ; $1CB2 = ($D4).y
LDA $1CB0 : SEC : SBC #$0080 : SEC : SBC $1B6A : TAX : BEQ +
  ASL A : BCC right : JSL $80A68B : BRA +
  right: JSL $80A70A
+ LDA $1CB2 : SEC : SBC #$0084 : SEC : SBC $1B6C : TAX : BEQ +
  ASL A : BCC down : JSL $80A816 : BRA +
  down: JSL $80A789
+ PLD : SEC : RTL
```

### One pixel, whatever the distance

`ASL A : BCC` is the whole of the movement decision. The shift's *result* is
discarded — nothing reads A again before the `JSL` — and only its carry, which
is bit 15 of the delta, is used. So the camera is told which way to go and never
how far, and it moves exactly one pixel per call. That is the mechanism behind
something anyone who has played the game has seen: the view drifts after the
players rather than snapping to them, and it never catches up while they are
running.

The delta is computed in full first, which makes the discarding look like a
mistake until you notice the routine is called 146,000 times over ten movies —
once per frame per axis, forever. One pixel a frame *is* the camera speed.

### Three details worth transcribing carefully

**The midpoint truncates.** `LDA ($D2),Y : CLC : ADC ($D4),Y : LSR A` shifts A
alone, so a sum over `$FFFF` loses its carry instead of shifting it back in. No
pair of player positions can reach that on any real map; the port wraps anyway,
because the day one can is not the day to discover the port disagreed.

**Two `SEC`s in a row are not decoration.** `SBC #$0080` can borrow, so the
second subtract genuinely needs carry set again before it runs.

**`$0084` is not the centre of the screen.** 128 across is; 132 down is eight
lines below the middle of a 224-line screen, because the playfield sits under a
status bar and this is where its centre actually falls.

### A `PHD` routine, so its flags describe the caller

All four exits are `PLD : SEC : RTL`. Carry is therefore set on every one of
them and says nothing at all; N and Z come from the `PLD` and describe the
*caller's* direct page, which is the pattern `step_tether_blocked` established.

The three register outputs are worth stating because two of them are leftovers.
A is the Y delta, or zero on the two exits that never compute one. X is that
same delta unless a scroll routine overwrote it. And Y is `$0006` — the index
the record read left behind — unless a scroll routine set it, which means the
value that comes back is `ACTOR_Y` for no reason connected to what the routine
did.

### The guard has to ask about both strips at once

The four scroll routines each carry the arena guard, and this one calls up to
two of them per call — one X, one Y. The second buys its strip out of what the
first left, so a guard that asked each question separately would answer yes
twice to a call that can only afford one. `camera_follow_supported()` works out
which scroll routines this call would reach, sums what they would ask for, and
puts the total to the allocator once. Never fired, like all the others.

### Bit 14 of `$26`, and grepping a disassembly for something that is not in it

`BIT $26 : BVS` is the routine's first instruction after the direct page, and
`$80:9E7B vram_queue_flush` opens with **exactly the same test**. So bit 14 is a
deliberate freeze: with it set, the camera does not move and the VRAM queue does
not drain. It is not an accident of two routines sharing a word.

Searching `analysis/bank_80.asm` through `bank_83.asm` for a writer turns up
none — the only writes to `$26` at direct page zero are one `STA` of `#$8000`
and three `STZ`s, and none of them can set bit 14. It was tempting to write that
down as a finding: *a switch still in the ROM with nothing left to throw it.*

It is wrong, and the way it is wrong is the useful part. Scanning the ROM
**image** instead — all eight opcodes that can write `$26`, direct page and
absolute, across the whole cartridge rather than across what the tracer managed
to decode — finds it immediately:

```
$80:AB8A  A9 00 40   LDA #$4000
$80:AB8D  04 26      TSB $26        ; hold the camera and the queue
   ... eight rows of eight tiles, each one AND #$01FF : CMP $DC : ORA #$2000 ...
$80:ABC6  20 37 9E   JSR $9E37
$80:ABC9  A9 00 40   LDA #$4000
$80:ABCC  14 26      TRB $26        ; and let go
$80:ABCE  20 6D 9E   JSR $9E6D
```

Neither instruction is in the listing as code. The whole routine — entry at
`$80:AB5A`, `PHD : PEA $0000 : PLD`, taking a tile in A and a block position in
X and Y — is one long `.db` run. It is the **runtime block writer**: it stamps a
64x64-pixel block of map into the tilemap, and it holds the view still while it
does, then asks for the transfer itself on the way out. That is the fourth
distinct place the `$DC` priority rule appears.

So `follow_held` is reachable, and stays. What it needs is an input that
repaints part of the map mid-level; `hotbytes.py` gives `$80:AB5A` **zero**
executions across all ten profiles, so no movie in the corpus has ever made the
game do it.

Three times this session a fact about this ROM has turned out to be hiding in a
`.db` block — `$80:A70A`, `$80:A816`, and now this — and all three were found by
going to the bytes. **A grep of the disassembly is a lower bound on the ROM, and
in these banks it is not a tight one.**

### The standing check, exact for the seventh time

The native total moved from **192,833,966 to 197,920,067** — 5,086,101
instructions, against a measured span of `$80:A93F..$80:A9CC` of exactly
5,086,101. Subsumed (175) and both-sides (40) counts did not move.

**62.0% strict, 73.5% call-weighted; 69.6% and 82.5% with the waiting out** —
the largest single-routine move this phase has had.

### And the row that was going to be next is not a routine

With the camera done, the ranking's top portable row read `$81:81A2`, 1.6% over
79 calls. Reading it first — which is now the habit — took forty-seven bytes:

```
$81:81A2  LDA $16 : STA $00 ... LDA ($0C),Y ... JSL $80825E ... RTS
$81:81EE  RTS
$81:81EF  LDA $06 : JSL $818191 : RTS
```

That is the whole routine, and in ten profiles it executes **3,234
instructions**. All 4,671,086 of the rest — the entire 1.46% — belongs to what
begins at `$81:81F6`, which the listing has no label for because nothing ever
proved it was an entry. It opens `PEI $02 : PLB` and drops into a loop around
`JSL thread_yield`, so **it is a thread body**: entered by the scheduler
resuming it, never by a `JSR`. With the boundary declared in `JUMP_ENTRIES` the
row moves to `$81:81F6` and its call count reads **0**, which is the ranking
saying so itself.

This is the seventh time nearest-preceding-entry attribution has put work on the
wrong routine here, and the fourth time the fix was a line in `JUMP_ENTRIES`.
The share is unaffected — an attribution boundary moves credit between rows, not
into or out of the native total — but the *reading list* is, and this one would
have been an afternoon spent on a routine that does not exist.

The real top portable rows are now `$82:8069` (1.1% over 1,126) and `$80:C8B8
dma_to_vram` (1.0% over 4,557).

## $82:8014 / $82:8069 boss_bg_queue — the boss that is not made of sprites

Routines seventy-two and seventy-three, taken together because they are the
same routine twice with one difference, and because neither of them has ever
appeared in the disassembly. Both passed first run: **427 and 598 calls on
`level25-lane`, 0 diverged**, and nothing to fix in either — 903 and 1,126 over
that movie's full 9,400 frames, and **10,486,161 calls on 42 movies, 0
diverged** across the whole corpus. Branch coverage is **262 of 340** with 78
untaken, which is exactly where it was, for a reason the next section but two
is about, and the decline census is still empty.

They draw level 25's giant baby. A 65816 will put 128 sprites on a screen and
no more than 34 on a scanline, which is nowhere near enough for a figure that
size, so the game does not use sprites for it at all. It keeps four animation
frames as **raw tilemap words** — 14 tiles by 20 rows, 112x160 pixels, 564
bytes each including a four-word header — and every frame it re-uploads one of
them into BG1's tilemap at VRAM word `$6800` and moves the whole layer with the
scroll registers.

```
$82:892E  ASL A : ASL A : TAX             ; frame 0..3
          $28/$2A = $895E,X               ; $97:FD9D, or three in bank $83
          A = $896E,X : BIT $36 : BPL +   ; the sway: 0, +8, -8, 0
          EOR #$FFFF : INC A              ; ...negated when facing left
        + $1E66 = A : $1E68 = $8970,X
          LDA $36 : BNE flipped
            JSR $890C : BRA out           ; -> $82:8014
  flipped: JSR $891D                      ; -> $82:8069
```

Both blitters walk the figure a row at a time and append one DMA job per row to
a queue at `$1D54` — source, bank, length, and a VRAM word address that starts
at `$6800` and steps by `$0020` — and both end by registering `$82:81C9`, the
vblank job that drains it.

The figures identify themselves. Printing `$83:DAC0` with a dot for tile zero
and a hash for anything else needs no graphics data at all:

```
......####....      head
.....######...
.....######...
.....######...
.....######...
.....######...
..#########...
##############      both arms, and the reach that sets the figure's width
##############
..########.##.
...########...
...########...
...########...
...###.####...
...########...
..########....
..########....
.....#######..
......######..      one leg, bent under
.........##...
```

That is the shape in the screenshot, and it is worth noticing that it is **not
symmetric** — the head sits right of centre, one arm is longer than the other,
the leg is tucked to one side. A symmetric figure would not need `$82:8069` at
all; it is the asymmetry that makes a mirror the only way to get the second
facing without storing it.

### The difference is one loop, and it costs a quarter of a frame

`boss_bg_queue` points each job straight at the ROM. There is nothing to
prepare, so it is four stores and an add per row: **11,565 master cycles**.

`boss_bg_queue_flip` cannot, because the figure it wants does not exist. It
builds one, in a staging buffer at `$7E:5736`, by copying each row **backwards**
and toggling bit 14 — the tilemap X-flip bit — of every word on the way past:

```
$82:8094  LDY $38 : DEY : DEY            ; the row's last word
$82:8098  LDA [$28],Y : EOR #$4000 : STA [$2C]
          INC $2C : INC $2C : DEY : DEY : BPL $8098
```

You need both halves or you get a figure made of correctly-ordered backwards
tiles. Eight instructions, 280 times — once per tile in the figure — and that
is **89,008 master cycles**, seven and a half times the plain version and
close to a quarter of an NTSC frame's 357,368, and the largest cycle budget in
the registry by a factor of two.

What the cartridge gets for it is the other direction free: four frames of one
facing is 2,256 bytes and the mirror is why that is not 4,512. What it costs is
560 bytes of WRAM round trip on every frame the boss faces the wrong way.

### The narrowest cycle spread in the registry

`verify` reports **11,528..11,686** and **88,980..89,256** over the full movie
— 158 cycles and 276, 1.4% and 0.3%. Nothing about either routine varies except
which of four stored figures it was pointed at, and all four are the same size,
so the only thing moving is the DMA queue's starting cursor. Every other entry
in the registry spans at least a factor of two and most span a factor of fifty.

### Neither has a branch worth marking

Between them they contain three conditional branches and **all three are loop
backs**. There is no decision in either routine: the figure's header decides
how long they run and nothing decides what they do. So this is the first pair
in the port to add **no branch-coverage sites at all**, which is the coverage
principle working in the direction it less often works in — a site no corpus
can reach dilutes the number, and so does a site with nothing on the other side
of it.

The decisions are one level up, at `$82:892E`, which reads the facing flag and
calls one or the other. That routine is 0.01% of the corpus.

### The registers are all somebody else's

Both end on `LDA #$81C9 : LDY #$0082 : JSL $8083AE : PLD : RTL`, so every
register output and the carry belong to `vbl_queue_a_add` and say only whether
the vblank queue had room:

* **A** and **Y** are `$81C8` — the address it stored, which is one less than
  the one it was given, because the dispatcher reaches a job by `RTL`;
* **X** is the slot it took;
* **C** is its own `CPY #$0010`, clear when there was room, and untouched by
  the six instructions between it and the `RTL`;
* **N** and **Z** are the caller's direct page, off the closing `PLD`.

On the queue-full path A keeps `$81C9`, the `PLY` puts the bank back in Y, and
X is whatever the blitter left there. That leaves the entire result of an
89,008-cycle routine in WRAM, which is the easiest kind of routine to check and
part of why both passed first run.

### 4.44% of one movie and 0.00% of the other nine

This is the most lopsided row the ranking has ever produced. The pair is 1.1%
of the corpus, and **every instruction of it comes from `level25-lane`**, where
it is 4.44% of everything the game does. Eight of the ten profiles have a zero
in this range; a ninth, `level21-bubble`, has 508 instructions, which is one
call of the cheap one and nothing else.

It is worth being plain about what that means. It is not a hot routine — it is
a routine that is *only ever* hot, on the levels that have one of these figures
in them, and nine call sites across banks `$82` and `$83` reach the pair, so
level 25's baby is not the only one. A corpus with more boss movies in it would put
this pair much higher, and a corpus with none would not show it at all. The
ranking is an average over the movies that exist, and this is the first row
where that sentence has done real work.

### Both routines were `.db`, and now there is a tool

Neither `$82:8014` nor `$82:8069` has a single byte in `analysis/bank_82.asm`
as code. That is the third round running in which something this project needed
was inside a `.db` run — the camera scroll pair, then the writer of the
camera-hold bit, now this — so `tools/dis816.py` exists: a plain forward 65816
decoder over the ROM image, with `SEP`/`REP` width tracking and nothing else.

**Before porting a routine, decode its bytes, not its listing.** The two agree
most of the time.

### `run`, and a parting that did not move

The control on `level25-lane` is identical at all 6,089 compared passes.
Substituting the pair alone leaves 3,768 passes differing and **every byte of
every one of them inside the stacks or a declared scratch byte** — no live game
state, which is the cycle budget being an estimate and nothing more.

With everything substituted the movie parts at **pass 3496**, stock on frame
2685 and native on 2684: exactly where it parted before this round, and before
the one before that. 3,485 of 3,485 compared passes differed, all of them in
the stacks.

*(Corrected in the next round: `level25-lane` is not the only movie that parts.
`level21-bubble` parts at pass 1152 and always has — it simply had never been
run under `run` before. See the `actor_aligned` entry.)*

The frame-6100 screenshot differs from stock's, which the README has said for
some time and for the right reason. What this round adds is a measurement that
settles it exactly. Four framebuffers, all at frame 6100:

| substituted | sha256 |
| --- | --- |
| nothing (`--stock`) | `8cd270317fe8376a` |
| **only this pair** | `8cd270317fe8376a` |
| everything **but** this pair | `e9ccdd50282b0a2d` |
| everything | `e9ccdd50282b0a2d` |

Adding the pair to a full substitution changes **not one byte** of the result,
and substituting the pair *alone* reproduces stock exactly. The divergence is
entirely older than this round, and the pair is provably not in it.

It is also not a one-frame offset — the native frame matches no stock frame
within two of 6100 — because that is what a parting *is*: past pass 3496 the
two are different playthroughs, and comparing their frame 6100 compares two
different moments of two different games. Stopping both at frame **2600**,
before the parting, gives byte-identical framebuffers. The other four movies
are unchanged.

Getting that table cost a one-line fix: `zamn.exe` silently capped `-r` at 32,
which was more than the registry held when it was written, so asking for
"everything except two" dropped the 33rd flag and then failed with `unexpected
argument 'enemy_cdde'`. It is 128 now, and overflowing it is an error rather
than a silent truncation.

### The standing check, exact for the eighth time

The native total moved from **197,920,067 to 201,357,517** — 3,437,450
instructions, against a measured span of `$82:8014..$82:80E0` of exactly
3,437,450. Subsumed (175) and called-from-both-sides (40) did not move, which
is what you expect from a leaf pair whose only callee was already ported.

**63.1% strict, 74.6% call-weighted; 70.8% and 83.7% with the waiting out.**

## $80:C8B8, and the first attribution fix to move a published number

**The next row on the ranking was `$80:C8B8 dma_to_vram`, 1.0% over 4,557 calls,
and reading it first took fifteen instructions.** It sets up channel 0, writes
`$420B`, and returns:

```
$80:C8B8  REP #$30 : STA $4302 : LDA $04,S : SEP #$20 : STA $4304
          STX $2116 : STY $4305 : LDA #$18 : STA $4301
          LDA #$01 : STA $4300 : LDA #$01 : STA $420B : REP #$20 : RTL
```

That is the whole routine, and in ten profiles it is **68,355 instructions,
0.02%** -- fifteen a call, exactly as written, with no loop anywhere in it. The
other 0.91% belongs to what starts at **`$80:C8F6`**, and that is a thread body:
`$80:87C1`, inside the level loader, does `LDA #$C8F6 : LDY #$0080 :
JSL thread_spawn`, and nothing in the ROM JSRs to it, JSLs to it, or holds a
pointer to it. It opens `JSR object_list_parse` and drops into a loop on
`JSL thread_yield`, spawning objects near the camera centre. With the boundary
declared its call count reads **0**, which is the ranking saying so itself.

**Eighth time nearest-preceding-entry attribution has put work on the wrong
routine here, fifth time the fix was one line in `JUMP_ENTRIES`.** Three of
those five have been thread bodies specifically, which is now a pattern worth
naming: a thread body is the *only* kind of live code with no call edge into it,
so it always lands on whatever subroutine happens to sit above it, and it is
always big.

**And this one moved a number that was already written down.** Every previous
fix left both shares alone, because an attribution boundary moves credit between
rows rather than into or out of the total. This one still leaves the strict
share at exactly **63.1%** -- unchanged to the instruction, 201,357,517 of
319,152,834 -- but the *call-weighted estimate* fell from **74.6% to 73.1%**,
and with the waits out from **83.7% to 82.0%**. The set of routines credited as
"called from both sides" went from 40 to 36 with it.

That is worth being plain about. The strict number counts instructions inside
ported routines and cannot care where a boundary sits. The estimate weights
every routine by the fraction of its calls that come from ported code, so it
does care: it was reading four routines as partly-native through a call edge
that belonged to a thread body rather than to `dma_to_vram`. **The 74.6%
recorded in the entry below was the honest measurement at the time and is 1.5
points too generous now.** The strict figure in it is unaffected.

The standing check is what makes this legible: it tests the strict number, and
the strict number is the one that did not move.

**Two smaller things fell out of the same reading.** `$80:C8DC` -- between the
two -- is a 16x16 hardware multiply through `$211B`/`$2134`, eleven
instructions, entirely `.db`, and executed **zero** times in all ten profiles:
a routine the game carries and never uses. And the listing annotates the thread
body's `$0C`/`$0E`/`$10`/`$12` as `vbl_queue_a_count` and friends, which they
are not -- a thread runs on its own 128-byte direct page, so those are its
locals. The symbol file's names are right for direct page zero and actively
misleading anywhere else.

**Next**, the top portable row is now `$80:B379` (0.6% over 7,779), and it is
honest: its span `$B379..$B3F1` measures exactly the 1,888,068 the ranking gives
it, and `$80:B3F1` above it is a separate routine with thirteen callers of its
own. It is a leaf, it is `.db` from end to end -- fourth round running -- and it
is a downward scan of the 32-record display list looking for a record of type 1,
5 or 6 lined up within eight pixels of a point, answering with which side it is
on.


## $80:B379 actor_aligned -- the other question the same enemy asks

**Routine seventy-four, and the cleanest result the harness has ever given.** It
passed first run -- 6,921 calls on `level21-bubble`, 0 diverged -- and under
`run` substituting it alone the two machines are **identical at all 6,089
compared scheduler passes**, not one stack byte different. No other routine in
the registry has managed that; every previous one has moved the stack somewhere,
because the cycle budget is an estimate. This one's estimate is close enough
that nothing notices.

**It is `actor_nearest`'s sibling and reads as its twin.** The same 32 slots
walked from the top down, the same `ACTOR_DRAW` then flag-bit-0 gate, the same
collision ids minus one -- `$05`, `$06`, `$01`, which is `actor_nearest`'s four
without `$38`. What differs is the question. That one measures every candidate
and keeps the closest; this one takes the **first** candidate that lines up and
returns on the spot, so it answers with whichever matching record sits in the
highest slot rather than with the nearest one.

"Lines up" is one tile: `SBC : CLC : ADC #$0008 : CMP #$0010 : BCS`, the same
add-half-and-compare-unsigned trick `actor_at_point` uses. **X is tested first
and wins**, so a record within a tile on both axes is reported as up or down and
never as left or right. The answer is a direction index already doubled -- `$02`
up, `$06` right, `$0A` down, `$0E` left, `$00` for nothing -- which the caller
uses as a table index directly.

**And the two callers are the same enemy, asking at two different rates.**
`$81:9D34` calls this every frame; four instructions later `$81:9D4B` calls
`actor_nearest`, but only when a `$3C` counter expires. So the creature asks
*who should I be walking towards* once a second and *can I shoot right now*
sixty times a second, and the second question is the cheap one because it can
stop at the first answer.

**Eight of nine coverage sites taken on one movie, and the ninth was predicted.**
`aligned_inactive` -- drawn but without flag bit 0 -- is untaken, which is
exactly where `nearest_inactive` has sat since it was added. That is now **two
independent readers of the same bit** agreeing that nothing in 42 movies ever
produces a record which is drawn with it clear, and the note on `ACTOR_ACTIVE`
in `port/oam.h` says so rather than guessing what clears it.

**A second movie parts, and it always did.** `run` with everything on
`level21-bubble` parts at pass 1152 -- frame 209, during the level load, before
this enemy exists -- and it parts at exactly the same pass with `actor_aligned`
taken back out. So the entry above is wrong to call `level25-lane` "the one
movie in the corpus with a parting"; it is the one that had been *run*. Its
framebuffer differs for the same reason and by the same mechanism. Both entries
now say so.

Pinning that down needed the `-r` cap raised in `zamn_cosim` as well as
`zamn.exe` -- the same latent 32-entry limit, in the second binary, found the
same way and one round later. "Run everything except one" is how a divergence
gets pinned on a routine or cleared of it, and it needs one more slot than the
registry has entries.

**Corpus: 10,506,090 calls checked across 42 movies, 0 diverged; branch
coverage 270 of 349, 79 untaken; census still empty.** Eight of the nine new
sites are taken and the ninth is `aligned_inactive`, untaken by all 42 -- so the
prediction holds at corpus scale and not just on the one movie. The four
framebuffer checks that were identical are still identical.

**The standing check, exact for the ninth time.** 203,245,585 less 201,357,517
is 1,888,068, against a measured span of `$80:B379..$80:B3F1` of exactly
1,888,068. Subsumed (175) and both-sides (36) unmoved, which is what a leaf that
calls nothing should do.

**63.7% strict, 73.7% call-weighted; 71.5% and 82.7% with the waiting out.**

**Next**, `$82:8138` (0.6% over 673) and `$80:BF1B` (0.5% over 9,784) -- and
`$80:B3F1`, the routine immediately above this one, which has thirteen callers
and 186,736 instructions and is now the obvious neighbour to read.


## $80:B3F1 actor_snap_to -- the last pixel, and two things that went wrong

**Routine seventy-five, and the first this phase taken deliberately below the
ranking's threshold.** It is 199,679 instructions over ten profiles -- 0.06%,
where the rows around it are ten times that. It went in because it was already
read, because it is twenty-one instructions with no branch structure worth
arguing about, because it sits in the same file as its partner, and because a
leaf with thirteen callers is the sort of thing that turns *other* routines into
fully-subsumed ones later. It is not an argument for porting the next 0.06% row.

**It is the other half of what `actor_aligned` is for.** That routine answers
*is something roughly lined up with me*, with a tile of slack; this one closes
the last pixel of it. Per axis, independently: if `|rec.pos - onto.pos| < 2`,
write the target's coordinate straight into the record. Without it an enemy
walking one pixel a frame towards an alignment can step past the pixel it wanted
and fire down an empty corridor; with it the last pixel is a snap rather than a
step, so lining up always succeeds exactly.

**It failed first run, on two.** `A: ROM $0028, port $0026` -- off by exactly
two, on every call, which is a good kind of failure because the number names the
bug. `CMP #$0002` **does not write A**: it sets N and Z from a subtraction it
throws away. I had transcribed it as though it were an `SBC`, so the port's A
was the difference minus two where the ROM's was the difference. The fix is that
the value and the flag source have to be carried separately out of each axis,
which is now what the code does and what the header says.

**And then the standing check fired, on 12,943.** The native total moved by
199,679 against a span I had measured at 186,736. That check exists to catch a
routine silently subsuming its neighbours, and it has never been wrong before,
so the first assumption was that something had been quietly claimed.

Nothing had. Dumping the tool's own native set before and after showed one
routine newly native -- `$80:B3F1`, at 199,679 -- and the discrepancy was
entirely in **my range**: the first time I sampled it I wrote
`hotbytes 80 B3F1 B420`, and the routine's `RTL` is at `$B421`. Two bytes short,
12,943 instructions. Measured as `$80:B3F1..$80:B422` the move is exact, for the
**tenth time**.

That is worth keeping rather than quietly correcting. The check is a tripwire
for one specific failure and it caught something else -- an arithmetic slip in
the person using it -- and it caught it the same way, by refusing to agree with
a number that had no business being different. The right response to it firing
is still to go and look; the answer just is not always in the code.

**All four coverage sites taken on one movie.** The routine has exactly four
shapes -- each axis snapped or left -- and `level25-lane` produces all of them
across 5,515 calls. Nothing added to the backlog.

**Corpus: 10,541,563 calls checked across 42 movies, 0 diverged; branch
coverage 274 of 353, 79 untaken; census still empty.**
`run` substituting it alone on `level25-lane` leaves no live-state difference on
any of 6,089 passes and does not move that movie's parting.

**63.7% strict, 73.7% call-weighted; 71.6% and 82.8% with the waiting out.**

**Next**, back to the ranking proper: `$82:8138` (0.6% over 673) and `$80:BF1B`
(0.5% over 9,784). Both want reading before they are believed.


## Fifty-nine vblank jobs, and 1.1 million instructions the port was not doing

**A round that ported nothing and is the most useful one in a while.** The next
row on the ranking was `$82:8138`, 0.6% over 673 calls. Reading it first --
which is now three for four at catching something -- found nineteen instructions
that copy sixteen palette words into two buffers and queue a vblank job. It
executes **104,988** instructions in ten profiles, 0.03%. The other 0.57% was
five other routines stacked on top of it, and none of them is reachable by a
call.

**They are vblank jobs, and that is a whole class.** The two queues store a job
as `addr - 1` and the dispatchers reach it by pushing that and executing `RTL`.
So a vblank job is *never* the target of a `JSR` or `JSL`; the call graph has no
edge into it; and attribution-by-nearest-preceding-entry therefore charges it to
whatever subroutine happens to sit below it in the ROM. Every one of them is
invisible to the ranking until it is declared.

This is the same structural problem as `$81:81F6` and `$80:C8F6` -- code that
runs and is never called -- and it is the third and by far the largest family of
it. The other two had to be found one at a time. This one does not, because the
ROM registers a job with a fixed idiom:

```
LDA #$<addr> : LDY #$00<bank> : JSL $8083AE   (queue A)
                                JSL $808418   (queue B)
```

Scanning the cartridge for that finds **fifty-nine distinct jobs**, and
`VBL_JOBS` in `native_share.py` is that list, with the twelve-line script that
produced it recorded beside it. Declaring the class took one commit; finding it
one at a time would have taken a dozen rounds and half of them would have been
spent porting phantoms.

**And the port's share was overstated by 1,138,046 instructions.** That is the
part worth being blunt about. With the fifty-nine declared, the strict share
falls from **63.7% to 63.4%** and the static share from **48.0% to 40.1%**, and
the fall decomposes exactly:

| where it came from | instructions |
| --- | --- |
| vblank-job code sitting inside the span of a routine the port counts as native | 984,217 |
| 24 routines wrongly marked *subsumed*, because call edges leaving a vblank job were charged to the ported routine below it | 153,829 |
| **total** | **1,138,046** |

Seventeen native routines had a job inside their old span. The second row is
precisely the failure the `JUMP_ENTRIES` comment has warned about since the
`$80:A937` round -- *a `JSL` in an orphan can make the orphan's callee look like
it is called only from ported code* -- and this is the first time it has been
measured rather than argued about.

**None of the standing checks was wrong.** Each compares a *delta* against a
span with the same attribution on both sides, and all ten were exact. What was
wrong is the absolute level, which no standing check tests. The honest reading
is that the recorded 63.7% should have been 63.4%, and that earlier figures were
overstated by some smaller amount that grew as routines were ported next to
jobs. A delta check does not catch a bias that is already in both terms.

**One exception in the fifty-nine, and it is worth stating rather than hiding.**
`$80:9E7B vram_queue_flush` is registered as a queue-A job by `$80:A676` **and**
called outright by `$80:81A2` inside the NMI, 60,940 times over the corpus. It
is the only one of the fifty-nine with an inbound call-graph edge, which is how
it was found, and it is therefore the only one the harness could substitute --
so it is an entry like the rest but is not marked unportable. `VBL_JOB_CALLED`
holds it, alone.

The other fifty-eight go into `BLOCKED` alongside the NMI entry and the two
dispatchers that run them: **47,985,359 instructions, 16.9% of everything the
game does**, in code that runs and cannot be reached by per-call substitution.
That number is not a wall so much as a description of what Phase 4 is for.

**63.4% strict, 73.0% call-weighted; 71.2% and 82.0% with the waiting out.**

**Next**, the top portable row is `$80:BF1B` (0.5% over 9,784), then `$82:8F93`
(0.5% over 15,409) and `$80:D1FF` (0.4% over 726). `$82:8138` itself is now a
0.03% row and not worth a round; its untouched sibling `$82:816F` -- three
callers, and zero executions in every profile -- is worth remembering as an
input the corpus has never produced.


## $80:BF1B actor_notify_box -- the blast radius, as a routine

**Routine seventy-six, and the first row the ranking has offered in three rounds
that was exactly what it said it was.** Its span `$BF1B..$BF67` measures the
1,437,829 the ranking gives it, to the instruction. After `$81:81A2`,
`$80:C8B8` and `$82:8138` that is worth saying out loud.

It passed first run: **7,104 calls on `level25-lane`, 0 diverged**, and no
declines anywhere.

**`actor_overlap_pass` asks who is touching whom. This asks who is inside a
box.** The caller fills in five direct-page words -- four bounds and its own
collision id -- and every visible record inside the rectangle has its handler
entered, with the caller's id as the argument. Fifteen call sites across four
banks reach it, and that is the mechanism behind every attack in the game that
is not a contact hit.

**Two quirks, both kept.**

The bound clamp is `LDX #$0006 : BIT $38,X : BPL + : STZ $38,X : + DEX DEX :
BPL`, four words tested for bit 15. A box hanging off the **left or top** of the
map is clipped to the edge; a box off the right or bottom is not clipped at all,
because there is no map size in this routine to clip against. The asymmetry is
free -- a negative coordinate would wrap to an enormous unsigned one and match
nothing, or everything -- and the port keeps it.

And `LDY $9C : BEQ : DEY DEY : BEQ` refuses **one** visible record as well as
none. The walk would have run from index 0 to index 0; the guard rejects it for
being zero rather than for being empty. So a board holding exactly one visible
actor is told nothing at all by a blast, and this is not a curiosity for the
backlog: `notify_one_actor` is *taken*, in ordinary play, on both movies that
exercise the routine.

**Every register is claimed, which is not the usual choice here.** A and X and
the carry are all leftovers of the last record the walk looked at -- the id that
read zero, or the coordinate that failed a bound, or whatever the dispatch left
-- and the project's habit with leftovers is to decline to claim them and say
why. That habit rests on showing they are dead, and here four of the five call
sites read return immediately, which puts the values a frame further away than
anything I could check. So all four were transcribed instead: the carry threaded
through five `CMP`s, A through three `LDA`s, X through the record and the
dispatch. **7,104 calls agreed with it first time**, which is the answer that
declining to claim would never have produced.

**Zero declines, and that is a measurement of how far `collide.h` has got.**
This routine dispatches into arbitrary actor handlers, exactly as
`actor_collide_notify` does, and across all forty-two movies not one of them was
a handler the port lacks. The door into actor behaviour is no longer mostly
shut.

**Ten of eleven coverage sites taken over the corpus.** Neither movie that
exercises the routine heavily reaches `notify_bound_clamped` -- a blast whose box
hangs off the left or top of the map -- but something in the other forty does,
which is the corpus doing the job forty movies are for. The one left is
`notify_no_actors`, a frame with an empty visible list; the HUD rides on that
list, so it may be unreachable in play rather than merely unreached. It goes on
the backlog rather than being argued about.

**Corpus: 10,568,472 calls checked across 42 movies, 0 diverged; branch
coverage 284 of 364, 80 untaken; census still empty.** `run`
substituting it alone on `level9-weapons` leaves no byte of live game state
differing on any of 6,089 passes and no timeline parting.

**The standing check, exact for the eleventh time.** 203,745,047 less
202,307,218 is 1,437,829, against a measured span of exactly 1,437,829 -- and
this time the range was checked at both ends before it was believed.

**63.8% strict, 73.5% call-weighted; 71.7% and 82.5% with the waiting out.**

**Next**, `$82:8F93` (0.5% over 15,409) and `$80:D1FF` (0.4% over 726).

## $82:8F93 boss_step -- how a boss too big for sprites walks into a wall (2026-08-02)

**Routine seventy-seven, and the second row running that measured exactly what
the ranking said.** `$8F93..$9034` is 1,399,863 instructions, against a claim of
1,399,863. It passed first run: **15,405 calls checked on `level25-lane`, 0
diverged, 0 declines**, and the harness counted 15,409 calls where the profiler
counted 15,409.

**It is the fifth routine this session found hiding in a `.db` block.**
`analysis/bank_82.asm` renders the whole body as data, because the disassembler
works from the CDL and the CDL only knows what it watched execute. `tools/dis816.py`
decodes it in one pass. Five for five is no longer a run of luck; it is the
reason the habit exists.

**Where this fits.** `port/bossbg.h` draws the big figure and `port/collide.h`
lets it be shot. This is the third side of the same object: where it is allowed
to be. And the position it moves is not in an actor record at all -- big figures
keep theirs in two fixed words at `$1E62` and `$1E64`, which **eight routines
across banks `$82` and `$83` write**, so the slot belongs to whichever oversized
thing a level has rather than to level 25's baby in particular.

**It is not `step_propose`.** `$80:E450` proposes a destination and leaves
somebody else to accept it. This one proposes, tests and *commits*, and it
commits the two axes separately -- which is the whole point of it. A direction
picks a record holding a delta, a facing flag, and a pass count that is 8 for the
four diagonals and 0 for the four cardinals. Each pass tests one edge of the
footprint and commits that axis alone if the edge is clear. **So a boss walking
north-east into a north wall still goes east.** Sliding along a wall is not a
special case here; it is what falling out of one of two independent passes looks
like.

**The footprint is two probes, not a rectangle.**

    north   (-18,  0) (18,  0)      the top corners
    east    ( 18,  4) (18, 20)      the right edge
    south   (-18, 18) (18, 18)      the bottom corners
    west    (-18,  4) (-18, 20)     the left edge

The origin is the top centre of something 36 wide and about 20 tall. The
vertical pairs span y 0..18 and the horizontal pairs span y 4..20 -- the box the
game tests is **not quite the same box in both directions**, and nothing rounds
it off. Four probes would have been a rectangle; two are a leading edge, which
is all a mover needs and half the terrain lookups.

**The table has five entries for four directions and the fifth repeats the
first.** That is not padding. The index is a base plus the pass counter, and
north-west's base is the last one, so its diagonal pass has to find north at the
end of the table rather than by wrapping to the start. And the axis a pass
commits is **bit 3 of that same index**, which works only because the entries
are eight bytes and alternate vertical, horizontal, vertical, horizontal. One
table, indexed once, decides both which two points to test and which coordinate
to write.

**`#$6969` in A means double speed** -- a magic word, not a flag, tested with a
bare `CMP` and nothing else. When it matches, `$40` is added to the record index,
selecting the second half of the same table: the same eight directions with the
deltas doubled.

**And it decrements `$2C`, which nothing in the boss's thread reads back.** I
first wrote that down as "a dead write" and it does not survive being checked.
Bank `$82` has five instructions that read direct-page `$2C`: two inside the
blitters in `port/bossbg.h`, which store to it before every use; two at
`$82:A494` and `$82:A517` that a store four instructions earlier seeded; and one
at `$82:EF49` -- `LDA $2C : JSL $80:8353`, a **yield count** -- in a routine
nowhere near the boss, whose direct page this routine never sets. Probably dead,
then, and the port does not claim it: it reproduces the decrement, and the
harness's 128 KB comparison settles the question without anyone having to answer
it. Which is the useful direction for this to go -- the write costs one line to
keep and an argument to remove.

**Carry means it is stuck.** The routine ends by comparing both coordinates
against the copies it saved on the way in, setting carry only if *neither*
moved. Six of its eight call sites branch on that, and three of them --
`$82:8A60`, `$8A68`, `$8A70` -- are the same double step written out three times
in a row, stopping at the first that gets nowhere. When it does get nowhere the
caller reaches for the RNG and picks a different direction, so **this carry is
the entire reason a cornered boss does not stand there grinding against a wall.**

**A repeat of the `actor_snap_to` lesson, caught before it cost anything.** The
exit is `LDA $1E62 : CMP $10 : BNE`, and `CMP` does not write A. What comes back
is the coordinate that was loaded while N and Z describe the difference -- the
accumulator and the flags disagreeing on purpose. Having been off by exactly two
on every call once already, I transcribed the value and the flag source
separately from the start.

**Index zero is the ROM's own guard.** `$16` is a direction times eight and both
tables are sized exactly for `$00..$40`, with nothing between the last entry and
`terrain_blocked_wide`'s first instruction. A direction out of range would read
code as coordinates. The port adds no check, because entry zero of the delta
table is four zero words -- no movement, no facing change, one pass, carry set --
which is a working "stay put" and reads like the intended floor. Anything above
`$40` the port reads exactly where the ROM would, so even the accident agrees.

**All twelve coverage sites taken, on one movie.** That is not the usual shape:
this routine runs on level 25 and nowhere else, 2.01% of `level25-lane` and
**0.0% of the other nine profiles**, so the corpus had no forty movies to fall
back on. It did not need them -- one boss walking around one room reaches every
branch the routine has, including `boss_moved_y`, the diagonal that took only
its vertical half.

**Across all five level-25 movies: 42,207 calls checked, 0 diverged, 0
declines.** Cycles 2,076..15,616, call-weighted 11,770; `stack_bytes` 6. Almost
all of that budget is `terrain_blocked_wide` -- the routine's own arithmetic is
about sixty instructions and the probes are the rest.

**Corpus: 10,610,679 calls checked across 42 movies, 0 diverged; branch
coverage 296 of 376, 80 untaken; census still empty.** The corpus total is
exactly 42,207 higher than last round, which is `boss_step`'s own call count
across the five level-25 movies and nothing else -- and the untaken count did
not move, because all twelve of the new sites were reached.

**`run` parts, and this one is mine.** Substituting `boss_step` alone on
`level25-lane`, the two timelines part at pass 3502 -- stock on frame 2692,
native on 2691. That is the mechanism `README.md` already documents for this
movie, but the controls say it is not the movie's own: `-r none` runs the whole
9,400 frames without parting, and so does `boss_bg_queue_flip` substituted
alone, all 9,389 passes of it. So it is worth being exact about why the port
believes this is timing and not a wrong answer:

  * `verify` -- the strict check, 128 KB of WRAM and every register after
    **every single call** -- passes 42,207 calls across all five level-25
    movies with nothing diverged;
  * across the 3,491 passes `run` did compare, 1,025 differed and **every
    difference on every one of them was inside the stacks or a declared scratch
    byte. No byte of live game state ever differed**;
  * and the frontend's framebuffers at frame 2,600, before the pass where they
    part, are **byte-identical**.

The arithmetic behind the parting is not mysterious. In `run` mode this routine
is entered 16,595 times over 9,400 frames -- 1.8 calls a frame at 11,770 cycles
-- so substituting it removes about 21,000 master cycles a frame, close to 6% of
an NTSC frame's 357,368. The blitter that does not part removes roughly half
that. A scheduler pass on this level sits near enough to the vblank boundary
that 6% is the difference between overrunning it and not, which is exactly what
the harness reports and exactly what it exists to tell apart from a bad answer.

**The standing check, exact for the twelfth time.** 205,144,910 less 203,745,047
is 1,399,863, against a measured span of exactly 1,399,863.

**64.3% strict, 73.9% call-weighted; 72.2% and 83.0% with the waiting out.**
Static 40.7%. 77 registry entries.

**These five figures were superseded the same day** -- see the thread-body
entry above, which found 12,763,896 instructions of misattribution and moved
the call-weighted estimate to 60.8%. Nothing about the routine changed.

**Next**, `$80:D1FF` (0.4% over 726) and `$80:ADC8` (0.4% over 32,169). Two rows
above them are worth a note first: `$81:81F6` at 1.6% and `$80:C8F6` at 1.0%
both show **zero calls**, which is the signature this session learned to read as
a thread body rather than a subroutine. `$80:C8F6` is already known to be one.
`$81:81F6` has not been looked at, and if it is another then 1.6% of the
ranking is misattributed and the published share is overstated again -- so it
gets decoded before either of the rows below it gets ported.

## Thread bodies: the second family, and the port's share was overstated again (2026-08-02)

**No C changed today after `boss_step`. The port does exactly what it did an
hour ago. What changed is that the ranking stopped lying about it**, and the
numbers in the entry above this one are superseded by the numbers at the bottom
of this one.

The round began as a small check. `$81:81F6` sat at 1.6% with **zero calls**,
and this session had learned to read zero calls as a thread body rather than a
subroutine. It is one. It was also **already declared** -- as a `JUMP_ENTRY`, so
its work would stop being credited to `$81:81A2` -- but not as `BLOCKED`, so the
ranking still offered it as the top portable row on the board. `$80:C8F6` was in
exactly the same state. Two routines nothing can call, presented as the two best
things to port next.

**And then the fix generalised.** `$80:825E thread_spawn` takes the far entry in
`A:Y`, and its second instruction is `DEC`: it parks **`addr - 1`** in a
nine-byte frame that the scheduler resumes with `RTL`. That is not merely
similar to how a vblank job is dispatched -- it is the same mechanism. So the
same trick works: the spawn idiom

    LDA #$<addr> : LDY #$00<bank> : JSL $80825E

matches 32 of the ROM's 67 spawn sites outright, and three more index tables
that are themselves in ROM (`$80:ED8C`, eight six-byte records; `$82:C209`,
twelve four-byte ones) which expand to 17 further bodies. **49 in total, where
the previous two had been found one at a time, a round apart, each after a
ranking row failed to make sense.**

Two of the 49 appear in both derivations -- `$81:F380` from an immediate and
from `$80:ED8C`, `$83:9776` from an immediate and from `$82:C209` -- which is
the cross-check that says the record layouts were read right rather than
guessed.

**Checked two ways before any of it was believed.** None of the 49 is the target
of a `JSR`, `JSL`, `JMP` or `JML` anywhere in the cartridge -- unlike `VBL_JOBS`,
which had exactly one such exception and needed `VBL_JOB_CALLED` to record it.
And every one of the 49 contains `JSL $80:8353 thread_yield`; eight of them not
within the first 96 bytes, which is why the check had to be widened rather than
declared passed.

**Five ported routines' owned ranges did shrink, and every one of the five
splits lands after the routine's own return.** That was the check that mattered,
because a boundary landing *inside* ported code would be a fabricated
improvement rather than a correction:

| routine | its last return | the thread body below it | gap |
| --- | --- | --- | --- |
| `$80:8480 thread_call_handler` | `$80:84B0` | `$80:84B1` | 1 byte |
| `$81:E6E4 enemy_e6e4` | `$81:E72A` | `$81:E72C` | 2 |
| `$81:F534 actor_f534` | `$81:F54E` | `$81:F55E` | 16 |
| `$81:F6A3 shot_f6a3` | `$81:F6B7` | `$81:F6EB` | 52 |
| `$81:EDAA shot_edaa` | `$81:EDAA` | `$81:EEB7` | 269 |

The first row is the nicest thing in the round: the body the scheduler resumes
begins **one byte** past the `RTL` of the routine that dispatches thread
handlers. And the last is the sharpest: `shot_edaa` is a single `RTL`, and it
had been credited with 53,494 instructions. It now correctly reports as having
executed nothing at all in these ten profiles.

**The correction, decomposed exactly.**

| where the 12,763,896 instructions went | count |
| --- | --- |
| thread-body code inside those five ported ranges, all of it below their returns | 224,946 |
| 112 routines that stopped being subsumed, and 12 that left the both-sides band | 12,538,950 |

The second row is the real one, and it is the same mechanism the vblank round
found on a smaller scale: subsumption asks whether *every caller* of a routine
is ported, and a caller was identified by the nearest entry preceding the call
site. A call made from inside a thread body was therefore credited to whatever
ported routine happened to sit above it -- a phantom ported caller, which made
its callee look subsumed. 112 routines were subsumed on that basis. They are
not.

**So the numbers move a long way, and downward:**

| | before | after |
| --- | --- | --- |
| static | 40.7% | **21.4%** |
| strict dynamic | 64.3% | **60.3%** |
| call-weighted | 73.9% | **60.8%** |
| strict, waits out | 72.2% | **67.7%** |
| call-weighted, waits out | 83.0% | **68.2%** |
| routines subsumed | 152 | 40 |
| routines on both sides | 37 | 25 |
| work the harness cannot take | 47,412,505 (16.7%) | 55,837,825 (19.6%) |

The registry's own 77 entries are untouched at 51.5%, and that is the number
that was always honest: it is measured from spans the port actually implements.
Everything that moved was inference *around* those spans.

**This is the second time this class of bug has inflated the published share,
and the second time a delta check did not catch it.** The standing check --
port a routine, see whether the total moves by more than the routine is worth --
has now been exact twelve times running, and was exact through every round that
carried this bias, because a bias present in both terms is invisible to a
difference. Twelve correct deltas on top of a wrong level.

**The one thing that cannot be fixed by a scan.** Four spawn sites read the
address from data rather than from an immediate or a ROM table: `$80:8774` and
`$80:87FB` take it from bank `$9F`, which is per-level data -- **which threads a
level starts is a property of the level, not of the code** -- `$81:80E7` takes
it from WRAM, and `$81:81D7` walks a list through `($0C),Y`. So `THREAD_BODIES`
is a lower bound in a way `VBL_JOBS` is not, and the honest claim is "every
thread body the code names", not "every thread body". Anything those four start
that is not already in the list is still misattributed, and 60.8% may yet be
generous.

**Next, and it is not what it was this morning.** The top callable row is now
`$80:91F7` at 1.6% over 4,406,610 instructions -- a routine that was counted as
*native* an hour ago, on the strength of a subsumption this round withdrew.
(*It turned out not to be a target either: see the entry above, which found it
99.993% spin loop and moved it to `WAIT_SITES`.*)
Below it are `$80:9F29` (0.8%) and `$80:AD2B` (0.5%), and all three share a
telling number: **ten calls, one per profile.** These are level-setup routines
that run once and do a great deal, which is a different shape from everything
ported so far -- seventy-seven per-frame routines called millions of times
between them. `$80:D1FF` and `$80:ADC8`, named as next in the entry above, were
chosen against a board that no longer looks like that. Read the ranking before
picking, and check the span at both ends, as ever.

## Two more spins at the top of the board, and a rule for spotting them (2026-08-02)

The thread-body round left `$80:91F7` as the top callable row at 1.6%. It is not
a port target. **It is 99.993% a spin loop**, and so is the row below it.

`hotbytes.py` prints a warning whenever the hottest byte in a range is a
backwards branch -- *"if it is spinning on a flag or a counter it belongs in
WAIT_SITES, not in the work ranking"* -- and it printed that warning for both of
these. The warning has been in the tool since the round that added it. This is
the first time it has been read and acted on rather than noted and stepped past.

**`$80:91F7` -- 4,406,610 instructions, of which 4,406,310 are two spin loops.**
The routine is twenty-four instructions long. It loads graphics, enables BG3
alone, and then:

    JSR $9C52                        ; zero $136C, queue the VBL job $80:9C63
    LDA $136C : CMP #$000F : BNE -   ; ...and hold until the job has counted 15
    LDA #$0080 : JSL thread_yield    ; hold 128 frames
    JSR $9C72                        ; queue $80:9C7D
    LDA $136C : AND #$0080 : BEQ -   ; ...and hold while it counts back past 0
    WAI

`$136C` is never touched by the main CPU. `$80:9C63` increments it once per
vblank and returns carry set to stay queued, clearing carry at fifteen to take
itself off; `$80:9C7D` decrements it and leaves when it goes negative. **The
fade is counted on the vblank side and the CPU is held against it** -- so both
loops are waits in exactly the sense the other seven `WAIT_SITES` are, and both
jobs were already in `VBL_JOBS`, which is a pleasing consistency check on the
previous round.

What is left after subtracting the two loops is **300 instructions across ten
calls -- thirty a call.** That is the whole of this routine's real work, and the
1.6% was the CPU doing nothing 4.4 million times.

**`$80:9F29` is the same story one row down.** 2,149,592 instructions, of which
2,149,252 -- **99.98%** -- are `LDA $C6 : BNE`, holding while the vblank job
`$80:9ED0` drains a tilemap into VRAM and decrements the count. Thirty-four real
instructions a call.

**The tell, worth writing down: ten calls.** Both of these show *exactly ten*,
one per profile, next to millions of instructions. A routine called once per
movie that appears to execute half a million instructions per call is not doing
half a million instructions of work -- it is waiting for something, and the only
question is what. Every routine ported so far runs per frame or per actor and is
called thousands to millions of times; a single-digit call count next to a large
share is the signature of a boot- or transition-time hold.

**So the honest denominator grows again:** 10.93% waiting before this round,
**12.98%** after, and the shares with the waiting removed move from 67.7% and
68.2% to **69.3% and 69.8%**. The strict and call-weighted numbers -- 60.3% and
60.8% -- do not move at all, because declaring a wait changes what the
denominator *should* be and not what the port has done.

**`$80:AD2B` was checked the same way and is real.** Its hot bytes are
`LDA [$28],Y : STA [$2C],Y : DEY DEY : BPL`, an eight-word copy run 20,691 times
a call -- a bulk move of the level's block library, not a spin. It stands at
0.5% over ten calls and is a genuine target. (*It is genuine work and still did not
go into the registry: one call is six frames long, so `verify` abandons every
one of them. See the entry above.*)

**Next.** With three of the top rows now correctly labelled, the board reads:
`$80:CB61 apu_ipl_upload` (1.4%, and already carrying 5,906,040 instructions of
declared waiting beside it), `$80:CC7C apu_load_set` (0.8% over twenty calls),
then `$80:AD2B` (0.5%) and `$80:D1FF` (0.4% over 726). The first two are the
SPC700 boot handshake; `$80:AD2B` is the first row on the board that has been
positively confirmed as work rather than merely not yet disproved.

## $80:AD2B and $80:ACF6 -- where the map comes from, and one of them will not go in (2026-08-02)

The first row on the corrected board that survived being checked. `$80:AD2B` is
not a spin and not a misattribution: its hot bytes are `LDA [$28],Y :
STA [$2C],Y : DEY DEY : BPL`, an eight-word copy run 20,691 times a call. It is
real work, it is written, and **it is not registered**, for a reason that took
one `verify` run to find and is worth more than the routine would have been.

**A ZAMN level is not stored as tiles.** It is a *block map* -- one 16-bit index
per 8x8-tile block -- plus a library of those blocks at `$7E:8000`.
`$80:86A2 level_load` points `$AA`/`$AC` at the library and calls `$80:AD2B`,
which walks the map once and expands every cell into 64 tiles of the real map in
bank `$7F`. That is the map `port/terrain.h` has been reading all along and
`port/camera.h` copies strips out of as the view scrolls. **So this is where it
comes from, and it is the first thing in the ranking that builds rather than
reads.**

Three helpers, one per level of loop: `$80:AD1C tilemap_tile_addr` (already
ported) for where the tiles go, `$80:ACF6` for where the cell is, and `$80:AD0B`
-- seven `ASL`s, which is 128 bytes, which is eight rows of sixteen -- for where
the block is.

## The body cannot be checked, and that is a property of the routine

One call is about **395,000 instructions, roughly six frames**. `verify`
snapshots WRAM at entry and diffs it at exit, so an interrupt landing in between
makes the comparison meaningless, and the harness abandons the call rather than
reporting a divergence that is really the NMI handler's.

Registered, it reported the same thing on every movie tried -- **one call, one
interruption, nothing checked** -- on `level1`, `level1-rescue`, `level9`,
`level25-boss` and `level53` alike. There was no movie where a call fitted
inside a frame and there cannot be: the inner loop runs 20,691 times.

This is `$80:CD20 lzss_decompress` again, almost to the frame count -- that one
is seven, this one is six -- and it gets the same answer, which the project
already argued out once and does not get to re-argue cheaply: **registering it
would claim a check that is not happening.** What it wants is a verification
mode scoped to a declared footprint, here the block map it reads and the range
of `$7F` it writes, compared against the port run on the entry snapshot. That is
a deliberate weakening of "all 128 KB, every call", and it is worth doing on
purpose rather than to get a second routine in.

So: **`src/port/levelmap.c` is written and is unverified**, and this write-up
says so rather than leaving it to be inferred from a registry it is absent from.
The code stays because Phase 4 needs to build a tile map from a level record
like everything else. Its two coverage sites were removed with it -- a site the
harness can never reach dilutes the number, which is the rule `lzss_decompress`
already has a paragraph about.

## The helper does go in, and that is the same split lzss made

`$80:ACF6 blockmap_cell_ptr` is **seven instructions** -- far too short for an
NMI to land in -- and it is `tilemap_tile_addr`'s exact twin: the same shape
against `$7E:4228` instead of `$7E:4328`, leaving its answer in `$28` rather
than in A. It carries the same trap, too. **X comes back doubled and that is not
a restore**: `PHA` saves the column already shifted and `PLX` puts that back, so
N and Z describe the doubled column rather than anything useful, and the carry
is the `ADC`'s from two instructions earlier.

**Ten call sites in four banks reach it** and this registry has one of their
callers, so it will read as a both-sides row for a while yet -- which is the
right answer rather than a defect.

**1,410 calls checked across four movies, 0 diverged, 0 declines**, first run.
Cycles 334..374, call-weighted 343, `stack_bytes` 2 -- and with no branch in the
routine at all the 40-cycle spread is the bus and nothing else, the same shape
and very nearly the same number as its twin's 258.

**Corpus: 10,624,552 calls checked across 42 movies, 0 diverged; branch coverage
296 of 376, 80 untaken; census still empty.** The total is 13,873 higher than
last round, which is `blockmap_cell_ptr`'s own corpus count and nothing else,
and the untaken figure did not move because the routine has no branches to mark.

**The standing check, exact for the thirteenth time.** 192,426,276 less
192,381,014 is 45,262, against a measured span of exactly 45,262.

**60.3% strict, 60.8% call-weighted; 69.3% and 69.8% with the waiting out.**
Static 21.5%. 78 registry entries.

**Next.** `$80:8002 init_ppu_regs` (0.5% over ten calls -- and *check it for a
spin first*, because three of the last four rows with a single-digit call count
have been one) and `$80:D1FF` (0.4% over 726). The two APU rows above them,
`$80:CB61 apu_ipl_upload` and `$80:CC7C apu_load_set`, are both hardware
handshakes: `CB61` spins on `$2140` inside its own byte loop and `CC7C` drives
`$80:CCC8 apu_send`, which is already registered `verify_only` for exactly that
reason. They are checkable but not substitutable, and worth a round only when
somebody wants the check rather than the share.

## $80:ADC8 and $80:ADF3 -- one tile's attributes, and the row under the row (2026-08-03)

`$80:D1FF` was the next row, and reading it turned into a different round.
Its very first instruction is `JSR $E86D`, on every one of its 27,698 entries,
and `$80:E86D`'s third is `JSL $80ADC8`. A port substitutes the whole call
including everything nested inside it, so **`$80:D1FF` cannot be written until
`$80:ADC8` is**, and `$80:E86D` also reaches `$80:F935` and `$80:AE14`. The
board is not a list of independent rows; it is a graph, and the row below the
one being read was underneath it all along.

So: bottom of the chain first.

## `$80:ADC8` is 43 bytes and everything it needs is already here

Thirty-one instructions, `hotbytes.py` says every byte runs exactly once per
call, and 997,239 / 32,169 is 31.0 -- the listing and the profile agreeing to
one decimal place, which is what a routine with no loop and no early exit looks
like.

    TXA : LSR A x3 : TAX       ; pixel to tile, both axes
    TYA : LSR A x3 : TAY
    PEA $007F : PLB
    JSL $80AD1C : TAX          ; tilemap_tile_addr -- already ported
    LDA $0000,X                ; the tilemap entry, in bank $7F
    AND #$03FF : ASL A : TAY   ; ten bits of tile number, doubled
    LDA [$BA],Y                ; the attribute table, wherever the loader put it

Every line of that already exists in `port/terrain.c`: the first half is
`tilemap_tile_addr`, the second is the back half of `probe_attrs`, and
`terrain_blocked` has been doing both six times a call since Phase 3 started.
What was missing was the *one-tile* form, which is what the other 22 call sites
in banks `$80`, `$81` and `$82` want -- **the raw attribute word**, for callers
that pick their own bits out of it rather than asking a yes/no question.

`$80:ADF3` is the same twenty-one instructions **without the six `LSR`s**, for
the four sites that already hold tile coordinates. 76 calls in 42 movies against
`$80:ADC8`'s 32,169, and it is in because it is free.

## The shift is not the footprint tests' shift

`TERRAIN_TILE_SHIFT` is 2, because `terrain_blocked` wants a byte offset into a
row of 16-bit entries and gets there by shifting twice and clearing the low bit.
These two want a tile *number*, because `tilemap_tile_addr` does the doubling
itself, so they shift three times and mask nothing.

And there is **no `(9, 8)` origin bias here**. The footprint tests subtract one
before dividing; these do not. The two families genuinely disagree about which
tile a pixel is in, and that is the ROM's arrangement: "what am I standing on"
and "can this actor fit" are questions about different rectangles.

## `PEA $007F : PLB` leaves a byte on the stack

`PLB` pulls one and `PEA` pushed two, so the high `$00` sits there until the
`PLB` at `$80:ADEE` takes it -- setting the data bank to zero for four
instructions that do not use it -- and only the *second* `PLB` restores the
caller's. That second `PLB` is the last flag-setting instruction in the routine,
so **N and Z are the caller's data bank byte** and have nothing to do with the
attribute word in A. `$80:8480` is the only other routine here that ends that
way, and `CosimRegs::db` was put there for it; this is the second customer.

The same idiom is why `stack_bytes` is 13 and not 14. Nine bytes go down (`PHB`,
`PHD`, `PHX`, `PHY`, `PEA`) but the `PLB` takes one back *before* the `JSL`, so
the deepest point is eight, plus three for the `JSL` and two for
`tilemap_tile_addr`'s own `PHA`. `verify` measured 13.

Carry is the `ASL`'s, and it is always clear: `AND #$03FF` has already taken bit
15 out. X and Y are the caller's, put back by `PLX`/`PLY` -- the shifted copies
never leave -- which is why the register struct in `port/terrain.h` has neither.

## Results

**14,053 calls checked across three movies, 0 diverged, 0 declines**, first run:
3,584 on `level1-rescue`, 6,124 and 52 on `level9-weapons`, 4,269 and 24 on
`level29-ice`. Those last two movies are the only ones in 42 that reach
`$80:ADF3` at all, both through `$81:D0D4`.

Cycles 960..1000, call-weighted **990** for the pixel form and **862** for the
tile form. The 40-cycle spread is the bus, because there is not a branch in
either routine -- the same shape as `tilemap_tile_addr`'s 258 and
`blockmap_cell_ptr`'s 343, and 990 is very nearly four times the first of them,
which is most of what the routine does. The 128 cycles between the two forms are
the six `LSR`s and the transfers around them, almost exactly.

**No coverage sites.** There is no branch to mark, and the two
`terrain_attrs_bank_7e`/`_7f` sites that already exist test the same pointer
from inside `terrain_footprint`; a second copy here would report the same
condition twice and dilute the number, which is the rule `blockmap_expand` had
its two sites removed under.

`run` on `level9-weapons`, 9,000 frames, with the two alone and then with
everything: **no byte of live game state ever differed on any compared pass.**
Nothing parted, so there was nothing to chase to a framebuffer this time.

## A symbol that was never a routine

`zamn.sym` had carried `$80:AE00 tile_walkable` since the walkability work, with
a comment describing the six-tile footprint test. `$80:AE00` is **the second
byte of `$80:ADFF JSL $80AD1C`**. It is an opcode boundary in no execution, the
profile counts it zero times in ten movies, and the routine it described is
`$80:AE14`, four instructions past the end of the one it pointed into.

It never did any harm because nothing read it, which is the point: **a wrong
symbol is invisible until something starts using the file.** This round put four
correct names in that block and a note on the row where the wrong one was. It is
the same failure as the `.db` hazard one level up -- a name read off a listing,
believed because it was written down -- and it wants the same rule.

## The standing check, exact for the fourteenth time

193,425,111 less 192,426,276 is **998,835**, against a measured span of 997,239
plus 1,596 -- **998,835**. Two routines, both leaves, and the number moved by
exactly what they execute and not a byte more.

**Corpus: 10,732,802 calls checked across 42 movies, 0 diverged; branch coverage
296 of 376, 80 untaken; census still empty.** The total is 108,250
higher than last round, which is the two routines' own corpus counts, and the
untaken figure did not move because neither has a branch to mark.

**60.6% strict, 61.1% call-weighted; 69.6% and 70.2% with the waiting out.**
Static 21.7%. 80 registry entries.

**Next.** Back up the chain. `$80:E86D` is 0.1% over 28,088 calls -- 13.2
instructions each, `$80:E86D..$E8D2` and every one of the 370,307 inside it --
and it calls exactly three things: `$80:ADC8` and `$80:AE14`, **both now
ported**, and `$80:F935`, which is 493 calls and 125 instructions each. One
small routine stands between this registry and the whole of it. Then `$80:D1FF`
itself. Before either, note the wrinkle the reset round turned up:
`$80:D1FF`'s ranking row says **726 calls** and its entry byte executes
**27,698** times, because `$80:D1EC` is `JMP ($D1EF,X)` -- a player-state
dispatcher with eight table entries, four of them live -- and 26,972 entries
arrive that way. That is not a blocker. `cosim_step` intercepts on
`pc == r->entry` and never asks how the PC got there, and the `RTS` returns to
whoever called the dispatcher either way. But it is the first row where the
calls column *under*counts, so the standing check will want reading with that in
mind rather than against 726.

## $80:E86D floor_effect -- what the ground does to you, and eleven ways out of it (2026-08-03)

The routine `$80:D1FF` runs before it looks at a single button. It reads one
tile attribute through `$80:ADC8` -- ported last round, which is why this one
was possible -- and for five particular words does something to the player.
28,088 calls, 13.2 instructions each.

    $4000   harm, unless the player holds weapon 3 and `$1E` is set
    $0400   harm, unconditionally
    $8000   `STZ $2A`, and nothing else
    bit 3   a conveyor, and then one of four directions

    $0108  one pixel up        $0208  one pixel left
    $0408  one pixel down      $0028  one pixel right, if the way is clear

**Only the rightward belt asks the terrain.** The other three write `$30` or
`$32` outright, so a belt can push a player into a wall going up, down or left
and cannot going right. There is no comment and no obvious reason. The port
reproduces the asymmetry, because a guard on the other three would be a
difference from the ROM that no input can tell apart from a fix.

`AND #$FF7F` comes first, so bit 7 is not part of any of those comparisons --
whatever it marks is orthogonal to what the floor does, and the routine drops it
rather than testing it.

## `BIT #$0008` is not `BIT $0008`

The conveyor test is `BIT` in **immediate** mode, and on the 65816 that form
sets **Z only**. Every other addressing mode loads N and V from bits 15 and 14
of the operand; immediate does not. So on the routine's commonest exit --
`$80:E88C RTS`, 26,479 of 28,088 calls -- Z is the `BIT`'s and **N and carry are
`CMP #$8000`'s, three instructions earlier**.

That is the kind of thing a port gets wrong silently. Nothing in this routine
reads N afterwards; a caller might, and the harness compares the flag on every
call either way, which is what makes it cheap to be right about.

## `$80:F935` is inlined, and what it told us about `$50` and `$52`

Eleven instructions and **one call site in the entire cartridge**, four
instructions up at `$80:E89F`. So it goes in the body rather than the registry
-- the same call the blockmap round made about `$80:AD0B` -- and porting
`$80:E86D` subsumes it whole.

    LDA $70 : CMP #$0002 : BEQ out      ; two modes suppress it entirely
              CMP #$0004 : BEQ out
    LDA $52 : BPL out                   ; ...and so does the cooldown
    LDA #$8001 : STA $50
    LDA #$0020 : STA $52

`$50` and `$52` are a pair, and reading `$80:D01B` is what named them.
`BIT $50 : BMI` takes an "effect is running" branch that walks an animation
frame table and clears `$50` at the end of it before going on to
`$1CB8 player_health`; `DEC $52 : BPL` counts the other one down and parks it at
`$FFFF`. **So `$52` negative means idle**, the `BPL` here is a guard rather than
a test, and `$0020` is a cooldown: the floor cannot hurt you again until the
last one has finished. 19 of 493 calls get that far.

The port does not name `$70`. Two of its values switch the whole thing off and
the ROM does not say which two states they are, so the header says that instead
of guessing.

**And `$80:F935` is not a void call, however much it looks like one.** Its `RTS`
lands on `$80:E8A2`, which is the first of the four conveyor compares -- so A on
the way out matters, and A on the way out is `$70`, `$52` or `#$0020` depending
on which of its three exits it took. None of the three normally matches a
conveyor word, and "normally" is not "never": `$52` holding `$0028` would take
the rightward belt. The port reproduces the fall-through literally rather than
returning early.

## Direct page, and the trap next door

There is no `PHD` anywhere in either routine, so every one of `$0E`, `$1E`,
`$2A`, `$30`, `$32`, `$50`, `$52` and `$70` is a field of the **player thread's
own page**. Read against the absolute symbol table they are `vbl_queue_b_count`
and `apu_seq`, and they are nothing of the kind -- exactly the trap the thread
bodies sprang one file over, and the reason `zamn.sym` now carries a warning on
the row rather than a name. `$1CBC player_weapon` is the exception and is
genuinely absolute, because `LDA $1CBC,X` has a 16-bit operand.

No `PHD` also means **no `PLD` to take N and Z from**, so the flags are whichever
comparison the exit stopped at. There are eleven exits and the port tracks them
one at a time; `terrain_out_of_bounds` is the only other routine here shaped
that way, and it has six.

## Results

**11,355 calls checked, 0 diverged, 0 declines**, first run: 5,712 on
`level9-weapons` and 5,643 on `level25-lane`. Cycles 1,248..1,792,
call-weighted **1,322** -- the floor is the routine on its own and everything
above it is the two nested calls. `stack_bytes` 16: it pushes nothing itself,
and the deepest point is the `JSL` to `$80:ADC8` with that routine's thirteen
under it.

**Sixteen coverage sites, eleven of them taken across the 42-movie corpus.**
The five that are not are `floor_clear_2a`, `floor_mode_off`,
`floor_belt_left`, `floor_belt_right` and `floor_belt_right_blocked` -- zero in
all ten profiles and zero in all forty-two movies, and they go to the untaken
backlog rather than being dropped. That is the distinction
`blockmap_expand` established: a site the harness can *never* reach dilutes the
number and comes out; a site no movie has reached *yet* is what the backlog is
for.

`run` on `level25-lane`, the one movie with conveyors: **floor_effect alone is
clean on all 9,389 compared passes**, no live game state differing. With
everything substituted that movie parts at pass 3496 -- the same level-25
parting `boss_step` was established to own two rounds ago, six passes earlier
than before because the cycle budget moved. `level9-weapons` with everything
substituted is clean for all 8,989 passes.

## The standing check, exact for the fifteenth time

193,799,438 less 193,425,111 is **374,327**, against 370,307 for `$80:E86D` and
4,020 for the `$80:F935` it subsumes -- **374,327**.

**Corpus: 10,832,940 calls checked across 42 movies, 0 diverged; branch coverage
307 of 376, 85 untaken.** The total is 100,138 higher than last
round.

**60.7% strict, 61.2% call-weighted; 69.8% and 70.3% with the waiting out.**
Static 21.9%. 81 registry entries.

**Next.** `$80:D1FF` is now the top portable row on the board at 0.4%, and the
chain under it is gone: `$80:E86D` was its first instruction and everything
`$80:E86D` reached is ported. Two things to carry into that round. Its ranking
row says **726 calls** while its entry byte executes **27,698** times, because
`$80:D1EC` is `JMP ($D1EF,X)` -- a player-state dispatcher with eight table
entries, four of them live -- so the standing check wants reading against the
span and not against the call count. And three of its four remaining callees
(`$80:EA63 weapon_select_next`, `$80:EAA8 item_select_next`, `$80:EAE1
item_use`) are edge-triggered on buttons that **no movie in the profile set
presses in that state** -- `$80:EAA8` is already registered and executes nothing
in ten profiles -- so a port of `$80:D1FF` will be checked on 27,698 calls that
never enter three of its four branches. Worth knowing before, not after.

## $80:D1FF player_state_normal -- the player's ordinary frame, and a site that came back out (2026-08-03)

The top portable row on the board, and it went in without needing anything new.
Five of the six things it reaches were already C -- `floor_effect` from last
round, `weapon_select_next`, `item_select_next`, `thread_spawn` and
`apu_play_sfx` -- which is what two rounds of working *down* the call graph
instead of across the ranking buys you.

## A state handler, so the calls column is wrong about it

`$80:D1EC` is `JMP ($D1EF,X)`: a table of eight player states, four of them
live. **26,972 of this routine's 27,698 entries arrive that way**, and the other
726 are the one real `JSR $D1FF` at `$80:D40B`, inside another state.

The harness does not mind, and that is worth writing down rather than
rediscovering: `cosim_step` intercepts on `pc == r->entry` and never asks how
the PC got there, and the closing `RTS` returns to whoever called the dispatcher
either way. What it does affect is the standing check, which had to be read
against the routine's span and not against the calls column -- the first row
where that column *under*counts rather than over-attributing work.

## A frame, in order

  1. `JSR $E86D floor_effect` -- **before a single button is read**;
  2. the held weapon, checked against how much of it is left;
  3. the direction latch, `$0072,X` into `$24` and into `$26` if non-zero;
  4. four edge-triggered buttons -- B cycles weapons, A cycles items, X uses
     one, and L or R spawn a thread at `$82:D8DB` with a sound;
  5. four countdowns, each `LDA : BEQ : DEC`, the fourth clearing `$54` when it
     lands on zero.

The weapon block is the interesting one. It runs only while the fire button is
held and the weapon index is not negative, and then `LDA ($64),Y` reads that
weapon's BCD counter. **Empty sets bit 15 of `$006E,X`** -- the routine writing
back over the word it read from the controller, which is the only place in this
file the game does that. Non-empty clears it again and files `#$4000` in `$1E`
or `$20` by where the weapon sits in the list: 6 through 12 in `$20`, everything
else in `$1E`.

And `$1E` is the same word `port/floor.h`'s `$4000` floor reads to decide
whether weapon 3 makes the player immune. **So that immunity is a weapon both
selected and not empty**, and the two routines only connect through this word --
neither header could have said so on its own.

## Carry belongs to whatever ran before the countdowns

Nothing in the four closing `LDA : BEQ : DEC` blocks writes carry, so what the
caller gets at the `RTS` is whichever earlier instruction last did: the `ASL A`
that doubles the weapon index, one of the two `CPY`s that pick between `$1E` and
`$20`, the `LSR A` on the spawn path, or a nested call's. Eleven paths, and the
port tracks the flag through each rather than setting it at the end.

N and Z are simpler and worth saying so: all four countdowns write them
unconditionally, so the exit's N and Z are always the fourth one's, whatever
else happened.

## `$80:EAE1 item_use` is declined, and its coverage site came back out

`item_use` dispatches through a table of per-item routines and executes **zero
times in all ten profiles**, so porting it would be a large amount of C the
corpus cannot check. The guard declines those frames instead, and it can,
because the condition is entirely readable before the routine runs: `$006E,X &
$0040` set with `$1C & $0040` clear. Bit 15 is the only bit of `$006E,X` the
routine rewrites, so the guard reading the raw word rather than `$1A` is exact
rather than approximate.

**The first draft marked that branch with a coverage site, and it should not
have.** The guard declines every frame that would reach it, so the port never
runs it and no input can ever make it run -- which is precisely
`blockmap_expand`'s rule about sites the harness cannot reach. The branch itself
stays, because `$80:D28C` is what makes the fourth button exclusive with the
third; the site is gone. Sixteen sites became fifteen.

That distinction is now worth stating in one line, because three rounds have
turned on it: **a site no input has reached yet is backlog; a site the harness
is structurally prevented from reaching is dilution.** A guard is what turns the
first into the second.

## Results

**24,419 calls checked across five movies, 0 diverged**, first run -- 3,584 on
`level1-rescue`, 5,712 on `level9-weapons`, 2,235 on `level1-keys`, 7,245 on
`level1-2p` and 5,643 on `level25-lane`. **One decline in the entire corpus**,
on `level1-keys`, which is the guard catching its `item_use` frame.

Cycles 2,104..7,012, call-weighted **2,434**. The floor is `floor_effect` plus
four countdowns and nothing else, which is most frames; the ceiling is a button
edge that reaches `apu_play_sfx`, and what that costs is how long the SPC700
took to acknowledge the previous sound. `stack_bytes` 18: the routine pushes
nothing itself, so it is two bytes of `JSR` with `floor_effect`'s sixteen under
them.

**Eleven of fifteen coverage sites taken across the corpus.** The four that are
not -- `psn_fire_high`, `psn_spawn`, `psn_spawn_swallowed`, `psn_t3_expired` --
are backlog: reachable, and no movie has done them. Two of the four want a movie
that presses L or R, which no movie in the corpus does at all.

`run` is clean on both movies tried with the routine alone and with everything
substituted: `level1-2p` at 6,000 frames, 5,989 compared passes, and
`level9-weapons` at 9,000 frames, 8,989 passes. **No byte of live game state
differed on any of them.**

## The standing check, exact for the sixteenth time

195,017,908 less 193,799,438 is **1,218,470**, against a measured span of
exactly 1,218,470. Nothing new was subsumed, because everything this routine
calls was already registered -- which is itself the check working: a routine
whose callees are all ported should move the number by its own span and not a
byte more.

**Corpus: 10,931,722 calls checked across 42 movies, 0 diverged; branch coverage
318 of 407, 89 untaken.** The total is 98,782 higher than last round.

**61.1% strict, 61.6% call-weighted; 70.2% and 70.8% with the waiting out.**
Static 22.3%. 82 registry entries.

## Next, and the board has changed shape

For the first time there is **no portable row above 1%**, and the top of what is
left is APU: `$80:CB61 apu_ipl_upload` at 1.4% with 5.9 million more instructions
of measured waiting behind it, and `$80:CC7C apu_load_set` at 0.8%. Both are
hardware handshakes -- `CB61` spins on `$2140` inside its own byte loop and
`CC7C` drives `$80:CCC8 apu_send`, already registered `verify_only` for exactly
that reason. They are checkable and not substitutable.

Below them the rows are 0.3% and under: `$80:CCBF apu_next_byte` (231,975
calls), `$82:BB0D` (10 calls -- **check it for a spin first**), `$80:B2A5`
(18,927), `$80:CDF4`, `$81:BC3D`, `$80:AF2C`, `$81:8024`. That is the shape of
a project that has taken the big things: what remains is either hardware, a
thread body the harness cannot intercept, or a long tail of small routines. The
tail is worth taking on its own terms -- eight rows of 0.2% is 1.6% -- but the
honest framing is that the ranking has stopped being the interesting question
and `docs/cosim.md`'s untaken list has started being it.

## $82:BB0D was never a routine -- twelve thread bodies out of the level records (2026-08-03)

The write-up above said the next round would either take `$82:BB0D` or start
reducing the untaken list, and that `$82:BB0D` should be **checked for a spin
first**, because three of the last five single-digit-call-count rows had been
spins or misattributions. It is the fourth. Nothing was ported this round; a
row came off the board and the tooling got less wrong.

## Ten calls, 0.3%, and 609 instructions

`$82:BB0D` was 784,715 instructions across ten calls -- one per profile. The
first check is `hotbytes.py` over the routine's own bytes, and it accounted for
**609 of them in three profiles**. Everything else was somewhere else.

`span.py` says where and why: the CDL marks no subroutine entry anywhere between
`$82:BB0D` and `$82:D88C`, so the attribution span is **7,551 bytes** and
whatever runs inside it is charged to the routine at the top. The work is 7 KB
downstream at `$82:D81F`, and the profile alone identifies what it is:

| byte | what | executions |
| --- | --- | --- |
| `$82:D818` | `TXA : LSR : STA $48` -- the loop's set-up | 2 |
| `$82:D81C` | `LDX #$FFFE` -- the same loop's restart | 10,263 |
| `$82:D883` | `BNE $D81C` -- where the other 10,261 arrive from | 10,261 |
| `$82:D87D` | `JSL $808353` -- **`thread_yield`** | 10,263 |

Two entries, ten thousand iterations, each ending in a yield. A thread body.

The tell is worth keeping separate from the count: it was **`$82:D818` running
twice while `$82:D81C` ran 10,263 times** that said the entry was not where the
fall-through suggested. A relative-branch scan of the surrounding 200 bytes
found `$82:D883 BNE $D81C`, which closed the loop and sent the search backwards
instead -- and nothing at all executes before `$82:D7CF`.

## Which is a thread the *level* names, not the code

`$82:D7CF` is in no `THREAD_BODIES` entry, and `native_share.py` explained why
before the search started. Its own comment listed four spawn sites that read the
address from data, `$80:8774` and `$80:87FB` among them, and finished: *"there
is no static way to find it."*

**That was wrong for two of the four.** Per-level data is still data in the
cartridge:

  * `$80:886D` is `LDA $9F8002,X : STA $10` with X = level*2, so `$9F:8002` is a
    table of level-record bases. Index 56 holds `$8000`, the address of the
    table itself, and is the sentinel -- 56 records, levels 0..55.
  * `$80:8774` spawns the single far entry at record offset **`$18`/`$1A`**.
    35 levels have one; five distinct bodies, and all five turned out to animate
    the palette shadow `$82:8138 palette_copy_a` fills. `$80:A0EF` is the
    clearest: `AND #$FC1F` clears green in BGR555, ORs a new value in, yields
    four ticks, repeats.
  * `$80:87CB` is `CLC : LDA #$003C : ADC $10 : PHA`, and the loop above it
    walks **eight-byte `(entry far, parameter far)` records** from offset
    **`$3C`**, spawning each until a zero entry. 36 levels, seven bodies.

**Twelve new thread bodies, 49 to 61.**

## The parameter is the reason they had to be data

`$82:D7CF` runs eight animation channels off one direct-page table, each channel
a script of `(tile, delay)` pairs reached through `[$00]` -- and that script is
the spawn parameter. Twenty-one levels share the code and differ only in the
table, so the entry is a constant and the behaviour is not. An immediate scan
cannot see that, and a call graph cannot either.

The same field explains a shape that looked like a parse error at first:
`$82:A8BB` appears **three times in level 20 alone**. It is not a duplicate --
it is three instances of one body with three parameters. `$82:A8BB` and
`$82:A8C3` are also two entry points into a single body: both reach the same
`thread_yield` at `$82:A8EB`, eight bytes apart.

## Checked the way the first 49 were

Every one of the twelve reaches `JSL $808353 thread_yield` within `$C5` bytes of
its entry -- only a thread yields -- and **none has an inbound call edge**.

That second scan has a trap that bit on the first attempt and is worth writing
down: `JSR` and `JMP` are program-bank-relative, so matching operand bytes alone
reported a `JSR $A0EF` in bank `$91` as an edge into `$80:A0EF`. It is a call to
`$91:A0EF`. Only `JSL` and `JML` carry a bank; the other two only count inside
the target's own bank. With that fixed the total is zero.

`tools/levelthreads.py` is the round's artefact and asserts the rest: all 56
records parse, every entry is a code pointer into banks `$80..$83`, every list
terminates, and every body yields. It also cross-checks itself against
`THREAD_BODIES`, so it stays honest as the set changes. One free cross-check
fell out: the 21 levels that use `$82:D7CF` is exactly the number of times the
three bytes `CF D7 82` occur anywhere in bank `$9F`.

## Results, and why the headline number does not move

`$82:BB0D` leaves the board, keeping the **2,030 instructions that are genuinely
its own** -- which puts it below every row the report prints. `$82:D7CF` takes
its place in the blocked family at 782,685. The blocked families go from
58,543,890 to **59,326,575**, 21.1% of everything to **21.4%**.

**The native share does not move at all: 61.1% strict, 61.6% call-weighted,
before and after.** That is correct and it is the point. A thread body was
already in the denominator and never in the numerator; all that changed is whose
name the work is filed under. The ranking is a list of what to port next, and a
row that cannot be ported does not belong on it.

The other eleven bodies executed nothing in these ten profiles, because ten
profiles are ten levels and these are per-level threads. They are registered
now, so the next profile that visits levels 12, 20, 47 or 52 will attribute
them correctly rather than quietly inflating whatever sits above them.

## What this says about the remaining rows

Four of the last six single-digit-call-count rows have now been spins or
misattributions, and none has been a routine worth porting. **A call count in
single digits next to a share above 0.1% has not once meant real work.** That is
no longer a heuristic to apply case by case; it is strong enough to check first
and by default, and `hotbytes.py` plus `span.py` do it in two commands.

What is left on the board above 0.2% is now `$80:CB61 apu_ipl_upload` (1.4%),
`$80:CC7C apu_load_set` (0.8%), `$80:CCBF apu_next_byte` (0.3%) and `$80:B2A5`
(0.3%) -- three of the four APU handshakes, checkable but not substitutable.
**The ranking has run out of things to say.** The next round should be the
untaken list: 89 sites, and the cheapest four are `psn_fire_high`, `psn_spawn`,
`psn_spawn_swallowed` and `psn_t3_expired`, two of which need nothing more than
a movie that presses L or R -- which no movie in the corpus does at all.

## The untaken list instead of the ranking -- two sites cleared, and two priced (2026-08-03)

The round before last ended by saying the ranking had stopped being the
interesting question and `docs/cosim.md`'s untaken list had started being it.
This is the first round run that way: nothing was ported, one movie was written,
and the two sites it did not clear are now priced instead of guessed at.

## `psn_spawn` and `psn_spawn_swallowed`, for the cost of one movie

`movies/level1-map.zmv`. **No movie in the corpus had ever pressed L or R** --
checked, not assumed: zero rows across all 42 files name either button. That is
the whole reason those two branches were untaken, and it is the cleanest example
yet of backlog rather than dilution.

The two branches are a press and *the same press again too soon*. `$82:D8DB`'s
first instruction is `INC $1F98,X`, so the spawned thread sets the player's flag
itself, and `$80:D29C` requires that flag clear -- when it is not, the press is
eaten and the flag cleared instead. So `psn_spawn` wants one press and
`psn_spawn_swallowed` wants a second one while the first is still up. The movie
presses L five times at 40-frame spacing and then R five times at 12, and the
tight spacing is what takes the swallow.

**Both taken on the first run**, with 162,997 calls checked and nothing
diverged. Some inputs cost a round of route-finding; this one cost ten lines.

## `psn_fire_high` is one object in the cartridge

`$80:D245  CPY #$000D` sends weapon indices 13-and-up back to `$1E`, and the
weapon search walks fourteen slots -- so the branch means **slot 13 exactly**,
the last one. `$80:F87B player_pickup` does `SBC #$0018` on the doubled
collision id, so slot = id - `$0C` and slot 13 is id `$19`; `$80:CA30
object_type_to_id` maps that back to **object type `$2A`**.

There is exactly one type `$2A` in all 56 level records: **game level 22, at
(739,133)**. Level 22 has no password -- the table only spells every fourth
level -- so the input is a level-21 password start and then playing level 21 to
its exit. That is one level completion, not a route, and it is a different kind
of work from anything the corpus contains. Priced, not attempted.

## `psn_t3_expired` needed the mechanism understood before the input

This one looked like the cheap one and was not, and the reason is worth keeping
because it is a fact about the game rather than about the movie.

**`$56` is a per-state timer, not a global one.** States 2, 4 and 6 each arm it
and each decrement their own copy, and the two paths that end a state do it
*exactly* when `$56` hits zero -- inside that state. `player_state_normal` is
state 0, and `$80:D2DF DEC $56` has executed **zero times in 27,698 calls**: by
the time state 0 runs, `$56` is always already zero.

The dig that ruled out the obvious route is worth recording. `$80:DC5C` arms
`$56` with 390 as part of entering state 6, and state 6 is **the bubble** --
`movies/level21-bubble.zmv` is the only movie in the corpus that enters it, once,
at frame 4204, and it waits out all 390 frames. State 6 has an escape: `$80:D493`
and `$80:D4A6` want `$0072,X` and last frame's `$24` to be `$06` and `$0E` in
either order, which is a **d-pad wiggle** -- the codes are direction x 2, so
`$06` is right and `$0E` is left, off the nine `(dx,dy)` pairs at `$82:B7FC`.
Three alternations and `$80:D4BE` sets `$56` to 1.

**And that is still no good**, which is the point: `$80:D4DB` decrements it in
the same frame, hits zero, and takes the exit. `$80:D4BE` ends the bubble early;
it never hands a live countdown to state 0. The same is true of `$80:DB16`,
which arms `$56` with 5 and returns -- it is reached only from state 2, whose
body has never executed at all.

**The one site that can do it is `$80:EB23`**, item slot 1's entry in the
`JMP ($EB07,X)` table:

    $80:EB23  LDA #$8000 : STA $54
    $80:EB28  LDA #$0352 : STA $56

`$80:EAEC  LDA $4E : ORA $70 : BNE` refuses the item unless `$70` is zero, so it
is used **from state 0 and stays there**, and its 850 frames count down in
`player_state_normal` until the fourth countdown clears the `$8000` this set.
That pairing -- `$54` armed here, cleared there -- is what the branch is *for*,
and neither routine says so alone.

Item slot 1 is id `$22`, object type `$0A`, and there are nine in the cartridge:
in the records for game levels 3, 12 (two), 19 (two), 25, 29, 41 and 47. Only
three of those levels have a password -- 25, 29 and 41 -- and of the three
**only one has a route**. `zamn_assets route` refuses both of the others from
their movies' own start positions with its standard verdict, that the target is
either inside scenery or behind a door; which of the two it is was not chased.
So the branch hangs on the object at (553,212) in level 29.

**The route drifts and the movie was not kept.** `zamn_assets route 30` returns
375 cells in 37 legs, and replaying them puts the player at (899,1217) when the
route wants (876,1228) -- the BFS grid calls that cell walkable and the player's
3x2 footprint cannot reach it. Everything after that leg is walking into walls:
the player finishes at (911,1025), **888 pixels from the item**, and sits there
for the rest of the movie. That is the failure mode every routed movie in
`movies/` has a paragraph about, and the fix is the same one they used: anchor
the legs on walls instead of on cell counts. A movie whose header described
reaching the item while ending nowhere near it would be worse than no movie, so
it was deleted rather than committed.

## Two things about the tools that cost time

**The player's direct page is `$7E:0100`.** `--watch` needs an address and the
state index lives at `$70` on the player thread's page, which `src/headless.c`
finds by scanning `$7E:0100` upwards in `$80` strides for the page whose `$64`
holds `$1CCC`. For player 1 it is the first page, so the state index is
`$7E:0170` and `$56` is `$7E:0156`. That is what turned "the bubble happens
somewhere in this movie" into "frames 4204 to 4667" in one run.

**`zamn_assets actors` and `zamn_assets route` are off by one from each other.**
`route` takes the record index and `actors` prints it too, but the *game's* level
number is the record minus one: `movies/level29-firstaid.zmv` routes through
record 30. This was checked rather than inferred -- the first weapon-13 scan
reported "level 23" and the answer is level 22.

## Results

**Corpus: 11,094,719 calls checked across 43 movies, 0 diverged; branch coverage
320 of 407, 87 untaken.** Up from 318 and 89. The total is 162,997 higher than
last round, which is exactly `level1-map.zmv`'s own count -- nothing else in the
tree changed, so every other movie's numbers are unchanged and **the native
share is untouched**. This round moved coverage, not share.

Of `player_state_normal`'s fifteen sites, **thirteen are now taken**. The two
that are not are the two above, and both are priced: `psn_fire_high` wants a
level-21 playthrough, `psn_t3_expired` wants a wall-anchored route to one object
in level 29.

The drop is exactly two -- 89 to 87 -- so no site went the other way and nothing
new appeared. That is worth checking every round and not assuming: registering a
routine adds its sites to the denominator, and a round that clears two while
quietly adding three has not moved.

## Next

The level 29 route is the obvious next thing, and it is now a bounded problem
rather than an open one: the target, the item, the mechanism and the exact cell
the BFS route cannot reach are all written down. After that the remaining
untaken sites are worth re-reading as a list, because this round is evidence for
something the census keeps suggesting -- **the cheap ones are cheap for the same
reason every time.** L and R were untaken because forty-two routes had never
needed a button, not because the branch was hard. It is worth checking the rest
of the list for that shape before spending another round on route-finding.

## ...and the level 29 route, which is not a route-following problem (2026-08-03)

The entry above left the level 29 route as the obvious next thing and called it
bounded. It is bounded, and it is **not** the problem it looked like. Two
route-followers were written, the second one works, and the route still fails --
at a single cell, for a reason none of the three obvious explanations covers.

## The closed loop was the wrong correction, and the source says so

The first attempt re-planned from wherever the player actually was after every
leg, on the reasoning that drift cannot accumulate if it is measured out each
time. It **oscillates**: the player ping-pongs between (945,1487) and
(999,1487) for forty legs, because the plan from one cell begins `Left` and the
plan from the other begins `Right`.

`src/assets.c` already explains why, in a comment written when the search was:

> Exactly the game's box, with no margin, because there is none to be had ... So
> a path this finds is walkable **on the row it was planned for and on no
> other**, which is a constraint on the *movie* rather than on the search.

Re-planning from a drifted position is therefore the one correction guaranteed
not to work: it puts the player on a row the new plan was not drawn for. The
right correction runs the other way -- keep the original plan and steer the
player back onto it.

## The second follower works, and the bug in it is worth keeping

Walk each leg on its own axis, then put the **perpendicular** coordinate back
exactly where the plan expects it before starting the next leg. The first
version of that still missed, and the reason was mine rather than the game's:

    n = max(2, int(px / SPEED) + 2)      # two frames of slop "to be sure"

At two pixels a frame, two frames of slop turns a **three-pixel correction into
a six-pixel one**. Every fix overshot and left the player three pixels off on
the other side, which for a plan that is only walkable on its own row is exactly
as bad as not correcting at all. With `n = round(px / SPEED)` and no slop the
follower tracks the plan to **within one pixel** for four legs running -- (445,
1435), (445,1467), (453,1467), (453,1475) against wanted 444/1436, 444/1468,
452/1468, 452/1476.

Adding slack to be safe is the natural instinct and it was the whole bug.

## And leg 5 still fails, identically, both times

    leg  4  Right  want (972,1476)  got (973,1475)
    leg  5  Up     want (972,1228)  got (973,1473)

The player is on the planned row to the pixel, holds `Up`, moves **two pixels**,
and stops. Same cell, same result, with and without the correction bug -- so
this was never a route-following problem.

Three explanations were checked and all three are wrong:

  * **The planner and the game disagree about the map.** They do not.
    `zamn_assets verify-level` on this level compares the expanded map, both row
    tables, the block library, the tile attributes and all three palettes
    against WRAM: **15 checks, 0 failed.** Same data, byte for byte.
  * **Some other attribute mask stops the player.** `src/assets/level.h` records
    two footprint tests the planner does not model -- `$80:AF2C` on bit 2 and
    `$80:AF66` on bit 12. But all **19** `JSL $80AF2C` sites are in banks $81,
    $82 and $83, the actor and boss banks; none is in bank $80, so it is not on
    the player's movement path.
  * **An object is standing in the passage.** Level 30's object list has 22
    entries and **none** is within (900..1060, 1300..1500).

So: the planner models the game's box exactly, off the same bytes, with no
object in the way and no second mask involved -- and the player still cannot
enter the cell. **What stops it is not yet known**, and that is the finding.

## Why this is worth writing down rather than pushing through

`psn_t3_expired` is one coverage site, and the route to it now has a repro that
fits in a sentence: **stand at (973,1475) in level 29 and hold Up.** The player
moves two pixels and stops, where `zamn_assets route 30` says the way is open
for 248 more. That is a much better thing to hand the next round than another
hour of leg-tuning, because whatever explains it is a fact about the game that
every future route inherits -- the movie comments have been describing routes
that "drift" for a dozen rounds, and at least one of those may be this instead.

Neither follower's movie was kept. Both ended nowhere near the item, and a movie
whose header describes reaching it would be worse than none.

**Nothing in the tree changed for this entry.** The corpus is still 11,094,719
calls across 43 movies with 87 sites untaken; `psn_t3_expired` is still one of
them.

## The number a session reports about itself

Eighty-two routines is a number with no denominator attached. The per-routine
table says each of them worked; it cannot say what fraction of the game that
*is*, and it cannot tell a 17-byte leaf from a 2 KB state machine. That question
had one answer -- `tools/native_share.py`, offline, over a traced profile -- and
it could not be asked of the thing anybody actually does with the port, which is
play it. So the harness now measures it as it runs, and `zamn.exe` and
`zamn_cosim run` both print it when they exit.

Two rows, because there are two honest questions:

```
  work        296,719,746 of 654,074,274      45.4%
  calls            55,225 of 318,792          17.3%
```

**work** is SNES cycles. Native mode already burns a measured cycle budget in
place of every substituted routine -- `CosimRoutine::cycles`, the mean `verify`
reported for it -- so the numerator is not a new estimate bolted on for
reporting: it is the same number the rest of the machine was advanced by, and
`cycles_burn()` records the core's own delta rather than the budget, because
`snes_runCycles` adds 40 for a DRAM refresh when the burn crosses a scanline.

**calls** is `JSR`/`JSL`/`JSR (abs,X)`, counted at the instruction that executes
one. Substitution happens at the *callee's* entry PC, so the caller's `JSR` has
already run by then and is in the denominator whether the port served the call
or not; `calls_native` is a subset, never a second bucket to add.

Both denominators shrink as the port grows, and that is the property that makes
either ratio mean anything: **a call made inside a substituted routine never
executes at all.** A routine that used to contribute its own call plus six of
its callees' contributes one once it is ported, and that one is served.

### What comes out of the denominator, and why

Two kinds of cycle are not work and neither is in it:

  * **Halted.** The scheduler's `WAI` -- `$80:8371`, the same instruction
    lockstep synchronises on. The CPU is executing nothing, waiting for the NMI
    that starts the next frame. This is not a small correction: on level 1 at
    2,400 frames it is 485,200,770 cycles against 282,329,364 of work.
  * **Spinning.** The ten loops in `src/cosim/waits.h`. Porting a spin gives a
    spin -- the C would have to wait on the same flag -- so counting them would
    make the port's share look smaller than it is for no reason anyone could
    act on.

That table used to live in `tools/native_share.py` and now lives in C, because
the offline tool and the running game must not disagree about a denominator.
The Python reads the header.

What is *not* in the call denominator, and is in the work one, is the family
`native_share.py` documents at length: a thread body and a vblank job are
entered by `RTL` from a parked frame or a queue, so there is no call to
intercept and none to count. Their cycles are real and stay where they are.

### Two independent measurements, and what they cost to reconcile

`tools/native_share.py` measures the same quantity from the other end --
instructions from a traced profile, attributed by nearest preceding entry, with
a call-graph closure over what the registry subsumes. Nothing about that method
touches the substitution seam. Getting the two to agree took one real fix, and
the disagreement was worth having:

The first comparison was **62.3% offline against 23.6% live**, on the same movie.
The gap is entirely `verify_only`. `native_share.py` read the registry and
counted every entry in it as ported, which answers "how much of this game have
we written" -- and three of those entries are written, checked on every call,
and *never substituted*. On level 1 that is not a rounding difference:
`$80:CCC8 apu_send` alone is **23.8% of every instruction the movie executes**,
with the two LZSS leaves another 4.5%.

So the tool now closes over both sets and reports both numbers. "Written" is
still the one that says what to port next; "actually substituted" is what a run
of the port reaches, and it is the line that has a counterpart:

```
  dynamic share, waits out of the denominator:  62.3%
  ...substituted only, likewise:                24.0%   <- what the game reports
```

**24.0% offline, 23.6% live** -- one counting instructions from a profile, the
other counting cycles at the seam, with no shared code between them but the wait
table. They will never agree to the decimal and should not be made to: a cycle
is not an instruction, and the live numerator is a per-routine mean where the
offline one is a per-instruction count.

### How strong the work row is

The numerator is a mean standing in for a distribution, which is the same
approximation that `run` already lives with -- and its size is measurable rather
than assumed. Run the same movie twice and compare the work denominators:

```
  stock       273,055,690 cycles of work
  native      282,329,364 cycles of work, of which 66,634,070 is budget
```

Substituting removed 273,055,690 - (282,329,364 - 66,634,070) = **57.4M cycles
of real ROM work** and paid 66.6M of budget for it. The budgets over-pay by
about 16%, so the row reads high by roughly that and not by a factor, and the
totals stay within 3.4% of each other. Both framebuffers are identical at
2,400 frames, which is the check that says none of this perturbed the run.

### What it says

The number depends enormously on what the game is doing, which is itself the
useful part:

| movie | frames | work | calls |
| --- | --- | --- | --- |
| `level1` | 2,400 | 23.6% | 5.8% |
| `level9-weapons` | 6,000 | 45.6% | 18.1% |
| `level29-fighting` | 6,000 | 45.4% | 17.3% |
| `level25-boss` | 6,000 | 56.9% | 24.1% |

Level 1's movie is boot-dominated -- the APU upload, the LZSS decompression and
the fades, none of it substituted -- and 23.6% is a fact about the movie rather
than about the port. **Playing a level is where the ported routines are**, and
there the port is doing between two-fifths and three-fifths of the work the CPU
does. That is the number to watch across Phase 3, and the one Phase 4 has to
take to 100%.

One trap when comparing runs: those are `zamn.exe --frames N`, which is N **PPU
frames** from power-on. `zamn_cosim run -f N` is N **scheduler passes after
boot**, so it covers more game and less boot for the same N and reads higher --
40.1% against 23.6% on `level1`, both correct about different stretches. Only
runs of the same kind belong side by side, and the offline tool follows the
tracer, which counts PPU frames.
