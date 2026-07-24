# Frame skeleton

How ZAMN gets from RESET to a steady 60 Hz update loop. Everything here was
read off traces produced by `zamn_trace` (`analysis/reset_trace.txt`,
`analysis/nmi_trace.txt`) and the CDL-driven listings in `analysis/bank_*.asm`.
Addresses use the `$80-$9F` FastROM banks the game actually executes in.

> **The headline finding:** ZAMN is not a flat state machine. It runs a
> **24-slot cooperative thread scheduler with per-thread stacks**, and the
> "main loop" is a `WAI` inside that scheduler. Game logic is written as
> coroutines that call `thread_yield` to sleep for N ticks. This has direct
> consequences for Phase 3 — see [Porting consequences](#porting-consequences).

## Vectors

| Vector | Address | Notes |
| --- | --- | --- |
| RESET | `$00:80AE` | native-mode switch, then `JML $80:80B6` |
| NMI | `$00:816C` | trampoline, then `JML [$0000]` → `$80:8179` |
| IRQ | `$00:8209` | **never taken** in 2400 traced frames |

The IRQ vector never fires: ZAMN drives everything from NMI plus HDMA. There is
no h/v-timer interrupt to reproduce.

## Boot

```
$00:80AE  CLC / XCE               ; -> native mode
$00:80B0  REP #$30                ; 16-bit A/X/Y
$00:80B2  JML $80:80B6            ; leave bank $00 for the FastROM mirror
$80:80B6  LDX #$01FF / TXS
$80:80BD  TCD  (D = $0000)        ; direct page pinned at $0000 for the whole game
$80:80BE  JSR init_ppu_regs       ; $80:8002 — INIDISP=$8F, every PPU/DMA reg cleared
$80:80A4  STA $420D  (#$01)       ; FastROM on
$80:80C4  STA $7E0000 / LDA $7E2000 / CMP #$A675
                                  ; warm-boot signature check; mismatch -> full clear
$80:8116  MVN $7E,$7E             ; zero all 128 KB of WRAM
```

~~Direct page stays at `$0000` for the entire game, so every `$xx` direct-page
operand is literally `$7E:00xx`. That is a convenient invariant for the port.~~

**Corrected.** `$0000` is what boot sets and what the scheduler, NMI and the
per-frame housekeeping all run on — which was every routine ported until the
collision handlers (`src/port/collide.h`), and is why the invariant held for as
long as nothing looked past it. But **a thread
runs on its own 128-byte direct page**: `$80:82A4` installs one from a 24-entry
table at `$80:82DE` when the thread is spawned, the resume path restores it with
`PLD`, and `$80:8480` swaps to another thread's page to call its handler. Inside
thread code a `$xx` operand is that thread's page plus `$xx`, so `$1E` is an
actor's own state and not `$7E:001E`. See `docs/wram-map.md` → *Per-thread
direct pages*, which is also where the "array of ten objects at stride `$100`"
in that document turned out to come from.

This matters for reading listings as much as for porting: `zamn_disasm` resolves
direct-page operands as though `D` were `$0000`, so its symbol annotations are
correct for scheduler-level code and misleading for anything the scheduler
dispatches.

## The scheduler (`$80:8353`)

`thread_yield` is the most-called routine in the game (4115 calls in a 2400-frame
trace). Every piece of game logic is a thread that periodically sleeps.

```
thread_yield:            ; A = ticks to sleep
  PHB / PHP / REP #$30 / PHD / PEA $0000 / PLD / PHK / PLB
  LDX sched_cur_task
  ORA #$8000
  STA thread_wait,X      ; bit 15 = slot live, low bits = ticks remaining
  TSC
  STA thread_sp,X        ; park this thread's stack pointer
scan:
  INX / INX
  CPX #$0030             ; 24 slots x 2 bytes
  BNE find_ready
  ; --- wrapped past the end of the table: one scheduler pass is over ---
  WAI                    ; sleep until the next NMI
  INC sched_tick
  LDX #$125F / TXS       ; run housekeeping on the scheduler's own stack
  JSL $80:BD1F           ; per-frame actor/sprite work
  JSR thread_tick_waits  ; decrement every live thread's counter
  LDX #$0000
find_ready:
  LDA thread_wait,X
  BPL scan               ; slot empty
  ASL A
  BNE scan               ; still counting down
  STX sched_cur_task
  LDA thread_sp,X / TCS  ; switch stacks
  PLD / PLP / PLB / RTL  ; resume that thread where it yielded
```

So the frame boundary is `WAI` at `$80:8371`: the CPU halts, NMI fires, and the
scheduler resumes and re-scans. `thread_tick_waits` (`$80:8398`) decrements every
live counter, stopping at `$8000` so an expired thread stays runnable.

Threads live in two parallel 24-entry tables — `thread_wait` at `$7E:1180` and
`thread_sp` at `$7E:11B0` — with each thread's stack somewhere in `$7E:10xx-$12xx`.

## The NMI handler (`$80:8179`)

The vector at `$00:816C` is only a trampoline:

```
$00:816C  PHB / PEA $0000 / PLB   ; DB = 0
$00:8171  PER $008176             ; push a return address inside bank $00
$00:8174  JML [$0000]             ; far pointer at $00:0000 -> $80:8179
...
$00:8177  PLB / RTI               ; the handler RTLs back here
```

The handler proper, in execution order:

| Step | Code | What it does |
| --- | --- | --- |
| 1 | `REP #$30`, push A/X/Y/D/B, `D=0`, `PHK/PLB` | save the interrupted thread's context |
| 2 | `INC nmi_frame_counter` | `$7E:0016`, +1 per frame |
| 3 | `LDA #$8000 / TSB nmi_flags / BNE exit` | re-entrancy guard on bit 15 of `$7E:0014` |
| 4 | `LDA $4210` | acknowledge the NMI |
| 5 | `LDA #$80 / STA $2100` | **force blank on** — VRAM/CGRAM/OAM are now writable |
| 6 | `TSC / STA nmi_saved_sp`, `LDA #$129F / TCS` | switch to the dedicated NMI stack at `$7E:129F` |
| 7 | `JSL vram_queue_flush` | drain the queued VRAM DMAs (see below) |
| 8 | `JSR vbl_queue_a_run` | run the **forced-blank** job queue |
| 9 | `LDA brightness_shadow / STA $2100` | **force blank off**, restore brightness |
| 10 | `LDA $4212 / LSR / BCS -` | spin until auto-joypad read finishes |
| 11 | `$4218 -> joy1_raw`, `$421A -> joy2_raw`, direction nibble through the table at `$80:81F9` into `joy1_dir`/`joy2_dir` | latch this frame's input |
| 12 | `JSR vbl_queue_b_run` | run the **post-blank** job queue |
| 13 | `LDA $1EB4 / BNE + / INC $24` | conditional secondary counter |
| 14 | restore SP from `nmi_saved_sp`, `TRB nmi_flags`, pop, `RTL` → `RTI` | return to the interrupted thread |

Steps 7-9 are the entire VRAM-safe window. Everything a frame draws must already
be queued before NMI fires.

### The two vblank job queues

Both queues store `{u16 address-1, u8 bank, u8 pad}` per slot and are dispatched
by pushing the far address and executing `RTL` into it.

| Queue | Table | Slots | Count | Add | Run | When |
| --- | --- | --- | --- | --- | --- | --- |
| A | `$7E:12A0` | 16 | `$7E:000C` | `$80:83AE` | `$80:83E0` | during forced blank |
| B | `$7E:12E0` | 8 | `$7E:000E` | `$80:8418` | `$80:843D` | after blanking ends |

A job returns with the carry flag as its verdict:

* **carry set** — keep the slot; the job runs again next frame.
* **carry clear** — one-shot; the dispatcher zeroes the slot and decrements the count.

Jobs observed in queue A during gameplay: the OAM upload (`$7E:13BE`, 544 bytes →
`$2104`), the BG-scroll shadow copy (`$7E:1360-$136B` → `$210D-$2112`, each
register written twice as the hardware requires), and CGRAM/VRAM uploads.

### The VRAM upload queue (`$80:9E7B`)

Gated by bit 6 of `render_flags` (`$7E:0026`), this drains a list of pending VRAM
DMAs held in five parallel arrays indexed by a byte count in `$7E:00CE`:

| Array | Address | Meaning |
| --- | --- | --- |
| `vram_queue_src` | `$7E:1B84` | source address |
| `vram_queue_bank` | `$7E:1BB4` | source bank |
| `vram_queue_dest` | `$7E:1BE4` | VRAM word address → `$2116` |
| `vram_queue_vmain` | `$7E:1C14` | increment mode → `$2115` |
| `vram_queue_size` | `$7E:1C44` | byte count → `$4305` |

Each entry becomes one channel-0 DMA to `$2118`. This is the choke point every
graphics update passes through, which makes it the natural seam for the Phase 5
hi-res/widescreen renderer.

## Porting consequences

1. **Coroutines, not a state machine.** A zelda3-style port replaces one routine
   at a time with C. ZAMN's routines suspend mid-body via `thread_yield` and
   resume with their stack intact, so a naive C function cannot stand in for one.
   Each ported thread needs either an explicit resume-point state machine or a
   real coroutine (fibers / `ucontext` / a saved stack). Decide this before
   porting the first thread; it shapes every later one.
2. **`WAI` is the frame boundary.** The co-simulation harness should compare
   WRAM at the `WAI` in `scheduler_idle`, not at an arbitrary instruction count.
3. **Direct page is always `$0000`** and the IRQ vector is never taken — two
   invariants that simplify the reimplementation.
4. **All PPU traffic is queued.** Nothing writes VRAM outside NMI. Retargeting
   rendering later means intercepting the two job queues and the VRAM upload
   list, not chasing scattered `$21xx` stores.
