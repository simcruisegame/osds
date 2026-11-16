// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseTileDetail.h"
#include "World/SimCruiseWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseBuildingTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSignTextTest, "SimCruise.World.Buildings.SignText", SimCruiseBuildingTest::Flags)
bool FSimCruiseSignTextTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseTileDetail;
	// The names as the map has them on Ocean Drive, and the words on the real signs.
	TestEqual(TEXT("Cardozo"), SignText(TEXT("Cardozo Hotel South Beach"), true), FString(TEXT("CARDOZO")));
	TestEqual(TEXT("Leslie"), SignText(TEXT("Leslie Hotel"), true), FString(TEXT("LESLIE")));
	TestEqual(TEXT("The Carlyle"), SignText(TEXT("The Carlyle"), true), FString(TEXT("THE CARLYLE")));
	TestEqual(TEXT("Hotel Victor"), SignText(TEXT("Hotel Victor"), true), FString(TEXT("VICTOR")));
	TestEqual(TEXT("a branch tail"), SignText(TEXT("Mercato Di Mare - Ocean Drive"), false), FString(TEXT("Mercato Di Mare")));
	TestEqual(TEXT("a bar keeps its case"), SignText(TEXT("Finnegan's Way"), false), FString(TEXT("Finnegan's Way")));
	TestTrue(TEXT("long names are cut at a word"), SignText(TEXT("Courtyard Apartments Part of the Oasis Casita Collection"), true).Len() <= 22);

	TestTrue(TEXT("hotels get neon and rank first"), SignStyleFor(TEXT("lodging")).bNeon && SignStyleFor(TEXT("lodging")).Rank == 3);
	TestTrue(TEXT("restaurants get painted letters"), !SignStyleFor(TEXT("restaurant")).bNeon && SignStyleFor(TEXT("restaurant")).Rank == 1);
	TestTrue(TEXT("bus stops get none"), SignStyleFor(TEXT("bus")).Rank == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseWallMaterialTest, "SimCruise.World.Buildings.WallMaterial", SimCruiseBuildingTest::Flags)
bool FSimCruiseWallMaterialTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseTileDetail;
	namespace M = SimCruiseWallMaterial;
	// Data wins: a mapped material is drawn as it is, whatever the region says.
	{
		FWallMaterialInput In;
		In.LatDeg = 33.45; In.LonDeg = -112.07; In.HeightM = 6.0; In.AreaM2 = 180.0; In.bHouse = true; // Phoenix
		In.MaterialFact = TEXT("material:brick");
		bool bData = false;
		TestEqual(TEXT("mapped brick in Phoenix"), WallMaterialFor(In, &bData), M::Brick);
		TestTrue(TEXT("... from data"), bData);
		In.MaterialFact = TEXT("material:vinyl_siding");
		TestEqual(TEXT("mapped vinyl siding"), WallMaterialFor(In), M::Siding);
		In.MaterialFact = TEXT("material:limestone");
		TestEqual(TEXT("mapped limestone"), WallMaterialFor(In), M::Stone);
	}
	// The Art Deco hotels, resort towers and curtain walls keep their own look.
	{
		FWallMaterialInput In;
		In.LatDeg = 25.78; In.LonDeg = -80.13; In.bDeco = true; In.MaterialFact = TEXT("material:brick");
		TestEqual(TEXT("deco stays render"), WallMaterialFor(In), M::Render);
	}
	// The rules, over 2,000 houses each: the US Census Survey of Construction's regional picture.
	auto Share = [](double Lat, double Lon, const TCHAR* Use, bool bHouse, double AreaM2, double HeightM, int32 Material)
	{
		int32 N = 0;
		for (uint32 i = 0; i < 2000; ++i)
		{
			FWallMaterialInput In;
			In.LatDeg = Lat; In.LonDeg = Lon; In.Use = Use; In.bHouse = bHouse; In.AreaM2 = AreaM2; In.HeightM = HeightM;
			In.Hash = HashCombine(i * 2654435761u, 0x9E3779B9u);
			N += WallMaterialFor(In) == Material ? 1 : 0;
		}
		return N / 2000.0;
	};
	TestTrue(TEXT("Columbus OH houses mostly siding"), Share(39.96, -83.0, TEXT("house"), true, 160.0, 7.0, M::Siding) > 0.45);
	TestTrue(TEXT("Atlanta houses mostly brick"), Share(33.75, -84.39, TEXT("house"), true, 160.0, 7.0, M::Brick) > 0.4);
	TestTrue(TEXT("Phoenix houses mostly stucco"), Share(33.45, -112.07, TEXT("house"), true, 160.0, 7.0, M::Render) > 0.7);
	TestTrue(TEXT("Portland OR houses mostly siding"), Share(45.52, -122.68, TEXT("house"), true, 160.0, 7.0, M::Siding) > 0.65);
	TestTrue(TEXT("warehouses metal or block"), Share(39.96, -83.0, TEXT("warehouse"), false, 4000.0, 9.0, M::Metal) + Share(39.96, -83.0, TEXT("warehouse"), false, 4000.0, 9.0, M::Block) > 0.8);
	TestTrue(TEXT("no siding on a big box"), Share(33.75, -84.39, TEXT("retail"), false, 6000.0, 8.0, M::Siding) < 0.01);
	// The same building always gets the same material.
	{
		FWallMaterialInput In;
		In.LatDeg = 42.33; In.LonDeg = -83.05; In.Use = TEXT("commercial"); In.HeightM = 9.0; In.AreaM2 = 600.0; In.Hash = 12345u;
		const int32 A = WallMaterialFor(In), B = WallMaterialFor(In);
		TestEqual(TEXT("deterministic"), A, B);
	}
	// A window role stays under half (the shaders round the kind) and decodes to its share and pairs.
	for (int32 q = 0; q <= 9; ++q)
	for (int32 p = 0; p <= 1; ++p)
	{
		const float F = SimCruiseWindowRole::Encode(q / 9.0, p == 1);
		TestTrue(TEXT("role under half a kind"), F > 0.005f && F < 0.5f);
		const int32 Code = FMath::FloorToInt(F * 48.f);
		TestEqual(TEXT("role share"), Code % 10, q);
		TestEqual(TEXT("role pairs"), Code >= 10, p == 1);
		TestEqual(TEXT("kind survives"), FMath::RoundToFloat(SimCruiseSurfaceKind::WallFront + F), SimCruiseSurfaceKind::WallFront);
	}
	// The packed seed keeps its material and its seed bin.
	for (int32 m = 0; m <= M::Metal; ++m)
	{
		const float P = M::Pack(m, 0.73f);
		TestEqual(TEXT("material from the integer part"), FMath::FloorToInt(P), m);
		TestTrue(TEXT("seed mid-bin"), FMath::Abs(FMath::Frac(P) * 4096.f - (FMath::FloorToFloat(0.73f * 4096.f) + 0.5f)) < 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseWallAlbedoTest, "SimCruise.World.Buildings.WallAlbedo", SimCruiseBuildingTest::Flags)
bool FSimCruiseWallAlbedoTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseTileDetail;
	// Weathered paint: a 0.88 white comes down to the cap with its hue; darker colours stay.
	const FLinearColor White = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("F5F1E6")));
	const FLinearColor Capped = WeatheredPaint(White);
	TestTrue(FString::Printf(TEXT("white capped at %.2f (%.3f)"), MaxWallAlbedo, Capped.GetLuminance()), FMath::IsNearlyEqual(Capped.GetLuminance(), MaxWallAlbedo, 0.005f));
	TestTrue(TEXT("hue kept"), FMath::IsNearlyEqual(Capped.R / Capped.B, White.R / White.B, 1e-3f));
	const FLinearColor Pink = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("F4B39A")));
	TestTrue(TEXT("a colour under the cap is unchanged"), WeatheredPaint(Pink).Equals(Pink));
	TestTrue(TEXT("trim cap"), WeatheredPaint(FLinearColor::White, MaxTrimAlbedo).GetLuminance() <= MaxTrimAlbedo + 1e-3f);
	TestTrue(TEXT("tower cap"), WeatheredPaint(FLinearColor::White, MaxTowerAlbedo).GetLuminance() <= MaxTowerAlbedo + 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoundCornersTest, "SimCruise.World.Buildings.RoundCorners", SimCruiseBuildingTest::Flags)
bool FSimCruiseRoundCornersTest::RunTest(const FString& Parameters)
{
	// A 20 x 12 m block, counter-clockwise in (x, y); only the first edge (y = 0) is a street front.
	const TArray<FVector2d> Ring = { FVector2d(0, 0), FVector2d(20, 0), FVector2d(20, 12), FVector2d(0, 12) };
	double Area = 0.0;
	for (int32 i = 0; i < 4; ++i) Area += Ring[i].X * Ring[(i + 1) % 4].Y - Ring[i].Y * Ring[(i + 1) % 4].X;
	const double Sign = Area >= 0.0 ? 1.0 : -1.0;
	const TArray<bool> Front = { true, false, false, false };
	const TArray<bool> Keep = { false, false, false, false };
	TArray<FVector2d> Out;
	TArray<int32> Source;
	SimCruiseTileDetail::RoundCorners(Ring, Sign, Front, Keep, 2.0, Out, Source);
	// The two corners of the front round (5 points each); the back two stay square.
	TestEqual(TEXT("points"), Out.Num(), 12);
	int32 Arcs = 0;
	for (const int32 S : Source) Arcs += S == INDEX_NONE ? 1 : 0;
	TestEqual(TEXT("arc chords"), Arcs, 8);
	// Every arc point keeps the radius from its centre and stays inside the footprint.
	for (const FVector2d& P : Out)
	{
		TestTrue(FString::Printf(TEXT("%s inside"), *P.ToString()), P.X >= -1e-6 && P.X <= 20.0 + 1e-6 && P.Y >= -1e-6 && P.Y <= 12.0 + 1e-6);
	}
	const FVector2d Mid = Out[2]; // the middle of the first corner's arc (centre (2, 2), radius 2)
	TestTrue(FString::Printf(TEXT("arc radius (%.3f)"), FVector2d::Distance(Mid, FVector2d(2, 2))), FMath::IsNearlyEqual(FVector2d::Distance(Mid, FVector2d(2, 2)), 2.0, 1e-6));

	// A kept edge (a porch) keeps its corners square.
	const TArray<bool> KeepFront = { true, false, false, false };
	SimCruiseTileDetail::RoundCorners(Ring, Sign, Front, KeepFront, 2.0, Out, Source);
	TestEqual(TEXT("porch front stays square"), Out.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseOceanDriveSignsTest, "SimCruise.World.Buildings.OceanDriveSigns", SimCruiseBuildingTest::Flags)
bool FSimCruiseOceanDriveSignsTest::RunTest(const FString& Parameters)
{
	// The start tile: its hotels carry their names on their own fronts.
	FSimCruiseTileData Data;
	FString Error;
	if (!TestTrue(TEXT("tile loads"), SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(FIntPoint(18180, 27907)), Data, Error))) return false;
	TestTrue(TEXT("places read"), Data.Pois.Num() > 50);
	const double Side = FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat).TileSizeM;
	const TSharedRef<FSimCruiseTileBuild> Build = SimCruiseTileBuilder::Build(Data, Side);
	AddInfo(FString::Printf(TEXT("%d signs on %d buildings"), Build->Signs.Num(), Build->Buildings));
	TestTrue(TEXT("dozens of signs"), Build->Signs.Num() > 30);
	for (const TCHAR* Hotel : { TEXT("CARDOZO"), TEXT("LESLIE"), TEXT("THE CARLYLE"), TEXT("CAVALIER") })
	{
		const FSimCruiseSign* Found = Build->Signs.FindByPredicate([&](const FSimCruiseSign& S) { return S.Text == Hotel; });
		if (!TestNotNull(FString::Printf(TEXT("%s is signed"), Hotel), Found)) continue;
		TestTrue(FString::Printf(TEXT("%s is neon"), Hotel), Found->bNeon);
		// Ocean Drive runs north-south with the hotels on its west side: their fronts face east (yaw ~0);
		// the letters on a blade standing out of that front face north and south.
		const float Yaw = FMath::Abs(FRotator::NormalizeAxis(Found->YawDeg));
		const bool bFacesOcean = Found->bStacked ? (Yaw < 30.f || FMath::Abs(Yaw - 90.f) < 30.f) : Yaw < 30.f;
		TestTrue(FString::Printf(TEXT("%s faces the ocean or stands on a blade out of its ocean front (yaw %.0f)"), Hotel, Found->YawDeg), bFacesOcean);
		TestTrue(FString::Printf(TEXT("%s is above head height (%.1f m)"), Hotel, Found->LocationCm.Z / 100.0), Found->LocationCm.Z / 100.0 > 2.5);
	}
	return true;
}

#endif
