# Changelog

The short version of each release, newest first. Every release has full notes in [docs/release/notes/](docs/release/notes/). OSDS was called SimCruise up to 2.0.3 and Sunday Driver up to 1.0.8 ([history](docs/history.md)).

## 2.1.0 (2026-11-17)

**Two new cars, a drift handbrake, and CARLA art.**

- **New cars:** the **MINI Cooper S** (2015 F56, 6-speed manual, from the CARLA simulator's model) and the **1987 Camaro IROC-Z** (5.7 V8, automatic). Both calibrated against magazine tests.
- **Handbrake you can drift with:** it ramps on, locks the rears and works the clutch for you. Electric parking brakes (Corvette, Model 3) can't, as in real life.
- **Tyre pressure** per axle in the tuning menu (18-40 psi).
- **The Miata sounds like a Miata:** real exhaust and intake resonances, louder with revs, no pops on a stock car.
- **CARLA art:** traffic-signal poles and mast arms, roadside grass and bushes chosen per climate, brick and weathered concrete on buildings.
- **Trees pick their species from the aerial photo;** no more pines in Phoenix.
- Known issues: the Camaro has no traction control, as the real one did not: floor it and it spins (the test robot spun it on the Mackinac Bridge), so ease on; its model has no cabin (chase view from the seat); an occasional jolt at a map-tile border.

[Full notes](docs/release/notes/2.1.0.md)

## 2.0.4 (2026-11-17)

**Driving you can trust.**

- **The car holds its lane.** The front wheels point dead straight (they were always 0.03° off, so the car hunted); small road lips no longer push the tyres sideways; the driver's view stops swinging with every correction.
- **Stability control (ESC)** on the cars that have it in real life (Corvette, Model 3, F-150 Raptor, Grand Cherokee, Odyssey, CR-V): it brakes one wheel and eases the engine when the car starts to slide; the ESC light flashes. All six pass the US legal ESC test (FMVSS 126). Pause menu > Your car turns it off. The 2001 Miata and the 1965 Mustang never had it.
- **Manual with an automatic clutch** (new default): tap RB/LB to shift; the game works the clutch and blips the throttle. A rumble tick and a gear flash with each shift; a refused shift says the speed that gear can take.
- **Bridges are flat across**, the same in every map tile; a road under a bridge is no longer pulled up to it; no ground poking through the road at the Doraville test place (60 spots to 1).
- **Buildings from real data**: measured heights, what each building is and its storeys (Overture, the US National Structure Inventory), and the roof shape, wall colour and material mapped in OpenStreetMap.
- **Looks:** shadows lifted the way real cameras do; the AI-sharpened aerial photo fades in instead of popping; sealed cracks on older asphalt; roofs follow the map.
- **Bug hunt across the US**: the robot drives random places and 12 hard ones. Fixed: the car put in the lake instead of on the Mackinac Bridge, a building across the motorway there, bridge decks dipping at map-block cuts, hard launches stuck on the limiter, the Corvette skipping 1st to 3rd, `main` not building.
- Known issues: the Mackinac Bridge can still throw the car at one tile border; stacked interchanges (the High Five) may show steps between levels; small lips where some driveways join roads.

[Full notes](docs/release/notes/2.0.4.md)

## 2.0.3 (2026-11-15)

**A world that looks more real.**

- Trees take their colour from the aerial photo. Walls are brick, siding, block, stone, panels or metal. Windows and doors follow the wall.
- Each wheel knows its surface (asphalt, concrete, gravel, grass, kerb); you hear and feel it.
- Engines without a turbo lose power at altitude. The automatic shifts by road speed.
- Every menu works with the Xbox controller alone.
- About 2 ms of GPU back on Ultra. Sharper HUD digits.
- Moving traffic and aircraft are off for now; parked cars stay.
- The installer works on a clean PC (Visual C++ runtime, GameInput).
- Known issues: some bridges meet the road below at the same level; small lips where driveways join roads; ground over a lane edge at ~300 spots.

[Full notes](docs/release/notes/2.0.3.md)

## 2.0.2 (2026-11-13)

**A GPS and a new look.**

- GPS routes on the real roads, with a turn sign and a round map on the HUD.
- New pause menu: plan a route, famous roads, "Where to?".
- Seat & view menu, hood and far-chase cameras, look around with the right stick.
- Fixes: a car off the road no longer explodes; no road step at tile borders.

[Full notes](docs/release/notes/2.0.2.md)

## 2.0.1 (2026-11-13)

**Fixes from the first play test.**

- A less bright, more natural picture and a better field of view.
- Settings work with the pad; your own stick and trigger settings.
- The Miata's rear brakes no longer lock first; engine braking includes pumping loss.
- AI models left out of the download (smaller install).

[Full notes](docs/release/notes/2.0.1.md)

## 2.0.0 (2026-11-12)

**The first full Unreal Engine release.**

- Go anywhere in the US: roads, terrain, buildings and trees built from open data as you drive.
- Cars calibrated to magazine tests, with a data-backed tyre (TMeasy), aligning torque, live air and wind.
- Live weather, sea and tide; traffic that stops at lights; USB wheels.
- Tune your car; the game remembers your choices.

[Full notes](docs/release/notes/2.0.0.md)

## 2.0.0-preview.1 (2026-11-11)

**First Unreal build.** Ocean Drive in the 2001 Miata on Chaos physics, with traffic, engine sound, mirrors, parked cars and night lights. [Full notes](docs/release/notes/2.0.0-preview.1.md)

## 1.1.0 (2026-11-09)

**Last Godot release.** Mountain roads, bridges, five cars as data, Windows builds. The Godot code is at the tag `godot-final`. [Full notes](docs/release/notes/1.1.0.md)

## 1.0.8 and earlier (Sunday Driver)

See [docs/history.md](docs/history.md).
