<#
.SYNOPSIS
  The Long drive: the Drive robot drives about 100 miles at 75-80 mph, visibly, at Ultra, and the
  run reports whether streaming keeps up.

.DESCRIPTION
  A thin wrapper over scripts\preflight.ps1 (so it takes the GPU lock, shows the window, records
  frames and frame times, and keeps the evidence under Saved\Review). It sets the cruise speed with
  -OsdsPreflightKmh and the run length from the distance and the speed, then reads preflight.json
  and prints one LONGDRIVE line:

    LONGDRIVE PASS|FAIL road=.. miles=.. mph=.. frame_p95=..ms hitches=.. tile_popin=.. world_cache_hits=.. ...

  It fails when the car covers under 95 % of the miles, the frame p95 is over 50 ms, or pre-flight
  itself failed (spin, off road, tile pop-in, engine error). Run it twice, cold then -Warm (same
  caches, nothing cleared), to see what the World cache and the map cache save: a warm run must
  make no more helper requests than a cold one.

  Default road is us-50-nevada (US-50, a long straight at speed); any scripts\preflight.ps1 road
  name works, so the same drive can be run at a Random US place (-Road randomus).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\long-drive.ps1
  powershell -ExecutionPolicy Bypass -File scripts\long-drive.ps1 -Miles 20 -Mph 78 -Road kansas-plains
#>
param(
    [string]$Road = 'us-50-nevada',
    [string]$Car = 'nb2001_lowered',
    [double]$Miles = 100,
    # 75-80 mph is the spec; the middle by default.
    [ValidateRange(75, 80)][double]$Mph = 77,
    # Hidden windows are not a Long drive: the Owner watches (the default shows the window).
    [switch]$Hidden,
    # Label for the report: the second run over the same route is the warm one.
    [switch]$Warm
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Mps = $Mph * 0.44704
$Kmh = $Mph * 1.609344
$Seconds = [int][Math]::Ceiling($Miles * 1609.344 / $Mps * 1.05)
$Minutes = [int][Math]::Ceiling($Seconds / 60.0) + 25
Write-Host ("Long drive: {0} miles at {1} mph on {2} ({3} s, timeout {4} min), {5} caches" -f $Miles, $Mph, $Road, $Seconds, $Minutes, $(if ($Warm) { 'warm' } else { 'cold' }))

$Pre = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'preflight.ps1'),
    '-Roads', $Road, '-Cars', $Car, '-Seconds', $Seconds, '-TimeoutMinutes', $Minutes, '-Graphics', 'ultra',
    '-ExtraArgs', ('-OsdsPreflightKmh={0:0.#}' -f $Kmh))
if (-not $Hidden) { $Pre += '-ShowWindow' }
$Before = Get-Date
& powershell @Pre
$PreflightExit = $LASTEXITCODE

$Review = Join-Path $Root 'Saved\Review'
$Run = Get-ChildItem $Review -Directory -Filter '*-preflight' | Where-Object { $_.LastWriteTime -ge $Before.AddMinutes(-1) } | Sort-Object LastWriteTime | Select-Object -Last 1
$Json = if ($Run) { Get-ChildItem $Run.FullName -Recurse -Filter preflight.json | Select-Object -First 1 } else { $null }
if (-not $Json) { Write-Host "LONGDRIVE FAIL road=$Road reasons=no-preflight-json (pre-flight exit $PreflightExit)"; exit 1 }
$S = Get-Content $Json.FullName -Raw | ConvertFrom-Json
$Log = Join-Path (Split-Path $Json.FullName) 'game.log'

$Driven = $S.distance_m / 1609.344
$Reasons = @()
if ($Driven -lt 0.95 * $Miles) { $Reasons += ('short-{0:0.0}-of-{1}-miles' -f $Driven, $Miles) }
if ($S.frame_p95_ms -gt 50) { $Reasons += "slow-frames-p95-$($S.frame_p95_ms)ms" }
if ($PreflightExit -ne 0) { $Reasons += "preflight-exit-$PreflightExit" }
if ($S.reasons) { $Reasons += $S.reasons }
# What the services and the caches did (from the game's own log lines when present).
$Requests = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'LONGDRIVE_STATS (.*)' | Select-Object -Last 1) } else { @() }
$Extra = if ($Requests.Count) { $Requests[0].Matches[0].Groups[1].Value } else { 'stats=none' }
# A warm run over the same route (ADR 0005): nothing generated, nothing downloaded.
if ($Warm -and $Extra -match 'world_cache_stores=(\d+)' -and [int]$Matches[1] -gt 0) { $Reasons += "warm-run-generated-$($Matches[1])-layers" }
if ($Warm -and $Extra -match 'helper_requests=(\d+)' -and [int]$Matches[1] -gt 0) { $Reasons += "warm-run-made-$($Matches[1])-network-requests" }
$Result = if ($Reasons.Count) { 'FAIL' } else { 'PASS' }
Write-Host ("LONGDRIVE {0} road={1} cache={2} miles={3:0.0} mph_mean={4:0.0} frame_p95={5}ms hitches={6} tile_popin={7} {8} reasons={9}" -f `
    $Result, $Road, $(if ($Warm) { 'warm' } else { 'cold' }), $Driven, ($S.mean_speed_mps / 0.44704), $S.frame_p95_ms, $S.hitches_over_100ms, $S.tile_popins, $Extra, ($Reasons -join ','))
Write-Host "Evidence: $($Run.FullName)"
if ($Result -eq 'FAIL') { exit 1 }
