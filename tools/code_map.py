r"""A map of the ROM's code: each byte as ported, not ported, or data.

Step 1 of `docs/PLAN.md` ends when the 65816 executes nothing, and a movie can
only say that for what it reaches. This says it for the ROM. It follows the
code from the vectors, from what the stock ROM was seen to run, and from where
each port leaves, and sorts every byte by who would run it:

  * **core**    -- reached, and not inside any port, so only the 65816 can run
    it today. With no CPU this is what would stop the game;
  * **ported**  -- inside a port's stretch: from a registry row's entry to the
    instructions it leaves by, with everything it calls on the way;
  * **guarded** -- what the ROM runs when every row at an entry turns the call
    down (a guard, or an `accepts`), and no port's stretch has. The 65816 runs
    it, if it is ever turned down;
  * **data**    -- read as data in a traced run, and never run;
  * **unknown** -- touched by nothing, as code or as data.

A byte reached more than one way takes the first of these that fits: code
the core can reach is core even if some port also stands for it.

Each of the first three is split again by whether a traced session ran it, so
"core, never run" is the code no movie reaches and no port covers: the gap.

It needs two surveys and the registry:

    zamn_trace <rom> -o <stock>/<name> ...        (the stock ROM, per session)
    zamn.exe <rom> ... --profile <game>/<name>    (what the 65816 still runs)
    zamn_cosim list --tsv > registry.tsv

    python tools/code_map.py <rom> registry.tsv <stock> [--game <game>]

`<stock>` and `<game>` are directories of session directories, each with the
tracer's `zamn.cdl`, `callgraph.csv` and `flows.csv`, or the game's `zamn.cdl`.

**How a port's stretch is found.** A row is entered at `entry`.

  * One with `ret_op` stands for the call to its return, callees and all; the
    harness makes the return.
  * One with `exits` stands for the code from its entry to any of them,
    callees included. An exit on another row's entry hands over to that row,
    as `leave` in `cosim.c` does; otherwise the exit instruction is the
    harness's when it only moves control (`JSR`, `JSL`, `JMP`, `JML`, `BRA`,
    `BRL`, `RTS`, `RTL`, `RTI`, `WAI`) and the 65816's when not. A return that is not
    an exit is gone through: the port carries on in its caller, which is each
    place its routine was called from, and wherever the traces saw it return.
  * A link is a row whose entry is one such instruction, made by the harness.

Where control goes when the harness makes an instruction is where the next
port must be.

**What is not followed.** A jump through a table (`JMP (a,X)`, `JSR (a,X)`,
`JMP (a)`, `JML [a]`) and a return into an address the game pushed go where
the traces saw them go, and nowhere else. Code behind a table entry that no
session used is not found, and the report lists each such site with no target
seen. The register widths come from the traces where the instruction was run,
and from `SEP`/`REP` followed from there where not; a call is taken to come
back with the widths it went in with.

**The self-check** is the game's own survey. What the 65816 executed with the
ports in place must all be core or guarded here. And what the map calls core
that the stock ROM ran, though the game's 65816 never did, was run by a port
every time in play. The map reaches it on the core by a way no session went:
a chain of exits each taken somewhere, never in that order. Either a row is
missing for a case nobody played, or the ports never go that way, or the map
is wrong. Each such stretch is listed.

**What the map does not know** is what a guard or an `accepts` turns down, or
which exit a port takes when. It takes every exit as possible from every way
in, so it errs towards the core. That is the safe side for a game with no CPU.
"""

import argparse
import collections
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dis816 import T, SZ, IMM_M, IMM_X  # noqa: E402

BANK = 0x8000

CDL_CODE, CDL_OPERAND, CDL_DATA, CDL_SUB, CDL_JUMP, CDL_M16, CDL_X16, CDL_CONFLICT = \
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80

# What the harness makes itself when a port leaves by it (`leave` in cosim.c).
HARNESS_MAKES = {0x60, 0x6B, 0x40, 0x20, 0x22, 0x4C, 0x5C, 0x80, 0x82, 0xCB}

BRANCHES = {0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0}
JUMPS = {0x4C, 0x5C, 0x80, 0x82}
CALLS = {0x20, 0x22, 0xFC}
RETURNS = {0x60, 0x6B, 0x40}
INDIRECT = {0x6C, 0x7C, 0xDC, 0xFC}
ENDS = {0x00, 0x02, 0xDB}  # BRK, COP, STP: nothing in this game follows them

# The map's classes, one byte each, in the order a byte reached two ways takes.
UNKNOWN, DATA, GUARDED, PORTED, CORE = 0, 1, 2, 3, 4
NAMES = {UNKNOWN: 'unknown', DATA: 'data', GUARDED: 'guarded', PORTED: 'ported',
         CORE: 'core'}


def canon(addr):
    """The address as the game runs it: ROM through the $80-$FF mirror."""
    bank, lo = addr >> 16, addr & 0xFFFF
    if bank < 0x40 and lo >= 0x8000:
        bank |= 0x80
    return (bank << 16) | lo


def rom_off(addr, size):
    bank, lo = addr >> 16, addr & 0xFFFF
    if lo < 0x8000 or (0x7E <= bank <= 0x7F):
        return None
    off = (bank & 0x7F) * BANK + (lo - 0x8000)
    return off if off < size else None


def snes(off):
    return ((0x80 + off // BANK) << 16) | (0x8000 + off % BANK)


def fmt(addr):
    return '$%02X:%04X' % (addr >> 16, addr & 0xFFFF)


def parse_addr(text):
    text = text.strip().lstrip('$')
    if ':' in text:
        b, a = text.split(':')
        return (int(b, 16) << 16) | int(a, 16)
    return int(text, 16)


def in_bank(addr, delta):
    return (addr & 0xFF0000) | ((addr + delta) & 0xFFFF)


# ---------------------------------------------------------------------------
# Inputs
# ---------------------------------------------------------------------------

def load_cdl(path):
    blob = open(path, 'rb').read()
    if blob[:4] != b'ZCDL':
        raise SystemExit('%s: not a ZCDL log' % path)
    _, size = struct.unpack('<II', blob[4:12])
    return blob[12:12 + size]


def union_cdls(paths, size):
    try:
        import numpy as np
    except ImportError:
        out = bytearray(size)
        for p in paths:
            for i, v in enumerate(load_cdl(p)):
                out[i] |= v
        return out
    acc = np.zeros(size, dtype=np.uint8)
    for p in paths:
        acc |= np.frombuffer(load_cdl(p), dtype=np.uint8)
    return bytearray(acc.tobytes())


def load_pairs(paths):
    out = collections.Counter()
    for p in paths:
        with open(p) as f:
            next(f, None)
            for line in f:
                parts = line.strip().split(',')
                if len(parts) < 3:
                    continue
                out[(canon(parse_addr(parts[0])), canon(parse_addr(parts[1])))] += int(parts[2])
    return out


class Row:
    def __init__(self, name, entry, ret_op, exits, flags):
        self.name, self.entry, self.ret_op = name, entry, ret_op
        self.exits = set(exits)
        self.flags = set(flags)
        # The walk's state for this row's stretch.
        self.seen = set()
        self.inner_calls = collections.defaultdict(set)  # callee -> its calls' next places
        self.came_back = set()  # callees the stretch comes back from
        self.left_in = set()    # callees the port leaves from inside

    @property
    def guarded(self):
        """It may turn a call down: a guard, or an `accepts`."""
        return 'guard' in self.flags or 'accepts' in self.flags

    @property
    def link(self):
        return 'link' in self.flags

    @property
    def by_exits(self):
        return bool(self.exits) and not self.link


def load_registry(path):
    rows = []
    with open(path) as f:
        next(f)
        for line in f:
            p = line.rstrip('\n').split('\t')
            if len(p) < 8:
                continue
            exits = [canon(int(x, 16)) for x in p[6].split(',') if x]
            flags = [x for x in p[7].split(',') if x]
            rows.append(Row(p[0], canon(int(p[1], 16)), canon(int(p[2], 16)), exits, flags))
    return rows


def load_symbols(path):
    names = {}
    if not os.path.exists(path):
        return names
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.split('#')[0].strip()
        if line.startswith('$') and ':' in line[:5]:
            parts = line.split()
            if len(parts) >= 2:
                names[parse_addr(parts[0])] = parts[1]
    return names


# ---------------------------------------------------------------------------
# Decoding
# ---------------------------------------------------------------------------

class Insn:
    __slots__ = ('addr', 'op', 'mode', 'len', 'val', 'name')


def decode(rom, addr, m, x):
    off = rom_off(addr, len(rom))
    if off is None:
        return None
    op = rom[off]
    name, mode = T[op]
    n = SZ.get(mode, 2)
    if mode == IMM_M:
        n = 1 if m else 2
    elif mode == IMM_X:
        n = 1 if x else 2
    val = 0
    for i in range(n):
        o = rom_off(in_bank(addr, 1 + i), len(rom))
        if o is None:
            return None
        val |= rom[o] << (8 * i)
    ins = Insn()
    ins.addr, ins.op, ins.mode, ins.len, ins.val, ins.name = addr, op, mode, 1 + n, val, name
    return ins


def next_addr(ins):
    return in_bank(ins.addr, ins.len)


def direct_targets(ins):
    """Where a jump, branch or call goes, when the instruction says."""
    bank = ins.addr & 0xFF0000
    if ins.op in BRANCHES or ins.op == 0x80:
        return [bank | ((ins.addr + 2 + ((ins.val ^ 0x80) - 0x80)) & 0xFFFF)]
    if ins.op == 0x82:
        return [bank | ((ins.addr + 3 + ((ins.val ^ 0x8000) - 0x8000)) & 0xFFFF)]
    if ins.op in (0x4C, 0x20):
        return [bank | ins.val]
    if ins.op in (0x5C, 0x22):
        return [canon(ins.val)]
    return []


def widths_after(ins, m, x):
    if ins.op == 0xE2:  # SEP
        return (1 if ins.val & 0x20 else m), (1 if ins.val & 0x10 else x)
    if ins.op == 0xC2:  # REP
        return (0 if ins.val & 0x20 else m), (0 if ins.val & 0x10 else x)
    return m, x


# ---------------------------------------------------------------------------
# The walk
#
# Every place the walk stands carries its frame: the routine it is in, as the
# address a call entered it at, or None where it was not entered by a call (a
# thread resumed by `RTL`, an interrupt). A return goes to the places calls to
# its frame come back to, and who runs the code there is decided by who runs
# the return: the 65816, the harness, or a port's C going through it.
# ---------------------------------------------------------------------------

class Map:
    def __init__(self, rom, stock, flows, calls, rows, symbols):
        self.rom, self.size = rom, len(rom)
        self.stock = stock
        self.symbols = symbols
        self.cls = bytearray(self.size)          # the map's class per byte
        self.starts = {}                         # instruction start -> class
        self.reached_by = {}                     # core start -> how it was first reached
        self.row_at = collections.defaultdict(list)
        for r in rows:
            if 'verify_only' in r.flags:
                continue  # never substituted: the ROM runs it
            self.row_at[r.entry].append(r)
        # What the traces saw: the targets of jumps through memory, of every
        # return, and of the returns into an address that is not the
        # instruction after a call.
        self.indirect = collections.defaultdict(set)
        self.returns = collections.defaultdict(set)
        self.pushed = collections.defaultdict(set)
        for (site, target), _ in flows.items():
            off = rom_off(site, self.size)
            if off is None:
                continue
            op = rom[off]
            if op in INDIRECT:
                self.indirect[site].add(target)
            elif op in RETURNS and op != 0x40:
                self.returns[site].add(target)
                if not self.after_call(target):
                    self.pushed[site].add(target)
        self.noreturn, self.inline = self.study_calls(calls)
        self.switch_memo = {}
        self.work = []
        self.core_seen = set()
        self.entered = set()                     # (row, frame)
        # frame -> the places calls to it come back to: (address, the caller's
        # frame, m, x, the caller's class, how the caller was reached)
        self.ret_points = collections.defaultdict(set)
        # frame -> who runs its returns: ('core', class), ('port', row) or
        # ('made', row), each with the widths at the return
        self.waiting = collections.defaultdict(set)
        # Diagnostics.
        self.unseen_indirect = collections.defaultdict(set)  # site -> where
        self.into_data = collections.Counter()
        self.overlaps = collections.Counter()
        self.through_rows = set()
        self.off_rom = collections.Counter()

    # -- what the traces say -------------------------------------------------

    def code_at(self, addr):
        off = rom_off(addr, self.size)
        return off is not None and bool(self.stock[off] & CDL_CODE)

    def after_call(self, target):
        for back, ops in ((3, (0x20, 0xFC)), (4, (0x22,))):
            off = rom_off(in_bank(target, -back), self.size)
            if off is not None and self.stock[off] & CDL_CODE and self.rom[off] in ops:
                return True
        return False

    def study_calls(self, calls):
        """Callees that never come back to the instruction after the call, and
        those that come back a few bytes past it, over inline arguments."""
        sites = collections.defaultdict(list)
        for (site, callee), n in calls.items():
            sites[callee].append(site)
        noreturn, inline = set(), {}
        for callee, ss in sites.items():
            back, skips = False, collections.Counter()
            for s in ss:
                off = rom_off(s, self.size)
                if off is None:
                    continue
                ln = 4 if self.rom[off] == 0x22 else 3
                if self.code_at(in_bank(s, ln)):
                    back = True
                    continue
                for k in range(1, 9):
                    if self.code_at(in_bank(s, ln + k)):
                        skips[k] += 1
                        break
            if back:
                continue
            if len(skips) == 1:
                inline[callee] = next(iter(skips))
            else:
                noreturn.add(callee)
        return noreturn, inline

    def returns_to(self, ins, callee):
        """Where a call comes back to, if it does."""
        if callee in self.noreturn:
            return None
        return in_bank(next_addr(ins), self.inline.get(callee, 0))

    def widths(self, addr, m, x):
        off = rom_off(addr, self.size)
        if off is not None:
            f = self.stock[off]
            if f & CDL_CODE and not f & CDL_CONFLICT:
                return (0 if f & CDL_M16 else 1), (0 if f & CDL_X16 else 1)
        return m, x

    def callees(self, ins, where):
        if ins.op in (0x20, 0x22):
            return direct_targets(ins)
        seen = sorted(self.indirect.get(ins.addr, ()))
        if not seen:
            self.unseen_indirect[ins.addr].add(where)
        return seen

    def switches(self, callee):
        """The callee changes stacks (`TCS` or `TXS`) before it returns: a
        thread yields or ends there. A port's C does the same and carries on
        after the call, so its stretch does not go in: what lies beyond is the
        scheduler and every other thread. Followed within the routine, not
        into what it calls."""
        if callee in self.switch_memo:
            return self.switch_memo[callee]
        found, seen = False, set()
        work = [(callee,) + self.widths(callee, 0, 0)]
        while work and not found and len(seen) < 4000:
            addr, m, x = work.pop()
            if addr in seen:
                continue
            seen.add(addr)
            ins, m, x = self.instruction(addr, m, x)
            if ins is None:
                continue
            if ins.op in (0x1B, 0x9A):
                found = True
                break
            if ins.op in RETURNS or ins.op in ENDS or ins.op in INDIRECT:
                continue
            m2, x2 = widths_after(ins, m, x)
            if ins.op in (0x20, 0x22):
                ret = self.returns_to(ins, direct_targets(ins)[0])
                if ret is not None:
                    work.append((ret, m2, x2))
                continue
            for t in direct_targets(ins):
                work.append((t, m2, x2))
            if ins.op not in JUMPS:
                work.append((next_addr(ins), m2, x2))
        self.switch_memo[callee] = found
        return found

    # -- marking --------------------------------------------------------------

    def mark(self, ins, cls):
        for i in range(ins.len):
            off = rom_off(in_bank(ins.addr, i), self.size)
            if off is not None and self.cls[off] < cls:
                self.cls[off] = cls
        if cls > self.starts.get(ins.addr, UNKNOWN):
            self.starts[ins.addr] = cls

    def check(self, ins):
        """False if the instruction runs into bytes the traces say are data,
        or across the start of an instruction the traces ran."""
        for i in range(ins.len):
            off = rom_off(in_bank(ins.addr, i), self.size)
            if off is None:
                return False
            f = self.stock[off]
            if f & CDL_DATA and not f & (CDL_CODE | CDL_OPERAND):
                self.into_data[ins.addr] += 1
                return False
            if i and f & CDL_CODE:
                self.overlaps[ins.addr] += 1
                return False
        return True

    def instruction(self, addr, m, x):
        if rom_off(addr, self.size) is None:
            self.off_rom[addr] += 1
            return None, m, x
        m, x = self.widths(addr, m, x)
        ins = decode(self.rom, addr, m, x)
        if ins is None or not self.check(ins):
            return None, m, x
        return ins, m, x

    # -- the work list ----------------------------------------------------------

    def core(self, addr, m, x, cls, frame, how):
        self.work.append(('core', addr, m, x, cls, frame, how))

    def stretch(self, row, addr, m, x, frame, inner):
        self.work.append(('port', row, addr, m, x, frame, inner))

    def run(self):
        while self.work:
            item = self.work.pop()
            if item[0] == 'core':
                self.step_core(*item[1:])
            else:
                self.step_port(*item[1:])

    # -- calls and returns at the CPU's level ----------------------------------

    def call(self, callee, ret, m, x, cls, frame, how):
        """A call the 65816 or the harness makes: the callee runs in a frame of
        its own, and comes back to `ret` when its return is run."""
        self.core(callee, m, x, cls, callee, how)
        if ret is None:
            return
        rp = (ret, frame, m, x, cls, how)
        if rp in self.ret_points[callee]:
            return
        self.ret_points[callee].add(rp)
        for w in list(self.waiting[callee]):
            self.resume(w, rp)

    def resume(self, w, rp):
        ret, frame, m, x, cls, how = rp
        if w[0] == 'port':
            self.stretch(w[1], ret, m, x, frame, False)
        elif w[0] == 'made':
            self.core(ret, m, x, cls, frame, ('leaves', w[1].name))
        else:
            self.core(ret, m, x, cls, frame, how)

    def returned(self, site, frame, w, m, x, cls, how):
        """A return in `frame`, run by `w`: to its frame's callers, and to
        wherever the traces saw it go that a frame does not explain."""
        if frame is not None:
            if w not in self.waiting[frame]:
                self.waiting[frame].add(w)
                for rp in list(self.ret_points[frame]):
                    self.resume(w, rp)
            seen = self.pushed.get(site, ())
        else:
            seen = self.pushed.get(site, ())
        for t in seen:
            self.resume(w, (t, None, m, x, cls, how))

    # -- the 65816's code -----------------------------------------------------

    def step_core(self, addr, m, x, cls, frame, how):
        key = (addr, m, x, cls, frame)
        if key in self.core_seen:
            return
        self.core_seen.add(key)
        rows = self.row_at.get(addr)
        if rows:
            for r in rows:
                self.enter(r, frame)
            if not all(r.guarded for r in rows):
                return
            cls = GUARDED
        ins, m, x = self.instruction(addr, m, x)
        if ins is None:
            return
        self.mark(ins, cls)
        if cls == CORE:
            self.reached_by.setdefault(addr, how)
        op = ins.op
        m2, x2 = widths_after(ins, m, x)
        if op in ENDS or op == 0x40:
            return
        if op in RETURNS:
            self.returned(addr, frame, ('core', cls), m2, x2, cls, how)
            return
        if op in CALLS:
            for t in self.callees(ins, 'core'):
                self.call(t, self.returns_to(ins, t), m2, x2, cls, frame, how)
            if op == 0xFC and not self.indirect.get(addr):
                self.core(next_addr(ins), m2, x2, cls, frame, how)
            return
        if op in INDIRECT:
            for t in self.callees(ins, 'core'):
                self.core(t, m2, x2, cls, frame, how)
            return
        for t in direct_targets(ins):
            self.core(t, m2, x2, cls, frame, how)
        if op not in JUMPS:
            self.core(next_addr(ins), m2, x2, cls, frame, how)

    # -- a port's stretch -----------------------------------------------------

    def enter(self, row, frame):
        if (id(row), frame) in self.entered:
            return
        self.entered.add((id(row), frame))
        m, x = self.widths(row.entry, 0, 0)
        if row.link:
            ins, m, x = self.instruction(row.entry, m, x)
            if ins is not None:
                self.mark(ins, PORTED)
                self.harness_makes(ins, m, x, row, frame)
            return
        # The entry is not an exit the first time, even when the row lists it
        # as one: it stops there when it comes round again.
        self.stretch(row, row.entry, m, x, frame, 'entry')

    def step_port(self, row, addr, m, x, frame, inner):
        first = inner == 'entry'
        if first:
            inner = False
        key = (addr, m, x, frame, inner, first)
        if key in row.seen:
            return
        row.seen.add(key)
        ins, m, x = self.instruction(addr, m, x)
        if ins is None:
            return
        how = ('leaves', row.name)
        if addr in row.exits and not first:
            if addr in self.row_at:
                # The harness hands over to that row (`is_entry` in `leave`).
                self.core(addr, m, x, CORE, frame, how)
            elif ins.op in HARNESS_MAKES:
                self.mark(ins, PORTED)
                self.harness_makes(ins, m, x, row, frame)
            else:
                self.core(addr, m, x, CORE, frame, how)
            if inner:
                self.left_inside(row, frame)
            return
        self.mark(ins, PORTED)
        op = ins.op
        m2, x2 = widths_after(ins, m, x)
        if op in ENDS or op == 0x40:
            return
        if op in RETURNS:
            if inner:
                # Back into the stretch, after each call to this frame.
                if frame not in row.came_back:
                    row.came_back.add(frame)
                    for c in list(row.inner_calls[frame]):
                        self.stretch(row, c[0], c[3], c[4], c[1], c[2])
                return
            if row.by_exits:
                # Gone through: the port carries on in its caller.
                self.through_rows.add(row.name)
                self.returned(addr, frame, ('port', row), m2, x2, CORE, how)
            else:
                # `ret_op`: the harness makes the return.
                self.returned(addr, frame, ('made', row), m2, x2, CORE, how)
                for t in self.pushed.get(row.ret_op, ()):
                    self.core(t, m2, x2, CORE, None, how)
            return
        if op in CALLS:
            for t in self.callees(ins, 'ported'):
                ret = self.returns_to(ins, t)
                if self.switches(t):
                    # A yield: the C carries on after it.
                    if ret is not None:
                        self.stretch(row, ret, m2, x2, frame, inner)
                    continue
                self.stretch(row, t, m2, x2, t, True)
                if ret is not None:
                    self.inner_call(row, t, (ret, frame, inner, m2, x2))
            if op == 0xFC and not self.indirect.get(addr):
                self.stretch(row, next_addr(ins), m2, x2, frame, inner)
            return
        if op in INDIRECT:
            for t in self.callees(ins, 'ported'):
                self.stretch(row, t, m2, x2, frame, inner)
            return
        for t in direct_targets(ins):
            self.stretch(row, t, m2, x2, frame, inner)
        if op not in JUMPS:
            self.stretch(row, next_addr(ins), m2, x2, frame, inner)

    def inner_call(self, row, callee, c):
        """A call inside the stretch. Its next place is the stretch's if the
        callee comes back inside it, and whoever makes the callee's return if
        the port leaves from inside the callee."""
        if c in row.inner_calls[callee]:
            return
        row.inner_calls[callee].add(c)
        if callee in row.came_back:
            self.stretch(row, c[0], c[3], c[4], c[1], c[2])
        if callee in row.left_in:
            self.left_call(row, callee, c)

    def left_inside(self, row, callee):
        if callee in row.left_in:
            return
        row.left_in.add(callee)
        for c in list(row.inner_calls[callee]):
            self.left_call(row, callee, c)

    def left_call(self, row, callee, c):
        """The port left inside `callee`: the call's next place is a return
        point of the callee's frame at the CPU's level, and the caller, if it
        was inside the stretch too, was left from inside as well."""
        ret, frame, inner, m, x = c
        rp = (ret, frame, m, x, CORE, ('leaves', row.name))
        if rp not in self.ret_points[callee]:
            self.ret_points[callee].add(rp)
            for w in list(self.waiting[callee]):
                self.resume(w, rp)
        if inner:
            self.left_inside(row, frame)

    def harness_makes(self, ins, m, x, row, frame):
        """The harness makes `ins` as `row` leaves by it. Where it goes is
        where the next port must be."""
        how = ('leaves', row.name)
        m2, x2 = widths_after(ins, m, x)
        op = ins.op
        if op in RETURNS:
            self.returned(ins.addr, frame, ('made', row), m2, x2, CORE, how)
            return
        if op == 0xCB:  # WAI
            self.core(next_addr(ins), m2, x2, CORE, frame, how)
            return
        for t in direct_targets(ins):
            if op in (0x20, 0x22):
                self.call(t, self.returns_to(ins, t), m2, x2, CORE, frame, how)
            else:
                self.core(t, m2, x2, CORE, frame, how)


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

def runs(starts, wanted):
    """Runs of instruction starts in one of `wanted`, no more than four bytes
    apart, as [first, last], in ROM order."""
    out = []
    cur = None
    for addr in sorted(a for a, c in starts.items() if c in wanted):
        if cur and addr - cur[1] <= 4 and (addr >> 16) == (cur[0] >> 16):
            cur[1] = addr
        else:
            if cur:
                out.append(cur)
            cur = [addr, addr]
    if cur:
        out.append(cur)
    return out


def session_dirs(root, name):
    dirs = sorted(d for d in glob.glob(os.path.join(root, '*')) if os.path.isdir(d))
    return [os.path.join(d, name) for d in dirs if os.path.exists(os.path.join(d, name))]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('rom')
    ap.add_argument('registry', help='zamn_cosim list --tsv')
    ap.add_argument('stock', help='directory of zamn_trace session directories')
    ap.add_argument('--game', help='directory of zamn.exe --profile session directories')
    ap.add_argument('--banks', default='80-83', help='banks reported, e.g. 80-83')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--out', help='write the map, one class byte per ROM byte')
    ap.add_argument('--list', help='write every stretch of core and guarded code here')
    args = ap.parse_args()

    rom = open(args.rom, 'rb').read()
    size = len(rom)
    lo, hi = (int(b, 16) for b in args.banks.split('-'))

    cdls = session_dirs(args.stock, 'zamn.cdl')
    if not cdls:
        raise SystemExit('%s: no session has a zamn.cdl' % args.stock)
    stock = union_cdls(cdls, size)
    flows = load_pairs(session_dirs(args.stock, 'flows.csv'))
    calls = load_pairs(session_dirs(args.stock, 'callgraph.csv'))
    if not flows:
        print('warning: no flows.csv in %s: jumps through tables and returns '
              'are not followed' % args.stock)
    rows = load_registry(args.registry)
    here = os.path.dirname(os.path.abspath(__file__))
    symbols = load_symbols(os.path.join(here, 'symbols', 'zamn.sym'))

    mp = Map(rom, stock, flows, calls, rows, symbols)

    # Vectors, native and emulation, as the game sees them.
    def vec(o):
        return canon(struct.unpack('<H', rom[0x7FE0 + o:0x7FE2 + o])[0])
    for o, what in ((0x1C, 'reset'), (0x0A, 'NMI'), (0x0E, 'IRQ'), (0x06, 'BRK'),
                    (0x04, 'COP'), (0x1A, 'NMI (emulation)'), (0x1E, 'IRQ (emulation)')):
        wide = 1 if what == 'reset' else 0
        mp.core(vec(o), wide, wide, CORE, None, ('vector', what))
    mp.run()

    # What the stock ROM ran and no walk reached: arrived at some way the walk
    # does not model. Each is walked from, and counted.
    orphans = [snes(off) for off in range(size)
               if stock[off] & CDL_CODE and snes(off) not in mp.starts]
    orphan_roots = []
    for a in orphans:
        if a in mp.starts:
            continue
        if a in mp.row_at:
            for r in mp.row_at[a]:
                mp.enter(r, None)
        else:
            orphan_roots.append(a)
            m, x = mp.widths(a, 0, 0)
            mp.core(a, m, x, CORE, None, ('orphan', fmt(a)))
        mp.run()
    still = [a for a in orphans if a not in mp.starts]

    # Data, from the traces, where no walk put code.
    for off in range(size):
        if mp.cls[off] == UNKNOWN and stock[off] & CDL_DATA:
            mp.cls[off] = DATA

    # ---- the report ------------------------------------------------------
    def ran(off):
        return bool(stock[off] & (CDL_CODE | CDL_OPERAND))

    print('Code map of banks $%02X-$%02X, %d sessions traced, %d registry rows' % (
        lo, hi, len(cdls), len(rows)))
    print()
    print('%-6s %9s %9s %9s %9s %9s %9s %9s %9s' % (
        'bank', 'core', '(unrun)', 'ported', '(unrun)', 'guarded', '(unrun)', 'data',
        'unknown'))
    totals = collections.Counter()
    for b in range(lo, hi + 1):
        c = collections.Counter()
        for off in range((b - 0x80) * BANK, (b - 0x7F) * BANK):
            k = mp.cls[off]
            c[k] += 1
            if k in (PORTED, CORE, GUARDED) and not ran(off):
                c[(k, 'unrun')] += 1
        totals.update(c)
        print('$%02X    %9d %9d %9d %9d %9d %9d %9d %9d' % (
            b, c[CORE], c[(CORE, 'unrun')], c[PORTED], c[(PORTED, 'unrun')], c[GUARDED],
            c[(GUARDED, 'unrun')], c[DATA], c[UNKNOWN]))
    t = totals
    print('%-6s %9d %9d %9d %9d %9d %9d %9d %9d' % (
        'all', t[CORE], t[(CORE, 'unrun')], t[PORTED], t[(PORTED, 'unrun')], t[GUARDED],
        t[(GUARDED, 'unrun')], t[DATA], t[UNKNOWN]))
    print()
    print('Bytes of code reached: %d. Core %d, ported %d, guarded %d.' % (
        t[PORTED] + t[CORE] + t[GUARDED], t[CORE], t[PORTED], t[GUARDED]))
    print()

    how = collections.Counter()
    for a, c in mp.starts.items():
        if c == CORE and lo <= (a >> 16) <= hi:
            how[mp.reached_by.get(a, ('?',))[0]] += 1
    print('Instructions of core code, by how the walk first reached them: %s' % ', '.join(
        '%s %d' % kv for kv in how.most_common()))
    print('Traced code no walk reached at first: %d instructions, walked from %d places; '
          '%d still unreached.' % (len(orphans), len(orphan_roots), len(still)))
    print('Jumps through memory with no target seen: %d sites (%d in core code).' % (
        len(mp.unseen_indirect), sum('core' in v for v in mp.unseen_indirect.values())))
    print('Walks stopped at bytes the traces read as data: %d places. At an overlap: %d.' % (
        len(mp.into_data), len(mp.overlaps)))
    print('Calls taken never to return: %d callees. Over inline bytes: %d callees.' % (
        len(mp.noreturn), len(mp.inline)))
    print('Ports whose stretch goes through a return into their caller: %d.' % len(
        mp.through_rows))
    print()

    if args.game:
        gcdls = session_dirs(args.game, 'zamn.cdl')
        game = union_cdls(gcdls, size)
        gran = [snes(off) for off in range(size) if game[off] & CDL_CODE]
        bad = [a for a in gran if mp.starts.get(a) not in (CORE, GUARDED)]
        print('Self-check over %d game sessions: the 65816 ran %d distinct instructions, '
              '%d of them not core or guarded here.' % (len(gcdls), len(gran), len(bad)))
        for a in bad[:20]:
            print('    %s  %s' % (fmt(a), NAMES[mp.starts.get(a, UNKNOWN)]))
        short = {a: CORE for a, c in mp.starts.items()
                 if c == CORE and mp.code_at(a) and not game[rom_off(a, size)] & CDL_CODE}
        # The stock ROM ran these, and the game ran them in a port every time.
        # The map reaches them on the core by a way no session went: a chain
        # of exits each of which some session took, but not in that order. A
        # missing row for a case nobody played, or a way the ports never go.
        print('Core here, run by the stock ROM, and by a port every time in the game: '
              '%d instructions.' % len(short))
        print('  (Each is reached on the core only by a way no session went.)')
        for first, last in runs(short, {CORE})[:args.top]:
            print('    %s..%s  reached by %s' % (fmt(first), fmt(last),
                                               mp.reached_by.get(first)))
        print()

    stretches = []
    for first, last in runs(mp.starts, {CORE, GUARDED}):
        if not lo <= (first >> 16) <= hi:
            continue
        o0, o1 = rom_off(first, size), rom_off(last, size)
        span = [off for off in range(o0, min(o1 + 4, size)) if mp.cls[off] in (CORE, GUARDED)]
        unrun = sum(1 for off in span if not ran(off))
        g = all(mp.cls[off] == GUARDED for off in span)
        stretches.append((len(span), first, last, unrun, g, mp.reached_by.get(first)))
    stretches.sort(key=lambda s: -s[0])
    for kind, want in (('core', False), ('guarded', True)):
        sel = [s for s in stretches if s[4] == want]
        print('Stretches of %s code: %d, %d bytes, %d of them never run. The largest:' % (
            kind, len(sel), sum(s[0] for s in sel), sum(s[3] for s in sel)))
        print('  %-10s %-10s %6s %6s  %s' % ('from', 'to', 'bytes', 'unrun', 'reached by'))
        for nbytes, first, last, unrun, g, how in sel[:args.top]:
            print('  %-10s %-10s %6d %6d  %s %s' % (fmt(first), fmt(last), nbytes, unrun,
                                                  how or '', mp.symbols.get(first, '')))
        print()

    if mp.unseen_indirect:
        print('Jumps through memory with no target seen (the tables to read):')
        for a in sorted(mp.unseen_indirect):
            if lo <= (a >> 16) <= hi:
                ins = decode(rom, a, 0, 0)
                print('  %s  %s $%04X  in %s code' % (fmt(a), ins.name, ins.val,
                                                     '/'.join(sorted(mp.unseen_indirect[a]))))
        print()

    if args.list:
        with open(args.list, 'w') as f:
            for nbytes, first, last, unrun, g, how in sorted(stretches, key=lambda s: s[1]):
                f.write('%s %s %d %d %s %s\n' % (fmt(first), fmt(last), nbytes, unrun,
                                                 'guarded' if g else 'core', how))
    if args.out:
        with open(args.out, 'wb') as f:
            f.write(bytes(mp.cls))


if __name__ == '__main__':
    main()
