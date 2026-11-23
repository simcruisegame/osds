<#
.SYNOPSIS
  Self-tests of the script helpers that need no Unreal (scripts\check.ps1 runs them first).

.DESCRIPTION
  Covers the low-impact rule (builds and tests start BelowNormal with fewer UBT actions while a GPU
  test runs, nothing changes otherwise), the test window size of Preflight (1920x1080 by default,
  'native' on request), and that preflight.ps1 and check.ps1 use them. Exit 0 when all pass.
#>
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
$Failed = 0
function Assert-That([bool]$Condition, [string]$What) {
    if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What"; $script:Failed++ }
}

Write-Host 'Script helper tests'

# Low impact while a GPU test runs.
$env:OSDS_THROTTLE_ACTIONS = ''
$Idle = Get-LowImpactSettings -GpuTestRunning $false -Cores 8
Assert-That (-not $Idle.Throttle -and $Idle.Priority -eq 'Normal' -and $Idle.MaxParallelActions -eq 0) 'no GPU test: nothing changes'
$Busy = Get-LowImpactSettings -GpuTestRunning $true -Cores 8
Assert-That ($Busy.Throttle -and $Busy.Priority -eq 'BelowNormal' -and $Busy.MaxParallelActions -eq 4) 'GPU test on 8 cores: BelowNormal, 4 actions'
Assert-That ((Get-LowImpactSettings -GpuTestRunning $true -Cores 2).MaxParallelActions -eq 2) 'never fewer than 2 actions'
$env:OSDS_THROTTLE_ACTIONS = '3'
Assert-That ((Get-LowImpactSettings -GpuTestRunning $true -Cores 8).MaxParallelActions -eq 3) 'OSDS_THROTTLE_ACTIONS overrides the count'
$env:OSDS_THROTTLE_ACTIONS = ''

# The lock probe sees a held lock and lets go of a free one.
$Name = $script:GpuTestLockName
$script:GpuTestLockName = "Global\OsdsGpuTestSelfTest$PID"
Assert-That (-not (Test-GpuTestLockHeld)) 'a lock nobody created is not held'
$Job = Start-Job -ArgumentList $script:GpuTestLockName -ScriptBlock {
    param($N)
    $M = New-Object System.Threading.Mutex($false, $N)
    [void]$M.WaitOne()
    Start-Sleep -Seconds 8
}
Start-Sleep -Seconds 2
Assert-That (Test-GpuTestLockHeld) 'a lock another process holds is held'
Stop-Job $Job; Remove-Job $Job -Force
Start-Sleep -Milliseconds 500
Assert-That (-not (Test-GpuTestLockHeld)) 'a lock whose holder ended is free'
$script:GpuTestLockName = $Name

# The build lock: held -> waits and names the holder; holder ended -> proceeds (issue #142).
$BName = "Global\OsdsBuildSelfTest$PID"
$BHolder = Join-Path ([IO.Path]::GetTempPath()) "osds-build-lock-$PID.txt"
Remove-Item $BHolder -ErrorAction SilentlyContinue
$Free = Enter-BuildLock -Name $BName -HolderFile $BHolder -PollSeconds 1 -TimeoutMinutes 1 -Why 'self-test'
Assert-That $Free 'a free build lock is taken at once'
Exit-BuildLock $BHolder
$Holder = Start-Job -ArgumentList $BName, $BHolder -ScriptBlock {
    param($N, $H)
    $M = New-Object System.Threading.Mutex($false, $N)
    [void]$M.WaitOne()
    "pid $PID, other session, since now" | Set-Content $H
    Start-Sleep -Seconds 6
}
Start-Sleep -Seconds 2
$Waited = Get-Date
$Out = Enter-BuildLock -Name $BName -HolderFile $BHolder -PollSeconds 1 -TimeoutMinutes 0.05 -Why 'self-test' 6>&1 | Out-String
Assert-That ($Out -match 'Waiting for build lock held by pid \d+, other session') 'a held build lock: waits and names the holder'
Assert-That ($Out -match 'Gave up' -and -not $script:BuildLock) 'a build lock that stays held is given up, not taken'
$Out2 = Enter-BuildLock -Name $BName -HolderFile $BHolder -PollSeconds 1 -TimeoutMinutes 1 -Why 'self-test' 6>&1 | Out-String
Assert-That ($Out2 -match 'Waiting for build lock' -and $Out2 -match 'Build lock taken' -and $script:BuildLock) 'holder ended: the waiting build proceeds'
Exit-BuildLock $BHolder
Assert-That ((Get-BuildLockHolder $BHolder) -eq 'unknown') 'a released build lock names nobody'
Stop-Job $Holder -ErrorAction SilentlyContinue; Remove-Job $Holder -Force -ErrorAction SilentlyContinue
Remove-Item $BHolder -ErrorAction SilentlyContinue
Assert-That (Test-BuildRefusedByOtherBuild @('Build.bat is already running, waiting for existing script to terminate...') 4) 'exit 4 with "already running" is a queue case'
Assert-That (-not (Test-BuildRefusedByOtherBuild @('error C2065: undeclared identifier') 6)) 'a compile error is a real failure'
Assert-That (-not (Test-BuildRefusedByOtherBuild @('already running') 0)) 'a successful build is never retried'

# A binary older than Source\ is stale (Preflight refuses it).
$Tmp = Join-Path ([IO.Path]::GetTempPath()) "osds-stale-$PID"
New-Item -ItemType Directory -Force (Join-Path $Tmp 'Source\OSDS'), (Join-Path $Tmp 'Binaries\Win64') | Out-Null
$Dll = Join-Path $Tmp 'Binaries\Win64\UnrealEditor-OSDS.dll'
$Cpp = Join-Path $Tmp 'Source\OSDS\A.cpp'
Set-Content $Cpp 'x'; Set-Content $Dll 'x'
(Get-Item $Cpp).LastWriteTimeUtc = [datetime]::UtcNow.AddMinutes(-10)
(Get-Item $Dll).LastWriteTimeUtc = [datetime]::UtcNow.AddMinutes(-5)
Assert-That (-not (Get-StaleBinary -Root $Tmp).Stale) 'a binary newer than Source is current'
(Get-Item $Cpp).LastWriteTimeUtc = [datetime]::UtcNow.AddMinutes(-1)
$St = Get-StaleBinary -Root $Tmp
Assert-That ($St.Stale -and $St.Reason -match 'older than' -and $St.Newest -eq $Cpp) 'a source file newer than the binary makes it stale'
Remove-Item $Dll
Assert-That ((Get-StaleBinary -Root $Tmp).Stale) 'a missing binary is stale'
Remove-Item $Tmp -Recurse -Force

# Preflight's window size.
$Native = @(Get-NativeResolution)
$Default = @(Get-TestResolution)
Assert-That ($Default[0] -eq '-ResX=1920' -and $Default[1] -eq '-ResY=1080') 'default window is 1920x1080'
Assert-That ($Default[2] -eq $Native[2]) 'the screen is still named for the review (-OsdsNativeRes)'
Assert-That ($Default -contains '-OsdsWindowRes=1920x1080') 'a visible run is asked for a 1920x1080 window'
Assert-That ((@(Get-TestResolution 'native') -join ' ') -eq ($Native -join ' ')) "'native' is the screen's own size"
Assert-That ((@(Get-TestResolution '1280x720'))[0] -eq '-ResX=1280') 'a chosen size is used'
$Threw = $false
try { Get-TestResolution 'huge' | Out-Null } catch { $Threw = $true }
Assert-That $Threw 'a bad size is refused'

# The scripts use them.
$Pre = Get-Content (Join-Path $PSScriptRoot 'preflight.ps1') -Raw
Assert-That ($Pre -match "\[string\]\`$Resolution = '1920x1080'") 'preflight.ps1 defaults -Resolution to 1920x1080'
Assert-That ($Pre -match 'Get-TestResolution \$Resolution' -and $Pre -notmatch '@\(Get-NativeResolution\)') 'preflight.ps1 uses Get-TestResolution, not the native size'
Assert-That ($Pre -match 'timed_frames') 'preflight.ps1 fails a run that timed no frames'
$Check = Get-Content (Join-Path $PSScriptRoot 'check.ps1') -Raw
Assert-That ($Check -match 'Enter-BuildLock' -and $Check -match 'Exit-BuildLock') 'check.ps1 builds under the build lock'
Assert-That ($Pre -match 'Get-StaleBinary' -and $Pre -match 'AllowStaleBinary') 'preflight.ps1 refuses a binary older than Source'
Assert-That ($Check -match 'Test-GpuTestLockHeld' -and $Check -match '-MaxParallelActions=' -and $Check -match 'PriorityClass') 'check.ps1 throttles while a GPU test runs'

if ($Failed -gt 0) { Write-Host "$Failed helper test(s) failed."; exit 1 }
Write-Host 'Script helper tests passed.'
exit 0
