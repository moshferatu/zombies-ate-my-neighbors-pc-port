// The big figure's thread on level 25: its turn, and what it decides to do.
//
// `port/boss.h` moves the figure and `port/bossbg.h` draws it. This is the
// thread that owns it, at `$82:9569`, and each frame is one turn:
//
//     the next picture of its stride, every ninth frame
//     the state's own routine, by the word at `$1A`
//     a flash, if it was hit
//     where its plane is drawn        `$82:9248`
//     its four parts                  boss_place_parts
//     what it stands on               boss_stomp
//     its spit, two of the four a frame
//     sleep a frame, and again while nothing has set `$3A`
//
// **The states** are what it decides, and there are six that move it:
//
//     pacing      east or west, until a player comes within 480
//     rampaging   three double steps a frame, turning when it is stuck
//     going       to a point, two double steps a frame
//     stamping    36 frames on the spot, the screen shaking under it
//     home        north or south to the row it started on
//     lining up   to a spot beside a player, to spit at them
//
// Each ends by naming the next, and `boss_choose` at `$82:89D2` is where it
// goes when it has nothing in mind. `$44` is its temper: 6 when calm, 1 while
// it rampages, and negative when a hit has stung it, which every state tests
// first.
//
// **And two that spit.** In place beside a player it opens its mouth, which
// is a picture of its own and a bottle made of two of its four parts. Then
// one state aims a spit, a record of its own, by which way the player is
// from its mouth, and the other plays the bottle's pictures over it. When
// the player is out of reach, or right under it, or four are out, it shuts
// its mouth and lines up again.
//
// How the thread starts and ends is the ROM's still.
//
// **Written as stretches.** Each is from where a call comes back to the next
// call that this file does not make itself. It makes the ones whose cost is
// known to the cycle: the step, a draw, the nearer player and which way they
// are, the nearest thing. The rest are the ROM's instructions and the
// harness makes them: the picture's upload, the colours, the shake's job, its
// cry, a spit's record and what it hits.
//
// Every stretch stops on an instruction of the ROM's, and `boss_thread_run`
// leaves that address in `pc`.
//
// Port code: libc only.

#ifndef PORT_BOSS_THREAD_H
#define PORT_BOSS_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/boss.h"
#include "port/cpu.h"
#include "port/oam.h"
#include "port/wram.h"

#define BOSS_THREAD_BANK 0x82u

// Direct page, the thread's own.
#define BOSS_DP_SPIT_FROM_X 0x00u // where a spit starts
#define BOSS_DP_SPIT_FROM_Y 0x02u
#define BOSS_DP_SPIT_WAY 0x04u    // ...and which of its eight ways it goes
#define BOSS_DP_SCRATCH 0x08u     // and `$0A`: a hop's two offsets
#define BOSS_DP_SPIT_FAR 0x0au    // how far the player is from its mouth
#define BOSS_DP_SPOT_X 0x0cu      // where it would stand to spit
#define BOSS_DP_SPOT_Y 0x0eu
#define BOSS_DP_TIMER 0x14u       // frames left of what it is doing
#define BOSS_DP_WAY 0x16u         // a direction times 8: `boss_step`'s
#define BOSS_DP_STATE 0x1au       // the state's routine
#define BOSS_DP_HOME_Y 0x1cu      // the row it started on
#define BOSS_DP_PLAYER 0x1eu      // the player it is after
#define BOSS_DP_TO_X 0x20u        // the point it is going to
#define BOSS_DP_TO_Y 0x22u
#define BOSS_DP_STRIDE_LEFT 0x2cu // frames to the stride's next picture
#define BOSS_DP_STRIDE 0x2eu      // which of its four
#define BOSS_DP_PICTURES 0x30u    // the bottle's list for this spit...
#define BOSS_DP_PICTURE_LEFT 0x32u  // ...frames to its next picture...
#define BOSS_DP_PICTURE 0x34u     // ...and how far down the list
#define BOSS_DP_FACING 0x36u      // not zero: mirrored
#define BOSS_DP_SPIT_FACING 0x38u // the side it will spit from
#define BOSS_DP_DEAD 0x3au
#define BOSS_DP_HIT 0x3eu         // bit 15: hit since the last flash
#define BOSS_DP_FLASH_LEFT 0x40u
#define BOSS_DP_SCORED 0x42u      // cleared every turn
#define BOSS_DP_TEMPER 0x44u
#define BOSS_DP_SPITS 0x46u       // how many of the four are out
#define BOSS_DP_SPIT_RECORD 0x48u // four of these four words, 8 apart
#define BOSS_DP_SPIT_LEFT 0x4au   // frames to fly; negative, the burst's
#define BOSS_DP_SPIT_DX 0x4cu
#define BOSS_DP_SPIT_DY 0x4eu
#define BOSS_DP_SPIT 0x68u        // the one being moved

#define BOSS_SPIT_NONE 0xffffu

// Where on its plane the picture is, which `$82:9248` takes off its place.
#define W_BOSS_PLANE_AT_X 0x1e6au
#define W_BOSS_PLANE_AT_Y 0x1e6cu

// The states, as `$1A` holds them.
#define BOSS_STATE_PACING 0x89f8u
#define BOSS_STATE_RAMPAGING 0x8a58u
#define BOSS_STATE_GOING 0x8b2eu
#define BOSS_STATE_STAMPING 0x8b90u
#define BOSS_STATE_HOME 0x8c79u
#define BOSS_STATE_LINING_UP 0x8cceu
#define BOSS_STATE_SPITTING 0x8e2du
#define BOSS_STATE_SPIT_SHOWS 0x8edcu

#define BOSS_TEMPER_CALM 0x0006u
#define BOSS_TEMPER_RAMPAGE 0x0001u

#define BOSS_SIGHT 0x01e0u         // how far it sees a player
#define BOSS_STEP_PLAIN 0x0000u    // anything but `BOSS_STEP_FAST`
#define BOSS_STAMP_FRAMES 0x0024u
#define BOSS_HOME_FRAMES 0x0060u
#define BOSS_STRIDE_FRAMES 0x0008u
#define BOSS_FLASH_FRAMES 0x0003u

// Its cry is heard only while this word is 2.
#define W_BOSS_SOUNDS 0x1f52u
#define BOSS_SOUNDS_ITS_OWN 0x0002u
#define BOSS_CRY_SFX 0x0025u

// A part's flag bit that draws it facing the other way.
#define BOSS_PART_MIRRORED 0x0002u

// The thing ids that are a player, and the third it also minds.
#define BOSS_ID_PLAYER_A 0x0005u
#define BOSS_ID_PLAYER_B 0x0006u
#define BOSS_ID_MINDED 0x0038u

// Where each stretch begins.
#define BOSS_CHOOSE_PC 0x8289d2u
#define BOSS_PACING_PC 0x8289f8u
#define BOSS_PACING_STEP_PC 0x828a0eu  // after it chose again
#define BOSS_RAMPAGING_PC 0x828a58u
#define BOSS_GOING_PC 0x828b2eu
#define BOSS_STAMPING_PC 0x828b90u
#define BOSS_HOME_PC 0x828c79u
#define BOSS_LINING_UP_PC 0x828cceu
#define BOSS_SPIT_BEGINS_PC 0x828d6cu
#define BOSS_TURNS_TO_SPIT_PC 0x828d7bu  // after its cry
#define BOSS_OPENS_MOUTH_PC 0x828d83u    // and again each frame it waits
#define BOSS_MOUTH_OPEN_PC 0x828d98u     // after the picture
#define BOSS_STOP_SPITTING_PC 0x828ddeu
#define BOSS_MOUTH_SHUT_PC 0x828e0eu     // after the picture
#define BOSS_LINGERS_PC 0x828e1au
#define BOSS_LINGERS_SLEEPS_PC 0x828e21u
#define BOSS_SPITTING_PC 0x828e2du
#define BOSS_SPIT_SHOWS_PC 0x828edcu
#define BOSS_SPIT_MADE_PC 0x829324u      // after its record
#define BOSS_SPIT_SECOND_PC 0x8293e1u  // the frame's other spit
#define BOSS_SPIT_MOVES_PC 0x8293f7u
#define BOSS_SPIT_GONE_PC 0x829461u    // its record freed
#define BOSS_SPIT_HITS_PC 0x829475u    // the box round it, to be told
#define BOSS_TURN_PC 0x829579u
#define BOSS_STATE_GOES_PC 0x82957eu   // after a stride's picture
#define BOSS_TURN_ENDS_PC 0x829586u    // the state came back
#define BOSS_FLASHED_PC 0x829589u      // after a flash's colours
#define BOSS_SPITS_PC 0x829592u
#define BOSS_SLEEPS_PC 0x829595u
#define BOSS_WAKES_PC 0x82959cu

// Where one stops. All but the last are instructions the harness makes; the
// last is the ROM's own code, which this file does not have.
#define BOSS_STATE_PC 0x829585u        // `RTS`, to the state
#define BOSS_PICTURE_PC 0x828918u      // `JSL boss_bg_queue`
#define BOSS_PICTURE_FLIP_PC 0x828929u // `JSL boss_bg_queue_flip`
#define BOSS_UNFLASH_PC 0x828f78u      // `JSL figure_colours_set`
#define BOSS_FLASH_PC 0x828f8eu
#define BOSS_PARTS_PC 0x82958cu        // `JSR boss_place_parts`
#define BOSS_SPIT_FIRST_CALL_PC 0x8293deu   // `JSR $93F7`
#define BOSS_SPIT_SECOND_CALL_PC 0x8293ebu
#define BOSS_SPITS_RTS_PC 0x8293eeu
#define BOSS_YIELD_PC 0x829598u        // `JSL thread_yield`
#define BOSS_SPIT_RTS_PC 0x829458u
#define BOSS_SPIT_BURST_PC 0x829455u   // `JSR $9475`, as it bursts
#define BOSS_SPIT_BURSTING_PC 0x82941au  // ...and on each picture after
#define BOSS_SPIT_FREE_PC 0x82945du    // `JSL actor_slot_free`
#define BOSS_SPIT_GONE_RTS_PC 0x82946au
#define BOSS_SPIT_TELL_PC 0x82949fu    // `JSL actor_notify_box`
#define BOSS_CHOSE_RTS_PC 0x8289f7u
#define BOSS_CHOOSE_CALL_PC 0x828a0bu  // `JSR $89D2`
#define BOSS_PACED_RTS_PC 0x828a14u
#define BOSS_RAMPAGE_BEGUN_RTS_PC 0x828a57u
#define BOSS_RAMPAGED_RTS_PC 0x828a78u
#define BOSS_TURNED_RTS_PC 0x828a89u
#define BOSS_GOING_BEGUN_RTS_PC 0x828b2du
#define BOSS_WENT_RTS_PC 0x828b6fu
#define BOSS_STAMP_BEGUN_RTS_PC 0x828b8fu
#define BOSS_SHAKE_PC 0x828bddu        // `JSL vbl_queue_a_add`
#define BOSS_HOME_BEGUN_RTS_PC 0x828c78u
#define BOSS_HOME_RTS_PC 0x828ca9u
#define BOSS_LINE_UP_BEGUN_RTS_PC 0x828ccdu
#define BOSS_LINED_UP_RTS_PC 0x828d4du
#define BOSS_CRY_PC 0x828d77u          // `JSL apu_play_sfx`
#define BOSS_OPEN_YIELD_PC 0x828d8cu   // `JSL thread_yield`
#define BOSS_MOUTH_OPEN_RTS_PC 0x828dddu
#define BOSS_STOP_YIELD_PC 0x828de7u
#define BOSS_LINGER_YIELD_PC 0x828e24u
#define BOSS_SPAT_RTS_PC 0x828ef6u
#define BOSS_SPITS_AGAIN_RTS_PC 0x828f11u
#define BOSS_SPIT_ALLOC_PC 0x829320u   // `JSL actor_slot_alloc`
#define BOSS_DIES_PC 0x8295a0u         // the ROM's: `$3A` is set

// The straight runs of the listing a stretch is made of, each named for where
// it starts, with where it ends. The harness prices a stretch by the runs it
// took, and `tools/cycles816.py` prices a run.
#define BOSS_RUNS(X) \
  X(80E0, 0x8280e4u) \
  X(890C, 0x828918u) \
  X(891D, 0x828929u) \
  X(892E, 0x828944u) \
  X(8944, 0x828948u) \
  X(8948, 0x828955u) \
  X(8955, 0x828958u) \
  X(895A, 0x82895du) \
  X(897E, 0x82898du) \
  X(898D, 0x828992u) \
  X(8992, 0x828997u) \
  X(8998, 0x8289a9u) \
  X(89A9, 0x8289b0u) \
  X(89B0, 0x8289b8u) \
  X(89B8, 0x8289bdu) \
  X(89BD, 0x8289c5u) \
  X(89D2, 0x8289e0u) \
  X(89E0, 0x8289eau) \
  X(89EA, 0x8289f7u) \
  X(89F7, 0x8289f8u) \
  X(89F8, 0x828a07u) \
  X(8A07, 0x828a0bu) \
  X(8A0E, 0x828a14u) \
  X(8A15, 0x828a1eu) \
  X(8A1E, 0x828a21u) \
  X(8A21, 0x828a2eu) \
  X(8A2E, 0x828a31u) \
  X(8A31, 0x828a34u) \
  X(8A34, 0x828a57u) \
  X(8A58, 0x828a5cu) \
  X(8A5C, 0x828a60u) \
  X(8A60, 0x828a68u) \
  X(8A68, 0x828a70u) \
  X(8A70, 0x828a78u) \
  X(8A79, 0x828a89u) \
  X(8A8A, 0x828a97u) \
  X(8A97, 0x828a9au) \
  X(8A9A, 0x828a9du) \
  X(8A9D, 0x828aaeu) \
  X(8AAE, 0x828abeu) \
  X(8B06, 0x828b2du) \
  X(8B2E, 0x828b35u) \
  X(8B35, 0x828b38u) \
  X(8B38, 0x828b4eu) \
  X(8B4E, 0x828b5fu) \
  X(8B5F, 0x828b67u) \
  X(8B67, 0x828b6fu) \
  X(8B70, 0x828b7fu) \
  X(8B7F, 0x828b82u) \
  X(8B82, 0x828b85u) \
  X(8B85, 0x828b8fu) \
  X(8B90, 0x828b94u) \
  X(8B94, 0x828b9cu) \
  X(8B9C, 0x828bb0u) \
  X(8BB0, 0x828bb5u) \
  X(8BB5, 0x828bbau) \
  X(8BBA, 0x828bbcu) \
  X(8BBC, 0x828bc1u) \
  X(8BC1, 0x828bc6u) \
  X(8BC6, 0x828bcbu) \
  X(8BCB, 0x828bd4u) \
  X(8BD4, 0x828bd7u) \
  X(8BD7, 0x828bddu) \
  X(8BE2, 0x828be5u) \
  X(8BE5, 0x828bffu) \
  X(8BFF, 0x828c02u) \
  X(8C02, 0x828c14u) \
  X(8C14, 0x828c1cu) \
  X(8C1C, 0x828c2eu) \
  X(8C2E, 0x828c36u) \
  X(8C36, 0x828c49u) \
  X(8C5F, 0x828c6eu) \
  X(8C6E, 0x828c71u) \
  X(8C71, 0x828c78u) \
  X(8C79, 0x828c7du) \
  X(8C7D, 0x828c85u) \
  X(8C85, 0x828c89u) \
  X(8C89, 0x828c8eu) \
  X(8C8E, 0x828c96u) \
  X(8C96, 0x828c9au) \
  X(8C9A, 0x828ca9u) \
  X(8CAA, 0x828cadu) \
  X(8CAD, 0x828cb0u) \
  X(8CB0, 0x828cbdu) \
  X(8CBD, 0x828cc5u) \
  X(8CC5, 0x828ccdu) \
  X(8CCE, 0x828cdfu) \
  X(8CDF, 0x828ce3u) \
  X(8CE3, 0x828cecu) \
  X(8CEC, 0x828cf0u) \
  X(8CF0, 0x828cf5u) \
  X(8CF5, 0x828cfcu) \
  X(8CFC, 0x828d00u) \
  X(8D00, 0x828d17u) \
  X(8D17, 0x828d1bu) \
  X(8D1B, 0x828d20u) \
  X(8D20, 0x828d28u) \
  X(8D28, 0x828d2cu) \
  X(8D2C, 0x828d31u) \
  X(8D31, 0x828d33u) \
  X(8D33, 0x828d3cu) \
  X(8D3C, 0x828d49u) \
  X(8D49, 0x828d4du) \
  X(8D4E, 0x828d53u) \
  X(8D53, 0x828d69u) \
  X(8D69, 0x828d6cu) \
  X(8D6C, 0x828d74u) \
  X(8D74, 0x828d77u) \
  X(8D7B, 0x828d83u) \
  X(8D83, 0x828d89u) \
  X(8D89, 0x828d8cu) \
  X(8D92, 0x828d98u) \
  X(8D98, 0x828db5u) \
  X(8DB5, 0x828dc9u) \
  X(8DC9, 0x828ddbu) \
  X(8DDB, 0x828dddu) \
  X(8DDE, 0x828de4u) \
  X(8DE4, 0x828de7u) \
  X(8DED, 0x828e0eu) \
  X(8E0E, 0x828e12u) \
  X(8E12, 0x828e15u) \
  X(8E15, 0x828e1au) \
  X(8E1A, 0x828e1eu) \
  X(8E1E, 0x828e21u) \
  X(8E21, 0x828e24u) \
  X(8E2A, 0x828e2du) \
  X(8E2D, 0x828e38u) \
  X(8E38, 0x828e3cu) \
  X(8E3C, 0x828e41u) \
  X(8E41, 0x828e48u) \
  X(8E48, 0x828e4cu) \
  X(8E4C, 0x828e65u) \
  X(8E65, 0x828e68u) \
  X(8E68, 0x828e6fu) \
  X(8E6F, 0x828e72u) \
  X(8E72, 0x828e78u) \
  X(8E78, 0x828e82u) \
  X(8E82, 0x828e90u) \
  X(8E90, 0x828e9cu) \
  X(8E9C, 0x828eceu) \
  X(8ECE, 0x828ed0u) \
  X(8ED0, 0x828ed3u) \
  X(8ED3, 0x828edcu) \
  X(8EDC, 0x828ee0u) \
  X(8EE0, 0x828eefu) \
  X(8EEF, 0x828ef4u) \
  X(8EF4, 0x828ef6u) \
  X(8EF7, 0x828efdu) \
  X(8EFD, 0x828f00u) \
  X(8F00, 0x828f09u) \
  X(8F09, 0x828f0cu) \
  X(8F0C, 0x828f11u) \
  X(8F6A, 0x828f6eu) \
  X(8F6E, 0x828f72u) \
  X(8F72, 0x828f78u) \
  X(8F7D, 0x828f81u) \
  X(8F81, 0x828f8eu) \
  X(9204, 0x829219u) \
  X(9219, 0x82921cu) \
  X(921C, 0x829220u) \
  X(9220, 0x829224u) \
  X(9224, 0x82922au) \
  X(922A, 0x82923fu) \
  X(9248, 0x829265u) \
  X(9303, 0x829306u) \
  X(9306, 0x82930eu) \
  X(930E, 0x829315u) \
  X(9315, 0x829317u) \
  X(9317, 0x82931cu) \
  X(931C, 0x82931eu) \
  X(931E, 0x829320u) \
  X(9324, 0x82937eu) \
  X(93C6, 0x8293dcu) \
  X(93DC, 0x8293deu) \
  X(93E1, 0x8293e9u) \
  X(93E9, 0x8293ebu) \
  X(93F7, 0x8293fdu) \
  X(93FD, 0x829401u) \
  X(9401, 0x829404u) \
  X(9404, 0x82940fu) \
  X(940F, 0x82941au) \
  X(941D, 0x829420u) \
  X(9420, 0x829440u) \
  X(9440, 0x829455u) \
  X(9459, 0x82945du) \
  X(9461, 0x82946au) \
  X(9475, 0x82949fu) \
  X(9579, 0x82957eu) \
  X(957E, 0x829585u) \
  X(9586, 0x829589u) \
  X(9589, 0x82958cu) \
  X(9592, 0x829595u) \
  X(9595, 0x829598u) \
  X(959C, 0x8295a0u)

// The calls, jumps and returns between the stretches, which the harness
// makes: see `CosimRoutine::link`. `JSR boss_stomp` at `$82:958F` is one
// more, and `port/links.h` had it first.
#define BOSS_LINKS(X) \
  X(picture_up_rts, "$82:891C", 0x82891cu) \
  X(picture_flip_up_rts, "$82:892D", 0x82892du) \
  X(picture_bra, "$82:8958", 0x828958u) \
  X(picture_rts, "$82:895D", 0x82895du) \
  X(shake_rts, "$82:8BE1", 0x828be1u) \
  X(unflashed_rts, "$82:8F7C", 0x828f7cu) \
  X(flash_rts, "$82:8F92", 0x828f92u) \
  X(stride_rts, "$82:923F", 0x82923fu) \
  X(spits_rts, "$82:93EE", 0x8293eeu) \
  X(spit_told_jmp, "$82:941D", 0x82941du) \
  X(spit_rts, "$82:9458", 0x829458u) \
  X(spit_told_rts, "$82:94A3", 0x8294a3u) \
  X(open_waits_bra, "$82:8D90", 0x828d90u) \
  X(stop_waits_bra, "$82:8DEB", 0x828debu) \
  X(lingers_bra, "$82:8E28", 0x828e28u)

typedef enum {
#define X(from, to) BOSS_RUN_##from,
  BOSS_RUNS(X)
#undef X
  BOSS_RUN_COUNT,
  BOSS_RUN_RTS = BOSS_RUN_89F7,  // any `RTS`: they all cost the same
} BossRun;

// The most of each that one stretch makes.
#define BOSS_MAX_STEPS 3
#define BOSS_MAX_DRAWS 4
#define BOSS_MAX_LOOKS 2
#define BOSS_MAX_BEARINGS 2

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t runs[BOSS_RUN_COUNT];
  uint16_t taken;  // branches taken
  BossStepWork steps[BOSS_MAX_STEPS];
  int step_count;
  bool draw_twice[BOSS_MAX_DRAWS];  // `rng_next`'s overflow, a draw
  int draws;
  PlayerPickRegs looks[BOSS_MAX_LOOKS];  // `player_in_range`
  int look_count;
  PlayerPickRegs bearings[BOSS_MAX_BEARINGS];  // `player_bearing`
  int bearing_count;
  ActorNearestWork nearest;  // `actor_nearest`
  bool sought;
  // Overflow is the ROM's at the end, which it is not after a call that
  // does not say what it left there.
  bool v_known;
} BossWork;

// True if `pc` is where one of the stretches begins.
bool boss_thread_begins_at(uint32_t pc);

// Run the stretch that begins at `c->pc`.
void boss_thread_run(Wram* w, const Rom* rom, PortCpu* c, BossWork* k);

#endif
