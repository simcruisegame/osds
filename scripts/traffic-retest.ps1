<#
.SYNOPSIS
  The traffic re-test of a release candidate: Detroit Woodward north and south, a busy place with
  tile borders on the route, one gap-acceptance junction, and (on request) Bixby south.

.DESCRIPTION
  One launch per case (scripts\launch-review.ps1, the release-test settings, -ShowWindow for a
  visible run) into <OutRoot>\<case>\. The pre-flight judge of every run counts, besides its usual
  rules (docs/team/release-test.md):
    traffic-cut-in-N            a traffic car came into our lane within one car length of our front
                                bumper while we drove (2 m/s or more): across our path (heading more
                                than 30 deg from ours) or from the next lane
    traffic-through-our-car-N   a traffic car's body went into ours
    traffic-turn-round-N        a traffic car turned through 150 deg or more in 3 s (a turn round at a
                                tile edge or in the road)
    motorway-traffic-at-grade-N a car on a motorway ribbon crossed our road (45-135 deg) at our level
                                (within 2.5 m), -10 to 60 m ahead
    junction-hold-Xs-of-Ys      the gap-acceptance case found no stop line (the case did not run)
  The game log names each car (Pre-flight: traffic ...), and the review frames carry an event.
  The gap-acceptance case (-OsdsPreflightStopAtJunction=10): the robot stops at the first stop
  line ahead (a road its road gives way to), holds 10 s on the parking brake, then pulls out; the
  traffic must leave it a full gap (no cut-in, no car through ours).
  After the runs: the cooked gate (scripts\release-test.ps1 -Cooked) and one timed quiet run are
  separate steps; this script does not build a package.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\traffic-retest.ps1 -OutRoot Saved\ReleaseTest\rc5-traffic -ShowWindow
#>
param(
    [string]$OutRoot = '',
    [switch]$ShowWindow,
    [int]$Seconds = 60,
    # Bixby south (36.3690,-121.9012, the approach curve at the bridge's south end), only when the world lane fixed it.
    [switch]$WithBixby,
    # Run only these cases (names below), comma separated.
    [string]$OnlyCases = '',
    # Build the editor target first (Development).
    [switch]$Build
)

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutRoot) { $OutRoot = Join-Path $Root "Saved\ReleaseTest\traffic-$(Get-Date -Format 'yyyyMMdd-HHmm')" }
New-Item -ItemType Directory -Force $OutRoot | Out-Null

# Each case: name, start, start road, car, UTC, start seed (0: the game's own), stop-at-junction seconds.
$Cases = @(
    # (1) Woodward over the Fisher Freeway (I-75): no freeway car may cross Woodward at grade. Seeds of the c1a6b76 runs.
    @{ Name = 'detroit-woodward-north'; Start = '42.3445,-83.0560'; Road = 'Woodward Avenue'; Car = 'nb2001_lowered'; Utc = '2026-11-12T17:32:14Z'; Seed = 18129; Stop = 0 },
    @{ Name = 'detroit-woodward-south'; Start = '42.3390,-83.0532'; Road = 'Woodward Avenue'; Car = 'nb2001_lowered'; Utc = '2026-11-12T17:32:14Z'; Seed = 29587; Stop = 0 },
    # (2) Turn-round and tile edges: Doraville, the motorway with tile borders where a red car cut in within
    # a car length at 37.0-37.2 s on c1a6b76 (same seed).
    @{ Name = 'doraville-corvette'; Start = '33.8920,-84.2560'; Road = ''; Car = 'meridian_z'; Utc = '2026-11-12T17:37:01Z'; Seed = 10291; Stop = 0 },
    # (3) Gap acceptance: East Canfield Street (OSM class minor), Midtown Detroit, the 199 m block between
    # Woodward Avenue (primary; junction 42.35271,-83.06146) and John R Street (tertiary; junction about
    # 42.35360,-83.05935). Both cross roads rank above it, so the robot stops at either line. The start is
    # 90 m before Woodward (110 m before John R); the start pick chooses the direction. Source: the game's own
    # cached OpenMapTiles vector tile z14 4411/6060 (%LOCALAPPDATA%\OSDS\map-cache\v-4411-6060,
    # layer transportation_name), read 2026-11-14.
    @{ Name = 'detroit-canfield-junction'; Start = '42.35311,-83.06051'; Road = 'East Canfield Street'; Car = 'nb2001_lowered'; Utc = '2026-11-12T17:32:14Z'; Seed = 0; Stop = 10 }
)
if ($WithBixby) { $Cases += @{ Name = 'bixby-south'; Start = '36.3690,-121.9012'; Road = 'Cabrillo Highway'; Car = 'meridian_z'; Utc = '2026-11-12T20:08:00Z'; Seed = 15627; Stop = 0 } }
if ($OnlyCases) { $Only = $OnlyCases -split ','; $Cases = @($Cases | Where-Object { $Only -contains $_.Name }) }

& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'gpu-wait.ps1') -TimeoutMinutes 60
Set-GpuBusy "traffic re-test ($($Cases.Count) runs$(if ($ShowWindow) { ', visible' })) on $Root" (5 + 4 * $Cases.Count)
try {
    if ($Build) {
        & "$(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })\Engine\Build\BatchFiles\Build.bat" OSDSEditor Win64 Development "-Project=$Root\OSDS.uproject" -WaitMutex -NoHotReloadFromIDE | Select-Object -Last 1
        if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED exit $LASTEXITCODE"; return }
    }
    foreach ($C in $Cases) {
        $A = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'launch-review.ps1'), '-Name', $C.Name, '-OutRoot', $OutRoot, '-Seconds', $Seconds,
            '-NoCockpit', '-Start', $C.Start, '-Car', $C.Car, '-Utc', $C.Utc)
        if ($C.Road) { $A += @('-StartRoad', $C.Road) }
        if ($C.Seed -gt 0) { $A += @('-StartSeed', $C.Seed) }
        if ($C.Stop -gt 0) { $A += @('-StopAtJunction', $C.Stop) }
        if ($ShowWindow) { $A += '-ShowWindow' }
        & powershell @A | Out-Null
        Write-Host "  $($C.Name) done $((Get-Date).ToUniversalTime().ToString('HH:mm'))Z"
    }
}
finally { Clear-GpuBusy }

# The table: one row per case, the judge's result and the traffic numbers.
$Md = @('# Traffic re-test', '', "Runs in $OutRoot, commit $(git -C $Root rev-parse --short HEAD).", '',
    '| Case | Result | Cut-ins | Through our car | Turn-rounds | Motorway at grade | Junction hold | Stuck | Reasons |', '|---|---|---|---|---|---|---|---|---|')
foreach ($C in $Cases) {
    $F = Join-Path (Join-Path $OutRoot $C.Name) 'preflight.json'
    if (-not (Test-Path $F)) { $Md += "| $($C.Name) | no result | | | | | | | |"; continue }
    $P = Get-Content $F -Raw | ConvertFrom-Json
    $Hold = if ([double]$P.junction_hold_asked_s -gt 0) { "$($P.junction_hold_s) of $($P.junction_hold_asked_s) s ($($P.junction_cross))" } else { '-' }
    $Md += "| $($C.Name) | $($P.result) | $($P.traffic_cut_ins) | $($P.traffic_through_our_car) | $($P.traffic_turn_rounds) | $($P.motorway_traffic_at_grade) | $Hold | $($P.stuck_traffic) | $($P.reasons) |"
}
# Woodward Avenue over the Fisher Freeway (I-75): the frames within 80 m of the crossing, to check by eye that
# no freeway car crosses Woodward at road level (south-51s.png on c1a6b76 showed one).
$Spot = @(42.3417, -83.0547)
$Md += @('', "## Woodward over I-75 ($($Spot[0]), $($Spot[1])): frames to look at", '', '| Case | Frames within 80 m (distance m) | Video seconds |', '|---|---|---|')
foreach ($C in $Cases | Where-Object { $_.Name -like 'detroit-woodward-*' }) {
    $Rev = Join-Path (Join-Path $OutRoot $C.Name) 'review'
    $Near = @(Get-ChildItem $Rev -Filter '*.json' -ErrorAction SilentlyContinue | ForEach-Object {
        $J = Get-Content $_.FullName -Raw | ConvertFrom-Json
        if ($J.PSObject.Properties['lat']) {
            $K = [Math]::PI / 180
            $X = ([double]$J.lon - $Spot[1]) * $K * [Math]::Cos($Spot[0] * $K); $Y = ([double]$J.lat - $Spot[0]) * $K
            $D = 6371000 * [Math]::Sqrt($X * $X + $Y * $Y)
            if ($D -lt 80) { [pscustomobject]@{ Name = $_.BaseName + '.png'; D = [Math]::Round($D); T = [double]$J.t } }
        } } | Sort-Object T)
    $Md += "| $($C.Name) | $(if ($Near.Count) { ($Near | ForEach-Object { "review\$($_.Name) ($($_.D))" }) -join ', ' } else { 'none: the route did not pass it' }) | $(if ($Near.Count) { "$([Math]::Floor($Near[0].T - 3))-$([Math]::Ceiling($Near[-1].T + 3)) s in video.mp4" } else { '-' }) |"
}
$Md += @('', 'Each traffic event has a line "Pre-flight: traffic ..." in the run''s game.log and an event frame in review\. Look at those frames before a verdict.')
$Md | Set-Content -Encoding utf8 (Join-Path $OutRoot 'traffic-retest.md')
Write-Host "TRAFFIC RETEST -> $(Join-Path $OutRoot 'traffic-retest.md')"
