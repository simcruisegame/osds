// Copyright OSDS contributors. All Rights Reserved.

#include "Audio/OsdsAudioMix.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarMaster(TEXT("osds.Audio.MasterVolume"), 1.f, TEXT("Master volume, 0..1 (the Settings menu's Master row)."));
	TAutoConsoleVariable<float> CVarEngine(TEXT("osds.Audio.EngineVolume"), 1.f, TEXT("Your car's engine and turbo, 0..1 (the Engine row)."));
	TAutoConsoleVariable<float> CVarWorld(TEXT("osds.Audio.WorldVolume"), 1.f, TEXT("Tyres, road, wind and traffic, 0..1 (the World row)."));

	float Unit(const TAutoConsoleVariable<float>& Var) { return FMath::Clamp(Var.GetValueOnAnyThread(), 0.f, 1.f); }
}

float OsdsAudioMix::Master() { return Unit(CVarMaster); }
float OsdsAudioMix::Engine() { return Unit(CVarMaster) * Unit(CVarEngine); }
float OsdsAudioMix::World() { return Unit(CVarMaster) * Unit(CVarWorld); }
