<#
.SYNOPSIS
  Random US road points by place type, for scripts\launch-review.ps1 -RandomUS (dot-source this file).

.DESCRIPTION
  The game builds any place in the US as you drive, so a review must not only look at the hero
  roads. Each place type has three areas where roads are common; a seeded random number picks an
  area and a point in it, and the game puts the car on the nearest road (-SimCruiseStart without a
  road name). The same seed gives the same points, so a failure repeats.
#>

<#
  The seeded draw from data\test-places.json that scripts\preflight.ps1 -Roads random makes (a
  System.Random partial shuffle of the keys, the same in Windows PowerShell 5.1 and PowerShell 7),
  so a seed picks the same places in every script. Each place gets its own local solar noon on the
  -Utc date (12 h - lon/15) as .Utc. Empty when the file is not there.
#>
function Get-TestPlaceDraw([string]$Root, [int]$Seed, [int]$Count, [string]$Utc = '2026-11-12T17:00:00Z') {
    $File = Join-Path $Root 'data\test-places.json'
    if (-not (Test-Path $File)) { return @() }
    $Places = @((Get-Content $File -Raw | ConvertFrom-Json))
    $Rng = [System.Random]::new($Seed)
    $Keys = @($Places | ForEach-Object { $_.key })
    $Take = [Math]::Max(1, [Math]::Min($Count, $Keys.Count))
    for ($i = 0; $i -lt $Take; $i++) { $j = $Rng.Next($i, $Keys.Count); $t = $Keys[$i]; $Keys[$i] = $Keys[$j]; $Keys[$j] = $t }
    $Day = ([datetime]::Parse($Utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AdjustToUniversal)).Date
    return @($Places | Where-Object { $Keys[0..($Take - 1)] -contains $_.key } | ForEach-Object {
        $_ | Add-Member -NotePropertyName Utc -NotePropertyValue ($Day.AddHours(12.0 - [double]$_.lon / 15.0).ToString('yyyy-MM-ddTHH:mm:ssZ')) -PassThru -Force })
}

# Alaska and Hawaii road areas for the release draw (south, north, west, east): Anchorage to the
# Mat-Su, Fairbanks, the Kenai and Seward highways; Oahu, Maui, the Kona coast. A point with no road or
# no map data near it must fall back cleanly (the start pick's nearest-road rule): that is a test too.
$script:ReleaseExtraAreas = [ordered]@{
    'alaska' = @(@(61.05, 61.65, -150.1, -149.1), @(64.75, 64.9, -147.95, -147.5), @(60.1, 60.6, -151.3, -149.4))
    'hawaii' = @(@(21.28, 21.6, -158.15, -157.7), @(20.75, 20.95, -156.7, -156.35), @(19.5, 19.8, -156.05, -155.9))
}

<#
  The release test's draw (2026-11-14 team meeting): every place of data\test-places.json in a seeded
  order, at least -MinAlaska places in Alaska and -MinHawaii in Hawaii (from the list first, then seeded
  points in the areas above), and seeded points from the US areas below up to -Count. Each place gets
  the local noon of the day of -Utc. Same rules as the hero places in the release test.
#>
function Get-ReleasePlaceDraw([string]$Root, [int]$Seed, [int]$Count = 30, [int]$MinAlaska = 3, [int]$MinHawaii = 3, [string]$Utc = '2026-11-12T17:00:00Z') {
    $Rng = [System.Random]::new($Seed)
    $Day = ([datetime]::Parse($Utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AdjustToUniversal)).Date
    $Noon = { param($Lon) $Day.AddHours(12.0 - [double]$Lon / 15.0).ToString('yyyy-MM-ddTHH:mm:ssZ') }
    $IsAk = { param($P) [double]$P.lat -gt 51.0 -and [double]$P.lon -lt -129.0 }
    $IsHi = { param($P) [double]$P.lat -lt 23.0 -and [double]$P.lon -lt -154.0 }
    $File = Join-Path $Root 'data\test-places.json'
    $Listed = if (Test-Path $File) { @((Get-Content $File -Raw | ConvertFrom-Json) | Sort-Object { $Rng.Next() }) } else { @() }
    $Out = [System.Collections.Generic.List[object]]::new()
    foreach ($P in $Listed) { if ($Out.Count -lt $Count) { $Out.Add([pscustomobject]@{ key = $P.key; lat = [double]$P.lat; lon = [double]$P.lon; kind = $P.kind; Utc = (& $Noon $P.lon) }) } }
    $Point = {
        param($Kind, $Areas, $N)
        $A = $Areas[$Rng.Next(0, $Areas.Count)]
        $Lat = [Math]::Round($A[0] + ($A[1] - $A[0]) * $Rng.NextDouble(), 5)
        $Lon = [Math]::Round($A[2] + ($A[3] - $A[2]) * $Rng.NextDouble(), 5)
        [pscustomobject]@{ key = "rand-$Kind-$N"; lat = $Lat; lon = $Lon; kind = $Kind; Utc = (& $Noon $Lon) }
    }
    # Make room for the Alaska and Hawaii places, taking out listed places of other states from the end.
    foreach ($Need in @(@('alaska', $MinAlaska, $IsAk), @('hawaii', $MinHawaii, $IsHi))) {
        $Have = @($Out | Where-Object { & $Need[2] $_ }).Count
        for ($K = $Have; $K -lt $Need[1]; $K++) {
            if ($Out.Count -ge $Count) {
                $Drop = $Out | Where-Object { -not (& $IsAk $_) -and -not (& $IsHi $_) } | Select-Object -Last 1
                if ($Drop) { [void]$Out.Remove($Drop) }
            }
            $Out.Add((& $Point $Need[0] $script:ReleaseExtraAreas[$Need[0]] ($K + 1)))
        }
    }
    $Types = @($script:RandomUSAreas.Keys)
    $T = 0
    while ($Out.Count -lt $Count) {
        $Kind = $Types[$T % $Types.Count]; $T++
        $Out.Add((& $Point $Kind $script:RandomUSAreas[$Kind] $T))
    }
    return @($Out)
}

# Areas: south, north, west, east (degrees).
$script:RandomUSAreas = [ordered]@{
    'desert'     = @(@(32.3, 33.8, -114.5, -111.0), @(35.5, 36.6, -116.0, -114.6), @(34.5, 35.3, -108.5, -106.5))
    'mountains'  = @(@(38.6, 40.0, -107.0, -105.5), @(35.2, 36.0, -84.0, -82.5), @(45.5, 46.8, -114.0, -112.0))
    'plains'     = @(@(41.5, 42.8, -95.5, -92.0), @(37.8, 39.0, -100.0, -97.0), @(40.3, 41.5, -99.5, -96.8))
    'forest'     = @(@(44.0, 45.3, -71.5, -69.5), @(44.5, 46.5, -123.5, -122.3), @(46.5, 47.5, -94.5, -92.5))
    'coast'      = @(@(35.0, 37.5, -122.4, -120.6), @(27.0, 28.6, -82.8, -82.3), @(39.2, 40.2, -74.3, -74.0))
    'suburb'     = @(@(33.85, 34.1, -84.6, -84.15), @(32.95, 33.2, -96.95, -96.6), @(41.75, 42.0, -88.2, -87.95))
    'small-town' = @(@(40.0, 40.6, -83.5, -82.5), @(44.0, 44.6, -73.0, -72.4), @(29.9, 30.4, -99.2, -98.5))
    'city'       = @(@(40.70, 40.80, -74.02, -73.93), @(41.86, 41.92, -87.65, -87.61), @(37.74, 37.80, -122.47, -122.39))
}

<#
  N places. When data\test-places.json (the team's list of ordinary US places) is there, a seeded
  draw from it, one place type at a time; else the types in a seeded order (each once before any
  repeats), an area of that type and a point in it.
#>
function Get-RandomUSPlaces([int]$Count, [int]$Seed, [string]$Root = (Split-Path -Parent $PSScriptRoot)) {
    $Rng = New-Object System.Random($Seed)
    # The team's place list first, with the same seeded draw as preflight.ps1 -Roads random.
    $Draw = @(Get-TestPlaceDraw -Root $Root -Seed $Seed -Count $Count)
    if ($Draw.Count) {
        $I = 0
        return @($Draw | ForEach-Object { [pscustomobject]@{ Index = $I++; Type = $_.kind; Lat = [double]$_.lat; Lon = [double]$_.lon; Key = $_.key; Street = ''; Utc = $_.Utc } })
    }
    $Types = @($script:RandomUSAreas.Keys)
    $Order = @()
    while ($Order.Count -lt $Count) { $Order += @($Types | Sort-Object { $Rng.Next() }) }
    $Places = @()
    for ($I = 0; $I -lt $Count; $I++) {
        $Type = $Order[$I]
        $Areas = $script:RandomUSAreas[$Type]
        $A = $Areas[$Rng.Next($Areas.Count)]
        $Lat = $A[0] + ($A[1] - $A[0]) * $Rng.NextDouble()
        $Lon = $A[2] + ($A[3] - $A[2]) * $Rng.NextDouble()
        $Places += [pscustomobject]@{ Index = $I; Type = $Type; Lat = [Math]::Round($Lat, 5); Lon = [Math]::Round($Lon, 5) }
    }
    return $Places
}

<#
  A geotagged photo from Wikimedia Commons near a point (licence and author kept), saved as
  photo.jpg with photo.txt. Commons API etiquette: one request at a time, a User-Agent that names
  the client, thumbnails rather than originals. Returns the attribution text or $null.
#>
function Save-CommonsPhoto([string]$RunDir, [double]$Lat, [double]$Lon, [int]$RadiusM = 10000) {
    $Ua = @{ 'User-Agent' = 'SimCruiseReview/1.0 (game test review; contact via the SimCruise project)' }
    try {
        $Geo = Invoke-RestMethod -Headers $Ua -TimeoutSec 20 -Uri ("https://commons.wikimedia.org/w/api.php?action=query&list=geosearch&gsnamespace=6&gslimit=10&format=json&gsradius={0}&gscoord={1}|{2}" -f $RadiusM, $Lat, $Lon)
        foreach ($Hit in @($Geo.query.geosearch)) {
            $Title = [uri]::EscapeDataString($Hit.title)
            $Info = Invoke-RestMethod -Headers $Ua -TimeoutSec 20 -Uri "https://commons.wikimedia.org/w/api.php?action=query&prop=imageinfo&iiprop=url|extmetadata|mime&iiurlwidth=960&format=json&titles=$Title"
            $Page = @($Info.query.pages.PSObject.Properties)[0].Value
            $Ii = @($Page.imageinfo)[0]
            if (-not $Ii -or $Ii.mime -notmatch 'jpeg|png') { continue }
            $Licence = $Ii.extmetadata.LicenseShortName.value
            if ($Licence -notmatch 'CC|Public domain|PD') { continue }
            $Artist = ($Ii.extmetadata.Artist.value -replace '<[^>]+>', '').Trim()
            Invoke-WebRequest -Headers $Ua -TimeoutSec 30 -Uri $Ii.thumburl -OutFile (Join-Path $RunDir 'photo.jpg') -UseBasicParsing | Out-Null
            $Text = "$($Hit.title) by $Artist, $Licence, Wikimedia Commons, $($Ii.descriptionurl) ($([int]$Hit.dist) m from the point)"
            $Text | Set-Content -Encoding utf8 (Join-Path $RunDir 'photo.txt')
            return $Text
        }
    } catch { return $null }
    return $null
}
