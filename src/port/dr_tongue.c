// Dr. Tongue and his head, and its shot -- see port/dr_tongue.h.

#include "port/dr_tongue.h"

#include "port/coverage.h"
#include "port/oam.h"
#include "port/rng.h"

typedef struct {
  Wram* w;
  const Rom* rom;
  PortCpu* c;
  DrTongueWork* k;
} DrTongue;

// A block hands back the 24-bit address it goes to next, or `STOP` and the
// instruction it stopped on.
#define STOP 0x01000000u
#define AT(a) (0x830000u | (a))

// --- Its words ---------------------------------------------------------------

#define W_LOAD 0x00deu           // the level's load
#define W_FRAME 0x0008u          // copied into a record as it is made
#define W_TICK 0x0020u
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
#define W_BEARING_X 0x00a2u      // `$83:BD1D`'s two words
#define W_BEARING_Y 0x00a4u
#define W_BOX 0x0038u            // `actor_notify_box`'s box: four words...
#define W_BOX_RIGHT 0x003au
#define W_BOX_TOP 0x003cu
#define W_BOX_BOTTOM 0x003eu
#define W_BOX_KIND 0x0040u       // ...and what it is
#define W_PLAYER_RECORDS 0x00d2u // a word for each player, nought for none
#define W7E_MOSAIC 0x6db2u       // in bank `$7E`
#define W7E_SNAKEOID_UP 0x6058u

// Its page.
#define DP_AT_X 0x00             // the point it was given, and a place to
#define DP_AT_Y 0x02             // begin a thread at, and what to begin it
#define DP_ARG 0x04              // with
#define DP_ARG2 0x06
#define DP_RECORD DR_TONGUE_DP_RECORD
#define DP_LIFE DR_TONGUE_DP_LIFE
#define DP_TONGUES 0x16          // how many are out, of three
#define DP_TONGUE 0x18           // three of these four words, 8 apart: the
#define DP_TONGUE_LEFT 0x1a      // record or none; frames to go out, or
#define DP_TONGUE_DX 0x1c        // negative to come back; and its step
#define DP_TONGUE_DY 0x1e
#define DP_SPIT_LEFT 0x30        // frames to its next shots
#define DP_X 0x32
#define DP_Y 0x34
#define DP_COUNT 0x36
#define DP_STEP_X 0x38           // which way it sweeps...
#define DP_STEP_Y 0x3a           // ...across, or up and down
#define DP_WAY DR_TONGUE_DP_WAY
#define DP_FACING DR_TONGUE_DP_FACING
#define DP_STATE DR_TONGUE_DP_STATE
#define DP_TARGET DR_TONGUE_DP_TARGET
#define DP_PICTURE 0x44          // the picture it wants...
#define DP_SHOWN 0x46            // ...the one up...
#define DP_MIRRORED 0x48         // ...and `$8000` to have it mirrored
#define DP_BROKEN DR_TONGUE_DP_BROKEN
#define DP_HEALTH DR_TONGUE_DP_HEALTH
#define DP_HIT DR_TONGUE_DP_HIT
#define DP_FLASH DR_TONGUE_DP_FLASH
#define DP_HIT_BY DR_TONGUE_DP_HIT_BY
#define DP_TO_X 0x54             // where it would go
#define DP_TO_Y 0x56
#define DP_PROBE_X 0x58
#define DP_PROBE_Y 0x5a
#define DP_STUNG DR_TONGUE_DP_STUNG
#define DP_LEFT DR_TONGUE_DP_LEFT
#define DP_PICTURE_TICKS 0x60
#define DP_TEMP DR_TONGUE_DP_TEMP

// The shot's page.
#define SHOT_RECORD DR_TONGUE_SHOT_DP_RECORD
#define SHOT_X 0x0a
#define SHOT_Y 0x0c
#define SHOT_DX 0x0e
#define SHOT_DY 0x10
#define SHOT_WAY 0x12
#define SHOT_GONE DR_TONGUE_SHOT_DP_GONE
#define SHOT_FRAME 0x16
#define SHOT_FRAME_LEFT 0x18
#define SHOT_LEFT 0x1a           // frames to fly
#define SHOT_FLASH 0x1c          // frames hidden

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
#define T_WAY_STEP 0xb9a9u       // a way's step across and down, 4 apart
#define T_WAY_STEP_Y 0xb9abu
#define T_PART_AT 0xb9cdu        // each record's place, 4 apart
#define T_PART_AT_Y 0xb9cfu
#define T_SPIT_AT 0xb9e5u        // where its shots begin, 4 apart
#define T_SPIT_AT_Y 0xb9e7u
#define T_FIGURES 0xba47u        // a picture's far address...
#define T_FIGURES_BANK 0xba49u
#define T_FIGURE_AT 0xba7fu      // ...and its offsets
#define T_FIGURE_AT_Y 0xba81u
#define T_TONGUE_OUT 0xbbf2u     // a tongue's pictures going out down...
#define T_TONGUE_OUT_MOVING 0xbbfau  // ...or across...
#define T_TONGUE_BACK 0xbc02u    // ...and coming back
#define T_TONGUE_WAY 0xbc81u     // a way's step, picture and flags, 8 apart
#define T_TONGUE_WAY_DY 0xbc83u
#define T_TONGUE_WAY_PICTURE 0xbc85u
#define T_TONGUE_WAY_FLAGS 0xbc87u
#define T_PROBES 0xbdc8u         // its two probes, 4 apart
#define T_PROBES_Y 0xbdcau
#define T_FACING_PICTURE 0xbf26u
#define T_PUFFS_AT 0xbfafu       // a life's three puffs, 4 apart
#define T_PUFFS_AT_Y 0xbfb1u
#define T_PART_PICTURES 0xc5ceu  // each record's picture, and its flags
#define T_PART_FLAGS 0xc5d0u
#define T_BLINK 0xc78fu          // Dr. Tongue's two pictures
#define T_DEATH_PUFFS_AT 0xc909u // its death's twelve puffs, 4 apart
#define T_DEATH_PUFFS_AT_Y 0xc90bu
#define T_SHOT_TURN 0xcb34u      // a way's turn across and down, 4 apart
#define T_SHOT_TURN_DY 0xcb36u
#define T_SHOT_FRAMES 0xcb74u
#define T_SHOT_WAY 0xcbdfu       // a way's step, 4 apart
#define T_SHOT_WAY_DY 0xcbe1u

// --- The machine, as these blocks use it ------------------------------------

static uint16_t field(const DrTongue* s, uint16_t at) {
  return wram_r16(s->w, (uint16_t)(s->c->d + at));
}

static void set_field(DrTongue* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, (uint16_t)(s->c->d + at), v);
}

// A word through the data bank, `$83`: low memory, or a table of its own.
static uint16_t data(const DrTongue* s, uint16_t at) {
  return bus_r16(s->w, s->rom, (uint32_t)s->c->db << 16 | at);
}

// ...and one written, which is always low memory.
static void set_data(DrTongue* s, uint16_t at, uint16_t v) {
  if (at >= 0x1fffu) {
    s->k->lost = true;  // somewhere this file does not write
    return;
  }
  wram_w16(s->w, at, v);
}

// A word by its long address: the way of eight, in the cartridge.
static uint16_t far(const DrTongue* s, uint32_t at) {
  return bus_r16(s->w, s->rom, at & 0xffffffu);
}

static uint16_t w7e(const DrTongue* s, uint16_t at) {
  return wram_r16(s->w, at);
}

static void set_w7e(DrTongue* s, uint16_t at, uint16_t v) {
  wram_w16(s->w, at, v);
}

static void ran(DrTongue* s, int run) { hw_run(&s->k->trace, run); }
static void taken(DrTongue* s) { hw_run(&s->k->trace, DR_TONGUE_RUN_TAKEN); }

static bool zero(const DrTongue* s) { return flag(s->c, PORT_P_Z); }
static bool carry(const DrTongue* s) { return flag(s->c, PORT_P_C); }
static bool minus(const DrTongue* s) { return flag(s->c, PORT_P_N); }

static void lda(DrTongue* s, uint16_t v) {
  s->c->a = v;
  set_nz16(s->c, v);
}

static void ldx(DrTongue* s, uint16_t v) {
  s->c->x = v;
  set_nz16(s->c, v);
}

static void ldy(DrTongue* s, uint16_t v) {
  s->c->y = v;
  set_nz16(s->c, v);
}

static void and_(DrTongue* s, uint16_t v) { lda(s, s->c->a & v); }
static void ora(DrTongue* s, uint16_t v) { lda(s, s->c->a | v); }
static void eor(DrTongue* s, uint16_t v) { lda(s, s->c->a ^ v); }
static void inc_a(DrTongue* s) { lda(s, (uint16_t)(s->c->a + 1)); }
static void dec_a(DrTongue* s) { lda(s, (uint16_t)(s->c->a - 1)); }
static void asl_a(DrTongue* s) { s->c->a = asl16(s->c, s->c->a); }

static void lsr_a(DrTongue* s) {
  set_c(s->c, (s->c->a & 1) != 0);
  lda(s, s->c->a >> 1);
}

// `ADC` and `SBC` with the carry as it stands, and the `CLC`/`SEC` forms.
// Each says what overflow is.
static void adc(DrTongue* s, uint16_t v) {
  s->c->a = adc16(s->c, s->c->a, v);
  s->k->v_unknown = false;
}

static void sbc(DrTongue* s, uint16_t v) {
  s->c->a = sbc16(s->c, s->c->a, v);
  s->k->v_unknown = false;
}

static void add(DrTongue* s, uint16_t v) {
  set_c(s->c, false);
  adc(s, v);
}

static void sub(DrTongue* s, uint16_t v) {
  set_c(s->c, true);
  sbc(s, v);
}

static void cmp(DrTongue* s, uint16_t v) { cmp16(s->c, s->c->a, v); }
static void cpx(DrTongue* s, uint16_t v) { cmp16(s->c, s->c->x, v); }
static void cpy(DrTongue* s, uint16_t v) { cmp16(s->c, s->c->y, v); }

// `BIT #`: only Z.
static void bit_imm(DrTongue* s, uint16_t v) {
  s->c->p = (uint8_t)((s->c->a & v) == 0 ? s->c->p | PORT_P_Z
                                         : s->c->p & ~PORT_P_Z);
}

// `BIT` of a word on the page: N, V and Z.
static void bit_field(DrTongue* s, uint16_t at) {
  bit16(s->c, field(s, at));
  s->k->v_unknown = false;
}

// `DEC` and `INC` of a word on the page.
static uint16_t dec_field(DrTongue* s, uint16_t at) {
  const uint16_t v = (uint16_t)(field(s, at) - 1);
  set_field(s, at, v);
  set_nz16(s->c, v);
  return v;
}

static uint16_t inc_field(DrTongue* s, uint16_t at) {
  const uint16_t v = (uint16_t)(field(s, at) + 1);
  set_field(s, at, v);
  set_nz16(s->c, v);
  return v;
}

// A `JSR` at `at`, going to `to`.
static uint32_t jsr(DrTongue* s, uint16_t at, uint16_t to) {
  push16(s->w, s->c, (uint16_t)(at + 2));
  return AT(to);
}

// An `RTS`: where it goes is what it pulls, and one on.
static uint32_t rts(DrTongue* s) {
  return AT((uint16_t)(pull16(s->w, s->c) + 1));
}

static uint32_t stop(uint32_t at) { return STOP | at; }

// A `JSL` at `at` made here, and its `RTL`.
static void jsl(DrTongue* s, uint16_t at) {
  push8(s->w, s->c, DR_TONGUE_BANK);
  push16(s->w, s->c, (uint16_t)(at + 3));
}

static void rtl(DrTongue* s) {
  pull16(s->w, s->c);
  pull8(s->w, s->c);
}

// `JSL rng_next` at `at`.
static void draw(DrTongue* s, uint16_t at) {
  RngResult r;
  jsl(s, at);
  rng_next(s->w, carry(s), &r);
  rtl(s);
  ran(s, r.v ? DR_TONGUE_RUN_RNG_TWICE : DR_TONGUE_RUN_RNG_ONCE);
  s->c->a = r.a;
  set_nz16(s->c, r.a);
  s->c->p = (uint8_t)(r.n ? s->c->p | PORT_P_N : s->c->p & ~PORT_P_N);
  s->c->p = (uint8_t)(r.z ? s->c->p | PORT_P_Z : s->c->p & ~PORT_P_Z);
  set_c(s->c, r.c);
  set_v(s->c, r.v);
  s->k->v_unknown = false;
}

// `$83:BDD0`, by its `JSR`: would a probe stand at X and Y? The Snakeoid's
// `$82:A214`, which pushes its page and the point less 17, and pulls both.
static void wide(DrTongue* s) {
  PortCpu* c = s->c;
  TerrainRegs r;
  TerrainWideWork work = {{0}};
  push16(s->w, c, c->d);
  push16(s->w, c, (uint16_t)(c->x - 0x0011u));
  pull16(s->w, c);
  terrain_blocked_wide_low_counted(s->w, c->x, c->y, &r, &work);
  pull16(s->w, c);
  for (int i = 0; i < WIDE_BLOCK_COUNT; i++)
    hw_run_n(&s->k->trace, DR_TONGUE_RUN_WIDE + i, work.blocks[i]);
  c->a = r.a;
  c->x = r.x;
  c->y = r.y;
  set_nz16(c, c->d);  // `PLD`
  set_c(c, r.blocked);
  s->k->v_unknown = true;
}

// The `RTS` at `$83:C6B3` goes to its state, the routine at `$40`, which
// must be one of the seven.
static uint32_t dispatched(DrTongue* s, uint32_t state) {
  switch (state) {
    case AT(0xc030): case AT(0xc0d4): case AT(0xc199): case AT(0xc256):
    case AT(0xc31b): case AT(0xc3fd): case AT(0xc518):
      return state;
  }
  s->k->lost = true;
  return state;
}

// --- The threads -------------------------------------------------------------

static uint32_t block(DrTongue* s, uint32_t pc) {
  PortCpu* c = s->c;
  switch (pc) {
    // `$83:B9F5`: its picture put up on its layer, at (24, 64)
    // (`boss_bg_queue`)...
    case AT(0xb9f5):
      lda(s, 0x0018);
      set_data(s, W_PLANE_AT_X, c->a);
      lda(s, 0x0040);
      set_data(s, W_PLANE_AT_Y, c->a);
      ran(s, DR_TONGUE_RUN_B9F5);
      return stop(AT(0xba01));
    case AT(0xba05):
      ran(s, DR_TONGUE_RUN_BA05);
      return rts(s);

    // `$83:BA06`: ...or mirrored (`boss_bg_queue_flip`).
    case AT(0xba06):
      lda(s, 0x0018);
      set_data(s, W_PLANE_AT_X, c->a);
      lda(s, 0x0040);
      set_data(s, W_PLANE_AT_Y, c->a);
      ran(s, DR_TONGUE_RUN_BA06);
      return stop(AT(0xba12));
    case AT(0xba16):
      ran(s, DR_TONGUE_RUN_BA16);
      return rts(s);

    // `$83:BA17`: picture A of its set: the figure and its offsets, put up.
    case AT(0xba17):
      asl_a(s);
      asl_a(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_FIGURES + c->x)));
      set_data(s, W_FIGURE, c->a);
      lda(s, data(s, (uint16_t)(T_FIGURES_BANK + c->x)));
      set_data(s, W_FIGURE_BANK, c->a);
      lda(s, data(s, (uint16_t)(T_FIGURE_AT + c->x)));
      bit_field(s, DP_MIRRORED);
      ran(s, DR_TONGUE_RUN_BA17);
      if (!minus(s)) {
        taken(s);
        return AT(0xba31);
      }
      eor(s, 0xffff);
      inc_a(s);
      ran(s, DR_TONGUE_RUN_BA2D);
      return AT(0xba31);
    case AT(0xba31):
      set_data(s, W_PLANE_DX, c->a);
      lda(s, data(s, (uint16_t)(T_FIGURE_AT_Y + c->x)));
      set_data(s, W_PLANE_DY, c->a);
      lda(s, field(s, DP_MIRRORED));
      ran(s, DR_TONGUE_RUN_BA31);
      if (!zero(s)) {
        taken(s);
        return AT(0xba43);
      }
      PORT_COVER(dr_tongue_picture_plain);
      ran(s, DR_TONGUE_RUN_BA3E);
      return jsr(s, 0xba3e, 0xb9f5);
    case AT(0xba41):
      ran(s, DR_TONGUE_RUN_BA41);
      taken(s);
      return AT(0xba46);
    case AT(0xba43):
      PORT_COVER(dr_tongue_picture_mirrored);
      ran(s, DR_TONGUE_RUN_BA43);
      return jsr(s, 0xba43, 0xba06);
    case AT(0xba46):
      ran(s, DR_TONGUE_RUN_BA46);
      return rts(s);

    // `$83:BAB7`: its flash.
    case AT(0xbab7):
      lda(s, field(s, DP_FLASH));
      ran(s, DR_TONGUE_RUN_BAB7);
      if (zero(s)) {
        taken(s);
        return AT(0xbaca);
      }
      dec_field(s, DP_FLASH);
      ran(s, DR_TONGUE_RUN_BABB);
      if (!zero(s)) {
        taken(s);
        return AT(0xbadf);
      }
      PORT_COVER(dr_tongue_unflash);
      lda(s, 0xffd1);
      ldy(s, 0x0092);
      ran(s, DR_TONGUE_RUN_BABF);
      return stop(AT(0xbac5));
    case AT(0xbac9):
      ran(s, DR_TONGUE_RUN_BAC9);
      return rts(s);
    case AT(0xbaca):
      bit_field(s, DP_HIT);
      ran(s, DR_TONGUE_RUN_BACA);
      if (!minus(s)) {
        taken(s);
        return AT(0xbadf);
      }
      PORT_COVER(dr_tongue_flash);
      set_field(s, DP_HIT, 0);
      lda(s, 0x0002);
      set_field(s, DP_FLASH, c->a);
      lda(s, 0xffc3);
      ldy(s, 0x0095);
      ran(s, DR_TONGUE_RUN_BACE);
      return stop(AT(0xbadb));
    case AT(0xbadf):
      ran(s, DR_TONGUE_RUN_BADF);
      return rts(s);

    // `$83:BAE0`: where it is on its layer, and once the last picture is up,
    // every twelve frames the picture it wants.
    case AT(0xbae0):
      lda(s, field(s, DP_X));
      set_data(s, W_PLANE_X, c->a);
      lda(s, field(s, DP_Y));
      set_data(s, W_PLANE_Y, c->a);
      ran(s, DR_TONGUE_RUN_BAE0);
      jsl(s, 0xbaea);
      lda(s, data(s, W_BG_DMA_CURSOR));
      ran(s, DR_TONGUE_RUN_80E0);
      rtl(s);
      ran(s, DR_TONGUE_RUN_BAEE);
      if (!zero(s)) {
        taken(s);
        return AT(0xbb09);
      }
      dec_field(s, DP_PICTURE_TICKS);
      ran(s, DR_TONGUE_RUN_BAF0);
      if (!minus(s)) {
        taken(s);
        return AT(0xbb09);
      }
      PORT_COVER(dr_tongue_picture_ticks);
      lda(s, 0x000c);
      set_field(s, DP_PICTURE_TICKS, c->a);
      lda(s, field(s, DP_PICTURE));
      ora(s, field(s, DP_MIRRORED));
      cmp(s, field(s, DP_SHOWN));
      ran(s, DR_TONGUE_RUN_BAF4);
      if (zero(s)) {
        taken(s);
        return AT(0xbb09);
      }
      PORT_COVER(dr_tongue_picture_new);
      set_field(s, DP_SHOWN, c->a);
      and_(s, 0x7fff);
      ran(s, DR_TONGUE_RUN_BB01);
      return jsr(s, 0xbb06, 0xba17);
    case AT(0xbb09):
      ran(s, DR_TONGUE_RUN_BB09);
      return rts(s);

    // `$83:BB0A`: its layer's scroll.
    case AT(0xbb0a):
      lda(s, data(s, W_PLANE_DX));
      add(s, data(s, W_PLANE_X));
      sub(s, data(s, W_PLANE_AT_X));
      set_data(s, W_PLANE_SCROLL_X, c->a);
      lda(s, data(s, W_PLANE_DY));
      add(s, data(s, W_PLANE_Y));
      sub(s, data(s, W_PLANE_AT_Y));
      set_data(s, W_PLANE_SCROLL_Y, c->a);
      ran(s, DR_TONGUE_RUN_BB0A);
      return rts(s);

    // `$83:BB27`: each frame, its place, its flash and its scroll.
    case AT(0xbb27):
      ran(s, DR_TONGUE_RUN_BB27);
      return jsr(s, 0xbb27, 0xbae0);
    case AT(0xbb2a):
      ran(s, DR_TONGUE_RUN_BB2A);
      return jsr(s, 0xbb2a, 0xbab7);
    case AT(0xbb2d):
      ran(s, DR_TONGUE_RUN_BB2D);
      return jsr(s, 0xbb2d, 0xbb0a);
    case AT(0xbb30):
      ran(s, DR_TONGUE_RUN_BB30);
      return rts(s);

    // `$83:BB31`: no tongue out.
    case AT(0xbb31):
      set_field(s, DP_TONGUES, 0);
      ldx(s, 0x0010);
      ldy(s, 0xffff);
      ran(s, DR_TONGUE_RUN_BB31);
      return AT(0xbb39);
    case AT(0xbb39):
      set_field(s, (uint16_t)(DP_TONGUE + c->x), c->y);
      lda(s, c->x);
      sub(s, 0x0008);
      ldx(s, c->a);
      ran(s, DR_TONGUE_RUN_BB39);
      if (!minus(s)) {
        taken(s);
        return AT(0xbb39);
      }
      ran(s, DR_TONGUE_RUN_BB43);
      return rts(s);

    // `$83:BB44`: each tongue out...
    case AT(0xbb44):
      ldx(s, 0x0010);
      set_field(s, DP_COUNT, c->x);
      ran(s, DR_TONGUE_RUN_BB44);
      return AT(0xbb49);
    case AT(0xbb49):
      ldx(s, field(s, DP_COUNT));
      ldy(s, field(s, (uint16_t)(DP_TONGUE + c->x)));
      cpy(s, 0xffff);
      ran(s, DR_TONGUE_RUN_BB49);
      if (zero(s)) {
        taken(s);
        return AT(0xbb55);
      }
      ran(s, DR_TONGUE_RUN_BB52);
      return jsr(s, 0xbb52, 0xbb60);
    case AT(0xbb55):
      lda(s, field(s, DP_COUNT));
      sub(s, 0x0008);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_BB55);
      if (!minus(s)) {
        taken(s);
        return AT(0xbb49);
      }
      ran(s, DR_TONGUE_RUN_BB5F);
      return rts(s);

    // `$83:BB60`: ...goes out, comes back, and is gone.
    case AT(0xbb60):
      ldx(s, field(s, DP_COUNT));
      lda(s, field(s, (uint16_t)(DP_TONGUE_LEFT + c->x)));
      ran(s, DR_TONGUE_RUN_BB60);
      if (!minus(s)) {
        taken(s);
        return AT(0xbb92);
      }
      PORT_COVER(dr_tongue_tongue_back);
      inc_a(s);
      set_field(s, (uint16_t)(DP_TONGUE_LEFT + c->x), c->a);
      ran(s, DR_TONGUE_RUN_BB66);
      if (minus(s)) {
        taken(s);
        return AT(0xbb70);
      }
      ran(s, DR_TONGUE_RUN_BB6B);
      return AT(0xbbe2);
    case AT(0xbb70):
      eor(s, 0xffff);
      inc_a(s);
      bit_imm(s, 0x0003);
      ran(s, DR_TONGUE_RUN_BB70);
      if (zero(s)) {
        taken(s);
        return AT(0xbb7c);
      }
      ran(s, DR_TONGUE_RUN_BB79);
      return AT(0xbbe1);
    case AT(0xbb7c):
      PORT_COVER(dr_tongue_tongue_bites);
      lsr_a(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_TONGUE_BACK + c->x)));
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      lda(s, data(s, (uint16_t)(c->y + REC_X)));
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(c->y + REC_Y)));
      ldy(s, c->a);
      ran(s, DR_TONGUE_RUN_BB7C);
      return jsr(s, 0xbb8c, 0xbedd);
    case AT(0xbb8f):
      ran(s, DR_TONGUE_RUN_BB8F);
      return AT(0xbbe1);
    case AT(0xbb92):
      PORT_COVER(dr_tongue_tongue_goes);
      dec_a(s);
      set_field(s, (uint16_t)(DP_TONGUE_LEFT + c->x), c->a);
      ran(s, DR_TONGUE_RUN_BB92);
      if (minus(s)) {
        taken(s);
        return AT(0xbbcc);
      }
      push16(s->w, c, c->x);
      and_(s, 0x000c);
      lsr_a(s);
      push16(s->w, c, c->a);
      lda(s, field(s, (uint16_t)(DP_TONGUE_DX + c->x)));
      ran(s, DR_TONGUE_RUN_BB97);
      if (zero(s)) {
        taken(s);
        return AT(0xbba7);
      }
      PORT_COVER(dr_tongue_tongue_side);
      ldx(s, pull16(s->w, c));
      lda(s, data(s, (uint16_t)(T_TONGUE_OUT_MOVING + c->x)));
      ran(s, DR_TONGUE_RUN_BBA1);
      taken(s);
      return AT(0xbbab);
    case AT(0xbba7):
      PORT_COVER(dr_tongue_tongue_down);
      ldx(s, pull16(s->w, c));
      lda(s, data(s, (uint16_t)(T_TONGUE_OUT + c->x)));
      ran(s, DR_TONGUE_RUN_BBA7);
      return AT(0xbbab);
    case AT(0xbbab):
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      ldx(s, pull16(s->w, c));
      lda(s, data(s, (uint16_t)(c->y + REC_X)));
      add(s, field(s, (uint16_t)(DP_TONGUE_DX + c->x)));
      set_data(s, (uint16_t)(c->y + REC_X), c->a);
      push16(s->w, c, c->a);
      lda(s, data(s, (uint16_t)(c->y + REC_Y)));
      add(s, field(s, (uint16_t)(DP_TONGUE_DY + c->x)));
      set_data(s, (uint16_t)(c->y + REC_Y), c->a);
      ldy(s, c->a);
      ldx(s, pull16(s->w, c));
      ran(s, DR_TONGUE_RUN_BBAB);
      return stop(AT(0xbbc4));
    case AT(0xbbc8):
      ran(s, DR_TONGUE_RUN_BBC8);
      if (carry(s)) {
        taken(s);
        return AT(0xbbcc);
      }
      ran(s, DR_TONGUE_RUN_BBCA);
      taken(s);
      return AT(0xbbe1);
    case AT(0xbbcc):
      PORT_COVER(dr_tongue_tongue_turns);
      ldx(s, field(s, DP_COUNT));
      ldy(s, field(s, (uint16_t)(DP_TONGUE + c->x)));
      lda(s, 0xfff0);
      set_field(s, (uint16_t)(DP_TONGUE_LEFT + c->x), c->a);
      lda(s, 0xc3bd);
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      lda(s, 0x0090);
      set_data(s, (uint16_t)(c->y + REC_PICTURES_BANK), c->a);
      ran(s, DR_TONGUE_RUN_BBCC);
      return AT(0xbbe1);
    case AT(0xbbe1):
      ran(s, DR_TONGUE_RUN_BBE1);
      return rts(s);
    case AT(0xbbe2):
      PORT_COVER(dr_tongue_tongue_gone);
      ldx(s, field(s, DP_COUNT));
      ldy(s, 0xffff);
      lda(s, field(s, (uint16_t)(DP_TONGUE + c->x)));
      set_field(s, (uint16_t)(DP_TONGUE + c->x), c->y);
      ran(s, DR_TONGUE_RUN_BBE2);
      return stop(AT(0xbbeb));
    case AT(0xbbef):
      dec_field(s, DP_TONGUES);
      ran(s, DR_TONGUE_RUN_BBEF);
      return rts(s);

    // `$83:BC0A`: a tongue sent out, for `$00` frames, the way `$02`
    // says, if fewer than three are.
    case AT(0xbc0a):
      lda(s, field(s, DP_TONGUES));
      cmp(s, 0x0003);
      ran(s, DR_TONGUE_RUN_BC0A);
      if (carry(s)) {
        taken(s);
        return AT(0xbc23);
      }
      ldx(s, 0x0010);
      ran(s, DR_TONGUE_RUN_BC11);
      return AT(0xbc14);
    case AT(0xbc14):
      lda(s, field(s, (uint16_t)(DP_TONGUE + c->x)));
      cmp(s, 0xffff);
      ran(s, DR_TONGUE_RUN_BC14);
      if (zero(s)) {
        taken(s);
        return AT(0xbc24);
      }
      lda(s, c->x);
      sub(s, 0x0008);
      ldx(s, c->a);
      ran(s, DR_TONGUE_RUN_BC1B);
      if (!minus(s)) {
        taken(s);
        return AT(0xbc14);
      }
      return AT(0xbc23);
    case AT(0xbc23):
      ran(s, DR_TONGUE_RUN_BC23);
      return rts(s);
    case AT(0xbc24):
      PORT_COVER(dr_tongue_tongue_out);
      inc_field(s, DP_TONGUES);
      push16(s->w, c, c->x);
      ran(s, DR_TONGUE_RUN_BC24);
      return stop(AT(0xbc27));
    case AT(0xbc2b):
      ldx(s, pull16(s->w, c));
      set_field(s, (uint16_t)(DP_TONGUE + c->x), c->a);
      push16(s->w, c, c->a);
      lda(s, field(s, DP_AT_X));
      set_field(s, (uint16_t)(DP_TONGUE_LEFT + c->x), c->a);
      lda(s, field(s, DP_AT_Y));
      asl_a(s);
      asl_a(s);
      asl_a(s);
      ldy(s, c->a);
      lda(s, data(s, (uint16_t)(T_TONGUE_WAY + c->y)));
      set_field(s, (uint16_t)(DP_TONGUE_DX + c->x), c->a);
      lda(s, data(s, (uint16_t)(T_TONGUE_WAY_DY + c->y)));
      set_field(s, (uint16_t)(DP_TONGUE_DY + c->x), c->a);
      lda(s, data(s, (uint16_t)(T_TONGUE_WAY_PICTURE + c->y)));
      set_field(s, DP_ARG, c->a);
      lda(s, data(s, (uint16_t)(T_TONGUE_WAY_FLAGS + c->y)));
      set_field(s, DP_ARG2, c->a);
      ldy(s, pull16(s->w, c));
      lda(s, field(s, DP_X));
      set_data(s, (uint16_t)(c->y + REC_X), c->a);
      lda(s, 0x0000);
      set_data(s, (uint16_t)(c->y + REC_X_FRACTION), c->a);
      lda(s, field(s, DP_Y));
      set_data(s, (uint16_t)(c->y + REC_Y), c->a);
      lda(s, field(s, DP_ARG));
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      lda(s, 0x008f);
      set_data(s, (uint16_t)(c->y + REC_PICTURES_BANK), c->a);
      lda(s, 0x0009);
      set_data(s, (uint16_t)(c->y + REC_COLLIDE_ID), c->a);
      lda(s, data(s, W_FRAME));
      set_data(s, (uint16_t)(c->y + REC_BORN), c->a);
      lda(s, field(s, DP_ARG2));
      ora(s, 0x0008);
      ora(s, data(s, (uint16_t)(c->y + REC_FLAGS)));
      set_data(s, (uint16_t)(c->y + REC_FLAGS), c->a);
      ran(s, DR_TONGUE_RUN_BC2B);
      return rts(s);

    // `$83:BCE8`: the nearest player within A, in X, and the way of eight to
    // it, in A.
    case AT(0xbce8):
      set_field(s, DP_TEMP, c->a);
      ran(s, DR_TONGUE_RUN_BCE8);
      return stop(AT(0xbcea));
    case AT(0xbcee):
      cmp(s, field(s, DP_TEMP));
      ran(s, DR_TONGUE_RUN_BCEE);
      if (carry(s)) {
        taken(s);
        return AT(0xbd17);
      }
      PORT_COVER(dr_tongue_nearest_near);
      set_field(s, DP_TEMP, c->a);
      ldy(s, data(s, (uint16_t)(c->x + REC_COLLIDE_ID)));
      cpy(s, 0x0005);
      ran(s, DR_TONGUE_RUN_BCF2);
      if (zero(s)) {
        taken(s);
        return AT(0xbd08);
      }
      cpy(s, 0x0006);
      ran(s, DR_TONGUE_RUN_BCFC);
      if (zero(s)) {
        taken(s);
        return AT(0xbd08);
      }
      cpy(s, 0x0038);
      ran(s, DR_TONGUE_RUN_BD01);
      if (zero(s)) {
        taken(s);
        return AT(0xbd08);
      }
      ran(s, DR_TONGUE_RUN_BD06);
      taken(s);
      return AT(0xbd17);
    case AT(0xbd08):
      PORT_COVER(dr_tongue_nearest_player);
      ldy(s, data(s, (uint16_t)(c->x + REC_Y)));
      lda(s, data(s, (uint16_t)(c->x + REC_X)));
      push16(s->w, c, c->x);
      ldx(s, c->a);
      ran(s, DR_TONGUE_RUN_BD08);
      return jsr(s, 0xbd10, 0xbd1d);
    case AT(0xbd13):
      ldx(s, pull16(s->w, c));
      ldy(s, field(s, DP_TEMP));
      ran(s, DR_TONGUE_RUN_BD13);
      return rts(s);
    case AT(0xbd17):
      PORT_COVER(dr_tongue_nearest_none);
      lda(s, 0x0000);
      ldx(s, c->a);
      ldy(s, c->a);
      ran(s, DR_TONGUE_RUN_BD17);
      return rts(s);

    // `$83:BD1D`: the way of eight from it to (X, Y).
    case AT(0xbd1d):
      set_data(s, W_BEARING_X, c->x);
      set_data(s, W_BEARING_Y, c->y);
      ldx(s, 0x0000);
      lda(s, field(s, DP_Y));
      cmp(s, data(s, W_BEARING_Y));
      ran(s, DR_TONGUE_RUN_BD1D);
      if (zero(s)) {
        taken(s);
        return AT(0xbd34);
      }
      lda(s, c->x);
      adc(s, 0x0001);
      asl_a(s);
      asl_a(s);
      ldx(s, c->a);
      ran(s, DR_TONGUE_RUN_BD2D);
      return AT(0xbd34);
    case AT(0xbd34):
      lda(s, field(s, DP_X));
      cmp(s, data(s, W_BEARING_X));
      ran(s, DR_TONGUE_RUN_BD34);
      if (zero(s)) {
        taken(s);
        return AT(0xbd40);
      }
      lda(s, c->x);
      adc(s, 0x0001);
      ldx(s, c->a);
      ran(s, DR_TONGUE_RUN_BD3B);
      return AT(0xbd40);
    case AT(0xbd40):
      lda(s, far(s, 0x83bd48u + c->x));
      and_(s, 0x00ff);
      ran(s, DR_TONGUE_RUN_BD40);
      return rts(s);

    // `$83:BD55`: it goes to (`$54`, `$56`), if two probes about there say it
    // may. Carry set: it may not.
    case AT(0xbd55):
      lda(s, 0x0004);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_BD55);
      return AT(0xbd5a);
    case AT(0xbd5a):
      ldx(s, field(s, DP_COUNT));
      lda(s, field(s, DP_TO_X));
      add(s, data(s, (uint16_t)(T_PROBES + c->x)));
      set_field(s, DP_PROBE_X, c->a);
      lda(s, field(s, DP_TO_Y));
      add(s, data(s, (uint16_t)(T_PROBES_Y + c->x)));
      set_field(s, DP_PROBE_Y, c->a);
      lda(s, data(s, W_TICK));
      and_(s, 0x001f);
      ran(s, DR_TONGUE_RUN_BD5A);
      if (!zero(s)) {
        taken(s);
        return AT(0xbd7b);
      }
      PORT_COVER(dr_tongue_probe_told);
      ldx(s, field(s, DP_PROBE_X));
      ldy(s, field(s, DP_PROBE_Y));
      ran(s, DR_TONGUE_RUN_BD74);
      return jsr(s, 0xbd78, 0xbedd);
    case AT(0xbd7b):
      ldx(s, field(s, DP_PROBE_X));
      ldy(s, field(s, DP_PROBE_Y));
      ran(s, DR_TONGUE_RUN_BD7B);
      push16(s->w, c, 0xbd81);  // `JSR $BDD0`, the wide test
      wide(s);
      pull16(s->w, c);          // ...and its `RTS`
      ran(s, DR_TONGUE_RUN_BD82);
      if (carry(s)) {
        taken(s);
        return AT(0xbdc7);
      }
      PORT_COVER(dr_tongue_probe_open);
      ldx(s, field(s, DP_PROBE_X));
      ldy(s, field(s, DP_PROBE_Y));
      ran(s, DR_TONGUE_RUN_BD84);
      return stop(AT(0xbd88));
    case AT(0xbd8c):
      ran(s, DR_TONGUE_RUN_BD8C);
      if (carry(s)) {
        taken(s);
        return AT(0xbdc7);
      }
      PORT_COVER(dr_tongue_probe_on_map);
      lda(s, field(s, DP_COUNT));
      sub(s, 0x0004);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_BD8E);
      if (!minus(s)) {
        taken(s);
        return AT(0xbd5a);
      }
      PORT_COVER(dr_tongue_moves);
      lda(s, field(s, DP_TO_X));
      set_field(s, DP_X, c->a);
      lda(s, field(s, DP_TO_Y));
      set_field(s, DP_Y, c->a);
      lda(s, 0x000a);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_BD98);
      return AT(0xbda5);
    case AT(0xbda5):
      ldx(s, field(s, DP_COUNT));
      ldy(s, field(s, (uint16_t)(DP_RECORD + c->x)));
      lda(s, c->x);
      asl_a(s);
      ldx(s, c->a);
      lda(s, field(s, DP_TO_X));
      add(s, data(s, (uint16_t)(T_PART_AT + c->x)));
      set_data(s, (uint16_t)(c->y + REC_X), c->a);
      lda(s, field(s, DP_TO_Y));
      add(s, data(s, (uint16_t)(T_PART_AT_Y + c->x)));
      set_data(s, (uint16_t)(c->y + REC_Y), c->a);
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      dec_a(s);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_BDA5);
      if (!minus(s)) {
        taken(s);
        return AT(0xbda5);
      }
      set_c(c, false);
      ran(s, DR_TONGUE_RUN_BDC6);
      return AT(0xbdc7);
    case AT(0xbdc7):
      ran(s, DR_TONGUE_RUN_BDC7);
      return rts(s);

    // `$83:BEDD`: what is in a box of 36 about (X, Y) told about it
    // (`actor_notify_box`).
    case AT(0xbedd):
      lda(s, c->x);
      sub(s, 0x0012);
      set_data(s, W_BOX, c->a);
      add(s, 0x0024);
      set_data(s, W_BOX_RIGHT, c->a);
      lda(s, c->y);
      sub(s, 0x0012);
      set_data(s, W_BOX_TOP, c->a);
      add(s, 0x0024);
      set_data(s, W_BOX_BOTTOM, c->a);
      lda(s, 0x0003);
      set_data(s, W_BOX_KIND, c->a);
      ran(s, DR_TONGUE_RUN_BEDD);
      return stop(AT(0xbf01));
    case AT(0xbf05):
      ran(s, DR_TONGUE_RUN_BF05);
      return rts(s);

    // `$83:BF06`: its picture for the way it faces and the life it is on.
    case AT(0xbf06):
      ldy(s, field(s, DP_FACING));
      ran(s, DR_TONGUE_RUN_BF06);
      if (!zero(s)) {
        taken(s);
        return AT(0xbf0f);
      }
      PORT_COVER(dr_tongue_facing_none);
      ldy(s, 0x000a);
      set_field(s, DP_FACING, c->y);
      ran(s, DR_TONGUE_RUN_BF0A);
      return AT(0xbf0f);
    case AT(0xbf0f):
      lda(s, data(s, (uint16_t)(T_FACING_PICTURE + c->y)));
      add(s, field(s, DP_LIFE));
      set_field(s, DP_PICTURE, c->a);
      cpy(s, 0x000c);
      ran(s, DR_TONGUE_RUN_BF0F);
      if (carry(s)) {
        taken(s);
        return AT(0xbf20);
      }
      PORT_COVER(dr_tongue_facing_plain);
      set_field(s, DP_MIRRORED, 0);
      ran(s, DR_TONGUE_RUN_BF1C);
      taken(s);
      return AT(0xbf25);
    case AT(0xbf20):
      PORT_COVER(dr_tongue_facing_mirrored);
      lda(s, 0x8000);
      set_field(s, DP_MIRRORED, c->a);
      ran(s, DR_TONGUE_RUN_BF20);
      return AT(0xbf25);
    case AT(0xbf25):
      ran(s, DR_TONGUE_RUN_BF25);
      return rts(s);

    // `$83:BF38`: broken, on its first three lives: another.
    case AT(0xbf38):
      lda(s, field(s, DP_BROKEN));
      ran(s, DR_TONGUE_RUN_BF38);
      if (zero(s)) {
        taken(s);
        return AT(0xbfae);
      }
      PORT_COVER(dr_tongue_broken);
      ldy(s, field(s, DP_LIFE));
      ldy(s, (uint16_t)(c->y + 1));
      cpy(s, 0x0004);
      ran(s, DR_TONGUE_RUN_BF3C);
      if (carry(s)) {
        taken(s);
        return AT(0xbfae);
      }
      PORT_COVER(dr_tongue_life_gone);
      set_field(s, DP_LIFE, c->y);
      set_field(s, DP_BROKEN, 0);
      lda(s, 0x0023);
      set_field(s, DP_HEALTH, c->a);
      lda(s, 0x0000);
      ldy(s, c->a);
      ran(s, DR_TONGUE_RUN_BF44);
      return stop(AT(0xbf51));
    case AT(0xbf55):
      lda(s, data(s, W_SOUNDS));
      cmp(s, 0x0003);
      ran(s, DR_TONGUE_RUN_BF55);
      if (!zero(s)) {
        taken(s);
        return AT(0xbf64);
      }
      PORT_COVER(dr_tongue_life_cry);
      lda(s, 0x0027);
      ran(s, DR_TONGUE_RUN_BF5D);
      return stop(AT(0xbf60));
    case AT(0xbf64):
      lda(s, 0x0008);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_BF64);
      return AT(0xbf69);
    case AT(0xbf69):
      ldy(s, field(s, DP_COUNT));
      ran(s, DR_TONGUE_RUN_BF69);
      draw(s, 0xbf6b);
      and_(s, 0x000f);
      add(s, data(s, (uint16_t)(T_PUFFS_AT + c->y)));
      adc(s, field(s, DP_X));
      set_field(s, DP_AT_X, c->a);
      ran(s, DR_TONGUE_RUN_BF6F);
      draw(s, 0xbf7a);
      and_(s, 0x000f);
      add(s, data(s, (uint16_t)(T_PUFFS_AT_Y + c->y)));
      adc(s, field(s, DP_Y));
      set_field(s, DP_AT_Y, c->a);
      lda(s, 0xb1dd);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_BF7E);
      return stop(AT(0xbf8f));
    case AT(0xbf93):
      lda(s, 0x0004);
      ran(s, DR_TONGUE_RUN_BF93);
      return stop(AT(0xbf96));
    case AT(0xbf9a):
      lda(s, field(s, DP_COUNT));
      sub(s, 0x0004);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_BF9A);
      if (!minus(s)) {
        taken(s);
        return AT(0xbf69);
      }
      lda(s, 0xc994);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_BFA4);
      return stop(AT(0xbfaa));
    case AT(0xbfae):
      ran(s, DR_TONGUE_RUN_BFAE);
      return rts(s);

    // `$83:BFBB`: how many players there are, in Y, and how many are above
    // it, in A...
    case AT(0xbfbb):
      set_field(s, DP_COUNT, 0);
      set_field(s, DP_TEMP, 0);
      ldx(s, 0x0002);
      ran(s, DR_TONGUE_RUN_BFBB);
      return AT(0xbfc2);
    case AT(0xbfc2):
      ldy(s, data(s, (uint16_t)(W_PLAYER_RECORDS + c->x)));
      ran(s, DR_TONGUE_RUN_BFC2);
      if (zero(s)) {
        taken(s);
        return AT(0xbfd2);
      }
      PORT_COVER(dr_tongue_player_counted);
      lda(s, data(s, (uint16_t)(c->y + REC_Y)));
      cmp(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_BFC7);
      if (carry(s)) {
        taken(s);
        return AT(0xbfd0);
      }
      PORT_COVER(dr_tongue_player_above);
      inc_field(s, DP_TEMP);
      ran(s, DR_TONGUE_RUN_BFCE);
      return AT(0xbfd0);
    case AT(0xbfd0):
      inc_field(s, DP_COUNT);
      ran(s, DR_TONGUE_RUN_BFD0);
      return AT(0xbfd2);
    case AT(0xbfd2):
      ldx(s, (uint16_t)(c->x - 1));
      ldx(s, (uint16_t)(c->x - 1));
      ran(s, DR_TONGUE_RUN_BFD2);
      if (!minus(s)) {
        taken(s);
        return AT(0xbfc2);
      }
      lda(s, field(s, DP_TEMP));
      ldy(s, field(s, DP_COUNT));
      ran(s, DR_TONGUE_RUN_BFD6);
      return rts(s);

    // `$83:BFDB`: ...or to its left.
    case AT(0xbfdb):
      set_field(s, DP_COUNT, 0);
      set_field(s, DP_TEMP, 0);
      ldx(s, 0x0002);
      ran(s, DR_TONGUE_RUN_BFDB);
      return AT(0xbfe2);
    case AT(0xbfe2):
      ldy(s, data(s, (uint16_t)(W_PLAYER_RECORDS + c->x)));
      ran(s, DR_TONGUE_RUN_BFE2);
      if (zero(s)) {
        taken(s);
        return AT(0xbff2);
      }
      lda(s, data(s, (uint16_t)(c->y + REC_X)));
      cmp(s, field(s, DP_X));
      ran(s, DR_TONGUE_RUN_BFE7);
      if (carry(s)) {
        taken(s);
        return AT(0xbff0);
      }
      PORT_COVER(dr_tongue_player_left);
      inc_field(s, DP_TEMP);
      ran(s, DR_TONGUE_RUN_BFEE);
      return AT(0xbff0);
    case AT(0xbff0):
      inc_field(s, DP_COUNT);
      ran(s, DR_TONGUE_RUN_BFF0);
      return AT(0xbff2);
    case AT(0xbff2):
      ldx(s, (uint16_t)(c->x - 1));
      ldx(s, (uint16_t)(c->x - 1));
      ran(s, DR_TONGUE_RUN_BFF2);
      if (!minus(s)) {
        taken(s);
        return AT(0xbfe2);
      }
      lda(s, field(s, DP_TEMP));
      ldy(s, field(s, DP_COUNT));
      ran(s, DR_TONGUE_RUN_BFF6);
      return rts(s);

    // `$83:BFFB`: carry set: five or more damage since it last charged.
    case AT(0xbffb):
      lda(s, field(s, DP_STUNG));
      cmp(s, 0x0005);
      ran(s, DR_TONGUE_RUN_BFFB);
      return rts(s);

    // `$83:C001`: its six records' collision id, A.
    case AT(0xc001):
      set_field(s, DP_TEMP, c->a);
      ldx(s, 0x000a);
      set_field(s, DP_COUNT, c->x);
      ran(s, DR_TONGUE_RUN_C001);
      return AT(0xc008);
    case AT(0xc008):
      ldx(s, field(s, DP_COUNT));
      ldy(s, field(s, (uint16_t)(DP_RECORD + c->x)));
      lda(s, field(s, DP_TEMP));
      set_data(s, (uint16_t)(c->y + REC_COLLIDE_ID), c->a);
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      dec_a(s);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_C008);
      if (!minus(s)) {
        taken(s);
        return AT(0xc008);
      }
      ran(s, DR_TONGUE_RUN_C019);
      return rts(s);

    // --- It sweeps from side to side above them ------------------------------
    case AT(0xc01a):
      PORT_COVER(dr_tongue_sweeps);
      lda(s, 0x000a);
      set_field(s, DP_FACING, c->a);
      set_field(s, DP_MIRRORED, 0);
      lda(s, 0xc030);
      set_field(s, DP_STATE, c->a);
      set_field(s, DP_LEFT, 0);
      set_field(s, DP_STEP_Y, 0);
      ldx(s, 0x0001);
      set_field(s, DP_STEP_X, c->x);
      ran(s, DR_TONGUE_RUN_C01A);
      return rts(s);

    // `$83:C030`, a state.
    case AT(0xc030):
      ran(s, DR_TONGUE_RUN_C030);
      return jsr(s, 0xc030, 0xbfbb);
    case AT(0xc033):
      cmp(s, 0x0001);
      ran(s, DR_TONGUE_RUN_C033);
      if (!carry(s)) {
        taken(s);
        return AT(0xc03b);
      }
      PORT_COVER(dr_tongue_sweep_player_above);
      ran(s, DR_TONGUE_RUN_C038);
      return AT(0xc0b6);
    case AT(0xc03b):
      ran(s, DR_TONGUE_RUN_C03B);
      return jsr(s, 0xc03b, 0xbffb);
    case AT(0xc03e):
      ran(s, DR_TONGUE_RUN_C03E);
      if (!carry(s)) {
        taken(s);
        return AT(0xc043);
      }
      PORT_COVER(dr_tongue_sweep_stung);
      ran(s, DR_TONGUE_RUN_C040);
      return AT(0xc0b3);
    case AT(0xc043):
      lda(s, 0xffff);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C043);
      return jsr(s, 0xc04a, 0xbce8);
    case AT(0xc04d):
      cpx(s, 0x0000);
      ran(s, DR_TONGUE_RUN_C04D);
      if (zero(s)) {
        taken(s);
        return AT(0xc057);
      }
      PORT_COVER(dr_tongue_sweep_target);
      set_field(s, DP_TARGET, c->x);
      asl_a(s);
      set_field(s, DP_WAY, c->a);
      ran(s, DR_TONGUE_RUN_C052);
      return AT(0xc057);
    case AT(0xc057):
      ldx(s, field(s, DP_TARGET));
      lda(s, field(s, DP_WAY));
      ran(s, DR_TONGUE_RUN_C057);
      return jsr(s, 0xc05b, 0xc0b9);
    case AT(0xc05e):
      ran(s, DR_TONGUE_RUN_C05E);
      if (!carry(s)) {
        taken(s);
        return AT(0xc063);
      }
      PORT_COVER(dr_tongue_sweep_close);
      ran(s, DR_TONGUE_RUN_C060);
      return AT(0xc0ce);
    case AT(0xc063):
      lda(s, data(s, (uint16_t)(c->x + REC_Y)));
      sub(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C063);
      if (!minus(s)) {
        taken(s);
        return AT(0xc06e);
      }
      PORT_COVER(dr_tongue_sweep_target_above);
      ran(s, DR_TONGUE_RUN_C06B);
      return AT(0xc0b6);
    case AT(0xc06e):
      set_field(s, DP_TEMP, c->a);
      ran(s, DR_TONGUE_RUN_C06E);
      return AT(0xc070);

    // Its step: across, and then to keep 85 to 105 above the player.
    case AT(0xc070):
      lda(s, field(s, DP_STEP_X));
      ran(s, DR_TONGUE_RUN_C070);
      if (minus(s)) {
        taken(s);
        return AT(0xc079);
      }
      add(s, field(s, DP_LIFE));
      ran(s, DR_TONGUE_RUN_C074);
      taken(s);
      return AT(0xc07c);
    case AT(0xc079):
      PORT_COVER(dr_tongue_sweep_left);
      sub(s, field(s, DP_LIFE));
      ran(s, DR_TONGUE_RUN_C079);
      return AT(0xc07c);
    case AT(0xc07c):
      add(s, field(s, DP_X));
      set_field(s, DP_TO_X, c->a);
      lda(s, field(s, DP_Y));
      set_field(s, DP_TO_Y, c->a);
      ran(s, DR_TONGUE_RUN_C07C);
      return jsr(s, 0xc085, 0xbd55);
    case AT(0xc088):
      ran(s, DR_TONGUE_RUN_C088);
      if (carry(s)) {
        taken(s);
        return AT(0xc0aa);
      }
      lda(s, field(s, DP_TEMP));
      cmp(s, 0x0014);
      ran(s, DR_TONGUE_RUN_C08A);
      if (!carry(s)) {
        taken(s);
        return AT(0xc0b3);
      }
      PORT_COVER(dr_tongue_sweep_far);
      cmp(s, 0x0055);
      ran(s, DR_TONGUE_RUN_C091);
      if (!carry(s)) {
        taken(s);
        return AT(0xc09d);
      }
      cmp(s, 0x0069);
      ran(s, DR_TONGUE_RUN_C096);
      if (carry(s)) {
        taken(s);
        return AT(0xc0a1);
      }
      PORT_COVER(dr_tongue_sweep_level);
      ran(s, DR_TONGUE_RUN_C09B);
      taken(s);
      return AT(0xc0a5);
    case AT(0xc09d):
      PORT_COVER(dr_tongue_sweep_up);
      dec_field(s, DP_TO_Y);
      ran(s, DR_TONGUE_RUN_C09D);
      taken(s);
      return AT(0xc0a5);
    case AT(0xc0a1):
      PORT_COVER(dr_tongue_sweep_down);
      inc_field(s, DP_TO_Y);
      ran(s, DR_TONGUE_RUN_C0A1);
      taken(s);
      return AT(0xc0a5);
    case AT(0xc0a5):
      ran(s, DR_TONGUE_RUN_C0A5);
      return jsr(s, 0xc0a5, 0xbd55);
    case AT(0xc0a8):
      ran(s, DR_TONGUE_RUN_C0A8);
      taken(s);
      return AT(0xc0b2);
    case AT(0xc0aa):
      PORT_COVER(dr_tongue_sweep_turns);
      lda(s, field(s, DP_STEP_X));
      eor(s, 0xffff);
      inc_a(s);
      set_field(s, DP_STEP_X, c->a);
      ran(s, DR_TONGUE_RUN_C0AA);
      return AT(0xc0b2);
    case AT(0xc0b2):
      ran(s, DR_TONGUE_RUN_C0B2);
      return rts(s);
    case AT(0xc0b3):
      PORT_COVER(dr_tongue_to_charge);
      ran(s, DR_TONGUE_RUN_C0B3);
      return AT(0xc4e5);
    case AT(0xc0b6):
      PORT_COVER(dr_tongue_to_side);
      ran(s, DR_TONGUE_RUN_C0B6);
      return AT(0xc22f);

    // `$83:C0B9`: carry set: the player X is within 12 across.
    case AT(0xc0b9):
      lda(s, data(s, (uint16_t)(c->x + REC_X)));
      sub(s, field(s, DP_X));
      ran(s, DR_TONGUE_RUN_C0B9);
      if (!minus(s)) {
        taken(s);
        return AT(0xc0c5);
      }
      eor(s, 0xffff);
      inc_a(s);
      ran(s, DR_TONGUE_RUN_C0C1);
      return AT(0xc0c5);
    case AT(0xc0c5):
      cmp(s, 0x000c);
      ran(s, DR_TONGUE_RUN_C0C5);
      if (carry(s)) {
        taken(s);
        return AT(0xc0cc);
      }
      set_c(c, true);
      ran(s, DR_TONGUE_RUN_C0CA);
      return rts(s);
    case AT(0xc0cc):
      set_c(c, false);
      ran(s, DR_TONGUE_RUN_C0CC);
      return rts(s);
    case AT(0xc0ce):
      lda(s, 0xc0d4);
      set_field(s, DP_STATE, c->a);
      ran(s, DR_TONGUE_RUN_C0CE);
      return rts(s);

    // --- It licks down -------------------------------------------------------
    case AT(0xc0d4):
      lda(s, 0xffff);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C0D4);
      return jsr(s, 0xc0db, 0xbce8);
    case AT(0xc0de):
      cpx(s, 0x0000);
      ran(s, DR_TONGUE_RUN_C0DE);
      if (zero(s)) {
        taken(s);
        return AT(0xc0e8);
      }
      set_field(s, DP_TARGET, c->x);
      asl_a(s);
      set_field(s, DP_WAY, c->a);
      ran(s, DR_TONGUE_RUN_C0E3);
      return AT(0xc0e8);
    case AT(0xc0e8):
      ldx(s, field(s, DP_TARGET));
      lda(s, field(s, DP_WAY));
      asl_a(s);
      ldy(s, c->a);
      lda(s, data(s, (uint16_t)(T_WAY_STEP + c->y)));
      asl_a(s);
      set_field(s, DP_STEP_X, c->a);
      lda(s, data(s, (uint16_t)(c->x + REC_Y)));
      sub(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C0E8);
      if (!minus(s)) {
        taken(s);
        return AT(0xc0fe);
      }
      PORT_COVER(dr_tongue_lick_above);
      ran(s, DR_TONGUE_RUN_C0FC);
      taken(s);
      return AT(0xc0b3);
    case AT(0xc0fe):
      set_field(s, DP_TEMP, c->a);
      ran(s, DR_TONGUE_RUN_C0FE);
      draw(s, 0xc100);
      cmp(s, 0x000f);
      ran(s, DR_TONGUE_RUN_C104);
      if (!carry(s)) {
        taken(s);
        return AT(0xc10c);
      }
      PORT_COVER(dr_tongue_lick_waits);
      ran(s, DR_TONGUE_RUN_C109);
      return AT(0xc070);
    case AT(0xc10c):
      lda(s, field(s, DP_TONGUES));
      ran(s, DR_TONGUE_RUN_C10C);
      if (zero(s)) {
        taken(s);
        return AT(0xc188);
      }
      lda(s, data(s, (uint16_t)(c->x + REC_X)));
      sub(s, field(s, DP_X));
      ran(s, DR_TONGUE_RUN_C110);
      if (!minus(s)) {
        taken(s);
        return AT(0xc11c);
      }
      eor(s, 0xffff);
      inc_a(s);
      ran(s, DR_TONGUE_RUN_C118);
      return AT(0xc11c);
    case AT(0xc11c):
      cmp(s, 0x0028);
      ran(s, DR_TONGUE_RUN_C11C);
      if (!carry(s)) {
        taken(s);
        return AT(0xc124);
      }
      PORT_COVER(dr_tongue_lick_far);
      ran(s, DR_TONGUE_RUN_C121);
      return AT(0xc070);
    case AT(0xc124):
      lda(s, field(s, DP_LIFE));
      cmp(s, 0x0002);
      ran(s, DR_TONGUE_RUN_C124);
      if (carry(s)) {
        taken(s);
        return AT(0xc14c);
      }
      PORT_COVER(dr_tongue_lick_tongue);
      lda(s, field(s, DP_TEMP));
      lsr_a(s);
      lsr_a(s);
      set_field(s, DP_AT_X, c->a);
      lda(s, 0x0005);
      set_field(s, DP_AT_Y, c->a);
      ran(s, DR_TONGUE_RUN_C12B);
      return jsr(s, 0xc136, 0xbc0a);
    case AT(0xc139):
      lda(s, 0x000a);
      add(s, field(s, DP_LIFE));
      set_field(s, DP_SHOWN, c->a);
      ran(s, DR_TONGUE_RUN_C139);
      return jsr(s, 0xc141, 0xba17);
    case AT(0xc144):
      lda(s, 0x0014);
      set_field(s, DP_PICTURE_TICKS, c->a);
      ran(s, DR_TONGUE_RUN_C144);
      return AT(0xc070);
    case AT(0xc14c):
      PORT_COVER(dr_tongue_lick_spits);
      dec_field(s, DP_SPIT_LEFT);
      ran(s, DR_TONGUE_RUN_C14C);
      if (!minus(s)) {
        taken(s);
        return AT(0xc185);
      }
      PORT_COVER(dr_tongue_spit_pair);
      lda(s, 0x0004);
      set_field(s, DP_SPIT_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C150);
      return AT(0xc155);
    case AT(0xc155):
      ldx(s, field(s, DP_SPIT_LEFT));
      lda(s, field(s, DP_X));
      add(s, data(s, (uint16_t)(T_SPIT_AT + c->x)));
      set_field(s, DP_AT_X, c->a);
      lda(s, field(s, DP_Y));
      add(s, data(s, (uint16_t)(T_SPIT_AT_Y + c->x)));
      set_field(s, DP_AT_Y, c->a);
      lda(s, 0x000a);
      set_field(s, DP_ARG, c->a);
      lda(s, 0xca4a);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C155);
      return stop(AT(0xc172));
    case AT(0xc176):
      lda(s, field(s, DP_SPIT_LEFT));
      sub(s, 0x0004);
      set_field(s, DP_SPIT_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C176);
      if (!minus(s)) {
        taken(s);
        return AT(0xc155);
      }
      lda(s, 0x000a);
      set_field(s, DP_SPIT_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C180);
      return AT(0xc185);
    case AT(0xc185):
      ran(s, DR_TONGUE_RUN_C185);
      return AT(0xc070);
    case AT(0xc188):
      PORT_COVER(dr_tongue_lick_begins);
      lda(s, 0xc199);
      set_field(s, DP_STATE, c->a);
      lda(s, field(s, DP_STEP_X));
      asl_a(s);
      asl_a(s);
      set_field(s, DP_STEP_X, c->a);
      lda(s, 0x0014);
      set_field(s, DP_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C188);
      return rts(s);

    // `$83:C199`, a state: twenty frames of licking.
    case AT(0xc199):
      lda(s, field(s, DP_LEFT));
      dec_a(s);
      ran(s, DR_TONGUE_RUN_C199);
      if (!minus(s)) {
        taken(s);
        return AT(0xc1a1);
      }
      PORT_COVER(dr_tongue_licking_done);
      ran(s, DR_TONGUE_RUN_C19E);
      return AT(0xc22c);
    case AT(0xc1a1):
      set_field(s, DP_LEFT, c->a);
      and_(s, 0x0003);
      ran(s, DR_TONGUE_RUN_C1A1);
      if (!zero(s)) {
        taken(s);
        return AT(0xc1ee);
      }
      PORT_COVER(dr_tongue_licking_aim);
      lda(s, 0xffff);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C1A8);
      return jsr(s, 0xc1af, 0xbce8);
    case AT(0xc1b2):
      cpx(s, 0x0000);
      ran(s, DR_TONGUE_RUN_C1B2);
      if (zero(s)) {
        taken(s);
        return AT(0xc1bc);
      }
      set_field(s, DP_TARGET, c->x);
      asl_a(s);
      set_field(s, DP_WAY, c->a);
      ran(s, DR_TONGUE_RUN_C1B7);
      return AT(0xc1bc);
    case AT(0xc1bc):
      ldx(s, field(s, DP_TARGET));
      lda(s, data(s, (uint16_t)(c->x + REC_Y)));
      sub(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C1BC);
      if (!minus(s)) {
        taken(s);
        return AT(0xc1c9);
      }
      PORT_COVER(dr_tongue_licking_above);
      ran(s, DR_TONGUE_RUN_C1C6);
      return AT(0xc0b3);
    case AT(0xc1c9):
      lsr_a(s);
      lsr_a(s);
      set_field(s, DP_AT_X, c->a);
      lda(s, 0x0005);
      set_field(s, DP_AT_Y, c->a);
      lda(s, field(s, DP_LIFE));
      cmp(s, 0x0002);
      ran(s, DR_TONGUE_RUN_C1C9);
      if (carry(s)) {
        taken(s);
        return AT(0xc1f1);
      }
      PORT_COVER(dr_tongue_licking_tongue);
      ran(s, DR_TONGUE_RUN_C1D9);
      return jsr(s, 0xc1d9, 0xbc0a);
    case AT(0xc1dc):
      lda(s, 0x000a);
      add(s, field(s, DP_LIFE));
      set_field(s, DP_SHOWN, c->a);
      ran(s, DR_TONGUE_RUN_C1DC);
      return jsr(s, 0xc1e4, 0xba17);
    case AT(0xc1e7):
      set_field(s, DP_SHOWN, 0);
      lda(s, 0x0014);
      set_field(s, DP_PICTURE_TICKS, c->a);
      ran(s, DR_TONGUE_RUN_C1E7);
      return AT(0xc1ee);
    case AT(0xc1ee):
      ran(s, DR_TONGUE_RUN_C1EE);
      return AT(0xc070);
    case AT(0xc1f1):
      PORT_COVER(dr_tongue_licking_spits);
      dec_field(s, DP_SPIT_LEFT);
      ran(s, DR_TONGUE_RUN_C1F1);
      if (!minus(s)) {
        taken(s);
        return AT(0xc22a);
      }
      PORT_COVER(dr_tongue_licking_spit_pair);
      lda(s, 0x0004);
      set_field(s, DP_SPIT_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C1F5);
      return AT(0xc1fa);
    case AT(0xc1fa):
      ldx(s, field(s, DP_SPIT_LEFT));
      lda(s, field(s, DP_X));
      add(s, data(s, (uint16_t)(T_SPIT_AT + c->x)));
      set_field(s, DP_AT_X, c->a);
      lda(s, field(s, DP_Y));
      add(s, data(s, (uint16_t)(T_SPIT_AT_Y + c->x)));
      set_field(s, DP_AT_Y, c->a);
      lda(s, 0x000a);
      set_field(s, DP_ARG, c->a);
      lda(s, 0xca4a);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C1FA);
      return stop(AT(0xc217));
    case AT(0xc21b):
      lda(s, field(s, DP_SPIT_LEFT));
      sub(s, 0x0004);
      set_field(s, DP_SPIT_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C21B);
      if (!minus(s)) {
        taken(s);
        return AT(0xc1fa);
      }
      lda(s, 0x000a);
      set_field(s, DP_SPIT_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C225);
      return AT(0xc22a);
    case AT(0xc22a):
      ran(s, DR_TONGUE_RUN_C22A);
      taken(s);
      return AT(0xc1ee);
    case AT(0xc22c):
      PORT_COVER(dr_tongue_back_to_sweep);
      ran(s, DR_TONGUE_RUN_C22C);
      return AT(0xc01a);

    // --- It goes up and down at a side ---------------------------------------
    case AT(0xc22f):
      PORT_COVER(dr_tongue_sides);
      lda(s, 0xc256);
      set_field(s, DP_STATE, c->a);
      lda(s, 0xffff);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C22F);
      return jsr(s, 0xc23b, 0xbce8);
    case AT(0xc23e):
      cpx(s, 0x0000);
      ran(s, DR_TONGUE_RUN_C23E);
      if (zero(s)) {
        taken(s);
        return AT(0xc24a);
      }
      set_field(s, DP_TARGET, c->x);
      asl_a(s);
      set_field(s, DP_WAY, c->a);
      set_field(s, DP_FACING, c->a);
      ran(s, DR_TONGUE_RUN_C243);
      return AT(0xc24a);
    case AT(0xc24a):
      ldx(s, field(s, DP_TARGET));
      lda(s, field(s, DP_WAY));
      set_field(s, DP_STEP_X, 0);
      ldx(s, 0x0001);
      set_field(s, DP_STEP_Y, c->x);
      ran(s, DR_TONGUE_RUN_C24A);
      return rts(s);

    // `$83:C256`, a state.
    case AT(0xc256):
      ran(s, DR_TONGUE_RUN_C256);
      return jsr(s, 0xc256, 0xbfdb);
    case AT(0xc259):
      cpy(s, 0x0002);
      ran(s, DR_TONGUE_RUN_C259);
      if (!carry(s)) {
        taken(s);
        return AT(0xc266);
      }
      PORT_COVER(dr_tongue_side_two_players);
      cmp(s, 0x0001);
      ran(s, DR_TONGUE_RUN_C25E);
      if (!carry(s)) {
        taken(s);
        return AT(0xc266);
      }
      PORT_COVER(dr_tongue_side_split);
      ran(s, DR_TONGUE_RUN_C263);
      return AT(0xc2f6);
    case AT(0xc266):
      ran(s, DR_TONGUE_RUN_C266);
      return jsr(s, 0xc266, 0xbffb);
    case AT(0xc269):
      ran(s, DR_TONGUE_RUN_C269);
      if (!carry(s)) {
        taken(s);
        return AT(0xc26e);
      }
      PORT_COVER(dr_tongue_side_stung);
      ran(s, DR_TONGUE_RUN_C26B);
      return AT(0xc2f6);
    case AT(0xc26e):
      lda(s, 0xffff);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C26E);
      return jsr(s, 0xc275, 0xbce8);
    case AT(0xc278):
      cpx(s, 0x0000);
      ran(s, DR_TONGUE_RUN_C278);
      if (zero(s)) {
        taken(s);
        return AT(0xc282);
      }
      PORT_COVER(dr_tongue_side_target);
      set_field(s, DP_TARGET, c->x);
      asl_a(s);
      set_field(s, DP_WAY, c->a);
      ran(s, DR_TONGUE_RUN_C27D);
      return AT(0xc282);
    case AT(0xc282):
      ldx(s, field(s, DP_TARGET));
      lda(s, field(s, DP_WAY));
      ran(s, DR_TONGUE_RUN_C282);
      return jsr(s, 0xc286, 0xc2fc);
    case AT(0xc289):
      ran(s, DR_TONGUE_RUN_C289);
      if (!carry(s)) {
        taken(s);
        return AT(0xc28e);
      }
      PORT_COVER(dr_tongue_side_level);
      ran(s, DR_TONGUE_RUN_C28B);
      return AT(0xc315);
    case AT(0xc28e):
      set_field(s, DP_MIRRORED, 0);
      lda(s, data(s, (uint16_t)(c->x + REC_X)));
      sub(s, field(s, DP_X));
      ran(s, DR_TONGUE_RUN_C28E);
      if (!minus(s)) {
        taken(s);
        return AT(0xc2a1);
      }
      PORT_COVER(dr_tongue_side_player_left);
      eor(s, 0xffff);
      inc_a(s);
      ldy(s, 0x8000);
      set_field(s, DP_MIRRORED, c->y);
      ran(s, DR_TONGUE_RUN_C298);
      return AT(0xc2a1);
    case AT(0xc2a1):
      set_field(s, DP_TEMP, c->a);
      ran(s, DR_TONGUE_RUN_C2A1);
      return AT(0xc2a3);

    // Its step: up or down, and then to keep 85 to 105 off the player.
    case AT(0xc2a3):
      lda(s, field(s, DP_STEP_Y));
      ran(s, DR_TONGUE_RUN_C2A3);
      if (minus(s)) {
        taken(s);
        return AT(0xc2ac);
      }
      add(s, field(s, DP_LIFE));
      ran(s, DR_TONGUE_RUN_C2A7);
      taken(s);
      return AT(0xc2af);
    case AT(0xc2ac):
      PORT_COVER(dr_tongue_side_rises);
      sub(s, field(s, DP_LIFE));
      ran(s, DR_TONGUE_RUN_C2AC);
      return AT(0xc2af);
    case AT(0xc2af):
      add(s, field(s, DP_Y));
      set_field(s, DP_TO_Y, c->a);
      lda(s, field(s, DP_X));
      set_field(s, DP_TO_X, c->a);
      ran(s, DR_TONGUE_RUN_C2AF);
      return jsr(s, 0xc2b8, 0xbd55);
    case AT(0xc2bb):
      ran(s, DR_TONGUE_RUN_C2BB);
      if (carry(s)) {
        taken(s);
        return AT(0xc2ed);
      }
      lda(s, field(s, DP_TEMP));
      cmp(s, 0x0014);
      ran(s, DR_TONGUE_RUN_C2BD);
      if (!carry(s)) {
        taken(s);
        return AT(0xc2f6);
      }
      PORT_COVER(dr_tongue_side_far);
      cmp(s, 0x0055);
      ran(s, DR_TONGUE_RUN_C2C4);
      if (!carry(s)) {
        taken(s);
        return AT(0xc2d0);
      }
      cmp(s, 0x0069);
      ran(s, DR_TONGUE_RUN_C2C9);
      if (carry(s)) {
        taken(s);
        return AT(0xc2dc);
      }
      PORT_COVER(dr_tongue_side_between);
      ran(s, DR_TONGUE_RUN_C2CE);
      taken(s);
      return AT(0xc2e8);
    case AT(0xc2d0):
      PORT_COVER(dr_tongue_side_away);
      lda(s, field(s, DP_MIRRORED));
      ran(s, DR_TONGUE_RUN_C2D0);
      if (!minus(s)) {
        taken(s);
        return AT(0xc2d8);
      }
      inc_field(s, DP_TO_X);
      ran(s, DR_TONGUE_RUN_C2D4);
      taken(s);
      return AT(0xc2e8);
    case AT(0xc2d8):
      dec_field(s, DP_TO_X);
      ran(s, DR_TONGUE_RUN_C2D8);
      taken(s);
      return AT(0xc2e8);
    case AT(0xc2dc):
      PORT_COVER(dr_tongue_side_closer);
      lda(s, field(s, DP_MIRRORED));
      ran(s, DR_TONGUE_RUN_C2DC);
      if (!minus(s)) {
        taken(s);
        return AT(0xc2e4);
      }
      dec_field(s, DP_TO_X);
      ran(s, DR_TONGUE_RUN_C2E0);
      taken(s);
      return AT(0xc2e8);
    case AT(0xc2e4):
      inc_field(s, DP_TO_X);
      ran(s, DR_TONGUE_RUN_C2E4);
      taken(s);
      return AT(0xc2e8);
    case AT(0xc2e8):
      ran(s, DR_TONGUE_RUN_C2E8);
      return jsr(s, 0xc2e8, 0xbd55);
    case AT(0xc2eb):
      ran(s, DR_TONGUE_RUN_C2EB);
      taken(s);
      return AT(0xc2f5);
    case AT(0xc2ed):
      PORT_COVER(dr_tongue_side_turns);
      lda(s, field(s, DP_STEP_Y));
      eor(s, 0xffff);
      inc_a(s);
      set_field(s, DP_STEP_Y, c->a);
      ran(s, DR_TONGUE_RUN_C2ED);
      return AT(0xc2f5);
    case AT(0xc2f5):
      ran(s, DR_TONGUE_RUN_C2F5);
      return rts(s);
    case AT(0xc2f6):
      ran(s, DR_TONGUE_RUN_C2F6);
      return AT(0xc4e5);

    // `$83:C2FC`: carry set: the player X is within 12 of it, 18 up.
    case AT(0xc2fc):
      lda(s, data(s, (uint16_t)(c->x + REC_Y)));
      sub(s, 0x0012);
      sub(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C2FC);
      if (!minus(s)) {
        taken(s);
        return AT(0xc30c);
      }
      eor(s, 0xffff);
      inc_a(s);
      ran(s, DR_TONGUE_RUN_C308);
      return AT(0xc30c);
    case AT(0xc30c):
      cmp(s, 0x000c);
      ran(s, DR_TONGUE_RUN_C30C);
      if (carry(s)) {
        taken(s);
        return AT(0xc313);
      }
      set_c(c, true);
      ran(s, DR_TONGUE_RUN_C311);
      return rts(s);
    case AT(0xc313):
      set_c(c, false);
      ran(s, DR_TONGUE_RUN_C313);
      return rts(s);
    case AT(0xc315):
      lda(s, 0xc31b);
      set_field(s, DP_STATE, c->a);
      ran(s, DR_TONGUE_RUN_C315);
      return rts(s);

    // --- It licks across -----------------------------------------------------
    case AT(0xc31b):
      lda(s, 0xffff);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C31B);
      return jsr(s, 0xc322, 0xbce8);
    case AT(0xc325):
      cpx(s, 0x0000);
      ran(s, DR_TONGUE_RUN_C325);
      if (zero(s)) {
        taken(s);
        return AT(0xc32f);
      }
      set_field(s, DP_TARGET, c->x);
      asl_a(s);
      set_field(s, DP_WAY, c->a);
      ran(s, DR_TONGUE_RUN_C32A);
      return AT(0xc32f);
    case AT(0xc32f):
      ldx(s, field(s, DP_TARGET));
      lda(s, field(s, DP_WAY));
      asl_a(s);
      ldy(s, c->a);
      lda(s, data(s, (uint16_t)(T_WAY_STEP_Y + c->y)));
      asl_a(s);
      set_field(s, DP_STEP_Y, c->a);
      lda(s, data(s, (uint16_t)(c->x + REC_X)));
      sub(s, field(s, DP_X));
      ran(s, DR_TONGUE_RUN_C32F);
      if (!minus(s)) {
        taken(s);
        return AT(0xc347);
      }
      eor(s, 0xffff);
      inc_a(s);
      ran(s, DR_TONGUE_RUN_C343);
      return AT(0xc347);
    case AT(0xc347):
      set_field(s, DP_TEMP, c->a);
      ran(s, DR_TONGUE_RUN_C347);
      draw(s, 0xc349);
      cmp(s, 0x000f);
      ran(s, DR_TONGUE_RUN_C34D);
      if (!carry(s)) {
        taken(s);
        return AT(0xc355);
      }
      PORT_COVER(dr_tongue_side_lick_waits);
      ran(s, DR_TONGUE_RUN_C352);
      return AT(0xc2a3);
    case AT(0xc355):
      lda(s, field(s, DP_TONGUES));
      ran(s, DR_TONGUE_RUN_C355);
      if (!zero(s)) {
        taken(s);
        return AT(0xc35c);
      }
      PORT_COVER(dr_tongue_side_lick_begins);
      ran(s, DR_TONGUE_RUN_C359);
      return AT(0xc3ec);
    case AT(0xc35c):
      lda(s, data(s, (uint16_t)(c->x + REC_Y)));
      sub(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C35C);
      if (!minus(s)) {
        taken(s);
        return AT(0xc368);
      }
      eor(s, 0xffff);
      inc_a(s);
      ran(s, DR_TONGUE_RUN_C364);
      return AT(0xc368);
    case AT(0xc368):
      lsr_a(s);
      cmp(s, 0x0028);
      ran(s, DR_TONGUE_RUN_C368);
      if (!carry(s)) {
        taken(s);
        return AT(0xc371);
      }
      PORT_COVER(dr_tongue_side_lick_far);
      ran(s, DR_TONGUE_RUN_C36E);
      return AT(0xc2a3);
    case AT(0xc371):
      lda(s, field(s, DP_TEMP));
      lsr_a(s);
      lsr_a(s);
      set_field(s, DP_AT_X, c->a);
      ldx(s, 0x0007);
      lda(s, field(s, DP_MIRRORED));
      ran(s, DR_TONGUE_RUN_C371);
      if (minus(s)) {
        taken(s);
        return AT(0xc381);
      }
      PORT_COVER(dr_tongue_side_lick_right);
      ldx(s, 0x0003);
      ran(s, DR_TONGUE_RUN_C37E);
      return AT(0xc381);
    case AT(0xc381):
      set_field(s, DP_AT_Y, c->x);
      lda(s, field(s, DP_LIFE));
      cmp(s, 0x0002);
      ran(s, DR_TONGUE_RUN_C381);
      if (carry(s)) {
        taken(s);
        return AT(0xc3a2);
      }
      PORT_COVER(dr_tongue_side_lick_tongue);
      ran(s, DR_TONGUE_RUN_C38A);
      return jsr(s, 0xc38a, 0xbc0a);
    case AT(0xc38d):
      lda(s, 0x000c);
      add(s, field(s, DP_LIFE));
      set_field(s, DP_SHOWN, c->a);
      ran(s, DR_TONGUE_RUN_C38D);
      return jsr(s, 0xc395, 0xba17);
    case AT(0xc398):
      set_field(s, DP_SHOWN, 0);
      lda(s, 0x0014);
      set_field(s, DP_PICTURE_TICKS, c->a);
      ran(s, DR_TONGUE_RUN_C398);
      return AT(0xc2a3);
    case AT(0xc3a2):
      PORT_COVER(dr_tongue_side_lick_spits);
      dec_field(s, DP_SPIT_LEFT);
      ran(s, DR_TONGUE_RUN_C3A2);
      if (!minus(s)) {
        taken(s);
        return AT(0xc3e9);
      }
      PORT_COVER(dr_tongue_side_spit_pair);
      ldx(s, 0x000e);
      ldy(s, 0x0008);
      lda(s, field(s, DP_MIRRORED));
      ran(s, DR_TONGUE_RUN_C3A6);
      if (minus(s)) {
        taken(s);
        return AT(0xc3b6);
      }
      PORT_COVER(dr_tongue_side_spit_right);
      ldx(s, 0x0006);
      ldy(s, 0x000c);
      ran(s, DR_TONGUE_RUN_C3B0);
      return AT(0xc3b6);
    case AT(0xc3b6):
      set_field(s, DP_ARG, c->x);
      lda(s, field(s, DP_X));
      add(s, data(s, (uint16_t)(T_SPIT_AT + c->y)));
      set_field(s, DP_AT_X, c->a);
      lda(s, field(s, DP_Y));
      add(s, data(s, (uint16_t)(T_SPIT_AT_Y + c->y)));
      set_field(s, DP_AT_Y, c->a);
      lda(s, 0xca4a);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C3B6);
      return stop(AT(0xc3ce));
    case AT(0xc3d2):
      lda(s, field(s, DP_AT_Y));
      add(s, 0x0008);
      set_field(s, DP_AT_Y, c->a);
      lda(s, 0xca4a);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C3D2);
      return stop(AT(0xc3e0));
    case AT(0xc3e4):
      lda(s, 0x000a);
      set_field(s, DP_SPIT_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C3E4);
      return AT(0xc3e9);
    case AT(0xc3e9):
      ran(s, DR_TONGUE_RUN_C3E9);
      return AT(0xc2a3);
    case AT(0xc3ec):
      lda(s, 0xc3fd);
      set_field(s, DP_STATE, c->a);
      lda(s, field(s, DP_STEP_Y));
      asl_a(s);
      asl_a(s);
      set_field(s, DP_STEP_Y, c->a);
      lda(s, 0x0014);
      set_field(s, DP_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C3EC);
      return rts(s);

    // `$83:C3FD`, a state: twenty frames of licking.
    case AT(0xc3fd):
      lda(s, field(s, DP_LEFT));
      dec_a(s);
      ran(s, DR_TONGUE_RUN_C3FD);
      if (!minus(s)) {
        taken(s);
        return AT(0xc405);
      }
      PORT_COVER(dr_tongue_side_licking_done);
      ran(s, DR_TONGUE_RUN_C402);
      return AT(0xc4a6);
    case AT(0xc405):
      set_field(s, DP_LEFT, c->a);
      and_(s, 0x0003);
      ran(s, DR_TONGUE_RUN_C405);
      if (!zero(s)) {
        taken(s);
        return AT(0xc45a);
      }
      PORT_COVER(dr_tongue_side_licking_aim);
      lda(s, 0xffff);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C40C);
      return jsr(s, 0xc413, 0xbce8);
    case AT(0xc416):
      cpx(s, 0x0000);
      ran(s, DR_TONGUE_RUN_C416);
      if (zero(s)) {
        taken(s);
        return AT(0xc420);
      }
      set_field(s, DP_TARGET, c->x);
      asl_a(s);
      set_field(s, DP_WAY, c->a);
      ran(s, DR_TONGUE_RUN_C41B);
      return AT(0xc420);
    case AT(0xc420):
      ldx(s, field(s, DP_TARGET));
      lda(s, data(s, (uint16_t)(c->x + REC_X)));
      sub(s, field(s, DP_X));
      ran(s, DR_TONGUE_RUN_C420);
      if (!minus(s)) {
        taken(s);
        return AT(0xc42e);
      }
      eor(s, 0xffff);
      inc_a(s);
      ran(s, DR_TONGUE_RUN_C42A);
      return AT(0xc42e);
    case AT(0xc42e):
      lsr_a(s);
      lsr_a(s);
      set_field(s, DP_AT_X, c->a);
      ldx(s, 0x0007);
      lda(s, field(s, DP_MIRRORED));
      ran(s, DR_TONGUE_RUN_C42E);
      if (minus(s)) {
        taken(s);
        return AT(0xc43c);
      }
      ldx(s, 0x0003);
      ran(s, DR_TONGUE_RUN_C439);
      return AT(0xc43c);
    case AT(0xc43c):
      set_field(s, DP_AT_Y, c->x);
      lda(s, field(s, DP_LIFE));
      cmp(s, 0x0002);
      ran(s, DR_TONGUE_RUN_C43C);
      if (carry(s)) {
        taken(s);
        return AT(0xc45d);
      }
      PORT_COVER(dr_tongue_side_licking_tongue);
      ran(s, DR_TONGUE_RUN_C445);
      return jsr(s, 0xc445, 0xbc0a);
    case AT(0xc448):
      lda(s, 0x000c);
      add(s, field(s, DP_LIFE));
      set_field(s, DP_SHOWN, c->a);
      ran(s, DR_TONGUE_RUN_C448);
      return jsr(s, 0xc450, 0xba17);
    case AT(0xc453):
      set_field(s, DP_SHOWN, 0);
      lda(s, 0x0014);
      set_field(s, DP_PICTURE_TICKS, c->a);
      ran(s, DR_TONGUE_RUN_C453);
      return AT(0xc45a);
    case AT(0xc45a):
      ran(s, DR_TONGUE_RUN_C45A);
      return AT(0xc2a3);
    case AT(0xc45d):
      PORT_COVER(dr_tongue_side_licking_spits);
      dec_field(s, DP_SPIT_LEFT);
      ran(s, DR_TONGUE_RUN_C45D);
      if (!minus(s)) {
        taken(s);
        return AT(0xc4a4);
      }
      PORT_COVER(dr_tongue_side_licking_spit_pair);
      ldx(s, 0x000e);
      ldy(s, 0x0008);
      lda(s, field(s, DP_MIRRORED));
      ran(s, DR_TONGUE_RUN_C461);
      if (minus(s)) {
        taken(s);
        return AT(0xc471);
      }
      ldx(s, 0x0006);
      ldy(s, 0x000c);
      ran(s, DR_TONGUE_RUN_C46B);
      return AT(0xc471);
    case AT(0xc471):
      set_field(s, DP_ARG, c->x);
      lda(s, field(s, DP_X));
      add(s, data(s, (uint16_t)(T_SPIT_AT + c->y)));
      set_field(s, DP_AT_X, c->a);
      lda(s, field(s, DP_Y));
      add(s, data(s, (uint16_t)(T_SPIT_AT_Y + c->y)));
      set_field(s, DP_AT_Y, c->a);
      lda(s, 0xca4a);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C471);
      return stop(AT(0xc489));
    case AT(0xc48d):
      lda(s, field(s, DP_AT_Y));
      add(s, 0x0008);
      set_field(s, DP_AT_Y, c->a);
      lda(s, 0xca4a);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C48D);
      return stop(AT(0xc49b));
    case AT(0xc49f):
      lda(s, 0x000a);
      set_field(s, DP_SPIT_LEFT, c->a);
      ran(s, DR_TONGUE_RUN_C49F);
      return AT(0xc4a4);
    case AT(0xc4a4):
      ran(s, DR_TONGUE_RUN_C4A4);
      taken(s);
      return AT(0xc45a);
    case AT(0xc4a6):
      ran(s, DR_TONGUE_RUN_C4A6);
      return AT(0xc22f);

    // --- It charges ----------------------------------------------------------
    //
    // `$83:C4A9`: off a way at random, for 65 to 96 frames. Its handler
    // calls this too, when a player touches it.
    case AT(0xc4a9):
      PORT_COVER(dr_tongue_bounces);
      lda(s, 0x0035);
      ran(s, DR_TONGUE_RUN_C4A9);
      return jsr(s, 0xc4ac, 0xc001);
    case AT(0xc4af):
      ran(s, DR_TONGUE_RUN_C4AF);
      draw(s, 0xc4af);
      and_(s, 0x0003);
      set_field(s, DP_TEMP, c->a);
      lda(s, field(s, DP_WAY));
      lsr_a(s);
      dec_a(s);
      add(s, 0x0003);
      adc(s, field(s, DP_TEMP));
      and_(s, 0x0007);
      inc_a(s);
      asl_a(s);
      set_field(s, DP_FACING, c->a);
      set_field(s, DP_WAY, c->a);
      ran(s, DR_TONGUE_RUN_C4B3);
      draw(s, 0xc4cb);
      and_(s, 0x001f);
      add(s, 0x0041);
      set_field(s, DP_LEFT, c->a);
      lda(s, 0xffff);
      set_field(s, DP_TARGET, c->a);
      set_field(s, DP_STUNG, 0);
      lda(s, 0xc518);
      set_field(s, DP_STATE, c->a);
      ran(s, DR_TONGUE_RUN_C4CF);
      return rts(s);

    // `$83:C4E5`: at the nearest player, for 80 to 111 frames.
    case AT(0xc4e5):
      PORT_COVER(dr_tongue_charges);
      lda(s, 0x0035);
      ran(s, DR_TONGUE_RUN_C4E5);
      return jsr(s, 0xc4e8, 0xc001);
    case AT(0xc4eb):
      lda(s, 0xffff);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C4EB);
      return jsr(s, 0xc4f2, 0xbce8);
    case AT(0xc4f5):
      cpx(s, 0x0000);
      ran(s, DR_TONGUE_RUN_C4F5);
      if (zero(s)) {
        taken(s);
        return AT(0xc501);
      }
      set_field(s, DP_TARGET, c->x);
      asl_a(s);
      set_field(s, DP_WAY, c->a);
      set_field(s, DP_FACING, c->a);
      ran(s, DR_TONGUE_RUN_C4FA);
      return AT(0xc501);
    case AT(0xc501):
      ldx(s, field(s, DP_TARGET));
      ran(s, DR_TONGUE_RUN_C501);
      draw(s, 0xc503);
      and_(s, 0x001f);
      add(s, 0x0050);
      set_field(s, DP_LEFT, c->a);
      set_field(s, DP_STUNG, 0);
      lda(s, 0xc518);
      set_field(s, DP_STATE, c->a);
      ran(s, DR_TONGUE_RUN_C507);
      return rts(s);

    // `$83:C518`, a state: two steps a frame.
    case AT(0xc518):
      lda(s, 0x0001);
      set_field(s, DP_TEMP, c->a);
      ldy(s, field(s, DP_TARGET));
      cpy(s, 0xffff);
      ran(s, DR_TONGUE_RUN_C518);
      if (!zero(s)) {
        taken(s);
        return AT(0xc52d);
      }
      PORT_COVER(dr_tongue_charge_wanders);
      lda(s, field(s, DP_WAY));
      ran(s, DR_TONGUE_RUN_C524);
      return jsr(s, 0xc526, 0xc542);
    case AT(0xc529):
      lda(s, field(s, DP_WAY));
      ran(s, DR_TONGUE_RUN_C529);
      taken(s);
      return AT(0xc542);
    case AT(0xc52d):
      PORT_COVER(dr_tongue_charge_aims);
      ran(s, DR_TONGUE_RUN_C52D);
      return jsr(s, 0xc52d, 0xc530);
    case AT(0xc530):
      ldy(s, field(s, DP_TARGET));
      lda(s, data(s, (uint16_t)(c->y + REC_X)));
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(c->y + REC_Y)));
      ldy(s, c->a);
      ran(s, DR_TONGUE_RUN_C530);
      return jsr(s, 0xc53a, 0xbd1d);
    case AT(0xc53d):
      asl_a(s);
      set_field(s, DP_FACING, c->a);
      set_field(s, DP_WAY, c->a);
      ran(s, DR_TONGUE_RUN_C53D);
      return AT(0xc542);
    case AT(0xc542):
      asl_a(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_WAY_STEP + c->x)));
      add(s, field(s, DP_X));
      set_field(s, DP_TO_X, c->a);
      lda(s, data(s, (uint16_t)(T_WAY_STEP_Y + c->x)));
      add(s, field(s, DP_Y));
      set_field(s, DP_TO_Y, c->a);
      ran(s, DR_TONGUE_RUN_C542);
      return jsr(s, 0xc554, 0xbd55);
    case AT(0xc557):
      ran(s, DR_TONGUE_RUN_C557);
      if (carry(s)) {
        taken(s);
        return AT(0xc560);
      }
      PORT_COVER(dr_tongue_charge_steps);
      dec_field(s, DP_LEFT);
      ran(s, DR_TONGUE_RUN_C559);
      if (minus(s)) {
        taken(s);
        return AT(0xc560);
      }
      return AT(0xc55d);
    case AT(0xc55d):
      dec_field(s, DP_TEMP);
      ran(s, DR_TONGUE_RUN_C55D);
      return rts(s);
    case AT(0xc560):
      PORT_COVER(dr_tongue_charge_stops);
      lda(s, field(s, DP_TEMP));
      ran(s, DR_TONGUE_RUN_C560);
      if (!zero(s)) {
        taken(s);
        return AT(0xc55d);
      }
      lda(s, field(s, DP_TARGET));
      cmp(s, 0xffff);
      ran(s, DR_TONGUE_RUN_C564);
      if (!zero(s)) {
        taken(s);
        return AT(0xc574);
      }
      PORT_COVER(dr_tongue_charge_done);
      lda(s, 0x0003);
      ran(s, DR_TONGUE_RUN_C56B);
      return jsr(s, 0xc56e, 0xc001);
    case AT(0xc571):
      ran(s, DR_TONGUE_RUN_C571);
      return AT(0xc01a);
    case AT(0xc574):
      PORT_COVER(dr_tongue_charge_bounce);
      ran(s, DR_TONGUE_RUN_C574);
      return AT(0xc4a9);

    // --- The head ------------------------------------------------------------
    //
    // `$83:C577`: its six records.
    case AT(0xc577):
      lda(s, 0x000a);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_C577);
      return AT(0xc57c);
    case AT(0xc57c):
      return stop(AT(0xc57c));
    case AT(0xc580):
      ldx(s, field(s, DP_COUNT));
      set_field(s, (uint16_t)(DP_RECORD + c->x), c->a);
      ldx(s, c->a);
      lda(s, field(s, DP_COUNT));
      asl_a(s);
      ldy(s, c->a);
      lda(s, field(s, DP_X));
      add(s, data(s, (uint16_t)(T_PART_AT + c->y)));
      set_data(s, (uint16_t)(c->x + REC_X), c->a);
      set_data(s, (uint16_t)(c->x + REC_X_FRACTION), 0);
      lda(s, field(s, DP_Y));
      add(s, data(s, (uint16_t)(T_PART_AT_Y + c->y)));
      set_data(s, (uint16_t)(c->x + REC_Y), c->a);
      lda(s, data(s, (uint16_t)(T_PART_PICTURES + c->y)));
      set_data(s, (uint16_t)(c->x + REC_PICTURES), c->a);
      lda(s, 0x0000);
      set_data(s, (uint16_t)(c->x + REC_PICTURES_BANK), c->a);
      lda(s, 0x0003);
      set_data(s, (uint16_t)(c->x + REC_COLLIDE_ID), c->a);
      lda(s, data(s, W_FRAME));
      set_data(s, (uint16_t)(c->x + REC_BORN), c->a);
      lda(s, 0x0c00);
      set_data(s, (uint16_t)(c->x + REC_PALETTE), c->a);
      lda(s, data(s, (uint16_t)(T_PART_FLAGS + c->y)));
      ora(s, data(s, (uint16_t)(c->x + REC_FLAGS)));
      set_data(s, (uint16_t)(c->x + REC_FLAGS), c->a);
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      dec_a(s);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_C580);
      if (!minus(s)) {
        taken(s);
        return AT(0xc57c);
      }
      ran(s, DR_TONGUE_RUN_C5CD);
      return rts(s);

    // `$83:C5E6`: its colours and tiles, and where it is.
    case AT(0xc5e6):
      lda(s, 0xffd1);
      ldy(s, 0x0092);
      ran(s, DR_TONGUE_RUN_C5E6);
      return stop(AT(0xc5ec));
    case AT(0xc5f0):
      lda(s, 0xffd1);
      ldy(s, 0x0092);
      ran(s, DR_TONGUE_RUN_C5F0);
      return stop(AT(0xc5f6));
    case AT(0xc5fa):
      lda(s, 0x8000);
      set_data(s, W_ONE_SRC, c->a);
      lda(s, 0x0094);
      set_data(s, W_ONE_BANK, c->a);
      lda(s, 0x2300);
      set_data(s, W_ONE_BYTES, c->a);
      lda(s, 0x5000);
      set_data(s, W_ONE_VRAM, c->a);
      lda(s, 0x8259);
      ldy(s, 0x0082);
      ran(s, DR_TONGUE_RUN_C5FA);
      return stop(AT(0xc618));
    case AT(0xc61c):
      lda(s, 0x0001);
      ran(s, DR_TONGUE_RUN_C61C);
      return stop(AT(0xc61f));
    case AT(0xc623):
      lda(s, data(s, W_ONE_BYTES));
      ran(s, DR_TONGUE_RUN_C623);
      if (!zero(s)) {
        taken(s);
        return AT(0xc61c);
      }
      PORT_COVER(dr_tongue_tiles_sent);
      lda(s, 0x0020);
      set_data(s, W_PLANE_AT_X, c->a);
      lda(s, 0x0048);
      set_data(s, W_PLANE_AT_Y, c->a);
      lda(s, field(s, DP_AT_X));
      set_field(s, DP_X, c->a);
      lda(s, field(s, DP_AT_Y));
      set_field(s, DP_Y, c->a);
      set_field(s, DP_COUNT, 0);
      set_field(s, DP_PICTURE_TICKS, 0);
      set_field(s, DP_SPIT_LEFT, 0);
      set_field(s, DP_MIRRORED, 0);
      set_field(s, DP_BROKEN, 0);
      set_field(s, DP_HIT, 0);
      set_field(s, DP_FLASH, 0);
      set_field(s, DP_PICTURE, 0);
      set_field(s, DP_SHOWN, 0);
      set_field(s, DP_STEP_X, 0);
      set_field(s, DP_STEP_Y, 0);
      set_field(s, DP_FACING, 0);
      set_field(s, DP_LIFE, 0);
      ran(s, DR_TONGUE_RUN_C628);
      return rts(s);

    // `$83:C657`: no tongue out, its records, its colours' turning, its
    // health and its handler.
    case AT(0xc657):
      ran(s, DR_TONGUE_RUN_C657);
      return jsr(s, 0xc657, 0xbb31);
    case AT(0xc65a):
      ran(s, DR_TONGUE_RUN_C65A);
      return jsr(s, 0xc65a, 0xc577);
    case AT(0xc65d):
      lda(s, 0x0001);
      ran(s, DR_TONGUE_RUN_C65D);
      return stop(AT(0xc660));
    case AT(0xc664):
      lda(s, 0xa36e);
      ldy(s, 0x0080);
      ran(s, DR_TONGUE_RUN_C664);
      return stop(AT(0xc66a));
    case AT(0xc66e):
      ran(s, DR_TONGUE_RUN_C66E);
      if (zero(s)) {
        taken(s);
        return AT(0xc65d);
      }
      PORT_COVER(dr_tongue_colours_begun);
      lda(s, 0x0023);
      set_field(s, DP_HEALTH, c->a);
      lda(s, 0x000f);
      ran(s, DR_TONGUE_RUN_C670);
      return stop(AT(0xc678));
    case AT(0xc67c):
      lda(s, 0xc994);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C67C);
      return stop(AT(0xc682));
    case AT(0xc686):
      ran(s, DR_TONGUE_RUN_C686);
      return rts(s);

    // --- Dr. Tongue ----------------------------------------------------------
    case AT(0xc687):
      PORT_COVER(dr_tongue_begins);
      set_c(c, false);
      lda(s, data(s, W_LOAD));
      adc(s, 0x0023);
      set_data(s, W_LOAD, c->a);
      ran(s, DR_TONGUE_RUN_C687);
      return jsr(s, 0xc691, 0xc793);
    case AT(0xc694):
      lda(s, 0x003c);
      ran(s, DR_TONGUE_RUN_C694);
      return stop(AT(0xc697));
    case AT(0xc69b):
      ran(s, DR_TONGUE_RUN_C69B);
      return jsr(s, 0xc69b, 0xc5e6);
    case AT(0xc69e):
      ran(s, DR_TONGUE_RUN_C69E);
      return jsr(s, 0xc69e, 0xc6f3);
    case AT(0xc6a1):
      ran(s, DR_TONGUE_RUN_C6A1);
      return jsr(s, 0xc6a1, 0xc657);
    case AT(0xc6a4):
      ran(s, DR_TONGUE_RUN_C6A4);
      return jsr(s, 0xc6a4, 0xc01a);

    // Each frame.
    case AT(0xc6a7):
      ran(s, DR_TONGUE_RUN_C6A7);
      return jsr(s, 0xc6a7, 0xbb27);
    case AT(0xc6aa):
      set_field(s, DP_HIT_BY, 0);
      push16(s->w, c, 0xc6b3);
      lda(s, field(s, DP_STATE));
      dec_a(s);
      push16(s->w, c, c->a);
      ran(s, DR_TONGUE_RUN_C6AA);
      return dispatched(s, rts(s));
    case AT(0xc6b4):
      ran(s, DR_TONGUE_RUN_C6B4);
      return jsr(s, 0xc6b4, 0xbf06);
    case AT(0xc6b7):
      lda(s, 0x0001);
      ran(s, DR_TONGUE_RUN_C6B7);
      return stop(AT(0xc6ba));
    case AT(0xc6be):
      ran(s, DR_TONGUE_RUN_C6BE);
      return jsr(s, 0xc6be, 0xbf38);
    case AT(0xc6c1):
      ran(s, DR_TONGUE_RUN_C6C1);
      return jsr(s, 0xc6c1, 0xbb44);
    case AT(0xc6c4):
      lda(s, field(s, DP_BROKEN));
      ran(s, DR_TONGUE_RUN_C6C4);
      if (zero(s)) {
        taken(s);
        return AT(0xc6a7);
      }
      lda(s, field(s, DP_TONGUES));
      ran(s, DR_TONGUE_RUN_C6C8);
      if (zero(s)) {
        taken(s);
        return AT(0xc6d5);
      }
      PORT_COVER(dr_tongue_tongues_wait);
      lda(s, 0x0001);
      ran(s, DR_TONGUE_RUN_C6CC);
      return stop(AT(0xc6cf));
    case AT(0xc6d3):
      ran(s, DR_TONGUE_RUN_C6D3);
      taken(s);
      return AT(0xc6c1);

    // Dead.
    case AT(0xc6d5):
      PORT_COVER(dr_tongue_dies);
      ldx(s, 0x2000);
      lda(s, field(s, DP_HIT_BY));
      ran(s, DR_TONGUE_RUN_C6D5);
      if (zero(s)) {
        taken(s);
        return AT(0xc6e0);
      }
      PORT_COVER(dr_tongue_pays);
      return stop(AT(0xc6dc));
    case AT(0xc6e0):
      ran(s, DR_TONGUE_RUN_C6E0);
      return jsr(s, 0xc6e0, 0xc966);
    case AT(0xc6e3):
      set_c(c, true);
      lda(s, data(s, W_LOAD));
      sbc(s, 0x0023);
      set_data(s, W_LOAD, c->a);
      ran(s, DR_TONGUE_RUN_C6E3);
      if (minus(s)) {
        s->k->lost = true;  // `BMI *`: it hangs
        return stop(AT(0xc6ed));
      }
      ran(s, DR_TONGUE_RUN_C6EF);
      return jsr(s, 0xc6ef, 0xc836);
    case AT(0xc6f2):
      return stop(AT(0xc6f2));

    // `$83:C6F3`: he blinks until a player is within 65, drinks the potion, and
    // is the head, in three pictures under the mosaic.
    case AT(0xc6f3):
      lda(s, 0x0041);
      ldx(s, field(s, DP_X));
      ldy(s, field(s, DP_Y));
      ran(s, DR_TONGUE_RUN_C6F3);
      return stop(AT(0xc6fa));
    case AT(0xc6fe):
      ldx(s, c->a);
      ran(s, DR_TONGUE_RUN_C6FE);
      if (!zero(s)) {
        taken(s);
        return AT(0xc725);
      }
      PORT_COVER(dr_tongue_blinks);
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      ran(s, DR_TONGUE_RUN_C701);
      if (!minus(s)) {
        taken(s);
        return AT(0xc70c);
      }
      PORT_COVER(dr_tongue_blink_cry);
      ran(s, DR_TONGUE_RUN_C706);
      return jsr(s, 0xc706, 0xc7d7);
    case AT(0xc709):
      lda(s, 0x002c);
      ran(s, DR_TONGUE_RUN_C709);
      return AT(0xc70c);
    case AT(0xc70c):
      set_field(s, DP_COUNT, c->a);
      and_(s, 0x0008);
      lsr_a(s);
      lsr_a(s);
      ldx(s, c->a);
      ldy(s, field(s, DP_RECORD));
      lda(s, data(s, (uint16_t)(T_BLINK + c->x)));
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      lda(s, 0x0005);
      ran(s, DR_TONGUE_RUN_C70C);
      return stop(AT(0xc71f));
    case AT(0xc723):
      ran(s, DR_TONGUE_RUN_C723);
      taken(s);
      return AT(0xc6f3);
    case AT(0xc725):
      PORT_COVER(dr_tongue_drinks);
      lda(s, 0x0005);
      ran(s, DR_TONGUE_RUN_C725);
      return stop(AT(0xc728));
    case AT(0xc72c):
      lda(s, 0xb65a);
      ran(s, DR_TONGUE_RUN_C72C);
      return stop(AT(0xc72f));
    case AT(0xc733):
      lda(s, 0x0000);
      ran(s, DR_TONGUE_RUN_C733);
      return jsr(s, 0xc736, 0xba17);
    case AT(0xc739):
      lda(s, field(s, DP_X));
      set_data(s, W_PLANE_X, c->a);
      lda(s, field(s, DP_Y));
      set_data(s, W_PLANE_Y, c->a);
      ran(s, DR_TONGUE_RUN_C739);
      return jsr(s, 0xc743, 0xbb0a);
    case AT(0xc746):
      lda(s, field(s, DP_RECORD));
      ran(s, DR_TONGUE_RUN_C746);
      return stop(AT(0xc748));
    case AT(0xc74c):
      ran(s, DR_TONGUE_RUN_C74C);
      return jsr(s, 0xc74c, 0xc7d7);
    case AT(0xc74f):
      ran(s, DR_TONGUE_RUN_C74F);
      return jsr(s, 0xc74f, 0xc7e7);
    case AT(0xc752):
      lda(s, 0x000c);
      ran(s, DR_TONGUE_RUN_C752);
      return stop(AT(0xc755));
    case AT(0xc759):
      ran(s, DR_TONGUE_RUN_C759);
      return jsr(s, 0xc759, 0xc7d7);
    case AT(0xc75c):
      ran(s, DR_TONGUE_RUN_C75C);
      return jsr(s, 0xc75c, 0xc80d);
    case AT(0xc75f):
      lda(s, 0x0005);
      ran(s, DR_TONGUE_RUN_C75F);
      return stop(AT(0xc762));
    case AT(0xc766):
      lda(s, 0x0001);
      ran(s, DR_TONGUE_RUN_C766);
      return jsr(s, 0xc769, 0xba17);
    case AT(0xc76c):
      ran(s, DR_TONGUE_RUN_C76C);
      return jsr(s, 0xc76c, 0xc7e7);
    case AT(0xc76f):
      lda(s, 0x000c);
      ran(s, DR_TONGUE_RUN_C76F);
      return stop(AT(0xc772));
    case AT(0xc776):
      ran(s, DR_TONGUE_RUN_C776);
      return jsr(s, 0xc776, 0xc7d7);
    case AT(0xc779):
      ran(s, DR_TONGUE_RUN_C779);
      return jsr(s, 0xc779, 0xc80d);
    case AT(0xc77c):
      lda(s, 0x0005);
      ran(s, DR_TONGUE_RUN_C77C);
      return stop(AT(0xc77f));
    case AT(0xc783):
      lda(s, 0x0002);
      ran(s, DR_TONGUE_RUN_C783);
      return jsr(s, 0xc786, 0xba17);
    case AT(0xc789):
      ran(s, DR_TONGUE_RUN_C789);
      return jsr(s, 0xc789, 0xc7e7);
    case AT(0xc78c):
      set_field(s, DP_COUNT, 0);
      ran(s, DR_TONGUE_RUN_C78C);
      return rts(s);

    // `$83:C793`: Dr. Tongue's record, at the point he was given.
    case AT(0xc793):
      return stop(AT(0xc793));
    case AT(0xc797):
      set_field(s, DP_RECORD, c->a);
      ldx(s, c->a);
      lda(s, field(s, DP_AT_X));
      set_data(s, (uint16_t)(c->x + REC_X), c->a);
      set_data(s, (uint16_t)(c->x + REC_X_FRACTION), 0);
      lda(s, field(s, DP_AT_Y));
      set_data(s, (uint16_t)(c->x + REC_Y), c->a);
      lda(s, 0x008f);
      set_data(s, (uint16_t)(c->x + REC_PICTURES_BANK), c->a);
      lda(s, 0xf9e8);
      set_data(s, (uint16_t)(c->x + REC_PICTURES), c->a);
      lda(s, 0x0003);
      set_data(s, (uint16_t)(c->x + REC_COLLIDE_ID), c->a);
      lda(s, data(s, W_FRAME));
      set_data(s, (uint16_t)(c->x + REC_BORN), c->a);
      lda(s, 0x00c0);
      set_data(s, (uint16_t)(c->x + REC_PALETTE), c->a);
      lda(s, 0x8000);
      ora(s, data(s, (uint16_t)(c->x + REC_FLAGS)));
      set_data(s, (uint16_t)(c->x + REC_FLAGS), c->a);
      lda(s, 0x0000);
      ldy(s, c->a);
      ran(s, DR_TONGUE_RUN_C797);
      return stop(AT(0xc7d2));
    case AT(0xc7d6):
      ran(s, DR_TONGUE_RUN_C7D6);
      return rts(s);

    // `$83:C7D7`: a cry, with the sound set to 3.
    case AT(0xc7d7):
      lda(s, data(s, W_SOUNDS));
      cmp(s, 0x0003);
      ran(s, DR_TONGUE_RUN_C7D7);
      if (!zero(s)) {
        taken(s);
        return AT(0xc7e6);
      }
      PORT_COVER(dr_tongue_cry);
      lda(s, 0x0027);
      ran(s, DR_TONGUE_RUN_C7DF);
      return stop(AT(0xc7e2));
    case AT(0xc7e6):
      ran(s, DR_TONGUE_RUN_C7E6);
      return rts(s);

    // `$83:C7E7`: the mosaic finer, in fifteen steps of three frames...
    case AT(0xc7e7):
      lda(s, 0x000f);
      set_w7e(s, W7E_MOSAIC, c->a);
      ran(s, DR_TONGUE_RUN_C7E7);
      return AT(0xc7ee);
    case AT(0xc7ee):
      lda(s, w7e(s, W7E_MOSAIC));
      dec_a(s);
      ran(s, DR_TONGUE_RUN_C7EE);
      if (minus(s)) {
        taken(s);
        return AT(0xc80c);
      }
      set_w7e(s, W7E_MOSAIC, c->a);
      lda(s, 0xc949);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C7F5);
      return stop(AT(0xc7ff));
    case AT(0xc803):
      lda(s, 0x0003);
      ran(s, DR_TONGUE_RUN_C803);
      return stop(AT(0xc806));
    case AT(0xc80a):
      ran(s, DR_TONGUE_RUN_C80A);
      taken(s);
      return AT(0xc7ee);
    case AT(0xc80c):
      ran(s, DR_TONGUE_RUN_C80C);
      return rts(s);

    // `$83:C80D`: ...or coarser, in sixteen.
    case AT(0xc80d):
      lda(s, 0x0000);
      set_w7e(s, W7E_MOSAIC, c->a);
      ran(s, DR_TONGUE_RUN_C80D);
      return AT(0xc814);
    case AT(0xc814):
      lda(s, w7e(s, W7E_MOSAIC));
      inc_a(s);
      cmp(s, 0x0011);
      ran(s, DR_TONGUE_RUN_C814);
      if (carry(s)) {
        taken(s);
        return AT(0xc835);
      }
      set_w7e(s, W7E_MOSAIC, c->a);
      lda(s, 0xc949);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C81E);
      return stop(AT(0xc828));
    case AT(0xc82c):
      lda(s, 0x0003);
      ran(s, DR_TONGUE_RUN_C82C);
      return stop(AT(0xc82f));
    case AT(0xc833):
      ran(s, DR_TONGUE_RUN_C833);
      taken(s);
      return AT(0xc814);
    case AT(0xc835):
      ran(s, DR_TONGUE_RUN_C835);
      return rts(s);

    // --- Its death -----------------------------------------------------------
    //
    // `$83:C836`: twelve puffs, its colours flashing; the mosaic coarser in
    // fifteen steps of two frames, puffs and flashes by turns; its layer
    // blanked, the block put down, and the mosaic ended.
    case AT(0xc836):
      PORT_COVER(dr_tongue_death);
      lda(s, 0x002c);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_C836);
      return AT(0xc83b);
    case AT(0xc83b):
      ldx(s, field(s, DP_COUNT));
      lda(s, data(s, (uint16_t)(T_DEATH_PUFFS_AT + c->x)));
      add(s, field(s, DP_X));
      set_field(s, DP_AT_X, c->a);
      lda(s, data(s, (uint16_t)(T_DEATH_PUFFS_AT_Y + c->x)));
      add(s, field(s, DP_Y));
      set_field(s, DP_AT_Y, c->a);
      lda(s, 0xb1dd);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C83B);
      return stop(AT(0xc853));
    case AT(0xc857):
      lda(s, 0xffd1);
      ldy(s, 0x0097);
      ran(s, DR_TONGUE_RUN_C857);
      return stop(AT(0xc85d));
    case AT(0xc861):
      lda(s, 0x0006);
      ran(s, DR_TONGUE_RUN_C861);
      return stop(AT(0xc864));
    case AT(0xc868):
      lda(s, 0xfafc);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C868);
      return stop(AT(0xc86e));
    case AT(0xc872):
      lda(s, 0x0006);
      ran(s, DR_TONGUE_RUN_C872);
      return stop(AT(0xc875));
    case AT(0xc879):
      lda(s, field(s, DP_COUNT));
      sub(s, 0x0004);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_C879);
      if (!minus(s)) {
        taken(s);
        return AT(0xc83b);
      }
      lda(s, 0x0000);
      set_w7e(s, W7E_MOSAIC, c->a);
      lda(s, 0x000f);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_C883);
      return AT(0xc88f);
    case AT(0xc88f):
      lda(s, 0xc949);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C88F);
      return stop(AT(0xc895));
    case AT(0xc899):
      lda(s, w7e(s, W7E_MOSAIC));
      inc_a(s);
      set_w7e(s, W7E_MOSAIC, c->a);
      lda(s, 0x0002);
      ran(s, DR_TONGUE_RUN_C899);
      return stop(AT(0xc8a5));
    case AT(0xc8a9):
      lda(s, field(s, DP_COUNT));
      bit_imm(s, 0x0001);
      ran(s, DR_TONGUE_RUN_C8A9);
      if (!zero(s)) {
        taken(s);
        return AT(0xc8bc);
      }
      PORT_COVER(dr_tongue_death_flash);
      lda(s, 0xffd1);
      ldy(s, 0x0097);
      ran(s, DR_TONGUE_RUN_C8B0);
      return stop(AT(0xc8b6));
    case AT(0xc8ba):
      ran(s, DR_TONGUE_RUN_C8BA);
      taken(s);
      return AT(0xc8e5);
    case AT(0xc8bc):
      PORT_COVER(dr_tongue_death_puff);
      lda(s, field(s, DP_COUNT));
      asl_a(s);
      asl_a(s);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_DEATH_PUFFS_AT + c->x)));
      add(s, field(s, DP_X));
      set_field(s, DP_AT_X, c->a);
      lda(s, data(s, (uint16_t)(T_DEATH_PUFFS_AT_Y + c->x)));
      add(s, field(s, DP_Y));
      set_field(s, DP_AT_Y, c->a);
      lda(s, 0xb1dd);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C8BC);
      return stop(AT(0xc8d7));
    case AT(0xc8db):
      lda(s, 0xfafc);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C8DB);
      return stop(AT(0xc8e1));
    case AT(0xc8e5):
      dec_field(s, DP_COUNT);
      ran(s, DR_TONGUE_RUN_C8E5);
      if (!zero(s)) {
        taken(s);
        return AT(0xc88f);
      }
      lda(s, 0x0005);
      ran(s, DR_TONGUE_RUN_C8E9);
      return stop(AT(0xc8ec));
    case AT(0xc8f4):
      lda(s, 0x0000);
      set_w7e(s, W7E_SNAKEOID_UP, c->a);
      ran(s, DR_TONGUE_RUN_C8F4);
      return jsr(s, 0xc8fb, 0xc97c);
    case AT(0xc8fe):
      lda(s, 0xc95d);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_C8FE);
      return stop(AT(0xc904));
    case AT(0xc908):
      ran(s, DR_TONGUE_RUN_C908);
      return rts(s);

    // `$83:C966`: its six records freed.
    case AT(0xc966):
      ldx(s, 0x000a);
      set_field(s, DP_COUNT, c->x);
      ran(s, DR_TONGUE_RUN_C966);
      return AT(0xc96b);
    case AT(0xc96b):
      ldx(s, field(s, DP_COUNT));
      lda(s, field(s, (uint16_t)(DP_RECORD + c->x)));
      ran(s, DR_TONGUE_RUN_C96B);
      return stop(AT(0xc96f));
    case AT(0xc973):
      lda(s, field(s, DP_COUNT));
      dec_a(s);
      dec_a(s);
      set_field(s, DP_COUNT, c->a);
      ran(s, DR_TONGUE_RUN_C973);
      if (!minus(s)) {
        taken(s);
        return AT(0xc96b);
      }
      ran(s, DR_TONGUE_RUN_C97B);
      return rts(s);

    // `$83:C97C`: the map's block at (4, 2) put down as the block after it.
    case AT(0xc97c):
      PORT_COVER(dr_tongue_door);
      ldx(s, 0x0004);
      ldy(s, 0x0002);
      ran(s, DR_TONGUE_RUN_C97C);
      return stop(AT(0xc982));
    case AT(0xc986):
      lda(s, 0x002b);
      ldx(s, 0x0004);
      ldy(s, 0x0002);
      ran(s, DR_TONGUE_RUN_C986);
      return stop(AT(0xc98f));
    case AT(0xc993):
      ran(s, DR_TONGUE_RUN_C993);
      return rts(s);

    // --- Its shot ------------------------------------------------------------
    //
    // `$83:CA4A`, a thread of its own: half a second hidden, then it homes
    // in on the nearest player for three seconds.
    case AT(0xca4a):
      PORT_COVER(dr_tongue_shot_begins);
      set_c(c, false);
      lda(s, data(s, W_LOAD));
      adc(s, 0x0008);
      set_data(s, W_LOAD, c->a);
      ran(s, DR_TONGUE_RUN_CA4A);
      return jsr(s, 0xca54, 0xcb7d);
    case AT(0xca57):
      set_field(s, SHOT_RECORD, c->a);
      ldy(s, c->a);
      lda(s, 0x0008);
      ora(s, data(s, (uint16_t)(c->y + REC_FLAGS)));
      set_data(s, (uint16_t)(c->y + REC_FLAGS), c->a);
      lda(s, 0x001e);
      set_field(s, SHOT_FLASH, c->a);
      ran(s, DR_TONGUE_RUN_CA57);
      return AT(0xca68);
    case AT(0xca68):
      lda(s, 0x0001);
      ran(s, DR_TONGUE_RUN_CA68);
      return stop(AT(0xca6b));
    case AT(0xca6f):
      ran(s, DR_TONGUE_RUN_CA6F);
      return jsr(s, 0xca6f, 0xcb58);
    case AT(0xca72):
      dec_field(s, SHOT_FLASH);
      ran(s, DR_TONGUE_RUN_CA72);
      if (!minus(s)) {
        taken(s);
        return AT(0xca68);
      }
      PORT_COVER(dr_tongue_shot_shown);
      ldy(s, field(s, SHOT_RECORD));
      lda(s, 0xfff7);
      and_(s, data(s, (uint16_t)(c->y + REC_FLAGS)));
      set_data(s, (uint16_t)(c->y + REC_FLAGS), c->a);
      ran(s, DR_TONGUE_RUN_CA76);
      return AT(0xca81);
    case AT(0xca81):
      lda(s, 0x0001);
      ran(s, DR_TONGUE_RUN_CA81);
      return stop(AT(0xca84));
    case AT(0xca88):
      ran(s, DR_TONGUE_RUN_CA88);
      return jsr(s, 0xca88, 0xcb58);
    case AT(0xca8b):
      ran(s, DR_TONGUE_RUN_CA8B);
      return jsr(s, 0xca8b, 0xcac6);
    case AT(0xca8e):
      lda(s, field(s, SHOT_GONE));
      ran(s, DR_TONGUE_RUN_CA8E);
      if (zero(s)) {
        taken(s);
        return AT(0xca81);
      }
      PORT_COVER(dr_tongue_shot_gone);
      ldy(s, field(s, SHOT_RECORD));
      lda(s, 0x008f);
      set_data(s, (uint16_t)(c->y + REC_PICTURES_BANK), c->a);
      lda(s, 0xcab4);
      ran(s, DR_TONGUE_RUN_CA92);
      return stop(AT(0xca9d));
    case AT(0xcaa1):
      set_c(c, true);
      lda(s, data(s, W_LOAD));
      sbc(s, 0x0008);
      set_data(s, W_LOAD, c->a);
      ran(s, DR_TONGUE_RUN_CAA1);
      if (minus(s)) {
        s->k->lost = true;  // `BMI *`: it hangs
        return stop(AT(0xcaab));
      }
      lda(s, field(s, SHOT_RECORD));
      ran(s, DR_TONGUE_RUN_CAAD);
      return stop(AT(0xcaaf));

    // `$83:CAC6`: each fourth frame it turns to the nearest, and it flies.
    case AT(0xcac6):
      lda(s, data(s, W_TICK));
      and_(s, 0x0003);
      ran(s, DR_TONGUE_RUN_CAC6);
      if (!zero(s)) {
        taken(s);
        return AT(0xcb08);
      }
      PORT_COVER(dr_tongue_shot_homes);
      ldx(s, field(s, SHOT_X));
      ldy(s, field(s, SHOT_Y));
      ran(s, DR_TONGUE_RUN_CACE);
      return stop(AT(0xcad2));
    case AT(0xcad6):
      ldy(s, c->x);
      ldx(s, field(s, SHOT_RECORD));
      ran(s, DR_TONGUE_RUN_CAD6);
      return stop(AT(0xcad9));
    case AT(0xcadd):
      asl_a(s);
      asl_a(s);
      ldx(s, c->a);
      lda(s, field(s, SHOT_DX));
      add(s, data(s, (uint16_t)(T_SHOT_TURN + c->x)));
      ldy(s, c->a);
      ran(s, DR_TONGUE_RUN_CADD);
      if (!minus(s)) {
        taken(s);
        return AT(0xcaed);
      }
      eor(s, 0xffff);
      inc_a(s);
      ran(s, DR_TONGUE_RUN_CAE9);
      return AT(0xcaed);
    case AT(0xcaed):
      cmp(s, 0x0004);
      ran(s, DR_TONGUE_RUN_CAED);
      if (carry(s)) {
        taken(s);
        return AT(0xcaf4);
      }
      PORT_COVER(dr_tongue_shot_turns_x);
      set_field(s, SHOT_DX, c->y);
      ran(s, DR_TONGUE_RUN_CAF2);
      return AT(0xcaf4);
    case AT(0xcaf4):
      lda(s, field(s, SHOT_DY));
      add(s, data(s, (uint16_t)(T_SHOT_TURN_DY + c->x)));
      ldy(s, c->a);
      ran(s, DR_TONGUE_RUN_CAF4);
      if (!minus(s)) {
        taken(s);
        return AT(0xcb01);
      }
      eor(s, 0xffff);
      inc_a(s);
      ran(s, DR_TONGUE_RUN_CAFD);
      return AT(0xcb01);
    case AT(0xcb01):
      cmp(s, 0x0004);
      ran(s, DR_TONGUE_RUN_CB01);
      if (carry(s)) {
        taken(s);
        return AT(0xcb08);
      }
      PORT_COVER(dr_tongue_shot_turns_y);
      set_field(s, SHOT_DY, c->y);
      ran(s, DR_TONGUE_RUN_CB06);
      return AT(0xcb08);
    case AT(0xcb08):
      lda(s, field(s, SHOT_X));
      add(s, field(s, SHOT_DX));
      set_field(s, SHOT_X, c->a);
      lda(s, field(s, SHOT_Y));
      add(s, field(s, SHOT_DY));
      set_field(s, SHOT_Y, c->a);
      ldx(s, field(s, SHOT_X));
      ldy(s, field(s, SHOT_Y));
      ran(s, DR_TONGUE_RUN_CB08);
      return stop(AT(0xcb1a));
    case AT(0xcb1e):
      ran(s, DR_TONGUE_RUN_CB1E);
      if (carry(s)) {
        taken(s);
        return AT(0xcb31);
      }
      PORT_COVER(dr_tongue_shot_flies);
      ldy(s, field(s, SHOT_RECORD));
      lda(s, field(s, SHOT_X));
      set_data(s, (uint16_t)(c->y + REC_X), c->a);
      lda(s, field(s, SHOT_Y));
      set_data(s, (uint16_t)(c->y + REC_Y), c->a);
      dec_field(s, SHOT_LEFT);
      ran(s, DR_TONGUE_RUN_CB20);
      if (minus(s)) {
        taken(s);
        return AT(0xcb31);
      }
      ran(s, DR_TONGUE_RUN_CB30);
      return rts(s);
    case AT(0xcb31):
      PORT_COVER(dr_tongue_shot_ends);
      dec_field(s, SHOT_GONE);
      ran(s, DR_TONGUE_RUN_CB31);
      return rts(s);

    // `$83:CB58`: its next picture, every four frames.
    case AT(0xcb58):
      dec_field(s, SHOT_FRAME_LEFT);
      ran(s, DR_TONGUE_RUN_CB58);
      if (!minus(s)) {
        taken(s);
        return AT(0xcb73);
      }
      PORT_COVER(dr_tongue_shot_frame);
      lda(s, 0x0003);
      set_field(s, SHOT_FRAME_LEFT, c->a);
      lda(s, field(s, SHOT_FRAME));
      inc_a(s);
      set_field(s, SHOT_FRAME, c->a);
      and_(s, 0x0001);
      asl_a(s);
      ldx(s, c->a);
      ldy(s, field(s, SHOT_RECORD));
      lda(s, data(s, (uint16_t)(T_SHOT_FRAMES + c->x)));
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      ran(s, DR_TONGUE_RUN_CB5C);
      return AT(0xcb73);
    case AT(0xcb73):
      ran(s, DR_TONGUE_RUN_CB73);
      return rts(s);

    // `$83:CB7D`: its record, the way `$04` says.
    case AT(0xcb7d):
      return stop(AT(0xcb7d));
    case AT(0xcb81):
      push16(s->w, c, c->a);
      ldy(s, c->a);
      lda(s, field(s, DP_AT_X));
      set_field(s, SHOT_X, c->a);
      set_data(s, (uint16_t)(c->y + REC_X), c->a);
      lda(s, 0x000c);
      set_data(s, (uint16_t)(c->y + REC_X_FRACTION), c->a);
      lda(s, field(s, DP_AT_Y));
      set_field(s, SHOT_Y, c->a);
      set_data(s, (uint16_t)(c->y + REC_Y), c->a);
      lda(s, field(s, DP_ARG));
      asl_a(s);
      set_field(s, SHOT_WAY, c->a);
      ldx(s, c->a);
      lda(s, data(s, (uint16_t)(T_SHOT_WAY + c->x)));
      set_field(s, SHOT_DX, c->a);
      lda(s, data(s, (uint16_t)(T_SHOT_WAY_DY + c->x)));
      set_field(s, SHOT_DY, c->a);
      lda(s, 0x008f);
      set_data(s, (uint16_t)(c->y + REC_PICTURES_BANK), c->a);
      lda(s, 0xfc4b);
      set_data(s, (uint16_t)(c->y + REC_PICTURES), c->a);
      lda(s, data(s, W_FRAME));
      set_data(s, (uint16_t)(c->y + REC_BORN), c->a);
      lda(s, 0x0003);
      set_data(s, (uint16_t)(c->y + REC_COLLIDE_ID), c->a);
      lda(s, 0x8000);
      ora(s, data(s, (uint16_t)(c->y + REC_FLAGS)));
      set_data(s, (uint16_t)(c->y + REC_FLAGS), c->a);
      lda(s, 0x00b4);
      set_field(s, SHOT_LEFT, c->a);
      set_field(s, SHOT_GONE, 0);
      set_field(s, SHOT_FRAME, 0);
      set_field(s, SHOT_FRAME_LEFT, 0);
      lda(s, 0xcc03);
      ldy(s, 0x0083);
      ran(s, DR_TONGUE_RUN_CB81);
      return stop(AT(0xcbd9));
    case AT(0xcbdd):
      lda(s, pull16(s->w, c));
      ran(s, DR_TONGUE_RUN_CBDD);
      return rts(s);
  }
  s->k->lost = true;
  return stop(pc);
}

// --- The machine --------------------------------------------------------------

void dr_tongue_work_init(DrTongueWork* k) {
  k->trace.n = 0;
  k->trace.cap = DR_TONGUE_TRACE_STEPS;
  k->trace.full = false;
  k->trace.step = k->steps;
  k->lost = false;
  k->v_unknown = false;
}

void dr_tongue_run(Wram* w, const Rom* rom, PortCpu* c, DrTongueWork* k) {
  DrTongue s = {w, rom, c, k};
  uint32_t pc = c->pc & 0xffffffu;
  for (int i = 0; i < 2048; i++) {
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

// --- `$83:C994`, the head's handler -------------------------------------------

#define HANDLER_SELF_OFF 0x0000u
#define HANDLER_SELF_TONGUE 0x0009u
#define HANDLER_TOUCH_A 0x0005u   // a player's touch, with the top bit
#define HANDLER_TOUCH_B 0x0006u
#define HANDLER_WAYS_LEFT 0x0041u // 65 frames, and a draw of 32 more

static void piece(HandlerPieces* p, HandlerPiece what) {
  if (p && p->count < HANDLER_PIECES_MAX) p->piece[p->count++] = (uint8_t)what;
}

static uint16_t dp_r(const Wram* w, uint16_t dp, uint16_t at) {
  return wram_r16(w, (uint16_t)(dp + at));
}

static void dp_w(Wram* w, uint16_t dp, uint16_t at, uint16_t v) {
  wram_w16(w, (uint16_t)(dp + at), v);
}

// `$83:C4A9`, by the `JSR` at `$83:CA37`: its records hurt what they touch,
// and it goes off a way at random of the three about the way back from the
// one it went, for 65 to 96 frames. The first draw takes the carry the
// handler's `CMP` left, set; the second the `ASL`'s, clear.
static void bounce(Wram* w, uint16_t dp, ActorHandlerRegs* r,
                   HandlerPieces* pieces) {
  for (uint16_t i = 0; i <= 0x0a; i += 2) {  // `$83:C001`
    const uint16_t record = dp_r(w, dp, (uint16_t)(DR_TONGUE_DP_RECORD + i));
    wram_w16(w, (uint32_t)record + ACTOR_COLLIDE_ID, DR_TONGUE_CHARGING_ID);
  }
  dp_w(w, dp, DR_TONGUE_DP_TEMP, DR_TONGUE_CHARGING_ID);
  r->x = 0;
  r->y = dp_r(w, dp, DR_TONGUE_DP_RECORD);

  RngResult first, second;
  rng_next(w, true, &first);
  piece(pieces, first.v ? HP_DR_TONGUE_DRAW_TWICE : HP_DR_TONGUE_DRAW_ONCE);
  const uint16_t turn = first.a & 0x0003u;
  dp_w(w, dp, DR_TONGUE_DP_TEMP, turn);
  // `LDA $3C : LSR : DEC : CLC : ADC #$0003 : ADC $62`: the carry of the
  // first sum goes into the second, which matters only for a way of none.
  const uint16_t half = (uint16_t)((dp_r(w, dp, DR_TONGUE_DP_WAY) >> 1) - 1);
  const uint32_t sum = (uint32_t)half + 3u;
  const uint16_t way =
      (uint16_t)(((((uint16_t)sum + turn + (sum >> 16)) & 0x0007u) + 1u) << 1);
  dp_w(w, dp, DR_TONGUE_DP_FACING, way);
  dp_w(w, dp, DR_TONGUE_DP_WAY, way);

  rng_next(w, false, &second);
  piece(pieces, second.v ? HP_DR_TONGUE_DRAW_TWICE : HP_DR_TONGUE_DRAW_ONCE);
  dp_w(w, dp, DR_TONGUE_DP_LEFT,
       (uint16_t)((second.a & 0x001fu) + HANDLER_WAYS_LEFT));
  dp_w(w, dp, DR_TONGUE_DP_TARGET, DR_TONGUE_NO_TARGET);
  dp_w(w, dp, DR_TONGUE_DP_STUNG, 0);
  dp_w(w, dp, DR_TONGUE_DP_STATE, DR_TONGUE_CHARGE_STATE);
  // `LDA #$C518 : STA $40 : RTS`, `RTL`: the carry is the `ADC #$0041`'s.
  r->a = DR_TONGUE_CHARGE_STATE;
  r->n = true;
  r->z = false;
  r->c = false;
}

bool dr_tongue_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                       ActorHandlerRegs* r, HandlerPieces* pieces) {
  // `LDY $0078 : LDX $000E,Y`, and two `CPX`.
  const uint16_t record = wram_r16(w, W_HANDLER_SELF);
  const uint16_t self_id = wram_r16(w, (uint32_t)record + ACTOR_COLLIDE_ID);
  r->a = arg;
  r->x = self_id;
  r->y = record;
  if (self_id == HANDLER_SELF_OFF || self_id == HANDLER_SELF_TONGUE) {
    PORT_COVER(dr_tongue_hit_self);
    piece(pieces, self_id == HANDLER_SELF_OFF ? HP_DR_TONGUE_SELF_OFF
                                              : HP_DR_TONGUE_SELF_TONGUE);
    r->n = false;
    r->z = true;
    r->c = false;
    return true;
  }
  // `LDX $50 : BNE`: it is flashing.
  const uint16_t flash = dp_r(w, dp, DR_TONGUE_DP_FLASH);
  r->x = flash;
  if (flash != 0) {
    PORT_COVER(dr_tongue_hit_flashing);
    piece(pieces, HP_DR_TONGUE_FLASHING);
    r->n = (flash & 0x8000u) != 0;
    r->z = false;
    r->c = false;
    return true;
  }
  if (arg < COLLIDE_ID_PLAYER) {
    PORT_COVER(dr_tongue_hit_ignore);
    piece(pieces, HP_DR_TONGUE_IGNORE);
    r->n = ((uint16_t)(arg - COLLIDE_ID_PLAYER) & 0x8000u) != 0;
    r->z = false;
    r->c = false;
    return true;
  }

  // `$83:C9AF  STA $52 : AND #$7FFF`, and a player's touch.
  dp_w(w, dp, DR_TONGUE_DP_HIT_BY, arg);
  uint16_t id = arg & ENEMY_COLLIDE_ID_MASK;
  r->a = id;
  if (id == HANDLER_TOUCH_A || id == HANDLER_TOUCH_B) {
    PORT_COVER(dr_tongue_hit_touched);
    piece(pieces, id == HANDLER_TOUCH_A ? HP_DR_TONGUE_TOUCHED_5
                                        : HP_DR_TONGUE_TOUCHED_6);
    bounce(w, dp, r, pieces);
    return true;
  }

  // The ids it answers as others.
  const uint16_t tick = wram_r16(w, W_SCHED_TICK);
  if (id == 0x0060 || id == 0x0070) {
    const bool on = (tick & (id == 0x0060 ? 0x0007u : 0x000fu)) == 0;
    PORT_COVER(dr_tongue_hit_tossed);
    if (id == 0x0060)
      piece(pieces, on ? HP_DR_TONGUE_60_5C : HP_DR_TONGUE_60_5D);
    else
      piece(pieces, on ? HP_DR_TONGUE_70_5C : HP_DR_TONGUE_70_5D);
    id = on ? 0x005c : 0x005d;
  } else if (id == 0x006f) {
    PORT_COVER(dr_tongue_hit_6f);
    if (tick & 0x0007u) {
      piece(pieces, HP_DR_TONGUE_6F_5D);
      id = 0x005d;
    } else if (tick & 0x0008u) {
      piece(pieces, HP_DR_TONGUE_6F_63);
      id = 0x0063;
    } else {
      piece(pieces, HP_DR_TONGUE_6F_5C);
      id = 0x005c;
    }
  } else if (id == 0x0061) {
    PORT_COVER(dr_tongue_hit_as_5c);
    piece(pieces, HP_DR_TONGUE_61);
    id = 0x005c;
  } else if (id == 0x0062) {
    PORT_COVER(dr_tongue_hit_62);
    const bool on = (tick & 0x0003u) == 0x0003u;
    piece(pieces, on ? HP_DR_TONGUE_62_5C : HP_DR_TONGUE_62_5D);
    id = on ? 0x005c : 0x005d;
  } else if (id == 0x0067) {
    PORT_COVER(dr_tongue_hit_as_5c);
    piece(pieces, HP_DR_TONGUE_67);
    id = 0x005c;
  } else {
    piece(pieces, HP_DR_TONGUE_OTHER);
  }

  // `$83:CA11  SEC : SBC #$005C : ASL : TAX : DEC $4E`, and the damage added
  // to `$5C`.
  const uint16_t index = (uint16_t)((id - COLLIDE_ID_PLAYER) * 2);
  const uint16_t damage = rom_word(rom, ENEMY_DAMAGE_TABLE + (uint32_t)index);
  r->x = index;
  dp_w(w, dp, DR_TONGUE_DP_HIT, (uint16_t)(dp_r(w, dp, DR_TONGUE_DP_HIT) - 1));
  dp_w(w, dp, DR_TONGUE_DP_STUNG,
       (uint16_t)(dp_r(w, dp, DR_TONGUE_DP_STUNG) + damage));

  // `SEC : LDA $4C : SBC $818561,X : BMI`.
  const uint16_t health = dp_r(w, dp, DR_TONGUE_DP_HEALTH);
  const uint16_t left = (uint16_t)(health - damage);
  r->a = left;
  if (left & 0x8000u) {
    // `DEC $4A : SEC : RTL`.
    PORT_COVER(dr_tongue_hit_broken);
    piece(pieces, HP_DR_TONGUE_DIED);
    const uint16_t broken = (uint16_t)(dp_r(w, dp, DR_TONGUE_DP_BROKEN) - 1);
    dp_w(w, dp, DR_TONGUE_DP_BROKEN, broken);
    r->n = (broken & 0x8000u) != 0;
    r->z = broken == 0;
    r->c = true;
    return true;
  }
  // `CMP $4C : BEQ`, then `STA $4C`: N and Z are the compare's.
  r->n = ((uint16_t)(left - health) & 0x8000u) != 0;
  r->z = left == health;
  r->c = true;
  if (left == health) {
    PORT_COVER(dr_tongue_hit_no_damage);
    piece(pieces, HP_DR_TONGUE_NO_DAMAGE);
    return true;
  }
  PORT_COVER(dr_tongue_hit_hurt);
  piece(pieces, HP_DR_TONGUE_SURVIVED);
  dp_w(w, dp, DR_TONGUE_DP_HEALTH, left);
  return true;
}

// --- `$83:CC03`, its shot's handler -------------------------------------------

bool dr_tongue_shot_collide(Wram* w, uint16_t dp, uint16_t arg,
                            ActorHandlerRegs* r, HandlerPieces* pieces) {
  // `TAY`, and three compares.
  r->a = arg;
  r->y = arg;
  HandlerPiece way;
  if (arg == 0x0001) {
    way = HP_DR_TONGUE_SHOT_1;
  } else if (arg == 0x0005) {
    way = HP_DR_TONGUE_SHOT_5;
  } else if (arg == 0x0006) {
    way = HP_DR_TONGUE_SHOT_6;
  } else {
    // `AND #$7FFF : CMP #$005C : BCS`.
    r->a = arg & ENEMY_COLLIDE_ID_MASK;
    if (r->a < COLLIDE_ID_PLAYER) {
      PORT_COVER(dr_tongue_shot_hit_ignore);
      piece(pieces, HP_DR_TONGUE_SHOT_IGNORE);
      r->n = ((uint16_t)(r->a - COLLIDE_ID_PLAYER) & 0x8000u) != 0;
      r->z = false;
      r->c = false;
      return true;
    }
    way = HP_DR_TONGUE_SHOT_HIT;
  }
  // `LDY $08 : LDA #$0000 : STA $000E,Y : DEC $14`, and the carry the
  // compare left, set.
  PORT_COVER(dr_tongue_shot_hit);
  piece(pieces, way);
  const uint16_t record = dp_r(w, dp, DR_TONGUE_SHOT_DP_RECORD);
  r->y = record;
  r->a = 0;
  wram_w16(w, (uint32_t)record + ACTOR_COLLIDE_ID, 0);
  const uint16_t gone = (uint16_t)(dp_r(w, dp, DR_TONGUE_SHOT_DP_GONE) - 1);
  dp_w(w, dp, DR_TONGUE_SHOT_DP_GONE, gone);
  r->n = (gone & 0x8000u) != 0;
  r->z = gone == 0;
  r->c = true;
  return true;
}
