// $80:C07F, $80:C0A3, $80:C139 — the status panel, and everything under it.
//
// This is the two players' HUD: health bar, weapon icon and count, item icon and
// count, score. Twenty-three routines in bank `$80` between `$C07F` and
// `$C7BF`, and they form the cleanest tree in the game — an entry that
// alternates between the two panels, two panels that are six change tests each,
// twelve one-line adapters that turn a change into a call, and eight primitives
// that draw.
//
// Nothing in it touches hardware. That is worth saying first, because a HUD
// sounds like the last thing a WRAM diff could check: every routine here writes
// a **shadow tilemap in WRAM** at `$7E:5F36`, and a separate vblank job uploads
// that to VRAM later. So the whole cluster is pure memory, and the harness can
// check it byte for byte like anything else.
//
// ## The shape
//
//     $80:C07F  hud_refresh     one panel per call, alternating
//       $80:C0A3  hud_panel     player 1: six change tests
//       $80:C139  hud_panel     player 2: the same six, other addresses
//         $80:C6E4 .. $80:C7AB  twelve adapters, 3-10 instructions each
//           $80:C379  hud_bar         the health bar
//           $80:C4EC  hud_digit       one digit, with leading-zero suppression
//           $80:C519  hud_digits8     an 8-digit BCD number (the score)
//           $80:C553  hud_digits3     a 3-digit BCD number (a count)
//           $80:C580  hud_blank       six tiles cleared
//           $80:C59C  hud_health      health -> hud_bar
//           $80:C5C2  hud_weapon_icon a 2x2 icon from a tile table
//           $80:C666  hud_item_icon   ...and the item's, which is not the same
//
// Every one of the twenty routines under the two panels is reached **only** from
// inside this tree — checked for `JSR`, `JMP`, `JSL` and `JML` across the whole
// ROM. The two panels are the only entries with outside callers, and `$80:C07F`
// is the only one with far callers.
//
// ## Only one panel is refreshed per call, and the toggle is on a thread's page
//
// `$80:C07F  LDA $24 : EOR #$0001 : STA $24` — the low bit of a direct-page word
// picks which player's panel this call looks at. It is a `JSL` target with five
// call sites in three banks, so `$24` is *the caller's* `$24`, on whatever
// thread's 128-byte page is current, and two different callers therefore each
// keep their own alternation. `hud_refresh` takes `dp` for exactly this reason.
//
// The consequence is that each panel is checked for changes on every *other*
// call, so a value that changes and changes back within two calls is never
// drawn. That is not a bug the port could smooth over — it is what the ROM does,
// and the diff would catch smoothing it.
//
// ## Change detection is a shadow copy, and the shadows are interleaved
//
// Each of the six things a panel shows has a shadow word at `$7E:6036`, and the
// panel is six repetitions of
//
//     LDA <live> : CMP <shadow> : BEQ next
//     STA <shadow> : JSR <adapter> : INC $1E7A
//
// `$1E7A` is a dirty count, and `$80:C07F`'s last act is to turn a nonzero one
// into a queued vblank job (`$80:C34A`, bank `$80`) that uploads the shadow
// tilemap. It is a count and not a flag, but nothing ever reads the magnitude.
//
// The score is the one that does not fit the pattern, in two ways: it is 32 bits
// so it compares two words before deciding, and it draws *before* it updates its
// shadows rather than after. Nothing depends on the order — no adapter reads a
// shadow — but the port keeps it, because a WRAM diff would see the difference
// if a nested call ever came to.
//
// The shadows are laid out **by field and then by player**, not by player and
// then by field: health p1, health p2, weapon p1, weapon p2, and so on, ending
// with the two 32-bit scores. So the two panels' shadow addresses interleave at
// stride 2, which is the same stride the live per-player words use, and every
// one of them is therefore `base + side`.
//
// ## The second panel is the first shifted sixteen tiles
//
// Every tilemap column in this file is either a constant in an adapter or a word
// in a two-entry ROM table, and in all six cases player 2's is player 1's plus
// `$20` — sixteen words, half of a 32-tile tilemap row. `$04`/`$24` for the
// health bar, `$12`/`$32` and `$18`/`$38` for the two icons, `$90`/`$B0` and
// `$96`/`$B6` for the two counts, `$7E`/`$9E` for the score. So the panel is one
// layout drawn twice into the two halves of the same four rows, and the ROM
// spells out both copies rather than adding `side * 16` anywhere.
//
// ## "Side" versus "score slot", and why both appear
//
// The per-player words are indexed by a **side**: 0 or 2, already doubled, the
// same value `port/score.h` describes. `W_PLAYER_HEALTH + side` is that player's
// health, and every shadow and tilemap column follows the same rule.
//
// But the *colour* does not. `$80:C59C`, `$80:C5C2` and `$80:C666` all fetch
// their palette or their tile table through `$7E:1E84 + side` — the score-slot
// pairing — and then index a two-entry ROM table with the value they find. So
// **where** a bar is drawn is fixed by which half of the panel you are, and
// **what colour** it is drawn in is fixed by which score slot you own. `$80:925D`
// seeds the pairing as the identity, so on a stock boot the two agree; they are
// still two different lookups, and the port keeps them two.
//
// ## Leading-zero suppression is a rotate, not a flag
//
// `$80:C4EC` is the digit emitter, and the state it carries between digits lives
// in direct-page `$1E`:
//
//     BNE emit          ; a nonzero digit always prints
//     BIT $1E : BMI emit ; a zero prints only once something already has
//     LDA #$0000 : BEQ store   ; ...otherwise a blank tile
//     emit: CLC : ADC #$3C07 : SEC : ROR $1E
//     store: STA $7E5F36,X : INX : INX
//
// `SEC : ROR $1E` sets bit 15 — but it also **shifts what was already there**,
// so `$1E` after four emitted digits is not a flag with one bit set, it is
// `$F000`, and after eight it is `$FF00`. Only bit 15 is ever tested. The port
// reproduces the rotate rather than the test, because `$1E` is a live
// direct-page word that the harness diffs along with everything else: a port
// that stored `$8000` would satisfy every `BMI` in the game and fail the very
// first comparison.
//
// The rotate is also where the routine's carry comes from, and it is the bit
// rotated *out* — bit 0 of the old `$1E`. See the note on carry below.
//
// `$C4EC` reads the Z flag its caller arrived with, which is the `AND #$000F`
// that isolated the digit. It is a real register input, not housekeeping.
//
// ## The number renderers walk *bytes* with 16-bit loads
//
// `$80:C519` (the score) starts at the top byte of a four-byte BCD counter and
// walks down, emitting each byte's high nibble then its low nibble — eight
// digits. It reads with `LDA $0000,Y`, which is a **word** load, and then takes
// nibbles out of the low half; `DEY` moves by one, so consecutive iterations
// read overlapping words and use only the low byte of each. `$80:C553` does the
// same for three digits out of one word, taking the top digit from `$0001,Y`.
//
// Both end with the same coda: if no digit was emitted at all, force the last
// column to a literal `0`, at `$7E:5F34,X` — one word *below* the cursor, which
// after the loop is the column the last digit went into.
//
// `$80:C519` opens with two dead instructions, `LDA $0000,Y : ORA $FFFE,Y`,
// whose result is thrown away by the `LDA` at the top of the loop. They read a
// word two bytes below the counter and change nothing that survives. They are
// not ported, because there is nothing to port; they are noted here so the next
// reader does not go looking for the effect.
//
// ## Both icon routines have a dead half
//
// `$80:C5C2` (weapon) and `$80:C666` (item) each open by testing the selected
// slot for `BMI` — nothing selected — and branching to a "clear it" path. The
// weapon's clears **six** tiles, three rows of two, including a row its drawing
// path never writes; the item's is a bare `RTS` that clears nothing.
//
// Neither path can run. `$80:C5C2` is reached only from `$80:C76E` and
// `$80:C79C`, and `$80:C666` only from `$80:C785` and `$80:C7B3` — and all four
// of those are on the far side of a `BMI` on the *same word*, in adapters that
// do their own clearing when it is negative. So the inner test is always false
// by the time it runs.
//
// The port does not implement either path, and neither carries a coverage site.
// A branch no input can reach would sit in the untaken list forever saying
// nothing, which is the same reason `port/apu.c` has none.
//
// ## Carry is modelled, and it is not simple
//
// Nothing in this cluster returns a value in carry, and it would be easy to
// leave it unclaimed. It is claimed anyway, threaded through every primitive,
// because an unclaimed flag is an unchecked output — the lesson `$80:83AE` cost
// (see `src/cosim/routines.c`). The chain runs: `CMP <shadow>` in the panel,
// `CMP #$000E` in a count adapter, `ASL`/`ADC` while building a table index,
// `LSR` while shifting out a nibble, and finally the `ROR $1E` inside every
// emitted digit. Whichever ran last is what the caller gets, and on the paths
// where none ran it is the caller's own.
//
// ## What is not here
//
// `$80:C505`, a two-digit renderer sitting between `$C4EC` and `$C519`, has no
// caller anywhere in the ROM — searched for `JSR`, `JMP`, `JSL` and `JML`, all
// four came back empty. It is not ported and not registered.
//
// `$80:C1CF` and its neighbours call `$C0A3` and `$C139` after clearing the
// whole shadow tilemap; they are a screen transition rather than a refresh, and
// they are why the two panels are registered in their own right as well as under
// `$80:C07F`. A routine reached from two places and checked at one is checked
// half as hard.
//
// Port code: libc only.

#ifndef PORT_HUD_H
#define PORT_HUD_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define HUD_REFRESH_ENTRY 0x80c07fu
#define HUD_PANEL1_ENTRY 0x80c0a3u
#define HUD_PANEL2_ENTRY 0x80c139u

// The bank every absolute read in this cluster resolves against. `LDA $C5B6,X`
// and `LDA $0000,Y` are the same instruction shape with a data bank of `$80`:
// under `$2000` it is the WRAM mirror, over `$8000` it is ROM. The port splits
// them by address because it has to, and this is the constant for the ROM half.
#define HUD_TABLE_BANK 0x800000u

// --- Direct page, on whichever thread called in ---
#define HUD_DP_LEADING 0x1eu  // the leading-zero rotate; see above
#define HUD_DP_SCRATCH 0x20u  // the side, and the digit countdown
#define HUD_DP_TABLE 0x22u    // the icon tile table this call chose
#define HUD_DP_PHASE 0x24u    // which panel the next call will look at

// --- The shadow tilemap ---
//
// Four rows of 32 words at `$7E:5F36`, ending one word before the change
// shadows below. Every draw in this file writes `base + row + x` where `x` is a
// byte offset the adapter chose and `row` is a multiple of `HUD_TILEMAP_ROW`.
#define W_HUD_TILEMAP 0x5f36u
#define HUD_TILEMAP_ROW 0x40u

// --- The change shadows: `base + side`, except the score ---
#define W_HUD_SHADOW_HEALTH 0x6036u
#define W_HUD_SHADOW_WEAPON 0x603au
#define W_HUD_SHADOW_ITEM 0x603eu
#define W_HUD_SHADOW_ITEM_COUNT 0x6042u
#define W_HUD_SHADOW_WEAPON_COUNT 0x6046u
// Two words per player, so this one is `base + side * 2`: `$604A`/`$604C` for
// player 1 and `$604E`/`$6050` for player 2.
#define W_HUD_SHADOW_SCORE 0x604au

// --- ROM tables, all bank $80 absolute-indexed ---

// 11 rows of 10 words: the health bar at every health from 0 to 10, five tiles
// wide and two rows deep, full/half/empty as `$3001`/`$3003`/`$3005` on the top
// row and `$3002`/`$3004`/`$3006` beneath. The palette is ORed in afterwards.
#define HUD_BAR_TABLE 0xc3deu
#define HUD_BAR_ROW 20u   // bytes per row
#define HUD_BAR_TILES 10  // words per row

// The row is built as `A*4`, then `A*16 + A*4`, which is `A*20`. There is no
// bounds check: ten is the last row the table has, and what keeps health there
// is `$80:FACF` at the other end of the game.
#define HUD_BAR_MAX_HEALTH 10

#define HUD_HEALTH_ATTR 0xc5b6u    // 2 words, by score slot: $3400 / $3800
#define HUD_HEALTH_COLUMN 0xc5bau  // 2 words, by side: $0004 / $0024
#define HUD_HEALTH_AT 0xc5beu      // 2 words, by side: $1CB8 / $1CBA

#define HUD_WEAPON_ICON_COLUMN 0xc612u  // 2 words, by side: $0012 / $0032
#define HUD_WEAPON_ICON_TABLE 0xc616u   // 2 pointers, by score slot
#define HUD_ITEM_ICON_COLUMN 0xc698u    // 2 words, by side: $0018 / $0038
#define HUD_ITEM_ICON_TABLE 0xc69cu     // 2 pointers, by score slot

// The two icons are four consecutive tiles in a 2x2 block, and the tile number
// is one word from the table above — the other three are `INC`s of it.
#define HUD_ICON_TILES 4

// The count adapters' tilemap columns and their ceilings. The ceilings are the
// two inventories' lengths, from `port/wram.h`; the columns are immediates.
#define HUD_WEAPON_COUNT_COLUMN 0x0090u
#define HUD_ITEM_COUNT_COLUMN 0x0096u
#define HUD_SCORE_COLUMN 0x007eu
#define HUD_WEAPON_ICON_BLANK_COLUMN 0x0012u
#define HUD_ITEM_ICON_BLANK_COLUMN 0x0018u
// Player 2's copy of any of the above.
#define HUD_PANEL2_SHIFT 0x20u

// `$80:C4EC  ADC #$3C07` with carry clear: digit 0 is tile `$3C07` and the rest
// follow. The same constant is the forced zero both renderers end on.
#define HUD_DIGIT_TILE 0x3c07u

// `$80:C098  LDA #$C34A : LDY #$0080 : JML $8083AE` — the shadow tilemap's
// upload, queued on the forced-blank queue whenever anything changed.
#define HUD_UPLOAD_JOB 0xc34au
#define HUD_UPLOAD_JOB_BANK 0x0080u

// The two sides, which are player numbers already doubled.
#define HUD_SIDE_P1 0u
#define HUD_SIDE_P2 2u

// --- What the refresh actually did ------------------------------------------
//
// **The panel's cost is not a number, it is a shape.** One call costs 1,084
// cycles when the six comparisons all match their shadows and 11,950 when the
// score, both counts and both icons have all moved — and substituted against the
// mean of the two, this routine drags the machine's clock far enough to part
// `movies/level25-2p` from the stock core's framebuffer. `cosim_cost` is the
// harness side of the answer; this is the port's.
//
// Every constant is one straight-line run of the listing, and the run is named
// by where it starts. Nothing here is a cycle count: these are counts of
// branches taken, the same kind of fact `PORT_COVER` records, and
// `src/cosim/routines.c` is where each one is multiplied by what the
// corresponding instructions cost. Keeping the two apart is what lets the port
// stay ignorant of the machine's clock while still being able to say what it
// did.
//
// The whole model is checked against the ROM on every call — see the
// cost-model report `zamn_cosim verify` prints.
typedef enum {
  // $80:C07F itself. The two phases differ by one taken branch, and the tail is
  // either the quiet `RTL` or the upload.
  HUD_BLK_REFRESH_P1,
  HUD_BLK_REFRESH_P2,
  HUD_BLK_REFRESH_IDLE,
  HUD_BLK_REFRESH_QUEUE,
  // ...and $80:83AE, which `$80:C098` tail-jumps into, so its cost is part of
  // this call. `QUEUE_BUSY` is one rejected slot of the search; `QUEUE_FELL` is
  // the search running off the end and using slot 0 regardless.
  HUD_BLK_QUEUE_FULL,
  HUD_BLK_QUEUE_ACCEPTED,
  HUD_BLK_QUEUE_BUSY,
  HUD_BLK_QUEUE_FELL,
  // $80:C0A3 / $80:C139, which price the same: the two are the same code with
  // different constants in it.
  HUD_BLK_PANEL_OFF,
  HUD_BLK_PANEL_ON,
  HUD_BLK_PANEL_RTS,
  // The six comparisons. Health and the two icons have one shape, the two
  // counts another (they index an inventory first), and the score a third — it
  // is the only 32-bit test, so it can find the low half equal and the high half
  // not.
  HUD_BLK_FIELD_SAME,
  HUD_BLK_FIELD_CHANGED,
  HUD_BLK_COUNT_SAME,
  HUD_BLK_COUNT_CHANGED,
  HUD_BLK_SCORE_SAME,
  HUD_BLK_SCORE_LOW,
  HUD_BLK_SCORE_HIGH,
  HUD_BLK_SCORE_TAIL,
  // The adapters at $80:C6E4..$80:C7AB — three instructions each, and the reason
  // the tree has sixteen routines in it rather than six.
  HUD_BLK_ADAPT_SCORE,
  HUD_BLK_ADAPT_HEALTH,
  HUD_BLK_ADAPT_COUNT_NONE,
  HUD_BLK_ADAPT_COUNT_OVER,
  HUD_BLK_ADAPT_COUNT_SHOWN,
  HUD_BLK_ADAPT_ICON_SHOWN,
  HUD_BLK_ADAPT_ICON_NONE,
  // The drawing. `$80:C5C2` and `$80:C666` are one function in the port and two
  // prices here: the weapon's has an `LDY $20` at `$80:C5C8` the item's does not.
  HUD_BLK_BLANK,
  HUD_BLK_HEALTH,
  HUD_BLK_ICON_WEAPON,
  HUD_BLK_ICON_ITEM,
  // One digit, three ways. A digit that is not zero prints straight away; a zero
  // prints only once something before it has, and pays for the `BIT` that found
  // out; a leading zero blanks the cell.
  HUD_BLK_DIGIT_NONZERO,
  HUD_BLK_DIGIT_LEAD,
  HUD_BLK_DIGIT_BLANK,
  HUD_BLK_DIGITS_END_PRINTED,
  HUD_BLK_DIGITS_END_ZERO,
  // Everything in the two renderers except the digits: fixed, because both loops
  // are counted rather than terminated.
  HUD_BLK_DIGITS8,
  HUD_BLK_DIGITS3,
  HUD_BLOCK_COUNT,
} HudBlock;

typedef struct {
  uint16_t blocks[HUD_BLOCK_COUNT];
} HudWork;

// What `$80:C0A3` and `$80:C139` leave behind. Both are `RTS` and both have two
// exits — the early one when the panel is switched off, which touches nothing
// but A, and the end of the sixth test.
//
// `work` is where the call records what it did, and it must not be NULL: every
// caller of these is a shim, and a shim that did not want the counts would still
// have to give them somewhere to go.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
  HudWork* work;
} HudPanelRegs;

// `$80:C0A3` / `$80:C139` — refresh one player's panel.
//
// `side` is `HUD_SIDE_P1` or `HUD_SIDE_P2`, and `dp` is the caller's direct
// page: every routine under here scratches `$1E`, `$20` and `$22` on it. X, Y
// and carry are inputs as well as outputs, because the early exit returns all
// three untouched and so does any path where the six tests found nothing.
void hud_panel(Wram* w, const Rom* rom, uint16_t dp, uint16_t side,
               HudPanelRegs* io);

// What `$80:C07F` leaves behind, which on the interesting path is not its own:
// it ends `JML $8083AE`, so the queue adder's registers and its carry are what
// come back. On the quiet path — nothing changed — the `LDA $1E7A` that found
// zero is what set N and Z, and everything else is the panel's.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
  HudWork* work;  // as above, and not optional
} HudRefreshRegs;

// `$80:C07F` — refresh whichever panel is this call's turn, and queue the upload
// if anything moved.
//
// `dp` is the caller's direct page, which is where the alternating phase lives.
// `io` carries X, Y and carry in as well as out, for the reason above.
void hud_refresh(Wram* w, const Rom* rom, uint16_t dp, HudRefreshRegs* io);

#endif
