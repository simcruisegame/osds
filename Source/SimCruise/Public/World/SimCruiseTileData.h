// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Typed form of one zoom-16 tile document from the atlas helper
 * (helper/atlas.mjs `tile`, or a bundled data/tiles/<x>_<y>.json).
 * Coordinates are tile units (0..1, north-west origin, y south) exactly as the
 * helper writes them; heights are metres.
 */

/** helper `roads` row: [class, oneway, name, points, width m, surface?, "bridge"?]. */
struct FSimCruiseRoadRow
{
	FString Class;
	int32 OneWay = 0;          // 1: drawn direction only, -1: against it (OpenMapTiles)
	FString Name;
	TArray<FVector2d> Points;  // tile units, clipped to the tile
	double WidthM = 8.0;       // paved width (helper/road-widths.mjs)
	FString Surface;           // "paved", "unpaved" or empty
	bool bBridge = false;
};

/** helper `road_surface` station: [x, y, z m, e, tx, ty, w m] (road-grade.mjs roadSurface). */
struct FSimCruiseSurfaceStation
{
	FVector2d P = FVector2d::ZeroVector;
	double Z = 0.0;
	double E = 0.0;
	FVector2d T = FVector2d(1.0, 0.0);
	double WidthM = 0.0;
};

struct FSimCruiseSurfaceChain
{
	FString Class;
	double WidthM = 0.0;
	TArray<FSimCruiseSurfaceStation> Stations;
};

/** helper `bridges` row: [class, name, points, width m, deck height per point, layer, oneway]. */
struct FSimCruiseBridgeDeck
{
	FString Class;
	FString Name;
	TArray<FVector2d> Points;  // tile units, clipped to the tile
	double WidthM = 0.0;
	TArray<double> HeightsM;   // deck profile produced from the full bridge chain
	int32 Layer = 0;
	int32 OneWay = 0;
};

/** helper `pedestrian` row: [kind, width m, points]. */
struct FSimCruisePathRow
{
	FString Kind;
	double WidthM = 2.0;
	TArray<FVector2d> Points;
};

/** helper `land` row: [layer, class, rings] (landcover, park, water). */
struct FSimCruiseLandPolygon
{
	FString Layer;
	FString Class;
	TArray<TArray<FVector2d>> Rings;
};

/** helper `buildings` row: [rings, parts, height m, min height m, cx, cy, use, subtype] (overture-buildings.mjs). */
struct FSimCruiseBuildingRow
{
	TArray<FVector2d> Ring;    // outer ring, tile units
	double HeightM = 0.0;      // roof height above the ground
	double MinHeightM = 0.0;   // > 0 for a part that floats (a canopy, an overhang)
	FString Use;
	/** The helper row's centre (tile units): the key the building-facts reply matches on. */
	FVector2d Centre = FVector2d(-1.0, -1.0);
	/** From the helper's `buildings` reply (SimCruiseTileData::ApplyBuildingFacts): the height is a
	 *  measured one (Overture: lidar or a mapped height, checked against the OSM way), the storeys above
	 *  ground (USACE National Structure Inventory, 0 unknown), and the mapped finishes (OSM via Overture:
	 *  roof shape "flat" or "gabled", roof height, wall and roof material, "#rrggbb" colours). */
	bool bHeightMeasured = false;
	int32 Stories = 0;
	FString RoofShape;
	double RoofHeightM = 0.0;
	bool bRidgeAcross = false;
	FString WallMaterial;
	FString RoofMaterial;
	FString WallColour;
	FString RoofColour;
};

/** What ApplyBuildingFacts took from a `buildings` reply. */
struct FSimCruiseBuildingFactsCount
{
	int32 Heights = 0;
	int32 Uses = 0;
	int32 Finishes = 0;
};

/** helper `pois` row: [[x, y], name, kind] — a named place (OpenStreetMap / Overture), e.g. "lodging", "restaurant". */
struct FSimCruisePoi
{
	FVector2d Unit = FVector2d::ZeroVector; // tile units
	FString Name;
	FString Kind;
};

struct FSimCruiseTileData
{
	FIntPoint Id = FIntPoint::ZeroValue;
	int32 Grid = 0;                    // cells per side; Heights holds (Grid+1)^2 values, row-major from the north-west
	TArray<float> Heights;
	int32 RoadGradeVersion = 0;
	FString HeightSource;              // e.g. "USGS 3DEP 1/3 arc-second"
	bool bTriangleHeights = false;     // measured grids interpolate on their NE-SW triangles (WorldTile.base_height)
	TArray<FSimCruiseRoadRow> Roads;
	TArray<FSimCruiseBridgeDeck> Bridges;
	TArray<FSimCruiseSurfaceChain> RoadSurface;
	TArray<FSimCruisePathRow> Pedestrian;
	TArray<FSimCruiseLandPolygon> Land;
	TArray<FVector2d> BuildingCentres; // first vertex of each footprint (the built-up rule)
	TArray<TArray<FVector2d>> BuildingFootprints;
	TArray<FSimCruiseBuildingRow> Buildings;
	TArray<FSimCruisePoi> Pois;
	FString AerialKey;                 // a-<x>-<y>.jpg of the zoom-14 photograph, may be empty
	int32 Requests = 0;
	FString SourcePath;

	bool IsValid() const { return Grid > 0 && Heights.Num() == (Grid + 1) * (Grid + 1); }

	/** Natural ground height (m) at a point in tile units, clamped to the tile. */
	double HeightAtUnit(const FVector2d& Unit) const;

	/** Height of one grid node. */
	double Node(int32 I, int32 J) const { return Heights[J * (Grid + 1) + I]; }
};

namespace SimCruiseTileData
{
	/** Parse a helper tile document. Returns false with a path-specific error for malformed data. */
	SIMCRUISE_API bool Parse(const FString& JsonText, FSimCruiseTileData& Out, FString& OutError);
	SIMCRUISE_API bool ParseObject(const TSharedPtr<class FJsonObject>& Root, FSimCruiseTileData& Out, FString& OutError);

	/** Merge the helper's `buildings <x> <y>` reply into the tile's building rows: measured heights,
	 *  building use and storeys, mapped finishes. Each record names its row by index and centre (as the
	 *  Godot client's BuildingStream.gd did); a record whose centre or base height does not match is
	 *  skipped, so a reply for other rows can never move a building. */
	SIMCRUISE_API FSimCruiseBuildingFactsCount ApplyBuildingFacts(FSimCruiseTileData& Data, const TSharedPtr<class FJsonObject>& Reply);

	/** Read and parse a tile file. */
	SIMCRUISE_API bool LoadFile(const FString& Path, FSimCruiseTileData& Out, FString& OutError);

	/** Repository paths (the Godot client's data stays the canonical bundle while it is the reference). */
	SIMCRUISE_API FString RepositoryRoot();
	SIMCRUISE_API FString BundledTilePath(const FIntPoint& Tile);
	SIMCRUISE_API bool HasBundledTile(const FIntPoint& Tile);
	/** Bundled zoom-14 aerial photograph (data/tiles/aerial/a-<x>-<y>.jpg), or empty. */
	SIMCRUISE_API FString BundledAerialPath(const FString& AerialKey);
}
