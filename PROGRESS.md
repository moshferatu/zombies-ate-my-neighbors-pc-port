# Progress Tracker

Cross-session status for the ZAMN native-port project. Update this whenever a
milestone lands. See `PLAN.md` for the full multi-phase plan.

## Current status: **Phase 3 underway** 🔨 (2026-07-31)

### The creature in the sealed room (2026-07-31)

**Six of `enemy_d7f6_collide`'s seven branches had never been taken, and for four
rounds the reason was assumed to be a route nobody had cut.** `--records` found
`$81:D7F6` on level 17 at (563,513) the round that column was added, and
`zamn_assets actors` named the thing standing there — actor 11, type `$23`,
behaviour `$81:D704`. Every level-17 movie since has walked past it. The reason
none of them ever landed a shot is not the route. **There is no route**, and
saying so needed a new instrument.

**`zamn_assets route --reach` tints the level map per grid cell: green reached,
orange open but cut off, red solid.** The distinction is the whole point —
"no route" had been one answer for two situations that want opposite things, and
level 17 is the one that had been read the wrong way round:

```
 496 ######################################
 504 #######ooooooooo###ooooooooo##########
 512     ###ooooooooo###oGooooooo######
 520     ###ooooooooo###ooooooooo######
 528     ##############################
```

Two alcoves of clear floor with solid on all four sides, the creature in the
right one and the level's objects at (521,505) and (577,505) inside them too.
Nor can it be shot over the wall: standing at (586,462), the closest the player
can get, `--records` shows a `$5C` record spawn at (586,462) with handler
`$81:FE0E` and sit at (586,468) with handler `$00:0000` on the next frame — six
pixels and a splash — over and over, while the creature chases along the far side
at (586,504) with `d7f6_ignore` counting 90 and `d7f6_hit` counting nothing.

**The handler belongs to a behaviour and not to a level, and that is what turned
one sealed room into an input.** Six levels place a `$81:D704` actor — 5, 17, 29,
31, 49 and 54. Of the four a password reaches, `--reach` puts level 49's creature
in a pen of the same shape and level 29's 430 cells from the spawn. **Level 5's,
at (985,120), stands in the open 166 cells away**, and `tools/fit_route.py` walks
it in **nine legs on its first run with no hand-tuning at all**, arriving at
(926,123) on frame 3208. Then Right and Y held, and the fight is over in fifty
frames. That is what an open level costs against the twenty-three and forty-three
legs the maze levels have wanted.

**`--watch 050C` reads the creature's health straight off its thread page, and it
is `$0001`.** So the first hit leaves it at zero — `d7f6_survived`, because
`1 - 1` does not borrow and `0 != 1` — and the three after it go negative:
`$FFFF` at 3246, `$FFFE` at 3258, `$FFFD` at 3270. Four hits, one survivor, three
deaths. The report says `d7f6_hit` **20**, `d7f6_died` **15** and `d7f6_survived`
**5** against **4** calls on the routine's own row, which is five interceptions
per call and the note beside the coverage table doing what it says. **The ratio
is what is measured and it is 1:3 in both places.**

**Two more of the seven are now known to have no input rather than to be waiting
for one.** `d7f6_fatal_id` wants `$5E`, object type `$04`, and of the six levels
that place behaviour `$81:D704` **exactly one also places a type `$04`** — level
49, whose creature is the one in the pen. `d7f6_no_damage` was closed two rounds
ago with its six siblings. What is left is `d7f6_special`, id `$5D`: level 5's ice
weapon is sealed in a basement pocket at (667,1068), level 17's is reachable
beside a creature that is not, and level 31 has no password — **level 29 has
both**, its type `$02` at (756,730) sitting on a blocked cell with standable
ground at x=751 beside it.

**Forty-six routines, no port code changed, three sites moved: 3,482,740 calls
checked across 39 movies, 0 diverged; branch coverage 180 of 246, 66 untaken by
every input, and no census section on any movie** — from 3,429,877 across 38 and
177 of 246, the difference being the new movie's 52,863 exactly. It reaches 49
sites, and intersecting its untaken list with the corpus's says the three it has
to itself are precisely `d7f6_hit`, `d7f6_died` and `d7f6_survived`. `run`
substitutes all forty-six on it and finds **no byte of live game state differing
on any of 3,489 compared passes**, and `-r none` is **identical at all 3,489**.

**And last round's named next item turned out to have a prerequisite nobody had
checked.** `a264_ignore_named` was priced as a level-41 route; the route was never
the expensive part. `$83:A264` is a *state*, installed only by `$83:A21D`, which
`$83:9699` reaches from one place — the `$0002` arm of its event dispatch, which
is `VICTIM_ID_EVENT_2`, id `$0B`. `verify -c` counts that id arriving at a victim
**5 times on `movies/level21.zmv` and 0 times on `movies/level41.zmv`**, and
`--records` agrees over all 4,700 frames: level 41's neighbour answers at
`$83:A364` in every sampled frame from 2580 to 4440, on three pages as the camera
culls and rebuilds it, and never once at `$83:A264`. **Level 41 never menaces this
neighbour**, so there is nothing there to shoot at, and on level 21 — the one
level that does — the bubble gun is 1,300 frames up a one-way shaft from a window
that lasts about 340.

---

*The rest of this section is the previous round, kept as written.*

### The neighbour nobody had rescued (2026-07-31)

**`$83:A264` has eight branches, the corpus had four of them, and the two it did
not have were the two that mean a neighbour was saved.** `a264_claim_a` and
`a264_claim_b` are ids `$05` and `$06` — one player each — so their being untaken
said something plainer than a coverage number does: in thirty-six movies and
3.3 million checked calls, **nothing had ever rescued one of these neighbours.**
Both are taken now, by `movies/level21-rescue.zmv` and
`movies/level21-p2-rescue.zmv`, which are the same eighty-frame walk made by
different players.

**Finding the level was a sweep of the ROM's data, not of the movies, and that is
worth keeping.** A neighbour's handler follows from its *behaviour* address, and
the behaviour is in the level record: `zamn_assets actors` over all 56 records
shows that nine of level 21's ten neighbours run `$83:993D`, `$83:9776`,
`$83:9A89` or `$83:9EBE`, and that victim list entry **3, at (237,2726), runs
`$83:9699`** — the only one on the level whose collisions arrive at `$83:A264`.
The other direction agrees: `--records` over sixteen movies, one per level, finds
`$83:A264` on level 21 and nowhere else. **But the handler is a state, not a
label** — the same display record answers at `$83:A364` at frame 2700 and at
`$83:A264` from 2740 — so the level list says where a routine can be reached and
the record census says when it was, and a movie needs both.

**Seventy frames is the whole distance between the branches the corpus had and
the ones it did not.** `movies/level21.zmv`, a probe with no route in it, passes
(242,2728) at frame 3180 — one lane from the neighbour. The record disappears at
3110. That probe takes `a264_give_up`, because the level got there first. The
route that beats it is two legs long and both end at a wall: ten frames of `Left`
from the spawn at (258,2836) to the neighbour's column at x=238, then `Up`.

**Three words are written by a claim and exactly one of them differs between the
players.** `$7E:061E` is `$0001` (CLAIMED) for both. `$7E:605D` is `$80` for both,
because the flag byte is the *neighbour's* and not the player's — `$81:8191`
indexes `$7E:605A` by the victim's own list index, and this one is entry 3. The
claimant at `$7E:0618` is `$0005` for Zeke and **`$8000` for Julie**, which is the
same three-byte saving `victim_collide` makes one page over: `$83:A293  LDA
#$8000` falls into `$83:A296  STA $18`, and id `$05` enters at the `STA` with the
id still in the accumulator. Each branch counts **25**, the same twenty-five
frames of overlap.

**One perturbation is caught by one of the two movies and missed by the other,
and that is the round's most useful result.** Latching `$8000` for both ids fails
at `$7E:0618` — ROM `$05`, port `$00` — on the movie where Zeke rescues the
neighbour, and is invisible on the movie where Julie does, because there the port
is being made to do what the ROM already does. One broken line, two inputs,
opposite verdicts: the coverage report's thesis stated as a pair of verdicts
rather than as a count. `tools/perturb.py` takes a **list** of inputs per
perturbation now and runs all of them, because a perturbation caught by
everything it is offered says less than one caught by exactly one thing. Two more
were caught by both movies: writing the give-up event on a claim (`$7E:061E`, ROM
`$01`, port `$03`) and writing the flag byte at index 0 (`$7E:605A`, ROM `$00`,
port `$80`).

**A shot that cleared a give-up, stated carefully.** The first movie stands still
firing for 136 frames before it walks in, and `$7E:061E` goes to `$0003` — the
give-up value — at frame 2776, back to `$0000` at 2788 where `a264_shot_clears`
puts it, and to `$0001` at 2924 where the claim does. The same route without the
firing phase arrives at 2788 and goes straight `$0000` → `$0001`. Reading that as
a shot undoing a give-up in flight is a reading and not a measurement —
`a264_give_up` counts 0 on both movies, so the `$0003` came from the neighbour's
own unported behaviour — but the transitions are measured, and this is the only
handler in the project where a weapon shot *clears* a verdict instead of casting
one.

**Forty-six routines, no port code changed, two sites moved: 3,429,877 calls
checked across 38 movies, 0 diverged; branch coverage 177 of 246, 69 untaken by
every input, and no census section on any movie** — from 3,336,969 across 36 and
175 of 246, the difference being the two movies' 46,885 and 46,023 exactly. `run`
substitutes all forty-six on both and finds **no byte of live game state
differing on any of 2,989 compared passes** on either, and `-r none` is
**identical at all 2,989** on both.

**The next item is named and priced.** Two of the handler's branches are still
untaken. `a264_flag_none` wants a neighbour carrying `$FFFF` in the index word at
`$06`, and nothing the corpus has put on the board carries one.
`a264_ignore_named` wants id `$5E`, a bubble-gun shot from the first player, which
means a level with both a `$83:9699` neighbour and a type `$04` object — and of
the fourteen the corpus can start on (level 1 and the thirteen a password
reaches), exactly two have both:
level 21, where the gun is up a one-way shaft north of a neighbour that dies long
before, and **level 41**, where the gun at (1044,599) and the neighbour at
(1580,548) are on the same side of the map. That route is not cheap:
`tools/fit_route.py` stopped at (1242,861) after 43 legs, 500 pixels short.

---

*The rest of this section is the previous round, kept as written.*

### The player who was never player one (2026-07-31)

**The previous round ended with three perturbations nothing could distinguish and
one sentence saying what would — *a two-player level-21 movie* — and that
sentence was wrong about the movie and right about everything else.** There is no
such movie, because level 21 cannot hold two players. What catches all three is a
**one-player game whose one player is player two**, and it costs no port code at
all: `movies/level21-p2-bubble.zmv` types the password on port 1 exactly as every
password movie does, then answers the player-select window on **port 2 alone**.
Zeke never joins, `--pos` prints `--,--` in the first column for all 6,700
frames, and the player it does print is the page whose `$64` holds `$1CEC`.

**Two reasons the obvious movie does not exist, and the second one closes the
question rather than merely blocking the attempt.** The camera **tethers the two
players to about 176 pixels of each other** and level 21's route north is a
sequence of one-tile shafts: give both the same inputs and the one that spawns
thirty pixels east is stopped by a wall at the first shaft mouth while the other
climbs, until they halt at `296,2648` and `326,2824` and neither moves again.
Align them and stagger them by sixteen frames and both get in, and then whichever
reaches the mouth first ends the run **stranded at `x=378` beside a shaft at
`x=364`** while the other climbs away. And **level 21 places exactly one type
`$04` object** — 19 objects on the level, one bubble gun, at (485,1340) — so no
arrangement of two players ever puts that weapon in the second one's hands.

**One change to the input, and every question the game asks about identity gets
the other answer.** The player record carries `$06` where it always carried
`$05`, so `$82:F4EF`, `$81:845E` and `victim_a264_collide` are offered an id they
had never seen; every shot leaves carrying `$805E` rather than `$5E`, so the side
arithmetic in `enemy_9b6b_bubble` and `enemy_freeze` has something other than
zero to compute; and `score_slot_1` is credited where `score_slot_0` always was.
`f4ef_player` reads 30 and every one of them is `$06`. That is a whole class of
input the corpus did not have, bought with four Start presses.

**The route is the bubble movie's with three frames moved, and each of the three
is Julie standing thirty pixels east of where Zeke starts.** The rule the level-9
round arrived at from the other direction is what decides which three: **a leg
that ends at a wall survives a thirty-pixel offset and a leg that ends on a
stopwatch does not.**

* **2704, not 2719** — the opening leg east is four frames rather than nineteen,
  and both arrive at `x=296`, the mouth of the first shaft.
* **3985, not 4010** — the climb up the shaft at `x=364` covers about 24 more
  pixels in the same time. The two position traces are identical to the pixel as
  far as `y=1814`, where Zeke is held up by something Julie walks straight past.
  At 3990 the run ends at (463,1304) against a wall with the object uncollected;
  at 3985 it ends at (504,1338) with `$7E:1CF0` reading `$0040`.
* **`Down` held for the twenty frames before the first tap** — the tap-and-fire
  phase **drifts north about two pixels a cycle**, and above `y≈1310` the player
  stops firing altogether. Starting from `y=1338` instead of `y=1336` was enough
  to lose 35 of the 40 shots. Seating the player on the floor at `y≈1372` first
  lands all forty. An anchor is cheaper than a retimed fight.

**Three for three, and the two that fail at the same byte are the ones worth
reading.** Forcing the score slot to the first player's and taking the side off
bit 14 instead of bit 15 both fail at **`$7E:1FDC`, ROM `$00` against port
`$01`** — that word is the *first* player's MARTIAN/BUBBLED tally and `$7E:1FDE`
is the second's, so with only Zeke on the board both wrong spellings of the side
and the right one landed in the same array slot. `$82:F4EF` answering `$05` alone
fails at **`$7E:0618`**, which is `$18` on page `$0600` — and so locates the
direct page of a routine that has never once appeared in a display list.

**And the exercise that found them is a script now.** `tools/perturb.py` applies
one edit, rebuilds, runs `verify`, restores the source and rebuilds again. It
exists because the hand-done version acquired the same two bugs more than once
and both are now impossible rather than merely known: it **counts the anchor
first** and refuses anything that does not occur exactly once, because this port
has nine near-identical copies of the collision subsystem and two perturbations
have already come back MISSED having broken a routine the movie never runs; and
it **puts the binary back, not just the file**, in a `finally`, because an
earlier round spent an hour on a divergence in a tree that was already correct.
It also does the edit in `bytes` rather than `str`, which is a third lesson of
the same kind learned the same way: text mode turns CRLF into LF on the way in
and writes LF back out, so a restore through `str` reformats every line of the
file. A hash of the file before and after is what says it does not.

**Forty-six routines, and one new coverage site. **3,336,969 calls checked across 36 movies, 0 diverged; branch coverage 175 of
246, 71 untaken by every input, census empty** — from 174 of 246 and 72, and
the one that moved is the one the movie was built for.** `run` substitutes
all forty-six on the new movie and finds **no byte of live game state differing
on any of 6,689 compared passes**, and `-r none` — two stock cores, the control
that has to pass before anything else measured here means anything — is
**identical at all 6,689 of them**. The one site the corpus had never taken is
`d9b6b_bubble_slot_1`, with `d9b6b_bubble_slot_0` at zero on this input and
`d9b6b_died` 15, `d9b6b_hit` 30 and `d845e_pass` 180 beside it. Nothing in
`src/port/` changed this round: what it bought was three lines of it that had
never been checked by anything, and a script that will notice the next three.

---

*The rest of this section is the previous round, kept as written.*

### A census with two addresses on it, and both of them gone by the end (2026-07-31)

**The weapon table's second work item was `d9b6b_special` and `d9b6b_fatal_id`,
and what it actually bought was the census.** `movies/level21-bubble.zmv` climbs
to level 21's type `$04` object at (485,1340) — inventory slot 2, weapon `$5E`,
the **bubble gun** — and shoots the `$81:9B6B` creatures with it. That put
**`$81:83C6` and `$82:F4EF` on a decline census that had been empty for five
rounds**, and both are ported: 128,558 calls checked on the movie that reaches
them, 0 diverged, and the census is empty again.

**`$81:9BA2` is nine instructions, and they were the whole reason a branch
declined for four rounds.** Every other copy of `$81:8888` hands id `$5E`
straight to `enemy_bubble_react`; this one counts it first — `TYA : ASL A : AND
#$0000 : ROL A : ROL A` for the side, `JSL $80:9D6A` for the score slot, `INC
$1FDC,X`, then `JML $81:83C6`. The routine at the end of the `JML` had been
ported for four rounds. What was missing was never the code: `$5E` is the bubble
gun, no movie had ever fired one, and **a routine with no input is a routine
nobody can check**, so it stayed declined on purpose.

**The counter is what names the weapon, and its threshold is not its
neighbour's.** `$82:C9AE  LDA $1FDC : CMP #$000A : BCC` is the end-of-level
screen deciding whether to draw `MARTIAN/BUBBLED`, exactly as `$82:CA8C  LDA
$1FE0 : CMP #$0028` decides `MONSTER/FROZEN`. Two words apart because they are
two lines of the same screen — but **ten bubbled martians earn the bonus against
forty frozen monsters**, which is the ROM's own estimate of which gun is scarcer.

**The second address is sixteen bytes and the first ported handler outside bank
`$81`.** `$82:F4EF  CMP #$0005 : BEQ : CMP #$0006 : BEQ : CLC : RTL` /
`STA $18 : CLC : RTL` — `shot_f6a3_collide`'s shape one comparison shorter, in
the level-and-UI bank rather than the actor bank. What is worth the paragraph is
*which* ids it names: `$05` and `$06` are the two **players'** collision ids, the
numbers every other handler in the game throws away at the bottom of its
`CMP #$005C`. This one throws away everything else. No weapon, no monster and no
shot can make it store anything, and its whole interface is "a player is standing
on me". Its `$18` has seed and reader in the same routine — `$82:F3A7  STZ $18`
four instructions ahead of the install, `$82:F4C6  LDA $18 : BEQ` polling it —
which is the pattern `$81:F6A3`'s `$3E` established last round.

**And it is never drawn.** `--records` does not show it in any sampled frame of
the movie that calls it seven times, and neither does a sweep of all 24 live
handler words at six frames. Every other handler in the registry belongs to
something with a display record; this one belongs to a trigger, and that is also
why `f4ef_ignore` is untaken and hard to aim at. Its cycle budget is **118
exactly on all seven calls**, the only entry in the registry with no spread —
sixteen bytes of comparisons have nothing to be slow about.

**Level 21 charged three new tolls, and all three are written down.**

* **A pickup parks `$7E:1CBC` at `$000F` for about 460 frames.** With no buttons
  pressed at all it goes `$0000 → $000F` on the frame the object is collected and
  back 462 frames later, and the player can neither fire nor switch for the whole
  window. Six presses of B sixty frames apart put five inside it and only the
  last did anything — which reads exactly like the previous round's dropped-press
  problem and is a different thing entirely.
* **The shaft at x=364 is one-way.** The player climbs through y=1438 and is
  stopped dead by it coming down; `Down` held for a thousand frames does not
  move. Second one-way passage after level 25's escalators, and the first that
  would have stranded a movie. This one never goes back south and does not need
  to: three `$81:9B6B` creatures live a hundred pixels from the weapon.
* **Two tiles north of (404,1334) the player stops firing.** Held `Up` reaches
  (404,1302) and no shot ever leaves — the ammo word does not move across 550
  frames at step 1, with either weapon. So the fight is a **tap**: three frames
  of a direction to turn, then `Y` held for thirty-seven, four times around. A
  third fight shape after the box walk and the spin, and the one for when the
  ground you can shoot from is one tile wide.

**Thirteen perturbations, ten caught — and the three misses have a single
cause.** The counter's address, its value, the increment, the splice, the store's
offset and value, its absence, carry, Z and A all fail the diff. What does not:
forcing the score slot to the first player's, taking the side off bit 14 instead
of bit 15, and answering `$05` only instead of `$05` and `$06`. **Only one player
ever fires in this movie**, so the side arithmetic has one value to produce and
the id `$06` never arrives. One two-player level-21 movie catches all three, and
it is the same input `freeze_slot_1` has been waiting for since `enemy_freeze`
was written.

**Forty-six routines in the registry, and `enemy_9b6b_bubble` behind one of
them. 3,200,321 calls checked across 35 movies, 0 diverged. Coverage union 174 of
246**, 72 untaken by every input — from 168 of 242 and 74,
and the census is empty again. The denominator moved because the round added four
sites; of the four the *corpus* had never taken, one is `d9b6b_fatal_id` and
three came free with the route — `a264_flag_set` and `a264_give_up`, a victim
claimed and then giving up during the climb, and `player_hurt_alt_ignored`. `run`
substitutes all forty-six on the new movie and finds **no byte of live game
state differing on any of 6,689 passes**.

---

*The rest of this section is the previous round, kept as written.*

### One movie, three weapons, nine branches (2026-07-30)

**The previous round built a table and left four work items on it; this round
spent the first one and it paid nine times over.** The entry said
`cdde_survived`, `cdde_killed_reacting` and `cdde_react_begin` had an input,
because `CDDE_HIT_DAMAGE` is `$5D` and level 9 places the ice weapon at
(260,252). What `movies/level9-weapons.zmv` came back with is **two of those
three, seven of `enemy_d301_collide`'s eight, and an argument that the three
still untaken are not waiting for a movie at all**. It checks 165,757 calls, diverges
on none of them, and reaches **75 of 242 sites** — the most any single input has
ever reached, tying `movies/level25-boss.zmv` at nine hundred fewer frames of
walking.

**The first twenty minutes went into preparing to correct a table that was
right.** `zamn_assets actors <rom> N` takes the internal record index, and the
game's level numbering is one below it — `actors … 10` is level 9. Dumping
`actors … 9` finds no type `$02` object anywhere in it, which reads exactly like
the previous round having written down a level's weapons wrongly. The tell is
level 25: `actors … 26` holds the two `$22` objects at (1046,1045) and
(1110,1077) that `movies/level25-heavy.zmv` demonstrably picked up, and
`actors … 25` holds nothing of the kind. Both this file's per-level weapon table
and last round's boss arithmetic stand; what was missing was one sentence saying
which index the tool takes, and it is written down now.

**The route is the first one in the project built out of walls instead of
stopwatches.** Every movie before it is a list of timed legs, and level 9 is
where that stops working: its corridors are one tile wide, its actors stand in
them, and the same `Up` leg measured twice ended at y=204 on one pass and y=299
on the next, sixty frames of a creature in the way being the whole difference. A
leg that ends in the wrong place becomes a leg that walks into a wall for two
hundred frames. So nine of the twelve travel legs here are **held well past
arrival and stopped by geometry** — east to x=392, north to y=129, west to x=90,
south to y=719 — and every leg after one of those starts from a known pixel
rather than an estimated one.

**Two legs cannot be anchored, and both earned their place in the write-up.** The
descent to the ice weapon is timed, because there is no wall at (260,252): turn
at frame 3612 and the player passes at (338,251) with nothing collected, turn at
3616 and it is 99 shots. And the vertical corridor at x≈348 gets an anchor made
for it — walk east into the wall at x=392, then hold `Left` for **exactly twenty
frames**. Sixteen leaves the player at x=360 with `Up` blocked; twenty-four
leaves it at x=344 with `Up` blocked. A back-off from a wall is a timed leg whose
*start* is exact, which turns out to be most of what makes timing work.

**Then B ate a press and cost a phase.** Three weapons means two presses to get
back to the ice, and the second does nothing if it comes too soon: twelve frames
down and twenty-eight apart is swallowed, twelve and forty-eight is swallowed,
**sixty apart and every press lands** — `$1CBC` walking `$0003 → $0000 → $0001 →
$0003` cleanly. The version that lost the press spent eighty shots of `$5C` on a
creature immune to it and read `cdde_survived` as zero, which looked exactly like
a fight in the wrong place. A `--watch` on `$1CBC` is what told the two apart.

**What the three weapons bought is the largest single change in one routine's
standing since the census went empty.** `$81:D301`'s header has said since it was
written that one branch of eight was diffed and seven transcribed, because every
call any input had ever made to it carried the id `$FF`. **Seven of the eight are
diffed now** — `d301_shot_immune` 60, `d301_special` 380, `d301_survived` 125,
`d301_died` 20, `d301_reseed` 20, `d301_no_reseed` 105, and `d301_ignore` 7,564,
which was untaken for no better reason than that nobody had walked up to the
thing — off one creature at (107,315) shot with three different guns in ninety
seconds. The freeze family
came with it: `d301_special` *is* a tail call into `enemy_freeze`, and 380 hits
walk its counter well past five.

**The `$5F` phase failed twice first, and the failure is the reusable part.**
Fired **up**, from (98,331) at a creature sixteen pixels above and nine to the
side, two hundred and eighteen shots produced **zero** calls on the damage path;
fired **sideways**, from (90,313) at the same creature seventeen pixels to the
right, the same gun produced **145**. What `--records` shows in the failing case
is a `$5F` record living for a single frame — a shot that spawned and expired
without touching anything. Whatever the cause is, the working shape is now
written down for both weapons: **stand level with the thing and fire along the
row.**

**And the same table that produced the movie closes three more sites without
one.** By the argument the seven `*_no_damage` sites established two rounds ago —
name the id the branch needs, then ask whether any reachable level places the
object that carries it:

* **`d301_no_damage`** needs a zero-damage id. Of the five, `$5D` never arrives —
  `d301_special` catches it two comparisons earlier — which leaves `$5E`, object
  type `$04`. Three password levels place it (21, 41, 49) and `$81:D301` appears
  in no sampled frame of any of the six movies that walk them.
* **`cdde_counted`** needs `$64` or `$6F`. `$64` is object type `$1A`, which
  level 9 does not place; `$6F` is not a pickup at all — nothing in `$80:CA30`
  maps to its slot.
* **`cdde_killed_reacting`** is arithmetic. `enemy_cdde_react_begin` arms a
  30-frame flash and puts the countdown *back up* to 20, so finishing one during
  it is twenty-one hits in thirty frames. Held fire drains `$7E:1CCE` by eighty
  shots in 607 frames — **one every eight** — and one shot is worth about four
  hits, `$070C` walking `4 → 3 → 2 → 1 → 0` on four consecutive frames and then
  stopping. Fifteen hits at the very best, against twenty-one. Two players would close it, and **no level in
  the game places two type `$02` objects**: all sixteen that place one place
  exactly one, so both players can never carry the only weapon that damages this
  creature.

**Forty-five routines. 3,071,763 calls checked across 34 movies, 0 diverged.
Coverage union 168 of 242**, 74 untaken by every input — from 159 of 242 and 83
— and the census stays empty. `run` substitutes all forty-five on the new movie
and finds **no byte of live game state differing on any of 8,989 passes**, which
is a second stone in the level-25 clock-drift road: at 9,000 frames this is the
longest movie in the corpus, 1,400 frames longer than `movies/level25-boss.zmv`,
and it is clean. Length is not what the drift latches onto.

---

*The rest of this section is the previous round, kept as written.*

### A column that names the routine, and a weapon nobody had read the table for (2026-07-30)

**The census has been empty for four rounds and the work list is the coverage
table, which names branches and not inputs.** `d7f6_hit`, `d9b6b_hit`,
`d9063_survived`, `dac92_died` — thirty-odd sites saying what the port has never
been asked to do, and not one of them saying *where, on which level*, the
creature that would ask is standing. Every round before this one answered that by
walking around and looking at the picture. Two instruments and one ROM table
answered it this round instead, and between them they produced a new movie, three
newly-taken sites, a routine, and a correction to the previous round's arithmetic
that doubles a number this file printed yesterday.

**`--records` names the routine now.** Each row of the display list already
carried `ACTOR_REC_THREAD`; `$7E:1300,X` and `$7E:1330,X` are the collision
handler that thread installed — the pair `thread_call_handler` itself dispatches
on — so the dump stops being a list of things and becomes **a list of routines**,
every one named by the entry address `src/port/collide.c` already uses. Pointed
at `movies/level17.zmv` it finds `$81:D7F6` at (563,513) in thirty seconds, and
`zamn_assets actors` corroborates it without running anything: actor 11, type
`$23`, behavior `$81:D704` — the body that installs the handler, `$F2` bytes
ahead of it in the same routine.

**The page column shipped with a bug that looked right, which is the part worth
keeping.** The obvious reading of the thread arrays is `$0100 + slot/2 * $80`,
and the first column of the real table agrees with it. It is a table and not a
formula: `$80:82DE` reads `$0100 $0280 $0380 … $0C80` for thirteen slots and then
starts again at `$0180 $0200 $0300 … $0C00` for the twelve after. The formula
puts level 25's boss on `$0A80` where the ROM says `$0800`, so `--watch 0ABC` —
aimed at its health with real care — printed a steady zero and looked exactly
like a boss that never gets hurt. It reads the ROM table now.

**The second instrument is one line of a movie, and what it fixes is where the
player is looking.** `$81:9B6B` shows up 96 times in 126 sampled frames of
level 21's display list — three copies alive at once for much of it — and every
call it ever made took the ignore path. The reason is only visible with the two
position dumps side by side:
**these things walk at the player**, and `movies/level21.zmv`'s legs are 180
frames long, so the player spends almost all of its time facing away from the
crowd it is towing. A shot leaves in the direction you are facing; a chaser is
behind it by construction. `tools/make_spin_probe.py` writes the same twelve
directions at 30 frames instead of 180 — four legs of 30 cancel out, so the
player stays put and faces every direction once a second — and
`movies/level21-spin.zmv` verifies **73,039 of 73,039 on the first run** with
`d9b6b_hit` 35, `d9b6b_died` 35 and `f534_ignore`, all three previously zero.

**That the two 35s are equal is itself a finding.** Every hit this creature takes
kills it, so `d9b6b_survived` wants a weapon that does less than one point of
damage, and the only damage-table entries below 1 are the zeroes — which are a
different branch. It is the previous round's `*_no_damage` argument applied to a
`*_survived` site: one more untaken site that has **no input**, rather than
merely lacking one. And the spin is *not* universally better, which is worth
writing down because the obvious next move is to run it everywhere: on levels 5,
17 and 49 it added nothing at all. It trades ground for aim.

**The third thing is a table read the other way round.** The previous round
proved seven `*_no_damage` sites unreachable by enumerating which ids a shot can
carry. `$80:CA30`, `slot = id - $0C` and `$80:F8AC` run per level say which
weapons each of the thirteen password-reachable levels can hand you, and
therefore which `*_special`, `*_freeze` and `*_fatal_id` branch any input there
could ever take. That table is in `docs/cosim.md`. It turns four shrugs into work
items — level 9 places the ice weapon and `CDDE_HIT_DAMAGE` is `$5D`, so
`cdde_survived`, `cdde_killed_reacting` and `cdde_react_begin` have an input and
it is 99 shots at (260,252) — and one into a closed question: `dac92_freeze` is
`$5D` and level 49 places `$5E` and nothing else that freezes.

**And it re-priced the boss, by a factor of two, off a column this file read
yesterday and stopped one entry short.** Yesterday's ceiling for level 25 was
eighty damage against seventy health: four `$26` objects, weapon `$61`, five
shots each, twenty damage rewritten by `boss_9660_collide` to `$60`'s four.
Level 25's object list also holds **two type `$22` objects**, at (1046,1045) and
(1110,1077) — id `$17`, slot 11, weapon **`$67`**, twenty shots a pickup, four
damage, and **not one of the four ids the boss rewrites**. Forty shots at four is
a hundred and sixty. **`$80:F8AC` is BCD**, which is the detail that hid it:
slot 11 reads `$0020`, and reading that as thirty-two rather than twenty is how
the same table gives two different answers.

**So the kill stopped being ammunition-limited and became accuracy-limited, and
the round measured the accuracy rather than assuming it.** Six attempts after the
same detour: a 60-frame spin beside the boss does 16 damage (4 hits of 40 shots),
the box walk that `movies/level25-boss.zmv` uses does 8 with the same weapon, and **parking and firing
one direction does zero, four times out of four**. Four zeroes in a row is the
useful number. This boss does not walk into a stream of fire — it crosses the
plaza at about ten pixels a frame against the player's two, and between two
samples forty frames apart it can be four hundred pixels away. Every earlier
theory about this fight assumed it would come to you, because in
`movies/level25-boss.zmv` it did. **`boss_died` is still untaken**, and it is now
open for a reason that is measured instead of guessed.

**Two route findings came out of reaching those objects.** The south of the mall
is behind a pair of escalators eight pixels apart — **down at x≈917, up at
x≈945** — and each is one-way. `zamn_assets route` treats both as walkable in
both directions, so every route it plans out of the south climbs the down one;
that is the second escalator its 2x2-clear grid has been wrong about and the
first where being wrong strands the player. And a pickup wants the player within
about ten pixels: passing at (1057,1032) with the object at (1046,1045) collects
nothing, which is what `--watch 1CE2` is for.

**What the movie did buy is an address.** `movies/level25-heavy.zmv` put
**`$81:F6A3`, 273 declines**, on a census that had been empty for four rounds,
and the routine is seventeen bytes: three `CMP`s, a `STA $3E` and two exits that
both `CLC`. **It is identified off the bytes after it the way `$81:EDAA` was, and
the answer is different in kind** — `$81:F6B8` begins `63 00 63 80 | 64 00 64 80
| 64 00 64 80 | 66 00 66 80 | 67 00 67 80`, five collision-id tables in a row, so
this is **four weapons sharing one shot handler** rather than one weapon's. It is
the first address in the project reached by more than one weapon, and the reason
its census entry is 273 rather than a handful.

**Both exits clear carry**, so unlike `actor_845e_collide` — the same shape,
three named ids and an else — this one can never park its thread: a shot that
hits something keeps flying either way, and what changes is whether the shot's
*body* is told. `$3E` is where it is told, and that word has its seed and its
readers in the same bank — `$81:F691  STZ $3E` clears it four instructions before
`$81:F69B` installs this very handler, and `$81:F3F6` and `$81:F57B` are the body
polling it on its next pass. **273 of 273 checked, 0 diverged**, both sites taken.

**Ten perturbations, eight caught.** The store's offset, its value, the guard,
carry, A, and all three flag claims fail. The carry one fails at **`$7E:119E`,
one level up**, because carry set is what parks a thread and a parked shot stops
moving; nothing inside the routine disagrees at all. The two misses are both
priced: no collision in 6,700 frames hands this shot an id of `$01`, and
`arg - $0001` and `arg - $0003` have the same sign for every id except `$0002`.

**And the perturbation script had a bug of exactly the kind it exists to find.**
Two flag cases came back MISSED because the three lines they matched are
identical in an earlier routine, so `str.replace(old, new, 1)` broke *that* one;
both are CAUGHT with an anchor unique to this routine. A tool that breaks code on
purpose has to be sure it broke the code it meant to. The previous round's
version of the same lesson was that it has to restore the binary as well as the
file.

**And `run` moved a diagnosis that had been sitting still for three rounds.**
`movies/level21-spin.zmv` substitutes cleanly; `movies/level25-heavy.zmv` does
not — 1,959 bytes unaccounted for, with the NMI frame counter at `$7E:0016`
reading `$0C` against `$FF`, which is level 25's clock-drift signature exactly.
The control says what it is not: substituting **only** the new routine
(`-r shot_f6a3`) is **identical at all 6,689 compared passes** of the same 6,700
frames, so its 148-cycle budget costs nothing. What moved is the reading. Three
level-25 movies now exist and **the longest of them is the clean one** —
`movies/level25.zmv` 1,910 bytes over 4,700 frames, `movies/level25-heavy.zmv`
1,959 over 6,700, and `movies/level25-boss.zmv` **57 bytes at most over 7,600**.
"The busiest level anyone has run" is not sufficient; one route through level 25
avoids the drift for longer than either of the two that meet it.

**Forty-five routines. 2,906,006 calls checked across 33 movies, 0 diverged.
Coverage union 159 of 242**, 83 untaken by every input — from 154 of 240 and
86 — and the census is empty again. Three of the three sites this round set
out to take are taken, both of the two it added are taken, and one of the
eighty-three it left behind is now known to have no input rather than to be
waiting for one.

---

*The rest of this section is the previous round, kept as written.*

### The weapon the boss rewrites, and a routine one byte long (2026-07-30)

**The census has been empty for three rounds, so the work list is the coverage
table — and the first thing this round did was take seven items off it that were
never work.** `*_no_damage` is the largest single family in that table: nine
copies of the enemy collision subsystem each mark "a hit whose damage-table entry
is zero", and after thirty movies all nine read zero. The obvious reading is a
thin corpus. The ROM says otherwise, in two steps.

`ENEMY_DAMAGE_TABLE` has exactly seven zero entries — `$5D`, `$5E`, `$71`, `$72`,
`$73`, `$78`, `$7B`. And **every id a shot can carry appears in the ROM as the
four bytes `id 00 id 80`**, because each weapon's spawner keeps a two-word
collision-id table with bit 15 set on the second player's entry. Searching all
1 MB for that pattern and keeping the hits in bank `$81` enumerates the lot:
`$5C`–`$64`, `$66`, `$67`, `$68` and `$6F` — thirteen, with `$65` and `$69`–`$6E`
absent. `$71`, `$72`, `$73` and `$7B` are not
among them and there is no `LDA #$0071`, `#$0072`, `#$0073` or `#$007B` anywhere
in the ROM either; `$78`'s five immediate loads are all outside the shot bank.

**So the only zero-damage ids a shot can carry are `$5D` and `$5E`, and seven of
the nine copies divert both before the subtraction.** `enemy_no_damage`,
`monster_no_damage`, `b41c_no_damage`, `d7f6_no_damage`, `d9b6b_no_damage`,
`d9063_no_damage` and `dac92_no_damage` **have no input**, in the same sense
`$80:FA26` has no movie. The two that are not on that list are the interesting
half: `enemy_d301_collide` has no `CMP #$005E`, so the bubble gun reaches its
table, and `boss_9660_collide` *rewrites* `$62` and `$70` into `$5C` or `$5D` on
a coin toss and does not divert `$5D`. Both are one weapon away. The seven now
say so in the report itself.

**And then the boss, where `boss_remap_61` is taken for the first time.**
`$82:9660` answers four ids as some other id and no input had ever produced one,
because no input on level 25 had ever held anything but the squirt gun.
`movies/level25-boss.zmv` produces one, and getting there was three ROM tables
and three findings about the level.

**The tables turn "try a weapon" into arithmetic.** `$80:CA30` maps object type
to collision id, `$80:F87B` maps id to inventory slot (`slot = id - $0C`, weapon
`$5C + slot`), `$81:8561` prices the shot — and **`$80:F8AC` says how many shots a
pickup gives**, which is the table nobody had read. Level 25's reachable heavy
weapon is object type `$26`, id `$11`, slot 5, weapon `$61`, twenty damage — and
**five shots**. There are four such objects, so the whole of the level's heavy
ammunition is twenty shots, which the boss rewrites to `$60` at four damage
each. Eighty, against seventy health.

**The findings about the level are all about getting there.** The start is the
top of an **escalator**, which pushes the player back up the instant Down is
released — so `tools/fit_route.py` cannot fit this route at all: its loop is
press, release, measure, and on an escalator it measures itself back where it
began. Three runs, 40, 53 and 48 legs, about forty minutes each, finished 0, 200
and 46 pixels from where they started. `zamn_assets route` is no better: its
2x2-clear grid treats escalator tiles as walkable both ways, and both of its
routes south go through one. Two of the level's five weapon objects are inside
scenery and the router refuses them correctly. What worked was rendering the map,
marking the start and the target on it, and tuning nine hand-written legs against
`--pos`.

**The round's real finding is a word, and no instrument could read it.** The
first complete run landed **47 damaging hits with no zero-damage hits among
them**, on a health word seeded once to 70 by the only instruction in the ROM
that writes it other than the handler's own store — and `verify` reported **0
diverged on all 9,138 calls**. Forty-seven hits of at least one damage cannot
leave seventy health standing, and the port and the ROM agreed about every one of
them, so the error was in a *reading*. Neither `--pos` nor `--records` could say
which: the boss's health is not a position and it is not in the display list.

**`zamn_headless --watch <addr>[,first[,last[,step]]]` is the third instrument**,
and it prints a word only when it changes, so a whole movie costs ten lines:
`70 → 66 → 58 → 50 → 46` in twelve frames as the volley lands, then `45, 43, 42,
41, 40`. Thirty damage. It may be **given more than once**, which is what killed
the theory that a second creature was soaking the rest — pointed at `$3C` on all
twenty-four thread pages in one replay, only `$7E:083C` starts at 70 and falls,
and every other page cycles 0/2/6/10/14 like an animation frame.

**The discrepancy was the report telling the truth about itself.** Under `verify`
the harness runs the port once per *interception*, and `boss_9660` is reached
both at its own entry PC and through `thread_call_handler`, which is ported — so
its sites count twice per call, and `cosim_coverage_report` says exactly that in a
comment beside the numbers. Halved, 47 hits is 23 and 17 `$61` hits is 8 or 9, and
8 × 4 is 32 against 30 measured. **A bug that survives ten minutes of arithmetic
and dies to reading the paragraph next to the number is worth writing down**,
because the next reader of that table will do the same sum.

**`boss_died` is still untaken, and it is priced now rather than merely open.**
Twenty `$61` shots is 80 against 70, so the kill exists with almost no slack;
this movie lands about nine of the twenty and the rest fly into the mall.
Retiming the B press was tried at 4300 and 4450 and both are *worse* — 20 damage
against 30 — because the volley is 600 frames long and moving it moves every
monster on the level with it. What closes it is a route that stays adjacent for
the whole volley, or a **two-player** movie, which doubles the ammunition rather
than the accuracy.

**And the movie put an address back on the census, which had been empty for
three rounds.** `$81:EDAA`, 62 declines — and it is **one byte**. `$81:EDAA  6B`
is a bare `RTL`, and the four bytes immediately after it are weapon `$61`'s
two-player collision-id table, so the address and the table are one weapon's shot
code laid out back to back. It is **the shot's own collision handler, and the
shot does not react to anything it hits**: the squirt gun's `$81:FE0E` looks at
the id, expires on some and passes through others; this one is told about every
collision it has and answers none. The thing it hits still reacts — that is how
`boss_remap_61` gets taken at all — but the shot carries on, which is what a
weapon costing five shots a pickup should do.

**Its shim makes the strongest claim in the registry, and it is strong precisely
because the routine is empty.** An `RTL` sets no flag and touches no register, so
A, X, Y, **N, Z, C and V** all come back as they went in — every one claimed, and
every one checked on all 62 calls. `$82:F1C2` is the cautionary opposite: no
`CLC` and no `SEC` there either, and reading that as "carry passes through"
failed on the second call, because a `CMP` on every path had already decided it.
Here there is no instruction at all, which is a different fact and the only one
that licenses the claim. It is also the second registry entry with no coverage
site: a routine with no decision in it has no untaken branch.

**Forty-four routines.** The corpus was measured at forty-three, before the last
one landed: **2,694,401 calls checked across 31 movies, 0 diverged, coverage
union 156 of 240**, with `$81:EDAA` the single census entry. With it ported,
`movies/level25-boss.zmv` checks **200,400 of 200,400 with no declines and no
census**, and `movies/level1.zmv`, `movies/level25.zmv` and
`movies/level49-corner.zmv` re-verify to the same call counts they had. The union
goes to 154 with the two decline sites quiet again, and **84 sites are untaken by
every input — of which seven are now known to have no input at all.**

---

*The rest of this section is the previous round, kept as written.*

### The other second weapon, and a register nobody was reading (2026-07-30)

**`$81:83C6` is ported, and with it the last of the three splices and the last
branch the `$81:8888` family kept for an id no input produced.** The previous
round found the ice weapon by walking to an object and pressing B; doing the
same for `$5E` needed the object-to-weapon mapping rather than a guess, and
**the ROM states it plainly**. Each weapon has its own shot spawner and each
spawner its own two-word collision-id table, one entry per player with bit 15
set on the second — so searching the ROM for `5E 00 5E 80` finds exactly one
address, and doing that for all eight ids gives the table:

| id | id table | | id | id table |
| --- | --- | --- | --- | --- |
| `$5C` | `$81:FEC3` | | `$60` | `$81:EB9E` |
| `$5D` | `$81:FC0A` | | `$61` | `$81:EDAB` |
| `$5E` | `$81:F4C6` | | `$62` | `$81:FACD`, `$81:FC0E` |
| `$5F` | `$81:EA0C` | | `$63` | `$81:F6B8` |

`$80:CA30` turns object type `$04` into collision id `$0E` and `$80:F87B` puts
id `$0E` in inventory slot 2, so slot 2 should be `$5E` — and `--records` says
every shot on screen reads `$5E`, which is what makes it a measurement rather
than a chain of inferences. Three password-reachable levels place a type-`$04`
object; level 21's is across water and the router drowns, level 41's took 43
legs and never arrived, and **level 49's is 24 legs from the start**. That is
`movies/level49-bubble.zmv`.

**What the walk found was not what it was aimed at.** The census came back with
**`$81:AC92`, 290 declines** — a handler no input had reached and nothing to do
with the weapon. It is the **ninth copy of `$81:8888`** and the plainest
re-spelling yet: health `$3C`, parked id `$3E`, `DEC $10` on death,
`enemy_freeze` for `$5D`, `enemy_survived_react` for a survivor, and one
comparison no other copy has — `CMP #$0067 : BEQ` sends id `$67` straight into
the death tail with no subtraction. `ENEMY_DAMAGE_TABLE` says what that is
worth: `$67` costs 4 ordinarily, so this is a middling weapon being made lethal
rather than a strong one waved through.

**`$81:845E` is the round's oddity, and it came from the control.** Thirty-two
bytes, four comparisons, three exits — and **no stores at all**. Every other
handler in the project leaves at least one word of WRAM behind; this one writes
nothing anywhere, so carry is not merely most of its interface, it is the whole
of it. Ids `$03`, `$05` and `$06` — tested before the mask, so by name — park the
thread; anything below `COLLIDE_ID_PLAYER` does not; `$5E` does not, picked out
one comparison later; everything else does. It is the only place in the game
where `$5E` is an exception rather than a branch of its own, and the first
registry entry with `supported = NULL`: a routine that cannot decline and cannot
write has nothing to try on a scratch copy.

**`$81:83C6` itself is `enemy_survived_react` with one word changed**, which
after four rounds of this file calling it unported is the whole finding. Same
guard with the branch polarity flipped, the same ten-instruction splice, and
`$8404` where the twin writes `$8542`. `enemy_react_splice` already existed for
the other three, so what it cost was a guard and a constant.

**And this file described it wrongly two rounds ago.** The entry below says "the
same splice without the counter and with its tally one array over at
`$7E:1FDC`". The first half is right; the second half belongs to a different
routine. `$7E:1FDC` is incremented by `$81:9BA2`, the nine instructions
`enemy_9b6b_collide`'s own `$5E` branch runs *before* it `JML`s here.
`$81:83C6` has no counter, no tally and no side lookup at all.

**The naming chain is one link longer than `enemy_freeze`'s and it still
closes.** `$81:9BA2` increments `$7E:1FDC`; the end-of-level tally reads that
word to decide whether to draw a string reading `MARTIAN/BUBBLED`; and
`$81:8404`, what a spliced-in thread wakes up running, pushes the creature's
metasprite and collision id and then writes `$0036` over `ACTOR_COLLIDE_ID`. So
`$5E` is the **bubble** weapon, its damage-table entry is zero for the same
reason `$5D`'s is, and what it does is replace what a creature *is* rather than
take anything off it. One weapon freezes and one bubbles, and neither does a
point of damage.

**`movies/level49-corner.zmv` was written as a control and stopped being one.**
It is the same walk with the B press removed, to separate "new weapon" from "new
route" — and it does that job, since `$81:AC92` appears on both movies and
`$81:845E` only on the control. Then holding Y for eighteen hundred frames
**empties the squirt gun**, and the game moves the selection on by itself:
`weapon_select_next` runs twice with `weapon_changed` both times and no B press
anywhere in the file. So both movies fire the bubble gun, and `$81:83C6`'s three
diffed calls are on the *control* rather than on the movie built to reach it —
by the time the bubble movie is firing `$5E` its walk is in the north-west corner
and the `$81:AC92` creature is not. Three calls is a thin sample, all three of
them the splice, and `bubble_already` has never been reached.

**The round's real finding is in `sprite_build_oam`, which had a wrong register
for forty-one routines.** It ends `PLD : PLB : LDA $20 : AND #$0003 : TAX`, and
the `LDA` is a **direct-page** read that the `PLD` one instruction above has
already handed back to the caller — the `PEA $0000 : PLD` at the top covers
everything else the routine does, but not this. The port read absolute `$0020`,
`W_SCHED_TICK`, which is right whenever the caller's page is zero.

**It is not always zero, because the scheduler is not the only caller.** Two of
the three `JSL $80BD1F` in the ROM are `$82:DE03` and `$82:DE4B`, inside the
alert `$82:DDA7` runs when the player comes near `actor_deeb_collide`'s actor —
a thread, on its own page, driving the sprite pass itself between two `WAI`s. At
the failing call `D` was `$0700`, `$7E:0720` held `$001B`, and the ROM's X came
back `$0003` against the port's `$0000`.

**That difference reaches exactly one register, and it is the argument for
claiming every register you can justify.** All four entries of the table at
`$80:BDE6` are `$80`, so `$7E:1B64` and `A` are the same either way and 128 KB of
WRAM agreed on the failing call. The wrong value escaped through **X alone**, and
X is compared only because the shim claims it. That is the exact mirror of the
V-flag finding one round earlier: there, an output the shim did *not* claim was
invisible to `verify` and only `run` caught it; here, an output the shim *did*
claim was the only thing standing between a long-standing bug and nobody ever
knowing, and `run` could not have caught it, because it never becomes a byte of
WRAM. The fix is one parameter — `sprite_build_oam(Wram*, const Rom*, uint16_t
dp)`, with `dp` reaching precisely one instruction.

**Forty-three routines. 2,494,238 calls checked across 30 movies, 0 diverged.
Coverage union 153 of 240**, 87 untaken by every input, and the census is empty
for the third time.

---

*The rest of this section is the previous round, kept as written.*

### The second weapon, and what `STZ $7E` was for (2026-07-29)

**Every input this project has ever had fired the same gun.** The port
recognises more than twenty weapon ids between `player_collide`'s table, eight
copies of `$81:8888`, `shot_collide` and `ENEMY_DAMAGE_TABLE`, and **every shot in
every movie carried `$5C`** — which is why `enemy_hit_special`, the branch each
copy of the enemy subsystem keeps for ids `$5D` and `$5E`, had read zero since the
day it was written.

**It cost four moves.** `zamn_assets actors <rom> 18` puts an object of type `$02`
sixty-four pixels from where level 17 starts the player; `$80:CA30` says type `$02`
is collision id `$0D`, inventory slot 1; B cycles the selection. `zamn_assets
route` walks there in ten cells — Left is blocked at x=242, so it is Left, Down,
Left — and `--records` then shows every shot on screen reading `$5D`.
`movies/level17-weapon.zmv` is that, and the census, empty since this morning,
came back with **`$81:847E`, 333 declines**.

**`$81:847E` is `enemy_survived_react` a third time with a counter in front of
it, and the counter is the finding.**

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
call — written down as an honest gap four separate times, and once from the other
direction when *adding* a store of zero the ROM does not make proved equally
invisible. It was always zero because nothing had ever fired this weapon.
`INC $7E` is the only instruction in the game that makes it non-zero and the
`STZ` is its reset.

**And the weapon is identified off the ROM's own text rather than off the
screen.** `$81:849F  INC $1FE0,X` is the only writer of `$7E:1FE0`; the only
reader is `$82:CA8C  LDA $1FE0 : CMP #$0028 : BCC`, on the end-of-level tally,
deciding whether to draw the string at `$82:CABD` — which reads
`MONSTER/FROZEN/....////BONUS?`. So `$5D` is the **ice** weapon, five hits freeze
one thing, and forty freezes pay a bonus. Its `ENEMY_DAMAGE_TABLE` entry being
**zero** stops being a curiosity: it never damages anything, which is exactly why
it needs a counter of its own. `$81:83C6`, the `$5E` twin, is the same splice
without the counter and with its tally one array over at `$7E:1FDC` — the address
`collide.h` already described from `$81:9B6B`'s declined branch, whose string two
lines up reads `MARTIAN/BUBBLED`. **It is not ported**: no input fires that weapon
either, and a routine written ahead of its input is transcription.

**Six of the family's `$5D` branches now serve instead of declining**, because
the `JML $81:847E` is in each of their listings; only the spider's, which goes to
`$81:BB05` instead, still hands back. And the three copies of the splice are one
copy now — `enemy_react_splice`, with the three guards left where they are,
because the guard is the half that genuinely differs.

**Eleven perturbations, ten caught.** Four hits instead of five, the counter at
`$7C`, the tally at `$1FE2`, the spliced address not decremented, the flashing
guard never refusing, the slot lookup inverted, the tally never incremented, the
counting path parking the thread, the refused path returning the flags word
rather than the `AND`, and a reset the ROM does not make — all fail.

**The miss was the side read off A instead of Y**, and it is
`movies/level25-2p.zmv`'s finding one routine over: with one player every
collision id is positive, so every wrong way of computing zero is also zero.
`movies/level17-2p-freeze.zmv` walks the same four moves with **player two**,
whose shots read `$805D`, and the same perturbation fails at `$7E:1FE0`. The site
pair `freeze_slot_0` / `freeze_slot_1` is what names that gap and it takes one
movie each to fill — 12 freezes on each side.

**And an hour went into a divergence that was not in the tree.** The
perturbation script restored the source and left the *binary* built from the
perturbed one, so the next `verify` reported `$7E:067E: ROM $01, port $00` — the
freeze counter, reset by a line that no longer existed — on a movie that was
correct all along. A tool that breaks code on purpose has to put the build back
as well as the file, and it does now.

**Forty routines. 2,276,676 calls checked across 28 movies, 0 diverged. Coverage
union 145 of 226**, and the census is empty again. `run` substitutes over both new
movies with at most 37 and 31 bytes differing at once, all inside stacks or a
declared scratch byte.

---

*The rest of this section is the previous round, kept as written.*

### The census is empty again, and `run` found a flag `verify` was not looking at (2026-07-29)

**`$81:D301` is ported, and with it no input in the corpus declines anything at
all.** Twenty-six movies, 2,115,843 checked calls, and every `verify` run prints
no census section. The work list that has driven every round since Phase 3 began
is empty for the second time — and unlike the first time, it is empty across
thirteen levels rather than one.

**Porting it needed the random number generator first, and that was the better
half of the round.** `$81:D301`'s survivor branch rolls `JSL $80:9D39` and acts
on a 25-in-256 chance, so the last address on the census could not be written
without the routine PLAN.md names as one of the project's three top risks. It is
twenty-two bytes:

```
SEP #$20 : LDA $0024 : ROL $0024 : EOR $0024 : ROR $0024
INC $0025 : ADC $0025 : BVC +3 : INC $0025
STA $0024 : REP #$20 : AND #$00FF : RTL
```

An eight-bit shift register stirred by a counter, answering 0..255.

**The caller's carry is an argument to it.** `LDA` does not touch carry, so the
`ROL` shifts in whatever the caller arrived with — and `$81:D301` reaches the
generator on two paths whose carry differs, because `enemy_survived_react`
returns carry set from the splice and clear from the already-flashing guard. The
same creature draws from a different sequence depending on which one it took.

**The entropy is the NMI's, not the draws'.** `$80:81E7  LDA $1EB4 : BNE : INC
$24` bumps the *word* at `$0024` once per frame while the game is running, and it
is the only writer outside the generator any trace has seen. So the sequence
depends on *when* something asked, which is why a movie is a reproducible input
and a player is not.

**It was registered rather than inlined**, for `score_add`'s reason, and the
numbers say it earned that: `verify` offers it **1,060 calls on level 9 and 3,722
on `movies/level45-carried.zmv`**, essentially all of them from actor bodies
nobody has ported. It is the first routine in the registry whose call sites are
almost entirely code that does not exist in C.

**And then `run` failed where `verify` could not.** With the generator
substituted, every call passed on every movie and the whole-program diff came
back with **one byte, on two movies out of twenty-five**:

```
$7E:0DE8  stock $40, native $00   *** unexplained ***      (level 9)
$7E:0D58  stock $41, native $01   *** unexplained ***      (level 29)
```

`$40` is bit 6 of `P`, which is **V**, and both addresses are inside a thread's
stack. The `ADC` sets overflow; nothing in the port modelled it; `thread_yield`'s
`PHP` then parked the whole status byte on a suspended thread's stack, where the
byte-for-byte diff compares it as memory.

**The asymmetry is the finding, and it is the sharpest statement of this
project's own rule so far.** Flags are opt-in in the harness precisely so a claim
stays as strong as its evidence — and the price of that design is that *a flag
nobody claims is a flag `verify` never compares*. Thirty-eight routines had lived
happily on N/Z/C because no caller of any of them branches on overflow. No caller
of this one does either. What made V observable is not a branch, it is a
suspension. **The per-call diff cannot see an unclaimed output by construction;
the whole-program diff saw it by accident.** `COSIM_FLAG_V` now exists, exactly
one shim claims it, and the overflow flag is checked on all 1,060 of the
generator's calls — which is a stronger position than the one before the bug.

**`$81:D301` itself is the eighth copy of `$81:8888`, and it answers a question
four of the others left open.** `$81:B41C`, `$81:D7F6`, `$81:9B6B` and `$81:9063`
all end a death with a bare decrement of a word on their own page instead of
paying an award, and every one of those words is named for where it lives because
nothing in reach reads it. Here the reader is eleven instructions away:

```
$81:D2AD  LDA $0C : BEQ <loop top>   ; zero: carry on
          BPL $D2CE                  ; positive: leave, quietly
          ; negative: LDX #$0200 : LDA $36 : BEQ : JSL score_add : ...
```

So `$0C` is a **three-way verdict** the handler writes and the body reads on its
next pass, and the award this family "does not pay" *is* paid — `$0200` of it —
by the actor's own main loop, out of the id the handler parked. **Evidence about
the other four rather than proof**: different pages, different words, and
`$81:B41C`'s has a second writer this one does not.

**Two things it does that no sibling does.** It is **immune to the ordinary
weapon** — after masking, `CMP #$005C : BEQ` sends the player's basic shot, which
is every hit anywhere in the corpus, to a bare `CLC : RTL`, having parked it at
`$36` on the way past. And **`$FF` is how it is told to stop**, tested ahead of
the family's `CMP #$005C` and answered with `INC $0C`, the positive verdict. That
is `actor_deeb_collide`'s latch id and `victim_a264_collide`'s
`A264_ID_GIVE_UP_FF`: three unrelated actors reading `$FF` as "you are done
here", and still no routine anywhere that produces it.

**All three calls the corpus makes carry that one id**, which is the honest
counterweight: one branch of eight is diffed and seven are transcribed.
**Eighteen perturbations, twelve caught.** All seven on the generator fail — the
carry into the `ROL`, the carry into the `ADC` taken from the wrong rotate,
dropping the `BVC`'s second increment, dropping the `EOR`, and each of N, C and V
claimed wrongly. Five of eleven on the handler fail: the verdict decremented
instead of incremented, the verdict word at `$0A`, the stop id changed to `$FE`,
carry returned clear so the thread is never parked, and the stop path's flags
taken from the `CMP` rather than from the `INC`. **The six misses are all on
branches an untaken coverage site already names** — the `$5C` immunity, the
parked id's store, the health offset, the killing blow, and both sides of the
reseed draw.

**A list quietly wanted to be a field, and nothing would have said so.**
`guard_thread_call_handler` censuses where a declined dispatch went, and has
always had to exclude the handlers that decline *internally* — an id
`enemy_collide` hands back is a decline one level down, and naming the door would
put a routine the port already has at the top of the work list. That exclusion
was **four addresses, written when four handlers could decline; by this round
eight could**. The symptom of the omission is a census line naming a routine the
port already has, which is the same shape as the bug that hid `monster_collide`'s
missing dispatch for four rounds and just as quiet.
`thread_call_handler` hands the address back in `ThreadCallResult::unported` now,
set only on the branch that does not recognise the handler at all.

**And the other named work item is closed: Julie kills three monsters on level
25.** `movies/level25-2p.zmv` is `movies/level25.zmv`'s password prefix with four
Start presses on port 2 in the same player-select window, and two box walks
mirrored so the players cover different ground. It verifies **106,743 of 106,743
on the first run** with no census, and **68 of 219 sites** — the highest of any
single input in the corpus, against level 21's 62.

**What it was for is one word of WRAM, and it settles a question two rounds
old.** `monster_death_award` ends `LDA $20 : AND #$8000 : ASL A : ROL A : ROL A :
TAX : INC $1FD4,X` — a counter indexed by the side that landed the blow, already
doubled. Writing `1` there instead of `2` has passed every call on every input
since the level-45 round, because every death in the corpus was player one's and
both spellings of zero are zero. On this movie the same perturbation **fails at
`$7E:1FD5`** — the byte *between* the two counters, which is exactly where an
undoubled index lands. It is `player_pickup`'s doubled id and
`enemy_b41c_collide`'s `ASL` for the third time, and the first of the three to be
diffed rather than read.

**It gets there because the second player does most of the killing**: 20 awards
paid, `score_slot_0` 6 against `score_slot_1` **18**, and 00000300 / 00000900 on
the two score slots at frame 4600. `player_ignore` — a player told about an id of
its own side — reads 2,815, because two players walking a box run into each other
constantly.

**And it adds no branch coverage at all, which is the point worth keeping.** The
union is 139 of 219 with it and 139 without: every one of its 68 sites was already
taken by some other input. **The doubled index is not a branch.** A coverage
table counts decisions the code makes; it cannot count a *value* that flows
through a decision only one way, and this is the second finding in the project —
after `enemy_cdde_collide`'s already-zero `STZ $22` — where what was missing was
a second value rather than a second path.

**Thirty-nine routines. 2,115,843 calls checked across 26 movies, 0 diverged.
Coverage union 139 of 219.** `run` substitutes over every movie with no byte of
live game state unaccounted for except the two level-25 ones — 1,910 bytes and
2,021, with `$7E:0016` reading `$98` against `$25` on the new one. That is the
clock-drift finding from three rounds ago, unchanged in kind by anything here and
slightly worse on the busier input, exactly as its diagnosis predicts. And on
level 9 the substituted frontend, `--stock` and `zamn_headless` produce
**byte-identical framebuffers at frame 4700**, with the generator serving 1,060
calls from code that is still the ROM's.

---

*The rest of this section is the previous round, kept as written.*

### One address left, and a harness that had stopped measuring (2026-07-29)

**Eight more handlers are ported and the census is down to a single address.**
`$80:F9BE`, `$80:F979`, `$81:9B6B`, `$81:F534`, `$83:A264`, `$81:9063`,
`$82:DEEB` and `$82:F1C2` — everything the nine new movies named except
`$81:D301`, which is three declines on level 9 and the only entry left in a list
that was eleven addresses and 378 declines two rounds ago.

**Thirty-seven routines. 1,991,576 calls across 25 movies, 0 diverged. Coverage
union 137 of 208.** Twelve of the thirteen password-reachable levels now offer
the port every collision they produce.

**The two player jump-table entries turned out to be a group of four.**
`$80:F979`, `$80:F999`, `$80:F9AE` and `$80:F9BE` all open by testing
`ACTOR_DP_STATE` against the same small set and returning if it matches; what
differs is what they do otherwise and how many states are in the set. Two are
now ported, one already was, and the fourth (`$80:F999`, id `$35`) is **left
alone on purpose** — no input has ever carried that id, and a routine written
ahead of its input is transcription, which this project has enough of.

**`$80:F979` is the one that is not just a guard**, and it is a second kind of
being hurt: `ACTOR_DP_EVENT` = `$C000` and the recovery timer back to `$30`,
against `$80:F950`'s `$8001` and `$40`. Both of its branches are diffed — level
21 lands 5 hits and 2 refusals inside the invulnerability window.

**`$81:9B6B` and `$81:9063` are the sixth and seventh copies of `$81:8888`.**
Seven copies now, on seven pages, sharing one damage table and disagreeing about
almost everything else: health at `$1E`, `$0C`, `$22`, `$32`; the parked id at
`$22`, `$5A`, `$20`, `$30`, `$36`; a death that pays `$0100`, or `$0300`, or
`$0050`, or nothing at all.

**`$82:DEEB` is seven bytes and takes the record for the smallest handler in the
game by a factor of three.** `CMP #$00FF : BEQ : CLC : RTL` / `SEC : STA $12 :
RTL`. One id, one store, and carry is the whole of its interface.

**`$82:F1C2` is where the round earned its keep.** It contains no `CLC` and no
`SEC`, which the port read as "carry comes back as the caller left it" — and
`verify` said **`flag C: ROM 1, port 0` on the second call it ever saw**. `CMP`
is a subtraction and it sets carry; every path out of the routine has run one,
so carry was fully determined all along — set on the four acting paths, clear on
the ignore path where the comparison borrowed. **The absence of a carry
instruction is not the absence of a carry output**, and this is the first
routine in the project where that difference was load-bearing enough to fail.

**And then the harness stopped measuring, which is the finding worth keeping.**
Registering the thirty-third routine made `verify` report **zero calls checked
on every movie, with an empty routine table** — not a divergence, not a decline,
just nothing. `Cosim::enabled` was a `uint32_t` bitmask over the registry and
`Cosim::stats` a fixed `[32]`, so the 33rd routine shifted by 32 (undefined) and
indexed one past the end of an array. The instrument that exists to say when the
port is wrong had quietly stopped saying anything at all, and it took an empty
table to notice.

Both are widened to 64, and **`cosim_init` now refuses to start** rather than
silently miscounting:

```
error: 65 routines registered but COSIM_MAX_ROUTINES is 64 — widen
       Cosim::enabled and Cosim::stats together, in cosim.h.
```

The earlier totals are unaffected — the break is at 33 and every measurement in
this file was taken at 31 or fewer — but the shape of the failure is worth
writing down: **a harness has no harness.** Every routine in `src/port/` is
checked against the ROM on every call; the code doing the checking is checked by
nothing, and its failure mode here was to succeed loudly at examining nothing.

**`run` is unaffected on every level but 25**, whose 1,910-byte gap is unchanged
across all eight of this round's routines — the clock-drift diagnosis from two
rounds ago, holding still while the thing around it changed.

---

*The rest of this section is the previous round, kept as written.*

### The same routine, provably (2026-07-29)

**`$81:C440` and `$81:D7F6` are ported — the top two of the census the nine new
movies produced — and the first of them is the first routine in the project
where two ROM addresses share one C function.**

**`$81:C440` is `monster_collide` again, and for once "again" is not a
judgement call.** Three bytes differ in 102, and the case that it is the *same
creature* rather than a relative is the bodies: `$81:C321` installs this handler
and then `$81:C326  JMP $C3B5` falls into `$81:C3B6`, which installs
`$81:C4A6`. One thread, two handlers, in that order. So this is the giant spider
before whatever `$81:BFA8` decides and `monster_collide` is it afterwards, and
the page they share — health `$22`, parked id `$20`, latch `$26`, record `$08`,
counter `$2A` — is the same page because it is the same actor.

**So the body is shared rather than transcribed a second time**, with the two
differences as fields: a survivor leaves through `enemy_survived_react` here and
`monster_survived_react` there, and id `$5D` declines to `$81:847E` rather than
`$81:BB05`. That is the call `ENEMY_REACT_FRAME` already makes one level down —
two copies of one routine in C are two things that can drift, and a diff only
catches the drift on a level that runs both. What it costs is that a coverage
site inside the shared body no longer says *which* copy reached it, so the two
places the copies genuinely differ get sites of their own. Both routines stay
registered separately, because that is what tests the claim: `verify`
intercepting one never intercepts the other, and the flags each leaves are
checked only at its own entry PC.

**151 of 151 on the first run, and the sample is the one the spider never
got.** Level 45 offered `monster_collide` 1,138 calls of which almost all were
ignores; level 25 offers this copy **115 hits, 105 survivors and 10 deaths**. The
death tail, `$81:BBEB`'s award and `handler_park` are diffed properly for the
first time rather than transcribed.

**And `react_already` is taken — 25 times.** That site has read zero since it
was written, and this file has named it for rounds as the sharpest example of
what a diff cannot check: an entry guard whose only observed value is the one
that lets everything through. Level 25 hits these things fast enough to hit one
twice inside the flash.

**Nine perturbations, five caught, and two of the four misses were worth fixing
rather than recording.**

* **A guard marked on its passing side says nothing about its refusal.** Paying
  `$81:BBEB`'s award unconditionally — deleting the `LDA $20 : BEQ` — passed all
  151 calls while `monster_kill_award` read 10 the whole time, so the coverage
  table showed nothing missing. A site marks the branch it is written on. There
  is now a `monster_kill_free` site on the skip, and it is untaken: every monster
  that has died in the corpus was killed by something carrying an id.
* **The kill counter's doubled side index is transcribed**, for the third time
  in this project after `player_pickup`'s doubled id and `enemy_b41c_collide`'s
  `ASL`. Writing 1 where the ROM writes 2 passes everything, because every death
  in the corpus is player one's and both spellings of zero are zero. What settles
  it is a **two-player movie on level 25**, and that is now a specific work item
  rather than a shrug.

The other two misses are the usual pair: the `$5D` census address, which no input
reaches, and the `STZ $7E`, which is the fourth time that store has been
invisible for being already zero.

**`$81:D7F6` is the fifth copy of `$81:8888` and it is not close enough to any of
the four to share.** Health at `$0C`, parked id at `$20`, and — the real
difference — **id `$5E` reaches the death tail instead of `JML`ing out**, which
is `monster_collide`'s reading of that id rather than `enemy_collide`'s. It
starts on **one** health, so almost anything that hits it kills it.

**Its `$0A` is what `B41C_DP_COUNTER_0A` has been waiting for.** Two other copies
decrement a word at `$0A` and the port has said in as many words that what it
counts is not established. On this page it is unambiguous: `$81:D6D8  STZ $0A`
seeds it, the handler's `DEC $0A` is the only other writer in the bank, and
`$81:D72E  LDA $0A : BEQ <loop>` is the body's main loop deciding whether to go
on living — after which it pays `$0050`, the smallest award in the game, guarded
by the parked id being non-zero exactly as `$81:BBEB` is. **It is evidence about
the twins and not proof**: `$81:B41C`'s copy has a second writer and this one
does not, so that constant keeps its careful name.

**128 of 128 passed, and six of its seven branches are transcribed.** Every call
level 17 makes takes the ignore path — the `enemy_cdde` and `enemy_b592`
situation again, and the honest counterweight to the round. Eight perturbations,
**two** caught, and every miss is named by an untaken site. The interesting one
is the mirror of a finding already in the file: on `enemy_cdde_collide`, deleting
the ROM's `STZ $22` was invisible because the word was already zero; here,
**adding** a store of zero that the ROM does not make is invisible for the same
reason. A store the diff cannot see and an absent store the diff cannot see are
one fact from two directions, and one input closes both.

**Thirty-one routines. 1,991,162 calls checked across 25 movies, 0 diverged.
Coverage union 124 of 170.** The census is down to **nine addresses and 99
declines** from eleven and 378; level 17's is empty and level 25's is one player
jump-table entry. Level 25's `run` gap is unchanged at 1,910 bytes — the two new
routines neither helped it nor hurt it, which is what the previous round's
diagnosis predicts.

---

*The rest of this section is the previous round, kept as written.*

### Nine levels nobody had run, and a boss (2026-07-29)

**The previous round ended with an empty census and said the bottleneck had
moved from routines to inputs. It had, and the inputs were cheap.** Passwords
reach thirteen levels; the corpus had five of them. Nine
`tools/make_password_movie.py` prefixes with a fixed twelve-leg box walked with
fire held — no route-finding, no aiming, nothing measured on the way — and
**every one of the nine verifies clean on its first run**: 74,933 / 75,344 /
81,390 / 78,211 / 73,875 / 86,023 / 78,637 / 89,010 / 78,619 calls with no
divergence anywhere. That is the strongest structural claim the project has
made. Twenty-eight routines written against five levels ran nine they had never
seen — different tilesets, different actor bodies, a boss — and not one byte
came out different.

**And the census is back, which is what they were for.** Eleven addresses, 378
declines, from a work list that had been empty:

| address | declines | first seen on |
| --- | --- | --- |
| `$81:C440` | 151 | level 25 |
| `$81:D7F6` | 128 | level 17 |
| `$81:9B6B` | 33 | level 21 |
| `$81:9063` | 32 | level 5 |
| `$82:DEEB` | 12 | level 49 |
| `$80:F9BE` | 6 | level 25 (a player jump-table entry) |
| `$81:F534` | 5 | level 21 |
| `$83:A264` | 4 | level 21 |
| `$81:D301` | 3 | level 9 |
| `$82:F1C2` | 2 | level 37 |
| `$80:F979` | 2 | level 21 (another) |

**`$82:9660` was the twelfth and it was 5,126 on its own — the largest entry the
census has ever printed — and it is ported.** It is level 25's **boss**, and
that is read off the code around it rather than off the screen: its thread
allocates **four** display records where every other actor has one, it starts on
**70 health** against a level-1 zombie's zero, dying pays **`$2000`** — twice a
victim and the largest award in the game — and the death is a set piece rather
than a slot being freed, sixteen passes of a mosaic ramp queued into vblank and
a `thread_spawn` where it stood. The creature itself is not identified, so the
routine is named for its address, exactly as `enemy_b41c_collide` is.

**It is the first ported routine that mixes absolute and direct-page addressing,
and that is the whole of getting it right.** `LDY $0078` and `LDA $0020` are
three-byte absolute operands — `W_HANDLER_SELF` and the scheduler tick — while
`$3A`, `$3C`, `$3E`, `$40`, `$42` and `$44` are two-byte direct-page ones on the
boss's own page. The two coincide only if `D` is zero and it is not: `$82:948F`
writes absolute `$003C` as a *coordinate* in the same routine that seeds direct
`$3C` to 70. The diff located the page for us — `$7E:0800` — by failing at
`$7E:083C`, `$7E:083E`, `$7E:0840` and `$7E:0844` on four different
perturbations.

**Four ids are answered as some other id, and two of them on a coin toss.** No
other handler does this. `$62` and `$70` are rewritten to `$5C` or `$5D`
depending on `LDA $0020 : AND #$0001` / `AND #$0003` — the low bits of the
scheduler clock, read straight, not `$80:9D39` — and `$61` and `$6F` become
`$60` and `$63`. Reading `ENEMY_DAMAGE_TABLE` says what that buys: **`$61` costs
20 and `$60` costs 4**, so the remap is this creature taking a fifth of what
that weapon does to anything else, and the coin toss is a weapon doing 1 damage
half the time and **0 the other half**, because `$5D`'s entry is zero. The
damage table is shared with every enemy in the game; what a boss does with it is
not.

**Fifteen perturbations, ten caught.** The record read through
`W_HANDLER_OTHER` instead of `W_HANDLER_SELF` (`Y: ROM $1AA2, port $0000`), the
collide id read from `ACTOR_NEXT`, dropping either guard, `Z` claimed clear on
the id-`$09` path, parking the id at `$40`, dropping either decrement, and
returning carry clear — that last one at **`$7E:11A6`, one level up**, because
carry set is what parks the thread. **The five misses are all named by something
that already exists.** Two are untaken sites — the coin toss and the death path
— and the doubled damage index is `player_pickup`'s miss for the third time,
because every hit in the corpus is id `$5C` and every wrong way of computing
zero is also zero. The other two are one finding: **clearing the parked id on an
ignore path is invisible on both paths that do it**, because the boss's own main
loop opens `$82:9579  STZ $42` and clears it every pass anyway. It is
`enemy_die`'s `STZ $7E` again, and the one place it could matter is between a
killing blow and the award that reads that word.

**No input kills it, and the arithmetic says why.** 70 health, one damage per
basic shot, and a flash the handler refuses hits during — so a kill is about
seventy clean hits and the busiest probe lands 45. `boss_died` and
`boss_no_damage` are untaken and named, and the movie that closes them is a
weapon or a route rather than a longer wait.

**A bug fell out of having a corpus runner, and it was four rounds old.**
`monster_collide` has been registered on its own entry PC since the level-45
round and passed every call `verify` offered it — but **`thread_call_handler`
never routed to it**, so every one of those calls *also* declined one level up.
The two facts are not in tension and neither is visible alone: a routine reached
directly is checked, and the same routine reached through a caller that does not
know about it is a decline. What hid it is the census, which excludes the
handlers the port has by address — and `$81:C4A6` was on that exclusion list
while not being on the dispatcher's, so the declines were counted and never
named. On `movies/level45-carried.zmv` that was **1,351 of
`thread_call_handler`'s 2,800 calls**; it is 0 of 2,800 now, and the four
level-45 movies decline nothing at all.

**`tools/verify_corpus.ps1` is what found it and what the numbers above are
measured with.** The corpus was six movies when Phase 3 started and is
twenty-five now, and "they all verify clean" had become a claim nobody could
reproduce without knowing how many frames each one wants — which is not a
property of the `.zmv`, because a movie's last input is not its last interesting
frame. That table lives in the script now. It also does the two things no single
run can: intersect every movie's untaken list, and sum the census.

**1,989,827 calls checked across 25 movies, 0 diverged. Coverage union 121 of
160**, 39 untaken by every input — up from 134 sites and a union nobody had
computed.

**And level 25 is the first input in the project where `run` does not hold.**
Eight of the nine new levels substitute cleanly — at most 25 to 77 bytes
differing at once, every one inside a stack or a declared scratch byte — and
level 25 ends with **1,910 bytes unaccounted for**, with the NMI frame counter
at `$7E:0016` reading `$0F64` on the stock side against `$0F2F` on the native
one. Fifty-three frames apart. The substituted frontend says the same thing from
the other end: `zamn` and `zamn_headless` are **pixel-identical for 3,950 frames
of level 25 and differ from 3,980**, where every other movie ever checked agrees
to the end.

**That is not the boss's doing, and it was checked rather than assumed.** With
the boss handler taken back out of the dispatcher — declining exactly as it did
before this round — level 25 fails `run` *worse* (2,148 bytes unaccounted
against 1,910) and the frontend still diverges at the same frame. It is the
fixed cycle budget every substituted routine returns on, on the busiest level
anyone has run, and porting the boss made it smaller. The honest reading is that
`run`'s timing model has met its first level, and that is a finding about the
harness rather than about any routine in it.

**Twenty-nine routines.**

---

*The rest of this section is the previous round, kept as written.*

### The census is empty (2026-07-29)

**`$80:F9AE` is ported, and with it no input in the corpus declines anything at
all.** Sixteen movies, 2,096,000-odd checked calls, and every `verify` run prints
no census section. That has been true of individual movies before; it has never
been true of all of them at once.

**It was the last entry the census named, and it was one call.**
`movies/level29-firstaid.zmv` reaches it and nothing else does — `LDA $70 : CMP
#$0002 : BEQ : CMP #$0004 : BEQ : JMP $DC09`, which is the states-2-and-4 guard
`ACTOR_DP_STATE` has documented since the sprite pass, written out longhand.

**The `JMP` is why `$80:DC09` is ported with it rather than named as a decline.**
It is a jump and not a call, so what follows is the rest of *this* call — the
`RTS` the jump table's caller gets is `$80:DC09`'s. Three guards, each returning
having written nothing (still inside the hurt window; a state other than zero; a
sentinel `CMP #$FD72` against `$10` that nothing in reach explains, and which is
named for the comparison rather than for a meaning), and one store: `$80:DC1E`
into `$28`, which is this page's "what I do next" — the fourth offset the same
idea has appeared at, after `$0E`, `$12` and `$16` on the three enemy pages.

**Both calls took the branch that writes, which is the good luck of the round.**
The usual shape of a one-call entry is that the diff proves a guard and nothing
else; here `state_tail_queued` is taken twice and the store is genuinely checked.
Seven perturbations, four caught: the queued address off by one, the store into
`$26`, the hurt-window test inverted, and the flags after the store all fail.
**The three misses are all the same kind and were predictable from the coverage
table** — dropping the state test, dropping the sentinel test, and dropping the
states-2-and-4 gate itself. Each is a guard that only ever reads its passing
value, which is where `react_already`, `victim_latched` and `object_spent`
already live, and each has an untaken site naming it.

**What the empty census does and does not mean.** It means no input the project
has reaches a routine the port does not have — the work list that has driven
every round since Phase 3 began is exhausted. It does not mean the port is
finished, and the honest counter-weight is the round below this one: seven of
`enemy_cdde_collide`'s and `enemy_b592_collide`'s nine branches are transcribed
rather than diffed, because every call the corpus makes to them takes the ignore
path. **The bottleneck has moved from routines to inputs**, which is a different
kind of work and the first time it has been the only kind left.

**Twenty-eight routines.** All sixteen movies verify clean — 92,202 / 162,892 /
152,793 / 95,497 / 103,919 / 130,092 / 109,937 / 102,143 / 103,677 / 76,203 /
194,731 / 178,293 / 148,803 / 150,402 / 109,577 / 82,510 — `run` keeps every byte
of live game state identical, and the playable build still matches
`zamn_headless` pixel for pixel.

---

*The rest of this section is the previous round, kept as written.*

### Level 29's census is empty, and two of its handlers are transcribed (2026-07-29)

**`$81:CDDE` and `$81:B592` are ported, and level 29 declines no handler at
all.** 32 of 32 and 5 of 5 on the first run, and the `handler` census — three
entries deep at the start of the day — is gone. What is left on that level is one
`player id table $80:F9AE`, a jump-table entry, which is a different kind of work.

**`$81:CDDE` is the counter-example the other three needed.** Three copies of one
routine had made the enemy shape look universal; this one opens `CMP #$005C` and
then stops rhyming. There is **no damage table, no health word, no `$5E`, and no
`JML` into the `$81:8506` family**. Damage is a plain countdown — one hit is one
`DEC $0C`. It answers exactly three ids: `$5D` costs it a point, `$64` and `$6F`
are only tallied into `$0A`, and everything else falls off the end of the
comparisons into a `STZ $22` that **erases the id it parked two instructions
earlier**. Every exit is `CLC`, so unlike an enemy it never parks its thread.

**Its reaction to a killing blow is a third mechanism.**
`enemy_survived_react` splices a `JSL` frame into a suspended thread's own stack
and `monster_survived_react` does the same one page over; `$81:CC0A` just swaps
its own next-routine pointer — save `$16` into `$26`, install `$81:CC2F`, arm a
30-tick timer, put the countdown *back up* to `$14`, and set `ACTOR_ATTR_SET` and
`ACTOR_ATTR` = `$0C00` on its record. `$81:CC2F` is the undo. No stack, no
splice, nothing suspended. And the timer at `$24` is `react_already`'s guard
asked of a page rather than of a flags word — with a consequence, because a
killing hit that lands while it is already reacting is *tallied* instead of acted
on.

**`$81:B592` is twenty-four bytes and the smallest handler in the game**: two
comparisons, a decrement, an increment, three `RTL`s. It is also the only one
that never tests `COLLIDE_ID_PLAYER` — the two ids it answers to are **`$07` and
`$08`, far below a weapon shot** — so whatever hurts this thing hurts it by
touching it. Neither it nor `$81:CDDE` declares a guard, and that is the entry
worth noticing rather than an omission: they are the first collision handlers in
the project with **nothing to hand back**.

**And here is the honest part: between them, seven of nine branches are
transcribed rather than diffed.** Every one of the 37 calls the corpus makes to
the two of them took the *ignore* branch. `cdde_counted`, `cdde_survived`,
`cdde_killed_reacting`, `cdde_react_begin`, `cdde_unmatched`, `b592_survived` and
`b592_exhausted` all read zero on all sixteen movies. The diff proves what those
routines do with an id they do not care about, and nothing else.

**Nine perturbations, three caught, and the numbers say exactly that.** Clearing
the parked id at the wrong offset, taking Z as set, and leaving carry set are all
caught on the ignore path. The five damage-path and reaction perturbations are
not, because nothing reaches them. **The ninth is the interesting one: deleting
the `STZ $22` itself passes every call**, while writing that same zero to `$20`
fails at once — so `$22` is *already zero* every time, because the only thing
that ever parks anything there is a weapon shot and no input lands one on this
creature. It is `enemy_die`'s `STZ $7E` again, with one difference recorded
beside the line: this one stops being a no-op the moment a shot arrives.

**The input those seven branches want was chased and it is behind a door.**
`$5D` is a weapon id, so the damage path needs the player shooting this creature
with a *second* weapon. Pressing B does not do it — level 29 hands the player one
weapon, and `$80:EA63`'s first exit is `CMP $1CBC,X : BEQ`, cycling to the weapon
already held; four B-press variants of `movies/level29-fighting.zmv` were built
and every shot in all of them still carried `$005C`. Level 29 does place two
weapon pickups — type `$24`, collision id `$10`, at (983,425) and (981,940) — and
**the router cannot reach either**: `no route` from level 29's start (135,1215)
to both, on the same 2x2-clear grid that proved level 41's first-aid object
unreachable. Level 29 is an interior level and those are behind doors. So the
movie that settles this needs a key and a door first, and that is a round of
route-finding rather than an afternoon.

**Twenty-eight routines.** All sixteen movies verify clean — 92,202 / 162,892 /
152,793 / 95,497 / 103,919 / 130,092 / 109,937 / 102,143 / 103,675 / 76,203 /
194,731 / 178,293 / 148,803 / 150,402 / 109,577 / 82,510 — `run` keeps every byte
of live game state identical on all of them, and the playable build still matches
`zamn_headless` pixel for pixel on level 29.

---

*The rest of this section is the previous round, kept as written.*

### The same routine a third time (2026-07-29)

**`$81:B41C` is ported, and it is `$81:8888` again.** The census had it at the
top of level 29's list — 66 declines on `movies/level29-fighting.zmv` — and it
turned out to be the *third* copy of the enemy collision subsystem, after
`enemy_collide` and the giant spider's `monster_collide`. Same `CMP #$005C`
opening, same `AND #$7FFF`, same `ENEMY_DAMAGE_TABLE` at `$81:8561` indexed the
same way, same two ids (`$5E`, `$5D`) leaving through the same two routines, and
the same `JML $81:8506` for a survivor — **shared rather than re-spelled**, which
works because this page keeps its display record at `$08` where
`enemy_survived_react` already looks for it. Three relocations: health `$0C`
against `$1E` and `$22`, the parked id `$5A` against `$22` and `$20`.

**Two differences are real, and one of them is an outbound message.**
`$81:B437  INC $4C` raises a flag on every hit that reaches the damage path, and
the actor's own body consumes it on its next pass — `$81:AEC5  LDA $4C : BEQ :
STZ $4C : JMP $AF4E`. It is `ACTOR_DP_DEATH_REQ`'s shape one rung down: that one
says "take yourself apart", this one says "you were hit". `$81:B437` is its only
writer in the whole bank.

**The other is a death that pays nothing.** Where `enemy_collide` reaches
`$81:8727` and awards `ENEMY_DEATH_AWARD`, this copy does `DEC $0A` and returns.
Two decrements exist for that word — this one and `$81:AEE9`, where the body
gives up after `$80:B26B` hands back no target — and **no initialiser and no
reader anywhere in this actor's code**, so what it counts is not established and
`B41C_DP_COUNTER_0A` is named for where it lives. Same discipline as
`VICTIM_DP_FLAG_26`.

**And there is a third accepted id, which is a weapon with an opinion about
height.** `CMP #$0061 : BEQ` has no counterpart in either twin, and what it
branches on is `LDY $08 : LDX $0004,Y` — record `+4`, which is `ACTOR_Z`. One
standing **on the ground** gets `$81:B168` (three instructions: queue `$81:B16E`,
which swaps its metasprite out of the table at `$81:B199`, sleeps `$20` ticks and
re-installs this handler); one **in the air** falls through and takes the damage
any other id would do. The body tests the same field the same way at `$81:B217`.

**66 of 66 passed on the first run.** Measured 84..1312 cycles, mean 235; two
bytes of stack. All sixteen movies verify clean — 92,202 / 162,892 / 152,793 /
95,497 / 103,919 / 130,092 / 109,937 / 101,743 / 102,596 / 76,203 / 194,731 /
178,293 / 148,803 / 150,402 / 109,577 / 82,510 calls, no divergence anywhere —
and `run` substitutes it over 4,089 of 4,089 passes with at most 25 bytes
differing at once, all inside the stacks or a declared scratch byte.

**Twelve perturbations, ten caught.** The health offset, the parked id's slot,
the hit flag's slot, the counter's slot, dropping the `INC`, dropping the `DEC`,
not storing the dead enemy's negative health, returning carry clear so the thread
is never parked, and moving the `INC` below the death exit — all caught, most on
the first call. **The two misses are both named by something that already
exists.** Dropping the `ASL` that doubles the damage index passes every call on
every movie, because **every hit that reaches this creature in the entire corpus
carries id `$005C`**, whose index is 0 either way — sixteen movies were
instrumented to check that rather than assumed. It is `player_pickup`'s doubled-id
index exactly, before `movies/level1-pickups.zmv` existed. And inverting the `$61`
ground check is invisible because `b41c_special_grounded` and
`b41c_special_airborne` both read zero: no input fires that weapon at this
creature. Both are work lists for a movie, not for code.

**A bug fell out of reading the same two branches twice, and it was in the
existing port.** `ENEMY_SPECIAL_A_ENTRY` and `ENEMY_SPECIAL_B_ENTRY` were paired
with the wrong ids — the `_A`/`_B` suffixes follow `ENEMY_HIT_SPECIAL_A`/`_B`
(`$5E`, `$5D`) but the addresses were written down in the order the two `JML`s sit
in the ROM, which is the opposite order: `$8897  BEQ` lands on `$88C0  JML
$8183C6`. Nothing could see it. Both ids decline either way, so the only thing
the constant chooses is the address the *census* prints, and neither id has ever
been reached — `enemy_hit_special` has been untaken since it was written. Fixed,
and the reason it was invisible is recorded beside it.

**The creature is not identified, and the routine is named for its address.**
No actor in level 29's placement list names its body; it is spawned rather than
placed, entered through `$81:AEA6`, and nothing ties it to a sprite. So it is
`enemy_b41c_collide` until something proves what it is — the same choice
`VICTIM_DP_FLAG_26` makes, applied to a routine.

**Twenty-six routines. Coverage 134 sites; level 29's census is down to one**,
`$81:CDDE` at 32, from three.

---

*The rest of this section is the previous round, kept as written.*

### The port is playable (2026-07-29)

**You can now sit down with a keyboard and play a build that runs the port.**
Until this round the substituted build had no picture and the build with a
picture had no port: `zamn_cosim run` substituted for real but headless, driven
by a recorded movie and shadowed by a reference core, while `zamn.exe` was still
the Phase 0 baseline — linked against `snescore` and nothing else, executing
zero native code. Those were the only two options and neither was the thing.

**The join needed no new mechanism, which is the finding.** `cosim_init` hooks
the program counter rather than the machine's state, so it neither needs a reset
nor cares where input comes from or whether a second core exists. The frontend
is `COSIM_NATIVE` against the one live core and two lines of difference:
`cosim_frame()` where `snes_runFrame()` was. **Those two are the same function** —
the same drain-vblank / run-to-vblank loop, and `snes_readBBus(snes, 0x40)` is
`snes_catchupApu` by another name — differing only in that one steps through
`cosim_step`, which watches the PC. So `--stock` is not a second code path, it is
this one with `enabled` cleared, and **F1 moves between them mid-game**: clearing
the mask stops new interceptions while a resumable routine already parked
mid-call still resumes through the port, because `cosim_step` matches a
suspension by its resume address and not by the mask.

**The claim is checked three ways round, and the third is new.** `zamn_headless`
(plain `snes_runFrame`, no harness at all), `zamn --stock` and `zamn` with all 25
routines substituted produce **byte-identical framebuffers at frame 4000 on six
movies** — level 1, level 1 keys, level 1 two-player rescue, level 29 fighting,
level 45 race and level 53. That is a stronger result than lockstep's, which
compares WRAM per scheduler pass and is free to let the *picture* drift; here the
substituted build agrees with an unharnessed one pixel for pixel. The report at
exit says how much ran natively while it did: on level 29 fighting, **4,528
`sprite_build_oam`, 4,528 `thread_tick_waits`, 1,344 `sprite_frame_tile`, 173
`thread_spawn`** — the sprites on that screen were emitted by the port.

**Throughput is 236 fps uncapped**, four times the budget, so the per-opcode
interception costs nothing that matters. `--frames N` runs uncapped and exits and
`--shot` writes the final frame, which is how the above was measured without
anyone playing it; `-m` replays the corpus with the picture on.

**Two bugs, and both were in the frontend rather than the port.** The movie was
indexed by `snes->frames`, as `cosim_lockstep` does because its two cores keep
separate clocks — with one core there is nothing to drift from, and headless's
own loop counter is what the corpus was fitted against, so the movies landed a
frame out. And `--shot` reused headless's pixel conversion, which is wrong here
by one byte: `ppu_setPixelOutputFormat` **shifts** the channels rather than
reordering them, `[B,G,R,X]` under `pixelFormatXRGB` against `[X,B,G,R]` under
the `pixelFormatRGBX` the SDL texture wants. It does not crash, it turns level
1's grass brown — and it was caught only because the three-way comparison
against headless existed to catch it. The two same-frontend comparisons agreed
with each other perfectly the whole time, being wrong in the same way.

**What this is not is a native game, and the title bar says so.** The main loop,
the NMI handler, player movement, the level and camera code and every enemy body
are still the ROM's, under the core; what runs natively are the leaves they call.
The window title carries a live count of calls served and declined, which is the
honest description of the build: an emulator with two dozen of its hot routines
executed as C. Phase 4 is where that inverts.

---

*The rest of this section is the previous round, kept as written.*

### Twenty-five routines, and no census left (2026-07-26)

**Four of the five transcribed entries are diffed now, and one of them was
wrong.** The previous round ported `$80:FA26`, `$80:FA4A`, `$80:FA79`,
`$80:FAA4` and `$80:FACF` ahead of any input that reached them and said in as
many words that their own stores were transcribed rather than checked. Seven
new movies land this round, all fitted rather than played, and four of the five
entries now have one — and **the first call of the first one failed**:
`$80:FA4A` ends
`LDX $0E : LDA $1D4C,X : CMP #$0005 : BCS : INC A : STA $1D4C,X`, and **the
`LDX` is an output as well as an index**. The port had been leaving X as
whatever `thread_spawn` returned four instructions earlier —
`X: ROM $0000, port $0022`, with all 128 KB of WRAM matching. `$80:FA26` had
the identical error and still has no input; it was fixed by reading the listing
again, which is a weaker thing than fixing it by diffing and is worth saying
so.

**The round's opening question had a one-line answer and a different problem
behind it.** `$80:C9E3` copies the object list's X and Y into the display
record verbatim — the only indirection is the collision id, out of `$80:CA30` —
so the coordinates were never why standing on an object did nothing.
`zamn_headless --records` is the new instrument that says so: it walks
`ACTOR_NEXT` from `$7E:1B5E` and prints every live record's position, collision
id and owning thread, plus both score slots. One frame of it is the whole
finding:

```
  frame 3790 — display list, score 00000000 / 00000000
    $1AB6  $8001    222  1050   $05   $00     <- the player
    $1A7A  $8011    230  1050   $04   $24     <- a monster, on the same object
    $1AA2  $8001    230  1044   $30   $26     <- the object
```

**An object is contested, and the display list decides.** `$80:CAEE` accepts
three collision ids and the third, `$0004`, is the monster side — its entry in
the player's own jump table is `$80:F950`, the hit path. `actor_overlap_pass`
walks pairs from the end of the list, so whichever of the two the depth sort
put later is asked first, and `$80:CAEE` clears the object's collision id on
that first ask. The loser's handler is then called with an id of zero, which
for the player is `$80:F87A`, a bare `RTS`. The report counts it from both
ends in the same run: ** `object_spent` 3, `player_no_effect` 3.**

**So a route to an object is a race, and `tools/fit_route.py` was losing it by
being too careful.** Its arrival test was three pixels; the game's box is eight
( `$80:BEF1`, `other - self + 8 < 16` on each axis). Seven — the box with the
asymmetry taken off — took level 45's route from **56 legs to 16** (and to 14
once the stalls went, below), and its other bonus object from a chained detour
to twelve legs.

**`$82:E0B4` is identified, and it is the thing you just picked up flying away.**
A display record at the position it was handed, no collision id, a metasprite
from four words at `$82:E147`, one of four diagonals from `$80:9D39`'s random
number, eight pixels a tick for 21 ticks, then the slot is freed. Those four
metasprites are the last four entries of `$80:CA6C`, the object-type table, so
the sprite that flies off is the object's own — which is what the `kind`
argument is for, and which makes a pickup legible in a `--records` dump without
running the harness at all.

**Two movies are committed for failing, and they earn it.**
`movies/level45-contested.zmv` is the 56-leg route that loses the race;
`movies/level45-carried.zmv` is the one where level 45's spiders **pick Zeke up
and carry him**, so his own collision id reads `$38` and he walks over two
bonus objects taking neither. Between them they are the only witnesses to
`object_spent`, and the second is the only input that puts the *player* on
`object_ignore`. A movie that does not do what it was aimed at is still the
only witness to how it did not.

**The fitter can shoot now, and that alone was worth a branch.** `--fire` holds Y
down the whole way, and it is free to a closed loop for exactly the reason lane
snapping is: re-planning after every leg measures where the player *is*, so a
shot that changes the board only changes the next search's starting point.
(Patching `+Y` onto a finished movie does not work, and was tried.) What it
bought first was not the contested object but ** `heal_capped` **: walking to
level 29's first-aid object costs nineteen hits and arrives on six or less, so
`$80:FACF` adds three and stores it, while shooting costs fifteen and arrives
on seven to nine, where three would overshoot the ceiling and the entry clamps.
Two routes to one object and the difference between them is a branch.
`movies/level29-fighting.zmv` is also the highest-coverage single-player input
in the corpus at **51 of 111**, because firing turns on a subsystem the walking
routes never touch — `enemy_collide` runs 47 times there and zero here.

**And `$80:CAEE` is now diffed on all three of its exits.** `object_ignore` — an
id that touches an object and does nothing to it — needed something that is
neither a player nor a monster, and this round produced both such things: a
shot in flight (id `$5C`, which `--fire` puts over an object for the first
time in the corpus) and a player being carried (id `$38`).

**Coverage: union 76 → 89 of 111, and twenty-two are untaken by every input,
down from thirty-five.** The thirteen are `player_spawn_1`, `player_spawn_2`,
`player_spawn_3`, `player_heal_entry`, `heal_capped`, `score_digit_carry`,
`victim_event_4`, `victim_ignore`, `draw_no_meta`, `object_spent`,
`object_ignore`, and the two decline sites `handler_unported` and
`collide_unported`, which had gone quiet when level 1's last handler was
ported and which three unmapped levels put straight back. `draw_no_meta` is the
interesting one: `sprite_build_oam`'s guard against a drawable record whose
metasprite pointer is not in cartridge ROM has read zero since the sprite pass
was ported, and level 29 takes it **151 times**.

**The eight older movies check the same totals they did** — 40,252 / 130,092 /
150,500 / 162,892 / 41,253 / 73,072 / 48,071 / 53,545, at 2400 / 6100 / 6000 /
6100 / 2400 / 4050 / 3600 / 3700 frames, which this file records for the first
time — so the `player_collide` change regressed nothing. The seven new ones add
49,098 / 65,448 / 67,530 / 54,342 / 69,768 / 95,261 / 76,127, and `run`
substitutes over 3,289 / 4,409 / 4,239 / 3,389 / 3,799 / 4,389 / 3,589
scheduler passes, at most 46 / 21 / 24 / 13 / 19 / 19 / 28 bytes differing at
once, all inside the stacks or a declared scratch byte.

**The last two entries were then hunted properly**, by matching every object in
every password-reachable level against the type table and routing to it from
that level's own start. ** `$80:FAA4` ** (id `$30`) has thirteen objects
across five levels and exactly two are routable, both level 45's; the far one
is 400 cells away across the whole level and the fitter does not arrive — 42
legs, stopping at (922,466). The near one took six goes. Three routes lost it
to a monster standing on it. A fourth tried to win the *sort* instead of the
race — approach along the lane above it, so the player sorts later than the
object and is asked first — only for the monster to match his row exactly and
take the tie. That route found the other half of the guard: **level 45's
spiders pick Zeke up and carry him**, and while carried his own collision id
reads `$38`, which is not one of the three `$80:CAEE` accepts, so he walks
over things and nothing happens.

**The sixth collected it, by a correction to something written above.**
With `--fire` the record vanishes at **frame 3466**, 936 frames into gameplay,
with the player still 434 pixels away at (346,1158) — the monster that took it
was never near him, so this was never a race lost at the object, it was a
deadline. Pricing that deadline looked like a route problem: 226 cells is about
1,800 pixels, the player moves 2 pixels a frame, so the journey costs 904
frames at best against a fitted 1,182, and the 280-frame gap got written up as
lane-snap detours.
**Then it was measured, and it was not detours.** Summing `--pos` frame by frame:
1,838 pixels travelled against an 1,808 optimum — thirty pixels of detour — and
**263 frames of standing still**, in blocks of 103, 43 and 43, each ending exactly
on a leg line. A leg whose target the player never reached was running to the
end of its padded replay window before the fitter re-planned. Ending such a leg
where he *stopped* is worth **151 frames**.

**And 151 frames is still not enough on its own, which is the better half of the
finding.** The stall fix without `--fire` arrives at frame **3489** — earlier
than the movie that wins — and loses anyway: at 3460 the object is live, a
monster is standing on it at (222,1046), the player is forty pixels below at
(222,1086), and by 3470 it is gone. He watches it happen. So the clock is
necessary and not sufficient, and the difference is in the display list rather
than the arithmetic: at frame 3550 of the winning route the object is still
there, the player is eight pixels below it, and the two id- `$04` monsters
nearby are *beside him rather than on it*, with one of his own shots just
above. Shooting keeps the thief interested in the player instead of the object.

`movies/level45-race.zmv` collects it, and one word of WRAM proves it —
`score 00001000` at frame 3600, which is what `$80:FAA4` awards and nothing
else in the game does. ** `player_spawn_3` is taken**, 76,127 calls check
clean, and it is the only movie in the corpus that takes all three of
`$80:CAEE`'s exits at once: `object_taken` 19, `object_spent` 2,
`object_ignore` 196.

**And the fifth entry is not waiting for a movie — there isn't one.** `$80:FA26`
looked like the same kind of work-list item as the others and is a different
kind. Collision id `$2D` is object type `$34`; type `$34` is in exactly three
level object lists, and routing fails not only to each object but to **all 225
positions in the ±7 collision box around it**. The decisive probe is smaller
than that: `route` from one of those objects *to itself* answers "1 cells", so
its cell is walkable — and sampling all of level 33 on a 32-pixel grid from
that cell reaches **zero** of 1,680 points. They are one-cell islands. Nothing
drops one either: `$82:DC57` rolls `$80:9D39` into a 256-byte table at
`$81:E79F`, and type `$34` is not among the 22 types in it — `$38` and `$3A`
appear six times each, which is how the other two could have arrived by luck.
Those are the only two writers of the object-type array, which the trace's WRAM
map confirms.

**The one lead that looked like a way in was a mislabelled column, and
correcting it is the round's last finding.** `zamn_assets actors` printed the
actor placement byte as `id`, and fourteen placements carry `$2D` — one of
them 72 cells from level 29's start. They are actor **types**. `--records`
settles it: the record at level 29's (290,1302) carries `ACTOR_COLLIDE_ID`
`$00` where the list says `$2D`, and level 46's monsters carry `$03` and `$04`
, neither of which appears anywhere in that level's actor list. An actor's
collision id is written by its own body, not by its placement. The tool prints
`type` now, and `src/assets/actor.h` records what proved it.

The walkability predicate was re-derived rather than trusted on the way past,
and it holds: `$80:AE1F`'s `LSR A : LSR A : AND #$FFFE` makes the column index
pre-doubled, because the expanded map is one **word** per cell, so the
`LDY #$0002` and `#$0004` after it name columns c+1 and c+2 rather than c+2 and
c+4. A contiguous 3x2 block of cells, 24 px by 16 — which is what `route_open`
already did.

So `player_spawn_0` is untaken and will stay untaken, and that is the honest
end of it: five routines were ported ahead of their inputs, four now have one,
and the fifth has no input to have.

**And then the census named the routine every finding above had been describing.**
`$81:C4A6` is ported — **2,039 declines across the four level-45 movies**, more
than everything else on the list put together. `$81:C3B6` installs it, and that
is the body of level 46's type-`$14` actor: the giant spider. It is a **second
copy of the enemy subsystem**, not a variant of the first — same damage table at
`$81:8561`, same shape, and each routine it leans on has a ported twin
(`$81:BBEB` is `$81:8727` at three times the award; `$81:BAB3` and `$81:BB05` are
`$81:8506`). What it does not share is the page: health is `$22` here and `$1E`
there.

**Its object branch is the theft, in three instructions.**
`LDY $08 : LDA #$0003 : STA $000E,Y` rewrites its own display record's collision
id — which is exactly
the `$04` -> `$03` transition `--records` caught at frames 3466 and 3790 when a
bonus object vanished. The dump and the disassembly reached the same three
instructions from opposite ends.

**175 of 186 passed on the first run, and then one call failed.**
`movies/level45-carried.zmv` is the only movie in the corpus where one of these
dies, and it said `A: ROM $0000, port $0300`. `$81:BBEB` ends
`LDA $20 : AND #$8000 : ASL A : ROL A : ROL A : TAX : INC $1FD4,X`, so nothing
`score_add` returns in A survives — the parked id is reloaded over it and reduced
to the side, 0 or 2. One death in fifteen movies was enough to catch it.

Six perturbations, five caught: the record id, the latch guard, the `$46`
redirect (whose site fires exactly once in the whole corpus), health read from
`$1E`, and the countdown. **Not caught: collapsing the two ignore exits**, which
leave different flags but can only disagree on `arg == $33` — and `$80:CA30`
stops at `$30`, so nothing in the game carries it. Written down beside the line.

**And the survive path went in behind it**, because the census named `$81:BAB3`
the moment `monster_collide` was registered and it turned out to be **`$81:8506`
instruction for instruction** — the same three-byte gap, the same three words
slid down lowest-first, the same far return address written into the hole so the
scheduler resumes through a reaction nobody called. `ENEMY_REACT_FRAME` is
shared rather than re-spelled. Three differences and all at the edges: the guard
reads `ACTOR_ATTR` whole where the twin reads bit 4 of the flags word that
*selects* it (spelling it the twin's way fails at `$7E:0D82`, ROM `$80` against
port `$83`); what comes back on that path is therefore data rather than a
constant; and the spliced address writes `$0C00` into `ACTOR_ATTR` where the twin
sets a bit.

Five more perturbations, four caught — the guard field, the words moved
highest-first, the return address not decremented for its `RTL`, the two
overlapping stores swapped. **Not caught: dropping the already-reacting guard**,
which is precisely the twin's blind spot: `react_already` has never been taken
and neither has `monster_react_already`, and an entry guard that only ever reads
zero is a thing no diff can check.

**Twenty-five routines, and the four level-45 movies now print no census section
at all** — every call offered is served, 186 of 186 and 1,138 of 1,138.
**Coverage 89 → 100 of 125**, twenty-five untaken. All fifteen movies verify with
no divergence; the level-45 totals are 54,478 / 70,347 / 96,399 / 76,313, and
`run` substitutes over 3,589 and 4,389 passes at most 28 and 21 bytes differing,
all inside the stacks or a declared scratch byte.

The census that remains is level 29's — `$81:B41C` (163), `$81:CDDE` (112),
`$81:B592` (5). Three more handlers, on a level whose actors are a third kind
again.

---

*The rest of this section is the previous round, kept as written.*

### The route-finding became a tool (2026-07-26)

**The route-finding is a tool now, not an afternoon.** The previous round ended
with five routines ported ahead of any input and an admission that every route
tried had run into a wall. That is the wrong way round: the level already says
where its walls are. Bit 0 of a tile's attribute word blocks movement
(`$80:AE43  LSR A : BCS`), the expanded map gives a tile index per 8x8 cell, and
`zamn_assets route <level> <x0> <y0> <x1> <y1>` breadth-firsts between two points
and prints the turns. `--frames` prints .zmv lines instead, and
`tools/fit_route.py` closes the loop on the timing — it replays the movie with
`zamn_headless --pos`, finds the frame each leg actually finished on, and moves
the next turn there.

**It reproduces a route cut by hand.** `route 2 350 585 251 302` prints *Left 98
px, Up 288 px*, which is `movies/level1-pickups.zmv`'s first two lines and cost a
dozen headless runs to find the first time.

**And it proves a negative, which is what the afternoon could not.** `route 42
1534 703 1524 557` — level 41's start to the first-aid object 156 pixels from it
— comes back **no route**. The two are not connected on a 2x2-clear grid at all,
so either that object is behind a door or the way in is somewhere else entirely.
Twenty screenshots could only have said "not this way".

**The box is the game's own, and reading it out was the round's other finding.**
`$80:AE1F` turns a position into a map index as `col = x / 8` and
`row = (y - 8) / 8` — an eight-pixel bias nobody had noticed — and then samples
**six** tiles: three along that row at `+0`, `+2`, `+4` and the same three on the
row beneath. Standing somewhere needs twenty-four pixels by sixteen clear, not
the sixteen-by-sixteen the first version guessed.

**And the route-follower closes the loop, which is what finally made it work.**
A path is walkable on the row it was planned for and on no other — these
corridors are exactly two rows tall, and widening the search by a single row
disconnects level 1's own route — so a leg that snaps the player into a
neighbouring lane invalidates every leg after it. Level 45's first attempt
walked into a wall at x=788, at y=1226, one row below the 1220 it was planned
for. That is a *positional* error and no amount of better timing fixes it, so
`tools/fit_route.py` re-plans instead: route from where the player is, take only
the **first** leg, replay to find the frame he actually finished it on, ask
again. Lane snapping stops being an error to correct and becomes the position
the next search starts from.

**It walks level 45's maze**: `arrived at (228,1042) after 36 legs`, where the
plan had eight, on a level nobody has mapped and without anyone looking at it.

**And then it does not pick the object up.** Standing on an object's listed
coordinates is not the same as touching it: the run ends two pixels from
`$80:FAA4`'s object and `player_spawn_3` still reads zero. So the five
transcribed jump-table entries are still transcribed — and the question is now a
third one, sharper than either of the last two: **where is an object's display
record actually built?** The list says (230,1044); something disagrees. An
object's *collision id* already turned out to come from `$80:CA30` rather than
from the list, so a second indirection would not be a surprise.

---

*The rest of this section is the previous round, kept as written.*

### Five entries ported ahead of an input (2026-07-26)

**Five more jump-table entries are ported, and for the first time they are
ported *ahead* of an input that reaches them.** `$80:FA26`, `$80:FA4A`,
`$80:FA79` and `$80:FAA4` are one routine written four times — play the touch
sound, copy two words of position to the top of the player's page, write a
**kind** under them (0, 1, 2, 3), `thread_spawn` `$82:E0B4`, and then one tail
apiece: a counter, a counter that stops at five, `$0500` of score, `$1000` of
score. `$80:FACF` is the fifth and the only entry in all 57 that gives **health**
back: three of it, ceilinged at the same ten `$80:EB2F` refuses to spend a
first-aid kit at.

**The interesting instruction is a rotate.** `$80:FA97  LDX #$0500 : LDA $0C :
ROR A : ROR A : ROR A : JSL score_add`. `$0C` is the player's number already
doubled, 0 or 2, and `score_add` reads **bit 15 of A** to decide whose points
these are — so three `ROR`s are how a player index becomes a sign. The carry
that shifts in on the way lands in bit 13 where nothing reads it, and it comes
from `apu_play_sfx` five instructions earlier, because nothing in `thread_spawn`
touches carry. It still has to be reproduced exactly: the diff reads it even
though the game does not.

**What is honest about this round is what it does not claim.** Every routine
*under* those five is diffed on thousands of calls — `apu_play_sfx`,
`thread_spawn`, `score_add` — but their own half-dozen stores are **transcribed,
not checked**, because no input reaches them. The objects carrying their ids are
in levels 9, 17, 21, 25, 29, 33, 37, 41, 45, 49 and 53; passwords get to all
eleven, and the nearest is 156 pixels from a start position — but every route
tried so far runs into a wall, because those are mazes nobody has mapped. Eight
new coverage sites name exactly what is unproven, and they read as a work list
of *movies*.

That is a different shape of entry from anything before it. Until now the port
grew by following the census: something declined, it got named, it got written.
These five went the other way — the census could not name them because nothing
had ever dispatched to them — and the coverage report is the only thing keeping
them honest.

**111 sites now, and the union is unchanged at 76**, because all eight of the
new ones are untaken by every input. All eight movies still verify clean.

---

*The rest of this section is the previous round, kept as written.*

### thread_spawn (2026-07-26)

**`thread_spawn` is ported, so the port now creates the game's threads.**
`$80:825E` is the other end of the trick `$81:8506` had just shown: that one
splices a call into a thread that already exists, this one manufactures the
whole parked state of one that does not. Everything the scheduler needs is nine
bytes on the new thread's own stack — `$80:8390` resumes with `TCS : PLD : PLP :
PLB : RTL` — so a spawn writes a direct page, a processor status of zero, a data
bank, a far return address that lands on the entry point, and under it a second
one to `thread_exit`. **A brand-new thread and a thread parked mid-`yield` are
the same nine bytes.** Its last act is the one that makes it an interface rather
than an allocator: it copies the **caller's first five direct-page words** onto
the new thread's page, which is how `$80:FA26` passes a position to whatever it
spawns.

**The sample arrived for free.** Every shot either player fires is a spawn, so
registering the routine put **330 calls on `movies/level1-2p.zmv`** under the
diff without a new movie — the same shape as `apu_send`. Twenty-four routines
now, and `verify` totals are 40,252 / 130,092 / 150,500 / 162,892 / 41,253 /
73,072 / 48,071 / 53,545.

**The first run caught something on call 1 that only a register diff could
see.** The ROM returns **Y = 8**, not the bank it was called with: the argument
copy ends `LDY #$0008 : LDA ($01,S),Y` and nothing puts Y back. All of WRAM
matched; only Y did not.

**Six perturbations, five caught.** The slot search run upwards fails at
`$7E:0100` on call 1, because a different slot is a different page; the
`thread_exit` address not decremented for its `RTL` at `$7E:114D`; the two
overlapping frame stores swapped at `$7E:114E`; a thread marked runnable this
tick instead of the next at `$7E:11AE`; and **copying four of the caller's five
words instead of five, which fails on call 10 rather than call 1** because the
fifth word is only sometimes non-zero. Not caught: dropping the two stores that
clear the slot's handler, because every slot handed out already has a zero
there — recorded beside the line, like the `STZ $7E` in `enemy_die`.

**Coverage is 103 sites with a union of 76**, and `run` substitutes over 2,389 /
6,089 / 5,989 / 6,089 / 2,389 / 4,039 / 3,589 / 3,689 passes at at most 18 / 33 /
36 / 37 / 33 / 27 / 25 / 33 bytes, no byte of live game state ever differing —
with the port creating the threads.

---

*The rest of this section is the previous round, kept as written.*

### A call injected into code that is not running (2026-07-26)

**`$81:8506` is ported, and it is the routine this page has called the most
interesting unported one in the game for four rounds.** An enemy that survives a
hit reads its own thread's parked stack pointer out of `W_THREAD_SP`, moves it
down three bytes, slides the top three words down to meet it, and writes
`$81:8541` into the gap — so the next time the scheduler resumes that thread, the
`RTL` it resumes through returns into `$81:8542` first, which sets
`ACTOR_ATTR_SET` on the enemy's display record, sleeps two ticks, clears it, and
only then returns into whatever the enemy was actually doing. **The flash you see
when you shoot something that does not die is a call injected into code that is
not running.**

**And that is what `draw_attr_set` was.** The coverage report has carried it
untaken for six rounds under the description "`ACTOR_ATTR_SET` overrode a
record's palette", with no idea what set the bit. `$81:8542` sets it. Nothing had
ever taken the site because nothing had ever survived being shot.

**The password round is what made it reachable, and the effect was not limited
to the one item.** `movies/level53.zmv` types XDSJ, which is level 53 — eleven
actors, all running `$81:8C17`, whose init is `LDA #$0004 : STA $1E` — and holds
**four coverage sites alone**. Only two are this round's (`enemy_survived`,
`react_splice`). The other two have been on the untaken list far longer:
**`shot_expire_zero`**, the one of `shot_collide`'s three exits that runs no
`CMP` and so is the only one whose carry is the caller's, and
**`victim_latched`**, which `docs/cosim.md` called the sharpest example in the
project of what a diff cannot see. Neither needed code. Both needed a level that
is not level 1.

**The threads question turned out smaller than it looked.** `docs/threads.md` has
carried this routine as the thing the port's coroutines would eventually have to
answer, because a `PortCoro` parks as plain copyable data with no machine stack
to splice into. That was right about the mechanism and wrong about the work: the
splice is **twelve bytes of WRAM**, and the thread being spliced into belongs to
the ROM, because every enemy body in the game still does. The open question is
now the *other* direction — splicing into a thread the port owns — and nothing in
the game does that yet.

**Eight perturbations, seven caught, and two of the seven could only have been
caught here.** `LDA #$0081 : XBA : STA $0006,Y` and `LDA #$8542 : DEC A : STA
$0005,Y` are two overlapping 16-bit stores that between them lay down three
bytes, and doing them in the other order fails at `$7E:0F08` — ROM `$85`, port
`$00`. The three-word move overlaps its own source by three bytes, and copying
highest-first fails at `$7E:0F02`. Neither is a branch; no mark could name
either; both are caught because the harness compares memory rather than control
flow. **Not caught: deleting the already-flashing guard**, which `react_already`
predicts by reading zero — one player's weapon cooldown is longer than the two
ticks a flash lasts — and which joins `victim_latched` and `object_spent` as an
entry guard no diff can check.

**`enemy_collide` also learned to name what it declines.** Its two remaining ids
now go through the census like `player_collide`'s, so `$81:847E` and `$81:83C6`
would arrive as addresses rather than as a count. Neither has ever been reached.

**Coverage: 101 sites, union 75, twenty-six untaken by every input** — up from 71
of 99 with twenty-eight untaken, and `verify` totals are 40,239 / 130,048 /
150,170 / 162,510 / 41,233 / 73,024 / 48,032 / **53,497** across the eight
movies. `run` substitutes over 2,389 / 6,089 / 5,989 / 6,089 / 2,389 / 4,039 /
3,589 / **3,689** scheduler passes, at most 18 / 33 / 34 / 35 / 33 / 27 / 23 /
**31** bytes differing at once, no byte of live game state ever differing. The
last of those is worth a sentence: `$81:8506` writes into a *stack*, and stacks
are the one thing `run` forgives — but it forgives `$7E:1000-$7E:12FF`, and level
53's enemy threads park around `$7E:0F00`, outside it. So the twenty splices are
compared in both modes.

---

*The rest of this section is the previous round, kept as written.*

### The password system (2026-07-26)

**The password system is solved, and level 1 is no longer the only level.**
`$82:B018` reads four letters out of four tables in bank `$82`, and
`zamn_assets password` now spells all 130 of them back out of the same tables —
so any of the thirteen levels a password can name is one command and one
regenerate away. `docs/password.md` is the format; `movies/level33.zmv` is the
first movie in the project that reaches a level other than level 1.

**Why this stopped being one work-list item among many.** The previous round
ended by noticing that three items had quietly changed kind: they were not
"nobody has written that movie yet" but "level 1 cannot do that". `$80:F8D6`'s
slot arithmetic wants a collision id that sits behind a fence; `$80:FA26` wants
one inside a hedge; and `enemy_survived` — with `$81:8506`, the most interesting
unported routine in the game — wants an enemy that lives through a hit, which
**level 1 has none of by construction**: all fourteen actors its list places run
`$81:87F8`, whose init is `LDA #$0000 : STA $1E`, against a damage table whose
smallest non-zero entry is 1. Level 33 places fourteen actors that run
`$81:8C17`, whose init is `LDA #$0004 : STA $1E`.

**The format, in one paragraph.** Four characters from the 21 consonants at
`$82:B178`; the entry buffer's characters 0 and 2 are swapped (`$82:B02D`), so
the validator's two comparands are `(c2, c1)` and `(c0, c3)`. The first selects
one of **13 groups** and the group is level **5 + 4i** — which is why the game
hands you a password every fourth level. The second selects one of **10
variants**, and the variant is not a level at all: it goes to `$7E:1D50`, the
victim gate, so the second half of a password is **how many neighbours are still
out there**. Only the tenth variant of each group is a level as it starts, and
`$82:B0BE  CMP #$000A : BNE : LDA #$0010` is the line that says so. One password
is code rather than data: `$82:B018` compares both words against "BCDF" and
answers with **zero** where a level number goes.

**The screen took three findings to drive.** Its thread yields four ticks a pass,
so a press must be held to be seen; the D-pad is read as a *change*
(`$82:B298  CMP $004E : BEQ`), so a held direction moves the cursor exactly once
and every step is a press and a release; and it times out 300 frames after the
last input. The grid's geometry came off `$7E:1E96`/`$7E:1E9A` rather than off
the picture — `$82:B333` wraps x to `$20`..`$E0` by 32 and y to `$47`..`$87` by
16 — and that is how the enter key was found to be at column 6 rather than
column 5, which is backspace. `tools/make_password_movie.py` emits all of it.

**Two passwords were checked in the emulator and both land where the tables
say**: WHRB gives `$1E7C` = `$0005` and the level card reads LEVEL 5; XJQY gives
`$0021` and LEVEL 33. The command also reads all 130 spellings back through the
validator's own search and says so.

**And the structural question is answered: the port runs a level it has never
seen.** Different tileset, different actor bodies, different music, 29 actors
against level 1's 14 — `verify` checks **48,032 calls with no divergence and no
decline**, and `run` substitutes the port over **3,589 of 3,589** scheduler
passes with at most 23 bytes differing at once, all inside the stacks or a
declared scratch byte. That is a claim the six level-1 movies could not make
between them.

**Coverage: union 70 → 71 of 99, and the site it added is one of the two oldest
gaps in the project.** `draw_attr_set` — `ACTOR_ATTR_SET` overriding a record's
palette — has been untaken since the sprite pass was ported, and this file has
carried it for rounds as "needing a record type level 1 does not spawn". Level
33 spawns one. `sort_key_first`, its twin, is still untaken. Twenty-eight sites
are now untaken by every input, down from twenty-nine.

---

*The rest of this section is the previous round, kept as written.*

### The same routine again, one array over (2026-07-26)

**The other pickup is ported, and it is the first one written twice.**
`$80:F8D6` is `$80:F87B` again over a second array — the same `PHX` around the
same sound effect, the same `SED : CLC : ADC` out of a parallel table, the same
`CMP`/`BCC` ceiling, the same `BPL` into a selector — with five constants
changed: base `$66` not `$64`, first id `$21` not `$0C`, amounts at `$80:F907`,
ceiling `$0099` not `$0999`, and the tail into `$80:EAA8`. And `$80:EAA8` is
`$80:EA63` the same way: twelve slots against fourteen, thirteen tries against
fifteen, no weapon-data lookup on the way out. **Two inventories, two routines
each.** Twenty-three registered routines now, and
`movies/level1-keys.zmv` checks **73,024 of 73,024** calls with nothing
declined.

**The port was small; the ordering was the work.** The player does not start
level 1 empty-handed — `$1CC0` is seeded to 7 and item slot 7 holds one
first-aid kit — so the array is never empty and four of the eight new decisions
are reachable only in one order. The movie: press **A** standing still (a full
lap that settles where it began); take a zombie's touch, which is what makes
`$1CB8` 9; press **X**, which spends the kit, and because that leaves the
counter at zero it ends `JMP $EAA8` with **nothing in the array** —
`item_none_found`, `$FFFF` stored over the 7; press **A** again, from that
`$FFFF` — `item_none_held`; take key 1, which auto-selects; take key 2, which
does not; press **A** once more. Seven things in that order take eight sites;
in any other order they take four.

**Three buttons are now accounted for and two of them were unknown.**
`$80:D25B  AND #$8000` is **B**, weapons; `$80:D26C  AND #$0080` is **A**,
items; `$80:D27D  AND #$0040` is **X**, use the selected item. All three
edge-detect, which is the same trap `B` fell into two rounds ago, twice more.
And `$1CB8` is **health**, not the countdown the symbol file called it: it is 10
at level start, a zombie takes one, and `$80:EB2F  CMP #$000A : BEQ` is a
first-aid kit refusing to be spent while it is still 10 — which is why pressing
X *before* the hit does nothing at all, and how the field was identified.

**Twelve perturbations, ten caught, and four of the ten landed on the APU.**
Taking the base from `$64`, testing auto-select with `== 0`, reading the
selection from `$1CBC`, and wrapping at the weapons' `$001C` all fail at
`$7E:001E` — the audio sequence counter — because each one makes the port take a
different path through the selector and play one sound fewer than the ROM. Three
more fail at `$7E:012E`, the search's countdown, which is the same address that
caught the *weapon* search's perturbations a round ago and is the evidence that
the two searches share one scratch field. One is caught only by the APU **bus**
diff and one only by flag N.

**Not caught: the slot arithmetic, and this time there is no route.**
`index / 2 - ITEM_ID_FIRST` passes all 73,024 calls for exactly the reason the
same error passed in `player_pickup` before `movies/level1-pickups.zmv` — both
keys are id `$21`, whose slot is 0. But level 1's only other id into `$80:F8D6`
is `$28`, object 8 at (1208,71), which sits on the far side of the fence along
the top of the map where walking under it stops at y=129 against a sixteen-pixel
box. Last round's version of this was "nobody has written the movie yet"; this
one is a property of the level. Also uncaught, and named in advance:
`item_pickup_capped` and `item_digit_carry`, which want ninety-nine keys.

**Coverage: 99 sites now, union 70, and the new movie holds nine alone** — more
than the other five put together. The six take 26, 42, 50, 56, 34 and **43**;
twenty-nine sites are untaken by every input that exists, up from twenty-seven
because two of the eleven new marks are among them. `run` substitutes
twenty-two of the twenty-three over 2,389 / 6,089 / 5,989 / 5,989 / 2,389 /
**4,039** scheduler passes, at most 18 / 33 / 34 / 35 / 33 / **27** bytes
differing at once, every one inside the stacks or a declared scratch byte.

**Two tools grew, and both were the bottleneck rather than a nicety.**
`zamn_disasm --force` decodes a range as code whether the CDL saw it execute or
not, tracking M/X from the `REP`/`SEP` it decodes and marking `~` on every line
it guessed — because Phase 3's work list is now made entirely of routines no
input has ever run, which is precisely what a CDL calls data. `$80:F8D6` and
`$80:EAA8` were both read that way before either was ported.
`zamn_headless --pos` prints where each player actually is, in the level's own
coordinates, by finding the thread page whose `$64` holds `$1CCC` or `$1CEC` and
reading the record at its `$08`. Every lane in the new movie's comments is a
number it printed; the route would not have been worth cutting without it. It
also corrected this file's map of the player's page: `$08` is the body record
and `$0A` is a second one that never moves.

**And `$80:FA26` is still unported with no input to offer it.** Object 5, its
only id in level 1, sits inside a hedge. That is a new kind of work-list entry —
not a movie nobody has written, but a routine this level cannot reach.

---

*The rest of this section is the previous round, kept as written.*

### Two pickups, two items (2026-07-25)

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
  512×480 PNG. `--pos` prints each player's world position every N frames, found
  by the thread page whose `$64` holds one of the two inventory bases; that is
  the movie-authoring loop's other half, and how `level1-keys.zmv`'s route was
  cut.
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
  `--force` decodes a range as code whether the CDL saw it execute or not,
  tracking M/X from the `REP`/`SEP` it decodes and marking `~` on every line it
  guessed. That is what a work list made of never-executed routines needs, and
  it is how `$80:F8D6`, `$80:EAA8`, `$80:EAE1` and `$80:FA26` were read.
- `movies/boot.zmv`, `movies/level1.zmv`, `movies/level1-rescue.zmv`,
  `movies/level1-2p.zmv`, `movies/level1-2p-rescue.zmv`,
  `movies/level1-pickups.zmv`, `movies/level1-keys.zmv`, `movies/level33.zmv`,
  `movies/level53.zmv` — reproducible input scripts. `level1.zmv` reaches
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
  gets aimed, and `zamn_headless -m <movie> --pos first,last,step` prints where
  each player *is* every `step` frames, in the level's own coordinates, which is
  the faster half of the same loop once a route has a target.
  `level1-keys.zmv` is the newest and the richest per frame: it walks Zeke over
  **two keys** and presses **A** and **X**, which is what puts `$80:F8D6` and
  `$80:EAA8` — the item pickup and the item selector — under the diff, and it
  holds **nine** coverage sites that no other input takes. `level33.zmv` is the
  first that reaches a level other than level 1: it types the password **XJQY**
  on the title screen's second menu entry, and its boot half is regenerated by
  `tools/make_password_movie.py` from any four letters (`docs/password.md`).
  `level53.zmv` types **XDSJ** and is the one that makes an enemy live through
  being shot: all eleven of level 53's actors run `$81:8C17`, health 4, which
  is what `$81:8506` needed and level 1 could never supply. It holds four sites
  alone, and two of them — `shot_expire_zero` and `victim_latched` — had been
  untaken since long before passwords were a thing.
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
  the command interface — a port of `$80:CB1A`/`$80:CB61`/`$80:CC7C`),
  `password.c` (**the password system** — the four tables at
  `$82:B14A`/`$82:B164`/`$82:B178`/`$82:B18D` read both ways, which is a port of
  `$82:B018` and `$82:B0DE` between them, and the thing that made a level other
  than level 1 reachable at all).
- `src/assets.c` → **`zamn_assets.exe`** — decodes ROM data to `.bin`/`.png`,
  and `verify-lzss` / `verify-level` / `verify-actors` / `verify-sprites` /
  `verify-music` diff the C decoders against the ROM's own routines under the
  reference core. `level <n> [out.png]` and `actors <n>` report and render any of
  the 56 levels; `sprite <bank:addr> [out.png]` and `frame <n> [out.png]` do the
  same for sprites; `password` spells all 130 passwords out of the ROM's own
  four tables and reads one back to the level and victim gate it means;
  `route <level> <x0> <y0> <x1> <y1>` breadth-firsts a walkable path through a
  level's own tile attributes and prints the turns, or .zmv lines with
  `--frames`, which `tools/fit_route.py` then fits to what the game actually
  does;
  `music` reports the 16 APU data sets and what each level
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
  (`$80:8398`, both vblank-queue adders, and **`$80:825E`** — how a thread gets
  its first stack frame, which is the same nine bytes the scheduler parks one
  with and the way a spawner passes arguments), `oam.c` (**the whole per-frame
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
  three more of the player's jump-table entries, two of which used to be blocked
  by the APU: `$80:F92D`, three instructions of pure sound effect;
  `$81:8506`, **the survivor's reaction** — the only routine in the game that
  writes into a *suspended thread's* stack, moving `W_THREAD_SP` down three bytes
  and splicing a return frame into the gap so the scheduler runs a two-tick
  hurt flash before the enemy carries on. Plus
  `$80:F87B`, **the player's side of a pickup** — a BCD add into an inventory
  counter, capped at `$0999`, which tail-calls into the weapon selector when the
  player is holding nothing; and `$80:F8D6`, **the same routine over the second
  inventory** — the *items* rather than the weapons, first id `$21`, amounts at
  `$80:F907`, capped at `$0099`, tail-calling into the item selector. It also names twenty-four
  direct-page fields those routines touch, across four different pages that
  disagree about every offset), `apu.c`/`.h` (**how port code drives the APU** —
  `$80:CC3B` and `$80:CCC8`, the first port code that touches hardware, and the
  three-way split between the byte of WRAM it owns, the bus traffic it hands to
  a host hook, and the wait it does not attempt. `$80:CCC8` is registered on its
  own entry as well, which is what puts the data-set uploader's **23,820
  commands per movie** under the diff, and it is the one routine in the registry
  marked `verify_only`: its body is a bus handshake, and the thing that performs
  a handshake is the CPU), `player.c` (**the player's
  weapon and item selection** — `$80:EA63` with `$80:EA4B` under it, and
  `$80:EAA8`, which is the same search over a twelve-slot array with thirteen
  tries instead of a fourteen-slot one with fifteen. The first ported code that
  is neither collision nor per-frame housekeeping, and the reason `B` is known
  to cycle weapons, `A` to cycle items and `X` to use one), `bcd.h` (the 65816's decimal `ADC`, which stopped being the
  score's private business when the pickup arrived), `score.c` (**the score** —
  `$80:C7D9` and the slot search at `$80:C7C2`), `coroutine.h` (**how a ported
  routine suspends** — one `resume`
  index plus a context struct, ~40 lines), `fade.c` (`$80:891A`, the first
  routine ported that uses it) and `coverage.h`/`.c` (**which of the port's
  branches any input has actually taken** — 111 marked decision points across
  twenty-three of the twenty-four routines (`apu_send` has no branch of its own:
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
  handshake can be verified but not substituted; *Two pickups, two items*,
  how a movie gets aimed at one line of one routine when the thing that line
  gets wrong is an index rather than a branch, so no coverage mark can name it;
  and *The same routine again, one array over*, where the ordering of seven
  button presses is what decides whether a movie takes eight of the new sites or
  four, and where the identical index question has no route through level 1 at
  all.
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
build\zamn_assets.exe password "Zombies Ate My Neighbors.sfc"
build\zamn_assets.exe password "Zombies Ate My Neighbors.sfc" xjqy
python tools\make_password_movie.py XJQY > movies\level33.zmv
build\zamn_assets.exe route "Zombies Ate My Neighbors.sfc" 2 350 585 251 302
python tools\fit_route.py "Zombies Ate My Neighbors.sfc" prefix.zmv 46 140 1158 230 1044 2900
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
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level1-keys.zmv -f 4050 -c
build\zamn_cosim.exe run "Zombies Ate My Neighbors.sfc" -m movies\level1-keys.zmv -f 4050
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level33.zmv -f 3600 -c
build\zamn_cosim.exe run "Zombies Ate My Neighbors.sfc" -m movies\level33.zmv -f 3600
build\zamn_cosim.exe verify "Zombies Ate My Neighbors.sfc" -m movies\level53.zmv -f 3700 -c
build\zamn_cosim.exe run "Zombies Ate My Neighbors.sfc" -m movies\level53.zmv -f 3700
build\zamn_headless.exe "Zombies Ate My Neighbors.sfc" shot.png 4050 -m movies\level1-keys.zmv --pos 1860,4050,20
build\zamn_disasm.exe "Zombies Ate My Neighbors.sfc" analysis2\zamn.cdl --from 80F8D6 --to 80F950 -s tools\symbols\zamn.sym --force
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

- Phase 3 (**a call injected into code that is not running**):
  `movies/level53.zmv` types the password **XDSJ**, walks into level 53's mummies
  with the fire button held, and is the first input in the project in which an
  enemy **survives** being shot. That reaches `$81:8506`, which is ported here:
  it reads the enemy thread's parked stack pointer out of `W_THREAD_SP`, moves it
  down three bytes, slides the top three words down to meet it, and writes
  `$81:8541` into the gap, so the scheduler's next resume of that thread runs
  `$81:8542` — two ticks of `ACTOR_ATTR_SET` on the enemy's display record —
  before returning to whatever the enemy was doing. **53,497 of 53,497** calls
  checked with nothing declined and **twenty splices byte-identical**, and
  `run` compares 3,689 of 3,689 passes at at most 31 bytes. Non-vacuous **seven
  ways out of eight**: a four-byte gap (`$7E:0F01`), the two overlapping stores
  in the other order (`$7E:0F08`: ROM `$85`, port `$00`), the return address not
  decremented for the `RTL` (`$7E:0F07`), the three-word move done highest-first
  (`$7E:0F02`), `W_THREAD_SP` never moved (`$7E:11D2`), the thread slot read from
  the wrong record field (`$7E:0001`), and carry claimed clear (flag C, plus
  `$7E:11A2`). Two of those seven are pure memory-ordering errors that no branch
  mark could ever express. **Not caught**: deleting the already-flashing guard,
  which `react_already` predicts by reading zero — one weapon's cooldown is
  longer than the two ticks a flash lasts. Coverage is **43 of 101** and it holds
  four sites alone: `enemy_survived` and `react_splice`, plus **`shot_expire_zero`
  and `victim_latched`**, which needed no code at all and had been untaken since
  long before this round. The union across the eight movies is **75 of 101**.
- Phase 3 (**a level nobody had seen**): `movies/level33.zmv` types the password
  **XJQY** and starts level 33, and it is the first movie in the project that
  reaches any level but the first. `verify` checks **48,032 of 48,032** calls
  with **nothing declined** and `run` compares **3,589 of 3,589** scheduler
  passes at at most 23 bytes differing at once — on a level with a different
  tileset, different actor bodies, different music and 29 actors against level
  1's 14. Nothing about the port is level-specific and this is the first
  evidence of it. The password is derived rather than looked up: `$82:B018`
  reads four letters through four tables in bank `$82`, `src/assets/password.c`
  reads the same tables, and `zamn_assets password` prints all 130 and checks
  each one reads back through the validator's own search. Confirmed twice in the
  emulator: WHRB leaves `$1E7C` = `$0005` under a card reading LEVEL 5, XJQY
  leaves `$0021` under LEVEL 33. Coverage is **28 of 99** and it holds
  **`draw_attr_set`** alone — one of the two sites this file has carried longest
  as "needing a record type level 1 does not spawn", taken for the first time.
  The union across the seven movies is **71 of 99**.
- Phase 3 (**the same routine again, one array over**):
  `movies/level1-keys.zmv` checks **73,024 of 73,024** calls with **nothing
  declined**, and it is what puts `$80:F8D6` — the *item* pickup, thirteen
  collision ids — and `$80:EAA8` — the item selector, five calls from three of
  its four call sites — under the diff. Both are their weapon-side twins with five constants changed, which
  is why the port is small and the *movie* is where the work went: the player
  starts level 1 holding one first-aid kit in slot 7 with slot 7 selected, so
  the array is never empty, and four of the eight new decisions are reachable
  only in one order. Press **A** (a full lap, `item_unchanged`); take a zombie's
  touch, which makes `$1CB8` 9; press **X**, which spends the kit and, because
  that leaves zero, tail-calls `$80:EAA8` with nothing in the array
  (`item_none_found`, `$FFFF` stored); press **A** again (`item_none_held`);
  take key 1 (`item_autoselect`); take key 2 (`item_taken`); press **A**. That
  order takes eight sites; any other takes four. Non-vacuous **ten ways out of
  twelve**: reading amounts from the weapon table fails at `$7E:1D0C` (ROM `$01`,
  port `$99`); four separate errors — base from `$64`, auto-select tested
  `== 0`, the selection read from `$1CBC`, the wrap at the weapons' `$001C` —
  all fail at `$7E:001E`, the **APU sequence counter**, because each makes the
  port play one sound fewer than the ROM; three more (twelve tries, starting at
  the slot already held, never writing the countdown back) fail at `$7E:012E`,
  the same scratch field that caught the weapon search a round earlier; the
  wrong sfx id is caught only by the APU **bus** diff and a wrong N only by the
  flag. **Not caught: three.** `index / 2 - ITEM_ID_FIRST` passes every call —
  both keys are id `$21`, slot 0 — and unlike last round there is no route that
  would tell it apart: the only other id into `$80:F8D6` is `$28`, object 8 at
  (1208,71), on the far side of a fence the player cannot cross. Dropping the
  `$0099` ceiling and breaking the decimal adjust pass too, and
  `item_pickup_capped` and `item_digit_carry` read zero on every movie to say
  so. Coverage is **43 of 99**, and it **holds nine sites alone** — more than
  the other five movies put together; the union across the six is **70 of 99**.
  `run` compares 4,039 of 4,039 passes, at most 27 bytes differing at once, all
  inside the stacks or a declared scratch byte.
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
twenty-three routines are through it — twenty-two leaves and one that suspends —
and how much of them any given input actually exercises is measured rather than
guessed at. What is left is scale rather than shape.

One of the twenty-three is verified but not substituted, and it is worth
carrying forward: **`$80:CCC8`'s body is a bus handshake**, and substitution's
whole mechanism is to stop the CPU executing, so `run` leaves it to the ROM and
says `verify only`. It is not a limit on Phase 4 — the finished port has its own
main loop and can spin on `$2143` as the ROM does — but it is the first time a
routine's *shape* rather than its coverage decided what the harness could claim
about it.

**Nothing declines.** The census is empty on all seven movies — including the
one that plays a level the port has never seen — which means the work list is
still made entirely of *inputs* rather than of decisions: every
item below needs a movie that does something no movie does yet. That will not
last — the count goes back up the moment an input reaches `$81:8506` or one of
the two special collision ids — and that is the instrument working rather than
failing.

`movies/level1-pickups.zmv` and `movies/level1-keys.zmv` are what an item on
this list looks like when it is done, and they are worth reading for the method
rather than the result. The first closed a gap that was not a branch but an
*index*, by a route derived from `$80:CA30` — an object's type byte says what
its collision id will be, so a level's placement data can be read as "which of
the port's branches can this level reach at all".

**The second added the harder half of that method: the order.** Its eight new
sites are not eight places to walk to, they are one sequence — press A, get hit,
press X, press A, take a key, take another key, press A — where each step is
what makes the next one reachable, and doing the same seven things in a
different order takes half of them. Reading a level for what it can reach is
necessary; working out what *state* the reachable thing has to be in is the part
that took the round.

**And it produced a new kind of work-list entry.** `$80:FA26` is unported, and
the only object in level 1 whose id reaches it sits inside a hedge. That is not
a movie nobody has written; it is a routine this level cannot reach. The same is
true of the one thing `$80:F8D6`'s port still gets no evidence for — its slot
arithmetic, whose distinguishing id is on the far side of a fence. Entries like
these do not come off the list by walking; they come off when an input reaches a
*different level*.

**And that is now possible.** `docs/password.md` is the format, `zamn_assets
password` spells all 130 of them, `tools/make_password_movie.py` types any one
of them, and `movies/level33.zmv` is the first movie to use it. Thirteen levels
are one command away — 5, 9, 13 … 53 — and level 33 alone places fourteen actors
running `$81:8C17`, whose init is `LDA #$0004 : STA $1E`. **The unreachable
items above are now route-finding rather than dead ends**, and the route is
through a maze in a level nobody has mapped, which is what makes it the next
job rather than a finished one.

1. **The rest of what a collision does.** Every collision on the current movies
   is served whole, including a pickup and the weapon selection under it.
   What remains is a short, named list rather than a subsystem —
   which is the whole point of having gone through the door with a guard rather
   than around it — and none of it is a WRAM problem except the first. In rough
   order of size:
   * ~~**`$81:8506`** — the enemy's *survivor* reaction.~~ **Done** —
     `movies/level53.zmv`. Twelve bytes of WRAM, twenty splices, seven of eight
     perturbations caught. What `docs/threads.md` framed as the hard question
     for the port's coroutines turned out to be the wrong question: the thread
     being spliced into is the ROM's, so what the port has to get right is
     arithmetic on `W_THREAD_SP` and six stores. **The open question is now the
     other direction** — splicing into a thread the *port* owns — and nothing in
     the game does that yet.
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
   * ~~**`$80:F8D6`, the item pickup, and `$80:EAA8` under it.**~~ **Done** —
     `movies/level1-keys.zmv`. Both are ported and diffed, and every branch of
     both is taken except the `$0099` cap and the decimal adjust. Its slot
     arithmetic is the one thing left, and it is the *unreachable* kind: see the
     preamble.
   * ~~**`$80:FA26`**~~ and its three twins `$80:FA4A`, `$80:FA79`, `$80:FAA4`,
     plus `$80:FACF`. **Written** — but ahead of any input, so all five are
     transcribed rather than diffed and the eight sites
     `player_spawn_0`..`heal_capped` say so. What they want is a route, and here
     is where they are: `$80:FACF` is 156 px from the start of **level 41**
     (KRPY) and 645 from level 29's; `$80:FAA4` is 204 px from **level 45**'s
     (BLHR); `$80:FA79` is 463 from level 37's (KZVJ) and level 53's (XDSJ).
     `zamn_assets route` now searches for the path instead of a person doing
     it: it reproduces `level1-pickups.zmv`'s route exactly and it says
     **no route** for level 41's, which is a fact twenty screenshots could not
     establish. What stops it finishing level 45's is its clearance model — a
     2x2 block of 8-pixel cells, where the player's real box is anchored
     somewhere else — so **the next piece of work is reading whatever
     `$80:AE43`'s callers test**, and after that this whole class of item is a
     command.
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
   The eight movies take **27**, **43**, **51**, **57**, **35**, **44**, **29**
   and **44** of the **103** sites that exist now, and they are complementary rather than ordered,
   so the number that matters is their **union: 76 of 111**.
   `level1-keys.zmv` holds **nine** sites alone — the whole of
   `item_select_next` plus `player_item_entry`, `item_taken` and
   `item_autoselect` — which is more than the other five put together and the
   strongest case yet for writing narrow movies aimed at named sites.
   `level1-2p-rescue.zmv` holds four (`collide_none`, `victim_claim_b`,
   `pickup_autoselect`, `weapon_none_held`); `level1-pickups.zmv` two
   (`pickup_taken`, `pickup_digit_carry`); `level1-rescue.zmv` exactly one
   (`victim_claim_a`, and only because the two-player movie deliberately has
   Zeke stand still); `level1.zmv` and `level1-2p.zmv` hold none and are kept as
   regression baselines rather than as coverage. `level53.zmv` holds four alone
   — `enemy_survived`, `react_splice`, `shot_expire_zero` and `victim_latched` —
   and two of those four needed no code and had been on this list for rounds,
   which is the clearest measure of what a second level was worth.
   **Thirty-five are untaken by
   every input that exists**, in rough order of how gettable they look:
   * `pickup_capped`, `item_pickup_capped`, `item_digit_carry` — the three
     arithmetic sites, and all three want quantities level 1 does not contain:
     an ammo counter at `$0999` (it pays `$0099` twice into one starting at
     `$0150`), an item counter at `$0099`, and an item counter whose units
     digit is 9 when the next one arrives (every item in level 1 is worth `$01`,
     and there are four of them). Long sessions or other levels, not better
     paths.
   * `object_spent` and `react_already` — entry guards against acting on
     something already dealt with, each the whole design of the routine it
     opens, and **deleting either passes every call on every movie**. There were
     three of these; `victim_latched` came off the list on `movies/level53.zmv`,
     which did it by accident rather than by aim. `react_already` wants a second
     hit landing inside the two ticks an enemy's hurt flash lasts, and one
     player's weapon cooldown is longer than that. The input that takes
     `victim_latched` is two players reaching one victim inside a single
     `actor_overlap_pass` *with the victim as the outer record* — the outer
     record's collision id is read once and cached, so both dispatches see the
     pre-claim state. `movies/level1-2p-rescue.zmv` tries for this and does not
     get it; whether the victim sorts outer is a property of the depth sort
     rather than of the input, which is what makes it fiddly rather than hard.
   * `victim_ignore`, `victim_event_2`, `victim_event_4`,
     `victim_keep_id`, `object_ignore` — the rest of those two chains.
   * `spawn_full` — all 24 scheduler slots live at once, so a spawn fails. The
     busiest movie in the project peaks well below that, and the site is here
     mostly to say that the failure path is transcribed: it is two pulls and an
     `LDA #$0000`, and it is the one branch of `thread_spawn` no input has run.
   * `hurt_state_immune`, `hurt_weapon_immune` — two branches inside the
     player's hit path. They want a specific player state and the one weapon
     `$80:F950` singles out.
   * `enemy_no_damage`, `enemy_survived`, `enemy_hit_special` — the enemy's other
     three outcomes. Every hit so far kills in one, so `enemy_survived` wants a
     tougher enemy. `enemy_survived` is **done** — `movies/level53.zmv`, whose
     eleven actors all run `$81:8C17` — and it was the password screen that made
     it reachable, exactly as this entry predicted. `enemy_no_damage` and
     `enemy_hit_special` are what is left, and both now decline **by address**
     (`$81:847E`, `$81:83C6`) if anything ever reaches them.
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
   * `sort_key_first` (`ACTOR_SORT_FIRST`) — the last of the four originally
     hand-found gaps, and now the longest-standing untaken site in the project.
     Its twin `draw_attr_set` was here for as long and came off the list the
     first time an input reached another level, which is the best evidence
     there is that "needs a record type level 1 does not spawn" was the right
     diagnosis and a password was the cure.
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

   **The password screen has moved up.** It used to be one entry on that list;
   it is now the thing that unblocks the others. Three separate items above are
   now marked *not reachable in level 1* rather than *not written yet* —
   `$80:FA26`, `$80:F8D6`'s slot arithmetic, and the two capped counters — and
   every level-1 enemy the actor list places runs `$81:87F8`, whose init is
   `LDA #$0000 : STA $1E`, so **no enemy in level 1 can survive a hit** and
   `enemy_survived` (and with it `$81:8506`) is unreachable here by
   construction. Bodies with health in them exist — `$81:8C17`'s is 4,
   `$81:C1FB`'s 2, `$81:FA18`'s 3 — and every one of them is in a level no input
   has ever seen. Passwords are on PLAN.md's port order anyway; what is new is
   that they are also the cheapest route to five items on this list.

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
  111 marked branches an input reached, and **thirty-five** are untaken by every
  movie that exists — eight of those thirty-five arrived in one round, because
  five jump-table entries were ported ahead of any input that reaches them. When a decline does come back, it says **where the ROM went
  instead**, by address, so what is missing is a work list rather than a count.
  See
  `docs/cosim.md` → *Half a routine, honestly*, *Through the door*, *The last
  decline*, *Declined to what: the census*, *And the last address on the list:
  the victim*, *The APU: an output the diff could not see*, *The uploader's
  23,820 calls*, *Coverage the movie does not have*, *The second player
  rescues somebody* and *Two pickups, two items*.
- **An unreachable input is not the same as an unwritten one.** Three open items
  are *not* waiting on somebody to write a level-1 movie: `$80:F8D6`'s slot
  arithmetic wants collision id `$28`, which is on the far side of a fence;
  `$80:FA26` wants id `$2D`, which is inside a hedge; and `enemy_survived` wants
  an enemy with health, which level 1's actor list does not place. Saying so is
  the difference between a work list and a wish list — and the diagnosis was
  right, because a password moved the whole class: `draw_attr_set` had sat
  untaken since the sprite pass was ported and came off the list on the first
  input that reached another level. The three above are now route-finding in
  levels nobody has mapped rather than dead ends. See `docs/password.md`.
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
- **A marked branch is one somebody thought to mark.** The 111 sites are chosen by
  hand, one per decision the diff would have to run to check, not generated. A
  branch with no mark on it is invisible to the report, so this is a floor on
  coverage rather than a measurement of it.
- **A branch mark cannot catch a port that is too *permissive*.** Coverage says
  a line ran; nothing says a line should never have been reachable. Adding a
  fifth id to `shot_collide`'s four passes every diff, because no input produces
  it — the port would accept something the ROM rejects and the movie cannot tell.
  Same floor as the `STZ $7E` above, approached from the other side. The
  standing examples are entry guards: **deleting `object_collide`'s
  spent-object guard, or `enemy_survived_react`'s already-flashing one, passes
  every call on every movie**, and in both routines that guard is the whole
  design. `victim_collide`'s latch guard was the third and is not any more —
  `movies/level53.zmv` dispatched twice to one victim, which nothing had done in
  eight rounds of trying, and it did it without being aimed at that. Coverage
  sites (`object_spent`, `react_already`) name the two that are left; nothing
  can check either but an input.
- **Two activations of the same ported routine at once are not distinguished.**
  A suspension is attributed to the innermost in-flight call whose body contains
  the yield's return address; if two threads were ever inside the same ported
  routine simultaneously that would be ambiguous. No routine ported so far can
  be. See `docs/threads.md` → *What is not settled yet*.
- 11 passes go uncompared by `run` on each movie: one before the
  scheduler exists, and ten where a side never returned to the `WAI` within the
  step.
- **The census is capped at 64 distinct addresses.** None are in use: no movie
  declines anything, including the one that plays level 33. If it ever
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
