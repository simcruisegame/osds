// Copyright SimCruise contributors. All Rights Reserved.
//
// The start of a drive and the land around it (release test 20261114-0725-53b290c): the car
// starts on a road that goes on, never on a bank beside it; a corrupt elevation pixel never
// stands as a spike; the start tiles load together; a background car never builds a heavy model.

#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarAssets.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseRegionalTerrain.h"
#include "World/SimCruiseRoadCheck.h"
#include "World/SimCruiseStartPick.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseTileManager.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"
#include "World/SimCruiseWorldSubsystem.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseStartTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** A flat synthetic tile (100 m up) with the given roads ([class, travel, name, [[u, v]...], width] rows). */
	TSharedRef<FSimCruiseTileBuild> FlatTile(const FString& Roads, double Side)
	{
		const int32 Grid = 32;
		FString H;
		for (int32 i = 0; i < (Grid + 1) * (Grid + 1); ++i) H += i ? TEXT(",100") : TEXT("100");
		FSimCruiseTileData Data;
		FString Error;
		SimCruiseTileData::Parse(FString::Printf(TEXT("{\"id\":[300,600],\"grid\":%d,\"h\":[%s],\"roads\":[%s]}"), Grid, *H, *Roads), Data, Error);
		return SimCruiseTileBuilder::Build(Data, Side);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseStartOnRoadTest, "SimCruise.World.Start.OnRoadNotDeadEnd", SimCruiseStartTest::Flags)
bool FSimCruiseStartOnRoadTest::RunTest(const FString& Parameters)
{
	// Main Street across the tile; Wharf Road leaves it north and ends 165 m on (a dead end, as
	// a road to the water does). The start point lies near the wharf's end: the car starts on
	// Main Street, with the most road ahead there is, not facing the end of the wharf.
	constexpr double Side = 550.0;
	const TSharedRef<FSimCruiseTileBuild> B = SimCruiseStartTest::FlatTile(
		TEXT("[\"secondary\",0,\"Main Street\",[[0.02,0.5],[0.98,0.5]],7],[\"minor\",0,\"Wharf Road\",[[0.5,0.5],[0.5,0.2]],6]"), Side);
	const TArray<FSimCruiseStartTile> Tiles = { { FVector::ZeroVector, &B.Get() } };
	const FVector Near(0.5 * Side * 100.0, 0.25 * Side * 100.0, 0.0);
	const TArray<FSimCruiseStartPose> Picks = SimCruiseStartPick::Candidates(Tiles, FString(), Near, 900.0, 1e10);
	if (!TestTrue(TEXT("start candidates"), Picks.Num() > 0)) return false;
	const FSimCruiseStartPose& Best = Picks[0];
	const FSimCruiseRibbon& R = B->Roads.Ribbons[Best.Ribbon];
	TestEqual(TEXT("the start is on the road that goes on"), R.Name, FString(TEXT("Main Street")));
	double MostAhead = 0.0;
	for (const FSimCruiseStartPose& P : Picks) MostAhead = FMath::Max(MostAhead, P.AheadM);
	TestTrue(FString::Printf(TEXT("the most road ahead there is (%.0f of %.0f m)"), Best.AheadM, MostAhead), Best.AheadM >= MostAhead - 1.0);
	for (const FSimCruiseStartPose& P : Picks)
	{
		if (B->Roads.Ribbons[P.Ribbon].Name == TEXT("Wharf Road")) TestTrue(FString::Printf(TEXT("the wharf goes on only %.0f m"), P.AheadM), P.AheadM < 170.0);
	}
	// Every candidate stands on its carriageway; one moved 5 m to the side (onto the verge) does not.
	FString Why;
	TestTrue(TEXT("the start's footprint is on the carriageway"), SimCruiseStartPick::OnCarriageway(Tiles, Best, &Why));
	FSimCruiseStartPose Off = Best;
	const double Yaw = FMath::DegreesToRadians(Best.Rotation.Yaw);
	Off.LocationCm += FVector(-FMath::Sin(Yaw), FMath::Cos(Yaw), 0.0) * 500.0;
	TestFalse(TEXT("a pose 5 m to the side is off the carriageway"), SimCruiseStartPick::OnCarriageway(Tiles, Off, &Why));
	// The ahead walk: from Main Street's middle eastward to its end (~250 m), not past it.
	const double East = SimCruiseStartPick::AheadM(Tiles, Best.Tile, Best.Ribbon, B->Roads.Ribbons[Best.Ribbon].St[B->Roads.Ribbons[Best.Ribbon].St.Num() / 2].S, 1, 5000.0);
	TestTrue(FString::Printf(TEXT("ahead to the road's end: %.0f m"), East), East > 150.0 && East < 400.0);
	// An installed tile hands its layers' triangles to the collision and empties them
	// (ASimCruiseWorldTile::Install): the pick must find the same start on it. With the emptied
	// road layer it found none in the game and the nearest-road rule put the car at the bank
	// (Welch, WV, release-4 check 7dad4f6), while this test, on a build never installed, passed.
	TestTrue(TEXT("the build keeps its road faces for the pick"), B->RoadTops.IsValid());
	for (FSimCruiseMeshData& L : B->Layers)
	{
		L.Positions.Empty(); L.Normals.Empty(); L.Colors.Empty(); L.Indices.Empty(); L.UV0.Empty(); L.UV1.Empty();
	}
	FString WhyNone;
	const TArray<FSimCruiseStartPose> Installed = SimCruiseStartPick::Candidates(Tiles, FString(), Near, 900.0, 1e10, 24, &WhyNone);
	if (TestTrue(FString::Printf(TEXT("start candidates on an installed tile (%s)"), *WhyNone), Installed.Num() > 0))
	{
		TestTrue(TEXT("the same start as before the install"), FVector::Dist(Installed[0].LocationCm, Best.LocationCm) < 1.0 && FMath::Abs(Installed[0].Rotation.Yaw - Best.Rotation.Yaw) < 0.1);
	}
	return true;
}

namespace SimCruiseStartTest
{
	/** Independent checks of a start (not the pick's own code): the footprint's distance to the
	 * carriageway edge, the distance along its road to the nearest junction, the heading error. */
	void CheckStart(FAutomationTestBase& Test, const FString& Where, const TArray<FSimCruiseStartTile>& Tiles, const FSimCruiseStartPose& P)
	{
		const FSimCruiseTileBuild& B = *Tiles[P.Tile].Build;
		const FSimCruiseRibbon& R = B.Roads.Ribbons[P.Ribbon];
		const FVector2d C = FVector2d(P.LocationCm.X - Tiles[P.Tile].OriginCm.X, P.LocationCm.Y - Tiles[P.Tile].OriginCm.Y) / 100.0;
		const double Yaw = FMath::DegreesToRadians(P.Rotation.Yaw);
		const FVector2d Fwd(FMath::Cos(Yaw), FMath::Sin(Yaw)), Right(-Fwd.Y, Fwd.X);
		double Clear = TNumericLimits<double>::Max();
		for (const FVector2d& Q : { C + Fwd * 2.3 + Right, C + Fwd * 2.3 - Right, C - Fwd * 2.3 + Right, C - Fwd * 2.3 - Right })
		{
			double Best = TNumericLimits<double>::Max(), Lat = 0.0, HalfW = 0.0;
			for (int32 i = 0; i + 1 < R.St.Num(); ++i)
			{
				const FVector2d A = R.St[i].C, D = R.St[i + 1].C;
				const double T = FMath::Clamp(FVector2d::DotProduct(Q - A, D - A) / FMath::Max((D - A).SizeSquared(), 1e-9), 0.0, 1.0);
				const FVector2d On = FMath::Lerp(A, D, T);
				const double Dist = FVector2d::Distance(Q, On);
				if (Dist < Best) { Best = Dist; Lat = FVector2d::DotProduct(Q - On, FMath::Lerp(R.St[i].R, R.St[i + 1].R, T).GetSafeNormal()); HalfW = FMath::Lerp(R.St[i].HalfW, R.St[i + 1].HalfW, T); }
			}
			Clear = FMath::Min(Clear, HalfW - FMath::Abs(Lat));
		}
		Test.TestTrue(FString::Printf(TEXT("%s: the footprint %.2f m inside the carriageway edge (>= 0.5)"), *Where, Clear), Clear >= 0.5 - 1e-3);
		// The nearest junction along the road: a cut station, or an end of the ribbon on another road.
		const double S = R.St[P.Station].S;
		double ToJunction = TNumericLimits<double>::Max();
		for (int32 i = 0; i < R.St.Num(); ++i)
		{
			const bool bCut = (R.Sides[0].Cut.IsValidIndex(i) && R.Sides[0].Cut[i]) || (R.Sides[1].Cut.IsValidIndex(i) && R.Sides[1].Cut[i]);
			if (bCut) ToJunction = FMath::Min(ToJunction, FMath::Abs(R.St[i].S - S));
		}
		for (const int32 End : { 0, R.St.Num() - 1 })
		{
			if ((End == 0 && R.bStartOnBoundary) || (End > 0 && R.bEndOnBoundary)) continue;
			FSimCruiseRoadHit Hit;
			if (B.Roads.FindCarriageway(R.St[End].C, Hit, P.Ribbon, 2.0)) ToJunction = FMath::Min(ToJunction, FMath::Abs(R.St[End].S - S));
		}
		Test.TestTrue(FString::Printf(TEXT("%s: %.0f m from the nearest junction (>= 30)"), *Where, ToJunction), ToJunction >= 30.0 - 0.5);
		// The heading against the road's direction at the car (its own station's tangent, the way it drives).
		const FVector2d Road = R.St[P.Station].T * static_cast<double>(P.Dir);
		const double Error = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2d::DotProduct(Road, Fwd), -1.0, 1.0)));
		Test.TestTrue(FString::Printf(TEXT("%s: heading %.1f degrees off the road (< 5)"), *Where, Error), Error < 5.0);
		// The drive judge's own edge (ASimCruisePreflight's nearest aligned line and its half width,
		// docs/team/release-test.md "The drive judge: road edge and run-off"): the car's middle 1.5 m
		// inside, on the start's own ribbon.
		int32 JudgeTile = INDEX_NONE, JudgeRibbon = INDEX_NONE;
		const double Past = SimCruiseStartPick::JudgePastEdgeM(Tiles, P.LocationCm, P.Rotation.Yaw, &JudgeTile, &JudgeRibbon);
		Test.TestTrue(FString::Printf(TEXT("%s: %.2f m past the judge's edge (<= -1.5)"), *Where, Past), Past <= -1.5 + 1e-3);
		Test.TestTrue(FString::Printf(TEXT("%s: the judge's line is the start's road"), *Where), JudgeTile == P.Tile && JudgeRibbon == P.Ribbon);
	}

	/**
	 * The drive robot's route from a start (its own rule), 800 m or to a dead end, walked every
	 * 0.5 m in its lane over the drawn road: no gap (no road face, or one 0.5 m off the lane) and no
	 * step (the top's second difference) over 2 cm.
	 */
	void WalkRoute(FAutomationTestBase& Test, const FString& Where, const TArray<FSimCruiseStartTile>& Tiles, const FSimCruiseStartPose& P)
	{
		const TArray<SimCruiseStartPick::FRouteSample> Route = SimCruiseStartPick::RouteSamples(Tiles, P, 800.0, 0.5);
		TMap<int32, TSharedPtr<SimCruiseRoadCheck::FTopSurface>> Tops;
		TArray<double> Z;
		int32 Gaps = 0;
		FString FirstGap;
		for (const SimCruiseStartPick::FRouteSample& S : Route)
		{
			// The tile under the sample (a lane near a seam may lie in the next tile).
			double Top = 0.0;
			bool bFace = false;
			for (int32 t = 0; t < Tiles.Num() && !bFace; ++t)
			{
				const FVector2d L((S.LaneCm.X - Tiles[t].OriginCm.X) / 100.0, (S.LaneCm.Y - Tiles[t].OriginCm.Y) / 100.0);
				if (!Tiles[t].Build || L.X < 0.0 || L.Y < 0.0 || L.X > Tiles[t].Build->TileSizeM || L.Y > Tiles[t].Build->TileSizeM) continue;
				TSharedPtr<SimCruiseRoadCheck::FTopSurface>& G = Tops.FindOrAdd(t);
				if (!G.IsValid()) G = MakeShared<SimCruiseRoadCheck::FTopSurface>(*Tiles[t].Build);
				double H = 0.0;
				// A deck more than 1 m over the lane is a bridge across, not this road.
				if (G->Top(L, H) && H - S.LaneCm.Z / 100.0 < 1.0) { Top = H; bFace = true; }
			}
			if (!bFace || FMath::Abs(Top - S.LaneCm.Z / 100.0) > 0.5)
			{
				if (Gaps++ == 0) FirstGap = FString::Printf(TEXT("%s (%s)"), *S.LaneCm.ToString(), bFace ? *FString::Printf(TEXT("road %.2f m off the lane"), Top - S.LaneCm.Z / 100.0) : TEXT("no road face"));
				Z.Add(TNumericLimits<double>::Lowest());
				continue;
			}
			Z.Add(Top);
		}
		double Worst = 0.0;
		int32 WorstAt = INDEX_NONE, Steps = 0;
		// Report, not a gate (v2.0.3): every gap and every step over 2 cm goes to
		// Saved/RouteWalk/<place>.csv; the route walk becomes the gate in v2.0.4.
		FString Csv = TEXT("kind,along_m,x_cm,y_cm,z_cm,value\n");
		for (int32 k = 0; k < Z.Num(); ++k)
		{
			if (Z[k] < -1e30) Csv += FString::Printf(TEXT("gap,%.1f,%.0f,%.0f,%.0f,\n"), k * 0.5, Route[k].LaneCm.X, Route[k].LaneCm.Y, Route[k].LaneCm.Z);
		}
		for (int32 k = 1; k + 1 < Z.Num(); ++k)
		{
			if (Z[k - 1] < -1e30 || Z[k] < -1e30 || Z[k + 1] < -1e30) continue;
			const double J = FMath::Abs(Z[k + 1] - 2.0 * Z[k] + Z[k - 1]) * 100.0;
			if (J > 2.0)
			{
				++Steps;
				Csv += FString::Printf(TEXT("step_cm,%.1f,%.0f,%.0f,%.0f,%.1f\n"), k * 0.5, Route[k].LaneCm.X, Route[k].LaneCm.Y, Route[k].LaneCm.Z, J);
			}
			if (J > Worst) { Worst = J; WorstAt = k; }
		}
		FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RouteWalk"), Where + TEXT(".csv")));
		Test.AddInfo(FString::Printf(TEXT("ROUTE %s: %.0f m, %d gaps (first: %s), %d steps over 2 cm, worst %.1f cm%s"), *Where, Route.Num() * 0.5, Gaps,
			FirstGap.IsEmpty() ? TEXT("none") : *FirstGap, Steps, Worst, WorstAt != INDEX_NONE ? *FString::Printf(TEXT(" at %s"), *Route[WorstAt].LaneCm.ToString()) : TEXT("")));
	}

	/** Builds the tiles of one recorded place folder (x_y.json, place.txt with its order) in their order, with seam contracts. */
	bool BuildPlace(const FString& Dir, FSimCruiseWorldFrame& OutFrame, TMap<FIntPoint, TSharedRef<FSimCruiseTileBuild>>& OutBuilt, FVector& OutStartCm)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Dir, TEXT("place.txt")))) return false;
		double Lat = 0.0, Lon = 0.0;
		TArray<FIntPoint> Order;
		for (const FString& L : Lines)
		{
			TArray<FString> W;
			L.ParseIntoArrayWS(W);
			if (W.Num() >= 3 && W[0] == TEXT("start")) { Lat = FCString::Atod(*W[1]); Lon = FCString::Atod(*W[2]); }
			else if (W.Num() >= 2 && W[0] == TEXT("order")) { for (int32 k = 1; k < W.Num(); ++k) { FString X, Y; W[k].Split(TEXT("_"), &X, &Y); Order.Add(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y))); } }
		}
		if (Order.IsEmpty()) return false;
		OutFrame = FSimCruiseWorldFrame::ForPlace(Lon, Lat);
		OutStartCm = OutFrame.LonLatToWorldCm(Lon, Lat);
		for (const FIntPoint& Id : Order)
		{
			FSimCruiseTileData Data;
			FString Error;
			if (!SimCruiseTileData::LoadFile(FPaths::Combine(Dir, FString::Printf(TEXT("%d_%d.json"), Id.X, Id.Y)), Data, Error)) continue;
			const TArray<FSimCruiseSeamEnd> Contracts = SimCruiseTileBuilder::NeighbourContracts(Id, OutFrame.TileSizeM, [&OutBuilt](const FIntPoint& T) -> const FSimCruiseTileBuild* { const TSharedRef<FSimCruiseTileBuild>* F = OutBuilt.Find(T); return F ? &F->Get() : nullptr; });
			OutBuilt.Add(Id, SimCruiseTileBuilder::Build(Data, OutFrame.TileSizeM, Contracts));
		}
		return OutBuilt.Num() > 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseStartPlacesTest, "SimCruise.World.Start.EveryPlace", SimCruiseStartTest::Flags)
bool FSimCruiseStartPlacesTest::RunTest(const FString& Parameters)
{
	// The start rules on real places, CPU only: from the middle of every bundled Miami Beach tile,
	// and over every recorded place (Saved/StartPlaces/<key>/ and SIMCRUISE_PROBE_DIR: the
	// helper's tile documents and place.txt, as Debug.ReleaseFaultProbe reads them; the 18 places of
	// data/test-places.json go there with scripts\record-start-places.ps1). Every start: the
	// footprint 0.5 m or more inside the carriageway, 30 m or more from a junction, facing the road
	// within 5 degrees.
	int32 Starts = 0;
	{
		const FSimCruiseWorldFrame Frame = FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat);
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data"), TEXT("tiles"), TEXT("*.json")), true, false);
		TMap<FIntPoint, TSharedRef<FSimCruiseTileBuild>> Built;
		for (const FString& File : Files)
		{
			FString X, Y;
			if (!FPaths::GetBaseFilename(File).Split(TEXT("_"), &X, &Y) || !X.IsNumeric() || !Y.IsNumeric()) continue;
			const FIntPoint Id(FCString::Atoi(*X), FCString::Atoi(*Y));
			FSimCruiseTileData Data;
			FString Error;
			if (!SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(Id), Data, Error)) continue;
			Built.Add(Id, SimCruiseTileBuilder::Build(Data, Frame.TileSizeM));
		}
		TArray<FSimCruiseStartTile> Tiles;
		for (const TPair<FIntPoint, TSharedRef<FSimCruiseTileBuild>>& Pair : Built) Tiles.Add({ Frame.TileOriginCm(Pair.Key), &Pair.Value.Get() });
		for (const TPair<FIntPoint, TSharedRef<FSimCruiseTileBuild>>& Pair : Built)
		{
			const FVector Middle = Frame.TileOriginCm(Pair.Key) + FVector(Frame.TileSizeM * 50.0, Frame.TileSizeM * 50.0, 0.0);
			const TArray<FSimCruiseStartPose> Picks = SimCruiseStartPick::Candidates(Tiles, FString(), Middle, 300.0, 1e10, 1);
			if (Picks.IsEmpty()) continue;
			SimCruiseStartTest::CheckStart(*this, FString::Printf(TEXT("Miami Beach tile %d_%d"), Pair.Key.X, Pair.Key.Y), Tiles, Picks[0]);
			++Starts;
		}
	}
	TestTrue(FString::Printf(TEXT("starts checked: %d"), Starts), Starts >= 5);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseStartRecordedTest, "SimCruise.World.Start.RecordedPlaces", SimCruiseStartTest::Flags)
bool FSimCruiseStartRecordedTest::RunTest(const FString& Parameters)
{
	// As World.Start.EveryPlace, over every recorded place (Saved/StartPlaces/<key>/ and
	// SIMCRUISE_PROBE_DIR: the helper's tile documents and place.txt, as Debug.ReleaseFaultProbe reads
	// them; the 18 places of data/test-places.json go there with scripts\record-start-places.ps1).
	// About 200 tile builds: a slow test (Config/Tests/Tags.ini). Without recorded places it passes.
	int32 Starts = 0;
	TArray<FString> Roots = { FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StartPlaces")) };
	const FString Probe = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_PROBE_DIR"));
	if (!Probe.IsEmpty()) Roots.Add(Probe);
	for (const FString& Root : Roots)
	{
		TArray<FString> Places;
		IFileManager::Get().FindFiles(Places, *FPaths::Combine(Root, TEXT("*")), false, true);
		for (const FString& Place : Places)
		{
			FSimCruiseWorldFrame Frame;
			TMap<FIntPoint, TSharedRef<FSimCruiseTileBuild>> Built;
			FVector StartCm;
			if (!SimCruiseStartTest::BuildPlace(FPaths::Combine(Root, Place), Frame, Built, StartCm)) continue;
			TArray<FSimCruiseStartTile> Tiles;
			for (const TPair<FIntPoint, TSharedRef<FSimCruiseTileBuild>>& Pair : Built) Tiles.Add({ Frame.TileOriginCm(Pair.Key), &Pair.Value.Get() });
			const TArray<FSimCruiseStartPose> Picks = SimCruiseStartPick::Candidates(Tiles, FString(), StartCm, 900.0, 1e10, 1);
			if (!TestTrue(FString::Printf(TEXT("%s: a start"), *Place), Picks.Num() > 0)) continue;
			SimCruiseStartTest::CheckStart(*this, Place, Tiles, Picks[0]);
			SimCruiseStartTest::WalkRoute(*this, Place, Tiles, Picks[0]);
			++Starts;
		}
	}
	AddInfo(FString::Printf(TEXT("recorded places checked: %d"), Starts));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLandscapeOutlierTest, "SimCruise.World.Landscape.NoSpikes", SimCruiseStartTest::Flags)
bool FSimCruiseLandscapeOutlierTest::RunTest(const FString& Parameters)
{
	// A coast at 20 m (88 m mesh) with the Stonington ripple in it (a 1438 m peak beside a -314
	// m pit), and 30 km off a real mountain (a 1200 m cone, 3 km round) and a 300 m escarpment.
	TArray<FVector> P;
	auto Mountain = [](double X, double Y) { const double R = FMath::Sqrt(FMath::Square(X - 30000.0) + FMath::Square(Y)); return FMath::Max(0.0, 1200.0 * (1.0 - R / 3000.0)); };
	auto Escarpment = [](double X, double Y) { return X > 20000.0 && Y > 5000.0 ? 300.0 : 0.0; };
	for (int32 j = -60; j <= 60; ++j)
	for (int32 i = -60; i <= 420; ++i)
	{
		const double X = i * 88.0, Y = j * 88.0;
		P.Add(FVector(X * 100.0, Y * 100.0, (20.0 + Mountain(X, Y) + Escarpment(X, Y)) * 100.0));
	}
	const int32 Peak = P.IndexOfByPredicate([](const FVector& V) { return FMath::IsNearlyEqual(V.X, 0.0) && FMath::IsNearlyEqual(V.Y, 0.0); });
	const TArray<FVector> Clean = P;
	// The ripple: the row through the peak climbs to 1438 m, the row below drops to -314 m.
	const double Row[7] = { 157.0, 601.0, 1077.0, 1438.0, 1327.0, 1137.0, 274.0 };
	for (int32 k = 0; k < 7; ++k)
	{
		for (FVector& V : P)
		{
			if (FMath::IsNearlyEqual(V.X, (k - 3) * 8800.0) && FMath::IsNearlyEqual(V.Y, 0.0)) V.Z = Row[k] * 100.0;
			if (FMath::IsNearlyEqual(V.X, (k - 3) * 8800.0) && FMath::IsNearlyEqual(V.Y, -8800.0)) V.Z = -Row[k] * 0.22 * 100.0;
		}
	}
	const int32 Changed = ASimCruiseRegionalTerrain::RejectHeightOutliers(P);
	if (TestTrue(TEXT("the peak vertex is there"), P.IsValidIndex(Peak)))
	{
		TestTrue(FString::Printf(TEXT("the 1438 m peak is the coast again (%.1f m)"), P[Peak].Z / 100.0), FMath::Abs(P[Peak].Z / 100.0 - 20.0) < 1.0);
	}
	double WorstCoast = 0.0, WorstLand = 0.0;
	for (int32 i = 0; i < P.Num(); ++i)
	{
		const double Off = FMath::Abs(P[i].Z - Clean[i].Z) / 100.0;
		if (P[i].X < 15000.0 * 100.0) WorstCoast = FMath::Max(WorstCoast, Off);
		else WorstLand = FMath::Max(WorstLand, Off);
	}
	// What stays is under 100 m (the ripple's shallowest shoulder, a pit under the sea surface).
	TestTrue(FString::Printf(TEXT("the ripple is gone (worst %.1f m off the coast's 20 m)"), WorstCoast), WorstCoast < 100.0);
	TestTrue(FString::Printf(TEXT("the mountain and the escarpment keep their relief (worst change %.1f m)"), WorstLand), WorstLand < 0.01);
	TestTrue(FString::Printf(TEXT("only the ripple changed (%d vertices)"), Changed), Changed >= 7 && Changed <= 40);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseStartTilesTest, "SimCruise.World.Start.TilesTogether", SimCruiseStartTest::Flags)
bool FSimCruiseStartTilesTest::RunTest(const FString& Parameters)
{
	// Before the car is placed the start tile and its four corners load together (the start
	// boost), the start tile first; edge neighbours never build at the same time.
	FSimCruiseTileManager M;
	M.RingRadius = 1;
	M.MaxInFlight = ASimCruiseWorldStreamer::StartConcurrentBuilds;
	M.SetFocus(FVector2d(10.5, 20.5), FVector2d(10.5, 20.5));
	const TArray<FSimCruiseTileJobTicket> First = M.TakeRequests(0.0);
	TestEqual(TEXT("five start tiles at once"), First.Num(), 5);
	if (First.Num() > 0) TestEqual(TEXT("the start tile first"), First[0].Tile, FIntPoint(10, 20));
	for (int32 a = 0; a < First.Num(); ++a)
	for (int32 b = a + 1; b < First.Num(); ++b)
	{
		const FIntPoint D = First[a].Tile - First[b].Tile;
		TestFalse(TEXT("no two edge neighbours together"), FMath::Abs(D.X) + FMath::Abs(D.Y) == 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseBackgroundCarTest, "SimCruise.World.Start.NoHeavyBackgroundCars", SimCruiseStartTest::Flags)
bool FSimCruiseBackgroundCarTest::RunTest(const FString& Parameters)
{
	// The Miatas' model is heavy (38 MB of geometry, a 11-30 s build on the game thread): a parked
	// or traffic car never builds it; until the player's choice has, its share parks a lighter car.
	const int64 Miata = SimCruiseCarAssets::ModelBytes(TEXT("nb1999"));
	TestTrue(FString::Printf(TEXT("the Miata's model is heavy (%.1f MB)"), Miata / 1048576.0), Miata > SimCruiseCarAssets::BackgroundModelMaxBytes);
	TestTrue(TEXT("the Mustang's is light"), SimCruiseCarAssets::ModelBytes(TEXT("mustang65")) > 0 && SimCruiseCarAssets::ModelBytes(TEXT("mustang65")) <= SimCruiseCarAssets::BackgroundModelMaxBytes);
	// The start's build list (every model a background car can be, built one a frame while the
	// start tiles load, then the car is placed): each model once, and no heavy model that nothing
	// has built (the start would wait 30 s for it).
	const TArray<FString> StartList = SimCruiseParkedCars::AllCarIds();
	TestTrue(TEXT("a start build list"), StartList.Num() >= 3);
	for (int32 i = 0; i < StartList.Num(); ++i)
	{
		for (int32 j = i + 1; j < StartList.Num(); ++j) TestNotEqual(TEXT("each model once in the start list"), StartList[i], StartList[j]);
		TestTrue(FString::Printf(TEXT("%s: light or built already"), *StartList[i]),
			SimCruiseCarAssets::IsBuilt(StartList[i]) || SimCruiseCarAssets::ModelBytes(StartList[i]) <= SimCruiseCarAssets::BackgroundModelMaxBytes);
	}
	if (!SimCruiseCarAssets::IsBuilt(TEXT("nb1999")))
	{
		TestFalse(TEXT("the unbuilt Miata is not in the start list"), StartList.Contains(FString(TEXT("nb1999"))));
		for (int32 Pick = 0; Pick < 25; ++Pick)
		{
			const FString Id = SimCruiseParkedCars::CarFor(static_cast<uint8>(Pick));
			TestNotEqual(TEXT("the Miata's share parks a lighter car"), Id, FString(TEXT("nb1999")));
			TestTrue(TEXT("a light stand-in"), SimCruiseCarAssets::IsBuilt(Id) || SimCruiseCarAssets::ModelBytes(Id) <= SimCruiseCarAssets::BackgroundModelMaxBytes);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseReleaseFaultProbe, "SimCruise.Debug.ReleaseFaultProbe", SimCruiseStartTest::Flags)
bool FSimCruiseReleaseFaultProbe::RunTest(const FString& Parameters)
{
	// A probe on recorded places (not a check): SIMCRUISE_PROBE_DIR holds one folder per place with
	// its tiles' documents (x_y.json, from the helper's tile command) and place.txt (start lat lon;
	// order of the tiles as the game built them; placed x y in world cm; point lat lon label). It
	// builds the tiles in that order with their seam contracts, logs the start the rules pick, why
	// the old start failed, and what each flagged step is. Without the variable it does nothing.
	const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_PROBE_DIR"));
	if (Root.IsEmpty()) return true;
	TArray<FString> Places;
	IFileManager::Get().FindFiles(Places, *FPaths::Combine(Root, TEXT("*")), false, true);
	for (const FString& Place : Places)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Root, Place, TEXT("place.txt")))) continue;
		double Lat = 0.0, Lon = 0.0;
		FString Road;
		TArray<FIntPoint> Order;
		FVector2d Placed(1e30, 1e30);
		struct FPoint { double Lat, Lon; FString Label; };
		TArray<FPoint> Points;
		for (const FString& L : Lines)
		{
			TArray<FString> W;
			L.ParseIntoArrayWS(W);
			if (W.Num() >= 3 && W[0] == TEXT("start")) { Lat = FCString::Atod(*W[1]); Lon = FCString::Atod(*W[2]); }
			else if (W.Num() >= 2 && W[0] == TEXT("road")) { Road = L.RightChop(5).TrimStartAndEnd(); }
			else if (W.Num() >= 3 && W[0] == TEXT("placed")) { Placed = FVector2d(FCString::Atod(*W[1]), FCString::Atod(*W[2])); }
			else if (W.Num() >= 4 && W[0] == TEXT("point")) { Points.Add({ FCString::Atod(*W[1]), FCString::Atod(*W[2]), W[3] }); }
			else if (W.Num() >= 4 && W[0] == TEXT("pointcm")) { Points.Add({ 1e9 + FCString::Atod(*W[1]), FCString::Atod(*W[2]), W[3] }); }
			else if (W.Num() >= 2 && W[0] == TEXT("order")) { for (int32 k = 1; k < W.Num(); ++k) { FString X, Y; W[k].Split(TEXT("_"), &X, &Y); Order.Add(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y))); } }
		}
		const FSimCruiseWorldFrame Frame = FSimCruiseWorldFrame::ForPlace(Lon, Lat);
		TMap<FIntPoint, TSharedRef<FSimCruiseTileBuild>> Built;
		TArray<FSimCruiseStartTile> Tiles;
		TArray<FIntPoint> TileIds;
		for (const FIntPoint& Id : Order)
		{
			FSimCruiseTileData Data;
			FString Error;
			if (!SimCruiseTileData::LoadFile(FPaths::Combine(Root, Place, FString::Printf(TEXT("%d_%d.json"), Id.X, Id.Y)), Data, Error)) { AddInfo(FString::Printf(TEXT("%s %d_%d: %s"), *Place, Id.X, Id.Y, *Error)); continue; }
			const TArray<FSimCruiseSeamEnd> Contracts = SimCruiseTileBuilder::NeighbourContracts(Id, Frame.TileSizeM, [&Built](const FIntPoint& T) -> const FSimCruiseTileBuild* { const TSharedRef<FSimCruiseTileBuild>* F = Built.Find(T); return F ? &F->Get() : nullptr; });
			Built.Add(Id, SimCruiseTileBuilder::Build(Data, Frame.TileSizeM, Contracts));
		}
		for (const TPair<FIntPoint, TSharedRef<FSimCruiseTileBuild>>& Pair : Built) { Tiles.Add({ Frame.TileOriginCm(Pair.Key), &Pair.Value.Get() }); TileIds.Add(Pair.Key); }
		const FVector StartCm = Frame.LonLatToWorldCm(Lon, Lat);
		const TArray<FSimCruiseStartPose> Picks = SimCruiseStartPick::Candidates(Tiles, Road, StartCm, 900.0, 1e10);
		FString Text = FString::Printf(TEXT("PROBE %s: %d tiles, start %s, %d start candidates"), *Place, Tiles.Num(), *StartCm.ToString(), Picks.Num());
		for (int32 k = 0; k < FMath::Min(3, Picks.Num()); ++k)
		{
			const FSimCruiseStartPose& P = Picks[k];
			const FSimCruiseRibbon& R = Tiles[P.Tile].Build->Roads.Ribbons[P.Ribbon];
			Text += FString::Printf(TEXT("\n  pick %d: %s (%s) tile %d_%d at %s yaw %.0f, %.0f m from the start, %.0f m ahead"), k, *R.Name, *R.Class, TileIds[P.Tile].X, TileIds[P.Tile].Y, *P.LocationCm.ToString(), P.Rotation.Yaw, P.DistanceM, P.AheadM);
		}
		// The release test's start: the ribbons there, and the ground.
		if (Placed.X < 1e29)
		{
			for (int32 t = 0; t < Tiles.Num(); ++t)
			{
				const FVector2d Local = (Placed - FVector2d(Tiles[t].OriginCm.X, Tiles[t].OriginCm.Y)) / 100.0;
				const FSimCruiseTileBuild& B = *Tiles[t].Build;
				if (Local.X < -5.0 || Local.Y < -5.0 || Local.X > B.TileSizeM + 5.0 || Local.Y > B.TileSizeM + 5.0) continue;
				double Top = 0.0;
				const bool bTop = SimCruiseRoadCheck::TopSurface(B, Local, Top);
				Text += FString::Printf(TEXT("\n  old start in %d_%d at (%.1f, %.1f): road top %s, ground %.2f m"), TileIds[t].X, TileIds[t].Y, Local.X, Local.Y,
					bTop ? *FString::Printf(TEXT("%.2f m"), Top) : TEXT("none"), B.TerrainN > 0 ? B.TerrainHeightAt(FVector2d(FMath::Clamp(Local.X, 0.0, B.TileSizeM), FMath::Clamp(Local.Y, 0.0, B.TileSizeM))) : 0.0);
				for (int32 r = 0; r < B.Roads.Ribbons.Num(); ++r)
				{
					const FSimCruiseRibbon& R = B.Roads.Ribbons[r];
					int32 BestI = INDEX_NONE;
					double BestD = 15.0;
					for (int32 i = 0; i + 1 < R.St.Num(); ++i)
					{
						const FVector2d A = R.St[i].C, C = R.St[i + 1].C;
						const double T = FMath::Clamp(FVector2d::DotProduct(Local - A, C - A) / FMath::Max((C - A).SizeSquared(), 1e-9), 0.0, 1.0);
						const double D = FVector2d::Distance(Local, FMath::Lerp(A, C, T));
						if (D < BestD) { BestD = D; BestI = i; }
					}
					if (BestI == INDEX_NONE) continue;
					Text += FString::Printf(TEXT("\n    ribbon %d %s (%s) %d stations, station %d S %.1f: %.1f m from its centre line, half width %.1f, height %.2f, boundary start %d end %d"),
						r, *R.Name, *R.Class, R.St.Num(), BestI, R.St[BestI].S, BestD, R.St[BestI].HalfW, R.St[BestI].HC, R.bStartOnBoundary, R.bEndOnBoundary);
				}
			}
		}
		// Short ribbons (driveways, service ways) that start or end within 12 m of a point: their end
		// stations' column heights against the highest other carriageway under each column.
		for (const FPoint& Pt : Points)
		{
			const FVector W = Pt.Lat > 1e8 ? FVector(Pt.Lat - 1e9, Pt.Lon, 0.0) : Frame.LonLatToWorldCm(Pt.Lon, Pt.Lat);
			for (int32 t = 0; t < Tiles.Num(); ++t)
			{
				const FSimCruiseTileBuild& B = *Tiles[t].Build;
				const FVector2d Local = (FVector2d(W.X, W.Y) - FVector2d(Tiles[t].OriginCm.X, Tiles[t].OriginCm.Y)) / 100.0;
				if (Local.X < -12.0 || Local.Y < -12.0 || Local.X > B.TileSizeM + 12.0 || Local.Y > B.TileSizeM + 12.0) continue;
				for (int32 r = 0; r < B.Roads.Ribbons.Num(); ++r)
				{
					const FSimCruiseRibbon& R = B.Roads.Ribbons[r];
					for (const int32 End : { 0, 1 })
					{
						const int32 I0 = End == 0 ? 0 : R.St.Num() - 1;
						if (FVector2d::Distance(R.St[I0].C, Local) > 12.0) continue;
						Text += FString::Printf(TEXT("\n  %s: ribbon %d %s (%s, rank %d) %s end:"), *Pt.Label, r, *R.Name, *R.Class, R.Rank, End == 0 ? TEXT("start") : TEXT("far"));
						for (int32 k = 0; k < FMath::Min(6, R.St.Num()); ++k)
						{
							const int32 I = End == 0 ? k : R.St.Num() - 1 - k;
							const FSimCruiseRibbonStation& St = R.St[I];
							Text += FString::Printf(TEXT("\n    S %.1f:"), St.S);
							for (int32 c = 0; c < 5; ++c)
							{
								const FVector2d P = St.C + St.R * (FSimCruiseRibbonStation::ColumnOffset(c) * St.HalfW);
								// The highest other carriageway under this column.
								double Other = TNumericLimits<double>::Lowest();
								int32 OtherR = INDEX_NONE;
								TArray<TPair<int32, int32>> Segs;
								B.Roads.GatherSegments(P, 15.0, Segs);
								for (const TPair<int32, int32>& Sg : Segs)
								{
									if (Sg.Key == r) continue;
									const FSimCruiseRoadHit H = B.Roads.Project(Sg.Key, Sg.Value, P);
									if (FMath::Abs(H.Lateral) <= H.HalfW && FMath::Abs(H.Along) < 0.01 && H.Height > Other) { Other = H.Height; OtherR = Sg.Key; }
								}
								Text += OtherR == INDEX_NONE ? FString::Printf(TEXT(" %.3f/-"), St.Column(c))
									: FString::Printf(TEXT(" %.3f/%.3f(r%d rank %d)"), St.Column(c), Other, OtherR, B.Roads.Ribbons[OtherR].Rank);
							}
						}
					}
				}
			}
		}
		// The drive robot's route from the release run's own start ("placed x y" and "placedyaw deg"):
		// for samples between "routefrom" and "routeto" metres along it, the lane, the road face
		// under it, and every other carriageway within the car's width (1.2 m each side) whose
		// surface stands 0.15-4.5 m above the lane: a face the car's body would hit.
		{
			double Yaw = 1e10, From = 0.0, To = 0.0;
			for (const FString& L : Lines)
			{
				TArray<FString> Wd;
				L.ParseIntoArrayWS(Wd);
				if (Wd.Num() >= 2 && Wd[0] == TEXT("placedyaw")) Yaw = FCString::Atod(*Wd[1]);
				if (Wd.Num() >= 3 && Wd[0] == TEXT("route")) { From = FCString::Atod(*Wd[1]); To = FCString::Atod(*Wd[2]); }
			}
			if (Yaw < 1e9 && Placed.X < 1e29 && To > From)
			{
				// The start ribbon and station under the placed point, the way the yaw points.
				FSimCruiseStartPose Start;
				double BestD = 6.0;
				for (int32 t = 0; t < Tiles.Num(); ++t)
				{
					const FSimCruiseTileBuild& B = *Tiles[t].Build;
					const FVector2d Lc = (Placed - FVector2d(Tiles[t].OriginCm.X, Tiles[t].OriginCm.Y)) / 100.0;
					for (int32 r = 0; r < B.Roads.Ribbons.Num(); ++r)
					{
						const FSimCruiseRibbon& R = B.Roads.Ribbons[r];
						if (R.bService) continue;
						for (int32 i = 0; i < R.St.Num(); ++i)
						{
							const double D = FVector2d::Distance(R.St[i].C, Lc);
							if (D >= BestD) continue;
							BestD = D;
							const FVector2d H(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
							Start.Tile = t; Start.Ribbon = r; Start.Station = i; Start.Dir = FVector2d::DotProduct(R.St[i].T, H) >= 0.0 ? 1 : -1;
						}
					}
				}
				const TArray<SimCruiseStartPick::FRouteSample> Route = Start.Tile != INDEX_NONE ? SimCruiseStartPick::RouteSamples(Tiles, Start, To + 5.0, 0.5) : TArray<SimCruiseStartPick::FRouteSample>();
				Text += FString::Printf(TEXT("\n  route from the release start (%s): %d samples"), Start.Tile != INDEX_NONE ? TEXT("found") : TEXT("NOT FOUND"), Route.Num());
				TMap<int32, TSharedPtr<SimCruiseRoadCheck::FTopSurface>> Tops;
				for (int32 k = FMath::FloorToInt(From / 0.5); k < Route.Num() && k * 0.5 <= To; ++k)
				{
					const SimCruiseStartPick::FRouteSample& S = Route[k];
					const FSimCruiseStartTile& T = Tiles[S.Tile];
					const FVector2d L((S.LaneCm.X - T.OriginCm.X) / 100.0, (S.LaneCm.Y - T.OriginCm.Y) / 100.0);
					TSharedPtr<SimCruiseRoadCheck::FTopSurface>& G = Tops.FindOrAdd(S.Tile);
					if (!G.IsValid()) G = MakeShared<SimCruiseRoadCheck::FTopSurface>(*T.Build);
					double Top = 0.0;
					const bool bTop = G->Top(L, Top);
					FString Faces;
					// The lane's sideways direction from the next sample.
					const FVector2d Ahead = k + 1 < Route.Num() ? FVector2d(Route[k + 1].LaneCm - S.LaneCm).GetSafeNormal() : FVector2d(1.0, 0.0);
					const FVector2d Side(-Ahead.Y, Ahead.X);
					for (const double Off : { -1.2, -0.6, 0.6, 1.2 })
					{
						const FVector2d Q = L + Side * Off;
						TArray<TPair<int32, int32>> Segs;
						T.Build->Roads.GatherSegments(Q, 15.0, Segs);
						for (const TPair<int32, int32>& Sg : Segs)
						{
							const FSimCruiseRoadHit H = T.Build->Roads.Project(Sg.Key, Sg.Value, Q);
							if (FMath::Abs(H.Lateral) > H.HalfW || FMath::Abs(H.Along) > 0.01) continue;
							const double Up = H.Height - S.LaneCm.Z / 100.0;
							if (Up > 0.15 && Up < 4.5) Faces += FString::Printf(TEXT(" [r%d %s %.2f m up at %+.1f m]"), Sg.Key, *T.Build->Roads.Ribbons[Sg.Key].Class, Up, Off);
						}
					}
					// The ground beside the lane (a cut bank the car meets if it runs wide): its height
					// over the lane at 1.5, 2.5 and 3.5 m either side, where it stands 0.3 m or more over.
					for (const double Off : { -3.5, -2.5, -1.5, 1.5, 2.5, 3.5 })
					{
						const FVector2d Q = L + Side * Off;
						if (Q.X < 0.0 || Q.Y < 0.0 || Q.X > T.Build->TileSizeM || Q.Y > T.Build->TileSizeM) continue;
						const double Up = T.Build->TerrainHeightAt(Q) - S.LaneCm.Z / 100.0;
						if (Up > 0.3) Faces += FString::Printf(TEXT(" [ground %.2f m up at %+.1f m]"), Up, Off);
					}
					if (k % 4 == 0 || !Faces.IsEmpty() || !bTop)
					{
						Text += FString::Printf(TEXT("\n    %.1f m: tile %d_%d (%.1f, %.1f) lane %.2f, top %s%s"), k * 0.5, TileIds[S.Tile].X, TileIds[S.Tile].Y, L.X, L.Y,
							S.LaneCm.Z / 100.0, bTop ? *FString::Printf(TEXT("%.2f"), Top) : TEXT("NONE"), *Faces);
					}
				}

				// Every solid thing within 3 m of the path that reaches the car's body (0.15-1.3 m over
				// the lane there): bridge parapets and piers (ASimCruiseBridges builds them from the
				// bridge spans: parapet 0.85 m over the deck edge, its outer face down 1.2 m to the
				// deck's underside), and the tile's own collision (terrain and concrete over the lane
				// under the car's width or ahead; building and prop faces within 3 m). Traffic and
				// trees are actors the test does not build: see the run's log and frames for them.
				const int32 K0 = FMath::Max(0, FMath::FloorToInt(From / 0.5));
				const int32 K1 = FMath::Min(Route.Num() - 1, FMath::FloorToInt(To / 0.5));
				struct FObstacle { FString What; int32 FirstK = -1; int32 NearK = -1; double NearD = 1e9; double Lo = 0.0; double Hi = 0.0; };
				TMap<FString, FObstacle> Obstacles;
				auto Note = [&](const FString& Key, const FString& What, int32 k, double D, double Lo, double Hi)
				{
					const double Lane = Route[k].LaneCm.Z / 100.0;
					if (D > 3.0 || Hi < Lane + 0.15 || Lo > Lane + 1.3) return;
					FObstacle& O = Obstacles.FindOrAdd(Key);
					if (O.FirstK < 0) { O.What = What; O.FirstK = k; }
					if (D < O.NearD) { O.NearD = D; O.NearK = k; O.Lo = Lo - Lane; O.Hi = Hi - Lane; }
				};
				auto SegDist = [](const FVector2d& P, const FVector2d& A, const FVector2d& B)
				{
					const FVector2d D = B - A;
					const double T = D.SizeSquared() > 1e-12 ? FMath::Clamp(FVector2d::DotProduct(P - A, D) / D.SizeSquared(), 0.0, 1.0) : 0.0;
					return FVector2d::Distance(P, A + D * T);
				};
				TSet<int32> RouteTiles;
				for (int32 k = K0; k <= K1; ++k) RouteTiles.Add(Route[k].Tile);
				for (const int32 t : RouteTiles)
				{
					const FSimCruiseStartTile& T = Tiles[t];
					const FSimCruiseTileBuild& B = *T.Build;
					auto Local = [&](int32 k) { return FVector2d((Route[k].LaneCm.X - T.OriginCm.X) / 100.0, (Route[k].LaneCm.Y - T.OriginCm.Y) / 100.0); };
					// Bridges: the same spans, parapets and piers as SimCruiseBridges.cpp.
					for (int32 r = 0; r < B.Roads.Ribbons.Num(); ++r)
					{
						const FSimCruiseRibbon& R = B.Roads.Ribbons[r];
						const int32 N = R.St.Num();
						for (int32 a = 0; a < N; ++a)
						{
							if (!R.St[a].bBridge) continue;
							int32 e = a;
							while (e + 1 < N && R.St[e + 1].bBridge) ++e;
							const int32 A = FMath::Max(a - 1, 0), Z = FMath::Min(e + 1, N - 1);
							double SincePier = 35.0 * 0.5;
							for (int32 j = A; j < Z; ++j)
							{
								const FSimCruiseRibbonStation& S0 = R.St[j];
								const FSimCruiseRibbonStation& S1 = R.St[j + 1];
								// The deck's slab where SimCruiseBridges builds it.
								if (B.Roads.DeckUndersideAt(r, j))
								{
									for (int32 k = K0; k <= K1; ++k)
									{
										if (Route[k].Tile != t) continue;
										const FVector2d Lk = Local(k) - S0.C;
										const double Along = FVector2d::DotProduct(Lk, S0.T), Across = FMath::Abs(FVector2d::DotProduct(Lk, S0.R));
										if (Along < 0.0 || Along > S1.S - S0.S) continue;
										Note(FString::Printf(TEXT("t%d r%d slab %d"), t, r, a), FString::Printf(TEXT("bridge deck slab: ribbon %d (%s) span from station %d"), r, *R.Class, a),
											k, FMath::Max(0.0, Across - S0.HalfW - 0.4), S0.HC - 1.2, S0.HC);
									}
								}
								for (int32 Edge = -1; Edge <= 1; Edge += 2)
								{
									// Only the parapets SimCruiseBridges builds.
									if (!B.Roads.ParapetAt(r, j, Edge)) continue;
									const FVector2d M0 = S0.C + S0.R * (Edge * (S0.HalfW + 0.2)), M1 = S1.C + S1.R * (Edge * (S1.HalfW + 0.2));
									const double Z0 = S0.Height(Edge * S0.HalfW), Z1 = S1.Height(Edge * S1.HalfW);
									const double Lo = FMath::Min(Z0, Z1) - 1.2, Hi = FMath::Max(Z0, Z1) + 0.85;
									for (int32 k = K0; k <= K1; ++k)
									{
										if (Route[k].Tile != t) continue;
										Note(FString::Printf(TEXT("t%d r%d span %d edge %d"), t, r, a, Edge),
											FString::Printf(TEXT("bridge parapet: ribbon %d (%s%s) span from station %d, %s edge, deck edge %.2f m, parapet from %.2f to %.2f m"),
												r, *R.Class, R.Name.IsEmpty() ? TEXT("") : *(TEXT(" ") + R.Name), a, Edge < 0 ? TEXT("left") : TEXT("right"), Z0, Lo, Hi),
											k, FMath::Max(0.0, SegDist(Local(k), M0, M1) - 0.2), Lo, Hi);
									}
								}
								SincePier += S1.S - S0.S;
								if (SincePier >= 35.0 && j > A && j + 1 < Z)
								{
									SincePier = 0.0;
									const double Top = S0.HC - 1.2;
									const double Ground = B.TerrainHeightAt(FVector2d(FMath::Clamp(S0.C.X, 0.0, B.TileSizeM), FMath::Clamp(S0.C.Y, 0.0, B.TileSizeM)));
									if (Top - Ground > 4.0)
									{
										const double HalfR = FMath::Max(S0.HalfW * 0.6, 2.0);
										for (int32 k = K0; k <= K1; ++k)
										{
											if (Route[k].Tile != t) continue;
											const FVector2d D = Local(k) - S0.C;
											const double Dx = FMath::Max(0.0, FMath::Abs(FVector2d::DotProduct(D, S0.T)) - 0.6);
											const double Dy = FMath::Max(0.0, FMath::Abs(FVector2d::DotProduct(D, S0.R)) - HalfR);
											Note(FString::Printf(TEXT("t%d r%d pier %d"), t, r, j), FString::Printf(TEXT("bridge pier: ribbon %d (%s) at station %d"), r, *R.Class, j),
												k, FMath::Sqrt(Dx * Dx + Dy * Dy), Ground - 1.0, Top);
										}
									}
								}
							}
							a = e;
						}
					}
					// The tile's collision layers.
					const TPair<ESimCruiseTileLayer, const TCHAR*> Layers[] = {
						{ ESimCruiseTileLayer::Terrain, TEXT("terrain") }, { ESimCruiseTileLayer::Concrete, TEXT("concrete") },
						{ ESimCruiseTileLayer::Building, TEXT("building") }, { ESimCruiseTileLayer::Props, TEXT("prop") } };
					FBox2d Box(ForceInit);
					for (int32 k = K0; k <= K1; ++k) if (Route[k].Tile == t) Box += Local(k);
					Box = Box.ExpandBy(3.0);
					for (const TPair<ESimCruiseTileLayer, const TCHAR*>& Ly : Layers)
					{
						const FSimCruiseMeshData& M = B.Layer(Ly.Key);
						// Terrain and concrete are ground: they count under the car's width only (they
						// rise beside every cut); buildings and props count within 3 m.
						const bool bGround = Ly.Key == ESimCruiseTileLayer::Terrain || Ly.Key == ESimCruiseTileLayer::Concrete;
						const double Reach = bGround ? 0.9 : 3.0;
						for (int32 i = 0; i + 2 < M.Indices.Num(); i += 3)
						{
							const FVector3d P0 = FVector3d(M.Positions[M.Indices[i]]) / 100.0, P1 = FVector3d(M.Positions[M.Indices[i + 1]]) / 100.0, P2 = FVector3d(M.Positions[M.Indices[i + 2]]) / 100.0;
							const FBox2d Tb(TArray<FVector2d>{ FVector2d(P0), FVector2d(P1), FVector2d(P2) });
							if (!Tb.Intersect(Box)) continue;
							const double Lo = FMath::Min3(P0.Z, P1.Z, P2.Z), Hi = FMath::Max3(P0.Z, P1.Z, P2.Z);
							const FVector2d A(P0), Bv(P1), C(P2);
							for (int32 k = K0; k <= K1; ++k)
							{
								if (Route[k].Tile != t) continue;
								const FVector2d P = Local(k);
								if (!Tb.ExpandBy(Reach).IsInside(P)) continue;
								// Inside the triangle: distance 0, else to its nearest edge.
								const double D1 = (Bv - A) ^ (P - A), D2 = (C - Bv) ^ (P - Bv), D3 = (A - C) ^ (P - C);
								const bool bIn = (D1 >= 0 && D2 >= 0 && D3 >= 0) || (D1 <= 0 && D2 <= 0 && D3 <= 0);
								const double D = bIn ? 0.0 : FMath::Min3(SegDist(P, A, Bv), SegDist(P, Bv, C), SegDist(P, C, A));
								if (D > Reach) continue;
								double ZLo = Lo, ZHi = Hi;
								if (bGround)
								{
									// The ground's height at the nearest point, not the triangle's range.
									const FVector3d N = FVector3d::CrossProduct(P1 - P0, P2 - P0);
									if (FMath::Abs(N.Z) < 1e-6) continue;
									const FVector2d Q = bIn ? P : P; // near enough at under 0.9 m
									ZHi = ZLo = P0.Z - (N.X * (Q.X - P0.X) + N.Y * (Q.Y - P0.Y)) / N.Z;
									ZHi = FMath::Clamp(ZHi, Lo, Hi);
									ZLo = ZHi - 0.01;
								}
								const FVector2d Cen = (A + Bv + C) / 3.0;
								Note(FString::Printf(TEXT("t%d %s %d_%d"), t, Ly.Value, FMath::FloorToInt(Cen.X / 5.0), FMath::FloorToInt(Cen.Y / 5.0)),
									FString::Printf(TEXT("%s near (%.1f, %.1f) tile %d_%d"), Ly.Value, Cen.X, Cen.Y, TileIds[t].X, TileIds[t].Y), k, D, ZLo, ZHi);
							}
						}
					}
				}
				TArray<FObstacle> List;
				Obstacles.GenerateValueArray(List);
				List.Sort([](const FObstacle& A, const FObstacle& B) { return A.NearK < B.NearK; });
				Text += FString::Printf(TEXT("\n  solid things within 3 m of the path at body height, %.0f-%.0f m: %d"), From, To, List.Num());
				for (int32 i = 0; i < FMath::Min(60, List.Num()); ++i)
				{
					const FObstacle& O = List[i];
					Text += FString::Printf(TEXT("\n    nearest %.2f m at %.1f m along (first within 3 m at %.1f m), %+.2f to %+.2f m over the lane: %s"),
						O.NearD, O.NearK * 0.5, O.FirstK * 0.5, O.Lo, O.Hi, *O.What);
				}
			}
		}
		// Roads crossing within 80 m of a point labelled "crossing...": every pair of centre lines
		// that cross there, with both heights and bridge flags; a crossing between 0.3 and 4.5 m
		// apart is neither a junction nor an overpass (the upper road's edge is a wall for the lower).
		for (const FPoint& Pt : Points)
		{
			if (!Pt.Label.StartsWith(TEXT("crossing"))) continue;
			const FVector W = Pt.Lat > 1e8 ? FVector(Pt.Lat - 1e9, Pt.Lon, 0.0) : Frame.LonLatToWorldCm(Pt.Lon, Pt.Lat);
			for (int32 t = 0; t < Tiles.Num(); ++t)
			{
				const FSimCruiseTileBuild& B = *Tiles[t].Build;
				const FVector2d Local = (FVector2d(W.X, W.Y) - FVector2d(Tiles[t].OriginCm.X, Tiles[t].OriginCm.Y)) / 100.0;
				if (Local.X < -80.0 || Local.Y < -80.0 || Local.X > B.TileSizeM + 80.0 || Local.Y > B.TileSizeM + 80.0) continue;
				const TArray<FSimCruiseRibbon>& Rs = B.Roads.Ribbons;
				for (int32 a = 0; a < Rs.Num(); ++a)
				for (int32 b = a + 1; b < Rs.Num(); ++b)
				for (int32 i = 0; i + 1 < Rs[a].St.Num(); ++i)
				{
					const FVector2d P0 = Rs[a].St[i].C, P1 = Rs[a].St[i + 1].C;
					if (FVector2d::Distance(P0, Local) > 80.0) continue;
					for (int32 j = 0; j + 1 < Rs[b].St.Num(); ++j)
					{
						const FVector2d Q0 = Rs[b].St[j].C, Q1 = Rs[b].St[j + 1].C;
						const FVector2d D1 = P1 - P0, D2 = Q1 - Q0;
						const double Den = D1.X * D2.Y - D1.Y * D2.X;
						if (FMath::Abs(Den) < 1e-9) continue;
						const double U = ((Q0.X - P0.X) * D2.Y - (Q0.Y - P0.Y) * D2.X) / Den;
						const double V = ((Q0.X - P0.X) * D1.Y - (Q0.Y - P0.Y) * D1.X) / Den;
						if (U < 0.0 || U > 1.0 || V < 0.0 || V > 1.0) continue;
						const double Ha = FMath::Lerp(Rs[a].St[i].HC, Rs[a].St[i + 1].HC, U), Hb = FMath::Lerp(Rs[b].St[j].HC, Rs[b].St[j + 1].HC, V);
						const FVector2d X = P0 + D1 * U;
						const double Dh = FMath::Abs(Ha - Hb);
						Text += FString::Printf(TEXT("\n  %s: tile %d_%d (%.1f, %.1f) %.0f m from the point: ribbon %d %s (%s%s) %.2f m x ribbon %d %s (%s%s) %.2f m: %.2f m apart%s"),
							*Pt.Label, TileIds[t].X, TileIds[t].Y, X.X, X.Y, FVector2d::Distance(X, Local),
							a, *Rs[a].Name, *Rs[a].Class, (Rs[a].St[i].bBridge || Rs[a].St[i + 1].bBridge) ? TEXT(", bridge") : TEXT(""), Ha,
							b, *Rs[b].Name, *Rs[b].Class, (Rs[b].St[j].bBridge || Rs[b].St[j + 1].bBridge) ? TEXT(", bridge") : TEXT(""), Hb,
							Dh, (Dh > 0.3 && Dh < 4.5) ? TEXT("  <-- NEITHER JUNCTION NOR OVERPASS") : TEXT(""));
					}
				}
			}
		}
		// The flagged steps: the step check's steps within 12 m of each point, and what is there.
		for (const FPoint& Pt : Points)
		{
			// A point given in world cm (pointcm x y) carries x + 1e9 in Lat and y in Lon.
			const FVector W = Pt.Lat > 1e8 ? FVector(Pt.Lat - 1e9, Pt.Lon, 0.0) : Frame.LonLatToWorldCm(Pt.Lon, Pt.Lat);
			Text += FString::Printf(TEXT("\n  %s at %s:"), *Pt.Label, *W.ToString());
			// The ribbons there (within 10 m).
			for (int32 t = 0; t < Tiles.Num(); ++t)
			{
				const FSimCruiseTileBuild& B = *Tiles[t].Build;
				const FVector2d Local = (FVector2d(W.X, W.Y) - FVector2d(Tiles[t].OriginCm.X, Tiles[t].OriginCm.Y)) / 100.0;
				if (Local.X < -10.0 || Local.Y < -10.0 || Local.X > B.TileSizeM + 10.0 || Local.Y > B.TileSizeM + 10.0) continue;
				for (int32 r = 0; r < B.Roads.Ribbons.Num(); ++r)
				{
					const FSimCruiseRibbon& R = B.Roads.Ribbons[r];
					for (int32 i = 0; i + 1 < R.St.Num(); ++i)
					{
						const FVector2d A = R.St[i].C, C = R.St[i + 1].C;
						const double T = FMath::Clamp(FVector2d::DotProduct(Local - A, C - A) / FMath::Max((C - A).SizeSquared(), 1e-9), 0.0, 1.0);
						const double D = FVector2d::Distance(Local, FMath::Lerp(A, C, T));
						if (D > 10.0) continue;
						Text += FString::Printf(TEXT("\n    on tile %d_%d ribbon %d %s (%s, travel %d) %.1f m from its centre line, half width %.1f, height %.2f, heading %.0f, S %.0f of %.0f..%.0f"),
							TileIds[t].X, TileIds[t].Y, r, *R.Name, *R.Class, R.St[i].Travel, D, R.St[i].HalfW, R.St[i].HC,
							FMath::RadiansToDegrees(FMath::Atan2(R.St[i].T.Y, R.St[i].T.X)), R.St[i].S, R.St[0].S, R.St.Last().S);
						break;
					}
				}
			}
			for (int32 t = 0; t < Tiles.Num(); ++t)
			{
				const FSimCruiseTileBuild& B = *Tiles[t].Build;
				const FVector2d Local = (FVector2d(W.X, W.Y) - FVector2d(Tiles[t].OriginCm.X, Tiles[t].OriginCm.Y)) / 100.0;
				if (Local.X < -12.0 || Local.Y < -12.0 || Local.X > B.TileSizeM + 12.0 || Local.Y > B.TileSizeM + 12.0) continue;
				const double ToEdge = FMath::Min(FMath::Min(Local.X, Local.Y), FMath::Min(B.TileSizeM - Local.X, B.TileSizeM - Local.Y));
				Text += FString::Printf(TEXT("\n    tile %d_%d local (%.1f, %.1f), %.1f m from its edge"), TileIds[t].X, TileIds[t].Y, Local.X, Local.Y, ToEdge);
				// The top surface the car drives on (any ribbon), along the nearest ribbon through the
				// point, 0.1 m apart for 8 m each way, across this tile and its neighbours: the largest
				// second difference and where.
				{
					int32 NearR = INDEX_NONE, NearI = 0;
					double NearD = 12.0;
					for (int32 r = 0; r < B.Roads.Ribbons.Num(); ++r)
					{
						const FSimCruiseRibbon& R = B.Roads.Ribbons[r];
						for (int32 i = 0; i + 1 < R.St.Num(); ++i)
						{
							const FVector2d A = R.St[i].C, C = R.St[i + 1].C;
							const double T = FMath::Clamp(FVector2d::DotProduct(Local - A, C - A) / FMath::Max((C - A).SizeSquared(), 1e-9), 0.0, 1.0);
							const double D = FVector2d::Distance(Local, FMath::Lerp(A, C, T));
							if (D < NearD) { NearD = D; NearR = r; NearI = i; }
						}
					}
					if (NearR != INDEX_NONE)
					{
						const FVector2d Dir = B.Roads.Ribbons[NearR].St[NearI].T;
						const FVector2d WorldLocal = FVector2d(W.X, W.Y) / 100.0;
						TArray<double> H;
						TArray<FString> Who;
						for (double D = -8.0; D <= 8.0 + 1e-6; D += 0.1)
						{
							const FVector2d Q = WorldLocal + Dir * D;
							double Top = TNumericLimits<double>::Lowest();
							FString By = TEXT("none");
							for (int32 u = 0; u < Tiles.Num(); ++u)
							{
								const FSimCruiseTileBuild& U = *Tiles[u].Build;
								const FVector2d L2 = Q - FVector2d(Tiles[u].OriginCm.X, Tiles[u].OriginCm.Y) / 100.0;
								if (L2.X < 0.0 || L2.Y < 0.0 || L2.X > U.TileSizeM || L2.Y > U.TileSizeM) continue;
								double Th = 0.0;
								if (SimCruiseRoadCheck::TopSurface(U, L2, Th) && Th > Top) { Top = Th; By = FString::Printf(TEXT("%d_%d"), TileIds[u].X, TileIds[u].Y); }
							}
							H.Add(Top);
							Who.Add(By);
						}
						double Worst = 0.0;
						int32 At = INDEX_NONE;
						for (int32 k = 1; k + 1 < H.Num(); ++k)
						{
							if (H[k - 1] < -1e30 || H[k] < -1e30 || H[k + 1] < -1e30) continue;
							const double J = FMath::Abs(H[k + 1] - 2.0 * H[k] + H[k - 1]);
							if (J > Worst) { Worst = J; At = k; }
						}
						if (At != INDEX_NONE)
						{
							Text += FString::Printf(TEXT("\n      top surface along %s: worst jump %.1f cm at %+.1f m (%.3f, %.3f, %.3f m; tiles %s / %s / %s)"),
								*B.Roads.Ribbons[NearR].Name, Worst * 100.0, -8.0 + At * 0.1, H[At - 1], H[At], H[At + 1], *Who[At - 1], *Who[At], *Who[At + 1]);
						}
					}
				}
				for (const FSimCruiseRoadStep& S : SimCruiseRoadCheck::FindSteps(B))
				{
					if (FVector2d::Distance(S.PositionM, Local) > 12.0) continue;
					const FSimCruiseRibbon& R = B.Roads.Ribbons[S.Ribbon];
					// The nearest junction cut along the ribbon, and a bridge.
					double ToCut = 1e9;
					bool bBridge = false;
					for (int32 i = 0; i < R.St.Num(); ++i)
					{
						const bool bCut = (R.Sides[0].Cut.IsValidIndex(i) && R.Sides[0].Cut[i]) || (R.Sides[1].Cut.IsValidIndex(i) && R.Sides[1].Cut[i]);
						if (bCut) ToCut = FMath::Min(ToCut, FMath::Abs(R.St[i].S - S.S));
						if (FMath::Abs(R.St[i].S - S.S) < 10.0) bBridge |= R.St[i].bBridge;
					}
					Text += FString::Printf(TEXT("\n      step %.1f cm on %s (%s) at S %.1f lane %+.1f, %.1f m from a junction cut, bridge %d, ribbon S %.0f..%.0f boundary %d/%d"),
						S.JumpCm, *R.Name, *R.Class, S.S, S.Lateral, ToCut, bBridge ? 1 : 0, R.St[0].S, R.St.Last().S, R.bStartOnBoundary, R.bEndOnBoundary);
				}
			}
		}
		AddInfo(Text);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Text);
	}
	return true;
}

namespace SimCruiseStartTest
{
	/**
	 * Faces a car meets where two roads overlap (Doraville, GA, v2.0.3 release-4: a deck at the
	 * motorway's height stood its parapet across it). Faults: two centre lines crossing 0.3-4.5 m
	 * apart in height (neither one surface nor a deck over a road); a carriageway edge (and the skirt
	 * under it) standing 0.3-4.5 m over another road's surface inside its carriageway; a bridge
	 * parapet (0.85 m over the deck edge, its outer face 1.2 m below) inside another carriageway at
	 * a car's body height.
	 */
	TArray<FString> RoadOverlaps(const FSimCruiseTileBuild& B, const FString& Where)
	{
		TArray<FString> Out;
		const FSimCruiseRoadNetwork& Roads = B.Roads;
		auto Name = [&Roads](int32 Index)
		{
			const FSimCruiseRibbon& Rb = Roads.Ribbons[Index];
			return FString::Printf(TEXT("ribbon %d (%s%s)"), Index, *Rb.Class, Rb.Name.IsEmpty() ? TEXT("") : *(TEXT(" ") + Rb.Name));
		};
		auto Place = [&B, &Where](const FVector2d& At)
		{
			double Lon = 0.0, Lat = 0.0;
			SimCruiseGeo::TileToLonLat(FVector2d(B.Tile.X, B.Tile.Y) + At / B.TileSizeM, Lon, Lat);
			return FString::Printf(TEXT("%s (%.1f, %.1f) = %.6f, %.6f"), *Where, At.X, At.Y, Lat, Lon);
		};
		// Inside another carriageway at Q (not ribbon Not), Inset m in from its edges: its lowest surface there.
		auto Inside = [&Roads](const FVector2d& Q, int32 Not, double Inset, double& OutH, int32& OutRibbon)
		{
			TArray<TPair<int32, int32>> Near;
			Roads.GatherSegments(Q, 1.0, Near);
			bool bAny = false;
			for (const TPair<int32, int32>& Sg : Near)
			{
				if (Sg.Key == Not) continue;
				const FSimCruiseRoadHit Hq = Roads.Project(Sg.Key, Sg.Value, Q);
				if (FMath::Abs(Hq.Along) > 0.01 || FMath::Abs(Hq.Lateral) > Hq.HalfW - Inset) continue;
				if (!bAny || Hq.Height < OutH) { OutH = Hq.Height; OutRibbon = Sg.Key; bAny = true; }
			}
			return bAny;
		};
		const double Side = B.TileSizeM;
		for (int32 r = 0; r < Roads.Ribbons.Num(); ++r)
		{
			const FSimCruiseRibbon& R = Roads.Ribbons[r];
			for (int32 i = 0; i + 1 < R.St.Num(); ++i)
			{
				const FSimCruiseRibbonStation& A = R.St[i];
				const FSimCruiseRibbonStation& C = R.St[i + 1];
				// Centre lines crossing.
				TArray<TPair<int32, int32>> Segs;
				Roads.GatherSegments(0.5 * (A.C + C.C), 0.5 * FVector2d::Distance(A.C, C.C) + 1.0, Segs);
				for (const TPair<int32, int32>& Sg : Segs)
				{
					if (Sg.Key <= r) continue;
					const FSimCruiseRibbonStation& D = Roads.Ribbons[Sg.Key].St[Sg.Value];
					const FSimCruiseRibbonStation& E = Roads.Ribbons[Sg.Key].St[Sg.Value + 1];
					const FVector2d U = C.C - A.C, V = E.C - D.C;
					const double Den = U ^ V;
					if (FMath::Abs(Den) < 1e-9) continue;
					const double Tu = ((D.C - A.C) ^ V) / Den, Tv = ((D.C - A.C) ^ U) / Den;
					if (Tu < 0.0 || Tu > 1.0 || Tv < 0.0 || Tv > 1.0) continue;
					const FVector2d X = A.C + U * Tu;
					if (X.X < 0.5 || X.Y < 0.5 || X.X > Side - 0.5 || X.Y > Side - 0.5) continue;
					const double Dz = FMath::Abs(FMath::Lerp(A.HC, C.HC, Tu) - FMath::Lerp(D.HC, E.HC, Tv));
					if (Dz > 0.3 && Dz < 4.5) Out.Add(FString::Printf(TEXT("%s x %s cross %.2f m apart in height at %s"), *Name(r), *Name(Sg.Key), Dz, *Place(X)));
				}
				// A deck's slab (1.2 m deep) over a road in a car's way, where SimCruiseBridges builds it.
				if ((A.bBridge || C.bBridge) && Roads.DeckUndersideAt(r, i))
				{
					double Hs = 0.0;
					int32 Os = INDEX_NONE;
					if (Inside(A.C, r, 0.0, Hs, Os) && A.HC - 1.2 < Hs + 1.3 && A.HC > Hs + 0.15)
					{
						Out.Add(FString::Printf(TEXT("%s's deck slab stands in %s, %+.2f to %+.2f m over it, at %s"), *Name(r), *Name(Os), A.HC - 1.2 - Hs, A.HC - Hs, *Place(A.C)));
					}
				}
				// Its edges, and its parapets on a deck.
				for (const int32 Edge : { -1, 1 })
				{
					const FVector2d P = A.C + A.R * (Edge * A.HalfW);
					if (P.X < 0.5 || P.Y < 0.5 || P.X > Side - 0.5 || P.Y > Side - 0.5) continue;
					const double Ze = A.Height(Edge * A.HalfW);
					double H = 0.0;
					int32 O = INDEX_NONE;
					if (Inside(P, r, 0.3, H, O) && Ze - H > 0.3 && Ze - H < 4.5)
					{
						Out.Add(FString::Printf(TEXT("%s's %s edge stands %.2f m over %s at %s"), *Name(r), Edge < 0 ? TEXT("left") : TEXT("right"), Ze - H, *Name(O), *Place(P)));
					}
					// The parapets SimCruiseBridges builds (one station past each span end too).
					const bool bSpan = A.bBridge || C.bBridge || (i > 0 && R.St[i - 1].bBridge);
					if (bSpan && Roads.ParapetAt(r, i, Edge))
					{
						const FVector2d Q = A.C + A.R * (Edge * (A.HalfW + 0.2));
						double Hq = 0.0;
						int32 Oq = INDEX_NONE;
						if (Inside(Q, r, 0.0, Hq, Oq) && Ze - 1.2 < Hq + 1.3 && Ze + 0.85 > Hq + 0.15)
						{
							Out.Add(FString::Printf(TEXT("%s's %s parapet stands in %s, %+.2f to %+.2f m over it, at %s"), *Name(r), Edge < 0 ? TEXT("left") : TEXT("right"), *Name(Oq), Ze - 1.2 - Hq, Ze + 0.85 - Hq, *Place(Q)));
						}
					}
				}
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoadOverlapsTest, "SimCruise.World.RoadOverlaps.Bundled", SimCruiseStartTest::Flags)
bool FSimCruiseRoadOverlapsTest::RunTest(const FString& Parameters)
{
	// Every bundled tile (Miami Beach, data/tiles): no face where two roads overlap, and a deck
	// that crosses a road clears it (SimCruiseRoadNetwork 4b). Each changed span is listed.
	const FSimCruiseWorldFrame Frame = FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat);
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data"), TEXT("tiles"), TEXT("*.json")), true, false);
	int32 NumTiles = 0, Faults = 0, Spans = 0, EdgeFaults = 0;
	for (const FString& File : Files)
	{
		FString X, Y;
		if (!FPaths::GetBaseFilename(File).Split(TEXT("_"), &X, &Y) || !X.IsNumeric() || !Y.IsNumeric()) continue;
		const FIntPoint Id(FCString::Atoi(*X), FCString::Atoi(*Y));
		FSimCruiseTileData Data;
		FString Error;
		if (!SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(Id), Data, Error)) continue;
		const TSharedRef<FSimCruiseTileBuild> B = SimCruiseTileBuilder::Build(Data, Frame.TileSizeM);
		++NumTiles;
		for (const FString& Span : B->Roads.BridgeChanges) { AddInfo(Span); ++Spans; }
		for (const FString& Fault : SimCruiseStartTest::RoadOverlaps(*B, FString::Printf(TEXT("tile %d_%d"), Id.X, Id.Y)))
		{
			// Crossings and parapets fail. Edges standing over a parallel carriageway (the two halves of
			// MacArthur Causeway and Alton Road overlap 0.31-0.45 m apart in height, older than the
			// bridge rule) are a ratchet: never more than the 27 of 2026-11-14.
			if (Fault.Contains(TEXT(" edge stands "))) { AddWarning(Fault); ++EdgeFaults; }
			else { AddError(Fault); }
			++Faults;
		}
	}
	TestTrue(FString::Printf(TEXT("edges standing over a parallel carriageway: %d (ratchet 27)"), EdgeFaults), EdgeFaults <= 27);
	TestTrue(FString::Printf(TEXT("bundled tiles checked: %d"), NumTiles), NumTiles >= 5);
	AddInfo(FString::Printf(TEXT("ROADOVERLAPS bundled: %d tiles, %d faults, %d bridge spans changed"), NumTiles, Faults, Spans));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoadOverlapsRecordedTest, "SimCruise.World.RoadOverlaps.Recorded", SimCruiseStartTest::Flags)
bool FSimCruiseRoadOverlapsRecordedTest::RunTest(const FString& Parameters)
{
	// As World.RoadOverlaps over every recorded place (Saved/StartPlaces and SIMCRUISE_PROBE_DIR),
	// the tiles in the game's build order. Writes Saved/BridgeFix/changed-spans.md: every bridge
	// span the clearance rule lifted or de-decked, by place. A slow test (Config/Tests/Tags.ini).
	TArray<FString> Roots = { FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StartPlaces")) };
	const FString Probe = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_PROBE_DIR"));
	if (!Probe.IsEmpty()) Roots.Add(Probe);
	// SIMCRUISE_PLACES=us-detroit-mi,us-doraville-ga: only those places.
	TArray<FString> Only;
	FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_PLACES")).ParseIntoArray(Only, TEXT(","), true);
	FString Md = TEXT("# Bridge spans changed by the clearance rule\n\nOne line per span whose parapets or deck slab were left out over a road at the deck's level. Tile/ribbon stand in for the way id, which the tile data does not carry.\n");
	int32 NumPlaces = 0, Faults = 0, Spans = 0;
	for (const FString& Root : Roots)
	{
		TArray<FString> Dirs;
		IFileManager::Get().FindFiles(Dirs, *FPaths::Combine(Root, TEXT("*")), false, true);
		for (const FString& Place : Dirs)
		{
			if (Only.Num() > 0 && !Only.Contains(Place)) continue;
			FSimCruiseWorldFrame Frame;
			TMap<FIntPoint, TSharedRef<FSimCruiseTileBuild>> Built;
			FVector StartCm;
			if (!SimCruiseStartTest::BuildPlace(FPaths::Combine(Root, Place), Frame, Built, StartCm)) continue;
			++NumPlaces;
			int32 PlaceFaults = 0, PlaceSpans = 0, PlaceFaces = 0, PlaceOther = 0;
			int32 PlaceCrossings = 0, PlaceConflicts = 0;
			FString CrossLines;
			FString Lines;
			for (const TPair<FIntPoint, TSharedRef<FSimCruiseTileBuild>>& Pair : Built)
			{
				for (const FString& Span : Pair.Value->Roads.BridgeChanges) { Lines += FString::Printf(TEXT("- %s\n"), *Span); ++PlaceSpans; }
				for (const FSimCruiseRoadCrossing& C : Pair.Value->Roads.Crossings)
				{
					++PlaceCrossings;
					if (!C.bAtGradeConflict) continue;
					++PlaceConflicts;
					const FSimCruiseRibbon& D = Pair.Value->Roads.Ribbons[C.Deck];
					const FSimCruiseRibbon& U = Pair.Value->Roads.Ribbons[C.Under];
					double Lon = 0.0, Lat = 0.0;
					SimCruiseGeo::TileToLonLat(FVector2d(Pair.Key.X, Pair.Key.Y) + D.St[C.DeckFrom].C / Pair.Value->TileSizeM, Lon, Lat);
					CrossLines += FString::Printf(TEXT("- tile %d_%d: %s %s (r%d) over %s %s (r%d) at %.6f, %.6f: clearance %.2f m\n"), Pair.Key.X, Pair.Key.Y,
						*D.Class, D.Name.IsEmpty() ? TEXT("(unnamed)") : *D.Name, C.Deck, *U.Class, U.Name.IsEmpty() ? TEXT("(unnamed)") : *U.Name, C.Under, Lat, Lon, C.ClearanceM);
				}
				for (const FString& Fault : SimCruiseStartTest::RoadOverlaps(*Pair.Value, FString::Printf(TEXT("%s tile %d_%d"), *Place, Pair.Key.X, Pair.Key.Y)))
				{
					// Faces in a car's way fail; edges over a parallel carriageway and crossings are
					// counted for the before/after ratchet (SIMCRUISE_NO_BRIDGE_RULE=1 gives "before").
					if (Fault.Contains(TEXT(" parapet stands ")) || Fault.Contains(TEXT(" deck slab stands "))) { AddError(Fault); ++PlaceFaces; }
					else { AddWarning(Fault); ++PlaceOther; }
					++PlaceFaults;
				}
			}
			AddInfo(FString::Printf(TEXT("ROADOVERLAPS %s: %d tiles, %d bridge spans changed, %d faults (%d faces in a car's way, %d other), bridge rule %s"),
				*Place, Built.Num(), PlaceSpans, PlaceFaults, PlaceFaces, PlaceOther, FSimCruiseRoadNetwork::BridgeRuleOn() ? TEXT("on") : TEXT("off")));
			Md += FString::Printf(TEXT("\n## %s: %d spans changed, %d overlap faults\n\n%s"), *Place, PlaceSpans, PlaceFaults, Lines.IsEmpty() ? TEXT("(none)\n") : *Lines);
			Md += FString::Printf(TEXT("\nDeck crossings over another road: %d, at one level (clearance under 4.9 m): %d\n\n%s"), PlaceCrossings, PlaceConflicts, *CrossLines);
			AddInfo(FString::Printf(TEXT("CROSSINGS %s: %d deck crossings, %d at one level"), *Place, PlaceCrossings, PlaceConflicts));
			Faults += PlaceFaults;
			Spans += PlaceSpans;
		}
	}
	FFileHelper::SaveStringToFile(Md, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BridgeFix"), FSimCruiseRoadNetwork::BridgeRuleOn() ? TEXT("changed-spans.md") : TEXT("before-rule.md")));
	AddInfo(FString::Printf(TEXT("ROADOVERLAPS recorded: %d places, %d bridge spans changed, %d faults"), NumPlaces, Spans, Faults));
	return true;
}

#endif
