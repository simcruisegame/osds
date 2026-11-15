// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruisePreflight.h"

namespace SimCruisePreflightTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** A good drive: 60 s, 800 m, on the road, clean pictures. */
	FSimCruisePreflightStats Good()
	{
		FSimCruisePreflightStats S;
		S.DriveSeconds = 60.f;
		S.DistanceM = 800.f;
		S.MeanSpeedMps = 13.f;
		S.Frames = 590;
		S.SkySamples = 4000;
		S.SkyWhitePercent = 3.f;
		S.bViewCheckRan = true;
		S.bViewCheckPassed = true;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePreflightJudgeTest, "SimCruise.Gameplay.Preflight.Judge", SimCruisePreflightTest::Flags)
bool FSimCruisePreflightJudgeTest::RunTest(const FString& Parameters)
{
	using namespace SimCruisePreflightTest;
	TestEqual(TEXT("a good drive passes"), SimCruisePreflightRules::Judge(Good()), FString());

	auto Fails = [this](const TCHAR* What, const FSimCruisePreflightStats& S, const TCHAR* Reason)
	{
		TestTrue(What, SimCruisePreflightRules::Judge(S).Contains(Reason));
	};
	FSimCruisePreflightStats S = Good(); S.SkyWhitePercent = 60.f; Fails(TEXT("white sky"), S, TEXT("white-sky"));
	S = Good(); S.TilePopIns = 1; Fails(TEXT("a tile built in front of the car"), S, TEXT("tile-pop-in"));
	S = Good(); S.CarPopIns = 3; Fails(TEXT("cars appearing"), S, TEXT("car-pop-in"));
	S = Good(); S.RoadSteps = 1; S.MaxStepCm = 12.f; Fails(TEXT("a step in the road"), S, TEXT("road-steps"));
	S = Good(); S.RoadGaps = 1; Fails(TEXT("a gap in the road"), S, TEXT("road-gaps"));
	S = Good(); S.BorderJolts = 1; Fails(TEXT("a jolt at a tile border"), S, TEXT("jolts-at-tile-borders"));
	// Stuck traffic is a note now (traffic is off for v2.0.3), not a FAIL.
	S = Good(); S.StuckTraffic = 2; TestTrue(TEXT("stuck traffic is noted"), SimCruisePreflightRules::Notes(S).Contains(TEXT("stuck-traffic-2")));
	S = Good(); S.OffRoadSeconds = 5.f; Fails(TEXT("off the road"), S, TEXT("left-the-road"));
	S = Good(); S.bFellBack = true; Fails(TEXT("fell back to Miami Beach"), S, TEXT("fell-back"));
	S = Good(); S.bViewCheckPassed = false; S.ViewCheckReasons = TEXT("a,b"); Fails(TEXT("the view check failed"), S, TEXT("viewcheck-fail(a+b)"));
	S = Good(); S.DistanceM = 20.f; Fails(TEXT("the robot did not drive"), S, TEXT("robot-moved-only"));
	S = Good(); S.Frames = 40; Fails(TEXT("the recording failed"), S, TEXT("few-frames"));

	S = Good(); S.CarPopIns = 2; S.Jolts = 4; S.SkySamples = 10; S.SkyWhitePercent = 90.f;
	TestEqual(TEXT("two car pop-ins, jolts away from borders and too few sky samples pass"), SimCruisePreflightRules::Judge(S), FString());

	// Car control and frame times.
	S = Good(); S.PeakRearSlipDeg = 40.f; Fails(TEXT("a spin (rear slip)"), S, TEXT("spin-rear-slip-40deg"));
	S = Good(); S.ReversedSeconds = 1.f; Fails(TEXT("a slide backwards"), S, TEXT("spin-"));
	S = Good(); S.PeakRearSlipDeg = 40.f; S.bForcedSpin = true; Fails(TEXT("the forced spin is named"), S, TEXT("-forced"));
	S = Good(); S.OffRoadSeconds = 1.5f; Fails(TEXT("1.5 s off the surface"), S, TEXT("left-the-road"));
	S = Good(); S.MaxOffRibbonM = 6.f; S.OffRibbonSeconds = 2.f; Fails(TEXT("a run-off past the edge"), S, TEXT("ran-off-6.0m"));
	S = Good(); S.TimedFrames = 2000; S.FrameP95Ms = 80.f; Fails(TEXT("slow frames"), S, TEXT("slow-frames-p95-80ms"));
	S = Good(); S.TimedFrames = 2000; S.Hitches = 9; Fails(TEXT("hitches"), S, TEXT("hitches-9"));
	S = Good(); S.PeakRearSlipDeg = 9.f; S.ReversedSeconds = 0.2f; S.OffRoadSeconds = 0.2f; S.MaxOffRibbonM = 2.5f; S.OffRibbonSeconds = 0.3f;
	S.TimedFrames = 2000; S.FrameP95Ms = 20.f; S.Hitches = 2;
	TestEqual(TEXT("a lively but controlled drive passes"), SimCruisePreflightRules::Judge(S), FString());
	S = Good(); S.TimedFrames = 50; S.FrameP95Ms = 200.f; S.Hitches = 40;
	TestEqual(TEXT("too few timed frames are not judged"), SimCruisePreflightRules::Judge(S), FString());

	TArray<float> Ms;
	for (int32 I = 0; I < 100; ++I) Ms.Add(I < 94 ? 16.7f : (I < 97 ? 60.f : 150.f));
	FSimCruisePreflightStats F;
	SimCruisePreflightRules::MeasureFrames(Ms, F);
	TestEqual(TEXT("timed frames"), F.TimedFrames, 100);
	TestEqual(TEXT("p95 of 94 fast, 3 slow, 3 hitches"), F.FrameP95Ms, 60.f);
	TestEqual(TEXT("over 50 ms"), F.SlowFrames, 6);
	TestEqual(TEXT("over 100 ms"), F.Hitches, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePreflightStepsTest, "SimCruise.Gameplay.Preflight.Steps", SimCruisePreflightTest::Flags)
bool FSimCruisePreflightStepsTest::RunTest(const FString& Parameters)
{
	// A 4 % grade with a crest, sampled every 0.4 m: no step.
	TArray<FVector2f> Smooth;
	for (int32 I = 0; I < 200; ++I)
	{
		const float X = I * 0.4f;
		Smooth.Add(FVector2f(X, 0.04f * X - 0.0005f * X * X));
	}
	float MaxCm = 0.f;
	TestEqual(TEXT("a smooth road has no step"), SimCruisePreflightRules::CountSteps(Smooth, MaxCm), 0);
	TestTrue(TEXT("and the largest jump is small"), MaxCm < 1.f);

	// The same road with a 12 cm step at 40 m.
	TArray<FVector2f> Stepped = Smooth;
	for (FVector2f& P : Stepped)
	{
		if (P.X >= 40.f) P.Y += 0.12f;
	}
	TestEqual(TEXT("one step"), SimCruisePreflightRules::CountSteps(Stepped, MaxCm), 1);
	TestTrue(TEXT("of about 12 cm"), FMath::Abs(MaxCm - 12.f) < 1.f);

	// Samples far apart are not compared.
	TArray<FVector2f> Sparse = { FVector2f(0.f, 0.f), FVector2f(5.f, 0.3f), FVector2f(10.f, 0.f) };
	TestEqual(TEXT("samples 5 m apart are not judged"), SimCruisePreflightRules::CountSteps(Sparse, MaxCm), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePreflightFlashTest, "SimCruise.Gameplay.Preflight.Flashes", SimCruisePreflightTest::Flags)
bool FSimCruisePreflightFlashTest::RunTest(const FString& Parameters)
{
	TArray<float> Diffs;
	TArray<bool> Turning;
	for (int32 I = 0; I < 100; ++I)
	{
		Diffs.Add(4.f + (I % 3));
		Turning.Add(false);
	}
	float Median = 0.f;
	TestEqual(TEXT("an even drive has no flash"), SimCruisePreflightRules::CountFlashes(Diffs, Turning, Median), 0);
	TestEqual(TEXT("median"), Median, 5.f);
	Diffs[50] = 60.f;
	Diffs[70] = 60.f;
	Turning[70] = true;
	TestEqual(TEXT("one flash; the one while turning is not counted"), SimCruisePreflightRules::CountFlashes(Diffs, Turning, Median), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePreflightTrafficTest, "SimCruise.Gameplay.Preflight.Traffic", SimCruisePreflightTest::Flags)
bool FSimCruisePreflightTrafficTest::RunTest(const FString& Parameters)
{
	using namespace SimCruisePreflightRules;
	// Turn round: 180 deg in 2 s across the +-180 seam is one; a 90 deg junction turn in 3 s and a
	// slow 180 deg loop over 6 s are not.
	TArray<FVector2d> UTurn;
	for (int32 I = 0; I <= 20; ++I) UTurn.Add(FVector2d(I * 0.1, FMath::UnwindDegrees(170.0 + 9.0 * I)));
	TestTrue(TEXT("a U-turn in 2 s across the seam"), IsTurnRound(UTurn));
	TArray<FVector2d> Corner;
	for (int32 I = 0; I <= 30; ++I) Corner.Add(FVector2d(I * 0.1, -3.0 * I));
	TestFalse(TEXT("a right-angle turn at a junction"), IsTurnRound(Corner));
	TArray<FVector2d> Loop;
	for (int32 I = 0; I <= 60; ++I) Loop.Add(FVector2d(I * 0.1, FMath::UnwindDegrees(3.0 * I)));
	TestFalse(TEXT("a slow loop round a ramp"), IsTurnRound(Loop));

	// Cut-in: a car crossing 3 m from our bumper while we drive at 10 m/s; the same while we stand;
	// a car ahead in our lane, same heading, that we close on; a car changing into our lane from the side.
	TestTrue(TEXT("a crossing car within one car length"), IsCutIn(OwnHalfLengthM + 2.25 + 3.0, 0.5, 0.5, 90.0, 10.0, 4.5));
	TestFalse(TEXT("a crossing car while we wait at the line"), IsCutIn(OwnHalfLengthM + 2.25 + 3.0, 0.5, 0.5, 90.0, 0.0, 4.5));
	TestFalse(TEXT("the car ahead in our lane"), IsCutIn(OwnHalfLengthM + 2.25 + 3.0, 0.2, 0.2, 2.0, 10.0, 4.5));
	TestTrue(TEXT("a lane change into our lane, 2 m ahead"), IsCutIn(OwnHalfLengthM + 2.25 + 2.0, 1.2, 3.0, 8.0, 10.0, 4.5));
	TestFalse(TEXT("a crossing car 10 m ahead"), IsCutIn(OwnHalfLengthM + 2.25 + 10.0, 0.0, 0.0, 90.0, 10.0, 4.5));
	TestFalse(TEXT("a car in the next lane"), IsCutIn(OwnHalfLengthM + 2.25 + 2.0, 3.5, 3.5, 0.0, 10.0, 4.5));

	// Overlap of the bodies.
	TestTrue(TEXT("a car through ours"), Overlaps(1.0, 0.3, 4.5));
	TestFalse(TEXT("a car passing in the next lane"), Overlaps(0.0, 3.4, 4.5));
	TestFalse(TEXT("a car just in front"), Overlaps(5.0, 0.0, 4.5));

	// At grade: a motorway car across our road at our level; the same 7 m below (an overpass); a primary road.
	TestTrue(TEXT("motorway traffic across our road"), IsAtGradeMotorway(TEXT("motorway"), 20.0, 2.0, 0.3, 88.0));
	TestFalse(TEXT("motorway traffic under our bridge"), IsAtGradeMotorway(TEXT("motorway"), 20.0, 2.0, -7.0, 88.0));
	TestFalse(TEXT("a crossing street"), IsAtGradeMotorway(TEXT("primary"), 20.0, 2.0, 0.3, 88.0));
	TestFalse(TEXT("a motorway alongside"), IsAtGradeMotorway(TEXT("motorway"), 20.0, 6.0, 0.3, 5.0));

	// Traffic is information only (traffic is off for now): notes, never a FAIL.
	FSimCruisePreflightStats S = SimCruisePreflightTest::Good();
	S.TrafficCutIns = 1; S.TrafficOverlaps = 1; S.TrafficTurnRounds = 2; S.TrafficAtGrade = 1; S.StuckTraffic = 3;
	TestEqual(TEXT("traffic counts do not fail the run"), Judge(S), FString());
	TestTrue(TEXT("a cut-in is noted"), Notes(S).Contains(TEXT("traffic-cut-in-1")));
	TestTrue(TEXT("a car through ours is noted"), Notes(S).Contains(TEXT("traffic-through-our-car-1")));
	TestTrue(TEXT("turn-rounds are noted"), Notes(S).Contains(TEXT("traffic-turn-round-2")));
	TestTrue(TEXT("motorway at grade is noted"), Notes(S).Contains(TEXT("motorway-traffic-at-grade-1")));
	S = SimCruisePreflightTest::Good(); S.JunctionHoldAskedS = 10.f; S.JunctionHoldS = 0.f; TestTrue(TEXT("no stop line found is noted"), Notes(S).Contains(TEXT("junction-hold-0s-of-10s")));
	S = SimCruisePreflightTest::Good(); S.JunctionHoldAskedS = 10.f; S.JunctionHoldS = 10.f; TestEqual(TEXT("a held junction notes nothing"), Notes(S), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePreflightCarStateTest, "SimCruise.Gameplay.Preflight.CarState", SimCruisePreflightTest::Flags)
bool FSimCruisePreflightCarStateTest::RunTest(const FString& Parameters)
{
	using namespace SimCruisePreflightRules;
	// HIT: the Doraville crash, 28.6 to 0.4 m/s in 0.4 s; hard braking at 1.1 g over 0.5 s is not one.
	TArray<FVector2d> Crash = { FVector2d(30.3, 28.6), FVector2d(30.5, 14.0), FVector2d(30.7, 0.4) };
	TestTrue(TEXT("a crash is a hit"), MaxSpeedDrop(Crash) > HitDropMps);
	TArray<FVector2d> Brake;
	for (int32 I = 0; I <= 10; ++I) Brake.Add(FVector2d(I * 0.05, 25.0 - 1.1 * 9.81 * I * 0.05));
	TestFalse(TEXT("hard braking is not a hit"), MaxSpeedDrop(Brake) > HitDropMps);
	TArray<FVector2d> Slow = { FVector2d(0.0, 20.0), FVector2d(1.0, 10.0) };
	TestFalse(TEXT("a drop over a whole second is outside the window"), MaxSpeedDrop(Slow) > HitDropMps);

	// DECK NOT SMOOTH: a 6 deg pitch kink within 3 m; a steady 8 % grade (4.6 deg) over 50 m; a 3 deg roll change.
	TArray<FVector> Kink = { FVector(100.0, 0.0, 0.0), FVector(101.5, 3.0, 0.0), FVector(103.0, 6.0, 0.5) };
	TestTrue(TEXT("a hump on a deck"), MaxAngleChange(Kink) > DeckDeg);
	TArray<FVector> Grade;
	for (int32 I = 0; I <= 50; ++I) Grade.Add(FVector(I * 1.0, 4.6, 0.0));
	TestFalse(TEXT("a steady grade"), MaxAngleChange(Grade) > DeckDeg);
	TArray<FVector> Camber = { FVector(0.0, 0.0, 0.0), FVector(4.0, 0.0, 3.0) };
	TestFalse(TEXT("a camber change of 3 deg"), MaxAngleChange(Camber) > DeckDeg);
	TArray<FVector> Seam = { FVector(0.0, 179.0, 0.0), FVector(2.0, -179.0, 0.0) };
	TestFalse(TEXT("no false jump across +-180 deg"), MaxAngleChange(Seam) > DeckDeg);

	FSimCruisePreflightStats S = SimCruisePreflightTest::Good();
	S.StuckSeconds = 27.f; TestTrue(TEXT("27 s at the limit fails"), Judge(S).Contains(TEXT("stuck-at-rev-limit-27s")));
	S = SimCruisePreflightTest::Good(); S.StuckSeconds = 1.5f; TestEqual(TEXT("a 1.5 s launch at the limit passes"), Judge(S), FString());
	S = SimCruisePreflightTest::Good(); S.Hits = 1; TestTrue(TEXT("a hit fails"), Judge(S).Contains(TEXT("hit-1")));
	S = SimCruisePreflightTest::Good(); S.DeckJumps = 2; S.MaxDeckDeg = 9.f; TestTrue(TEXT("a warped deck fails"), Judge(S).Contains(TEXT("deck-not-smooth-2-max-9deg")));
	S = SimCruisePreflightTest::Good(); S.WheelsOffSeconds = 3.f; TestTrue(TEXT("wheels in the air fail"), Judge(S).Contains(TEXT("wheels-off-ground-3.0s")));
	// A slow look (10 km/h for 30 s) covers 48 m: it passes with its own least distance, a normal drive does not.
	S = SimCruisePreflightTest::Good(); S.DistanceM = 48.f; S.MinDistanceAskedM = 41.7f; TestEqual(TEXT("a slow look passes"), Judge(S), FString());
	S = SimCruisePreflightTest::Good(); S.DistanceM = 48.f; TestTrue(TEXT("a normal drive of 48 m fails"), Judge(S).Contains(TEXT("robot-moved-only-48m")));
	// The hang watchdog: the Detroit start's 312 s fails; a 15 s stall is a note; 5 s is nothing.
	S = SimCruisePreflightTest::Good(); S.LongestHangS = 312.f; TestTrue(TEXT("a 5-minute hang fails"), Judge(S).Contains(TEXT("hang-312s")));
	S = SimCruisePreflightTest::Good(); S.LongestHangS = 15.f; TestEqual(TEXT("a 15 s stall passes"), Judge(S), FString());
	TestTrue(TEXT("and is noted"), Notes(S).Contains(TEXT("game-thread-stall-15s")));
	S = SimCruisePreflightTest::Good(); S.LongestHangS = 5.f; TestEqual(TEXT("a 5 s stall is not noted"), Notes(S), FString());
	return true;
}

#endif
