// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Vehicle/OsdsDifferential.h"

namespace
{
	constexpr EAutomationTestFlags DiffTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;
	constexpr double Dt = 1.0 / 360.0;

	FOsdsDifferentialInput MakeInput(double TorqueNm, double W0, double W1, double W2, double W3, bool bDrive = true)
	{
		FOsdsDifferentialInput In;
		In.AxleTorqueNm = TorqueNm;
		In.WheelOmegaRadPerSecond[0] = W0;
		In.WheelOmegaRadPerSecond[1] = W1;
		In.WheelOmegaRadPerSecond[2] = W2;
		In.WheelOmegaRadPerSecond[3] = W3;
		In.DeltaTimeS = Dt;
		In.bDrive = bDrive;
		return In;
	}

	FOsdsDifferentialOutput Run(const FOsdsCarDefinition& Car, const FOsdsDifferentialInput& In)
	{
		FOsdsDifferential Diff;
		Diff.Configure(Car);
		FOsdsDifferentialOutput Out;
		Diff.Step(In, Out);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsDifferentialOpenTest, "OSDS.Vehicle.Differential.Open", DiffTestFlags)
bool FOsdsDifferentialOpenTest::RunTest(const FString&)
{
	FOsdsCarDefinition Car;
	Car.DrivetrainLayout = TEXT("rwd");
	Car.RearDifferentialBiasRatio = 1.0;
	const FOsdsDifferentialOutput Out = Run(Car, MakeInput(1000.0, 0.0, 0.0, 30.0, 60.0));
	TestEqual(TEXT("open: left"), Out.WheelTorqueNm[2], 500.0, 1.0e-12);
	TestEqual(TEXT("open: right"), Out.WheelTorqueNm[3], 500.0, 1.0e-12);
	TestEqual(TEXT("rwd: front axle gets nothing"), Out.WheelTorqueNm[0] + Out.WheelTorqueNm[1], 0.0, 0.0);
	TestEqual(TEXT("no brake request yet"), Out.WheelBrakeNm[2] + Out.WheelBrakeNm[3], 0.0, 0.0);

	Car.DrivetrainLayout = TEXT("fwd");
	Car.FrontDifferentialBiasRatio = 1.0;
	const FOsdsDifferentialOutput Fwd = Run(Car, MakeInput(800.0, 60.0, 30.0, 0.0, 0.0));
	TestEqual(TEXT("fwd: left"), Fwd.WheelTorqueNm[0], 400.0, 1.0e-12);
	TestEqual(TEXT("fwd: rear gets nothing"), Fwd.WheelTorqueNm[2] + Fwd.WheelTorqueNm[3], 0.0, 0.0);

	FOsdsDifferential Diff;
	Diff.Configure(Car);
	const double Spin[4] = { 10.0, 20.0, 99.0, 99.0 };
	TestEqual(TEXT("fwd: driven speed is the front mean"), Diff.DrivenOmega(Spin), 15.0, 1.0e-12);
	Car.DrivetrainLayout = TEXT("rwd");
	Diff.Configure(Car);
	const double Spin2[4] = { 99.0, 99.0, 10.0, 20.0 };
	TestEqual(TEXT("rwd: driven speed is the rear mean"), Diff.DrivenOmega(Spin2), 15.0, 1.0e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsDifferentialBiasTest, "OSDS.Vehicle.Differential.Bias", DiffTestFlags)
bool FOsdsDifferentialBiasTest::RunTest(const FString&)
{
	FOsdsCarDefinition Car;
	Car.DrivetrainLayout = TEXT("rwd");
	Car.RearDifferentialBiasRatio = 2.0;
	Car.RearDifferentialCoastBiasRatio = 1.5;
	FOsdsDifferentialOutput Out = Run(Car, MakeInput(1000.0, 0.0, 0.0, 30.0, 60.0));
	TestEqual(TEXT("drive: ratio capped at the bias"), Out.WheelTorqueNm[2] / Out.WheelTorqueNm[3], 2.0, 1.0e-9);
	TestEqual(TEXT("drive: conserves torque"), Out.WheelTorqueNm[2] + Out.WheelTorqueNm[3], 1000.0, 1.0e-9);
	Out = Run(Car, MakeInput(1000.0, 0.0, 0.0, 30.0, 30.0));
	TestEqual(TEXT("equal speeds split equally"), Out.WheelTorqueNm[2], 500.0, 1.0e-12);
	Out = Run(Car, MakeInput(1000.0, 0.0, 0.0, 60.0, 30.0));
	TestTrue(TEXT("the slower wheel gets more"), Out.WheelTorqueNm[3] > Out.WheelTorqueNm[2]);

	Out = Run(Car, MakeInput(-200.0, 0.0, 0.0, 30.0, 60.0, false));
	TestEqual(TEXT("coast: capped at the coast bias"),
		FMath::Max(FMath::Abs(Out.WheelTorqueNm[2]), FMath::Abs(Out.WheelTorqueNm[3])) /
		FMath::Min(FMath::Abs(Out.WheelTorqueNm[2]), FMath::Abs(Out.WheelTorqueNm[3])), 1.5, 1.0e-9);
	TestEqual(TEXT("coast: conserves torque"), Out.WheelTorqueNm[2] + Out.WheelTorqueNm[3], -200.0, 1.0e-9);

	Car.DrivetrainLayout = TEXT("fwd");
	Car.FrontDifferentialBiasRatio = 3.0;
	Out = Run(Car, MakeInput(1000.0, 30.0, 60.0, 0.0, 0.0));
	TestEqual(TEXT("fwd drive uses the front bias"), Out.WheelTorqueNm[0] / Out.WheelTorqueNm[1], 3.0, 1.0e-9);
	Out = Run(Car, MakeInput(-200.0, 30.0, 60.0, 0.0, 0.0, false));
	TestEqual(TEXT("fwd coast is open"), Out.WheelTorqueNm[0], Out.WheelTorqueNm[1], 1.0e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsDifferentialFixedAwdTest, "OSDS.Vehicle.Differential.FixedAwd", DiffTestFlags)
bool FOsdsDifferentialFixedAwdTest::RunTest(const FString&)
{
	FOsdsCarDefinition Car;
	Car.DrivetrainLayout = TEXT("awd");
	Car.AwdFrontTorqueShareFraction = 0.4;
	Car.CenterDifferentialBiasRatio = 1.0;
	Car.FrontDifferentialBiasRatio = 1.0;
	Car.RearDifferentialBiasRatio = 1.0;
	FOsdsDifferentialOutput Out = Run(Car, MakeInput(1000.0, 20.0, 20.0, 40.0, 40.0));
	TestEqual(TEXT("fixed split: front share"), Out.WheelTorqueNm[0] + Out.WheelTorqueNm[1], 400.0, 1.0e-9);
	TestEqual(TEXT("fixed split: rear share"), Out.WheelTorqueNm[2] + Out.WheelTorqueNm[3], 600.0, 1.0e-9);

	// Limited-slip centre: the faster rear axle sends torque to the slower front, never beyond the bias limit.
	Car.CenterDifferentialBiasRatio = 3.0;
	Out = Run(Car, MakeInput(1000.0, 20.0, 20.0, 400.0, 400.0));
	const double Limit = 1000.0 * 0.5 * (3.0 - 1.0) / (3.0 + 1.0);
	TestEqual(TEXT("centre lock: front gains the limit"), Out.WheelTorqueNm[0] + Out.WheelTorqueNm[1], 400.0 + Limit, 1.0e-9);
	TestEqual(TEXT("centre lock: conserves torque"),
		Out.WheelTorqueNm[0] + Out.WheelTorqueNm[1] + Out.WheelTorqueNm[2] + Out.WheelTorqueNm[3], 1000.0, 1.0e-9);
	Out = Run(Car, MakeInput(-300.0, 20.0, 20.0, 400.0, 400.0, false));
	TestEqual(TEXT("coast: centre not applied"), Out.WheelTorqueNm[0] + Out.WheelTorqueNm[1], -120.0, 1.0e-9);

	FOsdsDifferential Diff;
	Diff.Configure(Car);
	const double Spin[4] = { 10.0, 20.0, 30.0, 50.0 };
	TestEqual(TEXT("awd: driven speed follows the fixed split"), Diff.DrivenOmega(Spin), 0.4 * 15.0 + 0.6 * 40.0, 1.0e-12);
	return true;
}

#endif

// The pre-#48 split, copied verbatim from OsdsDynamics::SplitDriveTorqueToWheels at 588d2e2, kept here
// as the reference the Differential module must equal exactly on identical inputs (the Bench lines
// shift in the last bits only because the compiler fuses the same maths differently in a new file).
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	namespace Legacy
	{
		TPair<double, double> SplitAxleTorque(double AxleTorqueNm, double LeftWheelAngularSpeed, double RightWheelAngularSpeed,
			double WheelInertiaKgM2, double DeltaTimeS, bool bIsDriveTorque, double BiasRatio)
		{
			const double HalfTorqueNm = AxleTorqueNm * 0.5;
			if (DeltaTimeS <= 0.0 || BiasRatio <= 1.0)
			{
				return TPair<double, double>(HalfTorqueNm, HalfTorqueNm);
			}
			const double TransferLimitNm = FMath::Abs(HalfTorqueNm) * (BiasRatio - 1.0) / (BiasRatio + 1.0);
			const double EqualizingTorqueNm = (RightWheelAngularSpeed - LeftWheelAngularSpeed) *
				WheelInertiaKgM2 / (2.0 * DeltaTimeS);
			const double TransferNm = FMath::Clamp(EqualizingTorqueNm, -TransferLimitNm, TransferLimitNm);
			return TPair<double, double>(HalfTorqueNm + TransferNm, HalfTorqueNm - TransferNm);
		}

		void Split(const FOsdsCarDefinition& Car, double AxleTorqueNm, const double W[4], double DeltaTimeS, bool bIsDriveTorque, double Out[4])
		{
			for (int32 Index = 0; Index < 4; ++Index) { Out[Index] = 0.0; }
			const double I = Car.WheelRotationalInertiaKgM2;
			if (Car.DrivetrainLayout == TEXT("fwd"))
			{
				const TPair<double, double> Front = SplitAxleTorque(AxleTorqueNm, W[0], W[1], I, DeltaTimeS, bIsDriveTorque,
					bIsDriveTorque ? Car.FrontDifferentialBiasRatio : 1.0);
				Out[0] = Front.Key;
				Out[1] = Front.Value;
				return;
			}
			double RearTorqueNm = AxleTorqueNm;
			if (Car.DrivetrainLayout == TEXT("awd"))
			{
				double FrontTorqueNm = AxleTorqueNm * Car.AwdFrontTorqueShareFraction;
				RearTorqueNm = AxleTorqueNm * (1.0 - Car.AwdFrontTorqueShareFraction);
				if (bIsDriveTorque && DeltaTimeS > 0.0 && Car.CenterDifferentialBiasRatio > 1.0)
				{
					const double TransferLimitNm = FMath::Abs(AxleTorqueNm) * 0.5 *
						(Car.CenterDifferentialBiasRatio - 1.0) / (Car.CenterDifferentialBiasRatio + 1.0);
					const double AxleSpeedDifference = (W[2] + W[3] - W[0] - W[1]) * I / DeltaTimeS;
					const double TransferNm = FMath::Clamp(AxleSpeedDifference, -TransferLimitNm, TransferLimitNm);
					FrontTorqueNm += TransferNm;
					RearTorqueNm -= TransferNm;
				}
				const TPair<double, double> Front = SplitAxleTorque(FrontTorqueNm, W[0], W[1], I, DeltaTimeS, bIsDriveTorque,
					bIsDriveTorque ? Car.FrontDifferentialBiasRatio : 1.0);
				Out[0] = Front.Key;
				Out[1] = Front.Value;
			}
			const TPair<double, double> Rear = SplitAxleTorque(RearTorqueNm, W[2], W[3], I, DeltaTimeS, bIsDriveTorque,
				bIsDriveTorque ? Car.RearDifferentialBiasRatio : Car.RearDifferentialCoastBiasRatio);
			Out[2] = Rear.Key;
			Out[3] = Rear.Value;
		}

		double DrivenOmega(const FOsdsCarDefinition& Car, const double W[4])
		{
			if (Car.DrivetrainLayout == TEXT("fwd")) { return (W[0] + W[1]) * 0.5; }
			if (Car.DrivetrainLayout == TEXT("awd"))
			{
				const double Share = Car.AwdFrontTorqueShareFraction;
				return Share * (W[0] + W[1]) * 0.5 + (1.0 - Share) * (W[2] + W[3]) * 0.5;
			}
			return (W[2] + W[3]) * 0.5;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsDifferentialMatchesLegacyTest, "OSDS.Vehicle.Differential.MatchesLegacy", DiffTestFlags)
bool FOsdsDifferentialMatchesLegacyTest::RunTest(const FString&)
{
	// Four layouts: rwd, fwd, awd with an open centre, awd with a limited-slip centre.
	struct FLayout { const TCHAR* Name; double CenterBias; };
	const FLayout Layouts[4] = { { TEXT("rwd"), 1.0 }, { TEXT("fwd"), 1.0 }, { TEXT("awd"), 1.0 }, { TEXT("awd"), 2.5 } };
	const double Biases[] = { 0.5, 1.0, 1.0000001, 1.5, 2.0, 3.0, 4.0 };
	const double Shares[] = { 0.0, 0.35, 0.5, 1.0 };
	const double Dts[] = { 0.0, 1.0 / 360.0, 1.0 / 720.0 };
	uint32 Seed = 12345u;
	auto Rand01 = [&Seed]() { Seed = Seed * 1664525u + 1013904223u; return static_cast<double>(Seed >> 8) / static_cast<double>(1u << 24); };
	int32 Cases = 0;
	int32 Mismatches = 0;
	for (const FLayout& L : Layouts)
	{
		for (double FrontBias : Biases) for (double RearBias : Biases) for (double CoastBias : Biases) for (double Share : Shares)
		{
			FOsdsCarDefinition Car;
			Car.DrivetrainLayout = L.Name;
			Car.CenterDifferentialBiasRatio = L.CenterBias;
			Car.FrontDifferentialBiasRatio = FrontBias;
			Car.RearDifferentialBiasRatio = RearBias;
			Car.RearDifferentialCoastBiasRatio = CoastBias;
			Car.AwdFrontTorqueShareFraction = Share;
			Car.WheelRotationalInertiaKgM2 = 0.9 + 0.2 * Rand01();
			FOsdsDifferential Diff;
			Diff.Configure(Car);
			for (double Dt2 : Dts) for (int32 Drive = 0; Drive < 2; ++Drive) for (int32 Rep = 0; Rep < 3; ++Rep)
			{
				const double Torque = (Rand01() - 0.4) * 2000.0;
				double W[4];
				for (double& V : W) { V = Rand01() * 200.0 - 20.0; }
				if (Rep == 2) { W[0] = W[1] = W[2] = W[3] = 55.5; }
				FOsdsDifferentialInput In;
				In.AxleTorqueNm = Torque;
				for (int32 K = 0; K < 4; ++K) { In.WheelOmegaRadPerSecond[K] = W[K]; }
				In.DeltaTimeS = Dt2;
				In.bDrive = Drive != 0;
				FOsdsDifferentialOutput Out;
				Diff.Step(In, Out);
				double Ref[4];
				Legacy::Split(Car, Torque, W, Dt2, Drive != 0, Ref);
				++Cases;
				for (int32 K = 0; K < 4; ++K) { if (Out.WheelTorqueNm[K] != Ref[K]) { ++Mismatches; break; } }
				if (Diff.DrivenOmega(W) != Legacy::DrivenOmega(Car, W)) { ++Mismatches; }
			}
		}
	}
	TestEqual(FString::Printf(TEXT("module equals the legacy split exactly on %d identical inputs"), Cases), Mismatches, 0);
	return true;
}
#endif
