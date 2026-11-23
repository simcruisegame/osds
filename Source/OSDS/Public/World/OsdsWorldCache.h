// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include <atomic>

OSDS_API DECLARE_LOG_CATEGORY_EXTERN(LogOsdsWorldCache, Log, All);

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
 * Thread-safe. Callers run on worker threads: Load, Store, Evict and WarmIndex do disk I/O and must
 * never be called from the frame path. The frame path may call SetProtectedTiles, TotalBytes,
 * LimitBytes and GetStats: they take only tiny locks or atomics and never wait for disk work.
 * A damaged file (bad magic, size or checksum) is deleted from disk and the index when it is read.
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

	/**
	 * Saves (atomically, replacing any older version of the layer), then trims to the size limit.
	 * A failed write is logged, retried once after evicting, and returns false: callers check it.
	 */
	bool Store(const FIntPoint& Tile16, FName Layer, int32 GeneratorVersion, TConstArrayView<uint8> Bytes);

	/** The one seed every generator uses for a Tile: the same on every visit and every machine. */
	static uint64 TileSeed(const FIntPoint& Tile16);

	/** Reads the cache folder into the index now (worker thread; Load, Store and Evict do it on first use). */
	void WarmIndex();

	/** Tiles that must stay (the Ring). Replaces the previous set. Frame-path safe: a copy and a swap under a tiny lock. */
	void SetProtectedTiles(const TArray<FIntPoint>& Tiles);

	/** Trims to the limit now. Returns the number of Tiles removed. */
	int32 Evict();

	/** Bytes on disk as far as the index knows (0 until WarmIndex or a first Load/Store). Never waits. */
	int64 TotalBytes() const;
	int64 LimitBytes() const;

	struct FStats
	{
		int64 Hits = 0;
		int64 Misses = 0;
		int64 Stores = 0;
		int64 EvictedTiles = 0;
		int64 CorruptDeleted = 0;
		int64 StoreFailures = 0;
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
	/** Index lock held by the caller: forgets one layer (a damaged file was deleted). */
	void ForgetLayerLocked(const FIntPoint& Tile, FName Layer);
	/** Evicts least recently driven unprotected Tiles down to Target bytes: victims leave the index under the
	 *  lock (memory only), their folders are deleted with no lock held. Returns the Tiles removed. */
	int32 EvictToTarget(int64 Target);
	bool WriteFile(const FString& File, const TArray<uint8>& Raw);

	FString Root;
	int64 FixedLimit;

	/** In-memory index edits only: never held across disk I/O. */
	mutable FCriticalSection IndexLock;
	TMap<FIntPoint, FTileInfo> Index;

	/** The Ring: its own tiny lock so the game thread never waits for the index. */
	mutable FCriticalSection ProtectedLock;
	TSet<FIntPoint> Protected;

	/** One directory scan at a time (workers only); the index lock is taken only to merge the result. */
	FCriticalSection ScanLock;
	std::atomic<bool> bIndexed{false};

	std::atomic<int64> Total{0};
	std::atomic<int64> Hits{0};
	std::atomic<int64> Misses{0};
	std::atomic<int64> Stores{0};
	std::atomic<int64> EvictedTiles{0};
	std::atomic<int64> CorruptDeleted{0};
	std::atomic<int64> StoreFailures{0};
};
