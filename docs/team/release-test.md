# The release test

One full game test of a release candidate, for all the teams at once. The tools thread owns it.

## How we look

The owner saw faults in our frames that our checks passed (2026-11-14: building tops cut by the roof line in Detroit). Measurements find what we decided to measure; a person finds what is wrong. So every frame review follows these rules, from the release-8 review on.

1. **Fresh eyes first.** Before any checklist, look at the full-size frames (1920x1080, never only contact sheets or thumbnails) from the driver's seat, as a player, with no checklist. Write down everything a person would call wrong or unreal. Only then do the checklist pass.
2. **Real-place compare.** Each reviewed place gets a side-by-side of a game frame and a real picture of the same spot: the aerial photograph (`scripts\detail-sheets.py`: USGS Imagery Only / NAIP, turned so the travel direction is up), or a legal street-level photograph with its licence recorded (no Google Street View scraping). List the differences by size, biggest first.
3. **The owner's reports.** When the owner reports a fault, find it in our frames and show it back (path and crop). Never explain it away. If we cannot see it, ask for the frame and say "not found yet", never "not real".
4. **Blind review.** Once per release, one reviewer with no history (a fresh subagent, given only the frames under neutral names and "list what looks wrong or unreal", no hints) reviews the frames of three places. Compare its list with ours. Anything it finds that we did not is a test escape (rule 7).
5. **Texture and material classes.** Our geometry checks do not see these, so look for them by eye:
   - **Building top:** a window row crosses or touches the top edge of a wall; a building with no top band (parapet, cornice, roof edge).
   - **Cut patterns:** road markings cut or offset at tile edges; a texture pattern cut at a mesh edge (kerbs, walls, guard rails).
   - **Stretch:** a texture stretched on tall walls, steep ground or long thin faces.
   - **Visible repeat:** the same window, tree, brick or ground tile repeated so the eye sees the grid.
   - **Missing edge trim:** no parapet, cornice, kerb, sill or roof edge where a real one is.
   - **Wrong scale:** bricks, windows or doors against the car and the lane width.
   - **Flat colour:** one flat colour where the real surface has detail (towers, embankments, plazas).
   - **Light that is wrong for the time:** windows glowing white in daylight, a black or a clay-grey cabin, a black mirror.
6. **Slow look segments.** Urban places get a 10 s segment at walking speed (or stopped) beside buildings, with a look left and right on the right stick, so near facades are seen at full detail, not only at 40 mph. Every place with buildings gets a crop sheet of the six nearest buildings (wall tops included), judged by eye.
7. **Test escapes.** Every fault a person finds before our tests do is a test escape. Record it in [test-escapes.md](test-escapes.md): what it was, which run had it in frame, why we missed it, and which rule or check now catches it.
8. **The owner's screenshots are reference cases.** A fix is checked against the same spot and the same view.

## When

- After each change: `scripts\check.ps1 -Tier fast` only (no game, no GPU). See [dev-loop.md](dev-loop.md).
- At the cut-off, all lanes merge into one candidate (a commit or tag such as `rc-v2.0.3`, or `docs/team/cycles/rc.txt`). Then `scripts\release-test.ps1` tests that commit once. Do not start it on a commit that is not the candidate.
- One GPU test at a time on the PC: the script takes the `Global\SimCruiseGpuTest` lock for each launch (`scripts\EditorLock.ps1 -GpuTest`). Other GPU tests wait. The lock holder is in `%LOCALAPPDATA%\SimCruise\gpu-test-lock.txt`.

## The final test of v2.0.3

The owner (2026-11-14): "2.0.3 should look real and be bug free, test thoroughly" and "drive real". The final candidate has the fine detail, building walls with doors and shopfronts, and bridges at their real height above the roads below. This is the one thorough test; it runs once, on that candidate, visible, at Ultra, traffic off. `scripts\final-test.ps1` chains the editor part and the cooked part; the review steps are done by people (and a blind reviewer) in the order below.

| # | Part | What | Tool | About |
|---|---|---|---|---|
| 1 | Build and unit tests | The candidate plus the test-only commits; `check.ps1 -Tier fast`, the pre-flight and vehicle tests (`SimCruise.Gameplay.Preflight.*`, `SimCruise.Vehicle.*` incl. the cards against the published figures) | `final-test.ps1` | 25 min |
| 2 | Full Ultra test | Ocean Drive, Dragon, Nebraska, Big Sur, each with the 2001 Miata, the '65 Mustang and the Corvette (12 drives), plus 30 random US places with at least 3 in Alaska and 3 in Hawaii (`Get-ReleasePlaceDraw`: every place of `data/test-places.json`, then seeded points; a place with no map data must fall back cleanly, which is a test too); the cases (standstill, speed, fallback, eye on and off) | `release-test.ps1 -ShowWindow -AllCars` | 210 min |
| 3 | Bridges | Bixby (both approaches), Detroit Woodward over I-75 (north and south), Doraville Northcrest Road over I-285: deck smooth (no `deck-not-smooth`), grade, no step, parapets, the height above the road below, the car over the deck at speed with no hit, stuck or wheels off | `bridge-check.ps1 -ShowWindow` | 25 min |
| 4 | Drive real | Per car: the cockpit review at rest (wheel, gauges, mirrors, steering ratio and lock to lock against the car data); a keen run on the Dragon at the car's limit (`-SimCruisePreflightKeen`); the step-steer and the cards from the vehicle benches (headless; an in-game step-steer is not automated yet); feel notes from the videos: body roll in the bends, pitch on braking and on the throttle, the wheels' motion against the car data | `cockpit-review.ps1`, `launch-review.ps1 -ExtraArgs:-SimCruisePreflightKeen`, `check.ps1` | 40 min |
| 5 | Slow look | Detroit Woodward at 10 km/h beside the buildings: doors, shopfronts, wall tops, window rows, signs | `detail-review.ps1` (detroit-slow) | 10 min |
| 6 | Controller and names | Every menu with the pad only; the HUD and menu texts searched for the owner's name; the car's display name | `pad-review.ps1` | 10 min |
| 7 | Post-processing | Photo sheets, event clips, strips, the video scan, the blind folders | `detail-review.ps1 -PostOnly` | 25 min |
| 8 | Review | "How we look" 1-6: fresh eyes on full-size frames, the blind reviewer on every place, the photo compare, the video and motion review, the checklist, the table "found by the new method only" | people | 90 min |
| 9 | Cooked package | From the exact candidate hash, no local changes (a worktree of its own) | `package.ps1 -Configuration Development` | 50 min |
| 10 | Cooked gate | Dragon and Ocean Drive in the cooked game: longest frame 0.5 s or less, no "Preparing Shaders", the owner-name search of the package | `release-test.ps1 -Cooked -Cases cooked-drive` | 15 min |
| 11 | Timed quiet run | One cooked Dragon drive with nothing else running; its report must not say NOT QUIET | `release-test.ps1 -Cooked -Places dragon` | 10 min |
| 12 | Known faults | Every fault from this release (the test escapes, the blind reviews, the owner's reports) with its state: fixed and verified (frame or run path), open (owner lane), or accepted (who accepted it, why) | report `known-faults.md` | 20 min |

The owner's criteria for v2.0.3 (2026-11-14, 23:31Z): the whole game looks as real as we can make it, anywhere in the US; the cars drive realistically in every way; the UI and all controls work with an Xbox controller. The team's full list is in [cycles/v2.0.3.md](cycles/v2.0.3.md). So the final test also has:

- **Grades per part**, the parts and grades of the team's table ([meetings/2026-11-14-every-part-real.md](meetings/2026-11-14-every-part-real.md)): building walls, bridges, street things, trees and bushes, near ground, road surface, roofs, parked cars, sound, roadside, water, far land, sky and weather, picture, cabin, car outside, driving feel. Each part gets a grade (A to D, with + and -, as that table) with a game frame next to a real photo of the same kind of spot (the aerial for the ground, trees, roofs and roadside; a licensed street-level photo where we have one). Pass: no part at D, and each part one grade or more above its baseline. The baseline is that table (estimates of 23:28Z: walls, bridges and street things at D), checked against the frames in the release-10 (dfed34c) review (`grades-baseline.md` in that review: each grade confirmed or corrected, with its frame).
- **Random places under the same rules as the hero places.** Same drive length, same cars, the same review steps and grades; not a lighter check.
- **Every Xbox controller control.** Menus and HUD (`pad-review.ps1`: every control reached and used with the pad), and driving: steer, throttle, brake, gears and paddles, handbrake, look, lens and view, pause, map. Each one by an injected pad input, with its effect seen in the log (telemetry) and in a frame. The driving pad pass is new test code (to write before the final test).
- **Sound.** The game's own output is recorded during each review drive (the audio mixer's submix recording, a WAV in the run folder) and put on the video, so the engine note against the rpm, the tyres and the wind can be graded. New test code (to write before the final test).
- **The owner's faults.** Every fault the owner reported in this release, with the frame from the same spot and view after the fix, side by side with the owner's own frame (rule 8 of "How we look").
- **Early reviews.** Each candidate branch that turns green gets the new-method review early, visible, traffic off (release-10 for the look; release-11 for the bridges), so the final test finds few new faults.

**The installer gate** (the owner, 2026-11-14: "someone can download the installer from github on any pc and it installs the game and dependencies and they can play it"), after the cooked package (part 13, about 60 min):

1. Build the installer from the cooked Shipping package (`package.ps1 -Configuration Shipping -Zip`, `installer.ps1`).
   Static dependency check, no admin needed (also in every cooked gate): `python scripts\dep-check.py <package>\Windows` reads the import and delay-import tables of every shipped .exe and .dll and marks each needed DLL as shipped, part of Windows, the Visual C++ runtime, an old DirectX extra, or not found; `dependencies.md` in the package folder. A needed DLL that is neither shipped nor part of Windows is a FAIL unless the installer installs it. On 2026-11-14 (lane3's package): the game exe and 20 engine DLLs need MSVCP140, MSVCP140_1, VCRUNTIME140 and VCRUNTIME140_1, which nothing ships or installs.
2. Test it on the cleanest Windows we can get on this PC: Windows Sandbox if the feature is on (`Get-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM`, an elevated query; nobody turns a Windows feature on without asking, it can need a restart), else a new local Windows user with no Unreal, no developer tools and no game data.
3. As a user would: take the installer file under its GitHub name from a local copy (no release page), install, first start, the dependency steps (the Visual C++ runtime, DirectX, any first-run map or model download), drive one place with the controller, quit, uninstall.
4. Check what the uninstall leaves: only the saved preferences may stay (name them); list every other file left under `Program Files`, `%LOCALAPPDATA%\SimCruise` and the Start menu.
5. Write an honest minimum PC (GPU, video memory, RAM, disk, Windows version, internet for the map data), what happens below it (a clear message, or a crash), and what we could not test (AMD and Intel GPUs, Windows 10 against 11, no internet).

**Civil structures** (the owner, 2026-11-14 23:35Z: "every civil architecture needs to work"; part 3b, about 150 min). Each kind on or over the road has the right shape and is drivable: bridges with clearance, long water bridges, roads under bridges and in cuts, elevated roads, ramps, stack interchanges, road tunnels, roundabouts, footbridges, skyways, rail level crossings and rail bridges, multi-storey parking, retaining walls, cuttings and embankments; and clean fallbacks (no fault, no stuck car) for toll plazas, ferries and dams. `data/civil-places.json` (`scripts\civil-places.py <seed>`, the seed recorded) has, per kind, one example picked at random from OpenStreetMap across the US (Overpass, low rate, ODbL) with an approach point before each end, plus the world lane's example (`detail-staging/civil-structures.md`). Each random example is driven from both approaches, the world lane's from one, with the bridge and deck rules (deck not smooth, steps, grade, hit, stuck, wheels off, parapets, clearance) and a frame beside the aerial. A kind the game does not draw yet (tunnels, rails, toll plazas in v2.0.3) must still give a clean drive: no hit, no stuck car, no gap.

**Checks for the branches in the candidate** (world lane plan, 2026-11-15 00:24Z):

- **Trees** (trees step 1): crown size and position against the aerial and the canopy height map (CHM v2) in the photo sheets; no forest in the desert at Delta, UT (shrubs low, sparse, grey-green); no pop and a smooth detail fade in the video scan and the clips; the GPU rule (A/B with the old crowns, `-SimCruiseOldCrowns`).
- **Facades** (facades steps 1-3): doors and shopfronts on the street side, wall tops with a parapet, cornice or roof edge (no window row cut by the wall top), no single window texture repeated over a whole wall, no windows glowing white by day; the Detroit slow look and the building crops of every place with buildings; the GPU rule (A/B with the facade switch).
- **Server load** (server-load-fixes): the car placed in 60 s or less at every place (the placement table against the earlier baselines), no `START FALLBACK` line, the hang watchdog quiet (`longest_hang_s` under 10 s).
- **Bridges** (release-12, only if World.BridgeDecks is green by about 03:00Z): the bridge check and the civil structures above; else the Northcrest Road / I-285 warped deck stays an open P1 in the known-fault list.

**The GPU rule for new detail** (team meeting 2026-11-14): each new detail costs at most 0.3 ms GPU p95 at Ultra and has an off switch. `scripts\ab-detail.ps1 -Name <detail> -Place <road> -OffArgs <switch> -Runs 3`: the same place and views (the game's perf sampler, driver's seat and chase, 10 s each, GPU time from the GPU so the frame cap does not hide it), on and off alternating, 3 runs each, a quiet PC; the difference with its noise; PASS at 0.3 ms or less, FAIL above it and above twice the noise, NOT SURE otherwise. About 20 min a detail.

About 13 hours from the GO with the 30 places, the civil structures and the A/B runs (two nights), most of it GPU time on one PC; other lanes' GPU work waits (`C:\aiwork\GPU-BUSY`, `GPU-QUEUE`). A P1 in parts 2-6 stops the cooked part until the owner decides. The report has a summary, the per-team reports, the review md with its tables, and `known-faults.md`.

## A re-test

After fixes, test only what failed:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\release-test.ps1 -Places welch,stonington,detroit,doraville -Cases controller
```

`-Places` keeps the places whose name contains one of the words. `-Cases` keeps the named cases (a kind such as `controller`, `fallback`, `eye` or `cooked-drive`, or a full case name). With only `-Cases`, no place runs.

## Shaders: the editor build and the cooked package

An editor build compiles shaders the first time a place needs them, and a fresh worktree has an empty cache: Big Sur froze 33 s on "Preparing Shaders" in the 53b290c run. Editor runs therefore first make one short untimed launch per place (15 s, folder `warm\`); `-NoWarm` skips it. A freeze over 1 s in an editor run is a P3 TEST ENV finding when its frame shows "Preparing Shaders", not a game fault.

`-Cooked` runs the cooked package in place of the editor (`scripts\package.ps1 -Configuration Development`: shipped shaders and a log; the Shipping exe writes no log; `-CookedExe` gives another path). There a "Preparing Shaders" or a freeze over 1 s is a P1. The `cooked-drive` case drives the Dragon and Ocean Drive on the package for 60 s with an Unreal Insights trace (`trace.utrace`): no frame or freeze over 0.5 s, with the longest frame and its drive time in the driving, picture and release reports.

## The GPU

The release test owns the GPU while it runs: other lanes ask before they launch a game. Every game launch takes `Global\SimCruiseGpuTest` (`scripts\EditorLock.ps1`); the holder is in `%LOCALAPPDATA%\SimCruise\gpu-test-lock.txt` (empty when free) and in each `load.csv` row (`gpu_lock_holder`). `load.csv` also counts other lanes' builds (UnrealBuildTool, cl, link, shader workers) as load, and names each process's lane (`name#pid@lane`, from its command line); `summary.md` lists the other lanes with their first and last time.

`C:\aiwork\GPU-BUSY` holds one line while this lane uses the GPU (`holder=lane-view, start=<UTC>, end_estimate=<UTC>, purpose=<text>`); `release-test.ps1`, `start-compare.ps1` and `focus-check.ps1` set it and clear it in a finally block (`Set-GpuBusy` / `Clear-GpuBusy` in `scripts\EditorLock.ps1`). Other lanes call `scripts\gpu-wait.ps1` before a build or a game launch: it waits while the file is there (`-TimeoutMinutes`, default 120); a file whose end_estimate is more than 30 min past is stale, so it is ignored and logged.

**CPU-heavy builds also take the lock.** A build (UnrealBuildTool, cl, link, a cook or a commandlet) disturbs a timed run on the same PC as much as a game does, so a lane sets `GPU-BUSY` for its builds and its headless test runs too, not only for game launches.

**Turns (gpu-wait.ps1 of 2026-11-15, not deployed yet).** The stricter `scripts\gpu-wait.ps1` adds `C:\aiwork\GPU-TURN` ("holder=<name>, until=<UTC>"): a listed lane that reaches the front takes the turn for `-TurnMinutes`, its own later calls pass (the steps of one chain; `-Me` can come from `$env:SIMCRUISE_GPU_ME`), and every other caller waits; a caller that is not in a fresh queue, or passes no `-Me`, waits with "not in queue, waiting" until the queue empties or goes stale (30 min); `-Release` ends a turn early. It is standalone, so one shared copy (`C:\aiwork\gpu-wait.ps1`) can serve every lane. Deploying it and writing a turn were held back on 2026-11-15 (this session's permission check); the coordinator decides.

**The order.** `C:\aiwork\GPU-QUEUE` has one holder per line, first goes first. A lane that calls `scripts\gpu-wait.ps1 -Me <holder>` waits until the GPU is free and it is first, then takes its line out. The coordinator writes the order; while lanes are listed it is refreshed at least every 20 min (a queue not changed for 30 min is stale and ignored). The order for the night of 2026-11-14 (23:41Z): `menus-ui-build` (holds the lock now), `world-release12-build`, `picture-savings`, `world-gpu-profile`, `lane-ai-build-tests`, `lane1-build-drives`, `cleanup-privacy-fast`, `lane-view-release10-review`, `lane-ai-eye-run`, `lane-ai-wall-library` (4-6 h, last, only when the queue is otherwise empty).

## A quiet window

Run the release test only in a quiet window: no build, test or game from another lane. Load from other lanes added about 2 ms to the frame p95. The script samples the PC every 30 s into `load.csv` (`scripts\LoadWatch.ps1`): the other Unreal and SimCruise processes and the GPU use of other processes. When another Unreal or SimCruise process ran, or other GPU use averaged over 5 %, the summary says **NOT QUIET** and the frame-time rows in the picture and ai reports are marked NOT QUIET: they are not valid for sign-off.

## Run it

```powershell
powershell -ExecutionPolicy Bypass -File scripts\release-test.ps1
powershell -ExecutionPolicy Bypass -File scripts\release-test.ps1 -MaxPlaces 2   # a dry run of the script
powershell -ExecutionPolicy Bypass -File scripts\release-test.ps1 -OnlyCases fallback-ocean-drive,eye-off-ocean-drive,eye-on-ocean-drive   # only these cases
```

| Argument | Effect |
|---|---|
| `-Seed n` | The draw of random places and the car turn (default: the date, `yyyyMMdd`). The same seed gives the same places and cars. |
| `-RandomCount n` | Random US places from `data/test-places.json` (default 4). |
| `-Seconds n` | Drive time per place (default 60). |
| `-MaxPlaces n` | Stop after n places. Also skips the controller test. |
| `-ShowWindow` | Show the game windows (default: off screen, `-RenderOffScreen`). |

At the start, the script prints the places, the cars and a time estimate (about 5 min per place; 8 places in about 42 min). The limit is 90 min.

## What one launch collects

Each place is one game (`scripts\launch-review.ps1`) at the Ultra preset and the screen's own resolution (1920x1080 on this PC). In the same launch:

- the view check (what the world shows at the start),
- the cockpit steer test at rest (wheel axis, hub, rim, needles with the wheel hidden, lock to lock),
- the robot's drive for `-Seconds` with the pre-flight judge (spin, run-off, slow frames p50/p95, hitches, road steps and gaps, pop-in, stuck traffic, white sky),
- frames from the driver's seat every 5 s and at each event, with close-ups of the wheel, the cluster, the mirrors and the windscreen,
- a 60 s video at 10 fps, the contact sheet, the log, crash data and GPU data,
- the real graphics settings from the engine (the `REVIEW SETTINGS` log line).

Places: the four hero roads (Ocean Drive, the Dragon, Nebraska, Big Sur) and the random US places. The three cars (the 1999 roadster, the Mustang, the Corvette) turn round the hero roads, so each car drives each hero road in three runs. Each random place gets one car. Each random place drives at its own local solar noon.

Then the controller menu test (`scripts\pad-review.ps1`, automation test `SimCruise.UI.PadNavigation`), once. When the script is not there, the UI report says NOT RUN.

## Test cases with their own launch

The world lane makes the game side. The release test starts each case, applies the gates, and writes the results to the team reports. When the game flag is not in the build, the case is NOT RUN with the reason; the rest of the run continues.

| Case | Launch | The game writes | Gates | Teams |
|---|---|---|---|---|
| Standstill | Ocean Drive, `-SimCruiseStandstillTest` (`launch-review.ps1 -Case standstill`) | `standstill\t000.png`, `t005.png`, `t015.png`, `t060.png` (0, 5, 15 and 60 s after the stop), `wall.png` (1 m from a wall), `ground.png` (1 m from the ground) | Edge density and fine-detail energy (`scripts\detail-metrics.py`) rise at each step. The 60 s frame beside the place's reference photo (`side-by-side.png`) when one is in `data\reference\<road>\` (legal source only). | world, picture |
| Speed | Nebraska (straight), then the Dragon, `-SimCruiseSpeedTest` (`-Case speed`) | `speed\speed.json`: `{"runs": [{"road", "kmh", "ai", "lead_m", "lead_s", "holes", "popin_spikes", "frame_median_ms", "frame_p95_ms", "hitches_2x"}]}`, one run per speed (50, 130, 240 km/h), AI workers on and off in the same launch | No holes. No pop-in spikes on the road ahead. On the Dragon, frame p95 with AI on is not more than 0.5 ms above AI off. No extra frames over 2x the median with AI on. Frame p95 not above 50 ms. | world, ai, picture |

## The drive judge: road edge and run-off

The pre-flight judge (`Source/SimCruise/Private/Gameplay/SimCruisePreflight.cpp`, limits in `SimCruisePreflightRules`, `SimCruisePreflight.h`) has two road checks. A world-lane test must use the same rules to agree with it.

| Reason | Rule | Road data | Limit |
|---|---|---|---|
| `ran-off-<m>m-past-the-edge-for-<s>s` | The car's centre (the actor location, 2D) against the nearest point of the nearest heading-aligned line: the route lines the robot gathered this frame (`FindCursor` on the drive's `Lines`). Past-edge = distance to that centreline minus that segment's `HalfW`. No aligned line within 30 m counts as 30 m. | The road ribbons of the built tiles (`Build->Roads.Ribbons`: OSM centreline stations with their half width), service ribbons left out (`bService`). Not the drawn mesh. | more than `MaxOffRibbonM` = 2.0 m for longer than `MaxOffRibbonSeconds` = 0.5 s |
| `left-the-road-<s>s` | The surface under the car's centre: a vertical trace on the tile meshes (the point's tile and its 8 neighbours, highest hit), or a bridge deck. It is road when the hit face is on a road layer (`IsRoadLayer(Tile->LayerAtFace)`). | The drawn mesh's road layer. | not road for longer than `MaxOffRoadSeconds` = 1.0 s |

There is no launch grace: both checks run from the first drive frame (frame 0). The car's start pose comes from the start pick (`World/SimCruiseStartPick.cpp`). It takes public, non-service ribbons, and it requires each corner of the car to be within `HalfW - EdgeClearM` of its own ribbon's centreline. It does not check the drawn road layer or the route's first line.

So a start the start pick accepts can still fail the judge at frame 0 in two ways:
- The judge's nearest aligned route line is another ribbon (at a junction, the cross street, or a ribbon with a smaller `HalfW`).
- The drawn road layer under the car's centre is not road (a junction flare, a kerb or a verge drawn as non-road).

Welch, 2026-11-14 (a97af50): the car was placed at about 37.4329 N, 81.5840 W, heading -124.6 deg. At frame 0 the judge read 2.0 m past the edge. In the frame, the car's left side is on asphalt and its nose points at a grass bank, with the drawn road curving away to the left. So the run-off was mostly real: the start pose follows the ribbon, not the drawn road.

## The car's own state

The judge also watches our car every frame (`SimCruisePreflightRules`, tests `SimCruise.Gameplay.Preflight.CarState`). On 2026-11-14 a 64-to-1 mph crash into a concrete piece at a Doraville motorway bridge and 27 s at the rev limit passed the judge; the owner found it. Each event has a log line `Pre-flight: STUCK|HIT|DECK NOT SMOOTH|... wheels off the ground` and an event frame.

| Reason | Rule | Team, severity |
|---|---|---|
| `stuck-at-rev-limit-<s>s` | The engine above 80 % of the car's redline and the car under 1 m/s for more than 2 s (high-centred, wedged, wheels in the air). While it lasts, a wheel-contact line each second (contact and load per wheel, pitch, roll). | driving, P1 |
| `hit-<n>` | The speed fell by more than 6 m/s within 0.5 s (more than the hardest braking, about 1.2 g). | driving, P1 |
| `deck-not-smooth-<n>-max-<deg>deg` | On a road surface or a bridge deck above 3 m/s, the body's pitch or roll changed by more than 4 deg within 5 m of path (a hump, a lip, a warped deck). | world, P1 |
| `wheels-off-ground-<s>s` | Two or more wheels without contact on a road surface for more than 1 s. | driving, P1 |
| `hang-<s>s` | The hang watchdog (a thread of its own) saw no tick of the game thread for more than 30 s. After 10 s it logs `Pre-flight: HANG` with the game thread's stack; 10-30 s is the note `game-thread-stall-<s>s`. (2026-11-14: a Detroit start stood still 5 min with no log line.) | release, P1 |

## Traffic and planes: off

The owner took traffic and planes out of the game for now (2026-11-14, 20:31Z): the review time goes to detail from the driver's seat and to driving feel. Every pre-flight run has traffic off (`simcruise.Traffic.Cars` 0, set at the start and again when the drive starts); `-SimCruisePreflightTraffic` keeps it on for a traffic test. The review runs have `-SimCruiseNoLive`, so no live planes. The traffic judge below still counts, but only as **information**: `preflight.json` `notes` and the `PREFLIGHT CAR ... notes=` line, never a FAIL.

## The traffic judge (information only)

Ten times a second the judge looks at each traffic car in our car's frame (Along: metres ahead of our centre, Side: metres to the side, centre to centre; heading relative to ours). Limits in `SimCruisePreflightRules` (`SimCruisePreflight.h`), tests in `SimCruise.Gameplay.Preflight.Traffic`. Each event has a line `Pre-flight: traffic ...` in the game log (the car's name, where it was) and an event frame in `review\`.

| Reason | Rule | Team, severity |
|---|---|---|
| `traffic-cut-in-<n>` | Our car drives at 2 m/s or more. The other car is in our lane (Side under 1.8 m), with at most one car length (4.5 m) from our front bumper to its rear (our half length 2.2 m). It came across (heading more than 30 deg from ours) or from the next lane (Side over 2.5 m one sample before). One count per car in 5 s. At a stop line (our car below 2 m/s) the crossing traffic has priority and nothing is counted. | driving, P1 |
| `traffic-through-our-car-<n>` | The bodies overlap: \|Along\| under 2.2 m + half its length - 0.3 m, \|Side\| under 1.6 m, height within 2 m. | driving, P1 |
| `traffic-turn-round-<n>` | The car's heading changes by 150 deg or more within 3 s (unwrapped through 180 deg), while it moves (over 0.5 m/s). A car removed and placed again (more than 30 m in one sample) starts a new history. | driving, P2 |
| `motorway-traffic-at-grade-<n>` | A car on a `motorway` ribbon is across our road (45 to 135 deg), on it (\|Side\| under 8 m, -10 to 60 m ahead), at our level (within 2.5 m). Motorways never cross a road at grade; an overpass is metres above or below. | driving, P2 |
| `junction-hold-<s>s-of-<n>s` | Only with `-SimCruisePreflightStopAtJunction=N` (`launch-review.ps1 -StopAtJunction N`): the robot held less than N s at a stop line, so the gap-acceptance case did not run. | release, P2 |

The gap-acceptance case: with `-SimCruisePreflightStopAtJunction=N` the robot looks up to 60 m ahead along its route for the first point inside a crossing carriageway of a road its own road gives way to (the traffic's rule, `SimCruiseLanes::ApproachStops`: a higher rank, or an equal rank that is not a major road). It stops `StopLineSetbackM` (5.25 m) before it, holds N s on the parking brake, then pulls out without waiting for a gap. The traffic must leave it a full gap: no cut-in and no car through ours. `scripts\traffic-retest.ps1` runs the traffic cases (Detroit Woodward north and south, Doraville, one junction; Bixby south with `-WithBixby`).

## Graphics rule

Review launches turn dynamic resolution off (console priority), so the 3D picture stays at the preset's screen percentage (75 for Ultra, from the preset) and the frame times are its honest cost; `-SimCruisePlayerRes` keeps the player's dynamic resolution. A launch is valid for sign-off only when the engine really had the top preset (Ultra), all `sg.*` levels at the top, the screen's own resolution, and a screen percentage, set and measured each frame, not below the preset's. A frame p95 over the budget at the fixed percentage is a FAIL for the picture and world teams, not a test fault. Else the run is **NOT VALID FOR SIGN-OFF**: the picture report has a P1 finding, the summary says so, and `scripts\review-check.ps1` and `scripts\playtest.ps1` refuse it.

## Output

`Saved\ReleaseTest\<stamp>-<commit>\`

| File | Contents |
|---|---|
| `summary.md` | The verdict, a row per place (car, verdict, drive judge, cockpit, view check, frame p95, minutes, link to review.md), and the verdict of each team. |
| `teams\driving.md` | Physics and the robot judge: spin, run-off, the robot did not move; rear slip and steering numbers. |
| `teams\cockpit.md` | Steering wheel and gauges: the cockpit test and the gauge samples from the drive. |
| `teams\picture.md` | Frame times, hitches, white sky, the real graphics settings. |
| `teams\world.md` | Road steps and gaps, pop-in, stuck traffic, the start place, the load time, the view check. |
| `teams\ai.md` | The detail engine's AI workers: frame cost with AI on against off, detail lead at speed. |
| Fallback ground | Ocean Drive, `-SimCruiseFailTiles=18180_27907,18181_27907` (the two tiles north of the start fail; `launch-review.ps1 -Case fallback`) | the log: `Forced tile failures`, `Fallback ground under tile` | The failure takes effect, fallback ground is laid, the drive judge passes, no road gaps; the last frames listed for the review (no hole or void). | world |
| Eye fusion | Ocean Drive, a live drive with `-SimCruiseScanEye=satlas_landcover` (`-Case eye`) and the same drive with the eye off (`-Case eyeoff`). The dev model is read in place from `-EyeModelDir` (default `D:\sc\lane-detail\Saved\AI`) through a junction at `Saved\AI`; it is never copied into the repo. | the log: `Scan eye ready`, one `SCANEYE` line per tile read | The eye starts and reads tiles; frame p95 with the eye on is not more than 0.5 ms above eye off; no extra hitches; frames listed for the detail layer. | world, ai |
| No ensure with the eye on | the eye-on log | | No `Ensure condition failed`, `ensure(`, assert, fatal error or crash: any hit is a P0 for ai. | ai |
| `teams\ui.md` | The controller menu test. |
| `teams\release.md` | Crashes, hangs, engine errors, a drive that did not happen. |
| `<place>\` | One launch: `review\` frames and JSON, `cockpit\`, `video.mp4`, `sheet.png`, `game.log`, `crash\`, `review.md`. |

Each team report has the same four sections:

1. **Verdict**: PASS, FAIL or NOT RUN.
2. **Findings**: a table; each row has the severity, place, car, time, evidence path and what is wrong.
3. **Frames to look at**: the frames the team must open.
4. **Numbers vs limits**: a table of measure, value, limit and result.

Severity:

| Level | Meaning |
|---|---|
| P0 | Stops the release: a crash, a hang, a drive that did not happen, a start at the wrong place. |
| P1 | A player sees it at once: a spin, a run-off, slow frames, a wrong wheel or gauge, NOT VALID FOR SIGN-OFF. |
| P2 | A player can see it: road steps, pop-in, stuck traffic, gauges off the true value. |

## A visible run

`-ShowWindow` runs each game in a borderless window at the screen's size (`-FULLSCREEN`, Unreal 5's windowed fullscreen), so the owner can watch. `scripts\ViewerWatch.ps1` logs the foreground window and the PC's last input every 250 ms into `viewer.csv`. A place where the game lost the foreground is **DISTURBED**: its frame times do not count (its frames, video and judge do), and it is run again off screen for the frame times only (`rerun off screen`; a window costs about 1 ms of p95 against off screen).

Known fact (2026-11-14, `scripts\focus-check.ps1`, repeated on a quiet PC at 13:30Z): with the focus on another window, frame p95 rose from 23.2 to 29.2 ms (+6.0 ms) on Ocean Drive at Ultra 75 %; no frame over 50 ms, no hitch. (The first check, 17.3 to 24.6 ms, +7.3 ms, ran while other lanes built and tested.) Frame p95 with the same settings varies between runs on this PC (16.7 to 23 ms), so compare runs only side by side. The process priority stays normal, as for a player. Input rows in `viewer.csv` are information only: on this PC something simulates input about every 50 s with nobody there.

## Off screen gives the same picture as a window

Proof, 2026-11-14 (`Saved\Review\offscreen-proof`): two launches of Ocean Drive with the 2001 roadster, Ultra, 1920x1080, one off screen (`-RenderOffScreen`) and one windowed. The start frames at the same pose differ by a mean of 1.84 of 255, with 0.79 % of pixels changed by more than 25 (the level of TSR noise between two runs). Mean luma was 77.3 against 77.4 (the same exposure), and sharpness (Laplacian variance) 851 against 872 (the same anti-aliasing and motion blur). Frame p50 was 17.5 ms off screen and 18.5 ms windowed: the window costs about 1 ms to present. The two drives then diverged because traffic is random per launch (a car crossed in front of the windowed run at 12 s), so only the start frames are compared.

## The frame review

The script's checks are not the review. A person or the thread must open the frames and the video of each place and fill its `review.md` (each item PASS or FAIL with a note). Then:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\review-check.ps1 "Saved\ReleaseTest\<stamp>-<commit>"
```

It fails when a `review.md` has an empty item or a FAIL.
