// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/VehicleBenchWorld.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/OsdsUnits.h"
#include "Vehicle/OsdsVehicleDynamics.h"
#include "Vehicle/OsdsVehicleSim.h"
#include "Vehicle/OsdsTuning.h"

namespace
{
	constexpr EAutomationTestFlags VehicleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;
	const TCHAR* const BenchCars[] = { TEXT("nb1999"), TEXT("mustang65"), TEXT("meridian_z") };

	/** A pure-model step on an analytic flat road at Z = 0 (no engine objects). */
	struct FFlatRoad
	{
		bool operator()(const FVector3d& StartM, const FVector3d& EndM, FOsdsGroundHit& OutHit) const
		{
			if (StartM.Z < 0.0 || EndM.Z > 0.0)
			{
				return false;
			}
			const double Alpha = StartM.Z / (StartM.Z - EndM.Z);
			OutHit.PointM = FMath::Lerp(StartM, EndM, Alpha);
			OutHit.NormalM = FVector3d::UnitZ();
			return true;
		}
	};

	FOsdsBodyState BodyAtRideHeight(const FOsdsVehicleSim& Sim, const FVector3d& VelocityMps)
	{
		FOsdsBodyState Body;
		Body.PositionM = -Sim.GetReferenceOriginLocalM();
		Body.CenterOfMassM = Body.PositionM + Sim.GetCenterOfMassLocalM();
		Body.LinearVelocityMps = VelocityMps;
		return Body;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelSuspensionPreloadTest, "OSDS.Vehicle.Model.SuspensionPreload", VehicleTestFlags)
bool FOsdsVehicleModelSuspensionPreloadTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* CarId : BenchCars)
	{
		FOsdsCarDefinition Car;
		if (!OsdsLoadBenchCar(*this, CarId, Car))
		{
			continue;
		}
		for (int32 Axle = 0; Axle < 2; ++Axle)
		{
			const double Share = Axle == 0 ? Car.FrontWeightFraction : 1.0 - Car.FrontWeightFraction;
			const double Corner = Car.MassKg * Share * 0.5 * OsdsDynamics::GravityMps2;
			TestEqual(FString::Printf(TEXT("%s axle %d carries its corner weight at static length"), CarId, Axle),
				OsdsDynamics::ComputeSuspensionLoadN(Car, Axle, Car.StaticSuspensionLengthM, 0.0), Corner, 1.0e-6);
			// Compression adds the wheel rate; rebound (+ velocity) removes damper force.
			const double Rate = OsdsDynamics::GetWheelSpringRateNPerM(Car, Axle);
			TestEqual(FString::Printf(TEXT("%s axle %d spring rate"), CarId, Axle),
				OsdsDynamics::ComputeSuspensionLoadN(Car, Axle, Car.StaticSuspensionLengthM - 0.01, 0.0), Corner + 0.01 * Rate, 1.0e-6);
			TestTrue(FString::Printf(TEXT("%s axle %d damper resists rebound"), CarId, Axle),
				OsdsDynamics::ComputeSuspensionLoadN(Car, Axle, Car.StaticSuspensionLengthM, 0.2) < Corner);
			TestTrue(FString::Printf(TEXT("%s axle %d damper resists compression"), CarId, Axle),
				OsdsDynamics::ComputeSuspensionLoadN(Car, Axle, Car.StaticSuspensionLengthM, -0.2) > Corner);
		}
	}
	// The NB's rates as CarSuspension.gd computes them.
	FOsdsCarDefinition Nb;
	TestEqual(TEXT("NB front wheel rate"), OsdsDynamics::GetWheelSpringRateNPerM(Nb, 0), 168.0 * 175.126835 * 0.68 * 0.68, 1.0e-6);
	TestTrue(TEXT("NB front bar is stiffer than the rear"), OsdsDynamics::GetAntiRollBarRateNPerM(Nb, 0) > OsdsDynamics::GetAntiRollBarRateNPerM(Nb, 1));
	// CarSuspension.gd roll_bar_rate, written out: the 11 mm rear bar's second
	// moment (7.2e-10 m^4) once fell under a small-number guard and read 0.
	for (int32 Axle = 0; Axle < 2; ++Axle)
	{
		const double D = Nb.AntiRollBarDiameterMByAxle[Axle];
		const double Arm = Nb.AntiRollBarArmLengthMByAxle[Axle];
		const double Moment = UE_DOUBLE_PI * D * D * D * D / 64.0;
		const double Twist = Arm * Arm * Nb.AntiRollBarTorsionLengthMByAxle[Axle] / (79.0e9 * 2.0 * Moment);
		const double Bending = 2.0 * Arm * Arm * Arm / (3.0 * 205.0e9 * Moment);
		const double Expected = 0.82 * FMath::Square(Nb.AntiRollBarLeverRatioByAxle[Axle]) / (Twist + Bending);
		TestEqual(FString::Printf(TEXT("NB anti-roll bar rate, axle %d"), Axle), OsdsDynamics::GetAntiRollBarRateNPerM(Nb, Axle), Expected, 1.0e-6);
	}
	TestTrue(TEXT("NB rear bar is not zero"), OsdsDynamics::GetAntiRollBarRateNPerM(Nb, 1) > 2000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelTyreTest, "OSDS.Vehicle.Model.Tyre", VehicleTestFlags)
bool FOsdsVehicleModelTyreTest::RunTest(const FString& Parameters)
{
	const FOsdsCarDefinition Car;
	const double Fz = Car.ReferenceCornerLoadN;
	const double Mu = OsdsDynamics::ComputeFrictionCoefficient(Car, 0, Fz, 1.0);
	TestEqual(TEXT("reference load gets the peak friction"), Mu, Car.PeakLongitudinalFrictionCoefficient, 1.0e-12);
	TestTrue(TEXT("load sensitivity: more load, less friction"), OsdsDynamics::ComputeFrictionCoefficient(Car, 0, Fz * 1.5, 1.0) < Mu);
	// Sliding to the right (positive slip angle) gives a force to the left.
	TestTrue(TEXT("lateral force opposes slip"), OsdsDynamics::ComputeLateralForceN(Car, Fz, Mu, 0.05, 0.0, 20.0) < 0.0);
	TestTrue(TEXT("camber thrust toward the lean"), OsdsDynamics::ComputeLateralForceN(Car, Fz, Mu, 0.0, 0.02, 20.0) > 0.0);
	const double Saturated = OsdsDynamics::ComputeLateralForceN(Car, Fz, Mu, 0.6, 0.0, 20.0);
	TestTrue(TEXT("lateral force saturates near mu Fz times the lateral scale"),
		FMath::Abs(Saturated) <= Fz * Mu * Car.LateralGripScale + 1.0 && FMath::Abs(Saturated) > 0.8 * Fz * Mu * Car.LateralGripScale);

	// Relaxation: a step in slip angle builds over the relaxation length.
	double Rolling = 0.0;
	const double First = OsdsDynamics::ComputeRelaxedSlipAngleRad(Car, 20.0, 1.0, 1.0 / 360.0, Fz, Rolling);
	const double Target = FMath::Atan2(1.0, 20.0);
	TestTrue(TEXT("relaxed slip lags the target"), First > 0.0 && First < Target);
	for (int32 Step = 0; Step < 360; ++Step)
	{
		OsdsDynamics::ComputeRelaxedSlipAngleRad(Car, 20.0, 1.0, 1.0 / 360.0, Fz, Rolling);
	}
	TestEqual(TEXT("relaxed slip reaches the target after 20 m"), Rolling, Target, 1.0e-9);

	// Longitudinal: a driven wheel spinning faster than the road pushes forward; a locked wheel slides at mu Fz.
	FOsdsLongitudinalSubstepInput In;
	In.NormalLoadN = Fz;
	In.FrictionCoefficient = Mu;
	In.ForwardSpeedMps = 10.0;
	In.TireRadiusM = Car.TireFreeRadiusM();
	In.WheelInertiaKgM2 = Car.WheelRotationalInertiaKgM2;
	In.SubstepS = 1.0 / 360.0;
	In.StartAngularSpeedRadPerSecond = 11.0 / In.TireRadiusM;
	In.SpinAfterTorqueRadPerSecond = In.StartAngularSpeedRadPerSecond;
	const FOsdsLongitudinalSubstepOutput Drive = OsdsDynamics::SolveLongitudinalSubstep(Car, In);
	TestTrue(TEXT("drive slip pushes forward"), Drive.LongitudinalForceN > 0.0 && Drive.SlipRatio > 0.0);
	TestTrue(TEXT("the friction circle caps the force"), Drive.LongitudinalForceN <= Fz * Mu + 1.0e-6);
	In.StartAngularSpeedRadPerSecond = 0.0;
	In.SpinAfterTorqueRadPerSecond = 0.0;
	In.BrakeTorqueNm = 2000.0;
	const FOsdsLongitudinalSubstepOutput Locked = OsdsDynamics::SolveLongitudinalSubstep(Car, In);
	TestEqual(TEXT("a locked wheel slides at mu Fz"), Locked.LongitudinalForceN, -Fz * Mu, 1.0e-6);
	TestEqual(TEXT("a locked wheel stays locked under the brake"), Locked.EndAngularSpeedRadPerSecond, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelTmeasyTest, "OSDS.Vehicle.Model.TyreTmeasy", VehicleTestFlags)
bool FOsdsVehicleModelTmeasyTest::RunTest(const FString& Parameters)
{
	// The shape of Chrono's passenger-tyre set (GuessPassCar70Par) as this code evaluates it.
	FOsdsCarDefinition Car;
	Car.TyreModel = TEXT("tmeasy");
	const double Fz = Car.ReferenceCornerLoadN;
	OsdsDynamics::FTmeasyCurve X, Y;
	OsdsDynamics::GetTmeasyCurves(Car, Fz, 1.0, X, Y);
	TestEqual(TEXT("nominal load: lateral peak is mu_y Fz"), Y.PeakForce, Car.TmeasyPeakMuY * Fz, 1.0e-6);
	TestEqual(TEXT("nominal load: longitudinal slide is mu_x,slide Fz"), X.SlideForce, Car.TmeasySlideMuX * Fz, 1.0e-6);
	FOsdsCarDefinition Twice = Car;
	OsdsDynamics::FTmeasyCurve X2, Y2;
	OsdsDynamics::GetTmeasyCurves(Car, 2.0 * Fz, 1.0, X2, Y2);
	TestEqual(TEXT("twice the load: lateral peak from the 2 pn set"), Y2.PeakForce, 2.0 * Fz * Car.TmeasyPeakMuY * Car.TmeasyPeakMuY2Ratio, 1.0e-6);
	// Chrono's set gives 1.61 (12.85 per unit at 2 pn against 15.98 at pn); a linear stiffness would be 2.
	TestTrue(TEXT("cornering stiffness saturates with load (K(2Fz)/K(Fz) at most 1.65)"), Y2.Stiffness / Y.Stiffness <= 1.65);
	TestTrue(TEXT("load sensitivity: the peak friction falls with load"), Y2.PeakForce / (2.0 * Fz) < Y.PeakForce / Fz);

	auto Lateral = [&](double AlphaDeg, double SlipX)
	{
		double Secant = 0.0, PeakX = 0.0;
		const double SlipY = FMath::Tan(FMath::DegreesToRadians(AlphaDeg));
		OsdsDynamics::TmeasyCombined(Car, Fz, 1.0, SlipX, SlipY, Secant, PeakX);
		return Secant * SlipY;
	};
	double PeakAlpha = 0.0, PeakForce = 0.0;
	for (double A = 0.25; A <= 20.0; A += 0.25)
	{
		const double F = Lateral(A, 0.0);
		if (F > PeakForce) { PeakForce = F; PeakAlpha = A; }
	}
	AddInfo(FString::Printf(TEXT("TMeasy pure lateral: peak %.2f Fz at %.2f deg; %.0f %% of it at 4 deg; slope %.1f /rad"),
		PeakForce / Fz, PeakAlpha, 100.0 * Lateral(4.0, 0.0) / PeakForce, Lateral(0.25, 0.0) / FMath::DegreesToRadians(0.25) / Fz));
	TestTrue(TEXT("radial tyre: peak slip angle 6-9 deg"), PeakAlpha >= 6.0 && PeakAlpha <= 9.0);
	TestTrue(TEXT("at 4 deg the tyre already gives at least 75 % of its peak"), Lateral(4.0, 0.0) >= 0.75 * PeakForce);
	TestTrue(TEXT("past the peak the force falls toward sliding, not to zero"), Lateral(20.0, 0.0) > 0.75 * Car.TmeasySlideMuY * Fz);
	const double Combined = Lateral(4.0, 0.05) / Lateral(4.0, 0.0);
	AddInfo(FString::Printf(TEXT("TMeasy combined: at 5 %% drive slip the 4 deg side force is %.0f %%"), 100.0 * Combined));
	// TMeasy couples the directions through the combined slip only: at a third of the peak slip it
	// costs little (Chrono's shape: 5 %); near the peak it costs much more (below).
	TestTrue(TEXT("combined slip: 5 % drive slip costs 3-20 % of the 4 deg side force"), Combined >= 0.80 && Combined <= 0.97);
	TestTrue(TEXT("combined slip: at the drive peak (14 %) the 4 deg side force falls below 70 %"), Lateral(4.0, 0.14) / Lateral(4.0, 0.0) < 0.70);

	// A locked wheel at speed slides at the full-slide force.
	FOsdsLongitudinalSubstepInput In;
	In.NormalLoadN = Fz;
	In.ForwardSpeedMps = 20.0;
	In.TireRadiusM = Car.TireFreeRadiusM();
	In.WheelInertiaKgM2 = Car.WheelRotationalInertiaKgM2;
	In.SubstepS = 1.0 / 360.0;
	In.BrakeTorqueNm = 3000.0;
	const FOsdsLongitudinalSubstepOutput Locked = OsdsDynamics::SolveLongitudinalSubstep(Car, In);
	TestTrue(TEXT("locked: the force is the sliding force, braking"),
		Locked.LongitudinalForceN < 0.0 && FMath::Abs(FMath::Abs(Locked.LongitudinalForceN) - X.SlideForce) < 0.02 * X.SlideForce);

	// Aligning torque: a contact patch about 13 cm long, a pneumatic trail of about a fifth of it
	// at small slip, the torque (trail times side force) peaking well before the side force does
	// and falling as the front slides: what a driver feels lighten before the front lets go.
	const double Patch = OsdsDynamics::ContactPatchLengthM(Car, Fz);
	const double Trail0 = OsdsDynamics::TmeasyPneumaticTrailM(Car, Fz, 0.0);
	double PeakMzAlpha = 0.0, PeakMz = 0.0;
	for (double A = 0.25; A <= 20.0; A += 0.25)
	{
		const double Mz = OsdsDynamics::TmeasyPneumaticTrailM(Car, Fz, FMath::Tan(FMath::DegreesToRadians(A))) * Lateral(A, 0.0);
		if (Mz > PeakMz) { PeakMz = Mz; PeakMzAlpha = A; }
	}
	AddInfo(FString::Printf(TEXT("TMeasy aligning: patch %.0f mm, trail %.1f mm at no slip, torque peak %.1f N m at %.2f deg (side force peaks at %.2f deg)"),
		Patch * 1000.0, Trail0 * 1000.0, PeakMz, PeakMzAlpha, PeakAlpha));
	TestTrue(TEXT("contact patch 10-16 cm at the nominal load"), Patch > 0.10 && Patch < 0.16);
	TestEqual(TEXT("trail at no slip is the lever times the patch"), Trail0, Car.TmeasyTrailFraction * Patch, 1.0e-9);
	TestTrue(TEXT("the aligning torque peaks at 0.3-0.6 of the side force's peak slip angle"),
		PeakMzAlpha >= 0.3 * PeakAlpha && PeakMzAlpha <= 0.6 * PeakAlpha);
	TestTrue(TEXT("past the side-force peak the aligning torque is less than half its peak"),
		OsdsDynamics::TmeasyPneumaticTrailM(Car, Fz, FMath::Tan(FMath::DegreesToRadians(PeakAlpha))) * Lateral(PeakAlpha, 0.0) < 0.5 * PeakMz);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelTuningTest, "OSDS.Vehicle.Model.Tuning", VehicleTestFlags)
bool FOsdsVehicleModelTuningTest::RunTest(const FString& Parameters)
{
	FOsdsCarDefinition Car;
	if (!OsdsLoadBenchCar(*this, TEXT("nb2001"), Car))
	{
		return false;
	}
	const FOsdsTuning Stock = FOsdsTuning::FromCar(Car, 15.0);
	TestEqual(TEXT("stock front spring is the car file's coil"), Stock.FrontSpringLbIn, Car.CoilSpringRateLbfPerInByAxle[0]);
	TestEqual(TEXT("stock final drive"), Stock.FinalDrive, Car.FinalDriveRatio);
	// The stock setup rebuilds the real car exactly.
	const FOsdsCarDefinition Same = Stock.Apply(Car, Stock);
	TestEqual(TEXT("stock: same spring rate"), OsdsDynamics::GetWheelSpringRateNPerM(Same, 0), OsdsDynamics::GetWheelSpringRateNPerM(Car, 0));
	TestEqual(TEXT("stock: same bar rate"), OsdsDynamics::GetAntiRollBarRateNPerM(Same, 1), OsdsDynamics::GetAntiRollBarRateNPerM(Car, 1));
	TestEqual(TEXT("stock: same ride height"), Same.SuspensionMountHeightM, Car.SuspensionMountHeightM);
	const double RealRoll = FOsdsTuning::RollGradientDegPerG(Car);
	AddInfo(FString::Printf(TEXT("NB body roll %.2f deg/g"), RealRoll));
	TestTrue(TEXT("a roadster rolls 2-8 deg per g"), RealRoll > 2.0 && RealRoll < 8.0);

	// Stiffer springs and bars roll less; lower sits lower; more torque pulls harder.
	FOsdsTuning Stiff = Stock;
	Stiff.FrontSpringLbIn *= 2.0;
	Stiff.RearSpringLbIn *= 2.0;
	Stiff.FrontBarPct = 200.0;
	Stiff.FrontRideHeightMm = Stiff.RearRideHeightMm = -30.0;
	Stiff.PeakTorqueLbFt *= 1.2;
	Stiff.PeakPowerHp *= 1.2;
	Stiff.Clamp(Stock);
	const FOsdsCarDefinition Tuned = Stiff.Apply(Car, Stock);
	TestTrue(TEXT("stiffer springs and bar roll less"), FOsdsTuning::RollGradientDegPerG(Tuned) < RealRoll);
	FOsdsTuning Lower = Stock;
	Lower.FrontRideHeightMm = Lower.RearRideHeightMm = -30.0;
	TestTrue(TEXT("a lower body (lower centre of mass) rolls less"), FOsdsTuning::RollGradientDegPerG(Lower.Apply(Car, Stock)) < RealRoll);
	// The body stands where the suspension holds its static length: mount over the road at
	// R + static length, the body centre (mount height - box centre) under it.
	auto BodyOverRoad = [](const FOsdsCarDefinition& C)
	{
		return C.TireFreeRadiusM() + C.StaticSuspensionLengthM - (C.SuspensionMountHeightM - C.CollisionShapeCenterYM);
	};
	TestEqual(TEXT("-30 mm: the body sits 30 mm lower over the road"), BodyOverRoad(Tuned), BodyOverRoad(Car) - 0.03, 1.0e-9);
	TestEqual(TEXT("the centre of mass keeps its place in the body"), Tuned.CenterOfGravityHeightM - Tuned.CollisionShapeCenterYM,
		Car.CenterOfGravityHeightM - Car.CollisionShapeCenterYM, 1.0e-9);
	TestEqual(TEXT("torque scaled"), OsdsDynamics::SampleEngineTorqueNm(Tuned, 5500.0), 1.2 * OsdsDynamics::SampleEngineTorqueNm(Car, 5500.0), 1.0e-6);
	// Safe limits.
	FOsdsTuning Wild = Stock;
	Wild.FrontRideHeightMm = Wild.RearRideHeightMm = -500.0;
	Wild.FinalDrive = 99.0;
	Wild.Clamp(Stock);
	TestEqual(TEXT("ride height clamped to -60 mm"), Wild.FrontRideHeightMm, -60.0);
	TestEqual(TEXT("final drive clamped to +25 %"), Wild.FinalDrive, Stock.FinalDrive * 1.25, 1.0e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelManualClutchTest, "OSDS.Vehicle.Model.ManualClutchLocks", VehicleTestFlags)
bool FOsdsVehicleModelManualClutchTest::RunTest(const FString& Parameters)
{
	FOsdsCarDefinition Car;
	if (!OsdsLoadBenchCar(*this, TEXT("nb2001"), Car))
	{
		return false;
	}
	// The car rolls at 20 m/s on a flat road in 3rd, clutch up, half load (the body held at
	// that speed): a dry clutch with the pedal up is locked, so the engine turns exactly with
	// the gearbox. The old viscous coupling let it slip by torque over 10 N m s. Half load, not
	// half pedal: the throttle plate's progression is not what this checks.
	Car.ThrottleProgression = 0.0;
	FOsdsVehicleSim Sim;
	Sim.Initialize(Car);
	Sim.SetManual(true, 20.0);
	Sim.SelectRatio(3);
	const double Ratio = Car.ForwardGearRatios[2] * Car.FinalDriveRatio;
	const double WheelOmega = 20.0 / Sim.GetTireRadiusM();
	for (FOsdsWheelState& W : Sim.State.Wheels) { W.AngularSpeedRadPerSecond = WheelOmega; }
	Sim.State.EngineOmegaRadPerSecond = WheelOmega * Ratio;
	FOsdsBodyState Body;
	Body.PositionM = FVector3d(0.0, 0.0, Car.CollisionShapeCenterYM);
	Body.CenterOfMassM = Body.PositionM + Sim.GetCenterOfMassLocalM();
	Body.LinearVelocityMps = FVector3d(20.0, 0.0, 0.0);
	auto Ground = [](const FVector3d& Start, const FVector3d& End, FOsdsGroundHit& Hit)
	{
		if (Start.Z <= 0.0 || End.Z > 0.0) return false;
		const double T = Start.Z / (Start.Z - End.Z);
		Hit.PointM = Start + (End - Start) * T;
		Hit.NormalM = FVector3d::UnitZ();
		return true;
	};
	FOsdsVehicleControls Controls;
	Controls.Throttle = 0.5;
	Controls.Clutch = 0.0;
	Controls.bDirectRack = true;
	FOsdsBodyForces Forces;
	for (int32 Step = 0; Step < 720; ++Step)
	{
		Sim.Step(1.0 / 360.0, 1, Controls, Body, Ground, Forces);
	}
	double Mean = 0.0;
	for (const FOsdsWheelState& W : Sim.State.Wheels) { Mean += 0.25 * W.AngularSpeedRadPerSecond; }
	const double Rear = 0.5 * (Sim.State.Wheels[2].AngularSpeedRadPerSecond + Sim.State.Wheels[3].AngularSpeedRadPerSecond);
	const double Slip = (Sim.State.EngineOmegaRadPerSecond - Rear * Ratio) / FMath::Max(Sim.State.EngineOmegaRadPerSecond, 1.0);
	AddInfo(FString::Printf(TEXT("Manual 3rd, clutch up, half throttle at 20 m/s: engine %.0f rpm, gearbox %.0f rpm, clutch slip %.3f %%"),
		Sim.State.EngineOmegaRadPerSecond * 60.0 / (2.0 * UE_DOUBLE_PI), Rear * Ratio * 60.0 / (2.0 * UE_DOUBLE_PI), 100.0 * Slip));
	// Locked within one substep's engine torque (the load side is not predicted): about 0.6 %,
	// against about 2 % with the viscous coupling alone.
	TestTrue(TEXT("the engaged clutch slips less than 1 %"), FMath::Abs(Slip) < 0.01);
	(void)Mean;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelClutchChatterTest, "OSDS.Vehicle.Model.ClutchNoChatter", VehicleTestFlags)
bool FOsdsVehicleModelClutchChatterTest::RunTest(const FString& Parameters)
{
	// Wheels in the air, 1st gear and reverse, clutch up, a little throttle: the locked clutch
	// must carry a steady torque, not one that flips sign every substep (launch chatter: the
	// coupling stiffer than a substep can carry).
	for (const TCHAR* Id : { TEXT("nb2001"), TEXT("mustang65") })
	{
		FOsdsCarDefinition Car;
		if (!OsdsLoadBenchCar(*this, Id, Car))
		{
			return false;
		}
		for (const int32 Gear : { 1, -1 })
		{
			FOsdsVehicleSim Sim;
			Sim.Initialize(Car);
			Sim.SetManual(true, 0.0);
			Sim.SelectRatio(Gear);
			FOsdsBodyState Body;
			Body.PositionM = FVector3d(0.0, 0.0, 5.0);
			Body.CenterOfMassM = Body.PositionM + Sim.GetCenterOfMassLocalM();
			Body.PositionM = FVector3d(0.0, 0.0, Car.CollisionShapeCenterYM);
			Body.CenterOfMassM = Body.PositionM + Sim.GetCenterOfMassLocalM();
			// On a flat road, the pedal let out over 0.6 s from 0.2 s: the launch itself.
			auto Air = [](const FVector3d& Start, const FVector3d& End, FOsdsGroundHit& Hit)
			{
				if (Start.Z <= 0.0 || End.Z > 0.0) return false;
				const double T = Start.Z / (Start.Z - End.Z);
				Hit.PointM = Start + (End - Start) * T;
				Hit.NormalM = FVector3d::UnitZ();
				return true;
			};
			FOsdsVehicleControls Controls;
			Controls.Throttle = 0.25;
			Controls.Clutch = 0.0;
			Controls.bDirectRack = true;
			FOsdsBodyForces Forces;
			int32 Flips = 0, Samples = 0;
			double Last = 0.0;
			for (int32 Step = 0; Step < 720; ++Step)
			{
				Controls.Clutch = static_cast<float>(FMath::Clamp(1.0 - (Step / 360.0 - 0.2) / 0.6, 0.0, 1.0));
				Sim.Step(1.0 / 360.0, 1, Controls, Body, Air, Forces);
				const double Load = Sim.State.ClutchTorqueNm;
				if (Step > 60)
				{
					++Samples;
					if (Load * Last < 0.0 && FMath::Abs(Load - Last) > 5.0) ++Flips;
				}
				Last = Load;
			}
			AddInfo(FString::Printf(TEXT("CHATTER %s gear %d: clutch torque flips sign %d times in %d substeps (last %.1f N m, engine %.0f rpm)"),
				Id, Gear, Flips, Samples, Last, Sim.State.EngineRpm));
			TestTrue(FString::Printf(TEXT("%s gear %d: the locked clutch does not chatter"), Id, Gear), Flips <= 2);
		}
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelAltitudeTest, "OSDS.Vehicle.Model.AltitudePower", VehicleTestFlags)
bool FOsdsVehicleModelAltitudeTest::RunTest(const FString& Parameters)
{
	// A naturally aspirated engine revs up slower in thin air: in neutral at full pedal, the rise
	// in 0.15 s at Denver's air (ISA at 1609 m and 15 C: about 1.04 kg/m3) against 1.225.
	FOsdsCarDefinition Car;
	if (!OsdsLoadBenchCar(*this, TEXT("nb2001"), Car))
	{
		return false;
	}
	auto Rise = [&Car](double Density, bool bTurbo)
	{
		FOsdsCarDefinition C = Car;
		C.bTurbocharged = bTurbo;
		FOsdsVehicleSim Sim;
		Sim.Initialize(C);
		Sim.SetManual(true, 0.0);
		Sim.SelectRatio(0);
		Sim.AirDensityKgPerM3 = Density;
		FOsdsBodyState Body;
		Body.PositionM = FVector3d(0.0, 0.0, 5.0);
		Body.CenterOfMassM = Body.PositionM + Sim.GetCenterOfMassLocalM();
		auto Air = [](const FVector3d&, const FVector3d&, FOsdsGroundHit&) { return false; };
		FOsdsVehicleControls Controls;
		Controls.bDirectRack = true;
		FOsdsBodyForces Forces;
		for (int32 Step = 0; Step < 180; ++Step) Sim.Step(1.0 / 360.0, 1, Controls, Body, Air, Forces);
		const double From = Sim.State.EngineRpm;
		Controls.Throttle = 1.0;
		for (int32 Step = 0; Step < 54; ++Step) Sim.Step(1.0 / 360.0, 1, Controls, Body, Air, Forces);
		return Sim.State.EngineRpm - From;
	};
	const double Sea = Rise(1.225, false), Denver = Rise(1.04, false), Turbo = Rise(1.04, true);
	AddInfo(FString::Printf(TEXT("ALTITUDE nb2001 rev rise in 0.15 s: %.0f rpm at sea level, %.0f at Denver, %.0f with a turbo's held boost"), Sea, Denver, Turbo));
	TestTrue(TEXT("thin air: a slower rise, about the density ratio (0.85) or less"), Denver < 0.93 * Sea && Denver > 0.6 * Sea);
	TestNearlyEqual(TEXT("a turbo holds its boost"), Turbo, Sea, 0.02 * Sea);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelDrivetrainTest, "OSDS.Vehicle.Model.Drivetrain", VehicleTestFlags)
bool FOsdsVehicleModelDrivetrainTest::RunTest(const FString& Parameters)
{
	FOsdsCarDefinition Car;
	TestEqual(TEXT("torque curve point"), OsdsDynamics::SampleEngineTorqueNm(Car, 5000.0), 161.0, 1.0e-9);
	TestEqual(TEXT("torque curve interpolates"), OsdsDynamics::SampleEngineTorqueNm(Car, 4575.0), 156.0, 1.0e-9);
	TestEqual(TEXT("below the curve holds the first point"), OsdsDynamics::SampleEngineTorqueNm(Car, 300.0), 94.0, 1.0e-9);
	TestEqual(TEXT("past the curve"), OsdsDynamics::SampleEngineTorqueNm(Car, 7500.0), Car.TorquePastCurveNm, 1.0e-9);

	// The throttle plate: full pedal is full load at any speed, none is none; a light pedal fills
	// the cylinders far more at low speed than at high.
	FOsdsCarDefinition Plate;
	Plate.ThrottleProgression = 3.0;
	TestEqual(TEXT("full pedal is full load"), OsdsDynamics::EngineLoad(Plate, 1.0, 2000.0), 1.0, 1.0e-9);
	TestEqual(TEXT("no pedal is no load"), OsdsDynamics::EngineLoad(Plate, 0.0, 2000.0), 0.0, 1.0e-9);
	const double LightLow = OsdsDynamics::EngineLoad(Plate, 0.2, 1500.0);
	const double LightHigh = OsdsDynamics::EngineLoad(Plate, 0.2, 6000.0);
	AddInfo(FString::Printf(TEXT("THROTTLE pedal 0.2: load %.2f at 1500 rpm, %.2f at 6000"), LightLow, LightHigh));
	TestTrue(TEXT("a light pedal at low revs is most of the torque"), LightLow > 0.6 && LightLow < 0.8);
	TestTrue(TEXT("and much less at high revs"), LightHigh > 0.25 && LightHigh < 0.45);
	FOsdsCarDefinition Linear;
	Linear.ThrottleProgression = 0.0;
	TestEqual(TEXT("progression 0 is linear"), OsdsDynamics::EngineLoad(Linear, 0.3, 1500.0), 0.3, 1.0e-9);

	// Torsen-style bias 2: the slower wheel can take up to twice the faster's torque.
	const double Equal[4] = { 0.0, 0.0, 30.0, 30.0 };
	double Out[4];
	OsdsDynamics::SplitDriveTorqueToWheels(Car, 1000.0, Equal, 1.0 / 360.0, true, Out);
	TestEqual(TEXT("equal speeds split equally"), Out[2], 500.0, 1.0e-9);
	const double Spinning[4] = { 0.0, 0.0, 30.0, 60.0 };
	OsdsDynamics::SplitDriveTorqueToWheels(Car, 1000.0, Spinning, 1.0 / 360.0, true, Out);
	TestEqual(TEXT("a spinning right wheel sends torque left, up to the bias"), Out[2] / Out[3], 2.0, 1.0e-9);
	TestEqual(TEXT("the split conserves torque"), Out[2] + Out[3], 1000.0, 1.0e-9);
	Car.RearDifferentialBiasRatio = 1.0;
	OsdsDynamics::SplitDriveTorqueToWheels(Car, 1000.0, Spinning, 1.0 / 360.0, true, Out);
	TestEqual(TEXT("an open differential splits equally"), Out[2], Out[3], 1.0e-9);

	// Engine braking: open on the coast side by default; a torque-sensing differential with a
	// coast bias moves the braking torque toward the faster (unloaded, spinning-up) wheel's partner.
	FOsdsCarDefinition Coast;
	OsdsDynamics::SplitDriveTorqueToWheels(Coast, -200.0, Spinning, 1.0 / 360.0, false, Out);
	TestEqual(TEXT("coast: open by default"), Out[2], Out[3], 1.0e-9);
	Coast.RearDifferentialCoastBiasRatio = 1.5;
	OsdsDynamics::SplitDriveTorqueToWheels(Coast, -200.0, Spinning, 1.0 / 360.0, false, Out);
	TestTrue(TEXT("coast: biased"), FMath::Abs(Out[2] - Out[3]) > 1.0);
	TestEqual(TEXT("coast: at most the bias"), FMath::Max(FMath::Abs(Out[2]), FMath::Abs(Out[3])) / FMath::Min(FMath::Abs(Out[2]), FMath::Abs(Out[3])), 1.5, 1.0e-9);
	TestEqual(TEXT("coast: the split conserves torque"), Out[2] + Out[3], -200.0, 1.0e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelReverseInhibitTest, "OSDS.Vehicle.Model.ReverseInhibit", VehicleTestFlags)
bool FOsdsVehicleModelReverseInhibitTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* CarId : BenchCars)
	{
		FOsdsCarDefinition Car;
		if (!OsdsLoadBenchCar(*this, CarId, Car))
		{
			continue;
		}
		FOsdsVehicleSim Sim;
		Sim.Initialize(Car);
		Sim.bSteeringAssist = false;
		Sim.ResetState();
		const FFlatRoad Road;
		FOsdsBodyForces Forces;
		// Rolling forward at 5 m/s, every brake pedal from the lightest touch up:
		// the automatic must never select reverse.
		for (const double Pedal : { 0.001, 0.005, 0.0099, 0.01, 0.02, 0.3, 1.0 })
		{
			Sim.ResetState();
			FOsdsVehicleControls Controls;
			Controls.Brake = Pedal;
			Sim.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Sim, FVector3d(5.0, 0.0, 0.0)), Road, Forces);
			TestFalse(FString::Printf(TEXT("%s: brake %.4f at 5 m/s keeps drive"), CarId, Pedal), Sim.State.bInReverse);
		}
		// At 0.44 m/s a pedal selects reverse, as a real selector would near standstill.
		FOsdsVehicleControls Controls;
		Controls.Brake = 0.5;
		Sim.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Sim, FVector3d(0.44, 0.0, 0.0)), Road, Forces);
		TestTrue(FString::Printf(TEXT("%s: brake at 0.44 m/s selects reverse"), CarId), Sim.State.bInReverse);
		// Rolling backwards, the accelerator brakes until 0.45 m/s.
		Controls = FOsdsVehicleControls();
		Controls.Throttle = 0.5;
		Sim.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Sim, FVector3d(-3.0, 0.0, 0.0)), Road, Forces);
		TestTrue(FString::Printf(TEXT("%s: accelerator rolling backwards brakes"), CarId), Sim.State.bServiceBrake && Sim.State.bInReverse);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelGearboxTest, "OSDS.Vehicle.Model.Gearbox", VehicleTestFlags)
bool FOsdsVehicleModelGearboxTest::RunTest(const FString& Parameters)
{
	FOsdsVehicleSim Sim;
	Sim.Initialize(FOsdsCarDefinition());
	Sim.SetManual(true, 0.0);
	TestEqual(TEXT("manual from rest selects neutral"), Sim.GetGearText(), FString(TEXT("N")));
	TestEqual(TEXT("no change without the clutch"), Sim.ChangeGear(1, 0.0), FString(TEXT("Hold the clutch to change gear")));
	Sim.State.ClutchPedal = 1.0;
	TestEqual(TEXT("first with the clutch"), Sim.ChangeGear(1, 0.0), FString());
	TestEqual(TEXT("in first"), Sim.GetGearText(), FString(TEXT("1")));
	Sim.ChangeGear(-1, 0.0);
	TestEqual(TEXT("reverse refused while rolling forward"), Sim.ChangeGear(-1, 3.0), FString(TEXT("Stop before changing direction")));
	TestEqual(TEXT("reverse at rest"), Sim.ChangeGear(-1, 0.0), FString());
	TestEqual(TEXT("in reverse"), Sim.GetGearText(), FString(TEXT("R")));
	Sim.ChangeGear(1, 0.0);
	Sim.ChangeGear(1, 0.0);
	TestEqual(TEXT("over-revving downshift refused"), Sim.ChangeGear(0, 0.0), FString());
	Sim.SelectRatio(5);
	TestTrue(TEXT("first refused at 40 m/s, with the speed it takes"), Sim.ChangeGear(-4, 40.0).StartsWith(TEXT("Too fast for 1st: below ")));
	Sim.SetManual(false, 20.0);
	TestFalse(TEXT("automatic again"), Sim.State.bManual);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelManualShiftBufferTest, "OSDS.Vehicle.Model.ManualShiftBuffer", VehicleTestFlags)
bool FOsdsVehicleModelManualShiftBufferTest::RunTest(const FString& Parameters)
{
	FOsdsCarDefinition Car;
	if (!OsdsLoadBenchCar(*this, TEXT("nb2001_lowered"), Car))
	{
		return false;
	}

	const FFlatRoad Road;
	FOsdsBodyForces Forces;
	FOsdsVehicleSim Clutched;
	Clutched.Initialize(Car);
	Clutched.SetManual(true, 0.0);
	Clutched.QueueManualGearChange(1);
	FOsdsVehicleControls Controls;
	Controls.Clutch = 1.0;
	for (int32 Step = 0; Step < 30 && Clutched.GetGearText() == TEXT("N"); ++Step)
	{
		Clutched.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Clutched, FVector3d::ZeroVector), Road, Forces);
	}
	TestEqual(TEXT("same-frame clutch and shift reaches first after the clutch ramps"), Clutched.GetGearText(), FString(TEXT("1")));
	TestTrue(TEXT("successful buffered shift has no refusal"), Clutched.ConsumeGearChangeMessage().IsEmpty());

	FOsdsVehicleSim Unclutched;
	Unclutched.Initialize(Car);
	Unclutched.SetManual(true, 0.0);
	Unclutched.QueueManualGearChange(1);
	Controls = FOsdsVehicleControls();
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Unclutched.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Unclutched, FVector3d::ZeroVector), Road, Forces);
	}
	TestEqual(TEXT("shift without clutch stays in neutral"), Unclutched.GetGearText(), FString(TEXT("N")));
	TestEqual(TEXT("expired shift reports the clutch refusal"), Unclutched.ConsumeGearChangeMessage(), FString(TEXT("Hold the clutch to change gear")));
	return true;
}

namespace
{
	/** The NB in manual with the automatic clutch, in a gear, rolling at a steady speed. */
	bool OsdsAutoClutchCar(FAutomationTestBase& Test, FOsdsVehicleSim& Sim, int32 Gear, double SpeedMps, double Gas)
	{
		FOsdsCarDefinition Car;
		if (!OsdsLoadBenchCar(Test, TEXT("nb2001_lowered"), Car))
		{
			return false;
		}
		Sim.Initialize(Car);
		Sim.bAutoClutch = true;
		Sim.SetManual(true, SpeedMps);
		Sim.SelectRatio(Gear);
		const FFlatRoad Road;
		FOsdsBodyForces Forces;
		FOsdsVehicleControls Controls;
		Controls.Throttle = Gas;
		for (int32 Step = 0; Step < 360; ++Step)
		{
			Sim.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Sim, FVector3d(SpeedMps, 0.0, 0.0)), Road, Forces);
		}
		return true;
	}

	void OsdsRollFor(FOsdsVehicleSim& Sim, double Seconds, double SpeedMps, double Gas, TFunctionRef<void(double)> Each)
	{
		const FFlatRoad Road;
		FOsdsBodyForces Forces;
		FOsdsVehicleControls Controls;
		Controls.Throttle = Gas;
		for (int32 Step = 0; Step < FMath::RoundToInt32(Seconds * 360.0); ++Step)
		{
			Sim.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Sim, FVector3d(SpeedMps, 0.0, 0.0)), Road, Forces);
			Each((Step + 1) / 360.0);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelAutoClutchUpshiftTest, "OSDS.Vehicle.Model.AutoClutchUpshift", VehicleTestFlags)
bool FOsdsVehicleModelAutoClutchUpshiftTest::RunTest(const FString& Parameters)
{
	FOsdsVehicleSim Sim;
	if (!OsdsAutoClutchCar(*this, Sim, 2, 12.0, 0.4))
	{
		return false;
	}
	Sim.QueueManualGearChange(1);
	double InThirdAtS = -1.0;
	OsdsRollFor(Sim, 0.8, 12.0, 0.4, [&](double T) { if (InThirdAtS < 0.0 && Sim.GetGearText() == TEXT("3")) InThirdAtS = T; });
	TestTrue(FString::Printf(TEXT("RB alone changes 2nd to 3rd within 0.15 s (%.3f s)"), InThirdAtS), InThirdAtS > 0.0 && InThirdAtS <= 0.15);
	TestTrue(TEXT("no refusal"), Sim.ConsumeGearChangeMessage().IsEmpty());
	TestTrue(FString::Printf(TEXT("the clutch is back out after the shift (%.2f)"), Sim.State.ClutchPedal), Sim.State.ClutchPedal < 0.05);
	TestFalse(TEXT("the shift is over"), Sim.IsAutoShifting());
	TestFalse(TEXT("no stall"), Sim.State.bStalled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelAutoClutchDownshiftTest, "OSDS.Vehicle.Model.AutoClutchDownshift", VehicleTestFlags)
bool FOsdsVehicleModelAutoClutchDownshiftTest::RunTest(const FString& Parameters)
{
	// 4th at 20 m/s, two quick presses of LB: two gears down, and the blip brings the engine up to
	// the road's speed in the new gear before the clutch bites.
	FOsdsVehicleSim Sim;
	if (!OsdsAutoClutchCar(*this, Sim, 4, 20.0, 0.0))
	{
		return false;
	}
	Sim.QueueManualGearChange(-1);
	Sim.QueueManualGearChange(-1);
	double WorstGapRpm = 0.0;
	OsdsRollFor(Sim, 1.0, 20.0, 0.0, [&](double)
	{
		// While the clutch comes out (the pedal passing the bite), the engine is near the road's speed in gear.
		if (Sim.GetGearText() == TEXT("2") && Sim.State.ClutchPedal < 0.5 && Sim.State.ClutchPedal > 0.15)
		{
			const FOsdsCarDefinition& Car = Sim.GetCar();
			const double RoadRpm = FMath::Abs(Sim.DrivenOmega()) * Car.ForwardGearRatios[1] * Car.FinalDriveRatio * 60.0 / (2.0 * UE_DOUBLE_PI);
			WorstGapRpm = FMath::Max(WorstGapRpm, FMath::Abs(RoadRpm - Sim.State.EngineRpm));
		}
	});
	TestEqual(TEXT("two presses of LB drop two gears"), Sim.GetGearText(), FString(TEXT("2")));
	TestTrue(TEXT("no refusal"), Sim.ConsumeGearChangeMessage().IsEmpty());
	TestTrue(FString::Printf(TEXT("the blip matches the engine within 900 rpm as the clutch bites (%.0f rpm)"), WorstGapRpm), WorstGapRpm < 900.0);
	TestFalse(TEXT("no stall"), Sim.State.bStalled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelAutoClutchNoStallTest, "OSDS.Vehicle.Model.AutoClutchNoStall", VehicleTestFlags)
bool FOsdsVehicleModelAutoClutchNoStallTest::RunTest(const FString& Parameters)
{
	// Third gear at walking pace with no throttle: a driver's foot would press the clutch; so does the automatic one.
	FOsdsVehicleSim Sim;
	if (!OsdsAutoClutchCar(*this, Sim, 3, 0.5, 0.0))
	{
		return false;
	}
	OsdsRollFor(Sim, 2.0, 0.5, 0.0, [](double) {});
	TestFalse(TEXT("3rd at 0.5 m/s does not stall"), Sim.State.bStalled);
	TestTrue(FString::Printf(TEXT("the clutch is held in (%.2f)"), Sim.State.ClutchPedal), Sim.State.ClutchPedal > 0.6);

	// Pulling away from a stop in 1st on the throttle alone.
	FOsdsVehicleSim Away;
	if (!OsdsAutoClutchCar(*this, Away, 1, 0.0, 0.0))
	{
		return false;
	}
	TestFalse(TEXT("1st at a standstill does not stall"), Away.State.bStalled);
	// The car rolls under its own drive force (a point mass on the flat).
	double MinClutch = 1.0, SpeedMps = 0.0;
	{
		const FFlatRoad Road;
		FOsdsBodyForces Forces;
		FOsdsVehicleControls Controls;
		Controls.Throttle = 0.5;
		for (int32 Step = 0; Step < 4 * 360; ++Step)
		{
			Away.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Away, FVector3d(SpeedMps, 0.0, 0.0)), Road, Forces);
			SpeedMps = FMath::Max(0.0, SpeedMps + Forces.ForceN.X / Away.GetCar().MassKg / 360.0);
			MinClutch = FMath::Min(MinClutch, Away.State.ClutchPedal);
		}
	}
	TestTrue(FString::Printf(TEXT("half throttle pulls away in 1st (%.1f m/s after 4 s)"), SpeedMps), SpeedMps > 3.0);
	TestTrue(FString::Printf(TEXT("the clutch comes fully out once moving (%.2f)"), MinClutch), MinClutch < 0.05);
	TestFalse(TEXT("pulling away does not stall"), Away.State.bStalled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelAutoClutchRefusalTest, "OSDS.Vehicle.Model.AutoClutchRefusal", VehicleTestFlags)
bool FOsdsVehicleModelAutoClutchRefusalTest::RunTest(const FString& Parameters)
{
	// 3rd at 30 m/s: 2nd would over-rev, so LB is refused and the clutch goes back out.
	FOsdsVehicleSim Sim;
	if (!OsdsAutoClutchCar(*this, Sim, 3, 30.0, 0.0))
	{
		return false;
	}
	Sim.QueueManualGearChange(-1);
	FString Message;
	OsdsRollFor(Sim, 0.8, 30.0, 0.0, [&](double) { const FString M = Sim.ConsumeGearChangeMessage(); if (!M.IsEmpty()) Message = M; });
	TestTrue(FString::Printf(TEXT("over-rev refused with the speed 2nd takes (%s)"), *Message), Message.StartsWith(TEXT("Too fast for 2nd: below ")));
	TestEqual(TEXT("the gear stays"), Sim.GetGearText(), FString(TEXT("3")));
	TestTrue(FString::Printf(TEXT("the clutch is back out (%.2f)"), Sim.State.ClutchPedal), Sim.State.ClutchPedal < 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelAckermannContinuousTest, "OSDS.Vehicle.Model.AckermannContinuous", VehicleTestFlags)
bool FOsdsVehicleModelAckermannContinuousTest::RunTest(const FString& Parameters)
{
	// The front wheels follow the rack through straight ahead with no jump: the old Ackermann floor
	// put both wheels 0.028 deg off straight for any rack off zero, so the car hunted about the tiny
	// angle a road's crown asks for.
	FOsdsCarDefinition Car;
	if (!OsdsLoadBenchCar(*this, TEXT("nb2001_lowered"), Car))
	{
		return false;
	}
	Car.SteeringStiffnessNmPerDeg = 0.0;
	const FFlatRoad Road;
	for (const double RackRad : { 0.0, 1.0e-6, -1.0e-6, 1.0e-4, -1.0e-4, 1.0e-3, -1.0e-3, 0.05 })
	{
		FOsdsVehicleSim Sim;
		Sim.Initialize(Car);
		FOsdsVehicleControls Controls;
		Controls.bDirectRack = true;
		Controls.RackAngleRad = RackRad;
		FOsdsBodyForces Forces;
		Sim.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Sim, FVector3d::ZeroVector), Road, Forces);
		const double Mean = 0.5 * (Sim.State.Wheels[0].SteerAngleRad + Sim.State.Wheels[1].SteerAngleRad);
		const double Allowed = FMath::Max(1.0e-9, 0.02 * FMath::Abs(RackRad));
		TestTrue(FString::Printf(TEXT("rack %.6f rad: the front wheels' mean is %.6f rad"), RackRad, Mean), FMath::Abs(Mean - RackRad) <= Allowed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleModelShiftMapTest, "OSDS.Vehicle.Model.ShiftMap", VehicleTestFlags)
bool FOsdsVehicleModelShiftMapTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* CarId : BenchCars)
	{
		FOsdsCarDefinition Car;
		if (!OsdsLoadBenchCar(*this, CarId, Car) || Car.ForwardGearRatios.Num() < 2)
		{
			continue;
		}
		const TArray<double>& Ratios = Car.ForwardGearRatios;
		const double TyreRadiusM = Car.TireFreeRadiusM();
		auto WheelOmega = [&](int32 Gear, double SpeedMps) { return SpeedMps * Ratios[Gear] * Car.FinalDriveRatio / TyreRadiusM; };
		constexpr double RpmPerRadPerSecond = 60.0 / UE_DOUBLE_TWO_PI;

		// The floor keeps the calibrated full-throttle point (0-60 is unchanged).
		TestEqual(FString::Printf(TEXT("%s: floored upshift at upshift_rpm"), CarId),
			OsdsDynamics::GetAutomaticShiftPoints(Car, 1.0).UpshiftRpm, Car.AutomaticUpshiftSpeedRpm, 1.0e-6);

		// Accelerate gently through the gears at each pedal: record where first changes up.
		double Previous = 0.0;
		for (const double Pedal : { 0.15, 0.3, 0.45, 0.7, 1.0 })
		{
			int32 Gear = 0;
			double FirstUpshiftRpm = 0.0;
			for (double Speed = 0.5; Speed < 70.0 && FirstUpshiftRpm == 0.0; Speed += 0.02)
			{
				const double Omega = WheelOmega(Gear, Speed);
				const int32 Next = OsdsDynamics::SelectAutomaticGear(Car, Gear, Omega * RpmPerRadPerSecond, Omega, Pedal, false);
				if (Next > Gear)
				{
					FirstUpshiftRpm = Omega * RpmPerRadPerSecond;
					// Straight after the change the lower engine speed must not ask for a downshift.
					const double After = WheelOmega(Next, Speed);
					TestEqual(FString::Printf(TEXT("%s: no hunting after the 1-2 change at pedal %.2f"), CarId, Pedal),
						OsdsDynamics::SelectAutomaticGear(Car, Next, After * RpmPerRadPerSecond, After, Pedal, false), Next);
				}
			}
			const FString Line = FString::Printf(TEXT("SHIFTMAP %-11s pedal %.2f: 1-2 at %.0f rpm, downshift below %.0f rpm"),
				CarId, Pedal, FirstUpshiftRpm, OsdsDynamics::GetAutomaticShiftPoints(Car, Pedal).DownshiftRpm);
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			AddInfo(Line);
			TestTrue(FString::Printf(TEXT("%s: changes up at pedal %.2f"), CarId, Pedal), FirstUpshiftRpm > 0.0);
			TestTrue(FString::Printf(TEXT("%s: a deeper pedal holds first longer (%.2f)"), CarId, Pedal), FirstUpshiftRpm > Previous);
			Previous = FirstUpshiftRpm;
			if (Pedal <= 0.45)
			{
				// A real automatic at half pedal changes up near the torque peak, not at the redline.
				TestTrue(FString::Printf(TEXT("%s: pedal %.2f changes up below 62 %% of upshift_rpm (%.0f rpm)"), CarId, Pedal, FirstUpshiftRpm),
					FirstUpshiftRpm < Car.AutomaticUpshiftSpeedRpm * 0.62);
			}
		}

		// Cruising in top at a light pedal holds the gear; flooring it kicks down.
		const int32 Top = Ratios.Num() - 1;
		const double CruiseSpeed = 2600.0 / RpmPerRadPerSecond / (Ratios[Top - 1] * Car.FinalDriveRatio / TyreRadiusM);
		const double TopOmega = WheelOmega(Top, CruiseSpeed);
		TestEqual(FString::Printf(TEXT("%s: light pedal holds top"), CarId),
			OsdsDynamics::SelectAutomaticGear(Car, Top, TopOmega * RpmPerRadPerSecond, TopOmega, 0.2, false), Top);
		TestEqual(FString::Printf(TEXT("%s: the floor kicks down"), CarId),
			OsdsDynamics::SelectAutomaticGear(Car, Top, TopOmega * RpmPerRadPerSecond, TopOmega, 1.0, false), Top - 1);

		// Mid-corner the gear holds: the part-throttle upshift waits until the car straightens.
		const double CornerRpm = OsdsDynamics::GetAutomaticShiftPoints(Car, 0.3).UpshiftRpm * 1.15;
		const double CornerOmega = CornerRpm / RpmPerRadPerSecond;
		TestEqual(FString::Printf(TEXT("%s: part throttle changes up on the straight"), CarId),
			OsdsDynamics::SelectAutomaticGear(Car, 1, CornerRpm, CornerOmega, 0.3, false, 0.05), 2);
		TestEqual(FString::Printf(TEXT("%s: and holds the gear at 0.6 g"), CarId),
			OsdsDynamics::SelectAutomaticGear(Car, 1, CornerRpm, CornerOmega, 0.3, false, 0.6), 1);

		// Braking from high revs never changes up.
		const double HighOmega = Car.AutomaticUpshiftSpeedRpm * 0.8 / RpmPerRadPerSecond;
		TestEqual(FString::Printf(TEXT("%s: no upshift on the brakes"), CarId),
			OsdsDynamics::SelectAutomaticGear(Car, 1, HighOmega * RpmPerRadPerSecond, HighOmega, 0.0, true), 1);
		// Lifting at high revs changes up, as a real automatic does.
		TestEqual(FString::Printf(TEXT("%s: lifting changes up"), CarId),
			OsdsDynamics::SelectAutomaticGear(Car, 1, HighOmega * RpmPerRadPerSecond, HighOmega, 0.0, false), 2);
	}
	return true;
}


// Tyre pressure: grip follows MF-Tyre 6.1's pressure term around the placard pressure; tuning keeps it
// in 18-40 psi and the tuned car carries it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTyrePressureTest, "OSDS.Vehicle.Model.TyrePressure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOsdsTyrePressureTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("at the placard pressure, the measured grip"), OsdsDynamics::TyrePressureGripScale(26.0, 26.0), 1.0);
	TestTrue(TEXT("lower pressure grips a little more"), OsdsDynamics::TyrePressureGripScale(20.0, 26.0) > 1.03);
	TestTrue(TEXT("higher pressure grips a little less"), OsdsDynamics::TyrePressureGripScale(32.0, 26.0) < 0.99);
	TestTrue(TEXT("never far from the measured grip"), OsdsDynamics::TyrePressureGripScale(5.0, 26.0) <= 1.10 && OsdsDynamics::TyrePressureGripScale(80.0, 26.0) >= 0.85);
	FOsdsCarDefinition Car;
	Car.TyrePressurePsiByAxle = { 26.0, 26.0 };
	Car.TyrePressureRefPsiByAxle = { 26.0, 26.0 };
	const FOsdsTuning Stock = FOsdsTuning::FromCar(Car, 15.0);
	TestEqual(TEXT("stock is the placard"), Stock.FrontTyrePsi, 26.0);
	FOsdsTuning T = Stock;
	T.FrontTyrePsi = 50.0; T.RearTyrePsi = 22.0;
	T.Clamp(Stock);
	TestEqual(TEXT("held to 40 psi"), T.FrontTyrePsi, 40.0);
	const FOsdsCarDefinition Tuned = T.Apply(Car, Stock);
	TestEqual(TEXT("the tuned car carries the rear pressure"), Tuned.TyrePressurePsiByAxle[1], 22.0);
	TestEqual(TEXT("and keeps the placard as the reference"), Tuned.TyrePressureRefPsiByAxle[1], 26.0);
	return true;
}

#endif
