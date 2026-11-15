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
    # The test-only commits: our review branch since main (cherry-picked in order onto the candidate).
    [string]$TestRange = 'origin/main..review/how-we-look',
    [string[]]$TestCommits = @(),
    # Commits picked onto the candidate in the package tree (the installer lane's; no game code), e.g. fbe0cfa.
    [string[]]$PackagePicks = @(),
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
    if (-not $TestCommits.Count) { $TestCommits = @(git -C $TestTree rev-list --reverse --no-merges $TestRange) }
    Write-Host "  test-only commits: $($TestCommits.Count) ($TestRange)"
    Prepare $TestTree $TestCommits
    Build $TestTree
    & powershell -ExecutionPolicy Bypass -File (Join-Path $TestTree 'scripts\check.ps1') -NoBuild -NoViewCheck -Filter 'SimCruise.Gameplay.Preflight' | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0) { throw 'the pre-flight unit tests failed' }
    $S = Join-Path $TestTree 'scripts'
    New-Item -ItemType Directory -Force $Out | Out-Null
    $Log = Join-Path $Out 'final-test-steps.log'
    function Step([string]$What) { $L = "$((Get-Date).ToUniversalTime().ToString('HH:mm'))Z $What"; Write-Host $L; Add-Content -Encoding utf8 $Log $L }
    # 3. Bridges first (the new bridge decks are the newest change): Doraville Northcrest Road over I-285,
    # Bixby, Detroit Woodward over I-75, both ways.
    Step 'part 3: bridge-check'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $S 'bridge-check.ps1') -OutRoot (Join-Path $Out 'bridges') -ShowWindow | Select-Object -Last 1
    Step 'part 3 done'
    # 2. The full visible Ultra test: four hero roads x three cars, 30 random places (3 AK, 3 HI), the cases.
    Step 'part 2: release-test -ShowWindow -AllCars'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $S 'release-test.ps1') -ShowWindow -AllCars | Select-Object -Last 5
    $Run = Get-ChildItem (Join-Path $TestTree 'Saved\ReleaseTest') -Directory | Where-Object { $_.LastWriteTime -gt (Get-Date).AddHours(-8) -and (Test-Path (Join-Path $_.FullName 'summary.md')) } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    Step "part 2 done: $($Run.FullName)"
    # 3b. Civil structures: each example of data\civil-places.json from its approaches (the random one both ways).
    $Civil = Join-Path $TestTree 'data\civil-places.json'
    if (Test-Path $Civil) {
        Step 'part 3b: civil structures'
        $C = Get-Content $Civil -Raw | ConvertFrom-Json
        $N = 0
        foreach ($P in $C.places) {
            if ($null -eq $P.lat) { continue }
            $Starts = if ($P.approach_a) { @(@('a', "$($P.approach_a[0]),$($P.approach_a[1])"), @('b', "$($P.approach_b[0]),$($P.approach_b[1])")) } else { @(@('x', "$($P.lat),$($P.lon)")) }
            foreach ($St in $Starts) {
                $N++
                & powershell -ExecutionPolicy Bypass -File (Join-Path $S 'launch-review.ps1') -Name ("civil-{0:D2}-{1}-{2}-{3}" -f $N, $P.kind, $P.source, $St[0]) -OutRoot (Join-Path $Out 'civil') -Start $St[1] -Car 'meridian_z' -Seconds 45 -NoCockpit -ShowWindow | Out-Null
            }
        }
    } else { Step 'part 3b: no data\civil-places.json (set aside)' }
    # 4. Drive real: the cockpit review per car, a keen Dragon run per car (the vehicle benches ran in part 1).
    Step 'part 4: cockpit review and keen Dragon runs'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $S 'cockpit-review.ps1') -ShowWindow | Select-Object -Last 2
    foreach ($Car in @((Get-ChildItem (Join-Path $TestTree 'data\cars') -Filter 'nb2001_*.json' | Select-Object -First 1).BaseName, 'mustang65', 'meridian_z')) {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $S 'launch-review.ps1') -Name "keen-dragon-$Car" -OutRoot (Join-Path $Out 'drive-real') -Road dragon -Car $Car -Seconds 90 -NoCockpit -ShowWindow '-ExtraArgs:-SimCruisePreflightKeen' | Out-Null
    }
    # 5. The slow look beside the buildings (Detroit, 10 km/h).
    Step 'part 5: slow look'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $S 'detail-review.ps1') -OutRoot (Join-Path $Out 'slow-look') -ShowWindow -OnlyCases detroit-slow | Select-Object -Last 1
    # 6. Controller: every menu (pad-review) and every driving control (the shell self-test, own launch).
    Step 'part 6: pad review and the driving pad pass'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $S 'pad-review.ps1') -OutDir (Join-Path $Out 'pad-review') -NoBuild -ShowWindow | Select-Object -Last 3
    . (Join-Path $S 'EditorLock.ps1')
    $PadLog = Join-Path $Out 'pad-drive.log'
    if (Enter-EditorLock -GpuTest -TimeoutMinutes 60 -Why 'final test: driving pad pass') {
        try {
            $P = Start-Process -FilePath (Join-Path $Ue 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList "`"$TestTree\SimCruise.uproject`" -game -SimCruiseWorld -SimCruiseBundledStart -SimCruiseNoLive -SimCruiseShellSelfTest -windowed -ResX=1280 -ResY=720 -NoLiveCoding -nosplash -unattended -log `"-abslog=$PadLog`"" -PassThru
            if (-not $P.WaitForExit(15 * 60 * 1000)) { Stop-ProcessTree -ProcessId $P.Id }
        } finally { Exit-EditorLock }
    }
    Step "part 6 driving pad pass: $(if (Test-Path $PadLog) { (Select-String $PadLog -Pattern 'SIMCRUISE SHELL SELF-TEST (PASSED|FAILED).*' | Select-Object -Last 1).Matches.Value } else { 'no log' })"
    # 7. Post-processing of every run with a video: photo sheets, clips, strips, video scan, blind folders.
    Step 'part 7: post-processing'
    $Dirs = @(@($Run.FullName, (Join-Path $Out 'bridges'), (Join-Path $Out 'civil'), (Join-Path $Out 'drive-real'), (Join-Path $Out 'slow-look')) | Where-Object { $_ -and (Test-Path $_) } | ForEach-Object { Get-ChildItem $_ -Directory } | Where-Object { Test-Path (Join-Path $_.FullName 'video.mp4') } | ForEach-Object FullName)
    & powershell -ExecutionPolicy Bypass -File (Join-Path $S 'detail-review.ps1') -PostOnly -OutRoot (Join-Path $Out 'review') -RunDirs ($Dirs -join ',') | Select-Object -Last 2
    Step "editor part done: $($Dirs.Count) runs; review in $(Join-Path $Out 'review') (parts 8 and 12 are people's work)"
}

if (-not $SkipCooked) {
    New-Item -ItemType Directory -Force $Out | Out-Null
    $Log = Join-Path $Out 'final-test-steps.log'
    function Step([string]$What) { $L = "$((Get-Date).ToUniversalTime().ToString('HH:mm'))Z $What"; Write-Host $L; Add-Content -Encoding utf8 $Log $L }
    # The package is the candidate plus only the picks named here (the installer lane's scripts, .iss and config):
    # no pick may touch game code (Source, Content).
    foreach ($C in $PackagePicks) {
        $Code = @(git -C $PackageTree show --name-only --format= $C | Where-Object { $_ -match '^(Source|Content)/' })
        if ($Code.Count) { throw "package pick $C touches game code: $($Code -join ', ')" }
    }
    Prepare $PackageTree $PackagePicks
    Step "cooked part: $PackageTree at $(git -C $PackageTree log --oneline -1) (picks: $($PackagePicks -join ', '))"
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\gpu-wait.ps1') -TimeoutMinutes 120
    $Iss = Join-Path $PackageTree 'packaging\windows\simcruise.iss'
    # 9. The Development package (it writes a log) for the cooked gate and the timed run.
    Step 'part 9: package Development'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\package.ps1') -Configuration Development | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw 'package failed' }
    Push-Location $PackageTree; try { if (git status --porcelain) { throw 'the package tree changed during the package build' } } finally { Pop-Location }
    # The static dependency check: every DLL the shipped binaries need is shipped, part of Windows, or installed
    # by the installer (the VC++ runtime only with vc_redist.x64.exe in the package and the .iss running it).
    & python (Join-Path $PSScriptRoot 'dep-check.py') (Join-Path $PackageTree 'Saved\Package\Windows') (Join-Path $Out 'dependencies-development.md') "--iss=$Iss" | ForEach-Object { Step "dep-check: $_" }
    # 10. The cooked gate: Dragon and Ocean Drive in the cooked game, the shader and name checks.
    Step 'part 10: cooked gate'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\release-test.ps1') -Cooked -ShowWindow -Cases 'cooked-drive' | Select-Object -Last 5
    # 11. One timed quiet run: the cooked Dragon drive again, its report must say quiet (no other load).
    Step 'part 11: timed quiet run'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\release-test.ps1') -Cooked -ShowWindow -Places 'dragon' -NoWarm | Select-Object -Last 5
    # 13. The installer gate, as far as this PC allows: the Shipping package with the Microsoft redistributables,
    # dep-check, the installer and its SHA256. Installing, uninstalling and the clean-PC steps of
    # docs/release/clean-pc-test.md need a clean PC or Windows Sandbox (admin); on this PC the uninstaller would also
    # delete the shared map cache in %LOCALAPPDATA%\SimCruise, so they are not run here.
    Step 'part 13: Shipping package and installer'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\package.ps1') -Configuration Shipping -Zip | Select-Object -Last 3
    & python (Join-Path $PSScriptRoot 'dep-check.py') (Join-Path $PackageTree 'Saved\Package\Windows') (Join-Path $Out 'dependencies-shipping.md') "--iss=$Iss" | ForEach-Object { Step "dep-check (Shipping): $_" }
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PackageTree 'scripts\installer.ps1') | Select-Object -Last 4 | ForEach-Object { Step "installer: $_" }
    $Setup = Get-ChildItem (Join-Path $PackageTree 'Saved\Package') -Filter '*-windows-x64-setup.exe' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $Sums = Get-ChildItem (Join-Path $PackageTree 'Saved\Package') -Filter '*-SHA256SUMS.txt' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($Setup -and $Sums) {
        $H = (Get-FileHash $Setup.FullName -Algorithm SHA256).Hash.ToLower()
        Step "installer $($Setup.Name) $([Math]::Round($Setup.Length / 1MB)) MB; SHA256 $(if ((Get-Content $Sums.FullName) -match $H) { 'matches' } else { 'DOES NOT MATCH' }) $($Sums.Name)"
    } else { Step 'installer: no setup exe or SHA256SUMS written' }
    Step 'part 13 not run on this PC (needs a clean PC or Windows Sandbox, admin): install, the Microsoft runtime prompts, first start, offline start, uninstall and what it leaves'
}
Write-Host "FINAL TEST of $Hash done $((Get-Date).ToUniversalTime().ToString('HH:mm'))Z"
