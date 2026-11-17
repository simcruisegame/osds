// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Vehicle/CarDefinition.h"

/** Results of godot-final:game/checks/CarTrack.gd's three tests. */
struct FSimCruiseCarTrackResult
{
	double ZeroToSixtyS = TNumericLimits<double>::Max();
	/** 0-60 mph timed as US magazines do: the clock starts after the first foot of travel (a drag strip's rollout). */
	double ZeroToSixtyRolloutS = TNumericLimits<double>::Max();
	double SeventyToZeroFt = 0.0;
	double SkidpadG = 0.0;
	bool bCompleted = false;
	bool bTimedOut = false;
	/** The car stopped harder than tyres can (over 5 g): it hit something on the pad. */
	bool bCollided = false;
	double SimSeconds = 0.0;
	int64 PhysicsSteps = 0;
	double MeanStepMicroseconds = 0.0;
	double WallSeconds = 0.0;
	/** Final body and wheel state, for determinism checks. */
	TArray<double> Fingerprint;
};

/**
 * Run CarTrack (0-60 mph from rest at full throttle in automatic, 70-0 mph
 * with the full service brake, ramp-steer skidpad at the 300 ft circle's
 * target speed) at the given fixed physics step.
 * MaxWheelSubstepS splits each physics step for the wheel/driveline integration.
 */
FSimCruiseCarTrackResult SimCruiseRunCarTrack(const FSimCruiseCarDefinition& Car, double FixedStepS,
	double MaxWheelSubstepS = 1.0 / 360.0, bool bAccelerationOnly = false);

/** One case of godot-final:game/checks/LiftOff.gd. */
struct FSimCruiseLiftOffCase
{
	double TargetG = 0.3;
	bool bBrake = false;
	/** A fixed brake pedal (0-1) instead of the 0.15 g trail brake, when 0 or more. */
	double BrakePedal = -1.0;
	/** The axle whose wheel locked first under braking (0 front, 1 rear, -1 none) and when (s). */
	int32 FirstLockAxle = -1;
	double FirstLockS = 0.0;
	bool bSteady = false;
	double SteadyLateralG = 0.0;
	int32 SteadyGear = 0;
	double SteadyBetaDeg = 0.0;
	/** Mean front road-wheel angle holding the steady circle (deg). */
	double SteadyRackDeg = 0.0;
	double YawRatio1s = 0.0;
	double YawOvershoot = 0.0;
	double Beta1sDeg = 0.0;
	double PeakBetaDeg = 0.0;
	double SettleS = 0.0;
	double DecelG = 0.0;
	bool bSpun = false;
	bool bReverseAtSpeed = false;
	/** Seconds stability control acted on the steady circle and after the lift (its dash light). */
	double EscCircleS = 0.0;
	double EscAfterS = 0.0;
	/** Peak filtered oversteer and understeer it saw on the circle (rad/s past its dead bands). */
	double EscPeakOver = 0.0;
	double EscPeakUnder = 0.0;
};

struct FSimCruiseLiftOffResult
{
	TArray<FSimCruiseLiftOffCase> Cases;
	bool bCompleted = false;
};

/** Run LiftOff on a RadiusM circle at each lateral level, lift and trail brake. */
FSimCruiseLiftOffResult SimCruiseRunLiftOff(const FSimCruiseCarDefinition& Car, double FixedStepS,
	const TArray<double>& LevelsG, double RadiusM = 40.0, bool bStabilityControl = true);

/** ISO 7975 braking in a turn: a steady LateralG circle, then each fixed brake pedal in turn. */
FSimCruiseLiftOffResult SimCruiseRunBrakeInTurn(const FSimCruiseCarDefinition& Car, double FixedStepS,
	const TArray<double>& Pedals, double LateralG = 0.5, double RadiusM = 100.0);

/** ISO 7401 step steer: straight at SpeedMps, then the road wheels stepped (0.15 s) to a lateral-g target. */
struct FSimCruiseStepSteerResult
{
	bool bCompleted = false;
	double SpeedMps = 0.0;
	double RackDeg = 0.0;
	double SteadyYawDegS = 0.0;
	double SteadyLateralG = 0.0;
	/** From 50 % of the steering step to 90 % of the steady yaw rate (s). */
	double YawResponseS = 0.0;
	/** Peak yaw rate over the steady one, less 1. */
	double YawOvershoot = 0.0;
	double PeakBetaDeg = 0.0;
	/** Body roll in the steady state over the steady lateral g (deg per g, positive: outward). */
	double RollDegPerG = 0.0;
	/** Seconds stability control acted (it should not, in a 0.4 g step). */
	double EscActiveS = 0.0;
	double EscPeakOver = 0.0;
	double EscPeakUnder = 0.0;
};

FSimCruiseStepSteerResult SimCruiseRunStepSteer(const FSimCruiseCarDefinition& Car, double FixedStepS,
	double SpeedMps = 80.0 / 3.6, double LateralG = 0.4);

/** FMVSS 126 / NHTSA sine with dwell, the US stability-control test: coasting from 80 km/h, one 0.7 Hz sine
 *  of steering with a 0.5 s dwell at its second peak, at 6.5 times the steer that gives 0.3 g. */
struct FSimCruiseSineDwellResult
{
	bool bCompleted = false;
	double AmplitudeDeg = 0.0;
	/** The yaw rate's peak after the reversal (deg/s), and the share of it left 1.0 s and 1.75 s
	 *  after the steering ends (the rule: at most 35 % and 20 %). */
	double PeakYawDegS = 0.0;
	double Ratio1s = 0.0;
	double Ratio175s = 0.0;
	/** Lateral move of the centre of mass 1.07 s after the steering starts (the rule: 1.83 m or more). */
	double LateralDisplacementM = 0.0;
	double PeakBetaDeg = 0.0;
	double EscActiveS = 0.0;
};

FSimCruiseSineDwellResult SimCruiseRunSineDwell(const FSimCruiseCarDefinition& Car, double FixedStepS, bool bStabilityControl);

/** Full throttle on a long flat pad, straight, until the speed stops rising: power against drag
 *  and rolling resistance (and the rev limit in top gear). */
struct FSimCruiseTopSpeedResult
{
	bool bCompleted = false;
	double TopSpeedMps = 0.0;
	int32 Gear = 0;
	double Rpm = 0.0;
	double SimSeconds = 0.0;
	double DistanceM = 0.0;
	/** Each gear change: time, gear, speed, engine and road rpm. */
	FString GearTrace;
};

FSimCruiseTopSpeedResult SimCruiseRunTopSpeed(const FSimCruiseCarDefinition& Car, double FixedStepS);

/** A magazine passing time: cruise steady at FromMps in the automatic's own gear for 4 s, then
 *  full pedal (kickdown allowed, as Car and Driver times an automatic) until ToMps. */
struct FSimCruisePassingResult
{
	bool bCompleted = false;
	double Seconds = 0.0;
	int32 CruiseGear = 0;
	int32 KickdownGear = 0;
};

FSimCruisePassingResult SimCruiseRunPassing(const FSimCruiseCarDefinition& Car, double FixedStepS, double FromMps, double ToMps);

/** The automatic on a road: full pedal, a moving pedal, braking down, full pedal again. */
struct FSimCruiseShiftDriveResult
{
	bool bCompleted = false;
	int32 Shifts = 0;
	/** Changes back the other way within 2 s of the last change (hunting). */
	int32 QuickReversals = 0;
	/** In gear with the clutch home: the largest gap of the engine from the road's speed (fraction). */
	double WorstRpmGap = 0.0;
	double LowestRoadRpm = 1.0e9;
	/** Where the worst gap was: time, gear, pedal, brake, engine and road rpm. */
	FString WorstAt;
	FString Trace;
	/** Time on the fuel cut at full pedal, s (a gearbox that will not change up). */
	double LimiterSeconds = 0.0;
	/** The longest unbroken time on the fuel cut at full pedal, s. */
	double LimiterLongestS = 0.0;
	/** When it was on the fuel cut (s, one entry per step). */
	FString LimiterAt;
};

FSimCruiseShiftDriveResult SimCruiseRunShiftDrive(const FSimCruiseCarDefinition& Car, double FixedStepS);

/** Static load balance after settling on the pad. */
struct FSimCruiseStaticLoadResult
{
	bool bCompleted = false;
	double LoadN[4] = { 0.0, 0.0, 0.0, 0.0 };
	double SuspensionLengthM[4] = { 0.0, 0.0, 0.0, 0.0 };
	double TotalLoadN = 0.0;
	double FrontFraction = 0.0;
	double PitchDeg = 0.0;
	double RollDeg = 0.0;
	double ResidualSpeedMps = 0.0;
	double GravityMps2 = 0.0;
};

FSimCruiseStaticLoadResult SimCruiseRunStaticLoad(const FSimCruiseCarDefinition& Car, double FixedStepS, double SettleS = 4.0);

/** The physics rate SimCruise ships with (matches Config/DefaultEngine.ini). */
inline constexpr double SimCruiseShippingFixedStepS = 1.0 / 360.0;

#endif
