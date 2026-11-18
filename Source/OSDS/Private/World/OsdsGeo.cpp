// Copyright OSDS contributors. All Rights Reserved.

#include "World/OsdsGeo.h"

namespace OsdsGeo
{
	FVector2d LonLatToTile(double LonDeg, double LatDeg)
	{
		const double Lat = FMath::DegreesToRadians(FMath::Clamp(LatDeg, -85.05112878, 85.05112878));
		const double X = (LonDeg + 180.0) / 360.0 * TilesPerAxis;
		// asinh(tan(lat)) is the Mercator ordinate (Drive.gd project()).
		const double Y = (1.0 - FMath::Loge(FMath::Tan(Lat) + 1.0 / FMath::Cos(Lat)) / UE_DOUBLE_PI) * 0.5 * TilesPerAxis;
		return FVector2d(X, Y);
	}

	void TileToLonLat(const FVector2d& Tile, double& OutLonDeg, double& OutLatDeg)
	{
		OutLonDeg = Tile.X / TilesPerAxis * 360.0 - 180.0;
		const double N = UE_DOUBLE_PI * (1.0 - 2.0 * Tile.Y / TilesPerAxis);
		OutLatDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sinh(N)));
	}

	double TileSizeMetres(double LatDeg)
	{
		return EarthCircumferenceM / TilesPerAxis * FMath::Cos(FMath::DegreesToRadians(LatDeg));
	}

	double TileRowLatitudeDeg(int64 TileY)
	{
		double Lon = 0.0, Lat = 0.0;
		TileToLonLat(FVector2d(0.0, static_cast<double>(TileY) + 0.5), Lon, Lat);
		return Lat;
	}

	FIntPoint AerialTileOf(const FIntPoint& Tile16)
	{
		return FIntPoint(static_cast<int32>(FloorToInt64(Tile16.X / 4.0)), static_cast<int32>(FloorToInt64(Tile16.Y / 4.0)));
	}
}

FOsdsWorldFrame FOsdsWorldFrame::ForPlace(double LonDeg, double LatDeg)
{
	FOsdsWorldFrame Frame;
	const FVector2d Tile = OsdsGeo::LonLatToTile(LonDeg, LatDeg);
	Frame.OriginTile = FIntPoint(static_cast<int32>(OsdsGeo::FloorToInt64(Tile.X)), static_cast<int32>(OsdsGeo::FloorToInt64(Tile.Y)));
	Frame.TileSizeM = OsdsGeo::TileSizeMetres(LatDeg);
	return Frame;
}

FVector FOsdsWorldFrame::TileOriginCm(const FIntPoint& Tile) const
{
	return FVector(static_cast<double>(Tile.X - OriginTile.X) * TileSizeCm(), static_cast<double>(Tile.Y - OriginTile.Y) * TileSizeCm(), 0.0);
}

FVector2d FOsdsWorldFrame::TileCoordToWorldCm(const FVector2d& TileCoord) const
{
	return FVector2d((TileCoord.X - OriginTile.X) * TileSizeCm(), (TileCoord.Y - OriginTile.Y) * TileSizeCm());
}

FVector2d FOsdsWorldFrame::WorldCmToTileCoord(const FVector2d& WorldCm) const
{
	return FVector2d(OriginTile.X + WorldCm.X / TileSizeCm(), OriginTile.Y + WorldCm.Y / TileSizeCm());
}

FIntPoint FOsdsWorldFrame::TileAtWorldCm(const FVector& WorldCm) const
{
	const FVector2d Tile = WorldCmToTileCoord(FVector2d(WorldCm.X, WorldCm.Y));
	return FIntPoint(static_cast<int32>(OsdsGeo::FloorToInt64(Tile.X)), static_cast<int32>(OsdsGeo::FloorToInt64(Tile.Y)));
}

FVector FOsdsWorldFrame::LonLatToWorldCm(double LonDeg, double LatDeg, double HeightM) const
{
	const FVector2d Xy = TileCoordToWorldCm(OsdsGeo::LonLatToTile(LonDeg, LatDeg));
	return FVector(Xy.X, Xy.Y, HeightM * 100.0);
}

void FOsdsWorldFrame::WorldCmToLonLat(const FVector& WorldCm, double& OutLonDeg, double& OutLatDeg) const
{
	OsdsGeo::TileToLonLat(WorldCmToTileCoord(FVector2d(WorldCm.X, WorldCm.Y)), OutLonDeg, OutLatDeg);
}

FVector FOsdsWorldFrame::RebaseTo(const FIntPoint& NewOriginTile)
{
	const FVector Offset(static_cast<double>(OriginTile.X - NewOriginTile.X) * TileSizeCm(), static_cast<double>(OriginTile.Y - NewOriginTile.Y) * TileSizeCm(), 0.0);
	OriginTile = NewOriginTile;
	return Offset;
}
