// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Audio/OsdsEngineVoice.h"
#include "Presentation/OsdsPresentationProfile.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OsdsVoiceTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	using FIn = FOsdsInlineFourSynth::FInputs;

	template <typename FRenderFn>
	TArray<float> Render(int32 Rate, double Seconds, FRenderFn&& Fn)
	{
		TArray<float> Out;
		Out.SetNumZeroed(FMath::RoundToInt(Seconds * Rate));
		for (int32 At = 0; At < Out.Num(); At += 512) Fn(Out.GetData() + At, FMath::Min(512, Out.Num() - At), At);
		return Out;
	}

	TArray<float> Tail(const TArray<float>& X, double FromFraction)
	{
		const int32 From = FMath::RoundToInt(X.Num() * FromFraction);
		return TArray<float>(X.GetData() + From, X.Num() - From);
	}

	/** Amplitude of one frequency (Goertzel). */
	double Magnitude(const TArray<float>& X, double Hz, int32 Rate)
	{
		const double W = UE_DOUBLE_TWO_PI * Hz / Rate, C = 2.0 * FMath::Cos(W);
		double S1 = 0.0, S2 = 0.0;
		for (const float V : X) { const double S = V + C * S1 - S2; S2 = S1; S1 = S; }
		return FMath::Sqrt(FMath::Max(S1 * S1 + S2 * S2 - C * S1 * S2, 0.0)) / X.Num();
	}

	double Rms(const TArray<float>& X)
	{
		double Sum = 0.0;
		for (const float V : X) Sum += V * V;
		return FMath::Sqrt(Sum / FMath::Max(X.Num(), 1));
	}

	double Db(double Ratio) { return 20.0 * FMath::LogX(10.0, FMath::Max(Ratio, 1e-12)); }

	FIn OnLoad(double Rpm, bool bCabin)
	{
		FIn In; In.Rpm = Rpm; In.Combustion = 0.9; In.ShaftLoad = 0.8; In.Pedal = 1.0; In.bCabin = bCabin; In.PopAmount = 0.0; In.LimiterRpm = 8000.0;
		return In;
	}

	FIn OffLoad(double Rpm, bool bCabin)
	{
		FIn In; In.Rpm = Rpm; In.Combustion = 0.0; In.ShaftLoad = -0.3; In.Pedal = 0.0; In.bCabin = bCabin; In.PopAmount = 0.0; In.LimiterRpm = 8000.0;
		return In;
	}

	/** A synthetic recording: firing harmonics of an engine with Events events a cycle, louder with rpm. */
	TSharedPtr<FOsdsLoopBank> MakeBank(int32 Events, bool bWithOff)
	{
		TSharedPtr<FOsdsLoopBank> Bank = MakeShared<FOsdsLoopBank>();
		for (const bool bOn : { true, false })
		{
			if (!bOn && !bWithOff) continue;
			for (const double Rpm : { 1000.0, 2000.0, 3000.0, 4000.0, 6000.0 })
			{
				FOsdsLoopBank::FLoop L;
				L.Rpm = Rpm; L.bOnLoad = bOn; L.bCabin = false; L.Rate = 44100;
				const double Cycle = 120.0 / Rpm;
				const int32 Cycles = FMath::Max(2, FMath::RoundToInt(0.5 / Cycle));
				const int32 N = FMath::RoundToInt(Cycles * Cycle * L.Rate);
				const double Firing = Events / Cycle; // Hz, an exact divisor of the loop: seamless
				const double A = (bOn ? 0.12 : 0.05) * FMath::Sqrt(Rpm / 1000.0);
				L.Samples.SetNumUninitialized(N);
				for (int32 I = 0; I < N; ++I)
				{
					const double T = static_cast<double>(I) / L.Rate;
					double S = 0.0;
					for (int32 K = 1; K <= 6; ++K) S += A / K * FMath::Sin(UE_DOUBLE_TWO_PI * Firing * K * T * (static_cast<double>(N) / L.Rate / (Cycles * Cycle)));
					L.Samples[I] = static_cast<float>(S);
				}
				Bank->Loops.Add(MoveTemp(L));
			}
		}
		return Bank;
	}
}

using namespace OsdsVoiceTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsLoopBankTest, "OSDS.Audio.LoopBank", OsdsVoiceTests::Flags)
bool FOsdsLoopBankTest::RunTest(const FString& Parameters)
{
	constexpr int32 Rate = 44100;
	auto Play = [](TSharedPtr<FOsdsLoopBank> Bank, const FIn& In, double Seconds)
	{
		FOsdsLoopBankVoice V;
		V.SetBank(Bank);
		return Tail(Render(Rate, Seconds, [&](float* O, int32 F, int32) { V.Render(O, F, Rate, In); }), 0.3);
	};
	const TSharedPtr<FOsdsLoopBank> Bank = MakeBank(4, true);
	// Between two loops (2500 rpm, between 2000 and 3000), the firing line sits at rpm / 30.
	const TArray<float> Mid = Play(Bank, OnLoad(2500.0, false), 1.5);
	const double F0 = 2500.0 / 30.0;
	const double M = Magnitude(Mid, F0, Rate), Below = Magnitude(Mid, 2000.0 / 30.0, Rate), Above = Magnitude(Mid, 3000.0 / 30.0, Rate);
	AddInfo(FString::Printf(TEXT("2500 rpm between the 2000 and 3000 loops: %.1f Hz %.4f, 2000-rpm line %.5f, 3000-rpm line %.5f"), F0, M, Below, Above));
	TestTrue(TEXT("both neighbouring loops are pitched onto the engine's speed (one firing line, not two)"), M > 8.0 * Below && M > 8.0 * Above);

	// Louder with revs and with load, as the loops were recorded.
	double Last = 0.0;
	FString Line;
	for (const double Rpm : { 1200.0, 2500.0, 3500.0, 5000.0 })
	{
		const double R = Rms(Play(Bank, OnLoad(Rpm, false), 0.8));
		Line += FString::Printf(TEXT(" %.0f:%.4f"), Rpm, R);
		TestTrue(FString::Printf(TEXT("louder at %.0f rpm than below"), Rpm), R > Last);
		Last = R;
	}
	AddInfo(TEXT("on-load RMS by rpm:") + Line);
	const double On = Rms(Play(Bank, OnLoad(3000.0, false), 0.8)), Off = Rms(Play(Bank, OffLoad(3000.0, false), 0.8));
	TestTrue(FString::Printf(TEXT("on throttle louder than off at 3000 rpm (%.1f dB)"), Db(On / Off)), Db(On / Off) > 3.0);

	// A sweep 1000 -> 6000 rpm in 4 s has no clicks: no sample step far above the signal's own.
	{
		FOsdsLoopBankVoice V;
		V.SetBank(Bank);
		const TArray<float> Sweep = Render(Rate, 4.0, [&](float* O, int32 F, int32 At)
		{
			V.Render(O, F, Rate, OnLoad(1000.0 + 5000.0 * At / (4.0 * Rate), false));
		});
		double MaxStep = 0.0, MeanStep = 0.0;
		for (int32 I = Rate / 10 + 1; I < Sweep.Num(); ++I)
		{
			const double D = FMath::Abs(Sweep[I] - Sweep[I - 1]);
			MaxStep = FMath::Max(MaxStep, D);
			MeanStep += D;
		}
		MeanStep /= Sweep.Num();
		AddInfo(FString::Printf(TEXT("sweep: largest sample step %.4f, mean %.5f"), MaxStep, MeanStep));
		TestTrue(TEXT("no clicks across loop changes (largest step under 12x the mean)"), MaxStep < 12.0 * MeanStep);
	}

	// Without an off-throttle set the on set stands in, quieter; without cabin loops the cabin filter is asked for.
	const TSharedPtr<FOsdsLoopBank> OnOnly = MakeBank(4, false);
	FOsdsLoopBankVoice Borrow;
	Borrow.SetBank(OnOnly);
	TestTrue(TEXT("a bank with no cabin loops asks for the cabin filter"), Borrow.NeedsCabinFilter());
	const double BorrowOff = Rms(Play(OnOnly, OffLoad(3000.0, false), 0.8)), BorrowOn = Rms(Play(OnOnly, OnLoad(3000.0, false), 0.8));
	TestTrue(TEXT("borrowed off-throttle sound is quieter than on"), BorrowOff < 0.7 * BorrowOn);

	// A stall fades the bank out.
	FIn Dead = OffLoad(0.0, false);
	Dead.bStalled = true;
	const TArray<float> Silent = Play(Bank, Dead, 1.0);
	TestTrue(TEXT("silent after a stall"), Rms(Silent) < 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsTurboTest, "OSDS.Audio.Turbo", OsdsVoiceTests::Flags)
bool FOsdsTurboTest::RunTest(const FString& Parameters)
{
	constexpr int32 Rate = 44100;
	FOsdsEngineVoiceSpec::FTurbo Spec;
	Spec.SpoolRpm = 2500.0; Spec.LagS = 0.6; Spec.WhineHz = 5000.0; Spec.BlowOff = TEXT("atmospheric");
	// Off boost (1000 rpm) the shaft barely turns; at 4000 rpm and full pedal it spools within about two lags.
	FOsdsTurboSynth Low, High;
	Low.Configure(Spec); High.Configure(Spec);
	Render(Rate, 2.0, [&](float* O, int32 F, int32) { Low.Render(O, F, Rate, OnLoad(1000.0, false)); });
	const TArray<float> Spooled = Tail(Render(Rate, 2.0, [&](float* O, int32 F, int32) { High.Render(O, F, Rate, OnLoad(4000.0, false)); }), 0.6);
	AddInfo(FString::Printf(TEXT("shaft: %.2f at 1000 rpm, %.2f at 4000 rpm after 2 s"), Low.GetShaft(), High.GetShaft()));
	TestTrue(TEXT("no boost off idle"), Low.GetShaft() < 0.05);
	TestTrue(TEXT("spooled at 4000 rpm"), High.GetShaft() > 0.9);
	const double Whine = Spec.WhineHz * (0.2 + 0.8 * High.GetShaft());
	const double MW = Magnitude(Spooled, Whine, Rate), MOff = Magnitude(Spooled, Whine * 0.8, Rate);
	AddInfo(FString::Printf(TEXT("whine at %.0f Hz %.5f, at 0.8x %.5f"), Whine, MW, MOff));
	TestTrue(TEXT("the compressor whines at its shaft's frequency"), MW > 5.0 * MOff);
	// Lifting on boost vents it: a burst louder than the cruise that came before.
	const TArray<float> Lift = Render(Rate, 0.4, [&](float* O, int32 F, int32) { High.Render(O, F, Rate, OffLoad(4000.0, false)); });
	TArray<float> First(Lift.GetData(), Rate / 10);
	AddInfo(FString::Printf(TEXT("blow-off: first 100 ms RMS %.4f against %.4f spooled"), Rms(First), Rms(Spooled)));
	TestTrue(TEXT("the lift-off vents with a hiss"), High.GetBlowOffEnvelope() > 0.0 && Rms(First) > 1.5 * Rms(Spooled));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsElectricMotorTest, "OSDS.Audio.ElectricMotor", OsdsVoiceTests::Flags)
bool FOsdsElectricMotorTest::RunTest(const FString& Parameters)
{
	constexpr int32 Rate = 44100;
	FOsdsEngineVoiceSpec::FMotor Spec; // 3 pole pairs, 54 slots, 23-tooth pinion
	auto Play = [&](double Rpm, double Torque)
	{
		FOsdsElectricMotorSynth M;
		M.Configure(Spec);
		FIn In; In.MotorRpm = Rpm; In.ShaftLoad = Torque; In.bCabin = false;
		return Tail(Render(Rate, 1.0, [&](float* O, int32 F, int32) { M.Render(O, F, Rate, In); }), 0.3);
	};
	const TArray<float> Pull = Play(6000.0, 0.8), Coast = Play(6000.0, 0.0);
	const double Rev = 100.0; // rev/s at 6000 rpm
	const double Ripple = Magnitude(Pull, 18.0 * Rev, Rate), Mesh = Magnitude(Pull, 23.0 * Rev, Rate), Gap = Magnitude(Pull, 15.5 * Rev, Rate);
	AddInfo(FString::Printf(TEXT("6000 rpm: 18th order %.5f, 23rd (mesh) %.5f, 15.5th (none) %.6f; RMS pulling %.4f, coasting %.4f"), Ripple, Mesh, Gap, Rms(Pull), Rms(Coast)));
	TestTrue(TEXT("the torque-ripple whine at 6 x the electrical frequency"), Ripple > 20.0 * Gap);
	TestTrue(TEXT("the pinion's gear mesh"), Mesh > 20.0 * Gap);
	TestTrue(TEXT("louder pulling than coasting"), Rms(Pull) > 1.5 * Rms(Coast));
	TestTrue(TEXT("rises with speed"), Rms(Play(9000.0, 0.8)) > Rms(Play(1500.0, 0.8)));
	TestTrue(TEXT("still at a standstill"), Rms(Play(0.0, 0.0)) < 1e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsV6Test, "OSDS.Audio.V6", OsdsVoiceTests::Flags)
bool FOsdsV6Test::RunTest(const FString& Parameters)
{
	constexpr int32 Rate = FOsdsInlineFourSynth::SampleRate;
	auto Play = [](double Far)
	{
		FOsdsInlineFourSynth S;
		S.ConfigureEvenFiring({ 1, 0, 1, 0, 1, 0 }, 1.0, Far);
		FIn In = OffLoad(1200.0, false);
		In.Combustion = 0.3; In.ShaftLoad = 0.2;
		return Tail(Render(Rate, 3.0, [&](float* O, int32 F, int32) { S.Render(O, F, In); }), 0.33);
	};
	const TArray<float> X = Play(0.75), Even = Play(1.0);
	const double Firing = 1200.0 / 20.0, OneAndHalf = 1200.0 / 40.0, FourFiring = 1200.0 / 30.0;
	const double MF = Magnitude(X, Firing, Rate), MH = Magnitude(X, OneAndHalf, Rate), MI4 = Magnitude(X, FourFiring, Rate), MHEven = Magnitude(Even, OneAndHalf, Rate);
	AddInfo(FString::Printf(TEXT("V6 at 1200 rpm: firing %.0f Hz %.5f, order 1.5 (%.0f Hz) %.5f (equal banks %.5f), an inline four's %.0f Hz %.5f"),
		Firing, MF, OneAndHalf, MH, MHEven, FourFiring, MI4));
	TestTrue(TEXT("three firings a revolution: rpm / 20"), MF > 3.0 * MI4);
	TestTrue(TEXT("alternating banks growl at order 1.5"), MH > 0.05 * MF && MH > 3.0 * MHEven);
	return true;
}

/**
 * Each car's voice as the game configures it (CarPresentation.json engine_sound): the
 * firing-order line at rpm x events / 120, louder with rpm and with load; and, where the car
 * has a reference recording (data/sound/reference/<car>.json, from cut_loops.py analyze),
 * its order levels against the recording's.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCarVoicesTest, "OSDS.Audio.CarVoices", OsdsVoiceTests::Flags)
bool FOsdsCarVoicesTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Ids = OsdsPresentationProfiles::AvailableIds();
	TestTrue(TEXT("cars to check"), Ids.Num() > 0);
	for (const FString& Id : Ids)
	{
		FOsdsCarPresentationProfile Profile;
		FString Error;
		if (!TestTrue(Id + TEXT(": profile loads ") + Error, OsdsPresentationProfiles::Load(Id, Profile, Error))) continue;
		const FOsdsEngineVoiceSpec Spec = Profile.EngineSound.IsSet() ? Profile.EngineSound->Voice : FOsdsEngineVoiceSpec();
		auto Make = [&Spec]() { TUniquePtr<FOsdsEngineVoice> V = MakeUnique<FOsdsEngineVoice>(); V->Configure(Spec); return V; };
		const int32 Rate = Make()->GetSampleRate();
		auto Play = [&](const FIn& In, double Seconds = 1.2)
		{
			TUniquePtr<FOsdsEngineVoice> V = Make();
			return Tail(Render(Rate, Seconds, [&](float* O, int32 F, int32) { V->Render(O, F, In); }), 0.35);
		};
		if (Spec.IsElectric())
		{
			FIn A = OnLoad(0.0, false); A.MotorRpm = 3000.0;
			FIn B = A; B.MotorRpm = 9000.0;
			const TArray<float> X = Play(A), Y = Play(B);
			const double Ripple = Magnitude(X, 6.0 * Spec.Motor->PolePairs * 50.0, Rate), Gap = Magnitude(X, (6.0 * Spec.Motor->PolePairs - 2.5) * 50.0, Rate);
			AddInfo(FString::Printf(TEXT("%s (%s): 3000 rpm ripple line %.5f vs gap %.6f; RMS %.4f at 3000, %.4f at 9000"), *Id, Make()->Describe(), Ripple, Gap, Rms(X), Rms(Y)));
			TestTrue(Id + TEXT(": the motor's ripple order"), Ripple > 10.0 * Gap);
			TestTrue(Id + TEXT(": louder at speed"), Rms(Y) > Rms(X));
			continue;
		}
		const int32 Events = Spec.EventsPerCycle();
		double LastOn = 0.0;
		FString Table;
		for (const double Rpm : { 1500.0, 3000.0, 4500.0 })
		{
			const TArray<float> On = Play(OnLoad(Rpm, false)), Off = Play(OffLoad(Rpm, false));
			const double Firing = Rpm * Events / 120.0;
			// The firing line against the half orders around it (none of them a firing harmonic).
			double Strongest = 0.0, Neighbours = 0.0;
			for (int32 K = 1; K <= 2 * Events + 2; ++K)
			{
				const double Order = 0.5 * K, Hz = Rpm / 60.0 * Order;
				if (FMath::IsNearlyEqual(FMath::Fmod(Order, Events / 2.0), 0.0)) continue;
				Strongest = FMath::Max(Strongest, Magnitude(On, Hz, Rate));
				Neighbours += Magnitude(On, Hz, Rate);
			}
			Neighbours /= (2 * Events + 2 - 2);
			const double MF = Magnitude(On, Firing, Rate);
			const double ROn = Rms(On), ROff = Rms(Off);
			Table += FString::Printf(TEXT(" | %.0f rpm: firing %.0f Hz %+.1f dB over the other orders' mean, on %.4f, off %.4f"), Rpm, Firing, Db(MF / FMath::Max(Neighbours, 1e-12)), ROn, ROff);
			TestTrue(FString::Printf(TEXT("%s: %.0f rpm, the firing line (%.0f Hz) stands 6 dB over the other orders"), *Id, Rpm, Firing), Db(MF / FMath::Max(Neighbours, 1e-12)) > 6.0);
			TestTrue(FString::Printf(TEXT("%s: %.0f rpm, louder on throttle than off"), *Id, Rpm), ROn > 1.4 * ROff);
			TestTrue(FString::Printf(TEXT("%s: louder at %.0f rpm than below"), *Id, Rpm), ROn > 0.97 * LastOn);
			LastOn = ROn;
		}
		AddInfo(FString::Printf(TEXT("%s (%s, %d events a cycle)%s"), *Id, Make()->Describe(), Events, *Table));

		// Against its reference recording, where one exists.
		FString Text;
		const FString RefPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("sound"), TEXT("reference"), Id + TEXT(".json"));
		TSharedPtr<FJsonObject> Ref;
		if (!FFileHelper::LoadFileToString(Text, *RefPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Ref) || !Ref) continue;
		double Rpm = 0.0, Tolerance = 8.0, Combustion = 0.12;
		FString Mic;
		Ref->TryGetNumberField(TEXT("rpm"), Rpm);
		Ref->TryGetNumberField(TEXT("tolerance_db"), Tolerance);
		Ref->TryGetNumberField(TEXT("combustion"), Combustion);
		Ref->TryGetStringField(TEXT("mic"), Mic);
		const TArray<TSharedPtr<FJsonValue>>* Orders = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
		if (!Ref->TryGetArrayField(TEXT("orders"), Orders) || !Ref->TryGetArrayField(TEXT("order_db"), Levels) || Orders->Num() != Levels->Num()) continue;
		FIn In = OffLoad(Rpm, Mic == TEXT("cabin"));
		In.Combustion = Combustion; In.ShaftLoad = 0.05;
		const TArray<float> X = Play(In, 2.5);
		TArray<double> Mine;
		double Max = 0.0;
		for (const TSharedPtr<FJsonValue>& O : *Orders) { Mine.Add(Magnitude(X, Rpm / 60.0 * O->AsNumber(), Rate)); Max = FMath::Max(Max, Mine.Last()); }
		TArray<double> Errors;
		FString Line;
		for (int32 K = 0; K < Mine.Num(); ++K)
		{
			const double D = FMath::Max(Db(Mine[K] / Max), -45.0);
			Errors.Add(FMath::Abs(D - (*Levels)[K]->AsNumber()));
			Line += FString::Printf(TEXT(" %.1f/%.1f"), D, (*Levels)[K]->AsNumber());
		}
		Errors.Sort();
		const double Median = Errors.Num() % 2 ? Errors[Errors.Num() / 2] : 0.5 * (Errors[Errors.Num() / 2 - 1] + Errors[Errors.Num() / 2]);
		FString File;
		Ref->TryGetStringField(TEXT("file"), File);
		AddInfo(FString::Printf(TEXT("%s against %s at %.0f rpm (%s): orders game/recording dB%s; median error %.2f dB"), *Id, *File, Rpm, *Mic, *Line, Median));
		TestTrue(FString::Printf(TEXT("%s: order levels within %.1f dB (median) of its reference recording (%.2f)"), *Id, Tolerance, Median), Median < Tolerance);
	}
	return true;
}

/** Every car's voice through a pull, a lift and the limiter, outside and in, as WAVs in Saved/Audio (the A/B material). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCarSweepsTest, "OSDS.Audio.CarSweeps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOsdsCarSweepsTest::RunTest(const FString& Parameters)
{
	for (const FString& Id : OsdsPresentationProfiles::AvailableIds())
	{
		FOsdsCarPresentationProfile Profile;
		FString Error;
		if (!OsdsPresentationProfiles::Load(Id, Profile, Error)) continue;
		const FOsdsEngineVoiceSpec Spec = Profile.EngineSound.IsSet() ? Profile.EngineSound->Voice : FOsdsEngineVoiceSpec();
		for (const bool bCabin : { false, true })
		{
			FOsdsEngineVoice V;
			V.Configure(Spec);
			const int32 Rate = V.GetSampleRate();
			const TArray<float> X = Render(Rate, 12.0, [&](float* O, int32 F, int32 At)
			{
				const double T = static_cast<double>(At) / Rate;
				FIn In;
				if (T < 1.5) In = OffLoad(800.0, bCabin), In.Combustion = 0.12;
				else if (T < 8.0) In = OnLoad(1500.0 + 4500.0 * (T - 1.5) / 6.5, bCabin);
				else if (T < 10.0) In = OffLoad(6000.0 - 3500.0 * (T - 8.0) / 2.0, bCabin);
				else In = OnLoad(2500.0 + 1500.0 * (T - 10.0) / 2.0, bCabin);
				In.MotorRpm = Spec.IsElectric() ? In.Rpm * 2.5 : 0.0;
				V.Render(O, F, In);
			});
			TArray<uint8> Wav;
			auto U32 = [&Wav](uint32 Value) { for (int32 B = 0; B < 4; ++B) Wav.Add((Value >> (8 * B)) & 255); };
			auto U16 = [&Wav](uint16 Value) { Wav.Add(Value & 255); Wav.Add(Value >> 8); };
			Wav.Append((const uint8*)"RIFF", 4); U32(36 + X.Num() * 2); Wav.Append((const uint8*)"WAVEfmt ", 8);
			U32(16); U16(1); U16(1); U32(Rate); U32(Rate * 2); U16(2); U16(16); Wav.Append((const uint8*)"data", 4); U32(X.Num() * 2);
			int32 Clipped = 0;
			for (const float S : X) { if (FMath::Abs(S) >= 0.93f) ++Clipped; U16(static_cast<uint16>(static_cast<int16>(FMath::Clamp(S, -1.f, 1.f) * 32000.f))); }
			FFileHelper::SaveArrayToFile(Wav, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Audio"), FString::Printf(TEXT("%s-%s.wav"), *Id, bCabin ? TEXT("cabin") : TEXT("outside"))));
			TestTrue(FString::Printf(TEXT("%s %s: clipped %.2f %% of samples"), *Id, bCabin ? TEXT("cabin") : TEXT("outside"), 100.0 * Clipped / X.Num()), Clipped <= X.Num() / 200);
		}
	}
	return true;
}

#endif
