// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsFeatures.h"

#include "HAL/IConsoleManager.h"

namespace OsdsFeaturesDetail
{
	bool ReadSwitch(const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var && Var->GetInt() != 0;
	}
}

bool OsdsFeatures::IsTrafficEnabled()
{
	// When the traffic lane's AOsdsTraffic::IsEnabled() is on main, call it here instead
	// (it also takes -OsdsTraffic).
	return OsdsFeaturesDetail::ReadSwitch(TEXT("sc.Feature.Traffic"));
}

bool OsdsFeatures::IsAircraftEnabled()
{
	return OsdsFeaturesDetail::ReadSwitch(TEXT("sc.Feature.Aircraft"));
}
