// Dr. Tongue, and his head: what comes after the giant spider on level 48,
// the last of the game's bosses.
//
// The giant spider, as it dies, begins the thread at `$83:C687` at its place
// once there are fewer than 18 records about (`port/giant_spider.h`). This is
// all of that thread, and of the thread its shots are.
//
// **Dr. Tongue** (`$83:C687`). His weight, 35, goes on the level's load
// (`$DE`, which `spawn_has_room` reads), and he is a record of his own at
// the point he was given, two pictures by turns, and no handler. A second
// later the head's colours are set, both rows (`port/figure_colours.h`), and
// its tiles go up, `$2300` bytes from `$94:8000` to VRAM `$5000` by
// `boss_bg_tiles_job` (`port/vblank.h`), a frame at a time until they are
// through. Then `$83:C6F3`: he blinks every five frames until a player is
// within 65 of him (`player_bearing`), with a cry every 45 while `$1F52` is
// 3. Then he drinks the potion (a sound, and a list of pictures played,
// `pictures_play`); the head's picture 0 goes up on its layer and his record
// is freed. The first layer's mosaic goes fine from coarse in fifteen steps
// of three frames, and after twelve frames coarse again in sixteen; picture
// 1 goes up and it goes fine again; and once more, to picture 2. He is the
// head.
//
// **The head** is drawn on the big figures' layer (`port/bossbg.h`) and is
// six records besides, about its place from `$83:B9CD`, which wear no picture
// and are what the players' shots and the players touch. Its colours turn
// (`$80:A36E`, `port/palcycle.h`), begun when there is room. It has 35
// health, waits a quarter of a second, and its handler is set
// (`dr_tongue_collide`, below).
//
// **Each frame** after that:
//
//     $83:BB27  where it is on its layer; once the last picture is up, every
//               twelve frames the picture it wants; its flash, two frames
//               white when it was hit; and its layer's scroll
//     its state, the routine at `$40`
//     $83:BF06  its picture for the way it faces and the life it is on
//     it sleeps a frame
//     $83:BF38  broken on one of its first three lives: another, with three
//               puffs about it (`$83:B1DD`) and 35 health
//     $83:BB44  each of its three tongues
//
// **The states.**
//
//     $83:C030  it sweeps from side to side above the nearest player, one a
//               frame faster on each life, keeping 85 to 105 above it. A
//               player above it sends it to a side; five damage since it
//               last charged, or a player within twenty, a charge; the
//               player within 12 across, a lick
//     $83:C0D4  it means to lick down: it sweeps on until a draw of 15 in 256
//     $83:C199  twenty frames of licking: every fourth, on its first two
//               lives a tongue down as far as the player is, and on its last
//               two a pair of shots, eleven turns apart (`$30`)
//     $83:C256  it goes up and down at a side, keeping 85 to 105 across from
//               the player. Two players on its two sides, or five damage, or
//               a player within twenty, a charge; the player within 12 of
//               its level, less 18, a lick
//     $83:C31B  it means to lick across, and
//     $83:C3FD  licks across, as down
//     $83:C518  a charge, two steps a frame, its records hurting what they
//               touch (collision id `$35`): at the nearest player for 80 to
//               111 frames, then off a way at random for 65 to 96, then back
//               to sweeping
//
// It goes only where two probes about where it would be are off the walls
// (`$83:BDD0`, the wide test, which is the Snakeoid's `$82:A214` to the
// byte) and on the map (`terrain_out_of_bounds`), and every 32 ticks each
// probe tells a box about itself (`actor_notify_box`).
//
// **A tongue** (`$83:BC0A`) is a record of its own, collision id 9, that
// goes out its way four pixels a frame for as long as it was sent, or until
// the ground (`terrain_point_bit2`) stops it; then it draws back for sixteen
// frames, every fourth a picture and a box told about it, and is freed.
//
// **Its lives.** Its handler counts hits down from its health by the damage
// table, and under nought it is broken. The first three times it has 35
// health again; the fourth it dies.
//
// **Its death.** Once its tongues are in, whoever hit it last is paid 20,000
// (`score_add`), its six records are freed and its weight comes off the
// load. Twelve puffs about it with its colours flashing; the mosaic coarser
// in fifteen steps of two frames, puffs and flashes by turns. Its layer is
// blanked (`boss_bg_clear`, `port/bossbg.h`), `$7E:6058` (a Snakeoid is up)
// is cleared, the map's block at (4, 2) is put down as the block after it
// (`blockmap_cell_ptr`, `tile_block_begin`), the mosaic is ended, and the
// thread ends.
//
// **Its handler** (`$83:C994`) takes no hit on a record that wears nothing
// or id 9 (`$0078` is the record touched), nor while it flashes, nor below a
// player's shot's id. A player touching it with the top bit set (ids 5 and
// 6) sends it off a way at random (`$83:C4A9`). Otherwise it keeps what hit
// it at `$52`, to pay; `$60`, `$70` and `$6F` are answered as `$5D` most
// ticks and `$5C` or `$63` on some, `$61` and `$67` as `$5C`, `$62` as `$5C`
// one tick in four; the hit flashes it (`$4E`), adds its damage to `$5C`, and
// takes it off its health. It returns with carry set, which parks its thread
// (`thread_call_handler`).
//
// **Its shot** (`$83:CA4A`) is a thread and a record of its own: half a
// second hidden, then for three seconds it flies, four pixels a frame its way
// at first and each fourth frame a step towards the nearest player
// (`actor_nearest`, `actor_bearing`), to three a frame each way, until the
// ground stops it. Then it plays a
// pop and is freed. Its handler (`$83:CC03`, `dr_tongue_shot_collide`) ends
// it on a player's touch (ids 1, 5 and 6) or a player's shot.
//
// **Written as blocks**, as `port/giant_spider.h` is, and generated from the
// listing as that was written by hand: a stretch begins where a call or a
// sleep comes back and runs block after block until the next call the
// harness makes, or the `RTL` or `JML`. Its own `JSR`s are made here, and an
// `RTS` goes where the stack says; the state's is made only to the seven
// states. A return to an address the thread does not know turns the stretch
// down. It makes three calls itself, whose cost is known: the draws
// (`rng_next`), whether the last picture has gone up (`$82:80E0`), and the
// wide test.
//
// Both threads run with data bank `$83`, their own.
//
// Its two mosaic jobs, `$83:C949` and `$83:C95D`, are the saucer's
// `mosaic_job` and `mosaic_off_job` to the byte, and `port/vblank.h` runs
// them.
//
// Port code: libc only.

#ifndef PORT_DR_TONGUE_H
#define PORT_DR_TONGUE_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/collide.h"
#include "port/cpu.h"
#include "port/hw.h"
#include "port/terrain.h"
#include "port/wram.h"

#define DR_TONGUE_BANK 0x83u
#define DR_TONGUE_PC 0x83c687u
#define DR_TONGUE_SHOT_PC 0x83ca4au
#define DR_TONGUE_COLLIDE_ENTRY 0x83c994u
#define DR_TONGUE_SHOT_COLLIDE_ENTRY 0x83cc03u

// Its mosaic jobs (`port/vblank.h`).
#define DR_TONGUE_MOSAIC_JOB_PC 0x83c949u
#define DR_TONGUE_MOSAIC_JOB_RTL_PC 0x83c95cu
#define DR_TONGUE_MOSAIC_OFF_JOB_PC 0x83c95du
#define DR_TONGUE_MOSAIC_OFF_JOB_RTL_PC 0x83c965u

// Its page.
#define DR_TONGUE_DP_RECORD 0x08    // and five more after it, to `$12`
#define DR_TONGUE_DP_LIFE 0x14      // 0 to 3
#define DR_TONGUE_DP_TEMP 0x62
#define DR_TONGUE_DP_WAY 0x3c       // of eight, doubled, and one on
#define DR_TONGUE_DP_FACING 0x3e
#define DR_TONGUE_DP_STATE 0x40
#define DR_TONGUE_DP_TARGET 0x42    // the player it charges, or none
#define DR_TONGUE_DP_BROKEN 0x4a    // not nought: its health went under
#define DR_TONGUE_DP_HEALTH 0x4c
#define DR_TONGUE_DP_HIT 0x4e       // negative: hit, to flash
#define DR_TONGUE_DP_FLASH 0x50     // frames of the flash left
#define DR_TONGUE_DP_HIT_BY 0x52
#define DR_TONGUE_DP_STUNG 0x5c     // damage since it last charged
#define DR_TONGUE_DP_LEFT 0x5e      // frames left of what it is doing
#define DR_TONGUE_SHOT_DP_RECORD 0x08
#define DR_TONGUE_SHOT_DP_GONE 0x14

#define DR_TONGUE_BOUNCE_PC 0xc4a9u  // off a way at random
#define DR_TONGUE_CHARGE_STATE 0xc518u
#define DR_TONGUE_CHARGING_ID 0x0035u
#define DR_TONGUE_NO_TARGET 0xffffu

// Where a stretch can begin: after each call the harness makes.
#define DR_TONGUE_ENTRIES(X) \
  X(picture_up, "$83:BA05", 0x83ba05u) \
  X(picture_flip_up, "$83:BA16", 0x83ba16u) \
  X(unflashed, "$83:BAC9", 0x83bac9u) \
  X(flashed, "$83:BADF", 0x83badfu) \
  X(tongue_ground, "$83:BBC8", 0x83bbc8u) \
  X(tongue_freed, "$83:BBEF", 0x83bbefu) \
  X(tongue_made, "$83:BC2B", 0x83bc2bu) \
  X(nearest_found, "$83:BCEE", 0x83bceeu) \
  X(bounds_asked, "$83:BD8C", 0x83bd8cu) \
  X(box_told, "$83:BF05", 0x83bf05u) \
  X(life_unhandled, "$83:BF55", 0x83bf55u) \
  X(life_cry_heard, "$83:BF64", 0x83bf64u) \
  X(life_puff, "$83:BF93", 0x83bf93u) \
  X(life_puff_waited, "$83:BF9A", 0x83bf9au) \
  X(life_handled, "$83:BFAE", 0x83bfaeu) \
  X(spit_down, "$83:C176", 0x83c176u) \
  X(spit_down2, "$83:C21B", 0x83c21bu) \
  X(spit_side, "$83:C3D2", 0x83c3d2u) \
  X(spit_side_low, "$83:C3E4", 0x83c3e4u) \
  X(spit_side2, "$83:C48D", 0x83c48du) \
  X(spit_side2_low, "$83:C49F", 0x83c49fu) \
  X(part_made, "$83:C580", 0x83c580u) \
  X(coloured, "$83:C5F0", 0x83c5f0u) \
  X(sprite_coloured, "$83:C5FA", 0x83c5fau) \
  X(tiles_asked, "$83:C61C", 0x83c61cu) \
  X(tiles_waited, "$83:C623", 0x83c623u) \
  X(colours_waited, "$83:C664", 0x83c664u) \
  X(colours_begun, "$83:C66E", 0x83c66eu) \
  X(handler_waited, "$83:C67C", 0x83c67cu) \
  X(handled, "$83:C686", 0x83c686u) \
  X(begins, "$83:C687", 0x83c687u) \
  X(doctor_waited, "$83:C69B", 0x83c69bu) \
  X(frame, "$83:C6BE", 0x83c6beu) \
  X(tongues_waited, "$83:C6D3", 0x83c6d3u) \
  X(paid, "$83:C6E0", 0x83c6e0u) \
  X(looked, "$83:C6FE", 0x83c6feu) \
  X(blink_waited, "$83:C723", 0x83c723u) \
  X(potion_heard, "$83:C72C", 0x83c72cu) \
  X(potion_drunk, "$83:C733", 0x83c733u) \
  X(doctor_freed, "$83:C74C", 0x83c74cu) \
  X(change_waited, "$83:C759", 0x83c759u) \
  X(change_heard, "$83:C766", 0x83c766u) \
  X(change2_waited, "$83:C776", 0x83c776u) \
  X(change2_heard, "$83:C783", 0x83c783u) \
  X(doctor_made, "$83:C797", 0x83c797u) \
  X(doctor_handled, "$83:C7D6", 0x83c7d6u) \
  X(cried, "$83:C7E6", 0x83c7e6u) \
  X(finer_asked, "$83:C803", 0x83c803u) \
  X(finer_waited, "$83:C80A", 0x83c80au) \
  X(coarser_asked, "$83:C82C", 0x83c82cu) \
  X(coarser_waited, "$83:C833", 0x83c833u) \
  X(death_puff, "$83:C857", 0x83c857u) \
  X(death_flashed, "$83:C861", 0x83c861u) \
  X(death_flash_waited, "$83:C868", 0x83c868u) \
  X(death_unflashed, "$83:C872", 0x83c872u) \
  X(death_unflash_waited, "$83:C879", 0x83c879u) \
  X(death_coarser_asked, "$83:C899", 0x83c899u) \
  X(death_coarser_waited, "$83:C8A9", 0x83c8a9u) \
  X(death_flashed2, "$83:C8BA", 0x83c8bau) \
  X(death_puff2, "$83:C8DB", 0x83c8dbu) \
  X(death_unflashed2, "$83:C8E5", 0x83c8e5u) \
  X(blanked, "$83:C8F4", 0x83c8f4u) \
  X(mosaic_ended, "$83:C908", 0x83c908u) \
  X(part_freed, "$83:C973", 0x83c973u) \
  X(cell_found, "$83:C986", 0x83c986u) \
  X(block_begun, "$83:C993", 0x83c993u) \
  X(shot_begins, "$83:CA4A", 0x83ca4au) \
  X(shot_hidden_waited, "$83:CA6F", 0x83ca6fu) \
  X(shot_waited, "$83:CA88", 0x83ca88u) \
  X(shot_popped, "$83:CAA1", 0x83caa1u) \
  X(shot_nearest, "$83:CAD6", 0x83cad6u) \
  X(shot_bearing, "$83:CADD", 0x83caddu) \
  X(shot_ground, "$83:CB1E", 0x83cb1eu) \
  X(shot_made, "$83:CB81", 0x83cb81u) \
  X(shot_handled, "$83:CBDD", 0x83cbddu)

// Where one stops: a call the harness makes, the `RTL` or the `JML`.
#define DR_TONGUE_EXITS(X) \
  X(0x83ba01u) /* JSL boss_bg_queue */ \
  X(0x83ba12u) /* JSL boss_bg_queue_flip */ \
  X(0x83bac5u) /* JSL palette_copy_a */ \
  X(0x83badbu) /* JSL palette_copy_a */ \
  X(0x83bbc4u) /* JSL terrain_point_bit2 */ \
  X(0x83bbebu) /* JSL actor_slot_free */ \
  X(0x83bc27u) /* JSL actor_slot_alloc */ \
  X(0x83bceau) /* JSL actor_nearest */ \
  X(0x83bd88u) /* JSL terrain_out_of_bounds */ \
  X(0x83bf01u) /* JSL actor_notify_box */ \
  X(0x83bf51u) /* JSL thread_set_handler */ \
  X(0x83bf60u) /* JSL apu_play_sfx */ \
  X(0x83bf8fu) /* JSL thread_spawn */ \
  X(0x83bf96u) /* JSL thread_yield */ \
  X(0x83bfaau) /* JSL thread_set_handler */ \
  X(0x83c172u) /* JSL thread_spawn */ \
  X(0x83c217u) /* JSL thread_spawn */ \
  X(0x83c3ceu) /* JSL thread_spawn */ \
  X(0x83c3e0u) /* JSL thread_spawn */ \
  X(0x83c489u) /* JSL thread_spawn */ \
  X(0x83c49bu) /* JSL thread_spawn */ \
  X(0x83c57cu) /* JSL actor_slot_alloc */ \
  X(0x83c5ecu) /* JSL palette_copy_a */ \
  X(0x83c5f6u) /* JSL palette_copy_b */ \
  X(0x83c618u) /* JSL vbl_queue_a_add */ \
  X(0x83c61fu) /* JSL thread_yield */ \
  X(0x83c660u) /* JSL thread_yield */ \
  X(0x83c66au) /* JSL thread_spawn */ \
  X(0x83c678u) /* JSL thread_yield */ \
  X(0x83c682u) /* JSL thread_set_handler */ \
  X(0x83c697u) /* JSL thread_yield */ \
  X(0x83c6bau) /* JSL thread_yield */ \
  X(0x83c6cfu) /* JSL thread_yield */ \
  X(0x83c6dcu) /* JSL score_add */ \
  X(0x83c6f2u) /* RTL */ \
  X(0x83c6fau) /* JSL player_bearing */ \
  X(0x83c71fu) /* JSL thread_yield */ \
  X(0x83c728u) /* JSL apu_play_sfx */ \
  X(0x83c72fu) /* JSL pictures_play */ \
  X(0x83c748u) /* JSL actor_slot_free */ \
  X(0x83c755u) /* JSL thread_yield */ \
  X(0x83c762u) /* JSL apu_play_sfx */ \
  X(0x83c772u) /* JSL thread_yield */ \
  X(0x83c77fu) /* JSL apu_play_sfx */ \
  X(0x83c793u) /* JSL actor_slot_alloc */ \
  X(0x83c7d2u) /* JSL thread_set_handler */ \
  X(0x83c7e2u) /* JSL apu_play_sfx */ \
  X(0x83c7ffu) /* JSL vbl_queue_a_add */ \
  X(0x83c806u) /* JSL thread_yield */ \
  X(0x83c828u) /* JSL vbl_queue_a_add */ \
  X(0x83c82fu) /* JSL thread_yield */ \
  X(0x83c853u) /* JSL thread_spawn */ \
  X(0x83c85du) /* JSL palette_copy_a */ \
  X(0x83c864u) /* JSL thread_yield */ \
  X(0x83c86eu) /* JSL palette_copy_a */ \
  X(0x83c875u) /* JSL thread_yield */ \
  X(0x83c895u) /* JSL vbl_queue_a_add */ \
  X(0x83c8a5u) /* JSL thread_yield */ \
  X(0x83c8b6u) /* JSL palette_copy_a */ \
  X(0x83c8d7u) /* JSL thread_spawn */ \
  X(0x83c8e1u) /* JSL palette_copy_a */ \
  X(0x83c8ecu) /* JSL thread_yield */ \
  X(0x83c8f0u) /* JSL boss_bg_clear */ \
  X(0x83c904u) /* JSL vbl_queue_a_add */ \
  X(0x83c96fu) /* JSL actor_slot_free */ \
  X(0x83c982u) /* JSL blockmap_cell_ptr */ \
  X(0x83c98fu) /* JSL tile_block_begin */ \
  X(0x83ca6bu) /* JSL thread_yield */ \
  X(0x83ca84u) /* JSL thread_yield */ \
  X(0x83ca9du) /* JSL pictures_play */ \
  X(0x83caafu) /* JML actor_slot_free */ \
  X(0x83cad2u) /* JSL actor_nearest */ \
  X(0x83cad9u) /* JSL actor_bearing */ \
  X(0x83cb1au) /* JSL terrain_point_bit2 */ \
  X(0x83cb7du) /* JSL actor_slot_alloc */ \
  X(0x83cbd9u) /* JSL thread_set_handler */

// The straight runs of the listing, each named for where it starts, with
// where it ends. `tools/price_runs.py` prices them.
#define DR_TONGUE_RUNS(X) \
  X(B9F5, 0x83ba01u) \
  X(BA05, 0x83ba06u) \
  X(BA06, 0x83ba12u) \
  X(BA16, 0x83ba17u) \
  X(BA17, 0x83ba2du) \
  X(BA2D, 0x83ba31u) \
  X(BA31, 0x83ba3eu) \
  X(BA3E, 0x83ba41u) \
  X(BA41, 0x83ba43u) \
  X(BA43, 0x83ba46u) \
  X(BA46, 0x83ba47u) \
  X(BAB7, 0x83babbu) \
  X(BABB, 0x83babfu) \
  X(BABF, 0x83bac5u) \
  X(BAC9, 0x83bacau) \
  X(BACA, 0x83baceu) \
  X(BACE, 0x83badbu) \
  X(BADF, 0x83bae0u) \
  X(BAE0, 0x83baeeu) \
  X(BAEE, 0x83baf0u) \
  X(BAF0, 0x83baf4u) \
  X(BAF4, 0x83bb01u) \
  X(BB01, 0x83bb09u) \
  X(BB09, 0x83bb0au) \
  X(BB0A, 0x83bb27u) \
  X(BB27, 0x83bb2au) \
  X(BB2A, 0x83bb2du) \
  X(BB2D, 0x83bb30u) \
  X(BB30, 0x83bb31u) \
  X(BB31, 0x83bb39u) \
  X(BB39, 0x83bb43u) \
  X(BB43, 0x83bb44u) \
  X(BB44, 0x83bb49u) \
  X(BB49, 0x83bb52u) \
  X(BB52, 0x83bb55u) \
  X(BB55, 0x83bb5fu) \
  X(BB5F, 0x83bb60u) \
  X(BB60, 0x83bb66u) \
  X(BB66, 0x83bb6bu) \
  X(BB6B, 0x83bb6eu) \
  X(BB70, 0x83bb79u) \
  X(BB79, 0x83bb7cu) \
  X(BB7C, 0x83bb8fu) \
  X(BB8F, 0x83bb92u) \
  X(BB92, 0x83bb97u) \
  X(BB97, 0x83bba1u) \
  X(BBA1, 0x83bba7u) \
  X(BBA7, 0x83bbabu) \
  X(BBAB, 0x83bbc4u) \
  X(BBC8, 0x83bbcau) \
  X(BBCA, 0x83bbccu) \
  X(BBCC, 0x83bbe1u) \
  X(BBE1, 0x83bbe2u) \
  X(BBE2, 0x83bbebu) \
  X(BBEF, 0x83bbf2u) \
  X(BC0A, 0x83bc11u) \
  X(BC11, 0x83bc14u) \
  X(BC14, 0x83bc1bu) \
  X(BC1B, 0x83bc23u) \
  X(BC23, 0x83bc24u) \
  X(BC24, 0x83bc27u) \
  X(BC2B, 0x83bc81u) \
  X(BCE8, 0x83bceau) \
  X(BCEE, 0x83bcf2u) \
  X(BCF2, 0x83bcfcu) \
  X(BCFC, 0x83bd01u) \
  X(BD01, 0x83bd06u) \
  X(BD06, 0x83bd08u) \
  X(BD08, 0x83bd13u) \
  X(BD13, 0x83bd17u) \
  X(BD17, 0x83bd1du) \
  X(BD1D, 0x83bd2du) \
  X(BD2D, 0x83bd34u) \
  X(BD34, 0x83bd3bu) \
  X(BD3B, 0x83bd40u) \
  X(BD40, 0x83bd48u) \
  X(BD55, 0x83bd5au) \
  X(BD5A, 0x83bd74u) \
  X(BD74, 0x83bd7bu) \
  X(BD7B, 0x83bd82u) \
  X(BD82, 0x83bd84u) \
  X(BD84, 0x83bd88u) \
  X(BD8C, 0x83bd8eu) \
  X(BD8E, 0x83bd98u) \
  X(BD98, 0x83bda5u) \
  X(BDA5, 0x83bdc6u) \
  X(BDC6, 0x83bdc7u) \
  X(BDC7, 0x83bdc8u) \
  X(BEDD, 0x83bf01u) \
  X(BF05, 0x83bf06u) \
  X(BF06, 0x83bf0au) \
  X(BF0A, 0x83bf0fu) \
  X(BF0F, 0x83bf1cu) \
  X(BF1C, 0x83bf20u) \
  X(BF20, 0x83bf25u) \
  X(BF25, 0x83bf26u) \
  X(BF38, 0x83bf3cu) \
  X(BF3C, 0x83bf44u) \
  X(BF44, 0x83bf51u) \
  X(BF55, 0x83bf5du) \
  X(BF5D, 0x83bf60u) \
  X(BF64, 0x83bf69u) \
  X(BF69, 0x83bf6fu) \
  X(BF6F, 0x83bf7eu) \
  X(BF7E, 0x83bf8fu) \
  X(BF93, 0x83bf96u) \
  X(BF9A, 0x83bfa4u) \
  X(BFA4, 0x83bfaau) \
  X(BFAE, 0x83bfafu) \
  X(BFBB, 0x83bfc2u) \
  X(BFC2, 0x83bfc7u) \
  X(BFC7, 0x83bfceu) \
  X(BFCE, 0x83bfd0u) \
  X(BFD0, 0x83bfd2u) \
  X(BFD2, 0x83bfd6u) \
  X(BFD6, 0x83bfdbu) \
  X(BFDB, 0x83bfe2u) \
  X(BFE2, 0x83bfe7u) \
  X(BFE7, 0x83bfeeu) \
  X(BFEE, 0x83bff0u) \
  X(BFF0, 0x83bff2u) \
  X(BFF2, 0x83bff6u) \
  X(BFF6, 0x83bffbu) \
  X(BFFB, 0x83c001u) \
  X(C001, 0x83c008u) \
  X(C008, 0x83c019u) \
  X(C019, 0x83c01au) \
  X(C01A, 0x83c030u) \
  X(C030, 0x83c033u) \
  X(C033, 0x83c038u) \
  X(C038, 0x83c03bu) \
  X(C03B, 0x83c03eu) \
  X(C03E, 0x83c040u) \
  X(C040, 0x83c043u) \
  X(C043, 0x83c04du) \
  X(C04D, 0x83c052u) \
  X(C052, 0x83c057u) \
  X(C057, 0x83c05eu) \
  X(C05E, 0x83c060u) \
  X(C060, 0x83c063u) \
  X(C063, 0x83c06bu) \
  X(C06B, 0x83c06eu) \
  X(C06E, 0x83c070u) \
  X(C070, 0x83c074u) \
  X(C074, 0x83c079u) \
  X(C079, 0x83c07cu) \
  X(C07C, 0x83c088u) \
  X(C088, 0x83c08au) \
  X(C08A, 0x83c091u) \
  X(C091, 0x83c096u) \
  X(C096, 0x83c09bu) \
  X(C09B, 0x83c09du) \
  X(C09D, 0x83c0a1u) \
  X(C0A1, 0x83c0a5u) \
  X(C0A5, 0x83c0a8u) \
  X(C0A8, 0x83c0aau) \
  X(C0AA, 0x83c0b2u) \
  X(C0B2, 0x83c0b3u) \
  X(C0B3, 0x83c0b6u) \
  X(C0B6, 0x83c0b9u) \
  X(C0B9, 0x83c0c1u) \
  X(C0C1, 0x83c0c5u) \
  X(C0C5, 0x83c0cau) \
  X(C0CA, 0x83c0ccu) \
  X(C0CC, 0x83c0ceu) \
  X(C0CE, 0x83c0d4u) \
  X(C0D4, 0x83c0deu) \
  X(C0DE, 0x83c0e3u) \
  X(C0E3, 0x83c0e8u) \
  X(C0E8, 0x83c0fcu) \
  X(C0FC, 0x83c0feu) \
  X(C0FE, 0x83c104u) \
  X(C104, 0x83c109u) \
  X(C109, 0x83c10cu) \
  X(C10C, 0x83c110u) \
  X(C110, 0x83c118u) \
  X(C118, 0x83c11cu) \
  X(C11C, 0x83c121u) \
  X(C121, 0x83c124u) \
  X(C124, 0x83c12bu) \
  X(C12B, 0x83c139u) \
  X(C139, 0x83c144u) \
  X(C144, 0x83c14cu) \
  X(C14C, 0x83c150u) \
  X(C150, 0x83c155u) \
  X(C155, 0x83c172u) \
  X(C176, 0x83c180u) \
  X(C180, 0x83c185u) \
  X(C185, 0x83c188u) \
  X(C188, 0x83c199u) \
  X(C199, 0x83c19eu) \
  X(C19E, 0x83c1a1u) \
  X(C1A1, 0x83c1a8u) \
  X(C1A8, 0x83c1b2u) \
  X(C1B2, 0x83c1b7u) \
  X(C1B7, 0x83c1bcu) \
  X(C1BC, 0x83c1c6u) \
  X(C1C6, 0x83c1c9u) \
  X(C1C9, 0x83c1d9u) \
  X(C1D9, 0x83c1dcu) \
  X(C1DC, 0x83c1e7u) \
  X(C1E7, 0x83c1eeu) \
  X(C1EE, 0x83c1f1u) \
  X(C1F1, 0x83c1f5u) \
  X(C1F5, 0x83c1fau) \
  X(C1FA, 0x83c217u) \
  X(C21B, 0x83c225u) \
  X(C225, 0x83c22au) \
  X(C22A, 0x83c22cu) \
  X(C22C, 0x83c22fu) \
  X(C22F, 0x83c23eu) \
  X(C23E, 0x83c243u) \
  X(C243, 0x83c24au) \
  X(C24A, 0x83c256u) \
  X(C256, 0x83c259u) \
  X(C259, 0x83c25eu) \
  X(C25E, 0x83c263u) \
  X(C263, 0x83c266u) \
  X(C266, 0x83c269u) \
  X(C269, 0x83c26bu) \
  X(C26B, 0x83c26eu) \
  X(C26E, 0x83c278u) \
  X(C278, 0x83c27du) \
  X(C27D, 0x83c282u) \
  X(C282, 0x83c289u) \
  X(C289, 0x83c28bu) \
  X(C28B, 0x83c28eu) \
  X(C28E, 0x83c298u) \
  X(C298, 0x83c2a1u) \
  X(C2A1, 0x83c2a3u) \
  X(C2A3, 0x83c2a7u) \
  X(C2A7, 0x83c2acu) \
  X(C2AC, 0x83c2afu) \
  X(C2AF, 0x83c2bbu) \
  X(C2BB, 0x83c2bdu) \
  X(C2BD, 0x83c2c4u) \
  X(C2C4, 0x83c2c9u) \
  X(C2C9, 0x83c2ceu) \
  X(C2CE, 0x83c2d0u) \
  X(C2D0, 0x83c2d4u) \
  X(C2D4, 0x83c2d8u) \
  X(C2D8, 0x83c2dcu) \
  X(C2DC, 0x83c2e0u) \
  X(C2E0, 0x83c2e4u) \
  X(C2E4, 0x83c2e8u) \
  X(C2E8, 0x83c2ebu) \
  X(C2EB, 0x83c2edu) \
  X(C2ED, 0x83c2f5u) \
  X(C2F5, 0x83c2f6u) \
  X(C2F6, 0x83c2f9u) \
  X(C2FC, 0x83c308u) \
  X(C308, 0x83c30cu) \
  X(C30C, 0x83c311u) \
  X(C311, 0x83c313u) \
  X(C313, 0x83c315u) \
  X(C315, 0x83c31bu) \
  X(C31B, 0x83c325u) \
  X(C325, 0x83c32au) \
  X(C32A, 0x83c32fu) \
  X(C32F, 0x83c343u) \
  X(C343, 0x83c347u) \
  X(C347, 0x83c34du) \
  X(C34D, 0x83c352u) \
  X(C352, 0x83c355u) \
  X(C355, 0x83c359u) \
  X(C359, 0x83c35cu) \
  X(C35C, 0x83c364u) \
  X(C364, 0x83c368u) \
  X(C368, 0x83c36eu) \
  X(C36E, 0x83c371u) \
  X(C371, 0x83c37eu) \
  X(C37E, 0x83c381u) \
  X(C381, 0x83c38au) \
  X(C38A, 0x83c38du) \
  X(C38D, 0x83c398u) \
  X(C398, 0x83c3a2u) \
  X(C3A2, 0x83c3a6u) \
  X(C3A6, 0x83c3b0u) \
  X(C3B0, 0x83c3b6u) \
  X(C3B6, 0x83c3ceu) \
  X(C3D2, 0x83c3e0u) \
  X(C3E4, 0x83c3e9u) \
  X(C3E9, 0x83c3ecu) \
  X(C3EC, 0x83c3fdu) \
  X(C3FD, 0x83c402u) \
  X(C402, 0x83c405u) \
  X(C405, 0x83c40cu) \
  X(C40C, 0x83c416u) \
  X(C416, 0x83c41bu) \
  X(C41B, 0x83c420u) \
  X(C420, 0x83c42au) \
  X(C42A, 0x83c42eu) \
  X(C42E, 0x83c439u) \
  X(C439, 0x83c43cu) \
  X(C43C, 0x83c445u) \
  X(C445, 0x83c448u) \
  X(C448, 0x83c453u) \
  X(C453, 0x83c45au) \
  X(C45A, 0x83c45du) \
  X(C45D, 0x83c461u) \
  X(C461, 0x83c46bu) \
  X(C46B, 0x83c471u) \
  X(C471, 0x83c489u) \
  X(C48D, 0x83c49bu) \
  X(C49F, 0x83c4a4u) \
  X(C4A4, 0x83c4a6u) \
  X(C4A6, 0x83c4a9u) \
  X(C4A9, 0x83c4afu) \
  X(C4AF, 0x83c4b3u) \
  X(C4B3, 0x83c4cfu) \
  X(C4CF, 0x83c4e5u) \
  X(C4E5, 0x83c4ebu) \
  X(C4EB, 0x83c4f5u) \
  X(C4F5, 0x83c4fau) \
  X(C4FA, 0x83c501u) \
  X(C501, 0x83c507u) \
  X(C507, 0x83c518u) \
  X(C518, 0x83c524u) \
  X(C524, 0x83c529u) \
  X(C529, 0x83c52du) \
  X(C52D, 0x83c530u) \
  X(C530, 0x83c53du) \
  X(C53D, 0x83c542u) \
  X(C542, 0x83c557u) \
  X(C557, 0x83c559u) \
  X(C559, 0x83c55du) \
  X(C55D, 0x83c560u) \
  X(C560, 0x83c564u) \
  X(C564, 0x83c56bu) \
  X(C56B, 0x83c571u) \
  X(C571, 0x83c574u) \
  X(C574, 0x83c577u) \
  X(C577, 0x83c57cu) \
  X(C580, 0x83c5cdu) \
  X(C5CD, 0x83c5ceu) \
  X(C5E6, 0x83c5ecu) \
  X(C5F0, 0x83c5f6u) \
  X(C5FA, 0x83c618u) \
  X(C61C, 0x83c61fu) \
  X(C623, 0x83c628u) \
  X(C628, 0x83c657u) \
  X(C657, 0x83c65au) \
  X(C65A, 0x83c65du) \
  X(C65D, 0x83c660u) \
  X(C664, 0x83c66au) \
  X(C66E, 0x83c670u) \
  X(C670, 0x83c678u) \
  X(C67C, 0x83c682u) \
  X(C686, 0x83c687u) \
  X(C687, 0x83c694u) \
  X(C694, 0x83c697u) \
  X(C69B, 0x83c69eu) \
  X(C69E, 0x83c6a1u) \
  X(C6A1, 0x83c6a4u) \
  X(C6A4, 0x83c6a7u) \
  X(C6A7, 0x83c6aau) \
  X(C6AA, 0x83c6b4u) \
  X(C6B4, 0x83c6b7u) \
  X(C6B7, 0x83c6bau) \
  X(C6BE, 0x83c6c1u) \
  X(C6C1, 0x83c6c4u) \
  X(C6C4, 0x83c6c8u) \
  X(C6C8, 0x83c6ccu) \
  X(C6CC, 0x83c6cfu) \
  X(C6D3, 0x83c6d5u) \
  X(C6D5, 0x83c6dcu) \
  X(C6E0, 0x83c6e3u) \
  X(C6E3, 0x83c6efu) \
  X(C6EF, 0x83c6f2u) \
  X(C6F3, 0x83c6fau) \
  X(C6FE, 0x83c701u) \
  X(C701, 0x83c706u) \
  X(C706, 0x83c709u) \
  X(C709, 0x83c70cu) \
  X(C70C, 0x83c71fu) \
  X(C723, 0x83c725u) \
  X(C725, 0x83c728u) \
  X(C72C, 0x83c72fu) \
  X(C733, 0x83c739u) \
  X(C739, 0x83c746u) \
  X(C746, 0x83c748u) \
  X(C74C, 0x83c74fu) \
  X(C74F, 0x83c752u) \
  X(C752, 0x83c755u) \
  X(C759, 0x83c75cu) \
  X(C75C, 0x83c75fu) \
  X(C75F, 0x83c762u) \
  X(C766, 0x83c76cu) \
  X(C76C, 0x83c76fu) \
  X(C76F, 0x83c772u) \
  X(C776, 0x83c779u) \
  X(C779, 0x83c77cu) \
  X(C77C, 0x83c77fu) \
  X(C783, 0x83c789u) \
  X(C789, 0x83c78cu) \
  X(C78C, 0x83c78fu) \
  X(C797, 0x83c7d2u) \
  X(C7D6, 0x83c7d7u) \
  X(C7D7, 0x83c7dfu) \
  X(C7DF, 0x83c7e2u) \
  X(C7E6, 0x83c7e7u) \
  X(C7E7, 0x83c7eeu) \
  X(C7EE, 0x83c7f5u) \
  X(C7F5, 0x83c7ffu) \
  X(C803, 0x83c806u) \
  X(C80A, 0x83c80cu) \
  X(C80C, 0x83c80du) \
  X(C80D, 0x83c814u) \
  X(C814, 0x83c81eu) \
  X(C81E, 0x83c828u) \
  X(C82C, 0x83c82fu) \
  X(C833, 0x83c835u) \
  X(C835, 0x83c836u) \
  X(C836, 0x83c83bu) \
  X(C83B, 0x83c853u) \
  X(C857, 0x83c85du) \
  X(C861, 0x83c864u) \
  X(C868, 0x83c86eu) \
  X(C872, 0x83c875u) \
  X(C879, 0x83c883u) \
  X(C883, 0x83c88fu) \
  X(C88F, 0x83c895u) \
  X(C899, 0x83c8a5u) \
  X(C8A9, 0x83c8b0u) \
  X(C8B0, 0x83c8b6u) \
  X(C8BA, 0x83c8bcu) \
  X(C8BC, 0x83c8d7u) \
  X(C8DB, 0x83c8e1u) \
  X(C8E5, 0x83c8e9u) \
  X(C8E9, 0x83c8ecu) \
  X(C8F4, 0x83c8feu) \
  X(C8FE, 0x83c904u) \
  X(C908, 0x83c909u) \
  X(C966, 0x83c96bu) \
  X(C96B, 0x83c96fu) \
  X(C973, 0x83c97bu) \
  X(C97B, 0x83c97cu) \
  X(C97C, 0x83c982u) \
  X(C986, 0x83c98fu) \
  X(C993, 0x83c994u) \
  X(CA4A, 0x83ca57u) \
  X(CA57, 0x83ca68u) \
  X(CA68, 0x83ca6bu) \
  X(CA6F, 0x83ca72u) \
  X(CA72, 0x83ca76u) \
  X(CA76, 0x83ca81u) \
  X(CA81, 0x83ca84u) \
  X(CA88, 0x83ca8bu) \
  X(CA8B, 0x83ca8eu) \
  X(CA8E, 0x83ca92u) \
  X(CA92, 0x83ca9du) \
  X(CAA1, 0x83caadu) \
  X(CAAD, 0x83caafu) \
  X(CAC6, 0x83caceu) \
  X(CACE, 0x83cad2u) \
  X(CAD6, 0x83cad9u) \
  X(CADD, 0x83cae9u) \
  X(CAE9, 0x83caedu) \
  X(CAED, 0x83caf2u) \
  X(CAF2, 0x83caf4u) \
  X(CAF4, 0x83cafdu) \
  X(CAFD, 0x83cb01u) \
  X(CB01, 0x83cb06u) \
  X(CB06, 0x83cb08u) \
  X(CB08, 0x83cb1au) \
  X(CB1E, 0x83cb20u) \
  X(CB20, 0x83cb30u) \
  X(CB30, 0x83cb31u) \
  X(CB31, 0x83cb34u) \
  X(CB58, 0x83cb5cu) \
  X(CB5C, 0x83cb73u) \
  X(CB73, 0x83cb74u) \
  X(CB81, 0x83cbd9u) \
  X(CBDD, 0x83cbdfu) \
  X(80E0, 0x8280e4u)

typedef enum {
#define X(at, end) DR_TONGUE_RUN_##at,
  DR_TONGUE_RUNS(X)
#undef X
  DR_TONGUE_RUN_TAKEN,      // a branch taken: one idle more
  DR_TONGUE_RUN_RNG_ONCE,   // `rng_next`, its counter bumped once...
  DR_TONGUE_RUN_RNG_TWICE,  // ...or twice
  DR_TONGUE_RUN_WIDE,       // the wide test's blocks, `TerrainWideBlock` on
  DR_TONGUE_RUN_COUNT = DR_TONGUE_RUN_WIDE + WIDE_BLOCK_COUNT
} DrTongueRun;

// The longest stretch is a frame of a charge: two steps, each two probes.
#define DR_TONGUE_TRACE_STEPS 512

typedef struct {
  HwStep steps[DR_TONGUE_TRACE_STEPS];
  HwTrace trace;   // runs and branches taken, in order: it writes no register
  bool lost;       // a return to an address the thread does not know
  bool v_unknown;  // overflow is the wide test's, which it does not say
} DrTongueWork;

void dr_tongue_work_init(DrTongueWork* k);

// A stretch of either thread, from `c->pc` to the instruction it stops on,
// which it leaves in `c->pc`.
void dr_tongue_run(Wram* w, const Rom* rom, PortCpu* c, DrTongueWork* k);

// `$83:C994`, the head's handler, and `$83:CC03`, its shot's, as
// `thread_call_handler` calls them. Every way out is ported: they are always
// served.
bool dr_tongue_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                       ActorHandlerRegs* r, HandlerPieces* pieces);
bool dr_tongue_shot_collide(Wram* w, uint16_t dp, uint16_t arg,
                            ActorHandlerRegs* r, HandlerPieces* pieces);

#endif
