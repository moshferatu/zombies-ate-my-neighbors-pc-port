r"""Decode 65816 from the ROM image, at an address the listing gave up on.

`analysis/bank_8*.asm` is produced by a tracer, so it only contains what the
game was observed to execute. Everything else is emitted as `.db`, and five
times now a fact this project needed has been inside one of those runs:

  * `$80:A70A` and `$80:A816`, two of the four camera scroll routines, are
    `.db` from end to end -- each is its partner byte-for-byte with six
    substitutions, which is the whole reason `port/camera.c` has one X scroller
    and one Y scroller rather than four of anything;
  * `$80:AB8D  LDA #$4000 : TSB $26`, the only writer of the bit that freezes
    the camera, which four banks of grepping the listing said did not exist;
  * `$82:8014` and `$82:8069`, both routines in `port/bossbg.h`, 1.1% of the
    corpus between them and not one byte of either ever disassembled;
  * `$80:B379` and `$80:B3F1`, the alignment test and the coordinate snap that
    sit between two routines the listing does show;
  * `$82:8F93`, `port/boss.h`'s mover -- 0.4% of the corpus, 2.0% of level 25,
    and the top callable row in the ranking when it was found.

**A grep of the disassembly is a lower bound on the ROM, and in these banks it
is not a tight one.** This is how to go to the bytes:

    python tools\dis816.py "Zombies Ate My Neighbors.sfc" 82:8069 0x80

The optional fourth and fifth arguments are the initial `M` and `X` widths, 0
for 16-bit (the default, which is how the game spends nearly all of its time)
and 1 for 8-bit. `SEP` and `REP` are tracked from there, so a run that starts
in one width and switches decodes correctly without being told twice.

There is no control flow analysis and no symbol table: it decodes forwards from
where it is pointed for as many bytes as it is given, which is exactly what is
wanted when the question is "what is actually here". Data will decode as
nonsense instructions, and the giveaway is the same as always -- a sensible
routine ends at an `RTS`, `RTL` or `RTI`.
"""
import sys


# name, mode
IMP, IMM_M, IMM_X, IMM8, DP, DPX, DPY, IDP, IDPX, IDPY, ILDP, ILDPY, \
    ABS, ABSX, ABSY, ABL, ABLX, IND, IIDX, ILABS, REL, REL16, SR, SRIY, \
    BLK = range(25)

T = {
0x00:("BRK",IMM8),0x01:("ORA",IDPX),0x02:("COP",IMM8),0x03:("ORA",SR),
0x04:("TSB",DP),0x05:("ORA",DP),0x06:("ASL",DP),0x07:("ORA",ILDP),
0x08:("PHP",IMP),0x09:("ORA",IMM_M),0x0A:("ASL",IMP),0x0B:("PHD",IMP),
0x0C:("TSB",ABS),0x0D:("ORA",ABS),0x0E:("ASL",ABS),0x0F:("ORA",ABL),
0x10:("BPL",REL),0x11:("ORA",IDPY),0x12:("ORA",IDP),0x13:("ORA",SRIY),
0x14:("TRB",DP),0x15:("ORA",DPX),0x16:("ASL",DPX),0x17:("ORA",ILDPY),
0x18:("CLC",IMP),0x19:("ORA",ABSY),0x1A:("INC",IMP),0x1B:("TCS",IMP),
0x1C:("TRB",ABS),0x1D:("ORA",ABSX),0x1E:("ASL",ABSX),0x1F:("ORA",ABLX),
0x20:("JSR",ABS),0x21:("AND",IDPX),0x22:("JSL",ABL),0x23:("AND",SR),
0x24:("BIT",DP),0x25:("AND",DP),0x26:("ROL",DP),0x27:("AND",ILDP),
0x28:("PLP",IMP),0x29:("AND",IMM_M),0x2A:("ROL",IMP),0x2B:("PLD",IMP),
0x2C:("BIT",ABS),0x2D:("AND",ABS),0x2E:("ROL",ABS),0x2F:("AND",ABL),
0x30:("BMI",REL),0x31:("AND",IDPY),0x32:("AND",IDP),0x33:("AND",SRIY),
0x34:("BIT",DPX),0x35:("AND",DPX),0x36:("ROL",DPX),0x37:("AND",ILDPY),
0x38:("SEC",IMP),0x39:("AND",ABSY),0x3A:("DEC",IMP),0x3B:("TSC",IMP),
0x3C:("BIT",ABSX),0x3D:("AND",ABSX),0x3E:("ROL",ABSX),0x3F:("AND",ABLX),
0x40:("RTI",IMP),0x41:("EOR",IDPX),0x42:("WDM",IMM8),0x43:("EOR",SR),
0x44:("MVP",BLK),0x45:("EOR",DP),0x46:("LSR",DP),0x47:("EOR",ILDP),
0x48:("PHA",IMP),0x49:("EOR",IMM_M),0x4A:("LSR",IMP),0x4B:("PHK",IMP),
0x4C:("JMP",ABS),0x4D:("EOR",ABS),0x4E:("LSR",ABS),0x4F:("EOR",ABL),
0x50:("BVC",REL),0x51:("EOR",IDPY),0x52:("EOR",IDP),0x53:("EOR",SRIY),
0x54:("MVN",BLK),0x55:("EOR",DPX),0x56:("LSR",DPX),0x57:("EOR",ILDPY),
0x58:("CLI",IMP),0x59:("EOR",ABSY),0x5A:("PHY",IMP),0x5B:("TCD",IMP),
0x5C:("JML",ABL),0x5D:("EOR",ABSX),0x5E:("LSR",ABSX),0x5F:("EOR",ABLX),
0x60:("RTS",IMP),0x61:("ADC",IDPX),0x62:("PER",REL16),0x63:("ADC",SR),
0x64:("STZ",DP),0x65:("ADC",DP),0x66:("ROR",DP),0x67:("ADC",ILDP),
0x68:("PLA",IMP),0x69:("ADC",IMM_M),0x6A:("ROR",IMP),0x6B:("RTL",IMP),
0x6C:("JMP",IND),0x6D:("ADC",ABS),0x6E:("ROR",ABS),0x6F:("ADC",ABL),
0x70:("BVS",REL),0x71:("ADC",IDPY),0x72:("ADC",IDP),0x73:("ADC",SRIY),
0x74:("STZ",DPX),0x75:("ADC",DPX),0x76:("ROR",DPX),0x77:("ADC",ILDPY),
0x78:("SEI",IMP),0x79:("ADC",ABSY),0x7A:("PLY",IMP),0x7B:("TDC",IMP),
0x7C:("JMP",IIDX),0x7D:("ADC",ABSX),0x7E:("ROR",ABSX),0x7F:("ADC",ABLX),
0x80:("BRA",REL),0x81:("STA",IDPX),0x82:("BRL",REL16),0x83:("STA",SR),
0x84:("STY",DP),0x85:("STA",DP),0x86:("STX",DP),0x87:("STA",ILDP),
0x88:("DEY",IMP),0x89:("BIT",IMM_M),0x8A:("TXA",IMP),0x8B:("PHB",IMP),
0x8C:("STY",ABS),0x8D:("STA",ABS),0x8E:("STX",ABS),0x8F:("STA",ABL),
0x90:("BCC",REL),0x91:("STA",IDPY),0x92:("STA",IDP),0x93:("STA",SRIY),
0x94:("STY",DPX),0x95:("STA",DPX),0x96:("STX",DPY),0x97:("STA",ILDPY),
0x98:("TYA",IMP),0x99:("STA",ABSY),0x9A:("TXS",IMP),0x9B:("TXY",IMP),
0x9C:("STZ",ABS),0x9D:("STA",ABSX),0x9E:("STZ",ABSX),0x9F:("STA",ABLX),
0xA0:("LDY",IMM_X),0xA1:("LDA",IDPX),0xA2:("LDX",IMM_X),0xA3:("LDA",SR),
0xA4:("LDY",DP),0xA5:("LDA",DP),0xA6:("LDX",DP),0xA7:("LDA",ILDP),
0xA8:("TAY",IMP),0xA9:("LDA",IMM_M),0xAA:("TAX",IMP),0xAB:("PLB",IMP),
0xAC:("LDY",ABS),0xAD:("LDA",ABS),0xAE:("LDX",ABS),0xAF:("LDA",ABL),
0xB0:("BCS",REL),0xB1:("LDA",IDPY),0xB2:("LDA",IDP),0xB3:("LDA",SRIY),
0xB4:("LDY",DPX),0xB5:("LDA",DPX),0xB6:("LDX",DPY),0xB7:("LDA",ILDPY),
0xB8:("CLV",IMP),0xB9:("LDA",ABSY),0xBA:("TSX",IMP),0xBB:("TYX",IMP),
0xBC:("LDY",ABSX),0xBD:("LDA",ABSX),0xBE:("LDX",ABSY),0xBF:("LDA",ABLX),
0xC0:("CPY",IMM_X),0xC1:("CMP",IDPX),0xC2:("REP",IMM8),0xC3:("CMP",SR),
0xC4:("CPY",DP),0xC5:("CMP",DP),0xC6:("DEC",DP),0xC7:("CMP",ILDP),
0xC8:("INY",IMP),0xC9:("CMP",IMM_M),0xCA:("DEX",IMP),0xCB:("WAI",IMP),
0xCC:("CPY",ABS),0xCD:("CMP",ABS),0xCE:("DEC",ABS),0xCF:("CMP",ABL),
0xD0:("BNE",REL),0xD1:("CMP",IDPY),0xD2:("CMP",IDP),0xD3:("CMP",SRIY),
0xD4:("PEI",DP),0xD5:("CMP",DPX),0xD6:("DEC",DPX),0xD7:("CMP",ILDPY),
0xD8:("CLD",IMP),0xD9:("CMP",ABSY),0xDA:("PHX",IMP),0xDB:("STP",IMP),
0xDC:("JML",IND),0xDD:("CMP",ABSX),0xDE:("DEC",ABSX),0xDF:("CMP",ABLX),
0xE0:("CPX",IMM_X),0xE1:("SBC",IDPX),0xE2:("SEP",IMM8),0xE3:("SBC",SR),
0xE4:("CPX",DP),0xE5:("SBC",DP),0xE6:("INC",DP),0xE7:("SBC",ILDP),
0xE8:("INX",IMP),0xE9:("SBC",IMM_M),0xEA:("NOP",IMP),0xEB:("XBA",IMP),
0xEC:("CPX",ABS),0xED:("SBC",ABS),0xEE:("INC",ABS),0xEF:("SBC",ABL),
0xF0:("BEQ",REL),0xF1:("SBC",IDPY),0xF2:("SBC",IDP),0xF3:("SBC",SRIY),
0xF4:("PEA",ABS),0xF5:("SBC",DPX),0xF6:("INC",DPX),0xF7:("SBC",ILDPY),
0xF8:("SED",IMP),0xF9:("SBC",ABSY),0xFA:("PLX",IMP),0xFB:("XCE",IMP),
0xFC:("JSR",IIDX),0xFD:("SBC",ABSX),0xFE:("INC",ABSX),0xFF:("SBC",ABLX),
}

SZ = {IMP:0, IMM8:1, DP:1, DPX:1, DPY:1, IDP:1, IDPX:1, IDPY:1, ILDP:1,
      ILDPY:1, SR:1, SRIY:1, REL:1, ABS:2, ABSX:2, ABSY:2, IND:2, IIDX:2,
      ILABS:2, REL16:2, BLK:2, ABL:3, ABLX:3}


def fmt(mode, val, pc, n):
    if mode == IMP:
        return ""
    if mode in (IMM8,):
        return "#$%02X" % val
    if mode in (IMM_M, IMM_X):
        return ("#$%04X" % val) if n == 2 else ("#$%02X" % val)
    return {
        DP: "$%02X", DPX: "$%02X,X", DPY: "$%02X,Y", IDP: "($%02X)",
        IDPX: "($%02X,X)", IDPY: "($%02X),Y", ILDP: "[$%02X]",
        ILDPY: "[$%02X],Y", SR: "$%02X,S", SRIY: "($%02X,S),Y",
        ABS: "$%04X", ABSX: "$%04X,X", ABSY: "$%04X,Y", IND: "($%04X)",
        IIDX: "($%04X,X)", ABL: "$%06X", ABLX: "$%06X,X",
        BLK: "$%04X",
    }[mode] % val


def main():
    rom = open(sys.argv[1], "rb").read()
    bank, addr = sys.argv[2].split(":")
    bank, addr = int(bank, 16), int(addr, 16)
    count = int(sys.argv[3], 0)
    m = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    x = int(sys.argv[5]) if len(sys.argv) > 5 else 0
    off = (bank & 0x7F) * 0x8000 + (addr - 0x8000)
    end = off + count
    pc = addr
    while off < end:
        op = rom[off]
        name, mode = T[op]
        n = SZ.get(mode, 2)
        if mode == IMM_M:
            n = 1 if m else 2
        if mode == IMM_X:
            n = 1 if x else 2
        raw = rom[off:off + 1 + n]
        val = 0
        for i, b in enumerate(rom[off + 1:off + 1 + n]):
            val |= b << (8 * i)
        if mode == REL:
            t = (pc + 2 + ((val ^ 0x80) - 0x80)) & 0xFFFF
            text = "$%04X" % t
        elif mode == REL16:
            t = (pc + 3 + ((val ^ 0x8000) - 0x8000)) & 0xFFFF
            text = "$%04X" % t
        elif mode == BLK:
            text = "$%02X,$%02X" % (val & 0xFF, val >> 8)
        else:
            text = fmt(mode, val, pc, n)
        print("$%02X:%04X  %-12s %s %s" % (
            bank, pc, " ".join("%02X" % b for b in raw), name, text))
        if name == "SEP":
            if val & 0x20: m = 1
            if val & 0x10: x = 1
        elif name == "REP":
            if val & 0x20: m = 0
            if val & 0x10: x = 0
        off += 1 + n
        pc += 1 + n


main()
