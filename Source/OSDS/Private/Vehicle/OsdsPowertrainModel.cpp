// Copyright OSDS contributors. All Rights Reserved.

#include "Vehicle/OsdsVehicleDynamics.h"
#include "Vehicle/OsdsDifferential.h"

#include "Math/UnrealMathUtility.h"

namespace OsdsDynamics
{
	FShiftPoints GetAutomaticShiftPoints(const FOsdsCarDefinition& Car, double PedalFraction)
	{
		const double FullUp = Car.AutomaticUpshiftSpeedRpm;
		const double LightUp = Car.AutomaticLightUpshiftSpeedRpm > 0.0
			? Car.AutomaticLightUpshiftSpeedRpm
			: FMath::Min(FullUp, FMath::Max(Car.IdleSpeedRpm * 2.5, FullUp * 0.36));
		const double MidDown = Car.AutomaticDownshiftSpeedRpm;
		const double LightDown = Car.AutomaticLightDownshiftSpeedRpm > 0.0
			? Car.AutomaticLightDownshiftSpeedRpm
			: FMath::Min(MidDown, FMath::Max(Car.IdleSpeedRpm * 1.5, FullUp * 0.19));
		const double Kickdown = Car.AutomaticKickdownSpeedRpm > 0.0 ? Car.AutomaticKickdownSpeedRpm : FMath::Max(MidDown, FullUp * 0.62);

		// The first 8 % of travel is slack; the map then rises a little faster
		// than linearly, so half throttle still changes up well short of the redline.
		const double Pedal = FMath::Clamp(PedalFraction, 0.0, 1.0);
		const double Demand = FMath::Pow(FMath::Clamp((Pedal - 0.08) / 0.84, 0.0, 1.0), 1.4);

		FShiftPoints Points;
		Points.UpshiftRpm = FMath::Lerp(LightUp, FullUp, Demand);
		Points.DownshiftRpm = Demand < 0.5
			? FMath::Lerp(LightDown, MidDown, Demand / 0.5)
			: FMath::Lerp(MidDown, Kickdown, (Demand - 0.5) / 0.5);
		return Points;
	}

	int32 SelectAutomaticGear(const FOsdsCarDefinition& Car, int32 Gear, double EngineRpm,
		double WheelEngineOmegaRadPerSecond, double PedalFraction, bool bBraking, double LateralG)
	{
		const TArray<double>& Ratios = Car.ForwardGearRatios;
		if (!Ratios.IsValidIndex(Gear) || Ratios[Gear] <= 0.0)
		{
			return Gear;
		}
		constexpr double RpmPerRadPerSecond = 60.0 / UE_DOUBLE_TWO_PI;
		FShiftPoints Points = GetAutomaticShiftPoints(Car, PedalFraction);
		if (FMath::Abs(LateralG) > 0.3)
		{
			// Corner hold: only the limits move it.
			Points.UpshiftRpm = FMath::Max(Points.UpshiftRpm, FMath::Min(Car.AutomaticUpshiftSpeedRpm, Car.FuelCutSpeedRpm * 0.97));
			Points.DownshiftRpm = FMath::Min(Points.DownshiftRpm, GetAutomaticShiftPoints(Car, 0.0).DownshiftRpm);
		}
		const double WheelRpm = FMath::Abs(WheelEngineOmegaRadPerSecond) * RpmPerRadPerSecond;
		// The road speed must carry the engine nearly to the shift point (no
		// change on a slipping launch), scaled from the car's full-throttle guard.
		const double UpshiftGuardRpm = Car.AutomaticUpshiftMinimumEngineSpeedRadPerSecond * RpmPerRadPerSecond *
			Points.UpshiftRpm / FMath::Max(Car.AutomaticUpshiftSpeedRpm, 1.0);
		if (!bBraking && Gear < Ratios.Num() - 1 && EngineRpm > Points.UpshiftRpm && WheelRpm > UpshiftGuardRpm)
		{
			return Gear + 1;
		}
		if (Gear > 0 && EngineRpm < Points.DownshiftRpm &&
			WheelRpm * Ratios[Gear - 1] / Ratios[Gear] < Points.UpshiftRpm * 0.92)
		{
			return Gear - 1;
		}
		return Gear;
	}

	double SampleEngineTorqueNm(const FOsdsCarDefinition& Car, double EngineSpeedRpm)
	{
		const TArray<FOsdsTorqueCurvePoint>& Curve = Car.EngineTorqueCurve;
		for (int32 Index = 1; Index < Curve.Num(); ++Index)
		{
			if (EngineSpeedRpm <= Curve[Index].EngineSpeedRpm)
			{
				const FOsdsTorqueCurvePoint& Lower = Curve[Index - 1];
				const FOsdsTorqueCurvePoint& Upper = Curve[Index];
				const double Range = Upper.EngineSpeedRpm - Lower.EngineSpeedRpm;
				const double Alpha = Range > UE_DOUBLE_SMALL_NUMBER
					? FMath::Clamp((EngineSpeedRpm - Lower.EngineSpeedRpm) / Range, 0.0, 1.0)
					: 0.0;
				return FMath::Lerp(Lower.EngineTorqueNm, Upper.EngineTorqueNm, Alpha) * Car.EngineTorqueScale;
			}
		}
		return Car.TorquePastCurveNm * Car.EngineTorqueScale;
	}

	void SplitDriveTorqueToWheels(
		const FOsdsCarDefinition& Car,
		double AxleTorqueNm,
		const double WheelAngularSpeedsRadPerSecond[4],
		double DeltaTimeS,
		bool bIsDriveTorque,
		double OutWheelTorquesNm[4])
	{
		if (WheelAngularSpeedsRadPerSecond == nullptr)
		{
			for (int32 Index = 0; Index < 4; ++Index)
			{
				OutWheelTorquesNm[Index] = 0.0;
			}
			return;
		}
		FOsdsDifferential Differential;
		Differential.Configure(Car);
		FOsdsDifferentialInput Input;
		Input.AxleTorqueNm = AxleTorqueNm;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Input.WheelOmegaRadPerSecond[Index] = WheelAngularSpeedsRadPerSecond[Index];
		}
		Input.DeltaTimeS = DeltaTimeS;
		Input.bDrive = bIsDriveTorque;
		FOsdsDifferentialOutput Output;
		Differential.Step(Input, Output);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			OutWheelTorquesNm[Index] = Output.WheelTorqueNm[Index];
		}
	}

	TArray<double> SplitDriveTorqueToWheels(
		const FOsdsCarDefinition& Car,
		double AxleTorqueNm,
		const double WheelAngularSpeedsRadPerSecond[4],
		double DeltaTimeS,
		bool bIsDriveTorque)
	{
		double Torques[4];
		SplitDriveTorqueToWheels(Car, AxleTorqueNm, WheelAngularSpeedsRadPerSecond, DeltaTimeS, bIsDriveTorque, Torques);
		return TArray<double>(Torques, 4);
	}
}
