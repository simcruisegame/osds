// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Gameplay/OsdsUiTheme.h"
#include "World/OsdsRoadNetwork.h"

class AOsdsHUD;
class UCanvas;
class UWorld;
class UOsdsRouteSubsystem;
struct FCanvasUVTri;

/**
 * The 2.0.2 HUD (docs/ui/ui-spec.md, mockup 5), drawn on the HUD's canvas:
 * speed, gear and rev bar at the bottom left; with a route, the guide-sign
 * turn panel at the top, magenta chevrons on the road in the driven lane, an
 * arrow board at the next turn and the course on a round map that turns with
 * the car. osds.Hud.Look 0 brings the earlier HUD back.
 */
class FOsdsDriveHud
{
public:
	static bool IsEnabled();

	/**
	 * The round map: only while the GPS has a route (the default), or always
	 * (osds.Hud.MapAlways, remembered as hud_map_always). In the driver's seat it
	 * sits at 75 % in the top-left corner, over the A-pillar and the sky, clear of the dash and the mirror.
	 */
	static bool IsMapAlways();
	static void SetMapAlways(bool bAlways, bool bRemember = true);

	void Draw(AOsdsHUD& Hud, float DeltaTimeS);

	/** Local clock at a place: the environment's UTC time and the live weather's offset (else the longitude's). */
	static FDateTime LocalTime(const UWorld* World, double Lon);

private:
	/** A road station near the car, in world metres. */
	struct FStation
	{
		FOsdsRibbonStation St;
		FVector2D C = FVector2D::ZeroVector;
		double TileZCm = 0.0;
	};

	void DrawSpeed(AOsdsHUD& Hud, float Alpha);
	void DrawTurnSign(const UOsdsRouteSubsystem& Gps);
	void DrawArrival(const FString& Text);
	void DrawStatus(const FString& Text, float Y);
	void DrawMessage(const FString& Text, float Y);
	void DrawRoundMap(AOsdsHUD& Hud, const UOsdsRouteSubsystem* Gps, float Alpha);
	void DrawRoadGuides(AOsdsHUD& Hud, const UOsdsRouteSubsystem& Gps);
	/** The co-driver's card: X is its left edge (bLeft) or its right edge; Edge is its bottom, or its top when bFromTop. */
	void DrawPlaceCard(AOsdsHUD& Hud, float X, float Edge, bool bFromTop, bool bLeft);
	/** The camera is in the driver's seat (the dash and its instruments are on screen). */
	static bool IsCockpitView(const AOsdsHUD& Hud);
	void GatherStations(AOsdsHUD& Hud, const FVector2D& CarM);
	/** World height (cm) of the road surface at a point in world metres, from the nearest ribbon station. */
	bool RoadHeightCm(const FVector2D& PointM, double& OutZCm) const;

	// Canvas helpers in pixels at 1080p (scaled to the canvas).
	float P(float Px) const { return Px * Scale; }
	void Rect(float X, float Y, float W, float H, const FLinearColor& Color);
	void RoundRect(float X, float Y, float W, float H, float R, const FLinearColor& Color);
	void Disc(const FVector2D& C, float R, const FLinearColor& Color, int32 Segments = 40);
	void Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness);
	void Strokes(const TArray<TArray<FVector2D>>& In, const FVector2D& Origin, float Size, const FLinearColor& Color, float Thickness);
	void Quad(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& Color);
	FVector2D Measure(const FString& Text, OsdsTheme::EWeight Weight, float Px) const;
	/**
	 * Text at X, Y (top); Align 0 left, 1 centre, 2 right. Digits get one width when bTabular.
	 * Drawn at its real point size (the canvas rasterises a runtime font at the font info's
	 * size, not at the item's scale) on whole pixels, with a hard 2 px shadow.
	 */
	float TextAt(const FString& Text, OsdsTheme::EWeight Weight, float Px, float X, float Y, const FLinearColor& Color, int32 Align = 0, bool bTabular = false, bool bShadow = true);

	UCanvas* Canvas = nullptr;
	float Scale = 1.f;
	double Clock = 0.0;
	TArray<FStation> Stations;
	double StationsTime = -100.0;
	FVector2D StationsAt = FVector2D::ZeroVector;
	float SmoothLateral = 0.f;
	float RouteAlpha = 0.f;
	/** This frame: the width the map holds in a top corner (the turn sign keeps clear of it on both sides). */
	float TopCornerReserveX = 0.f;
};

/** Overpass as runtime UFonts for the canvas (FCanvasTextItem does not draw Slate composite fonts here). */
class UFont;
namespace OsdsHudFonts
{
	UFont* Get(OsdsTheme::EWeight Weight);
}
