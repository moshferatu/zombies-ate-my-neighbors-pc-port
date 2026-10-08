# Progress Tracker

Cross-session status for the ZAMN native-port project. Update this whenever a
milestone lands. See `PLAN.md` for the full multi-phase plan.

## Current status: **Phase 3 underway** 🔨 (2026-09-20)

### The picture, drawn natively (2026-10-08)

The plan kept the console's video chip emulated for good. That has changed:
the picture and the sound are to be ported too, until the game does not
depend on LakeSnes. This is the start of the picture. `docs/video.md` is
the long form.

`src/video` finds every line's sprites and draws every scanline the game
shows, and the emulated PPU does neither. It keeps the chip's registers as
well, and the game reads those. At 16:9 a tick of `level1.zmv` costs
**2.8 ms** to emulate and take apart for the smoothing, from 8.3.

* **Why this first.** A profile at 240 Hz and 16:9 had 43% of the busy
  samples in the PPU drawing the frame a dot at a time, and 21% in the
  smoothing taking the same frame apart a dot at a time. The 65816, the
  ports and the sound together were 18%.
* **`src/video/video.c`** draws a line the way the game describes it.
  * Each background's line a tile at a time, eight pixels of a tile's row
    at once, into a row for its tiles that go behind and a row for those
    that go in front.
  * The rows painted over each other from the back, with the sprites' row
    where mode 1 puts it.
  * Each column's colour looked up in a table that is made again only when
    the palette, the brightness or the fixed colour changes.
* **The widened picture is runs of tiles too.** A policy says which column
  of a layer each column of the picture shows, and every policy is a few
  stretches of consecutive columns. The seven the game uses are drawn.
* **It draws what the game uses and declines the rest.** A survey of every
  line of the corpus, 77,686,784 of them at one width, found 15 kinds, all
  mode 1. `video_declines` names what a line has that is outside that, and
  such a line is left to the PPU. Over the corpus none is.
* **`src/video/ppu_hook.c`** is the one file that knows the emulator. The
  PPU still holds the registers and the memory and still finds each line's
  sprites. It calls `Ppu.drawLine` where it would have drawn.
* **`--renderer native`, `emulated` or `check`.** Native is the default.
  `--stock` runs emulated unless told otherwise. `check` draws every line
  both ways, reports the lines that differ and exits 1 if any did.
* **The smoothing's planes by rows.** `video_bg_row` is the same line
  reader over the columns and lines `src/layers.h` wants, and
  `layers_planes_by_row` fills a plane from it. A layer with a window on it
  is still read a dot at a time.

* **Checked**, on the last build.
  * `tools/verify_corpus.ps1 -Picture`, which is new: 54 movies at four
    widths, 310,747,136 lines drawn both ways, and none differ. Each of the
    216 runs comes to the same checksum with `src/video` drawing alone.
  * `zamn_test_video`, which is new and needs no ROM: PPUs filled with
    noise. 2,024,736 lines drawn both ways over eight seeds and none
    differ. 394,296 rows read back as the smoothing reads them and none
    differ.
  * 324 screenshots and 4,832 of the smoothing's pictures are byte for
    byte what the build before drew.
  * The corpus verifies as it did: 34,603,665 calls, 0 diverged, 1,273 of
    1,505 sites. Lockstep: 334,319 passes, 51 of 54 never part, the same
    three.
* **Cost**, milliseconds a tick, emulating and then taking apart.
  * 4:3: 4.4 and 2.3 before, 2.2 and 0.4 after.
  * 16:9: 5.5 and 2.8 before, 2.4 and 0.4 after.
  * 21:9: 6.6 and 3.3 before, 2.5 and 0.5 after.
  * The whole process in a window at 240 Hz and 16:9, no sound: 50% to 75%
    of one core before and 17% to 37% after, three runs of each. Those
    runs are noisy, and with no sound they leave out the wait below.
* **The wait between pictures**, which is not the picture and was most of
  what the game cost as it is played. I had measured with the sound off
  and said the pacing loop was fine. It was not, and the user saw no
  difference from the renderer until it was mended.
  * The loop slept in whole milliseconds and spun the last two before each
    deadline. When the display's own wait holds the loop, there is nothing
    left to spin. When the pacer is the clock, which it was in every run
    with the sound device open, that is two milliseconds of every 4.17 at
    240 pictures a second: nearly half a core.
  * `pace_sleep` in `src/main_sdl.c` sleeps by a high-resolution timer,
    and the loop spins the last half millisecond. Tried at 2, 1, 0.5, 0.2
    and 0: half a millisecond had the most pictures within a millisecond
    of the period, 99.8%, and none had 96.9%.
  * With the sound on. A level at 16:9 in a window: 75% and 93% of one
    core before either change, 27% and 30% now. The title in fullscreen
    at 3840x2160 with the pads read: 101% before, 21% now.
* **A line's sprites, found here.** `video_sprites` in `src/video/video.c`
  reads OAM as the console does: the sprites that cross the line, the first
  32 kept, then their tiles from the last kept to the first until 34 are
  fetched.
  * The two limits and the two flags the game reads from `$213E` are kept,
    and so are the frontend's own: sprites found before the rest, sprites
    in the frontend's colours, and a sprite's three places in a widened
    picture.
  * `Ppu.findSprites` is where the PPU asks for them. `--renderer check`
    finds them both ways and compares the rows and the flags.
  * The corpus at four widths: no line's sprites differ, and the picture
    still does not, over 310,747,136 lines. Noise: 1,260,224 lines' sprites
    and none differ.
  * It is no faster. A tick at 16:9 emulates in 2.4 to 2.5 ms before and
    after. It was done to need the emulator less.
  * A mistake of mine. The noise test did not catch one sprite too many
    kept, because sprites at random never crowd a line. One frame in four
    now has them in a band of lines, and it catches that, a tile too many
    and a flag never set.
  * Not established: whether any movie of the corpus crowds a line. The
    tools other than the game still use the PPU's finder.
* **The chip's registers, kept here.** `src/video/registers.c` is the
  sixty-four addresses from `$2100`: the writes decoded, the twelve reads
  answered, and the three things that happen to the chip once a frame.
  * A line is drawn from these registers, and the game reads what they
    answer. The PPU still has every write done to it too, because the
    frontend reads its copy. The memories are the PPU's arrays, shared.
  * `Ppu.wrote`, `Ppu.didRead` and `Ppu.happened` are where the PPU says
    what it has just done. `--renderer check` compares every register by
    name after each.
  * The corpus at four widths: no write, read or event left a register
    other than the PPU's, and the picture is unchanged.
  * The corpus caught one thing first. At the three widened widths, 12, 9
    and 8 movies drew a different picture with `src/video` alone: the
    levels with a big figure. `src/widescreen.h` moves that figure's plane
    by writing a scroll into the PPU's struct, which the registers here
    never saw. It calls `ppu_setScroll` now, and the PPU says so.
  * `zamn_test_registers`, which is new and needs no ROM: 13,000,000 steps
    at random over three seeds, none differing. Nine rules broken on
    purpose were each caught and named.
  * The game hardly reads the chip: `level1.zmv` makes 4,133,561 writes and
    one read. So the reads are checked by the noise and not by the corpus.
  * A mistake of mine in the last commit. `Ppu.findSprites` was never set
    to NULL in `ppu_init`, which does not clear what it allocates. The
    tools that do not install `src/video` ran on whatever was there, which
    happened to be zero. It is set now.
  * Not established: the cosim and lockstep passes were not rerun. A saved
    state loaded in the game was not tried; the registers are taken from
    the PPU when one is.
* **Tried before this and not kept.** Three smaller things, each measured
  in a copy: asking each layer once a dot in `ppu_getPixel`, 13% and
  pixel-identical; remembering the last tile read, 4% more; whole-program
  optimisation, nothing. The first two are moot now.
* **One mistake of mine.** To see that the noise test could fail I broke a
  rule on purpose, and the first rule I broke was one noise cannot reach:
  whether a sprite pixel of exactly `$C0` takes colour maths, which no
  sprite pixel is. The test passed. The second, a 16x16 tile's lower half
  read one character out, failed on 22,756 lines of 51,520.
* **Not established.**
  * I have not looked at it. Every check compares bytes.
  * The cost of a level in fullscreen. The title is measured there and a
    level in a window.
  * Why the display's wait holds the loop with the sound off and the pacer
    does with it on.
  * Whether half a millisecond is the right tail on another machine. It
    was measured on one.
  * That it builds on Linux. It has only been built here.
  * A screen the corpus does not reach may draw a kind of line that is
    declined. The PPU then draws it, correctly and slowly, and
    `--verbose` counts such lines by what they were.
* **Still the emulator's.** VRAM, the palette and OAM. A second copy of
  the registers, which the frontend reads. What the automatic policy is
  worked out from. The buffer the picture is written into, and the
  sprites' rows. When a frame starts and ends.
* **`PLAN.md` still says the PPU stays emulated.** I have not edited it.
* **Next.** The frontend reading `src/video`'s registers and not the
  PPU's, after which the PPU's copy has no reader. A frame drawn once:
  with the smoothing on, a frame shown as layers is still drawn as a
  picture nobody sees. The smoothing's sprites and its maths window, which
  are still a dot at a time.

### The wobble's call, the screens' sends, and the calls of a player's frame (2026-10-08)

What the 65816 still executed over the twelve movies goes from 132,840 to
**107,993** instructions of work. The game registers 566 routines, 39
more. The corpus is the same 54 movies.

* **The wobble's one `JSR`**, `$80:9512`, which I had been calling the
  ROM's on purpose. It ran 10,957 times. It is a stretch of its own now,
  `wave_thread_call` in `port/trig.c`, and ends where the build begins.
  * The reason given for leaving it was that a stretch beginning before
    the build would read the pads too early. That is true of one stretch
    from before the build to after the tests. It is not true of three:
    the call, the build taking its own time, and the tests after it.
  * So one stretch stays the ROM's on purpose, not two: the third of the
    between-games clear, `$80:897F`, 25,624.
* **The screen before the title**, in `port/frontend.c`. The front end
  shows it once. Its thread waits on `WAI` fifteen times and asks after
  the pads each time.
  * `opening_job` (`$80:938E`) moves the third layer every fourth frame.
  * `opening_wait` (`$80:931F`) is the call and the pads. Start alone on
    either ends the screen. A shoulder button alone plays a sound, and
    that is left to the ROM.
  * `opening_count` (`$80:9322`) counts the fifteen.
* **The title's two sprites**, `title_sprites_begin` (`$80:943D`), as the
  portraits' screen takes its two. And `frontend_reset` (`$80:9261`), what
  every pass of the front end zeroes.
* **What the screens' sends are called with**, in `port/dma.c`. A screen
  sets itself up with a run of calls to the DMA routines, and between two
  there is only the next one's numbers: a word pulled, a word pushed, and
  A, X and Y loaded. Twenty-five such stretches over four screens are one
  function and a table, `SEND_ARGS_BY_ADDRESS`, named for where they are.
* **Three more of the DMA's own**, each recording its writes:
  * `dma_to_cgram_at` (`$80:C892`), colours from a colour the caller names;
  * `hud_tiles_job` (`$80:C2AB`), the third layer's tiles;
  * `hud_layer_set` (`$80:C2E8`), where that layer's map and tiles are.
* **The big letters' multiply**, `text_big_multiply` (`$82:ADDB`): the two
  factors to the console's multiplier and three `NOP`s. It ran 1,775
  instructions between two stretches that were ported.
* **The calls of a player's frame**, in `port/bodies.c`. On a frame
  `player_frame` turns down, the ROM runs the frame a piece at a time, and
  ran the instructions between the pieces itself.
  * `player_branch` (`$80:D1EA`) now makes the jump through the table of
    states as well as the `LDX` in front of it.
  * `player_hurt_call` (`$80:CE01`) and `player_dead_call` (`$80:CE20`)
    are each a `JSR` with what it calls.
  * `player_out_show` and `player_out_again` (`$80:CF0B`, `$80:CF14`) are
    the loop a player ends in with none left of what `$7E:1D4C` counts.
* **A `JML` is an exit the harness can make**, the eighth instruction of
  its kind. Four threads end on `JML $80:BE41` and the ROM ran each.

* **Checked.** The corpus verifies at 34,603,665 calls across 54 movies
  with 0 diverged, and 1,273 of 1,505 coverage sites. All 16 new sites are
  taken. Every call priced is exact. It turns down 3,198 calls, as before.
* **Hardware.** The four that write registers are compared write by
  write: address, value and cycle. None was unmodelled.
* **Lockstep** over the corpus: 334,319 passes, 51 of 54 never part, the
  same three level-25 movies. Mean drift is smaller on 22 movies, the same
  on 20, and larger on twelve: `boot` by 7.8 cycles, `level1` by 4.8, the
  rest by 1.3 or less. Five worst figures moved: `level1-map` up 36,
  `boot` up 14, `password-bcdf` down 12, two more down 6. The total over
  the corpus is -1,992,290 against -1,992,262.
* **What it took off.** Bank `$80` goes from 85,587 instructions to
  63,252, bank `$82` from 25,190 to 23,415 and bank `$81` from 15,130 to
  14,393. Bank `$83` is as it was. Seven movies are down by 2,700 to
  3,700 each and the other five by 700 to 900.
* **One mistake of mine.** The new functions in `port/frontend.c` went in
  above the helpers they call, and it did not build.
* **Not established.**
  * What is on the screen before the title, or what the title's two
    sprites are. I have not looked.
  * What `$7E:1E84` and `$7E:1E86` are, which `frontend_reset` sets.
  * That `$7E:1D4C` counts lives. That is by what the code does with it.
  * Why `boot`'s mean drift is 7.8 cycles larger.
* **Still the ROM's.**
  * `$80:CDFE`, the first `JSR` of a frame `player_frame` turns down, 593.
    A second routine cannot begin where another does.
  * Of the screens: the stretches that write the picture registers
    between the sends, and the lone `RTS` after the last send.
  * The decimal count of neighbours at `$80:C863`.
* **Next.** Bank `$80` is 63,252 of the 107,993, and 25,624 of that is
  the clear that stays the ROM's. After it: a level's start, `$80:85BB`
  on, 3,743; the fractions at `$80:F6B4`, 1,868; level 21's three at
  `$82:DB96`, `$82:F354` and `$82:F44A`, 3,286; the animated tiles' start
  at `$82:D7CF`, 1,342.

### The fishman's leap and dive, the weeds' seed, and twenty-one more thread ends (2026-10-07)

What the 65816 still executed over the twelve movies goes from 148,960 to
**132,840** instructions of work. Live, each share reads as it did or
higher: the calls by a tenth of a percent on `level9`, `level13` and
`level29-fighting`, and by two tenths on `level41`. The game registers 527
routines, 35 more. The corpus is the same 54 movies.

* **The fishman from the water to the land and back**, in
  `port/fishman.c`. The port had its swimming, the flight of a leap, the
  landing and its walk. A pass that decided to leap was the ROM's, and so
  was everything a leap leads to. Over `level13` and `level41` the ROM ran
  10,296 instructions of the fishman's own. It runs 496: one pass on each
  that stops to look about.
  * **Four bodies sleep in the middle**, or show a list of pictures, which
    sleeps too. A pass that gets to one now stops there, with the body's
    returns on the thread's stack as the ROM has them. What is left of the
    pass is a stretch that begins where the thread wakes.
  * **The leap.** Having drawn a spot, it waits one to eight ticks.
    `fishman_leap_wake` (`$81:E076`) sets the leap up and finishes the
    pass. A spot under 32 pixels across is no leap after all. Otherwise
    its line is half the longer gap, and it leaves a splash where it left
    the water, which is a thread of its own.
  * **The sweep.** Landed, it turns to whoever it is after and shows three
    pictures. `fishman_sweep_begin` (`$81:E346`) takes its second record.
    `fishman_sweep_tick` had the five ticks, and now frees that record at
    the last. `fishman_sweep_after` (`$81:E397`) sets it walking.
  * **The dive back.** `port/wander.h` finds water, and it waits again.
    `fishman_dive_wake` (`$81:DB10`) sets the dive up and goes on with the
    pass on land it was in the middle of. The pass after is the dive's
    first, and the last is the splash, which stops at two pictures.
    `fishman_splash_after` (`$81:DBE1`) has it swimming again.
  * **It walks until it has swept once.** The search for water runs only
    when two words on its page differ, and they begin the same. The end of
    a sweep doubles one. Finding water makes them the same again. I had not
    seen that before this round.
  * **On land with somebody near or nobody near**, which were the ROM's:
    within 28 and eight or more across it sweeps again, and with nobody
    within 175 it leaves or goes about as the other kind does.
  * **The splash a leap leaves**, `$81:E72C`: `fishman_splash_begin`, and
    its end in `port/begin.c`.
* **The weeds' seed**, `$81:D4C9`, in `port/weeds.c`. Its flight was
  ported. Now its start is, and its landing, in four stretches:
  `weed_seed_begin`, `weed_seed_landed`, `weed_seed_told` and
  `weed_seed_end`. It comes down near whoever the weed was on its guard
  against, up to 32 pixels either way by two draws.
* **A slime's glob**, four stretches of a few instructions each, in
  `port/slime.c`: its start as far as its sound (`$81:CF10`), its landing
  as far as its second (`$81:CF36`), the list of its last pictures
  (`$81:CE6A`), and its end from inside the landing's `JSR` (`$81:CE71`).
  They ran 2,927 instructions between them.
* **Twenty-one more thread ends** in `record_end`'s table. A scan of the
  ROM for its seven instructions finds the ends the twelve movies still
  ran, 249 times. One is the splash's. Three are where a thread's loop
  goes on a fate that is not nothing, so they have their thread's name:
  the werewolf's, the slime's and the weed's. Seventeen are named for
  their address, because the port has not read the threads they end.
  * One is left: `$83:9F68`, which frees two records.
* **The table of routines was full.** It held 512 and the round reached
  519. It holds 768.

* **Checked.** The corpus verifies at 34,253,648 calls across 54 movies
  with 0 diverged, and 1,257 of 1,489 coverage sites. Every call priced is
  exact. It turns down 3,198 calls, where it was 3,331.
* **Coverage.** Of the 25 new sites the movies take 20. Four more are
  taken under `--poke`, on `level13`, each with 0 diverged:
  * a fishman on land put 12 pixels beside the player: it sweeps again;
  * put 4 across and 16 below: it steps all the same;
  * put far off: it leaves;
  * the water it found put straight below it: no dive.
  One is taken by nothing: the end of a sweep for the fishman that comes
  ashore, which is the other's with one instruction left out.
* **Lockstep** over the corpus: 334,319 passes, 51 of 54 never part, the
  same three level-25 movies. Mean drift is smaller on 36 movies, the same
  on 13, and larger on five by half a cycle or less. Three worst figures
  moved: `level1-pickups` down 6, `level1-keys` up 12, and
  `level9-weapons` up 770.
  * `level9-weapons`' is pass 6,614, where a weed's thread ends. Leaving
    `weed_end` to the ROM puts it back. The total over the movie is the
    same to the cycle, -58,740. It is last round's `level5` again, at
    another thread's end, and I have still not shown the cause.
* **What it took off.** Bank `$81` goes from 30,163 instructions to
  15,130. Bank `$82` goes from 25,689 to 25,190 and bank `$83` from 7,521
  to 6,933, both by the thread ends. Bank `$80` is as it was. `level41` is
  down by 7,013, `level13` by 6,046 and `level9` by 1,466.
* **Four mistakes of mine.**
  * The two fishman rows have a list of exits each, and I gave the new
    stops to one. On the other the ROM ran past the sleep into another
    thread: 1,131 calls of 1,754 failed, the first on the word that says
    which thread is running.
  * The seed's end failed 9 calls of 9. The free keeps the thread's page
    on the stack as it works, and here that is above where the stretch's
    stack began, over the return of the `JSR` the landing made. The port
    writes it. The glob's end has the same.
  * With 519 routines in a table of 512 the harness says so and checks
    nothing. I saw it as twelve empty reports.
  * A patch with a backslash in it went through a shell heredoc again,
    this time a print I was adding to find a frame, and did not build.
* **Not established.**
  * What the seventeen threads are whose ends are named for an address.
  * Why a thread's end can move one pass's figure in lockstep and leave
    the total alone.
  * What the weeds' seed keeps at `$1C`, which is the way to where it will
    come down. Nothing in its thread reads it.
  * Whether the fishman's second record is a swipe at whoever is beside
    it. That is still by what the code does.
* **Still the ROM's.**
  * Of the fishman: a pass that stops to look about, one on land that is
    stopped by the ground and finds water past it, the bite, what the
    kind that comes ashore does there, its setup, and an end that is not
    the plain one.
  * A call to play a sound that an entry stops at. A sound is not priced
    inside an entry.
* **Next.** Bank `$80` is 85,587 of the 132,840, and 36,581 of that was
  being left to the ROM on purpose: the third stretch of the
  between-games clear, `$80:897F`, 25,624, and the one `JSR` of the
  wobble's thread, `$80:9512`, 10,957. I had been giving 41,349, which
  counted 4,768 of ordinary residue next to that `JSR` with it. The round
  after this ports the `JSR`. After them: a level's
  start, `$80:85BB` on, 3,743; the fractions at `$80:F6B4`, 1,868; five
  instructions round a multiply at `$82:ADDB`, 1,775; and level 21's
  three at `$82:DB96`, `$82:F354` and `$82:F44A`, 3,286.

### The chainsaw maniac's swing, a wall knocked down, and threads that end in the port (2026-10-07)

What the 65816 still executed over the twelve movies goes from 162,997 to
**148,960** instructions of work. Live, each share reads as it did or a
tenth of a percent higher: the calls on `level1`, `level9` and `level41`.
The game registers 492 routines, 9 more. The corpus is the same 54 movies.

* **A thread's last jump.** A thing's thread ends by jumping to
  `actor_slot_free`, and the free's `RTL` is the thread's own. The entries
  for those ends stopped at the jump, which left the jump to the ROM: one
  instruction, 3,067 times over the twelve movies. They go through the free
  now and stop at `thread_exit`, where that `RTL` goes. 1,001 are left.
  * The squirt gun's shot was 1,570 of them (`squirt_gone`). Its
    `squirt_dress` begins one instruction earlier too, at the call that
    takes its record: another 1,570.
  * `record_end` in `port/begin.c` is the end of seven kinds of thing: three
    zombies, a slime's glob, weapon 5's shot, and a martian by either way
    in.
  * `actor_list_place` in `port/oam.c` is how far the free walks to unlink
    a record, which is what it costs. Four older files have a copy each.
    What was written this round uses the one.
* **The chainsaw maniac's swing**, in `port/chainsaw.c`. It is a full turn
  with the saw held out: eight pictures, three ticks each, and for the saw
  a record of its own with no picture, fifteen to nineteen pixels out. It
  sleeps between pictures, so it is two stretches: `chainsaw_frame` in the
  swing's state, as far as the first sleep, and `chainsaw_swing_next`
  (`$81:9598`) where it wakes.
  * Its frame was also turned down after every swing, for up to six
    passes. A swing borrows the word that counts its walking pictures and
    leaves it out of range. The guard asked for it in range. Nothing reads
    it before it is masked, so the guard no longer asks.
  * Over `level13` and `level41` the ROM ran 36 of its passes. It runs 8
    now, each a pass that cuts a hedge.
* **A block's put begun**, `$80:AB5A`, in `port/tile_rows.c`:
  `tile_block_begin`, as far as the two instructions that hold the camera.
  Those stay the ROM's, so the camera is held when the ROM holds it.
* **A block swapped for its pair**, `tile_block_swap_ask` in the same file.
  A wall that can come down is two blocks numbered next to each other, and
  three callers read the one at a place and ask for the other with the same
  instructions. Two of them use it:
  * **The thread a punch begins**, `$81:F2B2`, in `port/knock.c`: four
    stretches, from its record and sound to its end. `port/knock.h` had the
    punch that asks for it.
  * **Weapon 5's shot breaking a tile**, `$81:ECAC`, in `port/shot5.c`:
    `shot5_break`, as far as the call that puts the block.
* **Two sound commands with their argument built in**, `$80:CC13` and
  `$80:CC27`, in `port/apu.c`.

* **Checked.** The corpus verifies at 34,252,116 calls across 54 movies
  with 0 diverged, and 1,237 of 1,464 coverage sites. Every call priced is
  exact. It turns down 3,331 calls, where it was 3,490.
* **Coverage.** All 13 new sites are taken. `chainsaw_died` is taken for
  the first time: the passes a maniac died on were among those turned down.
* **Lockstep** over the corpus: 334,319 passes, 51 of 54 never part, the
  same three level-25 movies. Mean drift is smaller on 52 movies and three
  tenths of a cycle larger on two. Twenty-six worst figures moved. Nine
  went down, five of them by 282 to 922 cycles. Seventeen went up: twelve
  by 46 or less, `level49-bubble` by 76, `demo-end` by 138, `level49` by
  204, `boot` by 254 and `level5` by 892.
  * `level5`'s is one pass, 13,391, where a slime's glob ends. Leaving
    `slime_glob_end` to the ROM puts it back. The total over the movie is
    the same to the cycle. I take it to be where in that pass the clocks
    are compared, now that the end is one stretch where it was two. I have
    not shown that.
* **What it took off.** Bank `$81` goes from 41,248 instructions to 30,163
  and bank `$80` from 88,539 to 85,587. Banks `$82` and `$83` are as they
  were. `level13` is down by 3,470, `level41` by 3,392, `level37` by 2,099
  and `level5` by 2,081.
* **Four mistakes of mine.**
  * I first stopped the shot's end at the free's own `RTL`. That address is
    where another entry returns, and the harness ended that one and never
    saw mine: 150 calls of 150 "not reached". It stops at `thread_exit`
    now, which is checked.
  * I took all seven ends in `record_end` to jump to the free. The martian
    that began on the ground calls it and has an `RTL` of its own after.
    The harness said MODEL WRONG, 72 cycles short on 13 calls of 13. Priced.
  * The swing's end failed 25 calls of 32. The pass's `JSR` to its picture
    writes its return where the swing's had been, above where the stack
    began, and the port had not written it.
  * A patch written in a shell heredoc lost its backslashes again, and a
    script of mine stopped at a wrong anchor with four files already
    changed. I cut what had been applied and ran the rest.
* **Not established.**
  * What command 2 is to the sound driver, and what `$32` is to either
    command.
  * What reads `$7E:1FC2`, which the swap counts up.
  * Why the thread a punch begins keeps its page's `$08` in `$24`. It reads
    it nowhere.
* **Still the ROM's.**
  * Of the chainsaw maniac: the cutting, its setup, and being hit.
  * The two instructions that hold the camera in a block's put, and the
    five that let it go.
  * The last jump of the axe, the doll, the bubble and the swipe: 723. Each
    is inside a longer stretch with its own bill.
  * A call to play a sound that an entry stops at: 1,172. A sound is not
    priced inside an entry.
* **Next.** Bank `$80` is 85,587 of the 148,960, and 41,349 of that is the
  two stretches that stay the ROM's on purpose. After them: a level's
  start, `$80:85BB` on, 3,743; the fractions at `$80:F6B4`, 1,868; level
  21's two at `$82:DB96` and `$82:F354`, 2,521; and the fishman's patrol,
  whose frame is turned down 42 times on `level13` (`$81:E007`, `$81:E07C`,
  `$81:E550` and `$81:E610`, about 3,800).

### Level 37's circle, five starts, the layers' registers and a level's second loop (2026-10-07)

What the 65816 still executed over the twelve movies goes from 183,910 to
**162,997** instructions of work. Live, each share reads as it did or a
tenth of a percent higher: the work on `level1`, the calls on `level13`,
`level21` and `level37`. The game registers 483 routines, 14 more. The
corpus is the same 54 movies.

Nothing here is one large piece. The residue is a long tail now, and this
round took fourteen pieces off it and widened one that was there.

* **Level 37's thing leaving its circle**, in `port/seeker.c`. Its frame
  turned down every circling frame on which the draw said stop. Those
  frames go on to two questions, and both were already the port's: is where
  it is on the level (`$80:B422`), and will the ground there do
  (`$80:AF66`)? A no to either and it circles on. Two yeses and it stops
  (`$82:EA0C`): another picture, a frame's sleep, and a next state at
  `$82:EA35` that is still the ROM's.
  * The frame's guard also read two fields that only circling uses, in
    every state. It reads them only when circling now.
  * On `level37`'s 8,000 frames the frame was turned down 159 times, and is
    61 times now.
* **The start of the thing it launches**, `$82:F13C`, in `port/tracker.c`:
  a record twelve pixels up at the place asked for, a speed across and down
  from a table by the way it was sent, and 130 frames to live.
* **A slime's start**, `$81:CC4F`, in `port/slime.c`: a record where the
  thread was started, facing down, four hits to take. It ends at the call
  that plays its first pictures, which sleeps.
* **The rest of the figure that rises**, in `port/riser.c`. Only its last
  loop was the port's. `riser_begin` (`$81:8294`) puts its record in front
  of every layer where nothing can touch it. `riser_shown` (`$81:82CB`) is
  each of its first five pictures, and after the fifth the first pixel up.
* **The rest of the neighbour who jumps**, in `port/jumper.c`:
  `jumper_stand` (`$83:9EDB`), on the ground for sixty frames, and
  `jumper_rested` (`$83:9EED`), where the sixty end and it leaves the
  ground. With these its whole round is the port's.
* **A level's second loop**, `$80:8585`, in `port/mainloop.c`. With no
  neighbours left to save the level's thread goes round a second loop until
  everyone playing has left. A pass of it is `mainloop_leaving_frame`,
  priced as the first loop's is.
* **The picture's layers set up**, new in `port/layers_setup.c`.
  `layers_setup` (`$80:AC7E`) writes the mode and where three layers have
  their maps and tiles. `layers_reset` (`$80:88A9`) puts every layer at its
  corner first. Both record their writes and the harness makes them on the
  ROM's cycles.
* **The job that sends tiles put into the map**, `$80:AC55`, in
  `port/tile_put.c`: each word on the list to its address in VRAM, last
  first.
* **A side of the HUD blanked**, `$80:C1D4` and `$80:C1F8`, in
  `port/hud.c`: fifteen words of each of four rows.
  * `port/hud.h` said `$80:C1CF` clears the whole shadow. It clears one
    player's side. The header says so now.
* **Two screens' sprites**, in `port/frontend.c`: each of the game over's
  four (`$80:8AD3`), and the two on the portraits' screen (`$80:9847`).

* **Checked.** The corpus verifies at 34,251,025 calls across 54 movies
  with 0 diverged, and 1,223 of 1,451 coverage sites. Every call priced is
  exact. It turns down 3,490 calls, where it was 3,577.
* **Lockstep** over the corpus: 334,319 passes, 51 of 54 never part, the
  same three level-25 movies. Mean drift is a little smaller on every
  movie. Four worst figures moved: `level1-map` by 36 cycles, `level49`
  and `password-bcdf` by 12, all up, and `level1` down by 6.
* **What it took off.** Bank `$80` goes from 98,178 instructions to 88,539,
  bank `$81` from 44,231 to 41,248, bank `$82` from 31,368 to 25,689 and
  bank `$83` from 10,133 to 7,521. `level37` is down by 7,818 and `level1`
  by 2,149, which is over two fifths of what it had.
* **Three mistakes of mine.**
  * A patch written in a shell heredoc lost the backslashes that end the
    rows of `coverage.h`, as the notes warn it will. I mended the row by
    hand and wrote the rest as script files.
  * `game_over_sprite_begin` was turned down every time, and then priced
    eight cycles short. That screen runs with a data bank of `$00`. I had
    asked for `$80`, and its tables read through bank `$00` are slow where
    the run had them fast. The harness said MODEL WRONG on four calls of
    four.
  * I wrote that the bits level 37's thing clears when it stops circling
    make it one nothing touches. They are the two that draw it in front of
    every layer and sort it first. Corrected before the write-up.
* **Untaken by the corpus.** Five of the 24 new sites.
  * The second loop ending with nobody playing, or with neither player
    having a neighbour.
  * The tiles' job finding its list busy, or empty.
  * Level 37's thing told to stop circling from a place off the level.
* **Not established.**
  * What level 37's thing does at `$82:EA35`. I call it leaving the circle
    and no more.
  * That `$1FB8` and `$1FBA` are who has left by the exit. The second loop
    ends when they add up to how many are playing; I have not read what
    sets them.
  * What the jumping neighbour's second record is. It stays on the ground
    where the neighbour started.
  * What the picture at `$8F:E82D` on the portraits' screen is.
* **Still the ROM's.**
  * Of level 37's thing: the state at `$82:EE0E`, hiding, coming back and
    being hit. Each sleeps inside a call.
  * The hardware multiply in the big letters, `$82:ADDB`, on purpose.
* **Next.** Bank `$80` is 88,539 of the 162,997, and 41,349 of that is the
  two stretches that stay the ROM's on purpose. The largest piece after
  them is still a level's start, `$80:85BB` on, 3,743: it is calls with a
  few instructions between each. Then the creature at `$81:9063` on levels
  13 and 41 (`$81:9530`, `$81:9107`, about 3,500), and the fractions at
  `$80:F6B4`.

### The password screen: a password checked, a letter picked, and five fades (2026-10-07)

What the 65816 still executed over the twelve movies goes from 203,078 to
**183,910** instructions of work. Live, the twelve read as they did to a
tenth of a percent, in work and in calls. The game registers 469 routines,
14 more. The corpus has three movies more, 54.

Eleven of the twelve movies type a password before their level, so what
they shared of bank `$82` was mostly that screen.

* **A password checked**, `$82:B018`, new in `port/password_check.c`: four
  letters in, a level and a count of neighbours out. How the letters name
  them was already in `assets/password.h` and `docs/password.md`, and the
  port uses that header's names. What is new is the ROM's routine itself.
  * **A password it turns down is left changed.** It swaps the first and
    third letters to make a word of each pair, and swaps them back only
    when it has found both. Turned down, the level is 1 and carry is set.
* **A letter picked**, `$82:B3F6`, in `port/cursor.c`: the character under
  the cursor put at the end of what has been entered. Three characters of
  the grid are not letters. `$3B` ends the entry. `$3C` is entered as
  `$2F`. `$3A` takes the last one back, and that one is still the ROM's.
* **The end of a turn that did something**, `$82:B28A` and `$82:B2C5`: the
  clock set to three hundred turns again, and the count a plain turn ends
  with.
* **Five loops that fade a screen**, new in `port/dim.c`. The screens of
  bank `$82` do not fade through the scheduler. Each has a loop of its own
  round a `WAI` that moves the brightness one step. A frame of each is one
  entry: three that darken and two that lighten.
* **The job that sends a screen's words to VRAM**, `$82:B82E`, in
  `port/dma.c`, and its copy at `$82:B9B6`.
* **The top scores' lines**, `$82:BAB9`, in `port/card.c`: from where the
  printer comes back with one line to where it is called for the next.
  * `port/card.h` had `$82:BA11` as the wait after a level's name. It is
    the screen of top scores: its title is the cartridge's words for them,
    and the ten lines under it come from the table at `$7E:2064`. The
    header says so now.
* **A footballer coming on**, in `port/football.c`. `$81:C7A5` puts it
  just off whichever edge of the screen is nearer to where it was asked
  for, facing in. `$81:C7E6` is its start: a record, and its page cleared.

* **Three movies more**, each a password the screen does not take as a
  level and a count. `tools/make_password_movie.py` wrote them.
  * `password-bcdf`: the one password tested by its letters. It stores
    level 0. The card that comes up reads DAY OF THE TENTACLE, at frame
    2400, and a level follows that no other movie reaches.
  * `password-no-level` and `password-no-count`: turned down at each of
    the routine's two tables. Both then start level 1, which is what frame
    2990 shows of each.
* **`tools/cycles816.py --same-page`.** The tool would not price a read
  through an 8-bit index register, because a page crossed costs a cycle
  more. The option is the caller's word that none in the run crosses.

* **Checked.** The corpus verifies at 34,249,214 calls across 54 movies
  with 0 diverged, and 1,204 of 1,427 coverage sites. Every call
  priced is exact. It turns down 3,577 calls. Run first on last round's
  51 movies it was 33,442,941 calls, with 3,561 turned down as before.
* **Lockstep** over the corpus: 334,319 passes, 51 of 54 never part, the
  same three level-25 movies. On the 51 it was 325,322 passes as before.
  Mean drift is a little smaller on `level21` and `demo-end` and the same
  on the other 49. No worst figure moved.
* **What it took off.** Bank `$82` goes from 49,318 instructions to
  31,368, and bank `$81` from 45,449 to 44,231. Ten of the eleven movies
  that type a password are each down by between 1,200 and 2,100, and
  `level21` by 3,193 with its footballers.
* **Nothing diverged and nothing was mispriced on its first run** this
  round. One mistake was mine all the same: I wrote the password's port
  before looking for what the project already had on passwords. It had
  the whole format. The port now uses those names and says less.
* **Untaken by the corpus.** None of the 21 new sites, with the three
  movies in. Before them four were: the three ways a password is not a
  level and a count, and the grid's `$3C`.
* **Not established.**
  * That `$2F` is a space. The grid has it between its letters, and I take
    it from that.
  * The cursor's screen has a second table, with six places and a grid
    that has vowels. I take it to be where a name is entered for the top
    scores. No movie reaches it.
  * What the two tests are that a footballer's place has to pass
    (`$80:AE14` and `$80:B422`), and what `$7E` of its page is for.
* **Still the ROM's.**
  * The character that takes one back, and the flash of a finished entry
    (`$82:B396`).
  * The cursor's screen being drawn (`$82:B4B7`), which writes the
    hardware, and its cursor's record (`$82:B584`).
  * The first of the top scores' ten lines, and the screen's start.
* **Next.** Bank `$80` is now 98,178 of the 183,910, over half, and 41,349
  of that is the two stretches that stay the ROM's on purpose. What is
  left after them is a level's start: `$80:857E`, `$80:8868`, `$80:97AC`
  and `$80:9254`, about 13,500 between them. Then the creature at
  `$81:9063` on levels 13 and 41, still unnamed, and level 37's threads in
  bank `$82`.

### A martian's pass that fires, weapon 5's shot, and three small pieces (2026-10-07)

What the 65816 still executed over the twelve movies goes from 217,485 to
**203,078** instructions of work. Live, all twelve stay at 99.9% of the
work, and `level17` reads 100.0%. The game registers 455 routines, 10 more.

* **A martian's pass that fires**, in `port/martian.c`. A shot sleeps
  twelve ticks in the middle of the call that fires it, so such a pass was
  the ROM's, and so was the pass it woke in. A walker's is now two
  stretches. The frame ends at the `JSL` that asks for the shot's thread,
  three return addresses down. The rest is a new entry at the `RTS` the
  sleep comes back to, `$81:99C9`.
  * One that fires while arriving is still the ROM's. Its shot is called
    two ways, and the stack is not the same shape.
  * The rest of a pass is turned down when its look chooses no way to go:
    14 of 164 on `level21`. See `docs/cosim.md` for why.
* **A martian's start and its end.** `$81:9AC0` takes its record and clears
  its page, as far as its first pictures. `$81:9A17` and `$81:9A7B` are two
  more copies of the seven instructions a thread ends with, and
  `record_end` in `port/begin.c` has them.
* **Weapon 5's shot**, new in `port/shot5.c`: four stretches of the thread
  at `$81:EBE2`.
  * `$81:ED1A`, its record, in front of whoever fired it.
  * `$81:EC79`, that record moved to the mouth of the weapon, with the
    picture for the way it goes.
  * `$81:EC03`, a frame of its flight.
  * `$81:EC30`, where it bursts.
* **What stops that shot.** Each frame it asks the ground about one point.
  Carry stops it and leaves the tile alone. A tile with bit 1 of its
  attributes and any of bits 4, 5 and 6 stops it, and the ROM then changes
  the tile. A tile with bit 1 alone it flies over, for sixty such frames in
  all, and then it is gone without bursting. What the bits are to the game
  I have not established. By what the code does this is still the bazooka.
  I have not seen it on a screen.
* **Its step is not the same every way.** Four pixels a frame up or left,
  three down or right, two and two on a slant.
* **A footballer's picture**, `$81:C824`, for the ROM to call in a pass
  that is its own. The C was there already, inside the frame.
* **A tick of a fishman's sweep**, `$81:E381`. The state at `$81:E31C`
  takes a second record with no picture and collide id 3, and for five
  ticks puts it at the next of eight places in a ring about the fishman.
  By what the code does that is a swipe at whoever is beside it on land.
  The port had named the state a lurk, which was a guess. The header says
  so now. I have not seen it on a screen.

* **Checked.** The corpus verifies at 33,438,605 calls across 51 movies
  with 0 diverged, and 1,183 of 1,406 coverage sites. Every call priced is
  exact. It turns down 3,561 calls where it turned down 3,709: a walker's
  pass that fires is no longer turned down.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies. The mean drift is smaller on thirteen movies
  and larger on none. `level41` goes from 1,125.0 cycles to 841.3, and the
  seven level-21 movies by between 0.5% and 10%. On one, `level24-carry`, the
  worst single figure is larger, 84,096 for 83,924, with a smaller mean. I
  did not run any of this round's entries alone to see which did what.
* **The share of calls**: `level21` from 99.4% to 99.6%, `level5` from
  99.7% to 99.8%. The other ten read as they did.
* **What it took off.** Bank `$81` goes from 59,856 instructions to
  45,449. `level21` alone goes from 36,636 to 27,692.

* **Two mistakes of mine the checks caught.**
  * The registry was full. Four entries took it to 449 with room for 448,
    and `zamn_cosim` said so and stopped. It is 512 now.
  * `$81:EC79` was priced 8 cycles dear, on every call. It reads two words
    through X from a table in the cartridge, and I had priced them as reads
    of WRAM.
* **Untaken by the corpus.** One of the 14 new sites: the shot's sixtieth
  frame over a tile with bit 1 alone, where it is gone. No run of mine
  checks that against the ROM.
* **Not established.**
  * The shot is handed a side, 0 or 2 in `port/score.h`'s sense, and its
    start reads a height and a table of places by it. Why those differ by
    side I have not established. I take it to be that the two characters
    are drawn differently, and have not run it with two players.
  * A martian's start clears `$32` and `$7E` of its page. I have not read
    what uses them.
* **Still the ROM's.**
  * What weapon 5's shot does to the tile it stops at, `$81:ECAC`. It
    calls `$80:AB5A`, which has no entry.
  * The first stretch of that shot's thread, and the first test of the
    ground it makes.
  * A martian that fires while it is arriving.
  * The creature at `$81:9063` on levels 13 and 41: `$81:9530` and
    `$81:9107`, about 3,500 instructions. I have not found out what it is,
    so I have not named anything of its.
* **Next.** Bank `$82` is now 49,318 of the 203,078, a quarter. What
  every movie has of it is the screen a level opens with, which waits on
  `WAI` and writes the hardware; the rest is threads of levels 37, 21 and
  5. That is by the addresses in the profile. I have not gone through it.
  After it: `$80:AB5A`, which the shot's tile wants. `$80:857E`, a level's
  start. The creature at `$81:9063`.

### The pieces of a player's pose, two poses that sleep, and the starts and ends of five threads (2026-10-07)

What the 65816 still executed over the twelve movies goes from 244,319 to
**217,485** instructions of work. Live, all twelve stay at 99.9% of the
work, and `level17` reads 100.0%. The game registers 445 routines, 35 more.

No one of the 35 is large. The round is the long tail the last one ended
on: the stretches of a thread before its loop and after it, and the pieces
of a player's frame the ROM still reaches when the whole frame is turned
down.

* **Where the player's frames in the profile come from.** The live profile
  runs 20,000 frames and most movies end well before that. Past 9,000
  frames the same weapon 5, the same potion's monster and the same
  second-band weapons turn up on movie after movie, in the same numbers. I
  take them to be the title's demos, and did not watch one. A run at a
  corpus length never reaches them, so this round's entries were checked
  at 20,000 frames.
* **The pieces of a pose**, in `port/pose.c`. A handler the port turns
  down is the ROM's, and the ROM still jumps to what it is made of. Now
  those are entries: the pose the direction asks for (`$80:D4E9`), a stand
  begun (`$80:D4F4`), a walk begun (`$80:D65B`), and the shot
  (`$80:ED30`). The two walks that had no entry of their own have one: the
  second band's (`$80:D6B8`) and the monster's (`$80:D6DC`). So has the
  monster's state (`$80:D2EA`).
* **Two poses that sleep inside themselves.** Weapon 5's kick begins at
  `$80:DE9D`, and a second-band weapon fired standing is `$80:EE82`,
  `$80:EEA8` and `$80:EEB7`. Each is a stretch from one sleep to the next.
* **Weapon 5 kicks backward.** `port/lunge.h` said its fifteen frames go
  forward. The table at `$80:DF6C` has five pixels down for a player
  facing up. I had not read it against a table whose ways are known. The
  header is corrected. By what the code does this is the bazooka: its
  shot breaks walls and firing it throws the player back. I have not seen
  it on a screen.
* **The doll's handler has a price.** `$81:B41C` and the survivor's
  reaction it jumps to, `$81:8506`, are a straight line either side of
  each branch. Last round named this as what a punch on a doll wanted.
  Counted, it was two frames in eight movies, and both tell a neighbour
  too. A player, a neighbour and what lies on the ground still have no
  price, so no punch is the port's for this yet.
* **Five threads' starts and ends.**
  * The spawn list's start (`$81:80EC`), in `port/spawnlist.c`. It
    installs the list's bank with `PEI ($02) : PLB`, which leaves a byte
    on the thread's stack for good.
  * A clone's start and its growing (`$81:8E89`, `$81:8D16`, `$81:8D29`),
    in `port/clone.c`. Eleven pictures, ten frames each.
  * The swipe's thread round its two calls (`$81:E8A8`, `$81:E8C8`,
    `$81:E8DF`). It lasts two ticks, or one if its owner moves.
  * The thing thrown in an arc, from its start to its end (`$81:F976`,
    `$81:F99C`, `$81:F9B2`, `$81:F9BC`, `$81:F9C6`). Landing, it tells
    everything within forty pixels, twice.
  * A survivor's flash (`$81:8542`, `$81:8554`).
* **How a thread ends**, in `port/begin.c`. Seven instructions, a copy for
  each kind of thing: the weight given back, the record loaded, a jump to
  free it. Five copies are entries, and so is where each of the three
  zombies' loops goes when it is leaving. A killed thing's last pictures
  (`$81:83A3`) are two more.

* **Checked.** The corpus verifies at 33,427,039 calls across 51 movies
  with 0 diverged, and 1,170 of 1,392 coverage sites. Every call priced is
  exact. It turns down 3,709 calls where it turned down 3,521: some of the
  new entries turn calls down themselves.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies. Seven movies drift less than they did and
  none more. Four are level 29's, where the dolls are: `level29-item`
  goes from a mean of 8,823.9 cycles to 2,924.8. I take that to be the
  doll's handler's price, and did not run it without.
* **The share of calls**: `level37` from 99.6% to 99.7%. The other eleven
  read as they did.
* **What it took off.** The player thread's area goes from 29,197
  instructions to 18,519, and bank `$81` from 76,012 to 59,856.

* **Four mistakes of mine the checks caught.**
  * The doll's handler was priced from its third instruction. `verify`
    had it 36 cycles short on 11 of 66 calls.
  * I wrote an entry for `$80:EED3` that was there already, as
    `fired_wait`. Nothing diverged. The corpus's coverage showed three
    sites that had been taken going untaken, and that was the old entry
    shadowed. Mine is gone.
  * A sound inside a priced stretch. A sound waits for the sound chip to
    take the last one, and a killed thing's waited 1,048 cycles on one
    call of 16. Four stretches now end at the `JSL` that plays their
    sound, and the sound's price is its own entry's.
  * The swipe's start charged its fallback where its cut had played a
    sound. That is a fixed 9,000 cycles for a ROM that took 45,678. Such
    a start is turned down now: 3 of 26 on `level13`.
* **Untaken by the corpus.** Two of the 28 new sites. A clone started with
  both players in the game, where whose double it is comes from a random
  draw. And the shot asked for on its own while the last one's delay
  runs. No run of mine checks either against the ROM.
* **Not established.** Two words count zombies a player killed, `$1F64`
  and `$1F66`. I have not looked for what reads them. `$1E84` is named
  for the character each player plays, from what the clone does with it.
  The swipe and the thing thrown in an arc I have still not seen on a
  screen.
* **Still the ROM's.**
  * Weapon 5's shot, the thread at `$81:EBE2`: about 6,500 instructions on
    the three movies whose demo fires it.
  * A stand that goes on to a second-band weapon's aim: the entry at
    `$80:D4F4` turns it down, and the ROM runs as far as the aim.
  * The thing thrown in an arc landing by something with no price: 5 of
    13 on `level21`.
  * A monster's punch on a player, a neighbour or something on the
    ground.
* **Next.** Weapon 5's shot. Prices for `$83:A364` and `$80:CAEE`, which
  a punch and a landing both want. Level 21's thread (`$81:99E0` on).
  The title card in bank `$82`, which waits on `WAI` and writes the
  hardware.

### A swimmer's strokes, a monster's first swing, a slime's attack begun and the zombies' start (2026-10-06)

What the 65816 still executed over the twelve movies goes from 265,367 to
**244,319** instructions of work. Live, all twelve stay at 99.9% of the
work, and `level17` reads 100.0%. The game registers 410 routines, 5 more:
the swim's pose and four of the zombies' start.

What is left is a long tail. After the two at the top, which stay the
ROM's on purpose, nothing is over 6,000 instructions across the twelve.

* **Why a player's frame was turned down**, counted before anything was
  written. Of 814 frames: 288 told the player of a hit, 126 were the
  potion's monster's, 112 the ordinary state said no to, 108 were a
  swimmer's, 32 a player caught by a slime's glob, 23 that player
  getting free.
  * So last round's open question has its answer. A swimmer's frame was
    turned down by the pose, `$80:DCA2`, which no port had.
* **A swimmer's strokes**, `$80:DCA2`, in `port/pose.c`. Eight pictures,
  one every ten frames, the same eight whichever way they face. It names
  the movement afresh each frame. A swimmer's whole frame is the port's.
* **The monster's first swing**, in the same file. The first of a punch's
  four pictures makes a sound and tells nobody, and that frame was the
  ROM's.
* **A stuck player getting free**, in `port/stuck.c`. The frame the
  countdown ends on puts what they held back in their hands, with last
  round's `player_hands_back`.
* **A slime's attack begun**, in `port/slime.c`. A pass that began one
  was turned down whole. It ends now where the attack's pictures start,
  `$81:CBBC`, inside the state and not at the loop's sleep.
* **The zombies' start**, in `port/zombie.c`, for the slow kind and the
  fast: from the thread's first instruction to where it rises out of the
  ground, and from there to its first sleep. Four entries.

* **Checked.** The corpus verifies at 33,404,930 calls across 51 movies
  with 0 diverged, and 1,144 of 1,364 coverage sites; 7 of the 8 new ones
  are taken. Every call priced is exact. It turns down 3,521 calls where
  it turned down 7,178.
  * The corpus caught one price of mine. The zombie's start counted 9
    bytes where there are 11, which shows only where the model's FastROM
    flag is off. That is `level1-map.zmv` and no other of the 51. Why it
    is off there I did not look into.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes. Every figure in the
  report is last round's.
* **The share of calls**: `level5` from 99.6% to 99.7%. The other eleven
  read as they did.
* **What it took off.** Over the twelve movies a player's whole frame is
  turned down 625 times where it was 814. No slime's pass is: 246 were.
  The zombies' starts ran 21 and 23 instructions of the ROM each, 224
  times, and run none.
* **Untaken by the corpus.** One of the new sites: a swimmer who faces
  nowhere, and so has no movement. No run of mine checks it against the
  ROM.
* **Still the ROM's.**
  * A punch that lands on an evil doll. The frame is run and is right,
    and the harness turns it down because `$81:B41C`, the doll's handler,
    has no price. 42 frames on two movies.
  * The frame a slime's glob catches a player on (`$80:DC1E`), which
    sleeps a tick in the middle.
  * The third kind of zombie's start, `$81:8C17`.
* **Next.** `$81:B41C`'s price, which wants `$81:8506`'s. The ends of
  threads, which give back their weight and free a record: `$81:CF39`,
  `$81:E8DF` and others like them. Level 21's thread (`$81:99E0` on),
  about 10,000 instructions and all on one movie. The password screen in
  bank `$82`, which waits on `WAI` and writes the hardware.

### A locked door, a swim, level 37's dart and ten small things (2026-10-06)

What the 65816 still executed over the twelve movies goes from 0.30 to
**0.27** million instructions of work. Live, all twelve stay at 99.9% of
the work. The game registers 405 routines, 11 more: the swim and the ten
small things below.

Most of it was passes that ports turned down, not routines with no port.

* **A locked door**, in `port/walk.c`. `player_walk` turned down any step
  onto a tile with a reaction of its own, and the player's whole frame with
  it. Two of the six reactions are the port's now:
  * the third, a door. With nothing to open it a thread is begun at
    `$82:DEFB`, which plays a sound twice. A player pushing at one does
    that every frame, which is what `level5` was doing for about a hundred;
  * the fifth, which begins a thread for the square of 64 pixels the
    player is in, once. What that thread shows I have not seen.
  * A door that opens is still the ROM's, and so are the other four.
* **A swim**, `$80:E543`, in the same file. A player in water moves by it:
  water, the other player and the level's edge are asked after, and nobody
  else. The stroke that reaches the bank is the ROM's.
  * `terrain_point_bit8`, which it asks, was priced by an average and is
    priced by its path now.
* **Level 37's dart**, in `port/seeker.c`. The thing there darts at
  whoever is nearest, four pixels a hop, up, down or across. That is state
  `$82:ECC1`, and the three frames of a hop were the port's already.
  * Hidden, it looks for somewhere to come back: `$82:EBCF`. The frames
    that find nowhere are the port's.
  * Watching, it goes on to the state at `$82:EE0E` on a draw. That frame
    is the port's and the state is not.
* **Ten small things**, in a new `port/player_small.c`. A player who is
  hit, drowns or turns monster goes through routines that are the ROM's,
  and they share leaves:
  * nothing can touch the player, and that undone (`$80:F366`, `$80:F377`);
  * nobody is told about them, and that undone (`$80:F36C`, `$80:F382`);
  * what they hold kept aside, and put back (`$80:F38D`, `$80:F3A5`);
  * how far a point is from them (`$80:F3B4`);
  * the weapon in hand not drawn (`$80:ECFD`);
  * the second of the walk's reactions looking for a tile (`$80:F3E3`);
  * a tick of the wait after a shot (`$80:EED3`).

* **Checked.** The corpus verifies at 33,396,982 calls across 51 movies
  with 0 diverged, and 1,137 of 1,356 coverage sites; 29 of the 38 new
  ones are taken. Every call priced is exact. It turns down 7,178 calls
  where it turned down 7,487.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **The share of calls**: `level5` from 99.4% to 99.6%, `level13` and
  `level37` from 99.5% to 99.6%, `level41` from 99.4% to 99.5%, `level21`
  from 99.3% to 99.4%.
* **What it took off.** Over the twelve movies the walk is turned down 9
  times where it was 137, the swim runs in the ROM 4 times where it did
  108, and level 37's frame is turned down 172 times where it was 327. A
  player's whole frame is turned down 814 times where it was 939.
* **Untaken by the corpus.** Nine of the new sites:
  * a door's tile with no tile to look at, and one to look at that is not
    a door;
  * five squares held and a sixth wanted;
  * a swimmer too far from the other player, and one at the level's edge;
  * the tile search for a player in state `$06`;
  * level 37's dart arriving, and its hidden thing finding bad ground or a
    place off the level.
  * The corpus runs `level37.zmv` for 4,700 frames. Run for 30,000, as I
    did by hand, it takes the dart's arrival and the bad ground, with 0
    diverged. The other seven no run of mine checks against the ROM.
* **Still the ROM's.** On level 37: hiding, coming back, being hit, and
  the state at `$82:EE0E`. Each sleeps inside the state. A player's shot
  (`$80:ED30`) where the pose that fires is the ROM's. A swimmer's whole
  frame is still taken a piece at a time, and I have not looked at which
  piece says no.
* **Next.** The two at the top stay the ROM's on purpose (`$80:897F` and
  `$80:9512`). After them: a level's start (`$80:857E` on), the screens'
  clocks in bank `$82`, a player's shot and the poses that fire
  (`$80:D4F4`, `$80:EE82`, `$80:ED30`), level 5's and level 21's threads
  (`$81:8CD4`, `$81:9D1F`, `$81:9AA2`), and the screen's registers reset
  (`$80:88A9`), which writes the hardware.

### Where a werewolf's pounce comes down, a fishman on land, and five small things (2026-10-06)

What the 65816 still executed over the twelve movies goes from 0.33 to
**0.30** million instructions of work. Live, all twelve stay at 99.9% of
the work. The game registers 394 routines, 7 more.

Two creatures of levels 13 and 41 had 38,000 of the 332,000 between them:
the werewolf and the fishman. Both had ports, which turned passes down.

* **Where a werewolf's pounce comes down**, in `port/werewolf.c`.
  `werewolf_frame` turned down a pass that went on to choose the spot, and
  one that hopped away hurt. Both are the port's now:
  * the spot past whoever it pounces at (`$81:A8E8`), or near itself by
    three draws (`$81:A913`);
  * the four things that rule a spot out, and the crouch (`$81:A7E6`).
  * Two routines the choice asks were priced by an average, and are priced
    by their path now: `actor_bearing_point` and `terrain_footprint_bit12`.
* **A fishman on land**, in `port/fishman.c`: state `$81:DA1D` of the one
  that keeps to its pool. It walks at whoever is nearest, an axis at a
  time, and looks for water to dive back into.
  * `port/wander.h` was "a thing looking for somewhere near to go", which I
    had not seen. It is this fishman looking for water, and says so now.
* **A fishman begun and ended.** Whatever places the one that patrols asks
  again and again, and one with no player near leaves on its first pass:
  62 were begun in the two movies and 47 of them left at once.
  * `$81:E51A`, from the thread's first instruction to its first sleep.
  * `$81:E567`, from the test of its fate to the thread's `RTL`.
* **Five small things**:
  * `$81:E1D6`, the test of where a fishman's leap comes down. The pass
    that leaps is still the ROM's, and calls the port for this.
  * `$80:F0D7`, the tile a punch lands on, in a new `port/knock.c`.
  * Three things a level begins with, in `port/clears.c`: the threads'
    tables and the vblank queues cleared (`$80:820A`), a level's counts
    cleared (`$80:8947`), and the cartridge's top scores copied in
    (`$82:BB0D`).

* **Checked.** The corpus verifies at 33,374,357 calls across 51 movies
  with 0 diverged, and 1,108 of 1,318 coverage sites; 14 of the 19 new
  ones are taken. Every call priced is exact.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **The share of calls** moved where it should: `level13` from 99.3% to
  99.5%, `level41` from 99.2% to 99.4%, and `level37` from 99.4% to 99.5%.
* **What it took off.** The werewolf's instructions go from 12,103 to
  2,043, and the fishman's from 25,916 to 11,012.
* **Untaken.** A fishman begun with no room on the level, and one not put
  on deep water. A fishman on land finding water past where the ground
  stopped it. A werewolf's spot exactly 19 across, and one off the level.
  None of those five is checked against the ROM by any movie.
* **Still the ROM's.** A werewolf's crouch and its landing, which sleep. A
  fishman's leap from the draw to the flight, its bite on land
  (`$81:E31C`), its dive's start (`$81:DB04`) and its splash. A fishman on
  land with somebody within 28 or nobody within 175. The other kind of
  fishman's beginning: no movie has one.
* **A slip of mine.** Once, checking the build, I ran `zamn.exe` with no
  arguments. I stopped that process, found by its parent to be mine. No
  settings or saves were written.
* **Next.** The two at the top stay the ROM's on purpose (`$80:897F` and
  `$80:9512`). After them: a level's start (`$80:857E` on), level 37's
  thread (`$82:ECC1`, `$82:EBCF`), `$80:F354`, the two screens' clocks
  (`$82:BA36`, `$82:B2BE`), and the screen's registers reset (`$80:88A9`),
  which writes the hardware.

### A player hit, a page begun, a martian's shot and three small things (2026-10-06)

What the 65816 still executed over the twelve movies goes from 0.37 to
**0.33** million instructions of work. Live, all twelve stay at 99.9% of
the work. The game registers 387 routines, 6 more.

* **A player hit**, in a new `port/hit.c` and in `port/flinch.c`.
  * `$80:D02D`, what is done about a hit: one from their health, their
    other record no longer drawn, and on to how it shows.
  * `$80:D089`, the start of the flinch: the list of pictures for the way
    they face, and the first of them. `flinch_frame` had the rest.
* **A player's page begun**, in a new `port/page.c`: `$80:D13A` as far as
  its first call. It clears the page a word at a time, which was nearly
  all the routine cost.
* **Two pieces of a martian's pass that fires**, in `port/martian.c`. The
  pass is still the ROM's, because the shot sleeps inside a call. But the
  ROM now calls the port for `$81:9981`, as far as the shot's thread being
  asked for, and for `$81:9C99`, the walking picture.
* **Three small things**:
  * Two more states of level 37's thread in `seeker_frame`: `$82:ECA3`,
    which faces whoever is nearest and is drawn, and `$82:ECF7`, three
    frames shown at two places by turns.
  * `$81:CE40`, the box a slime's glob tells of when it lands, in
    `port/slime.c`.

* **Checked.** The corpus verifies at 33,372,679 calls across 51 movies
  with 0 diverged, and 1,094 of 1,299 coverage sites; 12 of the 14 new
  ones are taken. Every call priced is exact.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **The share of calls** moved where it should: `level21` from 99.0% to
  99.3%, and `level37` from 99.3% to 99.4%.
* **Untaken.** A hit on a player with no health. A hit on one in the
  pictures the table at `$80:FCF6` has.
* **Still the ROM's.** The other way a hit shows (`$80:D056`). The rest of
  a player's page, a dozen calls. The pass a martian fires on, around the
  two pieces. Level 37's thread after its third frame at two places, and
  its states at `$82:EBCF`, `$82:ECC1` and `$82:EE0E`.
* **Named by what the code says.** The pictures at `$80:FD72`, which have
  one flinch picture and not a list, I have not seen. Nor the set in
  which a hit takes nothing. What level 37's thing is I still do not
  know.
* **Next.** The two at the top stay the ROM's on purpose (`$80:897F` and
  `$80:9512`). After them: level 37's thread again (`$82:ECC1`,
  `$82:EBCF`), the tile a fist knocks (`$80:F0D7`), `$80:F354`,
  `$82:BA36`, and the cursor screen's clock (`$82:B2BE`).

### The share of calls, counted from what the CPU did (2026-10-06)

The second number in the title bar was wrong, and is right now. On
`level1` it said 60.2%. It says **99.9%**: 178,650 of the 178,894 calls
the game made were served by the port. Over the twelve movies it is 99.0%
to 99.9%, where it said 60% to 74%.

* **A call is counted when it is made.** The counter took the CPU being
  *on* a `JSR` or `JSL` for a call. 40 routines the port has begin with
  one, and when the port serves such a routine nobody executes it. That
  was 148,772 of the 327,642 "calls" on `level1`.
* **A serve is counted when the entry was called.** The harness now keeps
  where the last call went, and a port taking that entry on the next step
  has served it. An entry reached by an `RTL`, a jump or by falling into
  it serves nothing. The registry's `uncalled` flag said this by hand for
  154 rows; nothing reads it now.
* **An interrupt between a call and its entry** does not lose the call.
  It is kept until the handler's `RTI` is back at that entry.
* **Checked against the profile.** Its call graph records a call only when
  the instruction ran, and always did. Both numbers on `level1` and on
  `level5` are its numbers exactly.
* **Nothing else moved.** The work share's two numbers are the same to the
  cycle on all twelve movies. Lockstep's report is the same file, byte for byte: 325,322 passes, 48 of 51 never part.

### The weeds, the neighbours' loops, what a level leaves on the ground, and eight small things (2026-10-06)

What the 65816 still executed over the twelve movies goes from 0.51 to
**0.37** million instructions of work. Live, all twelve are at 99.9%: six
were at 99.8%. The game registers 381 routines, 27 more.

* **The weeds**, in a new `port/weeds.c`.
  * `$81:D2A5`, a pass of the thread: it grows an arm a tile at a time
    while nobody is near, is on its guard while somebody is, and rests
    after it has snapped. Levels 9, 13 and 41 have it. It was the largest
    family left, 24 thousand instructions over six stretches.
  * `$81:D4D6`, a pass of what it puts out when it snaps: a straight line
    to where the player was, in an arc.
* **The neighbours**, in new `port/begin.c` and `port/neighbours.c`.
  * `$83:A13E`, the record all eleven kinds begin with.
  * Six of their loops, a frame each. Three only go round their pictures
    (`$83:96C0`, `$83:979D`, `$83:9E3C`). The tourists do that and ask
    first whether to turn into werewolves (`$83:A009`). Two look about
    them, and are alarmed by a monster within a hundred (`$83:9AB7` with
    `$83:9AFB`, `$83:9969` with `$83:99A6`).
  * `$83:A210`, what a rescued neighbour leaves, going up.
  * `$83:A30A`, the sign ten of them put up beside themselves.
* **What a level leaves on the ground**, in a new `port/objects.c`: the
  four routines the pickups' thread calls (`$80:C9A5`, `$80:C9E3`,
  `$80:CAA8`, `$80:CABF`), and three of the neighbours' list
  (`$82:DB46`, `$81:817E`, `$81:81A2`).
* **Eight small things**:
  * `$81:8000`, a record begun at the page's place, and `$81:87BE`, a
    zombie's made of it, in `port/begin.c`.
  * `$81:C70B`, a footballer sent off, as a fifth state of
    `footballer_frame`.
  * `$80:DEC5`, a player gone forward fifteen frames, in a new
    `port/lunge.c`.
  * `$81:F16D`, the decoy, in a new `port/decoy.c`.
  * `$82:882C`, `$80:9C63` and `$80:9C7D`, three vblank jobs: the mosaic
    off, and the screen a step brighter or darker. In `port/vblank.c`.

* **Checked.** The corpus verifies at 33,315,506 calls across 51 movies
  with 0 diverged, and 1,082 of 1,285 coverage sites; 60 of the 65 new
  ones are taken. Every call priced is exact.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **One bug the first verify found.** The weeds failed 143 passes of 155
  on `level9`. Four of its eight arms read their steps with an index that
  is negative, and `LDA abs,X` carries such an index into the next bank;
  the port wrapped it inside the bank. See `docs/cosim.md`.
* **Untaken.** The decoy on a tile that ends it. A player gone forward
  stopped by the leash, or by the level's edge. A neighbour's sign
  running out its turns. A neighbour in the list with no gate.
* **Still the ROM's.** The pass on which the weeds snap. What a neighbour
  does when it is told something, and the sign's making. The tourists
  turning. How a player comes to be going forward, and ground that sends
  them on from it. The decoy on a tile that moves it.
* **Named by what the code says.** The tourists are the neighbours the
  ROM replaces with two werewolves. The decoy has collide id `$38`, one of
  the four that monsters look for. The weeds plant tiles and are on level
  13. I have seen none of them on a screen, and which neighbour each of
  the other five loops is I do not know.
* **The share of calls is counted wrongly, and has been.** It did not
  move this round, and it should have. 40 routines the port has begin with
  a `JSR` or a `JSL`, and each time one is served the counter takes that
  instruction for a call, which the port never makes. On `level1` that is
  148,772 of the 327,642 "calls". Of the 178,894 calls really made there,
  178,650 land on a routine the port has. Not fixed: see `docs/cosim.md`.
* **Next.** The two at the top stay the ROM's on purpose (`$80:897F`,
  6.9%, and `$80:9512`, 3.2%). After them: a player hurt (`$80:D02D`,
  `$80:D082`), a player's thread starting (`$80:D13A`), the martians'
  shot (`$81:9981`, with `$81:9C99`), the slime's splash (`$81:CE39`),
  and more states of level 37's thread (`$82:EB13`, `$82:ECA3`,
  `$82:EBE8`).

### Four threads' frames, a swipe that cuts tiles, and five small things (2026-10-06)

What the 65816 still executed over the twelve movies goes from 0.62 to
**0.51** million instructions of work. Live, ten of the twelve go up a
tenth or two; `level1` and `level17` do not move. The game registers 354
routines, 11 more.

* **Four threads, a frame each**, from the sleep's return to the next
  sleep:
  * `$82:F054`, a thing in level 37 that steers after whoever is nearest,
    in a new `port/tracker.c`. It keeps a speed across and one down, and
    each takes one more towards them a frame, up to five.
  * `$82:F4B8`, the thing level 21 keeps beside another, in
    `port/follower.c`, which had only its placing. Every seventh frame a
    place on and the next picture, and it is drawn every other frame.
  * `$81:D723`, a thing in levels 5 and 17 that walks until something is
    in its way and then turns a quarter, in a new `port/walker.c`. It has
    three states and all three are here.
  * `$80:E3D6`, a player carried along until something stops them, in a
    new `port/carried.c`. It is a pose that sleeps inside itself, so it
    was never `player_frame`'s to take.
* **A swipe that cuts tiles**, in a new `port/swipe.c`. `$81:E8F1` makes
  its record in front of its owner, and `$81:E979` looks at a list of
  tiles there and changes the ones with either of two bits. Levels 13, 37
  and 41 have it, and `level9-weapons` uses it three hundred times.
* **Five small things**:
  * `$80:ABD3`, one tile changed in the level's map and on the screen if
    it is there, in a new `port/tile_put.c`. The swipe calls it, and so
    do five places that are still the ROM's.
  * `$81:A74D`, a step of a thing going after the one it has chosen, in a
    new `port/pursuer.c`.
  * `$80:BDF2`, every display record freed, in `port/clears.c`.
  * `$80:9FB0`, three quarters of VRAM zeroed in one transfer, and
    `$80:9FDF`, the vblank job that sends the level's colours in two, both
    in `port/dma.c`.

* **Checked.** The corpus verifies at 33,297,294 calls across 51 movies
  with 0 diverged, and 1,022 of 1,220 coverage sites; 48 of the 55 new
  ones are taken. Every call priced is exact.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **One bug the corpus found**, after three movies had passed it.
  `swipe_cut` claimed an overflow that a tile's lookup can change, and
  `level9-weapons` failed 28 calls of 300. It is `wander_pick`'s bug of
  last round over again. See `docs/cosim.md`.
* **One the first verify found.** The pursuer's routine calls its own
  step and then falls into it, so its `RTS` is reached twice, and the
  harness stopped the ROM at the first. The entry is the step now.
* **Untaken.** Ground, the leash and the level's edge stopping a carried
  player: every one in the corpus runs out its count or meets a thing.
  The pursuer on top of its target. A tile cut by its second bit. A tile
  numbered low enough to be drawn over the sprites. The steering thing's
  frames running out.
* **Still the ROM's.** The draw before the pursuer's step. The follower's
  look at its tile, after the sleep inside its placing. The swipe's
  thread round its two calls. The job that sends the list of changed
  tiles, `$80:AC55`. How a player comes to be carried.
* **Named from what they do.** I have not seen the steering thing, the
  walker, the pursuer or the swipe on a screen. The swipe reads like a
  tool cutting what grows.
* **Next.** The two at the top stay the ROM's on purpose (`$80:897F`,
  5.0%, and `$80:9512`, 2.3%). After them nothing is over 1.4%: a stretch
  of level 21's (`$81:C6F2`), a thread that waits for someone to come
  near (`$83:9A89`), two of a player's (`$80:D082`, `$80:D02D`), and a
  player's thread starting (`$80:D13A`).

### The slime's attack, a trampoline's bounces, and six small things (2026-10-06)

What the 65816 still executed over the twelve movies goes from 0.74 to
**0.62** million instructions of work. Live, `level21`, `level29-fighting`,
`level33` and `level41` each go up a tenth; the other eight do not move.
The game registers 343 routines, 11 more.

* **The slime's attack, between its sleeps**, in `port/slime.c`. A pass
  that begins an attack is one `slime_frame` turns down, because the attack
  shows two lists of pictures and sleeps inside each. So the ROM ran all of
  it, and the glob's first frame with it. Five stretches of that are here
  now:
  * `$81:CCF0`, the picture and the touch and the pass's end, for a pass
    whose state was the ROM's.
  * `$81:CBC0`, the glob's thread started.
  * `$81:CBDD`, the slime's handler put back, and off a random way.
  * `$81:CECC`, a record for the glob.
  * `$81:CF1D`, where the glob will come down.
* **A player on a trampoline**, in `port/pose.c`: three more pose handlers.
  `$80:DFDA` is a bounce, straight up and down again. `$80:E035` waits on
  it between two. `$80:E180` is the bounce off it, across the ground. No
  routine is new: `player_frame` turned those frames down for the pose, and
  now takes them. I take it for a trampoline by what the handlers do.
* **Six small things**:
  * `$81:F823`, a frame of a thing sent off one way in a straight line, in
    a new `port/bolt.c`. Level 21 has it.
  * `$83:9D2C`, a turn of a thing that steps round four places, in a new
    `port/stepper.c`.
  * `$81:D948`, a thing looking for somewhere near to go, in a new
    `port/wander.c`.
  * `$82:8163`, a job that asks for the sixteen colours' job, in
    `port/dma.c`.
  * `$82:AEB4`, the vblank job that scrolls BG1 down, and `$80:9ED0`, the
    one that sends bytes to VRAM a kilobyte a vblank, both in
    `port/vblank.c`.

* **Checked.** The corpus verifies at 33,284,636 calls across 51 movies
  with 0 diverged, and 974 of 1,165 coverage sites; 26 of the 29 new ones
  are taken. Every call priced is exact.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **Two bugs the corpus found**, after the movies I had tried passed both. The
  glob's aim read its target before the draw, and a slime that found
  nobody hands it a target whose bytes are the generator's own. And
  `wander_pick` claimed an overflow it does not follow. See
  `docs/cosim.md`.
* **Untaken.** The bolt meeting ground, and the bolt past the leash: every
  one in the corpus runs out its sixty frames or is told to stop. A spot
  off the level.
* **Still the ROM's.** Of the attack: its first stretch, as far as the
  first list of pictures, and the glob's landing. Of the trampoline: each
  landing, and the frame a wait ends on.
* **Named from what they do.** I have not seen the bolt, the stepper or
  the wanderer on a screen.
* **Next.** Two stretches are over 1.6% of the residue and both stay the
  ROM's on purpose: the between-games clear (`$80:897F`, 4.2%), and the
  one `JSR` of the wobble's thread (`$80:9512`, 1.9%). After them come a
  thread that looks at the tiles round it (`$81:E8A8` and `$81:E979`), a
  pose of level 21's (`$80:F791`), and the loop of level 21's placed thing
  (`$82:F49E`), which sleeps inside a call.

### The potion's monster, level 37's thread, a cursor and a spawner (2026-10-05)

What the 65816 still executed over the twelve movies goes from 1.00 to
**0.74** million instructions of work. Live, `level37` goes from 99.5% to
**99.7%**, and `level5`, `level9` and `level17` each go up a tenth; the
other eight do not move. The game registers 332 routines, 8 more.

* **The monster the potion makes**, in `port/pose.c`. Its state was ported
  already, and `player_frame` turned down every frame of it, because its
  walk was not: `$80:D6DC`, which is a punch of four pictures. That is
  here now, with the box in front of the fist that everything in it is
  told of (`$80:F051`) and the tile the fist is at (`$80:F0D7`). So a
  monster's frame is the port's whole. Two kinds of frame are still the
  ROM's: the first picture of a swing, which makes a sound, and a punch
  that lands on a wall that can be knocked down.
* **Walking with a weapon of the second band**, `$80:D6B8`, in the same
  file: the other walk that was missing.
* **A frame of level 37's thing**, `$82:EF4F`, in `port/seeker.c`: the
  thread's loop and five of its states. It comes at whoever is nearest,
  backs off from too near, circles them at forty-eight pixels, and stands
  facing them. A frame that goes on to anything else is turned down.
* **A cursor a pad moves about a screen**, `$82:B267`, in a new
  `port/cursor.c`: a turn of the loop that reads the pad every fourth
  frame. I take it for the password screen: the eight movies that begin
  at a later level all run it first.
* **Something started from a list**, `$81:807E`, in a new
  `port/spawner.c`: at its place, or scattered near it on clear ground.
* **Five small things**:
  * `$80:8475`, a thread naming the handler others call it by, in
    `port/thread.c`.
  * `$81:D443`, a step along a line, in a new `port/line.c`.
  * `$80:D0BF`, a list of a player's pictures shown one after another, in
    a new `port/flinch.c`.
  * `$83:9C94`, a thing of two pictures turn about, in a new
    `port/blinker.c`.
  * `$82:F40B`, a thing in level 21 kept beside another, in a new
    `port/follower.c`.

* **Checked.** The corpus verifies at 33,254,386 calls across 51 movies
  with 0 diverged, and 948 of 1,136 coverage sites; 36 of the 47 new ones
  are taken. Every call priced is exact.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **Untaken.** The cursor past each of the four edges, Start, and its
  time running out. Level 37's thing backing off, going by way of its
  question on a fourth frame, finding someone else nearest, and the end
  of its loop. A thing scattered off the level.
* **Named from what they do.** I have not seen the cursor's screen, the
  two-picture thing or level 21's thing on a screen. `flinch` is where
  the code that takes one from a player's health goes on to.
* **Next.** Nothing left is over 1.6% of the residue but the
  between-games clear that stays the ROM's (`$80:897F`, 3.4%). After it
  come a thread in level 21 (`$81:F808`), `$83:9D00`, `$80:94AF`, and the
  job that scrolls a level's name (`$82:AEB4`).

### A block put into the map, a level's name coming down, and six small things a frame (2026-10-05)

What the 65816 still executed over the twelve movies goes from 1.28 to
**1.00** million instructions of work. Live, `level37` goes from 99.4% to
**99.5%**; the other eleven do not move a tenth. The game registers 324
routines, 10 more.

* **A block put into the map**, `$80:AB5A`, in a new `port/tile_rows.c`.
  It is how a door opens and a wall comes down: eight rows of eight tiles
  copied from the library, and a transfer queued for each row the camera
  shows. The routine holds the camera and the queue still while it works,
  with bit 14 of the render flags. The port is the stretch the bit is set
  for, `$80:AB8F` to `$80:ABC9`. The ROM sets the bit and clears it, so an
  NMI in the stretch finds it set whether the port has done the rows yet
  or not.
* **Where the map's rows are**, `$80:ACA2`, in the same file: the two
  tables of row addresses a level's start makes, and the camera's limits.
* **A level's name coming down the screen**, `$82:AE6F`, in a new
  `port/card.c`: a frame of the sixteen it drops for, and a frame of the
  bounce after. Each queues the job that puts the scroll in the register.
  The job itself is still the ROM's.
* **The wait for a button after it**, `$82:BA26`, in `port/card.c`: a
  frame of up to six seconds.
* **The figure that rises**, `$81:8300`, in a new `port/riser.c`: sixty
  steps of a pixel up, the picture changing every fourth. Its count and
  its place in the pictures are on the stack across the yield, and the
  port reads and writes them there.
* **The thing thrown in an arc**, `$81:F98A`, in a new `port/lob.c`: a
  frame of its flight, with the three routines it calls.
* **Two leaves of the thing in level 37 that comes at a player**,
  `$82:E7C7` and `$82:E807`, in a new `port/seeker.c`: a pixel the way it
  was told, and the other of its two pictures every fifth call.
* **A pose's picture as a call of its own**, `$80:F300`, in `port/pose.c`.
  The ported poses already did this in C. The poses that are still the
  ROM's called the ROM's, 1,005 times over the twelve movies.

* **Checked.** The corpus verifies at 33,227,487 calls across 51 movies
  with 0 diverged, and 912 of 1,089 coverage sites; 25 of the 29 new ones
  are taken. Every call priced is exact.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes. Each of the ten runs in
  step on at least one of `level5.zmv`, `level21.zmv` and `level37.zmv`.
* **Untaken.** The queue full when the name's job is added. A button
  during the wait. The thrown thing's pictures for `$44` up and over. A
  row across the tilemap's seam after a row of the same block that had
  more of itself this side of it.
* **Named from what they do.** I have not seen the rising figure, the
  thrown thing or level 37's on a screen. The names say what the code
  does to a display record.
* **Next.** Nothing left is over 2.6% of the residue. The largest are
  the between-games clear that stays the ROM's (`$80:897F`), a screen in
  bank `$82` that reads the pads (`$82:B27D`), a spawner (`$81:807E`),
  a loop that reads a pad (`$80:D2EA`), and the rest of level 37's thread
  (`$82:E858`, `$82:EF49`).

### The big letters, the radar's square, and the logo screens between their waits (2026-10-05)

What the 65816 still executed over the twelve movies goes from 1.8 to
**1.3** million instructions of work. Live, `level21` goes from 99.4% to
**99.6%**, and `level1`, `level5`, `level9`, `level13`, `level41` and
`level49` gain a tenth. The game registers 314 routines, 19 more.

* **The big letters of a level's name**, `$82:AD5A`, in `port/text.c`.
  The ROM asks the console's multiplier where in the font each letter
  starts, which is two writes to the hardware in the middle of every
  letter. So it is two stretches, each ending where the next write would
  be, and the five instructions between them stay the ROM's. The second
  does not read the product back: A and X still hold what was multiplied.
* **The radar's thread**, `$82:D8FD`, in a new `port/radar_thread.c`: the
  frame that moves the one square to the next neighbour within `$180`
  pixels of the player. The radar going down, and the count of neighbours
  changing under it, are left to the ROM.
* **The logo screens**, `$83:8000`. It is one run of code with eight
  loops, each waiting on a `WAI`. A new `port/logo.c` has a frame of each,
  from the instruction after its `WAI` to the next. `port/frontend.c` has
  what comes before them: the tilemap made in the scratch buffer
  (`$83:802D`) and the copy of colours from the cartridge (`$83:8241`).
* **A player's portrait**, `$80:9A52`, in `port/frontend.c`: 208 tiles
  from the cartridge into the text map. Its two tables are read through
  the data bank, which is `$80` on the way into a game and `$00` on the
  way to the top scores. The first guard asked for `$80` and declined six
  of the demo movie's eight calls.
* **The game over's thread**, `$80:8A11` and `$80:8A30`, in
  `port/frontend.c`: a frame of each of its two loops. The fall they call
  was already the port's.
* **The wobble's thread**, `$80:9515`, in `port/trig.c`: the three tests
  it makes after each build of the table. The call to the build is one
  instruction and is left to the ROM. Two of the tests read the pads,
  which the NMI writes, and a build is long enough for an NMI to land in:
  a stretch that began before the build would read them too early.
* **The neighbour who jumps**, `$83:9F11` and `$83:9F2E`, in a new
  `port/jumper.c`: the twenty frames up and the twenty down.

* **Checked.** The corpus verifies at 33,182,521 calls across 51 movies
  with 0 diverged, and 887 of 1,060 coverage sites; 36 of the 38 new ones
  are taken. Every call priced is exact. `level21.zmv` runs to 20,000
  frames in the corpus now: a demo that comes round after its game puts
  the radar up, which no movie does in play.
* **Lockstep** over the corpus: 325,322 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **A correction to the round before.** It said the flame a doll leaves
  had run in step with the ROM on no movie. It has: `demo-end.zmv`, in
  the corpus's lockstep, runs all 676 of the flame's frames and 14 dolls'
  deaths, and never parts. I had looked at `level5.zmv` alone. The radar
  is in lockstep on the same movie and on `level21.zmv`, 261 frames each.
  Which demo comes round after a game still differs between `verify` and
  lockstep on some movies, and with which routines lockstep leaves to the
  ROM: `level21.zmv` plays the radar's demo with the corpus's three left
  out and another with nothing left out.
* **Untaken.** A character the big letters' table draws nothing for. A
  radar with no neighbour near enough to show.
* **Not ported, on purpose.** The third stretch of the between-games
  clear, `$80:897F`, is still 25,000 instructions of the residue. It
  stays the ROM's for the reason `port/clears.h` gives.
* **Next.** The block of tiles the camera's code puts on the screen at
  `$80:AB5A`, with `$80:AAA1` and `$80:A9F3` under it, is the largest
  piece left, 77,000. After it the residue is a long tail: nothing else
  is much over 2% of it.

### The evil dolls whole, the bubble gun's bubble, and the words and tiles of a level's start (2026-10-05)

What the 65816 still executed over the twelve movies goes from 3.0 to
**1.8** million instructions of work. Live, every one of the twelve is at
99.4% or more: `level21` and `level37` go from 98.9% to **99.4%**, and
`level5` and `level9` from 99.1% to **99.5%**. The game registers 295
routines, 34 more.

* **The axe a doll throws**, `$81:B4EA`, in a new `port/axe.c`: three
  stretches, the launch, the dress and a frame of flight. It goes four
  pixels a frame right or down and three left or up, because the second of
  each pair of sums takes the first's carry. And of the eight sets of
  four pictures its table names it is only ever given three: the ROM
  tells left from right by comparing a step with -1, after it has doubled
  the step.
* **What a destroyed doll can leave**, `$81:B664`, in a new
  `port/flame.c`: five stretches. It wanders, goes for whoever comes
  within `$30`, trails pictures that fade, and is gone after 800 frames.
  A doll that is destroyed leaves one 80 times in 256.
* **The rest of the doll's own thread**, in a new `port/doll_thread.c`:
  ten stretches round the loop that was already the port's, and its
  opening state, which was not. `$80:AA0D`, the test for being on the
  screen that nothing else calls, is in one of them.
* **The bubble gun's bubble**, `$81:F380`, in a new `port/bubble.c`: six
  stretches. The martians of level 21 fire it, and players do in other
  movies. Its burst means to put the water's picture back first and
  writes it over the picture's bank instead: both stores are to the same
  word. The port writes what the ROM writes.
* **Three more vblank jobs**, in `port/dma.c`: the HUD's rows sent from
  their shadow (`$80:C34A`), sixteen colours from the 112th on
  (`$82:8308`, on level 21), and VRAM zeroed a kilobyte a vblank
  (`$80:9F62`).
* **What a level's start copies**, in a new `port/loads.c`: the tile
  attributes (`$80:AD92`), the background's colours to both their copies
  (`$80:A037`), the sprites' (`$80:A05B`), and the HUD's reset
  (`$80:C2F7`).
* **The text printer**, in a new `port/text.c`: one string at one place
  (`$82:B84A`), and strings each with its own place (`$82:B8FB`), which
  runs to where the ROM queues the map for VRAM and waits. Every screen of
  words goes through one or the other.
* **A whole screen of tiles**, `$80:A4D9`, in a new
  `port/screen_tiles.c`: the 31 rows and the column a level starts with,
  copied to a buffer. It stops at `$80:A4FA`. The thirty or so
  instructions after it fill the vblank's queue and are left to the ROM,
  so the queue is filled when the ROM fills it. The loop they end in
  waits for the vblank to empty the queue, and is in `cosim/waits.h` now
  with the others of its kind: 48,204 instructions of the twelve movies
  that were counted as work and are not.

* **Checked.** The corpus verifies at 31,564,811 calls across 51 movies
  with 0 diverged, and 851 of 1,022 coverage sites; 74 of the 81 new ones
  are taken. Every call priced is exact.
* **Lockstep** over the corpus: 310,022 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **What lockstep does not reach on `level5.zmv`.** A doll's death and
  what it leaves are verified call by call: 15 deaths across the corpus,
  14 of them and all 676 of the flames' frames on `level5.zmv`, after its
  game has ended and the demos have come round. In lockstep the same
  movie plays other demos after its game ends. This said that the flame
  had run in step with the ROM on no movie, which was wrong:
  `demo-end.zmv` does it. See the section above.
* **Untaken.** An axe that hits a second thing, and a frame of one whose
  mask is ORed in. A bubble fired from a tile that stops it. A flame that
  lives its 800 frames, one with both places for a trail in use, and one
  whose two legs are both nothing. A character under `$2F` in a string.
* **Next.** Outside a level still: the wave's thread at `$80:94AF`, the
  intro at `$83:8000`, `$82:AD5A`, and the portraits' copy at `$80:9A52`.
  In one: the radar's thread at `$82:D92A` on level 21, the camera's
  `$80:AB4E` and `$80:AAA1`, and the monster's punch at `$80:D6DC`. The 2
  cycles in `apu_send` are still not understood.

### The screens outside a level, the squirt gun's whole thread, and two more of the player's states (2026-10-05)

What the 65816 still executed over the twelve movies goes from 4.3 to
**3.0** million instructions of work. Live, `level5` goes from 98.8% to
**99.1%**, `level37` from 98.6% to **98.9%** and `level41` from 98.9% to
**99.2%**. `level9`, `level13` and `level21` gain two tenths, and
`level29-fighting` and `level49` one. The game registers 261 routines.

* **Jobs of the screens outside a level**, in `port/frontend.c`. The
  backdrop behind a screen of words slides a pixel every fourth frame
  (`$82:B1F9`). Behind the two players' portraits the first layer moves
  every frame and the third every fifth (`$80:9A1B`). The game over sends
  its third layer's height and three colours each frame (`$80:8B70`,
  `$80:8B82`), and its four sprites fall a pixel, two on an odd frame
  (`$80:8A58`).
* **A list of pictures**, `$81:832C`, in `port/bodies.c`. Ninety places
  call it: a list of pairs, a picture and the ticks to leave it up. It
  sleeps between them, so it is two stretches, from the call and from each
  wake.
* **The squirt gun's whole thread**, in `port/squirt.c`. Only the flight
  loop was the port's. Now the launch is too, with the record's dressing,
  the first two frames, both pictures of the splash and the end: seven
  more stretches. The sound and the record's allocation between them are
  calls to routines already ported.
* **Two more of the player's states**, in `port/player_frame.c`. The
  potion's monster (`$80:D2EA`) has no weapon: the pad turns it, and is
  kept when a button to punch with is down. A player who flashes and
  cannot be hurt (`$80:D404`) runs the ordinary state underneath, tells
  everything within eight pixels, and flips its picture on and off.
* **Two more walks**, in `port/walk.c`, beside the first and sharing its
  questions. The monster's (`$80:E595`) tests the ground as an enemy's is
  tested. A player stuck in slime (`$80:E6C2`) can cross two kinds of
  solid ground, and what covers them moves with them.
* **What the game forgets**, in a new `port/clears.c`. `$80:895A` clears
  three stretches of WRAM when a game or a demo ends, and `$80:8992` the
  threads' pages. The first two stretches and the threads' are the
  port's.

* **Checked.** The corpus verifies at 31,503,267 calls across 51 movies
  with 0 diverged, and 777 of 941 coverage sites; 29 of the 32 new ones
  are taken. Every call priced is exact. Three movies now run to 20,000
  frames, as the live profiles do: `level5`, `level13` and `level41`.
  That is where the corpus meets the monster, the slime walk and a game's
  end.
* **Lockstep** over the corpus: 310,022 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **What lockstep caught.** The third stretch of `$80:895A` was ported
  too, and every call of it verified. In lockstep `demo-end.zmv` came out
  with `$7E:0024` one ahead, and `level5.zmv` went on to a different game
  at pass 18,475. That stretch holds `$7E:1EB4`, which the NMI's handler
  reads: set, it holds the random numbers still. The ROM clears the
  stretch a byte at a time and reaches that word nine tenths of the way
  through. The port cleared it at once, so an NMI that landed in the
  stretch stepped the numbers where the ROM's did not. Without that
  stretch both movies are clean, and it is the ROM's again. `verify`
  could not have seen it: it sets the handler's work aside.
* **The registry's ceiling** was 256 routines and is 384.
* **Untaken.** A squirt fired from a tile that stops water; a wall that
  breaks under the monster, which is the ROM's anyway; solid ground a
  stuck player can cross.
* **Next.** The monster's punch is the pose at `$80:D6DC`, and with the
  second band's walk at `$80:D6B8` it is what the player's frame still
  turns down most. The creature from `$81:B4EA` to `$81:B8DC` is some
  300,000 instructions on levels 5, 9, 37 and 49. Outside a level:
  the first screen's tile map at `$80:A462`, the text printer at
  `$82:B84A`, the wave's thread at `$80:94AF`, and the portraits' copy at
  `$80:9A52`. The 2 cycles in `apu_send` are still not understood.

### What the ports were turning down: a guard, five handlers, a won level and a leap (2026-10-05)

What the 65816 still executed over the twelve movies goes from 5.0 to
**4.3** million instructions of work. Live, `level5` goes from 98.5% to
**98.8%** and `level37` from 98.3% to **98.6%**. `level9`, `level13`,
`level17`, `level21` and `level29-fighting` gain a tenth each, and the
other five are unchanged. The game registers 242 routines.

Most of this round is not new routines. It is calls that routines already
ported were handing back to the ROM, found by asking each why.

* **A guard that asked for a table nobody reads.** The player's pose shows
  a hand weapon from a table their page points at. The guard for the
  poses, and the one for the player's whole frame, wanted that pointer to
  be in the cartridge. It is zero whenever the weapon held has no picture
  in the hand, and then nothing reads it: only a pose with a hand weapon
  out does. On `demo-end.zmv` that guard was turning down 1,872 of 7,067
  walking frames. It asks only when a hand weapon is out now, and turns
  down 2.
* **A level that is won goes on.** The frame left to the ROM any frame with
  no neighbour left to rescue. The ROM's own test has a second half: with
  nobody left and somebody rescued it returns, and the player walks to the
  door. Only nobody left and nobody rescued ends anything. On
  `level21-exit.zmv` that was 292 frames of 2,317. The frame has both
  halves now, and turns down 5.
* **Five collision handlers.** A handler the port does not have costs more
  than its own few instructions: the sprite pass that reached it is handed
  back whole. `exit_door_collide` at `$82:F958` is the door a finished
  level opens. `actor_f25d_collide` and `actor_f8fc_collide` are named for
  their addresses, in bank `$81`: one counts hits, the other remembers
  which of four ids touched it. On the player's side, id `$35` at
  `$80:F999` and the door's id `$37` at `$80:FAF0`. On `demo-end.zmv` the
  sprite pass was handed back 39 times in 23,580 and is not at all now.
* **In the air.** Two of the player's eight states share a handler in which
  the pad only turns them, `$80:D343`, and the frame has it. Under it run
  three poses, in `port/pose.c`: `pose_arc_ready`, the picture before a
  leap, and `pose_arc`, a frame through the air, which the ROM has twice
  with two landings. The height takes a speed that gravity slows, and
  across the ground the player moves by fractions of a step. The landings
  are the ROM's.
* **The text layer's map**, `src/port/textmap.c`. `text_map_clear` at
  `$82:AD44` blanks the 32 rows of tiles the words on a screen are written
  on. It is a quarter of a frame of `MVN`, and its callers wait for the
  vertical blank first, so no interrupt lands in it.

* **Checked.** The corpus verifies at 26,745,836 calls across 51 movies
  with 0 diverged, and 738 of 909 coverage sites; 14 of the 19 new ones
  are taken. Every call priced is exact, among them 123,039 of the
  player's frame, 891 of the three poses in the air and 261 of the clear.
* **Lockstep** over the corpus: 264,122 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes.
* **What the corpus does not reach.** `actor_f25d_collide` is only in the
  fourth demo, a factory, which starts after frame 26,000 of
  `demo-end.zmv`. Run to 36,000 with the sound entries left out, that
  movie verifies at 3,443,811 calls with 0 diverged, and the handler on
  all 65 of its calls. The fifth untaken site is the player touched by id
  `$35` in a state that ignores it.
* **Why the fourth demo is not in the corpus.** Its sound upload fails
  `verify`, and that is not this round's code. From the sixteenth upload
  of the movie, `apu_send`'s third register write is 2 master cycles
  later in the ROM than the port's trace says: `$2143` at cycle 2,116
  against 2,114. Over 44,000 frames five calls of 118,713 are out on
  price, by 2. The fifteen uploads before it are exact. Nothing is known
  yet about why.
* **A wrong guess, taken out.** The first theory for the poses was the
  data bank, which `$80:895A` leaves at `$00` after a demo. A column for
  table bytes read through a slow bank was written and priced before
  anything was measured. Measured, the player's thread has bank `$80` on
  all 9,681 of its frames on `demo-end.zmv`. The column is gone.
* **Next.** On the player: the pose at `$80:DCA2` and the movement it
  uses, `$80:E543`, which with `$80:E595` is the `$80:E500` row on levels
  5 and 37; the walk at `$80:D6B8` for the second band of weapons; the
  state at `$80:D2EA`. The fishman on land. The 2 cycles in `apu_send`, which is
  what keeps the fourth demo out. Outside a level, as before: the clears
  `$80:895A` and `$80:8992`, the first screen's tile map at `$80:A462`,
  and the text printer at `$82:B898`.

### Four creatures of the demos: a chainsaw maniac, a fishman, a werewolf and the footballers (2026-10-05)

What the 65816 still executed over the twelve movies goes from 6.4 to
**5.0** million instructions of work. Live, `level13` goes from 98.3% to
**98.9%**, `level41` from 98.1% to **98.9%** and `level21` from 98.3% to
**98.6%**. The other nine are unchanged. The game registers 235 routines.

* **Where the creatures were.** The live runs go on 15,000 frames past each
  movie's input, and much of that is the title and the demos it plays when
  it is left alone. `level13` and `level41` end up watching the same demos,
  which is why their residue in these rows was the same to the instruction.
  So the biggest rows left under a creature were not levels 13 and 41 at
  all. They were the demos, and `demo-end.zmv` reaches them if it is run
  for long enough: it runs to 26,000 frames in the corpus now, from 9,000,
  through a graveyard and a football field. Ten level movies have one or
  another of the first three as well: the fishman on the seven level-29
  movies and the chainsaw maniac on five of them, the fishman on
  `level37-e6e4`, and the werewolf on `level49-bubble` and
  `level49-corner`. Only the demo has the footballers.
* **The chainsaw maniac**, `src/port/chainsaw.c`, thread `$81:983A`. He
  wanders, chases whoever is nearest two steps a pass, and when he is
  stopped beside a tile that can be cut he cuts through. `chainsaw_frame`
  is a pass of his loop from `$81:9878`, in the four states he gets about
  in. The cutting, his swing and being hit sleep in the middle and are the
  ROM's.
* **The fishmen**, `src/port/fishman.c`, threads `$81:E481` and `$81:E51A`.
  It may only be where all six tiles under it are water, and it leaps out
  at somebody on a draw. One kind comes ashore and the other patrols its
  pool, with a loop each over the same state bodies: `fishman_frame` and
  `fishman_patrol_frame`, nine states between them. A pass that decides to
  leap sleeps first, and is the ROM's.
* **The werewolves**, `src/port/werewolf.c`, thread `$81:ABF5`. One runs at
  whoever is nearest, strikes when they are beside it, and pounces from a
  distance. `werewolf_frame` is the running, the passes of a strike and the
  flight of a pounce.
* **The footballers of level 21**, `src/port/football.c`, thread `$81:C87B`.
  This is the creature the last round could not reach: no level-21 movie
  has one inside its checked frames, and the demo does. They run four
  pixels a pass in straight lines, veer on a draw, and go at a player who
  is beside them. `footballer_frame` is the running. The tackle is the
  ROM's.
* **Named after a look.** The first three were ported from what they do: a
  creature that cuts hedges, one that swims and one that pounces. Each was
  then found on the screen of `demo-end.zmv` at the place its thread's page
  says it is: the chainsaw maniac at frame 11,400, a fishman ashore at
  11,640 and a werewolf at 12,800. The files are named for them.
* **A footprint with another rule.** The fishman carries its own two copies
  of the six-tile test in `port/terrain.h`: all six water, and all six
  somewhere to land, the second with nine taken off both ways where the
  first takes nine and eight. `terrain_footprint_read` hands back the six
  attribute words and the two overflows, and the fishman asks its own
  question of them.

* **Checked.** The corpus verifies at 26,735,956 calls across 51 movies
  with 0 diverged, and 727 of 890 coverage sites; 61 of the 67 new ones are
  taken. `verify --level` over all 56 records, 39,050,551 calls, 0
  diverged. There the chainsaw maniac is on two records, the fishman that
  comes ashore on six and the one that patrols on eight, the werewolf on
  eleven and the footballers on two.
* **Prices.** Every call priced is exact, on the corpus and on the sweep.
  On the corpus: 3,489 passes of the chainsaw maniac, 641 and 4,047 of the two
  fishmen, 3,553 of the werewolf and 3,577 of the footballers, 445 of
  those under HDMA, where a price is only held not to be too high. All four
  ports verified on their first build, and three were exact on it.
* **The one that was not.** The fishman's price was out on 14 passes of
  1,236, by 614 or 294 cycles. `fishman.c` had a `#define` of the same name
  as an enumerator in its header, a distance in one and a path in the
  other. The port compared against the right number and logged it as the
  path, so the bill took a pass that had looked and found somebody as one
  that had asked after the players. The game was right throughout: only
  the price said anything.
* **What no movie reaches.** Six of the new sites. The chainsaw maniac dying
  was verified on an asserted state, `--poke 11200:052A=FFFF` on
  `demo-end.zmv`: 202 passes, all agreed and priced exactly. The other
  five are unchecked: the chainsaw maniac and the werewolf on the same spot as
  their quarry, the werewolf landing beside whoever it pounced at, and a
  fishman's leap refused for a spot off the level or with somebody on it.
* **Lockstep** over the corpus: 264,122 passes, 48 of 51 never part, the
  same three level-25 movies on the same passes. `demo-end` is clean over
  its 25,999. No other row moved by a cycle.
* **What is left of them.** On the sweep the fishman that comes ashore
  spends over half its passes on land, in states the port does not have:
  5,899 of its 12,633 passes were taken. The chainsaw maniac's cutting, the
  fishman's bite and the werewolf's crouch and landing each sleep inside a
  call, and want the second entry the saucer's hatch has.
* **Next.** The fishman on land (`$81:D9B8`, `$81:D9E9`, `$81:DA1D`). On
  level 21, the martians' shot at `$81:F380` and whatever is at
  `$82:D9xx`, which no port has looked at. On levels 5 and 37, `$80:BA00`
  to `$80:BFFF`, `$80:E500` and `$81:B500` to `$81:B8FF`. Outside a level,
  as before: the clears `$80:895A` and `$80:8992`, the title's logo at
  `$80:94AF`, and `$80:A462`.

### The flying saucer, and the two screens before a game (2026-10-04)

What the 65816 still executed over the twelve movies goes from 7.6 to
**6.4** million instructions of work. Live, `level21` goes from 97.2% to
**98.3%**, and `level5`, `level13`, `level33`, `level37` and `level41` by 0.1
each, to 98.5%, 98.3%, 99.4%, 98.3% and 98.1%. The other six are unchanged.
The game registers 230 routines.

* **The flying saucer**, `src/port/saucer.c`. The big figure of level 21 is
  the saucer of "The Day the Earth Ran Away": a thread at `$82:873C` with
  four states, drawn as a background. It keeps a spot 116 to one side of
  itself and flies to put the spot on whoever is nearest. Close, it shoots.
  Closer, it opens a hatch and hangs there, and the hatch is the only part
  of it that can be hit. Far off, it swoops. `saucer_frame` is one pass of
  its loop, from `$82:87B1` to the next yield. It was 0.8 million of the
  7.6, in the rows `$82:84CE`, `$82:87E8`, `$82:8337`, `$82:873C` and
  `$82:839C`.
* **A pass that sleeps in the middle.** The routine that shows the hatch
  calls `thread_yield` itself, two calls below the loop. A pass that gets
  there ends there, with two return addresses on the thread's stack, and
  the thread wakes at `$82:83E3`. So the stretch leaves with the stack four
  bytes deeper than it found it, and the shim writes those four as the
  ROM's `PEA` and `JSR` do. A second entry, `saucer_frame_shown`, takes the
  thread from where it wakes: it checks the two addresses are the ones it
  knows, pulls them, and runs the rest of the pass. No creature's frame
  before this one left the stack deeper than it found it.
* **The two screens before a game**, `src/port/menus.c`. `$80:96B9` and
  `$80:98EA` are not level loaders, which is what the last round called
  them. They are the title's menu and the screen where each player presses
  Start to join: loops in the main thread that sleep a tick a pass, for
  fifteen seconds at most. The live runs sit in them after a game is lost.
  `title_menu_frame` and `players_screen_frame` are a pass of each. A pass
  on which a press counts makes a sound, and those are the ROM's: one or
  two a movie.
* **The data bank is `$00` the second time.** The first version of the two
  frames took the screens at the start of every movie and none of the
  visits after, which are all the live runs' tails are. `$80:895A`, the
  clear a lost game ends with, is three `MVN`s, and the last leaves the
  data bank at `$00`. The title then runs with it. The menu's table is the
  same cartridge bytes through either bank and 8 cycles dearer through
  `$00`, and the price says so. `demo-end.zmv` runs to 9,000 frames in the
  corpus now, from 6,400, which is far enough to come round to the title
  again.

* **Checked.** The corpus verifies at 24,932,816 calls across 51 movies
  with 0 diverged, and 656 of 823 coverage sites; 29 of the 34 new ones are
  taken. `verify --level` over all 56 records, 38,969,187 calls, 0
  diverged. The saucer is on two records there, and the two screens on all
  56.
* **Prices.** Every call priced is exact, on the corpus and on the sweep:
  13,374 and 9,093 passes of the saucer, 3,638 and 1,479 rests of a pass,
  12,382 and 448 passes of the title's menu, 11,208 and 6,776 of the
  players' screen.
* **What only a poke reaches.** No movie shoots the saucer, and
  `a84ac_took`, its hit handler taking a hit, is untaken on the corpus too.
  The flash of a hit, the saucer shot down and the spot changing sides were
  verified on asserted states: on `level21.zmv`, `--poke 3000:0732=0004`
  for a hit, `4000:0730=FFFF` for no health and `4590+:0728=0000` for the
  line its quarry crosses. All passes agree and are priced exactly. The
  fifth untaken site, a move down off the level, is taken on the sweep.
* **Lockstep** over the corpus: 48 of 51 never part, the same three
  level-25 movies as before on the same passes. The eight movies with a
  saucer end within 26 cycles of where they did, and no other row moved
  but `demo-end`'s, which is longer.
* **The pictures that differ are a frame or two late, on three of four.** The last round
  left `level21`'s final frame differing from the stock core's and did not
  look. Four of nine movies differ at frame 20,000 now, `level9`, `level13`,
  `level21` and `level37`, and all four did before this round: the last
  round's build gives the same four pictures. Under lockstep to 20,000
  frames none of the four ever parts, with no live byte different and VRAM,
  CGRAM, OAM and the scroll equal on every pass. `level9`'s frame 20,000 is
  the stock core's frame 19,999, and `level13`'s is its 19,998. `level37`'s
  is both of those, which are the same picture. `level21`'s was not found:
  it is no stock frame from 19,940 to 20,060, so for that one the offset is
  an inference and not a measurement. Each is past a lost game, on a screen
  whose backdrop drifts, and lockstep leaves `lzss_decompress` and the
  camera to the ROM because a load comes back on a frame boundary that their
  cycles decide. So the game is the same game some frames apart after a
  load. `level1`, `level5`, `level33`, `level41` and `level49` end on the
  stock core's own frame.
* **What the round taught.** A lockstep run made by hand, with two of the
  three exclusions `verify_corpus.ps1` gives, parted on `demo-end` at pass
  4,688 with the new frames in, and read as their fault until the game's
  own run of the movie came out the same length to the cycle. And a share
  that does not move says more than a `verify` that passes: both screens
  verified on every movie and the residue still held their loops, because
  an entry whose `accepts` says no is not a call, and is counted nowhere.
* **Next.** Level 21 has one more creature, a thread at `$81:C87B` that
  walks in from the side of the screen (`$81:C824`, `$81:C51F`). No movie
  reaches it inside its checked frames: it comes after the input ends, so
  a movie comes before the port. Then the creatures of levels 13 and 41 at
  `$81:DC24`, `$81:9107` and `$81:A74D`. Outside a level: the title's logo
  at `$80:94AF`, which builds a wave a line at a time with `$80:9C8C`; the
  clears `$80:895A` and `$80:8992`, which are long enough for an interrupt
  to land in; and `$80:A462`, which draws a screenful of a level's map.

### The instructions between the ports: returns, calls, and the NMI's hardware (2026-10-04)

What the 65816 still executed over the twelve movies goes from 19.5 to
**7.6** million instructions of work, and none of it is the frame's any
more: the NMI, the scheduler and the two dispatchers have left the ranking.
Live, `level1` goes from 98.2% to **99.7%**, `level17` from 98.3% to 99.6%,
`level53` from 97.7% to 99.6%, `level49` from 97.3% to 99.4%, `level33` from
96.5% to 99.3%, `level29-fighting` from 97.5% to 99.2%, `level9` from 97.0%
to 98.8%, `level5` from 96.7% to 98.4%, `level13` from 96.6% to 98.2%,
`level37` from 96.5% to 98.2%, `level41` from 96.2% to 98.0% and `level21`
from 95.5% to 97.2%. The game registers 226 routines.

* **The harness makes the instruction a port leaves by.** A port ends
  standing on one instruction of the routine's own: its `RTS` or `RTL`, or
  the `JSL thread_yield` a native frame ends on, or the `RTL` the scheduler
  resumes a thread by. The core used to execute that one. Ranked by address,
  those single instructions were a third of the residue: 1.2 million `RTL`s
  out of the scheduler, 1.1 million out of the dispatchers into a job, every
  native call's own return. `leave` in `cosim.c` makes seven of them now,
  `RTS`, `RTL`, `JSR abs`, `JSL`, `JMP abs`, `RTI` and `WAI`, with the stack
  and the cycles the 65816 gives each, and an interrupt polled where the
  core polls it. This alone took the residue from 19.5 to 12.8 million. No
  port changed for it, and `verify` is not touched by it: it is `run`'s.
* **The NMI handler's hardware instructions**, `src/port/sched.c`. The
  handler was four ported stretches split round `LDA $4210`, two
  `STA $2100`s and the wait on `$4212`, and those and the trampoline at the
  vector were twenty instructions a frame, 5.0 of the 12.8 million. Three
  new stretches take them in: `nmi_vector` from the vector at `$00:816C` to
  the handler's own stack, `nmi_queue_a` for the `JSR` into the first
  dispatcher, and `nmi_unblank` for the brightness and the joypad wait.
  `nmi_leave` goes on through the handler's `RTL` and the trampoline's `PLB`
  to the `RTI`. A frame's interrupt now runs with no instruction of the
  core's in it.
* **Two more kinds of step in a hardware trace**, `port/hw.h`. A read that
  is made for what reading does, which is how `$4210` acknowledges the
  interrupt, and `LDA : LSR : BCS` on a register until its bit 0 is clear.
  `verify` compares each with the ROM's by address and cycle, as it does the
  writes.

* **Checked.** The corpus verifies at 24,705,495 calls across 51 movies with
  0 diverged, and 627 of 789 coverage sites; the 3 new ones are all taken.
  `verify --level` over all 56 records, 38,951,391 calls, 0 diverged. The
  four NMI stretches are exact on every call priced, 256,035 of the vector
  and of the unblank on the corpus, with every register access on the ROM's
  cycle.
* **Lockstep** over the corpus is what checks `leave`, and it is as it was:
  48 of 51 never part, the same three part on the same passes, and each
  movie's final drift and worst drift are the same to the cycle. The last
  frame of
  `level1`, `level5`, `level33`, `level49` and `level1-map` is the stock
  core's, byte for byte. `level21`'s is not, and was not before the round:
  this build's is the same picture as the last one's.
* **What the share now leaves out.** The wait on `$4212` is a wait, and is
  counted with the other waits and not as work, as `nmi_unblank` reports it.
  It was work while the ROM ran it. That takes about 0.06% off the
  denominator and nothing off the numerator.
* **What the round taught.** The ranking by routine hid this for rounds. It
  charged an `RTL` at `$80:8397` to `sched_rescan` and showed a ported
  routine with work against it, which reads as a guard declining. Ranked by
  address and opcode, a third of the residue was `RTL`, `RTS`, `JSL` and
  `JSR`, each executed alone between two ports.
* **Next.** What is left is a long tail with nothing over 4%: the big
  figure of level 21 (`$82:84CE`, `$82:87E8`), the level loaders
  (`$80:98EA`, `$80:96B9`, `$80:895A`, `$80:A462`), and the creatures at
  `$81:DC24`, `$81:9107` and `$81:A74D`. In the player, the live runs go on
  for 15,000 frames past the end of each movie's input, and there the frame
  turns down 12,678 of 158,609 passes, a third of `level5`'s and of
  `level37`'s. States `$02` (`$80:D2EA`) and `$0A` (`$80:D404`) are 3,172
  and 1,936 of those, with a movement at `$80:E555` beside the first. No
  movie in the corpus stays in either, so a movie comes before the port.
  On the corpus's own frames the frame turns down little: state `$06` 192
  times on `level5`, a movement at `$80:E6C2` 840 times on `level41`.

### The player's frame in readable C, and exact prices under it (2026-10-04)

A player's whole frame is one native call now, and everything it calls is
priced by what it did. Live, `level33` goes from 95.8% to **96.5%**, `level49` from 96.8% to 97.3%, `level9` from 96.6% to 97.0%, `level17` from 97.9% to 98.3%, `level53` from 97.3% to 97.7%, `level1` from 97.9% to 98.2%, `level29-fighting` from 97.2% to 97.5%, and `level5`, `level13`, `level21`, `level37` and `level41` by 0.2 each, to 96.7%, 96.6%, 95.5%, 96.5% and 96.2%. The residue over the twelve movies goes from
21.6 to **19.5** million instructions of work, and the game registers
223 routines.

* **The player's frame**, `src/port/player_frame.c`. From where the thread's
  sleep comes back at `$80:CDFE` to the `JSL` that is the next: the state,
  the hurt timer, the pose, the movement, the position, the buttons kept, and
  the two tests for the level's end and the player's. Each of the seven was
  already C; between them the core still ran thirteen or fourteen
  instructions a frame, a `JSR` here and an `RTS` there, and over the twelve
  movies that was 2.2 million, a tenth of what was left. The frame calls them
  as C functions. It serves 112,591 of the corpus's 125,189 player frames, nine in ten, and the ROM runs the rest through the old entries.
* **The player's shot**, in `src/port/pose.c`. `$80:ED30` takes a round of
  the weapon held, in decimal, and starts the shot's thread with where the
  player is and faces. The pose handlers used to leave every frame that
  fired to the ROM, which was most of what the frame turned down on its first
  run. Weapon 5's shot goes on to a pose that sleeps, and that stays the
  ROM's.
* **Exact prices for eight callees.** `terrain_blocked`, `step_propose`,
  `step_tether_blocked`, `actor_publish_pos`, `tilemap_tile_addr`,
  `tile_attrs_at_pixel`, `floor_effect` and `player_state_normal` were each
  charged a mean. The tether test costs 322 cycles alone and was charged
  519; the ground test was charged as if it never found a wall early. Each
  now reports what the call did and is priced from it.
* **The two "MODEL WRONG" flags are gone.** `player_walk` was exact on 105
  of 96,444 calls and `monster_chase` on 20,345 of 22,659, since the rounds
  that wrote them. The chase was only the tile read's mean, 20 cycles off.
  The walk had the tests' means and a mistake of its own that they hid: its
  table counted an `RTL` after each call, which the tests' prices count too.

* **Checked.** The corpus verifies at 23,937,390 calls across 51 movies
  with 0 diverged, and 624 of 786 coverage sites. Of the 8 new sites 7 are taken; no movie fires a weapon with no rounds left from a pose (`pose_fire_empty`).
  `verify --level` over all 56 records, 37,778,079 calls, 0 diverged.
  The frame serves 231,498 of the sweep's 263,587 player frames.
* **Prices.** Every call priced is exact, on the corpus and on the sweep: 112,591 and 231,498 frames of the player, 96,444 and 186,503 walks, 22,659 and 24,102 chases, and on the corpus 197,245 ground tests, 175,046 tether tests, 101,194 steps, 125,178 positions, 121,278 floors and 119,864 ordinary states.
* **Lockstep** over the corpus: 48 of 51 never part, where it was 47. `level25-lane` used to part at pass 6057 and now runs all 9,399. `level25`, `level25-2p` and `level25-heavy` part where they did. `level25-2p` shows five bytes the tool cannot account for, where it showed four, and `level25-item` its one, as before.
* **What the round taught.** A mean hides more than its own error. While the
  walk's tests were charged means, a wrong run in the walk's own table could
  not be told from them, and stayed from the day the walk was written. A guard that runs the
  port on a copy must not touch anything outside the copy: the ordinary
  state takes a frontend's request for the next weapon from a variable of
  its own, so the frame's guard turns the frame down while one is waiting.
* **Next.** Player state `$0A` (`$80:D404`), which runs the ordinary state
  and then calls `actor_notify_box` round the player; it is most of what the
  frame still turns down. Then the big figure of level 21 (`$82:84CE`,
  `$82:87E8`, `$82:873C`), the creatures at `$81:DC24`, `$81:9107`,
  `$81:A74D` and `$81:E1A3`, and the level loaders at `$80:98EA` and
  `$80:96B9`.

### The DMA jobs, the colour animations, the demo and the spawn list (2026-10-04)

Fourteen more entries in readable C, most of them things that run every
frame of every level. Live, `level33` goes from 94.1% to **95.8%**, `level5` from 95.5% to 96.5%, `level13` from 95.4% to 96.4%, `level37` from 95.3% to 96.3%, `level9` from 95.7% to 96.6%, `level41` from 95.1% to 96.0%, `level21` from 94.5% to 95.3%, `level29-fighting` from 96.4% to 97.2%, `level49` from 96.5% to 96.8%, `level53` from 97.0% to 97.3%, `level1` from 97.7% to 97.9% and `level17` from 97.8% to 97.9%. The residue over the twelve movies goes from
25.2 to **21.6** million instructions of work, and the game registers
222 routines.

* **DMA and its jobs**, `src/port/dma.c`. `dma_to_cgram` and `dma_to_vram`,
  the two palette jobs at `$80:A084` and `$80:A09E`, and the animated tiles'
  job at `$82:D88C`, which gives each tile that changed its frame's
  attributes and sends its picture. They record their register writes in
  `port/hw.h`'s trace, as the scroll jobs do. 1.45 million instructions.
* **The colour animations**, `src/port/palcycle.c`. Five threads that levels
  list: three that turn a run of colours round, one that pulses a colour's
  green, and one that steps the big figure's row through ten arrangements.
  One waking of each. 0.6 million.
* **The demo's playback**, `src/port/demo.c`. The vblank job at `$80:9CB2`
  that feeds a recorded pad to the game while the demo runs, and ends it on
  any real button. 0.6 million, on the six movies that sit through a demo.
* **Two jobs of the screens outside a level**, `src/port/frontend.c`: the
  logo screens' job at `$83:8255` and the backdrop's drift at `$80:953B`.
  0.4 million.
* **The spawn list**, `src/port/spawnlist.c`. One frame of the thread that
  brings a level's creatures in, `$81:810F`: ask for room, then count a
  resting place down, measure a ready one, or at the end of the list set the
  nearest resting and leave at the `JSR` that starts it. `bodies.c` had it as
  three stretches with two calls between them; they stay registered for the
  frames this declines.

* **Checked.** The corpus verifies at 23,819,697 calls across 50 movies
  with 0 diverged, and 617 of 778 coverage sites. All 25 new sites are taken. The corpus is 51 movies now: `movies/demo-end.zmv` sits through the title into the demo and ends it with a button, and is the only one in which the demo's job runs. `palcycle_turn_seven` is on one level record and no movie; the sweep checks it.
  `verify --level` over all 56 records, 37,537,913 calls, 0 diverged.
  None of the new entries declines a call on any record. The sweep is what found a guard too tight: the spawn list's declined all 5,299 frames of record 53, whose list is empty, because the thread never clears the field for the nearest place and the guard read it every frame. It reads it now only once something has been measured near enough.
* **Prices.** Every call priced is exact, on the corpus and on the sweep: 124,741 and 295,434 frames of the spawn list, 41,438 and 66,310 calls of `dma_to_cgram`, 4,322 and 11,905 of the tile job, 13,499 and 22,470 of the pulse, and so on down. The demo's job is 1,653 on the corpus and, before the movie was added, 41,039 over seven runs of 20,000 frames.
* **Lockstep** over the corpus: 47 of 51 never part, the same four level-25 movies as before. `level25-2p` runs 617 passes further than it did, to pass 1838. At pass 1221, where it used to part, it now shows four bytes the tool cannot account for, on that pass only: the count of queue A and one slot of it, with a HUD upload queued on one side and already run on the other, and one word of VRAM with them. With `spawnlist_frame` left to the ROM it parts at 1221 as before, so this is the same event and the frame's timing decides which way it shows. It is not explained further than that. `level25-item` has its one byte, as before.
* **What the round taught.** A job that writes hardware is no harder than one
  that does not: all six traced entries passed `verify` on the first build,
  writes compared by address, value and cycle. The pads' latch is already in
  `CosimRegs`, for the NMI, and a job can read it. And a frame can price a
  callee the registry charges a mean for by reading what it was given after
  the fact: `nearest_player_dist`'s players have not moved when the frame
  ends.
* **Next.** The frame's own instructions are a third of what is left, and
  most of them touch the hardware or are single calls between native
  stretches. Then the big figure of level 21 (`$82:84CE`, `$82:87E8`,
  `$82:873C`), the creatures at `$81:DC24`, `$81:9107`, `$81:A74D` and
  `$81:E1A3`, and the level loaders at `$80:98EA` and `$80:96B9`.

### A colour fade, a player stuck in slime, and two small ones (2026-10-04)

Four more pieces in readable C and one spin taken out of the count.
Live, `level41` goes from 94.5% to **95.1%**, `level21` from 94.0% to **94.5%**, `level13` from 95.0% to 95.4%, `level49` from 96.1% to 96.5%, `level29-fighting` from 96.1% to 96.4% and `level9` from 95.6% to 95.7%. The other six are unchanged. The residue over the twelve movies goes from 27.3 to **25.2**
million instructions of work. 0.6 million of that drop is the wait, which
was never work.

* **The colour fade**, `src/port/palfade.c`. A thread levels list,
  `$82:AB95`, that moves a level's colours to another set: a row of sixteen
  of the background and of the sprites each time it wakes, each channel one
  step nearer, sleeping a tick less each time. One entry, `palfade_frame`.
  It was `$82:AAB7`, `$82:AB19` and `$82:AB5B`, 0.57 million instructions.
* **A player stuck in slime**, `src/port/stuck.c`. The player's state `$0C`:
  390 frames, or three shakes left and right, and hurt again every `$B0`.
  One entry, `stuck`, from where the state's `JSR floor_effect` comes back.
  The row was filed under `$80:D343`, 0.43 million. The frame it ends on is
  the ROM's.
* **The big figure's colours**, `src/port/figure_colours.c`. `$82:8138`
  sets the background's eighth row, which is the palette of a boss drawn as
  a background. 0.33 million on level 21.
* **The bystander**, `src/port/bystander.c`. A level-49 thread that waits
  for a player to walk up to it, `$82:DD52`. One entry, `bystander_frame`,
  0.2 million.
* **`$80:9AB0` was a wait.** 613,311 instructions over six calls, all but 78
  a call of them `LDA $136C : AND #$0080 : BEQ`, a level fading out with the
  CPU held against a vblank job. It is in `src/cosim/waits.h` now, with its
  two twins.

* **Checked.** The corpus verifies at 22,945,464 calls across 50 movies
  with 0 diverged, and 591 of 753 coverage sites. Twelve of the fourteen new ones are taken. No movie shakes free of a slime, and none has the bystander alone on screen, so `stuck_shook` and `bystander_one_drawn` are unchecked.
  `verify --level` over all 56 records, 37,016,157 calls, 0 diverged.
  The fade is on 19 records, the figure's colours on 8, the stuck state on 4 and the bystander on 1.
* **Prices.** Every call priced is exact, on the corpus and on the sweep: 1,863 and 2,248 wakings of the fade, 1,352 and 5,103 stuck frames, 2,757 and 1,937 sets of the figure's colours, 2,630 and 1,740 passes of the bystander.
* **Lockstep** over the corpus: 46 of 50 never part, the same four level-25 movies as before. Mean drift on the six level-21 movies fell by about 30 cycles each, and no other row moved by more than a cycle.
* **Three things the tools taught.** `cycles816.py` takes `--db=7E` and
  ignores `--db 7E`, which showed as a fade priced 4 cycles a colour short.
  A row in the residue is a span, not a routine: `$80:D343` was `$80:D465`,
  and `$80:D4F4` is the `RTS`s the core runs for the ported poses. And
  `src/port/fade.c` already existed, the screen's fade-in, which is why the
  new file is `palfade.c`.
* **Next.** The big figure of level 21, a thread at `$82:873C` with four
  states, 0.5 million. Its picture's routine sleeps inside the call on about
  one pass in four. Then `$81:DC24`, `$81:9107`, `$81:A74D` and `$81:E1A3`
  on levels 13 and 41, and `$81:C51F` and `$81:C824` on level 21.

### The spiders in readable C (2026-10-03)

The red spiders of level 17, `src/port/spider.c`. `$83:B3E9`, `$83:B2D0`,
`$83:B50D` and five more rows, 1.2 million instructions and over a third of
that level's residue. Live, level 17 goes from 96.5% to **97.8%**. With the
martians below, the residue over the twelve movies goes from 29.9 to **27.3**
million instructions of work.

* **It wanders in a straight line** until the ground or somebody stops it,
  turns a quarter clockwise, and then follows the wall it met, as a slime
  does.
* **It looks every pass.** Anyone under `$B4` away and it takes aim. Nobody
  within `$F0`, and neither player either, and it leaves.
* **Taking aim** it faces its target, or one turn of eight wide of them, and
  runs for up to seven passes before it aims again. Both are one draw.
* **Running, it steps once or twice a pass**, by a draw each pass, three
  pixels an axis, and slides along what it runs into.

One entry, `spider_frame`, a whole pass of the thread's loop. Nothing in a
pass sleeps, so there is no pass it has to refuse: 0 declined.

* **Checked.** The corpus verifies at 22,936,862 calls across 50 movies with 0
  diverged, and 579 of 739 coverage sites: all nine new ones are taken.
  `verify --level` over all 56 records, 37,005,129 calls, 0 diverged, with
  spiders on records 17 and 30.
* **Prices.** Every pass is exact: 2,555 on the corpus, 806 on the sweep, and
  7,885 on 12,000 frames of `level17`.
* **Lockstep** over the corpus: 46 of 50 never part, and no row of the table
  moved from the martians' round.
* **What it is** was settled by a picture: a copy of the ROM with the
  creature's sixteen pictures made one, and the two screenshots compared.
* **Next.** `$82:AAB7` on four levels, the martian's shot at `$81:F380`, and
  the level's opening, `$82:8138` and `$82:84CE`. Then `dma_to_cgram` and
  `dma_to_vram`, which write the hardware.

### The martians in readable C (2026-10-03)

The martians of level 21, `src/port/martian.c`. They were the largest
cluster left anywhere: `$81:9C99`, `$81:9981`, `$81:9D2A` and three more
rows, a quarter of that level's residue. Live, level 21 goes from 92.1% to
**94.0%**. The residue over the twelve movies goes from 29.9 to **28.6**
million instructions of work.

* **It shoots along rows and columns.** Every pass it asks whether anything
  is within a tile of its row or its column, and fires that way unless it
  fired in the last sixty passes.
* **Walking, it keeps its distance.** Once in sixty passes it looks for
  whoever is nearest. Under `$3C` away it backs off. Under `$50` it lines up
  with them, along whichever axis it is nearer on. Under `$E0` it comes
  closer. Beyond that it stands, and leaves if neither player is near.
* **Arriving, it stays above them.** One that comes in over the top keeps
  between `$60` and `$78` above its target, going across, and climbs or drops
  on a slant at two steps a pass to get there. It fires downwards at random.
  When a player is above it, it becomes a walker.
* **A step** is two pixels on each axis, three passes in four, each axis
  tested by itself against the ground, the level's edges and whoever is
  there.

Two entries, `martian_frame` and `martian_arrival_frame`, one for each of the
thread's two loops. Both are whole frames and both run the same C.

**A direction nobody chose.** The thread's setup does not clear the
direction. A martian whose first look finds everything too far away returns
without choosing one, and for sixty passes steps by whatever the tables hold
far past their ends. The first guard refused those passes, 46% of one level
record's. The port now reads the same words from the cartridge, and they
verify like any other.

**`actor_aligned` is priced by its path.** It was charged its mean, 4,576
cycles, for a call that takes between 758 and 7,386. It now counts how many
of the 32 slots it dismissed at each test. It also says what overflow it
leaves, which a martian's frame hands on to its thread on the pass in four
that it rests.

* **Checked.** The corpus verifies at 22,934,307 calls across 50 movies with 0
  diverged, and 570 of 730 coverage sites: all thirteen new ones are taken.
  `verify --level` over all 56 records reaches the martians on three, 18,287
  passes with 0 diverged.
* **Prices.** Every pass of both entries and every call of `actor_aligned` is
  exact, on the corpus and on the sweep.
* **Lockstep** over the corpus: 46 of 50 movies never part, the same four of
  level 25 as before, at the same passes. On the six movies of level 21 the
  mean drift fell from between 3,676 and 10,737 cycles to between 612 and
  2,167.
* **Not here:** the shot. It sleeps twelve ticks inside the call that fires
  it, so the pass that fires and the pass that wakes from it are the ROM's,
  about one in a hundred. The death is the ROM's too.
* **Next** were the spiders of level 17, above.

### The slimes in readable C (2026-10-03)

The red blobs of levels 9, 29 and 41, `src/port/slime.c`. Their code was the
biggest thing left on those levels that was not the frame's own: `$81:CD33`,
`$81:C94E`, `$81:CAA6` and six more rows, 19% of the three levels' residue.
Live, level 9 goes from 94.5% to **95.6%**, level 29 from 95.6% to **96.1%**
and level 41 from 93.5% to **94.5%**. The residue over the twelve movies goes
from 31.7 to **29.9** million instructions of work.

* **It moves in lunges.** The thread keeps where the lunge will end. The
  record on screen stays where it began, and five pictures stretch the body
  from one to the other. A lunge is 14 pixels up or down and 24 across.
* **It turns when it cannot go on**, a quarter clockwise, and then feels its
  way: each pass it tries the turn back first, so it follows the wall it met.
* **Between lunges it thinks.** About three times in ten it turns to face
  whoever is nearest, one in five it thinks of attacking, and with neither
  player within `$140` it leaves the level.
* **It attacks less than it thinks of it.** A second draw has to come in under
  `$23`, about one in seven. Otherwise it sets off a random way.
* **It hurts by touch**, a box around its body told to `actor_notify_box`
  once a pass.
* **The glob it throws** goes straight up, is moved across at the top to above
  where it was aimed, and falls. That is a second entry, `slime_glob_frame`.

Both are whole frames, one native call from where `thread_yield` returns to
the next yield.

**A pass that only running it can refuse.** Whether a pass begins an attack
is down to a random draw inside it, and the attack sleeps in the middle of a
call, which a whole frame cannot do. So the guard runs the pass on the
harness's scratch copy and declines if an attack began. The touch reaching a
collision handler the port lacks is declined the same way. About one pass in
a hundred goes back to the ROM.

* **Checked.** The corpus verifies at 22,905,377 calls across 50 movies with 0
  diverged, and 557 of 717 coverage sites: all sixteen new ones are taken.
  `verify --level` over all 56 records reaches the slimes on six, 15,243
  passes with 0 diverged.
* **Prices.** Every pass of both entries is exact, on the corpus and on the
  sweep.
* **Lockstep** over the corpus: 46 of 50 movies never part, the same four of
  level 25 as before, at the same passes. Drift moved on no movie.
* **Not here:** the attack itself, the splash the glob lands with, and the
  slime's death. Each plays an animation that sleeps inside `$81:832C`.
* **Next.** The martians of level 21 are the largest cluster left, about a
  quarter of that level's residue. They ask `actor_aligned` every frame,
  which has no per-call price and no overflow in its port, and both are
  needed first.

### The logos come up at once (2026-10-03)

A launch that keeps the intro sat on a black window for three seconds before
Konami. Those are 187 frames the game spends with the screen off, clearing
memory and sending the sound driver. They now run at full speed, about a third
of a second, and the window's first picture is the logo fading in. Nothing is
patched and nothing is pressed: it is `skip_intro`'s loop with `keep_logos`,
stopping at the first lit frame. A movie and a `--frames` run boot as before,
because both count their frames from reset.

### The dolls, the clones, whole frames and the player's poses (2026-10-03)

Six new pieces in readable C, and the zombies made whole. Over the same
twelve movies as the last round every level gains: level 1 goes from 96.5% to
**97.7%**, level 5 from 91.1% to **95.5%**, level 49 from 92.5% to **96.1%**,
and the residue from 45.6 to **31.7** million instructions of work.

**The evil dolls**, `src/port/doll.c`. `$81:AEA6` was the biggest row left
that was not the frame's own, and nothing said what it was: its collision
handler has been `enemy_b41c_collide` since July. A copy of the cartridge with
its pictures swapped for a zombie's settled it. On level 49 the doll with the
axe turned into a zombie.

* **It climbs out of a toy box**, a twenty-frame jump down onto the floor.
  Only on landing does it install its collision handler.
* **It closes in** on whoever is nearest within `$D0`, a pixel every fourth
  frame, the smaller gap first, which lines it up.
* **Within 16 pixels it swings its axe**, a second display record shown beside
  it on the frames it swings.
* **Lined up, it charges** two pixels a frame until about 16 short.
* **It throws**: exactly on a diagonal, and on one frame in 256 otherwise, with
  36 frames between throws.
* **A hit knocks it back** in an arc, the opposite way to its facing, unless
  solid ground or the edge of the level is there. It throws again on landing.

**The clones**, `src/port/clone.c`, found the same way: on level 5, swapping
two picture tables turned everything that looked like the player into a
zombie. A clone alternates between two modes for random stretches. In one it
moves as its player moves, reading the same pad word the player's thread
reads. In the other it comes for the nearer player. A random stretch of zero
frames is 65,536, because the count is decremented before it is tested.

**The player's poses**, `src/port/pose.c`: standing, walking, and walking with
a hand weapon out. A handler waits for the buttons to change or the pose
timer to run out, and then shows the next picture. Firing stays the ROM's, and
`pose_supported` says which frames those are.

**Whole frames.** A zombie's frame was three native calls with the thread's own
loop interpreted between them. Now it is one: `zombie_frame` runs the
decision, the state body and the animation, from where `thread_yield` returns
to the next `JSL` to it. All three zombie threads have one. So do the dolls,
the clones, the squirt gun's water in flight (`src/port/squirt.c`) and the
level's main loop (`src/port/mainloop.c`), which was the pause check, the HUD
refresh and two questions. The harness needed two things for it, both in
`CosimRoutine`'s comments: a routine that leaves by an exit may now claim less
than every register, and may declare dead stack under the calls it makes.

**Two small ones.** The pause check, `$80:89B0`, which does nothing on a frame
nobody is pausing on (`src/port/pause.c`), and `camera_scroll`, the thunk at
`$80:A937` that calls `camera_follow` four times a frame.

**Carry and overflow** outlive every one of these bodies, through
`thread_yield`'s `PHP`. `src/port/flags.h` holds that bookkeeping now, so the
game code reads as arithmetic: `plus`, `minus`, `at_least`.

**Prices.** Nine routines are priced per call for the first time: `rng`,
`actor_gap`, `player_in_range`, `player_bearing`, `thread_spawn`,
`terrain_out_of_bounds`, `terrain_point_bit2`, `actor_snap_to` and
`actor_bearing`. Through them every zombie, doll, clone,
pose and shot entry is exact on every call, the zombies' chases and decisions
included, which the last round left a few hundred cycles off. Getting there
found the fast chase's stuck path charged one `JMP` twice.

* **Checked.** The corpus verifies at 22,896,500 calls across 50 movies with 0
  diverged, and 541 of 701 coverage sites. `verify --level` over all 56
  records gives 36,970,793 calls with 0 diverged. That sweep is what reaches the
  dolls' knock-back, which no corpus movie does.
* **Lockstep** over the corpus: 46 of 50 movies never part, as before, and the four that do are
  level 25's. On the rest, drift over a movie moved by at most 1,058 cycles
  against the last round.
* **Level 25's partings moved, and not because of the new code.** With the
  prices exact, `level25-item.zmv` runs to the end, `level25-lane.zmv` parts
  at pass 6,058, and `level25-2p.zmv` parts at pass 1,221 instead of 1,838.
  Each does the same with the new entries left to the ROM. It is the prices'
  few cycles, on a level already known to part on timing: at each parting one
  side overran vblank on a heavy pass and the other did not, with no byte of
  game state differing before it.
* **Not reached by any input:** a doll knocked back with no facing, and a
  charge begun inside 16 pixels. Both look unreachable: nothing leaves the
  facing unset, and a doll that close swings instead. A knock-back at the edge
  of the level is reachable and unreached.
* **Still not readable.** `collide.c`, `bodies.c`, `sched.c` and `player.c`
  are the transliterations they were. Rewriting one means finding, routine by
  routine, which registers its callers really read. The whole-frame entries
  are the way round that for anything a thread's loop calls: inside a frame
  only what reaches the yield matters.

### The zombies in readable C (2026-10-03)

The zombies are the third piece of the port in readable C: `src/port/zombie.c`,
about 420 lines, which serves ten routines. They were the biggest thing left in
the live residue. Over twelve corpus movies profiled under `zamn --profile`,
their rows came to about 20% of it: `$81:8A5C` at 5.0%, `$81:85EB` at 4.6%,
`$85D3`, `$8B30`, `$89FD`, `$8736` and `$8706`.

There are two kinds. The slow kind is `$81:87F8`, every enemy on level 1. The
fast kind is `$81:88CA`, and `$81:8C17` with four hits of health and its own
frames. Each frame a zombie decides, runs one of three state bodies, and
animates.

* **It walks straight on** a pixel a step, or two, until solid ground stops it.
  It waits for someone in the way rather than turning.
* **At a wall it turns and follows it.** Each step it tries the heading it
  turned from first, which rounds corners, and turns again when blocked. The
  slow kind turns a quarter clockwise. The fast kind turns by `$2C`, which
  nothing writes: `thread_spawn` copies five words into a new thread's page
  and leaves the rest as the last thread left it. With a multiple of 16 there
  it does not turn at all, and stands at the wall until something comes near.
  On twelve movies `$81:8A72` ran 55,422 times without a step.
* **It chases what comes near**: `$41` for the slow kind, `$A0` for the fast.
  It snaps into line, asks the bearing and steps two pixels. The slow kind
  takes two one-pixel steps. The fast kind takes one that slides along a wall.
  It gives up at `$46` or `$B4`, or when stuck, and walks off on a random
  heading.
* **With neither player within reach it leaves.** The reach is read from the
  cartridge, so widescreen's wider reach still holds.

The two kinds are the same code at two addresses, apart from the turn and the
chase, so one file serves both through a table of what differs. The state
bodies are entered by the thread's computed `RTS`, like the chase.

* **Carry and overflow outlive a state body.** The animation leaves overflow
  alone and carry three frames in four, and `thread_yield`'s `PHP` parks both.
  Every path ends on a test or a turn, so the shims claim C and V from those.
  `actor_at_point` now reports the overflow its window test leaves, as
  `actor_obstacle_at_point` already did, and the two share the helper.
* **Checked.** The corpus verifies at 22,398,922 calls across 50 movies with 0
  diverged, 118,061 of them zombie calls. `verify --level` over all 56 records
  gives 35,874,842 calls with 0 diverged, 217,510 of them zombie calls, and
  both kinds on every routine. 12 of the 13 new coverage sites are
  taken. The one that is not is the fast kind's `$24` counting down past
  32,768. Only a chase resets it, so that takes nine to eighteen minutes of a
  fast zombie that never gives chase. It then stops looking for anyone, and
  stops leaving, for as long again.
* **Cost models.** The walks, the wall-follows and the animations are exact to
  the refresh on every call. That took pricing `terrain_blocked_enemy` per call
  for the first time, by how many of its six probes ran, and it is exact on all
  197,170 of its own calls. The chases and decisions are off by a few hundred
  cycles, because `actor_snap_to`, `actor_bearing` and `player_bearing` are
  still priced at their means.
* **The chase's price is closer too.** `$80:9D39` is registered as `rng`, not
  `rng_next`, and the chase looked up the wrong name, so it charged nothing for
  the generator. It also prices its ground tests per call now. On
  `level24-carry.zmv` its error went from -197..+649 cycles to +183..+471,
  which is about the refresh a call that long collects.
* **Lockstep** over the corpus is the same with the zombies as without them:
  the same four level-25 partings at the same passes and the same two video
  differences. The drift moved on 17 movies, by at most 288 cycles over a movie.
* **Live**, on the same twelve movies, level 1 goes from **92.7% to 96.5%**.
  Every level with zombies gains 1.4 to 3.8 points, and the rest are
  unchanged. The residue goes from 58.8 to 45.6 million instructions of work.
  `zamn.exe` served 504,941 zombie calls and declined none.
* **What is left of them** is the threads' own loops: the yield, the three
  `JSR`s and the computed `RTS`, about 2.1 million instructions over the twelve
  movies.

### The monster's chase in readable C (2026-09-26)

The monster's chase, `$81:BEE3`, is the second piece of the port in readable
C: `src/port/chase.c`, about 250 lines. It was the biggest row of game logic
left in the residue, at 2.5%. Like the walk, it is reached by a computed
`RTS` and was long thought unportable for that reason.

Each frame it finds the nearest of the actors `actor_nearest` knows about.
With nothing within `$B4` it gives up and wanders off in a random straight
line. Otherwise it steps towards the target. If the target lies straight up,
down, left or right, it steps that way. On a diagonal it first closes the
smaller gap, which lines it up. On about half its frames the step is two
pixels. Someone standing in the way makes it wait. Solid ground makes it
look for a tile to leap. The leap is its own state and is declined to the
ROM.

* **Only V can outlive it, and only after giving up.** The thread follows
  the chase with `monster_seek` or `monster_deliver`, and both set N, Z and C
  first. `monster_seek`'s scan sets V too whenever it matches someone, which
  after a step it always does. So the shim claims no register on a step and
  the random number generator's V on a give-up.
* **Checked.** `verify -r monster_chase`: 22,659 calls over the 11 corpus
  movies that reach it, and 24,102 over all 56 level records, with 0
  diverged and 15 declined. The six levels with the monster are the only
  ones that reach it. Every branch is taken, the leap 16 times.
* **The cost model is close but not exact.** The per-call residual is +1.9
  to +103 cycles against a chase of about 16,000. Three of its callees are
  priced at their registry means.
* **Lockstep** on the 11 movies matches the round before, with one
  exception. `level25-item` no longer parts at pass 1,612, so for the first
  time passes past it are compared, and on passes 1,624 to 1,626 one byte
  differs: `$7E:0A1A`, field `$1A` of another thread's page, by 2. With only
  the chase substituted the same movie is clean through those passes, so
  this is timing that the parting used to hide, not the chase's logic. It is
  not explained yet.
* **zamn.exe runs it.** On `level45-carried.zmv` the game served 3,687
  chases natively and declined one leap.

**`level21-exit.zmv` no longer hangs under lockstep.** After its level ends
the game sits in `$80:89D3`, polling both pads for a Start the movie never
presses. Neither core came back to the scheduler's `WAI`, and each of the
2,900 remaining passes cost 1,000 frames on each core to find that out again.
A pass that does not come back now ends the run. When both cores are stuck
it says the game left the scheduler. When only one is, on timelines still
together, that is a failure. The movie finishes in 81 seconds, clean over
its 4,088 passes.

**`tools/verify_corpus.ps1` runs twelve movies at a time** (`-Jobs`). The
whole lockstep pass takes 201 seconds, against 486 at four at a time. Both
figures are with `run` and `verify` no longer drawing the picture: the PPU's new `noPixels` skips the pixel loop, which writes
only the frame buffer. On one movie that took lockstep from 63 seconds to 26
and `verify` from 48 to 35, and every row of the corpus came out identical to
the runs before it, digit for digit.

### The walk in readable C (2026-09-26)

The player's walk, `$80:E4BA`, is the first piece of the port in readable C:
`src/port/walk.c`, about 170 lines. It asks where the pad takes the player,
then tries across and then up or down, and each axis is refused by solid
ground, the other player's leash, someone standing there unless someone is
standing where the player is too, or the map's edge. It calls
`step_propose` and the four tests as C functions, so none of the ROM runs
between `$E4BA` and its `RTS`. It replaces the fifteen transliterated
stretches from the round before, which are gone from `port/bodies.c`.

What it still owes the emulator is kept out of the game logic. Only carry
and overflow outlive the call, because `thread_yield`'s `PHP` parks them in
the thread's status byte. Small adapters around each test record them, and
what was asked, in a `WalkLog`. The harness prices the call from that log.
Two paths are declined to the ROM: the double step, which no input takes,
and solid tiles with a reaction of their own, such as doors.

* **Checked.** `verify -r player_walk` passes every call it checks: 95,378
  across the corpus and 186,503 across all 56 level records, with 459 and
  2,323 declined. Lockstep over the corpus never parts where it did not
  before, on all 49 movies that finish. The four level-25 partings are
  lag-frame splits as before, and `level25-2p`'s moved from pass 1,263 to
  1,838. `level21-exit.zmv` still hangs.
* **Overflow was not clear, as the first version assumed.** Levels 19 and 25
  failed on half their walks. On those big maps the tilemap address
  `terrain_blocked` adds up crosses `$8000` and sets V. The terrain,
  tether, obstacle and proposal ports now report V from their last add, the
  way they already reported N, Z and C. The old stretches had the same
  error unseen, because the tests never published V and the corpus never
  reaches those maps.
* **The cost model is not exact.** It is the tests' own registry budgets
  plus the walk's instructions, priced exactly, and three of those budgets
  are means. Per call it is off by -1,941 to +2,067 cycles against the ROM.
  The lockstep clocks moved from last round's by at most 10,348 cycles
  over a movie, about 0.03 of a frame.
* **zamn.exe runs it.** On `level1-2p.zmv` the game served all 4,087 walks
  natively.

### The player's frame and the walk (2026-09-26)

Step five of the Devil's Crush path: the callable tail, ranked from play. The
top two rows of the live residue were one thing. `$80:CDF4` is the player's
frame, and `$80:E4BA` is the movement handler it calls through `$2A` on
ordinary ground. Both are ported now as stretches between their calls, in
`src/port/bodies.c`, with the four small routines the frame calls and the
tile reaction the walk calls. See `docs/threads.md`, "The player's frame".

`$80:CDF4` was filed as the level's main body. It is the player's. It runs
once per player, calls `actor_publish_pos` and reads that player's health at
`$1CB8`. Each pass it yields a tick and makes seven calls: the state machine,
the hit recovery count, the state handler at `$28`, the movement handler at
`$2A`, the position, and two checks, neighbours left and health left. Between
them it does almost nothing.

`$80:E4BA` asks `step_propose` where the player wants to be and then tries
each axis against four routines that were already ported: solid ground, the
other player's leash, an actor standing there, the map's edge. The first to
say no stops that axis. `$80:E739`, what solid ground does, keeps six
attribute values with reactions of their own and says blocked to the rest.

The rare paths go back to the ROM from inside a stretch: an event request in
`$50`, the level's end, a player's death, the six tile reactions, and the
double step `$54` asks for. The registry is 184 entries.

* **Checked.** The corpus verifies at 23,084,444 calls across 50 movies with 0
  diverged, and `verify --level` over all 56 records at 37,228,038. Every new
  cost model is exact to the refresh on every call. Lockstep with the 24 left
  to the ROM and with them substituted ends on the same cycle on all 49
  movies that finish, with the same four level-25 partings and the same two
  video differences. So the new routines add no drift at all. 18 of the 20
  new coverage sites are taken. The two that are not are `$6A` holding the
  recovery count and the double step. `zamn_test_layers` is unchanged on six
  movies in 16:9 and 21:9.
* **Live, over all 56 records, 92.3% becomes 93.1%**, and every record gains
  0.5 to 1.2 points. Without the vblank jobs' DMA it is **86.5%**, from 85.7%.
* **The residue** goes from 60.3 to 51.8 million instructions of work. By
  family: callable routines 85.0%, thread bodies 7.4%, vblank jobs 4.5%, the
  frame 3.1%. The top rows now are the NMI's own instructions,
  `$81:BEE3 monster_chase` at 2.5% and `$81:8A5C` at 2.4%.
* **What a stretch still costs the ROM** is its exits: the calls and returns
  between stretches. The frame's are three a pass at `$CDF7`. They are most of
  what the residue still charges to the new rows, and they are the price of
  never having one routine hold another's stack.
* **`level21-exit.zmv` still hangs under lockstep**, in both configurations.
  Stopped at 40 minutes.

### The radar shows every neighbour at once (2026-09-26)

Asked for in play-testing: the radar's squares flash, which smoothing makes
worse. The console has one sprite for all of them. `$82:D8DB` moves its
marker to the next neighbour in reach every second tick, so with five in
reach each is lit two ticks in ten. `src/radar.h` draws a square for every
neighbour the thread's loop would stop on, from the frame hook, into parked
OAM entries, with the marker's entry as the pattern, and parks the marker.
Nothing of the game's is written. `radar = flashing`, `--flashing-radar` or
the launcher's Radar row gives the console's.

The first version lost every square one tick in ten. On a frame the game
sends no OAM, the last frame's entries are still there, and a square whose
entry the game's marker had since landed on, exactly, was taken for one of
this code's and put back, marker and all. What was written is now put back
only when the whole of OAM is as the last frame left it. `zamn_test_radar` on
`level1-map`, in 4:3 and in 16:9: the machines identical, five squares on
all 320 ticks the console shows its marker, one of them on the marker, and
no pixel changed away from them.

Then, in play: squares still flashed on level 1, but only for neighbours
close together, such as the griller and the tourists, or the two
cheerleaders. The first square went into the marker's own entry, just parked,
and the pass's owner table still gave that entry to the marker's record. The
smoothing moves a record's pieces by the record's move, so that square slid
along the marker's hop. A hop between two far neighbours is taken for a
placement and nothing slid; between two close ones it was taken for a move.
The squares now start after the marker's entry. `zamn_test_layers` never saw
it, because in 4:3 it did not install the frame hook the frontend always
installs. Now it does, and it fails on a square eased from more than a
pixel: 58 on `level1-map` before, in either aspect, and none after. It is OK
on `level1`, `level1-2p`, `level25-lane` and `level9-weapons` in 4:3 and 16:9.

And a neighbour standing where a square fell was drawn over it. On the
console a sprite hides every sprite in a later entry, and the squares are in
entries after the game's own. Moving entries would have broken the tables
kept per entry: the pass's owners, the widened picture's places and the
margins. So the PPU has a flag per entry, `objFront`. `ppu_evaluateSprites`
looks for the marked entries first on each line, and the draw list draws
them last in their priority and lets them hide the others (`layers_ahead`).
`zamn_test_radar` draws the frame again with the squares alone and with no
sprites, and checks the squares' pixels are theirs in the picture. On
`level1-map`, with the box's middle poked over the player: 128 squares
overlapped him, with 896 pixels covered on 28 ticks before and none after.
`zamn_test_layers` is exact on the same poke.

### The tourists in bonus room 50 turn again under the neighbour cheat (2026-09-25)

Reported in play-testing, with a quick save: in the bonus room off level 22
an older couple could not be rescued. They are the tourists, and the room's
victim list puts them at (22,653), in a pocket no walk reaches even with
every door open (`zamn_assets route` and `keys`). Without cheats the moon
comes up and they turn into two werewolves that walk out. The neighbour
cheat takes out the tourists' `LDA $1F94 : BNE` at `$83:A012`, so they stayed
where nobody could touch them. Touching them still rescues them.

Turning does cost a neighbour, which is why the patch exists: the turn at
`$83:A086` retires the entry and counts down the neighbours left, as a death
does, and never reaches `$80:C81F`, the call that puts a rescue on the saved
list. So the patch stays everywhere but this room. `cheats_tick` takes it out
while the loaded record is room 50's, read from `$7E:0C10`, where `$80:8871
STA $10` leaves it on the game thread's page. It matches the ROM's own
record table entry and holds from one load to the next on every movie
watched. `zamn_test_cheats` checks the switch and the record's address. On a
copy of the save, with every cheat on, the couple turn at the same frame as
with none.

### The sound uploads, and a port that waits on the SPC700 (2026-09-25)

Step four of the Devil's Crush path. `apu_send`, `apu_load_set` and
`apu_boot`, with the IPL upload inside it, are substituted now. They were a
third of what the 65816 still ran. See `docs/cosim.md`, "The sound uploads".

A port's hardware trace can now wait: `hw_wait8` records a `CMP : BNE` loop
by its register and the value that ends it, and the burn makes the reads on
the ROM's cycles with the core's own read, so the SPC700 answers as it did
for the ROM. `verify` logs the ROM's reads of the APU's ports as well as its
writes and holds every one to the cycle. A routine marked
`through_interrupts` is not abandoned when the NMI lands in it: the handler
is set aside, its cycles and accesses taken out, and the WRAM it changed left
out of the diff. The registry is 160 entries.

* **Checked.** The corpus verifies at 21,045,696 calls across 50 movies with 0
  diverged, and `verify --level` over all 56 records at 33,074,432. Every
  sound model is exact on every call: 1,209,912 `apu_send`, 256 set uploads,
  50 boots. Lockstep matches the same harness with the three left to the ROM
  on 43 of 49 movies to the cycle. The other six end 10 to 256 cycles apart,
  with nothing unexplained and no new parting. `zamn_test_layers` is
  unchanged on six movies in 16:9 and 21:9.
* **Two things had to be exact for that, and were not.** The NMI now lands at
  the ROM's stack depth, because the pointer it saves is copied into a
  thread's page at boot (`hw_stack`). And a burn in these routines now takes
  an interrupt where the core polls for it, just before an instruction's last
  bus cycle, not at the end of a 12-cycle slice (`CosimInsn`). A command
  stored a few cycles late is seen a spin late, and on `level1.zmv` every
  call after the 16,265th ended a spin early or late.
* **`apu_boot` is not quite exact under `run`.** Alone on `boot.zmv` it ends 56
  cycles apart over the 64 NMIs that land in it, and the SPC700 can round
  that up to one pass of its polling loop, about 2,500 cycles. Why the 56
  has not been found. It is the six movies above.
* **Lockstep budgeted a pass in steps, and now in cycles.** Stock ran out of
  its 4,000,000 steps on a level load where native did not, and every level
  movie read as parted at its first load on cores that agreed to the cycle.
  The loads both sides used to run out on are compared now.
* **Live, over all 56 records, 88.1% becomes 92.3%**, and every record gains
  2.6 to 6.5 points. Without the vblank jobs' DMA it is **85.7%**, from 81.5%.
* **The residue** goes from 107.6 to 60.3 million instructions of work, and
  the sound uploads leave it. By family: callable routines 83.8%, thread
  bodies 9.7%, vblank jobs 3.8%, the frame 2.7%. The top rows are
  `$80:E4BA` at 8.1%, `$80:CDF4` at 5.4% and the NMI's own instructions.
* **`level21-exit.zmv` still hangs under lockstep.** Stopped after 25 minutes.
  The step budget was not the cause.

### The vblank jobs, and ports that write the PPU (2026-09-25)

Step three of the Devil's Crush path, second half. A port can now write the
hardware registers, and six vblank jobs are ported with it, in
`src/port/vblank.c`: `vram_queue_flush`, `sprite_upload_flush`,
`bg2_scroll_job`, `camera_scroll_job`, `scroll_shadow_job` and
`boss_bg_dma`. See `docs/cosim.md`, "Routines that write the hardware".

A port records its register writes in order, between the runs of the ROM's
instructions it went through (`src/port/hw.h`). The harness prices the runs
and makes each write on the ROM's cycle with the core's own CPU write, so a
DMA starts where the ROM's did. `verify` compares every write the ROM made,
address, value and cycle. The core counts the cycles the CPU does not spend,
refresh and DMA, so these models are held to the CPU's own clock and are
exact to the cycle. Lockstep now compares VRAM, CGRAM, OAM and the scroll
too. The registry is 158 entries.

* **Checked.** The corpus verifies at 20,937,412 calls across 50 movies with 0
  diverged. `verify --level` over all 56 records gives 33,063,209 calls with
  0 diverged. Every register write matches the ROM's. Lockstep matches the
  run with the six left to the ROM on 49 movies to the cycle, video included.
  `zamn_test_layers` is unchanged on six movies in 16:9 and 21:9.
* **The live figure counts the jobs' DMA.** Over all 56 records the game
  prints **88.1%**, from 80.1%, and every record gains between 4.9 and 11.6
  points. But 6.6 points of that is DMA the jobs start, which now runs inside
  their budgets. Without it the figure is **81.5%**. The game's report says
  both. A DMA is the CPU stopped for another device, like the waits, and
  taking it out of both sides is the principled fix. It is not done.
* **The residue** goes from 124.8 to 107.6 million instructions. Vblank jobs
  go from 14.3% of it to 2.1%. What is left, by family: callable routines
  57.6%, the SPC700 uploads 33.3%, thread bodies 5.4%, vblank jobs 2.1%, the
  frame 1.5%.
* **The widescreen hears the OAM send at `$80:B947`**, the job's entry. With
  the job substituted nothing executes `$80:B99B`.
* **Two video differences are older than this.** `level25-2p` differs in a VRAM
  word at pass 1,231 and `level25-lane` in BG1's scroll at pass 3,496, with the
  six jobs left to the ROM as well.
* **`level21-exit.zmv` hangs under lockstep again**, with the last commit's
  build too. Last round it ran to the end. Not looked into.
* **`level1.zmv`'s frame-step check** skips video frames at 735 and 895, in
  the level load, on the last commit's build too. From frame 1,000 it passes
  in 4:3 and 16:9.

### Margin sprites from the pass on screen, and why level 25 parts (2026-09-25)

**The margins' sprites are drawn from the memory the sprite pass on screen
was built from.** They used to be drawn from a copy of work RAM taken at the
top of the picture. On a tick the game runs long, that copy lands part way
through the tick, so a thing in the margin could stand a step from where the
console drew its neighbours for one frame. A key in the margin showed it as
a one-frame jump of a pixel or two. How far depended on how long the tick had
taken, so stock and native disagreed there too.

Now the widescreen copies work RAM when `sprite_build_oam` returns, at
`$80:BDE2`, and promotes that copy when the NMI's job sends OAM, at
`$80:B99B`. Both are the game's own events. A new harness call,
`cosim_watch`, reports them the same way whether the ROM ran the routine or
the port did. Eight sends with no pass behind them fall back to the old copy,
so nothing can freeze the margins. A quick load forgets both copies. They are
not in the save file, so existing quick saves still load.

* **Checked.** The stock-against-native picture compare in widescreen:
  `level25-lane` frames 5,000 to 5,600 goes from 3 differing frames to 0, in
  16:9 and in 21:9. `level25-heavy` frames 2,600 to 4,400 goes from 6 to 3 in 16:9 and from 8 to
  4 in 21:9. What is left there is the top few rows, and OAM is identical in
  both runs on every frame. On `level25-heavy` the port's pictures did not
  change at all: stock's moved to meet them. Levels 1, 21 and 45 still match
  exactly. The fallback fired only during boot and level loads, where there
  is no pass. `zamn_test_layers` gives output identical to the
  last commit's on `level1-keys`, `level1-rescue`, `level21-spin`,
  `level45-bonus`, `level25-heavy` and `level25-lane`, in 16:9 and 21:9.
  `level1.zmv`'s frame-step check passes in 4:3 and 16:9.

**Level 25 still parts between stock and native, and that is being left.**
`level25-boss.zmv` plays the same until frame 3,126 and parts at 3,129, with
widescreen off too. The game state is never wrong, and with nothing
substituted the two runs match through 4,400 frames. What parts them is the
cycle budgets: 54 routines still burn a measured mean, HDMA realigns as if
every access were 12 cycles, and an NMI during a budget is taken at a
12-cycle slice rather than at an instruction's end. The last two cannot be
made exact while routines are substituted into the emulator, and all three go
away when the game runs outside it. Until then, a picture compare on level 25
is expected to part, and to show a few rows of tearing at different lines
first. See `docs/cosim.md`, "Why native and stock part on level 25".

### Four thread bodies and the reset's WRAM clear (2026-09-25)

Step three of the Devil's Crush path, first half. Four thread bodies are
ported as stretches that leave by their own exits, in `src/port/bodies.c`:
the victims' starter at `$81:81F6`, the objects at `$80:C8F6`, the actor list
at `$81:80EC` and the animated tiles at `$82:D7CF`. A stretch runs from where
the scheduler resumes a body, or where one of its calls returns, to its next
yield or call. The call stays the ROM's, and the routine it reaches is
substituted or not on its own terms. See `docs/threads.md`, "Thread bodies".

The reset's two WRAM-clearing block moves went in too, as `reset_clear` at
`$80:80C1`. The 65816 helpers `sched.c` had to itself are in
`src/port/cpu.h` now, for both files. `tools/cycles816.py` prices `(dp)` and
`(dp),Y`. The registry is 152 entries.

Live, over all 56 records, the game's figure goes from **75.4% to 80.1%**, and
every record gains between 2.2 and 7.6 points. The work left in the residue
goes from 171.1 to 124.8 million instructions. Thread bodies go from 26.2% of
it to 4.7%.

* **Checked.** The corpus verifies at 20,047,706 calls across 50 movies with 0
  diverged. `verify --level` over all 56 records gives 31,325,781 calls with 0
  diverged. Every new cost model is exact to the refresh on every call,
  `reset_clear` included at 6.2 million cycles a call. Lockstep matches last
  round's to the cycle on 49 movies, the level-25 partings included, so the
  new routines add no drift at all.
* **`level21-exit.zmv` no longer hangs under lockstep.** Last round it compared
  nothing. It now runs 4,088 passes and never parts. Why it changed has not
  been looked into.
* **One branch untaken:** `reset_warm`, the reset that finds the top-scores
  table already in WRAM. Nothing in the corpus soft-resets.
* **Neighbours popped into view in widescreen, and that is fixed.** The first
  cut of `victims_resume` wrote the window's `$0080` and `$00A0` in as
  constants. Widescreen rewrites those two words in the ROM every frame so the
  window follows the picture, so the port started neighbours only once they
  were inside the margin. It reads all four of the window's immediates from
  the cartridge now. `actor_cull` had the same fault from the start, with the
  window widescreen widens for 21:9 and the map ends, and reads its four from
  the cartridge too. `verify` against a copy of the ROM with those words
  widened, on `level1-rescue.zmv`, starts 19 neighbours where stock starts 3,
  and 0 calls diverge. No other ported routine spans a word that widescreen,
  the blood, the level start or the twin stick write.
* **`$81:81F6` is the neighbours.** Its list is the one `victim_list_parse`
  reads, at level record offset `$1E`, and the gate it holds is `victim_gate`.

What is left, by family: callable routines 51.0%, the SPC700 uploads 28.7%,
vblank jobs 14.3%, thread bodies 4.7%, the frame 1.3%. The top portable rows are
the vblank jobs, which are DMA and need the port to be able to write hardware
registers, and `$80:E4BA`, a movement handler reached through a jump table.

### The scheduler, the vblank dispatchers and the NMI (2026-09-24)

Step two of the Devil's Crush path. `thread_yield`, the scan after each frame,
both vblank dispatchers and the NMI handler are ported, in
`src/port/sched.c`. None of them returns, so the harness learned a second way
to leave: a routine may name its **exits**, the instructions of its own that
control leaves it by, and the port hands over the whole register set and which
exit. See `docs/threads.md`, "The scheduler itself", and `docs/cosim.md`, "The
frame's own machinery".

Live, over all 56 records, the game's figure goes from **66.8% to 75.4%**.
Every record gains between 5.7 and 11.9 points. The frame family's share of the
residue goes from 44.1% to 5.2%.

* **Fourteen entries.** `thread_yield`, `thread_exit`, `sched_wake` and
  `sched_rescan`, the last two in both banks; the run and resume halves of
  each dispatcher; and the NMI in four stretches between its hardware
  accesses. The pads come in with the registers.
* **Checked.** The corpus verifies at 19,493,013 calls across 50 movies with 0
  diverged. `verify --level` over all 56 records gives 29,940,213 calls with 0
  diverged. Every cost model is exact to the refresh. Lockstep over the corpus
  is clean on 49 movies, `level24-carry`'s three level changes among them.
  `level25-2p`'s four unaccounted bytes and the level-25 partings are the same
  with the fourteen left to the ROM.
* **A thread ends in bank `$00`.** `thread_spawn` builds the exit return
  address with a bank of zero, so the scheduler runs on in the slow mirror
  after a thread ends. `thread_exit` is at `$00:833E`, and lockstep's sync
  point now accepts the `WAI` in either bank.
* **Fixed on the way:** `nmi_input` counted its table bytes twice when
  `$420D` was clear. `zamn_cosim -x` and `zamn.exe -r` had their own copy of
  the registry cap, 128, which the registry had passed.

**Found, and not fixed: lockstep hangs on `level21-exit.zmv`**, between frames
3,000 and 4,500, with the fourteen left to the ROM as well. It joined the
corpus list after the last lockstep pass, so this is the first time it has
been run that way. `verify` on it is clean, including the bank `$00` wake-up
and rescan.

### Every collision handler the records install (2026-09-24)

The eight handlers the residue named are ported, so a sweep of all 56 records
no longer sends a sprite pass back to the ROM for a missing handler. See
`docs/cosim.md`, "Every handler the records install".

* **`$82:9A6D` is `$81:D7F6`, byte for byte**, and both addresses route to
  `enemy_d7f6_collide`. It was 2,482 of the declines.
* **`$82:AA2E` is a boss on records 20, 40 and 47**, `$82:9660`'s shape with
  its own offsets, one immune id and five rewrites. It was 1,888.
* `$82:F330`, `$82:EFF0`, `$81:B95F`, `$82:84AC`, `$81:C8C3` and `$81:A638`
  are the other 207 declines. None of the eight declares a guard.

**`zamn_cosim verify --level N`** starts a game on any record, so routines no
movie reaches can be checked. Driven by the sweep movie, all 56 records
verify: **23,368,681 calls, 0 diverged**. The only decline left in that sweep
is `$80:F999` in the player's id table, 5 times.

**It found an old bug.** `enemy_b41c_collide` and `enemy_collide` left X
unset on the path that jumps to `$81:8506`, where the ROM leaves the damage
index. `verify` on record 36 failed 25 of 359 calls on `X`. It was invisible
to the game, because the dispatcher's `PLX` restores X, and to the corpus,
because no movie hits either creature while it is still flashing. Fixed.

Live, over all 56 records, the game's figure goes from **66.4% to 66.8%**.
Records 0 and 51 gain 8.2 points each, record 40 3.2 and record 20 2.9. The
corpus is 15,801,285 calls across 50 movies with 0 diverged, and `$82:F330`
has left its decline census.

### Measuring what is left, from every level and from real play (2026-09-24)

The first step of the Devil's Crush path: fix the instrument before chasing
the number. Three changes, no routine ported.

**The SPC700 wait in `apu_send` is a wait.** `$80:CCCC  CPY $2143 : BNE` holds
for the sound chip to acknowledge each command. It is 94% of `apu_send` and was
the biggest row in the ranking at 12 points, and the three IPL spins beside it
were already in `src/cosim/waits.h`. It is the eleventh row there now. Over
all 56 level records played live, the game's figure goes from **58.6% to
66.4%**. The work done natively did not change. The denominator lost 4 billion
cycles of waiting.

**`zamn --profile <dir>` writes what the 65816 still ran**, and
`tools/native_share.py --residue <dir>` ranks it. The files are the tracer's
three formats, counted under substitution, so the residue is measured rather
than inferred, from any session. The directory accumulates, so play-tests can
be collected into one. `src/cosim/profile.c`.

**`zamn_trace --level N`** starts a game on any record, with the same patch as
`zamn --level`, which moved into `src/levelstart.h`. Traced over all 56
records, the game's figure is **72.4%**. The eleven-movie corpus gives 73.7%.

The residue found starts the ranking had been missing, and the traced corpus
had been counting all of them as native:

  * `$80:B947 sprite_upload_flush` and three more vblank jobs queued by a tail
    `JML $8083AE` rather than the `JSL` the scan matches.
  * `$80:E4BA`, first of five handlers in the table at `$80:D74F`.
  * `$81:AEA6`, a state installed through `$0E`.

Declared, they take the traced corpus from 75.4% to 74.0%. A wait inside a
written routine also has to leave the numerator: the "written" lines read ten
points high until it did.

What is left, from the live residue of all 56 records. The points are
approximate, because the residue counts instructions and the share counts
cycles:

| Family | Share of residue | About |
|---|---|---|
| The frame: `thread_yield`, both dispatchers, NMI, reset | 43.1% | 14.5 points |
| Callable routines, including `apu_send`'s own work | 22.2% | 7.5 |
| Thread bodies | 15.8% | 5.3 |
| Sound uploads: `apu_ipl_upload`, `apu_load_set`, `apu_boot` | 12.6% | 4.2 |
| Vblank jobs | 6.3% | 2.1 |

One callable item is new and cheap. Eight collision handlers are unported.
When one comes up, a guard declines, and that frame's whole sprite pass runs
on the 65816. The records that decline are 0, 12, 20, 31, 36, 39, 40, 47, 48
and 51.
The handlers are `$82:9A6D` with 2,482 declines, `$82:AA2E` 1,888,
`$82:F330` 61, `$82:EFF0` 61, `$81:B95F` 58, `$82:84AC` 24, `$81:C8C3` 2 and
`$81:A638` 1. They cost about 2% of the residue.

### The launcher's first click only brought it forward (2026-09-24)

Reported in play-testing: with the launcher behind another window, a click
on a row or a button only brought it forward, and it took a second click to
do anything. That is SDL on Windows, which drops the click that activates a
window unless `SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH` is on
(`WIN_ShouldIgnoreFocusClick` in `SDL_windowsevents.c`). The launcher now
sets it.

### Zeke's head on the radar's panel, on level 15 (2026-09-24)

Reported in play-testing, with a quick save: by the fire in the top left of
level 15, Zeke's head and the top of the fire drawn away from where they
were in 16:9, until an item by the fire was picked up. Not in 4:3. The
actor pass declines there on every tick (`$82:F330`, a handler the port
does not have), so the ROM draws the sprites and no owner table is written.
`ws_pass_on_screen` then fell back to the newest table, the port's last,
which said entry 0 was the radar's marker; entry 0 was Zeke's head, and it
was anchored with the panel, 43 columns left. An OAM no recorded pass
matches is now read by its look (`ws_screen_by_look`): each screen-space
record in the visible list is composed and its pieces are matched against
the entries. On the save, 898 ticks drawn by the ROM: an entry pinned on
every one before, none after, and the picture right. On `radar11` with F1
pressed once the radar was up, 199 ticks drawn by the ROM and the marker
found on each, inside its box. `zamn_test_layers` in 16:9 on `level1`,
`level25-lane` and `level9-weapons` still OK. Porting `$82:F330` is left
for its own change.

### The launcher's ammo and lives rows were swapped (2026-09-24)

Found in play-testing, after infinite ammo seemed to stop working partway
through a game: it had never been on. The Cheats tab's labels listed
Infinite Lives before Infinite Ammo, and row `S_CHEAT + i` sets the `i`th of
`config_cheat_names`, which has ammo first. So the row called Infinite Ammo
wrote `infinite_lives` and the other way round. Each row now names its key
in `config_cheat_names`, so the order shown is free: Infinite Lives, then
Infinite Ammo / Uses. Checked by turning on each of the two rows in a scratch
config with `--press` and saving: only its own key comes on.

### The launcher draws only when something changes (2026-09-24)

Asked for in play-testing, after the same fault in another launcher: every
batch of events was drawn and presented, so a moving mouse kept the launcher
redrawing at the display's rate, and so did a controller's stick drifting in
its dead zone. `handle` now says in `ui.redraw` whether the event can have
changed the screen: a mouse move only when it changes the hovered row or the
lit option in an open list, or drags a slider; the left stick only when it
starts or ends a direction; a button or key release and events nothing reads,
never. The countdown for a binding is drawn when its second changes. Measured
by moving the cursor over the window about 8,000 times a second for 3
seconds: 359 ms of CPU within one row and 516 ms across rows before, 94 and
109 after, about what it uses sitting still. The scripted `--press` and
`--screenshot` run draws as before.

### The radar's box sliding again, on level 11 (2026-09-24)

Reported in play-testing: the dimmed box flashing below the radar's frame on
level 11, with the weed whacker cutting plants and the player moving. A third
way into the hole of the 09-20 entry. `apu_drive`, the port's wait for the
SPC before a sound effect, ran the clock without answering HDMA requests. The
SPC is nearly always ready, so the wait almost never happens, but the weed
whacker keeps it busy. Found by counting unanswered requests in the core: all
were inside `$80:CC3B`, none under `--stock`. Each piece of the wait now
calls `dma_handleDma`. `zamn_test_layers` on a level 11 movie in 16:9 with the
cheats, radar up and weed whacker cutting, 6,996 ticks: 94 with the box off
its lines before, none after. `level1`, `level25-lane`, `level9-weapons`
still OK.

### The top scores survive a relaunch (2026-09-18)

Asked for in play-testing: scores were gone at every launch. That is the
console's behaviour -- no save RAM in the header, table rebuilt from the ROM
when `$7E:2124` is zero (`$80:85F6` -> `$82:BB0D`) -- and the frontend now does
what the cartridge could not. `src/hiscore.h`: the 190 bytes at
`$7E:2064-$2121` (ten text rows, ten BCD scores) are restored once the game
has set its table up and written to `<rom name>.hiscore` whenever they change
and are well-formed; `--no-high-scores`, `--high-scores <file>`, off under
`-m` unless a file is named. Checked in `zamn.exe` itself: a real game over
under `level1.zmv` with health held at zero and the score poked saves
`///////7654321` at the head of the table, and a `--skip-intro` relaunch
restores it; an untouched run writes no file; a junk file is reported and
ignored. `zamn_test_layers --png` now writes work RAM beside video memory,
which is how the table was found.

- **A forced game over on a menu-sitting movie is the attract mode's.** A long
  detour went on "the game never files the score": `go.zmv` never starts a
  game, the demo does, and the demo's level loop (`$80:9B87` calls `$80:8516`
  too) goes from its game over to the top scores without `$82:BBED`. A PC log
  in the core for one build found it in one run; reading the routine did not.

### No more aspect setting (2026-09-23)

Asked for in play-testing: the setting was not wanted. The picture is always
4:3 pixels now. `aspect`, `--aspect`, F3's `toggle_aspect` and the launcher's
Aspect dropdown and hotkey row are gone, and `wide_for_display` no longer
takes a shape. An old `zamn.ini`'s `aspect` and `toggle_aspect` lines are
taken and ignored rather than warned about, which `zamn_test_config` checks.
Square pixels stay only as `Present.square`, which `zamn_test_present` sets
so that an exact 2x is exact on both axes; `zamn_test_scale` still sweeps
`scale_plan` at both shapes, and lost the two `auto` cases that were about
square pixels. `integer` is unchanged: it was always square.

### 21:9 widescreen (2026-09-23)

Asked for in play-testing: ultrawide, 21:9 and 32:9. 32:9 was dropped: at
684 columns it is wider than one lap of a sprite's nine-bit X and of the
game's 512-pixel planes. 21:9 is 448 columns, 96 a side (`WIDE_21_9` in
`src/scale.h`), in the config, `--widescreen`, the launcher's dropdown, F4's
cycle and `auto`, which now picks it on an ultrawide or anything wider.
Three things were only big enough for 16:9. `PPU_EXTRA_MAX` goes from 128 to
192, since one side takes both margins at the end of a map. The ring fill in
`widescreen_frame` filled the widest either margin can get on both sides, 80
of a 64-column ring at 21:9, so it fills what the picture reaches plus the
smoothing's 16 either side. And `actor_cull`'s horizontal words
(`$80:BCF8`, `$80:BCFD`) are moved out to 32 past the picture's edge when the
edge passes 128 behind or 383 ahead, which only 21:9 at a map's end does.
Checked: 200 frames over five movies at 16:9 byte-identical to the build
before; the same frames at 21:9 looked over, the map's left end with the
whole 192 on the right among them; `zamn_test_scale` updated for `auto`
(3440x1440 and square pixels at 3840x2160 now pick 21:9), and
`zamn_test_config`'s bad widescreen value is now 32:9.

### The title's logo as the launcher's heading (2026-09-23)

Asked for in play-testing: the logo in place of the text heading. Laid out
in a line it was too spread out, all three lines side by side or ZOMBIES
beside the other two, so it is as it is on the title, and the header is
122 points high for it rather than 74. `tools/make_logo.c` (`zamn_logo`) boots the cartridge to frame
2180, the icon's still, where the logo is BG1 alone (the spiral is BG3).
It draws the last frame twice with every other layer off and the backdrop
black and then white, and a pixel that is the same in both is the logo's,
so its black outline is kept with no colour keyed out. Its five pieces
(ZOMBIES, ATE, MY, NEIGHBORS, TM) are found by what touches what and
checked against the three lines by height, and the picture is cut to them:
210x173. It is a header made
at build time from `ZAMN_ICON_ROM`, never kept, and without a cartridge
the heading is the text it was. The launcher draws it 104 points high,
blown up by whole pixels past that and smoothed down, so it stays sharp at
any display scale.

### Dropdowns in the launcher, and the levels by name (2026-09-23)

Asked for in play-testing: a list to choose from in place of the values
that went round under Left and Right. Every setting with a fixed list is
now a dropdown: the choices, the starting level, the window size and the
refresh rate (the presets, and a rate the file had that is not one of them,
in its place). The mouse, the keyboard and a controller all open, move
through and close it, and Left and Right still step the value shut. The
three sliders are as they were. The starting level lists Off and levels 1
to 48, then the credits and the seven bonus rooms, each with its card's
title in parentheses. The titles were read off the 56 cards by rendering
each record with `--level`, since the cards' text is tile codes whose
letter shapes do not decode to one letter apiece.

### The launcher comes back when the game closes (2026-09-23)

Asked for in play-testing. Play used to start the game and close the
launcher. Now the launcher keeps the game's process, hides, and drops its
own input while the game runs, so a button pressed in the game is not one
pressed here. When the process ends the window shows again, with "The game
has closed." or the exit code if it was not 0.

### The cheats in zamn.ini and the launcher (2026-09-23)

Asked for in play-testing: the cheats, each on or off, in the launcher.
`zamn.ini` gains `[cheats]`, the six flags' names with underscores, all off
(`config_cheat_names` in `src/config.h`, in `CheatId`'s order, which
`main_sdl.c` asserts). A flag beats the file, `--no-<flag>` included, since
`cheats_flag` now marks what it was asked; the file's are not taken under a
movie, like the intro skip and the level. An image that cannot take one
names `[cheats]` rather than a flag it was not given. The launcher has a
seventh tab, Cheats. `zamn_test_config` reads the section, with the other
spelling of neighbours, and round-trips all six on. Checked natively: a file
with invincible and give all runs with both; `--no-invincible --always-run`
over it runs with give all and always run.

### A launcher for zamn.ini (2026-09-23)

`zamn_launcher.exe` (`src/launcher.c`): the file's settings in six tabs, on
black, and a Play button that saves and starts `zamn.exe --config <file>`.
SDL like the game, drawn by hand with the system's font through
`stb_truetype.h`, which is new in `third_party/stb`. Mouse, keyboard and
controller all work it, and bindings are set by pressing them. The writing
half is `config_update_text` in `src/config.h`: each value is set where it
stands, a missing one goes after its section's last setting, and a missing
section goes on the end. So the player's comments and spellings survive,
and `zamn_test_config` holds it to changing no byte of the default file and
to reading back whatever it writes. The icon is the title screen at frame
2180, where the logo has landed and the menu's sprites have not come up.
`zamn_icon` (`tools/make_icon.c`) draws it from the cartridge at build time
into a 7-size `.ico`, and CMake puts it in the launcher's resources when a
cartridge is there. Checked through `--press` and `--screenshot`, which
drive it without a window: save touches one line, capture, removal,
conflicts drawn red, a fresh file, and Play against a stand-in `zamn.exe`.

### Widescreen can follow the display (2026-09-23)

`--widescreen auto` (and `widescreen = auto` in `zamn.ini`): fullscreen, the
width whose picture covers the most of the display at the current aspect
(`wide_for_display`, `src/scale.h`); in a window, off. `WIDE_AUTO` is a fourth
value of `WideMode` that only the frontend's setting ever holds, so F4 now
cycles off, 16:9, 16:10, auto. `wide_parse` still refuses it, which keeps it
out of `zamn_headless` and `zamn_test_layers`, which have no display;
`wide_setting_parse` takes it. The frontend settles the width once per frame
after the display key, from `SDL_GetRendererOutputSize`, with the texture
rebuild F4 always did; the start is quiet unless `--verbose`. Checked on the
3840x2160 panel: fullscreen gives 342 columns, `--windowed` 256, F11 out and
back gives off and then 16:9 again, and F4 walks off, 16:9, 16:10 from there.
`zamn_test_scale` pins ten displays.

### The game over mask is whole on the neighbours-lost path too (2026-09-22)

Play-testing under `--invincible`: a game over on level 13 had the middle
third of the 16:9 picture bare again. It was the other way into the game
over. The level loop `$80:8516` returns game over when both panel flags are
down (a player's last life clears theirs, `$80:CEDA`) and also when the last
neighbour is lost with none rescued -- and then the player's flag stays up,
so the panel-flag tell from 2026-09-18 kept BG3 split. Under `--invincible`
that is the only way in. `src/widescreen.h` now reads where the main game
thread (`$80:84B1`, slot 23 in practice, found by its entry) is parked, off
its own stack under the scheduler's frame (`ws_main_thread_at`), and calls
BG3 the mask while that is inside `$80:8A00`'s waits from the mask's upload
on and the scroll shadow is set (`ws_game_over`). Reproduced on the stock
core with `level13.zmv --poke 3000:1D52=0000`; the layers test now carries
the mask on 782 frames there and 782 on the poked death movie (831 before:
the 30-tick wait before the upload is left out on purpose, since the live
panel is still up then), 0 differing pictures on both, none of the mask on
five level movies, and the next game's panel anchored after either kind
(the death one on `go2.zmv`, the neighbours one by a quick save at its top
scores and Start mashed from a load). `--png` and `--dump-pictures` print
the parked place and the verdict.

### The console is quiet unless asked (2026-09-22)

Play-testing: quitting the game scrolled some 200 lines past the prompt -- the
banner, the bindings, the cadence, the per-routine table, the native share and
its prose. `src/main_sdl.c` now prints, for a played session, the controller,
the cheats, the starting level, anything that could not be had or went wrong,
and one line at exit; `--verbose` restores all of it, and a `--frames` or `-m`
run has it by default, being a measurement. The core's own three lines at the
load (`snes_other.c`) are gone, and the frontend says the ROM's type and size
under `--verbose` instead; `src/hiscore.h` announces a restore or a save only
when told to. README, `docs/cosim.md` and `tools/native_share.py` say where
the report went.

### Give-all gives only to a player who is in the game (2026-09-21)

Play-testing under `--give-all --infinite-ammo`: the ghost potion turned the
weapon icon into its blue flame and left 999 under it, where the console
blanks the count. `$80:DACB` selects weapon 17 and item 15 for the flames,
past both inventories; the HUD blanks a count for a selection past the end,
but only redraws it when the word it reads changes, and for 17 that word is
player two's second weapon slot through player one's table. The game seeds
player two's inventory in a one-player game and give-all filled it, so the
word was 999 like the count already drawn and nothing was redrawn. Now
`src/cheats.h` gives only to a player whose HUD panel flag (`$7E:1E88`/`$1E8A`)
is up; it is raised at the character select before the seeding, so a new game
is still given to before the title card goes. Reproduced under the stock core
with the six flags and natively in 4:3 and 16:9; `zamn_test_cheats` covers
the flag. On the way, the trampoline: it is background tiles, and the flame
passing under its top edge and over the rest is the camera's rule forcing BG
priority on for tile indices under the level's threshold (`$70` there); the
layers test finds every picture over the shot identical to the PPU's.

### The monster's pose changes are placed, not slid (2026-09-21)

Play-testing: the potion's monster did not animate as smoothly as the rest,
walking or punching. Measured with `zamn_test_layers --track` on a movie that
drinks the potion: the monster's second punch frame draws the body three
pixels lower and its walking frames shift it by two, every piece by the same
amount, so the pairing vote in `src/layers.h` carried it as motion and the
body slid over the tick and bounced under `even`, where the console swaps
frames in one step and the player, whose frames move pieces by different
amounts, is placed by the tie. Now a unanimous shift further than a pixel
from the actor's own move (`LAYERS_POSE_MAX`) is a new pose and the pieces
are placed by the actor's move (README -> *Smoothing*). The monster's eased
ticks: punches 33 -> 0, first steps 3 -> 0, four directions 27 -> 9 (the nine
are pixel shifts, kept). Also reaches walkers whose frames shift the body by
exactly two pixels, which now snap as the console does: 266 records over
2,200 ticks of `level1`, 356 over `level25-boss`; end-of-tick pictures
unchanged over six corpus movies. `zamn_test_layers` counts the records
placed as a pose. Not tried by hand at 240 Hz. Noticed on the way: the
layers test over `level1` 2400-4600, `level9` 3000-5000, `level21-spin`,
`level29-fighting` and `level17-weapon` fails on one to four pieces apart
from their record before and after this change alike; not looked into.

### The monster aims with the right stick too (2026-09-21)

Play-testing: a player turned into the monster by the potion punched only the
way the D-pad last pointed, twin stick or not. The monster is player state 1
(`$80:D2EA`, entered by `$80:D9A3`), which shares state 0's idle and walk code
and `$26` but has its own copy of the direction latch at `$80:D2FD` -- eleven
bytes, the nine at `$80:D250` plus a `BRA +0` -- and the stub had only been
put under state 0's, that state having run zero times in the profiles. Now
both latches `JSR` the one stub (`src/twinstick.h`; README -> *Twin-stick
shooting*). State 1 is the 65816's in every build, so the patch is the whole
fix. `zamn_headless` grew `--twin-stick` and `--aim frame[+]:dirs` to drive the
stub under the stock core; checked on a movie that drinks the potion under
`--give-all` (standing: faces right then up with the stick, and punches that
way; walking right with the stick up: `$24 = $06`, `$26 = $02`), and natively
through a temporary hook, frame for frame the same. `zamn_test_twinstick`
covers the second latch and refuses a cartridge without it. Asked on the way:
the two punch sounds are the game's own -- `$80:EF67` (standing) plays `$23`,
and `$80:D6DC` (walking) plays `$15` at the top of each walk cycle, the sound
the thrown weapons' shots make (`$81:F56B`, `$81:F812`); the squirt gun's is
`$0B`. Also found: `--always-run` gives the monster the shoes too, since
`$80:D9A3` clears `$54`, and `$C000` is the mystery potion's wandering state
(`$80:DB42`), not the monster -- the cheat's notes said otherwise, and are
fixed.

### Cheats, behind six flags (2026-09-21)

Asked for in play-testing: `--invincible`, `--invincible-neighbors`,
`--infinite-ammo`, `--infinite-lives`, `--give-all`, `--always-run`
(`src/cheats.h`; README -> *Cheats*). Made of three things, by who runs the
code: patches to the loaded image, checked first and all or none (the five
`SBC #$0001`s that spend ammo, items and keys; `DEC $1D4C,X`, which is the
lives; a neighbour's five fatal ids in `$83:A364`, two in `$83:A264`, and the
tourists' werewolf `BNE`; the two player hits that do not ask the recovery
timer); words held once a tick where `--poke` writes (the recovery timer `$52`
at `$40`, health at ten, counts raised to `$0999`/`$0099`, `$54 = $8000` for
the shoes, and the inventory given when it is exactly a new game's or a save
has just been loaded); and `port_cheats` (`src/port/cheat.h`) for the routines
that are the port's own -- `victim_collide`, `victim_a264_collide`, and
`player_collide`'s id `$0A`. Works under `--stock` and F1 for that reason.
Found on the way: `$7E:1D4C` is the lives (collide.h had it as a counter
nobody read), `$7E:1D52` the neighbours left, item slots 6 and 11 have icons
and cannot be had, and the demo ends by its neighbours being eaten -- so with
them safe it never ended, and the cheats now stand down while the demo's job
(`$9CB1`) is filed at `$7E:12E0`. The top scores are read and not written
while a cheat is on. `zamn_headless` takes the same flags, for `--watch`.
Checked by `zamn_test_cheats` (synthetic image and the cartridge), under the
stock core and natively on `level5`, `level9`, `level21-bubble`,
`level1-rescue` and `level1`, and by `verify` still passing with the flags
off. Flags only: no `zamn.ini` settings, and no hotkeys to switch them in
play. Not tried: two players, a boss, or a whole level by hand.

### Shoulders and triggers select weapons and items, both ways (2026-09-21)

Asked for in play-testing. R1/L1 are the next item and the one before, R2/L2
the next weapon and the one before; the radar (SNES L and R) moved to the
touchpad's click and L3. All six are `[controller buttons]` settings. Not
SNES buttons, because the cartridge has no backwards and -- found on the way,
and now a test -- cannot change weapon while a loaded one fires
(`player_state_normal` clears B from `$1A` under a held Y). `pad_poll` reports
presses by port (`PadSet.cycle_pressed`), the frontend calls
`player_cycle_request`, and `player_state_normal` takes one step a frame
beside its B and A edges: `weapon_select_next`/`item_select_next` forwards,
`weapon_select_prev`/`item_select_prev` (the same search backwards) the other
way. Requests expire after 12 frames unanswered. With none pending the
routine is what it was, so the corpus is untouched. Does nothing under
`--stock`/F1, where the 65816 runs the routine. An input bound to a selection
is not also a SNES button, and `config_check_pad` says so: an old `zamn.ini`
with `l = l1, l2` selects and has no radar until edited. Checked by
`zamn_test_twinstick` (against the cartridge), `zamn_test_pad`,
`zamn_test_config`, and in `zamn.exe` on level 1 with requests injected:
weapon 0, 7, 3, 0, item 7 to 4, HUD following. Not checked with a real pad
from here. No keyboard bindings for these.

### Seams across the slimes while they attack (2026-09-21)

Reported in play-testing on level 9: horizontal lines through a slime during
its attack animation. The pictures between ticks again, and not only slimes:
`layers_link` eased each piece of a record from where that piece's own
predecessor had been, so a frame of animation that moves the pieces by
different amounts pulled them apart at the joins for three pictures in four,
and `even` kept pieces a quarter of a pixel apart on the tick after their
moves had differed. Now a record's pieces are all taken back by the move most
of them made (the origin's on a tie), and the move before, `even` and the
placement test are judged once per record. `zamn_test_layers` has a new count
and fails on it: pieces eased by a different amount from the first of their
record, 2,271 over `level9` in 16:9 before and 0 after; 646 of 35,634 paired
pieces now go by their record's move. The draw-list comparison is unchanged.
The new count then failed `level25-lane` on 21 pieces, all on the two ticks
after a tick too slow for its frame, when there was no owner table and pieces
were paired by looks one at a time. The last table now stands for a picture
the pass did not run for, if the sprites it accounts for are unchanged
(`layers_owners_stand`, used by `layers_take` and the test): 0 there, and 0
in `level25-heavy`.
The movie was not checked for a slime's attack in particular; the count covers
every record in it.

### Pickups flickering in the widescreen margins while walking (2026-09-21)

Reported in play-testing: with the camera moving, keys and other items in the
margins flicker for an instant, and never in the middle of the screen. They
were never missing -- a check on every frame of three level 1 movies found
every listed object's sprite in OAM exactly where the list and the camera put
it. It was the pictures between ticks. A sprite is eased from where it was,
and found there by its owner; the sprites `src/widescreen.h` puts into the
margins had none, and were paired only with other ownerless sprites. A piece
crossing column 256 changed from the one kind to the other, had no last
position for a tick, and was drawn a camera's step ahead of the ground for
three pictures out of four. `Widescreen` now keeps the record and origin of
every piece it places (`ws_owners`), both readers of the owner table merge it
in (`layers_take` in `src/main_sdl.c`, `tools/test_layers.c`), and
`layers_link` pairs an ownerless sprite with one whose record has gone from
the tick, which is what a pickup's does as the camera leaves it.
`level1-pickups` in 16:9: unpaired sprites 149 to 78, all first appearances;
pairings by looks 1,919 to 41. The draw-list comparison is unchanged.

That helped and the keys of level 7 still flashed. The second cause was a
sprite that really was missing, for one tick, as a pickup came in from the left
margin: the level's list drew it only outside the console's 256 columns, and
the spawner, which looks every fourth tick, had not yet given it a record at
column -15. Reproduced in the frontend (`--level 7`, sixty `--key-at` walks
left and right beside a pickup) with a check that every listed object in the
picture has its sprite in OAM: missing on 7 ticks of 1,414, one per pass,
and on none with `ws_object_sprites` drawing a recordless thing anywhere in
the picture (`inside`, at `ws_emit_meta`).

### The radar's dimmed box sliding under its frame (2026-09-20)

Reported again, worst on levels 7 and 8. Not the game's "wandering
interrupt" of the 09-19 entry: the box is HDMA off a ROM table and under
`--stock` never moves. `burn_slice` spent a substituted routine's whole
budget in one call, the core's HDMA request is one bool a line, and every
line after the first in a budget went without its transfer (top of the box
at 49-51 on level 1, 49-67 on level 7); `dma_handleDma` was also handed the
budget as the length of the access to finish, and ran on for it. Now a
scanline at a time, with a 12-cycle access, while any HDMA channel is on,
and unchanged otherwise. Levels 7 and 1, radar up, 1,095 ticks each: no
request lost, box on the stock lines on every tick. Draw-list test
exact on the radar movie, `level1`, `level25-lane`.

### Neighbours dying out of sight: the window follows the picture (2026-09-20)

Reported in play-testing. A neighbour is mortal wherever she exists (monsters
touch the whole visible list, 128 behind the camera to 383 ahead), and the
first widened window -- stock middle, reach `#$00A0 + 2 * margin` -- had her
existing 75 pixels past each edge of a 16:9 picture and 118 at a map's end,
against the console's 32. `ws_widen_window` now writes the window's middle
(`ADC #$0080` at `$81:820A`) as well as its reach, every frame, from the live
camera: the picture's middle and half the picture plus 32. Measured on
`level1` and `level25-lane`: furthest alive outside the picture 73/71 before,
2/37 after; none missing inside it. Draw-list test OK on three movies.

### Football players appearing in the widescreen margin (2026-09-20)

Reported in play-testing, level 12. The football player's thread (`$81:C87B`)
ignores its spawn point's column and puts him down 8 pixels left of the
console or 72 right of it, where he gets set before he charges: 8 left of the
console is inside a 16:9 picture. `ws_widen_window` is now a table of ROM
words moved out by twice the margin, the neighbours' `#$00A0` and ten more:
his two wings, the window he may wait in (`$81:C742`), his reach from the
player, and the same idiom in the bonus rooms' tentacle (`$82:990F`) and a
creature at `$82:EAC5`, found by reading every use of the camera's column in
the code banks. Level 12, logged: arrivals at -8/-6 and 328 before, at -92 to
-98 and 414 to 418 after; the tentacle at -94 and 414 on record 51. The
`$82:EAC5` creature has not been seen. Draw-list test OK on four movies.

### The baby and the saucer in the widescreen margins (2026-09-20)

Reported in play-testing: cut in half at the margin, and a copy on the other
side. They are BG1, not sprites: a 512-pixel plane with the figure in one
corner, scrolled into place. `ppu_wideAuto` either clipped it at the console's
256 or repeated the console's columns outward; in a level BG1 is now
`ppu_wideStretch`. And `$82:8209` parks the plane (`$0100`, `$0100`) once its
origin is 256 right of the console's left edge, so `ws_boss_plane` writes the
scroll it would have written while the origin is inside the right margin --
from the live words, which is what the job read (4,446 of 4,491 frames match
live, 1,591 the tick-old copy). Frame 5,300 of `level25-lane` is whole; he
walks in from the picture's edge; draw-list test OK on the boss movies.

Reported again: still flashing at the sides. Two more causes, and a trap in
fixing the first. The plane
repeats every 512 pixels, and with the baby 214 or more columns off to the
left the right margin read its next lap (his bottle and arm, standing there):
parked in that band. At a turn-round the next tick has sometimes moved the
origin by line 0, so the live words disagree with a plane the job parked
(gone from the margin for one frame): the first of live and tick-old that
agrees with the parking is used. The trap: on a frame the game drops the
register still holds the value written here, which nine bits read as far off
to the left, to be parked; all ten are read. Decisions are made from the figure's
measured extent and 16 columns past the picture (the smoothing's capture
margin). Checked in the hook on every frame of four level 25 movies: the
figure somewhere it is not standing on 44-107 frames with the routine off, 0
with it on, and the only single-frame gaps left are four of the game's own
(the figure coming on over the top or bottom edge).

### A settings file, and every binding in it (2026-09-20)

Asked for in play-testing. `src/config.h` reads `zamn.ini` (here, then beside
the executable; `--config`, `--no-config`; written with the defaults and a
comment on each when there is none): the ROM's path, skip intro, level,
hitbox, blood, top scores; fullscreen, widescreen, aspect, filter, window
scale, smoothing, refresh; audio and a new volume; pads, twin stick, deadzone,
which stick steers and which aims; and lists of keys or pad inputs for the
twelve SNES buttons (two players' worth of keys) and for the frontend's nine
actions, from keyboard or pad. An option beats the file; `skip_intro`, `level`
and `hitbox` from the file are not applied under `-m`. `src/pad.h`'s button
`switch` became a table (`PadMap`) whose default is built from it; the
function keys became actions done below the event loop. One fix on the way:
a button on two keys (Select) is now held until both are up. Checked by
`zamn_test_config` (the written file reads back as exactly the defaults),
more of `zamn_test_pad`, and `--key-at`, which presses keys through SDL's
queue: a rebound Start starts the game and the old key does nothing.

### The flash along the bottom as the game over begins (2026-09-19)

Reported in play-testing. One frame, the last line of the picture, orange
edge to edge; the game's own (`--stock` 4:3 has it), and in a television's
overscan. `$80:8A78` gives BG3 the mask's map (`$8B2B`) two frames before it
starts the job that scrolls it (`$8B70`), and at the panel's scroll of 0 the
mask's top edge is the screen's last line. `src/maskline.h` exchanges the two
`LDA` operands in the cartridge's copy, so the scroll is set first; the
panel's map is blank by then, so nothing else shows. Found by logging BG3's
map and scroll on line 224; of 1,105 pictures around it only frame 2,559's
change, in their last line.

### Quick save on F5, quick load on F9; smoothing moves to F6 (2026-09-19)

Asked for in play-testing: one save, the latest. `src/quicksave.h` writes
`<rom>.quicksave`: the core's save state plus what the core does not have
-- the widescreen's books (its work RAM copy, the borrowed sprite slots it
owes back) and the sprite pass's owner tables. Saved only on a tick the
harness ends clean (`cosim_idle`, new), and a load makes it forget its
calls (`cosim_forget_calls`). Done in the tick block, where the emulation
thread is known to be stopped; the smoothing cuts its link across a load;
the top scores are not rolled back. SAVED / LOADED shows at the top right
(`notice_draw`). Verified with `--quick-at`: a save from level 1 loaded into
the middle of another run's logos is byte-identical 100 ticks on (threaded
16:9, unthreaded 16:9, `--stock` 4:3). The even-motion key is gone (F6 is
the smoothing toggle now); `--no-even` stays.

### `--skip-intro` bypasses the logos instead of running them (2026-09-19)

Asked for in play-testing: the skip took several seconds (5.4 s, 1,150 real
frames). A save state cached beside the ROM was built first and made it
0.16 s, and was thrown away: the user did not want a first run and a file.
Instead `src/skipintro.h` takes the game's own way past its logos --
`$80:9136  LDA $7C : BNE $9152`, how a game over returns to the title --
by making the `BNE` a `BRA` in the cartridge's copy. Left are 222 frames of
sound upload with the screen off (0.6 s at full speed); hand-over is the
first frame the screen is on, so the title fades in. The title's first 500
pictures equal an untouched boot's, and a Start-mashing movie plays level 1
from it (`zamn_test_layers --bypass-logos`). Not drawing lines during the
skip was tried and saves 0.1 s, so the core was left alone; the rest is the
65816 and the SPC700 talking, which only a high-level upload would remove.
The old rule (mash Start, stop at 1,150) stays as the fallback.

### The game over's blood is red with `--red-blood` (2026-09-19)

Asked for in play-testing, after romhacking.net hack 4306 (only its
description was read). The mask on BG3 takes its three colours from six
immediates in the vblank job `$80:8B82` (CGRAM 25-27), patched in the
cartridge's copy of the image. The drips are sprites (`$8F:E9A7`, frames
`$A63`-`$A65`, palette 5, colours 9/11/12) and share their purple with a
spider and much else, so they are recoloured a sprite at a time:
`src/blood.h` marks their OAM entries from the frame hook by the frame
cache's tile numbers, and `Ppu.objRemap` (new; honoured by
`ppu_evaluateSprites` and `layers_sprite_cell`) sends the three pixels to
CGRAM `$C0/$D0/$E0`, colour 0 of palettes 4-6, which nothing reads. Stock
against red on the poked `level1.zmv`: every purple pixel is red, none
left, nothing else differs, 4:3 and 16:9, from the mask's first frame; the
frontend's GPU picture is red too. Off by default; allowed under `-m`.
`zamn_test_layers --png` also writes `prefix.frame.cgram.bin`.

### Pickups and weapons reach half as far again (2026-09-19)

Asked for in play-testing, with the DX hack as the example: bigger sprite
hitboxes so pickups and hits are more consistent. DX patches the 8/16 in
`$80:BEF1`, for every pair. Here `actor_overlap_reach` (`src/port/oam.c`,
`--hitbox <100..200>`, default 150, 100 under `-m`) is only for
player x pickup, player x neighbour and weapon x creature, so nothing reaches
the *player* any further than it did. Ids sorted from a log of every
dispatched pair over the corpus (players 5/6, neighbours 1/2, pickups the
table at `$80:CA30`, weapons >= `$5C`, `$38` a carried player, `$36` a
bubbled creature). Weapons mostly hit through `$80:BF1B` (shots:
`$80:D413`, a 16x16 box a tick; melee: `$80:F055`), so `actor_notify_box`
grows the box for creatures when a weapon asks. ROM untouched; tools stay
at 8. `zamn_test_layers` has `--hitbox` and `--watch addr`: pickups two
frames sooner, first kill two frames sooner, and a miss at 100 that hits
at 150 in `level9-weapons`. Movies desync at any reach but 100, as they
must.

### The title's logo, eased a line at a time (2026-09-19)

Asked for in play-testing: the ZOMBIES ATE MY NEIGHBORS logo should move
more smoothly. It is BG1 waved by an HDMA sine (`$80:9570`, table at
`$7E:8000`, per-line BG1HOFS only), which made it a raster layer: baked,
never eased. Worse, only the first ~84 lines of the table change every
tick; the rest change every 4th (stock ROM too), so most of the logo ran at
15 Hz in steps of up to 9 px. `src/layers.h` now eases a background whose
scroll varies across only, line by line (`lineX`, `dLineX`, `lineEase`,
per-line step spreading; `LAYERS_LINE_OPS` draws the plane in strips).
Title frames 1145/1145 identical in 4:3 and 16:9; a logo edge tracked over
64 pictures moves 2 target px a picture up top and 1-3 below (was 8 every
4th and 28 every 16th); the frontend's GPU pictures match the software
ones. After Start the logo snaps to rest and nothing on the menu moves but
the backdrop, which was already spread. `zamn_test_layers --png` writes
`prefix.frame.scrollN.txt` for a raster background, which is how the table
was read. Not done: the first step of a lower line after the screen goes
up is drawn as it comes (it teaches the interval).

### The radar flashed the player's head beside him (16:9) (2026-09-19)

Play-testing: the radar coming up flashed Zeke's head to the left of Zeke,
and sometimes a yellow marker to the right of the box. 43 columns both ways,
which is a sprite given the wrong place for a frame.
`ws_place_screen_sprites` runs at line 0 and read `sprite_oam_owners` as the
table of the OAM just DMA'd; the game starts its next tick inside vblank and
the pass has usually run again by then (497 of 585 level frames on the radar
movie), so the table was a tick ahead and it showed whenever entries changed
hands -- the marker takes entry 0 and shifts the rest, and is multiplexed.
Found with a throwaway check in the hook that printed owned entries whose
PPU words differed from the shadow buffer: every one was the shadow a tick
on. Fix: the pass keeps eight tables with their low-table bytes
(`sprite_oam_history`), `ws_pass_on_screen` picks the newest that matches the
PPU's OAM. Old against new over frames 1803-1840 of a movie that opens the
radar in play: one frame differs, a piece of a zombie moved 43 columns.
All movies 0 differing, 0 frames with no matching pass.

### Smoothing at any refresh rate, and the game's own uneven steps (2026-09-17)

The third play-test: much better, two things left. It should not need a
display that is a whole multiple of 60 (144 Hz), and diagonals still jittered
a little -- the player's, and a zombie's giving chase.

- **Any refresh rate.** The loop no longer counts `k` pictures per tick. Each
  refresh adds `p/q` of a tick to a phase (`pace_lock_ratio`: 1/4 at 240, 5/12
  at 144, 4/11 at 165, 4/5 at 75), a tick is taken when the phase passes one,
  and the picture is drawn `phase/q` of the way along -- `layers_list` always
  took a fraction, so nothing in `src/layers.h` changed for this. The tick
  rate is locked to the display (60.000 at 144 Hz) and the pacer's slack is a
  tick, as before. `--refresh <hz>` pretends a rate: 2.40, 2.75 and 1.25
  pictures per frame at 144, 165 and 75, all at 59.3-59.5 fps over a run with
  four level loads.
- **The diagonal jitter was the game's.** `zamn_test_layers --motion` prints
  every tick's moves: the player walks **2, 1, 2, 1** on each axis (1.5 px a
  tick, and a diagonal is both axes at once, in phase, not normalised), the
  camera follows exactly, and a chasing zombie moves **2, 0, 2, 0**. Eased
  tick to tick, that is a speed that changes 30 times a second, which a 60 Hz
  picture hides and a 240 Hz one draws.
- **First cure, rejected in play: average two ticks.** Pictures run between
  the midpoints of the last two moves. Perfectly even, and half a tick late on
  everything that moves -- noticed immediately, and not wanted.
- **Second cure: subtract the rounding, not the time.** The uneven steps stand
  a quarter pixel either side of a steady line (half, for 2, 0, 2, 0), so each
  thing is drawn `(d - q) / 4` back from where it is (`layers_even`,
  `layers_part_even`; F6, `--no-even`) -- as a filter, `(3p(t) + 2p(t-1) -
  p(t-2)) / 4`, which passes a steady speed with no delay and has a zero at the
  two-tick alternation. The chasing zombie's trace is 1 target pixel every
  picture as with the average, but ends each tick half a pixel ahead of or
  behind the truth in turn instead of a pixel behind it. The price is
  overshoot on a change of speed, a quarter of the change for one tick; more
  than 4 px of change is a jump and gets none. Each thing carries its own
  correction and the last tick's (`cx`/`bx`, `cScroll*`/`bScroll*`), the latter
  being where this tick's pictures start, so ticks join without a step; a
  sprite with no move-before-last is drawn where it is.
- **The survivor radar fell back to the PPU's picture**, the whole time it
  was up (it is a toggle), because its box is the colour-maths window with
  its edges rewritten at line ~50 and ~108 -- `--motion` on an unlayered
  frame now prints the first mid-frame register and line, which is how that
  took a minute rather than an evening. Window edges are recorded per line
  (`lineWindow`, `windowRaster`, $2126-9 exempt from `midFrameWrite`), the
  maths gate is read per line, and a plane the maths applies to is baked
  twice (`LAYERS_MATHED`, 18 planes) with the mathed twin drawn clipped to
  the window's rectangles (`mathRect`, `LayersOp.clipped`,
  `SDL_RenderSetClipRect`). The box stays put on the target while the world
  eases under it. 341 of 341 radar frames identical to the PPU in 4:3 and
  16:9; GPU and software pictures agree.
- **...and then its markers swept the box.** One OAM entry (slot 0, record
  `1a16`) is placed on a different survivor every tick -- multiplexing --
  and the pairing, by the record, eased it between them: 252,248 to 284,260
  to 278,204 to 352,252 in four ticks. `LAYERS_LINK_BEFORE` now refuses a
  move that differs from the move before by more than `LAYERS_JUMP_MAX`
  (6 px; 4 refused the first tick of every 6 px/tick shot) on either axis,
  judged on the actor's origin (`mx`, `my`) rather than the piece so that an
  animation frame's shift does not count. The test's pairing line says how
  many were "placed, not moved" and `--motion` lists them: 44 on
  `level25-boss`, 43 of them recordless margin sprites paired by looks.
- **In widescreen the dimmed box sat 43 columns right of its frame.** Not
  the list's fault -- it matched the PPU's picture exactly -- but the
  widened PPU's: the frame is on BG3, anchored to the picture's edges, and
  the window stayed in console coordinates. `ppu_windowTest` now maps a
  window's edges as `ppu_wideAnchor` maps columns whenever any layer is
  anchored (left-half edge from the picture's left, right-half from its
  right). Found by the fix a second bug: the rectangle scan used -1 as "no
  run open" and a run starting in the left margin (a negative column) kept
  restarting until column 0 -- 30 columns wide instead of 51. A flag now.
  `--png` prints the maths gate column by column on line 80, which is what
  showed the gate right and the scan wrong. Radar movie exact in 4:3 and
  16:9 again; `level1`, `level21-spin`, `level25-boss` unchanged.
- **The markers were 43 columns right of the box, and the box's foot
  flickered.** The markers are sprites of a screen-space record, drawn as
  world things by widescreen; `ws_anchor_screen_sprites` now flags their
  OAM entries anchored (`Ppu.spriteAnchored`, `snes_setSpriteAnchored`,
  applied in `ppu_spriteX`) from the owner table, whose serial at the top
  of a frame describes the OAM just DMA'd. `zamn_headless` links
  `zamn_port` now, because `widescreen.h` reads the table. The flicker is
  the game's: `--motion` printed the box's rectangle at y 49..52, h 57..59
  from tick to tick -- the window-edge interrupt lands on a wandering
  line. `mathShown` holds a rectangle within `LAYERS_WINDOW_WANDER` (3)
  lines of the last tick's, under `even` only, so the exactness test is
  untouched; 155 of 341 radar ticks held.
- **The logo, title and select screens were layered and exact and not
  smooth.** `--motion` now prints every background's move per tick, and
  showed the LucasArts backdrop (BG3) at +1,+1 every fourth tick, the
  select screen's wallpaper (BG3) every fifth, and the title's backdrop
  (BG3) round a circle 11 px every fourth -- motion at 15 Hz whatever the
  smoothing does per tick. A background stepping at the same interval twice
  running (3..8 ticks) has each step spread over the interval
  (`stepK`/`stepI`/`layers_spread`, `even` only, backgrounds only), landing
  exactly as the next step does; `--track` shows the title's plane at
  -41,-39,-36,... ,0 target pixels across the 16 pictures of a step. A few
  ticks of delay on a backdrop nobody steers; it never fires in a level
  (`level1`, `level25-boss`: spread ticks only on the title and select
  screens), and exactness is unchanged on `boot`, `level1`, `level25-boss`
  and the select movie (0 differing).
- **The game over screen was missing its middle third in widescreen.** The
  mask ("GAME OVER" cut out of purple, the level through the letters -- the
  console's own look, checked in 4:3) is on BG3, which a level anchors as
  the split status panel; the gap between the halves was the bare band.
  `$80:8A00` scrolls the mask in by counting BG3's vertical scroll shadow
  (`$136A`) down, and the panel never scrolls, so `widescreen_frame` gives
  BG3 `ppu_wideClampEdge` while that shadow is non-zero in a level
  (`W_BG3_VSCROLL_SHADOW`). Reached with the new `--poke` option of
  `zamn_test_layers` (health held at 0 from frame 1330 of a stand-still
  movie: death at 1594, mask up from ~2300, top scores at 3170); the
  policy fires on 799 frames of that movie and on none of `level1`,
  `level1-2p`, `level25-boss` or the radar movie, all still 0 differing.
- **...and then it was off-centre, with a line across the lower right.**
  `ppu_wideClampEdge` keeps the console's place, and at the end of a map
  the margins are 0 and 86 rather than 43 and 43 (pinned with
  `--poke 2000+:1B6A=0000`), so the mask sat left; and the mask's foot is a
  row of drips, and where a gap between two of them met the edge column the
  margin showed the level as a streak, and where an outline did, a dark
  line. New policy `ppu_wideCentre` (ppu.h/ppu.c): the layer's middle at
  the picture's middle, and a margin line filled from the edge column a
  few lines into the nearest opaque run at or above it, nothing on a line
  with nothing opaque. `ppu_wideMapX` takes the line now and may redirect
  it. `zamn.exe` takes `--poke` too, and its GPU picture at frame 2700 of
  the pinned game over matches the software one. All movies still 0
  differing; the policy fires only on the game over.
- **...and the next game's panel was centred, and the margins were a
  slab.** The BG3 scroll shadow stays at its game over value into the next
  game, so that game's panel got `ppu_wideCentre`: health bar off the left
  edge, half of it again on the right. The layer's own columns were tried
  next (panel: 112-143 empty in 2P, 112-255 in 1P; mask: none) and missed
  the mask's first ~100 frames, whose drips enter at columns 96-103 and
  216-223. What works is the game's `hud_panel_on` (`$1E88`, per side,
  `W_HUD_PANEL_ON`): 1 in play, 0 0 from before the mask's first drip to
  the top scores, 1 again in the next game -- `zamn_test_layers --png`
  prints it with BG3's empty columns now. And the margins beside the drips
  are the layer's own columns repeated (the 32-tile map's wrap) below the
  last fully-opaque line where both edge runs are under 16, the field
  carried out everywhere else: the curtain continues instead of a purple
  slab. 831 mask frames on the game over movie, none on five level movies,
  panel anchored again in a movie that starts a new game after (go2.zmv:
  Start every 32 frames from 3700). All 0 differing.
- **The panel was still off, and the exactness test could not have said.**
  The third `ppu_wideCentre` patch replaced the policy switch from that
  case to `default:` by index -- which took the `ppu_wideTile`,
  `ppu_wideStretch` and `ppu_wideAnchor` cases with it. Every anchored and
  stretched layer fell to `default: return true`: drawn in the console's
  place with the tilemap's own wrap, which is the panel 43 columns right
  and the health bar again at the right edge, in every level. The test
  reported 0 differing throughout, because it compares the draw list
  against the same PPU. Restored from `git show HEAD:` and looked at: the
  panel is at the left edge again in the test's render and in the
  frontend's own picture (`--dump-pictures` prints the panel flags and
  BG3's policy for the dumped frame now). Lesson: after any PPU change,
  look at a level frame in widescreen with the eye, since nothing else
  checks the margins.
- **The drips ended flat, and drops hung in the air.** Not the mask: a
  purple-only diff of the 16:9 render against the 4:3 one, column by
  column, found five-column strips 64 lines tall in the mask's own columns
  that were 43 columns apart between the two -- the hanging parts of the
  drips and their drops are sprites of screen-space records, anchored with
  the panel (`ws_anchor_screen_sprites`) while the mask was centred.
  `Ppu.spriteAnchored` is `Ppu.spritePlace` now (`ppu_spriteWorld`,
  `ppu_spriteAnchored`, `ppu_spriteCentred`; `snes_setSpritePlace`), and
  `ws_place_screen_sprites` gives a screen-space sprite the place of what
  BG3 is carrying. A centred sprite is drawn clipped to the layer's 256
  columns and again 256 columns either side, clipped to that margin, in
  `ppu_evaluateSprites` and in `layers_list` alike, so the drips the
  margins repeat end in drops too. Frame 2600 of the game over movie in
  16:9: the mask's columns match 4:3 in purple to the pixel, the margins
  match the wrap but for a world sprite's pixel; every movie still 0
  differing; the panel and the radar's marker are where they were; the
  frontend's own GPU picture shows every drip with its drop. `--png` prints
  how many sprites are anchored and centred.
- **The level card's text "dropped" after its bounce -- in time, not in
  place.** Fixed in `14c079f` (not by me; three attempts here measured where
  each picture was and found every one where the console has it). The
  card's last throw is 2 up, and with NMI off the native `lzss_decompress`
  burned 76 video frames inside one `cosim_frame`, so that picture stood
  for 334 ms and then the text came down. `burn_spend` parks at video
  boundaries now. The lesson is in the tools: `zamn --trace-frames` and
  `zamn_test_layers --check-frame-step` measure how long a picture stays
  up, which no picture dump can.
- **The Konami star drew two lines in 16:9.** BG1 is a 64-column map of
  16x16 tiles (row 1: sixteen of line, the star, black) scrolled 256 -> 0;
  `ppu_wideAuto` called it a half-maintained wide map and tiled the
  console's 256, so the right margin repeated the line and, at the end, the
  star, and the left margin was black. New policy `ppu_wideSweep`, set by
  `ws_konami_sweep` on that map's signature: the layer is shifted from
  -extraLeft to +extraRight in proportion to how much has been swept in
  (`ppu_layerShiftX`), and the map left of column 0 reads as column 0.
  `layers_capture` keeps the scroll less the shift so the easing follows
  what is drawn. `boot` 1-300 in 16:9: 0 differing; the star's leading edge
  advances 43 quarter-pixels a tick in steps of 10-12; the frontend's GPU
  pictures 187-232 have one unbroken lit run from column 0 to the star in
  every picture. `--png` writes the frame's video memory
  (`prefix.frame.vram.bin`) and prints each background's map and tile
  addresses, which is how the map was read.
- **The pre-game backdrops were choppy for their first fifth of a second.**
  Step spreading waited for two equal intervals: LucasArts stepped at 729,
  733 and was spread from 737; the title 1159, 1163, from 1167. Outside a
  level (`world` false) a move after a rest of 3-8 ticks is a step unless
  a different interval is known -- but not the first move since the screen
  went up (`fresh`), whose rest is only the picture's age: tried, and the
  level card (two ticks in sight, then 16 px a tick) crawled a tick and
  leapt. So in general from the second step -- which the user could still
  see. The backdrops are all BG3, and outside a level the game steps BG3
  on a counter and nothing else (`$80:938E` and `$80:953B` every 4th tick,
  `$80:9A1B` every 5th), so BG3's first move since screen-up is spread too
  (`LAYERS_BACKDROP_BG`, over `LAYERS_STEP_FIRST` = 4 ticks at least): now
  from 729, 1159 and, on the select, 1084. Frontend pictures on the user's
  path (`--widescreen 16:9 --skip-intro`, backdrop shift measured picture to
  picture): still for ticks 1-3, then the same shift in every picture from
  the first step at tick 4. In a level the rule is the old one: the
  background is the view there, and standing then walking is not a step.
  The test counts spreads cut short by a move and names the frame.
  **Open:** that counter shows the old rule misfiring *in* levels --
  `level21` and its kin, 1 to 45 a movie, `level25-2p` 2: BG1 there is a
  decoration stepping every 4th tick while the view stands, it is spread,
  and when the view starts to move the spread is dropped and the layer
  leaps up to three quarters of a step. Not new (the in-level rule is
  unchanged) and not fixed here.
- **The character select's edges flashed as it faded in (16:9).** Its
  wallpaper (BG3) comes up at scroll 0 and first steps on tick 5;
  `ppu_wideAuto` wraps a 256 map only once it has moved, so for five ticks
  the margins were the flat colour behind it, lighter than the wallpaper.
  Those frames are PPU pictures (sub screen in a fade), so the list's
  pictures never showed it; a movie made the way a player arrives (idle to
  the title, Start at 1300, 1420, 1540; select fades in 1469-1483) played
  in the frontend with `--dump-pictures` did. Outside a level a BG3 filled
  down both console edge columns (`ppu_columnFilledAt`) gets `ppu_wideTile`
  from the first frame. After: wallpaper in both margins from 1469. (A
  first guess -- "it comes up already scrolled", from `level1.zmv`, where
  it does -- was wrong for this path and is not in the tree.) The dump of a
  fallback frame is 2109 rows tall with the picture in rows 698-1409: the
  readback's size, not the picture's.
- **...and the select still started unsmoothed, because its fade-in was not
  a draw list at all.** "sub screen in a fade" refused every frame of it
  (1469-1482 on the player's-path movie): fifteen ticks of the PPU's
  picture, then smoothing. The refusal was about order -- the console adds,
  clamps, then dims; dimmed planes added clamp too late -- so the list now
  does it in the console's order: planes at full brightness when
  `layers_dim_late` (sub screen added, brightness < 15), and
  `LayersFrame.dim` applied to the finished picture, in `layers_render` and
  as a colour modulation in `present_layers_draw`. Layered from 1469, eased
  from 1470, wallpaper spread from its first step at 1474 over its true
  five ticks; fade-out layered too; 0 differing, 28 more frames within one.
  Frontend pictures 1467-1490: maxima 17, 34 ... 255, and all four pictures
  of every tick from 1470 differ. The step-spreading work before this was
  real but was not what was being seen on this screen: the first question
  on "not smooth at the start" is whether those frames are draw lists
  (`--motion` names the refusal).
- The draw list unmoved is still the PPU's frame: `level1`, `level21-spin`,
  `level25-boss` in 16:9, 0 differing. (The test runs to the frame it is
  given, not to the movie's end -- `0 99999` is four processes that never
  finish.)

### Smoothing, second attempt: the picture taken apart (2026-09-15)

The 240 Hz smoothing looked worse than the plain 60, and it jittered. Measured
rather than argued, with a probe over `level1`, `level25-boss`, `level9-weapons`
and a movie that sits on the character select:

- **A whole pixel is the floor.** Per-tick motion is 1 or 2 px: the camera
  moves (0,±1) or (±2,0) a tick depending on the level, walkers 1 px with a
  second step on masked ticks, the character select's film strips (0,1) every
  tick. `smooth_step` rounds to whole pixels, so a 1 px/tick move at 240 Hz
  steps at the second picture and holds for three -- the 60 Hz cadence with a
  phase shift, i.e. nothing. A 2 px move steps at pictures 1 and 3. **Different
  speeds step at different phases**, so a player (1 px) on a scrolling floor
  (2 px) is drawn moving against the floor, then with it, four times a tick.
  That is the jitter.
- **Matching sprites by looks was wrong 5% of the time** against ground truth
  from the display list (re-running the emitters per record): 2% of
  sprite-ticks were shown with a wrong delta, a one-pixel wobble on some sprite
  every few ticks; 1.3% found no predecessor and stepped at the tick boundary
  while their actor's other pieces eased -- tearing.
- **`ppu_renderFrame` costs 2.0-3.6 ms** on this machine, three times a tick,
  against a 4.17 ms refresh: the loop's `waiting` was 0.00 ms -- no headroom.

The fix is `src/layers.h`: take the frame apart once per tick (planes per
background and priority, the backdrop, sprites as cells in an atlas -- with
fixed-colour maths, brightness and the maths window baked in), and draw the
pictures as a *draw list* with the GPU (`src/present_layers.h`) on a target
several times the console's size, so a quarter pixel is a target pixel. The
sub screen is added where the console adds it by a masked additive blend on a
scratch target (`DST_ALPHA` factor). Sprites are moved by their *record*: the
port's `sprite_build_oam` now publishes `sprite_oam_owners` (record and origin
per OAM slot, with a serial so a stale table is not trusted); `--stock` falls
back to `smooth_match`.

Checked by `tools/test_layers.c` over the corpus: every movie, zero frames
differing by more than one; "within one" only where the sub screen is added
(five-bit add on the console, eight-bit on the GPU). Frames the list cannot
express -- forced blank, a sub-screen fade, mid-frame writes, mode 7 -- are
shown as the PPU drew them and counted. `--dump-pictures` read the renderer's
output back and found it pixel-identical to the software render on Direct3D 9
and 11 -- after it found the sub screen's mask op naming itself (a macro
argument evaluated after the op counter moved).

Two more things learned on the way. The PPU evaluates sprites for `line - 1`,
so OAM y lands on picture row y, one below where a background's line 1 lands
(593 pixels of the title's "START / PASSWORD" said so). And sprites hide each
other by OAM index regardless of priority bits; a draw list per priority
cannot say that, so the hidden pixels are removed from the hinder sprite's cell
at the tick's positions (`layers_occlude`) -- 37 frames of `level21`, 40 of
`level37`, 13 of `level5`, 5 of `level41` were differing before that and none
after.

Measured at 240 Hz, `level1.zmv --frames 1500 --paced`: 98.7% of pictures
within 1 ms of the 4.167 ms period, the loop now idle 1.3 ms a picture on
average, taking a tick apart 1.24 ms on the emulation thread. The one long
arrival (215 ms) is the level load, a 226 ms tick; the plain loop has it too,
at 500 ms.

**The first play-test still jittered, on walkers only**, and the movie tests
could not see it because they compare each tick's own picture, not the path
between ticks. `zamn_test_layers --track` prints where each piece is put in
each picture; one zombie's piece went 151, 154, 157, 160 | 166, 168, 170, 172
| 171, 174, ... -- a jump of six and then minus one at the tick boundaries.
The pieces were eased from the actor's origin delta, and a walk cycle's frames
put their pieces at different offsets from the origin, so every frame change
moved the start of the ease by a pixel. Now a piece is eased from the nearest
piece of the same record in the last tick (the origin delta only as the
fallback), and the same trace reads 151, 154, ... 172, 174, 176, 178, 180,
183, 186 with no seam. Also from that session: the frontend was not DPI-aware
(3072x1728 backbuffer on a 3840x2160 panel at 125%, stretched by Windows),
fixed with `SDL_HINT_WINDOWS_DPI_AWARENESS`; `--fullscreen` after `--frames`
lets a bounded run measure fullscreen; and the report prints the compositor's
dropped/missed counts when it composited anything, which fullscreen does not.

**The second play-test still jittered, in widescreen**, which no movie check
had run: the whole screen near a map's edge, items and weapons and neighbours
against a steady floor, zombies giving chase. Three causes, each measured with
`zamn_test_layers --widescreen 16:9` and its new pairing line:

- **The owner table is a tick ahead of the picture.** A frame of the core ends
  as vblank begins and the pass's buffer is DMA'd *in* that vblank, so the
  frame shows the pass before. Paired with the same tick's table, a piece was
  tagged with whatever record had its slot a tick later -- wrong whenever the
  slots shifted, which is what a crowd crossing in depth does. Holding the
  table a tick took the pieces with no near predecessor in their own record
  from 4,018 to 1,105 on `level25-boss`, and those paired with nothing from
  966 to 416. (`--no-hold` reproduces the old pairing, to measure against.)
- **Widescreen's own sprites have no record.** The pieces the pass drops for
  being outside the console's 256, and the items, weapons and neighbours on
  the ground with no actor, are put into parked OAM entries by
  `src/widescreen.h`. They were not eased at all, so they stepped once a tick
  against a floor that slid. They are now paired by looks among the last
  tick's recordless sprites -- 7,921 sprite-ticks on that movie.
- **At the ends of a map the margins trade width**, so the picture's origin
  moves up to two pixels a tick while the view stands still. The scroll was
  eased regardless: the whole screen slid back a pixel and a half and forward
  again, every tick. `dExtraLeft` now goes into every delta that lives in the
  console's coordinates -- the world planes read `+0` through frames 2824-2834
  of that movie, where they read `+6` -- and not into a layer pinned to the
  picture's edges (the status panel).

**Seen on the way, not chased:** with the port substituted (the default), a
movie that presses Start once at 1150 and then holds nothing leaves the
character select by itself around frame 1280 and is on the level card by 1400;
`--stock`, and the bare core in `zamn_headless`, sit on the select screen until
the next Start. Repro:

    build\zamn.exe "Zombies Ate My Neighbors.sfc" -m sel.zmv --frames 1400 --shot a.png
    build\zamn.exe "Zombies Ate My Neighbors.sfc" -m sel.zmv --frames 1400 --shot b.png --stock

with `sel.zmv` the standard Start-mashing boot half, `1150 Start`, `1158 -`.

### ...and the level 29 route, which is not a route-following problem (2026-08-03)

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

### The untaken list instead of the ranking -- two sites cleared, and two priced (2026-08-03)

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

### $82:BB0D was never a routine -- twelve thread bodies out of the level records (2026-08-03)

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

### $80:D1FF player_state_normal -- the player's ordinary frame, and a site that came back out (2026-08-03)

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

### $80:E86D floor_effect -- what the ground does to you, and eleven ways out of it (2026-08-03)

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

### $80:ADC8 and $80:ADF3 -- one tile's attributes, and the row under the row (2026-08-03)

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

### $80:8002 was $80:80AE all along -- the second hardware vector (2026-08-03)

The next row on the board was `$80:8002 init_ppu_regs`, 0.5% over ten calls, and
the write-up above said to check it for a spin first because three of the last
four single-digit call counts had been one. It is not a spin. `hotbytes.py`
printed **NO LOOP** over its 44 instructions, which is exactly right and exactly
not the point, because the range that answer applies to is not the range the
ranking was scoring.

`init_ppu_regs` ends at its `RTS` on `$80:80AD` and executes **630 instructions
over ten profiles** -- 63 a call, 0.0002%. The other 1,311,180 belong to
`$80:80AE`, and `$00:FFFC` holds `$80AE`. **It is the reset vector.**

Almost all of it is two bytes:

| byte | what | executions |
| --- | --- | --- |
| `$80:8116` | `MVN $7E,$7E` -- zero-fill bank `$7E` | 655,350 |
| `$80:813B` | `MVN $7F,$7E` -- copy the zeroes over bank `$7F` | 655,360 |

A block move on this CPU re-executes itself once per byte, so clearing 128 KB of
WRAM genuinely is 131,071 instructions and the profile is not exaggerating. It
is the machine coming up, once per movie, and nothing calls it -- so it joins
`$80:816C nmi_entry` in `BLOCKED`, and the doc's "three families of code that
runs and is never called" now names the first row **hardware vectors, 2**. The
remaining two vectors are inert: `$80:8000` is a bare `COP #$FE` and `$80:8209`
a bare `RTI`, both executed zero times in all ten profiles.

**Two things about this one are worth more than the correction.**

`reset_entry` was **already a symbol in `zamn.sym`**, named rounds ago, sitting
eight lines above `init_ppu_regs`. Naming it achieved nothing, because the
ranking takes its boundaries from the CDL's subroutine flags and not from the
symbol table, and no amount of reading the disassembly would have fixed that on
its own. The declaration is what counts.

And the "ten calls" tell pointed at the right row for the **wrong reason**. The
count was honest -- `init_ppu_regs` really is called ten times, once per reset --
and the tell has been reading a small call count as evidence the *routine* is
not what it looks like. Here the routine was exactly what it looked like and the
**span** was wrong. So the tell is better stated as: a single-digit call count
next to a large share means the share does not belong to those calls, and which
of the two ways that can be true is still a question.

Nothing was ported. The board is 630 instructions shorter and one row honester,
and the reset row now reads `!$80:80AE reset_entry ... 0 calls`.

**Next.** `$80:D1FF` (0.4%), and it has a wrinkle worth stating before the round
rather than after: the ranking says 726 calls and the entry byte executes
**27,698** times. `$80:D1EC` is `JMP ($D1EF,X)`, a player-state dispatcher, and
26,972 of those entries arrive through it. That is not a blocker -- `cosim_step`
intercepts on `pc == r->entry` and never looks at how the PC got there, and the
`RTS` returns to the dispatcher's caller either way -- but it is the first row
where the calls column undercounts the entries rather than overcounting the
work, and the standing check will have to be read with that in mind.

### $80:AD2B and $80:ACF6 -- where the map comes from, and one of them will not go in (2026-08-02)

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

### Two more spins at the top of the board, and a rule for spotting them (2026-08-02)

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

### Thread bodies: the second family, and the port's share was overstated again (2026-08-02)

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

### $82:8F93 boss_step -- how a boss too big for sprites walks into a wall (2026-08-02)

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

### $80:BF1B actor_notify_box -- the blast radius, as a routine (2026-08-02)

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

### Fifty-nine vblank jobs, and 1.1 million instructions the port was not doing (2026-08-02)

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

### $80:B3F1 actor_snap_to -- the last pixel, and two things that went wrong (2026-08-02)

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

### $80:B379 actor_aligned -- the other question the same enemy asks (2026-08-02)

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

### $80:C8B8, and the first attribution fix to move a published number (2026-08-02)

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

### $82:8014 / $82:8069 boss_bg_queue -- the boss that is not made of sprites (2026-08-02)

**Routines seventy-two and seventy-three, ported together because they are the
same routine twice with one difference.** Both passed first run -- 427 and 598
calls on `level25-lane`, 0 diverged, and 903 and 1,126 over that movie's full
9,400 frames -- and neither has a single byte in the disassembly.

They draw level 25's giant baby. A 65816 will put 128 sprites on a screen and no
more than 34 on a scanline, nowhere near enough for a figure that size, so the
game does not use sprites for it at all: it keeps **four animation frames as raw
tilemap words**, 14 tiles by 20 rows, and re-uploads one of them into BG1's
tilemap every frame, moving the whole layer with the scroll registers.

**The difference between the two is one loop, and it costs a quarter of a
frame.** `$82:8014` points each DMA job straight at the ROM: 11,565 master
cycles. `$82:8069` cannot, because the figure it wants does not exist -- it
builds one, copying each row **backwards** into a staging buffer at `$7E:5736`
and toggling bit 14, the tilemap X-flip bit, of every word on the way past. You
need both halves or you get a figure made of correctly-ordered backwards tiles.
Eight instructions, 280 times, once per tile: **89,008 master cycles**, close to
a quarter of an NTSC frame's 357,368 and the largest cycle budget in the
registry by a factor of two. What the cartridge gets is the other facing free -- four frames one way
is 2,256 bytes, and the mirror is why that is not 4,512.

**The narrowest cycle spread in the registry, on both.** 11,528..11,686 and
88,980..89,256 -- 1.4% and 0.3% -- because nothing about either routine varies
except which of four equally-sized figures it was pointed at. Every other entry
spans at least a factor of two and most span a factor of fifty.

**Neither has a branch worth marking.** Between them they contain three
conditional branches and all three are loop backs; the figure's header decides
how long they run and nothing decides what they do. First pair in the port to
add **no coverage sites at all**, which is the coverage principle working in the
direction it less often works in. The decisions are one level up, at `$82:892E`,
which reads the facing flag and calls one or the other.

**4.44% of one movie and 0.00% of the other nine.** The most lopsided row the
ranking has produced: every instruction of the pair's 1.1% comes from
`level25-lane`, bar 508 in `level21-bubble` -- one call of the cheap one. It is
not a hot routine, it is a routine that is *only ever* hot, on the levels that
have one of these figures; nine call sites across banks `$82` and `$83` reach
the pair, so level 25's baby is not the only one. A corpus with more boss movies
would rank this much higher and one with none would not show it at all.

**Both routines were `.db`, so now there is a tool.** Third round running that
something this project needed was inside a `.db` run -- the camera scroll pair,
then the writer of the camera-hold bit, now these two. `tools/dis816.py` is a
plain forward 65816 decoder over the ROM image with `SEP`/`REP` width tracking
and nothing else, and the rule it leaves behind is: **before porting a routine,
decode its bytes, not its listing.**

**The one movie that parts, settled.** `level25-lane` parts under `run` at pass
3496 -- exactly where it did before this round and the round before that -- and
the README already records that its frame-6100 screenshot does not match
stock's. Four framebuffers at frame 6100 settle whose fault that is: stock and
**only this pair** are byte-identical to each other, and **everything but this
pair** is byte-identical to **everything**. Adding the pair to a full
substitution changes not one byte, so the divergence is entirely older than this
round. Stopping both at frame 2600, before the parting, also gives identical
framebuffers; and the native frame matches no stock frame within two of 6100,
which is worth saying because "they parted" is easy to hear as "they are a frame
apart". They are not -- they are different playthroughs. The other four
framebuffer checks are unchanged. *(Corrected next round: it is not the only
movie that parts -- `level21-bubble` parts at pass 1152, and always has.)*

Getting that table cost a one-line fix: `zamn.exe` silently capped `-r` at 32,
so asking for "everything except two" dropped the 33rd flag and then died with
`unexpected argument 'enemy_cdde'`. It is 128 now, and overflowing it says so.

**Corpus: 10,486,161 calls checked across 42 movies, 0 diverged; branch coverage
262 of 340, 78 untaken; census still empty.** Coverage did not move, which for
once is the right answer rather than a gap -- there was nothing to add. `run`
substituting the pair alone leaves no live-state difference on any of 6,089
passes on `level25-lane`, and the control there is identical at all of them.

**The standing check, exact for the eighth time.** 201,357,517 less 197,920,067
is 3,437,450, against a measured span of `$82:8014..$82:80E0` of exactly
3,437,450. Subsumed (175) and both-sides (40) unmoved, which is what a leaf pair
whose only callee was already ported should do.

**63.1% strict, 74.6% call-weighted; 70.8% and 83.7% with the waiting out.**

**Next**, the top portable row is `$80:C8B8 dma_to_vram` (1.0% over 4,557),
followed by `$80:B379` (0.7% over 7,779).

### $80:A93F camera_follow -- the top of the chain, and the 1.6% it was for (2026-08-02)

**Routine seventy-one, and the one eleven routines were read bottom-up to
reach.** It passed first run -- 5,280 calls on `level45-race`, 0 diverged, and
94,784 across seven movies with nothing to fix. It picks the point the view
should centre on (one player, the other, or the midpoint of the two) and then
moves the camera towards it.

**One pixel, whatever the distance.** `ASL A : BCC` is the entire movement
decision: the shift's *result* is discarded -- nothing reads A again before the
`JSL` -- and only its carry, bit 15 of the delta, is used. The camera is told
which way to go and never how far. That is the mechanism behind the thing anyone
who has played the game has seen: the view drifts after the players rather than
snapping to them, and never catches up while they are running. The delta is
computed in full first, which looks like waste until you notice this runs 146,000
times over ten movies. One pixel a frame *is* the camera speed.

**Three details worth transcribing carefully.** The midpoint's `LSR A` shifts A
alone, so a sum over `$FFFF` loses its carry rather than shifting it back in. The
two consecutive `SEC`s are load-bearing, because `SBC #$0080` can borrow. And
`$0084` is not the centre of the screen -- 128 across is, but 132 down is eight
lines below the middle of a 224-line one, because the playfield sits under a
status bar.

**A `PHD` routine, so its flags describe the caller.** All four exits are `PLD :
SEC : RTL`: carry is set on every one and says nothing, and N and Z are the
caller's direct page. Two of the three register outputs are leftovers -- A is the
Y delta or zero, X is that delta unless a scroll routine overwrote it, and Y is
`$0006`, the index the record read left behind.

**The guard had to ask about both strips at once.** A call can reach one X scroll
and one Y scroll, and the second buys its strip out of what the first left, so
asking each question separately would answer yes twice to a call that can only
afford one.

**Bit 14 of `$26`, and a grep that was a lower bound.** `camera_follow` opens
`BIT $26 : BVS`, and `$80:9E7B vram_queue_flush` opens with the same test, so bit
14 is a deliberate freeze of both. Searching all four disassembled banks for a
writer turns up none, and it was tempting to write that down as a finding -- *a
switch still in the ROM with nothing left to throw it*. It is wrong. Scanning the
ROM **image** for the eight opcodes that can write `$26` finds `$80:AB8D  LDA
#$4000 : TSB $26` and `$80:ABCC ... TRB $26` immediately, around a runtime block
writer that stamps 64x64 pixels of map into the tilemap and holds the view still
while it does. Neither instruction is in the listing as code; the whole routine
is one `.db` run. **Three times this round a fact about this ROM was hiding in a
`.db` block** -- `$80:A70A`, `$80:A816`, and this -- and all three were found by
going to the bytes.

So `follow_held` stays as the one untaken site of the eleven added: reachable,
and needing an input that repaints part of the map mid-level. `hotbytes.py` gives
its setter zero executions across all ten profiles.

**And the row that was going to be next is not a routine.** With the camera done
the ranking's top portable row read `$81:81A2`, 1.6% over 79 calls. Reading it
first took forty-seven bytes: it ends at an `RTS` and executes **3,234**
instructions in ten profiles. All 4,671,086 of the rest belongs to what starts at
`$81:81F6` -- unlabelled, opening `PEI $02 : PLB` into a loop around `JSL
thread_yield`, so **a thread body**, entered by the scheduler and never by a
`JSR`. With the boundary declared in `JUMP_ENTRIES` the row's call count reads
**0**, which is the ranking saying so itself. Seventh time nearest-preceding-entry
attribution has misplaced work here, fourth time the fix was one line. The share
is unaffected -- an attribution boundary moves credit between rows, not into the
total -- but the reading list is.

**10,480,450 calls checked across 42 movies, 0 diverged; branch coverage 262 of
340, 78 untaken, census still empty** -- from 10,062,362 and 252 of 329. `run`
substituting it alone is clean at every one of 3,789 compared passes with no
timeline parting; `run` with everything is 2,389 of 2,389 on level 1; and the
frontend's four framebuffers stay byte-identical.

**The standing check, exact for the seventh time.** 192,833,966 to 197,920,067 --
5,086,101 instructions, against a measured span of exactly 5,086,101. Subsumed
and both-sides counts unmoved.

**62.0% strict, 73.5% call-weighted; 69.6% and 82.5% with the waiting out** --
the largest single-routine move this phase has had, and the camera chain is
finished.

**Next**, the real top portable rows are `$82:8069` (1.1% over 1,126) and
`$80:C8B8 dma_to_vram` (1.0% over 4,557).

### The four scroll routines, and two that were never in the listing (2026-08-02)

**Routines sixty-six to seventy, and with them everything under `$80:A93F` is
in.** `$80:A599 camera_split_x` plus the four scroll routines -- `$80:A68B`
left, `$A70A` right, `$A789` down, `$A816` up -- all passed first run. Each has
the same three parts: refuse if the camera is already against that edge of the
map; otherwise move it **one pixel** and the sub-tile remainder with it; and
every eighth pixel, when the remainder wraps, step the tilemap cursor, copy the
strip of map that has just come into view, and put it on the VRAM queue.

**Two of the four are not in `analysis/bank_80.asm` as code at all.** `$80:A70A`
and `$A816` have no label and no mnemonics, only `.db` runs -- nothing ever
proved those bytes were instructions. Decoding them by hand is what shows why it
was worth doing: **each is its partner byte for byte with six substitutions** --
`DEC A` against `INC A`, a `BEQ` against zero versus a `CMP $B8`, `AND #$0007 :
CMP #$0007` versus `AND #$0007` alone, the cursor stepped down versus up, and
the near tile-window word and cursor versus the far ones. So `src/port/camera.c`
holds one X scroller and one Y scroller and a table of exactly those six
differences, which is the shortest way to write a mirror down such that the
mirroring is *checkable* rather than something a reader diffs by eye.

**The two boundary tests publish completely different flags for the same
event.** Going forwards a pixel crosses a tile boundary when the low three bits
come out zero; going backwards, when they come out seven. One is `AND #$0007 :
BNE` and leaves N clear with the limit compare's carry still standing; the other
is `AND #$0007 : CMP #$0007 : BNE` and leaves N set and carry clear.

**`$80:A599` is `$80:A588` with four times the work, and the tilemap's shape is
why.** A background tilemap is 64 columns wide but the PPU stores it as **two
32x32 screens `$400` words apart**, so a row crossing the seam is not one
transfer with a wrapped address, it is two transfers to unrelated places. The Y
splitter hands back two counts and lets its caller decide; this one hands back
two complete transfer descriptions, and its two branches are the same pair of
runs written in opposite orders. Its four outputs look like they should differ
between the branches and do not: both close on `LDA #$0042 : SEC : SBC <the run
this branch measured>`.

**Six direct-page words, three meanings.** `$5C`..`$66` are written by whichever
splitter ran. `$80:A588` writes two of them as tile counts; `$80:A599` writes
all six as transfer descriptions; and the X scrollers use `$60` for a third
thing again, the strip they just bought off the allocator -- which they can only
do *because* the Y splitter they call does not write it. It reads as an aliasing
bug until one notices no scroll routine ever calls both splitters.

**The X pair and the Y pair split their work in opposite shapes.** A column is
32 tiles and the tilemap is 32 rows tall, so a column always wraps: the X
scrollers copy **twice** into one buffer of their own and queue **one**
transfer. A row is 33 tiles across a tilemap 64 wide in two screens: the Y
scrollers copy **once**, with `$80:A61D` allocating for them, and queue **two**
transfers at unrelated addresses. Two details a natural transcription gets
wrong: **`ADC $5E : ADC $1B7E` has no `CLC` between the adds**, so the base add
carries the offset add's out; and **the unconditional column copy cannot be the
65,536-iteration case**, because `32 - (cursor & 31)` is 1..32 and never zero,
which is why only the other copy has a `BEQ` in front of it.

**The first routines in the registry that take carry as an input.** `$80:A68B`
and `$A816` open `LDA $1B6A : BEQ out` with no `CMP` anywhere on that path, so a
camera already against the near edge returns **the caller's carry** -- an output
the routine never computed. All four take it in; the forward pair are handed it
and ignore it, because a shim passing `false` would be asserting something about
the caller rather than about the routine.

**A 180x cycle spread that did *not* behave like the LZSS leaves.**
`camera_scroll_left` costs 94 cycles to 17,032, the widest spread in the
registry -- and worse, which exit a movie takes is a property of the movie:
`level49` pins the camera against the left edge for its whole length and
contributes 13,560 calls at 94 cycles each, dragging the call-weighted mean to
394, a value no single call has ever cost. That is the shape of the argument
that made the LZSS leaves `verify_only`, and the answer here is the opposite,
which is what the flag actually turns on: the LZSS leaves are called a million
times **inside one multi-frame decompression**, so their errors accumulate
against a single deadline; these are called a few thousand times across a whole
movie, each call in a different frame that ends at a `WAI`. The errors have
nowhere to accumulate.

**10,062,362 calls checked across 42 movies, 0 diverged; branch coverage 252 of
329, 77 untaken, census still empty** -- from 9,843,730 and 244 of 321. **All
eight new sites were taken**, so the untaken list did not grow by one: eight
sites added, eight reached. `run` substituting the five alone on `level45-race`
is clean at every one of 3,989 compared passes with no timeline parting; `run`
with everything is 2,389 of 2,389 on level 1 and parts at pass 3496 on
`level25-lane`, exactly where it did before this round; and the frontend's four
framebuffers stay byte-identical.

**The standing check, exact for the sixth time.** The native total moved from
192,146,816 to 192,833,966 -- 687,150 instructions, against measured spans of
202,382 + 160,618 + 155,691 + 138,776 + 29,683 = 687,150 exactly. Subsumed and
both-sides counts did not move (175 and 40), which is the *expected* answer
rather than a lucky one: these five have one caller between them and it is not
ported, so there was no subsumption to collect.

**60.4% strict, 71.9% call-weighted; 67.8% and 80.7% with the waiting out.**

**Next**, `$80:A93F camera_follow` itself -- 1.6% over 146,000 calls, no loop,
every callee now ported, and the whole reason the chain was read bottom-up.

### The camera chain, bottom-up: seven routines and two unreachable branches (2026-08-01)

**`$80:A93F` is the top portable row on the work ranking -- 1.6% over 146,000
calls, and `hotbytes.py` gives it a clean bill: every byte runs exactly once per
call.** It is camera centring, and it reads quickly. The target is one player's
position, the other's, or the **midpoint of the two**; the deltas are against
the live scroll at `$1B6A`/`$1B6C`; and each axis dispatches by sign into one of
two tilemap scroll routines, which move the camera **one pixel** and push a new
column or row into the VRAM queue every eighth. That is why the view drifts
after the players rather than snapping to them.

**It cannot be taken yet, and the reason decides the order of work.** A
substituted routine has to do everything the ROM's does, and there is no way to
call back into the ROM half-way through -- so `$80:A93F` needs all four scroll
routines, and `$80:A68B` alone calls five more. The alternative is a guard
serving the calls that need no scroll, somewhere between 41% and 69% of them;
that would put the first entry in a decline census empty for several rounds, and
would credit the ranking 1.6% for a routine doing half of it. Bottom-up is the
honest order, and `src/port/camera.h` now carries the whole chain with each
link's share against it.

**`$80:AD1C tilemap_tile_addr` -- routine fifty-nine, the floor of the stack.**
Fifteen bytes, no calls, 42,167 of them, no loop: `TXA : ASL A : PHA / TYA : ASL
A : TAX / LDA $7E4328,X / CLC : ADC $01,S / PLX : RTL`. `$7E:4328` is
`W_TILE_ROW_BASE`, which `port/terrain.h` has documented since the first terrain
routine, so it lives there rather than in a file of its own. **Three registers
come back and no two from the same place**: A from the `ADC`, X from a `PLX` of
the column *already doubled* -- not a restore, and `$80:A5E5` depends on it --
and N and Z from that `PLX`, so they describe the doubled column and not the
address. Carry is the `ADC`'s and survives untouched. 1,037 calls on level 1 and
9,754 on level 25, 0 diverged, first run.

**`$80:A5E5 tilemap_copy_column` -- routine sixty.** A vertical strip of the
expanded map copied into the caller's buffer, source stepping by
`W_TILEMAP_ROW_BYTES` and destination by two. **303 calls on
`movies/level45-race.zmv`, 0 diverged, both new sites taken, first run.**
`stack_bytes` is 7 and it was predicted before it was measured: its own `PHA` is
2, the `JSL` is 3, `$80:AD1C`'s `PHA` under it is 2.

**`$DC` turns up doing its third job, and this is the one it was named for.**
`CMP $DC : BCS` forces bit 13 -- the PPU's background priority bit -- on every
tile whose nine-bit index is below `W_TILE_PRIORITY_BELOW`. `src/assets/level.h`
had that field as a draw-time flag since Phase 2; `$82:90F7` showed this morning
it is also a collision threshold; here is the draw-time half in the flesh. It is
not two rules that happen to agree -- it is **one number read twice**, which is
exactly why the tiles drawn in front of the player are the tiles nothing may
stand on.

Two things transcribed rather than tidied: the loop is `DEX : BNE`, so a count
of zero means **65,536 iterations and not none**, and its flags are for once the
ones a reader would guess -- `ADC $54` is last, so N, Z and C describe the
pointer it returns. Both other links in this chain end on a pull, so this one
breaking the pattern is worth noticing.

Its cycle spread, 1,526..14,250, is unlike any other in the registry: not the
bus and not a branch but **the count**. The same shape of number that made the
LZSS leaves unsubstitutable, at a thousandth of the call volume -- and at 303
calls it is harmless, which the checks confirm.

**`$80:A401 tilemap_buffer_alloc` -- routine sixty-one, and a guard used as a
statement.** Twenty-one bytes of bump allocator over the `$7E:4B28` arena that
every tilemap strip comes out of; neither `$CA` nor `$CC` had a name before this,
in `zamn.sym` or `docs/wram-map.md`, so they are now `W_TILEMAP_ARENA_NEXT` and
`W_TILEMAP_ARENA_LEFT`. **Its one branch is a spin, not a retry** -- the `BCC`
goes back to a reload of the same unchanged `$CC`, waiting for somebody else to
hand the arena back -- and in ten traces it is never taken once: every byte in
the routine runs exactly 3,977 times against 3,977 calls.

That branch is awkward to own. Implemented faithfully it is an infinite loop in
C, because nothing inside the port can change `$CC`; ignored, it is the port
quietly disagreeing with the ROM; as a coverage site it could never be taken,
and `coverage.h` says in as many words that a site no corpus can reach dilutes
the number the file exists to keep. **So it is a guard** -- declined and
censused if it ever happens, costing nothing while it does not. The `decl.`
column reads 0 on every movie, which is the profile's fact arrived at from the
front. 227 calls on `level45-race`, 0 diverged, 0 declined, first run.

**`$80:A61D tilemap_copy_row` and the last three leaves -- routines sixty-two to
sixty-five, and with them the whole layer the four scroll routines stand on.**
`$80:A61D` is `$80:A5E5`'s twin, but it walks a *row*, so one index does both
ends where the column version needed a source stepped by the row stride, and it
buys its own 66-byte strip from the arena instead of being handed one -- so it
carries the allocator's guard too. Its outputs are the least summary-like in the
registry: A and carry are whatever the thirty-third tile happened to be, N and Z
belong to a `DEY` that has already run off to `$FFFE`, and X to the allocator's
`PLX` from before the loop began. Its two sites took 238 and 521 hits, summing
to 759 = 23 x 33 -- the loop count `hotbytes.py` had already predicted as "33.0x
the entry", confirmed from a third direction.

`$80:A54D` recomputes the tile window (27 instructions, no branches; the masks
are the PPU's 64x32 tilemap, not the level's size), `$80:A588` says where that
tilemap wraps relative to the cursor, and **`$80:9E6D` is the one worth
pausing on**: it opens `BIT $26`, and `BIT` against memory sets N from bit 15 of
the *operand* but Z from *A AND the operand* -- so the Z it returns on that exit
is a fact about the caller's accumulator, which the routine never loads and has
no other use for.

**The registry outgrew its enable mask at sixty-five, and the assert written for
that moment caught it.** The registry had outgrown a *32*-bit mask once before
and the symptom then was `verify` reporting zero calls checked on every movie --
a harness that had stopped measuring rather than a port that had stopped
working. `Cosim::enabled` is now a bitset, so the next raise is one line. And
widening it found the same bug still live elsewhere: `main_sdl.c` counted its
enabled routines with a `uint32_t` cursor over a 64-bit mask, so the frontend
has been undercounting its own routines since the registry passed 32.

**One of the five new coverage sites came back unreachable, and got a different
answer from the last one.** `request_empty` was the only untaken site of the
five; looking rather than shrugging settled it -- all four of `$80:9E6D`'s call
sites store a *nonzero* `$CE` in the instruction immediately before the `JSR`,
so the queue is never empty when the routine is asked whether it is. The site
came out on the rule `coverage.h` states about itself; the code stays, because
it is three correct lines and the ROM has them. That is the second unreachable
branch this chain produced and deliberately the second different treatment: the
allocator's spin became a **guard** because the port cannot implement it, and
this becomes a **comment** because the port implements it perfectly well and
nothing will ever call it.

**9,843,730 calls checked across 42 movies, 0 diverged; branch coverage 244 of
321, 77 untaken, census still empty** -- from 9,647,058 and 238 of 315. `run`
substituting each alone is clean at every compared pass with no timeline
parting, `run` with everything on level 1 is 2,389 of 2,389, and the frontend's
four framebuffers stay identical.

**The standing check was exact five times running** -- 463,837 / 1,115,377 /
55,678 / 450,376 / 154,674, against measured spans of exactly the same, with the
subsumed count never moving. **And on the fifth it caught my own arithmetic**:
the total moved by 154,674 against a prediction of 184,357, and the prediction
was wrong because `$80:A588`'s share had been read off a `hotbytes.py` span of
`$A588..$A5E5`, which swallows `sub_80A599` whole. Its real span is 19,456, and
107,379 + 19,456 + 27,839 = 154,674 exactly. The nearest-preceding-entry hazard
this project has twice caught in the tooling turned up a third time **in the
arithmetic used to check the tooling** -- a good argument for a check being
cheap enough to run every time rather than clever enough to skip.

**60.2% strict, 71.7% call-weighted; 67.6% and 80.5% with the waiting out.**

**Next**, the four scroll routines -- `$80:A68B`, `$A70A`, `$A789`, `$A816`,
0.20% between them and **every callee they have now ported** -- and then
`$80:A93F` itself, which is the 1.6% the whole exercise was for.

### The LZSS leaves, and the metric inflating in exactly the documented way (2026-08-01)

**`$80:CDDA lzss_read_byte` and `$80:CDEB lzss_write_byte` — routines
fifty-seven and fifty-eight, and the first registered without their caller.**
`$80:CD20 lzss_decompress` has been written-but-unregistered for rounds because
one call is ~440,000 instructions and an NMI always lands inside it. That
argument is about the body. Its two leaves are the opposite shape in every
respect: 8 and 5 instructions, 1,071,108 calls across the corpus, **2.6% of
every instruction the game executes** — more work than any single routine left
on the ranking. Interception is per call site, so the ROM runs the body and the
port answers each `JSR` out of it; the six call sites in the trace are all
inside that one body, so neither needs a guard. **Both passed first run.**

Neither ends where it looks like it does: `$80:CDDA` closes on `INC $28` and
`$80:CDEB` on `INC $2C`, so N and Z describe *the pointer the routine just
advanced*, not the byte. The `PLD` trap in a third costume.

**9,647,058 calls checked across 42 movies, 0 diverged; branch coverage 238 of
315, 77 untaken, census still empty** — from 5,244,269 and 237 of 314.

**Substituting them broke the framebuffer test on every movie, and that is the
timeline law with a bigger lever.** `zamn.exe` against `--stock` had been
byte-identical on `level1`, `level1-2p`, `level29-fighting` and `level45-race`;
with these two in, all four differed and `run` on `level1` parted at pass 187
with stock on frame 1,049 and native on 1,052. `cycles` is one constant standing
in for a distribution — `lzss_read_byte` really costs 98 to 298 — and a million
calls packed inside one multi-frame decompression give the error no chance to
cancel. A level load lands three frames off, and a `.zmv` applies its inputs by
frame index. **No budget fixes it and the failed attempt is the argument**: the
mean is the mean by construction, so what is left over is variance and a
constant has none.

So both are **`verify_only`** — checked on every call, all 128 KB, never
substituted. That is the flag's second reason and quite a different one from
`apu_send`'s: that routine cannot be substituted because it has to *wait*, these
two because they cannot keep *time*. All four framebuffers are identical again,
which is the point — that check is the only end-to-end evidence in the project
involving no harness at all.

**And the metric inflated again, exactly the way the last inflation predicted.**
Registering them moved the native total by 8,382,014 instructions where the two
routines execute 7,113,522. `lzss_write_byte` is five instructions ending at its
`RTS` on `$80:CDF3`; `$80:CDF4` starts a different routine, reached by a jump,
opening `JSR $D13A` and `JSL thread_yield`. Attribution by nearest preceding
entry credited those call edges to the five-byte leaf — so the moment it counted
as ported, `$80:D13A`, `$80:D1EA`, `$80:D01B` and their closures were subsumed
for free: 13 routines, 1.28%. One line in `JUMP_ENTRIES`, and with the boundary
declared on both sides the total moves by **6,474,904** against a predicted
697,920 x 5 + 373,188 x 8 = **6,475,104**. Subsumed count does not move at all.

It restates the last round's figures too, because the boundary was missing then:
the call-weighted estimate was **70.2%**, not the 70.9% recorded, and 78.8%
rather than 79.6% with waits out. The strict number is unaffected — it counts
the entries themselves, which the boundary does not touch.

**59.5% strict, 71.0% call-weighted; 66.8% and 79.7% with the waiting out.**

**Next**, `$80:A93F` is the top portable row at 1.6% over 146,000 calls, and
`hotbytes.py` says NO LOOP — every byte exactly 1.0x its entry. It is camera
centring: the midpoint of the two players, or one of them, minus the current
scroll, dispatching into four tilemap scroll routines. Those four are only 0.20%
between them, but `$80:A68B` alone calls five more (`$A401`, `$A54D`, `$A588`,
`$A5E5`, `$9E6D`), so full coverage pulls in the whole column streamer. A guard
that serves the roughly half of calls needing no scroll is the cheap version.
`$81:81A2` is 1.6% but only 79 calls with a real loop at 2,133x its entry.

### `run` was measuring the wrong thing, in three separate ways (2026-08-01)

**`zamn_cosim run` on `movies/level25-lane.zmv` now reports 3,485 of 3,485
compared passes with no byte of live game state differing, all fifty-six
routines substituted.** Nothing in `src/port/` changed. The last round filed
this as "two causes, one certain, one open"; both were wrong, and the way they
were wrong is the result.

**The stack region was missing sixteen of the twenty-four stacks.** The harness
waived `$7E:1000-$7E:12FF` as "the stacks", a constant taken from
`docs/wram-map.md`'s *"`$7E:1120-$7E:114F`, 48 B, active thread stacks"* — which
came from a trace, and a trace only sees the slots the run spawned. It had seen
**one** stack and described it as all of them. `$80:830E` is a 24-entry table of
initial stack pointers: 24 values exactly 48 bytes apart, block `$7E:0CF6`-`$1175`,
and **sixteen of them sit below `$1000`**. So the mysterious "Cause B" bytes at
`$7E:0FB3` and `$7E:0F04` were stack residue in slots 19 and 17, as inert as the
residue at `$1021` the harness was already waiving. `cosim_init()` now reads the
table out of the ROM instead of trusting prose.

**`$7E:0004` is not state.** `$80:819C` writes `nmi_saved_sp` with `TSC : STA
$04`; `$80:81EB` reads it back with `LDA $04 : TCS` seventy-nine bytes later,
and nothing else in the game reads it. Between two NMIs it records where the
last NMI landed, nothing more. It is waived and **counted** — it is the only
visible measure of the cycle budget being an estimate, and `run` now prints it,
rare and small: `terrain_out_of_bounds` 6 passes of 9,389, `terrain_blocked` 2
of 6,057, both by at most 7 bytes of stack — one `JSL` deep.

**What is left is not a bug and cannot be fixed.** At pass 3,496 one core is on
frame 2,685 and the other on 2,684: a scheduler pass whose work overruns vblank
takes two NMIs, on a heavy level that is decided by a few hundred cycles, and a
substituted core spends a different number of cycles *by construction*. From
there the old report was comparing frame *N* against frame *N+1*. Realigning on
`$16` was tried and fails informatively — it costs the lagging side a scheduler
pass and puts `sched_tick` out by one instead. The two clocks genuinely
disagree, because one core really did run a pass in two frames. `run` now stops
comparing there and says so.

**Both of the last round's exclusions were sound experiments aimed at the wrong
number.** They measured *the first difference*, which is dead stack. Against the
number that decides the outcome — the pass the timelines part — `terrain_blocked`
at `.cycles` of 1,400, 1,591, 1,900 and 3,000 all part at **pass 6,068**, and so
does the four-line push repair, which moves only the first difference. **Cause A
is therefore not a cause**; it is cosmetic, and the "principled repair belongs
in `native_return`" plan is withdrawn rather than deferred. The general form is
worth keeping: **an experiment that does not move the number is only evidence if
that number is the one that matters.**

`run` with everything substituted, all of WRAM every pass: `level1` 2,389 of
2,389, `level9-weapons` 8,989 of 8,989, `level25-boss` 7,589 of 7,589,
`level29-fighting` 5,989 of 5,989, `level25-lane` 3,485 and then the timelines
part — no live state differing on any of them. `-r none` stays clean at all
9,389, which is what makes the parting attributable to substitution.

**The frontend agrees, with no harness involved.** `zamn.exe` against `--stock`
produces a byte-identical final framebuffer on `level1`, `level1-2p`,
`level29-fighting` and `level45-race` at 6,000 frames, and **not** on
`level25-lane` at 9,400 — the same event `run` names at pass 3,496, arrived at
independently. The corpus is unmoved by any of this: **5,244,269 calls across 42
movies, 0 diverged; 237 of 314 sites, 77 untaken; census empty.**

**Next**, the ranking by work is unchanged and now unblocked: `$80:A93F` (1.8%
over 73,000 — camera centring, dispatching into four tilemap scroll routines),
`$81:81A2` (1.6% over 79), the three `lzss_*` helpers, `$80:C8B8 dma_to_vram`
(1.0% over 4,557), `$82:8069` (1.1% over 1,126).

### A third footprint test, two spins, and `run` proving less than claimed (2026-08-01)

**`$82:90F7 terrain_blocked_wide` — routine fifty-six.** It is `$80:AE14` with
five tiles across instead of three (a 40x16 box, `SBC #$0011` for the origin),
the loop written out ten times, and a nine-bit tile mask instead of ten. **46,560
calls on `movies/level25-lane.zmv`, 0 diverged, all five new sites taken, first
run.**

**It also identifies a field `src/assets/level.h` has been half-right about since
Phase 2.** Before the attribute word is fetched at all, `CMP $00DC : BCC`
refuses any tile whose index is below the level record's `+$26` — the field
`level.h` calls `priority_below` and describes as *"a draw-time flag, not part
of the expanded map"*. It is also a collision threshold: **the tiles the game
draws in front of the player are exactly the tiles it will not let something
stand on**, and one 16-bit field does both jobs. Its caller `$82:8F93` is a
placement search, so refusing an overhead tile is exactly right — a thing put
there would be invisible. Every corpus movie that reaches it is a level 25
movie; nothing on the other thirteen sampled levels enters it at all.

**Fifty-six routines. 5,244,269 calls checked across 42 movies, 0 diverged;
branch coverage 237 of 314, 77 untaken by every input, and the census is still
empty** — from 5,116,847 and 232 of 309.

**`$82:AC07` was 97.8% asleep.** It sat third on the ranking at 3.3%, and
9,673,566 of its 9,893,468 instructions are two `LDA $0016 : CMP #$0078 : BCC`
loops — the level loader holding for **120 frames**, once for the intro screen
and once after the block library decompresses. Into `WAIT_SITES`, and it leaves
the top thirty entirely. The waiting is now **10.93%** of every instruction the
game executes, up from 7.89%.

**That closes `$82:AB5B` three rounds late, and against what was written down.**
`docs/analysis-tools.md` has said for several rounds that it *"never executed at
all: CDL flag `00`, exec count 0, in every one of the ten traces"*. It executes
520 times. The flag was read out of `analysis/zamn.cdl` — a level 1 trace, which
genuinely never reaches it — and then asserted of the ten *profile* traces,
which are different runs and in which the flag is `69`: code, and a `JSR`
target, an ordinary sixteen-iteration loop over the sprite palette. So the whole
of that 3.3% row is 0.03% of real routine, 3.03% of deliberate silence, and
0.07% of everything else. **Never read a flag out of one trace and a count out of
another.**

**`tools/hotbytes.py`** now exists, because that check had been done by hand
three times and each time found a spin. Its test is *not* "is the profile flat",
which was the first thing written and is wrong — an unrolled routine with early
exits is not flat and that is healthy. It is that **the first byte of a routine
is its entry, so nothing in a loop-free routine can run more often than that
byte does.** `$82:90F7`'s worst byte is 1.0x its entry; `$82:AC07`'s is
218,452x.

**The native share is 57.5% of executed instructions and 70.9% call-weighted —
64.5% and 79.6% with the waiting out of the denominator.** Registering the
routine moved the native total by 6,595,659 instructions, which is exactly what
the routine executes and nothing more: the check the last round's `$80:A937`
inflation demanded, passing on its first real use.

**And `run` proves less than this file has been claiming.** Every substitution
check ever recorded here was `movies/level1.zmv`, and *"identical at all 2,389
compared passes"* was true of it. `movies/level9-weapons.zmv` at 9,000 frames is
identical at all 8,989 too, so length is not the issue.
`movies/level25-lane.zmv` is a different answer, and the same answer for **every
routine tried, including `terrain_blocked` which shipped two rounds ago**:
5,935 of 9,389 passes differing, against a `-r none` control that is clean at
all 9,389. It starts at **2 bytes** — `$7E:0F04`/`$0F05`, which the harness
itself labels *"a push the port never made"* — and grows to 7,135 by frame
9,400, by which point `$7E:0016`, the NMI frame counter, differs and the two
cores are running different frames.

It is **not** the cycle budget: changing one routine's by 5x did not move the
onset a single pass, and correcting another's by the 2% its own measurement
suggests changed the blast radius by 9 bytes in 7,135.

**There are two causes and one is now certain.** *Cause A is the pushes a
routine abandons* — `terrain_out_of_bounds` declares `stack_bytes = 0` and is
clean where a four-byte pusher is already differing. That was a hypothesis, so
it was tested: `CosimRegs::s` gives a shim the entry stack pointer, so four
lines make `terrain_blocked`'s shim write exactly what its `PHD` and `PHA`
write — and its first difference moves from pass 1466 to **pass 3496**, which
is where every routine that pushes nothing first differs. Cause A is real,
understood, and four lines to repair. *Cause B is what remains*: every
substituted routine, pushes or not, first differs at pass ~3,500 at `$7E:0004`
and a few bytes near `$7E:0FB3`, marked `*** unexplained ***`; by pass 8,356
`$7E:0016`, the NMI frame counter, differs and nothing after that is evidence.

**The Cause A repair is deliberately not applied.** One shim of fifty-six makes
the registry inconsistent, and the principled version belongs in
`native_return` — which knows `stack_bytes` but not what to write, so it wants
a description of the pushes rather than a count. That is worth doing once Cause
B is understood and not before. `verify` is untouched by any of it — 5.2M
calls, 42 movies, 0 diverged, all 128 KB compared after every call — so what is
in question is the shims, not the C.

**Next**, the ranking by work reads `$80:A93F` (1.8% over 73,000 — camera
centring, and it dispatches into four tilemap scroll routines), `$81:81A2` (1.6%
over 79), the three `lzss_*` helpers, `$80:C8B8 dma_to_vram` (1.0% over 4,557)
and `$82:8069` (1.1% over 1,126). But the substitution result above is worth
more than any of them.

### The step validator closes, and the metric turns out to be inflatable (2026-08-01)

**`$80:E450` and `$80:A8B3` — routines fifty-four and fifty-five.** They are the
first and last things `$80:E4C1` does: work out where the mover wants to go,
then ask the four tests whether it may. With these two, **everything `$80:E4C1`
calls is C**, and only the sequencing is still the ROM's.

**Speed in this game is not a number.** `$80:E450` reads a direction out of
`$24` — already doubled, so it indexes the tables directly — and every delta in
those tables is one pixel. What makes a thing fast is a *mask*, ANDed with the
frame counter, deciding whether to add the delta twice. Two rows of four:
cardinals get `$FFFF`/`$0001`/`$0000`/`$0000`, diagonals get
`$0001`/`$0000`/`$0000`/`$0000`, and `dir & 2` picks the row. So the fastest
class moves 2 pixels straight and 1.5 diagonally — a 0.75 standing in for
0.707, which means diagonal movement in ZAMN is about 6% too fast and has been
since 1993. It is one table lookup and no multiply, which in 1993 was the entire
argument. A `$FFFF` mask is "always" except one frame in 65,536, when the
counter is zero; nothing depends on it and the port reproduces it because
reproducing it is free.

**`$80:A8B3` is the co-op leash**, and it needed a new WRAM symbol to read:
`$D6`, which `$80:A8A4` fills with the task that was running when player A's
record was registered. Comparing it against the current task is how the routine
asks *which player am I*, and the answer selects **the other one** as the
reference. A step is allowed inside a 224x176 window around that reference —
and outside it, allowed anyway if it strictly shortens the Manhattan distance
between the two players. You can always walk toward your partner; you can only
walk away until the leash runs out. A tie is refused, by a `BEQ` one instruction
ahead of the `BCS` that would otherwise have allowed it.

**Both passed first run** — 467 calls on level 1, 0 diverged, no second attempt
on either.

**And two thirds of one of them had never been read.** `$80:A8CC` through
`$80:A936` — 107 bytes, both windows and the whole distance comparison — is
`.db` in `analysis/bank_80.asm` and in every one of its four successors, because
no traced movie had ever reached it: in a one-player game the routine returns
eleven instructions in. That C was written by hand out of a hex dump.
`movies/level1-2p.zmv` settles it — **11,100 calls, 0 diverged, and the only two
sites it misses are `speed_dir_still` and `tether_alone`**, so the far path is
not merely reached but exhausted, tie included. The two-player movies were cut
four rounds ago for a single branch in `actor_obstacle_at_point`. What they were
actually worth was this.

**Fifty-five routines. 5,116,847 calls checked across 42 movies, 0 diverged;
branch coverage 232 of 309, 77 untaken by every input, and the census is still
empty** — from 4,888,163 and 219 of 296. **All thirteen new sites are taken**,
which has not happened before: the untaken count did not move at all.

**Then the completion metric moved three points for eight tenths of a point's
worth of work, which is how the round found a hole in itself.** `$80:A937` is
four bytes — a `JSL` — starting one byte past `$80:A8B3`'s `RTL`, and nothing
calls it. Attribution is by nearest preceding entry, so it belonged to
`$80:A8B3`; and because `owner_of` maps a **call site** to the entry above it,
the edge leaving that orphan was filed as *`$80:A8B3` calls `$80:A93B`*. The
moment `$80:A8B3` became native the closure walked down through `$80:A93B` to
`$80:A93F` and the four tilemap scroll routines under it and marked them all
native too: **1.6 points with no C behind them**, in the routine the previous
round had explicitly set aside as a subsystem rather than a leaf.

The documented caveat was always "a jump-entered routine scores zero and its
neighbour scores too much" — the loud version, which somebody checks, because a
row that is too big looks too big. This is the quiet version: the neighbour
inherits the orphan's outgoing *calls*, and what those buy shows up in no column
of the report. `JUMP_ENTRIES` in `native_share.py` now holds `$80:A937` and
`$82:AC07`.

**The native share is 55.4% of executed instructions and 68.8% call-weighted —
60.2% and 74.7% with the waiting out of the denominator** — from 54.6/68.0 and
59.2/73.8 measured the same way. Exactly 0.8 points for 0.8% of the work, which
is the only evidence available that the fix is right. (The 54.1/67.3 the last
entry records was measured before `JUMP_ENTRIES` existed and is not comparable;
54.6/68.0 is that same tree re-measured today.) The check that finds the next
one belongs in every round from here: **port a routine and see whether the
number moves by more than that routine is worth.**

**The ranking also turned out to be three deep in things this phase cannot
finish.** Its top three rows are 12.6% of everything the game does, and not one
of them can go through the harness: `lzss_decompress`, `thread_yield` and
`vbl_queue_a_run` — which the last entry called *"the first
genuine, self-contained, per-frame leaf"* and which is nothing of the kind. It
is a **dispatcher**: it pushes a far return address, pushes a job's address out
of a WRAM table and `RTL`s into it, so verifying it per call means porting all
thirteen jobs that can be in the table first. Its 3.0% is not the jobs —
attribution stops at the next entry — it is the *scan*, sixteen slots from the
top down every frame whether two are live or none. `vbl_queue_b_run` is the
same shape.

**"Cannot go through the harness" is not "cannot be written", and the
difference matters.** `src/port/lzss.c` is a complete transcription of
`$80:CD20` that exists today and cannot be registered only because it outlives
a frame and the harness diffs at frame boundaries. `thread_yield` is the
primitive the harness measures scheduler passes *with*, so substituting it
would be substituting the ruler; the port has had its own coroutine machinery
since the round that solved that problem. The two
dispatchers are twenty lines of C each, and `nmi_entry` is an interrupt vector
rather than a subroutine. **None of the 15.2% is un-reimplementable. All of it
is outside the reach of one instrument**, and the
instrument verifies leaves called within a frame, so the things it cannot see
are precisely the things that *are* the frame: the scheduler, the dispatchers,
the decompressor that spans a level load. They were never going to fit, and
Phase 4 is defined as the point where that inverts.

It also shrank while being looked at. `$82:AB5B`'s 3.3% was on that list this
morning and is not on it now: adding `$82:AC07` to `JUMP_ENTRIES` moved the work
to the routine that actually does it, and that routine is ordinary. The blocked
total is **15.2%**, not the 18.5% the first pass reported, and the difference
was a labelling bug rather than any code changing.

What the rest of it does mean is a ceiling. Strict native share cannot exceed
about **85%** while Phase 3's definition of "ported" is "registered and diffed
per call", and the steps will get smaller from here. That is worth stating
plainly rather than discovering later. They stay in the denominator, because a completion estimate
that quietly excluded the hard parts would be worth nothing; the ranking now
prints `!` against them and says which ones they are.

**Next**, with the walls marked, the ranking by work reads `$82:90F7` (2.2% over
46,563 calls), `$81:81A2` (1.6% over 79), the three `lzss_*` helpers,
`$80:C8B8 dma_to_vram` (1.0% over 4,557) and `$82:8069` (1.1% over 1,126). Not
one of them is a movement or collision routine, which is the round's other
result: the part of this game the port knows best is now the part that is
finished.

### Three at once, because they were one thing (2026-07-31)

**`$80:AE14`, `$80:AE97` and `$80:B422` — routines fifty-one to fifty-three.**
Last round ported the third of the four tests `$80:E4C1` puts a proposed step
through; this round takes the other two, plus the enemy bodies' copy of the
first. Together they are 3.8% of the work the game does, and they were picked as
a *unit* rather than off the top of the ranking.

**What a terrain test actually reads.** The game never consults the level record
at run time and never looks at the block map at all: it expands the level once
into a 16-bit tilemap in WRAM bank `$7F` and from then on collision is two
indirections — tilemap entry, masked to ten bits, indexes a 512-word attribute
table, and the low bits of that word are what blocks. Neither pointer is a
constant, and three scalars fell out of watching level 1 load: `$B2` is 352,
`$B4` is 104, `[$BA]` is `$7E:611A`. Level 1's record says 22 by 13 blocks, and
22 x 8 x 2 is 352 while 13 x 8 is 104 — so those are the row stride in bytes and
the row count in tiles, exactly, and `$7E:4328` is a per-row table of byte
offsets so a row lookup is a read rather than a multiply.

**A field the asset pipeline had left blank.** `src/assets/level.h` has said
since Phase 2 that attribute bit 0 blocks movement and *"the rest of the word is
not yet identified"*. `$80:AE97` identifies bit 1: it is `$80:AE14` byte for byte
over the same six-tile footprint with `BIT #$0002 : BNE` in place of
`LSR A : BCS`, and its three callers are all enemy bodies. The obvious next
question is whether bit 1 is just a stricter bit 0, and reading all 55 levels'
tables out of the ROM says **no**: 216 tiles carry bit 0 without bit 1 and 519
carry bit 1 without bit 0. Neither set contains the other, so 735 tiles across
the game stop one kind of mover and not the other. Two further copies of the
loop test bit 2 and bit 12 and are not ported; `level.h` now says that too.

**And the first routine here with no `PHD`.** Every other shim in this project
takes N and Z from the `PLD` on the way out — the trap `actor_nearest` cost four
movies to learn. `$80:B422` never touches the direct page, so there is nothing
to take them from: they are whatever the instruction that decided the answer
left. There are **six exits and six different answers** — two `TXA`/`TYA` and
four compares — and four of them share a `SEC` that does not touch N or Z, while
a fifth skips it and keeps its own compare's carry. Publishing the routine's
last instruction everywhere would have been right one path in six. That is the
`PLD` lesson from the opposite side: the flags come from the last instruction
that *ran*, not the last one written.

**All three passed first run** — 2,122 calls on level 1, 0 diverged, no second
attempt on any of them.

**Fifty-three routines. 4,888,163 calls checked across 42 movies, 0 diverged;
branch coverage 219 of 296, 77 untaken by every input, and the census is still
empty.** Eleven of the thirteen new sites are taken, and **five of `$80:B422`'s
six exits are among them** — so the six-answers table is checked rather than
argued. The two untaken are `bounds_y_negative`, which needs a step that is off
the top of the map while being comfortably inside it horizontally, and
`terrain_attrs_bank_7f`, which exists to make an assumption falsifiable: the
attribute table's bank is `$7E` everywhere anyone has looked, the port handles
`$7F` correctly anyway, and if that site is ever taken the port will already
have been right.

**The native share is 54.1% of executed instructions and 67.3% call-weighted —
58.7% and 73.0% with the waiting out of the denominator** — from 50.6% and
64.1%. That is the largest single step this measurement has recorded, and it is
the unit-rather-than-ranking hypothesis paying off.

**Next**, the ranking by work now reads: `lzss_decompress` (5.3%, written and
unregisterable), `thread_yield` (4.3%, the thing the harness measures against),
`$82:AB5B` (3.3%, the known misattribution — its real code starts at `$82:AC07`
and is jump-entered), `vbl_queue_a_run` (3.0% over 60,940 calls), `$82:90F7`
(2.2% over 46,563) and `$80:A93F` (1.7% over 73,000). `vbl_queue_a_run` is the
first of those that is a genuine, self-contained, per-frame leaf.

### The third one, and the 6% that turned out to be a CPU waiting (2026-07-31)

**`$80:BFC8` is the fiftieth routine**, and it is `actor_at_point` again —
eighty bytes further down bank `$80`, same backwards walk of `visible_actors`,
same six-pixel window, same `PLD` then `SEC`/`CLC` at both exits — asking a
different question about the same board. **2.2% of every instruction the game
executes, over 41,232 calls.**

Reading its callers is what named it. Both are inside `$80:E4C1`, the
**movement step validator**: something wants to be at a new position, and that
routine puts the candidate through `$80:AE14`, `$80:A8B3`, this, and
`$80:B422` in turn, committing it only if all four agree. A set carry here
means *the step is blocked* — a collision test, not a search, which is why it
can stop at the first record it finds and never uses which one that was.

Two things separate it from its twin. **It takes no self argument**: instead of
skipping one record the caller names, it skips `$D2` and `$D4`, both players,
always — a walker may walk through a player and somebody else decides what that
costs. And **the id filter is much narrower**, with a fall-through that is the
only real trap in the routine: the `$0C`..`$33` band test does not decide on
its own, so an id between `$34` and `$5B` drops out of the bottom of it and is
put through the seven singleton comparisons as well. That is the sole reason the
`CMP #$0037` in that chain is ever live.

The third difference is the one that would have cost a round if it had been
missed: **A is an input even though the routine never reads it.** There is no
`STA` on the way in and nothing on the empty-board path writes A, so a caller
that asks about an empty display list gets its own accumulator back.

**311 of 311 calls on level 1, 0 diverged, first run, no second attempt** — the
`PLD` lesson from two rounds ago paying for itself. `run` substituting only this
routine is identical at all 2,389 compared scheduler passes.

**Fifty routines. 4,317,463 calls checked across 42 movies, 0 diverged; branch
coverage 208 of 283, 75 untaken by every input, and the census is still empty.**
Twelve of the fourteen sites it added are taken, the best ratio any round here
has managed; the two that are not want a board state no movie has produced.

**Then the ranking that picked it was asked what to do next, and was wrong.**

`$80:9F9D` sat at the top of the list at 6.0% of everything the game executes,
over 49 calls — 394,000 instructions a call, for a routine that is nine
instructions long. It queues a VBL callback and then spins on `BIT $00C8 : BPL`
waiting for it. Summing the profile over those five bytes: **19,289,582 of the
19,289,827 instructions credited to it are the spin**, and `apu_ipl_upload`'s
`CMP $2140 : BNE` handshakes with the SPC700 are another 1.85%.

So **7.9% of the game's executed instructions are a CPU waiting**, it was in the
denominator of the completion estimate, and it was the number one thing to port
next — where porting it would have moved the number six points and achieved
nothing at all, because the C would spin on the same flag. `native_share.py` now
carries a measured table of wait sites and reports a **ranking by work**;
`$80:9F9D` leaves that ranking entirely and `apu_ipl_upload` drops from 3.1% to
1.3% with its wait annotated beside it.

This is the second time in three rounds that following the profile has meant
repairing the profile first, and both were the same shape: the tool measured
exactly what it claimed to and the claim was the wrong one.

**The native share is 50.6% of executed instructions and 64.1% call-weighted**
— or **54.9% and 69.6%** with the waiting taken out of the denominator, which
is the honest pair. From 48.4% and 61.8%.

Two side repairs went in with it. `src/port/oam.h` had its include guard closing
two thirds of the way down the file, leaving three routine blocks and two
`typedef struct`s outside it; and `src/port/lzss.h` was redefining
`LZSS_RING_START` at every build, which is now `W_LZSS_RING_*` because that
header describes the ring as a WRAM address while `src/assets/lzss.h` describes
it as a struct field.

**Next is `$80:E4C1`'s remaining leaves**, which is a coherent unit rather than
a list: `$80:AE14` (1.1%, 47,514 calls) and `$80:AE97` (1.4%, 58,331) are
near-identical twins that turn a point into tile coordinates and look up the
level's collision map, and `$80:B422` (1.3%, 79,021) tests the level extents.
Together they are 3.8% of the game's work and they finish the validator this
round started on. `$80:A93F` is tempting at 1.7% over 73,000 calls — it is the
camera centring on the two players, and it uses the `$D2`/`$D4` pointers this
round named — but it ends by dispatching into the four scroll routines at
`$80:A68B`, `$80:A70A`, `$80:A789` and `$80:A816`, which stream tilemap columns
into the VRAM queue and are a subsystem rather than a leaf.

### The second one the profiler picked, and it took nine minutes (2026-07-31)

**`$80:BF67` is the routine next to the one before it, in every sense.** It sits
eighty bytes further down bank `$80`, it is called by the same four enemy bodies
(`$81:85E3`, `$81:8627`, `$81:89E6`, `$81:8A15`), and the ranking had it second
at **1.8% of every instruction the game executes over 43,605 calls**. Where
`actor_nearest` asks *who is closest*, this asks **is anything standing within
six pixels of this point** — and answers in the carry.

It walks `visible_actors` backwards rather than the record table, so it sees
only what this frame's cull kept and costs whatever the board is wide instead of
a fixed 32. The window is the `CLC : ADC #$0006 : CMP #$000C : BCS` trick the
collision box uses, which tests **-6 <= d <= +5** in two instructions — a pixel
wider to the left than to the right, and `actor_overlap_pass` does the same
thing at 8 and 16.

The id filter is the fiddly part and the disassembly is the only place it is
written down: `$00` never, the whole band `$0C`-`$33` never, `$07` and `$08`
never, and everything else — `$01`-`$06`, `$09`-`$0B`, and everything above
`$33` — yes. Three comparisons, four ways out.

**The previous round's lesson paid for itself immediately.** Both exits are
`PLD` and *then* an explicit `SEC` or `CLC`, so carry is genuinely the routine's
and N and Z are the `PLD`'s — the same split as `actor_nearest` arrived at from
the opposite direction. Written that way from the start, it passed on the first
run, on every movie, including the four that caught the last one out. **26,796
calls across seven movies, 0 diverged**, and no second attempt.

A, X and Y are all claimed and none of them is tidy, because the ROM never
tidies them: it falls out of the loop with the last comparison's arithmetic in
A, the loop index in X — `$FFFE` when the walk ran out, the count being a byte
count and so always even — and the last entry examined in Y, which on the found
path is the record that matched and is presumably what the caller wanted.

**4182751 calls checked across 42 movies, 0 diverged; branch coverage 196 of 269, 73
untaken by every input, and the census is still empty.** Of the ten sites the routine added, 2 are untaken: `at_point_empty`, `at_point_id_named`.

`run` substituting only `actor_at_point` differs on 1,963 of 2,389 passes and
that is the declared model working rather than failing: `$7E:0F00` and
`$7E:0F02` hold the `PHD` and the `PEA` the port never pushed, which is what
`stack_bytes = 4` is for. **No byte of live game state ever differed.** The
cycle figure is 2,645, weighted across the 26,796 calls rather than taken from
one movie — the real spread is 698 to 6,958, because unlike `actor_nearest`'s
fixed 32 slots this one stops as soon as it finds something.

**Forty-nine routines, and the native share is 48.4% of executed instructions,
61.8% call-weighted** — from 46.6% and 60.0%. Two rounds of following the
profile have moved it seven points, against the 0.15% that porting the game's
entire placed cast would buy.

**And the next one is next door.** `$80:BFC8` is the routine immediately after
this one, opens with the identical `PHD : PEA $0000 : PLD`, takes the point in X
and Y without the self argument, and is 2.2% over 41,232 calls. After that the
list stops being easy: `$80:9F9D` is 6.0% over 49 calls and is jump-entered, so
its attribution is suspect; `thread_yield` is 3.9% and is the thing the harness
measures against; and `apu_ipl_upload` is a bus handshake.

### The first routine the profiler picked (2026-07-31)

**`$80:B123` is the first routine in this project chosen by measurement rather
than by the decline census**, and the census could never have named it: it is
not a collision handler, so the dispatcher never meets it. The
execution-weighted ranking built last round put it top of everything portable —
**5.2% of every instruction the game executes, over 44,248 calls** — and it is
375 instructions a call, which is what makes it checkable at all after
`$80:CD20` proved that a routine has to fit inside a frame.

It is a nearest-thing search by **Manhattan distance**, walking the raw 32-slot
record table downwards rather than following `ACTOR_NEXT`, and costing the same
32 iterations whatever the board holds. **The ids say what it is for**: `$05`
and `$06` are the two players, the same numbers `victim_a264_collide` claims a
victim by, and every caller is inside an enemy body (`$81:86B7`, `$81:870A`,
`$81:8ADC`, `$81:8B3A`). It is an enemy choosing who to go after.

Two things the listing gives away that a summary would not. **Ties go to the
higher slot**, because the comparison is strict and the walk runs downwards. And
**`$44` is never seeded**, so a search that matches nothing returns whatever
record the last successful search left there, alongside `$FFFF`. The port
reproduces both.

**The first version passed 1,006 of 1,006 calls on `movies/level1.zmv` and then
failed on four movies at once**, with one line of diff — `flag N: ROM 0, port
1`. Memory matched everywhere; so did A, X and Y. The shim had taken N and Z
from `$80:B18B  LDA $38`, the last instruction that looks like it computes
anything, and the routine ends `LDA $38 : PLD : RTL`. **`PLD` sets N and Z from
the value it pulls**, so what a caller sees is the sign and zeroness of its own
direct page. Level 1 hid it perfectly: every search there found something, and a
distance below `$8000` has its sign bit clear exactly like a thread page does.
Seeing the disagreement needs a search that comes up empty — 2,463 of level 21's
3,503 calls, and none of level 1's.

**The project had already learned this twice and I did not look.**
`shim_oam_buffer_clear` carries the note that "`PLD` is the last flag-setting
instruction, so N and Z describe the direct page it restores rather than
anything the routine computed", and `src/port/apu.c` says of the same thing
"that is easy to get wrong from the listing". So it is worth writing down as a
rule rather than as a third discovery: **a shim's flags come from the routine's
last flag-setting instruction, which is usually not the one that computes its
result**, and on anything that opens `PHD` that instruction is the `PLD`. An
audit of the whole registry for the same shape found no other case that has it
wrong.

`ACTOR_FLAGS` bit 0 got a name out of it — `ACTOR_ACTIVE`, set on every live
record in every display list sampled, with `$80:B123` the only reader found and
a comment saying that what clears it has not been established, because it has
not.

**4066151 calls checked across 42 movies, 0 diverged; branch coverage 188 of 259, 71
untaken by every input, and the census is still empty.** `run` substituting only
`actor_nearest` is **identical at all 2,389 compared scheduler passes** of
`movies/level1.zmv`, so the measured 7,195-cycle budget holds under real
substitution.

**And the ranking moved by what it predicted, which is the point.** One routine
took the native share of executed instructions from 41.4% to **46.6%**, and the
call-weighted estimate from 55.1% to **60.0%** — against the 0.15% that porting
the entire placed cast of the game would have bought. Forty-eight routines now,
and the next three targets are the same shape: `$80:BF67` (1.8%), `$80:AE97`
(1.3%) and `$80:B422` (1.2%), all short, all called tens of thousands of times
from the same enemy bodies.

### The routine that could not be checked, and the check that could (2026-07-31)

**The profile said LZSS was the best-value thing left — 7.1% of every
instruction the game executes, and a decoder already written and verified in
Phase 2 — and acting on it found the reason the harness had never been asked
for it.**

Writing the routine was the easy half. `src/assets/lzss.c` is the asset
pipeline's shape: bytes in, bytes out. `$80:CD20` is a 65816 routine whose
result is a *memory footprint* — a long source pointer it advances in place at
`$28`, a destination pointer at `$2C`, the byte count at `$38`, the window
position at `$3A`, and a 4 KB sliding window at `$7E:6F00` — so standing in for
it means reproducing all of that and not just the output. `src/port/lzss.c` is
that transcription, and it is deliberately not a wrapper around the other one.

It is also **the first routine whose argument arrives on the stack**: `PEA <src
address>` and then `JSL`, read back by `$80:CD27  LDA $06,S` once the routine's
own `PHD` is down, which is `s + 4` at the entry point. `CosimRegs` grew an `s`
for it, on the same footing as `d` and `db` — a stacked argument is part of a
calling convention too.

**And then the harness refused it, for a reason that is about the routine rather
than the port.** Registered, it reported five calls, five *interruptions* and
nothing checked. `verify` snapshots WRAM at entry and diffs it at exit, so an
interrupt landing in between makes the comparison meaningless and the call is
abandoned rather than reported as a divergence that is really the NMI handler's.
The profile says why it always lands:

| | instructions per call |
| --- | --- |
| `$80:CD20` body | 310,829 |
| `$80:CDDA lzss_read_byte` | 59,706 |
| `$80:CDEB lzss_write_byte` | 69,792 |
| **total** | **440,327 — about seven frames** |

`run` is no help either: substitution burns one mean cycle count in place of the
ROM's instructions, and a seven-frame mean cannot hold NMI alignment. **So the
registry entry was removed**, along with its three coverage sites — a site for a
routine nothing calls is untaken forever, and three of those would quietly
inflate the one number `coverage.h` exists to keep honest.

**Every one of the 47 routines ported so far fits inside a frame, and nobody had
noticed that was a requirement.** The census names what the ROM ran and the port
lacked; it has never named `$80:CD20`, because the dispatcher only meets what
the game calls during a scheduler pass. The harness's design has been quietly
selecting for short routines for twenty rounds.

**What could check it is a comparison scoped to a declared footprint**, and that
turned out to belong in `verify-lzss` rather than in the co-simulation harness.
That verifier already intercepts `$80:CD20` at entry, snapshots the window, and
diffs the ROM's output against the C decoder's — a controlled call, outside the
scheduler. Giving it a whole copy of WRAM at entry lets `lzss_decompress_wram()`
run from the same starting state, and the comparison is then the window, the
direct-page scratch at `$28`-`$41`, and the output range: exactly what the
routine's own instructions can write, read off the disassembly. What the NMI did
meanwhile is outside the comparison rather than inside it.

**35 of 35 calls across seven levels, byte-identical on all three.** The port is
verified against the ROM; it is simply verified by a different instrument, and
the co-simulation guarantee — all 128 KB, every call — is untouched, because
this check lives beside the asset verifiers and not inside it.

The routine count stays at 47 and the corpus is unchanged. What the round
actually produced is a piece of Phase 4 landing early — the finished game needs
a decompressor that works on the SNES's own memory whatever the harness can
do — and a much sharper idea of what the next harness capability has to be.

### The routine count finally got a denominator (2026-07-31)

**"Forty-seven routines ported" has been the headline number since Phase 3
began, and it has never had anything under it.** It cannot tell a 17-byte leaf
from a 2 KB state machine -- `shot_f6a3_collide` and `sprite_build_oam` both
count as one -- so after twenty rounds nobody could say whether the port was a
tenth of the way through the game's logic or half of it. `tools/native_share.py`
answers that, and the answer is **not the one the routine count implied**.

It took one addition to `zamn_trace`. The CDL records whether each ROM byte
executed; **`profile.bin` now records how often**, one word per byte counted at
the opcode, so summing it over a range gives instructions executed rather than
bytes touched. `subroutines.csv` already had a call count per entry, but that is
how often a routine *started* rather than how much it then did, and the two are
not close: `lzss_write_byte` is called 697,920 times and `$80:9F9D` 49 times,
and the second does four times the work.

Ten movies, one per level from 1 to 53, **319,152,834 instructions**:

| measure | share |
| --- | --- |
| static — distinct executed code bytes inside native code | **40.4%** |
| dynamic — instructions in wholly-native routines | **41.8%** |
| dynamic — weighted by native call fraction | **58.5%** |

**The gap between 28.9% and 41.8% is the finding that matters most**, because it
is a mistake this tracker had been making in prose. The 47 registry entries are
not the whole of what runs natively: the ROM splits work into subroutines the
port inlines, and `$80:BA51 sprite_emit` -- 16,221 calls from inside
`sprite_build_oam`'s body, four coverage sites of its own in `coverage.h` --
never executes at all under `zamn_cosim run`, because the port served its
caller. Counting entry points credits that to the ROM. Closing over the call
graph adds 171 such routines and 13.0 points; crediting the 75 routines reached
from both sides by their call share adds the rest.

So the honest summary is that **the port is between two-fifths and three-fifths
of the way through the work the game actually does**, not the under-a-tenth that
a routine count and a glance at code volume suggest. The reason is that every
routine chosen so far was chosen because the decline census asked for it, and
the census asks for what runs -- twenty rounds of following it has been an
accidental profile-guided ordering.

**And the ranking named low-hanging fruit that is not a porting job at all.**
`src/assets/` already holds `lzss.c`, `level.c`, `gfx.c`, `sprite.c` and
`music.c`, reimplemented in Phase 2 and **verified byte-exact against the ROM's
own routines** by the five `verify-*` commands. None of them is in the
co-simulation registry, so under substitution the ROM still runs all of it:

| routine | share of all instructions | calls |
| --- | --- | --- |
| `$80:CD20 lzss_decompress` | 4.9% | 50 |
| `$80:CDEB lzss_write_byte` | 1.3% | 697,920 |
| `$80:CDDA lzss_read_byte` | 0.9% | 373,188 |

**Seven percent of every instruction the game executes is LZSS, and the port has
had a verified LZSS decoder since Phase 2.** What is missing is a registry entry
and a shim, not a decoder. `apu_ipl_upload` is another 3.1% but is not the same
opportunity -- it is a handshake with the APU's own boot ROM, and its
instruction count is a spin loop waiting on hardware rather than work a C
function can do instead.

**Two caveats, both readable off the table.** `$80:9F9D` is 6.0% of the corpus
over 49 calls and no `callgraph.csv` edge names it as a callee -- it is reached
by a jump, so the span credited to it runs to the next `JSR` target and probably
swallows several pieces of level-load code. And load time is not gameplay:
`lzss_decompress`'s 50 calls and `apu_ipl_upload`'s 10 are per-level and
per-boot, so ranking by share alone points at the loader rather than at the
game. Which of those matters depends on whether the goal is Phase 4 or a faster
level load.


**The first thing the ranking did was refute the advice that prompted it.**
Counting the placed cast of the fourteen password levels gives 21 actor
behaviours and 10 victim behaviours -- 31 addresses for 291 actors and 140
victims, nine of them covering 85% of every enemy in the game -- and that looked
like the obvious next target. Weighted by execution it is not:

| | instructions | share |
| --- | --- | --- |
| all 15 actor behaviours reached | 447,825 | **0.14%** |
| all 10 victim behaviours reached | 23,472 | **0.01%** |

**Porting the entire cast of the game would move the native share by about a
seventh of a percent.** The reason is the thing that made the coroutine problem
worth solving in the first place: a behaviour is not a loop, it is a coroutine
that runs a few instructions, sets a wait and yields. The repetition lives in the
scheduler -- `thread_yield` alone is 3.9% across 236,192 calls -- and the bodies
barely execute. Placement count and code volume both pointed the other way, and
both were wrong, which is the whole argument for having the measurement.

It also took a second pass to see this at all. **Thread bodies are invisible to
a call-graph profiler**: the scheduler enters a behaviour through an indirect
jump, so no `CDL_SUB` mark is set, no `callgraph.csv` edge names it, and every
instruction it runs is credited to whatever subroutine sits above it in the
bank. All 31 scored exactly zero on the first run, which is the signature of the
bug and not of idle code; `native_share.py --entries` seeds them from
`zamn_assets actors` and the totals barely move (41.8% to 41.4%), which is how
you know the mis-attribution was small.

**What the behaviours do instead is call a library, and that is where the work
is.** `$80:B123` is 5.2% over 44,248 calls, and its callers are `$81:86B7`,
`$81:870A`, `$81:8ADC`, `$81:8B3A` -- addresses inside enemy bodies. So are
`$80:BF67`'s (1.8%), `$80:AE97`'s (1.3%) and `$80:B422`'s (1.2%). These are
shared movement and physics helpers in bank `$80` that every behaviour leans on,
they are the same shape as the 47 routines already ported, and together with
`$80:AE14` and `$80:A93F` they are **12.2% of every instruction the game
executes**.

So the ordering that comes out is: the LZSS decoder that is already written
(7.1%), the scheduler's own core (`thread_yield` plus the two vblank queue
runners, 7.7%), and the bank-`$80` helpers the actor bodies call (12.2%). The
bodies themselves are last, not first.

### The stream of fire that was a wall (2026-07-31)

**The previous round fired 596 shots at level 25's boss, did one point of damage,
and concluded that the boss does not walk into a stream of fire. There was no
stream of fire** — every one of those shots died six pixels from the barrel, and
the round that drew the conclusion never had a line of fire to measure.

`--records` sampled **per frame** rather than every fourth is what shows it. The
fire rate is 12 frames, so the old every-fourth sampling was aliased against it
and caught every shot at the same age; at step 1 a shot spawns at x=665, reaches
x=671 and is gone two frames later. That is the identical two-frame, six-pixel
death that proved level 17's creature was in a sealed pen rather than behind an
uncut route — the player was pressed against a wall, shooting into it.

`zamn_assets route --reach <rom> 26` says where the wall is not: a clean
**408-pixel corridor at y=588**, running x=748 to x=1156, straight through the
band the boss spends about 40% of its time in. `movies/level25-heavy.zmv` and all
four parked experiments fought at y≈561. **Twenty-seven pixels, and the same gun
goes from nothing to eight hits in four hundred frames:**

| row fought on | walk ends | boss health 54 → |
| --- | --- | --- |
| y=577 | stuck at (759,577) | 54 |
| **y=585** | (1207,585) | **46** |
| y=593 | (1271,593) | 48 |
| **y=601** | (1203,601) | **46** |
| y=609 | (1271,609) | 51 |
| y=617 | (1271,617) | 54 |

**`movies/level25-lane.zmv` is that, plus one other change and no new route.**
`level25-heavy` collects forty shots of `$67` — four damage each, and not one of
the four ids this boss rewrites — by frame 3387, then fires every one of them
walking north with Y held, at about ten percent. Holding fire from 3387 lets them
survive the trip, so the fight starts with the boss on its **full 70** rather
than on 54. The tail sweeps the lane in 300-frame legs instead of spinning on the
spot. **70 → 44:** twenty-six damage against the recorded movie's sixteen, and
the first number in this fight that came from aiming the experiment rather than
from lengthening it.

**Four of the boss's five untaken branches are now accounted for rather than
outstanding.** Running `$80:CA30` backwards — it is a **word** table, and type/2
indexes it rather than being the id — names every weapon each level places, and
`--reach` says which are on the boss's side of the map:

| weapon | object | reachable from the lane? |
| --- | --- | --- |
| `$5C` | starting inventory | yes, 150 shots |
| `$67` | `$22` ×2 | yes, 40 shots at 4 |
| `$61` | `$26` ×4 | yes, 20 shots — rewritten to `$60` |
| `$5F` | `$06` (989,122) | **no** — solid, nearest reached ground 48 px off |
| `$64` | `$1A` (369,1083) | **no** — solid, nothing within 40 px |
| `$62` | `$16` (1243,505) | **no** — a sealed pocket |

`boss_alt_cheap`, `boss_alt_dear` and `boss_no_damage` are one weapon between
them: all three want `$62` or `$70`, `$62` is rewritten on a coin toss off the
scheduler clock to `$5C` (1 damage) or `$5D` (**0**), and a dozen hits would take
all three without a kill. `--reach` draws level 25's one `$62` in a five-by-three
block of `o` walled on all four sides, with a one-cell neck two rows above it —
exactly the shape the 2×2-clear box refuses on ground the game allows, so it was
walked rather than assumed. `tools/fit_route.py` put the player on the lip in 15
legs; **he goes in and stops at y=470**, from three different columns, which is
the row the overlay draws `#`. Sixteen pixels short, and `$7E:1CD8` never leaves
zero.

`boss_remap_6f` is unreachable for a simpler reason: `$80:CA30`'s first fourteen
entries are the weapon slots, `slot = id - $0C` and `weapon = $5C + slot`, so the
pickup weapons run `$5C` to `$69` and stop. **No object in the game can hand a
player `$6F`.**

So `boss_died` is the one branch of the five an input could still close, and it
is arithmetic rather than mystery: 390 damage of reachable ammunition against 70
of health, at an accuracy the lane has not yet held above about a fifth across a
whole load. What it wants is aim — a fitter that turns the player toward the boss
between legs, the way `fit_route.py` re-plans between them — not a longer movie.

**One tooling trap worth recording, because `docs/analysis-tools.md` warns about
it and it caught me anyway:** `route`, `level`, `actors` and `spc` take the
*record index*, so level 25 is `26`. Asked for `25` it answers about a 960×1408
map, and the boss's own x reaches 1259 — a level whose actors are off its own
edge is the shape that error makes.

**3,931,989 calls checked across 42 movies, 0 diverged; branch coverage 184 of
254, 70 untaken by every input, and the census is still empty** — from 3,674,068
across 41. `movies/level25-lane.zmv` contributes 257,921 checked calls and 0
declines, and it takes **no site the corpus did not already have**: all 70
untaken names appear in its own never-reached list, which is what says the union
did not move. A movie that adds a quarter of a million checked calls and no
coverage is worth having anyway — it is a quarter of a million more chances for
the port to disagree with the ROM — but it is not progress against the backlog,
and the backlog is what the boss round was aimed at.

### The census named an address, and the input came before the port (2026-07-31)

**`$81:E6E4` went onto the census with one decline and came off it ported, and
the order of those two halves is the finding.** The previous round put it there
and deliberately did not write it: one call cannot reach more than one of eight
branches, and a routine written against one call is transcription with a diff
attached to a corner of it. So the input came first. `movies/level37-e6e4.zmv`
reaches the same routine **367 times**, and the port is written against that.

**Finding the level was three ROM reads and no play at all.** `A9 E4 E6` — `LDA
#$E6E4` — occurs **once in the whole ROM**, at `$81:E476`, inside an actor init
at `$81:E413` that also seeds this creature's health to `$0003`. Two behaviours
reach that init by a plain `JSR $E413`, `$81:E481` and `$81:E51A`, and that is
the first thing that makes this copy unlike the other nine: **every other member
of the `$81:8888` family belongs to one behaviour and this one is shared**, which
is why it turns up on nine levels with nine different actor types in front of it.
Three of those a password reaches, and `--reach` sorted them in seconds: level
29's is **inside solid scenery**, all seven of level 33's are **cut off** from its
spawn, and level 37's at (1057,204) is **61 cells away** with standable ground on
the same row. `tools/fit_route.py` walked it in **two legs**, first run.

**The sweep found a bug in the search it was using.** Level 33's actor list
places one at **(1260,1737) on a level 1,280 pixels tall**, and `route` answered
"2 cells" for a goal 814 pixels from the spawn — it computed the goal's row as
216 against 160 and subscripted `prev` with it, reading past the end of its own
array. Both ends are range-checked now. An off-map actor is a real thing to find
in this ROM, and the whole reachability method rests on that predicate having no
way to answer "yes" for a reason unrelated to the level.

**The routine is the tenth copy of `$81:8888` and the only one that can be
switched off.** `$81:E6E4  LDY $08 : LDX $0004,Y : BNE $E72A` is the same three
instructions `enemy_b41c_collide` opens with — record `+$04` is `ACTOR_Z`, the
height off the ground — but where `$81:B41C` lets height decide the answer to
**one** id and takes the damage either way, this one sends *every* collision to a
bare `CLC : RTL` while the creature is airborne. **That matters beyond the
routine**: `port/oam.h` has `ACTOR_Z` down as a drawing concern, with an actor
that jumps keeping its Y "and so its depth sort order and its collision box", and
the overlap pass really does ignore height. So height immunity is not a property
of the collision system at all — **it is something individual handlers opt into**,
two of ten do, with the same three instructions and to different extents. And
`$5E` and `$5D` leave to *different* routines here, which is
`enemy_ac92_collide`'s pair rather than `enemy_d7f6_collide`'s, where the same
`$5E` falls into the death tail with no subtraction. Ten copies and no two of
them answer `$5E` alike.

**No survivors, and `--watch` says why in two words.** `e6e4_ignore` 1720,
`e6e4_hit` 46, `e6e4_died` 46 — on a creature seeded to three health. `$7E:050C`
goes `$0003` → `$FFFF` in a single hit at frame 4200, and `$7E:0522`, the id this
routine parks, reads **`$0060`** on that same frame: damage 4 against health 3 is
one hit, every time. **It is not the player's weapon** — `$7E:1CBC` is parked at
`$0012` by a pickup from 2980 to 4158 and reads 0 after, so the player's own
shots are `$5C` and do 1. Something else on level 37 lands the `$60`s and this
movie does not say what.

**Five perturbations, four caught, and the pair that disagree are two
instructions apart.** Parking the id over the health word, not stepping the death
counter, and clearing carry instead of setting it all fail — the last at
`$7E:11A0`, *one level up*, because carry is what parks a thread. The interesting
pair is the guard: **reading `ACTOR_Y` instead of `ACTOR_Z` is caught
immediately, and deleting the guard's decision entirely is missed.**
`e6e4_airborne` reads 0 on this movie, so nothing it collides with is ever off
the ground — the guard demonstrably *executes* on all 367 calls and its branch is
taken on none of them, and only running both perturbations says so. The fourth
also cost a lesson the script already knew: its first anchor occurred twice,
because `enemy_b41c_collide` reads the same field with the same line four hundred
lines above, and the anchor check refused it rather than breaking the wrong
routine.

**Forty-seven routines, and the census is empty again: 3,674,068 calls checked
across 41 movies, 0 diverged; branch coverage 184 of 254, 70 untaken by every
input** — from 3,588,215 across 40 and 183 of 246. The denominator moved because
the round added eight sites; three of them are taken and five are not, and
`collide_unported` and `handler_unported` went **back** to untaken, which is the
empty census stated as coverage. `movies/level29-ice.zmv` gained five checked
calls without a frame changing — 105,475 to 105,480 — because the one call it
used to decline is now one the harness checks. `run` substituting **only**
`enemy_e6e4` is **identical at all 5,989 compared scheduler passes** of the movie
that reaches it, so its 192-cycle budget costs nothing; all forty-seven together
find **no byte of live game state differing** on the same 5,989, and `-r none` is
identical at all of them.

---

*The rest of this section is the previous round, kept as written.*

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
ago with its six siblings. That leaves `d7f6_special`, id `$5D` — and
**`movies/level29-ice.zmv` takes it**, which finishes the routine: five branches
diffed and the other two argued to have no input at all.

**The second movie is the first one's argument stated as a route.** `d7f6_special`
wants the ice weapon and a `$81:D704` actor on the same level; six levels place
the behaviour and four place a type `$02` object, and the intersection that
survives *reachability* is exactly one. Level 5's ice weapon is sealed in a
basement pocket, level 17's creature is sealed in an alcove, level 31 has no
password — **level 29 has both**, and it costs 2,800 frames of walking to put
them together, fitted in four goes. Every shot on screen reads `$5D`: 69 of them
across 200 consecutive sampled frames, against no other weapon-range id at all.

**One of the four legs was walked by hand, and it is the one that changed a
tool.** At (773,461) the way west is a 56-pixel detour south, and
`tools/fit_route.py` spent **41 legs** stepping 28 pixels down, 28 back up and
asking the same blocked question again — its unstick nudge was one fixed length,
and one length cannot clear a chicane. Walking on to the wall at (721,591), 130
pixels away, cut the remaining route from 277 cells to 147 and it fitted first
time. `STUCK_NUDGE` is `(14, 28, 56)` now, shortest first, and it reproduces the
level-5 route byte for byte — nine legs, same frames — because a route that never
gets stuck never reaches it.

**And the movie put an address on a census that had been empty for rounds.**
`$81:E6E4`, one decline, which is also why `collide_unported` and
`handler_unported` are taken for the first time by anything. It is a **tenth copy
of `$81:8888`** and it differs from `enemy_d7f6_collide` at both ends: an opening
`LDX $0004,Y : BNE` guard — a word on the *display record* rather than the thread
page, which makes the creature ignore every collision it has — and a `$5E` that
`JML`s to `$81:83C6`, the bubble tail, where `$81:D7F6` sends the same id into
its death tail. Nine copies of this subsystem and no two answer `$5E` alike.
**It is not ported this round, on purpose**: one call cannot reach more than one
of eleven branches, and a routine written against one call is transcription with
a diff attached to a corner of it. The address and the shape are written down so
the input that would fix that is cheap to aim.

**One thing on the level-29 movie is measured and unexplained.** `d7f6_hit` 15
and `d7f6_died` 10 count beside `d7f6_special` 5, and `$5D` is diverted two
comparisons before the damage table — so a lethal hit needs some other id at or
above `$5C`, and the display list over the frames those hits land in holds only
`$03`, `$36`, `$38`, `$00`, `$01` and `$5D`. Whatever carried them has no display
record, which this project has seen before. Both sites were already taken by the
level-5 movie, so nothing rests on it; it is written down because it is
unaccounted for.

**Forty-six routines, no port code changed, four sites moved and a census that is
no longer empty: 3,588,215 calls checked across 40 movies, 0 diverged; branch
coverage 183 of 246, 63 untaken by every input** — from 3,429,877 across 38 and
177 of 246. The two movies add 52,863 and 105,475, which is the whole difference
to the digit. Between them they take `d7f6_hit`, `d7f6_died`, `d7f6_survived` and
`d7f6_special`; `collide_unported` and `handler_unported` come with the census
entry, and the arithmetic that says so is the intersection of each movie's untaken
list with the corpus's rather than a count. `run` substitutes all forty-six on
both and finds **no byte of live game state differing on any of 3,489 and 5,989
compared passes**, and `-r none` is **identical at all of them** on both.

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
