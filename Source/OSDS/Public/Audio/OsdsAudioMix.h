// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The volume menu's three levels (Settings: Master, Engine, World), 0..1 each:
 * osds.Audio.MasterVolume, osds.Audio.EngineVolume (your car's engine and turbo) and
 * osds.Audio.WorldVolume (tyres, road, wind and the traffic's engines). Saved as
 * preferences master_volume / engine_volume / world_volume in hundredths.
 */
namespace OsdsAudioMix
{
	OSDS_API float Master();
	/** Master x engine: the player's engine. */
	OSDS_API float Engine();
	/** Master x world: road noise and traffic. */
	OSDS_API float World();
}
