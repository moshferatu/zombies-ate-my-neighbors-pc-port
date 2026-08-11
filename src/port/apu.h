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
// before its bytes go out one at a time under command `$06`. Twelve calls over
// six movies, about 23,800 commands each, which is what makes this a 104,000
// instruction call and everything else in this file a leaf.
//
// The block boundary is not a length the SPC is told. `$0A` is sent with a
// parameter that is `$1C ORA $1D` — **the two count bytes ORed together**, not
// the count and not either half of it. Any nonzero count produces a nonzero
// parameter and that is all the value can mean; it is the accumulator the `BNE`
// three instructions earlier happened to leave, spent rather than computed.
// The port sends the same byte because the SPC is the other side of a hook and
// the port does not get to decide what it makes of it.
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
//
// ## It is written and it is not checked
//
// **This is the second routine in the port that the harness structurally cannot
// verify**, after `sprite_cache_init`, and for the same reason: 104,000
// instructions is roughly eight frames, and an NMI lands inside every call.
// Measured, not assumed — 5 calls and 5 interrupted on `level25-lane`, 3 and 3
// on `boot.zmv`, where the load happens with the screen off and before the
// title. There is no registry entry and no shim; `tools/native_share.py` has
// the address in `BLOCKED` so the ranking stops offering it.
//
// It would have needed `verify_only` as well if it could be registered, because
// it spins: every one of those 23,800 commands goes through `apu_send`'s
// `CPY $2143 : BNE`, so the routine's duration is the SPC700's to decide and
// not the port's. Two independent reasons, and either one alone is enough.
//
// What the code below is *for*, then, is Phase 4 rather than the harness. It is
// the bookkeeping — the cursor, the counter, the order of the bytes — written
// out where the disassembly can be checked against it by eye, and the day the
// port owns its own main loop it is the routine that loads the music.
#define APU_LOAD_SET_ENTRY 0x80cc7cu

// Pairs of words: address then bank, indexed by `id * 4`.
#define APU_SET_TABLE 0x80ccdeu
// `LDX #$0A` before a block, `LDX #$06` for each byte in it.
#define APU_CMD_BLOCK 0x0a
#define APU_CMD_BYTE 0x06

// What it leaves. A's high byte is the junk from the table read that `SEP #$20`
// hid and nothing since has written; its low byte is the zero count that ended
// the set. X is `APU_CMD_BYTE` on any set that had a block in it and the low
// byte of `id * 4` on one that did not. Y is `W_APU_SEQ` after the last command,
// one byte wide.
//
// N and Z are the final `ORA $1C`'s, so they are always clear and set — the
// routine cannot return any other way. Carry is the last `apu_send`'s, which is
// its wait's, so it is set unless no command was ever sent; on that path it is
// whatever the caller arrived with, and so is Y with its high byte cleared.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} ApuLoadRegs;

// Upload sound data set `id`. `in_y` and `in_c` are wanted only for a set whose
// very first count is zero, which sends nothing and leaves both alone. `out`
// may be NULL.
void apu_load_set(Wram* w, const Rom* rom, uint16_t id, uint16_t in_y,
                  bool in_c, ApuLoadRegs* out);

#endif
