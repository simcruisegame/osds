// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OsdsVehicleTelemetry.generated.h"

/**
 * What the HUD, rumble, audio and presentation read from a vehicle. Written
 * by the physics thread after every fixed step, copied out on the game
 * thread. Physical quantities are SI unless the name says otherwise; points
 * are Unreal world centimetres so presentation code can use them directly.
 */
USTRUCT(BlueprintType)
struct OSDS_API FOsdsWheelTelemetry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	bool bContact = false;

	/** Normal load on the tyre (N). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double LoadN = 0.0;

	/** Longitudinal slip ratio, + driving (wheel faster than road). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SlipRatio = 0.0;

	/** Slip ratio where this tyre's longitudinal force peaks. */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double PeakSlipRatio = 0.1;

	/** Relaxed slip angle the lateral force used (deg, + contact sliding to the wheel's right). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SlipAngleDeg = 0.0;

	/** Share of the tyre's grip the longitudinal force used. */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double GripUse = 0.0;

	/** Combined force over the friction limit (0..1+), for rumble and tyre noise. */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double CombinedGripUse = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double AngularSpeedRadPerSecond = 0.0;

	/** Accumulated wheel rotation for the visual wheel (deg, + rolling forwards). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SpinAngleDeg = 0.0;

	/** Mount-to-hub length along the body's up axis (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SuspensionLengthCm = 0.0;

	/** Road-wheel steer angle (deg, + right). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SteerAngleDeg = 0.0;

	/** Wheel lean (deg, + top toward the car's right). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double InclinationDeg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double LongitudinalForceN = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double LateralForceN = 0.0;

	/** Torque the tyre puts about the steering axis (N m, + steering the wheel to the right). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double AligningTorqueNm = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SurfaceGrip = 1.0;

	/** EOsdsSurfaceKind under the tyre (Unknown 0, Asphalt, Concrete, Gravel, Grass, Dirt). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	uint8 SurfaceKind = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	bool bKerb = false;

	/** ABS pressure fraction (1 = full pedal pressure reaches the caliper). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double AbsRelease = 1.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	FVector ContactPointCm = FVector::ZeroVector;

	/** Ground normal under the tyre (world, unit). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	FVector ContactNormal = FVector::UpVector;
};

USTRUCT(BlueprintType)
struct OSDS_API FOsdsVehicleTelemetry
{
	GENERATED_BODY()

	/** Forward speed along the body (m/s, - in reverse). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double ForwardSpeedMps = 0.0;

	/** Speed magnitude of the centre of mass (m/s). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SpeedMps = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SpeedKph = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SpeedMph = 0.0;

	/** Centre-of-mass velocity in the body frame (m/s; X forward, Y right, Z up). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	FVector BodyVelocityLocalMps = FVector::ZeroVector;

	/** Angular velocity in the body frame (rad/s; X roll, Y pitch, Z yaw, + turning right). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	FVector BodyAngularVelocityLocalRadPerSecond = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double EngineRpm = 0.0;

	/** -1 reverse, 0 neutral, 1.. forward gear. */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	int32 Gear = 1;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	FString GearText;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	bool bManual = false;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	bool bInReverse = false;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	bool bStalled = false;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	bool bShifting = false;

	/** Brake lamps: the service brake is applied. */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	bool bServiceBrake = false;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	bool bRoofDown = false;

	/** Seconds the car has been stranded (roof down or no wheel on the ground). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double StrandedSeconds = 0.0;

	/** Steering input fraction (-1..1, + right) after the rack's rate limits. */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SteeringInput = 0.0;

	/** Rack angle at the road wheels (deg, + right). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SteeringAngleDeg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double ClutchPedal = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double Throttle = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double BrakeAmount = 0.0;

	/** Torque fraction traction control is removing (0..0.9). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double TractionControlCut = 0.0;
	/** Stability control is braking a wheel or cutting the engine (its dash light). */
	bool bStabilityControlActive = false;
	/** What stability control sees (rad/s, + right): the car's yaw rate, the reference, and the filtered
	 *  oversteer and understeer past its dead bands; for test logs. */
	bool bStabilityControlActing = false;
	double EscYawRadPerS = 0.0;
	double EscReferenceYawRadPerS = 0.0;
	double EscOversteer = 0.0;
	double EscUndersteer = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	bool bAbsActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	int32 WheelContactCount = 0;

	/** Sound observers: throttle, combustion and shaft load (Powertrain.gd's sound_*). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SoundThrottle = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SoundCombustion = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SoundShaftLoad = 0.0;

	/** Clutch or motor torque this step (N·m). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double ClutchTorqueNm = 0.0;

	/** Both front tyres' aligning torque about the steering axes (N m, + steering right): what a
	 *  steering wheel feels through the rack, for self-centring and force feedback. */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double RackAligningTorqueNm = 0.0;

	/** Physics time of the step that wrote this telemetry. */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double SimTimeS = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	int64 PhysicsSteps = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double FixedStepSeconds = 0.0;

	/** Mean CPU time of the vehicle's physics-thread step (microseconds). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	double MeanStepMicroseconds = 0.0;

	/** Why the last manual gear change was refused ("" if it was not). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	FString LastGearMessage;

	/** Times the physics thread restored the car's mass properties after something else changed them (should stay 0). */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	int32 MassPropertyRestores = 0;

	/** Front left, front right, rear left, rear right. */
	UPROPERTY(BlueprintReadOnly, Category = "OSDS|Vehicle")
	TArray<FOsdsWheelTelemetry> Wheels;
};
