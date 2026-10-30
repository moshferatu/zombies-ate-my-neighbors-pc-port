// The screen after a level: LEVEL COMPLETE, the neighbours saved, the
// bonuses, and every fourth level the password.
//
// `$82:BD6A` is called by the main thread when a level is won (`$80:84F2`)
// and comes back when the screen has faded out. It runs in that thread, a
// tick at a time, with its own data bank.
//
// **The screen.** It is blanked, VRAM cleared, its colours sent and its
// tiles unpacked and sent (`port/frontend_thread.h`, `port/dma.h`), and
// only the first layer and the sprites are shown (`$212C`). LEVEL COMPLETE
// is written, and each player's name if they are playing. Each player's
// lives are shown as that many records in a row, and their scores, a digit
// at a time with the leading noughts blank (`$82:BFD5`). It fades in.
//
// **The neighbours saved** (`$82:C068`). Each player's count of them is
// taken from `$1F9C`, a fewer for each if the two make more than ten, and
// then, a tick at a time, the next kind each saved is shown: a thread of
// its own (`$83:9699` and on), and a sound. Every fifth, and at the end,
// each of those is told (`thread_call_handler`) and pays 50 points.
//
// **The bonuses.** First `$82:C56B`: with both playing, 1,000 points to
// whoever saved more. Then a list of handlers in the level's record
// (`$9F:003A` and on), a tick each. Level 21's, for one, is three:
//
//     $82:CFEB  9,900 points for ALIEN INVASION REPULSED, by `$1FE4`
//     $82:C499  1,000 points for ten neighbours saved
//     $82:D0CB  2,000 points of the level's own, by `$1FF0`; and a level
//               may have words of its own first, by `$82:D1EE`, and on
//               some it waits for a pad there and pays nothing
//
// `$82:CE17` pays 2,000 for two Frankenstein's monsters killed (their tally
// is `port/frankenstein.h`'s). A bonus is paid by `$82:D774`: its points added
// to what the score is counting up to, a sound, and its words flashed three
// times. The score counts up a hundred at a time after each, a sound each.
//
// Last, `$82:D44E`: a player whose score has passed the next 40,000 is
// given a neighbour more, if the two saved fewer than ten; or else a life,
// a record more in the row, flashed; or with five lives already, 5,000
// points.
//
// **The password** is shown after levels whose number is a multiple of four,
// and it waits for a pad. Otherwise it waits three seconds or for a pad.
// Then it fades out.
//
// Of the bonus handlers, five are ported: those of `$82:C499`, `$82:C56B`,
// `$82:CE17`, `$82:CFEB` and `$82:D0CB`, which are the ones the sessions
// reach. They are called by `JSR ($C346,X)`, which the harness does not
// make, so a stretch that would call another is turned down and the 65816
// runs it.
//
// **Written as blocks.** Each block runs from an address of the ROM's to
// the next it goes to: an address after a branch or a `JSR`, or one an
// `RTS` pulls off the stack. A stretch begins at any address the screen can
// be come back to at, and runs block after block until it stops: on a sleep,
// a `WAI`, a sound, a call to the text or the records, or its last `RTL`.
// So nothing about where it is in its tree of calls has to be known first:
// the stack says, and a return to an address the port does not know is
// turned down before the stretch runs.
//
// Port code: libc only.

#ifndef PORT_LEVEL_END_H
#define PORT_LEVEL_END_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/hw.h"
#include "port/wram.h"

#define LEVEL_END_BANK 0x82u
#define LEVEL_END_PC 0x82bd6au

// Where a stretch can begin: after a call, a sleep or a `WAI`.
#define LEVEL_END_ENTRIES(X) \
  X(begins, "$82:BD6A", 0x82bd6au) \
  X(cleared, "$82:BE8D", 0x82be8du) \
  X(coloured, "$82:BE9A", 0x82be9au) \
  X(unpacked, "$82:BEAB", 0x82beabu) \
  X(layers, "$82:BEB4", 0x82beb4u) \
  X(named_a, "$82:BECF", 0x82becfu) \
  X(named_b, "$82:BEE1", 0x82bee1u) \
  X(titled, "$82:BEEE", 0x82beeeu) \
  X(lit, "$82:BEF2", 0x82bef2u) \
  X(oam_cleared, "$82:BF03", 0x82bf03u) \
  X(sprites_ready, "$82:BF11", 0x82bf11u) \
  X(life_made, "$82:BF38", 0x82bf38u) \
  X(lives_shown, "$82:BF95", 0x82bf95u) \
  X(score_sent, "$82:C02C", 0x82c02cu) \
  X(scores_sent, "$82:C038", 0x82c038u) \
  X(fading_in, "$82:BD80", 0x82bd80u) \
  X(saved_a, "$82:C0F1", 0x82c0f1u) \
  X(saved_a_heard, "$82:C0FF", 0x82c0ffu) \
  X(saved_b, "$82:C135", 0x82c135u) \
  X(saved_b_heard, "$82:C143", 0x82c143u) \
  X(saved_done, "$82:C16A", 0x82c16au) \
  X(pays_saved, "$82:C174", 0x82c174u) \
  X(pays_next, "$82:C181", 0x82c181u) \
  X(paid_a, "$82:C195", 0x82c195u) \
  X(paid_a_heard, "$82:C1B1", 0x82c1b1u) \
  X(paid_b, "$82:C1C5", 0x82c1c5u) \
  X(paid_b_heard, "$82:C1E1", 0x82c1e1u) \
  X(ticked, "$82:BD98", 0x82bd98u) \
  X(bonus_back, "$82:C2AB", 0x82c2abu) \
  X(all_paid, "$82:C2C4", 0x82c2c4u) \
  X(counted_a, "$82:C2F8", 0x82c2f8u) \
  X(counted_b, "$82:C327", 0x82c327u) \
  X(count_waited, "$82:C342", 0x82c342u) \
  X(bonus_heard, "$82:D79D", 0x82d79du) \
  X(bonus_shown, "$82:D7B6", 0x82d7b6u) \
  X(bonus_flashed, "$82:D7BD", 0x82d7bdu) \
  X(bonus_done, "$82:D7CE", 0x82d7ceu) \
  X(life_added, "$82:D4FC", 0x82d4fcu) \
  X(life_flashed, "$82:D54C", 0x82d54cu) \
  X(life_done, "$82:D56F", 0x82d56fu) \
  X(level_bonus_waited, "$82:D14F", 0x82d14fu) \
  X(rest_waited, "$82:BDC6", 0x82bdc6u) \
  X(fading_out, "$82:BDD4", 0x82bdd4u) \
  X(oam_put_back, "$82:BDEA", 0x82bdeau) \
  X(ends, "$82:BDF2", 0x82bdf2u) \
  X(password_written, "$82:BE06", 0x82be06u) \
  X(password_heard, "$82:BE0D", 0x82be0du) \
  X(password_shown, "$82:BE27", 0x82be27u) \
  X(password_waited, "$82:BE2E", 0x82be2eu) \
  X(password_hidden, "$82:BE42", 0x82be42u) \
  X(password_flashed, "$82:BE49", 0x82be49u) \
  X(password_read, "$82:BE55", 0x82be55u)

// Where one stops: a call or a `WAI` the harness makes, or its `RTL`.
#define LEVEL_END_EXITS(X) \
  X(0x82be89u) /* JSL vram_clear_ask */ \
  X(0x82be96u) /* JSL dma_to_cgram */ \
  X(0x82bea7u) /* JSL dma_to_vram, unpacked */ \
  X(0x82beacu) /* JSL $82:ACD6 */ \
  X(0x82becbu) /* JSL text_print */ \
  X(0x82beddu) \
  X(0x82beeau) \
  X(0x82bef1u) /* WAI */ \
  X(0x82beffu) /* JSL $80:BDF2 */ \
  X(0x82bf09u) /* JSL sprite_cache_init */ \
  X(0x82bf34u) /* JSL actor_slot_alloc */ \
  X(0x82bf91u) /* JSL thread_yield */ \
  X(0x82c028u) /* JSL $82:B84A */ \
  X(0x82c034u) /* JSL $82:B820 */ \
  X(0x82bd7bu) /* JSL thread_yield */ \
  X(0x82bd7fu) /* WAI */ \
  X(0x82c0edu) /* JSL thread_spawn */ \
  X(0x82c0fbu) /* JSL apu_play_sfx */ \
  X(0x82c131u) \
  X(0x82c13fu) \
  X(0x82c166u) /* JSL thread_yield */ \
  X(0x82c170u) \
  X(0x82c17du) \
  X(0x82c191u) /* JSL thread_call_handler */ \
  X(0x82c1adu) /* JSL apu_play_sfx */ \
  X(0x82c1c1u) \
  X(0x82c1ddu) \
  X(0x82bd94u) /* JSL thread_yield */ \
  X(0x82c2c0u) /* JSL text_print */ \
  X(0x82c2f4u) /* JSL apu_play_sfx */ \
  X(0x82c323u) \
  X(0x82c33eu) /* JSL thread_yield */ \
  X(0x82d799u) /* JSL apu_play_sfx */ \
  X(0x82d7b2u) /* JSL text_print */ \
  X(0x82d7b9u) /* JSL thread_yield */ \
  X(0x82d7cau) \
  X(0x82d4f8u) /* JSL actor_slot_alloc */ \
  X(0x82d548u) /* JSL thread_yield */ \
  X(0x82d56bu) \
  X(0x82d14bu) \
  X(0x82bdc2u) /* JSL thread_yield */ \
  X(0x82bdd3u) /* WAI */ \
  X(0x82bde5u) /* WAI */ \
  X(0x82bdedu) /* JSL $80:83D5 */ \
  X(0x82bdf4u) /* RTL */ \
  X(0x82bdfeu) /* JSL text_print */ \
  X(0x82be09u) /* JSL apu_play_sfx */ \
  X(0x82be1fu) /* JSL $82:B84A */ \
  X(0x82be2au) /* JSL thread_yield */ \
  X(0x82be3au) \
  X(0x82be45u) \
  X(0x82be51u)

// The straight runs of the listing the blocks are made of, each named for
// where it starts, with where it ends. `tools/price_runs.py` prices them.
#define LEVEL_END_RUNS(X) \
  X(BD6A, 0x82bd72u) \
  X(BD72, 0x82bd75u) \
  X(BD75, 0x82bd78u) \
  X(BD78, 0x82bd7bu) \
  X(BD80, 0x82bd88u) \
  X(BD88, 0x82bd8eu) \
  X(BD8E, 0x82bd91u) \
  X(BD91, 0x82bd94u) \
  X(BD98, 0x82bda1u) \
  X(BDA1, 0x82bda3u) \
  X(BDA3, 0x82bda8u) \
  X(BDA8, 0x82bdadu) \
  X(BDAD, 0x82bdb0u) \
  X(BDB0, 0x82bdb9u) \
  X(BDB9, 0x82bdbfu) \
  X(BDBF, 0x82bdc2u) \
  X(BDC6, 0x82bdceu) \
  X(BDCE, 0x82bdd3u) \
  X(BDD4, 0x82bdd9u) \
  X(BDD9, 0x82bddfu) \
  X(BDDF, 0x82bde5u) \
  X(BDEA, 0x82bdedu) \
  X(BDF2, 0x82bdf4u) \
  X(BDF5, 0x82bdfeu) \
  X(BE06, 0x82be09u) \
  X(BE0D, 0x82be13u) \
  X(BE13, 0x82be1fu) \
  X(BE27, 0x82be2au) \
  X(BE2E, 0x82be3au) \
  X(BE42, 0x82be45u) \
  X(BE49, 0x82be4eu) \
  X(BE4E, 0x82be51u) \
  X(BE55, 0x82be5du) \
  X(BE5E, 0x82be64u) \
  X(BE64, 0x82be6bu) \
  X(BE6B, 0x82be7cu) \
  X(BE7C, 0x82be7fu) \
  X(BE7F, 0x82be80u) \
  X(BE80, 0x82be89u) \
  X(BE8D, 0x82be96u) \
  X(BE9A, 0x82bea7u) \
  X(BEAB, 0x82beacu) \
  X(BEB4, 0x82bebbu) \
  X(BEBB, 0x82bec2u) \
  X(BEC2, 0x82becbu) \
  X(BECF, 0x82bed4u) \
  X(BED4, 0x82beddu) \
  X(BEE1, 0x82beeau) \
  X(BEEE, 0x82bef1u) \
  X(BEF2, 0x82befeu) \
  X(BF03, 0x82bf09u) \
  X(BF11, 0x82bf20u) \
  X(BF20, 0x82bf28u) \
  X(BF28, 0x82bf34u) \
  X(BF38, 0x82bf7fu) \
  X(BF7F, 0x82bf86u) \
  X(BF86, 0x82bf8eu) \
  X(BF8E, 0x82bf91u) \
  X(BFD5, 0x82bfdbu) \
  X(BFDB, 0x82bfe3u) \
  X(BFE3, 0x82bff7u) \
  X(BFF7, 0x82c004u) \
  X(C004, 0x82c00du) \
  X(C00D, 0x82c013u) \
  X(C013, 0x82c028u) \
  X(C02C, 0x82c034u) \
  X(C039, 0x82c03bu) \
  X(C03B, 0x82c040u) \
  X(C040, 0x82c045u) \
  X(C045, 0x82c04du) \
  X(C04D, 0x82c056u) \
  X(C068, 0x82c088u) \
  X(C088, 0x82c090u) \
  X(C090, 0x82c093u) \
  X(C093, 0x82c098u) \
  X(C098, 0x82c0a4u) \
  X(C0A4, 0x82c0abu) \
  X(C0AB, 0x82c0b1u) \
  X(C0B1, 0x82c0b5u) \
  X(C0B5, 0x82c0bbu) \
  X(C0BB, 0x82c0c0u) \
  X(C0C0, 0x82c0d9u) \
  X(C0D9, 0x82c0edu) \
  X(C0F1, 0x82c0fbu) \
  X(C0FF, 0x82c104u) \
  X(C104, 0x82c11du) \
  X(C11D, 0x82c131u) \
  X(C135, 0x82c13fu) \
  X(C143, 0x82c14eu) \
  X(C14E, 0x82c15bu) \
  X(C15B, 0x82c15eu) \
  X(C15E, 0x82c160u) \
  X(C160, 0x82c163u) \
  X(C163, 0x82c166u) \
  X(C16A, 0x82c16du) \
  X(C16D, 0x82c170u) \
  X(C174, 0x82c177u) \
  X(C177, 0x82c17au) \
  X(C17A, 0x82c17du) \
  X(C181, 0x82c190u) \
  X(C190, 0x82c191u) \
  X(C195, 0x82c1adu) \
  X(C1B1, 0x82c1c0u) \
  X(C1C0, 0x82c1c1u) \
  X(C1C5, 0x82c1ddu) \
  X(C1E1, 0x82c1f9u) \
  X(C1F9, 0x82c1fcu) \
  X(C1FC, 0x82c209u) \
  X(C251, 0x82c259u) \
  X(C259, 0x82c279u) \
  X(C279, 0x82c282u) \
  X(C282, 0x82c284u) \
  X(C284, 0x82c297u) \
  X(C297, 0x82c299u) \
  X(C299, 0x82c2a2u) \
  X(C2A2, 0x82c2a8u) \
  X(C2A8, 0x82c2abu) \
  X(C2AB, 0x82c2aeu) \
  X(C2AE, 0x82c2b0u) \
  X(C2B1, 0x82c2b4u) \
  X(C2B4, 0x82c2b7u) \
  X(C2B7, 0x82c2c0u) \
  X(C2C4, 0x82c2c6u) \
  X(C2C6, 0x82c2d1u) \
  X(C2D1, 0x82c2d9u) \
  X(C2D9, 0x82c2f4u) \
  X(C2F8, 0x82c300u) \
  X(C300, 0x82c308u) \
  X(C308, 0x82c323u) \
  X(C327, 0x82c32cu) \
  X(C32C, 0x82c334u) \
  X(C334, 0x82c336u) \
  X(C336, 0x82c338u) \
  X(C338, 0x82c33bu) \
  X(C33B, 0x82c33eu) \
  X(C342, 0x82c345u) \
  X(C499, 0x82c4a1u) \
  X(C4A1, 0x82c4adu) \
  X(C4AD, 0x82c4b5u) \
  X(C4B5, 0x82c4c4u) \
  X(C56B, 0x82c573u) \
  X(C574, 0x82c57cu) \
  X(C57C, 0x82c57eu) \
  X(C57E, 0x82c58au) \
  X(C58A, 0x82c599u) \
  X(CE17, 0x82ce1cu) \
  X(CE1C, 0x82ce21u) \
  X(CE21, 0x82ce2au) \
  X(CE2A, 0x82ce36u) \
  X(CE36, 0x82ce3bu) \
  X(CE3B, 0x82ce40u) \
  X(CE40, 0x82ce49u) \
  X(CE49, 0x82ce58u) \
  X(CFEB, 0x82cff0u) \
  X(CFF0, 0x82cff9u) \
  X(CFF9, 0x82d005u) \
  X(D005, 0x82d00au) \
  X(D00A, 0x82d013u) \
  X(D013, 0x82d022u) \
  X(D0CB, 0x82d0d0u) \
  X(D0D0, 0x82d0d5u) \
  X(D0D5, 0x82d0e0u) \
  X(D0E0, 0x82d0e3u) \
  X(D0E3, 0x82d0efu) \
  X(D0EF, 0x82d0f4u) \
  X(D0F4, 0x82d0f9u) \
  X(D0F9, 0x82d104u) \
  X(D104, 0x82d107u) \
  X(D107, 0x82d116u) \
  X(D118, 0x82d122u) \
  X(D122, 0x82d126u) \
  X(D126, 0x82d12bu) \
  X(D12B, 0x82d137u) \
  X(D137, 0x82d139u) \
  X(D139, 0x82d148u) \
  X(D148, 0x82d14bu) \
  X(D14F, 0x82d157u) \
  X(D157, 0x82d159u) \
  X(D159, 0x82d15eu) \
  X(D15E, 0x82d16au) \
  X(D16A, 0x82d16cu) \
  X(D16C, 0x82d17bu) \
  X(D17B, 0x82d17du) \
  X(D44E, 0x82d451u) \
  X(D451, 0x82d460u) \
  X(D460, 0x82d463u) \
  X(D463, 0x82d468u) \
  X(D468, 0x82d485u) \
  X(D485, 0x82d492u) \
  X(D492, 0x82d4b5u) \
  X(D4B5, 0x82d4c0u) \
  X(D4C0, 0x82d4c7u) \
  X(D4C7, 0x82d4c9u) \
  X(D4C9, 0x82d4d8u) \
  X(D4D8, 0x82d4e5u) \
  X(D4E5, 0x82d4e8u) \
  X(D4E9, 0x82d4f8u) \
  X(D4FC, 0x82d545u) \
  X(D545, 0x82d548u) \
  X(D54C, 0x82d558u) \
  X(D558, 0x82d55du) \
  X(D55D, 0x82d560u) \
  X(D560, 0x82d568u) \
  X(D568, 0x82d56bu) \
  X(D774, 0x82d796u) \
  X(D796, 0x82d799u) \
  X(D79D, 0x82d7a3u) \
  X(D7A3, 0x82d7b2u) \
  X(D7B6, 0x82d7b9u) \
  X(D7BD, 0x82d7c5u) \
  X(D7C5, 0x82d7cau)

typedef enum {
#define X(from, to) LEVEL_END_RUN_##from,
  LEVEL_END_RUNS(X)
#undef X
  LEVEL_END_RUN_COUNT,
  LEVEL_END_RUN_RTS = LEVEL_END_RUN_BE7F,  // any `RTS`: they all cost the same
} LevelEndRun;

// What a stretch did, for the harness to price.
typedef struct {
  uint16_t runs[LEVEL_END_RUN_COUNT];
  uint16_t taken;  // branches taken
  // The stretch that sets the layers writes `$212C` in its first run.
  bool layers_set;
  // Overflow is the ROM's at the end, which it is not after a decimal add.
  bool v_known;
  // It went back to an address the port does not know, or to one of the
  // ROM's own after a handler it does not have: turned down.
  bool lost;
} LevelEndWork;

#define LEVEL_END_TM 0x212cu
#define LEVEL_END_TM_SPRITES_AND_FIRST 0x11u

// Run the stretch that begins at `c->pc`.
void level_end_run(Wram* w, const Rom* rom, PortCpu* c, LevelEndWork* k);

#endif
