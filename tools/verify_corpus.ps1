# Run `zamn_cosim` over the whole movie corpus and print one line each.
#
# The corpus was six movies when Phase 3 started and it is twenty-five now, so
# "all the movies verify clean" had become a claim nobody could reproduce
# without knowing how many frames each one wants. That number is not a property
# of the .zmv -- a movie's last input is not its last interesting frame, because
# the game goes on doing things after the player stops pressing buttons -- so it
# lives here, one row per movie, and this script is what PROGRESS.md's totals
# are measured with.
#
#   powershell -ExecutionPolicy Bypass -File tools\verify_corpus.ps1
#   powershell -ExecutionPolicy Bypass -File tools\verify_corpus.ps1 -Coverage
#   powershell -ExecutionPolicy Bypass -File tools\verify_corpus.ps1 -Lockstep
#
# The first two run `verify`, which checks every call's answer against the
# ROM's. `-Lockstep` runs `run` instead, which substitutes the port for real and
# compares all of WRAM once per scheduler pass -- a stronger claim over fewer
# calls, and about forty seconds a movie against one.
#
# Exits non-zero if any movie diverges.

param(
    [string]$Rom = "Zombies Ate My Neighbors.sfc",
    [switch]$Coverage,
    # Run the lockstep pass instead of the verify pass.
    [switch]$Lockstep,
    # What `-Lockstep` leaves to the ROM; see the block under the corpus.
    # `-Without none` excludes nothing: PowerShell's -File does not evaluate
    # `@()` on the command line, and "none" is the spelling `zamn_cosim -r`
    # already uses for an empty selection.
    [string[]]$Without = @("lzss_decompress", "camera_follow"),
    # A wildcard over the corpus, for when one movie is the question. The
    # totals below then say how many movies they are totals over, because a
    # figure from a subset that reads like a figure from the corpus is worse
    # than no figure.
    [string]$Only = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# movie -> frames. The nine level-N probes all run 4700: their gameplay tail
# ends at 4660 and the level keeps running for a moment after it, and the
# two-player level 25 shares their shape. `level25-boss.zmv` is the exception
# that proves the column is needed: it walks a route before it starts fighting,
# so its interesting frames run to 7600.
$corpus = [ordered]@{
    "boot.zmv"               = 2400
    "level1.zmv"             = 2400
    "level1-pickups.zmv"     = 2400
    "level1-map.zmv"         = 2700
    "level1-keys.zmv"        = 4050
    "level1-rescue.zmv"      = 6100
    "level1-2p.zmv"          = 6000
    "level1-2p-rescue.zmv"   = 6100
    "level5.zmv"             = 4700
    "level5-d7f6.zmv"        = 3500
    "level9.zmv"             = 4700
    "level9-weapons.zmv"     = 9000
    "level13.zmv"            = 4700
    "level17.zmv"            = 4700
    "level17-weapon.zmv"     = 4700
    "level17-2p-freeze.zmv"  = 4700
    "level21.zmv"            = 4700
    "level21-spin.zmv"       = 4700
    "level21-bubble.zmv"     = 6700
    "level21-p2-bubble.zmv"  = 6700
    "level21-rescue.zmv"     = 3000
    "level21-p2-rescue.zmv"  = 3000
    "level25.zmv"            = 4700
    "level25-2p.zmv"         = 4700
    "level25-boss.zmv"       = 7600
    "level25-heavy.zmv"      = 6700
    "level25-lane.zmv"       = 9400
    "level29-fighting.zmv"   = 4400
    "level29-firstaid.zmv"   = 4700
    "level29-ice.zmv"        = 6000
    "level29-item.zmv"       = 5300
    "level33.zmv"            = 3600
    "level37.zmv"            = 4700
    "level37-e6e4.zmv"       = 6000
    "level41.zmv"            = 4700
    "level45-bonus.zmv"      = 3600
    "level45-carried.zmv"    = 4700
    "level45-contested.zmv"  = 4000
    "level45-race.zmv"       = 3800
    "level49.zmv"            = 4700
    "level49-bubble.zmv"     = 5700
    "level49-corner.zmv"     = 5700
    "level53.zmv"            = 3700
    "level53-bonus.zmv"      = 3500
}

$movies = @($corpus.Keys | Where-Object { $Only -eq "" -or $_ -like $Only })
if ($movies.Count -eq 0) {
    Write-Error "no movie in the corpus matches '$Only'"
    exit 2
}
# Said once, up front, rather than by qualifying every total below it. Two of
# those totals -- the branch-coverage union and the decline census -- are
# corpus-wide claims by name, and a subset run printing them unannounced would
# read as one.
if ($Only -ne "") {
    "Only movies matching '$Only': $($movies.Count) of $($corpus.Count). Every total below is over those."
    ""
}

# The lockstep pass.
#
# `verify` asks each call whether the port's answer matches the ROM's. `run`
# substitutes the port for real and compares all 128 KB of WRAM once per
# scheduler pass, which is the stronger claim about the same code.
#
# It stops comparing the moment the two timelines part, and left to itself that
# happens on 42 of these 43 movies before game frame 210 -- always inside the
# first level load, and so early that the pass was measuring the boot rather
# than the game. Two routines are why. `lzss_decompress` and `camera_follow`
# each spend that load in a wait that quantises in whole frames, so which pass
# they come back on is decided by a frame boundary and not by their cost
# models: taking 200,000 cycles off `lzss_decompress` moves the drift at the
# parting by two. Leaving those two to the ROM gives up nothing this pass was
# measuring -- `verify` still checks every call of both, and `run` still
# substitutes everything else -- and buys four and a half times as many
# compared passes. `-Without none` runs the plain configuration, which is what
# that is four and a half times more than. See "What actually ends a lockstep
# run" in docs/cosim.md.
#
# The three drift columns are the *steady state*, which on a movie that parts
# means the parting pass is left out: that pass is a level load worth 400,000
# cycles, and averaging it in would say nothing about the thousand ordinary
# passes either side of it.
if ($Lockstep) {
    $Without = @($Without | Where-Object { $_ -and $_ -ne "none" })
    $flags = @()
    foreach ($w in $Without) { $flags += @("-x", $w) }
    if ($Without.Count -gt 0) {
        "Lockstep over the corpus, leaving $($Without -join ' and ') to the ROM."
    } else {
        "Lockstep over the corpus, with every ported routine substituted."
    }
    ""
    $row = "{0,-24} {1,6} {2,8} {3,7} {4,9} {5,8} {6,7}  {7}"
    $row -f "movie", "frames", "passes", "parted", "drift", "worst", "mean", "state"
    $partedAll = @()
    $passTotal = 0
    $dirty = 0
    foreach ($movie in $movies) {
        $frames = $corpus[$movie]
        $runArgs = @("run", $Rom, "-m", "movies\$movie", "-f", "$frames") + $flags
        $text = (& "build\zamn_cosim.exe" @runArgs 2>&1 | Out-String)
        $code = $LASTEXITCODE
        $passes = 0
        $parted = "-"
        $drift = ""
        $worst = ""
        $mean = ""
        # Three shapes, and the em dashes in all of them are matched with `.` --
        # the console hands them back in whatever code page it feels like.
        $never = "never parted\.\s+Over (\d+) passes the clocks reached\s+([-+]?\d+) cycles" +
                 " apart, no single pass moved them by more than (\d+)\s+\(pass \d+\)," +
                 " and the mean pass moved them (\d+\.\d+)"
        $steady = "(?s)part at pass (\d+):.+?Without that pass.+?over the (\d+) the two" +
                  " timelines shared.+?clocks reached ([-+]?\d+) apart, no single pass moved" +
                  " them by more\s+than (\d+) \(pass \d+\), and the mean pass moved them (\d+\.\d+)"
        if ($text -match $never) {
            $passes = [int]$Matches[1]
            $drift = $Matches[2]
            $worst = $Matches[3]
            $mean = $Matches[4]
        } elseif ($text -match $steady) {
            $parted = $Matches[1]
            $passes = [int]$Matches[2]
            $drift = $Matches[3]
            $worst = $Matches[4]
            $mean = $Matches[5]
            $partedAll += $movie
        } elseif ($text -match "part at pass (\d+):") {
            # Parted with nothing shared before it, so there is no steady state
            # to report -- the columns stay empty rather than reading as zero.
            $parted = $Matches[1]
            $partedAll += $movie
        }
        # The verdict this pass exists for. "clean" is the whole of it: every
        # byte that differed was inside a stack or a declared scratch byte.
        $state = "clean"
        if ($text -match "Up to (\d+) byte") {
            $state = "$($Matches[1]) UNEXPLAINED"
            $dirty++
        } elseif (-not ($text -match "No byte of live game state ever differed")) {
            $state = "NOTHING COMPARED"
            $dirty++
        } elseif ($code -ne 0) {
            # Said clean and exited non-zero: believe the exit code.
            $state = "EXIT $code"
            $dirty++
        }
        $passTotal += $passes
        $row -f $movie, $frames, $passes, $parted, $drift, $worst, $mean, $state
    }
    ""
    "$passTotal scheduler passes compared across $($movies.Count) movie$(if ($movies.Count -ne 1) { 's' })."
    "$($movies.Count - $partedAll.Count) of $($movies.Count) never parted; " +
        "$dirty had a live byte differ."
    if ($partedAll.Count -gt 0) {
        "Parted: $($partedAll -join ' ')"
    }
    if ($dirty -gt 0) { exit 1 }
    exit 0
}

$failed = 0
$total = 0
$siteTotal = 0
# The intersection of every movie's untaken list, which is the only number that
# says what the *corpus* has never done. Per-movie coverage cannot be added up.
$untakenEverywhere = $null
$censusAll = @{}
# Cost models, summed over the corpus: how many calls priced themselves and how
# many of those were refresh-exact. A model is only as good as the movie that
# has not caught it out yet, so the number worth quoting is this one and not any
# single run's. See `cosim_cost`.
$pricedAll = @{}
$exactAll = @{}
$hdmaAll = @{}
"{0,-24} {1,6} {2,10} {3,7} {4,6}  {5}" -f "movie", "frames", "checked", "decl.", "sites", "census"
foreach ($movie in $movies) {
    $frames = $corpus[$movie]
    $args = @("verify", $Rom, "-m", "movies\$movie", "-f", "$frames")
    if ($Coverage) { $args += "-c" }
    $out = & "build\zamn_cosim.exe" @args 2>&1
    $ok = $LASTEXITCODE -eq 0
    $checked = 0
    $m = ($out | Select-String -Pattern "^(\d+) calls checked")
    if ($m) { $checked = [int]$m.Matches[0].Groups[1].Value }
    # One row of the per-routine table: name, calls, yields ("-" for a leaf),
    # checked, passed, int., decl., stack, then the cycle range.
    $decl = 0
    foreach ($line in ($out | Select-String -Pattern "^\S+ +\d+ +(-|\d+) +\d+ +\d+ +\d+ +\d+ +\d+ ")) {
        $f = ($line.ToString().Trim() -split "\s+")
        $decl += [int]$f[6]
    }
    $sites = ""
    $s = ($out | Select-String -Pattern "Branch coverage: (\d+) of (\d+)")
    if ($s) {
        $sites = "$($s.Matches[0].Groups[1].Value)/$($s.Matches[0].Groups[2].Value)"
        $siteTotal = [int]$s.Matches[0].Groups[2].Value
    }
    # The untaken list: indented rows of "routine  site  description" under the
    # "never reached" heading, and the site name is the second column.
    $untaken = @{}
    foreach ($line in ($out | Select-String -Pattern "^    \S+ +\S+ +\S")) {
        $f = ($line.ToString().Trim() -split "\s+")
        $untaken[$f[1]] = $true
    }
    if ($null -eq $untakenEverywhere) {
        $untakenEverywhere = $untaken
    } else {
        $keep = @{}
        foreach ($k in $untakenEverywhere.Keys) { if ($untaken.ContainsKey($k)) { $keep[$k] = $true } }
        $untakenEverywhere = $keep
    }
    $rows = @()
    foreach ($line in ($out | Select-String -Pattern "\`$[0-9A-F]{2}:[0-9A-F]{4}\s+\d+\s*$")) {
        $f = ($line.ToString().Trim() -split "\s+")
        $rows += ($f[-2] + "=" + $f[-1])
        $censusAll[$f[-2]] = [int]$censusAll[$f[-2]] + [int]$f[-1]
    }
    # Rows of the cost-model table: name, priced, checked, the error range, then
    # "<refresh-exact>/<priced with the PPU quiet>" and the HDMA count.
    foreach ($line in ($out | Select-String -Pattern "^  (\S+) +\d+ +\d+ +[-+0-9.]+\.\.[-+0-9.]+, mean [-+0-9.]+ +(\d+)/(\d+) +(\d+)")) {
        $g = $line.Matches[0].Groups
        $pricedAll[$g[1].Value] = [int]$pricedAll[$g[1].Value] + [int]$g[3].Value
        $exactAll[$g[1].Value] = [int]$exactAll[$g[1].Value] + [int]$g[2].Value
        $hdmaAll[$g[1].Value] = [int]$hdmaAll[$g[1].Value] + [int]$g[4].Value
    }
    $census = $rows -join " "
    $total += $checked
    if (-not $ok) { $failed++ }
    "{0,-24} {1,6} {2,10} {3,7} {4,6}  {5}" -f $movie, $frames, $checked,
        $decl, $sites, $(if ($ok) { $census } else { "DIVERGED" })
}
""
"$total calls checked across $($movies.Count) movie$(if ($movies.Count -ne 1) { 's' }), $failed diverged."
$never = $untakenEverywhere.Count
"Branch coverage, union over the corpus: $($siteTotal - $never) of $siteTotal taken, $never untaken by every input."
if ($never -gt 0) {
    ($untakenEverywhere.Keys | Sort-Object) -join " "
}
if ($censusAll.Count -gt 0) {
    ""
    "Declined to, summed over the corpus:"
    foreach ($k in ($censusAll.Keys | Sort-Object { -$censusAll[$_] })) {
        "  {0,-12} {1,6}" -f $k, $censusAll[$k]
    }
}
if ($pricedAll.Count -gt 0) {
    ""
    "Cost models over the corpus (refresh-exact / priced with the PPU quiet, then priced under HDMA):"
    $modelsWrong = 0
    foreach ($k in ($pricedAll.Keys | Sort-Object)) {
        $bad = $exactAll[$k] -ne $pricedAll[$k]
        if ($bad) { $modelsWrong++ }
        "  {0,-24} {1,10} / {2,-10} {3,8} HDMA{4}" -f $k, $exactAll[$k], $pricedAll[$k],
            $hdmaAll[$k], $(if ($bad) { "  <-- MODEL WRONG" } else { "" })
    }
    if ($modelsWrong -gt 0) { $failed++ }
}
if ($failed -gt 0) { exit 1 }
