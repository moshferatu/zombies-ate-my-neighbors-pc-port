#!/usr/bin/env python3
"""Write the .zmv prefix that types a password and starts the level it names.

Every movie in `movies/` before this one begins the same way: mash Start through
the logos and take whatever the title menu's first entry gives you, which is
level 1. That was the binding constraint on most of Phase 3's work list — three
separate items were marked *not reachable in level 1* rather than *not written
yet* — and a password is the way past it.

What this emits is the boot half of a movie: logos, the title menu's second
entry, the character grid, the four letters, enter, and the Start that gets you
off the player-select screen. Append gameplay to the result.

    python tools/make_password_movie.py XJQY > movies/level33.zmv
    build/zamn_assets.exe password "Zombies Ate My Neighbors.sfc"   # to pick one

The grid is 7 columns x 5 rows and the cursor's pixel position is
x = 32 + 32*col, y = 71 + 16*row (`$82:B333` clamps and wraps it there). Row 4's
tail was read off `$7E:1E96`/`$7E:1E9A` rather than off the picture: column 5
deletes a character and column 6 is enter.

The screen's thread yields four ticks a pass (`$82:B263`) and the D-pad is read
as a *change* (`$82:B298  CMP $004E : BEQ`), so every step is a press and a
release rather than a hold, and both have to be long enough to be seen. It also
times out: `$82:B25A` arms 300 frames and every input rearms it, so a slow route
through the grid is fine but a pause is not.
"""

import re
import sys

GRID = ["0123456", "789BCDF", "GHJKLMN", "PQRSTVW", "XYZ!?<@"]

# The boot prefix, lifted from an existing movie so the logo skipping is one
# thing rather than two. Everything up to and including frame 1004 is Start
# mashing through the logos; the title menu is up and idle from about 1150.
PREFIX_MOVIE = "movies/level1-pickups.zmv"
PREFIX_UNTIL = 1004

HOLD = 8   # frames a button is down: the screen's thread runs every fourth
GAP = 8    # frames it is up again, so the next press is a new edge


def cell(ch):
    for r, row in enumerate(GRID):
        c = row.find(ch)
        if c >= 0:
            return c, r
    raise KeyError("%r is not on the password grid" % ch)


def build(word):
    src = open(PREFIX_MOVIE, encoding="utf-8").read()
    lines = [l for l in src.splitlines() if re.match(r"^\d+\s", l)]
    ev = [l for l in lines if int(l.split()[0]) <= PREFIX_UNTIL]

    # Down moves the title menu's cursor from START to PASSWORD; Start takes it.
    ev += ["1200   Down", "1208   -", "1260   Start", "1268   -"]

    frame = [1400]

    def press(button):
        ev.append("%d   %s" % (frame[0], button))
        frame[0] += HOLD
        ev.append("%d   -" % frame[0])
        frame[0] += GAP

    col, row = 0, 0
    for ch in list(word.upper()) + ["@"]:
        c, r = cell(ch)
        while col < c:
            press("Right"); col += 1
        while col > c:
            press("Left"); col -= 1
        while row < r:
            press("Down"); row += 1
        while row > r:
            press("Up"); row -= 1
        press("A")

    # The accepted password drops straight to the player-select screen, which
    # wants a Start of its own. A few, because how long the fade takes is not
    # worth being precise about.
    frame[0] += 140
    for _ in range(6):
        ev.append("%d   Start" % frame[0]); frame[0] += 10
        ev.append("%d   -" % frame[0]); frame[0] += 20
    ev.append("%d   -" % (frame[0] + 60))
    return ev, frame[0]


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: make_password_movie.py <4 letters>\n")
        return 2
    word = sys.argv[1].upper()
    if len(word) != 4:
        sys.stderr.write("a password is four characters\n")
        return 2
    ev, last = build(word)
    sys.stderr.write("gameplay starts after frame %d\n" % last)
    print("\n".join(ev))
    return 0


if __name__ == "__main__":
    sys.exit(main())
