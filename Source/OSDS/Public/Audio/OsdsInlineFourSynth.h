// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The procedural voice of a four-stroke inline-four (godot-final:game/InlineFourSound.gd,
 * ported sample for sample): two evenly spaced firing events per crank revolution follow
 * the physics' engine speed; the exhaust's spectrum moves from a light-load to a loaded
 * harmonic table with delivered combustion torque, with induction and airflow noise on
 * top. The cabin pickup keeps the fundamental and a 120 Hz body boom and rolls the
 * exhaust off above about 780 Hz; the exterior pickup is brighter (about 2.6 kHz). Over
 * the rev limiter with the pedal down the fuel cut stutters at 14 Hz; closing the
 * throttle above 3000 rpm gives a decaying chance of mild exhaust pops. The envelopes are
 * estimates, not BP-4W microphone measurements (lab/vehicle/engine-sound).
 *
 * Pure DSP: no engine, no device, any thread. It only reads the physics' outputs and
 * never feeds anything back.
 */
class OSDS_API FOsdsInlineFourSynth
{
public:
	static constexpr int32 SampleRate = 22050;
	/** The rev limiter when the inputs give none. */
	static constexpr double DefaultLimiterRpm = 6950.0;
	static constexpr double LimiterHz = 14.0;
	static constexpr double PopRpm = 3000.0;

	struct FInputs
	{
		double Rpm = 0.0;
		/** Delivered combustion torque, 0..1 of the engine's peak. */
		double Combustion = 0.0;
		/** Shaft load, -1..1: + driving the car, - overrun (the car drives the engine). */
		double ShaftLoad = 0.0;
		/** The accelerator pedal, 0..1. */
		double Pedal = 0.0;
		bool bStalled = false;
		/** The driver's seat (cabin pickup) rather than outside. */
		bool bCabin = true;
		/** Engine speed where the powertrain cuts fuel (the car file's fuel_cut_rpm): the limiter's stutter starts here. */
		double LimiterRpm = DefaultLimiterRpm;
		/** The intake's level in the cabin against outside: 0.5 for an engine ahead of the dash. */
		double CabinInletGain = 0.5;
		/** Overrun pops and crackle, 0..1: 0 for a stock exhaust (a stock NB Miata does not pop). */
		double PopAmount = 1.0;
		/** An electric car's motor speed (rpm); unused by combustion voices. */
		double MotorRpm = 0.0;
	};

	/**
	 * Per-car colour of the exhaust (estimates unless a car's profile names a source): the
	 * muffler body's resonance, the pipe's rasp and its level, the cam's idle lope (cycle-to-
	 * cycle variation, 0 for a stock cam) and the pickups' low-pass (one-pole coefficients at
	 * 22.05 kHz; higher is brighter).
	 */
	struct FTone
	{
		double BodyHz = 180.0;
		double BodyQ = 1.5;
		double RaspHz = 1900.0;
		double RaspGain = 1.0;
		double Lope = 0.0;
		/** Multi-bank layouts: how much each cylinder's pulse differs from the others (fixed, 0..0.3): the half orders a real engine has at idle. */
		double CylinderSpread = 0.0;
		double CabinFilter = 0.25;
		double ExteriorFilter = 0.52;
	};

	FOsdsInlineFourSynth();

	/**
	 * Turn the voice into a cross-plane V8's: eight firing events per 720 crank degrees
	 * (four a revolution), and the exhaust heard as each bank's pipe delivers it. A
	 * cross-plane crank fires each bank at uneven intervals (90, 180 and 270 degrees), so
	 * with one pipe nearer than the other (BankGains) the pulse train swells and sags once
	 * an engine cycle: the V8's burble at half and quarter orders. BankOfEvent: the bank
	 * (0 or 1) of each event in firing order.
	 */
	void ConfigureCrossPlaneV8(const TArray<int32>& BankOfEvent, double NearGain, double FarGain);
	/**
	 * Any evenly firing multi-cylinder layout: BankOfEvent.Num() events per 720 crank degrees
	 * (6 for a V6, 8 for a V8), each heard through its bank's pipe. A 60-degree V6 alternates
	 * banks, which puts energy at the 1.5 engine order (rpm / 40 Hz) under its firing order
	 * (rpm / 20 Hz): the V6's growl.
	 */
	void ConfigureEvenFiring(const TArray<int32>& BankOfEvent, double NearGain, double FarGain);
	void SetTone(const FTone& InTone) { Tone = InTone; }
	const FTone& GetTone() const { return Tone; }
	bool IsV8() const { return Events == 8; }
	/** Firing events per engine cycle (720 degrees): 4 for the inline four. */
	int32 GetEvents() const { return Events; }

	/** Mono samples at SampleRate, within +-0.94. Inputs are smoothed over 25 ms. */
	void Render(float* Out, int32 Frames, const FInputs& In);

private:
	static constexpr int32 Size = 1024;
	static constexpr int32 Mask = Size - 1;

	TArray<float> ExhaustLight, ExhaustLoad, Intake, PulseWindow;
	/** Multi-bank layouts: the exhaust over a whole engine cycle (720 degrees), each event with its bank's gain. */
	TArray<float> CycleLight, CycleLoad;
	bool bV8 = false;
	int32 Events = 4;
	FTone Tone;
	double Phase = 0.0, FiringHz = 0.0, Charge = 0.0, Pull = 0.0, Overrun = 0.0, Gain = 0.0;
	int64 RandomState = 38791;
	double NoiseLow = 0.0, NoiseSlow = 0.0, OutputLow = 0.0, Boom = 0.0, PulseGain = 1.0, Throttle = 0.0;
	bool bFired = false;
	double LimiterPhase = 0.0, CutGain = 1.0, OverrunAge = 0.0, PopEnv = 0.0, PopLow = 0.0;
	/** Fixed exhaust resonances (RBJ band-pass, state x1 x2 y1 y2): the muffler body and the pipe's rasp. */
	double BodyZ[4] = { 0.0, 0.0, 0.0, 0.0 }, RaspZ[4] = { 0.0, 0.0, 0.0, 0.0 };
	/** Which cylinder fires (inline four): each a little different, the engine's half orders. */
	int32 FireIndex = 0;
};
