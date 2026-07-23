# WRAM map

Derived from `analysis/wram_regions.txt` and `analysis/wram_map.csv`, produced by
`zamn_trace` over a 2400-frame run that boots, clears the title screen and plays
the opening of level 1 (`movies/level1.zmv`).

**Read this as a starting point, not a finished map.** Entries below are backed
by traced accesses and, where a name is given, by reading the accessing code.
Regions marked *unidentified* are real (something reads or writes them) but their
meaning is not yet established.

## How to read the raw reports

* `wram_regions.txt` collapses the per-byte data into contiguous live regions —
  the fastest way to see the layout.
* `wram_map.csv` has one row per address: read/write counts, the frame each was
  first touched, whether it is accessed as a 16-bit word, and up to four distinct
  accessor PCs on each side.
* The boot-time `MVN` that zeroes all 128 KB is counted separately in the `bulk`
  column and excluded from liveness, or every byte would look used.
* Direct page is `$0000` for the entire game, so a `$xx` direct-page operand in
  any listing is exactly `$7E:00xx`.

92,850 of 131,072 WRAM bytes were touched by this run.

## Direct page — `$7E:0000-$7E:00DF`

The game's hot globals. 4.5 M reads / 0.95 M writes in 2400 frames.

| Address | Width | Name | Evidence |
| --- | --- | --- | --- |
| `$0004` | word | `nmi_saved_sp` | `TSC / STA $04` on NMI entry, restored at exit |
| `$0008` | word | `sched_cur_task` | index into `thread_wait` / `thread_sp` |
| `$000C` | word | `vbl_queue_a_count` | incremented by `$80:83AE`, decremented by `$80:83D5` |
| `$000E` | word | `vbl_queue_b_count` | same pattern at `$80:8418` |
| `$0010` | word | `vbl_queue_remaining` | dispatch-loop countdown |
| `$0012` | word | `vbl_queue_index` | slot index across the `RTL` into a job |
| `$0014` | word | `nmi_flags` | bit 15 is the NMI re-entrancy guard |
| `$0016` | word | `nmi_frame_counter` | `INC $16` once per NMI |
| `$0020` | dword | `sched_tick` | `INC $20 / BNE / INC $22` per scheduler pass |
| `$0026` | word | `render_flags` | bit 6 gates `vram_queue_flush` |
| `$0028` | word | LZSS source pointer | `[$28]` long-indirect fetch in the decompressor |
| `$002A`,`$002C`,`$002E` | word | LZSS state | destination / length / bank |
| `$0038`,`$003A`,`$003C`,`$003E` | word | LZSS working set | flag byte, ring index, match state |
| `$006E` | word | `joy1_raw` | `$4218` |
| `$0070` | word | `joy2_raw` | `$421A` |
| `$0072` | word | `joy1_dir` | joypad nibble via the table at `$80:81F9` |
| `$0074` | word | `joy2_dir` | |
| `$00CE` | word | `vram_queue_count` | loop bound in `vram_queue_flush` |
| `$0086`-`$009B` | word | *unidentified* | very hot; written from `$80:BAxx`/`$80:BDxx` (sprite build) |

The busiest addresses in the whole run are `$002C/$002D` (161 k reads) and
`$0038/$0039` (102 k reads) — both LZSS decompressor state.

## Scheduler and vblank tables

| Range | Size | Contents |
| --- | --- | --- |
| `$7E:1120-$7E:114F` | 48 B | active thread stacks (the NMI prologue pushes here) |
| `$7E:1180-$7E:11AF` | 24×2 | `thread_wait` — bit 15 live, low bits ticks remaining |
| `$7E:11B0-$7E:11DF` | 24×2 | `thread_sp` — parked stack pointer per thread |
| `$7E:125F` | — | scheduler's own stack top |
| `$7E:129F` | — | NMI stack top |
| `$7E:12A0-$7E:12DF` | 16×4 | `vbl_queue_a` — jobs run during forced blank |
| `$7E:12E0-$7E:12FF` | 8×4 | `vbl_queue_b` — jobs run after blanking ends |
| `$7E:1300`,`$7E:1330` | 24×2 | per-thread words written by `$80:8475`; *unidentified* |

## PPU shadow state

| Range | Size | Contents |
| --- | --- | --- |
| `$7E:1360-$7E:136B` | 12 B | BG1-BG4 H/V scroll, written twice per register into `$210D-$2112` |
| `$7E:136C` | 1 B | `brightness_shadow`, restored into INIDISP at the end of NMI |
| `$7E:13BE-$7E:15DD` | 544 B | `oam_buffer` — DMA'd to `$2104` every frame |
| `$7E:175E-$7E:185F` | 258 B | sprite build scratch; written by `$80:B9CE`/`$80:BA09` |
| `$7E:5628-$7E:56A7` | 128 B | `cgram_buffer` — 64 colours DMA'd to `$2122` |

## Graphics transfer queue

Five parallel arrays, `vram_queue_count/2` entries live:

| Address | Field |
| --- | --- |
| `$7E:1B84` | source address |
| `$7E:1BB4` | source bank |
| `$7E:1BE4` | VRAM destination word address |
| `$7E:1C14` | VMAIN increment mode |
| `$7E:1C44` | transfer length |

## Large buffers

| Range | Size | Contents |
| --- | --- | --- |
| `$7E:2122-$7E:4241` | 8480 B | *unidentified*; bulk-written by `$80:C06B` |
| `$7E:4B28-$7E:5327` | 2048 B | tilemap staging — DMA'd to VRAM via `$80:9EB2` |
| `$7E:5F36-$7E:6935` | 2560 B | *unidentified*; written by `$80:ADAC`, `$80:9A74`, `$82:AE00` |
| `$7E:6F00-$7E:7EFF` | 4096 B | `lzss_ring` — the decompressor's sliding window |
| `$7E:8000-$7F:9A85` | 72 KB | decompression output / level data |

## Repeating structures

The region report shows several strided arrays that are only partly touched, so
they appear as many small regions. Two are worth recording now:

* **Stride `$100`, at `$7E:0300`-`$7E:0C00`** — ten ~36-byte structures, all
  written by the same code at `$80:82B7`-`$80:82CC`. An array of ten objects with
  a 256-byte pitch.
* **Stride `$14` (20 bytes), at `$7E:1872`-`$7E:1A17`** — 22 slots, of which only
  a 2-byte field is read (2012 reads each, identical counts). A 22-entry table of
  20-byte records, read once per frame.

Both smell like actor/object slot tables, which is what Phase 3 needs next. Ports
of the actor logic should start by confirming these.

## What is still missing

This run only covers boot → title → the first seconds of level 1. Absent from the
map so far: the password system, level transitions, the pause/inventory screens,
bosses, and the two-player path. Extending `movies/` with longer scripted play is
the cheapest way to fill those in — the reports regenerate automatically.
