// The Snakeoid: the mound that tunnels about levels 20, 40 and 47, and its
// coming up to bite.
//
// `$82:A8BB` and `$82:A8C3` are the routines those records name, and they
// differ in one word: which four tiles it churns the ground into. A picture
// of record 20 shows it: a mound of earth beside the player, and a track of
// churned ground behind it.
//
// **It waits** where it was put until a player is within `$DC`, asking every
// four ticks.
//
// **Each frame** after that it sleeps a tick and runs its state:
//
//     wandering   `$82:A451`  the way it faces, until somebody is near
//     chasing     `$82:A4C3`  towards them, and up at them when it is there
//
// **Both** begin by ageing its track. A piece of track is a record of its
// own, of four, and lasts 32 frames: every eighth it is shown flatter, and
// then it is freed.
//
// **It steps one frame in seven.** Every other step lays a piece of track
// where it is, if one of the four is free, and churns the four tiles about
// it that can be churned. Then it goes 6 pixels its way, an axis at a time,
// each where the ground lets it.
//
// **Wandering**, a step that was stopped on either axis ends one of two ways
// by a draw: a new way, or a hop. A hop looks along its way, 8 pixels at a
// time from 24 off, for the first place it could stand, and comes up there
// over 32 ticks. One step in seventeen, by another draw, it comes up beside
// a player instead.
//
// **Chasing** somebody nearer than `$A0`, it steps about twice as often. A
// stopped step is always a hop, and a hop with nowhere to land is the end
// of the chase. With them within 12, or beside one of the four places it
// bites from, it comes up: `$82:A538` and `$82:A56E`.
//
// **Who it goes for** is asked at `$82:9F16`, three ways by the two words at
// `$6E` and `$70`, which `port/frontend.h` knows as the two pads. Neither
// set, and it is the nearest record of a kind it eats. One set, and it is
// that player. Both, and it is the nearer player.
//
// **It starts** with its weight on the level's load, `$23`, and the ground
// it churns. The first of a level sets the figure's colours, both rows
// (`port/figure_colours.h`), has the background's next upload queued
// (`port/bossbg.h`), and sleeps a tick at a time until the uploads are
// through. Then it is put where it was given, with no target and no side, its
// record is asked for, its track cleared, and its handler set.
//
// **Its bite**, `$82:A538` under them and `$82:A56E` beside them, is only
// for a place it may come up, and only while no other Snakeoid is up
// (`$7E:6058`). Otherwise, by a draw of 35 in 256, it leaps at a player as a
// step does. It turns to face them, and then:
//
//     comes up     `$82:9C02`  four records that hurt under its mouth, its
//                              track cleared, the shake queued, a cry, and
//                              three pictures of it rising on the second
//                              layer
//     under them   six frames' wait
//     beside them  a body of four records along the place it bites from,
//                  80 frames of its mouth working, two pictures by turns,
//                  and a lunge: five pictures, with a box told at its
//                  mouth on each frame whose count has no bit of `$2F`
//     goes under   `$82:9CEC`  the four records freed, the track cleared,
//                              the shake queued again, three pictures of
//                              it sinking, a vblank, and it is back where
//                              it came up
//
// **Its pictures** are on the big figures' layer (`port/bossbg.h`): the one
// it wants is `$3E`, and `$82:A392` puts it up when that differs from the
// one shown, `$40`, and the queue of uploads is empty. Nought blanks the
// layer, by `$82:8113`'s job. Each frame of a picture the plane's place is
// worked out again (`$82:A3BE`), and its flash, when it is hit, is seen to
// (`$82:A033`).
//
// The two scripts its pictures are played from, `$82:9B54` and `$82:9B83`,
// are lists of a picture and how many frames, read through the address
// each pushed.
//
// **Not here**: how it dies. No session of the survey reaches it. Its
// handler is `boss_aa2e_collide`, in `port/collide.h`.
//
// **Written as stretches**, like `port/tentacle.h`. The calls a stretch
// stops on are a record asked for, a record freed, and its sleeps, and in
// its bite a job queued, its cry, a picture's upload, its colours, a box
// told, and the `WAI` it goes under on. Most of those are inside routines
// its states call, so a stretch that begins after one finds its way back by
// the return addresses on the stack, as the ROM does. The calls it makes
// itself are the ones whose cost is known to the cycle: a draw, the ground
// tests, who is near, and a job taken out of the queue.
//
// Port code: libc only.

#ifndef PORT_SNAKEOID_H
#define PORT_SNAKEOID_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/tile_put.h"
#include "port/wram.h"

#define SNAKEOID_BANK 0x82u

// Its page.
#define SNAKEOID_DP_RECORD 0x08
#define SNAKEOID_DP_TRACK 0x0a       // four pieces: a record, or `$FFFF`...
#define SNAKEOID_DP_TRACK_AGE 0x0c   // ...and the frames it has left
#define SNAKEOID_DP_DROP_IN 0x1a     // steps until it may drop a piece
#define SNAKEOID_DP_LANDS_X 0x24
#define SNAKEOID_DP_LANDS_Y 0x26
#define SNAKEOID_DP_X 0x28
#define SNAKEOID_DP_Y 0x2a
#define SNAKEOID_DP_AIM_X 0x2c       // where a step would take it, or a
#define SNAKEOID_DP_AIM_Y 0x2e       // hop's stride
#define SNAKEOID_DP_TRY_X 0x30       // the point being asked about
#define SNAKEOID_DP_TRY_Y 0x32
#define SNAKEOID_DP_COUNT 0x34
#define SNAKEOID_DP_WAY 0x36         // a direction doubled: 2 for up, round
                                     // by the clock to 16
#define SNAKEOID_DP_STATE 0x38
#define SNAKEOID_DP_SIDE 0x3a        // which place it bites from, or negative
#define SNAKEOID_DP_TARGET 0x3c
#define SNAKEOID_DP_PICTURE 0x3e     // the picture it wants up...
#define SNAKEOID_DP_SHOWN 0x40       // ...and the one that is
#define SNAKEOID_DP_PAUSE 0x42       // frames until its next step
#define SNAKEOID_DP_FACING 0x44      // negative: its pictures mirrored
#define SNAKEOID_DP_DYING 0x46
#define SNAKEOID_DP_HIT 0x4a         // bit 15: hit since its last flash
#define SNAKEOID_DP_FLASH 0x4c       // frames its flash has left
#define SNAKEOID_DP_HIT_BY 0x4e
#define SNAKEOID_DP_GROUND 0x50      // 0 or `$20`: which tiles it churns
#define SNAKEOID_DP_GAP 0x54

// Up, the four records that hurt are at `$1C`, two apart; and beside them,
// the four of its body are where its track was.
#define SNAKEOID_DP_HURTS 0x1c
#define SNAKEOID_DP_BODY SNAKEOID_DP_TRACK
#define SNAKEOID_HURT_LAST 0x0006u

#define SNAKEOID_TRACK_PIECES 4
#define SNAKEOID_TRACK_STRIDE 4
#define SNAKEOID_NO_PIECE 0xffffu

// The states, as `$38` holds them.
#define SNAKEOID_STATE_WANDERING 0xa451u
#define SNAKEOID_STATE_CHASING 0xa4c3u

// A piece of track.
#define SNAKEOID_TRACK_FRAMES 0x0020u
#define SNAKEOID_MOUND_PICTURE 0xee42u
#define SNAKEOID_META_BANK 0x008fu
#define SNAKEOID_TRACK_PICTURES 0x9debu  // by its age over four, less two
// The list of tile changes is this long, in bytes, and it churns nothing.
#define SNAKEOID_CHURN_LIST_FULL 0x0014u
// Four rows of four words: a tile across and down from the last, and what
// it becomes. The second four are for the other ground.
#define SNAKEOID_CHURN 0x9eb1u
#define SNAKEOID_CHURN_LAST_ROW 0x0018u
#define SNAKEOID_CHURNS 4
// A tile that can be churned has this bit.
#define SNAKEOID_TILE_SOFT 0x0080u

// Its tables, in its own bank.
#define SNAKEOID_STEPS 0x9aaeu       // a pair of words a way, by the way
                                     // times two
#define SNAKEOID_HOP_FROM 0xa10eu    // where a hop begins looking...
#define SNAKEOID_HOP_STRIDE 0xa132u  // ...and how far apart
#define SNAKEOID_HOP_TRIES 0x0019u
#define SNAKEOID_RISE 0xa200u        // a picture and how long, four times
#define SNAKEOID_BESIDE 0x9fb7u      // where it makes for, by the way to who
#define SNAKEOID_WAYS 0x82a026u      // a way, by the two compares
#define SNAKEOID_BITES_FROM 0xa382u  // the four places beside it
#define SNAKEOID_BITE_LAST 0x000cu

// How near a player has to be to wake it, how near anybody for it to chase,
// and for it to go on chasing.
#define SNAKEOID_WAKES_AT 0x00dcu
#define SNAKEOID_SEES 0x00b4u
#define SNAKEOID_KEEPS 0x00dcu
#define SNAKEOID_BITES_AT 0x000cu
#define SNAKEOID_BESIDE_AT 0x000eu
#define SNAKEOID_HURRIES_AT 0x00a0u
#define SNAKEOID_STEP_FRAMES 0x0006u
// The draws: a step is a leap at a player under the first, and a stopped
// step is a hop under the second.
#define SNAKEOID_LEAPS_UNDER 0x000fu
#define SNAKEOID_HOPS_UNDER 0x0064u
// The kinds of record it goes for with neither word set, and the kinds a
// player's record can be for it to leap at them.
#define SNAKEOID_EATS_A 0x0038u
#define SNAKEOID_EATS_B 0x0001u
#define SNAKEOID_PLAYER_KIND_A 0x0005u
#define SNAKEOID_PLAYER_KIND_B 0x0006u

// A word for each player.
#define W_SNAKEOID_SIDE_A 0x006eu
#define W_SNAKEOID_SIDE_B 0x0070u
// Where `$82:9FFB` keeps the point it is asked the way to.
#define W_SNAKEOID_TO_X 0x00a2u
#define W_SNAKEOID_TO_Y 0x00a4u

// Where each stretch begins.
#define SNAKEOID_ENTER_PC 0x82a8bbu      // its thread, churning one ground
#define SNAKEOID_ENTER_B_PC 0x82a8c3u    // ...or the other
#define SNAKEOID_COLOURED_PC 0x82a84bu   // after the figure's colours
#define SNAKEOID_SPRITE_COLOURED_PC 0x82a855u  // ...and the sprites'
#define SNAKEOID_QUEUE_WOKE_PC 0x82a878u // after a tick of waiting for the upload
#define SNAKEOID_MADE_PC 0x82a7f7u       // its record, in A
#define SNAKEOID_READY_PC 0x82a8bau      // the `RTS` after its handler is set
#define SNAKEOID_WAITED_PC 0x82a43au     // after a sleep of its wait
#define SNAKEOID_WAKES_PC 0x82a8efu      // after a frame's sleep
#define SNAKEOID_FREED_PC 0x829de7u      // after a piece of track is freed
#define SNAKEOID_DROPPED_PC 0x829e18u    // after a record for a piece
#define SNAKEOID_ROSE_PC 0x82a1f8u       // after a tick of its coming up
// ...and in its bite.
#define SNAKEOID_HURT_MADE_PC 0x829c6bu  // a record that hurts, in A
#define SNAKEOID_BODY_MADE_PC 0x82a6b9u  // a record of its body, in A
#define SNAKEOID_CLEARED_PC 0x829f05u    // a piece of track freed, coming up
#define SNAKEOID_HURT_FREED_PC 0x829d44u
#define SNAKEOID_RISE_WAITED_PC 0x829c2fu
#define SNAKEOID_RISE_QUEUED_PC 0x829c3bu  // the shake's slot in X
#define SNAKEOID_CRIED_PC 0x829c43u
#define SNAKEOID_PLAYED_PC 0x829b7au     // after a frame of a picture
#define SNAKEOID_LUNGED_PC 0x829bb1u     // ...of its lunge
#define SNAKEOID_PICTURE_UP_PC 0x829ae2u
#define SNAKEOID_PICTURE_FLIPPED_PC 0x829af3u
#define SNAKEOID_BLANKED_PC 0x828135u    // the blanking job queued
#define SNAKEOID_UNFLASHED_PC 0x82a05bu
#define SNAKEOID_FLASHED_PC 0x82a087u
#define SNAKEOID_TOLD_PC 0x82a349u
#define SNAKEOID_BITTEN_PC 0x82a55eu     // after its six frames under them
#define SNAKEOID_STRUCK_LOW_PC 0x82a600u // after a frame of its mouth working
#define SNAKEOID_STRUCK_HIGH_PC 0x82a652u
#define SNAKEOID_SINK_WAITED_PC 0x829cfbu
#define SNAKEOID_SINK_QUEUED_PC 0x829d07u
#define SNAKEOID_SUNK_PC 0x829d25u       // after the `WAI`

// Where one stops: a call the harness makes, or what is not ported.
#define SNAKEOID_COLOURS_PC 0x82a847u    // `JSL figure_colours_set`
#define SNAKEOID_SPRITE_COLOURS_PC 0x82a851u  // `JSL figure_sprite_colours_set`
#define SNAKEOID_UPLOAD_PC 0x82a86du     // `JSL boss_bg_queue_one`
#define SNAKEOID_QUEUE_WAIT_PC 0x82a874u // `JSL thread_yield`
#define SNAKEOID_START_RECORD_PC 0x82a7f3u  // `JSL actor_slot_alloc`
#define SNAKEOID_WAIT_PC 0x82a436u       // `JSL thread_yield`
#define SNAKEOID_SLEEP_PC 0x82a8ebu      // `JSL thread_yield`
#define SNAKEOID_RISE_PC 0x82a1f4u       // `JSL thread_yield`
#define SNAKEOID_FREE_PC 0x829de3u       // `JSL actor_slot_free`
#define SNAKEOID_RECORD_PC 0x829e14u     // `JSL actor_slot_alloc`
#define SNAKEOID_DIES_PC 0x82a8f3u       // `LDX #$2000`: `port/begin.h`'s
// ...and in its bite.
#define SNAKEOID_HURT_RECORD_PC 0x829c67u   // `JSL actor_slot_alloc`
#define SNAKEOID_BODY_RECORD_PC 0x82a6b5u
#define SNAKEOID_CLEAR_FREE_PC 0x829f01u    // `JSL actor_slot_free`
#define SNAKEOID_HURT_FREE_PC 0x829d40u
#define SNAKEOID_RISE_WAIT_PC 0x829c2bu     // `JSL thread_yield`
#define SNAKEOID_PLAY_WAIT_PC 0x829b76u
#define SNAKEOID_LUNGE_WAIT_PC 0x829badu
#define SNAKEOID_BITE_WAIT_PC 0x82a55au
#define SNAKEOID_STRIKE_LOW_WAIT_PC 0x82a5fcu
#define SNAKEOID_STRIKE_HIGH_WAIT_PC 0x82a64eu
#define SNAKEOID_SINK_WAIT_PC 0x829cf7u
#define SNAKEOID_RISE_QUEUE_PC 0x829c37u    // `JSL vbl_queue_a_add`
#define SNAKEOID_BLANK_QUEUE_PC 0x828131u
#define SNAKEOID_SINK_QUEUE_PC 0x829d03u
#define SNAKEOID_CRY_PC 0x829c3fu           // `JSL apu_play_sfx`
#define SNAKEOID_PICTURE_PC 0x829adeu       // `JSL boss_bg_queue`
#define SNAKEOID_PICTURE_FLIP_PC 0x829aefu  // `JSL boss_bg_queue_flip`
#define SNAKEOID_UNFLASH_PC 0x82a057u       // `JSL figure_colours_set`
#define SNAKEOID_FLASH_PC 0x82a083u
#define SNAKEOID_TELL_PC 0x82a345u          // `JSL actor_notify_box`
#define SNAKEOID_SINK_WAI_PC 0x829d24u      // `WAI`

// The return addresses a stretch can find on the stack, as `RTS` pulls them.
#define SNAKEOID_RET_RECORD 0xa8a7u      // `JSR $A7F3`
#define SNAKEOID_RET_WAIT 0xa8dau        // `JSR $A425`
#define SNAKEOID_RET_READY 0xa8d7u       // `JSR $A82F`
#define SNAKEOID_RET_WAY 0xa8ddu         // `JSR $A43D`
#define SNAKEOID_RET_STATE 0xa8e7u       // the `PEA` before its state
#define SNAKEOID_RET_AGED 0xa423u        // `JSR $9DA7`, of a frame
#define SNAKEOID_RET_AGED_RISING 0xa1fau // ...and of a tick of coming up
#define SNAKEOID_RET_TICK_WANDER 0xa453u // `JSR $A418`
#define SNAKEOID_RET_TICK_CHASE 0xa4c5u
#define SNAKEOID_RET_DROP_WANDER 0xa476u // `JSR $9DF7`
#define SNAKEOID_RET_DROP_CHASE 0xa4f9u
#define SNAKEOID_RET_RISE_HOP 0xa105u    // `JSR $A1D4`, of a hop
#define SNAKEOID_RET_RISE_LEAP 0xa1adu   // ...and of a leap
#define SNAKEOID_RET_HOP_WANDER 0xa4b2u  // `JSR $A0C7`
#define SNAKEOID_RET_HOP_CHASE 0xa52cu
#define SNAKEOID_RET_LEAP 0xa470u        // `JSR $A156`
// ...and of its bite. Each is under the state's own return, or under one of
// these: the stack is a grammar, and `snakeoid_stack_known` reads it.
#define SNAKEOID_RET_UNDER_RISES 0xa556u   // `JSR $9C02`, under them...
#define SNAKEOID_RET_UNDER_SINKS 0xa560u   // ...and `JSR $9CEC`
#define SNAKEOID_RET_BESIDE_RISES 0xa58cu  // beside them...
#define SNAKEOID_RET_BODY 0xa58fu          // `JSR $A65D`
#define SNAKEOID_RET_STRIKE_LOW 0xa599u    // `JSR $A5B2`
#define SNAKEOID_RET_STRIKE_HIGH 0xa59eu   // `JSR $A60B`
#define SNAKEOID_RET_BESIDE_SINKS 0xa5a4u
#define SNAKEOID_RET_HURTS 0x9c15u         // `JSR $9C56`
#define SNAKEOID_RET_RISE_CLEARS 0x9c1fu   // `JSR $9EF1`
#define SNAKEOID_RET_RISE_PLAYS 0x9c48u    // `JSR $9B54`, the shake's slot under it
#define SNAKEOID_RET_RISE_TELLS 0x9c54u    // `JSR $A321`
#define SNAKEOID_RET_SINK_FREES 0x9d0au    // `JSR $9D37`, the shake's slot under it
#define SNAKEOID_RET_SINK_CLEARS 0x9d0du   // ...`JSR $9EF1`
#define SNAKEOID_RET_SINK_PLAYS 0x9d1eu    // ...`JSR $9B54`
#define SNAKEOID_RET_BODY_MADE 0xa65fu     // `JSR $A6A8`
#define SNAKEOID_RET_LOW_SHOWS 0xa5e6u     // `JSR $A3DB`
#define SNAKEOID_RET_LOW_LUNGES 0xa609u    // `JSR $9B83`
#define SNAKEOID_RET_HIGH_SHOWS 0xa635u
#define SNAKEOID_RET_HIGH_LUNGES 0xa65bu
#define SNAKEOID_RET_PLAY_SHOWS 0x9b6du    // `JSR $A3DB`, under a count and
#define SNAKEOID_RET_LUNGE_SHOWS 0x9ba4u   // the script's address
#define SNAKEOID_RET_LUNGE_TELLS 0x9bd3u   // `JSR $A321`, likewise
#define SNAKEOID_RET_PICKS 0xa3ddu         // `JSR $A392`
#define SNAKEOID_RET_FLASHES 0xa3e0u       // `JSR $A033`
#define SNAKEOID_RET_PUTS 0xa3a9u          // `JSR $9AF4`
#define SNAKEOID_RET_PICTURE 0x9b1du       // `JSR $9AD2`
#define SNAKEOID_RET_PICTURE_FLIP 0x9b22u  // `JSR $9AE3`
#define SNAKEOID_RET_BLANKS 0xa3aeu        // `JSL $82:8113`

// Its bite. One Snakeoid is up at a time, and while it is this word in bank
// `$7E` is set. It came up where these two say, and goes back there.
#define W_SNAKEOID_UP 0x6058u
#define W_SNAKEOID_UP_X 0x1e62u
#define W_SNAKEOID_UP_Y 0x1e64u
// Its picture's offset on the plane, and where the plane is drawn
// (`W_BOSS_PLANE_X` and `_Y`, in `port/vblank.h`).
#define W_SNAKEOID_PICTURE_X 0x1e66u
#define W_SNAKEOID_PICTURE_Y 0x1e68u
#define W_SNAKEOID_PLANE_X 0x1e6eu
#define W_SNAKEOID_PLANE_Y 0x1e70u
// Where its box is told about (`actor_notify_box`'s words, in page nought).
#define W_SNAKEOID_BOX 0x0038u
#define SNAKEOID_BOX_KIND 0x0003u
// It waits while the queue of jobs has this many.
#define SNAKEOID_QUEUE_ROOM 0x0005u
#define SNAKEOID_CRY_SFX 0x0022u
// A draw under this, with nowhere to come up, and it leaps at a player.
#define SNAKEOID_BITE_LEAPS_UNDER 0x0023u
// ...and under this, its mouth stops working early.
#define SNAKEOID_STRIKE_ENDS_UNDER 0x000fu
#define SNAKEOID_STRIKE_FRAMES 0x004fu
#define SNAKEOID_BITE_WAIT 0x0006u
#define SNAKEOID_LUNGE_FRAMES 0x002fu   // a box is told when none of these is set

// Its tables, for the bite.
#define SNAKEOID_HURTS_X 0x9cd4u     // the four records', across
#define SNAKEOID_HURTS_DOWN 0x002cu  // ...and how far below it
#define SNAKEOID_HURT_KIND 0x0009u
#define SNAKEOID_RISES 0x9cdcu       // the script it comes up by
#define SNAKEOID_SINKS 0x9d7bu       // ...and goes under by
#define SNAKEOID_LUNGES_LOW 0xa7a3u  // its lunge from a side below
#define SNAKEOID_LUNGES_HIGH 0xa7cbu // ...and above
#define SNAKEOID_PICTURES 0x9b24u    // a far address a picture, by four
#define SNAKEOID_PICTURE_AT 0x9b3cu  // ...and its offset on the plane
#define SNAKEOID_PICTURE_OF 0xa3b0u  // which, by the picture it wants
#define SNAKEOID_BODY_FLAGS 0xa753u  // a word of flags and a picture, by side
#define SNAKEOID_BODY_AT 0xa763u     // where its body goes: four rows of four
#define SNAKEOID_BODY_ROW 0x0010u    // sides, the last first
#define SNAKEOID_BODY_LAST_ROW 0x0030u
#define SNAKEOID_BODY_KIND 0x0003u
#define SNAKEOID_BODY_LAST 0x000cu
#define SNAKEOID_LUNGE_BOX 0x9be2u   // where the box is, from its body, by side
#define SNAKEOID_SIDE_LOW 0x0006u    // a side past this bites from below
// The pictures its mouth works by: from below, two that `$3E` follows...
#define SNAKEOID_STRIKE_LOW_A 0xed26u
#define SNAKEOID_STRIKE_LOW_B 0xed6fu
#define SNAKEOID_STRIKE_LOW_PICTURE 0x0004u
// ...and from above, two that it does not.
#define SNAKEOID_STRIKE_HIGH_A 0xedb0u
#define SNAKEOID_STRIKE_HIGH_B 0xedf9u
#define SNAKEOID_STRIKE_HIGH_PICTURE 0x0006u
// Its pictures plain and mirrored: the plane's place is that much off the
// picture's.
#define SNAKEOID_SPAN_PLAIN_X 0x0050u
#define SNAKEOID_SPAN_MIRRORED_X 0x002au
// Its flash's colours, on and off.
#define SNAKEOID_FLASH_COLOURS 0xfadcu
#define SNAKEOID_FLASH_FRAMES 0x0002u
// `$82:8113`: the layer blanked, from a word of nought.
#define SNAKEOID_BLANK_SOURCE 0x8136u
#define SNAKEOID_BLANK_BANK 0x0082u
#define SNAKEOID_BLANK_VRAM 0x6800u
#define SNAKEOID_BLANK_BYTES 0x2000u
// The bits of a record's flags it clears.
#define SNAKEOID_BODY_CLEARS 0xffd9u  // `ACTOR_FLIP` and `ACTOR_SORT_FIRST`

// Its start: the figure's colours, once a level, and the upload.
#define W_SNAKEOID_COLOURED 0x1ffau
#define SNAKEOID_COLOURS 0xfabcu
#define SNAKEOID_COLOURS_BANK 0x0083u
#define SNAKEOID_LOAD 0x0023u
#define SNAKEOID_GROUND_B 0x0020u
#define SNAKEOID_UPLOAD_SOURCE 0xc48bu
#define SNAKEOID_UPLOAD_BANK 0x0094u
#define SNAKEOID_UPLOAD_BYTES 0x2800u
#define SNAKEOID_UPLOAD_VRAM 0x5000u
#define W_SNAKEOID_SPAN_X 0x1e6au
#define W_SNAKEOID_SPAN_Y 0x1e6cu
#define SNAKEOID_SPAN_X 0x004eu
#define SNAKEOID_SPAN_Y 0x005au
#define SNAKEOID_DP_COUNTED 0x48
#define SNAKEOID_COUNT 0x004bu
#define SNAKEOID_HANDLER 0xaa2eu
#define SNAKEOID_HANDLER_BANK 0x0082u
#define SNAKEOID_ATTR 0x0c00u

// The straight runs of the listing a stretch is made of, each named for where
// it starts, with where it ends. `tools/price_runs.py` prices them.
#define SNAKEOID_RUNS(X) \
  X(80E0, 0x8280e4u) \
  X(8113, 0x828131u) \
  X(8135, 0x828136u) \
  X(80E4, 0x8280e8u) \
  X(9AD2, 0x829adeu) \
  X(9AE3, 0x829aefu) \
  X(9AF4, 0x829b0au) \
  X(9B0A, 0x829b0eu) \
  X(9B0E, 0x829b1bu) \
  X(9B1B, 0x829b1eu) \
  X(9B1E, 0x829b20u) \
  X(9B20, 0x829b23u) \
  X(9B54, 0x829b57u) \
  X(9B57, 0x829b5du) \
  X(9B5D, 0x829b5fu) \
  X(9B5F, 0x829b60u) \
  X(9B60, 0x829b6eu) \
  X(9B6E, 0x829b72u) \
  X(9B72, 0x829b76u) \
  X(9B7A, 0x829b7fu) \
  X(9B7F, 0x829b81u) \
  X(9B81, 0x829b83u) \
  X(9B83, 0x829b8eu) \
  X(9B8E, 0x829b94u) \
  X(9B94, 0x829ba5u) \
  X(9BA5, 0x829ba9u) \
  X(9BA9, 0x829badu) \
  X(9BB1, 0x829bbcu) \
  X(9BBC, 0x829bd4u) \
  X(9BD4, 0x829bd6u) \
  X(9BD6, 0x829bd8u) \
  X(9BD8, 0x829be2u) \
  X(9C02, 0x829c16u) \
  X(9C16, 0x829c20u) \
  X(9C20, 0x829c28u) \
  X(9C28, 0x829c2bu) \
  X(9C2F, 0x829c31u) \
  X(9C31, 0x829c37u) \
  X(9C3B, 0x829c3fu) \
  X(9C43, 0x829c49u) \
  X(9C49, 0x829c55u) \
  X(9C56, 0x829c5au) \
  X(9C5A, 0x829c67u) \
  X(9C6B, 0x829ca5u) \
  X(9CEC, 0x829cf4u) \
  X(9CF4, 0x829cf7u) \
  X(9CFB, 0x829cfdu) \
  X(9CFD, 0x829d03u) \
  X(9D07, 0x829d0bu) \
  X(9D0B, 0x829d0eu) \
  X(9D0E, 0x829d1fu) \
  X(9D1F, 0x829d24u) \
  X(9D25, 0x829d37u) \
  X(9D37, 0x829d3cu) \
  X(9D3C, 0x829d40u) \
  X(9D44, 0x829d4cu) \
  X(9D8F, 0x829d9au) \
  X(9D9A, 0x829da6u) \
  X(9DA7, 0x829dadu) \
  X(9DAD, 0x829db4u) \
  X(9DB4, 0x829dbbu) \
  X(9DBB, 0x829dc0u) \
  X(9DC0, 0x829dc8u) \
  X(9DC8, 0x829dc9u) \
  X(9DC9, 0x829ddbu) \
  X(9DDB, 0x829de3u) \
  X(9DE7, 0x829debu) \
  X(9DF7, 0x829dfbu) \
  X(9DFB, 0x829dfeu) \
  X(9DFE, 0x829e05u) \
  X(9E05, 0x829e0du) \
  X(9E0E, 0x829e14u) \
  X(9E18, 0x829e56u) \
  X(9E57, 0x829e5fu) \
  X(9E5F, 0x829e72u) \
  X(9E72, 0x829e95u) \
  X(9E95, 0x829ea6u) \
  X(9EA6, 0x829eb0u) \
  X(9EF1, 0x829ef9u) \
  X(9EF9, 0x829f00u) \
  X(9F00, 0x829f01u) \
  X(9F05, 0x829f0du) \
  X(9F0D, 0x829f15u) \
  X(9F16, 0x829f1au) \
  X(9F1A, 0x829f25u) \
  X(9F25, 0x829f2fu) \
  X(9F2F, 0x829f34u) \
  X(9F34, 0x829f36u) \
  X(9F36, 0x829f59u) \
  X(9F59, 0x829f5fu) \
  X(9F5F, 0x829f6cu) \
  X(9F6C, 0x829f74u) \
  X(9F74, 0x829f7cu) \
  X(9F7C, 0x829f84u) \
  X(9F84, 0x829f89u) \
  X(9F89, 0x829f8eu) \
  X(9F8E, 0x829f91u) \
  X(9F91, 0x829f99u) \
  X(9F99, 0x829f9du) \
  X(9F9D, 0x829fa7u) \
  X(9FA7, 0x829fabu) \
  X(9FAB, 0x829fb5u) \
  X(9FB5, 0x829fb7u) \
  X(9FFB, 0x82a00bu) \
  X(A00B, 0x82a012u) \
  X(A012, 0x82a019u) \
  X(A019, 0x82a01eu) \
  X(A01E, 0x82a026u) \
  X(A033, 0x82a037u) \
  X(A037, 0x82a03bu) \
  X(A03B, 0x82a057u) \
  X(A05C, 0x82a060u) \
  X(A060, 0x82a083u) \
  X(A088, 0x82a096u) \
  X(A096, 0x82a0a0u) \
  X(A0A0, 0x82a0a9u) \
  X(A0A9, 0x82a0b3u) \
  X(A0B3, 0x82a0bdu) \
  X(A0BD, 0x82a0c6u) \
  X(A0C7, 0x82a0eau) \
  X(A0EA, 0x82a103u) \
  X(A103, 0x82a106u) \
  X(A106, 0x82a108u) \
  X(A108, 0x82a10cu) \
  X(A10C, 0x82a10eu) \
  X(A156, 0x82a163u) \
  X(A163, 0x82a16cu) \
  X(A16C, 0x82a171u) \
  X(A172, 0x82a182u) \
  X(A182, 0x82a186u) \
  X(A186, 0x82a19cu) \
  X(A19C, 0x82a1a0u) \
  X(A1A0, 0x82a1abu) \
  X(A1AB, 0x82a1aeu) \
  X(A1AF, 0x82a1b9u) \
  X(A1B9, 0x82a1c3u) \
  X(A1C3, 0x82a1d3u) \
  X(A1D4, 0x82a1d8u) \
  X(A1D8, 0x82a1deu) \
  X(A1DE, 0x82a1ecu) \
  X(A1EC, 0x82a1f0u) \
  X(A1F0, 0x82a1f4u) \
  X(A1F8, 0x82a1fbu) \
  X(A1FB, 0x82a1fdu) \
  X(A1FD, 0x82a1ffu) \
  X(A321, 0x82a345u) \
  X(A34A, 0x82a34fu) \
  X(A34F, 0x82a36eu) \
  X(A36E, 0x82a378u) \
  X(A378, 0x82a37bu) \
  X(A37B, 0x82a382u) \
  X(A392, 0x82a398u) \
  X(A398, 0x82a39eu) \
  X(A39E, 0x82a3a4u) \
  X(A3A4, 0x82a3aau) \
  X(A3AB, 0x82a3afu) \
  X(A3BE, 0x82a3dbu) \
  X(A3DB, 0x82a3deu) \
  X(A3DE, 0x82a3e1u) \
  X(A3E1, 0x82a3e5u) \
  X(A418, 0x82a41cu) \
  X(A41C, 0x82a421u) \
  X(A421, 0x82a424u) \
  X(A425, 0x82a433u) \
  X(A433, 0x82a436u) \
  X(A43A, 0x82a43cu) \
  X(A43D, 0x82a44bu) \
  X(A44B, 0x82a451u) \
  X(A451, 0x82a454u) \
  X(A454, 0x82a461u) \
  X(A461, 0x82a465u) \
  X(A465, 0x82a46eu) \
  X(A46E, 0x82a471u) \
  X(A471, 0x82a474u) \
  X(A474, 0x82a477u) \
  X(A477, 0x82a498u) \
  X(A498, 0x82a49au) \
  X(A49A, 0x82a4a0u) \
  X(A4A0, 0x82a4a2u) \
  X(A4A2, 0x82a4a6u) \
  X(A4A7, 0x82a4b0u) \
  X(A4B0, 0x82a4b3u) \
  X(A4B3, 0x82a4b5u) \
  X(A4B5, 0x82a4b8u) \
  X(A4B8, 0x82a4bdu) \
  X(A4BD, 0x82a4c3u) \
  X(A4C3, 0x82a4c6u) \
  X(A4C6, 0x82a4d5u) \
  X(A4D5, 0x82a4dfu) \
  X(A4DF, 0x82a4e7u) \
  X(A4E7, 0x82a4ecu) \
  X(A4EC, 0x82a4f0u) \
  X(A4F0, 0x82a4f3u) \
  X(A4F3, 0x82a4f7u) \
  X(A4F7, 0x82a4fau) \
  X(A4FA, 0x82a51bu) \
  X(A51B, 0x82a51du) \
  X(A51D, 0x82a523u) \
  X(A523, 0x82a525u) \
  X(A525, 0x82a529u) \
  X(A52A, 0x82a52du) \
  X(A52D, 0x82a52fu) \
  X(A52F, 0x82a532u) \
  X(A532, 0x82a535u) \
  X(A535, 0x82a538u) \
  X(A538, 0x82a541u) \
  X(A541, 0x82a547u) \
  X(A547, 0x82a552u) \
  X(A552, 0x82a554u) \
  X(A554, 0x82a557u) \
  X(A557, 0x82a55au) \
  X(A55E, 0x82a561u) \
  X(A562, 0x82a56bu) \
  X(A56B, 0x82a56eu) \
  X(A56E, 0x82a574u) \
  X(A574, 0x82a57du) \
  X(A57D, 0x82a588u) \
  X(A588, 0x82a58au) \
  X(A58A, 0x82a58du) \
  X(A58D, 0x82a590u) \
  X(A590, 0x82a597u) \
  X(A597, 0x82a59au) \
  X(A59A, 0x82a59cu) \
  X(A59C, 0x82a59fu) \
  X(A59F, 0x82a5a5u) \
  X(A5A6, 0x82a5afu) \
  X(A5AF, 0x82a5b2u) \
  X(A5B2, 0x82a5bcu) \
  X(A5BC, 0x82a5c3u) \
  X(A5C3, 0x82a5cdu) \
  X(A5CD, 0x82a5d9u) \
  X(A5D9, 0x82a5e1u) \
  X(A5E1, 0x82a5e4u) \
  X(A5E4, 0x82a5e7u) \
  X(A5E7, 0x82a5f0u) \
  X(A5F0, 0x82a5f7u) \
  X(A5F7, 0x82a5f9u) \
  X(A5F9, 0x82a5fcu) \
  X(A600, 0x82a604u) \
  X(A604, 0x82a60au) \
  X(A60B, 0x82a615u) \
  X(A615, 0x82a61cu) \
  X(A61C, 0x82a626u) \
  X(A626, 0x82a62bu) \
  X(A62B, 0x82a630u) \
  X(A630, 0x82a633u) \
  X(A633, 0x82a636u) \
  X(A636, 0x82a63fu) \
  X(A63F, 0x82a649u) \
  X(A649, 0x82a64bu) \
  X(A64B, 0x82a64eu) \
  X(A652, 0x82a656u) \
  X(A656, 0x82a65cu) \
  X(A65D, 0x82a660u) \
  X(A660, 0x82a675u) \
  X(A675, 0x82a6a7u) \
  X(A6A8, 0x82a6b5u) \
  X(A6B9, 0x82a708u) \
  X(A709, 0x82a731u) \
  X(A7F7, 0x82a82fu) \
  X(A82F, 0x82a834u) \
  X(A834, 0x82a847u) \
  X(A84B, 0x82a851u) \
  X(A855, 0x82a86du) \
  X(A871, 0x82a874u) \
  X(A878, 0x82a87cu) \
  X(A87C, 0x82a87eu) \
  X(A87E, 0x82a8a8u) \
  X(A8A8, 0x82a8abu) \
  X(A8AB, 0x82a8bau) \
  X(A8BB, 0x82a8c3u) \
  X(A8C3, 0x82a8cbu) \
  X(A8CB, 0x82a8d8u) \
  X(A8D8, 0x82a8dbu) \
  X(A8DB, 0x82a8deu) \
  X(A8DE, 0x82a8e8u) \
  X(A8E8, 0x82a8ebu) \
  X(A8EF, 0x82a8f3u)

typedef enum {
#define X(from, to) SNAKEOID_RUN_##from,
  SNAKEOID_RUNS(X)
#undef X
  SNAKEOID_RUN_COUNT,
  SNAKEOID_RUN_RTS = SNAKEOID_RUN_9DC8,  // any `RTS`: they all cost the same
} SnakeoidRun;

// The most of each that one stretch makes. A chase asks who is near once
// for itself and once for each place it bites from, and the stretch that
// ends its wait has asked once before that.
#define SNAKEOID_MAX_PLAYER_ASKS 6
#define SNAKEOID_MAX_NEAREST 5

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t runs[SNAKEOID_RUN_COUNT];
  uint16_t taken;  // branches taken
  // The ground tests, by whether the footprint was off the ground it keeps
  // to and how many of the six tiles each looked at.
  uint16_t grounds[2][TERRAIN_PROBE_COUNT + 1];
  uint16_t edges[BOUNDS_LAST_COMPARE + 1];  // `terrain_out_of_bounds`
  uint16_t draws[2];  // `rng_next`, by its overflow
  uint16_t tiles_read;  // `tile_attrs_at_tile`
  TilePutRegs churned[SNAKEOID_CHURNS];  // `map_tile_put`
  int churn_count;
  PlayerPickRegs players[SNAKEOID_MAX_PLAYER_ASKS];  // `player_in_range`
  int player_count;
  ActorNearestWork nearest[SNAKEOID_MAX_NEAREST];  // `actor_nearest`
  int nearest_count;
  uint16_t handlers_set;  // `thread_set_handler`
  // Overflow is the ROM's at the end, which it is not after a call that
  // does not say what it left there.
  bool v_known;
  TerrainWideWork wide;  // `$82:A214`, `terrain_blocked_wide`'s test lower
  uint16_t unqueued;     // `vbl_queue_a_remove`
} SnakeoidWork;

// True if `state` is one of the two it can be in.
bool snakeoid_state_known(uint16_t state);

// True if the stretch that begins at `pc`, with the stack at `s`, comes back
// through return addresses this port knows.
bool snakeoid_stack_known(const Wram* w, uint32_t pc, uint16_t s);

// True if what a stretch of its bite is given in A and X is what it needs:
// a record it asked for, or the slot of the job it queued.
bool snakeoid_regs_known(uint32_t pc, uint16_t a, uint16_t x);

// Run the stretch that begins at `c->pc`.
void snakeoid_run(Wram* w, const Rom* rom, PortCpu* c, SnakeoidWork* k);

#endif
