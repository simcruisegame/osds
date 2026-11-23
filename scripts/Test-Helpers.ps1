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

# The build lock: held -> waits and names the holder at once; holder ended -> proceeds (issue #142).
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
    Start-Sleep -Seconds 7
}
Start-Sleep -Seconds 2
# 0.01 min = 0.6 s: a fractional timeout must not round down to "wait forever" or to zero tries.
$Out = Enter-BuildLock -Name $BName -HolderFile $BHolder -PollSeconds 0.3 -TimeoutMinutes 0.01 -Why 'self-test' 6>&1 | Out-String
Assert-That ($Out -match 'Waiting for build lock held by pid \d+, other session') 'a held build lock: says so at once and names the holder'
Assert-That ($Out -match 'Gave up' -and -not $script:BuildLock) 'a build lock that stays held is given up after a fractional-minute timeout, not taken'
$Out2 = Enter-BuildLock -Name $BName -HolderFile $BHolder -PollSeconds 1 -TimeoutMinutes 1 -Why 'self-test' 6>&1 | Out-String
Assert-That ($Out2 -match 'Waiting for build lock' -and $Out2 -match 'Build lock taken' -and $script:BuildLock) 'holder ended: the waiting build proceeds'
Exit-BuildLock $BHolder
Assert-That ((Get-BuildLockHolder $BHolder) -eq 'unknown') 'a released build lock names nobody'
Stop-Job $Holder -ErrorAction SilentlyContinue; Remove-Job $Holder -Force -ErrorAction SilentlyContinue
Remove-Item $BHolder -ErrorAction SilentlyContinue

# A holder note another process has open must not stop a lock being taken or released (a preflight died on this).
$Locked = Join-Path ([IO.Path]::GetTempPath()) "osds-locked-note-$PID.txt"
Set-Content $Locked 'pid 1, other'
$Stream = [System.IO.File]::Open($Locked, 'Open', 'ReadWrite', 'None')
try {
    $script:BuildLockName = "Global\OsdsBuildSelfTestC$PID"
    $Ok = Enter-BuildLock -Name $script:BuildLockName -HolderFile $Locked -PollSeconds 1 -TimeoutMinutes 1 -Why 'self-test' 6>&1 | Out-String
    Assert-That ($Ok -match 'Build lock taken' -and $Ok -match 'Could not write the lock note' -and $script:BuildLock) 'a holder note held open (FileShare.None): the build lock is still taken, with a warning'
    Exit-BuildLock $Locked
    Assert-That (-not $script:BuildLock) 'and released'
    Assert-That ((Get-BuildLockHolder $Locked) -eq 'unknown') 'an unreadable holder note reads as unknown'
}
finally { $Stream.Dispose() }
Write-LockNote $Locked 'pid 2, after'
Assert-That ((Get-BuildLockHolder $Locked) -eq 'pid 2, after') 'a holder note is written and read once free'
Remove-Item $Locked -ErrorAction SilentlyContinue

# Only exit 4 with the "already running" line is a queue case; any other failure is real.
$Running = @('Build.bat is already running, waiting for existing script to terminate...')
Assert-That (Test-BuildRefusedByOtherBuild $Running 4) 'exit 4 with "already running" is a queue case'
Assert-That (-not (Test-BuildRefusedByOtherBuild ($Running + 'error C2065: undeclared identifier') 6)) 'a compile error that also prints "already running" is a real failure'
Assert-That (-not (Test-BuildRefusedByOtherBuild $Running 1)) '"already running" with exit 1 is a real failure'
Assert-That (-not (Test-BuildRefusedByOtherBuild @('error C2065') 4)) 'exit 4 without "already running" is a real failure'
Assert-That (-not (Test-BuildRefusedByOtherBuild $Running 0)) 'a successful build is never retried'

# Invoke-LockedBuild with a fake Build.bat: retries the queue case, returns a real failure at once.
$Fake = Join-Path ([IO.Path]::GetTempPath()) "osds-fake-build-$PID.cmd"
$Count = Join-Path ([IO.Path]::GetTempPath()) "osds-fake-build-$PID.count"
Remove-Item $Count, "$Count.seen" -ErrorAction SilentlyContinue
Set-Content -Encoding ascii $Fake @"
@echo off
echo x>>"$Count"
if not "%1"=="queue" ( echo error C2065: undeclared identifier & exit /b 6 )
if exist "$Count.seen" exit /b 0
echo seen>"$Count.seen"
echo Build.bat is already running, waiting for existing script to terminate...
exit /b 4
"@
$script:BuildLockName = "Global\OsdsBuildSelfTestB$PID"; $script:BuildLockHolderFile = $BHolder
$Code = Invoke-LockedBuild -BuildBat $Fake -BuildArgs @('queue') -Why 'self-test' -TimeoutMinutes 1 -RetrySeconds 0.2 6>&1 | Where-Object { $_ -is [int] }
Assert-That ($Code -eq 0 -and (@(Get-Content $Count).Count -eq 2)) 'a build refused once by another Build.bat is tried again and passes'
Remove-Item $Count -ErrorAction SilentlyContinue
$Code = Invoke-LockedBuild -BuildBat $Fake -BuildArgs @('fail') -Why 'self-test' -TimeoutMinutes 1 -RetrySeconds 0.2 6>&1 | Where-Object { $_ -is [int] }
Assert-That ($Code -eq 6 -and (@(Get-Content $Count).Count -eq 1) -and -not $script:BuildLock) 'a real build failure is returned at once, once, and frees the lock'
Remove-Item $Fake, $Count, "$Count.seen", $BHolder -ErrorAction SilentlyContinue

# A binary older than the compiled sources is stale (Preflight refuses it); other files do not count.
$Tmp = Join-Path ([IO.Path]::GetTempPath()) "osds-stale-$PID"
New-Item -ItemType Directory -Force (Join-Path $Tmp 'Source\OSDS'), (Join-Path $Tmp 'Binaries\Win64') | Out-Null
$Dll = Join-Path $Tmp 'Binaries\Win64\UnrealEditor-OSDS.dll'
$Cpp = Join-Path $Tmp 'Source\OSDS\A.cpp'
$Doc = Join-Path $Tmp 'Source\OSDS\notes.md'
Set-Content $Cpp 'x'; Set-Content $Dll 'x'; Set-Content $Doc 'x'
(Get-Item $Cpp).LastWriteTimeUtc = [datetime]::UtcNow.AddMinutes(-10)
(Get-Item $Dll).LastWriteTimeUtc = [datetime]::UtcNow.AddMinutes(-5)
(Get-Item $Doc).LastWriteTimeUtc = [datetime]::UtcNow.AddMinutes(-20)
Assert-That (-not (Get-StaleBinary -Root $Tmp).Stale) 'a binary newer than Source is current'
(Get-Item $Doc).LastWriteTimeUtc = [datetime]::UtcNow.AddMinutes(-1)
Assert-That (-not (Get-StaleBinary -Root $Tmp).Stale) 'a newer .md under Source does not make the binary stale'
$Json = Join-Path $Tmp 'Source\OSDS\data.json'; Set-Content $Json 'x'
Assert-That (-not (Get-StaleBinary -Root $Tmp).Stale) 'a newer .json under Source does not make the binary stale'
(Get-Item $Cpp).LastWriteTimeUtc = [datetime]::UtcNow.AddMinutes(-1)
$St = Get-StaleBinary -Root $Tmp
Assert-That ($St.Stale -and $St.Newest -eq $Cpp -and $St.Reason -match 'A\.cpp' -and $St.Reason -match 'UnrealEditor-OSDS\.dll') 'a .cpp newer than the binary makes it stale, and the reason names both'
(Get-Item $Cpp).LastWriteTimeUtc = [datetime]::UtcNow.AddMinutes(-10)
$Build = Join-Path $Tmp 'Source\OSDS\OSDS.Build.cs'; Set-Content $Build 'x'
Assert-That ((Get-StaleBinary -Root $Tmp).Stale) 'a newer .Build.cs makes it stale'
Remove-Item $Build
Remove-Item $Dll
Assert-That ((Get-StaleBinary -Root $Tmp).Stale) 'a missing binary is stale'
Set-Content $Dll 'x'
Remove-Item (Join-Path $Tmp 'Source') -Recurse -Force
$Gone = Get-StaleBinary -Root $Tmp
Assert-That ($Gone.Stale -and $Gone.Reason -match 'cannot read') 'an unreadable Source folder fails closed with a message'
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
Assert-That ($Check -match 'Test-GpuTestLockHeld' -and $Check -match '-MaxParallelActions=' -and $Check -match 'PriorityClass') 'check.ps1 throttles while a GPU test runs'

if ($Failed -gt 0) { Write-Host "$Failed helper test(s) failed."; exit 1 }
Write-Host 'Script helper tests passed.'
exit 0
