// Copyright OSDS contributors. All Rights Reserved.

#include "Environment/OsdsEnvironmentSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/OsdsEnvironment.h"
#include "Environment/OsdsGraphics.h"
#include "Environment/OsdsLightingLab.h"
#include "Environment/OsdsPerfSampler.h"
#include "Engine/Level.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsEnvironmentSubsystem, Log, All);

namespace
{
	AOsdsEnvironment* FindEnvironment(UWorld* World)
	{
		if (UOsdsEnvironmentSubsystem* Subsystem = UOsdsEnvironmentSubsystem::Get(World))
		{
			return Subsystem->GetEnvironment();
		}
		return nullptr;
	}

	void ApplyCommandLine(AOsdsEnvironment& Env)
	{
		const TCHAR* Cmd = FCommandLine::Get();
		double Latitude = Env.LatitudeDeg;
		double Longitude = Env.LongitudeDeg;
		double Elevation = Env.ElevationM;
		const bool bPlace = FParse::Value(Cmd, TEXT("OsdsLat="), Latitude) | FParse::Value(Cmd, TEXT("OsdsLon="), Longitude)
			| FParse::Value(Cmd, TEXT("OsdsElevation="), Elevation);
		if (bPlace)
		{
			Env.SetPlace(Latitude, Longitude, Elevation);
		}

		FString Iso;
		FDateTime Utc;
		if (FParse::Value(Cmd, TEXT("OsdsUtc="), Iso, false) && FDateTime::ParseIso8601(*Iso, Utc))
		{
			Env.SetUtcTime(Utc);
		}
		else if (FParse::Param(Cmd, TEXT("OsdsRealTime")))
		{
			Env.FollowSystemClock();
		}
		else
		{
			// Owner decision (11 Nov 2026): a drive starts in daylight, the real clock is an option.
			Env.StartInDaylight();
		}
		float TimeScale = 1.0f;
		if (FParse::Value(Cmd, TEXT("OsdsTimeScale="), TimeScale))
		{
			Env.bFollowSystemClock = false;
			Env.TimeScale = TimeScale;
		}

		FOsdsWeatherConditions Weather = Env.GetWeather();
		bool bWeather = false;
		FString ConditionsPath;
		if (FParse::Value(Cmd, TEXT("OsdsConditions="), ConditionsPath, false))
		{
			FString Json;
			FString Error;
			if (FFileHelper::LoadFileToString(Json, *ConditionsPath) && FOsdsWeather::ParseConditionsJson(Json, Weather, Error))
			{
				bWeather = true;
			}
			else
			{
				UE_LOG(LogOsdsEnvironmentSubsystem, Warning, TEXT("Could not read conditions %s: %s"), *ConditionsPath, *Error);
			}
		}
		float Cloud = 0.0f;
		if (FParse::Value(Cmd, TEXT("OsdsCloud="), Cloud))
		{
			Weather.CloudCover = Cloud;
			bWeather = true;
		}
		float VisibilityKm = 0.0f;
		if (FParse::Value(Cmd, TEXT("OsdsVisibilityKm="), VisibilityKm))
		{
			Weather.VisibilityM = VisibilityKm * 1000.0f;
			bWeather = true;
		}
		if (FParse::Param(Cmd, TEXT("OsdsRain")))
		{
			Weather.bRaining = true;
			bWeather = true;
		}
		if (bWeather)
		{
			Env.SetWeather(Weather);
		}
	}

	void ApplyGraphicsFromCommandLine()
	{
		// Once per process: the command line's preset, else the auto-pick for this GPU.
		// "-OsdsGraphics=keep" leaves Unreal's saved scalability alone.
		static bool bApplied = false;
		if (bApplied)
		{
			return;
		}
		bApplied = true;
		FString Choice = TEXT("auto");
		const bool bExplicit = FParse::Value(FCommandLine::Get(), TEXT("OsdsGraphics="), Choice);
		if (Choice.Equals(TEXT("keep"), ESearchCase::IgnoreCase))
		{
			return;
		}
		// Nothing to pick for without a renderer (-nullrhi tests, servers), and Play-In-Editor
		// shares the editor's own scalability: there only an explicit preset is applied.
		if (!bExplicit && (!FApp::CanEverRender() || GIsEditor))
		{
			return;
		}
		if (!bExplicit)
		{
			Choice = FOsdsGraphics::ChoiceName(FOsdsGraphics::SavedChoice());
		}
		EOsdsGraphicsPreset Preset = EOsdsGraphicsPreset::High;
		if (Choice.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
		{
			Preset = FOsdsGraphics::AutoPickForThisMachine(true);
		}
		else if (!FOsdsGraphics::Parse(Choice, Preset))
		{
			UE_LOG(LogOsdsEnvironmentSubsystem, Warning, TEXT("Unknown graphics preset '%s'"), *Choice);
			return;
		}
		FOsdsGraphics::Apply(Preset);
	}

	FAutoConsoleCommandWithWorldAndArgs GTimeCommand(
		TEXT("OSDS.Env.Time"),
		TEXT("OSDS.Env.Time <ISO-8601 UTC, e.g. 2025-04-24T16:20:00Z> [timescale] | now: set the sky clock"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AOsdsEnvironment* Env = FindEnvironment(World);
			if (!Env || Args.IsEmpty())
			{
				return;
			}
			FDateTime Utc;
			if (Args[0].Equals(TEXT("now"), ESearchCase::IgnoreCase))
			{
				Env->FollowSystemClock();
			}
			else if (FDateTime::ParseIso8601(*Args[0], Utc))
			{
				Env->SetUtcTime(Utc);
				Env->TimeScale = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.0f;
			}
			UE_LOG(LogOsdsEnvironmentSubsystem, Display, TEXT("%s"), *Env->Describe());
		}));

	FAutoConsoleCommandWithWorldAndArgs GPlaceCommand(
		TEXT("OSDS.Env.Place"),
		TEXT("OSDS.Env.Place <latitude> <longitude> [elevation m]: move the observer"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AOsdsEnvironment* Env = FindEnvironment(World);
			if (!Env || Args.Num() < 2)
			{
				return;
			}
			Env->SetPlace(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), Args.Num() > 2 ? FCString::Atod(*Args[2]) : 0.0);
			UE_LOG(LogOsdsEnvironmentSubsystem, Display, TEXT("%s"), *Env->Describe());
		}));

	FAutoConsoleCommandWithWorldAndArgs GWeatherCommand(
		TEXT("OSDS.Env.Weather"),
		TEXT("OSDS.Env.Weather <cloud 0-1> [visibility km] [rain 0/1]: set the weather"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AOsdsEnvironment* Env = FindEnvironment(World);
			if (!Env || Args.IsEmpty())
			{
				return;
			}
			FOsdsWeatherConditions Weather = Env->GetWeather();
			Weather.CloudCover = FCString::Atof(*Args[0]);
			if (Args.Num() > 1)
			{
				Weather.VisibilityM = FCString::Atof(*Args[1]) * 1000.0f;
			}
			if (Args.Num() > 2)
			{
				Weather.bRaining = FCString::Atoi(*Args[2]) != 0;
			}
			Env->SetWeather(Weather);
			UE_LOG(LogOsdsEnvironmentSubsystem, Display, TEXT("%s"), *Env->Describe());
		}));

	FAutoConsoleCommandWithWorldAndArgs GLookCommand(
		TEXT("OSDS.Env.Look"),
		TEXT("OSDS.Env.Look <name> <value>: look development. Names: a cloud material scalar (Cloud_GlobalCoverage, Cloud_GlobalDensity), LayerBottomKm, LayerHeightKm, AerosolScaleHeightKm, AerosolAlbedo, MieAnisotropy, MultiScattering, ExposureCompensation, OpenStops, DaylightOpenStops, DriverDaylightOpenStops, LowPercent, CloseStops, Saturation"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AOsdsEnvironment* Env = FindEnvironment(World); Env && Args.Num() >= 2)
			{
				Env->SetLookParameter(FName(*Args[0]), FCString::Atof(*Args[1]));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GReportCommand(
		TEXT("OSDS.Env.Report"),
		TEXT("OSDS.Env.Report: log place, time, sun, moon, exposure and weather"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (AOsdsEnvironment* Env = FindEnvironment(World))
			{
				UE_LOG(LogOsdsEnvironmentSubsystem, Display, TEXT("%s"), *Env->Describe());
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GGraphicsCommand(
		TEXT("OSDS.Graphics"),
		TEXT("OSDS.Graphics <auto|low|medium|high|ultra>: apply a graphics preset"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			EOsdsGraphicsPreset Preset = EOsdsGraphicsPreset::High;
			if (Args.IsEmpty() || Args[0].Equals(TEXT("auto"), ESearchCase::IgnoreCase))
			{
				Preset = FOsdsGraphics::AutoPickForThisMachine(true);
			}
			else if (!FOsdsGraphics::Parse(Args[0], Preset))
			{
				return;
			}
			FOsdsGraphics::Apply(Preset);
		}));

	FAutoConsoleCommandWithWorldAndArgs GPerfCommand(
		TEXT("OSDS.Perf"),
		TEXT("OSDS.Perf <seconds> [label]: log frame, GPU, game and render thread times"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const float Seconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 10.0f;
			FOsdsPerfSampler::Start(Args.Num() > 1 ? Args[1] : FOsdsGraphics::Name(FOsdsGraphics::Current()), Seconds);
		}));

	FAutoConsoleCommandWithWorldAndArgs GCaptureCommand(
		TEXT("OSDS.Capture"),
		TEXT("OSDS.Capture <name>: save the frame (no UI) to Saved/Screenshots/<name>.png"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const FString Name = Args.Num() > 0 ? Args[0] : TEXT("capture");
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / (Name + TEXT(".png")), false, false);
		}));
}

bool UOsdsEnvironmentSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer);
}

void UOsdsEnvironmentSubsystem::Deinitialize()
{
	if (LevelAddedHandle.IsValid())
	{
		FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
		LevelAddedHandle.Reset();
	}
	Super::Deinitialize();
}

void UOsdsEnvironmentSubsystem::HandleLevelAdded(ULevel* Level, UWorld* World)
{
	if (Level && World == GetWorld() && IsValid(Environment))
	{
		Environment->TakeOverLevelLighting(Level);
	}
}

UOsdsEnvironmentSubsystem* UOsdsEnvironmentSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UOsdsEnvironmentSubsystem>() : nullptr;
}

void UOsdsEnvironmentSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld())
	{
		return;
	}
	const TCHAR* Cmd = FCommandLine::Get();
	ApplyGraphicsFromCommandLine();
	if (FParse::Param(Cmd, TEXT("OsdsNoEnvironment")))
	{
		return;
	}

	for (TActorIterator<AOsdsEnvironment> It(&InWorld); It; ++It)
	{
		Environment = *It;
		break;
	}
	if (!Environment)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("OsdsEnvironment");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Environment = InWorld.SpawnActor<AOsdsEnvironment>(AOsdsEnvironment::StaticClass(), FTransform::Identity, Params);
	}
	if (!Environment)
	{
		return;
	}
	Environment->TakeOverLevelLighting();
	if (!LevelAddedHandle.IsValid())
	{
		LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UOsdsEnvironmentSubsystem::HandleLevelAdded);
	}
	ApplyCommandLine(*Environment);
	UE_LOG(LogOsdsEnvironmentSubsystem, Display, TEXT("OSDS environment: %s"), *Environment->Describe());

	if (FParse::Param(Cmd, TEXT("OsdsLab")))
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("OsdsLightingLab");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Lab = InWorld.SpawnActor<AOsdsLightingLab>(AOsdsLightingLab::StaticClass(), FTransform::Identity, Params);
	}
}
