// Copyright SimCruise contributors. All Rights Reserved.

// -SimCruiseHudShots=<folder>: the HUD from the driver's seat, for the HUD review. It waits
// for the drive (the car of -SimCruiseCar=), then from the driver's seat takes: the HUD with
// the map on Always and no route, the HUD with a route, and the speed readout showing 0, 60
// and 120 (simcruise.Hud.TestSpeed; the car stands still). It changes no saved setting and
// quits when done, or after 300 s whatever happened.

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/SimCruiseDriveHud.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePlayerCameraManager.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruiseRouteSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseHudShots, Log, All);

namespace
{
	struct FHudShots
	{
		FString Dir;
		double Clock = 0.0;
		double StepAt = 0.0;
		int32 Step = 0;

		static ASimCruisePlayerController* Controller()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld())
				{
					if (ASimCruisePlayerController* PC = Cast<ASimCruisePlayerController>(World->GetFirstPlayerController())) return PC;
				}
			}
			return nullptr;
		}

		static void TestSpeed(int32 Speed)
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Hud.TestSpeed"))) Var->Set(Speed, ECVF_SetByCode);
		}

		void Shot(const FString& Name)
		{
			const FString Path = FPaths::Combine(Dir, Name);
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogSimCruiseHudShots, Display, TEXT("HUDSHOT %s"), *Path);
		}

		void Next(double Wait) { ++Step; StepAt = Clock + Wait; }

		bool Tick(float DeltaTime)
		{
			Clock += DeltaTime;
			if (Clock > 300.0)
			{
				UE_LOG(LogSimCruiseHudShots, Warning, TEXT("HUDSHOTS TIMED OUT at step %d"), Step);
				TestSpeed(-1);
				FPlatformMisc::RequestExit(false);
				return false;
			}
			if (Clock < StepAt) return true;
			ASimCruisePlayerController* PC = Controller();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			const ASimCruiseGameMode* Mode = World ? World->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
			USimCruiseRouteSubsystem* Gps = USimCruiseRouteSubsystem::Get(World);
			if (!PC || !Mode || !Gps) return true;
			const FString Car = Mode->GetCarId();
			switch (Step)
			{
			case 0: // the road is built and the car is on it
				if (Mode->IsDriveReady() && PC->HasVehicle()) Next(10.0);
				break;
			case 1:
				if (ASimCruisePlayerCameraManager* Camera = Cast<ASimCruisePlayerCameraManager>(PC->PlayerCameraManager)) Camera->SetView(ESimCruiseView::Driver);
				// Not remembered: the player's own choice stays as it was.
				FSimCruiseDriveHud::SetMapAlways(true, false);
				Next(3.0);
				break;
			case 2: Shot(FString::Printf(TEXT("cockpit-%s-map-always.png"), *Car)); Next(1.5); break;
			case 3:
			{
				FSimCruiseRouteStop Stop;
				Stop.Title = TEXT("Collins Park");
				Stop.Lon = -80.1290;
				Stop.Lat = 25.7930;
				Gps->StartRoute({ Stop });
				Next(1.0);
				break;
			}
			case 4: // wait for the route, then let the HUD fade it in
				if (Gps->HasRoute()) Next(3.0);
				else if (!Gps->IsBusy() && Clock - StepAt > 40.0) { UE_LOG(LogSimCruiseHudShots, Warning, TEXT("No route: %s"), *Gps->GetStatus()); Next(0.0); }
				break;
			case 5: Shot(FString::Printf(TEXT("cockpit-%s-route.png"), *Car)); Next(1.5); break;
			case 6: TestSpeed(0); Next(1.0); break;
			case 7: Shot(FString::Printf(TEXT("speed-%s-000.png"), *Car)); Next(1.0); break;
			case 8: TestSpeed(60); Next(1.0); break;
			case 9: Shot(FString::Printf(TEXT("speed-%s-060.png"), *Car)); Next(1.0); break;
			case 10: TestSpeed(120); Next(1.0); break;
			case 11: Shot(FString::Printf(TEXT("speed-%s-120.png"), *Car)); Next(1.5); break;
			case 12:
				TestSpeed(-1);
				UE_LOG(LogSimCruiseHudShots, Display, TEXT("HUDSHOTS DONE in %s"), *Dir);
				FPlatformMisc::RequestExit(false);
				return false;
			default: break;
			}
			return true;
		}
	};

	struct FHudShotsStarter
	{
		FHudShotsStarter()
		{
			FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
			{
				FString Dir;
				if (!FParse::Value(FCommandLine::Get(), TEXT("SimCruiseHudShots="), Dir)) return;
				TSharedRef<FHudShots> Shots = MakeShared<FHudShots>();
				Shots->Dir = FPaths::ConvertRelativePathToFull(Dir);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Shots](float Dt) { return Shots->Tick(Dt); }));
			});
		}
	};
	FHudShotsStarter HudShotsStarter;
}
