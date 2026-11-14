<#
.SYNOPSIS
  The frame review of a candidate's look and feel, by the "How we look" rules (docs/team/release-test.md).

.DESCRIPTION
  One visible launch per place (scripts\launch-review.ps1: Ultra at the screen's resolution, traffic off,
  the pre-flight robot, a full-size frame every 1 s plus an event frame at every judge event, the 10 fps
  video kept), then for every run:
    sheets\     each frame next to the aerial photograph of the same spot (scripts\detail-sheets.py)
    motion\     event clips 1 s before to 1 s after, real-speed strips (one frame each 0.5 s) and the
                automatic video scan for pop-in, flicker, shimmer, judder and the mirror (scripts\review-motion.py)
  and for the whole review:
    blind\<code>\fNN.png  full-size frames under neutral names for the blind reviewer (blind-map.txt is beside
                          the folder, not in it)
    detail-review.md      the review, in this order: 1 fresh eyes, 2 blind review, 3 photo compare,
                          4 video, 5 checklist, 6 "found by the new method only"
  The Dragon runs twice: as built and with -SimCruiseOldCrowns (the old canopy-peak finder). Detroit has a slow
  segment at 10 km/h beside the buildings, so near facades are seen at full detail.
  -PostOnly with -RunDirs runs the post-processing only, on saved runs (the dry run).
  -Me: the holder name in C:\aiwork\GPU-QUEUE (scripts\gpu-wait.ps1 -Me).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\detail-review.ps1 -OutRoot Saved\ReleaseTest\release8-detail -ShowWindow -Me lane-view-release8-review -Build
  powershell -ExecutionPolicy Bypass -File scripts\detail-review.ps1 -PostOnly -OutRoot Saved\ReleaseTest\dry -RunDirs D:\runs\a,D:\runs\b
#>
param(
    [string]$OutRoot = '',
    [switch]$ShowWindow,
    [int]$Seconds = 45,
    [string]$Me = '',
    [switch]$Build,
    [string]$OnlyCases = '',
    [switch]$PostOnly,
    [string[]]$RunDirs = @(),
    # Frames per place for the blind reviewer.
    [int]$BlindFrames = 12
)

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutRoot) { $OutRoot = Join-Path $Root "Saved\ReleaseTest\detail-$(Get-Date -Format 'yyyyMMdd-HHmm')" }
New-Item -ItemType Directory -Force $OutRoot | Out-Null
# The start car: the lowered Miata's id since the privacy change, else the older id.
$Miata = if (Test-Path (Join-Path $Root 'data\cars\nb2001_lowered.json')) { 'nb2001_lowered' } else { @(Get-ChildItem (Join-Path $Root 'data\cars') -Filter 'nb2001_*.json' | Select-Object -First 1).BaseName }

$Cases = @(
    @{ Name = 'dragon'; Road = 'dragon'; Car = $Miata; Utc = '2026-11-12T17:00:00Z'; Extra = ''; Seconds = $Seconds },
    @{ Name = 'dragon-oldcrowns'; Road = 'dragon'; Car = $Miata; Utc = '2026-11-12T17:00:00Z'; Extra = '-SimCruiseOldCrowns'; Seconds = $Seconds },
    @{ Name = 'ocean-drive'; Road = 'ocean-drive'; Car = $Miata; Utc = '2026-11-12T17:00:00Z'; Extra = ''; Seconds = $Seconds },
    # Delta, Utah (tile 12273_24963): desert; the canopy map gives 236 "crowns" in the start tile.
    @{ Name = 'delta-ut'; Start = '39.3522,-112.5772'; Car = 'mustang65'; Utc = '2026-11-12T19:00:00Z'; Extra = ''; Seconds = $Seconds },
    # Two of data/test-places.json: an Ozark forest road and a Midwest suburb with street trees.
    @{ Name = 'jasper-ar'; Start = '36.0081,-93.1866'; Car = 'meridian_z'; Utc = '2026-11-12T17:30:00Z'; Extra = ''; Seconds = $Seconds },
    @{ Name = 'ankeny-ia'; Start = '41.7318,-93.6001'; Car = $Miata; Utc = '2026-11-12T17:30:00Z'; Extra = ''; Seconds = $Seconds },
    # The slow look: Woodward Avenue, Detroit, at 10 km/h beside the low brick buildings (the building-top fault).
    @{ Name = 'detroit-slow'; Start = '42.3390,-83.0532'; Road = 'Woodward Avenue'; Car = $Miata; Utc = '2026-11-12T17:32:14Z'; Extra = '-SimCruisePreflightKmh=10'; Seconds = 30; StartRoad = 'Woodward Avenue' }
)
if ($OnlyCases) { $Only = $OnlyCases -split ','; $Cases = @($Cases | Where-Object { $Only -contains $_.Name }) }

if (-not $PostOnly) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'gpu-wait.ps1') -TimeoutMinutes 120 -Me $Me
    Set-GpuBusy "frame review ($($Cases.Count) runs$(if ($ShowWindow) { ', visible' })) on $Root" (5 + 3 * $Cases.Count)
    try {
        if ($Build) {
            & "$(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })\Engine\Build\BatchFiles\Build.bat" SimCruiseEditor Win64 Development "-Project=$Root\SimCruise.uproject" -WaitMutex -NoHotReloadFromIDE | Select-Object -Last 1
            if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED exit $LASTEXITCODE"; return }
        }
        foreach ($C in $Cases) {
            $A = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'launch-review.ps1'), '-Name', $C.Name, '-OutRoot', $OutRoot, '-Seconds', $C.Seconds,
                '-NoCockpit', '-Car', $C.Car, '-Utc', $C.Utc)
            if ($C.Start) { $A += @('-Start', $C.Start) } else { $A += @('-Road', $C.Road) }
            if ($C.StartRoad) { $A += @('-StartRoad', $C.StartRoad) }
            # The colon form: a value that starts with '-' is otherwise read as a parameter name.
            if ($C.Extra) { $A += "-ExtraArgs:$($C.Extra)" }
            if ($ShowWindow) { $A += '-ShowWindow' }
            & powershell @A | Out-Null
            Write-Host "  $($C.Name) done $((Get-Date).ToUniversalTime().ToString('HH:mm'))Z"
        }
    }
    finally { Clear-GpuBusy }
    $RunDirs = @($Cases | ForEach-Object { Join-Path $OutRoot $_.Name })
}
$RunDirs = @($RunDirs | ForEach-Object { $_ -split ',' } | Where-Object { $_ -and (Test-Path $_) })
if (-not $RunDirs.Count) { Write-Host 'No run folders.'; exit 2 }

# Post-processing: the photo sheets, the motion review, the blind folders.
$Py = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $Py) { Write-Host 'python not found'; exit 2 }
& $Py (Join-Path $PSScriptRoot 'detail-sheets.py') @RunDirs
& $Py (Join-Path $PSScriptRoot 'review-motion.py') @RunDirs
$Blind = Join-Path $OutRoot 'blind'
Remove-Item -Recurse -Force $Blind -ErrorAction SilentlyContinue
$Map = @()
$Code = 0
foreach ($Dir in $RunDirs) {
    $Code++
    $Place = 'place{0:D2}' -f $Code
    New-Item -ItemType Directory -Force (Join-Path $Blind $Place) | Out-Null
    $Frames = @(Get-ChildItem (Join-Path $Dir 'review') -Filter '*.png' | Sort-Object Name)
    $Step = [Math]::Max(1, [Math]::Floor($Frames.Count / $BlindFrames))
    $K = 0
    for ($I = 0; $I -lt $Frames.Count -and $K -lt $BlindFrames; $I += $Step) {
        $K++
        $N = 'f{0:D2}.png' -f $K
        Copy-Item $Frames[$I].FullName (Join-Path (Join-Path $Blind $Place) $N)
        $Map += "$Place\$N = $($Frames[$I].FullName)"
    }
}
$Map | Set-Content -Encoding utf8 (Join-Path $OutRoot 'blind-map.txt')

$Md = @('# Frame review', '', "Runs: $($RunDirs -join ', '). Commit $(git -C $Root rev-parse --short HEAD). The rules: docs/team/release-test.md, ""How we look"".", '',
    'Do the steps in this order. Write each fault with its frame or clip path and the step that found it.', '',
    '## 1. Fresh eyes (before any checklist)', '',
    'Open the full-size frames (<run>\review\*.png, one a second) at 1920x1080, as a player, and list everything a person would call wrong or unreal. No checklist yet.', '',
    '| Place | Fault | Frame | Severity |', '|---|---|---|---|', '', '',
    '## 2. Blind review', '',
    "A fresh reviewer (a new subagent with no history) gets only $Blind (folders place01.. with frames f01..; no names, no hints) and the words ""list everything that looks wrong or unreal"". Its list goes here; blind-map.txt maps the frames back.", '',
    '| Fault (reviewer''s words) | Frame | Mapped to | Found in step 1 too? |', '|---|---|---|---|', '', '',
    '## 3. Real-place compare', '',
    'Each place: <run>\sheets\all.png and one sheet per frame (the game left, the aerial photograph right, travel up, rings 25/50/100 m). A legal street-level photo where we have one (licence recorded). List the differences by size, biggest first.', '',
    '| Place | Difference (biggest first) | Sheet |', '|---|---|---|', '', '',
    '## 4. Video and motion', '',
    'Each place: <run>\motion\motion.md (judge event clips, the scan''s pop, flicker, shimmer, mirror and judder candidates with pictures) and <run>\motion\strip-NN.png (one frame each 0.5 s). Watch every clip at real speed and give each candidate a verdict there. Wheel and gauge motion: watch the cluster in the clips (not automatic).', '', '',
    '## 5. Checklist', '',
    '| Place | Result | Trees / shrubs (log) | Building top (window rows, parapet) | Ground colour vs photo | Trees vs photo (positions, crown size, count) | Texture classes (cut, stretch, repeat, trim, scale, flat colour, light) | Cockpit (dash, mirror, gauges) | Detail fade / pop (video) | Close ground (video) |', '|---|---|---|---|---|---|---|---|---|---|')
foreach ($Dir in $RunDirs) {
    $P = if (Test-Path (Join-Path $Dir 'preflight.json')) { Get-Content (Join-Path $Dir 'preflight.json') -Raw | ConvertFrom-Json } else { $null }
    $Trees = if (Test-Path (Join-Path $Dir 'game.log')) { (Select-String (Join-Path $Dir 'game.log') -Pattern '(\d+) trees and (\d+) shrubs on (\d+) tiles' | Select-Object -Last 1 | ForEach-Object { $_.Matches[0].Value }) } else { '' }
    $Md += "| $(Split-Path -Leaf $Dir) | $(if ($P) { "$($P.result) $($P.reasons)" } else { 'no result' }) | $Trees | | | | | | | |"
}
$Md += @('', 'Delta, UT: shrubs drawn as shrubs (low, sparse, grey-green) pass; tall trees in the desert are a P1. Dragon against dragon-oldcrowns: the same frames, the segmented crowns against the old finder.', '',
    '## 6. Found by the new method only', '',
    'Every fault above, the step that found it first, and whether the old method (a timed frame every 5 s, the start frame, the judge''s event frames, the checklist) had it in a frame at all: an old timed frame existed at 0, 5, 10 ... s, so a fault seen only between them, only in motion, or only by the blind reviewer was not in the old review.', '',
    '| Fault | Place | Time (s) | Found by (1 s frames / event clip / blind review / photo compare / video scan / checklist) | In an old 5 s frame? |', '|---|---|---|---|---|')
$Md | Set-Content -Encoding utf8 (Join-Path $OutRoot 'detail-review.md')
Write-Host "FRAME REVIEW -> $(Join-Path $OutRoot 'detail-review.md'); blind frames in $Blind"
