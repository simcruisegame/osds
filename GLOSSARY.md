# OSDS

OSDS (Open Source Driving Simulator) is a realistic driving game that builds the world around the car from real-world data, anywhere in the US.

## World

**Tile**:
One square of the world (a zoom-16 web map tile), the unit the world is fetched, built and streamed in.
_Avoid_: chunk, cell (a Cell is something else)

**Ring**:
The set of Tiles kept built around the car.

**Seam**:
The line where two Tiles meet; roads, ground and banking must match across it.
_Avoid_: tile border, tile edge

**Ribbon**:
The built surface of one road: carriageway, markings, kerbs, gutters and sidewalks.

**Lane**:
A traffic lane on a road. Only that.
_Avoid_: using "lane" for a working copy of the repo (that is a Worktree)

**Cell**:
A stretch of about 32 m of road with its own context (land use, roadside, region).

**Place graph**:
The facts and rules about a place that decide what its buildings, roadside and furniture look like.

**Map helper**:
The separate background program that downloads and prepares map data for each Tile.
_Avoid_: atlas (for this)

**Aerial imagery**:
The photographs from above that colour and inform the ground.
_Avoid_: atlas (for this)

**Atlas**:
The in-game screen where the player searches for a place to drive.

**Hero road**:
One of the fixed roads used to compare builds over time (Ocean Drive, the Dragon, Nebraska, Big Sur).

**Random US place**:
A start drawn from the test-place list so every change is judged away from the Hero roads too.

**Race track**:
A real oval or road course the player can pick, built from map data and published banking.

## Cars and driving

**Drivable car**:
A car the player can pick and drive. In 3.0 there are exactly five: Miata, Corvette, Mini, Porsche, Audi.
_Avoid_: car (when it matters which kind)

**Traffic car**:
A car the game drives as traffic. Traffic cars are never offered in the car picker.

**Car file**:
The data file that describes one car: its real-world numbers, model, tuning ranges and sources.

**Trim**:
One version of a car (year, engine, options). Each Drivable car ships in one Trim.

**Bench**:
A repeatable, scripted test of one car against published numbers (0-60, 70-0, skidpad, step steer and others).

**Band**:
The pass range for a Bench result, taken from a real road test or standard.

**ESC**:
Electronic stability control: the system that brakes single wheels to stop a skid.
_Avoid_: stability assist, traction control (a different system)

**Gamepad**:
The player's handheld controller (an Xbox controller).
_Avoid_: pad

**Test pad**:
The flat, empty surface the drive robot and Benches use.
_Avoid_: pad

## Testing and release

**Drive robot**:
The scripted driver that drives a car through set manoeuvres or along a road for tests.

**Preflight**:
A recorded set of Drive robot runs over Hero roads, Random US places and Race tracks, judged for spins, jolts, pop-ins and flashes.

**View check**:
Fixed camera views captured and judged automatically.

**Launch and look**:
The rule that nobody calls a change working until they have run the game and looked at the captured frames.

**Done-test**:
The check that decides when a piece of work is finished.

**Release candidate**:
A packaged build that is a candidate to ship, given one full release test.

## People and work

**Owner**:
The person who owns OSDS and approves each release.

**Coordinator**:
The main session that plans, reviews, merges and runs QA.

**Thread**:
A session that does one area's work in its own Worktree.

**Worktree**:
A separate working copy of the repository on its own branch, used by one Thread.
_Avoid_: lane

**Cycle**:
The work toward one version, from plan to release.
