// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/SimCruiseTrees.h"

// The regional conifer share each crown's own look then moves (SimCruiseTrees.cpp): conifer
// forests in the Pacific Northwest, broadleaf in the low deserts and most of the East.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseTreeConiferShareTest, "SimCruise.World.Trees.ConiferShare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruiseTreeConiferShareTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Oregon Cascades are conifer forest"), ASimCruiseTrees::ConiferShare(45.5, -122.0, 300.0) >= 0.85);
	TestTrue(TEXT("Phoenix is mesquite and palo verde"), ASimCruiseTrees::ConiferShare(33.45, -112.07, 340.0) <= 0.15);
	TestTrue(TEXT("Las Vegas is desert broadleaf"), ASimCruiseTrees::ConiferShare(36.17, -115.14, 610.0) <= 0.15);
	TestTrue(TEXT("Flagstaff (2100 m) is ponderosa pine"), ASimCruiseTrees::ConiferShare(35.2, -111.65, 2100.0) >= 0.85);
	TestTrue(TEXT("Ohio is mostly broadleaf"), ASimCruiseTrees::ConiferShare(40.0, -83.0, 250.0) <= 0.2);
	return true;
}

#endif
