// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Environment/OsdsGraphics.h"
#include "GameFramework/Actor.h"
#include "OsdsLightingLab.generated.h"

class ACameraActor;
class AOsdsEnvironment;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** A named camera pose in the lab, metres and degrees (heading clockwise from north). */
USTRUCT()
struct FOsdsLabView
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FVector EyeM = FVector::ZeroVector;

	UPROPERTY()
	float HeadingDeg = 0.0f;

	UPROPERTY()
	float PitchDeg = 0.0f;

	UPROPERTY()
	float HorizontalFovDeg = 70.8f;
};

/** A point whose screen position is written next to each capture, for pixel readback. */
USTRUCT()
struct FOsdsLabProbe
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FVector WorldCm = FVector::ZeroVector;
};

/**
 * A simple, measurable street for judging light against photographs: an asphalt road
 * running north-south with kerbs and sidewalks, pastel stucco blocks on the west side
 * (Ocean Drive's hotel row), grass, sand and sea to the east, and reference objects:
 * an 18 % grey card, white / grey / black / chrome spheres and a car-sized box.
 * Everything is built from engine basic shapes at runtime; no assets.
 *
 * With -OsdsLabShots / -OsdsLabPerf on the command line it runs a capture and
 * timing script (see docs/unreal-architecture.md) and can quit when done.
 */
UCLASS()
class OSDS_API AOsdsLightingLab : public AActor
{
	GENERATED_BODY()

public:
	AOsdsLightingLab();

	virtual void Tick(float DeltaSeconds) override;

	/** Builds the street and props (idempotent). */
	void Build();

	/** Puts the player's view at a named camera pose. */
	bool SetView(const FString& ViewName);

	/** Reads the script from the command line and starts it. */
	void StartScriptFromCommandLine();

	UPROPERTY()
	TArray<FOsdsLabView> Views;

	UPROPERTY()
	TArray<FOsdsLabProbe> Probes;

protected:
	virtual void BeginPlay() override;

private:
	struct FStep
	{
		enum class EKind : uint8 { Preset, Time, View, Settle, Capture, Perf, Quit };
		EKind Kind = EKind::Settle;
		FString Text;
		float Seconds = 0.0f;
		EOsdsGraphicsPreset Preset = EOsdsGraphicsPreset::High;
	};

	UStaticMeshComponent* AddBox(const FString& Name, const FVector& CenterM, const FVector& SizeM, UMaterialInterface* Material, float YawDeg = 0.0f);
	UStaticMeshComponent* AddShape(const FString& Name, UStaticMesh* Mesh, const FVector& CenterM, const FVector& ScaleUnits, UMaterialInterface* Material, const FRotator& Rotation = FRotator::ZeroRotator);
	UMaterialInterface* Surface(const FString& Name, const FLinearColor& Albedo, float Roughness, float Metallic = 0.0f, float Specular = 0.5f);
	void EnsureMasterMaterial();
	AOsdsEnvironment* Environment() const;
	bool IsWorldReady() const;
	void RunSteps(float DeltaSeconds);
	void WriteSidecar(const FString& ImagePath) const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> MasterMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> ViewCamera;

	TArray<FStep> Steps;
	int32 StepIndex = INDEX_NONE;
	float StepClock = 0.0f;
	bool bStepStarted = false;
	bool bBuilt = false;
	FString OutputDir;
	FString CurrentView;
	FString CurrentShot;
	EOsdsGraphicsPreset CurrentPreset = EOsdsGraphicsPreset::High;
	FString PendingCapture;
	bool bCaptureRequested = false;
	bool bPerfRunning = false;
};
