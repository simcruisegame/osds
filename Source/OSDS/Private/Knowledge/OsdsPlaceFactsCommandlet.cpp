// Copyright OSDS contributors. All Rights Reserved.

#include "Knowledge/OsdsPlaceFactsCommandlet.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Live/OsdsSceneParser.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Knowledge/OsdsPlaceAI.h"
#include "Knowledge/OsdsPlaceFacts.h"
#include "Knowledge/OsdsFurnitureData.h"
#include "Knowledge/OsdsPlacePhotos.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/OsdsGeo.h"
#include "World/OsdsTileData.h"
#include "Atlas/OsdsUserAgent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsPlaceFactsCommandlet, Log, All);

namespace
{
	/** A blocking HTTP call for the commandlet (ticks the HTTP manager itself). */
	bool Fetch(const FString& Url, const FString& Verb, const FString& Body, const FString& Accept, FString& Out, float Timeout = 60.0f, const TCHAR* ContentType = TEXT("application/x-www-form-urlencoded"))
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(Url);
		Req->SetVerb(Verb);
		Req->SetHeader(TEXT("User-Agent"), OsdsUserAgent::Get());
		Req->SetHeader(TEXT("Accept"), Accept);
		if (!Body.IsEmpty())
		{
			Req->SetHeader(TEXT("Content-Type"), ContentType);
			Req->SetContentAsString(Body);
		}
		Req->SetTimeout(Timeout);
		Req->SetActivityTimeout(Timeout); // a local model answers only when done
		bool bDone = false, bOk = false;
		Req->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			bOk = bConnected && Response.IsValid() && Response->GetResponseCode() == 200;
			if (bOk) Out = Response->GetContentAsString();
			else UE_LOG(LogOsdsPlaceFactsCommandlet, Warning, TEXT("%s: %d"), *Url.Left(60), Response.IsValid() ? Response->GetResponseCode() : 0);
			bDone = true;
		});
		Req->ProcessRequest();
		const double End = FPlatformTime::Seconds() + Timeout + 10.0;
		while (!bDone && FPlatformTime::Seconds() < End)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.05f);
			FPlatformProcess::Sleep(0.05f);
		}
		return bOk;
	}
}

namespace
{
	bool FetchBytes(const FString& Url, TArray<uint8>& Out)
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(Url);
		Req->SetVerb(TEXT("GET"));
		Req->SetHeader(TEXT("User-Agent"), OsdsUserAgent::Get());
		Req->SetTimeout(60.0f);
		bool bDone = false, bOk = false;
		Req->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			bOk = bConnected && Response.IsValid() && Response->GetResponseCode() == 200;
			if (bOk) Out = Response->GetContent();
			bDone = true;
		});
		Req->ProcessRequest();
		const double End = FPlatformTime::Seconds() + 70.0;
		while (!bDone && FPlatformTime::Seconds() < End)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.05f);
			FPlatformProcess::Sleep(0.05f);
		}
		return bOk;
	}
}

UOsdsPlaceFactsCommandlet::UOsdsPlaceFactsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UOsdsPlaceFactsCommandlet::Main(const FString& Params)
{
	TArray<FIntPoint> Tiles;
	FString List;
	if (FParse::Value(*Params, TEXT("Tiles="), List))
	{
		TArray<FString> Parts;
		List.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts)
		{
			FString X, Y;
			if (P.Split(TEXT("_"), &X, &Y)) Tiles.Add(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y)));
		}
	}
	else
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(OsdsTileData::RepositoryRoot(), TEXT("data/tiles/*.json")), true, false);
		for (const FString& F : Files)
		{
			FString X, Y;
			if (FPaths::GetBaseFilename(F).Split(TEXT("_"), &X, &Y)) Tiles.Add(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y)));
		}
	}
	const bool bAll = FParse::Param(*Params, TEXT("All"));
	int32 Failures = 0;
	const bool bDoOsm = !FParse::Param(*Params, TEXT("NoOSM"));
	for (const FIntPoint& Tile : (bDoOsm ? Tiles : TArray<FIntPoint>()))
	{
		// A tile already fetched from both sources is kept (-All fetches again).
		FString Old;
		const FString Path = OsdsPlaceFacts::BundledPath(Tile);
		if (!bAll && FFileHelper::LoadFileToString(Old, *Path) && !Old.Contains(TEXT("\"overpass:0 ")) && !Old.Contains(TEXT("wikidata:0\"")))
		{
			continue;
		}
		FString OsmText, WdText, Error;
		TArray<FOsdsPlaceRecord> Osm, Wd, All, Useful;
		bool bOsm = false;
		for (int32 Attempt = 0; Attempt < 6 && !bOsm; ++Attempt)
		{
			if (Attempt > 0) FPlatformProcess::Sleep(5.0f * Attempt);
			Osm.Reset();
			bOsm = Fetch(OsdsPlaceFacts::OverpassUrl(Attempt), TEXT("POST"), TEXT("data=") + FGenericPlatformHttp::UrlEncode(OsdsPlaceFacts::OverpassQuery(Tile)),
				TEXT("application/json"), OsmText) && OsdsPlaceFacts::ParseOverpass(OsmText, Osm, Error);
		}
		const bool bWd = Fetch(OsdsPlaceFacts::WikidataUrl(OsdsPlaceFacts::WikidataQuery(Tile)), TEXT("GET"), FString(),
			TEXT("application/sparql-results+json"), WdText) && OsdsPlaceFacts::ParseWikidata(WdText, Wd, Error);
		if (!bOsm || !bWd)
		{
			UE_LOG(LogOsdsPlaceFactsCommandlet, Warning, TEXT("%s: OSM %s, Wikidata %s %s"), *OsdsTileName(Tile), bOsm ? TEXT("ok") : TEXT("failed"), bWd ? TEXT("ok") : TEXT("failed"), *Error);
			++Failures;
			if (!bOsm && !bWd) continue;
		}
		OsdsPlaceFacts::Merge(Osm, Wd, All);
		for (FOsdsPlaceRecord& R : All) if (OsdsPlaceFacts::IsUseful(R)) Useful.Add(MoveTemp(R));
		FFileHelper::SaveStringToFile(OsdsPlaceFacts::ToJson(Tile, Useful, FString::Printf(TEXT("overpass:%d wikidata:%d"), Osm.Num(), Wd.Num())), *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogOsdsPlaceFactsCommandlet, Display, TEXT("%s: %d useful of %d OSM + %d Wikidata -> %s"), *OsdsTileName(Tile), Useful.Num(), Osm.Num(), Wd.Num(), *Path);
		FPlatformProcess::Sleep(2.0f);
	}
	// -Furniture: the street furniture of these tiles (data/knowledge/furniture).
	if (FParse::Param(*Params, TEXT("Furniture")))
	{
		int32 Total = 0;
		for (const FIntPoint& Tile : Tiles)
		{
			if (!bAll && FPaths::FileExists(OsdsFurniture::BundledPath(Tile))) continue;
			double S, W, N, E;
			OsdsGeo::TileToLonLat(FVector2d(Tile.X, Tile.Y), W, N);
			OsdsGeo::TileToLonLat(FVector2d(Tile.X + 1, Tile.Y + 1), E, S);
			const FString Box = FString::Printf(TEXT("(%.6f,%.6f,%.6f,%.6f)"), S, W, N, E);
			const FString Query = FString::Printf(TEXT("[out:json][timeout:25];(%s);out 3000;"), *OsdsFurniture::OverpassLines(Box));
			FString Text, Error;
			TArray<FOsdsFurniture> Items;
			bool bOk = false;
			for (int32 Attempt = 0; Attempt < 6 && !bOk; ++Attempt)
			{
				if (Attempt > 0) FPlatformProcess::Sleep(5.0f * Attempt);
				Items.Reset();
				bOk = Fetch(OsdsPlaceFacts::OverpassUrl(Attempt), TEXT("POST"), TEXT("data=") + FGenericPlatformHttp::UrlEncode(Query), TEXT("application/json"), Text)
					&& OsdsFurniture::ParseOverpass(Text, Items, Error);
			}
			if (!bOk) { ++Failures; UE_LOG(LogOsdsPlaceFactsCommandlet, Warning, TEXT("%s: furniture failed"), *OsdsTileName(Tile)); continue; }
			FFileHelper::SaveStringToFile(OsdsFurniture::ToJson(Tile, Items), *OsdsFurniture::BundledPath(Tile), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			Total += Items.Num();
			UE_LOG(LogOsdsPlaceFactsCommandlet, Display, TEXT("%s: %d furniture items"), *OsdsTileName(Tile), Items.Num());
			FPlatformProcess::Sleep(2.0f);
		}
		UE_LOG(LogOsdsPlaceFactsCommandlet, Display, TEXT("Furniture: %d items."), Total);
	}
	// -Photos: each building with its own Wikidata photograph gets its wall colour measured.
	if (FParse::Param(*Params, TEXT("Photos")))
	{
		// The local scene parser (CPU) finds the wall pixels; -Remeasure measures again the
		// colours measured before (a mapped colour is never touched).
		FOsdsSceneParser::Get().Initialize();
		const bool bRemeasure = FParse::Param(*Params, TEXT("Remeasure"));
		// -AlbedoFromPavement: darken over-exposed walls on the photograph's pavement (experimental).
		const bool bAlbedo = FParse::Param(*Params, TEXT("AlbedoFromPavement"));
		int32 Measured = 0, Tried = 0;
		for (const FIntPoint& Tile : Tiles)
		{
			FString Text, Error;
			TArray<FOsdsPlaceRecord> Records;
			const FString Path = OsdsPlaceFacts::BundledPath(Tile);
			if (!FFileHelper::LoadFileToString(Text, *Path) || !OsdsPlaceFacts::FromJson(Text, Records, Error)) continue;
			// The photographs (P18) of the tile's Wikidata items, joined by id.
			FString WdText;
			TArray<FOsdsPlaceRecord> Wd;
			if (Fetch(OsdsPlaceFacts::WikidataUrl(OsdsPlaceFacts::WikidataQuery(Tile)), TEXT("GET"), FString(), TEXT("application/sparql-results+json"), WdText))
			{
				OsdsPlaceFacts::ParseWikidata(WdText, Wd, Error);
			}
			for (FOsdsPlaceRecord& R : Records)
			{
				if (!R.Wikidata.IsEmpty())
				{
					for (const FOsdsPlaceRecord& W : Wd)
					{
						if (W.Wikidata == R.Wikidata) for (const FString& F : OsdsPlacePhotos::Images(W.Image)) OsdsPlacePhotos::AddImage(R.Image, F);
					}
				}
			}
			bool bChanged = false;
			for (FOsdsPlaceRecord& R : Records)
			{
				// A mapped colour wins; a measured one is kept.
				const bool bMeasuredBefore = R.ColourSource.StartsWith(TEXT("measured in File:"));
				if (R.Image.IsEmpty() || (!R.Colour.IsEmpty() && !(bRemeasure && bMeasuredBefore))) continue;
				bChanged = true;
				++Tried;
				// Each of its photographs in turn, until one is a recent colour photo with a clear wall.
				for (const FString& File : OsdsPlacePhotos::Images(R.Image))
				{
					FString InfoText;
					FOsdsPhotoInfo Info;
					if (!Fetch(OsdsPlacePhotos::ImageInfoUrl(File), TEXT("GET"), FString(), TEXT("application/json"), InfoText)
						|| !OsdsPlacePhotos::ParseImageInfo(InfoText, Info, Error))
					{
						continue;
					}
					if (!OsdsPlacePhotos::IsUsable(Info))
					{
						UE_LOG(LogOsdsPlaceFactsCommandlet, Display, TEXT("%s: %s left out (%d%s)"), *R.Name, *File, Info.Year, Info.bPostcard ? TEXT(", postcard") : TEXT(""));
						continue;
					}
					TArray<uint8> Bytes;
					if (!FetchBytes(Info.ThumbUrl, Bytes)) continue;
					TArray<FColor> Pixels;
					int32 W = 0, H = 0;
					FColor Wall;
					float Share = 0.0f;
					FPlatformProcess::Sleep(0.3f);
					if (!OsdsPlacePhotos::DecodeImage(Bytes, Pixels, W, H)) continue;
					// The local scene parser finds the wall pixels when it can; the colour rule otherwise.
					TArray<uint8> Labels;
					int32 GW = 0, GH = 0;
					const bool bParsed = FOsdsSceneParser::Get().IsReady() && FOsdsSceneParser::Get().ParseWithTimeout(Pixels, W, H, Labels, GW, GH);
					bool bNormalised = false;
					const bool bMasked = bParsed && OsdsPlacePhotos::WallColourMasked(Pixels, W, H, Labels, GW, GH, Wall, Share, bNormalised, bAlbedo);
					if (bMasked || (!bParsed && OsdsPlacePhotos::WallColour(Pixels, W, H, Wall, Share)))
					{
						OsdsPlacePhotos::Apply(R, Wall, Share, Info);
						if (bMasked)
						{
							R.ColourSource += bNormalised ? TEXT("; wall pixels found by the local scene parser; albedo from the pavement in the photo")
								: TEXT("; wall pixels found by the local scene parser");
						}
						++Measured;
						UE_LOG(LogOsdsPlaceFactsCommandlet, Display, TEXT("%s: wall %s %s"), *R.Name, *R.Colour, *R.ColourSource);
						break;
					}
				}
			}
			if (bChanged)
			{
				FString Sources = TEXT("bundled");
				TSharedPtr<FJsonObject> Old;
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Old) && Old.IsValid()) Old->TryGetStringField(TEXT("sources"), Sources);
				FFileHelper::SaveStringToFile(OsdsPlaceFacts::ToJson(Tile, Records, Sources + TEXT(" photos")), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			}
		}
		UE_LOG(LogOsdsPlaceFactsCommandlet, Display, TEXT("Photos: %d walls measured of %d buildings with a photograph."), Measured, Tried);
	}
	// -AI: the local model reads each area of these tiles (data/knowledge/areas).
	if (FParse::Param(*Params, TEXT("AI")))
	{
		FString Tags;
		const FString Model = Fetch(OsdsPlaceAI::OllamaUrl() + TEXT("/api/tags"), TEXT("GET"), FString(), TEXT("application/json"), Tags, 10.0f)
			? OsdsPlaceAI::PickModel(Tags) : FString();
		if (Model.IsEmpty())
		{
			UE_LOG(LogOsdsPlaceFactsCommandlet, Warning, TEXT("No local model at %s."), *OsdsPlaceAI::OllamaUrl());
			return 1;
		}
		TSet<FIntPoint> Areas;
		for (const FIntPoint& Tile : Tiles) Areas.Add(OsdsPlaceAI::AreaOf(Tile));
		for (const FIntPoint& Area : Areas)
		{
			double Lat, Lon;
			OsdsPlaceAI::AreaCentre(Area, Lat, Lon);
			FString Geo, Ex, Chat, Error;
			TArray<int64> Ids;
			FOsdsAreaPrior Prior;
			Prior.Model = Model;
			TArray<FString> Lines;
			if (!Fetch(OsdsPlaceAI::GeosearchUrl(Lat, Lon), TEXT("GET"), FString(), TEXT("application/json"), Geo) || !OsdsPlaceAI::ParseGeosearch(Geo, Ids, Error)) { ++Failures; continue; }
			if (Ids.Num() > 0)
			{
				if (!Fetch(OsdsPlaceAI::ExtractsUrl(Ids), TEXT("GET"), FString(), TEXT("application/json"), Ex) || !OsdsPlaceAI::ParseExtracts(Ex, Ids, Lines, Prior.Sources, Error)) { ++Failures; continue; }
				const TArray<FString> Sources = Prior.Sources;
				const double T0 = FPlatformTime::Seconds();
				if (!Fetch(OsdsPlaceAI::OllamaUrl() + TEXT("/api/chat"), TEXT("POST"), OsdsPlaceAI::ChatRequest(OsdsPlaceGraph::Rules(), Model, FString(), Lat, Lon, Lines),
					TEXT("application/json"), Chat, 600.0f, TEXT("application/json")) || !OsdsPlaceAI::ParseChat(OsdsPlaceGraph::Rules(), Chat, Prior, Error))
				{
					UE_LOG(LogOsdsPlaceFactsCommandlet, Warning, TEXT("Area %d_%d: the local model failed %s"), Area.X, Area.Y, *Error);
					++Failures;
					continue;
				}
				Prior.Sources = Sources;
				UE_LOG(LogOsdsPlaceFactsCommandlet, Display, TEXT("Area %d_%d: %.0f s on the CPU"), Area.X, Area.Y, FPlatformTime::Seconds() - T0);
			}
			FFileHelper::SaveStringToFile(OsdsPlaceAI::ToJson(Area, Prior), *OsdsPlaceAI::BundledPath(Area), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogOsdsPlaceFactsCommandlet, Display, TEXT("Area %d_%d: %s"), Area.X, Area.Y, *OsdsPlaceAI::ToJson(Area, Prior));
		}
	}
	return Failures == 0 ? 0 : 1;
}
