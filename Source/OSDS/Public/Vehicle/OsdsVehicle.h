// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/OsdsVehicleControl.h"
#include "Presentation/OsdsCarPresentationComponent.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/OsdsTuning.h"
#include "Vehicle/OsdsVehicleTelemetry.h"
#include "OsdsVehicle.generated.h"

class UBoxComponent;
class UOsdsEngineSoundComponent;
class UOsdsRoadNoiseComponent;
class UOsdsVehicleMovementComponent;
class USpotLightComponent;

/**
 * A drivable OSDS car: a simulating Chaos box (the car's collision box
 * from its definition) as the root body, driven by
 * UOsdsVehicleMovementComponent. Presentation (meshes, wheels, cameras,
 * audio, rumble) attaches to this pawn and reads GetVehicleTelemetry().
 *
 * The root is the collision box centre; the Godot car origin (ground at ride
 * height) is CollisionShapeCenterYM below it. Spawn with SpawnActorDeferred,
 * call SetCarDefinition or set CarId, then finish spawning.
 *
 * It is the driving shell's vehicle (IOsdsVehicleControl: the shell's
 * shaped pedals and rack angle in, telemetry out) and feeds the car model
 * (IOsdsWheelStateProvider: wheel steer, camber, spin, travel, lamps).
 * The visible car is optional (AttachCarModel), so physics benches stay light.
 */
UCLASS()
class OSDS_API AOsdsVehicle : public APawn, public IOsdsVehicleControl, public IOsdsWheelStateProvider
{
	GENERATED_BODY()

public:
	AOsdsVehicle();

	/** Car file id under data/cars, loaded at BeginPlay when no definition was set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OSDS|Vehicle")
	FString CarId = TEXT("nb1999");

	/** Install a car before spawning finishes (or later: resizes the body and resets). */
	void SetCarDefinition(const FOsdsCarDefinition& Car);

	/** The car as its file describes it, before any tuning. */
	const FOsdsCarDefinition& GetRealCar() const { return RealCar; }
	/** The real car's setup in tuning units (the "reset" values). */
	FOsdsTuning GetStockTuning() const;
	const FOsdsTuning& GetTuning() const { return Tuning; }
	/** Rebuild the car from the real one with this tuning (clamped to safe ranges); bSave writes
	 *  it to Saved/OSDS/tuning-<id>.json (false when that write fails). Resets the
	 *  drivetrain, so a save on the move waits for the car to stop (HasPendingTuning). */
	bool ApplyTuning(const FOsdsTuning& InTuning, bool bSave);
	/** Apply the driver's saved tuning for this car, if there is one (the player's car only). */
	bool LoadSavedTuning();
	/** A setup saved while moving, waiting for the car to stop. */
	bool HasPendingTuning() const { return bTuningPending; }

	/**
	 * Build the car's downloaded model on this pawn (blocking the first time a
	 * car is built; later from the cache), posed every frame from the physics.
	 * Returns false (and keeps driving, invisible) when the model is missing.
	 */
	bool AttachCarModel();

	UFUNCTION(BlueprintPure, Category = "OSDS|Vehicle")
	UOsdsVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement; }

	UFUNCTION(BlueprintPure, Category = "OSDS|Vehicle")
	UBoxComponent* GetBody() const { return Body; }

	UFUNCTION(BlueprintPure, Category = "OSDS|Vehicle")
	UOsdsCarPresentationComponent* GetCarModel() const { return CarModel; }

	UFUNCTION(BlueprintPure, Category = "OSDS|Vehicle|Telemetry")
	FOsdsVehicleTelemetry GetVehicleTelemetry() const;

	// Driving-shell interface (forwards to the movement component).
	UFUNCTION(BlueprintCallable, Category = "OSDS|Vehicle|Controls")
	void SetThrottle(float Value);

	UFUNCTION(BlueprintCallable, Category = "OSDS|Vehicle|Controls")
	void SetBrake(float Value);

	UFUNCTION(BlueprintCallable, Category = "OSDS|Vehicle|Controls")
	void SetSteer(float Value, bool bFromThumbstick = false);

	UFUNCTION(BlueprintCallable, Category = "OSDS|Vehicle|Controls")
	void SetHandbrake(bool bEngaged);

	UFUNCTION(BlueprintCallable, Category = "OSDS|Vehicle|Controls")
	void SetClutch(float Value);

	UFUNCTION(BlueprintCallable, Category = "OSDS|Vehicle|Controls")
	void ShiftUp();

	UFUNCTION(BlueprintCallable, Category = "OSDS|Vehicle|Controls")
	void ShiftDown();

	UFUNCTION(BlueprintCallable, Category = "OSDS|Vehicle|Controls")
	void ToggleManual();

	/** Place the car with its wheels on the ground at Location (Godot origin) facing Yaw (deg), stopped. */
	UFUNCTION(BlueprintCallable, Category = "OSDS|Vehicle")
	void ResetAt(const FVector& GroundLocation, float YawDeg);

	/** World transform of the root body for a car origin at GroundLocation facing YawDeg. */
	FTransform BodyTransformForGroundPose(const FVector& GroundLocation, float YawDeg) const;

	/** Head and tail lamps on (else they follow the sun: on from 1 deg below the horizon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSDS|Vehicle")
	bool bLampsOn = false;

	// IOsdsVehicleControl (game thread; see OsdsVehicleControl.h).
	virtual FOsdsVehicleSetup GetVehicleSetup() const override;
	virtual void GetVehicleTelemetry(FOsdsDriveTelemetry& OutTelemetry) const override;
	virtual void ApplyDriverCommand(const FOsdsDriverCommand& Command) override;
	virtual FString RequestGearChange(int32 Step) override;
	virtual void SetManualTransmission(bool bManual) override;
	virtual void RequestEngineStart() override;
	/** WorldTransform is a ground pose (the car origin on the road), as the shell's recover passes it. */
	virtual void ResetVehicle(const FTransform& WorldTransform) override;

	// IOsdsWheelStateProvider
	virtual bool GetCarVisualState(FOsdsCarVisualState& OutState) const override;

	/** Map the vehicle's telemetry to the shell's view of it (exposed for tests). */
	static void ToDriveTelemetry(const FOsdsCarDefinition& Car, const FOsdsVehicleTelemetry& In, FOsdsDriveTelemetry& Out);
	/** Map the vehicle's telemetry to the car model's pose (exposed for tests). */
	static void ToVisualState(const FOsdsCarDefinition& Car, const FOsdsVehicleTelemetry& In, FOsdsCarVisualState& Out);

	/**
	 * Low-beam photometry (a halogen H4 reflector lamp of the late 1990s):
	 * peak intensity per lamp in candela, aimed this far below level, with
	 * the bright core and the cone's edge as half angles, colour 3200 K.
	 */
	static constexpr double LowBeamPeakCandela = 15000.0;
	static constexpr double LowBeamAimDownDeg = 1.5;
	/** The cone's full-intensity core: the beam's own shape comes from the light function (OsdsLowBeamPattern), so the cone must not dim it a second time. */
	static constexpr double LowBeamCoreHalfAngleDeg = 30.0;
	/** A real low beam spreads about 40 deg to each side at low intensity; a round cone this wide also spills upward (no cut-off yet). */
	static constexpr double LowBeamEdgeHalfAngleDeg = 32.0;
	/**
	 * What the driver's eye adapts to at night in its own low beams: the luminance of the lit
	 * road ahead (dry asphalt under low beams reads about 0.4-2 cd/m2), not a card facing the
	 * lamps, which metered about 2 cd/m2 and left the 0.1 cd/m2 road black.
	 */
	static constexpr double NightAdaptationLuminanceCdM2 = 0.5;
	/** That luminance as the illuminance on a middle-grey (18 %) Lambertian surface, lux. */
	static constexpr double NightAdaptationLux = NightAdaptationLuminanceCdM2 * UE_DOUBLE_PI / 0.18;
	/** How far the beam reaches before Unreal cuts the light off, metres (the 15000 cd peak gives 0.24 lx there). */
	static constexpr double LowBeamRangeM = 250.0;

	/** The beam lights (empty until AttachCarModel, or when the model has no head lamps). */
	const TArray<TObjectPtr<USpotLightComponent>>& GetHeadLampLights() const { return HeadLampLights; }
	/** Head and tail lamps lit this frame. */
	bool AreLampsLit() const { return AreLampsWanted(); }

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ApplyBodyShape();
	void ApplyBodyShape(const FOsdsCarDefinition& Car);
	bool AreLampsWanted() const;

	UPROPERTY(VisibleAnywhere, Category = "OSDS|Vehicle")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "OSDS|Vehicle")
	TObjectPtr<UOsdsVehicleMovementComponent> VehicleMovement;

	UPROPERTY(Transient)
	TObjectPtr<UOsdsCarPresentationComponent> CarModel;

	/** The engine's voice (the driven car only, while sound is on and an audio device exists). */
	UPROPERTY(Transient)
	TObjectPtr<UOsdsEngineSoundComponent> EngineSound;

	/** Tyres, road and wind (the driven car only, while sound is on). */
	UPROPERTY(Transient)
	TObjectPtr<UOsdsRoadNoiseComponent> RoadNoise;
	/** The live wind at car height (m/s, world), as given to the physics: the wind noise follows the airspeed. */
	FVector AirWindMps = FVector::ZeroVector;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> HeadLampLights;

	/** Beam-pattern light functions taken off by osds.Lamps.Pattern 0, to put back. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<USpotLightComponent>, TObjectPtr<UMaterialInterface>> BeamPatterns;

	void UpdateHeadLamps();
	bool bHeadLampsShown = false;

	bool bHasDefinition = false;
	FOsdsCarDefinition RealCar;
	FOsdsTuning Tuning;
	bool bTuned = false;
	bool bTuningPending = false;
	bool bManualRequested = false;
	FString LastShownGearMessage;
	bool bMirrorsLive = false;
	void UpdateEngineSound(const struct FOsdsVehicleTelemetry& Telemetry, bool bInSeat, float DeltaSeconds);
	void UpdateRoadNoise(const struct FOsdsVehicleTelemetry& Telemetry, bool bInSeat, bool bWant, float DeltaSeconds);
	/** Road roughness for the road noise (RoadNoise.gd update_roughness). */
	double Travel[4] = {};
	bool bTravelKnown = false;
	double TravelSlow = 0.0, Roughness = 0.0;
};
