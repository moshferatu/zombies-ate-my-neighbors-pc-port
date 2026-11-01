// The cast after level 48: each kind of monster, and how many were killed.
//
// When level 48 is won the main thread calls `$80:8BBB` (`$80:84E8`) in
// place of the screen after a level. The screen is blanked, the sound and the
// threads and records are cleared, VRAM is cleared, and two sets of colours
// and pictures are sent: the big letters' (`$82:ACF0`) and the monsters'
// (`$82:AD1F`). The text map is cleared, the words' layer is set up, and the
// sprite cache is begun. Then, before it fades in:
//
//   $80:8D24  one record of its own, in the middle of the screen, 80 across
//             and 180 down: the monster being shown
//   $80:8D10  the first layer and the sprites shown (`$212C`), and the big
//             letters at `$80:8E5E` written across the top
//   $80:8CCD  five threads begun at `$82:DF6B`, the storm behind it (below)
//   $80:8CFF  the job at `$80:A2AB` asked for, which turns six sets of 16
//             colours over at `$20`, one every eleven frames
//
// **The roll call** (`$80:8C04`) is eighteen turns, one for each kind in the
// table at `$80:8E9A`: a far address of the pictures the record plays. Each
// turn the words of the last are cleared, and the record shows nothing for
// thirty frames. Then it shows the kind's pictures, waits a second, and with
// sound `$10` writes the words under it (`$80:8E76`). After twenty frames
// more the count is written beside them, from nought up to how many of the
// kind were killed, a word at `$80:8EE6` by the kind: zombies at `$1F64`, on
// to `$1F86`, the tentacles. More than 999 shows 999. Each step up is sound
// `$0E` and five frames, and the count is three digits at most with no
// noughts in front (`$80:8D60`), made by the console's divider. At the end
// of the count, sound 6 and the pictures at `$80:8F0A` by the kind, in the
// bank beside them if it is not nought; and two seconds.
//
// After the last it waits for a player to press Start, and nothing else, on
// either pad. Then it fades out, puts the colour maths back as a level has
// it, and returns. The main thread goes on to level 49.
//
// **The storm.** Each of the five threads takes a record and sleeps a
// random part of a second. Then, over and over: a place at random, 0 to 255
// across but not 80 to 159, and 0 to 139 down; one of eight palettes; a
// flash's pictures (`$82:E082`) and sound 6. Half the time the screen lights
// up as well (`$82:E01F`): the colour maths adds the fixed colour to
// everything, and a job at `$82:E076` sends a bright one for three frames
// with sound `$21`, then black. Then the record is cleared, and the thread
// sleeps a random number of frames, up to 255.
//
// **Written as blocks**, as `port/level_end.h` is. A stretch begins where a
// call or a sleep comes back to the screen, and runs block after block until
// the next call the harness makes, a sleep, a `WAI` or the `RTL`. Its own
// `JSR`s are made here, and an `RTS` goes where the stack says: to a block,
// or, if the stack holds an address the screen does not know, the stretch is
// turned down. The single `JSL`s between the first calls are links.
//
// The screen runs with the main thread's data bank, `$00`, so its tables are
// read through the slow copy of the cartridge there. The colours' job has
// `$80`, and the storm's threads `$82`.
//
// Every stretch records what it ran as a trace (`port/hw.h`): each run of
// the listing, each branch taken, and each register write, in order. Four
// write: `$212C` before the big letters, the divider's three bytes for each
// digit past the first, the colour maths at the end, and the storm's.
//
// Port code: libc only.

#ifndef PORT_CAST_H
#define PORT_CAST_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/hw.h"
#include "port/wram.h"

#define CAST_PC 0x808bbbu
#define CAST_BANK 0x80u
#define CAST_DATA_BANK 0x00u  // the main thread's: its tables through `$00`
#define CAST_STORM_BANK 0x82u
#define CAST_STORM_PC 0x82df6bu

// Where a stretch of the screen can begin.
#define CAST_ENTRIES(X) \
  X(begins, "$80:8BBB", 0x808bbbu) \
  X(letters_sent, "$80:8BD5", 0x808bd5u) \
  X(cache_ask, "$80:8BE5", 0x808be5u) \
  X(record_ask, "$80:8BF3", 0x808bf3u) \
  X(record_made, "$80:8D28", 0x808d28u) \
  X(record_handled, "$80:8D5F", 0x808d5fu) \
  X(title_written, "$80:8D23", 0x808d23u) \
  X(storm_1, "$80:8CD7", 0x808cd7u) \
  X(storm_2, "$80:8CE1", 0x808ce1u) \
  X(storm_3, "$80:8CEB", 0x808cebu) \
  X(storm_4, "$80:8CF5", 0x808cf5u) \
  X(storm_begun, "$80:8CFF", 0x808cffu) \
  X(colours_asked, "$80:8D0F", 0x808d0fu) \
  X(faded_in, "$80:8C00", 0x808c00u) \
  X(count_cleared, "$80:8E36", 0x808e36u) \
  X(words_cleared, "$80:8E13", 0x808e13u) \
  X(blanks_written, "$80:8DF7", 0x808df7u) \
  X(blank_waited, "$80:8C21", 0x808c21u) \
  X(shown_waited, "$80:8C3D", 0x808c3du) \
  X(words_heard, "$80:8E1B", 0x808e1bu) \
  X(words_written, "$80:8E28", 0x808e28u) \
  X(words_waited, "$80:8C47", 0x808c47u) \
  X(sign_written, "$80:8E05", 0x808e05u) \
  X(count_written, "$80:8DE9", 0x808de9u) \
  X(count_heard, "$80:8C7D", 0x808c7du) \
  X(count_waited, "$80:8C84", 0x808c84u) \
  X(end_heard, "$80:8E3E", 0x808e3eu) \
  X(end_played, "$80:8E55", 0x808e55u) \
  X(turn_waited, "$80:8C90", 0x808c90u) \
  X(start_waited, "$80:8C9A", 0x808c9au) \
  X(faded_out, "$80:8CAE", 0x808caeu)

// Where one stops: a call or a `WAI` the harness makes, or the `RTL`.
#define CAST_EXITS(X) \
  X(0x808bc4u) /* WAI */ \
  X(0x82acf9u) /* JSL dma_to_cgram, in `$82:ACF0` */ \
  X(0x808bebu) /* JSL sprite_cache_init */ \
  X(0x808d24u) /* JSL actor_slot_alloc */ \
  X(0x808d5bu) /* JSL thread_set_handler */ \
  X(0x808d1fu) /* JSL text_big_begin */ \
  X(0x808cd3u) /* JSL thread_spawn */ \
  X(0x808cddu) \
  X(0x808ce7u) \
  X(0x808cf1u) \
  X(0x808cfbu) \
  X(0x808d0bu) /* JSL vbl_queue_a_add */ \
  X(0x808bfcu) /* JSL fade_in */ \
  X(0x808e32u) /* JSL text_print_lines */ \
  X(0x808e0fu) \
  X(0x808df3u) \
  X(0x808c1du) /* JSL thread_yield */ \
  X(0x808c39u) \
  X(0x808e17u) /* JSL apu_play_sfx */ \
  X(0x808e24u) /* JSL text_print_lines */ \
  X(0x808c43u) /* JSL thread_yield */ \
  X(0x808e01u) /* JSL text_print_lines */ \
  X(0x808de5u) \
  X(0x808c79u) /* JSL apu_play_sfx */ \
  X(0x808c80u) /* JSL thread_yield */ \
  X(0x808e3au) /* JSL apu_play_sfx */ \
  X(0x808e51u) /* JSL pictures_play */ \
  X(0x808c8cu) /* JSL thread_yield */ \
  X(0x808c96u) \
  X(0x808caau) /* JSL fade_out */ \
  X(0x808c03u) /* RTL */

// The job that turns the colours over, from where the queue calls it and
// where the send comes back.
#define CAST_JOB_ENTRIES(X) \
  X(colours_job, "$80:A2AB", 0x80a2abu) \
  X(colours_job_sent, "$80:A2DF", 0x80a2dfu)

#define CAST_JOB_EXITS(X) \
  X(0x80a2dbu) /* JSL dma_to_cgram_at */ \
  X(0x80a2e1u) /* RTL, carry set: kept */

// The storm's threads, after each call and sleep. `$82:E019`, after the
// loop, frees the record and ends the thread, but nothing in the ROM goes
// there, and it is not here.
#define CAST_STORM_ENTRIES(X) \
  X(storm_waited, "$82:DF6F", 0x82df6fu) \
  X(storm_record, "$82:DF7A", 0x82df7au) \
  X(storm_across, "$82:DF82", 0x82df82u) \
  X(storm_down, "$82:DF9B", 0x82df9bu) \
  X(storm_palette, "$82:DFBF", 0x82dfbfu) \
  X(storm_handled, "$82:DFDB", 0x82dfdbu) \
  X(storm_played, "$82:DFE2", 0x82dfe2u) \
  X(storm_heard, "$82:DFE9", 0x82dfe9u) \
  X(storm_lights, "$82:DFF5", 0x82dff5u) \
  X(storm_flashed, "$82:E006", 0x82e006u) \
  X(storm_slept, "$82:E016", 0x82e016u) \
  X(storm_lit, "$82:E042", 0x82e042u) \
  X(storm_thunder, "$82:E049", 0x82e049u) \
  X(storm_dark, "$82:E050", 0x82e050u) \
  X(storm_unlit, "$82:E05D", 0x82e05du)

#define CAST_STORM_EXITS(X) \
  X(0x82df72u) /* JSL thread_yield */ \
  X(0x82df7eu) /* JSL rng */ \
  X(0x82df97u) \
  X(0x82dfbbu) \
  X(0x82dfd7u) /* JSL thread_set_handler */ \
  X(0x82dfdeu) /* JSL pictures_play */ \
  X(0x82dfe5u) /* JSL apu_play_sfx */ \
  X(0x82dff1u) /* JSL rng */ \
  X(0x82e002u) /* JSL thread_yield */ \
  X(0x82e00eu) /* JSL rng */ \
  X(0x82e03eu) /* JSL vbl_queue_a_add */ \
  X(0x82e045u) /* JSL apu_play_sfx */ \
  X(0x82e04cu) /* JSL thread_yield */ \
  X(0x82e059u) /* JSL vbl_queue_a_add */

// The storm's job: the fixed colour sent, once.
#define CAST_STORM_JOB_PC 0x82e076u
#define CAST_STORM_JOB_RTL_PC 0x82e081u

// The straight runs of the listing, each named for where it starts, with
// where it ends. `tools/price_runs.py` prices them. A run that ends `*` stops
// at a store to a register, and is priced to the store's write.
#define CAST_RUNS(X) \
  X(8BBB, 0x808bc4u) \
  X(8BD5, 0x808bd9u) \
  X(ACF0, 0x82acf9u) \
  X(8BE5, 0x808bebu) \
  X(8BF3, 0x808bf6u) \
  X(8D28, 0x808d5bu) \
  X(8D5F, 0x808d60u) \
  X(8BF6, 0x808bf9u) \
  X(8D10, 0x808d17u) /* * */ \
  X(8D17, 0x808d1fu) \
  X(8D23, 0x808d24u) \
  X(8BF9, 0x808bfcu) \
  X(8CCD, 0x808cd3u) \
  X(8CD7, 0x808cddu) \
  X(8CE1, 0x808ce7u) \
  X(8CEB, 0x808cf1u) \
  X(8CF5, 0x808cfbu) \
  X(8CFF, 0x808d0bu) \
  X(8D0F, 0x808d10u) \
  X(8C00, 0x808c03u) \
  X(8C04, 0x808c09u) \
  X(8C09, 0x808c0cu) \
  X(8E29, 0x808e32u) \
  X(8E36, 0x808e37u) \
  X(8C0C, 0x808c0fu) \
  X(8E06, 0x808e0fu) \
  X(8E13, 0x808e14u) \
  X(8C0F, 0x808c12u) \
  X(8DEA, 0x808df3u) \
  X(8DF7, 0x808df8u) \
  X(8C12, 0x808c1du) \
  X(8C21, 0x808c36u) \
  X(8C36, 0x808c39u) \
  X(8C3D, 0x808c40u) \
  X(8E14, 0x808e17u) \
  X(8E1B, 0x808e24u) \
  X(8E28, 0x808e29u) \
  X(8C40, 0x808c43u) \
  X(8C47, 0x808c4au) \
  X(8DF8, 0x808e01u) \
  X(8E05, 0x808e06u) \
  X(8C4A, 0x808c5cu) \
  X(8C5C, 0x808c5fu) \
  X(8C5F, 0x808c63u) \
  X(8C63, 0x808c6eu) \
  X(8D60, 0x808d8au) \
  X(8D8A, 0x808d8fu) \
  X(8D8F, 0x808d92u) /* * */ \
  X(8D92, 0x808d99u) /* * */ \
  X(8D99, 0x808dafu) \
  X(8DAF, 0x808db2u) /* * */ \
  X(8DB2, 0x808db9u) /* * */ \
  X(8DB9, 0x808dcfu) \
  X(8DCF, 0x808de5u) \
  X(8DE9, 0x808deau) \
  X(8C6E, 0x808c74u) \
  X(8C74, 0x808c79u) \
  X(8C7D, 0x808c80u) \
  X(8C84, 0x808c86u) \
  X(8C86, 0x808c89u) \
  X(8E37, 0x808e3au) \
  X(8E3E, 0x808e49u) \
  X(8E49, 0x808e4eu) \
  X(8E4E, 0x808e51u) \
  X(8E55, 0x808e5eu) \
  X(8C89, 0x808c8cu) \
  X(8C90, 0x808c93u) \
  X(8C93, 0x808c96u) \
  X(8C9A, 0x808ca2u) \
  X(8CA2, 0x808caau) \
  X(8CAE, 0x808cb5u) /* * */ \
  X(8CB5, 0x808cbau) /* * */ \
  X(8CBA, 0x808cbfu) /* * */ \
  X(8CBF, 0x808cc4u) /* * */ \
  X(8CC4, 0x808ccau) /* * */ \
  X(8CCA, 0x808ccdu) \
  X(A2AB, 0x80a2b0u) \
  X(A2B0, 0x80a2c1u) \
  X(A2C1, 0x80a2c7u) \
  X(A2C7, 0x80a2dbu) \
  X(A2DF, 0x80a2e1u) \
  X(A2E0, 0x80a2e1u)

#define CAST_STORM_RUNS(X) \
  X(DF6F, 0x82df72u) \
  X(DF7A, 0x82df7cu) \
  X(DF7C, 0x82df7eu) \
  X(DF82, 0x82df87u) \
  X(DF87, 0x82df8cu) \
  X(DF8C, 0x82df8eu) \
  X(DF8E, 0x82df97u) \
  X(DF9B, 0x82dfa0u) \
  X(DFA0, 0x82dfbbu) \
  X(DFBF, 0x82dfd7u) \
  X(DFDB, 0x82dfdeu) \
  X(DFE2, 0x82dfe5u) \
  X(DFE9, 0x82dff1u) \
  X(DFF5, 0x82dffau) \
  X(DFFA, 0x82dffdu) \
  X(DFFD, 0x82dfffu) \
  X(DFFF, 0x82e002u) \
  X(E006, 0x82e00eu) \
  X(E016, 0x82e019u) \
  X(E01F, 0x82e026u) /* * */ \
  X(E026, 0x82e02bu) /* * */ \
  X(E02B, 0x82e03eu) \
  X(E042, 0x82e045u) \
  X(E049, 0x82e04cu) \
  X(E050, 0x82e059u) \
  X(E05D, 0x82e064u) /* * */ \
  X(E064, 0x82e069u) /* * */ \
  X(E069, 0x82e06eu) /* * */ \
  X(E06E, 0x82e073u) /* * */ \
  X(E073, 0x82e076u) \
  X(E076, 0x82e07eu) /* * */ \
  X(E07E, 0x82e081u)

typedef enum {
#define X(at, end) CAST_RUN_##at,
  CAST_RUNS(X)
#undef X
  CAST_RUN_TAKEN,  // a branch taken: one idle more
  CAST_RUN_COUNT
} CastRun;

typedef enum {
#define X(at, end) CAST_STORM_RUN_##at,
  CAST_STORM_RUNS(X)
#undef X
  CAST_STORM_RUN_TAKEN,
  CAST_STORM_RUN_COUNT
} CastStormRun;

// The longest stretch is a turn's count written, at about twenty steps.
#define CAST_TRACE_STEPS 64

typedef struct {
  HwStep steps[CAST_TRACE_STEPS];
  HwTrace trace;  // runs, branches taken and writes, in order
  bool lost;      // a return to an address the screen does not know
} CastWork;

void cast_work_init(CastWork* k);

// A stretch of the screen, or of the colours' job, from `c->pc` to the
// instruction it stops on, which it leaves in `c->pc`.
void cast_run(Wram* w, const Rom* rom, PortCpu* c, CastWork* k);
// ...and of a storm's thread, or of its job.
void cast_storm_run(Wram* w, const Rom* rom, PortCpu* c, CastWork* k);

#endif
