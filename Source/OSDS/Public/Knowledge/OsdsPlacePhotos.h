// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FOsdsPlaceRecord;

/**
 * The wall colour of a building measured from its own photograph.
 *
 * Wikidata names a photograph for many buildings (P18, a Wikimedia Commons file). Commons'
 * imageinfo gives its date, licence, author, categories and a 480 px thumbnail. Archive
 * pictures (before 1990) and postcards are left out: their colours are not today's paint.
 * In the thumbnail's facade band (the middle of the frame) sky, leaves, shadow and glass
 * are dropped and the largest colour cluster is the wall, kept when it covers at least a
 * fifth of what is left. The colour goes on the building's record with its source, so the
 * graph uses it as mapped data (FOsdsBuildingPlan::bColourFromData).
 */
struct OSDS_API FOsdsPhotoInfo
{
	FString File;      // "Tides Hotel Miami Beach.jpg"
	FString ThumbUrl;
	FString PageUrl;
	int32 Year = 0;    // DateTimeOriginal
	FString Licence;   // "CC BY-SA 3.0"
	FString Artist;
	bool bPostcard = false;
};

namespace OsdsPlacePhotos
{
	/** "http://commons.wikimedia.org/wiki/Special:FilePath/Tides%20Hotel.jpg" -> "Tides Hotel.jpg". */
	OSDS_API FString FileFromCommonsUrl(const FString& Url);
	OSDS_API FString ImageInfoUrl(const FString& File);
	OSDS_API bool ParseImageInfo(const FString& Json, FOsdsPhotoInfo& Out, FString& OutError);
	/** A colour photograph of today's building: 1990 or later, not a postcard, a JPEG thumbnail. */
	OSDS_API bool IsUsable(const FOsdsPhotoInfo& Info);

	/** The wall colour (sRGB) in the facade band of an image; Share is the part of the kept pixels it covers. */
	OSDS_API bool WallColour(const TArray<FColor>& Pixels, int32 Width, int32 Height, FColor& OutColour, float& OutShare);

	/**
	 * The wall colour measured only where the local scene parser (FOsdsSceneParser) labels
	 * the photograph building or wall, anywhere in the frame: no guessing which pixels are sky,
	 * leaves, cars or street. Labels is the parser's grid (GridWidth x GridHeight over the photo).
	 * Needs walls on at least 8 % of the frame. Share is the dominant colour's part of the wall.
	 * With bAlbedoFromPavement and sidewalk or road in the photograph, the colour is darkened
	 * toward an albedo: the lit pavement taken as concrete 0.30 or asphalt 0.12 (gain 0.5-1, never
	 * brighter) with a +-10 % white balance on a near-neutral pavement (OutNormalised). Off by
	 * default: on the Miami Beach start tiles pavement brightness varied too much (known white
	 * walls such as the Breakwater came out grey); otherwise it is the camera's sRGB.
	 */
	OSDS_API bool WallColourMasked(const TArray<FColor>& Pixels, int32 Width, int32 Height, const TArray<uint8>& Labels, int32 GridWidth, int32 GridHeight,
		FColor& OutColour, float& OutShare, bool& OutNormalised, bool bAlbedoFromPavement = false);
	OSDS_API bool DecodeImage(const TArray<uint8>& Bytes, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);

	/** A record's photographs are kept as "a.jpg|b.jpg". */
	OSDS_API TArray<FString> Images(const FString& List);
	OSDS_API void AddImage(FString& List, const FString& File);

	/** Puts a measured colour on a record ("#RRGGBB" and its source line). */
	OSDS_API void Apply(FOsdsPlaceRecord& Record, const FColor& Colour, float Share, const FOsdsPhotoInfo& Info);
}
