// Copyright OSDS contributors. All Rights Reserved.

#include "World/OsdsWorldStreamer.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/CollisionProfile.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"

#include "Live/OsdsAerialEye.h"
#include "Live/OsdsNeuralDetail.h"
#include "World/OsdsTraffic.h"
#include "World/OsdsStartPick.h"
#include "Misc/App.h"

#include "Async/Async.h"
#include "Atlas/OsdsAtlasClient.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SpotLightComponent.h"
#include "Environment/OsdsEnvironment.h"
#include "Environment/OsdsEnvironmentSubsystem.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "World/OsdsTileBuilder.h"
#include "World/OsdsTileData.h"
#include "World/OsdsWorldResources.h"
#include "World/OsdsWorldTile.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsWorld, Log, All);

static TAutoConsoleVariable<int32> CVarBuildingFacts(
	TEXT("osds.World.BuildingFacts"), 1,
	TEXT("1 (default): each tile's buildings take measured heights, use, storeys and mapped finishes from the helper (Overture, USACE NSI); 0: the tile's rows only."));

AOsdsWorldStreamer::AOsdsWorldStreamer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AOsdsWorldStreamer::BeginPlay()
{
	Super::BeginPlay();
	{
		FString Fail;
		// The whole list up to the next space: Windows drops the quotes, and the default parse stops at the first comma.
		if (FParse::Value(FCommandLine::Get(), TEXT("OsdsFailTiles="), Fail, false))
		{
			TArray<FString> Parts;
			Fail.ParseIntoArray(Parts, TEXT(","));
			for (const FString& P : Parts)
			{
				FString X, Y;
				if (P.TrimQuotes().Split(TEXT("_"), &X, &Y))
				{
					ForcedFailures.Add(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y)));
					UE_LOG(LogOsdsWorld, Display, TEXT("Forced tile failure: %s_%s (-OsdsFailTiles)"), *X, *Y);
				}
			}
			UE_LOG(LogOsdsWorld, Display, TEXT("Forced tile failures: %d (-OsdsFailTiles)"), ForcedFailures.Num());
		}
	}
	Resources = NewObject<UOsdsWorldResources>(this);
	Resources->Initialise();
	FOsdsAtlasConfig Config = FOsdsAtlasConfig::ResolveDefault();
	Config.MaxConcurrent = FMath::Max(1, MaxConcurrentBuilds);
	Atlas = MakeShared<FOsdsAtlasClient, ESPMode::ThreadSafe>(Config);
	Inbox = MakeShared<FInbox, ESPMode::ThreadSafe>();
	// Moving traffic: scenery and obstacles, so drawn runs only (headless checks keep the roads empty).
	// Out of the game for now: no traffic actor, no traffic cars, no cost per frame.
	if (AOsdsTraffic::IsEnabled() && FApp::CanEverRender() && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Traffic = GetWorld()->SpawnActor<AOsdsTraffic>(Params);
		if (Traffic) Traffic->Streamer = this;
	}
	UE_LOG(LogOsdsWorld, Display, TEXT("World streamer ready; helper: %s; cache: %s"),
		Config.IsConfigured() ? *Config.Executable : TEXT("(none: bundled tiles only)"), *Config.CacheDir);
}

void AOsdsWorldStreamer::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Inbox.IsValid())
	{
		Inbox->bClosed = true;
	}
	if (Atlas.IsValid())
	{
		Atlas->CancelAll();
	}
	Super::EndPlay(Reason);
}

void AOsdsWorldStreamer::StartAt(double LonDeg, double LatDeg, const FString& InTitle)
{
	for (auto& Pair : Tiles)
	{
		if (Pair.Value) Pair.Value->Destroy();
	}
	Tiles.Reset();
	for (auto& Pair : FallbackGround) if (Pair.Value) Pair.Value->DestroyComponent();
	FallbackGround.Reset();
	LaunchedAt.Reset();
	if (Atlas.IsValid()) Atlas->CancelAll();
	Frame = FOsdsWorldFrame::ForPlace(LonDeg, LatDeg);
	Title = InTitle;
	Manager.RingRadius = RingRadius;
	Manager.MaxInFlight = FMath::Max(1, MaxConcurrentBuilds);
	if (Atlas.IsValid()) Atlas->SetMaxConcurrent(Manager.MaxInFlight);
	Manager.Reset();
	TilesFailed = 0;
	// Cars of the last place drive on its roads: they go with it.
	if (Traffic) Traffic->ClearCars();
	FocusCm = Frame.LonLatToWorldCm(LonDeg, LatDeg);
	FocusVelocity = FVector::ZeroVector;
	bHasFocus = true;
	bStarted = true;
	UE_LOG(LogOsdsWorld, Display, TEXT("Drive at %s (%.5f, %.5f): origin tile %s, tile %.2f m, generation %u"),
		*Title, LatDeg, LonDeg, *OsdsTileName(Frame.OriginTile), Frame.TileSizeM, Manager.GetGeneration());
}

void AOsdsWorldStreamer::SetStartBoost(bool bOn)
{
	// The road faces each build keeps for the start pick (2-3 MB a tile) are not needed once the
	// car stands on the road: freed now, and on every tile installed until the next start.
	bStartPending = bOn;
	if (!bOn)
	{
		for (const TPair<FIntPoint, TObjectPtr<AOsdsWorldTile>>& Pair : Tiles) if (Pair.Value) Pair.Value->DropRoadTops();
	}
	const int32 Builds = bOn ? FMath::Max(MaxConcurrentBuilds, StartConcurrentBuilds) : FMath::Max(1, MaxConcurrentBuilds);
	if (Manager.MaxInFlight == Builds) return;
	Manager.MaxInFlight = Builds;
	if (Atlas.IsValid()) Atlas->SetMaxConcurrent(Builds);
}

void AOsdsWorldStreamer::SetFocus(const FVector& WorldCm, const FVector& VelocityCmPerSecond)
{
	FocusCm = WorldCm;
	FocusVelocity = VelocityCmPerSecond;
	bHasFocus = true;
}

FVector AOsdsWorldStreamer::CurrentFocus(FVector& OutVelocity) const
{
	OutVelocity = FocusVelocity;
	if (bHasFocus)
	{
		return FocusCm;
	}
	if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		return Camera->GetCameraLocation();
	}
	return FVector::ZeroVector;
}

AOsdsWorldTile* AOsdsWorldStreamer::GetTile(const FIntPoint& Tile) const
{
	const TObjectPtr<AOsdsWorldTile>* Found = Tiles.Find(Tile);
	return Found ? Found->Get() : nullptr;
}

bool AOsdsWorldStreamer::IsBusy() const
{
	return Manager.NumInFlight() > 0;
}

bool AOsdsWorldStreamer::AreTilesReadyAround(const FVector& WorldCm, int32 Radius) const
{
	if (!Frame.IsValid()) return false;
	const FIntPoint Centre = Frame.TileAtWorldCm(WorldCm);
	for (int32 dy = -Radius; dy <= Radius; ++dy)
	for (int32 dx = -Radius; dx <= Radius; ++dx)
	{
		const AOsdsWorldTile* Tile = GetTile(Centre + FIntPoint(dx, dy));
		if (!Tile || !Tile->IsCollisionReady()) return false;
	}
	return true;
}

bool AOsdsWorldStreamer::SurfaceHeightAt(const FVector& WorldCm, double& OutZCm) const
{
	if (!Frame.IsValid()) return false;
	const FIntPoint TileId = Frame.TileAtWorldCm(WorldCm);
	const AOsdsWorldTile* Tile = GetTile(TileId);
	if (!Tile) return false;
	const FVector Local = (WorldCm - Tile->GetActorLocation()) / 100.0;
	OutZCm = Tile->GetActorLocation().Z + Tile->SurfaceHeightAt(FVector2d(Local.X, Local.Y)) * 100.0;
	return true;
}

bool AOsdsWorldStreamer::FindRoadPose(const FString& RoadName, const FVector& NearWorldCm, double SearchRadiusM, FVector& OutLocationCm, FRotator& OutRotation) const
{
	return FindRoadPoseFacing(RoadName, NearWorldCm, SearchRadiusM, TNumericLimits<double>::Max(), OutLocationCm, OutRotation);
}

TArray<FOsdsStartPose> AOsdsWorldStreamer::FindStartCandidates(const FString& RoadName, const FVector& NearWorldCm, double SearchRadiusM, double PreferredYawDeg, FString* OutWhyNone) const
{
	TArray<FOsdsStartTile> Built;
	for (const auto& Pair : Tiles)
	{
		const AOsdsWorldTile* Tile = Pair.Value;
		if (Tile && Tile->GetBuild()) Built.Add({ Tile->GetActorLocation(), Tile->GetBuild() });
	}
	return OsdsStartPick::Candidates(Built, RoadName, NearWorldCm, SearchRadiusM, PreferredYawDeg, 24, OutWhyNone);
}

bool AOsdsWorldStreamer::FindRoadPoseFacing(const FString& RoadName, const FVector& NearWorldCm, double SearchRadiusM, double PreferredYawDeg, FVector& OutLocationCm, FRotator& OutRotation) const
{
	// The start rules (OsdsStartPick): mid-block, inside the tile, straight, the footprint on
	// the carriageway, a road going on ahead. The old nearest-station rule below only where none
	// passes (a place with no such road near).
	const TArray<FOsdsStartPose> Picks = FindStartCandidates(RoadName, NearWorldCm, SearchRadiusM, PreferredYawDeg);
	if (Picks.Num() > 0)
	{
		OutLocationCm = Picks[0].LocationCm;
		OutRotation = Picks[0].Rotation;
		return true;
	}
	const bool bPreferred = PreferredYawDeg < 1e9;
	const FVector2d Wanted = bPreferred
		? FVector2d(FMath::Cos(FMath::DegreesToRadians(PreferredYawDeg)), FMath::Sin(FMath::DegreesToRadians(PreferredYawDeg)))
		: FVector2d::ZeroVector;
	double Best = SearchRadiusM * 100.0;
	bool bFound = false;
	for (const auto& Pair : Tiles)
	{
		const AOsdsWorldTile* Tile = Pair.Value;
		if (!Tile || !Tile->GetBuild()) continue;
		const FVector Origin = Tile->GetActorLocation();
		for (const FOsdsRibbon& Ribbon : Tile->GetBuild()->Roads.Ribbons)
		{
			// No name: any public road (service ways are driveways and parking aisles).
			if (RoadName.IsEmpty() ? Ribbon.Class == TEXT("service") : !Ribbon.Name.Equals(RoadName, ESearchCase::IgnoreCase)) continue;
			// Not in or next to a junction: a start there faces the crossing road (and the cars
			// parked along its far side).
			TArray<uint8> NearJunction;
			NearJunction.Init(0, Ribbon.St.Num());
			const TArray<uint8> Cuts = OsdsStartPick::JunctionCuts(Ribbon);
			for (int32 j = 0; j < Ribbon.St.Num(); ++j)
			{
				if (!Cuts[j]) continue;
				for (int32 k = 0; k < Ribbon.St.Num(); ++k)
				{
					if (FMath::Abs(Ribbon.St[k].S - Ribbon.St[j].S) < 15.0) NearJunction[k] = 1;
				}
			}
			for (int32 i = 1; i + 1 < Ribbon.St.Num(); ++i)
			{
				const FOsdsRibbonStation& St = Ribbon.St[i];
				if (NearJunction[i]) continue;
				const FVector C = Origin + FVector(St.C.X * 100.0, St.C.Y * 100.0, 0.0);
				const double D = FVector::Dist2D(C, NearWorldCm);
				if (D >= Best) continue;
				// Facing the way traffic goes; on a two-way street, the way with more room before
				// the next junction or the street's end (at least 25 m).
				int32 Way = St.Travel < 0 ? -1 : 1;
				bool bEnd = false;
				double Room = FMath::Abs(OsdsTrafficModel::NextDecisionS(Ribbon, Way, St.S, -1e9, bEnd) - St.S);
				if (St.Travel == 0)
				{
					const double Back = FMath::Abs(OsdsTrafficModel::NextDecisionS(Ribbon, -Way, St.S, -1e9, bEnd) - St.S);
					// A requested heading picks the way when that way has room; else the way with more room.
					bool bFlip = Back > Room;
					if (bPreferred)
					{
						const bool bWantBack = FVector2d::DotProduct(St.T * static_cast<double>(Way), Wanted) < 0.0;
						bFlip = bWantBack ? (Back >= 25.0 || Back > Room) : (Room < 25.0 && Back > Room);
					}
					if (bFlip) { Way = -Way; Room = Back; }
				}
				if (Room < 25.0) continue;
				const FVector2d Dir = St.T * static_cast<double>(Way);
				// The lane the traffic drives in this direction, between the lines the markings draw.
				const double L = OsdsTrafficModel::LaneOffsetM(Ribbon, i, Way);
				const FVector2d P = St.C + St.R * L;
				const double H = Ribbon.CarriagewayHeight(i, L);
				OutLocationCm = Origin + FVector(P.X * 100.0, P.Y * 100.0, H * 100.0);
				OutRotation = FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.0);
				Best = D;
				bFound = true;
			}
		}
	}
	return bFound;
}

void AOsdsWorldStreamer::Launch(const FOsdsTileJobTicket& Ticket)
{
	LaunchedAt.Add(Ticket.Serial, FPlatformTime::Seconds());
	if (ForcedFailures.Contains(Ticket.Tile) && Inbox.IsValid())
	{
		TSharedPtr<FResult, ESPMode::ThreadSafe> Result = MakeShared<FResult, ESPMode::ThreadSafe>();
		Result->Ticket = Ticket;
		Result->Error = TEXT("forced failure (-OsdsFailTiles)");
		Result->FinishedAt = FPlatformTime::Seconds();
		Inbox->Queue.Enqueue(Result);
		return;
	}
	TSharedPtr<FInbox, ESPMode::ThreadSafe> Box = Inbox;
	TSharedPtr<FOsdsAtlasClient, ESPMode::ThreadSafe> Client = bUseHelper ? Atlas : nullptr;
	const double TileSizeM = Frame.TileSizeM;
	TSet<FString> KnownAerials;
	for (const auto& Pair : Resources->Aerials) KnownAerials.Add(Pair.Key);

	// Seam contracts of the resident neighbours: this tile meets the roads they already built.
	TArray<FOsdsSeamEnd> Contracts = OsdsTileBuilder::NeighbourContracts(Ticket.Tile, TileSizeM, [this](const FIntPoint& T) -> const FOsdsTileBuild*
	{
		const AOsdsWorldTile* Tile = GetTile(T);
		return Tile ? Tile->GetBuild() : nullptr;
	});

	auto BuildFrom = [Box, Ticket, TileSizeM, KnownAerials, Client, Contracts](FOsdsTileData&& Data)
	{
		TSharedPtr<FResult, ESPMode::ThreadSafe> Result = MakeShared<FResult, ESPMode::ThreadSafe>();
		Result->Ticket = Ticket;
		if (Box->bClosed) return;
		// The photograph, when it is on disk already (bundled, or fetched for a neighbour of the same
		// zoom-14 cell): the build reads roof colours from it; otherwise the rules colour the roofs.
		TSharedPtr<const FOsdsAerialImage, ESPMode::ThreadSafe> Photo;
		if (!Data.AerialKey.IsEmpty())
		{
			FString Known = OsdsTileData::BundledAerialPath(Data.AerialKey);
			if (Known.IsEmpty() && Client.IsValid())
			{
				const FString Cached = FPaths::Combine(Client->GetConfig().CacheDir, Data.AerialKey);
				if (IFileManager::Get().FileExists(*Cached)) Known = Cached;
			}
			Photo = OsdsAerialCache::Load(Data.AerialKey, Known);
		}
		// The buildings' measured heights, use, storeys and mapped finishes (helper `buildings`: Overture,
		// the USACE National Structure Inventory). The helper caches the reply a week per tile; a first
		// visit waits at most a few seconds (on this worker, never the game thread), then builds without.
		if (Client.IsValid() && Client->GetConfig().IsConfigured() && Data.Buildings.Num() > 0 && CVarBuildingFacts.GetValueOnAnyThread() != 0)
		{
			FOsdsAtlasRequest Request;
			Request.Command = TEXT("buildings");
			Request.Args = { FString::FromInt(Data.Id.X), FString::FromInt(Data.Id.Y) };
			Request.TimeoutSeconds = 12.0;
			const FOsdsAtlasResult Facts = FOsdsAtlasClient::RunBlocking(Client->GetConfig(), Request, &Box->bClosed);
			if (Facts.IsOk() && Facts.Json.IsValid() && Facts.Json->Type == EJson::Object)
			{
				const FOsdsBuildingFactsCount Count = OsdsTileData::ApplyBuildingFacts(Data, Facts.Json->AsObject());
				UE_LOG(LogOsdsWorld, Log, TEXT("Tile %s buildings: %d measured heights, %d uses, %d mapped finishes (of %d)"),
					*OsdsTileName(Data.Id), Count.Heights, Count.Uses, Count.Finishes, Data.Buildings.Num());
			}
		}
		Result->Build = OsdsTileBuilder::Build(Data, TileSizeM, Contracts, Photo.Get());
		Result->AerialKey = Data.AerialKey;
		if (!Data.AerialKey.IsEmpty() && !KnownAerials.Contains(Data.AerialKey))
		{
			FString Path = OsdsTileData::BundledAerialPath(Data.AerialKey);
			if (Path.IsEmpty() && Client.IsValid())
			{
				const FString Cached = FPaths::Combine(Client->GetConfig().CacheDir, Data.AerialKey);
				// The tile command never fetches the photograph; the aerial command does (Drive.gd photo job).
				// Tiles of one zoom-14 photograph often build together: one worker fetches, the others wait for it.
				bool bClaimed = false;
				while (!Box->bClosed && !IFileManager::Get().FileExists(*Cached))
				{
					{
						FScopeLock Guard(&Box->AerialLock);
						if (!Box->AerialsFetching.Contains(Data.AerialKey))
						{
							Box->AerialsFetching.Add(Data.AerialKey);
							bClaimed = true;
						}
					}
					if (bClaimed) break;
					FPlatformProcess::Sleep(0.05f);
				}
				if (bClaimed)
				{
					if (!IFileManager::Get().FileExists(*Cached))
					{
						const FIntPoint A = OsdsGeo::AerialTileOf(Data.Id);
						FOsdsAtlasRequest Request;
						Request.Command = TEXT("aerial");
						Request.Args = { FString::FromInt(A.X), FString::FromInt(A.Y) };
						Request.TimeoutSeconds = 60.0;
						FOsdsAtlasClient::RunBlocking(Client->GetConfig(), Request, &Box->bClosed);
					}
					FScopeLock Guard(&Box->AerialLock);
					Box->AerialsFetching.Remove(Data.AerialKey);
				}
				if (IFileManager::Get().FileExists(*Cached)) Path = Cached;
			}
			if (Photo.IsValid() && Photo->Key == Data.AerialKey)
			{
				// Decoded before the build already (roof colours): copied, not decoded again.
				Result->AerialPixels = Photo->Pixels;
				Result->AerialWidth = Photo->Width;
				Result->AerialHeight = Photo->Height;
			}
			else
			{
				UOsdsWorldResources::DecodeImageFile(Path, Result->AerialPixels, Result->AerialWidth, Result->AerialHeight);
				OsdsAerialCache::Put(Data.AerialKey, Result->AerialWidth, Result->AerialHeight, Result->AerialPixels); // before the AI sharpening
			}
			// The local AI detail engine (Live/OsdsNeuralDetail): its cached sharper photograph, or queued for it.
			FOsdsNeuralDetail::Get().EnhanceAerial(Path, Result->AerialPixels, Result->AerialWidth, Result->AerialHeight);
			UOsdsWorldResources::BuildMipChain(Result->AerialPixels, Result->AerialWidth, Result->AerialHeight, true, Result->AerialMips);
		}
		// The scan engine's aerial eye reads this tile's part of the photograph on its own CPU thread.
		if (!Data.AerialKey.IsEmpty() && FOsdsAerialEye::Get().IsReady())
		{
			FString EyePath = OsdsTileData::BundledAerialPath(Data.AerialKey);
			if (EyePath.IsEmpty() && Client.IsValid()) EyePath = FPaths::Combine(Client->GetConfig().CacheDir, Data.AerialKey);
			// With the helper the eye also fetches each tile's zoom-16 photograph (clean photo albedo).
			if (Client.IsValid() && Client->GetConfig().IsConfigured()) FOsdsAerialEye::SetAtlas(Client->GetConfig());
			if (IFileManager::Get().FileExists(*EyePath)) FOsdsAerialEye::Get().Request(Data.Id, EyePath);
		}
		Result->FinishedAt = FPlatformTime::Seconds();
		if (!Box->bClosed) Box->Queue.Enqueue(Result);
	};
	auto Fail = [Box, Ticket](const FString& Error)
	{
		TSharedPtr<FResult, ESPMode::ThreadSafe> Result = MakeShared<FResult, ESPMode::ThreadSafe>();
		Result->Ticket = Ticket;
		Result->Error = Error;
		Result->FinishedAt = FPlatformTime::Seconds();
		if (!Box->bClosed) Box->Queue.Enqueue(Result);
	};

	if (OsdsTileData::HasBundledTile(Ticket.Tile))
	{
		const FString Path = OsdsTileData::BundledTilePath(Ticket.Tile);
		Async(EAsyncExecution::ThreadPool, [Path, BuildFrom, Fail]()
		{
			FOsdsTileData Data;
			FString Error;
			if (!OsdsTileData::LoadFile(Path, Data, Error)) { Fail(Error); return; }
			BuildFrom(MoveTemp(Data));
		});
		return;
	}
	if (!Client.IsValid() || !Client->GetConfig().IsConfigured())
	{
		Fail(TEXT("no bundled tile and no atlas helper"));
		return;
	}
	TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe> Job = Client->RequestTile(Ticket.Tile);
	// The continuation runs where the promise is fulfilled: the helper's worker thread, under
	// the client's lock for a cancel, or right here on the game thread if the job already
	// finished. Parsing and the multi-second build therefore go to the thread pool.
	Job->Future.Next([BuildFrom, Fail](FOsdsAtlasResult Result)
	{
		if (!Result.IsOk() || !Result.Json.IsValid() || Result.Json->Type != EJson::Object)
		{
			Fail(Result.Describe());
			return;
		}
		Async(EAsyncExecution::ThreadPool, [BuildFrom, Fail, Json = MoveTemp(Result.Json)]()
		{
			FOsdsTileData Data;
			FString Error;
			if (!OsdsTileData::ParseObject(Json->AsObject(), Data, Error)) { Fail(Error); return; }
			BuildFrom(MoveTemp(Data));
		});
	});
}

void AOsdsWorldStreamer::RemoveTile(const FIntPoint& Tile)
{
	TObjectPtr<AOsdsWorldTile> Actor;
	if (Tiles.RemoveAndCopyValue(Tile, Actor) && Actor)
	{
		Actor->Destroy();
	}
}

void AOsdsWorldStreamer::Rebase(const FIntPoint& NewOrigin)
{
	const FVector Offset = Frame.RebaseTo(NewOrigin);
	for (auto& Pair : Tiles)
	{
		if (Pair.Value)
		{
			Pair.Value->SetActorLocation(Frame.TileOriginCm(Pair.Key), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	for (auto& Pair : FallbackGround)
	{
		if (Pair.Value) Pair.Value->SetWorldLocation(Pair.Value->GetComponentLocation() + Offset, false, nullptr, ETeleportType::TeleportPhysics);
	}
	FocusCm += Offset;
	UE_LOG(LogOsdsWorld, Display, TEXT("Floating origin moved to tile %s (offset %.0f, %.0f m)"), *OsdsTileName(NewOrigin), Offset.X / 100.0, Offset.Y / 100.0);
	OnRebased.Broadcast(Offset);
}

void AOsdsWorldStreamer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickStreaming(DeltaSeconds);
	UpdateNightLighting(DeltaSeconds);
	UpdateHorizonGround(DeltaSeconds);
	// Parked cars, three a frame across the tiles.
	int32 Budget = 3;
	for (const TPair<FIntPoint, TObjectPtr<AOsdsWorldTile>>& Pair : Tiles)
	{
		if (Budget <= 0) break;
		if (Pair.Value) Budget -= Pair.Value->PlaceParkedCars(Budget);
	}
}

void AOsdsWorldStreamer::UpdateHorizonGround(float DeltaSeconds)
{
	static IConsoleVariable* Enabled = IConsoleManager::Get().RegisterConsoleVariable(TEXT("osds.World.HorizonGround"), 1,
		TEXT("1 (default): a far ground below the built tiles, so unbuilt distance shows hazy land, not the planet's dark surface; 0: off."), ECVF_Default);
	if (!HorizonGround)
	{
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Plane || !Base) return;
		HorizonGround = NewObject<UStaticMeshComponent>(this, TEXT("HorizonGround"));
		HorizonGround->SetStaticMesh(Plane);
		HorizonGround->SetMobility(EComponentMobility::Movable);
		HorizonGround->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HorizonGround->SetCastShadow(false);
		HorizonGround->bAffectDistanceFieldLighting = false;
		HorizonGround->SetupAttachment(RootComponent);
		// Distant land and sea both read as a dim blue-grey through 10-40 km of air; the
		// atmosphere's aerial perspective does the rest.
		UMaterialInstanceDynamic* Look = UMaterialInstanceDynamic::Create(Base, this);
		Look->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.075f, 0.085f, 0.08f));
		HorizonGround->SetMaterial(0, Look);
		// The engine plane is 1 m square: 80 km across reaches past the horizon a driver sees.
		HorizonGround->SetWorldScale3D(FVector(800.0, 800.0, 1.0));
		HorizonGround->RegisterComponent();
	}
	HorizonGround->SetVisibility(Enabled->GetInt() != 0);
	FVector Velocity;
	const FVector Focus = CurrentFocus(Velocity);
	HorizonGroundClock -= DeltaSeconds;
	if (HorizonGroundClock <= 0.f)
	{
		// Just under the lowest ground of the built tiles, so it never shows through them.
		HorizonGroundClock = 2.f;
		double Lowest = TNumericLimits<double>::Max();
		for (const TPair<FIntPoint, TObjectPtr<AOsdsWorldTile>>& Pair : Tiles)
		{
			if (!Pair.Value) continue;
			const FBox Box = Pair.Value->GetComponentsBoundingBox(false);
			if (Box.IsValid) Lowest = FMath::Min(Lowest, Box.Min.Z);
		}
		HorizonGroundZ = Lowest < TNumericLimits<double>::Max() ? Lowest - 300.0 : Focus.Z - 5000.0;
	}
	HorizonGround->SetWorldLocation(FVector(Focus.X, Focus.Y, HorizonGroundZ));
}

void AOsdsWorldStreamer::UpdateNightLighting(float DeltaSeconds)
{
	NightLightingClock -= DeltaSeconds;
	if (NightLightingClock > 0.f)
	{
		return;
	}
	NightLightingClock = 0.25f;

	float SunElevationDeg = 45.f;
	if (UOsdsEnvironmentSubsystem* Subsystem = UOsdsEnvironmentSubsystem::Get(GetWorld()))
	{
		if (AOsdsEnvironment* Environment = Subsystem->GetEnvironment())
		{
			SunElevationDeg = static_cast<float>(Environment->GetSunPosition().ApparentElevationDeg);
			// Sky glow from the lit streets around: a built-up ring of tiles (Miami Beach has about
			// 300 street lights in its nine) glows about 0.06 cd/m2 at the horizon, the bright end
			// of measured big-city horizons; open country, none.
			int32 Lamps = 0;
			for (const TPair<FIntPoint, TObjectPtr<AOsdsWorldTile>>& Pair : Tiles)
			{
				if (const FOsdsTileBuild* Build = Pair.Value ? Pair.Value->GetBuild() : nullptr)
				{
					Lamps += Build->LampPointsCm.Num();
				}
			}
			Environment->SetSkyGlowLuminance(0.06 * FMath::Clamp(Lamps / 300.0, 0.0, 1.0));
		}
	}
	// Photocells switch street lights at about 10-35 lx, near sunset; windows light through dusk.
	const float Target = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((2.f - SunElevationDeg) / 6.f, 0.f, 1.f));
	LightsOn = Target;
	for (const TPair<FIntPoint, TObjectPtr<AOsdsWorldTile>>& Pair : Tiles)
	{
		if (Pair.Value)
		{
			Pair.Value->SetLightsOn(LightsOn);
		}
	}

	// The light pool: the nearest lamp heads to the car light the road (no shadows; LED cobra
	// heads of about 9000 lm put roughly 1800 cd straight down, 3000 K in Miami Beach).
	if (StreetLightPool.Num() != StreetLightPoolSize)
	{
		for (USpotLightComponent* Light : StreetLightPool)
		{
			if (Light) Light->DestroyComponent();
		}
		StreetLightPool.Reset();
		if (!GetRootComponent())
		{
			USceneComponent* Root = NewObject<USceneComponent>(this, TEXT("Root"));
			Root->SetMobility(EComponentMobility::Movable);
			SetRootComponent(Root);
			Root->RegisterComponent();
		}
		for (int32 i = 0; i < StreetLightPoolSize; ++i)
		{
			USpotLightComponent* Light = NewObject<USpotLightComponent>(this, *FString::Printf(TEXT("StreetLight%d"), i));
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetupAttachment(GetRootComponent());
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetIntensity(1800.f);
			Light->SetInnerConeAngle(50.f);
			Light->SetOuterConeAngle(74.f);
			Light->SetAttenuationRadius(3200.f);
			Light->SetSourceRadius(15.f);
			Light->SetUseTemperature(true);
			Light->SetTemperature(3000.f);
			Light->SetCastShadows(false);
			Light->SetVisibility(false);
			Light->RegisterComponent();
			AddInstanceComponent(Light);
			StreetLightPool.Add(Light);
		}
	}
	TArray<TPair<double, FVector>> Near;
	if (LightsOn > 0.01f)
	{
		FVector Velocity;
		const FVector Focus = CurrentFocus(Velocity);
		for (const TPair<FIntPoint, TObjectPtr<AOsdsWorldTile>>& Pair : Tiles)
		{
			const AOsdsWorldTile* Tile = Pair.Value;
			const FOsdsTileBuild* Build = Tile ? Tile->GetBuild() : nullptr;
			if (!Build) continue;
			const FVector Origin = Tile->GetActorLocation();
			for (const FVector& P : Build->LampPointsCm)
			{
				const FVector W = Origin + P;
				const double D = FVector::DistSquared2D(W, Focus);
				if (D < FMath::Square(250.0 * 100.0)) Near.Add({ D, W });
			}
		}
		Near.Sort([](const TPair<double, FVector>& A, const TPair<double, FVector>& B) { return A.Key < B.Key; });
	}
	for (int32 i = 0; i < StreetLightPool.Num(); ++i)
	{
		USpotLightComponent* Light = StreetLightPool[i];
		if (!Light) continue;
		const bool bUse = i < Near.Num();
		Light->SetVisibility(bUse);
		if (bUse)
		{
			Light->SetWorldLocationAndRotation(Near[i].Value, FRotator(-90.0, 0.0, 0.0));
			Light->SetIntensity(1800.f * LightsOn);
		}
	}
}

void AOsdsWorldStreamer::TickStreaming(float DeltaSeconds)
{
	if (!bStarted || !Frame.IsValid() || !Inbox.IsValid())
	{
		return;
	}
	FVector Velocity;
	FVector Focus = CurrentFocus(Velocity);

	// The origin is a tile corner, so a rebase distance under a tile diagonal would ask
	// for the tile the origin is already on: never broadcast a zero offset.
	if (FVector::Dist2D(Focus, FVector::ZeroVector) > RebaseDistanceM * 100.0 && Frame.TileAtWorldCm(Focus) != Frame.OriginTile)
	{
		Rebase(Frame.TileAtWorldCm(Focus));
		Focus = CurrentFocus(Velocity);
	}

	const FVector2d FocusTile = Frame.WorldCmToTileCoord(FVector2d(Focus.X, Focus.Y));
	// Where the car will be when a tile asked for now is ready with 4 s to spare, after waiting
	// for one load slot (2 x load + 4 s; at least 5 s, as before). The tile manager caps it at
	// one tile past the ring.
	const double AheadS = FMath::Max(5.0, 2.0 * LoadSecondsMean + LookAheadMarginSeconds);
	const FVector2d Ahead = Frame.WorldCmToTileCoord(FVector2d(Focus.X + Velocity.X * AheadS, Focus.Y + Velocity.Y * AheadS));
	Manager.SetFocus(FocusTile, Ahead);

	for (const FIntPoint& Tile : Manager.TakeEvictions())
	{
		RemoveTile(Tile);
	}
	const double Now = FPlatformTime::Seconds();
	for (const FOsdsTileJobTicket& Ticket : Manager.TakeRequests(Now))
	{
		Launch(Ticket);
	}

	// Install at most one finished tile per frame (mesh sections and collision are game-thread work).
	TSharedPtr<FResult, ESPMode::ThreadSafe> Result;
	int32 Installed = 0;
	auto NextResult = [this](TSharedPtr<FResult, ESPMode::ThreadSafe>& Out)
	{
		if (Deferred.IsValid()) { Out = MoveTemp(Deferred); Deferred.Reset(); return true; }
		return Inbox->Queue.Dequeue(Out);
	};
	// The tiles' remaining install steps, about 6 ms a frame (at least one step).
	double InstallBudget = 6.0;
	for (const TPair<FIntPoint, TObjectPtr<AOsdsWorldTile>>& Pair : Tiles)
	{
		if (InstallBudget <= 0.0) break;
		if (Pair.Value && Pair.Value->HasPendingInstall()) InstallBudget -= FMath::Max(Pair.Value->ContinueInstall(InstallBudget), 0.01);
	}
	// A tile still finishing its install steps holds the next one back: one at a time.
	bool bBusy = false;
	for (const TPair<FIntPoint, TObjectPtr<AOsdsWorldTile>>& Pair : Tiles) bBusy |= Pair.Value && Pair.Value->HasPendingInstall();
	while (!bBusy && Installed < 1 && NextResult(Result))
	{
		if (!Result.IsValid()) continue;
		if (const double* Started = LaunchedAt.Find(Result->Ticket.Serial))
		{
			// A running mean of the load time (helper fetch, worker build; not the install queue),
			// 0.5-20 s (the look-ahead caps at one tile past the ring anyway).
			if (Result->FinishedAt > *Started) LoadSecondsMean = FMath::Clamp(0.8 * LoadSecondsMean + 0.2 * (Result->FinishedAt - *Started), 0.5, 20.0);
			LaunchedAt.Remove(Result->Ticket.Serial);
		}
		if (Result->AerialPixels.Num() > 0 && !Resources->FindAerial(Result->AerialKey))
		{
			// The photograph gets a frame of its own (its upload); the tile installs next frame.
			if (Result->AerialMips.Num() > 0)
			{
				Resources->AddAerialMips(Result->AerialKey, MoveTemp(Result->AerialMips), Result->AerialWidth, Result->AerialHeight);
			}
			else
			{
				Resources->AddAerial(Result->AerialKey, Result->AerialPixels, Result->AerialWidth, Result->AerialHeight);
			}
			Result->AerialPixels.Empty();
			Deferred = Result;
			break;
		}
		const bool bOk = Result->Build.IsValid() && Result->Error.IsEmpty();
		if (!Manager.Complete(Result->Ticket, bOk, Now))
		{
			if (bOk) ++ResultsRejected;
			else
			{
				if (Result->Ticket.Generation == Manager.GetGeneration()) ++TilesFailed;
				UE_LOG(LogOsdsWorld, Warning, TEXT("Tile %s failed: %s"), *OsdsTileName(Result->Ticket.Tile), *Result->Error);
			}
			continue;
		}
		const FIntPoint TileId = Result->Ticket.Tile;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AOsdsWorldTile* Tile = GetWorld()->SpawnActor<AOsdsWorldTile>(AOsdsWorldTile::StaticClass(), Frame.TileOriginCm(TileId), FRotator::ZeroRotator, Params);
		if (!Tile) continue;
#if WITH_EDITOR
		Tile->SetActorLabel(FString::Printf(TEXT("Tile_%s"), *OsdsTileName(TileId)));
#endif
		UTexture2D* Aerial = Resources->FindAerial(Result->AerialKey);
		const FOsdsTileBuild& B = *Result->Build;
		UE_LOG(LogOsdsWorld, Display, TEXT("Tile %s: %d ribbons, %d stations, %d markings, %d stop signs, %d triangles, %d ground vertices conditioned, heights %.2f..%.2f m, built in %.0f ms%s"),
			*OsdsTileName(TileId), B.Roads.Ribbons.Num(), B.Roads.NumStations(), B.MarkingQuads, B.StopSigns, B.NumTriangles(), B.ConditionedVertices,
			B.MinHeightM, B.MaxHeightM, B.BuildMs, Aerial ? TEXT(", aerial") : TEXT(""));
		Tile->Install(Result->Build.ToSharedRef(), Resources, Aerial, true);
		if (!bStartPending) Tile->DropRoadTops();
		Tiles.Add(TileId, Tile);
		++TilesInstalled;
		++Installed;
	}

	UpdateFallbackGround(Focus);

	// Photographs no tile uses any more are released.
	if (Installed > 0)
	{
		TSet<FString> Keep;
		for (const auto& Pair : Tiles) if (Pair.Value && Pair.Value->GetBuild()) Keep.Add(Pair.Value->GetBuild()->AerialKey);
		Resources->TrimAerials(Keep);
	}
}

void AOsdsWorldStreamer::UpdateFallbackGround(const FVector& AtCm)
{
	// Every tile of the ring round the car with no collision yet (loading, or its load failed)
	// gets a flat slab, so the car never falls through a hole; it goes when the tile's own
	// collision is ready (OsdsFallbackGround). Collision only: nothing is drawn.
	const FIntPoint Here = Frame.TileAtWorldCm(AtCm);
	const int32 R = FMath::Max(1, RingRadius);
	auto Ready = [this](const FIntPoint& T) -> const FOsdsTileBuild*
	{
		const AOsdsWorldTile* Tile = GetTile(T);
		return Tile && Tile->IsCollisionReady() ? Tile->GetBuild() : nullptr;
	};
	TSet<FIntPoint> Need;
	for (int32 dy = -R; dy <= R; ++dy)
	for (int32 dx = -R; dx <= R; ++dx)
	{
		const FIntPoint T = Here + FIntPoint(dx, dy);
		if (!Ready(T)) Need.Add(T);
	}
	for (auto It = FallbackGround.CreateIterator(); It; ++It)
	{
		if (Need.Contains(It.Key())) continue;
		if (It.Value()) It.Value()->DestroyComponent();
		It.RemoveCurrent();
	}
	const double HalfCm = Frame.TileSizeM * 50.0;
	for (const FIntPoint& T : Need)
	{
		if (TObjectPtr<UBoxComponent>* Existing = FallbackGround.Find(T))
		{
			// Neighbours built since: the slab takes their edge height, but never moves under the car.
			if (*Existing && T != Here)
			{
				const double H = OsdsFallbackGround::HeightM(T, Ready, AtCm.Z / 100.0 - 0.5);
				const FVector L = (*Existing)->GetComponentLocation();
				if (FMath::Abs(H * 100.0 - 100.0 - L.Z) > 25.0) (*Existing)->SetWorldLocation(FVector(L.X, L.Y, H * 100.0 - 100.0));
			}
			continue;
		}
		int32 Neighbours = 0;
		// Without a built neighbour: half a metre under the car (it stands on the start tile's slab).
		const double HeightM = OsdsFallbackGround::HeightM(T, Ready, AtCm.Z / 100.0 - 0.5, &Neighbours);
		UBoxComponent* Slab = NewObject<UBoxComponent>(this);
		Slab->SetMobility(EComponentMobility::Movable);
		Slab->SetBoxExtent(FVector(HalfCm, HalfCm, 100.0));
		Slab->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Slab->SetHiddenInGame(true);
		Slab->RegisterComponent();
		const FVector O = Frame.TileOriginCm(T);
		Slab->SetWorldLocation(FVector(O.X + HalfCm, O.Y + HalfCm, HeightM * 100.0 - 100.0));
		FallbackGround.Add(T, Slab);
		UE_LOG(LogOsdsWorld, Display, TEXT("Fallback ground under tile %s at %.2f m (%d built neighbours)"), *OsdsTileName(T), HeightM, Neighbours);
	}
}
