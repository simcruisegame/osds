// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

/**
 * The CARLA art OSDS places (CC-BY 4.0, CARLA Team; fetched into Content/Carla by
 * scripts/fetch-carla.ps1): data/carla/props.json names each model, what it is (grass, bush, tree,
 * crop, prop), the climate bands it belongs in, how far it is drawn, and whether it casts shadows or
 * stops the car. The tile builder picks entries by rule on its worker (Entries, Find); the tile
 * loads the meshes on the game thread (Mesh). Without the art every lookup is empty and the world
 * is built as before.
 */
struct SIMCRUISE_API FSimCruiseCarlaEntry
{
	FString Key;
	/** The asset, /Game/Carla/Static/... */
	FString Path;
	/** grass, bush, tree, crop, prop */
	FString Kind;
	/** Climate bands (north, south, wet, dry, tropic) it belongs in; empty: all. */
	TArray<FString> Bands;
	double CullM = 120.0;
	bool bShadow = false;
	bool bCollide = false;
	double MinScale = 0.85;
	double MaxScale = 1.15;
};

namespace SimCruiseCarlaCatalog
{
	/** The catalogue (read once; empty when the art or the file is missing). Safe on any thread. */
	SIMCRUISE_API const TArray<FSimCruiseCarlaEntry>& Entries();
	/** The entries of a kind that belong in a climate band. */
	SIMCRUISE_API TArray<int32> Find(const FString& Kind, const FString& Band);
	/** The mesh of an entry, loaded once and kept (game thread); null when it does not load. */
	SIMCRUISE_API UStaticMesh* Mesh(int32 Index);
	/** The climate band at a place (the same rough bands the building materials use). */
	SIMCRUISE_API FString BandAt(double LatDeg, double LonDeg);
}
