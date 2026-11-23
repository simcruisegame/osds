// Copyright OSDS contributors. All Rights Reserved.

#include "World/OsdsWorldCache.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogOsdsWorldCache);

namespace
{
	TAutoConsoleVariable<int32> CVarLimitMiB(
		TEXT("OSDS.WorldCache.LimitMiB"), 2048,
		TEXT("The most disk the World cache (generated Tile layers) may use, in MiB. The 2048 MiB default is an estimate (about 40 MiB per driven km at the current layers, so roughly 50 km before the oldest Tiles go). Least recently driven Tiles go first."),
		ECVF_Default);

	FAutoConsoleCommand GStatsCommand(
		TEXT("OSDS.WorldCache.Stats"),
		TEXT("Log the World cache counters: hits, misses, stores, evicted Tiles and size on disk."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FOsdsWorldCache& Cache = FOsdsWorldCache::Get();
			const FOsdsWorldCache::FStats S = Cache.GetStats();
			UE_LOG(LogOsdsWorldCache, Display, TEXT("World cache: %lld hits, %lld misses, %lld stores, %lld Tiles evicted, %lld damaged deleted, %lld failed stores, %.1f MiB of %.0f MiB"),
				S.Hits, S.Misses, S.Stores, S.EvictedTiles, S.CorruptDeleted, S.StoreFailures, Cache.TotalBytes() / 1048576.0, Cache.LimitBytes() / 1048576.0);
		}));

	constexpr uint32 Magic = 0x4f574331; // "OWC1"
	constexpr int32 FormatVersion = 1;
	const TCHAR* const Generation = TEXT("v1");

	/** The busy-file retry of a move: 9 sleeps of 5 ms, about 45 ms in all. */
	constexpr int32 MoveTries = 10;
	constexpr float MoveSleepSeconds = 0.005f;

	struct FHeader
	{
		uint32 Magic;
		int32 Format;
		int32 GeneratorVersion;
		int32 Size;
		uint32 Crc;
	};
	static_assert(sizeof(FHeader) == 20, "header layout is part of the file format");

	int64 NowTicks() { return FDateTime::UtcNow().GetTicks(); }
}

FOsdsWorldCache::FOsdsWorldCache(const FString& InRoot, int64 InLimitBytes)
	: Root(InRoot), FixedLimit(InLimitBytes)
{
}

FString FOsdsWorldCache::DefaultRoot()
{
	return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("OSDS"), TEXT("world-cache"));
}

FOsdsWorldCache& FOsdsWorldCache::Get()
{
	// Never destroyed (as FOsdsNeuralDetail): no static destructor runs at process exit. The folder scan
	// starts on a worker at once, so the frame path never pays for it.
	static FOsdsWorldCache* Instance = []()
	{
		FOsdsWorldCache* Cache = new FOsdsWorldCache(DefaultRoot());
		Async(EAsyncExecution::ThreadPool, [Cache]() { Cache->WarmIndex(); });
		return Cache;
	}();
	return *Instance;
}

FString FOsdsWorldCache::TileDir(const FIntPoint& Tile) const
{
	return FPaths::Combine(Root, Generation, FString::FromInt(Tile.X), FString::FromInt(Tile.Y));
}

FString FOsdsWorldCache::LayerFile(const FIntPoint& Tile, FName Layer) const
{
	return FPaths::Combine(TileDir(Tile), Layer.ToString() + TEXT(".bin"));
}

int64 FOsdsWorldCache::LimitBytes() const
{
	return FixedLimit >= 0 ? FixedLimit : int64(FMath::Max(CVarLimitMiB.GetValueOnAnyThread(), 0)) * 1024 * 1024;
}

uint64 FOsdsWorldCache::TileSeed(const FIntPoint& Tile16)
{
	// splitmix64 over the packed tile id: fixed forever, so a Tile is the same on every visit.
	uint64 Z = ((uint64(uint32(Tile16.X)) << 32) | uint64(uint32(Tile16.Y))) + 0x9E3779B97F4A7C15ull;
	Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
	Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
	return Z ^ (Z >> 31);
}

void FOsdsWorldCache::WarmIndex()
{
	EnsureIndex();
}

void FOsdsWorldCache::EnsureIndex()
{
	if (bIndexed.load(std::memory_order_acquire)) return;
	// One scan at a time and no index lock during the disk walk: it blocks other workers only, never the frame path.
	FScopeLock Scan(&ScanLock);
	if (bIndexed.load(std::memory_order_acquire)) return;
	IFileManager& Files = IFileManager::Get();
	const FString Base = FPaths::Combine(Root, Generation);
	TMap<FIntPoint, FTileInfo> Found;
	TArray<FString> Xs;
	Files.FindFiles(Xs, *FPaths::Combine(Base, TEXT("*")), false, true);
	for (const FString& XName : Xs)
	{
		TArray<FString> Ys;
		Files.FindFiles(Ys, *FPaths::Combine(Base, XName, TEXT("*")), false, true);
		for (const FString& YName : Ys)
		{
			const FIntPoint Tile(FCString::Atoi(*XName), FCString::Atoi(*YName));
			const FString Dir = TileDir(Tile);
			TArray<FString> Layers;
			Files.FindFiles(Layers, *FPaths::Combine(Dir, TEXT("*.bin")), true, false);
			FTileInfo Info;
			for (const FString& L : Layers)
			{
				const FString File = FPaths::Combine(Dir, L);
				const int64 Size = Files.FileSize(*File);
				if (Size < 0) continue;
				Info.LayerBytes.Add(FName(*FPaths::GetBaseFilename(L)), Size);
				Info.Bytes += Size;
				Info.LastDrivenTicks = FMath::Max(Info.LastDrivenTicks, Files.GetTimeStamp(*File).GetTicks());
			}
			if (Info.LayerBytes.Num() == 0) continue;
			Found.Add(Tile, MoveTemp(Info));
		}
	}
	{
		FScopeLock Guard(&IndexLock);
		for (TPair<FIntPoint, FTileInfo>& P : Found)
		{
			if (Index.Contains(P.Key)) continue;
			Total += P.Value.Bytes;
			Index.Add(P.Key, MoveTemp(P.Value));
		}
	}
	bIndexed.store(true, std::memory_order_release);
}

void FOsdsWorldCache::MarkDriven(const FIntPoint& Tile)
{
	FScopeLock Guard(&IndexLock);
	if (FTileInfo* Info = Index.Find(Tile)) Info->LastDrivenTicks = NowTicks();
}

void FOsdsWorldCache::ForgetLayerLocked(const FIntPoint& Tile, FName Layer)
{
	FTileInfo* Info = Index.Find(Tile);
	if (!Info) return;
	int64 Bytes = 0;
	if (Info->LayerBytes.RemoveAndCopyValue(Layer, Bytes))
	{
		Info->Bytes -= Bytes;
		Total -= Bytes;
	}
	if (Info->LayerBytes.Num() == 0) Index.Remove(Tile);
}

bool FOsdsWorldCache::Load(const FIntPoint& Tile16, FName Layer, int32 GeneratorVersion, TArray<uint8>& Out)
{
	const FString File = LayerFile(Tile16, Layer);
	TArray<uint8> Raw;
	bool bOk = false;
	bool bCorrupt = false;
	if (FFileHelper::LoadFileToArray(Raw, *File, FILEREAD_Silent))
	{
		if (Raw.Num() < int32(sizeof(FHeader)))
		{
			bCorrupt = true;
		}
		else
		{
			FHeader H;
			FMemory::Memcpy(&H, Raw.GetData(), sizeof(H));
			const int32 Payload = Raw.Num() - int32(sizeof(FHeader));
			if (H.Magic != Magic || H.Format != FormatVersion || H.Size != Payload)
			{
				bCorrupt = true;
			}
			else if (H.GeneratorVersion == GeneratorVersion) // another version is a plain miss: the next Store replaces it
			{
				if (H.Crc == FCrc::MemCrc32(Raw.GetData() + sizeof(FHeader), Payload))
				{
					Out.Reset(Payload);
					Out.Append(Raw.GetData() + sizeof(FHeader), Payload);
					bOk = true;
				}
				else
				{
					bCorrupt = true;
				}
			}
		}
	}
	EnsureIndex();
	if (bOk)
	{
		++Hits;
		MarkDriven(Tile16);
		// Last-driven survives a restart: the file's own time is the record. Disk work: no lock is held.
		IFileManager::Get().SetTimeStamp(*File, FDateTime::UtcNow());
		return true;
	}
	++Misses;
	if (bCorrupt)
	{
		// A damaged entry can never hit: take it off the disk and out of the index so it is rebuilt cleanly.
		UE_LOG(LogOsdsWorldCache, Warning, TEXT("World cache: deleting damaged entry %s"), *File);
		IFileManager::Get().Delete(*File, false, true, true);
		{
			FScopeLock Guard(&IndexLock);
			ForgetLayerLocked(Tile16, Layer);
		}
		++CorruptDeleted;
	}
	return false;
}

bool FOsdsWorldCache::Contains(const FIntPoint& Tile16, FName Layer, int32 GeneratorVersion) const
{
	const FString File = LayerFile(Tile16, Layer);
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*File, FILEREAD_Silent));
	if (!Reader || Reader->TotalSize() < int64(sizeof(FHeader))) return false;
	FHeader H;
	Reader->Serialize(&H, sizeof(H));
	return H.Magic == Magic && H.Format == FormatVersion && H.GeneratorVersion == GeneratorVersion && int64(H.Size) + int64(sizeof(FHeader)) == Reader->TotalSize();
}

bool FOsdsWorldCache::WriteFile(const FString& File, const TArray<uint8>& Raw)
{
	IFileManager& Files = IFileManager::Get();
	Files.MakeDirectory(*FPaths::GetPath(File), true);
	// Unique temp name, then a move: a reader (or a second drive's writer) never sees half a file.
	const FString Temp = File + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
	if (!FFileHelper::SaveArrayToFile(Raw, *Temp))
	{
		Files.Delete(*Temp, false, false, true);
		return false;
	}
	// The platform call is silent (IFileManager::Move logs an error per failure). A reader may hold
	// the file for a moment, and another writer may be replacing it: retry briefly (about 45 ms in all),
	// and if the entry is still busy drop this write; the other writer's copy of the same layer stands.
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
	for (int32 Try = 0; Try < MoveTries; ++Try)
	{
		if (Try > 0) FPlatformProcess::Sleep(MoveSleepSeconds);
		Platform.DeleteFile(*File);
		if (Platform.MoveFile(*File, *Temp)) return true;
	}
	Files.Delete(*Temp, false, false, true);
	return false;
}

bool FOsdsWorldCache::Store(const FIntPoint& Tile16, FName Layer, int32 GeneratorVersion, TConstArrayView<uint8> Bytes)
{
	if (Bytes.Num() == 0) return false;
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(int32(sizeof(FHeader)) + Bytes.Num());
	const FHeader H{Magic, FormatVersion, GeneratorVersion, Bytes.Num(), FCrc::MemCrc32(Bytes.GetData(), Bytes.Num())};
	FMemory::Memcpy(Raw.GetData(), &H, sizeof(H));
	FMemory::Memcpy(Raw.GetData() + sizeof(H), Bytes.GetData(), Bytes.Num());

	EnsureIndex();
	const FString File = LayerFile(Tile16, Layer);
	if (!WriteFile(File, Raw))
	{
		// Disk full or the entry stuck busy: free the least recently driven 10 % (at least twice this
		// entry) and try once more.
		const int64 Now = Total.load();
		const int32 Freed = EvictToTarget(Now - FMath::Max<int64>(Now / 10, int64(Raw.Num()) * 2));
		if (!WriteFile(File, Raw))
		{
			UE_LOG(LogOsdsWorldCache, Warning, TEXT("World cache: could not store %s (%d bytes; %d Tiles evicted, still failed)"), *File, Raw.Num(), Freed);
			++StoreFailures;
			return false;
		}
	}
	{
		FScopeLock Guard(&IndexLock);
		FTileInfo& Info = Index.FindOrAdd(Tile16);
		const int64 Old = Info.LayerBytes.FindRef(Layer);
		Info.LayerBytes.Add(Layer, Raw.Num());
		Info.Bytes += Raw.Num() - Old;
		Total += Raw.Num() - Old;
		Info.LastDrivenTicks = NowTicks();
	}
	++Stores;
	EvictToTarget(LimitBytes());
	return true;
}

void FOsdsWorldCache::SetProtectedTiles(const TArray<FIntPoint>& Tiles)
{
	// Frame path: build the new set first, then swap it in under a lock nothing holds for more than a swap.
	TSet<FIntPoint> Fresh;
	Fresh.Append(Tiles);
	FScopeLock Guard(&ProtectedLock);
	Swap(Protected, Fresh);
}

int32 FOsdsWorldCache::EvictToTarget(int64 Target)
{
	if (Total.load() <= Target) return 0;
	TSet<FIntPoint> Keep;
	{
		FScopeLock Guard(&ProtectedLock);
		Keep = Protected;
	}
	// Choose the victims and take them out of the index under the lock (memory only) ...
	TArray<TPair<FIntPoint, FTileInfo>> Taken;
	{
		FScopeLock Guard(&IndexLock);
		TArray<TPair<int64, FIntPoint>> Order;
		for (const TPair<FIntPoint, FTileInfo>& P : Index)
		{
			if (!Keep.Contains(P.Key)) Order.Emplace(P.Value.LastDrivenTicks, P.Key);
		}
		Order.Sort([](const TPair<int64, FIntPoint>& A, const TPair<int64, FIntPoint>& B) { return A.Key < B.Key; });
		for (const TPair<int64, FIntPoint>& P : Order)
		{
			if (Total.load() <= Target) break;
			FTileInfo Info;
			Index.RemoveAndCopyValue(P.Value, Info);
			Total -= Info.Bytes;
			Taken.Emplace(P.Value, MoveTemp(Info));
		}
	}
	// ... then delete their folders with no lock held.
	IFileManager& Files = IFileManager::Get();
	int32 Removed = 0;
	for (TPair<FIntPoint, FTileInfo>& P : Taken)
	{
		const FString Dir = TileDir(P.Key);
		Files.DeleteDirectory(*Dir, false, true);
		if (Files.DirectoryExists(*Dir))
		{
			// In use by another process: keep it in the index for a later trim.
			FScopeLock Guard(&IndexLock);
			if (!Index.Contains(P.Key))
			{
				Total += P.Value.Bytes;
				Index.Add(P.Key, MoveTemp(P.Value));
			}
			continue;
		}
		++Removed;
		++EvictedTiles;
	}
	return Removed;
}

int32 FOsdsWorldCache::Evict()
{
	EnsureIndex();
	return EvictToTarget(LimitBytes());
}

int64 FOsdsWorldCache::TotalBytes() const
{
	return Total.load();
}

FOsdsWorldCache::FStats FOsdsWorldCache::GetStats() const
{
	FStats S;
	S.Hits = Hits.load();
	S.Misses = Misses.load();
	S.Stores = Stores.load();
	S.EvictedTiles = EvictedTiles.load();
	S.CorruptDeleted = CorruptDeleted.load();
	S.StoreFailures = StoreFailures.load();
	return S;
}
