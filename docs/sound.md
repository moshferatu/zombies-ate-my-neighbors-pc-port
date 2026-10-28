# The sound, ported

The game's sound no longer runs on an emulated sound chip. `src/sound` is
the console's boot loader and the game's own sound driver, written in C, on
a DSP of the port's own. In `zamn.exe` it takes the place of LakeSnes's APU,
and the APU is left alone. It is checked against the APU on every movie: the
same answers to the game on the same cycles, the same writes to the DSP on
the same cycles, the same RAM, and the same samples.

## What the game's sound is

The whole audio program runs on the sound chip's SPC700. It is David
Warhol's driver of 1992: 2,999 bytes at `$0600`, uploaded by the 65816
through the console's 64-byte boot loader along with 36,547 bytes of
samples. Then the 65816 talks to it a command at a time on four ports, and
uploads songs and sample sets through it, a byte and an answer at a time.

The driver plays songs that are MIDI files in all but name: four tracks at
once, each a stream of delta times and events on sixteen channels, and eight
voices shared between the tracks by priority. Its main loop polls the ports
for a command (`$0AFF`), and on each tick of a 500 Hz timer winds the tracks
on (`$06B5`) and moves each voice's vibrato (`$0D03`). The handlers are
reached through tables, by writing a handler's address into a `JMP` and
running it: 21 commands, seven events by their high nibble, and 22
controllers. Events `$A0` and `$D0` jump to `$0000`; no song uses them.

`tools/dis700.py` disassembles it out of the user's ROM, following the three
tables and, with `--trace`, the addresses `zamn_test_dsp --spc-trace` saw
run. The listing goes in `analysis/`, which is never committed.

## The pieces

| File | What |
|---|---|
| `src/sound/dsp.c` | The DSP: BRR, pitch, envelopes, noise, echo and its filter, the mix |
| `src/sound/sound.c` | The chip around the program: the clock, the four ports, timer 0, the DSP kept in step, and the boot loader |
| `src/sound/driver.c` | The game's driver, routine by routine |
| `src/sound/console.c` | The one place the sound meets LakeSnes: `snes_setSound` |

The driver keeps its variables where the original kept them, in the sound
RAM, under names of this project's. That is what lets the test compare the
RAM byte for byte, and what lets `src/sfx_overlay.h` read the driver as it
always did.

## Time

The game waits on the sound chip. It sends a command and spins until the
driver answers, and a level's songs and samples go over one byte and one
answer at a time. So when the driver answers moves everything the game does
after it, and a movie only stays in step with the console if every answer
comes on the same cycle. The C therefore counts the cycles of the
instructions it stands for, on every path, and the clock is the SPC700's.

The driver never waits for the game except at the ports, and all of its
reads of them are in the poll. So it runs ahead of the console and stops
only at a read of a port, where it needs to know what the game had written
by that cycle and the game may not have got there yet. What the game writes
is kept with its cycle, and what the driver writes is shown to the game once
the game's clock passes it. An instruction that starts before the game's
cycle has been done, and one that starts on it or after has not: the line
LakeSnes draws when it catches its SPC700 up an instruction at a time.

The DSP keeps step with the driver, which can be ahead of the game, so each
write to it and each byte of RAM it could read is made on its own cycle. A
store writes on its instruction's last cycle and a load reads on its last.

One thing is not the same. LakeSnes runs its SPC700 to the end of the
instruction that straddles the game's cycle, and the DSP with it. The port
stops at the cycle. So in the game, the 534 samples a frame's audio is taken
from can be one sample later or earlier than LakeSnes's on some frames.
Nothing the game sees depends on it. The test catches the sound up to the
end of that instruction, as LakeSnes does, and is exact.

## What is checked

**`zamn_test_dsp`** runs the port's DSP beside LakeSnes's on the register
writes of each movie: every sample, all 128 registers, and the RAM the echo
writes. Over the 56 movies, 285,915 frames and 152.8 million samples. No
movie turns on noise or pitch modulation.

**`zamn_test_sound`** runs the port's sound beside the console's SPC700
running the original, on each movie, and gives both every write the game
makes to the ports, at the console's cycle:

| Checked | Over the 56 movies |
|---|---|
| Every read the game makes of the ports, byte and cycle | 108 million |
| Each routine the C marks, entered on the same cycle | 107.7 million |
| Every DSP write: register, value and cycle | 2.7 million |
| Every port write the driver makes | 7.7 million |
| The driver's RAM at every tick, all of the sound RAM every 64 | |
| Every sample, and each frame's audio | 152.8 million |

`--break N` makes the driver one cycle late at its Nth poll, and it fails.
`--spc-cycles` prints LakeSnes's cycles for each opcode, which agree with the
costs the driver was written with.

**`verify_corpus.ps1 -Lockstep -PortSound`** runs the corpus's lockstep pass
with the port's sound on the port's core and the APU on the stock one.

## The frontend's other sound machines

`src/sfx_overlay.h` (the effects the driver drops) and `src/bank_sfx.h` (the
monster sounds of the other sample sets) ran extra copies of LakeSnes's APU.
They now run copies of the port's sound: a `Sound` is a plain struct, and
`sound_copy` makes another machine of one. The overlay watches the real
driver through `Sound.watch`, at seven places `driver.c` marks for it with a
register each, and the test checks those marks' cycles and registers too.

Two things changed in the overlay. It takes its copy only once the real
driver has taken every command on the ports; after the boot loader, port 3
holds a byte that is not a command, which the old check took for one, so
the overlay skipped the game's first real command and the copy never set
its upload address. And a voice it opens can be heard up to one pass of the driver's
loop late, because the copy's DSP keeps step with its own driver.

`zamn_test_sfx_overlay` gives the numbers it gave on the APU.
`zamn_test_bank_sfx` fails the three checks it already failed before the
sound was ported, and passes the rest; see below.

## What is left

- **LakeSnes's APU is still linked into `zamn.exe`**, unused. It leaves with
  the rest of the console, in step 5 of `docs/PLAN.md`.
- **The extra machines are still copies.** The plan's step 4 ends with one
  engine that has more slots and voices than the driver's four and eight,
  and all four sample sets loaded, with the overlay and the monster sounds
  as settings of it.
- **`zamn_test_bank_sfx` finds too few of the game's checks.** It hears the
  monsters' checks of the sample set through `cosim_watch` on the 65816's
  program counter, and since the reset was ported the 65816 runs none of the
  game's code, so a check that is not the first instruction of a ported
  routine is never seen. This is so before the sound
  was ported too, and in the game as well as the test.
- **Quick saves** made before this do not load: the sound's part of a state
  is laid out differently, and a different size, and the file is refused.
