// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The one User-Agent the game sends to every outside service (the map helper sends the same,
 * helper/atlas-cache.mjs `userAgent`): the game's name, version and public page. Generic, with no
 * person's name, address or account in it.
 */
namespace SimCruiseUserAgent
{
	inline const TCHAR* Get() { return TEXT("OSDS/2.0.3 (open-source driving simulator; +https://github.com/simcruisegame/simcruise)"); }
}
