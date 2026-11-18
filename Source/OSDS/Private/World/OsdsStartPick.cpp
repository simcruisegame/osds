// Copyright OSDS contributors. All Rights Reserved.

#include "World/OsdsStartPick.h"

#include "World/OsdsRoadCheck.h"
#include "World/OsdsRoadNetwork.h"
#include "World/OsdsTileBuilder.h"
#include "World/OsdsTraffic.h"
#include "Misc/ScopeExit.h"

namespace
{
	bool IsPublic(const FOsdsRibbon& R) { return !R.bService && R.Class != TEXT("service") && R.St.Num() >= 2; }

	/** The road-layer faces of each build, binned once per Candidates call (else per check). */
	thread_local TMap<const FOsdsTileBuild*, TSharedPtr<OsdsRoadCheck::FTopSurface>>* GTops = nullptr;
	const OsdsRoadCheck::FTopSurface& TopsFor(const FOsdsTileBuild& B, TSharedPtr<OsdsRoadCheck::FTopSurface>& Local)
	{
		// The build's own (made on the worker): an installed tile's road layer is empty.
		if (B.RoadTops.IsValid()) return *B.RoadTops;
		if (GTops)
		{
			TSharedPtr<OsdsRoadCheck::FTopSurface>& Entry = GTops->FindOrAdd(&B);
			if (!Entry.IsValid()) Entry = MakeShared<OsdsRoadCheck::FTopSurface>(B);
			return *Entry;
		}
		if (!Local.IsValid()) Local = MakeShared<OsdsRoadCheck::FTopSurface>(B);
		return *Local;
	}

	FVector2d World(const FOsdsStartTile& T, const FVector2d& LocalM) { return FVector2d(T.OriginCm.X, T.OriginCm.Y) / 100.0 + LocalM; }

}

TArray<uint8> OsdsStartPick::JunctionCuts(const FOsdsRibbon& R)
{
	TArray<uint8> Out;
	Out.Init(0, R.St.Num());
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const auto& Cut = R.Sides[Side].Cut;
		for (int32 i = 0; i < R.St.Num(); )
		{
			if (!(Cut.IsValidIndex(i) && Cut[i])) { ++i; continue; }
			int32 j = i;
			while (j + 1 < R.St.Num() && Cut.IsValidIndex(j + 1) && Cut[j + 1]) ++j;
			if (R.St[j].S - R.St[i].S <= ParallelCutM)
			{
				for (int32 k = i; k <= j; ++k) Out[k] = 1;
			}
			i = j + 1;
		}
	}
	return Out;
}

namespace
{

	/** The station index whose S is nearest S. */
	int32 StationAt(const FOsdsRibbon& R, double S)
	{
		int32 Best = 0;
		for (int32 i = 1; i < R.St.Num(); ++i) if (FMath::Abs(R.St[i].S - S) < FMath::Abs(R.St[Best].S - S)) Best = i;
		return Best;
	}

	/** The point of a ribbon's centreline nearest P (tile metres), within stations [Lo, Hi]: segment index and t. */
	bool Nearest(const FOsdsRibbon& R, const FVector2d& P, int32 Lo, int32 Hi, int32& OutSeg, double& OutT)
	{
		double Best = TNumericLimits<double>::Max();
		Lo = FMath::Clamp(Lo, 0, R.St.Num() - 2);
		Hi = FMath::Clamp(Hi, 0, R.St.Num() - 2);
		for (int32 i = Lo; i <= Hi; ++i)
		{
			const FVector2d A = R.St[i].C, B = R.St[i + 1].C;
			const double L2 = FMath::Max((B - A).SizeSquared(), 1e-9);
			const double T = FMath::Clamp(FVector2d::DotProduct(P - A, B - A) / L2, 0.0, 1.0);
			const double D = FVector2d::DistSquared(P, A + (B - A) * T);
			if (D < Best) { Best = D; OutSeg = i; OutT = T; }
		}
		return Best < TNumericLimits<double>::Max();
	}
}

double OsdsStartPick::AheadM(const TArray<FOsdsStartTile>& Tiles, int32 Tile, int32 Ribbon, double S, int32 Dir, double CapM)
{
	if (!Tiles.IsValidIndex(Tile) || !Tiles[Tile].Build || !Tiles[Tile].Build->Roads.Ribbons.IsValidIndex(Ribbon)) return 0.0;
	double Total = 0.0;
	TSet<uint64> Seen;
	for (int32 Hop = 0; Hop < 256; ++Hop)
	{
		const FOsdsRibbon& R = Tiles[Tile].Build->Roads.Ribbons[Ribbon];
		Seen.Add((static_cast<uint64>(Tile) << 40) | (static_cast<uint64>(Ribbon) << 2) | (Dir > 0 ? 1 : 0));
		const double EndS = Dir > 0 ? R.St.Last().S : R.St[0].S;
		Total += FMath::Abs(EndS - S);
		if (Total >= CapM) return CapM;
		// The ribbon's end: on along the ribbon that starts or ends within 3 m of it, straightest
		// first, as the drive robot (AOsdsPreflight::Walk) and a driver going on do.
		const FVector2d End = World(Tiles[Tile], Dir > 0 ? R.St.Last().C : R.St[0].C);
		const FVector2d Heading = (Dir > 0 ? R.St.Last().T : -R.St[0].T).GetSafeNormal();
		int32 NextTile = INDEX_NONE, NextRibbon = INDEX_NONE, NextDir = 0;
		double NextS = 0.0, BestDot = -0.2;
		for (int32 t = 0; t < Tiles.Num(); ++t)
		{
			if (!Tiles[t].Build) continue;
			const TArray<FOsdsRibbon>& Ribbons = Tiles[t].Build->Roads.Ribbons;
			for (int32 r = 0; r < Ribbons.Num(); ++r)
			{
				if (t == Tile && r == Ribbon) continue;
				const FOsdsRibbon& O = Ribbons[r];
				if (!IsPublic(O)) continue;
				if (FVector2d::Distance(World(Tiles[t], O.St[0].C), End) < 3.0)
				{
					const double Dot = FVector2d::DotProduct((O.St[1].C - O.St[0].C).GetSafeNormal(), Heading);
					if (Dot > BestDot) { BestDot = Dot; NextTile = t; NextRibbon = r; NextDir = 1; NextS = O.St[0].S; }
				}
				if (FVector2d::Distance(World(Tiles[t], O.St.Last().C), End) < 3.0)
				{
					const int32 N = O.St.Num();
					const double Dot = FVector2d::DotProduct((O.St[N - 2].C - O.St[N - 1].C).GetSafeNormal(), Heading);
					if (Dot > BestDot) { BestDot = Dot; NextTile = t; NextRibbon = r; NextDir = -1; NextS = O.St.Last().S; }
				}
			}
		}
		if (NextRibbon == INDEX_NONE) return Total; // a dead end
		if (Seen.Contains((static_cast<uint64>(NextTile) << 40) | (static_cast<uint64>(NextRibbon) << 2) | (NextDir > 0 ? 1 : 0))) return CapM; // a loop goes on
		Tile = NextTile; Ribbon = NextRibbon; Dir = NextDir; S = NextS;
	}
	return Total;
}

double OsdsStartPick::JudgePastEdgeM(const TArray<FOsdsStartTile>& Tiles, const FVector& CarCm, double YawDeg, int32* OutTile, int32* OutRibbon)
{
	const FVector2d Car(CarCm.X, CarCm.Y);
	const FVector2d Forward(FMath::Cos(FMath::DegreesToRadians(YawDeg)), FMath::Sin(FMath::DegreesToRadians(YawDeg)));
	double BestScore = 3000.0, BestDist = 0.0, BestHalfW = 0.0;
	int32 BestTile = INDEX_NONE, BestRibbon = INDEX_NONE;
	bool bFound = false;
	for (int32 TileIndex = 0; TileIndex < Tiles.Num(); ++TileIndex)
	{
		const FOsdsStartTile& T = Tiles[TileIndex];
		if (!T.Build) continue;
		const FVector2d Origin(T.OriginCm.X, T.OriginCm.Y);
		for (int32 RibbonIndex = 0; RibbonIndex < T.Build->Roads.Ribbons.Num(); ++RibbonIndex)
		{
			const FOsdsRibbon& R = T.Build->Roads.Ribbons[RibbonIndex];
			if (R.St.Num() < 2 || R.bService) continue;
			for (int32 i = 0; i + 1 < R.St.Num(); ++i)
			{
				const FVector2d A = Origin + R.St[i].C * 100.0, B = Origin + R.St[i + 1].C * 100.0;
				const FVector2d AB = B - A;
				const double Len2 = AB.SquaredLength();
				if (Len2 < 1.0) continue;
				const double Tt = FMath::Clamp(FVector2d::DotProduct(Car - A, AB) / Len2, 0.0, 1.0);
				const double Dist = FVector2d::Distance(Car, A + AB * Tt);
				const double Align = FVector2d::DotProduct(AB / FMath::Sqrt(Len2), Forward);
				if (FMath::Abs(Align) < 0.5) continue;
				const double Score = Dist + (1.0 - FMath::Abs(Align)) * 800.0;
				if (Score < BestScore) { BestScore = Score; BestDist = Dist; BestHalfW = R.St[i].HalfW; BestTile = TileIndex; BestRibbon = RibbonIndex; bFound = true; }
			}
		}
	}
	if (OutTile) *OutTile = BestTile;
	if (OutRibbon) *OutRibbon = BestRibbon;
	return bFound ? BestDist / 100.0 - BestHalfW : 30.0;
}

TArray<OsdsStartPick::FRouteSample> OsdsStartPick::RouteSamples(const TArray<FOsdsStartTile>& Tiles, const FOsdsStartPose& Start, double LengthM, double StepM)
{
	TArray<FRouteSample> Out;
	if (!Tiles.IsValidIndex(Start.Tile) || !Tiles[Start.Tile].Build || !Tiles[Start.Tile].Build->Roads.Ribbons.IsValidIndex(Start.Ribbon)) return Out;
	int32 Tile = Start.Tile, Ribbon = Start.Ribbon, Dir = Start.Dir;
	double S = Tiles[Tile].Build->Roads.Ribbons[Ribbon].St[Start.Station].S;
	double Walked = 0.0;
	TSet<uint64> Seen;
	for (int32 Hop = 0; Hop < 256 && Walked < LengthM; ++Hop)
	{
		const FOsdsRibbon& R = Tiles[Tile].Build->Roads.Ribbons[Ribbon];
		Seen.Add((static_cast<uint64>(Tile) << 40) | (static_cast<uint64>(Ribbon) << 2) | (Dir > 0 ? 1 : 0));
		const double EndS = Dir > 0 ? R.St.Last().S : R.St[0].S;
		// Samples along this ribbon in its lane, every StepM.
		while (Walked < LengthM && (Dir > 0 ? S <= EndS : S >= EndS))
		{
			int32 I = 0;
			while (I + 2 < R.St.Num() && R.St[I + 1].S < S) ++I;
			const FOsdsRibbonStation& A = R.St[I];
			const FOsdsRibbonStation& B = R.St[I + 1];
			const double Tt = FMath::Clamp((S - A.S) / FMath::Max(B.S - A.S, 1e-9), 0.0, 1.0);
			const double L = OsdsTrafficModel::LaneOffsetM(R, Tt < 0.5 ? I : I + 1, Dir);
			const FVector2d C = FMath::Lerp(A.C, B.C, Tt) + FMath::Lerp(A.R, B.R, Tt).GetSafeNormal() * L;
			const double H = FMath::Lerp(R.CarriagewayHeight(I, L), R.CarriagewayHeight(I + 1, L), Tt);
			Out.Add({ Tiles[Tile].OriginCm + FVector(C.X * 100.0, C.Y * 100.0, H * 100.0), Tile });
			S += Dir * StepM;
			Walked += StepM;
		}
		if (Walked >= LengthM) break;
		// On along the straightest ribbon that starts or ends within 3 m of this end (the robot's rule).
		const FVector2d End = World(Tiles[Tile], Dir > 0 ? R.St.Last().C : R.St[0].C);
		const FVector2d Heading = (Dir > 0 ? R.St.Last().T : -R.St[0].T).GetSafeNormal();
		int32 NextTile = INDEX_NONE, NextRibbon = INDEX_NONE, NextDir = 0;
		double NextS = 0.0, BestDot = -0.2;
		for (int32 t = 0; t < Tiles.Num(); ++t)
		{
			if (!Tiles[t].Build) continue;
			const TArray<FOsdsRibbon>& Ribbons = Tiles[t].Build->Roads.Ribbons;
			for (int32 r = 0; r < Ribbons.Num(); ++r)
			{
				if (t == Tile && r == Ribbon) continue;
				const FOsdsRibbon& O = Ribbons[r];
				if (!IsPublic(O)) continue;
				if (FVector2d::Distance(World(Tiles[t], O.St[0].C), End) < 3.0)
				{
					const double Dot = FVector2d::DotProduct((O.St[1].C - O.St[0].C).GetSafeNormal(), Heading);
					if (Dot > BestDot) { BestDot = Dot; NextTile = t; NextRibbon = r; NextDir = 1; NextS = O.St[0].S; }
				}
				if (FVector2d::Distance(World(Tiles[t], O.St.Last().C), End) < 3.0)
				{
					const int32 N = O.St.Num();
					const double Dot = FVector2d::DotProduct((O.St[N - 2].C - O.St[N - 1].C).GetSafeNormal(), Heading);
					if (Dot > BestDot) { BestDot = Dot; NextTile = t; NextRibbon = r; NextDir = -1; NextS = O.St.Last().S; }
				}
			}
		}
		if (NextRibbon == INDEX_NONE) break;
		if (Seen.Contains((static_cast<uint64>(NextTile) << 40) | (static_cast<uint64>(NextRibbon) << 2) | (NextDir > 0 ? 1 : 0))) break;
		Tile = NextTile; Ribbon = NextRibbon; Dir = NextDir; S = NextS;
	}
	return Out;
}

bool OsdsStartPick::OnCarriageway(const TArray<FOsdsStartTile>& Tiles, const FOsdsStartPose& Pose, FString* Why, TArray<FVector>* OutCheckCm)
{
	if (OutCheckCm) OutCheckCm->Reset();
	auto Fail = [Why](const FString& Text) { if (Why) *Why = Text; return false; };
	if (!Tiles.IsValidIndex(Pose.Tile) || !Tiles[Pose.Tile].Build) return Fail(TEXT("no tile"));
	const FOsdsTileBuild& B = *Tiles[Pose.Tile].Build;
	if (!B.Roads.Ribbons.IsValidIndex(Pose.Ribbon)) return Fail(TEXT("no ribbon"));
	const FOsdsRibbon& R = B.Roads.Ribbons[Pose.Ribbon];
	const FVector2d Centre = FVector2d(Pose.LocationCm.X - Tiles[Pose.Tile].OriginCm.X, Pose.LocationCm.Y - Tiles[Pose.Tile].OriginCm.Y) / 100.0;
	const double Yaw = FMath::DegreesToRadians(Pose.Rotation.Yaw);
	const FVector2d Fwd(FMath::Cos(Yaw), FMath::Sin(Yaw)), Right(-Fwd.Y, Fwd.X);
	// A car: 4.6 m long, 2 m wide; its corners and its middle.
	const FVector2d Points[5] = { Centre, Centre + Fwd * 2.3 + Right * 1.0, Centre + Fwd * 2.3 - Right * 1.0, Centre - Fwd * 2.3 + Right * 1.0, Centre - Fwd * 2.3 - Right * 1.0 };
	TSharedPtr<OsdsRoadCheck::FTopSurface> LocalTops;
	const OsdsRoadCheck::FTopSurface& Tops = TopsFor(B, LocalTops);
	// The drawn road ahead of the nose: a road-layer face 10 and 20 m out along the heading (a
	// start on a curve points the nose off the road: Welch, WV, at a brick building's bank).
	for (const double Out : { 10.0, 20.0 })
	{
		const FVector World2 = Pose.LocationCm + FVector(Fwd.X, Fwd.Y, 0.0) * (Out * 100.0);
		bool bFace = false;
		for (const FOsdsStartTile& T : Tiles)
		{
			if (!T.Build) continue;
			const FVector2d L((World2.X - T.OriginCm.X) / 100.0, (World2.Y - T.OriginCm.Y) / 100.0);
			if (L.X < 0.0 || L.Y < 0.0 || L.X > T.Build->TileSizeM || L.Y > T.Build->TileSizeM) continue;
			TSharedPtr<OsdsRoadCheck::FTopSurface> LocalAhead;
			double H = 0.0;
			if (TopsFor(*T.Build, LocalAhead).Top(L, H) && FMath::Abs(H - Pose.LocationCm.Z / 100.0) < 0.15 * Out + 0.5) { bFace = true; break; }
		}
		if (!bFace) return Fail(FString::Printf(TEXT("no road under the heading %.0f m ahead"), Out));
	}
	for (const FVector2d& P : Points)
	{
		int32 Seg = 0;
		double T = 0.0;
		if (!Nearest(R, P, Pose.Station - 8, Pose.Station + 8, Seg, T)) return Fail(TEXT("off its ribbon"));
		const FOsdsRibbonStation& A = R.St[Seg];
		const FOsdsRibbonStation& C = R.St[Seg + 1];
		const FVector2d On = FMath::Lerp(A.C, C.C, T);
		const FVector2d Rn = FMath::Lerp(A.R, C.R, T).GetSafeNormal();
		const double Lateral = FVector2d::DotProduct(P - On, Rn);
		const double HalfW = FMath::Lerp(A.HalfW, C.HalfW, T);
		if (FMath::Abs(Lateral) > HalfW - EdgeClearM || FVector2d::Distance(P, On + Rn * Lateral) > 0.5)
		{
			return Fail(FString::Printf(TEXT("a corner %.1f m from the centre line, the road %.1f m wide (%.1f m kept from its edge)"), Lateral, 2.0 * HalfW, EdgeClearM));
		}
		// No ground above the road under the car (a bank the road conditioning missed would hide
		// the road and stop the car).
		const double Road = FMath::Lerp(R.CarriagewayHeight(Seg, Lateral), R.CarriagewayHeight(Seg + 1, Lateral), T);
		if (OutCheckCm) OutCheckCm->Add(Tiles[Pose.Tile].OriginCm + FVector(P.X * 100.0, P.Y * 100.0, Road * 100.0));
		// A road-layer face of the drawn mesh under it (what the judge's left-the-road trace needs),
		// and no other road surface over it (a junction's flare, an overlapping ribbon).
		double Top = 0.0;
		if (!Tops.Top(P, Top)) return Fail(TEXT("no road face of the drawn mesh under a corner"));
		if (Top > Road + 0.03 || Top < Road - 0.05)
		{
			return Fail(FString::Printf(TEXT("the drawn road %+.0f cm off the road's height"), (Top - Road) * 100.0));
		}
		if (B.TerrainN > 0 && P.X >= 0.0 && P.Y >= 0.0 && P.X <= B.TileSizeM && P.Y <= B.TileSizeM && !A.bBridge && !C.bBridge)
		{
			const double Ground = B.TerrainHeightAt(P);
			if (Ground > Road + 0.1) return Fail(FString::Printf(TEXT("ground %.2f m above the road"), Ground - Road));
		}
	}
	return true;
}

TArray<uint8> OsdsStartPick::JunctionCuts(const FOsdsRoadNetwork& Roads, int32 RibbonIndex)
{
	const FOsdsRibbon& R = Roads.Ribbons[RibbonIndex];
	TArray<uint8> Out = JunctionCuts(R);
	for (int32 i = 0; i < R.St.Num(); ++i)
	{
		if (!Out[i]) continue;
		bool bParallel = true;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (!(R.Sides[Side].Cut.IsValidIndex(i) && R.Sides[Side].Cut[i])) continue;
			const FOsdsRibbonStation& St = R.St[i];
			const FVector2d Probe = St.C + St.R * (Side == 0 ? -1.0 : 1.0) * (St.HalfW + 1.0);
			FOsdsRoadHit Hit;
			const bool bAlongside = Roads.FindCarriageway(Probe, Hit, RibbonIndex) && Roads.Ribbons.IsValidIndex(Hit.Ribbon)
				&& Roads.Ribbons[Hit.Ribbon].St.IsValidIndex(Hit.Station)
				&& FMath::Abs(FVector2d::DotProduct(Roads.Ribbons[Hit.Ribbon].St[Hit.Station].T, St.T)) > 0.85;
			bParallel = bParallel && bAlongside;
		}
		if (bParallel) Out[i] = 0;
	}
	return Out;
}

namespace
{
	/** Second pass only (no start passed with every side cut taken for a junction): a long side cut is a
	 *  carriageway beside this one (JunctionCuts), not a junction. */
	bool GParallelCutsAllowed = false;
}

TArray<FOsdsStartPose> OsdsStartPick::Candidates(const TArray<FOsdsStartTile>& Tiles, const FString& RoadName, const FVector& NearCm,
	double RadiusM, double PreferredYawDeg, int32 MaxCandidates, FString* OutWhyNone)
{
	TArray<FOsdsStartPose> Out;
	// Why stations in reach were left out, for the log when none is left.
	int32 InReach = 0, NumJunction = 0, NumEdge = 0, NumCurve = 0, NumCurveAhead = 0, NumRoom = 0, NumFootprint = 0, NumJudgeEdge = 0, NumJudgeLine = 0;
	FString FirstFootprint;
	// The road faces of each tile, binned once for this call.
	TMap<const FOsdsTileBuild*, TSharedPtr<OsdsRoadCheck::FTopSurface>> TopCache;
	GTops = &TopCache;
	ON_SCOPE_EXIT { GTops = nullptr; };
	const bool bPreferred = PreferredYawDeg < 1e9;
	const FVector2d Wanted = bPreferred ? FVector2d(FMath::Cos(FMath::DegreesToRadians(PreferredYawDeg)), FMath::Sin(FMath::DegreesToRadians(PreferredYawDeg))) : FVector2d::ZeroVector;
	const FVector2d Near(NearCm.X / 100.0, NearCm.Y / 100.0);
	for (int32 t = 0; t < Tiles.Num(); ++t)
	{
		const FOsdsTileBuild* B = Tiles[t].Build;
		if (!B) continue;
		for (int32 r = 0; r < B->Roads.Ribbons.Num(); ++r)
		{
			const FOsdsRibbon& Ribbon = B->Roads.Ribbons[r];
			if (!IsPublic(Ribbon) || (!RoadName.IsEmpty() && !Ribbon.Name.Equals(RoadName, ESearchCase::IgnoreCase))) continue;
			// 30 m or more from any junction: where another carriageway runs into this one's side (a
			// cut), and where this ribbon's own end meets another road (a T: Welch, WV started 26 m
			// from one, on the flared kerb of the corner).
			TArray<uint8> NearJunction;
			NearJunction.Init(0, Ribbon.St.Num());
			auto MarkNear = [&](double S) { for (int32 k = 0; k < Ribbon.St.Num(); ++k) if (FMath::Abs(Ribbon.St[k].S - S) < JunctionClearM) NearJunction[k] = 1; };
			TArray<uint8> Cuts;
			if (GParallelCutsAllowed) Cuts = OsdsStartPick::JunctionCuts(B->Roads, r);
			else
			{
				Cuts.Init(0, Ribbon.St.Num());
				for (int32 j = 0; j < Ribbon.St.Num(); ++j)
				{
					Cuts[j] = (Ribbon.Sides[0].Cut.IsValidIndex(j) && Ribbon.Sides[0].Cut[j]) || (Ribbon.Sides[1].Cut.IsValidIndex(j) && Ribbon.Sides[1].Cut[j]) ? 1 : 0;
				}
			}
			for (int32 j = 0; j < Ribbon.St.Num(); ++j) if (Cuts[j]) MarkNear(Ribbon.St[j].S);
			for (const int32 End : { 0, Ribbon.St.Num() - 1 })
			{
				if ((End == 0 && Ribbon.bStartOnBoundary) || (End > 0 && Ribbon.bEndOnBoundary)) continue; // a tile seam, not a junction
				FOsdsRoadHit Hit;
				if (B->Roads.FindCarriageway(Ribbon.St[End].C, Hit, r, 2.0)) MarkNear(Ribbon.St[End].S);
			}
			for (int32 i = 1; i + 1 < Ribbon.St.Num(); ++i)
			{
				const FOsdsRibbonStation& St = Ribbon.St[i];
				const double D = FVector2d::Distance(World(Tiles[t], St.C), Near);
				if (D > RadiusM) continue;
				++InReach;
				if (NearJunction[i]) { ++NumJunction; continue; }
				// 25 m or more inside the tile: the seam's road ends meet the next tile's there.
				if (St.C.X < 25.0 || St.C.Y < 25.0 || St.C.X > B->TileSizeM - 25.0 || St.C.Y > B->TileSizeM - 25.0) { ++NumEdge; continue; }
				// A straight piece: under 15 degrees of turn over 12 m each way (the car faces the road
				// within a few degrees wherever it stands on it).
				const FVector2d Back = Ribbon.St[StationAt(Ribbon, St.S - 12.0)].T, Fore = Ribbon.St[StationAt(Ribbon, St.S + 12.0)].T;
				if (FVector2d::DotProduct(Back, Fore) < FMath::Cos(FMath::DegreesToRadians(15.0))) { ++NumCurve; continue; }
				for (const int32 Way : { 1, -1 })
				{
					if (St.Travel != 0 && St.Travel != Way) continue;
					// The road ahead of the nose is straight too: under 10 degrees of turn over the next
					// 20 m (the drawn road curving away from the car's heading).
					if (FVector2d::DotProduct(St.T, Ribbon.St[StationAt(Ribbon, St.S + 20.0 * Way)].T) < FMath::Cos(FMath::DegreesToRadians(10.0))) { ++NumCurveAhead; continue; }
					bool bEnd = false;
					const double Room = FMath::Abs(OsdsTrafficModel::NextDecisionS(Ribbon, Way, St.S, -1e9, bEnd) - St.S);
					if (Room < 25.0) { ++NumRoom; continue; }
					FOsdsStartPose Pose;
					Pose.Tile = t; Pose.Ribbon = r; Pose.Station = i; Pose.Dir = Way; Pose.DistanceM = D;
					const FVector2d Dir = St.T * static_cast<double>(Way);
					// The lane the traffic drives in this direction, between the lines the markings draw.
					const double L = OsdsTrafficModel::LaneOffsetM(Ribbon, i, Way);
					const FVector2d P = St.C + St.R * L;
					Pose.LocationCm = Tiles[t].OriginCm + FVector(P.X * 100.0, P.Y * 100.0, Ribbon.CarriagewayHeight(i, L) * 100.0);
					Pose.Rotation = FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.0);
					FString Why;
					if (!OnCarriageway(Tiles, Pose, &Why, &Pose.CheckCm)) { if (NumFootprint++ == 0) FirstFootprint = Why; continue; }
					// The drive judge's own edge (its nearest aligned line and that line's half width):
					// the car's middle 1.5 m or more inside it (a car's half width and 0.5 m), and that
					// line is this ribbon (the robot's first route line is the start's road, not a cross
					// street). Welch, WV passed the footprint check and still stood 2.0 m past it.
					int32 JudgeTile = INDEX_NONE, JudgeRibbon = INDEX_NONE;
					if (JudgePastEdgeM(Tiles, Pose.LocationCm, Pose.Rotation.Yaw, &JudgeTile, &JudgeRibbon) > -JudgeInsideM) { ++NumJudgeEdge; continue; }
					if (JudgeTile != t || JudgeRibbon != r) { ++NumJudgeLine; continue; }
					Pose.AheadM = AheadM(Tiles, t, r, St.S, Way, WantedAheadM);
					// Nearest first among the starts with a road ahead; a short road (a dead end in
					// reach) only when there is no other; the requested heading where the road allows.
					Pose.Score = D + (Pose.AheadM < WantedAheadM ? 10000.0 + 10.0 * (WantedAheadM - Pose.AheadM) : 0.0)
						+ (bPreferred && FVector2d::DotProduct(Dir, Wanted) < 0.0 ? 2000.0 : 0.0);
					Out.Add(Pose);
				}
			}
		}
	}
	Out.Sort([](const FOsdsStartPose& A, const FOsdsStartPose& B) { return A.Score < B.Score; });
	// One candidate per ribbon and direction within 40 m of a better one, so the list offers
	// different places, not the stations of one stretch.
	TArray<FOsdsStartPose> Distinct;
	for (const FOsdsStartPose& P : Out)
	{
		bool bClose = false;
		for (const FOsdsStartPose& Q : Distinct)
		{
			if (Q.Tile == P.Tile && Q.Ribbon == P.Ribbon && Q.Dir == P.Dir && FVector::Dist2D(Q.LocationCm, P.LocationCm) < 4000.0) { bClose = true; break; }
		}
		if (!bClose) Distinct.Add(P);
		if (Distinct.Num() >= MaxCandidates) break;
	}
	// None: once more, with a long side cut taken for a carriageway beside this one (a bridge's two
	// one-way decks with no gap), not a junction.
	if (Distinct.IsEmpty() && !GParallelCutsAllowed)
	{
		GParallelCutsAllowed = true;
		TArray<FOsdsStartPose> Loose = Candidates(Tiles, RoadName, NearCm, RadiusM, PreferredYawDeg, MaxCandidates, nullptr);
		GParallelCutsAllowed = false;
		if (!Loose.IsEmpty()) return Loose;
	}
	if (OutWhyNone && Distinct.IsEmpty())
	{
		*OutWhyNone = FString::Printf(TEXT("%d tiles, %d stations in reach: %d near a junction, %d near the tile edge, %d on a curve, %d curve ahead, %d short of room, %d off the carriageway (first: %s), %d outside the judge's edge, %d on another judge line"),
			Tiles.Num(), InReach, NumJunction, NumEdge, NumCurve, NumCurveAhead, NumRoom, NumFootprint, FirstFootprint.IsEmpty() ? TEXT("none") : *FirstFootprint, NumJudgeEdge, NumJudgeLine);
	}
	return Distinct;
}
