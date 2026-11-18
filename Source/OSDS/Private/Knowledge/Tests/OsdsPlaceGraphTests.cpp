// Copyright OSDS contributors. All Rights Reserved.

#include "Knowledge/OsdsPlaceFacts.h"
#include "Knowledge/OsdsPlaceGraph.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/OsdsGeo.h"
#include "World/OsdsTileData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OsdsPlaceGraphTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	// Recorded 2026-11-12 from overpass-api.de for Ocean Drive (shortened).
	const TCHAR* Overpass = TEXT(R"json({"version":0.6,"elements":[
		{"type":"way","id":40001,"center":{"lat":25.78393,"lon":-80.13011},"tags":{"building":"yes","name":"The Carlyle","wikidata":"Q116250307","addr:street":"Ocean Drive"}},
		{"type":"way","id":40002,"center":{"lat":25.7799,"lon":-80.1312},"tags":{"building":"yes","name":"News Cafe","start_date":"1988"}},
		{"type":"way","id":40003,"center":{"lat":25.781,"lon":-80.132},"tags":{"building":"yes"}},
		{"type":"way","id":40004,"center":{"lat":25.782,"lon":-80.133},"tags":{"building":"apartments","building:colour":"#f3d9d2","roof:shape":"flat","start_date":"~1936"}},
		{"type":"node","id":50001,"lat":25.7801,"lon":-80.1305,"tags":{"amenity":"bar","name":"Mango's"}}]})json");

	// Recorded 2026-11-12 from query.wikidata.org (wikibase:around), two rows for the Carlyle.
	const TCHAR* Wikidata = TEXT(R"json({"head":{"vars":["item"]},"results":{"bindings":[
		{"item":{"type":"uri","value":"http://www.wikidata.org/entity/Q116250307"},"loc":{"type":"literal","value":"Point(-80.1301 25.7839)"},
		 "itemLabel":{"type":"literal","value":"Carlyle Hotel"},"styleLabel":{"type":"literal","value":"Streamline Moderne"},
		 "inception":{"type":"literal","value":"1941-01-01T00:00:00Z"},"architectLabel":{"type":"literal","value":"Richard Kiehnel"},"typeLabel":{"type":"literal","value":"hotel"}},
		{"item":{"type":"uri","value":"http://www.wikidata.org/entity/Q116250307"},"loc":{"type":"literal","value":"Point(-80.1301 25.7839)"},
		 "itemLabel":{"type":"literal","value":"Carlyle Hotel"},"styleLabel":{"type":"literal","value":"Art Deco architecture"},"typeLabel":{"type":"literal","value":"building"}},
		{"item":{"type":"uri","value":"http://www.wikidata.org/entity/Q7601602"},"loc":{"type":"literal","value":"Point(-80.1302 25.7833)"},
		 "itemLabel":{"type":"literal","value":"Stardust Hotel"},"architectLabel":{"type":"literal","value":"Q16067363"},
		 "heritageLabel":{"type":"literal","value":"National Register of Historic Places contributing property"}},
		{"item":{"type":"uri","value":"http://www.wikidata.org/entity/Q6827850"},"loc":{"type":"literal","value":"Point(-80.1340 25.7810)"},
		 "itemLabel":{"type":"literal","value":"Miami Beach Architectural District"},"styleLabel":{"type":"literal","value":"Art Deco"},
		 "typeLabel":{"type":"literal","value":"historic district"}}]}})json");

	bool LoadRules(FAutomationTestBase& Test, FOsdsPlaceRules& Out)
	{
		FString Text, Error;
		if (!FFileHelper::LoadFileToString(Text, *OsdsPlaceGraph::RulesPath()))
		{
			Test.AddError(TEXT("data/knowledge/place-graph.json is missing"));
			return false;
		}
		if (!Out.Parse(Text, Error))
		{
			Test.AddError(FString::Printf(TEXT("place-graph.json: %s"), *Error));
			return false;
		}
		return true;
	}

	/** A square footprint (side m) centred at a point in tile metres. */
	FOsdsBuildingRow Square(const FVector2d& CentreM, double SideM, double TileM, double HeightM)
	{
		FOsdsBuildingRow R;
		const double H = 0.5 * SideM;
		for (const FVector2d& C : { FVector2d(-H, -H), FVector2d(H, -H), FVector2d(H, H), FVector2d(-H, H) }) R.Ring.Add((CentreM + C) / TileM);
		R.HeightM = HeightM;
		return R;
	}

	FOsdsPlaceRecord RecordAt(const FIntPoint& Tile, const FVector2d& CentreM, double TileM)
	{
		FOsdsPlaceRecord R;
		OsdsGeo::TileToLonLat(FVector2d(Tile.X, Tile.Y) + CentreM / TileM, R.Lon, R.Lat);
		return R;
	}
}

using namespace OsdsPlaceGraphTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPlaceGraphRulesTest, "OSDS.Knowledge.RulesInfer", Flags)
bool FOsdsPlaceGraphRulesTest::RunTest(const FString&)
{
	FOsdsPlaceRules Rules;
	if (!LoadRules(*this, Rules)) return false;
	TestTrue(TEXT("the graph has rules, live rules, regions and palettes"), Rules.Rules.Num() > 40 && Rules.LiveRules.Num() > 5 && Rules.Regions.Num() > 5 && Rules.Palettes.Num() > 5);
	TestEqual(TEXT("Wikidata label to token"), Rules.StyleToken(TEXT("Art Deco architecture")), FString(TEXT("art_deco")));
	TestEqual(TEXT("unknown label slug"), Rules.StyleToken(TEXT("Carpenter Gothic architecture")), FString(TEXT("carpenter_gothic")));

	// An Art Deco hotel of the 1930s in Miami Beach.
	FOsdsFactSet F;
	Rules.RegionFacts(25.781, -80.131, F);
	TestTrue(TEXT("Miami Beach is tropical"), F.Has(TEXT("climate:tropical")));
	TestTrue(TEXT("and in the Art Deco district"), F.Has(TEXT("district:art_deco")));
	F.Add(TEXT("style:art_deco"), 1.0f, TEXT("test"));
	F.Add(TEXT("poi:hotel"), 1.0f, TEXT("test"));
	F.Add(TEXT("form:lowrise"), 1.0f, TEXT("test"));
	OsdsPlaceGraph::EraFacts(1936, TEXT("test"), F);
	Rules.Infer(F);
	TestTrue(TEXT("eyebrow ledges"), F.Get(TEXT("detail:eyebrows")) >= 0.85f);
	TestTrue(TEXT("vertical fins"), F.Get(TEXT("detail:vertical_fins")) >= 0.5f);
	TestTrue(TEXT("a neon name sign"), F.Get(TEXT("detail:neon_sign")) >= 0.85f);
	TestTrue(TEXT("pastel stucco"), F.Strongest(TEXT("palette")) == FName(TEXT("palette:deco_pastel")));
	TestTrue(TEXT("the deco recipe"), F.Get(TEXT("recipe:deco")) >= 0.9f);
	TestTrue(TEXT("salt air wear on the coast is not claimed without water"), F.Get(TEXT("detail:salt_wear")) == 0.0f);
	TestTrue(TEXT("the reason is kept"), F.Facts.Find(TEXT("detail:neon_sign"))->Why.Contains(TEXT("poi:hotel")));

	// A desert town far from any district: earth-tone stucco, flat roofs, sun fade.
	FOsdsFactSet D;
	Rules.RegionFacts(33.45, -112.07, D);
	D.Add(TEXT("form:lowrise"), 1.0f, TEXT("test"));
	Rules.Infer(D);
	TestTrue(TEXT("Phoenix is arid"), D.Has(TEXT("climate:arid")));
	TestTrue(TEXT("desert palette"), D.Strongest(TEXT("palette")) == FName(TEXT("palette:desert_earth")));
	TestTrue(TEXT("no neon without a reason"), D.Get(TEXT("detail:neon_sign")) == 0.0f);

	// Anywhere on Earth gets a climate.
	FOsdsFactSet Far;
	Rules.RegionFacts(-33.9, 151.2, Far);
	TestFalse(TEXT("a climate from the latitude band"), Far.Strongest(TEXT("climate")).IsNone());

	// The live rules: neon lights at night, wind takes the awnings in.
	FOsdsBuildingPlan Plan;
	Plan.Facts = F;
	Plan.Facts.Add(TEXT("detail:awnings"), 0.9f, TEXT("test"));
	FOsdsFactSet Live, View;
	Live.Add(TEXT("live:night"), 1.0f, TEXT("test"));
	OsdsPlaceGraph::LiveView(Rules, Plan, Live, View);
	TestTrue(TEXT("neon on at night"), View.Get(TEXT("light:neon_on")) >= 0.8f);
	FOsdsFactSet Windy;
	Windy.Add(TEXT("live:day"), 1.0f, TEXT("test"));
	Windy.Add(TEXT("live:sunny"), 1.0f, TEXT("test"));
	FOsdsFactSet Calm;
	OsdsPlaceGraph::LiveView(Rules, Plan, Windy, Calm);
	Windy.Add(TEXT("live:wind_strong"), 1.0f, TEXT("test"));
	OsdsPlaceGraph::LiveView(Rules, Plan, Windy, View);
	TestTrue(TEXT("awnings out in the sun"), Calm.Get(TEXT("street:awnings_out")) >= 0.8f);
	TestTrue(TEXT("strong wind takes them in"), View.Get(TEXT("street:awnings_out")) < 0.35f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPlaceFactsParseTest, "OSDS.Knowledge.ParseOpenData", Flags)
bool FOsdsPlaceFactsParseTest::RunTest(const FString&)
{
	TArray<FOsdsPlaceRecord> Osm, Wd, All;
	FString Error;
	TestTrue(TEXT("Overpass parses"), OsdsPlaceFacts::ParseOverpass(Overpass, Osm, Error));
	TestTrue(TEXT("Wikidata parses"), OsdsPlaceFacts::ParseWikidata(Wikidata, Wd, Error));
	TestEqual(TEXT("OSM elements with tags"), Osm.Num(), 5);
	TestEqual(TEXT("Wikidata rows join per item"), Wd.Num(), 3);
	TestEqual(TEXT("year from ~1936"), Osm[3].Year, 1936);
	TestEqual(TEXT("bare item ids are not names"), Wd[1].Architect, FString());
	TestTrue(TEXT("the district is marked"), Wd[2].bDistrict);

	OsdsPlaceFacts::Merge(Osm, Wd, All);
	const FOsdsPlaceRecord* Carlyle = All.FindByPredicate([](const FOsdsPlaceRecord& R) { return R.Wikidata == TEXT("Q116250307"); });
	TestNotNull(TEXT("the Carlyle"), Carlyle);
	if (Carlyle)
	{
		TestEqual(TEXT("one record for the Carlyle"), All.FilterByPredicate([](const FOsdsPlaceRecord& R) { return R.Wikidata == TEXT("Q116250307"); }).Num(), 1);
		TestEqual(TEXT("keeps the OSM name and place"), Carlyle->OsmId, FString(TEXT("way/40001")));
		TestEqual(TEXT("both styles"), Carlyle->Styles.Num(), 2);
		TestEqual(TEXT("built 1941"), Carlyle->Year, 1941);
		TestEqual(TEXT("architect"), Carlyle->Architect, FString(TEXT("Richard Kiehnel")));
	}
	TArray<FOsdsPlaceRecord> Useful;
	for (const FOsdsPlaceRecord& R : All) if (OsdsPlaceFacts::IsUseful(R)) Useful.Add(R);
	TestEqual(TEXT("an untagged building and a bare bar are not kept"), Useful.Num(), All.Num() - 2);

	// The cache file round-trips.
	TArray<FOsdsPlaceRecord> Back;
	TestTrue(TEXT("records read back"), OsdsPlaceFacts::FromJson(OsdsPlaceFacts::ToJson(FIntPoint(1, 2), Useful, TEXT("test")), Back, Error));
	TestEqual(TEXT("same count"), Back.Num(), Useful.Num());
	TestTrue(TEXT("queries name the tile box"), OsdsPlaceFacts::OverpassQuery(FIntPoint(18178, 27907)).Contains(TEXT("way[\"building\"]")));
	TestTrue(TEXT("wikidata query is encoded"), OsdsPlaceFacts::WikidataUrl(OsdsPlaceFacts::WikidataQuery(FIntPoint(18178, 27907))).Contains(TEXT("wikibase%3Aaround")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPlaceGraphBakeTest, "OSDS.Knowledge.BakeTile", Flags)
bool FOsdsPlaceGraphBakeTest::RunTest(const FString&)
{
	FOsdsPlaceRules Rules;
	if (!LoadRules(*this, Rules)) return false;
	// A tile outside any district (Orlando, humid subtropical): one dated building, three
	// neighbours without data, one far away.
	const double Lat = 28.54, Lon = -81.38;
	const FVector2d T = OsdsGeo::LonLatToTile(Lon, Lat);
	FOsdsTileData Data;
	Data.Id = FIntPoint(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y));
	const double Side = OsdsGeo::TileSizeMetres(Lat);
	Data.Buildings.Add(Square(FVector2d(200, 200), 20, Side, 10));  // 0: the dated hotel
	Data.Buildings.Add(Square(FVector2d(240, 200), 18, Side, 9));   // 1
	Data.Buildings.Add(Square(FVector2d(200, 250), 16, Side, 8));   // 2
	Data.Buildings.Add(Square(FVector2d(160, 190), 16, Side, 8));   // 3
	Data.Buildings.Add(Square(FVector2d(480, 480), 30, Side, 40));  // 4: far, a tower
	TArray<FOsdsPlaceRecord> Records;
	FOsdsPlaceRecord A = RecordAt(Data.Id, FVector2d(201, 199), Side);
	A.Name = TEXT("Test Hotel");
	A.Wikidata = TEXT("Q1");
	A.Styles = { TEXT("Art Deco architecture") };
	A.Year = 1937;
	A.Building = TEXT("hotel");
	A.Heritage = TEXT("National Register of Historic Places contributing property");
	Records.Add(A);
	FOsdsPlaceRecord B = RecordAt(Data.Id, FVector2d(240, 200), Side);
	B.Styles = { TEXT("Art Deco") };
	B.Year = 1939;
	Records.Add(B);
	FOsdsPlaceRecord C = RecordAt(Data.Id, FVector2d(200, 251), Side);
	C.Colour = TEXT("#D5E6F0");
	Records.Add(C);

	FOsdsTilePlan Plan;
	OsdsPlaceGraph::BakeTile(Rules, Data, Lat, Lon, Side, Records, Plan);
	TestEqual(TEXT("one plan per building"), Plan.Buildings.Num(), 5);
	TestEqual(TEXT("three records matched"), Plan.Records, 3);
	const FOsdsBuildingPlan& Hotel = Plan.Buildings[0];
	TestEqual(TEXT("its own style"), Hotel.Style, FName(TEXT("style:art_deco")));
	TestEqual(TEXT("year"), Hotel.Year, 1937);
	TestTrue(TEXT("neon name sign"), Hotel.P(TEXT("detail:neon_sign")) >= 0.85f);
	TestTrue(TEXT("restored, so little wear"), Hotel.P(TEXT("detail:restored")) >= 0.8f);
	TestTrue(TEXT("deco recipe"), Hotel.IsRecipe(TEXT("deco")));
	TestTrue(TEXT("a pastel wall"), Hotel.bHasColour && Hotel.Palette == FName(TEXT("palette:deco_pastel")));

	const FOsdsBuildingPlan& Next = Plan.Buildings[3];
	TestEqual(TEXT("a neighbour without data takes the street's style"), Next.Style, FName(TEXT("style:art_deco")));
	TestTrue(TEXT("with less confidence"), Next.StyleConfidence > 0.4f && Next.StyleConfidence < 0.9f);
	TestTrue(TEXT("and the era band"), Next.P(TEXT("era:before_1945")) > 0.4f);
	TestTrue(TEXT("the reason says spread"), Next.Facts.Facts.Find(TEXT("style:art_deco"))->Why.Contains(TEXT("spread")));
	TestTrue(TEXT("the mapped colour wins"), Plan.Buildings[2].bColourFromData && Plan.Buildings[2].Wall.Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("D5E6F0"))), 0.03f));
	TestTrue(TEXT("the far tower keeps no style"), Plan.Buildings[4].Style.IsNone() || Plan.Buildings[4].StyleConfidence < 0.3f);
	TestTrue(TEXT("climate everywhere"), Plan.Buildings[4].Facts.Has(TEXT("climate:humid_subtropical")));
	TestTrue(TEXT("fast"), Plan.BakeMs < 20.0);

	// Deterministic: the same input bakes the same plan.
	FOsdsTilePlan Again;
	OsdsPlaceGraph::BakeTile(Rules, Data, Lat, Lon, Side, Records, Again);
	TestTrue(TEXT("same colours"), Again.Buildings[1].Wall.Equals(Plan.Buildings[1].Wall));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPlaceGraphBundledTest, "OSDS.Knowledge.BundledOceanDrive", Flags)
bool FOsdsPlaceGraphBundledTest::RunTest(const FString&)
{
	// The bundled start: every bundled tile bakes, and the Ocean Drive hotels known to
	// Wikidata get their real style.
	FOsdsPlaceRules Rules;
	if (!LoadRules(*this, Rules)) return false;
	int32 Tiles = 0, Matched = 0, Deco = 0, Buildings = 0;
	double Slowest = 0.0;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(OsdsTileData::RepositoryRoot(), TEXT("data/tiles/*.json")), true, false);
	for (const FString& F : Files)
	{
		FOsdsTileData Data;
		FString Error;
		if (!OsdsTileData::LoadFile(FPaths::Combine(OsdsTileData::RepositoryRoot(), TEXT("data/tiles"), F), Data, Error)) continue;
		double Lon, Lat;
		OsdsGeo::TileToLonLat(FVector2d(Data.Id.X + 0.5, Data.Id.Y + 0.5), Lon, Lat);
		TArray<FOsdsPlaceRecord> Records;
		OsdsPlaceFacts::ForTile(Data.Id, Records);
		FOsdsTilePlan Plan;
		OsdsPlaceGraph::BakeTile(Rules, Data, Lat, Lon, OsdsGeo::TileSizeMetres(Lat), Records, Plan);
		++Tiles;
		Matched += Plan.Matched;
		Buildings += Plan.Buildings.Num();
		Slowest = FMath::Max(Slowest, Plan.BakeMs);
		for (const FOsdsBuildingPlan& B : Plan.Buildings) if (B.IsRecipe(TEXT("deco"))) ++Deco;
	}
	AddInfo(FString::Printf(TEXT("%d tiles, %d buildings, %d with open data, %d deco, slowest bake %.2f ms"), Tiles, Buildings, Matched, Deco, Slowest));
	TestTrue(TEXT("bundled tiles bake"), Tiles > 0);
	TestTrue(TEXT("Ocean Drive buildings carry their own open data"), Matched >= 20);
	TestTrue(TEXT("the district is Art Deco"), Deco >= 100);
	TestTrue(TEXT("each tile bakes in real time"), Slowest < 50.0);
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Knowledge/OsdsPlaceGraphSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPlaceCardTest, "OSDS.Knowledge.PlaceCard", OsdsPlaceGraphTests::Flags)
bool FOsdsPlaceCardTest::RunTest(const FString&)
{
	FOsdsBuildingPlan P;
	FOsdsPlaceCard C;
	TestFalse(TEXT("no name, no card"), UOsdsPlaceGraphSubsystem::MakeCard(P, C));
	P.Name = TEXT("The Carlyle");
	TestFalse(TEXT("a name alone is not enough"), UOsdsPlaceGraphSubsystem::MakeCard(P, C));
	P.Style = TEXT("style:streamline_moderne");
	P.StyleConfidence = 1.0f;
	P.Year = 1941;
	P.Architect = TEXT("Richard Kiehnel");
	P.Facts.Add(TEXT("heritage:nrhp"), 1.0f, TEXT("test"));
	TestTrue(TEXT("card"), UOsdsPlaceGraphSubsystem::MakeCard(P, C));
	TestEqual(TEXT("title"), C.Title, FString(TEXT("THE CARLYLE")));
	TestTrue(TEXT("style label"), C.Line.StartsWith(TEXT("Streamline Moderne")));
	TestTrue(TEXT("year and architect"), C.Line.Contains(TEXT("1941")) && C.Line.EndsWith(TEXT("Richard Kiehnel")));
	TestEqual(TEXT("heritage"), C.Note, FString(TEXT("National Register of Historic Places")));
	P.StyleConfidence = 0.6f; // spread from neighbours: not said as fact
	P.Year = 0;
	P.Architect.Reset();
	TestFalse(TEXT("a guessed style is not a card"), UOsdsPlaceGraphSubsystem::MakeCard(P, C));
	TestEqual(TEXT("MiMo"), UOsdsPlaceGraphSubsystem::StyleLabel(TEXT("style:mimo")), FString(TEXT("MiMo (Miami Modern)")));
	return true;
}
#endif
