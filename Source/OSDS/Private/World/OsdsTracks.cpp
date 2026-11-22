// Copyright OSDS contributors. All Rights Reserved.

#include "World/OsdsTracks.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/OsdsGeo.h"
#include "World/OsdsTileData.h"

namespace
{
	double Smooth01(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	bool ReadLatLon(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, double& OutLon, double& OutLat)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Field, A) || A->Num() != 2) return false;
		OutLat = (*A)[0]->AsNumber();
		OutLon = (*A)[1]->AsNumber();
		return FMath::IsFinite(OutLat) && FMath::IsFinite(OutLon) && FMath::Abs(OutLat) <= 82.0 && FMath::Abs(OutLon) <= 180.0;
	}

	/** Resampling step of a loop in a tile frame (m). */
	constexpr double LoopStepM = 5.0;
	/** Half the arc over which the loop's curvature is read (m): longer than a dogleg's kink. */
	constexpr double CurveArcM = 40.0;
}

bool OsdsTracks::IsRacewayClass(const FString& Class)
{
	return Class == RacewayClass;
}

bool OsdsTracks::IsPitName(const FString& Name)
{
	return Name.Contains(TEXT("pit "), ESearchCase::IgnoreCase) || Name.EndsWith(TEXT("pit"), ESearchCase::IgnoreCase)
		|| Name.Contains(TEXT("pitlane"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("pit-"), ESearchCase::IgnoreCase);
}

FString OsdsTracks::DataPath()
{
	return FPaths::Combine(OsdsTileData::RepositoryRoot(), TEXT("data"), TEXT("tracks"), TEXT("tracks.json"));
}

TArray<FOsdsTrack> OsdsTracks::Parse(const FString& Json, FString* OutError)
{
	TArray<FOsdsTrack> Out;
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("tracks"), Rows))
	{
		if (OutError) *OutError = TEXT("tracks.json: expected {\"tracks\": [...]}");
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& V : *Rows)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid()) continue;
		FOsdsTrack T;
		if (!O->TryGetStringField(TEXT("id"), T.Id) || !O->TryGetStringField(TEXT("name"), T.Name)) continue;
		if (!ReadLatLon(O, TEXT("start"), T.StartLon, T.StartLat)) continue;
		O->TryGetStringField(TEXT("place"), T.Place);
		O->TryGetStringField(TEXT("note"), T.Note);
		O->TryGetStringField(TEXT("street"), T.Street);
		const FString Kind = O->GetStringField(TEXT("kind"));
		T.Kind = Kind == TEXT("oval") ? EOsdsTrackKind::Oval : Kind == TEXT("street") ? EOsdsTrackKind::Street : EOsdsTrackKind::Road;
		O->TryGetNumberField(TEXT("length_mi"), T.LengthMi);
		O->TryGetNumberField(TEXT("mapped_mi"), T.MappedMi);
		T.bHasBearing = O->TryGetNumberField(TEXT("bearing"), T.BearingDeg) && FMath::IsFinite(T.BearingDeg);
		if (!ReadLatLon(O, TEXT("centre"), T.CentreLon, T.CentreLat)) { T.CentreLon = T.StartLon; T.CentreLat = T.StartLat; }
		O->TryGetNumberField(TEXT("width_m"), T.WidthM);
		T.bConcrete = O->GetStringField(TEXT("surface")) == TEXT("concrete");
		T.WidthM = T.WidthM > 0.0 ? FMath::Clamp(T.WidthM, 6.0, 30.0) : 0.0;
		const TArray<TSharedPtr<FJsonValue>>* Loop = nullptr;
		if (O->TryGetNumberField(TEXT("bank_turn_deg"), T.BankTurnDeg) && O->TryGetArrayField(TEXT("loop"), Loop) && Loop->Num() >= 8)
		{
			O->TryGetNumberField(TEXT("bank_front_deg"), T.BankFrontDeg);
			O->TryGetNumberField(TEXT("bank_back_deg"), T.BankBackDeg);
			for (const TSharedPtr<FJsonValue>& P : *Loop)
			{
				const TArray<TSharedPtr<FJsonValue>>& LL = P->AsArray();
				if (LL.Num() == 2) T.LoopLonLat.Add(FVector2d(LL[1]->AsNumber(), LL[0]->AsNumber()));
			}
			T.bBanked = T.LoopLonLat.Num() >= 8;
		}
		const TArray<TSharedPtr<FJsonValue>>* Sources = nullptr;
		if (O->TryGetArrayField(TEXT("sources"), Sources)) for (const TSharedPtr<FJsonValue>& S : *Sources) T.Sources.Add(S->AsString());
		Out.Add(MoveTemp(T));
	}
	return Out;
}

const TArray<FOsdsTrack>& OsdsTracks::All()
{
	static const TArray<FOsdsTrack> Tracks = []()
	{
		FString Json, Error;
		TArray<FOsdsTrack> Loaded;
		if (FFileHelper::LoadFileToString(Json, *DataPath())) Loaded = Parse(Json, &Error);
		if (Loaded.IsEmpty()) UE_LOG(LogTemp, Warning, TEXT("No race tracks from %s %s"), *DataPath(), *Error);
		return Loaded;
	}();
	return Tracks;
}

const FOsdsTrack* OsdsTracks::Near(double LonDeg, double LatDeg)
{
	const FOsdsTrack* Best = nullptr;
	double BestM = TrackReachM;
	const double Kx = 111320.0 * FMath::Cos(FMath::DegreesToRadians(LatDeg));
	for (const FOsdsTrack& T : All())
	{
		if (!T.IsRaceway()) continue;
		const double D = FMath::Sqrt(FMath::Square((T.CentreLon - LonDeg) * Kx) + FMath::Square((T.CentreLat - LatDeg) * 110540.0));
		if (D < BestM) { BestM = D; Best = &T; }
	}
	return Best;
}

FOsdsPlace OsdsTracks::ToPlace(const FOsdsTrack& Track)
{
	const FString Title = FString::Printf(TEXT("%s · %s"), *Track.Name, *Track.Place);
	return FOsdsPlace::Make(Track.StartLon, Track.StartLat, Title, Track.IsRaceway() ? FString(RacewayStreet) : Track.Street,
		Track.bHasBearing ? Track.BearingDeg : -1.0);
}

bool OsdsTracks::MakeFrame(const FOsdsTrack& Track, const FIntPoint& TileId, double TileSizeM, FOsdsTrackFrame& Out)
{
	Out = FOsdsTrackFrame();
	Out.Track = &Track;
	if (!Track.bBanked) return false;
	const FVector2d Origin(TileId.X, TileId.Y);
	auto Local = [&](double Lon, double Lat) { return (OsdsGeo::LonLatToTile(Lon, Lat) - Origin) * TileSizeM; };
	TArray<FVector2d> Raw;
	for (const FVector2d& LL : Track.LoopLonLat) Raw.Add(Local(LL.X, LL.Y));
	if (FVector2d::Distance(Raw[0], Raw.Last()) > 0.5) { const FVector2d First = Raw[0]; Raw.Add(First); }
	// Resample every LoopStepM along the loop.
	TArray<double> S;
	S.Add(0.0);
	for (int32 i = 1; i < Raw.Num(); ++i) S.Add(S.Last() + FVector2d::Distance(Raw[i - 1], Raw[i]));
	const double Length = S.Last();
	if (Length < 200.0) return false;
	const int32 N = FMath::Max(16, FMath::RoundToInt(Length / LoopStepM));
	int32 Seg = 0;
	for (int32 k = 0; k < N; ++k)
	{
		const double At = Length * k / N;
		while (Seg + 2 < S.Num() && S[Seg + 1] < At) ++Seg;
		const double T = (At - S[Seg]) / FMath::Max(S[Seg + 1] - S[Seg], 1e-6);
		Out.Loop.Add(FMath::Lerp(Raw[Seg], Raw[Seg + 1], FMath::Clamp(T, 0.0, 1.0)));
	}
	// Orientation in the tile frame (+Y is south, so the map's counter-clockwise loop turns the other way here).
	double Area2 = 0.0;
	for (int32 k = 0; k < N; ++k) { const FVector2d& A = Out.Loop[k]; const FVector2d& B = Out.Loop[(k + 1) % N]; Area2 += A.X * B.Y - B.X * A.Y; }
	const double Orient = Area2 >= 0.0 ? 1.0 : -1.0;
	const int32 Arc = FMath::Max(1, FMath::RoundToInt(CurveArcM / (Length / N)));
	TArray<double> K;
	K.SetNum(N);
	Out.Out.SetNum(N);
	for (int32 k = 0; k < N; ++k)
	{
		const FVector2d D = (Out.Loop[(k + 1) % N] - Out.Loop[(k + N - 1) % N]).GetSafeNormal();
		// For a positive (shoelace) loop the outward normal is (D.Y, -D.X).
		Out.Out[k] = FVector2d(D.Y, -D.X) * Orient;
		const FVector2d A = (Out.Loop[k] - Out.Loop[(k + N - Arc) % N]).GetSafeNormal();
		const FVector2d B = (Out.Loop[(k + Arc) % N] - Out.Loop[k]).GetSafeNormal();
		K[k] = FMath::Acos(FMath::Clamp(FVector2d::DotProduct(A, B), -1.0, 1.0)) / CurveArcM;
	}
	double KMax = 0.0;
	for (const double V : K) KMax = FMath::Max(KMax, V);
	Out.TurnW.SetNum(N);
	// Full turn banking from 3/4 of the tightest curvature, none under 3/10: a tri-oval's dogleg
	// keeps nearly its frontstretch figure, the turns their own.
	for (int32 k = 0; k < N; ++k) Out.TurnW[k] = KMax > 1e-6 ? Smooth01((K[k] / KMax - 0.3) / 0.45) : 0.0;
	Out.StartM = Local(Track.StartLon, Track.StartLat);
	Out.CentreM = Local(Track.CentreLon, Track.CentreLat);
	// The front half: within a quarter of the loop's length of the start/finish line, along the loop
	// (the frontstretch and half of each turn; the turns take their own figure anyway).
	int32 StartK = 0;
	for (int32 k = 0; k < N; ++k) if (FVector2d::DistSquared(Out.Loop[k], Out.StartM) < FVector2d::DistSquared(Out.Loop[StartK], Out.StartM)) StartK = k;
	Out.Front.SetNum(N);
	for (int32 k = 0; k < N; ++k) { const int32 D = FMath::Abs(k - StartK); Out.Front[k] = FMath::Min(D, N - D) < N / 4 ? 1 : 0; }
	const FVector2d First = Out.Loop[0];
	Out.Loop.Add(First);
	for (const FVector2d& P : Out.Loop) Out.Bounds += P;
	return true;
}

bool FOsdsTrackFrame::Nearest(const FVector2d& P, double& OutDist, FVector2d& OutOut, double& OutTurnW, bool& bOutFront) const
{
	if (Loop.Num() < 3) return false;
	const int32 N = Loop.Num() - 1;
	double Best = TNumericLimits<double>::Max();
	int32 BestK = 0;
	double BestT = 0.0;
	for (int32 k = 0; k < N; ++k)
	{
		const FVector2d A = Loop[k], B = Loop[k + 1];
		const double L2 = FMath::Max((B - A).SizeSquared(), 1e-9);
		const double T = FMath::Clamp(FVector2d::DotProduct(P - A, B - A) / L2, 0.0, 1.0);
		const double D = FVector2d::DistSquared(P, A + (B - A) * T);
		if (D < Best) { Best = D; BestK = k; BestT = T; }
	}
	const int32 K1 = (BestK + 1) % N;
	OutDist = FMath::Sqrt(Best);
	OutOut = FMath::Lerp(Out[BestK], Out[K1], BestT).GetSafeNormal();
	OutTurnW = FMath::Lerp(TurnW[BestK], TurnW[K1], BestT);
	bOutFront = (BestT < 0.5 ? Front[BestK] : Front[K1]) != 0;
	return true;
}

bool OsdsTracks::BankAt(const FOsdsTrackFrame& Frame, const FVector2d& P, const FVector2d& R, double HalfW, double& OutSlope, double& OutWeight, double& OutOutsideSign)
{
	OutSlope = 0.0; OutWeight = 0.0; OutOutsideSign = 0.0;
	if (!Frame.Track || Frame.Loop.Num() < 3) return false;
	const double Reach = HalfW + BankFadeM + 5.0;
	if (!Frame.Bounds.ExpandBy(Reach).IsInside(P)) return false;
	double Dist = 0.0, TurnW = 0.0;
	FVector2d OutN;
	bool bFront = false;
	if (!Frame.Nearest(P, Dist, OutN, TurnW, bFront)) return false;
	// Full on the loop's carriageway; easing out to none BankFadeM past its edge.
	OutWeight = 1.0 - Smooth01((Dist - 0.5 * HalfW) / (0.5 * HalfW + BankFadeM));
	if (OutWeight <= 0.0) return false;
	const FOsdsTrack& T = *Frame.Track;
	const double Straight = bFront ? T.BankFrontDeg : T.BankBackDeg;
	const double Deg = FMath::Lerp(Straight, T.BankTurnDeg, TurnW);
	OutSlope = FMath::Min(FMath::Tan(FMath::DegreesToRadians(FMath::Max(Deg, 0.0))), MaxBankSlope);
	// The station's right normal against the loop's outward normal; a ribbon across the loop (a crossover) takes none.
	const double Align = FVector2d::DotProduct(R, OutN);
	if (FMath::Abs(Align) < 0.5) { OutWeight = 0.0; return false; }
	OutOutsideSign = Align > 0.0 ? 1.0 : -1.0;
	return true;
}
