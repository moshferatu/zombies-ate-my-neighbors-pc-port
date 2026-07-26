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
* Direct page is `$0000` for everything the scheduler itself runs, so a `$xx`
  operand in most listings is exactly `$7E:00xx` — but **not inside a thread**,
  which runs on its own 128-byte page. See *Per-thread direct pages* below, and
  read every listing with that caveat in mind: `zamn_disasm` annotates
  direct-page operands as though `D` were `$0000`, which is wrong for any code
  the scheduler dispatches.

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
| `$001E` | **byte** | `apu_seq` | `$80:CCC8  LDY $1E : CPY $2143 : BNE` — the APU command counter, and the only WRAM the audio path has |
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
| `$007C` | word | `sprite_upload_count` | bytes (entries×2) queued for `$80:B947` |
| `$007E`,`$0080` | word | `sprite_frame_base` | address / bank of the frame array; set to `$84:8000` by `$80:C05A` |
| `$0086` | word | `sprite_pieces_left` | metasprite pieces still to emit |
| `$0088` | word | `oam_index` | byte offset of the next free `oam_buffer` entry |
| `$008A`,`$008C` | word | `sprite_meta_ptr` | 24-bit pointer into the metasprite's piece array |
| `$008E`,`$0090` | word | `sprite_origin_x/y` | actor position with the camera already subtracted |
| `$0092` | word | `sprite_attr_or` | OAM attribute bits the actor forces on |
| `$0096` | word | `sprite_attr_and` | mask applied to each piece's attribute word |
| `$009E` | word | `sprite_lru_slot` | ×2; the cache slot `$80:B9D6` evicts next |
| `$00A0` | word | `sprite_tick` | `sched_tick` snapshot, stamped into `sprite_slot_tick` |

`$0086`-`$00A0` are among the busiest words in WRAM; all of them are scratch for
the sprite build described in `docs/asset-formats.md` → *Sprite graphics*, and
`verify-sprites` reads them to drive the C port with the ROM's own arguments.

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
| `$7E:1300`,`$7E:1330` | 24×2 | `thread_handler` / `thread_handler_bank` — the callback `$80:8480` enters; see below |

## PPU shadow state

| Range | Size | Contents |
| --- | --- | --- |
| `$7E:1360-$7E:136B` | 12 B | BG1-BG4 H/V scroll, written twice per register into `$210D-$2112` |
| `$7E:136C` | 1 B | `brightness_shadow`, restored into INIDISP at the end of NMI |
| `$7E:137E-...` | 2×n | `visible_actors` — actor slot offsets the OAM pass walks (`$80:BD12`) |
| `$7E:13BE-$7E:15DD` | 544 B | `oam_buffer` — DMA'd to `$2104` every frame |
| `$7E:175E-$7E:185D` | 128×2 | `sprite_slot_tick` — last `sched_tick` each cache slot was drawn at (the LRU key) |
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

## Sprite frame cache

The 128-slot LRU cache of 16x16 sprite frames (`docs/asset-formats.md` →
*Sprite graphics*). Only 128 of the game's 4096 frames fit in VRAM at once, so
`$80:B9D6` loads them on demand and `$80:B947` drains the upload queue in
vblank.

| Address | Size | Contents |
| --- | --- | --- |
| `$7E:15DE` | 128×2 | queued upload: source address |
| `$7E:165E` | 128×2 | queued upload: source bank |
| `$7E:16DE` | 128×2 | queued upload: VRAM word address |
| `$7E:2128-$7E:4127` | 4096×2 | `frame_slot` — per frame, the cache slot ×2 holding it, or negative |
| `$7E:4128-$7E:4227` | 128×2 | `slot_frame` — the reverse map, used to evict |

That last pair is what the region report shows as the 8480-byte block
bulk-written by `$80:C06B`: `$80:C05A` fills both with `$FFFF` at boot.

## Sprite display list

**32 records of 20 bytes at `$7E:185E`**, chained into a singly-linked list
whose head is `$7E:1B5E`. A record is addressed by its own WRAM offset, so a
link is just that offset and `0` terminates. `$80:BDF3` clears all 32 and empties
the list, `$80:BE0C` takes a free one and links it in at the head, `$80:BE41`
unlinks one. This is the list the per-frame sprite pass at `$80:BD1F` walks;
`src/port/oam.h` is the port's view of it.

| Offset | Field |
| --- | --- |
| `+$00` | flags. Bit 15 = draw, bit 14 = the position is already screen-space, bit 5 = sort ahead, bits 1-2 = which of the four emitters to use, bits 3/4 = extra attribute bits |
| `+$02` | X — world, or screen when bit 14 is set |
| `+$04` | subtracted from Y before drawing (height off the ground) |
| `+$06` | Y |
| `+$08`/`+$0A` | far pointer to the metasprite; the bank must be `$8F` or `$90` |
| `+$0C` | the scheduler slot (×2) whose handler is told when this record collides — read only by `$80:BE8F` |
| `+$0E` | non-zero groups a record into the overlap pass at `$80:BEC9` |
| `+$10` | attribute bits OR'd in when flags bit 4 is set |
| `+$12` | link to the next record, or 0 |

The remaining bytes belong to logic that is not ported yet.

The camera the pass subtracts lives at `$7E:1B6A` (X) and `$7E:1B6C` (Y), zeroed
at level start by `$80:A65B` and moved by `$80:A691`/`$80:A792`.

## Large buffers

| Range | Size | Contents |
| --- | --- | --- |
| `$7E:4B28-$7E:5327` | 2048 B | tilemap staging — DMA'd to VRAM via `$80:9EB2` |
| `$7E:5F36-$7E:6935` | 2560 B | *unidentified*; written by `$80:ADAC`, `$80:9A74`, `$82:AE00` |
| `$7E:6F00-$7E:7EFF` | 4096 B | `lzss_ring` — the decompressor's sliding window |
| `$7E:8000-$7F:9A85` | 72 KB | decompression output / level data |

## Placement working arrays

Built once at level load from the record's three placement lists (see
`docs/asset-formats.md` → *Actor, victim and object placement*), then read by the
gameplay code. `verify-actors` diffs the victim and object arrays byte-for-byte.

| Address | Field | Built by |
| --- | --- | --- |
| `$7E:6DF4` | victim X, stride 4 | `$82:DB46` |
| `$7E:6DF6` | victim Y, stride 4 | `$82:DB46` |
| `$7E:6E30` | victim count | `$82:DB46` |
| `$7E:6D02` | object X, stride 2 | `$80:C9A5` |
| `$7E:6D48` | object Y, stride 2 | `$80:C9A5` |
| `$7E:1F0A` | object type (low byte), stride 2 | `$80:C9A5` |
| `$7E:1EC4` | object state, stride 2; `$C000` sentinel one past the last | `$80:C9A5` |
| `$7E:1D50` | victim spawn gate — constant `$0010`, set by `$80:85E7` | `$80:85CF` |

## Repeating structures

The region report shows several strided arrays that are only partly touched, so
they appear as many small regions. Two are worth recording now:

* ~~**Stride `$100`, at `$7E:0300`-`$7E:0C00`** — ten ~36-byte structures, all
  written by the same code at `$80:82B7`-`$80:82CC`. An array of ten objects with
  a 256-byte pitch.~~ **Identified**: these are *per-thread direct pages*, and
  the stride is `$80`, not `$100` — see below. The report saw only every other
  page because the busiest threads happened to land on the even ones.
* **Stride `$14` (20 bytes), at `$7E:1872`-`$7E:1A17`** — 22 slots, of which only
  a 2-byte field is read (2012 reads each, identical counts). ~~A 22-entry table
  of 20-byte records, read once per frame.~~ **Identified**: this is the sprite
  display list above, which is 32 records starting one stride lower at
  `$7E:185E`. The report only shows the slots the traced run actually touched,
  and only the flags word at `+$00`, because that is the field the sort and the
  cull read first and reject on.

## Per-thread direct pages — `$7E:0100-$7E:0CFF`

**Each of the 24 scheduler threads owns a 128-byte direct page, and that page is
where its state lives.** This is the biggest single correction to this document,
because it changes what a direct-page operand *means*: `$1E` inside a routine
the scheduler is running is not `$7E:001E`, it is that thread's page plus `$1E`.

The evidence is the spawn path. `$80:82A4` reads a 24-entry word table at
`$80:82DE`, `STA $01 : TCD` installs it, and `$80:82B5`-`$80:82D3` then copy five
words off the caller's stack into `$00`-`$08` of the new page — which is exactly
the "ten ~36-byte structures" the region report showed above. `$80:8480` does the
same `LDA $8082DE,X : TCD` when it calls into another thread's handler, and the
scheduler's own resume path (`... TCS : PLD : PLP : PLB : RTL`) restores each
thread's `D` from its parked stack.

The table's 24 entries are 24 distinct pages covering `$7E:0100-$7E:0CFF` with no
overlap, assigned in an order that is not monotonic:

| Slot | Page | Slot | Page |
| --- | --- | --- | --- |
| 0 | `$0100` | 12 | `$0180` |
| 1 | `$0280` | 13 | `$0200` |
| 2 | `$0380` | 14 | `$0300` |
| … | … | … | … |
| 11 | `$0C80` | 23 | `$0C00` |

Every live region the trace found between `$7E:0100` and `$7E:0CFF` falls inside
one of those pages — including `$7E:0100-$7E:017F`, which is slot 0's page
exactly, and `$7E:01E8-$7E:0227`, which straddles the boundary between slot 12's
and slot 13's.

So an "actor slot table" is not a separate structure to find: **an actor's state
is its thread's direct page**. That is what the camera-driven spawner `$81:80EC`
is allocating when it starts an actor thread, and it is why the enemy collision
handler at `$81:8888` reads its own health from `$1E`.

### Fields, so far

Porting the four collision handlers (`src/port/collide.h`) named fifteen of them.
Each is what *those* routines do with the offset and no more — the page belongs
to each actor's own code, so `$1E` meaning health to an enemy says nothing about
what the player keeps there.

**Read the four tables below as four different layouts, not one.** They are
grouped by which kind of actor's page they were found on, because the pages do
not agree: `$1E` is health to an enemy and a latched event code to a victim; a
shot keeps its display record at `$0A` and a victim keeps its at `$08`. Nothing
here is a struct definition. It is a record of what each actor's own code does
with its own 128 bytes.

#### On a player's or an enemy's page

| Offset | What reads or writes it | Meaning |
| --- | --- | --- |
| `$0E` | `$80:8874`, `$80:D206`, `$80:F95C` | the player's index, already doubled (0 or 2) |
| `$12` | `$81:882B` clears, `$81:8727` posts, `$81:8842` reads | an enemy's death request: 0 = carry on, `$F5F5` = a player killed me |
| `$1E` | `$81:88A5`, `$80:F966` | health, to anything that has any |
| `$22` | `$81:888F` | the collision id, parked before masking. **Bit 15 is not part of the id** — `$81:8891` masks it off, and `$80:C7D9` reads it as which player's weapon this was |
| `$7E` | `$81:87F5`, `$81:88B9` | zeroed by an enemy's init and again on the way into dying; nothing any trace has seen ever reads it |
| `$50` | `$80:F971` posts, `$80:D050` consumes and clears | an event request word |
| `$52` | `$80:F96C`, `$80:F976` | hit recovery: a hit only lands once this goes negative |
| `$58` | `$80:F801` | the record this actor collided with |
| `$70` | `$80:D1EA  LDX $70 : JMP ($D1EF,X)` | which branch of its own state machine is running |

Four more came out of `$80:F87B` and `$80:EA63` — a pickup, and the weapon
selection a pickup can trigger. All four are on a **player's** page only; the
table above is shared with an enemy's because the collision handlers happened
to agree, and these do not test that.

| Offset | What reads or writes it | Meaning |
| --- | --- | --- |
| `$0A` | `$80:EA5A  LDY $0A : STA $000A,Y`, and `$80:EA42` writes `+$0E` through it | the player's own display record — an *address*, exactly as on a shot's page and at the same offset |
| `$0C` | `$80:EA4F  LDX $0C : LDA $FD9C,X` | the index a two-entry table of per-weapon data is read with. It has to be a doubled player number for the table to have two entries — which the page already keeps at `$0E` — so either it is held twice or one of the two is something else. Named for where it is |
| `$12` | `$80:EA58  STA $12` | a word out of that player's weapon-data table, filed when the weapon changes. **The same offset is an enemy's death request**, which is the clearest case yet of two pages disagreeing |
| `$64` | `$80:F88A  CLC : ADC $64` | the base of this player's `player_inventory` array — `$1CCC` or `$1CEC`, the two words at `$80:EAA4` |
| `$2C`,`$2E` | `$80:EA66`, `$80:EA6A`, `$80:EA86  DEC $2E` | the weapon search's scratch: a pointer that is overwritten mid-routine, and a countdown from 15. Neither survives the call, and they are named because the diff compares them |

#### On a weapon shot's page (`$81:FCB2`'s thread)

| Offset | What reads or writes it | Meaning |
| --- | --- | --- |
| `$0A` | `$81:FA48`, `$81:FE21` | the shot's own display record — an *address*, not an index |
| `$42` | `$81:FDD1` sets `$14`, `$81:FD1E  DEC $42 : BNE` is the whole main loop | frames of life left; writing 1 means "end on the next pass" |

#### On a victim's page (`$83:A364`'s thread)

| Offset | What reads or writes it | Meaning |
| --- | --- | --- |
| `$08` | `$83:A39E`, and `$83:A213  LDA ($08),Y` walks it as a pointer | the victim's own display record — an address, like the shot's `$0A` and at a different offset |
| `$18` | `$83:A397` writes, `$83:A1EA  LDA $18 : JSL $80C7D9` reads | which side claimed this victim, in `score_add`'s convention: **bit 15 and nothing else**. Id 5 latches `$0005` here and id 6 latches `$8000`, so the pair is the two players |
| `$1E` | `$83:A364  LDX $1E : BNE` guards on it; every exit writes it; `$83:A239  LDA $1E : BNE` wakes on it | what happened to this victim, **latched** — the first thing to reach it decides, and everything after is ignored. `$83:A23D` writes 3 itself after a 300-frame timeout, so 3 is "nobody came" |
| `$26` | `$83:A3B1  LDA $26 : BNE` | read once, to decide whether to clear the record's `ACTOR_COLLIDE_ID`. Named for where it is rather than for what it means: one `BNE` is not evidence, and no input has taken both sides of it |

#### On the object manager's page (`$80:CAEE`'s thread)

The odd one out, and the reason it is worth its own table: this page does not
belong to *an* object. `$80:C9D6  LDA #$CAEE : LDY #$0080 : JSL
thread_set_handler` registers one handler on one thread for **every object in
the level**, so what the page holds is not an object's state but a queue of the
ones that were touched. Which object is being reacted to comes in through
`W_HANDLER_SELF` (`$7E:0078`) rather than off this page at all.

| Offset | What reads or writes it | Meaning |
| --- | --- | --- |
| `$12` | `$80:C9E0  STZ $12` initialises, `$80:CB0E`/`$80:CB12` reads and advances by 2 | how many **bytes** of the queue below are in use — a cursor, not a count, because `STA $14,X` indexes by it directly |
| `$14` | `$80:CB10  STA $14,X` | the queue itself: display-record *addresses* of objects picked up and not yet dealt with, one word each. Nothing in this routine bounds it; the manager's own pass is what empties it |

One consequence for reading listings: `zamn_disasm` resolves a direct-page
operand as though `D` were `$0000`, so inside any of these routines its symbol
column is wrong. `$70` annotated `joy2_raw` is the page's `$70`, not `$7E:0070`.

## Thread handler callbacks — `$7E:1300`, `$7E:1330`

~~24×2 per-thread words written by `$80:8475`; *unidentified*.~~ **Identified**:
a thread registers a callback by putting its address in `$7E:1300+slot×2` and
its bank in `$7E:1330+slot×2` (`$80:8475`, which takes the address in A and the
bank in Y and indexes by `sched_cur_task`). `$80:8480` is the other end: given a
slot in X and one word of argument in Y, it builds a call frame out of those two
words, installs the thread's direct page, and `RTL`s into the handler; when the
handler returns, carry set makes `$80:84A8` park the thread by writing `$8000`
to its `thread_wait`. A slot with both words zero makes the whole call a no-op.

This is how the sprite pass reaches actor behaviour. `$80:BEC9` finds a touching
pair, `$80:BE8F` reads each record's `+$0C` — the thread slot — and dispatches
twice, telling each actor the other's collision id. All of that is ported now,
along with the two handlers it reaches in ordinary play; see `src/port/collide.h`
and `docs/cosim.md` → *Through the door*.

## Per-player arrays — `$7E:1CB8`, `$7E:1CBC`, `$7E:1CC0`

Two entries of one word each, indexed by a player number already doubled — the
same doubled index the player thread keeps at `$0E` of its own page. `$80:8874`
seeds all three for each player that is in the game (`$1CB8` = 10, `$1CBC` = 0,
`$1CC0` = 7) and uses `CPX #$0000` to tell player 1 from player 2, which is what
fixes the stride and the direction.

`$1CBC` is the selected weapon: `$80:D219` uses it as an index into a table,
`$80:F38D`/`$80:F3A5` save and restore it in a pair with `$1CC0`, and `$80:F950`
singles out one value of it when deciding whether a collision hurts. That last
one is why the port names it at all. **Negative means none selected** —
`$80:EA8A` writes `$FFFF` when the search comes up empty, and both `$80:EA72
BMI` and `$80:F8A6  BPL` test the sign.

## Per-player inventory — `$7E:1CCC`, stride `$20`

Fourteen words per player, and the same **BCD** encoding the score uses. The two
base addresses are the words at `$80:EAA4` — `$1CCC` and `$1CEC` — indexed by
the same doubled player number, and `$80:EA7E  CPY #$001C` is what bounds a walk
at fourteen entries. The player's own page caches its base at `$64`.

Two routines touch it and they are the two halves of picking something up.
`$80:F87B` adds to one slot: the item's collision id, doubled, minus `$18`, is
the byte offset, and the amount comes from a parallel 21-word table at
`$80:F8AC` — `SED : CLC : ADC`, capped at `$0999`. `$80:EA63` searches the whole
array for a non-empty slot, which is what pressing **B** does.

The ids that reach `$80:F87B` run `$0C..$20`, which is 21 of them against 14
slots, and the last seven of the amount table are zero — so those ids add
nothing and store the counter back unchanged. Nothing bounds the index, so on
player 1 they write past the end of the array and into player 2's.

## The score — `$7E:1E72`, and the slot map at `$7E:1E84`

Two score slots of one 32-bit **BCD** counter each, four bytes apart: `$1E72`
low, `$1E74` high, and the same again at `$1E76`/`$1E78`. `$80:C7EB` and
`$80:C801` are the two copies of the addition — `SED : CLC : ADC` on the low
half, and a second `ADC` into the high half only if that carried — and `$80:C0CF`
is the HUD noticing the value changed and redrawing it.

The slots are not indexed by player. `$80:C7D9`'s caller supplies a **side**, 0
or 2, which is bit 15 of the collision id turned into a number, and `$80:C7C2`
searches the two words at `$7E:1E84` for it: found at `$1E84` means slot 0, found
at `$1E86` means slot 1, and found in neither means the points are dropped —
which is a real branch (`$80:C817  PLA : RTL`) rather than an oversight.
`$80:925D` seeds the pair 0 and 2, so with one player the search is the identity
and nothing about it can be proved; see `docs/cosim.md` → *The last decline*.

Two awards fell out of the diff and are worth having written down: a victim
rescued is `$1000` and an enemy killed is `$0100`, both BCD, both constants in
their callers (`$83:A1D5` and `$81:8727`).

## What is still missing

This run only covers boot → title → the first seconds of level 1. Absent from the
map so far: the password system, level transitions, the pause/inventory screens,
bosses, and the two-player path. Extending `movies/` with longer scripted play is
the cheapest way to fill those in — the reports regenerate automatically.
