// Copyright OSDS contributors. All Rights Reserved.

#include "Audio/OsdsEngineVoice.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsEngineVoice, Log, All);

namespace
{
	double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** RBJ band-pass (constant peak gain), C = b0, b2, a1, a2 (b1 = 0). */
	void BandPass(double F, double Q, int32 Rate, double* C)
	{
		F = FMath::Clamp(F, 20.0, 0.45 * Rate);
		const double W = 2.0 * UE_DOUBLE_PI * F / Rate, A = FMath::Sin(W) / (2.0 * Q), N = 1.0 + A;
		C[0] = A / N; C[1] = -A / N; C[2] = -2.0 * FMath::Cos(W) / N; C[3] = (1.0 - A) / N;
	}

	/** Z = x1, x2, y1, y2. */
	double Ring(double X, const double* C, double* Z)
	{
		const double Y = C[0] * X + C[1] * Z[1] - C[2] * Z[2] - C[3] * Z[3];
		Z[1] = Z[0]; Z[0] = X; Z[3] = Z[2]; Z[2] = Y;
		return Y;
	}

	double White(int64& State)
	{
		State = (State * 1664525 + 1013904223) & 0x7fffffff;
		return static_cast<double>(State) / 1073741824.0 - 1.0;
	}

	/** 4-point Hermite on a looping buffer. */
	double Hermite(const TArray<float>& X, double Pos)
	{
		const int32 N = X.Num();
		const int32 I = static_cast<int32>(Pos);
		const double T = Pos - I;
		const double Ym1 = X[(I - 1 + N) % N], Y0 = X[I % N], Y1 = X[(I + 1) % N], Y2 = X[(I + 2) % N];
		const double C1 = 0.5 * (Y1 - Ym1);
		const double C2 = Ym1 - 2.5 * Y0 + 2.0 * Y1 - 0.5 * Y2;
		const double C3 = 0.5 * (Y2 - Ym1) + 1.5 * (Y0 - Y1);
		return ((C3 * T + C2) * T + C1) * T + Y0;
	}
}

// --- The spec -----------------------------------------------------------------

bool OsdsEngineVoice::ParseSpec(const FJsonObject& J, FOsdsEngineVoiceSpec& Out, FString& OutError)
{
	J.TryGetStringField(TEXT("layout"), Out.Layout);
	if (Out.Layout.IsEmpty()) Out.Layout = TEXT("i4");
	const TArray<TSharedPtr<FJsonValue>>* Banks = nullptr;
	if (J.TryGetArrayField(TEXT("bank_of_event"), Banks) && Banks)
	{
		for (const TSharedPtr<FJsonValue>& V : *Banks) Out.BankOfEvent.Add(FMath::Clamp(static_cast<int32>(V->AsNumber()), 0, 1));
	}
	const int32 Want = Out.Layout == TEXT("v8_crossplane") ? 8 : Out.Layout == TEXT("v6_60") ? 6 : 0;
	if (Out.Layout != TEXT("i4") && Out.Layout != TEXT("electric") && Want == 0)
	{
		OutError = FString::Printf(TEXT("'engine_sound' layout '%s' is not one of i4, v6_60, v8_crossplane, electric"), *Out.Layout);
		return false;
	}
	if (Want > 0 && Out.BankOfEvent.Num() != Want)
	{
		OutError = FString::Printf(TEXT("'engine_sound' layout '%s' needs %d 'bank_of_event' values (0 or 1)"), *Out.Layout, Want);
		return false;
	}
	if (Want == 0) Out.BankOfEvent.Reset();
	J.TryGetNumberField(TEXT("near_gain"), Out.NearGain);
	J.TryGetNumberField(TEXT("far_gain"), Out.FarGain);
	J.TryGetStringField(TEXT("source"), Out.Source);
	J.TryGetStringField(TEXT("recording"), Out.Recording);

	const TSharedPtr<FJsonObject>* Tone = nullptr;
	if (J.TryGetObjectField(TEXT("tone"), Tone) && Tone)
	{
		FOsdsInlineFourSynth::FTone& T = Out.Tone;
		(*Tone)->TryGetNumberField(TEXT("body_hz"), T.BodyHz);
		(*Tone)->TryGetNumberField(TEXT("body_q"), T.BodyQ);
		(*Tone)->TryGetNumberField(TEXT("rasp_hz"), T.RaspHz);
		(*Tone)->TryGetNumberField(TEXT("rasp_gain"), T.RaspGain);
		(*Tone)->TryGetNumberField(TEXT("lope"), T.Lope);
		(*Tone)->TryGetNumberField(TEXT("cylinder_spread"), T.CylinderSpread);
		(*Tone)->TryGetNumberField(TEXT("cabin_filter"), T.CabinFilter);
		(*Tone)->TryGetNumberField(TEXT("exterior_filter"), T.ExteriorFilter);
		if (T.BodyHz < 40.0 || T.BodyHz > 1000.0 || T.RaspHz < 300.0 || T.RaspHz > 6000.0 || T.BodyQ < 0.3 || T.BodyQ > 10.0
			|| T.RaspGain < 0.0 || T.RaspGain > 3.0 || T.Lope < 0.0 || T.Lope > 0.3 || T.CylinderSpread < 0.0 || T.CylinderSpread > 0.3
			|| T.CabinFilter <= 0.0 || T.CabinFilter > 1.0 || T.ExteriorFilter <= 0.0 || T.ExteriorFilter > 1.0)
		{
			OutError = TEXT("'engine_sound' tone: body_hz 40-1000, body_q 0.3-10, rasp_hz 300-6000, rasp_gain 0-3, lope 0-0.3, cylinder_spread 0-0.3, filters 0-1");
			return false;
		}
	}
	const TSharedPtr<FJsonObject>* Turbo = nullptr;
	if (J.TryGetObjectField(TEXT("turbo"), Turbo) && Turbo)
	{
		FOsdsEngineVoiceSpec::FTurbo T;
		(*Turbo)->TryGetNumberField(TEXT("spool_rpm"), T.SpoolRpm);
		(*Turbo)->TryGetNumberField(TEXT("lag_s"), T.LagS);
		(*Turbo)->TryGetNumberField(TEXT("whine_hz"), T.WhineHz);
		(*Turbo)->TryGetNumberField(TEXT("whine_gain"), T.WhineGain);
		(*Turbo)->TryGetNumberField(TEXT("whoosh_gain"), T.WhooshGain);
		(*Turbo)->TryGetStringField(TEXT("blow_off"), T.BlowOff);
		(*Turbo)->TryGetNumberField(TEXT("count"), T.Count);
		if (T.SpoolRpm < 800.0 || T.SpoolRpm > 8000.0 || T.LagS < 0.05 || T.LagS > 5.0 || T.WhineHz < 500.0 || T.WhineHz > 16000.0
			|| (T.BlowOff != TEXT("recirc") && T.BlowOff != TEXT("atmospheric") && T.BlowOff != TEXT("none")) || T.Count < 1 || T.Count > 2)
		{
			OutError = TEXT("'engine_sound' turbo: spool_rpm 800-8000, lag_s 0.05-5, whine_hz 500-16000, blow_off recirc|atmospheric|none, count 1-2");
			return false;
		}
		Out.Turbo = T;
	}
	const TSharedPtr<FJsonObject>* Motor = nullptr;
	if (J.TryGetObjectField(TEXT("motor"), Motor) && Motor)
	{
		FOsdsEngineVoiceSpec::FMotor M;
		(*Motor)->TryGetNumberField(TEXT("pole_pairs"), M.PolePairs);
		(*Motor)->TryGetNumberField(TEXT("slots"), M.Slots);
		(*Motor)->TryGetNumberField(TEXT("pinion_teeth"), M.PinionTeeth);
		(*Motor)->TryGetNumberField(TEXT("max_rpm"), M.MaxRpm);
		(*Motor)->TryGetNumberField(TEXT("gain"), M.Gain);
		if (M.PolePairs < 1 || M.PolePairs > 12 || M.Slots < 6 || M.Slots > 120 || M.PinionTeeth < 8 || M.PinionTeeth > 60 || M.MaxRpm < 3000.0)
		{
			OutError = TEXT("'engine_sound' motor: pole_pairs 1-12, slots 6-120, pinion_teeth 8-60, max_rpm 3000+");
			return false;
		}
		Out.Motor = M;
	}
	if (Out.IsElectric() && !Out.Motor.IsSet()) Out.Motor = FOsdsEngineVoiceSpec::FMotor();
	return true;
}

// --- Turbo --------------------------------------------------------------------

void FOsdsTurboSynth::Render(float* Out, int32 Frames, int32 Rate, const FOsdsInlineFourSynth::FInputs& In)
{
	if (Frames < 1) return;
	const double Dt = 1.0 / Rate;
	const double TargetPedal = In.bStalled ? 0.0 : FMath::Clamp(In.Pedal, 0.0, 1.0);
	// Boost the engine can make here: none off idle, full from the spool speed up.
	const double Want = TargetPedal * Smooth(0.45 * Spec.SpoolRpm, Spec.SpoolRpm, In.Rpm);
	// Lifting on boost vents the charge (the valve opens on manifold vacuum).
	if (PrevPedal > 0.45 && TargetPedal < 0.15 && Shaft > 0.3)
	{
		BlowEnv = FMath::Min(1.0, Shaft * 1.2);
	}
	PrevPedal = TargetPedal;
	const bool bAtmospheric = Spec.BlowOff == TEXT("atmospheric"), bNone = Spec.BlowOff == TEXT("none");
	const double Rise = 1.0 - FMath::Exp(-Dt / FMath::Max(Spec.LagS, 0.05));
	const double Fall = 1.0 - FMath::Exp(-Dt / FMath::Max(1.6 * Spec.LagS, 0.1));
	const double PedalSmooth = 1.0 - FMath::Exp(-Dt / 0.03);
	const double BlowDecay = FMath::Exp(-Dt / (bAtmospheric ? 0.35 : bNone ? 0.45 : 0.22));
	double WhooshC[4], BlowC[4];
	BandPass(1800.0, 0.7, Rate, WhooshC);
	BandPass(bAtmospheric ? 2600.0 : bNone ? 650.0 : 900.0, bAtmospheric ? 0.6 : 0.9, Rate, BlowC);
	const double Pickup = In.bCabin ? 0.6 : 1.0;
	const double Twin = Spec.Count > 1 ? 1.25 : 1.0;
	const double BlowLevel = bAtmospheric ? 0.16 : bNone ? 0.10 : 0.05;
	for (int32 I = 0; I < Frames; ++I)
	{
		Pedal += (TargetPedal - Pedal) * PedalSmooth;
		Shaft += (Want - Shaft) * (Want > Shaft ? Rise : Fall);
		const double F = Spec.WhineHz * (0.2 + 0.8 * Shaft);
		const double A = 0.035 * Spec.WhineGain * Twin * Shaft * Shaft * (0.35 + 0.65 * Pedal);
		WhinePhase = FMath::Fmod(WhinePhase + F * Dt, 1.0);
		Whine2Phase = FMath::Fmod(Whine2Phase + 2.0 * F * Dt, 1.0);
		double S = 0.0;
		if (F < 0.45 * Rate) S += A * FMath::Sin(UE_DOUBLE_TWO_PI * WhinePhase);
		if (2.0 * F < 0.45 * Rate) S += 0.3 * A * FMath::Sin(UE_DOUBLE_TWO_PI * Whine2Phase);
		const double N = White(Random);
		S += 0.06 * Spec.WhooshGain * Twin * Shaft * Pedal * Ring(N, WhooshC, BpZ);
		if (BlowEnv > 1e-4)
		{
			double Burst = Ring(N, BlowC, BlowZ) * BlowEnv * BlowLevel;
			if (bNone)
			{
				// Compressor surge: the air stalls and restarts at about 18 Hz.
				FlutterPhase = FMath::Fmod(FlutterPhase + 18.0 * Dt, 1.0);
				Burst *= FlutterPhase < 0.45 ? 1.0 : 0.15;
			}
			S += Burst;
			BlowEnv *= BlowDecay;
		}
		Out[I] = static_cast<float>(Out[I] + S * Pickup);
	}
}

// --- Electric motor -----------------------------------------------------------

TArray<TPair<double, double>> FOsdsElectricMotorSynth::Orders() const
{
	const double Pp = Spec.PolePairs;
	// Torque ripple at 6 x the electrical frequency is the classic inverter-motor whine; the
	// slot-pass and the pinion's mesh come from the iron and the gears (levels are estimates).
	return {
		{ 2.0 * Pp, 0.15 }, { 6.0 * Pp, 0.50 }, { 12.0 * Pp, 0.22 },
		{ static_cast<double>(Spec.Slots), 0.18 },
		{ static_cast<double>(Spec.PinionTeeth), 0.42 }, { 2.0 * Spec.PinionTeeth, 0.12 },
	};
}

void FOsdsElectricMotorSynth::Render(float* Out, int32 Frames, int32 Rate, const FOsdsInlineFourSynth::FInputs& In)
{
	const TArray<TPair<double, double>> List = Orders();
	if (Phases.Num() != List.Num()) Phases.SetNumZeroed(List.Num());
	const double TargetRpm = FMath::Clamp(FMath::Abs(In.MotorRpm), 0.0, Spec.MaxRpm * 1.05);
	if (!bStarted) { Rpm = TargetRpm; bStarted = true; }
	const double RpmStep = (TargetRpm - Rpm) / FMath::Max(Frames, 1);
	const double TargetTorque = FMath::Clamp(FMath::Abs(In.ShaftLoad), 0.0, 1.0);
	const double Smoothing = 1.0 - FMath::Exp(-1.0 / (Rate * 0.04));
	const double Pickup = (In.bCabin ? 0.7 : 1.0) * Spec.Gain;
	for (int32 I = 0; I < Frames; ++I)
	{
		Rpm += RpmStep;
		Torque += (TargetTorque - Torque) * Smoothing;
		Gain += ((In.bStalled ? 0.0 : 1.0) - Gain) * Smoothing;
		const double RevPerS = Rpm / 60.0;
		// Louder over the motor's whole range (the whine grows with speed on the road).
		const double SpeedLevel = FMath::Sqrt(FMath::Clamp(Rpm / (0.6 * Spec.MaxRpm), 0.0, 1.0));
		double S = 0.0;
		for (int32 K = 0; K < List.Num(); ++K)
		{
			const double F = List[K].Key * RevPerS;
			Phases[K] = FMath::Fmod(Phases[K] + F / Rate, 1.0);
			if (F > 0.45 * Rate || F < 20.0) continue;
			// Gear mesh grows with load; the magnetic orders keep a floor while the inverter runs.
			const double Load = K >= 4 ? (0.15 + 0.85 * Torque) : (0.3 + 0.7 * Torque);
			S += List[K].Value * Load * FMath::Sin(UE_DOUBLE_TWO_PI * Phases[K]);
		}
		Out[I] = static_cast<float>(0.06 * S * SpeedLevel * Pickup * Gain);
	}
}

// --- Loop bank ----------------------------------------------------------------

namespace
{
	bool ReadWav(const FString& Path, TArray<float>& Out, int32& OutRate, FString& OutError)
	{
		TArray<uint8> B;
		if (!FFileHelper::LoadFileToArray(B, *Path)) { OutError = Path + TEXT(": cannot read"); return false; }
		auto U32 = [&B](int32 At) { return static_cast<uint32>(B[At]) | (B[At + 1] << 8) | (B[At + 2] << 16) | (static_cast<uint32>(B[At + 3]) << 24); };
		auto U16 = [&B](int32 At) { return static_cast<uint16>(B[At] | (B[At + 1] << 8)); };
		if (B.Num() < 44 || FMemory::Memcmp(B.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(B.GetData() + 8, "WAVE", 4) != 0)
		{
			OutError = Path + TEXT(": not a WAV file");
			return false;
		}
		int32 Format = 0, Channels = 0, Bits = 0, At = 12;
		while (At + 8 <= B.Num())
		{
			const int32 Size = static_cast<int32>(U32(At + 4));
			const int32 Body = At + 8;
			if (Size < 0 || Body + Size > B.Num()) break;
			if (FMemory::Memcmp(B.GetData() + At, "fmt ", 4) == 0 && Size >= 16)
			{
				Format = U16(Body); Channels = U16(Body + 2); OutRate = static_cast<int32>(U32(Body + 4)); Bits = U16(Body + 14);
				if (Format == 0xFFFE && Size >= 26) Format = U16(Body + 24);
			}
			else if (FMemory::Memcmp(B.GetData() + At, "data", 4) == 0)
			{
				if (Channels < 1 || OutRate < 8000) break;
				const int32 Bytes = Bits / 8, Frame = Bytes * Channels, Count = Size / FMath::Max(Frame, 1);
				Out.SetNumUninitialized(Count);
				for (int32 I = 0; I < Count; ++I)
				{
					double Sum = 0.0;
					for (int32 C = 0; C < Channels; ++C)
					{
						const int32 P = Body + I * Frame + C * Bytes;
						if (Format == 1 && Bits == 16) Sum += static_cast<int16>(U16(P)) / 32768.0;
						else if (Format == 1 && Bits == 24) Sum += static_cast<double>((static_cast<int32>(B[P] << 8 | B[P + 1] << 16 | B[P + 2] << 24)) >> 8) / 8388608.0;
						else if (Format == 3 && Bits == 32) { uint32 V = U32(P); float F; FMemory::Memcpy(&F, &V, 4); Sum += F; }
						else { OutError = Path + TEXT(": only 16/24-bit PCM or 32-bit float WAV"); return false; }
					}
					Out[I] = static_cast<float>(Sum / Channels);
				}
				return Out.Num() > 16;
			}
			At = Body + Size + (Size & 1);
		}
		OutError = Path + TEXT(": no fmt/data chunk");
		return false;
	}
}

bool FOsdsLoopBank::Load(const FString& Dir, FString& OutError)
{
	FString Text;
	const FString Manifest = FPaths::Combine(Dir, TEXT("bank.json"));
	if (!FFileHelper::LoadFileToString(Text, *Manifest)) { OutError = Manifest + TEXT(": cannot read"); return false; }
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) { OutError = Manifest + TEXT(": not JSON"); return false; }
	Root->TryGetStringField(TEXT("name"), Name);
	Root->TryGetStringField(TEXT("credit"), Credit);
	Root->TryGetStringField(TEXT("licence"), Licence);
	Root->TryGetNumberField(TEXT("gain_db"), GainDb);
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Root->TryGetArrayField(TEXT("loops"), List) || !List || List->Num() == 0) { OutError = Manifest + TEXT(": no 'loops'"); return false; }
	for (const TSharedPtr<FJsonValue>& V : *List)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V->TryGetObject(O) || !O) continue;
		FLoop L;
		FString File, LoadName, Mic;
		(*O)->TryGetStringField(TEXT("file"), File);
		(*O)->TryGetNumberField(TEXT("rpm"), L.Rpm);
		(*O)->TryGetStringField(TEXT("load"), LoadName);
		(*O)->TryGetStringField(TEXT("mic"), Mic);
		L.bOnLoad = LoadName != TEXT("off");
		L.bCabin = Mic == TEXT("cabin");
		if (L.Rpm < 300.0 || L.Rpm > 12000.0) { OutError = Manifest + TEXT(": a loop's rpm is out of 300-12000"); return false; }
		if (!ReadWav(FPaths::Combine(Dir, File), L.Samples, L.Rate, OutError)) return false;
		Loops.Add(MoveTemp(L));
	}
	return Loops.Num() > 0;
}

bool FOsdsLoopBank::Has(bool bCabin, bool bOnLoad) const
{
	for (const FLoop& L : Loops) if (L.bCabin == bCabin && L.bOnLoad == bOnLoad) return true;
	return false;
}

void FOsdsLoopBankVoice::SetBank(TSharedPtr<const FOsdsLoopBank> InBank)
{
	Bank = InBank;
	for (FSet& S : Sets) S = FSet();
	bStarted = false;
	if (!HasBank()) return;
	Positions.SetNumZeroed(Bank->Loops.Num());
	TArray<int32> Real[4];
	for (int32 I = 0; I < Bank->Loops.Num(); ++I)
	{
		const FOsdsLoopBank::FLoop& L = Bank->Loops[I];
		Real[(L.bCabin ? 2 : 0) + (L.bOnLoad ? 1 : 0)].Add(I);
	}
	for (TArray<int32>& R : Real) R.Sort([this](int32 A, int32 B) { return Bank->Loops[A].Rpm < Bank->Loops[B].Rpm; });
	for (int32 Mic = 0; Mic < 2; ++Mic)
	{
		for (int32 Load = 0; Load < 2; ++Load)
		{
			FSet& S = Sets[Mic * 2 + Load];
			const int32 Other = 1 - Mic;
			// Prefer this mic's other load, then the other mic's same load, then anything.
			const int32 Order[4][2] = { { Mic, Load }, { Mic, 1 - Load }, { Other, Load }, { Other, 1 - Load } };
			for (const auto& O : Order)
			{
				const TArray<int32>& R = Real[O[0] * 2 + O[1]];
				if (R.Num() == 0) continue;
				S.Loops = R;
				if (O[1] != Load)
				{
					// Off throttle borrowed from on: quieter and darker; on from off: louder.
					S.Gain = Load == 0 ? 0.4 : 1.6;
					S.bDarken = Load == 0;
				}
				break;
			}
		}
	}
}

double FOsdsLoopBankVoice::PlaySet(const FSet& Set, double Rpm, int32 Rate)
{
	const int32 N = Set.Loops.Num();
	if (N == 0) return 0.0;
	int32 J = 0;
	while (J < N && Bank->Loops[Set.Loops[J]].Rpm <= Rpm) ++J;
	if (J == 0) return Hermite(Bank->Loops[Set.Loops[0]].Samples, Positions[Set.Loops[0]]) * Set.Gain;
	if (J == N) return Hermite(Bank->Loops[Set.Loops[N - 1]].Samples, Positions[Set.Loops[N - 1]]) * Set.Gain;
	const int32 A = Set.Loops[J - 1], B = Set.Loops[J];
	const double Ra = Bank->Loops[A].Rpm, Rb = Bank->Loops[B].Rpm;
	const double X = FMath::Clamp(FMath::Loge(Rpm / Ra) / FMath::Loge(Rb / Ra), 0.0, 1.0);
	const double Wa = FMath::Cos(X * UE_DOUBLE_HALF_PI), Wb = FMath::Sin(X * UE_DOUBLE_HALF_PI);
	return (Wa * Hermite(Bank->Loops[A].Samples, Positions[A]) + Wb * Hermite(Bank->Loops[B].Samples, Positions[B])) * Set.Gain;
}

void FOsdsLoopBankVoice::Render(float* Out, int32 Frames, int32 Rate, const FOsdsInlineFourSynth::FInputs& In)
{
	if (!HasBank() || Frames < 1)
	{
		if (Frames > 0) FMemory::Memzero(Out, sizeof(float) * Frames);
		return;
	}
	double Lowest = 1e9;
	for (const FOsdsLoopBank::FLoop& L : Bank->Loops) Lowest = FMath::Min(Lowest, L.Rpm);
	const double TargetRpm = FMath::Clamp(In.Rpm, 0.5 * Lowest, In.LimiterRpm + 200.0);
	if (!bStarted) { SmoothRpm = TargetRpm; Cabin = In.bCabin ? 1.0 : 0.0; bStarted = true; }
	const double RpmStep = (TargetRpm - SmoothRpm) / Frames;
	const double TargetCharge = In.bStalled ? 0.0 : FMath::Clamp(In.Combustion, 0.0, 1.0);
	const double Smoothing = 1.0 - FMath::Exp(-1.0 / (Rate * 0.025));
	const double MicSmoothing = 1.0 - FMath::Exp(-1.0 / (Rate * 0.05));
	const double CutSmoothing = 1.0 - FMath::Exp(-1.0 / (Rate * 0.0025));
	const bool bLimiting = In.Rpm > In.LimiterRpm && In.Pedal > 0.35 && !In.bStalled;
	const double Master = FMath::Pow(10.0, Bank->GainDb / 20.0);
	const double Dark = 1.0 - FMath::Exp(-UE_DOUBLE_TWO_PI * 900.0 / Rate);
	for (int32 I = 0; I < Frames; ++I)
	{
		SmoothRpm += RpmStep;
		Charge += (TargetCharge - Charge) * Smoothing;
		Gain += ((In.bStalled ? 0.0 : 1.0) - Gain) * Smoothing;
		Cabin += ((In.bCabin ? 1.0 : 0.0) - Cabin) * MicSmoothing;
		// Every loop runs on, pitched to the engine, so a loop that fades in is already in step.
		for (int32 K = 0; K < Positions.Num(); ++K)
		{
			const FOsdsLoopBank::FLoop& L = Bank->Loops[K];
			Positions[K] += (SmoothRpm / L.Rpm) * static_cast<double>(L.Rate) / Rate;
			const double Len = L.Samples.Num();
			if (Positions[K] >= Len) Positions[K] -= Len * FMath::FloorToDouble(Positions[K] / Len);
		}
		const double On = FMath::Sqrt(Smooth(0.04, 0.45, Charge));
		const double Off = FMath::Sqrt(1.0 - On * On);
		double Mix = 0.0;
		for (int32 Mic = 0; Mic < 2; ++Mic)
		{
			const double MicW = Mic == 1 ? FMath::Sqrt(Cabin) : FMath::Sqrt(1.0 - Cabin);
			if (MicW < 1e-3) continue;
			const FSet& OnSet = Sets[Mic * 2 + 1];
			const FSet& OffSet = Sets[Mic * 2];
			double OffS = Off > 1e-3 ? PlaySet(OffSet, SmoothRpm, Rate) : 0.0;
			if (OffSet.bDarken) { DarkZ += Dark * (OffS - DarkZ); OffS = DarkZ; }
			const double OnS = On > 1e-3 ? PlaySet(OnSet, SmoothRpm, Rate) : 0.0;
			Mix += MicW * (On * OnS + Off * OffS);
		}
		double CutTarget = 1.0;
		if (bLimiting)
		{
			LimiterPhase += FOsdsInlineFourSynth::LimiterHz / Rate;
			if (LimiterPhase >= 1.0) LimiterPhase -= 1.0;
			if (LimiterPhase < .42) CutTarget = .12;
		}
		CutGain += (CutTarget - CutGain) * CutSmoothing;
		Out[I] = static_cast<float>(FMath::Clamp(Mix * Master * Gain * CutGain, -.94, .94));
	}
}

// --- The car's voice ----------------------------------------------------------

void FOsdsEngineVoice::Configure(const FOsdsEngineVoiceSpec& Spec)
{
	bElectric = Spec.IsElectric();
	if (bElectric) Motor.Configure(Spec.Motor.Get(FOsdsEngineVoiceSpec::FMotor()));
	Synth.SetTone(Spec.Tone);
	if (Spec.BankOfEvent.Num() >= 3) Synth.ConfigureEvenFiring(Spec.BankOfEvent, Spec.NearGain, Spec.FarGain);
	bTurbo = Spec.Turbo.IsSet() && !bElectric;
	if (bTurbo) Turbo.Configure(Spec.Turbo.GetValue());
	if (!Spec.Recording.IsEmpty() && !bElectric)
	{
		// Banks are shared (traffic cars of one model play the same loops) and read once.
		static FCriticalSection CacheLock;
		static TMap<FString, TSharedPtr<const FOsdsLoopBank>> Cache;
		FScopeLock Lock(&CacheLock);
		TSharedPtr<const FOsdsLoopBank>* Found = Cache.Find(Spec.Recording);
		if (!Found)
		{
			TSharedPtr<FOsdsLoopBank> Bank = MakeShared<FOsdsLoopBank>();
			FString Error;
			if (!Bank->Load(FPaths::Combine(FPaths::ProjectDir(), Spec.Recording), Error))
			{
				UE_LOG(LogOsdsEngineVoice, Warning, TEXT("Engine recording not used, the synth plays instead: %s"), *Error);
				Bank.Reset();
			}
			Found = &Cache.Add(Spec.Recording, Bank);
		}
		if (Found->IsValid()) Loops.SetBank(*Found);
	}
	const int32 N = Spec.EventsPerCycle();
	Description = bElectric ? TEXT("electric motor") : Loops.HasBank() ? TEXT("recorded loops") :
		N == 8 ? TEXT("cross-plane V8 synth") : N == 6 ? TEXT("V6 synth") : TEXT("inline-four synth");
	if (bTurbo) Description += TEXT(" + turbo");
}

const TCHAR* FOsdsEngineVoice::Describe() const
{
	return *Description;
}

void FOsdsEngineVoice::Render(float* Out, int32 Frames, const FOsdsInlineFourSynth::FInputs& In)
{
	const int32 Rate = GetSampleRate();
	switch (GetKind())
	{
	case EKind::Electric: Motor.Render(Out, Frames, Rate, In); break;
	case EKind::Recording: Loops.Render(Out, Frames, Rate, In); break;
	default: Synth.Render(Out, Frames, In); break;
	}
	if (bTurbo) Turbo.Render(Out, Frames, Rate, In);
	for (int32 I = 0; I < Frames; ++I) Out[I] = FMath::Clamp(Out[I], -.94f, .94f);
}
