# The picture, drawn natively

The plan this project started with kept the console's video chip emulated for
good. That has changed. The picture and the sound are to be ported as the
game's code is, until nothing of LakeSnes is left in the game.

This is where the picture stands. `src/video` finds every line's sprites and
draws every scanline the game shows, and the emulated PPU does neither. It
keeps the chip's registers too: a line is drawn from them, the game reads
what they answer, and the frontend reads them. The PPU still holds VRAM, the
palette and OAM. It is still written to as well, because it reads its own
copy for what it notes of each frame and for what the frontend still asks it.

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
emulator's. `src/video/ppu_hook.c` is the one file that knows both. Before
each line it fills a `VideoState`: the registers' share from
`VideoRegisters`, and the frontend's share from the PPU, which is the widened
picture and what it has said of each sprite.

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

The memories themselves are not `VideoRegisters`'s. It is told where they
are, and for now they are the PPU's own arrays. The frontend writes sprites
and map words straight into those, and both sets of registers must see them.

The PPU calls `Ppu.wrote`, `Ppu.didRead` and `Ppu.happened` after it has done
each thing itself. So every write is done twice, to the same memory, which
comes to the same.

One register is written by the frontend and not the game. In a widened
picture `src/widescreen.h` moves the big figure's plane, which the game parks
for a picture 256 wide. It wrote the scroll into the PPU's struct. It now
calls `ppu_setScroll`, which tells the registers here.

The frontend reads these registers and not the PPU's: the smoothing, the
widescreen's policies, the radar and the blood. `video_registers_of` hands
them over, under any of the three renderers. The memories are read through
them as well.

Two things the frontend asked the PPU are asked here now:

- **`video_column_empty`**: whether a background draws nothing down the
  column of tiles under one column of the picture.
- **`video_column_filled`**: whether every tile down that column has
  something in it.

A sprite's size is `video_obj_size`, which `video_sprites` already used.

Four tools share the frontend's code: `zamn_headless`, `zamn_record`,
`zamn_test_layers` and `zamn_test_radar`. They keep registers for it as the
game does. The PPU still draws their pictures.

The sprites come in two steps. The state has OAM and the registers that say
how to read it, and `video_sprites` finds a line's sprites from them into a
row: a palette index and a priority for each column. The line is then drawn
with that row.

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

Half-height sprites, which the game never asks for, are left to the PPU.

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
| stretch | the columns the world has anything in |
| anchor | the left half at the left edge, the right half at the right |
| tile | the console's 256, repeated |
| centre clip | the 256 at the picture's middle |
| centre | the same, and each margin: nothing, the 256 again, or one pixel carried out |
| sweep | the layer shifted, and its first column carried out to the left |

`ppu_wideClampEdge` is not drawn. Nothing sets it.

**The centred layer's margins are searched for once.** The game over's mask
is the one user. The PPU keeps what it found, by line and scroll, and
`src/video` keeps the same thing in the same place (`VideoCentre`, copied out
of the PPU and back), because the PPU's is not made again when VRAM changes
and two copies could come apart.

## What it does not draw

`video_declines` names the first thing about a line that `video_line` does
not draw as the PPU does, and such a line is left to the PPU:

- a mode other than 1;
- mosaic, pseudo hi-res or overscan;
- a window on a layer;
- the main screen clipped by the colour window;
- colour maths halved;
- a layer's edge column carried into the margins, or sprites placed other than
  with the world or the console.

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

A layer with a window on it, or in a mode other than 1, is still read a dot at
a time.

## How it is checked

- **`zamn --renderer check`** finds every line's sprites both ways, draws
  every line both ways, and compares each column by column, with the two
  flags. It reports the lines that differ and the first of them, and exits 1
  if there were any.
  It also compares the registers with the PPU's after every write, every
  read and every event, each register by name, and a read's two answers.
- **`tools/verify_corpus.ps1 -Picture`** runs every movie of the corpus under
  the check, then again with `src/video` drawing alone, and compares a
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

## What is still the emulator's

- **The memory.** VRAM, the palette and OAM are the PPU's arrays.
- **A second set of registers.** The PPU still decodes every write, and
  reads that copy itself: for what it notes of a frame, below, and for the
  four things the frontend still asks it. Those are where a sprite goes in a
  widened picture, where colour maths is allowed, one dot of a layer, and
  how far a swept layer is shifted. Each needs what the frontend has said of
  the picture, which the PPU holds. What the tools print for a person reads
  it too. A saved state is the PPU's, and the registers here are taken from
  it when one is loaded.
- **When things happen.** The emulated console says where the beam is, when
  a frame starts and when the picture ends.
- **The sprites' two rows.** They are found here but kept in the PPU.
- **The tools.** Only the game draws with `src/video`. The four tools that
  share the frontend's code keep registers and leave the drawing to the PPU.
  `zamn_cosim` and the rest still run the PPU's own sprite finder, and draw
  nothing.
- **What `VIDEO_WIDE_AUTO` is worked out from.** Whether a layer is empty at
  its edges, scrolled or drawn a line at a time is the PPU's to notice.
- **The picture's buffer.** A line is written into the PPU's pixel buffer,
  eight bytes a column, and the frontend reads it from there.

## Next

- Draw a frame once. With the smoothing on, a frame shown as layers is still
  drawn as a picture that nobody sees.
- Move what the frontend says of the picture, and what is noted of a frame,
  out of the PPU: the margins and each layer's policy, each sprite's place,
  each line's scrolls and windows. The four things the frontend still asks
  the PPU can then be asked here, and the PPU's registers have no reader.
  The memories and the saved state can move after that.
- The smoothing's sprites and its maths window are still worked out a dot at
  a time.
