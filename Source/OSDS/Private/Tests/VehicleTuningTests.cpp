// Copyright OSDS contributors. All Rights Reserved.

// One test per tuning row: the row's change, applied through FOsdsTuning to the real car, moves the
// matching bench number the way the real car's would (more power, a quicker 0-60; a stiffer front
// bar, more understeer on the step steer; ...). The car is the 2001 NB unless a row needs a car
// that has the system (stability and traction control: meridian_z).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/OsdsTuning.h"
#include "Vehicle/OsdsVehicleSim.h"

#include <atomic>

namespace OsdsTuningTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;
	constexpr double Dt = OsdsShippingFixedStepS;
	constexpr double MphToMps = 0.44704;

	struct FSetup
	{
		FOsdsCarDefinition Real;
		FOsdsTuning Stock;
		/** The car with Change applied to the stock setup (clamped, as the menu does). */
		FOsdsCarDefinition With(TFunctionRef<void(FOsdsTuning&)> Change) const
		{
			FOsdsTuning T = Stock;
			Change(T);
			T.Clamp(Stock);
			return T.Apply(Real, Stock);
		}
		FOsdsCarDefinition Tuned() const { return Stock.Apply(Real, Stock); }
	};

	bool Load(FAutomationTestBase& Test, const TCHAR* CarId, FSetup& Out)
	{
		if (!OsdsLoadBenchCar(Test, CarId, Out.Real))
		{
			return false;
		}
		Out.Stock = FOsdsTuning::FromCar(Out.Real, 15.0);
		return true;
	}

	/** Report and check: the tuned number moved the expected way past the stock one. */
	bool Moved(FAutomationTestBase& Test, const FString& What, double Stock, double Tuned, bool bShouldRise, double MinChange = 1.0e-6)
	{
		Test.AddInfo(FString::Printf(TEXT("%s: stock %.4f, tuned %.4f"), *What, Stock, Tuned));
		const bool bOk = bShouldRise ? Tuned > Stock + MinChange : Tuned < Stock - MinChange;
		Test.TestTrue(FString::Printf(TEXT("%s %s (stock %.4f, tuned %.4f)"), *What, bShouldRise ? TEXT("rises") : TEXT("falls"), Stock, Tuned), bOk);
		return bOk;
	}

	/** Run a physics-step driver on the flat pad until it says it is done. */
	template <typename TDriver>
	TSharedRef<TDriver> Drive(const FOsdsCarDefinition& Car, TSharedRef<TDriver> Driver, double MaxS, double PadM = 3000.0)
	{
		FOsdsVehicleBenchWorld World(Dt, PadM);
		if (World.IsValid() && World.SpawnVehicle(Car, FVector3d(0.0, 0.0, 0.15), [Driver](FOsdsVehicleDriverContext& C) { Driver->Step(C); }) != nullptr)
		{
			World.RunUntil([&Driver]() { return Driver->bDone.load(); }, MaxS);
		}
		return Driver;
	}

	double Forward(const FOsdsVehicleDriverContext& C)
	{
		return C.Body.LinearVelocityMps.Dot(C.Body.Rotation.RotateVector(FVector3d::UnitX()));
	}

	/** Full throttle from rest in the automatic: the road speed at each upshift (the gear it left). */
	struct FPull
	{
		double T = 0.0;
		int32 Gear = -1;
		int32 Upshifts = 1;
		TArray<double> UpshiftMps;
		std::atomic<bool> bDone{ false };
		void Step(FOsdsVehicleDriverContext& C)
		{
			C.Controls = FOsdsVehicleControls();
			if (bDone) return;
			T += Dt;
			C.Controls.bDirectRack = true;
			if (T < 0.5) return;
			C.Controls.Throttle = 1.0;
			const double Speed = Forward(C);
			if (Gear >= 0 && C.Sim.State.Gear > Gear) UpshiftMps.Add(Speed);
			Gear = C.Sim.State.Gear;
			if (UpshiftMps.Num() >= Upshifts || T > 80.0) bDone = true;
		}
	};

	/** The road speeds of the first Upshifts changes up. */
	TArray<double> Pull(const FOsdsCarDefinition& Car, int32 Upshifts)
	{
		TSharedRef<FPull> D = MakeShared<FPull>();
		D->Upshifts = Upshifts;
		return Drive(Car, D, 90.0, 6000.0)->UpshiftMps;
	}

	/** Settle, then a 0.5 m/s downward push on the body: each axle's deepest compression below
	 *  static and its rebound past static after it (from the front-left and rear-left lengths). */
	struct FHeave
	{
		double T = 0.0;
		double Static[2] = { 0.0, 0.0 };
		double Deepest[2] = { 0.0, 0.0 };
		double Rebound[2] = { 0.0, 0.0 };
		/** The fastest the suspension extended after its deepest point (m/s): rebound damping slows it. */
		double ReboundSpeed[2] = { 0.0, 0.0 };
		double Last[2] = { 0.0, 0.0 };
		bool bPushed = false;
		std::atomic<bool> bDone{ false };
		void Step(FOsdsVehicleDriverContext& C)
		{
			C.Controls = FOsdsVehicleControls();
			if (bDone) return;
			T += Dt;
			C.Controls.bDirectRack = true;
			C.Controls.Brake = 0.3;
			const double L[2] = { C.Sim.State.Wheels[0].SuspensionLengthM, C.Sim.State.Wheels[2].SuspensionLengthM };
			if (T < 2.5)
			{
				for (int32 A = 0; A < 2; ++A) Static[A] = L[A];
				return;
			}
			if (!bPushed)
			{
				C.Body.LinearVelocityMps.Z -= 0.5;
				C.bBodyStateChanged = true;
				bPushed = true;
				return;
			}
			for (int32 A = 0; A < 2; ++A)
			{
				Deepest[A] = FMath::Max(Deepest[A], Static[A] - L[A]);
				// Rebound counts once the stroke has turned (the length back above static).
				if (Deepest[A] > 0.002) Rebound[A] = FMath::Max(Rebound[A], L[A] - Static[A]);
				if (Deepest[A] > 0.002 && Last[A] > 0.0) ReboundSpeed[A] = FMath::Max(ReboundSpeed[A], (L[A] - Last[A]) / Dt);
				Last[A] = L[A];
			}
			if (T > 4.5) bDone = true;
		}
	};

	/** The body's nose-up pitch (deg) settled on the pad. */
	struct FSettle
	{
		double T = 0.0;
		double PitchDeg = 0.0;
		std::atomic<bool> bDone{ false };
		void Step(FOsdsVehicleDriverContext& C)
		{
			C.Controls = FOsdsVehicleControls();
			if (bDone) return;
			T += Dt;
			C.Controls.bDirectRack = true;
			C.Controls.Brake = 0.3;
			if (T > 3.0)
			{
				PitchDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(C.Body.Rotation.RotateVector(FVector3d::UnitX()).Z, -1.0, 1.0)));
				bDone = true;
			}
		}
	};

	/** Straight at 70 mph, then a fixed pedal: the stopping distance and the mean front-minus-rear
	 *  braking slip (which end works harder). */
	struct FBrake
	{
		double T = 0.0;
		double Pedal = 0.5;
		int32 State = 0;
		FVector3d Start = FVector3d::ZeroVector;
		double DistanceM = 0.0;
		double SlipSum = 0.0;
		int32 SlipN = 0;
		std::atomic<bool> bDone{ false };
		void Step(FOsdsVehicleDriverContext& C)
		{
			C.Controls = FOsdsVehicleControls();
			if (bDone) return;
			T += Dt;
			C.Controls.bDirectRack = true;
			const double Speed = Forward(C);
			if (State == 0)
			{
				if (T >= 0.5)
				{
					const double V = 70.0 * MphToMps;
					C.Body.LinearVelocityMps = C.Body.Rotation.RotateVector(FVector3d::UnitX()) * V;
					C.bBodyStateChanged = true;
					for (FOsdsWheelState& W : C.Sim.State.Wheels) W.AngularSpeedRadPerSecond = V / C.Sim.GetTireRadiusM();
					Start = C.Body.PositionM;
					State = 1;
				}
				return;
			}
			C.Controls.Brake = Pedal;
			if (Speed > 5.0)
			{
				const double R = C.Sim.GetTireRadiusM();
				auto Slip = [&](int32 W) { return FMath::Max(0.0, 1.0 - C.Sim.State.Wheels[W].AngularSpeedRadPerSecond * R / FMath::Max(Speed, 1.0)); };
				SlipSum += 0.5 * (Slip(0) + Slip(1)) - 0.5 * (Slip(2) + Slip(3));
				++SlipN;
			}
			if (Speed < 0.3 || T > 30.0)
			{
				DistanceM = (C.Body.PositionM - Start).Size();
				bDone = true;
			}
		}
	};

	/** A steady left circle at 9 m/s (15 m radius; the steady part also gives the rack's aligning torque), then full throttle 1.5 s, then the throttle shut 1 s: the
	 *  mean difference of the rear wheels' speeds (rad/s) on power and on the coast. A tighter
	 *  limited-slip differential holds them closer. */
	struct FCornerDiff
	{
		double T = 0.0;
		int32 State = 0;
		double Power = 0.0, Coast = 0.0, Aligning = 0.0;
		int32 PowerN = 0, CoastN = 0, AligningN = 0;
		std::atomic<bool> bDone{ false };
		void Step(FOsdsVehicleDriverContext& C)
		{
			C.Controls = FOsdsVehicleControls();
			if (bDone) return;
			T += Dt;
			C.Controls.bDirectRack = true;
			const FOsdsCarDefinition& Car = C.Sim.GetCar();
			const double V = 9.0, Radius = 15.0;
			C.Controls.RackAngleRad = -(Car.WheelbaseM / Radius + FMath::DegreesToRadians(2.0));
			const double Diff = FMath::Abs(C.Sim.State.Wheels[2].AngularSpeedRadPerSecond - C.Sim.State.Wheels[3].AngularSpeedRadPerSecond);
			if (State == 0)
			{
				if (T >= 0.5)
				{
					C.Body.LinearVelocityMps = C.Body.Rotation.RotateVector(FVector3d::UnitX()) * V;
					C.bBodyStateChanged = true;
					for (FOsdsWheelState& W : C.Sim.State.Wheels) W.AngularSpeedRadPerSecond = V / C.Sim.GetTireRadiusM();
					State = 1;
					T = 0.0;
				}
				return;
			}
			if (State == 1)
			{
				C.Controls.Throttle = 0.25;
				if (T > 1.0) { Aligning += FMath::Abs(C.Sim.State.RackAligningTorqueNm); ++AligningN; }
				if (T > 2.0) { State = 2; T = 0.0; Aligning /= FMath::Max(AligningN, 1); }
				return;
			}
			if (State == 2)
			{
				C.Controls.Throttle = 1.0;
				if (T > 0.3) { Power += Diff; ++PowerN; }
				if (T > 1.5) { State = 3; T = 0.0; }
				return;
			}
			C.Controls.Throttle = 0.0;
			if (T > 0.2) { Coast += Diff; ++CoastN; }
			if (T > 1.2)
			{
				Power /= FMath::Max(PowerN, 1);
				Coast /= FMath::Max(CoastN, 1);
				bDone = true;
			}
		}
	};

	/** Full throttle from rest for 2.5 s: the highest driven-wheel slip the traction control allowed. */
	struct FLaunchSlip
	{
		double T = 0.0;
		double PeakSlip = 0.0;
		std::atomic<bool> bDone{ false };
		void Step(FOsdsVehicleDriverContext& C)
		{
			C.Controls = FOsdsVehicleControls();
			if (bDone) return;
			T += Dt;
			C.Controls.bDirectRack = true;
			if (T < 0.5) return;
			C.Controls.Throttle = 1.0;
			for (int32 W = 0; W < 4; ++W)
			{
				if (C.Sim.GetCar().IsDrivenWheel(W) && T > 0.7) PeakSlip = FMath::Max(PeakSlip, C.Sim.State.Wheels[W].TractionControlSlip);
			}
			if (T > 3.0) bDone = true;
		}
	};

	/** Coasting from 90 km/h for 4 s with the wheel straight: the speed left (tyre scrub from toe slows it). */
	struct FCoast
	{
		double T = 0.0;
		int32 State = 0;
		double EndMps = 0.0;
		std::atomic<bool> bDone{ false };
		void Step(FOsdsVehicleDriverContext& C)
		{
			C.Controls = FOsdsVehicleControls();
			if (bDone) return;
			T += Dt;
			C.Controls.bDirectRack = true;
			if (State == 0)
			{
				if (T >= 0.5)
				{
					const double V = 25.0;
					C.Body.LinearVelocityMps = C.Body.Rotation.RotateVector(FVector3d::UnitX()) * V;
					C.bBodyStateChanged = true;
					for (FOsdsWheelState& W : C.Sim.State.Wheels) W.AngularSpeedRadPerSecond = V / C.Sim.GetTireRadiusM();
					State = 1;
					T = 0.0;
				}
				return;
			}
			if (T > 4.0)
			{
				EndMps = Forward(C);
				bDone = true;
			}
		}
	};

	TSharedRef<FCornerDiff> CornerDiffOf(const FOsdsCarDefinition& Car) { return Drive(Car, MakeShared<FCornerDiff>(), 12.0); }

	double ZeroToSixty(const FOsdsCarDefinition& Car)
	{
		return OsdsRunCarTrack(Car, Dt, 1.0 / 360.0, true).ZeroToSixtyS;
	}

	FOsdsStepSteerResult StepSteer(const FOsdsCarDefinition& Car)
	{
		return OsdsRunStepSteer(Car, Dt);
	}
}

using namespace OsdsTuningTest;

#define OSDS_TUNING_ROW_TEST(Ident, Car) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTuningRow_##Ident, "OSDS.Tuning.Row." #Ident, OsdsTuningTest::Flags) \
	static bool RunTuningRow_##Ident(FAutomationTestBase& Test, const FSetup& S); \
	bool FOsdsTuningRow_##Ident::RunTest(const FString& Parameters) \
	{ \
		FSetup S; \
		if (!Load(*this, TEXT(Car), S)) return false; \
		return RunTuningRow_##Ident(*this, S); \
	} \
	static bool RunTuningRow_##Ident(FAutomationTestBase& Test, const FSetup& S)

// ------------------------------------------------------------------ the stock setup and the hooks

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTuningStockIsRealTest, "OSDS.Tuning.StockIsReal", OsdsTuningTest::Flags)
bool FOsdsTuningStockIsRealTest::RunTest(const FString& Parameters)
{
	// The stock setup through Apply (with the three tuning hooks at their defaults) drives exactly as
	// the real car: the same bench numbers to the last digit.
	for (const TCHAR* Id : { TEXT("nb2001"), TEXT("meridian_z") })
	{
		FSetup S;
		if (!Load(*this, Id, S)) continue;
		const FOsdsCarDefinition Tuned = S.Tuned();
		TestEqual(FString::Printf(TEXT("%s: same 0-60"), Id), ZeroToSixty(Tuned), ZeroToSixty(S.Real));
		const FOsdsStepSteerResult A = StepSteer(Tuned), B = StepSteer(S.Real);
		TestEqual(FString::Printf(TEXT("%s: same step-steer yaw"), Id), A.SteadyYawDegS, B.SteadyYawDegS);
		TestEqual(FString::Printf(TEXT("%s: same step-steer roll"), Id), A.RollDegPerG, B.RollDegPerG);
		const FOsdsSineDwellResult C = OsdsRunSineDwell(Tuned, Dt, true), D = OsdsRunSineDwell(S.Real, Dt, true);
		TestEqual(FString::Printf(TEXT("%s: same sine-dwell yaw ratio"), Id), C.Ratio1s, D.Ratio1s);
		// The stock setup round-trips through Clamp untouched.
		FOsdsTuning T = S.Stock;
		T.Clamp(S.Stock);
		TestEqual(FString::Printf(TEXT("%s: stock is inside its own ranges"), Id), FCrc::MemCrc32(&T, sizeof(T)), FCrc::MemCrc32(&S.Stock, sizeof(S.Stock)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTuningSaveLoadTest, "OSDS.Tuning.SaveLoad", OsdsTuningTest::Flags)
bool FOsdsTuningSaveLoadTest::RunTest(const FString& Parameters)
{
	FSetup S;
	if (!Load(*this, TEXT("nb2001"), S)) return false;
	// Every row saved and read back, per car (a test id so the player's own file is untouched).
	FOsdsTuning T = S.Stock;
	T.PeakTorqueLbFt += 10.0; T.PeakPowerHp += 12.0; T.RevLimitRpm -= 300.0; T.FinalDrive += 0.2; T.GearRatio[1] += 0.05;
	T.FrontSpringLbIn += 50.0; T.RearSpringLbIn += 40.0; T.FrontBumpPct = 120.0; T.FrontReboundPct = 130.0; T.RearBumpPct = 90.0; T.RearReboundPct = 110.0;
	T.FrontBarPct = 150.0; T.RearBarPct = 60.0; T.FrontRideHeightMm = -20.0; T.RearRideHeightMm = -10.0;
	T.FrontCamberDeg -= 1.0; T.RearCamberDeg -= 0.5; T.FrontToeDeg = 0.0; T.RearToeDeg = 0.3; T.CasterDeg += 1.0; T.SteeringRatio = 13.0;
	T.BrakeBiasFrontPct += 3.0; T.BrakeForcePct = 110.0; T.DiffPowerLock = 3.0; T.DiffCoastLock = 2.0; T.FrontTyrePsi = 28.0; T.RearTyrePsi = 30.0;
	T.Clamp(S.Stock);
	const FString Id = TEXT("osds_tuning_test");
	TestTrue(TEXT("saved"), T.Save(Id));
	FOsdsTuning Back = S.Stock;
	TestTrue(TEXT("loaded"), Back.Load(Id));
	Back.Clamp(S.Stock);
	TestEqual(TEXT("every row back as saved"), FCrc::MemCrc32(&Back, sizeof(Back)), FCrc::MemCrc32(&T, sizeof(T)));
	IFileManager::Get().Delete(*FOsdsTuning::PathFor(Id));
	// A 2.1 file (one damping %, one ride height, a torque %) comes in as the new rows.
	const FString Old = TEXT("{\"damping_pct\":130,\"ride_height_mm\":-25,\"torque_pct\":110,\"front_spring_lb_in\":300}");
	TestTrue(TEXT("2.1 file written"), FFileHelper::SaveStringToFile(Old, *FOsdsTuning::PathFor(Id)));
	FOsdsTuning Migrated = S.Stock;
	TestTrue(TEXT("2.1 file loaded"), Migrated.Load(Id));
	TestEqual(TEXT("2.1 damping to all four dampers"), Migrated.RearReboundPct, 130.0);
	TestEqual(TEXT("2.1 ride height to both ends"), Migrated.RearRideHeightMm, -25.0);
	TestEqual(TEXT("2.1 torque % to the peak torque"), Migrated.PeakTorqueLbFt, S.Stock.PeakTorqueLbFt * 1.1, 1.0e-9);
	IFileManager::Get().Delete(*FOsdsTuning::PathFor(Id));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTuningRangesTest, "OSDS.Tuning.Ranges", OsdsTuningTest::Flags)
bool FOsdsTuningRangesTest::RunTest(const FString& Parameters)
{
	FSetup S;
	if (!Load(*this, TEXT("nb2001"), S)) return false;
	AddInfo(FString::Printf(TEXT("NB stock: %.0f lb-ft, %.0f hp, limit %.0f, %d gears, LSD %d, bias %.1f %% front"),
		S.Stock.PeakTorqueLbFt, S.Stock.PeakPowerHp, S.Stock.RevLimitRpm, S.Stock.NumGears, S.Stock.bHasLsd ? 1 : 0, S.Stock.BrakeBiasFrontPct));
	// The NB's published figures (2001: 142 hp at 7000 rpm, 125 lb-ft at 5500, Mazda) within the car file's curve's reach.
	TestTrue(TEXT("NB about 120-150 hp"), S.Stock.PeakPowerHp > 120.0 && S.Stock.PeakPowerHp < 150.0);
	TestTrue(TEXT("NB about 110-130 lb-ft"), S.Stock.PeakTorqueLbFt > 110.0 && S.Stock.PeakTorqueLbFt < 130.0);
	FOsdsTuning W = S.Stock;
	W.PeakTorqueLbFt = 1000.0; W.PeakPowerHp = 1000.0; W.RevLimitRpm = 20000.0; W.GearRatio[1] = 9.0; W.FrontRideHeightMm = 30.0; W.RearRideHeightMm = -60.0;
	W.BrakeBiasFrontPct = 100.0; W.DiffCoastLock = 9.0; W.EscLevel = 2.0; W.TcLevel = 2.0;
	W.Clamp(S.Stock);
	TestEqual(TEXT("torque held to +40 %"), W.PeakTorqueLbFt, S.Stock.PeakTorqueLbFt * 1.4, 1.0e-9);
	TestEqual(TEXT("power held to +50 %"), W.PeakPowerHp, S.Stock.PeakPowerHp * 1.5, 1.0e-9);
	TestEqual(TEXT("rev limit held to +500 rpm"), W.RevLimitRpm, S.Stock.RevLimitRpm + 500.0, 1.0e-9);
	TestTrue(TEXT("each gear taller than the one before"), W.GearRatio[1] < W.GearRatio[0]);
	TestEqual(TEXT("rake held to 40 mm"), W.RearRideHeightMm, -10.0, 1.0e-9);
	TestEqual(TEXT("brake bias held to +12 points"), W.BrakeBiasFrontPct, S.Stock.BrakeBiasFrontPct + 12.0, 1.0e-9);
	TestTrue(TEXT("coast lock no more than power lock"), W.DiffCoastLock <= W.DiffPowerLock);
	TestEqual(TEXT("no stability control on a 2001 NB to tune"), W.EscLevel, 0.0);
	// The shaped curve gives what the rows say.
	FOsdsTuning P = S.Stock;
	P.PeakTorqueLbFt *= 1.1;
	P.PeakPowerHp *= 1.2;
	P.Clamp(S.Stock);
	const FOsdsTuning::FPeaks Peaks = FOsdsTuning::EnginePeaks(P.Apply(S.Real, S.Stock));
	TestEqual(TEXT("tuned peak power is the row's hp"), Peaks.PowerW / OsdsTuningUnits::WattsPerHp, P.PeakPowerHp, 0.5);
	TestTrue(TEXT("tuned peak torque at least the row's"), Peaks.TorqueNm / OsdsTuningUnits::NmPerLbFt >= P.PeakTorqueLbFt - 0.5);
	// The estimate: in the same street as the bench for the stock car, and moving the same way.
	const FOsdsTuning::FEstimate Est = FOsdsTuning::EstimatePerformance(S.Real);
	AddInfo(FString::Printf(TEXT("NB estimate: 0-60 %.2f s (car file's published %.2f), top speed %.0f mph"), Est.ZeroToSixtyS, S.Real.TargetZeroToSixtySeconds, Est.TopSpeedMps / MphToMps));
	TestTrue(TEXT("estimate 0-60 within 30 % of the published time"), FMath::Abs(Est.ZeroToSixtyS / S.Real.TargetZeroToSixtySeconds - 1.0) < 0.3);
	TestTrue(TEXT("estimate top speed 100-150 mph"), Est.TopSpeedMps > 100.0 * MphToMps && Est.TopSpeedMps < 150.0 * MphToMps);
	const FOsdsTuning::FEstimate More = FOsdsTuning::EstimatePerformance(P.Apply(S.Real, S.Stock));
	TestTrue(TEXT("estimate: more power, quicker 0-60 and faster"), More.ZeroToSixtyS < Est.ZeroToSixtyS && More.TopSpeedMps > Est.TopSpeedMps);
	// Rows only where the real car has them.
	FSetup M;
	if (Load(*this, TEXT("meridian_z"), M))
	{
		TestTrue(TEXT("meridian_z has stability and traction control to tune"), M.Stock.bHasEsc && M.Stock.bHasTc);
	}
	FSetup Ms;
	if (Load(*this, TEXT("mustang65"), Ms))
	{
		TestFalse(TEXT("a 1965 Mustang's open diff has no lock to tune"), Ms.Stock.bHasLsd);
		TestFalse(TEXT("its live rear axle has no rear camber adjustment"), Ms.Stock.bRearCamberAdjustable);
	}
	FSetup E;
	if (Load(*this, TEXT("model3"), E))
	{
		TestTrue(TEXT("the Model 3 is electric: no rev limit"), E.Stock.bElectric && E.Stock.RevLimitRpm == 0.0);
		FOsdsTuning T = E.Stock;
		T.PeakPowerHp *= 1.2;
		T.Clamp(E.Stock);
		TestEqual(TEXT("motor power follows the row"), T.Apply(E.Real, E.Stock).ElectricMotorPeakPowerW, T.PeakPowerHp * OsdsTuningUnits::WattsPerHp, 1.0);
	}
	return true;
}

// ------------------------------------------------------------------ engine

OSDS_TUNING_ROW_TEST(PeakTorque, "nb2001")
{
	// More torque across the curve: a quicker 0-60 on CarTrack.
	return Moved(Test, TEXT("0-60 s with +20 % torque"), ZeroToSixty(S.Tuned()),
		ZeroToSixty(S.With([](FOsdsTuning& T) { T.PeakTorqueLbFt *= 1.2; T.PeakPowerHp *= 1.2; })), false, 0.05);
}

OSDS_TUNING_ROW_TEST(PeakPower, "nb2001")
{
	// More top-end power (the same peak torque): a higher top speed.
	const FOsdsTopSpeedResult Stock = OsdsRunTopSpeed(S.Tuned(), Dt);
	const FOsdsTopSpeedResult More = OsdsRunTopSpeed(S.With([](FOsdsTuning& T) { T.PeakPowerHp *= 1.2; }), Dt);
	return Moved(Test, TEXT("top speed m/s with +20 % power"), Stock.TopSpeedMps, More.TopSpeedMps, true, 1.0);
}

OSDS_TUNING_ROW_TEST(RevLimit, "nb2001")
{
	// A lower rev limit: the automatic changes up from first at a lower road speed.
	const TArray<double> Stock = Pull(S.Tuned(), 1);
	const TArray<double> Low = Pull(S.With([](FOsdsTuning& T) { T.RevLimitRpm -= 1000.0; }), 1);
	Test.TestTrue(TEXT("both changed up out of first"), Stock.Num() > 0 && Low.Num() > 0);
	return Stock.Num() > 0 && Low.Num() > 0 && Moved(Test, TEXT("1-2 upshift speed m/s with the limit 1000 rpm lower"), Stock[0], Low[0], false, 0.5);
}

// ------------------------------------------------------------------ gearbox

OSDS_TUNING_ROW_TEST(FinalDrive, "nb2001")
{
	// A shorter final drive: more pull in each gear, so a quicker 0-60 (the NB needs third either way).
	return Moved(Test, TEXT("0-60 s with a 15 % shorter final drive"), ZeroToSixty(S.Tuned()),
		ZeroToSixty(S.With([](FOsdsTuning& T) { T.FinalDrive *= 1.15; })), false, 0.02);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTuningRowGears, "OSDS.Tuning.Row.Gears", OsdsTuningTest::Flags)
bool FOsdsTuningRowGears::RunTest(const FString& Parameters)
{
	// Each gear row: that gear 8 % shorter, the automatic leaves it at a lower road speed (every gear
	// but top, which it never leaves: there the engine turns faster at top speed).
	FSetup S;
	if (!Load(*this, TEXT("nb2001"), S)) return false;
	const int32 Ups = S.Stock.NumGears - 1;
	const TArray<double> Stock = Pull(S.Tuned(), Ups);
	AddInfo(FString::Printf(TEXT("stock upshifts: %d"), Stock.Num()));
	for (int32 G = 0; G + 1 < S.Stock.NumGears; ++G)
	{
		const TArray<double> Short = Pull(S.With([G](FOsdsTuning& T) { T.GearRatio[G] *= 1.08; }), G + 1);
		if (!Stock.IsValidIndex(G) || !Short.IsValidIndex(G))
		{
			AddError(FString::Printf(TEXT("gear %d: no upshift out of it in 80 s"), G + 1));
			continue;
		}
		Moved(*this, FString::Printf(TEXT("gear %d: upshift speed m/s, 8 %% shorter"), G + 1), Stock[G], Short[G], false, 0.3);
	}
	const int32 Top = S.Stock.NumGears - 1;
	const FOsdsTopSpeedResult A = OsdsRunTopSpeed(S.Tuned(), Dt);
	const FOsdsTopSpeedResult B = OsdsRunTopSpeed(S.With([Top](FOsdsTuning& T) { T.GearRatio[Top] *= 1.08; }), Dt);
	TestTrue(TEXT("both reach top speed in top gear"), A.Gear == Top + 1 && B.Gear == Top + 1);
	Moved(*this, FString::Printf(TEXT("gear %d (top): rpm at top speed, 8 %% shorter"), Top + 1), A.Rpm, B.Rpm, true, 50.0);
	return true;
}

// ------------------------------------------------------------------ springs, dampers, bars, ride height

OSDS_TUNING_ROW_TEST(FrontSprings, "nb2001")
{
	// Stiffer front springs: less body roll per g on the step steer.
	return Moved(Test, TEXT("roll deg/g, front springs x1.6"), StepSteer(S.Tuned()).RollDegPerG,
		StepSteer(S.With([](FOsdsTuning& T) { T.FrontSpringLbIn *= 1.6; })).RollDegPerG, false, 0.05);
}

OSDS_TUNING_ROW_TEST(RearSprings, "nb2001")
{
	return Moved(Test, TEXT("roll deg/g, rear springs x1.6"), StepSteer(S.Tuned()).RollDegPerG,
		StepSteer(S.With([](FOsdsTuning& T) { T.RearSpringLbIn *= 1.6; })).RollDegPerG, false, 0.05);
}

namespace OsdsTuningTest
{
	TSharedRef<FHeave> Heave(const FOsdsCarDefinition& Car) { return Drive(Car, MakeShared<FHeave>(), 10.0); }
}

OSDS_TUNING_ROW_TEST(FrontBump, "nb2001")
{
	// More bump (compression) damping: the push sinks the front less deep.
	return Moved(Test, TEXT("front compression m after a 0.5 m/s push, bump x2"), Heave(S.Tuned())->Deepest[0],
		Heave(S.With([](FOsdsTuning& T) { T.FrontBumpPct = 200.0; }))->Deepest[0], false, 1.0e-4);
}

OSDS_TUNING_ROW_TEST(FrontRebound, "nb2001")
{
	// More rebound damping: the front comes back up from the push more slowly.
	return Moved(Test, TEXT("front extension speed m/s after the push, rebound x2"), Heave(S.Tuned())->ReboundSpeed[0],
		Heave(S.With([](FOsdsTuning& T) { T.FrontReboundPct = 200.0; }))->ReboundSpeed[0], false, 1.0e-3);
}

OSDS_TUNING_ROW_TEST(RearBump, "nb2001")
{
	return Moved(Test, TEXT("rear compression m after a 0.5 m/s push, bump x2"), Heave(S.Tuned())->Deepest[1],
		Heave(S.With([](FOsdsTuning& T) { T.RearBumpPct = 200.0; }))->Deepest[1], false, 1.0e-4);
}

OSDS_TUNING_ROW_TEST(RearRebound, "nb2001")
{
	return Moved(Test, TEXT("rear extension speed m/s after the push, rebound x2"), Heave(S.Tuned())->ReboundSpeed[1],
		Heave(S.With([](FOsdsTuning& T) { T.RearReboundPct = 200.0; }))->ReboundSpeed[1], false, 1.0e-3);
}

OSDS_TUNING_ROW_TEST(FrontBar, "nb2001")
{
	// The owner's example: a stiffer front bar, more understeer (less yaw for the same steer).
	return Moved(Test, TEXT("step-steer yaw deg/s, front bar x2.5"), StepSteer(S.Tuned()).SteadyYawDegS,
		StepSteer(S.With([](FOsdsTuning& T) { T.FrontBarPct = 250.0; })).SteadyYawDegS, false, 0.01);
}

OSDS_TUNING_ROW_TEST(RearBar, "nb2001")
{
	// A stiffer rear bar: less understeer (more yaw for the same steer).
	return Moved(Test, TEXT("step-steer yaw deg/s, rear bar x3"), StepSteer(S.Tuned()).SteadyYawDegS,
		StepSteer(S.With([](FOsdsTuning& T) { T.RearBarPct = 300.0; })).SteadyYawDegS, true, 0.01);
}

namespace OsdsTuningTest
{
	double Pitch(const FOsdsCarDefinition& Car) { return Drive(Car, MakeShared<FSettle>(), 6.0)->PitchDeg; }
}

OSDS_TUNING_ROW_TEST(FrontRideHeight, "nb2001")
{
	// The front 25 mm lower: the nose sits down (about atan(25 mm / wheelbase) = 0.6 deg).
	return Moved(Test, TEXT("nose-up pitch deg, front 25 mm lower"), Pitch(S.Tuned()),
		Pitch(S.With([](FOsdsTuning& T) { T.FrontRideHeightMm = -25.0; })), false, 0.3);
}

OSDS_TUNING_ROW_TEST(RearRideHeight, "nb2001")
{
	return Moved(Test, TEXT("nose-up pitch deg, rear 25 mm lower"), Pitch(S.Tuned()),
		Pitch(S.With([](FOsdsTuning& T) { T.RearRideHeightMm = -25.0; })), true, 0.3);
}

// ------------------------------------------------------------------ alignment

OSDS_TUNING_ROW_TEST(FrontCamber, "nb2001")
{
	// More negative front camber: the loaded outer front stands up in the roll and grips more, so less understeer.
	return Moved(Test, TEXT("step-steer yaw deg/s, front camber -2 deg more"), StepSteer(S.Tuned()).SteadyYawDegS,
		StepSteer(S.With([](FOsdsTuning& T) { T.FrontCamberDeg -= 2.0; })).SteadyYawDegS, true, 0.005);
}

OSDS_TUNING_ROW_TEST(RearCamber, "nb2001")
{
	// More negative rear camber: more rear grip, more understeer.
	return Moved(Test, TEXT("step-steer yaw deg/s, rear camber -2 deg more"), StepSteer(S.Tuned()).SteadyYawDegS,
		StepSteer(S.With([](FOsdsTuning& T) { T.RearCamberDeg -= 2.0; })).SteadyYawDegS, false, 0.005);
}

namespace OsdsTuningTest
{
	double Coast(const FOsdsCarDefinition& Car) { return Drive(Car, MakeShared<FCoast>(), 8.0)->EndMps; }
}

OSDS_TUNING_ROW_TEST(FrontToe, "nb2001")
{
	// More front toe: the tyres scrub, so the car coasts down faster.
	return Moved(Test, TEXT("speed m/s after 4 s coasting, front toe +0.4 deg"), Coast(S.Tuned()),
		Coast(S.With([](FOsdsTuning& T) { T.FrontToeDeg += 0.4; })), false, 1.0e-3);
}

OSDS_TUNING_ROW_TEST(RearToe, "nb2001")
{
	return Moved(Test, TEXT("speed m/s after 4 s coasting, rear toe +0.4 deg"), Coast(S.Tuned()),
		Coast(S.With([](FOsdsTuning& T) { T.RearToeDeg += 0.4; })), false, 1.0e-3);
}

OSDS_TUNING_ROW_TEST(Caster, "nb2001")
{
	// More caster: a longer mechanical trail, so a stronger self-centring torque at the rack in a
	// steady turn (heavier steering; through the steering's compliance also a little more understeer).
	return Moved(Test, TEXT("rack aligning torque N m on a steady circle, caster +2 deg"), CornerDiffOf(S.Tuned())->Aligning,
		CornerDiffOf(S.With([](FOsdsTuning& T) { T.CasterDeg += 2.0; }))->Aligning, true, 0.5);
}

OSDS_TUNING_ROW_TEST(SteeringRatio, "nb2001")
{
	// The ratio lives in the cockpit (the hand wheel to the rack), not the car file: kept in its range
	// and carried by the setup (AOsdsVehicle hands it to the model; OSDS.Vehicle.Interface drives it).
	FOsdsTuning T = S.Stock;
	T.SteeringRatio = 1.0;
	T.Clamp(S.Stock);
	Test.TestEqual(TEXT("a quicker ratio held to 60 % of the real one"), T.SteeringRatio, S.Stock.SteeringRatio * 0.6, 1.0e-9);
	return true;
}

// ------------------------------------------------------------------ brakes and differential

namespace OsdsTuningTest
{
	TSharedRef<FBrake> Brake(const FOsdsCarDefinition& Car) { return Drive(Car, MakeShared<FBrake>(), 40.0); }
}

OSDS_TUNING_ROW_TEST(BrakeBias, "nb2001")
{
	// More front bias: the fronts do more of the braking (more slip at the front than the rear).
	return Moved(Test, TEXT("front-minus-rear brake slip, bias +8 points"), Brake(S.Tuned())->SlipSum,
		Brake(S.With([](FOsdsTuning& T) { T.BrakeBiasFrontPct += 8.0; }))->SlipSum, true, 1.0e-4);
}

OSDS_TUNING_ROW_TEST(BrakeForce, "nb2001")
{
	// Less brake force: the same pedal stops the car in a longer distance.
	return Moved(Test, TEXT("70-0 m at half pedal, force 75 %"), Brake(S.Tuned())->DistanceM,
		Brake(S.With([](FOsdsTuning& T) { T.BrakeForcePct = 75.0; }))->DistanceM, true, 1.0);
}

OSDS_TUNING_ROW_TEST(DiffPowerLock, "nb2001")
{
	// The lock on power taken off (1:1, an open diff): the unloaded inner rear spins up out of a
	// tight turn, so the rear wheels' speeds differ more; at 4:1 they differ no more than stock.
	const double Stock = CornerDiffOf(S.Tuned())->Power;
	const double Open = CornerDiffOf(S.With([](FOsdsTuning& T) { T.DiffPowerLock = 1.0; T.DiffCoastLock = 1.0; }))->Power;
	const double Tight = CornerDiffOf(S.With([](FOsdsTuning& T) { T.DiffPowerLock = 4.0; }))->Power;
	Test.TestTrue(FString::Printf(TEXT("4:1 holds them no further apart than stock (%.4f vs %.4f)"), Tight, Stock), Tight <= Stock + 1.0e-6);
	return Moved(Test, TEXT("rear wheel speed difference rad/s on power, open (1:1)"), Stock, Open, true, 1.0e-3);
}

OSDS_TUNING_ROW_TEST(DiffCoastLock, "nb2001")
{
	// And on the coast.
	return Moved(Test, TEXT("rear wheel speed difference rad/s on the coast, coast lock 2:1 (power 2:1)"), CornerDiffOf(S.Tuned())->Coast,
		CornerDiffOf(S.With([](FOsdsTuning& T) { T.DiffCoastLock = 2.0; }))->Coast, false, 1.0e-4);
}

// ------------------------------------------------------------------ tyres and aids

OSDS_TUNING_ROW_TEST(FrontTyrePressure, "nb2001")
{
	// Lower front pressure (toward the grip peak): more front grip, less understeer.
	return Moved(Test, TEXT("step-steer yaw deg/s, front 6 psi lower"), StepSteer(S.Tuned()).SteadyYawDegS,
		StepSteer(S.With([](FOsdsTuning& T) { T.FrontTyrePsi -= 6.0; })).SteadyYawDegS, true, 0.005);
}

OSDS_TUNING_ROW_TEST(RearTyrePressure, "nb2001")
{
	// Lower rear pressure: more rear grip, more understeer.
	return Moved(Test, TEXT("step-steer yaw deg/s, rear 6 psi lower"), StepSteer(S.Tuned()).SteadyYawDegS,
		StepSteer(S.With([](FOsdsTuning& T) { T.RearTyrePsi -= 6.0; })).SteadyYawDegS, false, 0.005);
}

OSDS_TUNING_ROW_TEST(StabilityControl, "meridian_z")
{
	// Sport lets the car rotate further before it brakes (the FMVSS 126 sine with dwell: more yaw
	// left 1 s after the steering ends); Off brakes nothing.
	const FOsdsSineDwellResult On = OsdsRunSineDwell(S.Tuned(), Dt, true);
	const FOsdsSineDwellResult Sport = OsdsRunSineDwell(S.With([](FOsdsTuning& T) { T.EscLevel = 1.0; }), Dt, true);
	const FOsdsSineDwellResult Off = OsdsRunSineDwell(S.With([](FOsdsTuning& T) { T.EscLevel = 0.0; }), Dt, true);
	Test.AddInfo(FString::Printf(TEXT("ESC acting s: on %.2f, sport %.2f, off %.2f"), On.EscActiveS, Sport.EscActiveS, Off.EscActiveS));
	Test.TestEqual(TEXT("Off: the system never acts"), Off.EscActiveS, 0.0);
	return Moved(Test, TEXT("seconds ESC acts in the sine with dwell, Sport"), On.EscActiveS, Sport.EscActiveS, false, 0.01);
}

OSDS_TUNING_ROW_TEST(TractionControl, "meridian_z")
{
	// Sport lets the driven wheels slip further at a full-throttle launch; Off further still.
	const double On = Drive(S.Tuned(), MakeShared<FLaunchSlip>(), 6.0)->PeakSlip;
	const double Sport = Drive(S.With([](FOsdsTuning& T) { T.TcLevel = 1.0; }), MakeShared<FLaunchSlip>(), 6.0)->PeakSlip;
	const double Off = Drive(S.With([](FOsdsTuning& T) { T.TcLevel = 0.0; }), MakeShared<FLaunchSlip>(), 6.0)->PeakSlip;
	Moved(Test, TEXT("peak launch slip, Off over On"), On, Off, true, 1.0e-3);
	return Moved(Test, TEXT("peak launch slip, Sport over On"), On, Sport, true, 1.0e-3);
}

#endif
