// What this game, specifically, wants done with a widened picture.
//
// The PPU knows how to draw margins and what the treatments of one are
// (`ppu_wideStretch`, `ppu_wideAnchor`, `ppu_wideClampEdge`, `ppu_wideClip`);
// it does not know which layer of *Zombies Ate My Neighbors* deserves which,
// or where this game's world stops. Those are facts about the game, and this
// header is the only place they are written down.
//
// There are seven of them.
//
// ## The status panel belongs on the edges
//
// In a level BG3 carries both players' panels -- health, weapon, item, score --
// laid out as player 1 in the left sixteen tilemap columns and player 2 in the
// right sixteen (`src/port/hud.h`). That is already two half-width halves, so
// widening should move them apart rather than stretch or repeat them, which is
// `ppu_wideAnchor` and needs no change to the shadow tilemap the co-simulation
// checks byte for byte.
//
// The same layer carries the title, the password screen and the story cards,
// and those must not be torn in half and flung at the edges. What tells the two
// apart is the layer next door: a level scrolls, so `$80:9E5C` sets BG2's
// tilemap to 64 tiles across, and a fixed screen does not. No symbol, no WRAM
// read, and no list of screens to fall out of date as more of the game is
// ported.
//
// **A tilemap's width outlives the screen that asked for it**, though, and that
// is half the test rather than all of it. `$80:9E5C` runs when a level loads;
// nothing puts BG2SC back to 32 when the level ends, so the card that tallies a
// finished level and the card that names the next one are drawn with a 64-column
// BG2 still configured behind them, and asking only about the width calls them
// levels. They were: every fixed screen up to the first level came out centred
// and every card after one was jammed against the left edge of the picture,
// because a fixed screen leaves `$1B6A` at zero, and at camera x zero the
// sliding margins below hand the left margin's whole share to the right one.
//
// The other half is that a fixed screen also switches BG2 *off the main screen*
// (`$212C`), and a level leaves it on for as long as the level lasts -- through
// the map screen, through a boss, through both players, through a bonus room.
// Measured over all 49 movies in the corpus: BG2 is wide-but-unshown in exactly
// the two that finish a level (`level21-exit`, one crossing; `level24-carry`,
// three) and in neither of them for a frame that is not a card. So the question
// is asked of both registers, and the answer is about the picture being drawn
// rather than about a register left over from the last one.
//
// ## The world only reaches so far, and the game only draws 256 of it
//
// BG2's tilemap in VRAM is a 64-column ring and the camera shows 32 of it, but
// the streamer only ever *maintains* those 32: it writes one fresh column at
// the leading edge each time the camera crosses an eight-pixel boundary and
// never touches the rest. A ring slot therefore holds the right map column only
// where the camera has already been -- correct behind it, stale ahead of it --
// which is why a level that scrolls steadily one way showed a band of leftover
// tiles at its leading edge and a level whose camera wanders did not.
//
// Nothing about that is fixable by looking at it harder: those columns have
// never been written. So they get written here, from the same map, by the same
// rule the ROM's own column copy uses (`tilemap_copy_column` in
// `src/port/camera.c`) -- and into ring slots the game does not read, does not
// write, and will overwrite with exactly these values if the camera ever
// carries them into view. That is what makes this safe to do from outside: it
// is not a change to the game's tilemap, it is the rest of the tilemap.
//
// Where the margin runs off the end of the map there is no column to copy, and
// the answer is not to black it out but to stop insisting the two margins be
// equal — see the sliding margins below. `ppu_setWideClamp` is only the
// backstop for a map narrower than the whole picture.
//
// ## Sprites are world things in a level and stagecraft outside one
//
// The card that names a level slides its letters in from beyond the console's
// 256 and trusts that edge to hide them until they arrive. Widen the picture
// and the trick shows: the same words a second time at both edges, which reads
// exactly like a tilemap being wrapped and is nothing of the kind — that screen
// has one background layer enabled and its tilemap is a single repeated tile.
// So objects get `ppu_wideClip` outside a level and `ppu_wideStretch` inside
// one, on the same test as the other two.
//
// (The card that announces how much worse this level is than the last one does
// the same thing with a background instead, and that one is not settled here:
// `ppu_wideAuto` clips it, because it is a 256-pixel tilemap the game never
// scrolls sideways. See the note on the enum in `ppu.h`.)
//
// ## ...and in a level, the game throws away the ones in the margins
//
// This is the one that cannot be fixed by choosing a policy, because by the
// time the picture is drawn the sprites are already gone.
//
// The cull the ROM runs first (`actor_cull`, `$80:BCE2`) is generous: it keeps
// every record within 128 pixels behind the camera and 383 ahead of it, which
// is far more than a 43-pixel margin needs, and it is not the problem. The
// problem is the last thing that happens to a record. `sprite_emit`
// (`$80:BA51` and its three flipped twins) works out where each 16x16 piece
// lands and then does this:
//
//     CMP #$0100 : BCC keep      ; on screen
//     CMP #$FFF1 : BCC drop      ; ...or off it, and there is no third case
//
// A piece whose screen X is 256 or more, or 16 or more to the left of zero, is
// dropped on the floor. Not parked, not clipped — never written to OAM at all,
// because the console cannot show it and OAM is 128 entries and the game has
// better uses for them. That is exactly the band widening turns into picture,
// and it is why a zombie vanishes a body's width before the edge of a
// widescreen frame while walking about quite happily in the game's own memory.
//
// So `ws_margin_sprites` puts them back. It reads the same visible list the
// game's own pass read, the same records, the same metasprites and the same
// VRAM cache, applies the same composition, and keeps the pieces the ROM
// dropped for being outside the console's 256 and inside the widened picture.
// They go into OAM entries the game's pass left parked, so nothing it placed
// moves, and not one byte of the game's memory is written: this is the frame
// the game has already built, plus the entries it could not afford.
//
// ## The graphics for those sprites are not loaded, because nothing asked for
// ## them
//
// A 16x16 frame is only in VRAM if something has drawn it: `sprite_emit` looks
// the frame up as it emits a piece (`$80:B9D6`), and that lookup is what
// uploads it. A piece dropped for being off the console's edge never reaches
// the lookup, so an actor walking off the side of the screen stops refreshing
// the frames of whatever parts of it are already past the edge, and the LRU
// reclaims them a few frames later. Put the sprite back without its graphics
// and you get exactly what this looked like: survivors and pickups that lose
// half of themselves at the margin, and flicker as slots come and go.
//
// `ws_lend_slot` fixes that by borrowing a slot, uploading the frame's 128
// bytes from ROM into it exactly as `$80:B960`'s DMA would have, and pointing
// the margin sprite at it. Which slot can be borrowed is the whole question,
// and there are two answers.
//
// A slot whose `slot_frame` entry is still negative is one the game has never
// allocated: no frame maps to it, so nothing the game can emit points at it,
// and it is free outright. Early in a level there are dozens of those. But the
// cache only ever fills — a slot goes from unused to used and never back —
// so after a few minutes there are none, which is why a long level was where
// the flicker got worse.
//
// The second answer is what makes it hold up: a slot whose graphics no sprite
// in *this frame's* OAM is drawing from is free for exactly the length of this
// picture. The OAM being drawn is sitting right there to be read, and every
// sprite in it is a whole 16x16 frame, so the slots it reads from are known
// exactly. Such a slot is borrowed for one frame and given back at the top of
// the next one — `ws_return_slots` puts into it whatever the game's own cache
// map says belongs there, read back out of ROM — before a single line of that
// frame is drawn.
//
// Either way the game's own tables are never written. It is not told that a
// slot has changed, because by the time it could look, it has not.
//
// ## The things on the ground are not there yet, or not any more
//
// A dropped piece is at least a record the game still has. This one is not.
//
// Everything a level leaves lying about -- the pickups, the keys, the weapons
// and the potions, thirty kinds of them and every one a single 16x16 -- is an
// entry in the level's object list and only sometimes an actor.
// `object_spawner_body` (`$80:C8F6`) walks that list every fourth tick and
// measures each entry against the middle of the camera's window,
// `($1B6A + $80, $1B6C + $70)`:
//
//     LDA $7E6D02,X : SEC : SBC $0E : (abs) : CMP #$0090 : BCS out
//
// Inside $90 of the middle on both axes it calls `object_spawn` and the object
// gets an actor record; outside, `$80:CAA8` takes the record away again. On the
// horizontal that window is the console's 256 plus sixteen pixels either side
// -- exactly one sprite's width of slack, which is precisely enough for a
// pickup to be gone by the time the last of it leaves the console and not one
// pixel more. Widen the picture by 43 and it pops out of existence with a body
// still showing, and pops back in at the other edge just as abruptly.
//
// That is the difference the monsters do not have, and it is why a zombie walks
// calmly off the side of a widened frame while the first-aid kit beside him
// blinks out of existence: monsters are spawned by their own threads and roam,
// and only the things a level put down live and die by the camera.
//
// It cannot be fixed the way the dropped pieces were, because there is no
// record to read. It does not need to be. The list itself is still there and
// still complete -- `W_OBJECT_X`, `W_OBJECT_Y`, `W_OBJECT_TYPE` and
// `W_OBJECT_STATE` in `port/wram.h` -- and so is the table `object_spawn`
// builds the record from, so `ws_object_sprites` draws the object from the two
// of them directly: an entry with no record, at the position the list gives it,
// with the metasprite `$80:CA6C` names for its type. When the camera does reach
// it, `object_spawn` puts the record at that same position out of that same
// list, so this is not a guess at where the thing would be. It is where it is.
//
// The state word is what keeps this honest. Zero is an object with no record,
// and the only kind drawn here. A record offset means it is already an actor
// and the pass above has it. `$8000` means it has been picked up or rescued and
// is never coming back, and `$C000` is the end of the list. Nothing else is
// consulted and nothing is remembered between frames: an object that stops
// being listed stops being drawn, the same tick.
//
// ## The neighbours go the same way, and a copy of one cannot be made to live
//
// The people are on the same plan as the pickups and a different list. A thread
// at `$81:81F6` walks the level's neighbour list against the same middle of the
// camera's window and the same kind of test, `CMP #$00A0` rather than `#$0090`,
// so the slack is 32 pixels either side of the console's 256 instead of 16:
//
//     LDA ($0C),Y : SEC : SBC $1A : (abs) : CMP #$00A0 : BCS out
//
// Inside it, `$81:81A2` starts a thread from the list's last two fields and the
// thread makes its own actor; outside it, `$81:8276` kills the thread, and the
// record goes with it. Thirty-two pixels is again just enough for the console
// and nothing like enough for a margin: a cheerleader standing in the widened
// left edge has a good half of herself still showing when the camera nudges
// right and takes her away.
//
// The pickups' answer will not work here. An object is a row in a table and a
// type, and `object_spawn` puts the same metasprite at the same coordinates
// every time, so drawing one is reading. A neighbour is a *thread*: what she
// looks like is whatever her thread had reached in whatever script it runs, and
// that dies with it. There is no table to go back to.
//
// Keeping the last picture of her instead is easy and it is not enough. She
// stops moving the moment the game lets go, and if the player stops walking she
// stops for ever; guessing the rest of her loop from the poses she was seen in
// gets her breathing again but not doing anything she was going to do; and none
// of it touches the far end at all, because when the camera comes back the
// spawner starts her thread again and the thread starts its script from the
// top, a pose or two from wherever the copy had got to. That reset is in the
// unmodified game too. What is not in the unmodified game is being able to see
// it: at 256 columns it happens 32 pixels off the side of the console.
//
// Which is the whole shape of the problem, and says what to do about it. The
// window is not wrong; the window is 256 pixels wide because the picture was.
// So this one is not drawn from outside the game at all. `ws_widen_window`
// writes `#$00A0 + 2 * margin` over that immediate, and the spawner goes on
// doing exactly what it always did -- one comparison, against the picture that
// is actually being drawn. She is spawned before she reaches the edge of it and
// taken away 32 pixels past the other one, which is the relationship the stock
// game has to its own edge, to the pixel. Her thread runs the whole time she is
// in view, so she animates because she is animating: nothing here follows her
// poses, remembers them, or replays them.
//
// This is the one thing in this file that changes what the game does rather
// than what it draws, and it is worth being plain about the cost. A neighbour
// in the margin is now a neighbour: she can be rescued out there, and a monster
// standing next to her can reach her out there, where on a console she would
// have been lifted out of the world and been safe until the camera came back.
// That is the same fact as her being visible and animated, seen from the other
// side, and there is no version of one without the other.
//
// She also holds an actor slot for longer. `$80:825E` already returns
// empty-handed when there is no slot and `$81:81A2` already gives up quietly
// when it does, so the failure mode there is the one the game shipped with.
//
// Nothing else is written, and the pickups' `#$0090` in particular is left
// alone: the same byte would work there and would buy nothing, because a pickup
// can already be drawn from its list exactly, and changing the game to put a
// record behind it would only be changing the game. The vertical half of the
// neighbours' own test at `$81:8250` keeps its `#$00A0` as well, no rows having
// been added. At margin zero the stock figure goes back and the game is the game
// again, which is what makes `--widescreen off` still byte-identical; and the
// co-simulation never sees any of it, because it does not include this file.
//
// ## Everything above happens one tick late
//
// The frame the console is about to draw was composed during the *previous*
// tick: the game builds an OAM buffer and a tilemap upload queue as it runs,
// and the NMI at the top of the next frame DMAs both into the hardware and only
// then lets the game run again. So at the moment the margins are drawn, the
// OAM and VRAM on screen are one tick older than the actor records, the camera
// and the sprite cache in WRAM.
//
// Reading the live memory therefore composes the margins from a world that has
// already moved: pieces land one frame's motion away from the same actor's
// on-screen pieces, and near an eight-pixel boundary the tilemap columns go to
// the wrong ring slots. Both show up as edge flicker whenever the camera moves.
// So `Widescreen` keeps a copy of WRAM as it stood at the previous frame start,
// and everything here reads that. It is a lot of memory to copy and it is
// exactly the memory the picture was made from.

#ifndef ZAMN_WIDESCREEN_H
#define ZAMN_WIDESCREEN_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ppu.h"
#include "snes.h"

#include "assets/rom.h"
#include "assets/sprite.h"
#include "port/camera.h"
#include "port/oam.h"
#include "port/wram.h"

// How many distinct frames one widened picture may borrow cache slots for.
// Measured over nine movies and 13,869 frames of play: the busiest picture puts
// twelve pieces back and borrows for five of them, so this is room to spare
// rather than a limit anything is expected to reach.
#define WS_LENT_MAX 24

// The metasprite `object_spawn` (`$80:C9E3`) gives an object of each type, as
// `LDA $1F0A,X : TAX : LDA $CA6C,X` reads it -- 30 words, indexed by a type
// already doubled, and every one of them a pointer into bank `$8F`. The table
// next door at `$80:CA30` is the collide id, which a picture does not need.
#define OBJECT_META_TABLE 0x80ca6cu
#define OBJECT_TYPE_COUNT 30

// `CMP #$00A0` at `$81:823C`, in the ROM file: bank `$81` is LoROM offset
// `$08000`, so the operand of that compare is two bytes at `$0823D`. See the
// header for what it is and why this is the one thing here that is written.
#define WS_WINDOW_OPCODE 0x0823cu
#define WS_WINDOW_OPERAND 0x0823du
#define WS_WINDOW_STOCK 0x00a0

// Everything the hook needs: where the ROM is, because metasprites and sprite
// graphics are read from it, how wide the margins are, the memory the picture
// on screen was composed from, and the cache slots borrowed for it.
typedef struct {
  Rom rom;
  int margin;  // game pixels per side, 0 when widescreen is off
  uint8_t mem[0x20000];
  bool have_mem;
  uint16_t lent_frame[WS_LENT_MAX];
  int lent_slot[WS_LENT_MAX];
  int lent_count;
  int back_slot[WS_LENT_MAX];  // ...of those, the ones that have to be put back
  int back_count;
  uint8_t slot_drawn[SPRITE_SLOTS];  // slots this frame's own sprites read from
} Widescreen;

// WRAM as a flat 128 KB, the way `src/port/wram.h` numbers it: bank `$7E` is
// `$00000-$0FFFF` and bank `$7F` is `$10000-$1FFFF`.
static inline uint16_t ws_r16(const uint8_t* mem, uint32_t off) {
  return (uint16_t)(mem[off & 0x1ffff] | (mem[(off + 1) & 0x1ffff] << 8));
}

// `EOR #$FFFF : SEC : SBC #$000F`, the mirror the flipped emitters apply to a
// piece offset. Same as `mirror()` in `src/assets/sprite.c`, which is static.
static inline uint16_t ws_mirror(uint16_t v) {
  return (uint16_t)(((uint16_t)~v) - 0x000f);
}

// Put a 16x16 frame into a cache slot's VRAM, which is what the DMA `$80:B960`
// queues would have done: the first 64 bytes are the slot's two top tiles and
// the second 64 the two below them, one VRAM row of 32 words further on.
static inline bool ws_upload(Snes* snes, const Rom* rom, uint16_t frame,
                             int slot) {
  uint8_t raw[SPRITE_FRAME_BYTES];
  if (!sprite_frame_read(rom, frame, raw)) return false;
  const uint16_t base = sprite_slot_vram(slot);
  for (int i = 0; i < 32; i++) {
    snes_writeVramWord(snes, (uint16_t)(base + i),
                       (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8)));
    snes_writeVramWord(snes, (uint16_t)(base + 0x100 + i),
                       (uint16_t)(raw[64 + i * 2] | (raw[65 + i * 2] << 8)));
  }
  return true;
}

// Give back the slots the last picture borrowed, by putting into each one the
// graphics the game's own cache map says belongs there. The live map and not
// the snapshot: a slot the game has re-let since has already had its own upload
// DMA'd into it, and the newest owner is the one whose graphics belong there.
static inline void ws_return_slots(Snes* snes, Widescreen* ws) {
  for (int i = 0; i < ws->back_count; i++) {
    const uint16_t f = ws_r16(snes->ram, W_SLOT_FRAME + ws->back_slot[i] * 2);
    if (!(f & 0x8000))
      ws_upload(snes, &ws->rom, (uint16_t)(f / 2), ws->back_slot[i]);
  }
  ws->back_count = 0;
}

// The OAM tile word for `frame`, putting it into a cache slot nothing else is
// reading from if it is not already in one. -1 if there is nowhere to put it.
//
// Two kinds of slot will do, in that order. One the game has never allocated —
// `slot_frame` still negative — is free outright: no frame maps to it, so
// nothing the game can emit points at it. Failing that, one whose graphics no
// sprite in *this* frame's OAM is drawing from is free for the length of this
// picture, and is put back at the top of the next one before anything is drawn.
// A level that has been running for a while has none of the first kind left,
// because the cache only ever fills, and that is when the second kind matters.
//
// Either way the game's own tables are not touched: it is not told that a slot
// has changed, because by the time it could look, it has not.
static inline int ws_lend_slot(Snes* snes, Widescreen* ws, uint16_t frame) {
  const uint16_t entry = ws_r16(ws->mem, W_FRAME_SLOT + (uint32_t)frame * 2);
  if (!(entry & 0x8000)) return sprite_slot_tile(entry / 2);

  for (int i = 0; i < ws->lent_count; i++)
    if (ws->lent_frame[i] == frame) return sprite_slot_tile(ws->lent_slot[i]);
  if (ws->lent_count == WS_LENT_MAX) return -1;

  for (int pass = 0; pass < 2; pass++) {
    for (int slot = 0; slot < SPRITE_SLOTS; slot++) {
      const bool mapped =
          !(ws_r16(snes->ram, W_SLOT_FRAME + slot * 2) & 0x8000);
      if (pass == 0 ? mapped : (!mapped || ws->slot_drawn[slot])) continue;
      bool taken = false;
      for (int i = 0; i < ws->lent_count; i++) taken |= ws->lent_slot[i] == slot;
      if (taken) continue;
      if (!ws_upload(snes, &ws->rom, frame, slot)) return -1;

      if (pass == 1) ws->back_slot[ws->back_count++] = slot;
      ws->lent_frame[ws->lent_count] = frame;
      ws->lent_slot[ws->lent_count] = slot;
      ws->lent_count++;
      return sprite_slot_tile(slot);
    }
  }
  return -1;
}

// One metasprite's pieces, composed exactly as `sprite_emit` composes them and
// placed in the OAM entries the game's own pass left parked, for the pieces
// that fall outside the console's 256 and inside the widened picture. Returns
// the next free entry.
//
// The one test worth reading twice is the one that decides which pieces those
// are. `x > -16 && x < 256` is the console's own picture: a 16-wide piece with
// any part of it in there. For a record the game drew, that is precisely the
// `CMP #$0100 / CMP #$FFF1` pair the emitters keep a piece on, so skipping it
// leaves exactly the pieces the ROM dropped. For something the game has no
// record of -- an object the spawner has not reached yet -- the same test
// means something different and just as necessary: nothing drawn from outside
// the game may put a pixel where the console puts one, because there the
// console is right and this is not.
static inline int ws_emit_meta(Snes* snes, Widescreen* ws, int slot,
                               const SpriteMeta* meta, int16_t ox, int16_t oy,
                               uint16_t attr_or, uint16_t attr_and, bool flip_x,
                               bool flip_y, int left, int right) {
  const uint16_t flip_eor =
      (uint16_t)((flip_x ? 0x4000 : 0) | (flip_y ? 0x8000 : 0));

  for (int i = 0; i < meta->count && slot < OAM_ENTRIES; i++) {
    const SpritePiece* p = &meta->pieces[i];

    uint16_t sy = (uint16_t)p->y;
    if (flip_y) sy = ws_mirror(sy);
    sy = (uint16_t)(sy + (uint16_t)oy);
    // The ROM's Y test, unchanged: nothing is added above or below, because
    // nothing was added above or below.
    if (sy >= 0x00e0 && sy < 0xfff1) continue;

    uint16_t sx = (uint16_t)p->x;
    if (flip_x) sx = ws_mirror(sx);
    sx = (uint16_t)(sx + (uint16_t)ox);
    const int x = (int16_t)sx;
    if (x > -16 && x < 256) continue;
    // A 16-wide piece at `x` covers `x..x+15`, so it is worth drawing while any
    // of that is inside the widened picture.
    if (x >= 0 ? x > 255 + right : x < -15 - left) continue;

    const int tile = ws_lend_slot(snes, ws, p->frame);
    if (tile < 0) continue;
    const uint16_t word =
        (uint16_t)(((uint16_t)tile | (p->attr & attr_and) | attr_or) ^ flip_eor);
    snes_setSprite(snes, slot, x & 0x1ff, sy & 0xff, word, true);
    slot = snes_freeSprite(snes, slot + 1);
  }
  return slot;
}

// The objects the level put on the ground that have no actor record right now,
// drawn from the four arrays the spawner reads and the metasprite table it
// spawns them with. See the header, and `W_OBJECT_STATE` in `port/wram.h` for
// what the states mean.
static inline int ws_object_sprites(Snes* snes, Widescreen* ws, int slot,
                                    int left, int right) {
  const uint8_t* mem = ws->mem;
  const uint16_t cam_x = ws_r16(mem, W_CAMERA_X);
  const uint16_t cam_y = ws_r16(mem, W_CAMERA_Y);

  for (int i = 0; i < OBJECT_SLOT_COUNT && slot < OAM_ENTRIES; i++) {
    // `$80:C934 BIT $1EC4,X : BVS done : BMI skip`, in that order and for the
    // same reasons: past the end of the list there is nothing, and an object
    // already picked up is not coming back.
    const uint16_t state = ws_r16(mem, W_OBJECT_STATE + (uint32_t)i * 2);
    if (state & 0x4000) break;
    if (state) continue;  // gone for good, or holding a record already drawn

    const uint16_t type = ws_r16(mem, W_OBJECT_TYPE + (uint32_t)i * 2);
    if (type >= OBJECT_TYPE_COUNT * 2 || (type & 1)) continue;
    SpriteMeta meta;
    if (sprite_meta_read(&ws->rom,
                         ((uint32_t)SPRITE_META_BANK_LO << 16) |
                             rom_word(&ws->rom, OBJECT_META_TABLE + type),
                         &meta) != SPRITE_OK)
      continue;

    // `object_spawn` writes the record it allocates from these three and
    // nothing else: X and Y straight out of the list, Z zero, and the only
    // flag it sets is `ACTOR_DRAW`. So there is no flip, no forced palette and
    // no raised priority to work out -- an object is drawn one way.
    slot = ws_emit_meta(
        snes, ws, slot, &meta,
        (int16_t)(ws_r16(mem, W_OBJECT_X + (uint32_t)i * 2) - cam_x),
        (int16_t)(ws_r16(mem, W_OBJECT_Y + (uint32_t)i * 2) - cam_y), 0x2000,
        0xffff, false, false, left, right);
  }
  return slot;
}

// The pieces `sprite_emit` dropped for being outside the console's 256, drawn
// into the OAM entries the game's own pass left parked. See the header.
static inline void ws_margin_sprites(Snes* snes, Widescreen* ws, int left,
                                     int right) {
  const uint8_t* mem = ws->mem;
  const uint16_t count = ws_r16(mem, W_VISIBLE_ACTOR_COUNT);
  const uint16_t cam_x = ws_r16(mem, W_CAMERA_X);
  const uint16_t cam_y = ws_r16(mem, W_CAMERA_Y);
  int slot = snes_freeSprite(snes, 0);
  ws->lent_count = 0;

  // Which cache slots the picture already on its way to the screen is reading
  // from. Every sprite the game emits is a whole 16x16 frame, so an entry's
  // tile number is a slot's tile number and the map back is exact.
  memset(ws->slot_drawn, 0, sizeof ws->slot_drawn);
  for (int e = 0; e < OAM_ENTRIES; e++) {
    // Parked is `$E0` exactly, and the emitters can reach neither it nor the
    // sixteen rows below it; `$F1` upwards is a sprite hanging off the top of
    // the screen, which is drawn.
    const int y = snes->ppu->oam[e * 2] >> 8;
    if (y >= 0xe0 && y <= 0xf0) continue;
    const int tile = snes->ppu->oam[e * 2 + 1] & 0x1ff;
    const int s = (tile / 32) * SPRITE_SLOTS_PER_ROW + (tile % 32) / 2;
    if (s < SPRITE_SLOTS) ws->slot_drawn[s] = 1;
  }

  for (uint16_t cur = 0; cur < count && slot < OAM_ENTRIES; cur += 2) {
    const uint16_t rec = ws_r16(mem, W_VISIBLE_ACTORS + cur);
    const uint16_t flags = ws_r16(mem, (uint32_t)rec + ACTOR_FLAGS);
    if (!(flags & ACTOR_DRAW)) continue;

    // `draw_args` in `src/port/oam.c`, which is `$80:BD46`..`$80:BD9F`.
    uint16_t attr_or = (flags & ACTOR_PRIORITY_TOP) ? 0x3000 : 0x2000;
    uint16_t attr_and = 0xffff;
    if (flags & ACTOR_ATTR_SET) {
      attr_or |= ws_r16(mem, (uint32_t)rec + ACTOR_ATTR);
      attr_and = 0xf1ff;
    }
    int16_t ox, oy;
    if (flags & ACTOR_SCREEN_SPACE) {
      ox = (int16_t)ws_r16(mem, (uint32_t)rec + ACTOR_X);
      oy = (int16_t)ws_r16(mem, (uint32_t)rec + ACTOR_Y);
    } else {
      ox = (int16_t)(ws_r16(mem, (uint32_t)rec + ACTOR_X) - cam_x);
      oy = (int16_t)(ws_r16(mem, (uint32_t)rec + ACTOR_Y) -
                     ws_r16(mem, (uint32_t)rec + ACTOR_Z) - cam_y);
    }
    const uint16_t ptr = ws_r16(mem, (uint32_t)rec + ACTOR_META);
    if (ptr < 0x8000) continue;
    const uint16_t bank = ws_r16(mem, (uint32_t)rec + ACTOR_META_BANK);
    if (bank < SPRITE_META_BANK_LO || bank > SPRITE_META_BANK_HI) continue;
    SpriteMeta meta;
    if (sprite_meta_read(&ws->rom, ((uint32_t)bank << 16) | ptr, &meta) !=
        SPRITE_OK)
      continue;

    slot = ws_emit_meta(snes, ws, slot, &meta, ox, oy, attr_or, attr_and,
                        (flags & SPRITE_FLIP_X) != 0,
                        (flags & SPRITE_FLIP_Y) != 0, left, right);
  }

  // ...and then the ones with no record to have been dropped from. Last because
  // they are the only ones drawn from a list rather than from a record, so if
  // OAM runs out it is these that go without.
  ws_object_sprites(snes, ws, slot, left, right);
}

// The sprites of a screen-space record go with the status panel, not the
// world: the survivor radar's markers are laid out over its box, and the box
// is on BG3, which `ppu_wideAnchor` has pinned to the picture's edges. The
// pass says which OAM entries came from which record (`sprite_oam_owners`),
// and at the top of a frame that table describes the OAM the vblank just
// DMA'd; the record's flags are read from `ws->mem` for the same reason. A
// flag per slot rather than a moved X, so that a frame the game did not
// redraw is not moved twice.
static inline void ws_anchor_screen_sprites(Snes* snes, Widescreen* ws, bool in_level) {
  const uint8_t* mem = ws->mem;
  for (int s = 0; s < OAM_ENTRIES; s++) {
    const int rec = sprite_oam_owners.rec[s];
    const bool anchored =
        in_level && rec >= 0 && (ws_r16(mem, (uint32_t)rec + ACTOR_FLAGS) & ACTOR_SCREEN_SPACE) != 0;
    snes_setSpriteAnchored(snes, s, anchored);
  }
}

// Called at the top of every frame, before any of it is drawn — see
// `SnesFrameHook`. At that moment the game's vblank has finished: this frame's
// tilemap columns are in VRAM, its OAM has been DMA'd, and the picture is fixed
// but for the columns the console never had. Everything read here comes from
// `ws->mem`, the memory that picture was composed from, which is a tick behind
// the memory the game is running on now.
static inline void widescreen_frame(Snes* snes, Widescreen* ws) {
  const uint8_t* mem = ws->mem;
  const int margin = ws->margin;
  // Both halves, and see the note at the top of this file on why the width
  // alone is not enough: BG2SC keeps its 64 columns across the cards between
  // two levels, and only `$212C` says the world has stopped being drawn.
  const bool in_level =
      snes_bgTilemapWider(snes, 1) && snes_bgOnMainScreen(snes, 1);
  // BG3 is the status panel in a level and everything else outside one.
  snes_setLayerWide(snes, 2, in_level ? ppu_wideAnchor : ppu_wideAuto);
  // BG2 is the scrolling world, and its margins are filled below, so it is the
  // one layer whose continuation is known rather than guessed at.
  snes_setLayerWide(snes, 1, in_level ? ppu_wideStretch : ppu_wideAuto);
  // Sprites are world things in a level and stagecraft outside one. A title
  // card slides its letters in from off the side of the console's 256 and
  // relies on that edge to hide them; widening the picture without saying this
  // shows the trick, as the same words a second time at both edges.
  snes_setLayerWide(snes, 4, in_level ? ppu_wideStretch : ppu_wideClip);

  if (!in_level || margin <= 0) {
    // Nothing outside a level has a map to run off the end of.
    snes_setWidescreen(snes, margin, margin);
    snes_setWideClamp(snes, -PPU_EXTRA_MAX, 255 + PPU_EXTRA_MAX);
    ws_anchor_screen_sprites(snes, ws, false);
    return;
  }

  const uint16_t cam_x = ws_r16(mem, W_CAMERA_X);
  const uint16_t cam_y = ws_r16(mem, W_CAMERA_Y);
  const int cam_tx = cam_x >> 3, cam_ty = cam_y >> 3;
  const int cursor_x = ws_r16(mem, W_TILEMAP_CURSOR_X);
  const int cursor_y = ws_r16(mem, W_TILEMAP_CURSOR_Y);
  const uint16_t vram_base = ws_r16(mem, W_TILEMAP_VRAM_BASE);
  const uint16_t threshold = ws_r16(mem, W_TILE_PRIORITY_BELOW);
  // One map row in bytes, so half of it is the map's width in tiles. This is
  // the only measurement of the world's size taken here, and both the clamp
  // below and the column bounds come out of it.
  const int map_cols = ws_r16(mem, W_TILEMAP_ROW_BYTES) >> 1;

  // ## Where the extra width goes
  //
  // The camera stops at the edges of the map, because it was written for a
  // 256-pixel window and that window is the whole of what the game thinks is on
  // screen. Hang 43 more pixels off each side of it and at either end of a level
  // the picture leaves the world: nothing to draw, nothing to fill it with, and
  // the actors out there quite correctly do not exist.
  //
  // The answer is not to black it out and not to touch the camera. It is to
  // stop insisting the two margins be equal. The picture is always the same
  // width; when the left margin cannot have its 43 pixels because the world
  // starts sooner, the right margin takes what is left over. Walking to the far
  // west of a map, the view slides to a stop against the world's edge while the
  // player carries on to it — which is what every game that has ever had a
  // camera does at the end of a level, and it costs nothing but the picture no
  // longer being centred on a camera that was never centred on the player
  // either.
  const int room_left = cam_x;
  const int room_right = map_cols * 8 - (cam_x + 256);
  int left = margin < room_left ? margin : room_left;
  int right = 2 * margin - left;
  if (right > room_right) {
    right = room_right > 0 ? room_right : 0;
    left = 2 * margin - right;
  }
  if (left < 0) left = 0;
  snes_setWidescreen(snes, left, right);
  // ...and if the map is narrower than the whole picture, neither margin can be
  // filled and the total has to stay put or the framebuffer would change size
  // frame to frame. The backdrop covers whatever is left, from the map's two
  // edges in picture columns: world x is `cam_x` plus screen x, so world 0 sits
  // at `-cam_x` and the last map pixel at the far end of the last column.
  snes_setWideClamp(snes, -(int)cam_x, map_cols * 8 - 1 - (int)cam_x);

  // Whole tiles, rounded up, and enough for the widest either margin can get —
  // one side takes the other's share at the edges of a map, so both are filled
  // to the full width rather than to the width they happen to have this frame.
  const int cols = (2 * margin + 7) / 8;
  // The camera shows 28 rows and a fraction, and the fraction is a row.
  const int rows = CAMERA_WINDOW_TILES_Y + 1;

  for (int side = 0; side < 2; side++) {
    for (int i = 0; i < cols; i++) {
      // Left of the window, then right of it. `CAMERA_WINDOW_TILES_X` is where
      // the game's own 32 columns end and these begin.
      const int col =
          side ? cam_tx + CAMERA_WINDOW_TILES_X + i : cam_tx - 1 - i;
      if (col < 0 || col >= map_cols) continue;  // off the end of the world

      // Which slot of the 64-column ring that map column belongs in. The game
      // keeps `W_TILEMAP_CURSOR_X` as the slot holding `W_CAMERA_TILE_X`, and
      // everything else is that offset by the distance between the two.
      const int slot = (cursor_x + (col - cam_tx)) & TILEMAP_CURSOR_MASK_X;
      // A 64x32 tilemap is two 32x32 screens side by side, the right one a
      // fixed word offset away — the same split `camera_split_x` works in.
      const uint16_t slot_word =
          (uint16_t)((slot & TILEMAP_SCREEN_MASK) |
                     (slot >= CAMERA_SPLIT_COLUMNS ? TILEMAP_SECOND_SCREEN : 0));

      for (int j = 0; j < rows; j++) {
        const int row = cam_ty + j;
        // Rows cannot leave the map: the camera's own limit stops it 28 rows
        // short of the bottom, and this adds nothing vertically.
        const uint16_t row_base =
            ws_r16(mem, (uint32_t)(W_TILE_ROW_BASE + row * 2));
        uint16_t tile = ws_r16(
            mem, ((uint32_t)(TILEMAP_SRC_BANK & 1) << 16) |
                     (uint16_t)(row_base + col * 2));
        // The priority rule the ROM's column copy applies, and for the same
        // reason: a tile below the level record's threshold is drawn in front
        // of whatever walks over it.
        if ((tile & TILEMAP_COPY_MASK) < threshold) tile |= TILEMAP_PRIORITY_BIT;

        const int ring_row = (cursor_y + j) & TILEMAP_CURSOR_MASK_Y;
        snes_writeVramWord(
            snes, (uint16_t)(vram_base + slot_word + ring_row * 32), tile);
      }
    }
  }

  ws_margin_sprites(snes, ws, left, right);
  ws_anchor_screen_sprites(snes, ws, true);
}

// Move the neighbour spawner's window out to the edges of the picture that is
// actually being drawn. Idempotent, and applied every frame because `F4` can
// change the margin between two of them; at margin zero it writes the stock
// figure back and the game is the game again.
//
// Twice the margin, not the margin. The two margins slide -- at the end of a
// map the side with no world left to show gives its pixels to the other one --
// so either of them can be the whole `2 * margin` at once, and the window is a
// single distance either side of the middle. Sizing it to the widest one margin
// can get is the only figure that is right at both edges of every map, and it
// does not move, so nothing spawns and unspawns as the two margins trade.
static inline void ws_widen_window(Snes* snes, int margin) {
  Cart* cart = snes->cart;
  if (!cart || !cart->rom || cart->romSize <= WS_WINDOW_OPERAND + 1) return;
  // The opcode is not what gets written, so this stays true after a patch and
  // is false for any ROM whose `$81:823C` is not that compare.
  if (cart->rom[WS_WINDOW_OPCODE] != 0xc9) return;
  const uint16_t want = (uint16_t)(WS_WINDOW_STOCK + 2 * margin);
  cart->rom[WS_WINDOW_OPERAND] = (uint8_t)want;
  cart->rom[WS_WINDOW_OPERAND + 1] = (uint8_t)(want >> 8);
}

static inline void widescreen_hook(Snes* snes, void* ctx) {
  Widescreen* ws = (Widescreen*)ctx;
  ws_widen_window(snes, ws->margin);
  // The first frame has no tick before it to have been composed from, and the
  // game has drawn nothing yet either.
  if (ws->have_mem) {
    ws_return_slots(snes, ws);
    widescreen_frame(snes, ws);
  }
  memcpy(ws->mem, snes->ram, sizeof ws->mem);
  ws->have_mem = true;
}

// Hang the above off the machine's frame start. `ws` must outlive `snes`.
static inline void widescreen_install(Snes* snes, Widescreen* ws,
                                      const uint8_t* rom, int rom_len,
                                      int margin) {
  memset(ws, 0, sizeof *ws);
  ws->rom.data = rom;
  ws->rom.size = (uint32_t)rom_len;
  ws->margin = margin;
  snes_setFrameHook(snes, widescreen_hook, ws);
}

#endif
