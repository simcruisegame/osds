<#
.SYNOPSIS
  Frame review of the world's fine detail (ground, trees, buildings) against the aerial photograph.

.DESCRIPTION
  One visible launch per place (scripts\launch-review.ps1, Ultra at the screen's resolution, the
  pre-flight robot, -Seconds each), then scripts\detail-sheets.py puts every review frame next to the
  aerial photograph of the same spot (USGS Imagery Only, NAIP-based), turned so the travel direction
  is up, with the car and its view wedge. The Dragon runs twice: as built, and with
  -SimCruiseOldCrowns (the old canopy-peak finder instead of the segmented crowns), for an A/B of the
  same spot. detail-review.md has a row per place for the reviewer:
    ground colour and tint against the photo; tree positions and crown size against the photo;
    the crowns in view against the trees the photo shows; the tree detail fade (no pop between
    levels, video); close ground (no tiling, no swimming, video); at Delta, UT: shrubs drawn as
    shrubs (low, sparse, grey-green), not tall trees in the desert (a P1 if they are trees).
  The game log's "N trees and N shrubs on N tiles" line is copied into the table.
  -Me: the holder name in C:\aiwork\GPU-QUEUE (scripts\gpu-wait.ps1 -Me).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\detail-review.ps1 -OutRoot Saved\ReleaseTest\release8-detail -ShowWindow -Me lane-view-release8-review
#>
param(
    [string]$OutRoot = '',
    [switch]$ShowWindow,
    [int]$Seconds = 45,
    [string]$Me = '',
    [switch]$Build,
    [string]$OnlyCases = ''
)

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutRoot) { $OutRoot = Join-Path $Root "Saved\ReleaseTest\detail-$(Get-Date -Format 'yyyyMMdd-HHmm')" }
New-Item -ItemType Directory -Force $OutRoot | Out-Null
# The start car: the lowered Miata's id since the privacy change, else the older id.
$Miata = if (Test-Path (Join-Path $Root 'data\cars\nb2001_lowered.json')) { 'nb2001_lowered' } else { @(Get-ChildItem (Join-Path $Root 'data\cars') -Filter 'nb2001_*.json' | Select-Object -First 1).BaseName }

$Cases = @(
    @{ Name = 'dragon'; Road = 'dragon'; Car = $Miata; Utc = '2026-11-12T17:00:00Z'; Extra = '' },
    @{ Name = 'dragon-oldcrowns'; Road = 'dragon'; Car = $Miata; Utc = '2026-11-12T17:00:00Z'; Extra = '-SimCruiseOldCrowns' },
    @{ Name = 'ocean-drive'; Road = 'ocean-drive'; Car = $Miata; Utc = '2026-11-12T17:00:00Z'; Extra = '' },
    # Delta, Utah (tile 12273_24963): desert; the canopy map gives 236 "crowns" in the start tile.
    @{ Name = 'delta-ut'; Start = '39.3522,-112.5772'; Car = 'mustang65'; Utc = '2026-11-12T19:00:00Z'; Extra = '' },
    # Two of data/test-places.json: an Ozark forest road and a Midwest suburb with street trees.
    @{ Name = 'jasper-ar'; Start = '36.0081,-93.1866'; Car = 'meridian_z'; Utc = '2026-11-12T17:30:00Z'; Extra = '' },
    @{ Name = 'ankeny-ia'; Start = '41.7318,-93.6001'; Car = $Miata; Utc = '2026-11-12T17:30:00Z'; Extra = '' }
)
if ($OnlyCases) { $Only = $OnlyCases -split ','; $Cases = @($Cases | Where-Object { $Only -contains $_.Name }) }

& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'gpu-wait.ps1') -TimeoutMinutes 120 -Me $Me
Set-GpuBusy "fine-detail frame review ($($Cases.Count) runs$(if ($ShowWindow) { ', visible' })) on $Root" (5 + 3 * $Cases.Count)
try {
    if ($Build) {
        & "$(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })\Engine\Build\BatchFiles\Build.bat" SimCruiseEditor Win64 Development "-Project=$Root\SimCruise.uproject" -WaitMutex -NoHotReloadFromIDE | Select-Object -Last 1
        if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED exit $LASTEXITCODE"; return }
    }
    foreach ($C in $Cases) {
        $A = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'launch-review.ps1'), '-Name', $C.Name, '-OutRoot', $OutRoot, '-Seconds', $Seconds,
            '-NoCockpit', '-Car', $C.Car, '-Utc', $C.Utc)
        if ($C.Start) { $A += @('-Start', $C.Start) } else { $A += @('-Road', $C.Road) }
        # The colon form: a value that starts with '-' is otherwise read as a parameter name.
        if ($C.Extra) { $A += "-ExtraArgs:$($C.Extra)" }
        if ($ShowWindow) { $A += '-ShowWindow' }
        & powershell @A | Out-Null
        Write-Host "  $($C.Name) done $((Get-Date).ToUniversalTime().ToString('HH:mm'))Z"
    }
}
finally { Clear-GpuBusy }

# The sheets (the aerial photographs are fetched once per frame and kept in <run>\aerial).
$Py = (Get-Command python -ErrorAction SilentlyContinue).Source
if ($Py) { & $Py (Join-Path $PSScriptRoot 'detail-sheets.py') @($Cases | ForEach-Object { Join-Path $OutRoot $_.Name }) }
$Md = @('# Fine-detail frame review', '', "Runs in $OutRoot, commit $(git -C $Root rev-parse --short HEAD). Sheets: <run>\sheets\all.png and one per frame (game left, aerial right, travel up, rings 25/50/100 m).", '',
    '| Place | Result | Trees / shrubs (log) | Ground colour vs photo | Trees: positions, crown size, count in view vs photo | Detail fade (video) | Close ground (video) | Verdict |', '|---|---|---|---|---|---|---|---|')
foreach ($C in $Cases) {
    $Dir = Join-Path $OutRoot $C.Name
    $P = if (Test-Path (Join-Path $Dir 'preflight.json')) { Get-Content (Join-Path $Dir 'preflight.json') -Raw | ConvertFrom-Json } else { $null }
    $Trees = if (Test-Path (Join-Path $Dir 'game.log')) { (Select-String (Join-Path $Dir 'game.log') -Pattern '(\d+) trees and (\d+) shrubs on (\d+) tiles' | Select-Object -Last 1 | ForEach-Object { $_.Matches[0].Value }) } else { '' }
    $Md += "| $($C.Name)$(if ($C.Extra) { " ($($C.Extra))" }) | $(if ($P) { "$($P.result) $($P.reasons)" } else { 'no result' }) | $Trees | | | | | |"
}
$Md += @('', 'Delta, UT: shrubs drawn as shrubs (low, sparse, grey-green) pass; tall trees in the desert are a P1. Dragon against dragon-oldcrowns: the same frames, the segmented crowns against the old finder.')
$Md | Set-Content -Encoding utf8 (Join-Path $OutRoot 'detail-review.md')
Write-Host "DETAIL REVIEW -> $(Join-Path $OutRoot 'detail-review.md')"
