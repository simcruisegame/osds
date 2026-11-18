// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsHotReload.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Environment/OsdsEnvironment.h"
#include "Environment/OsdsEnvironmentSubsystem.h"
#include "Gameplay/OsdsGameMode.h"
#include "Gameplay/OsdsPlayerCameraManager.h"
#include "Gameplay/OsdsPlayerController.h"
#include "Gameplay/OsdsPreferences.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Presentation/OsdsPresentationProfile.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "OsdsGameInstance.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/OsdsUnits.h"
#include "Vehicle/OsdsVehicle.h"
#include "Vehicle/OsdsVehicleMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsHotReload, Log, All);

// --- FOsdsFileWatcher ---------------------------------------------------

void FOsdsFileWatcher::WatchFolder(const FString& Folder, const FString& Wildcard)
{
	Folders.AddUnique(TPair<FString, FString>(FPaths::ConvertRelativePathToFull(Folder), Wildcard));
}

void FOsdsFileWatcher::WatchFile(const FString& Path)
{
	Files.AddUnique(FPaths::ConvertRelativePathToFull(Path));
}

void FOsdsFileWatcher::Scan(TMap<FString, TPair<FDateTime, int64>>& Out) const
{
	IFileManager& FileManager = IFileManager::Get();
	auto Add = [&Out, &FileManager](const FString& Path)
	{
		const FFileStatData Stat = FileManager.GetStatData(*Path);
		if (Stat.bIsValid && !Stat.bIsDirectory)
		{
			Out.Add(Path, TPair<FDateTime, int64>(Stat.ModificationTime, Stat.FileSize));
		}
	};
	for (const TPair<FString, FString>& Folder : Folders)
	{
		TArray<FString> Names;
		FileManager.FindFiles(Names, *FPaths::Combine(Folder.Key, Folder.Value), true, false);
		for (const FString& Name : Names)
		{
			Add(FPaths::Combine(Folder.Key, Name));
		}
	}
	for (const FString& File : Files)
	{
		Add(File);
	}
}

void FOsdsFileWatcher::Baseline(double NowSeconds)
{
	TMap<FString, TPair<FDateTime, int64>> Now;
	Scan(Now);
	States.Reset();
	for (const TPair<FString, TPair<FDateTime, int64>>& Entry : Now)
	{
		FFileState& State = States.Add(Entry.Key);
		State.Stamp = Entry.Value.Key;
		State.Size = Entry.Value.Value;
		State.ChangedAt = NowSeconds;
	}
}

TArray<FString> FOsdsFileWatcher::Poll(double NowSeconds)
{
	TMap<FString, TPair<FDateTime, int64>> Now;
	Scan(Now);
	for (const TPair<FString, TPair<FDateTime, int64>>& Entry : Now)
	{
		FFileState* State = States.Find(Entry.Key);
		if (!State)
		{
			// A new file is a change too (a calibration overlay saved for the first time).
			State = &States.Add(Entry.Key);
			State->bPending = true;
			State->ChangedAt = NowSeconds;
		}
		else if (State->Stamp != Entry.Value.Key || State->Size != Entry.Value.Value)
		{
			State->bPending = true;
			State->ChangedAt = NowSeconds;
		}
		State->Stamp = Entry.Value.Key;
		State->Size = Entry.Value.Value;
	}
	// A deleted file is forgotten; saving it again reports it.
	for (auto It = States.CreateIterator(); It; ++It)
	{
		if (!Now.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	TArray<FString> Settled;
	for (TPair<FString, FFileState>& Entry : States)
	{
		if (Entry.Value.bPending && NowSeconds - Entry.Value.ChangedAt >= SettleSeconds)
		{
			Entry.Value.bPending = false;
			Settled.Add(Entry.Key);
		}
	}
	Settled.Sort();
	return Settled;
}

// --- OsdsHotReload ------------------------------------------------------

namespace OsdsHotReload
{
	FString CalibrationDir()
	{
		// The same folder FOsdsCarDefinitionLoader::LoadById reads.
		FString Dir;
#if !UE_BUILD_SHIPPING
		Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("OSDS_CALIBRATION_DIR"));
#endif
		return Dir.IsEmpty() ? FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Vehicle"), TEXT("Calibration")) : Dir;
	}

	FString LookPath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OSDS"), TEXT("look.json"));
	}

	namespace
	{
		bool SamePath(const FString& A, const FString& B)
		{
			FString FullA = FPaths::ConvertRelativePathToFull(A);
			FString FullB = FPaths::ConvertRelativePathToFull(B);
			FPaths::NormalizeFilename(FullA);
			FPaths::NormalizeFilename(FullB);
			return FullA.Equals(FullB, ESearchCase::IgnoreCase);
		}
	}

	EOsdsHotReloadKind Classify(const FString& Path, FString& OutCarId)
	{
		OutCarId.Reset();
		if (SamePath(Path, OsdsPresentationProfiles::ProfilePath()))
		{
			return EOsdsHotReloadKind::Presentation;
		}
		if (SamePath(Path, OsdsPreferences::Path()))
		{
			return EOsdsHotReloadKind::Preferences;
		}
		if (SamePath(Path, LookPath()))
		{
			return EOsdsHotReloadKind::Look;
		}
		const FString Folder = FPaths::GetPath(Path);
		if (FPaths::GetExtension(Path).Equals(TEXT("json"), ESearchCase::IgnoreCase)
			&& (SamePath(Folder, FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("cars"))) || SamePath(Folder, CalibrationDir())))
		{
			OutCarId = FPaths::GetBaseFilename(Path);
			return EOsdsHotReloadKind::Car;
		}
		return EOsdsHotReloadKind::Unknown;
	}

	void WatchGameData(FOsdsFileWatcher& Watcher)
	{
		Watcher.WatchFolder(FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("cars")), TEXT("*.json"));
		Watcher.WatchFolder(CalibrationDir(), TEXT("*.json"));
		Watcher.WatchFile(OsdsPresentationProfiles::ProfilePath());
		Watcher.WatchFile(OsdsPreferences::Path());
		Watcher.WatchFile(LookPath());
	}

	bool ReloadCarInPlace(AOsdsVehicle* Vehicle, FString& OutError)
	{
		if (!Vehicle || !Vehicle->GetVehicleMovement())
		{
			OutError = TEXT("no car");
			return false;
		}
		FOsdsCarDefinition Car;
		if (!FOsdsCarDefinitionLoader::LoadById(Vehicle->GetRealCar().Id, Car, OutError))
		{
			return false;
		}
		// Where the car stands, under the old body box, then the new car on that spot at rest.
		const double OldBoxCentreCm = OsdsUnits::ToUnrealLength(Vehicle->GetVehicleMovement()->GetCarDefinition().CollisionShapeCenterYM);
		const FVector Ground = Vehicle->GetActorLocation() - Vehicle->GetActorQuat().RotateVector(FVector(0.0, 0.0, OldBoxCentreCm));
		const float Yaw = static_cast<float>(Vehicle->GetActorRotation().Yaw);
		Vehicle->SetCarDefinition(Car);
		Vehicle->ResetAt(Ground, Yaw);
		return true;
	}

	bool ApplyLookFile(UWorld* World, const FString& Path, FString& OutError)
	{
		UOsdsEnvironmentSubsystem* Subsystem = UOsdsEnvironmentSubsystem::Get(World);
		AOsdsEnvironment* Env = Subsystem ? Subsystem->GetEnvironment() : nullptr;
		if (!Env)
		{
			OutError = TEXT("no environment in this world");
			return false;
		}
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			OutError = TEXT("not a JSON object");
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Root->Values)
		{
			double Value = 0.0;
			if (!Field.Key.StartsWith(TEXT("_")) && Field.Value.IsValid() && Field.Value->TryGetNumber(Value))
			{
				Env->SetLookParameter(FName(*Field.Key), static_cast<float>(Value));
			}
		}
		return true;
	}
}

// --- UOsdsHotReloadSubsystem --------------------------------------------

bool UOsdsHotReloadSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (GIsAutomationTesting || FParse::Param(FCommandLine::Get(), TEXT("OsdsNoHotReload")) || !Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
#endif
}

void UOsdsHotReloadSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	OsdsHotReload::WatchGameData(Watcher);
	Watcher.Baseline(FPlatformTime::Seconds());
	bStarted = true;
	UE_LOG(LogOsdsHotReload, Display, TEXT("Hot reload on: car files, calibration, CarPresentation.json, preferences.json, %s"), *OsdsHotReload::LookPath());
}

TStatId UOsdsHotReloadSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOsdsHotReloadSubsystem, STATGROUP_Tickables);
}

void UOsdsHotReloadSubsystem::Tick(float DeltaTime)
{
	if (!bStarted)
	{
		return;
	}
	// A look.json kept from an earlier session is the look the developer left: put it on once
	// the environment exists.
	if (!bLookApplied && FPaths::FileExists(OsdsHotReload::LookPath()))
	{
		const UOsdsEnvironmentSubsystem* Env = UOsdsEnvironmentSubsystem::Get(GetWorld());
		if (Env && Env->GetEnvironment())
		{
			bLookApplied = true;
			FString Error;
			if (OsdsHotReload::ApplyLookFile(GetWorld(), OsdsHotReload::LookPath(), Error))
			{
				UE_LOG(LogOsdsHotReload, Display, TEXT("Look from %s"), *OsdsHotReload::LookPath());
			}
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (Now < NextPoll)
	{
		return;
	}
	NextPoll = Now + 0.25;
	for (const FString& Path : Watcher.Poll(Now))
	{
		ApplyChange(Path);
	}
}

APlayerController* UOsdsHotReloadSubsystem::GetPlayer() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetFirstPlayerController() : nullptr;
}

void UOsdsHotReloadSubsystem::Show(const FString& Text) const
{
	if (AOsdsPlayerController* Player = Cast<AOsdsPlayerController>(GetPlayer()))
	{
		Player->ShowMessage(Text, 3.f);
	}
	else
	{
		UE_LOG(LogOsdsHotReload, Display, TEXT("%s"), *Text);
	}
}

void UOsdsHotReloadSubsystem::ApplyChange(const FString& Path)
{
	FString CarId;
	switch (OsdsHotReload::Classify(Path, CarId))
	{
	case EOsdsHotReloadKind::Car:
		ReloadCar(CarId, Path);
		break;
	case EOsdsHotReloadKind::Presentation:
		RespawnPlayerCar(Path);
		break;
	case EOsdsHotReloadKind::Preferences:
		ApplyPreferences(Path);
		break;
	case EOsdsHotReloadKind::Look:
	{
		FString Error;
		Show(OsdsHotReload::ApplyLookFile(GetWorld(), Path, Error)
			? FString::Printf(TEXT("Reloaded %s"), *FPaths::GetCleanFilename(Path))
			: FString::Printf(TEXT("Reload failed: %s: %s"), *FPaths::GetCleanFilename(Path), *Error));
		break;
	}
	default:
		break;
	}
}

void UOsdsHotReloadSubsystem::ReloadCar(const FString& CarId, const FString& Path)
{
	const FString File = FPaths::GetCleanFilename(Path);
	FString Error;
	UOsdsGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance<UOsdsGameInstance>() : nullptr;
	if (GameInstance && !GameInstance->ReloadCarDefinition(CarId, Error))
	{
		// The roster keeps the last good definition; the car drives on.
		Show(FString::Printf(TEXT("Reload failed: %s: %s"), *File, *Error));
		return;
	}
	APlayerController* Player = GetPlayer();
	AOsdsVehicle* Vehicle = Player ? Cast<AOsdsVehicle>(Player->GetPawn()) : nullptr;
	if (!Vehicle || Vehicle->GetRealCar().Id != CarId)
	{
		// Another car's file: the roster has it for the next spawn.
		UE_LOG(LogOsdsHotReload, Display, TEXT("Reloaded %s (not the car being driven)"), *File);
		return;
	}
	if (AOsdsGameMode* Mode = GetWorld()->GetAuthGameMode<AOsdsGameMode>())
	{
		Mode->RespawnCar(Player);
	}
	else if (!OsdsHotReload::ReloadCarInPlace(Vehicle, Error))
	{
		Show(FString::Printf(TEXT("Reload failed: %s: %s"), *File, *Error));
		return;
	}
	Show(FString::Printf(TEXT("Reloaded %s"), *File));
}

void UOsdsHotReloadSubsystem::RespawnPlayerCar(const FString& Path)
{
	const FString File = FPaths::GetCleanFilename(Path);
	APlayerController* Player = GetPlayer();
	const AOsdsVehicle* Vehicle = Player ? Cast<AOsdsVehicle>(Player->GetPawn()) : nullptr;
	if (!Vehicle)
	{
		return;
	}
	// A broken file would leave the car without a body: check it first.
	FOsdsCarPresentationProfile Profile;
	FString Error;
	if (!OsdsPresentationProfiles::Load(Vehicle->GetRealCar().Id, Profile, Error))
	{
		Show(FString::Printf(TEXT("Reload failed: %s: %s"), *File, *Error));
		return;
	}
	if (AOsdsGameMode* Mode = GetWorld()->GetAuthGameMode<AOsdsGameMode>())
	{
		Mode->RespawnCar(Player);
		Show(FString::Printf(TEXT("Reloaded %s"), *File));
	}
}

void UOsdsHotReloadSubsystem::ApplyPreferences(const FString& Path)
{
	// The game writes preferences.json itself (menus); say something only when the seat or the
	// lens the driver sees changes.
	AOsdsPlayerController* Player = Cast<AOsdsPlayerController>(GetPlayer());
	AOsdsPlayerCameraManager* Camera = Player ? Cast<AOsdsPlayerCameraManager>(Player->PlayerCameraManager) : nullptr;
	const AOsdsVehicle* Vehicle = Player ? Cast<AOsdsVehicle>(Player->GetPawn()) : nullptr;
	if (!Camera || !OsdsPreferences::IsEnabled())
	{
		return;
	}
	bool bChanged = false;
	if (Vehicle)
	{
		const FOsdsSeat Seat = FOsdsSeat::Load(Vehicle->GetRealCar().Id);
		bChanged |= Seat.UpCm != Camera->Seat.UpCm || Seat.ForwardCm != Camera->Seat.ForwardCm || Seat.PitchDeg != Camera->Seat.PitchDeg || Seat.FovDeg != Camera->Seat.FovDeg;
		Camera->Seat = Seat;
	}
	const int32 Lens = OsdsPreferences::GetInt(TEXT("lens_v3"), static_cast<int32>(Camera->GetLens()), 0, 2);
	for (int32 I = 0; I < 3 && static_cast<int32>(Camera->GetLens()) != Lens; ++I)
	{
		Camera->CycleLens();
		bChanged = true;
	}
	// The screen the true-FOV lens is computed for, as PlayerTick reads it at the start.
	auto SetVar = [&bChanged](const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name); Var && Var->GetFloat() != Value)
		{
			Var->Set(Value, ECVF_SetByCode);
			bChanged = true;
		}
	};
	const int32 Tenths = OsdsPreferences::GetInt(TEXT("screen_tenths_inch"), -1, -1, 1000);
	const int32 Distance = OsdsPreferences::GetInt(TEXT("view_distance_cm"), -1, -1, 400);
	if (Tenths > 0) SetVar(TEXT("osds.Lens.ScreenInches"), Tenths / 10.f);
	if (Distance > 0) SetVar(TEXT("osds.Lens.ViewDistanceCm"), static_cast<float>(Distance));
	if (bChanged)
	{
		Show(FString::Printf(TEXT("Reloaded %s"), *FPaths::GetCleanFilename(Path)));
	}
}
