// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Whole features that are switched off for a release. The menus, the HUD and the
 * map ask here, in one place, so a switch can change its name or source without
 * touching every screen.
 *
 * The console variables are defined by the lanes that own the features (traffic:
 * sc.Feature.Traffic in World/OsdsTraffic.cpp; aircraft: sc.Feature.Aircraft in
 * Live/OsdsLiveWorldSubsystem.cpp). Here they are only read: a variable that is
 * not there (not in this build) means the feature is off.
 */
namespace OsdsFeatures
{
	/** Other cars on the road (sc.Feature.Traffic, default 0). */
	OSDS_API bool IsTrafficEnabled();
	/** Live aircraft in the sky and their data credit (sc.Feature.Aircraft, default 0). */
	OSDS_API bool IsAircraftEnabled();
}
