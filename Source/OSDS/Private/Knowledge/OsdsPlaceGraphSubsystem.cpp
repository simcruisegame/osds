// Copyright OSDS contributors. All Rights Reserved.

#include "Knowledge/OsdsPlaceGraphSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Async/Async.h"
#include "Misc/ScopeExit.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Knowledge/OsdsPlaceAI.h"
#include "Knowledge/OsdsFurnitureData.h"
#include "Knowledge/OsdsRoadsideData.h"
#include "Knowledge/OsdsPlaceFacts.h"
// The live feeds (Source/OSDS/*/Live) join when that code is in the tree; until then
// other systems push live facts with OsdsPlaceHub::SetLive.
#if __has_include("Live/OsdsLiveWorldSubsystem.h")
#include "Live/OsdsLiveData.h"
#include "Live/OsdsLiveWorldSubsystem.h"
#define OSDS_PLACE_GRAPH_LIVE 1
#else
#define OSDS_PLACE_GRAPH_LIVE 0
#endif
#include "Misc/App.h"
#include "Atlas/OsdsUserAgent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeLock.h"
#include "World/OsdsGeo.h"
#include "World/OsdsTileData.h"
#include "World/OsdsWorldStreamer.h"
#include "World/OsdsWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsPlaceGraph, Log, All);

namespace
{
	FCriticalSection HubLock;
	FOsdsFactSet& LiveStore() { static FOsdsFactSet S; return S; }
	using FPlanRef = TSharedPtr<const FOsdsTilePlan, ESPMode::ThreadSafe>;
	TMap<FIntPoint, FPlanRef>& PlanStore() { static TMap<FIntPoint, FPlanRef> S; return S; }

	/** -OsdsPlaceOffline: every request of the graph goes to a closed local port, to test that a start with no network neither stalls nor fails. */
	FString TestUrl(const FString& Url)
	{
		static const bool bOffline = FParse::Param(FCommandLine::Get(), TEXT("OsdsPlaceOffline"));
		return bOffline ? FString(TEXT("http://127.0.0.1:9/")) : Url;
	}

	const TCHAR* UserAgent = OsdsUserAgent::Get();

	/** The pause after the n-th failure in a row: 30 s doubling to an hour. */
	double FailurePause(int32 Failures) { return FMath::Min(3600.0, 30.0 * FMath::Pow(2.0, FMath::Clamp(Failures - 1, 0, 12))); }

	FAutoConsoleCommandWithWorldAndArgs GExplain(
		TEXT("OSDS.Knowledge.Explain"), TEXT("OSDS.Knowledge.Explain [n]: the n buildings nearest the car, their facts and why each detail"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (const UOsdsPlaceGraphSubsystem* G = UOsdsPlaceGraphSubsystem::Get(World))
			{
				UE_LOG(LogOsdsPlaceGraph, Display, TEXT("%s"), *G->Explain(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 3));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GLive(
		TEXT("OSDS.Knowledge.Live"), TEXT("OSDS.Knowledge.Live: the live facts the graph holds now"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
		{
			UE_LOG(LogOsdsPlaceGraph, Display, TEXT("Live facts:\n%s"), *OsdsPlaceHub::Live().Describe());
		}));

	FAutoConsoleCommandWithWorldAndArgs GReport(
		TEXT("OSDS.Knowledge.Report"), TEXT("OSDS.Knowledge.Report: tiles baked, records, fetches"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (const UOsdsPlaceGraphSubsystem* G = UOsdsPlaceGraphSubsystem::Get(World))
			{
				UE_LOG(LogOsdsPlaceGraph, Display, TEXT("%s"), *G->Report());
			}
		}));
}

// ---------------------------------------------------------------------------------------------
// Hub store

namespace OsdsPlaceHub
{
	void SetLive(FName Fact, float Confidence, const FString& Why)
	{
		FScopeLock Lock(&HubLock);
		if (Confidence <= 0.0f) { LiveStore().Facts.Remove(Fact); return; }
		FOsdsFactSet::FEntry& E = LiveStore().Facts.FindOrAdd(Fact);
		E.Confidence = FMath::Clamp(Confidence, 0.0f, 1.0f);
		E.Why = Why;
	}

	void ClearLive(const TCHAR* Prefix)
	{
		FScopeLock Lock(&HubLock);
		for (auto It = LiveStore().Facts.CreateIterator(); It; ++It)
		{
			if (It.Key().ToString().StartsWith(Prefix)) It.RemoveCurrent();
		}
	}

	FOsdsFactSet Live()
	{
		FScopeLock Lock(&HubLock);
		return LiveStore();
	}

	void StorePlan(const FOsdsTilePlan& Plan)
	{
		FScopeLock Lock(&HubLock);
		PlanStore().Add(Plan.Tile, MakeShared<const FOsdsTilePlan, ESPMode::ThreadSafe>(Plan));
		// Keep the plans of the tiles around the car only (a tile holds a few hundred buildings).
		if (PlanStore().Num() > 64)
		{
			TArray<FIntPoint> Keys;
			PlanStore().GetKeys(Keys);
			Keys.Sort([&](const FIntPoint& A, const FIntPoint& B) { return (A - Plan.Tile).SizeSquared() > (B - Plan.Tile).SizeSquared(); });
			for (int32 i = 0; i < Keys.Num() - 48; ++i) PlanStore().Remove(Keys[i]);
		}
	}

	TSharedPtr<const FOsdsTilePlan, ESPMode::ThreadSafe> FindPlan(const FIntPoint& Tile)
	{
		FScopeLock Lock(&HubLock);
		const FPlanRef* P = PlanStore().Find(Tile);
		return P ? *P : FPlanRef();
	}

	void ForgetPlan(const FIntPoint& Tile)
	{
		FScopeLock Lock(&HubLock);
		PlanStore().Remove(Tile);
	}

	void BakeForBuild(const FOsdsTileData& Data, double LatDeg, double LonDeg, double SideM, FOsdsTilePlan& Out)
	{
		TArray<FOsdsPlaceRecord> Records;
		OsdsPlaceFacts::ForTile(Data.Id, Records);
		FOsdsAreaPrior Prior;
		FOsdsFactSet AreaFacts;
		if (OsdsPlaceAI::ForArea(OsdsPlaceAI::AreaOf(Data.Id), Prior)) OsdsPlaceAI::ToFacts(Prior, AreaFacts);
		OsdsPlaceGraph::BakeTile(OsdsPlaceGraph::Rules(), Data, LatDeg, LonDeg, SideM, Records, Out, &AreaFacts);
		StorePlan(Out);
	}
}

// ---------------------------------------------------------------------------------------------
// Subsystem

bool UOsdsPlaceGraphSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UOsdsPlaceGraphSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	const bool bForce = FParse::Param(Cmd, TEXT("OsdsLive"));
	bAI = !FParse::Param(Cmd, TEXT("OsdsNoPlaceAI"));
	// Open data at run time (Overpass, Wikidata, Wikipedia) only when asked for with -OsdsLive:
	// the public Overpass and Wikidata servers are community services that cannot carry a game's
	// players (detail-staging/server-load.md). The bundled start tiles keep their facts; elsewhere
	// the graph bakes from its region and climate rules. The facts for every place come later from
	// a data pack built once per release.
	bNetwork = bForce && !FParse::Param(Cmd, TEXT("OsdsNoLive")) && !FParse::Param(Cmd, TEXT("OsdsNoPlaceFetch"));
	const FOsdsPlaceRules& R = OsdsPlaceGraph::Rules();
	UE_LOG(LogOsdsPlaceGraph, Display, TEXT("Place graph: %d rules, %d live rules, %d regions, %d palettes; open-data fetch %s."),
		R.Rules.Num(), R.LiveRules.Num(), R.Regions.Num(), R.Palettes.Num(), bNetwork ? TEXT("on") : TEXT("off"));
}

void UOsdsPlaceGraphSubsystem::Deinitialize()
{
	// The run's summary: what was baked and fetched, and what the graph cost the game thread.
	if (GetWorld() && GetWorld()->IsGameWorld() && (Fetched > 0 || AIAreas > 0 || SlowestTickMs > 0.0)) UE_LOG(LogOsdsPlaceGraph, Display, TEXT("%s"), *Report());
	Super::Deinitialize();
}

TStatId UOsdsPlaceGraphSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOsdsPlaceGraphSubsystem, STATGROUP_Tickables);
}

UOsdsPlaceGraphSubsystem* UOsdsPlaceGraphSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UOsdsPlaceGraphSubsystem>() : nullptr;
}

bool UOsdsPlaceGraphSubsystem::ObserverLonLat(double& OutLat, double& OutLon) const
{
	const UWorld* World = GetWorld();
	const UOsdsWorldSubsystem* WS = World ? World->GetSubsystem<UOsdsWorldSubsystem>() : nullptr;
	const AOsdsWorldStreamer* Streamer = WS ? WS->GetStreamer() : nullptr;
	if (!Streamer || !Streamer->GetFrame().IsValid()) return false;
	FVector At = FVector::ZeroVector;
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0)) At = Pawn->GetActorLocation();
	else if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(World, 0)) At = Cam->GetCameraLocation();
	Streamer->GetFrame().WorldCmToLonLat(At, OutLon, OutLat);
	return true;
}

void UOsdsPlaceGraphSubsystem::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	ON_SCOPE_EXIT { SlowestTickMs = FMath::Max(SlowestTickMs, (FPlatformTime::Seconds() - Now) * 1000.0); };
	if (Now >= NextLive)
	{
		NextLive = Now + 5.0;
		UpdateLiveFacts();
	}
	if (bCardShotPending && Now - CardStart > 1.2)
	{
		bCardShotPending = false;
		const FString Shot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Knowledge/cards"), FString::Printf(TEXT("card-%d.png"), CardsShown.Num()));
		FScreenshotRequest::RequestScreenshot(Shot, true, false);
		UE_LOG(LogOsdsPlaceGraph, Display, TEXT("%s"), *Explain(1));
	}
	if (Now >= NextCardCheck)
	{
		NextCardCheck = Now + 0.5;
		const double C0 = FPlatformTime::Seconds();
		UpdateCard(Now);
		SlowestCardMs = FMath::Max(SlowestCardMs, (FPlatformTime::Seconds() - C0) * 1000.0);
	}
	if (bNetwork && !bFetching && Now >= NextFetch)
	{
		NextFetch = Now + 2.0;
		FetchAhead();
	}
	if (bNetwork && bAI && AIStep == EAIStep::Idle && Now >= NextAI)
	{
		NextAI = Now + 5.0;
		StartAI();
	}
}

void UOsdsPlaceGraphSubsystem::UpdateLiveFacts()
{
#if OSDS_PLACE_GRAPH_LIVE
	using namespace OsdsPlaceHub;
	const UOsdsLiveWorldSubsystem* Live = UOsdsLiveWorldSubsystem::Get(GetWorld());
	if (!Live) return;
	auto Set = [](const TCHAR* F, bool bOn, float C, const FString& Why) { SetLive(FName(F), bOn ? C : 0.0f, Why); };

	const FOsdsLiveWeather& W = Live->GetWeather();
	if (W.bValid)
	{
		const FString When = W.TimeUtc.ToIso8601();
		const FString Src = FString::Printf(TEXT("Open-Meteo %s"), *When);
		Set(TEXT("live:day"), W.bIsDay, 1.0f, Src);
		Set(TEXT("live:night"), !W.bIsDay, 1.0f, Src);
		const bool bRain = FOsdsLiveData::IsRainCode(W.WmoCode) || W.PrecipitationMm > 0.1f;
		Set(TEXT("live:rain"), bRain, 1.0f, FString::Printf(TEXT("%s: %s, %.1f mm"), *Src, *FOsdsLiveData::DescribeWmo(W.WmoCode), W.PrecipitationMm));
		Set(TEXT("live:fog"), FOsdsLiveData::IsFogCode(W.WmoCode) || (W.VisibilityM > 0.0f && W.VisibilityM < 1000.0f), 1.0f,
			FString::Printf(TEXT("%s: visibility %.0f m"), *Src, W.VisibilityM));
		Set(TEXT("live:sunny"), W.bIsDay && W.CloudCover < 0.4f, 1.0f - W.CloudCover, FString::Printf(TEXT("%s: cloud %.0f%%"), *Src, W.CloudCover * 100.0f));
		Set(TEXT("live:overcast"), W.CloudCover > 0.8f, W.CloudCover, FString::Printf(TEXT("%s: cloud %.0f%%"), *Src, W.CloudCover * 100.0f));
		Set(TEXT("live:hot"), W.TemperatureC >= 27.0f, FMath::Clamp((W.TemperatureC - 24.0f) / 6.0f, 0.0f, 1.0f), FString::Printf(TEXT("%s: %.1f C"), *Src, W.TemperatureC));
		Set(TEXT("live:cold"), W.TemperatureC <= 5.0f, 1.0f, FString::Printf(TEXT("%s: %.1f C"), *Src, W.TemperatureC));
		const FString Wind = FString::Printf(TEXT("%s: wind %.1f m/s, gusts %.1f"), *Src, W.WindSpeedMs, W.WindGustMs);
		Set(TEXT("live:wind_strong"), W.WindSpeedMs >= 10.0f || W.WindGustMs >= 15.0f, 1.0f, Wind);
		Set(TEXT("live:wind_breezy"), W.WindSpeedMs >= 4.5f && W.WindSpeedMs < 10.0f, 1.0f, Wind);
	}
	const FOsdsLiveAir& A = Live->GetAir();
	if (A.bValid)
	{
		Set(TEXT("live:hazy"), A.AerosolOpticalDepth > 0.3f || A.UsAqi > 100.0f, FMath::Clamp(FMath::Max(A.AerosolOpticalDepth / 0.6f, A.UsAqi / 150.0f), 0.0f, 1.0f),
			FString::Printf(TEXT("CAMS: AOD %.2f, AQI %.0f"), A.AerosolOpticalDepth, A.UsAqi));
	}
	const FOsdsLiveSea& S = Live->GetSea();
	if (S.bValid)
	{
		Set(TEXT("live:sea_rough"), S.WaveHeightM >= 1.5f, FMath::Clamp(S.WaveHeightM / 2.5f, 0.0f, 1.0f), FString::Printf(TEXT("Open-Meteo marine: waves %.1f m"), S.WaveHeightM));
	}

	// Traffic on the road under the car (FHWA counts, scaled to the hour).
	const UWorld* World = GetWorld();
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		float PerKmLane = 0.0f, PerHour = 0.0f;
		FString Road;
		if (Live->TrafficAt(Pawn->GetActorLocation(), PerKmLane, PerHour, &Road))
		{
			const FString Why = FString::Printf(TEXT("FHWA count on %s: %.0f veh/h, %.1f veh/km/lane"), *Road, PerHour, PerKmLane);
			Set(TEXT("live:traffic_heavy"), PerKmLane >= 20.0f, FMath::Clamp(PerKmLane / 35.0f, 0.0f, 1.0f), Why);
			Set(TEXT("live:traffic_light"), PerKmLane < 6.0f, 1.0f, Why);
		}
	}
	const int32 Photos = Live->GetPhotos().Num();
	Set(TEXT("live:photos"), Photos > 0, FMath::Clamp(Photos / 20.0f, 0.1f, 1.0f), FString::Printf(TEXT("Wikimedia Commons: %d photographs nearby"), Photos));
#endif
}

void UOsdsPlaceGraphSubsystem::FetchAhead()
{
	double Lat, Lon;
	if (!ObserverLonLat(Lat, Lon)) return;
	const FVector2d T = OsdsGeo::LonLatToTile(Lon, Lat);
	const FIntPoint Here(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y));
	// Nearest first: the tile under the car, then its ring (not a 5 x 5 ring: one player's share of
	// a public server stays small).
	for (int32 R = 0; R <= 1; ++R)
	{
		for (int32 dy = -R; dy <= R; ++dy)
		{
			for (int32 dx = -R; dx <= R; ++dx)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R) continue;
				const FIntPoint Tile = Here + FIntPoint(dx, dy);
				// Place facts and the roadside come in one answer; a tile whose place facts are
				// bundled or cached still asks once for its roadside (and keeps its place facts).
				const bool bPlaces = OsdsPlaceFacts::IsKnown(Tile);
				if (Failed.Contains(Tile) || (bPlaces && OsdsRoadsideData::IsKnown(Tile))) continue;
				bRoadsideOnly = bPlaces;
				StartFetch(Tile);
				return;
			}
		}
	}
}

void UOsdsPlaceGraphSubsystem::StartFetch(const FIntPoint& Tile)
{
	bFetching = true;
	FetchTile = Tile;
	PendingOsmBody.Reset();
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(TestUrl(OsdsPlaceFacts::OverpassUrl(OverpassAttempt)));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("User-Agent"), UserAgent);
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));
	Req->SetContentAsString(TEXT("data=") + FGenericPlatformHttp::UrlEncode(OsdsPlaceFacts::OverpassQuery(Tile)));
	Req->SetTimeout(40.0f);
	TWeakObjectPtr<UOsdsPlaceGraphSubsystem> Weak(this);
	Req->OnProcessRequestComplete().BindLambda([Weak, Tile](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		if (!Weak.IsValid()) return;
		const bool bOk = bConnected && Response.IsValid() && Response->GetResponseCode() == 200;
		Weak->OnOverpass(Tile, bOk ? Response->GetContentAsString() : FString(), bOk);
	});
	Req->ProcessRequest();
}

void UOsdsPlaceGraphSubsystem::OnOverpass(const FIntPoint& Tile, const FString& Body, bool bOk)
{
	// Keep the answer as text: Overpass answers run to megabytes, so they are parsed on a
	// worker thread with the Wikidata answer (OnWikidata), never on the game thread.
	if (bRoadsideOnly)
	{
		// Only the roadside is new for this tile: parse it on a worker, no Wikidata.
		if (!bOk)
		{
			LastError = FString::Printf(TEXT("Overpass %s (%s): no answer"), *OsdsTileName(Tile), *OsdsPlaceFacts::OverpassUrl(OverpassAttempt));
			bFetching = false;
			Failed.Add(Tile);
			NextFetch = FPlatformTime::Seconds() + FailurePause(++FetchFailures);
			return;
		}
		TWeakObjectPtr<UOsdsPlaceGraphSubsystem> Weak(this);
		Async(EAsyncExecution::ThreadPool, [Weak, Tile, Body]()
		{
			FOsdsRoadside Roadside;
			FString Error;
			const bool bParsed = OsdsRoadsideData::ParseOverpass(Body, Roadside, Error);
			if (bParsed) OsdsRoadsideData::Store(Tile, Roadside, true);
			AsyncTask(ENamedThreads::GameThread, [Weak, Tile, bParsed]()
			{
				if (!Weak.IsValid()) return;
				UOsdsPlaceGraphSubsystem& S = *Weak;
				S.bFetching = false;
				++S.Fetched;
				if (!bParsed) S.Failed.Add(Tile);
				// The Overpass fair-use rule: one request at a time, a pause between them.
				S.FetchFailures = bParsed ? 0 : S.FetchFailures;
				S.NextFetch = FPlatformTime::Seconds() + 30.0;
			});
		});
		return;
	}
	PendingOsmBody = bOk ? Body : FString();
	if (bOk) FetchFailures = 0;
	if (!bOk)
	{
		// Overpass is busy at times (429/504): keep going with Wikidata alone, and wait longer
		// before the next request (one instance only: rotating spreads the load to every mirror).
		LastError = FString::Printf(TEXT("Overpass %s (%s): no answer"), *OsdsTileName(Tile), *OsdsPlaceFacts::OverpassUrl(OverpassAttempt));
		++FetchFailures;
		UE_LOG(LogOsdsPlaceGraph, Display, TEXT("%s"), *LastError);
	}
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(TestUrl(OsdsPlaceFacts::WikidataUrl(OsdsPlaceFacts::WikidataQuery(Tile))));
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("User-Agent"), UserAgent);
	Req->SetHeader(TEXT("Accept"), TEXT("application/sparql-results+json"));
	Req->SetTimeout(40.0f);
	const bool bOsmOk = bOk;
	TWeakObjectPtr<UOsdsPlaceGraphSubsystem> Weak(this);
	Req->OnProcessRequestComplete().BindLambda([Weak, Tile, bOsmOk](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		if (!Weak.IsValid()) return;
		const bool bWdOk = bConnected && Response.IsValid() && Response->GetResponseCode() == 200;
		if (!bOsmOk && !bWdOk) { Weak->OnWikidata(Tile, FString(), false); return; }
		Weak->OnWikidata(Tile, bWdOk ? Response->GetContentAsString() : FString(), true);
	});
	Req->ProcessRequest();
}

void UOsdsPlaceGraphSubsystem::OnWikidata(const FIntPoint& Tile, const FString& Body, bool bOk)
{
	if (!bOk)
	{
		bFetching = false;
		PendingOsmBody.Reset();
		Failed.Add(Tile);
		NextFetch = FPlatformTime::Seconds() + FailurePause(++FetchFailures);
		return;
	}
	// Parse, merge and save on a worker thread; only the result comes back.
	FString OsmBody = MoveTemp(PendingOsmBody);
	PendingOsmBody.Reset();
	TWeakObjectPtr<UOsdsPlaceGraphSubsystem> Weak(this);
	Async(EAsyncExecution::ThreadPool, [Weak, Tile, OsmBody = MoveTemp(OsmBody), WdBody = Body]()
	{
		TArray<FOsdsPlaceRecord> Osm, Wd, All, Useful;
		FString Error, OsmError, WdError;
		if (!OsmBody.IsEmpty() && !OsdsPlaceFacts::ParseOverpass(OsmBody, Osm, OsmError)) Osm.Reset();
		if (!WdBody.IsEmpty() && !OsdsPlaceFacts::ParseWikidata(WdBody, Wd, WdError)) Wd.Reset();
		OsdsPlaceFacts::Merge(Osm, Wd, All);
		for (FOsdsPlaceRecord& R : All) if (OsdsPlaceFacts::IsUseful(R)) Useful.Add(MoveTemp(R));
		// Both sources answered: saved for good. One missing: kept for this run only, asked again next time.
		const bool bComplete = !OsmBody.IsEmpty() && OsmError.IsEmpty() && !WdBody.IsEmpty() && WdError.IsEmpty();
		OsdsPlaceFacts::Store(Tile, Useful, FString::Printf(TEXT("overpass:%d wikidata:%d"), Osm.Num(), Wd.Num()), bComplete);
		// The street furniture comes in the same Overpass answer.
		if (!OsmBody.IsEmpty() && OsmError.IsEmpty())
		{
			TArray<FOsdsFurniture> Furniture;
			FString FError;
			if (OsdsFurniture::ParseOverpass(OsmBody, Furniture, FError)) OsdsFurniture::Store(Tile, Furniture, true);
			// ... and the roadside: road-edge tags, barriers, poles, lamps, signals, named shops.
			FOsdsRoadside Roadside;
			if (OsdsRoadsideData::ParseOverpass(OsmBody, Roadside, FError)) OsdsRoadsideData::Store(Tile, Roadside, true);
		}
		const FString Line = FString::Printf(TEXT("Place facts %s: %d useful records (%d OSM, %d Wikidata)%s%s."), *OsdsTileName(Tile), Useful.Num(), Osm.Num(), Wd.Num(),
			OsmError.IsEmpty() ? TEXT("") : *(TEXT("; ") + OsmError), WdError.IsEmpty() ? TEXT("") : *(TEXT("; ") + WdError));
		AsyncTask(ENamedThreads::GameThread, [Weak, Line]()
		{
			if (!Weak.IsValid()) return;
			UOsdsPlaceGraphSubsystem& S = *Weak;
			S.bFetching = false;
			++S.Fetched;
			UE_LOG(LogOsdsPlaceGraph, Display, TEXT("%s"), *Line);
			// The Overpass fair-use rule: one request at a time, a pause between them.
			S.NextFetch = FPlatformTime::Seconds() + (S.FetchFailures > 0 ? FailurePause(S.FetchFailures) : 30.0);
		});
	});
}

FString UOsdsPlaceGraphSubsystem::Report() const
{
	FScopeLock Lock(&HubLock);
	int32 Buildings = 0, Matched = 0, Records = 0;
	double Ms = 0.0;
	for (const auto& KV : PlanStore())
	{
		Buildings += KV.Value->Buildings.Num();
		Matched += KV.Value->Matched;
		Records += KV.Value->Records;
		Ms = FMath::Max(Ms, KV.Value->BakeMs);
	}
	return FString::Printf(TEXT("Place graph: %d tiles baked, %d buildings, %d with their own open data (%d records), slowest bake %.2f ms (worker); %d tiles fetched, %d areas read by the AI%s%s; %d live facts; game thread: slowest tick %.2f ms, slowest card check %.2f ms."),
		PlanStore().Num(), Buildings, Matched, Records, Ms, Fetched, AIAreas, LastError.IsEmpty() ? TEXT("") : TEXT(", last error: "), *LastError, LiveStore().Facts.Num(),
		SlowestTickMs, SlowestCardMs);
}

FString UOsdsPlaceGraphSubsystem::Explain(int32 Count) const
{
	double Lat, Lon;
	if (!ObserverLonLat(Lat, Lon)) return TEXT("Place graph: no world yet.");
	const FVector2d T = OsdsGeo::LonLatToTile(Lon, Lat);
	const FIntPoint Tile(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y));
	const TSharedPtr<const FOsdsTilePlan, ESPMode::ThreadSafe> PlanRef = OsdsPlaceHub::FindPlan(Tile);
	if (!PlanRef.IsValid()) return FString::Printf(TEXT("Place graph: tile %s has no plan yet."), *OsdsTileName(Tile));
	const FOsdsTilePlan& Plan = *PlanRef;
	const double Side = OsdsGeo::TileSizeMetres(Lat);
	const FVector2d Here = (T - FVector2d(Tile.X, Tile.Y)) * Side;
	TArray<const FOsdsBuildingPlan*> Near;
	for (const FOsdsBuildingPlan& B : Plan.Buildings) Near.Add(&B);
	Near.Sort([&](const FOsdsBuildingPlan& A, const FOsdsBuildingPlan& B) { return FVector2d::DistSquared(A.CentreM, Here) < FVector2d::DistSquared(B.CentreM, Here); });
	const FOsdsFactSet Live = OsdsPlaceHub::Live();
	const FOsdsPlaceRules& Rules = OsdsPlaceGraph::Rules();
#if OSDS_PLACE_GRAPH_LIVE
	const UOsdsLiveWorldSubsystem* LiveWorld = UOsdsLiveWorldSubsystem::Get(GetWorld());
#endif
	FString Out = FString::Printf(TEXT("Place graph at %.5f, %.5f (tile %s, %d buildings, %d with open data, bake %.2f ms)\nTile facts:\n%s"),
		Lat, Lon, *OsdsTileName(Tile), Plan.Buildings.Num(), Plan.Matched, Plan.BakeMs, *Plan.TileFacts.Describe());
	for (int32 i = 0; i < FMath::Min(Count, Near.Num()); ++i)
	{
		const FOsdsBuildingPlan& B = *Near[i];
		Out += FString::Printf(TEXT("\n--- %.0f m away, %.0f m tall ---\n"), FVector2d::Distance(B.CentreM, Here), B.HeightM);
		Out += B.Explain();
		FOsdsFactSet View;
		OsdsPlaceGraph::LiveView(Rules, B, Live, View);
		FString LiveLines;
		for (const auto& KV : View.Facts)
		{
			const FString K = KV.Key.ToString();
			if (K.StartsWith(TEXT("light:")) || K.StartsWith(TEXT("street:")) || K.StartsWith(TEXT("surface:")) || K.StartsWith(TEXT("air:"))
				|| K.StartsWith(TEXT("sea:")) || K.StartsWith(TEXT("vegetation:")) || K.StartsWith(TEXT("sky:")))
			{
				LiveLines += FString::Printf(TEXT("  %-28s %.2f  %s\n"), *K, KV.Value.Confidence, *KV.Value.Why);
			}
		}
		if (!LiveLines.IsEmpty()) Out += TEXT(" Live view now:\n") + LiveLines;
#if OSDS_PLACE_GRAPH_LIVE
		// Photographs of this place on Wikimedia Commons (for the builders and the AI).
		if (LiveWorld && !B.Name.IsEmpty())
		{
			// The name, or its longest word ("The Carlyle" -> "Carlyle"), as photo titles write it.
			TArray<FString> Words;
			B.Name.ParseIntoArray(Words, TEXT(" "));
			FString First;
			for (const FString& Wd : Words) if (Wd.Len() > First.Len() && Wd != TEXT("Hotel") && Wd != TEXT("South") && Wd != TEXT("Beach")) First = Wd;
			const FString Key = B.Name;
			for (const FOsdsPlacePhoto& P : LiveWorld->GetPhotos())
			{
				if (P.Title.Contains(Key, ESearchCase::IgnoreCase) || (First.Len() > 4 && P.Title.Contains(First, ESearchCase::IgnoreCase)))
				{
					Out += FString::Printf(TEXT(" Photo: %s (%s, %s)\n"), *P.Title, *P.License, *P.PageUrl);
				}
			}
		}
#endif
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------
// The co-driver's place card

namespace
{
	constexpr double CardSeconds = 7.0;
	constexpr double CardFadeSeconds = 0.8;
	constexpr double CardRangeM = 45.0;
}

FString UOsdsPlaceGraphSubsystem::StyleLabel(FName Style)
{
	FString S = Style.ToString();
	S.RemoveFromStart(TEXT("style:"));
	static const TMap<FString, FString> Special = {
		{ TEXT("mimo"), TEXT("MiMo (Miami Modern)") }, { TEXT("art_deco"), TEXT("Art Deco") }, { TEXT("beaux_arts"), TEXT("Beaux-Arts") },
		{ TEXT("mid_century_modern"), TEXT("Mid-century Modern") }, { TEXT("queen_anne"), TEXT("Queen Anne") } };
	if (const FString* L = Special.Find(S)) return *L;
	TArray<FString> Words;
	S.ParseIntoArray(Words, TEXT("_"));
	for (FString& W : Words) if (W.Len() > 0) W[0] = FChar::ToUpper(W[0]);
	return FString::Join(Words, TEXT(" "));
}

bool UOsdsPlaceGraphSubsystem::MakeCard(const FOsdsBuildingPlan& Plan, FOsdsPlaceCard& Out)
{
	if (Plan.Name.IsEmpty()) return false;
	TArray<FString> Parts;
	// Only a style the place's own data gives (not one spread from the neighbours).
	if (!Plan.Style.IsNone() && Plan.StyleConfidence >= 0.95f) Parts.Add(StyleLabel(Plan.Style));
	if (Plan.Year > 0) Parts.Add(FString::FromInt(Plan.Year));
	if (!Plan.Architect.IsEmpty()) Parts.Add(Plan.Architect);
	if (Parts.Num() == 0) return false;
	Out.Title = Plan.Name.ToUpper();
	Out.Line = FString::Join(Parts, TEXT("  ·  "));
	Out.Note = Plan.Facts.Has(TEXT("heritage:nrhp")) ? TEXT("National Register of Historic Places")
		: Plan.Facts.Has(TEXT("heritage:listed")) ? TEXT("Listed building") : TEXT("Wikidata, OpenStreetMap");
	return true;
}

void UOsdsPlaceGraphSubsystem::UpdateCard(double Now)
{
	if (Now - CardStart < CardSeconds + 1.5) return; // one card at a time, with a pause
	double Lat, Lon;
	if (!ObserverLonLat(Lat, Lon)) return;
	const FVector2d T = OsdsGeo::LonLatToTile(Lon, Lat);
	const FIntPoint Tile(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y));
	const TSharedPtr<const FOsdsTilePlan, ESPMode::ThreadSafe> PlanRef = OsdsPlaceHub::FindPlan(Tile);
	if (!PlanRef.IsValid()) return;
	const FOsdsTilePlan& Plan = *PlanRef;
	const FVector2d Here = (T - FVector2d(Tile.X, Tile.Y)) * OsdsGeo::TileSizeMetres(Lat);
	const FOsdsBuildingPlan* Best = nullptr;
	double BestD = CardRangeM;
	FOsdsPlaceCard BestCard;
	for (const FOsdsBuildingPlan& B : Plan.Buildings)
	{
		const double D = FVector2d::Distance(B.CentreM, Here);
		if (D >= BestD || CardsShown.Contains(B.Name)) continue;
		FOsdsPlaceCard C;
		if (!MakeCard(B, C)) continue;
		Best = &B;
		BestD = D;
		BestCard = C;
	}
	if (!Best) return;
	CardsShown.Add(Best->Name);
	Card = BestCard;
	CardStart = Now;
	if (FParse::Param(FCommandLine::Get(), TEXT("OsdsCardShots")))
	{
		bCardShotPending = true; // taken once the card has faded in
	}
	UE_LOG(LogOsdsPlaceGraph, Display, TEXT("Place card: %s | %s | %s (%.0f m)"), *Card.Title, *Card.Line, *Card.Note, BestD);
}

bool UOsdsPlaceGraphSubsystem::GetPlaceCard(FOsdsPlaceCard& Out) const
{
	const double Age = FPlatformTime::Seconds() - CardStart;
	if (Age < 0.0 || Age > CardSeconds || Card.Title.IsEmpty()) return false;
	Out = Card;
	Out.Alpha = static_cast<float>(FMath::Clamp(FMath::Min(Age, CardSeconds - Age) / CardFadeSeconds, 0.0, 1.0));
	return true;
}

// ---------------------------------------------------------------------------------------------
// The local AI reads the area (OsdsPlaceAI)

namespace
{
	using FAIThen = TFunction<void(UOsdsPlaceGraphSubsystem&, bool, const FString&)>;

	void AIRequest(TWeakObjectPtr<UOsdsPlaceGraphSubsystem> Weak, const FString& Url, const FString& Verb, const FString& Body, float Timeout, FAIThen Then)
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(TestUrl(Url));
		Req->SetVerb(Verb);
		Req->SetHeader(TEXT("User-Agent"), UserAgent);
		if (!Body.IsEmpty())
		{
			Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			Req->SetContentAsString(Body);
		}
		Req->SetTimeout(Timeout);
		Req->SetActivityTimeout(Timeout); // a local model answers only when done
		Req->OnProcessRequestComplete().BindLambda([Weak, Then](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!Weak.IsValid()) return;
			const bool bOk = bConnected && Response.IsValid() && Response->GetResponseCode() == 200;
			Then(*Weak, bOk, bOk ? Response->GetContentAsString() : FString());
		});
		Req->ProcessRequest();
	}
}

void UOsdsPlaceGraphSubsystem::AIFail(const FString& Why)
{
	AIStep = EAIStep::Idle;
	AIFailed.Add(AIArea);
	LastError = Why;
	UE_LOG(LogOsdsPlaceGraph, Display, TEXT("Place AI %d_%d: %s"), AIArea.X, AIArea.Y, *Why);
}

void UOsdsPlaceGraphSubsystem::StartAI()
{
	double Lat, Lon;
	if (!ObserverLonLat(Lat, Lon)) return;
	const FVector2d T = OsdsGeo::LonLatToTile(Lon, Lat);
	const FIntPoint Area = OsdsPlaceAI::AreaOf(FIntPoint(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y)));
	if (OsdsPlaceAI::IsKnown(Area) || AIFailed.Contains(Area)) return;
	AIArea = Area;
	AIPrior = FOsdsAreaPrior();
	AILines.Reset();
	AIStep = EAIStep::Busy;
	const TWeakObjectPtr<UOsdsPlaceGraphSubsystem> Weak(this);

	// 1. Is the local model there? (Ollama /api/tags: the smallest chat model that fits beside the game.)
	AIRequest(Weak, OsdsPlaceAI::OllamaUrl() + TEXT("/api/tags"), TEXT("GET"), FString(), 5.0f, [Weak](UOsdsPlaceGraphSubsystem& S, bool bOk, const FString& Tags)
	{
		S.AIModel = bOk ? OsdsPlaceAI::PickModel(Tags) : FString();
		if (S.AIModel.IsEmpty())
		{
			S.bAI = false; // no local model: stop asking
			S.AIStep = EAIStep::Idle;
			UE_LOG(LogOsdsPlaceGraph, Display, TEXT("Place AI off: no local model at %s (install Ollama and a 2-6 GB model)."), *OsdsPlaceAI::OllamaUrl());
			return;
		}
		double CLat, CLon;
		OsdsPlaceAI::AreaCentre(S.AIArea, CLat, CLon);
		// 2. The Wikipedia articles about places near the area's centre.
		AIRequest(Weak, OsdsPlaceAI::GeosearchUrl(CLat, CLon), TEXT("GET"), FString(), 30.0f, [Weak, CLat, CLon](UOsdsPlaceGraphSubsystem& S, bool bOk, const FString& Geo)
		{
			TArray<int64> Ids;
			FString Error;
			if (!bOk || !OsdsPlaceAI::ParseGeosearch(Geo, Ids, Error)) { S.AIFail(TEXT("Wikipedia geosearch failed")); return; }
			if (Ids.Num() == 0)
			{
				// Nothing written about this area: an empty answer, so it is not asked again.
				S.AIPrior.Model = S.AIModel;
				OsdsPlaceAI::Store(S.AIArea, S.AIPrior);
				S.AIStep = EAIStep::Idle;
				return;
			}
			// 3. Their lead extracts.
			AIRequest(Weak, OsdsPlaceAI::ExtractsUrl(Ids), TEXT("GET"), FString(), 30.0f, [Weak, Ids, CLat, CLon](UOsdsPlaceGraphSubsystem& S, bool bOk, const FString& Ex)
			{
				FString Error;
				if (!bOk || !OsdsPlaceAI::ParseExtracts(Ex, Ids, S.AILines, S.AIPrior.Sources, Error)) { S.AIFail(TEXT("Wikipedia extracts failed")); return; }
				const UWorld* World = S.GetWorld();
				const UOsdsWorldSubsystem* WS = World ? World->GetSubsystem<UOsdsWorldSubsystem>() : nullptr;
				const AOsdsWorldStreamer* Streamer = WS ? WS->GetStreamer() : nullptr;
				const FString Label = Streamer ? Streamer->GetTitle() : FString();
				const double T0 = FPlatformTime::Seconds();
				// 4. The local model reads them (CPU only) and answers in the graph's words.
				AIRequest(Weak, OsdsPlaceAI::OllamaUrl() + TEXT("/api/chat"), TEXT("POST"),
					OsdsPlaceAI::ChatRequest(OsdsPlaceGraph::Rules(), S.AIModel, Label, CLat, CLon, S.AILines), 600.0f,
					[T0](UOsdsPlaceGraphSubsystem& S, bool bOk, const FString& Chat)
				{
					FString Error;
					const TArray<FString> Sources = S.AIPrior.Sources;
					if (!bOk || !OsdsPlaceAI::ParseChat(OsdsPlaceGraph::Rules(), Chat, S.AIPrior, Error)) { S.AIFail(TEXT("local model failed: ") + Error); return; }
					S.AIPrior.Sources = Sources;
					OsdsPlaceAI::Store(S.AIArea, S.AIPrior);
					S.AIStep = EAIStep::Idle;
					++S.AIAreas;
					FOsdsFactSet F;
					OsdsPlaceAI::ToFacts(S.AIPrior, F);
					UE_LOG(LogOsdsPlaceGraph, Display, TEXT("Place AI %d_%d (%s, %.0f s on the CPU, %d articles):\n%s"), S.AIArea.X, S.AIArea.Y, *S.AIPrior.Model,
						FPlatformTime::Seconds() - T0, Sources.Num(), *F.Describe());
				});
			});
		});
	});
}
