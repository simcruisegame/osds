<#
.SYNOPSIS
  The morning report of the final test: final-summary.md (verdict first), and a local branch with the
  summary and the best frames as JPG under 2 MB, ready to push when pushing is allowed.

.DESCRIPTION
  Reads the release test's summary.md and teams\*.md (-RunDir), the frame review's detail-review.md
  (-ReviewDir: the "found by the new method only" table and the checklist), the grades (-Grades, a md
  table), the cooked gate and installer notes (-CookedDir, optional) and the known-fault list
  (docs/team/known-faults-v2.0.3.md). Writes <RunDir>\final-summary.md:
    1. Verdict (PASS / FAIL / PASS WITH OPEN P1s), what passed, what failed
    2. The P1 list (every "| P1 |" row of the team reports)
    3. "Found by the new method only"
    4. The grades table
    5. The videos to watch (the runs with a P1 or a FAIL first)
  -Frames: up to 8 frame paths (png or jpg) to copy as JPG under 2 MB.
  -Branch: a local branch made from -BaseRef in a temporary worktree, with release-reports\<name>\ (the
  summary and the frames) committed; nothing is pushed.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\final-report.ps1 -RunDir D:\...\final-x -ReviewDir D:\...\final-x\review -Grades D:\...\grades.md -Frames a.png,b.png -Branch report/v2.0.3-final
#>
param(
    [Parameter(Mandatory)][string]$RunDir,
    [string]$ReviewDir = '',
    [string]$Grades = '',
    [string]$CookedDir = '',
    [string[]]$Frames = @(),
    [string]$Branch = '',
    [string]$BaseRef = 'origin/main',
    [string]$Candidate = ''
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Frames = @($Frames | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Teams = @(Get-ChildItem (Join-Path $RunDir 'teams') -Filter '*.md' -ErrorAction SilentlyContinue)
$P1 = @($Teams | ForEach-Object { $T = $_.BaseName; Get-Content $_.FullName | Where-Object { $_ -match '^\|\s*P1\s*\|' } | ForEach-Object { "| $T $($_.Substring(1))" } })
$P0 = @($Teams | ForEach-Object { $T = $_.BaseName; Get-Content $_.FullName | Where-Object { $_ -match '^\|\s*P0\s*\|' } | ForEach-Object { "| $T $($_.Substring(1))" } })
$Summary = if (Test-Path (Join-Path $RunDir 'summary.md')) { Get-Content (Join-Path $RunDir 'summary.md') } else { @() }
$Fails = @($Summary | Where-Object { $_ -match 'FAIL' })
$Verdict = if (-not $Teams.Count -and -not $Summary.Count) { 'NO RESULT (no summary.md or team reports in the run folder)' } elseif ($P0.Count) { "FAIL: $($P0.Count) P0" } elseif ($P1.Count) { "NOT READY: $($P1.Count) open P1 (the owner decides)" } else { 'PASS' }
function Section([string]$File, [string]$Heading) {
    if (-not $File -or -not (Test-Path $File)) { return @("(not available: $File)") }
    $L = Get-Content $File; $Out = @(); $In = $false
    foreach ($X in $L) { if ($X -match "^## .*$([regex]::Escape($Heading))") { $In = $true; continue }; if ($In -and $X -match '^## ') { break }; if ($In) { $Out += $X } }
    return $Out
}
$Videos = @(Get-ChildItem $RunDir -Recurse -Filter 'video.mp4' -ErrorAction SilentlyContinue | Sort-Object { $D = $_.Directory.FullName; -[int](@($P1 | Where-Object { $_ -match [regex]::Escape((Split-Path -Leaf $D)) }).Count) } | Select-Object -First 15)
$Md = @("# v2.0.3 final test: $Verdict", '',
    "Candidate: $Candidate. Report: $RunDir. Written $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm'))Z.", '',
    "**Passed:** $(@($Summary | Where-Object { $_ -match '\|\s*(ok|PASS)\s*\|' }).Count) checks in the summary table. **Failed:** $($Fails.Count) lines with FAIL. **P0:** $($P0.Count). **P1:** $($P1.Count).", '',
    '## P1 and P0', '', '| Team | Sev | Place | Car | Time | Evidence | What |', '|---|---|---|---|---|---|---|') + $P0 + $P1 + @('',
    '## Found by the new method only', '') + (Section $(if ($ReviewDir) { Join-Path $ReviewDir 'detail-review.md' } else { '' }) 'Found by the new method only') + @('',
    '## Grades', '') + $(if ($Grades -and (Test-Path $Grades)) { Get-Content $Grades } else { @('(grades file not given)') }) + @('',
    '## Videos to watch (P1 places first)', '') + @($Videos | ForEach-Object { "- $($_.FullName)" }) + @('',
    '## Cooked package, gate and installer', '') + $(if ($CookedDir -and (Test-Path (Join-Path $CookedDir 'summary.md'))) { Get-Content (Join-Path $CookedDir 'summary.md') | Select-Object -First 30 } else { @('(see the cooked run folder)') }) + @('',
    '## Known faults', '', 'docs/team/known-faults-v2.0.3.md (each fault with its state and the after-the-fix frame).', '',
    '## The full release-test summary', '') + $Summary
$Out = Join-Path $RunDir 'final-summary.md'
$Md | Set-Content -Encoding utf8 $Out
Write-Host "FINAL SUMMARY -> $Out ($Verdict)"

if ($Branch) {
    Add-Type -AssemblyName System.Drawing
    $Wt = Join-Path $env:TEMP "report-wt-$(Get-Date -Format 'HHmmss')"
    git -C $Root worktree add -q -b $Branch $Wt $BaseRef
    try {
        $Name = Split-Path -Leaf $RunDir
        $Dst = Join-Path $Wt "release-reports\$Name"
        New-Item -ItemType Directory -Force $Dst | Out-Null
        Copy-Item $Out (Join-Path $Dst 'summary.md')
        $K = 0
        foreach ($F in ($Frames | Select-Object -First 8)) {
            if (-not (Test-Path $F)) { continue }
            $K++
            $Img = [System.Drawing.Image]::FromFile($F)
            try {
                $Enc = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object MimeType -eq 'image/jpeg'
                foreach ($Q in 85, 75, 65, 55) {
                    $P = New-Object System.Drawing.Imaging.EncoderParameters 1
                    $P.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), ([long]$Q)
                    $J = Join-Path $Dst ('{0:D2}-{1}.jpg' -f $K, [IO.Path]::GetFileNameWithoutExtension($F))
                    $Img.Save($J, $Enc, $P)
                    if ((Get-Item $J).Length -lt 2MB) { break }
                }
            } finally { $Img.Dispose() }
        }
        git -C $Wt add -A
        git -C $Wt commit -q -m "Report: v2.0.3 final test ($Verdict)`n`nThe summary and $K frames of $RunDir. Not pushed.`n`nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
        Write-Host "Local branch ${Branch}: $(git -C $Wt log --oneline -1)"
    } finally { git -C $Root worktree remove --force $Wt }
}
