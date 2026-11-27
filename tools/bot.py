r"""Play a level to its end, and keep the movie: a bot for reaching code.

The corpus and `tools/discover.py` reach the start of levels. What lies
deeper -- every neighbour saved, the exit, a boss, the levels after -- is code
no generated session gets to, and the only way there is to play. This does,
against the ROM in `zamn_bot_host` (`src/bot_host.c`), so what it writes is a
movie the game really plays.

How it plays:

  * **Where to go.** The level's own tiles, read from WRAM as `$80:AE14` reads
    them -- the row table at `$7E:4328`, the attribute table at `[$BA]` -- and
    the player's own 3x2-tile box. A distance field is flooded back from the
    goal, so every step is "downhill", and it is flooded again when the tiles
    change (a door opened) or the goal moves (a neighbour walking).
  * **What to go to.** The nearest neighbour still waiting (`$7E:605A` not
    `$80`), at its placement; with none left, the exit door, the record with
    collide id `$37`. A neighbour behind a locked door is gone to through it
    when a key is held (`$7E:1D0C`, item slot 0), and when none is, the
    nearest key on the ground (object type `$08`) is fetched first.
  * **Fighting.** An enemy (`HOSTILE`) on a line with the player and in range
    is turned to and shot at, and fire is tapped only then: shots are ammo, and
    a squirt gun fired at nothing is empty before level 1 is over. One nearer
    than `TOO_CLOSE` and off the line is walked away from, and so is anything
    close while there is nothing to shoot with. Something shot at for `FUTILE`
    frames without moving is let be.
  * **Not being touched.** What hurts a player is what `$80:F7F7`'s table
    sends to a hurt path (`TOUCH`), and turning to shoot one is a step towards
    it: nearer than `HARD_CLOSE`, it is walked away from even when it is on a
    line. A slime's glob (`DODGE`) is kept away from, since where it lands
    holds a player fast; held fast (state `$0C` on the player's page), left
    and right are shaken in turn, which `$80:D468` lets go of at the third.
  * **A kit.** A player who got to level 30 by playing has more than a new
    game's squirt gun. Unless `--no-kit`, every weapon and item is poked in
    at `PLAY_FROM`, to the most the game lets anyone carry (what `--give-all`
    gives); the pokes are written into the movie's comment, and a replay
    (`zamn.exe`, `zamn_cosim verify`) wants them too.
  * **Arms.** Weapons lying on the ground (object types whose collide id,
    `$80:CA30`, is a weapon's, `$0C-$19`) are fetched before anything else
    while the ammo is low, and taken on the way when near. So are first-aid
    kits. With the selected weapon (`$7E:1CBC`) empty and another one loaded,
    B is tapped to change.
  * **Backing up.** A checkpoint every `CHECK` frames. Losing a life, losing
    health fast, a neighbour lost or no progress for a long while sends it back
    one or more checkpoints, with its random choices changed. Repeated failure
    at one place backs up further, and in the end accepts the loss.

    python tools\bot.py "Zombies Ate My Neighbors.sfc" out --level 1
        [--levels 1] [--seed 0] [--max-frames 40000]

Writes `out\levelNN.zmv` after each level finished (and `out\partial.zmv`
when it gives up), and says on stdout what happened.
"""
import argparse
import heapq
import os
import random
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(REPO, 'build', 'zamn_bot_host.exe')

B, Y, SELECT, START = 1, 2, 4, 8
UP, DOWN, LEFT, RIGHT = 16, 32, 64, 128
A, X = 256, 512

PLAY_FROM = 1560  # `movies/level1.zmv`'s way in, as `tools/discover.py` takes it

W_PLAYER_A_RECORD = 0x00D2
W_HEALTH = 0x1CB8
W_LIVES = 0x1D4C
W_LEFT = 0x1D52          # neighbours left, BCD
W_SAVED = 0x1F9C         # neighbours each player saved, BCD
W_LEVEL = 0x1E7C
W_ACTORS, ACTOR_STRIDE, ACTORS = 0x185E, 0x14, 32
W_VICTIM_X, W_VICTIM_COUNT, W_VICTIM_STATE, W_VICTIM_THREAD = 0x6DF4, 0x6E30, 0x605A, 0x609A
W_ROW_TABLE = 0x4328
W_OBJECT_X, W_OBJECT_Y, W_OBJECT_TYPE, W_OBJECT_STATE = 0x6D02, 0x6D48, 0x1F0A, 0x1EC4
OBJECTS = 35
OBJECT_KEY = 0x08
OBJECT_COLLIDE_IDS = 0x00CA30  # bank $80, a word an object type
W_WEAPON = 0x1CBC        # the selected weapon, $FFFF for none
W_AMMO = 0x1CCC          # 14 BCD counters, the weapons'
WEAPONS = 14
WEAPON_IDS = range(0x0C, 0x0C + WEAPONS)
KIT_ID = 0x28            # item slot 7
LOW_AMMO = 60            # fewer shots than this, all weapons together: go for more
ARMS_NEAR = 24           # a pickup this few steps out of the way is taken
LOW_HEALTH = 6           # this or less (of 10): a first-aid kit anywhere is gone for
PICKUP_PATIENCE = 300   # frames without getting nearer, and a pickup is let be
PICKUPS = ('weapon', 'first-aid kit')  # goals that are only on the way
W_KEYS = 0x1D0C          # item slot 0, BCD
W_KITS = 0x1D1A          # item slot 7, the first-aid kits
W_ITEM = 0x1CC0          # the selected item
KIT_SLOT = 7
DOOR_ID = 0x37
ATTR_SOLID = 0x0001
# What the walk tests a tile ahead of an up or down step for (`$80:E8D3`,
# `$80:E943`, `$80:E9B3`): a door a key opens, one a skeleton key opens, and
# one that breaks open for anybody walking into it.
ATTR_DOOR_MASK = 0x8B38
ATTR_DOOR, ATTR_DOOR_B, ATTR_BREAKS = 0x0010, 0x8008, 0x0020
OPEN, WATER, BREAKS, DOOR, BLAST, SOLID = range(6)
# A cell's steps: swimming is slow, a door spends a key, a hedge a rocket.
COST = (1, 3, 8, 40, 30, None)
# What stops a bazooka's shot and is broken by it (`port/shot5.h`): bit 1 and
# any of bits 4-6, and not bit 2, which stops it with nothing broken. Hedges
# are `$2043`; a hedge's block opens to its pair as a door's does.
BAZOOKA = 5
BLAST_AHEAD = 6  # cells along the way a hedge is shot at from
ATTR_WATER_MASK, ATTR_WATER = 0x8B39, 0x0101
W_SKELETON_KEYS = 0x1D1E  # item slot 9, what `$80:E943` reads
# The level as 8x8-tile blocks (`$80:ACF6`, `$80:AD0B`): a word a block, in
# the bank at `$A8`, each block row at the offset `$7E:4228` gives; block n
# is 64 tile words at n * 128 from the long pointer at `$AA`.
W_BLOCK_ROWS = 0x4228
BLOCK_MAP_BANK, BLOCK_LIB, BLOCK_BYTES = 0xA8, 0xAA, 128

_roms = {}


def _rom(path):
    if path not in _roms:
        with open(path, 'rb') as f:
            _roms[path] = f.read()
    return _roms[path]

STEP = 4      # frames a choice is held
CHECK = 240   # frames between checkpoints
RING = 200    # checkpoint slots, 1..RING

# Collide ids that hurt. Measured with a probe that logs the ids near the
# player each time health drops: on level 5 the clones, id 3, were within ten
# pixels of six hits in seven. 5 and 6 are the players, $0C-$33 what lies on
# the ground, $37 the exit. Id 1 is not here: on level 5 it is something
# that stands still and takes the squirt gun's shots without end.
HOSTILE = {0x02, 0x03, 0x04, 0x07, 0x08, 0x09, 0x0A, 0x0B}
FUTILE = 120     # frames of shooting at one thing that stays put, and it is let be
AIM_RANGE = 150  # pixels a shot is worth taking at
AIM_SLACK = 10   # how far off the line still counts as on it
TOO_CLOSE = 40   # nearer than this and not on a line: get away
# The ids `$80:F7F7  JSR ($F808,X)` sends to a path that hurts: `$80:F950`
# (3, 4, 9), `$80:F9BE` ($0A), `$80:F979` ($0B), `$80:F9AE` ($34, which holds
# a player fast) and `$80:F999` ($35). Over the first survey's partial levels
# id 3 was within 20 pixels of 528 of the 681 hits.
TOUCH = {0x03, 0x04, 0x09, 0x0A, 0x0B, 0x34, 0x35}
DODGE = {0x36}   # a slime's glob: where it comes down, a player is held fast
DODGE_RANGE = 40
HARD_CLOSE = 24  # a `TOUCH` this near is walked away from, on a line or not
THREAD_PAGES = 0x0082DE  # bank $80: each thread's direct page, by slot
ACTOR_THREAD = 0x0C      # a display record's thread slot, doubled
DP_STATE = 0x70          # the player's state, on the player's page
STATE_STUCK = 0x0C       # held fast: `$80:D465`
W_WEAPONS_A, W_ITEMS_A = 0x1CCC, 0x1D0C
ITEMS, ITEMS_UNUSED = 12, (6, 11)
KIT_POKES = (['%d:%x=0999' % (PLAY_FROM, W_WEAPONS_A + 2 * s) for s in range(WEAPONS)] +
             ['%d:%x=0099' % (PLAY_FROM, W_ITEMS_A + 2 * s) for s in range(ITEMS)
              if s not in ITEMS_UNUSED])


def bcd(v):
    out, m = 0, 1
    while v:
        out += (v & 15) * m
        v >>= 4
        m *= 10
    return out


class Host:
    """`zamn_bot_host`, one command a line."""

    def __init__(self, rom, level, movie=None, until=PLAY_FROM, extra=()):
        movie = movie or os.path.join(REPO, 'movies', 'level1.zmv')
        self.rom_path = rom
        cmd = [HOST, rom, '-m', movie, '--until', str(until)]
        if level is not None:
            cmd += ['--level', str(level)]
        self.p = subprocess.Popen(cmd + list(extra), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, bufsize=1)
        self.frame = int(self._read())

    def _read(self):
        line = self.p.stdout.readline().strip()
        if not line.startswith('ok'):
            raise RuntimeError('host: %r' % line)
        return line[3:]

    def cmd(self, *lines):
        self.p.stdin.write(''.join(l + '\n' for l in lines))
        self.p.stdin.flush()
        return [self._read() for _ in lines]

    def run(self, n, pad1, pad2=0):
        self.frame = int(self.cmd('run %d %x %x' % (n, pad1, pad2))[0])

    def save(self, k):
        self.cmd('save %d' % k)

    def load(self, k):
        self.frame = int(self.cmd('load %d' % k)[0])

    def write(self, path, comment=''):
        self.cmd('write %s %s' % (path, comment))

    def close(self):
        try:
            self.cmd('quit')
        except Exception:
            pass
        self.p.wait()


class Ram:
    """The parts of WRAM a step reads, fetched in one round trip."""

    RANGES = [(0x0000, 0x0100), (0x0100, 0x0C00), (0x185E, 0x0280),
              (0x1C00, 0x0400), (0x6000, 0x0E40)]  # 0x1EC4.. and 0x6D02.. inside

    def __init__(self, host):
        got = host.cmd(*['peek %x %x' % r for r in self.RANGES])
        self.mem = {}
        for (a, n), h in zip(self.RANGES, got):
            self.mem[a] = (n, bytes.fromhex(h))

    def b(self, addr):
        for a, (n, d) in self.mem.items():
            if a <= addr < a + n:
                return d[addr - a]
        raise KeyError(hex(addr))

    def w(self, addr):
        return self.b(addr) | self.b(addr + 1) << 8


class Level:
    """The tiles, as `$80:AE14` reads them, and which player boxes fit."""

    def __init__(self, host, blast_ok=False):
        self.blast_ok = blast_ok  # a bazooka with shots: hedges are a way
        zp = bytes.fromhex(host.cmd('peek 0 100')[0])
        self.stride = struct.unpack_from('<H', zp, 0xB2)[0]
        attrs = zp[0xBA] | zp[0xBB] << 8 | (zp[0xBC] & 1) << 16
        rows = struct.unpack('<512H', bytes.fromhex(host.cmd('peek %x 400' % W_ROW_TABLE)[0]))
        n = 1
        while n < 512 and rows[n] == (rows[n - 1] + self.stride) & 0xFFFF:
            n += 1
        self.rows, self.cols = n, self.stride // 2
        base = 0x10000 + rows[0]
        size = self.rows * self.stride
        raw = b''
        while size > 0:  # the host answers at most 0x20000 hex digits a line
            take = min(size, 0x8000)
            raw += bytes.fromhex(host.cmd('peek %x %x' % (base, take))[0])
            base += take
            size -= take
        self.map = struct.unpack('<%dH' % (self.rows * self.cols), raw)
        self.attrs = struct.unpack('<512H', bytes.fromhex(host.cmd('peek %x 400' % attrs)[0]))
        self.key = hash((self.map, blast_ok))
        tile = [[SOLID] * self.cols for _ in range(self.rows)]
        for r in range(self.rows):
            for c in range(self.cols):
                i = self.map[r * self.cols + c] & 0x3FF
                a = self.attrs[i] if i < 512 else ATTR_SOLID
                if not a & ATTR_SOLID:
                    tile[r][c] = OPEN
                elif a & ATTR_WATER_MASK == ATTR_WATER:
                    tile[r][c] = WATER
                elif a & ATTR_DOOR_MASK in (ATTR_DOOR, ATTR_DOOR_B):
                    tile[r][c] = DOOR
                elif a & ATTR_DOOR_MASK == ATTR_BREAKS:
                    tile[r][c] = BREAKS
                elif a & 0x0002 and a & 0x0070 and not a & 0x0004:
                    tile[r][c] = BLAST
        self.doors_open(host, zp, tile)
        # A cell is the box at it, columns c..c+2 and rows r..r+1, and is the
        # worst of its six tiles.
        self.kind = [[max(tile[r + dy][c + dx] for dy in (0, 1) for dx in (0, 1, 2))
                      if r + 1 < self.rows and c + 2 < self.cols else SOLID
                      for c in range(self.cols)] for r in range(self.rows)]
        self.open = [[k <= WATER for k in row] for row in self.kind]

    def doors_open(self, host, zp, tile):
        """What a door leaves is not the door's tiles alone: it is drawn on a
        wall several tiles thick, and opening it puts the next block of the
        library over the whole 8x8-tile block it is in (`$80:E934  LDA [$28] :
        ORA #$0001`, then `$80:AB5A`). So in a block with a door or a tile
        that breaks, whatever that block opens up is the door."""
        rom = _rom(host.rom_path)
        bank = zp[BLOCK_MAP_BANK]
        lib = zp[BLOCK_LIB] | zp[BLOCK_LIB + 1] << 8 | zp[BLOCK_LIB + 2] << 16
        bcols, brows = self.cols // 8, self.rows // 8
        if bank < 0x80 or lib >> 16 not in (0x7E, 0x7F) or not brows:
            return
        rows = struct.unpack('<%dH' % brows,
                             bytes.fromhex(host.cmd('peek %x %x' % (W_BLOCK_ROWS, 2 * brows))[0]))
        library = None
        for br in range(brows):
            for bc in range(bcols):
                kinds = {tile[br * 8 + i][bc * 8 + j] for i in range(8) for j in range(8)}
                kind = (DOOR if DOOR in kinds else BREAKS if BREAKS in kinds else
                        BLAST if BLAST in kinds else None)
                if kind is None:
                    continue
                at = (bank & 0x7F) * 0x8000 + rows[br] + 2 * bc - 0x8000
                block = ((rom[at] | rom[at + 1] << 8) | 1) & 0x1FF
                if library is None:
                    raw = bytes.fromhex(host.cmd('peek %x %x' % (lib - 0x7E0000, BLOCK_BYTES * 256))[0])
                    library = struct.unpack('<%dH' % (len(raw) // 2), raw)
                if (block + 1) * 64 > len(library):
                    continue
                for i in range(64):
                    a = self.attrs[library[block * 64 + i] & 0x3FF]
                    r, c = br * 8 + i // 8, bc * 8 + i % 8
                    if not a & ATTR_SOLID or a & ATTR_WATER_MASK == ATTR_WATER:
                        if tile[r][c] == SOLID:
                            tile[r][c] = kind
                    elif tile[r][c] in (DOOR, BREAKS, BLAST):
                        tile[r][c] = SOLID  # still wall once it is open

    @staticmethod
    def cell(x, y):
        return (x - 9) >> 3, (y - 8) >> 3

    def is_open(self, c, r):
        return 0 <= r < self.rows and 0 <= c < self.cols and self.open[r][c]

    def nearest_open(self, c, r, reach=6):
        best = None
        for dr in range(-reach, reach + 1):
            for dc in range(-reach, reach + 1):
                if self.is_open(c + dc, r + dr):
                    d = dc * dc + dr * dr
                    if best is None or d < best[0]:
                        best = (d, (c + dc, r + dr))
        return best[1] if best else None

    def passable(self, c, r, doors):
        if not (0 <= r < self.rows and 0 <= c < self.cols):
            return False
        k = self.kind[r][c]
        return k <= BREAKS or (doors and k == DOOR) or (self.blast_ok and k == BLAST)

    def flood(self, goal, doors=False):
        """Cost to `goal` from every cell that reaches it, eight ways and no
        corner cutting, at what `COST` says each cell is worth; door cells
        only when `doors` lets the flood cross them."""
        dist = {goal: 0}
        heap = [(0, goal)]
        while heap:
            d0, (c, r) = heapq.heappop(heap)
            if d0 > dist[(c, r)]:
                continue
            for dc in (-1, 0, 1):
                for dr in (-1, 0, 1):
                    if not dc and not dr:
                        continue
                    n = (c + dc, r + dr)
                    if not self.passable(*n, doors):
                        continue
                    if dc and dr and not (self.passable(c + dc, r, doors) and
                                          self.passable(c, r + dr, doors)):
                        continue
                    d = d0 + COST[self.kind[n[1]][n[0]]]
                    if d < dist.get(n, 1 << 30):
                        dist[n] = d
                        heapq.heappush(heap, (d, n))
        return dist


def object_collide_ids(rom):
    """Object type (doubled, as `$7E:1F0A` holds it) to the collide id its
    record is given."""
    at = OBJECT_COLLIDE_IDS - 0x8000
    return {2 * i: struct.unpack_from('<H', rom, at + 2 * i)[0] for i in range(30)}


def pad_toward(dc, dr):
    p = 0
    if dc < 0:
        p |= LEFT
    elif dc > 0:
        p |= RIGHT
    if dr < 0:
        p |= UP
    elif dr > 0:
        p |= DOWN
    return p


class Bot:
    def __init__(self, host, rng, log):
        self.h = host
        self.rng = rng
        self.log = log
        self.level = None
        self.plan = None   # ((goal, doors), map key, dist)
        self.reach = None  # ((from, map key), plain, through doors)
        self.skip = set()  # goals given up on
        self.goal_item = None
        self.last_goal = None
        self.chosen = None  # (what it depended on, frame, goal, item)
        self.aiming = None  # ((id, x, y), since)
        self.futile = set()
        self.shoot = False  # what `combat` said: something to fire at
        self.urgent = False  # ...and that getting away comes before all else
        self.blast = None  # the way to face to shoot a hedge on the way
        with open(host.rom_path, 'rb') as f:
            rom = f.read()
        self.pickup_ids = object_collide_ids(rom)
        self.pages = struct.unpack_from('<24H', rom, THREAD_PAGES - 0x8000)

    def player(self, ram):
        rec = ram.w(W_PLAYER_A_RECORD)
        if not 0x185E <= rec < 0x185E + ACTOR_STRIDE * ACTORS:
            return None
        return ram.w(rec + 2), ram.w(rec + 6)

    def stuck(self, ram):
        """Held fast where a slime's glob came down."""
        rec = ram.w(W_PLAYER_A_RECORD)
        if not 0x185E <= rec < 0x185E + ACTOR_STRIDE * ACTORS:
            return False
        slot = ram.w(rec + ACTOR_THREAD)
        if slot & 1 or slot >= 2 * len(self.pages):
            return False
        page = self.pages[slot // 2]
        return 0x0100 <= page < 0x0C00 and ram.w(page + DP_STATE) == STATE_STUCK

    def actors(self, ram):
        out = []
        for i in range(ACTORS):
            a = W_ACTORS + i * ACTOR_STRIDE
            flags = ram.w(a)
            if flags & 1:
                out.append((ram.w(a + 0x0E), ram.w(a + 2), ram.w(a + 6), a))
        return out

    def waiting(self, ram):
        """Each neighbour not yet saved, where it is."""
        out = []
        for i in range(min(ram.w(W_VICTIM_COUNT), 64)):
            if ram.b(W_VICTIM_STATE + i) & 0x80:
                continue
            x, y = ram.w(W_VICTIM_X + 4 * i), ram.w(W_VICTIM_X + 4 * i + 2)
            out.append((i, x, y))
        return out

    def combat(self, ram, pos):
        """A pad that faces the nearest enemy on a line with the player, or
        steps out of the way of one too close; None when nothing calls for it."""
        best = None
        close = None
        near = []
        self.shoot = False
        armed = self.ammo(ram) > 0
        urgent = False  # something about to touch the player, or a glob
        for cid, x, y, _ in self.actors(ram):
            dx, dy = x - pos[0], y - pos[1]
            d = abs(dx) + abs(dy)
            if cid in DODGE:
                if d < DODGE_RANGE * 2:
                    near.append((x, y))
                if d < DODGE_RANGE:
                    urgent = True
                    if close is None or d < close[0]:
                        close = (d, dx, dy)
                continue
            if cid in TOUCH and d < HARD_CLOSE:
                urgent = True
            if (cid not in HOSTILE and cid not in TOUCH) or (cid, x, y) in self.futile:
                continue
            if d < TOO_CLOSE * 2:
                near.append((x, y))
            if d < TOO_CLOSE and (close is None or d < close[0]):
                close = (d, dx, dy)
            if cid not in HOSTILE:
                continue  # kept away from, never shot at
            if abs(dy) <= AIM_SLACK and abs(dx) <= AIM_RANGE:
                face = RIGHT if dx > 0 else LEFT
            elif abs(dx) <= AIM_SLACK and abs(dy) <= AIM_RANGE:
                face = DOWN if dy > 0 else UP
            else:
                continue
            if best is None or d < best[0]:
                best = (d, face, (cid, x, y))
        if best and not urgent and (armed or not close):
            self.shoot = armed
            # The same thing in the same place, shot at for long enough, is
            # something the weapon does nothing to.
            target = best[2]
            if self.aiming and self.aiming[0] == target:
                if self.h.frame - self.aiming[1] >= FUTILE:
                    self.futile.add(target)
            else:
                self.aiming = (target, self.h.frame)
            return best[1]
        self.aiming = None
        self.urgent = urgent
        if close and self.level:
            # On top of the player, where everything is on a line and a shot
            # comes too late: away, to whichever open
            # cell next to the player is furthest from everything near. The
            # chasers here are slower than a player walking.
            here = Level.cell(*pos)
            best_way = None
            for dc in (-1, 0, 1):
                for dr in (-1, 0, 1):
                    if (not dc and not dr) or not self.level.is_open(here[0] + dc, here[1] + dr):
                        continue
                    nx, ny = pos[0] + 8 * dc, pos[1] + 8 * dr
                    score = sum(1.0 / (1 + abs(nx - x) + abs(ny - y)) for x, y in near)
                    if best_way is None or score < best_way[0]:
                        best_way = (score, pad_toward(dc, dr))
            if best_way:
                self.shoot = armed
                return best_way[1]
        return None

    def ammo(self, ram):
        """Shots left, all weapons together."""
        return sum(bcd(ram.w(W_AMMO + 2 * i)) for i in range(WEAPONS))

    def lying(self, ram, wanted):
        """Each object on the ground not yet taken whose type `wanted` says yes to."""
        out = []
        for i in range(OBJECTS):
            st = ram.w(W_OBJECT_STATE + 2 * i)
            if st == 0xC000:
                break
            if st & 0x8000 or not wanted(ram.w(W_OBJECT_TYPE + 2 * i)):
                continue
            out.append((i, ram.w(W_OBJECT_X + 2 * i), ram.w(W_OBJECT_Y + 2 * i)))
        return out

    def keys_lying(self, ram):
        return self.lying(ram, lambda t: t == OBJECT_KEY)

    def arms_lying(self, ram):
        # Not a weapon there is nearly as much of as anybody may carry: the
        # kit's are full, and `$80:F87B` would only keep them there.
        def wanted(t):
            cid = self.pickup_ids.get(t)
            return cid in WEAPON_IDS and bcd(ram.w(W_AMMO + 2 * (cid - WEAPON_IDS[0]))) < 900
        return self.lying(ram, wanted)

    def kits_lying(self, ram):
        if bcd(ram.w(W_KITS)) >= 90:
            return []
        return self.lying(ram, lambda t: self.pickup_ids.get(t) == KIT_ID)

    def goal(self, ram, pos):
        """What to go to, where it is, and whether the way may open doors."""
        door = [(x, y) for cid, x, y, _ in self.actors(ram) if cid == DOOR_ID]
        if door:
            return 'door', door[0], False
        left = self.waiting(ram)
        if not left:
            return None, None, False
        lv = self.level
        here = lv.nearest_open(*Level.cell(*pos), reach=2) or Level.cell(*pos)
        # Two floods from where the player is: without doors and with them.
        if self.reach is None or self.reach[0] != (here, lv.key):
            self.reach = ((here, lv.key), lv.flood(here), lv.flood(here, doors=True))
        plain, through = self.reach[1], self.reach[2]

        def cost(dist, x, y):
            g = lv.nearest_open(*Level.cell(x, y))
            return dist.get(g) if g else None

        def nearest(items, dist):
            best = None
            for i, x, y in items:
                if (i, x, y) in self.skip:
                    continue
                d = cost(dist, x, y)
                if d is not None and (best is None or d < best[0]):
                    best = (d, i, x, y)
            if best:
                self.goal_item = best[1:]
            return best

        self.goal_item = None
        arms = self.arms_lying(ram)
        b = nearest(arms, plain)
        if b and (b[0] <= ARMS_NEAR or self.ammo(ram) < LOW_AMMO):
            return 'weapon %d (%d shots)' % (b[1], self.ammo(ram)), (b[2], b[3]), False
        b = nearest(self.kits_lying(ram), plain)
        if b and (b[0] <= ARMS_NEAR or ram.w(W_HEALTH) <= LOW_HEALTH):
            return 'first-aid kit %d' % b[1], (b[2], b[3]), False
        b = nearest(left, plain)
        if b:
            return 'neighbour %d' % b[1], (b[2], b[3]), False
        keys = bcd(ram.w(W_KEYS)) + bcd(ram.w(W_SKELETON_KEYS))
        if keys:
            b = nearest(left, through)
            if b:
                return 'neighbour %d through a door (%d keys)' % (b[1], keys), (b[2], b[3]), True
        b = nearest(self.keys_lying(ram), plain)
        if b:
            return 'key %d' % b[1], (b[2], b[3]), False
        b = nearest(self.keys_lying(ram), through)
        if b and keys:
            return 'key %d through a door' % b[1], (b[2], b[3]), True
        return None, None, False

    def choose(self, ram, pos):
        self.blast = None
        if self.level is None:
            self.level = Level(self.h, bcd(ram.w(W_AMMO + 2 * BAZOOKA)) > 0)
        lv = self.level
        # Choosing floods the level twice, so it is done again only when
        # something it depends on changed, or a second has gone by.
        why = (lv.key, bcd(ram.w(W_LEFT)), ram.w(W_KEYS), ram.w(W_SKELETON_KEYS),
               len(self.keys_lying(ram)),
               len(self.arms_lying(ram)), len(self.kits_lying(ram)),
               any(cid == DOOR_ID for cid, *_ in self.actors(ram)))
        if (self.chosen is None or self.chosen[0] != why or
                self.h.frame - self.chosen[1] >= 60):
            self.chosen = (why, self.h.frame, self.goal(ram, pos), self.goal_item)
        what, at, doors = self.chosen[2]
        self.goal_item = self.chosen[3]
        self.last_goal = None
        if at is None:
            return 0, what
        g = lv.nearest_open(*Level.cell(*at))
        if g is None:
            return 0, what
        self.last_goal = self.goal_item
        if not self.plan or self.plan[0] != (g, doors) or self.plan[1] != lv.key:
            self.plan = ((g, doors), lv.key, lv.flood(g, doors))
        dist = self.plan[2]
        here = Level.cell(*pos)
        if here == g or abs(pos[0] - at[0]) + abs(pos[1] - at[1]) < 12:
            # On the goal's cell: the last pixels, straight at it.
            return pad_toward(at[0] - pos[0], at[1] - pos[1]), what
        if here not in dist:
            near = lv.nearest_open(*here, reach=2)
            if near is None or near not in dist:
                return pad_toward(at[0] - pos[0], at[1] - pos[1]), what
            return pad_toward(near[0] - here[0], near[1] - here[1]), what
        # Walk downhill, looking a few cells ahead along the descent.
        c = here
        for _ in range(3):
            best = None
            for dc in (-1, 0, 1):
                for dr in (-1, 0, 1):
                    n = (c[0] + dc, c[1] + dr)
                    if n in dist and dist[n] < dist[c]:
                        k = (dist[n], abs(dc) + abs(dr))
                        if best is None or k < best[0]:
                            best = (k, n)
            if best is None:
                break
            c = best[1]
        # A hedge on the way a few cells on is shot at, square on: the
        # bazooka's shot goes the way the player faces.
        n = here
        for _ in range(BLAST_AHEAD):
            nxt = min(((dist[m], m) for m in
                       ((n[0] + dc, n[1] + dr) for dc in (-1, 0, 1) for dr in (-1, 0, 1))
                       if m in dist and dist[m] < dist[n]), default=None)
            if nxt is None:
                break
            n = nxt[1]
            if lv.kind[n[1]][n[0]] == BLAST:
                dc, dr = n[0] - here[0], n[1] - here[1]
                # The axis it is further along; on the other the player
                # is lined up first.
                if abs(dc) >= abs(dr):
                    self.blast = RIGHT if dc > 0 else LEFT
                    if dr:
                        return pad_toward(0, dr), what
                else:
                    self.blast = DOWN if dr > 0 else UP
                    if dc:
                        return pad_toward(dc, 0), what
                return self.blast, what
        return pad_toward(c[0] - here[0], c[1] - here[1]), what


def play_level(host, rng, log, max_frames):
    """Until the level number steps. Returns True if it did."""
    ram = Ram(host)
    start_level = ram.w(W_LEVEL)
    bot = Bot(host, rng, log)
    # Wait for the player: the level card and the load take about 500 frames.
    waited = host.frame
    while bot.player(ram) is None:
        host.run(STEP, 0)
        ram = Ram(host)
        if host.frame - waited > 3000:
            log('no player by frame %d' % host.frame)
            return False
    begin = host.frame
    checkpoints = []  # (slot, frame, lives, health, saved, left)
    next_slot = 1

    def snapshot(r):
        # The last life lost leaves the lives at `$FFFF`: the game is over.
        lives = r.w(W_LIVES)
        return (-1 if lives & 0x8000 else bcd(lives), r.w(W_HEALTH), bcd(r.w(W_SAVED)) + bcd(r.w(W_SAVED + 2)),
                bcd(r.w(W_LEFT)))

    def checkpoint(r):
        nonlocal next_slot
        host.save(next_slot)
        checkpoints.append((next_slot, host.frame) + snapshot(r))
        next_slot = next_slot % RING + 1
        if len(checkpoints) > RING - 1:
            checkpoints.pop(0)

    checkpoint(ram)
    failures = {}  # checkpoint index -> times backed up to it
    accepted_at = 0  # the frame of the last loss accepted
    best_progress = None
    stall_since = host.frame
    fire = 0
    last_what = None
    last_goal = None
    none_since = None
    steps = 0
    aggression = 0.9  # how often a shot on a line beats walking on
    while host.frame - begin < max_frames and steps < max_frames:
        steps += 1
        ram = Ram(host)
        lives_now = ram.w(W_LIVES)
        if ram.w(W_LEVEL) != start_level and not lives_now & 0x8000:
            if ram.w(W_LEVEL) <= 1 and start_level > 1:
                log('frame %d: back to the start, the game is over' % host.frame)
                return False
            log('level %d left at frame %d' % (start_level, host.frame))
            return True
        pos = bot.player(ram)
        lives, health, saved, left = snapshot(ram)
        cp = checkpoints[-1]
        bad = None
        if lives < 0:
            bad = 'the game is over'
        elif lives < cp[2]:
            bad = 'lost a life'
        elif health + 3 <= cp[3] or (health <= 2 and health < cp[3]):
            bad = 'health %d -> %d' % (cp[3], health)
        elif left < cp[5] and saved == cp[4]:
            bad = 'a neighbour was lost'
        elif host.frame - stall_since > 900:
            bad = 'no progress'
        elif (host.frame - stall_since > PICKUP_PATIENCE and last_what and
              last_what.startswith(PICKUPS)):
            # A pickup is on the way or not worth having: not taken in that
            # long, it is let be, and nothing is undone.
            log('frame %d: %s not taken, let be' % (host.frame, last_what))
            bot.skip.add(bot.last_goal)
            bot.chosen = None
            stall_since = host.frame
            continue
        if bad:
            # The last checkpoint from before the trouble began, and further
            # back each third time the same place fails.
            began = stall_since if bad == 'no progress' else host.frame - 60
            idx = len(checkpoints) - 1
            while idx > 0 and checkpoints[idx][1] > began:
                idx -= 1
            # Never back behind a loss already accepted, or the same loss is
            # met and accepted again without end. A game over may go past it.
            while (bad != 'the game is over' and idx < len(checkpoints) - 1 and
                   checkpoints[idx][1] < accepted_at):
                idx += 1
            n = failures.get(checkpoints[idx][1], 0)
            limit = 30 if bad in ('lost a life',) else 12
            if lives == 0 and health <= 3:
                limit = 40  # one more hit and the game is over
            if bad == 'the game is over':
                limit = 1 << 30
            if n >= limit:
                log('frame %d: %s, accepted after %d tries' % (host.frame, bad, n))
                failures.pop(checkpoints[idx][1], None)
                if bad == 'no progress' and bot.plan:
                    bot.skip.add(last_goal)
                stall_since = host.frame
                # The checkpoints before it stay, so that a game over can
                # still go back past it.
                checkpoint(ram)
                accepted_at = host.frame
                continue
            failures[checkpoints[idx][1]] = n + 1
            low = 0
            if bad != 'the game is over':
                while low < idx and checkpoints[low][1] < accepted_at:
                    low += 1
            idx = max(low, idx - n // 3)
            target = checkpoints[idx]
            del checkpoints[idx + 1:]
            host.load(target[0])
            log('frame %d: %s; back to %d (try %d)' % (host.frame, bad, target[1], n + 1))
            rng.seed(rng.random())
            aggression = rng.choice((0.5, 0.75, 0.9, 1.0))
            bot.level = None
            bot.plan = None
            bot.reach = None
            bot.chosen = None
            stall_since = host.frame
            best_progress = None
            last_what = None
            continue
        if pos is None:
            # Gone through the door, or between lives: nothing to steer, and
            # nothing stuck.
            stall_since = host.frame
            if none_since is None:
                none_since = host.frame
            # Every fourth level ends on a screen with the password on it,
            # which waits for Start. In play Start pauses, so only after a
            # long while with no player.
            if host.frame - none_since > 600 and host.frame % 60 < STEP:
                host.run(2, START)
            host.run(STEP, 0)
            continue
        none_since = None
        if bot.stuck(ram):
            # Held fast: shaken free, left and right in turn. Nothing else
            # does anything, and it is not being stuck in the other sense.
            stall_since = host.frame
            host.run(STEP, LEFT if (host.frame // STEP) & 1 else RIGHT)
            continue
        pad, what = bot.choose(ram, pos)
        last_goal = bot.last_goal
        aim = bot.combat(ram, pos)
        if aim is not None and (bot.urgent or rng.random() < aggression):
            pad = aim
        if what != last_what:
            log('frame %d at %s: going to %s, %d left' % (host.frame, pos, what, left))
            last_what = what
            best_progress = None
            stall_since = host.frame
        # Out through the door, the level takes 800 frames or more to step:
        # waiting for it is not being stuck.
        if what is None and left == 0:
            stall_since = host.frame
        # Progress is the distance field going down.
        if bot.plan:
            d = bot.plan[2].get(Level.cell(*pos))
            if d is not None and (best_progress is None or d < best_progress):
                best_progress = d
                stall_since = host.frame
        # A random wiggle now and then breaks the ties a wall makes.
        if rng.random() < 0.05 and not bot.urgent:
            pad = rng.choice([UP, DOWN, LEFT, RIGHT, UP | LEFT, UP | RIGHT,
                              DOWN | LEFT, DOWN | RIGHT])
        fire ^= 1
        if health <= LOW_HEALTH and bcd(ram.w(W_KITS)) and not fire:
            if ram.w(W_ITEM) == KIT_SLOT:
                pad |= X  # the selected item is the first-aid kit
            elif host.frame % 32 < STEP:
                pad |= A  # A steps through the items to it
        # The selected weapon empty and another loaded: B changes it. A hedge
        # to shoot wants the bazooka, and nothing else does: its blast is
        # not for the crowd round the neighbours.
        weapon = ram.w(W_WEAPON)
        loaded = [bcd(ram.w(W_AMMO + 2 * i)) for i in range(WEAPONS)]
        empty = weapon >= WEAPONS or not loaded[weapon]
        want = None
        if bot.blast is not None and loaded[BAZOOKA]:
            want = BAZOOKA
        elif weapon == BAZOOKA and any(loaded[:BAZOOKA] + loaded[BAZOOKA + 1:]):
            want = next(i for i in range(WEAPONS) if i != BAZOOKA and loaded[i])
        if want is not None and weapon != want and not fire:
            pad |= B  # every other step, so that each is a press
        elif empty and bot.ammo(ram) and host.frame % 32 < STEP and not fire:
            pad |= B
        blasting = bot.blast is not None and weapon == BAZOOKA and (pad & 0xF0) == bot.blast
        host.run(STEP, pad | (Y if fire and (bot.shoot or blasting) else 0))
        if bot.level and host.frame % 60 < STEP:
            bot.level = None  # tiles change: doors open
        if host.frame - checkpoints[-1][1] >= CHECK:
            checkpoint(Ram(host))
    log('gave up on level %d at frame %d' % (start_level, host.frame))
    return False


REPLAY = '# replay with:'


def note_pokes(path, pokes):
    """The pokes a replay wants, as comment lines under the first: a movie
    line is read into 256 bytes (`src/analysis/movie.c`), so six a line."""
    with open(path) as f:
        lines = f.read().split('\n')
    notes = [REPLAY + ''.join(' --poke ' + p for p in pokes[i:i + 6])
             for i in range(0, len(pokes), 6)]
    with open(path, 'w') as f:
        f.write('\n'.join(lines[:1] + notes + lines[1:]))


def replay_pokes(path):
    """What `note_pokes` wrote, as arguments for `zamn.exe` or `zamn_cosim`."""
    out = []
    with open(path) as f:
        for line in f:
            if not line.startswith('#'):
                break
            if line.startswith(REPLAY):
                out += line[len(REPLAY):].split()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('rom')
    ap.add_argument('out')
    ap.add_argument('--level', type=int, default=1)
    ap.add_argument('--levels', type=int, default=1)
    ap.add_argument('--seed', type=int, default=0)
    ap.add_argument('--max-frames', type=int, default=40000)
    # Carry on from a movie this wrote: `--level` is still the level the game
    # was started on, and `--first` the one being played when it ends.
    ap.add_argument('--from', dest='start_from', help='a movie to replay first')
    ap.add_argument('--until', type=int, default=PLAY_FROM,
                    help="the frame to replay it to (the movie's last line)")
    ap.add_argument('--first', type=int, help='the level that movie ends on')
    ap.add_argument('--no-kit', dest='kit', action='store_false',
                    help="play with a new game's squirt gun, not every weapon and item")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rng = random.Random(a.seed)

    def log(s):
        print(s, flush=True)

    pokes = ' '.join('--poke ' + p for p in KIT_POKES) if a.kit else ''
    if pokes:
        log('the kit, which a replay wants too: %s' % pokes)
    host = Host(os.path.abspath(a.rom), a.level,
                a.start_from and os.path.abspath(a.start_from), a.until,
                pokes.split())
    try:
        first = a.first if a.first is not None else a.level
        for n in range(a.levels):
            level = first + n
            ok = play_level(host, rng, log, a.max_frames)
            name = 'level%02d.zmv' % level if ok else 'partial.zmv'
            path = os.path.abspath(os.path.join(a.out, name))
            host.write(path, 'tools/bot.py --level %d, seed %d; %s' % (
                a.level, a.seed, 'finished level %d' % level if ok else 'gave up'))
            if pokes:
                note_pokes(path, KIT_POKES)
            log('wrote %s' % path)
            if not ok:
                return 1
    finally:
        host.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
