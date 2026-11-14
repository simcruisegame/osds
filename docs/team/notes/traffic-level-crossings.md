# Level crossings: a bridge and the road under it

> **v2.0.3 uses the driving lane's own rule** (rc5-traffic 27913ee). A crossing counts unless the two roads are 4.9 m or more apart in height at the crossing point, and `ChooseAt` skips roads more than 3 m above or below the turn point. The record and queries below (`FSimCruiseRoadCrossing`, `LevelConflictAt`, `AcrossLevels`) are v2.0.4 work on branch `world-detail-v2-release-6`. They are not in the v2.0.3 candidate, and this note keeps them as a reference.

The tile data marks bridges (`bridge` on a road row) but carries no OSM `layer`, `tunnel` or `cutting`, and the elevation model has no cuts or embankments. So most mapped overpasses are built at one level with the road under them: Woodward Avenue over the Fisher Freeway (I-75) in Detroit, Northcrest Road over I-285 in Doraville. Since v2.0.3 no parapet or deck slab stands across such a road, so the roads cross flat. They are still two levels in the map: traffic must not turn from one onto the other there.

## The record (`World/SimCruiseRoadNetwork.h`)

Each tile's road network lists every deck crossing in `FSimCruiseRoadNetwork::Crossings`. A crossing is a bridge span over a carriageway that is not a deck there, crossed at 20 degrees or more.

```cpp
struct FSimCruiseRoadCrossing
{
    int32 Deck;                 // the bridge ribbon
    int32 DeckFrom, DeckTo;     // its stations over the other road
    int32 Under;                // the road under it
    int32 UnderFrom, UnderTo;
    double DeckS, UnderS;       // the crossing point along each (m)
    double ClearanceM;          // the deck's underside (1.2 m under its surface) over the road
    bool bAtGradeConflict;      // ClearanceM < 4.9: two levels in the map, one in the game
};
```

## Queries

- `bool LevelConflictAt(int32 Ribbon, double S, double ReachM = 20.0, int32* OutOther = nullptr) const`: whether ribbon `Ribbon` at `S` is within `ReachM` of a crossing with `bAtGradeConflict`. `OutOther` receives the road it crosses.
- `bool AcrossLevels(int32 RibbonA, double SA, int32 RibbonB, double SB, double ReachM = 20.0) const`: whether going from `RibbonA` at `SA` onto `RibbonB` at `SB` would cross from a deck to the road under it (or back) at a listed crossing.

Both look at one tile's network. A crossing lies in the tile that holds the deck's stations, so ask the tile that holds both ribbons.

## The traffic fix (for the driving lane)

`ASimCruiseTraffic::ChooseAt` (`World/SimCruiseTraffic.cpp`) offers a turn onto every public ribbon within 14 m of the decision point in plan view. It checks neither height nor bridge, so at Woodward a car on I-75 can turn onto the overpass. In the loop over `Best` (after `EntryS` is known), skip the option when it crosses levels:

```cpp
// A bridge and the road under it are no junction (docs/team/level-crossings.md).
if (TileId == Car.Tile && Build->Roads.AcrossLevels(Car.Ribbon, DecisionS, Hit.Key, EntryS)) continue;
```

For a ribbon in a neighbouring tile (a crossing on a seam), skip the option when exactly one of the car's station at `DecisionS` and `Other.St[I]` is `bBridge`.

## A fast test for it

`SimCruise.World.TrafficLevelCrossings` (driving lane): build the recorded Doraville and Detroit-Woodward tiles. For every crossing with `bAtGradeConflict`, put a traffic car on each ribbon 30 m before the crossing, heading towards it. Call `ChooseAt` at the crossing and fail if any option it offers is the other ribbon, i.e. `AcrossLevels` holds for the option. The Miami bundled tiles have no crossings, so the test needs the recorded places, or two synthetic ribbons, one flagged bridge, crossing at one level.

## Counts (tile data, plan view)

Bridge pieces crossing another drivable road: Detroit round Woodward 37, Doraville 85, Big Sur 0, Detroit start tiles 0, Miami bundled tiles 0 (their format has no bridge flag). `SimCruise.World.RoadOverlaps.Recorded` lists how many are at one level per place in `Saved/BridgeFix/changed-spans.md`.

## Later (v2.0.4)

Lower the road under a bridge into a cut, or lift the deck, to a real 4.9 m clearance, with embankment and cut terrain. This needs `layer`, `tunnel` and `cutting` in the tile data. Branch `v2.0.4-bridge-lift` has the deck lift.
