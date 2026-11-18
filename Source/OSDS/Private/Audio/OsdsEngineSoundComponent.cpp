// Copyright OSDS contributors. All Rights Reserved.

#include "Audio/OsdsEngineSoundComponent.h"

#include "Misc/ScopeLock.h"

UOsdsEngineSoundComponent::UOsdsEngineSoundComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 1;
	bAllowSpatialization = false;
	bAutoActivate = false;
}

void UOsdsEngineSoundComponent::SetEngineState(const FOsdsInlineFourSynth::FInputs& Inputs)
{
	FScopeLock Lock(&InputsLock);
	Latest = Inputs;
}

bool UOsdsEngineSoundComponent::Init(int32& SampleRate)
{
	SampleRate = Voice.GetSampleRate();
	NumChannels = 1;
	return true;
}

int32 UOsdsEngineSoundComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	FOsdsInlineFourSynth::FInputs Inputs;
	{
		FScopeLock Lock(&InputsLock);
		Inputs = Latest;
	}
	Voice.Render(OutAudio, NumSamples, Inputs);
	if (!bCabinSet)
	{
		Cabin.Set(FOsdsRoadNoiseSynth::CabinCutoffHz, Voice.GetSampleRate());
		bCabinSet = true;
	}
	// A recording made in the cabin already is the cabin; anything else passes the cabin's low-pass.
	const bool bFilter = Voice.NeedsCabinFilter();
	for (int32 I = 0; I < NumSamples; ++I)
	{
		const float Filtered = Cabin.Process(OutAudio[I]);
		if (Inputs.bCabin && bFilter) OutAudio[I] = Filtered;
	}
	return NumSamples;
}

UOsdsRoadNoiseComponent::UOsdsRoadNoiseComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 2;
	bAllowSpatialization = false;
	bAutoActivate = false;
}

void UOsdsRoadNoiseComponent::SetRoadState(const FOsdsRoadNoiseSynth::FInputs& Inputs)
{
	FScopeLock Lock(&InputsLock);
	Latest = Inputs;
}

bool UOsdsRoadNoiseComponent::Init(int32& SampleRate)
{
	SampleRate = FOsdsRoadNoiseSynth::SampleRate;
	NumChannels = 2;
	return true;
}

int32 UOsdsRoadNoiseComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	FOsdsRoadNoiseSynth::FInputs Inputs;
	{
		FScopeLock Lock(&InputsLock);
		Inputs = Latest;
	}
	// NumSamples counts every channel's samples.
	Synth.Render(OutAudio, NumSamples / 2, Inputs);
	return NumSamples;
}
