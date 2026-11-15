// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicleDynamics.h"

namespace
{
	constexpr EAutomationTestFlags VehicleBenchFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	// docs/cars.md bands.
	constexpr double ZeroToSixtyBandS = 0.15;
	constexpr double SeventyToZeroBandFt = 5.0;
	constexpr double SkidpadBandG = 0.02;

	/** A car file's calibration_open note excuses only the measurement it names. */
	bool IsOpenItem(const FSimCruiseCarDefinition& Car, const TCHAR* Measurement)
	{
		return !Car.CalibrationOpen.IsEmpty() && Car.CalibrationOpen.Contains(Measurement, ESearchCase::IgnoreCase);
	}

	FString Seconds(double Value)
	{
		return Value >= TNumericLimits<double>::Max() ? FString(TEXT("never")) : FString::Printf(TEXT("%.2f s"), Value);
	}

	bool RunCarTrackTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseCarTrackResult R = SimCruiseRunCarTrack(Car, SimCruiseShippingFixedStepS);
		if (!Test.TestTrue(TEXT("bench completed"), R.bCompleted))
		{
			return false;
		}
		Test.TestFalse(TEXT("the car hit nothing on the bench pad"), R.bCollided);
		const double ZeroToSixty = Car.bTargetZeroToSixtyWithRollout ? R.ZeroToSixtyRolloutS : R.ZeroToSixtyS;
		const bool bAccel = FMath::Abs(ZeroToSixty - Car.TargetZeroToSixtySeconds) <= ZeroToSixtyBandS;
		const bool bBrake = FMath::Abs(R.SeventyToZeroFt - Car.TargetSeventyToZeroFeet) <= SeventyToZeroBandFt;
		const bool bSkid = FMath::Abs(R.SkidpadG - Car.TargetSkidpadG) <= SkidpadBandG;
		const FString Line = FString::Printf(
			TEXT("CARTRACK %-11s 0-60 %s (target %.2f)  70-0 %.1f ft (target %.1f)  skidpad %.3f g (target %.2f)  %s  [%lld steps at %.0f Hz, %.1f us/step vehicle, %.1f s sim in %.1f s wall]"),
			CarId, *(Seconds(ZeroToSixty) + (Car.bTargetZeroToSixtyWithRollout ? FString::Printf(TEXT(" rollout, %s from rest"), *Seconds(R.ZeroToSixtyS)) : FString())),
			Car.TargetZeroToSixtySeconds, R.SeventyToZeroFt, Car.TargetSeventyToZeroFeet,
			R.SkidpadG, Car.TargetSkidpadG, (bAccel && bBrake && bSkid) ? TEXT("PASS") : TEXT("OFF"),
			R.PhysicsSteps, 1.0 / SimCruiseShippingFixedStepS, R.MeanStepMicroseconds, R.SimSeconds, R.WallSeconds);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		if (!Car.CalibrationOverlayChanges.IsEmpty())
		{
			const FString Overlay = FString::Printf(TEXT("CALIBRATION %s overlay %s: %s (%s)"), CarId, *Car.CalibrationOverlayPath,
				*Car.CalibrationOverlayChanges, *Car.CalibrationOverlayNote);
			UE_LOG(LogTemp, Display, TEXT("%s"), *Overlay);
			Test.AddInfo(Overlay);
		}
		auto Check = [&Test, &Car](bool bInBand, const TCHAR* Measurement, const FString& What)
		{
			if (bInBand)
			{
				return;
			}
			if (IsOpenItem(Car, Measurement))
			{
				Test.AddInfo(FString::Printf(TEXT("%s outside its band, stated open item: %s"), *What, *Car.CalibrationOpen));
			}
			else
			{
				Test.AddError(FString::Printf(TEXT("%s outside its band"), *What));
			}
		};
		Check(bAccel, TEXT("0-60"), FString::Printf(TEXT("0-60 mph %s vs %.2f +/- %.2f s"), *Seconds(ZeroToSixty), Car.TargetZeroToSixtySeconds, ZeroToSixtyBandS));
		Check(bBrake, TEXT("70-0"), FString::Printf(TEXT("70-0 mph %.1f ft vs %.1f +/- %.0f ft"), R.SeventyToZeroFt, Car.TargetSeventyToZeroFeet, SeventyToZeroBandFt));
		Check(bSkid, TEXT("skidpad"), FString::Printf(TEXT("skidpad %.3f g vs %.2f +/- %.2f g"), R.SkidpadG, Car.TargetSkidpadG, SkidpadBandG));
		return true;
	}

	bool RunLiftOffTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseLiftOffResult R = SimCruiseRunLiftOff(Car, SimCruiseShippingFixedStepS, { 0.3, 0.5, 0.7 });
		if (!Test.TestTrue(TEXT("bench completed"), R.bCompleted))
		{
			return false;
		}
		constexpr double GateG = 0.5;
		for (const FSimCruiseLiftOffCase& Case : R.Cases)
		{
			const bool bGated = Case.TargetG <= GateG + 0.001;
			const FString Line = Case.bSteady
				? FString::Printf(TEXT("LIFTOFF %-11s %.1f g %-5s steady %.2f g (gear %d, beta %+.1f)  yaw_ratio_1s %.2f  yaw_overshoot %+.2f  beta_1s %+.1f  peak_beta %+.1f  settle_s %.2f  decel_g %.2f  %s%s%s"),
					CarId, Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift"), Case.SteadyLateralG, Case.SteadyGear, Case.SteadyBetaDeg,
					Case.YawRatio1s, Case.YawOvershoot, Case.Beta1sDeg, Case.PeakBetaDeg, Case.SettleS, Case.DecelG,
					Case.bSpun ? TEXT("SPIN") : TEXT("ok"), bGated ? TEXT("") : TEXT(" (reported)"),
					Case.bReverseAtSpeed ? TEXT(" REVERSE SELECTED AT SPEED") : TEXT(""))
				: FString::Printf(TEXT("LIFTOFF %-11s %.1f g %-5s no steady state"), CarId, Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift"));
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			Test.AddInfo(Line);
			if (bGated && !Case.bSteady)
			{
				Test.AddError(FString::Printf(TEXT("%.1f g %s: the circle driver found no steady state"), Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift")));
			}
			if (bGated && Case.bSpun)
			{
				Test.AddError(FString::Printf(TEXT("%.1f g %s: spun (peak sideslip %.1f deg)"), Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift"), Case.PeakBetaDeg));
			}
			if (Case.bReverseAtSpeed)
			{
				Test.AddError(FString::Printf(TEXT("%.1f g %s: the automatic selected reverse while rolling forward"), Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift")));
			}
		}
		// Understeer gradient, ISO 4138's constant-radius method: on the fixed circle the steer
		// angle the car needs grows with lateral acceleration by K deg/g (0 = neutral). Road cars
		// understeer; surveys put the average at about 2.6 deg/g for European and 4.4 for
		// American cars (Crolla's handling course notes).
		const FSimCruiseLiftOffCase* Low = nullptr;
		const FSimCruiseLiftOffCase* Mid = nullptr;
		for (const FSimCruiseLiftOffCase& Case : R.Cases)
		{
			if (!Case.bSteady || Case.bBrake) continue;
			if (FMath::IsNearlyEqual(Case.TargetG, 0.3, 0.01)) Low = &Case;
			if (FMath::IsNearlyEqual(Case.TargetG, 0.5, 0.01)) Mid = &Case;
		}
		if (Low && Mid && Mid->SteadyLateralG - Low->SteadyLateralG > 0.05)
		{
			const double K = (Mid->SteadyRackDeg - Low->SteadyRackDeg) / (Mid->SteadyLateralG - Low->SteadyLateralG);
			const FString Line = FString::Printf(TEXT("UNDERSTEER %-11s %.2f deg/g (road-wheel steer %.2f deg at %.2f g, %.2f deg at %.2f g)"),
				CarId, K, Low->SteadyRackDeg, Low->SteadyLateralG, Mid->SteadyRackDeg, Mid->SteadyLateralG);
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			Test.AddInfo(Line);
			Test.TestTrue(TEXT("understeers below the limit (0 to 8 deg/g, ISO 4138 constant radius)"), K > 0.0 && K < 8.0);
		}
		return true;
	}

	/**
	 * ISO 7975 braking in a turn: steady at 0.5 g on a 100 m circle, then the throttle off and a
	 * fixed brake pedal with the wheel held. A road car slows without spinning at every pedal,
	 * and without ABS its fronts lock before its rears (the proportioning valve's job): a car
	 * that locks its rears first swaps ends.
	 */
	bool RunBrakeInTurnTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseLiftOffResult R = SimCruiseRunBrakeInTurn(Car, SimCruiseShippingFixedStepS, { 0.3, 0.45, 0.6, 0.8, 1.0 });
		if (!Test.TestTrue(TEXT("bench completed"), R.bCompleted))
		{
			return false;
		}
		const bool bOpen = IsOpenItem(Car, TEXT("brake-in-turn"));
		for (const FSimCruiseLiftOffCase& Case : R.Cases)
		{
			const TCHAR* Lock = Case.FirstLockAxle == 0 ? TEXT("front") : Case.FirstLockAxle == 1 ? TEXT("REAR") : TEXT("none");
			const FString When = Case.FirstLockAxle >= 0 ? FString::Printf(TEXT(" at %.2f s"), Case.FirstLockS) : FString();
			const FString Line = Case.bSteady
				? FString::Printf(TEXT("BRAKETURN %-11s pedal %.2f  steady %.2f g  decel_g %.2f  peak_beta %+.1f  yaw_ratio_1s %.2f  first lock %s%s  %s"),
					CarId, Case.BrakePedal, Case.SteadyLateralG, Case.DecelG, Case.PeakBetaDeg, Case.YawRatio1s, Lock, *When,
					Case.bSpun ? TEXT("SPIN") : TEXT("ok"))
				: FString::Printf(TEXT("BRAKETURN %-11s pedal %.2f  no steady state"), CarId, Case.BrakePedal);
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			Test.AddInfo(Line);
			auto Fail = [&Test, &Car, bOpen](const FString& What)
			{
				if (bOpen)
				{
					Test.AddInfo(FString::Printf(TEXT("%s, stated open item: %s"), *What, *Car.CalibrationOpen));
				}
				else
				{
					Test.AddError(What);
				}
			};
			if (!Case.bSteady)
			{
				Test.AddError(FString::Printf(TEXT("pedal %.2f: the circle driver found no steady state"), Case.BrakePedal));
				continue;
			}
			// Past the friction circle any car may rotate: judge only while the combined demand
			// stays within 90 % of the car's skidpad grip.
			// The deceleration the pedal asks for (the brakes' torque over the wheel radius and the car's
			// weight), not the one measured: a spin can raise the measured one past the gate.
			const double DemandedG = 2.0 * (Case.BrakePedal * Car.FrontBrakeTorqueNm + SimCruiseDynamics::RearBrakePedal(Car, Case.BrakePedal) * Car.RearBrakeTorqueNm)
				/ (Car.TireFreeRadiusM() * Car.MassKg * SimCruiseDynamics::GravityMps2);
			const double CombinedG = FMath::Sqrt(FMath::Square(DemandedG) + FMath::Square(Case.SteadyLateralG));
			const bool bWithinGrip = CombinedG <= 0.9 * Car.TargetSkidpadG;
			if (Case.bSpun && bWithinGrip)
			{
				Fail(FString::Printf(TEXT("pedal %.2f: spun braking in the turn at %.2f g combined (peak sideslip %.1f deg)"), Case.BrakePedal, CombinedG, Case.PeakBetaDeg));
			}
			if (!Car.bAbsEnabled && Case.FirstLockAxle == 1 && bWithinGrip)
			{
				Fail(FString::Printf(TEXT("pedal %.2f: a rear wheel locked before the fronts"), Case.BrakePedal));
			}
		}

		// The proportioning rule in a straight line: from 0.15 to 0.8 g the rear axle's share of the
		// brake force stays at or under its share of the load (static rear share - a h / L), so on
		// equal grip the fronts lock first. A rear share far under the load costs stopping distance.
		if (!Car.bAbsEnabled)
		{
			auto RearPedal = [&Car](double P) { return SimCruiseDynamics::RearBrakePedal(Car, P); };
			const double Radius = Car.TireFreeRadiusM();
			double WorstMargin = 1.0;
			for (double A = 0.15; A <= 0.8001; A += 0.05)
			{
				const double Needed = A * Car.MassKg * SimCruiseDynamics::GravityMps2 * Radius / 2.0;
				double Lo = 0.0, Hi = 1.0;
				for (int32 Iteration = 0; Iteration < 40; ++Iteration)
				{
					const double P = 0.5 * (Lo + Hi);
					(P * Car.FrontBrakeTorqueNm + RearPedal(P) * Car.RearBrakeTorqueNm < Needed ? Lo : Hi) = P;
				}
				const double Front = Hi * Car.FrontBrakeTorqueNm;
				const double Rear = RearPedal(Hi) * Car.RearBrakeTorqueNm;
				const double Share = Rear / FMath::Max(Front + Rear, 1.0);
				const double Ideal = (1.0 - Car.FrontWeightFraction) - A * Car.CenterOfGravityHeightM / Car.WheelbaseM;
				const FString Line = FString::Printf(TEXT("BRAKESPLIT %-11s %.2f g  pedal %.2f  rear share %.3f  load share %.3f"), CarId, A, Hi, Share, Ideal);
				UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
				Test.AddInfo(Line);
				WorstMargin = FMath::Min(WorstMargin, Ideal - Share);
				if (Share > Ideal + 0.005)
				{
					const FString What = FString::Printf(TEXT("%.2f g straight: the rear takes %.3f of the braking but carries %.3f of the load (rears lock first)"), A, Share, Ideal);
				if (bOpen) Test.AddInfo(What + TEXT(", stated open item")); else Test.AddError(What);
				}
			}
		}
		return true;
	}

	bool RunStaticLoadTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseStaticLoadResult R = SimCruiseRunStaticLoad(Car, SimCruiseShippingFixedStepS);
		if (!Test.TestTrue(TEXT("settled"), R.bCompleted))
		{
			return false;
		}
		const double Weight = Car.MassKg * R.GravityMps2;
		const FString Line = FString::Printf(
			TEXT("STATICLOAD %-11s loads FL %.0f FR %.0f RL %.0f RR %.0f N  total %.1f of %.1f N  front %.4f (file %.4f)  ride FL %.1f RL %.1f mm (static %.1f)  pitch %+.3f roll %+.3f deg  residual %.4f m/s"),
			CarId, R.LoadN[0], R.LoadN[1], R.LoadN[2], R.LoadN[3], R.TotalLoadN, Weight, R.FrontFraction, Car.FrontWeightFraction,
			R.SuspensionLengthM[0] * 1000.0, R.SuspensionLengthM[2] * 1000.0, Car.StaticSuspensionLengthM * 1000.0, R.PitchDeg, R.RollDeg, R.ResidualSpeedMps);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		Test.TestEqual(TEXT("the tyres carry the car's weight"), R.TotalLoadN, Weight, Weight * 0.002);
		Test.TestEqual(TEXT("front share matches the file"), R.FrontFraction, Car.FrontWeightFraction, 0.002);
		Test.TestEqual(TEXT("left and right front agree"), R.LoadN[0], R.LoadN[1], Weight * 0.001);
		Test.TestEqual(TEXT("left and right rear agree"), R.LoadN[2], R.LoadN[3], Weight * 0.001);
		// Ride height: preload makes the static length exact at 9.80665 m/s^2; Unreal's 9.8 sits it a fraction of a mm higher.
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Test.TestEqual(FString::Printf(TEXT("wheel %d sits at the static suspension length"), Index),
				R.SuspensionLengthM[Index], Car.StaticSuspensionLengthM, 0.002);
		}
		Test.TestTrue(TEXT("level (pitch, roll under 0.2 deg)"), FMath::Abs(R.PitchDeg) < 0.2 && FMath::Abs(R.RollDeg) < 0.2);
		Test.TestTrue(TEXT("at rest"), R.ResidualSpeedMps < 0.01);
		return true;
	}

	/**
	 * ISO 7401 step steer at 80 km/h to about 0.4 g. Road cars reach 90 % of their steady yaw
	 * rate 0.1-0.4 s after the steering's half point, overshoot it by no more than about 30 %
	 * and settle without a slide; a slower answer feels vague, a larger overshoot nervous.
	 */
	bool RunStepSteerTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseStepSteerResult R = SimCruiseRunStepSteer(Car, SimCruiseShippingFixedStepS);
		if (!Test.TestTrue(TEXT("bench completed"), R.bCompleted))
		{
			return false;
		}
		const FString Line = FString::Printf(TEXT("STEPSTEER %-11s %.1f km/h  road wheels %.2f deg  steady %.1f deg/s %.2f g  t90 %.3f s  overshoot %+.2f  peak beta %.1f deg  roll %.2f deg/g"),
			CarId, R.SpeedMps * 3.6, R.RackDeg, R.SteadyYawDegS, R.SteadyLateralG, R.YawResponseS, R.YawOvershoot, R.PeakBetaDeg, R.RollDegPerG);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		// Roll per g: logged against no published figure yet (none sourced for these cars); only a
		// sanity band here (road cars roll outward, a few degrees per g).
		Test.TestTrue(TEXT("rolls outward, 0.5-9 deg/g"), R.RollDegPerG > 0.5 && R.RollDegPerG < 9.0);
		Test.TestTrue(TEXT("turns (steady lateral 0.2-0.7 g)"), R.SteadyLateralG > 0.2 && R.SteadyLateralG < 0.7);
		Test.TestTrue(TEXT("yaw response time 0.08-0.40 s"), R.YawResponseS > 0.08 && R.YawResponseS < 0.40);
		Test.TestTrue(TEXT("yaw overshoot under 30 %"), R.YawOvershoot < 0.30);
		Test.TestTrue(TEXT("no slide (sideslip under 4 deg)"), R.PeakBetaDeg < 4.0);
		return true;
	}
}

namespace
{
	/**
	 * Top speed at full throttle on the flat. A reference only where it is published: the 2020
	 * Corvette Stingray Z51, 194 mph (Chevrolet's specification). The others are logged until a
	 * figure is sourced.
	 */
	bool RunTopSpeedTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseTopSpeedResult R = SimCruiseRunTopSpeed(Car, SimCruiseShippingFixedStepS);
		const double Mph = R.TopSpeedMps * 3600.0 / 1609.344;
		const bool bC8 = FString(CarId) == TEXT("meridian_z");
		const FString Line = FString::Printf(TEXT("TOPSPEED %-11s %.1f mph in gear %d at %.0f rpm after %.0f s, %.1f km%s"),
			CarId, Mph, R.Gear, R.Rpm, R.SimSeconds, R.DistanceM / 1000.0, bC8 ? TEXT(" (target 194 mph, Chevrolet)") : TEXT(" (no sourced figure: logged only)"));
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		UE_LOG(LogTemp, Display, TEXT("TOPSPEED %s gears:%s"), CarId, *R.GearTrace);
		Test.TestTrue(TEXT("speed levelled off on the pad"), R.bCompleted);
		if (bC8)
		{
			Test.TestTrue(TEXT("C8 Z51 top speed 194 +/- 6 mph"), FMath::Abs(Mph - 194.0) <= 6.0);
		}
		if (FString(CarId) == TEXT("mustang65") && Car.ForwardGearRatios.Num() > 0)
		{
			// Car Life's 289/200 four-speed: 24.7 mph per 1000 rpm in top (3.00 axle), at the tyre's
			// rolling radius, about 0.97 of its free radius for a loaded bias-ply.
			const double PerThousand = 1000.0 / (Car.ForwardGearRatios.Last() * Car.FinalDriveRatio) * 2.0 * UE_DOUBLE_PI * 0.97 * Car.TireFreeRadiusM() / 60.0 * 3600.0 / 1609.344;
			Test.AddInfo(FString::Printf(TEXT("mustang65 top gear %.1f mph per 1000 rpm (Car Life 24.7)"), PerThousand));
			Test.TestTrue(TEXT("Mustang top gear 24.7 mph per 1000 rpm +/- 4 %"), FMath::Abs(PerThousand / 24.7 - 1.0) <= 0.04);
		}
		return true;
	}

	/**
	 * The automatic on a road (the Dragon review's faults): no change back within 2 s of the last
	 * one (hunting), and in gear with the clutch home the engine turns with the road (no 19 mph in
	 * first at 851 rpm).
	 */
	bool RunShiftDriveTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseShiftDriveResult R = SimCruiseRunShiftDrive(Car, SimCruiseShippingFixedStepS);
		const FString Line = FString::Printf(TEXT("SHIFTDRIVE %-11s %d changes, %d back within 2 s, engine off the road by up to %.0f %% in gear, lowest road speed in gear %.0f rpm"),
			CarId, R.Shifts, R.QuickReversals, 100.0 * R.WorstRpmGap, R.LowestRoadRpm);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		UE_LOG(LogTemp, Display, TEXT("SHIFTDRIVE %s worst at %s"), CarId, *R.WorstAt);
		UE_LOG(LogTemp, Display, TEXT("SHIFTDRIVE %s changes:%s"), CarId, *R.Trace);
		Test.TestTrue(TEXT("drive completed"), R.bCompleted);
		Test.TestEqual(TEXT("no hunting (no change back within 2 s)"), R.QuickReversals, 0);
		// Full pedal never sits on the fuel cut: it changes up (the Mustang held 1st at 5000 rpm for 2 s
		// with its tyres spinning on the Dragon).
		Test.AddInfo(FString::Printf(TEXT("%s: %.2f s on the fuel cut at full pedal"), CarId, R.LimiterSeconds));
		Test.TestTrue(TEXT("no more than 0.5 s on the fuel cut"), R.LimiterSeconds <= 0.5);
		// The self-shifting clutch is a calibrated viscous coupling: under full torque it slips a few
		// percent (about 9 % on the Mustang in 2nd). A locked dry clutch waits for v2.0.4 with the
		// recalibration it needs; the gross faults (a clutch slipping to the launch speed in every gear,
		// 4672 rpm against the road's 1900) are what this bounds.
		Test.TestTrue(TEXT("in gear, the engine turns with the road (within 12 %)"), R.WorstRpmGap < 0.12);
		return true;
	}

	/**
	 * Passing at full pedal from a steady cruise, kickdown allowed. Reference only where sourced:
	 * the Corvette Stingray Z51, Car and Driver (Z51 long-term test, the 2020-21 LT2 and eight-speed
	 * DCT): 30-50 mph 1.8 s, 50-70 mph 2.5 s. The others are logged from their sourced inputs.
	 */
	bool RunPassingTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const double Mph = 1609.344 / 3600.0;
		const bool bC8 = FString(CarId) == TEXT("meridian_z");
		struct FCase { double From, To, Target; };
		for (const FCase& Case : { FCase{ 30.0, 50.0, 1.8 }, FCase{ 50.0, 70.0, 2.5 } })
		{
			const FSimCruisePassingResult R = SimCruiseRunPassing(Car, SimCruiseShippingFixedStepS, Case.From * Mph, Case.To * Mph);
			const FString Line = FString::Printf(TEXT("PASSING %-11s %.0f-%.0f mph %.2f s (cruise gear %d, kickdown to %d)%s"), CarId, Case.From, Case.To, R.Seconds,
				R.CruiseGear, R.KickdownGear, bC8 ? *FString::Printf(TEXT(" target %.1f s (Car and Driver)"), Case.Target) : TEXT(" (no sourced figure: logged only)"));
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			Test.AddInfo(Line);
			Test.TestTrue(TEXT("passing run completed"), R.bCompleted);
			if (bC8)
			{
				const FString What = FString::Printf(TEXT("C8 %.0f-%.0f mph %.2f s against %.1f +/- 0.3 s"), Case.From, Case.To, R.Seconds, Case.Target);
				if (FMath::Abs(R.Seconds - Case.Target) > 0.3)
				{
					if (IsOpenItem(Car, *FString::Printf(TEXT("%.0f-%.0f"), Case.From, Case.To))) Test.AddInfo(What + TEXT(", stated open item")); else Test.AddError(What);
				}
			}
		}
		return true;
	}
}

#define SIMCRUISE_VEHICLE_BENCH(Kind, CarIdent) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicle##Kind##_##CarIdent, "SimCruise.Vehicle." #Kind "." #CarIdent, VehicleBenchFlags) \
	bool FSimCruiseVehicle##Kind##_##CarIdent::RunTest(const FString& Parameters) { return Run##Kind##Test(*this, TEXT(#CarIdent)); }

SIMCRUISE_VEHICLE_BENCH(CarTrack, nb1999)
SIMCRUISE_VEHICLE_BENCH(CarTrack, nb2001)
SIMCRUISE_VEHICLE_BENCH(CarTrack, nb2001_lowered)
SIMCRUISE_VEHICLE_BENCH(CarTrack, mustang65)
SIMCRUISE_VEHICLE_BENCH(CarTrack, meridian_z)
SIMCRUISE_VEHICLE_BENCH(CarTrack, model3)
SIMCRUISE_VEHICLE_BENCH(CarTrack, f150raptor18)
SIMCRUISE_VEHICLE_BENCH(CarTrack, grandcherokee13)
SIMCRUISE_VEHICLE_BENCH(CarTrack, odyssey18)
SIMCRUISE_VEHICLE_BENCH(CarTrack, crv25)
SIMCRUISE_VEHICLE_BENCH(BrakeInTurn, nb1999)
SIMCRUISE_VEHICLE_BENCH(BrakeInTurn, nb2001)
SIMCRUISE_VEHICLE_BENCH(BrakeInTurn, nb2001_lowered)
SIMCRUISE_VEHICLE_BENCH(BrakeInTurn, mustang65)
SIMCRUISE_VEHICLE_BENCH(StepSteer, nb1999)
SIMCRUISE_VEHICLE_BENCH(StepSteer, nb2001)
SIMCRUISE_VEHICLE_BENCH(StepSteer, nb2001_lowered)
SIMCRUISE_VEHICLE_BENCH(StepSteer, mustang65)
SIMCRUISE_VEHICLE_BENCH(StepSteer, meridian_z)
SIMCRUISE_VEHICLE_BENCH(StepSteer, model3)
SIMCRUISE_VEHICLE_BENCH(StepSteer, f150raptor18)
SIMCRUISE_VEHICLE_BENCH(StepSteer, grandcherokee13)
SIMCRUISE_VEHICLE_BENCH(StepSteer, odyssey18)
SIMCRUISE_VEHICLE_BENCH(StepSteer, crv25)
SIMCRUISE_VEHICLE_BENCH(LiftOff, nb1999)
SIMCRUISE_VEHICLE_BENCH(LiftOff, nb2001)
SIMCRUISE_VEHICLE_BENCH(LiftOff, nb2001_lowered)
SIMCRUISE_VEHICLE_BENCH(LiftOff, mustang65)
SIMCRUISE_VEHICLE_BENCH(LiftOff, meridian_z)
SIMCRUISE_VEHICLE_BENCH(LiftOff, model3)
SIMCRUISE_VEHICLE_BENCH(LiftOff, f150raptor18)
SIMCRUISE_VEHICLE_BENCH(LiftOff, grandcherokee13)
SIMCRUISE_VEHICLE_BENCH(LiftOff, odyssey18)
SIMCRUISE_VEHICLE_BENCH(LiftOff, crv25)
SIMCRUISE_VEHICLE_BENCH(TopSpeed, nb2001_lowered)
SIMCRUISE_VEHICLE_BENCH(TopSpeed, mustang65)
SIMCRUISE_VEHICLE_BENCH(TopSpeed, meridian_z)
SIMCRUISE_VEHICLE_BENCH(ShiftDrive, nb2001_lowered)
SIMCRUISE_VEHICLE_BENCH(ShiftDrive, mustang65)
SIMCRUISE_VEHICLE_BENCH(ShiftDrive, meridian_z)
SIMCRUISE_VEHICLE_BENCH(Passing, nb2001_lowered)
SIMCRUISE_VEHICLE_BENCH(Passing, mustang65)
SIMCRUISE_VEHICLE_BENCH(Passing, meridian_z)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, nb1999)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, nb2001)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, nb2001_lowered)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, mustang65)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, meridian_z)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, model3)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, f150raptor18)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, grandcherokee13)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, odyssey18)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, crv25)

#undef SIMCRUISE_VEHICLE_BENCH

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleDeterminismTest, "SimCruise.Vehicle.Determinism", VehicleBenchFlags)
bool FSimCruiseVehicleDeterminismTest::RunTest(const FString& Parameters)
{
	FSimCruiseCarDefinition Car;
	if (!SimCruiseLoadBenchCar(*this, TEXT("nb1999"), Car))
	{
		return false;
	}
	// The same car, controls and fixed steps must give the same state, bit for
	// bit, through all three tests: the restart and the skidpad included, where
	// anything else in the bench world (streamed tiles once) shows up first.
	const FSimCruiseCarTrackResult A = SimCruiseRunCarTrack(Car, SimCruiseShippingFixedStepS);
	const FSimCruiseCarTrackResult B = SimCruiseRunCarTrack(Car, SimCruiseShippingFixedStepS);
	if (!TestTrue(TEXT("both runs completed"), A.bCompleted && B.bCompleted))
	{
		return false;
	}
	TestEqual(TEXT("0-60 identical"), A.ZeroToSixtyS, B.ZeroToSixtyS);
	TestEqual(TEXT("70-0 identical"), A.SeventyToZeroFt, B.SeventyToZeroFt);
	TestEqual(TEXT("skidpad identical"), A.SkidpadG, B.SkidpadG);
	TestEqual(TEXT("fingerprint size"), A.Fingerprint.Num(), B.Fingerprint.Num());
	int32 Mismatches = 0;
	for (int32 Index = 0; Index < FMath::Min(A.Fingerprint.Num(), B.Fingerprint.Num()); ++Index)
	{
		Mismatches += A.Fingerprint[Index] != B.Fingerprint[Index] ? 1 : 0;
	}
	const FString Line = FString::Printf(TEXT("DETERMINISM two CarTrack runs: 0-60 %.9f / %.9f s, skidpad %.9f / %.9f g, %d of %d state values differ"),
		A.ZeroToSixtyS, B.ZeroToSixtyS, A.SkidpadG, B.SkidpadG, Mismatches, A.Fingerprint.Num());
	UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
	AddInfo(Line);
	TestEqual(TEXT("state identical after the run"), Mismatches, 0);
	return true;
}

namespace
{
	bool RunConvergenceTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
	FSimCruiseCarDefinition Car;
	if (!SimCruiseLoadBenchCar(Test, CarId, Car))
	{
		return false;
	}
	struct FRate { double Hz; double WheelHz; const TCHAR* Label; };
	const FRate Rates[] = {
		{ 60.0, 360.0, TEXT("60 Hz body, 6 wheel substeps (Godot)") },
		{ 120.0, 360.0, TEXT("120 Hz body, 3 wheel substeps") },
		{ 240.0, 480.0, TEXT("240 Hz body, 2 wheel substeps") },
		{ 360.0, 360.0, TEXT("360 Hz body and wheels (shipping)") },
		{ 720.0, 720.0, TEXT("720 Hz body and wheels") },
	};
	TArray<FSimCruiseCarTrackResult> Results;
	for (const FRate& Rate : Rates)
	{
		const FSimCruiseCarTrackResult R = SimCruiseRunCarTrack(Car, 1.0 / Rate.Hz, 1.0 / Rate.WheelHz);
		Results.Add(R);
		const FString Line = FString::Printf(TEXT("CONVERGENCE %-11s %-38s 0-60 %s  70-0 %.1f ft  skidpad %.3f g  %.1f us/step vehicle  %.2f ms of vehicle per simulated second"),
			CarId, Rate.Label, *Seconds(R.ZeroToSixtyS), R.SeventyToZeroFt, R.SkidpadG, R.MeanStepMicroseconds, R.MeanStepMicroseconds * Rate.Hz / 1000.0);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		Test.TestTrue(FString::Printf(TEXT("%s completed"), Rate.Label), R.bCompleted);
		Test.TestFalse(FString::Printf(TEXT("%s hit nothing on the pad"), Rate.Label), R.bCollided);
	}
	// Halving the step from the shipping rate must move each result by less
	// than a fifth of its calibration band.
	const FSimCruiseCarTrackResult& Shipping = Results[3];
	const FSimCruiseCarTrackResult& Finer = Results[4];
	Test.TestTrue(TEXT("0-60 converged at 360 Hz"), FMath::Abs(Shipping.ZeroToSixtyS - Finer.ZeroToSixtyS) < ZeroToSixtyBandS / 5.0);
	Test.TestTrue(TEXT("70-0 converged at 360 Hz"), FMath::Abs(Shipping.SeventyToZeroFt - Finer.SeventyToZeroFt) < SeventyToZeroBandFt / 5.0);
	Test.TestTrue(TEXT("skidpad converged at 360 Hz"), FMath::Abs(Shipping.SkidpadG - Finer.SkidpadG) < SkidpadBandG / 5.0);
	return true;
	}
}

#define SIMCRUISE_VEHICLE_CONVERGENCE(CarIdent) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleConvergence_##CarIdent, "SimCruise.Vehicle.Convergence." #CarIdent, VehicleBenchFlags) \
	bool FSimCruiseVehicleConvergence_##CarIdent::RunTest(const FString& Parameters) { return RunConvergenceTest(*this, TEXT(#CarIdent)); }

SIMCRUISE_VEHICLE_CONVERGENCE(nb1999)
SIMCRUISE_VEHICLE_CONVERGENCE(mustang65)
SIMCRUISE_VEHICLE_CONVERGENCE(meridian_z)

#undef SIMCRUISE_VEHICLE_CONVERGENCE

#endif
