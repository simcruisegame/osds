// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseHudTextLog.h"

#include "HAL/PlatformTime.h"

namespace
{
	bool GHudTextLogOn = false;
	TMap<FString, double> GHudTextSeen;
}

void SimCruiseHudTextLog::Enable(bool bOn)
{
	GHudTextLogOn = bOn;
	if (!bOn) GHudTextSeen.Reset();
}

bool SimCruiseHudTextLog::IsEnabled() { return GHudTextLogOn; }

void SimCruiseHudTextLog::Note(const FString& Text)
{
	if (!GHudTextLogOn || Text.IsEmpty()) return;
	// The HUD draws on the game thread only; a bound keeps a long drive's changing numbers small.
	if (GHudTextSeen.Num() > 2048) GHudTextSeen.Reset();
	GHudTextSeen.Add(Text, FPlatformTime::Seconds());
}

TArray<FString> SimCruiseHudTextLog::Recent(double Seconds)
{
	TArray<FString> Out;
	const double Since = FPlatformTime::Seconds() - Seconds;
	for (const TPair<FString, double>& Pair : GHudTextSeen) if (Pair.Value >= Since) Out.Add(Pair.Key);
	return Out;
}
