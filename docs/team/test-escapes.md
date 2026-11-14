# Test escapes

A test escape is a fault that a person (the owner, a player, a blind reviewer) found before our tests did. Each row says which run already had it in frame, why we missed it, and what now catches it. The rules are in [release-test.md](release-test.md), "How we look".

| Date | Fault | Found by | Already in frame | Why we missed it | Now caught by |
|---|---|---|---|---|---|
| 2026-11-14 | **Building tops:** the top window row of low brick buildings is cut by the roof line (it reads as battlements), or the windows touch the wall top; no building has a parapet, cornice or roof edge. Woodward Avenue, Detroit. | Owner (screenshots, 19:30Z and 20:23Z) | c1a6b76 `bridges\detroit-woodward-south\review\000-000.1s-start.png`; e9db211 `release7-traffic\detroit-woodward-south\review\000-000.1s-start.png` (crops in `release7-traffic\building-tops\`); c1a6b76 Detroit north video frame 444 (44.4 s) | We looked for missing geometry (open boxes, pop, near-plane cuts) and said "no fault", then blamed the windscreen frame. We did not look at the texture on the wall at full size. | "How we look" rules 1, 3, 5 (building top) and 6 (crop sheet of the six nearest buildings). Suggested test for the world lane below. |
| 2026-11-14 | **Freeway traffic at grade:** cars on I-75 cross Woodward Avenue at road level. | Owner ("cars arent following traffic") | c1a6b76 Detroit south, video 51 s (`aaron-1930\south-51s.png`) | The judge had no traffic checks; we watched our own car only. | The traffic judge: `motorway-traffic-at-grade-N` (and cut-in, through our car, turn-round). |
| 2026-11-14 | **Pad menus: the top row** (the pause menu's tabs) could not be reached with the controller. | Owner | Pad review frames before e639014 | The pad walk started below the tab row. | `scripts\pad-review.ps1` walks every control from the top row; PADNAV/PADWALK lines. |
| 2026-11-13 | **Steering wheel and gauges:** the Miata's horn pad swung at full lock; the tachometer lagged. | Owner | Cockpit frames of the release test | The rim check measured one ring; nobody compared the needles with the true values. | Cockpit rim measure (outer edge, 64 sectors). The needles: the sidecars carry `speed_true`/`rpm_true`, but `speed_needle`/`rpm_needle` are still empty, so the needle check is NOT automatic yet (open). |

## Blind review, 2026-11-14 (c1a6b76: Detroit north and south, Doraville Miata and Corvette, 16 frames)

A fresh reviewer with only the frames (neutral names, no hints) listed these, which our review had not reported. Each is a test escape until a lane confirms or rejects it. Frame paths are under `D:\sc\lane-rc2\Saved\ReleaseTest\final-c1a6b76\`.

| Fault (reviewer's words, shortened) | Reviewer's severity | Frames | Lane |
|---|---|---|---|
| Jagged grey terrain shards, spikes and mounds beside the highway | high | `doraville-corvette\review\011-025.3s-timed.png`, `doraville-miata\review\005-010.4s-timed.png`, `bridges\detroit-woodward-south\review\013-055.4s-timed.png` (a grey-green mound where the sidewalk should be) | world |
| Black jagged streaks across the binnacle and the steering column | high | `doraville-miata\review\024-052.3s-jolt.png` | picture/cockpit |
| A street light pole floats in the air, see-through and dithered (a fade/LOD fault) | medium-high | `bridges\detroit-woodward-south\review\006-020.2s-timed.png` | world |
| The Miata cabin turns flat clay-grey on the motorway, near black in town | medium-high | `doraville-miata\review\005-010.4s-timed.png` against the Detroit frames | picture |
| Highway buildings drawn as a white "checkerboard" or battlement wall | medium | `doraville-miata\review\012-023.0s-*.png`, `019-045.6s-timed.png` | world |
| Windows glowing white in daylight | low-medium | `bridges\detroit-woodward-north\review\011-055.3s-timed.png`, `bridges\detroit-woodward-south\review\013-055.4s-timed.png` | world |
| Untextured dark-grey towers in the distance | medium | Detroit frames 05-08 | world |
| Road markings on sidewalks and verges, a yellow line across the lanes | medium | `bridges\detroit-woodward-south\review\006-020.2s-timed.png`, `doraville-corvette\review\011-025.3s-timed.png` | world |
| The minimap covers the dash clock and the Corvette's screen | low-medium | most frames | ui |
| The HUD speed digits fade between frames | low-medium | Detroit 040 s, Doraville | ui |
| Mirrors blocky and soft; the Corvette's inside mirror is cut by the screen edge and once tilted | medium | most frames; `doraville-corvette\review\005-010.2s-timed.png` | picture |
| A wrong badge on the Miata's wheel hub (reads "ND") | low | Detroit frames | cockpit |
| The automatic holds first gear to 6150 rpm at 28 mph | high (reviewer) | `bridges\detroit-woodward-north\review\008-040.2s-timed.png` | driving: the robot was at full throttle (ROBOT thr 1.00), so this can be a real full-throttle shift point; to check against the car data |
| A traffic car sideways across the road or on the sidewalk; a car in the mirror facing the wrong way | medium | `bridges\detroit-woodward-north\review\005-025.2s-timed.png`, `bridges\detroit-woodward-south\review\003-010.5s-*.png` | driving (traffic judge) |
| No pedestrians; identical street lights; repeated trees | medium | all | world (scope) |

## Suggested automatic test for the building tops (world lane)

In the facade code (the window grid on a wall, `SimCruiseTileDetail.cpp` near the `Floors` count, and the facade material), for every wall: the top of the top window row must be at least a top band below the wall top, for example 0.6 m for a flat roof (a parapet) and 0.3 m under eaves, and the band must be drawn (a parapet, cornice or roof edge). A test over the bundled and recorded tiles: for each building, `WallTopM - (WindowRows x FloorM + FirstSillM + WindowM) >= TopBandM`, and no window texel row crosses the wall's top edge in UV space. The world lane owns the fix; this test only suggests the check.
