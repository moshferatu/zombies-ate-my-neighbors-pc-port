r"""Run the sessions `tools/code_map.py` reads.

Each corpus movie at its length in `verify_corpus.ps1`, and each level
started with `--level N` under `level1.zmv` for 4700 frames. Then what no
movie reaches, by a `--poke`:

  * four game overs, the player's health held at nought: one with a low
    score, two that type a name for the top scores (`game-over-name.zmv`),
    and one with both players' scores going in;
  * the Snakeoid's two levels under `hold-y.zmv`, a player standing still
    for it to bite, with their health held up;
  * level 20 twice more with the Snakeoid moved, on one frame, to where it
    bites: from under the player, and from the side above them, where their
    water hits it until it dies.

Each is run twice: as the stock ROM under the tracer, and as the game with
`--profile`.

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
# Health held at nought from frame 1900: the game over, the name entry (which
# times out) and the table filed by about frame 5000, then the title.
DEAD = ['--poke', '1900+:1CB8=0000']
SCORE = ['--poke', '1900+:1E72=4321', '--poke', '1900+:1E74=0765']
OVER_FRAMES = 6500
ALIVE = ['--poke', '1600+:1CB8=000A']
# The Snakeoid's place on its page, `$0700`, at frame 1800 while it chases:
# four pixels left of the player standing at (695, 1371), and 17 below and 30
# right, which is the place it bites from above. See `port/snakeoid.h`.
UNDER = ['--poke', '1800:728=02B3', '--poke', '1800:72A=055B']
ABOVE = ['--poke', '1800:728=02D5', '--poke', '1800:72A=056C']
# Both players of `level1-2p.zmv` dead, and both scores going in: the second
# is asked after the first is filed.
BOTH_DEAD = ['--poke', '3000+:1CB8=0000', '--poke', '3000+:1CBA=0000',
             '--poke', '3000+:1E72=4321', '--poke', '3000+:1E74=0765',
             '--poke', '3000+:1E76=0000', '--poke', '3000+:1E78=0800']


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
    out.append(('over_score', 'game-over-name.zmv', OVER_FRAMES, DEAD + SCORE))
    out.append(('over_low', 'level1.zmv', OVER_FRAMES, DEAD))
    out.append(('over_rec10', 'game-over-name.zmv', OVER_FRAMES,
                ['--level', '10'] + DEAD + SCORE))
    out.append(('over_2p', 'level1-2p.zmv', 9500, BOTH_DEAD))
    for n in (20, 40):
        out.append(('bitten%d' % n, 'hold-y.zmv', 7000, ['--level', str(n)] + ALIVE))
    out.append(('bitten_under', 'hold-y.zmv', 7000, ['--level', '20'] + ALIVE + UNDER))
    out.append(('bitten_above', 'hold-y.zmv', 7000, ['--level', '20'] + ALIVE + ABOVE))
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
