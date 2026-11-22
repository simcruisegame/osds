// Copyright OSDS contributors. All Rights Reserved.

#include "Atlas/OsdsAtlasClient.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "World/OsdsGeo.h"
#include "World/OsdsRoadNetwork.h"
#include "World/OsdsStartPick.h"
#include "World/OsdsTileBuilder.h"
#include "World/OsdsTileData.h"
#include "World/OsdsTracks.h"
#include "World/OsdsWorldStreamer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OsdsTrackTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTrackDataTest, "OSDS.World.Tracks.Data", OsdsTrackTest::Flags)
bool FOsdsTrackDataTest::RunTest(const FString& Parameters)
{
	const TArray<FOsdsTrack>& All = OsdsTracks::All();
	TestTrue(TEXT("every NASCAR Cup track, Richmond and the road courses (34 and a street circuit)"), All.Num() >= 35);
	TSet<FString> Ids;
	int32 Ovals = 0, Roads = 0;
	for (const FOsdsTrack& T : All)
	{
		TestFalse(FString::Printf(TEXT("%s: one entry per id"), *T.Id), Ids.Contains(T.Id));
		Ids.Add(T.Id);
		// No series branding in the names the menu shows.
		TestFalse(FString::Printf(TEXT("%s: no series name"), *T.Name), T.Name.Contains(TEXT("NASCAR")) || T.Name.Contains(TEXT("IndyCar")) || T.Name.Contains(TEXT("Formula")));
		TestTrue(FString::Printf(TEXT("%s: a source"), *T.Id), T.Sources.Num() >= 1);
		TestTrue(FString::Printf(TEXT("%s: a published length"), *T.Id), T.LengthMi > 0.4 && T.LengthMi < 4.5);
		if (T.Kind == EOsdsTrackKind::Oval)
		{
			++Ovals;
			TestTrue(FString::Printf(TEXT("%s: an oval has its loop and banking"), *T.Id), T.bBanked && T.BankTurnDeg > 0.0 && T.BankTurnDeg <= 35.0);
			// The loop on the map is the track: within 6 % of the published length.
			TestTrue(FString::Printf(TEXT("%s: mapped %.3f mi against %.3f published"), *T.Id, T.MappedMi, T.LengthMi), FMath::Abs(T.MappedMi / T.LengthMi - 1.0) < 0.06);
			TestTrue(FString::Printf(TEXT("%s: an oval runs one way"), *T.Id), T.bHasBearing);
		}
		else if (T.Kind == EOsdsTrackKind::Road) ++Roads;
	}
	TestTrue(TEXT("22 ovals"), Ovals >= 22);
	TestTrue(TEXT("12 road courses"), Roads >= 12);
	for (const TCHAR* Must : { TEXT("richmond"), TEXT("daytona"), TEXT("talladega"), TEXT("bristol"), TEXT("watkinsglen"), TEXT("lagunaseca"), TEXT("cota"), TEXT("charlotteroval") })
	{
		TestTrue(FString::Printf(TEXT("%s is in the list"), Must), Ids.Contains(Must));
	}
	const FOsdsTrack* Daytona = All.FindByPredicate([](const FOsdsTrack& T) { return T.Id == TEXT("daytona"); });
	if (Daytona)
	{
		TestEqual(TEXT("Daytona's turns: 31 deg (published)"), Daytona->BankTurnDeg, 31.0);
		const FOsdsPlace Place = OsdsTracks::ToPlace(*Daytona);
		TestEqual(TEXT("a track's place starts on the raceway"), Place.Street, FString(OsdsTracks::RacewayStreet));
		TestTrue(TEXT("facing the racing direction"), Place.bHasBearing);
		TestEqual(TEXT("found by its place"), OsdsTracks::Near(Daytona->StartLon, Daytona->StartLat), Daytona);
	}
	TestNull(TEXT("no track in central Kansas City"), OsdsTracks::Near(-94.5786, 39.0997));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTrackBankingTest, "OSDS.World.Tracks.Banking", OsdsTrackTest::Flags)
bool FOsdsTrackBankingTest::RunTest(const FString& Parameters)
{
	// A stadium oval 1200 m by 500 m (two 350 m straights, 250 m turn radius) at Daytona's latitude.
	FOsdsTrack T;
	T.Id = TEXT("test");
	T.Kind = EOsdsTrackKind::Oval;
	T.bBanked = true;
	T.BankTurnDeg = 31.0; T.BankFrontDeg = 18.0; T.BankBackDeg = 3.0;
	const double Lat0 = 29.18, Lon0 = -81.07;
	const double Kx = 111320.0 * FMath::Cos(FMath::DegreesToRadians(Lat0)), Ky = 110540.0;
	auto LonLat = [&](double E, double N) { return FVector2d(Lon0 + E / Kx, Lat0 + N / Ky); };
	const double Half = 350.0, Radius = 250.0;
	// Counter-clockwise on the map (east, north), as curate.py writes it; the front straight is the south one.
	for (int32 k = 0; k <= 35; ++k) T.LoopLonLat.Add(LonLat(-Half + 2.0 * Half * k / 35.0, -Radius));
	for (int32 k = 1; k < 40; ++k) { const double A = -UE_DOUBLE_PI / 2 + UE_DOUBLE_PI * k / 40.0; T.LoopLonLat.Add(LonLat(Half + Radius * FMath::Cos(A), Radius * FMath::Sin(A))); }
	for (int32 k = 0; k <= 35; ++k) T.LoopLonLat.Add(LonLat(Half - 2.0 * Half * k / 35.0, Radius));
	for (int32 k = 1; k < 40; ++k) { const double A = UE_DOUBLE_PI / 2 + UE_DOUBLE_PI * k / 40.0; T.LoopLonLat.Add(LonLat(-Half + Radius * FMath::Cos(A), Radius * FMath::Sin(A))); }
	const FVector2d Start = LonLat(0.0, -Radius);
	T.StartLon = Start.X; T.StartLat = Start.Y;
	const FVector2d Centre = LonLat(0.0, 0.0);
	T.CentreLon = Centre.X; T.CentreLat = Centre.Y;
	const FVector2d TileF = OsdsGeo::LonLatToTile(Centre.X, Centre.Y);
	const FIntPoint Tile(FMath::FloorToInt(TileF.X), FMath::FloorToInt(TileF.Y));
	const double Side = OsdsGeo::TileSizeMetres(Lat0);
	FOsdsTrackFrame Frame;
	if (!TestTrue(TEXT("a frame"), OsdsTracks::MakeFrame(T, Tile, Side, Frame))) return false;
	auto Local = [&](double E, double N) { return (OsdsGeo::LonLatToTile(LonLat(E, N).X, LonLat(E, N).Y) - FVector2d(Tile.X, Tile.Y)) * Side; };
	auto Check = [&](const TCHAR* What, double E, double N, double OutE, double OutN, double WantDeg)
	{
		const FVector2d P = Local(E, N);
		// The station's right normal pointing outward (east, north to the tile frame's +X, -Y).
		const FVector2d R = FVector2d(OutE, -OutN).GetSafeNormal();
		double Slope = 0.0, W = 0.0, Sign = 0.0;
		TestTrue(FString::Printf(TEXT("%s: on the loop"), What), OsdsTracks::BankAt(Frame, P, R, 8.0, Slope, W, Sign));
		TestEqual(FString::Printf(TEXT("%s: %.1f deg"), What, WantDeg), FMath::RadiansToDegrees(FMath::Atan(Slope)), WantDeg, 1.5);
		TestEqual(FString::Printf(TEXT("%s: full weight"), What), W, 1.0, 0.02);
		TestEqual(FString::Printf(TEXT("%s: the right side is the outside"), What), Sign, 1.0);
		double S2 = 0.0, W2 = 0.0, Sign2 = 0.0;
		OsdsTracks::BankAt(Frame, P, -R, 8.0, S2, W2, Sign2);
		TestEqual(FString::Printf(TEXT("%s: driven the other way the left side is"), What), Sign2, -1.0);
	};
	Check(TEXT("the front straight"), 0.0, -Radius, 0.0, -1.0, 18.0);
	Check(TEXT("the back straight"), 0.0, Radius, 0.0, 1.0, 3.0);
	Check(TEXT("the east turn's apex"), Half + Radius, 0.0, 1.0, 0.0, 31.0);
	Check(TEXT("the west turn's apex"), -Half - Radius, 0.0, -1.0, 0.0, 31.0);
	// A road course in the infield, 120 m inside the loop: no banking.
	double Slope = 0.0, W = 0.0, Sign = 0.0;
	TestFalse(TEXT("the infield is flat"), OsdsTracks::BankAt(Frame, Local(0.0, -Radius + 120.0), FVector2d(0.0, 1.0), 6.0, Slope, W, Sign));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTrackStartTest, "OSDS.Network.Tracks.StartResolves", OsdsTrackTest::Flags)
bool FOsdsTrackStartTest::RunTest(const FString& Parameters)
{
	// Each track's start, on the tiles the map helper builds today: the start pick finds the
	// racing surface (a raceway ribbon, not the pit lane) within 60 m of the curated point, facing
	// the racing direction. Needs the helper and the network.
	const FOsdsAtlasConfig Config = FOsdsAtlasConfig::ResolveDefault();
	if (Config.Executable.IsEmpty())
	{
		AddWarning(TEXT("No map helper: the track starts are not checked"));
		return true;
	}
	int32 Checked = 0;
	for (const FOsdsTrack& T : OsdsTracks::All())
	{
		if (!T.IsRaceway()) continue;
		if (!Parameters.IsEmpty() && !Parameters.Contains(T.Id)) continue;
		const FOsdsWorldFrame Frame = FOsdsWorldFrame::ForPlace(T.StartLon, T.StartLat);
		const FVector StartCm = Frame.LonLatToWorldCm(T.StartLon, T.StartLat);
		const FVector2d TileF = OsdsGeo::LonLatToTile(T.StartLon, T.StartLat);
		const FIntPoint Home(FMath::FloorToInt(TileF.X), FMath::FloorToInt(TileF.Y));
		// The start's tile and the neighbours within 80 m of the start (its ribbon may run on across a seam).
		TArray<FIntPoint> Ids = { Home };
		const FVector2d In = (TileF - FVector2d(Home.X, Home.Y)) * Frame.TileSizeM;
		for (int32 dy = -1; dy <= 1; ++dy)
		for (int32 dx = -1; dx <= 1; ++dx)
		{
			if (dx == 0 && dy == 0) continue;
			const bool bX = dx == 0 || (dx < 0 ? In.X < 80.0 : In.X > Frame.TileSizeM - 80.0);
			const bool bY = dy == 0 || (dy < 0 ? In.Y < 80.0 : In.Y > Frame.TileSizeM - 80.0);
			if (bX && bY) Ids.Add(Home + FIntPoint(dx, dy));
		}
		TMap<FIntPoint, TSharedRef<FOsdsTileBuild>> Built;
		bool bFetched = true;
		for (const FIntPoint& Id : Ids)
		{
			FOsdsAtlasRequest Request;
			Request.Command = TEXT("tile");
			Request.Args = { FString::FromInt(Id.X), FString::FromInt(Id.Y), TEXT("--without-lane-counts") };
			Request.TimeoutSeconds = 120.0;
			const FOsdsAtlasResult Result = FOsdsAtlasClient::RunBlocking(Config, Request, nullptr);
			FOsdsTileData Data;
			FString Error;
			if (!Result.IsOk() || Result.Json->Type != EJson::Object || !OsdsTileData::ParseObject(Result.Json->AsObject(), Data, Error))
			{
				AddWarning(FString::Printf(TEXT("%s: tile %d_%d not fetched (%s%s)"), *T.Id, Id.X, Id.Y, *Result.Describe(), *Error));
				bFetched = false;
				break;
			}
			const TArray<FOsdsSeamEnd> Contracts = OsdsTileBuilder::NeighbourContracts(Id, Frame.TileSizeM, [&Built](const FIntPoint& P) -> const FOsdsTileBuild* { const TSharedRef<FOsdsTileBuild>* F = Built.Find(P); return F ? &F->Get() : nullptr; });
			Built.Add(Id, OsdsTileBuilder::Build(Data, Frame.TileSizeM, Contracts));
		}
		if (!bFetched) continue;
		TArray<FOsdsStartTile> Tiles;
		for (const TPair<FIntPoint, TSharedRef<FOsdsTileBuild>>& Pair : Built) Tiles.Add({ Frame.TileOriginCm(Pair.Key), &Pair.Value.Get() });
		const double Yaw = T.bHasBearing ? OsdsPlaces::BearingToYawDeg(T.BearingDeg) : 1e10;
		FString Why;
		const TArray<FOsdsStartPose> Picks = OsdsStartPick::Candidates(Tiles, OsdsTracks::RacewayStreet, StartCm, 300.0, Yaw, 4, &Why);
		++Checked;
		if (!TestTrue(FString::Printf(TEXT("%s: a start on the track (%s)"), *T.Id, *Why), Picks.Num() > 0)) continue;
		const FOsdsStartPose& P = Picks[0];
		const FOsdsRibbon& R = Tiles[P.Tile].Build->Roads.Ribbons[P.Ribbon];
		TestTrue(FString::Printf(TEXT("%s: on the racing surface (%s, '%s')"), *T.Id, *R.Class, *R.Name), R.bTrack && !R.bPit);
		TestTrue(FString::Printf(TEXT("%s: %.0f m from the start/finish point"), *T.Id, P.DistanceM), P.DistanceM < 60.0);
		if (T.bHasBearing)
		{
			const double Off = FMath::Abs(FMath::FindDeltaAngleDegrees(P.Rotation.Yaw, Yaw));
			TestTrue(FString::Printf(TEXT("%s: facing the racing direction (%.0f deg off)"), *T.Id, Off), Off < 45.0);
		}
		AddInfo(FString::Printf(TEXT("TRACK START %s: %.0f m from the line, %.0f m of track ahead, half width %.1f m, bank %.1f deg"),
			*T.Id, P.DistanceM, P.AheadM, R.St[P.Station].HalfW, FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(R.St[P.Station].E)))));
	}
	TestTrue(TEXT("tracks checked"), Checked > 0 || !Parameters.IsEmpty());
	return true;
}

#endif
