// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsGameMode.h"

#include "World/OsdsTracks.h"
#include "Engine/World.h"
#include "Environment/OsdsEnvironment.h"
#include "Environment/OsdsEnvironmentSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/OsdsDriveCapture.h"
#include "Gameplay/OsdsDriveTest.h"
#include "Gameplay/OsdsHUD.h"
#include "Gameplay/OsdsLatencyProbe.h"
#include "Gameplay/OsdsSoak.h"
#include "Gameplay/OsdsPlaceholderVehicle.h"
#include "Gameplay/OsdsPlayerCameraManager.h"
#include "Gameplay/OsdsPlayerController.h"
#include "Gameplay/OsdsPreferences.h"
#include "Gameplay/OsdsPreflight.h"
#include "Gameplay/OsdsCockpitTest.h"
#include "Gameplay/OsdsPadReview.h"
#include "Gameplay/OsdsVehicleControl.h"
#include "Gameplay/OsdsViewCheck.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OsdsGameInstance.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/OsdsUnits.h"
#include "Vehicle/OsdsVehicle.h"
#include "Vehicle/OsdsVehicleMovementComponent.h"
#include "Misc/App.h"
#include "Presentation/OsdsCarAssets.h"
#include "World/OsdsWorldTile.h"
#include "World/OsdsBridges.h"
#include "World/OsdsGuardRails.h"
#include "World/OsdsRegionalTerrain.h"
#include "World/OsdsSignals.h"
#include "World/OsdsStartPick.h"
#include "World/OsdsTrees.h"
#include "Presentation/OsdsCarAssets.h"
#include "World/OsdsWorldStreamer.h"
#include "World/OsdsWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsDrive, Log, All);

namespace
{
	/** How far from the start point the start road may be. */
	constexpr double StartSearchRadiusM = 900.0;
	/** How far a recover looks for a road. */
	constexpr double RecoverSearchRadiusM = 250.0;
	/** The car is placed this far above the road and settles onto its springs. */
	constexpr double SpawnLiftCm = 3.0;

	bool ParseLatLon(const FString& Text, double& OutLat, double& OutLon)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() != 2 || !Parts[0].IsNumeric() || !Parts[1].IsNumeric())
		{
			return false;
		}
		OutLat = FCString::Atod(*Parts[0]);
		OutLon = FCString::Atod(*Parts[1]);
		return true;
	}

	/** The bundled offline place (its tiles ship in data/tiles). */
	bool IsBundledPlace(const FOsdsPlace& Place)
	{
		const FOsdsPlace Bundled = OsdsPlaces::Fallback();
		return FMath::Abs(Place.Lat - Bundled.Lat) < 1e-4 && FMath::Abs(Place.Lon - Bundled.Lon) < 1e-4;
	}

	/** A normal drive starts on a random pool road; checks, captures and previews start on the bundled streets. */
	bool WantsRandomStart(const TCHAR* Cmd, bool bConfigDefault)
	{
		if (FParse::Param(Cmd, TEXT("OsdsRandomStart"))) return true;
		static const TCHAR* BundledFlags[] = {
			TEXT("OsdsBundledStart"), TEXT("OsdsDriveCapture"), TEXT("OsdsDriveTest"), TEXT("OsdsShellSelfTest"),
			TEXT("OsdsShellCapture"), TEXT("OsdsLab"), TEXT("OsdsLatencyProbe"),
		};
		for (const TCHAR* Flag : BundledFlags)
		{
			if (FParse::Param(Cmd, Flag)) return false;
		}
		FString Ignored;
		if (FParse::Value(Cmd, TEXT("OsdsPreview="), Ignored) || FParse::Value(Cmd, TEXT("OsdsDrivePerf="), Ignored)) return false;
		return bConfigDefault && !GIsAutomationTesting;
	}
}

AOsdsGameMode::AOsdsGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	DefaultPawnClass = AOsdsPlaceholderVehicle::StaticClass();
	PlayerControllerClass = AOsdsPlayerController::StaticClass();
	HUDClass = AOsdsHUD::StaticClass();
}

UClass* AOsdsGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (VehiclePawnClass.IsValid())
	{
		UClass* Loaded = VehiclePawnClass.TryLoadClass<APawn>();
		if (Loaded && Loaded->ImplementsInterface(UOsdsVehicleControl::StaticClass()))
		{
			return Loaded;
		}
		UE_LOG(LogOsdsDrive, Warning, TEXT("OSDS VehiclePawnClass %s is not a pawn implementing IOsdsVehicleControl; using the placeholder."),
			*VehiclePawnClass.ToString());
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

AOsdsWorldStreamer* AOsdsGameMode::GetStreamer() const
{
	return Streamer.Get();
}

void AOsdsGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// Decided here, not in StartPlay: the local player logs in (PostLogin ->
	// RestartPlayer) while the map loads, before StartPlay, and a world drive
	// must hold that player until the road under the car exists.
	const TCHAR* Cmd = FCommandLine::Get();
	CarId = StartCarId;
	// The car driven last (DrivingPreferences.gd "car"), when it still exists.
	const FString Remembered = OsdsPreferences::GetString(TEXT("car"), FString());
	const UOsdsGameInstance* Garage = GetGameInstance<UOsdsGameInstance>();
	if (!Remembered.IsEmpty() && Garage && Garage->HasCarDefinition(Remembered))
	{
		CarId = Remembered;
	}
	FParse::Value(Cmd, TEXT("OsdsCar="), CarId);
	// The lighting lab builds its own street, and -OsdsNoWorld drives the loaded map.
	bWorldDrive = GetWorld() && GetWorld()->IsGameWorld()
		&& !FParse::Param(Cmd, TEXT("OsdsNoWorld")) && !FParse::Param(Cmd, TEXT("OsdsLab"));
}

void AOsdsGameMode::StartPlay()
{
	OsdsFramePacing::Apply();
	if (bWorldDrive)
	{
		BeginWorldDrive();
	}
	Super::StartPlay();
	if (!bWorldDrive && !PendingPlayers.IsEmpty())
	{
		// The world could not start: the held players drive the loaded map instead.
		TArray<TWeakObjectPtr<AController>> Held = MoveTemp(PendingPlayers);
		for (const TWeakObjectPtr<AController>& Weak : Held)
		{
			if (AController* Player = Weak.Get())
			{
				Super::RestartPlayer(Player);
			}
		}
	}
}

void AOsdsGameMode::BeginWorldDrive()
{
	UWorld* World = GetWorld();
	UOsdsWorldSubsystem* Worlds = World->GetSubsystem<UOsdsWorldSubsystem>();
	if (!Worlds)
	{
		bWorldDrive = false;
		return;
	}
	const TCHAR* Cmd = FCommandLine::Get();

	// Where this drive begins (Drive.gd plan_start): a chosen point, else a seeded random
	// start-pool road in normal play, else the bundled place, which checks and captures use
	// so their runs repeat.
	int32 Seed = 0;
	if (FParse::Value(Cmd, TEXT("OsdsStartSeed="), Seed)) StartRng.Initialize(Seed);
	else StartRng.GenerateNewSeed();
	FOsdsPlace First = OsdsPlaces::Fallback();
	First.Street = StartRoad;
	FString Mode = TEXT("bundled");
	FString Start;
	double Lat = 0.0, Lon = 0.0;
	if (FParse::Value(Cmd, TEXT("OsdsStart="), Start, /*bShouldStopOnSeparator=*/ false) && ParseLatLon(Start, Lat, Lon))
	{
		First = FOsdsPlace::Make(Lon, Lat, FString::Printf(TEXT("%.5f, %.5f"), Lat, Lon));
		Mode = TEXT("chosen");
	}
	else if (WantsRandomStart(Cmd, bRandomStart))
	{
		StartPool = OsdsPlaces::LoadStartPool();
		StartChain = OsdsPlaces::Sequence(StartPool, StartRng, 1 + OsdsPlaces::Retries);
		if (!StartChain.IsEmpty())
		{
			First = StartChain[0];
			StartChain.RemoveAt(0);
			bStartLaunch = true;
			Mode = TEXT("random");
		}
	}
	// A race track by its id in data/tracks/tracks.json (-OsdsTrack=daytona): its start/finish line,
	// facing the racing direction, as the pause menu's Race tracks list starts it.
	FString TrackId;
	if (FParse::Value(Cmd, TEXT("OsdsTrack="), TrackId))
	{
		const FOsdsTrack* Track = OsdsTracks::All().FindByPredicate([&](const FOsdsTrack& T) { return T.Id.Equals(TrackId, ESearchCase::IgnoreCase); });
		if (Track)
		{
			First = OsdsTracks::ToPlace(*Track);
			Mode = TEXT("track");
		}
		else UE_LOG(LogOsdsDrive, Warning, TEXT("No race track '%s' in %s"), *TrackId, *OsdsTracks::DataPath());
	}
	FString Road;
	if (FParse::Value(Cmd, TEXT("OsdsStartRoad="), Road))
	{
		First.Street = Road;
	}
	// Checks parse this line (Drive.gd: "START <mode> seed=<n> <title>").
	UE_LOG(LogOsdsDrive, Display, TEXT("START %s seed=%d %s"), *Mode, StartRng.GetInitialSeed(), *First.Title);

	// The world subsystem may already have started a place from the command line.
	AOsdsWorldStreamer* Existing = Worlds->GetStreamer();
	const bool bSamePlace = Existing && Existing->GetFrame().IsValid()
		&& Existing->GetFrame().OriginTile == FOsdsWorldFrame::ForPlace(First.Lon, First.Lat).OriginTile;
	Streamer = Existing ? Existing : Worlds->StartDrive(First.Lon, First.Lat, First.Title);
	if (!Streamer.IsValid())
	{
		UE_LOG(LogOsdsDrive, Error, TEXT("Could not start the world stream; driving the loaded map."));
		bWorldDrive = false;
		return;
	}
	Streamer->OnRebased.AddUObject(this, &AOsdsGameMode::HandleRebased);
	BeginTravel(First, Existing != nullptr && !bSamePlace);

	// The land to the horizon round wherever the car is (needs the helper; drawn runs only).
	if (FApp::CanEverRender() && !FParse::Param(Cmd, TEXT("OsdsNoLandscape")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AOsdsRegionalTerrain* Land = World->SpawnActor<AOsdsRegionalTerrain>(AOsdsRegionalTerrain::StaticClass(), FTransform::Identity, Params))
		{
			Land->Streamer = Streamer;
		}
	}
	// Trees anywhere, from the canopy height map (needs the helper; drawn runs only).
	if (FApp::CanEverRender() && !FParse::Param(Cmd, TEXT("OsdsNoTrees")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AOsdsTrees* Trees = World->SpawnActor<AOsdsTrees>(AOsdsTrees::StaticClass(), FTransform::Identity, Params))
		{
			Trees->Streamer = Streamer;
		}
	}
	// Guard rails where the ground falls away beside the road (AASHTO warrant).
	if (!FParse::Param(Cmd, TEXT("OsdsNoGuardRails")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AOsdsGuardRails* Rails = World->SpawnActor<AOsdsGuardRails>(AOsdsGuardRails::StaticClass(), FTransform::Identity, Params))
		{
			Rails->Streamer = Streamer;
		}
	}
	// Traffic signals where two through roads cross.
	if (!FParse::Param(Cmd, TEXT("OsdsNoSignals")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AOsdsSignals* Signals = World->SpawnActor<AOsdsSignals>(AOsdsSignals::StaticClass(), FTransform::Identity, Params))
		{
			Signals->Streamer = Streamer;
		}
	}
	// The structure under the mapped bridges: deck, parapets, piers.
	if (!FParse::Param(Cmd, TEXT("OsdsNoBridgeDecks")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AOsdsBridges* Bridges = World->SpawnActor<AOsdsBridges>(AOsdsBridges::StaticClass(), FTransform::Identity, Params))
		{
			Bridges->Streamer = Streamer;
		}
	}

	if (FParse::Param(Cmd, TEXT("OsdsDriveCapture")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AOsdsDriveCapture>(AOsdsDriveCapture::StaticClass(), FTransform::Identity, Params);
	}
	if (FParse::Param(Cmd, TEXT("OsdsDriveTest")) || FParse::Param(Cmd, TEXT("OsdsDriveLog")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AOsdsDriveTest>(AOsdsDriveTest::StaticClass(), FTransform::Identity, Params);
	}
	FString PadReviewDir;
	if (FParse::Value(Cmd, TEXT("OsdsPadReview="), PadReviewDir) || FParse::Param(Cmd, TEXT("OsdsPadReview")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AOsdsPadReview>(AOsdsPadReview::StaticClass(), FTransform::Identity, Params);
	}
	if (FParse::Param(Cmd, TEXT("OsdsCockpitTest")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AOsdsCockpitTest>(AOsdsCockpitTest::StaticClass(), FTransform::Identity, Params);
	}
	if (FParse::Param(Cmd, TEXT("OsdsPreflight")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AOsdsPreflight>(AOsdsPreflight::StaticClass(), FTransform::Identity, Params);
	}
	if (AOsdsViewCheck::IsRequested(Cmd))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AOsdsViewCheck>(AOsdsViewCheck::StaticClass(), FTransform::Identity, Params);
	}
	if (FParse::Param(Cmd, TEXT("OsdsLatencyProbe")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AOsdsLatencyProbe>(AOsdsLatencyProbe::StaticClass(), FTransform::Identity, Params);
	}
	FString SoakMinutes;
	if (FParse::Value(Cmd, TEXT("OsdsSoak="), SoakMinutes))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AOsdsSoak>(AOsdsSoak::StaticClass(), FTransform::Identity, Params);
	}
	UE_LOG(LogOsdsDrive, Display, TEXT("Drive at %s in %s: waiting for the tiles around the start"), *First.Title, *CarId);
}

void AOsdsGameMode::BeginTravel(const FOsdsPlace& Place, bool bRestartWorld)
{
	AOsdsWorldStreamer* Stream = Streamer.Get();
	if (!Stream)
	{
		return;
	}
	CurrentPlace = Place;
	StartRoadName = Place.Street;
	bCarPlaced = false;
	if (bRestartWorld)
	{
		Stream->StartAt(Place.Lon, Place.Lat, Place.Title);
	}
	StartCm = Stream->GetFrame().LonLatToWorldCm(Place.Lon, Place.Lat);
	Stream->SetFocus(StartCm, FVector::ZeroVector);
	WaitStartedSeconds = FPlatformTime::Seconds();
	LastMessageSeconds = -100.0;
	bStartCarBuilt = false;
	// The car's model and the traffic models start on worker threads now (glTF and rig, most of a
	// heavy model's time: the Miata's took 27.7 s on the game thread at Welch); the meshes are built
	// on the game thread later, while the start tiles load.
	OsdsCarAssets::PrepareAsync(CarId);
	for (const FString& Model : OsdsParkedCars::AllCarIds()) OsdsCarAssets::PrepareAsync(Model);

	// The sun and sky follow the place (unless the command line placed the observer).
	double CommandLineLat = 0.0;
	if (!FParse::Value(FCommandLine::Get(), TEXT("OsdsLat="), CommandLineLat))
	{
		if (UOsdsEnvironmentSubsystem* Environments = UOsdsEnvironmentSubsystem::Get(GetWorld()))
		{
			if (AOsdsEnvironment* Environment = Environments->GetEnvironment())
			{
				Environment->SetPlace(Place.Lat, Place.Lon, Environment->ElevationM);
			}
		}
	}
}

void AOsdsGameMode::HoldPlayers()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Player = It->Get();
		if (!Player) continue;
		if (APawn* Pawn = Player->GetPawn())
		{
			Player->UnPossess();
			Pawn->Destroy();
		}
		PendingPlayers.AddUnique(Player);
	}
	bCarPlaced = false;
}

void AOsdsGameMode::TravelTo(const FOsdsPlace& Place)
{
	if (!bWorldDrive || !Streamer.IsValid() || !Place.IsValid())
	{
		return;
	}
	// A chosen place ends any start sequence (Drive.gd choose_place).
	StartChain.Reset();
	bStartLaunch = false;
	StartNote.Reset();
	HoldPlayers();
	UE_LOG(LogOsdsDrive, Display, TEXT("Travel to %s (%.5f, %.5f)%s"), *Place.Title, Place.Lat, Place.Lon,
		Place.Street.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on %s"), *Place.Street));
	BeginTravel(Place, true);
	UpdateLoadingMessage();
}

bool AOsdsGameMode::SurpriseMe()
{
	if (StartPool.IsEmpty())
	{
		StartPool = OsdsPlaces::LoadStartPool();
	}
	TArray<FOsdsPlace> Picks = OsdsPlaces::Sequence(StartPool, StartRng, 1 + OsdsPlaces::Retries, CurrentPlace.Title);
	if (Picks.IsEmpty())
	{
		return false;
	}
	const FOsdsPlace First = Picks[0];
	Picks.RemoveAt(0);
	TravelTo(First);
	StartChain = MoveTemp(Picks);
	return true;
}

void AOsdsGameMode::TravelFailed(const FString& Reason)
{
	UE_LOG(LogOsdsDrive, Warning, TEXT("%s did not load: %s"), *CurrentPlace.Title, *Reason);
	if (!StartChain.IsEmpty())
	{
		const FOsdsPlace Next = StartChain[0];
		StartChain.RemoveAt(0);
		BeginTravel(Next, true);
		return;
	}
	StartNote = bStartLaunch
		? FString(TEXT("No random road loaded · starting in bundled Miami Beach (works offline)"))
		: FString::Printf(TEXT("%s did not load · back in bundled Miami Beach (works offline)"), *CurrentPlace.Title);
	bStartLaunch = false;
	UE_LOG(LogOsdsDrive, Display, TEXT("START fallback after: %s"), *Reason);
	FOsdsPlace Fallback = OsdsPlaces::Fallback();
	Fallback.Street = StartRoad;
	BeginTravel(Fallback, true);
}

bool AOsdsGameMode::WorldToLonLat(const FVector& WorldCm, double& OutLon, double& OutLat) const
{
	const AOsdsWorldStreamer* Stream = Streamer.Get();
	if (!Stream || !Stream->GetFrame().IsValid())
	{
		return false;
	}
	Stream->GetFrame().WorldCmToLonLat(WorldCm, OutLon, OutLat);
	return true;
}

void AOsdsGameMode::CycleCar(APlayerController* Player)
{
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const UOsdsGameInstance* GameInstance = GetGameInstance<UOsdsGameInstance>();
	if (!Pawn || !GameInstance || !bCarPlaced)
	{
		return;
	}
	TArray<FString> Ids = GameInstance->GetAvailableCarIds();
	Ids.Sort();
	if (Ids.Num() < 2)
	{
		return;
	}
	const int32 Index = Ids.IndexOfByKey(CarId);
	CarId = Ids[(Index + 1) % Ids.Num()];
	OsdsPreferences::SetString(TEXT("car"), CarId);
	RespawnCar(Player);
	if (AOsdsPlayerController* SimPlayer = Cast<AOsdsPlayerController>(Player))
	{
		const FOsdsCarDefinition* Car = GameInstance->FindCarDefinition(CarId);
		SimPlayer->ShowMessage(FString::Printf(TEXT("Car · %s"), Car ? *Car->Name : *CarId), 3.f);
	}
}

void AOsdsGameMode::RespawnCar(APlayerController* Player, const FString& NewCarId)
{
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	if (!Pawn || !bCarPlaced)
	{
		return;
	}
	if (!NewCarId.IsEmpty())
	{
		CarId = NewCarId;
	}
	// The new car starts at rest where the old one stood, facing the same way.
	FVector Ground = Pawn->GetActorLocation();
	const FRotator Heading(0.0, Pawn->GetActorRotation().Yaw, 0.0);
	double Z = 0.0;
	if (const AOsdsVehicle* Vehicle = Cast<AOsdsVehicle>(Pawn); Vehicle && Vehicle->GetVehicleMovement())
	{
		// The ground under the body box, whatever it stands on (a road, the drive robot's pad).
		Ground -= Vehicle->GetActorQuat().RotateVector(FVector(0.0, 0.0,
			OsdsUnits::ToUnrealLength(Vehicle->GetVehicleMovement()->GetCarDefinition().CollisionShapeCenterYM)));
	}
	else if (AOsdsWorldStreamer* Stream = Streamer.Get(); Stream && Stream->SurfaceHeightAt(Ground, Z))
	{
		Ground.Z = Z;
	}
	Player->UnPossess();
	Pawn->Destroy();
	SpawnVehicleAt(Player, Ground, Heading);
}

void AOsdsGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!bWorldDrive || !NewPlayer)
	{
		Super::RestartPlayer(NewPlayer);
		return;
	}
	if (bCarPlaced && NewPlayer->GetPawn())
	{
		return;
	}
	// The car waits until the road under it exists.
	PendingPlayers.AddUnique(NewPlayer);
	if (bCarPlaced)
	{
		TryPlacePendingPlayers();
	}
}

void AOsdsGameMode::UpdateLoadingMessage()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastMessageSeconds < 2.0)
	{
		return;
	}
	LastMessageSeconds = Now;
	for (const TWeakObjectPtr<AController>& Weak : PendingPlayers)
	{
		if (AOsdsPlayerController* Player = Cast<AOsdsPlayerController>(Weak.Get()))
		{
			const int32 Tiles = Streamer.IsValid() ? Streamer->NumTiles() : 0;
			Player->ShowMessage(FString::Printf(TEXT("Building %s · %d of 9 tiles"),
				Streamer.IsValid() ? *Streamer->GetTitle() : TEXT("the place"), FMath::Min(Tiles, 9)), 2.5f);
		}
	}
}

bool AOsdsGameMode::TryPlacePendingPlayers()
{
	PendingPlayers.RemoveAll([](const TWeakObjectPtr<AController>& Weak) { return !Weak.IsValid(); });
	if (PendingPlayers.IsEmpty())
	{
		return true;
	}
	AOsdsWorldStreamer* Stream = Streamer.Get();
	if (!Stream)
	{
		return false;
	}
	const bool bFirstPlacement = !bCarPlaced;
	Stream->HoldRoadTops();
	if (bFirstPlacement)
	{
		Stream->SetFocus(StartCm, FVector::ZeroVector);
		// The start tiles first and together (the start tile and its corners at once); while they
		// are fetched and built, the player's car model, then the traffic and parked-car models
		// (one a frame): building a model holds the game thread (30 s for the Miatas' 38 MB on a
		// busy PC), and after the tiles it added all of that to the start (Detroit and Doraville,
		// 2026-11-14: placed after 98-100 s); the 1999 Miata also froze a packaged drive for 3.3 s
		// when its first car appeared. Background cars never take an unbuilt heavy model
		// (OsdsParkedCars::CarFor), so the list holds light models only.
		Stream->SetStartBoost(true);
		const bool bTilesUnderWay = Stream->NumTilesInFlight() >= 3 || FPlatformTime::Seconds() - WaitStartedSeconds > 4.0;
		bool bModelsReady = true;
		if (bStartCarBuilt)
		{
			const TArray<FString> Models = OsdsParkedCars::AllCarIds();
			// Each model only once its worker-thread part is done: Get would otherwise wait for it on
			// the game thread (the Miata: 29 s with the window not answering).
			if (PrebuiltModels < Models.Num() && OsdsCarAssets::IsReadyToGet(Models[PrebuiltModels]))
			{
				const double ModelStart = FPlatformTime::Seconds();
				FString ModelError;
				OsdsCarAssets::Get(Models[PrebuiltModels], ModelError);
				UE_LOG(LogOsdsDrive, Display, TEXT("Traffic model %s built before the drive in %.2f s%s"), *Models[PrebuiltModels],
					FPlatformTime::Seconds() - ModelStart, ModelError.IsEmpty() ? TEXT("") : *(TEXT(": ") + ModelError));
				++PrebuiltModels;
			}
			bModelsReady = PrebuiltModels >= Models.Num();
		}
		else
		{
			bModelsReady = false;
		}
		const double Waited = FPlatformTime::Seconds() - WaitStartedSeconds;
		// The player's car once its worker-thread part (glTF and rig) is done; until then the loading
		// screen keeps drawing. Every Miata start froze the game for about 29 s here (Get waited on
		// the worker from the game thread), long enough for Windows to call it Not Responding.
		// Past the start timeout it is taken as it is.
		if (!bStartCarBuilt && (CarId.IsEmpty() || bTilesUnderWay) &&
			(OsdsCarAssets::IsReadyToGet(CarId) || FPlatformTime::Seconds() - WaitStartedSeconds > StartTimeoutSeconds))
		{
			bStartCarBuilt = true;
			const double Before = FPlatformTime::Seconds();
			FString Error;
			if (!CarId.IsEmpty()) OsdsCarAssets::Get(CarId, Error);
			UE_LOG(LogOsdsDrive, Display, TEXT("The car's model (%s) built while %d start tiles load: %.1f s"), *CarId, Stream->NumTilesInFlight(), FPlatformTime::Seconds() - Before);
		}
		const bool bReady = Stream->AreTilesReadyAround(StartCm, 1) && bModelsReady;
		const bool bTimedOut = Waited > StartTimeoutSeconds;
		// Tiles that cannot load (offline, no helper, nothing there): the next place, else the
		// bundled one (Drive.gd start_next). The bundled place itself loads from disk. A failed
		// tile is retried after 15 s, so one passing failure (a busy cache file, a slow server)
		// must not give the place up: offline, all nine fail at once.
		const int32 Failed = Stream->NumTilesFailed();
		if (!bReady && (Failed >= 4 || (Failed >= 1 && Waited > 90.0)) && !IsBundledPlace(CurrentPlace))
		{
			TravelFailed(FString::Printf(TEXT("%d tiles failed to load"), Failed));
			return false;
		}
		if (!bReady && !bTimedOut)
		{
			UpdateLoadingMessage();
			return false;
		}
		if (!bReady)
		{
			UE_LOG(LogOsdsDrive, Warning, TEXT("The start tiles were not all ready after %.0f s; placing the car on what is there."), StartTimeoutSeconds);
		}
	}

	// On the place's street (a start-pool point is a node on it), facing its bearing on a two-way
	// road; else the nearest public road. A pool point takes the road under it only within 30 m
	// (Drive.gd: named street within 125 m, loose fallback main road within 30 m).
	const double PreferredYaw = CurrentPlace.bHasBearing ? OsdsPlaces::BearingToYawDeg(CurrentPlace.BearingDeg) : TNumericLimits<double>::Max();
	const double NamedRadiusM = CurrentPlace.bLoose ? 125.0 : StartSearchRadiusM;
	const double AnyRadiusM = CurrentPlace.bLoose ? 30.0 : StartSearchRadiusM;
	FVector Ground = StartCm;
	FRotator Heading = CurrentPlace.bHasBearing ? FRotator(0.0, PreferredYaw, 0.0) : FRotator::ZeroRotator;
	// The start places by the general rules (OsdsStartPick: mid-block, inside its tile,
	// straight, on the carriageway, a road going on ahead), best first; the first one a ground
	// trace confirms (the road surface is there, at the road's height, nothing above it). Where
	// none passes, the nearest-road rule as before.
	auto PickStart = [&](const FString& Name, double RadiusM) -> bool
	{
		bStartByNearestRoad = false;
		FString WhyNone;
		const TArray<FOsdsStartPose> Picks = Stream->FindStartCandidates(Name, StartCm, RadiusM, PreferredYaw, &WhyNone);
		// Never silent: a start by the old nearest-road rule is the fault the release test sees
		// (Welch, WV, nose at the bank, when the pick saw no road faces on installed tiles).
		if (Picks.IsEmpty())
		{
			// One fixed string the release judge fails on: "OSDS START FALLBACK: start pick found no candidate".
			UE_LOG(LogOsdsDrive, Warning, TEXT("OSDS START FALLBACK: start pick found no candidate%s within %.0f m (%s); the nearest-road rule places the car"),
				Name.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on %s"), *Name), RadiusM, *WhyNone);
		}
		for (const FOsdsStartPose& Pick : Picks)
		{
			FString Why;
			if (!GroundTraceOnRoad(Pick.CheckCm, Why))
			{
				UE_LOG(LogOsdsDrive, Display, TEXT("Start candidate at %s left out: %s"), *Pick.LocationCm.ToString(), *Why);
				continue;
			}
			Ground = Pick.LocationCm;
			Heading = Pick.Rotation;
			UE_LOG(LogOsdsDrive, Display, TEXT("Start: %.0f m from the start point, %.0f m of road ahead"), Pick.DistanceM, Pick.AheadM);
			return true;
		}
		if (!Picks.IsEmpty())
		{
			UE_LOG(LogOsdsDrive, Warning, TEXT("OSDS START FALLBACK: start pick found no candidate the ground trace confirms (%d left out); the nearest-road rule places the car"), Picks.Num());
		}
		bStartByNearestRoad = true;
		return Stream->FindRoadPoseFacing(Name, StartCm, RadiusM, PreferredYaw, Ground, Heading);
	};
	bStartByNearestRoad = false;
	const bool bNamed = !StartRoadName.IsEmpty() && PickStart(StartRoadName, NamedRadiusM);
	if (!bNamed && !StartRoadName.IsEmpty())
	{
		UE_LOG(LogOsdsDrive, Warning, TEXT("No room to start on %s within %.0f m of the start; starting on the nearest road."), *StartRoadName, NamedRadiusM);
	}
	const bool bOnRoad = bNamed || PickStart(FString(), AnyRadiusM);
	if (!bOnRoad)
	{
		if (bFirstPlacement && (!StartChain.IsEmpty() || bStartLaunch))
		{
			TravelFailed(TEXT("no road near the start point"));
			return false;
		}
		double Z = 0.0;
		if (Stream->SurfaceHeightAt(StartCm, Z))
		{
			Ground.Z = Z;
		}
		UE_LOG(LogOsdsDrive, Warning, TEXT("No road within %.0f m of the start; the car starts on the ground."), AnyRadiusM);
	}
	for (const TWeakObjectPtr<AController>& Weak : PendingPlayers)
	{
		if (AController* Player = Weak.Get())
		{
			if (APawn* Pawn = SpawnVehicleAt(Player, Ground, Heading))
			{
				UE_LOG(LogOsdsDrive, Display, TEXT("Car placed%s at %s heading %.1f deg after %.1f s"),
					bOnRoad ? *FString::Printf(TEXT(" on %s"), bNamed ? *StartRoadName : TEXT("the nearest road")) : TEXT(""),
					*Ground.ToString(), Heading.Yaw, FPlatformTime::Seconds() - WaitStartedSeconds);
				if (AOsdsPlayerController* SimPlayer = Cast<AOsdsPlayerController>(Player))
				{
					SimPlayer->ShowMessage(!StartNote.IsEmpty() ? StartNote
						: FString::Printf(TEXT("%s  ·  RT or W to accelerate  ·  / or Menu to go somewhere else"), *CurrentPlace.Title), 6.f);
				}
				(void)Pawn;
			}
		}
	}
	PendingPlayers.Reset();
	bCarPlaced = true;
	Stream->SetStartBoost(false);
	StartChain.Reset();
	bStartLaunch = false;
	StartNote.Reset();

	return true;
}

bool AOsdsGameMode::GroundTraceOnRoad(const TArray<FVector>& PointsCm, FString& OutWhy) const
{
	if (PointsCm.IsEmpty())
	{
		OutWhy = TEXT("no points to check");
		return false;
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(OsdsStartTrace), true);
	// Under the car's middle and its four corners, the first thing down from 5 m is the road at
	// its own height (within 8 cm): a kerb or a sidewalk (15 cm), a bank, a building or a hole fails.
	for (int32 i = 0; i < PointsCm.Num(); ++i)
	{
		const FVector& At = PointsCm[i];
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, 500.0), At - FVector(0.0, 0.0, 500.0), Objects, Params))
		{
			OutWhy = FString::Printf(TEXT("no ground under point %d"), i);
			return false;
		}
		const double Off = Hit.ImpactPoint.Z - At.Z;
		if (FMath::Abs(Off) > 8.0)
		{
			OutWhy = FString::Printf(TEXT("the ground under point %d is %+.0f cm off the road's height"), i, Off);
			return false;
		}
	}
	return true;
}

APawn* AOsdsGameMode::SpawnVehicleAt(AController* Player, const FVector& GroundCm, const FRotator& Heading)
{
	UWorld* World = GetWorld();
	UClass* PawnClass = GetDefaultPawnClassForController(Player);
	APawn* Pawn = nullptr;
	if (PawnClass && PawnClass->IsChildOf(AOsdsVehicle::StaticClass()))
	{
		FOsdsCarDefinition Car;
		const UOsdsGameInstance* GameInstance = GetGameInstance<UOsdsGameInstance>();
		const FOsdsCarDefinition* Known = GameInstance ? GameInstance->FindCarDefinition(CarId) : nullptr;
		FString Error;
		if (Known)
		{
			Car = *Known;
		}
		else if (!FOsdsCarDefinitionLoader::LoadById(CarId, Car, Error))
		{
			UE_LOG(LogOsdsDrive, Error, TEXT("Car %s: %s; using nb1999"), *CarId, *Error);
			FOsdsCarDefinitionLoader::LoadById(TEXT("nb1999"), Car, Error);
		}
		AOsdsVehicle* Vehicle = World->SpawnActorDeferred<AOsdsVehicle>(PawnClass, FTransform(Heading, GroundCm), nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Vehicle)
		{
			return nullptr;
		}
		Vehicle->SetCarDefinition(Car);
		Vehicle->FinishSpawning(Vehicle->BodyTransformForGroundPose(GroundCm + FVector(0.0, 0.0, SpawnLiftCm), static_cast<float>(Heading.Yaw)));
		Vehicle->AttachCarModel();
		Pawn = Vehicle;
	}
	else if (PawnClass)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Pawn = World->SpawnActor<APawn>(PawnClass, FTransform(Heading, GroundCm + FVector(0.0, 0.0, 5.0)), Params);
	}
	if (Pawn)
	{
		Player->SetControlRotation(Heading);
		Player->Possess(Pawn);
		FinishRestartPlayer(Player, Heading);
	}
	return Pawn;
}

void AOsdsGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bWorldDrive)
	{
		return;
	}
	if (!PendingPlayers.IsEmpty())
	{
		TryPlacePendingPlayers();
		return;
	}
	AOsdsWorldStreamer* Stream = Streamer.Get();
	if (!Stream)
	{
		return;
	}
	// The world streams around the first player's car.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* Player = It->Get())
		{
			if (const APawn* Pawn = Player->GetPawn())
			{
				Stream->SetFocus(Pawn->GetActorLocation(), Pawn->GetVelocity());
				break;
			}
		}
	}
}

void AOsdsGameMode::HandleRebased(const FVector& OffsetCm)
{
	StartCm += OffsetCm;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Player = It->Get();
		if (APawn* Pawn = Player ? Player->GetPawn() : nullptr)
		{
			// Teleport keeps the body's velocity: the car drives on across the shift.
			Pawn->AddActorWorldOffset(OffsetCm, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogTemp, Display, TEXT("PLAYER MOVE rebase: the world shifted by X=%.1f Y=%.1f Z=%.1f cm (the car keeps its place in the world)"), OffsetCm.X, OffsetCm.Y, OffsetCm.Z);
		}
		if (AOsdsPlayerCameraManager* Camera = Player ? Cast<AOsdsPlayerCameraManager>(Player->PlayerCameraManager) : nullptr)
		{
			Camera->SnapNextFrame();
		}
	}
}

void AOsdsGameMode::RecoverVehicle(APlayerController* Player)
{
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	IOsdsVehicleControl* Vehicle = Cast<IOsdsVehicleControl>(Pawn);
	if (!Vehicle)
	{
		return;
	}
	const FVector Location = Pawn->GetActorLocation();
	FVector Ground = Location;
	FRotator Heading(0.0, Pawn->GetActorRotation().Yaw, 0.0);
	bool bOnRoad = false;
	if (AOsdsWorldStreamer* Stream = Streamer.Get())
	{
		FVector RoadGround;
		FRotator RoadHeading;
		if (Stream->FindRoadPose(FString(), Location, RecoverSearchRadiusM, RoadGround, RoadHeading))
		{
			Ground = RoadGround;
			Heading = RoadHeading;
			bOnRoad = true;
		}
	}
	if (!bOnRoad)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OsdsRecover), false, Pawn);
		// Probe from just above the car (3 m) first: under an overpass or a canopy
		// a high start would land the car on the deck above. Only a car that has
		// fallen below the surface needs the probe from high above.
		if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 300.0), Location - FVector(0.0, 0.0, 20000.0), ECC_Visibility, Params)
			|| GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 20000.0), Location - FVector(0.0, 0.0, 20000.0), ECC_Visibility, Params))
		{
			Ground = Hit.ImpactPoint;
		}
	}
	Vehicle->ResetVehicle(FTransform(Heading, Ground + FVector(0.0, 0.0, SpawnLiftCm)));
	if (AOsdsPlayerController* Controller = Cast<AOsdsPlayerController>(Player))
	{
		Controller->ShowMessage(bOnRoad ? TEXT("Back on the road") : TEXT("Recovered upright"), 3.f);
	}
}
