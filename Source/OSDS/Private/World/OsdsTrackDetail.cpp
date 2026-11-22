// Copyright OSDS contributors. All Rights Reserved.

#include "World/OsdsTrackDetail.h"

#include "World/OsdsGeo.h"
#include "World/OsdsRoadNetwork.h"
#include "World/OsdsTileData.h"
#include "World/OsdsTracks.h"

namespace
{
	constexpr double LiftM = 0.006;          // paint over the carriageway (OsdsTileBuilder MarkLiftM)
	constexpr double KerbLiftM = 0.012;      // kerbs a little above the paint
	constexpr double EdgeLineM = 0.2;        // 8 in edge lines
	constexpr double EdgeInsetM = 0.35;      // their centre from the paved edge
	constexpr double StartLineM = 0.9;       // the start/finish stripe across the track
	constexpr double WallDepthM = 0.6;
	constexpr double SaferDepthM = 0.5;      // the SAFER barrier stands this far off the wall's face
	constexpr double SaferTopM = 1.05;
	constexpr double FenceSpacingM = 3.7;    // a post every 12 ft
	constexpr double PostHalfM = 0.07;
	constexpr double PitWallTopM = 1.05;
	constexpr double KerbWidthM = 1.0;
	constexpr double KerbBlockM = 1.2;       // alternate red and white every 4 ft
	constexpr double OnLoopWeight = 0.95;

	const FLinearColor White(0.62f, 0.62f, 0.60f, 1.f);       // OsdsTileBuilder's line white
	const FLinearColor Yellow(0.70f, 0.48f, 0.08f, 1.f);
	const FLinearColor KerbRed(0.42f, 0.035f, 0.03f, 1.f);
	const FLinearColor KerbWhite(0.66f, 0.66f, 0.64f, 1.f);
	const FLinearColor FenceGrey(0.30f, 0.31f, 0.31f, 1.f);

	FVector Cm(const FVector2d& P, double Z) { return FVector(P.X * 100.0, P.Y * 100.0, Z * 100.0); }

	/** A flat quad on the carriageway between stations I and I+1, from lateral L0 to L1 (m), along t in [T0, T1]. */
	void PaintQuad(FOsdsMeshData& Paint, const FOsdsRibbon& R, int32 I, double T0, double T1, double L0, double L1, double Lift, const FLinearColor& Color)
	{
		const FOsdsRibbonStation& A = R.St[I];
		const FOsdsRibbonStation& B = R.St[I + 1];
		FVector Q[4];
		for (int32 e = 0; e < 2; ++e)
		{
			const double T = e == 0 ? T0 : T1;
			const FVector2d C = FMath::Lerp(A.C, B.C, T);
			const FVector2d Rn = FMath::Lerp(A.R, B.R, T).GetSafeNormal();
			const double HalfW = FMath::Lerp(A.HalfW, B.HalfW, T);
			for (int32 s = 0; s < 2; ++s)
			{
				const double L = FMath::Clamp(s == 0 ? L0 : L1, -HalfW, HalfW);
				const double H = FMath::Lerp(R.CarriagewayHeight(I, L), R.CarriagewayHeight(I + 1, L), T) + Lift;
				Q[e * 2 + s] = Cm(C + Rn * L, H);
			}
		}
		const FVector Up = FVector::CrossProduct(Q[2] - Q[0], Q[1] - Q[0]).GetSafeNormal();
		const FVector N = Up.Z >= 0.0 ? Up : -Up;
		const int32 I0 = Paint.Add(Q[0], N, Color), I1 = Paint.Add(Q[1], N, Color);
		const int32 I2 = Paint.Add(Q[3], N, Color), I3 = Paint.Add(Q[2], N, Color);
		Paint.Quad(I0, I1, I2, I3, N);
	}

	/** A vertical face between two bottom points and two top points, facing Facing. */
	void Face(FOsdsMeshData& M, const FVector& A, const FVector& B, const FVector& BTop, const FVector& ATop, const FVector& Facing, bool bUV = false, const FLinearColor& C = FLinearColor::White)
	{
		const FVector N = Facing.GetSafeNormal();
		auto Add = [&](const FVector& P, double U, double V)
		{
			return bUV ? M.Add(P, N, C, FVector2D(U, V), FVector2D(OsdsSurfaceKind::Metal, 0.0)) : M.Add(P, N, C);
		};
		const double Len = FVector::Dist2D(A, B) / 100.0;
		const int32 Ia = Add(A, 0.0, A.Z / 100.0), Ib = Add(B, Len, B.Z / 100.0), Ic = Add(BTop, Len, BTop.Z / 100.0), Id = Add(ATop, 0.0, ATop.Z / 100.0);
		M.Quad(Ia, Ib, Ic, Id, Facing);
	}

	/** An upright post (Props, metal): foot centre (cm), half size and height (m). */
	void Post(FOsdsMeshData& Props, const FVector& Foot, double Half, double Height)
	{
		const FVector X(Half * 100.0, 0, 0), Y(0, Half * 100.0, 0), Z(0, 0, Height * 100.0);
		const FVector A = Foot - X - Y, B = Foot + X - Y, C = Foot + X + Y, D = Foot - X + Y;
		Face(Props, A, B, B + Z, A + Z, -FVector::YAxisVector, true, FenceGrey);
		Face(Props, B, C, C + Z, B + Z, FVector::XAxisVector, true, FenceGrey);
		Face(Props, C, D, D + Z, C + Z, FVector::YAxisVector, true, FenceGrey);
		Face(Props, D, A, A + Z, D + Z, -FVector::XAxisVector, true, FenceGrey);
	}

	/** Signed curvature (1/m) of a ribbon at station I over about +-Arc metres. */
	double CurvatureAt(const FOsdsRibbon& R, int32 I, double Arc)
	{
		int32 J = I, K = I;
		while (J + 1 < R.St.Num() && R.St[J].S - R.St[I].S < Arc) ++J;
		while (K > 0 && R.St[I].S - R.St[K].S < Arc) --K;
		const double Ds = R.St[J].S - R.St[K].S;
		if (Ds < 4.0) return 0.0;
		const double Turn = FMath::Atan2((R.St[K].T.X * R.St[J].T.Y - R.St[K].T.Y * R.St[J].T.X), FVector2d::DotProduct(R.St[K].T, R.St[J].T));
		return Turn / Ds;
	}
}

OsdsTrackDetail::FCounts OsdsTrackDetail::Build(const OsdsTileDetail::FContext& X)
{
	FCounts Out;
	const FOsdsRoadNetwork& Roads = X.B.Roads;
	bool bAnyTrack = false;
	for (const FOsdsRibbon& R : Roads.Ribbons) bAnyTrack |= R.bTrack && R.St.Num() >= 2;
	if (!bAnyTrack) return Out;
	const FOsdsTrack* Track = OsdsTracks::Near(X.LonDeg, X.LatDeg);
	FOsdsTrackFrame Frame;
	const bool bOval = Track && OsdsTracks::MakeFrame(*Track, X.Data.Id, X.Side, Frame);
	FOsdsMeshData& Paint = X.B.Layer(EOsdsTileLayer::Paint);
	FOsdsMeshData& Concrete = X.B.Layer(EOsdsTileLayer::Concrete);
	FOsdsMeshData& Props = X.B.Layer(EOsdsTileLayer::Props);
	const FVector2d StartM = Track ? (OsdsGeo::LonLatToTile(Track->StartLon, Track->StartLat) - FVector2d(X.Data.Id.X, X.Data.Id.Y)) * X.Side : FVector2d(-1e9, -1e9);
	const bool bStartHere = Track && StartM.X >= 0.0 && StartM.Y >= 0.0 && StartM.X <= X.Side && StartM.Y <= X.Side;
	int32 StartRibbon = INDEX_NONE, StartStation = INDEX_NONE;
	double StartT = 0.0, StartDist = 1e9;

	for (int32 r = 0; r < Roads.Ribbons.Num(); ++r)
	{
		const FOsdsRibbon& R = Roads.Ribbons[r];
		const int32 NS = R.St.Num();
		if (!R.bTrack || NS < 2) continue;

		// Per station: on the oval's loop (weight), which side is the outside (+1: the +R side).
		TArray<double> OnLoop, Outside;
		OnLoop.Init(0.0, NS); Outside.Init(0.0, NS);
		if (bOval)
		{
			for (int32 i = 0; i < NS; ++i)
			{
				double Slope = 0.0, W = 0.0, Sign = 0.0;
				if (OsdsTracks::BankAt(Frame, R.St[i].C, R.St[i].R, R.St[i].HalfW, Slope, W, Sign)) { OnLoop[i] = W; Outside[i] = Sign; }
			}
		}

		// Edge lines on both sides, except where another carriageway joins (a cut side).
		for (int32 i = 0; i + 1 < NS; ++i)
		{
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const double Sign = Side == 0 ? -1.0 : 1.0;
				if (R.Sides[Side].Cut.IsValidIndex(i) && (R.Sides[Side].Cut[i] || R.Sides[Side].Cut[i + 1])) continue;
				const bool bInside = !R.bPit && OnLoop[i] > 0.5 && Outside[i] != 0.0 && Sign != Outside[i];
				const double HalfW = R.St[i].HalfW;
				if (bInside)
				{
					// The double yellow line at the bottom of the banking.
					for (const double Off : { 0.0, 0.3 })
					{
						const double L = Sign * (HalfW - EdgeInsetM - Off);
						PaintQuad(Paint, R, i, 0.0, 1.0, L - 0.5 * 0.12, L + 0.5 * 0.12, LiftM, Yellow);
						++Out.LineQuads;
					}
				}
				else
				{
					const double L = Sign * (HalfW - EdgeInsetM);
					PaintQuad(Paint, R, i, 0.0, 1.0, L - 0.5 * EdgeLineM, L + 0.5 * EdgeLineM, LiftM, White);
					++Out.LineQuads;
				}
			}
		}

		// The start/finish line: the racing-surface station nearest the curated start point.
		if (bStartHere && !R.bPit)
		{
			for (int32 i = 0; i + 1 < NS; ++i)
			{
				const FVector2d A = R.St[i].C, B = R.St[i + 1].C;
				const double L2 = FMath::Max((B - A).SizeSquared(), 1e-9);
				const double T = FMath::Clamp(FVector2d::DotProduct(StartM - A, B - A) / L2, 0.0, 1.0);
				const double D = FVector2d::Distance(StartM, A + (B - A) * T);
				if (D < StartDist && D < R.St[i].HalfW + 6.0) { StartDist = D; StartRibbon = r; StartStation = i; StartT = T; }
			}
		}

		// The oval's outside wall: SAFER barrier, concrete wall and catch fence, where the station is on the loop.
		if (bOval && !R.bPit)
		{
			double NextPostS = FMath::CeilToDouble(R.St[0].S / FenceSpacingM) * FenceSpacingM;
			for (int32 i = 0; i + 1 < NS; ++i)
			{
				if (OnLoop[i] < OnLoopWeight || OnLoop[i + 1] < OnLoopWeight || Outside[i] == 0.0 || Outside[i] != Outside[i + 1]) continue;
				const double Sign = Outside[i];
				const int32 Side = Sign > 0.0 ? 1 : 0;
				if (R.Sides[Side].Cut.IsValidIndex(i) && (R.Sides[Side].Cut[i] || R.Sides[Side].Cut[i + 1])) continue;
				FVector Foot[2], Barrier[2], WallIn[2], WallOut[2];
				double Edge[2];
				for (int32 e = 0; e < 2; ++e)
				{
					const FOsdsRibbonStation& St = R.St[i + e];
					Edge[e] = R.CarriagewayHeight(i + e, Sign * St.HalfW);
					Foot[e] = Cm(St.C + St.R * (Sign * St.HalfW), Edge[e] - 0.05);
					Barrier[e] = Cm(St.C + St.R * (Sign * (St.HalfW + 0.05)), Edge[e] - 0.3);
					WallIn[e] = Cm(St.C + St.R * (Sign * (St.HalfW + SaferDepthM)), Edge[e] - 0.3);
					WallOut[e] = Cm(St.C + St.R * (Sign * (St.HalfW + SaferDepthM + WallDepthM)), Edge[e] - 3.0);
				}
				const FVector2d Rn = R.St[i].R * Sign;
				const FVector Toward(-Rn.X, -Rn.Y, 0.0); // the wall's track face looks back at the track
				const FVector Away(Rn.X, Rn.Y, 0.0);
				const FVector Up = FVector::UpVector;
				auto Z = [](FVector P, double Zm) { P.Z = Zm * 100.0; return P; };
				// The SAFER barrier: a face of steel tubes 1.05 m tall just off the paved edge, and its top.
				Face(Concrete, Barrier[0], Barrier[1], Z(Barrier[1], Edge[1] + SaferTopM), Z(Barrier[0], Edge[0] + SaferTopM), Toward);
				Face(Concrete, Z(Barrier[0], Edge[0] + SaferTopM), Z(Barrier[1], Edge[1] + SaferTopM), Z(WallIn[1], Edge[1] + SaferTopM), Z(WallIn[0], Edge[0] + SaferTopM), Up);
				// The concrete wall behind it: its face above the barrier, its top and its back.
				Face(Concrete, Z(WallIn[0], Edge[0] + SaferTopM), Z(WallIn[1], Edge[1] + SaferTopM), Z(WallIn[1], Edge[1] + WallTopM), Z(WallIn[0], Edge[0] + WallTopM), Toward);
				const FVector OutTop0 = Z(WallOut[0], Edge[0] + WallTopM), OutTop1 = Z(WallOut[1], Edge[1] + WallTopM);
				Face(Concrete, Z(WallIn[0], Edge[0] + WallTopM), Z(WallIn[1], Edge[1] + WallTopM), OutTop1, OutTop0, Up);
				Face(Concrete, WallOut[1], WallOut[0], OutTop0, OutTop1, Away);
				// The strip between the paved edge and the barrier's foot.
				Face(Concrete, Foot[0], Foot[1], Barrier[1] + FVector(0, 0, 25.0), Barrier[0] + FVector(0, 0, 25.0), Up);
				++Out.WallSegments;
				// The catch fence: posts on the wall top, cables between them (drawn per segment).
				for (; NextPostS <= R.St[i + 1].S; NextPostS += FenceSpacingM)
				{
					if (NextPostS < R.St[i].S) continue;
					const double T = (NextPostS - R.St[i].S) / FMath::Max(R.St[i + 1].S - R.St[i].S, 1e-6);
					const FVector2d C = FMath::Lerp(R.St[i].C, R.St[i + 1].C, T);
					const FVector2d Rr = FMath::Lerp(R.St[i].R, R.St[i + 1].R, T).GetSafeNormal();
					const double HalfW = FMath::Lerp(R.St[i].HalfW, R.St[i + 1].HalfW, T);
					const double Top = FMath::Lerp(Edge[0], Edge[1], T) + WallTopM;
					Post(Props, Cm(C + Rr * (Sign * (HalfW + SaferDepthM + 0.5 * WallDepthM)), Top), PostHalfM, FenceTopM - WallTopM);
					++Out.FencePosts;
				}
				for (const double H : { 0.6, 1.9, 3.2, 4.5, FenceTopM - WallTopM - 0.05 })
				{
					const double Lw = SaferDepthM + 0.5 * WallDepthM - 0.04;
					FVector P[2];
					for (int32 e = 0; e < 2; ++e) P[e] = Cm(R.St[i + e].C + R.St[i + e].R * (Sign * (R.St[i + e].HalfW + Lw)), Edge[e] + WallTopM + H);
					Face(Props, P[0], P[1], P[1] + FVector(0, 0, 4.0), P[0] + FVector(0, 0, 4.0), Toward, true, FenceGrey);
					Face(Props, P[1], P[0], P[0] + FVector(0, 0, 4.0), P[1] + FVector(0, 0, 4.0), Away, true, FenceGrey);
				}
			}
		}

		// The pit wall: on the pit road's track side, where the racing surface runs alongside.
		if (bOval && R.bPit)
		{
			const double TrackHalf = 0.5 * (Track ? Track->WidthM : 14.0);
			for (int32 i = 0; i + 1 < NS; ++i)
			{
				double Dist[2] = { 0, 0 };
				int32 Side = INDEX_NONE;
				bool bOk = true;
				for (int32 e = 0; e < 2; ++e)
				{
					const FOsdsRibbonStation& St = R.St[i + e];
					double DL = 0, DR = 0, Tw = 0; FVector2d On; bool bF = false;
					Frame.Nearest(St.C - St.R * (St.HalfW + 1.0), DL, On, Tw, bF);
					Frame.Nearest(St.C + St.R * (St.HalfW + 1.0), DR, On, Tw, bF);
					const int32 S = DR < DL ? 1 : 0;
					if (Side != INDEX_NONE && S != Side) bOk = false;
					Side = S;
					Dist[e] = FMath::Min(DL, DR);
					// Room for a wall between the two carriageways, and the track close by (not a garage lane).
					if (Dist[e] < TrackHalf + 1.5 || Dist[e] > TrackHalf + 30.0) bOk = false;
				}
				if (!bOk || (R.Sides[Side].Cut.IsValidIndex(i) && (R.Sides[Side].Cut[i] || R.Sides[Side].Cut[i + 1]))) continue;
				const double Sign = Side == 1 ? 1.0 : -1.0;
				FVector In[2], OutP[2];
				double Edge[2];
				for (int32 e = 0; e < 2; ++e)
				{
					const FOsdsRibbonStation& St = R.St[i + e];
					Edge[e] = R.CarriagewayHeight(i + e, Sign * St.HalfW);
					In[e] = Cm(St.C + St.R * (Sign * (St.HalfW + 0.2)), Edge[e] - 0.5);
					OutP[e] = Cm(St.C + St.R * (Sign * (St.HalfW + 0.8)), Edge[e] - 0.5);
				}
				const FVector2d Rn = R.St[i].R * Sign;
				auto Z = [](FVector P, double Zm) { P.Z = Zm * 100.0; return P; };
				Face(Concrete, In[0], In[1], Z(In[1], Edge[1] + PitWallTopM), Z(In[0], Edge[0] + PitWallTopM), FVector(-Rn.X, -Rn.Y, 0.0));
				Face(Concrete, Z(In[0], Edge[0] + PitWallTopM), Z(In[1], Edge[1] + PitWallTopM), Z(OutP[1], Edge[1] + PitWallTopM), Z(OutP[0], Edge[0] + PitWallTopM), FVector::UpVector);
				Face(Concrete, OutP[1], OutP[0], Z(OutP[0], Edge[0] + PitWallTopM), Z(OutP[1], Edge[1] + PitWallTopM), FVector(Rn.X, Rn.Y, 0.0));
				++Out.WallSegments;
			}
		}

		// Road-course kerbs: red and white blocks on the inside of a corner and the outside of its exit.
		if (!R.bPit && (!bOval || OnLoop.ContainsByPredicate([](double W) { return W < 0.05; })))
		{
			for (int32 i = 0; i + 1 < NS; ++i)
			{
				if (bOval && OnLoop[i] > 0.05) continue;
				if (FMath::Abs(CurvatureAt(R, i, 10.0)) < 1.0 / KerbRadiusM) continue;
				// The inside is the side the road turns toward (read from the tangent 5 stations on).
				const FVector2d Ahead = R.St[FMath::Min(i + 5, NS - 1)].T;
				const double Toward = FVector2d::DotProduct(Ahead - R.St[i].T, R.St[i].R);
				const double InsideSign = Toward >= 0.0 ? 1.0 : -1.0;
				const double HalfW = R.St[i].HalfW;
				const double Seg = FMath::Max(R.St[i + 1].S - R.St[i].S, 1e-6);
				for (double S = FMath::CeilToDouble(R.St[i].S / KerbBlockM) * KerbBlockM; S < R.St[i + 1].S; S += KerbBlockM)
				{
					const double T0 = (S - R.St[i].S) / Seg;
					const double T1 = FMath::Min((S + KerbBlockM - R.St[i].S) / Seg, 1.0);
					const bool bRed = (FMath::FloorToInt(S / KerbBlockM) & 1) == 0;
					const double L1 = InsideSign * HalfW, L0 = InsideSign * (HalfW - KerbWidthM);
					PaintQuad(Paint, R, i, T0, T1, FMath::Min(L0, L1), FMath::Max(L0, L1), KerbLiftM, bRed ? KerbRed : KerbWhite);
					++Out.KerbBlocks;
				}
			}
		}
	}

	if (StartRibbon != INDEX_NONE)
	{
		const FOsdsRibbon& R = Roads.Ribbons[StartRibbon];
		const double Seg = FMath::Max(R.St[StartStation + 1].S - R.St[StartStation].S, 1e-6);
		const double Half = 0.5 * StartLineM / Seg;
		const double HalfW = R.St[StartStation].HalfW;
		PaintQuad(Paint, R, StartStation, FMath::Max(StartT - Half, 0.0), FMath::Min(StartT + Half, 1.0), -HalfW, HalfW, LiftM + 0.002, White);
		Out.bStartLine = true;
	}
	return Out;
}
