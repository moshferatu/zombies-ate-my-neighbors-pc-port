#include "port/vblank.h"

#include "port/coverage.h"
#include "port/frontend.h"  // W_FRAME_COUNT

// `XBA`: the accumulator's bytes swapped, N and Z from the new low byte.
static void xba(PortCpu* c) {
  c->a = (uint16_t)(c->a << 8 | c->a >> 8);
  set_nz8(c, (uint8_t)c->a);
}

// `SEP #$30`: both widths to 8 bits, which drops X's and Y's high bytes.
static void sep30(PortCpu* c) { set_p(c, (uint8_t)(c->p | PORT_P_M | PORT_P_X)); }

static void rep30(PortCpu* c) {
  c->p = (uint8_t)(c->p & ~(PORT_P_M | PORT_P_X));
}

// `LDA #imm` with the accumulator 8 bits wide: the high byte stays.
static void lda8(PortCpu* c, uint8_t v) {
  c->a = (uint16_t)((c->a & 0xff00u) | v);
  set_nz8(c, v);
}

static void lda16(PortCpu* c, uint16_t v) {
  c->a = v;
  set_nz16(c, v);
}

// `SEP #$20 : LDA #$01 : STA $420B : REP #$20`, the last instruction in the
// run after it.
static void dma_go(PortCpu* c, HwTrace* t) {
  hw_run(t, VBL_GO);
  lda8(c, 0x01);
  hw_w8(t, 0x420b, 0x01);
}

void vram_queue_flush(Wram* w, PortCpu* c, HwTrace* t) {
  const uint16_t flags = wram_r16(w, W_RENDER_FLAGS);
  hw_run(t, VQ_HEAD);
  bit16(c, flags);
  if (flags & 0x4000u) {
    PORT_COVER(vram_flush_held);
    hw_run(t, VQ_BUSY);
    set_c(c, true);
    return;
  }
  const uint16_t count = wram_r16(w, W_VRAM_QUEUE_COUNT);
  if (count == 0) {
    PORT_COVER(vram_flush_empty);
    hw_run(t, VQ_EMPTY);
    lda16(c, 0);
    set_c(c, false);
    return;
  }
  PORT_COVER(vram_flush_sent);
  hw_run(t, VQ_START);
  lda16(c, 0x1801);
  hw_w16(t, 0x4300, 0x1801);
  hw_run(t, VQ_FIRST);
  uint16_t x = 0;
  for (;;) {
    hw_run(t, VBL_LOAD);
    hw_w16(t, 0x4302, wram_r16(w, W_VRAM_QUEUE_SRC + x));
    hw_run(t, VBL_LOAD);
    hw_w16(t, 0x4304, wram_r16(w, W_VRAM_QUEUE_BANK + x));
    hw_run(t, VBL_LOAD);
    const uint16_t dest = wram_r16(w, W_VRAM_QUEUE_DEST + x);
    lda16(c, dest);
    hw_w16(t, 0x2116, dest);
    hw_run(t, VQ_VMAIN);
    const uint8_t vmain = wram_r8(w, W_VRAM_QUEUE_VMAIN + x);
    lda8(c, vmain);
    hw_w8(t, 0x2115, vmain);
    hw_run(t, VQ_SIZE);
    const uint16_t size = wram_r16(w, W_VRAM_QUEUE_SIZE + x);
    lda16(c, size);
    hw_w16(t, 0x4305, size);
    dma_go(c, t);
    hw_run(t, VBL_NEXT);
    x = (uint16_t)(x + 2);
    cmp16(c, x, count);
    if (x == count) break;
    hw_run(t, VBL_TAKEN);
  }
  c->x = x;
  hw_run(t, VQ_DONE);
  wram_w16(w, W_RENDER_FLAGS, 0);
  wram_w16(w, W_VRAM_QUEUE_COUNT, 0);
  wram_w16(w, W_TILEMAP_ARENA_LEFT, 0x0900);
  wram_w16(w, W_TILEMAP_ARENA_NEXT, 0x4b28);
  lda16(c, 0x4b28);
  set_c(c, false);
}

void sprite_upload_flush(Wram* w, PortCpu* c, HwTrace* t) {
  const uint16_t count = wram_r16(w, W_SPRITE_UPLOAD_COUNT);
  if (count == 0) {
    PORT_COVER(upload_oam_only);
    hw_run(t, SU_EMPTY);
    lda16(c, 0);
  } else {
    PORT_COVER(upload_frames);
    hw_run(t, SU_HEAD);
    lda16(c, 0x1801);
    hw_w16(t, 0x4300, 0x1801);
    hw_run(t, SU_VMAIN);
    lda8(c, 0x80);
    hw_w8(t, 0x2115, 0x80);
    hw_run(t, SU_INIT);
    c->y = 0x0040;
    uint16_t x = 0;
    for (;;) {
      hw_run(t, VBL_LOAD);
      hw_w16(t, 0x4302, wram_r16(w, W_SPRITE_UPLOAD_SRC + x));
      hw_run(t, VBL_LOAD);
      hw_w16(t, 0x4304, wram_r16(w, W_SPRITE_UPLOAD_BANK + x));
      hw_run(t, VBL_STORE);
      hw_w16(t, 0x4305, c->y);
      hw_run(t, VBL_LOAD);
      // The frame's top row of tiles, then the row under it: a 16x16 sprite
      // is two 64-byte rows, `$100` words apart in VRAM.
      const uint16_t dest = wram_r16(w, W_SPRITE_UPLOAD_DEST + x);
      lda16(c, dest);
      hw_w16(t, 0x2116, dest);
      dma_go(c, t);
      hw_run(t, SU_VADDR2);
      lda16(c, (uint16_t)(dest | 0x0100));
      hw_w16(t, 0x2116, c->a);
      hw_run(t, VBL_STORE);
      hw_w16(t, 0x4305, c->y);
      dma_go(c, t);
      hw_run(t, VBL_NEXT);
      x = (uint16_t)(x + 2);
      cmp16(c, x, count);
      if (x == count) break;
      hw_run(t, VBL_TAKEN);
    }
    c->x = x;
    hw_run(t, SU_CLEAR);
    wram_w16(w, W_SPRITE_UPLOAD_COUNT, 0);
  }
  // All of OAM, from the buffer the sprite pass built.
  hw_run(t, VBL_STORE);
  hw_w16(t, 0x2102, 0x0000);
  hw_run(t, SU_OAM_IMM);
  hw_w16(t, 0x4300, 0x0400);
  hw_run(t, SU_OAM_IMM);
  hw_w16(t, 0x4302, 0x13be);
  hw_run(t, SU_OAM_IMM);
  hw_w16(t, 0x4304, 0x0000);
  hw_run(t, SU_OAM_IMM);
  hw_w16(t, 0x4305, 0x0220);
  lda16(c, 0x0220);
  dma_go(c, t);
  hw_run(t, SU_OAM_LAST);
  lda16(c, 0x8000);
  hw_w16(t, 0x2102, 0x8000);
  hw_run(t, SU_TAIL);
  set_c(c, true);
}

// `LDY abs : TYA : XBA : SEP #$30 : STY reg : STA reg : REP #$30`, the idiom
// both scroll jobs write a scroll register's two bytes with.
static void scroll_write(PortCpu* c, HwTrace* t, uint16_t reg, uint16_t v) {
  c->y = v;
  c->a = v;
  xba(c);
  sep30(c);
  hw_w8(t, reg, (uint8_t)v);
  hw_run(t, VBL_STORE);
  hw_w8(t, reg, (uint8_t)(v >> 8));
}

void bg2_scroll_job(Wram* w, PortCpu* c, HwTrace* t) {
  hw_run(t, B2_H);
  scroll_write(c, t, 0x210f, wram_r16(w, W_CAMERA_SUB_X));
  rep30(c);
  hw_run(t, B2_V);
  scroll_write(c, t, 0x2110, wram_r16(w, W_CAMERA_SUB_Y));
  rep30(c);
  // `$2108` is BG2SC: the tilemap's word address over 256, and bit 0 for a
  // map two screens wide.
  hw_run(t, B2_BASE);
  c->a = wram_r16(w, W_BG2_TILEMAP_VRAM);
  xba(c);
  lda16(c, (uint16_t)(c->a | 0x0001));
  set_c(c, true);
  hw_w8(t, 0x2108, (uint8_t)c->a);
  hw_run(t, B2_TAIL);
}

void camera_scroll_job(Wram* w, PortCpu* c, HwTrace* t) {
  set_c(c, true);
  const uint16_t dx = sbc16(c, wram_r16(w, W_CAMERA_X), wram_r16(w, W_BOSS_PLANE_X));
  c->a = dx;
  hw_run(t, CS_DX);
  cmp16(c, dx, 0x0100);
  bool park = false;
  if (dx < 0x0100) {
    hw_run(t, VBL_TAKEN);
  } else {
    hw_run(t, CS_NEG);
    cmp16(c, dx, 0xff01);
    if (dx < 0xff01) {
      hw_run(t, VBL_TAKEN);
      park = true;
    }
  }
  if (!park) {
    hw_run(t, CS_WRITE);
    scroll_write(c, t, 0x210d, dx);
    rep30(c);
    set_c(c, true);
    const uint16_t dy =
        sbc16(c, wram_r16(w, W_CAMERA_Y), wram_r16(w, W_BOSS_PLANE_Y));
    c->a = dy;
    hw_run(t, CS_DY);
    cmp16(c, dy, 0x0100);
    if (dy < 0x0100) {
      hw_run(t, VBL_TAKEN);
    } else {
      hw_run(t, CS_NEG);
      cmp16(c, dy, 0xff21);
      if (dy < 0xff21) {
        hw_run(t, VBL_TAKEN);
        park = true;
        PORT_COVER(boss_plane_y_out);
      }
    }
    if (!park) {
      PORT_COVER(boss_plane_shown);
      hw_run(t, CS_WRITE);
      scroll_write(c, t, 0x210e, dy);
      rep30(c);
      hw_run(t, CS_TAIL);
      set_c(c, true);
      return;
    }
  } else {
    PORT_COVER(boss_plane_x_out);
  }
  // Both scrolls to `$0100`, which puts the plane past the picture's edge.
  c->p = (uint8_t)(c->p | PORT_P_M);
  hw_run(t, CS_PARK);
  lda8(c, 0x01);
  hw_w8(t, 0x210d, 0x00);
  hw_run(t, VBL_STORE);
  hw_w8(t, 0x210d, 0x01);
  hw_run(t, VBL_STORE);
  hw_w8(t, 0x210e, 0x00);
  hw_run(t, VBL_STORE);
  hw_w8(t, 0x210e, 0x01);
  rep30(c);
  hw_run(t, CS_TAIL);
  set_c(c, true);
}

void scroll_shadow_job(Wram* w, PortCpu* c, HwTrace* t) {
  for (int i = 0; i < 12; i++) {
    hw_run(t, i == 0 ? SS_FIRST : SS_NEXT);
    const uint8_t v = wram_r8(w, (uint32_t)(W_SCROLL_SHADOW + i));
    lda8(c, v);
    hw_w8(t, (uint16_t)(0x210d + i / 2), v);
  }
  hw_run(t, SS_TAIL);
  set_c(c, true);
}

void boss_bg_dma(Wram* w, PortCpu* c, HwTrace* t) {
  hw_run(t, BB_HEAD);
  lda8(c, 0x80);
  hw_w8(t, 0x2115, 0x80);
  hw_run(t, BB_MODE);
  lda16(c, 0x1801);
  hw_w16(t, 0x4300, 0x1801);
  hw_run(t, BB_COUNT);
  uint16_t x = wram_r16(w, W_BG_DMA_CURSOR);
  c->x = x;
  set_nz16(c, x);
  if (x == 0) {
    PORT_COVER(boss_bg_dma_empty);
    hw_run(t, VBL_TAKEN);
    hw_run(t, BB_EMPTY);
    set_c(c, false);
    return;
  }
  PORT_COVER(boss_bg_dma_sent);
  hw_run(t, BB_FIRST);
  x = (uint16_t)(x - 2);
  for (;;) {
    hw_run(t, VBL_LOAD);
    hw_w16(t, 0x4302, wram_r16(w, W_BG_DMA_SRC + x));
    hw_run(t, VBL_LOAD);
    hw_w16(t, 0x4304, wram_r16(w, W_BG_DMA_BANK + x));
    hw_run(t, VBL_LOAD);
    hw_w16(t, 0x4305, wram_r16(w, W_BG_DMA_LEN + x));
    hw_run(t, VBL_LOAD);
    const uint16_t dest = wram_r16(w, W_BG_DMA_DEST + x);
    lda16(c, dest);
    hw_w16(t, 0x2116, dest);
    dma_go(c, t);
    hw_run(t, BB_NEXT);
    x = (uint16_t)(x - 2);
    set_nz16(c, x);
    if (x & 0x8000u) break;
    hw_run(t, VBL_TAKEN);
  }
  c->x = x;
  hw_run(t, BB_DONE);
  wram_w16(w, W_BG_DMA_CURSOR, 0);
  set_c(c, false);
}

void bg1_vscroll_job(Wram* w, PortCpu* c, HwTrace* t) {
  PORT_COVER(bg1_vscroll_job);
  // `scroll_shadow_job`'s instructions on two of its twelve bytes, and so
  // its runs. The tail is `CLC : REP #$20 : RTL`, which costs what that
  // one's does.
  for (int i = 0; i < 2; i++) {
    hw_run(t, i == 0 ? SS_FIRST : SS_NEXT);
    const uint8_t v = wram_r8(w, (uint32_t)(W_SCROLL_SHADOW + 2 + i));
    lda8(c, v);
    hw_w8(t, 0x210e, v);
  }
  hw_run(t, SS_TAIL);
  set_c(c, false);
}

void vram_send_job(Wram* w, PortCpu* c, HwTrace* t) {
  hw_run(t, VS_MODE);
  hw_w16(t, 0x4300, 0x1801);
  hw_run(t, VS_VMAIN);
  hw_w8(t, 0x2115, 0x80);
  const uint16_t source = wram_r16(w, W_VRAM_SEND_SOURCE);
  hw_run(t, VS_SOURCE);
  c->x = source;
  hw_w16(t, 0x4302, source);
  // A word to `$4304`, so the byte above the bank goes to `$4305`, which
  // the length then overwrites.
  hw_run(t, VS_LOAD);
  hw_w16(t, 0x4304, wram_r16(w, W_VRAM_SEND_BANK));
  const uint16_t at = wram_r16(w, W_VRAM_SEND_AT);
  hw_run(t, VS_LOAD);
  c->y = at;
  hw_w16(t, 0x2116, at);

  const uint16_t left = wram_r16(w, W_VRAM_SEND_LEFT);
  hw_run(t, VS_TEST);
  cmp16(c, left, VRAM_SEND_BYTES);
  if (left <= VRAM_SEND_BYTES) {
    PORT_COVER(vram_send_last);
    if (left == VRAM_SEND_BYTES) hw_run(t, VS_BEQ);
    hw_run(t, VBL_TAKEN);
    hw_run(t, VBL_STORE);
    hw_w16(t, 0x4305, left);
    c->a = left;
    dma_go(c, t);
    wram_w16(w, W_VRAM_SEND_LEFT, 0);
    hw_run(t, VS_DONE);
    set_c(c, false);
    return;
  }

  PORT_COVER(vram_send_more);
  hw_run(t, VS_BEQ);
  wram_w16(w, W_VRAM_SEND_LEFT, sbc16(c, left, VRAM_SEND_BYTES));
  hw_run(t, VS_MORE);
  hw_w16(t, 0x4305, VRAM_SEND_BYTES);
  // The subtraction left carry set, so `ADC #$03FF` is a kilobyte on. VRAM's
  // address is in words, and takes that sum's carry.
  wram_w16(w, W_VRAM_SEND_SOURCE, adc16(c, source, VRAM_SEND_BYTES - 1));
  c->a = adc16(c, at, VRAM_SEND_BYTES / 2);
  wram_w16(w, W_VRAM_SEND_AT, c->a);
  hw_run(t, VS_STEP);
  lda8(c, 0x01);
  hw_w8(t, 0x420b, 0x01);
  hw_run(t, VS_MORE_TAIL);
  set_c(c, true);
}

void mosaic_off_job(Wram* w, PortCpu* c, HwTrace* t) {
  (void)w;
  PORT_COVER(mosaic_off_job);
  // The tail is `REP #$20 : CLC : RTL`, which costs what the scroll
  // shadow's does.
  hw_run(t, MO_HEAD);
  hw_w8(t, 0x2106, 0);
  hw_run(t, SS_TAIL);
  set_c(c, false);
}

void boss_shake_job(Wram* w, PortCpu* c, HwTrace* t) {
  PORT_COVER(boss_shake_job);
  const uint16_t down =
      adc16(c, wram_r16(w, W_FRAME_COUNT) & BOSS_SHAKE_LINES,
            wram_r16(w, W_CAMERA_SUB_Y));
  hw_run(t, SK_FIRST);
  hw_w8(t, 0x2110, (uint8_t)down);
  // `XBA`, for the high byte.
  c->a = (uint16_t)(down << 8 | down >> 8);
  set_nz8(c, (uint8_t)c->a);
  hw_run(t, SK_SECOND);
  hw_w8(t, 0x2110, (uint8_t)c->a);
  // The tail is `REP #$30 : CLC : RTL`, which costs what the scroll
  // shadow's does.
  hw_run(t, SS_TAIL);
  set_c(c, false);
}

bool brightness_up_job(Wram* w, PortCpu* c) {
  c->a = (uint16_t)(wram_r16(w, W_BRIGHTNESS_SHADOW) + 1);
  wram_w16(w, W_BRIGHTNESS_SHADOW, c->a);
  cmp16(c, c->a, BRIGHTNESS_FULL);
  const bool more = c->a != BRIGHTNESS_FULL;
  if (more) {
    PORT_COVER(brightness_up);
  } else {
    PORT_COVER(brightness_full);
  }
  set_c(c, more);
  return more;
}

bool brightness_down_job(Wram* w, PortCpu* c) {
  const uint16_t now = (uint16_t)(wram_r16(w, W_BRIGHTNESS_SHADOW) - 1);
  wram_w16(w, W_BRIGHTNESS_SHADOW, now);
  set_nz16(c, now);
  if (!(now & 0x8000u)) {
    PORT_COVER(brightness_down);
    set_c(c, true);
    return true;
  }
  PORT_COVER(brightness_blanked);
  c->a = BRIGHTNESS_BLANKED;
  set_nz16(c, c->a);
  wram_w16(w, W_BRIGHTNESS_SHADOW, c->a);
  set_c(c, false);
  return false;
}
