r"""Run the sessions `tools/code_map.py` reads.

Each corpus movie at its length in `verify_corpus.ps1`, and each level started
with `--level N` under `level1.zmv` for 4700 frames, run twice: as the stock
ROM under the tracer, and as the game with `--profile`.

    python tools/code_map_survey.py <rom> <out> [--jobs 12] [--only stock|game] [--match re]
    zamn_cosim list --tsv > <out>/registry.tsv
    python tools/code_map.py <rom> <out>/registry.tsv <out>/stock --game <out>/game

The outputs are `<out>/stock/<name>` (the tracer's `zamn.cdl`, `callgraph.csv`
and `flows.csv`) and `<out>/game/<name>` (the game's profile). Each holds the
run's console output in `log.txt`. Both programs only read the ROM.

It takes about 25 minutes at twelve jobs. More than twelve at once has strained
the machine this was written on.
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEVELS = 56
LEVEL_FRAMES = 4700


def exe(*parts):
    path = os.path.join(REPO, *parts)
    if os.name == 'nt':
        path += '.exe'
    return path


def sessions():
    """(name, movie, frames, extra args) for each session."""
    src = open(os.path.join(REPO, 'tools', 'verify_corpus.ps1')).read()
    out = []
    for movie, frames in re.findall(r'"([\w.-]+\.zmv)"\s*=\s*(\d+)', src):
        out.append((movie[:-4], movie, int(frames), []))
    for n in range(LEVELS):
        out.append(('rec%d' % n, 'level1.zmv', LEVEL_FRAMES, ['--level', str(n)]))
    return out


def run(cmd, out_dir, env=None):
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'log.txt'), 'w') as log:
        code = subprocess.call(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
    return code


def one(rom, out, kinds, session):
    name, movie, frames, extra = session
    movie = os.path.join(REPO, 'movies', movie)
    failed = []
    if 'stock' in kinds:
        d = os.path.join(out, 'stock', name)
        cmd = [exe('build', 'zamn_trace'), rom, '-o', d, '-f', str(frames), '-m', movie] + extra
        if run(cmd, d):
            failed.append('stock ' + name)
    if 'game' in kinds:
        d = os.path.join(out, 'game', name)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        cmd = [exe('zamn'), rom, '-m', movie, '--frames', str(frames)] + extra + ['--profile', d]
        if run(cmd, d, env):
            failed.append('game ' + name)
    return failed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('rom')
    ap.add_argument('out')
    ap.add_argument('--jobs', type=int, default=12)
    ap.add_argument('--only', choices=('stock', 'game'))
    ap.add_argument('--match', help='run only the sessions whose name matches this regex')
    a = ap.parse_args()
    kinds = {a.only} if a.only else {'stock', 'game'}
    rom, out = os.path.abspath(a.rom), os.path.abspath(a.out)
    todo = [s for s in sessions() if not a.match or re.search(a.match, s[0])]
    failed = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        for i, f in enumerate(pool.map(lambda s: one(rom, out, kinds, s), todo), 1):
            failed += f
            print('\r%d/%d sessions' % (i, len(todo)), end='', flush=True)
    print()
    for f in failed:
        print('FAILED', f)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
