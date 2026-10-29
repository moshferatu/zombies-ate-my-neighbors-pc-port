# The sound, ported

The game's sound no longer runs on an emulated sound chip. `src/sound` is
the console's boot loader and the game's own sound driver, written in C, on
a DSP of the port's own. In `zamn.exe` it takes the place of LakeSnes's APU,
and the APU is left alone. It is checked against the APU on every movie: the
same answers to the game on the same cycles, the same writes to the DSP on
the same cycles, the same RAM, and the same samples.

The driver is there twice. `src/sound/player.c` is the one the game runs:
the driver written again as plain C with its state in structs, with
settings for more tracks, voices and sample sets than the console had.
`src/sound/driver.c` is the driver routine by routine, its variables where
the original kept them; it is how the port was first checked against the
SPC700, and the tests keep it for that, as they keep LakeSnes.

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
| `src/sound/player.c` | The driver as the game runs it, on the chip, and with more tracks, voices and sample sets as a second player following that one |
| `src/sound/driver.c` | The driver routine by routine, for the tests only |
| `src/sound/console.c` | The one place the sound meets LakeSnes: `snes_setSound` |

What the loader starts at `$0600` is the chip's program (`SoundProgram`):
the player in the game, or `driver.c` in a test. `driver.c` keeps the
driver's variables where the original kept them, in the sound RAM, under
the names in `src/sound/driver_ram.h`, which is what let the first test
compare the RAM byte for byte. The player keeps them in structs, and the
rest of the RAM as the driver does.

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

**`zamn_test_sound --player`** is the same test with the player as the
chip's program, the game's sound as the game has it. All of the above is
checked but two things: the marks are the five the player makes (the start,
the poll, the tick, the pickup, and the loader's), and since its variables
are structs, the RAM is checked whole every 64 ticks but for `$0000` to
`$05FF`, the songs' table there excepted. Over the 56 movies:

| Checked | Over the 56 movies |
|---|---|
| Every read the game makes of the ports, byte and cycle | 108.2 million |
| The marks, each on the same cycle | 106.4 million |
| Every DSP write: register, value and cycle | 2.7 million |
| Every port write | 7.7 million |
| The sound RAM every 64 ticks | 36,125 times |
| Every sample, and each frame's audio | 152.8 million |

**`verify_corpus.ps1 -Lockstep -PortSound`** runs the corpus's lockstep pass
with the port's sound on the port's core and the APU on the stock one. The
report is the same as without it: 53 of the 56 movies never part, and the
three level 25 movies that do part at the same passes either way.

**`zamn_test_player`** runs a follower beside the player on the chip, each
with the console's settings: every DSP write the one on the chip makes must
be the follower's, register, value and cycle, and every frame's audio the
same. Over the 56 movies, 2,717,807 writes and 281,379 frames, all the same.
`--break` gives the follower a ninth voice, and it fails. `--wide` counts
what the effect overlay's settings save:

| Over the 56 movies | Console | Follower, overlay on |
|---|---|---|
| Effects started | 7,252 | 7,280 |
| Effects with no track | 218 | 0 |
| Effects held, playing three times already | | 190 |
| Notes with no voice | 70 | 0 |
| Voices taken from another note | 1,045 | 0 |

**`zamn_test_bank_sfx`** loads the sets on a follower and plays the monster
sounds from them: the game is untouched, every one is heard, and each is
the game's sound on a level with that set (`$29` and `$2B` to 0.0%,
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

`src/sound/player.c` is one engine instead. It is the driver written again
as plain C, its state in structs, with settings for what the console was
short of:

| Setting | Console | Effect overlay on |
|---|---|---|
| Tracks for the game | 4 | 8 |
| Voices for the game | 8 | 24, on three DSPs whose outputs are added |
| One effect playing at once, at most | no limit | 3 |
| The other sample sets | | with all the monster sounds on: a driver of their own for each, two tracks and eight voices |

The limit of three is the console's own: a weapon that fires faster than
its sound ends fills every track the song leaves, three, with itself, and
that is how it sounds there. With more tracks it would stack higher.

The player counts the driver's cycles as `driver.c` does, routine by
routine, every path, and makes each DSP write on its cycle. That matters
twice. The game waits on the chip's answers, a command and a byte at a
time, songs and samples included, so the answers must come on the
console's cycles, and they do only if everything before them took as long
as it did there. And a note's writes take the driver a thousand cycles, and
the DSP takes a key-on or a key-off only every other sample.

So the player with the console's settings is the program on the chip
(`player_lead`). It polls the ports, answers, takes the commands and ticks
on the timer as the driver does, and it is the driver to the cycle: the
same answers, DSP writes, RAM and samples as the SPC700. With both settings
off it is what is heard.

More tracks and voices would take more cycles, and move the answers. So
the wider settings are a second player that follows the first
(`player_follow`): `Sound.watch` tells it each command the first picks up
and each tick of the timer, and it does the same at the same cycle. It
takes its start from the first, the RAM, tracks, voices and DSP, when that
one has started, and again after a reset or a quick load that did not keep
it. It is what is heard, and the one on the chip makes no samples, which
costs nothing the game can see. It keeps the driver's way with time: the
driver runs at most one tick each time round its loop, so a tick that comes
while it is busy waits, two lose one, and the song slows. The chainsaw's
first beat does that on the console.

A quick save keeps the chip, the player on it, and the follower. One made
with other settings for the follower loads, and the follower takes its
start from the player on the chip again.

## What is left

- **LakeSnes's APU is still linked into `zamn.exe`**, unused. It leaves with
  the rest of the console, in step 5 of `docs/PLAN.md`.
- **The game still uploads a byte and an answer at a time**, and must,
  while the console's timing is kept: each answer's cycle is the game's.
  Reading the sets from the ROM instead would be the game not waiting,
  which is the other side of the first decision in `docs/PLAN.md`.
- **`zamn_test_bank_sfx` finds too few of the game's checks.** It hears the
  monsters' checks of the sample set through `cosim_watch` on the 65816's
  program counter, and since the reset was ported the 65816 runs none of the
  game's code, so a check that is not the first instruction of a ported
  routine is never seen. Three checks of the test fail for it, as they did
  before the sound was ported, and in the game as well as the test.
- **Quick saves** made before 2026-10-29 do not load: the sound's part of a
  state now holds the player on the chip, and the follower is a part of its
  own, so the file is a different size and is refused.
