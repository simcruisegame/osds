<#
.SYNOPSIS
  Builds, cooks and stages a Windows build of OSDS in Saved\Package\Windows.

.DESCRIPTION
  Saves the code-built materials first (a cooked game cannot compile them), runs
  BuildCookRun for Win64 Development, then copies in the files the game reads
  from disk rather than from its .pak: data\ (car definitions, bundled tiles and
  art), the car presentation profile and calibration overlays, and the map
  helper (tools\atlas.exe) when it is present. The result runs with
  Saved\Package\Windows\OSDS.exe and starts on the bundled Miami Beach streets.

.PARAMETER Configuration
  Development (default) or Shipping.

.PARAMETER SkipMaterials
  Do not resave the materials (Content\*\Materials must be current).

.PARAMETER Zip
  Also write Saved\Package\OSDS-<ProjectVersion>-windows-x64.zip (no debug symbols).

.PARAMETER NoBuild
  Skip the materials and BuildCookRun; only refresh the loose files, credits and zip.

.PARAMETER EngineDir
  The Unreal Engine 5.8 directory (default: $env:UE_ROOT, else C:\Program Files\Epic Games\UE_5.8).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\package.ps1
#>
param(
    [ValidateSet('Development', 'Shipping')]
    [string]$Configuration = 'Development',
    [switch]$SkipMaterials,
    [switch]$Zip,
    [switch]$NoBuild,
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'OSDS.uproject'
$Out = Join-Path $Root 'Saved\Package'

if (-not $SkipMaterials -and -not $NoBuild) {
    Write-Host 'Saving the generated materials'
    & (Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $Project -run=OsdsBuildWorldMaterials -unattended -nop4 -nosplash | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "OsdsBuildWorldMaterials failed ($LASTEXITCODE)" }
}

if (-not $NoBuild) {
# A fresh folder: archiving over an older package keeps its stale files (another configuration's exe).
$Stale = Join-Path $Out 'Windows'
if (Test-Path $Stale) { Remove-Item -Recurse -Force $Stale }
Write-Host "BuildCookRun Win64 $Configuration"
& (Join-Path $EngineDir 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$Project" -noP4 -platform=Win64 `
    "-clientconfig=$Configuration" -build -cook -map=/Engine/Maps/Entry -stage -pak -prereqs -archive "-archivedirectory=$Out" -unattended -utf8output
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE)" }
}

$Game = Join-Path $Out 'Windows\OSDS'
if (-not (Test-Path $Game)) { throw "No staged game at $Game" }

# The runtimes a clean Windows does not have (scripts\dep-check.py lists the DLLs): the
# Visual C++ 2015-2022 x64 runtime (MSVCP140, MSVCP140_1, VCRUNTIME140, VCRUNTIME140_1) and
# GameInput (racing wheels). -prereqs stages Microsoft's redistributable installers from the
# engine; they are copied here too, so a -NoBuild run or an older package also has them. The
# launcher OSDS.exe offers to run them when they are missing, and the installer runs them
# (packaging\windows\osds.iss). Terms: licenses\microsoft-redistributables.md.
$Redist = Join-Path $Out 'Windows\Engine\Extras\Redist\en-us'
New-Item -ItemType Directory -Force $Redist | Out-Null
# vc_redist.arm64.exe: the launcher asks for it on Windows on Arm (the game runs there in emulation).
foreach ($Name in 'vc_redist.x64.exe', 'vc_redist.arm64.exe', 'GameInputRedist.msi') {
    $From = Join-Path $EngineDir "Engine\Extras\Redist\en-us\$Name"
    if (-not (Test-Path $From)) { throw "The engine has no $From (a clean PC cannot start the game without it)" }
    Copy-Item $From $Redist -Force
    if ((Get-AuthenticodeSignature (Join-Path $Redist $Name)).Status -ne 'Valid') { throw "$Name has no valid signature" }
}
Write-Host 'Copying the loose data the game reads from disk'
# Only the car models a profile uses, and not data\observed (the Godot client's; unread here).
$Profiles = Get-Content -Raw (Join-Path $Root 'Content\Vehicles\CarPresentation.json') | ConvertFrom-Json
$Used = @($Profiles.cars.PSObject.Properties | ForEach-Object { ($_.Value.model -split '/')[3] } | Where-Object { $_ })
$Skip = @(Join-Path $Root 'data\observed') + @(Get-ChildItem -Directory (Join-Path $Root 'data\art\traffic') |
    Where-Object { $Used -notcontains $_.Name } | ForEach-Object { $_.FullName })
# AI models: only the ones cleared for shipping (scripts\ShippedModels.ps1, CREDITS.md). The
# copy leaves every .onnx out, so a new model is not shipped by accident, then puts in exactly
# the listed ones; scripts\release.ps1 fails on any other .onnx in the zip.
. (Join-Path $PSScriptRoot 'ShippedModels.ps1')
robocopy (Join-Path $Root 'data') (Join-Path $Game 'data') /MIR /XD @Skip /XF *.onnx /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying data failed ($LASTEXITCODE)" }
# An earlier copy's other models go (robocopy /MIR leaves excluded files in place).
Get-ChildItem (Join-Path $Game 'data') -Recurse -Filter *.onnx -ErrorAction SilentlyContinue | Where-Object {
    $script:ShippedModels -notcontains ('data/' + $_.FullName.Substring((Join-Path $Game 'data').Length + 1).Replace('\', '/'))
} | Remove-Item -Force
foreach ($Model in $script:ShippedModels) {
    $From = Join-Path $Root $Model
    if (-not (Test-Path $From)) { throw "A shipped model is missing: $Model" }
    $To = Join-Path $Game $Model
    New-Item -ItemType Directory -Force (Split-Path $To) | Out-Null
    Copy-Item $From $To -Force
}
foreach ($Dir in $Skip) {
    # robocopy /MIR leaves excluded folders in place; an earlier copy's go now.
    $Copied = Join-Path $Game ('data' + $Dir.Substring((Join-Path $Root 'data').Length))
    if (Test-Path $Copied) { Remove-Item -Recurse -Force $Copied }
}
New-Item -ItemType Directory -Force (Join-Path $Game 'Content\Vehicles') | Out-Null
Copy-Item (Join-Path $Root 'Content\Vehicles\CarPresentation.json') (Join-Path $Game 'Content\Vehicles') -Force
robocopy (Join-Path $Root 'Content\Vehicle\Calibration') (Join-Path $Game 'Content\Vehicle\Calibration') /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying calibration failed ($LASTEXITCODE)" }
$Atlas = Join-Path $Root 'tools\atlas.exe'
# The helper is not in git: build it from helper\atlas.mjs when Bun is installed, so a
# package never ships an older helper than its source.
$Bun = Get-Command bun -ErrorAction SilentlyContinue
$BunExe = if ($Bun) { $Bun.Source } else { Join-Path $env:USERPROFILE '.bun\bin\bun.exe' }
if (Test-Path $BunExe) {
    Write-Host 'Building tools\atlas.exe from helper\atlas.mjs'
    Push-Location $Root
    try {
        & $BunExe install --frozen-lockfile | Out-Null
        & $BunExe build --compile --minify --target=bun-windows-x64 helper/atlas.mjs --outfile tools/atlas.exe | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Building the atlas helper failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
}
if (Test-Path $Atlas) {
    New-Item -ItemType Directory -Force (Join-Path $Game 'tools') | Out-Null
    Copy-Item $Atlas (Join-Path $Game 'tools') -Force
} else {
    Write-Warning 'tools\atlas.exe is missing: the package drives only the bundled streets'
}

# What travels with the game: its licence, the data sources and every model's credit.
$Windows = Join-Path $Out 'Windows'
Copy-Item (Join-Path $Root 'LICENSE') (Join-Path $Windows 'LICENSE.txt') -Force
Copy-Item (Join-Path $Root 'docs\data-sources.md') (Join-Path $Windows 'DATA-SOURCES.md') -Force
Copy-Item (Join-Path $Root 'docs\controls.md') (Join-Path $Windows 'CONTROLS.md') -Force
robocopy (Join-Path $Root 'licenses') (Join-Path $Windows 'licenses') /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying licenses failed ($LASTEXITCODE)" }
Copy-Item (Join-Path $Root 'Credits.txt') (Join-Path $Windows 'licenses\Credits.txt') -Force
$Credits = @('OSDS uses these 3D models under their licences (CC BY 4.0 unless stated):', '')
$Seen = @{}
foreach ($Car in $Profiles.cars.PSObject.Properties) {
    $Credit = $Car.Value.credit
    if ($Credit -and -not $Seen.ContainsKey($Credit)) { $Seen[$Credit] = $true; $Credits += "- $Credit" }
}
$Credits += '', 'Traffic-signal poles and arms, roadside grass and bushes, facade weathering and brick textures, and the Mini: CARLA simulator content by the CARLA Team (https://carla.org), CC BY 4.0, from bitbucket.org/carla-simulator/carla-content (licenses\CARLA.txt).'
$Credits += 'Map data (c) OpenStreetMap contributors (ODbL); imagery, textures and the rest: see DATA-SOURCES.md.'
$Credits += 'Made with Unreal(R) Engine. Unreal(R) is a trademark or registered trademark of Epic Games, Inc.'
Set-Content -Path (Join-Path $Windows 'CREDITS.txt') -Value $Credits -Encoding utf8

if ($Zip) {
    $Version = ((Get-Content (Join-Path $Root 'Config\DefaultGame.ini')) -match '^ProjectVersion=' | Select-Object -First 1).Split('=')[1]
    $Archive = Join-Path $Out "OSDS-$Version-windows-x64.zip"
    if (Test-Path $Archive) { Remove-Item $Archive -Force }
    Write-Host "Zipping $Archive"
    # Windows' own bsdtar (not a Git or MSYS tar earlier on the PATH) writes a zip far faster
    # than Compress-Archive. The game sits at the archive's top (Windows' "Extract All" puts it in
    # a folder named after the zip); local test runs' Saved folder and debug symbols stay out.
    $Tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    $Top = Get-ChildItem $Windows -Name
    & $Tar -a -c -f $Archive -C $Windows --exclude '*.pdb' --exclude 'OSDS/Saved' @Top
    if ($LASTEXITCODE -ne 0) { throw "Zipping failed ($LASTEXITCODE)" }
    Write-Host "ZIPPED: $Archive ($([Math]::Round((Get-Item $Archive).Length / 1MB)) MB)"
}
$global:LASTEXITCODE = 0
Write-Host "PACKAGED: $(Join-Path $Out 'Windows\OSDS.exe')"
