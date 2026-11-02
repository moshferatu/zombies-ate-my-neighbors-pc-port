// The giant spider of levels 36 and 48 -- see port/giant_spider.h.

#include "port/giant_spider.h"

#include "port/coverage.h"
#include "port/oam.h"
#include "port/rng.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  GiantSpiderWork* k;
} GiantSpider;

// A block hands back the 24-bit address it goes to next, or `STOP` and the
// instruction it stopped on.
#define STOP 0x01000000u
#define AT(a) (0x830000u | (a))

// --- Its words ---------------------------------------------------------------

#define W_LOAD 0x00deu           // the level's load
#define W_FRAME 0x0008u          // copied into a record as it is made
#define W_TICK 0x0020u
#define W_RECORDS 0x0006u        // how many records there are
#define W_LEVEL 0x1e7cu
#define W_SOUNDS 0x1f52u
#define W_PLANE_X 0x1e62u        // where it is on its layer...
#define W_PLANE_Y 0x1e64u
#define W_PLANE_DX 0x1e66u       // ...the picture's offsets...
#define W_PLANE_DY 0x1e68u
#define W_PLANE_AT_X 0x1e6au     // ...and where on the layer it is drawn
#define W_PLANE_AT_Y 0x1e6cu
#define W_PLANE_SCROLL_X 0x1e6eu
#define W_PLANE_SCROLL_Y 0x1e70u
#define W_ONE_SRC 0x1e58u        // `boss_bg_queue_one`'s four words
#define W_ONE_BANK 0x1e5au
#define W_ONE_BYTES 0x1e56u
#define W_ONE_VRAM 0x1e5cu
#define W_FIGURE 0x0028u         // the figure's far address, for the queue
#define W_FIGURE_BANK 0x002au
#define W_BEARING_X 0x00a2u      // `$83:A8B5`'s two words
#define W_BEARING_Y 0x00a4u
#define W_BOX 0x0038u            // `actor_notify_box`'s box: four words...
#define W_BOX_KIND 0x0040u       // ...and what it is
#define W7E_MOSAIC 0x6db2u       // in bank `$7E`
#define W7E_SNAKEOID_UP 0x6058u
#define LEVEL_36 0x0024u

// Its page.
#define DP_AT_X 0x00             // the point it was given, and a place to
#define DP_AT_Y 0x02             // begin a thread at
#define DP_SCRATCH 0x04
#define DP_SCRATCH2 0x06
#define DP_RECORD GIANT_SPIDER_DP_RECORD
#define DP_PART 0x0a             // the record its mouth is
#define DP_LIVES GIANT_SPIDER_DP_LIVES
#define DP_WEBS GIANT_SPIDER_DP_WEBS
#define DP_WEB GIANT_SPIDER_DP_WEB   // the record, or none
#define DP_WEB_LEFT 0x1a         // frames to fly; negative, of its spreading
#define DP_WEB_DX 0x1c
#define DP_WEB_DY 0x1e
#define DP_SCRIPT 0x30           // the mouth's script, and its frames
#define DP_SCRIPT_LEFT 0x32
#define DP_PLAYS 0x34            // the part a little script plays on...
#define DP_PLAYS_SCRIPT 0x36
#define DP_PLAYS_LEFT 0x38
#define DP_WEBBING 0x3a            // frames of webbing left
#define DP_X GIANT_SPIDER_DP_X
#define DP_Y GIANT_SPIDER_DP_Y
#define DP_COUNT 0x40
#define DP_WAY 0x42              // of eight, doubled, and one on
#define DP_STATE GIANT_SPIDER_DP_STATE
#define DP_PICTURE 0x48          // which of four it wants...
#define DP_SHOWN 0x4a            // ...and which is up
#define DP_PICTURE_TICKS 0x4c
#define DP_PICTURE_LEFT 0x4e
#define DP_FACING 0x50
#define DP_BROKEN GIANT_SPIDER_DP_BROKEN
#define DP_HEALTH GIANT_SPIDER_DP_HEALTH
#define DP_HIT GIANT_SPIDER_DP_HIT
#define DP_FLASH GIANT_SPIDER_DP_FLASH
#define DP_HIT_BY GIANT_SPIDER_DP_HIT_BY
#define DP_TO_X 0x5c             // where it would go, or a tile
#define DP_TO_Y 0x5e
#define DP_PROBE_X 0x60
#define DP_PROBE_Y 0x62
#define DP_STUNG GIANT_SPIDER_DP_STUNG
#define DP_TEMP 0x66

// A record's words.
#define REC_FLAGS 0x00
#define REC_X 0x02
#define REC_X_FRACTION 0x04
#define REC_Y 0x06
#define REC_PICTURES 0x08
#define REC_PICTURES_BANK 0x0a
#define REC_BORN 0x0c
#define REC_COLLIDE_ID 0x0e
#define REC_PALETTE 0x10

// Its tables, in bank `$83`.
#define T_WAY_STEP 0xa3cau       // a way's step across and down, 4 apart...
#define T_WAY_PICTURE 0xa3eau    // ...and the mouth's picture for it
#define T_PART_AT 0xa40eu        // each part's place, 4 apart
#define T_FIGURES 0xa478u        // a far address a picture
#define T_FIGURE_AT 0xa48cu      // and its offsets
#define T_PICTURE_FIGURE 0xa512u
#define T_PLAYS_PART 0xa613u     // the part a little script plays on...
#define T_PLAYS_SCRIPT 0xa615u   // ...and the script
#define T_WEB_TILES 0xa755u      // four tiles about a web, 8 apart...
#define T_WEB_TILE 0xa759u       // ...and the web tile, by level
#define T_SPREAD_PICTURES 0xa795u
#define T_WEB_WAY 0xa814u       // a way's step, pictures and flags, 8 apart
#define T_BEARING 0x83a8e0u      // long: a way of eight by the two signs
#define T_PROBES 0xa970u         // its five probes, 4 apart
#define T_SPIDERS_AT 0xaba5u
#define T_MOUTH_SCRIPT 0xabb5u
#define T_PART_PICTURES 0xac8du  // each part's pictures, and its flags
#define T_BLINK 0xaef7u
#define T_RISE 0xaefbu
#define T_PUFFS_AT 0xb0acu

// --- The machine, as these blocks use it ------------------------------------

static uint16_t field(const GiantSpider* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->c->d + at));
}

static void set_field(GiantSpider* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->c->d + at), v);
}

// A word through the data bank, `$83`: low memory, or a table of its own.
static uint16_t data(const GiantSpider* s, uint16_t at) {
  return bus_r16(s->w, s->rom, (uint32_t)s->c->db << 16 | at);
}

// ...and one written, which is always low memory.
static void set_data(GiantSpider* s, uint16_t at, uint16_t v) {
  if (at >= 0x1fffu) {
    s->k->lost = true;  // somewhere this file does not write
    return;
  }
  wram_w16(s->w, at, v);
}

static uint16_t w7e(const GiantSpider* s, uint16_t at) { return wram_r16(s->w, at); }
static void set_w7e(GiantSpider* s, uint16_t at, uint16_t v) { wram_w16(s->w, at, v); }

static void ran(GiantSpider* s, int run) { hw_run(&s->k->trace, run); }
static void taken(GiantSpider* s) { hw_run(&s->k->trace, GIANT_SPIDER_RUN_TAKEN); }

static bool zero(const GiantSpider* s) { return flag(s->c, PORT_P_Z); }
static bool carry(const GiantSpider* s) { return flag(s->c, PORT_P_C); }
static bool minus(const GiantSpider* s) { return flag(s->c, PORT_P_N); }

static void lda(GiantSpider* s, uint16_t v) {
  s->c->a = v;
  set_nz16(s->c, v);
}

static void ldx(GiantSpider* s, uint16_t v) {
  s->c->x = v;
  set_nz16(s->c, v);
}

static void ldy(GiantSpider* s, uint16_t v) {
  s->c->y = v;
  set_nz16(s->c, v);
}

static void and_(GiantSpider* s, uint16_t v) { lda(s, s->c->a & v); }
static void ora(GiantSpider* s, uint16_t v) { lda(s, s->c->a | v); }
static void eor(GiantSpider* s, uint16_t v) { lda(s, s->c->a ^ v); }
static void inc_a(GiantSpider* s) { lda(s, (uint16_t)(s->c->a + 1)); }
static void dec_a(GiantSpider* s) { lda(s, (uint16_t)(s->c->a - 1)); }
static void asl_a(GiantSpider* s) { s->c->a = asl16(s->c, s->c->a); }

static void lsr_a(GiantSpider* s) {
  set_c(s->c, (s->c->a & 1) != 0);
  lda(s, s->c->a >> 1);
}

// `ADC` and `SBC` with the carry as it stands, and the `CLC`/`SEC` forms.
// Each says what overflow is.
static void adc(GiantSpider* s, uint16_t v) {
  s->c->a = adc16(s->c, s->c->a, v);
  s->k->v_unknown = false;
}

static void sbc(GiantSpider* s, uint16_t v) {
  s->c->a = sbc16(s->c, s->c->a, v);
  s->k->v_unknown = false;
}

static void add(GiantSpider* s, uint16_t v) {
  set_c(s->c, false);
  adc(s, v);
}

static void sub(GiantSpider* s, uint16_t v) {
  set_c(s->c, true);
  sbc(s, v);
}

static void cmp(GiantSpider* s, uint16_t v) { cmp16(s->c, s->c->a, v); }
static void cpx(GiantSpider* s, uint16_t v) { cmp16(s->c, s->c->x, v); }
static void cpy(GiantSpider* s, uint16_t v) { cmp16(s->c, s->c->y, v); }

// `BIT #`: only Z.
static void bit_imm(GiantSpider* s, uint16_t v) {
  s->c->p = (uint8_t)((s->c->a & v) == 0 ? s->c->p | PORT_P_Z
                                         : s->c->p & ~PORT_P_Z);
}

// `BIT` of a word on the page: N, V and Z.
static void bit_field(GiantSpider* s, uint16_t at) {
  bit16(s->c, field(s, at));
  s->k->v_unknown = false;
}

// `DEC` and `INC` of a word on the page.
static uint16_t dec_field(GiantSpider* s, uint16_t at) {
  const uint16_t v = (uint16_t)(field(s, at) - 1);
  set_field(s, at, v);
  set_nz16(s->c, v);
  return v;
}

static uint16_t inc_field(GiantSpider* s, uint16_t at) {
  const uint16_t v = (uint16_t)(field(s, at) + 1);
  set_field(s, at, v);
  set_nz16(s->c, v);
  return v;
}

// A `JSR` at `at`, going to `to`.
static uint32_t jsr(GiantSpider* s, uint16_t at, uint16_t to) {
  push16(s->w, s->c, (uint16_t)(at + 2));
  return AT(to);
}

// An `RTS`: where it goes is what it pulls, and one on.
static uint32_t rts(GiantSpider* s) {
  return AT((uint16_t)(pull16(s->w, s->c) + 1));
}

static uint32_t stop(uint32_t at) { return STOP | at; }

// A `JSL` at `at` made here, and its `RTL`.
static void jsl(GiantSpider* s, uint16_t at) {
  push8(s->w, s->c, GIANT_SPIDER_BANK);
  push16(s->w, s->c, (uint16_t)(at + 3));
}

static void rtl(GiantSpider* s) {
  pull16(s->w, s->c);
  pull8(s->w, s->c);
}

// `JSL rng_next` at `at`.
static void draw(GiantSpider* s, uint16_t at) {
  RngResult r;
  jsl(s, at);
  rng_next(s->w, carry(s), &r);
  rtl(s);
  ran(s, r.v ? GIANT_SPIDER_RUN_RNG_TWICE : GIANT_SPIDER_RUN_RNG_ONCE);
  s->c->a = r.a;
  set_nz16(s->c, r.a);
  s->c->p = (uint8_t)(r.n ? s->c->p | PORT_P_N : s->c->p & ~PORT_P_N);
  s->c->p = (uint8_t)(r.z ? s->c->p | PORT_P_Z : s->c->p & ~PORT_P_Z);
  set_c(s->c, r.c);
  set_v(s->c, r.v);
  s->k->v_unknown = false;
}

// `$83:A97C`, by the `JSR` at `$83:A917`: would its probe stand at X and Y?
// The Snakeoid's `$82:A214`, which pushes its page and the point less 17,
// and pulls both.
static void wide(GiantSpider* s) {
  PortCpu* c = s->c;
  TerrainRegs r;
  TerrainWideWork work = {{0}};
  push16(s->w, c, c->d);
  push16(s->w, c, (uint16_t)(c->x - 0x0011u));
  pull16(s->w, c);
  terrain_blocked_wide_low_counted(s->w, c->x, c->y, &r, &work);
  pull16(s->w, c);
  for (int i = 0; i < WIDE_BLOCK_COUNT; i++)
    hw_run_n(&s->k->trace, GIANT_SPIDER_RUN_WIDE + i, work.blocks[i]);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_nz16(c, c->d);  // `PLD`
  set_c(c, r.blocked);
  s->k->v_unknown = true;
}

// `LDA #pc : LDY #bank`, as each call that names a routine has it.
static void names(GiantSpider* s, uint16_t pc, uint16_t bank) {
  lda(s, pc);
  ldy(s, bank);
}

// `LDA table,X : CLC : ADC $3C : STA $00`, and the same down: a place about
// it, at offsets four bytes apart.
static void place_about(GiantSpider* s, uint16_t table, uint16_t x) {
  lda(s, data(s, (uint16_t)(table + x)));
  add(s, field(s, DP_X));
  set_field(s, DP_AT_X, s->c->a);
  lda(s, data(s, (uint16_t)(table + 2 + x)));
  add(s, field(s, DP_Y));
  set_field(s, DP_AT_Y, s->c->a);
}

// `LDA $40 : SEC : SBC #by : STA $40 : BPL`, the loops' count.
static bool count_down(GiantSpider* s, uint16_t by) {
  lda(s, field(s, DP_COUNT));
  sub(s, by);
  set_field(s, DP_COUNT, s->c->a);
  return !minus(s);
}

// --- The thread --------------------------------------------------------------

static uint32_t block(GiantSpider* s, uint32_t pc) {
  PortCpu* c = s->c;
  switch (pc) {
    // --- It rises -----------------------------------------------------------
    case AT(0xad36):
      PORT_COVER(giant_spider_wakes);
      set_c(c, false);
      lda(s, data(s, W_LOAD));
      adc(s, 0x0028);
      set_data(s, W_LOAD, c->a);
      ran(s, GIANT_SPIDER_RUN_AD36);
      return jsr(s, 0xad40, 0xaca5);

    // `$83:ACA5`: its colours, its tiles, and where it is.
    case AT(0xaca5):
      names(s, 0xfafc, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_ACA5);
      return stop(AT(0xacab));
    case AT(0xacaf):
      names(s, 0xfafc, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_ACAF);
      return stop(AT(0xacb5));
    case AT(0xacb9):
      set_data(s, W_ONE_SRC, 0xa800);
      set_data(s, W_ONE_BANK, 0x0092);
      set_data(s, W_ONE_BYTES, 0x2800);
      set_data(s, W_ONE_VRAM, 0x5000);
      names(s, 0x8259, 0x0082);
      ran(s, GIANT_SPIDER_RUN_ACB9);
      return stop(AT(0xacd7));
    case AT(0xacdb):
      lda(s, 0x0001);
      ran(s, GIANT_SPIDER_RUN_ACDB);
      return stop(AT(0xacde));
    case AT(0xace2):
      lda(s, data(s, W_ONE_BYTES));
      ran(s, GIANT_SPIDER_RUN_ACE2);
      if (!zero(s)) {
        PORT_COVER(giant_spider_tiles_wait);
        taken(s);
        return AT(0xacdb);
      }
      PORT_COVER(giant_spider_tiles_sent);
      set_data(s, W_PLANE_AT_X, 0x003c);
      set_data(s, W_PLANE_AT_Y, 0x005a);
      set_field(s, DP_X, field(s, DP_AT_X));
      set_field(s, DP_Y, field(s, DP_AT_Y));
      set_field(s, DP_COUNT, 0);
      set_field(s, DP_FACING, 0);
      set_field(s, DP_BROKEN, 0);
      set_field(s, DP_HIT, 0);
      set_field(s, DP_FLASH, 0);
      set_field(s, DP_PICTURE_LEFT, 0);
      set_field(s, DP_PLAYS, 0);
      set_field(s, DP_SCRIPT, 0);
      set_field(s, DP_PICTURE_TICKS, 0x0002);
      lda(s, 0x0008);
      set_field(s, DP_LIVES, c->a);
      ran(s, GIANT_SPIDER_RUN_ACE7);
      return rts(s);

    // `$83:AD98`: its record, blinking until a player is near.
    case AT(0xad43):
      ran(s, GIANT_SPIDER_RUN_AD43);
      return jsr(s, 0xad43, 0xad98);
    case AT(0xad98):
      ran(s, GIANT_SPIDER_RUN_AD98);
      return jsr(s, 0xad98, 0xaeff);
    case AT(0xaeff):
      return stop(AT(0xaeff));
    case AT(0xaf03): {
      PORT_COVER(giant_spider_record_made);
      set_field(s, DP_RECORD, c->a);
      ldx(s, c->a);
      const uint16_t x = c->x;
      set_data(s, (uint16_t)(x + REC_X), field(s, DP_AT_X));
      set_data(s, (uint16_t)(x + REC_X_FRACTION), 0);
      set_data(s, (uint16_t)(x + REC_Y), field(s, DP_AT_Y));
      set_data(s, (uint16_t)(x + REC_PICTURES_BANK), 0x008f);
      set_data(s, (uint16_t)(x + REC_PICTURES), 0xf9e8);
      set_data(s, (uint16_t)(x + REC_COLLIDE_ID), 0x0003);
      set_data(s, (uint16_t)(x + REC_BORN), data(s, W_FRAME));
      set_data(s, (uint16_t)(x + REC_PALETTE), 0x00c0);
      lda(s, (uint16_t)(0x8008u | data(s, (uint16_t)(x + REC_FLAGS))));
      set_data(s, (uint16_t)(x + REC_FLAGS), c->a);
      lda(s, 0);
      ldy(s, 0);  // `TAY`
      ran(s, GIANT_SPIDER_RUN_AF03);
      return stop(AT(0xaf3e));
    }
    case AT(0xaf42):
      ran(s, GIANT_SPIDER_RUN_AF42);
      return rts(s);
    case AT(0xad9b):
      set_field(s, DP_COUNT, 0);
      ran(s, GIANT_SPIDER_RUN_AD9B);
      return AT(0xad9d);
    case AT(0xad9d):
      lda(s, 0x0041);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, GIANT_SPIDER_RUN_AD9D);
      return stop(AT(0xada4));
    case AT(0xada8):
      ldx(s, c->a);
      ran(s, GIANT_SPIDER_RUN_ADA8);
      if (!zero(s)) {
        PORT_COVER(giant_spider_seen);
        taken(s);
        return AT(0xadcf);
      }
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      ran(s, GIANT_SPIDER_RUN_ADAB);
      if (!minus(s)) {
        PORT_COVER(giant_spider_blinks);
        taken(s);
        return AT(0xadb6);
      }
      PORT_COVER(giant_spider_blink_heard);
      ran(s, GIANT_SPIDER_RUN_ADB0);
      return jsr(s, 0xadb0, 0xaf43);

    // `$83:AF43`: a sound, only while `$1F52` is 3.
    case AT(0xaf43):
      lda(s, data(s, W_SOUNDS));
      cmp(s, 0x0003);
      ran(s, GIANT_SPIDER_RUN_AF43);
      if (!zero(s)) {
        PORT_COVER(giant_spider_quiet);
        taken(s);
        return AT(0xaf52);
      }
      PORT_COVER(giant_spider_heard);
      lda(s, 0x0027);
      ran(s, GIANT_SPIDER_RUN_AF4B);
      return stop(AT(0xaf4e));
    case AT(0xaf52):
      ran(s, GIANT_SPIDER_RUN_AF52);
      return rts(s);

    case AT(0xadb3):
      lda(s, 0x002c);
      ran(s, GIANT_SPIDER_RUN_ADB3);
      return AT(0xadb6);
    case AT(0xadb6):
      set_field(s, DP_COUNT, c->a);
      and_(s, 0x0008);
      lsr_a(s);
      lsr_a(s);
      ldx(s, c->a);
      ldy(s, field(s, DP_RECORD));
      lda(s, data(s, (uint16_t)(T_BLINK + c->x)));
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      lda(s, 0x0005);
      ran(s, GIANT_SPIDER_RUN_ADB6);
      return stop(AT(0xadc9));
    case AT(0xadcd):
      ran(s, GIANT_SPIDER_RUN_ADCD);
      taken(s);
      return AT(0xad9d);

    // A player near: its cries, and it is put up on its layer.
    case AT(0xadcf):
      lda(s, 0x0005);
      ran(s, GIANT_SPIDER_RUN_ADCF);
      return stop(AT(0xadd2));
    case AT(0xadd6):
      lda(s, 0xb65a);
      ran(s, GIANT_SPIDER_RUN_ADD6);
      return stop(AT(0xadd9));
    case AT(0xaddd):
      lda(s, 0x0023);
      ran(s, GIANT_SPIDER_RUN_ADDD);
      return stop(AT(0xade0));
    case AT(0xade4):
      lda(s, 0xb678);
      ran(s, GIANT_SPIDER_RUN_ADE4);
      return stop(AT(0xade7));
    case AT(0xadeb):
      lda(s, field(s, DP_X));
      sub(s, 0x0011);
      set_data(s, W_PLANE_X, c->a);
      lda(s, field(s, DP_Y));
      sub(s, 0x0012);
      set_data(s, W_PLANE_Y, c->a);
      ran(s, GIANT_SPIDER_RUN_ADEB);
      return jsr(s, 0xadfd, 0xa51a);

    // `$83:A51A`: the layer's scroll, from where it is and is drawn.
    case AT(0xa51a):
      lda(s, data(s, W_PLANE_DX));
      add(s, data(s, W_PLANE_X));
      sub(s, data(s, W_PLANE_AT_X));
      set_data(s, W_PLANE_SCROLL_X, c->a);
      lda(s, data(s, W_PLANE_DY));
      add(s, data(s, W_PLANE_Y));
      sub(s, data(s, W_PLANE_AT_Y));
      set_data(s, W_PLANE_SCROLL_Y, c->a);
      ran(s, GIANT_SPIDER_RUN_A51A);
      return rts(s);

    case AT(0xae00):
      lda(s, 0x0004);
      ran(s, GIANT_SPIDER_RUN_AE00);
      return jsr(s, 0xae03, 0xa448);

    // `$83:A448`: picture A on its layer, mirrored when `$50` is negative.
    case AT(0xa448): {
      asl_a(s);
      asl_a(s);
      ldx(s, c->a);
      const uint16_t x = c->x;
      lda(s, data(s, (uint16_t)(T_FIGURES + x)));
      set_data(s, W_FIGURE, c->a);
      lda(s, data(s, (uint16_t)(T_FIGURES + 2 + x)));
      set_data(s, W_FIGURE_BANK, c->a);
      lda(s, data(s, (uint16_t)(T_FIGURE_AT + x)));
      bit_field(s, DP_FACING);
      ran(s, GIANT_SPIDER_RUN_A448);
      if (!minus(s)) {
        taken(s);
      } else {
        PORT_COVER(giant_spider_picture_mirrored);
        eor(s, 0xffff);
        inc_a(s);
        ran(s, GIANT_SPIDER_RUN_A45E);
      }
      set_data(s, W_PLANE_DX, c->a);
      lda(s, data(s, (uint16_t)(T_FIGURE_AT + 2 + x)));
      set_data(s, W_PLANE_DY, c->a);
      lda(s, field(s, DP_FACING));
      ran(s, GIANT_SPIDER_RUN_A462);
      if (!zero(s)) {
        PORT_COVER(giant_spider_picture_flips);
        taken(s);
        ran(s, GIANT_SPIDER_RUN_A474);
        return jsr(s, 0xa474, 0xa437);
      }
      PORT_COVER(giant_spider_picture_shown);
      ran(s, GIANT_SPIDER_RUN_A46F);
      return jsr(s, 0xa46f, 0xa426);
    }
    case AT(0xa426):
    case AT(0xa437):
      lda(s, 0x0050);
      set_data(s, W_PLANE_AT_X, c->a);
      lda(s, 0x0050);
      set_data(s, W_PLANE_AT_Y, c->a);
      ran(s, pc == AT(0xa426) ? GIANT_SPIDER_RUN_A426 : GIANT_SPIDER_RUN_A437);
      return stop(pc == AT(0xa426) ? AT(0xa432) : AT(0xa443));
    case AT(0xa436):
      ran(s, GIANT_SPIDER_RUN_A436);
      return rts(s);
    case AT(0xa472):
      ran(s, GIANT_SPIDER_RUN_A472);
      taken(s);
      return AT(0xa477);
    case AT(0xa447):
      ran(s, GIANT_SPIDER_RUN_A447);
      return rts(s);
    case AT(0xa477):
      ran(s, GIANT_SPIDER_RUN_A477);
      return rts(s);

    // Two pictures by turns, twelve ticks each, with its cry.
    case AT(0xae06):
      lda(s, 0x0002);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_AE06);
      return AT(0xae0b);
    case AT(0xae0b):
      lda(s, 0x0023);
      ran(s, GIANT_SPIDER_RUN_AE0B);
      return stop(AT(0xae0e));
    case AT(0xae12):
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      ran(s, GIANT_SPIDER_RUN_AE12);
      if (minus(s)) {
        taken(s);
        return AT(0xae2f);
      }
      PORT_COVER(giant_spider_rise_turn);
      set_field(s, DP_COUNT, c->a);
      and_(s, 0x0001);
      asl_a(s);
      ldx(s, c->a);
      ldy(s, field(s, DP_RECORD));
      lda(s, data(s, (uint16_t)(T_RISE + c->x)));
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      lda(s, 0x000c);
      ran(s, GIANT_SPIDER_RUN_AE17);
      return stop(AT(0xae29));
    case AT(0xae2d):
      ran(s, GIANT_SPIDER_RUN_AE2D);
      taken(s);
      return AT(0xae12);
    case AT(0xae2f):
      ran(s, GIANT_SPIDER_RUN_AE2F);
      return jsr(s, 0xae2f, 0xaf43);

    // Coarser, a step every five ticks, with puffs.
    case AT(0xae32):
      lda(s, 0);
      set_w7e(s, W7E_MOSAIC, c->a);
      lda(s, 0x000f);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_AE32);
      return AT(0xae3e);
    case AT(0xae3e):
    case AT(0xaea3): {
      const bool coarser = pc == AT(0xae3e);
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      ran(s, coarser ? GIANT_SPIDER_RUN_AE3E : GIANT_SPIDER_RUN_AEA3);
      if (minus(s)) {
        taken(s);
        return coarser ? AT(0xae8f) : AT(0xaef4);
      }
      set_field(s, DP_COUNT, c->a);
      names(s, 0xb0ec, GIANT_SPIDER_BANK);
      ran(s, coarser ? GIANT_SPIDER_RUN_AE43 : GIANT_SPIDER_RUN_AEA8);
      return stop(coarser ? AT(0xae4b) : AT(0xaeb0));
    }
    case AT(0xae4f):
    case AT(0xaeb4): {
      const bool coarser = pc == AT(0xae4f);
      lda(s, (uint16_t)(w7e(s, W7E_MOSAIC) + (coarser ? 1 : -1)));
      set_w7e(s, W7E_MOSAIC, c->a);
      lda(s, 0x0005);
      ran(s, coarser ? GIANT_SPIDER_RUN_AE4F : GIANT_SPIDER_RUN_AEB4);
      return stop(coarser ? AT(0xae5b) : AT(0xaec0));
    }
    case AT(0xae5f):
    case AT(0xaec4): {
      const bool coarser = pc == AT(0xae5f);
      ran(s, coarser ? GIANT_SPIDER_RUN_AE5F : GIANT_SPIDER_RUN_AEC4);
      draw(s, coarser ? 0xae5f : 0xaec4);
      cmp(s, coarser ? 0x00c8 : 0x00a0);
      ran(s, coarser ? GIANT_SPIDER_RUN_AE63 : GIANT_SPIDER_RUN_AEC8);
      if (carry(s)) {
        PORT_COVER(giant_spider_rise_no_puff);
        taken(s);
        return coarser ? AT(0xae3e) : AT(0xaea3);
      }
      PORT_COVER(giant_spider_rise_puff);
      lda(s, field(s, DP_COUNT));
      asl_a(s);
      asl_a(s);
      ldx(s, c->a);
      place_about(s, T_PUFFS_AT, c->x);
      ran(s, coarser ? GIANT_SPIDER_RUN_AE68 : GIANT_SPIDER_RUN_AECD);
      return stop(coarser ? AT(0xae7d) : AT(0xaee2));
    }
    case AT(0xae81):
    case AT(0xaee6): {
      const bool coarser = pc == AT(0xae81);
      ran(s, coarser ? GIANT_SPIDER_RUN_AE81 : GIANT_SPIDER_RUN_AEE6);
      if (carry(s)) {
        PORT_COVER(giant_spider_rise_no_room);
        taken(s);
        return coarser ? AT(0xae8d) : AT(0xaef2);
      }
      names(s, 0xb1dd, GIANT_SPIDER_BANK);
      ran(s, coarser ? GIANT_SPIDER_RUN_AE83 : GIANT_SPIDER_RUN_AEE8);
      return stop(coarser ? AT(0xae89) : AT(0xaeee));
    }
    case AT(0xae8d):
      ran(s, GIANT_SPIDER_RUN_AE8D);
      taken(s);
      return AT(0xae3e);
    case AT(0xaef2):
      ran(s, GIANT_SPIDER_RUN_AEF2);
      taken(s);
      return AT(0xaea3);

    // Its picture shown again, its record freed, and finer.
    case AT(0xae8f):
      ran(s, GIANT_SPIDER_RUN_AE8F);
      return jsr(s, 0xae8f, 0xa537);
    case AT(0xae92):
      lda(s, 0);
      ran(s, GIANT_SPIDER_RUN_AE92);
      return jsr(s, 0xae95, 0xa448);
    case AT(0xae98):
      lda(s, field(s, DP_RECORD));
      ran(s, GIANT_SPIDER_RUN_AE98);
      return stop(AT(0xae9a));
    case AT(0xae9e):
      lda(s, 0x000f);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_AE9E);
      return AT(0xaea3);
    case AT(0xaef4):
      set_field(s, DP_COUNT, 0);
      ran(s, GIANT_SPIDER_RUN_AEF4);
      return rts(s);

    // --- `$83:AD16`: its six parts, its health and its handler --------------
    case AT(0xad46):
      ran(s, GIANT_SPIDER_RUN_AD46);
      return jsr(s, 0xad46, 0xad16);
    case AT(0xad16):
      ran(s, GIANT_SPIDER_RUN_AD16);
      return jsr(s, 0xad16, 0xa661);
    case AT(0xa661):
      ldx(s, 0x0010);
      ldy(s, GIANT_SPIDER_WEB_NONE);
      ran(s, GIANT_SPIDER_RUN_A661);
      for (;;) {
        set_field(s, (uint16_t)(DP_WEB + c->x), c->y);
        lda(s, c->x);
        sub(s, 0x0008);
        ldx(s, c->a);
        ran(s, GIANT_SPIDER_RUN_A669);
        if (minus(s)) break;
        taken(s);
      }
      ran(s, GIANT_SPIDER_RUN_A673);
      return rts(s);
    case AT(0xad19):
      ran(s, GIANT_SPIDER_RUN_AD19);
      return jsr(s, 0xad19, 0xac36);
    case AT(0xac36):
      lda(s, 0x000a);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_AC36);
      return AT(0xac3b);
    case AT(0xac3b):
      return stop(AT(0xac3b));
    case AT(0xac3f): {
      ldx(s, field(s, DP_COUNT));
      set_field(s, (uint16_t)(DP_RECORD + c->x), c->a);
      ldx(s, c->a);
      const uint16_t x = c->x;
      lda(s, field(s, DP_COUNT));
      asl_a(s);
      ldy(s, c->a);
      const uint16_t y = c->y;
      lda(s, field(s, DP_X));
      add(s, data(s, (uint16_t)(T_PART_AT + y)));
      set_data(s, (uint16_t)(x + REC_X), c->a);
      set_data(s, (uint16_t)(x + REC_X_FRACTION), 0);
      lda(s, field(s, DP_Y));
      add(s, data(s, (uint16_t)(T_PART_AT + 2 + y)));
      set_data(s, (uint16_t)(x + REC_Y), c->a);
      lda(s, data(s, (uint16_t)(T_PART_PICTURES + y)));
      set_data(s, (uint16_t)(x + REC_PICTURES), c->a);
      lda(s, 0x008f);
      set_data(s, (uint16_t)(x + REC_PICTURES_BANK), c->a);
      lda(s, 0x0003);
      set_data(s, (uint16_t)(x + REC_COLLIDE_ID), c->a);
      lda(s, data(s, W_FRAME));
      set_data(s, (uint16_t)(x + REC_BORN), c->a);
      lda(s, 0x0c00);
      set_data(s, (uint16_t)(x + REC_PALETTE), c->a);
      lda(s, (uint16_t)(data(s, (uint16_t)(T_PART_PICTURES + 2 + y)) |
                        data(s, (uint16_t)(x + REC_FLAGS))));
      set_data(s, (uint16_t)(x + REC_FLAGS), c->a);
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      dec_a(s);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_AC3F);
      if (!minus(s)) {
        taken(s);
        return stop(AT(0xac3b));
      }
      PORT_COVER(giant_spider_parts_made);
      ran(s, GIANT_SPIDER_RUN_AC8C);
      return rts(s);
    }
    case AT(0xad1c):
      lda(s, 0x0014);
      set_field(s, DP_HEALTH, c->a);
      lda(s, 0x001e);
      ran(s, GIANT_SPIDER_RUN_AD1C);
      return stop(AT(0xad24));
    case AT(0xad28):
      names(s, 0xb13d, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_AD28);
      return stop(AT(0xad2e));
    case AT(0xad32):
      ran(s, GIANT_SPIDER_RUN_AD32);
      return rts(s);
    case AT(0xad49):
      ran(s, GIANT_SPIDER_RUN_AD49);
      return jsr(s, 0xad49, 0xaad2);

    // --- `$83:AAD2`: waiting, and a way chosen ------------------------------
    case AT(0xaad2):
      lda(s, 0x0003);
      set_field(s, DP_PICTURE_LEFT, c->a);
      lda(s, 0xaadc);
      set_field(s, DP_STATE, c->a);
      ran(s, GIANT_SPIDER_RUN_AAD2);
      return AT(0xaadc);
    case AT(0xaadc):
      dec_field(s, DP_PICTURE_LEFT);
      ran(s, GIANT_SPIDER_RUN_AADC);
      if (minus(s)) {
        taken(s);
        return AT(0xaae1);
      }
      PORT_COVER(giant_spider_waits);
      ran(s, GIANT_SPIDER_RUN_AAE0);
      return rts(s);
    case AT(0xaae1):
      PORT_COVER(giant_spider_chooses);
      ran(s, GIANT_SPIDER_RUN_AAE1);
      draw(s, 0xaae1);
      and_(s, 0x0007);
      inc_a(s);
      asl_a(s);
      set_field(s, DP_WAY, c->a);
      ran(s, GIANT_SPIDER_RUN_AAE5);
      set_field(s, DP_STUNG, 0);
      lda(s, 0x0002);
      set_field(s, DP_PICTURE_TICKS, c->a);
      lda(s, 0xaafc);
      set_field(s, DP_STATE, c->a);
      ran(s, GIANT_SPIDER_RUN_AAEF);
      return rts(s);

    // --- Each frame ---------------------------------------------------------
    case AT(0xad4c):
      ran(s, GIANT_SPIDER_RUN_AD4C);
      return jsr(s, 0xad4c, 0xa537);
    case AT(0xad4f): {
      set_field(s, DP_HIT_BY, 0);
      push16(s->w, c, 0xad58);
      lda(s, field(s, DP_STATE));
      dec_a(s);
      push16(s->w, c, c->a);
      ran(s, GIANT_SPIDER_RUN_AD4F);
      const uint32_t state = rts(s);
      if (state != AT(0xaadc) && state != AT(0xaafc) &&
          state != AT(0xab58) && state != AT(0xac13))
        s->k->lost = true;
      return state;
    }
    case AT(0xad59):
      lda(s, 0x0001);
      ran(s, GIANT_SPIDER_RUN_AD59);
      return stop(AT(0xad5c));
    case AT(0xad60):
      ran(s, GIANT_SPIDER_RUN_AD60);
      return jsr(s, 0xad60, 0xa541);

    // `$83:A541`: the next of its four pictures, every `$4C` frames.
    case AT(0xa541):
      dec_field(s, DP_PICTURE_LEFT);
      ran(s, GIANT_SPIDER_RUN_A541);
      if (!minus(s)) {
        taken(s);
      } else {
        PORT_COVER(giant_spider_next_picture);
        set_field(s, DP_PICTURE_LEFT, field(s, DP_PICTURE_TICKS));
        lda(s, field(s, DP_PICTURE));
        inc_a(s);
        and_(s, 0x0003);
        set_field(s, DP_PICTURE, c->a);
        ran(s, GIANT_SPIDER_RUN_A545);
      }
      ran(s, GIANT_SPIDER_RUN_A551);
      return rts(s);
    case AT(0xad63):
      ran(s, GIANT_SPIDER_RUN_AD63);
      return jsr(s, 0xad63, 0xa580);

    // `$83:A580`: a part's little script, or a life gone.
    case AT(0xa580):
      lda(s, field(s, DP_BROKEN));
      ran(s, GIANT_SPIDER_RUN_A580);
      if (zero(s)) {
        taken(s);
        return AT(0xa587);
      }
      ran(s, GIANT_SPIDER_RUN_A584);
      return AT(0xa5c9);
    case AT(0xa587):
      ldx(s, field(s, DP_PLAYS));
      ran(s, GIANT_SPIDER_RUN_A587);
      if (zero(s)) {
        taken(s);
        return AT(0xa5ab);
      }
      return AT(0xa58b);
    case AT(0xa58b):
      dec_field(s, DP_PLAYS_LEFT);
      ran(s, GIANT_SPIDER_RUN_A58B);
      if (!minus(s)) {
        taken(s);
        return AT(0xa5a7);
      }
      ldx(s, field(s, DP_PLAYS));
      lda(s, data(s, field(s, DP_PLAYS_SCRIPT)));
      ran(s, GIANT_SPIDER_RUN_A58F);
      if (zero(s)) {
        PORT_COVER(giant_spider_plays_ends);
        taken(s);
        return AT(0xa5a8);
      }
      PORT_COVER(giant_spider_plays);
      set_data(s, (uint16_t)(c->x + REC_PICTURES), c->a);
      ldy(s, 0x0002);
      lda(s, data(s, (uint16_t)(field(s, DP_PLAYS_SCRIPT) + 2)));
      set_field(s, DP_PLAYS_LEFT, c->a);
      lda(s, field(s, DP_PLAYS_SCRIPT));
      add(s, 0x0004);
      set_field(s, DP_PLAYS_SCRIPT, c->a);
      ran(s, GIANT_SPIDER_RUN_A595);
      return rts(s);
    case AT(0xa5a7):
      ran(s, GIANT_SPIDER_RUN_A5A7);
      return rts(s);
    case AT(0xa5a8):
      set_field(s, DP_PLAYS, 0);
      ran(s, GIANT_SPIDER_RUN_A5A8);
      return rts(s);
    case AT(0xa5ab): {
      ran(s, GIANT_SPIDER_RUN_A5AB);
      draw(s, 0xa5ab);
      and_(s, 0x001c);
      ldy(s, c->a);
      ldx(s, data(s, (uint16_t)(T_PLAYS_PART + c->y)));
      lda(s, field(s, c->x));
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(c->x + REC_PICTURES)));
      ran(s, GIANT_SPIDER_RUN_A5AF);
      if (zero(s)) {
        PORT_COVER(giant_spider_plays_gone);
        taken(s);
        return AT(0xa5a7);
      }
      PORT_COVER(giant_spider_plays_begins);
      set_field(s, DP_PLAYS, c->x);
      lda(s, data(s, (uint16_t)(T_PLAYS_SCRIPT + c->y)));
      set_field(s, DP_PLAYS_SCRIPT, c->a);
      set_field(s, DP_PLAYS_LEFT, 0);
      ran(s, GIANT_SPIDER_RUN_A5BE);
      taken(s);
      return AT(0xa58b);
    }

    // A life gone: a part, its colours, and its health again.
    case AT(0xa5c9):
      ldx(s, field(s, DP_LIVES));
      ran(s, GIANT_SPIDER_RUN_A5C9);
      if (zero(s)) {
        PORT_COVER(giant_spider_last_life);
        taken(s);
        ran(s, GIANT_SPIDER_RUN_A5DA);
        return jsr(s, 0xa5da, 0xa608);
      }
      ran(s, GIANT_SPIDER_RUN_A5CD);
      if (!minus(s)) {
        PORT_COVER(giant_spider_life_lost);
        taken(s);
        ran(s, GIANT_SPIDER_RUN_A5D2);
        return jsr(s, 0xa5d2, 0xa5fd);
      }
      PORT_COVER(giant_spider_dies);
      ran(s, GIANT_SPIDER_RUN_A5CF);
      return AT(0xa5a7);
    case AT(0xa5fd):
      ldy(s, field(s, (uint16_t)(DP_PART + c->x)));
      lda(s, 0);
      set_data(s, (uint16_t)(c->y + REC_PICTURES), 0);
      set_data(s, (uint16_t)(c->y + REC_PICTURES_BANK), 0);
      names(s, 0xfb1c, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_A5FD);
      return stop(AT(0xa60e));
    case AT(0xa608):
      names(s, 0xfb1c, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_A608);
      return stop(AT(0xa60e));
    case AT(0xa612):
      ran(s, GIANT_SPIDER_RUN_A612);
      return rts(s);
    case AT(0xa5d5):
      lda(s, 0x0014);
      ran(s, GIANT_SPIDER_RUN_A5D5);
      taken(s);
      return AT(0xa5e0);
    case AT(0xa5dd):
      lda(s, 0x002d);
      ran(s, GIANT_SPIDER_RUN_A5DD);
      return AT(0xa5e0);
    case AT(0xa5e0):
      set_field(s, DP_HEALTH, c->a);
      ldx(s, data(s, W_LEVEL));
      cpx(s, LEVEL_36);
      ran(s, GIANT_SPIDER_RUN_A5E0);
      if (!zero(s)) {
        taken(s);
      } else {
        PORT_COVER(giant_spider_life_36);
        sub(s, 0x000a);
        set_field(s, DP_HEALTH, c->a);
        ran(s, GIANT_SPIDER_RUN_A5EA);
      }
      set_field(s, DP_BROKEN, 0);
      lda(s, field(s, DP_LIVES));
      dec_a(s);
      dec_a(s);
      set_field(s, DP_LIVES, c->a);
      set_field(s, DP_PLAYS, 0);
      ran(s, GIANT_SPIDER_RUN_A5F0);
      return AT(0xa5ab);

    // `$83:A674`: its three webs.
    case AT(0xad66):
      ran(s, GIANT_SPIDER_RUN_AD66);
      return jsr(s, 0xad66, 0xa674);
    case AT(0xa674):
      ldx(s, 0x0010);
      set_field(s, DP_COUNT, c->x);
      ran(s, GIANT_SPIDER_RUN_A674);
      return AT(0xa679);
    case AT(0xa679):
      ldx(s, field(s, DP_COUNT));
      ldy(s, field(s, (uint16_t)(DP_WEB + c->x)));
      cpy(s, GIANT_SPIDER_WEB_NONE);
      ran(s, GIANT_SPIDER_RUN_A679);
      if (zero(s)) {
        taken(s);
        return AT(0xa685);
      }
      ran(s, GIANT_SPIDER_RUN_A682);
      return jsr(s, 0xa682, 0xa690);
    case AT(0xa685):
      ran(s, GIANT_SPIDER_RUN_A685);
      if (count_down(s, 0x0008)) {
        taken(s);
        return AT(0xa679);
      }
      ran(s, GIANT_SPIDER_RUN_A68F);
      return rts(s);
    case AT(0xa690):
      ldx(s, field(s, DP_COUNT));
      lda(s, field(s, (uint16_t)(DP_WEB_LEFT + c->x)));
      ran(s, GIANT_SPIDER_RUN_A690);
      if (!minus(s)) {
        taken(s);
        return AT(0xa6c2);
      }
      inc_a(s);
      set_field(s, (uint16_t)(DP_WEB_LEFT + c->x), c->a);
      ran(s, GIANT_SPIDER_RUN_A696);
      if (minus(s)) {
        taken(s);
        return AT(0xa6a0);
      }
      PORT_COVER(giant_spider_web_spread);
      ran(s, GIANT_SPIDER_RUN_A69B);
      return AT(0xa6ed);

    // Spreading: a picture and a box told, every fourth frame.
    case AT(0xa6a0): {
      eor(s, 0xffff);
      inc_a(s);
      bit_imm(s, 0x0003);
      ran(s, GIANT_SPIDER_RUN_A6A0);
      if (!zero(s)) {
        ran(s, GIANT_SPIDER_RUN_A6A9);
        return AT(0xa6ec);
      }
      PORT_COVER(giant_spider_web_spreads);
      taken(s);
      lsr_a(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_SPREAD_PICTURES + c->x)));
      const uint16_t web = c->y;
      set_data(s, (uint16_t)(web + REC_PICTURES), c->a);
      lda(s, data(s, (uint16_t)(web + REC_X)));
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(web + REC_Y)));
      ldy(s, c->a);
      ran(s, GIANT_SPIDER_RUN_A6AC);
      return jsr(s, 0xa6bc, 0xaa89);
    }
    // `$83:AA89`: a box about X and Y, told.
    case AT(0xaa89):
      lda(s, c->x);
      sub(s, 0x0018);
      set_data(s, W_BOX, c->a);
      add(s, 0x0030);
      set_data(s, (uint16_t)(W_BOX + 2), c->a);
      lda(s, c->y);
      sub(s, 0x0018);
      set_data(s, (uint16_t)(W_BOX + 4), c->a);
      add(s, 0x0038);
      set_data(s, (uint16_t)(W_BOX + 6), c->a);
      lda(s, 0x0003);
      set_data(s, W_BOX_KIND, c->a);
      ran(s, GIANT_SPIDER_RUN_AA89);
      return stop(AT(0xaaad));
    case AT(0xaab1):
      ran(s, GIANT_SPIDER_RUN_AAB1);
      return rts(s);
    case AT(0xa6bf):
      ran(s, GIANT_SPIDER_RUN_A6BF);
      return AT(0xa6ec);

    // Flying, or landed and spreading.
    case AT(0xa6c2): {
      dec_a(s);
      set_field(s, (uint16_t)(DP_WEB_LEFT + c->x), c->a);
      ran(s, GIANT_SPIDER_RUN_A6C2);
      const uint16_t web = c->y;
      if (minus(s)) {
        PORT_COVER(giant_spider_web_lands);
        taken(s);
        lda(s, 0xfff0);
        set_field(s, (uint16_t)(DP_WEB_LEFT + c->x), c->a);
        lda(s, 0xa2bd);
        set_data(s, (uint16_t)(web + REC_PICTURES), c->a);
        lda(s, 0x0090);
        set_data(s, (uint16_t)(web + REC_PICTURES_BANK), c->a);
        ran(s, GIANT_SPIDER_RUN_A6DB);
        return AT(0xa6ec);
      }
      PORT_COVER(giant_spider_web_flies);
      lda(s, data(s, (uint16_t)(web + REC_X)));
      add(s, field(s, (uint16_t)(DP_WEB_DX + c->x)));
      set_data(s, (uint16_t)(web + REC_X), c->a);
      lda(s, data(s, (uint16_t)(web + REC_Y)));
      add(s, field(s, (uint16_t)(DP_WEB_DY + c->x)));
      set_data(s, (uint16_t)(web + REC_Y), c->a);
      ran(s, GIANT_SPIDER_RUN_A6C7);
      taken(s);
      return AT(0xa6ec);
    }
    case AT(0xa6ec):
      ran(s, GIANT_SPIDER_RUN_A6EC);
      return rts(s);

    // Spread: four tiles about it webbed where they can be, and freed.
    case AT(0xa6ed): {
      const uint16_t web = c->y;
      lda(s, data(s, (uint16_t)(web + REC_X)));
      lsr_a(s);
      lsr_a(s);
      lsr_a(s);
      set_field(s, DP_TO_X, c->a);
      lda(s, data(s, (uint16_t)(web + REC_Y)));
      lsr_a(s);
      lsr_a(s);
      lsr_a(s);
      set_field(s, DP_TO_Y, c->a);
      ldx(s, 0x0018);
      set_field(s, DP_TEMP, c->x);
      ran(s, GIANT_SPIDER_RUN_A6ED);
      return AT(0xa702);
    }
    case AT(0xa702):
      ldx(s, field(s, DP_TEMP));
      lda(s, field(s, DP_TO_X));
      add(s, data(s, (uint16_t)(T_WEB_TILES + c->x)));
      set_field(s, DP_TO_X, c->a);
      lda(s, field(s, DP_TO_Y));
      add(s, data(s, (uint16_t)(T_WEB_TILES + 2 + c->x)));
      set_field(s, DP_TO_Y, c->a);
      ldx(s, field(s, DP_TO_X));
      ldy(s, field(s, DP_TO_Y));
      ran(s, GIANT_SPIDER_RUN_A702);
      return stop(AT(0xa718));
    case AT(0xa71c):
      bit_imm(s, 0x1000);
      ran(s, GIANT_SPIDER_RUN_A71C);
      if (zero(s)) {
        PORT_COVER(giant_spider_tile_bare);
        taken(s);
        return AT(0xa73b);
      }
      PORT_COVER(giant_spider_tile_webbed);
      lda(s, field(s, DP_TEMP));
      ldx(s, data(s, W_LEVEL));
      cpx(s, LEVEL_36);
      ran(s, GIANT_SPIDER_RUN_A721);
      if (zero(s)) {
        taken(s);
      } else {
        add(s, 0x0020);
        ran(s, GIANT_SPIDER_RUN_A72B);
      }
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_WEB_TILE + c->x)));
      ldx(s, field(s, DP_TO_X));
      ldy(s, field(s, DP_TO_Y));
      ran(s, GIANT_SPIDER_RUN_A72F);
      return stop(AT(0xa737));
    case AT(0xa73b):
      lda(s, field(s, DP_TEMP));
      sub(s, 0x0008);
      set_field(s, DP_TEMP, c->a);
      ran(s, GIANT_SPIDER_RUN_A73B);
      if (!minus(s)) {
        taken(s);
        return AT(0xa702);
      }
      ldx(s, field(s, DP_COUNT));
      ldy(s, GIANT_SPIDER_WEB_NONE);
      lda(s, field(s, (uint16_t)(DP_WEB + c->x)));
      set_field(s, (uint16_t)(DP_WEB + c->x), c->y);
      ran(s, GIANT_SPIDER_RUN_A745);
      return stop(AT(0xa74e));
    case AT(0xa752):
      dec_field(s, DP_WEBS);
      ran(s, GIANT_SPIDER_RUN_A752);
      return rts(s);

    // Broken, it waits for its webs to be gone.
    case AT(0xad69):
      lda(s, field(s, DP_BROKEN));
      ran(s, GIANT_SPIDER_RUN_AD69);
      if (zero(s)) {
        taken(s);
        return AT(0xad4c);
      }
      return AT(0xad6d);
    case AT(0xad6d):
      lda(s, field(s, DP_WEBS));
      ran(s, GIANT_SPIDER_RUN_AD6D);
      if (zero(s)) {
        taken(s);
        return AT(0xad7a);
      }
      PORT_COVER(giant_spider_webs_wait);
      lda(s, 0x0001);
      ran(s, GIANT_SPIDER_RUN_AD71);
      return stop(AT(0xad74));
    case AT(0xad78):
      ran(s, GIANT_SPIDER_RUN_AD78);
      taken(s);
      return AT(0xad66);

    // --- Its death ----------------------------------------------------------
    case AT(0xad7a):
      PORT_COVER(giant_spider_dead);
      ldx(s, 0x2000);
      lda(s, field(s, DP_HIT_BY));
      ran(s, GIANT_SPIDER_RUN_AD7A);
      if (zero(s)) {
        PORT_COVER(giant_spider_unpaid);
        taken(s);
        return AT(0xad85);
      }
      return stop(AT(0xad81));
    case AT(0xad85):
      ran(s, GIANT_SPIDER_RUN_AD85);
      return jsr(s, 0xad85, 0xb109);
    case AT(0xb109):
      ldx(s, 0x000a);
      set_field(s, DP_COUNT, c->x);
      ran(s, GIANT_SPIDER_RUN_B109);
      return AT(0xb10e);
    case AT(0xb10e):
      ldx(s, field(s, DP_COUNT));
      lda(s, field(s, (uint16_t)(DP_RECORD + c->x)));
      ran(s, GIANT_SPIDER_RUN_B10E);
      return stop(AT(0xb112));
    case AT(0xb116):
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      dec_a(s);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_B116);
      if (!minus(s)) {
        taken(s);
        return AT(0xb10e);
      }
      ran(s, GIANT_SPIDER_RUN_B11E);
      return rts(s);
    case AT(0xad88):
      lda(s, data(s, W_LOAD));
      sub(s, 0x0028);
      set_data(s, W_LOAD, c->a);
      ran(s, GIANT_SPIDER_RUN_AD88);
      if (minus(s)) {
        s->k->lost = true;  // `BMI *`: the load gone under, and it hangs
        return stop(AT(0xad92));
      }
      ran(s, GIANT_SPIDER_RUN_AD94);
      return jsr(s, 0xad94, 0xaf53);

    // `$83:AF53`: twelve puffs, its colours flashing.
    case AT(0xaf53):
      lda(s, 0x002c);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_AF53);
      return AT(0xaf58);
    case AT(0xaf58):
      ldx(s, field(s, DP_COUNT));
      place_about(s, T_PUFFS_AT, c->x);
      ran(s, GIANT_SPIDER_RUN_AF58);
      return stop(AT(0xaf6a));
    case AT(0xaf6e):
    case AT(0xaff8):
    case AT(0xb02d): {
      const int at = pc == AT(0xaf6e) ? 0 : pc == AT(0xaff8) ? 1 : 2;
      static const int RUNS[3] = {GIANT_SPIDER_RUN_AF6E, GIANT_SPIDER_RUN_AFF8,
                                  GIANT_SPIDER_RUN_B02D};
      static const int BEGINS[3] = {GIANT_SPIDER_RUN_AF70, GIANT_SPIDER_RUN_AFFA,
                                    GIANT_SPIDER_RUN_B02F};
      static const uint32_t PAST[3] = {AT(0xaf7a), AT(0xb004), AT(0xb039)};
      static const uint32_t CALLS[3] = {AT(0xaf76), AT(0xb000), AT(0xb035)};
      ran(s, RUNS[at]);
      if (carry(s)) {
        PORT_COVER(giant_spider_death_no_room);
        taken(s);
        return PAST[at];
      }
      names(s, at == 2 ? 0xb277 : 0xb1dd, GIANT_SPIDER_BANK);
      ran(s, BEGINS[at]);
      return stop(CALLS[at]);
    }
    case AT(0xaf7a):
    case AT(0xafd3):
      names(s, 0xffd1, 0x0097);
      ran(s, pc == AT(0xaf7a) ? GIANT_SPIDER_RUN_AF7A : GIANT_SPIDER_RUN_AFD3);
      return stop(pc == AT(0xaf7a) ? AT(0xaf80) : AT(0xafd9));
    case AT(0xaf84):
    case AT(0xaf95):
      lda(s, 0x0006);
      ran(s, pc == AT(0xaf84) ? GIANT_SPIDER_RUN_AF84 : GIANT_SPIDER_RUN_AF95);
      return stop(pc == AT(0xaf84) ? AT(0xaf87) : AT(0xaf98));
    case AT(0xaf8b):
    case AT(0xb004):
      names(s, 0xfafc, GIANT_SPIDER_BANK);
      ran(s, pc == AT(0xaf8b) ? GIANT_SPIDER_RUN_AF8B : GIANT_SPIDER_RUN_B004);
      return stop(pc == AT(0xaf8b) ? AT(0xaf91) : AT(0xb00a));
    case AT(0xaf9c):
      ran(s, GIANT_SPIDER_RUN_AF9C);
      if (count_down(s, 0x0004)) {
        taken(s);
        return AT(0xaf58);
      }
      lda(s, 0);
      set_w7e(s, W7E_MOSAIC, c->a);
      lda(s, 0x000f);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_AFA6);
      return AT(0xafb2);

    // Coarser in fifteen steps of two ticks, puffs and flashes by turns.
    case AT(0xafb2):
      names(s, 0xb0ec, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_AFB2);
      return stop(AT(0xafb8));
    case AT(0xafbc):
      lda(s, (uint16_t)(w7e(s, W7E_MOSAIC) + 1));
      set_w7e(s, W7E_MOSAIC, c->a);
      lda(s, 0x0002);
      ran(s, GIANT_SPIDER_RUN_AFBC);
      return stop(AT(0xafc8));
    case AT(0xafcc):
      lda(s, field(s, DP_COUNT));
      bit_imm(s, 0x0001);
      ran(s, GIANT_SPIDER_RUN_AFCC);
      if (!zero(s)) {
        PORT_COVER(giant_spider_death_puff);
        taken(s);
        return AT(0xafdf);
      }
      PORT_COVER(giant_spider_death_flash);
      return AT(0xafd3);
    case AT(0xafdd):
      ran(s, GIANT_SPIDER_RUN_AFDD);
      taken(s);
      return AT(0xb00e);
    case AT(0xafdf):
      lda(s, field(s, DP_COUNT));
      asl_a(s);
      asl_a(s);
      ldx(s, c->a);
      place_about(s, T_PUFFS_AT, c->x);
      ran(s, GIANT_SPIDER_RUN_AFDF);
      return stop(AT(0xaff4));
    case AT(0xb00e):
      dec_field(s, DP_COUNT);
      ran(s, GIANT_SPIDER_RUN_B00E);
      if (!zero(s)) {
        taken(s);
        return AT(0xafb2);
      }
      lda(s, 0x0010);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_B012);
      return AT(0xb017);

    // Five spiders.
    case AT(0xb017):
      ldx(s, field(s, DP_COUNT));
      place_about(s, T_PUFFS_AT, c->x);
      ran(s, GIANT_SPIDER_RUN_B017);
      return stop(AT(0xb029));
    case AT(0xb039):
      ran(s, GIANT_SPIDER_RUN_B039);
      if (count_down(s, 0x0004)) {
        taken(s);
        return AT(0xb017);
      }
      lda(s, 0x0014);
      ran(s, GIANT_SPIDER_RUN_B043);
      return stop(AT(0xb046));

    // What it leaves: a sparkle on level 36, what comes next on 48.
    case AT(0xb04a):
      lda(s, field(s, DP_X));
      set_field(s, DP_AT_X, c->a);
      lda(s, field(s, DP_Y));
      set_field(s, DP_AT_Y, c->a);
      lda(s, data(s, W_LEVEL));
      cmp(s, LEVEL_36);
      ran(s, GIANT_SPIDER_RUN_B04A);
      if (!zero(s)) {
        PORT_COVER(giant_spider_leaves_48);
        taken(s);
        return AT(0xb06f);
      }
      PORT_COVER(giant_spider_leaves_36);
      return AT(0xb05a);
    case AT(0xb05a):
      lda(s, 0x0001);
      ran(s, GIANT_SPIDER_RUN_B05A);
      return stop(AT(0xb05d));
    case AT(0xb061):
      names(s, 0xb55e, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_B061);
      return stop(AT(0xb067));
    case AT(0xb06b):
      ran(s, GIANT_SPIDER_RUN_B06B);
      if (zero(s)) {
        PORT_COVER(giant_spider_sparkle_again);
        taken(s);
        return AT(0xb05a);
      }
      ran(s, GIANT_SPIDER_RUN_B06D);
      taken(s);
      return AT(0xb08c);
    case AT(0xb06f):
      lda(s, data(s, W_RECORDS));
      cmp(s, 0x0012);
      ran(s, GIANT_SPIDER_RUN_B06F);
      if (!carry(s)) {
        taken(s);
        names(s, 0xc687, GIANT_SPIDER_BANK);
        ran(s, GIANT_SPIDER_RUN_B080);
        return stop(AT(0xb086));
      }
      PORT_COVER(giant_spider_after_waits);
      lda(s, 0x0004);
      ran(s, GIANT_SPIDER_RUN_B077);
      return stop(AT(0xb07a));
    case AT(0xb07e):
      ran(s, GIANT_SPIDER_RUN_B07E);
      taken(s);
      return AT(0xb06f);
    case AT(0xb08a):
      ran(s, GIANT_SPIDER_RUN_B08A);
      if (zero(s)) {
        PORT_COVER(giant_spider_after_again);
        taken(s);
        return AT(0xb06f);
      }
      return AT(0xb08c);
    case AT(0xb08c):
      lda(s, 0x0005);
      ran(s, GIANT_SPIDER_RUN_B08C);
      return stop(AT(0xb08f));

    // After `JSL $82:8113`, the layer blanked (`boss_bg_clear`).
    case AT(0xb097):
      lda(s, 0);
      set_w7e(s, W7E_SNAKEOID_UP, 0);
      ran(s, GIANT_SPIDER_RUN_B097);
      return jsr(s, 0xb09e, 0xb11f);

    // `$83:B11F`: on level 36, a tile of the map put down again.
    case AT(0xb11f):
      lda(s, data(s, W_LEVEL));
      cmp(s, LEVEL_36);
      ran(s, GIANT_SPIDER_RUN_B11F);
      if (!zero(s)) {
        taken(s);
        return AT(0xb13c);
      }
      PORT_COVER(giant_spider_tile_36);
      ldx(s, 0x0006);
      ldy(s, 0x0001);
      ran(s, GIANT_SPIDER_RUN_B127);
      return stop(AT(0xb12d));
    case AT(0xb131):
      inc_a(s);
      ldx(s, 0x0006);
      ldy(s, 0x0001);
      ran(s, GIANT_SPIDER_RUN_B131);
      return stop(AT(0xb138));
    case AT(0xb13c):
      ran(s, GIANT_SPIDER_RUN_B13C);
      return rts(s);
    case AT(0xb0a1):
      names(s, 0xb100, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_B0A1);
      return stop(AT(0xb0a7));
    case AT(0xb0ab):
      PORT_COVER(giant_spider_ends);
      ran(s, GIANT_SPIDER_RUN_B0AB);
      return rts(s);
    case AT(0xad97):
      return stop(AT(0xad97));

    // --- `$83:A537`: its picture on its layer, and its flash ----------------
    case AT(0xa537):
      ran(s, GIANT_SPIDER_RUN_A537);
      return jsr(s, 0xa537, 0xa4f1);
    case AT(0xa4f1):
      lda(s, field(s, DP_X));
      set_data(s, W_PLANE_X, c->a);
      lda(s, field(s, DP_Y));
      set_data(s, W_PLANE_Y, c->a);
      ran(s, GIANT_SPIDER_RUN_A4F1);
      jsl(s, 0xa4fb);
      lda(s, data(s, W_BG_DMA_CURSOR));
      ran(s, GIANT_SPIDER_RUN_80E0);
      rtl(s);
      ran(s, GIANT_SPIDER_RUN_A4FF);
      if (!zero(s)) {
        PORT_COVER(giant_spider_picture_waits);
        taken(s);
        return AT(0xa511);
      }
      lda(s, field(s, DP_PICTURE));
      cmp(s, field(s, DP_SHOWN));
      ran(s, GIANT_SPIDER_RUN_A501);
      if (zero(s)) {
        taken(s);
        return AT(0xa511);
      }
      PORT_COVER(giant_spider_picture_put);
      set_field(s, DP_SHOWN, c->a);
      asl_a(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_PICTURE_FIGURE + c->x)));
      ran(s, GIANT_SPIDER_RUN_A507);
      return jsr(s, 0xa50e, 0xa448);
    case AT(0xa511):
      ran(s, GIANT_SPIDER_RUN_A511);
      return rts(s);
    case AT(0xa53a):
      ran(s, GIANT_SPIDER_RUN_A53A);
      return jsr(s, 0xa53a, 0xa4a0);
    case AT(0xa4a0):
      lda(s, field(s, DP_FLASH));
      ran(s, GIANT_SPIDER_RUN_A4A0);
      if (zero(s)) {
        taken(s);
        return AT(0xa4c5);
      }
      dec_field(s, DP_FLASH);
      ran(s, GIANT_SPIDER_RUN_A4A4);
      if (!zero(s)) {
        PORT_COVER(giant_spider_flashing);
        taken(s);
        return AT(0xa4f0);
      }
      PORT_COVER(giant_spider_unflash);
      ldx(s, 0x000a);
      ran(s, GIANT_SPIDER_RUN_A4A8);
      for (;;) {
        ldy(s, field(s, (uint16_t)(DP_RECORD + c->x)));
        lda(s, (uint16_t)(data(s, (uint16_t)(c->y + REC_FLAGS)) & 0xffefu));
        set_data(s, (uint16_t)(c->y + REC_FLAGS), c->a);
        ldx(s, (uint16_t)(c->x - 2));
        ran(s, GIANT_SPIDER_RUN_A4AB);
        if (minus(s)) break;
        taken(s);
      }
      names(s, 0xfafc, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_A4BA);
      return stop(AT(0xa4c0));
    case AT(0xa4c4):
      ran(s, GIANT_SPIDER_RUN_A4C4);
      return rts(s);
    case AT(0xa4c5):
      bit_field(s, DP_HIT);
      ran(s, GIANT_SPIDER_RUN_A4C5);
      if (!minus(s)) {
        taken(s);
        return AT(0xa4f0);
      }
      PORT_COVER(giant_spider_flash);
      set_field(s, DP_HIT, 0);
      lda(s, 0x0002);
      set_field(s, DP_FLASH, c->a);
      lda(s, field(s, DP_LIVES));
      ran(s, GIANT_SPIDER_RUN_A4C9);
      if (!zero(s)) {
        taken(s);
        return AT(0xa4de);
      }
      PORT_COVER(giant_spider_flash_last);
      names(s, 0xfb1c, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_A4D4);
      return stop(AT(0xa4da));
    case AT(0xa4de):
      ldx(s, 0x000a);
      ran(s, GIANT_SPIDER_RUN_A4DE);
      for (;;) {
        ldy(s, field(s, (uint16_t)(DP_RECORD + c->x)));
        lda(s, (uint16_t)(data(s, (uint16_t)(c->y + REC_FLAGS)) | 0x0010u));
        set_data(s, (uint16_t)(c->y + REC_FLAGS), c->a);
        ldx(s, (uint16_t)(c->x - 2));
        ran(s, GIANT_SPIDER_RUN_A4E1);
        if (minus(s)) break;
        taken(s);
      }
      return AT(0xa4f0);
    case AT(0xa4f0):
      ran(s, GIANT_SPIDER_RUN_A4F0);
      return rts(s);
    case AT(0xa53d):
      ran(s, GIANT_SPIDER_RUN_A53D);
      return jsr(s, 0xa53d, 0xa51a);
    case AT(0xa540):
      ran(s, GIANT_SPIDER_RUN_A540);
      return rts(s);

    // --- `$83:AAFC`: going its way ------------------------------------------
    case AT(0xaafc):
      lda(s, 0x0008);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, GIANT_SPIDER_RUN_AAFC);
      return jsr(s, 0xab03, 0xa87b);

    // `$83:A87B`: the nearest within A, if it is a player, and which way.
    case AT(0xa87b):
      set_field(s, DP_TEMP, c->a);
      ran(s, GIANT_SPIDER_RUN_A87B);
      return stop(AT(0xa87d));
    case AT(0xa881): {
      cmp(s, field(s, DP_TEMP));
      ran(s, GIANT_SPIDER_RUN_A881);
      if (carry(s)) {
        PORT_COVER(giant_spider_nobody_near);
        taken(s);
        return AT(0xa8af);
      }
      set_field(s, DP_TEMP, c->a);
      ldy(s, data(s, (uint16_t)(c->x + REC_COLLIDE_ID)));
      static const struct {
        uint16_t id;
        int run;
      } PLAYERS[] = {{0x0005, GIANT_SPIDER_RUN_A885},
                     {0x0006, GIANT_SPIDER_RUN_A88F},
                     {0x0038, GIANT_SPIDER_RUN_A894},
                     {0x0001, GIANT_SPIDER_RUN_A899}};
      for (int i = 0; i < 4; i++) {
        cpy(s, PLAYERS[i].id);
        ran(s, PLAYERS[i].run);
        if (zero(s)) {
          PORT_COVER(giant_spider_player_near);
          taken(s);
          return AT(0xa8a0);
        }
      }
      PORT_COVER(giant_spider_other_near);
      ran(s, GIANT_SPIDER_RUN_A89E);
      taken(s);
      return AT(0xa8af);
    }
    case AT(0xa8a0):
      ldy(s, data(s, (uint16_t)(c->x + REC_Y)));
      lda(s, data(s, (uint16_t)(c->x + REC_X)));
      push16(s->w, c, c->x);
      ldx(s, c->a);
      ran(s, GIANT_SPIDER_RUN_A8A0);
      return jsr(s, 0xa8a8, 0xa8b5);

    // `$83:A8B5`: which way the point at X and Y is from it, of eight.
    case AT(0xa8b5):
      set_data(s, W_BEARING_X, c->x);
      set_data(s, W_BEARING_Y, c->y);
      ldx(s, 0);
      lda(s, field(s, DP_Y));
      cmp(s, data(s, W_BEARING_Y));
      ran(s, GIANT_SPIDER_RUN_A8B5);
      if (zero(s)) {
        taken(s);
      } else {
        lda(s, c->x);
        adc(s, 0x0001);
        asl_a(s);
        asl_a(s);
        ldx(s, c->a);
        ran(s, GIANT_SPIDER_RUN_A8C5);
      }
      lda(s, field(s, DP_X));
      cmp(s, data(s, W_BEARING_X));
      ran(s, GIANT_SPIDER_RUN_A8CC);
      if (zero(s)) {
        taken(s);
      } else {
        lda(s, c->x);
        adc(s, 0x0001);
        ldx(s, c->a);
        ran(s, GIANT_SPIDER_RUN_A8D3);
      }
      lda(s, bus_r16(s->w, s->rom, T_BEARING + c->x) & 0x00ffu);
      ran(s, GIANT_SPIDER_RUN_A8D8);
      return rts(s);
    case AT(0xa8ab):
      ldx(s, pull16(s->w, c));
      ldy(s, field(s, DP_TEMP));
      ran(s, GIANT_SPIDER_RUN_A8AB);
      return rts(s);
    case AT(0xa8af):
      lda(s, 0);
      ldx(s, 0);
      ldy(s, 0);
      ran(s, GIANT_SPIDER_RUN_A8AF);
      return rts(s);

    case AT(0xab06): {
      lda(s, field(s, DP_WAY));
      asl_a(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_WAY_STEP + c->x)));
      add(s, field(s, DP_X));
      set_field(s, DP_TO_X, c->a);
      lda(s, data(s, (uint16_t)(T_WAY_STEP + 2 + c->x)));
      add(s, field(s, DP_Y));
      set_field(s, DP_TO_Y, c->a);
      ran(s, GIANT_SPIDER_RUN_AB06);
      return jsr(s, 0xab1a, 0xa8ed);
    }

    // `$83:A8ED`: could it stand where it would go? Three probes, each
    // asked of the ground and of its own wide test. Carry set: no.
    case AT(0xa8ed):
      lda(s, 0x0008);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_A8ED);
      return AT(0xa8f2);
    case AT(0xa8f2):
      ldx(s, field(s, DP_COUNT));
      lda(s, field(s, DP_TO_X));
      add(s, data(s, (uint16_t)(T_PROBES + c->x)));
      set_field(s, DP_PROBE_X, c->a);
      lda(s, field(s, DP_TO_Y));
      add(s, data(s, (uint16_t)(T_PROBES + 2 + c->x)));
      set_field(s, DP_PROBE_Y, c->a);
      lda(s, data(s, W_TICK));
      and_(s, 0x001f);
      ran(s, GIANT_SPIDER_RUN_A8F2);
      if (!zero(s)) {
        taken(s);
        return AT(0xa913);
      }
      PORT_COVER(giant_spider_probe_told);
      ldx(s, field(s, DP_PROBE_X));
      ldy(s, field(s, DP_PROBE_Y));
      ran(s, GIANT_SPIDER_RUN_A90C);
      return jsr(s, 0xa910, 0xaa89);
    case AT(0xa913):
      ldx(s, field(s, DP_PROBE_X));
      ldy(s, field(s, DP_PROBE_Y));
      ran(s, GIANT_SPIDER_RUN_A913);
      push16(s->w, c, 0xa919);  // `JSR $A97C`
      wide(s);
      pull16(s->w, c);          // ...and its `RTS`, in the blocks it ran
      ran(s, GIANT_SPIDER_RUN_A91A);
      if (carry(s)) {
        PORT_COVER(giant_spider_probe_wide);
        taken(s);
        return AT(0xa96f);
      }
      ldx(s, field(s, DP_PROBE_X));
      ldy(s, field(s, DP_PROBE_Y));
      ran(s, GIANT_SPIDER_RUN_A91C);
      return stop(AT(0xa920));
    case AT(0xa924):
      ran(s, GIANT_SPIDER_RUN_A924);
      if (carry(s)) {
        PORT_COVER(giant_spider_probe_ground);
        taken(s);
        return AT(0xa96f);
      }
      ran(s, GIANT_SPIDER_RUN_A926);
      if (count_down(s, 0x0004)) {
        taken(s);
        return AT(0xa8f2);
      }
      PORT_COVER(giant_spider_steps);
      lda(s, field(s, DP_TO_X));
      set_field(s, DP_X, c->a);
      lda(s, field(s, DP_TO_Y));
      set_field(s, DP_Y, c->a);
      lda(s, 0x000a);
      set_field(s, DP_COUNT, c->a);
      ran(s, GIANT_SPIDER_RUN_A930);
      for (;;) {
        ldx(s, field(s, DP_COUNT));
        ldy(s, field(s, (uint16_t)(DP_RECORD + c->x)));
        lda(s, c->x);
        asl_a(s);
        ldx(s, c->a);
        const uint16_t part = c->y;
        lda(s, field(s, DP_TO_X));
        add(s, data(s, (uint16_t)(T_PART_AT + c->x)));
        set_data(s, (uint16_t)(part + REC_X), c->a);
        lda(s, field(s, DP_TO_Y));
        add(s, data(s, (uint16_t)(T_PART_AT + 2 + c->x)));
        set_data(s, (uint16_t)(part + REC_Y), c->a);
        lda(s, field(s, DP_COUNT));
        dec_a(s);
        dec_a(s);
        set_field(s, DP_COUNT, c->a);
        ran(s, GIANT_SPIDER_RUN_A93D);
        if (minus(s)) break;
        taken(s);
      }
      lda(s, field(s, DP_WAY));
      asl_a(s);
      add(s, 0x0020);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_WAY_STEP + c->x)));
      ldy(s, field(s, DP_PART));
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      set_c(c, false);
      ran(s, GIANT_SPIDER_RUN_A95E);
      return rts(s);
    case AT(0xa96f):
      ran(s, GIANT_SPIDER_RUN_A96F);
      return rts(s);

    case AT(0xab1d):
      ran(s, GIANT_SPIDER_RUN_AB1D);
      if (!carry(s)) {
        taken(s);
        return AT(0xab22);
      }
      PORT_COVER(giant_spider_stopped);
      ran(s, GIANT_SPIDER_RUN_AB1F);
      return AT(0xaad2);
    case AT(0xab22):
      ran(s, GIANT_SPIDER_RUN_AB22);
      draw(s, 0xab22);
      cmp(s, 0x0004);
      ran(s, GIANT_SPIDER_RUN_AB26);
      if (!carry(s)) {
        PORT_COVER(giant_spider_to_spiders);
        taken(s);
        ran(s, GIANT_SPIDER_RUN_AB39);
        return AT(0xab3f);
      }
      ran(s, GIANT_SPIDER_RUN_AB2B);
      draw(s, 0xab2b);
      cmp(s, 0x0019);
      ran(s, GIANT_SPIDER_RUN_AB2F);
      if (!carry(s)) {
        PORT_COVER(giant_spider_to_webs);
        taken(s);
        ran(s, GIANT_SPIDER_RUN_AB3C);
        return AT(0xabdb);
      }
      PORT_COVER(giant_spider_goes_on);
      ran(s, GIANT_SPIDER_RUN_AB34);
      return rts(s);

    // `$83:AB3F`: its mouth's script, if there is room for spiders.
    case AT(0xab3f):
      lda(s, data(s, W_RECORDS));
      cmp(s, 0x000f);
      ran(s, GIANT_SPIDER_RUN_AB3F);
      if (carry(s)) {
        PORT_COVER(giant_spider_no_spiders);
        taken(s);
        return AT(0xab57);
      }
      lda(s, 0x0004);
      set_field(s, DP_PICTURE_TICKS, c->a);
      lda(s, 0xab58);
      set_field(s, DP_STATE, c->a);
      lda(s, T_MOUTH_SCRIPT);
      ran(s, GIANT_SPIDER_RUN_AB47);
      return jsr(s, 0xab54, 0xa552);
    case AT(0xab57):
      ran(s, GIANT_SPIDER_RUN_AB57);
      return rts(s);

    // `$83:A552`: the mouth's script, begun with A if it was not playing.
    // Carry set while it plays.
    case AT(0xa552):
      set_c(c, false);
      ldx(s, field(s, DP_SCRIPT));
      ran(s, GIANT_SPIDER_RUN_A552);
      if (zero(s)) {
        PORT_COVER(giant_spider_script_begins);
        taken(s);
        set_field(s, DP_SCRIPT, c->a);
        set_field(s, DP_SCRIPT_LEFT, 0);
        ran(s, GIANT_SPIDER_RUN_A579);
      }
      return AT(0xa557);
    case AT(0xa557):
      dec_field(s, DP_SCRIPT_LEFT);
      ran(s, GIANT_SPIDER_RUN_A557);
      if (!minus(s)) {
        taken(s);
        set_c(c, true);
        ran(s, GIANT_SPIDER_RUN_A573);
        return rts(s);
      }
      ldx(s, field(s, DP_PART));
      lda(s, data(s, field(s, DP_SCRIPT)));
      ran(s, GIANT_SPIDER_RUN_A55B);
      if (zero(s)) {
        PORT_COVER(giant_spider_script_ends);
        taken(s);
        set_field(s, DP_SCRIPT, 0);
        set_c(c, false);
        ran(s, GIANT_SPIDER_RUN_A575);
        return rts(s);
      }
      PORT_COVER(giant_spider_script_plays);
      set_data(s, (uint16_t)(c->x + REC_PICTURES), c->a);
      ldy(s, 0x0002);
      lda(s, data(s, (uint16_t)(field(s, DP_SCRIPT) + 2)));
      set_field(s, DP_SCRIPT_LEFT, c->a);
      lda(s, field(s, DP_SCRIPT));
      add(s, 0x0004);
      set_field(s, DP_SCRIPT, c->a);
      set_c(c, true);
      ran(s, GIANT_SPIDER_RUN_A561);
      return rts(s);

    case AT(0xab58):
      ran(s, GIANT_SPIDER_RUN_AB58);
      return jsr(s, 0xab58, 0xa552);
    case AT(0xab5b):
      ran(s, GIANT_SPIDER_RUN_AB5B);
      if (carry(s)) {
        ran(s, GIANT_SPIDER_RUN_AB5D);
        return rts(s);
      }
      taken(s);
      lda(s, data(s, W_RECORDS));
      cmp(s, 0x000f);
      ran(s, GIANT_SPIDER_RUN_AB5E);
      if (carry(s)) {
        PORT_COVER(giant_spider_spiders_no_room);
        taken(s);
        ran(s, GIANT_SPIDER_RUN_ABA2);
        return AT(0xaad2);
      }
      ldx(s, 0x0004);
      lda(s, data(s, W_LEVEL));
      cmp(s, LEVEL_36);
      ran(s, GIANT_SPIDER_RUN_AB66);
      if (!zero(s)) {
        taken(s);
      } else {
        PORT_COVER(giant_spider_spiders_36);  // one, not two
        ldx(s, 0);
        ran(s, GIANT_SPIDER_RUN_AB71);
      }
      set_field(s, DP_COUNT, c->x);
      ran(s, GIANT_SPIDER_RUN_AB74);
      return AT(0xab76);
    case AT(0xab76):
      ldx(s, field(s, DP_COUNT));
      place_about(s, T_SPIDERS_AT, c->x);
      ran(s, GIANT_SPIDER_RUN_AB76);
      return stop(AT(0xab88));
    case AT(0xab8c):
      ran(s, GIANT_SPIDER_RUN_AB8C);
      if (carry(s)) {
        PORT_COVER(giant_spider_spider_no_room);
        taken(s);
        return AT(0xab98);
      }
      PORT_COVER(giant_spider_spider);
      names(s, 0xb277, GIANT_SPIDER_BANK);
      ran(s, GIANT_SPIDER_RUN_AB8E);
      return stop(AT(0xab94));
    case AT(0xab98):
      ran(s, GIANT_SPIDER_RUN_AB98);
      if (count_down(s, 0x0004)) {
        taken(s);
        return AT(0xab76);
      }
      ran(s, GIANT_SPIDER_RUN_ABA2);
      return AT(0xaad2);

    // `$83:ABDB`: webs at the nearest player within 190.
    case AT(0xabdb):
      lda(s, 0x00be);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, GIANT_SPIDER_RUN_ABDB);
      return jsr(s, 0xabe2, 0xa87b);
    case AT(0xabe5):
      cmp(s, 0);
      ran(s, GIANT_SPIDER_RUN_ABE5);
      if (zero(s)) {
        PORT_COVER(giant_spider_no_webs);
        taken(s);
        ran(s, GIANT_SPIDER_RUN_AC12);
        return rts(s);
      }
      PORT_COVER(giant_spider_webs);
      dec_a(s);
      dec_a(s);
      and_(s, 0x0007);
      inc_a(s);
      set_field(s, DP_AT_Y, c->a);
      lda(s, c->y);
      lsr_a(s);
      lsr_a(s);
      lsr_a(s);
      set_field(s, DP_AT_X, c->a);
      ran(s, GIANT_SPIDER_RUN_ABEA);
      return jsr(s, 0xabf8, 0xa79d);
    case AT(0xabfb):
      ldy(s, field(s, DP_RECORD));
      lda(s, 0xf673);
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      lda(s, 0x0010);
      set_field(s, DP_WEBBING, c->a);
      lda(s, 0x0004);
      set_field(s, DP_PICTURE_TICKS, c->a);
      lda(s, 0xac13);
      set_field(s, DP_STATE, c->a);
      ran(s, GIANT_SPIDER_RUN_ABFB);
      ran(s, GIANT_SPIDER_RUN_AC12);
      return rts(s);

    // `$83:AC13`: webbing, a web every eighth frame.
    case AT(0xac13):
      lda(s, field(s, DP_WEBBING));
      dec_a(s);
      set_field(s, DP_WEBBING, c->a);
      ran(s, GIANT_SPIDER_RUN_AC13);
      if (minus(s)) {
        PORT_COVER(giant_spider_webs_done);
        taken(s);
        ldy(s, field(s, DP_RECORD));
        lda(s, 0xf684);
        set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
        ran(s, GIANT_SPIDER_RUN_AC2B);
        return AT(0xaad2);
      }
      bit_imm(s, 0x0007);
      ran(s, GIANT_SPIDER_RUN_AC1A);
      if (!zero(s)) {
        taken(s);
        return AT(0xac2a);
      }
      PORT_COVER(giant_spider_webs_again);
      lda(s, field(s, DP_AT_Y));
      inc_a(s);
      and_(s, 0x0007);
      set_field(s, DP_AT_Y, c->a);
      ran(s, GIANT_SPIDER_RUN_AC1F);
      return jsr(s, 0xac27, 0xa79d);
    case AT(0xac2a):
      ran(s, GIANT_SPIDER_RUN_AC2A);
      return rts(s);

    // `$83:A79D`: a web, if fewer than three are out.
    case AT(0xa79d):
      lda(s, field(s, DP_WEBS));
      cmp(s, 0x0003);
      ran(s, GIANT_SPIDER_RUN_A79D);
      if (carry(s)) {
        PORT_COVER(giant_spider_webs_full);
        taken(s);
        ran(s, GIANT_SPIDER_RUN_A7B6);
        return rts(s);
      }
      ldx(s, 0x0010);
      ran(s, GIANT_SPIDER_RUN_A7A4);
      for (;;) {
        lda(s, field(s, (uint16_t)(DP_WEB + c->x)));
        cmp(s, GIANT_SPIDER_WEB_NONE);
        ran(s, GIANT_SPIDER_RUN_A7A7);
        if (zero(s)) {
          taken(s);
          break;
        }
        lda(s, c->x);
        sub(s, 0x0008);
        ldx(s, c->a);
        ran(s, GIANT_SPIDER_RUN_A7AE);
        if (minus(s)) {
          PORT_COVER(giant_spider_webs_none_free);
          ran(s, GIANT_SPIDER_RUN_A7B6);
          return rts(s);
        }
        taken(s);
      }
      PORT_COVER(giant_spider_web);
      inc_field(s, DP_WEBS);
      push16(s->w, c, c->x);
      ran(s, GIANT_SPIDER_RUN_A7B7);
      return stop(AT(0xa7ba));
    case AT(0xa7be): {
      ldx(s, pull16(s->w, c));
      set_field(s, (uint16_t)(DP_WEB + c->x), c->a);
      push16(s->w, c, c->a);
      lda(s, field(s, DP_AT_X));
      set_field(s, (uint16_t)(DP_WEB_LEFT + c->x), c->a);
      lda(s, field(s, DP_AT_Y));
      asl_a(s);
      asl_a(s);
      asl_a(s);
      ldy(s, c->a);
      const uint16_t y = c->y;
      lda(s, data(s, (uint16_t)(T_WEB_WAY + y)));
      set_field(s, (uint16_t)(DP_WEB_DX + c->x), c->a);
      lda(s, data(s, (uint16_t)(T_WEB_WAY + 2 + y)));
      set_field(s, (uint16_t)(DP_WEB_DY + c->x), c->a);
      lda(s, data(s, (uint16_t)(T_WEB_WAY + 4 + y)));
      set_field(s, DP_SCRATCH, c->a);
      lda(s, data(s, (uint16_t)(T_WEB_WAY + 6 + y)));
      set_field(s, DP_SCRATCH2, c->a);
      ldy(s, pull16(s->w, c));
      const uint16_t web = c->y;
      lda(s, field(s, DP_X));
      set_data(s, (uint16_t)(web + REC_X), c->a);
      lda(s, 0);
      set_data(s, (uint16_t)(web + REC_X_FRACTION), 0);
      lda(s, field(s, DP_Y));
      set_data(s, (uint16_t)(web + REC_Y), c->a);
      lda(s, field(s, DP_SCRATCH));
      set_data(s, (uint16_t)(web + REC_PICTURES), c->a);
      lda(s, 0x0090);
      set_data(s, (uint16_t)(web + REC_PICTURES_BANK), c->a);
      lda(s, 0x0009);
      set_data(s, (uint16_t)(web + REC_COLLIDE_ID), c->a);
      lda(s, data(s, W_FRAME));
      set_data(s, (uint16_t)(web + REC_BORN), c->a);
      lda(s, field(s, DP_SCRATCH2));
      ora(s, 0x0008);
      ora(s, data(s, (uint16_t)(web + REC_FLAGS)));
      set_data(s, (uint16_t)(web + REC_FLAGS), c->a);
      ran(s, GIANT_SPIDER_RUN_A7BE);
      return rts(s);
    }
  }
  s->k->lost = true;
  return stop(pc);
}

// --- The machine --------------------------------------------------------------

void giant_spider_work_init(GiantSpiderWork* k) {
  k->trace.n = 0;
  k->trace.cap = GIANT_SPIDER_TRACE_STEPS;
  k->trace.full = false;
  k->trace.step = k->steps;
  k->lost = false;
  k->v_unknown = false;
}

void giant_spider_run(Wram* w, const Rom* rom, PortCpu* c, GiantSpiderWork* k) {
  GiantSpider s = {w, rom, c, k};
  uint32_t pc = c->pc & 0xffffffu;
  for (int i = 0; i < 1024; i++) {
    pc = block(&s, pc);
    if (k->lost) {
      c->pc = pc & 0xffffffu;
      return;
    }
    if (pc & STOP) {
      c->pc = pc & 0xffffffu;
      return;
    }
  }
  k->lost = true;
}

// --- `$83:B13D`, its handler ---------------------------------------------------

#define HANDLER_SELF_OFF 0x0000u
#define HANDLER_SELF_SHOT 0x0009u

static void piece(HandlerPieces* p, HandlerPiece what) {
  if (p && p->count < HANDLER_PIECES_MAX) p->piece[p->count++] = (uint8_t)what;
}

bool giant_spider_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                    ActorHandlerRegs* r, HandlerPieces* pieces) {
  // `LDY $0078 : LDX $000E,Y`, and two `CPX`.
  const uint16_t record = wram_r16(w, W_HANDLER_SELF);
  const uint16_t self_id = wram_r16(w, (uint32_t)record + ACTOR_COLLIDE_ID);
  r->a = arg;
  r->x = self_id;
  r->y = record;
  if (self_id == HANDLER_SELF_OFF || self_id == HANDLER_SELF_SHOT) {
    PORT_COVER(giant_spider_hit_self);
    piece(pieces, self_id == HANDLER_SELF_OFF ? HP_GIANT_SPIDER_SELF_OFF
                                              : HP_GIANT_SPIDER_SELF_SHOT);
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }
  // `LDX $58 : BNE`: it is flashing.
  const uint16_t flash = wram_r16(w, (uint32_t)dp + GIANT_SPIDER_DP_FLASH);
  r->x = flash;
  if (flash != 0) {
    PORT_COVER(giant_spider_hit_flashing);
    piece(pieces, HP_GIANT_SPIDER_FLASHING);
    r->n = (flash & 0x8000u) != 0;
    r->z = false;
    r->c = false;
    return true;
  }
  if (arg < COLLIDE_ID_PLAYER) {
    PORT_COVER(giant_spider_hit_ignore);
    piece(pieces, HP_GIANT_SPIDER_IGNORE);
    r->n = ((uint16_t)(arg - COLLIDE_ID_PLAYER) & 0x8000u) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$83:B158  STA $5A : AND #$7FFF`, and the ids it answers as others.
  wram_w16(w, (uint32_t)dp + GIANT_SPIDER_DP_HIT_BY, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  const uint16_t tick = wram_r16(w, W_SCHED_TICK);
  if (id == 0x0060 || id == 0x0070) {
    const bool eighth = (tick & 0x0007u) == 0;
    PORT_COVER(giant_spider_hit_tossed);
    piece(pieces, id == 0x0060 ? (eighth ? HP_GIANT_SPIDER_60_5C : HP_GIANT_SPIDER_60_5D)
                               : (eighth ? HP_GIANT_SPIDER_70_5C : HP_GIANT_SPIDER_70_5D));
    id = eighth ? 0x005c : 0x005d;
  } else if (id == 0x006f) {
    PORT_COVER(giant_spider_hit_6f);
    if (tick & 0x0007u) {
      piece(pieces, HP_GIANT_SPIDER_6F_5D);
      id = 0x005d;
    } else if (tick & 0x0008u) {
      piece(pieces, HP_GIANT_SPIDER_6F_63);
      id = 0x0063;
    } else {
      piece(pieces, HP_GIANT_SPIDER_6F_5C);
      id = 0x005c;
    }
  } else if (id == 0x0061 || id == 0x0062) {
    PORT_COVER(giant_spider_hit_as_5c);
    piece(pieces, id == 0x0061 ? HP_GIANT_SPIDER_61 : HP_GIANT_SPIDER_62);
    id = 0x005c;
  } else {
    piece(pieces, HP_GIANT_SPIDER_OTHER);
  }

  // `$83:B1A0  SEC : SBC #$005C : ASL : TAX : DEC $56`, and the damage added
  // to `$64`. At 5 it chooses a new way, by `JSR $AAE1`.
  const uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  const uint16_t damage = rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index);
  r->x = index;
  wram_w16(w, (uint32_t)dp + GIANT_SPIDER_DP_HIT,
           (uint16_t)(wram_r16(w, (uint32_t)dp + GIANT_SPIDER_DP_HIT) - 1));
  const uint16_t stung =
      (uint16_t)(wram_r16(w, (uint32_t)dp + GIANT_SPIDER_DP_STUNG) + damage);
  wram_w16(w, (uint32_t)dp + GIANT_SPIDER_DP_STUNG, stung);
  if (stung < 0x0005u) {
    piece(pieces, HP_GIANT_SPIDER_STUNG);
  } else {
    // `$83:AAE1`: a way of eight at random, `$64` cleared, and going.
    // `rng_next` is called with the carry the `CMP #$0005` left: set.
    PORT_COVER(giant_spider_hit_turns);
    RngResult rng;
    rng_next(w, true, &rng);
    piece(pieces, rng.v ? HP_GIANT_SPIDER_TURNS_TWICE : HP_GIANT_SPIDER_TURNS_ONCE);
    wram_w16(w, (uint32_t)dp + 0x42u, (uint16_t)((((rng.a & 7u) + 1u) << 1)));
    wram_w16(w, (uint32_t)dp + GIANT_SPIDER_DP_STUNG, 0);
    wram_w16(w, (uint32_t)dp + 0x4cu, 0x0002);
    wram_w16(w, (uint32_t)dp + GIANT_SPIDER_DP_STATE, 0xaafc);
  }

  // `$83:B1B9  SEC : LDA $54 : SBC $818561,X : BMI`.
  const uint16_t health = wram_r16(w, (uint32_t)dp + GIANT_SPIDER_DP_HEALTH);
  const uint16_t left = (uint16_t)(health - damage);
  r->a = left;
  if (left & 0x8000u) {
    // `DEC $52 : SEC : RTL`.
    PORT_COVER(giant_spider_hit_broken);
    piece(pieces, HP_GIANT_SPIDER_DIED);
    const uint16_t broken =
        (uint16_t)(wram_r16(w, (uint32_t)dp + GIANT_SPIDER_DP_BROKEN) - 1);
    wram_w16(w, (uint32_t)dp + GIANT_SPIDER_DP_BROKEN, broken);
    r->n = (broken & 0x8000u) != 0;
    r->z = broken == 0;
    r->c = true;
    return true;
  }
  // `CMP $54 : BEQ`, then `STA $54`: N and Z are the compare's.
  r->n = ((uint16_t)(left - health) & 0x8000u) != 0;
  r->z = left == health;
  r->c = true;
  if (left == health) {
    PORT_COVER(giant_spider_hit_no_damage);
    piece(pieces, HP_GIANT_SPIDER_NO_DAMAGE);
    return true;
  }
  PORT_COVER(giant_spider_hit_hurt);
  piece(pieces, HP_GIANT_SPIDER_SURVIVED);
  wram_w16(w, (uint32_t)dp + GIANT_SPIDER_DP_HEALTH, left);
  return true;
}
