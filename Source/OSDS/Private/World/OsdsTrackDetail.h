// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/OsdsTileDetail.h"

/**
 * What a race track has beyond its racing surface (the raceway ribbons of FOsdsRoadNetwork),
 * built by rule on every tile that holds raceway ribbons, from the track's data (OsdsTracks):
 *
 * - edge lines: white at both edges; on an oval the inside edge is the double yellow line
 *   that keeps cars off the apron;
 * - the start/finish line across the track at the curated start point;
 * - on an oval: the outside retaining wall with a SAFER barrier face (white steel tubes on foam,
 *   in front of the concrete) and a catch fence of posts and cables on top, wherever the
 *   station lies on the oval's loop; a pit wall on the track side of the pit road;
 * - on a road course: red and white kerbs on the inside of every corner tighter than
 *   KerbRadiusM, and on the outside at its exit.
 *
 * The walls and kerbs are simple geometry in the tile's own layers: the walls in Concrete (they
 * collide), the fence in Props (it collides), lines and kerbs in Paint. No logos, no lettering.
 */
namespace OsdsTrackDetail
{
	/** Retaining wall top above the track's outer edge (m): about 4 ft of concrete and barrier. */
	inline constexpr double WallTopM = 1.25;
	/** Catch fence top above the wall's foot (m): about 22 ft at the superspeedways. */
	inline constexpr double FenceTopM = 6.7;
	/** A corner tighter than this (radius, m) gets kerbs. */
	inline constexpr double KerbRadiusM = 140.0;

	struct FCounts
	{
		int32 LineQuads = 0;
		int32 WallSegments = 0;
		int32 FencePosts = 0;
		int32 KerbBlocks = 0;
		bool bStartLine = false;
	};

	/** Builds it all into the tile's layers; returns what it drew (for the log and the tests). */
	OSDS_API FCounts Build(const OsdsTileDetail::FContext& X);
}
