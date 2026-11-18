// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OsdsWorldSubsystem.generated.h"

class AOsdsWorldStreamer;

/**
 * Entry point of real-world streaming for a game world.
 *
 * Gameplay code calls StartDrive(lon, lat, title). From the command line:
 *   -OsdsWorld              start at the bundled Miami Beach place (offline)
 *   -OsdsStart=lat,lon      start somewhere else (needs the helper / network)
 *   -OsdsPreview=<shots>    also spawn the preview rig (sun, sky, camera) and
 *                                capture the listed shots (see AOsdsWorldPreview)
 */
UCLASS()
class OSDS_API UOsdsWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Start (or restart) a drive at a place; spawns the streamer on first use. */
	AOsdsWorldStreamer* StartDrive(double LonDeg, double LatDeg, const FString& Title);

	AOsdsWorldStreamer* GetStreamer() const { return Streamer.Get(); }

	/** The bundled offline place (godot-final:game/StartPool.gd FALLBACK). */
	static constexpr double BundledLon = -80.13025;
	static constexpr double BundledLat = 25.78000;

private:
	UPROPERTY(Transient)
	TObjectPtr<AOsdsWorldStreamer> Streamer;
};
