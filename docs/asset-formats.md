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

## `zamn_assets`

```
zamn_assets verify-lzss <rom.sfc> [-m movie] [-f frames]
zamn_assets decompress  <rom.sfc> <bank:addr> <out.bin>
zamn_assets gfx         <rom.sfc> <bank:addr> <out.png> [options]
zamn_assets palette     <rom.sfc> <bank:addr> <out.png> [-n colors]
```

Addresses are SNES addresses in the `$80-$BF` FastROM mirror the game itself
uses, written `94:A300` or `$94:A300`. `gfx` with no options prints its own
option list.

## Not yet decoded

Level layout, sprite/actor definitions, tilemaps and music. `dma_log.csv` gives
the tilemap sources (the `$2118` transfers with a mode-0 VMAIN) but the level
format that produces them is still unknown; the actor slot tables flagged in
`docs/wram-map.md` are the way in.
