// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "World/OsdsGeo.h"

class FSlateWindowElementList;
class UTexture2D;
struct FGeometry;

/**
 * Map tiles for the route planner: the helper's `map z x y` documents (or the
 * bundled data/map files), drawn into 512 px textures in the guide-sign
 * palette. The same tiles and the same zoom choice as the road atlas
 * (SOsdsRoadAtlas); a view is a centre in world metres and px per metre.
 */
class OSDS_API FOsdsMapTiles
{
public:
	struct FLabel
	{
		FString Text;
		FVector2D AtM = FVector2D::ZeroVector;
		int32 Priority = 0;
		bool bBold = false;
	};

	~FOsdsMapTiles();

	/** Fetch what the view needs (one job at a time) and take finished ones in. Call every frame. */
	void Update(const FOsdsWorldFrame& Frame, const FVector2D& CenterM, double PxPerM, const FVector2D& SizePx);
	/** Draw the tiles; ScreenOf maps world metres to the widget's pixels. */
	void Paint(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FOsdsWorldFrame& Frame,
		const FVector2D& CenterM, double PxPerM, const FVector2D& SizePx) const;
	/** Place names of the tiles drawn, in world metres. */
	void Labels(const FOsdsWorldFrame& Frame, const FVector2D& CenterM, double PxPerM, const FVector2D& SizePx, TArray<FLabel>& Out) const;
	int32 Num() const { return Images.Num(); }

	/** Rasterise a map document into 512 x 512 pixels (tests use it). */
	static bool Raster(const FString& Raw, TArray<FColor>& OutPixels, TArray<FLabel>& OutNames);

private:
	struct FImage
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
		TArray<FLabel> Names;
		double LastUsed = 0.0;
	};
	struct FFetch;
	static void Wanted(const FOsdsWorldFrame& Frame, const FVector2D& CenterM, double PxPerM, const FVector2D& SizePx, TArray<FIntVector>& Out);

	TMap<FString, FImage> Images;
	TMap<FString, double> RetryAt;
	TSharedPtr<FFetch, ESPMode::ThreadSafe> Fetch;
};
