// Copyright SimCruise contributors. All Rights Reserved.
//
// Road steps where a lower road joins a higher one (release tests 20261114-0725-53b290c and
// 20261114-0953-525b8db): a driveway or a ramp that overlaps the road it joins takes that road's
// surface, never another minor road's raised edge, and never a bridge over it.

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseRoadCheck.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseJunctionStepsTest, "SimCruise.World.JunctionSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruiseJunctionStepsTest::RunTest(const FString& Parameters)
{
	// The recorded tiles (data/test/tiles) and the joins the release test found steps at (tile metres).
	struct FCase { const TCHAR* Name; FIntPoint Id; double Lat; TArray<FVector2d> Joins; };
	const TArray<FCase> Cases = {
		{ TEXT("Welch, WV: driveways and Hobart Street at Court Street"), FIntPoint(17916, 25409), 37.433, { FVector2d(66.0, 290.6), FVector2d(39.1, 291.6) } },
		{ TEXT("Doraville, GA: motorway ramp merges"), FIntPoint(17429, 26202), 33.893, { FVector2d(393.2, 465.7), FVector2d(384.6, 477.1), FVector2d(389.6, 470.9) } },
	};
	for (const FCase& Case : Cases)
	{
		FSimCruiseTileData Data;
		FString Error;
		const FString Path = FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data"), TEXT("test"), TEXT("tiles"), FString::Printf(TEXT("%d_%d.json"), Case.Id.X, Case.Id.Y));
		if (!TestTrue(FString::Printf(TEXT("%s: the tile loads (%s)"), Case.Name, *Error), SimCruiseTileData::LoadFile(Path, Data, Error))) continue;
		const TSharedRef<FSimCruiseTileBuild> B = SimCruiseTileBuilder::Build(Data, SimCruiseGeo::TileSizeMetres(Case.Lat));
		const TArray<FSimCruiseRoadStep> Steps = SimCruiseRoadCheck::FindSteps(*B);
		// The limit: 2 cm (the step check's second difference, which takes the grade out).
		double Worst = 0.0;
		for (const FSimCruiseRoadStep& S : Steps)
		{
			for (const FVector2d& J : Case.Joins) if (FVector2d::Distance(S.PositionM, J) < 15.0) Worst = FMath::Max(Worst, S.JumpCm);
		}
		TestTrue(FString::Printf(TEXT("%s: worst step at the joins %.1f cm (limit 2)"), Case.Name, Worst), Worst <= SimCruiseRoadCheck::DefaultThresholdCm);
		// Under a bridge the road below keeps its own surface: no step where it passes under a deck
		// (a join that took the deck's surface would stand metres up).
		double WorstUnder = 0.0;
		int32 Crossings = 0;
		const TArray<FSimCruiseRibbon>& Ribbons = B->Roads.Ribbons;
		for (int32 a = 0; a < Ribbons.Num(); ++a)
		for (const FSimCruiseRibbonStation& Deck : Ribbons[a].St)
		{
			if (!Deck.bBridge) continue;
			for (int32 b = 0; b < Ribbons.Num(); ++b)
			{
				if (b == a) continue;
				for (const FSimCruiseRibbonStation& Below : Ribbons[b].St)
				{
					if (Below.bBridge || FVector2d::Distance(Below.C, Deck.C) > Deck.HalfW || Deck.HC - Below.HC < 3.0) continue;
					++Crossings;
					for (const FSimCruiseRoadStep& S : Steps)
					{
						if (S.Ribbon == b && FVector2d::Distance(S.PositionM, Below.C) < 10.0) WorstUnder = FMath::Max(WorstUnder, S.JumpCm);
					}
				}
			}
		}
		TestTrue(FString::Printf(TEXT("%s: %d stations under a deck, worst step there %.1f cm"), Case.Name, Crossings, WorstUnder), WorstUnder <= SimCruiseRoadCheck::DefaultThresholdCm);
	}
	return true;
}

#endif
