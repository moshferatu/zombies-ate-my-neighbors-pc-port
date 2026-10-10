r"""Price the straight runs a thread's stretches are made of.

A thread written as stretches (`src/port/boss_thread.h`) names every
straight run of the listing it can take, in one X-macro:

    #define BOSS_RUNS(X) \
      X(8A58, 0x828a5cu) \
      ...

Each row is where a run starts and where it ends, in the thread's own bank.
The port counts the runs a stretch took and the harness adds up what they
cost, so the harness needs a table with one price a row. This prints it:

    python tools\price_runs.py "Zombies Ate My Neighbors.sfc" ^
        src\port\boss_thread.h BOSS_RUN

`tools/cycles816.py` does the pricing, with the data bank the same as the
code's. A run that reads through a pointer needs to be told where that
points, as `--ind=8EE0=82:8F52` for the run that starts at `$8EE0`.

A run has to be straight: a branch, a jump or a return can only be its last
instruction, and the one exception is a `BRA` to the instruction after it.
A run that is not is refused, because its price would not be one number.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENDS_A_RUN = {'BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ', 'BRA',
              'BRL', 'JMP', 'JML', 'RTS', 'RTL', 'RTI'}


def main():
    rom, header, prefix = sys.argv[1:4]
    indirect = {}
    for arg in sys.argv[4:]:
        m = re.fullmatch(r'--ind=([0-9A-Fa-f]{4})=(\w\w:\w{4})', arg)
        if not m:
            sys.exit('what is %s?' % arg)
        indirect[int(m.group(1), 16)] = m.group(2)

    text = open(header, encoding='utf-8').read()
    block = re.search(r'#define %sS\(X\) \\\n((?:  X\(.*\n)+)' % prefix, text)
    if not block:
        sys.exit('no %sS(X) in %s' % (prefix, header))
    rows = re.findall(r'X\(([0-9A-F]{4}), 0x([0-9a-f]{2})([0-9a-f]{4})u\)',
                      block.group(1))

    for start, bank, end in rows:
        a, b = int(start, 16), int(end, 16)
        cmd = [sys.executable, os.path.join(HERE, 'cycles816.py'), rom,
               '%s:%04X' % (bank, a), str(b - a), '--db=' + bank]
        if a in indirect:
            cmd.append('--ind=' + indirect[a])
        lines = subprocess.run(cmd, capture_output=True, text=True,
                               check=True).stdout.strip().splitlines()
        total = re.match(r'(\d+) master cycles over (\d+) FastROM bytes, '
                         r'(\d+) direct', lines[-1])
        names = []
        for line in lines[:-1]:
            fields = line.split()[1:]
            names.append(next(f for f in fields if re.fullmatch(r'[A-Z]{3}', f)))
        inside = names[:-1]
        if names[:1] == ['BRA'] and len(names) == 2:
            inside = []  # a `BRA` to the next instruction
        if any(n in ENDS_A_RUN for n in inside):
            sys.exit('$%s:%04X-%04X is not straight: %s' % (
                bank, a, b, ' '.join(names)))
        print('    [%s_%s] = {%s, %s, %s},  // %s' % (
            prefix, start, total.group(1), total.group(2), total.group(3),
            ' : '.join(names)))


if __name__ == '__main__':
    main()
