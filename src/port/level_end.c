// The screen after a level -- see port/level_end.h.

#include "port/level_end.h"

#include "port/bcd.h"
#include "port/coverage.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  LevelEndWork* k;
} End;

// A block hands back the address it goes to next, in its bank, or `STOP`
// and the instruction it stopped on.
#define STOP 0x01000000u

// --- The machine, as these blocks use it ------------------------------------

static uint16_t field(const End* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->c->d + at));
}

static void set_field(End* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->c->d + at), v);
}

// A word read through the data bank, which is its own: a table of its own,
// or low memory.
static uint16_t data(const End* s, uint16_t at) {
  return bus_r16(s->w, s->rom, (uint32_t)LEVEL_END_BANK << 16 | at);
}

// ...and one written, which is always to low memory.
static void set_data(End* s, uint16_t at, uint16_t v) { wram_w16(s->w, at, v); }

// A word at a long address: the level's record in `$9F`, or WRAM in `$7E`.
static uint16_t far(const End* s, uint32_t at) {
  return bus_r16(s->w, s->rom, at & 0xffffffu);
}

static void set_far(End* s, uint32_t at, uint16_t v) {
  wram_w16(s->w, (at & 0xffffffu) - 0x7e0000u, v);
}

static void ran(End* s, LevelEndRun run) { s->k->runs[run]++; }

static bool took(End* s, bool taken) {
  if (taken) s->k->taken++;
  return taken;
}

static bool negative(uint16_t v) { return (v & 0x8000u) != 0; }
static bool zero(const End* s) { return flag(s->c, PORT_P_Z); }
static bool carry(const End* s) { return flag(s->c, PORT_P_C); }
static bool minus(const End* s) { return flag(s->c, PORT_P_N); }

static void lda(End* s, uint16_t v) {
  s->c->a = v;
  set_nz16(s->c, v);
}

static void ldx(End* s, uint16_t v) {
  s->c->x = v;
  set_nz16(s->c, v);
}

static void ldy(End* s, uint16_t v) {
  s->c->y = v;
  set_nz16(s->c, v);
}

static void doubled(End* s) { s->c->a = asl16(s->c, s->c->a); }
static void cmp(End* s, uint16_t v) { cmp16(s->c, s->c->a, v); }

// `CLC : ADC`.
static void add(End* s, uint16_t v) {
  set_c(s->c, false);
  s->k->v_known = true;
  s->c->a = adc16(s->c, s->c->a, v);
}

// `ADC` with the carry as it stands, in decimal mode. Its overflow is not
// modelled.
static void adc_decimal(End* s, uint16_t v) {
  bool c = carry(s), adjusted = false;
  lda(s, bcd_add16(s->c->a, v, &c, &adjusted));
  set_c(s->c, c);
  s->k->v_known = false;
}

// `DEC` and `INC` of a word of low memory.
static uint16_t count_down_at(End* s, uint16_t at) {
  const uint16_t v = (uint16_t)(data(s, at) - 1);
  set_data(s, at, v);
  set_nz16(s->c, v);
  return v;
}

static uint16_t count_up_at(End* s, uint16_t at) {
  const uint16_t v = (uint16_t)(data(s, at) + 1);
  set_data(s, at, v);
  set_nz16(s->c, v);
  return v;
}

// `DEX`, `INX`, `DEY`, `INY`.
static void dex(End* s) { ldx(s, (uint16_t)(s->c->x - 1)); }
static void inx(End* s) { ldx(s, (uint16_t)(s->c->x + 1)); }
static void dey(End* s) { ldy(s, (uint16_t)(s->c->y - 1)); }
static void iny(End* s) { ldy(s, (uint16_t)(s->c->y + 1)); }

static void pha(End* s) { push16(s->w, s->c, s->c->a); }
static void phx(End* s) { push16(s->w, s->c, s->c->x); }
static void pla(End* s) { lda(s, pull16(s->w, s->c)); }
static void plx(End* s) { ldx(s, pull16(s->w, s->c)); }
static void ply(End* s) { ldy(s, pull16(s->w, s->c)); }

// A `JSR` at `at`, going to `to`.
static uint32_t jsr(End* s, uint16_t at, uint16_t to) {
  push16(s->w, s->c, (uint16_t)(at + 2));
  return to;
}

// An `RTS`: where it goes is what it pulls, and one on.
static uint32_t rts(End* s) { return (uint16_t)(pull16(s->w, s->c) + 1); }

// ...one that is a run of its own.
static uint32_t lone_rts(End* s) {
  ran(s, LEVEL_END_RUN_RTS);
  return rts(s);
}

static uint32_t stop(uint16_t at) {
  return STOP | (uint32_t)LEVEL_END_BANK << 16 | at;
}

// `SED : LDA lo : CLC : ADC #v : STA lo : LDA #0 : ADC hi : STA hi : CLD`:
// the score at `lo`, `v` more.
static void pays(End* s, uint16_t lo, uint16_t v) {
  lda(s, data(s, lo));
  set_c(s->c, false);
  adc_decimal(s, v);
  set_data(s, lo, s->c->a);
  lda(s, 0);
  adc_decimal(s, data(s, (uint16_t)(lo + 2)));
  set_data(s, (uint16_t)(lo + 2), s->c->a);
}

// --- Its words and tables ---------------------------------------------------

#define W_BRIGHTNESS 0x136cu
#define W_PLAYING 0x1e88u        // a word a player: in the game
#define W_PLAYING_B 0x1e8au
#define W_SIDE 0x1e84u           // ...and which side
#define W_LIVES 0x1d4cu
#define W_SAVED 0x1f9cu          // neighbours saved, a word a player
#define W_SAVED_B 0x1f9eu
#define W_SHOWN 0x1e9cu          // ...left to show
#define W_SHOWN_B 0x1e9eu
#define W_KINDS 0x1fa0u          // the kind of each saved, a byte apiece
#define W_KINDS_B 0x1fabu
#define W_STATE 0x1eb2u
#define W_LEVEL 0x1e7cu
#define W_SCORE_A 0x1e72u        // two words each, in decimal
#define W_SCORE_A_HI 0x1e74u
#define W_SCORE_B 0x1e76u
#define W_SCORE_B_HI 0x1e78u
#define W_TARGET 0x005cu         // what each counts up to, likewise
#define W_PADS_A 0x006eu
#define W_PADS_B 0x0070u
#define W_LIST 0x7e6d8eu         // the threads it showed, six words a player
#define W_LIST_B 0x7e6d9au
#define W_NEXT_LIFE 0x7e6054u    // the score's top word that pays a life
#define W_COUNT 0x1e92u
#define W_AT 0x1e94u
#define W_EACH 0x1f54u
#define W_RECORDS 0x1e96u        // a player's last life's record

#define BONUSES 0xc346u          // its handlers, by the id in the record
#define LEVEL_BONUSES 0xd1eeu    // ...and one of `$82:D0CB`'s, by the level
#define DP_RECORD 0x10           // the main thread's level record

static bool bonus_ported(uint16_t at) {
  switch (at) {
    case 0xc499:
    case 0xc56b:
    case 0xce17:
    case 0xcfeb:
    case 0xd0cb:
      return true;
  }
  return false;
}

static bool level_bonus_ported(uint16_t at) {
  switch (at) {
    case 0xd117:
    case 0xd118:
    case 0xd126:
    case 0xd159:
    case 0xd17d:
      return true;
  }
  return false;
}

// --- The blocks -------------------------------------------------------------

// `$82:BF38` and `$82:D4FC`: a word of a record.
static void set_record(End* s, uint16_t record, uint16_t at, uint16_t v) {
  set_data(s, (uint16_t)(record + at), v);
}

static uint32_t block(End* s, uint16_t pc) {
  PortCpu* c = s->c;
  uint16_t v;
  switch (pc) {
    // --- `$82:BD6A`: the screen -------------------------------------------
    case 0xbd6a:
      // `PHB : PEA $0082 : PLB`.
      push8(s->w, c, c->db);
      push16(s->w, c, LEVEL_END_BANK);
      c->db = pull8(s->w, c);
      set_nz8(c, c->db);
      ran(s, LEVEL_END_RUN_BD6A);
      return jsr(s, 0xbd6f, 0xbe80);
    case 0xbd72:
      ran(s, LEVEL_END_RUN_BD72);
      return jsr(s, 0xbd72, 0xbe5e);
    case 0xbd75:
      ran(s, LEVEL_END_RUN_BD75);
      return jsr(s, 0xbd75, 0xbfd5);
    case 0xbd78:
      lda(s, 1);
      ran(s, LEVEL_END_RUN_BD78);
      return stop(0xbd7b);
    case 0xbd80:
      lda(s, data(s, W_BRIGHTNESS));
      cmp(s, 0x000f);
      ran(s, LEVEL_END_RUN_BD80);
      if (took(s, zero(s))) return 0xbd8e;
      lda(s, (uint16_t)(c->a + 1));
      set_data(s, W_BRIGHTNESS, c->a);
      ran(s, LEVEL_END_RUN_BD88);
      s->k->taken++;
      return stop(0xbd7f);
    case 0xbd8e:
      PORT_COVER(level_end_lit);
      ran(s, LEVEL_END_RUN_BD8E);
      return jsr(s, 0xbd8e, 0xc068);
    case 0xbd91:
      lda(s, 0x0010);
      ran(s, LEVEL_END_RUN_BD91);
      return stop(0xbd94);
    case 0xbd98:
      // `PEA $BDA0 : LDA $1EB2 : DEC : PHA : RTS`: to its state.
      push16(s->w, c, 0xbda0);
      lda(s, data(s, W_STATE));
      lda(s, (uint16_t)(c->a - 1));
      pha(s);
      ran(s, LEVEL_END_RUN_BD98);
      return rts(s);
    case 0xbda1:
      ran(s, LEVEL_END_RUN_BDA1);
      if (took(s, !carry(s))) return 0xbd91;
      lda(s, data(s, W_LEVEL));
      ran(s, LEVEL_END_RUN_BDA3);
      if (took(s, zero(s))) return 0xbdb9;
      lda(s, c->a & 0x0003);
      ran(s, LEVEL_END_RUN_BDA8);
      if (took(s, !zero(s))) return 0xbdb9;
      PORT_COVER(level_end_password);
      ran(s, LEVEL_END_RUN_BDAD);
      return jsr(s, 0xbdad, 0xbdf5);
    case 0xbdb0:
      lda(s, data(s, W_LEVEL));
      lda(s, (uint16_t)(c->a + 1));
      set_data(s, 0x137c, c->a);
      ran(s, LEVEL_END_RUN_BDB0);
      s->k->taken++;
      return stop(0xbdd3);
    case 0xbdb9:
      PORT_COVER(level_end_rests);
      lda(s, 0x00b4);
      set_data(s, 0x004e, c->a);
      ran(s, LEVEL_END_RUN_BDB9);
      return 0xbdbf;
    case 0xbdbf:
      lda(s, 1);
      ran(s, LEVEL_END_RUN_BDBF);
      return stop(0xbdc2);
    case 0xbdc6:
      lda(s, data(s, W_PADS_A));
      lda(s, c->a | data(s, W_PADS_B));
      ran(s, LEVEL_END_RUN_BDC6);
      if (took(s, !zero(s))) {
        PORT_COVER(level_end_rest_cut);
        return stop(0xbdd3);
      }
      v = count_down_at(s, 0x004e);
      ran(s, LEVEL_END_RUN_BDCE);
      if (took(s, !negative(v))) return 0xbdbf;
      return stop(0xbdd3);
    case 0xbdd4:
      lda(s, data(s, W_BRIGHTNESS));
      ran(s, LEVEL_END_RUN_BDD4);
      if (took(s, zero(s))) return 0xbddf;
      lda(s, (uint16_t)(c->a - 1));
      set_data(s, W_BRIGHTNESS, c->a);
      ran(s, LEVEL_END_RUN_BDD9);
      s->k->taken++;
      return stop(0xbdd3);
    case 0xbddf:
      lda(s, 0x0080);
      set_data(s, W_BRIGHTNESS, c->a);
      ran(s, LEVEL_END_RUN_BDDF);
      return stop(0xbde5);
    case 0xbdea:
      lda(s, data(s, 0x0068));
      ran(s, LEVEL_END_RUN_BDEA);
      return stop(0xbded);
    case 0xbdf2:
      // `PLB : PLB`, and its `RTL`.
      c->db = pull8(s->w, c);
      set_nz8(c, c->db);
      c->db = pull8(s->w, c);
      set_nz8(c, c->db);
      ran(s, LEVEL_END_RUN_BDF2);
      return stop(0xbdf4);

    // --- `$82:BDF5`: the password -------------------------------------------
    case 0xbdf5:
      ldy(s, 0);
      ldx(s, 0xbfbe);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_BDF5);
      return stop(0xbdfe);
    case 0xbe06:
      lda(s, 3);
      ran(s, LEVEL_END_RUN_BE06);
      return stop(0xbe09);
    case 0xbe0d:
      lda(s, 4);
      set_data(s, 0x004c, c->a);
      ran(s, LEVEL_END_RUN_BE0D);
      return 0xbe13;
    case 0xbe13:
      lda(s, LEVEL_END_BANK);
      ldx(s, 0xbfd0);
      ldy(s, 0x1ea0);
      set_data(s, 0x004e, 0);
      ran(s, LEVEL_END_RUN_BE13);
      return stop(0xbe1f);
    case 0xbe27:
      lda(s, 3);
      ran(s, LEVEL_END_RUN_BE27);
      return stop(0xbe2a);
    case 0xbe2e:
      lda(s, LEVEL_END_BANK);
      ldx(s, 0xbfcb);
      ldy(s, 0x1ea0);
      set_data(s, 0x004e, 0);
      ran(s, LEVEL_END_RUN_BE2E);
      return stop(0xbe3a);
    case 0xbe42:
      lda(s, 3);
      ran(s, LEVEL_END_RUN_BE42);
      return stop(0xbe45);
    case 0xbe49:
      v = count_down_at(s, 0x004c);
      ran(s, LEVEL_END_RUN_BE49);
      if (took(s, !negative(v))) return 0xbe13;
      return 0xbe4e;
    case 0xbe4e:
      lda(s, 1);
      ran(s, LEVEL_END_RUN_BE4E);
      return stop(0xbe51);
    case 0xbe55:
      lda(s, data(s, W_PADS_A));
      lda(s, c->a | data(s, W_PADS_B));
      ran(s, LEVEL_END_RUN_BE55);
      if (took(s, zero(s))) return 0xbe4e;
      return lone_rts(s);

    // --- `$82:BE5E`: its string cleared, and the pads ----------------------
    case 0xbe5e:
      lda(s, 0);
      ldx(s, 0x0010);
      ran(s, LEVEL_END_RUN_BE5E);
      return 0xbe64;
    case 0xbe64:
      set_data(s, (uint16_t)(0x1ea0 + c->x), c->a);
      dex(s);
      dex(s);
      ran(s, LEVEL_END_RUN_BE64);
      if (took(s, !minus(s))) return 0xbe64;
      set_data(s, 0x1ea0, 0);
      set_data(s, W_PADS_A, 0);
      set_data(s, W_PADS_B, 0);
      lda(s, data(s, W_LEVEL));
      cmp(s, 0);
      ran(s, LEVEL_END_RUN_BE6B);
      if (took(s, zero(s))) return lone_rts(s);
      set_data(s, 0x1f94, 0);
      ran(s, LEVEL_END_RUN_BE7C);
      return lone_rts(s);

    // --- `$82:BE80`: the screen set up --------------------------------------
    case 0xbe80:
      lda(s, data(s, W_BRIGHTNESS));
      lda(s, c->a | 0x0080);
      set_data(s, W_BRIGHTNESS, c->a);
      ran(s, LEVEL_END_RUN_BE80);
      return stop(0xbe89);
    case 0xbe8d:
      lda(s, 0x0083);
      ldx(s, 0xf18c);
      ldy(s, 0x0100);
      ran(s, LEVEL_END_RUN_BE8D);
      return stop(0xbe96);
    case 0xbe9a:
      lda(s, 0x0096);
      pha(s);
      lda(s, 0x9683);
      ldx(s, 0x4000);
      ldy(s, 0x1620);
      ran(s, LEVEL_END_RUN_BE9A);
      return stop(0xbea7);
    case 0xbeab:
      pla(s);
      ran(s, LEVEL_END_RUN_BEAB);
      return stop(0xbeac);
    case 0xbeb4:
      // `SEP #$20 : LDA #$11 : STA $212C : REP #$20`.
      c->a = (uint16_t)((c->a & 0xff00u) | LEVEL_END_TM_SPRITES_AND_FIRST);
      set_nz8(c, LEVEL_END_TM_SPRITES_AND_FIRST);
      s->k->layers_set = true;
      ran(s, LEVEL_END_RUN_BEB4);
      lda(s, data(s, W_PLAYING));
      ran(s, LEVEL_END_RUN_BEBB);
      if (took(s, zero(s))) return 0xbecf;
      ldy(s, 0);
      ldx(s, 0xbfb2);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_BEC2);
      return stop(0xbecb);
    case 0xbecf:
      lda(s, data(s, W_PLAYING_B));
      ran(s, LEVEL_END_RUN_BECF);
      if (took(s, zero(s))) return 0xbee1;
      ldy(s, 0);
      ldx(s, 0xbfb8);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_BED4);
      return stop(0xbedd);
    case 0xbee1:
      ldy(s, 0);
      ldx(s, 0xbf9e);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_BEE1);
      return stop(0xbeea);
    case 0xbeee:
      set_data(s, W_BRIGHTNESS, 0);
      ran(s, LEVEL_END_RUN_BEEE);
      return stop(0xbef1);
    case 0xbef2:
      lda(s, data(s, W_BRIGHTNESS));
      lda(s, c->a & 0x007f);
      set_data(s, W_BRIGHTNESS, c->a);
      ran(s, LEVEL_END_RUN_BEF2);
      return jsr(s, 0xbefb, 0xbeff);
    case 0xbeff:
      return stop(0xbeff);
    case 0xbf03:
      lda(s, 0x8000);
      ldy(s, 0x0084);
      ran(s, LEVEL_END_RUN_BF03);
      return stop(0xbf09);

    // --- `$82:BF11`: each player's lives, a record apiece -------------------
    case 0xbf11:
      set_data(s, 0x0068, c->x);
      set_data(s, 0x1b6a, 0);
      set_data(s, 0x1b6c, 0);
      lda(s, 2);
      set_data(s, W_EACH, c->a);
      ran(s, LEVEL_END_RUN_BF11);
      return 0xbf20;
    case 0xbf20:
      ldx(s, data(s, W_EACH));
      lda(s, data(s, (uint16_t)(W_PLAYING + c->x)));
      ran(s, LEVEL_END_RUN_BF20);
      if (took(s, zero(s))) return 0xbf86;
      lda(s, 0x0018);
      set_data(s, W_AT, c->a);
      lda(s, data(s, (uint16_t)(W_LIVES + c->x)));
      set_data(s, W_COUNT, c->a);
      ran(s, LEVEL_END_RUN_BF28);
      return stop(0xbf34);
    case 0xbf38: {
      const uint16_t record = c->a;
      ldy(s, record);
      lda(s, data(s, W_AT));
      set_record(s, record, 0x02, c->a);
      add(s, 0x0010);
      set_data(s, W_AT, c->a);
      lda(s, 0);
      set_record(s, record, 0x04, c->a);
      ldx(s, data(s, W_EACH));
      lda(s, data(s, (uint16_t)(0xbf96 + c->x)));
      set_record(s, record, 0x06, c->a);
      lda(s, data(s, (uint16_t)(W_SIDE + c->x)));
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(0xbf9a + c->x)));
      set_record(s, record, 0x08, c->a);
      lda(s, 0x008f);
      set_record(s, record, 0x0a, c->a);
      lda(s, data(s, 0x0008));
      set_record(s, record, 0x0c, c->a);
      lda(s, 0);
      set_record(s, record, 0x0e, c->a);
      lda(s, 0xc008);
      lda(s, c->a | data(s, record));
      set_record(s, record, 0x00, c->a);
      v = count_down_at(s, W_COUNT);
      ran(s, LEVEL_END_RUN_BF38);
      if (took(s, !negative(v))) return stop(0xbf34);
      lda(s, record);
      ldx(s, data(s, W_EACH));
      set_data(s, (uint16_t)(W_RECORDS + c->x), c->a);
      ran(s, LEVEL_END_RUN_BF7F);
      return 0xbf86;
    }
    case 0xbf86:
      count_down_at(s, W_EACH);
      v = count_down_at(s, W_EACH);
      ran(s, LEVEL_END_RUN_BF86);
      if (took(s, !negative(v))) return 0xbf20;
      lda(s, 1);
      ran(s, LEVEL_END_RUN_BF8E);
      return stop(0xbf91);
    case 0xbf95:
    case 0xbefe:
      return lone_rts(s);

    // --- `$82:BFD5`: both scores into their strings, and sent ---------------
    case 0xbfd5:
      ldy(s, 2);
      set_data(s, W_EACH, c->y);
      ran(s, LEVEL_END_RUN_BFD5);
      return 0xbfdb;
    case 0xbfdb:
      ldy(s, data(s, W_EACH));
      lda(s, data(s, (uint16_t)(W_PLAYING + c->y)));
      ran(s, LEVEL_END_RUN_BFDB);
      if (took(s, zero(s))) return 0xc02c;
      lda(s, c->y);
      doubled(s);
      add(s, 0x1e75);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(0xc056 + c->y)));
      ldy(s, c->a);
      lda(s, 3);
      set_data(s, 0x004a, c->a);
      set_data(s, 0x0048, 0);
      ran(s, LEVEL_END_RUN_BFE3);
      return 0xbff7;
    case 0xbff7:
      // A byte's top digit, and then its bottom.
      lda(s, data(s, c->x));
      for (int i = 0; i < 4; i++) {
        set_c(c, (c->a & 1) != 0);
        lda(s, (uint16_t)(c->a >> 1));
      }
      lda(s, c->a & 0x000f);
      ran(s, LEVEL_END_RUN_BFF7);
      return jsr(s, 0xc001, 0xc039);
    case 0xc004:
      lda(s, data(s, c->x) & 0x000f);
      ran(s, LEVEL_END_RUN_C004);
      return jsr(s, 0xc00a, 0xc039);
    case 0xc00d:
      dex(s);
      v = count_down_at(s, 0x004a);
      ran(s, LEVEL_END_RUN_C00D);
      if (took(s, !negative(v))) return 0xbff7;
      ldy(s, data(s, W_EACH));
      lda(s, data(s, (uint16_t)(0xc056 + c->y)));
      add(s, 1);
      pha(s);
      ldx(s, data(s, (uint16_t)(0xc05a + c->y)));
      lda(s, LEVEL_END_BANK);
      ply(s);
      set_data(s, 0x004e, 0);
      ran(s, LEVEL_END_RUN_C013);
      return stop(0xc028);
    case 0xc02c:
      count_down_at(s, W_EACH);
      v = count_down_at(s, W_EACH);
      ran(s, LEVEL_END_RUN_C02C);
      if (took(s, !negative(v))) return 0xbfdb;
      return stop(0xc034);
    case 0xc038:
      return lone_rts(s);
    case 0xc039:
      // A digit, blank while it is a leading nought.
      ran(s, LEVEL_END_RUN_C039);
      if (!took(s, !zero(s))) {
        bit16(c, data(s, 0x0048));
        s->k->v_known = true;
        ran(s, LEVEL_END_RUN_C03B);
        if (!took(s, minus(s))) {
          lda(s, 0x002f);
          ran(s, LEVEL_END_RUN_C040);
          s->k->taken++;
          return 0xc04d;
        }
      }
      return 0xc045;
    case 0xc045: {
      add(s, 0x0030);
      // `SEC : ROR $0048`.
      const uint16_t m = data(s, 0x0048);
      v = (uint16_t)(m >> 1 | 0x8000u);
      set_data(s, 0x0048, v);
      set_c(c, (m & 1) != 0);
      set_nz16(c, v);
      ran(s, LEVEL_END_RUN_C045);
      return 0xc04d;
    }
    case 0xc04d:
      wram_w8(s->w, c->y, (uint8_t)c->a);
      iny(s);
      ran(s, LEVEL_END_RUN_C04D);
      return rts(s);

    // --- `$82:C068`: the neighbours saved -----------------------------------
    case 0xc068:
      lda(s, 0);
      set_data(s, W_SHOWN, c->a);
      set_data(s, W_SHOWN_B, c->a);
      set_data(s, W_TARGET, 0);
      set_data(s, W_AT, 0);
      lda(s, 0x0024);
      set_data(s, 0x0044, c->a);
      set_data(s, 0x0046, c->a);
      lda(s, 0x000b);
      doubled(s);
      ldx(s, c->a);
      lda(s, 0xffff);
      ran(s, LEVEL_END_RUN_C068);
      return 0xc088;
    case 0xc088:
      set_far(s, W_LIST + c->x, c->a);
      dex(s);
      dex(s);
      ran(s, LEVEL_END_RUN_C088);
      if (took(s, !minus(s))) return 0xc088;
      ldy(s, 2);
      ran(s, LEVEL_END_RUN_C090);
      return 0xc093;
    case 0xc093:
      lda(s, data(s, (uint16_t)(W_PLAYING + c->y)));
      ran(s, LEVEL_END_RUN_C093);
      if (took(s, zero(s))) return 0xc0b1;
      lda(s, data(s, W_SAVED));
      add(s, data(s, W_SAVED_B));
      cmp(s, 0x000b);
      ran(s, LEVEL_END_RUN_C098);
      if (!took(s, !carry(s))) {
        PORT_COVER(level_end_saved_cut);
        lda(s, data(s, (uint16_t)(W_SAVED + c->y)));
        lda(s, (uint16_t)(c->a - 1));
        set_data(s, (uint16_t)(W_SAVED + c->y), c->a);
        ran(s, LEVEL_END_RUN_C0A4);
      }
      lda(s, data(s, (uint16_t)(W_SAVED + c->y)));
      set_data(s, (uint16_t)(W_SHOWN + c->y), c->a);
      ran(s, LEVEL_END_RUN_C0AB);
      return 0xc0b1;
    case 0xc0b1:
      dey(s);
      dey(s);
      ran(s, LEVEL_END_RUN_C0B1);
      if (took(s, !minus(s))) return 0xc093;
      lda(s, 0xc0bb);
      set_data(s, W_STATE, c->a);
      ran(s, LEVEL_END_RUN_C0B5);
      return 0xc0bb;
    case 0xc0bb:
      // Its state, a tick at a time: the next each saved.
      lda(s, data(s, W_SHOWN));
      ran(s, LEVEL_END_RUN_C0BB);
      if (took(s, zero(s))) return 0xc0ff;
      lda(s, (uint16_t)(c->a - 1));
      set_data(s, W_SHOWN, c->a);
      lda(s, data(s, 0x0044));
      set_field(s, 0x00, c->a);
      lda(s, 0x0066);
      set_field(s, 0x02, c->a);
      ldx(s, data(s, W_AT));
      lda(s, data(s, (uint16_t)(W_KINDS + c->x)));
      lda(s, c->a & 0x00ff);
      ran(s, LEVEL_END_RUN_C0C0);
      if (took(s, zero(s))) return 0xc0ff;
      PORT_COVER(level_end_saved_shown);
      ldx(s, c->a);
      lda(s, data(s, 0x0044));
      add(s, data(s, (uint16_t)(0xc239 + c->x)));
      set_data(s, 0x0044, c->a);
      lda(s, c->x);
      doubled(s);
      ldx(s, c->a);
      ldy(s, data(s, (uint16_t)(0xc20b + c->x)));
      lda(s, data(s, (uint16_t)(0xc209 + c->x)));
      ran(s, LEVEL_END_RUN_C0D9);
      return stop(0xc0ed);
    case 0xc0f1:
      ldx(s, data(s, W_TARGET));
      set_far(s, W_LIST + c->x, c->a);
      lda(s, 0x0010);
      ran(s, LEVEL_END_RUN_C0F1);
      return stop(0xc0fb);
    case 0xc0ff:
      lda(s, data(s, W_SHOWN_B));
      ran(s, LEVEL_END_RUN_C0FF);
      if (took(s, zero(s))) return 0xc143;
      lda(s, (uint16_t)(c->a - 1));
      set_data(s, W_SHOWN_B, c->a);
      lda(s, data(s, 0x0046));
      set_field(s, 0x00, c->a);
      lda(s, 0x00ae);
      set_field(s, 0x02, c->a);
      ldx(s, data(s, W_AT));
      lda(s, data(s, (uint16_t)(W_KINDS_B + c->x)));
      lda(s, c->a & 0x00ff);
      ran(s, LEVEL_END_RUN_C104);
      if (took(s, zero(s))) return 0xc143;
      ldx(s, c->a);
      lda(s, data(s, 0x0046));
      add(s, data(s, (uint16_t)(0xc239 + c->x)));
      set_data(s, 0x0046, c->a);
      lda(s, c->x);
      doubled(s);
      ldx(s, c->a);
      ldy(s, data(s, (uint16_t)(0xc20b + c->x)));
      lda(s, data(s, (uint16_t)(0xc209 + c->x)));
      ran(s, LEVEL_END_RUN_C11D);
      return stop(0xc131);
    case 0xc135:
      ldx(s, data(s, W_TARGET));
      set_far(s, W_LIST_B + c->x, c->a);
      lda(s, 0x0010);
      ran(s, LEVEL_END_RUN_C135);
      return stop(0xc13f);
    case 0xc143:
      count_up_at(s, W_AT);
      lda(s, data(s, W_SHOWN));
      lda(s, c->a | data(s, W_SHOWN_B));
      ran(s, LEVEL_END_RUN_C143);
      if (took(s, zero(s))) return 0xc160;
      lda(s, data(s, W_TARGET));
      lda(s, (uint16_t)(c->a + 1));
      lda(s, (uint16_t)(c->a + 1));
      set_data(s, W_TARGET, c->a);
      cmp(s, 0x000a);
      ran(s, LEVEL_END_RUN_C14E);
      if (took(s, !zero(s))) return 0xc15e;
      PORT_COVER(level_end_saved_row);
      ran(s, LEVEL_END_RUN_C15B);
      return jsr(s, 0xc15b, 0xc16d);
    case 0xc15e:
      set_c(c, false);
      ran(s, LEVEL_END_RUN_C15E);
      return rts(s);
    case 0xc160:
      ran(s, LEVEL_END_RUN_C160);
      return jsr(s, 0xc160, 0xc16d);
    case 0xc163:
      lda(s, 3);
      ran(s, LEVEL_END_RUN_C163);
      return stop(0xc166);
    case 0xc16a:
      ran(s, LEVEL_END_RUN_C16A);
      return 0xc251;
    case 0xc251:
      lda(s, 0xc259);
      set_data(s, W_STATE, c->a);
      set_c(c, false);
      ran(s, LEVEL_END_RUN_C251);
      return rts(s);

    // --- `$82:C16D`: each of those shown told, and paid for ---------------
    case 0xc16d:
      lda(s, 0x003c);
      ran(s, LEVEL_END_RUN_C16D);
      return stop(0xc170);
    case 0xc174:
      set_data(s, W_TARGET, 0);
      ran(s, LEVEL_END_RUN_C174);
      return 0xc177;
    case 0xc177:
      ran(s, LEVEL_END_RUN_C177);
      return jsr(s, 0xc177, 0xbfd5);
    case 0xc17a:
      lda(s, 3);
      ran(s, LEVEL_END_RUN_C17A);
      return stop(0xc17d);
    case 0xc181:
      ldy(s, 0x00ff);
      ldx(s, data(s, W_TARGET));
      lda(s, far(s, W_LIST + c->x));
      cmp(s, 0xffff);
      ran(s, LEVEL_END_RUN_C181);
      if (took(s, zero(s))) return 0xc1b1;
      ldx(s, c->a);
      ran(s, LEVEL_END_RUN_C190);
      return stop(0xc191);
    case 0xc195:
      PORT_COVER(level_end_saved_paid);
      pays(s, W_SCORE_A, 0x0050);
      lda(s, 0x000e);
      ran(s, LEVEL_END_RUN_C195);
      return stop(0xc1ad);
    case 0xc1b1:
      ldy(s, 0x00ff);
      ldx(s, data(s, W_TARGET));
      lda(s, far(s, W_LIST_B + c->x));
      cmp(s, 0xffff);
      ran(s, LEVEL_END_RUN_C1B1);
      if (took(s, zero(s))) return 0xc1e1;
      ldx(s, c->a);
      ran(s, LEVEL_END_RUN_C1C0);
      return stop(0xc1c1);
    case 0xc1c5:
      pays(s, W_SCORE_B, 0x0050);
      lda(s, 0x000e);
      ran(s, LEVEL_END_RUN_C1C5);
      return stop(0xc1dd);
    case 0xc1e1:
      ldx(s, data(s, W_TARGET));
      lda(s, 0xffff);
      set_far(s, W_LIST + c->x, c->a);
      set_far(s, W_LIST_B + c->x, c->a);
      inx(s);
      inx(s);
      set_data(s, W_TARGET, c->x);
      cmp16(c, c->x, 0x000c);
      ran(s, LEVEL_END_RUN_C1E1);
      if (took(s, zero(s))) return 0xc1fc;
      ran(s, LEVEL_END_RUN_C1F9);
      return 0xc177;
    case 0xc1fc:
      lda(s, 0x0024);
      set_data(s, 0x0044, c->a);
      set_data(s, 0x0046, c->a);
      set_data(s, W_TARGET, 0);
      ran(s, LEVEL_END_RUN_C1FC);
      return rts(s);

    // --- `$82:C259`: the bonuses --------------------------------------------
    case 0xc259:
      lda(s, data(s, W_SCORE_A));
      set_data(s, 0x005c, c->a);
      lda(s, data(s, W_SCORE_A_HI));
      set_data(s, 0x005e, c->a);
      lda(s, data(s, W_SCORE_B));
      set_data(s, 0x0060, c->a);
      lda(s, data(s, W_SCORE_B_HI));
      set_data(s, 0x0062, c->a);
      ldx(s, field(s, DP_RECORD));
      lda(s, far(s, 0x9f003au + c->x));
      ran(s, LEVEL_END_RUN_C259);
      if (took(s, !zero(s))) return 0xc284;
      PORT_COVER(level_end_no_bonuses);
      set_data(s, 0x0044, 0);
      set_data(s, 0x0046, 0);
      ran(s, LEVEL_END_RUN_C279);
      return jsr(s, 0xc27f, 0xc56b);
    case 0xc282:
      ran(s, LEVEL_END_RUN_C282);
      s->k->taken++;
      return 0xc2ab;
    case 0xc284:
      set_data(s, 0x0044, c->a);
      add(s, 0x0008);
      set_data(s, 0x0046, c->a);
      lda(s, 0xc299);
      set_data(s, W_STATE, c->a);
      ran(s, LEVEL_END_RUN_C284);
      return jsr(s, 0xc294, 0xc56b);
    case 0xc297:
      ran(s, LEVEL_END_RUN_C297);
      s->k->taken++;
      return 0xc2ab;
    case 0xc299: {
      // Its state: the next bonus in the record.
      ldx(s, data(s, 0x0044));
      lda(s, far(s, 0x9f0000u + c->x));
      ran(s, LEVEL_END_RUN_C299);
      if (took(s, zero(s))) return 0xc2b1;
      inx(s);
      inx(s);
      set_data(s, 0x0044, c->x);
      ldx(s, c->a);
      ran(s, LEVEL_END_RUN_C2A2);
      const uint16_t handler = data(s, (uint16_t)(BONUSES + c->x));
      if (!bonus_ported(handler)) {
        // The harness cannot make a `JSR ($C346,X)`, so the whole stretch
        // is the 65816's.
        PORT_COVER(level_end_bonus_unported);
        s->k->lost = true;
        return stop(0xc2a8);
      }
      ran(s, LEVEL_END_RUN_C2A8);
      return jsr(s, 0xc2a8, handler);
    }
    case 0xc2ab:
      ran(s, LEVEL_END_RUN_C2AB);
      return jsr(s, 0xc2ab, 0xc2c6);
    case 0xc2ae:
      ran(s, LEVEL_END_RUN_C2AE);
      if (took(s, carry(s))) return 0xc2b1;
      return lone_rts(s);
    case 0xc2b1:
      PORT_COVER(level_end_bonuses_paid);
      ran(s, LEVEL_END_RUN_C2B1);
      return jsr(s, 0xc2b1, 0xd44e);
    case 0xc2b4:
      ran(s, LEVEL_END_RUN_C2B4);
      return jsr(s, 0xc2b4, 0xc2c6);
    case 0xc2b7:
      ldy(s, 0);
      ldx(s, 0xc369);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_C2B7);
      return stop(0xc2c0);
    case 0xc2c4:
      set_c(c, true);
      ran(s, LEVEL_END_RUN_C2C4);
      return rts(s);

    // --- `$82:C2C6`: the scores counting up ---------------------------------
    case 0xc2c6:
      set_data(s, 0x0038, 0);
      lda(s, data(s, 0x005e));
      cmp(s, data(s, W_SCORE_A_HI));
      ran(s, LEVEL_END_RUN_C2C6);
      if (took(s, !zero(s))) return 0xc2d9;
      lda(s, data(s, 0x005c));
      cmp(s, data(s, W_SCORE_A));
      ran(s, LEVEL_END_RUN_C2D1);
      if (took(s, zero(s))) return 0xc2f8;
      return 0xc2d9;
    case 0xc2d9:
      PORT_COVER(level_end_counts_up);
      count_up_at(s, 0x0038);
      pays(s, W_SCORE_A, 0x0100);
      lda(s, 0x000e);
      ran(s, LEVEL_END_RUN_C2D9);
      return stop(0xc2f4);
    case 0xc2f8:
      lda(s, data(s, 0x0062));
      cmp(s, data(s, W_SCORE_B_HI));
      ran(s, LEVEL_END_RUN_C2F8);
      if (took(s, !zero(s))) return 0xc308;
      lda(s, data(s, 0x0060));
      cmp(s, data(s, W_SCORE_B));
      ran(s, LEVEL_END_RUN_C300);
      if (took(s, zero(s))) return 0xc327;
      return 0xc308;
    case 0xc308:
      count_up_at(s, 0x0038);
      pays(s, W_SCORE_B, 0x0100);
      lda(s, 0x000e);
      ran(s, LEVEL_END_RUN_C308);
      return stop(0xc323);
    case 0xc327:
      lda(s, data(s, 0x0038));
      ran(s, LEVEL_END_RUN_C327);
      if (took(s, !zero(s))) return 0xc338;
      lda(s, data(s, 0x0044));
      cmp(s, data(s, 0x0046));
      ran(s, LEVEL_END_RUN_C32C);
      if (took(s, zero(s))) {
        set_c(c, true);
        ran(s, LEVEL_END_RUN_C336);
        return rts(s);
      }
      set_c(c, false);
      ran(s, LEVEL_END_RUN_C334);
      return rts(s);
    case 0xc338:
      ran(s, LEVEL_END_RUN_C338);
      return jsr(s, 0xc338, 0xbfd5);
    case 0xc33b:
      lda(s, 4);
      ran(s, LEVEL_END_RUN_C33B);
      return stop(0xc33e);
    case 0xc342:
      ran(s, LEVEL_END_RUN_C342);
      return 0xc2c6;

    // --- The bonuses' handlers ----------------------------------------------
    case 0xc499:
      // Ten neighbours saved by a player: 1,000 points.
      lda(s, data(s, W_SAVED));
      cmp(s, 0x000a);
      ran(s, LEVEL_END_RUN_C499);
      if (took(s, !carry(s))) return 0xc4ad;
      PORT_COVER(level_end_ten_saved);
      ldy(s, 0x1000);
      ldx(s, 0xc4c5);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_C4A1);
      return jsr(s, 0xc4aa, 0xd774);
    case 0xc4ad:
      lda(s, data(s, W_SAVED_B));
      cmp(s, 0x000a);
      ran(s, LEVEL_END_RUN_C4AD);
      if (took(s, !carry(s))) return lone_rts(s);
      ldy(s, 0x1000);
      ldx(s, 0xc517);
      lda(s, LEVEL_END_BANK);
      lda(s, c->a | 0x0400);
      ran(s, LEVEL_END_RUN_C4B5);
      return jsr(s, 0xc4c1, 0xd774);
    case 0xc4c4:
      return lone_rts(s);
    case 0xc56b:
      // Both playing: 1,000 points to whoever saved more.
      lda(s, data(s, W_PLAYING));
      lda(s, c->a & data(s, W_PLAYING_B));
      ran(s, LEVEL_END_RUN_C56B);
      if (took(s, !zero(s))) return 0xc574;
      return lone_rts(s);
    case 0xc574:
      lda(s, data(s, W_SAVED));
      cmp(s, data(s, W_SAVED_B));
      ran(s, LEVEL_END_RUN_C574);
      if (took(s, zero(s))) return lone_rts(s);
      ran(s, LEVEL_END_RUN_C57C);
      if (took(s, !carry(s))) return 0xc58a;
      PORT_COVER(level_end_more_saved);
      ldy(s, 0x1000);
      ldx(s, 0xc599);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_C57E);
      return 0xd774;
    case 0xc58a:
      ldy(s, 0x1000);
      ldx(s, 0xc5ed);
      lda(s, LEVEL_END_BANK);
      lda(s, c->a | 0x0400);
      ran(s, LEVEL_END_RUN_C58A);
      return 0xd774;
    case 0xce17:
      lda(s, data(s, W_PLAYING));
      ran(s, LEVEL_END_RUN_CE17);
      if (took(s, zero(s))) return 0xce36;
      lda(s, data(s, 0x1fe8));
      ran(s, LEVEL_END_RUN_CE1C);
      if (took(s, zero(s))) return 0xce36;
      add(s, data(s, 0x1fea));
      cmp(s, 0x0002);
      ran(s, LEVEL_END_RUN_CE21);
      if (took(s, !carry(s))) return 0xce36;
      PORT_COVER(level_end_frankenstein_bonus);
      ldy(s, 0x2000);
      ldx(s, 0xce59);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_CE2A);
      return jsr(s, 0xce33, 0xd774);
    case 0xce36:
      lda(s, data(s, W_PLAYING_B));
      ran(s, LEVEL_END_RUN_CE36);
      if (took(s, zero(s))) return lone_rts(s);
      lda(s, data(s, 0x1fea));
      ran(s, LEVEL_END_RUN_CE3B);
      if (took(s, zero(s))) return lone_rts(s);
      add(s, data(s, 0x1fe8));
      cmp(s, 0x0002);
      ran(s, LEVEL_END_RUN_CE40);
      if (took(s, !carry(s))) return lone_rts(s);
      ldy(s, 0x2000);
      ldx(s, 0xcead);
      lda(s, LEVEL_END_BANK);
      lda(s, c->a | 0x0400);
      ran(s, LEVEL_END_RUN_CE49);
      return jsr(s, 0xce55, 0xd774);
    case 0xce58:
      return lone_rts(s);
    case 0xcfeb:
      lda(s, data(s, W_PLAYING));
      ran(s, LEVEL_END_RUN_CFEB);
      if (took(s, zero(s))) return 0xd005;
      lda(s, data(s, 0x1fe4));
      add(s, data(s, 0x1fe6));
      ran(s, LEVEL_END_RUN_CFF0);
      if (took(s, zero(s))) return 0xd005;
      PORT_COVER(level_end_bonus_cfeb);
      ldy(s, 0x9900);
      ldx(s, 0xd023);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_CFF9);
      return jsr(s, 0xd002, 0xd774);
    case 0xd005:
      lda(s, data(s, W_PLAYING_B));
      ran(s, LEVEL_END_RUN_D005);
      if (took(s, zero(s))) return lone_rts(s);
      lda(s, data(s, 0x1fe6));
      add(s, data(s, 0x1fe4));
      ran(s, LEVEL_END_RUN_D00A);
      if (took(s, zero(s))) return lone_rts(s);
      ldy(s, 0x9900);
      ldx(s, 0xd077);
      lda(s, LEVEL_END_BANK);
      lda(s, c->a | 0x0400);
      ran(s, LEVEL_END_RUN_D013);
      return jsr(s, 0xd01f, 0xd774);
    case 0xd022:
      return lone_rts(s);
    case 0xd0cb: {
      lda(s, data(s, W_PLAYING));
      ran(s, LEVEL_END_RUN_D0CB);
      if (took(s, zero(s))) return 0xd0ef;
      lda(s, data(s, 0x1ff0));
      ran(s, LEVEL_END_RUN_D0D0);
      if (took(s, zero(s))) return 0xd0ef;
      lda(s, 0);
      set_data(s, W_AT, c->a);
      lda(s, data(s, W_LEVEL));
      doubled(s);
      ldx(s, c->a);
      ran(s, LEVEL_END_RUN_D0D5);
      const uint16_t to = data(s, (uint16_t)(LEVEL_BONUSES + c->x));
      if (!level_bonus_ported(to)) {
        s->k->lost = true;
        return stop(0xd0e0);
      }
      ran(s, LEVEL_END_RUN_D0E0);
      return jsr(s, 0xd0e0, to);
    }
    case 0xd0e3:
      PORT_COVER(level_end_level_bonus);
      ldy(s, 0x2000);
      ldx(s, 0xd25e);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_D0E3);
      return jsr(s, 0xd0ec, 0xd774);
    case 0xd0ef: {
      lda(s, data(s, W_PLAYING_B));
      ran(s, LEVEL_END_RUN_D0EF);
      if (took(s, zero(s))) return lone_rts(s);
      lda(s, data(s, 0x1ff2));
      ran(s, LEVEL_END_RUN_D0F4);
      if (took(s, zero(s))) return lone_rts(s);
      lda(s, 2);
      set_data(s, W_AT, c->a);
      lda(s, data(s, W_LEVEL));
      doubled(s);
      ldx(s, c->a);
      ran(s, LEVEL_END_RUN_D0F9);
      const uint16_t to = data(s, (uint16_t)(LEVEL_BONUSES + c->x));
      if (!level_bonus_ported(to)) {
        s->k->lost = true;
        return stop(0xd104);
      }
      ran(s, LEVEL_END_RUN_D104);
      return jsr(s, 0xd104, to);
    }
    case 0xd107:
      ldy(s, 0x2000);
      ldx(s, 0xd2b2);
      lda(s, LEVEL_END_BANK);
      lda(s, c->a | 0x0400);
      ran(s, LEVEL_END_RUN_D107);
      return jsr(s, 0xd113, 0xd774);
    case 0xd116:
    case 0xd117:
    case 0xd17d:
      return lone_rts(s);
    case 0xd118:
      lda(s, data(s, W_LEVEL));
      doubled(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(0xd17e + c->x)));
      ran(s, LEVEL_END_RUN_D118);
      if (took(s, zero(s))) return lone_rts(s);
      set_data(s, 0x1f50, c->a);
      ran(s, LEVEL_END_RUN_D122);
      return rts(s);
    case 0xd126:
      // Its words, and a wait for a pad; then back past the bonus.
      PORT_COVER(level_end_level_words);
      lda(s, data(s, W_AT));
      ran(s, LEVEL_END_RUN_D126);
      if (took(s, !zero(s))) return 0xd139;
      ldy(s, 0);
      ldx(s, 0xd30a);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_D12B);
      return jsr(s, 0xd134, 0xd774);
    case 0xd137:
      ran(s, LEVEL_END_RUN_D137);
      s->k->taken++;
      return 0xd148;
    case 0xd139:
      ldy(s, 0);
      ldx(s, 0xd35a);
      lda(s, LEVEL_END_BANK);
      lda(s, c->a | 0x0400);
      ran(s, LEVEL_END_RUN_D139);
      return jsr(s, 0xd145, 0xd774);
    case 0xd148:
      lda(s, 1);
      ran(s, LEVEL_END_RUN_D148);
      return stop(0xd14b);
    case 0xd14f:
      lda(s, data(s, W_PADS_A));
      lda(s, c->a | data(s, W_PADS_B));
      ran(s, LEVEL_END_RUN_D14F);
      if (took(s, zero(s))) return 0xd148;
      pla(s);
      ran(s, LEVEL_END_RUN_D157);
      return rts(s);
    case 0xd159:
      lda(s, data(s, W_AT));
      ran(s, LEVEL_END_RUN_D159);
      if (took(s, !zero(s))) return 0xd16c;
      ldy(s, 0x9900);
      ldx(s, 0xd3ae);
      lda(s, LEVEL_END_BANK);
      ran(s, LEVEL_END_RUN_D15E);
      return jsr(s, 0xd167, 0xd774);
    case 0xd16a:
      ran(s, LEVEL_END_RUN_D16A);
      s->k->taken++;
      return 0xd17b;
    case 0xd16c:
      ldy(s, 0x9900);
      ldx(s, 0xd3fe);
      lda(s, LEVEL_END_BANK);
      lda(s, c->a | 0x0400);
      ran(s, LEVEL_END_RUN_D16C);
      return jsr(s, 0xd178, 0xd774);
    case 0xd17b:
      pla(s);
      ran(s, LEVEL_END_RUN_D17B);
      return rts(s);

    // --- `$82:D44E`: the lives for the score --------------------------------
    case 0xd44e:
      set_data(s, W_AT, 0);
      ran(s, LEVEL_END_RUN_D44E);
      return 0xd451;
    case 0xd451:
      lda(s, data(s, W_AT));
      ldx(s, c->a);
      doubled(s);
      ldy(s, c->a);
      lda(s, data(s, (uint16_t)(W_SCORE_A_HI + c->y)));
      cmp(s, far(s, W_NEXT_LIFE + c->x));
      ran(s, LEVEL_END_RUN_D451);
      if (took(s, carry(s))) return 0xd463;
      ran(s, LEVEL_END_RUN_D460);
      return 0xd4d8;
    case 0xd463:
      lda(s, data(s, (uint16_t)(W_PLAYING + c->x)));
      ran(s, LEVEL_END_RUN_D463);
      if (took(s, zero(s))) {
        ran(s, LEVEL_END_RUN_D460);
        return 0xd4d8;
      }
      PORT_COVER(level_end_life_earned);
      ldx(s, data(s, W_AT));
      lda(s, far(s, W_NEXT_LIFE + c->x));
      set_c(c, false);
      adc_decimal(s, 0x0004);
      set_far(s, W_NEXT_LIFE + c->x, c->a);
      lda(s, data(s, W_SAVED));
      add(s, data(s, W_SAVED_B));
      cmp(s, 0x000a);
      ran(s, LEVEL_END_RUN_D468);
      if (took(s, carry(s))) return 0xd4b5;
      PORT_COVER(level_end_life_as_saved);
      ldy(s, c->x);
      ldx(s, data(s, (uint16_t)(0xd570 + c->y)));
      lda(s, LEVEL_END_BANK);
      ldy(s, 0);
      ran(s, LEVEL_END_RUN_D485);
      return jsr(s, 0xd48f, 0xd774);
    case 0xd492:
      ldx(s, data(s, W_AT));
      lda(s, data(s, (uint16_t)(W_SAVED + c->x)));
      lda(s, (uint16_t)(c->a + 1));
      set_data(s, (uint16_t)(W_SAVED + c->x), c->a);
      lda(s, 1);
      set_c(c, false);
      adc_decimal(s, data(s, 0x1d50));
      set_data(s, 0x1d50, c->a);
      lda(s, 1);
      set_c(c, false);
      adc_decimal(s, data(s, 0x1d52));
      set_data(s, 0x1d52, c->a);
      ran(s, LEVEL_END_RUN_D492);
      return 0xd4d8;
    case 0xd4b5:
      ldx(s, data(s, W_AT));
      lda(s, data(s, (uint16_t)(W_LIVES + c->x)));
      cmp(s, 0x0005);
      ran(s, LEVEL_END_RUN_D4B5);
      if (took(s, carry(s))) return 0xd4c9;
      lda(s, (uint16_t)(c->a + 1));
      set_data(s, (uint16_t)(W_LIVES + c->x), c->a);
      ran(s, LEVEL_END_RUN_D4C0);
      return jsr(s, 0xd4c4, 0xd4e9);
    case 0xd4c7:
      ran(s, LEVEL_END_RUN_D4C7);
      s->k->taken++;
      return 0xd4d8;
    case 0xd4c9:
      PORT_COVER(level_end_lives_full);
      ldy(s, data(s, W_AT));
      ldx(s, data(s, (uint16_t)(0xd6c8 + c->y)));
      lda(s, LEVEL_END_BANK);
      ldy(s, 0x5000);
      ran(s, LEVEL_END_RUN_D4C9);
      return jsr(s, 0xd4d5, 0xd774);
    case 0xd4d8:
      lda(s, data(s, W_AT));
      lda(s, (uint16_t)(c->a + 1));
      lda(s, (uint16_t)(c->a + 1));
      set_data(s, W_AT, c->a);
      cmp(s, 0x0004);
      ran(s, LEVEL_END_RUN_D4D8);
      if (took(s, carry(s))) return lone_rts(s);
      ran(s, LEVEL_END_RUN_D4E5);
      return 0xd451;
    case 0xd4e9:
      ldy(s, data(s, W_AT));
      ldx(s, data(s, (uint16_t)(0xd61c + c->y)));
      lda(s, LEVEL_END_BANK);
      ldy(s, 0);
      ran(s, LEVEL_END_RUN_D4E9);
      return jsr(s, 0xd4f5, 0xd774);
    case 0xd4f8:
      return stop(0xd4f8);
    case 0xd4fc: {
      // A life's record, beside the last.
      const uint16_t record = c->a;
      ldy(s, record);
      ldx(s, data(s, W_AT));
      lda(s, data(s, (uint16_t)(W_RECORDS + c->x)));
      ldx(s, c->a);
      const uint16_t last = c->x;
      lda(s, data(s, (uint16_t)(last + 0x02)));
      add(s, 0x0010);
      set_record(s, record, 0x02, c->a);
      lda(s, 0);
      set_record(s, record, 0x04, c->a);
      lda(s, data(s, (uint16_t)(last + 0x06)));
      set_record(s, record, 0x06, c->a);
      lda(s, data(s, (uint16_t)(last + 0x08)));
      set_record(s, record, 0x08, c->a);
      lda(s, data(s, (uint16_t)(last + 0x0a)));
      set_record(s, record, 0x0a, c->a);
      lda(s, data(s, 0x0008));
      set_record(s, record, 0x0c, c->a);
      lda(s, 0);
      set_record(s, record, 0x0e, c->a);
      lda(s, data(s, last));
      set_record(s, record, 0x00, c->a);
      ldx(s, data(s, W_AT));
      lda(s, record);
      set_data(s, (uint16_t)(W_RECORDS + c->x), c->a);
      lda(s, 5);
      set_data(s, W_EACH, c->a);
      ran(s, LEVEL_END_RUN_D4FC);
      return 0xd545;
    }
    case 0xd545:
      lda(s, 3);
      ran(s, LEVEL_END_RUN_D545);
      return stop(0xd548);
    case 0xd54c:
      // ...flashed: drawn and not, by turns.
      ldx(s, data(s, W_AT));
      lda(s, data(s, (uint16_t)(W_RECORDS + c->x)));
      ldx(s, c->a);
      lda(s, data(s, c->x));
      ran(s, LEVEL_END_RUN_D54C);
      if (took(s, minus(s))) {
        lda(s, c->a & 0x7fff);
        ran(s, LEVEL_END_RUN_D55D);
      } else {
        lda(s, c->a | 0x8000);
        ran(s, LEVEL_END_RUN_D558);
        s->k->taken++;
      }
      set_data(s, c->x, c->a);
      v = count_down_at(s, W_EACH);
      ran(s, LEVEL_END_RUN_D560);
      if (took(s, !negative(v))) return 0xd545;
      lda(s, 0x0028);
      ran(s, LEVEL_END_RUN_D568);
      return stop(0xd56b);
    case 0xd56f:
      return lone_rts(s);

    // --- `$82:D774`: a bonus paid -------------------------------------------
    case 0xd774:
      PORT_COVER(level_end_bonus_paid);
      phx(s);
      pha(s);
      // `XBA`: N and Z are the new low byte's.
      c->a = (uint16_t)(c->a << 8 | c->a >> 8);
      set_nz8(c, (uint8_t)c->a);
      lda(s, c->a & 0x00ff);
      ldx(s, c->a);
      lda(s, c->y);
      set_c(c, false);
      adc_decimal(s, data(s, (uint16_t)(W_TARGET + c->x)));
      set_data(s, (uint16_t)(W_TARGET + c->x), c->a);
      lda(s, 0);
      adc_decimal(s, data(s, (uint16_t)(W_TARGET + 2 + c->x)));
      set_data(s, (uint16_t)(W_TARGET + 2 + c->x), c->a);
      lda(s, 0x001b);
      cmp16(c, c->y, 0);
      ran(s, LEVEL_END_RUN_D774);
      if (!took(s, !zero(s))) {
        PORT_COVER(level_end_bonus_none);
        lda(s, 0x0003);
        ran(s, LEVEL_END_RUN_D796);
      }
      return stop(0xd799);
    case 0xd79d:
      lda(s, 6);
      set_data(s, W_COUNT, c->a);
      ran(s, LEVEL_END_RUN_D79D);
      return 0xd7a3;
    case 0xd7a3: {
      lda(s, data(s, W_COUNT));
      lda(s, c->a & 0x0002);
      ldy(s, c->a);
      // `LDA ($03,S),Y`: the table its caller left in X, pushed.
      const uint16_t table = wram_r16(s->w, (uint16_t)(c->s + 3));
      lda(s, data(s, (uint16_t)(table + c->y)));
      ldx(s, c->a);
      pla(s);
      pha(s);
      ldy(s, 0);
      ran(s, LEVEL_END_RUN_D7A3);
      return stop(0xd7b2);
    }
    case 0xd7b6:
      lda(s, 5);
      ran(s, LEVEL_END_RUN_D7B6);
      return stop(0xd7b9);
    case 0xd7bd:
      count_down_at(s, W_COUNT);
      v = count_down_at(s, W_COUNT);
      ran(s, LEVEL_END_RUN_D7BD);
      if (took(s, !negative(v))) return 0xd7a3;
      pla(s);
      plx(s);
      lda(s, 0x001e);
      ran(s, LEVEL_END_RUN_D7C5);
      return stop(0xd7ca);
    case 0xd7ce:
      return lone_rts(s);
  }
  s->k->lost = true;
  return stop(pc);
}

void level_end_run(Wram* w, const Rom* rom, PortCpu* c, LevelEndWork* k) {
  End s = {w, rom, c, k};
  k->v_known = true;
  uint32_t pc = c->pc & 0xffffu;
  for (int i = 0; i < 4096; i++) {
    pc = block(&s, (uint16_t)pc);
    if (pc & STOP) {
      c->pc = pc & 0xffffffu;
      return;
    }
  }
  k->lost = true;
}
