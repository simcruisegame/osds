# World graph V0: learned tables (first real-data run, 15 November 2026)

Made on the development PC by `lab/world-graph/learn_walls.py` and `lab/world-graph/learn_roadside.py`
(run steps in `lab/world-graph/README.md`). The game reads none of these yet.

| Files | Source | Licence and credit |
|---|---|---|
| `walls_soc.csv`, `walls_secondary.csv`, `walls_prior.csv`, `storeys_base.csv`, `lot_house.csv` | U.S. Census Bureau, Survey of Construction public-use microdata 1999-2025 (plus the expert rows in `materials.json` for older eras) | US public domain; credit "U.S. Census Bureau, Survey of Construction" |
| `roadside_priors.csv`, `roadside_spacing.csv`, `roadside_offsets.csv`, `fence_share.csv`, `culvert_share.csv`, `stop_share.csv`, `cooccur.csv`, `county_completeness.csv`, `t1_heldout.csv`, `run_log.json`, `LICENCE.txt` | OpenStreetMap through the Overpass API, one request at a time with pauses | A derivative database of OpenStreetMap: ODbL 1.0, credit "Map data (c) OpenStreetMap contributors" (share-alike applies to these tables) |

NSI was not used in this run (`nsi_aggregate.csv` is empty). `run_log.json` is the roadside run's log
(the wall run wrote its own first; both runs share one output folder).

## Coverage of this run

- Walls: all 9 Census divisions, eras 1999-2025 from SOC, older eras from the expert table.
- Roadside: 47 counties in `counties.json`; 19 answered, 28 failed on Overpass "502 Gateway Timeout"
  (large counties such as San Francisco, King WA, Maricopa, Dallas, and the Alaska and Hawaii ones) after
  two runs. Smaller request tiles (`--tile-deg 0.1`) would split them into many more requests; that is
  not done here, to keep the load on the public Overpass server low. Counties that passed the
  completeness gate per thing: fence 11, bench 10, trash bin 10, collection mailbox 10, street light 8,
  fire hydrant 4, utility pole 4, stop sign 0 (no training county passed its gate).
- `t1_heldout.csv` compares the pooled prior with the held-out counties; a held-out county's own count is
  only trustworthy where its completeness passes the gate (most held-out counties are thinly mapped).
