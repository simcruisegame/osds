// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OsdsEnvironmentSubsystem.generated.h"

class AOsdsEnvironment;
class AOsdsLightingLab;
class ULevel;

/**
 * Gives every game world its OSDS sky. When play begins it spawns one
 * AOsdsEnvironment (unless the level already has one), turns off any other sun,
 * sky, cloud or fog in the level, and applies the command line:
 *
 *   -OsdsLat=25.78 -OsdsLon=-80.13 -OsdsElevation=2
 *   -OsdsUtc=2025-04-24T16:20:00Z  -OsdsTimeScale=60
 *   -OsdsConditions=<file written by helper/conditions.mjs>
 *   -OsdsCloud=0.44 -OsdsVisibilityKm=16 -OsdsRain
 *   -OsdsGraphics=auto|low|medium|high|ultra
 *   -OsdsLab            build the lighting lab street (see AOsdsLightingLab)
 *   -OsdsNoEnvironment  leave the level's own lighting alone
 */
UCLASS()
class OSDS_API UOsdsEnvironmentSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "OSDS|Environment")
	AOsdsEnvironment* GetEnvironment() const { return Environment; }

	static UOsdsEnvironmentSubsystem* Get(const UWorld* World);

private:
	/** A streamed level or World Partition cell arrived: its own sky parts are turned off too. */
	void HandleLevelAdded(ULevel* Level, UWorld* World);

	FDelegateHandle LevelAddedHandle;

	UPROPERTY(Transient)
	TObjectPtr<AOsdsEnvironment> Environment;

	UPROPERTY(Transient)
	TObjectPtr<AOsdsLightingLab> Lab;
};
