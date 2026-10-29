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
| `src/sound/player.c` | The driver's sequencer again, with more tracks, voices and sample sets: what the frontend hears |
| `src/sound/console.c` | The one place the sound meets LakeSnes: `snes_setSound` |

The driver keeps its variables where the original kept them, in the sound
RAM, under names of this project's. That is what lets the test compare the
RAM byte for byte, and what lets the player (`src/sound/driver_ram.h`) take
the driver's place from wherever it is.

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

**`zamn_test_player`** runs the player beside the driver on each movie, with
the console's settings: every DSP write the driver makes must be the
player's, register, value and cycle, and every frame's audio the same. Over
the 56 movies, 2,717,807 writes and 281,379 frames, all the same, and the
two lose the same: 218 effects with no track, 70 notes with no voice, 1,045
voices taken. `--break` gives the player a ninth voice, and it fails.
`--wide` counts what the effect overlay's settings save instead:

| Over the 56 movies | Driver | Player, overlay on |
|---|---|---|
| Effects started | 7,252 | 7,286 |
| Effects with no track | 218 | 0 |
| Effects held, playing three times already | | 184 |
| Notes with no voice | 70 | 0 |
| Voices taken from another note | 1,045 | 0 |

**`zamn_test_bank_sfx`** loads the sets on the player and plays the monster
sounds from them: the game is untouched, every one is heard, and each is
the driver's sound on a level with that set (`$29` and `$2B` to 0.0%,
`$2A` to 3.1%, lined up to the sample).

## The player

The driver has four tracks and eight voices for the song and every effect,
and one sample set at a time. A busy fight runs out: an effect with no free
track is not started, and a note with no free voice takes one from
something else or is not played. And seven monster sounds are drawn with
the level's sample set, so a monster whose sound is in another set is
silent (`src/bank_sfx.h`). The frontend used to patch both with extra
machines: a copy of the sound chip that played the dropped effects, its
voices muted until the real driver lost one, and four more copies holding
the other sets.

`src/sound/player.c` is one engine instead. It is the driver's sequencer
written again as plain C, its state in structs, with settings for what the
console was short of:

| Setting | Console | Effect overlay on |
|---|---|---|
| Tracks for the game | 4 | 8 |
| Voices for the game | 8 | 24, on three DSPs whose outputs are added |
| One effect playing at once, at most | no limit | 3 |
| The other sample sets | | with all the monster sounds on: a driver of their own for each, two tracks and eight voices |

The limit of three is the console's own: a weapon that fires faster than
its sound ends fills every track the song leaves, three, with itself, and
that is how it sounds there. With more tracks it would stack higher.

The game still talks to the driver in C, to the cycle, since it waits on
the driver's answers. The player follows it: `Sound.watch` tells it each
command the driver picks up and each tick of the driver's timer, and it
does the same at the same cycle. It takes its start from the driver, the
RAM, tracks, voices, DSP and the samples made so far, when the driver is
first running and again after a quick load or a reset. The frontend hears
the player; with both settings off it hears the driver, as before.

The player counts the driver's cycles as `driver.c` does, routine by
routine, and makes each DSP write on its cycle. That matters: a note's
writes take the driver a thousand cycles, and the DSP takes a key-on or a
key-off only every other sample. And it keeps the driver's way with time:
the driver runs at most one tick each time round its loop, so a tick that
comes while it is busy waits, two lose one, and the song slows. The
chainsaw's first beat does that on the console.

With the console's settings the player is the driver: every DSP write on
the same cycle, and every frame's audio the same.

## What is left

- **LakeSnes's APU is still linked into `zamn.exe`**, unused. It leaves with
  the rest of the console, in step 5 of `docs/PLAN.md`.
- **The driver still runs beside the player**, for the game's timing, and
  its DSP with it. When the game stops uploading a byte at a time (step 4,
  item 3, of `docs/PLAN.md`), the player alone can be the sound.
- **`zamn_test_bank_sfx` finds too few of the game's checks.** It hears the
  monsters' checks of the sample set through `cosim_watch` on the 65816's
  program counter, and since the reset was ported the 65816 runs none of the
  game's code, so a check that is not the first instruction of a ported
  routine is never seen. Three checks of the test fail for it, as they did
  before the sound was ported, and in the game as well as the test.
- **Quick saves** made before this do not load: the sound's part of a state
  is laid out differently, and a different size, and the file is refused.
