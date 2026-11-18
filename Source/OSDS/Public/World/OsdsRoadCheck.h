// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FOsdsTileBuild;

/**
 * The road-step check: the surface a wheel rolls on must be continuous. Along every
 * ribbon's centre line and the middle of each half (where the wheels run), every 0.1 m,
 * the top of the tile's asphalt (the carriageway triangles the wheels meet) is sampled;
 * a change from one sample to the next that the slope just before it does not explain
 * (the pre-flight's own step measure) larger than the threshold is a step: another
 * carriageway's edge standing proud at a junction, a hole, a seam. A deck more than 1 m
 * above the ribbon's own profile is a bridge across and is skipped. Pure and worker-thread safe: it runs on
 * every tile build (logged) and in the tests.
 */
struct OSDS_API FOsdsRoadStep
{
	int32 Ribbon = INDEX_NONE;
	double S = 0.0;              // station along the ribbon (m)
	double Lateral = 0.0;        // the sampled line's offset from the centre (m)
	FVector2d PositionM = FVector2d::ZeroVector; // tile metres
	double JumpCm = 0.0;
	double HeightBeforeM = 0.0;
	double HeightAfterM = 0.0;
};

namespace OsdsRoadCheck
{
	inline constexpr double DefaultThresholdCm = 2.0;
	inline constexpr double SpacingM = 0.1;

	/** Steps above ThresholdCm, largest first. */
	OSDS_API TArray<FOsdsRoadStep> FindSteps(const FOsdsTileBuild& Build, double ThresholdCm = DefaultThresholdCm);

	/** The top of the collision surfaces at P (tile metres); false when nothing covers P. */
	OSDS_API bool TopSurface(const FOsdsTileBuild& Build, const FVector2d& P, double& OutHeightM);

	/** TopSurface for many points: the asphalt (road-layer) triangles of one build, binned once. */
	struct OSDS_API FTopSurface
	{
		explicit FTopSurface(const FOsdsTileBuild& Build);
		/** The highest road-layer face at tile metres P (false: no road face there). */
		bool Top(const FVector2d& P, double& OutHeightM) const;
	private:
		// Floats (tile metres): a tile keeps this for its life (the start pick), about 36 bytes a face.
		TArray<FVector3f> A, B, C;
		TMap<FIntPoint, TArray<int32>> Cells;
	};

	OSDS_API FString Describe(const FOsdsTileBuild& Build, const TArray<FOsdsRoadStep>& Steps, int32 Max = 8);
}
