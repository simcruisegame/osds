// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsRoute.h"

#include "Algo/BinarySearch.h"
#include "Gameplay/OsdsSpeedUnits.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace OsdsRouteText
{
	EOsdsTurn ParseTurn(const FString& Kind)
	{
		if (Kind == TEXT("depart")) return EOsdsTurn::Depart;
		if (Kind == TEXT("arrive")) return EOsdsTurn::Arrive;
		if (Kind == TEXT("slight right")) return EOsdsTurn::SlightRight;
		if (Kind == TEXT("right")) return EOsdsTurn::Right;
		if (Kind == TEXT("sharp right")) return EOsdsTurn::SharpRight;
		if (Kind == TEXT("slight left")) return EOsdsTurn::SlightLeft;
		if (Kind == TEXT("left")) return EOsdsTurn::Left;
		if (Kind == TEXT("sharp left")) return EOsdsTurn::SharpLeft;
		if (Kind == TEXT("u-turn")) return EOsdsTurn::UTurn;
		return EOsdsTurn::Continue;
	}

	FString TurnVerb(EOsdsTurn Turn)
	{
		switch (Turn)
		{
		case EOsdsTurn::SlightRight: return TEXT("Slight right");
		case EOsdsTurn::Right: return TEXT("Right");
		case EOsdsTurn::SharpRight: return TEXT("Sharp right");
		case EOsdsTurn::SlightLeft: return TEXT("Slight left");
		case EOsdsTurn::Left: return TEXT("Left");
		case EOsdsTurn::SharpLeft: return TEXT("Sharp left");
		case EOsdsTurn::UTurn: return TEXT("Make a U-turn");
		case EOsdsTurn::Arrive: return TEXT("Arrive");
		case EOsdsTurn::Depart: return TEXT("Start");
		default: return TEXT("Continue");
		}
	}

	FString Instruction(EOsdsTurn Turn, const FString& Street)
	{
		if (Turn == EOsdsTurn::Arrive) return Street.IsEmpty() ? FString(TEXT("Arrive at the stop")) : FString::Printf(TEXT("Arrive at %s"), *Street);
		if (Turn == EOsdsTurn::Continue || Turn == EOsdsTurn::Depart)
		{
			return Street.IsEmpty() ? FString(TEXT("Continue")) : FString::Printf(TEXT("Continue on %s"), *Street);
		}
		if (Turn == EOsdsTurn::UTurn) return Street.IsEmpty() ? FString(TEXT("Make a U-turn")) : FString::Printf(TEXT("Make a U-turn onto %s"), *Street);
		if (Street.IsEmpty()) return FString::Printf(TEXT("Turn %s"), *TurnVerb(Turn).ToLower());
		return FString::Printf(TEXT("%s onto %s"), *TurnVerb(Turn), *Street);
	}

	void DistanceParts(double Metres, FString& OutNumber, FString& OutUnit)
	{
		DistancePartsIn(Metres, OsdsSpeedUnits::Get(), OutNumber, OutUnit);
	}

	void DistancePartsIn(double Metres, EOsdsSpeedUnits Units, FString& OutNumber, FString& OutUnit)
	{
		if (Units == EOsdsSpeedUnits::Kmh)
		{
			// Metric: metres below 1 km (to 10 m, to 50 m from 500 m), then km.
			const double M = FMath::Max(Metres, 0.0);
			if (M < 950.0)
			{
				const int32 Step = M >= 500.0 ? 50 : 10;
				OutNumber = FString::FromInt(FMath::Max(Step, FMath::RoundToInt(M / Step) * Step));
				OutUnit = TEXT("m");
				return;
			}
			const double Km = M / 1000.0;
			OutNumber = Km < 10.0 ? FString::Printf(TEXT("%.1f"), Km) : FString::FromInt(FMath::RoundToInt(Km));
			OutUnit = TEXT("km");
			return;
		}
		const double Miles = FMath::Max(Metres, 0.0) / 1609.344;
		if (Miles < 0.1)
		{
			const double Feet = Metres * 3.28084;
			const int32 Step = Feet >= 500.0 ? 50 : 10;
			OutNumber = FString::FromInt(FMath::Max(Step, FMath::RoundToInt(Feet / Step) * Step));
			OutUnit = TEXT("ft");
			return;
		}
		OutNumber = Miles < 10.0 ? FString::Printf(TEXT("%.1f"), Miles) : FString::FromInt(FMath::RoundToInt(Miles));
		OutUnit = TEXT("mi");
	}

	FString Distance(double Metres)
	{
		FString Number, Unit;
		DistanceParts(Metres, Number, Unit);
		return Number + TEXT(" ") + Unit;
	}

	FString Duration(double Seconds)
	{
		const int32 Minutes = FMath::Max(1, FMath::RoundToInt(Seconds / 60.0));
		if (Minutes < 60) return FString::Printf(TEXT("%d min"), Minutes);
		return FString::Printf(TEXT("%d h %d min"), Minutes / 60, Minutes % 60);
	}

	FString Clock(const FDateTime& Local)
	{
		const int32 Hour = Local.GetHour();
		const int32 H12 = Hour % 12 == 0 ? 12 : Hour % 12;
		return FString::Printf(TEXT("%d:%02d %s"), H12, Local.GetMinute(), Hour < 12 ? TEXT("AM") : TEXT("PM"));
	}
}

bool FOsdsRoute::ParseHelperString(const FString& Raw, const TArray<FOsdsRouteStop>& InStops, FString* OutError)
{
	TSharedPtr<FJsonValue> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		if (OutError) *OutError = TEXT("The route answer is not JSON");
		return false;
	}
	return ParseHelperJson(Json, InStops, OutError);
}

bool FOsdsRoute::ParseHelperJson(const TSharedPtr<FJsonValue>& Json, const TArray<FOsdsRouteStop>& InStops, FString* OutError)
{
	auto Fail = [OutError](const FString& Why) { if (OutError) *OutError = Why; return false; };
	const TSharedPtr<FJsonObject>* Doc = nullptr;
	if (!Json.IsValid() || !Json->TryGetObject(Doc) || !Doc->IsValid()) return Fail(TEXT("The route answer is not an object"));
	FString Status;
	(*Doc)->TryGetStringField(TEXT("status"), Status);
	if (Status != TEXT("ok"))
	{
		FString Reason;
		(*Doc)->TryGetStringField(TEXT("reason"), Reason);
		return Fail(Reason.IsEmpty() ? FString(TEXT("No route was found")) : FString::Printf(TEXT("No route: %s"), *Reason));
	}
	const TArray<TSharedPtr<FJsonValue>>* LegValues = nullptr;
	if (!(*Doc)->TryGetArrayField(TEXT("legs"), LegValues) || LegValues->Num() == 0) return Fail(TEXT("The route has no legs"));
	if (InStops.Num() != LegValues->Num() + 1) return Fail(TEXT("The route and its stops do not agree"));

	TArray<FOsdsRouteLeg> NewLegs;
	for (const TSharedPtr<FJsonValue>& LegValue : *LegValues)
	{
		const TSharedPtr<FJsonObject>* LegObj = nullptr;
		if (!LegValue.IsValid() || !LegValue->TryGetObject(LegObj)) return Fail(TEXT("A leg is not an object"));
		FOsdsRouteLeg Leg;
		(*LegObj)->TryGetNumberField(TEXT("length_m"), Leg.LengthM);
		(*LegObj)->TryGetNumberField(TEXT("duration_s"), Leg.DurationS);
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (!(*LegObj)->TryGetArrayField(TEXT("points"), Points)) return Fail(TEXT("A leg has no points"));
		for (const TSharedPtr<FJsonValue>& P : *Points)
		{
			const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
			if (P.IsValid() && P->TryGetArray(XY) && XY->Num() >= 2) Leg.LonLat.Add(FVector2D((*XY)[0]->AsNumber(), (*XY)[1]->AsNumber()));
		}
		if (Leg.LonLat.Num() < 1) return Fail(TEXT("A leg has no points"));
		// Street names: run-length [[first segment, name], ...].
		Leg.Streets.Init(FString(), FMath::Max(Leg.LonLat.Num() - 1, 0));
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if ((*LegObj)->TryGetArrayField(TEXT("names"), Names))
		{
			for (int32 i = 0; i < Names->Num(); ++i)
			{
				const TArray<TSharedPtr<FJsonValue>>* Run = nullptr;
				if (!(*Names)[i]->TryGetArray(Run) || Run->Num() < 2) continue;
				const int32 First = static_cast<int32>((*Run)[0]->AsNumber());
				int32 End = Leg.Streets.Num();
				const TArray<TSharedPtr<FJsonValue>>* NextRun = nullptr;
				if (Names->IsValidIndex(i + 1) && (*Names)[i + 1]->TryGetArray(NextRun) && NextRun->Num() >= 1) End = static_cast<int32>((*NextRun)[0]->AsNumber());
				for (int32 k = FMath::Max(First, 0); k < FMath::Min(End, Leg.Streets.Num()); ++k) Leg.Streets[k] = (*Run)[1]->AsString();
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
		if ((*LegObj)->TryGetArrayField(TEXT("steps"), StepValues))
		{
			for (const TSharedPtr<FJsonValue>& StepValue : *StepValues)
			{
				const TSharedPtr<FJsonObject>* StepObj = nullptr;
				if (!StepValue.IsValid() || !StepValue->TryGetObject(StepObj)) continue;
				FOsdsRouteStep Step;
				FString Kind;
				(*StepObj)->TryGetStringField(TEXT("kind"), Kind);
				Step.Turn = OsdsRouteText::ParseTurn(Kind);
				(*StepObj)->TryGetNumberField(TEXT("angle"), Step.AngleDeg);
				(*StepObj)->TryGetStringField(TEXT("name"), Step.Street);
				(*StepObj)->TryGetNumberField(TEXT("at_m"), Step.AtM);
				(*StepObj)->TryGetNumberField(TEXT("index"), Step.Index);
				Leg.Steps.Add(MoveTemp(Step));
			}
		}
		NewLegs.Add(MoveTemp(Leg));
	}
	Clear();
	Legs = MoveTemp(NewLegs);
	Stops = InStops;
	// The arrival steps name the stop they reach.
	for (int32 i = 0; i < Legs.Num(); ++i)
	{
		for (FOsdsRouteStep& Step : Legs[i].Steps)
		{
			if (Step.Turn == EOsdsTurn::Arrive && Step.Street.IsEmpty()) Step.Street = Stops[i + 1].Title;
		}
	}
	return true;
}

void FOsdsRoute::Clear()
{
	Stops.Reset();
	Legs.Reset();
	ActiveLeg = 0;
	LastSegment = 0;
	OffTimer = 0.0;
	bFinished = false;
	Progress = FOsdsRouteProgress();
}

void FOsdsRoute::Project(const FOsdsLonLatToM& ToMetres)
{
	for (FOsdsRouteLeg& Leg : Legs)
	{
		Leg.M.Reset(Leg.LonLat.Num());
		Leg.CumM.Reset(Leg.LonLat.Num());
		for (const FVector2D& P : Leg.LonLat)
		{
			const FVector2D M = ToMetres.IsBound() ? ToMetres.Execute(P.X, P.Y) : P;
			Leg.CumM.Add(Leg.M.Num() ? Leg.CumM.Last() + FVector2D::Distance(Leg.M.Last(), M) : 0.0);
			Leg.M.Add(M);
		}
		// Steps are placed by point index, so they agree with these metres exactly.
		for (FOsdsRouteStep& Step : Leg.Steps)
		{
			if (Leg.CumM.IsValidIndex(Step.Index)) Step.AtM = Leg.CumM[Step.Index];
		}
		if (Leg.CumM.Num()) Leg.LengthM = Leg.CumM.Last();
	}
}

void FOsdsRoute::Locate(const FOsdsRouteLeg& Leg, const FVector2D& CarM, double& OutAlong, double& OutLateral, int32& OutSegment) const
{
	auto Search = [&](int32 From, int32 To, double& Best)
	{
		for (int32 i = FMath::Max(From, 0); i < FMath::Min(To, Leg.M.Num() - 1); ++i)
		{
			const FVector2D A = Leg.M[i], AB = Leg.M[i + 1] - A;
			const double L2 = AB.SizeSquared();
			const double T = L2 > 1e-9 ? FMath::Clamp(FVector2D::DotProduct(CarM - A, AB) / L2, 0.0, 1.0) : 0.0;
			const FVector2D Foot = A + AB * T;
			const double D = FVector2D::Distance(CarM, Foot);
			if (D < Best)
			{
				Best = D;
				OutSegment = i;
				OutAlong = Leg.CumM[i] + FMath::Sqrt(L2) * T;
				// The right of the travel direction (x east, y south: right = (-dir.y, dir.x)).
				const FVector2D Dir = L2 > 1e-9 ? AB / FMath::Sqrt(L2) : FVector2D(1.0, 0.0);
				const FVector2D Right(-Dir.Y, Dir.X);
				OutLateral = FVector2D::DotProduct(CarM - Foot, Right);
			}
		}
	};
	if (Leg.M.Num() < 2)
	{
		OutAlong = 0.0;
		OutLateral = 0.0;
		OutSegment = 0;
		if (Leg.M.Num() == 1) OutLateral = FVector2D::Distance(CarM, Leg.M[0]);
		return;
	}
	double Best = TNumericLimits<double>::Max();
	// Near the last place first (a road that passes itself, a loop, must not jump ahead).
	Search(LastSegment - 4, LastSegment + 80, Best);
	if (Best > OffRouteM)
	{
		Search(0, Leg.M.Num(), Best);
	}
}

const FOsdsRouteProgress& FOsdsRoute::Update(const FVector2D& CarM, double DeltaS)
{
	ArrivalTime = FMath::Max(0.0, ArrivalTime - DeltaS);
	if (!IsActive())
	{
		Progress = FOsdsRouteProgress();
		return Progress;
	}
	const FOsdsRouteLeg& Leg = Legs[ActiveLeg];
	double Along = 0.0, Lateral = 0.0;
	int32 Segment = 0;
	Locate(Leg, CarM, Along, Lateral, Segment);
	LastSegment = Segment;

	// At the stop: the end of the leg is near, or the car has passed it on the line.
	const double ToEnd = Leg.M.Num() ? FVector2D::Distance(CarM, Leg.M.Last()) : 0.0;
	if (ToEnd < ArriveM || (Leg.LengthM - Along < 5.0 && FMath::Abs(Lateral) < OffRouteM))
	{
		ArrivalText = FString::Printf(TEXT("You are at %s"), Stops.IsValidIndex(ActiveLeg + 1) ? *Stops[ActiveLeg + 1].Title : TEXT("the stop"));
		ArrivalTime = ArrivalMessageSeconds;
		++ActiveLeg;
		LastSegment = 0;
		OffTimer = 0.0;
		if (!IsActive())
		{
			bFinished = true;
			Progress = FOsdsRouteProgress();
			return Progress;
		}
		return Update(CarM, 0.0);
	}

	Progress.AlongM = Along;
	Progress.LateralM = Lateral;
	Progress.LegRemainingM = FMath::Max(Leg.LengthM - Along, 0.0);
	Progress.LegRemainingS = Leg.LengthM > 1.0 ? Leg.DurationS * Progress.LegRemainingM / Leg.LengthM : 0.0;
	Progress.NextStep = -1;
	for (int32 i = 0; i < Leg.Steps.Num(); ++i)
	{
		const FOsdsRouteStep& Step = Leg.Steps[i];
		if (Step.Turn == EOsdsTurn::Depart) continue;
		// A turn stays "next" until the car is a few metres past it.
		if (Step.AtM > Along - 3.0)
		{
			Progress.NextStep = i;
			Progress.ToNextStepM = FMath::Max(Step.AtM - Along, 0.0);
			break;
		}
	}
	OffTimer = FMath::Abs(Lateral) > OffRouteM ? OffTimer + DeltaS : 0.0;
	Progress.bOffRoute = OffTimer > OffRouteSeconds;
	return Progress;
}

const FOsdsRouteStep* FOsdsRoute::GetStep(int32 Index) const
{
	const FOsdsRouteLeg* Leg = GetLeg();
	return Leg && Leg->Steps.IsValidIndex(Index) ? &Leg->Steps[Index] : nullptr;
}

const FOsdsRouteStep* FOsdsRoute::GetThenStep(double WithinM) const
{
	const FOsdsRouteStep* Next = GetStep(Progress.NextStep);
	const FOsdsRouteStep* Then = GetStep(Progress.NextStep + 1);
	if (!Next || !Then || Next->Turn == EOsdsTurn::Arrive) return nullptr;
	return Then->AtM - Next->AtM <= WithinM ? Then : nullptr;
}

bool FOsdsRoute::PointAt(double AlongM, FVector2D& OutM, FVector2D& OutDir) const
{
	const FOsdsRouteLeg* Leg = GetLeg();
	if (!Leg || Leg->M.Num() < 2) return false;
	AlongM = FMath::Clamp(AlongM, 0.0, Leg->CumM.Last());
	int32 i = FMath::Max(0, Algo::UpperBound(Leg->CumM, AlongM) - 1);
	i = FMath::Min(i, Leg->M.Num() - 2);
	// Skip zero-length segments.
	while (i < Leg->M.Num() - 2 && Leg->CumM[i + 1] - Leg->CumM[i] < 1e-6) ++i;
	const double Len = FMath::Max(Leg->CumM[i + 1] - Leg->CumM[i], 1e-6);
	const double T = FMath::Clamp((AlongM - Leg->CumM[i]) / Len, 0.0, 1.0);
	OutM = FMath::Lerp(Leg->M[i], Leg->M[i + 1], T);
	OutDir = (Leg->M[i + 1] - Leg->M[i]).GetSafeNormal();
	return true;
}

TArray<FOsdsRouteStop> FOsdsRoute::RemainingStops() const
{
	TArray<FOsdsRouteStop> Out;
	for (int32 i = ActiveLeg + 1; i < Stops.Num(); ++i) Out.Add(Stops[i]);
	return Out;
}

double FOsdsRoute::TotalLengthM() const
{
	double Sum = 0.0;
	for (const FOsdsRouteLeg& Leg : Legs) Sum += Leg.LengthM;
	return Sum;
}

double FOsdsRoute::TotalDurationS() const
{
	double Sum = 0.0;
	for (const FOsdsRouteLeg& Leg : Legs) Sum += Leg.DurationS;
	return Sum;
}
