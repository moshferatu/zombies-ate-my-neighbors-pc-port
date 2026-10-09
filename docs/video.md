# The picture, drawn natively

The plan this project started with kept the console's video chip emulated for
good. That has changed. The picture and the sound are to be ported as the
game's code is, until nothing of LakeSnes is left in the game.

This is where the picture stands. `src/video` is the console's video chip.
The emulated console tells it every write and asks it for every read. It
keeps the chip's registers and memories, what is noted of each frame and
what the frontend says of the picture. It finds every line's sprites and
draws every scanline the game shows, into a picture of its own. It saves and
loads its share of a state. Drawing alone, it tells the emulated PPU
nothing. The PPU is run beside it only to be checked against, or when it is
asked to draw in its place.

The game, `zamn.exe`, has no emulated PPU in it at all. A second build of
it that has one, `zamn_with_ppu`, is the test its picture is held to.

## Why it was worth doing first

A profile of `level1.zmv` at 240 Hz and 16:9, with the smoothing on:

| Share of busy samples | What |
|---|---|
| 43% | The PPU drawing the frame, a dot at a time |
| 21% | The smoothing taking the frame apart, a dot at a time |
| 18% | The 65816, the ports and the sound chip |
| 18% | Showing 240 pictures a second |

The first two are the same work done twice, and both asked the PPU about one
dot at a time: for each dot, for each layer from the front, find the tile, read
its bits, stop at the first that is not transparent.

## What a line is drawn from

`VideoState`, in `src/video/video.h`: VRAM, the palette, and the registers as
the game last set them, under names of this project's. It names nothing of the
emulator's. Before each line `src/video/chip.c` fills one from three things
it keeps: the registers (`VideoRegisters`), what the frontend has said of
the picture (`VideoPicture`) and what has been noted of the frame
(`VideoFrame`). The memories the registers point at and the two rows of the
line's sprites are kept there as well. `console.c` puts the chip in an
emulated console and `state.c` writes a state with the emulator's handler.
`beside.c` is the one file that knows the emulated PPU.

## The registers

`VideoRegisters`, in `src/video/registers.h`, is the chip as the game sees
it: the sixty-four addresses from `$2100`.

- **`video_registers_write`** is the fifty-two the game writes. What they say
  is kept decoded, under the names a line is drawn from.
- **`video_registers_read`** is the twelve it reads: the memories back, a
  multiply, the beam's latched place, and two bytes of status.
- **Three events a frame** that the game does not ask for: the frame's
  start, the check for overscan at line 225, and the picture's end.

Three of the addresses are doors into the chip's memories. Each has an
address that steps as bytes go through, and each has a catch:

- **OAM** takes a word when its second byte is written. Its address goes
  round from the sprites' words into the table of extra bits and back, and
  is put back to what the game wrote when the picture ends.
- **VRAM** steps by 1, 32 or 128, after the low byte or the high. Its
  address can have its low bits turned, and a read comes through a word
  fetched ahead.
- **The palette** takes a colour when its second byte is written.

The memories themselves are a `VideoMemory`, and `VideoRegisters` is told
where one is. `src/video` has its own, and the PPU has a second set. See
"The memories, the sprites' rows and the picture" below.

The PPU calls `Ppu.wrote`, `Ppu.didRead` and `Ppu.happened` after it has done
each thing itself. So every write is done twice, each to its own memory.

One register is written by the frontend and not the game. In a widened
picture `src/widescreen.h` moves the big figure's plane, which the game parks
for a picture 256 wide. It wrote the scroll into the PPU's struct. It now
calls `video_set_scroll`, which sets the register here and tells the PPU.

The frontend reads these registers and not the PPU's: the smoothing, the
widescreen's policies, the radar and the blood. It has the chip, from
`video_chip_of`, and reads `registers` in it under any of the three
renderers. The memories are read through them as well.

Two things the frontend asked the PPU are asked here now:

- **`video_column_empty`**: whether a background draws nothing down the
  column of tiles under one column of the picture.
- **`video_column_filled`**: whether every tile down that column has
  something in it.

A sprite's size is `video_obj_size`, which `video_sprites` already used.

Four tools share the frontend's code: `zamn_headless`, `zamn_record`,
`zamn_test_layers` and `zamn_test_radar`. They keep registers for it as the
game does. The PPU still draws their pictures.

## The picture and the frame's notes

`src/video/frame.h` holds the two things a line is drawn from that are not
the game's.

**`VideoPicture` is what the frontend says:**

- how many columns the picture goes on past the console's 256, each side;
- what each layer does out there, and the columns the level's map reaches;
- for each sprite, where it goes, how far along it is shifted, whether it is
  found before the rest, and whether it is drawn in the frontend's colours.

The frontend says each with a `video_set_` function, which keeps it here and
tells the PPU. The console's own wrappers for the same things are gone, so
nothing can be said to the PPU alone.

**`VideoFrame` is what is noted while a frame is drawn:**

- **Each line's scrolls and windows**, as the line begins. A game that draws
  a raster effect leaves only the last line's scroll in the registers.
- **Whether anything else was written on the way down.** A frame with such a
  write cannot be taken apart from its end. A frame whose windows' edges
  moved can, because they are noted by line.
- **What an automatic policy is worked out from**, kept from frame to frame:
  whether a background has been scrolled sideways on this screen, whether its
  scroll changes line by line, and whether it has nothing at the console's
  two edges.

The PPU says when a line begins, through `Ppu.beganLine`. Nothing else is
new in it.

Three things the frontend asked the PPU are asked of a `VideoState` now:

- **`video_sprite_x`**: the column a sprite is drawn at.
- **`video_math_allowed`**: whether colour maths was allowed at a column of a
  line, by the window's edges as they were on that line.
- **`video_sweep_shift`**: how far a swept layer was shifted on a line.

The fourth was one dot of a layer, `ppu_layerPixel`. The smoothing asked it
of a layer with a window on it, or in a mode other than 1. It now shows such
a frame as it was drawn, unsmoothed. No frame of the corpus is one.

The sprites come in two steps. The state has OAM and the registers that say
how to read it, and `video_sprites` finds a line's sprites from them into a
row: a palette index and a priority for each column. The line is then drawn
with that row.

## The memories, the sprites' rows and the picture

These three were the PPU's, and `src/video` wrote into them. They are kept
in `src/video` now, and the PPU's own are written only by the PPU.

**The memories.** `VideoMemory` is VRAM, the palette, OAM and OAM's extra
bits. The game's writes reach it through the three doors. Under `check` and
`emulated` the PPU decodes the same writes into its own set.

The frontend puts four things into the memories that the game did not. Each
is a function of `registers.h`, and `chip.h` has one beside it that tells
whoever is beside the chip. That is the PPU's set, where the PPU is being
kept up:

- **`video_set_sprite`**: a sprite whole, for the ones the game drops because
  they are off the console's picture and on the widened one, and for the
  radar's squares.
- **`video_free_sprite`**: the first sprite parked below the picture. It
  only reads.
- **`video_set_vram`**: a word of VRAM, for the map beyond the console's
  columns and the characters of lent sprites.
- **`video_set_colour`**: a colour, for the red of `--red-blood`.

The PPU's own functions for the first three are gone. The fourth had none:
`src/blood.h` wrote the palette through the registers' pointer, which was
the PPU's array. This file listed three writes and that one was not among
them. See the results.

**The sprites' rows.** `video_sprites` finds a line's sprites into two rows
of the chip's. The PPU's callback for it is now called on every line, blanked or
not, and whether a blanked line's sprites are looked for is decided here from
the registers here. When the PPU finds them, for a line that is declined or
under `emulated`, its rows are copied here.

**The picture.** A line is put into a buffer of the chip's, laid out as the
PPU's is: two fields of 239 lines, eight bytes a column. The frontend takes
it with `video_put_pixels`, which hands over 480 rows as the PPU's did, and
asks its width with `video_output_width`. `video_set_pixel_format` says
which end of a pixel's four bytes is unused. A line the PPU draws is copied
here, so the picture is taken from one place under all three renderers.
Under `check` the picture shown is the PPU's.

**A saved state** was the PPU's when these three moved. It is written from
here now: see the next section.

`zamn_headless` takes `--renderer`, as the game does, so a state can be
saved and loaded under each of the three.

## The console's chip

Until this step the emulated console drove its PPU, and the PPU called
`src/video` after each thing it had done. Every write was decoded twice
under every renderer. The console now asks whoever is its video chip, and
in the game that is `src/video`.

**`SnesVideo`**, in the console's `snes.h`, is what a console asks of its
chip:

- a write to `$2100`-`$213F`, and a read;
- the frame's start, the overscan check and the picture's end;
- a line of the picture;
- a reset;
- the chip's share of a state, saved or loaded;
- which field the frame is and whether it is interlaced, which decide how
  long a line and a frame are.

A console made by `snes_init` starts with its own PPU behind those.
`video_beside_attach` puts the chip there with the PPU beside it, and
`video_console_attach` puts the chip there alone: see the next two
sections. `zamn_cosim` and the tools that attach nothing run on the PPU as
they did.

**What each renderer does with them.**

| | `native` | `check` | `emulated` |
|---|---|---|---|
| The PPU is told writes, events and what the frontend says | no | yes, first | yes, first |
| The console's reads are answered by | `src/video` | the PPU | the PPU |
| A line's sprites, and the line | `src/video` | both, compared | the PPU |
| The frame's timing is asked of | `src/video` | the PPU | the PPU |
| The state saved is | `src/video`'s | the PPU's, compared | the PPU's |

The PPU's callbacks for a write, a read and an event are gone. Its three
for a line remain, and are how `check` and `emulated` run a line: the PPU
notes the line, and calls back to have the sprites found and the line drawn.

**A line left to the PPU under `native`.** A line `video_declines` is still
drawn by the PPU, which by then has been told nothing since the console was
attached. It is first told everything at once: the registers, the memories,
the notes, the picture as the frontend has said it, and the line's sprites
if they were found here. Nothing this game shows is such a line, so no frame
pays for it.

Until this step such a line lost its sprites under `native`. The PPU drew
it from a row of its own that nobody had filled. Nothing showed it, because
no line of the game is declined. It is tried now by declining every odd
line on purpose: see the checks.

**A saved state.** `video_state_handle` saves and loads the chip's share in
the layout the PPU has always used: the registers a field at a time in the
PPU's order, then the memories, then the two rows of the last line's
sprites. A state saved by either is loaded by either, and a quick save made
by an earlier build still loads.

Two things in it are read by nobody after a load: where the PPU had got to
in a line of mode 7, for which zero is saved here, and a sprite's priority
where there is no sprite. Under `check` the state as it would be saved from
here is compared with the PPU's, those two aside. A load is into both, each
from the same bytes, and the registers and memories are then compared as
after anything else.

A state saved under `native` used to differ from the PPU's in its row of
sprite pixels. It is the same file now, byte for byte, in every case tried.

## The chip, and the PPU beside it

One struct had been all of this: the chip, the PPU's pointer, and what a
check had found. It was named `VideoHook`, for what it began as, and the
frontend's handle on it was the PPU's pointer. It is split now.

**`VideoChip`**, in `chip.h`, is the chip: the registers and the memories,
the picture as the frontend says it, the frame's notes, the line's sprites
and the picture. It includes nothing of the emulator's. Whoever runs the
console calls `video_chip_write`, `video_chip_read`, the three events and
`video_chip_run_line`.

The frontend has the chip itself, from `video_chip_of`. It reads
`registers`, `picture` and `frame` in it by name, and calls
`video_set_margins` and the rest on it. Nothing of the frontend's is handed
a PPU now. The PPU is named where a console is set up, and by
`zamn_test_radar`, which has it draw a frame again.

**`VideoOther`** is how the chip speaks to somebody with another chip
beside it. It is three functions. One is told each thing the frontend says
or puts, after the chip has done it. The other two are asked for a line's
sprites the chip does not find, and for a line it does not draw. With
nobody there, such a line is black and has no sprites.

**`VideoBeside`**, in `beside.h`, is that somebody: a chip in its console,
the console's PPU and the renderer. It is what a console with a PPU is
given as its video chip (`SnesVideo`), and it does to the PPU what the
renderer says. Everything a check counts is in it. It was named
`VideoConsole` for one commit, and `VideoHook` before that.

**`state.h`** is the chip's share of a saved state, moved out on its own.
It is written with the emulator's state handler and knows nothing else of
the emulator.

Under `native` a line is run by the chip alone, in `chip.c`. Under `check`
and `emulated` the PPU runs it and calls back into `beside.c`, which calls
the chip for its part. Those had been one function with three branches.

**The noise tests.** A `VideoHook` could be put on a PPU with no console,
and a line was then drawn from the PPU's own registers, by a second way of
filling a `VideoState`. That way is gone. `zamn_test_video` still puts its
noise straight into a PPU. It then has the chip take what is in the PPU
(`video_beside_take`) before a line is drawn. So the chip draws from
itself in the test as it does in the game, and the frame's notes are
compared in the test as well, which they were not.

`zamn_test_registers` does to a `VideoChip` what it did to the registers
and the notes separately, so the chip's own functions are what the noise
runs.

**An overrun in the emulated PPU.** Comparing the notes in the noise test
found one at once. Given the sprites under the policy that centres a layer,
the PPU kept a record for layer 4 in arrays of four. It wrote two bytes
past the last of them, into its own flags for which backgrounds have
scrolled. The frontend never says that policy of the sprites, and
`src/video` declines such a line, so no picture of the game had it. The old
test read those flags back out of the PPU, so both ways drew from the same
rubbish and agreed. Under that policy the PPU now shows nothing of the
sprites outside their own 256 columns. The noise test's checksum changed
with that and with nothing else: before the mend, the new build came to the
old number.

## A build without the PPU

Three things called the emulated PPU from code the game links, and each had
to stop before a program could be built without it.

**The emulated console.** `snes_init` made a PPU, and the same file held the
PPU as the console's default video chip. `snes_initWithoutPpu` now makes a
console with no PPU and nobody for its video chip, and calls nothing of the
PPU's. What does call it is in one new file of the console's, `snes_ppu.c`:
`snes_init`, the PPU as a `SnesVideo`, and the three functions that hand
over the PPU's picture.

**`console.c`.** It is two files now.

- **`VideoConsole`**, in `console.h`, is a chip in a console and nothing
  else. `video_console_attach` makes it the console's video chip. Every
  one of the ten things a console asks is one call of the chip. Nobody is
  beside the chip, so a line it declines is black.
- **`VideoBeside`**, in `beside.h`, is a `VideoConsole` with the PPU and
  the renderer. Under `native` it attaches the console's own ten functions
  and sets itself as the chip's `VideoOther`. Under `check` and `emulated`
  it attaches ten of its own, each of which does the thing to the PPU
  first.

**The lockstep harness.** `cosim_lockstep` made its two consoles, and the
game links the file it is in. They are handed to it now.

**Which a program has is settled where it is linked.** `console.h` declares
three functions that are written twice: `video_snes_init`, which makes a
console drawn by a renderer, `video_snes_report` and `video_snes_free`.

| | `with_ppu.c` | `without_ppu.c` |
|---|---|---|
| The console is made by | `snes_init` | `snes_initWithoutPpu` |
| Its video chip is | a `VideoBeside` | a `VideoConsole` |
| Renderers | all three | `native` |
| Asked for another | | `video_snes_init` returns NULL |
| A declined line | the PPU's | black, and the run exits 1 |

There is no `#ifdef`. The frontend and the tools call those three and name
no PPU, except `zamn_test_radar` where it has the PPU draw a frame again.

**Two libraries of the console's.** `snescore_without_ppu` is everything
but `ppu.c` and `snes_ppu.c`, and `snescore` is those two on top of it. The
harness and `src/video` link the first. So a program that links
`zamn_video_without_ppu` has no PPU on its link line, and a call of the PPU
from anywhere in it is a link error. A PPU cannot be linked in quietly.

**`zamn_native`** is `src/main_sdl.c` built a second time against that. It
is left in the build directory, and `zamn.exe` is as it was. Given
`--renderer check` or `emulated`, or `--stock` alone, it says it has no PPU
and exits. It is 41 KB smaller than `zamn.exe`.

`tools/verify_corpus.ps1 -Picture` now takes its second checksum from
`zamn_native`. That run used to be `zamn.exe --renderer native`, which had
a PPU to fall back on.

**The two have since changed places.** `zamn.exe`, the game the launcher
starts and the release zips, is the build without the PPU. The build with
it is `zamn_with_ppu`, left in the build directory, and is what
`--renderer check` and `emulated` are run in. Wherever this document says
`zamn_native` it means what is now `zamn.exe`, and where it says a check
was run in `zamn` it would now be run in `zamn_with_ppu`.

`--stock` used to have the PPU draw unless told otherwise. It still does in
`zamn_with_ppu`. In `zamn.exe` it is drawn by `src/video`, as everything
there is.

`tools/verify_corpus.ps1 -Picture` runs the check in `zamn_with_ppu` and
takes its second checksum from `zamn.exe`.

## How a line's sprites are found

As the console finds them, because the game can see how it does.

1. **The sprites that cross the line**, in OAM's order, and the first 32 kept.
2. **Their tiles**, from the last kept to the first, eight pixels at a time
   until 34 have been fetched. Each is written over what was there, so the
   sprite found first ends up in front.

A line with more than 32 sprites, or more than 34 tiles, loses some, and
sets a flag the game can read from `$213E`. Both limits and both flags are
kept. A widened picture has the limits in proportion to its width.

The frontend's additions are kept too: sprites marked to be found before the
rest, sprites drawn in colours of the frontend's, and the three places a
sprite can have in a widened picture. A sprite placed with a centred layer
is found up to three times, once for the layer and once for each margin.

Half-height sprites, which the game never asks for, are left to the PPU,
and what it finds is copied into the rows here.

## How a line is drawn

1. **Each background's line, a tile at a time.** `tile_row` reads one row of
   one tile as eight pixels at once. A table turns each byte of a bitplane
   into eight bytes, one a pixel, and four of them shifted and ORed are the
   row. The eight go to one of two rows of the line: one for tiles that go
   behind and one for tiles that go in front.
2. **The rows painted from the back.** Mode 1 has ten places in its stack: a
   background's two rows and the sprites' four priorities, in a fixed order.
   Each place that has anything in it is one pass over the line.
3. **Each column's colour looked up.** Two tables of 256, the palette as it
   goes to the screen and the palette with the fixed colour's maths done.
   They are made again only when the palette, the brightness or the fixed
   colour changes.

Where the game adds the sub screen, the sub screen's line is painted the same
way from the same rows, and the two colours are added a column at a time.

The colour window is two ranges of columns, so it is worked out as ranges and
not asked about a column at a time.

## The widened picture

A layer's policy says which column of the layer each column of the picture
shows. Every policy is a few runs of consecutive columns, so a widened line is
still a few runs of whole tiles.

| Policy | Runs |
|---|---|
| clip | the console's 256 |
| clamp edge | the 256, and each margin the nearer edge column's pixel |
| stretch | the columns the world has anything in |
| anchor | the left half at the left edge, the right half at the right |
| tile | the console's 256, repeated |
| centre clip | the 256 at the picture's middle |
| centre | the same, and each margin: nothing, the 256 again, or one pixel carried out |
| sweep | the layer shifted, and its first column carried out to the left |

Nothing sets clamp edge. It is three runs, so it is drawn, and the noise
test checks it.

**The centred layer's margins are searched for once.** The game over's mask
is the one user. What was found is kept by line and scroll in `VideoFrame`
(`VideoCentre`). The PPU keeps its own for the lines it draws, and the two
are no longer one copy: under the check each searches for itself, and the
lines still come out the same. The smoothing reads the frame with a copy of
what the lines found, and leaves theirs alone.

## What it does not draw

`video_declines` names the first thing about a line that `video_line` does
not draw as the PPU does, and such a line is left to the PPU:

- a mode other than 1;
- mosaic, pseudo hi-res or overscan;
- a window on a layer;
- the main screen clipped by the colour window;
- colour maths halved;
- sprites placed other than with the world or the console.

Over the corpus the game does none of them. A survey of every line of all 54
movies found 15 kinds of line, all mode 1 with BG3's high tiles in front. The
only window in use is the colour window, to say where colour maths applies.

## The smoothing's planes

`src/layers.h` takes a frame apart into a plane for each background and
priority. It asked `ppu_layerPixel` for each dot of each plane, and worked out
each dot's colour.

`video_bg_row` is the same line reader, over a few more columns either side of
the picture and a few lines above and below it, read with the scroll recorded
as each line was drawn. `layers_planes_by_row` fills a plane a line at a time
from it, with each colour looked up in a table made once for the layer.

A frame with a window on a background, or in a mode other than 1, is not
taken apart. It was read a dot at a time by the PPU. It is now shown as it
was drawn.

## How it is checked

- **`zamn --renderer check`** finds every line's sprites both ways, draws
  every line both ways, and compares each column by column, with the two
  flags. It reports the lines that differ and the first of them, and exits 1
  if there were any.
  It also compares the registers with the PPU's after every write, every
  read and every event, each register by name, and a read's two answers.
  And it compares the frame's notes with the PPU's own after every write,
  every line and every frame's top, and at a frame's top what the frontend
  has said of the picture with what the PPU was told.
  And it compares the three memories whole with the PPU's at each of a
  frame's three events.
  And it compares a state as it would be saved from here with the state the
  PPU saves, and after a load the registers and the memories.
- **`zamn_headless --renderer`** with `--save` and `--load` saves a state
  under one renderer and loads it under another. Loaded under `check`, every
  comparison above runs from the load on.
- **`tools/verify_corpus.ps1 -Picture`** runs every movie of the corpus under
  the check, then again in `zamn_native`, which has no PPU, and compares a
  checksum of every line of the picture between the two. `-Widescreen
  off,16:9,16:10,21:9` is all four widths.
- **`zamn_test_video`** needs no ROM. It fills a PPU with noise: VRAM, the
  palette, OAM, the scrolls, the layers, the maths, the window, the margins
  and the policies. Every line is drawn both ways, and some rows are read
  back with `video_bg_row` against `ppu_layerPixel`. A share of the frames
  have something `video_declines`, so a line that should have been declined
  and was drawn wrong shows as a line that differs.

- **`zamn_test_registers`** needs no ROM. A PPU and a `VideoRegisters`, with
  memories of their own, have the same things done to them at random: any
  byte written to any address, any address read, the beam moved, the three
  events, a reset. Every register is compared after each, and the memories
  every 64 steps. A third of the writes and reads go to the three doors.
  Every eighth step both are asked the two questions about a column.
  A `VideoFrame` is kept beside the registers. Lines begin at random, a
  scroll is written over and over some frames, the picture is widened and
  its policies changed, and the notes are compared after each. Every eighth
  step both are also asked where a sprite is drawn, whether maths is allowed
  at a column of a line, and how far a background is shifted.
  One step in a hundred puts something into the memories from outside: a
  sprite, a word of VRAM or a colour. The PPU's is done as the PPU's own
  functions did it, which the test keeps a copy of. The memories are compared
  whole at once, and so is which sprite each says is free.
  One step in eight hundred is a state. Both are saved and the two states
  compared, or a state the PPU saved some steps back is loaded into both
  from the same bytes.

A rule broken on purpose is caught: `zamn_test_video` with a 16x16 tile's
lower half read one character out reported 22,756 lines differing of 51,520.

For the sprites, three rules broken in turn over 600 frames: one sprite too
many kept, 1,691 lines differing; one tile too many fetched, 39,834; the
flag for too many sprites never set, 53. The first of those was not caught
until the noise was changed. Sprites at random are spread too thin to crowd
a line, so one frame in four now has them in a band of lines.

For the registers, nine rules broken in turn over 600,000 steps, and each
was caught and named:

| Broken | Steps wrong | First named |
|---|---|---|
| A scroll's low bits from the wrong latch | 6,518 | `bg[i].hscroll` |
| VRAM's address turned one bit short | 3,169 | VRAM |
| An OAM read not going on into the extra bits | 7 | `oam_high` |
| A colour's sixteenth bit read as 0 | 954 | `bus2` |
| OAM's address put back under a blanked screen | 3,792 | `oam_at` |
| The fixed colour's green set by the red bit | 1,809 | `fixed_g` |
| A status read not letting the counters latch again | 783 | `latched` |
| VRAM stepping after the wrong byte | 15,228 | `vram_at` |
| An OAM word written with its bytes swapped | 2,826 | OAM |

And five of the two questions about a column, over the same 600,000 steps:

| Broken | Steps wrong |
|---|---|
| A big tile's lower two characters looked for a row out | 1,084 |
| The map's second screen across never read | 2,100 |
| The half row at the picture's bottom left out | 83 |
| Mode 1's third background read as sixteen colours | 570 |
| Filled asked of the scroll's column and not the picture's | 5,444 |

Twenty of the notes and of what the frontend asks. Eighteen over 600,000
steps of `zamn_test_registers`:

| Broken | Steps wrong | First named |
|---|---|---|
| A scroll counted when written, not when changed | 894 | `hscroll_writes` |
| A raster read one change late | 30 | `raster` |
| A faded-out picture not ending a screen | 387 | `scrolled` |
| Window 2's right edge noted as any other write | 6,214 | `mid_frame_writes` |
| BG4's scroll down noted as any other write | 3,799 | `mid_frame_writes` |
| The right edge looked at a column in | 39 | `edge_empty` |
| The edges looked at on the console's picture too | 2,256 | `edge_empty` |
| A line's second window noted right then left | 47,856 | `line_window` |
| A line's scroll down noted from its scroll across | 48,056 | `line_vscroll` |
| The first mid-frame write's line a line out | 8,880 | `mid_frame_line` |
| An anchored sprite's left half moved by the right margin | 901 | where a sprite is drawn |
| A sprite's X coming round at the console's edge | 14,928 | where a sprite is drawn |
| A centred sprite not moved to the picture's middle | 1,703 | where a sprite is drawn |
| Maths asked of the last line's window, not the line's | 4,830 | whether maths is allowed |
| Maths allowed inside the window where it is outside | 33,531 | whether maths is allowed |
| A line above the picture asked as line 0 | 230 | whether maths is allowed |
| The sweep's shift rounded down | 257 | how far it is shifted |
| A shift asked of a layer that is not swept | 43,017 | how far it is shifted |

And two of clamp edge, over 300 frames of `zamn_test_video`: the right
margin taken from a column in, 1,277 lines and 135 rows read back; the left
margin taken from the line above, 1,207 lines and 131 rows.

Eighteen of the memories, the rows and the picture. Each was caught by the
run named:

| Broken | Caught by | What it said |
|---|---|---|
| A sprite put with its size bit in the ninth bit's place | noise, registers | 1,343 steps, OAM's extra bits |
| A sprite put without X's ninth bit | noise, registers | 1,354 steps, OAM's extra bits |
| A sprite on line `$F0` not read as parked | noise, registers | 304 steps, which sprite is free |
| A word put into half of VRAM | noise, registers | 968 steps, VRAM |
| A colour put into half of the palette | noise, registers | 681 steps, the palette |
| Sprites looked for on a blanked line | noise, video | 3,134 lines' sprites |
| The sprites' row not cleared before a line | noise, video | 16 lines' sprites |
| Sprites the PPU found not copied here | noise, video | 1,282 lines |
| A colour put here and not into the PPU's palette | a forced game over, check | 394 events, the palette |
| A sprite put here without the PPU's extra bits | the same | 981 events, OAM's extra bits |
| A word of VRAM put here and not into the PPU's | the same | 1,717 events, VRAM |
| A loaded state's memories left as they were | a state loaded under check | VRAM, at the first frame |
| A line put a byte out of place | a movie alone | another checksum |
| A line the PPU drew not copied here | a movie, emulated | another checksum |
| The picture's second rows taken from the other field | a screenshot | another picture |
| The picture handed over two rows high | a screenshot | another picture |
| A line drawn into the field that is not shown | a screenshot | another picture |
| The picture's width without its right margin | a screenshot | another picture |

Nineteen of the console's chip. Each was caught by the run named:

| Broken | Caught by | What it said |
|---|---|---|
| A state with two of its bytes in each other's place | noise, registers | 568 steps, the state as saved |
| A state without the second window's inversion | noise, registers | 743 steps, the state as saved |
| A state with OAM's address where the one last written goes | noise, registers | 448 steps, the state as saved |
| A state loaded with a background's scrolls in each other's place | a state loaded under check | `bg[i].hscroll`, after the load |
| A state's sprites saved from another row | a state saved under check | the sprites' row as saved |
| A state saved from here with OAM where the palette goes | a state saved alone | another file |
| The console told the wrong field of the two | a state saved on an odd frame, alone | another file |
| The console told every frame has overscan | a forced game over, alone | another checksum |
| The console told every frame is interlaced | the same | another checksum |
| A line's sprites not found | the same | another checksum |
| A sprite the frontend puts not put under `native` | the same | another checksum |
| A line's scrolls not noted as it begins | a movie alone | another checksum |
| The PPU not told a colour the frontend puts | a forced game over, check | 394 events, the palette |
| Odd lines left to a PPU not told the registers | a forced game over, alone | another checksum |
| ...not told the memories | the same | another checksum |
| ...not told the picture | the same | another checksum |
| ...not given the line's sprites | the same | another checksum |
| ...not told the notes | a movie alone | another checksum |
| Odd lines' sprites left to a PPU told nothing | a forced game over, alone | another checksum |

The first time through, three of these broke nothing that could be seen.
Two saved one field in another's place where the two happened to be equal.
The third told the console the wrong field, which moves a line four cycles
shorter from one frame to the next and changes no pixel. It shows in a
state saved on an odd frame, which has the console's clock in it.

And five things done on purpose that must change nothing, on the forced
game over. Each came to the right checksum:

- every odd line left to the PPU under `native`, 403,536 lines;
- every odd line's sprites left to it;
- both;
- both under `check`;
- the PPU's memories and registers filled with rubbish once the console is
  attached, under `native`.

Thirty of the chip and its console. Each was caught by the run named:

| Broken | Caught by | What it said |
|---|---|---|
| The chip's reset not its registers' | noise, registers | 18 steps, `blank` |
| A write told to the chip as made outside the picture | noise, registers | 114,356 steps, `mid_frame_write` |
| The frame's start not noted by the chip | noise, registers | 10,960 steps, `mid_frame_write` |
| A line's beginning not noted by the chip | noise, registers | 48,129 steps, `line_hscroll` |
| A colour put into the chip at the next index | noise, registers | 1,155 steps, the palette |
| A state with two of its bytes in each other's place | noise, registers | 568 steps, the state as saved |
| The chip not taking the PPU's notes | noise, video | 300 frames' tops, `last_hscroll` |
| The chip not taking the PPU's registers | noise, video | 65,820 lines' sprites |
| A line's scrolls not noted as it begins | a movie alone | another checksum |
| A line's sprites not found | a forced game over, alone | another checksum |
| A sprite the frontend puts not put | the same | another checksum |
| The margins the frontend says held to one size | the same | another checksum |
| The frame's start not told to the chip | the same | another checksum |
| The console told every frame is interlaced | the same | another checksum |
| A line put into the other field's row | a screenshot | another picture |
| The picture handed over from line 2 | a screenshot | another picture |
| A state saved from the chip with OAM where the palette goes | a state saved alone | another file |
| The PPU not told a colour the frontend puts | a forced game over, check | 394 events, the palette |
| ...a layer's policy | the same | 3,602 frames' tops, a layer's policy |
| ...a sprite's place | the same | 394 frames' tops, a sprite's place |
| ...a sprite's extra bits | the same | 981 events, OAM's extra bits |
| The PPU told a word of VRAM at the next address | the same | 1,717 events, VRAM |
| The PPU told what the frontend says under `native` and not under the check | the same | 1,717 events and 3,603 frames' tops |
| Odd lines left to a PPU not told the registers | a forced game over, alone | another checksum |
| ...not told the memories | the same | another checksum |
| ...not told the picture | the same | another checksum |
| ...not given the line's sprites | the same | another checksum |
| ...not told the notes | a movie alone | another checksum |
| Odd lines left, and the PPU's line not copied to the chip | a forced game over, alone | another checksum |
| Odd lines' sprites left to a PPU told nothing | the same | another checksum |

Two more were not caught, and a third was no test at all:

- **The console's reset not passed to the chip.** The same checksum. A
  movie resets the console once, before anything is written, when the chip
  is already as a reset leaves it. The chip's own reset is tried by the
  registers' noise test, which is the first row above. That the console
  passes one on in the middle of a run is tried by nothing.
- **The PPU not told a sprite's shift.** Clean under the check. A sprite is
  shifted only on the winner's screen, for its fireworks, and no movie of
  the corpus gets there. This was so before this step too.
- **The check's line compared with the chip's own row.** It found nothing,
  and could not have: that is the check made blind, and a check does not
  catch that itself.

One more is not a breakage and not a no-change. With every odd line left
and nobody given to draw it, the picture comes to the checksum it comes to
when a PPU told nothing draws those lines: they are black.

The five things that must change nothing were done again, on the forced
game over, and each came to the right checksum: every odd line left to the
PPU under `native`, every odd line's sprites, both, both under `check`, and
the PPU filled with rubbish once the console is attached.

Eleven of the console with no PPU. Each was run in `zamn_native` on two
movies and a forced game over, with a quick save made on an odd frame.

| Broken | Caught by | What it said |
|---|---|---|
| The frame's start not passed to the chip | any of the three, alone | another checksum |
| The console told every frame has overscan | the same | another checksum |
| The console told every frame is interlaced | the same | another checksum |
| A line run as the line after it | the same | another checksum |
| The console told the wrong field of the two | the quick save, alone | another file |
| No state saved from the chip | the same | another file |
| A read answered with nothing | a forced game over, check | 1 read, of `$213F` |
| Every write told to the chip as made outside the picture | the same | 240,307 notes, `mid_frame_write` |
| A write told to the chip as on the line before | the same | 2 writes and 19 notes, `mosaic_from` |

The last three changed no checksum in `zamn_native`. They are of the two
functions the check runs as well, a write and a read as the console makes
them, and `zamn --renderer check` caught each.

Two were not caught by anything:

- **The console's reset not passed to the chip**, as before, and for the
  same reason.
- **The picture's end not passed to the chip.** It puts OAM's address back
  and settles whether the next frame is interlaced. The game sets the
  address itself every frame and never interlaces.

Both are one line that calls the chip, and the chip's side of each is
tried by the registers' noise test.

## Results

**Checked**, on the build that is described here:

- **The corpus.** 54 movies at four widths: 310,747,136 lines drawn both ways
  and none differ, and no line's sprites differ. None was left to the PPU.
  No write, read or event left a register other than the PPU's. Each of the
  216 runs comes to the same checksum with `src/video` working alone.
  The first pass did not: 12 movies at 16:9, 9 at 16:10 and 8 at 21:9 drew a
  different picture alone, on the levels with a big figure. That was the
  scroll the frontend wrote past the registers, above.
- **The registers at random.** 13,000,000 steps over three seeds: 7,798,816
  writes, 3,250,101 reads and 782,591 events, and none left a register, an
  answer or a memory other than the PPU's. 1,478,376 columns were asked
  about, 312,744 of them empty and 206,976 filled, and the PPU said the same
  of every one.
- **The frontend on these registers.** The corpus at four widths again: no
  line differs, no register differs, and each of the 216 runs comes to the
  same checksum alone. Two movies at 16:9 come to one checksum under all
  three renderers. Against the build before, `zamn_test_radar` prints the
  same at 4:3 and 16:9, and `zamn_headless` writes the same picture from two
  movies. `zamn_test_layers` passes on four runs: 8,997 frames drawn as a
  list and none differing.
- **A mistake of mine, three commits old.** `zamn_test_layers` and
  `zamn_record` had read every background as empty since the picture was
  first drawn here. `video_bg_row` wants a table that only `video_init`
  made, and those two never called it. The game did, so the game was right
  and I did not run the test. The build before this one fails it on 2,681
  frames of 2,686. The table is now made by whatever first needs it, and
  that change alone makes the build before pass.
- **The picture and the notes kept here.** The corpus at four widths again:
  310,747,136 lines, none differing, none left to the PPU. No write, line or
  frame's top left a note, or the picture as it was said, other than the
  PPU's. Each of the 216 runs comes to the same checksum alone. That was
  before the fix below, which the corpus does not reach; 16:9 was run again
  after it and is the same.
  `zamn_test_registers`, 13,000,000 steps over three seeds: 24,892,754
  writes, 1,949,790 reads, 781,456 events and 1,038,963 lines begun, and no
  register, answer, memory or note other than the PPU's. Both were asked
  the five things 1,478,677 times and said the same. `zamn_test_video`,
  6,000 frames over two seeds with clamp edge drawn: 1,076,320 lines drawn
  both ways and 200,688 rows read back, none differing.
  Against the build before: `zamn_test_layers` prints the same on seven
  runs, 17,605 frames drawn as a list and none differing, two of the runs a
  forced game over, which is the one screen with a centred layer;
  `zamn_test_radar` the same at 4:3 and 16:9; `zamn_headless` writes the
  same picture from two movies; and `zamn_record` the same pictures from
  three recordings, one of them the game over's mask. The forced game
  over comes to one checksum under all three renderers at 16:9, 16:10 and
  21:9, and at 16:9 and 21:9 it is the build before's.
- **A mistake of mine, caught by that last comparison.** `--red-blood` had
  stopped changing the picture. `src/blood.h` built its table of colours and
  I left out the call that hands it over. Nothing else noticed. The check
  compares `src/video` with the PPU, and both had been told nothing. The
  layers test counts the frames the drips were marked on, not what colour
  they came out. The build before's checksum for that run was a different
  number.
- **The memories, the rows and the picture kept here.**
  The corpus at four widths again: 310,747,136 lines, none differing, none
  left to the PPU. No register, memory or note was other than the PPU's, and
  each of the 216 runs comes to the same checksum alone. Some comments were
  mended after that pass. Everything below is on the build after.
  `zamn_test_registers`, 13,000,000 steps over three seeds: 24,889,909
  writes, 1,820,766 reads, 781,462 events, 1,038,936 lines begun and 130,378
  things put into the memories from outside, and nothing other than the
  PPU's. `zamn_test_video`, 6,000 frames over two seeds: 1,261,568 lines'
  sprites found both ways, 1,076,320 lines drawn both ways and 200,724 rows
  read back, none differing.
  Three renderers come to one checksum on two movies at 16:9, and on the
  forced game over with red blood at 16:9, 16:10 and 21:9. Each is the
  number the build before came to.
  Against the build before: `zamn_test_layers` prints the same on seven
  runs, 17,605 frames drawn as a list and none differing; `zamn_test_radar`
  the same at 4:3 and 16:9; `zamn_headless` writes the same picture from
  three movies; and `zamn_record` the same bytes from four recordings.
- **A saved state.** `zamn_headless` on two movies at two widths, saved at
  frame 1,500 and run to 3,000. A state saved under `native` and one saved
  under `emulated` were each loaded under all three renderers. Each of the
  six loaded runs writes the picture the unbroken run writes, the six come
  to one checksum, and under `check` nothing differs after the load: no
  line, no line's sprites, no register, no memory and no note.
- **A fourth write I had not listed, caught twice.** With the memories
  apart, `--red-blood` drew its red under `native` and not under `check` or
  `emulated`: `src/blood.h` put its three colours into the palette through
  the registers' pointer, which had been the PPU's array and was now only
  the one here. The forced game over showed it as two checksums where there
  should be one, and the check's new comparison named it: the palette, 394
  times. It calls `video_set_colour` now. The corpus would not have shown
  it, having no red blood.
- **Linux again.** The tree builds in WSL with gcc 13 and both noise tests
  pass there. `level25-boss.zmv` and the forced game over with red blood, at
  16:9, are clean under the check and come to the Windows build's checksums
  alone.
- **The console's chip.**
  The corpus at four widths again: 310,747,136 lines, none differing, none
  left to the PPU. No register, memory or note was other than the PPU's, and
  each of the 216 runs comes to the same checksum with the PPU told nothing.
  The first pass said NOT CHECKED of every movie. I had reworded a line of
  the check's report, and the corpus script reads that line. The script was
  right to refuse, and reads the new wording now.
  The game's own corpus, which runs on the PPU through the same ten
  functions: 34,603,665 calls checked over 54 movies and none diverged, and
  under lockstep 334,319 passes with 51 of 54 never parting, the same three.
  Both are the figures from before. The first of those passes had not been
  run without `-Coverage` for some time and did not start: an empty list of
  extra arguments reached the program as a null. That is mended in
  `tools/verify_corpus.ps1`.
  `zamn_test_registers`, 13,000,000 steps over three seeds: 24,893,913
  writes, 1,819,398 reads, 781,999 events, 1,039,619 lines begun, 113,817
  things put into the memories from outside, 7,996 states saved and
  compared and 8,196 loaded, and nothing other than the PPU's.
  `zamn_test_video`, 6,000 frames over two seeds: 1,261,568 lines' sprites
  found both ways, 1,076,320 lines drawn both ways and 200,724 rows read
  back, none differing.
  Three renderers come to one checksum on two movies at 16:9, and on the
  forced game over with red blood at 16:9, 16:10 and 21:9. Each is the
  number the build before came to.
  A saved state, on two movies at two widths, saved at frame 1,500 and run
  to 3,000. The states saved under `native`, `emulated` and `check` are one
  file in each of the four cases, and so are three saved on an odd frame.
  Each of the twelve was loaded under all three renderers: every one of the
  36 loaded runs writes the picture the unbroken run writes, and under
  `check` nothing differs after the load or at the save.
  Against the build before: `zamn_test_layers` prints the same on seven
  runs, 17,605 frames drawn as a list and none differing; `zamn_test_radar`
  the same at 4:3 and 16:9; `zamn_headless` writes the same picture from
  three movies; and `zamn_record` the same bytes from four recordings.
  On Linux, in WSL with gcc 13: the tree builds and both noise tests pass.
  `level25-boss.zmv` and the forced game over with red blood, at 16:9, are
  clean under the check and come to the Windows build's checksums under all
  three renderers. A state saved on an odd frame is the same file under all
  three, and the file the Windows build saves.
- **The chip and its console.**
  The corpus at four widths: 310,747,136 lines, none differing, none left to
  the PPU. No register, memory or note was other than the PPU's, and each of
  the 216 runs comes to the same checksum with the PPU told nothing.
  The game's own corpus, run because `ppu.c` changed by a line: 34,603,665
  calls checked over 54 movies and none diverged, as before. The lockstep
  pass was not run again. It draws no widened picture, which is the only
  place that line is reached.
  Three renderers come to one checksum on two movies at 16:9, and on the
  forced game over with red blood at 16:9, 16:10 and 21:9. Each is the
  number the build before came to.
  States saved at frame 1,500 on two movies at two widths, and on an odd
  frame, are each one file under the three renderers, and each is the file
  the build before saved. Each of the twelve was loaded under all three:
  every one of the 36 runs writes the picture the unbroken run writes, and
  under `check` nothing differs after the load or at the save.
  `zamn_test_registers`, driving the chip, 13,000,000 steps over three
  seeds: the same 24,893,913 writes, 1,819,398 reads, 781,999 events,
  1,039,619 lines, 113,817 things put, 7,996 states saved and 8,196 loaded
  as before, and nothing other than the PPU's.
  `zamn_test_video`, 6,000 frames over two seeds: the same 1,261,568 lines'
  sprites found both ways, 1,076,320 lines drawn both ways and 200,724 rows
  read back, none differing, and now 1,344,000 lines' notes compared, none
  differing.
  Against the build before: `zamn_test_layers` prints the same on seven
  runs, 17,605 frames drawn as a list and none differing; `zamn_test_radar`
  the same at 4:3 and 16:9; `zamn_headless` writes the same picture from
  three movies; and `zamn_record` the same bytes from four recordings.
  On Linux, in WSL with gcc 13: the tree builds and both noise tests pass.
  `level25-boss.zmv` and the forced game over with red blood, at 16:9, are
  clean under the check and come to the Windows build's checksums under all
  three renderers. A state saved on an odd frame is the same file under all
  three, and the file the Windows build saves.
- **A build without the PPU.**
  `zamn_native` links against a console that has no PPU in it, and builds.
  On Linux its symbols were counted: none of the PPU's, against 47 in
  `zamn`.
  The corpus at four widths: 310,747,136 lines drawn both ways by `zamn`,
  none differing and none left to the PPU, and each of the 216 runs comes
  to the same checksum in `zamn_native`, which exits 0 on every one.
  It comes to the three renderers' checksum on two movies at 16:9 and on
  the forced game over with red blood at three widths.
  A quick save made by the command line (`--quick-at`), on an even frame
  and on an odd one, is the same file from `zamn` under each of the three
  renderers and from `zamn_native`. One made by either was loaded by
  `zamn` under `check` and `native` and by `zamn_native`: twelve runs,
  each the checksum of the others loading the same file, and nothing
  differing under the check.
  The game's own corpus, run because its harness and the console's making
  changed: 34,603,665 calls checked over 54 movies and none diverged, and
  under lockstep 334,319 passes with 51 of 54 never parting, the same
  three. Both are the figures from before.
  Both noise tests print what the build before printed, byte for byte:
  13,000,000 steps over three seeds and 6,000 frames over two.
  Three renderers still come to one checksum in `zamn` on the five runs
  above, each the build before's. The states `zamn_headless` saves are the
  files it saved before, and the 36 loaded runs the same pictures.
  Against the build before: `zamn_test_layers` prints the same on seven
  runs, `zamn_test_radar` at two widths, `zamn_headless` writes the same
  picture from three movies and `zamn_record` the same bytes from four
  recordings.
  On Linux, in WSL with gcc 13: the tree builds, both noise tests pass,
  and `zamn` comes to the Windows checksums under all three renderers.
  `zamn_native` there comes to them on one movie and the forced game over,
  and its quick save is the file the Windows build makes.
  The first run of the corpus script did not start. I had written a
  PowerShell string with a variable and a colon after it, which is read as
  a drive.
- **Linux.** The tree builds in WSL with gcc 13, once `src/port/clears.c`
  includes `<stddef.h>`, which an earlier commit of mine left out. Both
  noise tests pass there. `level25-boss.zmv` at 16:9 is clean under the
  check and comes to the Windows build's checksum alone.
- **Noise.** 6,000 frames over two seeds: 1,260,224 lines' sprites found both
  ways and none differ, with 22,848 more left to the PPU. The picture, drawn
  from them both ways, does not differ either, and nor do 196,824 rows read
  back. (Before the sprites, 12,000 frames over eight seeds: 2,024,736 lines
  drawn both ways and none differ.)
- **The smoothing.** 324 screenshots and 4,832 of the smoothing's pictures,
  from twelve movies at two widths, are byte for byte what the build before
  drew.

**Cost**, `level1.zmv`, milliseconds a tick: emulating the tick, which has the
drawing in it, and taking it apart for the smoothing.

| Width | Before | After |
|---|---|---|
| 4:3 | 4.4 and 2.3 | 2.2 and 0.4 |
| 16:9 | 5.5 and 2.8 | 2.4 and 0.4 |
| 21:9 | 6.6 and 3.3 | 2.5 and 0.5 |

The whole process, in a window at 240 Hz and 16:9 with no sound, used 50% to
75% of one core before and 17% to 37% after, over three runs of each. Those
runs are noisy; the table is the steadier measure.

A profile of the same run afterwards has 39% as many busy samples. Of them
the drawing is 11%, taking the frame apart 12%, the 65816 with the ports and
the sound about 44%, and showing the pictures about 32%.

Finding the sprites here did not change the cost. A tick at 16:9 emulates in
2.4 to 2.5 ms before and after. It was done to need the emulator less.

Keeping the registers here did not change it either: 2.4 to 2.5 ms. Every write is done twice for now, and a write is cheap.

Nor did the frontend reading them: 2.34, 2.35 and 2.43 ms.

Nor did keeping the picture and the notes here. Measured back to back with
the build before, on a noisier afternoon: 2.50 to 2.96 ms against 2.56 to
2.87, and taking a tick apart 0.54 to 0.60 against 0.53 to 0.56.

Nor did keeping the memories, the rows and the picture's buffer here: 2.41
to 2.48 ms over three runs, against 2.55 to 2.56 for the build before, run
in turn. The PPU still decodes every write under every renderer. That is the
cost left to take out, and it goes when the console calls `src/video`
itself.

Nor did having the console call `src/video`, which I had expected to show:
2.37, 2.41 and 2.42 ms against 2.40, 2.36 and 2.35 for the build before,
run in turn. The PPU's decoding of about a thousand writes a frame was too
little to measure. What the step buys is that `native` no longer needs the
PPU to run.

Nor did giving the frontend the chip. Run in turn with the build before:
2.46, 2.47 and 2.46 ms against 2.44, 2.39 and 2.42. An earlier pass of the
same six runs had 2.60, 2.42 and 2.52 against 2.46, 2.74 and 2.39. If the
second pass's 0.04 ms is real it is under 2% of a tick; I cannot tell it
from the noise of the first.

Nor does having no PPU. Run in turn on a busier afternoon: the build
before 2.64, 2.60 and 2.84 ms, `zamn` 2.56, 2.69 and 2.52, and
`zamn_native` 2.57, 2.65 and 2.51. Under `native` the PPU was already told
nothing, so there was nothing of it left to stop paying for.

## What is still the emulator's

- **When things happen.** The emulated console says where the beam is, when
  a frame starts, when a line begins and when the picture ends. It says them
  to `src/video` directly now.
- **The PPU, in `zamn.exe`.** Under `native` it does nothing unless a line
  is declined, which no line of this game is. It is still what `check` and
  `emulated` run. `beside.c` and `with_ppu.c` are the only files of
  `src/video` that name it, and the frontend does not. `zamn_native` has
  none.
- **The rest of the console, in both.** `zamn_native` still links the
  emulated console for its clock and its bus, the 65816 for what is not
  ported, DMA, the cartridge, the pads and the whole of the sound.
- **The state's layout.** It is the PPU's, written with the emulator's state
  handler, so that states already saved still load. `state.c` is the only
  other file of `src/video` that includes anything of the emulator's.
- **The tools.** The game draws with `src/video`, and `zamn_headless` does
  when asked. The other three that share the frontend's code attach it and
  leave the drawing to the PPU. `zamn_cosim` and the rest run on the PPU
  alone, and draw nothing. `zamn_icon` and `zamn_logo`, which draw the
  launcher's icon and heading when the game is built, have the PPU draw
  the title screen.

## Next

- Draw a frame once. With the smoothing on, a frame shown as layers is still
  drawn as a picture that nobody sees.
- The tools off the PPU: `zamn_headless`, `zamn_record` and
  `zamn_test_layers` drawn by `src/video` and linked without it, the
  radar test's second drawing of a frame, and the launcher's icon and
  heading.
- The picture is kept eight bytes a column because the frontend takes it
  doubled. Four would do if the frontend took it as drawn.
- The smoothing's sprites and its maths window are still worked out a dot at
  a time.
