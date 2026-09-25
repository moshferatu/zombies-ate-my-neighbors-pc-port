// Thread bodies: the code the scheduler resumes, between one yield and the
// next.
//
// A body is never called. `thread_spawn` parks its address and the scheduler
// reaches it by `RTL`; after that it runs until it does `JSL thread_yield`,
// and resumes at the instruction after that `JSL` a frame or more later. So a
// body is ported the way the scheduler is (`port/sched.h`): as stretches of
// the ROM over the whole register set, each from an address control arrives
// at to the next place it leaves.
//
// **A body's stretches stop at every call, not only at its yields.** The
// instruction control leaves by is the ROM's, as everywhere in this style, and
// a `JSR` or `JSL` out of a body is one of those instructions. The routine it
// reaches is the harness's business -- most of them are ported routines of
// their own -- and the instruction after it is the next stretch's entry. That
// keeps each stretch free of other routines' stack traffic, so `verify` can
// compare them with no dead-stack allowance, and it means a body never has to
// know whether what it calls is the port's or the ROM's.
//
// The yield itself stops a stretch on `JSL $808353` with A holding the ticks,
// which the scheduler's own port then takes. See `docs/threads.md`, "Thread
// bodies".
//
// Port code: libc only.

#ifndef PORT_BODIES_H
#define PORT_BODIES_H

#include <stdint.h>

#include "assets/rom.h"
#include "port/cpu.h"
#include "port/wram.h"

// `$7E:1B6A`/`$7E:1B6C`, the camera's top-left in world pixels. All three
// bodies here measure from the screen's middle, `+$80` and `+$70`.
#define W_CAMERA_X 0x1b6a
#define W_CAMERA_Y 0x1b6c

// What each run of the ROM's instructions was taken how many times, for the
// harness to price. `fast_data` and `slow_data` are data bytes a run read
// outside its own listing: the level lists in bank `$9F`, which cost what a
// program byte does, and anything that would cost 8 whatever `$420D` says.
typedef struct {
  uint16_t blocks[24];
  uint16_t fast_data;
  uint16_t slow_data;
} BodyWork;

// --- $81:81F6  victims_body ---------------------------------------------------
//
// The neighbours, each one's thread started when the camera comes near and
// stopped when it leaves. The level record's word at `$1E` points at the
// victim list in bank `$9F`, the one `victim_list_parse` reads: twelve-byte
// entries of x, y, a parameter, the gate `$81:81A2` holds against
// `victim_gate` at `$7E:1D50`, and the far address of the thread to start,
// ended by a zero x. The thread's own page holds its state: `$0C` the list,
// `$10` the entry it is on, `$1A`/`$1C` the screen's middle, `$16`/`$18` the
// entry's position.
//
// Two bytes per entry at `$7E:605A` and `$7E:609A`: whether its thread is
// live, and in which slot. Bit 7 of the first retires the entry for good.
//
// Each frame it walks from `$10`, skipping retired entries, until one is
// within `$A0` of the middle on both axes and not yet started -- `JSR $81A2`
// starts it -- or is out of range and live -- it is forgotten, and its thread
// handed to `thread_call_handler` with `Y = $FF` -- or the list ends. Starting
// or stopping one ends the frame's walk; ending the list starts the next from
// the top.

#define VICTIMS_START_PC 0x8181f6u    // spawned: thread_spawn's RTL lands here
#define VICTIMS_RESUME_PC 0x818206u   // after its `JSL thread_yield`
#define VICTIMS_STARTED_PC 0x818263u  // after `JSR $81A2`
#define VICTIMS_STOPPED_PC 0x81828fu  // after `JSL thread_call_handler`
// Where it leaves.
#define VICTIMS_YIELD_PC 0x818202u
#define VICTIMS_START_CALL_PC 0x818260u
#define VICTIMS_STOP_CALL_PC 0x81828bu

#define W_VICTIM_STATE 0x605a
#define W_VICTIM_SLOT 0x609a

enum {
  VICTIMS_START,  // LDA $00 STA $0C PEI $02 PLB
  VICTIMS_STZ,    // STZ $10
  VICTIMS_TICKS,  // LDA #$0003
  VICTIMS_HEAD,   // $8206-$8216: the screen's middle
  VICTIMS_FLAG,   // LDX $10 : LDA $7E605A,X : AND #imm : Bxx
  VICTIMS_INDEX,  // TXA ... TAY : LDA ($0C),Y : BEQ
  VICTIMS_DX,     // STA $16 : SEC : SBC $1A : BPL
  VICTIMS_DY,     // INY INY : LDA ($0C),Y : STA $18 : SEC : SBC $1C : BPL
  VICTIMS_NEG,    // EOR #$FFFF : INC
  VICTIMS_CMP,    // CMP #$00A0 : BCS
  VICTIMS_NEXT,   // INC $10 : BRA
  VICTIMS_NEXT_JMP,  // INC $10 : JMP
  VICTIMS_STOP,   // $8276-$8288: retire the live flag, fetch the slot
  VICTIMS_TAKEN,  // a branch taken
  VICTIMS_BLOCK_COUNT
};

void victims_start(Wram* w, PortCpu* c, BodyWork* k);
void victims_resume(Wram* w, const Rom* rom, PortCpu* c, BodyWork* k);
void victims_started(Wram* w, PortCpu* c, BodyWork* k);
void victims_stopped(Wram* w, PortCpu* c, BodyWork* k);

// --- $80:C8F6  object_spawner_body --------------------------------------------
//
// The level's objects, started when the camera comes near. The list is in
// WRAM, parsed at the thread's start by `$80:C9A5`: x at `$7E:6D02`, y at
// `$7E:6D48`, a type at `$7E:1F0A` and a state word at `$7E:1EC4`, whose bit
// 14 ends the list and bit 15 retires an entry; otherwise it is the actor the
// entry has, or zero. `$0C` is the entry it is on, `$0E`/`$10` the screen's
// middle, and `$12` counts requests the thread's handler, `$80:CAEE`, has
// left for `JSR $CABF` to serve.
//
// It yields a tick at a time, and on every fourth frame of the scheduler's
// clock walks the list from `$0C`: an entry within `$90` of the middle with no
// actor is given one (`JSR $C9E3`), one out of range with an actor has it
// freed (`JSR $CAA8`), and either ends the frame's walk. The end of the list
// starts the next walk from the top.

#define OBJECT_RESUME_PC 0x80c911u   // after its `JSL thread_yield`
#define OBJECT_POLLED_PC 0x80c918u   // after `JSR $CABF`
#define OBJECT_GIVEN_PC 0x80c967u  // after `JSR $C9E3`
#define OBJECT_FREED_PC 0x80c971u  // after `JSR $CAA8`
// Where it leaves.
#define OBJECT_YIELD_PC 0x80c90du
#define OBJECT_POLL_CALL_PC 0x80c915u
#define OBJECT_GIVE_CALL_PC 0x80c964u
#define OBJECT_FREE_CALL_PC 0x80c96eu

#define W_OBJECT_STATE 0x1ec4
#define W_OBJECT_X 0x6d02
#define W_OBJECT_Y 0x6d48

enum {
  OBJECT_POLL,   // LDA $12 : BEQ
  OBJECT_TICK,   // LDA $0020 : AND #$0003 : BNE
  OBJECT_TICKS,  // LDA #$0001
  OBJECT_STZ,    // STZ $0C
  OBJECT_HEAD,   // $C920-$C930: the screen's middle
  OBJECT_SCAN,   // LDX $0C : BIT $1EC4,X : BVS
  OBJECT_LIVE,   // BMI
  OBJECT_D,      // LDA $7E6Dxx,X : SEC : SBC dp : BPL
  OBJECT_NEG,    // EOR #$FFFF : INC
  OBJECT_CMP,    // CMP #$0090 : BCS
  OBJECT_STATE,  // LDA $1EC4,X : BNE/BEQ
  OBJECT_STEP,   // INC $0C : INC $0C : BRA
  OBJECT_BRA,    // BRA $C979, after either call
  OBJECT_TAKEN,  // a branch taken
  OBJECT_BLOCK_COUNT
};

void object_resume(Wram* w, PortCpu* c, BodyWork* k);
void object_polled(Wram* w, PortCpu* c, BodyWork* k);
void object_acted(Wram* w, PortCpu* c, BodyWork* k);

// --- $81:80EC  actor_list_spawn -----------------------------------------------
//
// The level's actor list, one entry a frame. The level record's word at `$1C`
// points at ten-byte entries in bank `$9F`: a rest time in frames (zero ends
// the list), x and y, and more that `$81:807E` reads. `$7E:60DA` holds a byte
// per entry that counts the rest down. `$10` is the entry it is on, `$14` the
// nearest distance this pass and `$12` its entry.
//
// Each frame, unless `$80:9D5B` says not to (carry set), it steps one entry:
// a resting one counts down, a ready one is measured against the nearer
// player by `JSR $8024`. At the end of the list the nearest ready entry, if
// nearer than `$100`, is set resting and started by `JSR $807E`, and the next
// pass starts from the top.

#define ACTORS_CHECKED_PC 0x818113u   // after `JSL $809D5B`
#define ACTORS_MEASURED_PC 0x81814bu  // after `JSR $8024`
#define ACTORS_STARTED_PC 0x81817cu   // after `JSR $807E`
// Where it leaves.
#define ACTORS_YIELD_PC 0x81810bu
#define ACTORS_MEASURE_CALL_PC 0x818148u
#define ACTORS_START_CALL_PC 0x818179u

#define W_ACTORS_COUNT 0x60da

enum {
  ACTORS_CHECK,   // BCS
  ACTORS_TICKS,   // LDA #$0001
  ACTORS_COUNT,   // LDX $10 : LDA $7E60DA,X : AND #$00FF : BEQ
  ACTORS_TICK,    // DEC : SEP : STA $7E60DA,X : REP : BRA
  ACTORS_NEXT,    // INC $10 : BRA
  ACTORS_INDEX,   // LDA $10 ... TAY
  ACTORS_TYPE,    // LDA ($0C),Y : AND #$00FF : BEQ
  ACTORS_XY,      // INY : LDA ($0C),Y : STA $16 : INY : INY : LDA ($0C),Y : STA $18
  ACTORS_BEST,    // CMP $14 : BCS
  ACTORS_NEW_BEST,  // STA $14 : LDA $10 : STA $12
  ACTORS_END,     // LDA $14 : CMP #$0100 : BCS
  ACTORS_RESET,   // LDA #$FFFF : STA $14 : STZ $10
  ACTORS_PICK,    // LDA $12 : TAX ... TAY
  ACTORS_ARM,     // LDA ($0C),Y : AND #$00FF : SEP : STA $7E60DA,X : REP
  ACTORS_BACK,    // BRA $8101
  ACTORS_TAKEN,   // a branch taken
  ACTORS_BLOCK_COUNT
};

void actors_checked(Wram* w, const Rom* rom, PortCpu* c, BodyWork* k);
void actors_measured(Wram* w, PortCpu* c, BodyWork* k);
void actors_started(Wram* w, PortCpu* c, BodyWork* k);

// --- $82:D7CF  level_tile_anim ------------------------------------------------
//
// A level's animated tiles, up to eight of them, started from the level
// record's thread list. Each slot on the thread's page has a sequence pointer
// at `$08,X` (its bank is `$02`'s), a position in it at `$18,X`, frames left
// at `$28,X` and a live flag at `$38,X`; `$48` counts live slots, and the
// thread ends when it reaches zero.
//
// Each frame it walks all eight. A live slot counts down, and when it runs
// out reads the next `(frame, frames)` pair: the frame goes to `$7E:6DE4,X`
// doubled and to `$7E:6DC4,X` as a tile address past `$7E:1E80`, and the
// slot's bit from the table at `$D8CB` goes into `$7E:1F56`. A duration of
// `$FFFF` starts the sequence over; `$FFFE` stops the slot. If any bit is set
// at the end, the upload job at `$82:D88C` is queued on queue A.

#define TILE_ANIM_RESUME_PC 0x82d881u  // after its `JSL thread_yield`
#define TILE_ANIM_QUEUED_PC 0x82d87au  // after `JSL vbl_queue_a_add`
// Where it leaves.
#define TILE_ANIM_QUEUE_CALL_PC 0x82d876u
#define TILE_ANIM_YIELD_PC 0x82d87du
#define TILE_ANIM_END_PC 0x82d885u  // `RTL`: every slot stopped

#define W_TILE_ANIM_BASE 0x1e80
#define W_TILE_ANIM_DIRTY 0x1f56
#define W_TILE_ANIM_FRAME 0x6de4
#define W_TILE_ANIM_TILE 0x6dc4
#define TILE_ANIM_BITS 0x82d8cbu

enum {
  TANIM_HEAD,     // LDA $48 : BNE
  TANIM_START,    // LDX #$FFFE
  TANIM_SLOT,     // INX INX : LDA $38,X : BMI
  TANIM_IDLE,     // CPX #$0010 : BEQ
  TANIM_BRA,      // BRA $D81F
  TANIM_COUNT,    // DEC $28,X : BNE
  TANIM_FRAME,    // $D830-$D857: the next frame, and the next duration
  TANIM_WRAP,     // CMP #$FFFE : BEQ
  TANIM_RESTART,  // LDY #$0000 : LDA [$00],Y
  TANIM_NEXT,     // STA $28,X : INY INY : STY $18,X : BRA
  TANIM_END,      // STZ $38,X : DEC $48 : BRA
  TANIM_DONE,     // LDA $1F56 : BEQ
  TANIM_QUEUE,    // LDA #$D88C : LDY #$0082
  TANIM_TICKS,    // LDA #$0001
  TANIM_TAKEN,    // a branch taken
  TANIM_BLOCK_COUNT
};

void tile_anim_resume(Wram* w, const Rom* rom, PortCpu* c, BodyWork* k);
void tile_anim_queued(Wram* w, PortCpu* c, BodyWork* k);

#endif
