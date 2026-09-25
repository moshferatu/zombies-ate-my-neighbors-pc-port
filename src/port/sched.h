// The frame's own machinery: the scheduler, and the two vblank dispatchers.
//
// Everything else in `src/port/` is a routine that is called and returns. None
// of these is. `thread_yield` is entered by one thread and leaves into another;
// a dispatcher leaves into a job by `RTL` and comes back at an address inside
// itself. So each is written as what it really is, a stretch of the ROM that
// runs from one address to the next place control leaves it, over the whole
// 65816 register set: A, X, Y, the status byte, the stack pointer, the direct
// page and the data bank, plus the address to go on at. See `docs/threads.md`,
// "The scheduler itself".
//
// **The stack is WRAM**, and every push and pull here is a WRAM access at `S`.
// The ROM's pushes are copied byte for byte, including the ones that are dead
// the moment they are made (`PEA $0000 : PLD`, `PHK : PLB`): that is what lets
// `verify` compare these with no dead-stack allowance at all, where every other
// routine is allowed the bytes under its own pushes.
//
// What is not here is the one instruction each stretch leaves by. That stays
// the ROM's: the `RTL` into the thread being resumed, the `WAI` that ends the
// frame, the `RTL` into a job. The port stops on it and the core executes it.
//
// Port code: libc only.

#ifndef PORT_SCHED_H
#define PORT_SCHED_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/player.h"
#include "port/rng.h"
#include "port/wram.h"

// --- The scheduler ------------------------------------------------------------
//
//     thread_yield ($80:8353)       thread_exit ($00:833E)
//       park this thread              free this thread's slot
//                 \                  /
//                  scan ($80:836A) -- a slot ready: switch to it, RTL ($80:8397)
//                    | ran off the end
//                  WAI ($80:8371) -- the frame boundary; NMI runs
//                  sched_wake ($80:8372) -- tick, stack to $125F
//                  JSL sprite_build_oam ($80:837C)
//                  sched_rescan ($80:8380) -- tick every wait, scan from slot 0
//
// Three entries into one scan and one exit out of each. The port has one
// function per entry.
//
// **And two banks.** `thread_spawn` builds a thread's exit return address with
// a bank of zero, so a body's final `RTL` lands on `$00:833E`, the same code
// through the slow mirror. The scan it falls into leaves by `$00:8397` or
// `$00:8371`, and after that `WAI` the whole next frame's housekeeping runs in
// bank `$00`, until a thread's `JSL $808353` brings it back. Every exit here is
// in the bank the stretch was entered in, and every `PHK` pushes that bank.

#define THREAD_YIELD_ENTRY_PC 0x808353u
#define THREAD_EXIT_ENTRY_PC 0x00833eu
#define SCHED_WAKE_PC 0x808372u
#define SCHED_RESCAN_PC 0x808380u
// Where the three leave.
#define SCHED_WAI_PC 0x808371u     // `WAI`: no thread is ready this tick
#define SCHED_RESUME_PC 0x808397u  // `RTL`: into the thread just picked
#define SCHED_OAM_PC 0x80837cu     // `JSL $80BD1F`, from `sched_wake`

// `$80:8378  LDX #$125F : TXS` — the scheduler's own stack, which it runs the
// frame's housekeeping on.
#define SCHED_STACK 0x125f

// What each run of the ROM's instructions was taken how many times, so the
// harness can price the call. The names are the runs; `routines.c` has their
// costs.
enum {
  SCHED_PARK,        // $8353-$8369  thread_yield's opening, to STA $11B0,X
  SCHED_EXIT,        // $833E-$8351  thread_exit's, to its BRA into the scan
  SCHED_STEP,        // INX INX CPX BNE, and on to the next slot
  SCHED_WRAP,        // ...off the end: the next instruction is the WAI
  SCHED_EMPTY,       // LDA BPL, the slot not live
  SCHED_ASLEEP,      // LDA BPL ASL BNE, live but still counting down
  SCHED_RESUME,      // ...ready: STX $08, and the switch to its stack
  SCHED_WAKE,        // $8372-$837B  INC $20, the carry not taken, LDX TXS
  SCHED_WAKE_CARRY,  // ...and the carry into $22
  SCHED_TICK_HEAD,   // JSR $8398, LDX #$2E
  SCHED_TICK_EMPTY,  // LDA BPL: a slot not live
  SCHED_TICK_DONE,   // LDA BPL CMP BEQ: a slot at $8000 already
  SCHED_TICK_STEP,   // LDA BPL CMP BEQ DEC STA: a counter stepped down
  SCHED_TICK_NEXT,   // DEX DEX BPL, back round
  SCHED_TICK_TAIL,   // DEX DEX BPL out, RTS, LDX #$0000
  SCHED_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[SCHED_BLOCK_COUNT];
} SchedWork;

// `$80:8353`. A is the ticks to sleep, as the caller's `JSL` left it.
void thread_yield_port(Wram* w, PortCpu* c, SchedWork* k);

// `$80:833E`, reached by `RTL` when a thread body returns. Assumes 16-bit A
// and X, which the ROM does too: `LDA #$0000` is three bytes only then. The
// harness declines the call otherwise.
void thread_exit_port(Wram* w, PortCpu* c, SchedWork* k);

// `$80:8372`, the instruction after the `WAI`: the tick, and the stack.
void sched_wake(Wram* w, PortCpu* c, SchedWork* k);

// `$80:8380`: `JSR thread_tick_waits`, then the scan from slot 0.
void sched_rescan(Wram* w, PortCpu* c, SchedWork* k);

// --- The two vblank dispatchers --------------------------------------------
//
// `$80:83E0` and `$80:843D` are one routine with two sets of constants. Each
// walks its queue downwards from the last slot and reaches a job by pushing a
// return address inside itself and then the job's own address, and executing
// `RTL`. The job comes back to the next instruction, which reads the carry the
// job left: set means keep the job for next frame, clear means a one-shot, and
// the slot is freed.
//
// So a dispatcher is two stretches: from its entry to the first job or its
// `RTS`, and from a job's return to the next job or the `RTS`.

typedef struct {
  uint32_t entry;     // $80:83E0          $80:843D
  uint32_t resume;    // $80:8401          $80:845E
  uint32_t dispatch;  // $80:8400  RTL     $80:845D
  uint32_t done;      // $80:8417  RTS     $80:8474
  uint16_t table;     // $12A0             $12E0
  uint16_t count_at;  // $0C               $0E
  uint16_t last;      // $38               $1C: the last slot's offset
} VblQueueDesc;

extern const VblQueueDesc VBL_QUEUE_A_DESC;
extern const VblQueueDesc VBL_QUEUE_B_DESC;

// `$10` counts the one-shots still to go and stops the walk early at zero;
// `$12` holds X across the job.
#define W_VBL_QUEUE_REMAINING 0x0010
#define W_VBL_QUEUE_INDEX 0x0012

enum {
  VBL_RUN_EMPTY_QUEUE,  // LDA BEQ: nothing queued, straight to the RTS
  VBL_RUN_HEAD,         // LDA BEQ STA LDX: the walk starts
  VBL_RUN_FREE_SLOT,    // LDA BEQ: a slot with no job
  VBL_RUN_JOB,          // ...a job: the pushes, STX $12, up to the RTL
  VBL_RUN_KEPT,         // LDX $12, BCS: the job asked to stay
  VBL_RUN_DROPPED,      // LDX $12 BCS LDA STA DEC DEC: a one-shot, freed
  VBL_RUN_DROPPED_OUT,  // ...BEQ: and it was the last one
  VBL_RUN_DROPPED_ON,   // ...BEQ not taken
  VBL_RUN_NEXT,         // DEX x4 BPL, back round
  VBL_RUN_END,          // DEX x4 BPL out, to the RTS
  VBL_RUN_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[VBL_RUN_BLOCK_COUNT];
} VblRunWork;

// From the entry, or (`resumed`) from a job's return with the job's carry in
// `c->p`. Assumes 16-bit A and X, and a direct page of zero: the harness
// declines anything else.
void vbl_queue_run(Wram* w, const VblQueueDesc* q, bool resumed, PortCpu* c,
                   VblRunWork* k);

// --- The NMI handler, `$80:8179` -----------------------------------------------
//
// Four stretches, split where the handler touches the hardware. Those
// instructions stay the ROM's: `LDA $4210` acknowledges the interrupt,
// `STA $2100` blanks and unblanks the screen, and `$80:81B5` waits for the
// auto-joypad read to finish. The joypad itself is the exception. `LDA $4218`
// and `LDA $421A` only read a latch, and the two words come in as arguments,
// which is how a port with its own input would hand them over anyway.
//
//     nmi_enter  $80:8179  save A X Y D B, D = 0, B = $80, count the frame,
//                          and the re-entry guard -> $818F, or all the way
//                          out to the RTL at $81F8 if an NMI is already running
//     nmi_stack  $80:8199  park S in $04 and move to NMI's own stack -> $81A2
//     nmi_input  $80:81BB  the two pads, raw and as direction codes -> $81E1
//     nmi_leave  $80:81E4  the rng tick, S back, the guard off, the pulls,
//                          up to the RTL at $81F8

#define NMI_ENTER_PC 0x808179u
#define NMI_STACK_PC 0x808199u
#define NMI_INPUT_PC 0x8081bbu
#define NMI_LEAVE_PC 0x8081e4u
#define NMI_BLANK_PC 0x80818fu  // `SEP #$20 : LDA $4210`
#define NMI_FLUSH_PC 0x8081a2u  // `JSL vram_queue_flush`
#define NMI_QUEUE_B_PC 0x8081e1u  // `JSR vbl_queue_b_run`
#define NMI_RETURN_PC 0x8081f8u  // `RTL`, to the trampoline's `PLB : RTI`

// `$80:81F9`: sixteen bytes, one direction code per D-pad nibble.
#define NMI_DIR_TABLE 0x8081f9u
// NMI's own stack, from `$80:819E  LDA #$129F : TCS`.
#define NMI_STACK 0x129f

// `$14`'s bit 15 is set while a handler runs. `$1EB4`, when non-zero, stops
// the handler's `INC $24`, which is a 16-bit step of `W_RNG_STATE` and
// `W_RNG_COUNTER` together (`port/rng.h`). The pads go to `W_JOY_RAW` and
// `W_JOY_DIR` (`port/player.h`), a word per player.
#define W_NMI_FLAGS 0x0014
#define W_RNG_HOLD 0x1eb4

enum {
  NMI_ENTER,       // $8179-$818D, the BNE not taken
  NMI_ENTER_BUSY,  // ...taken, and the five pulls
  NMI_STACK_RUN,   // $8199-$81A1
  NMI_INPUT_RUN,   // $81BB-$81DF
  NMI_LEAVE,       // $81E4-$81F7, with the rng held
  NMI_LEAVE_TICK,  // ...or ticked
  NMI_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[NMI_BLOCK_COUNT];
} NmiWork;

void nmi_enter(Wram* w, PortCpu* c, NmiWork* k);
void nmi_stack(Wram* w, PortCpu* c, NmiWork* k);
// `rom` is for the table; `joy1`/`joy2` are `$4218`/`$421A` as read.
void nmi_input(Wram* w, const Rom* rom, PortCpu* c, uint16_t joy1,
               uint16_t joy2, NmiWork* k);
void nmi_leave(Wram* w, PortCpu* c, NmiWork* k);

// --- The reset's WRAM clear -----------------------------------------------------
//
// `$80:80C1`, where `JSR init_ppu_regs` returns, to the `SEP #$20` before
// `STA $4200` turns the NMI on. Both banks of WRAM cleared by `MVN`, a byte an
// instruction -- 131,071 instructions on a cold start, which is why the reset
// vector was 4.3% of what the ROM still ran with the frame ported.
//
// A warm start keeps `$7E:2000-$2127`, the top-scores table (see
// `src/hiscore.h`), and it knows one by four magic words in it: `$A675` at
// `$2000`, `$98A3` at `$2062`, `$4102` at `$2122` and `$2217` at `$2126`. A
// cold start clears everything and writes them. Either way the second move
// then copies bank `$7E` over `$7F`, so the table is in both.

#define RESET_CLEAR_PC 0x8080c1u
#define RESET_NMI_ON_PC 0x808152u  // `SEP #$20 : LDA #$81 : STA $4200`

enum {
  RESET_HEAD,    // LDA #0 : STA $7E0000 : LDA $7E2000 : CMP : BNE
  RESET_CHECK,   // LDA $7E2xxx : CMP : BNE, the other three magic words
  RESET_TAKEN,   // ...one of them wrong: the BNE to the cold start
  RESET_SETUP,   // LDA : LDX : LDY before either first move
  RESET_WARM,    // $80F8-$8105, between the warm start's two moves
  RESET_BRA,     // BRA $8135
  RESET_MAGIC,   // $8119-$8131, the cold start's four stores
  RESET_TAIL,    // $8135-$814F, less the move in it
  RESET_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[RESET_BLOCK_COUNT];
  uint32_t moved;  // bytes `MVN` moved, each one an instruction
} ResetWork;

void reset_clear(Wram* w, PortCpu* c, ResetWork* k);

#endif
