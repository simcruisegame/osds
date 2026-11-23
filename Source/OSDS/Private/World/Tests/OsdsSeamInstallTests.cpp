// Copyright OSDS contributors. All Rights Reserved.

#include "Async/TaskGraphInterfaces.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "World/OsdsGeo.h"
#include "World/OsdsRoadNetwork.h"
#include "World/OsdsTileBuilder.h"
#include "World/OsdsTileData.h"
#include "World/OsdsWorldSubsystem.h"
#include "World/OsdsWorldResources.h"
#include "World/OsdsWorldTile.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OsdsSeamInstallTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	double TileSizeM() { return FOsdsWorldFrame::ForPlace(UOsdsWorldSubsystem::BundledLon, UOsdsWorldSubsystem::BundledLat).TileSizeM; }

	TSharedRef<FOsdsTileBuild> BuildWith(const FIntPoint& Tile, double Side, const TMap<FIntPoint, TSharedPtr<FOsdsTileBuild>>& Done)
	{
		FOsdsTileData Data;
		FString Error;
		OsdsTileData::LoadFile(OsdsTileData::BundledTilePath(Tile), Data, Error);
		const TArray<FOsdsSeamEnd> Contracts = OsdsTileBuilder::NeighbourContracts(Tile, Side, [&Done](const FIntPoint& T) -> const FOsdsTileBuild*
		{
			const TSharedPtr<FOsdsTileBuild>* Found = Done.Find(T);
			return Found ? Found->Get() : nullptr;
		});
		return OsdsTileBuilder::Build(Data, Side, Contracts);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsSeamBankingTest, "OSDS.World.SeamBanking", OsdsSeamInstallTest::Flags)
bool FOsdsSeamBankingTest::RunTest(const FString& Parameters)
{
	using namespace OsdsSeamInstallTest;
	// The start tile, then its east and south neighbours built to its contracts.
	const double Side = TileSizeM();
	const FIntPoint Start(18180, 27908);
	TMap<FIntPoint, TSharedPtr<FOsdsTileBuild>> Done;
	Done.Add(Start, BuildWith(Start, Side, Done));
	int32 Pairs = 0, Lifts = 0;
	double WorstBank = 0.0, WorstGrade = 0.0;
	for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1) })
	{
		const FIntPoint Next = Start + Step;
		if (!OsdsTileData::HasBundledTile(Next)) continue;
		const TSharedRef<FOsdsTileBuild> B = BuildWith(Next, Side, Done);
		const FOsdsTileBuild& A = *Done[Start];
		const FVector2d Shift(Step.X * Side, Step.Y * Side);
		for (const FOsdsRibbon& RB : B->Roads.Ribbons)
		{
			for (int32 End = 0; End < 2; ++End)
			{
				if (!(End == 0 ? RB.bStartOnBoundary : RB.bEndOnBoundary)) continue;
				const FOsdsRibbonStation& SB = End == 0 ? RB.St[0] : RB.St.Last();
				// The same point on the start tile.
				const FVector2d OnA = SB.C + Shift;
				for (const FOsdsRibbon& RA : A.Roads.Ribbons)
				{
					for (int32 EndA = 0; EndA < 2; ++EndA)
					{
						if (!(EndA == 0 ? RA.bStartOnBoundary : RA.bEndOnBoundary)) continue;
						const FOsdsRibbonStation& SA = EndA == 0 ? RA.St[0] : RA.St.Last();
						if (FVector2d::Distance(SA.C, OnA) > 0.05 || FMath::Abs(FVector2d::DotProduct(SA.T, SB.T)) < 0.7) continue;
						++Pairs;
						// The seam section stores the banking it has, the same seen from either side.
						const double Sign = FVector2d::DotProduct(SA.T, SB.T) >= 0.0 ? 1.0 : -1.0;
						// The cross-slope as built (edge heights), which is what the tyres meet.
						const double XA = (SA.HL - SA.HR) / (2.0 * FMath::Max(SA.HalfW, 0.1)), XB = (SB.HL - SB.HR) / (2.0 * FMath::Max(SB.HalfW, 0.1));
						WorstBank = FMath::Max(WorstBank, FMath::Abs(XA - Sign * XB));
					}
				}
			}
		}
		Done.Add(Next, B);
	}
	// Seam pins change the grade by 1 % at most: the profile's slope near an uncontracted end
	// on the start tile differs from the slope 60 m in by no more than the lift allows.
	for (const FOsdsRibbon& R : Done[Start]->Roads.Ribbons)
	{
		for (int32 k = 1; k < R.St.Num(); ++k)
		{
			const double Ds = R.St[k].S - R.St[k - 1].S;
			if (Ds < 1.0) continue;
			const double FromEnd = FMath::Min(R.St[k].S - R.St[0].S, R.St.Last().S - R.St[k].S);
			if (FromEnd > 30.0) continue;
			if (!(R.bStartOnBoundary || R.bEndOnBoundary)) continue;
			++Lifts;
			WorstGrade = FMath::Max(WorstGrade, FMath::Abs(R.St[k].Z - R.St[k - 1].Z) / Ds);
		}
	}
	AddInfo(FString::Printf(TEXT("%d seam pairs, worst banking mismatch %.5f; worst grade within 30 m of a seam %.3f over %d steps"), Pairs, WorstBank, WorstGrade, Lifts));
	TestTrue(TEXT("seams found"), Pairs > 3);
	// Contracted ends meet exactly; an end whose neighbour found no contract (a corner, a
	// split) is solved on each side alone and may differ by about a centimetre at the edges.
	TestTrue(FString::Printf(TEXT("the banking meets across the seam (%.5f)"), WorstBank), WorstBank < 3e-3);
	TestTrue(FString::Printf(TEXT("no steep pin ramp at a seam (%.3f)"), WorstGrade), WorstGrade < 0.12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsStepInstallTest, "OSDS.World.StepInstall", OsdsSeamInstallTest::Flags)
bool FOsdsStepInstallTest::RunTest(const FString& Parameters)
{
	using namespace OsdsSeamInstallTest;
	// A tile installed without the drawn resources (headless) is complete at once; its
	// collision is there after one install.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("OsdsStepInstallTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	{
		const double Side = TileSizeM();
		TMap<FIntPoint, TSharedPtr<FOsdsTileBuild>> None;
		const TSharedRef<FOsdsTileBuild> Build = BuildWith(FIntPoint(18180, 27908), Side, None);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AOsdsWorldTile* Tile = World->SpawnActor<AOsdsWorldTile>(AOsdsWorldTile::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		Tile->Install(Build, nullptr, nullptr, false);
		TestFalse(TEXT("headless: no steps left after the install"), Tile->HasPendingInstall());
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestTrue(TEXT("headless: collision ready"), Tile->IsCollisionReady());
		// ContinueInstall with nothing left does nothing.
		TestTrue(TEXT("nothing to continue"), Tile->ContinueInstall(5.0) < 5.0);
		Tile->Destroy();

		// With the world's resources the install is staged: steps remain, the tile is not
		// collision-ready until they are done, and each ContinueInstall runs at least one.
		UOsdsWorldResources* Resources = NewObject<UOsdsWorldResources>();
		Resources->AddToRoot();
		Resources->Initialise();
		AOsdsWorldTile* Staged = World->SpawnActor<AOsdsWorldTile>(AOsdsWorldTile::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		Staged->Install(BuildWith(FIntPoint(18180, 27908), Side, None), Resources, nullptr, false);
		TestTrue(TEXT("staged: steps remain after the install"), Staged->HasPendingInstall());
		TestFalse(TEXT("staged: not collision-ready while steps remain"), Staged->IsCollisionReady());
		int32 Steps = 0;
		while (Staged->HasPendingInstall() && Steps < 100)
		{
			Staged->ContinueInstall(0.0); // a zero budget still runs one step
			++Steps;
		}
		AddInfo(FString::Printf(TEXT("%d install steps"), Steps));
		TestTrue(TEXT("staged: several steps (collision sections, one cook, palms, signs)"), Steps >= 3 && Steps < 100);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestTrue(TEXT("staged: collision ready once the steps are done"), Staged->IsCollisionReady());
		Staged->Destroy();
		Resources->RemoveFromRoot();
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsSlopedSeamTest, "OSDS.World.SlopedSeam", OsdsSeamInstallTest::Flags)
bool FOsdsSlopedSeamTest::RunTest(const FString& Parameters)
{
	// Two small synthetic tiles on a hillside: the ground rises 6 % east and falls 8 % across
	// the road (north to south), and one road runs east across their shared edge.
	constexpr double Side = 550.0;
	auto TileJson = [](int32 X, double Z0, const TCHAR* Road)
	{
		FString H;
		const int32 Grid = 8;
		for (int32 j = 0; j <= Grid; ++j)
		for (int32 i = 0; i <= Grid; ++i)
		{
			const double East = (i / double(Grid)) * Side, South = (j / double(Grid)) * Side;
			if (!H.IsEmpty()) H += TEXT(",");
			H += FString::Printf(TEXT("%.3f"), Z0 + 0.06 * East - 0.08 * (South - 0.5 * Side));
		}
		return FString::Printf(TEXT("{\"id\":[%d,500],\"grid\":%d,\"h\":[%s],\"roads\":[%s]}"), X, Grid, *H, Road);
	};
	FOsdsTileData A, B;
	FString Error;
	const bool bA = OsdsTileData::Parse(TileJson(100, 0.0, TEXT("[\"tertiary\",0,\"Hill Road\",[[0.2,0.5],[0.6,0.52],[1.0,0.5]],7]")), A, Error);
	const bool bB = OsdsTileData::Parse(TileJson(101, 0.06 * Side, TEXT("[\"tertiary\",0,\"Hill Road\",[[0.0,0.5],[0.4,0.48],[0.8,0.5]],7]")), B, Error);
	if (!TestTrue(FString::Printf(TEXT("synthetic tiles parse (%s)"), *Error), bA && bB)) return false;
	TMap<FIntPoint, TSharedPtr<FOsdsTileBuild>> Done;
	const TSharedRef<FOsdsTileBuild> BA = OsdsTileBuilder::Build(A, Side);
	Done.Add(A.Id, BA);
	const TArray<FOsdsSeamEnd> Contracts = OsdsTileBuilder::NeighbourContracts(B.Id, Side, [&Done](const FIntPoint& T) -> const FOsdsTileBuild*
	{
		const TSharedPtr<FOsdsTileBuild>* Found = Done.Find(T);
		return Found ? Found->Get() : nullptr;
	});
	const TSharedRef<FOsdsTileBuild> BB = OsdsTileBuilder::Build(B, Side, Contracts);
	const FOsdsRibbon* RA = BA->Roads.Ribbons.FindByPredicate([](const FOsdsRibbon& R) { return R.bEndOnBoundary || R.bStartOnBoundary; });
	const FOsdsRibbon* RB = BB->Roads.Ribbons.FindByPredicate([](const FOsdsRibbon& R) { return R.bEndOnBoundary || R.bStartOnBoundary; });
	if (!TestNotNull(TEXT("road on tile A"), RA) || !TestNotNull(TEXT("road on tile B"), RB)) return false;
	const FOsdsRibbonStation& EA = RA->bEndOnBoundary ? RA->St.Last() : RA->St[0];
	const FOsdsRibbonStation& EB = RB->bStartOnBoundary ? RB->St[0] : RB->St.Last();
	// The sections meet exactly, banking and all.
	const bool bSame = FVector2d::DotProduct(EA.T, EB.T) >= 0.0;
	TestTrue(FString::Printf(TEXT("centre heights meet (%.4f / %.4f)"), EA.HC, EB.HC), FMath::Abs(EA.HC - EB.HC) < 1e-3);
	TestTrue(TEXT("edge heights meet"), FMath::Abs((bSame ? EA.HL : EA.HR) - EB.HL) < 1e-3 && FMath::Abs((bSame ? EA.HR : EA.HL) - EB.HR) < 1e-3);
	TestTrue(TEXT("the seam keeps its banking"), FMath::Abs((bSame ? EA.E : -EA.E) - EB.E) < 1e-3);
	// Built at the same time (no contract on either side, as when two tiles stream in together),
	// the two seam sections still meet: no step at the tile edge.
	{
		const TSharedRef<FOsdsTileBuild> Alone = OsdsTileBuilder::Build(B, Side);
		const FOsdsRibbon* R2 = Alone->Roads.Ribbons.FindByPredicate([](const FOsdsRibbon& R) { return R.bEndOnBoundary || R.bStartOnBoundary; });
		if (TestNotNull(TEXT("road on the independent tile B"), R2))
		{
			const FOsdsRibbonStation& E2 = R2->bStartOnBoundary ? R2->St[0] : R2->St.Last();
			const bool bSame2 = FVector2d::DotProduct(EA.T, E2.T) >= 0.0;
			const double Step = FMath::Max3(FMath::Abs(EA.HC - E2.HC), FMath::Abs((bSame2 ? EA.HL : EA.HR) - E2.HL), FMath::Abs((bSame2 ? EA.HR : EA.HL) - E2.HR));
			AddInfo(FString::Printf(TEXT("seam step between independently built tiles: %.4f m"), Step));
			// Each side samples the shared ground along its own section line: about a centimetre on this 8 %% sidehill.
			TestTrue(FString::Printf(TEXT("independently built tiles meet at the seam (%.4f m)"), Step), Step < 0.02);
		}
	}
	// Grade change from the seam pins: at most 1 % plus the hill's own change of grade.
	double Worst = 0.0;
	for (const FOsdsRibbon* R : { RA, RB })
	{
		for (int32 k = 2; k < R->St.Num(); ++k)
		{
			const double D1 = R->St[k - 1].S - R->St[k - 2].S, D2 = R->St[k].S - R->St[k - 1].S;
			if (D1 < 1.0 || D2 < 1.0) continue;
			const double G1 = (R->St[k - 1].Z - R->St[k - 2].Z) / D1, G2 = (R->St[k].Z - R->St[k - 1].Z) / D2;
			Worst = FMath::Max(Worst, FMath::Abs(FMath::Abs(G2) - 0.06));
		}
	}
	AddInfo(FString::Printf(TEXT("worst departure from the hill's 6 %% grade: %.4f"), Worst));
	// Every boundary end keeps its clearance: the shared ground stays under both edges of the
	// seam section by the shoulder drop (0.05 m), across the whole carriageway.
	double WorstClear = 1e9;
	for (const TPair<const FOsdsTileData*, const FOsdsTileBuild*>& T : { TPair<const FOsdsTileData*, const FOsdsTileBuild*>(&A, &BA.Get()), TPair<const FOsdsTileData*, const FOsdsTileBuild*>(&B, &BB.Get()) })
	{
		for (const FOsdsRibbon& R : T.Value->Roads.Ribbons)
		{
			for (int32 End = 0; End < 2; ++End)
			{
				if (!(End == 0 ? R.bStartOnBoundary : R.bEndOnBoundary)) continue;
				const FOsdsRibbonStation& St = End == 0 ? R.St[0] : R.St.Last();
				for (double L = -St.HalfW; L <= St.HalfW + 1e-6; L += 0.5)
				{
					const double Ground = T.Key->HeightAtUnit((St.C + St.R * L) / Side);
					WorstClear = FMath::Min(WorstClear, St.Height(L) - Ground);
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("least clearance over the ground at a seam: %.3f m"), WorstClear));
	TestTrue(FString::Printf(TEXT("the road clears the ground at the seams (%.3f m)"), WorstClear), WorstClear >= 0.04);
	TestTrue(FString::Printf(TEXT("seam pins bend the grade by about 1 %% at most (%.4f)"), Worst), Worst < 0.02);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsBridgeDeckProfileTest, "OSDS.World.BridgeDeckProfile", OsdsSeamInstallTest::Flags)
bool FOsdsBridgeDeckProfileTest::RunTest(const FString& Parameters)
{
	// The helper supplies the full-chain deck profile in `bridges`. A deck at 10 m over
	// flat zero-metre ground must remain at that height even when this tile has no
	// neighbouring seam contract yet.
	const FString Json = TEXT("{\"id\":[100,100],\"grid\":1,\"h\":[0,0,0,0],\"roads\":[[\"primary\",0,\"test deck\",[[0,0.5],[1,0.5]],8,\"paved\",\"bridge\"]],\"bridges\":[[\"primary\",\"test deck\",[[0,0.5],[1,0.5]],8,[10,10],0,0]]}");
	FOsdsTileData A;
	FString Error;
	if (!TestTrue(TEXT("bridge tile parses"), OsdsTileData::Parse(Json, A, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("the tile keeps the exported bridge profile"), A.Bridges.Num(), 1);
	constexpr double Side = 200.0;
	auto DeckHeightError = [](const FOsdsRoadNetwork& Roads)
	{
		double Worst = 0.0;
		for (const FOsdsRibbon& Ribbon : Roads.Ribbons)
		{
			for (const FOsdsRibbonStation& Station : Ribbon.St)
			{
				if (Station.bBridge) Worst = FMath::Max(Worst, FMath::Abs(Station.HC - 10.0));
			}
		}
		return Worst;
	};
	auto FindEnd = [](const FOsdsRoadNetwork& Roads, double X, FOsdsSeamEnd& OutEnd)
	{
		TArray<FOsdsSeamEnd> Ends;
		Roads.CollectSeamEnds(Ends);
		for (const FOsdsSeamEnd& End : Ends)
		{
			if (FMath::Abs(End.P.X - X) < 0.05) { OutEnd = End; return true; }
		}
		return false;
	};
	auto Translate = [](const FOsdsRoadNetwork& Roads, double DX)
	{
		TArray<FOsdsSeamEnd> Ends;
		Roads.CollectSeamEnds(Ends);
		for (FOsdsSeamEnd& End : Ends) End.P.X += DX;
		return Ends;
	};

	FOsdsRoadNetwork FirstBuilt;
	FirstBuilt.Build(A, Side);
	TestEqual(TEXT("one bridge ribbon"), FirstBuilt.Ribbons.Num(), 1);
	const double UncontractedError = DeckHeightError(FirstBuilt);
	TestTrue(FString::Printf(TEXT("the uncontracted deck follows its authored profile (%.3f m error)"), UncontractedError), UncontractedError < 0.02);

	FOsdsTileData B = A;
	B.Id = FIntPoint(101, 100);
	FOsdsRoadNetwork SecondBuilt;
	SecondBuilt.Build(B, Side, Translate(FirstBuilt, -Side));
	FOsdsSeamEnd FirstEast, SecondWest;
	const bool bFirstEast = FindEnd(FirstBuilt, Side, FirstEast);
	const bool bSecondWest = FindEnd(SecondBuilt, 0.0, SecondWest);
	TestTrue(TEXT("the first bridge tile has an east seam"), bFirstEast);
	TestTrue(TEXT("the later bridge tile has a west seam"), bSecondWest);
	if (bFirstEast && bSecondWest) TestTrue(TEXT("a later tile agrees with the existing bridge seam"), FMath::Abs(FirstEast.HC - SecondWest.HC) < 0.01);
	TestTrue(FString::Printf(TEXT("the later tile keeps the authored deck profile (%.3f m error)"), DeckHeightError(SecondBuilt)), DeckHeightError(SecondBuilt) < 0.02);

	FOsdsRoadNetwork ReverseFirst;
	ReverseFirst.Build(B, Side);
	FOsdsRoadNetwork ReverseSecond;
	A.Id = FIntPoint(100, 100);
	ReverseSecond.Build(A, Side, Translate(ReverseFirst, Side));
	FOsdsSeamEnd ReverseWest, ReverseEast;
	const bool bReverseWest = FindEnd(ReverseFirst, 0.0, ReverseWest);
	const bool bReverseEast = FindEnd(ReverseSecond, Side, ReverseEast);
	TestTrue(TEXT("the reverse first tile has a west seam"), bReverseWest);
	TestTrue(TEXT("the reverse second tile has an east seam"), bReverseEast);
	TestTrue(FString::Printf(TEXT("the reverse build order keeps its deck profile (%.3f m error)"), DeckHeightError(ReverseFirst)), DeckHeightError(ReverseFirst) < 0.02);
	if (bReverseWest && bReverseEast) TestTrue(TEXT("reverse build order meets at the same deck height"), FMath::Abs(ReverseWest.HC - ReverseEast.HC) < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsBridgeFlagSeamTest, "OSDS.World.BridgeFlagAcrossSeam", OsdsSeamInstallTest::Flags)
bool FOsdsBridgeFlagSeamTest::RunTest(const FString& Parameters)
{
	// N4 / H4 (Mackinac): the neighbour tile's road ends on the seam as a bridge deck, but this
	// tile's road row lost its "bridge" flag, so the deck stretch was built at ground level (a hole,
	// a lip and a fall). The exported deck profile is still here, so the flag must follow the
	// seam contract: this road is the same deck, at the deck's height.
	const FString Json = TEXT("{\"id\":[101,100],\"grid\":1,\"h\":[0,0,0,0],\"roads\":[[\"primary\",0,\"test deck\",[[0,0.5],[1,0.5]],8,\"paved\"]],\"bridges\":[[\"primary\",\"test deck\",[[0,0.5],[1,0.5]],8,[10,10],0,0]]}");
	FOsdsTileData B;
	FString Error;
	if (!TestTrue(TEXT("tile parses"), OsdsTileData::Parse(Json, B, Error)))
	{
		AddError(Error);
		return false;
	}
	constexpr double Side = 200.0;
	FOsdsSeamEnd Neighbour;
	Neighbour.P = FVector2d(0.0, 0.5 * Side);
	Neighbour.T = FVector2d(1.0, 0.0);
	Neighbour.HalfW = 4.0;
	Neighbour.HL = Neighbour.HC = Neighbour.HR = 10.0;
	Neighbour.bBridge = true;
	FOsdsRoadNetwork Built;
	Built.Build(B, Side, { Neighbour });
	if (!TestEqual(TEXT("one ribbon"), Built.Ribbons.Num(), 1)) return false;
	const FOsdsRibbon& Ribbon = Built.Ribbons[0];
	TestTrue(TEXT("the seam end of the road is a bridge deck"), Ribbon.St[0].bBridge);
	TestTrue(FString::Printf(TEXT("the deck stays at its height at the seam (%.2f m)"), Ribbon.St[0].HC), FMath::Abs(Ribbon.St[0].HC - 10.0) < 0.05);
	// A road with no bridge neighbour is left alone: no flag invented.
	FOsdsSeamEnd Ground = Neighbour;
	Ground.bBridge = false;
	Ground.HL = Ground.HC = Ground.HR = 0.0;
	FOsdsRoadNetwork Plain;
	Plain.Build(B, Side, { Ground });
	if (Plain.Ribbons.Num() == 1) TestFalse(TEXT("no bridge neighbour, no bridge flag"), Plain.Ribbons[0].St[0].bBridge);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsBridgeDeckSmoothTest, "OSDS.World.BridgeDeckSmooth", OsdsSeamInstallTest::Flags)
bool FOsdsBridgeDeckSmoothTest::RunTest(const FString& Parameters)
{
	// The owner's report: "the bridges go up and down, not flat across". A deck row whose station
	// heights carry noise and one point dropped to the valley floor (a chain split at a ramp) must
	// still give one smooth deck: a straight grade from end to end plus at most a crest, never a sag.
	const FString Json = TEXT("{\"id\":[100,100],\"grid\":1,\"h\":[0,0,0,0],")
		TEXT("\"roads\":[[\"primary\",0,\"wavy deck\",[[0,0.5],[1,0.5]],8,\"paved\",\"bridge\"]],")
		TEXT("\"bridges\":[[\"primary\",\"wavy deck\",[[0,0.5],[0.1,0.5],[0.2,0.5],[0.3,0.5],[0.4,0.5],[0.5,0.5],[0.6,0.5],[0.7,0.5],[0.8,0.5],[0.9,0.5],[1,0.5]],8,")
		TEXT("[10,10.6,9.4,11.5,6,11.2,10.5,12.3,10.9,11.9,12],0,0]]}");
	FOsdsTileData Data;
	FString Error;
	if (!TestTrue(TEXT("wavy bridge tile parses"), OsdsTileData::Parse(Json, Data, Error)))
	{
		AddError(Error);
		return false;
	}
	FOsdsRoadNetwork Roads;
	Roads.Build(Data, 200.0);
	TArray<double> Z;
	for (const FOsdsRibbon& Ribbon : Roads.Ribbons)
	{
		for (const FOsdsRibbonStation& Station : Ribbon.St) if (Station.bBridge) Z.Add(Station.HC);
	}
	if (!TestTrue(TEXT("the deck has stations"), Z.Num() > 10)) return false;
	TestTrue(FString::Printf(TEXT("the deck starts at its first end (%.2f m)"), Z[0]), FMath::Abs(Z[0] - 10.0) < 0.1 || FMath::Abs(Z[0] - 12.0) < 0.1);
	TestTrue(FString::Printf(TEXT("the deck ends at its other end (%.2f m)"), Z.Last()), FMath::Abs(Z.Last() - 10.0) < 0.1 || FMath::Abs(Z.Last() - 12.0) < 0.1);
	double WorstSag = 0.0, Lowest = 1e9;
	for (int32 i = 1; i + 1 < Z.Num(); ++i)
	{
		WorstSag = FMath::Max(WorstSag, Z[i + 1] - 2.0 * Z[i] + Z[i - 1]);
		Lowest = FMath::Min(Lowest, Z[i]);
	}
	TestTrue(FString::Printf(TEXT("no sag anywhere on the deck (worst second difference %.4f m)"), WorstSag), WorstSag < 0.005);
	TestTrue(FString::Printf(TEXT("the dropped point does not pull the deck down (lowest %.2f m)"), Lowest), Lowest > 9.9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsBridgeOverRoadTest, "OSDS.World.BridgeOverRoad", OsdsSeamInstallTest::Flags)
bool FOsdsBridgeOverRoadTest::RunTest(const FString& Parameters)
{
	// A street on a deck 6.5 m over a primary road: neither road joins the other. The junction join
	// pulled a deck down to the road under it within 15 m (and lifted the road below to the deck).
	const FString Json = TEXT("{\"id\":[100,100],\"grid\":1,\"h\":[0,0,0,0],")
		TEXT("\"roads\":[[\"residential\",0,\"over\",[[0,0.5],[1,0.5]],7,\"paved\",\"bridge\"],")
		TEXT("[\"primary\",0,\"under\",[[0.5,0],[0.5,1]],10,\"paved\",\"\"]],")
		TEXT("\"bridges\":[[\"residential\",\"over\",[[0,0.5],[1,0.5]],7,[6.5,6.5],0,0]]}");
	FOsdsTileData Data;
	FString Error;
	if (!TestTrue(TEXT("overpass tile parses"), OsdsTileData::Parse(Json, Data, Error)))
	{
		AddError(Error);
		return false;
	}
	FOsdsRoadNetwork Roads;
	Roads.Build(Data, 200.0);
	double DeckWorst = 0.0, UnderWorst = 0.0;
	for (const FOsdsRibbon& Ribbon : Roads.Ribbons)
	{
		for (const FOsdsRibbonStation& Station : Ribbon.St)
		{
			for (int32 c = 0; c < 5; ++c)
			{
				if (Station.bBridge) DeckWorst = FMath::Max(DeckWorst, FMath::Abs(Station.Column(c) - 6.5));
				else UnderWorst = FMath::Max(UnderWorst, FMath::Abs(Station.Column(c)));
			}
		}
	}
	TestTrue(FString::Printf(TEXT("the deck keeps its height over the road (worst %.2f m off)"), DeckWorst), DeckWorst < 0.25);
	TestTrue(FString::Printf(TEXT("the road under keeps its own level (worst %.2f m off)"), UnderWorst), UnderWorst < 0.25);
	return true;
}

#endif
