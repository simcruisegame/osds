// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/OsdsPlaces.h"
#include "Gameplay/OsdsRoute.h"

/**
 * Famous drives: a place to start and the stops of the route that follows
 * the road (docs/plan/player-first-roadmap.md, the hero roads). Each end was
 * checked with the helper's router on the OpenStreetMap roads.
 */
struct OSDS_API FOsdsFamousRoad
{
	FString Title;
	/** One line: where it goes and what it is like. */
	FString Note;
	FOsdsPlace Start;
	TArray<FOsdsRouteStop> Stops;
	double LengthMi = 0.0;
};

namespace OsdsFamousRoads
{
	OSDS_API const TArray<FOsdsFamousRoad>& All();
}
