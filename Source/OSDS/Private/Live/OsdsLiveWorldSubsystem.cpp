// Copyright OSDS contributors. All Rights Reserved.

#include "Live/OsdsLiveWorldSubsystem.h"

#include "Engine/World.h"
#include "Components/PostProcessComponent.h"
#include "Environment/OsdsEnvironment.h"
#include "Environment/OsdsEnvironmentSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/OsdsHUD.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Live/OsdsAerialEye.h"
#include "Live/OsdsLiveSky.h"
#include "Atlas/OsdsUserAgent.h"
#include "Live/OsdsNeuralDetail.h"
#include "Live/OsdsSceneParser.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "UObject/UObjectIterator.h"
#include "World/OsdsTileBuilder.h"
#include "World/OsdsWorldResources.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "World/OsdsGeo.h"
#include "World/OsdsWorldStreamer.h"
#include "World/OsdsWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsLive, Log, All);

namespace OsdsLiveWorldPrivate
{
	constexpr double MoveRefetchKm = 5.0;
	constexpr int32 AircraftRadiusNm = 25;

	// Planes are out of v2.0.3: no aircraft are drawn and the adsb.lol feed (a community server)
	// is not asked at all. When they return, the feed runs every 60 s, not every 10 s.
	TAutoConsoleVariable<int32> CVarFeaturePlanes(TEXT("sc.Feature.Planes"), 0,
		TEXT("1: live aircraft in the sky from adsb.lol, polled every 60 s (read each poll). 0 (default): planes are out of the game for now, nothing is fetched."));

	FAutoConsoleCommandWithWorldAndArgs GReport(
		TEXT("OSDS.Live.Report"), TEXT("OSDS.Live.Report: log the live real-world data and the state of each feed"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UOsdsLiveWorldSubsystem* Live = UOsdsLiveWorldSubsystem::Get(World))
			{
				UE_LOG(LogOsdsLive, Display, TEXT("%s"), *Live->Report());
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GShot(
		TEXT("OSDS.Live.Shot"), TEXT("OSDS.Live.Shot <name> [fov]: save a frame from the camera toward the nearest live aircraft"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOsdsLiveWorldSubsystem* Live = UOsdsLiveWorldSubsystem::Get(World))
			{
				Live->Shot(Args.Num() > 0 ? Args[0] : TEXT("live-aircraft"), Args.Num() > 1 ? FCString::Atof(*Args[1]) : 20.0f);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GWet(
		TEXT("OSDS.Live.Wet"), TEXT("OSDS.Live.Wet <0..1 | -1>: force the road water (look tests); -1 follows the weather"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOsdsLiveWorldSubsystem* Live = UOsdsLiveWorldSubsystem::Get(World))
			{
				Live->WetOverride = Args.Num() > 0 ? FCString::Atof(*Args[0]) : -1.0f;
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GRefresh(
		TEXT("OSDS.Live.Refresh"), TEXT("OSDS.Live.Refresh: read every live feed again now"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UOsdsLiveWorldSubsystem* Live = UOsdsLiveWorldSubsystem::Get(World))
			{
				Live->RefreshAll();
			}
		}));
}

using namespace OsdsLiveWorldPrivate;

const TCHAR* UOsdsLiveWorldSubsystem::FeedName(EFeed Feed)
{
	switch (Feed)
	{
	case EFeed::Weather: return TEXT("weather");
	case EFeed::Metar: return TEXT("metar");
	case EFeed::Air: return TEXT("air");
	case EFeed::Sea: return TEXT("sea");
	case EFeed::TideStations: return TEXT("tide-stations");
	case EFeed::Tide: return TEXT("tide");
	case EFeed::Aircraft: return TEXT("aircraft");
	case EFeed::State: return TEXT("state");
	case EFeed::Roads: return TEXT("roads");
	case EFeed::Photos: return TEXT("photos");
	default: return TEXT("?");
	}
}

double UOsdsLiveWorldSubsystem::PeriodSeconds(EFeed Feed)
{
	switch (Feed)
	{
	case EFeed::Weather: return 600.0;
	case EFeed::Metar: return 600.0;
	case EFeed::Air: return 1800.0;
	case EFeed::Sea: return 1800.0;
	case EFeed::TideStations: return 30.0 * 86400.0;
	case EFeed::Tide: return 360.0;
	case EFeed::Aircraft: return 60.0;
	case EFeed::State: return 7.0 * 86400.0;
	case EFeed::Roads: return 7.0 * 86400.0;
	case EFeed::Photos: return 7.0 * 86400.0;
	default: return 600.0;
	}
}

bool UOsdsLiveWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer);
}

UOsdsLiveWorldSubsystem* UOsdsLiveWorldSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UOsdsLiveWorldSubsystem>() : nullptr;
}

TStatId UOsdsLiveWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOsdsLiveWorldSubsystem, STATGROUP_Tickables);
}

void UOsdsLiveWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	// The local AI detail engine loads before the first tile streams (it needs the game thread).
	if (InWorld.IsGameWorld() && !GIsAutomationTesting)
	{
		FOsdsNeuralDetail::Get().Initialize();
		// The scan engine's aerial eye (CPU, its own low-priority thread; off without its model file).
		FOsdsAerialEye::Get().Initialize();
		// A sharper photograph from the background worker replaces the one the tiles draw now.
		TWeakObjectPtr<UWorld> WeakWorld(&InWorld);
		FOsdsNeuralDetail::Get().OnEnhanced = [WeakWorld](const FString& Key, TArray<TArray<FColor>>& Mips, int32 Width, int32 Height)
		{
			UWorld* World = WeakWorld.Get();
			UOsdsWorldSubsystem* WorldSubsystem = World ? World->GetSubsystem<UOsdsWorldSubsystem>() : nullptr;
			AOsdsWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
			if (!Streamer || !Streamer->Resources)
			{
				return;
			}
			const double SwapStart = FPlatformTime::Seconds();
			UTexture2D* New = nullptr;
			UTexture2D* Old = Streamer->Resources->ReplaceAerialMips(Key, MoveTemp(Mips), Width, Height, New);
			if (!Old || !New)
			{
				return;
			}
			int32 Swapped = 0;
			TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Fading;
			for (TObjectIterator<UMaterialInstanceDynamic> It; It; ++It)
			{
				UTexture* Current = nullptr;
				if (It->GetWorld() == World && It->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Aerial")), Current) && Current == Old)
				{
					// The old photograph stays under the new one and fades out (AerialBlend in the ground shader).
					It->SetTextureParameterValue(TEXT("AerialPrev"), Old);
					It->SetTextureParameterValue(TEXT("Aerial"), New);
					It->SetScalarParameterValue(TEXT("AerialBlend"), 0.f);
					Fading.Add(*It);
					++Swapped;
				}
			}
			if (Fading.Num() > 0)
			{
				constexpr double FadeS = 1.5;
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Fading, New = TWeakObjectPtr<UTexture2D>(New), Start = FPlatformTime::Seconds()](float)
				{
					const double T = FMath::Clamp((FPlatformTime::Seconds() - Start) / FadeS, 0.0, 1.0);
					const float Blend = static_cast<float>(FMath::SmoothStep(0.0, 1.0, T));
					for (const TWeakObjectPtr<UMaterialInstanceDynamic>& M : Fading)
					{
						if (!M.IsValid()) continue;
						M->SetScalarParameterValue(TEXT("AerialBlend"), Blend);
						// Done: the old photograph is let go (the shader no longer samples it at 1).
						if (T >= 1.0 && New.IsValid()) M->SetTextureParameterValue(TEXT("AerialPrev"), New.Get());
					}
					return T < 1.0;
				}));
			}
			UE_LOG(LogOsdsLive, Display, TEXT("AI detail: %s now drawn at %dx%d on %d ground materials (%.1f ms on the game thread)"), *Key, Width, Height, Swapped,
				(FPlatformTime::Seconds() - SwapStart) * 1000.0);
		};
	}
	const bool bForce = FParse::Param(Cmd, TEXT("OsdsLive"));
	bEnabled = InWorld.IsGameWorld() && !FParse::Param(Cmd, TEXT("OsdsNoLive")) && !FParse::Param(Cmd, TEXT("OsdsNoEnvironment"))
		&& (bForce || (!FApp::IsUnattended() && !GIsAutomationTesting));
	if (!bEnabled)
	{
		return;
	}
	FString Ignored;
	float IgnoredFloat = 0.0f;
	bWeatherFromCommandLine = FParse::Value(Cmd, TEXT("OsdsConditions="), Ignored) || FParse::Value(Cmd, TEXT("OsdsCloud="), IgnoredFloat)
		|| FParse::Value(Cmd, TEXT("OsdsVisibilityKm="), IgnoredFloat) || FParse::Param(Cmd, TEXT("OsdsRain"));
	// -OsdsLiveWet=<0..1>: road water for look tests.
	FParse::Value(Cmd, TEXT("OsdsLiveWet="), WetOverride);
	// -OsdsLiveWind=<m/s>: wind in the palms for look tests.
	FParse::Value(Cmd, TEXT("OsdsLiveWind="), WindOverride);
	// -OsdsLiveShots=<s>: a telephoto shot toward the nearest aircraft every s seconds (play tests).
	if (FParse::Value(Cmd, TEXT("OsdsLiveShots="), ShotEverySeconds))
	{
		NextShotAt = ShotEverySeconds;
	}
	UE_LOG(LogOsdsLive, Display, TEXT("Live world on%s."), bWeatherFromCommandLine ? TEXT(" (weather chosen on the command line)") : TEXT(""));
}

void UOsdsLiveWorldSubsystem::Deinitialize()
{
	for (const FHttpRequestPtr& Request : Requests)
	{
		if (Request.IsValid())
		{
			Request->OnProcessRequestComplete().Unbind();
			Request->CancelRequest();
		}
	}
	Requests.Reset();
	Super::Deinitialize();
}

bool UOsdsLiveWorldSubsystem::ObserverLonLat(double& OutLat, double& OutLon) const
{
	const UWorld* World = GetWorld();
	const UOsdsWorldSubsystem* WorldSubsystem = World ? World->GetSubsystem<UOsdsWorldSubsystem>() : nullptr;
	const AOsdsWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
	if (Streamer && Streamer->GetFrame().IsValid())
	{
		FVector At = FVector::ZeroVector;
		if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
		{
			At = Pawn->GetActorLocation();
		}
		else if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(World, 0))
		{
			At = Camera->GetCameraLocation();
		}
		Streamer->GetFrame().WorldCmToLonLat(At, OutLon, OutLat);
		return true;
	}
	if (const UOsdsEnvironmentSubsystem* EnvSubsystem = UOsdsEnvironmentSubsystem::Get(World))
	{
		if (const AOsdsEnvironment* Env = EnvSubsystem->GetEnvironment())
		{
			OutLat = Env->LatitudeDeg;
			OutLon = Env->LongitudeDeg;
			return true;
		}
	}
	return false;
}

void UOsdsLiveWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bEnabled)
	{
		return;
	}
	Clock += DeltaTime;
	// Play tests (-OsdsLiveShots or -OsdsFrameCheck): the worst frame of each 30 s, to catch stalls.
	static const bool bFrameCheck = FParse::Param(FCommandLine::Get(), TEXT("OsdsFrameCheck"));
	if (ShotEverySeconds > 0.0f || bFrameCheck)
	{
		WorstFrameS = FMath::Max(WorstFrameS, static_cast<double>(DeltaTime));
		if (Clock >= NextFrameReportAt)
		{
			UE_LOG(LogOsdsLive, Display, TEXT("Live frame check: worst frame %.1f ms in the last 30 s"), WorstFrameS * 1000.0);
			WorstFrameS = 0.0;
			NextFrameReportAt = Clock + 30.0;
		}
	}
	if (ShotEverySeconds > 0.0f && Clock >= NextShotAt)
	{
		NextShotAt = Clock + ShotEverySeconds;
		Shot(FString::Printf(TEXT("live-aircraft-%d"), ++ShotCount), 20.0f);
		bViewShotTaken = false;
	}
	// The driver's own view halfway between aircraft shots: the weather, the haze, the road.
	if (ShotEverySeconds > 0.0f && !bViewShotTaken && Clock >= NextShotAt - ShotEverySeconds * 0.5)
	{
		bViewShotTaken = true;
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / FString::Printf(TEXT("live-view-%d.png"), ShotCount), false, false);
	}

	double Lat = 0.0, Lon = 0.0;
	if (!ObserverLonLat(Lat, Lon))
	{
		return;
	}

	UWorld* World = GetWorld();
	UOsdsWorldSubsystem* WorldSubsystem = World->GetSubsystem<UOsdsWorldSubsystem>();
	AOsdsWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
	if (Streamer && !Sky)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("OsdsLiveSky");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Sky = World->SpawnActor<AOsdsLiveSky>(AOsdsLiveSky::StaticClass(), FTransform::Identity, Params);
		if (Sky && !Aircraft.IsEmpty())
		{
			Sky->SetAircraft(Aircraft, Feeds[static_cast<int32>(EFeed::Aircraft)].LastOkUtc);
		}
		TWeakObjectPtr<UOsdsLiveWorldSubsystem> Weak(this);
		RebaseHandle = Streamer->OnRebased.AddLambda([Weak](const FVector& Offset)
		{
			if (Weak.IsValid() && Weak->Sky)
			{
				Weak->Sky->ShiftOrigin(Offset);
			}
		});
	}
	if (Sky)
	{
		if (const UOsdsEnvironmentSubsystem* EnvSubsystem = UOsdsEnvironmentSubsystem::Get(World))
		{
			if (const AOsdsEnvironment* Env = EnvSubsystem->GetEnvironment())
			{
				Sky->OriginElevationM = Env->ElevationM;
				Sky->SunElevationDeg = Env->GetSunPosition().ApparentElevationDeg;
			}
		}
	}

	// The first place: the saved answers for it (within 5 km) apply at once.
	if (!bHaveFeedPlace)
	{
		FeedLat = Lat;
		FeedLon = Lon;
		bHaveFeedPlace = true;
		LoadCache(Lat, Lon);
		ApplyWeather();
		UE_LOG(LogOsdsLive, Display, TEXT("%s"), *Report());
	}
	// A new place: the slow feeds read again for it.
	else if (FOsdsLiveData::DistanceKm(FeedLat, FeedLon, Lat, Lon) > MoveRefetchKm)
	{
		FeedLat = Lat;
		FeedLon = Lon;
		for (EFeed Feed : {EFeed::Weather, EFeed::Metar, EFeed::Air, EFeed::Sea, EFeed::Tide, EFeed::State, EFeed::Roads, EFeed::Photos})
		{
			Feeds[static_cast<int32>(Feed)].NextAt = 0.0;
		}
		StateCode.Reset();
		Roads.Reset();
		Photos.Reset();
		TideStation.Reset();
		if (const FOsdsTideStation* Near = FOsdsLiveData::NearestTideStation(TideStations, Lat, Lon))
		{
			TideStation = *Near;
		}
		Sea.bTideObserved = false;
	}

	TickPhotoDownloads();
	// -OsdsParseDir=<folder>: after 8 s, label up to 8 photographs there with the local scene
	// parser on a worker (look tests of the parser in a running game).
	if (!bParseTestStarted && Clock > 8.0)
	{
		bParseTestStarted = true;
		FString Dir;
		if (FParse::Value(FCommandLine::Get(), TEXT("OsdsParseDir="), Dir) && FOsdsSceneParser::Get().Initialize())
		{
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.jpg")), true, false);
			Files.SetNum(FMath::Min(Files.Num(), 8));
			Async(EAsyncExecution::Thread, [Dir, Files]()
			{
				for (const FString& F : Files)
				{
					FOsdsSceneParser::Get().ParseFileToPicture(Dir / F);
				}
			});
		}
	}
	ApplyRoadWater();

	for (int32 Index = 0; Index < static_cast<int32>(EFeed::Count); ++Index)
	{
		const EFeed Feed = static_cast<EFeed>(Index);
		FFeedState& State = Feeds[Index];
		if (State.bInFlight || Clock < State.NextAt)
		{
			continue;
		}
		switch (Feed)
		{
		case EFeed::Weather: Fetch(Feed, FOsdsLiveData::WeatherUrl(FeedLat, FeedLon)); break;
		case EFeed::Metar: Fetch(Feed, FOsdsLiveData::MetarUrl(FeedLat, FeedLon)); break;
		case EFeed::Air: Fetch(Feed, FOsdsLiveData::AirUrl(FeedLat, FeedLon)); break;
		case EFeed::Sea: Fetch(Feed, FOsdsLiveData::MarineUrl(FeedLat, FeedLon)); break;
		case EFeed::TideStations: Fetch(Feed, FOsdsLiveData::TideStationsUrl()); break;
		case EFeed::Tide:
			if (TideStation.IsSet())
			{
				Fetch(Feed, FOsdsLiveData::TideUrl(TideStation->Id));
			}
			else
			{
				State.NextAt = Clock + 30.0; // no gauge near (or the list is not read yet)
			}
			break;
		case EFeed::State: Fetch(Feed, FOsdsLiveData::StateUrl(FeedLat, FeedLon)); break;
		case EFeed::Roads:
			if (!StateCode.IsEmpty())
			{
				Fetch(Feed, FOsdsLiveData::RoadsUrl(StateCode, FeedLat, FeedLon));
			}
			else
			{
				State.NextAt = Clock + 20.0; // the state first (or not in the US)
			}
			break;
		case EFeed::Photos: Fetch(Feed, FOsdsLiveData::PhotosUrl(FeedLat, FeedLon)); break;
		case EFeed::Aircraft:
			// Only where the world is drawn (the aircraft need the map frame), and only with planes on.
			if (CVarFeaturePlanes.GetValueOnGameThread() == 0)
			{
				State.NextAt = Clock + 60.0;
			}
			else if (Sky)
			{
				Fetch(Feed, FOsdsLiveData::AircraftUrl(Lat, Lon, AircraftRadiusNm));
			}
			else
			{
				State.NextAt = Clock + 2.0;
			}
			break;
		default: break;
		}
	}
}

void UOsdsLiveWorldSubsystem::Shot(const FString& Name, float FovDeg)
{
	const FString What = Sky ? Sky->StartShot(Name, FovDeg) : FString();
	UE_LOG(LogOsdsLive, Display, TEXT("Live shot %s: %s"), *Name, What.IsEmpty() ? TEXT("no aircraft above 3 degrees") : *What);
}

FString UOsdsLiveWorldSubsystem::PhotoPath(const FOsdsPlacePhoto& Photo)
{
	return FPaths::ProjectSavedDir() / TEXT("Live") / TEXT("photos") / FString::Printf(TEXT("%08x.jpg"), GetTypeHash(Photo.Title));
}

void UOsdsLiveWorldSubsystem::TickPhotoDownloads()
{
	if (bPhotoInFlight || PhotoQueue.IsEmpty() || Clock < NextPhotoAt)
	{
		return;
	}
	const int32 Index = PhotoQueue[0];
	PhotoQueue.RemoveAt(0);
	if (!Photos.IsValidIndex(Index))
	{
		return;
	}
	if (!TakeRequestSlot(Photos[Index].ThumbUrl))
	{
		PhotoQueue.Insert(Index, 0);
		return;
	}
	bPhotoInFlight = true;
	NextPhotoAt = Clock + 1.5;
	const FString Title = Photos[Index].Title;
	const FString File = PhotoPath(Photos[Index]);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Photos[Index].ThumbUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), OsdsUserAgent::Get());
	Request->SetTimeout(30.0f);
	TWeakObjectPtr<UOsdsLiveWorldSubsystem> Weak(this);
	Request->OnProcessRequestComplete().BindLambda([Weak, Title, File](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bConnected)
	{
		UOsdsLiveWorldSubsystem* Self = Weak.Get();
		if (!Self)
		{
			return;
		}
		Self->Requests.Remove(Req);
		Self->InFlight = FMath::Max(0, Self->InFlight - 1);
		Self->bPhotoInFlight = false;
		if (!bConnected || !Response.IsValid() || Response->GetResponseCode() != 200 || Response->GetContent().Num() < 1000)
		{
			return;
		}
		FFileHelper::SaveArrayToFile(Response->GetContent(), *File);
		for (FOsdsPlacePhoto& Photo : Self->Photos)
		{
			if (Photo.Title == Title)
			{
				Photo.LocalFile = File;
			}
		}
	});
	Requests.Add(Request);
	Request->ProcessRequest();
}

void UOsdsLiveWorldSubsystem::RefreshAll()
{
	for (FFeedState& State : Feeds)
	{
		State.NextAt = 0.0;
	}
}

void UOsdsLiveWorldSubsystem::ApplyRoadWater()
{
	if (Clock < NextRoadWaterAt)
	{
		return;
	}
	NextRoadWaterAt = Clock + 3.0;
	const UOsdsEnvironmentSubsystem* EnvSubsystem = UOsdsEnvironmentSubsystem::Get(GetWorld());
	const AOsdsEnvironment* Env = EnvSubsystem ? EnvSubsystem->GetEnvironment() : nullptr;
	UOsdsWorldSubsystem* WorldSubsystem = GetWorld()->GetSubsystem<UOsdsWorldSubsystem>();
	AOsdsWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
	if (!Env || !Streamer || !Streamer->Resources)
	{
		return;
	}
	const float Water = WetOverride >= 0.0f ? WetOverride : Env->GetRoadWetness();
	const UMaterialInterface* Asphalt = Streamer->Resources->GetLayerMaterial(EOsdsTileLayer::Asphalt);
	const UMaterialInterface* SeaMaterial = Streamer->Resources->GetLayerMaterial(EOsdsTileLayer::Water);
	// The sea: the observed tide (else the modelled sea level) lifts the open water, and the
	// measured waves set the swell's slope and the way it runs (toward = from + 180 degrees;
	// the map has +X east and +Y south).
	const float SeaLevelCm = !Sea.bValid ? 0.0f : (Sea.bTideObserved ? Sea.TideM : Sea.SeaLevelM) * 100.0f;
	const float WaveScale = Sea.bValid ? FMath::Clamp(Sea.WaveHeightM / 0.3f, 0.3f, 5.0f) : 1.0f;
	const float Toward = FMath::DegreesToRadians(Sea.WaveFromDeg + 180.0f);
	const FLinearColor WaveDir(FMath::Sin(Toward), -FMath::Cos(Toward), 0.0f, 0.0f);
	for (TObjectIterator<UMaterialInstanceDynamic> It; It; ++It)
	{
		if (It->GetWorld() != GetWorld())
		{
			continue;
		}
		if (It->Parent == Asphalt)
		{
			It->SetScalarParameterValue(TEXT("Wet"), Water);
		}
		else if (It->Parent == SeaMaterial && Sea.bValid)
		{
			It->SetScalarParameterValue(TEXT("SeaLevelCm"), SeaLevelCm);
			It->SetScalarParameterValue(TEXT("WaveScale"), WaveScale);
			It->SetVectorParameterValue(TEXT("WaveDir"), WaveDir);
		}
	}
	AppliedRoadWater = Water;

	// The wind in the palms: the fronds of every palm (any mesh drawing the foliage layer
	// material) get one instance that carries the live wind, the way it blows (from + 180).
	UMaterialInterface* Foliage = Streamer->Resources->GetLayerMaterial(EOsdsTileLayer::Foliage);
	if (Foliage && !FrondMaterial)
	{
		FrondMaterial = UMaterialInstanceDynamic::Create(Foliage, this);
	}
	if (FrondMaterial)
	{
		const FOsdsWeatherConditions& Outside = Env->GetWeather();
		const float Downwind = FMath::DegreesToRadians(Outside.WindFromDeg + 180.0f);
		FrondMaterial->SetScalarParameterValue(TEXT("WindMs"), WindOverride >= 0.0f ? WindOverride : Outside.WindSpeedMs);
		FrondMaterial->SetVectorParameterValue(TEXT("WindDir"), FLinearColor(FMath::Sin(Downwind), -FMath::Cos(Downwind), 0.0f, 0.0f));
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			if (It->GetWorld() != GetWorld() || It->GetNumMaterials() < 2)
			{
				continue;
			}
			for (int32 Slot = 0; Slot < It->GetNumMaterials(); ++Slot)
			{
				if (It->GetMaterial(Slot) == Foliage)
				{
					It->SetMaterial(Slot, FrondMaterial);
				}
			}
		}
	}
}

bool UOsdsLiveWorldSubsystem::TakeRequestSlot(const FString& Url)
{
	FString Host = Url;
	Host.RemoveFromStart(TEXT("https://"));
	Host.RemoveFromStart(TEXT("http://"));
	Host = Host.Left(Host.Find(TEXT("/")));
	RecentRequests.RemoveAll([this](double At) { return Clock - At > 3600.0; });
	const double* Next = HostNextAt.Find(Host);
	if (InFlight >= 3 || RecentRequests.Num() >= 600 || (Next && Clock < *Next))
	{
		return false;
	}
	const double Spacing = Host.Contains(TEXT("wikimedia.org")) ? 1.5 : 1.0;
	HostNextAt.Add(Host, Clock + Spacing);
	RecentRequests.Add(Clock);
	++InFlight;
	return true;
}

void UOsdsLiveWorldSubsystem::Fetch(EFeed Feed, const FString& Url)
{
	FFeedState& State = Feeds[static_cast<int32>(Feed)];
	if (!TakeRequestSlot(Url))
	{
		return; // the budget is spent for now: the next tick asks again
	}
	State.bInFlight = true;
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	// -OsdsLiveOffline: every request goes to a closed local port, to test a start with no network.
	static const bool bOffline = FParse::Param(FCommandLine::Get(), TEXT("OsdsLiveOffline"));
	Request->SetURL(bOffline ? FString(TEXT("http://127.0.0.1:9/offline")) : Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), OsdsUserAgent::Get());
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetTimeout(Feed == EFeed::TideStations ? 60.0f : 20.0f);
	TWeakObjectPtr<UOsdsLiveWorldSubsystem> Weak(this);
	Request->OnProcessRequestComplete().BindLambda([Weak, Feed](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bConnected)
	{
		UOsdsLiveWorldSubsystem* Self = Weak.Get();
		if (!Self)
		{
			return;
		}
		Self->Requests.Remove(Req);
		Self->InFlight = FMath::Max(0, Self->InFlight - 1);
		FFeedState& S = Self->Feeds[static_cast<int32>(Feed)];
		S.bInFlight = false;
		if (!bConnected || !Response.IsValid() || Response->GetResponseCode() != 200)
		{
			S.LastError = Response.IsValid() ? FString::Printf(TEXT("HTTP %d"), Response->GetResponseCode()) : TEXT("no connection");
			S.Failures++;
			S.NextAt = Self->Clock + FMath::Min(PeriodSeconds(Feed), 15.0 * FMath::Pow(2.0, FMath::Min(S.Failures, 6)));
			if (S.Failures <= 2)
			{
				UE_LOG(LogOsdsLive, Display, TEXT("Live %s: %s"), FeedName(Feed), *S.LastError);
			}
			return;
		}
		// Parse (and save the cache) on a worker: a road inventory or the tide-station list is
		// megabytes of JSON, which would stall a frame. The game thread only applies the result.
		const FDateTime Received = FDateTime::UtcNow();
		S.NextAt = Self->Clock + 5.0; // not again while the answer is being parsed
		const FString Cache = Self->CachePath(Feed);
		const FString Meta = FString::Printf(TEXT("%.5f %.5f %lld"), Self->FeedLat, Self->FeedLon, Received.ToUnixTimestamp());
		TWeakObjectPtr<UOsdsLiveWorldSubsystem> WeakSelf(Self);
		Async(EAsyncExecution::ThreadPool, [WeakSelf, Feed, Body = Response->GetContentAsString(), Received, Cache, Meta]()
		{
			TSharedRef<FParsedFeed> Parsed = ParseFeed(Feed, Body);
			if (Parsed->bOk && Feed != EFeed::Aircraft)
			{
				FFileHelper::SaveStringToFile(Body, *Cache, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				FFileHelper::SaveStringToFile(Meta, *(Cache + TEXT(".meta")));
			}
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Feed, Parsed, Received]()
			{
				if (UOsdsLiveWorldSubsystem* Live = WeakSelf.Get())
				{
					Live->ApplyFeed(Feed, *Parsed, Received, false);
				}
			});
		});
	});
	Requests.Add(Request);
	Request->ProcessRequest();
}

void UOsdsLiveWorldSubsystem::OnFeed(EFeed Feed, const FString& Body, const FDateTime& ReceivedUtc, bool bFromCache)
{
	ApplyFeed(Feed, *ParseFeed(Feed, Body), ReceivedUtc, bFromCache);
}

TSharedRef<UOsdsLiveWorldSubsystem::FParsedFeed> UOsdsLiveWorldSubsystem::ParseFeed(EFeed Feed, const FString& Body)
{
	TSharedRef<FParsedFeed> P = MakeShared<FParsedFeed>();
	switch (Feed)
	{
	case EFeed::Weather: P->bOk = FOsdsLiveData::ParseWeather(Body, P->Weather, P->Error); break;
	case EFeed::Metar: P->bOk = FOsdsLiveData::ParseMetars(Body, P->Metars, P->Error); break;
	case EFeed::Air: P->bOk = FOsdsLiveData::ParseAir(Body, P->Air, P->Error); break;
	case EFeed::Sea: P->bOk = FOsdsLiveData::ParseMarine(Body, P->Sea, P->Error); break;
	case EFeed::TideStations: P->bOk = FOsdsLiveData::ParseTideStations(Body, P->TideStations, P->Error); break;
	case EFeed::Tide:
	{
		FDateTime When;
		P->bOk = FOsdsLiveData::ParseTide(Body, P->TideLevelM, When, P->Error);
		break;
	}
	case EFeed::State: P->bOk = FOsdsLiveData::ParseState(Body, P->StateCode, P->Error); break;
	case EFeed::Roads: P->bOk = FOsdsLiveData::ParseRoads(Body, P->Roads, P->Error); break;
	case EFeed::Photos:
		P->bOk = FOsdsLiveData::ParsePhotos(Body, P->Photos, P->Error);
		for (FOsdsPlacePhoto& Photo : P->Photos)
		{
			const FString File = PhotoPath(Photo);
			if (IFileManager::Get().FileExists(*File))
			{
				Photo.LocalFile = File;
			}
		}
		break;
	case EFeed::Aircraft: P->bOk = FOsdsLiveData::ParseAircraft(Body, P->Aircraft, P->Error); break;
	default: break;
	}
	return P;
}

void UOsdsLiveWorldSubsystem::ApplyFeed(EFeed Feed, FParsedFeed& P, const FDateTime& ReceivedUtc, bool bFromCache)
{
	FFeedState& State = Feeds[static_cast<int32>(Feed)];
	switch (Feed)
	{
	case EFeed::Weather:
		if (P.bOk) { Weather = P.Weather; }
		break;
	case EFeed::Metar:
		if (P.bOk) { Metars = MoveTemp(P.Metars); }
		break;
	case EFeed::Air:
		if (P.bOk) { Air = P.Air; }
		break;
	case EFeed::Sea:
		if (P.bOk)
		{
			// The modelled sea; an observed tide already read stays.
			P.Sea.bTideObserved = Sea.bTideObserved;
			P.Sea.TideM = Sea.TideM;
			P.Sea.TideStation = Sea.TideStation;
			Sea = P.Sea;
		}
		else if (!bFromCache)
		{
			Sea.bValid = false; // inland: no sea
		}
		break;
	case EFeed::TideStations:
		if (P.bOk)
		{
			TideStations = MoveTemp(P.TideStations);
			if (bHaveFeedPlace)
			{
				TideStation.Reset();
				if (const FOsdsTideStation* Near = FOsdsLiveData::NearestTideStation(TideStations, FeedLat, FeedLon))
				{
					TideStation = *Near;
					Feeds[static_cast<int32>(EFeed::Tide)].NextAt = 0.0;
				}
			}
		}
		break;
	case EFeed::Tide:
		if (P.bOk && TideStation.IsSet())
		{
			Sea.bTideObserved = true;
			Sea.TideM = P.TideLevelM;
			Sea.TideStation = TideStation->Name;
		}
		break;
	case EFeed::State:
		if (P.bOk)
		{
			StateCode = P.StateCode;
			Feeds[static_cast<int32>(EFeed::Roads)].NextAt = FMath::Min(Feeds[static_cast<int32>(EFeed::Roads)].NextAt, Clock);
		}
		else if (!bFromCache)
		{
			// Outside the US: no road inventory; ask again only after a move.
			StateCode.Reset();
			Feeds[static_cast<int32>(EFeed::Roads)].NextAt = Clock + PeriodSeconds(EFeed::Roads);
		}
		break;
	case EFeed::Roads:
		if (P.bOk)
		{
			Roads = MoveTemp(P.Roads);
			UE_LOG(LogOsdsLive, Display, TEXT("Live roads: %d counted road sections (FHWA HPMS %s)"), Roads.Num(), *StateCode);
		}
		break;
	case EFeed::Photos:
		if (P.bOk)
		{
			Photos = MoveTemp(P.Photos);
			// Keep up to 24 of them on this computer for the builders and the AI to look at.
			PhotoQueue.Reset();
			for (int32 Index = 0; Index < Photos.Num() && PhotoQueue.Num() < 24; ++Index)
			{
				if (Photos[Index].LocalFile.IsEmpty())
				{
					PhotoQueue.Add(Index);
				}
			}
			UE_LOG(LogOsdsLive, Display, TEXT("Live photos: %d freely licensed photographs within 600 m (Wikimedia Commons)"), Photos.Num());
		}
		break;
	case EFeed::Aircraft:
		if (P.bOk)
		{
			Aircraft = MoveTemp(P.Aircraft);
			if (Sky)
			{
				Sky->SetAircraft(Aircraft, ReceivedUtc);
			}
		}
		break;
	default:
		break;
	}

	if (!P.bOk)
	{
		State.LastError = P.Error;
		if (!bFromCache)
		{
			State.Failures++;
			State.NextAt = Clock + PeriodSeconds(Feed);
			UE_LOG(LogOsdsLive, Display, TEXT("Live %s: %s"), FeedName(Feed), *P.Error);
		}
		return;
	}
	State.LastOkUtc = ReceivedUtc;
	State.LastError.Reset();
	if (!bFromCache)
	{
		State.Failures = 0;
		State.NextAt = Clock + PeriodSeconds(Feed);
	}
	if (Feed == EFeed::Weather || Feed == EFeed::Metar || Feed == EFeed::Air)
	{
		ApplyWeather();
	}
	if (!bFromCache)
	{
		Announce();
	}
}

void UOsdsLiveWorldSubsystem::ApplyWeather()
{
	NearMetar.Reset();
	if (bHaveFeedPlace)
	{
		if (const FOsdsMetar* Near = FOsdsLiveData::NearestMetar(Metars, FeedLat, FeedLon, FDateTime::UtcNow()))
		{
			FOsdsMetar Copy = *Near;
			Copy.Km = static_cast<float>(FOsdsLiveData::DistanceKm(FeedLat, FeedLon, Copy.Lat, Copy.Lon));
			NearMetar = Copy;
		}
	}
	if (bWeatherFromCommandLine || (!Weather.bValid && !NearMetar.IsSet() && !Air.bValid))
	{
		return;
	}
	const UOsdsEnvironmentSubsystem* EnvSubsystem = UOsdsEnvironmentSubsystem::Get(GetWorld());
	AOsdsEnvironment* Env = EnvSubsystem ? EnvSubsystem->GetEnvironment() : nullptr;
	if (!Env)
	{
		return;
	}
	const FOsdsWeatherConditions Fused = FOsdsLiveData::Fuse(Weather.bValid ? &Weather : nullptr, NearMetar.GetPtrOrNull(),
		Air.bValid ? &Air : nullptr, Env->GetWeather());
	Env->SetWeather(Fused);
}

void UOsdsLiveWorldSubsystem::Announce()
{
	// Once the weather is in, tell the driver what the real place is like now.
	if (bAnnounced || !Weather.bValid)
	{
		return;
	}
	if (APlayerController* Controller = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (AOsdsHUD* Hud = Cast<AOsdsHUD>(Controller->GetHUD()))
		{
			bAnnounced = true;
			Hud->ShowMessage(TEXT("Live now: ") + FOsdsLiveData::Summary(&Weather, NearMetar.GetPtrOrNull(), Air.bValid ? &Air : nullptr,
				Sea.bValid ? &Sea : nullptr, Sky ? Sky->NumAircraft() : 0), 10.0f);
		}
	}
	UE_LOG(LogOsdsLive, Display, TEXT("%s"), *Report());
}

FString UOsdsLiveWorldSubsystem::CachePath(EFeed Feed) const
{
	return FPaths::ProjectSavedDir() / TEXT("Live") / (FString(FeedName(Feed)) + TEXT(".json"));
}

void UOsdsLiveWorldSubsystem::LoadCache(double Lat, double Lon)
{
	const FDateTime Now = FDateTime::UtcNow();
	for (EFeed Feed : {EFeed::TideStations, EFeed::Weather, EFeed::Metar, EFeed::Air, EFeed::Sea, EFeed::State, EFeed::Roads, EFeed::Photos})
	{
		FString Body, Meta;
		if (!FFileHelper::LoadFileToString(Body, *CachePath(Feed)) || !FFileHelper::LoadFileToString(Meta, *(CachePath(Feed) + TEXT(".meta"))))
		{
			continue;
		}
		TArray<FString> Parts;
		Meta.ParseIntoArrayWS(Parts);
		if (Parts.Num() < 3)
		{
			continue;
		}
		const FDateTime Saved = FDateTime::FromUnixTimestamp(FCString::Atoi64(*Parts[2]));
		const double AgeHours = (Now - Saved).GetTotalHours();
		// Old weather is no longer the real sky; the station list keeps for a month.
		const double MaxHours = Feed == EFeed::TideStations ? 24.0 * 30.0 : (Feed == EFeed::State || Feed == EFeed::Roads || Feed == EFeed::Photos) ? 24.0 * 7.0 : 6.0;
		// Saved data belongs to its place: another place's sky is not this one's.
		const bool bHere = Feed == EFeed::TideStations
			|| FOsdsLiveData::DistanceKm(Lat, Lon, FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1])) <= MoveRefetchKm;
		if (AgeHours < 0.0 || AgeHours > MaxHours || !bHere)
		{
			continue;
		}
		OnFeed(Feed, Body, Saved, true);
		// Fresh enough data waits for its own period; older data is read again at once.
		Feeds[static_cast<int32>(Feed)].NextAt = FMath::Max(0.0, Clock + PeriodSeconds(Feed) - AgeHours * 3600.0);
	}
	TideStation.Reset();
	if (const FOsdsTideStation* Near = FOsdsLiveData::NearestTideStation(TideStations, Lat, Lon))
	{
		TideStation = *Near;
	}
}

FDateTime UOsdsLiveWorldSubsystem::LocalNow() const
{
	FDateTime Utc = FDateTime::UtcNow();
	if (const UOsdsEnvironmentSubsystem* EnvSubsystem = UOsdsEnvironmentSubsystem::Get(GetWorld()))
	{
		if (const AOsdsEnvironment* Env = EnvSubsystem->GetEnvironment())
		{
			Utc = Env->GetUtcTime();
		}
	}
	// The place's own offset (Open-Meteo, time zone and daylight saving), else solar time.
	const double Offset = Weather.bValid ? Weather.UtcOffsetSeconds : FeedLon / 15.0 * 3600.0;
	return Utc + FTimespan::FromSeconds(Offset);
}

bool UOsdsLiveWorldSubsystem::TrafficAt(const FVector& WorldCm, float& OutPerKmLane, float& OutPerHour, FString* OutRoad) const
{
	const UWorld* World = GetWorld();
	const UOsdsWorldSubsystem* WorldSubsystem = World ? World->GetSubsystem<UOsdsWorldSubsystem>() : nullptr;
	const AOsdsWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
	if (Roads.IsEmpty() || !Streamer || !Streamer->GetFrame().IsValid())
	{
		return false;
	}
	double Lat = 0.0, Lon = 0.0, Metres = 0.0;
	Streamer->GetFrame().WorldCmToLonLat(WorldCm, Lon, Lat);
	const FOsdsRoadSection* Road = FOsdsLiveData::NearestRoad(Roads, Lat, Lon, 40.0, Metres);
	if (!Road)
	{
		return false;
	}
	const UOsdsEnvironmentSubsystem* EnvSubsystem = UOsdsEnvironmentSubsystem::Get(World);
	const AOsdsEnvironment* Env = EnvSubsystem ? EnvSubsystem->GetEnvironment() : nullptr;
	const bool bRain = Env && Env->GetWeather().bRaining;
	const bool bSnow = Env && Env->GetWeather().bSnowing;
	OutPerHour = FOsdsLiveData::FlowPerHour(*Road, LocalNow(), bRain, bSnow);
	OutPerKmLane = FOsdsLiveData::DensityPerKmLane(*Road, OutPerHour);
	if (OutRoad)
	{
		*OutRoad = FString::Printf(TEXT("%s (%d a day, %d lanes)"), Road->Name.IsEmpty() ? *Road->RouteId : *Road->Name, Road->Aadt, Road->Lanes);
	}
	return true;
}

FString UOsdsLiveWorldSubsystem::Report() const
{
	FString Out = FString::Printf(TEXT("Live world at %.4f,%.4f: %s"), FeedLat, FeedLon,
		*FOsdsLiveData::Summary(Weather.bValid ? &Weather : nullptr, NearMetar.GetPtrOrNull(), Air.bValid ? &Air : nullptr,
			Sea.bValid ? &Sea : nullptr, Sky ? Sky->NumAircraft() : 0));
	float PerKmLane = 0.0f, PerHour = 0.0f;
	FString Road;
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0); Pawn && TrafficAt(Pawn->GetActorLocation(), PerKmLane, PerHour, &Road))
	{
		Out += FString::Printf(TEXT("\n  traffic here at %s local: %.0f vehicles an hour, %.1f per km per lane on %s"),
			*LocalNow().ToString(TEXT("%Y-%m-%d %H:%M")), PerHour, PerKmLane, *Road);
	}
	Out += FString::Printf(TEXT("\n  %d counted road sections, %d photographs of the place"), Roads.Num(), Photos.Num());
	const FDateTime Now = FDateTime::UtcNow();
	for (int32 Index = 0; Index < static_cast<int32>(EFeed::Count); ++Index)
	{
		const FFeedState& State = Feeds[Index];
		Out += FString::Printf(TEXT("\n  %-13s %s%s"), FeedName(static_cast<EFeed>(Index)),
			State.LastOkUtc.GetTicks() ? *FString::Printf(TEXT("ok %.0f s ago"), (Now - State.LastOkUtc).GetTotalSeconds()) : TEXT("not read"),
			State.LastError.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", last error: %s"), *State.LastError));
	}
	return Out;
}
