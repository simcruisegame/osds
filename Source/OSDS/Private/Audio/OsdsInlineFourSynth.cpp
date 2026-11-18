// Copyright OSDS contributors. All Rights Reserved.

#include "Audio/OsdsInlineFourSynth.h"

namespace
{
	double SmoothStep(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
}

FOsdsInlineFourSynth::FOsdsInlineFourSynth()
{
	// Each smooth, band-limited cycle is one evenly spaced exhaust event. A short pressure
	// pulse keeps the upper orders under load; gentle running and the cabin pickup
	// emphasise the fundamental; turbulent air is separate.
	static const double Light[] = { .82, .14, .085, .067, .045, .037, .028, .020, .014, .012, .007, .005 };
	static const double Load[] = { .64, .31, .19, .13, .10, .072, .073, .054, .055, .046, .035, .031, .025, .019, .012, .007 };
	constexpr int32 NumLight = UE_ARRAY_COUNT(Light), NumLoad = UE_ARRAY_COUNT(Load);
	for (int32 I = 0; I <= Size; ++I)
	{
		const double Theta = UE_DOUBLE_TWO_PI * static_cast<double>(I) / Size;
		double Quiet = 0.0, Loud = 0.0, Air = 0.0;
		for (int32 N = 0; N < NumLoad; ++N)
		{
			Loud += Load[N] * FMath::Sin(Theta * (N + 1) - .12 * N);
			if (N < NumLight) Quiet += Light[N] * FMath::Sin(Theta * (N + 1) - .15 * N);
		}
		for (int32 N = 0; N < 7; ++N) Air += FMath::Sin((Theta - .78) * (N + 1)) * .21 / (1.0 + .36 * N * N);
		ExhaustLight.Add(static_cast<float>(Quiet));
		ExhaustLoad.Add(static_cast<float>(Loud));
		Intake.Add(static_cast<float>(Air));
		PulseWindow.Add(static_cast<float>(.28 + .72 * FMath::Pow(.5 + .5 * FMath::Cos(Theta - .65), 2.0)));
	}
}

void FOsdsInlineFourSynth::ConfigureCrossPlaneV8(const TArray<int32>& BankOfEvent, double NearGain, double FarGain)
{
	if (BankOfEvent.Num() != 8) return;
	ConfigureEvenFiring(BankOfEvent, NearGain, FarGain);
}

void FOsdsInlineFourSynth::ConfigureEvenFiring(const TArray<int32>& BankOfEvent, double NearGain, double FarGain)
{
	const int32 N = BankOfEvent.Num();
	if (N < 3 || N > 16) return;
	Events = N;
	CycleLight.SetNumUninitialized(Size + 1);
	CycleLoad.SetNumUninitialized(Size + 1);
	// Each cylinder a little different (a fixed pattern scaled by the tone's spread).
	static constexpr double Pattern[16] = { 0.1, -0.9, 0.6, -0.3, 0.9, -0.6, 0.3, -0.1, 0.5, -0.7, 0.8, -0.4, 0.2, -0.8, 0.7, -0.2 };
	auto GainOf = [&](int32 Event)
	{
		const int32 E = (Event + N) % N;
		return (BankOfEvent[E] == 0 ? NearGain : FarGain) * (1.0 + Tone.CylinderSpread * Pattern[E]);
	};
	const double EventDeg = 720.0 / N;
	for (int32 I = 0; I <= Size; ++I)
	{
		// Crank angle over the cycle; each event's pulse is one period of the event tables.
		const double Angle = 720.0 * static_cast<double>(I % Size) / Size;
		const int32 Event = FMath::Min(FMath::FloorToInt(Angle / EventDeg), N - 1);
		const double U = (Angle - EventDeg * Event) / EventDeg;
		// The bank's gain eases in over the first fifth of the event (no click at the edge).
		const double G = FMath::Lerp(GainOf(Event - 1), GainOf(Event), FMath::SmoothStep(0.0, 0.2, U));
		const double X = U * Size;
		const int32 K = FMath::Min(static_cast<int32>(X), Size - 1);
		const double T = X - K;
		CycleLight[I] = static_cast<float>(G * FMath::Lerp(ExhaustLight[K], ExhaustLight[K + 1], T));
		CycleLoad[I] = static_cast<float>(G * FMath::Lerp(ExhaustLoad[K], ExhaustLoad[K + 1], T));
	}
	bV8 = true; // "multi-bank": the cycle tables are in use
}

void FOsdsInlineFourSynth::Render(float* Out, int32 Frames, const FInputs& In)
{
	if (Frames < 1)
	{
		return;
	}
	if (In.bStalled && Gain < .00003)
	{
		FMemory::Memzero(Out, sizeof(float) * Frames);
		return;
	}
	const double Rpm = In.Rpm;
	// The phase runs over one firing period (inline four: two a revolution) or, for a V8,
	// over the whole 720-degree cycle of eight events.
	// The pitch follows the engine to just past its own limiter (the overshoot a fuel cut allows).
	const double TargetHz = FMath::Clamp(Rpm, 0.0, In.LimiterRpm + 200.0) / (bV8 ? 120.0 : 30.0);
	const double EventsPerCycle = static_cast<double>(Events);
	const double SpeedHz = bV8 ? 215.0 / 4.0 : 215.0;
	// The engine speed follows the physics continuously: rev limiter, shifts and stalls.
	if (!bFired)
	{
		FiringHz = TargetHz;
		bFired = true;
	}
	const double HzStep = (TargetHz - FiringHz) / Frames;
	// A lumpy cam lopes at idle and smooths out with revs.
	const double Speed0 = FMath::Clamp((Rpm - 900.0) / 1600.0, 0.0, 1.0);
	const double TargetCharge = In.bStalled ? 0.0 : FMath::Clamp(In.Combustion, 0.0, 1.0);
	const double TargetThrottle = FMath::Clamp(In.Pedal, 0.0, 1.0);
	const double TargetPull = FMath::Clamp(In.ShaftLoad, 0.0, 1.0);
	const double TargetOverrun = FMath::Clamp(-In.ShaftLoad, 0.0, 1.0) * SmoothStep(1150.0, 2300.0, Rpm) * (1.0 - TargetCharge);
	const double TargetGain = In.bStalled ? 0.0 : 1.0;
	const double Smoothing = 1.0 - FMath::Exp(-1.0 / (SampleRate * .025));
	const double Filter = In.bCabin ? Tone.CabinFilter : Tone.ExteriorFilter;
	const double BoomLevel = In.bCabin ? .45 : 0.0;
	const double ExhaustPresence = In.bCabin ? 1.0 : 1.12;
	// The powertrain cuts torque over the limiter; with the pedal still down the sound
	// alternates between fuel cut and firing at LimiterHz.
	const bool bLimiting = Rpm > In.LimiterRpm && TargetThrottle > .35 && !In.bStalled;
	const double CutSmoothing = 1.0 - FMath::Exp(-1.0 / (SampleRate * .0025));
	// Overrun pops: likely right after the throttle closes, fading over about 1.2 s.
	if (TargetOverrun > .25 && Rpm > PopRpm) OverrunAge += static_cast<double>(Frames) / SampleRate;
	else OverrunAge = 0.0;
	double PopChance = 0.0;
	if (OverrunAge > 0.0) PopChance = .10 * FMath::Clamp(In.PopAmount, 0.0, 1.0) * TargetOverrun * SmoothStep(PopRpm, 4200.0, Rpm) * FMath::Exp(-OverrunAge / 1.2);
	// Fixed resonances: the exhaust's pulses ring the muffler body (about 180 Hz) and the pipe's rasp
	// (about 1.9 kHz) at their own frequencies while the firing orders sweep through them, as a real car
	// does (the tables alone moved the whole timbre with rpm, like a recording sped up). RBJ band-pass.
	auto BandPass = [](double F, double Q, double* C)
	{
		const double W = 2.0 * UE_DOUBLE_PI * F / SampleRate, A = FMath::Sin(W) / (2.0 * Q), N = 1.0 + A;
		C[0] = A / N; C[1] = -A / N; C[2] = -2.0 * FMath::Cos(W) / N; C[3] = (1.0 - A) / N;
	};
	double BodyC[4], RaspC[4];
	BandPass(Tone.BodyHz, Tone.BodyQ, BodyC);
	BandPass(Tone.RaspHz, 1.2, RaspC);
	auto Ring = [](double X, const double* C, double* Z)
	{
		const double Y = C[0] * X + C[1] * Z[1] - C[2] * Z[2] - C[3] * Z[3];
		Z[1] = Z[0]; Z[0] = X; Z[3] = Z[2]; Z[2] = Y;
		return Y;
	};
	// Louder as the engine revs (about +7 dB from 2000 to 7000 rpm; it was nearly flat), quieter at idle.
	const double EngineRpm = FMath::Max(TargetHz * (bV8 ? 120.0 : 30.0), 1.0);
	const double RevGain = FMath::Clamp(FMath::Pow(EngineRpm / 5000.0, 0.6), 0.5, 1.15);
	// The four cylinders each a little different (fixed): the half orders of a real inline four.
	static constexpr double CylinderGain[4] = { 1.00, 0.94, 1.04, 0.97 };
	const double PopLevel = In.bCabin ? .30 : .55;
	const double PopDecay = FMath::Exp(-1.0 / (SampleRate * .012));
	for (int32 I = 0; I < Frames; ++I)
	{
		Throttle += (TargetThrottle - Throttle) * Smoothing;
		Charge += (TargetCharge - Charge) * Smoothing;
		Pull += (TargetPull - Pull) * Smoothing;
		Overrun += (TargetOverrun - Overrun) * Smoothing;
		Gain += (TargetGain - Gain) * Smoothing;
		FiringHz += HzStep;
		Phase += FiringHz * Size / SampleRate;
		if (Phase >= Size)
		{
			Phase -= Size;
			// A small zero-mean cycle-to-cycle variation, strongest under light load.
			PulseGain = 1.0 + (static_cast<double>(RandomState) / 1073741824.0 - 1.0) * (.035 - .018 * Charge + Tone.Lope * (1.0 - Charge) * (1.0 - Speed0));
			FireIndex = (FireIndex + 1) & 3;
			if (!bV8) PulseGain *= CylinderGain[FireIndex];
			if (PopChance > 0.0 && static_cast<double>(RandomState) / 2147483647.0 < PopChance)
			{
				PopEnv = PopLevel * (.6 + .4 * static_cast<double>(RandomState & 1023) / 1023.0);
			}
		}
		double CutTarget = 1.0;
		if (bLimiting)
		{
			LimiterPhase += LimiterHz / SampleRate;
			if (LimiterPhase >= 1.0) LimiterPhase -= 1.0;
			if (LimiterPhase < .42) CutTarget = .12;
		}
		CutGain += (CutTarget - CutGain) * CutSmoothing;
		int32 K = static_cast<int32>(Phase) & Mask;
		double T = Phase - FMath::FloorToDouble(Phase);
		double Quiet, Loaded;
		if (bV8)
		{
			Quiet = FMath::Lerp(CycleLight[K], CycleLight[K + 1], T);
			Loaded = FMath::Lerp(CycleLoad[K], CycleLoad[K + 1], T);
			// Intake and the breath window follow each event.
			const double EventPhase = FMath::Fmod(Phase * EventsPerCycle, static_cast<double>(Size));
			K = static_cast<int32>(EventPhase) & Mask;
			T = EventPhase - FMath::FloorToDouble(EventPhase);
		}
		else
		{
			Quiet = FMath::Lerp(ExhaustLight[K], ExhaustLight[K + 1], T);
			Loaded = FMath::Lerp(ExhaustLoad[K], ExhaustLoad[K + 1], T);
		}
		const double Air = FMath::Lerp(Intake[K], Intake[K + 1], T);
		RandomState = (RandomState * 1664525 + 1013904223) & 0x7fffffff;
		const double White = static_cast<double>(RandomState) / 1073741824.0 - 1.0;
		NoiseLow += .32 * (White - NoiseLow);
		NoiseSlow += .04 * (NoiseLow - NoiseSlow);
		const double Breath = (NoiseLow - NoiseSlow) * PulseWindow[K];
		const double Speed = FMath::Clamp(FiringHz / SpeedHz, 0.0, 1.0);
		const double Pulse = FMath::Lerp(Quiet, Loaded, Charge) * PulseGain * CutGain;
		const double ExhaustLevel = (.17 + .34 * Charge + .14 * Pull + .08 * Speed - .045 * Overrun) * ExhaustPresence;
		const double InletLevel = RevGain * (.02 + .42 * Throttle * Charge) * (In.bCabin ? FMath::Clamp(In.CabinInletGain, 0.0, 1.0) : 1.0) * CutGain;
		const double Airflow = (.036 + .13 * Charge + .08 * Overrun) * (.55 + Speed) + .05 * (1.0 - CutGain);
		PopEnv *= PopDecay;
		PopLow += .35 * (White * PopEnv - PopLow);
		const double Dry = Pulse * ExhaustLevel * RevGain;
		// The resonances ring with the exhaust's energy: faint at idle (a real NB's idling cabin is the
		// firing order alone), full under load.
		const double Exhaust = .6 * Dry + .6 * (.15 + .85 * Charge) * Ring(Dry, BodyC, BodyZ) + Tone.RaspGain * (.05 + .35 * Charge) * Ring(Dry, RaspC, RaspZ);
		Boom += .034 * (Exhaust - Boom);
		const double Pressure = Exhaust + Boom * BoomLevel + Air * InletLevel + Breath * Airflow + PopLow;
		OutputLow += Filter * (Pressure - OutputLow);
		Out[I] = static_cast<float>(FMath::Clamp(OutputLow * Gain, -.94, .94));
	}
}
