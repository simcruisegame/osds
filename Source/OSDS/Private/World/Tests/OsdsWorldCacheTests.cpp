// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "World/OsdsWorldCache.h"

namespace OsdsWorldCacheTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	FString TempRoot()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests/WorldCache"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	TArray<uint8> Bytes(int32 Count, uint8 Seed)
	{
		TArray<uint8> Out;
		Out.SetNumUninitialized(Count);
		for (int32 i = 0; i < Count; ++i) Out[i] = uint8(Seed + i * 7);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsWorldCacheHitMissTest, "OSDS.World.WorldCache.HitMissVersion", OsdsWorldCacheTests::Flags)
bool FOsdsWorldCacheHitMissTest::RunTest(const FString& Parameters)
{
	const FString Root = OsdsWorldCacheTests::TempRoot();
	FOsdsWorldCache Cache(Root, 1 << 20);
	const FIntPoint Tile(18180, 27907);
	TArray<uint8> Got;

	TestFalse(TEXT("empty cache misses"), Cache.Load(Tile, TEXT("recipe"), 1, Got));
	const TArray<uint8> Recipe = OsdsWorldCacheTests::Bytes(300, 3);
	const TArray<uint8> Eye = OsdsWorldCacheTests::Bytes(500, 9);
	TestTrue(TEXT("store recipe"), Cache.Store(Tile, TEXT("recipe"), 1, Recipe));
	TestTrue(TEXT("store eye"), Cache.Store(Tile, TEXT("eye"), 4, Eye));
	TestTrue(TEXT("recipe hit"), Cache.Load(Tile, TEXT("recipe"), 1, Got));
	TestTrue(TEXT("recipe bytes identical"), Got == Recipe);

	// Bumping one generator's version invalidates only its own layer.
	TestFalse(TEXT("recipe v2 misses"), Cache.Load(Tile, TEXT("recipe"), 2, Got));
	TestTrue(TEXT("eye v4 still hits"), Cache.Load(Tile, TEXT("eye"), 4, Got) && Got == Eye);
	TestTrue(TEXT("rebuilt recipe v2 stored"), Cache.Store(Tile, TEXT("recipe"), 2, OsdsWorldCacheTests::Bytes(40, 1)));
	TestTrue(TEXT("recipe v2 hits"), Cache.Load(Tile, TEXT("recipe"), 2, Got) && Got.Num() == 40);
	TestFalse(TEXT("another tile misses"), Cache.Load(FIntPoint(1, 2), TEXT("recipe"), 2, Got));

	// A new instance on the same folder (a later game start) sees the same data.
	FOsdsWorldCache Again(Root, 1 << 20);
	TestTrue(TEXT("hit after restart"), Again.Load(Tile, TEXT("eye"), 4, Got) && Got == Eye);
	TestEqual(TEXT("size survives restart"), Again.TotalBytes(), Cache.TotalBytes());

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsWorldCacheCorruptTest, "OSDS.World.WorldCache.CorruptIsMiss", OsdsWorldCacheTests::Flags)
bool FOsdsWorldCacheCorruptTest::RunTest(const FString& Parameters)
{
	const FString Root = OsdsWorldCacheTests::TempRoot();
	FOsdsWorldCache Cache(Root, 1 << 20);
	const FIntPoint Tile(5, 6);
	TArray<uint8> Got;
	Cache.Store(Tile, TEXT("recipe"), 1, OsdsWorldCacheTests::Bytes(200, 2));
	const FString File = FPaths::Combine(Root, TEXT("v1/5/6/recipe.bin"));
	TArray<uint8> Raw;
	TestTrue(TEXT("file exists"), FFileHelper::LoadFileToArray(Raw, *File));
	Raw.Last() ^= 0x55;
	FFileHelper::SaveArrayToFile(Raw, *File);
	TestFalse(TEXT("flipped byte is a miss"), Cache.Load(Tile, TEXT("recipe"), 1, Got));
	Raw.SetNum(10);
	FFileHelper::SaveArrayToFile(Raw, *File);
	TestFalse(TEXT("truncated file is a miss"), Cache.Load(Tile, TEXT("recipe"), 1, Got));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsWorldCacheEvictionTest, "OSDS.World.WorldCache.Eviction", OsdsWorldCacheTests::Flags)
bool FOsdsWorldCacheEvictionTest::RunTest(const FString& Parameters)
{
	const FString Root = OsdsWorldCacheTests::TempRoot();
	const int32 Entry = 1000;
	// Room for three Tiles (payload plus the 20-byte header each).
	FOsdsWorldCache Cache(Root, 3 * (Entry + 20) + 10);
	TArray<uint8> Got;
	auto Put = [&](int32 X) { Cache.Store(FIntPoint(X, 0), TEXT("recipe"), 1, OsdsWorldCacheTests::Bytes(Entry, uint8(X))); FPlatformProcess::Sleep(0.02f); };

	Put(1); Put(2); Put(3);
	TestEqual(TEXT("nothing evicted under the limit"), int32(Cache.GetStats().EvictedTiles), 0);
	// Driving over tile 1 again makes tile 2 the least recently driven.
	TestTrue(TEXT("tile 1 hit"), Cache.Load(FIntPoint(1, 0), TEXT("recipe"), 1, Got));
	FPlatformProcess::Sleep(0.02f);
	Put(4);
	TestFalse(TEXT("least recently driven tile 2 evicted"), Cache.Load(FIntPoint(2, 0), TEXT("recipe"), 1, Got));
	TestTrue(TEXT("tile 1 kept (driven again)"), Cache.Load(FIntPoint(1, 0), TEXT("recipe"), 1, Got));
	TestTrue(TEXT("tile 3 kept"), Cache.Load(FIntPoint(3, 0), TEXT("recipe"), 1, Got));
	TestTrue(TEXT("tile 4 kept"), Cache.Load(FIntPoint(4, 0), TEXT("recipe"), 1, Got));
	TestTrue(TEXT("under the limit"), Cache.TotalBytes() <= Cache.LimitBytes());

	// The Ring is never evicted, even when it is the oldest.
	Cache.SetProtectedTiles({FIntPoint(3, 0), FIntPoint(1, 0)});
	FPlatformProcess::Sleep(0.02f);
	Cache.Load(FIntPoint(4, 0), TEXT("recipe"), 1, Got);
	FPlatformProcess::Sleep(0.02f);
	Put(5);
	TestTrue(TEXT("protected tile 3 survives"), Cache.Load(FIntPoint(3, 0), TEXT("recipe"), 1, Got));
	TestTrue(TEXT("protected tile 1 survives"), Cache.Load(FIntPoint(1, 0), TEXT("recipe"), 1, Got));
	TestTrue(TEXT("tile 5 stored survives"), Cache.Load(FIntPoint(5, 0), TEXT("recipe"), 1, Got));

	// The index rebuilt from disk after a restart agrees.
	FOsdsWorldCache Again(Root, 3 * (Entry + 20) + 10);
	TestEqual(TEXT("restart total matches"), Again.TotalBytes(), Cache.TotalBytes());
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsWorldCacheSeedTest, "OSDS.World.WorldCache.StableSeed", OsdsWorldCacheTests::Flags)
bool FOsdsWorldCacheSeedTest::RunTest(const FString& Parameters)
{
	// Fixed values: the seed must never change between versions of the game (a Tile stays itself).
	TestEqual(TEXT("seed is repeatable"), FOsdsWorldCache::TileSeed(FIntPoint(18180, 27907)), FOsdsWorldCache::TileSeed(FIntPoint(18180, 27907)));
	TestNotEqual(TEXT("neighbours differ"), FOsdsWorldCache::TileSeed(FIntPoint(18180, 27907)), FOsdsWorldCache::TileSeed(FIntPoint(18181, 27907)));
	TestNotEqual(TEXT("x and y are not interchangeable"), FOsdsWorldCache::TileSeed(FIntPoint(1, 2)), FOsdsWorldCache::TileSeed(FIntPoint(2, 1)));
	TestEqual(TEXT("splitmix64 of tile (0,0)"), FOsdsWorldCache::TileSeed(FIntPoint(0, 0)), uint64(0xE220A8397B1DCDAFull));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsWorldCacheConcurrentTest, "OSDS.World.WorldCache.ConcurrentWriters", OsdsWorldCacheTests::Flags)
bool FOsdsWorldCacheConcurrentTest::RunTest(const FString& Parameters)
{
	const FString Root = OsdsWorldCacheTests::TempRoot();
	FOsdsWorldCache Cache(Root, 1 << 24);
	const TArray<uint8> Same = OsdsWorldCacheTests::Bytes(4000, 7);
	// Many writers and readers on one layer: a reader sees a whole entry or none, never half.
	TAtomic<int32> Torn{0};
	ParallelFor(64, [&](int32 i)
	{
		if (i % 2 == 0) Cache.Store(FIntPoint(9, 9), TEXT("recipe"), 1, Same);
		else
		{
			TArray<uint8> Got;
			if (Cache.Load(FIntPoint(9, 9), TEXT("recipe"), 1, Got) && Got != Same) Torn++;
		}
	});
	TArray<uint8> Got;
	TestEqual(TEXT("no torn reads"), Torn.Load(), 0);
	TestTrue(TEXT("final entry intact"), Cache.Load(FIntPoint(9, 9), TEXT("recipe"), 1, Got) && Got == Same);
	TestEqual(TEXT("size counted once"), Cache.TotalBytes(), int64(4000 + 20));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
