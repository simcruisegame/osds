// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsHudTextLog.h"

#include "HAL/PlatformTime.h"

namespace
{
	bool GHudTextLogOn = false;
	TMap<FString, double> GHudTextSeen;
}

void OsdsHudTextLog::Enable(bool bOn)
{
	GHudTextLogOn = bOn;
	if (!bOn) GHudTextSeen.Reset();
}

bool OsdsHudTextLog::IsEnabled() { return GHudTextLogOn; }

void OsdsHudTextLog::Note(const FString& Text)
{
	if (!GHudTextLogOn || Text.IsEmpty()) return;
	// The HUD draws on the game thread only; a bound keeps a long drive's changing numbers small.
	if (GHudTextSeen.Num() > 2048) GHudTextSeen.Reset();
	GHudTextSeen.Add(Text, FPlatformTime::Seconds());
}

TArray<FString> OsdsHudTextLog::Recent(double Seconds)
{
	TArray<FString> Out;
	const double Since = FPlatformTime::Seconds() - Seconds;
	for (const TPair<FString, double>& Pair : GHudTextSeen) if (Pair.Value >= Since) Out.Add(Pair.Key);
	return Out;
}
