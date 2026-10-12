#include "port/intro.h"

#include "port/coverage.h"
#include "port/logo.h"

static void load_a(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

static void load_x(PortCpu* c, uint16_t v) {
  c->x = v;
  set_nz16(c, v);
}

static void load_y(PortCpu* c, uint16_t v) {
  c->y = v;
  set_nz16(c, v);
}

static uint16_t dp(const PortCpu* c, uint8_t at) {
  return (uint16_t)(c->d + at);
}

void intro_begin(Wram* w, PortCpu* c) {
  PORT_COVER(intro_begin);
  // `PHB : PHK : PLB`, then `PHD : PEA $0000 : PLD`.
  push8(w, c, c->db);
  push8(w, c, INTRO_DATA_BANK);
  c->db = pull8(w, c);
  push16(w, c, c->d);
  push16(w, c, 0);
  c->d = pull16(w, c);
  load_a(c, (uint16_t)(wram_r16(w, W_LOGO_BRIGHTNESS) | INTRO_SCREEN_OFF));
  wram_w16(w, W_LOGO_BRIGHTNESS, c->a);
  c->pc = INTRO_BEGUN_PC;
}

// What `dma_to_vram` is called with: the scratch buffer's bank on the stack,
// its address in A, VRAM's in X and how much in Y.
static void send_ask(Wram* w, PortCpu* c, uint16_t vram, uint16_t bytes) {
  push16(w, c, INTRO_SCRATCH_BANK);
  c->a = INTRO_SCRATCH_AT;
  c->x = vram;
  load_y(c, bytes);
}

// ...and `lzss_decompress`: the packed data's address on the stack, its bank
// in A, and the scratch buffer's bank and address in X and Y.
static void unpack_ask(Wram* w, PortCpu* c, uint16_t packed) {
  push16(w, c, packed);
  c->a = INTRO_DATA_BANK;
  c->x = INTRO_SCRATCH_BANK;
  load_y(c, INTRO_SCRATCH_AT);
}

void intro_map_send_ask(Wram* w, PortCpu* c) {
  PORT_COVER(intro_map_send_ask);
  send_ask(w, c, INTRO_MAP_VRAM, INTRO_MAP_BYTES);
  c->pc = 0x83806du;
}

void intro_map_2_unpack_ask(Wram* w, PortCpu* c) {
  PORT_COVER(intro_map_2_unpack_ask);
  pull16(w, c);
  unpack_ask(w, c, INTRO_MAP_2_PACKED);
  c->pc = 0x83807fu;
}

void intro_map_2_send_ask(Wram* w, PortCpu* c) {
  PORT_COVER(intro_map_2_send_ask);
  pull16(w, c);
  send_ask(w, c, INTRO_MAP_2_VRAM, INTRO_MAP_BYTES);
  c->pc = 0x838091u;
}

void intro_tiles_unpack_ask(Wram* w, PortCpu* c) {
  PORT_COVER(intro_tiles_unpack_ask);
  pull16(w, c);
  unpack_ask(w, c, INTRO_TILES_PACKED);
  c->pc = 0x8380a3u;
}

void intro_tiles_send_ask(Wram* w, PortCpu* c) {
  PORT_COVER(intro_tiles_send_ask);
  pull16(w, c);
  send_ask(w, c, INTRO_TILES_VRAM, INTRO_TILES_BYTES);
  c->pc = 0x8380b5u;
}

void intro_job_ask(Wram* w, PortCpu* c) {
  PORT_COVER(intro_job_ask);
  pull16(w, c);
  c->a = INTRO_JOB;
  load_y(c, INTRO_DATA_BANK);
  c->pc = 0x8380c0u;
}

void intro_job_kept(Wram* w, PortCpu* c) {
  PORT_COVER(intro_job_kept);
  wram_w16(w, dp(c, INTRO_DP_JOB_SLOT), c->x);
  c->pc = 0x8380c6u;
}

void intro_slide_begin(Wram* w, PortCpu* c) {
  PORT_COVER(intro_slide_begin);
  wram_w16(w, W_LOGO_BRIGHTNESS, INTRO_FULL_BRIGHTNESS);
  wram_w16(w, dp(c, LOGO_DP_OTHER), 0);
  load_a(c, INTRO_SLIDE_FIRST_TILE);
  wram_w16(w, dp(c, LOGO_DP_TILE), c->a);
  c->pc = LOGO_SLIDE_WAI_PC;
}

void intro_sweep_colours_ask(Wram* w, PortCpu* c) {
  (void)w;
  PORT_COVER(intro_sweep_colours_ask);
  c->a = INTRO_SWEEP_COLOUR_BYTES;
  c->y = INTRO_SWEEP_COLOURS;
  load_x(c, INTRO_SWEEP_FIRST_BYTE);
  c->pc = 0x83817bu;
}

static void times_begin(Wram* w, PortCpu* c) {
  load_a(c, INTRO_SWEEP_TIMES);
  wram_w16(w, dp(c, LOGO_DP_TIMES), c->a);
}

void intro_sweep_begin(Wram* w, PortCpu* c) {
  PORT_COVER(intro_sweep_begin);
  times_begin(w, c);
  c->pc = LOGO_SWEEP_WAI_PC;
}

void intro_sweep_2_begin(Wram* w, PortCpu* c) {
  PORT_COVER(intro_sweep_2_begin);
  times_begin(w, c);
  c->pc = LOGO_SWEEP_2_WAI_PC;
}

void intro_job_remove_ask(Wram* w, PortCpu* c) {
  PORT_COVER(intro_job_remove_ask);
  load_a(c, wram_r16(w, dp(c, INTRO_DP_JOB_SLOT)));
  c->pc = 0x83822cu;
}

void intro_job_forgotten(Wram* w, PortCpu* c) {
  PORT_COVER(intro_job_forgotten);
  wram_w16(w, dp(c, INTRO_DP_JOB_SLOT), 0);
  c->pc = 0x838232u;
}

void intro_blank(Wram* w, PortCpu* c) {
  PORT_COVER(intro_blank);
  load_a(c, INTRO_SCREEN_OFF);
  wram_w16(w, W_LOGO_BRIGHTNESS, c->a);
  c->pc = 0x838239u;
}

void intro_end(Wram* w, PortCpu* c) {
  PORT_COVER(intro_end);
  c->d = pull16(w, c);
  c->db = pull8(w, c);
  set_nz8(c, c->db);
  c->pc = 0x838240u;
}
