// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Audio/OsdsInlineFourSynth.h"

class FJsonObject;

/**
 * A car's voice, from Content/Vehicles/CarPresentation.json "engine_sound" (see
 * docs/engine-sound.md). Layouts: "i4" (the default inline four), "v6_60" and
 * "v8_crossplane" (evenly firing, BankOfEvent in firing order), "electric".
 */
struct OSDS_API FOsdsEngineVoiceSpec
{
	FString Layout = TEXT("i4");
	TArray<int32> BankOfEvent;
	double NearGain = 1.0;
	double FarGain = 0.62;
	FOsdsInlineFourSynth::FTone Tone;

	/** A turbocharger's sound on top of the engine: spool whine, whoosh and the lift-off. */
	struct FTurbo
	{
		/** Engine speed where boost is fully available at full pedal. */
		double SpoolRpm = 2500.0;
		/** Seconds for the shaft to reach speed (the lag). */
		double LagS = 0.6;
		/** The compressor whine at full shaft speed, Hz (heard; an estimate per car). */
		double WhineHz = 4200.0;
		double WhineGain = 1.0;
		double WhooshGain = 1.0;
		/** "recirc" (stock: a soft, dark sigh), "atmospheric" (a bright psssh) or "none" (compressor flutter). */
		FString BlowOff = TEXT("recirc");
		/** How many turbochargers (two: a twin-turbo V6), a little louder and wider. */
		int32 Count = 1;
	};
	TOptional<FTurbo> Turbo;

	/** An electric motor's voice: magnetic orders, gear mesh and the inverter. */
	struct FMotor
	{
		/** Pole pairs: the electrical frequency is pole pairs x shaft rev/s. */
		int32 PolePairs = 3;
		/** Stator slots (the slot-pass order). */
		int32 Slots = 54;
		/** Teeth on the motor's pinion (the gear mesh order). */
		int32 PinionTeeth = 23;
		/** The motor's top speed (the sweep's end). */
		double MaxRpm = 17000.0;
		double Gain = 1.0;
	};
	TOptional<FMotor> Motor;

	/** A recorded loop bank (data/sound/engines/<name>, bank.json and WAV loops), relative to the project. */
	FString Recording;
	/** Where the values come from (engine data, recordings) and what is an estimate. */
	FString Source;

	int32 EventsPerCycle() const { return Layout == TEXT("electric") ? 0 : BankOfEvent.Num() >= 3 ? BankOfEvent.Num() : 4; }
	bool IsElectric() const { return Layout == TEXT("electric"); }
};

namespace OsdsEngineVoice
{
	/** Reads an "engine_sound" object; false with OutError if it does not make sense. */
	OSDS_API bool ParseSpec(const FJsonObject& Json, FOsdsEngineVoiceSpec& Out, FString& OutError);
}

/**
 * A turbocharger's sound. The shaft speed follows the boost the pedal and engine speed ask
 * for, with lag; the compressor whines (a tone rising with shaft speed), the intake whooshes
 * (band noise with flow), and closing the throttle on boost vents the charge: a recirculating
 * valve sighs, an atmospheric one hisses, and with none the compressor surges (flutter).
 * Pure DSP at any sample rate.
 */
class OSDS_API FOsdsTurboSynth
{
public:
	void Configure(const FOsdsEngineVoiceSpec::FTurbo& InSpec) { Spec = InSpec; }
	/** Adds the turbo to Out (mono). */
	void Render(float* Out, int32 Frames, int32 Rate, const FOsdsInlineFourSynth::FInputs& In);
	/** 0..1: the shaft's speed against full boost (for tests). */
	double GetShaft() const { return Shaft; }
	/** Seconds left of the current blow-off (for tests). */
	double GetBlowOffEnvelope() const { return BlowEnv; }

private:
	FOsdsEngineVoiceSpec::FTurbo Spec;
	double Shaft = 0.0, Pedal = 0.0, PrevPedal = 0.0, WhinePhase = 0.0, Whine2Phase = 0.0;
	double BlowEnv = 0.0, FlutterPhase = 0.0;
	double BpZ[4] = { 0, 0, 0, 0 }, BlowZ[4] = { 0, 0, 0, 0 };
	int64 Random = 90121;
};

/**
 * An electric motor's voice (the Model 3's rear drive unit): the electromagnetic orders at
 * multiples of the electrical frequency, the slot-pass order, the pinion's gear mesh, all
 * louder with torque (drive and regeneration) and a little with speed. Orders above 45 % of
 * the sample rate are left out. Pure DSP.
 */
class OSDS_API FOsdsElectricMotorSynth
{
public:
	void Configure(const FOsdsEngineVoiceSpec::FMotor& InSpec) { Spec = InSpec; }
	/** Mono samples into Out (overwrites). In.MotorRpm drives it; In.Combustion is |torque|. */
	void Render(float* Out, int32 Frames, int32 Rate, const FOsdsInlineFourSynth::FInputs& In);

	/** The orders the voice plays: shaft orders and their relative level under torque. */
	TArray<TPair<double, double>> Orders() const;

private:
	FOsdsEngineVoiceSpec::FMotor Spec;
	TArray<double> Phases;
	double Rpm = 0.0, Torque = 0.0, Gain = 0.0;
	bool bStarted = false;
};

/**
 * A recorded engine: loops cut at steady engine speeds, on throttle and off, inside and
 * outside (scripts/engine-sound/cut_loops.py writes them). Each loop is whole engine cycles,
 * so it repeats without a seam.
 */
struct OSDS_API FOsdsLoopBank
{
	struct FLoop
	{
		TArray<float> Samples;
		int32 Rate = 44100;
		double Rpm = 1000.0;
		bool bOnLoad = true;
		bool bCabin = false;
	};
	TArray<FLoop> Loops;
	FString Name;
	double GainDb = 0.0;
	FString Credit;
	FString Licence;

	/** Reads Dir/bank.json and its WAVs (16-bit or float PCM, mono or stereo). */
	bool Load(const FString& Dir, FString& OutError);
	/** Is there a loop for this mic and load? */
	bool Has(bool bCabin, bool bOnLoad) const;
};

/**
 * Plays a loop bank the way racing games do: the two loops recorded nearest the engine's
 * speed play together, each pitched by (rpm / its rpm) (so a loop is never shifted further
 * than to its neighbour), crossfaded at equal power along the log of rpm; the on-throttle
 * and off-throttle sets crossfade by delivered torque; the cabin and exterior sets by the
 * camera. A missing set borrows another (off from on, quieter and darker; cabin from
 * exterior through the cabin filter). Over the limiter the fuel cut stutters; a stall fades.
 */
class OSDS_API FOsdsLoopBankVoice
{
public:
	void SetBank(TSharedPtr<const FOsdsLoopBank> InBank);
	bool HasBank() const { return Bank.IsValid() && Bank->Loops.Num() > 0; }
	/** Should the cabin low-pass be applied on top (the bank has no cabin recording)? */
	bool NeedsCabinFilter() const { return HasBank() && !Bank->Has(true, true); }
	/** Mono samples at Rate into Out (overwrites). */
	void Render(float* Out, int32 Frames, int32 Rate, const FOsdsInlineFourSynth::FInputs& In);

private:
	struct FSet
	{
		TArray<int32> Loops; // indices into the bank, by rpm
		double Gain = 1.0;
		bool bDarken = false;
	};
	FSet& SetFor(bool bCabin, bool bOnLoad) { return Sets[(bCabin ? 2 : 0) + (bOnLoad ? 1 : 0)]; }
	double PlaySet(const FSet& Set, double Rpm, int32 Rate);

	TSharedPtr<const FOsdsLoopBank> Bank;
	FSet Sets[4];
	TArray<double> Positions;
	double Charge = 0.0, Cabin = 0.0, Gain = 0.0, SmoothRpm = 0.0, CutGain = 1.0, LimiterPhase = 0.0;
	double DarkZ = 0.0;
	bool bStarted = false;
};

/**
 * Everything the engine sound component plays for one car: a loop bank when the car has a
 * recording that loads, else the procedural voice for its layout (inline four, V6, V8 with
 * its tone), or the motor for an electric car; plus the turbo on top.
 */
class OSDS_API FOsdsEngineVoice
{
public:
	void Configure(const FOsdsEngineVoiceSpec& Spec);
	/** For tests: play this bank (instead of Spec.Recording). */
	void SetBank(TSharedPtr<const FOsdsLoopBank> InBank) { Loops.SetBank(InBank); }

	enum class EKind : uint8 { Synth, Recording, Electric };
	EKind GetKind() const { return bElectric ? EKind::Electric : Loops.HasBank() ? EKind::Recording : EKind::Synth; }
	const TCHAR* Describe() const;
	/** 44.1 kHz for recordings and the motor (their content reaches above 11 kHz), 22.05 kHz for the synth. */
	int32 GetSampleRate() const { return GetKind() == EKind::Synth ? FOsdsInlineFourSynth::SampleRate : 44100; }
	bool NeedsCabinFilter() const { return GetKind() == EKind::Synth || (GetKind() == EKind::Recording && Loops.NeedsCabinFilter()); }
	bool HasTurbo() const { return bTurbo; }
	const FOsdsInlineFourSynth& GetSynth() const { return Synth; }

	/** Mono samples at GetSampleRate(), within +-0.94. */
	void Render(float* Out, int32 Frames, const FOsdsInlineFourSynth::FInputs& In);

private:
	FOsdsInlineFourSynth Synth;
	FOsdsLoopBankVoice Loops;
	FOsdsElectricMotorSynth Motor;
	FOsdsTurboSynth Turbo;
	bool bTurbo = false;
	bool bElectric = false;
	FString Description;
};
