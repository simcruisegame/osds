// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/CarDefinition.h"

/**
 * A driver's setup changes on top of the real car, in the units a workshop uses. Applied to a
 * copy of the car's definition (the car file never changes) and saved per car in
 * Saved/OSDS/tuning-<car id>.json. Each value has a safe range around the real car's (Clamp).
 *
 * Plain values only (no heap members), so a copy is the whole setup and tests can hash it.
 */
struct OSDS_API FOsdsTuning
{
	static constexpr int32 MaxGears = 10;

	// ---- Power
	/** Peak engine (or motor) torque, lb-ft: the whole curve scales with it. */
	double PeakTorqueLbFt = 0.0;
	/** Peak power, hp: the curve above the torque peak is shaped to give it (a cam and tune's top end). */
	double PeakPowerHp = 0.0;
	/** Rev limit (fuel cut), rpm; 0 for an electric motor. */
	double RevLimitRpm = 0.0;
	/** Final drive ratio. */
	double FinalDrive = 0.0;
	/** Each forward gear's ratio (NumGears of them). */
	double GearRatio[MaxGears] = {};

	// ---- Springs, dampers, bars, ride height
	/** Coil spring rates (lb/in), front and rear. */
	double FrontSpringLbIn = 0.0;
	double RearSpringLbIn = 0.0;
	/** Damper force, % of the real car's at every shaft speed: bump (compression) and rebound, front and rear. */
	double FrontBumpPct = 100.0;
	double FrontReboundPct = 100.0;
	double RearBumpPct = 100.0;
	double RearReboundPct = 100.0;
	/** Anti-roll bar rates, % of the real car's (0 = disconnected). */
	double FrontBarPct = 100.0;
	double RearBarPct = 100.0;
	/** Ride height change (mm, + higher), front and rear. */
	double FrontRideHeightMm = 0.0;
	double RearRideHeightMm = 0.0;

	// ---- Alignment (where the real car has the adjustment)
	/** Static camber, deg (negative: top of the tyre in). */
	double FrontCamberDeg = 0.0;
	double RearCamberDeg = 0.0;
	/** Toe per wheel, deg (+ toe-in). */
	double FrontToeDeg = 0.0;
	double RearToeDeg = 0.0;
	/** Caster, deg. */
	double CasterDeg = 0.0;
	/** Steering ratio (steering-wheel turns per road-wheel turn, :1); 0 when the car has none. */
	double SteeringRatio = 0.0;

	// ---- Brakes and differential
	/** Front share of the brake torque, %. */
	double BrakeBiasFrontPct = 0.0;
	/** Brake torque, % of the real car's (pads and master cylinder). */
	double BrakeForcePct = 100.0;
	/** Limited-slip lock as a torque bias ratio, on power and on the coast (1 = open). */
	double DiffPowerLock = 1.0;
	double DiffCoastLock = 1.0;
	/** Clutch-LSD preload, N m (hidden until the sim has a preload: HasDiffPreload). */
	double DiffPreloadNm = 0.0;

	// ---- Tyres and driver aids
	/** Cold tyre pressure (psi), front and rear; the car's placard pressure is stock. */
	double FrontTyrePsi = 32.0;
	double RearTyrePsi = 32.0;
	/** Stability control and traction control: 0 off, 1 Sport (later, wider), 2 on as the maker set it. */
	double EscLevel = 2.0;
	double TcLevel = 2.0;

	// ---- What this car has (set by FromCar, never saved): which rows exist and their ranges.
	int32 NumGears = 0;
	bool bElectric = false;
	bool bHasLsd = false;
	bool bHasEsc = false;
	bool bHasTc = false;
	bool bFrontCamberAdjustable = false;
	bool bRearCamberAdjustable = false;
	bool bRearToeAdjustable = false;
	bool bCasterAdjustable = false;

	/** The real car's values (the "reset" state). StockSteeringRatio comes from the car's model. */
	static FOsdsTuning FromCar(const FOsdsCarDefinition& Car, double StockSteeringRatio);

	/** Keep every value inside its safe range around the real car's. */
	void Clamp(const FOsdsTuning& Stock);

	/** The tuned car: Stock is the real car's tuning (FromCar), Real the real car's definition. */
	FOsdsCarDefinition Apply(const FOsdsCarDefinition& Real, const FOsdsTuning& Stock) const;

	/** Body roll per g of lateral acceleration (deg/g) from the springs, bars, track and centre of
	 *  mass height (the sim's roll axis runs through the contact patches). */
	static double RollGradientDegPerG(const FOsdsCarDefinition& Car);

	/** The engine's (or motor's) peaks from its curve: torque N m and rpm, power W and rpm. */
	struct FPeaks { double TorqueNm = 0.0; double TorqueRpm = 0.0; double PowerW = 0.0; double PowerRpm = 0.0; };
	static FPeaks EnginePeaks(const FOsdsCarDefinition& Car);
	/** Torque (N m) the car makes at an engine speed: the curve, or the motor's torque-and-power limits. */
	static double TorqueAtRpm(const FOsdsCarDefinition& Car, double Rpm);

	/** A quick straight-line estimate (point mass, ideal shifts, traction-limited): the menu's
	 *  "before you drive" numbers. Not the bench: compare a tuned car's with the stock car's. */
	struct FEstimate { double ZeroToSixtyS = 0.0; double TopSpeedMps = 0.0; };
	static FEstimate EstimatePerformance(const FOsdsCarDefinition& Car);

	/** Does the car's sim have a clutch-LSD preload yet (the Preload row shows only then)? */
	static bool HasDiffPreload() { return false; }

	bool Load(const FString& CarId);
	bool Save(const FString& CarId) const;
	static FString PathFor(const FString& CarId);

	/** Which alignment adjustments the real car has (data/tuning/adjustable.json; front toe always). */
	static void ReadAdjustable(const FString& CarId, FOsdsTuning& InOut);
};

namespace OsdsTuningUnits
{
	constexpr double NmPerLbFt = 1.3558179483;
	constexpr double WattsPerHp = 745.69987;
	constexpr double RpmPerRadPerS = 60.0 / UE_DOUBLE_TWO_PI;
}
