# Progress Tracker

Cross-session status for the ZAMN native-port project. Update this whenever a
milestone lands. See `PLAN.md` for the full multi-phase plan.

## Current status: **Phase 3 underway** 🔨 (2026-07-25)

**A movie was written to look at one line, and the line fell on its first
call.** `movies/level1-pickups.zmv` picks up **two items rather than one**, and
that is the whole of it: 2,400 frames, nothing fired, nobody rescued, **41,233
of 41,233 calls checked with nothing declined**. Every input that existed before
it picked up exactly one item and it was id `$0C`, the *first* — so
`player_pickup`'s `SEC : SBC #$0018` produced 0 whichever way you spelled it,
and `index / 2 - PICKUP_ID_FIRST`, which is off by a factor of two on every
other id in the game, **passed all 138,513 calls**. This page called that the
highest-value item on its own list and it was right.

**Aiming an input at a particular branch needed the object table, not the
level.** A level's object list gives x, y and a type byte, and the type is an
index — already doubled — into a 30-word table at **`$80:CA30`** that is what
gives the object's display record its **collision id**. `$80:C9E3` reads it once
for the id and once, at `$80:CA6C`, for the metasprite. With that, level 1's
nine objects stop being scenery and become a shopping list: object 6 at
(251,302) is id `$12`, a pickup whose slot is 6; object 0 at (307,135) is id
`$0C`, slot 0; objects 1/2/3 are keys, id `$21`, whose jump-table entry is not
ported and would come back as declines. So the movie is a *route* — over 6, then
over 0, and nowhere near the keys.

**Two of the three open questions are now settled, and the third was named in
advance.** Object 6's slot is `$0C` the right way and `$06` the wrong way, so
the wrong one fails on **call 1** at `$7E:1CD3`, the high byte of the
neighbouring inventory word — because slot `$06` also reads the wrong entry out
of the amount table (`$0300` where `$0020` belongs). And slot 0 starts the level
at `$0150` while object 0 pays `$0099`, so the tens digit carries:
`pickup_digit_carry` is taken for the first time, deleting that line of
`bcd_add16` fails on **call 2** at `$7E:1CCC` (ROM `$49`, port `$E9`), and the
same perturbation still passes call 1, whose `$0000 + $0020` needs no
correction. The finding is legible on the HUD: the ammo counter reads 150 before
and **249** after. The third, the `$0999` ceiling, still passes when deleted —
and `pickup_capped` reads zero on all five movies, which is the report saying so
rather than nobody noticing.

**Coverage: 34 of 88 for this movie, and the union goes 59 → 61.** It is the
smallest single contribution of any input and it holds two sites alone
(`pickup_taken`, `pickup_digit_carry`), which is the argument for writing narrow
movies rather than long ones. Twenty-seven sites are now untaken by every input
that exists, down from twenty-nine. `run` compares 2,389 of 2,389 scheduler
passes with at most 33 bytes differing at once, all inside the stacks or a
declared scratch byte, and both pickups happen identically on the native side.

---

*The rest of this section is the previous round, kept as written.*

**`$80:CCC8` is registered in its own right, and it more than doubled the
harness's sample.** `apu_send` was checked only through `apu_play_sfx` — a few
hundred calls from a handful of sites, all sending the same command — while the
same eleven instructions are the inner loop of the data-set uploader, which puts
**23,820 commands** through them on every movie. That was the top code item on
this page's own list. It needed no new port code, only a signature saying what
the arguments are, and it passes **23,632 of 23,632 on `level1.zmv` first run**.
Twenty-two routines now, and `verify` checks **40,239 / 130,048 / 150,170 /
162,510** calls across the four movies — up from 16,607 / 106,392 / 126,191 /
138,513 — with a zero in every `decl.` column and no census section printed
anywhere.

**The reason it waited was recorded here, and it was wrong.** The routine has two
kinds of caller and they disagree about register width: 16 bits from
`apu_play_sfx`, 8 from the uploader. `CosimRegs` has no width field, so this page
said intercepting there would be comparing high bytes that mean nothing. It does
not — and the reason is the routine's own first instruction. `SEP #$30`
normalises everything the contract depends on: `A` is only ever *read*, so the
whole 16-bit register comes back as it went in (junk high byte included, and on
the uploader's path it *is* junk — the top half of a pointer word left over from
`$80:CC84`); the index registers have their high bytes **cleared by that very
`SEP`**; and `Y` is loaded fresh from one byte of WRAM. No width field was
needed, and the port takes `A` and `X` full width, which is what the ROM's
calling convention actually says.

**A wrap bug fell out that no sound effect could have found.** Returning a
16-bit `seq + 1` instead of the byte passes 251 calls and fails on **call 256** —
`Y: ROM $0000, port $0100`. The counter is one byte, it wraps, and the only thing
in the game that goes round it is an uploader sending 23,820 commands in a row,
which goes round it ninety-three times. Three more perturbations were caught on
**call 1**: never writing the counter back (`$7E:001E`), swapping the command and
the parameter (caught by the *bus* diff, which nothing else in the harness could
see), and taking N/Z from the pre-`INY` counter. Plus carry, also call 1.

**One was not caught, and it is the honest limit of the width argument.**
Deleting the `x & 0xff` mask passes every call on every movie: the six call sites
are `LDX #$0001`, `#$0002`, `#$0006`, `#$0008`, `#$000A` and `#$0013`, every one
a small constant whose high byte is already zero. The one place register width
could still have shown through is a place no caller ever puts anything. Not a
branch, so no mark can express it; written down in `src/port/apu.c` beside the
line, like the `STZ $7E` in `enemy_die` and the doubled-id index in
`player_pickup`.

**And the registry grew a field, because substituting it does not work.** With
`apu_send` substituted, `run` desynchronises the two cores inside the first ten
scheduler passes — `$7E:0016`, the NMI frame counter, reads `$02DC` stock against
`$0595` native. The first guess was the cycle budget, and it is true that no
single figure fits 218..85,184; it is also beside the point. **The spread is not
work a budget should pay for — it is the SPC700 deciding how long the 65816 has
to sit still.** So the wait now burns the machine's time (`snes_runCycles`)
rather than the APU's, which is right on its own terms: fast-forwarding the APU
alone moves it without moving `snes->cycles`, putting it on a different clock
from everything else. **It still deadlocks**, and the instrumentation says why in
one line: during a driver upload the SPC sits in the driver's own RAM-clear init
loop at `$0672`, and it was watched running **12,959 of its own cycles** there
without answering, where the ROM's spin at the same point takes about a thousand.
The machine is advancing; the CPU is not, and the CPU is what the rest of the
machine is waiting for. **A routine whose body is a bus handshake cannot be
substituted from inside it**, because substitution's whole mechanism is to stop
the 65816 executing. So `verify_only` says *checked per call, never substituted*,
and the report prints `verify only` where a verdict goes. It is not a decline —
nothing was offered, so nothing is claimed and `decl.` stays honestly at zero.
`apu_play_sfx` is still substituted, which is the same fact from the other side:
a lone sound effect finds the SPC caught up from sounds ago, so its wait is
satisfied by the first read and no spin happens — which is also why its budget is
now the measured *minimum*, 484, rather than a mean. None of this constrains
Phase 4: the finished port owns its main loop and can spin on `$2143` as the ROM
does. What it cannot do is spin while impersonating one instruction inside
somebody else's core.

`run` is otherwise unchanged — twenty-one of the twenty-two substituted over
2,389 / 6,089 / 5,989 / 5,989 scheduler passes, at most 18 / 31 / 34 / 35 bytes
differing at once, every one inside the stacks or a declared scratch byte, and no
byte of live game state ever differing. Branch coverage is unchanged at **88**
sites with a union of **59**, because `apu_send` has no branch of its own to
mark: its one decision point is the wait, and the wait belongs to the host.

---

*The rest of this section is the previous round, kept as written.*

**The APU decision is made, and with it the decline census is empty.**
Twenty-one routines are ported, and `zamn_cosim verify` on
`movies/level1-2p-rescue.zmv` now checks **138,513 of 138,513** calls with a
**zero in every `decl.` column** — no call anywhere on any of the four movies is
handed back to the ROM, and the report prints no census section at all. That was
the top item on this file's own list and it was the only one that needed a
design decision rather than an input.

**How port code drives the APU** (`src/port/apu.h`). `$80:CCC8` is split three
ways: what it does to *memory* is one byte (`W_APU_SEQ`, `$7E:001E`) and is
ordinary port code; what it puts on the *bus* goes out through `ApuPorts`, a
hook the host installs once; and the **wait** — the `CPY $2143 : BNE` the CPU
spins on until the SPC700 acknowledges — is the host's, because the port has no
way to advance an SPC700 and no business trying. That split is what leaves
`apu_send()` a pure function of WRAM plus two arguments, so `apu_play_sfx`
(`$80:CC3B`) is intercepted, rewound and diffed like everything else: **371 of
371 calls** on the new movie, from six unrelated call sites. It costs one global,
and `port/apu.h` argues the case: the harness needs two copies of WRAM and
routinely has them, and there is never a reason to have two APUs.

**The ports are compared too, and that is not a formality.** A port that
computed the sequence counter correctly and sent *nothing at all* would pass
every byte of every diff, because the ports are not memory. So the harness
watches `$80:CCD1  STX $2142` — the ROM's own instruction, command in X and
parameter in A — and diffs the window of sends inside each call against what the
port asked for. Suppressing the port's send is then caught on **call 1**, where
before it would have passed 16,607 of 16,607 and silenced the game.

**A flag finding fell out of it.** `$80:CC3B` ends `REP #$30 : PLD : RTL`, and
**`PLD` sets N and Z from the direct page it restores** — so what a caller gets
back describes *its own* direct page, not the `INY` that is visibly the last
arithmetic in the routine. Taking the flags from the `INY` fails on N at call 1.
First ported routine whose output depends on a register the caller never thought
it was passing.

**Both blocked jump-table entries are ported.** `$80:F92D` is three
instructions — the smallest thing ever to sit on this file's work list.
`$80:F87B` is
the player's side of a pickup — the other half of last round's `object_collide`
— and the game's **second decimal routine**: the item's collision id, doubled,
minus `$18` is an inventory slot; the amount comes from a 21-word BCD table at
`$80:F8AC`; the counter is capped at `$0999`. The inventory is
`W_PLAYER_INVENTORY`, fourteen words per player at `$7E:1CCC` stride `$20`.

**And the tail call is where the round stopped being about audio.** A player who
picks something up holding no weapon falls through `JMP $80:EA63`, which selects
one for them — and that is the path the single pickup in the whole corpus
actually takes. So `$80:EA63` is ported too (`src/port/player.h`, a new file
because this is not collision code), registered in its own right because
`$80:D267` calls it as well.

**That second caller is a correction to something recorded here.** An earlier
round established that `Y` is the fire button and concluded that `B` "does
nothing". **`B` cycles weapons.** `$80:D259  LDA $1A : AND #$8000 : ... : JSR
$EA63` is the player's input handler edge-detecting it. It was invisible twice
over: the experiment that established it was a 120-frame *hold* and the handler
takes an edge, and a player carrying one weapon cycles to the weapon they are
already holding — whose first exit is `CMP $1CBC,X : BEQ`, no store and no
sound. `docs/analysis-tools.md` carries the correction.

Nine deliberate perturbations, six caught and three not, and the split is the
sharpest yet. **The six were each caught on their first call:** sending nothing to the APU, sending the sfx id + 1,
taking N/Z from the `INY`, starting the weapon search at the slot already held
(`$7E:012E`, which is how the player's page is known to be based at `$7E:0100`),
giving the search fourteen tries instead of fifteen, and never writing its
countdown back. The fourteen-tries one failed at **`$7E:001E`** — the APU
sequence counter — because one try short of a full lap the search finds nothing,
decides the weapon changed, and plays a sound the ROM did not. Two subsystems
ported an hour apart, and the second is what caught the first.

**Not caught: three, and all three are one pickup.** Dropping the `$0999`
ceiling and breaking the BCD decimal adjust both pass every call, which
`pickup_capped` and `pickup_digit_carry` said in advance by reading zero.
**Indexing the amount table by the id rather than the doubled id also passes,
and nothing names that one** — the item picked up is id `$0C`, the first, so its
slot is 0, and every wrong way of computing zero is also zero. It is not a
branch, so no coverage mark can express it; it is written down in
`src/port/collide.c` beside the line, the same defence the `STZ $7E` in
`enemy_die` gets.

There are **88** marked sites now, up from 75, and the union across the four
movies is **59** — 26, 42, 50 and 56 individually. `run` substitutes all
twenty-one routines on all four, comparing 2,389, 6,089, 5,989 and 5,989
scheduler passes with at most 18, 31, 34 and 35 bytes differing at once — every
one of them inside the stacks or a declared scratch byte, and no byte of live
game state ever differing.

---

*The rest of this section is the previous round, kept as written.*

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
  `movies/level1-2p.zmv`, `movies/level1-2p-rescue.zmv` — reproducible input
  scripts. `level1.zmv` reaches
  actual gameplay; `level1-rescue.zmv` goes on to rescue a victim and get mobbed
  in the graveyard, which is what makes actors touch; `level1-2p.zmv` is the
  first that uses **controller 2** and the first in which anything is actually
  **fired** (`Y`, not `B` — see the status section); `level1-2p-rescue.zmv` has
  the **second** player do the rescuing and then plays the two of them as a pair
  rather than counter-phase, which is what reaches `victim_claim_b`,
  `collide_none` and the object manager's handler; `level1-pickups.zmv` is the
  narrowest of the five and exists for one line of one routine — it walks Zeke
  over **two** of level 1's objects, ids `$12` and `$0C`, which is the only way
  the port's slot arithmetic and its BCD adjust can be told apart from wrong
  versions of themselves. A frame may carry a `2:`
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
  callback dispatcher that installs a thread's own direct page, plus **all five
  handlers a collision on the current movies reaches**: `$80:F7F7` with its
  `$80:F950` hit path, `$81:8888` with its death path at `$81:889F`/`$81:8727`,
  `$81:FE0E`, **a weapon shot's handler** and the first ported code that writes
  the display list from inside a handler, `$83:A364`, **a victim's** — eight
  ids, eight endings, latched on the first one to arrive — and `$80:CAEE`, **the
  object manager's**, which is the first that is not an actor reacting on its own
  behalf: every object in the level shares one thread, so the handler's direct
  page is the manager's and the touched object arrives in `W_HANDLER_SELF`, to be
  switched off and queued for a later pass rather than reacted to here. Plus
  two more of the player's jump-table entries, both of which used to be blocked
  by the APU: `$80:F92D`, three instructions of pure sound effect, and
  `$80:F87B`, **the player's side of a pickup** — a BCD add into an inventory
  counter, capped at `$0999`, which tail-calls into the weapon selector when the
  player is holding nothing. It also names twenty-two
  direct-page fields those routines touch, across four different pages that
  disagree about every offset), `apu.c`/`.h` (**how port code drives the APU** —
  `$80:CC3B` and `$80:CCC8`, the first port code that touches hardware, and the
  three-way split between the byte of WRAM it owns, the bus traffic it hands to
  a host hook, and the wait it does not attempt. `$80:CCC8` is registered on its
  own entry as well, which is what puts the data-set uploader's **23,820
  commands per movie** under the diff, and it is the one routine in the registry
  marked `verify_only`: its body is a bus handshake, and the thing that performs
  a handshake is the CPU), `player.c` (**the player's
  weapon selection** — `$80:EA63` and `$80:EA4B`; the first ported code that is
  neither collision nor per-frame housekeeping, and the reason `B` is now known
  to cycle weapons), `bcd.h` (the 65816's decimal `ADC`, which stopped being the
  score's private business when the pickup arrived), `score.c` (**the score** —
  `$80:C7D9` and the slot search at `$80:C7C2`), `coroutine.h` (**how a ported
  routine suspends** — one `resume`
  index plus a context struct, ~40 lines), `fade.c` (`$80:891A`, the first
  routine ported that uses it) and `coverage.h`/`.c` (**which of the port's
  branches any input has actually taken** — 88 marked decision points across
  twenty-one of the twenty-two routines (`apu_send` has no branch of its own:
  its one decision point is the wait, and the wait belongs to the host); with `PORT_COVERAGE` undefined every mark compiles to
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
  the ROM went to instead, so the `decl.` count comes with a work list. A routine
  may instead declare `verify_only` — checked per call, never substituted — which
  exactly one does, and the report prints `verify only` where a verdict goes so
  the exclusion is visible rather than silent.
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
  what a second controller reached that nothing else could, and — *The uploader's
  23,820 calls* — why register width turned out not to matter, what a sample that
  size caught that a small one could not, and why a routine whose body is a
  handshake can be verified but not substituted — and, *Two pickups, two items*,
  how a movie gets aimed at one line of one routine when the thing that line
  gets wrong is an index rather than a branch, so no coverage mark can name it.
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
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1-2p-rescue.zmv -f 6100 -c
build\zamn_cosim.exe run "Zombies Ate My Neighbors.sfc" -m movies\level1-2p-rescue.zmv -f 6000
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1-pickups.zmv -f 2400 -c
build\zamn_cosim.exe run "Zombies Ate My Neighbors.sfc" -m movies\level1-pickups.zmv -f 2400
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400 -r apu_send -v
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
  project. `movies/level1-2p-rescue.zmv` widens that again without any new
  code: **39,002 of 39,002** emissions byte-identical (30,800 unflipped, 7,887
  flip-x, 315 flip-y) across **166** distinct metasprites, the most of any
  movie — two players roaming as a pair see more of the level's art than two
  players running in opposite directions do.
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

- Phase 3 (**two pickups, two items**): `movies/level1-pickups.zmv` checks
  **41,233 of 41,233** calls with **nothing declined**, and it exists to make
  three lines of `player_pickup` checkable that no input could reach before.
  Every earlier movie contains exactly one pickup and it is id `$0C`, the first
  item, whose slot is 0 — so three perturbations passed. Two of them now fail.
  The route was derived rather than searched for: an object's type byte indexes
  **`$80:CA30`**, which gives its display record's collision id, so level 1's
  object 6 at (251,302) is id `$12` (**slot 6**) and object 0 at (307,135) is id
  `$0C` (slot 0), while objects 1/2/3 are keys at id `$21` whose jump-table
  entry is unported — a route, not a wander. Non-vacuous two ways, and the split
  is the point. **Call 1**: `index / 2 - PICKUP_ID_FIRST` instead of `index -
  PICKUP_ID_FIRST * 2` fails at `$7E:1CD3` — the *high* byte of the neighbouring
  inventory word, because slot `$06` also reads `$0300` out of the amount table
  where `$0020` belongs. **Call 2**: deleting the tens-digit decimal adjust in
  `bcd_add16` fails at `$7E:1CCC`, ROM `$49` against port `$E9`, because slot 0
  starts the level at `$0150` and object 0 pays `$0099` — visible on the HUD as
  the ammo counter going 150 → 249 — while call 1's `$0000 + $0020` still
  passes. **Not caught, and named in advance:** dropping the `$0999` ceiling
  passes every call on every movie, and `pickup_capped` reads zero on all five.
  Coverage is **34 of 88**, the smallest of any single movie, and it holds
  `pickup_taken` and `pickup_digit_carry` alone; the union across the five goes
  **59 → 61**. `run` compares 2,389 of 2,389 passes, at most 33 bytes differing
  at once, all inside the stacks or a declared scratch byte.
- Phase 3 (**the uploader's 23,820 calls**): `apu_send` (`$80:CCC8`) is
  registered on its own entry, which puts the data-set uploader's inner loop
  under the diff — **23,632 of 23,632** on `level1.zmv` first run, and 23,656 /
  23,979 / 23,997 on the other three, against `apu_play_sfx`'s few hundred. It
  needed no new port code, only a signature saying the arguments are `A` and `X`
  full width. The register-width objection this file recorded for several rounds
  is answered by the routine's own `SEP #$30`: `A` is only read (so the whole
  16-bit register returns as it arrived, junk high byte included), the index
  registers have their high bytes cleared by that very instruction, and `Y` is
  loaded fresh from one byte of WRAM. Twenty-two routines now, and `verify`
  totals are **40,239 / 130,048 / 150,170 / 162,510** across the four movies.
  Non-vacuous six ways. Four on **call 1**: never writing the counter back
  (`$7E:001E: ROM $01, port $00`), swapping the command and the parameter (`APU
  command 0: ROM $08/$01, port $01/$08` — caught by the bus diff, invisible to
  memory), taking N/Z from the pre-`INY` counter, and returning carry clear. One
  on **call 256**: returning a 16-bit `seq + 1` rather than the byte passes 251
  calls and then fails `Y: ROM $0000, port $0100`, because the counter is one
  byte and only an uploader sending 23,820 commands in a row goes round it — 93
  times. **One was not caught**: deleting the `x & 0xff` mask passes every call
  on every movie, because the six call sites are all `LDX #<small constant>` with
  a zero high byte, so the one place width could still have shown through is a
  place no caller ever puts anything. Written down in `src/port/apu.c`.
  **It is also the first routine marked `verify_only`.** Substituting it
  desynchronises `run` inside ten scheduler passes (`$7E:0016`, the NMI frame
  counter: `$02DC` stock against `$0595` native). The cycle budget is not the
  cause and cannot be the fix — 218..85,184 is the SPC700 deciding how long the
  65816 waits, not work — so `apu_drive`'s wait now burns the machine's time
  rather than the APU's, which is right on its own terms and still deadlocks:
  during a driver upload the SPC sits in its own RAM-clear init loop at `$0672`
  and was watched running **12,959 of its own cycles** there without answering,
  where the ROM's spin takes about a thousand. The machine advances; the CPU does
  not, and the CPU is what the rest of the machine is waiting for. A routine
  whose body is a bus handshake can be verified per call but not substituted from
  inside one. `apu_play_sfx` still is, because a lone sound effect never waits at
  all — which is why its budget is now the measured minimum, 484.
- Phase 3 (**the APU decision**): `apu_play_sfx` (`$80:CC3B`) is ported, and
  with it the two jump-table entries that were waiting on it — `$80:F92D` and
  `$80:F87B` — plus the weapon selector `$80:F87B` tail-calls into,
  `$80:EA63`/`$80:EA4B`. Twenty-one routines now, and **no call declines
  anywhere on any movie.** `apu_play_sfx` passes **371 of 371** on
  `movies/level1-2p-rescue.zmv` from six unrelated call sites, and
  `weapon_select_next` passes 3 of 3 there, 2 on `level1-2p.zmv` and 1 on each of
  the other two. The APU's *bus traffic* is compared as well as its one byte of
  WRAM (`$7E:001E`), because a port that sent nothing at all would otherwise pass
  every diff: the harness watches `$80:CCD1  STX $2142` and diffs the window of
  sends inside each call against what the port asked for.
  Non-vacuous six times, every one on the first call: sending nothing (`the ROM
  sent 1 command, the port 0`), sending the sfx id + 1 (`ROM $01/$31, port
  $01/$32`), taking N/Z from the `INY` rather than the `PLD` that follows it
  (`flag N: ROM 0, port 1`), starting the weapon search at the slot already held
  (`$7E:012E`), giving it fourteen tries instead of fifteen (`$7E:001E` — the
  APU counter, because a short lap finds nothing and plays a sound the ROM did
  not), and never writing its countdown back (`$7E:012E`).
  **Three were not caught and all three are one pickup.** Dropping the `$0999`
  ceiling and breaking the BCD decimal adjust are both named in advance by
  coverage sites reading zero (`pickup_capped`, `pickup_digit_carry`).
  Indexing the amount table by the id rather than the *doubled* id is not named
  by anything: the one item ever picked up is id `$0C`, so its slot is 0 and
  every wrong way of computing zero is also zero. Written down in
  `src/port/collide.c`, because it is not a branch and no mark can express it.
- Phase 3 (**the object manager**): `object_collide` (`$80:CAEE`) is the fifth
  handler and the nineteenth ported routine. On
  `movies/level1-2p-rescue.zmv` it is reached once and passes whole-WRAM and
  registers first try, inside a run of **138,129 of 138,129** calls with nothing
  diverged. Non-vacuous three ways, each caught at `$7E:1A10` — the touched
  object's `ACTOR_COLLIDE_ID` in the display list, which is also how the
  object's own collision id is known to be `$0C`: dropping the `STZ` that
  switches the object's collision off, advancing the queue cursor by 4 instead
  of 2, and queueing `W_HANDLER_OTHER` instead of the object. All three took
  `thread_call_handler` down with them, from 11,163 passing to 8,392. **A fourth
  was not caught and is the finding:** deleting the entry guard — letting an
  object already in the queue be queued a second time — passes every call on
  every movie, because nothing has ever touched a spent object. Recorded in
  `src/port/collide.c` and named by the `object_spent` coverage site.
- Phase 3 (verify): `zamn_cosim verify` replays a movie and checks each of
  the twenty-two ported routines against the ROM's own on **every call the game
  makes** — the whole 128 KB of WRAM plus A/X/Y and the flags each shim claims.
  On `level1-2p-rescue.zmv` — two players moving as a pair, the second of them
  doing the rescuing — **162,510 of 162,510 pass and nothing declines at all**.
  It is the only
  movie that reaches `collide_none` (2,277), `object_taken`, `victim_claim_b`,
  `pickup_autoselect` and `weapon_none_held`, and the only one where every score
  award lands in slot 1. The other three movies also decline nothing: **40,239**
  on `level1.zmv` (2,400 frames), **130,048** on `level1-rescue.zmv` (6,100) and
  **150,170** on `level1-2p.zmv` (6,000).
  (Those four totals were 138,513 / 16,607 / 106,392 / 126,191 before `apu_send`
  was registered on its own entry; the whole increase is the data-set uploader's
  23,820 commands per movie, which had never been diffed call by call. Every
  figure quoted below this line is from before that, and none of the routines it
  describes changed.)
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
sprite pass is native, the APU question is answered (`src/port/apu.h`), the
collision path is served end to end for every outcome any input has produced,
twenty-two routines are through it — twenty-one leaves and one that suspends —
and how much of them any given input actually exercises is measured rather than
guessed at. What is left is scale rather than shape.

One of the twenty-two is verified but not substituted, and that is the newest
thing on this page worth carrying forward: **`$80:CCC8`'s body is a bus
handshake**, and substitution's whole mechanism is to stop the CPU executing, so
`run` leaves it to the ROM and says `verify only`. It is not a limit on Phase 4 —
the finished port has its own main loop and can spin on `$2143` as the ROM does —
but it is the first time a routine's *shape* rather than its coverage decided
what the harness could claim about it.

**Nothing declines.** The census is empty on all five movies, which means the
work list is now made entirely of *inputs* rather than of decisions: every item
below needs a movie that does something no movie does yet. That will not last —
the count goes back up the moment an input reaches `$81:8506` or one of the two
special collision ids — and that is the instrument working rather than failing.

`movies/level1-pickups.zmv` is what an item on this list looks like when it is
done, and it is worth reading for the method rather than the result: the gap it
closed was not a branch but an *index*, so the coverage report could not name
it, and the route that closed it was derived from `$80:CA30` — an object's type
byte says what its collision id will be, so the level's placement data can be
read as "which of the port's branches can this level reach at all". The same
reading is available for the two ids below and for `$81:8506`.

1. **The rest of what a collision does.** Every collision on the current movies
   is served whole, including a pickup and the weapon selection under it.
   What remains is a short, named list rather than a subsystem —
   which is the whole point of having gone through the door with a guard rather
   than around it — and none of it is a WRAM problem except the first. In rough
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
   * ~~**A second pickup, of anything but the first item.**~~ **Done** —
     `movies/level1-pickups.zmv`. It settled three of the four things this item
     listed: `pickup_taken` and `pickup_digit_carry` are taken, and the slot
     arithmetic no longer agrees with the wrong version of itself (it fails on
     call 1). `pickup_capped` is still untaken and its perturbation still
     passes; a `$0999` counter is not reachable by any route through level 1,
     so it moves to the long-session list below. The route came out of
     **`$80:CA30`**, the object type → collision id table, which is now
     documented (`docs/asset-formats.md`) and is what makes "walk over the
     object whose id is `$12`" a thing you can plan rather than stumble into.
   * **`$81:83C6` and `$81:847E`** — the two collision ids (`$5D`, `$5E`) with
     routines of their own. Nothing has ever reached them, so what they are is
     still an open question.
   * **The untaken branches** the coverage report names inside what is already
     ported — `hurt_state_immune`, `hurt_weapon_immune`,
     `enemy_no_damage`, `shot_expire_zero`, three of the five `score_add`
     sites, five of `victim_collide`'s nine, two of `object_collide`'s three
     and one of `player_pickup`'s four (`pickup_capped`; `pickup_taken` and
     `pickup_digit_carry` are done and `pickup_autoselect` was never on this
     list).
     Each is transcribed from the
     listing and waiting for an input, not
     for code. (`player_ignore`, `player_no_effect` and `score_slot_1` were on
     this list until `movies/level1-2p.zmv`; `collide_none` and `victim_claim_b`
     until `movies/level1-2p-rescue.zmv`.)
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
   The five movies take **26**, **42**, **50**, **56** and **34** of the **88**
   sites that exist now, and they are complementary rather than ordered, so the
   number that matters is their **union: 61 of 88**. `level1-2p-rescue.zmv` is
   the richest single input and holds six sites alone (`collide_none`,
   `object_taken`, `victim_claim_b`, `pickup_autoselect`, `player_pickup_entry`,
   `weapon_none_held`); `level1-pickups.zmv` is the poorest and holds two
   (`pickup_taken`, `pickup_digit_carry`), which is the case for writing narrow
   movies aimed at a named site rather than long ones;
   `level1-rescue.zmv` holds exactly one (`victim_claim_a`,
   and only because the two-player movie deliberately has Zeke stand still);
   `level1.zmv` and `level1-2p.zmv` hold none and are kept as regression
   baselines rather than as coverage. **Twenty-seven are untaken by
   every input that exists**, in rough order of how gettable they look:
   * `pickup_capped` — what is left of the three that were here last round.
     `pickup_taken` and `pickup_digit_carry` went to
     `movies/level1-pickups.zmv`; this one wants a counter at `$0999`, and
     level 1 pays `$0099` twice into one that starts at `$0150`, so no route
     through it gets there. A long session, not a better path.
   * `victim_latched` and `object_spent` — the same untaken branch in two
     routines, and the sharpest example in the project of what a diff cannot
     see. Both are entry guards against acting on something already dealt with,
     both are the whole design of the routine they open, and **deleting either
     one passes every call on every movie**, because nothing has ever dispatched
     twice to the same victim or touched a spent object. The input that takes
     `victim_latched` is two players reaching one victim inside a single
     `actor_overlap_pass` *with the victim as the outer record* — the outer
     record's collision id is read once and cached, so both dispatches see the
     pre-claim state. `movies/level1-2p-rescue.zmv` tries for this and does not
     get it; whether the victim sorts outer is a property of the depth sort
     rather than of the input, which is what makes it fiddly rather than hard.
   * `victim_ignore`, `victim_event_2`, `victim_event_4`,
     `victim_keep_id`, `object_ignore` — the rest of those two chains.
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
     gets the last two for free. The adjust itself is no longer transcribed —
     `port/bcd.h` is shared with the pickup, and
     `movies/level1-pickups.zmv` carries the tens digit on the second pickup, so
     breaking it now fails at `$7E:1CCC` — but `score_add`'s own use of it is
     still unreached, which is a smaller claim than the one this line used to
     make.
   * `sort_key_first` (`ACTOR_SORT_FIRST`) and `draw_attr_set`
     (`ACTOR_ATTR_SET`) — two of the four originally hand-found gaps, both
     needing a record type level 1 does not spawn. They are now the two
     longest-standing untaken sites in the project.
   * `queue_full`, `emit_oam_full`, `draw_oam_full` — all three want a scene
     busy enough to saturate a queue or all 128 sprites. A boss, probably.
   * `draw_no_meta`, `draw_bad_bank` — defensive branches in `sprite_build_oam`
     that a well-formed record may never reach at all.
     (`draw_empty_meta` was here until the two-player movie hit it 1,272 times,
     and `collide_none` until `movies/level1-2p-rescue.zmv` hit it 2,277 — which
     is twice over the reminder that "defensive" and "unreachable by design" are
     guesses until they are not.)
   * `handler_unported`, `player_unported` and `collide_unported` — the three
     **decline** sites, and the only ones on this list that would be a step
     backwards to take. All three are untaken for the first time, because
     nothing declines anywhere. A decline site is a site like any
     other, so porting a routine shrinks the coverage number; that is the
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
  *that movie made*, not that it can serve every call. **Every movie now
  declines nothing at all**, which makes that distinction the whole of what is
  left to say: the census is empty and the coverage report is not. `zamn_cosim
  verify -c` reports which of the port's
  88 marked branches an input reached, and twenty-seven are untaken by every
  movie that exists. When a decline does come back, it says **where the ROM went
  instead**, by address, so what is missing is a work list rather than a count.
  See
  `docs/cosim.md` → *Half a routine, honestly*, *Through the door*, *The last
  decline*, *Declined to what: the census*, *And the last address on the list:
  the victim*, *The APU: an output the diff could not see*, *The uploader's
  23,820 calls*, *Coverage the movie does not have*, *The second player
  rescues somebody* and *Two pickups, two items*.
- **A branch can be marked; a store cannot — and neither can an index.**
  Coverage says whether a line ran.
  It cannot say whether running it *changed* anything — and the death path's
  `STZ $7E` writes a word that is already zero, so deleting it passes every diff.
  The second example of this is **retired, and how it was retired is the useful
  part.** `player_pickup` turns a doubled collision id into an inventory slot;
  the one item any movie picked up was the first one, so every wrong way of
  computing zero was also zero, and no mark could express it because it is not
  a branch. Nothing about the port changed: `movies/level1-pickups.zmv` walks
  over an object whose id is `$12` instead, and the wrong spelling fails on call
  1. **An index the diff cannot see is a statement about the corpus, not about
  the instrument** — unlike the `STZ $7E`, which no input can make visible.
  Both are found by perturbation and recorded in `src/port/collide.c` beside the
  line; the floor on what "byte-identical" means is real, but it is lower than
  this entry used to claim.
- **Branch coverage counts port executions, not game events.** Under `verify` the
  port runs once per interception, so a routine reached both directly and through
  a ported caller has its sites counted once for each. That chain is six deep on
  the collision path, so a site inside `$80:F950` reading 6,125 means 1,225
  collisions, and `overlap_hit` reads 2,452 for 1,226 passes, exactly twice.
  Whether a site was reached at all, which is the only thing the report claims,
  is unaffected.
- **A marked branch is one somebody thought to mark.** The 88 sites are chosen by
  hand, one per decision the diff would have to run to check, not generated. A
  branch with no mark on it is invisible to the report, so this is a floor on
  coverage rather than a measurement of it.
- **A branch mark cannot catch a port that is too *permissive*.** Coverage says
  a line ran; nothing says a line should never have been reachable. Adding a
  fifth id to `shot_collide`'s four passes every diff, because no input produces
  it — the port would accept something the ROM rejects and the movie cannot tell.
  Same floor as the `STZ $7E` above, approached from the other side. There are
  now **two** sharpest examples and they are the same shape: **deleting
  `victim_collide`'s latch guard, or `object_collide`'s spent-object guard,
  passes every call on all five movies** — none of them ever dispatches twice to
  the same victim or touches an object already in the pickup queue, and in both
  routines that guard is the whole design. Coverage sites (`victim_latched`,
  `object_spent`) name them; nothing can check either but an input.
- **Two activations of the same ported routine at once are not distinguished.**
  A suspension is attributed to the innermost in-flight call whose body contains
  the yield's return address; if two threads were ever inside the same ported
  routine simultaneously that would be ambiguous. No routine ported so far can
  be. See `docs/threads.md` → *What is not settled yet*.
- 11 passes go uncompared by `run` on each of the five movies: one before the
  scheduler exists, and ten where a side never returned to the `WAI` within the
  step.
- **The census is capped at 64 distinct addresses.** None are in use: no movie
  declines anything. If it ever
  overflows, the report says so on its own line rather than truncating quietly —
  and that would itself be the finding, because the point of the list is that it
  is short enough to work through.
- **The APU's bus traffic is compared per call, not per run.** The harness logs
  the ROM's writes to `$2142`/`$2141` only while an interception is in flight,
  in a 64-entry ring, and diffs each call's window against what the port asked
  for. That is enough for everything ported — nothing sends more than one
  command per call — and an overflow is reported rather than truncated. Since
  `$80:CCC8` was registered in its own right, a data-set upload's 23,820 commands
  are inside an interception too, one per call, so the only traffic left outside
  the window is whatever no ported routine encloses — today, nothing.
  `zamn_assets verify-music` still checks the uploads end to end as *streams*,
  which is a different claim from per-call agreement and worth keeping.
- **A routine whose body is a hardware handshake cannot be substituted.**
  `$80:CCC8` is verified on every call — 23,632 of 23,632 on one movie — and
  `run` leaves it to the ROM, because eight of its eleven instructions are a spin
  on `$2143` and the only thing that can perform a spin is the CPU that
  substitution stops. Advancing the machine from inside the hook is not enough:
  the SPC was measured running 12,959 of its own cycles inside one such wait,
  still in the driver's RAM-clear init loop. The registry says `verify_only` and
  the report prints it; nothing is claimed about a substitution that never
  happened. `apu_play_sfx`, one level up, *is* substituted, and its wait is
  always already satisfied — a lone sound effect finds the SPC caught up from
  sounds ago — which is why its budget is the measured **minimum** (484) rather
  than a mean. Its full range is 484..**85,450**, and charging the mean would be
  billing a wait that did not happen.
- Frame pacing fixed 2026-07-22: paced by sync-to-audio, with a monotonic-timer
  fallback when no audio device (`src/main_sdl.c`).
- No gamepad mapping yet (keyboard only). No save states / config yet.
- `analysis/` and `build/` are scratch (git-ignored). `analysis/` in particular
  contains verbatim ROM bytes and must never be committed.
- Windows-only by design for now (per project scope).
