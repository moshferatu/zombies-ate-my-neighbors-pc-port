#!/usr/bin/env python3
"""Walk a route by re-planning after every leg, from where the game actually is.

`zamn_assets route` searches the level's own tile attributes and prints turns.
That path is correct and it is also fragile in a way that took two rounds to pin
down: it is walkable **on the row it was planned for and on no other**. These
corridors are exactly two tiles tall -- widening the search by a single row
disconnects level 1's own route -- so a leg that snaps the player into a
neighbouring lane invalidates every leg after it. Level 45's route walks into a
wall at x=788 for exactly that reason: the planned row is open there and the row
he ended up in is not.

An open-loop fitter cannot repair that, because the error is positional rather
than temporal. So this is a closed loop instead, one leg at a time:

  1. Ask for a route from where the player *is* to where he should end up.
  2. Take only the **first** leg of it.
  3. Replay, and find the frame he actually finished that leg on.
  4. Go back to 1.

Re-planning after every leg means lane snapping stops being an error to correct
and becomes just the position the next search starts from. It costs one headless
replay per leg, which is a few seconds each.

    python tools/fit_route.py <rom> <prefix.zmv> <level> <x0> <y0> <x1> <y1> <start>
                             [--fire] [--doors] [--state saved.state]

`--state` is a machine saved by `zamn_headless --save <frame>,<file>` at or
before `<start>`, and on a movie with a long prefix it is what makes the loop
affordable at all -- see `positions` below.

Prints a complete .zmv -- the prefix, then the route -- on stdout, and says on
stderr whether it arrived.

**Level 25 starts on an escalator, and `probe` now says so to the tile.** The
spawn is (1297,546) -- the first frame `movies/level25.zmv` has a live player,
2492 -- and all six tiles of the collision box there are `$0108`. He is moving
on that frame, one pixel a frame northward, before any input is read.

The pixel this file used to call the spawn, (1303,488), is fifty-eight pixels
further up the same ride, and it is worth keeping for a different reason: there
the upper row of the box is plain floor and the lower row is three tiles of
`$0108`, so it shows that the tile `floor_effect` reads -- `x>>3, y>>3`, which
is neither of the box's corners -- is one of the three.

Two things this file used to say about that are now measured and both were
wrong. It said the no-progress guard "nudges sideways forever"; the guard could
not have run at all. A conveyor moves the player one pixel **every frame**, so
`p != prev` was true on every frame of the window, the stall detector never
fired, every leg burned its whole hold, and `landed == pos` was never true
either, so the unstick queue was retired unused on every leg. And it said the
loop "cannot cross an escalator": it can, and could before any of this round's
changes. From (1303,488) to (917,941), straight down the up escalator, it
arrives in **7 legs**. The 40, 53 and 48-leg runs recorded here
predate `STALL`, `ARRIVED = 7` and the three-length `STUCK_NUDGE`, and none of
those is what this round changed.

What did change is that both guards now measure progress **along the leg**
rather than "the position changed" -- the same test on ordinary ground and the
right one on a belt -- and that `first_leg` asks `zamn_assets route` whether the
path crosses a conveyor and doubles the hold when it does, because walking
against one is a pixel a frame rather than two. Measured on the two routes
across level 25's escalators, that is **7 legs to 5 and 6 legs to 4**, arriving
on the same pixel at the same frame both times: the legs it drops are re-issues
of a direction already held, which a truncated hold had forced the loop to
re-plan. Fewer legs is fewer replays and not a shorter movie.

Those two runs are `... 26 1303 488 917 941 2550` and `... 26 977 744 917 941
4700`. The last argument is not decoration: it has to be a frame at which the
game really has the player on the given pixel. 2681 also has him at x=1303 and
one pixel away, standing still, and it yields 18 legs instead of 5 -- because
that one pixel is the difference between the belt's row and the plain floor
above it. Nothing has yet
been routed across records 31 or 37, which carpet a tenth of the level in belts
and are where guards that were passing by luck would stop doing so.

`zamn_assets route` knows about them now as well -- `route_floor_at` in
`src/assets.c` reads the same four attribute words `floor_effect` does, the
printed route says how many of its cells are conveyor, its walk check applies
the push frame by frame in the ROM's order, and `--reach` paints them white.
What it still does *not* do is refuse to plan through one: the flood treats a
belt as open ground, because it is open ground. `movies/level25-boss.zmv` is
hand-written from before any of this -- render the map with `zamn_assets level
<rom> <n> map.png`, mark the start and the target, and tune nine legs against
`--pos`.

`--fire` holds Y down the whole way, which turns a walk into a fighting retreat.
Some objects are **contested**: `$80:CAEE` lets collision id $0004 -- the monster
side -- take one, and level 45's `$80:FAA4` object was lost to a monster standing
on it on three separate routes. Shooting is the only answer to that, and it is
free here for the same reason lane snapping is: re-planning after every leg
measures where the player *is*, so it does not matter that a shot changes the
board. Patching `+Y` onto a movie the fitter has already finished does not work
and was tried -- the trajectory diverges within a leg or two and every turn after
it is aimed at the wrong place.

**Appending fire after the last leg is a different thing and it does work**, and
it is worth reaching for first, because a fight often does not need a route at
all. `movies/level29-990b.zmv` is `movies/level29-item.zmv` unchanged through its
final turn plus one line, `3624 Down+Y`: the actor it shoots *chases*, so the
walking route had already delivered the player to it and left it following thirty
pixels behind. There are no planned frames after the last leg, so there is
nothing for firing to invalidate. Route to where a chaser will follow you, stop,
and turn round -- and save `--fire` for the objects that have to be walked to.
"""

import os
import re
import subprocess
import sys
import tempfile

HEADLESS = os.path.join("build", "zamn_headless.exe")
ASSETS = os.path.join("build", "zamn_assets.exe")

# Close enough to the goal to stop, and it is the game's own number rather than
# a tolerance: `$80:BEF1` tests a pair of display records as
# `other - self + 8 < 16` on each axis, so two things touch when they are within
# eight pixels. Seven is that box with the asymmetry taken off -- the test is
# `[-8,+7]`, and which end you get depends on which of the two records the
# display list happens to have sorted later.
#
# It was 3 before, which is stricter than the game and cost whole routes: level
# 45's fitter thrashed for twenty legs trying to stand exactly on an object it
# had been touching since leg 36.
ARRIVED = 7
# Frames of a perpendicular step, when a leg cannot finish -- tried in this
# order, so a lane change is attempted before a corridor change.
#
# **One length is not enough, and level 29 is what proved it.** 14 frames is 28
# pixels, which clears a leg that snapped one lane the wrong way. It does not
# clear a *chicane*: at (773,461) the way west is a 56-pixel detour south, and
# the fitter spent 41 legs stepping 28 pixels down, 28 back up and asking the
# same blocked question again. Re-anchoring by hand at a wall 130 pixels away cut
# the same route from 277 cells to 147 and it fitted without a thrash -- so the
# fix is to try a longer step before giving up, not to plan better.
STUCK_NUDGE = (14, 28, 56)
# How long the player must hold still before a leg that has not reached its
# target is called finished anyway. Four pixels of walking, which is well clear
# of anything the position could do on its own, and short enough to be worth
# recovering: see the note in the loop below.
STALL = 8
MAX_LEGS = 60

AXIS = {"Left": 0, "Right": 0, "Up": 1, "Down": 1}
SIGN = {"Left": -1, "Right": 1, "Up": -1, "Down": 1}


def first_leg(rom, level, x0, y0, x1, y1, doors=False):
    """The first turn of a route, or None if there is no route at all.

    The fourth value is whether the route crosses a conveyor, which changes how
    long a leg has to be held for: walking against a belt is one pixel a frame
    rather than two. It is a property of the whole path and not of the first leg
    -- being generous costs replay time and no movie frames, because the leg
    ends at the first frame the target is passed and not at the end of the hold.

    `doors` passes `--doors` through, which lets the plan cross a door block --
    scenery a key removes. **The loop cannot open one.** A door is opened by
    walking into it holding a key, and the leg that does it looks to this loop
    like a leg that stalled against a wall, so a fitted route with `--doors` on
    is a plan to be walked *up to* each doorway rather than through it. What the
    flag buys is the legs on the far side, once a hand-written push has opened
    the way and the loop is restarted from the pixel it came out on.
    """
    cmd = [ASSETS, "route", rom, str(level), str(x0), str(y0), str(x1), str(y1)]
    if doors:
        cmd.append("--doors")
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    m = re.search(r"^\s+(\w+)\s+to \((\d+),(\d+)\)", out, re.M)
    if not m:
        return None
    belt = re.search(r"^\d+ of them are conveyor", out, re.M) is not None
    return m.group(1), int(m.group(2)), int(m.group(3)), belt


def positions(rom, movie, first, last, state=None):
    """frame -> (x, y) for every frame in the range, from the game itself.

    `state` is a `--save`d machine to start from instead of the reset vector,
    and on a long prefix it is the difference between a usable loop and an
    unusable one. Fitting level 24 means replaying levels 21 to 23 first, every
    leg, for nothing: 10,350 frames of movie that cannot change, at 42 seconds,
    to measure the 200 frames that can. From a state saved at the level load the
    same leg is 0.76 seconds, and `zamn_headless` was checked frame by frame
    against a full replay before this was allowed to matter -- identical
    positions, identical watched words.
    """
    fd, png = tempfile.mkstemp(suffix=".png")
    os.close(fd)
    cmd = [HEADLESS, rom, png, str(last), "-m", movie,
           "--pos", "%d,%d,1" % (first, last)]
    if state:
        cmd += ["--load", state]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    os.unlink(png)
    got = {}
    for m in re.finditer(r"^\s+(\d+)\s+(\d+),(\d+)", out, re.M):
        got[int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
    return got


def write(path, prefix, legs, end, fire=False):
    with open(path, "w") as f:
        f.write("\n".join(prefix) + "\n")
        for fr, d in legs:
            f.write("%d   %s\n" % (fr, d + "+Y" if fire else d))
        f.write("%d   -\n" % end)


def main():
    argv = [a for a in sys.argv if not a.startswith("--")]
    fire = "--fire" in sys.argv
    doors = "--doors" in sys.argv
    state = None
    for i, a in enumerate(sys.argv):
        if a == "--state" and i + 1 < len(sys.argv):
            state = sys.argv[i + 1]
            argv = [v for v in argv if v != state]
    if len(argv) < 9:
        sys.stderr.write(__doc__)
        return 2
    rom, prefix_path = argv[1], argv[2]
    level, x0, y0, x1, y1, start = (int(a) for a in argv[3:9])
    prefix = [l for l in open(prefix_path).read().splitlines()
              if l.strip() and l[0].isdigit() and int(l.split()[0]) < start]

    fd, movie = tempfile.mkstemp(suffix=".zmv")
    os.close(fd)
    legs, frame, pos = [], start, (x0, y0)
    unstick = []

    for _ in range(MAX_LEGS):
        if abs(pos[0] - x1) <= ARRIVED and abs(pos[1] - y1) <= ARRIVED:
            break

        if unstick:
            # The previous leg made no progress at all, so the row the route was
            # planned for is not the row he is in. Step one lane the other way
            # and let the next search start from wherever that lands, which is
            # the whole reason this loop re-plans rather than corrects.
            (direction, hold), target = unstick.pop(0), None
        else:
            leg = first_leg(rom, level, pos[0], pos[1], x1, y1, doors)
            if leg is None:
                sys.stderr.write("no route from (%d,%d) to (%d,%d)\n"
                                 % (pos[0], pos[1], x1, y1))
                return 1
            direction, tx, ty, belt = leg
            target = tx if AXIS[direction] == 0 else ty
            # Two pixels a frame, or one where the floor is taking one back.
            dist = abs(target - pos[AXIS[direction]])
            hold = (dist if belt else dist // 2) + 40

        end = frame + hold
        write(movie, prefix, legs + [(frame, direction)], end, fire)
        seen = positions(rom, movie, frame, end, state)

        done = None
        if target is not None:
            for fr in range(frame, end + 1):
                p = seen.get(fr)
                if p and (p[AXIS[direction]] - target) * SIGN[direction] >= 0:
                    done = fr
                    break
        if done is None:
            # He never reached the leg's target, so he is against something. End
            # the leg where he *stopped* rather than where the replay window
            # happens to end: a player who has not moved for STALL frames has
            # either arrived or hit a wall, and both mean re-plan now.
            #
            # This is where a fitted route's slack was. Level 45's `--fire` route
            # travelled 1,838 px against an 1,808 px optimum -- 30 px of detour --
            # and still cost 1,182 frames against 904, because **263 of those
            # frames were spent standing still**, in blocks of 103, 43 and 43 at
            # the ends of legs whose `hold` had 40 frames of padding left on it.
            # Cutting them is not a better route, it is the same route sooner.
            # **Progress along the leg, not "the position changed".** Those are
            # the same thing on ordinary ground and they are not on a conveyor,
            # which moves the player one pixel every frame whether a button is
            # down or not. `p != prev` is true on every frame of a belt, so this
            # guard never fired there, the leg burned its whole hold, and the
            # loop re-planned from wherever the floor had dragged him -- which is
            # the escalator failure the docstring describes, and the mechanism is
            # the opposite of what it says. The no-progress guard did not thrash;
            # it never ran. `route_floor_at` in `src/assets.c` is the other end.
            axis, sign = AXIS[direction], SIGN[direction]
            last_change = frame
            best = seen.get(frame, pos)[axis] * sign
            for fr in range(frame + 1, end + 1):
                p = seen.get(fr)
                if p is not None and p[axis] * sign > best:
                    last_change, best = fr, p[axis] * sign
            done = last_change if end - last_change >= STALL else end - 2
        landed = seen.get(done, pos)

        gained = ((landed[AXIS[direction]] - pos[AXIS[direction]])
                  * SIGN[direction])
        if gained <= 0 and target is not None and not unstick:
            # Not one pixel of progress *along the leg*. That is `landed == pos`
            # on ordinary ground and is not the same test on a belt, where a leg
            # can end somewhere else entirely and no nearer. Try a lane either
            # side before giving up on the route; if neither helps, the loop's
            # own no-progress guard below stops it.
            both = ["Up", "Down"] if AXIS[direction] == 0 else ["Left", "Right"]
            unstick = [(d, n) for n in STUCK_NUDGE for d in both]
            continue

        legs.append((frame, direction))
        frame = done + 2
        # Same correction as the guard above: on a belt `landed != pos` is true
        # every leg, which would retire the nudge queue without any of the
        # nudges having achieved anything.
        if gained > 0:
            unstick = []
        pos = landed

    write(movie, prefix, legs, frame + 40, fire)
    sys.stdout.write(open(movie).read())
    os.unlink(movie)
    ok = abs(pos[0] - x1) <= ARRIVED and abs(pos[1] - y1) <= ARRIVED
    sys.stderr.write("%s at (%d,%d) after %d legs, last frame %d\n"
                     % ("arrived" if ok else "STOPPED", pos[0], pos[1],
                        len(legs), frame))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
