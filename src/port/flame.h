// What a destroyed doll can leave behind: a small thing that wanders, goes
// for whoever comes near, and trails pictures that fade.
//
// It is a thread, `$81:B664`. The doll's own ending starts one where it
// stood, 80 times in 256, and the spawn lists of bank `$9F` name it too. It
// is called a flame here for what it does, not from having seen it: the one
// weapon that ends it at a touch is id `$5D`, where every other shot only
// takes from the 8 it starts with (`enemy_b95f_collide`, `port/collide.h`),
// and it is gone by itself after 800 frames.
//
// ## What it does
//
// **It wanders.** It picks a point 8 to 24 pixels away on each axis, and
// walks the longer of the two legs only, a pixel on every other frame, for
// half the two legs' sum and one. Solid ground stops a step and not the
// count. Then it picks again.
//
// **It gives up.** On each frame it steps, nobody within `$D0` ends it.
//
// **It goes for whoever is near.** Something `actor_nearest` knows within
// `$30` becomes its target: it aims at where the target stands, and from
// then on moves every frame and twice, again along the longer leg alone, for
// the same count. It will not step onto its target: a step that would leave
// the two within 9 pixels, summed over both axes, is not taken. When the
// count is out it wanders again, target or no target.
//
// **It trails.** Every sixteenth frame of the game it leaves a picture two
// steps behind it, if one of its two places for them is free. Each lasts 24
// frames and changes on every fourth.
//
// **It ends** when its 800 frames are out, when nobody is near, or when it is
// destroyed, which is worth 400 points. Whichever, it first runs its trail's
// 24 frames out on the spot, in the one frame.
//
// ## The stretches
//
//     $81:B664  launch   24 of the budget at `$00DE`, and on to ask for a
//                        record
//     $81:B6C1  dress    the record, the page, the handler, and a wait of 35
//     $81:B67B  begin    the first point to wander to, and the first yield
//     $81:B687  frame    from each wake: the state body, the picture, the
//                        trail. Then the yield, or the way out.
//     $81:B6A8  scored   the way out, from after `score_add`
//
// Port code: libc only.

#ifndef PORT_FLAME_H
#define PORT_FLAME_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/wram.h"

#define FLAME_BANK 0x81u

#define FLAME_LAUNCH_PC 0x81b664u
#define FLAME_DRESS_PC 0x81b6c1u   // after `JSL actor_slot_alloc`
#define FLAME_BEGIN_PC 0x81b67bu   // after the first yield
#define FLAME_FRAME_PC 0x81b687u   // after every other
#define FLAME_SCORED_PC 0x81b6a8u  // after `JSL score_add`
// Where they leave.
#define FLAME_ALLOC_CALL_PC 0x81b6bdu
#define FLAME_FIRST_YIELD_PC 0x81b677u
#define FLAME_YIELD_PC 0x81b683u   // `JSL thread_yield`, A already 1
#define FLAME_SCORE_CALL_PC 0x81b6a4u
#define FLAME_FREE_JML_PC 0x81b6b9u
#define FLAME_YIELD_TICKS 1
#define FLAME_FIRST_TICKS 0x0023

// Fields on its page. The first two are where the doll stood.
#define FLAME_DP_FROM_X 0x00
#define FLAME_DP_FROM_Y 0x02
#define FLAME_DP_RECORD 0x08
#define FLAME_DP_LEAVE 0x0a        // nonzero ends the thread
#define FLAME_DP_HEALTH 0x0c       // the handler's
#define FLAME_DP_STATE 0x0e
#define FLAME_DP_TURN_WAIT 0x10    // frames until the picture changes
#define FLAME_DP_FRAMES_LEFT 0x12  // of the 800
#define FLAME_DP_SCRATCH 0x14
#define FLAME_DP_FRAMES 0x16       // the animation table
#define FLAME_DP_X 0x18
#define FLAME_DP_Y 0x1c
#define FLAME_DP_TO_X 0x1e         // the point being tried, or aimed at
#define FLAME_DP_TO_Y 0x20
#define FLAME_DP_STRIDE 0x36       // which of four pictures, 0 to 3
#define FLAME_DP_LEG_X 0x38        // a wander's two legs, as lengths
#define FLAME_DP_LEG_Y 0x3a
#define FLAME_DP_TARGET 0x3c
#define FLAME_DP_AIM_DX 0x3e       // from here to `$1E`/`$20`: the distance...
#define FLAME_DP_AIM_STEP_X 0x40   // ...and 1, 0 or -1, on each axis
#define FLAME_DP_AIM_DY 0x42
#define FLAME_DP_AIM_STEP_Y 0x44
#define FLAME_DP_FACING 0x46       // a facing times four, as a doll's is
#define FLAME_DP_STEPS_LEFT 0x4a
#define FLAME_DP_TRAILS 0x50       // how many of the two places are in use
#define FLAME_DP_TRAIL 0x52        // two of: a record or `$FFFF`, frames left
#define FLAME_DP_HIT_ID 0x5a       // what hit it, cleared every frame

#define FLAME_TRAIL_PLACES 2
#define FLAME_TRAIL_STRIDE 4
#define FLAME_NO_TRAIL 0xffffu

#define FLAME_STATE_WANDER 0xb77du
#define FLAME_STATE_CHASE 0xb7eeu

// How much of the budget at `$00DE` it is: as much as a doll.
#define FLAME_BUDGET 0x0018
#define FLAME_HEALTH 8
#define FLAME_FRAMES_LEFT 0x0320
#define FLAME_POINTS 0x0400
#define FLAME_COLLIDE_ID 0x0003
#define FLAME_ATTR 0x0c00
#define FLAME_HANDLER 0xb95fu
// How many of them have been destroyed.
#define W_FLAMES_DESTROYED 0x1f6au
// Its pictures, in bank `$90`: the first, a trail's first, and the table the
// doll's picture numbers index, where a trail's six begin at number `$1B`.
#define FLAME_PICTURE 0xce26u
#define FLAME_TRAIL_PICTURE 0xce1cu
#define FLAME_PICTURE_BANK 0x0090
#define FLAME_PICTURES 0xacd8u
#define FLAME_TRAIL_PICTURES 0x0036  // a byte offset into it
#define FLAME_ANIMATION 0xae66u      // four bytes a frame, as a doll's

enum {
  FL_LAUNCH,         // $B664-$B670
  FL_DRESS,          // $B6C1-$B721
  FL_HANDLER,        // $B722-$B72B, and `$80:8475` itself
  FL_DRESS_TAIL,     // $B72C, $B671-$B676, and `$81:B8A4` itself
  FL_JSR,            // any of the `JSR`s
  FL_JMP,            // ...and of the `JMP`s
  FL_RTS,
  FL_TAKEN,          // a branch taken
  FL_AGAIN,          // $B67E-$B682
  FL_DISPATCH,       // $B687-$B68E
  FL_PICK,           // $B72D-$B742, and $B749-$B75E the same
  FL_PICK_NEG,       // $B743-$B748
  FL_PICK_TAIL,      // $B765-$B77C
  FL_AIM,            // $B24D-$B27D less the next two, twice each
  FL_AIM_DEX,        // $B257
  FL_AIM_NEG,        // $B25A-$B260
  FL_AXIS_HEAD,      // $B83E-$B843
  FL_AXIS_DOWN,      // $B844-$B847
  FL_AXIS_ACROSS,    // $B848-$B849
  FL_FACE_HEAD,      // $B84A-$B850, and $B85A-$B860 the same
  FL_FACE_BMI,       // $B851-$B852
  FL_FACE_LDY,       // $B853-$B855
  FL_FACE_STY_BRA,   // $B856-$B859
  FL_FACE_STY,       // $B866-$B867
  FL_FACE_NONE,      // $B869-$B86E
  FL_W_HEAD,         // $B77D-$B784
  FL_W_LOOK,         // $B785-$B793
  FL_COUNT,          // $B794-$B797, and $B7EE-$B7F1 the same
  FL_STEP,           // $B798-$B7AD, and $B7F5-$B80A the same
  FL_TAKE,           // $B7AE-$B7B5, and $B82F-$B836 the same
  FL_W_PLAYERS,      // $B7B6-$B7C3
  FL_DEC,            // $B7C4-$B7C5: a `DEC` of the page
  FL_C_START,        // $B7CD-$B7E3
  FL_C_START_TAIL,   // $B7E4-$B7ED
  FL_C_GAP,          // $B80B-$B814, and $B819-$B822 the same
  FL_NEG,            // $B815-$B818
  FL_C_SUM,          // $B827-$B82E
  FL_C_STEP_TAIL,    // $B837-$B839
  FL_A_HEAD,         // $B86F-$B872
  FL_A_TURN,         // $B873-$B885
  FL_SHOW_HEAD,      // $B1A1-$B1A9
  FL_SHOW_ORA,       // $B1AA-$B1B1
  FL_SHOW_AND,       // $B1B2-$B1B7
  FL_SHOW_TAIL,      // $B1B8-$B1CA
  FL_A_TRAIL_TEST,   // $B886-$B88D
  FL_A_TAIL,         // $B891-$B8A0
  FL_T_HEAD,         // $B8E4-$B8E6, and $B8AE-$B8B0 the same
  FL_T_TEST,         // $B8E7-$B8ED, and $B8B1-$B8B7 the same
  FL_T_NEXT,         // $B8EE-$B8F5, and $B8D4-$B8DB the same
  FL_T_FOUND,        // $B8F7-$B8FB
  FL_T_DRESS,        // $B8FC-$B94E
  FL_G_COUNT,        // $B8B8-$B8BB
  FL_G_PHASE,        // $B8BC-$B8C4
  FL_G_SHOW,         // $B8C5-$B8D3
  FL_G_FREE,         // $B8DD-$B8E3, and $B94F-$B95E
  FL_L_TEST,         // $B695-$B698, and $B699-$B69C the same
  FL_L_SCORE_TEST,   // $B69D-$B6A3
  FL_L_END,          // $B6AB-$B6B8
  FL_S_INC,          // $B6A8-$B6AA
  FL_BLOCK_COUNT
};

// What a stretch did, for the harness to price: the runs, and what each call
// it made in C answered.
typedef struct {
  uint16_t blocks[FL_BLOCK_COUNT];
  int draws;               // random numbers drawn...
  bool draw_v[2];          // ...and whether each left overflow set
  bool looked;             // `actor_nearest` asked
  ActorNearestWork nearest;
  int grounds;             // steps tried on the ground
  TerrainRegs ground[2];
  bool asked_players;      // `player_in_range` asked
  PlayerPickRegs players;
  bool trailed;            // a trail's record taken...
  uint16_t trail_record;
  int frees;               // ...and how many given back,
  int free_place[FLAME_TRAIL_PLACES];  // each from where in the display list
} FlameWork;

void flame_launch(Wram* w, PortCpu* c, FlameWork* k);
void flame_dress(Wram* w, PortCpu* c, FlameWork* k);
void flame_begin(Wram* w, PortCpu* c, FlameWork* k);
void flame_frame(Wram* w, const Rom* rom, PortCpu* c, FlameWork* k);
void flame_scored(Wram* w, PortCpu* c, FlameWork* k);

// Can `flame_frame` run this page's frame as the ROM would? Not when the
// state is neither of the two, when the trail's places and the display list
// disagree, or when a trail would be left and no record is free, which the
// ROM does not check.
bool flame_frame_supported(const Wram* w, uint16_t page);

#endif
