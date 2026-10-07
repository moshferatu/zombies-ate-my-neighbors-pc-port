// The thing in level 37 that comes at a player.
//
// Its thread sleeps at `$82:EF4B` for as many frames as `$2C` says, and then
// runs the state `$16` names. A frame of that is here, for nine states:
//
//   $82:E858  It comes at whoever is nearest, a pixel a frame. From nearer
//             than `$20` it backs off instead. Between `$20` and `$30` it
//             starts to circle them.
//   $82:E8F9  It circles: forty places round whoever it chose, forty-eight
//             pixels out, one way or the other. Every fourth frame of the
//             game it goes by way of `$82:E8D9` next.
//   $82:E8D9  ...which asks whether they are still there and still the
//             nearest, and goes on circling.
//   $82:EB64  It stays where it is and faces whoever is nearest.
//   $82:EB7A  The same, with another thing to do next.
//   $82:ECA3  The same again, and its record set to be drawn every frame.
//   $82:ECF7  Three frames in which its record is put at its own place and
//             at the place at `$0A` and `$0C` by turns, drawn on the game's
//             even frames and not on the odd ones.
//   $82:ECC1  It darts at whoever is nearest, four pixels a hop, straight up,
//             down or across: whichever they are further off along. A hop is
//             the three frames above and then its place moved. It stops
//             within `$10` of them, or after eight to twenty-three hops.
//   $82:EBCF  Hidden, it looks for somewhere to come back: a place 24 to 39
//             pixels from whoever is nearest each way, drawn afresh every
//             frame until the ground there will do.
//
// The third of those goes on to the dart on thirty draws in 256. The fourth
// goes on to the state at `$82:EE0E` on thirty-five, which begins threads at
// `$82:F03E` and is the ROM's.
//
// What else each goes on to do when it stops is the ROM's: `seeker_frame`
// says false for a frame that gets there, having changed only the copy it
// was given. Those are the frames that sleep inside a state: it hides, it
// comes back, it is hit.
//
// Two leaves of it are entries of their own as well.
//
// `$82:E7C7` takes the way in A and moves the thing a pixel along it. Its
// place is kept on its page, at `$0E` and `$10`, and copied to its display
// record. The table at `$82:E7E3` has a step across and a step down for
// each of nine ways, the first of which is none.
//
// `$82:E807` changes its picture: every fifth call the record gets the
// other of two.
//
// Port code: libc only.

#ifndef PORT_SEEKER_H
#define PORT_SEEKER_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/terrain.h"
#include "port/wram.h"

#define SEEKER_STEP_PC 0x82e7c7u
#define SEEKER_STEP_RTS_PC 0x82e7e2u
#define SEEKER_FLAP_PC 0x82e807u
#define SEEKER_FLAP_RTS_PC 0x82e822u

#define SEEKER_BANK 0x82
#define SEEKER_STEPS 0x82e7e3u
#define SEEKER_WAYS 9
#define SEEKER_PICTURES 0x82e823u
#define SEEKER_FLAP_FRAMES 4  // counted down past zero
#define SEEKER_DP_RECORD 0x08
#define SEEKER_DP_X 0x0e
#define SEEKER_DP_Y 0x10
#define SEEKER_DP_STATE 0x16
#define SEEKER_DP_PICTURE 0x18
#define SEEKER_DP_FRAMES 0x1a
#define SEEKER_DP_WAY 0x1c
#define SEEKER_DP_TARGET 0x1e    // the record of whoever it chose
#define SEEKER_DP_SCRATCH 0x22
#define SEEKER_DP_PLACE 0x26     // circling: which of the forty places, by 4
#define SEEKER_DP_TURN 0x28      // ...and what a frame adds to that
#define SEEKER_DP_LAPS 0x2a      // ...and how often it has come round
#define SEEKER_DP_SLEEP 0x2c     // frames the thread sleeps for
#define SEEKER_DP_ENDED 0x2e     // not zero ends the loop
#define SEEKER_DP_STRENGTH 0x30  // the more, the longer between its frames
#define SEEKER_DP_HURT 0x32      // negative when something has hit it

// The loop: the return of the sleep, the sleep, and what follows the loop.
#define SEEKER_FRAME_PC 0x82ef4fu
#define SEEKER_FRAME_SLEEP_PC 0x82ef4bu  // `JSL`, A already the frames
#define SEEKER_FRAME_ENDED_PC 0x82ef5bu

#define SEEKER_STATE_CHASE 0xe858u
#define SEEKER_STATE_CIRCLE_ASK 0xe8d9u
#define SEEKER_STATE_CIRCLE 0xe8f9u
#define SEEKER_STATE_WATCH 0xeb64u
#define SEEKER_STATE_WATCH_B 0xeb7au
#define SEEKER_STATE_HOLD 0xeca3u
#define SEEKER_STATE_BLINK 0xecf7u
#define SEEKER_STATE_DART 0xecc1u
#define SEEKER_STATE_PLACE 0xebcfu
#define SEEKER_STATE_EE0E 0xee0eu  // the ROM's

#define SEEKER_TOO_NEAR 0x0020
#define SEEKER_NEAR 0x0030
#define SEEKER_BACK_WAYS 0x82e89au    // the way opposite each way
#define SEEKER_CIRCLE 0x82e96cu       // a place: a step across, a step down
#define SEEKER_CIRCLE_END 0x00a0      // forty places of four bytes
#define SEEKER_CIRCLE_LAST 0x009c
#define SEEKER_CIRCLE_LOST 0x0018     // someone else this near takes over
#define SEEKER_FACINGS 0x82e7b5u      // the picture for each way
#define SEEKER_FACES_RIGHT 0x000c     // ways, doubled, from which it is flipped
#define SEEKER_FLIPPED 0x0002
#define SEEKER_TOO_FAR 0x00f0
#define SEEKER_SWOOP_ODDS 0x37        // of 256, each frame after a lap
#define SEEKER_WATCH_ODDS 0x23
#define SEEKER_WATCH_B_ODDS 0x28
#define SEEKER_HOLD_ODDS 0x1e
#define SEEKER_BLINK_FRAMES 4    // what a frame takes from `$1A`
#define SEEKER_DP_AHEAD_X 0x0a   // the other place it is shown at
#define SEEKER_DP_AHEAD_Y 0x0c
#define SEEKER_BLINK_START 0x000c  // three frames of four
#define SEEKER_DP_STEP_X 0x12    // a hop, and the place it looks to come back
#define SEEKER_DP_STEP_Y 0x14
#define SEEKER_DP_DIST 0x20      // how far whoever is nearest was
#define SEEKER_DP_HOPS 0x24      // hops left
#define SEEKER_DART_NEAR 0x0010
#define SEEKER_HOP 4
#define SEEKER_HOPS_LEAST 8      // and up to fifteen more
#define SEEKER_HANDLER 0xeff0u   // `enemy_eff0_collide`, `port/collide.h`
#define SEEKER_HANDLER_BANK 0x0082u
// The ways a hop can go, doubled: indexes of the table of pictures.
#define SEEKER_WAY_UP 0x0002
#define SEEKER_WAY_ACROSS 0x0006
#define SEEKER_WAY_DOWN 0x000a
#define SEEKER_WAY_BACK 0x000e
#define SEEKER_PLACE_LEAST 0x0030  // halved: 24 pixels, and up to 15 more
#define SEEKER_MAX_LOOKS 2
#define SEEKER_MAX_DRAWS 2

enum {
  SF_HEAD,          // PEA, LDA $16 : DEC : PHA, RTS
  SF_TAIL,          // LDA $2E : BEQ
  SF_AGAIN,         // LDA $2C
  SF_CHASE,         // $E858-$E866
  SF_CHASE_MID,     // CMP #$0030 : BCC
  SF_CHASE_FAR,     // TXY : LDX $08 : JSL
  SF_CHASE_GO,      // JSR, JSR, RTS
  SF_CHASE_BACK,    // $E87A-$E887
  SF_CHASE_CIRCLE,  // $E888-$E899
  SF_CIRCLE_BEGIN,  // $E8AC-$E8D8
  SF_ASK,           // $E8D9-$E8DF
  SF_ASK_LOOK,      // $E8E0-$E8EB
  SF_ASK_NEAR,      // CMP #$0018 : BCS
  SF_ASK_SET,       // LDA #$E8F9 : STA $16
  SF_CIRCLE,        // $E8F9-$E914
  SF_CIRCLE_CMP,    // CMP #$00A0 : BCC
  SF_CIRCLE_OVER,   // LDA #$0000 : INC $2A : BRA
  SF_CIRCLE_UNDER,  // LDA #$009C : INC $2A
  SF_CIRCLE_PUT,    // $E926-$E93E
  SF_CIRCLE_ASK,    // LDA #$E8D9 : STA $16
  SF_CIRCLE_LAPS,   // LDA $2A : CMP #$0001 : BCS
  SF_CIRCLE_DRAW,   // JSL, CMP #$0037 : BCS
  SF_WATCH,         // JSR, JSL, CMP : BCS
  SF_FACE,          // LDA $32 : BMI
  SF_FACE_LOOK,     // $EB17-$EB23
  SF_FACE_TURN,     // $EB24-$EB3D
  SF_FACE_LEFT,     // LDA #$FFFD : AND $0000,Y : BRA
  SF_FACE_RIGHT,    // LDA #$0002 : ORA $0000,Y
  SF_FACE_END,      // STA $0000,Y : RTS
  SF_STEP,          // $E7C7-$E7E2
  SF_FLAP,          // DEC $1A : BPL
  SF_FLAP_PICTURE,  // $E80B-$E821
  SF_RTS,
  SF_TAKEN,
  SF_SHOW,          // $EC97-$ECA2
  SF_BLINK,         // $ECF7-$ECFE
  SF_BLINK_WHERE,   // $ECFF-$ED0E
  SF_BLINK_ON,      // LDA #$8000 : ORA $0000,Y : BRA
  SF_BLINK_OFF,     // LDA #$7FFF : AND $0000,Y
  SF_BLINK_PUT,     // $ED1D-$ED2A
  SF_GO_EE0E,       // JMP $EDFD, and $EDFD-$EE0D
  SF_HOLD_GO,       // $ECAF-$ECC0
  SF_DART,          // $ECC1-$ECCD
  SF_DART_NEAR,     // JMP $EB5E, and $EB5E-$EB63
  SF_DART_COUNT,    // $ECD1-$ECD8
  SF_DART_OVER,     // $ED2B-$ED42, and $EB5E-$EB63
  SF_DART_FAR,      // LDA $20 : CMP #$00F0 : BCS
  SF_DART_AIM,      // JSR $ED75, and $ECE3-$ECEC
  SF_DART_SET,      // $ECED-$ECF6
  SF_DART_ON,       // $ED51-$ED5B
  SF_AIM_X,         // $ED75-$ED80
  SF_NEGATE,        // EOR #$FFFF : INC
  SF_AIM_Y,         // $ED85-$ED90
  SF_AIM_CMP,       // STA $14 : CMP $12 : BCS
  SF_AIM_ACROSS,    // $ED9B-$EDA6
  SF_AIM_BACK,      // $EDA7-$EDB3
  SF_AIM_DOWN,      // $EDB4-$EDBF
  SF_AIM_UP,        // $EDC0-$EDCA
  SF_AIM_PUT,       // $EDCB-$EDEA
  SF_CLAMP,         // LDA : CMP #$0004 : BCC
  SF_CLAMP_SET,     // LDA #$0004 : STA
  SF_PLACE,         // $EBCF-$EBDD
  SF_PLACE_DRAW,    // $EBE8-$EBF5
  SF_PLACE_X,       // $EBFA-$EC0D
  SF_PLACE_Y,       // $EC12-$EC21
  SF_PLACE_MAP,     // $EC22-$EC2B
  SF_BLOCK_COUNT
};

// For the harness: what ran, and what each call under it did.
typedef struct {
  uint16_t blocks[SF_BLOCK_COUNT];
  int looks;    // `actor_nearest` asked, and what each did
  ActorNearestWork nearest[SEEKER_MAX_LOOKS];
  bool faced;   // `actor_bearing` asked
  ActorBearingRegs bearing;
  int draws;    // random bytes drawn, and the overflow each left
  bool draw_overflow[SEEKER_MAX_DRAWS];
  bool grounded;  // the ground asked after, by either of two tests
  TerrainRegs ground;
  bool mapped;    // ...and the level's edge
  BoundsExit map_exit;
  bool handler_set;
} SeekerWork;

// A frame of the thread, from the sleep's return to the next sleep or to
// the end of the loop. False for a frame that goes on to something that is
// the ROM's.
bool seeker_frame(Wram* w, const Rom* rom, PortCpu* c, SeekerWork* k);

// A is the way. False for one the table does not have.
bool seeker_step_supported(uint16_t way);
void seeker_step(Wram* w, const Rom* rom, PortCpu* c);

// True if the picture changed.
bool seeker_flap(Wram* w, const Rom* rom, PortCpu* c);

#endif
