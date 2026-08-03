"""How much of the work the ROM does is the port doing?

`PROGRESS.md` has counted ported routines since Phase 3 began, and 47 of them is
a real number with no denominator attached: it cannot tell a 17-byte leaf from a
2 KB state machine, and both count as one. This answers the question the routine
count cannot -- **what share of the instructions the game actually executes are
inside routines the port has** -- and ranks what is left by the same measure, so
that "what should be ported next" is a number rather than a taste.

It needs `profile.bin`, which `zamn_trace` writes beside the CDL: one word per
ROM byte, counted at the opcode, so summing it over a range gives instructions
executed rather than bytes touched.

    build/zamn_trace.exe <rom> -o analysis/prof/level1 -f 6100 -m movies/level1.zmv
    python tools/native_share.py analysis/prof/*

**Attribution is by nearest preceding subroutine entry**, which is what a
sampling profiler does with a symbol table and carries the same caveat: code
entered only by a jump is credited to the routine above it. That is usually the
same routine -- a branch target inside a body, or its tail -- and is wrong where
the ROM jumps between two things that happen to be adjacent. The `entries` count
in the report is how many boundaries the attribution had to work with; a routine
with a suspiciously large share is worth checking against the disassembly before
it is believed.

Two numbers come out, and they answer different questions:

  * **static** -- of the distinct code bytes the game executed at all, what
    share is inside ported routines. This is "how much of the ROM have we
    written", and it is the flattering one.
  * **dynamic** -- of the instructions executed, what share ran inside ported
    routines. This is "how much of the work have we taken over", and it is the
    one that says whether the next routine is worth the round.
"""

import os
import re
import struct
import sys
import collections

import numpy as np

BANK_SIZE = 0x8000
CDL_CODE = 0x01
CDL_SUB = 0x08

# Busy-waits, as (address, bytes, what it is waiting for).
#
# These are counted by the profiler like any other instructions, and they are
# not work: a 65816 going round `BIT $00C8 : BPL` a hundred thousand times is a
# CPU with nothing to do until the next VBlank. Left in the denominator they
# make the port's share look smaller than it is, and left in the ranking they
# put a two-instruction loop at the top of the list of things to port next --
# where porting it would achieve exactly nothing, because the C would have to
# spin on the same flag.
#
# Each was found by reading the disassembly of a routine the ranking had put
# near the top, and each is checked against the profile rather than assumed:
# `$80:9F9D` is nine instructions long and 19,289,582 of the 19,289,827 credited
# to it are the two below.
WAIT_SITES = [
    (0x809FAA, 5, 'BIT $00C8 : BPL   -- waiting for a VBL callback to fire'),
    (0x80CB6C, 5, 'CMP $2140 : BNE   -- SPC700 IPL, waiting for $BBAA'),
    (0x80CB84, 5, 'CMP $2140 : BNE   -- SPC700 IPL, waiting per byte'),
    (0x80CB94, 5, 'CMP $2140 : BNE   -- SPC700 IPL, waiting per block'),
    (0x80CB99, 4, 'ADC #$03 : BEQ    -- the IPL delay loop after it'),
    (0x82AC65, 8, 'CMP #$0078 : BCC  -- the level intro, holding for 120 frames'),
    (0x82AC92, 8, 'CMP #$0078 : BCC  -- ...and again after the block library'),
    # `$80:91F7` spends 4,406,310 of its 4,406,610 instructions in these two
    # loops -- 99.993% -- and the 300 that are left are 30 real instructions a
    # call. Both spin on `$136C`, which the main CPU never touches: `$80:9C52`
    # zeroes it and queues the vblank job `$80:9C63` to `INC` it fifteen times,
    # and `$80:9C72` queues `$80:9C7D` to `DEC` it back past zero. So this is a
    # fade counted on the vblank side with the CPU held against it, and porting
    # either loop would replace a spin with a spin.
    (0x80923A, 8, 'CMP #$000F : BNE  -- a screen fading in, counted by a VBL job'),
    (0x80924C, 8, 'AND #$0080 : BEQ  -- ...and the same screen fading back out'),
    # The same story one row down. `$80:9F29` queues the vblank job `$80:9ED0`
    # to push a tilemap into VRAM and then holds here until `$C6`, the job's
    # remaining byte count, reaches zero: 2,149,252 of its 2,149,592
    # instructions, 99.98%, leaving 34 a call that are real.
    (0x809F5C, 4, 'LDA $C6 : BNE     -- waiting for a queued VRAM upload to drain'),
]

# Routines the ranking will keep putting near the top and which the harness
# cannot take, for reasons that are structural rather than a matter of effort.
# They stay in the list -- their work is real and the denominator is honest --
# but they are marked, because the ranking is used to pick rounds and without
# this it points four deep into a wall.
#
# The two dispatchers are the interesting case. Neither is a leaf in any sense:
# each pushes a far return address, pushes a job's address out of a WRAM table
# and `RTL`s into it, so "port this routine" means "port every job that can be
# in the table". Queue A has thirteen distinct enqueue sites and queue B has
# one. What the profile credits to them is not the jobs, though -- attribution
# stops at the next subroutine entry -- it is the scan: sixteen slots from the
# top down, every call, whether two are live or none, which is 146 instructions
# a frame in A and 53 in B.
# Routine starts the CDL cannot see, because nothing ever `JSR`/`JSL`s to them.
#
# Attribution is by nearest preceding entry, so a routine missing from the
# entry list does not merely score zero: **its work is charged to whatever sits
# above it**, and — worse — the call-graph edges leaving it are charged there
# too, because `owner_of` maps a call site to the entry above it. A `JSL` in an
# orphan can therefore make the orphan's *callee* look like it is called only
# from ported code, and the subsumption closure will then mark that callee, and
# everything below it, native.
#
# That is not hypothetical. `$80:A937` is a four-byte `JSL` thunk that begins
# one byte past `$80:A8B3`'s `RTL`; nothing calls it, so it belonged to
# `$80:A8B3`, so the round that ported `$80:A8B3` silently claimed `$80:A93F`
# and the four tilemap scroll routines under it — 1.6 points of native share
# that nobody had written a line of C for. Anything added here should be
# checked the same way: port a neighbour, and see whether the number moves by
# more than the neighbour is worth.
JUMP_ENTRIES = {
    0x80A937: 'a JSL thunk one byte past $80:A8B3 RTL, into $80:A93B',
    0x82AC07: 'the real start of what the ranking credits to $82:AB5B',
    # $80:CDEB lzss_write_byte is five instructions and ends at its RTS on
    # $80:CDF3. What follows is a different routine, reached by a jump, and it
    # opens `JSR $D13A` and then `JSL thread_yield` -- so without this line its
    # call edges are credited to lzss_write_byte, and registering a five-byte
    # leaf silently subsumes $80:D13A, $80:D1EA, $80:D01B and their closures.
    # Caught by the check the $80:A937 round installed: registering the two LZSS
    # leaves moved the native total by 8,382,014 where the routines themselves
    # execute 7,113,522.
    0x80CDF4: 'the real start of what the ranking credits to $80:CDEB',
    # $81:81A2 ends at its RTS on $81:81EE, with a two-instruction alternate
    # tail at $81:81EF; the entry itself executes 3,234 instructions in ten
    # profiles. Everything the ranking credited to it -- 4,671,086, or 1.46% --
    # belongs to what starts here, which is not a subroutine at all: it opens
    # `PEI $02 : PLB`, drops into a loop around `JSL thread_yield`, and is
    # entered by the scheduler resuming a thread rather than by any JSR.
    # Without this line, `$81:81A2` reads as the top portable row on the whole
    # ranking at 1.6% over 79 calls, and it is neither.
    0x8181F6: 'the real start of what the ranking credits to $81:81A2',
    # $80:C8B8 dma_to_vram is fifteen instructions and ends at its RTL on
    # $80:C8DB: 68,355 instructions over 4,557 calls, 0.02%. The 0.91% the
    # ranking added to that begins here, and this is a thread body too --
    # $80:87C1, inside the level loader, does `LDA #$C8F6 : LDY #$0080 :
    # JSL thread_spawn`, and nothing in the ROM JSRs or JSLs to it or holds a
    # pointer to it. It opens `JSR object_list_parse` and then loops on
    # `JSL thread_yield`, spawning objects near the camera centre. Note that
    # its direct page is a thread's own 128-byte page, so the $0C/$0E/$10/$12
    # the listing annotates as vbl_queue_* are nothing of the kind -- they are
    # this thread's locals, and the symbol names are actively misleading there.
    # Between them the two routines at $80:C8DB and $80:C8DC are also the
    # tidiest example of why the listing is a lower bound: $80:C8DC is a 16x16
    # hardware multiply through $211B/$2134, entirely .db, and executed zero
    # times in all ten profiles.
    0x80C8F6: 'the real start of what the ranking credits to $80:C8B8',
}

BLOCKED = {
    0x80CD20: 'written, unregisterable: it outlives a frame',
    0x808353: 'the coroutine primitive the harness measures passes against',
    0x8083E0: 'a dispatcher -- RTLs into any of 13 jobs held in WRAM',
    0x80843D: 'a dispatcher -- the same, for the one job queue B carries',
    0x80816C: 'the NMI entry point, which is not called and cannot be shimmed',
}

# ...and every vblank job, for the same reason as the NMI entry: the dispatcher
# reaches it by `RTL`, so there is no call for the harness to substitute. See
# VBL_JOBS below for how the list is derived.
BLOCKED.update({a: 'a vblank job -- reached by RTL from a queue, never called'
                for a in ()})  # filled in below, once VBL_JOBS exists


# Every vblank job in the ROM, and a whole class of misattribution.
#
# The two queues store a job as **`addr - 1`** and the dispatchers reach it by
# pushing that and executing `RTL` (`$80:83E0`, `$80:843D`). So a vblank job is
# never the target of a `JSR` or a `JSL`, the call graph therefore has no edge
# into it, and attribution-by-nearest-preceding-entry always credits it to
# whatever subroutine happens to sit below it in the ROM. Every one of these is
# invisible to the ranking until it is declared, and they are not small: five of
# them stacked on `$82:8138` turned a 105,000-instruction palette copier into a
# 0.6 per cent row, which is how the class was found.
#
# This is the same structural problem as `$81:81F6` and `$80:C8F6` -- code that
# runs and is never called -- and it is the third and largest family of it. The
# other two had to be found one at a time. This one does not, because the ROM
# registers a job with a fixed three-instruction idiom:
#
#     LDA #$<addr> : LDY #$00<bank> : JSL $8083AE    (queue A)
#                                     JSL $80841 8   (queue B)
#
# so the set below is every match for it in the cartridge, and reproducing it is
# a twelve-line script rather than an afternoon of reading. A job that is also a
# registry entry (`$80:9E7B vram_queue_flush`) is already a boundary and appears
# here harmlessly.
#
# They are also **unportable for the same reason they are invisible**: the
# harness substitutes per call, and nothing calls these. So they go into BLOCKED
# too, alongside the NMI entry and the two dispatchers that run them.
VBL_JOBS = frozenset((
    0x808B17, 0x808B2B, 0x808B48, 0x808B5C, 0x808B70, 0x808B82,
    0x80938E, 0x80953B, 0x809A1B, 0x809A90, 0x809BFC, 0x809C63,
    0x809C7D, 0x809CB2, 0x809E3E, 0x809E7B, 0x809ED0, 0x809F62,
    0x809FDF, 0x80A084, 0x80A09E, 0x80A2AB, 0x80AC55, 0x80C2AB,
    0x80EC7A, 0x80ECA0, 0x828163, 0x82819A, 0x8281C9, 0x828209,
    0x828259, 0x8282B5, 0x828308, 0x828425, 0x82882C, 0x8288F0,
    0x828903, 0x828C49, 0x829644, 0x829657, 0x829CA6, 0x829D4D,
    0x82A9D1, 0x82A9E5, 0x82AE44, 0x82AEB4, 0x82B1F9, 0x82B82E,
    0x82B9B6, 0x82D88C, 0x82DAA0, 0x82DADE, 0x82DC0C, 0x82E076,
    0x838255, 0x83B0EC, 0x83B100, 0x83C949, 0x83C95D,
))

# `$80:9E7B vram_queue_flush` is the one exception in the fifty-nine, and it is
# worth stating rather than hiding: `$80:A676` registers it as a queue-A job in
# the ordinary way, *and* `$80:81A2` inside the NMI calls it outright, 60,940
# times over the corpus. It is therefore the one job the harness can substitute,
# because there is a call to intercept -- so it is an entry like the rest but is
# not blocked. It is also the only one of the fifty-nine with an inbound edge in
# the call graph, which is how it was found.
VBL_JOB_CALLED = frozenset({0x809E7B})

BLOCKED.update({a: 'a vblank job -- reached by RTL from a queue, never called'
                for a in VBL_JOBS - VBL_JOB_CALLED if a not in BLOCKED})


# Every thread body in the ROM, and the second family of "runs and is never
# called" reduced to a script.
#
# `$80:825E thread_spawn` takes the far entry in `A:Y`, and its second
# instruction is `DEC` -- it parks **`addr - 1`** in a nine-byte frame that the
# scheduler resumes with `RTL`, which is precisely what the two vblank queues do
# with a job. So a thread body has no inbound call edge either, the call graph
# cannot see it, and attribution-by-nearest-preceding-entry credits it to
# whatever subroutine sits below it. `$81:81F6` and `$80:C8F6` were each found
# by hand, a round apart, after a ranking row made no sense; this is the same
# search done once.
#
# The spawn idiom is `LDA #$<addr> : LDY #$00<bank> : JSL $80825E`, and matching
# it over the cartridge resolves 32 of the 67 spawn sites. Three more are
# spawned from tables that are themselves in ROM and are expanded below.
#
# **The remaining four read the address from data, and this set is therefore a
# lower bound in a way `VBL_JOBS` is not.** `$80:8774` and `$80:87FB` take it
# from bank `$9F` -- per-level data, so which threads a level starts is a
# property of the level and not of the code -- `$81:80E7` takes it from WRAM,
# and `$81:81D7` walks a list through `($0C),Y`. Anything they start that is not
# already below stays misattributed, and there is no static way to find it. The
# honest claim is "every thread body the code names", not "every thread body".
THREAD_BODIES = frozenset((
    # from `LDA #imm : LDY #imm : JSL thread_spawn`
    0x8084B1, 0x80A36E, 0x80C8F6, 0x8180EC, 0x8181F6, 0x81ABF5,
    0x81B4EA, 0x81B664, 0x81CF10, 0x81D4C9, 0x81E72C, 0x81EEB7,
    0x81F159, 0x81F2B2, 0x81F380, 0x82D8DB, 0x82DCA0, 0x82DEFB,
    0x82DF6B, 0x82E0B4, 0x82F03E, 0x82F1E6, 0x82F49E, 0x82F6EB,
    0x82F70B, 0x839776, 0x83B1DD, 0x83B277, 0x83B55E, 0x83B8F8,
    0x83C687, 0x83CA4A,
    # ...plus `$80:ED8C`, eight six-byte records of (addr, bank, parameter),
    # read at `$80:ED74`. `$81:F380` appears here and above, which is the
    # cross-check that says the record layout was read correctly.
    0x81FCB2, 0x81FAF5, 0x81E8A8, 0x81EAE6, 0x81EBE2, 0x81F976,
    0x81F55E,
    # ...and `$82:C209`, twelve four-byte records read at `$82:C0ED` and
    # `$82:C131`, the first of them a null entry. `$83:9776` is the same kind of
    # cross-check.
    0x839699, 0x839843, 0x83993D, 0x839C6D, 0x839D00, 0x839E15,
    0x839EBE, 0x839FE2, 0x839A89, 0x839BAD,
))

# Nothing in the ROM calls a thread body, so none of them can be a registry
# entry and all of them are blocked -- unlike `VBL_JOBS`, which had one
# exception. They stay in the denominator because the work is real.
BLOCKED.update({a: 'a thread body -- resumed by RTL from a parked frame, never called'
                for a in THREAD_BODIES if a not in BLOCKED})

def rom_to_snes(off):
    return ((0x80 + off // BANK_SIZE) << 16) | (0x8000 + off % BANK_SIZE)


def snes_to_rom(addr):
    bank, lo = addr >> 16, addr & 0xFFFF
    if lo < 0x8000:
        return None
    return (bank & 0x7F) * BANK_SIZE + (lo - 0x8000)


def load_ported(path='src/cosim/routines.c'):
    """The registry's entry points -- the addresses the harness intercepts."""
    src = open(path, encoding='utf-8', errors='replace').read()
    out = {}
    for m in re.finditer(r'\.name\s*=\s*"([^"]+)".*?\.entry\s*=\s*0x([0-9a-fA-F]+)',
                         src, re.S):
        out[int(m.group(2), 16)] = m.group(1)
    return out


def load_symbols(path='tools/symbols/zamn.sym'):
    names = {}
    if not os.path.exists(path):
        return names
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.split('#')[0].strip()
        m = re.match(r'^\$([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+(\S+)', line)
        if m:
            names[(int(m.group(1), 16) << 16) | int(m.group(2), 16)] = m.group(3)
    return names


def load_profile(path):
    with open(path, 'rb') as f:
        blob = f.read()
    if blob[:4] != b'ZPRF':
        raise SystemExit('%s: not a ZPRF profile' % path)
    ver, size = struct.unpack('<II', blob[4:12])
    if ver != 1:
        raise SystemExit('%s: unsupported version %d' % (path, ver))
    both = np.frombuffer(blob, dtype='<u4', count=2 * size, offset=12)
    return both[:size].astype(np.int64), both[size:].astype(np.int64)


def load_cdl(path):
    with open(path, 'rb') as f:
        blob = f.read()
    if blob[:4] != b'ZCDL':
        raise SystemExit('%s: not a ZCDL log' % path)
    _, size = struct.unpack('<II', blob[4:12])
    return np.frombuffer(blob, dtype=np.uint8, count=size, offset=12)


def load_extra_entries(path):
    """Addresses that are routines but never `JSR`/`JSL` targets.

    **Thread bodies are invisible to a call-graph profiler**, and on this game
    that is most of the cast. The scheduler enters an actor's behaviour through
    an indirect jump out of `thread_call_handler`, so the CDL never marks it
    `CDL_SUB`, no edge in `callgraph.csv` names it, and every instruction it
    executes is credited to whatever subroutine happens to sit above it in the
    bank. Ten actor behaviours and six victim behaviours -- the whole placed
    population of the fourteen password levels -- scored exactly zero before
    this existed, which is the signature of the bug rather than of idle code.

    Feed it the behaviour column of `zamn_assets actors`, one `$BB:AAAA` per
    line; `#` comments and anything else are ignored.
    """
    out = set()
    for line in open(path, encoding='utf-8', errors='replace'):
        for m in re.finditer(r'\$([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})',
                             line.split('#')[0]):
            addr = (int(m.group(1), 16) << 16) | int(m.group(2), 16)
            off = snes_to_rom(addr)
            if off is not None:
                out.add(off)
    return out


def main(dirs, extra=None):
    ported = load_ported()
    symbols = load_symbols()
    extra_entries = load_extra_entries(extra) if extra else set()

    total_exec = None
    total_call = None
    flags = None
    per_movie = []

    for d in dirs:
        prof = os.path.join(d, 'profile.bin')
        cdl = os.path.join(d, 'zamn.cdl')
        if not (os.path.exists(prof) and os.path.exists(cdl)):
            continue
        execs, calls = load_profile(prof)
        f = load_cdl(cdl)
        if total_exec is None:
            total_exec, total_call, flags = execs.copy(), calls.copy(), f.copy()
        else:
            total_exec += execs
            total_call += calls
            flags |= f
        per_movie.append((os.path.basename(d), int(execs.sum())))

    if total_exec is None:
        raise SystemExit('no profile.bin found in: %s' % ', '.join(dirs))

    size = len(total_exec)
    ported_offsets = {off for a in ported
                      if (off := snes_to_rom(a)) is not None}

    # Boundaries: every JSR/JSL target the corpus actually reached, plus the
    # registry's entries in case one was only ever jumped to, plus the ones
    # above that nothing calls at all.
    jump_offsets = {off for a in set(JUMP_ENTRIES) | VBL_JOBS | THREAD_BODIES
                    if (off := snes_to_rom(a)) is not None}
    entries = np.array(sorted(
        set(np.nonzero(flags & CDL_SUB)[0].tolist())
        | ported_offsets | extra_entries | jump_offsets), dtype=np.int64)

    # Nearest preceding entry, rejected when it lies in another bank -- the
    # first bytes of a bank can precede every entry in it.
    idx = np.searchsorted(entries, np.arange(size, dtype=np.int64), 'right') - 1
    owner = np.where(idx >= 0, entries[np.clip(idx, 0, None)], -1)
    same_bank = (owner >= 0) & (owner // BANK_SIZE == np.arange(size) // BANK_SIZE)
    idx = np.where(same_bank, idx, -1)

    is_code = (flags & CDL_CODE) != 0
    ok = idx >= 0
    exec_sums = np.bincount(idx[ok], weights=total_exec[ok],
                            minlength=len(entries)).astype(np.int64)
    code_sums = np.bincount(idx[ok & is_code],
                            minlength=len(entries)).astype(np.int64)

    unattributed_exec = int(total_exec[~ok].sum())
    exec_by_routine = {int(entries[i]): int(v)
                       for i, v in enumerate(exec_sums) if v}
    code_by_routine = {int(entries[i]): int(v)
                       for i, v in enumerate(code_sums) if v}

    # --- what substitution actually removes -------------------------------
    #
    # The registry's 47 entries are not the whole of what runs natively. The
    # ROM splits work into subroutines the port inlines: `$80:BA51 sprite_emit`
    # is called 16,221 times from inside `sprite_build_oam`'s body, and under
    # `zamn_cosim run` it never executes at all, because the port served its
    # caller. Counting only registry entries therefore understates the share by
    # everything they subsume.
    #
    # So take the call graph and close over it: a routine is subsumed when it
    # has callers and **every** one of them is already ported or subsumed. A
    # routine reached from both sides -- some calls from ported code, some from
    # the ROM proper -- is not subsumed, and is reported separately as the band
    # the answer lies in.
    def owner_of(off):
        k = int(np.searchsorted(entries, off, 'right')) - 1
        if k < 0:
            return None
        e = int(entries[k])
        return e if e // BANK_SIZE == off // BANK_SIZE else None

    callers = collections.defaultdict(collections.Counter)
    for d in dirs:
        path = os.path.join(d, 'callgraph.csv')
        if not os.path.exists(path):
            continue
        for line in open(path, encoding='utf-8', errors='replace'):
            m = re.match(r'\$([0-9A-F]{2}):([0-9A-F]{4}),'
                         r'\$([0-9A-F]{2}):([0-9A-F]{4}),(\d+)', line.strip())
            if not m:
                continue
            src = snes_to_rom((int(m.group(1), 16) << 16) | int(m.group(2), 16))
            dst = snes_to_rom((int(m.group(3), 16) << 16) | int(m.group(4), 16))
            if src is None or dst is None:
                continue
            o = owner_of(src)
            if o is not None:
                callers[dst][o] += int(m.group(5))

    # A routine reached from both sides is neither wholly native nor wholly
    # not: it runs for the calls the ROM still makes and not for the ones the
    # port serves. So carry a fraction rather than a flag, weight it by how
    # many calls came from where, and iterate until it settles -- registry
    # entries pinned at 1, anything with no caller at all (`nmi_entry`, reset)
    # at 0. A routine every one of whose callers is native reaches 1 on its
    # own, which is the all-or-nothing closure as a special case.
    frac = {r: (1.0 if r in ported_offsets else 0.0) for r in exec_by_routine}
    for _ in range(200):
        delta = 0.0
        for r in exec_by_routine:
            if r in ported_offsets:
                continue
            cs = callers.get(r)
            if not cs:
                continue
            tot = sum(cs.values())
            v = sum(n * frac.get(c, 0.0) for c, n in cs.items()) / tot
            delta = max(delta, abs(v - frac[r]))
            frac[r] = v
        if delta < 1e-9:
            break

    native = {r for r in exec_by_routine if frac[r] > 0.999}
    mixed = {r for r in exec_by_routine if 0.001 < frac[r] <= 0.999}
    subsumed = native - ported_offsets

    tot_exec = int(total_exec.sum())
    tot_code = int(is_code.sum())
    nat_exec = sum(v for e, v in exec_by_routine.items() if e in native)
    nat_code = sum(v for e, v in code_by_routine.items() if e in native)
    direct_exec = sum(v for e, v in exec_by_routine.items() if e in ported_offsets)
    mixed_exec = sum(exec_by_routine.get(e, 0) for e in mixed)
    # The point estimate: every routine credited by its native call fraction.
    weighted_exec = sum(v * frac[e] for e, v in exec_by_routine.items())

    print('Traced %d movie(s), %s instructions total\n'
          % (len(per_movie), '{:,}'.format(tot_exec)))
    for name, n in per_movie:
        print('  %-22s %14s' % (name, '{:,}'.format(n)))

    print('\n%s' % ('=' * 66))
    print('NATIVE SHARE')
    print('=' * 66)
    pct = lambda a, b: 100.0 * a / max(b, 1)
    print('  static  (distinct code bytes executed) %9s / %-9s %5.1f%%'
          % ('{:,}'.format(nat_code), '{:,}'.format(tot_code),
             pct(nat_code, tot_code)))
    print('  dynamic (instructions executed)        %9s / %-9s %5.1f%%'
          % ('{:,}'.format(nat_exec), '{:,}'.format(tot_exec),
             pct(nat_exec, tot_exec)))
    print('\n  of the dynamic share:')
    print('    %5.1f%%  in the %d registry entries themselves'
          % (pct(direct_exec, tot_exec), len(ported_offsets)))
    print('    %5.1f%%  in %d routines they subsume -- every caller is ported'
          % (pct(nat_exec - direct_exec, tot_exec), len(subsumed)))
    print('    %5.1f%%  in %d routines called from both sides, credited by'
          ' call share' % (pct(mixed_exec, tot_exec), len(mixed)))
    print('\n  best estimate, every routine weighted by its native call'
          ' fraction: %5.1f%%' % pct(weighted_exec, tot_exec))

    # --- and the part of the denominator that is not work at all -----------
    wait_rows = []
    # ...and which routine each wait belongs to, by the same attribution the
    # rest of the report uses, so the ranking below can charge it back.
    wait_by_routine = collections.Counter()
    for addr, span, what in WAIT_SITES:
        off = snes_to_rom(addr)
        if off is None:
            continue
        n = int(total_exec[off:off + span].sum())
        if n:
            wait_rows.append((n, addr, what))
        for b in range(off, off + span):
            if total_exec[b] and idx[b] >= 0:
                wait_by_routine[int(entries[idx[b]])] += int(total_exec[b])
    wait_total = sum(n for n, _, _ in wait_rows)
    if wait_rows:
        print('\n  of the %s instructions above, this many are a CPU waiting'
              ' rather than working:' % '{:,}'.format(tot_exec))
        for n, addr, what in sorted(wait_rows, reverse=True):
            print('    %5.2f%%  $%02X:%04X  %s'
                  % (pct(n, tot_exec), addr >> 16, addr & 0xFFFF, what))
        print('    ------')
        print('    %5.2f%%  total. Porting any of it would replace a spin with'
              ' a spin, so the' % pct(wait_total, tot_exec))
        print('             honest denominator is the one with it removed:')
        print('\n  dynamic share, waits out of the denominator: %5.1f%%'
              % pct(nat_exec, tot_exec - wait_total))
        print('  best estimate, likewise:                     %5.1f%%'
              % pct(weighted_exec, tot_exec - wait_total))
    print('\n  %d routine entries used for attribution; %s instructions (%.1f%%)'
          % (len(entries), '{:,}'.format(unattributed_exec),
             pct(unattributed_exec, tot_exec)))
    print('  fell before the first entry in their bank and are counted as'
          ' unported.')

    def label(off):
        a = rom_to_snes(off)
        s = '$%02X:%04X' % (a >> 16, a & 0xFFFF)
        nm = ported.get(a) or symbols.get(a)
        return '%s %s' % (s, nm) if nm else s

    print('\n%s' % ('=' * 66))
    print('TOP UNNATIVE ROUTINES BY WORK (registry entries + subsumed)')
    print('=' * 66)
    # Work, not instructions: a routine is charged only for what it does, so a
    # spin loop cannot buy its way to the top of the list of what to port next.
    tot_work = tot_exec - wait_total
    rows = [(v - wait_by_routine.get(e, 0), e)
            for e, v in exec_by_routine.items() if e not in native]
    rows = [r for r in rows if r[0] > 0]
    rows.sort(reverse=True)
    run = 0
    blocked_work = 0
    seen_blocked = []
    print('  %-34s %12s %6s %8s %8s'
          % ('routine', 'work', 'share', 'cumul.', 'calls'))
    for v, e in rows[:30]:
        run += v
        addr = rom_to_snes(e)
        why = BLOCKED.get(addr)
        if why:
            blocked_work += v
            seen_blocked.append((addr, why))
        waited = wait_by_routine.get(e, 0)
        note = '  (+%s waiting)' % '{:,}'.format(waited) if waited else ''
        print(' %s%-34s %12s %5.1f%% %7.1f%% %8s%s'
              % ('!' if why else ' ', label(e), '{:,}'.format(v),
                 100.0 * v / tot_work, 100.0 * (nat_exec + run) / tot_work,
                 '{:,}'.format(int(total_call[e])), note))
    print('\n  "cumul." is the native share this run would reach if every routine'
          '\n  down to that row were ported and nothing else changed.')
    if seen_blocked:
        print('\n  ! -- %s of the work above, %.1f%% of everything the game does,'
              % ('{:,}'.format(blocked_work), 100.0 * blocked_work / tot_work))
        print('  is in routines the harness structurally cannot take -- which is'
              '\n  not the same as cannot be written. What none of them can do is'
              '\n  pass through per-call substitution, because nothing calls them:'
              '\n  a dispatcher reaches a vblank job by RTL and the scheduler'
              '\n  reaches a thread body the same way, so there is no call to'
              '\n  intercept. They are the frame rather than something called'
              '\n  inside one, and they stay in the denominator because the work'
              '\n  is real:\n')
        for addr, why in seen_blocked:
            print('    $%02X:%04X  %s' % (addr >> 16, addr & 0xFFFF, why))

    print('\n%s' % ('=' * 66))
    print('NATIVE ROUTINES BY INSTRUCTIONS EXECUTED (registry entries + subsumed)')
    print('=' * 66)
    rows = [(v, e) for e, v in exec_by_routine.items() if e in native]
    rows.sort(reverse=True)
    for v, e in rows[:15]:
        print('  %-34s %12s %5.1f%%'
              % (label(e), '{:,}'.format(v), 100.0 * v / tot_exec))
    zero = [e for e in ported_offsets if exec_by_routine.get(e, 0) == 0]
    if zero:
        print('\n  %d ported routine(s) executed nothing in this run: %s'
              % (len(zero), ', '.join(sorted(label(e) for e in zero))))


if __name__ == '__main__':
    args = sys.argv[1:]
    extra = None
    if '--entries' in args:
        i = args.index('--entries')
        extra = args[i + 1]
        del args[i:i + 2]
    if not args:
        raise SystemExit(__doc__)
    main(args, extra)
