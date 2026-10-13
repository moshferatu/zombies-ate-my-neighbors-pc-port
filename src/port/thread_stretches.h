// Stretches of four threads, between their calls.
//
// Each of these threads has its loop ported already, a pass at a time. What
// was left was whatever a pass cannot hold: a thread's first instructions,
// its end, and a pass that sleeps in the middle. A stretch is from where the
// thread starts or a call comes back to the next call, sleep or return.
//
// ## The weed, `$81:D28C`
//
// See `port/weeds.h`. On its guard it snaps on a draw, and `weed_frame`
// leaves at that draw. From there:
//
//   $81:D154  who is nearest, however far, is asked for
//   $81:D15F  kept at `$32`, and four pictures shown, which sleeps
//   $81:D168  three seeds thrown at them, five ticks apart, each a thread
//             begun beside the root: eight to the left, eight to the right
//             and two to the left, with a picture of the weed's own before
//             the second and the third
//   $81:D1DE  its first picture again, and it rests forty-five passes
//   $81:D2AD  the end of a pass: it sleeps two ticks, or its fate is looked
//             at
//
// Its fate positive, it only ends. Negative, who killed it is paid and
// counted, which is `port/begin.h`'s, and after its last pictures its entry
// in the level's list is marked.
//
// It begins at `$81:D2F1` or `$81:D2F9`, which differ in the bits its tiles
// have: `$1400` and `$0800`. The level's load goes up by `$23`, it takes a
// record, and its four arms begin at the root. Its health is twenty-five.
//
// ## The follower, `$82:F49E`
//
// See `port/follower.h`. Every sixteenth frame it looks at the tile it is
// over: on the level, and with bit 7 of the tile's attributes set, it
// sleeps a frame and puts tile `$150D` there. That is the tile a weed can
// take, by the same bit.
//
// Told that something touched it, it shows one last picture for two
// frames: `$82:F3F4`.
//
// It begins only if the level has room for it. Its load is ten, and it is
// put to the left or the right of what it follows by the sign of the word
// at `$00` on its page.
//
// ## The main thread, `$80:84B1`
//
// Between its calls it is mostly calls, which are links. What is not:
//
//   $80:85E1  a game's first words: ten neighbours and its first level.
//             Have the top scores been set to what they begin as?
//             `$7E:2124` says
//   $80:8600  ...which they now have
//   $80:8615  the sound's bank sent, once: `$18` on its page
//   $80:8849  the level's record asked for, from the two words at
//             `$9F:0036` by the level's offset
//   $80:8525  a level faded in: a count at `$1FB6` goes up
//   $80:867B  the level's load cleared, and the neighbours a level begins
//             with: as many as the two players have saved, in decimal
//   $80:C04C  the sprite cache's thread asked for
//
// And three that are not its own: where the front end begins,
// `$80:9126`; a word a player's thread keeps before it asks for its
// record, `$80:D16E`; and the first two sleeps of the thread that puts
// things in a level, `$80:C8F9`, which waits for an odd frame.
//
// ## The martian, `$81:99DF`
//
// See `port/martian.h`. The walker's first instructions, either side of
// `martian_begin`, the end of that, and the twelve ticks a shot waits.
//
// Port code: libc only.

#ifndef PORT_THREAD_STRETCHES_H
#define PORT_THREAD_STRETCHES_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

// `LDA #value` and a call: the whole of a stretch.
typedef struct {
  uint32_t pc;
  uint16_t value;
} ThreadArgument;

#define THREAD_ARGUMENT_COUNT 6
extern const ThreadArgument THREAD_ARGUMENTS[THREAD_ARGUMENT_COUNT];

// `LDA #a : LDY #y` and a call.
typedef struct {
  uint32_t pc;
  uint16_t a, y;
} ThreadPair;

#define THREAD_PAIR_COUNT 3
extern const ThreadPair THREAD_PAIRS[THREAD_PAIR_COUNT];

// --- the weed ----------------------------------------------------------------

// A seed thrown: where the stretch starts, the weed's picture before it,
// how far across from the root it begins, and the `JSL` that begins it.
typedef struct {
  uint32_t pc;
  uint16_t picture;  // or 0: as it is
  int16_t across;
  uint32_t spawn_pc;
  int block;
} WeedThrow;

#define WEED_THROW_COUNT 3
extern const WeedThrow WEED_THROWS[WEED_THROW_COUNT];

// How it begins: the bits its tiles have.
typedef struct {
  uint32_t pc;
  uint16_t tile_bits;
} WeedKind;

#define WEED_KIND_COUNT 2
extern const WeedKind WEED_KINDS[WEED_KIND_COUNT];

#define WEED_SNAP_ASK_PC 0x81d15bu    // `JSL player_in_range`
#define WEED_SNAP_SHOWN_PC 0x81d15fu
#define WEED_SNAP_SHOW_PC 0x81d164u   // `JSL pictures_play`
#define WEED_SNAP_PICTURES 0xd1f8u
#define WEED_SNAP_REST_PC 0x81d1deu
#define WEED_SNAP_RTS_PC 0x81d1f4u
#define WEED_PASS_END_PC 0x81d2adu
#define WEED_KILLED_PC 0x81d2b3u      // `kill_scored` of `port/begin.h`
#define WEED_DIED_PC 0x81d2c8u
#define WEED_MARK_PC 0x81d2cau        // `JSL victim_mark_gone`
#define WEED_END_PC 0x81d2ceu         // `record_end` of `port/begin.h`
#define WEED_ENTER_PC 0x81d28cu
#define WEED_ALLOC_CALL_PC 0x81d296u  // `JSR $D210`
#define WEED_MADE_PC 0x81d214u
#define WEED_HANDLER_PC 0x81d277u     // `JSL thread_set_handler`
#define WEED_BEGUN_PC 0x81d299u
#define WEED_GROWS_PC 0x81d127u

#define WEED_ANYBODY 0xffffu          // however far
#define WEED_SEED_THREAD 0xd4c9u
#define WEED_SEED_THREAD_BANK 0x0081
#define WEED_SEED_TICKS_APART 0x0005
#define WEED_PICTURE 0xa12bu
#define WEED_PICTURE_BANK 0x0090
#define WEED_COLLIDE_ID 0x0003
#define WEED_ATTR 0x0c00
#define WEED_REST_PASSES 0x002d
#define WEED_LOAD 0x0023
#define WEED_HANDLER 0xd301u
#define WEED_HEALTH 0x0019
#define WEED_DP_ENTRY 0x06            // its place in the level's list
#define WEED_DP_HEALTH 0x0e           // `D301_DP_HEALTH` of `port/collide.h`
#define WEED_DP_FREEZE_HITS 0x7e      // what a freeze has done to it
#define WEED_DP_TARGET 0x32           // who it snaps at

// --- the follower ------------------------------------------------------------

#define FOLLOWER_LOOK_PC 0x82f44au
#define FOLLOWER_EDGE_PC 0x82f44eu      // `JSL terrain_out_of_bounds`
#define FOLLOWER_ON_LEVEL_PC 0x82f452u
#define FOLLOWER_TILE_ASK_PC 0x82f458u  // `JSL tile_attrs_at_pixel`
#define FOLLOWER_TILE_PC 0x82f45cu
#define FOLLOWER_MARK_SLEEP_PC 0x82f464u
#define FOLLOWER_MARK_PC 0x82f468u
#define FOLLOWER_PUT_PC 0x82f472u       // `JSL map_tile_put`
#define FOLLOWER_ROOM_PC 0x82f4a2u
#define FOLLOWER_NO_ROOM_PC 0x82f4dfu   // `RTL`
#define FOLLOWER_MAKE_PC 0x82f4aeu      // `JSR $F354`
#define FOLLOWER_MADE_PC 0x82f358u
#define FOLLOWER_HANDLER_PC 0x82f3afu   // `JSL thread_set_handler`

#define FOLLOWER_HIT_PC 0x82f3f4u
#define FOLLOWER_HIT_SLEEP_PC 0x82f406u
#define FOLLOWER_HIT_PICTURE 0xd781u
#define FOLLOWER_HIT_FRAMES 0x0002

#define FOLLOWER_GROUND 0x0080u         // the bit of a tile it marks
#define FOLLOWER_TILE 0x150du
#define FOLLOWER_LOAD 0x000a
#define FOLLOWER_PICTURE 0xd6b5u
#define FOLLOWER_PICTURE_BANK 0x008f
#define FOLLOWER_COLLIDE_ID 0x0003
#define FOLLOWER_HANDLER 0xf4efu
#define FOLLOWER_DP_SIDE_GIVEN 0x00
#define FOLLOWER_FLIP 0x0002u           // of `ACTOR_FLIP`: drawn the other way

// --- the main thread ---------------------------------------------------------

#define MAIN_RESET_PC 0x8085e1u
#define MAIN_SCORES_ASK_PC 0x8085fcu   // `JSL top_scores_default`
#define MAIN_SCORES_SET_PC 0x808600u
#define MAIN_SOUND_ASK_PC 0x808611u    // `JSL apu_sfx_bank_end`
#define MAIN_SOUND_SET_PC 0x808615u
#define MAIN_FIRST_RTL_PC 0x808617u
#define LEVEL_RECORD_ASK_PC 0x808849u
#define LEVEL_RECORD_PC 0x82ac07u      // what it jumps to
#define LEVEL_FADED_PC 0x808525u
#define MAIN_FRAME_SLEEP_PC 0x80852bu
#define LEVEL_COUNT_PC 0x80867bu
#define LEVEL_COUNT_ASK_PC 0x808693u   // `JSL $80A8A4`, twice
#define LEVEL_COUNT_ASKED_PC 0x808697u
#define LEVEL_COUNT_ASK_B_PC 0x80869du
#define CACHE_START_PC 0x80c04cu
#define THREAD_START_PC 0x8083aeu      // what it jumps to

#define FRONTEND_ENTER_PC 0x809126u
#define FRONTEND_LAYERS_PC 0x809128u   // `JSL layers_reset`
#define PLAYER_KEEP_PC 0x80d16eu
#define PLAYER_ALLOC_PC 0x80d170u      // `JSL actor_slot_alloc`
#define PLAYER_DP_KEPT 0x0a
#define SPAWNER_TICK_PC 0x80c8f9u
#define SPAWNER_TICK_SLEEP_PC 0x80c904u
#define SPAWNER_WAIT_PC 0x80c908u
#define SPAWNER_SLEEP_PC 0x80c90du
#define SPAWNER_DP_CLEARED 0x0c

#define W_MAIN_1B80 0x1b80u            // $2000 as the thread starts
#define W_MAIN_1E7E 0x1e7eu            // 1 before the sound's bank is sent
#define W_SCORES_SET 0x2124u           // in `$7E`
#define W_LEVELS_FADED 0x1fb6u
#define MAIN_NEIGHBOURS 0x0010         // ten, in decimal
// The level a game starts at is the operand of `$80:85F0`, and it is read
// from the cartridge: `--level` writes another there. See `src/levelstart.h`.
#define MAIN_FIRST_LEVEL_OPERAND 0x8085f1u
#define LEVEL_RECORD_WORDS 0x9f0036u   // two words by the level's offset
#define MAIN_DP_SOUND_SET 0x18
#define MAIN_DP_LEVEL 0x10
#define CACHE_THREAD 0xb947u
#define CACHE_THREAD_BANK 0x0080

// --- the martian -------------------------------------------------------------

#define MARTIAN_ENTER_PC 0x8199dfu
#define MARTIAN_BEGIN_CALL_PC 0x8199e9u  // `JSR martian_begin`
#define MARTIAN_BEGUN_PC 0x8199ecu
#define MARTIAN_FIRST_SLEEP_PC 0x8199f2u
#define MARTIAN_WALKS_PC 0x819d2au
#define MARTIAN_LOAD 0x001e

// name, first instruction, the one after its last, and how many branches
// in it are always taken. `tools/cycles816.py` prices each.
#define TS_BLOCKS(X) \
  X(TS_LDA_IMM, 0x81d183, 0x81d186, 0) \
  X(TS_LDA_DP, 0x81d2c8, 0x81d2ca, 0) \
  X(TS_LDA_LDY_IMM, 0x8085d3, 0x8085d9, 0) \
  X(TS_LDA_LDX_IMM, 0x808697, 0x80869d, 0) \
  X(TS_LDX_LDY_DP, 0x82f44a, 0x82f44e, 0) \
  X(TS_BRANCH, 0x81d2b1, 0x81d2b3, 0)         /* not taken */ \
  X(TS_JSR, 0x81d299, 0x81d29c, 0) \
  X(TS_WEED_AIM, 0x81d154, 0x81d15b, 0) \
  X(TS_WEED_SHOW, 0x81d15f, 0x81d164, 0) \
  X(TS_WEED_THROW_A, 0x81d168, 0x81d17f, 0) \
  X(TS_WEED_THROW_B, 0x81d18a, 0x81d1a9, 0) \
  X(TS_WEED_THROW_C, 0x81d1b4, 0x81d1d3, 0) \
  X(TS_WEED_REST, 0x81d1de, 0x81d1f4, 0) \
  X(TS_WEED_FATE, 0x81d2ad, 0x81d2b1, 0)      /* LDA $0C : BEQ */ \
  X(TS_WEED_AGAIN, 0x81d29c, 0x81d2a1, 0) \
  X(TS_WEED_KIND, 0x81d2f1, 0x81d2f9, 0)      /* LDA # : STA $0A : JMP */ \
  X(TS_WEED_ENTER, 0x81d28c, 0x81d296, 0) \
  X(TS_WEED_MADE, 0x81d214, 0x81d277, 0) \
  X(TS_WEED_GROWS, 0x81d127, 0x81d12d, 0)     /* LDA # : STA $14 : RTS */ \
  X(TS_FOLLOWER_TILE, 0x82f45c, 0x82f461, 0)  /* AND # : BEQ */ \
  X(TS_FOLLOWER_MARK, 0x82f468, 0x82f472, 0) \
  X(TS_FOLLOWER_LOAD, 0x82f4a4, 0x82f4ae, 0) \
  X(TS_FOLLOWER_SIDE, 0x82f358, 0x82f35f, 0)  /* STA $08 ... BPL */ \
  X(TS_FOLLOWER_LEFT, 0x82f35f, 0x82f369, 1) \
  X(TS_FOLLOWER_RIGHT, 0x82f369, 0x82f371, 0) \
  X(TS_FOLLOWER_MADE, 0x82f371, 0x82f3af, 0) \
  X(TS_MAIN_RESET, 0x8085e1, 0x8085fc, 0) \
  X(TS_MAIN_SCORES_SET, 0x808600, 0x808607, 0) \
  X(TS_MAIN_SOUND_TEST, 0x808607, 0x80860b, 0) \
  X(TS_MAIN_SOUND, 0x80860b, 0x808611, 0) \
  X(TS_MAIN_SOUND_SET, 0x808615, 0x808617, 0) \
  X(TS_LEVEL_RECORD, 0x808849, 0x80885b, 0) \
  X(TS_LEVEL_FADED, 0x808525, 0x80852b, 0) \
  X(TS_LEVEL_COUNT, 0x80867b, 0x808693, 0) \
  X(TS_CACHE_START, 0x80c04c, 0x80c059, 0) \
  X(TS_REP, 0x809126, 0x809128, 0) \
  X(TS_STA_DP, 0x80d16e, 0x80d170, 0) \
  X(TS_FOLLOWER_HIT, 0x82f3f4, 0x82f406, 0) \
  X(TS_SPAWNER_TICK, 0x80c8f9, 0x80c901, 0)      /* LDA abs : AND # : BNE */ \
  X(TS_SPAWNER_WAIT, 0x80c908, 0x80c90d, 0) \
  X(TS_MARTIAN_ENTER, 0x8199df, 0x8199e9, 0) \
  X(TS_MARTIAN_WALKS, 0x819d2a, 0x819d30, 0)  /* LDA # : STA $0A : RTS */

enum {
#define X(name, from, to, taken) name,
  TS_BLOCKS(X)
#undef X
  TS_TAKEN,  // a branch taken
  TS_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[TS_BLOCK_COUNT];
} ThreadStretchWork;

// What a stretch needs of where it starts.
typedef enum {
  TS_NEEDS_NOTHING,
  TS_NEEDS_RECORD,  // the thread's record on its page, in low WRAM
  TS_NEEDS_A,       // a record in A
  TS_NEEDS_LEVEL,   // the level's offset is one into the cartridge
  TS_NEEDS_COUNTS,  // the two counts of those saved are small
} ThreadStretchNeeds;

bool thread_stretch_needs_met(const Wram* w, ThreadStretchNeeds needs,
                              const PortCpu* c);

// name, symbol, entry, the port, its exits and what it needs.
#define THREAD_STRETCHES(X) \
  X(argument_weed_a, "$81:D183", 0x81d183u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_weed_b, "$81:D1AD", 0x81d1adu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_weed_c, "$81:D1D7", 0x81d1d7u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_follower, "$82:F4B1", 0x82f4b1u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_level_wait, "$80:8516", 0x808516u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_martian_shot, "$81:99C2", 0x8199c2u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(pair_main_layers, "$80:85D3", 0x8085d3u, thread_pair, PAIR, TS_NEEDS_NOTHING) \
  X(pair_martian_handler, "$81:9B30", 0x819b30u, thread_pair, PAIR, TS_NEEDS_NOTHING) \
  X(pair_hud_thread, "$80:C2DE", 0x80c2deu, thread_pair, PAIR, TS_NEEDS_NOTHING) \
  X(weed_snap_aim, "$81:D154", 0x81d154u, weed_snap_aim, WEED, TS_NEEDS_NOTHING) \
  X(weed_snap_show, "$81:D15F", WEED_SNAP_SHOWN_PC, weed_snap_show, WEED, TS_NEEDS_NOTHING) \
  X(weed_throw_a, "$81:D168", 0x81d168u, weed_throw, WEED, TS_NEEDS_RECORD) \
  X(weed_throw_b, "$81:D18A", 0x81d18au, weed_throw, WEED, TS_NEEDS_RECORD) \
  X(weed_throw_c, "$81:D1B4", 0x81d1b4u, weed_throw, WEED, TS_NEEDS_RECORD) \
  X(weed_snap_rest, "$81:D1DE", WEED_SNAP_REST_PC, weed_snap_rest, WEED, TS_NEEDS_RECORD) \
  X(weed_pass_end, "$81:D2AD", WEED_PASS_END_PC, weed_pass_end, WEED_PASS, TS_NEEDS_NOTHING) \
  X(weed_fate, "$81:D2B1", 0x81d2b1u, weed_fate, WEED, TS_NEEDS_NOTHING) \
  X(weed_died, "$81:D2C8", WEED_DIED_PC, weed_died, WEED, TS_NEEDS_NOTHING) \
  X(weed_enter_a, "$81:D2F1", 0x81d2f1u, weed_enter, WEED, TS_NEEDS_NOTHING) \
  X(weed_enter_b, "$81:D2F9", 0x81d2f9u, weed_enter, WEED, TS_NEEDS_NOTHING) \
  X(weed_made, "$81:D214", WEED_MADE_PC, weed_made, WEED, TS_NEEDS_A) \
  X(weed_begun, "$81:D299", WEED_BEGUN_PC, weed_begun, WEED_PASS, TS_NEEDS_NOTHING) \
  X(follower_look, "$82:F44A", FOLLOWER_LOOK_PC, follower_look, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_on_level, "$82:F452", FOLLOWER_ON_LEVEL_PC, follower_on_level, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_tile, "$82:F45C", FOLLOWER_TILE_PC, follower_tile, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_mark, "$82:F468", FOLLOWER_MARK_PC, follower_mark, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_room, "$82:F4A2", FOLLOWER_ROOM_PC, follower_room, FOLLOWER, TS_NEEDS_NOTHING) \
  X(follower_made, "$82:F358", FOLLOWER_MADE_PC, follower_made, FOLLOWER, TS_NEEDS_A) \
  X(main_reset, "$80:85E1", MAIN_RESET_PC, main_reset, MAIN, TS_NEEDS_NOTHING) \
  X(main_scores_set, "$80:8600", MAIN_SCORES_SET_PC, main_scores_set, MAIN, TS_NEEDS_NOTHING) \
  X(main_sound_set, "$80:8615", MAIN_SOUND_SET_PC, main_sound_set, MAIN, TS_NEEDS_NOTHING) \
  X(level_record_ask, "$80:8849", LEVEL_RECORD_ASK_PC, level_record_ask, MAIN, TS_NEEDS_LEVEL) \
  X(level_faded, "$80:8525", LEVEL_FADED_PC, level_faded, MAIN, TS_NEEDS_NOTHING) \
  X(level_count, "$80:867B", LEVEL_COUNT_PC, level_count, MAIN, TS_NEEDS_COUNTS) \
  X(level_count_asked, "$80:8697", LEVEL_COUNT_ASKED_PC, level_count_asked, MAIN, TS_NEEDS_NOTHING) \
  X(cache_start, "$80:C04C", CACHE_START_PC, cache_start, MAIN, TS_NEEDS_NOTHING) \
  X(follower_hit, "$82:F3F4", FOLLOWER_HIT_PC, follower_hit, FOLLOWER, TS_NEEDS_RECORD) \
  X(frontend_enter, "$80:9126", FRONTEND_ENTER_PC, frontend_enter, MAIN, TS_NEEDS_NOTHING) \
  X(player_keep, "$80:D16E", PLAYER_KEEP_PC, player_keep, MAIN, TS_NEEDS_NOTHING) \
  X(spawner_tick, "$80:C8F9", SPAWNER_TICK_PC, spawner_tick, MAIN, TS_NEEDS_NOTHING) \
  X(spawner_wait, "$80:C908", SPAWNER_WAIT_PC, spawner_wait, SPAWNER, TS_NEEDS_NOTHING) \
  X(martian_enter, "$81:99DF", MARTIAN_ENTER_PC, martian_enter, MARTIAN, TS_NEEDS_NOTHING) \
  X(martian_begun, "$81:99EC", MARTIAN_BEGUN_PC, martian_begun, MARTIAN, TS_NEEDS_NOTHING)

#define X(name, sym, entry, port, exits, needs) \
  void port(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k);
// Several stretches share a port, so each is declared as often.
THREAD_STRETCHES(X)
#undef X

#endif
