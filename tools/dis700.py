r"""Disassemble the game's SPC700 sound driver, out of the user's ROM.

The driver image is gathered the way `$80:CB1A` gathers it (two runs of ROM,
see `src/assets/music.h`), its IPL blocks are put where the boot ROM puts them
in the sound RAM, and the code is followed from the entry point: every branch,
call and jump that can be read off the bytes, and `TCALL`/`PCALL` too. A jump
through a table (`JMP [!a+X]`) is reported with its table, and the table's
entries are followed when the table's length is given:

    python tools\dis700.py "Zombies Ate My Neighbors.sfc" [out.asm]
        [--table addr,count]... [--split lo,hi,count]... [--entry addr]...
        [--trace trace.txt]

`--split` is a table kept as two arrays, the low bytes at `lo` and the high
bytes at `hi`. The driver dispatches its commands and its song events that
way, by writing an entry into the operand of a `JMP` and running it. With no
`--table` or `--split`, the driver's three are given (see `DRIVER_TABLES`).

`--trace` takes the list of addresses the SPC700 executed, one hex address a
line, as `zamn_test_dsp --spc-trace` writes it, and marks each instruction
that ran with `*` -- and follows from those too, so code reached only through
a table nobody gave is still decoded.

The listing quotes the ROM, so it goes in `analysis/` (ignored), never in git.

Each line is `addr: bytes  instruction  ; cycles`. The cycle counts are the
documented ones; a taken branch costs two more.
"""
import sys

# --- the image --------------------------------------------------------------

def lorom(rom, bank, addr):
    return (bank & 0x7F) * 0x8000 + (addr - 0x8000)


def driver_aram(rom):
    """The sound RAM as the boot ROM leaves it, plus the entry point and the
    list of (dest, len) blocks."""
    image = rom[lorom(rom, 0x91, 0x8000):lorom(rom, 0x91, 0x8000) + 0x8000]
    image += rom[lorom(rom, 0x95, 0xBA3D):lorom(rom, 0x95, 0xBA3D) + 0x1A86]
    aram = bytearray(0x10000)
    known = bytearray(0x10000)
    blocks = []
    at = 0
    while True:
        n = image[at] | image[at + 1] << 8
        dest = image[at + 2] | image[at + 3] << 8
        at += 4
        if n == 0:
            return aram, known, dest, blocks
        aram[dest:dest + n] = image[at:at + n]
        for i in range(n):
            known[(dest + i) & 0xFFFF] = 1
        blocks.append((dest, n))
        at += n


# --- the instruction set ----------------------------------------------------
# Operand kinds. dp: direct page; abs: !a; rel: branch; imm: #i;
# mb: a 13-bit address and a bit number; dd_ds/dp_imm: two operands whose
# bytes are the other way round from how they are written.

def build_table():
    t = {}
    branches = ["BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"]
    for i in range(8):
        t[i * 0x20 + 0x10] = (branches[i] + " {rel}", "rel", 2)
    t[0x00] = ("NOP", "", 2); t[0x20] = ("CLRP", "", 2); t[0x40] = ("SETP", "", 2)
    t[0x60] = ("CLRC", "", 2); t[0x80] = ("SETC", "", 2); t[0xA0] = ("EI", "", 3)
    t[0xC0] = ("DI", "", 3); t[0xE0] = ("CLRV", "", 2)
    for n in range(16):
        t[n * 0x10 + 1] = ("TCALL %d" % n, "", 8)
        t[n * 0x10 + 2] = ("%s1 {dp}.%d" % ("SET" if n % 2 == 0 else "CLR", n // 2), "dp", 4)
        t[n * 0x10 + 3] = ("%s {dp}.%d, {rel}" % ("BBS" if n % 2 == 0 else "BBC", n // 2), "dp_rel", 5)
    alu = ["OR", "AND", "EOR", "CMP", "ADC", "SBC"]
    for i, op in enumerate(alu):
        b = i * 0x20
        t[b + 0x04] = (op + " A, {dp}", "dp", 3)
        t[b + 0x14] = (op + " A, {dp}+X", "dp", 4)
        t[b + 0x05] = (op + " A, {abs}", "abs", 4)
        t[b + 0x15] = (op + " A, {abs}+X", "abs", 5)
        t[b + 0x06] = (op + " A, (X)", "", 3)
        t[b + 0x16] = (op + " A, {abs}+Y", "abs", 5)
        t[b + 0x07] = (op + " A, [{dp}+X]", "dp", 6)
        t[b + 0x17] = (op + " A, [{dp}]+Y", "dp", 6)
        t[b + 0x08] = (op + " A, #{imm}", "imm", 2)
        t[b + 0x18] = (op + " {dp2}, #{imm}", "dp_imm", 5)
        t[b + 0x09] = (op + " {dd}, {ds}", "dd_ds", 6)
        t[b + 0x19] = (op + " (X), (Y)", "", 5)
    t[0xC4] = ("MOV {dp}, A", "dp", 4); t[0xD4] = ("MOV {dp}+X, A", "dp", 5)
    t[0xC5] = ("MOV {abs}, A", "abs", 5); t[0xD5] = ("MOV {abs}+X, A", "abs", 6)
    t[0xC6] = ("MOV (X), A", "", 4); t[0xD6] = ("MOV {abs}+Y, A", "abs", 6)
    t[0xC7] = ("MOV [{dp}+X], A", "dp", 7); t[0xD7] = ("MOV [{dp}]+Y, A", "dp", 7)
    t[0xE4] = ("MOV A, {dp}", "dp", 3); t[0xF4] = ("MOV A, {dp}+X", "dp", 4)
    t[0xE5] = ("MOV A, {abs}", "abs", 4); t[0xF5] = ("MOV A, {abs}+X", "abs", 5)
    t[0xE6] = ("MOV A, (X)", "", 3); t[0xF6] = ("MOV A, {abs}+Y", "abs", 5)
    t[0xE7] = ("MOV A, [{dp}+X]", "dp", 6); t[0xF7] = ("MOV A, [{dp}]+Y", "dp", 6)
    t[0xC8] = ("CMP X, #{imm}", "imm", 2); t[0xD8] = ("MOV {dp}, X", "dp", 4)
    t[0xE8] = ("MOV A, #{imm}", "imm", 2); t[0xF8] = ("MOV X, {dp}", "dp", 3)
    t[0xC9] = ("MOV {abs}, X", "abs", 5); t[0xD9] = ("MOV {dp}+Y, X", "dp", 5)
    t[0xE9] = ("MOV X, {abs}", "abs", 4); t[0xF9] = ("MOV X, {dp}+Y", "dp", 4)
    t[0x0A] = ("OR1 C, {mb}", "mb", 5); t[0x2A] = ("OR1 C, /{mb}", "mb", 5)
    t[0x4A] = ("AND1 C, {mb}", "mb", 4); t[0x6A] = ("AND1 C, /{mb}", "mb", 4)
    t[0x8A] = ("EOR1 C, {mb}", "mb", 5); t[0xAA] = ("MOV1 C, {mb}", "mb", 4)
    t[0xCA] = ("MOV1 {mb}, C", "mb", 6); t[0xEA] = ("NOT1 {mb}", "mb", 5)
    t[0x1A] = ("DECW {dp}", "dp", 6); t[0x3A] = ("INCW {dp}", "dp", 6)
    t[0x5A] = ("CMPW YA, {dp}", "dp", 4); t[0x7A] = ("ADDW YA, {dp}", "dp", 5)
    t[0x9A] = ("SUBW YA, {dp}", "dp", 5); t[0xBA] = ("MOVW YA, {dp}", "dp", 5)
    t[0xDA] = ("MOVW {dp}, YA", "dp", 5); t[0xFA] = ("MOV {dd}, {ds}", "dd_ds", 5)
    for i, op in enumerate(["ASL", "ROL", "LSR", "ROR", "DEC", "INC"]):
        b = i * 0x20
        t[b + 0x0B] = (op + " {dp}", "dp", 4)
        t[b + 0x1B] = (op + " {dp}+X", "dp", 5)
        t[b + 0x0C] = (op + " {abs}", "abs", 5)
    t[0x1C] = ("ASL A", "", 2); t[0x3C] = ("ROL A", "", 2); t[0x5C] = ("LSR A", "", 2)
    t[0x7C] = ("ROR A", "", 2); t[0x9C] = ("DEC A", "", 2); t[0xBC] = ("INC A", "", 2)
    t[0xCB] = ("MOV {dp}, Y", "dp", 4); t[0xDB] = ("MOV {dp}+X, Y", "dp", 5)
    t[0xEB] = ("MOV Y, {dp}", "dp", 3); t[0xFB] = ("MOV Y, {dp}+X", "dp", 4)
    t[0xCC] = ("MOV {abs}, Y", "abs", 5); t[0xDC] = ("DEC Y", "", 2)
    t[0xEC] = ("MOV Y, {abs}", "abs", 4); t[0xFC] = ("INC Y", "", 2)
    t[0x0D] = ("PUSH PSW", "", 4); t[0x1D] = ("DEC X", "", 2)
    t[0x2D] = ("PUSH A", "", 4); t[0x3D] = ("INC X", "", 2)
    t[0x4D] = ("PUSH X", "", 4); t[0x5D] = ("MOV X, A", "", 2)
    t[0x6D] = ("PUSH Y", "", 4); t[0x7D] = ("MOV A, X", "", 2)
    t[0x8D] = ("MOV Y, #{imm}", "imm", 2); t[0x9D] = ("MOV X, SP", "", 2)
    t[0xAD] = ("CMP Y, #{imm}", "imm", 2); t[0xBD] = ("MOV SP, X", "", 2)
    t[0xCD] = ("MOV X, #{imm}", "imm", 2); t[0xDD] = ("MOV A, Y", "", 2)
    t[0xED] = ("NOTC", "", 3); t[0xFD] = ("MOV Y, A", "", 2)
    t[0x0E] = ("TSET1 {abs}", "abs", 6); t[0x1E] = ("CMP X, {abs}", "abs", 4)
    t[0x2E] = ("CBNE {dp}, {rel}", "dp_rel", 5); t[0x3E] = ("CMP X, {dp}", "dp", 3)
    t[0x4E] = ("TCLR1 {abs}", "abs", 6); t[0x5E] = ("CMP Y, {abs}", "abs", 4)
    t[0x6E] = ("DBNZ {dp}, {rel}", "dp_rel", 5); t[0x7E] = ("CMP Y, {dp}", "dp", 3)
    t[0x8E] = ("POP PSW", "", 4); t[0x9E] = ("DIV YA, X", "", 12)
    t[0xAE] = ("POP A", "", 4); t[0xBE] = ("DAS A", "", 3)
    t[0xCE] = ("POP X", "", 4); t[0xDE] = ("CBNE {dp}+X, {rel}", "dp_rel", 6)
    t[0xEE] = ("POP Y", "", 4); t[0xFE] = ("DBNZ Y, {rel}", "rel", 4)
    t[0x0F] = ("BRK", "", 8); t[0x1F] = ("JMP [{abs}+X]", "abs", 6)
    t[0x2F] = ("BRA {rel}", "rel", 4); t[0x3F] = ("CALL {abs}", "abs", 8)
    t[0x4F] = ("PCALL {upage}", "imm", 6); t[0x5F] = ("JMP {abs}", "abs", 3)
    t[0x6F] = ("RET", "", 5); t[0x7F] = ("RETI", "", 6)
    t[0x8F] = ("MOV {dp2}, #{imm}", "dp_imm", 5); t[0x9F] = ("XCN A", "", 5)
    t[0xAF] = ("MOV (X)+, A", "", 4); t[0xBF] = ("MOV A, (X)+", "", 4)
    t[0xCF] = ("MUL YA", "", 9); t[0xDF] = ("DAA A", "", 3)
    t[0xEF] = ("SLEEP", "", 3); t[0xFF] = ("STOP", "", 3)
    assert len(t) == 256, len(t)
    return t


T = build_table()
SIZE = {"": 1, "dp": 2, "abs": 3, "rel": 2, "imm": 2, "mb": 3, "dd_ds": 3,
        "dp_imm": 3, "dp_rel": 3}
IO = {0xF0: "TEST", 0xF1: "CONTROL", 0xF2: "DSPADDR", 0xF3: "DSPDATA",
      0xF4: "PORT0", 0xF5: "PORT1", 0xF6: "PORT2", 0xF7: "PORT3",
      0xF8: "AUX4", 0xF9: "AUX5", 0xFA: "T0TARGET", 0xFB: "T1TARGET",
      0xFC: "T2TARGET", 0xFD: "T0OUT", 0xFE: "T1OUT", 0xFF: "T2OUT"}
ENDS = {0x5F, 0x6F, 0x7F, 0x2F, 0x1F, 0xFF, 0xEF, 0x0F}  # no fall-through


def rel(pc, size, b):
    return (pc + size + (b - 256 if b > 127 else b)) & 0xFFFF


def decode(aram, pc):
    op = aram[pc]
    text, kind, cyc = T[op]
    n = SIZE[kind]
    b = [aram[(pc + i) & 0xFFFF] for i in range(n)]
    targets = []
    f = {}
    if kind == "dp":
        f["dp"] = "$%02X" % b[1] if b[1] not in IO else IO[b[1]]
    elif kind == "abs":
        a = b[1] | b[2] << 8
        f["abs"] = "!$%04X" % a
        if op in (0x3F, 0x5F):
            targets.append(a)
    elif kind == "rel":
        t = rel(pc, n, b[1])
        f["rel"] = "$%04X" % t
        targets.append(t)
    elif kind == "imm":
        f["imm"] = "$%02X" % b[1]
        f["upage"] = "$FF%02X" % b[1]
        if op == 0x4F:
            targets.append(0xFF00 | b[1])
    elif kind == "mb":
        w = b[1] | b[2] << 8
        f["mb"] = "$%04X.%d" % (w & 0x1FFF, w >> 13)
    elif kind == "dd_ds":
        f["ds"] = IO.get(b[1], "$%02X" % b[1])
        f["dd"] = IO.get(b[2], "$%02X" % b[2])
    elif kind == "dp_imm":
        f["imm"] = "$%02X" % b[1]
        f["dp2"] = IO.get(b[2], "$%02X" % b[2])
    elif kind == "dp_rel":
        f["dp"] = IO.get(b[1], "$%02X" % b[1])
        t = rel(pc, n, b[2])
        f["rel"] = "$%04X" % t
        targets.append(t)
    if (op & 0x0F) == 1:  # TCALL n: the vector at $FFDE - 2n
        targets.append(("tcall", op >> 4))
    return text.format(**f), n, cyc, targets, b


# The driver's dispatch tables, split: (lo, hi, count, name).
DRIVER_TABLES = [
    (0x0B34, 0x0B49, 0x15, "command"),   # $0B25: the 65816's commands
    (0x074E, 0x0755, 7, "event"),        # $073F: song events $80-$EF, by high nibble
    (0x07C4, 0x07DA, 0x16, "control"),   # $07B4: song events $D0, by parameter
]


def main():
    args = sys.argv[1:]
    tables, entries, trace_path = [], [], None
    splits = []
    pos = []
    i = 0
    while i < len(args):
        if args[i] == "--table":
            a, c = args[i + 1].split(","); tables.append((int(a, 16), int(c))); i += 2
        elif args[i] == "--split":
            lo, hi, c = args[i + 1].split(",")
            splits.append((int(lo, 16), int(hi, 16), int(c), "split_%s" % lo)); i += 2
        elif args[i] == "--entry":
            entries.append(int(args[i + 1], 16)); i += 2
        elif args[i] == "--trace":
            trace_path = args[i + 1]; i += 2
        else:
            pos.append(args[i]); i += 1
    rom = open(pos[0], "rb").read()
    out = open(pos[1], "w") if len(pos) > 1 else sys.stdout
    aram, known, exec_at, blocks = driver_aram(rom)
    ran = set()
    if trace_path:
        for line in open(trace_path):
            line = line.strip()
            if line:
                ran.add(int(line.split()[0], 16))

    todo = [exec_at] + entries + sorted(ran)
    labels = {exec_at: "entry"}
    for a, c in tables:
        for k in range(c):
            t = aram[a + 2 * k] | aram[a + 2 * k + 1] << 8
            todo.append(t)
            labels.setdefault(t, "table_%04X_%d" % (a, k))
    if not tables and not splits:
        splits = DRIVER_TABLES
    for lo, hi, c, name in splits:
        for k in range(c):
            t = aram[lo + k] | aram[hi + k] << 8
            todo.append(t)
            labels.setdefault(t, "%s_%02X" % (name, k))
    code = {}
    jumptables = []
    while todo:
        pc = todo.pop()
        while pc not in code and known[pc]:
            text, n, cyc, targets, b = decode(aram, pc)
            code[pc] = (text, n, cyc, b)
            for t in targets:
                if isinstance(t, tuple):
                    v = 0xFFDE - 2 * t[1]
                    t = aram[v] | aram[v + 1] << 8
                    labels.setdefault(t, "tcall_%d" % ((0xFFDE - v) // 2))
                labels.setdefault(t, "L%04X" % t)
                todo.append(t)
            op = b[0]
            if op == 0x1F:
                jumptables.append((pc, b[1] | b[2] << 8))
            if op in ENDS:
                break
            pc = (pc + n) & 0xFFFF

    w = out.write
    w("; Zombies Ate My Neighbors: the SPC700 sound driver, from the ROM.\n")
    w("; Generated by tools/dis700.py. Quotes the ROM: never commit.\n;\n")
    w("; IPL blocks (dest, bytes):\n")
    for d, n in blocks:
        w(";   $%04X-$%04X  %5d bytes\n" % (d, d + n - 1, n))
    w("; entry $%04X\n" % exec_at)
    if jumptables:
        w(";\n; jump tables (JMP [!a+X]) -- give --table addr,count to follow:\n")
        for pc, a in jumptables:
            w(";   at $%04X: table $%04X\n" % (pc, a))
    total = sum(v[1] for v in code.values())
    w("; %d instructions, %d bytes decoded%s\n\n" % (
        len(code), total, ", %d addresses ran" % len(ran) if ran else ""))
    prev_end = None
    for pc in sorted(code):
        text, n, cyc, b = code[pc]
        if prev_end is not None and pc != prev_end:
            w("\n")
        if pc in labels:
            w("%s:\n" % labels[pc])
        mark = "*" if pc in ran else " "
        w("%s %04X: %-9s %-28s ; %d\n" % (mark, pc, " ".join("%02X" % x for x in b), text, cyc))
        prev_end = (pc + n) & 0xFFFF
    # Data runs inside the uploaded blocks that are not code.
    w("\n; bytes of the blocks not decoded as code: ")
    covered = set()
    for pc, (text, n, cyc, b) in code.items():
        for k in range(n):
            covered.add((pc + k) & 0xFFFF)
    runs = []
    for d, n in blocks:
        start = None
        for a in range(d, d + n + 1):
            inside = a < d + n and a not in covered
            if inside and start is None:
                start = a
            elif not inside and start is not None:
                runs.append((start, a - start)); start = None
    w("%d runs\n" % len(runs))
    for s, n in runs:
        w(";   $%04X  %5d bytes\n" % (s, n))


if __name__ == "__main__":
    main()
