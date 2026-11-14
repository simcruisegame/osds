<#
.SYNOPSIS
  The final test of a release candidate, in one chain: the full visible Ultra test with the new review
  method, then the cooked package from the exact hash, the cooked gate and one timed quiet run.

.DESCRIPTION
  Two worktrees, so the package is the exact candidate and the test code is not in it:
    -TestTree   (default D:\sc\lane-rc):  the candidate + the test-only commits (-TestCommits), for the
                editor runs: scripts\release-test.ps1 -ShowWindow (hero places with the three cars, the
                seeded random places, the cases), then scripts\detail-review.ps1 -PostOnly on its runs
                (photo sheets, event clips, strips, video scan, blind folders).
    -PackageTree (default D:\sc\lane-rc2): the exact candidate hash, no local changes (checked), for
                scripts\package.ps1 -Configuration Development, then release-test.ps1 -Cooked
                (the cooked-drive cases: Dragon and Ocean Drive; longest frame 0.5 s or less, no
                "Preparing Shaders", the owner-name search of the package), then one timed quiet
                cooked run (Dragon; NOT QUIET in its report fails it).
  Local changes in a worktree are stashed first (named "final-test <time>"), never thrown away.
  Each step waits behind scripts\gpu-wait.ps1 and holds C:\aiwork\GPU-BUSY while it runs.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\final-test.ps1 -Hash 08787f6 -Branch world-detail-v2-release-11
#>
param(
    [Parameter(Mandatory)][string]$Hash,
    [string]$Branch = '',
    [string]$TestTree = 'D:\sc\lane-rc',
    [string]$PackageTree = 'D:\sc\lane-rc2',
    [string[]]$TestCommits = @('16b7572', '8323a86', 'fd9f51f', 'daffdf7'),
    [string]$Out = '',
    [switch]$SkipEditorTest,
    [switch]$SkipCooked
)
$ErrorActionPreference = 'Stop'
$Ue = if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }
$Stamp = Get-Date -Format 'yyyyMMdd-HHmm'
if (-not $Out) { $Out = Join-Path $TestTree "Saved\ReleaseTest\final-$Hash-$Stamp" }

function Prepare([string]$Tree, [string[]]$Picks) {
    Push-Location $Tree
    try {
        if (git status --porcelain) { git stash push -q -u -m "final-test $Stamp"; Write-Host "  ${Tree}: local changes stashed (final-test $Stamp)" }
        if ($Branch) { git -c credential.helper= -c 'credential.helper=!gh auth git-credential' fetch -q origin $Branch }
        git checkout -q --detach $Hash
        if ((git rev-parse --short HEAD) -notlike "$($Hash.Substring(0, 7))*") { throw "$Tree is not at $Hash" }
        foreach ($C in $Picks) { git cherry-pick $C | Out-Null; if ($LASTEXITCODE) { throw "cherry-pick $C failed in $Tree" } }
        if (git status --porcelain) { throw "$Tree has local changes after the checkout" }
        Write-Host "  $Tree at $(git log --oneline -1)"
    } finally { Pop-Location }
}
function Build([string]$Tree) {
    & (Join-Path $Ue 'Engine\Build\BatchFiles\Build.bat') SimCruiseEditor Win64 Development "-Project=$Tree\SimCruise.uproject" -WaitMutex -NoHotReloadFromIDE | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { throw "build failed in $Tree" }
}

if (-not $SkipEditorTest) {
    Prepare $TestTree $TestCommits
    Build $TestTree
    & powershell -ExecutionPolicy Bypass -File (Join-Path $TestTree 'scripts\check.ps1') -NoBuild -NoViewCheck -Filter 'SimCruise.Gameplay.Preflight' | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0) { throw 'the pre-flight unit tests failed' }
    # The full visible Ultra test (it waits for the GPU and holds GPU-BUSY itself).
    & powershell -ExecutionPolicy Bypass -File (Join-Path $TestTree 'scripts\release-test.ps1') -ShowWindow | Select-Object -Last 5
    $Run = Get-ChildItem (Join-Path $TestTree 'Saved\ReleaseTest') -Directory | Where-Object { $_.Name -like "*$($Hash.Substring(0, 7))*" -or $_.LastWriteTime -gt (Get-Date).AddHours(-6) } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $Dirs = @(Get-ChildItem $Run.FullName -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'video.mp4') } | ForEach-Object FullName)
    & powershell -ExecutionPolicy Bypass -File (Join-Path $TestTree 'scripts\detail-review.ps1') -PostOnly -OutRoot (Join-Path $Run.FullName 'review') -RunDirs ($Dirs -join ',') | Select-Object -Last 2
}

if (-not $SkipCooked) {
    Prepare $PackageTree @()
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\gpu-wait.ps1') -TimeoutMinutes 120
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\package.ps1') -Configuration Development | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw 'package failed' }
    Push-Location $PackageTree; try { if (git status --porcelain) { throw 'the package tree changed during the package build' } } finally { Pop-Location }
    # The static dependency check: every DLL the shipped binaries need, shipped or part of Windows.
    & python (Join-Path $PSScriptRoot 'dep-check.py') (Join-Path $PackageTree 'Saved\Package\Windows') (Join-Path $PackageTree 'Saved\Package\dependencies.md')
    # The cooked gate: Dragon and Ocean Drive in the cooked game, the shader and name checks.
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\release-test.ps1') -Cooked -ShowWindow -Cases 'cooked-drive' | Select-Object -Last 5
    # One timed quiet run: the cooked Dragon drive again, its report must say quiet (no other load).
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\release-test.ps1') -Cooked -ShowWindow -Places 'dragon' -NoWarm | Select-Object -Last 5
}
Write-Host "FINAL TEST of $Hash done $((Get-Date).ToUniversalTime().ToString('HH:mm'))Z"
