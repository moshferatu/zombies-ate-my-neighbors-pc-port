// A display record begun: three routines a thread's start calls.
//
//   $81:8000  record_begin     a record at the place on the thread's page
//   $81:87BE  zombie_begin     ...and a zombie's made of it
//   $83:A13E  neighbour_begin  a neighbour's record, and their handler
//
// **`record_begin`** takes a free record and puts it at the place in the
// page's first two words, on the ground, with nobody's collide id and this
// thread as its owner. It keeps the record at `$08`. Fourteen kinds of thing
// in bank `$81` begin with it.
//
// **`zombie_begin`** calls that and goes on: the place kept at `$16` and
// `$18`, the first picture of the two kinds of zombie, the record to be
// drawn, and the page's own words for its walk cleared.
//
// **`neighbour_begin`** is the same for the eleven kinds of neighbour, with
// the picture and its bank handed in. A neighbour's collide id is 1, their
// place is kept at `$20` and `$24`, and their handler is `$83:A364`, which
// is how whatever touches them tells their thread.
//
// None of the three tests whether a record was free. With none free the ROM
// goes on with what is no record, and that is the ROM's to do.
//
// ## And ended
//
// **A thing's thread ends the same way whatever it was.** It takes what it
// added to the level's load off again, loads its record, and jumps to
// `actor_slot_free`, whose return ends the thread. Seven instructions, a
// copy for each kind of thing. `record_end` is those and the free they jump
// to, as far as `thread_exit`, where the free's `RTL` goes.
// A load that would go below nothing stops the ROM where it stands, on a
// branch to itself, and that is the ROM's to do.
//
// **A thing a player killed pays first.** Most kinds have the same two
// stretches before that end, a copy each. The first gives whoever killed it
// what it was worth: `LDX #worth : LDA $who : BEQ`, and a `JSL score_add`.
// Killed by nobody, it goes past both. The second is where that call comes
// back: the level's count of that kind killed goes up by one, and its last
// pictures are played, by `JSL pictures_play`. `kill_scored` and
// `kill_counted` are those, as far as each `JSL`.
//
//   $81:83A3  death_pictures   a killed thing's last pictures begun
//
// **A killed thing is heard, and can no longer be touched.** Its thread
// calls this with a list of pictures in A, their bank in Y, and in X the
// word that says a player killed it. It plays a sound, takes the record's
// collide id away, and goes on to `pictures_play`. With anything else in X
// it does nothing at all.
//
// It is two stretches, one either side of the sound. A sound waits until
// the sound chip has taken the last one, and how long that is is not the
// port's to say. So the first stretch ends at the `JSL` that plays it, with
// the list and the bank on the stack, and the second begins where that
// comes back and ends at the `JSL pictures_play`.
//
// Port code: libc only.

#ifndef PORT_BEGIN_H
#define PORT_BEGIN_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/oam.h"
#include "port/wram.h"

#define RECORD_BEGIN_PC 0x818000u
#define RECORD_BEGIN_RTL_PC 0x818023u
#define ZOMBIE_BEGIN_PC 0x8187beu
#define ZOMBIE_BEGIN_RTS_PC 0x8187f7u
#define NEIGHBOUR_BEGIN_PC 0x83a13eu
#define NEIGHBOUR_BEGIN_RTS_PC 0x83a18eu

#define BEGIN_DP_PLACE_X 0x00
#define BEGIN_DP_PLACE_Y 0x02
#define BEGIN_DP_RECORD 0x08

#define ZOMBIE_BEGIN_PICTURE 0xc98au
#define ZOMBIE_BEGIN_META_BANK 0x0090
#define ZOMBIE_BEGIN_ATTR 0x0c00

#define NEIGHBOUR_HANDLER 0xa364u
#define NEIGHBOUR_HANDLER_BANK 0x0083
#define NEIGHBOUR_COLLIDE_ID 0x0001

typedef struct {
  bool declined;    // no record free
  uint16_t record;
} BeginWork;

// The record the search for a free one begins at. Passing over one in use,
// the search subtracts, which leaves overflow clear; taking this one, it
// leaves overflow as it found it.
#define RECORD_SEARCHED_FIRST 0x1acau

void record_begin(Wram* w, PortCpu* c, BeginWork* k);
void zombie_begin(Wram* w, PortCpu* c, BeginWork* k);
void neighbour_begin(Wram* w, PortCpu* c, BeginWork* k);

// A thread's end: where its seven instructions are, what it gives back,
// and which word of its page is its record.
typedef struct {
  uint32_t pc;       // `SEC`
  uint32_t free_pc;  // `JML actor_slot_free`, the record in A
  uint16_t load;
  uint8_t record_at;
  bool calls;        // ...or a `JSL` to it and an `RTL` of its own after
} RecordEnd;

// Thirty-two more of them, named for where they are. The port has not
// read the threads they end, so it does not say what they are: only that
// each is the same seven instructions, with what it gives back and where
// it keeps its record. Three call the free and have an `RTL` of their own.
//
// Sixteen more in the ROM are not here. Eight call the free and then free
// a second record: `$81:98BF`, `$81:B326`, `$81:C26E`, `$81:C303`,
// `$81:C398`, `$81:C422`, `$81:E59A` and `$83:9F68`. Three of those are the
// giant ant's, and `port/ant_thread.h` has them. And eight are inside
// a port that does them itself, the axe's, the flame's, the swipe's, the
// knock's, the bubble's, the bolt's, the lob's and the squirt's. An entry
// inside another port's stretch hides that port's exit from the check.
#define RECORD_ENDS_BY_ADDRESS(X) \
  X(81a523, "$81:A523", 0x81a523u, 0x001c, 0x08, true) \
  X(81c8b0, "$81:C8B0", 0x81c8b0u, 0x000a, 0x08, false) \
  X(81d747, "$81:D747", 0x81d747u, 0x0018, 0x08, false) \
  X(81e507, "$81:E507", 0x81e507u, 0x0015, 0x08, false) \
  X(81eb34, "$81:EB34", 0x81eb34u, 0x0004, 0x0a, false) \
  X(81f012, "$81:F012", 0x81f012u, 0x0004, 0x0a, false) \
  X(81f1ed, "$81:F1ED", 0x81f1edu, 0x0008, 0x0a, false) \
  X(81f5c7, "$81:F5C7", 0x81f5c7u, 0x0002, 0x0a, false) \
  X(81fb67, "$81:FB67", 0x81fb67u, 0x0002, 0x0a, false) \
  X(829969, "$82:9969", 0x829969u, 0x0012, 0x08, false) \
  X(82dd1a, "$82:DD1A", 0x82dd1au, 0x0005, 0x08, false) \
  X(82ef78, "$82:EF78", 0x82ef78u, 0x0014, 0x08, true) \
  X(82f06d, "$82:F06D", 0x82f06du, 0x0008, 0x08, false) \
  X(82f22f, "$82:F22F", 0x82f22fu, 0x0005, 0x08, false) \
  X(82f2ca, "$82:F2CA", 0x82f2cau, 0x0008, 0x08, false) \
  X(82f4cd, "$82:F4CD", 0x82f4cdu, 0x000a, 0x08, true) \
  X(82f767, "$82:F767", 0x82f767u, 0x0005, 0x08, false) \
  X(839700, "$83:9700", 0x839700u, 0x000e, 0x08, false) \
  X(8397dd, "$83:97DD", 0x8397ddu, 0x0006, 0x08, false) \
  X(839919, "$83:9919", 0x839919u, 0x000a, 0x08, false) \
  X(839a23, "$83:9A23", 0x839a23u, 0x000e, 0x08, false) \
  X(839b77, "$83:9B77", 0x839b77u, 0x000e, 0x08, false) \
  X(839c3f, "$83:9C3F", 0x839c3fu, 0x000a, 0x08, false) \
  X(839cce, "$83:9CCE", 0x839cceu, 0x000a, 0x08, false) \
  X(839dc9, "$83:9DC9", 0x839dc9u, 0x000e, 0x08, false) \
  X(839e7c, "$83:9E7C", 0x839e7cu, 0x000a, 0x08, false) \
  X(83a054, "$83:A054", 0x83a054u, 0x000e, 0x08, false) \
  X(83a0d6, "$83:A0D6", 0x83a0d6u, 0x000e, 0x08, false) \
  X(83b22d, "$83:B22D", 0x83b22du, 0x0007, 0x08, false) \
  X(83b2bd, "$83:B2BD", 0x83b2bdu, 0x000f, 0x08, false) \
  X(83b84b, "$83:B84B", 0x83b84bu, 0x0010, 0x08, false) \
  X(83caa1, "$83:CAA1", 0x83caa1u, 0x0008, 0x08, false)

enum {
  RECORD_END_ZOMBIE_SLOW,
  RECORD_END_ZOMBIE_FAST,
  RECORD_END_ZOMBIE_THIRD,
  RECORD_END_SLIME_GLOB,
  RECORD_END_SHOT_5,
  RECORD_END_MARTIAN,          // the loop of one that began on the ground
  RECORD_END_MARTIAN_ARRIVAL,  // ...and of one that came in over the top
  RECORD_END_FISHMAN_SPLASH,   // the splash a fishman's leap leaves
  // Each of these is where its thread's loop goes on a fate that is not
  // nothing, past what a killed one shows first.
  RECORD_END_WEREWOLF,
  RECORD_END_SLIME,
  RECORD_END_WEED,
#define X(at, sym, pc, load, record_at, calls) RECORD_END_AT_##at,
  RECORD_ENDS_BY_ADDRESS(X)
#undef X
  RECORD_END_COUNT
};
extern const RecordEnd RECORD_ENDS[RECORD_END_COUNT];

// Where a thread's last `RTL` goes: `thread_exit`.
#define RECORD_END_EXITED_PC 0x00833eu

// False if the load would go below nothing, if its record is not one the
// display list holds, or if the stack is not as a thread's is begun. It only
// looks, then. `place` is where in the list the record was, which is how
// far the free walks.
bool record_end(Wram* w, PortCpu* c, const RecordEnd* end, int* place);

// A killed thing's two stretches before its end. Named for where they are,
// as the ends are. The doll's are `port/doll.h`'s, and the flame's
// `port/flame.h`'s.
typedef struct {
  uint32_t pc;         // `LDX #worth`
  uint16_t worth;      // as `score_add` takes it
  uint8_t killer_at;   // the page's word for who killed it: nobody is zero
  uint32_t nobody_pc;  // where the `BEQ` goes
} KillScored;
#define KILL_SCORED_CALL_BYTES 7  // from the `LDX` to the `JSL score_add`

typedef struct {
  uint32_t pc;        // `INC $count`
  uint16_t count_at;  // how many of its kind have been killed
  uint16_t pictures;  // its last
} KillCounted;
#define KILL_COUNTED_CALL_BYTES 6  // ...and to the `JSL pictures_play`

#define KILLS_SCORED(X) \
  X(818ed5, "$81:8ED5", 0x818ed5u, 0x0020, 0x30, 0x818ef0u) \
  X(818f55, "$81:8F55", 0x818f55u, 0x0020, 0x30, 0x818f70u) \
  X(819a02, "$81:9A02", 0x819a02u, 0x0200, 0x30, 0x819a17u) \
  X(819a66, "$81:9A66", 0x819a66u, 0x0200, 0x30, 0x819a7bu) \
  X(81a4ea, "$81:A4EA", 0x81a4eau, 0x1000, 0x28, 0x81a523u) \
  X(81ac3d, "$81:AC3D", 0x81ac3du, 0x0300, 0x3e, 0x81ac67u) \
  X(81bbeb, "$81:BBEB", 0x81bbebu, 0x0300, 0x20, 0x81bc02u) \
  X(81d2b3, "$81:D2B3", 0x81d2b3u, 0x0200, 0x36, 0x81d2ceu) \
  X(81d732, "$81:D732", 0x81d732u, 0x0050, 0x20, 0x81d747u) \
  X(81e4d4, "$81:E4D4", 0x81e4d4u, 0x0200, 0x22, 0x81e507u) \
  X(81e567, "$81:E567", 0x81e567u, 0x0200, 0x22, 0x81e59au) \
  X(8295ac, "$82:95AC", 0x8295acu, 0x2000, 0x42, 0x8295b7u) \
  X(82993f, "$82:993F", 0x82993fu, 0x0200, 0x20, 0x829969u) \
  X(82a8f3, "$82:A8F3", 0x82a8f3u, 0x2000, 0x4e, 0x82a8feu) \
  X(82ef5b, "$82:EF5B", 0x82ef5bu, 0x2000, 0x34, 0x82ef78u) \
  X(83ad7a, "$83:AD7A", 0x83ad7au, 0x2000, 0x5a, 0x83ad85u) \
  X(83b2a8, "$83:B2A8", 0x83b2a8u, 0x0050, 0x24, 0x83b2bdu) \
  X(83b824, "$83:B824", 0x83b824u, 0x0010, 0x1c, 0x83b84bu) \
  X(83c6d5, "$83:C6D5", 0x83c6d5u, 0x2000, 0x52, 0x83c6e0u)

#define KILLS_COUNTED(X) \
  X(819a0d, "$81:9A0D", 0x819a0du, 0x1f72u, 0x9a2au) \
  X(819a71, "$81:9A71", 0x819a71u, 0x1f72u, 0x9a8eu) \
  X(81c264, "$81:C264", 0x81c264u, 0x1f7au, 0xb9d9u) \
  X(81c2f9, "$81:C2F9", 0x81c2f9u, 0x1f7au, 0xb9d9u) \
  X(81c38e, "$81:C38E", 0x81c38eu, 0x1f7au, 0xb9d9u) \
  X(81c418, "$81:C418", 0x81c418u, 0x1f7cu, 0xb9d9u) \
  X(81d2be, "$81:D2BE", 0x81d2beu, 0x1f84u, 0xd2e1u) \
  X(81d73d, "$81:D73D", 0x81d73du, 0x1f74u, 0xd75au) \
  X(83b2b3, "$83:B2B3", 0x83b2b3u, 0x1f78u, 0xb332u)

enum {
#define X(at, sym, pc, worth, killer_at, nobody_pc) KILL_SCORED_AT_##at,
  KILLS_SCORED(X)
#undef X
  KILL_SCORED_COUNT
};
enum {
#define X(at, sym, pc, count_at, pictures) KILL_COUNTED_AT_##at,
  KILLS_COUNTED(X)
#undef X
  KILL_COUNTED_COUNT
};
extern const KillScored KILLS_SCORED_BY[KILL_SCORED_COUNT];
extern const KillCounted KILLS_COUNTED_BY[KILL_COUNTED_COUNT];

// True when a player killed it, and it leaves at the `JSL score_add`.
bool kill_scored(const Wram* w, PortCpu* c, const KillScored* kill);
void kill_counted(Wram* w, PortCpu* c, const KillCounted* kill);

#define DEATH_PICTURES_PC 0x8183a3u
#define DEATH_PICTURES_SOUND_PC 0x8183adu  // `JSL apu_play_sfx`
#define DEATH_PICTURES_HEARD_PC 0x8183b1u
#define DEATH_PICTURES_PLAY_PC 0x8183bfu   // `JSL pictures_play`
#define DEATH_KILLED 0xf5f5u               // in X: a player killed it
#define DEATH_SFX 0x0021
// False for anything else in X, which is the ROM's.
bool death_pictures(Wram* w, PortCpu* c);
void death_pictures_heard(Wram* w, PortCpu* c);

#endif
