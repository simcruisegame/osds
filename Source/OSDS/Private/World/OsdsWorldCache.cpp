// Copyright OSDS contributors. All Rights Reserved.

#include "World/OsdsWorldCache.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace
{
	TAutoConsoleVariable<int32> CVarLimitMiB(
		TEXT("OSDS.WorldCache.LimitMiB"), 2048,
		TEXT("The most disk the World cache (generated Tile layers) may use, in MiB. Least recently driven Tiles go first."),
		ECVF_Default);

	FAutoConsoleCommand GStatsCommand(
		TEXT("OSDS.WorldCache.Stats"),
		TEXT("Log the World cache counters: hits, misses, stores, evicted Tiles and size on disk."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FOsdsWorldCache& Cache = FOsdsWorldCache::Get();
			const FOsdsWorldCache::FStats S = Cache.GetStats();
			UE_LOG(LogTemp, Display, TEXT("World cache: %lld hits, %lld misses, %lld stores, %lld Tiles evicted, %.1f MiB of %.0f MiB"),
				S.Hits, S.Misses, S.Stores, S.EvictedTiles, Cache.TotalBytes() / 1048576.0, Cache.LimitBytes() / 1048576.0);
		}));

	constexpr uint32 Magic = 0x4f574331; // "OWC1"
	constexpr int32 FormatVersion = 1;
	const TCHAR* const Generation = TEXT("v1");

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
	// Never destroyed (as FOsdsNeuralDetail): no static destructor runs at process exit.
	static FOsdsWorldCache* Instance = new FOsdsWorldCache(DefaultRoot());
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

void FOsdsWorldCache::EnsureIndex()
{
	if (bIndexed) return;
	bIndexed = true;
	IFileManager& Files = IFileManager::Get();
	const FString Base = FPaths::Combine(Root, Generation);
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
			Total += Info.Bytes;
			Index.Add(Tile, MoveTemp(Info));
		}
	}
}

void FOsdsWorldCache::MarkDriven(const FIntPoint& Tile)
{
	if (FTileInfo* Info = Index.Find(Tile)) Info->LastDrivenTicks = NowTicks();
}

bool FOsdsWorldCache::Load(const FIntPoint& Tile16, FName Layer, int32 GeneratorVersion, TArray<uint8>& Out)
{
	const FString File = LayerFile(Tile16, Layer);
	TArray<uint8> Raw;
	bool bOk = false;
	if (FFileHelper::LoadFileToArray(Raw, *File, FILEREAD_Silent) && Raw.Num() >= int32(sizeof(FHeader)))
	{
		FHeader H;
		FMemory::Memcpy(&H, Raw.GetData(), sizeof(H));
		const int32 Payload = Raw.Num() - int32(sizeof(FHeader));
		if (H.Magic == Magic && H.Format == FormatVersion && H.GeneratorVersion == GeneratorVersion && H.Size == Payload
			&& H.Crc == FCrc::MemCrc32(Raw.GetData() + sizeof(FHeader), Payload))
		{
			Out.Reset(Payload);
			Out.Append(Raw.GetData() + sizeof(FHeader), Payload);
			bOk = true;
		}
	}
	FScopeLock Guard(&Lock);
	EnsureIndex();
	if (bOk)
	{
		++Stats.Hits;
		MarkDriven(Tile16);
		// Last-driven survives a restart: the file's own time is the record.
		IFileManager::Get().SetTimeStamp(*File, FDateTime::UtcNow());
	}
	else
	{
		++Stats.Misses;
	}
	return bOk;
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

bool FOsdsWorldCache::Store(const FIntPoint& Tile16, FName Layer, int32 GeneratorVersion, TConstArrayView<uint8> Bytes)
{
	if (Bytes.Num() == 0) return false;
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(int32(sizeof(FHeader)) + Bytes.Num());
	const FHeader H{Magic, FormatVersion, GeneratorVersion, Bytes.Num(), FCrc::MemCrc32(Bytes.GetData(), Bytes.Num())};
	FMemory::Memcpy(Raw.GetData(), &H, sizeof(H));
	FMemory::Memcpy(Raw.GetData() + sizeof(H), Bytes.GetData(), Bytes.Num());

	IFileManager& Files = IFileManager::Get();
	const FString File = LayerFile(Tile16, Layer);
	Files.MakeDirectory(*FPaths::GetPath(File), true);
	// Unique temp name, then a move: a reader (or a second drive's writer) never sees half a file.
	const FString Temp = File + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
	if (!FFileHelper::SaveArrayToFile(Raw, *Temp))
	{
		Files.Delete(*Temp);
		return false;
	}
	// The platform call is silent (IFileManager::Move logs an error per failure). A reader may hold
	// the file for a moment, and another writer may be replacing it: retry briefly, and if the
	// entry is still busy drop this write; the other writer's copy of the same layer stands.
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
	bool bMoved = false;
	for (int32 Try = 0; Try < 20 && !bMoved; ++Try)
	{
		if (Try > 0) FPlatformProcess::Sleep(0.005f * Try);
		Platform.DeleteFile(*File);
		bMoved = Platform.MoveFile(*File, *Temp);
	}
	if (!bMoved)
	{
		Files.Delete(*Temp, false, false, true);
		return false;
	}
	FScopeLock Guard(&Lock);
	EnsureIndex();
	FTileInfo& Info = Index.FindOrAdd(Tile16);
	const int64 Old = Info.LayerBytes.FindRef(Layer);
	Info.LayerBytes.Add(Layer, Raw.Num());
	Info.Bytes += Raw.Num() - Old;
	Total += Raw.Num() - Old;
	Info.LastDrivenTicks = NowTicks();
	++Stats.Stores;
	EvictLocked();
	return true;
}

void FOsdsWorldCache::SetProtectedTiles(const TArray<FIntPoint>& Tiles)
{
	FScopeLock Guard(&Lock);
	Protected.Empty();
	Protected.Append(Tiles);
}

int32 FOsdsWorldCache::EvictLocked()
{
	const int64 Limit = LimitBytes();
	if (Total <= Limit) return 0;
	TArray<TPair<int64, FIntPoint>> Order;
	for (const TPair<FIntPoint, FTileInfo>& P : Index)
	{
		if (!Protected.Contains(P.Key)) Order.Emplace(P.Value.LastDrivenTicks, P.Key);
	}
	Order.Sort([](const TPair<int64, FIntPoint>& A, const TPair<int64, FIntPoint>& B) { return A.Key < B.Key; });
	IFileManager& Files = IFileManager::Get();
	int32 Removed = 0;
	for (const TPair<int64, FIntPoint>& P : Order)
	{
		if (Total <= Limit) break;
		const FString Dir = TileDir(P.Value);
		Files.DeleteDirectory(*Dir, false, true);
		if (Files.DirectoryExists(*Dir)) continue; // in use by another process: keep it for a later trim
		Total -= Index.FindRef(P.Value).Bytes;
		Index.Remove(P.Value);
		++Removed;
		++Stats.EvictedTiles;
	}
	return Removed;
}

int32 FOsdsWorldCache::Evict()
{
	FScopeLock Guard(&Lock);
	EnsureIndex();
	return EvictLocked();
}

int64 FOsdsWorldCache::TotalBytes()
{
	FScopeLock Guard(&Lock);
	EnsureIndex();
	return Total;
}

FOsdsWorldCache::FStats FOsdsWorldCache::GetStats() const
{
	FScopeLock Guard(&Lock);
	return Stats;
}
