// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/OsdsUnits.h"
#include "Vehicle/OsdsVehicle.h"
#include "Vehicle/OsdsVehicleMovementComponent.h"

namespace
{
	constexpr EAutomationTestFlags VehicleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;
}

/**
 * The driving shell's view: game-thread controls in, telemetry out, with
 * Chaos on its own task-graph threads as in the game.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsVehicleInterfaceTest, "OSDS.Vehicle.Interface", VehicleTestFlags)
bool FOsdsVehicleInterfaceTest::RunTest(const FString& Parameters)
{
	FOsdsCarDefinition Car;
	if (!OsdsLoadBenchCar(*this, TEXT("nb1999"), Car))
	{
		return false;
	}
	FOsdsVehicleBenchWorld Bench(OsdsShippingFixedStepS, 3000.0, false);
	AOsdsVehicle* Vehicle = Bench.SpawnVehicle(Car, FVector3d(0.0, 0.0, 0.05), nullptr, true);
	if (!TestNotNull(TEXT("vehicle"), Vehicle))
	{
		return false;
	}
	UOsdsVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement();
	auto Telemetry = [Movement]() { return Movement->GetTelemetry(); };
	auto WaitSim = [&Bench, &Telemetry](double Seconds)
	{
		const double Until = Telemetry().SimTimeS + Seconds;
		Bench.RunUntil([&Telemetry, Until]() { return Telemetry().SimTimeS >= Until; }, Seconds * 4.0 + 2.0);
	};

	WaitSim(1.5);
	FOsdsVehicleTelemetry T = Telemetry();
	TestTrue(TEXT("the physics thread steps the vehicle"), T.PhysicsSteps > 400);
	TestEqual(TEXT("fixed step"), T.FixedStepSeconds, OsdsShippingFixedStepS, 1.0e-6);
	TestEqual(TEXT("four wheels on the ground"), T.WheelContactCount, 4);
	TestEqual(TEXT("four wheel rows"), T.Wheels.Num(), 4);
	TestEqual(TEXT("automatic in first"), T.GearText, FString(TEXT("1")));
	TestTrue(TEXT("at rest"), FMath::Abs(T.ForwardSpeedMps) < 0.05);
	TestTrue(TEXT("idle"), FMath::Abs(T.EngineRpm - Car.IdleSpeedRpm) < 150.0);
	double Load = 0.0;
	for (const FOsdsWheelTelemetry& Wheel : T.Wheels)
	{
		Load += Wheel.LoadN;
	}
	TestEqual(TEXT("the wheels carry the car"), Load, Car.MassKg * 9.8, Car.MassKg * 9.8 * 0.01);

	// Throttle: the car drives away and the automatic upshifts.
	Vehicle->SetThrottle(1.0f);
	WaitSim(4.0);
	T = Telemetry();
	{ const FString Line = FString::Printf(TEXT("INTERFACE after 4 s full throttle: %.1f m/s, %.0f rpm, gear %s"), T.ForwardSpeedMps, T.EngineRpm, *T.GearText); UE_LOG(LogTemp, Display, TEXT("%s"), *Line); AddInfo(Line); }
	TestTrue(TEXT("throttle accelerates"), T.ForwardSpeedMps > 15.0);
	TestTrue(TEXT("automatic upshifted"), T.Gear >= 2);
	TestTrue(TEXT("rear wheels drive (positive slip)"), T.Wheels[2].SlipRatio > 0.0 && T.Wheels[3].SlipRatio > 0.0);

	// Steering right: rack and road wheels to the right, the car yaws right.
	Vehicle->SetThrottle(0.3f);
	Vehicle->SetSteer(0.3f);
	WaitSim(1.0);
	T = Telemetry();
	TestTrue(TEXT("rack angle positive (right)"), T.SteeringAngleDeg > 0.5);
	TestTrue(TEXT("front wheels steer right"), T.Wheels[0].SteerAngleDeg > 0.5 && T.Wheels[1].SteerAngleDeg > 0.5);
	TestTrue(TEXT("outer (left) tyres carry more load in a right turn"), T.Wheels[0].LoadN > T.Wheels[1].LoadN);
	TestTrue(TEXT("lateral force toward the turn centre"), T.Wheels[1].LateralForceN > 0.0);
	const FRotator Before = Vehicle->GetActorRotation();
	WaitSim(1.0);
	TestTrue(TEXT("the car yaws right"), FMath::FindDeltaAngleDegrees(Before.Yaw, Vehicle->GetActorRotation().Yaw) > 2.0);

	// Brake to a stop: never reverse while rolling forward.
	Vehicle->SetSteer(0.0f);
	Vehicle->SetThrottle(0.0f);
	Vehicle->SetBrake(1.0f);
	bool bReversedWhileRolling = false;
	Bench.RunUntil([&]()
	{
		const FOsdsVehicleTelemetry Now = Telemetry();
		bReversedWhileRolling |= Now.bInReverse && Now.ForwardSpeedMps > 1.0;
		return Now.ForwardSpeedMps < 0.3;
	}, 20.0);
	TestFalse(TEXT("no reverse while rolling forward"), bReversedWhileRolling);
	TestTrue(TEXT("brake lamps on"), Telemetry().bServiceBrake || Telemetry().ForwardSpeedMps < 0.45);
	Vehicle->SetBrake(0.0f);

	// Manual gearbox, the driver's own clutch (osds.Gearbox.AutoClutch 0): neutral at rest,
	// clutch to select first, release to drive. The automatic clutch is tested after it.
	IConsoleVariable* AutoClutch = IConsoleManager::Get().FindConsoleVariable(TEXT("osds.Gearbox.AutoClutch"));
	const int32 AutoClutchWas = AutoClutch ? AutoClutch->GetInt() : 1;
	if (AutoClutch) AutoClutch->Set(0, ECVF_SetByCode);
	ON_SCOPE_EXIT { if (AutoClutch) AutoClutch->Set(AutoClutchWas, ECVF_SetByCode); };
	Vehicle->ToggleManual();
	WaitSim(0.5);
	T = Telemetry();
	TestTrue(TEXT("manual selected"), T.bManual);
	TestEqual(TEXT("manual at rest is neutral"), T.GearText, FString(TEXT("N")));
	Vehicle->ShiftUp();
	Bench.RunUntil([&Telemetry]() { return !Telemetry().LastGearMessage.IsEmpty(); }, 0.5);
	TestEqual(TEXT("no gear without the clutch"), Telemetry().LastGearMessage, FString(TEXT("Hold the clutch to change gear")));
	Vehicle->SetClutch(1.0f);
	Vehicle->ShiftUp();
	Bench.RunUntil([&Telemetry]() { return Telemetry().GearText == TEXT("1"); }, 0.5);
	TestEqual(TEXT("same-frame clutch and shift reaches first"), Telemetry().GearText, FString(TEXT("1")));
	Vehicle->SetThrottle(0.35f);
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		Vehicle->SetClutch(1.0f - Step / 20.0f);
		WaitSim(0.1);
	}
	WaitSim(1.0);
	T = Telemetry();
	{ const FString Line = FString::Printf(TEXT("INTERFACE manual launch: %.1f m/s, %.0f rpm, stalled %d"), T.ForwardSpeedMps, T.EngineRpm, T.bStalled ? 1 : 0); UE_LOG(LogTemp, Display, TEXT("%s"), *Line); AddInfo(Line); }
	TestFalse(TEXT("a gentle clutch release does not stall"), T.bStalled);
	TestTrue(TEXT("the manual car drives off"), T.ForwardSpeedMps > 2.0);

	// The automatic clutch (the default): stop in gear without stalling, RB alone selects first
	// from neutral, and the throttle alone drives off.
	if (AutoClutch) AutoClutch->Set(1, ECVF_SetByCode);
	Vehicle->SetClutch(0.0f);
	Vehicle->SetThrottle(0.0f);
	Vehicle->SetBrake(1.0f);
	Bench.RunUntil([&]() { return FMath::Abs(Telemetry().ForwardSpeedMps) < 0.05; }, 20.0);
	WaitSim(1.0);
	TestFalse(TEXT("auto clutch: braking to a stop in gear does not stall"), Telemetry().bStalled);
	Vehicle->SetBrake(0.0f);
	for (int32 Down = 0; Down < 6 && Telemetry().GearText != TEXT("N"); ++Down)
	{
		Vehicle->ShiftDown();
		WaitSim(0.4);
	}
	TestEqual(TEXT("auto clutch: LB alone reaches neutral"), Telemetry().GearText, FString(TEXT("N")));
	Vehicle->ShiftUp();
	Bench.RunUntil([&Telemetry]() { return Telemetry().GearText == TEXT("1"); }, 1.0);
	TestEqual(TEXT("auto clutch: RB alone selects first"), Telemetry().GearText, FString(TEXT("1")));
	// Half the trigger (about 29 % throttle through the pedal curve), as a normal pull-away.
	Vehicle->SetThrottle(0.5f);
	WaitSim(3.0);
	T = Telemetry();
	{ const FString Line = FString::Printf(TEXT("INTERFACE auto-clutch launch: %.1f m/s, %.0f rpm, stalled %d"), T.ForwardSpeedMps, T.EngineRpm, T.bStalled ? 1 : 0); UE_LOG(LogTemp, Display, TEXT("%s"), *Line); AddInfo(Line); }
	TestFalse(TEXT("auto clutch: pulling away does not stall"), T.bStalled);
	TestTrue(TEXT("auto clutch: the throttle alone drives off"), T.ForwardSpeedMps > 2.0);
	if (AutoClutch) AutoClutch->Set(0, ECVF_SetByCode);

	// Handbrake holds the rear wheels at rest.
	Vehicle->SetThrottle(0.0f);
	Vehicle->SetClutch(1.0f);
	Vehicle->SetBrake(1.0f);
	Bench.RunUntil([&]() { return FMath::Abs(Telemetry().ForwardSpeedMps) < 0.05; }, 20.0);
	Vehicle->SetBrake(0.0f);
	Vehicle->SetHandbrake(true);
	WaitSim(1.0);
	T = Telemetry();
	TestTrue(TEXT("handbrake: rear wheels stopped"), FMath::Abs(T.Wheels[2].AngularSpeedRadPerSecond) < 0.01 && FMath::Abs(T.Wheels[3].AngularSpeedRadPerSecond) < 0.01);
	TestTrue(TEXT("handbrake: car stays put"), FMath::Abs(T.ForwardSpeedMps) < 0.05);

	// Reset puts the car back on its wheels at rest.
	Vehicle->SetHandbrake(false);
	Vehicle->ResetAt(FVector(1000.0, 500.0, 0.0), 90.0f);
	WaitSim(1.0);
	T = Telemetry();
	TestTrue(TEXT("reset moves the car"), FVector::Dist2D(Vehicle->GetActorLocation(), FVector(1000.0, 500.0, 0.0)) < 5.0);
	TestEqual(TEXT("reset heading"), Vehicle->GetActorRotation().Yaw, 90.0, 1.0);
	TestEqual(TEXT("reset keeps the manual gearbox in neutral"), T.GearText, FString(TEXT("N")));
	TestEqual(TEXT("four wheels down after reset"), T.WheelContactCount, 4);
	return true;
}

#endif
