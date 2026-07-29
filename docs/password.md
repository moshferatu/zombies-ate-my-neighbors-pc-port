# The password system

ZAMN has no SRAM (`PLAN.md` — RAM size `$00`, no battery), so a password is the
whole of what the game remembers about you. There are 130 of them, they are four
letters long, and every one is spelled out of four tables in bank `$82`.

This page is here for two reasons. The first is that the password system is on
PLAN.md's port order and this is the analysis half of it. The second is more
immediate: **it is the way past level 1**, and until now level 1 was the only
level any input had ever seen.

## What it looked like from the other side

By 2026-07-26 three items on Phase 3's work list had stopped being "nobody has
written that movie yet" and become "level 1 cannot do that":

* `$80:F8D6`'s slot arithmetic needs a second collision id through that routine.
  Level 1 has one — `$28`, object 8 — and it is at (1208, 71), on the far side
  of the fence along the top of the map.
* `$80:FA26` is unported and the only object whose id reaches it is inside a
  hedge.
* `enemy_survived`, and the `$81:8506` behind it, needs an enemy that lives
  through a hit. **Level 1 has none by construction**: all fourteen actors its
  list places run `$81:87F8`, whose init is `LDA #$0000 : STA $1E`, and the
  damage table's smallest non-zero entry is 1.

Bodies with health in them exist — `$81:8C17`'s is 4, `$81:C1FB`'s 2,
`$81:FA18`'s 3 — and every one of them is placed in a level nothing had reached.
So the password screen stopped being one entry on the work list and became the
thing that unblocks five of them.

## Getting there

The title menu has two entries, **START** and **PASSWORD**, and the older movies
mash Start through the logos and take the first. One `Down` before one `Start`
takes the second. (The movies were never *choosing*; they were mashing.)

The screen itself is a 7 x 5 grid of characters plus a four-slot field:

```
0 1 2 3 4 5 6
7 8 9 B C D F
G H J K L M N
P Q R S T V W
X Y Z !   ← ⏎
```

The cursor's pixel position is `$7E:1E96` / `$7E:1E9A`, and `$82:B333` clamps
and wraps it to x = `$20`..`$E0` in steps of 32 and y = `$47`..`$87` in steps of
16 — so x = 32 + 32·col and y = 71 + 16·row. Row 4's tail was read off those two
words rather than off the picture: **column 5 deletes a character and column 6
is enter.**

Three things about driving it, all of which cost a movie to learn:

* The screen's thread yields four ticks a pass (`$82:B263`), so a press has to
  be held long enough to be seen at all.
* The D-pad is read as a *change* — `$82:B298  CMP $004E : BEQ` — so a held
  direction moves the cursor exactly once. Every step is a press and a release.
* It times out. `$82:B25A` arms 300 frames and every input rearms it, so a slow
  walk across the grid is fine and a pause is not.

Any of the four face buttons picks the highlighted character (`$82:B27D  AND
#$C0C0`), and `Start` ends entry (`$82:B273  BIT #$1000`).

`tools/make_password_movie.py` emits all of that as a `.zmv` prefix given four
letters.

## The format

Four characters from a 21-letter alphabet — the consonants, at `$82:B178`:

```
H N C V W B X K G Z T D L F P S R Y J M Q
```

The grid offers the ten digits as well, and no password uses one.

The four are stored in entry order at `$7E:1EA0`..`$1EA3` and then **scrambled**:
`$82:B02D` swaps characters 0 and 2, so the two 16-bit comparands the validator
works with are `(c2, c1)` and `(c0, c3)`. That is the whole of the obfuscation,
and it is why a password does not read as two halves.

**`(c2, c1)` selects one of 13 groups.** `$82:B14A` holds thirteen pairs of
alphabet indices; the loop at `$82:B046` walks them and adds 4 to `$7E:1E7C` per
miss, having started it at 5. So group *i* is level **5 + 4i**, which is why the
game hands you a password every fourth level and why the reachable set is 5, 9,
13 … 53.

**`(c0, c3)` selects one of 10 variants.** `$82:B164` holds ten pairs, and each
index has the group's own two bytes from `$82:B18D` added to it before the
alphabet lookup — which is what stops one group's ten variants spelling another
group's. The loop at `$82:B083` counts them into `$003A` starting at 1.

And `$003A` is not a level at all. `$82:B0BE` writes it to `$7E:1D50` and
`$7E:1D52` — the victim gate — after turning ten into `$0010`:

```
LDA $003A : CMP #$000A : BNE + : LDA #$0010
+ STA $1D52 : STA $1D50
```

So the second half of a password is **how many neighbours are still out there**,
and only the tenth variant of each group is a level as it starts: `$0010` is
what a fresh level load writes (`$80:85E7`). The generator at `$82:B0DE` reads
the same tables the other way and takes the variant from `$1F9C + $1F9E - 1` in
BCD — the two players' rescue counts added together.

**One password is code rather than data.** `$82:B018` opens by comparing both
words against `$4342` and `$4644` — "BCDF" as typed — and answers by storing
**zero** into `$1E7C`, where every other password stores 5 + 4i. Nothing else in
the ROM's tables can produce a zero there.

## The tool

`src/assets/password.c` is a port-code decoder of all four tables, and
`zamn_assets password` prints them:

```
build\zamn_assets.exe password "Zombies Ate My Neighbors.sfc"
build\zamn_assets.exe password "Zombies Ate My Neighbors.sfc" xjqy
```

```
  level      1    2    3    4    5    6    7    8    9   16
      5   WHRB SHRG LHRR RHRK PHRP VHRV FHRX GHRT THRL XHRS
      9   CBGX FBGZ TBGY PBGG LBGS NBGW DBGK XBGD GBGF WBGR
     13   XFCK YFCT PFCJ JFCZ RFCR BFCB SFCG TFCL LFCP GFCY
     17   BKYZ RKYL FKYQ YKYD SKYJ WKYK PKYT ZKYP DKYR KKYM
     21   VXBB PXBG DXBR SXBK FXBP CXBV LXBX KXBT ZXBL BXBS
     25   XYLZ YYLL PYLQ JYLD RYLJ BYLK SYLT TYLP LYLR GYLM
     29   XLZG YLZD PLZM JLZT RLZY BLZX SLZZ TLZF LLZS GLZJ
     33   WJQK SJQT LJQJ RJQZ PJQR VJQB FJQG GJQL TJQP XJQY
     37   BZVG RZVD FZVM YZVT SZVY WZVX PZVZ ZZVF DZVS KZVJ
     41   BRPK RRPT FRPJ YRPZ SRPR WRPB PRPG ZRPL DRPP KRPY
     45   VLHX PLHZ DLHY SLHG FLHS CLHW LLHK KLHD ZLHF BLHR
     49   WWJX SWJZ LWJY RWJG PWJS VWJW FWJK GWJD TWJF XWJR
     53   WDSG SDSD LDSM RDST PDSY VDSX FDSZ GDSF TDSS XDSJ
```

Nothing there is transcribed: change the tables in a ROM hack and the output
changes with them, the same way `$80:F808` is read out of ROM rather than copied
into the port. Two checks say so is not vacuous — the command reads all 130
spellings back through the validator's own search and reports it, and the two
that were tried in the emulator land where they say: **WHRB** starts level 5
with `$1E7C` reading `$0005`, and **XJQY** starts level 33 with `$0021`.

## What it bought immediately

`movies/level33.zmv` is the first movie in the project that reaches a level
other than level 1, and the first thing it establishes is structural: **the port
runs a level it has never seen.** Different tileset, different actor bodies,
different music, 29 actors against level 1's 14 — and `verify` checks **48,032
calls with no divergence and no decline**, while `run` substitutes the port over
**3,589 of 3,589** scheduler passes with at most 23 bytes differing at once, all
inside the stacks or a declared scratch byte.

That is a claim the six level-1 movies could not make between them, and it is
the answer to a question this project had not been able to ask.
