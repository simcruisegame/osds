// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsSpeedUnits.h"

#include "Gameplay/OsdsPreferences.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSpeedUnits(
		TEXT("osds.Hud.Units"), 0,
		TEXT("Speed units on the HUD and the GPS: 0 mph and miles (default), 1 km/h and kilometres."),
		ECVF_Default);

	const TCHAR* PreferenceKey = TEXT("speed_units");
}

namespace OsdsSpeedUnits
{
	EOsdsSpeedUnits Get()
	{
		return CVarSpeedUnits.GetValueOnGameThread() == 1 ? EOsdsSpeedUnits::Kmh : EOsdsSpeedUnits::Mph;
	}

	void Set(EOsdsSpeedUnits Units)
	{
		// At console priority, so the menu's choice also wins over a value given on the command line.
		CVarSpeedUnits->Set(static_cast<int32>(Units), ECVF_SetByConsole);
	}

	EOsdsSpeedUnits Next(EOsdsSpeedUnits Units)
	{
		return Units == EOsdsSpeedUnits::Mph ? EOsdsSpeedUnits::Kmh : EOsdsSpeedUnits::Mph;
	}

	void LoadPreference()
	{
		// Without preferences (checks, captures) the console variable keeps what the command line set.
		if (!OsdsPreferences::IsEnabled()) return;
		const FString Value = OsdsPreferences::GetString(PreferenceKey, TEXT("mph"));
		Set(Value.Equals(TEXT("kmh"), ESearchCase::IgnoreCase) ? EOsdsSpeedUnits::Kmh : EOsdsSpeedUnits::Mph);
	}

	void Toggle()
	{
		const EOsdsSpeedUnits Units = Next(Get());
		Set(Units);
		OsdsPreferences::SetString(PreferenceKey, Units == EOsdsSpeedUnits::Kmh ? TEXT("kmh") : TEXT("mph"));
	}

	double FromMps(double Mps, EOsdsSpeedUnits Units)
	{
		return Mps * (Units == EOsdsSpeedUnits::Kmh ? KmhPerMps : MphPerMps);
	}

	int32 Shown(double Mps, EOsdsSpeedUnits Units)
	{
		if (!FMath::IsFinite(Mps)) return 0;
		return FMath::Max(0, FMath::RoundToInt(FMath::Abs(FromMps(Mps, Units))));
	}

	const TCHAR* Label(EOsdsSpeedUnits Units)
	{
		return Units == EOsdsSpeedUnits::Kmh ? TEXT("km/h") : TEXT("mph");
	}

	int32 ShownRpm(double Rpm)
	{
		if (!FMath::IsFinite(Rpm)) return 0;
		return FMath::Max(0, FMath::RoundToInt(Rpm / 50.0) * 50);
	}
}
