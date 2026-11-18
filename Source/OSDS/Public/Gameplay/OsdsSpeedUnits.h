// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** The speed units the driver reads: the HUD's speed and the GPS distances. */
enum class EOsdsSpeedUnits : uint8
{
	Mph = 0,
	Kmh = 1,
};

/**
 * The driver's units (pause menu, Settings, "Speed units"), kept in the console
 * variable osds.Hud.Units (0 mph, 1 km/h) and in preferences.json as
 * "speed_units" ("mph" or "kmh"). Miles per hour is the default.
 */
namespace OsdsSpeedUnits
{
	inline constexpr double MphPerMps = 1.0 / 0.44704;
	inline constexpr double KmhPerMps = 3.6;

	OSDS_API EOsdsSpeedUnits Get();
	OSDS_API void Set(EOsdsSpeedUnits Units);
	OSDS_API EOsdsSpeedUnits Next(EOsdsSpeedUnits Units);
	/** Apply the remembered choice (preferences.json; the default when preferences are off). */
	OSDS_API void LoadPreference();
	/** Switch to the other units and remember the choice. */
	OSDS_API void Toggle();

	/** A speed in m/s as the units' number (sign kept). */
	OSDS_API double FromMps(double Mps, EOsdsSpeedUnits Units);
	/** The whole number the HUD shows: the size of the speed, rounded; never negative. */
	OSDS_API int32 Shown(double Mps, EOsdsSpeedUnits Units);
	/** "mph" or "km/h". */
	OSDS_API const TCHAR* Label(EOsdsSpeedUnits Units);
	/** Engine rpm as the HUD shows it: to the nearest 50, never negative. */
	OSDS_API int32 ShownRpm(double Rpm);
}
