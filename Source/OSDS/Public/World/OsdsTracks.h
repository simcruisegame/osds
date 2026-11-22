// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/OsdsPlaces.h"

/**
 * The race tracks of the pause menu (data/tracks/tracks.json, made by scripts/tracks/curate.py):
 * where each track is and how it is built beyond what the map gives.
 *
 * The racing surface itself is the map's: OpenStreetMap highway=raceway ways, the vector tiles'
 * class "raceway", drawn as ribbons like every road (FOsdsRoadNetwork). This adds, by track:
 * the published banking of an oval (turns, frontstretch, backstretch), found by where a ribbon
 * station lies on the oval's loop, so the two tiles of a seam always bank the same; a typical
 * width (OpenStreetMap rarely maps a raceway's width and the vector tiles drop it); the side of
 * the loop the outside wall stands on; and the start/finish line.
 */
enum class EOsdsTrackKind : uint8
{
	Oval,
	Road,
	/** Public streets used as a circuit on race weekends (Long Beach): a normal start on the street. */
	Street,
};

struct OSDS_API FOsdsTrack
{
	FString Id;
	FString Name;
	FString Place;
	FString Note;
	/** A street circuit's start street (empty for raceways). */
	FString Street;
	EOsdsTrackKind Kind = EOsdsTrackKind::Road;
	double LengthMi = 0.0;
	/** The loop's length on the map (curate.py), beside the published one. */
	double MappedMi = 0.0;
	double StartLon = 0.0, StartLat = 0.0;
	bool bHasBearing = false;
	double BearingDeg = 0.0;
	double CentreLon = 0.0, CentreLat = 0.0;
	/** Paved width of the racing surface (m); pit lanes are at most PitWidthM. */
	double WidthM = 14.0;
	/** A concrete racing surface (Bristol, Dover) instead of asphalt. */
	bool bConcrete = false;
	/** Banked: the oval's loop (lon, lat; counter-clockwise on the map) and its published banking. */
	bool bBanked = false;
	double BankTurnDeg = 0.0, BankFrontDeg = 0.0, BankBackDeg = 0.0;
	TArray<FVector2d> LoopLonLat;
	TArray<FString> Sources;

	bool IsRaceway() const { return Kind != EOsdsTrackKind::Street; }
};

/**
 * One track's loop in a tile's metres (the tile frame: +X east, +Y south), with what the
 * banking and the walls need per loop vertex.
 */
struct OSDS_API FOsdsTrackFrame
{
	const FOsdsTrack* Track = nullptr;
	TArray<FVector2d> Loop;          // closed: Loop.Last() == Loop[0]
	TArray<FVector2d> Out;           // unit outward normal per vertex
	TArray<double> TurnW;            // 0 on a straight, 1 in a turn
	TArray<uint8> Front;             // 1 on the start/finish half
	FVector2d StartM = FVector2d::ZeroVector;
	FVector2d CentreM = FVector2d::ZeroVector;
	/** Whether a tile-metre point lies within Reach of the loop's bounding box. */
	FBox2d Bounds = FBox2d(ForceInit);

	/** The nearest point of the loop: distance, outward normal, turn weight, front flag. */
	bool Nearest(const FVector2d& P, double& OutDist, FVector2d& OutOut, double& OutTurnW, bool& bOutFront) const;
};

namespace OsdsTracks
{
	/** The Street a track's place carries: start on the raceway (not a pit lane), not on a public road. */
	inline const TCHAR* RacewayStreet = TEXT("@raceway");
	/** The vector tiles' class of highway=raceway. */
	inline const TCHAR* RacewayClass = TEXT("raceway");
	inline constexpr double PitWidthM = 12.0;
	/** Within this of a track's centre, a raceway ribbon takes the track's width and banking (m). */
	inline constexpr double TrackReachM = 3500.0;
	/** Banking runs out over this distance from the loop (a road course leaving the oval, m past the half width). */
	inline constexpr double BankFadeM = 25.0;
	/** Steepest bank slope a ribbon may take (Talladega's 33 deg is 0.65). */
	inline constexpr double MaxBankSlope = 0.7;

	OSDS_API bool IsRacewayClass(const FString& Class);
	/** A pit lane, pit road, pit entry or exit by its mapped name. */
	OSDS_API bool IsPitName(const FString& Name);

	OSDS_API FString DataPath();
	OSDS_API TArray<FOsdsTrack> Parse(const FString& Json, FString* OutError = nullptr);
	/** Every track, loaded once from DataPath(). */
	OSDS_API const TArray<FOsdsTrack>& All();

	/** The track whose centre is nearest (within TrackReachM), or null. */
	OSDS_API const FOsdsTrack* Near(double LonDeg, double LatDeg);

	/** The place a track's menu row travels to: its start/finish line, facing the racing direction. */
	OSDS_API FOsdsPlace ToPlace(const FOsdsTrack& Track);

	/** The track's loop in a zoom-16 tile's metres; false when the track has no loop. */
	OSDS_API bool MakeFrame(const FOsdsTrack& Track, const FIntPoint& TileId, double TileSizeM, FOsdsTrackFrame& Out);

	/**
	 * The published banking at a point on the oval: the turn figure in the turns, the frontstretch
	 * or backstretch figure on the straights, eased between them by the loop's curvature, as a
	 * slope (tan of the angle). Weight is 1 on the loop, easing to 0 by BankFadeM past HalfW.
	 * OutsideSign: +1 when the station's right (+R) side is the outside of the oval.
	 */
	OSDS_API bool BankAt(const FOsdsTrackFrame& Frame, const FVector2d& P, const FVector2d& R, double HalfW, double& OutSlope, double& OutWeight, double& OutOutsideSign);

	/** The not-affiliated line of the menu and docs/tracks.md. */
	inline const TCHAR* Disclaimer = TEXT("OSDS is not affiliated with or endorsed by any track or racing series.");
}
