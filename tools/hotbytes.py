"""Where inside a routine did the instructions actually go?

`native_share.py` ranks routines by work, and three times now a row near the top
of that ranking has turned out to be a two- or three-instruction spin loop
wearing a routine's name. This is the check that tells them apart, and it is
worth running on any row before it is worth porting:

    python tools\\hotbytes.py 82 AC07 AF00 analysis\\prof\\level*

**The test is not "is the profile flat".** An unrolled routine with early exits
is not flat -- the counts descend along it as callers drop out -- and that is
perfectly healthy. The invariant that actually separates work from spinning is:

    the first byte of the routine is its entry, so its count is the call
    count, and **no byte inside a loop-free routine can exceed it**.

So the report gives every byte as a multiple of the entry's count. A routine
whose worst byte is 1.0x the entry has no loop at all and every instruction in
it is work the port must do -- `$82:90F7` is 46,563 calls with nothing above
46,563. A routine whose worst byte is 218,452x the entry is a spin wearing a
routine's name -- `$82:AC07` is ten calls and 2,184,522 executions of
`LDA $0016 : CMP #$0078 : BCC`, the level loader holding for 120 frames. That
one belongs in `WAIT_SITES`, not in the work ranking, because porting it
replaces a spin with a spin.

A high multiple is not automatically a wait -- a real loop over real data looks
the same from here -- so it is a prompt to read the disassembly, not a verdict.
What it does reliably is tell you which rows need reading.

Reads the same `profile.bin` `native_share.py` does: one 32-bit count per ROM
byte, incremented at the opcode byte, so summing a span gives instructions
executed rather than bytes touched. A LoROM offset is
`(bank & $7F) * $8000 + (addr - $8000)`.
"""
import glob
import struct
import sys


def lorom(bank, addr):
    return (bank & 0x7F) * 0x8000 + (addr - 0x8000)


def load(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic, _ver, size = struct.unpack_from('<4sII', data, 0)
    if magic != b'ZPRF':
        raise SystemExit('%s: not a ZPRF profile' % path)
    return struct.unpack_from('<%dI' % size, data, 12)


def main(argv):
    if len(argv) < 4:
        raise SystemExit(
            'usage: hotbytes.py <bank> <from> <to> <profile-dir>...\n'
            '   e.g. hotbytes.py 82 AC07 AF00 analysis/prof/*')

    bank = int(argv[0], 16)
    lo = int(argv[1], 16)
    hi = int(argv[2], 16)

    dirs = []
    for pattern in argv[3:]:
        dirs.extend(sorted(glob.glob(pattern)) or [pattern])

    span = None
    grand = 0
    used = 0
    for d in dirs:
        path = d.rstrip('/\\') + '/profile.bin'
        try:
            ex = load(path)
        except (FileNotFoundError, NotADirectoryError):
            continue
        used += 1
        grand += sum(ex)
        seg = ex[lorom(bank, lo):lorom(bank, hi)]
        span = list(seg) if span is None else [a + b for a, b in zip(span, seg)]

    if not used:
        raise SystemExit('no profile.bin found under: %s' % ', '.join(dirs))

    total = sum(span)
    print('$%02X:%04X..$%02X:%04X over %d profile(s): %s of %s instructions,'
          ' %.2f%% of everything'
          % (bank, lo, bank, hi, used, '{:,}'.format(total),
             '{:,}'.format(grand), 100.0 * total / grand if grand else 0.0))
    if not total:
        return

    # The first byte of the range is the entry, so its count is the call count
    # and everything else is best read as a multiple of it.
    calls = span[0]
    hot = sorted(((v, i) for i, v in enumerate(span) if v), reverse=True)
    print('  %-10s %14s %9s %9s %8s'
          % ('byte', 'executed', 'of span', 'of all', 'x entry'))
    for v, i in hot[:24]:
        mult = ('%.1fx' % (v / calls)) if calls else '-'
        print('  $%02X:%04X %14s %8.2f%% %8.2f%% %8s'
              % (bank, lo + i, '{:,}'.format(v), 100.0 * v / total,
                 100.0 * v / grand, mult))

    print()
    if not calls:
        print('  The first byte of the range never executed, so there is no'
              ' call count to\n  measure against -- point --from at the'
              ' routine\'s entry.')
        return

    worst, at = hot[0]
    ratio = worst / calls
    print('  entry $%02X:%04X ran %s times; the worst byte in the range ran'
          ' %.1fx that.' % (bank, lo, '{:,}'.format(calls), ratio))
    if ratio < 1.05:
        print('  NO LOOP -- nothing runs more than once per call, so all of'
              ' this is work.')
    else:
        print('  LOOP at $%02X:%04X -- read it before porting it. If it is'
              ' spinning on a flag\n  or a counter it belongs in WAIT_SITES,'
              ' not in the work ranking.' % (bank, lo + at))


if __name__ == '__main__':
    main(sys.argv[1:])
