// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Gameplay/OsdsCameraModels.h"
#include "Gameplay/OsdsDriverShaping.h"
#include "Vehicle/OsdsSteeringGeometry.h"
#include "Gameplay/OsdsHaptics.h"
#include "Gameplay/OsdsHUD.h"
#include "Gameplay/OsdsInputConfig.h"
#include "Gameplay/OsdsPlayerCameraManager.h"
#include "Gameplay/OsdsPlaceholderVehicle.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/CarDefinitionLoader.h"

namespace OsdsGameplayTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** The 1999 roadster: FOsdsVehicleSetup defaults are the NB's. */
	FOsdsVehicleSetup Roadster() { return FOsdsVehicleSetup(); }

	FOsdsDriveTelemetry Moving(double Vx, double Vy = 0.0, double YawRate = 0.0)
	{
		FOsdsDriveTelemetry T;
		T.BodyVelocityMps = FVector(Vx, Vy, 0.0);
		T.BodyAngularVelocityRadPerS = FVector(0.0, 0.0, YawRate);
		return T;
	}

	/** Run the shaper for Seconds at 120 Hz with fixed raw controls. */
	double Hold(FOsdsDriverShaper& Shaper, const FOsdsDriveTelemetry& T, const FOsdsRawControls& Raw, double Seconds,
		FOsdsDriverCommand* OutCommand = nullptr, double* OutLargestStep = nullptr)
	{
		const FOsdsVehicleSetup Setup = Roadster();
		const int32 Steps = FMath::RoundToInt(Seconds * 120.0);
		FOsdsDriverCommand Command;
		double Largest = 0.0;
		for (int32 I = 0; I < Steps; ++I)
		{
			const double Before = Shaper.GetRackAngleRad();
			Command = Shaper.Update(Setup, T, Raw, 1.0 / 120.0);
			Largest = FMath::Max(Largest, FMath::Abs(Shaper.GetRackAngleRad() - Before));
		}
		if (OutCommand) { *OutCommand = Command; }
		if (OutLargestStep) { *OutLargestStep = Largest; }
		return Shaper.GetRackAngleRad();
	}

	const FEnhancedActionKeyMapping* FindMapping(const UOsdsInputConfig* Config, const TCHAR* Action, const FKey& Key)
	{
		const UInputAction* Wanted = Config->FindAction(Action);
		for (const FEnhancedActionKeyMapping& Mapping : Config->DrivingContext->GetMappings())
		{
			if (Mapping.Action == Wanted && Mapping.Key == Key)
			{
				return &Mapping;
			}
		}
		return nullptr;
	}

	float ApplyModifiers(const FEnhancedActionKeyMapping& Mapping, float Raw)
	{
		FInputActionValue Value(Raw);
		for (const UInputModifier* Modifier : Mapping.Modifiers)
		{
			Value = Modifier->ModifyRaw(nullptr, Value, 1.f / 60.f);
		}
		return Value.Get<float>();
	}
}

using namespace OsdsGameplayTest;

// --- Input ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsInputBindingsTest, "OSDS.Gameplay.Input.BindingsMatchControlsDoc", Flags)
bool FOsdsInputBindingsTest::RunTest(const FString& Parameters)
{
	UOsdsInputConfig* Config = UOsdsInputConfig::Create(GetTransientPackage());
	TestNotNull(TEXT("mapping context"), Config->DrivingContext.Get());

	// docs/controls.md, keyboard and Xbox columns.
	struct FExpect { const TCHAR* Action; FKey Key; };
	const FExpect Expected[] = {
		{TEXT("ThrottlePad"), EKeys::Gamepad_RightTriggerAxis}, {TEXT("ThrottleKey"), EKeys::W}, {TEXT("ThrottleKey"), EKeys::Up},
		{TEXT("BrakePad"), EKeys::Gamepad_LeftTriggerAxis}, {TEXT("BrakeKey"), EKeys::S}, {TEXT("BrakeKey"), EKeys::Down},
		{TEXT("SteerPad"), EKeys::Gamepad_LeftX}, {TEXT("SteerKey"), EKeys::A}, {TEXT("SteerKey"), EKeys::D},
		{TEXT("SteerKey"), EKeys::Left}, {TEXT("SteerKey"), EKeys::Right},
		{TEXT("FullBrake"), EKeys::SpaceBar},
		{TEXT("ParkingBrake"), EKeys::H}, {TEXT("ParkingBrake"), EKeys::Gamepad_FaceButton_Right},
		{TEXT("ToggleTransmission"), EKeys::T}, {TEXT("ToggleTransmission"), EKeys::Gamepad_RightThumbstick},
		{TEXT("Clutch"), EKeys::LeftShift}, {TEXT("Clutch"), EKeys::Gamepad_FaceButton_Bottom},
		{TEXT("ShiftUp"), EKeys::E}, {TEXT("ShiftUp"), EKeys::Gamepad_RightShoulder},
		{TEXT("ShiftDown"), EKeys::Q}, {TEXT("ShiftDown"), EKeys::Gamepad_LeftShoulder},
		{TEXT("StartEngine"), EKeys::I}, {TEXT("StartEngine"), EKeys::Gamepad_DPad_Left},
		{TEXT("CycleCamera"), EKeys::C}, {TEXT("CycleCamera"), EKeys::Gamepad_FaceButton_Top},
		{TEXT("CycleHud"), EKeys::F2}, {TEXT("CycleHud"), EKeys::Gamepad_FaceButton_Left},
		{TEXT("ToggleAtlas"), EKeys::Tab}, {TEXT("ToggleAtlas"), EKeys::Gamepad_Special_Right},
		{TEXT("FindPlace"), EKeys::Slash},
		{TEXT("Recover"), EKeys::R}, {TEXT("Recover"), EKeys::Gamepad_Special_Left},
		{TEXT("ToggleSound"), EKeys::M}, {TEXT("ToggleLights"), EKeys::L}, {TEXT("CycleLens"), EKeys::F6},{TEXT("ToggleStats"), EKeys::F3},
	};
	for (const FExpect& E : Expected)
	{
		TestNotNull(FString::Printf(TEXT("%s on %s"), E.Action, *E.Key.ToString()), FindMapping(Config, E.Action, E.Key));
	}
	TestEqual(TEXT("one mapping per table row"), Config->DrivingContext->GetMappings().Num(), UOsdsInputConfig::Bindings().Num());

	// No key drives two actions: a press must mean one thing.
	TMap<FKey, int32> Uses;
	for (const FEnhancedActionKeyMapping& Mapping : Config->DrivingContext->GetMappings())
	{
		Uses.FindOrAdd(Mapping.Key)++;
	}
	for (const TPair<FKey, int32>& Pair : Uses)
	{
		TestEqual(FString::Printf(TEXT("%s bound once"), *Pair.Key.ToString()), Pair.Value, 1);
	}
	// The steering stick's click does nothing: it is pressed by accident in hard steering.
	TestFalse(TEXT("left-stick click unbound"), Uses.Contains(EKeys::Gamepad_LeftThumbstick));
	TestTrue(TEXT("A and D cancel"), Config->SteerKey->AccumulationBehavior == EInputActionAccumulationBehavior::Cumulative);
	TestTrue(TEXT("stick is an axis"), Config->SteerPad->ValueType == EInputActionValueType::Axis1D);
	TestTrue(TEXT("trigger is an axis"), Config->ThrottlePad->ValueType == EInputActionValueType::Axis1D);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsInputModifierTest, "OSDS.Gameplay.Input.DeadZonesAndDirections", Flags)
bool FOsdsInputModifierTest::RunTest(const FString& Parameters)
{
	UOsdsInputConfig* Config = UOsdsInputConfig::Create(GetTransientPackage());
	const FEnhancedActionKeyMapping* Stick = FindMapping(Config, TEXT("SteerPad"), EKeys::Gamepad_LeftX);
	const FEnhancedActionKeyMapping* Trigger = FindMapping(Config, TEXT("ThrottlePad"), EKeys::Gamepad_RightTriggerAxis);
	const FEnhancedActionKeyMapping* KeyA = FindMapping(Config, TEXT("SteerKey"), EKeys::A);
	const FEnhancedActionKeyMapping* KeyD = FindMapping(Config, TEXT("SteerKey"), EKeys::D);
	if (!Stick || !Trigger || !KeyA || !KeyD)
	{
		AddError(TEXT("missing mappings"));
		return false;
	}
	// Godot's 0.12 stick dead zone, rescaled so full travel still reaches 1.
	TestEqual(TEXT("stick inside dead zone"), ApplyModifiers(*Stick, 0.11f), 0.f);
	TestNearlyEqual(TEXT("stick half travel"), ApplyModifiers(*Stick, 0.56f), 0.5f, 1e-4f);
	TestNearlyEqual(TEXT("stick left full"), ApplyModifiers(*Stick, -1.f), -1.f, 1e-4f);
	TestNearlyEqual(TEXT("stick right full"), ApplyModifiers(*Stick, 1.f), 1.f, 1e-4f);
	// Triggers: 0.04 dead zone, linear above it.
	TestEqual(TEXT("trigger rest"), ApplyModifiers(*Trigger, 0.03f), 0.f);
	TestNearlyEqual(TEXT("trigger linear"), ApplyModifiers(*Trigger, 0.52f), 0.5f, 1e-4f);
	TestNearlyEqual(TEXT("trigger full"), ApplyModifiers(*Trigger, 1.f), 1.f, 1e-4f);
	// A steers left (negative), D right.
	TestEqual(TEXT("A is left"), ApplyModifiers(*KeyA, 1.f), -1.f);
	TestEqual(TEXT("D is right"), ApplyModifiers(*KeyD, 1.f), 1.f);
	return true;
}

// --- Steering shaping -------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsSteeringRangeTest, "OSDS.Gameplay.Steering.RangeMatchesGodot", Flags)
bool FOsdsSteeringRangeTest::RunTest(const FString& Parameters)
{
	const FOsdsVehicleSetup Setup = Roadster();
	// Powertrain.available_steering: atan(2.266 * 0.95 g / 400) + 5 deg = 8.02 deg at 20 m/s.
	TestNearlyEqual(TEXT("NB at 20 m/s"), FMath::RadiansToDegrees(OsdsShaping::AvailableSteeringRad(Setup, 20.0)), 8.02, 0.03);
	TestNearlyEqual(TEXT("parking lock"), FMath::RadiansToDegrees(OsdsShaping::AvailableSteeringRad(Setup, 0.0)), 36.6, 1e-6);
	TestTrue(TEXT("range closes with speed"), OsdsShaping::AvailableSteeringRad(Setup, 30.0) < OsdsShaping::AvailableSteeringRad(Setup, 15.0));
	TestNearlyEqual(TEXT("reverse is symmetric"), OsdsShaping::AvailableSteeringRad(Setup, -7.0), OsdsShaping::AvailableSteeringRad(Setup, 7.0), 1e-12);
	// Stick centre goes to lane corrections, the ends keep full range.
	const double Small = OsdsShaping::ThumbstickCommand(Setup, 0.1, 30.0);
	TestTrue(TEXT("stick centre is gentle at speed"), Small < 0.1 && Small > 0.0);
	TestNearlyEqual(TEXT("full stick is full range"), OsdsShaping::ThumbstickCommand(Setup, 1.0, 30.0), 1.0, 1e-12);
	TestNearlyEqual(TEXT("parking: stick is linear"), OsdsShaping::ThumbstickCommand(Setup, 0.3, 0.0), 0.3, 1e-9);
	// The robot's inverse: the raw stick for a wanted command, back through the input's 0.12 dead
	// zone, the player's dead zone (default 0), the fine centre and the quickness.
	for (const double Speed : { 0.0, 11.0, 30.0 })
	{
		for (const double Want : { -0.6, -0.1, 0.05, 0.3, 0.9 })
		{
			const double Raw = OsdsShaping::PadStickForCommand(Setup, Want, Speed);
			const double AfterInput = FMath::Sign(Raw) * FMath::Max(0.0, (FMath::Abs(Raw) - 0.12) / 0.88);
			const double Pad = OsdsShaping::StickDeadZone(AfterInput, 0.0);
			const double Quick = FMath::Clamp(Setup.SteeringQuickness, 0.5, 2.0);
			const double Back = FMath::Clamp(OsdsShaping::ThumbstickCommand(Setup, Pad, Speed) * Quick, -1.0, 1.0);
			TestNearlyEqual(*FString::Printf(TEXT("stick for command %.2f at %.0f m/s"), Want, Speed), Back, Want, 1e-4);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsSteeringCentreTest, "OSDS.Gameplay.Steering.StickAndKeysCentreExactly", Flags)
bool FOsdsSteeringCentreTest::RunTest(const FString& Parameters)
{
	FOsdsDriverShaper Shaper;
	FOsdsRawControls Raw;
	Raw.PadSteer = -0.3;
	TestTrue(TEXT("stick left steers left"), Hold(Shaper, Moving(20.0), Raw, 0.5) < 0.0);
	TestTrue(TEXT("uses the stick map"), Shaper.IsUsingThumbstick());
	Raw.PadSteer = 0.3;
	TestTrue(TEXT("stick right steers right"), Hold(Shaper, Moving(20.0), Raw, 0.6) > 0.0);
	Raw.PadSteer = 0.0;
	TestEqual(TEXT("released stick returns to exactly zero"), Hold(Shaper, Moving(20.0), Raw, 0.5), 0.0);

	Raw.KeySteer = -1.0;
	const double Lock = Hold(Shaper, Moving(0.0), Raw, 1.0);
	TestNearlyEqual(TEXT("A held at rest reaches the parking lock"), Lock, -FMath::DegreesToRadians(36.6), 1e-6);
	Raw.KeySteer = 0.0;
	TestEqual(TEXT("released key returns to exactly zero"), Hold(Shaper, Moving(0.0), Raw, 1.0), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsSteeringKeyRateTest, "OSDS.Gameplay.Steering.KeyTapAtHighwaySpeed", Flags)
bool FOsdsSteeringKeyRateTest::RunTest(const FString& Parameters)
{
	// Powertrain.gd: a half-second tap at 60 mph reaches about 6 deg, not the lock.
	FOsdsDriverShaper Shaper;
	FOsdsRawControls Raw;
	Raw.KeySteer = 1.0;
	const double Rack = FMath::RadiansToDegrees(Hold(Shaper, Moving(26.8), Raw, 0.5));
	TestNearlyEqual(TEXT("0.5 s tap at 60 mph"), Rack, 6.0, 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCountersteerTest, "OSDS.Gameplay.Steering.CountersteerAllowance", Flags)
bool FOsdsCountersteerTest::RunTest(const FString& Parameters)
{
	const FOsdsVehicleSetup Setup = Roadster();
	// A right-hand corner whose rear has stepped out to the left: the car moves
	// left of where its nose points, the front axle 15 deg left of the nose.
	const double Beta = FMath::DegreesToRadians(-15.0);
	const FOsdsDriveTelemetry Slide = Moving(20.0, 20.0 * FMath::Tan(Beta), 0.0);
	TestNearlyEqual(TEXT("beta_f"), OsdsShaping::FrontAxleSlipAngleRad(Setup, 20.0, Slide.BodyVelocityMps.Y, 0.0), Beta, 1e-9);

	FOsdsDriverShaper Shaper;
	FOsdsRawControls Raw;
	Raw.PadSteer = -1.0;
	FOsdsDriverCommand Command;
	const double Counter = Hold(Shaper, Slide, Raw, 0.6, &Command);
	// |beta_f| + 5 deg surplus = 20 deg, reachable at the 40 deg/s countersteer rate.
	TestNearlyEqual(TEXT("full opposite lock follows the slide"), FMath::RadiansToDegrees(Counter), -20.0, 0.01);
	TestNearlyEqual(TEXT("range toward the slide"), FMath::RadiansToDegrees(Shaper.GetRangeLeftRad()), 20.0, 1e-6);
	TestNearlyEqual(TEXT("range away from it is unchanged"), FMath::RadiansToDegrees(Shaper.GetRangeRightRad()), 8.02, 0.03);

	FOsdsDriverShaper Other;
	Raw.PadSteer = 1.0;
	const double Into = Hold(Other, Slide, Raw, 1.0);
	TestNearlyEqual(TEXT("steering into the slide keeps the normal cap"), FMath::RadiansToDegrees(Into), 8.02, 0.03);

	// The slide ends: the rack unwinds at the return rate, never jumps.
	double LargestStep = 0.0;
	Raw.PadSteer = -1.0;
	const double After = Hold(Shaper, Moving(20.0), Raw, 1.0, nullptr, &LargestStep);
	TestNearlyEqual(TEXT("back to the normal cap"), FMath::RadiansToDegrees(After), -8.02, 0.03);
	const double ReturnPerStep = OsdsShaping::PadReturnRatePerS * OsdsShaping::AvailableSteeringRad(Setup, 20.0) / 120.0;
	TestTrue(TEXT("closing allowance is slewed"), LargestStep <= ReturnPerStep + 1e-9);

	// No allowance below 5 m/s or in reverse, where the angle is noise.
	TestEqual(TEXT("slow"), OsdsShaping::FrontAxleSlipAngleRad(Setup, 4.0, -3.0, 0.0), 0.0);
	TestEqual(TEXT("reverse"), OsdsShaping::FrontAxleSlipAngleRad(Setup, -10.0, -3.0, 0.0), 0.0);
	// A yaw rate alone moves the front axle sideways: r * a.
	TestNearlyEqual(TEXT("yaw contributes"), OsdsShaping::FrontAxleSlipAngleRad(Setup, 20.0, 0.0, 1.0), FMath::Atan2(Setup.CenterOfMassToFrontAxleM, 20.0), 1e-12);
	return true;
}

/**
 * The rim turns as a hand turns a real one. The column gives the quoted ratio on centre and the
 * quoted lock-to-lock turns at the lock; the stick may not spin the rim faster than about 450 deg/s
 * parking or 300 at speed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsHandRateTest, "OSDS.Gameplay.Steering.HandRate", Flags)
bool FOsdsHandRateTest::RunTest(const FString& Parameters)
{
	struct FCar { const TCHAR* Name; double Ratio; double LockDeg; double Turns; };
	for (const FCar& Car : { FCar{ TEXT("NB Miata"), 15.0, 36.6, 2.6 }, FCar{ TEXT("1965 Mustang"), 21.7, 33.0, 3.73 }, FCar{ TEXT("C8"), 15.7, 34.0, 2.5 } })
	{
		const double Lock = FMath::DegreesToRadians(Car.LockDeg);
		TestNearlyEqual(FString(Car.Name) + TEXT(": the rim at lock is half the lock-to-lock turns"),
			FMath::RadiansToDegrees(OsdsSteering::HandWheelRad(Lock, Car.Ratio, Lock, Car.Turns)), Car.Turns * 180.0, 0.5);
		TestNearlyEqual(FString(Car.Name) + TEXT(": the quoted ratio on centre"), OsdsSteering::LocalRatio(0.0, Car.Ratio, Lock, Car.Turns), Car.Ratio, 1e-9);
		const double Rim = OsdsSteering::HandWheelRad(0.3, Car.Ratio, Lock, Car.Turns);
		TestNearlyEqual(FString(Car.Name) + TEXT(": a USB rim maps back to the same rack"), OsdsSteering::RackRadForHandWheel(Rim, Car.Ratio, Lock, Car.Turns), 0.3, 1e-6);

		FOsdsVehicleSetup Setup = Roadster();
		Setup.SteeringRatio = Car.Ratio;
		Setup.ParkingRackAngleRad = Lock;
		Setup.SteeringLockTurns = Car.Turns;
		for (const double Speed : { 1.0, 30.0 })
		{
			FOsdsDriverShaper Shaper;
			FOsdsRawControls Raw;
			Raw.PadSteer = 1.0;
			const FOsdsDriveTelemetry Telemetry = Moving(Speed);
			// Step by step: the largest rim step a 120 Hz frame takes, as deg/s.
			double Previous = 0.0, Fastest = 0.0;
			FOsdsDriverCommand Command;
			for (int32 Step = 0; Step < 360; ++Step)
			{
				Command = Shaper.Update(Setup, Telemetry, Raw, 1.0 / 120.0);
				const double RimDeg = FMath::RadiansToDegrees(OsdsSteering::HandWheelRad(Command.RackAngleRad, Car.Ratio, Lock, Car.Turns));
				Fastest = FMath::Max(Fastest, FMath::Abs(RimDeg - Previous) * 120.0);
				Previous = RimDeg;
			}
			const double Allowed = Speed < 5.0 ? 450.0 : 300.0;
			AddInfo(FString::Printf(TEXT("HANDRATE %s at %.0f m/s: rim at most %.0f deg/s, ends at %.0f deg"), Car.Name, Speed, Fastest, Previous));
			TestTrue(FString::Printf(TEXT("%s at %.0f m/s: the rim no faster than %.0f deg/s (%.0f)"), Car.Name, Speed, Allowed, Fastest), Fastest <= Allowed + 1.0);
			if (Speed < 5.0)
			{
				TestNearlyEqual(FString(Car.Name) + TEXT(": parking, the stick reaches full lock"), Previous, Car.Turns * 180.0, 1.0);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPedalTest, "OSDS.Gameplay.Steering.PedalRamps", Flags)
bool FOsdsPedalTest::RunTest(const FString& Parameters)
{
	FOsdsDriverShaper Shaper;
	FOsdsRawControls Raw;
	FOsdsDriverCommand Command;
	Raw.bKeyBrake = true;
	Raw.bKeyThrottle = true;
	Hold(Shaper, Moving(20.0), Raw, 0.3, &Command);
	TestNearlyEqual(TEXT("0.3 s brake tap reaches 0.75"), Command.Brake, 0.75f, 0.01f);
	TestNearlyEqual(TEXT("0.3 s throttle tap reaches 0.9"), Command.Throttle, 0.9f, 0.01f);
	Raw.bKeyBrake = false;
	Raw.bKeyThrottle = false;
	Hold(Shaper, Moving(20.0), Raw, 0.125, &Command);
	TestEqual(TEXT("released within 1/8 s"), Command.Brake, 0.f);

	FOsdsDriverShaper Pad;
	FOsdsRawControls Trigger;
	Trigger.PadThrottle = 0.37;
	Trigger.PadBrake = 0.81;
	Hold(Pad, Moving(10.0), Trigger, 1.0 / 120.0, &Command);
	TestNearlyEqual(TEXT("trigger throttle follows the pedal curve"), Command.Throttle,
		static_cast<float>(FMath::Pow(0.37, OsdsShaping::DefaultTriggerGamma)), 1e-6f);
	TestNearlyEqual(TEXT("half trigger is light part throttle"), static_cast<float>(OsdsShaping::TriggerThrottle(0.5, OsdsShaping::DefaultTriggerGamma)), 0.287f, 0.002f);
	TestEqual(TEXT("full trigger is full throttle"), OsdsShaping::TriggerThrottle(1.0, OsdsShaping::DefaultTriggerGamma), 1.0);
	// The player's steering dead zone: inside it nothing, past it the rest of the travel still reaches full lock.
	TestEqual(TEXT("no dead zone passes the stick"), OsdsShaping::StickDeadZone(0.03, 0.0), 0.03);
	TestEqual(TEXT("inside a 10 % dead zone is centre"), OsdsShaping::StickDeadZone(-0.08, 0.1), 0.0);
	TestNearlyEqual(TEXT("past it the travel is rescaled"), OsdsShaping::StickDeadZone(0.55, 0.1), 0.5, 1e-9);
	TestEqual(TEXT("full stick is still full lock"), OsdsShaping::StickDeadZone(-1.0, 0.1), -1.0);
	// The brake trigger has a gentle start (trigger^1.4): a full squeeze is still full brake.
	TestNearlyEqual(TEXT("trigger brake follows its pedal curve"), Command.Brake, static_cast<float>(FMath::Pow(0.81, 1.4)), 1e-5f);

	Trigger.bClutch = true;
	Hold(Pad, Moving(10.0), Trigger, 0.1, &Command);
	TestEqual(TEXT("clutch pressed in 0.1 s"), Command.Clutch, 1.f);
	Trigger.bClutch = false;
	Hold(Pad, Moving(10.0), Trigger, 0.2, &Command);
	TestNearlyEqual(TEXT("clutch released slowly (2.8/s)"), Command.Clutch, 0.44f, 0.01f);
	return true;
}

// --- Haptics ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsHapticsTest, "OSDS.Gameplay.Haptics.SlipRumble", Flags)
bool FOsdsHapticsTest::RunTest(const FString& Parameters)
{
	using namespace OsdsHaptics;
	const FOsdsCarDefinition Car;
	TestNearlyEqual(TEXT("NB plateau angle (Haptics.gd: 10.6 deg)"), FMath::RadiansToDegrees(PlateauSlipAngleRad(Car)), 10.6, 0.15);

	TestEqual(TEXT("no feel below the onset"), SlipFeel(0.75), 0.0);
	TestNearlyEqual(TEXT("limit share at the plateau end"), SlipFeel(1.15), 0.65, 1e-12);
	TestNearlyEqual(TEXT("full deep in a slide"), SlipFeel(2.5), 1.0, 1e-12);
	double Previous = 0.0;
	bool bMonotonic = true;
	for (int32 I = 0; I <= 400; ++I)
	{
		const double F = SlipFeel(I * 0.01);
		bMonotonic &= F + 1e-12 >= Previous;
		Previous = F;
	}
	TestTrue(TEXT("never falls as slip grows"), bMonotonic);

	TestEqual(TEXT("ABS pulse on"), AbsPulse(true, 0.01), AbsOn);
	TestEqual(TEXT("ABS pulse off half a cycle later (17 Hz)"), AbsPulse(true, 0.04), 0.0);
	TestEqual(TEXT("no pulse without ABS"), AbsPulse(false, 0.01), 0.0);

	// Coasting straight on a smooth road: silence.
	FOsdsDriveTelemetry Coast = Moving(25.0);
	for (FOsdsDriveWheelTelemetry& Wheel : Coast.Wheels)
	{
		Wheel.bContact = true;
		Wheel.NormalLoadN = 2500.0;
		Wheel.PeakSlipAngleRad = PlateauSlipAngleRad(Car);
	}
	FOsdsHapticsModel Model;
	const FOsdsRumble Cruise = Model.Sample(Coast, 0.0, 0.0, 1.0 / 60.0);
	TestTrue(TEXT("coasting on smooth asphalt: only a light road hum"), Cruise.LowFrequency == 0.f && Cruise.HighFrequency < 0.06f);

	// Understeer: front tyres past the plateau buzz the high-frequency motor.
	FOsdsDriveTelemetry Push = Coast;
	Push.Wheels[0].SlipAngleRad = Push.Wheels[1].SlipAngleRad = 1.5 * PlateauSlipAngleRad(Car);
	const FOsdsRumble Under = Model.Sample(Push, 0.0, 0.0, 1.0 / 60.0);
	TestTrue(TEXT("understeer is the weak motor"), Under.HighFrequency > 0.6f && Under.LowFrequency == 0.f);

	// Oversteer and wheelspin: the rear, low-frequency motor and the right trigger.
	FOsdsDriveTelemetry Spin = Coast;
	Spin.Wheels[2].bDriven = Spin.Wheels[3].bDriven = true;
	Spin.Wheels[2].SlipRatio = Spin.Wheels[3].SlipRatio = 0.25;
	const FOsdsRumble Over = Model.Sample(Spin, 1.0, 0.0, 1.0 / 60.0);
	TestTrue(TEXT("wheelspin is the strong motor"), Over.LowFrequency > 0.6f && Over.HighFrequency < 0.06f);
	TestTrue(TEXT("wheelspin in the throttle trigger"), Over.RightTrigger > 0.4f && Over.LeftTrigger == 0.f);

	// Parked after a slide: a relaxed slip angle left on the tyres is not
	// scrub, but wheelspin from standstill is felt.
	FOsdsDriveTelemetry Parked = Coast;
	Parked.BodyVelocityMps = FVector(0.05, -0.05, 0.0);
	for (FOsdsDriveWheelTelemetry& Wheel : Parked.Wheels) { Wheel.SlipAngleRad = 0.6; }
	FOsdsHapticsModel ParkedModel;
	TestTrue(TEXT("a stopped car does not scrub"), ParkedModel.Sample(Parked, 0.0, 0.0, 1.0 / 60.0).IsQuiet());
	FOsdsDriveTelemetry Launch = Parked;
	for (FOsdsDriveWheelTelemetry& Wheel : Launch.Wheels) { Wheel.SlipAngleRad = 0.0; }
	Launch.Wheels[2].bDriven = Launch.Wheels[3].bDriven = true;
	Launch.Wheels[2].SlipRatio = Launch.Wheels[3].SlipRatio = 0.5;
	TestTrue(TEXT("wheelspin from standstill is felt"), ParkedModel.Sample(Launch, 1.0, 0.0, 1.0 / 60.0).LowFrequency > 0.6f);

	// ABS acting: the pulse, in the strong motor and the brake trigger.
	FOsdsDriveTelemetry Abs = Coast;
	Abs.bServiceBrakeOn = true;
	for (FOsdsDriveWheelTelemetry& Wheel : Abs.Wheels) { Wheel.bAbsActive = true; Wheel.SlipRatio = -0.1; }
	FOsdsHapticsModel AbsModel;
	const FOsdsRumble Pulse = AbsModel.Sample(Abs, 0.0, 1.0, 0.01);
	TestTrue(TEXT("ABS pulse felt"), AbsModel.IsAbsActive() && Pulse.LowFrequency >= 0.6f && Pulse.LeftTrigger >= 0.6f);

	// Backends: XInput drives max(Large) left and max(Small) right; GameInput has four motors.
	FOsdsRumble R;
	R.LowFrequency = 0.8f; R.HighFrequency = 0.3f; R.LeftTrigger = 0.5f; R.RightTrigger = 0.2f;
	const FForceFeedbackValues X = ToChannels(R, EOsdsRumbleBackend::XInput);
	TestTrue(TEXT("XInput low motor"), FMath::Max(X.LeftLarge, X.RightLarge) == 0.8f);
	TestTrue(TEXT("XInput high motor"), FMath::Max(X.LeftSmall, X.RightSmall) == 0.3f);
	const FForceFeedbackValues GI = ToChannels(R, EOsdsRumbleBackend::GameInput);
	TestTrue(TEXT("GameInput four motors"), GI.LeftLarge == 0.8f && GI.RightLarge == 0.3f && GI.LeftSmall == 0.5f && GI.RightSmall == 0.2f);
	return true;
}

namespace OsdsGameplayTest
{
	/** Put a wheel on a kerb: its flag where the build has one, and the fallback's rough road and a quick suspension jump. */
	template <typename TWheel>
	void PutOnKerb(TWheel& Wheel, double TravelJumpM)
	{
		if constexpr (requires { Wheel.bKerb; }) Wheel.bKerb = true;
		Wheel.SuspensionTravelM += TravelJumpM;
	}

	/** The wheel's surface where the build has the field (the grip says it otherwise). */
	template <typename TWheel>
	void SetSurface(TWheel& Wheel, uint8 Surface)
	{
		if constexpr (requires { Wheel.SurfaceKind; }) Wheel.SurfaceKind = Surface;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsHapticsSurfaceTest, "OSDS.Gameplay.Haptics.Surfaces", Flags)
bool FOsdsHapticsSurfaceTest::RunTest(const FString& Parameters)
{
	// Rumble you can read the road by: ABS pulses at 15-20 Hz, slip from 80 % of the peak, a kerb
	// is a short sharp pulse on its side's motor, gravel a coarse random low rumble, concrete a
	// fine joint tick, grass a soft wobble, asphalt almost nothing, the air nothing. Checked on the
	// channels the pad gets (ToChannels, XInput: LeftLarge is the left motor, LeftSmall the right).
	using namespace OsdsHaptics;
	const FOsdsCarDefinition Car;

	// ABS: count the pulses in one second.
	int32 Rises = 0;
	bool bWas = false;
	for (int32 Ms = 0; Ms < 1000; ++Ms)
	{
		const bool bOn = AbsPulse(true, Ms / 1000.0) > 0.0;
		Rises += bOn && !bWas ? 1 : 0;
		bWas = bOn;
	}
	TestTrue(FString::Printf(TEXT("ABS pulses at 15-20 Hz (%d a second)"), Rises), Rises >= 15 && Rises <= 20);

	// Slip: nothing below 80 % of the peak, felt above it.
	TestEqual(TEXT("slip at 79 % of the peak is not felt"), SlipFeel(0.79), 0.0);
	TestTrue(TEXT("slip at 90 % of the peak is felt"), SlipFeel(0.9) > 0.0);

	// Surfaces at 15 m/s over two seconds.
	auto Texture = [](uint8 Surface, double& MaxLow, double& MaxHigh, double& MinLow, int32& Ticks)
	{
		MaxLow = MaxHigh = 0.0; MinLow = 1e9; Ticks = 0;
		bool bHigh = false;
		for (int32 Ms = 0; Ms < 2000; Ms += 5)
		{
			double Low = 0.0, High = 0.0;
			SurfaceTexture(Surface, 15.0, Ms / 1000.0, Low, High);
			MaxLow = FMath::Max(MaxLow, Low); MaxHigh = FMath::Max(MaxHigh, High); MinLow = FMath::Min(MinLow, Low);
			const bool bTick = High > 0.2;
			Ticks += bTick && !bHigh ? 1 : 0;
			bHigh = bTick;
		}
	};
	double Lo, Hi, MinLo; int32 Ticks;
	Texture(Asphalt, Lo, Hi, MinLo, Ticks);
	TestTrue(FString::Printf(TEXT("asphalt: a light hum only (low %.2f, high %.2f)"), Lo, Hi), Lo == 0.0 && Hi > 0.0 && Hi < 0.06);
	Texture(Concrete, Lo, Hi, MinLo, Ticks);
	TestTrue(FString::Printf(TEXT("concrete: a fine joint tick on the high motor, about one per 4.5 m (%d in 30 m)"), Ticks), Lo == 0.0 && Ticks >= 6 && Ticks <= 7);
	const double ConcreteHigh = Hi;
	Texture(Gravel, Lo, Hi, MinLo, Ticks);
	TestTrue(FString::Printf(TEXT("gravel: a coarse random low rumble (low %.2f..%.2f)"), MinLo, Lo), Lo > 0.25 && Lo - MinLo > 0.1);
	const double GravelLow = Lo;
	Texture(Grass, Lo, Hi, MinLo, Ticks);
	TestTrue(FString::Printf(TEXT("grass: a soft low wobble, softer than gravel (low %.2f)"), Lo), Lo > 0.05 && Lo < GravelLow && Hi == 0.0);
	Texture(Air, Lo, Hi, MinLo, Ticks);
	TestTrue(TEXT("air: nothing"), Lo == 0.0 && Hi == 0.0);
	double StillLow = 0.0, StillHigh = 0.0;
	for (uint8 S = Asphalt; S <= Dirt; ++S) SurfaceTexture(S, 0.0, 0.3, StillLow, StillHigh);
	TestTrue(TEXT("no surface texture at a standstill"), StillLow == 0.0 && StillHigh == 0.0);
	TestTrue(TEXT("concrete ticks harder than the asphalt hum"), ConcreteHigh > 0.2);

	// A car rolling at 15 m/s on asphalt.
	FOsdsDriveTelemetry Road = OsdsGameplayTest::Moving(15.0);
	for (FOsdsDriveWheelTelemetry& Wheel : Road.Wheels)
	{
		Wheel.bContact = true;
		Wheel.NormalLoadN = 2500.0;
		Wheel.SurfaceGrip = 1.0;
		Wheel.PeakSlipAngleRad = PlateauSlipAngleRad(Car);
	}
	const double Dt = 1.0 / 60.0;

	// A kerb under the left front wheel: a short sharp pulse on the left motor only.
	{
		FOsdsHapticsModel Model;
		Model.Sample(Road, 0.0, 0.0, Dt);
		FOsdsDriveTelemetry Kerb = Road;
		Kerb.RoadRoughness = 1.0;
		OsdsGameplayTest::PutOnKerb(Kerb.Wheels[0], 0.03);
		const FForceFeedbackValues Hit = ToChannels(Model.Sample(Kerb, 0.0, 0.0, Dt), EOsdsRumbleBackend::XInput);
		TestTrue(FString::Printf(TEXT("left kerb: the left motor pulses (%.2f), the right only has the rough road's texture (%.2f)"), Hit.LeftLarge, Hit.LeftSmall), Hit.LeftLarge >= 0.7f && Hit.LeftSmall < 0.3f);
		FOsdsRumble After;
		for (int32 F = 0; F < 6; ++F) After = Model.Sample(Kerb, 0.0, 0.0, Dt);
		TestTrue(FString::Printf(TEXT("left kerb: the pulse is short (%.2f after 0.1 s on it)"), After.LowFrequency), After.LowFrequency < 0.1f);
	}
	// And under the right front wheel: the right motor.
	{
		FOsdsHapticsModel Model;
		Model.Sample(Road, 0.0, 0.0, Dt);
		FOsdsDriveTelemetry Kerb = Road;
		Kerb.RoadRoughness = 1.0;
		OsdsGameplayTest::PutOnKerb(Kerb.Wheels[1], 0.03);
		const FForceFeedbackValues Hit = ToChannels(Model.Sample(Kerb, 0.0, 0.0, Dt), EOsdsRumbleBackend::XInput);
		TestTrue(FString::Printf(TEXT("right kerb: the right motor pulses (%.2f), the left stays quiet (%.2f)"), Hit.LeftSmall, Hit.LeftLarge), Hit.LeftSmall >= 0.7f && Hit.LeftLarge < 0.1f);
	}
	// A rough road with no wheel jumping is no kerb (the fallback needs both).
	{
		FOsdsHapticsModel Model;
		FOsdsDriveTelemetry Rough = Road;
		Rough.RoadRoughness = 1.0;
		Model.Sample(Rough, 0.0, 0.0, Dt);
		const FOsdsRumble R = Model.Sample(Rough, 0.0, 0.0, Dt);
		TestTrue(FString::Printf(TEXT("rough road, steady wheels: no kerb pulse (low %.2f)"), R.LowFrequency), R.LowFrequency < 0.1f);
	}
	// Loose ground under all four wheels (the grip of a verge): the coarse low rumble.
	{
		FOsdsHapticsModel Model;
		FOsdsDriveTelemetry Loose = Road;
		for (FOsdsDriveWheelTelemetry& Wheel : Loose.Wheels)
		{
			Wheel.SurfaceGrip = 0.7;
			OsdsGameplayTest::SetSurface(Wheel, Gravel);
		}
		double MaxLow = 0.0;
		for (int32 F = 0; F < 60; ++F) MaxLow = FMath::Max(MaxLow, static_cast<double>(ToChannels(Model.Sample(Loose, 0.0, 0.0, Dt), EOsdsRumbleBackend::XInput).LeftLarge));
		TestTrue(FString::Printf(TEXT("loose ground: the low motor rumbles (%.2f)"), MaxLow), MaxLow > 0.2);
	}
	// ABS on the channels: both main motors' low one pulses while ABS acts.
	{
		FOsdsHapticsModel Model;
		FOsdsDriveTelemetry Abs = Road;
		Abs.bServiceBrakeOn = true;
		for (FOsdsDriveWheelTelemetry& Wheel : Abs.Wheels) { Wheel.bAbsActive = true; Wheel.SlipRatio = -0.1; }
		int32 On = 0, Off = 0;
		for (int32 F = 0; F < 100; ++F)
		{
			const float L = ToChannels(Model.Sample(Abs, 0.0, 1.0, 0.005), EOsdsRumbleBackend::XInput).LeftLarge;
			(L >= 0.6f ? On : Off) += 1;
		}
		TestTrue(FString::Printf(TEXT("ABS: the left motor pulses on and off (%d on, %d off in 0.5 s)"), On, Off), On > 20 && Off > 20);
	}
	// In the air: no rumble at all, even with the engine working.
	{
		FOsdsHapticsModel Model;
		FOsdsDriveTelemetry Air = Road;
		Air.EngineLoad = 1.0;
		for (FOsdsDriveWheelTelemetry& Wheel : Air.Wheels) Wheel.bContact = false;
		const FForceFeedbackValues V = ToChannels(Model.Sample(Air, 1.0, 0.0, Dt), EOsdsRumbleBackend::XInput);
		TestTrue(TEXT("in the air: every channel is still"), V.LeftLarge == 0.f && V.LeftSmall == 0.f && V.RightLarge == 0.f && V.RightSmall == 0.f);
	}
	return true;
}

// --- Cameras ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsHeadModelTest, "OSDS.Gameplay.Camera.HeadModel", Flags)
bool FOsdsHeadModelTest::RunTest(const FString& Parameters)
{
	const FOsdsVehicleSetup Setup = Roadster();
	const double G = OsdsShaping::StandardGravity;
	const double Dt = 1.0 / 60.0;

	// Steady 0.5 g left turn: the head leans out to the right, 1.5 cm.
	{
		FOsdsHeadModel Head;
		FVector Velocity(20.0, 0.0, 0.0);
		for (int32 I = 0; I < 180; ++I)
		{
			Velocity += FVector(0.0, -0.5 * G, 0.0) * Dt;
			Head.Update(FTransform::Identity, Velocity, Setup, Moving(20.0), Dt);
		}
		TestNearlyEqual(TEXT("3 cm per g lateral"), Head.GetHeadOffsetM().Y, 0.015, 0.0005);
	}
	// 0.8 g braking: the head moves 1.6 cm forward and the gaze dips 1.2 deg.
	{
		FOsdsHeadModel Head;
		FVector Velocity(30.0, 0.0, 0.0);
		for (int32 I = 0; I < 120; ++I)
		{
			Velocity += FVector(-0.8 * G, 0.0, 0.0) * Dt;
			Head.Update(FTransform::Identity, Velocity, Setup, Moving(Velocity.X), Dt);
		}
		TestNearlyEqual(TEXT("2 cm per g longitudinal"), Head.GetHeadOffsetM().X, 0.016, 0.0005);
		TestNearlyEqual(TEXT("brake dip"), Head.GetBrakePitchDeg(), 1.2, 0.02);
	}
	// Horizon: a 10 deg body roll shows as 7 deg.
	{
		FOsdsHeadModel Head;
		Head.HeadMotion = 0.0;
		const FTransform Rolled(FRotator(0.0, 0.0, 10.0));
		const FOsdsEyePose Pose = Head.Update(Rolled, FVector::ZeroVector, Setup, Moving(0.0), Dt);
		TestNearlyEqual(TEXT("70 % of the roll"), Pose.ToRotator().Roll, 7.0, 0.05);
		TestTrue(TEXT("head motion 0 welds the eye"), Pose.Location.Equals(Rolled.TransformPosition(Setup.DriverEyeLocalCm), 1e-6));
	}
	// Lens: natural 45 deg vertical is 72.9 deg across a 16:9 screen.
	TestNearlyEqual(TEXT("natural lens"), OsdsLens::HorizontalFromVerticalDeg(OsdsLens::VerticalFovDeg(EOsdsLens::Natural, 0, 0), 16.0 / 9.0), 72.74, 0.05);
	TestNearlyEqual(TEXT("screen lens, 15.6 in at 60 cm"), OsdsLens::VerticalFovDeg(EOsdsLens::Screen, 19.4, 60.0), 18.37, 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsChaseFrameTest, "OSDS.Gameplay.Camera.ChaseShowsSlip", Flags)
bool FOsdsChaseFrameTest::RunTest(const FString& Parameters)
{
	bool bHold = false;
	const double Beta = FMath::DegreesToRadians(15.0);
	const FVector Drift(20.0, 20.0 * FMath::Tan(Beta), 0.0);
	TestNearlyEqual(TEXT("swing is 0.4 beta"), FOsdsChaseFrame::TargetSwingRad(Drift, bHold), 0.4 * Beta, 1e-9);
	TestNearlyEqual(TEXT("beta clamps at 35 deg"), FOsdsChaseFrame::TargetSwingRad(FVector(10.0, 30.0, 0.0), bHold), 0.4 * FMath::DegreesToRadians(35.0), 1e-9);
	TestEqual(TEXT("reversing never swings"), FOsdsChaseFrame::TargetSwingRad(FVector(-5.0, 0.5, 0.0), bHold), 0.0);
	TestFalse(TEXT("reversing does not hold"), bHold);
	FOsdsChaseFrame::TargetSwingRad(FVector(-1.0, 8.0, 0.0), bHold);
	TestTrue(TEXT("a spin holds the last swing"), bHold);
	TestEqual(TEXT("walking pace: no swing"), FOsdsChaseFrame::TargetSwingRad(FVector(1.5, 1.0, 0.0), bHold), 0.0);

	FOsdsChaseFrame Frame;
	FVector Location;
	FVector LookAt;
	Frame.Update(EOsdsView::Chase, FTransform::Identity, FVector(20.0, 0.0, 0.0), 1.0 / 60.0, true, Location, LookAt);
	TestNearlyEqual(TEXT("chase behind"), Location.X, -580.0, 1e-6);
	TestNearlyEqual(TEXT("chase height"), Location.Z, 194.0, 1e-6);
	for (int32 I = 0; I < 180; ++I)
	{
		Frame.Update(EOsdsView::Chase, FTransform::Identity, Drift, 1.0 / 60.0, false, Location, LookAt);
	}
	TestNearlyEqual(TEXT("steady drift: camera yaw - body yaw = 0.4 beta"), Frame.GetSwingRad(), 0.4 * Beta, 1e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsHeadFrameRateTest, "OSDS.Gameplay.Camera.HeadModelFrameRate", Flags)
bool FOsdsHeadFrameRateTest::RunTest(const FString& Parameters)
{
	// The same 0.5 g turn must lean the head the same at 30, 60, 144 and
	// 360 fps: the felt acceleration is differentiated over the real frame.
	const FOsdsVehicleSetup Setup = Roadster();
	const double G = OsdsShaping::StandardGravity;
	for (const double Fps : {30.0, 60.0, 144.0, 360.0})
	{
		FOsdsHeadModel Head;
		FVector Velocity(20.0, 0.0, 0.0);
		const double Dt = 1.0 / Fps;
		const int32 Frames = FMath::RoundToInt(3.0 * Fps);
		for (int32 I = 0; I < Frames; ++I)
		{
			Velocity += FVector(0.0, -0.5 * G, 0.0) * Dt;
			Head.Update(FTransform::Identity, Velocity, Setup, Moving(20.0), Dt);
		}
		TestNearlyEqual(FString::Printf(TEXT("0.5 g lean at %.0f fps"), Fps), Head.GetHeadOffsetM().Y, 0.015, 0.0005);
		TestNearlyEqual(FString::Printf(TEXT("felt 0.5 g at %.0f fps"), Fps), Head.GetFeltSpecificForce().Y / G, -0.5, 0.01);
	}
	FOsdsGMeter Meter;
	FVector Velocity(20.0, 0.0, 0.0);
	for (int32 I = 0; I < 3 * 360; ++I)
	{
		Velocity += FVector(-0.8 * G, 0.0, 0.0) / 360.0;
		Meter.Update(FTransform::Identity, Velocity, 1.0 / 360.0);
	}
	TestNearlyEqual(TEXT("g-meter reads 0.8 g braking at 360 fps"), Meter.Get().X, -0.8, 0.01);
	const FVector2D Held = Meter.Get();
	Meter.Update(FTransform::Identity, Velocity, 0.0);
	TestTrue(TEXT("g-meter holds while paused"), Meter.Get().Equals(Held, 1e-9));
	return true;
}

/**
 * Release bug #4: the asynchronous 360 Hz physics step does not line up with the
 * frames, so a 60 fps frame sees 5, 6 or 7 new steps. Differentiating the velocity
 * over the frame time made the g-meter and the head model jitter by about 17 %;
 * over the physics time they read a steady value.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCameraPhysicsTimeTest, "OSDS.Gameplay.Camera.PhysicsTimeNoJitter", Flags)
bool FOsdsCameraPhysicsTimeTest::RunTest(const FString& Parameters)
{
	constexpr double G = 9.80665;
	constexpr double StepS = 1.0 / 360.0;
	FOsdsGMeter Meter;
	FVector Velocity(20.0, 0.0, 0.0);
	double SimTime = 1.0;
	static const int32 Pattern[] = {6, 5, 7, 6, 6, 5, 7, 0, 12};
	double MinG = 10.0, MaxG = -10.0;
	for (int32 I = 0; I < 360; ++I)
	{
		const int32 Steps = Pattern[I % UE_ARRAY_COUNT(Pattern)];
		Velocity += FVector(-0.8 * G, 0.0, 0.0) * Steps * StepS;
		SimTime += Steps * StepS;
		const FVector2D Reading = Meter.Update(FTransform::Identity, Velocity, 1.0 / 60.0, SimTime);
		if (I > 120)
		{
			MinG = FMath::Min(MinG, Reading.X);
			MaxG = FMath::Max(MaxG, Reading.X);
		}
	}
	AddInfo(FString::Printf(TEXT("g-meter with uneven physics steps per frame: %.4f .. %.4f g"), MinG, MaxG));
	TestNearlyEqual(TEXT("g-meter reads 0.8 g braking"), (MinG + MaxG) * 0.5, -0.8, 0.01);
	TestTrue(TEXT("g-meter steady (spread under 0.01 g)"), MaxG - MinG < 0.01);
	return true;
}

// --- Shell and vehicle together ---------------------------------------------

namespace OsdsGameplayTest
{
	struct FSlideResult
	{
		double PeakBodySlipDeg = 0.0;
		double FinalBodySlipDeg = 0.0;
		double PeakRackBeyondCapDeg = 0.0;
		double SteadyYawRate = 0.0;
		double SteadyLateralSpeed = 0.0;
		bool bSpun = false;
	};

	/** How the scripted driver answers a slide with the stick, toward the
	 *  front axle's travel beta_f. Align: aim the wheels along beta_f
	 *  (stick = beta_f / (|beta_f| + surplus)). Snap: full lock from 6 deg. */
	enum class ECatch : uint8 { Align, Snap };

	/** Drive the placeholder through the real shaper at 120 Hz frames: reach
	 *  20 m/s, turn right, flick the parking brake 0.15 s, then catch the
	 *  slide. bCapped clamps the rack to the speed cap, the pre-allowance
	 *  behaviour. */
	FSlideResult DriveSlide(UWorld* World, const FOsdsCarDefinition& Car, bool bCapped, ECatch Catch)
	{
		FSlideResult Result;
		AOsdsPlaceholderVehicle* Vehicle = World->SpawnActor<AOsdsPlaceholderVehicle>();
		Vehicle->SetCarDefinition(Car);
		Vehicle->ResetVehicle(FTransform::Identity);
		const FOsdsVehicleSetup Setup = Vehicle->GetVehicleSetup();
		FOsdsDriverShaper Shaper;
		FOsdsDriveTelemetry T;
		constexpr double Frame = 1.0 / 120.0;
		auto Step = [&](const FOsdsRawControls& Raw)
		{
			Vehicle->GetVehicleTelemetry(T);
			FOsdsDriverCommand Command = Shaper.Update(Setup, T, Raw, Frame);
			const double Cap = OsdsShaping::AvailableSteeringRad(Setup, T.BodyVelocityMps.X);
			if (bCapped)
			{
				Command.RackAngleRad = FMath::Clamp(Command.RackAngleRad, -Cap, Cap);
			}
			Result.PeakRackBeyondCapDeg = FMath::Max(Result.PeakRackBeyondCapDeg, FMath::RadiansToDegrees(FMath::Abs(Command.RackAngleRad) - Cap));
			Vehicle->ApplyDriverCommand(Command);
			Vehicle->StepPhysics(Frame * 0.5);
			Vehicle->StepPhysics(Frame * 0.5);
		};
		FOsdsRawControls Raw;
		Raw.PadThrottle = 1.0;
		for (int32 I = 0; I < 20 * 120 && Vehicle->GetForwardSpeedMps() < 20.0; ++I) { Step(Raw); }
		Raw.PadThrottle = 0.15;
		Raw.PadSteer = 0.5;
		for (int32 I = 0; I < 120; ++I) { Step(Raw); }
		Result.SteadyYawRate = Vehicle->GetYawRateRadPerS();
		Result.SteadyLateralSpeed = Vehicle->GetLateralSpeedMps();
		Raw.PadThrottle = 0.0;
		Raw.bParkingBrake = true;
		for (int32 I = 0; I < 18; ++I) { Step(Raw); }
		Raw.bParkingBrake = false;
		for (int32 I = 0; I < 3 * 120; ++I)
		{
			const double BetaFront = OsdsShaping::FrontAxleSlipAngleRad(Setup, Vehicle->GetForwardSpeedMps(), Vehicle->GetLateralSpeedMps(), Vehicle->GetYawRateRadPerS());
			Raw.PadSteer = Catch == ECatch::Snap
				? FMath::Clamp(BetaFront / FMath::DegreesToRadians(6.0), -1.0, 1.0)
				: BetaFront / (FMath::Abs(BetaFront) + Setup.SteeringSlipLimitSurplusRad);
			Step(Raw);
			const double BodySlip = FMath::RadiansToDegrees(FMath::Atan2(Vehicle->GetLateralSpeedMps(), FMath::Abs(Vehicle->GetForwardSpeedMps())));
			Result.PeakBodySlipDeg = FMath::Max(Result.PeakBodySlipDeg, FMath::Abs(BodySlip));
			Result.bSpun |= Vehicle->GetForwardSpeedMps() < 0.0 || FMath::Abs(BodySlip) > 90.0;
		}
		Result.FinalBodySlipDeg = FMath::RadiansToDegrees(FMath::Atan2(Vehicle->GetLateralSpeedMps(), FMath::Abs(Vehicle->GetForwardSpeedMps())));
		Vehicle->Destroy();
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsSlideCatchTest, "OSDS.Gameplay.Loop.PlaceholderConventionsAndSlideCatch", Flags)
bool FOsdsSlideCatchTest::RunTest(const FString& Parameters)
{
	FOsdsCarDefinition Car;
	FString Error;
	if (!FOsdsCarDefinitionLoader::LoadById(TEXT("nb1999"), Car, Error))
	{
		AddWarning(FString::Printf(TEXT("nb1999.json not loaded, using the built-in NB defaults: %s"), *Error));
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("OsdsSlideCatch"));
	const FSlideResult Free = DriveSlide(World, Car, false, ECatch::Align);
	const FSlideResult Capped = DriveSlide(World, Car, true, ECatch::Align);
	const FSlideResult Snap = DriveSlide(World, Car, false, ECatch::Snap);
	const FSlideResult SnapCapped = DriveSlide(World, Car, true, ECatch::Snap);
	World->DestroyWorld(false);

	// Conventions across the seam: the stick right turns the car right (yaw
	// rate positive in Unreal body axes); the shell and the vehicle agree.
	TestTrue(TEXT("stick right yaws right"), Free.SteadyYawRate > 0.05);
	AddInfo(FString::Printf(TEXT("Steady right turn at 20 m/s: yaw rate %.3f rad/s, lateral speed %.3f m/s"), Free.SteadyYawRate, Free.SteadyLateralSpeed));
	auto Report = [this](const TCHAR* Name, const FSlideResult& R)
	{
		AddInfo(FString::Printf(TEXT("%s: peak body slip %.1f deg, final %.1f deg, rack up to %.1f deg past the speed cap, spun %s"),
			Name, R.PeakBodySlipDeg, R.FinalBodySlipDeg, R.PeakRackBeyondCapDeg, R.bSpun ? TEXT("yes") : TEXT("no")));
	};
	Report(TEXT("Align the wheels with the travel, allowance"), Free);
	Report(TEXT("Align the wheels with the travel, rack held to the speed cap"), Capped);
	Report(TEXT("Snap to full lock, allowance"), Snap);
	Report(TEXT("Snap to full lock, rack held to the speed cap"), SnapCapped);
	// The allowance must actually open toward the slide in the loop.
	// A gentle catch never needs it; a snap to full lock opens it (and, on
	// this uncalibrated placeholder, overcorrects into a spin the capped rack
	// prevents: the plan 04 SlideCatch gate belongs to the real vehicle).
	TestTrue(TEXT("a full-lock countersteer goes past the speed cap"), Snap.PeakRackBeyondCapDeg > 1.0);
	TestTrue(TEXT("the capped runs stay within the cap"), Capped.PeakRackBeyondCapDeg < 1e-6 && SnapCapped.PeakRackBeyondCapDeg < 1e-6);
	return true;
}

// --- HUD --------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsHudRulesTest, "OSDS.Gameplay.Hud.Levels", Flags)
bool FOsdsHudRulesTest::RunTest(const FString& Parameters)
{
	using namespace OsdsHudRules;
	TestTrue(TEXT("Off -> Glance"), Next(EOsdsHudLevel::Off) == EOsdsHudLevel::Glance);
	TestTrue(TEXT("Glance -> Full"), Next(EOsdsHudLevel::Glance) == EOsdsHudLevel::Full);
	TestTrue(TEXT("Full -> Off"), Next(EOsdsHudLevel::Full) == EOsdsHudLevel::Off);
	TestEqual(TEXT("reverse"), GearText(-1, false), FString(TEXT("R")));
	TestEqual(TEXT("neutral"), GearText(0, true), FString(TEXT("N")));
	TestEqual(TEXT("automatic third"), GearText(3, false), FString(TEXT("D3")));
	TestEqual(TEXT("manual third"), GearText(3, true), FString(TEXT("3")));
	float Alpha = 1.f;
	for (int32 I = 0; I < 120; ++I) { Alpha = GlanceAlpha(Alpha, false, 1.f / 60.f); }
	TestNearlyEqual(TEXT("glance fades while cruising"), Alpha, GlanceFaded, 0.02f);
	for (int32 I = 0; I < 120; ++I) { Alpha = GlanceAlpha(Alpha, true, 1.f / 60.f); }
	TestNearlyEqual(TEXT("and wakes"), Alpha, 1.f, 0.02f);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsSeatRangeTest, "OSDS.Gameplay.Camera.SeatRange", Flags)
bool FOsdsSeatRangeTest::RunTest(const FString& Parameters)
{
	// The seat & view menu keeps the eye in the cabin and the view angle sane.
	FOsdsSeat Seat;
	TestTrue(TEXT("a new seat is the car's own"), Seat.IsDefault());
	Seat.UpCm = 40.0;
	Seat.ForwardCm = -40.0;
	Seat.PitchDeg = 30.0;
	Seat.FovDeg = 120.0;
	Seat.Clamp();
	TestEqual(TEXT("height within 10 cm"), Seat.UpCm, FOsdsSeat::MaxUpCm);
	TestEqual(TEXT("fore-aft within 15 cm"), Seat.ForwardCm, -FOsdsSeat::MaxForwardCm);
	TestEqual(TEXT("tilt within 8 deg"), Seat.PitchDeg, FOsdsSeat::MaxPitchDeg);
	TestEqual(TEXT("view angle at most 75 deg tall"), Seat.FovDeg, FOsdsSeat::MaxFovDeg);
	Seat.FovDeg = -3.0;
	Seat.Clamp();
	TestEqual(TEXT("no view angle is the lens's"), Seat.FovDeg, 0.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
