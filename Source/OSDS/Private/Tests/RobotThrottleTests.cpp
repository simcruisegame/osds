// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/OsdsRobotThrottle.h"

namespace
{
	constexpr EAutomationTestFlags RobotThrottleFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	FOsdsRobotThrottleInput Cruise()
	{
		FOsdsRobotThrottleInput In;
		In.SpeedErrorMps = 10.0;
		In.PreviousThrottle = 1.0;
		In.DeltaSeconds = 0.02;
		return In;
	}
}

// Bug #102: wheelspin at ratio 2.8 on gravel with full throttle spun the Miata.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsRobotThrottleLawTest, "OSDS.Vehicle.RobotThrottle.Law", RobotThrottleFlags)
bool FOsdsRobotThrottleLawTest::RunTest(const FString&)
{
	// Paved, straight, grippy: the old full pedal for a big speed gap.
	TestTrue(TEXT("paved straight, big gap: full pedal"), OsdsRobotThrottle::Compute(Cruise()) > 0.99);
	// No gap: no pedal.
	FOsdsRobotThrottleInput Hold = Cruise();
	Hold.SpeedErrorMps = -1.0;
	TestEqual(TEXT("no gap: lifted"), OsdsRobotThrottle::Compute(Hold), 0.0);
	// Gravel caps the pedal.
	FOsdsRobotThrottleInput Gravel = Cruise();
	Gravel.SurfaceGrip = 0.7;
	TestTrue(TEXT("gravel: below 0.9"), OsdsRobotThrottle::Compute(Gravel) < 0.9);
	TestTrue(TEXT("gravel: still above 0.6 (not timid)"), OsdsRobotThrottle::Compute(Gravel) > 0.6);
	// Wheelspin lifts hard.
	FOsdsRobotThrottleInput Spin = Cruise();
	Spin.DrivenSlipRatio = 2.8;
	TestTrue(TEXT("wheelspin 2.8: under a quarter"), OsdsRobotThrottle::Compute(Spin) < 0.25);
	Spin.DrivenSlipRatio = 0.1;
	TestTrue(TEXT("slip at the peak: no lift"), OsdsRobotThrottle::Compute(Spin) > 0.99);
	// Rear slide lifts.
	FOsdsRobotThrottleInput Slide = Cruise();
	Slide.RearSlipAngleDeg = -7.5;
	TestEqual(TEXT("rear slide 7.5 deg: off"), OsdsRobotThrottle::Compute(Slide), 0.0);
	// Steering reversal: no full pedal at full stick.
	FOsdsRobotThrottleInput Lock = Cruise();
	Lock.Stick = -1.0;
	TestTrue(TEXT("full stick: at most 0.4"), OsdsRobotThrottle::Compute(Lock) <= 0.401);
	// A foot opens gradually: from 0 at 20 ms the pedal is 0.03.
	FOsdsRobotThrottleInput Start = Cruise();
	Start.PreviousThrottle = 0.0;
	TestEqual(TEXT("opens at 1.5 per second"), OsdsRobotThrottle::Compute(Start), 0.03, 1e-6);
	return true;
}

#endif
