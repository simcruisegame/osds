<#
.SYNOPSIS
  Waits while another lane holds the GPU (C:\aiwork\GPU-BUSY). Call it before a build or a game launch.

.DESCRIPTION
  The file has one line: holder, start, end_estimate (UTC) and purpose. The script waits while the
  file is there, checking every 30 s, up to -TimeoutMinutes. A file whose end_estimate is more than
  30 min past is stale: it is ignored and logged. Exit 0 when the GPU is free (or the file is
  stale), 1 on the timeout.

  The order: C:\aiwork\GPU-QUEUE, when it is there, has one holder name per line, first goes first.
  With -Me <holder> the script also waits while another holder is first in it, and when the GPU is
  free and it is first (or not listed), it takes its own line out. A queue file not changed for
  30 min is stale and ignored. Without -Me the queue is not read.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\gpu-wait.ps1
  powershell -ExecutionPolicy Bypass -File scripts\gpu-wait.ps1 -TimeoutMinutes 90 -Me lane-view
#>
param([int]$TimeoutMinutes = 120, [string]$Me = '')

. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Until = (Get-Date).AddMinutes($TimeoutMinutes)
$Queue = 'C:\aiwork\GPU-QUEUE'
while ($true) {
    $B = Read-GpuBusy
    $Q = if ($Me -and (Test-Path $Queue) -and ((Get-Date) - (Get-Item $Queue).LastWriteTime).TotalMinutes -lt 30) { @(Get-Content $Queue | ForEach-Object { $_.Trim() } | Where-Object { $_ }) } else { @() }
    $Ahead = if ($Q.Count -and $Q[0] -ne $Me -and $Q -contains $Me) { $Q[0] } else { '' }
    if (-not $B -and -not $Ahead) {
        if ($Q -contains $Me) { @($Q | Where-Object { $_ -ne $Me }) | Set-Content -Encoding ascii $Queue }
        Write-Host 'GPU free.'; exit 0
    }
    if (-not $B) { if ((Get-Date) -gt $Until) { Write-Host "Timed out after $TimeoutMinutes min; $Ahead is first in the queue."; exit 1 }; Write-Host "$((Get-Date).ToUniversalTime().ToString('HH:mm:ss'))Z GPU free, $Ahead is first in the queue"; Start-Sleep -Seconds 30; continue }
    if ($B.Stale) { Write-Host "GPU-BUSY is stale (end_estimate more than 30 min past), ignored: $($B.Text)"; exit 0 }
    if ((Get-Date) -gt $Until) { Write-Host "Timed out after $TimeoutMinutes min; still busy: $($B.Text)"; exit 1 }
    Write-Host "$((Get-Date).ToUniversalTime().ToString('HH:mm:ss'))Z waiting: $($B.Text)"
    Start-Sleep -Seconds 30
}
