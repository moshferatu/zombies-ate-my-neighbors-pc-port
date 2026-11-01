// The cast after level 48 -- see port/cast.h.

#include "port/cast.h"

#include "port/coverage.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  CastWork* k;
  uint8_t bank;  // the code's
  int taken_run;
} Cast;

// A block hands back the address it goes to next, in its bank, or `STOP`
// and the instruction it stopped on.
#define STOP 0x01000000u

// --- Its words ---------------------------------------------------------------

#define W_BRIGHTNESS 0x136cu
#define W_FRAME 0x0008u          // copied into a record as it is made
#define W_COLOURS_WAIT 0x0046u   // the colours' job: frames to the next set
#define W_COLOURS_SET 0x0048u    // ...and which of six
#define W_FIXED_COLOUR 0x1370u   // what the storm's job sends to `$2132`
#define W_PAD_A 0x006eu
#define W_PAD_B 0x0070u
#define PAD_START 0x1000u

// The screen's page.
#define DP_TEXT 0x00             // a far address: where the count is written
#define DP_LIMIT 0x04            // the count's end
#define DP_KIND 0x06             // which of the eighteen
#define DP_RECORD 0x08           // the record that shows them
#define DP_COUNT 0x16

// A record's words.
#define REC_FLAGS 0x00
#define REC_X 0x02
#define REC_X_FRACTION 0x04
#define REC_Y 0x06
#define REC_PICTURES 0x08        // a far address
#define REC_PICTURES_BANK 0x0a
#define REC_BORN 0x0c
#define REC_0E 0x0e
#define REC_PALETTE 0x10

#define COUNT_TEXT 0x0038u       // in bank 0: the count's line, as words
#define COUNT_MOST 0x03e7u       // 999

// Its tables, in bank `$80`.
#define KIND_PICTURES 0x8e9au    // a far address a kind, four bytes apart
#define KIND_TALLY 0x8ee6u       // where each kind's kills are kept
#define KIND_END_PICTURES 0x8f0au  // and the pictures it ends on, with a bank
#define COLOURS_AT 0xa2e2u       // the six sets' offsets...
#define COLOURS 0xa2eeu          // ...into these

// The storm's, in bank `$82`.
#define STORM_PALETTES 0xe0a4u

// --- The machine, as these blocks use it ------------------------------------

static uint16_t field(const Cast* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->c->d + at));
}

static void set_field(Cast* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->c->d + at), v);
}

// A word read through the data bank: a table of its own, or low memory.
static uint16_t data(const Cast* s, uint16_t at) {
  return bus_r16(s->w, s->rom, (uint32_t)s->c->db << 16 | at);
}

// ...and one written, which is always to low memory.
static void set_data(Cast* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, at, v);
}

static void ran(Cast* s, int run) { hw_run(&s->k->trace, run); }
static void taken(Cast* s) { hw_run(&s->k->trace, s->taken_run); }
static void write_reg(Cast* s, uint16_t reg, uint8_t v) {
  hw_w8(&s->k->trace, reg, v);
}

static bool zero(const Cast* s) { return flag(s->c, PORT_P_Z); }
static bool carry(const Cast* s) { return flag(s->c, PORT_P_C); }
static bool minus(const Cast* s) { return flag(s->c, PORT_P_N); }

static void lda(Cast* s, uint16_t v) {
  s->c->a = v;
  set_nz16(s->c, v);
}

// `LDA #` with an 8-bit accumulator: the top byte stays.
static void lda8(Cast* s, uint8_t v) {
  s->c->a = (uint16_t)((s->c->a & 0xff00u) | v);
  set_nz8(s->c, v);
}

static void ldx(Cast* s, uint16_t v) {
  s->c->x = v;
  set_nz16(s->c, v);
}

static void ldy(Cast* s, uint16_t v) {
  s->c->y = v;
  set_nz16(s->c, v);
}

static void doubled(Cast* s) { s->c->a = asl16(s->c, s->c->a); }
static void cmp(Cast* s, uint16_t v) { cmp16(s->c, s->c->a, v); }

// `CLC : ADC #v`.
static void add(Cast* s, uint16_t v) {
  set_c(s->c, false);
  s->c->a = adc16(s->c, s->c->a, v);
}

// `INC` of a word on the page.
static void count_up(Cast* s, uint16_t at) {
  const uint16_t v = (uint16_t)(field(s, at) + 1);
  set_field(s, at, v);
  set_nz16(s->c, v);
}

// A `JSR` at `at`, going to `to`.
static uint32_t jsr(Cast* s, uint16_t at, uint16_t to) {
  push16(s->w, s->c, (uint16_t)(at + 2));
  return to;
}

// An `RTS`: where it goes is what it pulls, and one on.
static uint32_t rts(Cast* s) { return (uint16_t)(pull16(s->w, s->c) + 1); }

static uint32_t stop(const Cast* s, uint16_t at) {
  return STOP | (uint32_t)s->bank << 16 | at;
}

// `STA [$00] : INC $00`: a word of the count's line, and on a byte. The
// pointer is always the line in bank 0, which is low memory.
static void put(Cast* s, uint16_t v) {
  const uint32_t at = (uint32_t)wram_r8(s->w, (uint16_t)(s->c->d + DP_TEXT + 2))
                          << 16 |
                      field(s, DP_TEXT);
  if ((at & 0x400000u) || (at & 0xffffu) >= 0x1fffu) {
    s->k->lost = true;  // somewhere this file does not write
    return;
  }
  wram_w16(s->w, at & 0xffffu, v);
}

static void put_next(Cast* s) { count_up(s, DP_TEXT); }

// `STA $4204 : SEP #$20 : LDA #by : STA $4206`, and the quotient and the
// remainder read back after the wait.
static uint16_t divide(Cast* s, int to_dividend, int to_divisor, uint8_t by,
                       uint16_t* remainder) {
  const uint16_t dividend = s->c->a;
  ran(s, to_dividend);
  write_reg(s, 0x4204, (uint8_t)dividend);
  write_reg(s, 0x4205, (uint8_t)(dividend >> 8));
  lda8(s, by);
  ran(s, to_divisor);
  write_reg(s, 0x4206, by);
  *remainder = (uint16_t)(dividend % by);
  return (uint16_t)(dividend / by);
}

// --- The screen and its job --------------------------------------------------

static uint32_t screen_block(Cast* s, uint16_t pc) {
  PortCpu* c = s->c;
  switch (pc) {
    case 0x8bbb:
      PORT_COVER(cast_begins);
      lda(s, (uint16_t)(data(s, W_BRIGHTNESS) | 0x0080u));
      set_data(s, W_BRIGHTNESS, c->a);
      ran(s, CAST_RUN_8BBB);
      return stop(s, 0x8bc4);

    // `JSL $82:ACF0`, and the big letters' colours it sends first.
    case 0x8bd5:
      push8(s->w, c, CAST_BANK);
      push16(s->w, c, 0x8bd8);
      ran(s, CAST_RUN_8BD5);
      c->a = 0x0083;
      c->x = 0xf08c;
      ldy(s, 0x0100);
      ran(s, CAST_RUN_ACF0);
      return STOP | 0x82acf9u;

    case 0x8be5:
      c->a = 0x8000;
      ldy(s, 0x0084);
      ran(s, CAST_RUN_8BE5);
      return stop(s, 0x8beb);

    // `$80:8D24`: the record that shows each kind.
    case 0x8bf3:
      ran(s, CAST_RUN_8BF3);
      return jsr(s, 0x8bf3, 0x8d24);
    case 0x8d24:
      return stop(s, 0x8d24);
    case 0x8d28: {
      PORT_COVER(cast_record_made);
      set_field(s, DP_RECORD, c->a);
      const uint16_t y = c->y;
      set_data(s, (uint16_t)(y + REC_X), 0x0050);
      set_data(s, (uint16_t)(y + REC_X_FRACTION), 0);
      set_data(s, (uint16_t)(y + REC_Y), 0x00b4);
      set_data(s, (uint16_t)(y + REC_PICTURES), 0);
      set_data(s, (uint16_t)(y + REC_PICTURES_BANK), 0);
      set_data(s, (uint16_t)(y + REC_0E), 0);
      lda(s, data(s, W_FRAME));
      set_data(s, (uint16_t)(y + REC_BORN), c->a);
      lda(s, (uint16_t)(0xc001u | data(s, (uint16_t)(y + REC_FLAGS))));
      set_data(s, (uint16_t)(y + REC_FLAGS), c->a);
      lda(s, 0);
      ldy(s, 0);  // `TAY`
      ran(s, CAST_RUN_8D28);
      return stop(s, 0x8d5b);
    }
    case 0x8d5f:
      ran(s, CAST_RUN_8D5F);
      return rts(s);

    // `$80:8D10`: the layers, and the big letters.
    case 0x8bf6:
      ran(s, CAST_RUN_8BF6);
      return jsr(s, 0x8bf6, 0x8d10);
    case 0x8d10:
      lda8(s, 0x11);
      ran(s, CAST_RUN_8D10);
      write_reg(s, 0x212c, 0x11);
      c->a = 0x0080;
      ldx(s, 0x8e5e);
      ran(s, CAST_RUN_8D17);
      return stop(s, 0x8d1f);
    case 0x8d23:
      ran(s, CAST_RUN_8D23);
      return rts(s);

    // `$80:8CCD`: the storm's five threads, and the colours' job.
    case 0x8bf9:
      ran(s, CAST_RUN_8BF9);
      return jsr(s, 0x8bf9, 0x8ccd);
    case 0x8ccd:
    case 0x8cd7:
    case 0x8ce1:
    case 0x8ceb:
    case 0x8cf5: {
      static const struct {
        uint16_t at;
        CastRun run;
      } STORMS[] = {{0x8ccd, CAST_RUN_8CCD}, {0x8cd7, CAST_RUN_8CD7},
                    {0x8ce1, CAST_RUN_8CE1}, {0x8ceb, CAST_RUN_8CEB},
                    {0x8cf5, CAST_RUN_8CF5}};
      int i = 0;
      while (STORMS[i].at != pc) i++;
      if (i == 0) PORT_COVER(cast_storm_asked);
      c->a = (uint16_t)CAST_STORM_PC;
      ldy(s, CAST_STORM_BANK);
      ran(s, STORMS[i].run);
      return stop(s, (uint16_t)(pc + 6));
    }
    case 0x8cff:
      set_data(s, W_COLOURS_WAIT, 0);
      set_data(s, W_COLOURS_SET, 0);
      c->a = 0xa2ab;
      ldy(s, CAST_BANK);
      ran(s, CAST_RUN_8CFF);
      return stop(s, 0x8d0b);
    case 0x8d0f:
      ran(s, CAST_RUN_8D0F);
      return rts(s);
    case 0x8bfc:
      return stop(s, 0x8bfc);

    // `$80:8C04`: the roll call.
    case 0x8c00:
      PORT_COVER(cast_faded_in);
      ran(s, CAST_RUN_8C00);
      return jsr(s, 0x8c00, 0x8c04);
    case 0x8c04:
      lda(s, 0);
      set_field(s, DP_KIND, 0);
      ran(s, CAST_RUN_8C04);
      return 0x8c09;
    case 0x8c09:
      ran(s, CAST_RUN_8C09);
      return jsr(s, 0x8c09, 0x8e29);
    case 0x8e29:
      c->a = 0x0080;
      c->x = 0x8e84;
      ldy(s, 0);
      ran(s, CAST_RUN_8E29);
      return stop(s, 0x8e32);
    case 0x8e36:
      ran(s, CAST_RUN_8E36);
      return rts(s);
    case 0x8c0c:
      ran(s, CAST_RUN_8C0C);
      return jsr(s, 0x8c0c, 0x8e06);
    case 0x8e06:
      c->a = 0x0080;
      c->x = 0x8e70;
      ldy(s, 0);
      ran(s, CAST_RUN_8E06);
      return stop(s, 0x8e0f);
    case 0x8e13:
      ran(s, CAST_RUN_8E13);
      return rts(s);
    case 0x8c0f:
      ran(s, CAST_RUN_8C0F);
      return jsr(s, 0x8c0f, 0x8dea);
    case 0x8dea:
      c->a = 0x0080;
      c->x = 0x8e92;
      ldy(s, 0);
      ran(s, CAST_RUN_8DEA);
      return stop(s, 0x8df3);
    case 0x8df7:
      ran(s, CAST_RUN_8DF7);
      return rts(s);
    case 0x8c12:
      ldx(s, field(s, DP_RECORD));
      set_data(s, (uint16_t)(c->x + REC_PICTURES), 0);
      set_data(s, (uint16_t)(c->x + REC_PICTURES_BANK), 0);
      lda(s, 0x001e);
      ran(s, CAST_RUN_8C12);
      return stop(s, 0x8c1d);

    // The kind's pictures given to the record. A bank of nought ends it.
    case 0x8c21:
      lda(s, field(s, DP_KIND));
      doubled(s);
      doubled(s);
      ldx(s, c->a);
      ldy(s, field(s, DP_RECORD));
      lda(s, data(s, (uint16_t)(KIND_PICTURES + c->x)));
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      lda(s, data(s, (uint16_t)(KIND_PICTURES + 2 + c->x)));
      set_data(s, (uint16_t)(c->y + REC_PICTURES_BANK), c->a);
      ran(s, CAST_RUN_8C21);
      if (zero(s)) {
        PORT_COVER(cast_last_turn);
        taken(s);
        return 0x8c93;
      }
      PORT_COVER(cast_turn);
      lda(s, 0x003c);
      ran(s, CAST_RUN_8C36);
      return stop(s, 0x8c39);
    case 0x8c3d:
      ran(s, CAST_RUN_8C3D);
      return jsr(s, 0x8c3d, 0x8e14);
    case 0x8e14:
      lda(s, 0x0010);
      ran(s, CAST_RUN_8E14);
      return stop(s, 0x8e17);
    case 0x8e1b:
      c->a = 0x0080;
      c->x = 0x8e76;
      ldy(s, 0);
      ran(s, CAST_RUN_8E1B);
      return stop(s, 0x8e24);
    case 0x8e28:
      ran(s, CAST_RUN_8E28);
      return rts(s);
    case 0x8c40:
      lda(s, 0x0014);
      ran(s, CAST_RUN_8C40);
      return stop(s, 0x8c43);
    case 0x8c47:
      ran(s, CAST_RUN_8C47);
      return jsr(s, 0x8c47, 0x8df8);
    case 0x8df8:
      c->a = 0x0080;
      c->x = 0x8e6a;
      ldy(s, 0);
      ran(s, CAST_RUN_8DF8);
      return stop(s, 0x8e01);
    case 0x8e05:
      ran(s, CAST_RUN_8E05);
      return rts(s);

    // How many of the kind were killed, as far as 999.
    case 0x8c4a:
      lda(s, field(s, DP_KIND));
      count_up(s, DP_KIND);
      doubled(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(KIND_TALLY + c->x)));
      ldx(s, c->a);
      lda(s, data(s, c->x));
      cmp(s, COUNT_MOST + 1);
      ran(s, CAST_RUN_8C4A);
      if (!carry(s)) {
        taken(s);
        return 0x8c5f;
      }
      PORT_COVER(cast_count_most);
      lda(s, COUNT_MOST);
      ran(s, CAST_RUN_8C5C);
      return 0x8c5f;
    case 0x8c5f:
      set_field(s, DP_LIMIT, c->a);
      set_field(s, DP_COUNT, 0);
      ran(s, CAST_RUN_8C5F);
      return 0x8c63;
    case 0x8c63:
      lda(s, field(s, DP_COUNT));
      ldx(s, 0x0011);
      ldy(s, 0x0013);
      ran(s, CAST_RUN_8C63);
      return jsr(s, 0x8c6b, 0x8d60);

    // `$80:8D60`: the count written at X and Y, its digits with no noughts
    // in front.
    case 0x8d60: {
      push16(s->w, c, c->a);
      lda(s, 0);
      set_nz8(c, 0);  // `XBA`
      set_field(s, DP_TEXT + 1, 0);
      lda(s, COUNT_TEXT);
      set_field(s, DP_TEXT, c->a);
      lda(s, c->x);
      put(s, c->a);
      put_next(s);
      lda(s, c->y);
      put(s, c->a);
      put_next(s);
      lda(s, 0x0006);
      put(s, c->a);
      put_next(s);
      lda(s, 0);
      put(s, c->a);
      put_next(s);
      lda(s, pull16(s->w, c));
      cmp(s, 10);
      ran(s, CAST_RUN_8D60);
      if (!carry(s)) {
        PORT_COVER(cast_count_digit);
        taken(s);
        return 0x8dcf;
      }
      cmp(s, 100);
      ran(s, CAST_RUN_8D8A);
      if (!carry(s)) {
        PORT_COVER(cast_count_digits);
        taken(s);
        return 0x8daf;
      }
      PORT_COVER(cast_count_hundreds);
      return 0x8d8f;
    }
    case 0x8d8f:
    case 0x8daf: {
      const bool hundreds = pc == 0x8d8f;
      uint16_t remainder = 0;
      const uint16_t quotient =
          divide(s, hundreds ? CAST_RUN_8D8F : CAST_RUN_8DAF,
                 hundreds ? CAST_RUN_8D92 : CAST_RUN_8DB2,
                 hundreds ? 100 : 10, &remainder);
      lda(s, quotient);
      add(s, '0');
      put(s, c->a);
      put_next(s);
      lda(s, remainder);
      ran(s, hundreds ? CAST_RUN_8D99 : CAST_RUN_8DB9);
      return hundreds ? 0x8daf : 0x8dcf;
    }
    case 0x8dcf:
      add(s, '0');
      put(s, c->a);
      put_next(s);
      lda(s, 0);
      put(s, c->a);
      c->a = 0;
      c->x = COUNT_TEXT;
      ldy(s, 0);
      ran(s, CAST_RUN_8DCF);
      return stop(s, 0x8de5);
    case 0x8de9:
      ran(s, CAST_RUN_8DE9);
      return rts(s);

    // A step up, with its sound, or the count's end.
    case 0x8c6e:
      lda(s, field(s, DP_COUNT));
      cmp(s, field(s, DP_LIMIT));
      ran(s, CAST_RUN_8C6E);
      if (carry(s)) {
        taken(s);
        return 0x8c86;
      }
      PORT_COVER(cast_counted);
      count_up(s, DP_COUNT);
      lda(s, 0x000e);
      ran(s, CAST_RUN_8C74);
      return stop(s, 0x8c79);
    case 0x8c7d:
      lda(s, 0x0005);
      ran(s, CAST_RUN_8C7D);
      return stop(s, 0x8c80);
    case 0x8c84:
      ran(s, CAST_RUN_8C84);
      taken(s);
      return 0x8c63;

    // `$80:8E37`: the kind's last pictures, with sound 6.
    case 0x8c86:
      ran(s, CAST_RUN_8C86);
      return jsr(s, 0x8c86, 0x8e37);
    case 0x8e37:
      lda(s, 0x0006);
      ran(s, CAST_RUN_8E37);
      return stop(s, 0x8e3a);
    case 0x8e3e:
      lda(s, (uint16_t)(field(s, DP_KIND) - 1));
      doubled(s);
      doubled(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(KIND_END_PICTURES + 2 + c->x)));
      ran(s, CAST_RUN_8E3E);
      if (zero(s)) {
        PORT_COVER(cast_end_same_bank);
        taken(s);
        return 0x8e4e;
      }
      PORT_COVER(cast_end_bank);
      ldy(s, field(s, DP_RECORD));
      set_data(s, (uint16_t)(c->y + REC_PICTURES_BANK), c->a);
      ran(s, CAST_RUN_8E49);
      return 0x8e4e;
    case 0x8e4e:
      lda(s, data(s, (uint16_t)(KIND_END_PICTURES + c->x)));
      ran(s, CAST_RUN_8E4E);
      return stop(s, 0x8e51);
    case 0x8e55:
      ldx(s, field(s, DP_RECORD));
      set_data(s, (uint16_t)(c->x + REC_PICTURES), 0);
      set_data(s, (uint16_t)(c->x + REC_PICTURES_BANK), 0);
      ran(s, CAST_RUN_8E55);
      return rts(s);
    case 0x8c89:
      lda(s, 0x0078);
      ran(s, CAST_RUN_8C89);
      return stop(s, 0x8c8c);
    case 0x8c90:
      ran(s, CAST_RUN_8C90);
      return 0x8c09;

    // Start on either pad, and nothing with it.
    case 0x8c93:
      lda(s, 0x0001);
      ran(s, CAST_RUN_8C93);
      return stop(s, 0x8c96);
    case 0x8c9a:
      lda(s, data(s, W_PAD_A));
      cmp(s, PAD_START);
      ran(s, CAST_RUN_8C9A);
      if (zero(s)) {
        PORT_COVER(cast_start_a);
        taken(s);
        return 0x8caa;
      }
      lda(s, data(s, W_PAD_B));
      cmp(s, PAD_START);
      ran(s, CAST_RUN_8CA2);
      if (!zero(s)) {
        PORT_COVER(cast_waits);
        taken(s);
        return 0x8c93;
      }
      PORT_COVER(cast_start_b);
      return 0x8caa;
    case 0x8caa:
      return stop(s, 0x8caa);

    // Faded out: the colour maths as a level has it, and back.
    case 0x8cae:
      PORT_COVER(cast_ends);
      lda8(s, 0xa0);
      ran(s, CAST_RUN_8CAE);
      write_reg(s, 0x2125, 0xa0);
      lda8(s, 0x10);
      ran(s, CAST_RUN_8CB5);
      write_reg(s, 0x2130, 0x10);
      lda8(s, 0xa2);
      ran(s, CAST_RUN_8CBA);
      write_reg(s, 0x2131, 0xa2);
      lda8(s, 0xe5);
      ran(s, CAST_RUN_8CBF);
      write_reg(s, 0x2132, 0xe5);
      wram_w8(s->w, W_FIXED_COLOUR, 0);
      ran(s, CAST_RUN_8CC4);
      write_reg(s, 0x2132, 0);
      ran(s, CAST_RUN_8CCA);
      return rts(s);
    case 0x8c03:
      return stop(s, 0x8c03);

    // --- `$80:A2AB`, the colours' job ---------------------------------------
    case 0xa2ab: {
      const uint16_t wait = (uint16_t)(data(s, W_COLOURS_WAIT) - 1);
      set_data(s, W_COLOURS_WAIT, wait);
      set_nz16(c, wait);
      ran(s, CAST_RUN_A2AB);
      if (!minus(s)) {
        PORT_COVER(cast_colours_wait);
        taken(s);
        return 0xa2e0;
      }
      lda(s, 0x000a);
      set_data(s, W_COLOURS_WAIT, c->a);
      const uint16_t set = (uint16_t)(data(s, W_COLOURS_SET) + 1);
      set_data(s, W_COLOURS_SET, set);
      lda(s, set);
      cmp(s, 6);
      ran(s, CAST_RUN_A2B0);
      if (!carry(s)) {
        PORT_COVER(cast_colours_turn);
        taken(s);
        return 0xa2c7;
      }
      PORT_COVER(cast_colours_wrap);
      lda(s, 0);
      set_data(s, W_COLOURS_SET, 0);
      ran(s, CAST_RUN_A2C1);
      return 0xa2c7;
    }
    case 0xa2c7:
      doubled(s);
      ldx(s, c->a);
      lda(s, 0x0020);
      push16(s->w, c, c->a);
      lda(s, data(s, (uint16_t)(COLOURS_AT + c->x)));
      add(s, COLOURS);
      ldx(s, c->a);
      c->a = CAST_BANK;
      ldy(s, 0x0020);
      ran(s, CAST_RUN_A2C7);
      return stop(s, 0xa2db);
    case 0xa2df:
      lda(s, pull16(s->w, c));
      set_c(c, true);
      ran(s, CAST_RUN_A2DF);
      return stop(s, 0xa2e1);
    case 0xa2e0:
      set_c(c, true);
      ran(s, CAST_RUN_A2E0);
      return stop(s, 0xa2e1);
  }
  s->k->lost = true;
  return stop(s, pc);
}

// --- The storm and its job ---------------------------------------------------

static uint32_t storm_block(Cast* s, uint16_t pc) {
  PortCpu* c = s->c;
  switch (pc) {
    case 0xdf6f:
      lda(s, c->a & 0x003f);
      ran(s, CAST_STORM_RUN_DF6F);
      return stop(s, 0xdf72);
    case 0xdf7a:
      set_field(s, DP_RECORD, c->a);
      ran(s, CAST_STORM_RUN_DF7A);
      return 0xdf7c;
    case 0xdf7c:
      ldy(s, field(s, DP_RECORD));
      ran(s, CAST_STORM_RUN_DF7C);
      return stop(s, 0xdf7e);

    // Across, but not in the middle, where the monster is.
    case 0xdf82:
      cmp(s, 0x0050);
      ran(s, CAST_STORM_RUN_DF82);
      if (!carry(s)) {
        taken(s);
        return 0xdf8e;
      }
      cmp(s, 0x00a0);
      ran(s, CAST_STORM_RUN_DF87);
      if (carry(s)) {
        taken(s);
        return 0xdf8e;
      }
      PORT_COVER(cast_storm_across_again);
      ran(s, CAST_STORM_RUN_DF8C);
      taken(s);
      return stop(s, 0xdf7e);
    case 0xdf8e:
      set_data(s, (uint16_t)(c->y + REC_X), c->a);
      lda(s, 0);
      set_data(s, (uint16_t)(c->y + REC_X_FRACTION), 0);
      ran(s, CAST_STORM_RUN_DF8E);
      return stop(s, 0xdf97);
    case 0xdf9b:
      cmp(s, 0x008c);
      ran(s, CAST_STORM_RUN_DF9B);
      if (carry(s)) {
        PORT_COVER(cast_storm_down_again);
        taken(s);
        return stop(s, 0xdf97);
      }
      set_data(s, (uint16_t)(c->y + REC_Y), c->a);
      lda(s, 0);
      set_data(s, (uint16_t)(c->y + REC_PICTURES), 0);
      lda(s, 0x008f);
      set_data(s, (uint16_t)(c->y + REC_PICTURES_BANK), c->a);
      lda(s, data(s, W_FRAME));
      set_data(s, (uint16_t)(c->y + REC_BORN), c->a);
      lda(s, 0);
      set_data(s, (uint16_t)(c->y + REC_0E), 0);
      ran(s, CAST_STORM_RUN_DFA0);
      return stop(s, 0xdfbb);
    case 0xdfbf:
      lda(s, c->a & 0x0007);
      doubled(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(STORM_PALETTES + c->x)));
      set_data(s, (uint16_t)(c->y + REC_PALETTE), c->a);
      lda(s, (uint16_t)(0xc018u | data(s, (uint16_t)(c->y + REC_FLAGS))));
      set_data(s, (uint16_t)(c->y + REC_FLAGS), c->a);
      lda(s, 0);
      ldy(s, 0);  // `TAY`
      ran(s, CAST_STORM_RUN_DFBF);
      return stop(s, 0xdfd7);
    case 0xdfdb:
      lda(s, 0xe082);
      ran(s, CAST_STORM_RUN_DFDB);
      return stop(s, 0xdfde);
    case 0xdfe2:
      lda(s, 0x0006);
      ran(s, CAST_STORM_RUN_DFE2);
      return stop(s, 0xdfe5);
    case 0xdfe9:
      ldx(s, field(s, DP_RECORD));
      lda(s, 0xf9a7);
      set_data(s, (uint16_t)(c->x + REC_PICTURES), c->a);
      ran(s, CAST_STORM_RUN_DFE9);
      return stop(s, 0xdff1);

    // Half the time, the screen lit.
    case 0xdff5:
      lda(s, c->a & 0x0040);
      ran(s, CAST_STORM_RUN_DFF5);
      if (zero(s)) {
        PORT_COVER(cast_storm_unlit);
        taken(s);
        return 0xdfff;
      }
      PORT_COVER(cast_storm_lit);
      ran(s, CAST_STORM_RUN_DFFA);
      return jsr(s, 0xdffa, 0xe01f);
    case 0xdfff:
      lda(s, 0x0005);
      ran(s, CAST_STORM_RUN_DFFF);
      return stop(s, 0xe002);
    case 0xe01f:
      lda8(s, 0x00);
      ran(s, CAST_STORM_RUN_E01F);
      write_reg(s, 0x2130, 0x00);
      lda8(s, 0x3f);
      ran(s, CAST_STORM_RUN_E026);
      write_reg(s, 0x2131, 0x3f);
      lda8(s, 0xe0);
      wram_w8(s->w, W_FIXED_COLOUR, 0xe0);
      lda(s, 0x00ef);
      set_data(s, W_FIXED_COLOUR, c->a);
      c->a = 0xe076;
      ldy(s, CAST_STORM_BANK);
      ran(s, CAST_STORM_RUN_E02B);
      return stop(s, 0xe03e);
    case 0xe042:
      lda(s, 0x0021);
      ran(s, CAST_STORM_RUN_E042);
      return stop(s, 0xe045);
    case 0xe049:
      lda(s, 0x0003);
      ran(s, CAST_STORM_RUN_E049);
      return stop(s, 0xe04c);
    case 0xe050:
      set_data(s, W_FIXED_COLOUR, 0);
      c->a = 0xe076;
      ldy(s, CAST_STORM_BANK);
      ran(s, CAST_STORM_RUN_E050);
      return stop(s, 0xe059);
    case 0xe05d:
      lda8(s, 0xa0);
      ran(s, CAST_STORM_RUN_E05D);
      write_reg(s, 0x2125, 0xa0);
      lda8(s, 0x10);
      ran(s, CAST_STORM_RUN_E064);
      write_reg(s, 0x2130, 0x10);
      lda8(s, 0xa2);
      ran(s, CAST_STORM_RUN_E069);
      write_reg(s, 0x2131, 0xa2);
      lda8(s, 0xe5);
      ran(s, CAST_STORM_RUN_E06E);
      write_reg(s, 0x2132, 0xe5);
      ran(s, CAST_STORM_RUN_E073);
      return rts(s);
    case 0xdffd:
      ran(s, CAST_STORM_RUN_DFFD);
      taken(s);
      return 0xe006;
    case 0xe006:
      ldx(s, field(s, DP_RECORD));
      set_data(s, (uint16_t)(c->x + REC_PICTURES), 0);
      set_data(s, (uint16_t)(c->x + REC_PICTURES_BANK), 0);
      ran(s, CAST_STORM_RUN_E006);
      return stop(s, 0xe00e);
    case 0xe016:
      ran(s, CAST_STORM_RUN_E016);
      return 0xdf7c;

    // `$82:E076`, the job: the fixed colour's byte sent, and taken off the
    // queue.
    case 0xe076: {
      PORT_COVER(cast_storm_job);
      const uint8_t v = (uint8_t)data(s, W_FIXED_COLOUR);
      lda8(s, v);
      ran(s, CAST_STORM_RUN_E076);
      write_reg(s, 0x2132, v);
      set_c(c, false);
      ran(s, CAST_STORM_RUN_E07E);
      return stop(s, 0xe081);
    }
  }
  s->k->lost = true;
  return stop(s, pc);
}

// --- The two machines --------------------------------------------------------

void cast_work_init(CastWork* k) {
  k->trace.n = 0;
  k->trace.cap = CAST_TRACE_STEPS;
  k->trace.full = false;
  k->trace.step = k->steps;
  k->lost = false;
}

static void run(Cast* s, uint32_t (*block)(Cast*, uint16_t)) {
  uint32_t pc = s->c->pc & 0xffffu;
  for (int i = 0; i < 256; i++) {
    pc = block(s, (uint16_t)pc);
    if (pc & STOP) {
      s->c->pc = pc & 0xffffffu;
      return;
    }
  }
  s->k->lost = true;
}

void cast_run(Wram* w, const Rom* rom, PortCpu* c, CastWork* k) {
  Cast s = {w, rom, c, k, CAST_BANK, CAST_RUN_TAKEN};
  run(&s, screen_block);
}

void cast_storm_run(Wram* w, const Rom* rom, PortCpu* c, CastWork* k) {
  Cast s = {w, rom, c, k, CAST_STORM_BANK, CAST_STORM_RUN_TAKEN};
  run(&s, storm_block);
}
