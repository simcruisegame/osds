// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Knowledge/OsdsPlaceGraph.h"

/**
 * Open data about the places of a tile, for the place knowledge graph. Free, keyless,
 * legal sources only:
 *
 *   OpenStreetMap  Overpass API: building and named-place tags (start_date,
 *                  building:architecture, building:material, building:colour,
 *                  roof:shape, building:levels, heritage, wikidata). ODbL, credit
 *                  "(c) OpenStreetMap contributors".
 *   Wikidata       SPARQL (wikibase:around): architectural style P149, inception
 *                  P571, architect P84, heritage designation P1435, material P186,
 *                  instance of P31 (historic districts). CC0.
 *
 * Records are cached per tile: the bundled data/knowledge/places/<x>_<y>.json first,
 * then Saved/Knowledge/places/<x>_<y>.json (fetched while playing). No network code
 * here: the parsers and the merge are pure, so each one has a test with a recorded
 * answer (UOsdsPlaceGraphSubsystem does the fetching).
 */
namespace OsdsPlaceFacts
{
	/** Overpass QL for a tile's buildings and tagged places. */
	OSDS_API FString OverpassQuery(const FIntPoint& Tile);
	/** Wikidata SPARQL for the items around the tile centre. */
	OSDS_API FString WikidataQuery(const FIntPoint& Tile);

	/** The public Overpass instances, main first (all free; each is busy at times). */
	OSDS_API const TArray<FString>& OverpassUrls();
	OSDS_API FString OverpassUrl(int32 Attempt = 0);
	OSDS_API FString WikidataUrl(const FString& Query);

	OSDS_API bool ParseOverpass(const FString& Json, TArray<FOsdsPlaceRecord>& Out, FString& OutError);
	OSDS_API bool ParseWikidata(const FString& Json, TArray<FOsdsPlaceRecord>& Out, FString& OutError);

	/** One record per place: Wikidata items join the OSM element that names them (styles, year, architect added). */
	OSDS_API void Merge(const TArray<FOsdsPlaceRecord>& Osm, const TArray<FOsdsPlaceRecord>& Wikidata, TArray<FOsdsPlaceRecord>& Out);

	/** Keeps only records that tell the graph something (a style, a year, a colour, a heritage ...). */
	OSDS_API bool IsUseful(const FOsdsPlaceRecord& Record);

	OSDS_API FString ToJson(const FIntPoint& Tile, const TArray<FOsdsPlaceRecord>& Records, const FString& Sources);
	OSDS_API bool FromJson(const FString& Json, TArray<FOsdsPlaceRecord>& Out, FString& OutError);

	OSDS_API FString BundledPath(const FIntPoint& Tile);
	OSDS_API FString CachePath(const FIntPoint& Tile);

	/** The tile's records from the bundle or the cache (thread-safe, kept in memory). False when none is saved yet. */
	OSDS_API bool ForTile(const FIntPoint& Tile, TArray<FOsdsPlaceRecord>& Out);
	/** Saves fetched records to the cache and makes them the tile's records. */
	/** bSave false: for this run only (a partial answer, e.g. Overpass was busy), so the next run asks again. */
	OSDS_API void Store(const FIntPoint& Tile, const TArray<FOsdsPlaceRecord>& Records, const FString& Sources, bool bSave = true);
	/** True when the tile has a saved answer (even an empty one). */
	OSDS_API bool IsKnown(const FIntPoint& Tile);
}
