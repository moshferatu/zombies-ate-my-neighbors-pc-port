#include "w65816.h"

#include <stdio.h>

#define I(m, a, f) {m, AM_##a, FLOW_##f}

const OpInfo w65816_ops[256] = {
    // 0x00
    I("BRK", IMM8, TRAP), I("ORA", IDX, NONE),  I("COP", IMM8, TRAP),
    I("ORA", SR, NONE),   I("TSB", DP, NONE),   I("ORA", DP, NONE),
    I("ASL", DP, NONE),   I("ORA", IDL, NONE),  I("PHP", IMP, NONE),
    I("ORA", IMM_M, NONE), I("ASL", ACC, NONE), I("PHD", IMP, NONE),
    I("TSB", ABS, NONE),  I("ORA", ABS, NONE),  I("ASL", ABS, NONE),
    I("ORA", ABL, NONE),
    // 0x10
    I("BPL", REL, BRANCH), I("ORA", IDY, NONE), I("ORA", IDP, NONE),
    I("ORA", ISY, NONE),  I("TRB", DP, NONE),   I("ORA", DPX, NONE),
    I("ASL", DPX, NONE),  I("ORA", IDLY, NONE), I("CLC", IMP, NONE),
    I("ORA", ABY, NONE),  I("INC", ACC, NONE),  I("TCS", IMP, NONE),
    I("TRB", ABS, NONE),  I("ORA", ABX, NONE),  I("ASL", ABX, NONE),
    I("ORA", ALX, NONE),
    // 0x20
    I("JSR", ABS, CALL),  I("AND", IDX, NONE),  I("JSL", ABL, CALL),
    I("AND", SR, NONE),   I("BIT", DP, NONE),   I("AND", DP, NONE),
    I("ROL", DP, NONE),   I("AND", IDL, NONE),  I("PLP", IMP, NONE),
    I("AND", IMM_M, NONE), I("ROL", ACC, NONE), I("PLD", IMP, NONE),
    I("BIT", ABS, NONE),  I("AND", ABS, NONE),  I("ROL", ABS, NONE),
    I("AND", ABL, NONE),
    // 0x30
    I("BMI", REL, BRANCH), I("AND", IDY, NONE), I("AND", IDP, NONE),
    I("AND", ISY, NONE),  I("BIT", DPX, NONE),  I("AND", DPX, NONE),
    I("ROL", DPX, NONE),  I("AND", IDLY, NONE), I("SEC", IMP, NONE),
    I("AND", ABY, NONE),  I("DEC", ACC, NONE),  I("TSC", IMP, NONE),
    I("BIT", ABX, NONE),  I("AND", ABX, NONE),  I("ROL", ABX, NONE),
    I("AND", ALX, NONE),
    // 0x40
    I("RTI", IMP, RET),   I("EOR", IDX, NONE),  I("WDM", IMM8, NONE),
    I("EOR", SR, NONE),   I("MVP", BM, NONE),   I("EOR", DP, NONE),
    I("LSR", DP, NONE),   I("EOR", IDL, NONE),  I("PHA", IMP, NONE),
    I("EOR", IMM_M, NONE), I("LSR", ACC, NONE), I("PHK", IMP, NONE),
    I("JMP", ABS, JUMP),  I("EOR", ABS, NONE),  I("LSR", ABS, NONE),
    I("EOR", ABL, NONE),
    // 0x50
    I("BVC", REL, BRANCH), I("EOR", IDY, NONE), I("EOR", IDP, NONE),
    I("EOR", ISY, NONE),  I("MVN", BM, NONE),   I("EOR", DPX, NONE),
    I("LSR", DPX, NONE),  I("EOR", IDLY, NONE), I("CLI", IMP, NONE),
    I("EOR", ABY, NONE),  I("PHY", IMP, NONE),  I("TCD", IMP, NONE),
    I("JML", ABL, JUMP),  I("EOR", ABX, NONE),  I("LSR", ABX, NONE),
    I("EOR", ALX, NONE),
    // 0x60
    I("RTS", IMP, RET),   I("ADC", IDX, NONE),  I("PER", RELL, NONE),
    I("ADC", SR, NONE),   I("STZ", DP, NONE),   I("ADC", DP, NONE),
    I("ROR", DP, NONE),   I("ADC", IDL, NONE),  I("PLA", IMP, NONE),
    I("ADC", IMM_M, NONE), I("ROR", ACC, NONE), I("RTL", IMP, RET),
    I("JMP", IND, JUMP),  I("ADC", ABS, NONE),  I("ROR", ABS, NONE),
    I("ADC", ABL, NONE),
    // 0x70
    I("BVS", REL, BRANCH), I("ADC", IDY, NONE), I("ADC", IDP, NONE),
    I("ADC", ISY, NONE),  I("STZ", DPX, NONE),  I("ADC", DPX, NONE),
    I("ROR", DPX, NONE),  I("ADC", IDLY, NONE), I("SEI", IMP, NONE),
    I("ADC", ABY, NONE),  I("PLY", IMP, NONE),  I("TDC", IMP, NONE),
    I("JMP", IAX, JUMP),  I("ADC", ABX, NONE),  I("ROR", ABX, NONE),
    I("ADC", ALX, NONE),
    // 0x80
    I("BRA", REL, JUMP),  I("STA", IDX, NONE),  I("BRL", RELL, JUMP),
    I("STA", SR, NONE),   I("STY", DP, NONE),   I("STA", DP, NONE),
    I("STX", DP, NONE),   I("STA", IDL, NONE),  I("DEY", IMP, NONE),
    I("BIT", IMM_M, NONE), I("TXA", IMP, NONE), I("PHB", IMP, NONE),
    I("STY", ABS, NONE),  I("STA", ABS, NONE),  I("STX", ABS, NONE),
    I("STA", ABL, NONE),
    // 0x90
    I("BCC", REL, BRANCH), I("STA", IDY, NONE), I("STA", IDP, NONE),
    I("STA", ISY, NONE),  I("STY", DPX, NONE),  I("STA", DPX, NONE),
    I("STX", DPY, NONE),  I("STA", IDLY, NONE), I("TYA", IMP, NONE),
    I("STA", ABY, NONE),  I("TXS", IMP, NONE),  I("TXY", IMP, NONE),
    I("STZ", ABS, NONE),  I("STA", ABX, NONE),  I("STZ", ABX, NONE),
    I("STA", ALX, NONE),
    // 0xa0
    I("LDY", IMM_X, NONE), I("LDA", IDX, NONE), I("LDX", IMM_X, NONE),
    I("LDA", SR, NONE),   I("LDY", DP, NONE),   I("LDA", DP, NONE),
    I("LDX", DP, NONE),   I("LDA", IDL, NONE),  I("TAY", IMP, NONE),
    I("LDA", IMM_M, NONE), I("TAX", IMP, NONE), I("PLB", IMP, NONE),
    I("LDY", ABS, NONE),  I("LDA", ABS, NONE),  I("LDX", ABS, NONE),
    I("LDA", ABL, NONE),
    // 0xb0
    I("BCS", REL, BRANCH), I("LDA", IDY, NONE), I("LDA", IDP, NONE),
    I("LDA", ISY, NONE),  I("LDY", DPX, NONE),  I("LDA", DPX, NONE),
    I("LDX", DPY, NONE),  I("LDA", IDLY, NONE), I("CLV", IMP, NONE),
    I("LDA", ABY, NONE),  I("TSX", IMP, NONE),  I("TYX", IMP, NONE),
    I("LDY", ABX, NONE),  I("LDA", ABX, NONE),  I("LDX", ABY, NONE),
    I("LDA", ALX, NONE),
    // 0xc0
    I("CPY", IMM_X, NONE), I("CMP", IDX, NONE), I("REP", IMM8, NONE),
    I("CMP", SR, NONE),   I("CPY", DP, NONE),   I("CMP", DP, NONE),
    I("DEC", DP, NONE),   I("CMP", IDL, NONE),  I("INY", IMP, NONE),
    I("CMP", IMM_M, NONE), I("DEX", IMP, NONE), I("WAI", IMP, TRAP),
    I("CPY", ABS, NONE),  I("CMP", ABS, NONE),  I("DEC", ABS, NONE),
    I("CMP", ABL, NONE),
    // 0xd0
    I("BNE", REL, BRANCH), I("CMP", IDY, NONE), I("CMP", IDP, NONE),
    I("CMP", ISY, NONE),  I("PEI", DP, NONE),   I("CMP", DPX, NONE),
    I("DEC", DPX, NONE),  I("CMP", IDLY, NONE), I("CLD", IMP, NONE),
    I("CMP", ABY, NONE),  I("PHX", IMP, NONE),  I("STP", IMP, TRAP),
    I("JML", IAL, JUMP),  I("CMP", ABX, NONE),  I("DEC", ABX, NONE),
    I("CMP", ALX, NONE),
    // 0xe0
    I("CPX", IMM_X, NONE), I("SBC", IDX, NONE), I("SEP", IMM8, NONE),
    I("SBC", SR, NONE),   I("CPX", DP, NONE),   I("SBC", DP, NONE),
    I("INC", DP, NONE),   I("SBC", IDL, NONE),  I("INX", IMP, NONE),
    I("SBC", IMM_M, NONE), I("NOP", IMP, NONE), I("XBA", IMP, NONE),
    I("CPX", ABS, NONE),  I("SBC", ABS, NONE),  I("INC", ABS, NONE),
    I("SBC", ABL, NONE),
    // 0xf0
    I("BEQ", REL, BRANCH), I("SBC", IDY, NONE), I("SBC", IDP, NONE),
    I("SBC", ISY, NONE),  I("PEA", PEA, NONE),  I("SBC", DPX, NONE),
    I("INC", DPX, NONE),  I("SBC", IDLY, NONE), I("SED", IMP, NONE),
    I("SBC", ABY, NONE),  I("PLX", IMP, NONE),  I("XCE", IMP, NONE),
    I("JSR", IAX, CALL),  I("SBC", ABX, NONE),  I("INC", ABX, NONE),
    I("SBC", ALX, NONE),
};

#undef I

// Operand size in bytes, excluding the opcode.
static int mode_operand_len(uint8_t mode, bool mf, bool xf) {
  switch (mode) {
    case AM_IMP:
    case AM_ACC:
      return 0;
    case AM_IMM_M:
      return mf ? 1 : 2;
    case AM_IMM_X:
      return xf ? 1 : 2;
    case AM_IMM8:
    case AM_DP:
    case AM_DPX:
    case AM_DPY:
    case AM_IDP:
    case AM_IDX:
    case AM_IDY:
    case AM_IDL:
    case AM_IDLY:
    case AM_REL:
    case AM_SR:
    case AM_ISY:
      return 1;
    case AM_ABS:
    case AM_ABX:
    case AM_ABY:
    case AM_IND:
    case AM_IAX:
    case AM_IAL:
    case AM_RELL:
    case AM_BM:
    case AM_PEA:
      return 2;
    case AM_ABL:
    case AM_ALX:
      return 3;
    default:
      return 0;
  }
}

int w65816_len(uint8_t opcode, bool mf, bool xf) {
  return 1 + mode_operand_len(w65816_ops[opcode].mode, mf, xf);
}

void w65816_operand(char* out, size_t out_size, const uint8_t* b, uint32_t pc,
                    bool mf, bool xf) {
  const OpInfo* op = &w65816_ops[b[0]];
  int len = 1 + mode_operand_len(op->mode, mf, xf);
  uint32_t lo = b[1];
  uint32_t word = (uint32_t)b[1] | ((uint32_t)b[2] << 8);
  uint32_t lng = word | ((uint32_t)b[3] << 16);

  switch (op->mode) {
    case AM_IMP:   snprintf(out, out_size, "%s", ""); break;
    case AM_ACC:   snprintf(out, out_size, "A"); break;
    case AM_IMM_M:
    case AM_IMM_X:
      if (len == 2) snprintf(out, out_size, "#$%02X", (unsigned)lo);
      else          snprintf(out, out_size, "#$%04X", (unsigned)word);
      break;
    case AM_IMM8:  snprintf(out, out_size, "#$%02X", (unsigned)lo); break;
    case AM_DP:    snprintf(out, out_size, "$%02X", (unsigned)lo); break;
    case AM_DPX:   snprintf(out, out_size, "$%02X,X", (unsigned)lo); break;
    case AM_DPY:   snprintf(out, out_size, "$%02X,Y", (unsigned)lo); break;
    case AM_IDP:   snprintf(out, out_size, "($%02X)", (unsigned)lo); break;
    case AM_IDX:   snprintf(out, out_size, "($%02X,X)", (unsigned)lo); break;
    case AM_IDY:   snprintf(out, out_size, "($%02X),Y", (unsigned)lo); break;
    case AM_IDL:   snprintf(out, out_size, "[$%02X]", (unsigned)lo); break;
    case AM_IDLY:  snprintf(out, out_size, "[$%02X],Y", (unsigned)lo); break;
    case AM_ABS:   snprintf(out, out_size, "$%04X", (unsigned)word); break;
    case AM_ABX:   snprintf(out, out_size, "$%04X,X", (unsigned)word); break;
    case AM_ABY:   snprintf(out, out_size, "$%04X,Y", (unsigned)word); break;
    case AM_ABL:   snprintf(out, out_size, "$%06X", (unsigned)lng); break;
    case AM_ALX:   snprintf(out, out_size, "$%06X,X", (unsigned)lng); break;
    case AM_IND:   snprintf(out, out_size, "($%04X)", (unsigned)word); break;
    case AM_IAX:   snprintf(out, out_size, "($%04X,X)", (unsigned)word); break;
    case AM_IAL:   snprintf(out, out_size, "[$%04X]", (unsigned)word); break;
    case AM_PEA:   snprintf(out, out_size, "$%04X", (unsigned)word); break;
    case AM_SR:    snprintf(out, out_size, "$%02X,S", (unsigned)lo); break;
    case AM_ISY:   snprintf(out, out_size, "($%02X,S),Y", (unsigned)lo); break;
    case AM_BM:    snprintf(out, out_size, "$%02X,$%02X", b[1], b[2]); break;
    case AM_REL: {
      uint16_t dest = (uint16_t)(pc + 2 + (int8_t)b[1]);
      snprintf(out, out_size, "$%02X%04X", (unsigned)(pc >> 16), dest);
      break;
    }
    case AM_RELL: {
      uint16_t dest = (uint16_t)(pc + 3 + (int16_t)word);
      snprintf(out, out_size, "$%02X%04X", (unsigned)(pc >> 16), dest);
      break;
    }
    default:
      snprintf(out, out_size, "?");
      break;
  }
}

int w65816_disasm(char* out, size_t out_size, const uint8_t* b, uint32_t pc,
                  bool mf, bool xf) {
  char operand[40];
  w65816_operand(operand, sizeof operand, b, pc, mf, xf);
  const OpInfo* op = &w65816_ops[b[0]];
  if (operand[0] == '\0') snprintf(out, out_size, "%s", op->mnem);
  else                    snprintf(out, out_size, "%-4s%s", op->mnem, operand);
  return 1 + mode_operand_len(op->mode, mf, xf);
}

bool w65816_static_target(const uint8_t* b, uint32_t pc, uint32_t* out_target) {
  const OpInfo* op = &w65816_ops[b[0]];
  uint32_t bank = pc & 0xff0000;
  uint32_t word = (uint32_t)b[1] | ((uint32_t)b[2] << 8);
  switch (op->mode) {
    case AM_ABS:  // JSR abs / JMP abs — stays in the current program bank
      if (op->flow != FLOW_CALL && op->flow != FLOW_JUMP) return false;
      *out_target = bank | word;
      return true;
    case AM_ABL:  // JSL long / JML long
      *out_target = word | ((uint32_t)b[3] << 16);
      return true;
    case AM_REL:
      *out_target = bank | (uint16_t)(pc + 2 + (int8_t)b[1]);
      return true;
    case AM_RELL:
      if (op->flow != FLOW_JUMP) return false;  // PER is not a branch
      *out_target = bank | (uint16_t)(pc + 3 + (int16_t)word);
      return true;
    default:
      return false;  // indirect jump or not a control transfer
  }
}
