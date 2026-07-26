#include "port/apu.h"

// The one APU. See the header for why this is a file-scope pointer and not a
// parameter.
static const ApuPorts* g_apu;

void apu_attach(const ApuPorts* ports) { g_apu = ports; }

// ---------------------------------------------------------------------------
// $80:CCC8  apu_send
// ---------------------------------------------------------------------------

void apu_send(Wram* w, uint16_t a, uint16_t x, ApuSendRegs* out) {
  // `SEP #$30 : LDY $1E`. Eight bits wide for the whole routine, so the counter
  // is one byte and it wraps at 256 — which is the whole protocol: the SPC
  // acknowledges by copying it back, and a byte is plenty because the CPU never
  // has more than one command outstanding. It is also the instruction that makes
  // the two callers' register widths stop mattering; see the header.
  uint8_t seq = wram_r8(w, W_APU_SEQ);
  uint8_t cmd = (uint8_t)x, param = (uint8_t)a;

  // `CPY $2143 : BNE` — the wait — then `STX $2142 : STA $2141 : INY : STY
  // $2143`. All four instructions are on the host's side of the line, in that
  // order, and a port with no APU attached simply skips them.
  if (g_apu && g_apu->send) g_apu->send(g_apu->ctx, seq, cmd, param);

  // `STY $1E`. The counter advances whether or not anybody was listening,
  // because it is game state and not hardware state — the ROM would have
  // written it too.
  uint8_t next = (uint8_t)(seq + 1);
  wram_w8(w, W_APU_SEQ, next);

  if (!out) return;
  // A is read by `STA $2141` and never written, so the whole 16-bit register
  // comes back exactly as it went in — high byte included, whatever it happens
  // to hold. On the uploader's path that is the high half of a pointer word left
  // over from `$80:CC84  LDA $80CCE0,X`; it is junk, and it is *preserved* junk.
  out->a = a;
  // X likewise, except that `SEP #$30` clears the high byte of an index register
  // on the way in. A caller that arrived 8 bits wide had it cleared already.
  //
  // **This mask is transcribed, not diffed, and nothing can check it.** Deleting
  // it passes all 23,632 calls on every movie, because the six call sites in the
  // ROM are `LDX #$0001`, `#$0002`, `#$0006`, `#$0008`, `#$000A` and `#$0013` —
  // every one of them a small constant whose high byte is already zero. So the
  // one place register width could still have shown through is a place no caller
  // ever puts anything. Same shape as the doubled-id index in
  // `src/port/collide.c`: not a branch, so no coverage mark can express it, and
  // the only defence is to write it down. Found by perturbation.
  out->x = x & 0xff;
  // Y is the post-increment counter, one byte wide, wrapping at 256 — and the
  // wrap is real rather than theoretical. Returning a 16-bit `seq + 1` instead
  // passes 251 calls and fails on **call 256** with `Y: ROM $0000, port $0100`,
  // which no sound effect would ever have shown: it takes an uploader sending
  // 23,820 commands in a row to go round the counter, and it goes round 93 times.
  out->y = next;
  // `INY` is genuinely the last flag-setting instruction here — unlike in
  // `apu_play_sfx`, where a `PLD` follows and takes them over — so N and Z
  // describe the new sequence counter.
  out->n = (next & 0x80) != 0;
  out->z = next == 0;
  // Carry is the wait's: `CPY $2143 : BNE` only falls through when the two are
  // equal, and equal sets carry. Set on every exit, because the SPC answered.
  out->c = true;
}

// ---------------------------------------------------------------------------
// $80:CC3B  apu_play_sfx
// ---------------------------------------------------------------------------

void apu_play_sfx(Wram* w, uint16_t id, uint16_t caller_dp, ApuSfxRegs* out) {
  // `PHD : PEA $0000 : PLD`. `apu_send` reads `$1E` on direct page zero no
  // matter who called it, which is why `W_APU_SEQ` is an address in
  // `port/wram.h` rather than an offset in `port/collide.h`: it is a global,
  // not a field of whichever thread happens to be making the noise.
  apu_send(w, id, APU_CMD_PLAY_SFX, NULL);

  // `REP #$30 : PLD : RTL`, and every one of these comes out of that.
  //
  // A survives untouched — `SEP #$30` hides its high byte rather than clearing
  // it, and the low byte is only ever read by the `STA $2141`.
  out->a = id;
  // X was `LDX #$0001` sixteen bits wide, then narrowed to 8 and widened back.
  // Narrowing an index register *clears* its high byte, so what returns is the
  // command, zero-extended — which happens to be the same word that went in.
  out->x = APU_CMD_PLAY_SFX;
  // Y is the post-increment counter, and it came back through the same
  // narrowing, so it is the byte and not a word.
  out->y = wram_r8(w, W_APU_SEQ);

  // N and Z are the `PLD`'s, not the `INY`'s. That is easy to get wrong from
  // the listing — `INY` is visibly the last arithmetic in the routine — but
  // `PLD` sets both from the 16-bit value it pulls, and it runs two
  // instructions later. So the flags a caller sees describe *its own direct
  // page*.
  out->n = (caller_dp & 0x8000) != 0;
  out->z = caller_dp == 0;
  // Carry is the wait's. `CPY $2143` leaves the loop only when the two are
  // equal, and equal sets carry; nothing after it touches C. So this is set on
  // every exit, and it is set because the SPC answered.
  out->c = true;
}
