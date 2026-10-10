// The big monster's thread: what it does round the routines it calls.
// The big monster is the giant ant: see `port/monster.h`.
//
// `port/monster.h` has what the creature's thread calls each frame,
// `port/chase.h` its chase and `port/monster_states.h` its other states and
// how one is set up. This is the thread itself, which is four copies
// of one loop, at `$81:C201`, `$81:C28C`, `$81:C321` and `$81:C3B6`:
//
//     its weight on the level's load, and its handler
//     loop:
//       sleep a frame
//       the state's own routine, by the word at `$12`
//       where to go        monster_seek, or monster_deliver
//       its picture        monster_anim
//       while nothing has set `$2A`
//     a killed one drops what it holds, is heard, is counted, and shows
//     its last pictures
//     its weight off the load, its record freed, and the one it held
//
// The calls are the ROM's own instructions, and the harness makes them: each
// is a link in the registry. What is here is the stretches between them,
// each from where a call comes back to the next call.
//
// **How a turn ends** is `monster_turned`. Nothing in `$2A` and it sleeps
// again. Otherwise it is over, and how is by two words. Holding a thing of
// kind `$2A` or `$21`, or told to go with `$2A` negative, it was killed.
// With `$2A` positive, which is `monster_seek` finding both players far
// away, it only leaves.
//
// **The copies differ in four things.** The handler they install, the count
// a killed one adds to, whether waking clears `$20`, and whether a killed
// one's handler is taken away before it is heard. `MONSTER_THREADS` has
// those. The second copy, the one that carries somebody home, is not here:
// no movie reaches it.
//
// A load that would go below nothing stops the ROM on a branch to itself,
// and that is the ROM's to do.
//
// Port code: libc only.

#ifndef PORT_MONSTER_THREAD_H
#define PORT_MONSTER_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include "port/cpu.h"
#include "port/wram.h"

typedef struct {
  uint32_t loaded_pc;  // `CLC`: its weight goes on the load
  uint16_t handler;    // what touches it tells this, in bank `$81`
  uint32_t sleep_pc;   // `LDA #$0001`, and the `JSL thread_yield` after it
  uint32_t woke_pc;    // where that comes back
  bool forgets;        // waking clears `$20`
  uint32_t turned_pc;  // `LDA $2A`, after its picture
  uint32_t heard_pc;   // `LDA #$0021`, a killed one's sound
  bool untouchable;    // ...before which its handler is taken away
  uint32_t end_pc;     // `SEC`: its weight comes off
} MonsterThread;

#define MONSTER_THREADS(X) \
  X(c201, 0x81c201u, 0xc440u, 0x81c215u, 0x81c21cu, true, 0x81c22cu, \
    0x81c25du, true, 0x81c26eu) \
  X(c321, 0x81c332u, 0xc440u, 0x81c346u, 0x81c34du, false, 0x81c35bu, \
    0x81c387u, false, 0x81c398u) \
  X(c3b6, 0x81c3bcu, 0xc4a6u, 0x81c3d0u, 0x81c3d7u, false, 0x81c3e5u, \
    0x81c411u, false, 0x81c422u)

enum {
#define X(at, loaded, handler, sleep, woke, forgets, turned, heard, \
          untouchable, end) \
  MONSTER_THREAD_AT_##at,
  MONSTER_THREADS(X)
#undef X
  MONSTER_THREAD_COUNT
};
extern const MonsterThread MONSTER_THREADS_BY[MONSTER_THREAD_COUNT];

#define MONSTER_THREAD_BANK 0x0081
#define MONSTER_WEIGHT 0x001c
#define MONSTER_KILLED_SFX 0x0021
#define MONSTER_DP_FORGOTTEN 0x20
// Held, either of these kills it.
#define MONSTER_FATAL_KIND_A 0x002a
#define MONSTER_FATAL_KIND_B 0x0021

// Where the rest of a copy is, from the addresses in its row.
static inline uint32_t monster_handler_call_pc(const MonsterThread* t) {
  return t->loaded_pc + 0x10;  // `JSL thread_set_handler`
}
static inline uint32_t monster_sleep_call_pc(const MonsterThread* t) {
  return t->sleep_pc + 3;      // `JSL thread_yield`
}
static inline uint32_t monster_state_pc(const MonsterThread* t) {
  return t->turned_pc - 7;     // `RTS`, to the state's routine
}
static inline uint32_t monster_where_pc(const MonsterThread* t) {
  return t->turned_pc - 6;     // `JSR monster_seek`: the state comes back here
}
static inline uint32_t monster_picture_pc(const MonsterThread* t) {
  return t->turned_pc - 3;     // `JSR monster_anim`
}
static inline uint32_t monster_drop_pc(const MonsterThread* t) {
  return t->turned_pc + 0x1b;  // `JSR $C0E5`: what it holds is put down
}
static inline uint32_t monster_dropped_pc(const MonsterThread* t) {
  return t->turned_pc + 0x1e;
}
static inline uint32_t monster_untouch_pc(const MonsterThread* t) {
  return t->heard_pc - 4;      // `JSL thread_set_handler`, with none
}
static inline uint32_t monster_sound_pc(const MonsterThread* t) {
  return t->heard_pc + 3;      // `JSL apu_play_sfx`
}
static inline uint32_t monster_free_pc(const MonsterThread* t) {
  return t->end_pc + 0x0e;     // `JSL actor_slot_free`, its own record
}
static inline uint32_t monster_freed_pc(const MonsterThread* t) {
  return t->end_pc + 0x12;
}
static inline uint32_t monster_free_held_pc(const MonsterThread* t) {
  return t->end_pc + 0x19;     // ...and the one it held
}
static inline uint32_t monster_over_pc(const MonsterThread* t) {
  return t->end_pc + 0x1d;     // `RTL`, which ends the thread
}

// How a turn came out.
typedef enum {
  MONSTER_GOES_ON,
  MONSTER_HELD_FATAL_A,  // killed: what it holds is of the first kind
  MONSTER_HELD_FATAL_B,  // ...or the second
  MONSTER_TOLD,          // ...or `$2A` is negative
  MONSTER_LEAVES,        // nobody near: it goes quietly
  MONSTER_FATE_COUNT
} MonsterFate;

// Each is one stretch, and leaves `c->pc` on the instruction it stops at.
//
// Its weight on the load, and its handler in A and Y for the call.
void monster_loaded(Wram* w, PortCpu* c, const MonsterThread* t);
// One frame, in A for the call.
void monster_sleeps(PortCpu* c, const MonsterThread* t);
// The state's routine, by an `RTS` to it that comes back to the next call.
void monster_woke(Wram* w, PortCpu* c, const MonsterThread* t);
// `holding` is whether a killed one has something to put down first.
MonsterFate monster_turned(const Wram* w, PortCpu* c, const MonsterThread* t,
                           bool* holding);
// A killed one's record: its pictures' bank, and nobody's collide id, so
// that nothing more touches it.
void monster_dropped(Wram* w, PortCpu* c, const MonsterThread* t);
// Where taking the handler away comes back: its sound, in A for the call.
void monster_untouched(PortCpu* c, const MonsterThread* t);
// False if the load would go below nothing. It only looks, then.
bool monster_ends(Wram* w, PortCpu* c, const MonsterThread* t);
// True if it held a record, which is freed too.
bool monster_freed(const Wram* w, PortCpu* c, const MonsterThread* t);

// `$81:C321` begins by asking whether there is room where it is: `JSR
// $BFA8`, and carry set is no. Then it ends where it stands.
#define MONSTER_C321_ROOM_PC 0x81c324u     // `BCC`
#define MONSTER_C321_NO_ROOM_PC 0x81c326u  // `JMP` to the thread's `RTL`
#define MONSTER_C321_BEGIN_PC 0x81c329u
bool monster_has_room(PortCpu* c);

// The instructions between those stretches that only call, jump or return.
#define MONSTER_LINKS(X) \
  X(c201_where, "$81:C226", 0x81c226u) \
  X(c201_picture, "$81:C229", 0x81c229u) \
  X(c201_over, "$81:C28B", 0x81c28bu) \
  X(c321_room, "$81:C321", 0x81c321u) \
  X(c321_no_room, "$81:C326", 0x81c326u) \
  X(c321_setup, "$81:C329", 0x81c329u) \
  X(c321_place, "$81:C32C", 0x81c32cu) \
  X(c321_state, "$81:C32F", 0x81c32fu) \
  X(c321_where, "$81:C355", 0x81c355u) \
  X(c321_picture, "$81:C358", 0x81c358u) \
  X(c321_over, "$81:C3B5", 0x81c3b5u) \
  X(c3b6_setup, "$81:C3B6", 0x81c3b6u) \
  X(c3b6_place, "$81:C3B9", 0x81c3b9u) \
  X(c3b6_where, "$81:C3DF", 0x81c3dfu) \
  X(c3b6_picture, "$81:C3E2", 0x81c3e2u) \
  X(c3b6_over, "$81:C43F", 0x81c43fu)

#endif
