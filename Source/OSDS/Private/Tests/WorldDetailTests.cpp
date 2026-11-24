// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Presentation/OsdsCarClock.h"
#include "Presentation/OsdsPresentationProfile.h"
#include "Presentation/OsdsLowBeamPattern.h"
#include "World/OsdsRoadNetwork.h"
#include "World/OsdsSurfaceKnowledge.h"
#include "World/OsdsTileDetail.h"

namespace
{
	constexpr EAutomationTestFlags WorldDetailTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsLowBeamPatternTest, "OSDS.Presentation.LowBeamPattern", WorldDetailTestFlags)
bool FOsdsLowBeamPatternTest::RunTest(const FString& Parameters)
{
	using OsdsLowBeamPattern::Relative;
	// The hot spot sits just under the cut-off, slightly to the curb side.
	TestTrue(TEXT("hot spot near full"), Relative(-1.4, 2.5) > 0.95);
	// Oncoming side: a sharp horizontal cut-off 0.57 degrees down.
	TestTrue(TEXT("oncoming side lit under the cut-off"), Relative(-1.0, -5.0) > 0.5);
	TestTrue(TEXT("oncoming side dark above it (glare under 2 %)"), Relative(0.0, -5.0) < 0.02 && Relative(1.0, -5.0) < 0.02);
	// Curb side: the cut-off rises at 15 degrees, so signs at the right are lit.
	TestTrue(TEXT("curb side lit above the horizon past the kink"), Relative(0.5, 6.0) > 0.4);
	TestTrue(TEXT("under 1 % high above the road"), Relative(5.0, 0.0) < 0.01 && Relative(5.0, 10.0) < 0.01);
	// The foreground is dimmer than the throw, so the near road does not dazzle the driver.
	TestTrue(TEXT("foreground dimmer than the hot spot"), Relative(-15.0, 0.0) < 0.5 * Relative(-1.4, 2.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsLaneFrameTest, "OSDS.World.LaneFrame", WorldDetailTestFlags)
bool FOsdsLaneFrameTest::RunTest(const FString& Parameters)
{
	using namespace OsdsLanes;
	// Ocean Drive: one 3.2 m lane between two parking lanes, centred: the lane spans u 0..1 around L = 0.
	const FParking Ocean = Parking(TEXT("tertiary"), 5.0, 1, true, true);
	const FLaneFrame O = LaneFrame(TEXT("tertiary"), 5.0, 1, Ocean);
	TestEqual(TEXT("one lane"), O.Count, 1);
	TestEqual(TEXT("lane width"), O.WidthM, 3.2, 1e-9);
	TestEqual(TEXT("lane centre at u 0.5"), O.U(0.0), 0.5, 1e-9);
	TestTrue(TEXT("parking is outside 0..Count"), O.U(-4.0) < 0.0 && O.U(4.0) > static_cast<double>(O.Count));
	// A two-way residential street, 6.4 m wide, no parking: one lane each way, the centre line at u = 1.
	const FLaneFrame R = LaneFrame(TEXT("residential"), 3.2, 0, FParking());
	TestEqual(TEXT("two lanes across"), R.Count, 2);
	TestEqual(TEXT("centre line between the lanes"), R.U(0.0), 1.0, 1e-9);
	// A two-way primary road, 21.6 m: two lanes each way, the centre line at u = 2.
	const FLaneFrame P = LaneFrame(TEXT("primary"), 10.8, 0, FParking());
	TestEqual(TEXT("four lanes across"), P.Count, 4);
	TestEqual(TEXT("centre line at u 2"), P.U(0.0), 2.0, 1e-9);
	// Parking on the -R side only moves the one-way lane toward +R, whichever way it runs.
	FParking Left; Left.Left = 2.4;
	for (int32 Travel : { 1, -1 })
	{
		const FLaneFrame F = LaneFrame(TEXT("residential"), 3.2, Travel, Left);
		TestTrue(*FString::Printf(TEXT("lane centre sits toward +R (travel %d)"), Travel), F.U(1.2 / 1.0) > 0.0 && F.OriginM > -F.WidthM);
		TestEqual(*FString::Printf(TEXT("its centre is (parking) 1.2 m off the carriageway centre (travel %d)"), Travel), F.U(1.2), 0.5 * F.Count, 1e-9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsRoadSurfaceTableTest, "OSDS.World.RoadSurfaceKnowledge", WorldDetailTestFlags)
bool FOsdsRoadSurfaceTableTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("data/knowledge/surface/road.json loads"), OsdsSurface::TableLoaded());
	TestTrue(TEXT("a motorway wears more than a service road"), OsdsSurface::ClassWear(TEXT("motorway")) > OsdsSurface::ClassWear(TEXT("service")));
	float Centre = 0.f, Half = 0.f, Oil = 0.f;
	OsdsSurface::WheelPathM(Centre, Half, Oil);
	TestTrue(TEXT("wheel paths sit between the lane centre and the lane line, the oil strip inside them"), Centre > Oil && Centre + Half < 1.6f && Half > 0.f);
	TestTrue(TEXT("a pool of several textures"), OsdsSurface::PoolTextures().Num() >= 4);
	// Stable for a seed, different across seeds, biased by the climate.
	const OsdsSurface::FRoadPlace A = OsdsSurface::RoadPlace(1234u, 0.f), A2 = OsdsSurface::RoadPlace(1234u, 0.f);
	TestTrue(TEXT("stable per seed"), A.Age == A2.Age && A.AlbedoScale == A2.AlbedoScale && A.PoolSeed == A2.PoolSeed);
	TSet<int32> Ages;
	for (uint32 Seed = 1; Seed <= 64; ++Seed)
	{
		const OsdsSurface::FRoadPlace P = OsdsSurface::RoadPlace(Seed * 2654435761u, 0.f);
		TestTrue(TEXT("age in range"), P.Age >= 0.f && P.Age <= 1.f && P.PoolSeed >= 0.f && P.PoolSeed < 1.f);
		Ages.Add(FMath::FloorToInt(P.Age * 10.f));
	}
	TestTrue(TEXT("tiles differ in age"), Ages.Num() >= 6);
	TestTrue(TEXT("a dry climate reads lighter"), OsdsSurface::RoadPlace(77u, 1.f).AlbedoScale > OsdsSurface::RoadPlace(77u, 0.f).AlbedoScale);
	const OsdsSurface::FRoadPlace X = OsdsSurface::RoadPlace(3u, 0.f), Y = OsdsSurface::RoadPlace(4u, 0.f);
	TestTrue(TEXT("older roads read lighter (oxidised binder)"), (X.Age < Y.Age) == (X.AlbedoScale < Y.AlbedoScale));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsParkingLayoutTest, "OSDS.World.ParkingLayout", WorldDetailTestFlags)
bool FOsdsParkingLayoutTest::RunTest(const FString& Parameters)
{
	using namespace OsdsLanes;
	// Ocean Drive: a 10 m one-way tertiary street with built-up edges parks on both sides.
	const FParking Ocean = Parking(TEXT("tertiary"), 5.0, 1, true, true);
	TestEqual(TEXT("Ocean Drive parks left"), Ocean.Left, 2.4);
	TestEqual(TEXT("Ocean Drive parks right"), Ocean.Right, 2.4);
	// Its single lane is centred between the parking lanes.
	TestEqual(TEXT("one lane, centred"), RightLaneCentreM(TEXT("tertiary"), 5.0, 1, Ocean), 0.0, 1e-9);
	// Narrow one-way: one side, the driver's right first, else the other curb.
	const FParking NarrowRight = Parking(TEXT("residential"), 3.0, 1, true, true);
	TestTrue(TEXT("6 m one-way parks on the driver's right"), NarrowRight.Right == 2.4 && NarrowRight.Left == 0.0);
	const FParking NarrowLeft = Parking(TEXT("residential"), 3.0, 1, true, false);
	TestTrue(TEXT("6 m one-way with only a left curb parks there"), NarrowLeft.Left == 2.4 && NarrowLeft.Right == 0.0);
	// Two-way: both sides or none.
	TestEqual(TEXT("11 m two-way parks both sides"), Parking(TEXT("residential"), 5.5, 0, true, true).Left, 2.4);
	TestEqual(TEXT("9 m two-way has no room"), Parking(TEXT("residential"), 4.5, 0, true, true).Left, 0.0);
	// Arterials and rural roads never.
	TestEqual(TEXT("primary never"), Parking(TEXT("primary"), 8.0, 0, true, true).Right, 0.0);
	TestEqual(TEXT("rural never"), Parking(TEXT("residential"), 6.0, 0, false, false).Right, 0.0);
	// Without parking the lane layout is the old one.
	TestEqual(TEXT("unchanged without parking"), RightLaneCentreM(TEXT("residential"), 5.0, 0), RightLaneCentreM(TEXT("residential"), 5.0, 0, FParking()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsRegionalDetailTest, "OSDS.World.RegionalDetail", WorldDetailTestFlags)
bool FOsdsRegionalDetailTest::RunTest(const FString& Parameters)
{
	using namespace OsdsTileDetail;
	TestTrue(TEXT("Ocean Drive at 8th Street is Art Deco"), StyleForPlace(25.7780, -80.1312) == EBuildingStyle::MiamiBeachDeco);
	TestTrue(TEXT("Downtown Miami is not"), StyleForPlace(25.7743, -80.1937) == EBuildingStyle::Generic);
	TestTrue(TEXT("Telluride is not"), StyleForPlace(37.9374, -107.8119) == EBuildingStyle::Generic);
	TestTrue(TEXT("coconut palms in Miami Beach"), IsCoconutPalmRegion(25.78, -80.13));
	TestTrue(TEXT("and Key West"), IsCoconutPalmRegion(24.555, -81.78));
	TestTrue(TEXT("and Honolulu"), IsCoconutPalmRegion(21.28, -157.83));
	TestFalse(TEXT("not Orlando"), IsCoconutPalmRegion(28.54, -81.38));
	TestFalse(TEXT("not Oatman"), IsCoconutPalmRegion(35.04, -114.37));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCarClockTest, "OSDS.Presentation.CarClock", WorldDetailTestFlags)
bool FOsdsCarClockTest::RunTest(const FString& Parameters)
{
	using namespace OsdsCarClock;
	auto At = [](const TCHAR* Iso) { FDateTime T; FDateTime::ParseIso8601(Iso, T); return T; };
	TestEqual(TEXT("Miami Beach, November: EST"), Format12(LocalMinutes(At(TEXT("2026-11-11T16:00:00Z")), 25.78, -80.13)), FString(TEXT("11:00")));
	TestEqual(TEXT("Miami Beach, July: EDT"), Format12(LocalMinutes(At(TEXT("2026-07-01T16:00:00Z")), 25.78, -80.13)), FString(TEXT("12:00")));
	TestEqual(TEXT("Chicago, January: CST"), Format12(LocalMinutes(At(TEXT("2026-01-15T18:30:00Z")), 41.88, -87.63)), FString(TEXT("12:30")));
	TestEqual(TEXT("Phoenix keeps MST in summer"), Format12(LocalMinutes(At(TEXT("2026-07-01T19:05:00Z")), 33.45, -112.07)), FString(TEXT("12:05")));
	TestEqual(TEXT("Los Angeles, July: PDT"), Format12(LocalMinutes(At(TEXT("2026-07-01T19:05:00Z")), 34.05, -118.24)), FString(TEXT("12:05")));
	TestEqual(TEXT("Honolulu: HST, no DST"), Format12(LocalMinutes(At(TEXT("2026-07-01T22:00:00Z")), 21.31, -157.86)), FString(TEXT("12:00")));
	TestEqual(TEXT("Telluride in winter: MST"), Format12(LocalMinutes(At(TEXT("2026-02-01T07:00:00Z")), 37.94, -107.81)), FString(TEXT("12:00")));
	TestEqual(TEXT("DST starts on the second Sunday of March"), Format12(LocalMinutes(At(TEXT("2026-03-08T07:30:00Z")), 25.78, -80.13)), FString(TEXT("3:30")));
	TestEqual(TEXT("midnight shows 12"), Format12(0), FString(TEXT("12:00")));
	// The NB's radio display shares the clock's texture and shows its station.
	FOsdsCarPresentationProfile Nb;
	FString Error;
	if (TestTrue(TEXT("nb2001 profile loads"), OsdsPresentationProfiles::Load(TEXT("nb2001"), Nb, Error)) && TestTrue(TEXT("nb2001 has a clock"), Nb.Clock.IsSet()))
	{
		TestNearlyEqual(TEXT("the radio is tuned to 91.3 FM"), Nb.Clock->RadioMHz, 91.3, 1e-6);
	}
	return true;
}

#endif
