// Copyright OSDS contributors. All Rights Reserved.

#include "Vehicle/OsdsVehicleSim.h"
#include "Vehicle/OsdsSurfaceKind.h"

#include "Vehicle/OsdsVehicleDynamics.h"
#include "Vehicle/OsdsUnits.h"

namespace
{
	using OsdsDynamics::MoveToward;
	using OsdsDynamics::SmoothStep;

	constexpr double Tau = 2.0 * UE_DOUBLE_PI;
	constexpr double LockBlendRad = 3.0 * UE_DOUBLE_PI / 180.0;
	constexpr double KeyRackRateHighwayRadPerSecond = 12.0 * UE_DOUBLE_PI / 180.0;
	constexpr double KeyReturnRateMinimumRadPerSecond = 24.0 * UE_DOUBLE_PI / 180.0;
	constexpr double KeyReturnFactor = 1.6;
	constexpr double SteerDeadBand = 0.01;
	constexpr double ManualShiftClutchBufferS = 0.12;
	// Automatic clutch timing: a quick road-car shift (the pedal down in about 55 ms at the press rate,
	// a short hold, the clutch out over 0.2 s up and 0.25 s down), a blip of at most 0.15 s, and gear
	// steps of a multi-gear request 80 ms apart.
	constexpr double AutoClutchHoldS = 0.04;
	constexpr double AutoClutchUpReleaseS = 0.20;
	constexpr double AutoClutchDownReleaseS = 0.25;
	constexpr double AutoClutchBlipMaxS = 0.15;
	constexpr double AutoClutchBlipBandRpm = 150.0;
	constexpr double ManualGearStepGapS = 0.08;
	constexpr int32 ManualShiftQueueMax = 3;

	// Traction control (Powertrain.gd): the slip read through a 20 ms low-pass
	// and a slew limit, proportional plus integral on the error, harder cut on
	// a large overshoot, cut changed no faster than an engine can.
	constexpr double TcFilterS = 0.02;
	constexpr double TcSlipSlewPerSecond = 100.0;
	constexpr double TcProportional = 0.15;
	constexpr double TcOvershoot = 0.05;
	constexpr double TcCutRateUp = 20.0;
	constexpr double TcCutRateDown = 6.0;

	// ABS modulator: integral gain on the slip error (1/s), a proportional dump
	// past the peak, and the slip it aims at relative to the peak.
	constexpr double AbsGainPerSecond = 20.0;
	constexpr double AbsProportional = 0.3;
	constexpr double AbsTargetOverPeak = 1.0;
	// The front wheels a little short of their peak braking slip, as ABS keeps them so the car still
	// steers: at the peak a TMeasy tyre has almost no side force left (a C8 at full lock under full
	// braking turned at 1-4 deg/s on Tail of the Dragon).
	constexpr double AbsFrontTargetOverPeak = 0.85;

	// Stability control (ESC), after van Zanten (Bosch ESP, SAE 2000-01-1633): it compares the car's
	// yaw rate with the one the steering asks for (a linear single-track model, held to what the tyres'
	// grip allows at this speed) and the body's side slip with a limit; past dead bands that leave
	// ordinary driving alone it brakes one wheel (the outer front against oversteer, the inner rear
	// against understeer) and cuts engine torque. The numbers are ours, not a maker's calibration.
	constexpr double EscFadeInLowMps = 3.0;          // active from about 11 km/h ...
	constexpr double EscFadeInHighMps = 6.0;         // ... fully from about 22 km/h
	constexpr double EscFrictionMargin = 1.1;        // the reference yaw rate stays within 1.1 mu g / v
	constexpr double EscYawDeadbandRadPerS = 0.05;   // oversteer dead band: 0.05 rad/s ...
	constexpr double EscYawDeadbandFraction = 0.15;  // ... plus 15 % of the reference
	constexpr double EscUnderDeadbandRadPerS = 0.10; // understeer: wider, it must not drag in a normal bend
	constexpr double EscUnderDeadbandFraction = 0.30;
	constexpr double EscBetaLowSpeedDeg = 8.0;       // side-slip limit at the CG, 8 deg slow ...
	constexpr double EscBetaHighSpeedDeg = 4.0;      // ... 4 deg at speed (ordinary cornering stays under 3)
	constexpr double EscBetaGainPerS = 2.0;          // rad/s of yaw error per rad of side slip past the limit
	constexpr double EscFilterS = 0.02;
	// The reference follows the steering with part of the lag of a car's yaw response (step-steer t90
	// about 0.2 s), so a quick turn-in is not read as understeer before the car has turned.
	constexpr double EscReferenceLagS = 0.06;
	// What counts as acting (and lights the lamp): a real brake pulse or a real engine cut.
	constexpr double EscActingBrakeNm = 100.0;
	constexpr double EscActingCut = 0.1;
	constexpr double EscOverGainNmPerRadPerS = 4000.0;
	constexpr double EscUnderGainNmPerRadPerS = 2500.0;
	constexpr double EscFrontBrakeShare = 0.7;       // at most this share of a wheel's full brake
	constexpr double EscRearBrakeShare = 0.5;
	constexpr double EscBuildS = 0.2;                // pump pressure up from nothing to the cap
	constexpr double EscReleaseS = 0.1;
	constexpr double EscCutPerOverRadPerS = 3.0;
	constexpr double EscCutPerUnderRadPerS = 1.5;
	constexpr double EscCutMax = 0.9;
	constexpr double EscRegenCutPerOverRadPerS = 10.0;
	constexpr double EscCutRisePerS = 5.0;
	constexpr double EscCutFallPerS = 2.0;
	constexpr double EscLightHoldS = 0.6;

	// Handbrake: a hand pulls the lever up in about 0.15 s and lets it drop in 0.1 s, to the car's own
	// parking-brake torque (parking_brake_nm: enough to lock the rear wheels for a handbrake turn).
	constexpr double HandbrakeApplyS = 0.15;
	constexpr double HandbrakeReleaseS = 0.1;

	// Godot ran the driveline at 60 Hz with 6 substeps; the cars' launches were calibrated there.
	constexpr double ClutchCalibrationSubstepS = 1.0 / 360.0;

	double SmoothMin(double A, double B, double K)
	{
		const double H = FMath::Clamp(0.5 + 0.5 * (B - A) / K, 0.0, 1.0);
		return FMath::Lerp(B, A, H) - K * H * (1.0 - H);
	}

	double AxleValue(const TArray<double>& Values, int32 Axle, double Fallback = 0.0)
	{
		return Values.IsValidIndex(Axle) ? Values[Axle] : Fallback;
	}
}

FOsdsVehicleSim::FOsdsVehicleSim()
{
	Initialize(FOsdsCarDefinition());
}

void FOsdsVehicleSim::Initialize(const FOsdsCarDefinition& InCar)
{
	Car = InCar;
	TireRadiusM = Car.TireFreeRadiusM();
	WheelInertiaKgM2 = FMath::Max(Car.WheelRotationalInertiaKgM2, UE_DOUBLE_SMALL_NUMBER);
	RestLengthM = Car.MaximumSuspensionTravelM;
	ParkingRackRad = OsdsUnits::ToRadians(Car.ParkingRackAngleDeg);
	RoadReferenceRad = OsdsUnits::ToRadians(Car.RoadReferenceSteerAngleDeg);
	LimitSlipSurplusRad = OsdsUnits::ToRadians(Car.SteeringSlipLimitSurplusDeg);
	KeyRackRateParkingRadPerSecond = 2.75 * ParkingRackRad;
	AntiRollRateNPerM[0] = OsdsDynamics::GetAntiRollBarRateNPerM(Car, 0);
	AntiRollRateNPerM[1] = OsdsDynamics::GetAntiRollBarRateNPerM(Car, 1);
	UE_LOG(LogTemp, Verbose, TEXT("OSDS vehicle %s: wheel rates %.0f/%.0f N/m, anti-roll %.0f/%.0f N/m"), *Car.Id,
		OsdsDynamics::GetWheelSpringRateNPerM(Car, 0), OsdsDynamics::GetWheelSpringRateNPerM(Car, 1),
		AntiRollRateNPerM[0], AntiRollRateNPerM[1]);

	// Godot's car origin sits at the ground at ride height; the body (the root
	// collision box) is centred collision_box_y above it.
	ReferenceOriginLocalM = FVector3d(0.0, 0.0, -Car.CollisionShapeCenterYM);
	const double HalfWheelbase = Car.WheelbaseM * 0.5;
	const double MountZ = Car.SuspensionMountHeightM - Car.CollisionShapeCenterYM;
	// Tuning's front/rear ride height moves each axle's mounts (SuspensionMountOffsetMByAxle, 0 stock).
	const double MountZFront = MountZ + AxleValue(Car.SuspensionMountOffsetMByAxle, 0);
	const double MountZRear = MountZ + AxleValue(Car.SuspensionMountOffsetMByAxle, 1);
	MountsLocalM[0] = FVector3d(HalfWheelbase, -Car.FrontTrackM * 0.5, MountZFront);
	MountsLocalM[1] = FVector3d(HalfWheelbase, Car.FrontTrackM * 0.5, MountZFront);
	MountsLocalM[2] = FVector3d(-HalfWheelbase, -Car.RearTrackM * 0.5, MountZRear);
	MountsLocalM[3] = FVector3d(-HalfWheelbase, Car.RearTrackM * 0.5, MountZRear);
	CenterOfMassLocalM = FVector3d((Car.FrontWeightFraction - 0.5) * Car.WheelbaseM, 0.0,
		Car.CenterOfGravityHeightM - Car.CollisionShapeCenterYM);

	State = FOsdsVehicleSimState();
	ResetShiftQueue();
	State.EngineRpm = Car.IdleSpeedRpm;
	State.EngineOmegaRadPerSecond = Car.RestartEngineSpeedRadPerSecond;
	for (FOsdsWheelState& Wheel : State.Wheels)
	{
		Wheel.SuspensionLengthM = RestLengthM;
	}
}

FVector3d FOsdsVehicleSim::GetPrincipalInertiaKgM2() const
{
	return OsdsUnits::GodotInertiaToUnrealAxes(Car.InertiaKgM2);
}

void FOsdsVehicleSim::ResetShiftQueue()
{
	PendingManualGearStep = 0;
	PendingManualGearAgeS = 0.0;
	GearChangeMessage.Reset();
	AutoPhase = 0;
	AutoPhaseTimeS = 0.0;
	SinceGearStepS = 10.0;
	bAutoBlip = false;
	bAutoDownshift = false;
}

void FOsdsVehicleSim::ResetState()
{
	ResetShiftQueue();
	for (FOsdsWheelState& Wheel : State.Wheels)
	{
		Wheel.RollingSlipAngleRad = 0.0;
		Wheel.AngularSpeedRadPerSecond = 0.0;
	}
	State.SoundThrottle = 0.0;
	State.SoundCombustion = 0.0;
	State.SoundShaftLoad = 0.0;
	State.SelectedGear = State.bManual ? 0 : 1;
	State.ClutchPedal = 0.0;
	State.bStalled = false;
	State.bRoofDown = false;
	State.FlippedSeconds = 0.0;
	State.StuckSeconds = 0.0;
	State.StrandedSeconds = 0.0;
	State.Gear = 0;
	State.bInReverse = false;
	State.ShiftTimeS = 0.0;
	State.SteeringRad = 0.0;
	State.SteeringInput = 0.0;
	State.bServiceBrake = false;
	State.HandbrakeAmount = 0.0;
	State.EscReferenceYawRadPerS = 0.0;
	State.EscOversteer = 0.0;
	State.EscUndersteer = 0.0;
	State.EscCut = 0.0;
	State.EscRegenCut = 0.0;
	State.EscLightS = 0.0;
	for (double& Nm : State.EscBrakeNm) Nm = 0.0;
	State.EngineRpm = Car.IdleSpeedRpm;
	State.EngineOmegaRadPerSecond = State.EngineRpm * Tau / 60.0;
}

bool FOsdsVehicleSim::IsElectric() const
{
	return Car.EngineKind == TEXT("electric");
}

void FOsdsVehicleSim::SetManual(bool bEnabled, double BodySpeedMps)
{
	if (IsElectric())
	{
		bEnabled = false;
	}
	ResetShiftQueue();
	State.bManual = bEnabled;
	State.ClutchPedal = 0.0;
	State.bStalled = false;
	State.SelectedGear = State.bInReverse ? -1 : State.Gear + 1;
	State.EngineOmegaRadPerSecond = FMath::Max(State.EngineOmegaRadPerSecond, Car.RestartEngineSpeedRadPerSecond);
	if (State.bManual && BodySpeedMps < 1.0)
	{
		SelectRatio(0);
	}
}

void FOsdsVehicleSim::SelectRatio(int32 Ratio)
{
	State.SelectedGear = Ratio;
	State.bInReverse = Ratio == -1;
	State.Gear = FMath::Clamp(Ratio - 1, 0, FMath::Max(Car.ForwardGearRatios.Num() - 1, 0));
}

FString FOsdsVehicleSim::ChangeGear(int32 GearStep, double ForwardSpeedMps)
{
	if (!State.bManual)
	{
		return FString();
	}
	const int32 Proposed = FMath::Clamp(State.SelectedGear + GearStep, -1, Car.ForwardGearRatios.Num());
	if (Proposed == State.SelectedGear)
	{
		return FString();
	}
	if (State.ClutchPedal < 0.75)
	{
		return TEXT("Hold the clutch to change gear");
	}
	if ((Proposed == -1 && ForwardSpeedMps > 1.0) || (Proposed > 0 && ForwardSpeedMps < -1.0))
	{
		return TEXT("Stop before changing direction");
	}
	if (Proposed > 0 && FMath::Abs(ForwardSpeedMps) / TireRadiusM * Car.ForwardGearRatios[Proposed - 1] *
		Car.FinalDriveRatio * 60.0 / Tau > Car.MaximumGearSpeedRpm)
	{
		// Say the speed the gear can take, so the driver knows how far to slow.
		const double MaxMps = Car.MaximumGearSpeedRpm * Tau / 60.0 / (Car.ForwardGearRatios[Proposed - 1] * Car.FinalDriveRatio) * TireRadiusM;
		static const TCHAR* Ordinals[] = { TEXT("1st"), TEXT("2nd"), TEXT("3rd"), TEXT("4th"), TEXT("5th"), TEXT("6th"), TEXT("7th"), TEXT("8th"), TEXT("9th"), TEXT("10th") };
		return FString::Printf(TEXT("Too fast for %s: below %.0f mph (%.0f km/h)"), Ordinals[FMath::Clamp(Proposed - 1, 0, 9)],
			FMath::FloorToDouble(MaxMps * 2.23694), FMath::FloorToDouble(MaxMps * 3.6));
	}
	SelectRatio(Proposed);
	return FString();
}

void FOsdsVehicleSim::QueueManualGearChange(int32 GearStep)
{
	if (!State.bManual || GearStep == 0)
	{
		return;
	}
	const int32 Direction = GearStep > 0 ? 1 : -1;
	// Presses add up (a quick double tap of the down button drops two gears); a press the other
	// way replaces what is still waiting.
	if (PendingManualGearStep == 0 || (PendingManualGearStep > 0) != (Direction > 0))
	{
		PendingManualGearStep = Direction;
		PendingManualGearAgeS = 0.0;
	}
	else
	{
		PendingManualGearStep = FMath::Clamp(PendingManualGearStep + Direction, -ManualShiftQueueMax, ManualShiftQueueMax);
	}
	GearChangeMessage.Reset();
}

FString FOsdsVehicleSim::ConsumeGearChangeMessage()
{
	FString Message = MoveTemp(GearChangeMessage);
	GearChangeMessage.Reset();
	return Message;
}

void FOsdsVehicleSim::StartEngine()
{
	if (State.bManual && State.bStalled && (State.SelectedGear == 0 || State.ClutchPedal > 0.75))
	{
		State.EngineOmegaRadPerSecond = Car.RestartEngineSpeedRadPerSecond;
		State.EngineRpm = Car.IdleSpeedRpm;
		State.bStalled = false;
	}
}

FString FOsdsVehicleSim::GetGearText() const
{
	if (IsElectric())
	{
		return State.bInReverse ? TEXT("R") : TEXT("D");
	}
	if (State.bManual)
	{
		if (State.SelectedGear == 0)
		{
			return TEXT("N");
		}
		return State.SelectedGear == -1 ? FString(TEXT("R")) : FString::FromInt(State.SelectedGear);
	}
	return State.bInReverse ? FString(TEXT("R")) : FString::FromInt(State.Gear + 1);
}

double FOsdsVehicleSim::AvailableSteeringRad(double SpeedMps, bool bAssisted) const
{
	const double Speed = FMath::Abs(SpeedMps);
	if (!bAssisted)
	{
		const double RoadAngle = RoadReferenceRad / (1.0 + Speed * 0.044);
		return FMath::Lerp(ParkingRackRad, RoadAngle, SmoothStep(2.0, 6.0, Speed));
	}
	// The Ackermann angle for the assist's lateral acceleration plus the slip
	// surplus this tyre model carries at the limit, never more than the lock.
	const double LimitAngle = FMath::Atan(Car.WheelbaseM * Car.SteeringAssistLateralG * OsdsDynamics::GravityMps2 /
		FMath::Max(Speed * Speed, 0.01)) + LimitSlipSurplusRad;
	return SmoothMin(ParkingRackRad, LimitAngle, LockBlendRad);
}

double FOsdsVehicleSim::KeyRackRateRadPerSecond(double SpeedMps) const
{
	return FMath::Lerp(KeyRackRateParkingRadPerSecond, KeyRackRateHighwayRadPerSecond,
		SmoothStep(5.0, 25.0, FMath::Abs(SpeedMps)));
}

double FOsdsVehicleSim::ThumbstickCommand(double Stick, double SpeedMps) const
{
	// A stick has a few millimetres of travel: give its centre to lane
	// corrections and keep full available steering at either end.
	Stick = FMath::Clamp(Stick, -1.0, 1.0);
	const double SpeedSquared = FMath::Max(SpeedMps * SpeedMps, 0.01);
	const double AngleForCornering = FMath::Atan(Car.WheelbaseM * 0.89 * OsdsDynamics::GravityMps2 / SpeedSquared);
	const double CenterGain = FMath::Clamp(AngleForCornering / AvailableSteeringRad(SpeedMps, bSteeringAssist), 0.16, 1.0);
	return Stick * (CenterGain + (1.0 - CenterGain) * Stick * Stick);
}

double FOsdsVehicleSim::AdvanceSteering(double Current, double Target, double DeltaTimeS, double ApplyRate, double ReturnRate)
{
	// Spend the actual fraction of this interval returning through centre; a
	// correction beyond centre releases the lock as promptly as letting go.
	const double UnwindTarget = Current * Target < 0.0 ? 0.0 : Target;
	if (FMath::Abs(UnwindTarget) < FMath::Abs(Current))
	{
		const double UnwindSeconds = FMath::Abs(Current - UnwindTarget) / ReturnRate;
		Current = MoveToward(Current, UnwindTarget, DeltaTimeS * ReturnRate);
		DeltaTimeS = FMath::Max(0.0, DeltaTimeS - UnwindSeconds);
	}
	return MoveToward(Current, Target, DeltaTimeS * ApplyRate);
}

double FOsdsVehicleSim::DrivenOmega() const
{
	const FOsdsWheelState* W = State.Wheels;
	if (Car.DrivetrainLayout == TEXT("fwd"))
	{
		return (W[0].AngularSpeedRadPerSecond + W[1].AngularSpeedRadPerSecond) * 0.5;
	}
	if (Car.DrivetrainLayout == TEXT("awd"))
	{
		const double Share = Car.AwdFrontTorqueShareFraction;
		return Share * (W[0].AngularSpeedRadPerSecond + W[1].AngularSpeedRadPerSecond) * 0.5 +
			(1.0 - Share) * (W[2].AngularSpeedRadPerSecond + W[3].AngularSpeedRadPerSecond) * 0.5;
	}
	return (W[2].AngularSpeedRadPerSecond + W[3].AngularSpeedRadPerSecond) * 0.5;
}

double FOsdsVehicleSim::MotorTorqueNm(double Throttle, double Ratio, double SignDirection, double SpeedMps)
{
	// Constant torque to the base speed, constant power above it, nothing past
	// the motor's top speed; light regeneration when coasting.
	const double MotorOmega = DrivenOmega() * SignDirection * Ratio;
	double Available = FMath::Min(Car.ElectricMotorPeakTorqueNm, Car.ElectricMotorPeakPowerW / FMath::Max(FMath::Abs(MotorOmega), 1.0)) * Car.EngineTorqueScale;
	if (FMath::Abs(MotorOmega) * 60.0 / Tau > Car.ElectricMotorMaximumSpeedRpm)
	{
		Available = 0.0;
	}
	double Torque = Available * Throttle;
	if (Throttle < 0.01 && FMath::Abs(SpeedMps) > 1.0)
	{
		// Stability control takes the regeneration off in a slide: it brakes the driven axle, and on a
		// rear-drive car that keeps the tail out.
		Torque = -Car.ElectricRegenerativeTorqueNm * SmoothStep(1.0, 5.0, FMath::Abs(SpeedMps)) * FMath::Sign(MotorOmega) * (1.0 - State.EscRegenCut);
	}
	State.EngineOmegaRadPerSecond = FMath::Abs(MotorOmega);
	return Torque;
}

void FOsdsVehicleSim::Step(double DeltaTimeS, int32 Substeps, const FOsdsVehicleControls& Controls,
	const FOsdsBodyState& Body, FOsdsGroundQuery Ground, FOsdsBodyForces& OutForces)
{
	OutForces = FOsdsBodyForces();
	if (DeltaTimeS <= 0.0)
	{
		return;
	}
	Substeps = FMath::Max(Substeps, 1);
	const double Dt = DeltaTimeS;
	const double R = TireRadiusM;
	const double I = WheelInertiaKgM2;
	const double MassKg = Car.MassKg;

	const FVector3d Forward = Body.Rotation.RotateVector(FVector3d::UnitX());
	const FVector3d Right = Body.Rotation.RotateVector(FVector3d::UnitY());
	const FVector3d Up = Body.Rotation.RotateVector(FVector3d::UnitZ());
	const FVector3d Velocity = Body.LinearVelocityMps;
	const FVector3d Omega = Body.AngularVelocityRadPerSecond;
	const double Speed = Velocity.Dot(Forward);
	State.ForwardSpeedMps = Speed;

	// Pedals. The automatic changes direction only near standstill, like a
	// real selector's reverse inhibitor: a pedal below the brake threshold
	// while rolling the other way coasts instead of engaging the other gear.
	const double Gas = FMath::Clamp(Controls.Throttle, 0.0, 1.0);
	const double Back = FMath::Clamp(Controls.Brake, 0.0, 1.0);
	const bool bFoot = Controls.bFullBrake;
	double Throttle = 0.0;
	State.bServiceBrake = bFoot || (Back > 0.01 && Speed > 0.45) || (Gas > 0.01 && Speed < -0.45);
	if (!State.bServiceBrake && !State.bManual)
	{
		if (Gas > 0.0 && Speed > -0.45)
		{
			State.bInReverse = false;
			Throttle = Gas;
		}
		else if (Back > 0.0 && Speed < 0.45)
		{
			State.bInReverse = true;
			Throttle = Back;
		}
	}
	double BrakeAmount = bFoot ? 1.0 : (State.bServiceBrake ? FMath::Max(Back, Gas) : 0.0);
	// Launch control: the brake (or the full service brake) and the full accelerator together
	// at a standstill arm it; letting go of the brake launches from its engine speed until the
	// accelerator lifts or the car passes 15 m/s.
	if (Car.LaunchControlRpm > 0.0 && !State.bManual && !IsElectric())
	{
		const bool bHolding = (bFoot || Back > 0.5) && Gas > 0.8 && FMath::Abs(Speed) < 0.45;
		if (bHolding)
		{
			State.bLaunchArmed = true;
			State.bLaunching = false;
		}
		else if (State.bLaunchArmed)
		{
			State.bLaunchArmed = false;
			State.bLaunching = Gas > 0.8 && Back < 0.1 && !bFoot;
		}
		if (State.bLaunching && (Gas < 0.5 || Speed > 15.0))
		{
			State.bLaunching = false;
		}
		if (State.bLaunchArmed)
		{
			State.bInReverse = false;
			State.bServiceBrake = true;
			BrakeAmount = 1.0;
			Throttle = Gas;
		}
	}
	else
	{
		State.bLaunchArmed = false;
		State.bLaunching = false;
	}
	if (State.bManual)
	{
		Throttle = Gas;
		State.bServiceBrake = bFoot || Back > 0.01;
		BrakeAmount = FMath::Max(Back, bFoot ? 1.0 : 0.0);
		State.bInReverse = State.SelectedGear == -1;
		double Pedal = FMath::Clamp(Controls.Clutch, 0.0, 1.0);
		double ReleaseRate = 2.8;
		if (bAutoClutch && !IsElectric())
		{
			// A pulled handbrake at speed: the left foot goes down, as a driver's does for a handbrake
			// turn, so the engine does not fight the locked rear wheels (or stall).
			if (Controls.bHandbrake && FMath::Abs(Speed) > 2.0 && Car.ParkingBrakeType != TEXT("electric"))
			{
				Pedal = 1.0;
			}
			// The automatic clutch: a pedal of its own under the driver's, and the throttle while it shifts.
			AutoPhaseTimeS += Dt;
			double AutoPedal = 0.0;
			if (AutoPhase == 0 && PendingManualGearStep != 0)
			{
				AutoPhase = 1;
				AutoPhaseTimeS = 0.0;
			}
			if (AutoPhase == 1)
			{
				AutoPedal = 1.0;
				Throttle = 0.0;
			}
			else if (AutoPhase == 2)
			{
				AutoPedal = 1.0;
				Throttle = 0.0;
				if (bAutoBlip)
				{
					Throttle = 1.0;
					if (State.EngineRpm >= AutoBlipTargetRpm - AutoClutchBlipBandRpm || AutoPhaseTimeS >= AutoClutchBlipMaxS)
					{
						bAutoBlip = false;
						Throttle = 0.0;
					}
				}
				if (PendingManualGearStep != 0)
				{
					AutoPhaseTimeS = 0.0;
				}
				else if (!bAutoBlip && AutoPhaseTimeS >= AutoClutchHoldS)
				{
					AutoPhase = 3;
					AutoPhaseTimeS = 0.0;
				}
			}
			else if (AutoPhase == 3)
			{
				const double Release = bAutoDownshift ? AutoClutchDownReleaseS : AutoClutchUpReleaseS;
				const double Progress = FMath::Clamp(AutoPhaseTimeS / Release, 0.0, 1.0);
				AutoPedal = 1.0 - Progress;
				Throttle = Gas * Progress;
				ReleaseRate = 1.2 / Release;
				if (PendingManualGearStep != 0)
				{
					AutoPhase = 1;
					AutoPhaseTimeS = 0.0;
				}
				else if (Progress >= 1.0)
				{
					AutoPhase = 0;
				}
			}
			// In gear at a crawl (the road turns the engine near or under idle) the clutch slips as a
			// driver's left foot would: in at idle so the engine never stalls, out as the throttle raises
			// the revs, so the car pulls away from a stop on the throttle alone.
			if (State.SelectedGear != 0 && !State.bStalled)
			{
				const int32 Index = FMath::Clamp(State.SelectedGear - 1, 0, FMath::Max(Car.ForwardGearRatios.Num() - 1, 0));
				const double GearRatio = (State.SelectedGear == -1 ? Car.ReverseGearRatio : Car.ForwardGearRatios[Index]) * Car.FinalDriveRatio;
				const double RoadRpm = FMath::Abs(DrivenOmega()) * GearRatio * 60.0 / Tau;
				// The bite follows the throttle, as a driver's launch revs do: the clutch held in at idle
				// with no throttle (no stall), biting from about 2100 rpm at a third of the pedal; it is
				// fully out once the road turns the engine at that speed.
				const double LaunchRpm = FMath::Min(Car.IdleSpeedRpm + 400.0 + 2500.0 * Gas, Car.FuelCutSpeedRpm * 0.55);
				if (RoadRpm < LaunchRpm + 100.0)
				{
					const double Hold = (1.0 - SmoothStep(LaunchRpm - 450.0, LaunchRpm + 250.0, State.EngineRpm))
						* (1.0 - SmoothStep(LaunchRpm - 300.0, LaunchRpm + 100.0, RoadRpm));
					AutoPedal = FMath::Max(AutoPedal, Hold);
					ReleaseRate = FMath::Max(ReleaseRate, 4.0);
				}
			}
			Pedal = FMath::Max(Pedal, AutoPedal);
		}
		State.ClutchPedal = MoveToward(State.ClutchPedal, Pedal, Dt * (Pedal > State.ClutchPedal ? 14.0 : ReleaseRate));
	}
	else
	{
		AutoPhase = 0;
	}
	State.BrakeAmount = BrakeAmount;

	// Steering rack.
	const double Cap = AvailableSteeringRad(Speed, bSteeringAssist);
	double Turn = Controls.bThumbstickSteer ? ThumbstickCommand(Controls.Steer, Speed) : FMath::Clamp(Controls.Steer, -1.0, 1.0);
	if (FMath::Abs(Turn) < SteerDeadBand)
	{
		Turn = 0.0;
	}
	if (Controls.bDirectRack)
	{
		// The shell's shaper owns rates and caps; the rack only stops at the lock.
		State.SteeringRad = FMath::Clamp(Controls.RackAngleRad, -ParkingRackRad, ParkingRackRad);
		State.SteeringInput = Cap > 1.0e-6 ? FMath::Clamp(State.SteeringRad / Cap, -1.0, 1.0) : 0.0;
	}
	else if (Controls.bThumbstickSteer)
	{
		State.SteeringInput = AdvanceSteering(State.SteeringInput, Turn, Dt, 2.75, 4.4);
	}
	else if (bSteeringAssist)
	{
		// Key rates are set in rack angle, so the same key moves the wheel more
		// slowly as the cap closes with speed. Releasing or countersteering unwinds first.
		const double Apply = KeyRackRateRadPerSecond(Speed) / Cap;
		const double ReturnRate = FMath::Max(Apply * KeyReturnFactor, KeyReturnRateMinimumRadPerSecond / Cap);
		State.SteeringInput = AdvanceSteering(State.SteeringInput, Turn, Dt, Apply, ReturnRate);
	}
	else
	{
		State.SteeringInput = MoveToward(State.SteeringInput, Turn, Dt * (Turn != 0.0 ? 2.75 : 4.4));
	}
	if (!Controls.bDirectRack)
	{
		State.SteeringRad = State.SteeringInput * Cap;
	}
	// Steering compliance: the front tyres' aligning torque (last step's) winds the column,
	// rack mounts and tie rods, so the road wheels turn a little less than the rack asks.
	// It is a real share of a road car's understeer (often a quarter to a half of it).
	const double Steering = Car.SteeringStiffnessNmPerDeg > 0.0
		? State.SteeringRad + State.RackAligningTorqueNm / OsdsUnits::ToDegrees(1.0) / Car.SteeringStiffnessNmPerDeg
		: State.SteeringRad;

	State.bRoofDown = Up.Dot(FVector3d::UnitZ()) < 0.2;
	State.FlippedSeconds = State.bRoofDown ? State.FlippedSeconds + Dt : 0.0;

	// Ackermann: the inner wheel turns more, by ackermann_blend of the full geometry.
	const double HalfTrack = Car.FrontTrackM * 0.5;
	const double Blend = Car.AckermannBlendFraction;
	const double Rack = FMath::Abs(Steering);
	// In the exact form (tan of each wheel's angle = L tan(rack) / (L -+ half track tan(rack))), which is
	// continuous through straight ahead. The old form floored tan(rack) at 0.001, so any rack off zero
	// put both wheels at least 0.028 deg off straight: the car could not hold the 0.015 deg a road's
	// crown asks for and hunted left and right of it (the owner: "the car is all over the road").
	const double TanRack = FMath::Tan(Rack);
	const double InnerAngle = FMath::Lerp(Rack, FMath::Atan2(Car.WheelbaseM * TanRack, FMath::Max(Car.WheelbaseM - HalfTrack * TanRack, 0.2 * TanRack)), Blend);
	const double OuterAngle = FMath::Lerp(Rack, FMath::Atan2(Car.WheelbaseM * TanRack, Car.WheelbaseM + HalfTrack * TanRack), Blend);
	const double ToeRad = OsdsUnits::ToRadians(Car.ToeAngleDegPerWheel);
	const double CasterRad = OsdsUnits::ToRadians(Car.CasterAngleDeg);

	struct FContactRow
	{
		bool bValid = false;
		FVector3d Point = FVector3d::ZeroVector;
		FVector3d Normal = FVector3d::UnitZ();
		double Load = 0.0;
		FVector3d WheelForward = FVector3d::UnitX();
		FVector3d WheelRight = FVector3d::UnitY();
		double ForwardSpeed = 0.0;
		double LateralSpeed = 0.0;
		double Mu = 0.0;
		double SideForce = 0.0;
		/** TMeasy: tan(relaxed slip angle) and surface times axle grip. */
		double SlipY = 0.0;
		double GripScale = 1.0;
		double MeanLateral = 0.0;
		double MeanLongitudinal = 0.0;
	};
	FContactRow Rows[4];
	double Lengths[4] = { RestLengthM, RestLengthM, RestLengthM, RestLengthM };
	State.WheelContactCount = 0;

	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		FOsdsWheelState& Wheel = State.Wheels[WheelIndex];
		const FVector3d Source = Body.PositionM + Body.Rotation.RotateVector(MountsLocalM[WheelIndex]);
		FOsdsGroundHit Hit;
		Wheel.bContact = false;
		// The tyre is a tread, not a point: rays at its centre and near both shoulders. The
		// highest one carries the wheel (a kerb edge under one shoulder lifts it, a pothole
		// narrower than the tread does not drop it), and the grip is the tread's mean.
		bool bHit = false;
		{
			const double Shoulder = 0.4 * Car.TireSectionWidthM;
			const FVector3d Offsets[3] = { FVector3d::ZeroVector, Right * Shoulder, Right * -Shoulder };
			double GripSum = 0.0;
			int32 GripCount = 0;
			double Best = -1.0e9;
			double CentreHeight = -1.0e9;
			for (int32 Ray = 0; Ray < (Car.ContactRays >= 3 ? 3 : 1); ++Ray)
			{
				FOsdsGroundHit Probe;
				const FVector3d From = Source + Offsets[Ray];
				if (Ground(From, From - Up * (RestLengthM + R), Probe) && Probe.NormalM.Dot(Up) > 0.33)
				{
					GripSum += Probe.SurfaceGrip;
					++GripCount;
					const double Height = (Probe.PointM - Source).Dot(Up) + Probe.RoadProfileM;
					if (Ray == 0)
					{
						CentreHeight = Height;
						Best = Height;
						Hit = Probe;
						bHit = true;
					}
					else if (Height > Best + 0.01)
					{
						// A step under a shoulder (a kerb, a pothole's edge) carries the tyre. The
						// contact stays under the wheel's centre: on even ground, and as the body
						// rolls, the shoulders must not widen the track.
						Best = Height;
						if (!bHit || CentreHeight < -1.0e8)
						{
							Hit = Probe;
							bHit = true;
						}
						else
						{
							Hit.PointM += Up * (Height - (Hit.PointM - Source).Dot(Up) - Hit.RoadProfileM);
							// The tyre rolls over a small step (a seam or a junction lip of a centimetre
							// or three) on its tread: the contact rises but the road under the centre still
							// sets the normal. Taking the step's own face normal (up to 70 deg) pushed the
							// tyre sideways by load x sin(tilt) at every 1 cm lip and steered the car.
							// A real kerb (over 3 cm) tilts the tread's envelope: atan(step / half the
							// tread), at most 30 deg, pushing the tyre away from the high side.
							const double StepM = Height - CentreHeight;
							if (StepM > 0.03)
							{
								const FVector3d SideDir = Offsets[Ray].GetSafeNormal();
								const double Tilt = FMath::Min(FMath::Atan(StepM / FMath::Max(Shoulder, 0.02)), UE_DOUBLE_PI / 6.0);
								Hit.NormalM = (Hit.NormalM * FMath::Cos(Tilt) - SideDir * FMath::Sin(Tilt)).GetSafeNormal();
							}
						}
						// A step of more than 3 cm under a shoulder: the tyre is on a kerb.
						if (Height > CentreHeight + 0.03 && CentreHeight > -1.0e8) Hit.bKerb = true;
					}
				}
			}
			if (bHit)
			{
				Hit.SurfaceGrip = GripSum / GripCount;
			}
		}
		// A tyre does not stand on a wall: a surface more than 75 deg from level (a bank the car slid
		// into, the ground of a car on its side or roof) gives the wheel no contact; the body's own
		// collision meets it. Such hits once drove the suspension into huge loads, and the car off
		// in a spin at hundreds of m/s.
		if (bHit && Hit.NormalM.Dot(Up) < 0.25)
		{
			bHit = false;
		}
		if (bHit)
		{
			FContactRow& Row = Rows[WheelIndex];
			const int32 Axle = WheelIndex < 2 ? 0 : 1;
			const double Length = FMath::Clamp((Source - Hit.PointM).Dot(Up) - R - Hit.RoadProfileM,
				Car.MinimumSuspensionTravelM, RestLengthM);
			Lengths[WheelIndex] = Length;
			const FVector3d Arm = Hit.PointM - Body.CenterOfMassM;
			const FVector3d PointVelocity = Velocity + Omega.Cross(Arm);
			// At most ten times the corner's static share: a hard bump-stop hit is three to five, and
			// a step past ten is a landing the integration cannot carry in one substep.
			const double Load = FMath::Min(OsdsDynamics::ComputeSuspensionLoadN(Car, Axle, Length,
				PointVelocity.Dot(Hit.NormalM) - Hit.RoadProfileRateMps), 10.0 * 0.25 * Car.MassKg * OsdsDynamics::GravityMps2);

			// Road-wheel angle, positive right; the inner wheel is the right one
			// in a right turn. Toe-in turns each wheel toward the centreline.
			const double Side = (WheelIndex % 2 == 0) ? -1.0 : 1.0;
			double Angle = 0.0;
			if (WheelIndex < 2)
			{
				const bool bInner = (WheelIndex == 1 && Steering > 0.0) || (WheelIndex == 0 && Steering < 0.0);
				Angle = FMath::Sign(Steering) * (bInner ? InnerAngle : OuterAngle);
			}
			// Tuning may set toe per axle (ToeAngleDegByAxle); else the car file's one toe for all four.
			Angle -= Side * (Car.ToeAngleDegByAxle.Num() == 2 ? OsdsUnits::ToRadians(Car.ToeAngleDegByAxle[Axle]) : ToeRad);
			const double CamberDeg = AxleValue(Car.StaticCamberDegByAxle, Axle) -
				(Car.StaticSuspensionLengthM - Length) * AxleValue(Car.CamberGainDegPerMByAxle, Axle);
			double Inclination = Side * OsdsUnits::ToRadians(CamberDeg);
			if (WheelIndex < 2)
			{
				Inclination += FMath::Sin(Angle) * CasterRad;
			}
			Wheel.InclinationRad = Inclination;
			Wheel.SteerAngleRad = Angle;

			FVector3d WheelForward = Forward * FMath::Cos(Angle) + Right * FMath::Sin(Angle);
			WheelForward = (WheelForward - Hit.NormalM * WheelForward.Dot(Hit.NormalM)).GetSafeNormal();
			const FVector3d WheelRight = Hit.NormalM.Cross(WheelForward).GetSafeNormal();

			Row.bValid = true;
			Row.Point = Hit.PointM;
			Row.Normal = Hit.NormalM;
			Row.Load = Load;
			Row.WheelForward = WheelForward;
			Row.WheelRight = WheelRight;
			Row.ForwardSpeed = PointVelocity.Dot(WheelForward);
			Row.LateralSpeed = PointVelocity.Dot(WheelRight);
			Wheel.bContact = true;
			Wheel.ContactPointM = Hit.PointM;
			Wheel.ContactNormal = Hit.NormalM;
			Wheel.SurfaceGrip = Hit.SurfaceGrip;
			Wheel.SurfaceKind = Hit.SurfaceKind;
			// The kerb's own slope (gutter edge, face, batter) also counts, past about 18 deg.
			Wheel.bKerb = Hit.bKerb || (Hit.SurfaceKind == static_cast<uint8>(EOsdsSurfaceKind::Concrete) && Hit.NormalM.Dot(Up) < 0.95);
			Wheel.ForwardSpeedMps = Row.ForwardSpeed;
			Wheel.LateralSpeedMps = Row.LateralSpeed;
			++State.WheelContactCount;
		}
		Wheel.SuspensionLengthM = Lengths[WheelIndex];
	}

	// Stranded: roof down for 1.5 s, or no wheel touching for 2 s while nearly still.
	State.StuckSeconds = (State.WheelContactCount == 0 && Velocity.Size() < 1.0) ? State.StuckSeconds + Dt : 0.0;
	State.StrandedSeconds = (State.FlippedSeconds > 1.5 || State.StuckSeconds > 2.0) ? State.StrandedSeconds + Dt : 0.0;

	// Anti-roll bars move load from the extended to the compressed side.
	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		if (Rows[WheelIndex].bValid)
		{
			Rows[WheelIndex].Load = FMath::Max(0.0, Rows[WheelIndex].Load +
				(Lengths[WheelIndex ^ 1] - Lengths[WheelIndex]) * AntiRollRateNPerM[WheelIndex < 2 ? 0 : 1]);
		}
	}

	// Roll centres: the links carry the axle's side force at the roll centre's height straight to
	// the outer tyre (F h_rc / track, the geometric load transfer), at once and without rolling the
	// body; the springs carry only the rest. The body's forces are unchanged in sum. From the
	// last substep's side forces (positive to the right moves load to the left, even, wheel).
	for (int32 Axle = 0; Axle < 2; ++Axle)
	{
		const double Height = AxleValue(Car.RollCentreHeightMByAxle, Axle);
		const int32 Left = Axle * 2;
		if (Height <= 0.0 || !Rows[Left].bValid || !Rows[Left + 1].bValid)
		{
			continue;
		}
		const double Track = Axle == 0 ? Car.FrontTrackM : Car.RearTrackM;
		const double Side = State.Wheels[Left].LateralForceN + State.Wheels[Left + 1].LateralForceN;
		const double Transfer = FMath::Clamp(Side * Height / FMath::Max(Track, 0.5),
			-Rows[Left].Load, Rows[Left + 1].Load);
		Rows[Left].Load += Transfer;
		Rows[Left + 1].Load -= Transfer;
	}

	// Gear selection.
	SinceGearStepS += Dt;
	if (State.bManual && PendingManualGearStep != 0)
	{
		PendingManualGearAgeS += Dt;
		const bool bAuto = bAutoClutch && !IsElectric();
		const bool bReady = State.ClutchPedal >= 0.75 && SinceGearStepS >= ManualGearStepGapS;
		// With the automatic clutch the request waits for its own pedal (there in about 55 ms); with
		// the driver's clutch it waits a short buffer, then reports the refusal.
		const bool bTimedOut = PendingManualGearAgeS >= (bAuto ? 0.6 : ManualShiftClutchBufferS);
		if (bReady || bTimedOut)
		{
			const int32 Direction = PendingManualGearStep > 0 ? 1 : -1;
			const int32 Before = State.SelectedGear;
			GearChangeMessage = ChangeGear(Direction, Speed);
			if (GearChangeMessage.IsEmpty() && State.SelectedGear != Before)
			{
				PendingManualGearStep -= Direction;
				PendingManualGearAgeS = 0.0;
				SinceGearStepS = 0.0;
				if (bAuto)
				{
					AutoPhase = 2;
					AutoPhaseTimeS = 0.0;
					bAutoDownshift = Direction < 0;
					// Blip on a downshift into a forward gear: the throttle opens until the engine
					// reaches the speed the road turns it at in the new gear.
					bAutoBlip = false;
					if (Direction < 0 && State.SelectedGear > 0)
					{
						const double NewRatio = Car.ForwardGearRatios[State.SelectedGear - 1] * Car.FinalDriveRatio;
						AutoBlipTargetRpm = FMath::Abs(DrivenOmega()) * NewRatio * 60.0 / Tau;
						bAutoBlip = AutoBlipTargetRpm > State.EngineRpm + AutoClutchBlipBandRpm;
					}
				}
			}
			else
			{
				// Refused, or already at the end of the box: drop what is waiting.
				PendingManualGearStep = 0;
				PendingManualGearAgeS = 0.0;
				if (bAuto && AutoPhase != 0)
				{
					AutoPhase = 3;
					AutoPhaseTimeS = 0.0;
					bAutoBlip = false;
				}
			}
		}
	}
	else if (!State.bManual)
	{
		PendingManualGearStep = 0;
		PendingManualGearAgeS = 0.0;
	}
	double ForwardRatio = (State.bInReverse ? Car.ReverseGearRatio : Car.ForwardGearRatios[State.Gear]) * Car.FinalDriveRatio;
	const double SignDirection = State.bInReverse ? -1.0 : 1.0;
	State.ShiftTimeS = FMath::Max(0.0, State.ShiftTimeS - Dt);
	State.SinceShiftS += Dt;
	if (!State.bManual && State.ShiftTimeS <= 0.0 && !State.bInReverse)
	{
		// The shift map follows the pedal (Throttle is the driver's pedal here,
		// before traction control trims it). It reads the road speed, as a gearbox's
		// controller reads its output shaft, not the engine's speed: through a shift and
		// with the clutch slipping the engine runs apart from the road (it dropped to idle
		// and called down shift after shift: 19 mph in first at 851 rpm on the Dragon).
		// The output shaft: the driven wheels' speed through the gear (a few percent of tyre slip
		// over the road at full load, as a real controller sees it). The upshift guard below still
		// reads the car's own speed, so a spinning launch does not change up.
		const double RoadRpm = FMath::Abs(DrivenOmega()) * ForwardRatio * 60.0 / Tau;
		const int32 Next = OsdsDynamics::SelectAutomaticGear(Car, State.Gear, RoadRpm,
			// Both the shift point and its guard read the output shaft (a gearbox changes up on its output
			// speed, spinning tyres or not): reading the car's own speed kept the Mustang in 1st on its fuel
			// cut with the rears spinning, 5000 rpm from 16 to 24 mph on the Dragon.
			FMath::Abs(DrivenOmega()) * ForwardRatio, Throttle, State.bServiceBrake || BrakeAmount > 0.01,
			Speed * Omega.Z / OsdsDynamics::GravityMps2);
		int32 NextGear = Next;
		// At the limiter, change up: a driven tyre's slip at speed (a few percent) keeps the road's
		// speed under the upshift point while the engine sits on its fuel cut (the C8 held 5th at
		// 6619 rpm and 153 mph, its road speed reading 6350).
		if (NextGear == State.Gear && State.Gear < Car.ForwardGearRatios.Num() - 1 && !(State.bServiceBrake || BrakeAmount > 0.01) &&
			State.EngineRpm >= Car.FuelCutSpeedRpm * 0.98 && RoadRpm > Car.AutomaticUpshiftSpeedRpm * 0.85)
		{
			NextGear = State.Gear + 1;
		}
		// Up one gear at a time (a kickdown may still drop two).
		NextGear = FMath::Min(NextGear, State.Gear + 1);
		const int32 Direction = NextGear > State.Gear ? 1 : NextGear < State.Gear ? -1 : 0;
		// No change back the other way within 2 s of the last one (the Dragon's D5-D4-D2-D4 and
		// 1-2-1-2 hunting as the pedal moved), unless the engine is at its limiter.
		const bool bReversal = Direction != 0 && Direction == -State.LastShiftDirection && State.SinceShiftS < 2.0;
		const bool bAtLimit = Direction > 0 && State.EngineRpm >= Car.FuelCutSpeedRpm * 0.98;
		// One gear at a time on the way up: no second upshift within 0.7 s of the last, unless the engine
		// is at its limiter (the Corvette went from 1st straight to 3rd on a hard launch, its spinning
		// rears reading over 2nd's upshift point as soon as the first change ended).
		const bool bTooSoonUp = Direction > 0 && State.LastShiftDirection > 0 && State.SinceShiftS < 0.7;
		if (Direction != 0 && (!bReversal || bAtLimit) && (!bTooSoonUp || (bAtLimit && State.SinceShiftS >= 0.4)))
		{
			State.Gear = NextGear;
			State.ShiftTimeS = Car.GearShiftDurationS;
			State.LastShiftDirection = Direction;
			State.SinceShiftS = 0.0;
			ForwardRatio = Car.ForwardGearRatios[State.Gear] * Car.FinalDriveRatio;
		}
	}

	// Lateral force per contact from this interval's relaxed slip angle.
	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		FOsdsWheelState& Wheel = State.Wheels[WheelIndex];
		FContactRow& Row = Rows[WheelIndex];
		if (!Row.bValid)
		{
			Wheel.LoadN = 0.0;
			Wheel.RollingSlipAngleRad = 0.0;
			Wheel.SlipAngleRad = 0.0;
			Wheel.LongitudinalForceN = 0.0;
			Wheel.LateralForceN = 0.0;
			continue;
		}
		const double Fz = Row.Load;
		Wheel.LoadN = Fz;
		double SlipAngle = 0.0;
		if (Fz > 0.0)
		{
			SlipAngle = OsdsDynamics::ComputeRelaxedSlipAngleRad(Car, Row.ForwardSpeed, Row.LateralSpeed, Dt, Fz,
				Wheel.RollingSlipAngleRad);
		}
		else
		{
			Wheel.RollingSlipAngleRad = 0.0;
		}
		Wheel.SlipAngleRad = SlipAngle;
		const int32 Axle = WheelIndex < 2 ? 0 : 1;
		// The tyre's pressure moves its peak grip (MF-Tyre 6.1 pressure term), on every surface.
		const double PressureGrip = OsdsDynamics::TyrePressureGripScale(AxleValue(Car.TyrePressurePsiByAxle, Axle, 32.0), AxleValue(Car.TyrePressureRefPsiByAxle, Axle, 32.0));
		Row.Mu = OsdsDynamics::ComputeFrictionCoefficient(Car, Axle, Fz, Wheel.SurfaceGrip * PressureGrip);
		Wheel.FrictionCoefficient = Row.Mu;
		const double RoadLean = FMath::Asin(FMath::Clamp(Right.Dot(Row.Normal), -1.0, 1.0));
		if (OsdsDynamics::UsesTmeasy(Car))
		{
			// The slip force comes from the combined TMeasy curve in each substep; here only
			// the camber thrust (the same term the legacy law adds).
			Row.SlipY = FMath::Tan(FMath::Clamp(SlipAngle, -1.4, 1.4));
			Row.GripScale = FMath::Max(Wheel.SurfaceGrip, 0.0) * AxleValue(Car.AxleGripMultiplier, Axle, 1.0) * PressureGrip;
			OsdsDynamics::FTmeasyCurve CurveX, CurveY;
			OsdsDynamics::GetTmeasyCurves(Car, Fz, Row.GripScale, CurveX, CurveY);
			Row.Mu = Fz > 1.0 ? CurveY.PeakForce / Fz : 0.0;
			Wheel.FrictionCoefficient = Row.Mu;
			Row.SideForce = OsdsDynamics::ComputeLateralForceN(Car, Fz, Row.Mu, 0.0,
				Wheel.InclinationRad - RoadLean, Row.ForwardSpeed);
		}
		else
		{
			Row.SideForce = OsdsDynamics::ComputeLateralForceN(Car, Fz, Row.Mu, SlipAngle,
				Wheel.InclinationRad - RoadLean, Row.ForwardSpeed);
		}
	}

	// Stability control (see the Esc constants): once per step, from the body's motion and the rack.
	{
		double MuSum = 0.0;
		int32 MuCount = 0;
		for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			if (Rows[WheelIndex].bValid)
			{
				MuSum += State.Wheels[WheelIndex].FrictionCoefficient;
				++MuCount;
			}
		}
		// d(Forward)/dt = Omega x Forward: its share along Right is the yaw rate, + turning right.
		const double YawRate = Omega.Cross(Forward).Dot(Right);
		State.EscYawRadPerS = YawRate;
		const double Presence = SmoothStep(EscFadeInLowMps, EscFadeInHighMps, Speed);
		const bool bHandbrakeTurn = State.HandbrakeAmount > 0.3 && Car.ParkingBrakeType != TEXT("electric");
		const bool bEsc = Car.bStabilityControlEnabled && bStabilityControlSwitch && !State.bInReverse && !bHandbrakeTurn &&
			Presence > 0.0 && MuCount >= 3 && !State.bRoofDown;
		double Over = 0.0;
		double Under = 0.0;
		double TurnSign = 0.0;
		double RefSign = 0.0;
		if (bEsc)
		{
			const double G = OsdsDynamics::GravityMps2;
			// Tuning's ESC level widens the dead bands (Sport); 1 is the maker's calibration.
			const double EscBand = FMath::Max(Car.StabilityControlDeadbandScale, 0.1);
			const double K = OsdsUnits::ToRadians(Car.StabilityControlUndersteerDegPerG) / G;
			const double FrictionYaw = EscFrictionMargin * (MuSum / MuCount) * G / Speed;
			const double SteadyRef = FMath::Clamp(Speed * Steering / (Car.WheelbaseM + K * Speed * Speed), -FrictionYaw, FrictionYaw);
			State.EscReferenceYawRadPerS += (SteadyRef - State.EscReferenceYawRadPerS) * (1.0 - FMath::Exp(-Dt / EscReferenceLagS));
			const double RefYaw = State.EscReferenceYawRadPerS;
			// Oversteer: the car turns further than the steering asks, or the other way to it (a slide the
			// driver is already countersteering).
			TurnSign = FMath::Abs(YawRate) > 0.02 ? FMath::Sign(YawRate) : 0.0;
			const double Excess = TurnSign * (YawRate - RefYaw);
			Over = FMath::Max(Excess - EscBand * (EscYawDeadbandRadPerS + EscYawDeadbandFraction * FMath::Abs(RefYaw)), 0.0);
			// Side slip at the CG with the tail out (turning right, the velocity points left of the nose).
			const double Beta = FMath::Atan2(Velocity.Dot(Right), FMath::Max(Speed, 1.0));
			const double BetaLimit = EscBand * OsdsUnits::ToRadians(FMath::Lerp(EscBetaLowSpeedDeg, EscBetaHighSpeedDeg, SmoothStep(10.0, 30.0, Speed)));
			Over += EscBetaGainPerS * FMath::Max(-TurnSign * Beta - BetaLimit, 0.0);
			// Understeer: the car turns less than the steering asks (and the tyres could give).
			RefSign = FMath::Abs(RefYaw) > 0.02 ? FMath::Sign(RefYaw) : 0.0;
			const double Short = RefSign * (RefYaw - YawRate);
			Under = Over > 0.0 ? 0.0 : FMath::Max(Short - EscBand * (EscUnderDeadbandRadPerS + EscUnderDeadbandFraction * FMath::Abs(RefYaw)), 0.0);
			Over *= Presence;
			Under *= Presence;
		}
		else
		{
			State.EscReferenceYawRadPerS = 0.0;
		}
		const double Filter = 1.0 - FMath::Exp(-Dt / EscFilterS);
		State.EscOversteer += (Over - State.EscOversteer) * Filter;
		State.EscUndersteer += (Under - State.EscUndersteer) * Filter;
		double Target[4] = { 0.0, 0.0, 0.0, 0.0 };
		if (bEsc && TurnSign != 0.0 && State.EscOversteer > 1.0e-3)
		{
			// Brake the outer front: turning right, the left front (wheel 0).
			Target[TurnSign > 0.0 ? 0 : 1] = FMath::Min(EscOverGainNmPerRadPerS * State.EscOversteer, EscFrontBrakeShare * Car.FrontBrakeTorqueNm);
		}
		if (bEsc && RefSign != 0.0 && State.EscUndersteer > 1.0e-3)
		{
			// Brake the inner rear: turning right, the right rear (wheel 3).
			Target[RefSign > 0.0 ? 3 : 2] = FMath::Min(EscUnderGainNmPerRadPerS * State.EscUndersteer, EscRearBrakeShare * Car.RearBrakeTorqueNm);
		}
		bool bActing = false;
		for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			const double Full = WheelIndex < 2 ? EscFrontBrakeShare * Car.FrontBrakeTorqueNm : EscRearBrakeShare * Car.RearBrakeTorqueNm;
			double& Nm = State.EscBrakeNm[WheelIndex];
			Nm = MoveToward(Nm, Target[WheelIndex], Dt * Full / (Target[WheelIndex] > Nm ? EscBuildS : EscReleaseS));
			bActing |= Nm > EscActingBrakeNm;
		}
		const double CutTarget = bEsc ? FMath::Clamp(EscCutPerOverRadPerS * State.EscOversteer + EscCutPerUnderRadPerS * State.EscUndersteer, 0.0, EscCutMax) : 0.0;
		State.EscCut = MoveToward(State.EscCut, CutTarget, Dt * (CutTarget > State.EscCut ? EscCutRisePerS : EscCutFallPerS));
		bActing |= State.EscCut > EscActingCut;
		// Regeneration off as soon as the car oversteers (a tenth of a rad/s past the dead band takes it all).
		const double RegenTarget = bEsc ? FMath::Clamp(EscRegenCutPerOverRadPerS * State.EscOversteer, 0.0, 1.0) : 0.0;
		State.EscRegenCut = MoveToward(State.EscRegenCut, RegenTarget, Dt * (RegenTarget > State.EscRegenCut ? EscCutRisePerS : EscCutFallPerS));
		State.EscLightS = bActing ? EscLightHoldS : FMath::Max(0.0, State.EscLightS - Dt);
		State.bEscActing = bActing;
	}

	const bool bThrottleAid = Car.bTractionControlEnabled;
	if (!bThrottleAid || Throttle <= 0.0)
	{
		State.TcCut = 0.0;
		State.TcIntegral = 0.0;
		State.TcSlip = 0.0;
		State.TcSlipFiltered = 0.0;
	}
	const double PedalThrottle = Throttle;
	State.PedalThrottle = PedalThrottle;
	State.SoundThrottle = Throttle;
	State.SoundCombustion = 0.0;
	State.SoundShaftLoad = 0.0;
	const bool bElectric = IsElectric();
	const double Ds = Dt / Substeps;
	const double SubstepWeight = 1.0 / Substeps;
	State.HandbrakeAmount = MoveToward(State.HandbrakeAmount, Controls.bHandbrake ? 1.0 : 0.0,
		Dt / (Controls.bHandbrake ? HandbrakeApplyS : HandbrakeReleaseS));
	// An electric parking brake at speed brakes all four wheels through ABS (and ESC), as the C8's and
	// Model 3's do when the switch is held; parked (under 2 m/s) it holds the rears as a lever does.
	const bool bElectricPark = Car.ParkingBrakeType == TEXT("electric") && FMath::Abs(Speed) > 2.0;
	if (bElectricPark && State.HandbrakeAmount > 0.0)
	{
		BrakeAmount = FMath::Max(BrakeAmount, 0.6 * State.HandbrakeAmount);
	}
	const double ParkingBrakeNm = bElectricPark ? 0.0
		: State.HandbrakeAmount * Car.ParkingBrakeTorqueNm;
	double AxleSpeed = 0.0;

	for (int32 Substep = 0; Substep < Substeps; ++Substep)
	{
		// Traction control: torque reduction from how far the most-slipping
		// driven wheel is past its target slip.
		if (bThrottleAid && PedalThrottle > 0.0)
		{
			double Ratio = 0.0;
			for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
			{
				const FOsdsWheelState& Wheel = State.Wheels[WheelIndex];
				if (!Car.IsDrivenWheel(WheelIndex)) continue;
				if (Wheel.LoadN > 0.0)
				{
					Ratio = FMath::Max(Ratio, Wheel.TractionControlSlip * SignDirection /
						FMath::Max(FMath::Max(Car.TractionControlTargetSlipRatio, Wheel.PeakSlip), 0.01));
				}
				else
				{
					// A driven wheel off the ground (the Corvette beached on a wall end at Doraville,
					// rear wheels free: 6600 rpm in first at 0 mph, full pedal). The real system reads
					// wheel-speed sensors, not tyre force: a wheel turning faster than the car moves
					// is slip, with or without load (3 m/s floor, as a sensor's reference speed).
					const double Surface = Wheel.AngularSpeedRadPerSecond * TireRadiusM * SignDirection;
					const double Ground = Wheel.ForwardSpeedMps * SignDirection;
					const double FreeSlip = (Surface - Ground) / FMath::Max(FMath::Abs(Ground), 3.0);
					Ratio = FMath::Max(Ratio, FreeSlip / FMath::Max(Car.TractionControlTargetSlipRatio, 0.01));
				}
			}
			State.TcSlipFiltered += (Ratio - State.TcSlipFiltered) * (1.0 - FMath::Exp(-Ds / TcFilterS));
			State.TcSlip = MoveToward(State.TcSlip, State.TcSlipFiltered, TcSlipSlewPerSecond * Ds);
			const double Excess = State.TcSlip - 1.0;
			if (State.ShiftTimeS <= 0.0)
			{
				// While the clutch slips at its capacity the wheels take the
				// flywheel, not the throttle: cutting harder would not help.
				const double IntegralStep = Ds * Car.TractionControlIntegralGainPerSecond * FMath::Clamp(Excess, -1.0, 0.5);
				if (!State.bClutchSaturated || IntegralStep < 0.0)
				{
					State.TcIntegral = FMath::Clamp(State.TcIntegral + IntegralStep, 0.0, 0.9);
				}
				const double Wanted = FMath::Clamp(State.TcIntegral + TcProportional * FMath::Clamp(Excess, -1.0, 2.0) +
					TcOvershoot * FMath::Max(Excess - 1.0, 0.0), 0.0, 0.9);
				State.TcCut = FMath::Clamp(Wanted, State.TcCut - TcCutRateDown * Ds,
					State.TcCut + (State.bClutchSaturated ? 0.0 : TcCutRateUp * Ds));
			}
			Throttle = PedalThrottle * (1.0 - State.TcCut);
		}
		// Stability control's engine cut, on top of traction control's.
		if (State.EscCut > 0.0)
		{
			Throttle = PedalThrottle * (1.0 - (bThrottleAid && PedalThrottle > 0.0 ? State.TcCut : 0.0)) * (1.0 - State.EscCut);
		}

		double ClutchLoad = 0.0;
		// Torque the gearbox input receives: the clutch's, multiplied by a slipping torque converter.
		double OutputLoad = 0.0;
		if (bElectric)
		{
			ClutchLoad = MotorTorqueNm(Throttle, ForwardRatio, SignDirection, Speed);
			State.SoundCombustion += FMath::Abs(ClutchLoad) / (Substeps * FMath::Max(Car.ElectricMotorPeakTorqueNm, 1.0));
			State.SoundShaftLoad += ClutchLoad / (Substeps * FMath::Max(Car.ElectricMotorPeakTorqueNm, 1.0));
		}
		else
		{
			State.EngineRpm = State.EngineOmegaRadPerSecond * 60.0 / Tau;
			const double Rpm = State.EngineRpm;
			const double Friction = Car.EngineFrictionTorqueNm + Car.EngineFrictionTorqueNmPerRpm * Rpm;
			// The pedal opens the throttle plate; the cylinders fill by how much air that opening
			// passes against what the engine draws at this speed (OsdsDynamics::EngineLoad).
			const double Load = OsdsDynamics::EngineLoad(Car, Throttle, Rpm);
			// The cylinders burn the air they take in: a naturally aspirated engine's indicated torque
			// (the published brake torque plus its friction) follows the air's density, against the
			// 1.225 kg/m3 the cars are calibrated in. Denver's 1600 m takes about 15 % (the rule of
			// thumb, about 3 % per 1000 ft); a turbo holds its boost.
			const double AirCharge = Car.bTurbocharged ? 1.0 : FMath::Clamp(AirDensityKgPerM3 / 1.225, 0.5, 1.2);
			double Torque = Rpm < Car.FuelCutSpeedRpm ? (OsdsDynamics::SampleEngineTorqueNm(Car, Rpm) + Friction) * AirCharge * Load : 0.0;
			double Drag = Friction * (Rpm > Car.IdleSpeedRpm ? 1.0 : 0.5);
			// A closed throttle also pumps against manifold vacuum; at full throttle that loss is
			// already in the published curve.
			if (Rpm > Car.IdleSpeedRpm)
			{
				Drag += (Car.EnginePumpingTorqueNm + Car.EnginePumpingTorqueNmPerRpm * Rpm) * (1.0 - Load);
			}
			double Governor = FMath::Clamp((Car.GovernorSpeedRpm - Rpm) * 0.8, 0.0, 100.0);
			AxleSpeed = DrivenOmega() * SignDirection;
			// The clutch (or converter, dual clutch) couples engine and driven
			// wheels in proportion to their speed difference, never harder than
			// the torque that closes the gap in one substep. In low gears that cap
			// binds below clutch_stiffness, so it is part of each car's
			// calibrated launch: it is evaluated at the substep the cars were
			// calibrated with (Godot's 1/360 s), not the actual one, which keeps
			// the clutch the same at every physics rate and is stable for any
			// substep up to that length.
			const double Reflected = ForwardRatio * ForwardRatio * Car.DrivelineEfficiencyFraction /
				(I * (Car.DrivetrainLayout == TEXT("awd") ? 4.0 : 2.0));
			const double Coupling = FMath::Min(Car.ClutchCouplingNmPerRadPerSecond,
				1.0 / (FMath::Max(Ds, ClutchCalibrationSubstepS) * (1.0 / Car.EngineRotationalInertiaKgM2 + Reflected)));
			ClutchLoad = FMath::Clamp((State.EngineOmegaRadPerSecond - AxleSpeed * ForwardRatio) * Coupling,
				-Car.ClutchBackTorqueNm, Car.ClutchCapacityNm);
			// The launch speed follows the pedal, not the torque traction control leaves.
			const double LaunchTargetRpm = State.bLaunching ? Car.LaunchControlRpm
				: Car.LaunchSpeedRpm + PedalThrottle * Car.LaunchSpeedRpmPerThrottleFraction;
			if (State.bManual)
			{
				const double Capacity = Car.ClutchCapacityNm * (1.0 - SmoothStep(0.16, 0.88, State.ClutchPedal));
				// A dry clutch with the pedal up locks: below its capacity it carries whatever torque
				// keeps engine and gearbox together. That stiffness is the most a substep can take
				// without overshooting (the 1/(dt I) cap the automatic's coupling uses); slipping
				// with the pedal part-way down it stays the calibrated viscous coupling.
				const double LockCoupling = 1.0 / (FMath::Max(Ds, ClutchCalibrationSubstepS) * (1.0 / Car.EngineRotationalInertiaKgM2 + Reflected));
				const double Engaged = 1.0 - SmoothStep(0.02, 0.16, State.ClutchPedal);
				const double ManualCoupling = FMath::Lerp(FMath::Min(Car.ManualClutchCouplingNmPerRadPerSecond, LockCoupling), LockCoupling, Engaged);
				// Locked, the clutch also carries this substep's engine torque, so engine and
				// gearbox end the substep at one speed instead of slipping by torque / stiffness.
				// A stalled engine makes no torque and has no governor: settle that first, so the
				// locked clutch does not carry torque the engine no longer makes.
				if (State.bStalled)
				{
					Torque = 0.0;
					Governor = 0.0;
					Drag = FMath::Min(26.0, State.EngineOmegaRadPerSecond * 0.85);
				}
				const double Gap = State.EngineOmegaRadPerSecond - AxleSpeed * ForwardRatio;
				const double EngineAhead = Engaged * (Torque + Governor - Drag) * Ds / Car.EngineRotationalInertiaKgM2;
				ClutchLoad = State.SelectedGear == 0 ? 0.0 : FMath::Clamp((Gap + EngineAhead) * ManualCoupling, -Capacity, Capacity);
			}
			// Pulling away the clutch slips to hold the launch speed: in first (and reverse) only,
			// unless a torque converter does it (it slips in any gear). It held every gear: in 3rd at
			// 36 mph the Mustang's clutch slipped its engine to 4672 rpm against the road's 1900.
			else if ((State.Gear == 0 || State.bInReverse || Car.TransmissionKind == TEXT("automatic")) &&
				AxleSpeed * ForwardRatio * 60.0 / Tau < LaunchTargetRpm && ClutchLoad > 0.0)
			{
				ClutchLoad = FMath::Min(ClutchLoad, FMath::Max(0.0, Torque - Drag + (Rpm - LaunchTargetRpm) * 0.16));
			}
			// Launch control armed: the clutch is open and the engine is held at its launch speed.
			if (State.bLaunchArmed)
			{
				ClutchLoad = 0.0;
				if (Rpm > Car.LaunchControlRpm)
				{
					Torque = 0.0;
				}
			}
			// A gear change interrupts the drive, except on a dual clutch.
			if (State.ShiftTimeS > 0.0 && Car.TransmissionKind != TEXT("dct"))
			{
				ClutchLoad = 0.0;
				Torque *= 0.12;
			}
			if (!State.bManual && Throttle < 0.01 && Rpm < Car.IdleSpeedRpm + 200.0 && FMath::Abs(Speed) < 3.0)
			{
				ClutchLoad = 0.0;
			}
			// An automatic with the handbrake pulled at speed: the torque converter slips (the engine at idle
			// cannot hold the locked rear wheels round), as the auto clutch opens in manual mode.
			if (!State.bManual && State.HandbrakeAmount > 0.3 && FMath::Abs(Speed) > 2.0 && Car.ParkingBrakeType != TEXT("electric"))
			{
				ClutchLoad = 0.0;
			}
			OutputLoad = ClutchLoad;
			// A torque converter (automatics only, not in manual mode) multiplies the torque it
			// passes while the turbine runs slower than the engine: stall_ratio at a standstill,
			// falling linearly to 1 at the coupling point (typical 1.8-2.4 and 0.85-0.9).
			if (!State.bManual && Car.TransmissionKind == TEXT("automatic") && Car.TorqueConverterStallRatio > 1.0 && ClutchLoad > 0.0)
			{
				const double SpeedRatio = FMath::Clamp(AxleSpeed * ForwardRatio / FMath::Max(State.EngineOmegaRadPerSecond, 1.0), 0.0, 1.0);
				const double CouplingPoint = FMath::Max(Car.TorqueConverterCouplingSpeedRatio, 0.1);
				OutputLoad = ClutchLoad * (1.0 + (Car.TorqueConverterStallRatio - 1.0) * FMath::Max(0.0, 1.0 - SpeedRatio / CouplingPoint));
			}
			State.SoundCombustion += (Torque + Governor) / (Substeps * 180.0);
			State.SoundShaftLoad += ClutchLoad / (Substeps * 180.0);
			State.EngineOmegaRadPerSecond = FMath::Clamp(
				State.EngineOmegaRadPerSecond + (Torque + Governor - Drag - ClutchLoad) * Ds / Car.EngineRotationalInertiaKgM2,
				State.bManual ? 0.0 : Car.MinimumAutomaticEngineSpeedRadPerSecond,
				Car.MaximumEngineSpeedRadPerSecond);
			if (State.bManual && !State.bStalled && State.EngineOmegaRadPerSecond < Car.StallEngineSpeedRadPerSecond)
			{
				State.bStalled = true;
			}
		}
		State.bClutchSaturated = !bElectric && ClutchLoad >= Car.ClutchCapacityNm * 0.98;
		State.ClutchTorqueNm = ClutchLoad;

		if (bElectric)
		{
			OutputLoad = ClutchLoad;
		}
		const double AxleTorque = OutputLoad * ForwardRatio * Car.DrivelineEfficiencyFraction * SignDirection;
		double WheelOmegas[4];
		for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			WheelOmegas[WheelIndex] = State.Wheels[WheelIndex].AngularSpeedRadPerSecond;
		}
		double DriveTorque[4];
		OsdsDynamics::SplitDriveTorqueToWheels(Car, AxleTorque, WheelOmegas, Ds,
			bElectric ? FMath::Abs(ClutchLoad) > 0.0 : ClutchLoad > 0.0, DriveTorque);

		for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			FOsdsWheelState& Wheel = State.Wheels[WheelIndex];
			FContactRow& Row = Rows[WheelIndex];
			const double RearPedal = OsdsDynamics::RearBrakePedal(Car, BrakeAmount);
			double BrakeNm = WheelIndex < 2 ? BrakeAmount * Car.FrontBrakeTorqueNm : RearPedal * Car.RearBrakeTorqueNm;
			// Stability control's pump adds pressure at one wheel, up to that wheel's full brake; ABS
			// below still keeps the wheel from locking.
			BrakeNm = FMath::Min(BrakeNm + State.EscBrakeNm[WheelIndex], WheelIndex < 2 ? Car.FrontBrakeTorqueNm : Car.RearBrakeTorqueNm);
			const double RowSpeed = Row.bValid ? Row.ForwardSpeed : 0.0;
			// ABS: a per-wheel pressure modulator cycling each tyre about its peak slip.
			if (Car.bAbsEnabled && BrakeNm > 0.0 && FMath::Abs(RowSpeed) > 1.5)
			{
				const double Target = Wheel.PeakSlip * (WheelIndex < 2 ? AbsFrontTargetOverPeak : AbsTargetOverPeak);
				// Braking slip is positive as the wheel turns slower than the road
				// in the direction of travel, forwards or in reverse.
				const double BrakingSlip = -Wheel.SlipRatio * FMath::Sign(RowSpeed);
				const double SlipError = (Target - BrakingSlip) / Target;
				Wheel.AbsRelease = FMath::Clamp(Wheel.AbsRelease + Ds * AbsGainPerSecond * FMath::Clamp(SlipError, -3.0, 1.0), 0.05, 1.0);
				BrakeNm *= FMath::Clamp(Wheel.AbsRelease + AbsProportional * FMath::Clamp(SlipError, -1.0, 0.0), 0.0, 1.0);
			}
			else
			{
				Wheel.AbsRelease = 1.0;
			}
			if (WheelIndex > 1)
			{
				BrakeNm = FMath::Max(BrakeNm, ParkingBrakeNm);
			}
			const double Spin = MoveToward(Wheel.AngularSpeedRadPerSecond + DriveTorque[WheelIndex] * Ds / I, 0.0, BrakeNm * Ds / I);
			if (!Row.bValid)
			{
				Wheel.AngularSpeedRadPerSecond = Spin;
				continue;
			}

			FOsdsLongitudinalSubstepInput Sub;
			Sub.NormalLoadN = Row.Load;
			Sub.FrictionCoefficient = Row.Mu;
			Sub.ForwardSpeedMps = Row.ForwardSpeed;
			Sub.LateralSpeedMps = Row.LateralSpeed;
			Sub.LateralForceN = Row.SideForce;
			Sub.StartAngularSpeedRadPerSecond = Wheel.AngularSpeedRadPerSecond;
			Sub.SpinAfterTorqueRadPerSecond = Spin;
			Sub.BrakeTorqueNm = BrakeNm;
			Sub.TireRadiusM = R;
			Sub.WheelInertiaKgM2 = I;
			Sub.SubstepS = Ds;
			Sub.LateralSlip = Row.SlipY;
			Sub.GripScale = Row.GripScale;
			const FOsdsLongitudinalSubstepOutput Out = OsdsDynamics::SolveLongitudinalSubstep(Car, Sub);
			Wheel.SlipRatio = Out.SlipRatio;
			Wheel.TractionControlSlip = Out.TractionControlSlip;
			Wheel.PeakSlip = Out.PeakSlipRatio;
			Wheel.GripUse = Out.GripUseFraction;
			Wheel.AngularSpeedRadPerSecond = Out.EndAngularSpeedRadPerSecond;
			Row.MeanLongitudinal += Out.LongitudinalForceN * SubstepWeight;
			Row.MeanLateral += Out.LateralForceN * SubstepWeight;
		}
	}

	// Forces at each contact patch, then aero and rolling resistance at the
	// centre of mass.
	State.RackAligningTorqueNm = 0.0;
	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		FOsdsWheelState& Wheel = State.Wheels[WheelIndex];
		Wheel.SpinAngleRad = FMath::Fmod(Wheel.SpinAngleRad + Wheel.AngularSpeedRadPerSecond * Dt, Tau);
		const FContactRow& Row = Rows[WheelIndex];
		if (!Row.bValid)
		{
			Wheel.AligningTorqueNm = 0.0;
			continue;
		}
		Wheel.LongitudinalForceN = Row.MeanLongitudinal;
		Wheel.LateralForceN = Row.MeanLateral;
		// Aligning torque: the side force acts a trail behind the steering axis (the tyre's
		// pneumatic trail plus the caster's R sin(caster)); a force to the right steers the
		// wheel to the left. Fades in from walking pace, where the trail has no meaning.
		{
			const double Trail = OsdsDynamics::TmeasyPneumaticTrailM(Car, Row.Load, FMath::Tan(FMath::Clamp(Wheel.SlipAngleRad, -1.4, 1.4)))
				+ (WheelIndex < 2 ? R * FMath::Sin(CasterRad) : 0.0);
			Wheel.AligningTorqueNm = -Trail * Row.MeanLateral * SmoothStep(0.5, 3.0, FMath::Abs(Row.ForwardSpeed));
			if (WheelIndex < 2)
			{
				State.RackAligningTorqueNm += Wheel.AligningTorqueNm;
			}
		}
		const FVector3d Force = Row.Normal * Row.Load + Row.WheelForward * Row.MeanLongitudinal + Row.WheelRight * Row.MeanLateral;
		OutForces.bWheelActive[WheelIndex] = true;
		OutForces.WheelPointM[WheelIndex] = Row.Point;
		OutForces.WheelForceN[WheelIndex] = Force;
		OutForces.ForceN += Force;
		OutForces.TorqueNm += (Row.Point - Body.CenterOfMassM).Cross(Force);
	}
	// Drag on the speed through the air: a headwind adds to it, a tailwind takes from it, a
	// crosswind pushes the car sideways (at the centre of mass; no yaw moment from it yet).
	const FVector3d AirVelocity = Velocity - WindMps;
	const double AirSpeed = AirVelocity.Size();
	if (AirSpeed > 0.02)
	{
		OutForces.ForceN += -AirVelocity / AirSpeed * 0.5 * AirDensityKgPerM3 * Car.DragAreaM2 * AirSpeed * AirSpeed;
	}
	const double SpeedMagnitude = Velocity.Size();
	if (SpeedMagnitude > 0.02)
	{
		const FVector3d Rolling(Velocity.X, Velocity.Y, 0.0);
		const double RollingSpeed = Rolling.Size();
		if (RollingSpeed > 0.01 && State.WheelContactCount > 0)
		{
			OutForces.ForceN += -Rolling / RollingSpeed *
				FMath::Min(RollingSpeed * MassKg / Dt, MassKg * OsdsDynamics::GravityMps2 * Car.RollingResistanceCoefficient);
		}
	}
	State.EngineRpm = State.EngineOmegaRadPerSecond * 60.0 / Tau;
}
