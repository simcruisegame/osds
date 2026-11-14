<#
.SYNOPSIS
  The HUD from the driver's seat in each car: screenshots to check that the HUD covers no
  instrument and that the speed digits are sharp.

.DESCRIPTION
  For each car it starts one game (editor binaries, -game, the bundled Ocean Drive start,
  offline, off screen, the GPU test lock) with -SimCruiseHudShots. From the driver's seat the
  game takes: the HUD with the round map on Always and no route, the HUD with a route, and the
  speed readout showing 0, 60 and 120 (simcruise.Hud.TestSpeed; the car stands still). It
  changes no saved setting and quits itself. Then this script crops the speed readout of each
  speed shot and enlarges it 2x (nearest neighbour, so every pixel shows): speed-<car>-<n>-crop2x.png.

  Output: Saved\Review\<yyyyMMdd-HHmm>-<commit>\hudshots-<car>\ (the pictures, game.log,
  the command line). Look at the pictures before you call the HUD done.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\hud-shots.ps1
  powershell -ExecutionPolicy Bypass -File scripts\hud-shots.ps1 -Cars meridian_z
#>
param(
    # Show the game's window (default: off screen, -RenderOffScreen, nothing on the desktop).
    [switch]$ShowWindow,
    [string[]]$Cars = @('nb1999', 'mustang65', 'meridian_z'),
    [string]$Graphics = 'ultra',
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$TimeoutMinutes = 8,
    [int]$LockTimeoutMinutes = 120
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
Add-Type -AssemblyName System.Drawing
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }
$Cars = @($Cars | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$env:UE_SKIP_UBT_SDK_SETUP = '1'
$OutRoot = Get-ReviewRoot $Root
$Failed = @()

function Save-SpeedCrop([string]$Png) {
    # The speed card at the bottom left: 760 x 300 px of a 1080-line screen, scaled to the shot.
    $Image = [System.Drawing.Image]::FromFile($Png)
    try {
        $K = $Image.Height / 1080.0
        $W = [int](760 * $K); $H = [int](300 * $K)
        $Src = New-Object System.Drawing.Rectangle(0, ($Image.Height - $H), $W, $H)
        $Out = New-Object System.Drawing.Bitmap(($W * 2), ($H * 2))
        $G = [System.Drawing.Graphics]::FromImage($Out)
        $G.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $G.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $G.DrawImage($Image, (New-Object System.Drawing.Rectangle(0, 0, ($W * 2), ($H * 2))), $Src, [System.Drawing.GraphicsUnit]::Pixel)
        $G.Dispose()
        $Crop = [IO.Path]::ChangeExtension($Png, $null).TrimEnd('.') + '-crop2x.png'
        $Out.Save($Crop, [System.Drawing.Imaging.ImageFormat]::Png)
        $Out.Dispose()
        return $Crop
    }
    finally { $Image.Dispose() }
}

foreach ($Car in $Cars) {
    $RunDir = Join-Path $OutRoot "hudshots-$Car"
    New-Item -ItemType Directory -Force $RunDir | Out-Null
    $Log = Join-Path $RunDir 'game.log'
    $GameArgs = @("`"$Project`"", '-game', '-SimCruiseWorld', '-SimCruiseBundledStart', '-SimCruiseNoLive', '-SimCruiseNoNeural', "-SimCruiseGraphics=$Graphics",
        '-SimCruiseUtc=2026-11-12T17:00:00Z', "-SimCruiseCar=$Car", "`"-SimCruiseHudShots=$RunDir`"",
        '-NoLiveCoding', '-nosplash', '-unattended', '-log', "`"-abslog=$Log`"") + @(Get-TestWindowArgs $ShowWindow) + @(Get-NativeResolution)
    ("`"$Editor`" " + ($GameArgs -join ' ')) | Set-Content -Encoding utf8 (Join-Path $RunDir 'command-line.txt')
    if (-not (Enter-EditorLock -GpuTest -TimeoutMinutes $LockTimeoutMinutes -Why "HUD shots $Car on $Root")) { exit 1 }
    $TimedOut = $false
    try {
        $P = Start-Process -FilePath $Editor -ArgumentList ($GameArgs -join ' ') -PassThru
        if (-not $P.WaitForExit($TimeoutMinutes * 60 * 1000)) { Stop-ProcessTree -ProcessId $P.Id; $TimedOut = $true }
    }
    finally { Exit-EditorLock }
    $Done = (Test-Path $Log) -and (Select-String -Path $Log -Pattern 'HUDSHOTS DONE' -Quiet)
    $Shots = @(Get-ChildItem $RunDir -Filter '*.png' -ErrorAction SilentlyContinue | Where-Object { $_.Name -notlike '*-crop2x.png' })
    foreach ($Shot in $Shots | Where-Object { $_.Name -like 'speed-*' }) { $null = Save-SpeedCrop $Shot.FullName }
    $State = if ($TimedOut) { 'TIMED OUT' } elseif (-not $Done) { 'NOT DONE' } else { 'DONE' }
    Write-Host "HUDSHOTS $State car=$Car shots=$($Shots.Count) dir=$RunDir"
    if ($State -ne 'DONE') { $Failed += $Car }
}
if ($Failed.Count) { Write-Host "HUD SHOTS FAILED: $($Failed -join ', ')"; exit 1 }
Write-Host "HUD SHOTS DONE: $OutRoot\hudshots-<car>\"
exit 0
