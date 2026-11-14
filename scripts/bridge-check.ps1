<#
.SYNOPSIS
  Drives across bridges and overpasses and lists, for each changed span near the route, the frame
  as the car passes over or under it (for the bridge fix: lifted to 4.9 m clearance or de-decked).

.DESCRIPTION
  Each case is one launch (scripts\launch-review.ps1, -Start on a road approach to a bridge, the
  same settings as the release test; -ShowWindow for a visible run). The start pick chooses the
  direction, so each bridge has an approach on both sides: one of them crosses the deck. After the
  drives the script reads the spans the world logged ("span"/"de-deck"/"lift" lines with a lat/lon)
  and each frame's sidecar (lat, lon, path_m), and lists for each span within -NearM of the route
  the closest frame, its distance, and the jolt and yaw events within 2 s. The verdict per span
  (deck continuous, no lip, no floating deck, clearance, car not upset) is the reviewer's, from the
  frames: bridge-check.md has a row per span to fill.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\bridge-check.ps1 -ShowWindow
#>
param(
    [string]$OutRoot = '',
    [switch]$ShowWindow,
    [int]$Seconds = 60,
    [double]$NearM = 60
)

$ErrorActionPreference = 'Stop'
# The world lane's span lines: "Bridge span <tile>/r<n> at <lat>, <lon>: lifted to <m> m clearance (deck <old> -> <new> m)" or "...: de-decked".
$script:SpanPattern = 'Bridge span (\S+) at (-?\d+(?:\.\d+)?), (-?\d+(?:\.\d+)?): (lifted to ([\d.]+) m|de-decked)'
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutRoot) { $OutRoot = (Get-ReviewRoot $Root) + '-bridges' }
New-Item -ItemType Directory -Force $OutRoot | Out-Null
$Cases = @(
    @{ Name = 'bixby-north'; Start = '36.3745,-121.9035'; Road = 'Cabrillo Highway'; Car = 'meridian_z'; Utc = '2026-11-12T20:08:00Z' },
    @{ Name = 'bixby-south'; Start = '36.3690,-121.9012'; Road = 'Cabrillo Highway'; Car = 'meridian_z'; Utc = '2026-11-12T20:08:00Z' },
    @{ Name = 'detroit-woodward-north'; Start = '42.3445,-83.0560'; Road = 'Woodward Avenue'; Car = 'nb2001_aaron'; Utc = '2026-11-12T17:32:14Z' },
    @{ Name = 'detroit-woodward-south'; Start = '42.3390,-83.0532'; Road = 'Woodward Avenue'; Car = 'nb2001_aaron'; Utc = '2026-11-12T17:32:14Z' }
)
foreach ($C in $Cases) {
    $Args = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'launch-review.ps1'), '-Start', $C.Start, '-StartRoad', $C.Road, '-Car', $C.Car,
        '-Seconds', $Seconds, '-Utc', $C.Utc, '-Name', $C.Name, '-OutRoot', $OutRoot, '-NoCockpit')
    if ($ShowWindow) { $Args += '-ShowWindow' }
    & powershell @Args | Out-Null
    Write-Host "  $($C.Name) done"
}

function Get-DistM([double]$La1, [double]$Lo1, [double]$La2, [double]$Lo2) {
    $K = [Math]::PI / 180
    $X = ($Lo2 - $Lo1) * $K * [Math]::Cos(($La1 + $La2) / 2 * $K)
    $Y = ($La2 - $La1) * $K
    return 6371000 * [Math]::Sqrt($X * $X + $Y * $Y)
}
$Md = @('# Bridge check', '', "Runs in $OutRoot. One row per changed span within $NearM m of a route: the closest frame, and the events within 2 s. Fill Seen (deck continuous, no lip or step, no floating deck or parapet, clearance) and Upset from the frames.", '',
    '| Run | Span (log) | Kind | Closest frame | Distance (m) | Events within 2 s | Seen | Upset |', '|---|---|---|---|---|---|---|---|')
foreach ($C in $Cases) {
    $Dir = Join-Path $OutRoot $C.Name
    $Log = Join-Path $Dir 'game.log'
    if (-not (Test-Path $Log)) { $Md += "| $($C.Name) | no log | | | | | | |"; continue }
    $Frames = @(Get-ChildItem (Join-Path $Dir 'review') -Filter '*.json' -ErrorAction SilentlyContinue | ForEach-Object {
        $J = Get-Content $_.FullName -Raw | ConvertFrom-Json
        if ($J.PSObject.Properties['lat']) { [pscustomobject]@{ Name = $_.BaseName + '.png'; T = [double]$J.t; Lat = [double]$J.lat; Lon = [double]$J.lon } } })
    $Rev = if (Test-Path (Join-Path $Dir 'review.json')) { Get-Content (Join-Path $Dir 'review.json') -Raw | ConvertFrom-Json } else { $null }
    $Spans = @(Select-String -Path $Log -Pattern $script:SpanPattern | ForEach-Object {
        [pscustomobject]@{ Text = (($_.Line -replace '^\[.*?\]\[.*?\]', '').Trim()); Id = $_.Matches[0].Groups[1].Value; Lat = [double]$_.Matches[0].Groups[2].Value; Lon = [double]$_.Matches[0].Groups[3].Value; Kind = $(if ($_.Matches[0].Groups[4].Value -match 'de-decked') { 'de-decked' } else { "lifted to $($_.Matches[0].Groups[5].Value) m" }) } })
    if (-not $Spans.Count) { $Md += "| $($C.Name) | no span lines with a position in the log | | | | | | |" }
    foreach ($S in $Spans) {
        $Best = $Frames | Sort-Object { Get-DistM $S.Lat $S.Lon $_.Lat $_.Lon } | Select-Object -First 1
        if (-not $Best) { continue }
        $D = Get-DistM $S.Lat $S.Lon $Best.Lat $Best.Lon
        if ($D -gt $NearM) { continue }
        $Ev = @(if ($Rev) { $Rev.events | Where-Object { $_ -match '^([\d.]+) s' -and [Math]::Abs([double]$Matches[1] - $Best.T) -le 2 } })
        $Md += "| $($C.Name) | $($S.Id) at $($S.Lat), $($S.Lon) | $($S.Kind) | $($C.Name)\review\$($Best.Name) | $([Math]::Round($D)) | $($Ev -join '; ') | | |"
    }
}
$Md | Set-Content -Encoding utf8 (Join-Path $OutRoot 'bridge-check.md')
Write-Host "BRIDGE CHECK -> $(Join-Path $OutRoot 'bridge-check.md')"
