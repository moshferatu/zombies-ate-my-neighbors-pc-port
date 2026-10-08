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
#   powershell -ExecutionPolicy Bypass -File tools\verify_corpus.ps1 -Picture
#
# The first two run `verify`, which checks every call's answer against the
# ROM's. `-Lockstep` runs `run` instead, which substitutes the port for real and
# compares all of WRAM once per scheduler pass -- a stronger claim over fewer
# calls, and about forty seconds a movie against one.
#
# `-Picture` is about the picture and not the game: it runs the game itself,
# `zamn.exe`, with no window, and has every scanline of every frame drawn
# twice, by `src/video` and by the emulated PPU, and compared. Then it runs
# each movie again with `src/video` drawing alone and checks that the picture
# comes to the same checksum. Once for each width in `-Widescreen`.
#
# Movies run `-Jobs` at a time, twelve unless told otherwise, and the rows come
# out in corpus order when all of them have finished. The whole lockstep pass
# takes about three and a half minutes.
#
# Exits non-zero if any movie diverges.

param(
    [string]$Rom = "Zombies Ate My Neighbors.sfc",
    [switch]$Coverage,
    # Run the lockstep pass instead of the verify pass.
    [switch]$Lockstep,
    # Run the picture pass instead, at each of these widths: off, 16:9, 16:10
    # or 21:9. `-Widescreen off,16:9,16:10,21:9` is all four; under -File a
    # list arrives as one string with the commas in it, and is split below.
    [switch]$Picture,
    [string[]]$Widescreen = @("off", "16:9"),
    # What `-Lockstep` leaves to the ROM; see the block under the corpus.
    # `-Without none` excludes nothing: PowerShell's -File does not evaluate
    # `@()` on the command line, and "none" is the spelling `zamn_cosim -r`
    # already uses for an empty selection.
    [string[]]$Without = @("lzss_decompress", "camera_follow", "camera_scroll"),
    # A wildcard over the corpus, for when one movie is the question. The
    # totals below then say how many movies they are totals over, because a
    # figure from a subset that reads like a figure from the corpus is worse
    # than no figure.
    [string]$Only = "",
    # How many movies run at once. Each is its own process and they share
    # nothing. Twelve keeps the machine usable while the corpus runs; one per
    # core does not.
    [int]$Jobs = 12
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
    # The demo that plays when the title is left alone starts near frame
    # 4350, past where `boot.zmv` stops, and a real button ends it at 6000.
    # Nothing else in the corpus runs the demo's playback job.
    # It runs on past 9000 for the title's menu and the players' screen the
    # second time they come up, when the data bank is `$00` and not `$80`,
    # and to 26000 for the two demos after the first, a graveyard and a
    # football field: nothing else in the corpus has their creatures, and
    # the footballers do not come on until about 21000.
    "demo-end.zmv"           = 26000
    "level1.zmv"             = 2400
    "level1-pickups.zmv"     = 2400
    "level1-map.zmv"         = 2700
    "level1-keys.zmv"        = 4050
    "level1-rescue.zmv"      = 6100
    "level1-2p.zmv"          = 6000
    "level1-2p-rescue.zmv"   = 6100
    # Three of the level probes run on to 20000, long past their last
    # input, which is how far the live profiles run all twelve. The players
    # die there and come back flashing, the game ends, and the title's demos
    # come round: `level5` has the potion's monster in one, `level41` a
    # player walking in slime, and `level13` gets through two games' ends.
    "level5.zmv"             = 20000
    "level5-d7f6.zmv"        = 3500
    "level9.zmv"             = 4700
    "level9-weapons.zmv"     = 9000
    "level13.zmv"            = 20000
    "level17.zmv"            = 4700
    "level17-weapon.zmv"     = 4700
    "level17-2p-freeze.zmv"  = 4700
    # Run on as the three above are. A demo that comes round after its
    # game puts the radar up, which no movie does in play.
    "level21.zmv"            = 20000
    "level21-spin.zmv"       = 4700
    "level21-bubble.zmv"     = 6700
    "level21-p2-bubble.zmv"  = 6700
    "level21-rescue.zmv"     = 3000
    "level21-p2-rescue.zmv"  = 3000
    "level25.zmv"            = 4700
    "level25-2p.zmv"         = 4700
    "level25-boss.zmv"       = 7600
    "level25-heavy.zmv"      = 6700
    "level25-item.zmv"       = 4600
    "level25-lane.zmv"       = 9400
    "level29-990b.zmv"       = 5200
    "level29-990b-2p.zmv"    = 4900
    "level29-990b-freeze.zmv" = 4600
    "level29-fighting.zmv"   = 4400
    "level29-firstaid.zmv"   = 4700
    "level29-ice.zmv"        = 6000
    "level29-item.zmv"       = 5300
    "level33.zmv"            = 3600
    "level37.zmv"            = 4700
    "level37-e6e4.zmv"       = 6000
    "level41.zmv"            = 20000
    "level45-bonus.zmv"      = 3600
    "level45-carried.zmv"    = 4700
    "level45-contested.zmv"  = 4000
    "level45-race.zmv"       = 3800
    "level49.zmv"            = 4700
    "level49-bubble.zmv"     = 5700
    "level49-corner.zmv"     = 5700
    "level53.zmv"            = 3700
    "level53-bonus.zmv"      = 3500
    # The two movies that leave a level, and they run long because most of
    # what they are for happens after the transition. Neither was in this
    # list before, which is why the corpus said 48 movies while `movies/`
    # held 50 -- and why `$82:F958`, put on the declined list with some
    # ceremony a round ago, had never been through the standing check.
    #
    # `level24-carry` finishes three levels and needs 10600: the last one
    # loads at 9833 and there is no point verifying a transition and then
    # stopping before the level it lands in has run.
    "level21-exit.zmv"       = 7000
    "level24-carry.zmv"      = 10600
    # Three passwords the screen does not take as a level and a count: the
    # one `$82:B018` tests by its letters, and two it turns down, one at
    # each of its tables. Each runs into the level the game starts instead.
    "password-bcdf.zmv"      = 3000
    "password-no-level.zmv"  = 3000
    "password-no-count.zmv"  = 3000
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

# Run `zamn_cosim <command>` on every movie, $Jobs at a time, and hand back
# each one's output and exit code. The rows are printed afterwards, in corpus
# order, so the report reads the same however the runs interleaved.
function Invoke-Corpus([string]$command, [string[]]$extra) {
    $exe = Join-Path $root "build\zamn_cosim.exe"
    # Start-Process joins its arguments with spaces and quotes nothing, and
    # the ROM's name has three in it.
    return Invoke-Movies $exe {
        param($m)
        @($command, "`"$Rom`"", "-m", "`"movies\$m`"", "-f", "$($corpus[$m])") + $extra
    }
}

# The same for any program: `$argvOf` is handed a movie and says what the
# program is run with.
function Invoke-Movies([string]$exe, [scriptblock]$argvOf) {
    $dir = Join-Path ([IO.Path]::GetTempPath()) ("zamn-corpus-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory $dir | Out-Null
    $pending = New-Object System.Collections.Queue
    foreach ($m in $movies) { $pending.Enqueue($m) }
    $live = @{}
    $done = @{}
    try {
        while ($pending.Count -gt 0 -or $live.Count -gt 0) {
            while ($pending.Count -gt 0 -and $live.Count -lt [Math]::Max(1, $Jobs)) {
                $m = $pending.Dequeue()
                $argv = & $argvOf $m
                $p = Start-Process -FilePath $exe -ArgumentList $argv -NoNewWindow -PassThru `
                    -RedirectStandardOutput (Join-Path $dir "$m.out") `
                    -RedirectStandardError (Join-Path $dir "$m.err")
                # Windows PowerShell only fills in ExitCode for a process
                # whose handle was taken while it was still running.
                $null = $p.Handle
                $live[$m] = $p
            }
            foreach ($m in @($live.Keys)) {
                $p = $live[$m]
                if (-not $p.HasExited) { continue }
                $p.WaitForExit()
                $lines = @(Get-Content (Join-Path $dir "$m.out")) + @(Get-Content (Join-Path $dir "$m.err"))
                $done[$m] = @{ Lines = $lines; Code = $p.ExitCode }
                $live.Remove($m)
            }
            if ($live.Count -gt 0) { Start-Sleep -Milliseconds 250 }
        }
    } finally {
        foreach ($p in $live.Values) { if (-not $p.HasExited) { $p.Kill() } }
        Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
    }
    return $done
}

# The picture pass.
#
# `--renderer check` draws each line both ways and counts the lines that came
# out different; it is the whole of the comparison, column by column. The
# second run is `--renderer native`, which is how the game is played, and
# proves one thing more: that with the PPU drawing nothing at all the picture
# is still the same one. Its checksum is over every line as `src/video` left
# it, and the check's is over every line as the PPU drew it.
if ($Picture) {
    $game = Join-Path $root "zamn.exe"
    $env:SDL_VIDEODRIVER = "dummy"
    $Widescreen = @($Widescreen | ForEach-Object { $_ -split "," } | Where-Object { $_ })
    $bad = 0
    $row = "{0,-24} {1,6} {2,9} {3,7} {4,7}  {5}"
    foreach ($w in $Widescreen) {
        $runs = @{}
        foreach ($renderer in @("check", "native")) {
            $runs[$renderer] = Invoke-Movies $game {
                param($m)
                @("`"$Rom`"", "-m", "`"movies\$m`"", "--frames", "$($corpus[$m])", "--no-config",
                  "--no-pads", "--no-high-scores", "--no-audio", "--widescreen", $w,
                  "--renderer", $renderer)
            }
        }
        "The picture over the corpus, widescreen $w."
        ""
        $row -f "movie", "frames", "lines", "left", "differ", "drawn alone"
        $lineTotal = 0
        $leftTotal = 0
        $differTotal = 0
        $failed = 0
        foreach ($movie in $movies) {
            $text = $runs["check"][$movie].Lines | Out-String
            $alone = $runs["native"][$movie].Lines | Out-String
            $lines = 0
            $left = 0
            $differ = -1
            if ($text -match "Drawing: check; (\d+) lines drawn here, (\d+) left to the PPU") {
                $lines = [int]$Matches[1]
                $left = [int]$Matches[2]
            }
            if ($text -match "(\d+) of them differ from the PPU's") { $differ = [int]$Matches[1] }
            # ...and the lines whose sprites were left, or found differently,
            # with them.
            if ($text -match "Sprites: \d+ lines' found here, (\d+) left to the PPU") {
                $left += [int]$Matches[1]
            }
            if ($differ -ge 0 -and $text -match "(\d+) of those are not what the PPU found") {
                $differ += [int]$Matches[1]
            } else {
                $differ = -1
            }
            $sum = ""
            if ($text -match "Picture checksum ([0-9A-F]{16}) over (\d+) lines") { $sum = "$($Matches[1]) $($Matches[2])" }
            $sumAlone = "none"
            if ($alone -match "Picture checksum ([0-9A-F]{16}) over (\d+) lines") { $sumAlone = "$($Matches[1]) $($Matches[2])" }
            $state = "the same picture"
            $ok = $true
            if ($differ -ne 0 -or $lines -eq 0) { $state = "NOT CHECKED"; $ok = $false }
            if ($differ -gt 0) { $state = "DIFFERS" }
            elseif ($sum -ne $sumAlone) { $state = "ANOTHER PICTURE"; $ok = $false }
            elseif ($runs["check"][$movie].Code -ne 0 -or $runs["native"][$movie].Code -ne 0) {
                $state = "EXIT $($runs['check'][$movie].Code), $($runs['native'][$movie].Code)"
                $ok = $false
            }
            if (-not $ok) { $failed++ }
            $lineTotal += $lines
            $leftTotal += $left
            if ($differ -gt 0) { $differTotal += $differ }
            $row -f $movie, $corpus[$movie], $lines, $left, $(if ($differ -lt 0) { "-" } else { $differ }), $state
        }
        ""
        "$lineTotal lines drawn both ways across $($movies.Count) movie$(if ($movies.Count -ne 1) { 's' }) at $w, " +
            "$differTotal differ; $leftTotal left to the PPU; $failed movie$(if ($failed -ne 1) { 's' }) not the same picture."
        ""
        $bad += $failed
    }
    if ($bad -gt 0) { exit 1 }
    exit 0
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
# `camera_scroll` is the thunk that calls `camera_follow` four times a frame,
# and is left to the ROM with it.
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
    $results = Invoke-Corpus "run" $flags
    $row -f "movie", "frames", "passes", "parted", "drift", "worst", "mean", "state"
    $partedAll = @()
    $passTotal = 0
    $dirty = 0
    foreach ($movie in $movies) {
        $frames = $corpus[$movie]
        $text = $results[$movie].Lines | Out-String
        $code = $results[$movie].Code
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
        # ...and the PPU's memory and scroll, which ported routines that write
        # the hardware can get wrong with every byte of WRAM right.
        $state = "clean"
        if ($text -match "Up to (\d+) byte") {
            $state = "$($Matches[1]) UNEXPLAINED"
            $dirty++
        } elseif ($text -match "or the scroll differed on (\d+)") {
            $state = "VIDEO on $($Matches[1])"
            $dirty++
        } elseif (-not ($text -match "No byte of live game state ever differed")) {
            $state = "NOTHING COMPARED"
            $dirty++
        } elseif ($code -ne 0) {
            # Said clean and exited non-zero: believe the exit code.
            $state = "EXIT $code"
            $dirty++
        }
        # The game left the scheduler before the movie's frames ran out, on
        # both cores, and the run stopped there. `level21-exit` does, waiting
        # for a Start the movie never presses.
        if ($text -match "neither core came back to the WAI") {
            $state += ", left the scheduler"
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
$results = Invoke-Corpus "verify" $(if ($Coverage) { @("-c") } else { @() })
"{0,-24} {1,6} {2,10} {3,7} {4,6}  {5}" -f "movie", "frames", "checked", "decl.", "sites", "census"
foreach ($movie in $movies) {
    $frames = $corpus[$movie]
    $out = $results[$movie].Lines
    $ok = $results[$movie].Code -eq 0
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
