// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

/**
 * The World cache (ADR 0005): the generated layers of each Tile (Place recipe, cleaned Aerial
 * imagery, AI surface detail, roadside, scan eye) saved on the player's disk and reused on later
 * visits. The raw map downloads live elsewhere (the Map helper's map-cache).
 *
 * An entry is one layer of one Tile at one generator version. A stored entry whose version differs
 * from the one asked for is a miss, so bumping one generator's version rebuilds only its layer.
 * Layers are plain bytes; each generator owns its format and its version constant.
 *
 * Eviction is by Tile, least recently driven first (a Load or Store of any layer of the Tile marks
 * it driven), down to the player's size limit (OSDS.WorldCache.LimitMiB). Tiles in the protected
 * set (the Ring around the car) are never evicted. A damaged file is a miss.
 *
 * Thread-safe. Callers run on worker threads: Load and Store do disk I/O and must never be called
 * from the frame path.
 */
class OSDS_API FOsdsWorldCache
{
public:
	/** Root = the cache folder (tests pass a temp folder). LimitBytes < 0 reads OSDS.WorldCache.LimitMiB. */
	explicit FOsdsWorldCache(const FString& InRoot, int64 InLimitBytes = -1);

	/** The game's cache: %LOCALAPPDATA%\OSDS\world-cache. */
	static FOsdsWorldCache& Get();
	static FString DefaultRoot();

	/** True and Out filled when this Tile has this layer at exactly this generator version. */
	bool Load(const FIntPoint& Tile16, FName Layer, int32 GeneratorVersion, TArray<uint8>& Out);

	/** True when this Tile has this layer at this generator version (header check only; no payload read). */
	bool Contains(const FIntPoint& Tile16, FName Layer, int32 GeneratorVersion) const;

	/** Saves (atomically, replacing any older version of the layer), then trims to the size limit. */
	bool Store(const FIntPoint& Tile16, FName Layer, int32 GeneratorVersion, TConstArrayView<uint8> Bytes);

	/** The one seed every generator uses for a Tile: the same on every visit and every machine. */
	static uint64 TileSeed(const FIntPoint& Tile16);

	/** Tiles that must stay (the Ring). Replaces the previous set. */
	void SetProtectedTiles(const TArray<FIntPoint>& Tiles);

	/** Trims to the limit now. Returns the number of Tiles removed. */
	int32 Evict();

	int64 TotalBytes();
	int64 LimitBytes() const;

	struct FStats
	{
		int64 Hits = 0;
		int64 Misses = 0;
		int64 Stores = 0;
		int64 EvictedTiles = 0;
	};
	FStats GetStats() const;

private:
	struct FTileInfo
	{
		int64 Bytes = 0;
		int64 LastDrivenTicks = 0;
		TMap<FName, int64> LayerBytes;
	};

	FString TileDir(const FIntPoint& Tile) const;
	FString LayerFile(const FIntPoint& Tile, FName Layer) const;
	void EnsureIndex();
	void MarkDriven(const FIntPoint& Tile);
	int32 EvictLocked();

	FString Root;
	int64 FixedLimit;
	mutable FCriticalSection Lock;
	bool bIndexed = false;
	TMap<FIntPoint, FTileInfo> Index;
	TSet<FIntPoint> Protected;
	int64 Total = 0;
	FStats Stats;
};
