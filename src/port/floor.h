// What the floor does to whoever is standing on it.
//
//   $80:E86D  floor_effect  0.1%  the tile under the player, and its effect
//   $80:F935                --    the harm it starts, inlined; one call site
//
// `port/terrain.h` answers "can something stand here". This asks the opposite
// question about the tile it is already standing on: it reads one attribute
// word through `$80:ADC8 tile_attrs_at_pixel` and, for five particular values,
// does something to the player. Conveyor belts, the harmful floors, and one
// tile that clears a word. Everything else is a floor you just stand on, and
// the routine returns without touching anything.
//
// It is 13.2 instructions a call over 28,088 calls, and `$80:D1FF` -- the
// player's state-zero handler -- opens with `JSR $E86D` before it looks at a
// single button. **Standing on the ground is checked before the controller
// is.**
//
// ## Five attribute words, and the mask that makes them five
//
// `AND #$FF7F` first, so bit 7 is not part of any comparison -- whatever that
// bit marks, it is orthogonal to the floor's effect and the routine drops it
// rather than testing it. Then, in order:
//
//   $4000   harm, unless the player is carrying weapon 3 with `$1E` set
//   $0400   harm, unconditionally
//   $8000   `STZ $2A`, and nothing else
//   bit 3   a conveyor: fall through to the four direction cases
//
// and the four conveyors, which are the low byte plus that same bit 3:
//
//   $0108   one pixel up          $0208   one pixel left
//   $0408   one pixel down        $0028   one pixel right, if the way is clear
//
// **Only the rightward one is checked against terrain.** The other three write
// `$30` or `$32` outright, so a belt can push a player into a wall going up,
// down or left, and cannot going right. There is no comment in the ROM and no
// obvious reason; the port reproduces the asymmetry rather than rounding it
// off, because a guard on the other three would be a difference from the ROM
// that no input can distinguish from a fix.
//
// ## `BIT #$0008` is not `BIT $0008`
//
// The conveyor test is `BIT` in **immediate** mode, and on the 65816 that form
// sets **Z only** -- N and V are left alone, where every other addressing mode
// would load them from bits 15 and 14 of the operand. So the routine's flags on
// its plainest exit, `$80:E88C RTS`, are Z from the `BIT` and N and C from the
// `CMP #$8000` three instructions earlier. Getting that wrong is invisible
// until a caller branches on N.
//
// ## What `$80:F935` starts, and why it is inlined
//
// Eleven instructions, and **one call site in the whole cartridge** -- this one
// -- so it goes in the body rather than the registry, the same call the
// `blockmap` round made about `$80:AD0B`. Its own span is 4,020 instructions
// over 493 calls, and only 19 of those 493 get past the third branch.
//
//     LDA $70 : CMP #$0002 : BEQ out      ; two modes suppress it entirely
//               CMP #$0004 : BEQ out
//     LDA $52 : BPL out                   ; ...and so does the cooldown
//     LDA #$8001 : STA $50
//     LDA #$0020 : STA $52
//
// `$50` and `$52` are a pair, and `$80:D01B` is where they are read back:
// `BIT $50 : BMI` takes the "an effect is running" branch, which walks an
// animation frame table and clears `$50` at the end of it before going on to
// `$1CB8 player_health`; and `DEC $52 : BPL` counts the other one down, parking
// it at `$FFFF` when it expires. So `$52` negative means *idle*, which is why
// the `BPL` here is a guard rather than a test -- **the floor can only hurt you
// again once the last one has finished** -- and `$0020` is that cooldown.
//
// The port does not name `$70`. Two of its values switch the whole thing off
// and the ROM does not say which two states they are.
//
// ## Direct page, and why it is the caller's
//
// There is no `PHD` anywhere in either routine, so every one of these is a
// field of the **player thread's own page** and not the absolute WRAM address
// the symbol file would give it. `$0E`, `$1E`, `$30` and `$32` in particular
// look like `vbl_queue_*` and `apu_seq` at face value and are nothing of the
// kind -- the same trap the thread bodies sprang, one file over. `$1CBC` is the
// exception and is genuinely absolute: `LDA $1CBC,X` is a 16-bit operand.
//
// No `PHD` also means no `PLD` to take N and Z from, so **the flags come from
// whichever comparison the routine stopped at** -- eleven exits, and the port
// tracks them one at a time.
//
// Port code: libc only.

#ifndef PORT_FLOOR_H
#define PORT_FLOOR_H

#include <stdbool.h>
#include <stdint.h>

#include "assets/rom.h"
#include "port/wram.h"

#define FLOOR_EFFECT_ENTRY 0x80e86du

// Direct page -- the player thread's, handed in.
#define FLOOR_DP_PLAYER 0x0eu    // player index, already doubled
#define FLOOR_DP_GUARD 0x1eu     // `$80:D1FF` clears this on entry every frame
#define FLOOR_DP_CLEARED 0x2au   // what the $8000 tile zeroes
#define FLOOR_DP_X 0x30u         // the player's position, and what a belt moves
#define FLOOR_DP_Y 0x32u
#define FLOOR_DP_STATE 0x50u     // bit 15 set = an effect is running
#define FLOOR_DP_COOLDOWN 0x52u  // counted down by $80:D023; negative = idle
#define FLOOR_DP_MODE 0x70u      // two of its values suppress the harm

// `AND #$FF7F` -- bit 7 is not part of any comparison below.
#define FLOOR_ATTR_MASK 0xff7fu

#define FLOOR_ATTR_HARM_GATED 0x4000u  // ...unless weapon 3 with the guard set
#define FLOOR_ATTR_HARM 0x0400u
#define FLOOR_ATTR_CLEAR 0x8000u
#define FLOOR_ATTR_BELT 0x0008u  // `BIT #$0008` -- immediate, so Z only

#define FLOOR_ATTR_BELT_UP 0x0108u
#define FLOOR_ATTR_BELT_DOWN 0x0408u
#define FLOOR_ATTR_BELT_LEFT 0x0208u
#define FLOOR_ATTR_BELT_RIGHT 0x0028u  // the only one tested against terrain

#define FLOOR_WEAPON_GATE 0x0003u  // the `player_weapon` that reads `$1E`

// $80:F935, inlined.
#define FLOOR_MODE_OFF_A 0x0002u
#define FLOOR_MODE_OFF_B 0x0004u
#define FLOOR_STATE_START 0x8001u
#define FLOOR_COOLDOWN_TICKS 0x0020u

typedef enum {
  FLOOR_TILE_PLAIN,
  FLOOR_TILE_BELT,
  FLOOR_TILE_CLEAR,
  FLOOR_TILE_HARM,
  FLOOR_TILE_GATED,
} FloorTile;

// The gated tile: what the player holds decides whether it harms.
typedef enum {
  FLOOR_GATE_NONE,
  FLOOR_GATE_OTHER_WEAPON,
  FLOOR_GATE_OPEN,
  FLOOR_GATE_SHUT,
} FloorGate;

typedef enum {
  FLOOR_HARM_NONE,
  FLOOR_HARM_OFF_A,    // the player's state switches it off...
  FLOOR_HARM_OFF_B,    // ...either of two
  FLOOR_HARM_COOLING,  // the last hurt has not run out
  FLOOR_HARM_START,
} FloorHarm;

typedef enum {
  FLOOR_BELT_UNASKED,
  FLOOR_BELT_UP,
  FLOOR_BELT_DOWN,
  FLOOR_BELT_LEFT,
  FLOOR_BELT_RIGHT,
  FLOOR_BELT_RIGHT_BLOCKED,
  FLOOR_BELT_NOT,  // asked, and it was none of the four
} FloorBelt;

// A, X and Y all differ by exit. X starts as the player's own X, becomes the
// player index on the `$4000` path (`LDX $0E`), and is `terrain_blocked`'s row
// on the rightward belt; Y is the player's Y except on that same path. Carry
// arrives clear from `tile_attrs_at_pixel` and only the rightward belt and the
// `CMP`s change it.
typedef struct {
  uint16_t a, x, y;
  bool n, z, c;
  // For the harness, and only for it: what the tile was and what came of it,
  // which is what it takes to price the ROM's instructions.
  FloorTile tile;
  FloorGate gate;
  FloorHarm harm;
  FloorBelt belt;
  int probes;  // the rightward belt's ground test: how many of its six ran
} FloorRegs;

// `$80:E86D` -- read the tile under the player and apply what it does.
void floor_effect(Wram* w, const Rom* rom, uint16_t dp, FloorRegs* out);

#endif  // PORT_FLOOR_H
