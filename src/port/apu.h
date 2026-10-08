// The port's window onto the APU — and the first piece of *hardware* any port
// code touches.
//
// ## The decision this file is
//
// PROGRESS.md carried an item for several rounds that was not a routine but a
// design question. Two addresses were left in the decline census on every movie
// — `$80:F92D` and `$80:F87B` — and both of them open with `JSL apu_play_sfx`.
// Neither is hard; both were blocked on nobody having decided **how port code
// drives the APU**, because until now the port has only ever touched WRAM, and
// `port/wram.h` is passed in as a `Wram*` precisely so the harness can hand a
// routine a private copy of it and diff the result.
//
// The APU cannot work like that. There is one of it, it is stateful, and
// writing to it is not something you can do speculatively on a scratch copy and
// throw away. PLAN.md's Phase 4 also says it never becomes native: the SPC700
// stays emulated for good, so what the port owes the APU is not a computation
// but **the same bytes in the same order on the same three ports**.
//
// So the shape is:
//
//   * everything `$80:CCC8` does to *memory* is ordinary port code — it is one
//     byte, `W_APU_SEQ`, and the port owns it exactly as it owns the rest of
//     WRAM;
//   * everything it does to the *bus* goes out through `ApuPorts`, a hook the
//     host installs once;
//   * and the **wait** — the `CPY $2143 : BNE` the ROM spins on until the SPC
//     echoes the last command back — is the host's problem, not the port's,
//     because the port has no way to advance an SPC700 and no business trying.
//
// That last split is the load-bearing one. It leaves `apu_send()` a pure
// function of WRAM plus two arguments, which means the harness can verify it
// per call exactly like every other routine, and the only thing it cannot check
// by diffing memory is whether the hook was handed the right `(cmd, param)`.
// That is checked too, and not by memory: under `verify` the harness captures
// what the ROM's own `STX $2142` put on the bus during the call and compares it
// with what the port asked to send. See `docs/cosim.md`.
//
// The cost, stated plainly: **one global.** `apu_attach()` is not threaded
// through as a parameter the way `Wram*` is, and the reason is not convenience
// — it is that the two are different kinds of thing. The harness needs two
// copies of WRAM at once and routinely has them; there is never a reason to
// have two APUs, and a machine that had two would not be this one. Every
// routine between `sprite_build_oam` and a sound effect is six frames deep and
// none of them mentions audio, so threading a parameter through all six would
// be describing the harness's plumbing in the game's code. With no hook
// attached the port still computes correctly and simply makes no noise, which
// is the right behaviour for a headless diff.
//
// Port code: libc only.

#ifndef PORT_APU_H
#define PORT_APU_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/hw.h"
#include "port/wram.h"

// The three ports `$80:CCC8` uses, named here as well as in `assets/music.h`
// because a host implementing the hook needs them and must not have to include
// the asset decoder to get them. Reading `$2143` returns what the SPC wrote;
// writing it is what the SPC reads back. Same address, two registers.
#define APU_PORT_PARAM 0x2141u  // `STA $2141` — the command's argument
#define APU_PORT_CMD 0x2142u    // `STX $2142` — the command
#define APU_PORT_SEQ 0x2143u    // the sequence counter, both ways

// `$80:CC42  LDX #$0001`. The driver's "play a sound effect" command; the same
// value `assets/music.h` calls `MUSIC_CMD_PLAY_SFX`, which is where the rest of
// them are enumerated.
#define APU_CMD_PLAY_SFX 0x01

// What the host has to be able to do.
//
// One call means: wait until the SPC has echoed `seq` back on `APU_PORT_SEQ`,
// then put `cmd` on `APU_PORT_CMD`, `param` on `APU_PORT_PARAM` and `seq + 1`
// on `APU_PORT_SEQ`. That is `$80:CCC8` minus its two WRAM accesses, which is
// everything about it the port cannot do.
//
// The waiting is deliberately on this side of the line. It is unbounded in
// principle, it is the one part of the routine whose duration is not the port's
// to decide, and under `verify` it must not happen at all — the ROM already ran
// the real one and the port is being replayed over a snapshot of the past.
typedef struct {
  void (*send)(void* ctx, uint8_t seq, uint8_t cmd, uint8_t param);
  void* ctx;
} ApuPorts;

// Install the hook, or `NULL` to detach. The pointer is kept, not copied, so it
// must outlive the port's use of it.
void apu_attach(const ApuPorts* ports);

// --- $80:CCC8  apu_send -----------------------------------------------------

// What the routine leaves in the caller's registers. Wanted only by the
// harness: the caller inside this file discards all six, and so does every
// caller in the ROM, but an unclaimed output is an unchecked output.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} ApuSendRegs;

// Send one command. Increments `W_APU_SEQ` and hands the pre-increment value to
// the hook, which is the value the SPC is expected to be echoing right now.
// `out` may be NULL.
//
// **The arguments are A and X, full width, and not the two bytes that go on the
// bus** — because which bytes those are is the routine's own business. This was
// recorded here for several rounds as a reason `$80:CCC8` could not be
// registered as a routine in its own right: it has two kinds of caller, and they
// disagree about register width. `apu_play_sfx` arrives 16 bits wide (`REP #$30`
// four instructions earlier); the data-set uploader arrives 8 (`$80:CC90  SEP
// #$30`), and it is the one that matters, because it sends 23,820 commands on
// one movie against `apu_play_sfx`'s 371. `CosimRegs` has no width field, so the
// concern was that intercepting here would compare high bytes that mean nothing.
//
// It does not, and the reason is the routine's first instruction. `SEP #$30`
// normalises everything the contract depends on before anything else happens:
// the index registers have their high bytes *cleared* by that very instruction,
// `Y` is then loaded fresh from one byte of WRAM, and `A` is only ever read — so
// its high byte survives untouched whatever it was, hidden `B` register or not,
// and both sides return it unchanged. There is nothing left for a width field to
// disambiguate. Registered, and checked on every call either kind of caller
// makes; see `docs/cosim.md` → *The uploader's 23,820 calls*.
void apu_send(Wram* w, uint16_t a, uint16_t x, ApuSendRegs* out);

// --- $80:CC3B  apu_play_sfx -------------------------------------------------

// What the routine leaves in the caller's registers. `a` and `x` are constants
// and `y` is the new sequence counter; the flags are the interesting part.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} ApuSfxRegs;

// Play sound effect `id`. `caller_dp` is the direct page the `JSL` arrived on,
// which the routine parks with `PHD` and restores with `PLD` — and **`PLD` sets
// N and Z**, so the caller's own direct page is what decides two of the three
// flags this returns. It is the first ported routine whose output depends on a
// register the caller never thought it was passing.
void apu_play_sfx(Wram* w, uint16_t id, uint16_t caller_dp, ApuSfxRegs* out);

// --- $80:CC13 and $80:CC27  one fixed command each ----------------------------
//
// Two more of `apu_play_sfx`'s shape, each with its argument built in:
// `LDA #$0032`, then `LDX #$0001` at `$80:CC13` and `LDX #$0002` at
// `$80:CC27`. So the first is sound effect `$32` played. What command 2 is
// to the driver I have not established, nor what `$32` is to either. They
// come back as `apu_play_sfx` does, with `$0032` in A and the command in X.
#define APU_FIXED_PARAM 0x0032
#define APU_FIXED_1_ENTRY 0x80cc13u
#define APU_FIXED_1_RTL 0x80cc26u
#define APU_FIXED_2_ENTRY 0x80cc27u
#define APU_FIXED_2_RTL 0x80cc3au
#define APU_CMD_2 0x02
void apu_fixed_command(Wram* w, uint8_t cmd, uint16_t caller_dp,
                       ApuSfxRegs* out);

// --- $80:CCBF  apu_next_byte ------------------------------------------------
//
// Five instructions, and 255,859 calls over the eleven profiled movies — third
// in the registry behind `sprite_frame_tile` (577,573) and `apu_send`
// (257,114), and ahead of `thread_yield` (240,307). That it lands within 1,255
// calls of `apu_send` is not a coincidence but the shape of the caller: the
// data-set uploader `$80:CC7C` points `$18`/`$19`/`$1A` at a table entry in ROM
// and then does nothing but fetch a byte through here and hand it straight to
// `apu_send`, two bytes of length and then that many bytes of payload.
//
// It is here rather than in `port/lzss.h` next to the other two stream readers
// because it is not a decoder: there is no window, no control byte and no end
// marker. It is a pointer with an increment, and the only thing about it that
// is worth writing down is that the increment is done **eight bits at a time**.
//
//     LDA [$18] : INC $18 : BNE +2 : INC $19
//
// `$80:CC90  SEP #$30` is still in force, so `INC $18` increments the *byte* at
// `$18` and the `BNE` is the carry into `$19` written out by hand. A 16-bit
// `INC $18` would do the same arithmetic and would be one instruction shorter,
// and the ROM does not use one — which matters, because it means `$1A` is never
// touched. A data set that runs off the end of its bank wraps to `$xx:0000`
// rather than crossing into the next one, exactly as `rom_ptr` describes.
//
// The flags come out of whichever `INC` ran last, so **N and Z describe the
// cursor, not the byte**: 255 calls in every 256 return N from bit 7 of the new
// low byte with Z clear, and the 256th returns the high byte's. Nothing reads
// them — the caller's next instruction is `STA $1C` — but an unclaimed output
// is an unchecked output, and at about 23,900 calls a movie the wrap comes
// round some ninety times in every one of them.
#define APU_NEXT_BYTE_ENTRY 0x80ccbfu

// What the routine leaves behind. `a` is the byte in the low half with the
// caller's own high byte still above it, the same preservation `apu_send`
// documents at length: `SEP #$20` hides the high byte rather than clearing it,
// and an 8-bit `LDA` never writes it.
typedef struct {
  uint16_t a;
  bool n, z;
} ApuNextRegs;

// Fetch one byte from the cursor and advance it. `out` may be NULL. `in_a` is
// the accumulator on entry, and is wanted only for the high byte it carries
// through — the routine's answer is in the low one.
void apu_next_byte(Wram* w, const Rom* rom, uint16_t in_a, ApuNextRegs* out);

// --- $80:CC7C  apu_load_set -------------------------------------------------
//
// **The caller of the other three, and the reason two of them are in the
// registry at all.** `apu_next_byte`'s 255,859 calls and `apu_send`'s 257,114
// are almost entirely this routine's: it points the cursor at a table entry and
// then does nothing but fetch a byte and hand it to the SPC, over and over, for
// as long as the set lasts.
//
// ## The format is two nested lengths and no header
//
//     set := block* $0000
//     block := u16 count, count bytes
//
// A count of zero ends the set — there is no size in front of it and no
// terminator beyond that zero — and each block is announced with command `$0A`
// before its bytes go out one at a time under command `$06`. Fifteen sets in
// the table at `$80:CCDE`, 1.7 to 9.3 KB each, so a call sends up to 9,300
// commands and runs for several frames.
//
// The block boundary is not a length the SPC is told. `$0A` is sent with a
// parameter that is `$1C ORA $1D` — **the two count bytes ORed together**, not
// the count and not either half of it. Any nonzero count produces a nonzero
// parameter and that is all the value can mean; it is the accumulator the `BNE`
// three instructions earlier happened to leave, spent rather than computed.
//
// ## Everything is eight bits wide, including the loop counter
//
// `$80:CC90  SEP #$30` covers the whole body, so `W_APU_BLOCK_LEFT` is a 16-bit
// count decremented as two bytes with the borrow spelled out, and the four
// pointer bytes at `W_APU_SRC` behave as `apu_next_byte` documents. Only the
// nine instructions before that `SEP` are wide, and all they do is index the
// table:
//
//     AND #$00FF : ASL : ASL : TAX
//     LDA $80CCE0,X : STA $1A     the bank, high byte and all
//     LDA $80CCDE,X : STA $18     the address
//
// so a set id is masked to a byte and scaled by four, and the table is pairs of
// words at `$80:CCDE`. **`$1A` is written 16 bits wide and read 8**, which is
// where the junk high byte `apu_send` preserves in A comes from — see the note
// on `ApuSendRegs`.
#define APU_LOAD_SET_ENTRY 0x80cc7cu

// Pairs of words: address then bank, indexed by `id * 4`.
#define APU_SET_TABLE 0x80ccdeu
// `LDX #$0A` before a block, `LDX #$06` for each byte in it.
#define APU_CMD_BLOCK 0x0a
#define APU_CMD_BYTE 0x06

// --- The uploads, traced ------------------------------------------------------
//
// For three rounds `apu_load_set` was written here and could not be checked,
// and the IPL upload at `$80:CB61` could not be written at all. Two reasons,
// either enough: every call runs for frames, so an interrupt lands in all of
// them and `verify` abandoned each one; and all they do is talk to the
// SPC700, one byte at a time and waiting on each reply, which a port that
// only writes WRAM cannot do. `port/hw.h` now lets a port wait, and `verify`
// sets an interrupt aside instead of giving up on the call
// (`CosimRoutine::through_interrupts`), so these are the whole routines, from
// entry to the instruction that leaves.
//
// Each takes the CPU as it arrived and leaves it as the ROM does at its exit,
// stack and all: the return addresses the `JSR`s push and the byte `PHP` and
// `PHA` leave are written where the ROM writes them. What it records in the
// trace is every access to the APU's ports, in order, with the runs of code
// between them named from the table below.

// The runs. The prices are `tools/cycles816.py`'s, in `routines.c`.
enum {
  // `$80:CCC8 apu_send`.
  AS_HEAD,      // SEP #$30 : LDY $1E
  AS_STORE,     // an absolute store's three fetches
  AS_INY,       // INY, and STY $2143's fetches
  AS_TAIL,      // STY $1E
  // `JSR abs` and `RTS`, around each call `apu_load_set` makes.
  APU_JSR,
  APU_RTS,
  // `$80:CCBF apu_next_byte`, by whether the byte is in fast ROM.
  NB_READ_FAST, // LDA [$18]
  NB_READ_SLOW,
  NB_STEP,      // INC $18 : BNE, taken
  NB_CARRY,     // INC $18 : BNE, not taken : INC $19
  // `$80:CC7C apu_load_set`.
  LS_HEAD,      // REP #$30 ... SEP #$30
  LS_LO,        // STA $1C
  LS_HI,        // STA $1D : ORA $1C
  LS_END,       // BNE, not taken: the set is over
  LS_BLOCK,     // BNE, taken : LDX #$0A
  LS_BYTE,      // LDX #$06
  LS_COUNT,     // LDA $1C : BNE, taken
  LS_BORROW,    // LDA $1C : BNE, not taken : DEC $1D
  LS_DEC,       // DEC $1C : LDA $1C : ORA $1D
  LS_MORE,      // BNE, taken
  LS_NEXT,      // BNE, not taken : JMP $CC92
  // `$80:CB1A apu_boot`.
  AB_HEAD,      // REP #$30 : PHB : LDA : LDX : LDY
  AB_MVN,       // one byte of `MVN $7F,$91` or `$7F,$95`
  AB_MID,       // LDA #$1A85 : LDX #$BA3D
  AB_POINT,     // PLB ... JSR $CB61
  AB_ZERO,      // LDA #$00, and STA $2140's fetches
  AB_REP,       // REP #$30
  // `$80:CB61 apu_ipl_upload`.
  IP_HEAD,      // PHP : REP : REP : LDY #0 : LDA #$BBAA
  IP_START,     // SEP #$20 : LDA #$CC : BRA
  IP_HDR,       // PHA ... STA $2142's fetches
  IP_FLAG,      // SEP #$20 : CPX #1 : LDA #0 : ROL, and STA $2141's fetches
  IP_KICK,      // ADC #$7F : PLA, and STA $2140's fetches
  IP_FIRST,     // BVS, taken ... STA $2140's fetches
  IP_NEXT,      // SEP #$20 : DEX : BNE, taken : XBA : LDA [$18],Y : INY : XBA
  IP_SEND,      // INC : REP #$20, and STA $2140's fetches
  IP_ENDB,      // SEP #$20 : DEX : BNE, not taken
  IP_ADC,       // ADC #$03
  IP_BEQ_T,     // BEQ, taken
  IP_BEQ_N,     // BEQ, not taken
  IP_DONE,      // BVS, not taken : PLP : RTS
  APU_BLOCK_COUNT
};

// The instruction each leaves by, which the core then executes.
#define APU_SEND_EXIT 0x80ccddu      // RTS
#define APU_LOAD_SET_EXIT 0x80cca0u  // RTS
#define APU_BOOT_EXIT 0x80cb60u      // RTL

// The most steps any of them records: the IPL upload's 39,546 bytes at five
// each, with room over.
#define APU_TRACE_MAX (1 << 18)

// `$80:CCC8`. The same routine as `apu_send` above, for the caller that is the
// ROM: the wait and the three stores go in the trace rather than to the hook.
void apu_send_traced(Wram* w, PortCpu* c, HwTrace* t);

// `$80:CC7C`, the set in A.
void apu_load_set_traced(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t);

// `$80:CB1A apu_boot`, and `$80:CB61 apu_ipl_upload`, its only caller's
// callee. Copies the driver out of `$91:8000` and `$95:BA3D` into `$7F:0000`
// with two `MVN`s, points `$18` at it, and hands it to the SPC700's boot ROM
// block by block: size and destination, then each byte against the counter the
// SPC echoes back. A size of zero ends it with a jump to the destination, and
// the routine clears the four ports on the way out.
void apu_boot_traced(Wram* w, const Rom* rom, PortCpu* c, HwTrace* t);

#endif
