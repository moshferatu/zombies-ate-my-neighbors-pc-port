// 65816 instruction table: mnemonics, addressing modes, lengths, disassembly.
//
// Shared by the tracer (which needs instruction lengths to mark up a Code/Data
// Log) and the disassembler (which needs the full formatting). Keeping one
// table means the two tools can never disagree about what an opcode is.
//
// Note that on the 65816 an instruction's length is not a property of the
// opcode alone: immediate operands are 8 or 16 bits depending on the M and X
// status flags. Every entry point here therefore takes the flag state.

#ifndef W65816_H
#define W65816_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  AM_IMP,    // implied                      1
  AM_ACC,    // accumulator (ASL A)          1
  AM_IMM_M,  // #const, width from M flag    2/3
  AM_IMM_X,  // #const, width from X flag    2/3
  AM_IMM8,   // #const, always 8-bit         2
  AM_DP,     // dp                           2
  AM_DPX,    // dp,X                         2
  AM_DPY,    // dp,Y                         2
  AM_IDP,    // (dp)                         2
  AM_IDX,    // (dp,X)                       2
  AM_IDY,    // (dp),Y                       2
  AM_IDL,    // [dp]                         2
  AM_IDLY,   // [dp],Y                       2
  AM_ABS,    // abs                          3
  AM_ABX,    // abs,X                        3
  AM_ABY,    // abs,Y                        3
  AM_ABL,    // long                         4
  AM_ALX,    // long,X                       4
  AM_IND,    // (abs)                        3
  AM_IAX,    // (abs,X)                      3
  AM_IAL,    // [abs]                        3
  AM_REL,    // rel8                         2
  AM_RELL,   // rel16                        3
  AM_SR,     // sr,S                         2
  AM_ISY,    // (sr,S),Y                     2
  AM_BM,     // block move (MVN/MVP)         3
  AM_PEA,    // #abs (PEA pushes a literal)  3
} AddrMode;

// How an instruction affects control flow. The tracer uses this to decide when
// the post-execution PC is a branch/call target worth recording.
typedef enum {
  FLOW_NONE = 0,
  FLOW_BRANCH,  // conditional relative branch
  FLOW_JUMP,    // unconditional jump (JMP/JML/BRA/BRL)
  FLOW_CALL,    // JSR / JSL
  FLOW_RET,     // RTS / RTL / RTI
  FLOW_TRAP,    // BRK / COP / STP / WAI
} FlowKind;

typedef struct {
  const char* mnem;
  uint8_t mode;  // AddrMode
  uint8_t flow;  // FlowKind
} OpInfo;

extern const OpInfo w65816_ops[256];

// Total instruction length in bytes, including the opcode.
// mf/xf are the M and X status flags at the time of the fetch (true = 8-bit).
int w65816_len(uint8_t opcode, bool mf, bool xf);

// Operand text only (no mnemonic), e.g. "$1234,X" or "#$30".
// `bytes` points at the opcode; `pc` is the full 24-bit address of the opcode,
// needed to resolve relative branch targets.
void w65816_operand(char* out, size_t out_size, const uint8_t* bytes,
                    uint32_t pc, bool mf, bool xf);

// Full "MNEM operand" text. Returns the instruction length.
int w65816_disasm(char* out, size_t out_size, const uint8_t* bytes,
                  uint32_t pc, bool mf, bool xf);

// For a static control-flow instruction, compute the target address from the
// operand bytes. Returns false for indirect jumps and returns, whose target is
// only knowable at run time. `pc` is the address of the opcode.
bool w65816_static_target(const uint8_t* bytes, uint32_t pc, uint32_t* out_target);

#endif
