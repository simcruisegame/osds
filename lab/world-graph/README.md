# World knowledge graph, first step (V0), 15 Nov 2026

Innovation lane. Design: `docs/research/innovation/world-knowledge-graph.md` (v3; sections 4-9 the graph and method, 10 data supply at scale, 11 "id to detail"). These files are the first step that the coordinator accepted on 14 Nov 23:45Z: the graph tables, the learning script for the roadside things, the wall-material priors from the Census building survey, plus the tree and bush layer and the building variety between neighbours.

Nothing here is game code. The AI lane runs the two scripts on the PC; the world lane writes the sampler in the scan engine that reads the tables. In the repo the tables are in `data/knowledge/world/` and the scripts in `lab/world-graph/` (the scripts find the tables in either place).

## Files

| File | What it is |
|---|---|
| `things.json` | 14 roadside things (12 V0 + guard rail + blue collection box): OSM match rules, completeness gates, sizes, offsets, spacing checks, exclusions, co-occurrence, variants, colour rules, collision class, night behaviour. 9 learned from OSM, 5 from written standards (residential mailbox: USPS; delineator, speed sign, curve sign: MUTCD; driveway culvert: rule). Also the context fields. |
| `anchors.json` | 11 anchors (road edge, driveway, junction, junction approach, curve, maxspeed change, stream crossing, ditch-driveway crossing, bus stop, building front, parcel edge) with stable-id rules (no rules version in the seed). |
| `materials.json` | Wall materials with library keys, albedo and pattern parameters, wear, compatibility; Census SOC code map; published seed shares (2013, 2019, 2024) until the script runs; expert table for eras before 1999 (confidence low) with state overrides (FL, OR, WA, AK, HI, NM, AZ). |
| `vegetation.json` | Tree and bush layer: what changes first for trees (ranked), layers, FIA forest-type groups with crown form and undergrowth, non-forest cover, roadside profile (mowed strip, edge wall, power-line gap), residential trees and shrubs by region, season, Alaska and Hawaii. |
| `buildings-variety.json` | Building variety between neighbours: what changes first for buildings (ranked), block-mix concentration by era, builder plans with anti-monotony, styles by region x era, roof form and pitch, ground contact, small parts, manufactured homes, construction sites. |
| `sources.json` | Every source with licence, credit line, terms and status; the do-not-use list. |
| `counties.json` | 47 candidate counties (30 training candidates over all 9 Census divisions, 17 held out: hero roads, Doraville, sparse places, Alaska, Hawaii). |
| `learn_roadside.py` | Learns rates, spacing, lumpiness, offsets, fence shares, culvert shares, stop-sign shares, co-occurrence from OSM in counties that pass each thing's completeness gate; T1 on held-out counties. |
| `learn_walls.py` | Learns wall material (principal and secondary), storeys, foundation, lot and house size, garage, porch from the Census SOC microdata 1999-2025; optional NSI aggregates (public fields, aggregates only); writes `walls_prior.csv` for the scan engine. |

## How to run (PC)

```
pip install numpy requests pandas openpyxl xlrd
python learn_roadside.py --selftest      # no network, about 1 s: 13 checks must PASS
python learn_walls.py --selftest         # no network, a few s: 7 checks must PASS
python learn_walls.py --out out          # downloads the SOC files once (27 files), learns
python learn_roadside.py --out out       # Overpass, one request at a time, 10 s pause, resumable
python learn_walls.py --out out --nsi 55025,31109,47093   # optional NSI aggregates
```

- Network use, learn_roadside: one boundary query per county plus one query per 0.25 degree tile; about 700 requests for all 47 counties (the big Alaska and desert counties have many small, almost empty tiles), about 2-4 hours at a 10 s pause; far below the Overpass limits (about 10,000 requests and 1 GB a day). A stopped run continues from the cache (`cache/osm/`). Slow link: `--tile-deg 0.1 --pause 20`. Re-run with `--offline` to re-learn from the cache only.
- Network use, learn_walls: 27 SOC files from census.gov, once (cached in `cache/soc/`). NSI is never cached (terms); each `--nsi` county is one API call.
- Slow PC: both scripts use one thread. `--fence-step 40` halves the fence work. Large counties (Maricopa, Cook-size) need 2-4 GB RAM.
- The self-tests were run in the cloud on 15 Nov: 13/13 and 7/7 PASS (synthetic data only; the cloud cannot reach Overpass or census.gov).

## Outputs (in `out/`)

learn_roadside: `roadside_priors.csv` (rate per 100 m by thing x road class x density bin x division, raw and shrunk), `roadside_spacing.csv` (gap quantiles, robust CV, gamma shape, Fano factor, line share), `roadside_offsets.csv`, `fence_share.csv`, `culvert_share.csv`, `stop_share.csv`, `cooccur.csv`, `county_completeness.csv`, `t1_heldout.csv`, `run_log.json`, `LICENCE.txt`.

learn_walls: `walls_soc.csv`, `walls_secondary.csv`, `storeys_base.csv`, `lot_house.csv`, `nsi_aggregate.csv`, `walls_prior.csv`, `run_log.json`.

What to check after the first real run: `county_completeness.csv` (which counties passed which gate; aim about 20 per thing), `t1_heldout.csv` (ratio predicted/measured near 1 only where the held-out completeness also passes), and that `walls_soc.csv` matches the published 2024 leaders in `materials.json` (for example division 1 vinyl about 0.73, division 9 stucco about 0.64, division 7 brick about 0.48).

## Licences

- Everything learned from OSM is a derivative database: ODbL 1.0, credit "Map data (c) OpenStreetMap contributors". The repo is public, so we publish it under ODbL.
- Census SOC, ACS, TIGER; USGS; USDA (TreeMap, FIA); LANDFIRE; NLCD: public domain.
- NSI (cleared by the world lane 14 Nov 23:53Z): public fields only (bldgtype, found_type, found_ht, num_story, med_yr_blt, occtype); never the restricted fields (p_extwall and others); no saved or bundled raw responses; aggregates only; credit "U.S. Army Corps of Engineers National Structure Inventory (USACE NSI)". `learn_walls.py` drops every other field before it counts, and its self-test checks that.
- Standards (MUTCD federal; USPS, NFPA, AASHTO): numbers as facts only, no copied text.

## What the sampler (world lane) reads

Per tile, in pass 1: anchors (anchors.json) -> context (road class, density bin, division, hooks) -> `roadside_priors.csv` shrunk rate -> stable dice thinning -> spacing from `roadside_spacing.csv` (gamma gaps with the learned shape, hard-core minimum from p05; a road gets a line of poles only with probability `line_share`) -> offsets -> exclusions and co-occurrence -> lidar veto for things taller than 3 m -> variant and parameters -> INST with the "imagined" flag. Materials: posterior of `walls_prior.csv` row (state override first for AK, HI, FL, OR, WA, NM, AZ) x eye likelihood, sampled through the block mix (buildings-variety.json alpha by era), secondary material from `walls_secondary.csv`.

## Open items (status 15 Nov)

| Item | Status |
|---|---|
| NSI terms | Cleared, with limits (above). Packs at scale: proposal in design 10.5 that players never call NSI; the world lane confirms if block-group aggregates may go in packs. |
| Alaska and Hawaii priors | Written (materials.json state overrides, vegetation.json `alaska_hawaii`, buildings-variety.json styles); 6 held-out counties for T1. The Census Pacific division is mostly California, so AK and HI never use the division row. |
| Construction sites in aerial photos | Design written (buildings-variety.json `construction_sites`); needs one test place. |
| Slow PC (4 cores) | Scripts are one thread and run once; the run-time sampler is lookups. 4-core game profile test still to run (affinity mask). |
| Slow internet | Scripts resume from cache; game side is design section 10 (packs, range reads, cache). Throttled-link test (10 Mbit/s) still to run. |
| Not yet checked against real data | All learned numbers: the first PC run gives them. Expert rows (pre-1999 walls, styles, undergrowth, block alpha) stay "confidence low" until measured. |
