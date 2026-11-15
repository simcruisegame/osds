// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseRoadNetwork.h"

#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileData.h"

#include <limits>

namespace
{
	constexpr double MergeM = 2.0;            // endpoint tolerance when chaining (road-grade.mjs MERGE)
	constexpr double SigmaM = 16.0;           // profile fit (road-grade.mjs SIGMA)
	constexpr double TukeyScale = 3.5;
	constexpr double FilletMaxM = 1.0;        // largest plan offset from a mapped vertex on gentle bends (edge-authority)
	constexpr double FilletClearance = 1.15;  // a sharp bend gets a radius this many times the ribbon's outer half width
	constexpr double PinSpanM = 30.0;         // boundary pin fade (and superelevation run-out)
	constexpr double JoinFadeM = 15.0;        // RoadSurface.gd JOIN_FADE_M
	constexpr double JoinProtectM = 10.0;     // joins fade out this close to a pinned boundary end
	constexpr double JoinStepM = 0.5;         // station spacing where ribbons overlap
	constexpr double SeamClearM = 0.02;       // a seam section's lowest edge over the shared ground
	constexpr double PadMaxRadiusM = 16.0;    // closed rings up to this size are turnaround pads
	constexpr int32 PadSegments = 32;
	constexpr double JoinEdgeMarginM = 1.0;   // a lower ribbon stays under a higher one's edge this far out
	constexpr double CornerSplitDeg = 55.0;   // a sharper bend the fillet cannot round for the whole section is a corner
	constexpr double WidthTaperM = 10.0;      // road-grade.mjs WIDTH_TAPER
	constexpr double PlanSigmaM = 4.0;        // plan-view smoothing of the mapped centreline
	constexpr double PlanEndM = 6.0;          // chain ends keep their exact point and direction
	constexpr double ContractBlendM = 10.0;   // a seam end takes the neighbour's direction and width over this
	constexpr double BoundaryEps = 1e-3;      // m: a point this near the edge lies on it
	constexpr double DeckMaxGrade = 0.06;     // a deck's line and its sags to a road at grade: 6 % at most
	constexpr double DeckClearM = 4.9;        // a deck's underside over a road it crosses (16 ft)
	constexpr double DeckDepthM = 1.2;        // the deck below its carriageway (SimCruiseBridges.cpp)
	constexpr double RoadCutGrade = 0.04;     // the steepest grade a cut adds to a freeway under a bridge
	constexpr double LevelGapM = 2.0;         // two surfaces this far apart in height are two levels: the join never makes them one
	constexpr double DeckAtGradeM = 2.5;      // a deck less than this over a road it crosses meets it (its slab would stand at a car's height)
	constexpr double DeckFaceWindowM = 2.5;   // a deck within this of a road under it stands its parapet or slab in a car's way (0.85 over, 1.2 under the deck)

	// Kerb evidence (RoadSurface.gd classify_edges).
	constexpr double KerbSidewalkMinM = 0.3;
	constexpr double KerbSidewalkMaxM = 12.0;
	constexpr double KerbParallel = 0.93;
	constexpr int32 KerbFillStations = 5;
	constexpr int32 KerbMinStations = 5;
	constexpr double TownCellM = 50.0;
	constexpr int32 TownBuildings = 15;

	double Smooth01(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double Cross2(const FVector2d& A, const FVector2d& B) { return A.X * B.Y - A.Y * B.X; }

	double DesignSpeedKmh(const FString& Class)
	{
		if (Class == TEXT("motorway") || Class == TEXT("trunk")) return 90.0;
		if (Class == TEXT("primary") || Class == TEXT("secondary")) return 60.0;
		// County and rural roads bank their bends too (AASHTO Green Book 3.3: e max 4-8 % on
		// rural roads at their 50-70 km/h design speeds); residential streets and service ways
		// keep a normal crown (low-speed urban streets, AASHTO 3.3.6).
		if (Class == TEXT("tertiary")) return 55.0;
		if (Class == TEXT("unclassified")) return 45.0;
		return 0.0;
	}

	struct FPiece
	{
		TArray<FVector2d> P;
		TArray<double> S;
		double Length = 0.0;
		double WidthM = 8.0;
		int32 OneWay = 0;
		int32 Rank = 0;
		FString Class;
		FString Name;
		int32 Contract[2] = { INDEX_NONE, INDEX_NONE }; // neighbour seam end each piece end was snapped to
		bool bBridge = false;
		int32 Layer = 0;
		bool bUnpaved = false;
	};

	struct FPart
	{
		int32 Piece = 0;
		bool bReversed = false;
		double Offset = 0.0;
	};

	struct FChainVertex
	{
		FVector2d P;
		double S = 0.0;
		bool bHard = false;
		double TLen = 0.0;   // fillet tangent length
		double Theta = 0.0;
		double Turn = 0.0;
		FVector2d A = FVector2d::ZeroVector; // unit direction of the incoming leg
	};

	/** Weighted Gaussian local-linear estimate at s (road-grade.mjs gaussianLinear). */
	double GaussianLinear(const TArray<double>& S, const TArray<double>& Z, const TArray<double>& W, double At, double Sigma)
	{
		const double Window = 3.0 * Sigma;
		int32 Lo = 0, Hi = S.Num();
		while (Lo < Hi)
		{
			const int32 M = (Lo + Hi) >> 1;
			if (S[M] < At - Window) Lo = M + 1; else Hi = M;
		}
		double Sw = 0, Swx = 0, Swy = 0, Swxx = 0, Swxy = 0;
		for (int32 i = Lo; i < S.Num() && S[i] <= At + Window; ++i)
		{
			if (!(W[i] > 0.0)) continue;
			const double X = S[i] - At;
			const double Wt = W[i] * FMath::Exp(-0.5 * X * X / (Sigma * Sigma));
			Sw += Wt; Swx += Wt * X; Swy += Wt * Z[i]; Swxx += Wt * X * X; Swxy += Wt * X * Z[i];
		}
		if (Sw <= 1e-9) return std::numeric_limits<double>::quiet_NaN();
		const double Det = Sw * Swxx - Swx * Swx;
		if (Det <= 1e-9 * FMath::Max(Sw * Swxx, 1e-12)) return Swy / Sw;
		return (Swy - Swx * (Sw * Swxy - Swx * Swy) / Det) / Sw;
	}

	/** Join pieces end to end (road-grade.mjs chainPieces). */
	TArray<TArray<FPart>> ChainPieces(const TArray<FPiece>& Pieces, double Side)
	{
		struct FEnd { int32 Piece; int32 At; FVector2d P; FVector2d D; };
		TArray<FEnd> Ends;
		Ends.Reserve(Pieces.Num() * 2);
		for (int32 i = 0; i < Pieces.Num(); ++i)
		{
			const TArray<FVector2d>& P = Pieces[i].P;
			Ends.Add({ i, 0, P[0], P[1] - P[0] });
			Ends.Add({ i, 1, P.Last(), P[P.Num() - 2] - P.Last() });
		}
		TMap<FIntPoint, TArray<int32>> Grid;
		auto Key = [](const FVector2d& P) { return FIntPoint(FMath::FloorToInt(P.X / MergeM), FMath::FloorToInt(P.Y / MergeM)); };
		// An end on the tile edge is where the map was clipped: the road goes on in the
		// neighbour tile, so it never joins another piece that happens to end nearby.
		for (int32 e = 0; e < Ends.Num(); ++e)
		{
			const FVector2d& P = Ends[e].P;
			if (P.X <= BoundaryEps || P.Y <= BoundaryEps || P.X >= Side - BoundaryEps || P.Y >= Side - BoundaryEps) continue;
			Grid.FindOrAdd(Key(P)).Add(e);
		}

		TArray<int32> Link; Link.Init(INDEX_NONE, Ends.Num());
		TArray<bool> Seen; Seen.Init(false, Ends.Num());
		for (int32 e = 0; e < Ends.Num(); ++e)
		{
			if (Seen[e]) continue;
			{
				const FVector2d& P = Ends[e].P;
				if (P.X <= BoundaryEps || P.Y <= BoundaryEps || P.X >= Side - BoundaryEps || P.Y >= Side - BoundaryEps) { Seen[e] = true; continue; }
			}
			TArray<int32> Cluster;
			const FIntPoint K = Key(Ends[e].P);
			for (int32 x = K.X - 1; x <= K.X + 1; ++x)
			for (int32 y = K.Y - 1; y <= K.Y + 1; ++y)
			{
				if (const TArray<int32>* Cell = Grid.Find(FIntPoint(x, y)))
				{
					for (int32 j : *Cell)
					{
						if (!Seen[j] && FVector2d::Distance(Ends[j].P, Ends[e].P) <= MergeM) Cluster.Add(j);
					}
				}
			}
			for (int32 j : Cluster) Seen[j] = true;
			struct FPair { double Score; int32 A; int32 B; };
			TArray<FPair> Pairs;
			for (int32 a = 0; a < Cluster.Num(); ++a)
			for (int32 b = a + 1; b < Cluster.Num(); ++b)
			{
				const FEnd& A = Ends[Cluster[a]];
				const FEnd& B = Ends[Cluster[b]];
				if (A.Piece == B.Piece) continue;
				const double La = FMath::Max(A.D.Size(), 1e-9), Lb = FMath::Max(B.D.Size(), 1e-9);
				const double Straight = -FVector2d::DotProduct(A.D, B.D) / (La * Lb);
				const FPiece& PA = Pieces[A.Piece];
				const FPiece& PB = Pieces[B.Piece];
				const double Same = (PA.Name == PB.Name ? 0.3 : 0.0) + (PA.Class == PB.Class ? 0.2 : 0.0);
				if (Cluster.Num() > 2 && Straight < 0.5) continue;
				Pairs.Add({ Straight + Same, Cluster[a], Cluster[b] });
			}
			Pairs.Sort([](const FPair& L, const FPair& R) { return L.Score > R.Score; });
			for (const FPair& Pair : Pairs)
			{
				if (Link[Pair.A] < 0 && Link[Pair.B] < 0) { Link[Pair.A] = Pair.B; Link[Pair.B] = Pair.A; }
			}
		}

		TArray<bool> Used; Used.Init(false, Pieces.Num());
		TArray<TArray<FPart>> Chains;
		auto Walk = [&](int32 StartEnd)
		{
			TArray<FPart> Parts;
			int32 End = StartEnd;
			double Offset = 0.0;
			while (End >= 0)
			{
				const FEnd& E = Ends[End];
				if (Used[E.Piece]) break;
				Used[E.Piece] = true;
				Parts.Add({ E.Piece, E.At == 1, Offset });
				Offset += Pieces[E.Piece].Length;
				End = Link[End ^ 1];
			}
			if (Parts.Num()) Chains.Add(MoveTemp(Parts));
		};
		for (int32 e = 0; e < Ends.Num(); ++e) if (!Used[Ends[e].Piece] && Link[e] < 0) Walk(e);
		for (int32 e = 0; e < Ends.Num(); ++e) if (!Used[Ends[e].Piece] && Ends[e].At == 0) Walk(e); // closed loops
		return Chains;
	}

	/** Vertices of a chain in chain order with fillets (edge-authority chainVertices). */
	TArray<FChainVertex> ChainVertices(const TArray<FPiece>& Pieces, const TArray<FPart>& Parts, double OuterHalfM)
	{
		TArray<FChainVertex> V;
		for (const FPart& Part : Parts)
		{
			const FPiece& Piece = Pieces[Part.Piece];
			const int32 N = Piece.P.Num();
			for (int32 j = 0; j < N; ++j)
			{
				const int32 K = Part.bReversed ? N - 1 - j : j;
				const FVector2d& P = Piece.P[K];
				const double Station = Part.Offset + (Part.bReversed ? Piece.Length - Piece.S[K] : Piece.S[K]);
				if (V.Num())
				{
					FChainVertex& Last = V.Last();
					if (FVector2d::Distance(Last.P, P) < 0.05)
					{
						if (j == 0) continue;
						if (Station - Last.S < 0.05) continue;
					}
					else if (j == 0)
					{
						Last.bHard = true;
						FChainVertex Joint; Joint.P = P; Joint.S = Station; Joint.bHard = true;
						V.Add(Joint);
						continue;
					}
				}
				FChainVertex Vertex; Vertex.P = P; Vertex.S = Station;
				V.Add(Vertex);
			}
		}
		if (V.Num())
		{
			V[0].bHard = true;
			V.Last().bHard = true;
		}
		for (int32 i = 1; i + 1 < V.Num(); ++i)
		{
			FChainVertex& Vx = V[i];
			if (Vx.bHard) continue;
			const FVector2d A = Vx.P - V[i - 1].P;
			const FVector2d B = V[i + 1].P - Vx.P;
			const double La = A.Size(), Lb = B.Size();
			if (La < 1e-6 || Lb < 1e-6 || Vx.S - V[i - 1].S < 1e-6 || V[i + 1].S - Vx.S < 1e-6) continue;
			const double Cross = Cross2(A, B) / (La * Lb);
			const double Dot = FVector2d::DotProduct(A, B) / (La * Lb);
			const double Theta = FMath::Atan2(FMath::Abs(Cross), Dot);
			if (Theta < 1e-4 || Theta > 2.6) continue;
			// A gentle bend stays within FilletMaxM of its mapped vertex; a sharp one gets a
			// radius its whole cross-section can follow without folding on the inside.
			const double Gentle = FilletMaxM / FMath::Tan(Theta / 4.0);
			const double Clear = FilletClearance * OuterHalfM * FMath::Tan(Theta / 2.0);
			Vx.TLen = FMath::Min3(0.5 * La, 0.5 * Lb, FMath::Max(Gentle, Clear));
			Vx.Theta = Theta;
			Vx.Turn = FMath::Sign(Cross);
			Vx.A = A / La;
		}
		return V;
	}

	void ArcPoint(const FChainVertex& V, double W, FVector2d& OutP, FVector2d& OutT)
	{
		const double F = (FMath::Clamp(W, -V.TLen, V.TLen) + V.TLen) / (2.0 * V.TLen);
		const double R = V.TLen / FMath::Tan(V.Theta / 2.0);
		const FVector2d Start = V.P - V.A * V.TLen;
		const FVector2d N(-V.A.Y * V.Turn, V.A.X * V.Turn);
		const FVector2d Centre = Start + N * R;
		const double Phi = F * V.Theta * V.Turn;
		const double C = FMath::Cos(Phi), Sn = FMath::Sin(Phi);
		const FVector2d Rv = Start - Centre;
		OutP = Centre + FVector2d(Rv.X * C - Rv.Y * Sn, Rv.X * Sn + Rv.Y * C);
		OutT = FVector2d(V.A.X * C - V.A.Y * Sn, V.A.X * Sn + V.A.Y * C);
	}

	/** Point and unit tangent of a filleted chain at station s (edge-authority chainPoint). */
	void ChainPoint(const TArray<FChainVertex>& V, double S, FVector2d& OutP, FVector2d& OutT)
	{
		int32 Lo = 0, Hi = V.Num() - 1;
		if (S <= V[0].S) Hi = 0;
		else if (S >= V[Hi].S) Lo = Hi;
		else
		{
			while (Hi - Lo > 1)
			{
				const int32 M = (Lo + Hi) >> 1;
				if (V[M].S <= S) Lo = M; else Hi = M;
			}
		}
		const FChainVertex& A = V[Lo];
		const FChainVertex& B = V[FMath::Min(Lo + 1, V.Num() - 1)];
		const double L = B.S - A.S;
		if (&A == &B || L < 1e-9)
		{
			const FChainVertex& Prev = V[FMath::Max(Lo - 1, 0)];
			const FChainVertex& Next = V[FMath::Min(Lo + 1, V.Num() - 1)];
			FVector2d D = (&A == &Prev) ? Next.P - A.P : A.P - Prev.P;
			OutP = A.P;
			OutT = D.GetSafeNormal();
			if (OutT.IsNearlyZero()) OutT = FVector2d(1.0, 0.0);
			return;
		}
		const double U = FMath::Clamp(S - A.S, 0.0, L);
		// The chord between vertices runs at constant speed in station; its real
		// length can differ from the station span at a hard joint, so use direction only.
		const FVector2d D = (B.P - A.P) / L;
		const FVector2d Dir = D.GetSafeNormal();
		if (A.TLen > 0.0 && U < A.TLen) { ArcPoint(A, U, OutP, OutT); return; }
		if (B.TLen > 0.0 && L - U < B.TLen) { ArcPoint(B, U - L, OutP, OutT); return; }
		OutP = A.P + D * U;
		OutT = Dir.IsNearlyZero() ? FVector2d(1.0, 0.0) : Dir;
	}

	bool OnBoundary(const FVector2d& P, double Side)
	{
		return P.X <= BoundaryEps || P.Y <= BoundaryEps || P.X >= Side - BoundaryEps || P.Y >= Side - BoundaryEps;
	}

	void FillGaps(TArray<uint8>& Run, int32 Gap)
	{
		int32 Last = -1;
		for (int32 i = 0; i < Run.Num(); ++i)
		{
			if (Run[i] != 1) continue;
			if (Last >= 0 && i - Last - 1 <= Gap) for (int32 j = Last + 1; j < i; ++j) Run[j] = 1;
			Last = i;
		}
	}

	void DropShort(TArray<uint8>& Run, int32 Shortest)
	{
		int32 i = 0;
		while (i < Run.Num())
		{
			if (Run[i] != 1) { ++i; continue; }
			int32 j = i;
			while (j < Run.Num() && Run[j] == 1) ++j;
			if (j - i < Shortest) for (int32 k = i; k < j; ++k) Run[k] = 0;
			i = j;
		}
	}
}

int32 FSimCruiseRoadNetwork::ClassRank(const FString& Class)
{
	if (Class == TEXT("motorway")) return 7;
	if (Class == TEXT("trunk")) return 6;
	if (Class == TEXT("primary")) return 5;
	if (Class == TEXT("secondary")) return 4;
	if (Class == TEXT("tertiary")) return 3;
	if (Class == TEXT("minor") || Class == TEXT("residential") || Class == TEXT("unclassified")) return 2;
	return 1;
}

bool FSimCruiseRoadNetwork::IsDrivableClass(const FString& Class)
{
	return Class == TEXT("motorway") || Class == TEXT("trunk") || Class == TEXT("primary") || Class == TEXT("secondary")
		|| Class == TEXT("tertiary") || Class == TEXT("minor") || Class == TEXT("service") || Class == TEXT("busway");
}

bool FSimCruiseRoadNetwork::IsMajorClass(const FString& Class)
{
	return Class == TEXT("motorway") || Class == TEXT("trunk") || Class == TEXT("primary") || Class == TEXT("secondary") || Class == TEXT("tertiary");
}

namespace SimCruiseLanes
{
	double LaneWidthM(const FString& Class)
	{
		if (Class == TEXT("motorway") || Class == TEXT("trunk")) return 3.65;
		if (Class == TEXT("primary")) return 3.5;
		if (Class == TEXT("secondary")) return 3.35;
		return 3.2;
	}

	FParking Parking(const FString& Class, double HalfW, int32 Travel, bool bKerbLeft, bool bKerbRight)
	{
		FParking Out;
		const bool bStreet = Class == TEXT("secondary") || Class == TEXT("tertiary") || Class == TEXT("residential") || Class == TEXT("unclassified");
		if (!bStreet)
		{
			return Out;
		}
		constexpr double ParkM = 2.4;
		const double Width = 2.0 * HalfW;
		if (Travel == 0)
		{
			if (bKerbLeft && bKerbRight && Width >= 2.0 * 3.0 + 2.0 * ParkM)
			{
				Out.Left = Out.Right = ParkM;
			}
			return Out;
		}
		// One-way streets park on either curb (on the right first).
		const bool bDriverRightIsR = Travel > 0;
		const bool bKerbDriverRight = bDriverRightIsR ? bKerbRight : bKerbLeft;
		const bool bKerbDriverLeft = bDriverRightIsR ? bKerbLeft : bKerbRight;
		if (bKerbLeft && bKerbRight && Width >= 3.0 + 2.0 * ParkM)
		{
			Out.Left = Out.Right = ParkM;
		}
		else if ((bKerbDriverRight || bKerbDriverLeft) && Width >= 3.0 + ParkM)
		{
			const bool bRightSide = bKerbDriverRight ? bDriverRightIsR : !bDriverRightIsR;
			(bRightSide ? Out.Right : Out.Left) = ParkM;
		}
		return Out;
	}

	void TravelLanes(const FString& Class, double HalfW, int32 Travel, const FParking& Park, int32& OutCount, double& OutWidthM)
	{
		const double Lane = LaneWidthM(Class);
		if (Travel == 0)
		{
			const double Half = HalfW - Park.Right;
			OutCount = FMath::Clamp(FMath::FloorToInt((Half - 0.6) / Lane), 1, 3);
			OutWidthM = FMath::Max(0.5, FMath::Min(Lane, (Half - 0.3) / OutCount));
			return;
		}
		const double LeftP = Travel > 0 ? Park.Left : Park.Right;
		const double RightP = Travel > 0 ? Park.Right : Park.Left;
		const double Travelled = 2.0 * HalfW - LeftP - RightP;
		OutCount = Travelled >= 2.0 * 2.75 + 0.6 ? 2 : 1;
		OutWidthM = FMath::Max(0.5, FMath::Min(Lane, (Travelled - 0.8) / OutCount));
	}

	double RightLaneCentreM(const FString& Class, double HalfW, int32 Travel, const FParking& Park)
	{
		const double Lane = LaneWidthM(Class);
		if (Travel == 0)
		{
			// Lanes run outward from the centre line on each half, inside any parking lane.
			const double Half = HalfW - Park.Right;
			const int32 PerDir = FMath::Clamp(FMath::FloorToInt((Half - 0.6) / Lane), 1, 3);
			const double Lw = FMath::Max(0.5, FMath::Min(Lane, (Half - 0.3) / PerDir));
			return (PerDir - 0.5) * Lw;
		}
		// Driver's frame: the parking on the driver's left and right.
		const double LeftP = Travel > 0 ? Park.Left : Park.Right;
		const double RightP = Travel > 0 ? Park.Right : Park.Left;
		const double Travelled = 2.0 * HalfW - LeftP - RightP;
		const int32 Lanes = Travelled >= 2.0 * 2.75 + 0.6 ? 2 : 1;
		const double Lw = FMath::Max(0.5, FMath::Min(Lane, (Travelled - 0.8) / Lanes));
		const double Centre = 0.5 * (LeftP - RightP);
		return Centre + 0.5 * Lanes * Lw - 0.5 * Lw;
	}
}

double FSimCruiseRibbon::CarriagewayHeight(int32 I, double L) const
{
	return St[I].Height(L);
}

void FSimCruiseRoadNetwork::Build(const FSimCruiseTileData& Tile, double InTileSizeM, const TArray<FSimCruiseSeamEnd>& NeighbourEnds)
{
	Ribbons.Reset();
	Pads.Reset();
	BridgeChanges.Reset();
	DecksLeftAsTheyWere.Reset();
	Bins.Reset();
	NumContractedEnds = 0;
	TileSizeM = InTileSizeM;
	const double Side = TileSizeM;

	// 1. Pieces in tile metres. Ends on the tile edge are snapped exactly onto it, then
	// onto the matching road end of an already built neighbour (seam contract): the
	// bundled and streamed tiles come from different zoom-14 vector tiles, whose
	// simplified lines cross a shared edge 0.2-0.3 m apart.
	TArray<FPiece> Pieces;
	TArray<bool> ContractUsed;
	ContractUsed.Init(false, NeighbourEnds.Num());
	auto EdgeOf = [Side](const FVector2d& P)
	{
		if (P.X <= BoundaryEps) return 0;
		if (P.X >= Side - BoundaryEps) return 1;
		if (P.Y <= BoundaryEps) return 2;
		if (P.Y >= Side - BoundaryEps) return 3;
		return -1;
	};
	for (const FSimCruiseRoadRow& Row : Tile.Roads)
	{
		if (!IsDrivableClass(Row.Class)) continue;
		FPiece Piece;
		Piece.Class = Row.Class;
		Piece.Name = Row.Name;
		Piece.WidthM = FMath::Clamp(Row.WidthM, 2.5, 40.0);
		Piece.OneWay = Row.OneWay;
		Piece.Rank = ClassRank(Row.Class);
		Piece.bBridge = Row.bBridge;
		Piece.Layer = Row.Layer;
		Piece.bUnpaved = Row.Surface == TEXT("unpaved");
		for (FVector2d U : Row.Points)
		{
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				double& C = Axis == 0 ? U.X : U.Y;
				if (FMath::Abs(C) < 2e-5) C = 0.0;
				if (FMath::Abs(C - 1.0) < 2e-5) C = 1.0;
			}
			const FVector2d P = U * Side;
			if (Piece.P.Num() == 0 || FVector2d::Distance(Piece.P.Last(), P) >= 0.01) Piece.P.Add(P);
		}
		if (Piece.P.Num() < 2) continue;
		// A small closed ring is a turnaround: a paved pad, not a ribbon.
		if (Piece.P.Num() >= 4 && FVector2d::Distance(Piece.P[0], Piece.P.Last()) < 0.5)
		{
			FVector2d Centre = FVector2d::ZeroVector;
			for (int32 k = 0; k + 1 < Piece.P.Num(); ++k) Centre += Piece.P[k];
			Centre /= (Piece.P.Num() - 1);
			double MaxR = 0.0;
			for (const FVector2d& P : Piece.P) MaxR = FMath::Max(MaxR, FVector2d::Distance(P, Centre));
			// Narrowest width of the ring: a loop thinner than its own road is paved inside anyway.
			double MinWidth = TNumericLimits<double>::Max();
			for (int32 k = 0; k < PadSegments / 2; ++k)
			{
				const double A = UE_DOUBLE_PI * k / (PadSegments / 2);
				const FVector2d U(FMath::Cos(A), FMath::Sin(A));
				double Lo = TNumericLimits<double>::Max(), Hi = TNumericLimits<double>::Lowest();
				for (const FVector2d& P : Piece.P) { const double D = FVector2d::DotProduct(P, U); Lo = FMath::Min(Lo, D); Hi = FMath::Max(Hi, D); }
				MinWidth = FMath::Min(MinWidth, Hi - Lo);
			}
			if (MaxR <= PadMaxRadiusM || (MinWidth < 1.2 * Piece.WidthM && MaxR <= 2.0 * PadMaxRadiusM))
			{
				FSimCruiseRoadPad Pad;
				Pad.Centre = Centre;
				Pad.Rank = Piece.Rank;
				Pad.Name = Piece.Name;
				// Radial outline of the ring's convex hull, pushed out by the half width.
				const double Half = 0.5 * Piece.WidthM;
				Pad.Radii.SetNum(PadSegments);
				double HeightSum = 0.0;
				for (const FVector2d& P : Piece.P) HeightSum += Tile.HeightAtUnit(P / Side);
				Pad.Height = HeightSum / Piece.P.Num();
				for (int32 k = 0; k < PadSegments; ++k)
				{
					const double A = 2.0 * UE_DOUBLE_PI * k / PadSegments;
					const FVector2d U(FMath::Cos(A), FMath::Sin(A));
					double Support = 0.0;
					for (const FVector2d& P : Piece.P) Support = FMath::Max(Support, FVector2d::DotProduct(P - Centre, U));
					Pad.Radii[k] = Support + Half;
				}
				Pads.Add(MoveTemp(Pad));
				continue;
			}
		}
		for (int32 End = 0; End < 2; ++End)
		{
			FVector2d& P = End == 0 ? Piece.P[0] : Piece.P.Last();
			const FVector2d Inward = (End == 0 ? Piece.P[1] : Piece.P[Piece.P.Num() - 2]) - P;
			const int32 Edge = EdgeOf(P);
			if (Edge < 0) continue;
			// Contracts are for roads crossing the seam, not running along it.
			const FVector2d In = Inward.GetSafeNormal();
			if ((Edge < 2 ? FMath::Abs(In.X) : FMath::Abs(In.Y)) < 0.5) continue;
			int32 Best = INDEX_NONE;
			double BestD = 1.5;
			for (int32 c = 0; c < NeighbourEnds.Num(); ++c)
			{
				if (ContractUsed[c] || EdgeOf(NeighbourEnds[c].P) != Edge) continue;
				const double D = FVector2d::Distance(NeighbourEnds[c].P, P);
				if (D < BestD && FMath::Abs(FVector2d::DotProduct(Inward.GetSafeNormal(), NeighbourEnds[c].T)) > 0.7)
				{
					BestD = D;
					Best = c;
				}
			}
			if (Best != INDEX_NONE)
			{
				ContractUsed[Best] = true;
				P = NeighbourEnds[Best].P;
				Piece.Contract[End] = Best;
			}
		}
		Piece.S.Add(0.0);
		for (int32 k = 1; k < Piece.P.Num(); ++k) Piece.S.Add(Piece.S.Last() + FVector2d::Distance(Piece.P[k], Piece.P[k - 1]));
		Piece.Length = Piece.S.Last();
		if (Piece.Length < 0.5) continue;
		Pieces.Add(MoveTemp(Piece));
	}

	// Town cells (LaneMarkings.gd / RoadSurface.gd rule: buildings in the 250 m square).
	const int32 Cells = FMath::CeilToInt(Side / TownCellM);
	TMap<FIntPoint, int32> Homes;
	for (const FVector2d& U : Tile.BuildingCentres)
	{
		const FVector2d P = U * Side;
		Homes.FindOrAdd(FIntPoint(FMath::FloorToInt(P.X / TownCellM), FMath::FloorToInt(P.Y / TownCellM)))++;
	}
	TMap<FIntPoint, bool> TownCache;
	auto IsTown = [&](const FVector2d& P)
	{
		const FIntPoint Cell(FMath::FloorToInt(P.X / TownCellM), FMath::FloorToInt(P.Y / TownCellM));
		if (const bool* Known = TownCache.Find(Cell)) return *Known;
		int32 Count = 0, Inside = 0;
		for (int32 dx = -2; dx <= 2; ++dx)
		for (int32 dy = -2; dy <= 2; ++dy)
		{
			const FIntPoint K = Cell + FIntPoint(dx, dy);
			if (K.X < 0 || K.Y < 0 || K.X >= Cells || K.Y >= Cells) continue;
			++Inside;
			if (const int32* H = Homes.Find(K)) Count += *H;
		}
		const bool bTown = Count * 25.0 / FMath::Max(Inside, 1) >= TownBuildings;
		TownCache.Add(Cell, bTown);
		return bTown;
	};

	// Sidewalk segments (8 m bins).
	struct FWalk { FVector2d A; FVector2d B; double WidthM; };
	TMap<FIntPoint, TArray<FWalk>> Walks;
	constexpr double WalkBin = 8.0;
	for (const FSimCruisePathRow& Path : Tile.Pedestrian)
	{
		if (Path.Kind != TEXT("sidewalk") && Path.Kind != TEXT("footway")) continue;
		for (int32 j = 1; j < Path.Points.Num(); ++j)
		{
			const FVector2d A = Path.Points[j - 1] * Side, B = Path.Points[j] * Side;
			if (FVector2d::DistSquared(A, B) < 0.01) continue;
			for (int32 x = FMath::FloorToInt(FMath::Min(A.X, B.X) / WalkBin); x <= FMath::FloorToInt(FMath::Max(A.X, B.X) / WalkBin); ++x)
			for (int32 y = FMath::FloorToInt(FMath::Min(A.Y, B.Y) / WalkBin); y <= FMath::FloorToInt(FMath::Max(A.Y, B.Y) / WalkBin); ++y)
			{
				Walks.FindOrAdd(FIntPoint(x, y)).Add({ A, B, Path.WidthM });
			}
		}
	}

	// 2. Chains, split where the line doubles back (a turnaround mapped as one way),
	// then one ribbon per span.
	const TArray<TArray<FPart>> Chains = ChainPieces(Pieces, Side);
	for (const TArray<FPart>& Parts : Chains)
	{
		double Total = 0.0;
		double MaxHalf = 0.0;
		for (const FPart& Part : Parts)
		{
			Total = FMath::Max(Total, Part.Offset + Pieces[Part.Piece].Length);
			MaxHalf = FMath::Max(MaxHalf, Pieces[Part.Piece].WidthM * 0.5);
		}
		if (Total < 1.0) continue;
		const double OuterHalf = MaxHalf + SimCruiseRoadSection::GutterM + SimCruiseRoadSection::CurbTopM + SimCruiseRoadSection::DefaultSidewalkM;
		TArray<FChainVertex> V = ChainVertices(Pieces, Parts, OuterHalf);
		if (V.Num() < 2) continue;
		// A bend sharper than CornerSplitDeg that the fillet cannot round with room for the
		// whole cross-section (legs too short: a parking aisle, a street corner mapped as
		// one way) is a corner: the ribbon ends there and the next begins, each running
		// on past the corner by its half width so the outside corner is paved square.
		TArray<double> Splits = { 0.0 };
		for (int32 i = 1; i + 1 < V.Num(); ++i)
		{
			const FVector2d A = V[i].P - V[i - 1].P, B = V[i + 1].P - V[i].P;
			if (A.SizeSquared() < 1e-6 || B.SizeSquared() < 1e-6) continue;
			const double Turn = FMath::Acos(FMath::Clamp(FVector2d::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0));
			const double Radius = V[i].TLen > 0.0 ? V[i].TLen / FMath::Tan(0.5 * V[i].Theta) : 0.0;
			bool bCorner = Turn > FMath::DegreesToRadians(CornerSplitDeg) && Radius < 0.9 * OuterHalf;
			// A hard joint (two pieces whose ends are up to MergeM apart) is a corner when
			// the legs either side of it turn sharply: it has no fillet to round it.
			if (!bCorner && V[i].bHard && i + 2 < V.Num() && V[i + 1].bHard && FVector2d::Distance(V[i].P, V[i + 1].P) < MergeM + 0.5)
			{
				const FVector2d Out = V[i + 2].P - V[i + 1].P;
				if (Out.SizeSquared() > 1e-6)
				{
					const double JointTurn = FMath::Acos(FMath::Clamp(FVector2d::DotProduct(A.GetSafeNormal(), Out.GetSafeNormal()), -1.0, 1.0));
					bCorner = JointTurn > FMath::DegreesToRadians(CornerSplitDeg);
				}
			}
			if ((bCorner || Turn > FMath::DegreesToRadians(115.0)) && V[i].S - Splits.Last() > 1.0 && Total - V[i].S > 1.0)
			{
				Splits.Add(V[i].S);
				V[i].TLen = 0.0;
			}
		}
		Splits.Add(Total);

		// Contracts at the chain's two ends (the piece end that is the chain end).
		int32 ChainContract[2] = { INDEX_NONE, INDEX_NONE };
		{
			const FPart& First = Parts[0];
			ChainContract[0] = Pieces[First.Piece].Contract[First.bReversed ? 1 : 0];
			const FPart& Last = Parts.Last();
			ChainContract[1] = Pieces[Last.Piece].Contract[Last.bReversed ? 0 : 1];
		}

		for (int32 Span = 0; Span + 1 < Splits.Num(); ++Span)
		{
			const double S0 = Splits[Span], S1 = Splits[Span + 1];
			const double SpanLength = S1 - S0;
			if (SpanLength < 1.0) continue;

			FSimCruiseRibbon Ribbon;
			int32 BestPiece = INDEX_NONE;
			for (const FPart& Part : Parts)
			{
				const FPiece& P = Pieces[Part.Piece];
				if (Part.Offset > S1 || Part.Offset + P.Length < S0) continue;
				if (BestPiece == INDEX_NONE || P.Rank > Pieces[BestPiece].Rank || (P.Rank == Pieces[BestPiece].Rank && P.Length > Pieces[BestPiece].Length)) BestPiece = Part.Piece;
			}
			if (BestPiece == INDEX_NONE) continue;
			Ribbon.Rank = Pieces[BestPiece].Rank;
			Ribbon.Class = Pieces[BestPiece].Class;
			Ribbon.Name = Pieces[BestPiece].Name;
			Ribbon.bMajor = IsMajorClass(Ribbon.Class);
			Ribbon.bService = Ribbon.Class == TEXT("service");
			Ribbon.bUnpaved = Pieces[BestPiece].bUnpaved;

			const int32 Count = FMath::Max(1, FMath::CeilToInt(SpanLength / SimCruiseRoadSection::StationStepM));
			Ribbon.St.SetNum(Count + 1);
			TArray<double> S, Zs, W, Ones, RawX, RawY;
			S.SetNum(Count + 1); Zs.SetNum(Count + 1); W.SetNum(Count + 1); Ones.Init(1.0, Count + 1); RawX.SetNum(Count + 1); RawY.SetNum(Count + 1);
			TArray<FVector2d> RawT;
			RawT.SetNum(Count + 1);
			for (int32 k = 0; k <= Count; ++k)
			{
				S[k] = S0 + SpanLength * k / Count;
				FVector2d P;
				// A span ending at a split joint ends on the first piece's last point.
				ChainPoint(V, k == Count && Span + 2 < Splits.Num() ? S[k] - 1e-6 : S[k], P, RawT[k]);
				RawX[k] = P.X;
				RawY[k] = P.Y;
			}
			// Plan smoothing: mapped vertices are quantised to ~0.6 m (zoom-14 tiles,
			// 4096 units), which leaves wiggles a filleted line still follows. A
			// local-linear fit (sigma 4 m) keeps straight lines exact and real curves
			// within a few centimetres; the ends keep their exact point and direction.
			for (int32 k = 0; k <= Count; ++k)
			{
				FSimCruiseRibbonStation& St = Ribbon.St[k];
				St.S = S[k];
				const double SX = GaussianLinear(S, RawX, Ones, S[k], PlanSigmaM);
				const double SY = GaussianLinear(S, RawY, Ones, S[k], PlanSigmaM);
				const double EndW = Smooth01(FMath::Min(S[k] - S0, S1 - S[k]) / PlanEndM);
				St.C = FVector2d(FMath::Lerp(RawX[k], SX, EndW), FMath::Lerp(RawY[k], SY, EndW));
			}
			for (int32 k = 0; k <= Count; ++k)
			{
				FSimCruiseRibbonStation& St = Ribbon.St[k];
				const FVector2d D = Ribbon.St[FMath::Min(k + 1, Count)].C - Ribbon.St[FMath::Max(k - 1, 0)].C;
				const double EndW = Smooth01(FMath::Min(S[k] - S0, S1 - S[k]) / PlanEndM);
				FVector2d T = FMath::Lerp(RawT[k], D.GetSafeNormal(), EndW).GetSafeNormal();
				St.T = T.IsNearlyZero() ? RawT[k] : T;
			}
			for (int32 k = 0; k <= Count; ++k)
			{
				FSimCruiseRibbonStation& St = Ribbon.St[k];
				St.R = FVector2d(-St.T.Y, St.T.X);
				Zs[k] = Tile.HeightAtUnit(St.C / Side);
				W[k] = 1.0;
				// The part this station lies on: width (tapered at joints) and one-way direction.
				double SumW = 0.0, SumL = 0.0;
				const FPart* Own = &Parts[0];
				for (const FPart& Part : Parts)
				{
					const FPiece& P = Pieces[Part.Piece];
					const double Overlap = FMath::Min(St.S + WidthTaperM, Part.Offset + P.Length) - FMath::Max(St.S - WidthTaperM, Part.Offset);
					if (Overlap > 0.0) { SumW += Overlap * P.WidthM; SumL += Overlap; }
					if (St.S >= Part.Offset - 1e-6 && St.S <= Part.Offset + P.Length + 1e-6) Own = &Part;
				}
				St.HalfW = 0.5 * (SumL > 0.0 ? SumW / SumL : Pieces[Own->Piece].WidthM);
				const int32 OneWay = Pieces[Own->Piece].OneWay;
				St.Travel = static_cast<int8>(OneWay == 0 ? 0 : (Own->bReversed ? -OneWay : OneWay));
				St.bBridge = Pieces[Own->Piece].bBridge;
				St.Layer = static_cast<int8>(FMath::Clamp(Pieces[Own->Piece].Layer, -5, 5));
			}
			// A bridge spans the gap: its profile runs straight from abutment to abutment
			// (BridgeDeck.gd), not down to the river or the valley floor the elevation model
			// has under it. A span that reaches the end of the ribbon keeps the height of its
			// one abutment.
			for (int32 k = 0; k <= Count; ++k)
			{
				if (!Ribbon.St[k].bBridge) continue;
				int32 e = k;
				while (e + 1 <= Count && Ribbon.St[e + 1].bBridge) ++e;
				const int32 A = k - 1, B = e + 1;
				if (A >= 0 || B <= Count)
				{
					const double ZA = A >= 0 ? Zs[A] : Zs[B];
					const double ZB = B <= Count ? Zs[B] : Zs[A];
					const double SA = A >= 0 ? S[A] : S[k], SB = B <= Count ? S[B] : S[e];
					for (int32 j = k; j <= e; ++j)
					{
						const double T = SB > SA ? FMath::Clamp((S[j] - SA) / (SB - SA), 0.0, 1.0) : 0.0;
						// The line between the abutments, never the ground under it: the elevation model's
						// embankments, medians and its own plug under the deck made humps (Doraville, GA).
						Zs[j] = FMath::Lerp(ZA, ZB, T);
					}
				}
				k = e;
			}

			// 3. Profile: robust local-linear fit of the ground under the centreline.
			TArray<double> First; First.SetNum(Count + 1);
			for (int32 k = 0; k <= Count; ++k) First[k] = GaussianLinear(S, Zs, W, S[k], SigmaM);
			TArray<double> Robust; Robust.SetNum(Count + 1);
			for (int32 k = 0; k <= Count; ++k)
			{
				const double R = FMath::IsFinite(First[k]) ? (Zs[k] - First[k]) / TukeyScale : 0.0;
				Robust[k] = FMath::Abs(R) < 1.0 ? FMath::Square(1.0 - R * R) : 0.0;
			}
			for (int32 k = 0; k <= Count; ++k)
			{
				const double Z = GaussianLinear(S, Zs, Robust, S[k], SigmaM);
				Ribbon.St[k].Z = FMath::IsFinite(Z) ? Z : Zs[k];
			}
			// A deck is one smooth line between its approach ends as fitted (the fit above rounds the
			// ground; it must not round a deck): straight, with a small crest on a long span (0.5 % of
			// its length at most, the grade 6 % or less).
			if (FSimCruiseRoadNetwork::BridgeRuleOn())
			{
				for (int32 k = 0; k <= Count; ++k)
				{
					if (!Ribbon.St[k].bBridge) continue;
					int32 e = k;
					while (e + 1 <= Count && Ribbon.St[e + 1].bBridge) ++e;
					const int32 A = k - 1, B = e + 1;
					if (A >= 0 || B <= Count)
					{
						const double ZA = A >= 0 ? Ribbon.St[A].Z : Ribbon.St[B].Z;
						const double ZB = B <= Count ? Ribbon.St[B].Z : Ribbon.St[A].Z;
						const double SA = A >= 0 ? S[A] : S[k], SB = B <= Count ? S[B] : S[e];
						const double Len = FMath::Max(SB - SA, 0.01);
						const double Grade = FMath::Abs(ZB - ZA) / Len;
						const double Crest = Len > 60.0 ? FMath::Clamp(FMath::Min(0.005 * Len, (DeckMaxGrade - Grade) * Len / 4.0), 0.0, 2.0) : 0.0;
						for (int32 j = k; j <= e; ++j)
						{
							const double T = FMath::Clamp((S[j] - SA) / Len, 0.0, 1.0);
							Ribbon.St[j].Z = FMath::Lerp(ZA, ZB, T) + Crest * 4.0 * T * (1.0 - T);
						}
					}
					k = e;
				}
			}

			// 4. Boundary pins: an end on the tile edge takes the neighbour's contract or
			// the shared ground height there.
			Ribbon.bStartOnBoundary = OnBoundary(Ribbon.St[0].C, Side);
			Ribbon.bEndOnBoundary = OnBoundary(Ribbon.St.Last().C, Side);
			const FSimCruiseSeamEnd* Contract[2] = { nullptr, nullptr };
			if (Span == 0 && ChainContract[0] != INDEX_NONE && Ribbon.bStartOnBoundary) Contract[0] = &NeighbourEnds[ChainContract[0]];
			if (Span + 2 == Splits.Num() && ChainContract[1] != INDEX_NONE && Ribbon.bEndOnBoundary) Contract[1] = &NeighbourEnds[ChainContract[1]];
			// Without a neighbour's contract, each half of the seam section clears the shared
			// ground under it on its own: the side that is low lifts, the other stays (a tilt, not
			// a lift of the whole section). It is solved on the section as built: the curve's bank
			// EndE and the crown that bank leaves. Returns the lift each edge needs (left, right; >= 0).
			auto SeamEdgeLift = [&](const FSimCruiseRibbonStation& St, double EndE)
			{
				const double CrownE = SimCruiseRoadSection::Crown * (1.0 - FMath::Min(1.0, FMath::Abs(EndE) / SimCruiseRoadSection::Crown));
				const double EdgeL = St.Z + EndE * St.HalfW - CrownE * St.HalfW;
				const double EdgeR = St.Z - EndE * St.HalfW - CrownE * St.HalfW;
				const double Clear = SimCruiseRoadSection::RuralShoulderDropM + SeamClearM;
				double Lift[2] = { 0.0, 0.0 };
				for (double L = -St.HalfW; L <= St.HalfW + 1e-6; L += 0.5)
				{
					const double Ground = Tile.HeightAtUnit((St.C + St.R * L) / Side);
					const double F = FMath::Abs(L) / FMath::Max(St.HalfW, 0.01);
					const double Road = FMath::Lerp(St.Z, L < 0.0 ? EdgeL : EdgeR, F); // the section is linear centre to edge
					// A point at F of the half rises with its edge's lift (the centre half as much, which
					// the other half's lift covers where both sides need it).
					const double Need = Ground + Clear - Road;
					if (Need > 0.0) Lift[L < 0.0 ? 0 : 1] = FMath::Max(Lift[L < 0.0 ? 0 : 1], Need / FMath::Max(F, 0.5));
				}
				return FVector2d(Lift[0], Lift[1]);
			};
			// The pin fades over a span long enough that it changes the grade by 1 % at most (a
			// smoothstep's steepest slope is 1.5 x rise / span), no longer than half the ribbon so
			// the two ends do not overlap. The clearance always wins: on a short stub the fade is
			// steeper rather than the ground coming through the road at the tile edge.
			const double HalfRibbon = 0.5 * FMath::Max(S1 - S0, 1.0);
			const double MaxSpan = FMath::Max(HalfRibbon, 1.0);
			auto EndLift = [&](int32 End, const TArray<double>& Bank, double& OutDelta, double& OutTilt, double& OutSpan)
			{
				OutDelta = 0.0; OutTilt = 0.0; OutSpan = PinSpanM;
				const bool bOn = End == 0 ? Ribbon.bStartOnBoundary : Ribbon.bEndOnBoundary;
				if (!bOn) return;
				const FSimCruiseRibbonStation& St = End == 0 ? Ribbon.St[0] : Ribbon.St[Count];
				if (Contract[End])
				{
					// A contracted end meets the neighbour exactly, up or down.
					OutDelta = Contract[End]->HC - St.Z;
					OutSpan = FMath::Min(FMath::Max(PinSpanM, 150.0 * FMath::Abs(OutDelta)), FMath::Max(MaxSpan, PinSpanM * 0.5));
					return;
				}
				// No contract: the neighbour may be building at the same time without one either, so
				// the seam section must come out the same from both sides. It is pinned to a height
				// that depends only on the shared ground under the seam (the centre that clears it
				// across the carriageway with a plain crown), with no bank there (the bank eases to 0
				// at this end). A lift relative to this tile's own profile, or a tilt solved on its own
				// bank, made each side different: a 9.9 cm step on CA-1 (v2.0.2 pre-flight).
				double Pin = TNumericLimits<double>::Lowest();
				for (double L = -St.HalfW; L <= St.HalfW + 1e-6; L += 0.5)
				{
					Pin = FMath::Max(Pin, Tile.HeightAtUnit((St.C + St.R * L) / Side) + SimCruiseRoadSection::Crown * FMath::Abs(L));
				}
				Pin += SimCruiseRoadSection::RuralShoulderDropM + SeamClearM;
				OutDelta = Pin - St.Z;
				OutTilt = 0.0;
				OutSpan = FMath::Min(FMath::Max(PinSpanM, 150.0 * FMath::Abs(OutDelta)), MaxSpan);
				(void)Bank;
			};

			// A contracted end takes the neighbour's direction and width, blended in over 10 m.
			for (int32 End = 0; End < 2; ++End)
			{
				if (!Contract[End]) continue;
				++NumContractedEnds;
				const FSimCruiseRibbonStation& EndSt = End == 0 ? Ribbon.St[0] : Ribbon.St[Count];
				const double Orient = FVector2d::DotProduct(EndSt.T, Contract[End]->T) >= 0.0 ? 1.0 : -1.0;
				const FVector2d Target = Contract[End]->T * Orient;
				for (int32 k = 0; k <= Count; ++k)
				{
					const double D = End == 0 ? S[k] - S0 : S1 - S[k];
					const double Wc = Smooth01(1.0 - D / ContractBlendM);
					if (Wc <= 0.0) continue;
					FSimCruiseRibbonStation& St = Ribbon.St[k];
					St.T = FMath::Lerp(St.T, Target, Wc).GetSafeNormal();
					St.R = FVector2d(-St.T.Y, St.T.X);
					St.HalfW = FMath::Lerp(St.HalfW, Contract[End]->HalfW, Wc);
				}
			}

			// Superelevation from the curvature (road-grade.mjs chainProfile).
			// County roads bank out of town only: in a town a tertiary or unclassified street keeps
			// its crown like any low-speed street.
			const bool bMinor = Ribbon.Class == TEXT("tertiary") || Ribbon.Class == TEXT("unclassified");
			const double Design = (bMinor && IsTown(Ribbon.St[Count / 2].C)) ? 0.0 : DesignSpeedKmh(Ribbon.Class);
			TArray<double> E; E.Init(0.0, Count + 1);
			if (Design > 0.0)
			{
				TArray<double> H; H.SetNum(Count + 1);
				for (int32 k = 0; k <= Count; ++k) H[k] = FMath::Atan2(Ribbon.St[k].T.Y, Ribbon.St[k].T.X);
				for (int32 k = 0; k <= Count; ++k)
				{
					const int32 J = FMath::Min(Count, k + 5), K = FMath::Max(0, k - 5);
					const double Ds = S[J] - S[K];
					if (Ds < 4.0) continue;
					double Turn = H[J] - H[K];
					Turn = FMath::Atan2(FMath::Sin(Turn), FMath::Cos(Turn));
					const double Curvature = Turn / Ds;
					const double Radius = 1.0 / FMath::Max(FMath::Abs(Curvature), 1e-6);
					E[k] = FMath::Sign(Curvature) * FMath::Min(0.06, 0.5 * Design * Design / (127.0 * Radius));
				}
				TArray<double> Smoothed; Smoothed.SetNum(Count + 1);
				for (int32 k = 0; k <= Count; ++k) Smoothed[k] = GaussianLinear(S, E, Ones, S[k], SigmaM);
				E = Smoothed;
			}

			// The seam lifts and tilts, solved on the built section (the bank E is known now).
			double Delta0, Tilt0, Span0, Delta1, Tilt1, Span1;
			EndLift(0, E, Delta0, Tilt0, Span0);
			EndLift(1, E, Delta1, Tilt1, Span1);
			const bool bBlendEnds = Ribbon.bStartOnBoundary && Ribbon.bEndOnBoundary && (S1 - S0) < Span0 + Span1;
			for (int32 k = 0; k <= Count; ++k)
			{
				FSimCruiseRibbonStation& St = Ribbon.St[k];
				const double FromStart = St.S - S0;
				const double FromEnd = S1 - St.S;
				double W0 = Ribbon.bStartOnBoundary ? Smooth01(1.0 - FromStart / Span0) : 0.0;
				double W1 = Ribbon.bEndOnBoundary ? Smooth01(1.0 - FromEnd / Span1) : 0.0;
				// A short ribbon pinned at both ends (a stub between two seams) blends one pin into
				// the other over its whole length, instead of two steep ramps that meet in the middle.
				if (bBlendEnds)
				{
					const double T = Smooth01(FromStart / FMath::Max(S1 - S0, 0.01));
					W0 = 1.0 - T;
					W1 = T;
				}
				St.Z += Delta0 * W0 + Delta1 * W1;
				// Banking runs on across tile seams: an end with the neighbour's contract eases to the
				// neighbour's edge superelevation over the pin span; an end without one keeps its
				// own (the neighbour, built later, takes it as its contract). It used to run out
				// to flat over the last 30 m of every tile, so long bends lost their banking at
				// each seam.
				double Ek = FMath::IsFinite(E[k]) ? E[k] : 0.0;
				for (int32 End = 0; End < 2; ++End)
				{
					if (!Contract[End]) continue;
					const double Wb = Smooth01(1.0 - (End == 0 ? FromStart : FromEnd) / PinSpanM);
					if (Wb <= 0.0) continue;
					const FSimCruiseSeamEnd& Ce = *Contract[End];
					const FSimCruiseRibbonStation& EndSt = End == 0 ? Ribbon.St[0] : Ribbon.St[Count];
					const bool bSame = FVector2d::DotProduct(EndSt.T, Ce.T) >= 0.0;
					const double Their = Ce.HalfW > 0.1 ? (Ce.HL - Ce.HR) / (2.0 * Ce.HalfW) : 0.0;
					Ek = FMath::Lerp(Ek, bSame ? Their : -Their, Wb);
				}
				// An uncontracted end has no bank at the seam (both sides agree), eased like its pin.
				if (Ribbon.bStartOnBoundary && !Contract[0]) Ek *= 1.0 - W0;
				if (Ribbon.bEndOnBoundary && !Contract[1]) Ek *= 1.0 - W1;
				Ek += Tilt0 * W0 + Tilt1 * W1;
				St.E = FMath::Clamp(Ek, -0.08, 0.08);
				const double Crown = SimCruiseRoadSection::Crown * (1.0 - FMath::Min(1.0, FMath::Abs(St.E) / SimCruiseRoadSection::Crown));
				St.HC = St.Z;
				St.HL = St.Z + St.E * St.HalfW - Crown * St.HalfW;
				St.HR = St.Z - St.E * St.HalfW - Crown * St.HalfW;
				St.HQL = 0.5 * (St.HL + St.HC);
				St.HQR = 0.5 * (St.HR + St.HC);
			}
			// The crown and the bank clamp change the edges after the solve: check the finished seam
			// section of an uncontracted end against the ground and lift what is still short, with
			// the same fade.
			for (int32 End = 0; End < 2; ++End)
			{
				if (Contract[End] || !(End == 0 ? Ribbon.bStartOnBoundary : Ribbon.bEndOnBoundary)) continue;
				const FSimCruiseRibbonStation& EndSt = End == 0 ? Ribbon.St[0] : Ribbon.St[Count];
				double Short = 0.0;
				const double Clear = SimCruiseRoadSection::RuralShoulderDropM + SeamClearM;
				for (double L = -EndSt.HalfW; L <= EndSt.HalfW + 1e-6; L += 0.5)
				{
					Short = FMath::Max(Short, Tile.HeightAtUnit((EndSt.C + EndSt.R * L) / Side) + Clear - EndSt.Height(L));
				}
				if (Short <= 1e-4) continue;
				const double EndSpan = End == 0 ? Span0 : Span1;
				for (int32 k = 0; k <= Count; ++k)
				{
					FSimCruiseRibbonStation& St = Ribbon.St[k];
					const double From = End == 0 ? St.S - S0 : S1 - St.S;
					const double Wf = bBlendEnds ? (End == 0 ? 1.0 - Smooth01((St.S - S0) / FMath::Max(S1 - S0, 0.01)) : Smooth01((St.S - S0) / FMath::Max(S1 - S0, 0.01)))
						: Smooth01(1.0 - From / EndSpan);
					if (Wf <= 0.0) continue;
					const double Up = Short * Wf;
					St.Z += Up; St.HC += Up; St.HL += Up; St.HR += Up; St.HQL += Up; St.HQR += Up;
				}
			}
			// The seam section is the neighbour's, exactly.
			for (int32 End = 0; End < 2; ++End)
			{
				if (!Contract[End]) continue;
				FSimCruiseRibbonStation& St = End == 0 ? Ribbon.St[0] : Ribbon.St[Count];
				const bool bSame = FVector2d::DotProduct(St.T, Contract[End]->T) >= 0.0;
				St.C = Contract[End]->P;
				St.HalfW = Contract[End]->HalfW;
				St.HC = St.Z = Contract[End]->HC;
				St.HL = bSame ? Contract[End]->HL : Contract[End]->HR;
				St.HR = bSame ? Contract[End]->HR : Contract[End]->HL;
				St.HQL = 0.5 * (St.HL + St.HC);
				St.HQR = 0.5 * (St.HR + St.HC);
				// Its banking too, so the next build that contracts to this tile reads it back.
				St.E = St.HalfW > 0.1 ? (St.HL - St.HR) / (2.0 * St.HalfW) : 0.0;
			}

			// 6. Edge treatment from mapped evidence.
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				FSimCruiseRibbonSide& RS = Ribbon.Sides[SideIndex];
				RS.Edge.Init(ESimCruiseRoadEdge::Town, Count + 1);
				RS.SidewalkM.Init(static_cast<float>(SimCruiseRoadSection::DefaultSidewalkM), Count + 1);
				RS.Cut.Init(0, Count + 1);
			}
			TArray<uint8> Kerb[2];
			TArray<float> Beyond[2];
			for (int32 s = 0; s < 2; ++s) { Kerb[s].Init(0, Count + 1); Beyond[s].Init(0.f, Count + 1); }
			const int32 Reach = FMath::CeilToInt((KerbSidewalkMaxM + 12.0) / WalkBin);
			for (int32 k = 0; k <= Count; ++k)
			{
				const FSimCruiseRibbonStation& St = Ribbon.St[k];
				const bool bTown = IsTown(St.C);
				const ESimCruiseRoadEdge Base = bTown || Ribbon.bService ? ESimCruiseRoadEdge::Town : ESimCruiseRoadEdge::Rural;
				Ribbon.Sides[0].Edge[k] = Base;
				Ribbon.Sides[1].Edge[k] = Base;
				if (Ribbon.bService || Walks.Num() == 0) continue;
				const FIntPoint Home(FMath::FloorToInt(St.C.X / WalkBin), FMath::FloorToInt(St.C.Y / WalkBin));
				for (int32 dx = -Reach; dx <= Reach; ++dx)
				for (int32 dy = -Reach; dy <= Reach; ++dy)
				{
					const TArray<FWalk>* Cell = Walks.Find(Home + FIntPoint(dx, dy));
					if (!Cell) continue;
					for (const FWalk& Walk : *Cell)
					{
						const FVector2d D = (Walk.B - Walk.A).GetSafeNormal();
						const double Denom = FVector2d::DotProduct(D, St.T);
						if (FMath::Abs(Denom) < KerbParallel) continue;
						const double Sw = FVector2d::DotProduct(St.C - Walk.A, St.T) / Denom;
						if (Sw < -0.5 || Sw > FVector2d::Distance(Walk.A, Walk.B) + 0.5) continue;
						const double Lat = FVector2d::DotProduct(Walk.A + D * Sw - St.C, St.R);
						const double Out = FMath::Abs(Lat) - St.HalfW;
						if (Out < KerbSidewalkMinM || Out > KerbSidewalkMaxM) continue;
						const int32 SideIndex = Lat > 0.0 ? 1 : 0;
						// The sidewalk reaches from behind the curb to the far side of the mapped walk.
						const double Width = Out + 0.5 * Walk.WidthM - SimCruiseRoadSection::GutterM - SimCruiseRoadSection::CurbTopM;
						if (Kerb[SideIndex][k] == 0 || Width < Beyond[SideIndex][k])
						{
							Beyond[SideIndex][k] = static_cast<float>(Width);
						}
						Kerb[SideIndex][k] = 1;
					}
				}
			}
			for (int32 s = 0; s < 2; ++s)
			{
				FillGaps(Kerb[s], KerbFillStations);
				DropShort(Kerb[s], KerbMinStations);
				// One sidewalk width per run (median, 0.25 m steps), so the outer edge is a straight line.
				int32 i = 0;
				while (i <= Count)
				{
					if (Kerb[s][i] != 1) { ++i; continue; }
					int32 j = i;
					TArray<float> Widths;
					while (j <= Count && Kerb[s][j] == 1) { if (Beyond[s][j] > 0.f) Widths.Add(Beyond[s][j]); ++j; }
					Widths.Sort();
					double Median = Widths.Num() ? Widths[Widths.Num() / 2] : SimCruiseRoadSection::DefaultSidewalkM;
					// A run reaching a seam takes the standard width, which the neighbour also uses there.
					if ((i == 0 && Ribbon.bStartOnBoundary) || (j > Count && Ribbon.bEndOnBoundary)) Median = SimCruiseRoadSection::DefaultSidewalkM;
					const float Width = static_cast<float>(FMath::Clamp(FMath::RoundToDouble(Median * 4.0) / 4.0, 1.2, 4.0));
					for (int32 k = i; k < j; ++k)
					{
						Ribbon.Sides[s].Edge[k] = ESimCruiseRoadEdge::Kerb;
						Ribbon.Sides[s].SidewalkM[k] = Width;
					}
					i = j;
				}
			}
			// Corner ends run on past the corner by the half width (square outside corner).
			for (int32 End = 0; End < 2; ++End)
			{
				const bool bCornerEnd = End == 0 ? Span > 0 : Span + 2 < Splits.Num();
				if (!bCornerEnd) continue;
				FSimCruiseRibbonStation Ext = End == 0 ? Ribbon.St[0] : Ribbon.St.Last();
				const double Run = Ext.HalfW;
				Ext.C += Ext.T * (End == 0 ? -Run : Run);
				Ext.S += End == 0 ? -Run : Run;
				const int32 From = End == 0 ? 0 : Ribbon.St.Num() - 1;
				for (int32 s = 0; s < 2; ++s)
				{
					const ESimCruiseRoadEdge EdgeCopy = Ribbon.Sides[s].Edge[From];
					const float WalkCopy = Ribbon.Sides[s].SidewalkM[From];
					const int32 At = End == 0 ? 0 : Ribbon.Sides[s].Edge.Num();
					Ribbon.Sides[s].Edge.Insert(EdgeCopy, At);
					Ribbon.Sides[s].SidewalkM.Insert(WalkCopy, At);
					Ribbon.Sides[s].Cut.Insert(0, At);
				}
				Ribbon.St.Insert(Ext, End == 0 ? 0 : Ribbon.St.Num());
			}
			Ribbons.Add(MoveTemp(Ribbon));
		}
	}

	// 5. Junction joins: major roads first; a lower ribbon takes the height of the
	// carriageway it overlaps and fades back to its own profile over JoinFadeM.
	TArray<int32> Order;
	for (int32 r = 0; r < Ribbons.Num(); ++r) Order.Add(r);
	Order.Sort([this](int32 A, int32 B)
	{
		const FSimCruiseRibbon& RA = Ribbons[A];
		const FSimCruiseRibbon& RB = Ribbons[B];
		if (RA.Rank != RB.Rank) return RA.Rank > RB.Rank;
		return RA.St.Num() > RB.St.Num();
	});
	TArray<FSimCruiseRibbon> Sorted;
	Sorted.Reserve(Ribbons.Num());
	for (int32 r : Order) Sorted.Add(MoveTemp(Ribbons[r]));
	Ribbons = MoveTemp(Sorted);

	// 4b. Clearance: a mapped deck clears every road it crosses by DeckClearM under its slab (Woodward
	// Avenue over I-75 in Detroit and Northcrest Road over I-285 in Doraville lay at the freeway's
	// level). Per crossing (a road that ends into the deck is a junction at its abutment, not a
	// crossing): the road under it goes down into a cut (a motorway or trunk, or a road on a lower OSM
	// layer) over ramps of RoadCutGrade at most along it; else the deck rises over embankment ramps of
	// DeckMaxGrade at most along its own ribbon. A ramp never reaches a ribbon's end or a seam pin: if
	// neither fits, the span is logged and left to the junction join as before. The tile's ground
	// conditioning makes the cut's backslopes and the embankment's fill slopes from the road edges.
	TArray<TArray<uint8>> DeckFinal;
	DeckFinal.SetNum(Ribbons.Num());
	for (int32 r = 0; r < Ribbons.Num(); ++r) DeckFinal[r].Init(0, Ribbons[r].St.Num());
	if (BridgeRuleOn())
	{
		TArray<FBox2d> Boxes;
		for (const FSimCruiseRibbon& Rb : Ribbons)
		{
			FBox2d Box(ForceInit);
			double Wide = 0.0;
			for (const FSimCruiseRibbonStation& St : Rb.St) { Box += St.C; Wide = FMath::Max(Wide, St.HalfW); }
			Boxes.Add(Box.ExpandBy(Wide + 1.0));
		}
		struct FChange { int32 Ribbon; double S0; double S1; double D; double R; };
		TArray<FChange> Changes;
		// The ribbon's own grade around S (m per m), so a ramp's added slope keeps the TOTAL under the limit.
		auto GradeAt = [&](int32 Ri, double S)
		{
			const FSimCruiseRibbon& Rb = Ribbons[Ri];
			int32 A = 0, B = Rb.St.Num() - 1;
			for (int32 i = 0; i < Rb.St.Num(); ++i) { if (Rb.St[i].S <= S - 10.0) A = i; if (Rb.St[i].S >= S + 10.0) { B = i; break; } }
			const double Run = Rb.St[B].S - Rb.St[A].S;
			return Run > 1.0 ? FMath::Abs(Rb.St[B].Z - Rb.St[A].Z) / Run : 0.0;
		};
		auto Fits = [&](int32 Ri, double S0, double S1, double R)
		{
			const FSimCruiseRibbon& Rb = Ribbons[Ri];
			const double Keep0 = Rb.bStartOnBoundary ? JoinProtectM + 5.0 : 5.0, Keep1 = Rb.bEndOnBoundary ? JoinProtectM + 5.0 : 5.0;
			return S0 - R >= Rb.St[0].S + Keep0 && S1 + R <= Rb.St.Last().S - Keep1;
		};
		for (int32 r = 0; r < Ribbons.Num(); ++r)
		{
			const FSimCruiseRibbon& Ribbon = Ribbons[r];
			const int32 N = Ribbon.St.Num();
			for (int32 k = 0; k < N; ++k)
			{
				if (!Ribbon.St[k].bBridge) continue;
				int32 e = k;
				while (e + 1 < N && Ribbon.St[e + 1].bBridge) ++e;
				struct FCross { double Need = 0.0; double US0 = 1e18, US1 = -1e18, DS0 = 1e18, DS1 = -1e18; FVector2d At = FVector2d::ZeroVector; };
				TMap<int32, FCross> Cross;
				for (int32 j = k; j <= e; ++j)
				{
					const FSimCruiseRibbonStation& St = Ribbon.St[j];
					for (int32 c = 0; c < 5; ++c)
					{
						const FVector2d P = St.C + St.R * (FSimCruiseRibbonStation::ColumnOffset(c) * St.HalfW);
						for (int32 o = 0; o < Ribbons.Num(); ++o)
						{
							if (o == r || !Boxes[o].IsInside(P)) continue;
							const FSimCruiseRibbon& O = Ribbons[o];
							for (int32 i = 0; i + 1 < O.St.Num(); ++i)
							{
								if (O.St[i].bBridge && O.St[i + 1].bBridge) continue; // a deck by a deck
								const FSimCruiseRoadHit Hit = Project(o, i, P);
								if (FMath::Abs(Hit.Along) > 0.01 || FMath::Abs(Hit.Lateral) > Hit.HalfW) continue;
								if (FMath::Abs(FVector2d::DotProduct(St.T, O.St[i].T)) > 0.94) continue; // the road going on
								const double UnderS = FMath::Lerp(O.St[i].S, O.St[i + 1].S, Hit.T);
								const double FromStart = UnderS - O.St[0].S, FromEnd = O.St.Last().S - UnderS;
								const double Stem = St.HalfW + 1.5;
								if ((!O.bStartOnBoundary && FromStart < Stem) || (!O.bEndOnBoundary && FromEnd < Stem)) continue; // a junction
								const double Clear = St.Column(c) - DeckDepthM - Hit.Height;
								if (Clear >= DeckClearM || Hit.Height > St.Column(c) + 1.0) continue; // clears it, or above the deck
								FCross& X = Cross.FindOrAdd(o);
								if (DeckClearM - Clear > X.Need) { X.Need = DeckClearM - Clear; X.At = P; }
								X.US0 = FMath::Min(X.US0, UnderS); X.US1 = FMath::Max(X.US1, UnderS);
								X.DS0 = FMath::Min(X.DS0, St.S); X.DS1 = FMath::Max(X.DS1, St.S);
							}
						}
					}
				}
				bool bAllFit = true, bHidden = false;
				double SpanHalfW = 0.0;
				for (int32 j = k; j <= e; ++j) SpanHalfW = FMath::Max(SpanHalfW, Ribbon.St[j].HalfW);
				for (const TPair<int32, FCross>& Pair : Cross)
				{
					const FCross& X = Pair.Value;
					const FSimCruiseRibbon& O = Ribbons[Pair.Key];
					const bool bFreeway = O.Class == TEXT("motorway") || O.Class == TEXT("trunk");
					const bool bLowerLayer = O.St[0].Layer < Ribbon.St[k].Layer;
					FString What;
					const double CutRoom = FMath::Clamp(DeckMaxGrade - GradeAt(Pair.Key, 0.5 * (X.US0 + X.US1)), 0.01, RoadCutGrade);
					const double LiftRoom = FMath::Max(DeckMaxGrade - GradeAt(r, 0.5 * (X.DS0 + X.DS1)), 0.01);
					const double RCut = FMath::Max(40.0, 1.5 * X.Need / CutRoom);
					const double RLift = FMath::Max(20.0, 1.5 * X.Need / LiftRoom);
					if ((bFreeway || bLowerLayer) && Fits(Pair.Key, X.US0 - 3.0, X.US1 + 3.0, RCut))
					{
						Changes.Add({ Pair.Key, X.US0 - 3.0, X.US1 + 3.0, -X.Need, RCut });
						What = FString::Printf(TEXT("cut: %s %s down %.2f m (ramps %.0f m)"), *O.Class, O.Name.IsEmpty() ? TEXT("(unnamed)") : *O.Name, X.Need, RCut);
					}
					else if (Fits(r, X.DS0 - 3.0, X.DS1 + 3.0, RLift))
					{
						Changes.Add({ r, X.DS0 - 3.0, X.DS1 + 3.0, X.Need, RLift });
						What = FString::Printf(TEXT("lifted %.2f m over %s (ramps %.0f m)"), X.Need, *O.Class, RLift);
					}
					else
					{
						// Both at once: the road under it down by a share and the deck up by the rest, each
						// over a ramp for its own share (Northcrest Road over I-285: 7.1 m either way did not
						// fit, about half each does). The share that leaves the most room on both.
						double BestF = -1.0, BestRoom = -1e9;
						for (int32 s = 1; s <= 9; ++s)
						{
							const double F = s / 10.0;
							const double Rc = FMath::Max(40.0, 1.5 * X.Need * F / CutRoom), Rl = FMath::Max(20.0, 1.5 * X.Need * (1.0 - F) / LiftRoom);
							if (!Fits(Pair.Key, X.US0 - 3.0, X.US1 + 3.0, Rc) || !Fits(r, X.DS0 - 3.0, X.DS1 + 3.0, Rl)) continue;
							const double Room = -FMath::Max(Rc, Rl);
							if (Room > BestRoom) { BestRoom = Room; BestF = F; }
						}
						if (BestF > 0.0)
						{
							const double Rc = FMath::Max(40.0, 1.5 * X.Need * BestF / CutRoom), Rl = FMath::Max(20.0, 1.5 * X.Need * (1.0 - BestF) / LiftRoom);
							Changes.Add({ Pair.Key, X.US0 - 3.0, X.US1 + 3.0, -X.Need * BestF, Rc });
							Changes.Add({ r, X.DS0 - 3.0, X.DS1 + 3.0, X.Need * (1.0 - BestF), Rl });
							What = FString::Printf(TEXT("cut and lifted: %s %s down %.2f m (ramps %.0f m), the deck up %.2f m (ramps %.0f m)"), *O.Class,
								O.Name.IsEmpty() ? TEXT("(unnamed)") : *O.Name, X.Need * BestF, Rc, X.Need * (1.0 - BestF), Rl);
						}
						else
						{
							// Nothing fits: the deck stays whole and drivable where it is, and the road under it
							// dips out of sight under the slab for the crossing's length (steep, short ramps
							// inside the deck's shadow). A hidden piece of the road below, never a hole in the lane
							// the car drives on. Logged; the span counts as left as it was.
							const double Under = X.Need - DeckClearM + 0.3; // from its height now to 0.3 m under the slab
							const double RHide = FMath::Max(8.0, Under / 0.25);
							if (Under <= 0.0)
							{
								// Already under the slab, only low: the deck stays as it is, whole.
								bHidden = true;
								What = FString::Printf(TEXT("needs %.2f m over %s; neither a cut (%.0f m ramps) nor a lift (%.0f m) fits: %.2f m under the slab, the deck kept whole, left as it was"),
									X.Need, *O.Class, RCut, RLift, DeckClearM - X.Need);
							}
							else if (Fits(Pair.Key, X.US0 - SpanHalfW - 1.0, X.US1 + SpanHalfW + 1.0, RHide))
							{
								Changes.Add({ Pair.Key, X.US0 - SpanHalfW - 1.0, X.US1 + SpanHalfW + 1.0, -Under, RHide });
								bHidden = true;
								What = FString::Printf(TEXT("needs %.2f m over %s; neither a cut (%.0f m ramps) nor a lift (%.0f m) fits: the deck kept whole, %s %s hidden under it (down %.2f m, ramps %.0f m), left as it was"),
									X.Need, *O.Class, RCut, RLift, *O.Class, O.Name.IsEmpty() ? TEXT("(unnamed)") : *O.Name, Under, RHide);
							}
							else
							{
								bAllFit = false;
								What = FString::Printf(TEXT("needs %.2f m over %s; neither a cut (%.0f m ramps) nor a lift (%.0f m) fits, left as it was"), X.Need, *O.Class, RCut, RLift);
							}
						}
					}
					double Lon = 0.0, Lat = 0.0;
					SimCruiseGeo::TileToLonLat(FVector2d(Tile.Id.X, Tile.Id.Y) + X.At / FMath::Max(Side, 1.0), Lon, Lat);
					const FString Span = FString::Printf(TEXT("Bridge span %d_%d/r%d at %.6f, %.6f: %s"), Tile.Id.X, Tile.Id.Y, r, Lat, Lon, *What);
					BridgeChanges.Add(Span);
					UE_LOG(LogTemp, Display, TEXT("%s"), *Span);
				}
				if (bAllFit) for (int32 j = k; j <= e; ++j) DeckFinal[r][j] = 1;
				if (!bAllFit || bHidden)
				{
					DecksLeftAsTheyWere.Add(FVector2d(r, Ribbon.St[k].S));
					for (int32 j = k; j <= e; ++j) { Ribbons[r].St[j].bLeftAsItWas = true; Ribbons[r].St[j].bDeckKept = bAllFit; }
				}
				k = e;
			}
		}
		// Apply: on each ribbon the deepest cut and the highest lift, a smoothstep ramp each side of
		// the plateau (its steepest slope 1.5 x rise / length).
		for (int32 r = 0; r < Ribbons.Num(); ++r)
		{
			FSimCruiseRibbon& Ribbon = Ribbons[r];
			for (FSimCruiseRibbonStation& St : Ribbon.St)
			{
				double Down = 0.0, Up = 0.0;
				for (const FChange& X : Changes)
				{
					if (X.Ribbon != r) continue;
					const double W = St.S < X.S0 ? Smooth01(1.0 - (X.S0 - St.S) / X.R) : St.S > X.S1 ? Smooth01(1.0 - (St.S - X.S1) / X.R) : 1.0;
					if (W <= 0.0) continue;
					Down = FMath::Min(Down, X.D * W);
					Up = FMath::Max(Up, X.D * W);
				}
				const double D = Down + Up;
				if (D == 0.0) continue;
				St.Z += D;
				for (int32 c = 0; c < 5; ++c) St.Column(c) += D;
			}
		}
	}

	for (int32 r = 0; r < Ribbons.Num(); ++r)
	{
		if (r > 0)
		{
			FSimCruiseRibbon& Ribbon = Ribbons[r];
			// Where this ribbon overlaps one already built, its stations are refined to
			// JoinStepM so the two surfaces can be made one: coarse quads would bridge the
			// other road's crown and stand up to 0.1 m proud of it.
			{
				const int32 N0 = Ribbon.St.Num();
				TArray<bool> Near; Near.Init(false, N0);
				for (int32 i = 0; i < N0; ++i)
				{
					const FSimCruiseRibbonStation& St = Ribbon.St[i];
					for (int32 c = 0; c < 5 && !Near[i]; ++c)
					{
						FSimCruiseRoadHit Hit;
						Near[i] = FindCarriageway(St.C + St.R * (FSimCruiseRibbonStation::ColumnOffset(c) * St.HalfW), Hit, r, 1.5);
					}
				}
				TArray<FSimCruiseRibbonStation> Fine;
				TArray<uint8> FineFinal;
				FSimCruiseRibbonSide FineSides[2];
				for (int32 i = 0; i < N0; ++i)
				{
					auto Push = [&](const FSimCruiseRibbonStation& S, int32 From)
					{
						Fine.Add(S);
						FineFinal.Add(DeckFinal[r].IsValidIndex(From) ? DeckFinal[r][From] : 0);
						for (int32 s = 0; s < 2; ++s)
						{
							FineSides[s].Edge.Add(Ribbon.Sides[s].Edge[From]);
							FineSides[s].SidewalkM.Add(Ribbon.Sides[s].SidewalkM[From]);
							FineSides[s].Cut.Add(0);
						}
					};
					Push(Ribbon.St[i], i);
					if (i + 1 >= N0 || !(Near[i] || Near[i + 1])) continue;
					const FSimCruiseRibbonStation& A = Ribbon.St[i];
					const FSimCruiseRibbonStation& B = Ribbon.St[i + 1];
					const int32 Steps = FMath::Clamp(FMath::CeilToInt((B.S - A.S) / JoinStepM), 1, 16);
					for (int32 p = 1; p < Steps; ++p)
					{
						const double T = static_cast<double>(p) / Steps;
						FSimCruiseRibbonStation M = A;
						// Cubic Hermite through both stations and their tangents: the refined
						// stations stay on the curve (chords would kink every offset line).
						const double Len = FVector2d::Distance(A.C, B.C);
						const FVector2d M0 = A.T * Len, M1 = B.T * Len;
						const double T2 = T * T, T3 = T2 * T;
						M.C = A.C * (2 * T3 - 3 * T2 + 1) + M0 * (T3 - 2 * T2 + T) + B.C * (-2 * T3 + 3 * T2) + M1 * (T3 - T2);
						const FVector2d D = A.C * (6 * T2 - 6 * T) + M0 * (3 * T2 - 4 * T + 1) + B.C * (-6 * T2 + 6 * T) + M1 * (3 * T2 - 2 * T);
						M.T = D.IsNearlyZero() ? FMath::Lerp(A.T, B.T, T).GetSafeNormal() : D.GetSafeNormal();
						M.R = FVector2d(-M.T.Y, M.T.X);
						M.S = FMath::Lerp(A.S, B.S, T);
						M.Z = FMath::Lerp(A.Z, B.Z, T);
						M.E = FMath::Lerp(A.E, B.E, T);
						M.HalfW = FMath::Lerp(A.HalfW, B.HalfW, T);
						for (int32 c = 0; c < 5; ++c) M.Column(c) = FMath::Lerp(A.Column(c), B.Column(c), T);
						Push(M, T < 0.5 ? i : i + 1);
					}
				}
				if (Fine.Num() != N0)
				{
					Ribbon.St = MoveTemp(Fine);
					DeckFinal[r] = MoveTemp(FineFinal);
					for (int32 s = 0; s < 2; ++s) Ribbon.Sides[s] = MoveTemp(FineSides[s]);
				}
			}

			const int32 N = Ribbon.St.Num();
			TArray<double> Delta; Delta.Init(0.0, N);
			TArray<bool> Covered; Covered.Init(false, N);
			TArray<double> Snap[5], Cap[5];
			TArray<bool> Snapped[5], Capped[5];
			for (int32 c = 0; c < 5; ++c) { Snap[c].Init(0.0, N); Snapped[c].Init(false, N); Cap[c].Init(0.0, N); Capped[c].Init(false, N); }
			for (int32 i = 0; i < N; ++i)
			{
				const FSimCruiseRibbonStation& St = Ribbon.St[i];
				// A deck is final (its smooth line, or its sag to a road at grade): the join never
				// moves it (the per-carriageway snaps made the Doraville decks humps and dips).
				if (DeckFinal[r][i]) continue;
				double Sum = 0.0; int32 Num = 0;
				for (int32 c = 0; c < 5; ++c)
				{
					const FVector2d P = St.C + St.R * (FSimCruiseRibbonStation::ColumnOffset(c) * St.HalfW);
					FSimCruiseRoadHit Hit;
					// Two levels (a deck and the road under it, 2 m or more apart) are never joined.
					auto OtherLevel = [&](const FSimCruiseRoadHit& H)
					{
						if (!BridgeRuleOn() || !Ribbons.IsValidIndex(H.Ribbon)) return false;
						const FSimCruiseRibbon& O = Ribbons[H.Ribbon];
						const bool bOtherDeck = O.St[H.Station].bBridge || (O.St.IsValidIndex(H.Station + 1) && O.St[H.Station + 1].bBridge);
						return (St.bBridge || bOtherDeck) && FMath::Abs(H.Height - St.Column(c)) > LevelGapM;
					};
					if (FindCarriageway(P, Hit, r) && !OtherLevel(Hit))
					{
						Snap[c][i] = Hit.Height; Snapped[c][i] = true;
						Sum += Hit.Height - St.Column(c); ++Num;
					}
					else if (FindCarriageway(P, Hit, r, JoinEdgeMarginM) && !OtherLevel(Hit))
					{
						// Just outside the other carriageway: never above its edge, so this
						// ribbon's quads cannot stand proud of the road it runs into.
						Cap[c][i] = Hit.Height; Capped[c][i] = true;
					}
				}
				if (Num > 0) { Covered[i] = true; Delta[i] = Sum / Num; }
			}
			bool bAny = false;
			for (bool B : Covered) bAny |= B;
			for (int32 c = 0; c < 5; ++c) for (bool B : Capped[c]) bAny |= B;
			if (bAny)
			{
				// Each column fades from its own snapped points (where it lies on the other road), so
				// no column jumps between neighbouring stations: with one shift per station (the mean
				// of the covered columns) a column went from its own height to the other road's in
				// one 0.5 m step where the roads met at an angle (65 cm on 5th Street at MacArthur
				// Causeway, Miami Beach; SimCruise.World.RoadSteps). A column never on the other
				// road takes the station mean, as before.
				TArray<double> Own[5];
				TArray<double> Ss;
				Ss.SetNum(N);
				for (int32 i = 0; i < N; ++i) Ss[i] = Ribbon.St[i].S;
				for (int32 c = 0; c < 5; ++c) { Own[c].SetNum(N); for (int32 i = 0; i < N; ++i) Own[c][i] = Ribbon.St[i].Column(c); }
				int32 Lo = 0;
				for (int32 i = 0; i < N; ++i)
				{
					FSimCruiseRibbonStation& St = Ribbon.St[i];
					// The stations within the fade (stations run in S order).
					while (Lo < N && Ss[Lo] < St.S - JoinFadeM) ++Lo;
					if (DeckFinal[r][i]) continue; // a deck is final
					// The nearest snapped stations (the strongest weight) give the offsets: means over
					// every station in reach mixed two junctions' offsets and made more steps
					// (SimCruise.World.RoadSteps: 362 steps over 2 cm against 236).
					double Best = 0.0, Weight = 0.0;
					double ColBest[5] = { 0, 0, 0, 0, 0 }, ColWeight[5] = { 0, 0, 0, 0, 0 };
					for (int32 j = Lo; j < N && Ss[j] <= St.S + JoinFadeM; ++j)
					{
						const double Wj = Smooth01(1.0 - FMath::Abs(St.S - Ss[j]) / JoinFadeM);
						if (Wj <= 0.0) continue;
						if (Covered[j] && Wj > Weight) { Weight = Wj; Best = Delta[j]; }
						for (int32 c = 0; c < 5; ++c)
						{
							if (Snapped[c][j] && Wj > ColWeight[c]) { ColWeight[c] = Wj; ColBest[c] = Snap[c][j] - Own[c][j]; }
						}
					}
					// Never move a pinned seam end: the neighbour tile cannot see this junction.
					double Protect = 1.0;
					if (Ribbon.bStartOnBoundary) Protect = FMath::Min(Protect, Smooth01((St.S - Ribbon.St[0].S) / JoinProtectM));
					if (Ribbon.bEndOnBoundary) Protect = FMath::Min(Protect, Smooth01((Ribbon.St.Last().S - St.S) / JoinProtectM));
					const double Shift = Best * Weight * Protect;
					for (int32 c = 0; c < 5; ++c)
					{
						// A column with a snapped point in reach fades from it alone; one without takes
						// the station shift. Blending the two (by the column's weight, or eased) made
						// more and larger steps on the 24 Miami tiles (SimCruise.World.RoadSteps: 236-257
						// steps over 2 cm and up to 50 cm, against 260 and 33 cm), because the station
						// shift belongs to whichever column was covered, often across the road.
						St.Column(c) += ColWeight[c] > 0.0 ? ColBest[c] * ColWeight[c] * Protect : Shift;
					}
					St.Z += St.HC - Own[2][i]; // Z is the centre column's height
					if (Protect >= 0.999)
					{
						// Inside the other carriageway this one is that surface.
						for (int32 c = 0; c < 5; ++c)
						{
							if (Snapped[c][i]) St.Column(c) = Snap[c][i];
							else if (Capped[c][i]) St.Column(c) = FMath::Min(St.Column(c), Cap[c][i]);
						}
						St.Z = St.HC;
					}
				}
			}
		}
		AddToBins(r);
	}

	// Sides that run into another carriageway are cut (junction corners, driveways).
	for (int32 r = 0; r < Ribbons.Num(); ++r)
	{
		FSimCruiseRibbon& Ribbon = Ribbons[r];
		for (int32 s = 0; s < 2; ++s)
		{
			const double Sign = s == 0 ? -1.0 : 1.0;
			for (int32 i = 0; i < Ribbon.St.Num(); ++i)
			{
				const FSimCruiseRibbonStation& St = Ribbon.St[i];
				double Outer = 0.0, H = 0.0;
				OuterEdge(r, i, s, Outer, H);
				const bool bKerb = Ribbon.Sides[s].Edge[i] == ESimCruiseRoadEdge::Kerb;
				const double Margin = bKerb ? SimCruiseRoadSection::GutterM : 0.1;
				bool bCut = false;
				for (const double L : { St.HalfW + 0.2, 0.5 * (St.HalfW + Outer), Outer })
				{
					FSimCruiseRoadHit Hit;
					if (FindCarriageway(St.C + St.R * (Sign * L), Hit, r, Margin)) { bCut = true; break; }
				}
				Ribbon.Sides[s].Cut[i] = bCut ? 1 : 0;
			}
		}
	}

	// One line per bridge span whose parapets or slab were left out over a road at its level.
	for (int32 r = 0; r < Ribbons.Num() && BridgeRuleOn(); ++r)
	{
		const FSimCruiseRibbon& Rb = Ribbons[r];
		const int32 N = Rb.St.Num();
		for (int32 k = 0; k < N; ++k)
		{
			if (!Rb.St[k].bBridge) continue;
			int32 e = k;
			while (e + 1 < N && Rb.St[e + 1].bBridge) ++e;
			double Clipped = 0.0;
			FVector2d At = Rb.St[(k + e) / 2].C;
			for (int32 j = FMath::Max(k - 1, 0); j < FMath::Min(e + 1, N - 1); ++j)
			{
				const bool bOut = !ParapetAt(r, j, -1) || !ParapetAt(r, j, 1) || !DeckUndersideAt(r, j);
				if (!bOut) continue;
				if (Clipped == 0.0) At = Rb.St[j].C;
				Clipped += Rb.St[j + 1].S - Rb.St[j].S;
			}
			if (Clipped > 0.0)
			{
				double Lon = 0.0, Lat = 0.0;
				SimCruiseGeo::TileToLonLat(FVector2d(Tile.Id.X, Tile.Id.Y) + At / FMath::Max(Side, 1.0), Lon, Lat);
				const FString Span = FString::Printf(TEXT("Bridge span %d_%d/r%d at %.6f, %.6f: faces removed over %.0f m (parapet or deck inside another carriageway at its level), %s %s"),
					Tile.Id.X, Tile.Id.Y, r, Lat, Lon, Clipped, *Rb.Class, Rb.Name.IsEmpty() ? TEXT("(unnamed)") : *Rb.Name);
				BridgeChanges.Add(Span);
				UE_LOG(LogTemp, Display, TEXT("%s"), *Span);
			}
			k = e;
		}
	}
}

void FSimCruiseRoadNetwork::AddToBins(int32 RibbonIndex)
{
	const FSimCruiseRibbon& Ribbon = Ribbons[RibbonIndex];
	for (int32 i = 0; i + 1 < Ribbon.St.Num(); ++i)
	{
		const FSimCruiseRibbonStation& A = Ribbon.St[i];
		const FSimCruiseRibbonStation& B = Ribbon.St[i + 1];
		const double Pad = FMath::Max(A.HalfW, B.HalfW) + 8.0;
		const FVector2d Lo(FMath::Min(A.C.X, B.C.X) - Pad, FMath::Min(A.C.Y, B.C.Y) - Pad);
		const FVector2d Hi(FMath::Max(A.C.X, B.C.X) + Pad, FMath::Max(A.C.Y, B.C.Y) + Pad);
		for (int32 x = FMath::FloorToInt(Lo.X / BinM); x <= FMath::FloorToInt(Hi.X / BinM); ++x)
		for (int32 y = FMath::FloorToInt(Lo.Y / BinM); y <= FMath::FloorToInt(Hi.Y / BinM); ++y)
		{
			Bins.FindOrAdd(FIntPoint(x, y)).Add(TPair<int32, int32>(RibbonIndex, i));
		}
	}
}

FSimCruiseRoadHit FSimCruiseRoadNetwork::Project(int32 RibbonIndex, int32 I, const FVector2d& P) const
{
	const FSimCruiseRibbon& Ribbon = Ribbons[RibbonIndex];
	const FSimCruiseRibbonStation& A = Ribbon.St[I];
	const FSimCruiseRibbonStation& B = Ribbon.St[I + 1];
	FSimCruiseRoadHit Hit;
	Hit.Ribbon = RibbonIndex;
	Hit.Station = I;
	const FVector2d D = B.C - A.C;
	const double L2 = D.SizeSquared();
	const double Raw = L2 > 1e-12 ? FVector2d::DotProduct(P - A.C, D) / L2 : 0.0;
	Hit.T = FMath::Clamp(Raw, 0.0, 1.0);
	const FVector2d Q = A.C + D * Hit.T;
	FVector2d R = FMath::Lerp(A.R, B.R, Hit.T).GetSafeNormal();
	if (R.IsNearlyZero()) R = A.R;
	const FVector2d T(R.Y, -R.X);
	Hit.Lateral = FVector2d::DotProduct(P - Q, R);
	Hit.Along = FVector2d::DotProduct(P - Q, T);
	Hit.HalfW = FMath::Lerp(A.HalfW, B.HalfW, Hit.T);
	const double U = FMath::Clamp(Hit.Lateral / FMath::Max(Hit.HalfW, 0.01), -1.0, 1.0);
	const double HA = A.Height(U * A.HalfW);
	const double HB = B.Height(U * B.HalfW);
	Hit.Height = FMath::Lerp(HA, HB, Hit.T);
	return Hit;
}

void FSimCruiseRoadNetwork::GatherSegments(const FVector2d& P, double Reach, TArray<TPair<int32, int32>>& Out) const
{
	Out.Reset();
	const int32 X0 = FMath::FloorToInt((P.X - Reach) / BinM), X1 = FMath::FloorToInt((P.X + Reach) / BinM);
	const int32 Y0 = FMath::FloorToInt((P.Y - Reach) / BinM), Y1 = FMath::FloorToInt((P.Y + Reach) / BinM);
	for (int32 x = X0; x <= X1; ++x)
	for (int32 y = Y0; y <= Y1; ++y)
	{
		if (const TArray<TPair<int32, int32>>* Cell = Bins.Find(FIntPoint(x, y)))
		{
			for (const TPair<int32, int32>& Seg : *Cell) Out.AddUnique(Seg);
		}
	}
}

double FSimCruiseRoadPad::RadiusAt(const FVector2d& P) const
{
	const int32 N = Radii.Num();
	if (N == 0) return 0.0;
	double A = FMath::Atan2(P.Y - Centre.Y, P.X - Centre.X);
	if (A < 0.0) A += 2.0 * UE_DOUBLE_PI;
	const double F = A / (2.0 * UE_DOUBLE_PI) * N;
	const int32 K = FMath::FloorToInt(F) % N;
	return FMath::Lerp(Radii[K], Radii[(K + 1) % N], F - FMath::FloorToDouble(F));
}

FVector2d FSimCruiseRoadPad::OutlinePoint(int32 K) const
{
	const double A = 2.0 * UE_DOUBLE_PI * K / Radii.Num();
	return Centre + FVector2d(FMath::Cos(A), FMath::Sin(A)) * Radii[K % Radii.Num()];
}

bool FSimCruiseRoadNetwork::BridgeRuleOn()
{
	static const bool bOn = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_NO_BRIDGE_RULE")) != TEXT("1");
	return bOn;
}

bool FSimCruiseRoadNetwork::DeckFaceClear(int32 RibbonIndex, int32 I, double Frac, double OutM) const
{
	if (!BridgeRuleOn() || !Ribbons.IsValidIndex(RibbonIndex) || !Ribbons[RibbonIndex].St.IsValidIndex(I + 1)) return true;
	const FSimCruiseRibbon& Rb = Ribbons[RibbonIndex];
	const FSimCruiseRibbonStation& A = Rb.St[I];
	const FSimCruiseRibbonStation& B = Rb.St[I + 1];
	// A deck kept whole over a road hidden under it keeps its parapets and slab.
	if (A.bDeckKept || B.bDeckKept) return true;
	for (const double T : { 0.0, 0.5, 1.0 })
	{
		const FVector2d C = FMath::Lerp(A.C, B.C, T);
		const FVector2d R = FMath::Lerp(A.R, B.R, T).GetSafeNormal();
		const double HalfW = FMath::Lerp(A.HalfW, B.HalfW, T);
		const double L = Frac * HalfW;
		const FVector2d P = C + R * (L + FMath::Sign(Frac) * OutM);
		const double Deck = FMath::Lerp(A.Height(L), B.Height(L), T);
		TArray<TPair<int32, int32>> Segs;
		GatherSegments(P, 1.0, Segs);
		for (const TPair<int32, int32>& Sg : Segs)
		{
			if (Sg.Key == RibbonIndex) continue;
			const FSimCruiseRoadHit Hit = Project(Sg.Key, Sg.Value, P);
			if (FMath::Abs(Hit.Along) > 0.01 || FMath::Abs(Hit.Lateral) > Hit.HalfW) continue;
			if (Deck > Hit.Height - 0.7 && Deck < Hit.Height + DeckFaceWindowM) return false;
		}
	}
	return true;
}

bool FSimCruiseRoadNetwork::ParapetAt(int32 RibbonIndex, int32 I, int32 Edge) const
{
	return DeckFaceClear(RibbonIndex, I, Edge, 0.2);
}

bool FSimCruiseRoadNetwork::DeckUndersideAt(int32 RibbonIndex, int32 I) const
{
	return DeckFaceClear(RibbonIndex, I, -1.0, 0.0) && DeckFaceClear(RibbonIndex, I, 0.0, 0.0) && DeckFaceClear(RibbonIndex, I, 1.0, 0.0);
}

bool FSimCruiseRoadNetwork::FindCarriageway(const FVector2d& P, FSimCruiseRoadHit& OutHit, int32 ExcludeRibbon, double Margin, int32 MinRank) const
{
	bool bFound = false;
	for (int32 i = 0; i < Pads.Num(); ++i)
	{
		const FSimCruiseRoadPad& Pad = Pads[i];
		if (Pad.Rank < MinRank || !Pad.Contains(P, Margin)) continue;
		if (!bFound || Pad.Height > OutHit.Height)
		{
			OutHit = FSimCruiseRoadHit();
			OutHit.Ribbon = -2 - i; // a pad, not a ribbon
			OutHit.Height = Pad.Height;
			OutHit.HalfW = Pad.RadiusAt(P);
			OutHit.Lateral = FVector2d::Distance(P, Pad.Centre);
			bFound = true;
		}
	}
	const TArray<TPair<int32, int32>>* Cell = Bins.Find(FIntPoint(FMath::FloorToInt(P.X / BinM), FMath::FloorToInt(P.Y / BinM)));
	if (!Cell) return bFound;
	for (const TPair<int32, int32>& Seg : *Cell)
	{
		if (Seg.Key == ExcludeRibbon) continue;
		const FSimCruiseRibbon& Ribbon = Ribbons[Seg.Key];
		if (Ribbon.Rank < MinRank) continue;
		const FSimCruiseRoadHit Hit = Project(Seg.Key, Seg.Value, P);
		const bool bFirst = Seg.Value == 0;
		const bool bLast = Seg.Value + 2 == Ribbon.St.Num();
		// Clamped projections beyond an interior station are covered by the neighbouring segment;
		// at the chain's own ends the ribbon stops square.
		const double AlongTolerance = ((bFirst && Hit.T <= 0.0) || (bLast && Hit.T >= 1.0)) ? 0.01 : 1.2;
		if (FMath::Abs(Hit.Along) > AlongTolerance) continue;
		if (FMath::Abs(Hit.Lateral) > Hit.HalfW + Margin) continue;
		if (!bFound || Hit.Height > OutHit.Height)
		{
			OutHit = Hit;
			bFound = true;
		}
	}
	return bFound;
}

bool FSimCruiseRoadNetwork::FindNearest(const FVector2d& P, double Reach, FSimCruiseRoadHit& OutHit, int32 ExcludeRibbon) const
{
	TArray<TPair<int32, int32>> Segments;
	GatherSegments(P, Reach, Segments);
	bool bFound = false;
	double Best = Reach;
	for (const TPair<int32, int32>& Seg : Segments)
	{
		if (Seg.Key == ExcludeRibbon) continue;
		const FSimCruiseRoadHit Hit = Project(Seg.Key, Seg.Value, P);
		const double D = FMath::Sqrt(FMath::Square(Hit.Lateral) + FMath::Square(Hit.Along));
		if (D < Best) { Best = D; OutHit = Hit; bFound = true; }
	}
	return bFound;
}

void FSimCruiseRoadNetwork::OuterEdge(int32 RibbonIndex, int32 I, int32 Side, double& OutLateral, double& OutHeight) const
{
	using namespace SimCruiseRoadSection;
	const FSimCruiseRibbon& Ribbon = Ribbons[RibbonIndex];
	const FSimCruiseRibbonStation& St = Ribbon.St[I];
	const double EdgeH = Side == 0 ? St.HL : St.HR;
	switch (Ribbon.Sides[Side].Edge[I])
	{
	case ESimCruiseRoadEdge::Kerb:
	{
		const double Sw = Ribbon.Sides[Side].SidewalkM[I];
		OutLateral = St.HalfW + GutterM + CurbBatterM + CurbTopM + Sw;
		OutHeight = EdgeH - GutterDropM + CurbFaceM + 0.005 + SidewalkSlope * Sw;
		break;
	}
	case ESimCruiseRoadEdge::Rural:
		OutLateral = St.HalfW + RuralShoulderM;
		OutHeight = EdgeH - RuralShoulderDropM;
		break;
	default:
		OutLateral = St.HalfW + TownEdgeM;
		OutHeight = EdgeH - TownEdgeDropM;
		break;
	}
}

double FSimCruiseRoadNetwork::LowestSurface(int32 RibbonIndex, int32 I) const
{
	const FSimCruiseRibbonStation& St = Ribbons[RibbonIndex].St[I];
	double Lowest = FMath::Min3(St.HL, St.HC, St.HR);
	for (int32 s = 0; s < 2; ++s)
	{
		const double EdgeH = s == 0 ? St.HL : St.HR;
		switch (Ribbons[RibbonIndex].Sides[s].Edge[I])
		{
		case ESimCruiseRoadEdge::Kerb: Lowest = FMath::Min(Lowest, EdgeH - SimCruiseRoadSection::GutterDropM); break;
		case ESimCruiseRoadEdge::Rural: Lowest = FMath::Min(Lowest, EdgeH - SimCruiseRoadSection::RuralShoulderDropM); break;
		default: Lowest = FMath::Min(Lowest, EdgeH - SimCruiseRoadSection::TownEdgeDropM); break;
		}
	}
	return Lowest;
}

void FSimCruiseRoadNetwork::CollectSeamEnds(TArray<FSimCruiseSeamEnd>& Out) const
{
	Out.Reset();
	for (const FSimCruiseRibbon& Ribbon : Ribbons)
	{
		for (int32 End = 0; End < 2; ++End)
		{
			if (!(End == 0 ? Ribbon.bStartOnBoundary : Ribbon.bEndOnBoundary)) continue;
			const FSimCruiseRibbonStation& St = End == 0 ? Ribbon.St[0] : Ribbon.St.Last();
			FSimCruiseSeamEnd Seam;
			Seam.P = St.C;
			Seam.T = St.T;
			Seam.HalfW = St.HalfW;
			Seam.HL = St.HL;
			Seam.HC = St.HC;
			Seam.HR = St.HR;
			Out.Add(Seam);
		}
	}
}

int32 FSimCruiseRoadNetwork::NumStations() const
{
	int32 N = 0;
	for (const FSimCruiseRibbon& R : Ribbons) N += R.St.Num();
	return N;
}
