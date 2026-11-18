// Copyright OSDS contributors. All Rights Reserved.

#include "Vehicle/OsdsTuning.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Vehicle/OsdsVehicleDynamics.h"

namespace
{
	double Axle(const TArray<double>& Values, int32 Index, double Fallback = 0.0)
	{
		return Values.IsValidIndex(Index) ? Values[Index] : Fallback;
	}

	constexpr double LbInToNm = 175.126835;
	constexpr double MphToMps = 0.44704;

	bool IsElectric(const FOsdsCarDefinition& Car)
	{
		return Car.EngineKind == TEXT("electric");
	}

	/** The highest engine speed the car's power counts to: the fuel cut, or the motor's top speed. */
	double TopRpm(const FOsdsCarDefinition& Car)
	{
		if (IsElectric(Car))
		{
			return Car.ElectricMotorMaximumSpeedRpm > 0.0 ? Car.ElectricMotorMaximumSpeedRpm : 16000.0;
		}
		return Car.FuelCutSpeedRpm;
	}

	/** 0 at or below From, 1 at or past To, smooth between. */
	double Ramp(double X, double From, double To)
	{
		if (To <= From + 1.0)
		{
			return X > From ? 1.0 : 0.0;
		}
		const double T = FMath::Clamp((X - From) / (To - From), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** The driven axle whose differential a limited-slip row tunes: 1 rear, 0 front, -1 none. */
	int32 LsdAxle(const FOsdsCarDefinition& Car)
	{
		if (Car.DrivetrainLayout == TEXT("fwd"))
		{
			return Car.FrontDifferentialBiasRatio > 1.001 ? 0 : -1;
		}
		if (Car.RearDifferentialBiasRatio > 1.001)
		{
			return 1;
		}
		return Car.DrivetrainLayout == TEXT("awd") && Car.FrontDifferentialBiasRatio > 1.001 ? 0 : -1;
	}

	/** Combustion curve shaped for a tuning: the whole curve times TorqueScale, and above the
	 *  real torque peak a further (1 + Tilt) blended in up to the real power peak (the top end). */
	void ShapeCurve(FOsdsCarDefinition& Car, const FOsdsCarDefinition& Real, const FOsdsTuning::FPeaks& RealPeaks,
		double TorqueScale, double Tilt, double RevLimitRpm)
	{
		const double From = RealPeaks.TorqueRpm;
		const double To = FMath::Max(RealPeaks.PowerRpm, From + 500.0);
		Car.EngineTorqueCurve = Real.EngineTorqueCurve;
		// A rev limit past the end of the curve carries it on along its last slope (falling, as
		// past a power peak), to the new limit and a little past it.
		const int32 N = Car.EngineTorqueCurve.Num();
		if (N >= 2 && RevLimitRpm > Car.EngineTorqueCurve.Last().EngineSpeedRpm)
		{
			const FOsdsTorqueCurvePoint A = Car.EngineTorqueCurve[N - 2];
			const FOsdsTorqueCurvePoint B = Car.EngineTorqueCurve[N - 1];
			const double Slope = FMath::Min((B.EngineTorqueNm - A.EngineTorqueNm) / FMath::Max(B.EngineSpeedRpm - A.EngineSpeedRpm, 1.0), 0.0);
			const double End = RevLimitRpm + 200.0;
			Car.EngineTorqueCurve.Add({ End, FMath::Max(B.EngineTorqueNm + Slope * (End - B.EngineSpeedRpm), 0.6 * B.EngineTorqueNm) });
			Car.TorquePastCurveNm = FMath::Min(Real.TorquePastCurveNm, Car.EngineTorqueCurve.Last().EngineTorqueNm);
		}
		for (FOsdsTorqueCurvePoint& P : Car.EngineTorqueCurve)
		{
			P.EngineTorqueNm *= Real.EngineTorqueScale * TorqueScale * FMath::Max(0.05, 1.0 + Tilt * Ramp(P.EngineSpeedRpm, From, To));
		}
		Car.TorquePastCurveNm *= Real.EngineTorqueScale * TorqueScale * FMath::Max(0.05, 1.0 + Tilt);
		Car.EngineTorqueScale = 1.0;
	}

	double RoundLevel(double Level)
	{
		return FMath::Clamp(FMath::RoundToDouble(Level), 0.0, 2.0);
	}
}

double FOsdsTuning::TorqueAtRpm(const FOsdsCarDefinition& Car, double Rpm)
{
	if (IsElectric(Car))
	{
		const double Omega = FMath::Max(Rpm / OsdsTuningUnits::RpmPerRadPerS, 1.0);
		return FMath::Min(Car.ElectricMotorPeakTorqueNm, Car.ElectricMotorPeakPowerW / Omega) * Car.EngineTorqueScale;
	}
	return OsdsDynamics::SampleEngineTorqueNm(Car, Rpm);
}

FOsdsTuning::FPeaks FOsdsTuning::EnginePeaks(const FOsdsCarDefinition& Car)
{
	FPeaks Peaks;
	const double Top = TopRpm(Car);
	const double Start = IsElectric(Car) ? 100.0 : FMath::Max(Car.IdleSpeedRpm, 500.0);
	for (double Rpm = Start; Rpm <= Top + 0.5; Rpm += 25.0)
	{
		const double T = TorqueAtRpm(Car, Rpm);
		const double P = T * Rpm / OsdsTuningUnits::RpmPerRadPerS;
		if (T > Peaks.TorqueNm + 1.0e-9) { Peaks.TorqueNm = T; Peaks.TorqueRpm = Rpm; }
		if (P > Peaks.PowerW + 1.0e-6) { Peaks.PowerW = P; Peaks.PowerRpm = Rpm; }
	}
	return Peaks;
}

FOsdsTuning FOsdsTuning::FromCar(const FOsdsCarDefinition& Car, double StockSteeringRatio)
{
	FOsdsTuning T;
	T.bElectric = IsElectric(Car);
	const FPeaks Peaks = EnginePeaks(Car);
	T.PeakTorqueLbFt = Peaks.TorqueNm / OsdsTuningUnits::NmPerLbFt;
	T.PeakPowerHp = Peaks.PowerW / OsdsTuningUnits::WattsPerHp;
	T.RevLimitRpm = T.bElectric ? 0.0 : Car.FuelCutSpeedRpm;
	T.FinalDrive = Car.FinalDriveRatio;
	T.NumGears = FMath::Min(Car.ForwardGearRatios.Num(), MaxGears);
	for (int32 G = 0; G < T.NumGears; ++G)
	{
		T.GearRatio[G] = Car.ForwardGearRatios[G];
	}
	// A car given as wheel rates (N/m) shows them as the equivalent coil at its lever ratio.
	for (int32 A = 0; A < 2; ++A)
	{
		double Coil = Axle(Car.CoilSpringRateLbfPerInByAxle, A);
		const double Wheel = Axle(Car.WheelSpringRateNPerMByAxle, A);
		if (Wheel > 0.0)
		{
			const double Lever = Axle(Car.SpringLeverRatioByAxle, A, 1.0);
			Coil = Wheel / LbInToNm / FMath::Max(Lever * Lever, 0.01);
		}
		(A == 0 ? T.FrontSpringLbIn : T.RearSpringLbIn) = Coil;
	}
	T.FrontCamberDeg = Axle(Car.StaticCamberDegByAxle, 0);
	T.RearCamberDeg = Axle(Car.StaticCamberDegByAxle, 1);
	T.FrontToeDeg = Car.ToeAngleDegByAxle.Num() == 2 ? Car.ToeAngleDegByAxle[0] : Car.ToeAngleDegPerWheel;
	T.RearToeDeg = Car.ToeAngleDegByAxle.Num() == 2 ? Car.ToeAngleDegByAxle[1] : Car.ToeAngleDegPerWheel;
	T.CasterDeg = Car.CasterAngleDeg;
	T.SteeringRatio = StockSteeringRatio;
	const double Brakes = Car.FrontBrakeTorqueNm + Car.RearBrakeTorqueNm;
	T.BrakeBiasFrontPct = Brakes > 0.0 ? 100.0 * Car.FrontBrakeTorqueNm / Brakes : 60.0;
	const int32 Lsd = LsdAxle(Car);
	T.bHasLsd = Lsd >= 0;
	T.DiffPowerLock = Lsd == 1 ? Car.RearDifferentialBiasRatio : Lsd == 0 ? Car.FrontDifferentialBiasRatio : 1.0;
	T.DiffCoastLock = Lsd == 1 ? Car.RearDifferentialCoastBiasRatio : 1.0;
	T.FrontTyrePsi = Car.TyrePressurePsiByAxle.IsValidIndex(0) ? Car.TyrePressurePsiByAxle[0] : 32.0;
	T.RearTyrePsi = Car.TyrePressurePsiByAxle.IsValidIndex(1) ? Car.TyrePressurePsiByAxle[1] : T.FrontTyrePsi;
	T.bHasEsc = Car.bStabilityControlEnabled;
	T.bHasTc = Car.bTractionControlEnabled;
	T.EscLevel = T.bHasEsc ? 2.0 : 0.0;
	T.TcLevel = T.bHasTc ? 2.0 : 0.0;
	ReadAdjustable(Car.Id, T);
	return T;
}

void FOsdsTuning::Clamp(const FOsdsTuning& Stock)
{
	// What the car has is the real car's, whatever a file said.
	NumGears = Stock.NumGears;
	bElectric = Stock.bElectric;
	bHasLsd = Stock.bHasLsd;
	bHasEsc = Stock.bHasEsc;
	bHasTc = Stock.bHasTc;
	bFrontCamberAdjustable = Stock.bFrontCamberAdjustable;
	bRearCamberAdjustable = Stock.bRearCamberAdjustable;
	bRearToeAdjustable = Stock.bRearToeAdjustable;
	bCasterAdjustable = Stock.bCasterAdjustable;

	// Power: -40 % to +40 % torque (a turbo kit's range, estimate); power follows within -25/+30 %
	// of what the torque change alone gives, and -40/+50 % of the real car's.
	PeakTorqueLbFt = FMath::Clamp(PeakTorqueLbFt, Stock.PeakTorqueLbFt * 0.6, Stock.PeakTorqueLbFt * 1.4);
	const double TorqueRatio = Stock.PeakTorqueLbFt > 0.0 ? PeakTorqueLbFt / Stock.PeakTorqueLbFt : 1.0;
	PeakPowerHp = FMath::Clamp(PeakPowerHp, Stock.PeakPowerHp * FMath::Max(0.6, 0.75 * TorqueRatio),
		Stock.PeakPowerHp * FMath::Min(1.5, 1.3 * TorqueRatio));
	// Rev limit: 1500 rpm down, 500 up (valve springs and rods are the real car's), never near idle.
	RevLimitRpm = bElectric ? 0.0 : FMath::Clamp(RevLimitRpm, FMath::Max(Stock.RevLimitRpm - 1500.0, 3000.0), Stock.RevLimitRpm + 500.0);
	FinalDrive = FMath::Clamp(FinalDrive, Stock.FinalDrive * 0.75, Stock.FinalDrive * 1.25);
	for (int32 G = 0; G < MaxGears; ++G)
	{
		if (G >= NumGears)
		{
			GearRatio[G] = 0.0;
			continue;
		}
		GearRatio[G] = FMath::Clamp(GearRatio[G], Stock.GearRatio[G] * 0.8, Stock.GearRatio[G] * 1.2);
		// Each gear taller than the one before it (3 % at least), so the gearbox still steps up.
		if (G > 0)
		{
			GearRatio[G] = FMath::Min(GearRatio[G], GearRatio[G - 1] * 0.97);
		}
	}

	FrontSpringLbIn = FMath::Clamp(FrontSpringLbIn, Stock.FrontSpringLbIn * 0.5, Stock.FrontSpringLbIn * 4.0);
	RearSpringLbIn = FMath::Clamp(RearSpringLbIn, Stock.RearSpringLbIn * 0.5, Stock.RearSpringLbIn * 4.0);
	FrontBumpPct = FMath::Clamp(FrontBumpPct, 50.0, 250.0);
	FrontReboundPct = FMath::Clamp(FrontReboundPct, 50.0, 250.0);
	RearBumpPct = FMath::Clamp(RearBumpPct, 50.0, 250.0);
	RearReboundPct = FMath::Clamp(RearReboundPct, 50.0, 250.0);
	FrontBarPct = FMath::Clamp(FrontBarPct, 0.0, 300.0);
	RearBarPct = FMath::Clamp(RearBarPct, 0.0, 300.0);
	FrontRideHeightMm = FMath::Clamp(FrontRideHeightMm, -60.0, 30.0);
	// Rake within 40 mm of level (the bump stops and the splitter): the rear follows the front.
	RearRideHeightMm = FMath::Clamp(FMath::Clamp(RearRideHeightMm, -60.0, 30.0), FrontRideHeightMm - 40.0, FrontRideHeightMm + 40.0);

	// Alignment only where the real car has an adjustment; ranges from the usual eccentric bolts and shims (estimate).
	FrontCamberDeg = bFrontCamberAdjustable ? FMath::Clamp(FrontCamberDeg, Stock.FrontCamberDeg - 3.0, Stock.FrontCamberDeg + 1.0) : Stock.FrontCamberDeg;
	RearCamberDeg = bRearCamberAdjustable ? FMath::Clamp(RearCamberDeg, Stock.RearCamberDeg - 3.0, Stock.RearCamberDeg + 1.0) : Stock.RearCamberDeg;
	FrontToeDeg = FMath::Clamp(FrontToeDeg, Stock.FrontToeDeg - 0.5, Stock.FrontToeDeg + 0.5);
	RearToeDeg = bRearToeAdjustable ? FMath::Clamp(RearToeDeg, Stock.RearToeDeg - 0.5, Stock.RearToeDeg + 0.5) : Stock.RearToeDeg;
	CasterDeg = bCasterAdjustable ? FMath::Clamp(CasterDeg, Stock.CasterDeg - 2.0, Stock.CasterDeg + 2.0) : Stock.CasterDeg;
	SteeringRatio = Stock.SteeringRatio > 0.0 ? FMath::Clamp(SteeringRatio, Stock.SteeringRatio * 0.6, Stock.SteeringRatio * 1.4) : 0.0;

	BrakeBiasFrontPct = FMath::Clamp(BrakeBiasFrontPct, FMath::Max(Stock.BrakeBiasFrontPct - 12.0, 40.0), FMath::Min(Stock.BrakeBiasFrontPct + 12.0, 85.0));
	BrakeForcePct = FMath::Clamp(BrakeForcePct, 70.0, 130.0);
	if (bHasLsd)
	{
		DiffPowerLock = FMath::Clamp(DiffPowerLock, 1.0, 5.0);
		DiffCoastLock = FMath::Clamp(DiffCoastLock, 1.0, DiffPowerLock);
	}
	else
	{
		DiffPowerLock = Stock.DiffPowerLock;
		DiffCoastLock = Stock.DiffCoastLock;
	}
	DiffPreloadNm = bHasLsd && HasDiffPreload() ? FMath::Clamp(DiffPreloadNm, 0.0, 200.0) : Stock.DiffPreloadNm;

	FrontTyrePsi = FMath::Clamp(FrontTyrePsi, 18.0, 40.0);
	RearTyrePsi = FMath::Clamp(RearTyrePsi, 18.0, 40.0);
	EscLevel = bHasEsc ? RoundLevel(EscLevel) : 0.0;
	TcLevel = bHasTc ? RoundLevel(TcLevel) : 0.0;
}

FOsdsCarDefinition FOsdsTuning::Apply(const FOsdsCarDefinition& Real, const FOsdsTuning& Stock) const
{
	FOsdsCarDefinition Car = Real;

	// ---- Power
	const double TorqueScale = Stock.PeakTorqueLbFt > 0.0 ? PeakTorqueLbFt / Stock.PeakTorqueLbFt : 1.0;
	if (bElectric)
	{
		Car.ElectricMotorPeakTorqueNm *= TorqueScale;
		Car.ElectricMotorPeakPowerW = PeakPowerHp * OsdsTuningUnits::WattsPerHp;
	}
	else
	{
		const double RevDelta = RevLimitRpm - Real.FuelCutSpeedRpm;
		const FPeaks RealPeaks = EnginePeaks(Real);
		if (RevDelta != 0.0)
		{
			Car.FuelCutSpeedRpm = RevLimitRpm;
			Car.MaximumGearSpeedRpm = Real.MaximumGearSpeedRpm + RevDelta;
			Car.MaximumEngineSpeedRadPerSecond = FMath::Max(Real.MaximumEngineSpeedRadPerSecond, Real.MaximumEngineSpeedRadPerSecond + RevDelta / OsdsTuningUnits::RpmPerRadPerS);
			// The automatic changes up as far below the new limit as below the old, and its guard
			// (the road speed that must carry the engine near the shift) comes down with a lower limit.
			Car.AutomaticUpshiftSpeedRpm = FMath::Min(Real.AutomaticUpshiftSpeedRpm + RevDelta, RevLimitRpm - 50.0);
			const double LimitRatio = FMath::Min(1.0, RevLimitRpm / FMath::Max(Real.FuelCutSpeedRpm, 1.0));
			Car.AutomaticUpshiftMinimumEngineSpeedRadPerSecond = Real.AutomaticUpshiftMinimumEngineSpeedRadPerSecond * LimitRatio;
			if (Car.AutomaticKickdownSpeedRpm > 0.0) Car.AutomaticKickdownSpeedRpm = FMath::Min(Car.AutomaticKickdownSpeedRpm, RevLimitRpm * 0.85);
			if (Car.AutomaticLightUpshiftSpeedRpm > 0.0) Car.AutomaticLightUpshiftSpeedRpm = FMath::Min(Car.AutomaticLightUpshiftSpeedRpm, RevLimitRpm * 0.9);
			if (Car.LaunchControlRpm > 0.0) Car.LaunchControlRpm = FMath::Min(Car.LaunchControlRpm, RevLimitRpm * 0.8);
		}

		// Peak power: the top-end tilt that gives it, by bisection (power rises with the tilt).
		// No tilt when the torque change alone gives the power asked for (the stock setup rebuilds
		// the real curve exactly).
		const double TargetW = PeakPowerHp * OsdsTuningUnits::WattsPerHp;
		ShapeCurve(Car, Real, RealPeaks, TorqueScale, 0.0, RevLimitRpm);
		if (FMath::Abs(EnginePeaks(Car).PowerW - TargetW) > 0.5 * OsdsTuningUnits::WattsPerHp)
		{
			double Low = -0.8, High = 3.0;
			for (int32 I = 0; I < 30; ++I)
			{
				const double Mid = 0.5 * (Low + High);
				ShapeCurve(Car, Real, RealPeaks, TorqueScale, Mid, RevLimitRpm);
				(EnginePeaks(Car).PowerW < TargetW ? Low : High) = Mid;
			}
			ShapeCurve(Car, Real, RealPeaks, TorqueScale, 0.5 * (Low + High), RevLimitRpm);
		}
	}
	// More torque than stock comes with a clutch that holds it (as an uprated clutch does): the
	// stock clutch would slip from about 124 % on the NB.
	const double NewPeak = EnginePeaks(Car).TorqueNm;
	const double OldPeak = EnginePeaks(Real).TorqueNm;
	Car.ClutchCapacityNm *= FMath::Max(1.0, OldPeak > 0.0 ? NewPeak / OldPeak : 1.0);
	Car.FinalDriveRatio = FinalDrive;
	for (int32 G = 0; G < NumGears && G < Car.ForwardGearRatios.Num(); ++G)
	{
		Car.ForwardGearRatios[G] = GearRatio[G];
	}

	// ---- Springs, dampers, bars, ride height
	const double Springs[2] = { FrontSpringLbIn, RearSpringLbIn };
	const double StockSprings[2] = { Stock.FrontSpringLbIn, Stock.RearSpringLbIn };
	const double Bump[2] = { FrontBumpPct / 100.0, RearBumpPct / 100.0 };
	const double Rebound[2] = { FrontReboundPct / 100.0, RearReboundPct / 100.0 };
	for (int32 A = 0; A < 2; ++A)
	{
		const double Scale = StockSprings[A] > 0.0 ? Springs[A] / StockSprings[A] : 1.0;
		if (Car.WheelSpringRateNPerMByAxle.IsValidIndex(A) && Car.WheelSpringRateNPerMByAxle[A] > 0.0)
		{
			Car.WheelSpringRateNPerMByAxle[A] *= Scale;
		}
		if (Car.CoilSpringRateLbfPerInByAxle.IsValidIndex(A))
		{
			Car.CoilSpringRateLbfPerInByAxle[A] = Springs[A];
		}
		// Dampers: the ratio law's critical fractions, or the dyno's forces, per direction.
		if (Car.DamperCompressionCriticalFractionByAxle.IsValidIndex(A)) Car.DamperCompressionCriticalFractionByAxle[A] *= Bump[A];
		if (Car.DamperReboundCriticalFractionByAxle.IsValidIndex(A)) Car.DamperReboundCriticalFractionByAxle[A] *= Rebound[A];
		if (Car.DamperDynoByAxle.IsValidIndex(A))
		{
			for (FOsdsDamperDynoPoint& P : Car.DamperDynoByAxle[A])
			{
				P.CompressionForceLbf *= Bump[A];
				P.ReboundForceLbf *= Rebound[A];
			}
		}
	}
	Car.AntiRollBarScaleByAxle = { FrontBarPct / 100.0, RearBarPct / 100.0 };
	// Ride height: the wheels hang from the mounts at the same static length, so moving the mounts
	// up in the body lowers the body (and its centre of mass) by as much over the road. The body's
	// collision box and centre of mass keep their places in the body. The mean moves every mount;
	// each axle's difference from it moves that axle's (the rake).
	const double Mean = 0.5 * (FrontRideHeightMm + RearRideHeightMm);
	const double Offset = Mean / 1000.0;
	Car.SuspensionMountHeightM -= Offset;
	Car.RideHeightOffsetM += Offset;
	Car.SuspensionMountOffsetMByAxle = { -(FrontRideHeightMm - Mean) / 1000.0, -(RearRideHeightMm - Mean) / 1000.0 };

	// ---- Alignment
	Car.StaticCamberDegByAxle = { FrontCamberDeg, RearCamberDeg };
	Car.ToeAngleDegByAxle = { FrontToeDeg, RearToeDeg };
	Car.CasterAngleDeg = CasterDeg;

	// ---- Brakes and differential
	if (BrakeForcePct != 100.0 || BrakeBiasFrontPct != Stock.BrakeBiasFrontPct)
	{
		const double Brakes = (Real.FrontBrakeTorqueNm + Real.RearBrakeTorqueNm) * BrakeForcePct / 100.0;
		Car.FrontBrakeTorqueNm = Brakes * BrakeBiasFrontPct / 100.0;
		Car.RearBrakeTorqueNm = Brakes - Car.FrontBrakeTorqueNm;
	}
	switch (LsdAxle(Real))
	{
	case 1:
		Car.RearDifferentialBiasRatio = DiffPowerLock;
		Car.RearDifferentialCoastBiasRatio = DiffCoastLock;
		break;
	case 0:
		Car.FrontDifferentialBiasRatio = DiffPowerLock;
		break;
	default:
		break;
	}

	// ---- Tyres and aids
	Car.TyrePressurePsiByAxle = { FrontTyrePsi, RearTyrePsi };
	if (Real.bStabilityControlEnabled)
	{
		Car.bStabilityControlEnabled = EscLevel >= 0.5;
		// Sport: twice the dead bands and side-slip limit before it brakes (an estimate of the
		// makers' sport modes, which let the car slide "a bit" more).
		Car.StabilityControlDeadbandScale = EscLevel < 1.5 ? 2.0 : 1.0;
	}
	if (Real.bTractionControlEnabled)
	{
		Car.bTractionControlEnabled = TcLevel >= 0.5;
		// Sport: more wheel slip before the torque comes down (estimate).
		Car.TractionControlTargetSlipRatio = TcLevel < 1.5 ? FMath::Max(2.0 * Real.TractionControlTargetSlipRatio, 0.25) : Real.TractionControlTargetSlipRatio;
	}
	return Car;
}

double FOsdsTuning::RollGradientDegPerG(const FOsdsCarDefinition& Car)
{
	// Per axle: the springs give k t^2 / 2 and the bar (its load moved across per metre of
	// difference in travel) K_bar t^2, in N m per radian of roll.
	double RollStiffness = 0.0;
	for (int32 A = 0; A < 2; ++A)
	{
		const double Track = A == 0 ? Car.FrontTrackM : Car.RearTrackM;
		const double Spring = OsdsDynamics::GetWheelSpringRateNPerM(Car, A);
		const double Bar = OsdsDynamics::GetAntiRollBarRateNPerM(Car, A);
		RollStiffness += Track * Track * (0.5 * Spring + Bar);
	}
	const double Weight = Car.MassKg * OsdsDynamics::GravityMps2;
	// The centre of mass over the road moves with the ride height.
	const double Arm = FMath::Max(0.05, Car.CenterOfGravityHeightM + Car.RideHeightOffsetM);
	const double Net = RollStiffness - Weight * Arm;
	return Net > 1.0 ? FMath::RadiansToDegrees(Weight * Arm / Net) : 99.0;
}

FOsdsTuning::FEstimate FOsdsTuning::EstimatePerformance(const FOsdsCarDefinition& Car)
{
	FEstimate Out;
	const int32 Gears = Car.ForwardGearRatios.Num();
	if (Gears == 0 || Car.MassKg <= 0.0)
	{
		return Out;
	}
	const bool bElectric = IsElectric(Car);
	const double G = OsdsDynamics::GravityMps2;
	const double R = FMath::Max(Car.TireFreeRadiusM(), 0.15);
	const double Mass = Car.MassKg;
	// Wheels, driveline and engine add about 5 % to the mass to be accelerated (estimate).
	const double Effective = Mass * 1.05;
	const double Limit = bElectric ? TopRpm(Car) : Car.FuelCutSpeedRpm * 0.99;
	// A launch slips the clutch at about the car's launch rpm; a converter multiplies torque at stall.
	const double LaunchRpm = bElectric ? 0.0 : FMath::Min(Car.LaunchSpeedRpm + Car.LaunchSpeedRpmPerThrottleFraction, Car.FuelCutSpeedRpm * 0.6);
	const double Mu = (Car.TyreModel == TEXT("tmeasy") ? Car.TmeasyPeakMuX : Car.PeakLongitudinalFrictionCoefficient);
	const bool bRwd = Car.DrivetrainLayout == TEXT("rwd");
	const bool bFwd = Car.DrivetrainLayout == TEXT("fwd");
	const double Share = bRwd ? 1.0 - Car.FrontWeightFraction : bFwd ? Car.FrontWeightFraction : 1.0;
	const double Grip = bRwd ? Axle(Car.AxleGripMultiplier, 1, 1.0) : bFwd ? Axle(Car.AxleGripMultiplier, 0, 1.0)
		: 0.5 * (Axle(Car.AxleGripMultiplier, 0, 1.0) + Axle(Car.AxleGripMultiplier, 1, 1.0));
	const double Shift = bElectric || Car.TransmissionKind == TEXT("dct") ? 0.05 : FMath::Max(Car.GearShiftDurationS, 0.05);

	auto DriveForce = [&](int32 Gear, double Speed, bool& bOver) -> double
	{
		const double Ratio = Car.ForwardGearRatios[Gear] * Car.FinalDriveRatio;
		const double WheelRpm = Speed / R * Ratio * OsdsTuningUnits::RpmPerRadPerS;
		bOver = WheelRpm > Limit;
		const double Rpm = FMath::Max(WheelRpm, LaunchRpm);
		double Torque = TorqueAtRpm(Car, FMath::Min(Rpm, Limit));
		if (Car.TorqueConverterStallRatio > 1.0 && Rpm > 0.0)
		{
			const double SpeedRatio = FMath::Clamp(WheelRpm / Rpm, 0.0, 1.0);
			const double Couple = FMath::Max(Car.TorqueConverterCouplingSpeedRatio, 0.1);
			Torque *= SpeedRatio < Couple ? FMath::Lerp(Car.TorqueConverterStallRatio, 1.0, SpeedRatio / Couple) : 1.0;
		}
		return bOver ? 0.0 : Torque * Ratio * Car.DrivelineEfficiencyFraction / R;
	};

	const double Dt = 0.005;
	double Speed = 0.0, Time = 0.0, ShiftLeft = 0.0, Accel = 0.0, Still = 0.0;
	int32 Gear = 0;
	while (Time < 200.0)
	{
		bool bOver = false;
		double Force = 0.0;
		if (ShiftLeft > 0.0)
		{
			ShiftLeft -= Dt;
		}
		else
		{
			Force = DriveForce(Gear, Speed, bOver);
			bool bNextOver = false;
			if (Gear + 1 < Gears && (bOver || DriveForce(Gear + 1, Speed, bNextOver) > Force))
			{
				++Gear;
				ShiftLeft = Shift;
				Force = 0.0;
			}
		}
		// The driven axle's grip, with the load moved onto (rear drive) or off (front) it as the car pulls.
		const double Transfer = Effective * Accel * Car.CenterOfGravityHeightM / FMath::Max(Car.WheelbaseM, 1.0);
		const double Load = Mass * G * Share + (bRwd ? Transfer : bFwd ? -Transfer : 0.0);
		Force = FMath::Min(Force, Mu * Grip * FMath::Max(Load, 0.0));
		const double Drag = 0.5 * 1.204 * Car.DragAreaM2 * Speed * Speed + Car.RollingResistanceCoefficient * Mass * G;
		Accel = (Force - Drag) / Effective;
		if (Speed <= 0.0 && Accel < 0.0) Accel = 0.0;
		Speed += Accel * Dt;
		Time += Dt;
		if (Out.ZeroToSixtyS <= 0.0 && Speed >= 60.0 * MphToMps)
		{
			Out.ZeroToSixtyS = Time;
		}
		if (Speed > Out.TopSpeedMps + 1.0e-3)
		{
			Out.TopSpeedMps = Speed;
			Still = 0.0;
		}
		else if (ShiftLeft <= 0.0 && (Still += Dt) > 3.0)
		{
			break;
		}
	}
	return Out;
}

FString FOsdsTuning::PathFor(const FString& CarId)
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OSDS"), FString::Printf(TEXT("tuning-%s.json"), *CarId));
}

void FOsdsTuning::ReadAdjustable(const FString& CarId, FOsdsTuning& InOut)
{
	// Every car's front toe is adjustable (the tie rods); the rest is per car.
	InOut.bFrontCamberAdjustable = InOut.bRearCamberAdjustable = InOut.bRearToeAdjustable = InOut.bCasterAdjustable = false;
	static TSharedPtr<FJsonObject> Table;
	static bool bRead = false;
	if (!bRead)
	{
		bRead = true;
		FString Text;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("tuning"), TEXT("adjustable.json"));
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Table);
		}
	}
	const TSharedPtr<FJsonObject>* Cars = nullptr;
	const TSharedPtr<FJsonObject>* Entry = nullptr;
	if (!Table.IsValid() || !Table->TryGetObjectField(TEXT("cars"), Cars) || !(*Cars)->TryGetObjectField(CarId, Entry))
	{
		return;
	}
	(*Entry)->TryGetBoolField(TEXT("front_camber"), InOut.bFrontCamberAdjustable);
	(*Entry)->TryGetBoolField(TEXT("rear_camber"), InOut.bRearCamberAdjustable);
	(*Entry)->TryGetBoolField(TEXT("rear_toe"), InOut.bRearToeAdjustable);
	(*Entry)->TryGetBoolField(TEXT("caster"), InOut.bCasterAdjustable);
}

namespace
{
	/** Every saved number by its file key. */
	struct FKeyed { const TCHAR* Key; double FOsdsTuning::* Field; };
	const FKeyed SavedFields[] = {
		{ TEXT("peak_torque_lb_ft"), &FOsdsTuning::PeakTorqueLbFt }, { TEXT("peak_power_hp"), &FOsdsTuning::PeakPowerHp },
		{ TEXT("rev_limit_rpm"), &FOsdsTuning::RevLimitRpm }, { TEXT("final_drive"), &FOsdsTuning::FinalDrive },
		{ TEXT("front_spring_lb_in"), &FOsdsTuning::FrontSpringLbIn }, { TEXT("rear_spring_lb_in"), &FOsdsTuning::RearSpringLbIn },
		{ TEXT("front_bump_pct"), &FOsdsTuning::FrontBumpPct }, { TEXT("front_rebound_pct"), &FOsdsTuning::FrontReboundPct },
		{ TEXT("rear_bump_pct"), &FOsdsTuning::RearBumpPct }, { TEXT("rear_rebound_pct"), &FOsdsTuning::RearReboundPct },
		{ TEXT("front_bar_pct"), &FOsdsTuning::FrontBarPct }, { TEXT("rear_bar_pct"), &FOsdsTuning::RearBarPct },
		{ TEXT("front_ride_height_mm"), &FOsdsTuning::FrontRideHeightMm }, { TEXT("rear_ride_height_mm"), &FOsdsTuning::RearRideHeightMm },
		{ TEXT("front_camber_deg"), &FOsdsTuning::FrontCamberDeg }, { TEXT("rear_camber_deg"), &FOsdsTuning::RearCamberDeg },
		{ TEXT("front_toe_deg"), &FOsdsTuning::FrontToeDeg }, { TEXT("rear_toe_deg"), &FOsdsTuning::RearToeDeg },
		{ TEXT("caster_deg"), &FOsdsTuning::CasterDeg }, { TEXT("steering_ratio"), &FOsdsTuning::SteeringRatio },
		{ TEXT("brake_bias_front_pct"), &FOsdsTuning::BrakeBiasFrontPct }, { TEXT("brake_force_pct"), &FOsdsTuning::BrakeForcePct },
		{ TEXT("diff_power_lock"), &FOsdsTuning::DiffPowerLock }, { TEXT("diff_coast_lock"), &FOsdsTuning::DiffCoastLock },
		{ TEXT("diff_preload_nm"), &FOsdsTuning::DiffPreloadNm },
		{ TEXT("front_tyre_psi"), &FOsdsTuning::FrontTyrePsi }, { TEXT("rear_tyre_psi"), &FOsdsTuning::RearTyrePsi },
		{ TEXT("esc_level"), &FOsdsTuning::EscLevel }, { TEXT("tc_level"), &FOsdsTuning::TcLevel },
	};
}

bool FOsdsTuning::Load(const FString& CarId)
{
	// Called on the stock setup: a key the file lacks keeps the real car's value.
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *PathFor(CarId)))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	for (const FKeyed& K : SavedFields)
	{
		Json->TryGetNumberField(K.Key, this->*(K.Field));
	}
	const TArray<TSharedPtr<FJsonValue>>* Gears = nullptr;
	if (Json->TryGetArrayField(TEXT("gear_ratios"), Gears) && Gears->Num() == NumGears)
	{
		for (int32 G = 0; G < NumGears; ++G)
		{
			GearRatio[G] = (*Gears)[G]->AsNumber();
		}
	}
	// Setups saved by 2.1 (one damping %, one ride height, a torque %).
	double Old = 0.0;
	if (!Json->HasField(TEXT("front_bump_pct")) && Json->TryGetNumberField(TEXT("damping_pct"), Old))
	{
		FrontBumpPct = FrontReboundPct = RearBumpPct = RearReboundPct = Old;
	}
	if (!Json->HasField(TEXT("front_ride_height_mm")) && Json->TryGetNumberField(TEXT("ride_height_mm"), Old))
	{
		FrontRideHeightMm = RearRideHeightMm = Old;
	}
	if (!Json->HasField(TEXT("peak_torque_lb_ft")) && Json->TryGetNumberField(TEXT("torque_pct"), Old))
	{
		PeakTorqueLbFt *= Old / 100.0;
		PeakPowerHp *= Old / 100.0;
	}
	return true;
}

bool FOsdsTuning::Save(const FString& CarId) const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const FKeyed& K : SavedFields)
	{
		Json->SetNumberField(K.Key, this->*(K.Field));
	}
	TArray<TSharedPtr<FJsonValue>> Gears;
	for (int32 G = 0; G < NumGears; ++G)
	{
		Gears.Add(MakeShared<FJsonValueNumber>(GearRatio[G]));
	}
	Json->SetArrayField(TEXT("gear_ratios"), Gears);
	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	const FString Path = PathFor(CarId);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	// Written whole and moved into place, so a crash never leaves half a file.
	const FString Temp = Path + TEXT(".tmp");
	return FFileHelper::SaveStringToFile(Text, *Temp) && IFileManager::Get().Move(*Path, *Temp, true, true);
}
