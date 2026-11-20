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

<div style="background-color: #fff3cd; border-left: 4px solid #ffc107; padding: 12px 16px; margin-bottom: 16px; border-radius: 4px;">
  <strong>Note from the developer:</strong> (PLEASE STAR IN GITHUB <3) This is an open-source project started by a solo developer (And Claude, Mostly). Everyone is encouraged to jump in, contribute, and send PRs to help build this together! Backstory: A couple of weeks ago this whole game was just a random idea - I started messaging Claude about modern game development, open-source architecture, licensing for integrating other projects and data, and it started blowing my mind. This is a fun project, would love to see others contribute! I know it has a lot of bugs but I don't want to throw a ton of money at this so I have a 20$ a month Claude sub developing as efficiently as possible. So, more to come! 
</div>

<p align="center">
  <a href="https://github.com/simcruisegame/osds/releases/latest"><b>Download for Windows</b></a>
  · <a href="https://simcruisegame.github.io/osds/">Website</a>
  · <a href="docs/controls.md">Controls</a>
  · <a href="CHANGELOG.md">What's new</a>
  · <a href="docs/technical-deep-dive.md">How it works</a>
</p>

<p align="center">
  <a href="https://github.com/simcruisegame/osds/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/simcruisegame/osds?include_prereleases&sort=semver"></a>
  <a href="https://github.com/simcruisegame/osds/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/simcruisegame/osds/total"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/code-MIT-blue"></a>
  <a href="https://www.unrealengine.com/"><img alt="Made with Unreal Engine 5.8" src="https://img.shields.io/badge/Unreal_Engine-5.8-0e1128?logo=unrealengine&logoColor=white"></a>
</p>

<p align="center">
  <img src="docs/images/release-2.0.4/04-corvette-tail-of-the-dragon-chase.jpg" alt="The Corvette Stingray C8 on US 129, the Tail of the Dragon, built from real map and elevation data" width="900">
</p>

## TL;DR

- **Download** the setup from [Releases](https://github.com/simcruisegame/osds/releases/latest), install, play. Windows 10/11, DirectX 12 GPU with 6 GB, 16 GB RAM, internet ([details](docs/install.md)).
- **Drive anywhere:** press `/` and type a US place. Tab (Menu on the pad) opens the pause menu: GPS route, famous roads, car, view, settings.
- **No account, no keys, no telemetry.** Only free public data, cached on your disk.
- **Early days.** It is a driving simulator, not a racing game. No moving traffic for now. See [known issues](#known-issues).

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
| Shift up / down (manual; automatic clutch by default) | E / Q | RB / LB |
| Automatic / manual | T | Right-stick click |
| Camera | C | Y |
| Back on the road | R | View |
| Pause menu | Tab | Menu |

Everything else, including stability control (ESC): [docs/controls.md](docs/controls.md).

## Screenshots

| | |
|---|---|
| ![The 2015 Mini Cooper S on a Nebraska county road, with roadside grass from the CARLA simulator](docs/images/release-2.1.0/01-mini-nebraska-roadside-chase.jpg) | ![The 1987 Camaro IROC-Z on California 1 near Big Sur](docs/images/release-2.1.0/04-camaro-big-sur-chase.jpg) |
| *Mini Cooper S, Nebraska (new in 2.1)* | *Camaro IROC-Z, Big Sur (new in 2.1)* |
| ![From the Mini's seat in Chicago's Loop](docs/images/release-2.1.0/03-mini-chicago-cockpit.jpg) | ![The Camaro IROC-Z on US 129, the Tail of the Dragon](docs/images/release-2.1.0/05-camaro-tail-of-the-dragon-chase.jpg) |
| *Mini cockpit, Chicago* | *Camaro, Tail of the Dragon* |
| ![From the 2001 Miata's seat on Ocean Drive, Miami Beach](docs/images/release-2.1.0/06-miata-ocean-drive-cockpit.jpg) | ![The 1965 Mustang on Ocean Drive at night](docs/images/release-2.1.0/07-mustang-ocean-drive-night.jpg) |
| *Miata cockpit, Ocean Drive* | *Mustang, Ocean Drive at night* |

More in [docs/images/](docs/images/).

## Known issues

- Small lips (6-12 cm) where some driveways and ramps join a road.
- Very long bridges (the 8 km Mackinac Bridge) can still throw the car at a map-tile border.
- Stacked motorway interchanges (the High Five in Dallas) may show steps between levels.
- In direct sun, dark jagged shadow marks can show on the Miata's dash behind the steering wheel.
- Full list per release: [CHANGELOG.md](CHANGELOG.md).

## Build from source

Needs Unreal Engine 5.8, Visual Studio 2022 Build Tools (C++ game workload) and [Bun](https://bun.com/). In PowerShell, editor closed:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' OSDSEditor Win64 Development -Project="$PWD\OSDS.uproject" -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\OSDS.uproject" -game
```

Tests: `powershell -ExecutionPolicy Bypass -File scripts\check.ps1` (add `-Filter OSDS.Vehicle` for one part). Options, captures and packaging: [docs/unreal-building.md](docs/unreal-building.md).

| Folder | What |
|---|---|
| `Source/OSDS/` | The game (C++): car physics, world, menus, tests |
| `helper/` | Map helper (Bun): turns public data into tiles |
| `data/` | Car data, car models, the bundled offline tiles |
| `scripts/` | Build, test, package, release |
| `docs/` | How it works, building, cars, research |

## Docs

For players: [Installing and PC needs](docs/install.md) · [Controls and menus](docs/controls.md) · [The cars](docs/cars.md) · [What's next (roadmap)](docs/roadmap.md)

For developers: [How the game works](docs/technical-deep-dive.md) · [How the code is organised](docs/unreal-architecture.md) · [Building from source](docs/unreal-building.md) · [Where the data comes from](docs/data-sources.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) · [Code of Conduct](CODE_OF_CONDUCT.md)

## License and credits

Code: [MIT](LICENSE). Data, imagery, models and fonts keep their own licences: [CREDITS.md](CREDITS.md), [docs/data-sources.md](docs/data-sources.md), [`licenses/`](licenses/), [Credits.txt](Credits.txt).

- Map data © [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors (ODbL), via [OpenFreeMap](https://openfreemap.org/) / [OpenMapTiles](https://openmaptiles.org/); search by [Photon](https://photon.komoot.io/); buildings also from [Overture Maps](https://overturemaps.org/) and the USACE National Structure Inventory.
- Elevation, aerial photos and land cover: USGS (3DEP), USDA (NAIP), LANDFIRE, MRLC (NLCD), Mapzen, NASA, Copernicus Sentinel-2. Tree heights: [CHMv2](https://registry.opendata.aws/dataforgood-fb-forestsv2/) by Meta and WRI, 2026, imagery © 2016 Vantor ([CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)), changed. Climate: Köppen-Geiger 1991-2020, Beck et al. (2023), CC BY 4.0. Weather: [Open-Meteo.com](https://open-meteo.com/) (CC BY 4.0) and NOAA.
- AI models: Stable Diffusion XL (textures), Real-ESRGAN, SatlasPretrain, Detectron2, run with ONNX Runtime ([details](CREDITS.md)).
- Cars: Sketchfab models under CC BY 4.0, among them the Mazda MX-5 (NB) by [niev](https://sketchfab.com/niev), the 1965 Mustang by Kristian LaGrange, the Corvette C8 by Hari Prasath R and the 1985 Camaro IROC-Z by Tony; the Mini Cooper S is the [CARLA](https://carla.org/) Team's model. Signal poles, roadside plants and wall textures: CARLA simulator content (CC BY 4.0). Textures: Poly Haven, Innerscene, OpenGameArt (CC0). Font: Overpass (SIL OFL 1.1).
- Built with [Unreal Engine](https://www.unrealengine.com/) and [Bun](https://bun.com/).

OSDS was called SimCruise until 2.0.3, and the code kept that name inside until 2.1.1; from 2.1.2 everything is OSDS, and the game moves old settings and the map cache from `%LOCALAPPDATA%\SimCruise` to `%LOCALAPPDATA%\OSDS` on its first start.
