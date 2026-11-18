// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Audio/OsdsInlineFourSynth.h"
#include "Audio/OsdsRoadNoiseSynth.h"
#include "OsdsEngineSoundComponent.generated.h"

/**
 * Plays FOsdsInlineFourSynth: the game thread sets the engine's state each frame
 * (SetEngineState), the audio render thread pulls samples (mono, 22.05 kHz). Not
 * spatialised: like the Godot build, the pickup (cabin or outside) is a filter choice.
 */
UCLASS(ClassGroup = OSDS, meta = (BlueprintSpawnableComponent))
class OSDS_API UOsdsEngineSoundComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UOsdsEngineSoundComponent(const FObjectInitializer& ObjectInitializer);

	/** Game thread: the latest engine state (read by the next audio buffer). */
	void SetEngineState(const FOsdsInlineFourSynth::FInputs& Inputs);

	/** Before Start: a cross-plane V8's voice instead of the inline four (see FOsdsInlineFourSynth). */
	void ConfigureCrossPlaneV8(const TArray<int32>& BankOfEvent, double NearGain, double FarGain) { Synth.ConfigureCrossPlaneV8(BankOfEvent, NearGain, FarGain); }
	bool IsV8() const { return Synth.IsV8(); }

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	FOsdsInlineFourSynth Synth;
	/** The cabin's 2.4 kHz low-pass, which the engine passes through as the road does. */
	FOsdsCabinFilter Cabin;
	bool bCabinSet = false;
	FCriticalSection InputsLock;
	FOsdsInlineFourSynth::FInputs Latest;
};

/** Plays FOsdsRoadNoiseSynth (stereo, 22.05 kHz): tyres, road and wind. */
UCLASS(ClassGroup = OSDS, meta = (BlueprintSpawnableComponent))
class OSDS_API UOsdsRoadNoiseComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UOsdsRoadNoiseComponent(const FObjectInitializer& ObjectInitializer);

	/** Game thread: the latest road state (read by the next audio buffer). */
	void SetRoadState(const FOsdsRoadNoiseSynth::FInputs& Inputs);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	FOsdsRoadNoiseSynth Synth;
	FCriticalSection InputsLock;
	FOsdsRoadNoiseSynth::FInputs Latest;
};
