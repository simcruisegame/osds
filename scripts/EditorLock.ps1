<#
.SYNOPSIS
  One GPU Unreal Editor at a time on this PC, across every checkout and worktree,
  and a few headless test editors beside it.

.DESCRIPTION
  Dot-source this file, then wrap an editor run in Enter-EditorLock / Exit-EditorLock.
  The lock is a named Windows mutex, so Windows frees it when the process that holds
  it ends, even after a crash or a kill. Two editors at once fight over the GPU and
  over the MCP port (8000), so play tests, -game runs, captures and MCP sessions take it.
  Headless -nullrhi test runs (scripts\check.ps1) take a slot instead
  (Enter-HeadlessSlot / Exit-HeadlessSlot): they start without the MCP plugins, so they
  can run next to the GPU editor and next to each other.
#>

$script:EditorLockName = 'Global\OSDSEditor'
$script:EditorLock = $null
# Every script that starts a game for a picture or driving test also takes the GPU test lock
# (-GpuTest), before the editor lock: one GPU test at a time across all lanes, and the tests
# queue in the order they asked.
$script:GpuTestLockName = 'Global\OsdsGpuTest'
$script:GpuTestLock = $null

<#
  The arguments that keep a test game off the desktop: -RenderOffScreen (no window, no swap chain,
  no focus taken, Slate and the renderer still draw; the game's own 60 fps cap still paces it).
  -ShowWindow in the calling script gives the window back.
#>
function Get-TestWindowArgs([bool]$ShowWindow) {
    # A visible run: -FULLSCREEN is a borderless window at the screen's size in Unreal 5 (r.FullScreenMode 1,
    # windowed fullscreen, the default; -OsdsBorderless also sets it in builds that have it),
    # so a focus change cannot minimise it or change the display mode.
    if ($ShowWindow) { return @('-FULLSCREEN', '-OsdsBorderless') }
    return @('-RenderOffScreen')
}

# The one wait loop for a named lock (the GPU test lock and the build lock use it): try once at
# once, and if it is held say so right away, naming the holder, then keep trying every
# -PollSeconds until -TimeoutMinutes. A holder that ended without releasing (abandoned) hands it over.
function Wait-LockWithHolder {
    param(
        [Parameter(Mandatory)][System.Threading.Mutex]$Mutex, [string]$Label, [string]$Why = '',
        [string]$HolderFile, [double]$TimeoutMinutes = 60, [double]$PollSeconds = 30
    )
    $Start = Get-Date
    $PollSpan = [TimeSpan]::FromSeconds($PollSeconds)
    $Wait = [TimeSpan]::Zero
    while ($true) {
        $Taken = $false
        try { $Taken = $Mutex.WaitOne($Wait) }
        catch [System.Threading.AbandonedMutexException] { $Taken = $true }
        if ($Taken) { return $true }
        $Wait = $PollSpan
        $Waited = [int]((Get-Date) - $Start).TotalMinutes
        $Who = 'unknown'
        try { if ($HolderFile -and (Test-Path $HolderFile)) { $T = (Get-Content $HolderFile -Raw).Trim(); if ($T) { $Who = $T } } } catch { }
        Write-Host "Waiting for $Label held by $Who ($Why): $Waited min so far"
        if (((Get-Date) - $Start).TotalMinutes -ge $TimeoutMinutes) { return $false }
    }
}

function Enter-EditorLock {
    param([int]$TimeoutMinutes = 60, [string]$Why = 'editor run', [switch]$GpuTest)
    if ($GpuTest) {
        $script:GpuTestLock = New-Object System.Threading.Mutex($false, $script:GpuTestLockName)
        # Who holds the lock, for the people waiting (the mutex itself does not say).
        $Holder = Join-Path $env:LOCALAPPDATA 'OSDS\gpu-test-lock.txt'
        $Start = Get-Date
        if (-not (Wait-LockWithHolder -Mutex $script:GpuTestLock -Label 'the GPU test lock' -Why $Why -HolderFile $Holder -TimeoutMinutes $TimeoutMinutes)) {
            Write-Host "Another GPU test held the lock for $TimeoutMinutes min. Gave up."
            $script:GpuTestLock.Dispose(); $script:GpuTestLock = $null
            return $false
        }
        New-Item -ItemType Directory -Force (Split-Path $Holder) | Out-Null
        "$Why, since $((Get-Date).ToString('HH:mm:ss')), pid $PID" | Set-Content -Encoding utf8 $Holder
        Write-Host "GPU test lock taken after $([int]((Get-Date) - $Start).TotalSeconds) s."
    }
    $script:EditorLock = New-Object System.Threading.Mutex($false, $script:EditorLockName)
    Write-Host "Waiting for the editor lock ($Why)..."
    try {
        if (-not $script:EditorLock.WaitOne([TimeSpan]::FromMinutes($TimeoutMinutes))) {
            Write-Host "Another OSDS editor run held the lock for $TimeoutMinutes min. Gave up."
            Exit-EditorLock
            return $false
        }
    }
    catch [System.Threading.AbandonedMutexException] {
        # The last holder ended without releasing it; the lock is now ours.
    }
    Write-Host 'Editor lock taken.'
    return $true
}

function Exit-EditorLock {
    if ($script:EditorLock) {
        try { $script:EditorLock.ReleaseMutex() } catch { }
        $script:EditorLock.Dispose()
        $script:EditorLock = $null
    }
    if ($script:GpuTestLock) {
        try { $script:GpuTestLock.ReleaseMutex() } catch { }
        $script:GpuTestLock.Dispose()
        $script:GpuTestLock = $null
        # Free: the holder file names nobody (load.csv logs the holder).
        $Holder = Join-Path $env:LOCALAPPDATA 'OSDS\gpu-test-lock.txt'
        try { Set-Content -Encoding utf8 $Holder '' } catch { }
    }
}

# Headless (-nullrhi) test runs use neither the GPU nor MCP port 8000, so they do not take
# the one-editor lock. They take one of a few slots instead, so several worktrees can test at
# once without running the PC out of memory. Each slot is its own named mutex
# (Global\OsdsHeadless0, 1, ...), not one semaphore: Windows frees a mutex when its
# holder dies, while a semaphore count taken by a killed script would stay taken.
$script:HeadlessSlot = $null
$script:HeadlessSlotMax = 3

function Get-HeadlessSlotCount {
    # How many headless test editors may run at once: -Slots, else $env:OSDS_TEST_SLOTS,
    # else what the free memory allows (keeping ReserveGB free; at least 2 with 20 GB free), from 1 to 3.
    # EditorPeakGB: a full -nullrhi test run peaked at 6.1 GB (working set, 2026-11-12).
    param([int]$Requested = 0, [double]$EditorPeakGB = 6.5, [double]$ReserveGB = 8)
    if ($Requested -le 0 -and $env:OSDS_TEST_SLOTS) { $Requested = [int]$env:OSDS_TEST_SLOTS }
    if ($Requested -gt 0) { return [int][math]::Min([math]::Max($Requested, 1), $script:HeadlessSlotMax) }
    try { $FreeGB = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB } catch { return 1 }
    $N = [math]::Floor(($FreeGB - $ReserveGB) / $EditorPeakGB)
    # Two full test editors (about 6 GB each) fit side by side with 20 GB free.
    if ($FreeGB -ge 20) { $N = [math]::Max($N, 2) }
    return [int][math]::Min([math]::Max($N, 1), $script:HeadlessSlotMax)
}

function Enter-HeadlessSlot {
    param([int]$Slots = 1, [int]$TimeoutMinutes = 60, [string]$Why = 'headless test run')
    $Deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    Write-Host "Waiting for a headless test slot (one of $Slots; $Why)..."
    while ($true) {
        for ($I = 0; $I -lt $Slots; $I++) {
            $M = New-Object System.Threading.Mutex($false, "Global\OsdsHeadless$I")
            $Taken = $false
            try { $Taken = $M.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $Taken = $true }
            if ($Taken) {
                $script:HeadlessSlot = $M
                Write-Host "Headless test slot $I taken."
                return $true
            }
            $M.Dispose()
        }
        if ((Get-Date) -gt $Deadline) {
            Write-Host "All $Slots headless test slots stayed busy for $TimeoutMinutes min. Gave up."
            return $false
        }
        Start-Sleep -Seconds 2
    }
}

function Exit-HeadlessSlot {
    if ($script:HeadlessSlot) {
        try { $script:HeadlessSlot.ReleaseMutex() } catch { }
        $script:HeadlessSlot.Dispose()
        $script:HeadlessSlot = $null
    }
}

# One Unreal build at a time on this PC (issue #142). Unreal Build Tool has its own mutex, but
# a second Build.bat can still give up with "Build.bat is already running" (exit 4). The scripts
# that run Build.bat (check.ps1, realism.ps1, playtest.ps1) do it through Invoke-LockedBuild, which
# takes this lock first and queues behind the holder, with a line that names it.
# A named Windows mutex, so a crashed or killed holder frees it. The holder file is for the
# people waiting (the mutex itself does not say who has it).
$script:BuildLockName = 'Global\OsdsBuild'
$script:BuildLockHolderFile = Join-Path $env:LOCALAPPDATA 'OSDS\build-lock.txt'
$script:BuildLock = $null

function Get-BuildLockHolder([string]$HolderFile = $script:BuildLockHolderFile) {
    # The holder's pid and a note, or 'unknown'.
    try {
        if (Test-Path $HolderFile) {
            $T = (Get-Content $HolderFile -Raw).Trim()
            if ($T) { return $T }
        }
    } catch { }
    return 'unknown'
}

function Enter-BuildLock {
    param(
        [double]$TimeoutMinutes = 60, [string]$Why = 'build', [double]$PollSeconds = 30,
        [string]$Name = $script:BuildLockName, [string]$HolderFile = $script:BuildLockHolderFile
    )
    $Mutex = New-Object System.Threading.Mutex($false, $Name)
    $Start = Get-Date
    if (-not (Wait-LockWithHolder -Mutex $Mutex -Label 'build lock' -Why $Why -HolderFile $HolderFile -TimeoutMinutes $TimeoutMinutes -PollSeconds $PollSeconds)) {
        Write-Host "Another build held the lock for $TimeoutMinutes min. Gave up."
        $Mutex.Dispose()
        return $false
    }
    $script:BuildLock = $Mutex
    try {
        New-Item -ItemType Directory -Force (Split-Path $HolderFile) | Out-Null
        "pid $PID, $Why, since $((Get-Date).ToString('HH:mm:ss'))" | Set-Content -Encoding utf8 $HolderFile
    } catch { }
    Write-Host "Build lock taken after $([int]((Get-Date) - $Start).TotalSeconds) s."
    return $true
}

function Exit-BuildLock([string]$HolderFile = $script:BuildLockHolderFile) {
    if ($script:BuildLock) {
        try { Set-Content -Encoding utf8 $HolderFile '' } catch { }
        try { $script:BuildLock.ReleaseMutex() } catch { }
        $script:BuildLock.Dispose()
        $script:BuildLock = $null
    }
}

# Did Build.bat give up only because another build was running (exit 4 and its "already running"
# line)? Any other failure, a compile error included, is a real failure and is never retried.
function Test-BuildRefusedByOtherBuild([string[]]$Output, [int]$ExitCode) {
    if ($ExitCode -ne 4) { return $false }
    return [bool](@($Output) -match 'already running')
}

<#
  Runs Build.bat under the build lock; returns its exit code (1 when the lock timed out).
  -AfterLock runs once the lock is ours (so a GPU-test throttle is read after the wait, not before)
  and returns extra Build.bat arguments. A build another tool started outside the scripts can still
  make Build.bat answer exit 4 "already running": that waits 30 s and tries again until the timeout.
  Any other failure is returned at once.
#>
function Invoke-LockedBuild {
    param(
        [Parameter(Mandatory)][string]$BuildBat, [string[]]$BuildArgs = @(), [string]$Why = 'build',
        [double]$TimeoutMinutes = 60, [scriptblock]$AfterLock, [double]$RetrySeconds = 30
    )
    if (-not (Enter-BuildLock -TimeoutMinutes $TimeoutMinutes -Why $Why)) { return 1 }
    try {
        $Extra = if ($AfterLock) { @(& $AfterLock) } else { @() }
        $Deadline = (Get-Date).AddMinutes($TimeoutMinutes)
        while ($true) {
            Write-Host 'Building OSDSEditor (Win64 Development)...'
            $Out = @()
            & $BuildBat @BuildArgs @Extra | ForEach-Object { Write-Host $_; $Out += [string]$_ }
            $Code = $LASTEXITCODE
            if ((Test-BuildRefusedByOtherBuild $Out $Code) -and (Get-Date) -lt $Deadline) {
                Write-Host "Waiting for build lock held by another Build.bat (exit $Code); trying again in $RetrySeconds s."
                Start-Sleep -Seconds $RetrySeconds
                continue
            }
            return $Code
        }
    }
    finally { Exit-BuildLock }
}

<#
  A run on a binary older than the code is a run on the wrong game (twice a refused build let a
  drive measure the old binary). Stale = the editor module is missing, or a file that gets compiled
  (*.cpp, *.h, *.inl, *.cs, which includes *.Build.cs) under Source\ was written after it (2 s of slack
  for file-system time rounding). Notes, data and other files do not count: Unreal Build Tool would
  not relink for them. A source tree that cannot be read counts as stale (fail closed).
#>
function Get-StaleBinary {
    param([string]$Root, [string]$Binary = (Join-Path $Root 'Binaries\Win64\UnrealEditor-OSDS.dll'))
    if (-not (Test-Path $Binary)) {
        return [pscustomobject]@{ Stale = $true; Reason = "no binary at $Binary"; Newest = $null }
    }
    $BinTime = (Get-Item $Binary).LastWriteTimeUtc
    $Fmt = 'yyyy-MM-dd HH:mm:ss'
    try {
        $Newest = Get-ChildItem (Join-Path $Root 'Source') -Recurse -File -Include '*.cpp', '*.h', '*.inl', '*.cs' -ErrorAction Stop |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    }
    catch {
        return [pscustomobject]@{ Stale = $true; Newest = $null; Reason = "cannot read $(Join-Path $Root 'Source') to compare it with the binary ($($_.Exception.Message))" }
    }
    if ($Newest -and $Newest.LastWriteTimeUtc -gt $BinTime.AddSeconds(2)) {
        return [pscustomobject]@{
            Stale = $true; Newest = $Newest.FullName
            Reason = "newest source $($Newest.FullName) was written $($Newest.LastWriteTimeUtc.ToLocalTime().ToString($Fmt)), the binary $Binary $($BinTime.ToLocalTime().ToString($Fmt))"
        }
    }
    return [pscustomobject]@{ Stale = $false; Reason = ''; Newest = if ($Newest) { $Newest.FullName } else { $null } }
}

function Stop-ProcessTree {
    param([int]$ProcessId)
    & taskkill.exe /T /F /PID $ProcessId 2>&1 | Out-Null
}

<#
  C:\aiwork\GPU-BUSY: one line that tells the other lanes this lane uses the GPU, and until when
  (holder=lane-view, start=<UTC>, end_estimate=<UTC>, purpose=<text>). Set it before a GPU slot
  (a build or a game launch) and clear it in a finally block. scripts\gpu-wait.ps1 waits on it.
#>
$script:GpuBusyFile = 'C:\aiwork\GPU-BUSY'
function Set-GpuBusy([string]$Purpose, [double]$Minutes, [string]$Holder = 'lane-view') {
    try {
        New-Item -ItemType Directory -Force (Split-Path $script:GpuBusyFile) | Out-Null
        $Now = (Get-Date).ToUniversalTime()
        "holder=$Holder, start=$($Now.ToString('yyyy-MM-ddTHH:mm:ssZ')), end_estimate=$($Now.AddMinutes($Minutes).ToString('yyyy-MM-ddTHH:mm:ssZ')), purpose=$($Purpose -replace '[\r\n,]+', ' ')" |
            Set-Content -Encoding ascii $script:GpuBusyFile
    } catch { Write-Host "Could not write $($script:GpuBusyFile): $_" }
}
function Clear-GpuBusy([string]$Holder = 'lane-view') {
    # Only our own line: another lane may hold it now.
    if ((Test-Path $script:GpuBusyFile) -and (Get-Content $script:GpuBusyFile -Raw) -match "holder=$Holder,") { Remove-Item $script:GpuBusyFile -Force -ErrorAction SilentlyContinue }
}
function Read-GpuBusy {
    if (-not (Test-Path $script:GpuBusyFile)) { return $null }
    $T = Get-Content $script:GpuBusyFile -Raw
    $End = if ($T -match 'end_estimate=(\S+?),') { [datetime]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AdjustToUniversal) } else { $null }
    return [pscustomobject]@{ Text = $T.Trim(); End = $End; Stale = ($End -and (Get-Date).ToUniversalTime() -gt $End.AddMinutes(30)) }
}

<#
  Is a GPU test (a visible or off-screen game that took -GpuTest) running right now? True while
  the Global\OsdsGpuTest mutex is held by another process. Probes with a zero-wait grab and lets go
  at once; a mutex nobody ever created is not held.
#>
function Test-GpuTestLockHeld {
    $M = $null
    try { $Open = [System.Threading.Mutex]::TryOpenExisting($script:GpuTestLockName, [ref]$M) } catch { return $false }
    if (-not $Open) { return $false }
    try {
        $Got = $false
        try { $Got = $M.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $Got = $true }
        if ($Got) { try { $M.ReleaseMutex() } catch { }; return $false }
        return $true
    }
    finally { $M.Dispose() }
}

<#
  What a build or a headless test run does so a game someone is watching stays smooth (issue #69):
  with a GPU test running, start at BelowNormal priority and let Unreal Build Tool run half the
  cores' worth of compile actions (-MaxParallelActions; 4 of 8). With no GPU test: nothing changes
  (Normal priority, MaxParallelActions 0 = no argument, UBT picks its own number).
  $env:OSDS_THROTTLE_ACTIONS overrides the number of actions.
#>
function Get-LowImpactSettings {
    # Cores: the physical cores (8 on the Owner's PC; Environment.ProcessorCount counts hyper-threads too).
    param([bool]$GpuTestRunning, [int]$Cores = 0)
    if ($Cores -le 0) {
        try { $Cores = [int]((Get-CimInstance Win32_Processor | Measure-Object NumberOfCores -Sum).Sum) } catch { $Cores = 0 }
        if ($Cores -le 0) { $Cores = [int][math]::Max(2, [Environment]::ProcessorCount / 2) }
    }
    if (-not $GpuTestRunning) { return [pscustomobject]@{ Throttle = $false; Priority = 'Normal'; MaxParallelActions = 0 } }
    $Actions = [int][math]::Max(2, [math]::Floor($Cores / 2))
    if ($env:OSDS_THROTTLE_ACTIONS -and [int]$env:OSDS_THROTTLE_ACTIONS -gt 0) { $Actions = [int]$env:OSDS_THROTTLE_ACTIONS }
    return [pscustomobject]@{ Throttle = $true; Priority = 'BelowNormal'; MaxParallelActions = $Actions }
}
