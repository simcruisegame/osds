// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/CarDefinition.h"

/** What the rest of the vehicle is doing; later Differential kinds (active lock, coupling, vectoring) read it. Unused by open, bias and fixed AWD. */
struct FOsdsDifferentialVehicleContext
{
	double Throttle = 0.0;
	double SpeedMps = 0.0;
	double YawRateRadPerSecond = 0.0;
	double LateralAccelMps2 = 0.0;
	double SteerRackRad = 0.0;
};

struct FOsdsDifferentialInput
{
	/** Total torque at the gearbox output (N m), already through ratio and driveline efficiency. */
	double AxleTorqueNm = 0.0;
	/** Wheel speeds: 0 front left, 1 front right, 2 rear left, 3 rear right (rad/s). */
	double WheelOmegaRadPerSecond[4] = { 0.0, 0.0, 0.0, 0.0 };
	double DeltaTimeS = 0.0;
	/** True when the engine drives the wheels, false on the over-run (engine braking). */
	bool bDrive = true;
	FOsdsDifferentialVehicleContext Vehicle;
};

struct FOsdsDifferentialOutput
{
	double WheelTorqueNm[4] = { 0.0, 0.0, 0.0, 0.0 };
	/** Extra brake torque per wheel the Differential asks for (brake vectoring); zero for the kinds so far. */
	double WheelBrakeNm[4] = { 0.0, 0.0, 0.0, 0.0 };
};

/**
 * The Differential: shares drive torque between the wheels and gives the driven speed back to the engine.
 * One instance per car; it keeps any per-car state itself. Kinds today: open, bias (Torsen-style, drive and
 * coast, per axle) and fixed-split AWD with a limited-slip centre.
 */
class OSDS_API FOsdsDifferential
{
public:
	/** Read the layout and bias numbers from the Car file; resets state. */
	void Configure(const FOsdsCarDefinition& Car);
	void Reset();

	void Step(const FOsdsDifferentialInput& Input, FOsdsDifferentialOutput& Output);

	/** The speed the engine's clutch sees (rad/s) for the given wheel speeds. */
	double DrivenOmega(const double WheelOmegaRadPerSecond[4]) const;

	/** How many wheels the engine turns through this Differential (2 or 4): sets the inertia the clutch sees. */
	double DrivenWheelCount() const { return Layout == ELayout::Awd ? 4.0 : 2.0; }

private:
	enum class ELayout : uint8 { Rwd, Fwd, Awd };

	ELayout Layout = ELayout::Rwd;
	double WheelInertiaKgM2 = 1.0;
	double AwdFrontShare = 0.5;
	double FrontBias = 1.0;
	double RearBias = 1.0;
	double RearCoastBias = 1.0;
	double CenterBias = 1.0;
};
