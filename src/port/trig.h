// The game's sine, the one entry in its table that would not fit, and the only
// thing the game ever asks a sine for.
//
//   $80:9C90  sin_deg          degrees 0-359 -> sin x 128, sign-extended
//   $80:9570  wave_hdma_build  ...360 of them a frame, into an HDMA table
//
// ## What the table is
//
// `$83:9431` is **360 signed bytes**, one per whole degree, holding
// `round(sin(d) * 127.5)` or thereabouts: 0 at 0, 127 through the top of the
// arc, 0 again at 180, `$80` (-128) at 270, and back to 0 at 359. Index 360
// would be the byte after it, and there is no wrap here — the callers do the
// reduction, and the table ends where the circle does.
//
// It is a *degree* table, not the power-of-two-turn table almost every SNES
// game reaches for. That costs a division somewhere upstream and buys nothing
// the port can see; what it means here is that the index is genuinely a number
// up to 359, so **X is sixteen bits** at the `LDA $839431,X` — an eight-bit
// index could not address the second half of the circle at all. That is the
// only evidence for the register width, and it is conclusive.
//
// ## `$FF` is not -1
//
//     LDA $839431,X : CMP #$FF : BNE +      ; the sentinel
//     LDA #$0080 : BRA out                  ; ...means +128
//
// Exactly one byte of the 360 is `$FF`, at index **90**, and it is there
// because the answer is +128 and a signed byte stops at +127. Rather than scale
// the whole table down — which would cost precision on all 360 entries to buy
// the one — the ROM spends an impossible value on it and a compare on every
// call.
//
// `$FF` is *free* to spend, and the table is why. One degree of arc is
// `128 * sin(1°) = 2.23`, so the first step either side of a zero crossing is
// ±2 and **±1 never occurs anywhere in the circle**: there is no `$01` byte in
// the table and no `$FF` but the sentinel. Both extremes are present honestly —
// `$7F` fourteen times across the plateau, `$80` once at 270 — so the one value
// the encoding could not represent is the one value that is not needed, which
// is a happier accident than it looks. Values are `round(128 * sin(d))` to
// within 1 everywhere.
//
// The other end of the circle needs no trick at all. 270 wants -128 and that is
// exactly what a signed byte holds, so two's complement's asymmetry pays for one
// extreme and the sentinel pays for the other.
//
// ## The sign extension, and why the caller's junk cannot leak
//
//     BIT #$0080 : BEQ + : ORA #$FF00 : BRA out
//     + : AND #$00FF
//
// This runs *after* `REP #$20`, so A is sixteen bits with the caller's own high
// byte still sitting above the byte just loaded — `SEP #$20` hid that half, it
// did not clear it. Both arms then overwrite it unconditionally, `ORA #$FF00`
// or `AND #$00FF`, which is what makes the result a function of the table byte
// alone. Written the other way round — `AND #$00FF` first and a conditional
// `ORA` after — the same two instructions would have leaked the caller's high
// byte on the negative half of the circle.
//
// ## The flags belong to the `PHX` at the top
//
// Every path converges on `PLX : RTS`, and `PLX` sets N and Z from the value it
// pulls. So the N and Z a caller reads describe **the index register it passed
// in**, not the sine it asked for, and N is bit 15 of a number that is at most
// 359 and therefore always clear. Carry does survive, from the `CMP #$FF`, and
// so carry set means *this call hit the sentinel* — a fact about the table that
// no caller in the ROM reads and that the routine advertises anyway.
//
// ## What the game actually wants a sine for
//
// One caller, `$80:959A`, inside `$80:9570` — and it is not gameplay at all.
// The routine builds a table at `$7E:8000`, two bytes per scanline, of `$F8`
// and then a sine:
//
//     $72 = ($72 + 1) mod 360        ; a phase that advances one degree a frame
//     Y = $72
//     loop: A = #$00F8 : STA $7E8000,X : INX
//           Y += 4 (mod 360)          ; four degrees of arc per scanline
//           TYA : JSL $809C8C : ... : STA $7E8000,X
//
// That is an HDMA table — a repeat byte and a sixteen-bit parameter per entry,
// three bytes at a time — carrying a per-scanline offset, one full period of
// the wave every 90 entries and the whole thing sliding by a degree a frame.
// The whole of the game's trigonometry is a screen wobble.
//
// It is also the only thing in the corpus that calls this at all, and the
// harness makes the shape of that unmistakable — **2,676 calls, to the call, on
// every movie measured**, all of them between frames 900 and 1,200 of a
// transition that every movie passes through identically. A routine whose call
// count does not depend on the input is a routine no input is driving.
//
// ## The caller sign-extends the answer a second time
//
//     JSL $809C8C : BIT #$8000 : BEQ +2 : ORA #$FF00 : STA $7E8000,X
//
// Bit 15 is set only on the path where `sin_deg` has *already* done
// `ORA #$FF00`, so the `ORA` here can never change a bit, and on the two paths
// where it would matter bit 15 is clear and the `BEQ` skips it. Four
// instructions in the inner loop of a table build that cannot affect the
// result. They are transcribed nowhere, because they are the caller's and the
// caller is not ported — but the reason to write it down is that the redundancy
// is *evidence*: it is what a sign extension looks like when nobody was sure
// which side of the call had already done it.
//
// Port code: libc only.

#ifndef PORT_TRIG_H
#define PORT_TRIG_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define SIN_DEG_ENTRY 0x809c90u
// `$80:9C8C  JSR $9C90 : RTL` — a four-byte trampoline, and the only reason
// this routine has a far-callable name at all. The profile counts both.
#define SIN_DEG_FAR_ENTRY 0x809c8cu

// 360 signed bytes at one per degree.
#define SIN_TABLE 0x839431u
#define SIN_TABLE_ENTRIES 360
// The impossible byte, and what it stands for.
#define SIN_SENTINEL 0xff
#define SIN_SENTINEL_VALUE 0x0080

typedef struct {
  uint16_t a;
  bool n, z;
  bool c;  // set means the call landed on the sentinel — i.e. the index was 90
} SinRegs;

// `deg` is A on entry and is used whole: the routine's `TAX` runs with sixteen
// bit indices, so an out-of-range value indexes past the table rather than
// wrapping, exactly as the ROM would.
void sin_deg(const Rom* rom, uint16_t deg, uint16_t caller_x, SinRegs* out);

// --- $80:9570  wave_hdma_build — the only thing the sine is for --------------
//
// One frame of the screen wobble: advance the phase by a degree, rebuild the
// whole HDMA table from it, and — sometimes — take one scanline off the end.
//
// The caller is a thread, and its loop is four instructions:
//
//     $80:950B  LDA #$0001 : JSL thread_yield : JSR $9570
//               LDA $006E : CMP #$1000 : BEQ done      ; joy1 Start
//               LDA $0070 : CMP #$1000 : BEQ done      ; joy2 Start
//               LDA $70 : BPL -
//
// so this runs once a frame for as long as `$70` stays non-negative, with HDMA
// channel 6 already enabled (`$80:9504  LDA #$40 : STA $420C`) and switched off
// the moment the loop ends. **Either player can end it with Start**, which is
// what says the effect is a screen the player is waiting through rather than
// anything play depends on.
//
// It is the only caller, and it runs in two places that look nothing alike from
// the profile: 132 calls over the eleven profiled level movies — twelve each,
// ~223 sines apiece, the whole of `sin_deg`'s traffic and the reason that
// routine's count is identical on every input — and **1,077 calls in
// `boot.zmv` alone**, which is the title sequence holding the same wobble for
// as long as nobody presses anything.
//
// ## The table
//
// `$7E:8000` is an *indirect-less* HDMA table in repeat mode:
//
//     $8000       $F8            header: repeat, 120 lines
//     $8001..F0   120 x u16      one parameter per line
//     $80F1       $F8            header: repeat, 120 lines
//     ...
//     $8000+len   $0000          stop
//
// The build writes the first header with a sixteen-bit store of `$00F8` and
// then immediately overwrites the high half with the first parameter, which is
// how a byte-sized header ends up costing no byte-sized store. The second
// header is written the same way in reverse — `LDA #$F800 : STA $7E7FFF,X` at
// `X = $F1`, so the `$F8` lands on `$80F1` and the `$00` lands on `$80F0`,
// which is the high byte of the 120th parameter. **One scanline of the wave
// loses its sign every frame** and nobody has ever seen it: the parameter is a
// scroll offset and the line it lands on is the seam between the two headers.
//
// Four degrees of arc per scanline, so the wave has a period of 90 lines and a
// little over two and a half of them fit on a screen. The phase advances one
// degree a frame, which slides the whole thing down at 90 frames a cycle.
//
// ## How the effect ends, and why it cannot tear
//
//     ...loop over ; LDA #$0000 : STA $7E8000,X   ; the terminator
//     CMP #$0000 : BNE out                        ; A is still the last sine
//     LDA $76 : BEQ + : DEC : STA $76 : BRA out
//     + : DEC $70 : DEC $70
//
// The table only *shrinks* on a frame whose last parameter came out exactly
// zero — that is, when the bottom of the wave is on the axis — and then only
// after the hold counter `$76` has run out. So the wobble retracts two bytes,
// one scanline, at the one moment in each 90-frame cycle when removing that
// line changes nothing on screen. It is a fade-out written as a boundary
// condition.
//
// `$70` going negative is what the caller's `BPL` is waiting for, so the two
// `DEC`s are both the retraction and the clock on the whole effect.
//
// ## The caller's dead sign extension
//
// `$80:959E  BIT #$8000 : BEQ +2 : ORA #$FF00` sits between the `JSL` and the
// store and cannot change a bit — bit 15 is set only where `sin_deg` has
// already done the same `ORA`. It is transcribed here as the one line it is,
// and see the note above on why a redundancy is worth recording.
#define WAVE_HDMA_ENTRY 0x809570u

#define WAVE_DEGREES 360
// `INY` four times a scanline. The wave's period in lines is 360 / 4.
#define WAVE_DEGREES_PER_LINE 4
// `LDA #$00F8` — HDMA repeat mode, 120 lines. Stored sixteen bits wide.
#define WAVE_REPEAT_HEADER 0x00f8
// ...and again at the offset the first block's data ends at, low half first.
#define WAVE_SECOND_HEADER 0x00f1
#define WAVE_HEADER_FIXUP 0xf800

// How long the table is, in bytes, and negative when the effect is over. The
// only field the caller looks at.
#define WAVE_DP_LENGTH 0x70
#define WAVE_DP_PHASE 0x72  // degrees, 0..359, one per frame
#define WAVE_DP_HOLD 0x76   // zero crossings to sit through before retracting

typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
} WaveRegs;

// `dp` is the wobble thread's direct page. `in_c` answers for the one exit that
// does not touch carry — the `BMI` on the first instruction.
void wave_hdma_build(Wram* w, const Rom* rom, uint16_t dp, uint16_t in_x,
                     uint16_t in_y, bool in_c, WaveRegs* out);

// What one frame of the wobble did, counted by the branch the ROM would have
// taken — the same arrangement `ActorSortWork` uses, and for a sharper reason.
//
// A call here costs between about 1,200 cycles and about 163,000, and the two
// ends are not a tail: **they are two populations that do not overlap.** Every
// level movie makes exactly twelve calls, all of them within 350 cycles of
// 163,300, because the level-entry transition builds the table at its full 448
// bytes and then leaves. `boot.zmv` makes 1,077, mean 96,406, floor 1,176 — the
// title screen holding the same wobble for as long as nobody presses Start,
// while the retraction takes the table apart two bytes at a time. A single mean
// is not a poor description of that; it is a description of neither half.
//
// Almost the whole cost is `iterations x (a far call to sin_deg + two long
// stores)`, and the loop runs once per two bytes of table — 223 times at the
// full `$01C0` that `$80:94CD` starts it at — so what a call costs is how much
// of the table is left. That is a count, and this is where it is taken.
typedef enum {
  WAVE_BLK_OVER,       // $80:9572 BMI taken: the effect is finished, incl. the RTS
  WAVE_BLK_PROLOGUE,   // entry through the first header store, phase below 360
  WAVE_BLK_PROLOGUE_WRAP,  // ...and the same with `LDA #$0000` in it
  WAVE_BLK_HEAD,       // $80:958D INY x4 : CPY : BCC taken
  WAVE_BLK_HEAD_WRAP,  // ...not taken, so the arc wrapped: + `LDY #$0000`
  WAVE_BLK_ITER,       // the rest of the loop body that every iteration pays
  WAVE_BLK_SIN_SENTINEL,  // the call to $80:9C8C, by which arm of `sin_deg` it took
  WAVE_BLK_SIN_NEGATIVE,  // ...including the caller's `BIT`/`BEQ`/`ORA`, which
  WAVE_BLK_SIN_POSITIVE,  //    is decided by the same bit the arm is
  WAVE_BLK_HEADER_SKIP,   // $80:95AF BNE taken: not the seam
  WAVE_BLK_HEADER_FIXUP,  // ...not taken: the second repeat header goes in
  WAVE_BLK_STEP,          // $80:95BB CPX $70 : BCC taken: another scanline
  WAVE_BLK_EXIT,          // ...not taken: the table is as long as it is
  WAVE_BLK_OFF_AXIS,      // the three exits, each including its own `RTS`
  WAVE_BLK_HOLD,
  WAVE_BLK_RETRACT,
  WAVE_BLOCK_COUNT,
} WaveBlock;

// `HEAD` + `HEAD_WRAP`, the three `SIN`s, `HEADER_SKIP` + `HEADER_FIXUP` and
// `STEP` + `EXIT` all number the iterations, and `ITER` does too — five ways of
// counting the same loop, which is what makes a mistake in any of them visible.
// Exactly one of `OVER`, `OFF_AXIS`, `HOLD` and `RETRACT` is set on any call.
typedef struct {
  uint16_t blocks[WAVE_BLOCK_COUNT];
} WaveWork;

// The same frame, reporting what it did. `wave_hdma_build` is this with the
// counts thrown away, and is what the rest of the port calls.
void wave_hdma_build_counted(Wram* w, const Rom* rom, uint16_t dp,
                             uint16_t in_x, uint16_t in_y, bool in_c,
                             WaveRegs* out, WaveWork* work);

// --- $80:9515  the wobble's thread, after each build -------------------------
//
// `$80:94AF` is the thread that holds the wobble up. Each frame it calls
// `wave_hdma_build`, and then asks three things: is Start down on the first
// pad and nothing else, is it on the second, and has the table's length gone
// negative. Any of them ends the wobble at `$80:9529`. Otherwise it sleeps a
// frame.
//
// This is those tests, from where the build returns to. The call is a
// stretch of its own, the one instruction, and ends where the build begins.
// The pads are the NMI's to write and a build is long enough for an NMI to
// land in: one stretch from before the build to after the tests would have
// read them too early. Three do not. The build is in the middle, taking its
// own time, and the tests begin when it has returned.
#define WAVE_THREAD_CALL_PC 0x809512u  // `JSR $9570`
#define WAVE_THREAD_TESTS_PC 0x809515u
#define WAVE_THREAD_YIELD_PC 0x80950eu  // `JSL thread_yield`, A already 1
#define WAVE_THREAD_END_PC 0x809529u
#define WAVE_THREAD_YIELD_TICKS 1
// The two pads as they were read this frame, a word each: `W_JOY_RAW` in
// `port/player.h`.
#define W_WAVE_PADS 0x006eu
#define WAVE_PAD_START 0x1000u

enum {
  WT_PAD,     // $9515-$951C, and the same again at $951D-$9524
  WT_LENGTH,  // $9525-$9528
  WT_AGAIN,   // $950B-$950D
  WT_TAKEN,   // a branch taken
  WT_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[WT_BLOCK_COUNT];
} WaveThreadWork;

void wave_thread_tests(const Wram* w, PortCpu* c, WaveThreadWork* k);
// `$80:9512`: the call, as far as the build's first instruction.
void wave_thread_call(Wram* w, PortCpu* c);

#endif
