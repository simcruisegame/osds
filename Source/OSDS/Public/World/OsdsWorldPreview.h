// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OsdsWorldPreview.generated.h"

class ACameraActor;
class AOsdsWorldStreamer;

/**
 * Capture rig for reviewing the streamed world without the car or the HUD:
 * adds a sun, sky, sky light and haze when the map has none (the lighting
 * stream owns the real setup), places a camera at each requested shot once
 * the tiles around it are built and the shaders compiled, lets exposure and
 * temporal effects settle, writes Saved/Screenshots/OSDS/<shot>.png and
 * quits when done (-OsdsKeepOpen keeps the game running).
 *
 * Shots (comma separated):
 *   ocean    driver's eye height in the travel lane of Ocean Drive, Miami Beach
 *   collins  the same on Collins Avenue
 *   top      straight down from 120 m over the start point (edges, junctions)
 *   seam     straight down from 60 m over the nearest tile corner
 *   aerial   three-quarter view from 70 m
 *   drop     a 1000 kg box dropped onto Ocean Drive; logs where it settles
 */
UCLASS(NotPlaceable, Transient)
class OSDS_API AOsdsWorldPreview : public AActor
{
	GENERATED_BODY()

public:
	AOsdsWorldPreview();

	void Begin(AOsdsWorldStreamer* InStreamer, const FString& ShotList);

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EStage : uint8 { WaitStart, Place, WaitTiles, Settle, Shoot, AfterShot, Done };

	void SpawnLightingIfMissing();
	bool PlaceShot(const FString& Shot);
	void Finish();

	UPROPERTY(Transient)
	TObjectPtr<AOsdsWorldStreamer> Streamer;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY(Transient)
	TObjectPtr<AActor> DropBody;

	TArray<FString> Shots;
	int32 ShotIndex = 0;
	EStage Stage = EStage::WaitStart;
	int32 Frames = 0;
	double StageStarted = 0.0;
	FVector StartCm = FVector::ZeroVector;
	FVector TargetCm = FVector::ZeroVector;
	FString OutputDir;
};
