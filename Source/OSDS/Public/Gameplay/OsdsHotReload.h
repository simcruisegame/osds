// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OsdsHotReload.generated.h"

class AOsdsVehicle;
class APlayerController;

/**
 * Polls a set of files and folders for changes (time stamp or size) and reports a changed
 * file once it has stayed unchanged for SettleSeconds, so an editor that writes a file in
 * several steps gives one reload. No threads and no OS notifications: Poll is cheap for the
 * few dozen data files the game reads, and the caller owns the clock (tests pass their own).
 */
class OSDS_API FOsdsFileWatcher
{
public:
	/** Watch every file in Folder that matches Wildcard (for example "*.json"); not recursive. */
	void WatchFolder(const FString& Folder, const FString& Wildcard);
	/** Watch one file (it may not exist yet). */
	void WatchFile(const FString& Path);
	/** Take the files as they are now: no change is reported for them. */
	void Baseline(double NowSeconds);
	/** Files changed since the last call and quiet for SettleSeconds (full paths). */
	TArray<FString> Poll(double NowSeconds);

	double SettleSeconds = 1.0;

private:
	struct FFileState
	{
		FDateTime Stamp;
		int64 Size = -1;
		double ChangedAt = 0.0;
		bool bPending = false;
	};
	void Scan(TMap<FString, TPair<FDateTime, int64>>& Out) const;

	TArray<TPair<FString, FString>> Folders;
	TArray<FString> Files;
	TMap<FString, FFileState> States;
};

/** What a watched file feeds. */
enum class EOsdsHotReloadKind : uint8
{
	Unknown,
	/** data/cars/<id>.json or Content/Vehicle/Calibration/<id>.json: the car's physics (applied at spawn). */
	Car,
	/** Content/Vehicles/CarPresentation.json: the car model (applied at spawn). */
	Presentation,
	/** Saved/OSDS/preferences.json: the seat and the lens (live). */
	Preferences,
	/** Saved/OSDS/look.json: exposure and colour grade (live, OSDS.Env.Look names). */
	Look,
};

namespace OsdsHotReload
{
	/** The calibration overlay folder the car loader reads (OSDS_CALIBRATION_DIR, else Content/Vehicle/Calibration). */
	OSDS_API FString CalibrationDir();
	/** Saved/OSDS/look.json. */
	OSDS_API FString LookPath();
	/** What a changed file feeds; OutCarId is set for EOsdsHotReloadKind::Car. */
	OSDS_API EOsdsHotReloadKind Classify(const FString& Path, FString& OutCarId);
	/** Every file the game reads for car tuning, the seat and lens, and the look. */
	OSDS_API void WatchGameData(FOsdsFileWatcher& Watcher);
	/**
	 * Reload the car's definition (car file plus calibration overlay) and put it on this vehicle
	 * where it stands, at rest: the in-place respawn when there is no OSDS game mode (tests,
	 * benches). False with OutError when the files do not load; the car keeps its old definition.
	 */
	OSDS_API bool ReloadCarInPlace(AOsdsVehicle* Vehicle, FString& OutError);
	/** Apply the numbers of a look.json object through AOsdsEnvironment::SetLookParameter. */
	OSDS_API bool ApplyLookFile(UWorld* World, const FString& Path, FString& OutError);
}

/**
 * Hot reload while the game runs (development builds; off in Shipping and in automation
 * runs; -OsdsNoHotReload turns it off). About a second after a save of a car file, its
 * calibration overlay, CarPresentation.json, preferences.json (seat and lens) or look.json
 * (exposure and colour grade), the new values are live and the screen says
 * "Reloaded <file>". Car physics and the car model are only read at spawn, so the car is
 * respawned in place, at rest, with the driver's saved tuning.
 */
UCLASS()
class OSDS_API UOsdsHotReloadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

	/** Apply one changed file now (the tick calls it for each settled change). */
	void ApplyChange(const FString& Path);

private:
	APlayerController* GetPlayer() const;
	void Show(const FString& Text) const;
	void ReloadCar(const FString& CarId, const FString& Path);
	void RespawnPlayerCar(const FString& Path);
	void ApplyPreferences(const FString& Path);

	FOsdsFileWatcher Watcher;
	bool bStarted = false;
	bool bLookApplied = false;
	double NextPoll = 0.0;
};
