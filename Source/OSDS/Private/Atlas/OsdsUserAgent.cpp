// Copyright OSDS contributors. All Rights Reserved.

#include "Atlas/OsdsUserAgent.h"

#include "Misc/ConfigCacheIni.h"

const FString& OsdsUserAgent::Version()
{
	static const FString Value = []()
	{
		FString V;
		if (GConfig) GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), V, GGameIni);
		return V.IsEmpty() ? FString(TEXT("dev")) : V;
	}();
	return Value;
}
