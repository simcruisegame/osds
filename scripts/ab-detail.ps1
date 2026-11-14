<#
.SYNOPSIS
  The GPU cost of one detail: the same place and view with the detail on and off, alternating, N runs
  each, on a quiet PC. The rule (2026-11-14 team meeting): each new detail costs at most 0.3 ms GPU p95
  at Ultra and has an off switch.

.DESCRIPTION
  Each run is one game launch with -SimCruiseDrivePerf=ultra (the game's own perf sampler: 10 s in the
  driver's seat and 10 s in the chase view after the car settles; GPU time per frame from the GPU, so the
  60 fps frame cap does not hide it), at -Place (a hero road or lat,lon), traffic off, the screen's own
  resolution. -OnArgs are the game arguments with the detail on (often none), -OffArgs with it off (the
  detail's switch, for example '-SimCruiseOldCrowns' or '"-ExecCmds=simcruise.Detail.Bushes 0"').
  The runs alternate on, off, on, off ... so a slow drift of the PC (heat) falls on both.
  The result (ab.md in -OutDir): GPU p95 and average per run and view, the mean and spread of each side,
  the difference on - off and its noise (the standard error of the difference). The verdict: PASS when the
  difference is 0.3 ms or less; FAIL when it is more than 0.3 ms and more than twice its noise; NOT SURE
  when the noise is too large to tell (run more, on a quieter PC). Every run checks the PC is quiet
  (no other game or build), else it is marked DISTURBED and left out.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\ab-detail.ps1 -Name crowns -Place dragon -OffArgs '-SimCruiseOldCrowns' -Runs 3
#>
param(
    [Parameter(Mandatory)][string]$Name,
    [string]$Place = 'dragon',
    [string]$OnArgs = '',
    [string]$OffArgs = '',
    [int]$Runs = 3,
    [string]$Car = 'meridian_z',
    [string]$OutDir = '',
    [string]$Me = '',
    [double]$LimitMs = 0.3
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Ue = if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }
$Editor = Join-Path $Ue 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not $OutDir) { $OutDir = Join-Path $Root "Saved\ReleaseTest\ab-$Name-$(Get-Date -Format 'yyyyMMdd-HHmm')" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$Csv = Join-Path $Root 'Saved\Profiling\SimCruisePerf.csv'
$Places = @{ 'ocean-drive' = '-SimCruiseBundledStart'; 'dragon' = '-SimCruiseStart=35.4792763,-83.9327265 "-SimCruiseStartRoad=Calderwood Highway"'; 'nebraska' = '-SimCruiseStart=40.56724,-96.5909 "-SimCruiseStartRoad=Princeton Road"'; 'big-sur' = '-SimCruiseStart=36.363197,-121.900041 "-SimCruiseStartRoad=Cabrillo Highway"' }
$PlaceArgs = if ($Places.ContainsKey($Place)) { $Places[$Place] } else { "-SimCruiseStart=$Place" }
function Busy { @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^(UnrealBuildTool|cl|link|ShaderCompileWorker|UnrealEditor-Cmd)$' }).Count + @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -notmatch [regex]::Escape($OutDir) }).Count }

& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'gpu-wait.ps1') -TimeoutMinutes 240 -Me $Me
Set-GpuBusy "A/B GPU cost of '$Name' at $Place ($Runs runs each)" (5 + 4 * 2 * $Runs)
$Rows = @()
try {
    for ($I = 0; $I -lt 2 * $Runs; $I++) {
        $Side = if ($I % 2 -eq 0) { 'on' } else { 'off' }
        $Extra = if ($Side -eq 'on') { $OnArgs } else { $OffArgs }
        $RunDir = Join-Path $OutDir ("{0:D2}-{1}" -f ($I + 1), $Side)
        New-Item -ItemType Directory -Force $RunDir | Out-Null
        $Before = if (Test-Path $Csv) { @(Get-Content $Csv).Count } else { 0 }
        $Quiet = (Busy) -eq 0
        $Args = "`"$Root\SimCruise.uproject`" -game -SimCruiseWorld -SimCruiseNoLive -SimCruiseNoNeural -SimCruiseGraphics=ultra -SimCruiseDrivePerf=ultra -SimCruiseCar=$Car $PlaceArgs $Extra " +
            "-NoLiveCoding -nosplash -unattended -log `"-abslog=$RunDir\game.log`" `"-ExecCmds=simcruise.Traffic.Cars 0`" " + ((Get-TestWindowArgs $true) -join ' ') + ' ' + ((Get-NativeResolution) -join ' ')
        if (-not (Enter-EditorLock -GpuTest -TimeoutMinutes 60 -Why "A/B $Name $Side")) { throw 'no GPU lock' }
        try {
            $P = Start-Process -FilePath $Editor -ArgumentList $Args -PassThru
            if (-not $P.WaitForExit(15 * 60 * 1000)) { Stop-ProcessTree -ProcessId $P.Id }
        } finally { Exit-EditorLock }
        $Quiet = $Quiet -and ((Busy) -eq 0)
        $New = if (Test-Path $Csv) { @(Get-Content $Csv | Select-Object -Skip $Before) } else { @() }
        foreach ($L in $New) {
            $F = $L -split ','
            if ($F.Count -lt 14 -or $F[0] -notmatch '^drive_') { continue }
            $Rows += [pscustomobject]@{ Run = $I + 1; Side = $Side; View = ($F[0] -split '_')[-1]; GpuP95 = [double]$F[11]; GpuAvg = [double]$F[10]; FrameP95 = [double]$F[8]; Quiet = $Quiet }
        }
        Write-Host "  run $($I + 1) ($Side): $(@($Rows | Where-Object Run -eq ($I + 1) | ForEach-Object { "$($_.View) gpu p95 $($_.GpuP95) ms" }) -join ', ')$(if (-not $Quiet) { ' DISTURBED' })"
    }
} finally { Clear-GpuBusy }

function Stat($Xs) { $N = @($Xs).Count; if (-not $N) { return @(0, 0, 0) }; $M = ($Xs | Measure-Object -Average).Average; $S = if ($N -gt 1) { [Math]::Sqrt((($Xs | ForEach-Object { ($_ - $M) * ($_ - $M) }) | Measure-Object -Sum).Sum / ($N - 1)) } else { 0 }; return @($M, $S, $N) }
$Md = @("# A/B GPU cost: $Name", '', "Place $Place, car $Car, Ultra at the screen's resolution, traffic off, -SimCruiseDrivePerf (10 s per view). On: '$OnArgs'. Off: '$OffArgs'. Commit $(git -C $Root rev-parse --short HEAD). Rule: at most $LimitMs ms GPU p95.", '',
    '| Run | Side | View | GPU p95 (ms) | GPU avg (ms) | Frame p95 (ms) | Quiet |', '|---|---|---|---|---|---|---|')
$Md += $Rows | ForEach-Object { "| $($_.Run) | $($_.Side) | $($_.View) | $($_.GpuP95) | $($_.GpuAvg) | $($_.FrameP95) | $(if ($_.Quiet) { 'yes' } else { 'DISTURBED' }) |" }
$Md += @('', '| View | On: mean +- sd (n) | Off: mean +- sd (n) | On - off | Noise (se) | Verdict |', '|---|---|---|---|---|---|')
foreach ($View in @($Rows | Select-Object -ExpandProperty View -Unique)) {
    $On = Stat @($Rows | Where-Object { $_.View -eq $View -and $_.Side -eq 'on' -and $_.Quiet } | ForEach-Object GpuP95)
    $Off = Stat @($Rows | Where-Object { $_.View -eq $View -and $_.Side -eq 'off' -and $_.Quiet } | ForEach-Object GpuP95)
    $D = $On[0] - $Off[0]
    $Se = if ($On[2] -and $Off[2]) { [Math]::Sqrt($On[1] * $On[1] / $On[2] + $Off[1] * $Off[1] / $Off[2]) } else { [double]::NaN }
    $Verdict = if ($On[2] -lt 2 -or $Off[2] -lt 2) { 'NOT SURE (too few quiet runs)' } elseif ($D -le $LimitMs) { 'PASS' } elseif ($D -gt 2 * $Se) { 'FAIL' } else { 'NOT SURE (noise too large)' }
    $Md += "| $View | $([Math]::Round($On[0], 2)) +- $([Math]::Round($On[1], 2)) ($($On[2])) | $([Math]::Round($Off[0], 2)) +- $([Math]::Round($Off[1], 2)) ($($Off[2])) | $([Math]::Round($D, 2)) | $([Math]::Round($Se, 2)) | $Verdict |"
}
$Md | Set-Content -Encoding utf8 (Join-Path $OutDir 'ab.md')
Write-Host "A/B -> $(Join-Path $OutDir 'ab.md')"
