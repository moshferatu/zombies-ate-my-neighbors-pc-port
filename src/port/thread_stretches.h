// Stretches of threads, between their calls.
//
// Most of these threads have their loop ported already, a pass at a time.
// What was left was whatever a pass cannot hold: a thread's first
// instructions, its end, and a pass that sleeps in the middle. Others are
// short enough to be stretches and nothing else. A stretch is from where the
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
// **One that comes in over the top starts at `$81:9A3E`**, eight pixels
// below the top of the screen and at whatever X it was given. If that place
// is in the ground or off the level there is no martian: the thread ends
// before it has a record.
//
// ## The werewolf, `$81:ABF5`
//
// See `port/werewolf.h`. Its thread ends at once unless the word at `$1F94`
// is set. Otherwise it weighs 28 in the level's load, howls if the level's
// sample set is the one with its howl in it, takes a record, sleeps twenty
// ticks, and begins running with ten for what it can be hit by. Then what
// follows the end of a strike and a crouch's pictures, and the rest after a
// landing.
//
// **The check of the sample set is a stretch of its own**, `$81:AC05`. The
// frontend plays the howl itself on a level with another set, and knows to
// by watching the 65816 come to that instruction: see `src/bank_sfx.h`. A
// stretch that ran on through it would silence that, and nothing here would
// notice. So the one before stops there, and this one begins there.
//
// ## The walk's square, `$82:F6EB`
//
// See `port/walk.h`: the walk begins this thread for a square of 64 pixels
// a player walks into. It keeps the square's columns, and rows from ten
// above its top to fifty-six below it, and every six ticks asks whether
// that band still has a player in it. While it does, nothing happens.
//
//   $82:F6EB  the band set, and the first sleep
//   $82:F6F8  a pass: is somebody at or below the band's top, and somebody
//             above its bottom?
//
// **The two need not be the same player.** With one above the band and the
// other below it, the band counts as still held.
//
// When it is not held, what happens is the ROM's, `$82:F5FA`: a record is
// shown with a sound and two lists of pictures, the tile at the middle of
// the square becomes the tile before it, and the thread ends and lets the
// square go.
//
// ## A bonus object flying off, `$82:E0B4`
//
// See `port/collide.h`: a player who picks up one of the four bonus objects
// begins this thread, with the kind at `$04`. It sleeps a tick, takes a
// record, and between its calls:
//
//   $82:E0BF  the record is put where the object was, with the object's own
//             picture, drawn and with nothing that can touch it
//   $82:E102  one of the four diagonals by the draw, for twenty-one ticks
//   $82:E115  a tick of it: eight pixels each way. After the last the
//             record is given back.
//
// ## The bystander, `$82:DD52`
//
// See `port/bystander.h`, which has its loop. Its setup:
//
//   $82:DD52  `JSR $DE83`, which asks for a record at once
//   $82:DE87  the record put on the spot, drawn, with the picture its kind
//             names and nothing that can touch it. Around the spot, a box
//             forty wide and sixty-five tall, from twenty to the left and
//             ten above. Its handler is named.
//   $82:DEE8  it is not to end, and the first sleep is five ticks
//
// What it shows when a player comes up, `$82:DDA7`, waits a frame at a
// time while two words of a table at `$7E:6DA6` step, the first down by
// one and the second up by two, until the first is eight:
//
//   $82:DE08  a frame of that, as far as the next `WAI`. After the last,
//             the words its kind names are asked for.
//
// ## A frozen creature, `$81:84D6`
//
// See `port/collide.h`: five hits of the ice weapon send a creature's
// thread here, and it comes back when the freeze is over.
//
//   $81:84D6  its record takes its palette from the record's own attribute
//             word, its collide id is kept on the thread's stack, and it
//             is given `$36` in its place
//   $81:84F2  ten ticks gone, and one off the count of hits at `$7E`. Once
//             that is below zero, the record's palette and collide id are
//             put back, and the thread goes on where it was sent from.
//
// ## The chainsaw maniac's cut, `$81:92D6`
//
// See `port/chainsaw.h`: the state it is in while it cuts a hedge, with the
// hedge's place at `$42` and `$44` and the side it cuts from, one to eight,
// at `$46`. Its stretches, from where each call comes back:
//
//   $81:92D6  the cut called
//   $81:93D3  if the level's sample set is the first, the saw is heard
//   $81:93E2  a place to cut from, across or down by the side
//   $81:9326  across: a draw of one to four, times four, past twenty-eight
//             into the hedge's square of 64. The maniac is put there, and
//             sleeps a tick.
//   $81:9362  down: the same, from thirty-two
//   $81:9347  is somebody standing there? Then another draw.
//   $81:9351  the place is kept, in pixels and quarters, and a record of
//             chips is asked for beside it
//   $81:939E  the chips are put there with their first picture, drawn in
//             front of everything; side seven's the other way round. The
//             maniac is hidden while the chips show their list, eight ticks
//             a picture.
//   $81:944E  the next picture of the list, or the maniac drawn again
//   $81:92D9  the hedge's block swapped for its pair: `port/tile_rows.h`
//             has the instructions, and `cut_swap` is a row of its own
//   $81:930C  hedges cut counted at `$1FC2`, and a second list, five ticks
//             a picture
//   $81:94DA  the next picture of that, or the chips no longer in front and
//             given back
//   $81:94EE  and the maniac's handler is `$81:990B` again
//   $81:931D  its count of refused steps cleared
//
// **The check of the sample set is a stretch of its own**, `$81:93D3`, for
// the werewolf's reason: `src/bank_sfx.h` watches that instruction.
//
// ## A player's death, `$80:CE81`
//
// `$80:CE72`, which a player's thread asks every frame, has found them with
// no health, and played the sound of it. Then:
//
//   $80:CE81  a state that has an end due is ended, by its routine at
//             `$72`: the flashing's `$80:D97F`, or one of five others
//   $80:CE94  the potion's monster shows one picture for thirty ticks.
//             Anyone else is shown a list of pictures by which of the two
//             they are, and the picture they had is kept.
//   $80:CEAA  the monster's picture after, by the same
//   $80:CEC3  a life gone. With one left the kept picture is put back, and
//             `$80:D95F`: health is ten again, and they flash for 120
//             frames with nothing able to touch them.
//
// With none left it is the ROM's, `$80:CED5`, and so is what comes between
// the first and second for a player with no end due.
//
// **The lives cheat patches `$80:CEC5`**, the `DEC` of a life, which is in
// the fourth. `port_cheats.lives` says when it is on: see `port/cheat.h`.
//
// ## A locked door's buzz, `$82:DEFB`
//
// See `port/walk.h`: a player who pushes at a door with no key begins this
// thread every frame they push. The word at `$7E:1FF4` says one is buzzing
// already, and then the new one only ends. Otherwise:
//
//   $82:DEFB  the word set, and sound `$13`
//   $82:DF0A  ten ticks' sleep (an argument), sound `$13` again (another),
//             and twenty ticks' sleep (a third)
//   $82:DF1F  the word cleared, and the thread ends
//
// ## A slime's start, `$81:CCCF`
//
// See `port/slime.h`, which has its loop and `slime_begin`, its record:
//
//   $81:CCCF  its weight, thirty-two, on the level's load, and the record
//             asked for
//   $81:CCAC  its first pictures begun: its handler named, and a draw
//   $81:C9E8  one of the four ways, from the draw, and the loop's state set
//             to `$81:C9FA`. Then it sleeps four ticks.
//
// The draw and the way are `$81:C9E4`, a routine, which the slime's loop
// calls too. The second and third stretches are only the start's: each
// goes on through an `RTS`, and needs it to return to the start.
//
// ## The seeker's states that sleep, `$82:EA35` and on
//
// See `port/seeker.h`: its frame goes as far as the first call of each and
// leaves the loop's return on the stack. Then:
//
//   landing  $82:EA35  sound `$27` where the sample set is set 3
//            $82:EA44  the pictures of landing asked for
//            $82:EA4B  nine health, its handler again, and a draw...
//            $82:EA5E  ...says whether it hides or darts after firing; and
//                      it fires, drawn
//   firing   $82:EE1B  its pictures played: trackers, more for a weaker one
//            $82:EE3F  three ticks apart,
//            $82:EE4A  each sent a way round from the last
//            $82:EE62  the last pictures played: it watches and hides,
//                      watches and fires, or holds and darts
//   hiding   $82:EBA3  its going played: not drawn, and it keeps where
//                      whoever it watched was
//   back     $82:EC5A  32 ticks' sleep after its puff begins (an argument)
//            $82:EC61  things that touch it are told to its handler again,
//                      and it watches
//
// The four that end a state go on through its `RTS` to the loop, which
// sleeps or ends. Each needs the loop's return on the stack.
//
// ## A tracker's start and end, `$82:F03E`, and the seeker's puff
//
// See `port/tracker.h`, which has the tracker's loop and its record:
//
//   $82:F03E  its weight, eight, on the level's load
//   $82:F04B  its record kept, and a tick's sleep
//   $82:F05E  its loop over: its last pictures asked for
//
// The puff the seeker comes back in, `$82:F1E6`:
//
//   $82:F1E6  its weight, five, on the load
//   $82:F1F4  its record, at the place it was asked for, drawn, and its
//             pictures asked for
//
// ## The colour pulse's setup, `$80:A0EF`
//
// See `port/palcycle.h`, which has its waking. The setup keeps the level's
// colour 90 without its green, twice, and a step of one green, and sleeps
// four ticks.
//
// ## The big figure's start, level 25, `$82:9569`
//
// See `port/boss_thread.h`, which has its turns. Before the first:
//
//   $82:9569  its weight, forty, on the level's load, and its colours
//             asked for
//   $82:94EE  its tiles queued to go up, `$2800` bytes from `$93:8000` to
//             VRAM `$5000`, by `port/bossbg.h`'s `boss_bg_queue_one`
//   $82:9511  a frame at a time until they have gone up. Then its place,
//             where its plane is drawn, and a record asked for
//   $82:94B8  one of its four parts, which shows nothing and nothing can
//             touch, kept on its page. After the fourth: none of its four
//             spits out, its handler named, calm, and off to choose
//
// `$82:94B8` knows which part it has made by where its `RTS` goes.
//
// ## The saucer's start, level 21, `$82:873C`
//
// See `port/saucer.h`, which has its passes. Before the first:
//
//   $82:873C  its weight, twenty-four, on the load, and its colours
//   $82:8750  its place, and its picture drawn
//   $82:876A  its tiles queued as the figure's are, from `$93:A620`
//   $82:8786  its lights, its two shots, its health of nine, its hatch
//             shut, and the nearest thing asked for
//   $82:84DD  the spot set beside it on the side that thing is on, as a
//             hunting saucer sets it, and its first sleep
//
// ## One instruction or two
//
// What stands by itself between two calls and belongs to nothing ported: a
// word of a thread's page set to an address before a return, a list of
// pictures named, a page pulled, a sleep's ticks.
//
// ## A player's flashing ended, `$80:D97F`
//
// See `port/player_frame.h`. The player is drawn again whichever frame it
// is, and goes back to the ordinary state, or to the turning one if that is
// what ran under the flashing.
//
// ## A player shown hurt, `$80:D056`
//
// `port/hit.h` leaves a hit player here when it is shown by a list of
// pictures: the picture they had is kept on the thread's stack, the list
// plays, which sleeps, and the picture is put back.
//
// Port code: libc only.

#ifndef PORT_THREAD_STRETCHES_H
#define PORT_THREAD_STRETCHES_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/tile_rows.h"
#include "port/wram.h"

// `LDA #value` and a call: the whole of a stretch.
typedef struct {
  uint32_t pc;
  uint16_t value;
} ThreadArgument;

#define THREAD_ARGUMENT_COUNT 17
extern const ThreadArgument THREAD_ARGUMENTS[THREAD_ARGUMENT_COUNT];

// `LDA #value : STA at`, on the thread's page, and the `RTS` after.
typedef struct {
  uint32_t pc;
  uint16_t value;
  uint8_t at;
} ThreadSetter;

#define THREAD_SETTER_COUNT 2
extern const ThreadSetter THREAD_SETTERS[THREAD_SETTER_COUNT];

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

// --- the walk's square -------------------------------------------------------

#define SQUARE_ENTER_PC 0x82f6ebu
#define SQUARE_PASS_PC 0x82f6f8u       // where its sleep comes back
#define SQUARE_YIELD_PC 0x82f6f4u      // `JSL thread_yield`, six ticks in A
#define SQUARE_LEFT_PC 0x82f5fau       // nobody in the band: the ROM's
#define SQUARE_DONE_PC 0x82f704u       // ...and after that, the end
#define SQUARE_BAND_CALL_PC 0xf6ebu    // `JSR $F6BA`
#define SQUARE_HANDLER_CALL_PC 0xf6eeu // `JSR $F6AB`
#define SQUARE_ASK_CALL_PC 0xf6b1u     // the handler: `JSR $F593`
#define SQUARE_PASS_RETURN 0xf6ffu     // the `PEA` the handler returns by
#define SQUARE_HANDLER 0xf6b1u
#define SQUARE_TICKS 0x0006
#define SQUARE_SIZE 0x0040
#define SQUARE_ABOVE 0x000a            // the band's top, above the square's
#define SQUARE_TO_MIDDLE 0x0042        // ...from there to the middle
#define SQUARE_BELOW_MIDDLE 0x0024     // ...and from there to the bottom
#define W_SQUARE_PLAYERS 0x00d2u       // each player's record, or 0
#define SQUARE_DP_X 0x00               // given: where the square is
#define SQUARE_DP_Y 0x02
#define SQUARE_DP_SLOT 0x04            // ...and which of the five it is
#define SQUARE_DP_DONE 0x0a
#define SQUARE_DP_MIDDLE_X 0x0c
#define SQUARE_DP_MIDDLE_Y 0x0e
#define SQUARE_DP_HANDLER 0x10
#define SQUARE_DP_LEFT 0x18
#define SQUARE_DP_RIGHT 0x1a
#define SQUARE_DP_TOP 0x1c
#define SQUARE_DP_BOTTOM 0x1e
#define SQUARE_DP_SLOT_KEPT 0x24

// --- a bonus object flying off -----------------------------------------------

#define FLYOFF_DRESS_PC 0x82e0bfu      // where the record comes back
#define FLYOFF_HANDLER_PC 0x82e0fau    // `JSL thread_set_handler`, to none
#define FLYOFF_WAY_PC 0x82e102u        // where the draw comes back
#define FLYOFF_PASS_PC 0x82e115u       // where its sleep comes back
#define FLYOFF_YIELD_PC 0x82e111u      // `JSL thread_yield`, one tick in A
#define FLYOFF_FREE_PC 0x82e133u       // `JML actor_slot_free`
#define FLYOFF_PICTURES 0x82e147u      // by the kind
#define FLYOFF_STEPS 0x82e137u         // by the diagonal times four: x, y
#define FLYOFF_PICTURE_BANK 0x008f
#define FLYOFF_FLAGS 0x8008u           // drawn, and its priority
#define FLYOFF_TICKS 20                // and one more: the count runs to -1
#define FLYOFF_DIAGONALS 4
#define FLYOFF_DP_X 0x00               // given: where the object was
#define FLYOFF_DP_Y 0x02
#define FLYOFF_DP_KIND 0x04
#define FLYOFF_DP_RECORD 0x08
#define FLYOFF_DP_STEP_AT 0x0a
#define FLYOFF_DP_TICKS_LEFT 0x0c

// --- the bystander -----------------------------------------------------------

#define BYSTANDER_ENTER_PC 0x82dd52u
#define BYSTANDER_SETUP_RETURN 0xdd54u   // `JSR $DE83`'s
#define BYSTANDER_ALLOC_PC 0x82de83u     // `JSL actor_slot_alloc`
#define BYSTANDER_DRESS_PC 0x82de87u     // where the record comes back
#define BYSTANDER_HANDLER_CALL_PC 0x82dee4u  // `JSL thread_set_handler`
#define BYSTANDER_SET_PC 0x82dee8u       // where that comes back
#define BYSTANDER_STEP_PC 0x82de08u      // after a `WAI`
#define BYSTANDER_WAIT_PC 0x82de07u      // the `WAI`
#define BYSTANDER_SAY_PC 0x82de2bu       // `JSL $80:C219`

// `$80:DD79`: the first leap's landing, from where its sound comes back. The
// player is put where the leap was aimed, their record's pictures are bank
// `$90`'s, and it stops at the `JSL` that plays the splash, the list in A.
// `port/player_frame.h` has the rest.
#define LEAP_SPLASH_PC 0x80dd79u
#define LEAP_SPLASH_CALL_PC 0x80dd8cu  // `JSL $81:832C`
#define LEAP_SPLASH_PICTURES 0xdda5u
#define LEAP_DP_RECORD 0x08
#define LEAP_DP_X 0x30
#define LEAP_DP_Y 0x32
#define LEAP_DP_AIM_X 0x60
#define LEAP_DP_AIM_Y 0x62
#define LEAP_RECORD_PICTURE_BANK 0x0a
#define LEAP_PICTURE_BANK 0x0090u

// --- level 29's creature staggered ----------------------------------------
//
// When its stagger meter empties (`enemy_990b_stagger`, `port/collide.h`)
// its body's loop leaves for `$81:9643`. It is answered for by the stagger's
// handler, `$81:96E4`, and spins: eight pictures two ticks apart, from the
// way it faced and round, with bit 1 of its record's flags clear. Then its
// flags are put back, its collide id is 2, and it plays a list of pictures,
// another seven times, and a last. Then it is itself again: its pictures,
// collide id 3, its own handler, and its loop's state at `$12`.
//
//   $81:9643  stagger_begin  the handler and the spin begun, to the sleep
//   $81:9680  stagger_spin   the next picture and a sleep, or the spin over
//                            and the first list asked for
//   $81:96A7  stagger_dazed  the second list, the first of seven times
//   $81:96B3  stagger_daze   ...the next time, or the last list
//   $81:96BE  stagger_over   itself again, to the `JMP` to the state setter
#define STAGGER_BEGIN_PC 0x819643u
#define STAGGER_SPIN_YIELD_PC 0x81967cu  // `JSL thread_yield`, A already 2
#define STAGGER_SPIN_PC 0x819680u
#define STAGGER_SHOWN_CALL_PC 0x8196a3u  // `JSL $81:832C`, the list in A
#define STAGGER_DAZED_PC 0x8196a7u
#define STAGGER_DAZE_CALL_PC 0x8196afu
#define STAGGER_DAZE_PC 0x8196b3u
#define STAGGER_LAST_CALL_PC 0x8196bau
#define STAGGER_OVER_PC 0x8196beu
#define STAGGER_BACK_PC 0x819215u        // `$12` set to the loop's state
#define STAGGER_BANK 0x0081
#define STAGGER_HANDLER 0x96e4u
#define STAGGER_SPIN_PICTURES 0x8196f1u  // a picture for each way
#define STAGGER_SPIN_TICKS 2
#define STAGGER_SPINS 8
#define STAGGER_DAZES 6                  // more times the second list plays
#define STAGGER_SHOWN_LIST 0x9701u
#define STAGGER_DAZE_LIST 0x9713u
#define STAGGER_LAST_LIST 0x971du
#define STAGGER_RESUME 0x96acu           // `$12` while the lists play
#define STAGGER_FLAG_CLEARED 0x0002u
#define STAGGER_COLLIDE_DAZED 0x0002
#define STAGGER_PICTURE_BANK_DAZED 0x008f
#define STAGGER_OWN_PICTURE 0xc73bu
#define STAGGER_OWN_PICTURE_BANK 0x0090
#define STAGGER_COLLIDE_OWN 0x0003
#define STAGGER_OWN_HANDLER 0x990bu
#define STAGGER_DP_RECORD 0x08
#define STAGGER_DP_WAY 0x0c      // the spin's: a way, doubled
#define STAGGER_DP_FACING 0x0e   // the way it faced, from 2
#define STAGGER_DP_STATE 0x12
#define STAGGER_DP_SPINS 0x36
#define STAGGER_DP_FLAGS 0x3c    // its record's flags, kept
#define STAGGER_DP_4A 0x4a       // set to $0F, before and after; I have not
#define STAGGER_DP_4A_VALUE 0x000f  // read what uses it
#define STAGGER_DP_DAZES 0x7e
#define BYSTANDER_HANDLER 0xdeebu
#define BYSTANDER_HANDLER_BANK 0x0082
#define BYSTANDER_PICTURES 0xe611u       // a long pointer by the kind
#define BYSTANDER_PICTURE_BANK 0x008f
#define BYSTANDER_FLAGS 0x8008u
#define BYSTANDER_BOX_LEFT 0x0014
#define BYSTANDER_BOX_WIDTH 0x0028
#define BYSTANDER_BOX_ABOVE 0x000a
#define BYSTANDER_BOX_HEIGHT 0x0041
#define BYSTANDER_WORDS 0x82e14fu        // what each kind says, by the kind
#define BYSTANDER_WORDS_BANK 0x0082
#define BYSTANDER_DP_X 0x00              // given: the spot
#define BYSTANDER_DP_Y 0x02
#define BYSTANDER_DP_KIND 0x04
#define BYSTANDER_DP_PICTURE 0x14        // the pointer the kind names
#define W_BYSTANDER_SHRINKS 0x6da6u      // of bank $7E: down by one...
#define W_BYSTANDER_GROWS 0x6da9u        // ...and up by two, a frame
#define BYSTANDER_SHRUNK 0x0008

// --- a frozen creature -------------------------------------------------------

#define FREEZE_BEGIN_PC 0x8184d6u
#define FREEZE_PASS_PC 0x8184f2u       // where its sleep comes back
#define FREEZE_YIELD_PC 0x8184eeu      // `JSL thread_yield`, ten ticks in A
#define FREEZE_OVER_PC 0x818505u       // the `RTL` back to where it was
#define FREEZE_TICKS 0x000a
#define FREEZE_COLLIDE_ID 0x0036
#define FREEZE_DP_RECORD 0x08
#define FREEZE_DP_HITS 0x7e            // `ACTOR_DP_FREEZE_HITS`

// --- the chainsaw maniac's cut -----------------------------------------------

#define CUT_PC 0x8192d6u
#define CUT_RETURN 0x92d8u              // the first `JSR`'s
#define CUT_HEARD_PC 0x8193d3u          // `LDA $1F52`, which is watched
#define CUT_SOUND_PC 0x8193deu          // `JSL apu_play_sfx`
#define CUT_SOUND 0x0029
#define CUT_AIM_PC 0x8193e2u
#define CUT_AIMS 0x81945du              // a routine by the side
#define CUT_AIM_RETURN 0x93e8u
#define CUT_ACROSS_PC 0x819322u         // `JSL rng`, and back for another
#define CUT_DOWN_PC 0x81935eu
#define CUT_ACROSS_DRAWN_PC 0x819326u
#define CUT_DOWN_DRAWN_PC 0x819362u
#define CUT_ACROSS_SLEEP_PC 0x819343u   // `JSL thread_yield`, a tick in A
#define CUT_DOWN_SLEEP_PC 0x81937fu
#define CUT_ACROSS_WOKE_PC 0x819347u
#define CUT_DOWN_WOKE_PC 0x819383u
#define CUT_ACROSS_ASK_PC 0x81934du     // `JSL actor_at_point`
#define CUT_DOWN_ASK_PC 0x819389u
#define CUT_ACROSS_PLACED_PC 0x819351u
#define CUT_DOWN_PLACED_PC 0x81938du
#define CUT_MAKE_PC 0x81939au           // `JSL actor_slot_alloc`
#define CUT_MAKE_RETURN 0x9412u
#define CUT_MADE_PC 0x81939eu
#define CUT_SLEEP_PC 0x81944au          // `JSL thread_yield`, eight ticks
#define CUT_SHOWN_PC 0x81944eu
#define CUT_LISTED_PC 0x81945cu         // the `RTS` back to the cut
#define CUT_SWAP_PC 0x8192d9u
#define CUT_SWAP_PUT_PC 0x819308u       // `JSL $80:AB5A`, the block in A
#define CUT_TILED_PC 0x81930cu
#define CUT_SECOND_RETURN 0x9312u
#define CUT_SECOND_SLEEP_PC 0x8194d6u   // `JSL thread_yield`, five ticks
#define CUT_SHOWN_AGAIN_PC 0x8194dau
#define CUT_FREE_PC 0x8194eau           // `JSL actor_slot_free`
#define CUT_FREED_PC 0x8194eeu
#define CUT_HANDLER_CALL_PC 0x819319u   // `JSL thread_set_handler`
#define CUT_DONE_PC 0x81931du
#define CUT_SETTER_PC 0x819250u         // `setter_819250`
#define CUT_ACROSS_INTO 0x001c          // into the hedge's square
#define CUT_DOWN_INTO 0x0020
#define CUT_SQUARE 0x0040
#define CUT_FIRST_LISTS 0x81946fu       // by the side times four: a list,
                                        // and how far below the chips go
#define CUT_SECOND_LISTS 0x8194f4u      // by the side doubled
#define CUT_FIRST_TICKS 0x0008
#define CUT_SECOND_TICKS 0x0005
#define CUT_CHIPS 0xb944u
#define CUT_CHIPS_BANK 0x0090
#define CUT_SIDE_TURNED 7               // its chips are drawn the other way
#define CUT_TURNED 0x0002u              // of `ACTOR_FLIP`
#define CUT_HANDLER 0x990bu
#define CUT_HANDLER_BANK 0x0081
#define W_HEDGES_CUT 0x1fc2u
#define CUT_DP_SPREAD 0x3c
#define CUT_DP_LIST 0x48
#define CUT_DP_CHIPS_X 0x00             // where the chips go
#define CUT_DP_CHIPS_Y 0x02
#define CUT_DP_CHIPS_Z 0x04

// --- a player's death --------------------------------------------------------

#define DEATH_SOUNDED_PC 0x80ce81u      // where the sound comes back
#define DEATH_ENDS_RETURN 0xce8au       // the `PEA` before a state's end
// What `$72` can hold: the routines that end a state, by the stores to it.
#define DEATH_ENDS_COUNT 6
extern const uint16_t DEATH_ENDS[DEATH_ENDS_COUNT];
#define DEATH_HANDS_PC 0x80ce8bu        // `JSR $ECFD`: the ROM's
#define DEATH_SHOWN_PC 0x80ce94u
#define DEATH_SLEEP_PC 0x80cea6u        // `JSL thread_yield`, thirty ticks
#define DEATH_PLAY_PC 0x80cebfu         // `JSL pictures_play`, the list in A
#define DEATH_STILL_PC 0x80ceaau
#define DEATH_COUNTED_PC 0x80cec3u
#define DEATH_BACK_PC 0x80ced4u         // the `RTS` to the thread's loop
#define DEATH_NONE_LEFT_PC 0x80ced5u    // `JSR $F354`: the ROM's
#define DEATH_AGAIN_RETURN 0xced3u      // `JSR $D95F`'s
#define DEATH_MONSTER_PICTURES 0xfd72u  // `$10` while they are the monster
#define DEATH_MONSTER_PICTURE 0x9b89u
#define DEATH_MONSTER_TICKS 0x001e
#define DEATH_LISTS 0x80cf63u           // by which of the two
#define DEATH_PICTURES_AFTER 0x80cf5bu  // ...and the monster's after
#define DEATH_HEALTH 0x000a
#define DEATH_FLASHING_FRAMES 0x0078
#define DEATH_COLLIDE_ID 0x0038         // nothing can touch them
#define W_PLAYER_LIVES 0x1d4cu          // a word a player
#define DEATH_DP_RECORD 0x08
#define DEATH_DP_WHO 0x0c               // which of the two, doubled
#define DEATH_DP_PLAYER 0x0e            // which player, doubled
#define DEATH_DP_PICTURES 0x10
#define DEATH_DP_PICTURE_KEPT 0x2e
#define DEATH_DP_STATE 0x70
#define DEATH_DP_ENDS_BY 0x72           // the routine that ends the state
#define DEATH_DP_FRAMES_LEFT 0x74
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
#define MARTIAN_ARRIVAL_ENTER_PC 0x819a3eu
#define MARTIAN_PLACE_RETURN 0x9a40u           // its `JSR $9AA2`
#define MARTIAN_PLACE_GROUND_PC 0x819aaeu      // `JSL terrain_blocked`
#define MARTIAN_PLACE_GROUNDED_PC 0x819ab2u
#define MARTIAN_PLACE_EDGE_PC 0x819ab8u        // `JSL terrain_out_of_bounds`
#define MARTIAN_PLACED_PC 0x819abcu
#define MARTIAN_ARRIVAL_NONE_PC 0x819a8du      // the `RTL` with no martian
#define MARTIAN_ARRIVAL_BEGIN_CALL_PC 0x819a4du
#define MARTIAN_ARRIVAL_BEGUN_PC 0x819a50u
#define MARTIAN_ARRIVAL_BELOW_TOP 0x0008

// --- the werewolf ------------------------------------------------------------

#define WEREWOLF_ENTER_PC 0x81abf5u
#define WEREWOLF_NONE_PC 0x81abfau          // the `RTL` with none due
#define WEREWOLF_HOWL_CHECK_PC 0x81ac05u    // `LDA $1F52`, which is watched
#define WEREWOLF_SAMPLE_SET 0x0001
#define WEREWOLF_HOWL_CALL_PC 0x81ac10u     // `JSL apu_play_sfx`
#define WEREWOLF_HOWLED_PC 0x81ac14u
#define WEREWOLF_MAKE_PC 0x81a643u          // `JSL record_begin`
#define WEREWOLF_MAKE_RETURN 0xac16u        // what the `JSR` to it pushed
#define WEREWOLF_MADE_PC 0x81a647u
#define WEREWOLF_FIRST_SLEEP_PC 0x81a686u
#define WEREWOLF_WOKE_PC 0x81a68au
#define WEREWOLF_HANDLER_CALL_PC 0x81abbcu  // `JSL thread_set_handler`
#define WEREWOLF_RUNS_PC 0x81abc0u
#define WEREWOLF_STRUCK_PC 0x81ab02u
#define WEREWOLF_RESTED_PC 0x81aa9bu
#define WEREWOLF_CROUCHED_PC 0x81a9afu
#define WEREWOLF_LOAD 0x001c
#define WEREWOLF_HOWL 0x002b
#define WEREWOLF_FIRST_PICTURE 0xe9d8u
#define WEREWOLF_LEAP_PICTURE 0xeb89u
#define WEREWOLF_START_ATTR 0x0c00
#define WEREWOLF_COLLIDE_ID 0x0003
#define WEREWOLF_FIRST_SLEEP 0x0014
#define WEREWOLF_HEALTH 0x000a
#define WEREWOLF_HANDLER 0xac92u
// In the air it is drawn over everything, and is nothing to hit.
#define WEREWOLF_LEAP_FLAGS 0x8008u
#define WEREWOLF_DP_PLACED_X 0x00
#define WEREWOLF_DP_PLACED_Y 0x02
#define WEREWOLF_DP_7E 0x7e                 // cleared; I have not read its use

// --- one instruction or two --------------------------------------------------

#define PAGE_CLEAR_PC 0x80d0ddu       // `STZ $1C`, and an `RTS`
#define PAGE_CLEAR_RTS_PC 0x80d0dfu
#define PAGE_CLEARED 0x1c
#define PAGE_PULL_PC 0x80abd1u        // `PLD`, and an `RTL`
#define PAGE_PULL_RTL_PC 0x80abd2u
#define CARRY_SET_PC 0x80e78eu        // `SEC`, and an `RTS`
#define CARRY_SET_RTS_PC 0x80e78fu
#define VICTIM_GONE_PC 0x8181efu      // `LDA $06`, and `JSL victim_mark_gone`
#define VICTIM_GONE_CALL_PC 0x8181f1u
#define VICTIM_DP_GONE 0x06

// --- a player's flashing ended -----------------------------------------------

#define FLASHING_END_PC 0x80d97fu
#define FLASHING_END_CALL_PC 0x80d99du  // `JSR $F377`

// --- a player shown hurt -----------------------------------------------------

#define PLAYER_HIT_SHOW_PC 0x80d056u
#define PLAYER_HIT_SHOW_CALL_PC 0x80d066u   // `JSR $F36C`
#define PLAYER_HIT_SHOWN_PC 0x80d070u
#define PLAYER_HIT_SHOWN_CALL_PC 0x80d07eu  // `JSR $F382`
#define PLAYER_DP_RECORD 0x08
#define PLAYER_DP_16 0x16                   // cleared after; I have not read
#define PLAYER_DP_50 0x50                   // the use of either
#define PLAYER_HURT_PICTURE_BANK 0x0090

// --- a locked door's buzz ---------------------------------------------------

#define DOOR_BUZZ_PC 0x82defbu
#define DOOR_BUZZ_SOUND_PC 0x82df06u  // `JSL apu_play_sfx`, sound in A
#define DOOR_BUZZED_PC 0x82df1fu
#define DOOR_END_PC 0x82df22u         // the thread's `RTL`
#define DOOR_BUZZ_SFX 0x0013
#define W_DOOR_BUZZING 0x1ff4u        // in `$7E`: one is buzzing

// --- a slime's start ---------------------------------------------------------

#define SLIME_ENTER_PC 0x81cccfu
#define SLIME_BEGIN_CALL_PC 0x81ccd9u   // `JSR slime_begin`
#define SLIME_SHOWN_PC 0x81ccacu        // where its first pictures come back
#define SLIME_BEGIN_RETURN 0xccdbu      // `slime_begin`'s return, less one
#define SLIME_WAY_CALL_PC 0x81ccdcu     // `JSR $C9E4`
#define SLIME_WAY_RETURN 0xccdeu
#define SLIME_DRAW_PC 0x81c9e4u         // `JSL rng`
#define SLIME_WAY_PC 0x81c9e8u
#define SLIME_FIRST_YIELD_PC 0x81cce4u
#define SLIME_LOAD 0x0020
#define SLIME_HANDLER 0xcddeu
#define SLIME_HANDLER_BANK 0x0081
#define SLIME_STATE_FIRST 0xc9fau       // the loop's state after the way
#define SLIME_YIELD_TICKS 4
#define SLIME_DP_WAY 0x1a               // a way of four, as 2, 6, 10 or 14
#define SLIME_DP_STATE 0x16
#define SLIME_DP_22 0x22

// --- the seeker's states that sleep ------------------------------------------

#define SEEKER_LAND_SFX_CALL_PC 0x82ea40u       // `JSL apu_play_sfx`
#define SEEKER_LAND_SOUNDED_PC 0x82ea44u
#define SEEKER_LAND_PICTURES_CALL_PC 0x82ea47u  // `JSL pictures_resume`
#define SEEKER_LANDED_PC 0x82ea4bu
#define SEEKER_LAND_DRAW_PC 0x82ea5au           // `JSL rng`
#define SEEKER_LAND_WAY_PC 0x82ea5eu
#define SEEKER_LAND_SOUNDS 0x0003               // the sample set with its sound
#define SEEKER_LAND_SFX 0x0027
#define SEEKER_LAND_PICTURES 0xea66u
#define SEEKER_LANDED_HEALTH 0x0009
#define SEEKER_FIRED_PC 0x82ee1bu
#define SEEKER_SPAWN_CALL_PC 0x82ee3bu          // `JSL thread_spawn`
#define SEEKER_SPAWNED_PC 0x82ee3fu
#define SEEKER_SPAWN_SLEEP_PC 0x82ee46u         // `JSL thread_yield`
#define SEEKER_SPAWN_AGAIN_PC 0x82ee4au
#define SEEKER_FIRED_CALL_PC 0x82ee5eu          // `JSL pictures_resume`
#define SEEKER_NEXT_PC 0x82ee62u
#define SEEKER_TRACKERS_FROM 0x0036  // less its strength, by 32, and one more
#define SEEKER_TRACKER_THREAD 0xf03eu
#define SEEKER_SPAWN_TICKS 0x0003
#define SEEKER_FIRED_PICTURES 0x82eeafu         // a list for each way
#define SEEKER_HIDDEN_PC 0x82eba3u
#define SEEKER_BACK_PC 0x82ec61u
#define SEEKER_COLLIDE_ID 0x0003

// --- a tracker's start and end, and the seeker's puff ------------------------

#define TRACKER_ENTER_PC 0x82f03eu
#define TRACKER_BEGIN_CALL_PC 0x82f048u  // `JSR tracker_begin`
#define TRACKER_BEGUN_PC 0x82f04bu
#define TRACKER_END_CALL_PC 0x82f069u    // `JSL pictures_resume`
#define TRACKER_LOAD 0x0008
#define TRACKER_END_PICTURE_BANK 0x008f
#define TRACKER_END_PICTURES 0xf07fu
#define PUFF_ENTER_PC 0x82f1e6u
#define PUFF_ALLOC_CALL_PC 0x82f1f0u     // `JSL actor_slot_alloc`
#define PUFF_MADE_PC 0x82f1f4u
#define PUFF_PICTURES_CALL_PC 0x82f22bu  // `JSL pictures_resume`
#define PUFF_LOAD 0x0005
#define PUFF_PICTURE 0xa01du
#define PUFF_PICTURE_BANK 0x0090
#define PUFF_PICTURES 0xf241u
#define PUFF_DP_RECORD 0x08
#define PUFF_DP_X 0x00                   // where it was asked for
#define PUFF_DP_Y 0x02

// --- the big figure's start, and the saucer's --------------------------------

#define FIGURE_ENTER_PC 0x829569u
#define FIGURE_START_RETURN 0x9575u       // what `JSR $94E4` leaves
#define FIGURE_COLOURS_CALL_PC 0x8294eau  // `JSL figure_colours_set`
#define FIGURE_COLOURED_PC 0x8294eeu
#define FIGURE_QUEUE_CALL_PC 0x829506u    // `JSL boss_bg_queue_one`
#define FIGURE_YIELD_PC 0x82950du         // `JSL thread_yield`, A already 1
#define FIGURE_WAITED_PC 0x829511u
#define FIGURE_PART_CALL_PC 0x8294b4u     // `JSL actor_slot_alloc`
#define FIGURE_PART_PC 0x8294b8u
#define FIGURE_CHOOSE_RETURN 0x9578u      // what `JSR $89D2` leaves
#define FIGURE_LOAD 0x0028
#define FIGURE_COLOURS 0xffd1u
#define FIGURE_COLOURS_BANK 0x0092
#define FIGURE_TILES 0x8000u
#define FIGURE_TILES_BANK 0x0093
#define FIGURE_TILES_BYTES 0x2800
#define FIGURE_TILES_VRAM 0x5000
#define FIGURE_PLANE_AT_X 0x003c
#define FIGURE_PLANE_AT_Y 0x0082
#define FIGURE_PARTS 4
#define FIGURE_PART_COLLIDE_ID 0x0003
#define FIGURE_PART_FLAGS 0x8008u
#define FIGURE_HANDLER 0x9660u
#define FIGURE_HANDLER_BANK 0x0082
#define FIGURE_DP_PLACE_X 0x00   // given: where it stands
#define FIGURE_DP_PLACE_Y 0x02
#define FIGURE_DP_3C 0x3c        // set to seventy; what reads it I have not found
#define FIGURE_3C 0x0046
// Where each part's `JSR $94B4` comes back to, less one.
extern const uint16_t FIGURE_PART_RETURNS[FIGURE_PARTS];

#define SAUCER_ENTER_PC 0x82873cu
#define SAUCER_COLOURS_CALL_PC 0x82874cu  // `JSL figure_colours_set`
#define SAUCER_COLOURED_PC 0x828750u
#define SAUCER_DRAW_CALL_PC 0x828766u     // `JSL boss_bg_queue`
#define SAUCER_DRAWN_PC 0x82876au
#define SAUCER_QUEUE_CALL_PC 0x828782u    // `JSL boss_bg_queue_one`
#define SAUCER_QUEUED_PC 0x828786u
#define SAUCER_NEAREST_CALL_PC 0x8284d9u  // `JSL actor_nearest`
#define SAUCER_AIMED_PC 0x8284ddu
#define SAUCER_START_RETURN 0x87a9u       // what `JSR $84CE` leaves
#define SAUCER_LOAD 0x0018
#define SAUCER_FIGURE 0xf961u             // for `boss_bg_queue`
#define SAUCER_FIGURE_BANK 0x0097
#define SAUCER_TILES 0xa620u
#define SAUCER_DP_PLACE_X 0x00            // given: where it is
#define SAUCER_DP_PLACE_Y 0x02
#define SAUCER_DP_12 0x12                 // cleared; I have not followed it

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
  X(TS_MARTIAN_WALKS, 0x819d2a, 0x819d30, 0)  /* LDA # : STA $0A : RTS */ \
  X(TS_RTS, 0x819abf, 0x819ac0, 0) \
  X(TS_ARRIVAL_PLACE, 0x819aa2, 0x819aae, 0) \
  X(TS_ARRIVAL_FREE, 0x819abe, 0x819ac0, 0)   /* CLC : RTS */ \
  X(TS_ARRIVAL_STATE, 0x819db1, 0x819dbb, 0) \
  X(TS_PLAYER_HIT_KEEP, 0x80d056, 0x80d066, 0) \
  X(TS_PLAYER_HIT_BACK, 0x80d070, 0x80d07e, 0) \
  X(TS_FLASHING_END, 0x80d97f, 0x80d998, 0)  /* to the BNE */ \
  X(TS_WOLF_ASK, 0x81abf5, 0x81abfa, 0)      /* LDA abs : BNE */ \
  X(TS_WOLF_HEARD, 0x81ac05, 0x81ac0d, 0)    /* LDA abs : CMP # : BNE */ \
  X(TS_WOLF_MADE, 0x81a647, 0x81a686, 0) \
  X(TS_WOLF_HEALTH, 0x81a68a, 0x81a694, 0)   /* the JMP too */ \
  X(TS_WOLF_UNBLOW, 0x81ab02, 0x81ab0a, 0)   /* the JMP too */ \
  X(TS_WOLF_LEAP, 0x81a9d5, 0x81a9f0, 0)     /* the RTS too */ \
  X(TS_JMP, 0x81a9af, 0x81a9b2, 0) \
  X(TS_STZ_DP, 0x80d0dd, 0x80d0df, 0) \
  X(TS_PLD, 0x80abd1, 0x80abd2, 0) \
  X(TS_SEC, 0x80e78e, 0x80e78f, 0) \
  X(TS_SQUARE_BAND, 0x82f6ba, 0x82f6ea, 0)   /* the RTS too */ \
  X(TS_SQUARE_HANDLER, 0x82f6ab, 0x82f6b0, 0)  /* the RTS too */ \
  X(TS_SQUARE_CALL, 0x82f6f8, 0x82f6ff, 0)   /* PEA : LDA : DEC : PHA : RTS */ \
  X(TS_SQUARE_WHO, 0x82f593, 0x82f597, 0)    /* LDY abs : BEQ */ \
  X(TS_SQUARE_ROW, 0x82f598, 0x82f59e, 0)    /* LDA abs,Y : CMP dp : BCS */ \
  X(TS_SQUARE_BRA, 0x82f5ab, 0x82f5ac, 0) \
  X(TS_SQUARE_HELD, 0x82f5c7, 0x82f5c8, 0)   /* CLC : RTS */ \
  X(TS_SQUARE_LEFT, 0x82f5c9, 0x82f5ca, 0)   /* SEC : RTS */ \
  X(TS_SQUARE_DONE, 0x82f700, 0x82f703, 0)   /* LDA $0A : BEQ */ \
  X(TS_FLYOFF_DRESS, 0x82e0bf, 0x82e0f9, 0)  /* to the TAY */ \
  X(TS_FLYOFF_WAY, 0x82e102, 0x82e10d, 0) \
  X(TS_FLYOFF_STEP, 0x82e115, 0x82e130, 0)   /* to the BPL */ \
  X(TS_FLYOFF_END, 0x82e131, 0x82e132, 0)    /* LDA $08 */ \
  X(TS_BYSTANDER_DRESS, 0x82de87, 0x82dee3, 0)  /* to the LDY # */ \
  X(TS_BYSTANDER_SET, 0x82dee8, 0x82deea, 0)   /* STZ $12 : RTS */ \
  X(TS_BYSTANDER_STEP, 0x82de08, 0x82de1f, 0)  /* to the BNE */ \
  X(TS_BYSTANDER_SAY, 0x82de20, 0x82de2a, 0) \
  X(TS_LEAP_SPLASH, 0x80dd79, 0x80dd8c, 0) \
  X(TS_SET_HANDLER, 0x808475, 0x808480, 0)   /* thread_set_handler */ \
  X(TS_STAGGER_HANDLER, 0x819643, 0x81964d, 0) \
  X(TS_STAGGER_BEGIN, 0x81964d, 0x81966f, 0) \
  X(TS_STAGGER_PICTURE, 0x81966f, 0x81967c, 0) \
  X(TS_STAGGER_TURN, 0x819680, 0x81968d, 0)  /* to the BNE */ \
  X(TS_STAGGER_SHOWN, 0x81968d, 0x8196a3, 0) \
  X(TS_STAGGER_DAZED, 0x8196a7, 0x8196af, 0) \
  X(TS_STAGGER_DAZE, 0x8196b3, 0x8196b7, 0)  /* DEC $7E : BPL */ \
  X(TS_STAGGER_LAST, 0x8196b7, 0x8196ba, 0) \
  X(TS_STAGGER_OVER, 0x8196be, 0x8196e1, 0) \
  X(TS_STAGGER_BACK, 0x8196e1, 0x8196e4, 0)  /* JMP $9215 */ \
  X(TS_FREEZE_BEGIN, 0x8184d6, 0x8184ea, 0) \
  X(TS_FREEZE_COUNT, 0x8184f2, 0x8184f5, 0)  /* DEC $7E : BPL */ \
  X(TS_FREEZE_END, 0x8184f6, 0x818504, 0)    /* to the RTL */ \
  X(TS_CUT_HEARD, 0x8193d3, 0x8193da, 0)     /* LDA abs : CMP # : BNE */ \
  X(TS_CUT_AIM, 0x8193e2, 0x8193e8, 0)       /* to the JSR (abs,X) */ \
  X(TS_CUT_DRAWN, 0x819326, 0x81933f, 0)     /* and $81:9362-$937B */ \
  X(TS_CUT_WOKE, 0x819347, 0x81934c, 0) \
  X(TS_CUT_KEEP, 0x819355, 0x81935d, 0)      /* the RTS too */ \
  X(TS_CUT_PUT, 0x8193e9, 0x81940f, 0) \
  X(TS_CUT_MADE, 0x81939e, 0x8193d2, 0)      /* the RTS too */ \
  X(TS_CUT_MARKED, 0x819413, 0x819426, 0)    /* to the BNE */ \
  X(TS_CUT_SEVEN, 0x819427, 0x81942c, 0) \
  X(TS_CUT_SHOWN, 0x81942d, 0x81943a, 0) \
  X(TS_CUT_NEXT, 0x81943b, 0x81943e, 0)      /* LDA (dp),Y : BEQ */ \
  X(TS_CUT_PICTURE, 0x81943f, 0x819449, 0) \
  X(TS_CUT_LISTED, 0x819451, 0x81945b, 0) \
  X(TS_CUT_AGAIN, 0x81944e, 0x81944f, 0)     /* PLY : BRA */ \
  X(TS_CUT_TILED, 0x81930c, 0x819312, 0)     /* INC abs : PLD : JSR */ \
  X(TS_CUT_SECOND, 0x8194bb, 0x8194c6, 0) \
  X(TS_CUT_UNMARK, 0x8194dd, 0x8194e9, 0) \
  X(TS_CUT_FREED, 0x8194ee, 0x8194f3, 0)     /* the RTS too */ \
  X(TS_CUT_HANDLER, 0x819313, 0x819318, 0) \
  X(TS_CUT_DONE, 0x81931d, 0x819321, 0)      /* STZ dp : JMP */ \
  X(TS_DEATH_FLASHING, 0x80ce81, 0x80ce84, 0)  /* LDA $72 : BEQ */ \
  X(TS_DEATH_ENDS, 0x80ce85, 0x80ce8a, 0)    /* PEA : DEC : PHA : RTS */ \
  X(TS_DEATH_WHICH, 0x80ce94, 0x80ce9a, 0)   /* LDA $10 : CMP # : BNE */ \
  X(TS_DEATH_MONSTER, 0x80ce9b, 0x80cea5, 0) \
  X(TS_DEATH_LIST, 0x80ceb3, 0x80cebe, 0) \
  X(TS_DEATH_STILL, 0x80ceaa, 0x80ceb2, 0)   /* the BRA too */ \
  X(TS_DEATH_LIFE, 0x80cec3, 0x80cec9, 0)    /* LDX : DEC abs,X : BMI */ \
  X(TS_DEATH_LIFE_KEPT, 0x80cec3, 0x80cec9, 0)  /* ...with three NOPs */ \
  X(TS_DEATH_SHOW, 0x80ceca, 0x80ced0, 0) \
  X(TS_DEATH_AGAIN, 0x80d95f, 0x80d97e, 0)   /* the RTS too */ \
  X(TS_DOOR_ASK, 0x82defb, 0x82df00, 0)      /* LDA abs : BNE */ \
  X(TS_DOOR_BUZZ, 0x82df00, 0x82df06, 0) \
  X(TS_DOOR_DONE, 0x82df1f, 0x82df22, 0) \
  X(TS_SLIME_ENTER, 0x81cccf, 0x81ccd9, 0) \
  X(TS_SLIME_HANDLER, 0x81ccac, 0x81ccb6, 0)  /* the JSL too */ \
  X(TS_SLIME_WAY, 0x81c9e8, 0x81c9f9, 0)     /* the JMP too */ \
  X(TS_SLIME_FIRST, 0x81ccdf, 0x81cce4, 0) \
  X(TS_SEEKER_TAIL, 0x82ef57, 0x82ef5b, 0)    /* LDA $2E : BEQ */ \
  X(TS_SEEKER_SLEEP, 0x82ef49, 0x82ef4b, 0)   /* LDA $2C */ \
  X(TS_SEEKER_SET, 0x82eb5e, 0x82eb64, 0)     /* and $82:EB74-$EB79 */ \
  X(TS_SEEKER_SET_HOLD, 0x82ec92, 0x82eca3, 0) \
  X(TS_SEEKER_LAND_ASK, 0x82ea35, 0x82ea3d, 0)  /* LDA abs : CMP # : BNE */ \
  X(TS_SEEKER_LAND_SOUND, 0x82ea3d, 0x82ea40, 0) \
  X(TS_SEEKER_LAND_PICTURES, 0x82ea44, 0x82ea47, 0) \
  X(TS_SEEKER_LANDED, 0x82ea4b, 0x82ea5a, 0)  /* the JSL too */ \
  X(TS_SEEKER_LAND_WAY, 0x82ea5e, 0x82ea66, 0)  /* the JMP too */ \
  X(TS_SEEKER_GO_FIRE, 0x82edfd, 0x82ee0e, 0)   /* the RTS too */ \
  X(TS_SEEKER_FIRED, 0x82ee1b, 0x82ee29, 0) \
  X(TS_SEEKER_SPAWN, 0x82ee29, 0x82ee3b, 0) \
  X(TS_SEEKER_SPAWNED, 0x82ee3f, 0x82ee43, 0)   /* DEC dp : BEQ */ \
  X(TS_SEEKER_PAUSE, 0x82ee43, 0x82ee46, 0) \
  X(TS_SEEKER_AGAIN, 0x82ee4a, 0x82ee50, 0)     /* DEC dp, DEC dp : BNE */ \
  X(TS_SEEKER_WRAP, 0x82ee50, 0x82ee57, 1)      /* the BRA too */ \
  X(TS_SEEKER_LAST, 0x82ee57, 0x82ee5e, 0) \
  X(TS_SEEKER_NEXT, 0x82ee62, 0x82ee67, 0)      /* LDX dp : JMP (abs,X) */ \
  X(TS_SEEKER_HIDDEN, 0x82eba3, 0x82ebcf, 0)    /* the RTS too */ \
  X(TS_SEEKER_BACK, 0x82ec61, 0x82ec73, 0)      /* the JSL too */ \
  X(TS_SEEKER_BACK_ON, 0x82ec73, 0x82ec76, 0)   /* JMP $EB5E */ \
  X(TS_TRACKER_ENTER, 0x82f03e, 0x82f048, 0) \
  X(TS_TRACKER_BEGUN, 0x82f04b, 0x82f050, 0) \
  X(TS_TRACKER_ENDED, 0x82f05e, 0x82f069, 0) \
  X(TS_PUFF_ENTER, 0x82f1e6, 0x82f1f0, 0) \
  X(TS_PUFF_MADE, 0x82f1f4, 0x82f22b, 0) \
  X(TS_PULSE_ENTER, 0x80a0ef, 0x80a102, 0) \
  X(TS_FIGURE_ENTER, 0x829569, 0x829576, 0)     /* the JSR too */ \
  X(TS_FIGURE_COLOURS, 0x8294e4, 0x8294ea, 0) \
  X(TS_FIGURE_COLOURED, 0x8294ee, 0x829506, 0) \
  X(TS_FIGURE_WAIT_ASK, 0x829511, 0x829517, 0)  /* $82:80E0 too */ \
  X(TS_FIGURE_PLACED, 0x829517, 0x829534, 0)    /* the JSR too */ \
  X(TS_FIGURE_PART, 0x8294b8, 0x8294e4, 0)      /* the RTS too */ \
  X(TS_FIGURE_PART_KEPT, 0x829534, 0x829539, 0) /* and two like it */ \
  X(TS_FIGURE_PARTS_KEPT, 0x829543, 0x829554, 0) \
  X(TS_FIGURE_NO_SPITS, 0x8294a4, 0x8294b4, 0) \
  X(TS_FIGURE_HANDLER, 0x829554, 0x82955e, 0)   /* the JSL too */ \
  X(TS_FIGURE_CALM, 0x82955e, 0x829569, 0)      /* the RTS too */ \
  X(TS_SAUCER_ENTER, 0x82873c, 0x82874c, 0) \
  X(TS_SAUCER_COLOURED, 0x828750, 0x828766, 0) \
  X(TS_SAUCER_DRAWN, 0x82876a, 0x828782, 0) \
  X(TS_SAUCER_QUEUED, 0x828786, 0x8287aa, 0)    /* the JSR too */ \
  X(TS_SAUCER_NEAREST_ASK, 0x8284ce, 0x8284d9, 0) \
  X(TS_SAUCER_AIMED, 0x8284dd, 0x828505, 0)     /* the RTS too */

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
  TS_NEEDS_PLACE,   // the arriving martian's `JSR $9AA2` is what it returns to
  TS_NEEDS_WOLF,    // a werewolf's record, and its loop to return to
  TS_NEEDS_SQUARE,  // the band's handler, and the players' records in low WRAM
  TS_NEEDS_FLYOFF_RECORD,  // a record in Y, and a kind that is one
  TS_NEEDS_FLYOFF,  // the record on the page, and a diagonal
  TS_NEEDS_BYSTANDER_RECORD,  // a record in Y, and a kind that is one
  TS_NEEDS_BYSTANDER_SET,     // its setup's `JSR` to return to
  TS_NEEDS_BYSTANDER_KIND,    // a kind that is one
  TS_NEEDS_CUT,         // a side of one to eight, and the maniac's record
  TS_NEEDS_CUT_PLACED,  // ...and the aim's `JSR` to return to
  TS_NEEDS_CUT_MADE,    // ...and the chips' record in A, a list, and the
                        //    `JSR` that asked for them
  TS_NEEDS_CUT_LIST,    // the chips' record and a list
  TS_NEEDS_CUT_TILED,   // the maniac's page on the stack, with a side
  TS_NEEDS_CUT_FREED,   // the second list's `JSR` to return to
  TS_NEEDS_DEATH_ENDS,  // an end due that is one of the six, or none
  TS_NEEDS_DEATH,       // which player, which of the two, and the record
  TS_NEEDS_SLIME_BEGUN,  // `slime_begin`'s `JSR` to return to
  TS_NEEDS_SLIME_WAY,    // the start's `JSR $C9E4` to return to
  TS_NEEDS_SEEKER,       // the seeker's loop to return to, and its record
  TS_NEEDS_SEEKER_HIDDEN,  // ...and whoever it watched, in low WRAM
  TS_NEEDS_SEEKER_NEXT,    // ...and what it does after firing, one of three
  TS_NEEDS_FIGURE_PART,    // a record in A, and a part's `JSR` to return to
  TS_NEEDS_SAUCER_AIMED,   // a record in X, and the start's `JSR` to return to
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
  X(argument_player_hurt, "$80:D069", 0x80d069u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81ec72, "$81:EC72", 0x81ec72u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_81f30e, "$81:F30E", 0x81f30eu, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_flyoff, "$82:E0B4", 0x82e0b4u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(setter_819215, "$81:9215", 0x819215u, thread_setter, SETTER, TS_NEEDS_NOTHING) \
  X(setter_819250, "$81:9250", 0x819250u, thread_setter, SETTER, TS_NEEDS_NOTHING) \
  X(page_clear, "$80:D0DD", PAGE_CLEAR_PC, page_clear, SMALL, TS_NEEDS_NOTHING) \
  X(page_pull, "$80:ABD1", PAGE_PULL_PC, page_pull, SMALL, TS_NEEDS_NOTHING) \
  X(carry_set, "$80:E78E", CARRY_SET_PC, carry_set, SMALL, TS_NEEDS_NOTHING) \
  X(victim_gone, "$81:81EF", VICTIM_GONE_PC, victim_gone, SMALL, TS_NEEDS_NOTHING) \
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
  X(martian_begun, "$81:99EC", MARTIAN_BEGUN_PC, martian_begun, MARTIAN, TS_NEEDS_NOTHING) \
  X(martian_arrival_enter, "$81:9A3E", MARTIAN_ARRIVAL_ENTER_PC, martian_arrival_enter, MARTIAN, TS_NEEDS_NOTHING) \
  X(martian_arrival_grounded, "$81:9AB2", MARTIAN_PLACE_GROUNDED_PC, martian_arrival_grounded, MARTIAN, TS_NEEDS_PLACE) \
  X(martian_arrival_placed, "$81:9ABC", MARTIAN_PLACED_PC, martian_arrival_placed, MARTIAN, TS_NEEDS_PLACE) \
  X(martian_arrival_begun, "$81:9A50", MARTIAN_ARRIVAL_BEGUN_PC, martian_arrival_begun, MARTIAN, TS_NEEDS_NOTHING) \
  X(flashing_end, "$80:D97F", FLASHING_END_PC, flashing_end, PLAYER_HIT, TS_NEEDS_RECORD) \
  X(werewolf_enter, "$81:ABF5", WEREWOLF_ENTER_PC, werewolf_enter, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_howl_check, "$81:AC05", WEREWOLF_HOWL_CHECK_PC, werewolf_howl_check, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_howled, "$81:AC14", WEREWOLF_HOWLED_PC, werewolf_howled, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_made, "$81:A647", WEREWOLF_MADE_PC, werewolf_made, WOLF, TS_NEEDS_RECORD) \
  X(werewolf_woke, "$81:A68A", WEREWOLF_WOKE_PC, werewolf_woke, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_runs, "$81:ABC0", WEREWOLF_RUNS_PC, werewolf_runs, WOLF, TS_NEEDS_WOLF) \
  X(werewolf_struck, "$81:AB02", WEREWOLF_STRUCK_PC, werewolf_struck, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_rested, "$81:AA9B", WEREWOLF_RESTED_PC, werewolf_rested, WOLF, TS_NEEDS_NOTHING) \
  X(werewolf_crouched, "$81:A9AF", WEREWOLF_CROUCHED_PC, werewolf_crouched, WOLF, TS_NEEDS_WOLF) \
  X(player_hit_show, "$80:D056", PLAYER_HIT_SHOW_PC, player_hit_show, PLAYER_HIT, TS_NEEDS_RECORD) \
  X(player_hit_shown, "$80:D070", PLAYER_HIT_SHOWN_PC, player_hit_shown, PLAYER_HIT, TS_NEEDS_RECORD) \
  X(square_enter, "$82:F6EB", SQUARE_ENTER_PC, square_enter, SQUARE, TS_NEEDS_NOTHING) \
  X(square_pass, "$82:F6F8", SQUARE_PASS_PC, square_pass, SQUARE, TS_NEEDS_SQUARE) \
  X(flyoff_dress, "$82:E0BF", FLYOFF_DRESS_PC, flyoff_dress, FLYOFF, TS_NEEDS_FLYOFF_RECORD) \
  X(flyoff_way, "$82:E102", FLYOFF_WAY_PC, flyoff_way, FLYOFF, TS_NEEDS_NOTHING) \
  X(flyoff_pass, "$82:E115", FLYOFF_PASS_PC, flyoff_pass, FLYOFF, TS_NEEDS_FLYOFF) \
  X(bystander_enter, "$82:DD52", BYSTANDER_ENTER_PC, bystander_enter, BYSTANDER, TS_NEEDS_NOTHING) \
  X(bystander_dress, "$82:DE87", BYSTANDER_DRESS_PC, bystander_dress, BYSTANDER, TS_NEEDS_BYSTANDER_RECORD) \
  X(bystander_set, "$82:DEE8", BYSTANDER_SET_PC, bystander_set, BYSTANDER, TS_NEEDS_BYSTANDER_SET) \
  X(bystander_step, "$82:DE08", BYSTANDER_STEP_PC, bystander_step, BYSTANDER, TS_NEEDS_BYSTANDER_KIND) \
  X(leap_splash, "$80:DD79", LEAP_SPLASH_PC, leap_splash, LEAP, TS_NEEDS_RECORD) \
  X(stagger_begin, "$81:9643", STAGGER_BEGIN_PC, stagger_begin, STAGGER, TS_NEEDS_RECORD) \
  X(stagger_spin, "$81:9680", STAGGER_SPIN_PC, stagger_spin, STAGGER, TS_NEEDS_RECORD) \
  X(stagger_dazed, "$81:96A7", STAGGER_DAZED_PC, stagger_dazed, STAGGER, TS_NEEDS_NOTHING) \
  X(stagger_daze, "$81:96B3", STAGGER_DAZE_PC, stagger_daze, STAGGER, TS_NEEDS_NOTHING) \
  X(stagger_over, "$81:96BE", STAGGER_OVER_PC, stagger_over, STAGGER, TS_NEEDS_RECORD) \
  X(freeze_begin, "$81:84D6", FREEZE_BEGIN_PC, freeze_begin, FREEZE, TS_NEEDS_RECORD) \
  X(freeze_pass, "$81:84F2", FREEZE_PASS_PC, freeze_pass, FREEZE, TS_NEEDS_RECORD) \
  X(cut_enter, "$81:92D6", CUT_PC, cut_enter, CUT, TS_NEEDS_NOTHING) \
  X(cut_heard, "$81:93D3", CUT_HEARD_PC, cut_heard, CUT, TS_NEEDS_CUT) \
  X(cut_aim, "$81:93E2", CUT_AIM_PC, cut_aim, CUT, TS_NEEDS_CUT) \
  X(cut_drawn_across, "$81:9326", CUT_ACROSS_DRAWN_PC, cut_drawn, CUT, TS_NEEDS_CUT) \
  X(cut_drawn_down, "$81:9362", CUT_DOWN_DRAWN_PC, cut_drawn, CUT, TS_NEEDS_CUT) \
  X(cut_woke_across, "$81:9347", CUT_ACROSS_WOKE_PC, cut_woke, CUT, TS_NEEDS_CUT) \
  X(cut_woke_down, "$81:9383", CUT_DOWN_WOKE_PC, cut_woke, CUT, TS_NEEDS_CUT) \
  X(cut_placed_across, "$81:9351", CUT_ACROSS_PLACED_PC, cut_placed, CUT, TS_NEEDS_CUT_PLACED) \
  X(cut_placed_down, "$81:938D", CUT_DOWN_PLACED_PC, cut_placed, CUT, TS_NEEDS_CUT_PLACED) \
  X(cut_made, "$81:939E", CUT_MADE_PC, cut_made, CUT, TS_NEEDS_CUT_MADE) \
  X(cut_shown, "$81:944E", CUT_SHOWN_PC, cut_list_again, CUT, TS_NEEDS_CUT_LIST) \
  X(cut_tiled, "$81:930C", CUT_TILED_PC, cut_tiled, CUT, TS_NEEDS_CUT_TILED) \
  X(cut_shown_again, "$81:94DA", CUT_SHOWN_AGAIN_PC, cut_list_again, CUT, TS_NEEDS_CUT_LIST) \
  X(cut_freed, "$81:94EE", CUT_FREED_PC, cut_freed, CUT, TS_NEEDS_CUT_FREED) \
  X(cut_done, "$81:931D", CUT_DONE_PC, cut_done, CUT, TS_NEEDS_NOTHING) \
  X(death_sounded, "$80:CE81", DEATH_SOUNDED_PC, death_sounded, DEATH, TS_NEEDS_DEATH_ENDS) \
  X(death_shown, "$80:CE94", DEATH_SHOWN_PC, death_shown, DEATH, TS_NEEDS_DEATH) \
  X(death_still, "$80:CEAA", DEATH_STILL_PC, death_still, DEATH, TS_NEEDS_DEATH) \
  X(death_counted, "$80:CEC3", DEATH_COUNTED_PC, death_counted, DEATH, TS_NEEDS_DEATH) \
  X(argument_swipe, "$81:E8C1", 0x81e8c1u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_door_a, "$82:DF0A", 0x82df0au, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_door_b, "$82:DF11", 0x82df11u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_door_c, "$82:DF18", 0x82df18u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(door_buzz, "$82:DEFB", DOOR_BUZZ_PC, door_buzz, DOOR, TS_NEEDS_NOTHING) \
  X(door_buzzed, "$82:DF1F", DOOR_BUZZED_PC, door_buzzed, DOOR, TS_NEEDS_NOTHING) \
  X(slime_enter, "$81:CCCF", SLIME_ENTER_PC, slime_enter, SLIME, TS_NEEDS_NOTHING) \
  X(slime_shown, "$81:CCAC", SLIME_SHOWN_PC, slime_shown, SLIME, TS_NEEDS_SLIME_BEGUN) \
  X(slime_way, "$81:C9E8", SLIME_WAY_PC, slime_way, SLIME, TS_NEEDS_SLIME_WAY) \
  X(argument_seeker_back, "$82:EC5A", 0x82ec5au, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(seeker_land, "$82:EA35", SEEKER_LAND_PC, seeker_land, SEEKER, TS_NEEDS_NOTHING) \
  X(seeker_land_sounded, "$82:EA44", SEEKER_LAND_SOUNDED_PC, seeker_land_sounded, SEEKER, TS_NEEDS_NOTHING) \
  X(seeker_landed, "$82:EA4B", SEEKER_LANDED_PC, seeker_landed, SEEKER, TS_NEEDS_NOTHING) \
  X(seeker_land_way, "$82:EA5E", SEEKER_LAND_WAY_PC, seeker_land_way, SEEKER, TS_NEEDS_SEEKER) \
  X(seeker_fired, "$82:EE1B", SEEKER_FIRED_PC, seeker_fired, SEEKER, TS_NEEDS_NOTHING) \
  X(seeker_spawned, "$82:EE3F", SEEKER_SPAWNED_PC, seeker_spawned, SEEKER, TS_NEEDS_NOTHING) \
  X(seeker_spawn_again, "$82:EE4A", SEEKER_SPAWN_AGAIN_PC, seeker_spawn_again, SEEKER, TS_NEEDS_NOTHING) \
  X(seeker_next, "$82:EE62", SEEKER_NEXT_PC, seeker_next, SEEKER, TS_NEEDS_SEEKER_NEXT) \
  X(seeker_hidden, "$82:EBA3", SEEKER_HIDDEN_PC, seeker_hidden, SEEKER, TS_NEEDS_SEEKER_HIDDEN) \
  X(seeker_back, "$82:EC61", SEEKER_BACK_PC, seeker_back, SEEKER, TS_NEEDS_SEEKER) \
  X(tracker_enter, "$82:F03E", TRACKER_ENTER_PC, tracker_enter, TRACKER, TS_NEEDS_NOTHING) \
  X(tracker_begun, "$82:F04B", TRACKER_BEGUN_PC, tracker_begun, TRACKER, TS_NEEDS_NOTHING) \
  X(tracker_ended, "$82:F05E", TRACKER_ENDED_PC, tracker_ended, TRACKER, TS_NEEDS_RECORD) \
  X(puff_enter, "$82:F1E6", PUFF_ENTER_PC, puff_enter, TRACKER, TS_NEEDS_NOTHING) \
  X(puff_made, "$82:F1F4", PUFF_MADE_PC, puff_made, TRACKER, TS_NEEDS_A) \
  X(pulse_enter, "$80:A0EF", PALCYCLE_PULSE_ENTER_PC, pulse_enter, PULSE, TS_NEEDS_NOTHING) \
  X(argument_figure_queued, "$82:950A", 0x82950au, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(argument_82a871, "$82:A871", 0x82a871u, thread_argument, ARGUMENT, TS_NEEDS_NOTHING) \
  X(figure_enter, "$82:9569", FIGURE_ENTER_PC, figure_enter, FIGURE, TS_NEEDS_NOTHING) \
  X(figure_coloured, "$82:94EE", FIGURE_COLOURED_PC, figure_coloured, FIGURE, TS_NEEDS_NOTHING) \
  X(figure_waited, "$82:9511", FIGURE_WAITED_PC, figure_waited, FIGURE, TS_NEEDS_NOTHING) \
  X(figure_part, "$82:94B8", FIGURE_PART_PC, figure_part, FIGURE, TS_NEEDS_FIGURE_PART) \
  X(saucer_enter, "$82:873C", SAUCER_ENTER_PC, saucer_enter, SAUCER, TS_NEEDS_NOTHING) \
  X(saucer_coloured, "$82:8750", SAUCER_COLOURED_PC, saucer_coloured, SAUCER, TS_NEEDS_NOTHING) \
  X(saucer_drawn, "$82:876A", SAUCER_DRAWN_PC, saucer_drawn, SAUCER, TS_NEEDS_NOTHING) \
  X(saucer_queued, "$82:8786", SAUCER_QUEUED_PC, saucer_queued, SAUCER, TS_NEEDS_NOTHING) \
  X(saucer_aimed, "$82:84DD", SAUCER_AIMED_PC, saucer_aimed, SAUCER, TS_NEEDS_SAUCER_AIMED)

#define X(name, sym, entry, port, exits, needs) \
  void port(Wram* w, const Rom* rom, PortCpu* c, ThreadStretchWork* k);
// Several stretches share a port, so each is declared as often.
THREAD_STRETCHES(X)
#undef X

// `$81:92D9`, the cut's hedge, to the `JSL` that puts the block. False if
// its cell is not where a word can be read.
bool cut_swap_supported(const Wram* w, const Rom* rom, uint16_t page);
void cut_swap(Wram* w, const Rom* rom, PortCpu* c, BlockSwapWork* k);

#endif
