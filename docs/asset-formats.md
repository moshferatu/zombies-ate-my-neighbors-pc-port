# Asset formats

Phase 2 loads game data out of the user's ROM at runtime, so every format the
game uses has to be reimplemented in C. This documents the ones decoded so far
and how each one was checked.

The code lives in `src/assets/`. Unlike `src/analysis/`, it is **port code** —
it ships in the finished game — so it depends on nothing but libc. The tool that
exercises it, `zamn_assets`, is where the emulator and the ROM-specific
addresses live.

## LZSS compression — verified byte-exact

ZAMN compresses graphics with textbook Okumura LZSS: a 4 KB sliding window
(`N=4096`), maximum match length 18 (`F=18`), minimum match 3 (`THRESHOLD=2`).
The ROM routine is `$80:CD20`; ported to C in `src/assets/lzss.c`.

### Stream layout

| Bytes | Meaning |
| --- | --- |
| 0-1 | number of bytes that follow (little-endian) |
| 2.. | flag byte, then the 8 items its bits describe, repeating |

A flag byte is consumed low bit first. A **set** bit means one literal byte
follows. A **clear** bit means a two-byte match token follows:

```
byte0 = window position bits 0-7
byte1 = high nibble -> window position bits 8-11
        low nibble  -> match length - 3
```

Matched bytes are copied one at a time and written back into the window as they
go, so a match may overlap the bytes it is itself producing. There is no end
marker: the stream ends when the header's byte count runs out.

### The window

The window is a 4096-byte ring at WRAM `$7E:6F00`. Each call refills it with
`$20` (space) and sets the write position to `$0FEE`.

The refill covers `$0FEF` bytes, not `$1000` — the ROM's `MVN` at `$80:CD42`
stops at `$7E:7EEE`, leaving the 17 bytes above the initial write position
holding whatever the previous call left there. Okumura's original fills exactly
the same range, so this is faithful rather than a bug, and a well-formed stream
never reads ahead of the write position. `lzss_decompress()` reproduces it
anyway: the window is caller-owned state that persists across calls, and the
verifier compares its final contents too, not just the output.

### Register interface

```
push  <16-bit source address>     ; caller pushes before the JSL
A = source bank
X = destination bank
Y = destination address
JSL $80CD20
                                  ; returns Y = bytes written
```

### Verification

`zamn_assets verify-lzss` replays an input movie under the reference core,
intercepts **every** call the game makes to `$80:CD20`, and runs the C port on
the same input — comparing the output bytes, the byte count, *and* the final
window contents. This is the Phase 3 co-simulation pattern in miniature: same
core, same movie, per-call equality.

```
build\zamn_assets.exe verify-lzss "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
```

Current result — every call `movies/level1.zmv` reaches is byte-identical:

| # | source | destination | in | out |
| --- | --- | --- | --- | --- |
| 1 | `$83:830E` | `$7E:8000` | 649 | 2048 |
| 2 | `$83:8598` | `$7E:8000` | 3159 | 8192 |
| 3 | `$94:A300` | `$7E:8000` | 8587 | 11136 |
| 4 | `$92:CF80` | `$7E:8000` | 10033 | 15648 |
| 5 | `$9B:8000` | `$7E:8000` | 15541 | 32768 |

The tool exits non-zero if any call diverges or if the movie never reached one,
so it works as a regression test. Widening `movies/` widens this table for free.

## Planar tiles

Standard SNES encoding, not a ZAMN format — `src/assets/gfx.c` decodes it
because the port needs it, not because it needed reverse engineering. An 8x8
tile is `8 * bpp` bytes: bitplanes in pairs, 16 bytes per pair, two bytes per
row (low plane, high plane), pixel `x` in bit `7-x`.

That pairing is why a 4bpp tile handed to a 2bpp PPU mode still shows its low
two planes.

## Palettes

CGRAM entries are BGR555 little-endian words: `0BBBBBGG GGGRRRRR`. Each 5-bit
channel is expanded to 8 by replicating its top bits (`c << 3 | c >> 2`) so
`$1F` maps to 255 rather than 248.

## Checking a decode against the game

`analysis/dma_log.csv` names the ROM address, length and destination register of
every transfer the game performs, which is what makes a decode checkable: a
source that reached `$2118` is tiles, a source that reached `$2122` is a
palette, and the length says how much. Point the tool at one and the picture has
to come out right.

```
:: compressed tiles, coloured with a palette the game DMAs to CGRAM
build\zamn_assets.exe gfx "Zombies Ate My Neighbors.sfc" 94:A300 out.png ^
    --lzss --bpp 4 --pal 83:EE8C --pal-index 1 --scale 3

:: tiles DMA'd straight from ROM, uncompressed
build\zamn_assets.exe gfx "Zombies Ate My Neighbors.sfc" 97:AFC4 out.png ^
    --bytes 3616 --bpp 4 --scale 3

:: a palette as a swatch grid, 16 colours per row
build\zamn_assets.exe palette "Zombies Ate My Neighbors.sfc" 83:EE8C out.png -n 128
```

Both tile sources above render as legible text glyphs with correct outlines, and
`$83:EE8C` decodes to a coherent 16-colour ramp (`001F` red, `7FFF` white,
`0000` black). Unlike the LZSS check this is an eyeball test — the tile encoding
is fixed hardware behaviour with no ROM routine to diff against.

Note that `$83:EE8C` holds *two* sub-palettes: entry 0 is red plus 15 copies of
one grey, and the artwork palette starts 32 bytes in (`--pal-index 1`).

## Level layout — verified against the ROM's own expansion

A level is built from three ROM pieces plus a handful of scalars, all named by a
**54-byte record** (`src/assets/level.c` decodes it). The records sit in bank
`$9F`, and a **pointer table at `$9F:8000`** turns a level number into a record;
levels are numbered from 1, and entry 0 of the table is not a record. The
opening playable level (`movies/level1.zmv`) is **entry 2**, record `$9F:9060`.

### The record

| Offset | Field | Meaning |
| --- | --- | --- |
| `$00`/`$02` | `block_defs` | LZSS stream → the **block library** |
| `$04`/`$06` | `block_map` | `cols × rows` 16-bit **block indices** |
| `$08`/`$0A` | `tile_attrs` | 512 words, one per BG tile; bit 0 = solid |
| `$0C`/`$0E` | `bg_tiles` | 16 KB of 4bpp BG characters |
| `$10`/`$12` | `bg_palette` | 256-byte BGR555 background palette |
| `$14`/`$16` | `sprite_palette` | 256-byte sprite palette |
| `$22` / `$24` | `cols` / `rows` | level size **in blocks** |
| `$26` | `priority_below` | tiles below this index get BG priority forced on |
| `$2A`-`$30` | starts | the two players' spawn X/Y |
| `$32` / `$34` | `song` / `sample_set` | which APU data sets to load — see *Music* |

Each pointer is a `(16-bit address, 16-bit bank)` pair, which is how the loader
at `$80:86A2` reads them. Fields `$1C`/`$1E`/`$20` are bank-`$9F` addresses of
the three **placement lists** (actors, victims, objects) — decoded below.
Fields `$18`,`$1A`,`$28` are still unidentified.

### The three pieces

* **Block library** — up to 256 "blocks", each an 8×8 array of 16-bit BG
  tilemap entries (128 bytes). One LZSS stream, decompressed to `$7E:8000`
  (which is exactly 256 blocks' worth of room before bank `$7E` ends).
* **Block map** — `cols × rows` little-endian block indices, the level's shape.
* **Tile attributes** — copied verbatim to `$7E:611A`. Collision reads *this*,
  indexed by the tile from the expanded map, never the block map (`$80:AE43`
  tests bit 0 with `LSR A / BCS`).

### Expansion

The loader does not keep the block map. It **expands** it once into a full
tilemap in WRAM bank `$7F` — one 16-bit entry per 8×8 tile, row stride
`cols × 16` bytes — and everything afterwards (the camera row/column streamers
at `$80:A462`/`$80:A5E5`/`$80:A61D`) reads only that. `level_expand()`
reproduces it, faithful to the ROM's 16-bit block-address arithmetic at
`$80:AD0B` (`base + (index << 7)` truncated to 16 bits, so an index ≥ 256 is a
malformed level rather than an out-of-library read).

The scalars the engine scrolls by are derived, not stored: row stride (`$B2`),
max scroll X (`$B8 = cols·64 − 256`) and Y (`$B6 = rows·64 − 240`). `level.h`
exposes them as inline helpers.

### Verification

`zamn_assets verify-level` is the LZSS pattern one level up: it lets the game
load a level under the reference core, then diffs **the entire expanded tilemap**
— plus the block library, the two row-base tables, the derived scalars, the tile
attributes and both palettes — against the WRAM the ROM just built.

```
build\zamn_assets.exe verify-level "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
```

Result on `level1.zmv`: the game loads record `$9F:9060` (table entry 2), and all
15 checks pass, including the full **18,304-tile** expanded map byte-for-byte.
The tool exits non-zero if any check fails, if the loaded record is not in the
table, or if the movie never reached a level load, so it doubles as a regression
test — and, like `verify-lzss`, widening `movies/` widens what it covers.

`zamn_assets level <n> [out.png]` reports a record and renders its map with the
level's own characters and palette. All **56** levels decode without error, and
maps from every tileset group render as coherent art (checked by eye — the
expansion itself is what `verify-level` proves exactly).

## Actor, victim and object placement — verified against the ROM's parsers

Three of the level record's bank-`$9F` pointers name the level's **placement
lists** — where its enemies, victims and objects are put. They sit in bank `$9F`
right after the record, and the loader at `$80:86A2` hands each one to a
dedicated routine. `src/assets/actor.c` decodes all three; the record layouts
were read straight out of those three parsers.

| Record field | Points at | Parser | Meaning |
| --- | --- | --- | --- |
| `+$1C` (`list_1c`) | actor list | `$81:80EC` | enemies and other actors |
| `+$1E` (`list_1e`) | victim list | `$82:DB46` | the people you rescue |
| `+$20` (`list_20`) | object list | `$80:C9A5` | doors, warps, item spawns |

### Actor records — 10 bytes

`$81:80EC` walks the list at stride 10, reading `+0` as an id (`AND #$00FF /
BEQ` ends the list), and `+1`/`+3` as the spawn position.

| Offset | Field | Notes |
| --- | --- | --- |
| `+0` | `id` (u8) | actor type; **`0` terminates the list** |
| `+1` | `x` (u16) | spawn X in level pixels |
| `+3` | `y` (u16) | spawn Y |
| `+5` | `flags` (u8) | per-placement flags, meaning not yet established |
| `+6`/`+8` | `behavior` | 24-bit far pointer to the actor's routine; `+9` pad |

The behavior pointer varies by level and by actor (e.g. `$82:990F` throughout
level 1, a mix of `$81:87F8`/`$81:88CA` in level 3), so it is the per-actor
class, and `id` is a sub-type the class reads. A few levels list **no** actors
(the list is just the `00 00` terminator).

### Victim records — 12 bytes

`$82:DB46` walks the list at stride 12, keying it on the index at `+6`: it stops
at the first record whose index is `0` **or greater than `$1D50`**, and `$1D50`
is a constant `$0010` set at every level load (`$80:85E7`). So a victim record
is live while `0 < index <= 16`; the padding past the last victim reads as an
index above the gate. Every one of the 56 levels lists exactly **10** victims.

| Offset | Field | Notes |
| --- | --- | --- |
| `+0` | `x` (u16) | spawn position |
| `+2` | `y` (u16) | |
| `+4` | — (u16) | always `$0000` in the shipped data |
| `+6` | `index` (u16) | 1..N; `0` or `> 16` ends the list |
| `+8`/`+10` | `behavior` | 24-bit far pointer (bank `$83`); `+11` pad |

The parser only copies `+0`/`+2` into working arrays (`$7E:6DF4` X, `$7E:6DF6`
Y, stride 4) and counts them at `$7E:6E30`.

### Object records — 5 bytes

`$80:C9A5` walks the list reading `+0` (u16, `BEQ` ends the list), `+2` (u16)
and `+4` (u8), into `$7E:6D02` (X), `$7E:6D48` (Y) and `$7E:1F0A` (type), all at
stride 2, and writes a `$C000` sentinel into `$7E:1EC4` one past the last entry.

| Offset | Field | Notes |
| --- | --- | --- |
| `+0` | `x` (u16) | **`0` terminates the list** |
| `+2` | `y` (u16) | |
| `+4` | `type` (u8) | object type |

### Verification

`zamn_assets verify-actors` is the same pattern as `verify-level`: it replays a
movie under the reference core and, the instant each parser finishes, diffs the
flat array the ROM built against the one `actors_read()` decoded from the record.

```
build\zamn_assets.exe verify-actors "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
```

On `level1.zmv` (record `$9F:9060`) all four checks pass: the victim count
(`$7E:6E30`) and all 10 victim positions match `$82:DB46`'s arrays, and the
object count (proven by the `$C000` sentinel landing exactly at our count) and
all 9 object positions **and** types match `$80:C9A5`'s. The tool exits non-zero
if any diff fails or a list is never reached, so it doubles as a regression
test. The **actor** list (`$81:80EC`) spreads its work across frames as the
camera scrolls rather than building a flat array at load, so its runtime check
belongs with the Phase 3 spawner; `actors_read()` decodes it here (backed by the
`$81:80EC` disassembly) and all 56 levels' lists parse without error.

`zamn_assets actors <n>` reports a level's three lists.

## Sprite graphics — verified against the ROM's own OAM emitters

Everything the game draws as a sprite — Zeke, the monsters, the victims, item
pickups, even the "PASSWORD" lettering on the title screen — is built from one
unit: a **16x16 frame** of 4bpp graphics, 128 bytes, and a **metasprite** that
places one or more frames at signed offsets.

Ported in `src/assets/sprite.c`, checked by `zamn_assets verify-sprites`.

### Frames — a flat array at `$84:8000`

Frame `n` is the 128 bytes at `$84:8000 + n * 128`. The base is set once at boot
by `$80:C05A`, which both `$80:85D3` and `$80:8632` call with A=`$8000`,
Y=`$0084`; the same routine clears a `$2000`-byte frame→slot map, so the frame
number is **12 bits — 4096 frames**, half a megabyte, most of the second half of
the ROM.

`$80:BA29` builds the address the LoROM-friendly way, which is why 128 bytes is
the unit: 256 frames are exactly `$8000` bytes, so `n >> 8` goes in the bank and
`(n & $FF) * 128` in the offset, and no frame ever straddles a bank.

The 128 bytes are four ordinary 4bpp tiles in reading order — top-left,
top-right, bottom-left, bottom-right. `$80:B960` uploads them as two 64-byte
DMAs, the second to the VRAM row below (`ORA #$0100` on the address).

```
build\zamn_assets.exe frame "Zombies Ate My Neighbors.sfc" 0x463 zeke.png --count 24
```

### Metasprites — banks `$8F` and `$90`

A metasprite is a count byte followed by that many 8-byte pieces. The drawing
pass rejects any pointer outside banks `$8F`/`$90` (`$80:BD97`) or below `$8000`
(`$80:BD8E`), and treats a count of 0 as "draw nothing" (`$80:BDAA`). Records
are packed back to back with no index table — actors carry the pointer.

| Offset | Field | Notes |
| --- | --- | --- |
| `+0` | `x` (i16) | signed offset from the actor's position |
| `+2` | `y` (i16) | |
| `+4` | `attr` (u16) | OAM attribute word: `$8000` vflip, `$4000` hflip, `$3000` priority, `$0E00` palette, `$0100` tile bit 8 |
| `+6` | `frame` (u16) | which 16x16 frame to draw |

```
build\zamn_assets.exe sprite "Zombies Ate My Neighbors.sfc" 90:9172 zeke.png
```

### How a frame reaches VRAM

The sprite character area holds only 512 tiles — 128 frames — so the game keeps
an **LRU cache** of them and uploads on demand. `$80:B9D6` maps a frame number
to the OAM tile number it is currently loaded at, evicting the least recently
used slot (`$7E:175E` holds each slot's last-used tick) and queueing a DMA that
`$80:B947` drains in vblank. Three tables drive it, and all three are exact
functions of the slot number — the port computes them and `verify-sprites`
proves the formulas against the ROM's 128 entries:

| Table | Meaning | Formula |
| --- | --- | --- |
| `$80:B547` | slot → VRAM word address | `(slot / 8) * $200 + (slot % 8) * $20` |
| `$80:B647` | slot → OAM tile number | `(slot / 8) * 32 + (slot % 8) * 2` |
| `$80:B747` | sprite → OAM high-table address and x-bit mask | `$200 + (n / 4)`, `1 << (n % 4 * 2)` |

The cache is runtime state, not a ROM format, so `sprite_emit()` takes a
callback for the frame→tile lookup and the cache itself is left for Phase 3.

### Composition — the four emitters

`$80:BD1F` walks the visible-actor list and, for each actor, dispatches through
`($80:BDEA,X)` on flag bits 1-2 to one of four near-identical routines:

| `flags & 6` | Routine | Effect |
| --- | --- | --- |
| 0 | `$80:BA51` | as authored |
| 2 | `$80:BABA` | mirror `x` (`-x-16`), `EOR #$4000` |
| 4 | `$80:BB30` | mirror `y` (`-y-16`), `EOR #$8000` |
| 6 | `$80:BBA6` | both, `EOR #$C000` |

`sprite_emit()` is one function covering all four. Each piece becomes one OAM
entry in `$7E:13BE`:

* screen position is `piece + actor`, where the actor's position already has the
  camera subtracted (`$8E`/`$90`);
* the actor supplies an AND mask (`$96`) and an OR value (`$92`) applied to the
  piece's attribute word — that is how an actor overrides the palette and
  priority its frames were authored with (`$F1FF` masks the palette field out);
* a piece whose screen `y` lands in `$00E0..$FFF0`, or whose `x` lands in
  `$0100..$FFF0`, is dropped and does not consume an OAM slot;
* `x` in `$FFF1..$FFFF` sets the sprite's bit-8 flag in the OAM high table.

### Verification

`zamn_assets verify-sprites` intercepts all four emitters. At each entry it
snapshots the ROM's 544-byte OAM buffer and the emitter's arguments; at the
matching `RTS` it runs `sprite_emit()` on that snapshot and diffs **the whole
buffer** plus the resulting OAM index against what the ROM produced.

```
build\zamn_assets.exe verify-sprites "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
```

On `level1.zmv` all **4591** emissions are byte-identical (3572 unflipped, 1019
flipped horizontally), across 40 distinct metasprites in `$8F:DF86..$90:CBA3`,
and the three slot tables match for all 128 entries. Confirmed non-vacuous
twice: changing the mirror constant from `-16` to `-15` broke exactly the 1019
flipped calls, and perturbing one palette bit broke the unflipped ones.

**Not yet exercised:** this movie never flips vertically, so `$80:BB30` and
`$80:BBA6` are ported from their ROM bytes (they are byte-for-byte the same
routine as the other two with the negation moved) but have not been diffed
against execution. A movie that reaches an actor using them would close that.

An independent cross-check: the pieces of `$8F:E889` name frames `$A17`-`$A1C`,
whose addresses `$8E:8B80`-`$8E:8E00` are exactly the sprite uploads
`analysis/dma_log.csv` records at frame 987 — and it draws the word "PASSWORD".

## Music and sound — verified against the bytes on the APU bus

ZAMN keeps its entire audio program on the SPC700. The 65816 never touches a
note: it uploads a driver, uploads data for it to play, and sends one-byte
commands. Since the port keeps the SPC700 and DSP emulated (PLAN.md, Phase 4),
that CPU-side traffic **is** the format the port has to reproduce — get the same
bytes onto the same ports in the same order and the original audio comes out by
construction.

Ported in `src/assets/music.c`, checked by `zamn_assets verify-music`.

### The data-set table — `$80:CCDE`

Sixteen 4-byte entries, each a 24-bit address plus a zero pad;`$80:CC7C` indexes
it with `index * 4` and loads the bank and the address as two separate words.

| Index | Contents |
| --- | --- |
| 0 | the sound driver and its samples |
| 1 | the sound-effect bank — 50 blocks, loaded once at boot by `$80:8611` |
| 2-11 | songs |
| 12-15 | sample sets |

The 15 ROM-resident sets are packed end to end and never overlap; several start
exactly where the previous one's terminator ends, which is what makes the block
walk checkable without running anything. Set 2 ends at `$97:AFC4` — the
uncompressed tile source used as an example further up — so that address is the
boundary between the audio data and the graphics that follow it.

### Entry 0 — the driver, uploaded by the IPL boot ROM

The driver image is not contiguous in ROM. `$80:CB1A` gathers it with two `MVN`
block moves — `$91:8000` (`$8000` bytes) then `$95:BA3D` (`$1A86` bytes) — into
WRAM at `$7F:0000`, which is why entry 0 of the table is a WRAM address and not
a cartridge one. `$80:CB61` then uploads it with the stock SNES boot-ROM
protocol (`$BBAA` handshake on `$2140`, payload byte in `$2141`, destination in
`$2142`/`$2143`).

Its layout is the boot ROM's own: `[len16][dest16][len bytes]` repeated, ending
at a zero length whose second word is the entry point instead of a destination.
The shipped image is two blocks and accounts for every one of its 39,558 bytes:

| Block | Bytes | SPC destination |
| --- | --- | --- |
| 0 | 2,999 | `$0600` — the driver |
| 1 | 36,547 | `$1340` — samples |
| — | — | execute at `$0600` |

### Entries 1-15 — uploaded through the running driver

Once the driver is live the boot ROM is gone, so the rest goes over the driver's
own command port, one byte per command. These streams have **no** destination
words — the driver decides where bytes land — so the format is just
`[len16][len bytes]` repeated until a zero length.

Only the sound-effect bank has more than one block; every song and sample set is
a single block.

### The command protocol — `$80:CCC8`

```
$2142 = command      (X)
$2141 = parameter    (A)
$2143 = sequence counter, incremented per command
```

The CPU spins until the SPC echoes the previous counter back on `$2143` before
writing the next command, so the stream is strictly ordered and lossless.

| Command | Sent by | Meaning |
| --- | --- | --- |
| `$01` | `$80:CC3B` | play sound effect (parameter = id) |
| `$02` | `$80:CC27` | play sound effect, second channel |
| `$06` | `$80:CCA9` | one payload byte |
| `$08` | `$80:CC6F` | the data set about to arrive |
| `$0A` | `$80:CCA1` | start a block |
| `$0D` | `$80:CBFD` | the sound-effect bank is loaded |
| `$14` | `$80:CBF3` | play the song just uploaded |

One wart the port has to reproduce: the parameter `$80:CCA1` sends with `$0A` is
the block length's two bytes **ORed together**, because the ROM reuses the
register it tested the length for zero with. It cannot be a usable size and the
driver almost certainly ignores it, but it goes on the bus, so `music.c` emits
it (`music_block_param()`).

### Which set a level plays

`$82:AC56` reads `$9F0032,X` and `$9F0034,X` — the level record's `+$32` and
`+$34`, previously unidentified — and passes them to `$80:CBD9`, which uploads
the song, then the sample set (adding 12 to the index at `$80:CBEB`), then sends
`$14`. Across all 56 levels `+$32` is always in `2..11` and `+$34` in `0..3`,
exactly the ranges the table's layout implies.

### Verification

`zamn_assets verify-music` observes the stores that reach the APU ports — at the
instructions that perform them, so what is compared is what the hardware sees —
and reassembles the whole conversation:

```
build\zamn_assets.exe verify-music "Zombies Ate My Neighbors.sfc" -m movies\level1.zmv -f 2400
```

On `level1.zmv` all 8 checks pass. The staged 39,558-byte driver image in WRAM
is byte-identical to the one `music_driver_image()` builds from ROM; the block
list matches the destinations written to `$2142`; all 39,546 payload bytes
handed to the boot ROM match; and all 5 data-set uploads the movie performs
(sets 1, 3, 6 and 15 twice) are byte-identical command streams — 23,820
commands.

Two independent cross-checks:

* the payload bytes those 5 uploads carry sum to **23,766**, which is exactly the
  number of `$80:CCAB → $80:CCC8` calls `analysis/callgraph.csv` recorded for the
  same movie;
* the commands sent *outside* an upload tell the expected story on their own —
  `$08 $01` / upload / `$0D` at boot (sound effects), then `$08 $03` / song 3 /
  set 15 / `$14` for the title screen, then `$08 $06` / song 6 / set 15 / `$14`
  when level 1 loads, which is what record `$9F:9060`'s `+$32`/`+$34` say.

Confirmed non-vacuous twice: simplifying `music_block_param()` to the low byte
broke exactly the four sets with blocks longer than 255 bytes and left the
sound-effect bank (whose blocks are all shorter) passing; moving the driver's
second staging source by one byte broke all three driver checks.

**Not yet exercised:** the movie only reaches sets 1, 3, 6 and 15. The other
songs and sample sets decode statically and are proven not to overlap, but no
movie has driven their upload yet.

### Listening to a level's music

There is no static decode for this. A level PNG can be built from ROM bytes
alone, but a song cannot: the bytes reach the SPC700 one at a time through the
command port and the **driver** decides where they land, so there is no address
to point a decoder at. The only way to a playable artifact is to let the ROM
perform the upload and then capture the APU.

`zamn_assets spc` does that. It boots the game under the reference core and,
at the game's own `$80:CBD9` call, swaps in the song and sample set you asked
for — which is what makes all 56 levels reachable without playing to them —
then dumps SPC RAM, the DSP registers and the SPC700's register file as a
standard `.spc` file, and/or renders audio to a `.wav`.

```
:: level 2's music as a .spc, for an SPC player
build\zamn_assets.exe spc "Zombies Ate My Neighbors.sfc" 2 level2.spc

:: ...and/or as plain audio, no special player needed
build\zamn_assets.exe spc "Zombies Ate My Neighbors.sfc" 2 level2.spc --wav level2.wav --seconds 60

:: any song, including the two no level uses (1 is sound effects, 10 unused)
build\zamn_assets.exe spc "Zombies Ate My Neighbors.sfc" 1 song10.spc --song 10
```

The dump is taken 12 frames after the upload finishes (`--settle`), so the song
is at its start. Two caveats worth knowing:

* the `.spc` is the clean artifact. The `.wav` is rendered by letting the game
  keep running, so a stray title-screen sound effect can land in the recording;
* the driver is *running* when captured, so its RAM is not byte-identical to the
  ROM image — measured against a dump, block `$0600` differs in **4 bytes** of
  2,999 and block `$1340` in **9** of 36,547, all of them live variables and
  sample-directory entries. That the other 99.9% matches is itself good evidence
  the upload landed correctly.

Checked on dumps of levels 1, 2, 8, 9 and 52: every file is 66,048 bytes with a
valid header, the SPC700's PC sits inside the driver, the DSP is unmuted with
all 8 channels carrying volume, and song data lands above the sample block at
`$AC5F`-`$D92F` and `$E700`-`$FEFE`. Levels 1 and 52 — which share record
`$9F:83DA` — produce **byte-identical** SPC RAM, while levels with different
songs differ by 20-27%. Rendered audio is continuous and unclipped for the full
length, and no two songs share more than 0.2% of their samples.

## `zamn_assets`

```
zamn_assets verify-lzss   <rom.sfc> [-m movie] [-f frames]
zamn_assets verify-level  <rom.sfc> [-m movie] [-f frames]
zamn_assets verify-actors <rom.sfc> [-m movie] [-f frames]
zamn_assets verify-sprites <rom.sfc> [-m movie] [-f frames]
zamn_assets verify-music  <rom.sfc> [-m movie] [-f frames]
zamn_assets music         <rom.sfc>
zamn_assets spc           <rom.sfc> <level 1-56> [out.spc] [--wav f] [options]
zamn_assets level         <rom.sfc> <level 1-56> [out.png]
zamn_assets actors        <rom.sfc> <level 1-56>
zamn_assets sprite        <rom.sfc> <bank:addr> [out.png] [options]
zamn_assets frame         <rom.sfc> <frame 0-4095> [out.png] [options]
zamn_assets decompress    <rom.sfc> <bank:addr> <out.bin>
zamn_assets gfx           <rom.sfc> <bank:addr> <out.png> [options]
zamn_assets palette       <rom.sfc> <bank:addr> <out.png> [-n colors]
```

Addresses are SNES addresses in the `$80-$BF` FastROM mirror the game itself
uses, written `94:A300` or `$94:A300`. `gfx` with no options prints its own
option list.

## Not yet decoded

Every ROM format Phase 2 set out to decode is done. What remains is the *runtime*
half of the actor system. The placement lists say where each actor/victim/object
starts and what code drives it, and the sprite formats above say how any given
metasprite draws — but the wiring between them is code, not data:

* the actor **slot** tables that hold a spawned actor (the `$100`-stride array
  at `$7E:0300`+ and the 20-byte-stride table at `$7E:1872`+, flagged in
  `docs/wram-map.md`), filled by the camera-driven spawner `$81:80EC`;
* the **animation** state that picks which metasprite an actor points at from
  one frame to the next — the pointer lives in the actor slot (`+8`/`+$0A`),
  written by the behavior routines in banks `$81`-`$83`;
* the **VRAM frame cache** (`$80:B9D6`), whose slot assignment depends on the
  whole history of what has been on screen.

All three are Phase 3. Note that `verify-sprites` already proves the format side
independently of them: it takes the pointer and the flags out of the running
game and only checks the decode.
