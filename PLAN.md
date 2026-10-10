# Native PC Port of *Zombies Ate My Neighbors* (SNES) — Plan

Rewritten 2026-10-09. The first plan kept the console's video chip and sound
chip emulated for good, as `snesrev/zelda3` does. That changed on 2026-10-08.
The plan before this one is in git.

## The goal

**`zamn.exe` links nothing of LakeSnes.** The game's code, the picture, the
sound, the clock and the saved states are the port's own. LakeSnes stays in
the tree for the test programs, which run it beside the port and compare.

The game still loads everything from the user's ROM when it starts. Nothing
of the ROM is in the repository.

## Where it stands

| Phase of the first plan | State |
|---|---|
| 0. A reference emulator in a window | Done |
| 1. Analysis and the memory map | Done |
| 2. Assets read from the ROM | Done |
| 3. The game's code in C, under the harness | Underway. 566 routines |
| 4. The emulator cut loose | Begun, with the picture. This plan |
| 5. Widescreen, quick save, pads, cheats, a launcher | Done, ahead of 4 |

The picture is drawn by `src/video`, and `zamn.exe` has no emulated PPU in
it. `build/zamn_with_ppu` is the same game with one, for the check.

What `zamn.exe` still links from LakeSnes:

| Piece | Lines | What the game uses it for |
|---|---|---|
| The 65816, `cpu.c` | 2,478 | Runs what is not ported. Carries control from one port to the next. Is the clock every cost is spent on |
| Sound, `apu.c` `spc.c` `dsp.c` | 2,462 | The SPC700 running the game's sound driver, and the DSP mixing it. The game's machine, and more of them for the frontend |
| The console, `snes.c` `snes_other.c` | 863 | The beam, the bus, WRAM, when the NMI falls, the multiplier, the pads' latch |
| DMA, `dma.c` | 388 | Copies to VRAM, the palette and the sprites. HDMA, a line at a time |
| States, the cartridge, the pads | 468 | The layout of a saved state, where the ROM is mapped, the buttons |

The 65816 is the hub. The console, DMA and the states cannot leave before
it. Sound is separate, and can be done at any time.

## The survey of 2026-10-09

110 sessions: the 54 movies of the corpus at their usual lengths, and each
of the 56 level records started with `--level` under `level1.zmv` for 4,700
frames. 609,850 frames. Each was run twice: in the game with `--profile`,
which counts what the 65816 still executes, and in `zamn_trace`, which counts
what the stock ROM executes. Nothing was built for it.

### The game's code

**By work the 65816 is nearly gone. By code it is not.**

| Measure | The 65816 still runs | Of the stock ROM's |
|---|---|---|
| Instructions executed, waits left out | 25,859,710 | 2,290,971,949: 1.13% |
| ...without one pause, see below | 7,963,192 | 0.35% |
| Cycles of work | 741,454,158 | 60,915,677,430: 1.22% |
| Calls not served by a port | 286,356 | 4,703,179: 6.1% |
| Distinct instructions | 11,297 | 27,494: 41.1% |
| Bytes of code | 26,697 | 62,439: 42.8% |

So 16,197 instructions of the 27,494 these sessions reach are never executed
by the 65816 in any of them. The other 11,297 are, at least once. The corpus
alone gives 10,185 of 26,581. The records reach 1,112 the corpus does not.

What the 11,297 are:

- **6,127 run as often as in the stock ROM.** Nothing has taken them. Most
  run seldom: a level's start, a screen's setup, a creature's first frame.
- **5,170 run less often than in the stock ROM.** A port takes them some of
  the time. The ROM runs them when a port turns a call down, or leaves by a
  path it does not have. 39 routines turned down 4,254 calls of 15,830,512.
  `player_frame` is 1,989 of those.
- **They are rare.** 3,484 ran fewer than ten times in 609,850 frames, and
  7,847 fewer than a hundred. 1,097 ran a thousand times or more.
- **They are in 496 runs of adjacent code**, 146 of them 64 bytes or longer.
  Two 4 KB blocks have none left: `$80:B000` and `$82:C000`. The most are
  left in `$82:9000` and `$82:D000`, three quarters of each.

Two things the ranking showed that are not code to port:

- **The pause is a wait nobody declared.** `$80:89C8` to `$80:89E7` spins
  on the pads' word until Start is let go, pressed and let go. It is
  17,896,518 instructions in one movie, `level21-exit`. It belongs in
  `src/cosim/waits.h` with the other thirteen. (It is there now, and
  ported: see step 2's notes.)
- **The waits are where the 65816 spends its time**: 374,719,039
  instructions in the thirteen declared ones. Each spins on a word only the
  NMI changes, or on the sound chip's answer.

**Code no session reaches.** Following every branch and call out of the code
that did run finds 8,476 more bytes of code: 1,681 in bank `$80`, 1,409 in
`$81`, 4,103 in `$82` and 1,283 in `$83`. That is a lower bound. Code
reached only through a table of addresses is not found this way. 48,386
bytes of the four banks were touched by nothing, as code or as data.

**How a port is driven.** This decides the size of step 2.

- A port is entered at an address of the ROM's and leaves by an instruction
  of the ROM's: a return, a call, a jump, or `JSL thread_yield`. The harness
  already makes eight kinds of that instruction itself, 15,177,851 times in
  these sessions. The core makes the rest.
- A thread's stack is WRAM, and the ports that need it write it as the ROM
  does. The scheduler, both dispatchers and the NMI handler are ported with
  no instruction of the core's between them.
- The registry and its 368 adapters in `src/cosim/routines.c` call nothing
  of the emulator. Neither does `src/port`. Only the engine in
  `src/cosim/cosim.c` does, through fifteen functions.
- The frontend calls sixteen functions of the emulator and reads six of
  its fields.
- An NMI fell due inside a ported call 55,182 times, one frame in eleven.
  Those are mostly the frames the console could not finish in time, and
  the long loads.

### The console

103 registers are touched. 52 are the video chip's, which `src/video` has.

| What | Used |
|---|---|
| Interrupts | 608,094 NMIs. No IRQ, ever. The timers are written once, at reset |
| Pads | The automatic read: `$4218` to `$421B`, and the wait on `$4212`. `$4016` and `$4017` are never touched |
| Multiplier | 3,795 products, from two places. The divider is cleared at reset and never used |
| DMA | Channel 0 only, to the chip only: sprites (`$2104`), VRAM (`$2118`) and colours (`$2122`). 1,423,455 transfers |
| HDMA | Channels 5, 6 and 7. `$420C` is written 725 times, from four places |
| WRAM's own port, `$2180` | Never |
| Speed, `$420D` | Written from three places. It is not left set |
| Sound's four ports | 185,039,098 reads of `$2143`, nearly all of them the wait for an answer |

### The sound

- The whole audio program runs on the SPC700. It is David Warhol's driver
  of 1992: 2,999 bytes at `$0600`, with 36,547 bytes of samples. The 65816
  sends it one-byte commands and uploads songs and sample sets through it.
- Part of it is already read. `src/sfx_overlay.h` knows where a command is
  taken, where an effect is started or dropped, and how a note takes a
  voice: four sequence slots, eight voices, a priority to each.
- The DSP as each of the 56 levels' songs starts: every voice on ADSR, echo
  on with a delay of 3 and feedback up to `$6E`, no noise and no pitch
  modulation. That is one moment of each song. Whether an effect turns
  noise on later was not looked at.
- The frontend runs more sound machines on LakeSnes, for two things. One
  brings back effects the driver drops for want of a slot or a voice. One
  plays monster sounds from sample sets the level did not load. Both exist
  only because the driver has four slots, eight voices and room for one
  set.

## The plan

### Step 1. The game's code, until no session meets the 65816

The work is the same as it has been: a routine at a time, verified per call
against the ROM, ranked by `tools/native_share.py --residue`. What changes
is the target and the ranking.

1. **Rank by code and not by work.** By work the list is finished. What is
   left is 11,297 instructions that run seldom, and a rarely run instruction
   stops a game with no CPU as surely as a common one.
2. **The paths the ports turn down.** 39 routines, 4,254 calls.
3. **Every wait becomes a wait.** Thirteen declared and the pause. In C each
   is "until the NMI has changed this word", and the driver of step 2 runs
   frames until it has.
4. **Code no session reaches.** At least 8,476 bytes. A movie comes before a
   port, as now. Where no movie can be made, a poke.
5. **A map of the ROM's code**, kept by a tool: each byte of the four banks
   as ported, not ported, or data. It is what says the work is done, since
   a movie can only say what it reached.

Done when all 110 sessions run with the 65816 executing nothing, and the map
has no code byte that is not ported.

### Step 2. A driver with no CPU

Run the registry without the core. The pieces exist: `PortCpu` in
`src/port/cpu.h` is the register set, the stack is WRAM, and `leave` in
`cosim.c` makes the instruction a port ends on.

- **A loop.** Look the address up, run the port, make the instruction it
  left by, look the next address up. An address nothing has is an error
  that names it.
- **The NMI as a call**: the push the 65816 makes, then the handler's
  ports.
- **`WAI` and the waits** run the frame to its NMI.
- **A clock**: 262 lines of 1,364 cycles, the refresh each line, a DMA's
  hold on the CPU, and where the NMI falls. Each port already says what it
  cost. See the first decision below.

It is built in the test programs first, as a second way to run the same
registry, and compared with the core's way a scheduler pass at a time.

**Begun, 2026-10-09.** The harness takes the interrupt and waits out the
`WAI` itself, so the core is called for one thing: an instruction no port
has. Each place it takes over is counted, with how the program counter got
there. That list is the work of step 1 by address:

| How the core got there | Places | Times |
|---|---|---|
| A port returned to code that is not ported | 888 | 637,120 |
| A port stopped on an instruction the harness does not make | 61 | 5,796 |
| A port turned the call down | 34 | 4,024 |
| Reset: the boot code | 1 | 110 |
| A port called code that is not ported | 5 | 84 |

Over the same 110 sessions. The 893 places returned to or called are in
276 routines. When the list is empty for a session, the core executed
nothing in it.

**Later that day**, after the first work down the list: 878 places
returned to, 45 left standing, 34 declined. The core executed 400.6
million instructions over the survey, and 392.8 million of them are seven
loops that wait. So item 3 of step 1, the waits, is nearly all of the
core's work, and almost none of its places.

**The waits, the same day.** Item 3 of step 1 is done for the twelve that
wait on a word of WRAM: `port/hold.h`, one turn of a loop a call. The core
executes 7.5 million instructions over the survey where it executed 400.6
million. The places are 973, ten more, because the instruction after a
wait is the ROM's still. What a driver with no CPU does with a turn that
says "not yet" is its own choice: the harness spends a turn's cycles, and
one that runs the frame to its NMI would be right too.

**Links, the same day.** A call, a jump or a return that stands between
two ports is a registry row of its own, a link, and the harness makes it.
The giant ant's thread is written that way: three of its four copies,
as stretches from one call to the next. The places are 883 and the core
executes 6,805,266 instructions over the survey. Most of what it runs
now on levels 25 and 45 is the ant's three state routines and the
boss's thread.

**The big figure's thread, the same day.** Level 25's boss was two of
every five instructions the core still ran. Its turn and its eight states
are ported, each state a C function that calls the ports it uses and
stops only on a call with no exact price. The places are 852 and
the core executes 4,091,951 instructions over the survey.

**The giant ant's states, 2026-10-10.** What it does when it is not
chasing, how one is set up and what it carries are ported, and the chase
no longer turns a leap down. So is the job that shakes the screen under
the boss. The places are 821 and the core executes 3,399,313
instructions over the survey.

What is left of this step: registers of the driver's own in place of the
core's, the clock, and a stop that names the address in place of the
core.

### Step 3. A machine with no console

Small, by the survey.

- WRAM as an array, which `Wram` is already.
- The frame: `src/video`'s chip called directly for each line.
- DMA on channel 0 to three registers, and HDMA on three channels.
- The pads' latch, the multiplier, and `$4210` and `$4212`.
- No IRQ, no divider, no manual pad read, no `$2180`.

Then `zamn.exe` is `main_sdl.c` on the driver and this, with the sound.

### Step 4. The sound

Independent of steps 1 to 3.

1. **The DSP**, written again: BRR decoding, ADSR, pitch, echo with its
   filter, the mix. It is a fixed specification. Checked sample for sample
   against LakeSnes's, on the register writes of every song and effect.
2. **The driver, in C**, as the game's code was done: the sequencer that
   reads a song and drives the DSP. Checked against the SPC700 running the
   original, by the DSP writes each makes and when.
3. **The uploads go.** A song and its samples are read from the ROM.
   `src/assets/music.c` already decodes every data set.
4. **The frontend's two extra machines go.** A driver of our own can have
   more slots and voices than four and eight, and all four sample sets
   loaded. The dropped effects and the missing monster sounds become
   settings of one engine.

The hard part is what the game sees of the sound chip: how long it takes to
answer a command. See the risks.

### Step 5. What is left

- **Saved states** in a layout of the port's own: WRAM, the registers, the
  clock, `src/video`'s share, the sound.
- **The launcher's icon and heading**, drawn when the game is built, by
  `src/video` and not the PPU.
- **The link.** As with the PPU: the console is split into libraries, and
  `zamn.exe` is linked against none. A call of LakeSnes from anywhere in
  the game is then a link error. `zamn_with_ppu` grows into the build that
  has all of it.

## How each step is checked

The way the picture was. The test build runs the port and LakeSnes side by
side and compares, and the corpus is the standing check.

| Step | Compared |
|---|---|
| 1 | Every call: 128 KB of WRAM and the registers, as now |
| 2 | Every scheduler pass: WRAM and the clock, driver against core |
| 3 | The same, and every register write by address, value and cycle |
| 4 | Every sample from the DSP. Every DSP write from the driver |
| 5 | A state saved by one build loads in the other. The link itself |

Each new check gets things broken on purpose, to show it catches them.

## Decisions that are the user's

1. **Timing.** With no CPU there are no cycles unless the port counts them.
   - *Keep the console's timing.* The ports already report their costs and
     the harness holds them exact. The game slows where the console did,
     and a movie stays in step with the emulator, which is what makes the
     corpus a check.
   - *Drop it.* The game never slows, and no movie can be compared frame
     for frame again.
   - Recommended: keep it. "Never slows" can be a setting later.
2. **The rare code.** Three ways to get the 65816 out.
   - *Port all of it by hand.* The most work. The only one that leaves
     readable C throughout.
   - *An interpreter of our own for what is left.* LakeSnes leaves sooner.
     It is still emulation of the game's code.
   - *Translate what is left by machine.* Native, and not readable.
   - Recommended: by hand.
3. **The sound driver.** Port it to C, or write an SPC700 of our own and
   run the original. Recommended: port it. The second removes LakeSnes and
   keeps an emulator.
4. **Old quick saves** stop loading when the layout changes. Recommended:
   let them, and have the test build convert one if it is wanted.

## Order

1. **The driver and its clock, in the test build.** It is the design risk,
   it is small, and it turns "what is left" into an address per movie.
2. **Step 1**, by the driver's list and the ranking by code. This is most
   of the work.
3. **The DSP**, then the sound driver, alongside. Neither waits on step 1.
4. **The machine**, when a movie first runs through on the driver.
5. **States, the art, the link.**

## Risks

- **Step 1 is larger than the share of work says.** 99% by work is 59% by
  code, and the code that is left is the code the movies reach least.
- **Code nothing reaches.** A game with no CPU has nothing to fall back
  on. An address nothing has must stop the game with its name, and the map
  of step 1 is what keeps that from happening in play.
- **The sound chip's answer.** The game waits on it, and how long it waits
  moves everything after it. A native driver answers at its own time. If
  timing is kept, that time has to be modelled to the frame, or the movies
  keyed to the game's own ticks and not to frames. Not looked at yet.
- **Level 25.** Three movies part from the stock console under lockstep
  today, on timing. A second clock will meet the same thing.

## Not established

- How much code is behind tables of addresses that no session used.
- Which of the 8,476 bytes are dead code.
- Whether any effect uses the DSP's noise or pitch modulation.
- How the sound chip's answering time is distributed.
- What the three HDMA channels each write, beyond the wave on channel 6.
- That the 56 records were played well. Each ran `level1.zmv`'s buttons on
  another level's map.

## The ROM

- **Mapping:** LoROM, 1,048,576 bytes, no header.
- **Chips:** none. No SRAM. The game saves by password.
- **Region:** NTSC.
- **Vectors:** RESET `$80AE`, NMI `$816C`. IRQ is never taken.
- **Code:** banks `$80` to `$83`. Level records in `$9F`. Sound from `$91`.

## Resources

- `Piranhaplant/Necrofy`: the level, tile, sprite and palette formats.
- `snesrev/zelda3`: the harness's pattern.
- LakeSnes, in `third_party/lakesnes`: the reference.
- `docs/cosim.md`, `docs/threads.md`, `docs/video.md`: how the harness, the
  threads and the picture were done.
- `PROGRESS.md`: what has been done, by date.
