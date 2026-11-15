// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * What a tyre stands on, from the world's own data (the tile layer of the face it touches and
 * the road's OSM surface). One definition for the tyre's grip, the road noise and the rumble.
 * Dirt is reserved for a land-cover source; today the ground beside a road reads as Grass.
 */
enum class ESimCruiseSurfaceKind : uint8
{
	Unknown = 0,
	Asphalt,
	Concrete,   // the kerb's gutter, face and top, and the sidewalk
	Gravel,     // an OSM unpaved or gravel road
	Grass,      // the tile's ground: verges, lawns, fields
	Dirt,
};

namespace SimCruiseSurface
{
	inline const TCHAR* Name(ESimCruiseSurfaceKind Kind)
	{
		switch (Kind)
		{
		case ESimCruiseSurfaceKind::Asphalt: return TEXT("asphalt");
		case ESimCruiseSurfaceKind::Concrete: return TEXT("concrete");
		case ESimCruiseSurfaceKind::Gravel: return TEXT("gravel");
		case ESimCruiseSurfaceKind::Grass: return TEXT("grass");
		case ESimCruiseSurfaceKind::Dirt: return TEXT("dirt");
		default: return TEXT("unknown");
		}
	}
}
