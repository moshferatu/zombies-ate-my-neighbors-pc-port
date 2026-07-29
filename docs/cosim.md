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
