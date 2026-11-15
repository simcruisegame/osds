# World knowledge graph: teach the AI what a real place contains (design v3, 14-15 November 2026)

Innovation team, for the owner:
- 23:36Z "create a knowledge graph of real world materials, other real world things and teach the AI how to create the 3d overlay and a global overlay of detail that imagines the details that would be there, like that or no?"
- 23:37Z "how else would we generate fine detail? other than context engineering a guess"
- 23:40Z "research and come up with something" and "always think of gotchas"
- 23:56Z "are we going to be dosing a bunch of public servers especially when it scales?" (section 10)
- 23:59Z the "code per pixel, add detail from it, then a layer over the whole image" idea (section 11)

First-step files (graph tables and learning scripts): `research/innovation/world-graph/` (see its README.md).

Read with: `world-detail/scan-map-spec.md` (scan map v1 + v2 sections A-L, J2, J3), `research/innovation/hyper-detail-proposals.md`, `data/knowledge/place-graph.json` (the place graph on main: facts `kind:value` with a confidence, rules `if -> then` with weights, a `spread` rule that gives one block one style).

Fixed limits (v2.0.3): each detail at most 0.3 ms GPU p95 with an off switch; generation per tile on the CPU ahead of the car (AI eyes up to 8 threads and 8 GB while driving); cached in the scan map; deterministic per tile; no per-frame AI repaint; each detail has a cause in data or a stated probability rule; legal data and weights, with credits (rule F: the publisher's weights licence is the gate; training data is recorded).

Source marks: **[V]** read today; **[S]** headline or search summary only; **[K]** known reference, not re-read today.

---

## 1. Short answer

**Yes, the owner's idea is right, with one change that makes it much better.** The AI must not imagine from nothing. It must:

1. **See** what can be measured at this place (lidar, aerial photo, map, inventories, the eye).
2. **Know** what real places like this one contain, from statistics learned where open data counts things well (the world knowledge graph).
3. **Imagine** only the rest, by sampling from "what we know" multiplied by "what we see", so each guess is a probability with a source.
4. **Select and adapt** a library material or object, with its own colour, wear and variation.
5. **Check** itself against held-out real data and against street photos, and learn from the error.

Our name for it: **calibrated imagination**. A guess is allowed only where nothing measures, it never contradicts a measurement, and its probabilities are tested to mean what they say.

**The most important idea in this note:** for walls, fences, mailboxes and the like, the player cannot check each house, but sees the mix at once. A street where 60 % of houses are brick and 40 % siding looks right even if half the single guesses are wrong; a street where every house is the same "most likely" material looks fake even if it is "accurate". So we optimise and test the **mix and its lumpiness**, not only top-1 accuracy (section 5.4).

---

## 2. All the ways to get fine detail (the owner's second question)

An informed guess is only one of eight ways. The rule: **use the highest method in this list that has data for this place; guess only what nothing measures.**

| # | Method | What it gives | US coverage | Licence | Right tool for | Fails where |
|---|---|---|---|---|---|---|
| 1 | **Measure in 3D**: airborne lidar (USGS 3DEP) | Ground shape, cuts, ditches, kerb steps (coarse), crowns, poles as point columns, building heights, roof planes | Most of the lower 48; gaps in parts of the West; Alaska partly IfSAR only | Public domain | Shapes and heights; vetoing wrong guesses | Objects under ~1 m; colour; dense canopy |
| 2 | **Measure from above**: aerial photo (NAIP 0.6 m; our restored RSTR pages) | Real driveway stains, roof patches and colour, crop rows, parking paint | Lower 48 and Hawaii (NAIP); Sentinel-2 10 m elsewhere | Public domain; Copernicus terms for Sentinel | Flat surfaces seen from above; roof material | Walls, anything vertical, under trees |
| 3 | **Measure from the street**: street photos (Mapillary, Panoramax, KartaView, Wikimedia Commons) and Mapillary's detected map features (poles, hydrants, street lights, benches, manholes, bins, signs) | Wall material and colour, fences, mailboxes, signs; positions of roadside objects | Patchy: good in many towns and main roads, poor on rural lanes | Images CC BY-SA; Mapillary says its API map data is free for personal and commercial use under its terms [V] | Facts about the vertical world where photos exist; **learning and testing the statistics** | Most rural roads; old photos |
| 4 | **Read an inventory**: government lists | Bridges (NBI), rail crossings and their gates (FRA), buildings (NSI: construction type, stories, foundation height), road condition (HPMS), city trees, hydrants, street lights (city portals) | National for NBI, FRA, NSI, HPMS; city by city for the rest | Mostly public domain; each city portal recorded | Exact things at exact places | Between cities; rural counts |
| 5 | **Follow the standard**: written rules that builders obey | MUTCD sign types, sizes, heights, placement; road cross-sections; USPS curbside mailbox height and setback; hydrant spacing ranges | Everywhere | MUTCD public domain; codes used as published numbers only | Signs, road edges, anything regulated | Old or private roads |
| 6 | **Simulate the cause**: process rules | Dirt where water runs, gravel where tyres throw it, cracks by age and frost, moss on the wet shaded side, leaf drifts, rust streaks | Everywhere | Our code | Wear and weathering (hyper-detail proposal 3) | Things placed by people's choices |
| 7 | **Learned statistics (the informed guess)**: this graph | How many mailboxes, poles, hydrants per 100 m; which wall, fence, roof material for this region, era and use | Everywhere, with a confidence | Counts from ODbL and public data; tables are ours (ODbL where OSM-derived) | Cultural and random things | Anything unique |
| 8 | Generated appearance (GENR offline, library) | The look inside a surface | Everywhere | Model weights licence, recorded | Texture after 1-7 chose what the surface is | Choosing what is there (it must not) |

**Never imagined:** names, brands, logos, landmarks, murals, shop signs, house numbers, anything unique. They come only from map data or a licensed source.

---

## 3. What others do (research, current sources)

| Work | What it does | Numbers | Licence | What we take |
|---|---|---|---|---|
| Microsoft Flight Simulator + Blackshark.ai | Cloud ML turns aerial photos into 1.5 billion buildings (footprint, height from shadows and area averages, roof type); trees detected; biome rules | "might be that 20 % of buildings are off" [V] | proprietary | Rules by area when data is missing; plausibility matters (the 212-floor tower) |
| **OpenFACADES** (Liang et al., 2025, ISPRS J.) | Matches Mapillary panoramas to OSM/Overture footprints by isovist visibility (50 m radius, 10-120° view angle), detects the building, fine-tunes a 2B vision-language model (InternVL2.5) to predict type, surface material, floors, age; 7 cities incl. San Francisco, Houston, Washington DC | Material top-1 **0.69**, top-2 0.82; floors MAE 2.3; age MAE 29 years [V] | data and code open on GitHub (licence not stated in paper); labels partly made with GPT-4o | **The visibility matching method** (which photo sees which wall); the honest number: even with street photos, wall material is about 70 % top-1 |
| **BMAT** (Yin, Zhang et al., ESSD 2026) | Facade material for 22 million buildings in 73 cities from street-view images and a fine-tuned Qwen2.5-VL-7B; Overture footprints | F1 0.91 on held-out images; 0.72 agreement with Overture's material records [V] | Article CC BY 4.0; **images from Google and Baidu Street View** | Do not use the data (Google terms forbid derived datasets; see 8.7). Take the 9-class material scheme and the agreement method |
| **RoofNet** (2025) | 51,503 satellite tiles, 14 roof material classes, 112 countries; RemoteCLIP fine-tuned with material-aware prompts | +38-40 points top-1 over zero-shot [V] | **CC BY-NC 4.0** | Do not use the data. Take the class list and the "prompt with geography" idea |
| **GlobalBuildingAtlas** (TUM, 2025) | 2.75 billion building polygons with heights and LoD1 | 97.4 % with heights [V] | **CC BY-NC 4.0** | Do not use. We have 3DEP lidar heights |
| Overture Maps buildings | Building schema with height, floors, roof shape, roof and facade material and colour (mostly from OSM) | — | ODbL | A label source where filled; the same fields as our `BLDG` |
| Zhang, Aliaga et al., "Synthesis and Completion of Facades from Satellite Imagery" (ECCV 2020); "Automatic Deep Inference of Procedural Cities from Global-scale Spatial Data" [S] | Infer procedural facade and building parameters from satellite and map data | — | research | Infer grammar **parameters**, not pixels |
| FaçAID (2024), "inverting procedural buildings with transformers" (2025) [S] | Neuro-symbolic: a network outputs a facade grammar program | — | research | Our FACD record is such a program; an eye could output it later |
| UrbanWorld 2.0 (2025), MetaUrban (2024), Real2sim [S] | City-scale world generation for AI agents from maps, with rule-based placement of street furniture | — | research | Rule placement on sidewalks; confirms nobody publishes learned per-context rates for the US roadside |
| Horizon Zero Dawn GPU placement (GDC 2017) [S] | Density rules over world data maps | — | — | Density from data maps, but ours learned, not painted |
| OSM completeness: Barrington-Leigh and Millard-Ball (PLoS ONE 2017): world roads > 80 % complete; saturation curves of edit history (HeiGIT ohsome) [S] | Estimate how complete OSM is per area from its own history | — | open | **Choose the "rich" places to learn from** (section 5.2) |
| Census Survey of Construction (annual "Characteristics of New Housing") | New single-family houses: primary exterior wall material, foundation, stories, parking, outdoor features [S] | — | public domain | Wall and foundation priors by region and year built |

**What nobody does yet (as far as we found):** learn per-context rates and mixes of roadside things and materials for the whole US from completeness-checked open data, combine them with calibrated eyes and lidar as a posterior, sample them with stable dice so updates do not reshuffle the world, and test the mix (not only top-1) against held-out places and street photos.

---

## 4. The graph

### 4.1 Nodes

The graph keeps the place graph's token style (`kind:value`), so the existing rule engine and the existing context facts (`climate:*`, `land:*`, `road:*`, `era:*`, `use:*`, `region:*`, `district:*`) join it without new code.

| Node | Token | Examples | Fields |
|---|---|---|---|
| **Thing** | `obj:*` | `obj:mailbox`, `obj:fire_hydrant`, `obj:utility_pole`, `obj:street_light`, `obj:delineator_post`, `obj:fence`, `obj:guard_rail`, `obj:culvert_end`, `obj:bench`, `obj:bus_stop_sign`, `obj:trash_bin`, `obj:manhole`, `obj:storm_inlet`, `obj:rail_crossing_gate` | size range (m), parts, variants, material slots, mesh library key, real 3D or trick (section 6), anchor kinds, collision class, night behaviour (lit, retroreflective) |
| **Variant** | `var:*` | `var:mailbox_post_single`, `var:mailbox_cluster_box`, `var:fence_barbed_wire`, `var:fence_wood_privacy`, `var:fence_chain_link`, `var:fence_split_rail`, `var:pole_wood_distribution` | parent, size, library key, parameter ranges (lean, wear, colour) |
| **Material** | `mat:*` | `mat:brick_common`, `mat:vinyl_siding`, `mat:fiber_cement`, `mat:stucco`, `mat:wood_clapboard`, `mat:asphalt_shingle`, `mat:standing_seam_metal`, `mat:clay_tile`, `mat:asphalt_new`, `mat:asphalt_aged`, `mat:chip_seal`, `mat:concrete_slab`, `mat:soil_<munsell bin>`, `mat:rock_<family>` | albedo range (linear), roughness range, **library key = the AI lane's key (surface/material/style)**, parameters (colour, bond, exposure, mortar), wear process, era range, **compatibility** (max floors, uses, eras) |
| **Plant** | `plant:*` | `plant:foundation_shrub`, `plant:hedge`, `plant:species_group_<TreeMap group>` | height and crown range, season behaviour (leaf-off, colour) |
| **Anchor** | `anchor:*` | `anchor:road_edge`, `anchor:driveway`, `anchor:junction_approach`, `anchor:building_front`, `anchor:kerb`, `anchor:parcel_edge`, `anchor:ditch_driveway_crossing`, `anchor:bridge_end`, `anchor:rail_crossing` | how the scan engine finds it (map, rule, derived), stable id rule |
| **Context** | existing + new | `climate:Cfa`, `land:nlcd_22`, `road:residential`, `use:house`, `era:1950s`, `region:census_division_5`, `density:hu_km2_bin3`, `rural:urban_cluster`, `dist:0_5m`, `side:left` | bins |
| **Source** | `src:*` | `src:osm`, `src:overture`, `src:nbi`, `src:fra_crossings`, `src:nsi`, `src:census_soc`, `src:mapillary_features`, `src:city_<name>_hydrants`, `src:mutcd`, `src:usps` | licence, credit line, coverage, completeness per place, date |

### 4.2 Edges (most are table rows, not hand rules)

| Edge | Meaning | Example | Learned from |
|---|---|---|---|
| `rate(thing, anchor, context) -> λ` | expected count per 100 m of anchor | `obj:utility_pole`, `anchor:road_edge`, `rural:rural`, `road:tertiary` | OSM `power=pole` in rich counties, lidar columns, Mapillary features |
| `spacing(thing, context) -> (min, mode, max)` | hard-core distance and typical gap | hydrant on urban streets: the AI lane's 100-150 m is the starting value; learned per city | nearest-neighbour gaps along the anchor; fire-code ranges as a check |
| `attach(thing, anchor) -> offset distribution` | where, how far, which side, height | curbside mailbox at the driveway, road side, box bottom about 1.0-1.1 m high (USPS) | USPS published numbers; street-photo audit |
| `probability(thing, anchor, context) -> p` | for one-per-anchor things | `obj:mailbox` per rural driveway; cluster boxes for newer subdivisions | Mapillary features, audit |
| `co_occurs(a, b) -> lift` | b is more likely near a | culvert end under a driveway that crosses a ditch; street light on a utility pole | engineering practice + counts |
| `excludes(a, b, d)` | not within d m | no hydrant within 3 m of a driveway; nothing inside the travel lane or clear zone | standards |
| `share(variant | thing, context) -> p` | which variant | fence by land use: pasture -> barbed wire, backyard -> wood privacy | OSM `fence_type`, audit |
| `share(material | surface, context) -> p` | which material | house wall by region x era x use | Census SOC, NSI construction type, Overture/OSM `building:material` |
| `compatible(material, building facts)` | hard limits | `mat:vinyl_siding` only up to 4 floors and houses or small commercial | written by hand with a `why` |
| `made_of`, `wears_as`, `counted_by`, `looks_like` | slots, wear process, source completeness, library key | — | — |

Every row carries: value, sample size n, source ids, rules version, and a `why` line.

### 4.3 Files

- `data/knowledge/world/things.json`, `materials.json`, `anchors.json`, `sources.json`: hand-written, small, reviewed.
- `data/knowledge/world/priors.csv`: learned rows. Made by a script, never edited by hand. ODbL (see 8.7).
- Compiled at build time to `priors.bin` (sorted keys, binary search, a few MB), shipped in the installer, loaded by the scan engine.

---

## 5. The method: see, know, imagine, select and adapt, check

This is the AI lane's chain ("see, then imagine, then select from the library") with five changes that make it better.

### 5.1 See: calibrated evidence, not a top-1 label

- The eye's new heads (road surface, roof material, wall "imagine" head) output a **probability vector**, never only the top class.
- Each head is **calibrated** on held-out places (temperature scaling: one number per head, fitted so that "70 % sure" is right 70 % of the time). An uncalibrated 85 % is worse than an honest 60 %.
- Other evidence enters as likelihoods too: NSI construction type (masonry raises brick, stone, stucco; wood raises siding), OSM and Overture `building:material` and `roof:material`, lidar roof pitch (steep pitch lowers flat membrane), roof colour from the restored aerial, footprint shape (long thin ranch footprints are post-war; complex footprints are newer), Census median year built.

### 5.2 Know: the prior from the graph

- `share(material | surface, context)` and the rates from section 4, learned offline (section 5.6).
- **Shrink sparse contexts toward their parent** (empirical Bayes): λ(context) = (count + k · λ(parent)) / (length + k), where the parent drops the least important field first (era, then density, then region). A rare context gets a sensible number; a well-sampled one keeps its own.

### 5.3 Imagine: sample the posterior

- **Posterior = prior x evidence**, normalised. Where the eye is sure, the eye wins; where it is unsure, the region's mix wins. A measured fact (map tag, NSI value checked by plausibility) is a near-certain likelihood.
- **Sample, do not take the top-1.** Taking the most likely answer everywhere makes every house in a region vinyl ("mode collapse"), which looks fake.
- **Block coherence (lumpiness).** Real streets come in lumps: one builder, one decade, one fence style. So each block (the place graph's `spread` group, about 140 m) first draws a **block mix** from a Dirichlet around the posterior, and each building draws from the block mix. Concentration is learned from how lumpy real blocks are (street-photo audit, OSM in rich places). Same for fence and mailbox styles along a road.
- **Roadside things**: along each 1D anchor, a Poisson process with rate λ and a hard-core minimum spacing; one-per-anchor things by seeded Bernoulli; co-occurrence and exclusion after that.
- **Stable dice (determinism across versions).** Each candidate slot has a fixed random number from hash(anchor stable id, thing id, slot index), with **no rules version in the seed**. Candidates come from a fixed high-rate process; a candidate is kept if its number is below λ / λ_max. Materials use the inverse of the cumulative mix with the building's fixed number. So a small change in a rate or a mix changes only a few objects; an update does not reshuffle the world (see 8.12).
- **Lidar veto.** Where 3DEP points exist, an imagined object taller than about 3 m (pole, light, sign gantry) stays only if lidar shows a column of returns within 1.5 m; else it moves to the nearest column or is dropped. A guess may not contradict a measurement.

### 5.4 Select and adapt: a library entry plus its own parameters

- 49 fixed library entries will repeat, and the eye sees repeats at once. So each entry becomes a **parameterised material**: colour (from the regional palette or the roof/aerial evidence, inside the albedo range), pattern parameters (brick size and bond, mortar colour, siding exposure 10-20 cm, board direction), wear (from age and process fields: rust streaks under steel, dirt at the base, sun fade on the south side), and TINT.
- The selection writes `MATL` / `FACD` with the library key **and** the parameters, seeded per building. 49 entries x parameters = thousands of plausible, non-identical walls.
- Props the same: mailbox variant x colour x lean (0-4°) x wear x post type.

### 5.5 Check and learn

- Plausibility and compatibility gates (section 8.1).
- Tests on held-out places and street photos (section 7), **scored on the mix**: share error per block, lumpiness, and calibration (log-loss), not only top-1.
- Offline, the errors per region update the priors and the calibration for the next rules version.

### 5.6 Offline learning on the PC (the cloud cannot reach the OSM servers)

1. **Choose rich places per thing.** Estimate OSM completeness per county or city: compare OSM counts with an authoritative count (city hydrant or light layers, FRA crossings, NBI bridges, lidar pole columns, Mapillary features) and with the edit-history saturation curve. Rich = completeness at least 0.8.
2. **Count** each thing per anchor and context in rich places only (Geofabrik extracts on the PC). Measure spacing and block lumpiness.
3. **Weight against bias.** Rich places are mostly cities and college towns; weight samples so that rural contexts are not filled from urban ones, and hold out rural counties.
4. **Optional teacher model** (the AI lane's proposal): gradient-boosted trees (LightGBM MIT, or XGBoost Apache-2.0) with a Poisson or softmax objective learn rates and mixes from context; **export their predictions into the same tables** for every context bin. The game runs only lookups; the model is a teacher, not a run-time part.
5. **Only measured data trains.** Items flagged "imagined" never enter a training set (no feedback loop).
6. Write `priors.csv` with n, sources and credit lines.

### 5.7 Run time per tile (CPU worker, deterministic)

Anchors -> context -> prior lookup -> measured items first -> sample (stable dice) -> co-occurrence, exclusion -> lidar veto -> variant, materials, parameters -> plausibility -> write `INST` (flag "imagined", probability u8, prior row id in META), `MATL`, `FACD`. Estimate: well under 50 ms on one thread per tile. Imagined roadside items are made in **pass 1** (they are cheap), so they exist before the car can see them; only their fine parts wait for the J3 ladder.

### 5.8 What the AI lane gets from the graph

- Entity ids equal to the library keys (or mapped one-to-one in `looks_like`).
- For each entity: contexts and priors, size range, spacing, offsets, co-occurrence and exclusion, variant shares, compatibility limits, parameter ranges.
- `counted_by` with completeness per place, so labels come from rich places only.
- The calibration rule for each head and the mix-based test metrics.

---

## 6. Real 3D and surface trick

Rule (spec v2): outlines are real 3D, the inside of a surface can be shading, every detail has a cause.

| Thing | From the driver's seat | Real 3D or trick | GPU plan (each group ≤ 0.3 ms p95, off switch) |
|---|---|---|---|
| Mailbox, hydrant, sign, delineator, bench, bin, culvert end | Outline at 5-60 m; passes the side window | **Real 3D**: instanced, 2 LoDs, culled at 120 m, shadows within 30 m | One instance group per kind per tile (estimate 0.05-0.15 ms for all) |
| Utility poles, street lights, wires | Outline against the sky; edge rate gives speed | **Real 3D**; wires as thin ribbons, TSR thin-geometry detection | 0.05-0.1 ms (estimate) |
| Fences, guard rails, hedges | Long outlines | **Real 3D** under 80 m, strips or impostors far | 0.1-0.2 ms (estimate) |
| Kerb, gutter, driveway lip, steps, porch, window reveals, sills | Small sharp outlines near the car | **Real 3D** (road and facade meshes) | Inside road and building budgets |
| Bushes, foundation plantings | Soft outline | **Real 3D** cards in clumps | 0.1-0.2 ms (estimate) |
| Wall material, mortar, siding lines, shingles | Inside a surface | **Trick**: parameterised material + normal; parallax only on walls facing the driver | 0 extra |
| Stains, cracks, paint wear, moss, leaf litter, fine gravel | Inside a surface | **Trick** (corridor texture, decals), cause from process fields | Inside the corridor budget |
| Manholes, storm inlets, road paint | Flat | **Trick** (decal); the inlet grate is 3D at standstill (J3 L3) | ~0.02 ms |

Each group is measured alone at Ultra (frame-time p95 on and off) before it ships (criterion 7).

---

## 7. How we prove it against real photos, with no person looking

| Test | What | Automatic pass line |
|---|---|---|
| T1 Held-out counts | Predict count per km for each thing in held-out cities and rural counties; compare with authoritative counts (city layers, FRA, lidar columns, Mapillary features) | Ratio 0.7-1.4 per thing; spacing KS test not rejected at 5 % |
| T2 Mix and lumpiness | In held-out places with Overture/OSM material tags or audited photos: per block, material share error and a lumpiness statistic (share of same-material neighbours) | Mean share error ≤ 15 points; lumpiness within the real 10-90 % band |
| T3 Calibration | Log-loss and reliability curve of each eye head and of the posterior on held-out places | Expected calibration error ≤ 0.05; posterior log-loss better than prior-only |
| T4 Street-photo audit | 20 random 200 m segments at random US places with licensed photos; counted once by a person, then frozen as a test set | Mean absolute count error ≤ 30 % per thing group |
| T5 Frame beside photo | Game frame at the photo's pose next to the photo (criterion 6); image statistics (spectrum slope, colour, edge density) computed automatically | Each graded part up one grade (people, at release); statistics within bands (every run) |
| T6 Never-wrong checks | Counters that must be zero: imagined items in water, on lanes, inside buildings, on slopes > 35°; incompatible materials (siding above 4 floors, brick curtain-wall towers); imagined names or brands | 0 |
| T7 Repetition | Identical (variant, parameters) neighbours within 50 m; identical facade parameters on adjacent buildings | Below a set rate (for example < 2 % of pairs), except where the block mix says "same builder" |
| T8 Churn | Between two rules versions at the 18 test places: share of objects and materials that changed | Below 5 % unless the change note says why |
| T9 Seams and pop | Objects cut at tile borders; items that appear inside 60 m in view on the pre-flight video (pop counter) | 0 cut objects; pops not above main |
| T10 Cost | Each group on and off at Ultra on the Dragon and one random town | ≤ 0.3 ms p95; 0 new hitches |

T1-T3 and T6-T10 run without a person, every build, and fail the run when a line is crossed.

---

## 8. What can go wrong (gotchas) and the fix for each

| # | Gotcha | Fix | Status |
|---|---|---|---|
| 8.1 | **Wrong guess looks worse than plain**: brick on a glass tower, vinyl on a church, a mailbox in a river, a hydrant in a field | Hard compatibility table (`compatible`: floors, use, era) checked after sampling; placement masks exclude water, lanes, clear zone, buildings, slopes > 35°, parcels without a building; **fail to plain**: below a confidence threshold, pick a neutral material of the right class (painted render, plain concrete) instead of a specific wrong one; rural things only where the anchor exists (a mailbox needs a driveway and a house) | Solved in design; T6 proves it |
| 8.2 | **Repeats the eye sees as a pattern** | Parameterised materials and props (5.4); no-repeat sampling (adjacent buildings draw different parameters unless the block mix says one builder); hex tiling inside surfaces; T7 | Solved in design |
| 8.3 | **Seams at tile borders** | Anchors and buildings have global stable ids; each anchor is owned by one tile (by hash of its id), and objects on it are generated once; 8-cell padding already in spec; materials per building, not per tile | Solved in design |
| 8.4 | **Seams at class borders** (grass to asphalt to gravel) | Transition rules in the graph (asphalt edge -> gravel strip -> grass), noise-dithered blend widths from the cross-section; no hard straight class lines | Solved in design; check in frames |
| 8.5 | **Pop when detail arrives late** | Roadside things in pass 1 (cheap, ready ≥ 12 s ahead per J2); fine parts by the J3 ladder with 1 s fades; swaps behind masks (study idea 4); T9 | Mostly solved; high-speed pop at 240 km/h still to measure |
| 8.6 | **Thin-data places** (Alaska, Hawaii, deserts, new suburbs, tribal land) | Every rate has a parent fallback down to national; deserts correctly get few objects (rates are learned, not forced); Alaska: no NAIP in parts (Sentinel-2 fallback already planned), IfSAR terrain, sparse OSM: rules from the nearest climate-similar contexts (Köppen Dfc, ET); new suburbs: Microsoft/Overture footprints show houses that OSM lacks; ACS year-built lags, so footprint shape and "no trees yet" in the aerial raise the "new" era; tribal land: OSM often sparse, Census data present; never assume a city-like density | Priors written (15 Nov): wall overrides for AK and HI (materials.json), Alaska and Hawaii vegetation from LANDFIRE (TreeMap is lower 48 only), house styles and parts (buildings-variety.json); 6 held-out counties (Anchorage, Fairbanks North Star, Juneau, Honolulu, Hawaii, Maui) in counties.json for T1. Note: the Census Pacific division is mostly California, so AK and HI must never use the division row |
| 8.7 | **Licence traps** | (a) OSM-derived tables and any weights trained on OSM labels are a derivative database under ODbL: credit "© OpenStreetMap contributors" and publish them under ODbL (the repo is public anyway). (b) NSI: CLEARED (world lane, 14 Nov 23:53Z): the public base layer is a US federal work, releasable to the public. Use only the public fields (bldgtype, found_type, found_ht, num_story, med_yr_blt), never the restricted parcel fields (yrbuilt, p_extwall, p_construc, basement, naics, empnum, apn); no saved or bundled raw responses (learning keeps only aggregate counts); credit "U.S. Army Corps of Engineers National Structure Inventory (USACE NSI)". So p_extwall is NOT a label source for the wall head. (c) SDXL outputs: allowed under rule F; obey the Open RAIL++-M use limits and record them. (d) Street photos (CC BY-SA): never ship pixels; use for counts and tests, logged with credit. (e) **Do not use**: GlobalBuildingAtlas and RoofNet (CC BY-NC), BMAT and any data made from Google or Baidu Street View (their terms forbid derived datasets), labels made with a commercial chat model unless its terms allow it. (f) City open data: record each city's licence; skip a city whose terms are unclear | Rules written; (b) cleared with limits |
| 8.8 | **Download size and the first run on a clean PC with slow internet** | The graph tables ship in the installer (a few MB). Pass 1 needs only OSM + DEM per tile and works with rules alone. Lidar is the heavy part (tens of MB per tile): fetch only the road corridor (±60 m) and coarse levels first; skip lidar on slow links (rules fill). Ship the start pool and hero places pre-baked. Disk cache LRU with a size cap (for example 20 GB default, a setting). The first drive shows a complete world from rules; detail upgrades in place | Design, now section 10 (packs, range reads, mirrors, cache); learning scripts resume from cache and take --tile-deg and --pause for slow links; still needs a throttled-link test (10 Mbit/s) |
| 8.9 | **CPU load on a 4-core PC** | Worker count = cores - 2 (min 1); eyes by tier (off on 4 cores while driving, on when stopped); the sampler and rules are cheap and always run; the J2 speed rule drops near jobs at speed; back-off on frame-time p95 is already in the spec. Result: a slower PC gets more rule fallback, never holes | Design; learning scripts are single-thread and run once on the PC (no game effect); the run-time sampler is table lookups; still needs the 4-core profile test (affinity mask) |
| 8.10 | **Old or seasonal aerial photos** (snow, leaf-off, long shadows, construction) | NAIP is flown leaf-on in the growing season (by design); the de-light step removes shadows; record the photo date per tile; if the eye finds snow or construction (bare graded ground in a footprint-rich area), lower the eye's weight and let the prior lead; season in the game comes from the date, not the photo | Design written (buildings-variety.json `construction_sites`): detect from Annual NLCD change, footprints without trees, landuse=construction, new roads without footprints; set `site:construction`, lower the eye weight, and show a real building site (silt fence, orange fence, dumpster, slabs and frames) instead of a fault; needs one test place |
| 8.11 | **Map errors and vandalism** | Plausibility gate (spec G9): floors vs NSI stories vs lidar height; road width vs lanes; outliers fall to the next source; very new OSM edits with one editor get less weight; Overture's cleaned data as a second opinion | Solved in design |
| 8.12 | **Determinism across versions** (the same place must not change at each update) | Stable dice (5.3): seeds without the rules version; thinning and inverse-CDF selection so small prior changes move few items; T8 churn test with a change note | Solved in design |
| 8.13 | **Night, rain, seasons** | Each thing has night behaviour (street lights lit with the right lamp type by era and region; delineators and signs retroreflective in headlights); rain wets materials through DYN (darker, lower roughness); leaf-off for deciduous species in winter (crowns thin, the view opens), snow poles and plough stakes in snowy states in winter, bins on the street on collection day (later) | Design; lamps and retroreflection need the picture thread |
| 8.14 | **A test fails silently when the guess is wrong and no person looks** | T1-T3 and T6-T10 run every build with hard lines; any guess below confidence is counted; a "dull world" check (objects per km far below the prior) and a "cluttered world" check (far above) both fail the run | Solved in design |
| 8.15 | **Bias from rich places** (cities teach the countryside) | Completeness weighting, contexts that include rural/urban and density, rural held-out counties in T1 | Solved in design |
| 8.16 | **Imagined things that change driving** (a stop sign that is not real; a mailbox in the car's path) | Regulatory signs only from the map or from a written standard at a mapped junction, and the traffic code reads the same record; props never on the drivable area; small props break away or are light in physics (a car can hit a mailbox) | Design; physics class per thing needs the car team |
| 8.17 | **Feedback loop** (imagined data trains the next model) | Only "measured" items train (5.6 step 5) | Solved |
| 8.18 | **Privacy** | Street photos never shipped; NSI per-house values are used only as a style input, never shown | Solved |

---

## 9. First version this week, and later

### Ranked first step

1. **V0 roadside things (this week; the most realism per hour).** Rural and suburban roads look empty today, and these things give the sense of speed.
   - Graph: 12 things (mailbox, utility pole, street light, fire hydrant, delineator post, fence in 4 variants, culvert end, speed-limit sign, curve warning sign, stop sign at mapped junctions, bench, trash bin), 6 anchors, 8 context fields, `sources.json` with licences.
   - Priors: standards typed by hand with sources (MUTCD, USPS, the cross-section); rates, spacing and lumpiness learned from OSM in about 20 rich US counties and cities on the PC, completeness checked against 5 city open-data layers and lidar columns, with shrinkage.
   - Sampler: C++ in the scan engine (anchors, stable dice, co-occurrence, lidar veto, masks, flags) writing `INST` in pass 1.
   - Proof: T1 on 2 held-out cities and 2 rural counties, T6-T10 automatic, T5 frames at 3 hero roads and 5 random places.
   - Who: innovation lane writes the graph files and the learning script (Python, runs on the PC); world lane writes the sampler; props from the CC0 or own-made set.
2. **V0 materials (this week, small).** `share(material | wall, region x era x use)` from the Census Survey of Construction (new houses since the 1970s) plus NSI construction type, the compatibility table, and parameter ranges for the 49 library entries; the AI lane's wall head becomes a calibrated likelihood. Test T2 and T3 on held-out places with Overture/OSM material tags.
3. **v2.0.4: the teacher model** (gradient-boosted trees over many more counties, exported to tables); more things (sidewalk presence, kerb type, driveway surface, foundation plantings, porch type, roof parts); variant shares and lumpiness from the street-photo audit.
4. **v2.0.5: street-photo facts.** Where Mapillary or Panoramax photos see a wall (OpenFACADES-style visibility matching, our own code), a cleared model reads material, floors and fence type as "seen" evidence that beats the prior. Expected gain from the literature: wall material from about 50-65 % (prior + eye) to about 70 % top-1 where photos exist.
5. **Later:** learned co-occurrence by state (lights on utility poles), seasonal and weekly things (bins on collection day, holiday lights by date), the standstill L3 props.

### What would make it better still (beyond the AI lane's chain)

1. Calibrated probabilities instead of top-1 labels (5.1).
2. Posterior sampling with block lumpiness instead of "most likely everywhere" (5.3).
3. Stable dice so updates do not reshuffle places (5.3, 8.12).
4. Parameterised library entries so 49 entries do not repeat (5.4).
5. Tests on the mix, calibration and churn, not only accuracy (7).
6. Lidar veto: the guess never contradicts a measurement (5.3).

---

## 10. Data supply at scale (the owner, 23:56Z: "are we going to be dosing a bunch of public servers especially when it scales?")

### 10.1 Short answer

- **The learning scripts do not overload servers.** They run once, on one PC, one request at a time, with a cache. The results (a few MB of tables) ship in the installer.
- **The game must not fetch raw data per player from servers that are not built for it.** That design grows with the number of players, and some hosts forbid it. So: **the game never contacts the OSM tile servers, Overpass, Nominatim or the NSI API**, and never a requester-pays bucket.
- **Recommended design: "packs first".** We pre-make compact "recipe" packs per state (our scan-map data: causes and seeds, not pixels), host them as static files on free hosts that permit it (GitHub Releases plus mirrors), and the game reads only the bytes for the road ahead with HTTP range requests, then keeps them in a long-lived cache. Bulk public buckets that are built for many users (USGS 3DEP lidar on AWS Open Data, Overture GeoParquet) are a fill-in, with a per-player cap. Rules and priors fill everything when there is no network.

### 10.2 What each source permits (read 15 Nov unless marked)

| Source | Host and policy | Per-player fetch OK? | May we redistribute it? |
|---|---|---|---|
| OSM raster tiles (tile.openstreetmap.org) | No bulk downloading ("any pre-emptive fetching of tiles other than those a user is actively viewing"), **no offline use**, distinct User-Agent, honour cache headers, access can be withdrawn without notice [V] | **No** | n/a (we do not use tiles) |
| Overpass API public instances | About 10,000 requests and 1 GB per day per user; "setting up an app for more than just OSM mappers and relying on the public instances as backend" is named as a problem; heavy users are throttled first [V] | **No** (learning on the PC only) | Data is ODbL (see below) |
| Nominatim (geocoder) | Strict policy: at most 1 request per second, no bulk, no heavy app use [K] | **No** | n/a |
| Geofabrik OSM extracts | Download files; fine for one pack-builder machine [K] | No (builder only) | ODbL |
| OSM data | ODbL 1.0: credit "(c) OpenStreetMap contributors"; a derivative **database** that we make public must be offered under ODbL; a produced work (an image) needs the credit only [K] | n/a | **Yes**, under ODbL with credit |
| USGS 3DEP lidar, `usgs-lidar-public` on AWS | Free, no account, "US Government Public Domain", EPT (streamable octree, range reads); the raw `usgs-lidar` bucket is requester pays [V] | **Yes, with a cap** (built for public bulk access) | Yes (public domain) |
| USGS DEM products (TNM staged products) | Public domain; anonymous bucket access to confirm on the PC [K] | Confirm first | Yes |
| NAIP on AWS (`naip-analytic`, `naip-source`, `naip-visualization`) | **All requester pays** (the reader pays AWS per GB) [V] | **No** (no money rule) | Yes ("public domain with attribution") |
| NAIP on Microsoft Planetary Computer | Free anonymous access with short SAS tokens; "preview" service with supplemental terms [S]; terms not read today | Not as a dependency | Yes (the data is public domain) |
| Overture Maps (S3 `overturemaps-us-west-2`, Azure) | Anonymous GeoParquet, partitioned by theme and type; PMTiles copies are "for data inspection only and shouldn't be used in production" [V]; licence per theme (buildings and transportation ODbL; others vary) [K] | **Yes, with a cap**, GeoParquet only | Per theme; ODbL themes yes with credit |
| USACE NSI API | Federal; cleared for public fields, **run time only, no saved or bundled raw responses** (world lane, 23:53Z); no published capacity for game traffic | **No** (see 10.5) | **No** raw records; aggregates only |
| Census API (ACS) | Public domain; key advised; required notice "This product uses the Census Bureau Data API but is not endorsed or certified by the Census Bureau." [K] | No (builder only) | Yes |
| Live data (weather, aircraft) | Owned by the live-data thread; each free API has its own per-user limits | Small calls per player with a 1 h cache are normal use; the live thread checks each term | n/a |

### 10.3 The five options

| Option | Load on others | Cost to us | Licence | Verdict |
|---|---|---|---|---|
| (a) Polite per-player fetch, long cache, only from hosts built for bulk | Grows with players; acceptable only for AWS Open Data / Overture-type hosts | Zero | Each source's terms | **Fill-in only** (lidar, Overture), with a per-player cap |
| (b) Pre-made data packs per state as static files | Our hosts only; zero on community servers | Builder time on a PC; free hosting | Only redistributable data: OSM (ODbL, credit, share-alike), public domain (USGS, USDA, Census, NAIP-derived), Overture ODbL themes; **no raw NSI** | **Yes, the base** |
| (c) Packs of derived "recipes" (scan-map causes, seeds, vectors, coarse rasters) instead of raw data | Same as (b) | Much smaller than raw (pixels are made at run time from causes and seeds) | Derived from OSM = still a derivative database (ODbL applies; we publish it anyway); derived from public domain = free; derived from AI eyes = allowed by rule F with credit; **NSI only as block aggregates** (10.5) | **Yes: the packs are recipes** |
| (d) Cloud-optimised formats with range reads (COG, EPT/COPC, GeoParquet, a PMTiles-like tiled container) | Each player reads only the bytes for the corridor ahead | Zero | Same as the data | **Yes, for our own packs and for the bulk buckets** |
| (e) Peer sharing of caches | Lowest | Engineering, NAT, firewall prompts, players' IP addresses visible to peers | Same as packs | **Later, and only as BitTorrent for whole-state packs** (Internet Archive makes torrents with web seeds automatically [V]); no per-tile peer network |

### 10.4 Free hosts for packs

| Host | Limits (read 15 Nov) | Use |
|---|---|---|
| GitHub Releases | Each file under 2 GiB; up to 1000 files per release; "no limit on the total size of a release, nor bandwidth usage" [V]. GitHub's general terms let it act against excessive use [K, confirm the current text] | **Primary** (same place as the installer) |
| Hugging Face datasets | Public storage "best-effort" (no fixed number); file hard limit 500 GB; served through CloudFront; under 100k files per repo, 10k per folder [V] | **Mirror 1**, range reads work on its file URLs |
| Internet Archive | Free; items get torrents with web seeds automatically [V]; no stated item size or bandwidth limit on that page; slow | **Mirror 2 and torrent source** for whole-state downloads |
| Source Cooperative (Radiant Earth) | Non-profit, S3-compatible, over 5 PB hosted, about 700 TB per month transferred; publishing needs an application (beta) [V] | **Apply**; best fit for range reads at scale |
| Cloudflare R2 and similar | Free tier then paid | **No** (no money rule) |

Each pack has a manifest (file list, SHA-256, sources, licences, credits, build date) inside the installer or signed, so a mirror cannot change the data.

### 10.5 NSI at scale

The cleared use is "run time only, no saved or bundled raw responses". If every player called the NSI API for every tile, a federal service with no published capacity for game traffic would carry our load. **Proposal for the world lane to confirm:** players do not call NSI. The pack builder calls it once per county and keeps only **aggregates per block group** (for example foundation-height distribution, masonry share, storey shares, median year built), which is the same aggregate use that `learn_walls.py` makes. The per-building values never leave the builder's memory. If the world lane reads the limit as "no aggregates in packs either", the game uses the division-level priors from `learn_walls.py` and the eye, and NSI stays a learning-only source.

### 10.6 Recommended architecture

1. **Installer**: graph tables and priors (a few MB), rules, start pool and hero places pre-made.
2. **Pack builder** (any PC, ours first): per state, from Geofabrik OSM, Overture, USGS 3DEP DEM, NAIP (read once by the builder from a free source, kept only as derived fields), TreeMap, LANDFIRE, NLCD, SSURGO, SGMC, Census, NSI aggregates. Output: one range-readable container per state, tiled (1-2 km), with an index; corridor tiles have the CORR cross-section, measured things, buildings with attributes, 2-4 m ALBD colour, 10 m terrain, stand pointers (TreeMap plot id per 30 m), causes and seeds. Fine detail (0.25 m and below) is made at run time from these causes (design sections 5 and 11).
3. **Hosting**: GitHub Releases (primary) + Hugging Face and Internet Archive mirrors (+ Source Cooperative after it accepts us). Packs rebuilt each quarter; small deltas between.
4. **Game**: reads the tiles for the corridor ahead by range requests from the nearest working mirror; long-lived disk cache (LRU, size cap, default 20 GB); one network module with a **host allowlist**, a stable User-Agent, at most 4 parallel requests per host, back-off on 429/503, and a per-host daily cap. Lidar EPT from `usgs-lidar-public` only on fast links and only near the car, capped (for example 2 GB per day). No network: rules and priors.
5. **Optional later**: full-state download by torrent (Internet Archive web seeds) for offline play.

**Load estimate (to measure on the first pack):** if a corridor tile costs 0.5-2 MB per km of road, a one-hour drive at 100 km/h on new roads is 50-200 MB the first time and zero on repeat; 1,000 new-road player-hours a day is 50-200 GB a day spread over mirrors. That is ordinary for CDN-backed hosts and zero for the OSM community servers.

### 10.7 Migration order

1. **Now (v2.0.3 safety):** the world lane's table of what the game contacts today. Remove every per-player call to OSM tiles, Overpass, Nominatim, NSI and requester-pays buckets; put all traffic through one module with the host allowlist; fast test: a test drive makes no request to a forbidden host.
2. **One state pilot** (Tennessee, for the Dragon): pack format and builder on the PC; measure MB per km, build time, and frame-time effect of reading packs.
3. **Host** the pilot on GitHub Releases + Hugging Face; the game reads packs with range requests and caches.
4. **All 50 states**, quarterly rebuild, deltas, credits page generated from the manifests.
5. **Lidar**: either capped range reads, or lidar-derived fields (crowns, poles, roof planes, bridge decks) baked into packs, which also removes the heaviest download.
6. **Optional**: torrents for whole states; offline mode.

### 10.8 Gotchas

- GitHub can limit very heavy downloads: keep two mirrors and the torrent path, and let the game switch mirror on failure.
- Stale packs: OSM changes daily; quarterly rebuild + deltas; the game prefers the pack, not a live fetch.
- Data poisoning or a changed mirror: SHA-256 per file from the manifest that ships in the installer.
- Credits: the pack manifest carries every credit line; CREDITS.md and the in-game credits page are made from it (ODbL, NAIP attribution, USACE NSI, Census API notice).
- Privacy: any download shows the player's IP to the host (normal web use); peer sharing would also show it to other players, which is why per-tile peer sharing is out.
- Builder load on sources: one builder, one request at a time, cached; a second builder reuses the first builder's caches, not the sources.

---

## 11. "Id to detail": the owner's idea, made exact (the owner, 23:59Z)

The owner: "imagines grass so it adds grass, imagines tree bark, details, the gravel on the road, buildings, imperfections, paint, the details, leaves, more texture, then a layer over the whole image that adds more imperfections and real detail. How does it come up with when to add what? The objects like grass, road, etc have a code which is context for that pixel and it generates based off that."

### 11.1 Short answer

Yes, this works, and it is cheap when it is done in the right place. The "code" is two things together:

1. **The surface code**: the material id of the surface (asphalt, road paint, gravel shoulder, kerb, mowed grass, tall grass, cut-slope soil, rock, leaf litter, bark, leaf, brick, stucco, siding, glass, roof, metal, water).
2. **The cause numbers** for that surface, from the scan map and the graph: age, wear class, traffic, wetness, sun side, slope, distance from the road edge, climate, season, era, region. These come from data we already have (sections 4-5, scan-map spec PROC, CORR, MATL, GENR, TINT).

One **rule set per surface code** says what detail to add, at which distance, and from which causes. Little internet context is needed: the cause numbers come with the tile, and the rules ship in the game.

But the photos show that **texture is only part of the gap.** Much of it is **missing things and layers** (11.2). So the rule set adds three kinds of detail: small real 3D parts, surface detail, and wear; and the graph (sections 4-9) adds the missing objects.

### 11.2 Real photo against game frame (what the eye sees)

Frames: `photo-compare/side-by-side_dragon.jpg`, `side-by-side_ocean.jpg` (game main 4aaf1fc), `release-2.0.3/01-dragon-miata-cockpit.jpg`, `03-big-sur-roadside.jpg` (newest frames found; the review lane's newer frames replace these when they exist).

| Surface or thing | Real photo | Game frame | Kind of gap |
|---|---|---|---|
| Forest at the road | Many thin vertical trunks, mixed sizes, light spring green over dark, layered depth into haze | Few big round "blob" crowns, short trunks, one green, flat | **Structure** (vegetation.json ranks 1-3), then leaf texture |
| Undergrowth and forest edge | Dense shrubs and saplings fill the gap under trees; leaves on the ground | Bare ground or smooth grass under trees | **Missing layer** |
| Distant hills | Strong aerial haze, each ridge lighter and bluer; texture of individual crowns | Smooth even green, no haze layering; on Big Sur a visible repeating pattern | **Atmosphere** + far canopy texture |
| Road surface | Darker wheel paths, lighter centre, tar-sealed cracks, patches, polished aggregate, oil drip line in lane centre | Uniform grey with even noise | **Surface detail and wear** |
| Road paint | Worn edge lines, wheel-worn centre lines, raised pavement markers on some roads | Clean, full-strength lines | **Wear** |
| Road edge | Asphalt edge breaks into gravel and dirt, leaves collect, grass and weeds grow into the edge | Sharp clean line from grey to grass | **Small 3D parts + transition** |
| Cut slopes | Red-brown soil and rock faces, roots, small trees on the slope | Smooth grass mounds | **Material from GEOL/soil + slope** |
| Roadside things | Signs (clusters at state line), sign posts, guardrail, concrete barrier, power-line corridor, delineators | None | **Missing objects** (things.json) |
| Kerbs and street (Ocean Drive) | Yellow-painted kerbs, green bike lane, raised markers, hedges, bins, people, umbrellas | No kerb, one grey | **Missing objects + road cross-section** |
| Art Deco walls | Bright white render with crisp shadows from eyebrow sun-shades, deep-set windows, fine trim lines | Flat pastel walls, flat windows, colours too saturated | **Geometry (outlines) + colour from photo** |
| Whole image | Camera look: slight grain, lens fall-off, highlights roll off, haze | Clean computer render | **Camera layer (11.6)** |

### 11.3 The rule set per surface code

Distance bands: **near** 0-15 m (real 3D small parts and parallax only on faces toward the driver), **mid** 15-80 m (surface detail in the shader), **far** beyond 80 m (colour and roughness only; aerial perspective). At speed the J2 speed rule drops near work the driver cannot see.

| Code | Causes it reads | Near (0-15 m) | Mid (15-80 m) | Wear and change | Where it runs |
|---|---|---|---|---|---|
| Asphalt | age, AADT class, climate (freeze-thaw), lane position | aggregate grains as detail normal; crack edges as real geometry only at the edge | crack network (block cracks in hot climates, transverse cracks in cold), sealed cracks (tar snakes), patches | wheel-path darkening and polish; centre oil line; rain: darker, specular, puddles in ruts (DYN) | Shader (procedural from seeds) + CPU bake of crack and patch layout per tile (CORR pages) |
| Road paint | age, traffic, snow plough region | raised markers as meshes where the road class uses them | line texture with gaps and chips | wheel wear where cars cross lines; fade with age | Shader + CPU layout |
| Shoulder and edge | CORR cross-section, surface type, climate | gravel stones as instanced meshes in the first 0.5 m; broken asphalt edge as geometry; leaves | gravel and dirt blend, grass tufts into the edge | leaf drift by season and forest group; sand in winter in snow states | CPU bake (placements) + shader |
| Kerb and gutter | era, city, CORR | kerb face real 3D; chips at driveways | gutter dirt line, painted kerb colour (city rule) | stains below drains | CPU + shader |
| Mowed grass | mowed strip width, season, climate | blades as cards or meshes (grass system already exists) | grass texture with mowing stripes and bare patches | brown in summer drought in dry climates (Koppen B, Csa), green in humid; frost in winter | GPU grass system + shader; placement from CPU |
| Tall grass and weeds | unmowed band, land cover | taller clumps, seed heads, weeds at fence posts | clump texture | season colour | CPU placement + shader |
| Cut slope, soil, rock | SSURGO colour, SGMC rock type, lidar slope | rocks and roots as meshes on near faces | soil and rock strata, erosion rills on steep soil | wet streaks after rain; moss on north faces in wet climates | CPU (GEOL) + shader |
| Forest floor | forest group, season | litter, twigs, fallen logs (low density) | litter texture by group (needles, broad leaves, moss) | autumn leaf fall depth | CPU placement + shader |
| Bark | species group, trunk diameter | bark ridges by species as detail normal | bark colour (birch white, sycamore patchy, pine plated) | moss on the wet side, lichen | Shader per species group |
| Leaves and canopy | species group, season, wind | leaf cards with real silhouettes, gaps you can see sky through | crown texture, light and dark clusters | season colour (vegetation.json) | Trees team meshes + shader |
| Brick, stucco, siding | material + parameters (materials.json), era, sun side | parallax on facing walls only; window reveals and sills real 3D | bond, mortar, siding exposure, stucco texture | dirt at the base, streaks under sills, fade on the south side, algae on the north in humid climates | Shader (parameterised) + CPU FACD |
| Windows | era, style, use | frames, sills, reveal depth real 3D | reflections, blinds and curtains (interior mapping later) | dirt in corners | Mesh + shader |
| Roof | material, pitch, age | none from the road on most roofs | shingle rows, ridge caps, moss in wet climates | stains, missing shingles on old roofs | Shader |
| Metal (poles, rails, signs) | age, climate | bolts, rivets, sign backs | galvanised spangle, rust in salt states | rust streaks, sticker clutter on sign posts in busy places | Mesh + shader |
| Water | NHD, wind | none | ripples, reflections | mud colour after rain | Shader |
| Air | humidity, season, time | n/a | aerial perspective and height fog (Dragon hills) | morning mist in valleys | Engine sky and fog settings |

Every row has a cause in data. Nothing is drawn because it "looks nice" only.

### 11.4 Where each part runs (the three places)

| Place | Good for | Not good for | Cost | Stability |
|---|---|---|---|---|
| **1. Material shader per surface code** (procedural micro detail from seeds and cause numbers) | Grain, cracks pattern, wear masks, colour variation, dirt by height, moss by normal direction | Placing objects; anything that must be exact | Low; a few texture samples per pixel; the 0.3 ms per detail limit holds if near-only work is masked | Stable (world-anchored, seeded) |
| **2. CPU bake per tile ahead of the car** (the scan engine) | Where things go: gravel stones, rocks, litter, crack layout, patches, unique corridor texture pages, object placement | Per-frame effects | Zero GPU per frame; CPU time in the worker | Stable, deterministic per tile |
| **3. Screen pass over the whole image** | Camera look: grain, lens fall-off, small colour response, highlight roll-off, sharpening, the per-place photo-look LUT | Surface detail: in screen space it "swims" over surfaces as the car moves, and a learned pass can flicker frame to frame | Cheap for the camera layer (about 0.1-0.3 ms); expensive for a learned network (11.5) | Camera effects: stable by nature. Learned detail: risky |

Rule: **detail that belongs to a surface stays on the surface** (places 1 and 2). **Only camera imperfections are screen-wide** (place 3). This answers "a layer over the whole image": yes for the camera layer, no for surface detail.

### 11.5 Can a very small id-conditioned network run in the post pass on an RTX 2070-class GPU?

Measured numbers that exist (no guesses):

| Network | GPU | Input | Time per frame | Source |
|---|---|---|---|---|
| NVIDIA DLSS super resolution, presets J/K (a production network on tensor cores, FP16) | RTX 2080 Ti | 960x540 in, 1920x1080 out | **1.16 ms** | NVIDIA DLSS Programming Guide, execution time table [V] |
| Same | RTX 3060 | same | **1.45 ms** | same [V] |
| Same, preset L | RTX 2080 Ti | same | 3.51 ms | same [V] |
| REGEN (photorealism enhancement, Pix2PixHD-based) | RTX 4090 | 960x512 | **33.53 ms** | REGEN paper, Table I [V] |
| EPE (Intel "Enhancing Photorealism Enhancement", G-buffer conditioned) | RTX 4090 | 960x512 | **1110 ms** | REGEN paper, Table I [V] |

Reading:
- Even one of the most optimised image networks in games costs about **1.2-1.5 ms** at 1080p on GPUs faster than the owner's laptop GPU (RTX 2070 Super Max-Q). Our limit is **0.3 ms per detail**, and Ultra is already at 21.5 ms against 16.7 ms. A full-screen learned detail pass is 4-8x over the limit, before we pay anything else.
- G-buffer-conditioned photorealism networks (the closest to the owner's idea) are 30-1000 ms on an RTX 4090. Not possible in real time on our target.
- **What can fit, to measure on the PC:** a very small network that runs only on a small part of the screen (for example the road surface within 15 m, about 10 % of pixels) or at quarter resolution, in FP16. A bench gives the answer in one hour: Unreal NNE (or ONNX Runtime with DirectML) on the laptop GPU, a 3-level U-Net with 16/32/64 channels, 8 input channels (surface code, normal, depth, causes), at 960x540 and at 480x270, p50 and p95 ms with `gpu-wait.ps1`. Pass line: at most 0.3 ms p95. Owner: picture lane with the AI lane. Until that bench passes, the network is not in the plan.
- **Where networks do pay off now:** offline and on the CPU. The AI lane's networks make textures and decisions per tile ahead of the car (zero GPU per frame), and the photo-look LUT per place (about 0.05 ms) gives most of the "camera" realism. DLSS-type upscaling, if used, is NVIDIA's own network, not ours.

### 11.6 The last layer (camera imperfections), cheap and stable

All are screen-space and cost about 0.1-0.3 ms together: film grain that follows brightness (finer and weaker in bright areas), lens fall-off (vignetting) and slight colour fringe at the edges, highlight roll-off (sky and white walls do not clip hard), slight bloom and halation around strong lights, the per-place photo-look LUT fitted offline from real photos of the place, a little sharpening matched to a phone or dash camera. Aerial perspective (haze on far hills) is **not** a post effect: it is the engine sky and height fog with humidity and season, so it is right in mirrors and at every distance.

### 11.7 How it decides when to add what (one pixel, worked)

A pixel 6 m ahead on the right road edge on US 129 in May:
1. Scan map says: surface code = shoulder gravel then asphalt edge; CORR says no paved shoulder, ditch on the right; forest group 500 (oak/hickory) on a north slope; AADT class medium; age class 12 years; climate Cfa; month May.
2. Rule set for "shoulder": gravel stones 1-4 cm placed by the CPU bake in a band 0-0.4 m from the asphalt edge (density from the cross-section), broken asphalt edge geometry every 3-8 m, leaf litter drift from the forest group with season weight (low in May), grass tufts into the gravel (humid climate weight high).
3. Rule set for "asphalt": edge cracks parallel to the edge (no shoulder support), wheel-path polish 0.6-1.2 m from the edge line, a sealed crack every 20-50 m (age class).
4. Distance band near: stones and edge are real 3D; at 80 km/h the J2 rule keeps them (they are in pass 1); at 160 km/h only the shader detail stays.
5. Camera layer and LUT over the whole image.
6. Check: the frame next to the real photo of the same place; the dull-world and cluttered-world checks (section 7).

### 11.8 What to build first (with the graph work in section 9)

1. **Road edge and shoulder rule set** (gravel, broken edge, grass into the edge, leaves) - biggest visible gap per cost on the Dragon and Big Sur.
2. **Asphalt and paint wear** from age and traffic classes (shader + CPU crack layout).
3. **Cut slopes from soil and rock data** (GEOL) instead of grass mounds.
4. **Camera layer** (grain, fall-off, roll-off) + per-place LUT, measured at under 0.3 ms.
5. **Tree structure and undergrowth** (vegetation.json ranks 1-3) with the trees team.
6. **Bench of a tiny post network** (11.5), only as a measured experiment.

---

## 12. Sources

- OpenFACADES (Liang et al., 2025): https://arxiv.org/html/2504.02866v2 [V]; project page https://ual.sg/project/openfacades/ [S]
- BMAT facade material dataset (Yin et al., ESSD 18, 8191-8209, 2026): https://essd.copernicus.org/articles/18/8191/2026/ [V]
- RoofNet (2025): https://arxiv.org/html/2505.19358v1 [V]
- GlobalBuildingAtlas (TUM, 2025), licence CC BY-NC 4.0: https://gee-community-catalog.org/projects/gba/ [V]
- Zhang, Aliaga et al., "Synthesis and Completion of Facades from Satellite Imagery", ECCV 2020: https://www.cs.purdue.edu/cgvlab/www/resources/papers/ECCV_2020_Zhang_Synthesis_Completion_Facades.pdf [S]
- FaçAID (2024): https://arxiv.org/html/2406.01829v2 [S]; GeoTexBuild (2025): https://arxiv.org/html/2504.08419v1 [S]
- UrbanWorld 2.0 (2025): https://arxiv.org/pdf/2511.18005 [S]; MetaUrban (2024): https://arxiv.org/pdf/2407.08725 [S]
- Barrington-Leigh and Millard-Ball, "The world's user-generated road map is more than 80% complete", PLoS ONE 2017: https://www.ncbi.nlm.nih.gov/pmc/articles/PMC5552279/ [S]; HeiGIT completeness exercise (saturation): https://giscience.courses-pages.gistools.geog.uni-heidelberg.de/openstreetmap-analyses-with-qgis-python-and-r/content/chapter_3/completeness_2.html [S]
- Mapillary, "Global access to map data with the Mapillary API" (free, personal and commercial use under its terms): https://blog.mapillary.com/update/2020/08/28/map-data-mapillary-api.html [V]; map feature classes: https://blog.mapillary.com/update/2019/01/23/updating-maps-with-cameras-42-new-point-features.html [V]; image licence CC BY-SA: https://help.mapillary.com/hc/en-us/articles/115001770409-CC-BY-SA-license-for-open-data [V]
- Census Survey of Construction, Characteristics of New Housing (annual, tables of primary exterior wall material, foundation, stories): https://census.gov/construction/chars/pdf/c25ann2017.pdf [S]
- USACE NSI fields: https://gee-community-catalog.org/projects/nsi/ [V]; FHWA HPMS field manual: https://www.fhwa.dot.gov/policyinformation/hpms/fieldmanual/page02.cfm [V]
- MSFS and Blackshark: see `flight-simulator-study.md` [V]
- Horizon Zero Dawn placement: https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn [S]
- OSM tile usage policy: https://operations.osmfoundation.org/policies/tiles/ [V]
- Overpass API user's manual, commons and limits: https://dev.overpass-api.de/overpass-doc/en/preface/commons.html [V]
- NAIP on AWS (requester pays): https://registry.opendata.aws/naip/ [V]
- USGS 3DEP lidar on AWS (usgs-lidar-public, free): https://registry.opendata.aws/usgs-lidar/ [V]
- Overture cloud sources: https://docs.overturemaps.org/getting-data/cloud-sources/ [V]
- GitHub Releases limits: https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases [V]
- Hugging Face storage limits: https://huggingface.co/docs/hub/storage-limits [V]
- Internet Archive torrents: https://help.archive.org/help/archive-bittorrents/ [V]
- Source Cooperative: https://docs.source.coop/what-is-source [V]
- Protomaps FAQ (self-hosting, bandwidth): https://protomaps.com/faq [V]
- NVIDIA DLSS Programming Guide (execution time table): copy read at https://primebuild-pc.github.io/guida-completa-nvidia-dlss-4.5/DLSS_Programming_Guide_Release.pdf [V]
- REGEN, real-time photorealism enhancement (Table I timings incl. EPE): https://arxiv.org/html/2508.17061v1 [V]
- Census Survey of Construction microdata and codes: https://www.census.gov/construction/chars/microdata.html, https://www2.census.gov/programs-surveys/soc/guidance/socmicro_info.pdf [V]
- NAHB Eye on Housing, wall material shares 2024: https://eyeonhousing.org/2025/10/vinyl-surpasses-stucco-as-most-used-principal-exterior-wall-material/ [V]; 2019: https://www.nahb.org/blog/2020/10/Regional-Variations-on-Most-Popular-Exterior-Wall-Materials [V]; 2013: https://rismedia.com/2014/09/14/vinyl-is-the-most-widely-used-siding-on-new-homes-started/ [V]
- NSI field notes: https://gee-community-catalog.org/projects/nsi/ [V]; USACE NSI webinar Q&A (Jan 2026): https://planning.erdc.dren.mil/toolbox/webinars/23Jan26-NSI_QA.pdf [V]
