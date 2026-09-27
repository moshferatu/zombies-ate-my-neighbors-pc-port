"""How much of the work the ROM does is the port doing?

`PROGRESS.md` has counted ported routines since Phase 3 began, and 47 of them is
a real number with no denominator attached: it cannot tell a 17-byte leaf from a
2 KB state machine, and both count as one. This answers the question the routine
count cannot -- **what share of the instructions the game actually executes are
inside routines the port has** -- and ranks what is left by the same measure, so
that "what should be ported next" is a number rather than a taste.

It needs `profile.bin`, which `zamn_trace` writes beside the CDL: one word per
ROM byte, counted at the opcode, so summing it over a range gives instructions
executed rather than bytes touched.

    build/zamn_trace.exe <rom> -o analysis/prof/level1 -f 6100 -m movies/level1.zmv
    python tools/native_share.py analysis/prof/*

**`--residue` reads the other kind of profile**, the one the game writes with
`zamn.exe --profile <dir>`: the same three files, counted while the port was
substituted, so what is in them is only what the 65816 still executed. There
is nothing to infer there -- no closure, no call weighting -- and no share
either, because the port's own work never reached the counters. What it does
give is the ranking, from real play on any level, which the traced corpus
cannot: eleven movies over ten of the fifty-six level records.

    build/zamn.exe --profile analysis/residue/playtest ...
    python tools/native_share.py --residue analysis/residue/playtest

**Attribution is by nearest preceding subroutine entry**, which is what a
sampling profiler does with a symbol table and carries the same caveat: code
entered only by a jump is credited to the routine above it. That is usually the
same routine -- a branch target inside a body, or its tail -- and is wrong where
the ROM jumps between two things that happen to be adjacent. The `entries` count
in the report is how many boundaries the attribution had to work with; a routine
with a suspiciously large share is worth checking against the disassembly before
it is believed.

Two numbers come out, and they answer different questions:

  * **static** -- of the distinct code bytes the game executed at all, what
    share is inside ported routines. This is "how much of the ROM have we
    written", and it is the flattering one.
  * **dynamic** -- of the instructions executed, what share ran inside ported
    routines. This is "how much of the work have we taken over", and it is the
    one that says whether the next routine is worth the round.
"""

import glob
import os
import re
import struct
import sys
import collections

import numpy as np

BANK_SIZE = 0x8000
CDL_CODE = 0x01
CDL_SUB = 0x08

def load_wait_sites(path='src/cosim/waits.h'):
    """Busy-waits, as (address, bytes, what it is waiting for).

    These are counted by the profiler like any other instructions, and they are
    not work: a 65816 going round `BIT $00C8 : BPL` a hundred thousand times is
    a CPU with nothing to do until the next VBlank. Left in the denominator they
    make the port's share look smaller than it is, and left in the ranking they
    put a two-instruction loop at the top of the list of things to port next --
    where porting it would achieve exactly nothing, because the C would have to
    spin on the same flag.

    **The table used to live here and now lives in C**, because the substituted
    build measures the same share live and needs the same exclusions: the game
    prints its own native share when you quit it, and a denominator that the
    offline tool and the running game disagreed about would make those two
    numbers incomparable for no reason. `src/cosim/waits.h` is the one copy and
    carries the argument for each site; this reads it.
    """
    src = open(path, encoding='utf-8', errors='replace').read()
    sites = [(int(a, 16), int(n), what) for a, n, what in re.findall(
        r'\{\s*0x([0-9A-Fa-f]+)\s*,\s*(\d+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}', src)]
    if not sites:
        raise SystemExit('%s: no wait sites found -- has the shape of the '
                         'table changed? See the note in that file.' % path)
    return sites

# Routines the ranking will keep putting near the top and which the harness
# cannot take, for reasons that are structural rather than a matter of effort.
# They stay in the list -- their work is real and the denominator is honest --
# but they are marked, because the ranking is used to pick rounds and without
# this it points four deep into a wall.
#
# The two dispatchers are the interesting case. Neither is a leaf in any sense:
# each pushes a far return address, pushes a job's address out of a WRAM table
# and `RTL`s into it, so "port this routine" means "port every job that can be
# in the table". Queue A has thirteen distinct enqueue sites and queue B has
# one. What the profile credits to them is not the jobs, though -- attribution
# stops at the next subroutine entry -- it is the scan: sixteen slots from the
# top down, every call, whether two are live or none, which is 146 instructions
# a frame in A and 53 in B.
# Routine starts the CDL cannot see, because nothing ever `JSR`/`JSL`s to them.
#
# Attribution is by nearest preceding entry, so a routine missing from the
# entry list does not merely score zero: **its work is charged to whatever sits
# above it**, and — worse — the call-graph edges leaving it are charged there
# too, because `owner_of` maps a call site to the entry above it. A `JSL` in an
# orphan can therefore make the orphan's *callee* look like it is called only
# from ported code, and the subsumption closure will then mark that callee, and
# everything below it, native.
#
# That is not hypothetical. `$80:A937` is a four-byte `JSL` thunk that begins
# one byte past `$80:A8B3`'s `RTL`; nothing calls it, so it belonged to
# `$80:A8B3`, so the round that ported `$80:A8B3` silently claimed `$80:A93F`
# and the four tilemap scroll routines under it — 1.6 points of native share
# that nobody had written a line of C for. Anything added here should be
# checked the same way: port a neighbour, and see whether the number moves by
# more than the neighbour is worth.
JUMP_ENTRIES = {
    0x80A937: 'a JSL thunk one byte past $80:A8B3 RTL, into $80:A93B',
    0x82AC07: 'the real start of what the ranking credits to $82:AB5B',
    # $80:CDEB lzss_write_byte is five instructions and ends at its RTS on
    # $80:CDF3. What follows is a different routine, reached by a jump, and it
    # opens `JSR $D13A` and then `JSL thread_yield` -- so without this line its
    # call edges are credited to lzss_write_byte, and registering a five-byte
    # leaf silently subsumes $80:D13A, $80:D1EA, $80:D01B and their closures.
    # Caught by the check the $80:A937 round installed: registering the two LZSS
    # leaves moved the native total by 8,382,014 where the routines themselves
    # execute 7,113,522.
    0x80CDF4: 'the real start of what the ranking credits to $80:CDEB',
    # $81:81A2 ends at its RTS on $81:81EE, with a two-instruction alternate
    # tail at $81:81EF; the entry itself executes 3,234 instructions in ten
    # profiles. Everything the ranking credited to it -- 4,671,086, or 1.46% --
    # belongs to what starts here, which is not a subroutine at all: it opens
    # `PEI $02 : PLB`, drops into a loop around `JSL thread_yield`, and is
    # entered by the scheduler resuming a thread rather than by any JSR.
    # Without this line, `$81:81A2` reads as the top portable row on the whole
    # ranking at 1.6% over 79 calls, and it is neither.
    0x8181F6: 'the real start of what the ranking credits to $81:81A2',
    # $80:C8B8 dma_to_vram is fifteen instructions and ends at its RTL on
    # $80:C8DB: 68,355 instructions over 4,557 calls, 0.02%. The 0.91% the
    # ranking added to that begins here, and this is a thread body too --
    # $80:87C1, inside the level loader, does `LDA #$C8F6 : LDY #$0080 :
    # JSL thread_spawn`, and nothing in the ROM JSRs or JSLs to it or holds a
    # pointer to it. It opens `JSR object_list_parse` and then loops on
    # `JSL thread_yield`, spawning objects near the camera centre. Note that
    # its direct page is a thread's own 128-byte page, so the $0C/$0E/$10/$12
    # the listing annotates as vbl_queue_* are nothing of the kind -- they are
    # this thread's locals, and the symbol names are actively misleading there.
    # Between them the two routines at $80:C8DB and $80:C8DC are also the
    # tidiest example of why the listing is a lower bound: $80:C8DC is a 16x16
    # hardware multiply through $211B/$2134, entirely .db, and executed zero
    # times in all ten profiles.
    0x80C8F6: 'the real start of what the ranking credits to $80:C8B8',
    # $80:8002 init_ppu_regs is 44 instructions and ends at its RTS on
    # $80:80AD: 630 instructions over ten profiles, 63 per call, 0.0002%. The
    # 0.5% the ranking added to that is the **reset vector** -- $00:FFFC holds
    # $80AE -- and 1,310,710 of its 1,311,180 instructions are two bytes:
    # `MVN $7E,$7E` at $80:8116 and `MVN $7F,$7E` at $80:813B, which clear
    # both WRAM banks a byte per instruction. A block move is genuinely 65,536
    # instructions on this CPU, so the work is real; it is simply the machine
    # coming up, once per movie, and nothing calls it.
    #
    # This is the fourth kind of code that runs and is never called, and the
    # only one that is not a family: the reset entry and the NMI entry are the
    # two hardware vectors, and the other two vectors are inert -- $80:8000 is
    # a bare `COP #$FE` (BRK, COP in emulation) and $80:8209 a bare `RTI`
    # (IRQ, ABORT, COP), both executed zero times in all ten profiles.
    0x8080AE: 'the reset vector, which is not called and cannot be shimmed',
    # $81:BC3D ends at its RTS and is 112,743 instructions over 1,775 calls --
    # 63 per call for a routine of about that many bytes, which is the shape of
    # a leaf. The 581,911 the ranking credited to it is four fifths something
    # else: $81:BEDA, reached by `JMP` from $81:BB8F and $81:BBE7 and by
    # nothing that calls it, so the CDL never marks it and attribution walks
    # back past it. On its own it is 469,168 instructions, 0.16%, and it ranked
    # as the top *portable* row on the whole list for two rounds while being
    # neither the routine named nor anything anyone could shim at that address.
    #
    # It is also a fourth family, and this one is not enumerated here. $81:BEDA
    # is three instructions -- `LDA #$BEE3 : STA $12 : RTS` -- that install the
    # *next* state of the monster's behaviour in `$12`, which its thread then
    # reaches with `PEA <ret> : LDA $12 : DEC : PHA : RTS`. So the state bodies
    # under it are entered by a computed `RTS` through a WRAM word: no JSR, no
    # JSL, and no spawn idiom to grep for. Declaring the installer separates the
    # two rows; the bodies below it stay charged to it, and that is a lower
    # bound again.
    0x81BEDA: 'a state installer reached by JMP; what the ranking credited to $81:BC3D',
    # ...and the round that ported $81:BB75 and $81:BBA4 -- the two routines
    # whose tail *is* $81:BEDA -- found the body the installer names, because
    # writing the port meant reading the constant it stores. $81:BEDA is four
    # instructions (`JMP $BEDD` and the three it lands on) and the 469,168 above
    # is almost all $81:BEE3, the monster's chase: `LDX $0A : LDY $0C : JSL
    # actor_nearest` and then two hundred bytes of steering. Declaring it makes
    # the installer's row the size of an installer and gives the chase a row of
    # its own, which is where it belongs. Nothing calls it, which once meant it
    # could not be ported; it is now, as `monster_chase` in src/port/chase.c,
    # entered at the address the thread's `RTS` lands on as the walk is.
    0x81BEE3: 'the monster chase state, entered by computed RTS through $12',
    # The five handlers in the word table at $80:D74F, reached through it by
    # `JMP $F300` and never called. Found the same way as the vblank jobs
    # queued by JML (see VBL_JOBS): a residue profile charged 110,349
    # instructions to `step_propose`, whose every call the port had served,
    # and the traced corpus had been counting all 991,403 of $80:E4BA's as
    # native for the same reason -- it opens `JSR $E450`, which is
    # `step_propose`, and it sits right below it.
    0x80E4BA: 'a handler from the table at $80:D74F; what the ranking credited to step_propose',
    0x80E595: 'a handler from the table at $80:D74F',
    0x80E5FF: 'a handler from the table at $80:D74F',
    0x80E653: 'a handler from the table at $80:D74F',
    0x80E6C2: 'a handler from the table at $80:D74F',
    # A state that `$81:AF9A`, `$81:B193` and `$81:B221` install with `LDA
    # #$AEA6 : STA $0E`, and `$81:B164` jumps to. Nothing calls it, and the
    # 56-record residue charged its 836,345 instructions to `enemy_ac92`, a
    # ported routine the port had served on every call.
    0x81AEA6: 'a state installed through $0E; what the residue credited to enemy_ac92',
}

BLOCKED = {
    # $80:CD20 is not on this list any more either, and it is the one that
    # settles what "data-dependent" was worth as a reason. It sat here saying
    # the cost could not be priced because the compressed stream picks every
    # branch -- which is true, and which turned out not to be an obstacle at
    # all. The port decompresses the same stream, so it takes the same branches,
    # so it can count them: `LzssWork` in `port/lzss.h` is the tally and
    # `lzss_cycles` in `cosim/routines.c` prices it, and the total came out
    # equal to the ROM's own elapsed cycles on all five calls level1.zmv makes,
    # to the cycle, the largest of them 12.9 million. A price nobody can predict
    # in advance is not the same thing as a price nobody can compute.
    # $80:AD2B left this list the round after $80:C05A did, and for the same
    # reason plus one. The same reason: `verify` cannot check a ten-frame call,
    # which is a fact about rewind-and-replay and not about substitution, so it
    # is registered `run_only`. The one more: ten frames is ten NMIs and the
    # core latches one, so it needed the parked burn -- `CosimBurn` in
    # `src/cosim/cosim.c` -- before substituting it could be honest about the
    # interrupts it owes the game. Priced by counting: no branch in it depends
    # on the data it reads, so the four loop trip counts are the whole cost.
    # $80:C05A was the third of them and is not on this list any more, which is
    # worth a note where it sat rather than a silent deletion. It is 16,900
    # instructions of `STA $2128,X : DEX : DEX : BPL`, 346,734 master cycles, so
    # an NMI lands inside virtually every call and `verify` abandons all of them
    # -- two on boot.zmv, two on level1.zmv, none checked. That makes it
    # unverifiable, and this list read it as unregisterable, which was one
    # instrument too narrow: an interrupt breaks rewind-and-replay and breaks
    # nothing about substitution, because under `run` the ROM never executes the
    # routine at all. It is registered `run_only`, priced by counting rather
    # than by measurement, and checked by `run`'s per-pass comparison of all
    # 128 KB instead of per call. See `CosimRoutine::run_only`.
    # $80:CC7C, the sound-data uploader, was here as "written, unregisterable:
    # it outlives a frame". It is registered now, with its waits in the trace
    # and its interrupts set aside (`CosimRoutine::through_interrupts`).
    # $80:CDF4 is already a JUMP_ENTRIES line above, because it is the real
    # start of what the ranking used to credit to lzss_write_byte. Declaring it
    # made it visible and also made it obvious what it is: `JSR $D13A : LDA
    # #$0001 : JSL thread_yield : ... : BRA $CDF7`. It is a **loop with a yield
    # in it and no exit** -- the level's main body. Nothing calls it, it never
    # returns, and the profile agrees: 651,060 instructions and zero calls in
    # eleven movies. It is the player's frame, not the level's, and since the
    # step-five round its stretches are ported (BODY_STRETCHES below). What
    # stays here is the entry itself, one `JSR $D13A` a level.
    0x80CDF4: 'a thread body, and a loop with no exit -- nothing calls it',
    # $80:CB61, the IPL upload, and $80:CB1A, its caller, were here as a
    # per-byte SPC handshake that every call is interrupted in. `apu_boot` is
    # registered at $80:CB1A with the upload inside it, for the same reasons
    # as $80:CC7C above.
    # $80:8353 thread_yield and the two dispatchers at $80:83E0 and $80:843D
    # were here, as "the coroutine primitive" and "a dispatcher -- RTLs into
    # any of 13 jobs". Neither returns, and that was the whole objection: the
    # harness substituted calls. It now also takes a routine that leaves by a
    # jump of its own (`CosimRoutine::exits`), entered at any instruction, so
    # they are registry entries -- with `$80:8401`, `$80:845E`, `$80:8372`,
    # `$80:8380` and their `$00` twins -- and off this list.
    #
    # The NMI's handler went the same way, in four pieces between its hardware
    # accesses. What is left under this row is the bank $00 trampoline, the
    # `LDA $4210`/`STA $2100` pair either side of the queues, and the
    # auto-joypad wait.
    0x80816C: 'the NMI trampoline and its hardware accesses, which stay the ROM\'s',
    # The reset vector's work was the two block moves that clear WRAM, and
    # those are `$80:80C1 reset_clear` now, entered where `init_ppu_regs`
    # returns. What stays is the mode switch, the stack, and the hardware on
    # either side.
    0x8080AE: 'the reset vector\'s hardware setup, around reset_clear at $80:80C1',
}

# ...and every vblank job, for the same reason as the NMI entry: the dispatcher
# reaches it by `RTL`, so there is no call for the harness to substitute. See
# VBL_JOBS below for how the list is derived.
BLOCKED.update({a: 'a vblank job -- reached by RTL from a queue, never called'
                for a in ()})  # filled in below, once VBL_JOBS exists


# Every vblank job in the ROM, and a whole class of misattribution.
#
# The two queues store a job as **`addr - 1`** and the dispatchers reach it by
# pushing that and executing `RTL` (`$80:83E0`, `$80:843D`). So a vblank job is
# never the target of a `JSR` or a `JSL`, the call graph therefore has no edge
# into it, and attribution-by-nearest-preceding-entry always credits it to
# whatever subroutine happens to sit below it in the ROM. Every one of these is
# invisible to the ranking until it is declared, and they are not small: five of
# them stacked on `$82:8138` turned a 105,000-instruction palette copier into a
# 0.6 per cent row, which is how the class was found.
#
# This is the same structural problem as `$81:81F6` and `$80:C8F6` -- code that
# runs and is never called -- and it is the third and largest family of it. The
# other two had to be found one at a time. This one does not, because the ROM
# registers a job with a fixed three-instruction idiom:
#
#     LDA #$<addr> : LDY #$00<bank> : JSL $8083AE    (queue A)
#                                     JSL $80841 8   (queue B)
#
# so the set below is every match for it in the cartridge, and reproducing it is
# a twelve-line script rather than an afternoon of reading. A job that is also a
# registry entry (`$80:9E7B vram_queue_flush`) is already a boundary and appears
# here harmlessly.
#
# They are also **unportable for the same reason they are invisible**: the
# harness substitutes per call, and nothing calls these. So they go into BLOCKED
# too, alongside the NMI entry and the two dispatchers that run them.
VBL_JOBS = frozenset((
    0x808B17, 0x808B2B, 0x808B48, 0x808B5C, 0x808B70, 0x808B82,
    0x80938E, 0x80953B, 0x809A1B, 0x809A90, 0x809BFC, 0x809C63,
    0x809C7D, 0x809CB2, 0x809E3E, 0x809E7B, 0x809ED0, 0x809F62,
    0x809FDF, 0x80A084, 0x80A09E, 0x80A2AB, 0x80AC55, 0x80C2AB,
    0x80EC7A, 0x80ECA0, 0x828163, 0x82819A, 0x8281C9, 0x828209,
    0x828259, 0x8282B5, 0x828308, 0x828425, 0x82882C, 0x8288F0,
    0x828903, 0x828C49, 0x829644, 0x829657, 0x829CA6, 0x829D4D,
    0x82A9D1, 0x82A9E5, 0x82AE44, 0x82AEB4, 0x82B1F9, 0x82B82E,
    0x82B9B6, 0x82D88C, 0x82DAA0, 0x82DADE, 0x82DC0C, 0x82E076,
    0x838255, 0x83B0EC, 0x83B100, 0x83C949, 0x83C95D,
    # ...and four the idiom above does not match, because the registration is
    # the routine's last act and so a tail jump: `LDA #<addr> : LDY #$00<bank>
    # : JML $8083AE` (or `$808418`). Found by the first residue profile, which
    # charged 206,138 instructions to `terrain_out_of_bounds` after the port had
    # served every one of its calls: they were `$80:B947`, the OAM upload
    # `$80:C049` queues each frame, credited to the ported routine above it and
    # so counted native in the traced corpus too, at 0.63% of every instruction
    # there. `$80:A937` is the queue-B thunk JUMP_ENTRIES already names.
    0x80A937, 0x80B947, 0x80C34A, 0x82DC4F,
))

# `$80:9E7B vram_queue_flush` is the one exception in the fifty-nine, and it is
# worth stating rather than hiding: `$80:A676` registers it as a queue-A job in
# the ordinary way, *and* `$80:81A2` inside the NMI calls it outright, 60,940
# times over the corpus. It is therefore the one job the harness can substitute,
# because there is a call to intercept -- so it is an entry like the rest but is
# not blocked. It is also the only one of the fifty-nine with an inbound edge in
# the call graph, which is how it was found.
VBL_JOB_CALLED = frozenset({0x809E7B})

# ...and since the jobs that write the PPU are ported (`src/port/vblank.h`),
# any job can be: the harness substitutes at any instruction the ROM reaches,
# and a job's `RTL` back to the dispatcher is an ordinary return. These are the
# ones that are. They stay in the vblank family, and are not blocked.
PORTED_JOBS = frozenset((
    0x80B947,  # sprite_upload_flush
    0x809E3E,  # bg2_scroll_job
    0x828209,  # camera_scroll_job
    0x809BFC,  # scroll_shadow_job
    0x8281C9,  # boss_bg_dma
))

BLOCKED.update({a: 'a vblank job -- reached by RTL from a queue, never called'
                for a in VBL_JOBS - VBL_JOB_CALLED - PORTED_JOBS
                if a not in BLOCKED})


# Every thread body in the ROM, and the second family of "runs and is never
# called" reduced to a script.
#
# `$80:825E thread_spawn` takes the far entry in `A:Y`, and its second
# instruction is `DEC` -- it parks **`addr - 1`** in a nine-byte frame that the
# scheduler resumes with `RTL`, which is precisely what the two vblank queues do
# with a job. So a thread body has no inbound call edge either, the call graph
# cannot see it, and attribution-by-nearest-preceding-entry credits it to
# whatever subroutine sits below it. `$81:81F6` and `$80:C8F6` were each found
# by hand, a round apart, after a ranking row made no sense; this is the same
# search done once.
#
# The spawn idiom is `LDA #$<addr> : LDY #$00<bank> : JSL $80825E`, and matching
# it over the cartridge resolves 32 of the 67 spawn sites. Three more are
# spawned from tables that are themselves in ROM and are expanded below.
#
# **The remaining four read the address from data.** This file used to say there
# was no static way to follow them; for two of the four that was wrong, and
# `$82:BB0D` is what proved it -- a 0.3% row with ten calls whose work turned
# out to be 7 KB downstream, in `$82:D7CF`, a thread body reached from a level
# record. The address is data, but the data is in the cartridge:
#
#   * `$80:886D` is `LDA $9F8002,X : STA $10` with X = level*2, so `$9F:8002` is
#     a table of level-record bases (index 56 holds $8000, the table's own
#     address, and is the sentinel: 56 records, levels 0..55).
#   * `$80:8774` spawns the single far entry at record offset **$18/$1A**, when
#     it is non-zero -- 35 levels have one, five distinct bodies.
#   * `$80:87CB` is `CLC : LDA #$003C : ADC $10 : PHA`, and the loop above it
#     walks **eight-byte (entry far, parameter far) records** from offset
#     **$3C**, spawning each until a zero entry -- 36 levels, seven distinct
#     bodies, some listed more than once with different parameters.
#
# All twelve contain `JSL $808353 thread_yield` within $C5 bytes of their entry,
# which is the check that says they are threads and not mis-parsed data: only a
# thread yields. `tools/levelthreads.py` regenerates the list and asserts that
# all 56 records parse -- every entry a code pointer, every list terminating.
#
# **The other two remain a lower bound.** `$81:80E7` takes the address from WRAM
# and `$81:81D7` walks a list through `($0C),Y`; anything they start that is not
# below stays misattributed. So this is now "every thread body the cartridge
# names", which is more than the code names and still not provably all of them.
THREAD_BODIES = frozenset((
    # from `LDA #imm : LDY #imm : JSL thread_spawn`
    0x8084B1, 0x80A36E, 0x80C8F6, 0x8180EC, 0x8181F6, 0x81ABF5,
    0x81B4EA, 0x81B664, 0x81CF10, 0x81D4C9, 0x81E72C, 0x81EEB7,
    0x81F159, 0x81F2B2, 0x81F380, 0x82D8DB, 0x82DCA0, 0x82DEFB,
    0x82DF6B, 0x82E0B4, 0x82F03E, 0x82F1E6, 0x82F49E, 0x82F6EB,
    0x82F70B, 0x839776, 0x83B1DD, 0x83B277, 0x83B55E, 0x83B8F8,
    0x83C687, 0x83CA4A,
    # ...plus `$80:ED8C`, eight six-byte records of (addr, bank, parameter),
    # read at `$80:ED74`. `$81:F380` appears here and above, which is the
    # cross-check that says the record layout was read correctly.
    0x81FCB2, 0x81FAF5, 0x81E8A8, 0x81EAE6, 0x81EBE2, 0x81F976,
    0x81F55E,
    # ...and `$82:C209`, twelve four-byte records read at `$82:C0ED` and
    # `$82:C131`, the first of them a null entry. `$83:9776` is the same kind of
    # cross-check.
    0x839699, 0x839843, 0x83993D, 0x839C6D, 0x839D00, 0x839E15,
    0x839EBE, 0x839FE2, 0x839A89, 0x839BAD,
    # ...and the level records in bank $9F, per the comment above. One far entry
    # at record offset $18, spawned at `$80:8774`:
    0x80A0AD, 0x80A0EF, 0x80A137, 0x80A222, 0x80A264,
    # ...and the eight-byte (entry, parameter) list at record offset $3C,
    # spawned at `$80:87FB`. `$82:A8BB` and `$82:A8C3` are two entry points into
    # one body -- both reach the same `thread_yield` at `$82:A8EB`.
    0x82873C, 0x829569, 0x82A8BB, 0x82A8C3, 0x82AB95, 0x82D7CF,
    0x83AD33,
    # ...and one of the four the comment above calls a lower bound, found by the
    # check the `$80:A937` round installed rather than by a search. Registering
    # `$81:C16B monster_anim` moved the native total by 291,771 where the
    # routine itself executes 145,410; the other 146,361 begins at `$81:C1FB`,
    # opens `JSR $B9F9 : JSR $BA46 : CLC : LDA $00DE : ADC #$001C` -- the spawn
    # charge -- installs `$81:C440` as its collision handler and then loops on
    # `thread_yield`. **No instruction in the cartridge names the address.**
    # Nothing `JSR`s or `JSL`s it, no `LDA #imm : LDY #imm : JSL thread_spawn`
    # matches it, and it is in none of the three tables above, so it comes from
    # `$81:80E7` or `$81:81D7` -- the two spawners that read a body's address out
    # of WRAM. It executes four times over the eleven profiles, which is how
    # often the creature is placed.
    0x81C1FB,
))

# Nothing in the ROM calls a thread body, so none of them could be a registry
# entry and all of them were blocked. They stay in the denominator because the
# work is real.
#
# Since the scheduler's round a body can be ported all the same: a registry
# entry may be any instruction the ROM reaches and may leave by a jump
# (`CosimRoutine::exits`), so a body is registered as stretches, each from
# where the scheduler resumes it or one of its calls returns to its next yield
# or call. These are those entries. They are not blocked, and the residue
# files them with the bodies they belong to rather than with the callables.
BODY_STRETCHES = frozenset((
    0x8181F6, 0x818206, 0x818263, 0x81828F,  # $81:81F6, the victims
    0x80C911, 0x80C918, 0x80C967, 0x80C971,  # $80:C8F6, the objects
    0x818113, 0x81814B, 0x81817C,            # $81:80EC, the actor list
    0x82D881, 0x82D87A,                      # $82:D7CF, animated tiles
    # $80:CDF4, the player's frame. The movement handler it calls through $2A,
    # $80:E4BA, is stretches too, but it is reached by a call's RTS and is
    # filed with the callables.
    0x80CDF7, 0x80CE04, 0x80CE0C, 0x80CE19, 0x80CE23,
))

BLOCKED.update({a: 'a thread body -- resumed by RTL from a parked frame, never called'
                for a in THREAD_BODIES
                if a not in BLOCKED and a not in BODY_STRETCHES})

def rom_to_snes(off):
    return ((0x80 + off // BANK_SIZE) << 16) | (0x8000 + off % BANK_SIZE)


def snes_to_rom(addr):
    bank, lo = addr >> 16, addr & 0xFFFF
    if lo < 0x8000:
        return None
    return (bank & 0x7F) * BANK_SIZE + (lo - 0x8000)


def load_ported(path='src/cosim/routines.c'):
    """The registry's entry points -- the addresses the harness intercepts.

    Returns `(names, verify_only)`, and the second one matters more than it
    looks. **A `verify_only` routine is written, checked on every call, and
    never substituted** -- `CosimRoutine::verify_only` has the argument for each
    of the three. So it counts towards how much of the game has been *written*
    and not at all towards how much of a run executes as C, and on some movies
    that is not a rounding difference: while it was `verify_only`, `$80:CCC8
    apu_send` alone was 23.8% of every instruction level 1 executes.

    Conflating the two is what this function used to do, and it made this tool
    disagree with the running game by a factor of two and a half for a reason
    that was entirely bookkeeping. Both numbers are now reported, and the second
    is the one to compare against what `zamn.exe --verbose` prints when you
    quit it.

    There is now a third set, and it is the mirror of the second. A `run_only`
    routine is **substituted and never checked per call**, because `verify`
    structurally cannot score it -- an interrupt lands inside the call window.
    It therefore counts towards both numbers, and the report names it anyway:
    the substituted figure is real, and what stands behind it is `run`'s
    per-pass comparison rather than a per-call diff. Saying which is which is
    the whole reason this function reads flags instead of just addresses.
    """
    src = open(path, encoding='utf-8', errors='replace').read()
    names, verify_only, run_only = {}, set(), set()
    # A record runs from its `.name` to the next one's, which is what makes a
    # flag anywhere inside it attributable to the right routine. Splitting is
    # more honest here than a single regex with `.*?`: the fields are in no
    # guaranteed order, and `.verify_only` sits at the end of a long record.
    for chunk in re.split(r'(?=\.name\s*=\s*")', src):
        m = re.search(r'\.name\s*=\s*"([^"]+)"', chunk)
        e = re.search(r'\.entry\s*=\s*0x([0-9a-fA-F]+)', chunk)
        if not (m and e):
            continue
        addr = int(e.group(1), 16)
        names[addr] = m.group(1)
        if re.search(r'\.verify_only\s*=\s*true', chunk):
            verify_only.add(addr)
        if re.search(r'\.run_only\s*=\s*true', chunk):
            run_only.add(addr)
    return names, verify_only, run_only


def load_symbols(path='tools/symbols/zamn.sym'):
    names = {}
    if not os.path.exists(path):
        return names
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.split('#')[0].strip()
        m = re.match(r'^\$([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+(\S+)', line)
        if m:
            names[(int(m.group(1), 16) << 16) | int(m.group(2), 16)] = m.group(3)
    return names


def load_profile(path):
    with open(path, 'rb') as f:
        blob = f.read()
    if blob[:4] != b'ZPRF':
        raise SystemExit('%s: not a ZPRF profile' % path)
    ver, size = struct.unpack('<II', blob[4:12])
    if ver != 1:
        raise SystemExit('%s: unsupported version %d' % (path, ver))
    both = np.frombuffer(blob, dtype='<u4', count=2 * size, offset=12)
    return both[:size].astype(np.int64), both[size:].astype(np.int64)


def load_cdl(path):
    with open(path, 'rb') as f:
        blob = f.read()
    if blob[:4] != b'ZCDL':
        raise SystemExit('%s: not a ZCDL log' % path)
    _, size = struct.unpack('<II', blob[4:12])
    return np.frombuffer(blob, dtype=np.uint8, count=size, offset=12)


def load_extra_entries(path):
    """Addresses that are routines but never `JSR`/`JSL` targets.

    **Thread bodies are invisible to a call-graph profiler**, and on this game
    that is most of the cast. The scheduler enters an actor's behaviour through
    an indirect jump out of `thread_call_handler`, so the CDL never marks it
    `CDL_SUB`, no edge in `callgraph.csv` names it, and every instruction it
    executes is credited to whatever subroutine happens to sit above it in the
    bank. Ten actor behaviours and six victim behaviours -- the whole placed
    population of the fourteen password levels -- scored exactly zero before
    this existed, which is the signature of the bug rather than of idle code.

    Feed it the behaviour column of `zamn_assets actors`, one `$BB:AAAA` per
    line; `#` comments and anything else are ignored.
    """
    out = set()
    for line in open(path, encoding='utf-8', errors='replace'):
        for m in re.finditer(r'\$([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})',
                             line.split('#')[0]):
            addr = (int(m.group(1), 16) << 16) | int(m.group(2), 16)
            off = snes_to_rom(addr)
            if off is not None:
                out.add(off)
    return out


# What kind of thing a blocked row is, for the residue's summary by family:
# the first words of its `BLOCKED` entry, which say it already.
def family_of(why, addr=None):
    if addr in BODY_STRETCHES:
        return 'thread bodies -- resumed by RTL, never called'
    if addr in PORTED_JOBS:
        return 'vblank jobs -- reached by RTL from a queue'
    if why is None:
        return 'callable -- an ordinary per-call port'
    if why.startswith('a thread body'):
        return 'thread bodies -- resumed by RTL, never called'
    if why.startswith('a vblank job'):
        return 'vblank jobs -- reached by RTL from a queue'
    return 'the frame -- NMI, reset, scheduler, dispatchers'


def residue_report(label, exec_by_routine, total_exec, total_call, idx,
                   entries, ported_offsets, verify_only, top):
    # The waits, charged back to the routine each sits in, as the share report
    # does below: a spin cannot buy its way up a list of what to port.
    wait_rows = []
    wait_by_routine = collections.Counter()
    for addr, span, what in load_wait_sites():
        off = snes_to_rom(addr)
        if off is None:
            continue
        n = int(total_exec[off:off + span].sum())
        if n:
            wait_rows.append((n, addr, what))
        for b in range(off, off + span):
            if total_exec[b] and idx[b] >= 0:
                wait_by_routine[int(entries[idx[b]])] += int(total_exec[b])
    tot_exec = int(total_exec.sum())
    wait_total = sum(n for n, _, _ in wait_rows)
    work = tot_exec - wait_total
    pct = lambda a, b: 100.0 * a / max(b, 1)
    print('\n%s' % ('=' * 66))
    print('RESIDUE -- what the 65816 executed with the port substituted')
    print('=' * 66)
    print('  %14s instructions, of which %s (%.1f%%) are the %d wait loops'
          % ('{:,}'.format(tot_exec), '{:,}'.format(wait_total),
             pct(wait_total, tot_exec), len(load_wait_sites())))
    print('  %14s instructions of work left, ranked below. The share of the'
          % '{:,}'.format(work))
    print('  whole this is, the game prints at exit; this cannot know it,'
          ' because\n  what the port ran never reached the counters.')

    rows = [(v - wait_by_routine.get(e, 0), e) for e, v in exec_by_routine.items()]
    rows = [r for r in rows if r[0] > 0]
    rows.sort(reverse=True)

    fam = collections.Counter()
    fam_rows = collections.Counter()
    for v, e in rows:
        f = family_of(BLOCKED.get(rom_to_snes(e)), rom_to_snes(e))
        fam[f] += v
        fam_rows[f] += 1
    print('\n  by family:')
    for f, v in fam.most_common():
        print('    %5.1f%%  %-50s %4d routine%s'
              % (pct(v, work), f, fam_rows[f], '' if fam_rows[f] == 1 else 's'))

    print('\n  %-34s %12s %6s %8s %8s' % ('routine', 'work', 'share', 'cumul.', 'calls'))
    run = 0
    for v, e in rows[:top]:
        run += v
        a = rom_to_snes(e)
        why = BLOCKED.get(a)
        mark = '!' if why else ('v' if a in verify_only else
                                ('p' if e in ported_offsets else ' '))
        print(' %s%-34s %12s %5.1f%% %7.1f%% %8s'
              % (mark, label(e), '{:,}'.format(v), pct(v, work), pct(run, work),
                 '{:,}'.format(int(total_call[e]))))
    # A substituted routine hands back at its own return instruction, so a
    # registered row that the port served costs about one instruction a call
    # here. Much more than that is not the routine: it is code below it that
    # nothing calls and nothing declares, which is how $80:B947 and $80:E4BA
    # were found. Said out loud, because otherwise it reads as a guard problem.
    suspect = [(v, e) for v, e in rows
               if e in ported_offsets and rom_to_snes(e) not in verify_only
               and v > 4 * max(int(total_call[e]), 1)
               and v > 0.001 * work]
    for v, e in suspect:
        print('\n  %s is registered and was charged %s instructions over %s'
              ' calls.\n  Either a guard declined it, which the census the game'
              ' prints at exit\n  names, or a routine start below it is not'
              ' declared anywhere: check\n  with tools/hotbytes.py, and add'
              ' one to JUMP_ENTRIES, VBL_JOBS or\n  THREAD_BODIES.'
              % (label(e), '{:,}'.format(v), '{:,}'.format(int(total_call[e]))))

    print('\n  "cumul." is the share of the residue taken if every row down to'
          ' that one\n  were ported. ! cannot pass through per-call substitution'
          ' (see BLOCKED);\n  v is written and verify_only; p is registered and'
          ' ran anyway, which\n  is a guard declining -- `--verbose` at exit'
          ' prints the census of why.')


def main(dirs, extra=None, residue=False, top=40):
    ported, verify_only, run_only = load_ported()
    symbols = load_symbols()
    extra_entries = load_extra_entries(extra) if extra else set()

    total_exec = None
    total_call = None
    flags = None
    per_movie = []

    for d in dirs:
        prof = os.path.join(d, 'profile.bin')
        cdl = os.path.join(d, 'zamn.cdl')
        if not (os.path.exists(prof) and os.path.exists(cdl)):
            continue
        execs, calls = load_profile(prof)
        f = load_cdl(cdl)
        if total_exec is None:
            total_exec, total_call, flags = execs.copy(), calls.copy(), f.copy()
        else:
            total_exec += execs
            total_call += calls
            flags |= f
        per_movie.append((os.path.basename(d), int(execs.sum())))

    if total_exec is None:
        raise SystemExit('no profile.bin found in: %s' % ', '.join(dirs))

    # A residue profile knows fewer routine starts than a trace does, and not
    # by accident: a substituted routine's own callees are never called, so
    # nothing marks them, and everything after a ported entry is filed under
    # it down to the next start the session happened to see. The first run of
    # this charged 210,300 instructions to `terrain_out_of_bounds`, which had
    # executed none -- the port served all 4,162 of its calls. So the traced
    # corpus lends its starts. Only its boundaries: nothing it counted is added.
    corpus_entries = 0
    if residue:
        for d in sorted(glob.glob(os.path.join('analysis', 'prof', '*'))):
            cdl = os.path.join(d, 'zamn.cdl')
            if os.path.exists(cdl):
                f = load_cdl(cdl)
                if len(f) == len(flags):
                    flags = flags | (f & CDL_SUB)
                    corpus_entries += 1

    size = len(total_exec)
    ported_offsets = {off for a in ported
                      if (off := snes_to_rom(a)) is not None}
    # ...and the subset that a substituted build actually runs. See load_ported.
    run_offsets = {off for a in ported if a not in verify_only
                   if (off := snes_to_rom(a)) is not None}

    # Boundaries: every JSR/JSL target the corpus actually reached, plus the
    # registry's entries in case one was only ever jumped to, plus the ones
    # above that nothing calls at all.
    jump_offsets = {off for a in set(JUMP_ENTRIES) | VBL_JOBS | THREAD_BODIES
                    if (off := snes_to_rom(a)) is not None}
    entries = np.array(sorted(
        set(np.nonzero(flags & CDL_SUB)[0].tolist())
        | ported_offsets | extra_entries | jump_offsets), dtype=np.int64)

    # Nearest preceding entry, rejected when it lies in another bank -- the
    # first bytes of a bank can precede every entry in it.
    idx = np.searchsorted(entries, np.arange(size, dtype=np.int64), 'right') - 1
    owner = np.where(idx >= 0, entries[np.clip(idx, 0, None)], -1)
    same_bank = (owner >= 0) & (owner // BANK_SIZE == np.arange(size) // BANK_SIZE)
    idx = np.where(same_bank, idx, -1)

    is_code = (flags & CDL_CODE) != 0
    ok = idx >= 0
    exec_sums = np.bincount(idx[ok], weights=total_exec[ok],
                            minlength=len(entries)).astype(np.int64)
    code_sums = np.bincount(idx[ok & is_code],
                            minlength=len(entries)).astype(np.int64)

    unattributed_exec = int(total_exec[~ok].sum())
    exec_by_routine = {int(entries[i]): int(v)
                       for i, v in enumerate(exec_sums) if v}
    code_by_routine = {int(entries[i]): int(v)
                       for i, v in enumerate(code_sums) if v}

    # --- what substitution actually removes -------------------------------
    #
    # The registry's 47 entries are not the whole of what runs natively. The
    # ROM splits work into subroutines the port inlines: `$80:BA51 sprite_emit`
    # is called 16,221 times from inside `sprite_build_oam`'s body, and under
    # `zamn_cosim run` it never executes at all, because the port served its
    # caller. Counting only registry entries therefore understates the share by
    # everything they subsume.
    #
    # So take the call graph and close over it: a routine is subsumed when it
    # has callers and **every** one of them is already ported or subsumed. A
    # routine reached from both sides -- some calls from ported code, some from
    # the ROM proper -- is not subsumed, and is reported separately as the band
    # the answer lies in.
    def owner_of(off):
        k = int(np.searchsorted(entries, off, 'right')) - 1
        if k < 0:
            return None
        e = int(entries[k])
        return e if e // BANK_SIZE == off // BANK_SIZE else None

    callers = collections.defaultdict(collections.Counter)
    for d in dirs:
        path = os.path.join(d, 'callgraph.csv')
        if not os.path.exists(path):
            continue
        for line in open(path, encoding='utf-8', errors='replace'):
            m = re.match(r'\$([0-9A-F]{2}):([0-9A-F]{4}),'
                         r'\$([0-9A-F]{2}):([0-9A-F]{4}),(\d+)', line.strip())
            if not m:
                continue
            src = snes_to_rom((int(m.group(1), 16) << 16) | int(m.group(2), 16))
            dst = snes_to_rom((int(m.group(3), 16) << 16) | int(m.group(4), 16))
            if src is None or dst is None:
                continue
            o = owner_of(src)
            if o is not None:
                callers[dst][o] += int(m.group(5))

    # A routine reached from both sides is neither wholly native nor wholly
    # not: it runs for the calls the ROM still makes and not for the ones the
    # port serves. So carry a fraction rather than a flag, weight it by how
    # many calls came from where, and iterate until it settles -- registry
    # entries pinned at 1, anything with no caller at all (`nmi_entry`, reset)
    # at 0. A routine every one of whose callers is native reaches 1 on its
    # own, which is the all-or-nothing closure as a special case.
    def close_over(seeds):
        frac = {r: (1.0 if r in seeds else 0.0) for r in exec_by_routine}
        for _ in range(200):
            delta = 0.0
            for r in exec_by_routine:
                if r in seeds:
                    continue
                cs = callers.get(r)
                if not cs:
                    continue
                tot = sum(cs.values())
                v = sum(n * frac.get(c, 0.0) for c, n in cs.items()) / tot
                delta = max(delta, abs(v - frac[r]))
                frac[r] = v
            if delta < 1e-9:
                break
        return frac

    frac = close_over(ported_offsets)

    native = {r for r in exec_by_routine if frac[r] > 0.999}
    mixed = {r for r in exec_by_routine if 0.001 < frac[r] <= 0.999}
    subsumed = native - ported_offsets

    tot_exec = int(total_exec.sum())
    tot_code = int(is_code.sum())
    nat_exec = sum(v for e, v in exec_by_routine.items() if e in native)
    nat_code = sum(v for e, v in code_by_routine.items() if e in native)
    direct_exec = sum(v for e, v in exec_by_routine.items() if e in ported_offsets)
    mixed_exec = sum(exec_by_routine.get(e, 0) for e in mixed)
    # The point estimate: every routine credited by its native call fraction.
    weighted_exec = sum(v * frac[e] for e, v in exec_by_routine.items())

    # ...and the same closure again over the routines that are actually
    # substituted, which is a smaller set and a different question. Everything
    # above answers "how much of this game have we written"; this answers "how
    # much of a run executes as C", which is what the game itself reports and
    # what Phase 4 has to get to 100%. They are not close on every movie: a
    # `verify_only` routine is fully written and never substituted, and the
    # subsumption closure follows -- a routine whose only caller is `apu_send`
    # counts as written and still runs on the 65816.
    run_frac = close_over(run_offsets)
    run_native = {r for r in exec_by_routine if run_frac[r] > 0.999}
    run_exec = sum(v for e, v in exec_by_routine.items() if e in run_native)
    run_weighted = sum(v * run_frac[e] for e, v in exec_by_routine.items())
    vo_exec = sum(v for e, v in exec_by_routine.items()
                  if e in native and e not in run_native)

    def label(off):
        a = rom_to_snes(off)
        s = '$%02X:%04X' % (a >> 16, a & 0xFFFF)
        nm = ported.get(a) or symbols.get(a)
        return '%s %s' % (s, nm) if nm else s

    if residue:
        print('Profiled %d session director%s, %s instructions total;'
              ' routine starts from them\nand from %d traced profile%s under'
              ' analysis/prof'
              % (len(per_movie), 'y' if len(per_movie) == 1 else 'ies',
                 '{:,}'.format(int(total_exec.sum())), corpus_entries,
                 '' if corpus_entries == 1 else 's'))
        residue_report(label, exec_by_routine, total_exec, total_call, idx,
                       entries, ported_offsets, verify_only, top)
        return

    print('Traced %d movie(s), %s instructions total\n'
          % (len(per_movie), '{:,}'.format(tot_exec)))
    for name, n in per_movie:
        print('  %-22s %14s' % (name, '{:,}'.format(n)))

    print('\n%s' % ('=' * 66))
    print('NATIVE SHARE')
    print('=' * 66)
    pct = lambda a, b: 100.0 * a / max(b, 1)
    print('  static  (distinct code bytes executed) %9s / %-9s %5.1f%%'
          % ('{:,}'.format(nat_code), '{:,}'.format(tot_code),
             pct(nat_code, tot_code)))
    print('  dynamic (instructions executed)        %9s / %-9s %5.1f%%'
          % ('{:,}'.format(nat_exec), '{:,}'.format(tot_exec),
             pct(nat_exec, tot_exec)))
    print('\n  of the dynamic share:')
    print('    %5.1f%%  in the %d registry entries themselves'
          % (pct(direct_exec, tot_exec), len(ported_offsets)))
    print('    %5.1f%%  in %d routines they subsume -- every caller is ported'
          % (pct(nat_exec - direct_exec, tot_exec), len(subsumed)))
    print('    %5.1f%%  in %d routines called from both sides, credited by'
          ' call share' % (pct(mixed_exec, tot_exec), len(mixed)))
    print('\n  best estimate, every routine weighted by its native call'
          ' fraction: %5.1f%%' % pct(weighted_exec, tot_exec))

    # Only the ones a substituted build still executes. A `verify_only` routine
    # whose every caller is itself substituted never runs at all, so it costs
    # the substituted share nothing -- `vo_exec` has always been right about
    # that, and the list under it used to name them anyway. The two LZSS leaves
    # are what made the difference visible: they are 2.1% of the corpus, they
    # are `verify_only` and always will be, and the round that registered
    # `$80:CD20` above them took every one of their 1,071,108 calls out of a
    # substituted run without changing a line about either.
    vo_still_run = [a for a in sorted(verify_only)
                    if (off := snes_to_rom(a)) is not None
                    and off not in run_native]
    if vo_exec:
        print('\n  ...of which %5.1f%% is in %d verify_only routine(s) and what'
              ' they subsume:' % (pct(vo_exec, tot_exec), len(vo_still_run)))
        for a in vo_still_run:
            off = snes_to_rom(a)
            print('    %-34s %12s' % (label(off),
                                      '{:,}'.format(exec_by_routine.get(off, 0))))
        subsumed_vo = len(verify_only) - len(vo_still_run)
        if subsumed_vo:
            print('    (%d more written and never substituted, and never'
                  ' reached either:\n     every caller of each is itself'
                  ' substituted, so they cost this nothing)' % subsumed_vo)
        print('  written and checked on every call, and never substituted --'
              ' see\n  CosimRoutine::verify_only. So a build that runs the port'
              ' executes\n  these on the 65816, and the share it reaches is'
              ' lower than the one\n  above by exactly this much:')
        print('\n  dynamic share actually substituted:          %5.1f%%'
              % pct(run_exec, tot_exec))

    # ...and the mirror, which has to be said out loud for the same reason: the
    # substituted figure above *includes* these, and what stands behind them is
    # not a per-call diff.
    ro_exec = sum(v for e, v in exec_by_routine.items()
                  if rom_to_snes(e) in run_only)
    if ro_exec:
        print('\n  ...and %5.1f%% of the substituted figure is in %d run_only'
              ' routine(s):' % (pct(ro_exec, tot_exec), len(run_only)))
        for a in sorted(run_only):
            off = snes_to_rom(a)
            if off is None:
                continue
            print('    %-34s %12s' % (label(off),
                                      '{:,}'.format(exec_by_routine.get(off, 0))))
        print('  substituted on every call and never checked on one, because'
              ' an\n  interrupt lands inside the call window and `verify`'
              ' abandons it.\n  What stands behind them is `run`\'s comparison'
              ' of all 128 KB once\n  per scheduler pass -- a claim about a'
              ' stretch rather than a call.\n  See CosimRoutine::run_only.')

    # --- and the part of the denominator that is not work at all -----------
    wait_rows = []
    # ...and which routine each wait belongs to, by the same attribution the
    # rest of the report uses, so the ranking below can charge it back.
    wait_by_routine = collections.Counter()
    for addr, span, what in load_wait_sites():
        off = snes_to_rom(addr)
        if off is None:
            continue
        n = int(total_exec[off:off + span].sum())
        if n:
            wait_rows.append((n, addr, what))
        for b in range(off, off + span):
            if total_exec[b] and idx[b] >= 0:
                wait_by_routine[int(entries[idx[b]])] += int(total_exec[b])
    wait_total = sum(n for n, _, _ in wait_rows)
    # A wait can sit inside a routine counted native, and since `$80:CCCC` it
    # does: `apu_send` is written, so its spin was in every numerator below
    # while the same instructions were taken out of the denominator, and the
    # "written" lines read ten points high over the 56-record sweep. Out of
    # both, then. The substituted line never had the problem -- `apu_send` is
    # verify_only and was never in `run_native` -- but it gets the same rule.
    nat_wait = sum(n for e, n in wait_by_routine.items() if e in native)
    weighted_wait = sum(n * frac.get(e, 0.0) for e, n in wait_by_routine.items())
    run_wait = sum(n for e, n in wait_by_routine.items() if e in run_native)
    if wait_rows:
        print('\n  of the %s instructions above, this many are a CPU waiting'
              ' rather than working:' % '{:,}'.format(tot_exec))
        for n, addr, what in sorted(wait_rows, reverse=True):
            print('    %5.2f%%  $%02X:%04X  %s'
                  % (pct(n, tot_exec), addr >> 16, addr & 0xFFFF, what))
        print('    ------')
        print('    %5.2f%%  total. Porting any of it would replace a spin with'
              ' a spin, so the' % pct(wait_total, tot_exec))
        print('             honest denominator is the one with it removed:')
        print('\n  dynamic share, waits out of the denominator: %5.1f%%'
              % pct(nat_exec - nat_wait, tot_exec - wait_total))
        print('  best estimate, likewise:                     %5.1f%%'
              % pct(weighted_exec - weighted_wait, tot_exec - wait_total))
        # The one number that has a counterpart measured a completely different
        # way. `zamn.exe` and `zamn_cosim run` report the same quantity live,
        # from the substitution seam and in SNES cycles rather than from a
        # profile and in instructions -- so the two are independent all the way
        # down, and agreeing is worth something. They will not agree to the
        # decimal: a cycle is not an instruction, and the live numerator is a
        # per-routine mean budget where this is a per-instruction count.
        print('  ...substituted only, likewise:               %5.1f%%   <- what'
              ' the game reports' % pct(run_exec - run_wait, tot_exec - wait_total))
    print('\n  %d routine entries used for attribution; %s instructions (%.1f%%)'
          % (len(entries), '{:,}'.format(unattributed_exec),
             pct(unattributed_exec, tot_exec)))
    print('  fell before the first entry in their bank and are counted as'
          ' unported.')

    print('\n%s' % ('=' * 66))
    print('TOP UNNATIVE ROUTINES BY WORK (registry entries + subsumed)')
    print('=' * 66)
    # Work, not instructions: a routine is charged only for what it does, so a
    # spin loop cannot buy its way to the top of the list of what to port next.
    tot_work = tot_exec - wait_total
    rows = [(v - wait_by_routine.get(e, 0), e)
            for e, v in exec_by_routine.items() if e not in native]
    rows = [r for r in rows if r[0] > 0]
    rows.sort(reverse=True)
    run = 0
    blocked_work = 0
    seen_blocked = []
    print('  %-34s %12s %6s %8s %8s'
          % ('routine', 'work', 'share', 'cumul.', 'calls'))
    for v, e in rows[:30]:
        run += v
        addr = rom_to_snes(e)
        why = BLOCKED.get(addr)
        if why:
            blocked_work += v
            seen_blocked.append((addr, why))
        waited = wait_by_routine.get(e, 0)
        note = '  (+%s waiting)' % '{:,}'.format(waited) if waited else ''
        print(' %s%-34s %12s %5.1f%% %7.1f%% %8s%s'
              % ('!' if why else ' ', label(e), '{:,}'.format(v),
                 100.0 * v / tot_work,
                 100.0 * (nat_exec - nat_wait + run) / tot_work,
                 '{:,}'.format(int(total_call[e])), note))
    print('\n  "cumul." is the native share this run would reach if every routine'
          '\n  down to that row were ported and nothing else changed.')
    if seen_blocked:
        print('\n  ! -- %s of the work above, %.1f%% of everything the game does,'
              % ('{:,}'.format(blocked_work), 100.0 * blocked_work / tot_work))
        print('  is in routines the harness structurally cannot take -- which is'
              '\n  not the same as cannot be written. What none of them can do is'
              '\n  pass through per-call substitution, because nothing calls them:'
              '\n  a dispatcher reaches a vblank job by RTL and the scheduler'
              '\n  reaches a thread body the same way, so there is no call to'
              '\n  intercept. They are the frame rather than something called'
              '\n  inside one, and they stay in the denominator because the work'
              '\n  is real:\n')
        for addr, why in seen_blocked:
            print('    $%02X:%04X  %s' % (addr >> 16, addr & 0xFFFF, why))

    print('\n%s' % ('=' * 66))
    print('NATIVE ROUTINES BY INSTRUCTIONS EXECUTED (registry entries + subsumed)')
    print('=' * 66)
    rows = [(v, e) for e, v in exec_by_routine.items() if e in native]
    rows.sort(reverse=True)
    for v, e in rows[:15]:
        print('  %-34s %12s %5.1f%%'
              % (label(e), '{:,}'.format(v), 100.0 * v / tot_exec))
    zero = [e for e in ported_offsets if exec_by_routine.get(e, 0) == 0]
    if zero:
        print('\n  %d ported routine(s) executed nothing in this run: %s'
              % (len(zero), ', '.join(sorted(label(e) for e in zero))))


if __name__ == '__main__':
    args = sys.argv[1:]
    extra = None
    if '--entries' in args:
        i = args.index('--entries')
        extra = args[i + 1]
        del args[i:i + 2]
    top = 40
    if '--top' in args:
        i = args.index('--top')
        top = int(args[i + 1])
        del args[i:i + 2]
    residue = '--residue' in args
    args = [a for a in args if a != '--residue']
    if not args:
        raise SystemExit(__doc__)
    main(args, extra, residue, top)
