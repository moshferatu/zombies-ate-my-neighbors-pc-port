#!/usr/bin/env python3
"""Break one line of the port on purpose, rebuild, and see whether the diff says so.

Every round of Phase 3 ends the same way: change the port so it disagrees with
the ROM, run `zamn_cosim verify`, and write down which changes the harness
catches and which it does not. A change that is *not* caught is the useful half
of the exercise -- it names a branch no movie takes, or a value that flows
through a decision only one way -- and PROGRESS.md has carried those lists for
a dozen rounds. The exercise itself was hand-done every time, which is how it
picked up the same two bugs twice.

Both are encoded here.

**It anchors uniquely.** `str.replace(old, new, 1)` edits the *first* match in
the file, and this port has nine copies of the same collision subsystem: three
lines that look unique inside one routine are verbatim the same three lines in
a routine four hundred lines above it. Two perturbations came back MISSED that
way, having broken code the movie never runs. Every anchor below is counted
first and a count that is not exactly one is a hard error.

**It puts the binary back, not just the file.** An earlier version restored the
source and left `build/zamn_cosim.exe` built from the perturbed one, and the
next `verify` reported a divergence in a tree that was correct -- an hour spent
chasing a bug that did not exist. Restoring is a rebuild, and it happens in a
`finally` so a Ctrl-C leaves the tree consistent too.

    python tools/perturb.py                 # every perturbation
    python tools/perturb.py --list
    python tools/perturb.py f4ef_p2_only    # one, by name

A perturbation is CAUGHT if `verify` exits non-zero, and the first divergence it
printed is echoed beside the verdict, because *where* it failed is most of what
the exercise is for.

**A perturbation is run against every input listed for it, and the ones that
disagree are the point.** `a264_claim_latch` is caught by the movie where the
first player rescues the neighbour and missed by the movie where the second one
does, from a single broken line -- which is the whole thesis of the coverage
report written as a two-line verdict. A perturbation caught by every input it is
offered says less than one caught by exactly one.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = "Zombies Ate My Neighbors.sfc"


# Each entry: the file to edit, an anchor that occurs exactly once in it, what to
# put there instead, and the inputs to check it against -- `(movie, frames)`,
# one or several. `why` is what the perturbation is asking; read it as the
# question the answer belongs to.
PERTURBATIONS = [
    dict(
        name="bubble_slot_forced",
        file="src/port/collide.c",
        why="$81:9BA2 credits the bubble to the side that fired it; force the first player's slot",
        old="  uint16_t slot = side == wram_r16(w, W_SCORE_SLOT_SIDE) ? 0 : 2;\n"
            "  PORT_COVER_IF(slot == 0, d9b6b_bubble_slot_0, d9b6b_bubble_slot_1);",
        new="  uint16_t slot = 0;  /* PERTURBED */\n"
            "  PORT_COVER_IF(slot == 0, d9b6b_bubble_slot_0, d9b6b_bubble_slot_1);",
        inputs=[("movies/level21-p2-bubble.zmv", 6700)],
    ),
    dict(
        name="bubble_side_bit14",
        file="src/port/collide.c",
        why="the side is bit 15 of Y, the shooter's half of the collision id; take bit 14 instead",
        old="  uint16_t side = (uint16_t)((r->y & 0x8000) ? 2 : 0);\n\n"
            "  // `$81:9BA9  JSL $80:9D6A`",
        new="  uint16_t side = (uint16_t)((r->y & 0x4000) ? 2 : 0);  /* PERTURBED */\n\n"
            "  // `$81:9BA9  JSL $80:9D6A`",
        inputs=[("movies/level21-p2-bubble.zmv", 6700)],
    ),
    dict(
        name="f4ef_p2_only",
        file="src/port/collide.c",
        why="$82:F4EF answers both players' ids; answer only the first player's",
        old="  if (arg == F4EF_LATCH_P1 || arg == F4EF_LATCH_P2) {",
        new="  if (arg == F4EF_LATCH_P1) {  /* PERTURBED */",
        inputs=[("movies/level21-p2-bubble.zmv", 6700)],
    ),
    dict(
        name="a264_claim_latch",
        file="src/port/collide.c",
        why="$83:A293 latches $8000 for id $06 and the id itself for $05; latch $8000 for both",
        old="    uint16_t claimant = arg;\n"
            "    if (arg == A264_ID_CLAIM_B) {",
        new="    uint16_t claimant = 0x8000;  /* PERTURBED */\n"
            "    if (arg == A264_ID_CLAIM_B) {",
        inputs=[("movies/level21-rescue.zmv", 3000),
                ("movies/level21-p2-rescue.zmv", 3000)],
    ),
    dict(
        name="a264_claim_event",
        file="src/port/collide.c",
        why="a rescue and a give-up write different words into $1E; write the give-up one",
        old="    wram_w16(w, (uint32_t)dp + A264_DP_EVENT, A264_EVENT_CLAIMED);\n"
            "    r->a = A264_EVENT_CLAIMED;",
        new="    wram_w16(w, (uint32_t)dp + A264_DP_EVENT, A264_EVENT_GIVE_UP);  /* PERTURBED */\n"
            "    r->a = A264_EVENT_CLAIMED;",
        inputs=[("movies/level21-rescue.zmv", 3000),
                ("movies/level21-p2-rescue.zmv", 3000)],
    ),
    dict(
        name="a264_flag_slot_0",
        file="src/port/collide.c",
        why="$81:8191 sets the flag byte at the victim's own index; set the first one instead",
        old="  wram_w8(w, W_A264_FLAG_ARRAY + (uint32_t)index, A264_FLAG_SET);",
        new="  wram_w8(w, W_A264_FLAG_ARRAY, A264_FLAG_SET);  /* PERTURBED */",
        inputs=[("movies/level21-rescue.zmv", 3000),
                ("movies/level21-p2-rescue.zmv", 3000)],
    ),
]


def build():
    """Configure+build through tools/build.ps1, which owns the MSVC environment."""
    r = subprocess.run(
        ["powershell", "-ExecutionPolicy", "Bypass", "-File",
         os.path.join("tools", "build.ps1")],
        cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")


def verify(movie, frames):
    """Run `zamn_cosim verify`. Returns (ok, first line that looks like a diff)."""
    r = subprocess.run(
        [os.path.join("build", "zamn_cosim.exe"), "verify", ROM,
         "-m", movie, "-f", str(frames)],
        cwd=ROOT, capture_output=True, text=True)
    detail = ""
    for line in (r.stdout + r.stderr).splitlines():
        s = line.strip()
        if s.startswith(("WRAM $", "flag ", "APU ")) or s.startswith(("A:", "X:", "Y:")):
            detail = s
            break
        if "DIVERGED" in s and not detail:
            detail = s
    return r.returncode == 0, detail


def apply(entry):
    """Edit the file in place and hand back the bytes needed to undo it.

    Read and written as **bytes**, not text. Python's text mode translates CRLF
    to LF on the way in and would write LF back out, so a restore that went
    through `str` would silently reformat every line of a CRLF file -- a
    whole-file diff, produced by a tool whose entire job is to make one line
    wrong and then put it back exactly.
    """
    path = os.path.join(ROOT, entry["file"])
    with open(path, "rb") as f:
        original = f.read()
    crlf = b"\r\n" in original

    def encode(s):
        b = s.encode("utf-8")
        return b.replace(b"\n", b"\r\n") if crlf else b

    old, new = encode(entry["old"]), encode(entry["new"])
    n = original.count(old)
    if n != 1:
        raise SystemExit(
            "%s: anchor occurs %d times in %s, not once. Widen it -- this port "
            "has nine near-identical copies of the collision subsystem and an "
            "ambiguous anchor breaks the wrong one."
            % (entry["name"], n, entry["file"]))
    with open(path, "wb") as f:
        f.write(original.replace(old, new, 1))
    return path, original


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("names", nargs="*", help="perturbations to run (default: all)")
    ap.add_argument("--list", action="store_true", help="print the table and exit")
    args = ap.parse_args()

    if args.list:
        for e in PERTURBATIONS:
            movies = " ".join(os.path.basename(m) for m, _ in e["inputs"])
            print("%-22s %-48s %s" % (e["name"], movies, e["why"]))
        return 0

    chosen = [e for e in PERTURBATIONS if not args.names or e["name"] in args.names]
    unknown = set(args.names) - {e["name"] for e in PERTURBATIONS}
    if unknown:
        raise SystemExit("unknown perturbation(s): %s" % ", ".join(sorted(unknown)))

    print("Baseline build...")
    build()

    caught = 0
    for e in chosen:
        print("\n=== %s\n    %s" % (e["name"], e["why"]))
        path, original = apply(e)
        try:
            build()
            # Every input, not just the first: a perturbation one movie catches
            # and another does not is worth more than either verdict alone.
            results = [(movie, verify(movie, frames)) for movie, frames in e["inputs"]]
        finally:
            with open(path, "wb") as f:
                f.write(original)
            build()  # the tree and the binary go back together, or not at all
        for movie, (ok, detail) in results:
            name = os.path.basename(movie)
            if ok:
                print("    MISSED  %-26s does not distinguish it" % name)
            else:
                print("    CAUGHT  %-26s %s" % (name, detail or "(verify exited non-zero)"))
        if any(not ok for _, (ok, _) in results):
            caught += 1

    print("\n%d of %d caught by at least one input." % (caught, len(chosen)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
