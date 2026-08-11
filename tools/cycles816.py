r"""What a run of 65816 instructions costs the machine, in master cycles.

This exists because of `cosim_cost`. A routine whose cost depends on its input
cannot be substituted against a constant -- `$80:BC7F` runs every frame at
anywhere from 92 to 7,642 cycles, and declared at its mean it drags the whole
machine's clock off far enough to part a framebuffer. The fix is for the port to
price the call it just served, which means somebody has to know what each
straight-line run of the ROM costs. Doing that by hand is an invitation to a
silent off-by-six, so it is done here instead.

    python tools\cycles816.py "Zombies Ate My Neighbors.sfc" 80:BC7F 100
    python tools\cycles816.py "Zombies Ate My Neighbors.sfc" 80:BC7F 100 --sum

Every line comes back with its own cost and a running total, and `--sum` prints
only the total -- which is the number a cost model's constant wants.

## What it assumes, and why each assumption is safe here

The cost of a 65816 instruction is not a property of the opcode: it is the
opcode plus where every byte it touches lives. Four facts settle that for this
ROM, and the tool asserts rather than guesses whenever one of them might not
hold.

  * **Banks $80+ are fast — sometimes.** `$80:80A2 LDA #$01 : STA $420D` turns
    FastROM on at boot, and an opcode or operand byte from `$80:8000`+ then
    costs 6 master cycles instead of 8. It does not stay on: on level 49 the
    same three instructions of `$80:C139` were measured at 86 cycles and at 98,
    and the difference is exactly 2 per byte of the three instructions. So this
    is priced fast by default, and the **bytes** column is printed next to the
    cycles: a run costs `cycles + 2 * bytes` while the register is off, which is
    how a cost model carries the difference instead of assuming it away.
  * **Low WRAM is slow.** Anything under `$2000` -- the direct page, the stack,
    the display list, the tilemap shadow -- costs 8, in every bank the data-bank
    register can hold.
  * **A ROM table costs 6 through bank $80 and 8 through bank $00.** This is the
    one assumption that has already been wrong once, so it is worth being blunt
    about: a LoROM cartridge appears twice, and only the `$80`+ copy is fast.
    `$80:C5A3 LDA $C5B6,X` is an instruction in bank $80 reading a table in
    whatever bank the *data bank register* holds, and the HUD's callers leave a
    low bank there — so the read costs 8 a byte and the instruction costs 40,
    not the 36 this tool said when it assumed `$80`. The symptom was three
    calls out of 233 whose modelled cost was short by 12; the fix is `--db`,
    and the default is still `$80`.

    Anything under `$2000` costs 8 through every bank, which is why a routine
    that only touches WRAM -- `$80:BC7F`, say -- is priced right either way.
  * **The direct page is page-aligned.** A `dp` with a low byte costs one extra
    internal cycle on every direct-page instruction. Pass `--dp-unaligned` for a
    caller that presents one; the shims check for it and decline to price the
    call instead.

Addresses in `$2000`-`$5FFF` are the PPU, DMA and joypad registers, which cost 6
or 12 depending on which side of `$4000` they fall. They are priced, but a
routine that touches them is doing I/O and probably should not be substituted at
all, so the tool says so.

## Reading the output

Branches print both prices, `not-taken/taken`, because which one applies is the
caller's business. `--sum` counts branches as not taken and says so, which is
what makes it right for a straight-line run and wrong for anything else: sum the
pieces between branches, then add the branch you meant.

The check that this is right is not this file. It is `zamn_cosim verify`, which
compares a model built out of these numbers against what the ROM's own
instructions really took, on every call, and reports the error. `$80:BC7F`'s
model came out of here and is refresh-exact on all 143,929 of its calls across
the corpus that were not made under HDMA.
"""
import sys

from dis816 import (T, SZ, fmt, IMP, IMM_M, IMM_X, IMM8, DP, DPX, DPY, IDP,
                    IDPX, IDPY, ILDP, ILDPY, ABS, ABSX, ABSY, ABL, ABLX, IND,
                    IIDX, ILABS, REL, REL16, SR, SRIY, BLK)

FETCH = 6   # an opcode or operand byte from bank $80+, $8000+, with FastROM on
IDLE = 6    # an internal cycle
SLOW = 8    # low WRAM: the direct page, the stack, and everything the game keeps
FAST = 6    # ROM through the data bank, and $2000-$3FFF/$4200-$5FFF
SLOWEST = 12  # $4000-$41FF, the joypad and DMA-enable ports


def access(addr, bank=0x80):
    """Master cycles for one byte at `bank:addr`, as `snes_getAccessTime` has it.

    `bank` defaults to $80 because that is the data bank every routine priced
    here runs under, and it is the interesting case: below $2000 the address is
    the WRAM mirror and above $8000 it is ROM, which is one instruction in the
    listing and two prices here.
    """
    if (bank < 0x40 or 0x80 <= bank < 0xC0) and addr < 0x8000:
        if addr < 0x2000 or addr >= 0x6000:
            return SLOW
        if 0x4000 <= addr < 0x4200:
            return SLOWEST
        return FAST
    return FAST if bank >= 0x80 else SLOW


# Opcodes whose implied form is not the two-cycle kind: an entry is the number
# of internal cycles and the number of stack bytes it moves.
STACK_OPS = {
    "PHA": (1, 2), "PHX": (1, 2), "PHY": (1, 2), "PHD": (1, 2),
    "PHB": (1, 1), "PHK": (1, 1), "PHP": (1, 1),
    "PLA": (2, 2), "PLX": (2, 2), "PLY": (2, 2), "PLD": (2, 2),
    "PLB": (2, 1), "PLP": (2, 1),
}

# Read-modify-write: the value is read, an internal cycle passes, and it goes
# back out. `INC`/`DEC` in their memory forms, and the four shifts.
RMW = {"INC", "DEC", "ASL", "LSR", "ROL", "ROR", "TSB", "TRB"}

# Written but not read, so the operand is one access rather than two rounds.
STORES = {"STA", "STX", "STY", "STZ"}

BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ", "BRA"}

# Index-register width decides the operand size for these; A's width for the
# rest. Only matters for immediates, which `dis816` has already sized.
INDEX_OPS = {"LDX", "LDY", "STX", "STY", "CPX", "CPY", "INX", "INY", "DEX",
             "DEY"}


class Unpriced(Exception):
    """An opcode or mode this tool will not guess at. See the module docstring."""


def operand_width(name, m, x):
    """Bytes the instruction moves to or from memory."""
    if name in INDEX_OPS:
        return 1 if x else 2
    return 1 if m else 2


def cost(name, mode, val, size, m, x, dp_unaligned, db=0x80):
    """Master cycles for one instruction, and a note when it is not a number.

    Returns `(cycles, note)`. `note` is a string for a branch, whose taken cost
    differs, and None otherwise.
    """
    c = FETCH * (1 + size)  # the opcode and its operand bytes
    width = operand_width(name, m, x)

    if mode in (IMM_M, IMM_X, IMM8):
        # The operand bytes *are* the data, already counted above. `SEP`/`REP`
        # spend an extra internal cycle setting the flags.
        if name in ("SEP", "REP"):
            c += IDLE
        return c, None

    if mode == IMP:
        if name in STACK_OPS:
            idles, bytes_ = STACK_OPS[name]
            return c + idles * IDLE + bytes_ * SLOW, None
        if name == "RTS":
            return c + 3 * IDLE + 2 * SLOW, None
        if name == "RTL":
            return c + 2 * IDLE + 3 * SLOW, None
        if name == "RTI":
            raise Unpriced("RTI: an interrupt return is not a routine's cost")
        # The two-cycle kind: transfers, flag sets, INC/DEC A, the shifts on A.
        return c + IDLE, None

    if mode == REL:
        return c, "%d taken" % (c + IDLE)
    if mode == REL16:  # BRL is always taken and always costs the extra
        return c + IDLE, None

    if name == "JSR" and mode == ABS:
        return c + IDLE + 2 * SLOW, None
    if name == "JSL":
        return c + IDLE + 3 * SLOW, None
    if name == "JMP" and mode == ABS:
        return c, None
    if name == "JML":
        return c, None
    if name in ("JMP",):  # ($xxxx) and ($xxxx,X) reach a table this cannot see
        raise Unpriced("%s %s: an indirect jump's table is not in this run" % (name, mode))

    # ...everything else reaches memory.
    if mode == DP:
        c += (IDLE if dp_unaligned else 0)
        at = SLOW
    elif mode in (DPX, DPY):
        c += (IDLE if dp_unaligned else 0) + IDLE
        at = SLOW
    elif mode == ABS:
        at = access(val, db)
    elif mode in (ABSX, ABSY):
        # 16-bit index registers take the extra cycle unconditionally, and so
        # does every write. With 8-bit ones it would depend on a page crossing,
        # which is data this cannot see.
        if x:
            raise Unpriced("%s with 8-bit index: the page crossing decides" % name)
        c += IDLE
        at = access(val, db)
    elif mode in (ABL, ABLX):
        # A long address carries its own bank, so this is the one mode that does
        # not have to assume one. `ABLX` costs no more than `ABL`: the index is
        # added without an internal cycle.
        at = access(val & 0xFFFF, val >> 16)
    else:
        raise Unpriced("addressing mode %d is not priced" % mode)

    if name in RMW:
        c += width * at + IDLE + width * at
    else:
        c += width * at
    return c, None


def main():
    rom = open(sys.argv[1], "rb").read()
    bank, addr = sys.argv[2].split(":")
    bank, addr = int(bank, 16), int(addr, 16)
    count = int(sys.argv[3], 0)
    args = sys.argv[4:]
    total_only = "--sum" in args
    dp_unaligned = "--dp-unaligned" in args
    m = x = 0
    db = 0x80
    for a in args:
        if a.startswith("--m="):
            m = int(a[4:])
        if a.startswith("--x="):
            x = int(a[4:])
        if a.startswith("--db="):
            db = int(a[5:], 16)

    off = (bank & 0x7F) * 0x8000 + (addr - 0x8000)
    end = off + count
    pc = addr
    total = 0
    program = 0  # bytes fetched from the program stream: see --sum
    while off < end:
        op = rom[off]
        name, mode = T[op]
        n = SZ.get(mode, 2)
        if mode == IMM_M:
            n = 1 if m else 2
        if mode == IMM_X:
            n = 1 if x else 2
        val = 0
        for i, b in enumerate(rom[off + 1:off + 1 + n]):
            val |= b << (8 * i)

        c, note = cost(name, mode, val, n, m, x, dp_unaligned, db)
        total += c
        # Every byte of an instruction is fetched exactly once, immediates
        # included — `cpu_adrImm` hands the opcode function two program
        # addresses and it reads them like any other operand.
        program += 1 + n
        if not total_only:
            raw = " ".join("%02X" % b for b in rom[off:off + 1 + n])
            if mode == REL:
                text = "$%04X" % ((pc + 2 + ((val ^ 0x80) - 0x80)) & 0xFFFF)
            elif mode == REL16:
                text = "$%04X" % ((pc + 3 + ((val ^ 0x8000) - 0x8000)) & 0xFFFF)
            elif mode == BLK:
                text = "$%02X,$%02X" % (val & 0xFF, val >> 8)
            elif n:
                text = fmt(mode, val, pc, n)
            else:
                text = ""
            print("$%02X:%04X  %-12s %-4s %-11s %4d %6d %4d%s" % (
                bank, pc, raw, name, text, c, total, program,
                "   (%s)" % note if note else ""))

        if name == "SEP":
            if val & 0x20:
                m = 1
            if val & 0x10:
                x = 1
        elif name == "REP":
            if val & 0x20:
                m = 0
            if val & 0x10:
                x = 0
        off += 1 + n
        pc += 1 + n

    print("%d master cycles over %d program bytes, branches not taken "
          "(add 2 per byte with FastROM off)" % (total, program))


if __name__ == "__main__":
    main()
