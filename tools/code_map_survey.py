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
    once firing down at it with three weapons in turn until it dies; and
    a third time, after it is dead, with the vampire held under the
    player's fire for fifty frames and its strength at nought,
    so that one hit kills it; and a fourth time moved about, with the
    werewolf that comes out at the end left as a fatal bazooka hit leaves
    it, so that it dies as a player's kill;
  * level 31 once more with its three clones, on pages `$0300`, `$0400`
    and `$0600`, left as a fatal bazooka hit leaves them: the one that only
    hunts reads it and dies as a player's kill.
  * the screen after level 21 three times more, with what it counts poked
    as it begins: the neighbours saved, a tally two bonuses read, a life
    due, and the level's number, which decides the password and the level's
    own bonus;
  * the same screen seven times more with another level's record put in
    the main thread's page as it begins, `$0C10`, so that its list of
    bonuses is read instead, and the tallies they pay for poked: every
    bonus but MONSTER FROZEN, which no level lists. Three have the second
    player playing too.
  * what a player met when the game first ran with no 65816: the items of
    slots 1, 2, 3 and 8 and six of the random potion's eight draws, each
    stocked and selected by a `--poke` under `item-walk.zmv`; weapon 4's shot
    (`fire-walk.zmv`); the trampoline's other bounce (`trampoline.zmv`); weapon
    5's kick into the pool (`kick-pool.zmv`); a sparkle on level 48
    (`level48-sparkle.zmv`); and the monster pushing a hedge (`monster-wall.zmv`).
  * the cast after level 48, from level 1's door with `$1E7C` poked
    (`level48-cast.zmv`): once with no kills, left by the second pad's Start;
    once with three kinds' kills poked, counted up and left by the first
    pad's; and once with more than 999 zombies, which shows 999.
  * the giant spider of levels 36 and 48 (`port/giant_spider.h`), with the
    player poked beside its point: going about, laying spiders and webbing
    for 9000 frames; shot by each of thirteen weapons held in turn, on its
    fifth life, until it dies; blinking first with a player too far, its
    puffs with no room; and its lives and death poked on both levels, paid
    and not, with the wait for fewer than 18 records on level 48.
  * Dr. Tongue and his head, after the giant spider on level 48
    (`port/dr_tongue.h`), the spider killed by `--poke` as above: the head
    licking a player below it, hurt enough to charge, and dying with a tongue
    out; its lives and death poked, paid, and the door;
    shot from below by each of thirteen weapons, in two sessions; at a side
    of a player above it, licking and spitting across, first mirrored and
    then not; touched by a player; and the doctor blinking and crying with
    the player too far.

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
# ...and after it is dead, the vampire of the same level, on page `$0300`
# with its record at `$1A7A`: its strength at nought, and held under the
# player for fifty frames, at their place and on its record, until a shot
# hits it. It flies off, and when it is gone it is paid for as a player's
# kill (`ts_vampire_counted`).
VAMPIRE_KILLED = ARMED + ['--poke', '3600:330=0000',
                         '--poke', '3600+:130=0460', '--poke', '3600+:132=0140',
                         '--poke', '3600-3650:30E=0460', '--poke', '3600-3650:310=0168',
                         '--poke', '3600-3650:1A7C=0460', '--poke', '3600-3650:1A80=0168']
# ...and in `frankenstein_roams`, the werewolf that comes out at about frame
# 4300 on page `$0700`, as its handler `$81:AC92` leaves it after a hit of
# id `$61` that kills: the id parked at `$3E`, its health under nought, and
# its fate stepped down (`ts_werewolf_counted`).
WEREWOLF_KILLED = ROAM + ['--poke', '4400:73E=0061', '--poke', '4400:73C=FFF0',
                          '--poke', '4400:710=FFFF']
# Level 31's clones, as their handler `enemy_9063_collide` leaves them after
# a hit of id `$61` that kills: the id parked at `$30` and the health at
# `$22` under nought. Only the one that only hunts reads `$22`, and it ends
# as a player's kill (`ts_clone_unscored` after `death_pictures`).
CLONES_KILLED = []
for page in (0x300, 0x400, 0x600):
    CLONES_KILLED += ['--poke', '2500:%X=0061' % (page + 0x30),
                      '--poke', '2500:%X=FFF0' % (page + 0x22)]
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
# Level 1 under `level1.zmv` ended at frame 1500: no neighbours left, one
# saved, and the player out by the door a hundred frames on. The game
# reaches this door, and not `level21-exit.zmv`'s, which it plays apart.
LEVEL1_DOOR = ['--poke', '1490-1700:1F9C=0001', '--poke', '1500-1510:1D52=0000',
               '--poke', '1600-1700:1FB8=0001']
# The bonuses another level's record lists, by its record, and the tallies
# that pay them; `$1E8A` has the second player playing.
TALLY_BONUSES = {
    # TEN CHEERLEADER, ALIEN INVASION, PASS COMPLETION and the level's own.
    'cheerleaders': ('9804', ['1F9C=0006', '1F9E=0004', '1FC4=000A', '1FC6=000A',
                              '1E8A=0001']),
    # Ten saved, MASSIVE DESTRUCTION, NO BAZOOKA FIRED, CHAINSAW BEGONE.
    'massive': ('93A2', ['1F9C=000A', '1FC2=0032', '1FCC=0000', '1FCE=0001',
                         '1FD0=0004', '1FD2=0003', '1E8A=0001']),
    # Ten saved, NO BAZOOKA FIRED turned down, EXTERMINATION.
    'extermination': ('8965', ['1F9C=000A', '1FCC=0001', '1FD4=000A']),
    # Ten saved, FISH FRY.
    'fish_fry': ('B151', ['1F9C=000A', '1FD8=000A']),
    # Ten saved, VAMPIRE DESTROYED, FRANKENSTEIN DESTROY.
    'vampire': ('CE21', ['1F9C=000A', '1FEC=0001', '1FEE=0001', '1FE8=0002',
                         '1E8A=0001']),
    # ALIEN INVASION and MARTIAN BUBBLED.
    'martian': ('8D2E', ['1FDC=000A']),
    # Ten saved, WEED CUTTING.
    'weed': ('8072', ['1F9C=000A', '1FC8=012C']),
}


def tally_bonus_pokes(record, pokes):
    out = LEVEL_END + ['--poke', '4700:0C10=' + record]
    for p in pokes:
        out += ['--poke', '4700:' + p]
    return out


# An item in slot `s`, selected and stocked, and weapon 4 the same way.
def item(s):
    return ['--poke', '1750-1760:1CC0=%04X' % s,
            '--poke', '1750-1760:%04X=0099' % (0x1D0C + 2 * s)]


WEAPON4 = ['--poke', '1750-1760:1CBC=0004', '--poke', '1750-1760:1CD4=0999']
# The random potion's generator the frame before it is drunk, for each of its
# eight draws: the first is the monster, the fourth the wanderer.
RANDOM_DRAWS = {'monster': '007A', 'healed': '000B', 'poisoned': '00C4',
                'sped': '0030', 'ghost': '009F', 'wanderer': '0133'}

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
    out.append(('werewolf_killed', 'level1.zmv', 4700, ['--level', '48'] + WEREWOLF_KILLED))
    out.append(('clone_killed', 'level1.zmv', 4700, ['--level', '31'] + CLONES_KILLED))
    out.append(('frankenstein_armed', 'hold-y.zmv', 5000, ['--level', '48'] + ARMED))
    out.append(('vampire_killed', 'hold-y.zmv', 4500, ['--level', '48'] + VAMPIRE_KILLED))
    out.append(('level_end_paid', 'level21-exit.zmv', 7000, LEVEL_END_PAID))
    out.append(('level_end_cut', 'level21-exit.zmv', 7000, LEVEL_END_CUT))
    out.append(('level_end_few', 'level21-exit.zmv', 7000, LEVEL_END_FEW))
    for name, (record, pokes) in TALLY_BONUSES.items():
        out.append(('level_end_' + name, 'level21-exit.zmv', 7000,
                    tally_bonus_pokes(record, pokes)))
    # What a player met with no 65816 left to run it, 2026-10-30.
    for name, slot in (('shoes', 1), ('monster', 2), ('ghost', 3), ('box', 8)):
        out.append(('item_' + name, 'item-walk.zmv', 4200, item(slot)))
    for name, seed in RANDOM_DRAWS.items():
        out.append(('random_' + name, 'item-walk.zmv', 4200,
                    item(4) + ['--poke', '1799:0024=' + seed]))
    out.append(('weapon4', 'fire-walk.zmv', 3000, WEAPON4))
    out.append(('trampoline', 'trampoline.zmv', 2600,
                ['--poke', '1760:0130=01D6', '--poke', '1760:0132=0250']))
    out.append(('kick_pool', 'kick-pool.zmv', 2400,
                ['--poke', '1750-1760:1CBC=0005', '--poke', '1750-1760:1CD6=0099',
                 '--poke', '1790:0130=0240', '--poke', '1790:0132=01B8']))
    out.append(('sparkle48', 'level48-sparkle.zmv', 2400,
                ['--level', '48', '--poke', '1800:0130=0064',
                 '--poke', '1800:0132=0420']))
    out.append(('monster_wall', 'monster-wall.zmv', 2600,
                ['--poke', '1700-1710:1CC0=0002', '--poke', '1700-1710:1D10=0099']))
    # ...and, 2026-10-31, the main thread after level 1's door (`$80:84DB`),
    # and after level 49's, into the ending.
    out.append(('level1_door', 'level1.zmv', 3600, LEVEL1_DOOR))
    out.append(('credits_door', 'level1.zmv', 6000,
                LEVEL1_DOOR + ['--poke', '1500:1E7C=0031']))
    # ...and, 2026-11-01, the cast after level 48 (`$80:8BBB`), from the same
    # door.
    cast = LEVEL1_DOOR + ['--poke', '1500:1E7C=0030']
    out.append(('cast48', 'level48-cast.zmv', 8200, cast))
    out.append(('cast48_kills', 'level48-cast.zmv', 9200,
                cast + ['--poke', '1500:1F64=0005', '--poke', '1500:1F66=000C',
                        '--poke', '1500:1F6A=0065']))
    out.append(('cast48_most', 'level48-cast.zmv', 5000,
                cast + ['--poke', '1500:1F64=0400']))
    # ...and, the same day, the giant spider (`$83:AD36`), with the player
    # poked beside the point it waits at on each level. Its page is `$0500`
    # on level 48 and `$0700` on level 36: `$14` its lives, `$52` broken and
    # `$5A` what hit it.
    near48 = ['--level', '48', '--poke', '1800+:1CB8=000A',
              '--poke', '1800:130=00D2', '--poke', '1800:132=0180']
    near36 = ['--level', '36', '--poke', '1800+:1CB8=000A',
              '--poke', '1800:130=0118', '--poke', '1800:132=00F0']
    out.append(('spider48', 'level1.zmv', 9000, near48))
    armed = []
    for i in range(13):
        frame = 2100 + 450 * i
        armed += ['--poke', '%d+:1CBC=%04X' % (frame, i),
                  '--poke', '%d+:%04X=0099' % (frame, 0x1CCC + 2 * i)]
    out.append(('spider48_shot', 'hold-y.zmv', 8200,
                near48 + ['--poke', '2100+:130=00D2', '--poke', '2100+:132=0110',
                          '--poke', '3000:0514=0000'] + armed))
    out.append(('spider48_blink', 'level1.zmv', 3200,
                ['--level', '48', '--poke', '1800+:1CB8=000A',
                 '--poke', '1800-2300:130=00D2', '--poke', '1800-2300:132=0110',
                 '--poke', '1800-2300:1F52=0000', '--poke', '2301:132=0180',
                 '--poke', '2300-2500:00DE=00F0']))
    out.append(('spider48_killed', 'level1.zmv', 5000,
                near48 + ['--poke', '2300:0552=FFFF', '--poke', '2400-2700:0006=000F',
                          '--poke', '2500:0514=0000', '--poke', '2500:0552=FFFF',
                          '--poke', '2700:0514=FFFE', '--poke', '2700:0552=FFFF',
                          '--poke', '2700:055A=005C',
                          '--poke', '2850-3000:0006=0012']))
    out.append(('spider36_killed', 'level1.zmv', 3600,
                near36 + ['--poke', '2300:0752=FFFF',
                          '--poke', '2500:0714=0000', '--poke', '2500:0752=FFFF',
                          '--poke', '2700:0714=FFFE', '--poke', '2700:0752=FFFF']))
    out.append(('spider36_blink', 'level1.zmv', 3000,
                ['--level', '36', '--poke', '1800+:1CB8=000A',
                 '--poke', '1800-2300:130=0118', '--poke', '1800-2300:132=0060',
                 '--poke', '1800+:1F52=0003', '--poke', '2301:132=00F0']))
    # ...and, 2026-11-02, the bazooka fired up into one of level 36's tiles
    # with bits 1 and 5 (attributes `$63`, a row of them at y `$78`), which
    # calls `$82:DC57` for what it leaves. The player is held below it, and
    # the movie has them facing up there.
    out.append(('bazooka36', 'fire-up.zmv', 5600,
                ['--level', '36', '--poke', '1600+:1CB8=000A',
                 '--poke', '1750+:1CBC=0005', '--poke', '1750+:1CD6=0099',
                 '--poke', '4400-4800:130=00E0', '--poke', '4400-4800:132=00B0']))
    # ...and, the same day, what comes after it: Dr. Tongue and his head
    # (`$83:C687`). The spider is killed as `spider48_killed` kills it. Where
    # he appears is where it died, so it differs by movie, and so does his
    # thread's page: `$0700` under `level1.zmv` and `fire-up.zmv`, `$0300`
    # under `hold-y.zmv`. `$14` is its life, `$16` its tongues out, `$4A`
    # broken and `$52` what hit it.
    kill48 = near48 + ['--poke', '2300:0552=FFFF', '--poke', '2400-2700:0006=000F',
                       '--poke', '2500:0514=0000', '--poke', '2500:0552=FFFF',
                       '--poke', '2700:0514=FFFE', '--poke', '2700:0552=FFFF',
                       '--poke', '2700:055A=005C', '--poke', '2850-3000:0006=0012']
    doctor = kill48 + ['--poke', '3500:130=0122', '--poke', '3500:132=0110']
    out.append(('tongue48', 'level1.zmv', 9000,
                doctor + ['--poke', '1800+:1F52=0003', '--poke', '6000:075C=0005',
                          '--poke', '8000:0714=0003', '--poke', '8000:074A=FFFF']))
    out.append(('tongue48_lives', 'level1.zmv', 9000,
                doctor + ['--poke', '4400:0714=0002', '--poke', '6000:074A=FFFF',
                          '--poke', '7000:074A=FFFF', '--poke', '7000:0752=005C']))
    below = kill48 + ['--poke', '4000:130=0096', '--poke', '4000:132=014A',
                      '--poke', '4400+:130=0096', '--poke', '4400+:132=0190',
                      '--poke', '4400+:0716=0003']
    for name, weapons in (('tongue48_shot', range(7)),
                          ('tongue48_shot2', range(7, 13))):
        armed = []
        for j, i in enumerate(weapons):
            frame = 4500 + 500 * j
            armed += ['--poke', '%d+:1CBC=%04X' % (frame, i),
                      '--poke', '%d+:%04X=0099' % (frame, 0x1CCC + 2 * i)]
        out.append((name, 'fire-up.zmv', 8100, below + armed))
    out.append(('tongue48_side', 'hold-y.zmv', 9000,
                doctor + ['--poke', '1800+:1F52=0003', '--poke', '5000:0314=0002',
                          '--poke', '6500+:130=01C2', '--poke', '6500+:132=0110',
                          '--poke', '7500:035C=0005']))
    out.append(('tongue48_touch', 'level1.zmv', 6000,
                doctor + ['--poke', '4100-4300:130=0122',
                          '--poke', '4100-4300:132=00C4']))
    out.append(('tongue48_blink', 'fire-up.zmv', 4700,
                kill48 + ['--poke', '1800+:1F52=0003', '--poke', '4300:130=0096',
                          '--poke', '4300:132=014A']))
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
