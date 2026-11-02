// The giant spider: the boss of levels 36 and 48, from when a player comes
// near it until it is dead.
//
// Both levels' records start the thread at `$83:AD33` at a point of their
// own: (280, 210) on level 36, (210, 350) on level 48. It waits there for a
// player within 250 (`near_wait`, `port/waits.h`), and this is the rest of it.
// It is a red spider bigger than the screen is tall, drawn on the big
// figures' layer (`port/bossbg.h`), and the records about it are its parts.
//
// **It comes up** (`$83:AD36`). Its weight, 40, goes on the level's load
// (`$DE`, which `spawn_has_room` reads). Its colours are set, both rows
// (`port/figure_colours.h`), and its tiles go up, `$2800` bytes from
// `$92:A800` to VRAM `$5000` by `boss_bg_tiles_job` (`port/vblank.h`), a
// tick at a time until they are through. Then `$83:AD98`: a record of its
// own at the point, two pictures by turns every forty ticks, until a player
// is within 65 of it (`player_bearing`); every 225 ticks a sound, `$27`,
// while `$1F52` is 3. Then two sounds and two lists of pictures played, and
// its picture 4 goes up on its layer, at its place less 17 across and 18
// down. With another sound the record shows two pictures, twelve ticks each.
// The first layer's mosaic then goes a step coarser every five ticks for
// fifteen, with a puff (`$83:B1DD`) at one of twelve places about it by a
// draw of 200 in 256 each time. Its picture 0 goes up, the record is freed,
// and the mosaic goes fine again in fifteen more, with puffs by a draw of
// 160: it has come up out of the floor.
//
// **Then it is six records** (`$83:AD16`): its parts, about its place from
// `$83:A40E`, which are what the players' shots and the players touch. It
// has 20 health, waits half a second, and its handler is set
// (`giant_spider_collide`, below).
//
// **Each frame** after that it sleeps a tick, and:
//
//     $83:A541  the next of its four pictures, every `$4C` frames
//     $83:A580  one of its parts plays a little script; or, if a hit has
//               broken it, its next life (below)
//     $83:A674  its three webs, each a record of its own
//     $83:A537  its picture on its layer, when it differs and the last has
//               gone up; and its flash, two frames, when it was hit
//     its state, the routine at `$44`
//
// **The states.**
//
//     $83:AADC  three frames still, then a way at random of eight
//     $83:AAFC  going that way two pixels a frame. Where the edge of the map
//               (`terrain_out_of_bounds`) or the ground (`$83:A97C`, the wide
//               test) stops one of three probes ahead of it, it stands and
//               chooses again. Every 32 ticks each probe tells a box about
//               itself (`actor_notify_box`). Then, by a draw of 4 in 256,
//               spiders; or by a draw of 25, webs; or on its way
//     $83:AB58  with fewer than 15 records about, its mouth plays a script,
//               and at the end two small spiders, one on level 36, are
//               begun about it (`$83:B277`, `port/spider.h`)
//     $83:AC13  webs at the nearest player within 190, if there is one:
//               three, one at once and one every eighth frame after, each
//               the next way round of eight from the way the player was
//
// **A web** (`$83:A79D`) flies its way at the speed `$83:A814` gives it for
// as many frames as the player was far, over eight. Then it lands and
// spreads for sixteen frames: every fourth a picture and a box told about it.
// Then each of four tiles about it that may take one (bit 12 of its
// attributes, `tile_attrs_at_tile`) is put down as a web tile, one for level
// 36 and another for anywhere else, and the record is freed. Those are the
// webs it leaves on the floor.
//
// **Its lives.** Its handler counts hits down from its health by the damage
// table, and under nought it is broken. The first four times a part of it
// goes (its pictures cleared), its colours are another set's, and it has 20
// health again; the fifth time 45. Each is ten less on level 36. The sixth
// time it dies.
//
// **Its death.** Once its webs are gone, whoever killed it is paid 20,000
// (`score_add`), its six records are freed and its weight comes off the load.
// Twelve puffs about it with its colours flashing; the mosaic coarser in
// fifteen steps of two ticks, puffs and flashes by turns; five small spiders
// about it; a third of a second. Then on level 36 a gold sparkle at its place
// (`$83:B55E`), tried every tick until there is room, and on level 48, once
// there are fewer than 18 records about, what comes after it, `$83:C687`,
// which is `port/dr_tongue.h`. Its layer is blanked (`$82:8113`), `$7E:6058` (a
// Snakeoid is up) is cleared, on level 36 the map's block at (6, 1) is put
// down as the block after it (`blockmap_cell_ptr`, `tile_block_begin`), the
// mosaic is ended, and the thread ends.
//
// **Its handler** (`$83:B13D`) takes no hit while it flashes, nor on a part
// that wears nothing or id 9 (`$0078` is the record touched), nor below a
// player's shot's id. It keeps what hit it at `$5A`, to pay. `$60` and `$70` are `$5D` seven
// ticks in eight and `$5C` the eighth; `$6F` the same but `$63` on the eighth
// when bit 3 of the tick is set; `$61` and `$62` are `$5C`. The hit flashes it
// (`$56`), adds its damage to `$64`, which at 5 makes it stand and choose a
// new way (`$83:AAE1`), and takes the damage off its health. It returns with
// carry set, which parks its thread (`thread_call_handler`).
//
// **Written as blocks**, as `port/cast.h` is. A stretch begins where a call
// or a sleep comes back and runs block after block until the next call the
// harness makes, or the `RTL`. Its own `JSR`s are made here, and an `RTS`
// goes where the stack says. A return to an address the thread does not know
// turns the stretch down. It makes three calls itself, whose cost is known:
// the draws (`rng_next`), whether the last picture has gone up (`$82:80E0`),
// and the wide test, `$83:A97C`, which is the Snakeoid's `$82:A214` to the
// byte. The layer's blanking, `$82:8113`, is a call the harness makes
// (`boss_bg_clear`, `port/bossbg.h`), as it is for Dr. Tongue's head.
//
// The thread runs with data bank `$83`, its own.
//
// Its two mosaic jobs, `$83:B0EC` and `$83:B100`, are the saucer's
// `mosaic_job` and `mosaic_off_job` to the byte, and `port/vblank.h` runs
// them.
//
// Port code: libc only.

#ifndef PORT_GIANT_SPIDER_H
#define PORT_GIANT_SPIDER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/collide.h"
#include "port/cpu.h"
#include "port/hw.h"
#include "port/terrain.h"
#include "port/wram.h"

#define GIANT_SPIDER_BANK 0x83u
#define GIANT_SPIDER_PC 0x83ad33u
#define GIANT_SPIDER_WOKEN_PC 0x83ad36u  // where `near_wait` returns to
#define GIANT_SPIDER_COLLIDE_ENTRY 0x83b13du

// Its jobs: its tiles sent, and the mosaic (`port/vblank.h`).
#define GIANT_SPIDER_TILES_JOB_PC 0x828259u
#define GIANT_SPIDER_MOSAIC_JOB_PC 0x83b0ecu
#define GIANT_SPIDER_MOSAIC_JOB_RTL_PC 0x83b0ffu
#define GIANT_SPIDER_MOSAIC_OFF_JOB_PC 0x83b100u
#define GIANT_SPIDER_MOSAIC_OFF_JOB_RTL_PC 0x83b108u

// Its page.
#define GIANT_SPIDER_DP_RECORD 0x08    // and five more after it, to `$12`
#define GIANT_SPIDER_DP_LIVES 0x14     // 8, 6, 4, 2, 0: which part goes next
#define GIANT_SPIDER_DP_WEBS 0x16     // how many are out, of three
#define GIANT_SPIDER_DP_WEB 0x18      // three of these four words, 8 apart
#define GIANT_SPIDER_DP_X 0x3c
#define GIANT_SPIDER_DP_Y 0x3e
#define GIANT_SPIDER_DP_STATE 0x44
#define GIANT_SPIDER_DP_BROKEN 0x52    // not nought: its health went under
#define GIANT_SPIDER_DP_HEALTH 0x54
#define GIANT_SPIDER_DP_HIT 0x56       // negative: hit, to flash
#define GIANT_SPIDER_DP_FLASH 0x58     // frames of the flash left
#define GIANT_SPIDER_DP_HIT_BY 0x5a
#define GIANT_SPIDER_DP_STUNG 0x64     // damage since it last chose a way

#define GIANT_SPIDER_WEB_NONE 0xffffu

// Where a stretch can begin: after each call the harness makes.
#define GIANT_SPIDER_ENTRIES(X) \
  X(woken, "$83:AD36", 0x83ad36u) \
  X(coloured, "$83:ACAF", 0x83acafu) \
  X(sprite_coloured, "$83:ACB9", 0x83acb9u) \
  X(tiles_asked, "$83:ACDB", 0x83acdbu) \
  X(tiles_waited, "$83:ACE2", 0x83ace2u) \
  X(record_made, "$83:AF03", 0x83af03u) \
  X(record_handled, "$83:AF42", 0x83af42u) \
  X(looked, "$83:ADA8", 0x83ada8u) \
  X(blink_heard, "$83:AF52", 0x83af52u) \
  X(blinked, "$83:ADCD", 0x83adcdu) \
  X(cry_heard, "$83:ADD6", 0x83add6u) \
  X(rise_played, "$83:ADDD", 0x83adddu) \
  X(cry2_heard, "$83:ADE4", 0x83ade4u) \
  X(rise2_played, "$83:ADEB", 0x83adebu) \
  X(picture_up, "$83:A436", 0x83a436u) \
  X(picture_flip_up, "$83:A447", 0x83a447u) \
  X(turn_heard, "$83:AE12", 0x83ae12u) \
  X(turned, "$83:AE2D", 0x83ae2du) \
  X(coarser_asked, "$83:AE4F", 0x83ae4fu) \
  X(coarser_waited, "$83:AE5F", 0x83ae5fu) \
  X(puff_room, "$83:AE81", 0x83ae81u) \
  X(puff_begun, "$83:AE8D", 0x83ae8du) \
  X(unflashed, "$83:A4C4", 0x83a4c4u) \
  X(flashed, "$83:A4DE", 0x83a4deu) \
  X(record_freed, "$83:AE9E", 0x83ae9eu) \
  X(finer_asked, "$83:AEB4", 0x83aeb4u) \
  X(finer_waited, "$83:AEC4", 0x83aec4u) \
  X(puff2_room, "$83:AEE6", 0x83aee6u) \
  X(puff2_begun, "$83:AEF2", 0x83aef2u) \
  X(part_made, "$83:AC3F", 0x83ac3fu) \
  X(parts_waited, "$83:AD28", 0x83ad28u) \
  X(handled, "$83:AD32", 0x83ad32u) \
  X(frame, "$83:AD60", 0x83ad60u) \
  X(life_coloured, "$83:A612", 0x83a612u) \
  X(web_told, "$83:AAB1", 0x83aab1u) \
  X(tile_read, "$83:A71C", 0x83a71cu) \
  X(tile_put, "$83:A73B", 0x83a73bu) \
  X(web_freed, "$83:A752", 0x83a752u) \
  X(webs_waited, "$83:AD78", 0x83ad78u) \
  X(paid, "$83:AD85", 0x83ad85u) \
  X(part_freed, "$83:B116", 0x83b116u) \
  X(death_room, "$83:AF6E", 0x83af6eu) \
  X(death_puff, "$83:AF7A", 0x83af7au) \
  X(death_flashed, "$83:AF84", 0x83af84u) \
  X(death_flash_waited, "$83:AF8B", 0x83af8bu) \
  X(death_unflashed, "$83:AF95", 0x83af95u) \
  X(death_unflash_waited, "$83:AF9C", 0x83af9cu) \
  X(death_coarser, "$83:AFBC", 0x83afbcu) \
  X(death_coarser_waited, "$83:AFCC", 0x83afccu) \
  X(death_flashed2, "$83:AFDD", 0x83afddu) \
  X(death_room2, "$83:AFF8", 0x83aff8u) \
  X(death_puff2, "$83:B004", 0x83b004u) \
  X(death_unflashed2, "$83:B00E", 0x83b00eu) \
  X(spider_room, "$83:B02D", 0x83b02du) \
  X(spider_begun, "$83:B039", 0x83b039u) \
  X(death_waited, "$83:B04A", 0x83b04au) \
  X(sparkle_waited, "$83:B061", 0x83b061u) \
  X(sparkle_begun, "$83:B06B", 0x83b06bu) \
  X(after_waited, "$83:B07E", 0x83b07eu) \
  X(after_begun, "$83:B08A", 0x83b08au) \
  X(blanked, "$83:B097", 0x83b097u) \
  X(tile_looked, "$83:B131", 0x83b131u) \
  X(tile_set, "$83:B13C", 0x83b13cu) \
  X(mosaic_ended, "$83:B0AB", 0x83b0abu) \
  X(bearing, "$83:A881", 0x83a881u) \
  X(ground, "$83:A924", 0x83a924u) \
  X(spiders_room, "$83:AB8C", 0x83ab8cu) \
  X(spiders_begun, "$83:AB98", 0x83ab98u) \
  X(web_made, "$83:A7BE", 0x83a7beu)

// Where one stops: a call the harness makes, or the `RTL`.
#define GIANT_SPIDER_EXITS(X) \
  X(0x83acabu) /* JSL figure_colours_set */ \
  X(0x83acb5u) /* JSL figure_sprite_colours_set */ \
  X(0x83acd7u) /* JSL vbl_queue_a_add */ \
  X(0x83acdeu) /* JSL thread_yield */ \
  X(0x83aeffu) /* JSL actor_slot_alloc */ \
  X(0x83af3eu) /* JSL thread_set_handler */ \
  X(0x83ada4u) /* JSL player_bearing */ \
  X(0x83af4eu) /* JSL apu_play_sfx */ \
  X(0x83adc9u) /* JSL thread_yield */ \
  X(0x83add2u) /* JSL apu_play_sfx */ \
  X(0x83add9u) /* JSL pictures_play */ \
  X(0x83ade0u) /* JSL apu_play_sfx */ \
  X(0x83ade7u) /* JSL pictures_play */ \
  X(0x83a432u) /* JSL boss_bg_queue */ \
  X(0x83a443u) /* JSL boss_bg_queue_flip */ \
  X(0x83ae0eu) /* JSL apu_play_sfx */ \
  X(0x83ae29u) /* JSL thread_yield */ \
  X(0x83ae4bu) /* JSL vbl_queue_a_add */ \
  X(0x83ae5bu) /* JSL thread_yield */ \
  X(0x83ae7du) /* JSL spawn_has_room */ \
  X(0x83ae89u) /* JSL thread_spawn */ \
  X(0x83a4c0u) /* JSL figure_colours_set */ \
  X(0x83a4dau) /* JSL figure_colours_set */ \
  X(0x83ae9au) /* JSL actor_slot_free */ \
  X(0x83aeb0u) /* JSL vbl_queue_a_add */ \
  X(0x83aec0u) /* JSL thread_yield */ \
  X(0x83aee2u) /* JSL spawn_has_room */ \
  X(0x83aeeeu) /* JSL thread_spawn */ \
  X(0x83ac3bu) /* JSL actor_slot_alloc */ \
  X(0x83ad24u) /* JSL thread_yield */ \
  X(0x83ad2eu) /* JSL thread_set_handler */ \
  X(0x83ad5cu) /* JSL thread_yield */ \
  X(0x83a60eu) /* JSL figure_colours_set */ \
  X(0x83aaadu) /* JSL actor_notify_box */ \
  X(0x83a718u) /* JSL tile_attrs_at_tile */ \
  X(0x83a737u) /* JSL map_tile_put */ \
  X(0x83a74eu) /* JSL actor_slot_free */ \
  X(0x83ad74u) /* JSL thread_yield */ \
  X(0x83ad81u) /* JSL score_add */ \
  X(0x83b112u) /* JSL actor_slot_free */ \
  X(0x83af6au) /* JSL spawn_has_room */ \
  X(0x83af76u) /* JSL thread_spawn */ \
  X(0x83af80u) /* JSL figure_colours_set */ \
  X(0x83af87u) /* JSL thread_yield */ \
  X(0x83af91u) /* JSL figure_colours_set */ \
  X(0x83af98u) /* JSL thread_yield */ \
  X(0x83afb8u) /* JSL vbl_queue_a_add */ \
  X(0x83afc8u) /* JSL thread_yield */ \
  X(0x83afd9u) /* JSL figure_colours_set */ \
  X(0x83aff4u) /* JSL spawn_has_room */ \
  X(0x83b000u) /* JSL thread_spawn */ \
  X(0x83b00au) /* JSL figure_colours_set */ \
  X(0x83b029u) /* JSL spawn_has_room */ \
  X(0x83b035u) /* JSL thread_spawn */ \
  X(0x83b046u) /* JSL thread_yield */ \
  X(0x83b05du) /* JSL thread_yield */ \
  X(0x83b067u) /* JSL thread_spawn */ \
  X(0x83b07au) /* JSL thread_yield */ \
  X(0x83b086u) /* JSL thread_spawn */ \
  X(0x83b08fu) /* JSL thread_yield */ \
  X(0x83b093u) /* JSL boss_bg_clear */ \
  X(0x83b12du) /* JSL blockmap_cell_ptr */ \
  X(0x83b138u) /* JSL tile_block_begin */ \
  X(0x83b0a7u) /* JSL vbl_queue_a_add */ \
  X(0x83a87du) /* JSL actor_nearest */ \
  X(0x83a920u) /* JSL terrain_out_of_bounds */ \
  X(0x83ab88u) /* JSL spawn_has_room */ \
  X(0x83ab94u) /* JSL thread_spawn */ \
  X(0x83a7bau) /* JSL actor_slot_alloc */ \
  X(0x83ad97u) /* RTL */

// The straight runs of the listing, each named for where it starts, with
// where it ends. `tools/price_runs.py` prices them, the four that read a
// script through a pointer with `--ind`.
#define GIANT_SPIDER_RUNS(X) \
  X(AD36, 0x83ad43u) \
  X(ACA5, 0x83acabu) \
  X(ACAF, 0x83acb5u) \
  X(ACB9, 0x83acd7u) \
  X(ACDB, 0x83acdeu) \
  X(ACE2, 0x83ace7u) \
  X(ACE7, 0x83ad16u) \
  X(AD43, 0x83ad46u) \
  X(AD98, 0x83ad9bu) \
  X(AF03, 0x83af3eu) \
  X(AF42, 0x83af43u) \
  X(AD9B, 0x83ad9du) \
  X(AD9D, 0x83ada4u) \
  X(ADA8, 0x83adabu) \
  X(ADAB, 0x83adb0u) \
  X(ADB0, 0x83adb3u) \
  X(AF43, 0x83af4bu) \
  X(AF4B, 0x83af4eu) \
  X(AF52, 0x83af53u) \
  X(ADB3, 0x83adb6u) \
  X(ADB6, 0x83adc9u) \
  X(ADCD, 0x83adcfu) \
  X(ADCF, 0x83add2u) \
  X(ADD6, 0x83add9u) \
  X(ADDD, 0x83ade0u) \
  X(ADE4, 0x83ade7u) \
  X(ADEB, 0x83ae00u) \
  X(A51A, 0x83a537u) \
  X(AE00, 0x83ae06u) \
  X(A448, 0x83a45eu) \
  X(A45E, 0x83a462u) \
  X(A462, 0x83a46fu) \
  X(A46F, 0x83a472u) \
  X(A474, 0x83a477u) \
  X(A426, 0x83a432u) \
  X(A436, 0x83a437u) \
  X(A472, 0x83a474u) \
  X(A477, 0x83a478u) \
  X(A437, 0x83a443u) \
  X(A447, 0x83a448u) \
  X(AE06, 0x83ae0bu) \
  X(AE0B, 0x83ae0eu) \
  X(AE12, 0x83ae17u) \
  X(AE17, 0x83ae29u) \
  X(AE2D, 0x83ae2fu) \
  X(AE2F, 0x83ae32u) \
  X(AE32, 0x83ae3eu) \
  X(AE3E, 0x83ae43u) \
  X(AE43, 0x83ae4bu) \
  X(AE4F, 0x83ae5bu) \
  X(AE5F, 0x83ae63u) \
  X(AE63, 0x83ae68u) \
  X(AE68, 0x83ae7du) \
  X(AE81, 0x83ae83u) \
  X(AE83, 0x83ae89u) \
  X(AE8D, 0x83ae8fu) \
  X(AE8F, 0x83ae92u) \
  X(A537, 0x83a53au) \
  X(A4F1, 0x83a4ffu) \
  X(80E0, 0x8280e4u) \
  X(A4FF, 0x83a501u) \
  X(A501, 0x83a507u) \
  X(A507, 0x83a511u) \
  X(A511, 0x83a512u) \
  X(A53A, 0x83a53du) \
  X(A4A0, 0x83a4a4u) \
  X(A4A4, 0x83a4a8u) \
  X(A4A8, 0x83a4abu) \
  X(A4AB, 0x83a4bau) \
  X(A4BA, 0x83a4c0u) \
  X(A4C4, 0x83a4c5u) \
  X(A4C5, 0x83a4c9u) \
  X(A4C9, 0x83a4d4u) \
  X(A4D4, 0x83a4dau) \
  X(A4DE, 0x83a4e1u) \
  X(A4E1, 0x83a4f0u) \
  X(A4F0, 0x83a4f1u) \
  X(A53D, 0x83a540u) \
  X(A540, 0x83a541u) \
  X(AE92, 0x83ae98u) \
  X(AE98, 0x83ae9au) \
  X(AE9E, 0x83aea3u) \
  X(AEA3, 0x83aea8u) \
  X(AEA8, 0x83aeb0u) \
  X(AEB4, 0x83aec0u) \
  X(AEC4, 0x83aec8u) \
  X(AEC8, 0x83aecdu) \
  X(AECD, 0x83aee2u) \
  X(AEE6, 0x83aee8u) \
  X(AEE8, 0x83aeeeu) \
  X(AEF2, 0x83aef4u) \
  X(AEF4, 0x83aef7u) \
  X(AD46, 0x83ad49u) \
  X(AD16, 0x83ad19u) \
  X(A661, 0x83a669u) \
  X(A669, 0x83a673u) \
  X(A673, 0x83a674u) \
  X(AD19, 0x83ad1cu) \
  X(AC36, 0x83ac3bu) \
  X(AC3F, 0x83ac8cu) \
  X(AC8C, 0x83ac8du) \
  X(AD1C, 0x83ad24u) \
  X(AD28, 0x83ad2eu) \
  X(AD32, 0x83ad33u) \
  X(AD49, 0x83ad4cu) \
  X(AAD2, 0x83aadcu) \
  X(AADC, 0x83aae0u) \
  X(AAE0, 0x83aae1u) \
  X(AAE1, 0x83aae5u) \
  X(AAE5, 0x83aaefu) \
  X(AAEF, 0x83aafcu) \
  X(AD4C, 0x83ad4fu) \
  X(AD4F, 0x83ad59u) \
  X(AD59, 0x83ad5cu) \
  X(AD60, 0x83ad63u) \
  X(A541, 0x83a545u) \
  X(A545, 0x83a551u) \
  X(A551, 0x83a552u) \
  X(AD63, 0x83ad66u) \
  X(A580, 0x83a584u) \
  X(A584, 0x83a587u) \
  X(A587, 0x83a58bu) \
  X(A58B, 0x83a58fu) \
  X(A58F, 0x83a595u) \
  X(A595, 0x83a5a8u) \
  X(A5A7, 0x83a5a8u) \
  X(A5A8, 0x83a5abu) \
  X(A5AB, 0x83a5afu) \
  X(A5AF, 0x83a5beu) \
  X(A5BE, 0x83a5c9u) \
  X(A5C9, 0x83a5cdu) \
  X(A5CD, 0x83a5cfu) \
  X(A5CF, 0x83a5d2u) \
  X(A5D2, 0x83a5d5u) \
  X(A5FD, 0x83a60eu) \
  X(A5DA, 0x83a5ddu) \
  X(A608, 0x83a60eu) \
  X(A612, 0x83a613u) \
  X(A5D5, 0x83a5dau) \
  X(A5DD, 0x83a5e0u) \
  X(A5E0, 0x83a5eau) \
  X(A5EA, 0x83a5f0u) \
  X(A5F0, 0x83a5fdu) \
  X(AD66, 0x83ad69u) \
  X(A674, 0x83a679u) \
  X(A679, 0x83a682u) \
  X(A682, 0x83a685u) \
  X(A685, 0x83a68fu) \
  X(A68F, 0x83a690u) \
  X(A690, 0x83a696u) \
  X(A696, 0x83a69bu) \
  X(A69B, 0x83a69eu) \
  X(A6A0, 0x83a6a9u) \
  X(A6A9, 0x83a6acu) \
  X(A6AC, 0x83a6bfu) \
  X(AA89, 0x83aaadu) \
  X(AAB1, 0x83aab2u) \
  X(A6BF, 0x83a6c2u) \
  X(A6C2, 0x83a6c7u) \
  X(A6C7, 0x83a6dbu) \
  X(A6DB, 0x83a6ecu) \
  X(A6EC, 0x83a6edu) \
  X(A6ED, 0x83a702u) \
  X(A702, 0x83a718u) \
  X(A71C, 0x83a721u) \
  X(A721, 0x83a72bu) \
  X(A72B, 0x83a72fu) \
  X(A72F, 0x83a737u) \
  X(A73B, 0x83a745u) \
  X(A745, 0x83a74eu) \
  X(A752, 0x83a755u) \
  X(AD69, 0x83ad6du) \
  X(AD6D, 0x83ad71u) \
  X(AD71, 0x83ad74u) \
  X(AD78, 0x83ad7au) \
  X(AD7A, 0x83ad81u) \
  X(AD85, 0x83ad88u) \
  X(B109, 0x83b10eu) \
  X(B10E, 0x83b112u) \
  X(B116, 0x83b11eu) \
  X(B11E, 0x83b11fu) \
  X(AD88, 0x83ad94u) \
  X(AD94, 0x83ad97u) \
  X(AF53, 0x83af58u) \
  X(AF58, 0x83af6au) \
  X(AF6E, 0x83af70u) \
  X(AF70, 0x83af76u) \
  X(AF7A, 0x83af80u) \
  X(AF84, 0x83af87u) \
  X(AF8B, 0x83af91u) \
  X(AF95, 0x83af98u) \
  X(AF9C, 0x83afa6u) \
  X(AFA6, 0x83afb2u) \
  X(AFB2, 0x83afb8u) \
  X(AFBC, 0x83afc8u) \
  X(AFCC, 0x83afd3u) \
  X(AFD3, 0x83afd9u) \
  X(AFDD, 0x83afdfu) \
  X(AFDF, 0x83aff4u) \
  X(AFF8, 0x83affau) \
  X(AFFA, 0x83b000u) \
  X(B004, 0x83b00au) \
  X(B00E, 0x83b012u) \
  X(B012, 0x83b017u) \
  X(B017, 0x83b029u) \
  X(B02D, 0x83b02fu) \
  X(B02F, 0x83b035u) \
  X(B039, 0x83b043u) \
  X(B043, 0x83b046u) \
  X(B04A, 0x83b05au) \
  X(B05A, 0x83b05du) \
  X(B061, 0x83b067u) \
  X(B06B, 0x83b06du) \
  X(B06D, 0x83b06fu) \
  X(B06F, 0x83b077u) \
  X(B077, 0x83b07au) \
  X(B07E, 0x83b080u) \
  X(B080, 0x83b086u) \
  X(B08A, 0x83b08cu) \
  X(B08C, 0x83b08fu) \
  X(B097, 0x83b0a1u) \
  X(B11F, 0x83b127u) \
  X(B127, 0x83b12du) \
  X(B131, 0x83b138u) \
  X(B13C, 0x83b13du) \
  X(B0A1, 0x83b0a7u) \
  X(B0AB, 0x83b0acu) \
  X(AAFC, 0x83ab06u) \
  X(A87B, 0x83a87du) \
  X(A881, 0x83a885u) \
  X(A885, 0x83a88fu) \
  X(A88F, 0x83a894u) \
  X(A894, 0x83a899u) \
  X(A899, 0x83a89eu) \
  X(A89E, 0x83a8a0u) \
  X(A8A0, 0x83a8abu) \
  X(A8B5, 0x83a8c5u) \
  X(A8C5, 0x83a8ccu) \
  X(A8CC, 0x83a8d3u) \
  X(A8D3, 0x83a8d8u) \
  X(A8D8, 0x83a8e0u) \
  X(A8AB, 0x83a8afu) \
  X(A8AF, 0x83a8b5u) \
  X(AB06, 0x83ab1du) \
  X(A8ED, 0x83a8f2u) \
  X(A8F2, 0x83a90cu) \
  X(A90C, 0x83a913u) \
  X(A913, 0x83a91au) \
  X(A91A, 0x83a91cu) \
  X(A91C, 0x83a920u) \
  X(A924, 0x83a926u) \
  X(A926, 0x83a930u) \
  X(A930, 0x83a93du) \
  X(A93D, 0x83a95eu) \
  X(A95E, 0x83a970u) \
  X(A96F, 0x83a970u) \
  X(AB1D, 0x83ab1fu) \
  X(AB1F, 0x83ab22u) \
  X(AB22, 0x83ab26u) \
  X(AB26, 0x83ab2bu) \
  X(AB2B, 0x83ab2fu) \
  X(AB2F, 0x83ab34u) \
  X(AB34, 0x83ab35u) \
  X(AB39, 0x83ab3cu) \
  X(AB3C, 0x83ab3fu) \
  X(AB3F, 0x83ab47u) \
  X(AB47, 0x83ab57u) \
  X(AB57, 0x83ab58u) \
  X(A552, 0x83a557u) \
  X(A557, 0x83a55bu) \
  X(A55B, 0x83a561u) \
  X(A561, 0x83a575u) \
  X(A573, 0x83a575u) \
  X(A575, 0x83a579u) \
  X(A579, 0x83a580u) \
  X(AB58, 0x83ab5bu) \
  X(AB5B, 0x83ab5du) \
  X(AB5D, 0x83ab5eu) \
  X(AB5E, 0x83ab66u) \
  X(AB66, 0x83ab71u) \
  X(AB71, 0x83ab74u) \
  X(AB74, 0x83ab76u) \
  X(AB76, 0x83ab88u) \
  X(AB8C, 0x83ab8eu) \
  X(AB8E, 0x83ab94u) \
  X(AB98, 0x83aba2u) \
  X(ABA2, 0x83aba5u) \
  X(ABDB, 0x83abe5u) \
  X(ABE5, 0x83abeau) \
  X(ABEA, 0x83abfbu) \
  X(ABFB, 0x83ac12u) \
  X(AC12, 0x83ac13u) \
  X(AC13, 0x83ac1au) \
  X(AC1A, 0x83ac1fu) \
  X(AC1F, 0x83ac2au) \
  X(AC2A, 0x83ac2bu) \
  X(AC2B, 0x83ac36u) \
  X(A79D, 0x83a7a4u) \
  X(A7A4, 0x83a7a7u) \
  X(A7A7, 0x83a7aeu) \
  X(A7AE, 0x83a7b6u) \
  X(A7B6, 0x83a7b7u) \
  X(A7B7, 0x83a7bau) \
  X(A7BE, 0x83a814u)

typedef enum {
#define X(at, end) GIANT_SPIDER_RUN_##at,
  GIANT_SPIDER_RUNS(X)
#undef X
  GIANT_SPIDER_RUN_TAKEN,      // a branch taken: one idle more
  GIANT_SPIDER_RUN_RNG_ONCE,   // `rng_next`, its counter bumped once...
  GIANT_SPIDER_RUN_RNG_TWICE,  // ...or twice
  GIANT_SPIDER_RUN_WIDE,       // the wide test's blocks, `TerrainWideBlock` on
  GIANT_SPIDER_RUN_COUNT = GIANT_SPIDER_RUN_WIDE + WIDE_BLOCK_COUNT
} GiantSpiderRun;

// The longest stretch is a frame with all three webs spreading.
#define GIANT_SPIDER_TRACE_STEPS 320

typedef struct {
  HwStep steps[GIANT_SPIDER_TRACE_STEPS];
  HwTrace trace;   // runs and branches taken, in order: it writes no register
  bool lost;       // a return to an address the thread does not know
  bool v_unknown;  // overflow is the wide test's, which it does not say
} GiantSpiderWork;

void giant_spider_work_init(GiantSpiderWork* k);

// A stretch of the thread, from `c->pc` to the instruction it stops on,
// which it leaves in `c->pc`.
void giant_spider_run(Wram* w, const Rom* rom, PortCpu* c, GiantSpiderWork* k);

// `$83:B13D`, its handler, as `thread_call_handler` calls it. Every way out
// is ported: it is always served.
bool giant_spider_collide(Wram* w, const Rom* rom, uint16_t dp, uint16_t arg,
                    ActorHandlerRegs* r, HandlerPieces* pieces);

#endif
