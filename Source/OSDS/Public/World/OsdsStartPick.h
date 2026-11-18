// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FOsdsTileBuild;

/** A built tile as the start pick sees it: where it lies (world cm) and its build. */
struct FOsdsStartTile
{
	FVector OriginCm = FVector::ZeroVector;
	const FOsdsTileBuild* Build = nullptr;
};

/** A place to start a car: on a road, in its lane, facing the way it drives. */
struct FOsdsStartPose
{
	FVector LocationCm = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	int32 Tile = INDEX_NONE;
	int32 Ribbon = INDEX_NONE;
	int32 Station = INDEX_NONE;
	int32 Dir = 0;
	/** How far the road goes on ahead the way the drive robot follows it (capped). */
	double AheadM = 0.0;
	/** From the requested point. */
	double DistanceM = 0.0;
	/** Lower is better (DistanceM, plus penalties for a short road ahead or the wrong way). */
	double Score = 0.0;
	/** The road surface under the car's middle and its four corners (world cm): the ground trace checks them. */
	TArray<FVector> CheckCm;
};

/**
 * Where a car starts (the player's, the drive robot's), anywhere in the US, by general rules:
 * on a public road (no service ways), mid-block (15 m or more from a junction, 25 m of room
 * before the next one), 25 m or more inside its tile (a seam's road ends are the least sure
 * ground), on a straight piece (under 25 degrees of turn over 24 m), in the lane the traffic
 * drives, with a car's footprint (4.6 x 2 m) inside the carriageway and no ground above the
 * road under it, facing a road that goes on: 800 m or more ahead the way the drive robot follows
 * it (along its ribbon, then the straightest ribbon that starts or ends where it ends), not into
 * a dead end. Candidates come out best first; the caller takes the first one a ground trace
 * confirms (the road surface is there, at the road's height).
 */
struct FOsdsRibbon;
class FOsdsRoadNetwork;

namespace OsdsStartPick
{
	/** Per station: 1 where a side is cut by a junction (another road running into this one's side).
	 *  A side cut along more than ParallelCutM is not a junction but a carriageway beside this one with
	 *  no gap (the two one-way decks of the Mackinac Bridge): every station there was "near a junction",
	 *  so no start passed and the car was put on the lake beside the bridge. */
	constexpr double ParallelCutM = 60.0;
	OSDS_API TArray<uint8> JunctionCuts(const FOsdsRibbon& Ribbon);
	/** As above, and a cut whose other carriageway runs alongside (its direction within about 30 deg of
	 *  this one's, found just past this side's edge) is not a junction either: a divided road's two
	 *  carriageways touching, broken into short runs by its cross streets (Ward Parkway, Kansas City). */
	OSDS_API TArray<uint8> JunctionCuts(const FOsdsRoadNetwork& Roads, int32 RibbonIndex);
	/** The road ahead the start wants (60 s of driving at about 13 m/s). */
	constexpr double WantedAheadM = 800.0;
	/** A start stays this far from any junction (along its road). */
	constexpr double JunctionClearM = 30.0;
	/** The car's footprint stays this far inside the carriageway's edge. */
	constexpr double EdgeClearM = 0.5;
	/** The car's middle stays this far inside the drive judge's edge (half a car's width and 0.5 m). */
	constexpr double JudgeInsideM = 1.5;

	/**
	 * How far the road goes on from station S of a ribbon in direction Dir (+1 along the
	 * stations, -1 against), the way the drive robot follows it, up to CapM; a dead end stops it,
	 * a loop counts as going on. Tiles: every built tile it may cross.
	 */
	OSDS_API double AheadM(const TArray<FOsdsStartTile>& Tiles, int32 Tile, int32 Ribbon, double S, int32 Dir, double CapM);

	/**
	 * Whether a car's footprint at Pose stands on the carriageway of its ribbon with no ground
	 * above the road under it; Why says what failed.
	 */
	OSDS_API bool OnCarriageway(const TArray<FOsdsStartTile>& Tiles, const FOsdsStartPose& Pose, FString* Why = nullptr, TArray<FVector>* OutCheckCm = nullptr);

	/**
	 * Start candidates within RadiusM of NearCm, best first (at most MaxCandidates): on the road
	 * named RoadName (empty: any public road), facing PreferredYawDeg where the road allows it
	 * (>= 1e9: no preference).
	 */
	/**
	 * The drive judge's edge (AOsdsPreflight: FindCursor and the past-edge measure): among the
	 * centre lines of the public ribbons, the one nearest the car plus 8 m per unit of misalignment
	 * with its heading; past edge = the distance to that line minus its half width at the
	 * segment's first station. Negative inside. Returns 30 when no line is aligned within 30 m.
	 */
	OSDS_API double JudgePastEdgeM(const TArray<FOsdsStartTile>& Tiles, const FVector& CarCm, double YawDeg, int32* OutTile = nullptr, int32* OutRibbon = nullptr);

	/** One sample of the drive robot's route: where (world cm, in its lane), the lane's own height, the tile. */
	struct FRouteSample
	{
		FVector LaneCm = FVector::ZeroVector;
		int32 Tile = INDEX_NONE;
	};

	/**
	 * The route the drive robot takes from a start: along its ribbon in its lane, then on the
	 * straightest ribbon that starts or ends within 3 m of where it ends (AOsdsPreflight::Walk),
	 * one sample every StepM for LengthM or to a dead end.
	 */
	OSDS_API TArray<FRouteSample> RouteSamples(const TArray<FOsdsStartTile>& Tiles, const FOsdsStartPose& Start, double LengthM, double StepM);

	OSDS_API TArray<FOsdsStartPose> Candidates(const TArray<FOsdsStartTile>& Tiles, const FString& RoadName, const FVector& NearCm,
		double RadiusM, double PreferredYawDeg, int32 MaxCandidates = 24, FString* OutWhyNone = nullptr);
}
