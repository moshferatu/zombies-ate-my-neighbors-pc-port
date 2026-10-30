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
    water hits it until it dies;
  * level 48 twice with the player moved beside Frankenstein's monster, which
    wakes it: once moved about by more pokes, near, middling and far, and
    once firing down at it with three weapons in turn until it dies;
  * the screen after level 21 three times more, with what it counts poked
    as it begins: the neighbours saved, a tally two bonuses read, a life
    due, and the level's number, which decides the password and the level's
    own bonus.

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
# Frankenstein's monster is at (1120, 387) on level 48, and wakes for a player
# within 80. Under `level1.zmv` the player stands still from frame 2000, so
# they are moved: beside it, 180 to its right, 220 below, 500 to its right,
# and beside it again.
ROAM = ['--poke', '300+:1CB8=000A']
for frame, x, y in ((2000, 0x424, 0x183), (2400, 0x514, 0x183),
                    (3000, 0x460, 0x25F), (3600, 0x654, 0x183),
                    (4200, 0x424, 0x183)):
    ROAM += ['--poke', '%d:130=%04X' % (frame, x), '--poke', '%d:132=%04X' % (frame, y)]
# ...and under `hold-y.zmv`, 67 above it facing down and firing: weapon 1
# (its hits are id `$5D`), then 2 (`$5E`), then 5 (`$61`), which kills it.
# A later poke of the same word wins.
ARMED = ['--poke', '1800:130=0460', '--poke', '1800:132=0140',
         '--poke', '1600+:1CB8=000A',
         '--poke', '1700+:1CBC=0001', '--poke', '1700+:1CCE=0099',
         '--poke', '2300+:1CBC=0002', '--poke', '2300+:1CD0=0099',
         '--poke', '2900+:1CBC=0005', '--poke', '2900+:1CD6=0099']
# The screen after level 21 under `level21-exit.zmv` begins at frame 4700.
# Ten neighbours of kind 8, a UFO tally and a spawn count for two of the
# level's bonuses, and the next life at nought; then eleven saved and five
# lives on level 24, and four saved on level 29.
LEVEL_END = ['--poke', '4700:1FA0=0808', '--poke', '4700:1FA2=0808',
             '--poke', '4700:1FA4=0808', '--poke', '4700:1FA6=0808',
             '--poke', '4700:1FA8=0808', '--poke', '4700:1FAA=0008',
             '--poke', '4700:1FE4=0001', '--poke', '4700:1FF0=0001',
             '--poke', '4700:6054=0000']
LEVEL_END_PAID = LEVEL_END + ['--poke', '4700:1F9C=000A', '--poke', '4700:1E7C=0014']
LEVEL_END_CUT = LEVEL_END + ['--poke', '4700:1F9C=000B', '--poke', '4700:1D4C=0005',
                             '--poke', '4700:1E7C=0018']
LEVEL_END_FEW = LEVEL_END + ['--poke', '4700:1F9C=0004', '--poke', '4700:1E7C=001D']
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
    out.append(('frankenstein_roams', 'level1.zmv', 4700, ['--level', '48'] + ROAM))
    out.append(('frankenstein_armed', 'hold-y.zmv', 5000, ['--level', '48'] + ARMED))
    out.append(('level_end_paid', 'level21-exit.zmv', 7000, LEVEL_END_PAID))
    out.append(('level_end_cut', 'level21-exit.zmv', 7000, LEVEL_END_CUT))
    out.append(('level_end_few', 'level21-exit.zmv', 7000, LEVEL_END_FEW))
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
