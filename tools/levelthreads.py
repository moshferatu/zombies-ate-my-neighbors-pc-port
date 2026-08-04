"""Which thread bodies does each level start?

`native_share.py` keeps `THREAD_BODIES` because a thread body has no inbound call
edge -- the scheduler resumes it with `RTL` from a parked frame -- so
attribution-by-nearest-preceding-entry credits its work to whatever subroutine
happens to sit below it in the ROM. Every body missing from that set is a
misattributed row in the ranking, and `$82:BB0D` was one: a 0.3% row with ten
calls whose work was 7 KB downstream in `$82:D7CF`.

Most bodies are named by an immediate (`LDA #$<addr> : LDY #$00<bank> : JSL
$80825E`) and a few by tables in bank `$80`/`$82`. The ones this script finds are
named by **per-level data**, which is why the immediate scan missed them:

  * `$80:886D` is `LDA $9F8002,X : STA $10` with X = level*2, so `$9F:8002` is a
    table of level-record bases in bank `$9F`, and `$10` is the base the rest of
    the loader indexes off. Index 56 holds `$8000`, the address of the table
    itself, and is the sentinel -- 56 real records, levels 0..55.
  * `$80:8774` spawns the single far entry at record offset **$18/$1A** when it
    is non-zero.
  * `$80:87CB` is `CLC : LDA #$003C : ADC $10 : PHA`, and the loop above it walks
    **eight-byte (entry far, parameter far) records** from offset **$3C**,
    spawning each and stopping at a zero entry. A body may be listed more than
    once in one level with different parameters.

The check that these are threads and not mis-parsed data is that every one of
them reaches `JSL $808353 thread_yield` within a few dozen bytes of its entry.
**Only a thread yields.** The script asserts the rest: every entry is a code
pointer into banks $80..$83, and every list terminates.

    python tools/levelthreads.py "Zombies Ate My Neighbors.sfc"

Two spawn sites stay out of reach and keep `THREAD_BODIES` a lower bound:
`$81:80E7` takes the address from WRAM, and `$81:81D7` walks a list through
`($0C),Y`.
"""
import collections
import re
import sys

BANK_SIZE = 0x8000
LEVEL_TABLE = 0x8002       # in bank $9F, indexed by level*2
N_LEVELS = 56              # index 56 is the sentinel
OFF_SINGLE = 0x18          # one far entry, spawned at $80:8774
OFF_LIST = 0x3C            # (entry, parameter) records, spawned at $80:87FB
LIST_STRIDE = 8
YIELD = bytes([0x22, 0x53, 0x83, 0x80])   # JSL $808353 thread_yield


def rom_offset(bank, addr):
    return (bank & 0x7F) * BANK_SIZE + (addr - 0x8000)


def word(rom, bank, addr):
    o = rom_offset(bank, addr)
    return rom[o] | (rom[o + 1] << 8)


def is_code_pointer(lo, bank):
    return 0x80 <= (bank & 0xFF) <= 0x83 and lo >= 0x8000 and bank >> 8 == 0


def yield_offset(rom, far, window=0x400):
    """How far past `far` the first `thread_yield` is, or None."""
    o = rom_offset(far >> 16, far & 0xFFFF)
    k = rom[o:o + window].find(YIELD)
    return None if k < 0 else k


def scan(rom):
    """-> (single, listed), each {far entry: [level, ...]}."""
    single, listed = collections.OrderedDict(), collections.OrderedDict()
    for lvl in range(N_LEVELS):
        base = word(rom, 0x9F, LEVEL_TABLE + lvl * 2)
        assert base >= 0x8000, 'level %d: record base $%04X is not in ROM' % (lvl, base)

        lo, bank = word(rom, 0x9F, base + OFF_SINGLE), word(rom, 0x9F, base + OFF_SINGLE + 2)
        if lo | bank:
            assert is_code_pointer(lo, bank), \
                'level %d: offset $18 is $%02X:%04X, not a code pointer' % (lvl, bank & 0xFF, lo)
            single.setdefault((bank & 0xFF) << 16 | lo, []).append(lvl)

        cur = base + OFF_LIST
        for _ in range(LIST_STRIDE + 1):
            lo, bank = word(rom, 0x9F, cur), word(rom, 0x9F, cur + 2)
            if (lo | bank) == 0:
                break
            assert is_code_pointer(lo, bank), \
                'level %d: list entry is $%02X:%04X, not a code pointer' % (lvl, bank & 0xFF, lo)
            listed.setdefault((bank & 0xFF) << 16 | lo, []).append(lvl)
            cur += LIST_STRIDE
        else:
            raise AssertionError('level %d: spawn list did not terminate' % lvl)
    return single, listed


def known_bodies(path='tools/native_share.py'):
    src = open(path, encoding='utf-8').read()
    block = re.search(r'THREAD_BODIES = frozenset\(\((.*?)\n\)\)', src, re.S)
    if not block:
        return set()
    return {int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{6})', block.group(1))}


def main():
    rom = open(sys.argv[1], 'rb').read()
    single, listed = scan(rom)
    known = known_bodies()
    new = []

    for title, table in (('$18, one entry, spawned at $80:8774', single),
                         ('$3C, a null-terminated list, spawned at $80:87FB', listed)):
        print('=== record offset %s ===' % title)
        for far in sorted(table):
            levels = table[far]
            k = yield_offset(rom, far)
            assert k is not None, '$%06X has no thread_yield -- not a thread body?' % far
            if far not in known:
                new.append(far)
            print('  $%02X:%04X  %2d level(s)  thread_yield at +$%03X  %s'
                  % (far >> 16, far & 0xFFFF, len(levels), k,
                     'known' if far in known else '** not in THREAD_BODIES **'))
        covered = {lvl for levels in table.values() for lvl in levels}
        print('  %d level(s) spawn none here\n' % (N_LEVELS - len(covered)))

    print('%d level records parsed; every entry a code pointer, every list '
          'terminated,\nevery body reaches thread_yield.' % N_LEVELS)
    if new:
        print('\n%d body(ies) not in THREAD_BODIES:' % len(new))
        print('    ' + ', '.join('0x%06X' % a for a in sorted(new)))


if __name__ == '__main__':
    main()
