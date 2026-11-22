# Request budgets

A **Request budget** is the most requests per kilometre driven that OSDS may make to one public service (see `GLOSSARY.md`). It is how a 100-mile drive stays polite to free services (OSM tile policy: no bulk or offline use of OSM raster tiles; Overpass is very limited).

- Table: `helper/request-budgets.json` (host -> `burst`, `perKm`, `policy`). A host not listed uses `default`. Numbers are **(estimate)**, sized from a Ring of radius 1 (about 6 new Tiles per km at zoom 16) and each service's policy; the Long drive reports the real counts.
- A session (a new start or teleport) earns `burst` requests to fill the first Ring, then `perKm` for each kilometre driven. Budget = `burst + perKm x km`.
- Accounting lives in the raw map cache's `.source-state/`: the game writes `session.json` (`started`, `km`); the helper keeps `<host>.spent` per session. `helper/atlas-cache.mjs` `spendBudget` takes one request before each network call. Over budget the helper makes no request and returns the stale cache file if it has one; otherwise the Tile waits and the streamer asks again as the car drives on.
- Without a `session.json` (tests, the helper run by hand) nothing is limited.
- Per-host request spacing and concurrency caps (`hostCap`, `acquireNetworkSlot`) and the failure backoff (`backoffMs`) stay as they were; the budget is on top of them.
- The Long drive (`scripts\long-drive.ps1`) prints the helper's request count for the run; a warm drive over the same route must make none.
- Test: `bun lab/check-request-budget.mjs` (part of `sh scripts/check-helper.sh`).
