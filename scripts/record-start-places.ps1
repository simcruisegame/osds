# Records the tiles of the 18 places of data/test-places.json for SimCruise.World.Start.EveryPlace:
# the 3 x 3 zoom-16 tiles round each place (the helper's tile command, from the map cache where it
# has them), into Saved\StartPlaces\<key>\x_y.json with a place.txt (the start and the build order,
# the start tile first). CPU and network only, no game. The documents are map data (ODbL): they stay
# in Saved (not in git).
#
#   powershell -ExecutionPolicy Bypass -File scripts\record-start-places.ps1 [-Only key,key]
param([string[]]$Only = @())
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $Root 'Saved\StartPlaces'
$Cache = Join-Path $env:LOCALAPPDATA 'SimCruise\map-cache'
$Bun = (Get-Command bun -ErrorAction SilentlyContinue).Source
if (-not $Bun) { throw 'bun is not on PATH (the map helper runs on it)' }
$Places = Get-Content (Join-Path $Root 'data\test-places.json') -Raw | ConvertFrom-Json
foreach ($P in $Places) {
    if ($Only.Count -gt 0 -and $Only -notcontains $P.key) { continue }
    $Lat = [double]$P.lat; $Lon = [double]$P.lon
    $X = [math]::Floor(($Lon + 180.0) / 360.0 * 65536)
    $Rad = $Lat * [math]::PI / 180.0
    $Y = [math]::Floor((1.0 - [math]::Log([math]::Tan($Rad) + 1.0 / [math]::Cos($Rad)) / [math]::PI) / 2.0 * 65536)
    $Dir = Join-Path $Out $P.key
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $Order = @("${X}_${Y}")
    foreach ($dy in -1..1) { foreach ($dx in -1..1) { if ($dx -ne 0 -or $dy -ne 0) { $Order += "$($X + $dx)_$($Y + $dy)" } } }
    foreach ($T in $Order) {
        $tx, $ty = $T.Split('_')
        $Json = & $Bun (Join-Path $Root 'helper\atlas.mjs') tile $Cache $tx $ty 2>$null | Select-Object -Last 1
        if ($Json -and $Json.StartsWith('{')) { [IO.File]::WriteAllText((Join-Path $Dir "$T.json"), $Json) } else { Write-Warning "$($P.key) $T`: no tile" }
    }
    [IO.File]::WriteAllText((Join-Path $Dir 'place.txt'), "start $Lat $Lon`norder $($Order -join ' ')`n")
    Write-Output "$($P.key): $($Order.Count) tiles"
}
