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
