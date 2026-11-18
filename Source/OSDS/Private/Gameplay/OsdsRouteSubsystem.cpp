// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsRouteSubsystem.h"

#include "Async/Async.h"
#include "Atlas/OsdsAtlasClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/OsdsGameMode.h"
#include "HAL/PlatformTime.h"
#include "World/OsdsWorldStreamer.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogOsdsRoute, Log, All);

struct UOsdsRouteSubsystem::FJob
{
	std::atomic<bool> bDone{ false };
	std::atomic<bool> bCancel{ false };
	bool bReroute = false;
	TArray<FOsdsRouteStop> Stops;
	FOsdsAtlasResult Result;
};

namespace
{
	const FOsdsAtlasConfig& RouteHelperConfig()
	{
		static const FOsdsAtlasConfig Config = FOsdsAtlasConfig::ResolveDefault();
		return Config;
	}
}

UOsdsRouteSubsystem* UOsdsRouteSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UOsdsRouteSubsystem>() : nullptr;
}

bool UOsdsRouteSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UOsdsRouteSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOsdsRouteSubsystem, STATGROUP_Tickables);
}

void UOsdsRouteSubsystem::Deinitialize()
{
	if (Job.IsValid()) Job->bCancel = true;
	if (PreviewJob.IsValid()) PreviewJob->bCancel = true;
	Job.Reset();
	PreviewJob.Reset();
	Super::Deinitialize();
}

bool UOsdsRouteSubsystem::CurrentFrame(FOsdsWorldFrame& Out) const
{
	const AOsdsGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AOsdsGameMode>() : nullptr;
	const AOsdsWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
	if (!Stream || !Stream->GetFrame().IsValid()) return false;
	Out = Stream->GetFrame();
	return true;
}

bool UOsdsRouteSubsystem::GetCarM(FVector2D& OutM, double* OutYawDeg) const
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Car = Controller ? Controller->GetPawn() : nullptr;
	if (!Car) return false;
	OutM = FVector2D(Car->GetActorLocation().X, Car->GetActorLocation().Y) / 100.0;
	if (OutYawDeg) *OutYawDeg = Car->GetActorRotation().Yaw;
	return true;
}

bool UOsdsRouteSubsystem::StartStops(const TArray<FOsdsRouteStop>& Stops, TArray<FOsdsRouteStop>& OutWithStart)
{
	FOsdsWorldFrame Now;
	FVector2D CarM;
	if (!CurrentFrame(Now) || !GetCarM(CarM)) return false;
	FOsdsRouteStop Start;
	Start.Title = TEXT("You are here");
	Now.WorldCmToLonLat(FVector(CarM * 100.0, 0.0), Start.Lon, Start.Lat);
	OutWithStart.Reset();
	OutWithStart.Add(Start);
	OutWithStart.Append(Stops);
	return true;
}

void UOsdsRouteSubsystem::StartRoute(const TArray<FOsdsRouteStop>& Stops)
{
	bPendingAfterTravel = false;
	TArray<FOsdsRouteStop> All;
	if (!StartStops(Stops, All))
	{
		Status = TEXT("The route can start when the car is on the road");
		StatusTime = 4.0;
		return;
	}
	Launch(All, false);
}

void UOsdsRouteSubsystem::RequestPreview(const TArray<FOsdsRouteStop>& Stops)
{
	if (PreviewJob.IsValid()) PreviewJob->bCancel = true;
	PreviewJob.Reset();
	Preview.Clear();
	++PreviewSerial;
	TArray<FOsdsRouteStop> All;
	if (Stops.Num() == 0 || !StartStops(Stops, All)) return;
	PreviewJob = RunJob(All);
}

void UOsdsRouteSubsystem::StartRouteAfterTravel(const TArray<FOsdsRouteStop>& Stops)
{
	ClearRoute();
	PendingStops = Stops;
	bPendingAfterTravel = true;
	PendingSince = FPlatformTime::Seconds();
	Status = TEXT("The route starts when the road is ready");
	StatusTime = 1e9;
}

void UOsdsRouteSubsystem::ClearRoute()
{
	if (Job.IsValid()) Job->bCancel = true;
	Job.Reset();
	Route.Clear();
	bPendingAfterTravel = false;
	Status.Reset();
}

void UOsdsRouteSubsystem::Launch(const TArray<FOsdsRouteStop>& StopsWithStart, bool bReroute)
{
	if (StopsWithStart.Num() < 2)
	{
		Status = TEXT("Add a stop to make a route");
		StatusTime = 3.0;
		return;
	}
	if (Job.IsValid()) Job->bCancel = true;
	Job = RunJob(StopsWithStart);
	if (!Job.IsValid())
	{
		Status = TEXT("The map helper is not installed, so there is no route");
		StatusTime = 5.0;
		return;
	}
	Job->bReroute = bReroute;
	Status = bReroute ? TEXT("Finding a new route") : TEXT("Finding the route");
	StatusTime = 1e9;
}

TSharedPtr<UOsdsRouteSubsystem::FJob, ESPMode::ThreadSafe> UOsdsRouteSubsystem::RunJob(const TArray<FOsdsRouteStop>& StopsWithStart)
{
	const FOsdsAtlasConfig& Config = RouteHelperConfig();
	if (!Config.IsConfigured()) return nullptr;
	TSharedPtr<FJob, ESPMode::ThreadSafe> NewJob = MakeShared<FJob, ESPMode::ThreadSafe>();
	NewJob->Stops = StopsWithStart;
	FOsdsAtlasRequest Request;
	Request.Command = TEXT("route");
	for (const FOsdsRouteStop& Stop : StopsWithStart)
	{
		Request.Args.Add(FString::Printf(TEXT("%.6f,%.6f"), Stop.Lon, Stop.Lat));
	}
	Request.TimeoutSeconds = 60.0;
	UE_LOG(LogOsdsRoute, Display, TEXT("Route request: %s"), *FString::Join(Request.Args, TEXT(" ")));
	Async(EAsyncExecution::ThreadPool, [NewJob, Config, Request]()
	{
		NewJob->Result = FOsdsAtlasClient::RunBlocking(Config, Request, &NewJob->bCancel);
		NewJob->bDone = true;
	});
	return NewJob;
}

bool UOsdsRouteSubsystem::AcceptHelperAnswer(const FString& Raw, const TArray<FOsdsRouteStop>& StopsWithStart)
{
	FString Error;
	if (!Route.ParseHelperString(Raw, StopsWithStart, &Error))
	{
		Status = Error;
		StatusTime = 5.0;
		return false;
	}
	ProjectRoute();
	RouteAge = 0.0;
	Status.Reset();
	return true;
}

void UOsdsRouteSubsystem::CollectJob()
{
	if (PreviewJob.IsValid() && PreviewJob->bDone)
	{
		const TSharedPtr<FJob, ESPMode::ThreadSafe> Done = PreviewJob;
		PreviewJob.Reset();
		FString Error;
		if (!Done->bCancel && Done->Result.IsOk() && Preview.ParseHelperString(Done->Result.Stdout, Done->Stops, &Error))
		{
			FOsdsWorldFrame Now;
			if (CurrentFrame(Now))
			{
				Preview.Project(FOsdsLonLatToM::CreateLambda([Now](double Lon, double Lat)
				{
					const FVector Cm = Now.LonLatToWorldCm(Lon, Lat);
					return FVector2D(Cm.X, Cm.Y) / 100.0;
				}));
			}
		}
		++PreviewSerial;
	}
	if (!Job.IsValid() || !Job->bDone) return;
	const TSharedPtr<FJob, ESPMode::ThreadSafe> Done = Job;
	Job.Reset();
	if (Done->bCancel) return;
	if (!Done->Result.IsOk())
	{
		UE_LOG(LogOsdsRoute, Warning, TEXT("Route request failed: %s"), *Done->Result.Describe());
		Status = TEXT("The route could not be found. Try again or move a stop.");
		StatusTime = 5.0;
		// A failed new route keeps the old one on screen.
		RerouteBlockedUntil = FPlatformTime::Seconds() + 15.0;
		return;
	}
	if (AcceptHelperAnswer(Done->Result.Stdout, Done->Stops))
	{
		UE_LOG(LogOsdsRoute, Display, TEXT("Route: %.0f m, %.0f s, %d legs in %.1f s"), Route.TotalLengthM(), Route.TotalDurationS(), Route.GetLegs().Num(), Done->Result.Seconds);
	}
	else
	{
		UE_LOG(LogOsdsRoute, Warning, TEXT("Route refused: %s"), *Status);
		RerouteBlockedUntil = FPlatformTime::Seconds() + 15.0;
	}
}

void UOsdsRouteSubsystem::ProjectRoute()
{
	if (!CurrentFrame(Frame)) return;
	const FOsdsWorldFrame Use = Frame;
	Route.Project(FOsdsLonLatToM::CreateLambda([Use](double Lon, double Lat)
	{
		const FVector Cm = Use.LonLatToWorldCm(Lon, Lat);
		return FVector2D(Cm.X, Cm.Y) / 100.0;
	}));
}

void UOsdsRouteSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	CollectJob();
	if (StatusTime < 1e8)
	{
		StatusTime -= DeltaTime;
		if (StatusTime <= 0.0) Status.Reset();
	}
	if (GetWorld() && GetWorld()->IsPaused()) return;
	if (bPendingAfterTravel)
	{
		const AOsdsGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AOsdsGameMode>() : nullptr;
		FVector2D CarM;
		if (Mode && Mode->IsDriveReady() && GetCarM(CarM) && FPlatformTime::Seconds() - PendingSince > 1.0)
		{
			const TArray<FOsdsRouteStop> Stops = PendingStops;
			StartRoute(Stops);
		}
		return;
	}
	// The world origin moves by whole tiles as the car drives: project again.
	FOsdsWorldFrame Now;
	if (Route.IsActive() && CurrentFrame(Now) && Now.OriginTile != Frame.OriginTile)
	{
		ProjectRoute();
	}
	FVector2D CarM;
	if (!GetCarM(CarM)) return;
	if (Route.IsActive() || !Route.GetArrivalMessage().IsEmpty())
	{
		RouteAge += DeltaTime;
		Route.Update(CarM, DeltaTime);
	}
	if (Route.IsActive() && Route.GetProgress().bOffRoute && !Job.IsValid() && FPlatformTime::Seconds() > RerouteBlockedUntil)
	{
		Route.ResetOffRoute();
		TArray<FOsdsRouteStop> Stops;
		FOsdsRouteStop Start;
		Start.Title = TEXT("You are here");
		Frame.WorldCmToLonLat(FVector(CarM * 100.0, 0.0), Start.Lon, Start.Lat);
		Stops.Add(Start);
		Stops.Append(Route.RemainingStops());
		UE_LOG(LogOsdsRoute, Display, TEXT("Off the route: a new route from the car"));
		Launch(Stops, true);
		RerouteBlockedUntil = FPlatformTime::Seconds() + 10.0;
	}
}
