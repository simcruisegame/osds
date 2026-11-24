// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Surface knowledge, the road table (data/knowledge/surface/road.json, ADR 0006): the rules that decide
 * how a road surface ages and wears, as numbers the materials take as parameters. The table file holds
 * each number's source; these functions read it (built-in defaults when it is missing).
 */
namespace OsdsSurface
{
	/** How much traffic wears a road of this OSM class: 0 (a drive) to 1 (a motorway). */
	OSDS_API float ClassWear(const FString& Class);

	/** The place's road character for one Tile: stable for a TileSeed, biased by the climate. */
	struct FRoadPlace
	{
		float Age = 0.5f;          // 0 freshly resurfaced ... 1 worn out
		float AlbedoScale = 1.f;   // multiplies the base asphalt tint
		float PoolSeed = 0.f;      // 0..1, picks which members of the texture pool this Tile's stretches draw
	};
	OSDS_API FRoadPlace RoadPlace(uint32 TileSeed, float Dry);

	/** The wheel path's centre (m from the lane centre) and half width, and the oil strip's half width. */
	OSDS_API void WheelPathM(float& OutCentre, float& OutHalfWidth, float& OutOilHalfWidth);

	/** Pool texture paths (relative to the repository root), in pool order. */
	OSDS_API const TArray<FString>& PoolTextures();

	/** True when the table file was found and parsed (the built-in defaults carry no sources). */
	OSDS_API bool TableLoaded();
}
