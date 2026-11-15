<#
.SYNOPSIS
  Waits for this lane's turn on the PC's GPU (C:\aiwork\GPU-BUSY, GPU-QUEUE, GPU-TURN). Call it before a build
  or a game launch. Standalone: the shared copy is C:\aiwork\gpu-wait.ps1, the same file as scripts\gpu-wait.ps1.

.DESCRIPTION
  C:\aiwork\GPU-BUSY: one line while a lane uses the GPU or the CPU heavily (holder, start, end_estimate (UTC),
  purpose). It is stale, and ignored, 30 min after its end_estimate.
  C:\aiwork\GPU-QUEUE: the order, one holder name per line, first goes first. It is fresh while it has lines
  and was changed in the last 30 min (the coordinator refreshes it); a stale queue blocks nobody.
  C:\aiwork\GPU-TURN: "holder=<name>, until=<UTC>": the lane whose turn it is. A listed lane that reaches the
  front takes the turn (for -TurnMinutes) and its line leaves the queue; during the turn its own later calls
  pass (the steps of one chain), and every other caller waits. -Release ends the turn early.

  The rules, in order, while GPU-BUSY is free:
    a fresh turn of this caller (-Me)           pass
    a fresh turn of another lane                wait ("turn of <name>")
    a fresh queue, this caller listed           wait until first, then take the turn and pass
    a fresh queue, this caller not listed       wait ("not in queue, waiting") until the queue empties or goes stale
    no fresh queue                              pass
  -Me defaults to $env:SIMCRUISE_GPU_ME, so a chain sets it once for all its steps.
  Exit 0 when it may go, 1 on the timeout.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File C:\aiwork\gpu-wait.ps1 -Me lane-view-final -TurnMinutes 600
  powershell -ExecutionPolicy Bypass -File C:\aiwork\gpu-wait.ps1 -Me lane-view-final -Release
#>
param([int]$TimeoutMinutes = 120, [string]$Me = '', [int]$TurnMinutes = 60, [switch]$Release)

$Dir = 'C:\aiwork'
$BusyFile = Join-Path $Dir 'GPU-BUSY'
$Queue = Join-Path $Dir 'GPU-QUEUE'
$TurnFile = Join-Path $Dir 'GPU-TURN'
if (-not $Me) { $Me = $env:SIMCRUISE_GPU_ME }
$Inv = [Globalization.CultureInfo]::InvariantCulture
function Utc([string]$S) { try { [datetime]::Parse($S, $Inv, [Globalization.DateTimeStyles]::AdjustToUniversal -bor [Globalization.DateTimeStyles]::AssumeUniversal) } catch { $null } }
function Busy {
    if (-not (Test-Path $BusyFile)) { return $null }
    $T = (Get-Content $BusyFile -Raw -ErrorAction SilentlyContinue)
    if (-not $T) { return $null }
    $End = if ($T -match 'end_estimate=([0-9T:\-Z]+)') { Utc $Matches[1] } else { $null }
    $Stale = $End -and ((Get-Date).ToUniversalTime() - $End).TotalMinutes -gt 30
    [pscustomobject]@{ Text = $T.Trim(); Stale = [bool]$Stale }
}
function Turn {
    if (-not (Test-Path $TurnFile)) { return '' }
    $T = Get-Content $TurnFile -Raw -ErrorAction SilentlyContinue
    if ($T -match 'holder=([^,\s]+),\s*until=([0-9T:\-Z]+)') { $U = Utc $Matches[2]; if ($U -and $U -gt (Get-Date).ToUniversalTime()) { return $Matches[1] } }
    return ''
}
function QueueNow {
    if (-not (Test-Path $Queue)) { return @() }
    if (((Get-Date) - (Get-Item $Queue).LastWriteTime).TotalMinutes -ge 30) { return @() }
    return @(Get-Content $Queue | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}
function Note([string]$Why) { Write-Host "$((Get-Date).ToUniversalTime().ToString('HH:mm:ss'))Z waiting: $Why" }

if ($Release) {
    if ($Me -and (Turn) -eq $Me) { Remove-Item $TurnFile -Force -ErrorAction SilentlyContinue; Write-Host "Turn of $Me released." }
    exit 0
}
$Until = (Get-Date).AddMinutes($TimeoutMinutes)
while ($true) {
    if ((Get-Date) -gt $Until) { Write-Host "Timed out after $TimeoutMinutes min."; exit 1 }
    $B = Busy
    if ($B -and $B.Stale) { Write-Host "GPU-BUSY is stale (end_estimate more than 30 min past), ignored: $($B.Text)"; $B = $null }
    if ($B) { Note $B.Text; Start-Sleep -Seconds 30; continue }
    $T = Turn
    if ($T -and $Me -and $T -eq $Me) { Write-Host "GPU free (turn of $Me)."; exit 0 }
    if ($T) { Note "turn of $T (GPU-TURN)"; Start-Sleep -Seconds 30; continue }
    $Q = QueueNow
    if (-not $Q.Count) { Write-Host 'GPU free.'; exit 0 }
    if ($Me -and $Q -contains $Me) {
        if ($Q[0] -ne $Me) { Note "$($Q[0]) is first in the queue"; Start-Sleep -Seconds 30; continue }
        @($Q | Select-Object -Skip 1) | Set-Content -Encoding ascii $Queue
        "holder=$Me, until=$((Get-Date).ToUniversalTime().AddMinutes($TurnMinutes).ToString('yyyy-MM-ddTHH:mm:ssZ'))" | Set-Content -Encoding ascii $TurnFile
        Write-Host "GPU free: the turn of $Me for up to $TurnMinutes min."
        exit 0
    }
    Note "not in queue, waiting ($(if ($Me) { $Me } else { 'no -Me' }); the queue: $($Q -join ', '))"
    Start-Sleep -Seconds 30
}
