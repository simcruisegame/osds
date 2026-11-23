// Copyright OSDS contributors. All Rights Reserved.

// -OsdsPickerShots=<folder>: test-only. Waits for the drive, takes a shot of the start car,
// then presses the car picker (AOsdsGameMode::CycleCar) CyclesToDo times and takes a shot after
// each, logging the car id every time. Changes no setting (the car preference is off under
// -unattended) and quits when done, or after 300 s.

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/OsdsGameMode.h"
#include "Gameplay/OsdsPlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsPickerShots, Log, All);

namespace
{
	struct FPickerShots
	{
		FString Dir;
		double Clock = 0.0;
		double StepAt = 0.0;
		int32 Step = 0;
		int32 Cycles = 0;
		static constexpr int32 CyclesToDo = 4;

		void Next(double Wait) { ++Step; StepAt = Clock + Wait; }

		bool Tick(float DeltaTime)
		{
			Clock += DeltaTime;
			if (Clock > 300.0)
			{
				UE_LOG(LogOsdsPickerShots, Warning, TEXT("PICKERSHOTS TIMED OUT at step %d"), Step);
				FPlatformMisc::RequestExit(false);
				return false;
			}
			if (Clock < StepAt || !GEngine) return true;
			AOsdsPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld()) PC = Cast<AOsdsPlayerController>(World->GetFirstPlayerController());
				if (PC) break;
			}
			AOsdsGameMode* Mode = PC ? PC->GetWorld()->GetAuthGameMode<AOsdsGameMode>() : nullptr;
			if (!PC || !Mode) return true;
			switch (Step)
			{
			case 0:
				if (Mode->IsDriveReady() && PC->HasVehicle()) Next(12.0);
				break;
			case 1: // the shot, then press the picker
			{
				const FString Car = Mode->GetCarId();
				const FString Path = FPaths::Combine(Dir, FString::Printf(TEXT("picker-%d-%s.png"), Cycles, *Car));
				FScreenshotRequest::RequestScreenshot(Path, true, false);
				UE_LOG(LogOsdsPickerShots, Display, TEXT("PICKERSHOT presses=%d car=%s %s"), Cycles, *Car, *Path);
				if (Cycles >= CyclesToDo)
				{
					Next(2.0);
					Step = 2;
					break;
				}
				Next(2.0);
				Step = 10;
				break;
			}
			case 10:
				Mode->CycleCar(PC);
				++Cycles;
				UE_LOG(LogOsdsPickerShots, Display, TEXT("PICKERSHOT pressed %d, car now %s"), Cycles, *Mode->GetCarId());
				StepAt = Clock + 10.0; // the new car spawns and the camera settles
				Step = 1;
				break;
			case 2:
				UE_LOG(LogOsdsPickerShots, Display, TEXT("PICKERSHOTS DONE in %s"), *Dir);
				FPlatformMisc::RequestExit(false);
				return false;
			default: break;
			}
			return true;
		}
	};

	struct FPickerShotsStarter
	{
		FPickerShotsStarter()
		{
			FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
			{
				FString Dir;
				if (!FParse::Value(FCommandLine::Get(), TEXT("OsdsPickerShots="), Dir)) return;
				TSharedRef<FPickerShots> Shots = MakeShared<FPickerShots>();
				Shots->Dir = FPaths::ConvertRelativePathToFull(Dir);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Shots](float Dt) { return Shots->Tick(Dt); }));
			});
		}
	};
	FPickerShotsStarter PickerShotsStarter;
}
