// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Street furniture where OpenStreetMap maps it: benches, litter bins, bicycle racks, post
 * boxes, fire hydrants, bollards and bus stops (with or without a shelter). Free, keyless
 * (Overpass, ODbL). One file per zoom-16 tile: data/knowledge/furniture/<x>_<y>.json
 * (bundled start tiles), then Saved/Knowledge/furniture/ (fetched while playing).
 * OsdsStreetFurniture (World/) draws them, at their real size, on the sidewalk.
 */
struct OSDS_API FOsdsFurniture
{
	double Lat = 0.0;
	double Lon = 0.0;
	FName Kind;            // bench, waste_basket, bicycle_parking, post_box, fire_hydrant, bollard, bus_stop
	bool bShelter = false; // bus_stop: shelter=yes
	bool bBench = false;   // bus_stop: bench=yes
	float DirectionDeg = -1.0f; // OSM direction=*, degrees from north, or -1
	FString Colour;        // OSM colour=* / fire_hydrant:colour=*, if mapped
};

namespace OsdsFurniture
{
	/** The Overpass union lines for a bounding box "(s,w,n,e)". */
	OSDS_API FString OverpassLines(const FString& Box);
	/** Furniture nodes out of an Overpass answer (other elements are skipped). */
	OSDS_API bool ParseOverpass(const FString& Json, TArray<FOsdsFurniture>& Out, FString& OutError);

	OSDS_API FString ToJson(const FIntPoint& Tile, const TArray<FOsdsFurniture>& Items);
	OSDS_API bool FromJson(const FString& Json, TArray<FOsdsFurniture>& Out);
	OSDS_API FString BundledPath(const FIntPoint& Tile);
	OSDS_API FString CachePath(const FIntPoint& Tile);

	/** The tile's furniture from the cache or the bundle (thread-safe). False when none was fetched. */
	OSDS_API bool ForTile(const FIntPoint& Tile, TArray<FOsdsFurniture>& Out);
	OSDS_API void Store(const FIntPoint& Tile, const TArray<FOsdsFurniture>& Items, bool bSave = true);
}
