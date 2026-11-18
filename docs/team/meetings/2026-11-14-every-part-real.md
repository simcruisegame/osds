# Team meeting: every part of the game looks real (2026-11-14 23:28Z)

Owner's asks: "2.0.3 should look real and be bug free, test thoroughly" (23:14Z); "the whole game needs to look real, thats why we discussed finding a way to use AI to create the details", "every part of the game" (23:27Z); "lets all meet as a team" (23:28Z). The owner took part in the thread. Context: release-10 dfed34c green; release-11 (smooth bridge decks) in build; traffic and planes out; GPU 90-96 % at Ultra on the RTX 2070 Super Max-Q; about 12 CPU threads free.

## 1. Every part, graded from the driver's seat (estimates from test frames)

| Part | Grade | Today |
|---|---|---|
| Building walls | D | One window grid; no doors or shopfronts |
| Bridges | D | Smooth decks in test; no real clearance over the freeway |
| Street things | D | One street-light model; signals at too many crossings |
| Trees and bushes | C | Real crown positions; 3 tree meshes; no bushes |
| Near ground | C | Colours from the aerial photo; one pattern everywhere |
| Road surface | C | One asphalt; no patches, cracks, wear |
| Roofs | C | Photo colours; simple shapes |
| Parked cars | C | Few models |
| Sound | C | One V8 voice for 2 cars; no ambience |
| Roadside | C+ | Rural ditch and cut; small junction lips |
| Water | C | Flat |
| Far land | B- | Real terrain and photo; flat and soft |
| Sky and weather | B- | Haze from sea level |
| Picture | B- | Exposure fixed; still soft |
| Cabin | B- | Miata dash shadow marks in sun |
| Car outside | B | Real models |
| Driving feel | B | 3 cars run the Dragon with no spin |

Each team also described the game in one or two sentences (thread); the owner's pick is the measure for the grades.

## 2. Method for each part (research-scientist, AI lane)

Per tile on CPU, cached in the scan map, any US place with a fallback, makers' licences (owner's rule 01:56Z).

| Part | Method | Where data is missing |
|---|---|---|
| Near ground, roofs | AI eye (aerial_student, Satlas ODC-BY, 12 classes, 0.5 s/tile) sorts the NAIP photo; PHTX cuts patches of each class and synthesises a seamless texture (image quilting, Efros-Freeman 2001; Heitz-Neyret 2018). Prototype: 5-10 s/tile; real for forest, plain for lawn, grids of copied markings on man-made surfaces, so PHTX is for natural ground only; fine 1 m detail must come from generated materials | Sentinel-2 colour plus the class detail kit |
| Far land | AI eye tint on NAIP; Sentinel-2 beyond | NLCD / WorldCover (AK, HI) |
| Building walls | Facade grammar on OSM/Overture use, levels, shop tags: storeys, street-side door, shopfronts; wall material library (SDXL outputs, OpenRAIL++-M, cleared, plus CC0 MatSynth) picked by use, era, region, tinted to the place; about 4-6 h GPU on the PC plus a day to wire | Look from road class and land use |
| Bridges | Clearance from 3DEP lidar class 17 minus bare earth (2.0.4); design minimum about 4.9 m over freeways as the rule now | Rule clearance |
| Trees, bushes | Bushes from CHMv2 0.5-3 m; species group from USFS TreeMap / LANDFIRE EVT | Ecoregion priors |
| Street things | 3-4 CC0 light models by road class, region, era; signals only at OSM nodes | Spacing rule by road class |
| Road surface | Tint from NAIP road pixels; HPMS pavement type; cracks and patches 2.0.4 | State average, then default |
| Water, sky | Water colour from NAIP; cloud amount from Open-Meteo | Region default, climate normal |
| Picture | One colour look per kind of place from licensed photos (2.0.4) | Neutral |

## 3. GPU budget (performance-engineer)

Ultra measured 21.5 ms GPU (45 fps), about 5 ms over 60 Hz. Savings (estimates): moving shadow pages 0.5-1.0 ms; Lumen GI High under Ultra 1.0-1.5 ms; mirror at half resolution 0.5-0.8 ms (and even pacing); reflections max roughness 0.4 0.5-0.8 ms; foliage without shadow and wind past 92 m 0.3-0.6 ms. Input delay: frame cap and a 15 ms dynamic-resolution budget, expected 55-65 ms at Ultra (from 96 ms). New detail goes into baked per-tile CPU textures first, then HISM with impostors; no new material layers, no new tile-mesh geometry. Rule: each new detail costs at most 0.3 ms GPU p95 at Ultra and has an off switch; measured with `-OsdsDrivePerf`, seat view, two runs on and two off.

## 4. v2.0.3 scope and order

Owner (23:31Z): "the entire game looks as real as you can make it, be innovative, utilize AI ... they can go anywhere in the US"; "the cars drive realistically in every way"; "the UI, controls all work with xbox controller"; the rest of the criteria are ours. So 2.0.3 waits for the wall library and the fine detail. Criteria and the order of work per lane: `docs/team/cycles/v2.0.3.md`.

## 5. How we prove it

- 30 places drawn from `data/test-places.json` plus the hero roads, at least 3 in Alaska and 3 in Hawaii; Ultra; frames read beside a real photo of the same place.
- PHTX: colour difference ΔE00 below 6 against held-out NAIP patches; no visible repeat within 50 m (autocorrelation peak under 0.3).
- Bushes: F1 of 0.6 or more against 3DEP canopy 0.5-3 m.
- Bridge clearance: ±0.5 m of the lidar value where lidar exists.
- Doors: 90 % or more on the street-facing side.
- Each new detail: GPU cost rule above.
- Blind frame review (review lane, `review/how-we-look`) before any release.

## Decisions and owners

1. v2.0.3 waits for real walls, doors and shopfronts, photo-true natural ground, fine ground detail, bushes, bridge clearance and the GPU savings. Owner: coordinator.
2. 10 release criteria (3 from the owner, 7 from the team) in `docs/team/cycles/v2.0.3.md`. Owner: coordinator.
3. PHTX is for natural ground only; man-made surfaces use generated materials. Owner: AI lane.
4. SDXL outputs are cleared under the owner's licence rule (the makers' licence permits it); the wall library is made on the PC GPU outside GPU tests. Owner: AI lane.
5. Every new detail: 0.3 ms GPU p95 at Ultra at most, with an off switch. Owner: each lane; measured by the review lane.
6. Proof: a game frame next to a real photo for each graded part; no part stays at D. Owner: review lane.
