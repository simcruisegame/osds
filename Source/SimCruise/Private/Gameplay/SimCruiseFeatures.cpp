// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseFeatures.h"

#include "HAL/IConsoleManager.h"

namespace SimCruiseFeaturesDetail
{
	bool ReadSwitch(const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var && Var->GetInt() != 0;
	}
}

bool SimCruiseFeatures::IsTrafficEnabled()
{
	// When the traffic lane's ASimCruiseTraffic::IsEnabled() is on main, call it here instead
	// (it also takes -SimCruiseTraffic).
	return SimCruiseFeaturesDetail::ReadSwitch(TEXT("sc.Feature.Traffic"));
}

bool SimCruiseFeatures::IsAircraftEnabled()
{
	return SimCruiseFeaturesDetail::ReadSwitch(TEXT("sc.Feature.Aircraft"));
}
