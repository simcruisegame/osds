<#
.SYNOPSIS
  Fetch the CARLA art OSDS uses (CC-BY 4.0) into Content\Carla (not kept in git).
.DESCRIPTION
  Clones bitbucket.org/carla-simulator/carla-content (branch ue5-dev) shallow and without file
  contents, checks out only the folders OSDS uses, then fetches those files' contents from Git LFS.
  About 2.4 GB. Run once per build PC before building or packaging; scripts\package.ps1 cooks
  what the game places.
#>
param([string]$Cache = 'C:\sc\carla-content', [string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$Folders = @('Static/TrafficSign', 'Static/TrafficLight', 'Static/Signs', 'Static/StreetLight', 'Static/Pole',
    'Static/GenericMaterials/Facade', 'Static/Building/Props', 'Static/Building/Roofs')
if (-not (Test-Path (Join-Path $Cache '.git'))) {
    $env:GIT_LFS_SKIP_SMUDGE = '1'
    git clone --depth 1 --filter=blob:none --no-checkout --branch ue5-dev https://bitbucket.org/carla-simulator/carla-content.git $Cache
}
Push-Location $Cache
try {
    git sparse-checkout init --no-cone
    (@('/LICENSE') + ($Folders | ForEach-Object { "/$_/" })) | Set-Content -Encoding ascii .git\info\sparse-checkout
    $env:GIT_LFS_SKIP_SMUDGE = '1'
    git checkout -q ue5-dev
    # Pointer files to their contents one by one (git lfs pull walks the whole tree in a partial clone).
    $Pointers = foreach ($F in $Folders) { Get-ChildItem $F -Recurse -File | Where-Object { $_.Length -lt 300 -and (Get-Content $_.FullName -TotalCount 1) -like 'version https://git-lfs*' } }
    foreach ($Ptr in $Pointers) {
        $P = $Ptr.FullName; cmd /c "git lfs smudge `"$P`" < `"$P`" > `"$P.tmp`"" | Out-Null
        if ((Get-Item "$P.tmp").Length -gt 300) { Move-Item -Force "$P.tmp" $P } else { Remove-Item "$P.tmp" }
    }
} finally { Pop-Location }
$Dest = Join-Path $Root 'Content\Carla\Static'
foreach ($F in $Folders) {
    $To = Join-Path $Root ("Content\Carla\" + $F.Replace('/', '\'))
    New-Item -ItemType Directory -Force (Split-Path -Parent $To) | Out-Null
    robocopy (Join-Path $Cache $F.Replace('/', '\')) $To /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
}
Copy-Item (Join-Path $Cache 'LICENSE') (Join-Path $Root 'Content\Carla\LICENSE.txt') -Force
Write-Host "CARLA art in $Dest"
