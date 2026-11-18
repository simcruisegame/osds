// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/OsdsViewCheck.h"

namespace OsdsViewCheckTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	void Set(FOsdsViewStats& S, EOsdsViewHit Hit, float Percent) { S.Percent[static_cast<int32>(Hit)] = Percent; }

	/** A street: road ahead, buildings to the side, a textured image. */
	FOsdsViewStats Street()
	{
		FOsdsViewStats S;
		S.RaysBelowHorizon = 600;
		Set(S, EOsdsViewHit::Road, 45.f);
		Set(S, EOsdsViewHit::Building, 20.f);
		Set(S, EOsdsViewHit::Ground, 20.f);
		Set(S, EOsdsViewHit::Tree, 10.f);
		Set(S, EOsdsViewHit::Empty, 5.f);
		S.NearestRoadM = 4.f;
		S.BuildingCells = 30;
		S.bPlaceHasRoads = true;
		S.bPlaceHasBuildings = true;
		S.bHasImage = true;
		S.Edges = 8.f;
		S.LumaStdDev = 40.f;
		S.HueBins = 9;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsViewCheckJudgeTest, "OSDS.Gameplay.ViewCheck.Judge", OsdsViewCheckTest::Flags)
bool FOsdsViewCheckJudgeTest::RunTest(const FString& Parameters)
{
	using namespace OsdsViewCheckTest;
	TestEqual(TEXT("a street passes"), OsdsViewCheckRules::Judge(Street()), FString());

	// The drive robot's pad: every ray below the horizon on an unknown slab, no road, no building.
	FOsdsViewStats Pad = Street();
	for (float& P : Pad.Percent) { P = 0.f; }
	Set(Pad, EOsdsViewHit::Other, 100.f);
	Pad.NearestRoadM = -1.f;
	Pad.BuildingCells = 0;
	const FString Reasons = OsdsViewCheckRules::Judge(Pad);
	TestTrue(TEXT("the pad fails on the world share"), Reasons.Contains(TEXT("world-")));
	TestTrue(TEXT("the pad fails on the road"), Reasons.Contains(TEXT("no-road")));
	TestTrue(TEXT("the pad fails on buildings"), Reasons.Contains(TEXT("no-building")));

	FOsdsViewStats Countryside = Street();
	Countryside.bPlaceHasBuildings = false;
	Countryside.BuildingCells = 0;
	TestEqual(TEXT("no buildings are asked for where the map has none"), OsdsViewCheckRules::Judge(Countryside), FString());

	FOsdsViewStats Flat = Street();
	Flat.Edges = 0.5f;
	TestTrue(TEXT("a flat image fails"), OsdsViewCheckRules::Judge(Flat).Contains(TEXT("flat-image")));

	FOsdsViewStats NoShot = Street();
	NoShot.bHasImage = false;
	TestTrue(TEXT("a missing screenshot fails"), OsdsViewCheckRules::Judge(NoShot).Contains(TEXT("no-screenshot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsViewCheckImageTest, "OSDS.Gameplay.ViewCheck.Image", OsdsViewCheckTest::Flags)
bool FOsdsViewCheckImageTest::RunTest(const FString& Parameters)
{
	constexpr int32 W = 64, H = 40;
	TArray<FColor> Grey;
	Grey.Init(FColor(128, 128, 128), W * H);
	FOsdsViewStats Flat;
	OsdsViewCheckRules::MeasureImage(W, H, Grey, Flat);
	TestTrue(TEXT("measured"), Flat.bHasImage);
	TestEqual(TEXT("a grey image has no edges"), Flat.Edges, 0.f);
	TestEqual(TEXT("a grey image has no spread"), Flat.LumaStdDev, 0.f);
	TestEqual(TEXT("a grey image has no hue"), Flat.HueBins, 0);

	// Black and white columns, and red, green and blue rows in the lower part.
	TArray<FColor> Busy;
	Busy.SetNum(W * H);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			static const FColor Rgb[3] = { FColor(200, 30, 30), FColor(30, 200, 30), FColor(30, 30, 200) };
			Busy[Y * W + X] = (X % 2 == 0) ? Rgb[Y % 3] : FColor::Black;
		}
	}
	FOsdsViewStats Edgy;
	OsdsViewCheckRules::MeasureImage(W, H, Busy, Edgy);
	TestTrue(TEXT("stripes have strong edges"), Edgy.Edges > 20.f);
	TestEqual(TEXT("three hues"), Edgy.HueBins, 3);

	TestEqual(TEXT("slug"), OsdsViewCheckRules::Slug(TEXT("Ocean Drive, Miami Beach")), FString(TEXT("ocean-drive-miami-beach")));
	TestEqual(TEXT("empty slug"), OsdsViewCheckRules::Slug(TEXT("  ")), FString(TEXT("place")));
	return true;
}

#endif
