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
