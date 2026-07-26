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

#endif
