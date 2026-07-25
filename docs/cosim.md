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
run to check**, not every `if`: 60 of them across the sixteen routines, chosen by
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
