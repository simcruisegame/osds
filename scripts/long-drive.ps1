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

  It fails when the car covers under 95 % of the miles, the frame p95 is over 50 ms, any frame is
  over 100 ms (hitches_over_100ms), memory in use grew by more than -MaxRamGrowthMb (default 1500,
  an estimate) between the start of the drive and the end, an enrichment host went over its
  Request budget (the helper's .source-state\*.spent files against helper\request-budgets.json),
  or pre-flight itself failed (spin, off road, tile pop-in, engine error). The LONGDRIVE line also
  reports max_frame, tiles_built and the World cache hit rate. Run it twice, cold then -Warm (same
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
    [switch]$Warm,
    # Most memory in use may grow over the drive (end minus start), MB. 1500 is an estimate.
    [double]$MaxRamGrowthMb = 1500
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
$Stat = @{}
foreach ($M in [regex]::Matches($Extra, '(\w+)=([-0-9.]+)')) { $Stat[$M.Groups[1].Value] = [double]$M.Groups[2].Value }
# Hitches: no frame over 100 ms in the whole drive.
if ($S.hitches_over_100ms -gt 0) { $Reasons += "hitches-over-100ms-$($S.hitches_over_100ms)" }
# Memory: growth from the start of the drive to the end.
$RamGrowth = $null
if ($Stat.ContainsKey('used_ram_mb') -and $Stat.ContainsKey('start_ram_mb')) {
    $RamGrowth = $Stat['used_ram_mb'] - $Stat['start_ram_mb']
    if ($RamGrowth -gt $MaxRamGrowthMb) { $Reasons += ('ram-grew-{0:0}-mb' -f $RamGrowth) }
}
# Request budgets: an enrichment host that spent more than burst + perKm x km. Essential hosts are never refused.
$Overruns = @()
$MapCache = if ($env:OSDS_MAP_CACHE) { $env:OSDS_MAP_CACHE } else { Join-Path $env:LOCALAPPDATA 'OSDS\map-cache' }
$StateDir = Join-Path $MapCache '.source-state'
$SessionFile = Join-Path $StateDir 'session.json'
if ((Test-Path $SessionFile) -and (Test-Path $StateDir)) {
    $Session = Get-Content $SessionFile -Raw | ConvertFrom-Json
    $Table = Get-Content (Join-Path $Root 'helper\request-budgets.json') -Raw | ConvertFrom-Json
    foreach ($F in Get-ChildItem $StateDir -Filter '*.spent') {
        $Spent = Get-Content $F.FullName -Raw | ConvertFrom-Json
        if ($Spent.started -ne $Session.started) { continue }
        $HostName = $F.BaseName
        $Budget = $Table.hosts.$HostName
        if (-not $Budget) { $Budget = $Table.default }
        if ($Budget.tier -eq 'essential') { continue }
        $Allowed = [Math]::Floor($Budget.burst + $Budget.perKm * [Math]::Max(0, $Session.km))
        if ($Spent.n -gt $Allowed) { $Overruns += ('{0}:{1}>{2}' -f $HostName, $Spent.n, $Allowed) }
    }
}
if ($Overruns.Count) { $Reasons += ('budget-overrun-' + ($Overruns -join '+')) }
$Result = if ($Reasons.Count) { 'FAIL' } else { 'PASS' }
Write-Host ("LONGDRIVE {0} road={1} cache={2} miles={3:0.0} mph_mean={4:0.0} frame_p95={5}ms hitches={6} max_frame={7}ms tiles_built={8} hit_rate={9} ram_growth={10}mb tile_popin={11} {12} reasons={13}" -f `
    $Result, $Road, $(if ($Warm) { 'warm' } else { 'cold' }), $Driven, ($S.mean_speed_mps / 0.44704), $S.frame_p95_ms, $S.hitches_over_100ms,
    $(if ($Stat.ContainsKey('max_frame_ms')) { $Stat['max_frame_ms'] } else { 'n/a' }),
    $(if ($Stat.ContainsKey('tiles_built')) { $Stat['tiles_built'] } else { 'n/a' }),
    $(if ($Stat.ContainsKey('world_cache_hit_rate')) { $Stat['world_cache_hit_rate'] } else { 'n/a' }),
    $(if ($null -ne $RamGrowth) { '{0:0}' -f $RamGrowth } else { 'n/a' }),
    $S.tile_popins, $Extra, ($Reasons -join ','))
Write-Host "Evidence: $($Run.FullName)"
if ($Result -eq 'FAIL') { exit 1 }
