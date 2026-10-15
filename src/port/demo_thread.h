// The demonstration, in the front end's thread.
//
// When nobody joins on the players' screen, `$80:9179` calls `$80:9AB0`,
// which plays one of four recordings (`port/demo.h`) and comes back with
// carry set if a real pad ended it. Between its calls:
//
//   $80:9AB0  demo_begin     the random numbers held still, the demo's
//                            words cleared, and one of the four not played
//                            yet drawn for: after all four, all four again.
//                            Its level, which side plays it and where its
//                            recording is, and its level's record asked for
//   $80:9B14  demo_unpack    ...and that level's map unpacked to `$7E:8000`
//   $80:9B29  demo_unpacked  the word the unpacking was given pulled, and
//                            the screen faded out
//   $80:9B2D  demo_rest      sixteen ticks
//   $80:9B34  demo_scroll    the front end's scroll job taken off its queue
//   $80:9B4D  demo_song      the level's song asked for, from its record
//   $80:9B5E  demo_lives     no lives, and a frame waited
//   $80:9B62  demo_clear     the frame count and the random state from
//                            nothing, to the `STA $70:0000`, which is the
//                            ROM's: the port has no save memory
//   $80:9B7D  demo_job_ask   the job that plays the recording, asked for on
//                            the queue after the picture
//   $80:9B8B  demo_played    the level's loop come back: with carry set it
//                            ended, and the screen is faded out
//   $80:9BA0  demo_ended     a real pad ended it, or the top scores are shown
//   $80:9BBD  demo_over      the first level and the first side again, and
//                            five ticks
//   $80:9BCD  demo_return    back to the front end, which goes on to the
//                            title if a pad ended it and from its start if not
//
// The calls between them are links (`port/links.h`), and the wait for the
// screen to go dark is `port/hold.h`'s `level_faded_out`.
//
// ## Its contract with the ROM
//
// WRAM as the ROM writes it, the stack with it, and every register and flag
// at the instruction a stretch stops on. The tables are read through the data
// bank, `$00` or `$80`, which is the same cartridge at two speeds.
//
// Port code: libc only.

#ifndef PORT_DEMO_THREAD_H
#define PORT_DEMO_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

#define DEMO_COUNT 4
#define W_DEMOS_PLAYED 0x1372u    // a byte each, `$FF` once played
#define W_DEMOS_PLAYED_COUNT 0x137au
#define DEMO_LEVELS 0x809bcfu     // a word each
#define DEMO_SIDES 0x809bd7u      // ...which side plays it
#define DEMO_RECORDINGS 0x809bdfu // ...and four bytes: where its recording is
#define DEMO_RECORDS 0x9f0000u    // a level's record: its map,
#define DEMO_RECORD_SONG 0x32     // ...and its song, two words each
#define DEMO_UNPACK_TO 0x8000u    // in `$7E`
#define DEMO_REST_TICKS 0x0010u
#define DEMO_OVER_TICKS 0x0005u
#define DEMO_DP_RECORD 0x10       // the level's record, as `$80:885B` leaves it
#define DEMO_DP_SCROLL_JOB 0x6c   // where the front end's scroll job is
#define DEMO_JOB_BANK 0x0080u
#define DEMO_RETURN 0x917bu       // the front end's `JSR $9AB0`

#define DEMO_BEGIN_PC 0x809ab0u
#define DEMO_RECORD_ASK_PC 0x809b10u  // `JSL $80:885B`
#define DEMO_UNPACK_PC 0x809b14u
#define DEMO_UNPACK_CALL_PC 0x809b25u // `JSL lzss_decompress`
#define DEMO_UNPACKED_PC 0x809b29u
#define DEMO_FADE_OUT_CALL_PC 0x809b2au  // `JSR $9C72`
#define DEMO_REST_PC 0x809b2du
#define DEMO_REST_CALL_PC 0x809b30u
#define DEMO_SCROLL_PC 0x809b34u
#define DEMO_SCROLL_CALL_PC 0x809b36u // `JSL vbl_queue_a_remove`
#define DEMO_SONG_PC 0x809b4du
#define DEMO_SONG_CALL_PC 0x809b5au
#define DEMO_LIVES_PC 0x809b5eu
#define DEMO_LIVES_WAI_PC 0x809b61u
#define DEMO_CLEAR_PC 0x809b62u
#define DEMO_CLEAR_SAVE_PC 0x809b78u  // `STA $70:0000`
#define DEMO_JOB_ASK_PC 0x809b7du
#define DEMO_JOB_ASK_CALL_PC 0x809b83u
#define DEMO_PLAYED_PC 0x809b8bu
#define DEMO_PLAYED_ENDED_PC 0x809b8du  // `JSL $80:CC27`
#define DEMO_PLAYED_OVER_PC 0x809b9cu   // `JSL $80:866F`
#define DEMO_ENDED_PC 0x809ba0u
#define DEMO_SCORES_CALL_PC 0x809ba9u   // `JSL $82:BA11`
#define DEMO_ENDED_ON_PC 0x809badu
#define DEMO_OVER_PC 0x809bbdu
#define DEMO_OVER_CALL_PC 0x809bc9u
#define DEMO_RETURN_PC 0x809bcdu
#define DEMO_BACK_TITLE_PC 0x80917eu    // a pad ended it
#define DEMO_BACK_START_PC 0x809126u    // ...or it played out

enum {
  DM_HOLD,       // $9AB0-$9ABE: the numbers held, three words cleared
  DM_COUNT,      // LDA $137A : CMP #4 : BCC
  DM_RESET,      // three STZs
  DM_DRAW,       // JSL rng_next, less its body
  DM_TEST,       // AND : TAX : LDA,X : AND : BNE
  DM_CHOSEN,     // $9AE0-$9B0F, less the tables' bytes
  DM_TABLES,     // ...which are these, read through bank `$80`
  DM_TABLES_SLOW,// ...or the extra they cost through bank `$00`
  DM_UNPACK,     // $9B14-$9B24
  DM_PULL,       // PLA
  DM_LDA_IMM,    // LDA #
  DM_LDA_DP,     // LDA dp
  DM_SONG,       // $9B4D-$9B59
  DM_LIVES,      // STZ $1D4C
  DM_CLEAR,      // $9B62-$9B77
  DM_JOB_ASK,    // LDA # : LDY #
  DM_PLAYED,     // BCC
  DM_ENDED,      // LDA $1EB6 : CMP #1 : PHP : BCS
  DM_OVER,       // $9BBD-$9BC8
  DM_RETURN,     // PLP : RTS : BCC
  DM_TAKEN,
  DM_BLOCK_COUNT
};

typedef struct {
  uint16_t blocks[DM_BLOCK_COUNT];
  int draws_twice;  // of the draws, those whose overflow counted twice
} DemoThreadWork;

// The return is the front end's.
bool demo_return_supported(const Wram* w, const PortCpu* c);
// The level's record is in the cartridge.
bool demo_record_supported(const Wram* w, const PortCpu* c);

void demo_begin(Wram* w, const Rom* rom, PortCpu* c, DemoThreadWork* k);
void demo_unpack(Wram* w, const Rom* rom, PortCpu* c, DemoThreadWork* k);
void demo_unpacked(Wram* w, PortCpu* c, DemoThreadWork* k);
void demo_rest(PortCpu* c, DemoThreadWork* k);
void demo_scroll(Wram* w, PortCpu* c, DemoThreadWork* k);
void demo_song(Wram* w, const Rom* rom, PortCpu* c, DemoThreadWork* k);
void demo_lives(Wram* w, PortCpu* c, DemoThreadWork* k);
void demo_clear(Wram* w, PortCpu* c, DemoThreadWork* k);
void demo_job_ask(PortCpu* c, DemoThreadWork* k);
void demo_played(PortCpu* c, DemoThreadWork* k);
void demo_ended(Wram* w, PortCpu* c, DemoThreadWork* k);
void demo_over(Wram* w, PortCpu* c, DemoThreadWork* k);
void demo_return(Wram* w, PortCpu* c, DemoThreadWork* k);

#endif
