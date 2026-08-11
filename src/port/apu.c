#include "port/apu.h"

#include "port/coverage.h"

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

// ---------------------------------------------------------------------------
// $80:CCBF  apu_next_byte
// ---------------------------------------------------------------------------

void apu_next_byte(Wram* w, const Rom* rom, uint16_t in_a, ApuNextRegs* out) {
  // `LDA [$18]`. Three bytes on direct page zero, and the bank is the third —
  // read but never written, which is what makes the wrap below a wrap.
  uint16_t lo = wram_r8(w, W_APU_SRC);
  uint16_t hi = wram_r8(w, W_APU_SRC + 1);
  uint8_t bank = (uint8_t)wram_r8(w, W_APU_SRC_BANK);
  uint32_t at = ((uint32_t)bank << 16) | (uint32_t)((hi << 8) | lo);

  uint32_t avail = 0;
  const uint8_t* p = rom_ptr(rom, at, &avail);
  // Every set the uploader walks lives in ROM, so this is the only reading
  // this routine can do. A pointer the host could not resolve reads as zero
  // rather than trapping, which is what `rom_word` does two files over.
  uint8_t byte = (p && avail) ? *p : 0;

  // `INC $18 : BNE +2 : INC $19` — eight bits at a time, so the carry is a
  // branch and the bank is out of reach. Whether the second `INC` runs is the
  // only decision in the routine and it decides the flags, so it is a site.
  lo = (uint16_t)((lo + 1) & 0xffu);
  wram_w8(w, W_APU_SRC, (uint8_t)lo);
  uint16_t last = lo;
  if (lo == 0) {
    PORT_COVER(apu_src_wrap);
    hi = (uint16_t)((hi + 1) & 0xffu);
    wram_w8(w, W_APU_SRC + 1, (uint8_t)hi);
    last = hi;
  } else {
    PORT_COVER(apu_src_step);
  }

  if (!out) return;
  // The high byte is the caller's, untouched: `SEP #$30` hid it and the 8-bit
  // `LDA` could not have written it. Same shape as `apu_send`'s A.
  out->a = (uint16_t)((in_a & 0xff00u) | byte);
  // ...and N and Z are the surviving `INC`'s, which is the cursor rather than
  // the byte. On the wrapping call `INC $18` set Z, and then `INC $19` took the
  // flags straight back off it.
  out->n = (last & 0x80u) != 0;
  out->z = last == 0;
}

// ---------------------------------------------------------------------------
// $80:CC7C  apu_load_set
// ---------------------------------------------------------------------------

// One byte from the cursor into the accumulator, keeping the high half the
// table read left there. All three `JSR $CCBF` sites in this routine do exactly
// this and throw the flags away — nothing here ever reads them.
static uint16_t apu_fetch(Wram* w, const Rom* rom, uint16_t acc) {
  ApuNextRegs n;
  apu_next_byte(w, rom, acc, &n);
  return n.a;
}

void apu_load_set(Wram* w, const Rom* rom, uint16_t id, uint16_t in_y,
                  bool in_c, ApuLoadRegs* out) {
  // The nine instructions before `SEP #$30`, and the only wide ones.
  const uint16_t index = (uint16_t)((id & 0x00ffu) * 4u);
  wram_w16(w, W_APU_SRC_BANK, rom_word(rom, APU_SET_TABLE + 2u + index));
  const uint16_t src = rom_word(rom, APU_SET_TABLE + index);
  wram_w16(w, W_APU_SRC, src);

  // From here everything is a byte. The accumulator's hidden high half is the
  // one thing that is not, and it is the address word's — see the header.
  uint16_t acc = src;
  // `TAX` happened while X was wide; `SEP #$30` then cleared its high byte.
  uint16_t x = (uint16_t)(index & 0xffu);
  bool sent = false;

  for (;;) {
    acc = apu_fetch(w, rom, acc);
    wram_w8(w, W_APU_BLOCK_LEFT, (uint8_t)acc);
    acc = apu_fetch(w, rom, acc);
    wram_w8(w, W_APU_BLOCK_LEFT + 1, (uint8_t)acc);

    // `ORA $1C` — the two count bytes folded into one, which is both the test
    // and, two instructions later, the parameter the SPC is given.
    const uint8_t either =
        (uint8_t)(wram_r8(w, W_APU_BLOCK_LEFT) | wram_r8(w, W_APU_BLOCK_LEFT + 1));
    acc = (uint16_t)((acc & 0xff00u) | either);
    // The `$0000` that ends the set, and the only way this routine returns.
    // No `PORT_COVER` here or below: see the note in `port/coverage.h`.
    if (either == 0) break;
    // ...or a nonzero count, so command $0A goes out and that many bytes follow.

    x = APU_CMD_BLOCK;
    apu_send(w, acc, x, NULL);
    sent = true;

    for (;;) {
      acc = apu_fetch(w, rom, acc);
      x = APU_CMD_BYTE;
      apu_send(w, acc, x, NULL);

      // `LDA $1C : BNE +2 : DEC $1D : + DEC $1C`. The load is what makes the
      // borrow visible, and it is also what puts the count's low byte in A —
      // where it stays until the next fetch overwrites it.
      uint8_t left = wram_r8(w, W_APU_BLOCK_LEFT);
      acc = (uint16_t)((acc & 0xff00u) | left);
      // Zero low byte: the high one is decremented first. Otherwise `DEC $1C`
      // alone. That is the 16-bit borrow, and it is the branch that would have
      // been the interesting site.
      if (left == 0)
        wram_w8(w, W_APU_BLOCK_LEFT + 1,
                (uint8_t)(wram_r8(w, W_APU_BLOCK_LEFT + 1) - 1u));
      left = (uint8_t)(left - 1u);
      wram_w8(w, W_APU_BLOCK_LEFT, left);

      const uint8_t rest = (uint8_t)(left | wram_r8(w, W_APU_BLOCK_LEFT + 1));
      acc = (uint16_t)((acc & 0xff00u) | rest);
      if (rest == 0) break;
    }
  }

  if (!out) return;
  // The low byte is the zero that ended the set; the high byte has not been
  // written since the table read.
  out->a = (uint16_t)(acc & 0xff00u);
  out->x = x;
  // `STY $1E` is the last thing `apu_send` does with it, so after any command
  // at all Y and the counter are the same byte.
  out->y = sent ? (uint16_t)wram_r8(w, W_APU_SEQ) : (uint16_t)(in_y & 0xffu);
  out->n = false;
  out->z = true;
  out->c = sent ? true : in_c;
}
