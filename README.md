<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/brand/png/osds-lockup-full-white.png">
    <img src="docs/brand/png/osds-lockup-full.png" alt="OSDS, Open Source Driving Simulator" width="460">
  </picture>
</p>

<p align="center">
  <b>Drive any real road in the US.</b> Free, open source, Unreal Engine 5, Windows.<br>
  Roads, terrain, buildings and trees are built live from OpenStreetMap, USGS elevation and aerial photos.<br>
  Cars are calibrated against real road tests. Keyboard, Xbox controller or USB wheel.
</p>

<p align="center">
  <a href="https://github.com/simcruisegame/simcruise/releases/latest"><b>Download for Windows</b></a>
  · <a href="https://simcruisegame.github.io/simcruise/">Website</a>
  · <a href="docs/controls.md">Controls</a>
  · <a href="CHANGELOG.md">What's new</a>
  · <a href="docs/technical-deep-dive.md">How it works</a>
</p>

<p align="center">
  <a href="https://github.com/simcruisegame/simcruise/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/simcruisegame/simcruise?include_prereleases&sort=semver"></a>
  <a href="https://github.com/simcruisegame/simcruise/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/simcruisegame/simcruise/total"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/code-MIT-blue"></a>
  <a href="https://www.unrealengine.com/"><img alt="Made with Unreal Engine 5.8" src="https://img.shields.io/badge/Unreal_Engine-5.8-0e1128?logo=unrealengine&logoColor=white"></a>
</p>

<p align="center">
  <img src="docs/images/release-2.0.2/06-miata-tail-of-the-dragon-chase.jpg" alt="A 2001 Mazda Miata on US 129, the Tail of the Dragon, built from real map and elevation data" width="900">
</p>

## TL;DR

- **Download** the setup from [Releases](https://github.com/simcruisegame/simcruise/releases/latest), install, play. Windows 10/11, DirectX 12 GPU with 6 GB, 16 GB RAM, internet ([details](docs/install.md)).
- **Drive anywhere:** press `/` and type a US place. Tab (Menu on the pad) opens the pause menu: GPS route, famous roads, car, view, settings.
- **No account, no keys, no telemetry.** Only free public data, cached on your disk.
- **Early days.** It is a driving simulator, not a racing game. No moving traffic for now. [Known issues](#known-issues).

## Famous roads

Pause menu > **Famous roads**, and the GPS takes you along:

| Road | Length |
|---|---|
| Tail of the Dragon, US 129 (NC/TN): 318 curves | 10.8 mi |
| Big Sur, CA 1: Bixby Creek Bridge to Big Sur | 13.2 mi |
| Monument Valley, US 163 (UT) | 6.2 mi |
| Ocean Drive, Miami Beach (works offline) | 1.3 mi |

## Controls

| Action | Keyboard | Xbox controller |
|---|---|---|
| Throttle / brake | W / S | Right / left trigger |
| Steer | A / D | Left stick |
| Shift up / down (manual) | E / Q | RB / LB |
| Automatic / manual | T | Right-stick click |
| Camera | C | Y |
| Back on the road | R | View |
| Pause menu | Tab | Menu |

Everything else: [docs/controls.md](docs/controls.md).

## Screenshots

| | |
|---|---|
| ![The GPS on Ocean Drive](docs/images/release-2.0.2/01-gps-route-ocean-drive.jpg) | ![The Corvette C8 on CA-1 near Big Sur](docs/images/release-2.0.2/07-corvette-big-sur-chase.jpg) |
| *GPS on Ocean Drive* | *Corvette C8, Big Sur* |
| ![From the Corvette's seat on CA-1](docs/images/release-2.0.2/08-corvette-big-sur-cockpit.jpg) | ![Ocean Drive at night](docs/images/release-2.0.1/07-ocean-drive-night-chase.jpg) |
| *Corvette cockpit* | *Ocean Drive at night* |

More in [docs/images/](docs/images/).

## Known issues

- Some motorway bridges meet the road below at the same level instead of passing over it.
- Small lips (6-12 cm) where some driveways and ramps join a road; ground over a lane edge at a few hundred spots.
- Full list per release: [CHANGELOG.md](CHANGELOG.md).

## Build from source

Needs Unreal Engine 5.8, Visual Studio 2022 Build Tools (C++ game workload) and [Bun](https://bun.com/). In PowerShell, editor closed:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' SimCruiseEditor Win64 Development -Project="$PWD\SimCruise.uproject" -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\SimCruise.uproject" -game
```

Tests: `powershell -ExecutionPolicy Bypass -File scripts\check.ps1` (add `-Filter SimCruise.Vehicle` for one part). Options, captures and packaging: [docs/unreal-building.md](docs/unreal-building.md).

| Folder | What |
|---|---|
| `Source/SimCruise/` | The game (C++): car physics, world, menus, tests |
| `helper/` | Map helper (Bun): turns public data into tiles |
| `data/` | Car data, car models, the bundled offline tiles |
| `scripts/` | Build, test, package, release |
| `docs/` | How it works, building, cars, research |

## Docs

[How it works](docs/technical-deep-dive.md) · [Architecture](docs/unreal-architecture.md) · [Cars](docs/cars.md) · [Controls](docs/controls.md) · [Data sources](docs/data-sources.md) · [Roadmap](docs/roadmap.md) · [Install](docs/install.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) · [Code of Conduct](CODE_OF_CONDUCT.md)

## License and credits

Code: [MIT](LICENSE). Data, imagery, models and fonts keep their own licences: [CREDITS.md](CREDITS.md), [docs/data-sources.md](docs/data-sources.md), [`licenses/`](licenses/), [Credits.txt](Credits.txt).

- Map data © [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors (ODbL), via [OpenFreeMap](https://openfreemap.org/) / [OpenMapTiles](https://openmaptiles.org/); search by [Photon](https://photon.komoot.io/); buildings also from [Overture Maps](https://overturemaps.org/) and the USACE National Structure Inventory.
- Elevation, aerial photos and land cover: USGS (3DEP), USDA (NAIP), LANDFIRE, MRLC (NLCD), Mapzen, NASA, Copernicus Sentinel-2. Tree heights: [CHMv2](https://registry.opendata.aws/dataforgood-fb-forestsv2/) by Meta and WRI, 2026, imagery © 2016 Vantor ([CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)), changed. Climate: Köppen-Geiger 1991-2020, Beck et al. (2023), CC BY 4.0. Weather: [Open-Meteo.com](https://open-meteo.com/) (CC BY 4.0) and NOAA.
- AI models: Stable Diffusion XL (textures), Real-ESRGAN, SatlasPretrain, Detectron2, run with ONNX Runtime ([details](CREDITS.md)).
- Cars: Sketchfab models under CC BY 4.0, among them the Mazda MX-5 (NB) by [niev](https://sketchfab.com/niev), the 1965 Mustang by Kristian LaGrange and the Corvette C8 by Hari Prasath R. Textures: Poly Haven, Innerscene, OpenGameArt (CC0). Font: Overpass (SIL OFL 1.1).
- Built with [Unreal Engine](https://www.unrealengine.com/) and [Bun](https://bun.com/).

OSDS was called SimCruise until 2.0.3; the code still uses that name inside (module, `-SimCruise...` options, `%LOCALAPPDATA%\SimCruise`), so old settings keep working.
