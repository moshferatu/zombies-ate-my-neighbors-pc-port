// The survivor radar, every survivor at once.
//
// The radar (the SNES's L or R) is a dimmed box over the top left of the
// level with a yellow square in it for each neighbour still to be rescued.
// The console draws those squares with **one** sprite. The thread behind the
// radar, `$82:D8DB`, gets one display record for its marker (`$82:D9F6`), and
// every second tick moves it to the next neighbour in range:
//
//     $82:D8F6  LDA #$0002 : JSL thread_yield     two ticks
//     $82:D92A  $0A = $0A + 1, wrapping from $7E:6E30 (the count) to 1
//     $82:D93C  LDA $7E605A,X : AND #$0080        rescued or dead: next one
//     $82:D953  dx = player X - $7E:6DF4[i]       |dx| >= $180: next one
//     $82:D96E  dy = player Y - $7E:6DF6[i]       |dy| >= $180: next one
//     $82:D999  marker = ($0C, $0E) -/+ (|dx|, |dy|) / 16
//
// so each square is lit for two ticks in every `2n`, and with five neighbours
// in range each is on a fifth of the time. Smoothed, that is five squares
// taking turns at being there.
//
// `radar_frame` draws them all instead, from the frame hook, into OAM entries
// the game's pass left parked, the way the widened picture's margins are
// drawn (`src/widescreen.h`): for each radar thread, every neighbour the loop
// above would stop on, placed as the loop would place the marker, with the
// marker's own OAM entry as the pattern. The marker itself is parked: one of
// the squares is on its neighbour, and it is where the loop put it up to two
// ticks ago while the square is where it would put it now -- a pixel apart
// once the player has moved, and a doubled square.
//
// Everything is read from the memory the sprites on screen were composed from
// (`ws_sprite_mem`): the thread's own page for its player, its box and the
// marker's record, and the list of neighbours. Nothing of the game's state is
// written. The PPU's OAM is, and the game sends all of it again at every
// vblank; `radar_return` puts back what this wrote over if the game did not.
//
// The squares are in higher entries than the marker's, which the game puts
// first, and a sprite hides every sprite in a higher entry: a neighbour
// standing where a square falls was drawn over it. So each is marked
// `VideoPicture.front`, which a line's sprites and the smoothing's draw list both read as
// "found first", and the squares are in front of everything, as the one
// marker in entry 0 was.
//
// `steady` off (`--flashing-radar`) is the console's radar.
#ifndef ZAMN_RADAR_H
#define ZAMN_RADAR_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "snes.h"
#include "video/console.h"

#include "assets/rom.h"
#include "assets/sprite.h"
#include "port/oam.h"
#include "port/wram.h"

// The radar thread, as the scheduler files it: `W_THREAD_ENTRY` holds the
// entry point less one.
#define RADAR_THREAD_ENTRY 0xd8da
#define RADAR_THREAD_BANK 0x82
// Where each slot's direct page is, 24 words in ROM (`THREAD_DP_TABLE` in
// `src/port/thread.h`).
#define RADAR_THREAD_DP_TABLE 0x8082deu
// On the thread's page: the player (0 or 1), the marker's record, and the
// middle of the box the marker is placed from.
#define RADAR_DP_PLAYER 0x00
#define RADAR_DP_RECORD 0x08
#define RADAR_DP_MIDDLE_X 0x0c
#define RADAR_DP_MIDDLE_Y 0x0e
// Per player: the radar is up (`$82:D8DF INC $1F98,X`). The player's record
// is `W_PLAYER_A_RECORD` or the word after it (`$82:D94F LDA $00D2,Y`).
#define RADAR_UP 0x1f98
// The neighbours: how many, each one's flags (a byte), and where it is.
#define RADAR_COUNT 0x6e30
#define RADAR_FLAGS 0x605a
#define RADAR_GONE 0x80
#define RADAR_POS_X 0x6df4
#define RADAR_POS_Y 0x6df6
// How far a neighbour may be from the player on either axis to be shown.
#define RADAR_REACH 0x0180
// The marker's metasprite (`$82:DA60`), which says a record is the marker.
#define RADAR_META 0xa33b
#define RADAR_META_BANK 0x90

// More than any level has neighbours, for both players' radars, and the
// markers.
#define RADAR_WRITTEN_MAX 64

typedef struct {
  bool steady;  // every square every frame; off is the console's radar
  // The entries written for the last frame and what they held before, for
  // a frame the game sends no OAM for.
  int written;
  int slot[RADAR_WRITTEN_MAX];
  uint16_t was_lo[RADAR_WRITTEN_MAX], was_word[RADAR_WRITTEN_MAX];
  uint8_t was_high[RADAR_WRITTEN_MAX];
  // The whole of OAM as the last frame left it. Any entry changed since is
  // the game having sent its own, all of them.
  uint16_t oam[OAM_ENTRIES * 2];
  uint8_t high[OAM_ENTRIES / 4];
  // For the test: frames with a radar up, squares drawn, and the most on
  // one frame.
  long frames, squares;
  int most;
} Radar;

static inline uint16_t radar_r16(const uint8_t* mem, uint32_t off) {
  return (uint16_t)(mem[off & 0x1ffff] | (mem[(off + 1) & 0x1ffff] << 8));
}

// Write one OAM entry, remembering what it held. False when there is no room
// left to remember it in, and then nothing is written.
static inline bool radar_put(Snes* snes, Radar* r, int s, int x, int y, uint16_t word, bool large) {
  if (r->written == RADAR_WRITTEN_MAX) return false;
  const VideoRegisters* reg = &video_chip_of(snes)->registers;
  const int i = r->written++;
  r->slot[i] = s;
  r->was_lo[i] = reg->oam[s * 2];
  r->was_word[i] = reg->oam[s * 2 + 1];
  r->was_high[i] = (uint8_t)((reg->high_oam[s >> 2] >> ((s & 3) * 2)) & 3);
  video_set_sprite(video_chip_of(snes), s, x, y, word, large);
  return true;
}

// Before anything else touches the OAM this frame: if the game sent none
// since the last, put back what the last one wrote over. Last written first,
// so an entry written twice ends as it began. (Not entry by entry: a square
// the game's own marker has since landed on is the game's.)
static inline void radar_return(Snes* snes, Radar* r) {
  VideoChip* chip = video_chip_of(snes);
  const VideoRegisters* reg = &chip->registers;
  for (int i = 0; i < r->written; i++) video_set_sprite_front(chip, r->slot[i], false);
  if (r->written && !memcmp(reg->oam, r->oam, sizeof r->oam) &&
      !memcmp(reg->high_oam, r->high, sizeof r->high)) {
    for (int i = r->written - 1; i >= 0; i--) {
      const int s = r->slot[i];
      video_set_sprite(chip, s, (r->was_lo[i] & 0xff) | (r->was_high[i] & 1) << 8, r->was_lo[i] >> 8,
                       r->was_word[i], (r->was_high[i] & 2) != 0);
    }
  }
  r->written = 0;
}

// ...and after this frame's are written, the OAM it leaves.
static inline void radar_keep(const Snes* snes, Radar* r) {
  const VideoRegisters* reg = &video_chip_of(snes)->registers;
  memcpy(r->oam, reg->oam, sizeof r->oam);
  memcpy(r->high, reg->high_oam, sizeof r->high);
}

// One radar's squares, for the thread on page `dp`. `place` is where the
// widened picture puts a sprite laid out over the panel.
static inline void radar_draw(Snes* snes, Radar* r, const Rom* rom, const uint8_t* mem,
                              uint16_t dp, VideoSpritePlace place, int* shown) {
  const VideoRegisters* reg = &video_chip_of(snes)->registers;
  const uint16_t player = radar_r16(mem, dp + RADAR_DP_PLAYER);
  if (player > 1 || !radar_r16(mem, RADAR_UP + player * 2u)) return;
  const uint16_t rec = radar_r16(mem, dp + RADAR_DP_RECORD);
  if (rec < W_ACTOR_SLOTS || rec >= W_ACTOR_SLOTS + ACTOR_SLOT_COUNT * ACTOR_SLOT_STRIDE) return;
  const uint16_t flags = radar_r16(mem, rec + ACTOR_FLAGS);
  if ((flags & (ACTOR_DRAW | ACTOR_SCREEN_SPACE)) != (ACTOR_DRAW | ACTOR_SCREEN_SPACE) ||
      (flags & (ACTOR_FLIP | ACTOR_ATTR_SET)) || radar_r16(mem, rec + ACTOR_META) != RADAR_META ||
      radar_r16(mem, rec + ACTOR_META_BANK) != RADAR_META_BANK)
    return;
  SpriteMeta meta;
  if (sprite_meta_read(rom, ((uint32_t)RADAR_META_BANK << 16) | RADAR_META, &meta) != SPRITE_OK ||
      meta.count != 1)
    return;
  const SpritePiece* p = &meta.pieces[0];

  // The pattern: the marker's own entry, told by its look -- its attributes,
  // and the tile its frame is cached at, which no other sprite draws from.
  // Not by where it is: the memory can be a tick on from the OAM when the
  // pass's own is not to hand. None while the marker is parked at -32 before
  // the first neighbour is found, and then there is nothing to draw either.
  const uint16_t cached = radar_r16(mem, W_FRAME_SLOT + p->frame * 2u);
  if (cached & 0x8000) return;
  const uint16_t tile = sprite_slot_tile(cached / 2);
  const uint16_t prio = (flags & ACTOR_PRIORITY_TOP) ? 0x3000 : 0x2000;
  const uint16_t look = (uint16_t)(((p->attr | prio) & 0xfe00) | tile);
  int entry = -1;
  for (int e = 0; e < OAM_ENTRIES && entry < 0; e++) {
    const int y = reg->oam[e * 2] >> 8;
    if ((y < 0xe0 || y > 0xf0) && reg->oam[e * 2 + 1] == look) entry = e;
  }
  if (entry < 0) return;
  const bool large = (reg->high_oam[entry >> 2] >> ((entry & 3) * 2 + 1)) & 1;
  if (!radar_put(snes, r, entry, 0, 0xe0, look, large)) return;

  const uint16_t prec = radar_r16(mem, W_PLAYER_A_RECORD + player * 2u);
  const uint16_t px = radar_r16(mem, prec + ACTOR_X), py = radar_r16(mem, prec + ACTOR_Y);
  const int16_t middle_x = (int16_t)radar_r16(mem, dp + RADAR_DP_MIDDLE_X);
  const int16_t middle_y = (int16_t)radar_r16(mem, dp + RADAR_DP_MIDDLE_Y);
  const uint16_t count = radar_r16(mem, RADAR_COUNT);
  // Not in the marker's own entry, parked just now: the pass's owner table
  // still says that entry is the marker's record, and the smoothing moves
  // a record's sprites by the record's move. A square there was slid along
  // the marker's hop -- where the hop was short enough to be taken for a
  // move, between two neighbours close together, a square going back and
  // forth, the others still.
  int slot = entry + 1;
  for (uint16_t i = 0; i < count && i < 64; i++) {
    if (mem[RADAR_FLAGS + i] & RADAR_GONE) continue;
    // `SEC : SBC`, and `BPL` on the result: the sign is the 16-bit word's.
    const int16_t dx = (int16_t)(px - radar_r16(mem, RADAR_POS_X + i * 4u));
    const int16_t dy = (int16_t)(py - radar_r16(mem, RADAR_POS_Y + i * 4u));
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax >= RADAR_REACH || ay >= RADAR_REACH) continue;
    const int x = middle_x + (dx < 0 ? ax >> 4 : -(ax >> 4));
    const int y = middle_y + (dy < 0 ? ay >> 4 : -(ay >> 4));
    slot = video_free_sprite(&video_chip_of(snes)->registers, slot);
    if (slot >= OAM_ENTRIES) return;
    if (!radar_put(snes, r, slot, (x + p->x) & 0x1ff, (y + p->y) & 0xff, look, large)) return;
    video_set_sprite_place(video_chip_of(snes), slot, place);
    video_set_sprite_front(video_chip_of(snes), slot, true);
    (*shown)++;
  }
}

// From the frame hook, once the sprites of the picture are placed. `mem` is
// the memory they were composed from.
static inline void radar_frame(Snes* snes, Radar* r, const Rom* rom, const uint8_t* mem,
                               VideoSpritePlace place) {
  if (!r->steady) return;
  int shown = 0;
  bool any = false;
  for (int s = 0; s < WRAM_THREAD_SLOTS; s++) {
    if (!(radar_r16(mem, W_THREAD_WAIT + s * 2u) & 0x8000)) continue;
    if (radar_r16(mem, W_THREAD_ENTRY + s * 2u) != RADAR_THREAD_ENTRY ||
        (radar_r16(mem, W_THREAD_ENTRY_BANK + s * 2u) & 0xff) != RADAR_THREAD_BANK)
      continue;
    const uint16_t dp = rom_word(rom, RADAR_THREAD_DP_TABLE + (uint32_t)s * 2);
    any = true;
    radar_draw(snes, r, rom, mem, dp, place, &shown);
  }
  if (r->written) radar_keep(snes, r);
  if (!any) return;
  r->frames++;
  r->squares += shown;
  if (shown > r->most) r->most = shown;
}

#endif
