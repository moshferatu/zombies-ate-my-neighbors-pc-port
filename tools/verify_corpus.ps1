# Run `zamn_cosim verify` over the whole movie corpus and print one line each.
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
#
# Exits non-zero if any movie diverges.

param(
    [string]$Rom = "Zombies Ate My Neighbors.sfc",
    [switch]$Coverage
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
    "level29-fighting.zmv"   = 4400
    "level29-firstaid.zmv"   = 4700
    "level33.zmv"            = 3600
    "level37.zmv"            = 4700
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

$failed = 0
$total = 0
$siteTotal = 0
# The intersection of every movie's untaken list, which is the only number that
# says what the *corpus* has never done. Per-movie coverage cannot be added up.
$untakenEverywhere = $null
$censusAll = @{}
"{0,-24} {1,6} {2,10} {3,7} {4,6}  {5}" -f "movie", "frames", "checked", "decl.", "sites", "census"
foreach ($movie in $corpus.Keys) {
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
    $census = $rows -join " "
    $total += $checked
    if (-not $ok) { $failed++ }
    "{0,-24} {1,6} {2,10} {3,7} {4,6}  {5}" -f $movie, $frames, $checked,
        $decl, $sites, $(if ($ok) { $census } else { "DIVERGED" })
}
""
"$total calls checked across $($corpus.Count) movies, $failed diverged."
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
if ($failed -gt 0) { exit 1 }
