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

`MVN`/`MVP` print the price of **one byte moved**, for the same reason and with
the note to say so: the count is A+1 and A is not in the listing. `--sum` counts
one. The three program bytes are re-fetched per byte as well, so both columns
want the same multiplier.

The running total is a running total, not a block price. Subtracting the figure
at the end of one block from the figure at the end of another is only the cost of
what lies between them *in the listing* — which is not the same thing as what
lies between them in execution, and getting that wrong is what charged every
LZSS match for a literal it never ran.

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


def fast_rom(addr, bank=0x80):
    """Is this byte one of the ones `$420D` makes cheap?

    The same question `access` answers with a number, asked so the byte can be
    counted. A ROM byte reached through bank $80+ costs 6 with FastROM on and 8
    with it off, exactly like an opcode fetch -- so it belongs in the byte
    column, which is what a cost model multiplies by 2 when the register is
    clear. `$2000`-`$5FFF` is also 6 and is *not* this: those are registers, and
    they cost 6 whatever `$420D` says.
    """
    if (bank < 0x40 or 0x80 <= bank < 0xC0) and addr < 0x8000:
        return False
    return bank >= 0x80


# Opcodes whose implied form is not the two-cycle kind: an entry is the number
# of internal cycles and the number of stack bytes it moves.
# Idles spent, and bytes moved to or from the stack. `None` for the width means
# "ask `operand_width`": `PHA`/`PLA` follow `m` and `PHX`/`PHY`/`PLX`/`PLY`
# follow `x`, exactly as `cpu.c` does — `case 0x48` pushes one byte when
# `cpu->mf` is set and two when it is clear. Hardcoding two made every 8-bit
# push 8 master cycles too dear, which is half of the 6-cycle error the
# dispatcher's frame first showed up as: `$80:848F` and `$80:8496` are both
# `PHA` under `SEP #$20`.
STACK_OPS = {
    "PHA": (1, None), "PHX": (1, None), "PHY": (1, None), "PHD": (1, 2),
    "PHB": (1, 1), "PHK": (1, 1), "PHP": (1, 1),
    "PLA": (2, None), "PLX": (2, None), "PLY": (2, None), "PLD": (2, 2),
    "PLB": (2, 1), "PLP": (2, 1),
}

# The implied instructions that touch no memory and still are not two cycles.
# Everything else in that shape -- the transfers, the flag sets, `INC A`, the
# shifts on A -- spends exactly one internal cycle, so this is the exception
# list rather than a table. `XBA` is the only entry the ROM uses, at `$80:BA2A`
# in the frame cache's miss path, and it is here because the default cost it
# six master cycles too little: `cpu.c` runs it as two `cpu_idle` calls with no
# `cpu_adrImp` in front, which is the 3-cycle instruction the manual describes.
IMP_IDLES = {"XBA": 2, "WAI": 2, "STP": 2}

# Read-modify-write: the value is read, an internal cycle passes, and it goes
# back out. `INC`/`DEC` in their memory forms, and the four shifts.
RMW = {"INC", "DEC", "ASL", "LSR", "ROL", "ROR", "TSB", "TRB"}

# Written but not read, so the operand is one access rather than two rounds.
STORES = {"STA", "STX", "STY", "STZ"}

BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ", "BRA"}

# The modes whose effective address is built from D, and which therefore cost
# one extra internal cycle whenever D's low byte is non-zero. `--dp-unaligned`
# prices a run that way; the count is reported separately as well, because a
# routine reached through `$80:84A2  TCD` runs on a page the *caller* picks and
# a model of it has to carry both numbers. Stack-relative is not on the list:
# its address is built from S, which has no such penalty.
DP_MODES = {DP, DPX, DPY, IDP, IDPX, IDPY, ILDP, ILDPY}

# Index-register width decides the operand size for these; A's width for the
# rest. Only matters for immediates, which `dis816` has already sized -- and for
# the four index pushes, which is what `STACK_OPS` means by `None`.
#
# `PHX`/`PHY`/`PLX`/`PLY` are on this list because `cpu.c` sizes them from
# `cpu->xf`, and leaving them off it silently sized them from `m` instead. That
# is only wrong where the two widths differ, which is why it survived: the game
# runs 16-bit almost everywhere. `$80:CDAE  PHX` and `$80:CDB4  PLX` are inside
# `SEP #$20 ... REP #$20` in the LZSS match loop, 8-bit A and 16-bit X, and they
# were each priced 8 master cycles light -- 16 per byte of every match the
# decompressor expands, which is 27,376 cycles on level 1's first stream alone.
INDEX_OPS = {"LDX", "LDY", "STX", "STY", "CPX", "CPY", "INX", "INY", "DEX",
             "DEY", "PHX", "PHY", "PLX", "PLY"}


class Unpriced(Exception):
    """An opcode or mode this tool will not guess at. See the module docstring."""


def operand_width(name, m, x):
    """Bytes the instruction moves to or from memory."""
    if name in INDEX_OPS:
        return 1 if x else 2
    return 1 if m else 2


def cost(name, mode, val, size, m, x, dp_unaligned, db=0x80, ind=None,
         bank=0x80):
    """Master cycles for one instruction, its note, and its FastROM-sensitive bytes.

    Returns `(cycles, note, fast)`. `note` is a string for a branch, whose taken
    cost differs, and None otherwise. `fast` counts the *data* bytes this
    instruction reaches at 6 cycles through a bank $80+ ROM address -- a ROM
    table read, or a `[dp]` pointer into one -- because those cost 2 more apiece
    while `$420D` is clear, exactly as its opcode bytes do. Adding them to the
    instruction's own length is what makes the byte column the number a
    `CosimRun` wants.
    """
    c = FETCH * (1 + size)  # the opcode and its operand bytes
    width = operand_width(name, m, x)

    if mode in (IMM_M, IMM_X, IMM8):
        # The operand bytes *are* the data, already counted above. `SEP`/`REP`
        # spend an extra internal cycle setting the flags.
        if name in ("SEP", "REP"):
            c += IDLE
        return c, None, 0

    if mode == IMP:
        if name in STACK_OPS:
            idles, bytes_ = STACK_OPS[name]
            if bytes_ is None:
                bytes_ = width
            return c + idles * IDLE + bytes_ * SLOW, None, 0
        if name == "RTS":
            return c + 3 * IDLE + 2 * SLOW, None, 0
        if name == "RTL":
            return c + 2 * IDLE + 3 * SLOW, None, 0
        if name == "RTI":
            raise Unpriced("RTI: an interrupt return is not a routine's cost")
        if name in IMP_IDLES:
            return c + IMP_IDLES[name] * IDLE, None, 0
        # The two-cycle kind: transfers, flag sets, INC/DEC A, the shifts on A.
        return c + IDLE, None, 0

    if mode == REL:
        return c, "%d taken" % (c + IDLE), 0
    if mode == REL16:  # BRL is always taken and always costs the extra
        return c + IDLE, None, 0

    # The three that push something instead of reading it. Without these `PEA`
    # decodes as an absolute operand and gets priced as a *load* of the address
    # it is pushing — 24 cycles instead of 34, at `$80:8490` a read of `$84A4`
    # that the hardware never performs.
    if name == "PEA":
        # `cpu_readOpcodeWord` then `cpu_pushWord`, and no idle between them.
        return c + 2 * SLOW, None, 0
    if name == "PER":
        return c + IDLE + 2 * SLOW, None, 0
    if name == "PEI":
        # ...and this one really does read, out of the direct page, before it
        # pushes.
        return c + (IDLE if dp_unaligned else 0) + 2 * SLOW + 2 * SLOW, None, 0

    if name == "JSR" and mode == ABS:
        return c + IDLE + 2 * SLOW, None, 0
    if name == "JSR" and mode == IIDX:
        # `JSR ($BDEA,X)`, the drawing pass's four-way dispatch on the actor's
        # flip bits. The vector is read out of the *program* bank, so unlike
        # `[dp]` there is nothing to ask the caller about: two more bytes at
        # whatever bank $80 costs, which is why they are counted as program.
        return (c + 2 * SLOW + IDLE + 2 * access(val, bank), None,
                2 * fast_rom(val, bank))
    if name == "JSL":
        return c + IDLE + 3 * SLOW, None, 0
    if name == "JMP" and mode == ABS:
        return c, None, 0
    if name == "JML":
        return c, None, 0
    if name in ("JMP",):  # ($xxxx) and ($xxxx,X) reach a table this cannot see
        raise Unpriced("%s %s: an indirect jump's table is not in this run" % (name, mode))

    if mode == BLK:
        # `MVN $7E,$7E`, the block move. One instruction that is paid for once
        # per byte: `cpu.c` moves a single byte and then rewinds PC by three
        # rather than looping inside the opcode, so every byte re-fetches all
        # three program bytes and then reads one, writes one and spends two
        # internal cycles.
        #
        # The count is A+1, and A is not in this listing, so what comes back is
        # the price of **one byte** -- the note says so, and a model multiplies
        # it. `--sum` therefore undercounts a run containing one by exactly the
        # count, which is the same shape of wrongness as its branches and is
        # documented in the same place.
        dest, src = val & 0xFF, (val >> 8) & 0xFF
        for b in (dest, src):
            if b < 0x40 or 0x80 <= b < 0xC0:
                # Banks where the price depends on where inside them X and Y
                # are, and X and Y are not here either. Every block move in this
                # ROM is $7E to $7E, where it does not.
                raise Unpriced("MVN through bank $%02X: the address decides" % b)
        # The three program bytes are re-read per byte moved, so they are
        # FastROM-sensitive once per byte as well. `main` credits them to the
        # program column once, which is right for a listing and wrong for a
        # model, so the note carries the multiplier for both columns.
        return (c + access(0, src) + access(0, dest) + 2 * IDLE,
                "x A+1 bytes moved, 3 program bytes each", 0)

    # ...everything else reaches memory.
    fast = False  # is the operand a FastROM byte? see the docstring
    if mode == DP:
        c += (IDLE if dp_unaligned else 0)
        at = SLOW
    elif mode in (DPX, DPY):
        c += (IDLE if dp_unaligned else 0) + IDLE
        at = SLOW
    elif mode == ABS:
        at = access(val, db)
        fast = fast_rom(val, db)
    elif mode in (ABSX, ABSY):
        # 16-bit index registers take the extra cycle unconditionally, and so
        # does every write. With 8-bit ones it would depend on a page crossing,
        # which is data this cannot see.
        if x:
            raise Unpriced("%s with 8-bit index: the page crossing decides" % name)
        c += IDLE
        at = access(val, db)
        fast = fast_rom(val, db)
    elif mode in (ABL, ABLX):
        # A long address carries its own bank, so this is the one mode that does
        # not have to assume one. `ABLX` costs no more than `ABL`: the index is
        # added without an internal cycle.
        at = access(val & 0xFFFF, val >> 16)
        fast = fast_rom(val & 0xFFFF, val >> 16)
    elif mode == SR:
        # `ADC $01,S` -- reaching past the return address a `JSL` just pushed to
        # the argument the caller left under it. `cpu_adrSr` reads the one-byte
        # offset, spends an internal cycle adding it to S, and lands in bank 0,
        # where the stack always is. So there is nothing to ask the caller about
        # and nothing to look up: the address is low WRAM by construction, which
        # is why this is `SLOW` outright rather than an `access` call.
        #
        # No direct-page penalty, and that is the point of the mode: the address
        # is built from S. See `DP_MODES`.
        c += IDLE
        at = SLOW
    elif mode in (ILDP, ILDPY):
        # `LDA [$8A]` and `LDA [$8A],Y`: three bytes of pointer read out of the
        # direct page — always low WRAM, always 8 apiece — and then the operand,
        # wherever the pointer says. `cpu_adrIly` adds no idle for the index, so
        # the two modes cost the same.
        #
        # Where the pointer *points* is runtime data this cannot see, and the
        # difference between a metasprite in bank $8F and something in WRAM is 2
        # cycles a byte, so it is asked for rather than assumed. `$80:BA51`
        # reaches metasprites, and `$80:BD91`/`$80:BD97` are the two tests that
        # guarantee `--ind=8F:8000` describes every pointer that gets that far.
        if ind is None:
            raise Unpriced("%s [dp]: pass --ind=BANK:ADDR for where it points" % name)
        c += (IDLE if dp_unaligned else 0) + 3 * SLOW
        at = access(ind & 0xFFFF, ind >> 16)
        fast = fast_rom(ind & 0xFFFF, ind >> 16)
    else:
        raise Unpriced("addressing mode %d is not priced" % mode)

    if name in RMW:
        c += width * at + IDLE + width * at
        touched = 2 * width
    else:
        c += width * at
        touched = width
    return c, None, touched if fast else 0


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
    ind = None
    for a in args:
        if a.startswith("--m="):
            m = int(a[4:])
        if a.startswith("--x="):
            x = int(a[4:])
        if a.startswith("--db="):
            db = int(a[5:], 16)
        if a.startswith("--ind="):
            ib, ia = a[6:].split(":")
            ind = (int(ib, 16) << 16) | int(ia, 16)

    off = (bank & 0x7F) * 0x8000 + (addr - 0x8000)
    end = off + count
    pc = addr
    total = 0
    program = 0  # bytes fetched from the program stream: see --sum
    dp_insns = 0  # instructions that pay an idle when D is unaligned
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

        c, note, fast = cost(name, mode, val, n, m, x, dp_unaligned, db, ind,
                             bank)
        total += c
        # Every byte of an instruction is fetched exactly once, immediates
        # included — `cpu_adrImm` hands the opcode function two program
        # addresses and it reads them like any other operand — and a data byte
        # this instruction read out of bank $80+ ROM costs the same 2 more with
        # FastROM off, so it belongs in the same column.
        program += 1 + n + fast
        if mode in DP_MODES:
            dp_insns += 1
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

    print("%d master cycles over %d FastROM bytes, %d direct-page, branches "
          "not taken (add 2 per byte with FastROM off, 6 per direct-page "
          "instruction with D unaligned)" % (total, program, dp_insns))


if __name__ == "__main__":
    main()
