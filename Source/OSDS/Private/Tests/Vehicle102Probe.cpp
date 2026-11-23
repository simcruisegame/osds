// Copyright OSDS contributors. All Rights Reserved.
// THROWAWAY probe for bug #102 (Miata swaps ends at 60-100 km/h, ESC off). Not a regression Band.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/OsdsUnits.h"
#include "Vehicle/OsdsVehicle.h"
#include "Vehicle/OsdsVehicleMovementComponent.h"

#include <atomic>

namespace
{
	constexpr double Tau102 = 2.0 * UE_DOUBLE_PI;

	enum class E102Mode { Pulses, LaneChange, Robot, RobotOffsetStart };

	struct F102Result
	{
		bool bCompleted = false;
		double PeakBetaDeg = 0.0;
		double PeakRearSlipDeg = 0.0;
		double PeakYawDegS = 0.0;
		double PeakLatG = 0.0;
		double PeakAbsY = 0.0;
		double SpinAtS = -1.0;
		double EndSpeedKmh = 0.0;
		int32 EndGear = 0;
	};

	struct F102Driver
	{
		E102Mode Mode = E102Mode::Pulses;
		double Dt = 1.0 / 360.0;
		double TargetSpeed = 27.8;
		double RunS = 30.0;
		double ControlHz = 0.0;   // 0: every step; else zero-order hold
		double RangeRad = OsdsUnits::ToRadians(8.0);
		int32 State = 0;
		double T = 0.0;
		double Steer = 0.0, Throttle = 0.0, Brake = 0.0;
		double ControlClock = 0.0;
		double SmoothedYaw = 0.0, LastHeading = 0.0, Integral = 0.0, Command = 0.0;
		double RackRad = 0.0;
		std::atomic<bool> bDone{ false };
		F102Result R;

		void Step(FOsdsVehicleDriverContext& C)
		{
			C.Controls = FOsdsVehicleControls();
			if (bDone) return;
			T += Dt;
			C.Sim.bStabilityControlSwitch = false;
			const FVector3d Forward = C.Body.Rotation.RotateVector(FVector3d::UnitX());
			const FVector3d Right = C.Body.Rotation.RotateVector(FVector3d::UnitY());
			const double Speed = C.Body.LinearVelocityMps.Dot(Forward);
			C.Controls.bDirectRack = true;
			if (State == 0)
			{
				if (T >= 0.5)
				{
					C.Body.LinearVelocityMps = Forward * TargetSpeed;
					if (Mode == E102Mode::RobotOffsetStart) C.Body.PositionM.Y += 1.5;
					C.bBodyStateChanged = true;
					for (FOsdsWheelState& W : C.Sim.State.Wheels) W.AngularSpeedRadPerSecond = TargetSpeed / C.Sim.GetTireRadiusM();
					const FOsdsCarDefinition& Car = C.Sim.GetCar();
					int32 Gear = Car.ForwardGearRatios.Num() - 1;
					for (int32 G = 0; G < Car.ForwardGearRatios.Num(); ++G)
					{
						if (TargetSpeed / C.Sim.GetTireRadiusM() * Car.ForwardGearRatios[G] * Car.FinalDriveRatio * 60.0 / Tau102 < 4500.0) { Gear = G; break; }
					}
					C.Sim.State.Gear = Gear;
					C.Sim.State.EngineOmegaRadPerSecond = TargetSpeed / C.Sim.GetTireRadiusM() * Car.ForwardGearRatios[Gear] * Car.FinalDriveRatio;
					C.Sim.State.EngineRpm = C.Sim.State.EngineOmegaRadPerSecond * 60.0 / Tau102;
					State = 1; T = 0.0;
					LastHeading = FMath::Atan2(Forward.Y, Forward.X);
				}
				return;
			}
			// Control law at ControlHz (zero-order hold), the physics step otherwise.
			ControlClock += Dt;
			const double Period = ControlHz > 0.0 ? 1.0 / ControlHz : 0.0;
			const double Heading = FMath::Atan2(Forward.Y, Forward.X);
			const double RawYaw = FMath::FindDeltaAngleRadians(LastHeading, Heading) / Dt;
			LastHeading = Heading;
			SmoothedYaw += (FMath::Clamp(RawYaw, -2.0, 2.0) - SmoothedYaw) * (1.0 - FMath::Exp(-Dt / 0.1));
			if (ControlClock >= Period)
			{
				const double ControlDt = FMath::Max(ControlClock, Dt);
				ControlClock = 0.0;
				// Speed: the robot's own throttle/brake law (OsdsPreflight.cpp:919-927).
				const double Error = TargetSpeed - Speed;
				Throttle = 0.0; Brake = 0.0;
				if (Error > 0.0) Throttle = FMath::Clamp(0.2 + 0.35 * Error, 0.0, 1.0);
				else if (Error < -0.8 && Speed > 0.5) Brake = FMath::Clamp(0.2 * -Error, 0.05, 1.0);
				const double Y = C.Body.PositionM.Y;
				if (Mode == E102Mode::Pulses)
				{
					// +-0.6 deg road-wheel pulses of 0.4 s every 4 s, alternating.
					const double Ph = FMath::Fmod(T, 4.0);
					const int32 N = static_cast<int32>(T / 4.0);
					RackRad = (T > 2.0 && Ph < 0.4) ? OsdsUnits::ToRadians(0.6) * ((N & 1) ? -1.0 : 1.0) : 0.0;
				}
				else if (Mode == E102Mode::LaneChange)
				{
					// One sine period of road-wheel angle, 2 s, 1.6 deg: a lane change (net heading zero).
					const double S = T - 3.0;
					RackRad = (S >= 0.0 && S < 2.0) ? OsdsUnits::ToRadians(1.6) * FMath::Sin(Tau102 * S / 2.0) : 0.0;
				}
				else
				{
					// The robot's law: pure pursuit to y = 0, yaw-error stick (OsdsPreflight.cpp:782-828); range 8 deg estimate.
					const double LookM = FMath::Clamp(6.0 + 0.8 * FMath::Max(Speed, 0.0), 8.0, 30.0);
					const FVector2d ToTarget(LookM, -Y);
					const double Alpha = FMath::Atan2(Forward.X * ToTarget.Y - Forward.Y * ToTarget.X, FVector2d::DotProduct(FVector2d(Forward.X, Forward.Y), ToTarget));
					const double Curvature = 2.0 * FMath::Sin(Alpha) / LookM;
					const double YawError = Speed * Curvature - SmoothedYaw;
					Integral = Speed < 1.0 ? 0.0 : FMath::Clamp(Integral + 1.5 * YawError * ControlDt, -1.0, 1.0);
					const double WheelRad = FMath::Atan(Curvature * C.Sim.GetCar().WheelbaseM);
					const double FF = WheelRad / RangeRad;
					const double Want = FMath::Clamp(FF + Integral + 0.4 * YawError, -1.0, 1.0);
					Command += FMath::Clamp(Want - Command, -4.0 * ControlDt, 4.0 * ControlDt);
					RackRad = Command * RangeRad;
				}
			}
			C.Controls.Throttle = Throttle;
			C.Controls.Brake = Brake;
			C.Controls.RackAngleRad = RackRad;
			const double Beta = OsdsUnits::ToDegrees(FMath::Atan2(C.Body.LinearVelocityMps.Dot(Right), FMath::Max(FMath::Abs(Speed), 0.5)));
			R.PeakBetaDeg = FMath::Max(R.PeakBetaDeg, FMath::Abs(Beta));
			R.PeakYawDegS = FMath::Max(R.PeakYawDegS, OsdsUnits::ToDegrees(FMath::Abs(C.Body.AngularVelocityRadPerSecond.Z)));
			R.PeakLatG = FMath::Max(R.PeakLatG, FMath::Abs(Speed * C.Body.AngularVelocityRadPerSecond.Z) / OsdsUnits::StandardGravityMps2);
			R.PeakAbsY = FMath::Max(R.PeakAbsY, FMath::Abs(C.Body.PositionM.Y));
			for (int32 W = 2; W < 4; ++W) R.PeakRearSlipDeg = FMath::Max(R.PeakRearSlipDeg, OsdsUnits::ToDegrees(FMath::Abs(C.Sim.State.Wheels[W].SlipAngleRad)));
			if (R.SpinAtS < 0.0 && FMath::Abs(Beta) > 30.0) R.SpinAtS = T;
			if (T >= RunS || FMath::Abs(Beta) > 60.0)
			{
				R.EndSpeedKmh = Speed * 3.6;
				R.EndGear = C.Sim.State.Gear;
				R.bCompleted = true;
				bDone = true;
			}
		}
	};

	F102Result Run102(const FOsdsCarDefinition& Car, E102Mode Mode, double Kmh, double RunS, double ControlHz)
	{
		TSharedRef<F102Driver> D = MakeShared<F102Driver>();
		D->Mode = Mode; D->TargetSpeed = Kmh / 3.6; D->RunS = RunS; D->ControlHz = ControlHz;
		FOsdsVehicleBenchWorld World(OsdsShippingFixedStepS);
		if (!World.IsValid() || World.SpawnVehicle(Car, FVector3d(0.0, 0.0, 0.15), [D](FOsdsVehicleDriverContext& C) { D->Step(C); }) == nullptr) return D->R;
		World.RunUntil([&D]() { return D->bDone.load(); }, RunS + 5.0);
		return D->R;
	}

	void Log102(FAutomationTestBase& Test, const TCHAR* Tag, const TCHAR* ModeName, double Kmh, double Hz, const F102Result& R)
	{
		const FString Line = FString::Printf(TEXT("STAB102 %-14s %-10s %4.0f km/h ctl %3.0f Hz : peak_beta %5.1f rear_slip %5.1f yaw %5.1f deg/s lat %.2f g |y| %5.1f m spin_at %5.1f end %.0f km/h gear %d"),
			Tag, ModeName, Kmh, Hz, R.PeakBetaDeg, R.PeakRearSlipDeg, R.PeakYawDegS, R.PeakLatG, R.PeakAbsY, R.SpinAtS, R.EndSpeedKmh, R.EndGear);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleStab102, "OSDS.Vehicle.Stab102.nb2001_lowered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FOsdsVehicleStab102::RunTest(const FString& Parameters)
{
	FOsdsCarDefinition Base;
	if (!OsdsLoadBenchCar(*this, TEXT("nb2001_lowered"), Base)) return false;
	TestFalse(TEXT("ESC off"), Base.bStabilityControlEnabled);

	struct FCase { const TCHAR* Name; E102Mode Mode; double Kmh; double Hz; };
	const FCase Cases[] = {
		{ TEXT("pulses"), E102Mode::Pulses, 100.0, 0.0 },
		{ TEXT("lanechange"), E102Mode::LaneChange, 100.0, 0.0 },
		{ TEXT("robot_offset"), E102Mode::RobotOffsetStart, 66.0, 0.0 },
		{ TEXT("robot_offset"), E102Mode::RobotOffsetStart, 100.0, 0.0 },
		{ TEXT("robot_offset"), E102Mode::RobotOffsetStart, 100.0, 45.0 },
		{ TEXT("robot_straight"), E102Mode::Robot, 100.0, 45.0 },
	};
	for (const FCase& Case : Cases)
	{
		const F102Result R = Run102(Base, Case.Mode, Case.Kmh, 45.0, Case.Hz);
		Log102(*this, TEXT("baseline"), Case.Name, Case.Kmh, Case.Hz, R);
		if (!TestTrue(TEXT("completed"), R.bCompleted)) continue;
		TestTrue(FString::Printf(TEXT("%s %.0f km/h stays stable (peak beta %.1f deg < 10)"), Case.Name, Case.Kmh, R.PeakBetaDeg), R.PeakBetaDeg < 10.0);
	}
	// Lift-off in a bend, ESC off (ISO 7975 style, the existing bench).
	{
		const FOsdsLiftOffResult L = OsdsRunLiftOff(Base, OsdsShippingFixedStepS, { 0.7 }, 40.0, false);
		for (const FOsdsLiftOffCase& Case : L.Cases)
		{
			const FString Line = FString::Printf(TEXT("STAB102 liftoff %.1f g %s peak_beta %+.1f %s"), Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift"), Case.PeakBetaDeg, Case.bSpun ? TEXT("SPIN") : TEXT("ok"));
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			AddInfo(Line);
		}
	}
	return true;
}

#endif
