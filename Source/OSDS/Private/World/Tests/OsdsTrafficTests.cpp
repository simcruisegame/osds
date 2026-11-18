// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "World/OsdsRoadNetwork.h"
#include "World/OsdsTraffic.h"
#include "World/OsdsGeo.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OsdsTrafficTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	/** A straight ribbon along +X, HalfW wide, with curbs (built-up) on both sides. */
	FOsdsRibbon Straight(const FString& Class, double HalfW, int8 Travel)
	{
		FOsdsRibbon R;
		R.Class = Class;
		for (int32 I = 0; I < 3; ++I)
		{
			FOsdsRibbonStation St;
			St.C = FVector2d(I * 20.0, 0.0);
			St.T = FVector2d(1.0, 0.0);
			St.R = FVector2d(0.0, 1.0);
			St.S = I * 20.0;
			St.HalfW = HalfW;
			St.Travel = Travel;
			R.St.Add(St);
			for (FOsdsRibbonSide& Side : R.Sides)
			{
				Side.Edge.Add(EOsdsRoadEdge::Kerb);
				Side.SidewalkM.Add(2.0f);
				Side.Cut.Add(0);
			}
		}
		return R;
	}

	/** Follows a leader at constant speed for Seconds; returns the final gap (m). */
	double Follow(double LeaderMps, double Seconds, double& OutSpeed)
	{
		double V = 0.0, Gap = 30.0;
		for (double T = 0.0; T < Seconds; T += 0.02)
		{
			const double A = OsdsTrafficModel::IdmAcceleration(V, 15.6, Gap, V - LeaderMps);
			V = FMath::Max(0.0, V + A * 0.02);
			Gap += (LeaderMps - V) * 0.02;
		}
		OutSpeed = V;
		return Gap;
	}
}

using namespace OsdsTrafficTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTrafficModelTest, "OSDS.World.Traffic", OsdsTrafficTests::Flags)
bool FOsdsTrafficModelTest::RunTest(const FString& Parameters)
{
	// Free road: pulls away at 1.4 m/s^2 and settles at the desired speed.
	TestNearlyEqual(TEXT("free road from rest accelerates at 1.4 m/s^2"), OsdsTrafficModel::IdmAcceleration(0.0, 15.6, -1.0, 0.0), 1.4, 1e-6);
	TestNearlyEqual(TEXT("no acceleration at the desired speed"), OsdsTrafficModel::IdmAcceleration(15.6, 15.6, -1.0, 0.0), 0.0, 1e-6);
	// A stopped car 20 m ahead at 15 m/s: brake hard.
	TestTrue(TEXT("brakes for a stopped car ahead"), OsdsTrafficModel::IdmAcceleration(15.0, 15.6, 20.0, 15.0) < -4.0);

	// Behind a leader at 10 m/s it settles at that speed, a little over 2 m + 1.4 s behind.
	double V = 0.0;
	const double Gap = Follow(10.0, 60.0, V);
	AddInfo(FString::Printf(TEXT("following at 10 m/s: %.2f m/s, gap %.1f m"), V, Gap));
	TestTrue(TEXT("matches the leader's speed"), FMath::Abs(V - 10.0) < 0.1);
	TestTrue(FString::Printf(TEXT("keeps about a 1.4 s gap (%.1f m)"), Gap), Gap > 2.0 + 14.0 && Gap < 2.0 + 14.0 + 8.0);
	// Behind a stopped car it stops, at least 2 m short, never through it.
	const double Stop = Follow(0.0, 40.0, V);
	TestTrue(FString::Printf(TEXT("stops behind a stopped car (%.2f m/s, gap %.2f m)"), V, Stop), V < 0.05 && Stop > 1.5);

	// Lanes: a two-way residential street keeps traffic on the right, each way.
	const FOsdsRibbon Two = Straight(TEXT("residential"), 6.0, 0);
	const double Along = OsdsTrafficModel::LaneOffsetM(Two, 1, 1), Against = OsdsTrafficModel::LaneOffsetM(Two, 1, -1);
	AddInfo(FString::Printf(TEXT("two-way 12 m street: %.2f m along, %.2f m against"), Along, Against));
	TestTrue(TEXT("along the street: right of the centre line"), Along > 0.5);
	TestTrue(TEXT("against it: the other side"), Against < -0.5);
	TestNearlyEqual(TEXT("the two directions mirror each other"), Along, -Against, 1e-6);
	// One way, 10 m with parking both sides: one lane, in the middle.
	const FOsdsRibbon One = Straight(TEXT("secondary"), 5.0, -1);
	const double OneWay = OsdsTrafficModel::LaneOffsetM(One, 1, -1);
	TestTrue(FString::Printf(TEXT("one way, one lane: centred (%.2f)"), OneWay), FMath::Abs(OneWay) < 0.3);
	// One way, 14 m: two lanes; driving against S the right-hand one lies toward -R.
	const FOsdsRibbon Wide = Straight(TEXT("secondary"), 7.0, -1);
	const double WideWay = OsdsTrafficModel::LaneOffsetM(Wide, 1, -1);
	TestTrue(FString::Printf(TEXT("one way, two lanes: the right lane toward -R (%.2f)"), WideWay), WideWay < -0.5 && WideWay > -7.0);

	// Junctions along a ribbon: a crossing road cuts stations 3-4 of a 0..100 m street.
	{
		FOsdsRibbon Street;
		Street.Class = TEXT("residential");
		for (int32 I = 0; I <= 10; ++I)
		{
			FOsdsRibbonStation St;
			St.C = FVector2d(I * 10.0, 0.0);
			St.S = I * 10.0;
			St.HalfW = 5.0;
			Street.St.Add(St);
			for (FOsdsRibbonSide& Side : Street.Sides)
			{
				Side.Edge.Add(EOsdsRoadEdge::Kerb);
				Side.SidewalkM.Add(2.0f);
				Side.Cut.Add(I == 3 || I == 4 ? 1 : 0);
			}
		}
		bool bEnd = false;
		const double Ahead = OsdsTrafficModel::NextDecisionS(Street, 1, 5.0, -1e9, bEnd);
		TestTrue(FString::Printf(TEXT("the junction's centre ahead (%.1f m)"), Ahead), !bEnd && FMath::IsNearlyEqual(Ahead, 35.0));
		const double Past = OsdsTrafficModel::NextDecisionS(Street, 1, 36.0, 35.0, bEnd);
		TestTrue(FString::Printf(TEXT("past it, the street's end (%.1f m)"), Past), bEnd && FMath::IsNearlyEqual(Past, 100.0));
		const double Back = OsdsTrafficModel::NextDecisionS(Street, -1, 90.0, -1e9, bEnd);
		TestTrue(FString::Printf(TEXT("driving the other way: the same junction (%.1f m)"), Back), !bEnd && FMath::IsNearlyEqual(Back, 35.0));
	}

	// A wide two-way avenue (22 m, no parking: a primary road) has two lanes each way; the
	// second lies one lane width nearer the centre line, and a lane past the last is the last.
	{
		const FOsdsRibbon Avenue = Straight(TEXT("primary"), 11.0, 0);
		const int32 Lanes = OsdsTrafficModel::LaneCount(Avenue, 1, 1);
		const double Right = OsdsTrafficModel::LaneOffsetM(Avenue, 1, 1, 0), Inner = OsdsTrafficModel::LaneOffsetM(Avenue, 1, 1, 1);
		AddInfo(FString::Printf(TEXT("22 m avenue: %d lanes each way, right lane %.2f m, inner lane %.2f m"), Lanes, Right, Inner));
		TestTrue(TEXT("two or more lanes each way"), Lanes >= 2);
		TestTrue(TEXT("the inner lane nearer the centre line, still on the right half"), Inner > 0.5 && Inner < Right - 2.5);
		TestNearlyEqual(TEXT("against the avenue: mirrored"), OsdsTrafficModel::LaneOffsetM(Avenue, 1, -1, 1), -Inner, 1e-6);
		TestNearlyEqual(TEXT("a lane past the last is the last"), OsdsTrafficModel::LaneOffsetM(Avenue, 1, 1, 9), OsdsTrafficModel::LaneOffsetM(Avenue, 1, 1, Lanes - 1), 1e-6);
		TestEqual(TEXT("a narrow residential street: one lane each way"), OsdsTrafficModel::LaneCount(Straight(TEXT("residential"), 6.0, 0), 1, 1), 1);
	}

	TestNearlyEqual(TEXT("residential streets at 25 mph"), OsdsTrafficModel::SpeedLimitMps(TEXT("residential")), 11.2, 0.05);
	TestNearlyEqual(TEXT("motorways at 65 mph"), OsdsTrafficModel::SpeedLimitMps(TEXT("motorway")), 29.1, 0.05);
	return true;
}

/**
 * A waiting car takes a shorter gap the longer it waits, but never in front of the player; and it
 * turns round only at a true dead end, never at the edge of a tile (where the road may go on in a
 * tile not built yet: cars made U-turns there in the middle of the road).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTrafficPatienceTest, "OSDS.World.Traffic.PatienceAndDeadEnds", OsdsTrafficTests::Flags)
bool FOsdsTrafficPatienceTest::RunTest(const FString& Parameters)
{
	for (const double Waited : { 0.0, 9.0, 16.0, 25.0, 120.0 })
	{
		TestEqual(FString::Printf(TEXT("left turn, player, waited %.0f s: the full gap"), Waited), OsdsTrafficModel::GapWindowS(true, Waited, true), 4.5);
		TestEqual(FString::Printf(TEXT("stop sign, player, waited %.0f s: the full gap"), Waited), OsdsTrafficModel::GapWindowS(false, Waited, true), 5.0);
	}
	TestEqual(TEXT("left turn, traffic, fresh: 4.5 s"), OsdsTrafficModel::GapWindowS(true, 0.0, false), 4.5);
	TestEqual(TEXT("left turn, traffic, after 16 s: 1.5 s"), OsdsTrafficModel::GapWindowS(true, 16.0, false), 1.5);
	TestEqual(TEXT("stop sign, traffic, after 25 s: 2 s"), OsdsTrafficModel::GapWindowS(false, 25.0, false), 2.0);

	FOsdsWorldFrame Frame;
	Frame.OriginTile = FIntPoint(18000, 27000);
	Frame.TileSizeM = 600.0;
	const FVector Corner = Frame.TileOriginCm(Frame.OriginTile);
	const FVector2d Size = FVector2d(Frame.TileOriginCm(Frame.OriginTile + FIntPoint(1, 1)) - Corner);
	const FVector Middle = Corner + FVector(Size.X * 0.5, Size.Y * 0.5, 0.0);
	const FVector2d East(FMath::Sign(Size.X), 0.0);
	TestTrue(TEXT("a road ending in the middle of its tile is a dead end"), OsdsTrafficModel::IsTrueDeadEnd(Frame, Middle, East));
	const FVector NearEdge = Corner + FVector(Size.X - FMath::Sign(Size.X) * 1000.0, Size.Y * 0.5, 0.0);
	TestFalse(TEXT("a road ending 10 m from its tile's edge, heading out: not a dead end"), OsdsTrafficModel::IsTrueDeadEnd(Frame, NearEdge, East));
	const FVector NearSide = Corner + FVector(Size.X * 0.5, Size.Y - FMath::Sign(Size.Y) * 1000.0, 0.0);
	TestFalse(TEXT("a road ending 10 m from its tile's side: not a dead end"), OsdsTrafficModel::IsTrueDeadEnd(Frame, NearSide, East));
	TestFalse(TEXT("no world frame: never"), OsdsTrafficModel::IsTrueDeadEnd(FOsdsWorldFrame(), Middle, East));
	// A motorway the data draws crossing a street at grade: traffic keeps off it. A ramp joining
	// it is no crossing, nor a street on a bridge that clears it.
	auto Road = [](const FString& Class, const FVector2d& From, const FVector2d& To)
	{
		FOsdsRibbon R = OsdsTrafficTests::Straight(Class, 6.0, 0);
		for (int32 I = 0; I < R.St.Num(); ++I)
		{
			const double F = I / double(R.St.Num() - 1);
			R.St[I].C = FMath::Lerp(From, To, F);
			R.St[I].S = F * (To - From).Size();
		}
		return R;
	};
	TArray<FOsdsRibbon> Ribbons;
	Ribbons.Add(Road(TEXT("motorway"), FVector2d(0.0, 0.0), FVector2d(200.0, 0.0)));
	Ribbons.Add(Road(TEXT("motorway_link"), FVector2d(80.0, 40.0), FVector2d(100.0, 0.0)));
	TestFalse(TEXT("a ramp joining the motorway is no crossing"), OsdsTrafficModel::CrossesAtGrade(Ribbons, 0));
	Ribbons.Add(Road(TEXT("residential"), FVector2d(120.0, 60.0), FVector2d(120.0, 2.0)));
	TestFalse(TEXT("a street ending at the motorway's edge is no crossing"), OsdsTrafficModel::CrossesAtGrade(Ribbons, 0));
	Ribbons.Add(Road(TEXT("primary"), FVector2d(150.0, -60.0), FVector2d(150.0, 60.0)));
	TestTrue(TEXT("a street drawn across the motorway at grade (I-75 under Woodward)"), OsdsTrafficModel::CrossesAtGrade(Ribbons, 0));
	for (FOsdsRibbonStation& St : Ribbons.Last().St) St.bBridge = true;
	TestTrue(TEXT("a mapped bridge drawn without clearance (Woodward over I-75) still crosses at grade"), OsdsTrafficModel::CrossesAtGrade(Ribbons, 0));
	for (FOsdsRibbonStation& St : Ribbons.Last().St) St.Z += 6.0;
	TestFalse(TEXT("the same street on a bridge 6 m above"), OsdsTrafficModel::CrossesAtGrade(Ribbons, 0));
	TestFalse(TEXT("only motorways are judged"), OsdsTrafficModel::CrossesAtGrade(Ribbons, 3));
	return true;
}
#endif
