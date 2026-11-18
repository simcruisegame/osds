// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsSoak.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/OsdsGameMode.h"
#include "Gameplay/OsdsPlaces.h"
#include "Gameplay/OsdsPlayerController.h"
#include "HAL/PlatformMemory.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsSoak, Log, All);

namespace
{
	/** Shipping builds write no log: the result lines also go to Saved/Profiling/OsdsSoak.log. */
	void Record(const FString& Line)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"), TEXT("OsdsSoak.log"));
		FFileHelper::SaveStringToFile(FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S ")) + Line + LINE_TERMINATOR, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
}

AOsdsSoak::AOsdsSoak()
{
	PrimaryActorTick.bCanEverTick = true;
	// Before the player controller reads its input this frame.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AOsdsPlayerController* AOsdsSoak::GetPlayer() const
{
	return Cast<AOsdsPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
}

void AOsdsSoak::BeginPlay()
{
	Super::BeginPlay();
	FParse::Value(FCommandLine::Get(), TEXT("OsdsSoak="), Minutes);
	Minutes = FMath::Clamp(Minutes, 0.5, 600.0);
	bTravel = !FParse::Param(FCommandLine::Get(), TEXT("OsdsSoakNoTravel"));
	Started = FPlatformTime::Seconds();
	{ const FString RecordLine = FString::Printf(TEXT("SOAK start: %.1f minutes, travel %d"), Minutes, bTravel ? 1 : 0); UE_LOG(LogOsdsSoak, Display, TEXT("%s"), *RecordLine); Record(RecordLine); }
}

void AOsdsSoak::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDone)
	{
		return;
	}
	AOsdsPlayerController* Player = GetPlayer();
	AOsdsGameMode* Mode = GetWorld()->GetAuthGameMode<AOsdsGameMode>();
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const double Wall = FPlatformTime::Seconds() - Started;
	Clock += DeltaSeconds;
	++Frames;
	FrameSeconds += DeltaSeconds;
	WorstFrame = FMath::Max(WorstFrame, static_cast<double>(DeltaSeconds));

	if (Wall > Minutes * 60.0)
	{
		Finish();
		return;
	}
	if (!Player)
	{
		return;
	}
	// Release keys pressed a moment ago.
	if (RecoverHeld >= 0.0 && Clock - RecoverHeld > 0.1)
	{
		Player->InjectKey(EKeys::R, false);
		RecoverHeld = -1.0;
	}
	if (CameraHeld >= 0.0 && Clock - CameraHeld > 0.1)
	{
		Player->InjectKey(EKeys::C, false);
		CameraHeld = -1.0;
	}

	const bool bReady = Pawn && (!Mode || Mode->IsDriveReady());
	if (bReady)
	{
		const FOsdsDriveTelemetry& T = Player->GetTelemetry();
		const FVector Location = Pawn->GetActorLocation();
		if (T.BodyVelocityMps.ContainsNaN() || Location.ContainsNaN() || !FMath::IsFinite(T.EngineSpeedRpm))
		{
			++NonFinite;
			{ const FString RecordLine = FString::Printf(TEXT("SOAK non-finite car state at %.0f s: location %s velocity %s"), Wall, *Location.ToString(),
				*T.BodyVelocityMps.ToString()); UE_LOG(LogOsdsSoak, Error, TEXT("%s"), *RecordLine); Record(RecordLine); }
		}
		const double Speed = T.BodyVelocityMps.X;
		// Hold about 45 km/h with the trigger, as a person would by feel, and weave slowly.
		const float Throttle = static_cast<float>(FMath::Clamp(0.25 + 0.25 * (12.5 - Speed), 0.0, 1.0));
		const float Stick = 0.35f * FMath::Sin(Clock * 0.45) + 0.15f * FMath::Sin(Clock * 1.7);
		Player->InjectAxis(EKeys::Gamepad_RightTriggerAxis, Throttle);
		Player->InjectAxis(EKeys::Gamepad_LeftTriggerAxis, 0.f);
		Player->InjectAxis(EKeys::Gamepad_LeftX, Stick);
		bDriving = true;

		StuckSeconds = FMath::Abs(Speed) < 1.0 ? StuckSeconds + DeltaSeconds : 0.0;
		if (StuckSeconds > 6.0 && RecoverHeld < 0.0)
		{
			Player->InjectKey(EKeys::R, true);
			RecoverHeld = Clock;
			StuckSeconds = 0.0;
			++Recovers;
		}
		if (Clock - LastCamera > 45.0 && CameraHeld < 0.0)
		{
			Player->InjectKey(EKeys::C, true);
			CameraHeld = Clock;
			LastCamera = Clock;
			++Cameras;
		}
		if (Mode && Clock - LastCar > 240.0)
		{
			LastCar = Clock;
			Mode->CycleCar(Player);
			{ const FString RecordLine = FString::Printf(TEXT("SOAK car -> %s at %.0f s"), *Mode->GetCarId(), Wall); UE_LOG(LogOsdsSoak, Display, TEXT("%s"), *RecordLine); Record(RecordLine); }
			++Cars;
		}
		if (bTravel && Mode && Clock - LastTravel > 360.0)
		{
			LastTravel = Clock;
			const TArray<FOsdsPlace>& Presets = OsdsPlaces::Presets();
			if (Presets.Num() > 0)
			{
				const FOsdsPlace& Place = Presets[Travels % Presets.Num()];
				{ const FString RecordLine = FString::Printf(TEXT("SOAK travel -> %s at %.0f s"), *Place.Title, Wall); UE_LOG(LogOsdsSoak, Display, TEXT("%s"), *RecordLine); Record(RecordLine); }
				Mode->TravelTo(Place);
				++Travels;
			}
		}
	}
	else
	{
		Player->InjectAxis(EKeys::Gamepad_RightTriggerAxis, 0.f);
		Player->InjectAxis(EKeys::Gamepad_LeftX, 0.f);
	}

	if (Wall - LastReport >= 30.0)
	{
		LastReport = Wall;
		const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
		const double AvgFps = FrameSeconds > 0.0 ? Frames / FrameSeconds : 0.0;
		{ const FString RecordLine = FString::Printf(TEXT("SOAK %4.0f s: %s, %.0f km/h, %.0f fps avg, worst frame %.0f ms, %.0f MB used, %d recoveries, %d camera changes, place %s, car %s"),
			Wall, bReady ? TEXT("driving") : TEXT("waiting"), Pawn && Player ? Player->GetTelemetry().BodyVelocityMps.X * 3.6 : 0.0, AvgFps,
			WorstFrame * 1000.0, Mem.UsedPhysical / (1024.0 * 1024.0), Recovers, Cameras, Mode ? *Mode->GetPlace().Title : TEXT("-"),
			Mode ? *Mode->GetCarId() : TEXT("-")); UE_LOG(LogOsdsSoak, Display, TEXT("%s"), *RecordLine); Record(RecordLine); }
		Frames = 0;
		FrameSeconds = 0.0;
		WorstFrame = 0.0;
	}
}

void AOsdsSoak::Finish()
{
	bDone = true;
	const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
	{ const FString RecordLine = FString::Printf(TEXT("SOAK DONE: %.1f minutes, drove %d, %d recoveries, %d camera changes, %d car changes, %d travels, %d non-finite states, peak %.0f MB used"),
		Minutes, bDriving ? 1 : 0, Recovers, Cameras, Cars, Travels, NonFinite, Mem.PeakUsedPhysical / (1024.0 * 1024.0)); UE_LOG(LogOsdsSoak, Display, TEXT("%s"), *RecordLine); Record(RecordLine); }
	if (FParse::Param(FCommandLine::Get(), TEXT("OsdsKeepOpen")))
	{
		return;
	}
	// Not the "quit" console command: a Shipping build did not act on it here.
	FPlatformMisc::RequestExit(false, TEXT("OSDS release probe"));
}
