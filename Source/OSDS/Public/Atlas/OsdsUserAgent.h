// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"

/**
 * The one User-Agent the game sends to every outside service (the map helper sends the same,
 * helper/atlas-cache.mjs `userAgent`, which reads the OSDS_VERSION the game sets when it starts the helper): the game's name, version and public page. Generic, with no
 * person's name, address or account in it.
 */
namespace OsdsUserAgent
{
	/** The game's version: ProjectVersion in Config/DefaultGame.ini ("dev" when unreadable). */
	inline const FString& Version()
	{
		static const FString Value = []()
		{
			FString V;
			if (GConfig) GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), V, GGameIni);
			return V.IsEmpty() ? FString(TEXT("dev")) : V;
		}();
		return Value;
	}

	inline const TCHAR* Get()
	{
		static const FString Value = FString::Printf(TEXT("OSDS/%s (open-source driving simulator; +https://github.com/simcruisegame/osds)"), *Version());
		return *Value;
	}
}
