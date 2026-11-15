// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseHaptics.h"

#include "Vehicle/CarDefinition.h"

namespace SimCruiseHaptics
{
	double PlateauSlipAngleRad(const FSimCruiseCarDefinition& Car)
	{
		constexpr int32 Samples = 1001;
		double Force[Samples];
		double Best = 0.0;
		for (int32 N = 0; N < Samples; ++N)
		{
			const double Alpha = N * 0.001;
			Force[N] = FMath::Tanh(Alpha * Car.SlipAngleStiffnessPerRad)
				* (1.0 - Car.SlipAngleFalloffFraction * FMath::SmoothStep(Car.SlipAngleFalloffStartRad, Car.SlipAngleFalloffEndRad, Alpha));
			Best = FMath::Max(Best, Force[N]);
		}
		for (int32 N = 0; N < Samples; ++N)
		{
			if (Force[N] >= Plateau * Best)
			{
				return FMath::Max(N * 0.001, 0.001);
			}
		}
		return 1.0;
	}

	double NormalizedSlip(double Kappa, double PeakKappa, double Alpha, double PeakAlpha)
	{
		const double X = Kappa / FMath::Max(PeakKappa, 0.001);
		const double Y = Alpha / FMath::Max(PeakAlpha, 0.001);
		return FMath::Sqrt(X * X + Y * Y);
	}

	double SlipFeel(double S)
	{
		return LimitShare * FMath::SmoothStep(SlipOnset, SlipLimit, S) + (1.0 - LimitShare) * FMath::SmoothStep(SlipLimit, SlipSlide, S);
	}

	double AbsPulse(bool bActive, double TimeS)
	{
		return bActive && FMath::Fmod(TimeS * AbsHz, 1.0) < 0.5 ? AbsOn : 0.0;
	}

	FSimCruiseRumble Motors(double FrontSlip, double RearSlip, double Pulse, double Roughness, double Loose,
		double SpeedMps, double EngineLoad, bool bAbsOn)
	{
		double Weak = SlipFeel(FrontSlip);
		Weak += TextureGain * FMath::SmoothStep(TextureFrom, TextureTo, Roughness);
		Weak += LooseGain * FMath::Clamp(Loose, 0.0, 1.0) * FMath::SmoothStep(0.5, 6.0, FMath::Abs(SpeedMps));
		Weak += EngineGain * FMath::Clamp(EngineLoad, 0.0, 1.0);
		// Kerbs are a sharp pulse on the side of the wheel (Sample, KerbPulse), not a steady buzz here.
		double Strong = SlipFeel(RearSlip) * (bAbsOn ? AbsSlipShare : 1.0) + Pulse;

		FSimCruiseRumble Out;
		Out.HighFrequency = static_cast<float>(FMath::Clamp(Weak, 0.0, 1.0));
		Out.LowFrequency = static_cast<float>(FMath::Clamp(Strong, 0.0, 1.0));
		return Out;
	}

	void SurfaceTexture(uint8 Surface, double SpeedMps, double TimeS, double& InOutLow, double& InOutHigh)
	{
		const double V = FMath::Abs(SpeedMps);
		switch (Surface)
		{
		case Asphalt:
			// A light road hum that grows with speed.
			InOutHigh += 0.04 * FMath::SmoothStep(3.0, 30.0, V);
			break;
		case Concrete:
		{
			// A fine regular tick at each slab joint: speed / 4.5 m ticks a second, each a few hundredths of a second.
			const double Along = TimeS * V / ConcreteJointM;
			const bool bTick = FMath::Frac(Along) < FMath::Clamp(0.03 * V / ConcreteJointM, 0.05, 0.4);
			InOutHigh += 0.05 * FMath::SmoothStep(3.0, 30.0, V) + (bTick ? 0.35 * FMath::SmoothStep(1.0, 8.0, V) : 0.0);
			break;
		}
		case Gravel:
		case Dirt:
		{
			// Coarse and random: a new strength every 1/25 s, low motor, with a little grit on the high one.
			const uint32 Step = static_cast<uint32>(FMath::FloorToDouble(TimeS * 25.0));
			const double Noise = 0.45 + 0.55 * ((Step * 2654435761u >> 16) & 0xFF) / 255.0;
			const double Speed = FMath::SmoothStep(0.5, 6.0, V);
			InOutLow += 0.38 * Speed * Noise;
			InOutHigh += 0.10 * Speed;
			break;
		}
		case Grass:
			// A soft low wobble, about 4 Hz.
			InOutLow += 0.16 * FMath::SmoothStep(0.5, 6.0, V) * (0.5 + 0.5 * FMath::Sin(UE_DOUBLE_TWO_PI * 4.0 * TimeS));
			break;
		default:
			// In the air: nothing.
			break;
		}
	}

	double KerbPulse(double SinceS)
	{
		return SinceS >= 0.0 && SinceS < KerbPulseS ? KerbGain : 0.0;
	}

	FForceFeedbackValues ToChannels(const FSimCruiseRumble& Rumble, ESimCruiseRumbleBackend Backend)
	{
		FForceFeedbackValues Values;
		if (Backend == ESimCruiseRumbleBackend::GameInput)
		{
			Values.LeftLarge = Rumble.LowFrequency;
			Values.RightLarge = Rumble.HighFrequency;
			Values.LeftSmall = Rumble.LeftTrigger;
			Values.RightSmall = Rumble.RightTrigger;
		}
		else
		{
			// XInputInterface.cpp: left motor = max(LeftLarge, RightLarge),
			// right motor = max(LeftSmall, RightSmall); no trigger motors.
			Values.LeftLarge = Rumble.LowFrequency;
			Values.LeftSmall = Rumble.HighFrequency;
			Values.RightLarge = 0.f;
			Values.RightSmall = 0.f;
		}
		return Values;
	}
}

FSimCruiseRumble FSimCruiseHapticsModel::Sample(const FSimCruiseDriveTelemetry& Telemetry, double Throttle, double Brake, double DeltaTimeS)
{
	using namespace SimCruiseHaptics;
	double FrontSlip = 0.0;
	double RearSlip = 0.0;
	double SpinSlip = 0.0;   // driven wheels turning faster than the road
	double LockSlip = 0.0;   // any wheel turning slower under braking
	bAbsActive = false;
	// Loose ground is in the surface textures now (gravel, grass).
	const double Loose = 0.0;
	// The surface under the wheels on the ground (SurfaceOf), and kerb strikes by side.
	SurfaceTime += DeltaTimeS;
	double TexLow = 0.0, TexHigh = 0.0;
	int32 OnGround = 0;
	const double SpeedNow = Telemetry.BodyVelocityMps.Size();
	for (int32 I = 0; I < 4; ++I)
	{
		const FSimCruiseDriveWheelTelemetry& Wheel = Telemetry.Wheels[I];
		const uint8 Surface = SurfaceOf(Wheel);
		if (Surface != Air)
		{
			++OnGround;
			SurfaceTexture(Surface, SpeedNow, SurfaceTime + 0.37 * I, TexLow, TexHigh);
		}
		// A kerb: the wheel's flag when the build has it, else rough road under a wheel whose
		// suspension jumps. Felt once, as it starts.
		const double TravelRate = bHasLastTravel && DeltaTimeS > 0.0 ? FMath::Abs(Wheel.SuspensionTravelM - LastTravel[I]) / DeltaTimeS : 0.0;
		const bool bKerb = Wheel.bContact && KerbOf(Wheel, Telemetry.RoadRoughness >= KerbFrom && TravelRate >= KerbTravelMps);
		if (bKerb && !bOnKerb[I]) SinceKerb[I % 2] = 0.0;
		bOnKerb[I] = bKerb;
		LastTravel[I] = Wheel.SuspensionTravelM;
	}
	bHasLastTravel = true;
	for (double& Since : SinceKerb) Since += DeltaTimeS;
	// Two wheels on gravel are half the feel of four.
	TexLow *= 0.25;
	TexHigh *= 0.25;
	if (OnGround == 0)
	{
		// In the air: no rumble at all.
		for (double& S : Slip) S = 0.0;
		PulseTime = 0.0;
		return FSimCruiseRumble();
	}
	// A slip angle is only felt while the patch slides sideways, at about
	// v sin(alpha): below walking pace a relaxed angle left over from a slide
	// (or the noise of a near-zero velocity) is not scrub. Wheelspin is felt
	// from standstill, so the slip ratio is not gated.
	const double Speed = Telemetry.BodyVelocityMps.Size();
	const double LateralFeel = FMath::SmoothStep(LateralFeelFromMps, LateralFeelToMps, Speed);
	for (int32 I = 0; I < 4; ++I)
	{
		const FSimCruiseDriveWheelTelemetry& Wheel = Telemetry.Wheels[I];
		if (!Wheel.bContact || Wheel.NormalLoadN <= 0.0)
		{
			Slip[I] = 0.0;
			continue;
		}
		const double PeakAlpha = Wheel.PeakSlipAngleRad > 0.0 ? Wheel.PeakSlipAngleRad : DefaultPeakSlipAngleRad;
		Slip[I] = NormalizedSlip(Wheel.SlipRatio, Wheel.PeakSlipRatio, Wheel.SlipAngleRad * LateralFeel, PeakAlpha);
		const double Longitudinal = Wheel.SlipRatio / FMath::Max(Wheel.PeakSlipRatio, 0.001);
		if (I < 2)
		{
			FrontSlip = FMath::Max(FrontSlip, Slip[I]);
		}
		else
		{
			RearSlip = FMath::Max(RearSlip, Slip[I]);
		}
		if (Wheel.bDriven)
		{
			RearSlip = FMath::Max(RearSlip, FMath::Abs(Longitudinal));
			SpinSlip = FMath::Max(SpinSlip, Longitudinal);
		}
		LockSlip = FMath::Max(LockSlip, -Longitudinal);
		if (Telemetry.bServiceBrakeOn && Wheel.bAbsActive)
		{
			bAbsActive = true;
		}
	}
	PulseTime = bAbsActive ? PulseTime + DeltaTimeS : 0.0;

	const double Engine = Telemetry.bElectric ? 0.0 : FMath::Clamp(Telemetry.EngineLoad, 0.0, 1.0);
	const double Pulse = AbsPulse(bAbsActive, PulseTime);
	FSimCruiseRumble Out = Motors(FrontSlip, RearSlip, Pulse, Telemetry.RoadRoughness, Loose, Speed, Engine, bAbsActive);
	// The surface, and a kerb on its side: the left wheels on the left (low) motor, the right on the right (high) one.
	Out.LowFrequency = static_cast<float>(FMath::Clamp(Out.LowFrequency + TexLow + KerbPulse(SinceKerb[0] - DeltaTimeS), 0.0, 1.0));
	Out.HighFrequency = static_cast<float>(FMath::Clamp(Out.HighFrequency + TexHigh + KerbPulse(SinceKerb[1] - DeltaTimeS), 0.0, 1.0));

	// Impulse triggers: the pedal under the foot that is losing the tyre.
	if (Throttle > 0.05)
	{
		Out.RightTrigger = static_cast<float>(FMath::Clamp(TriggerGain * SlipFeel(SpinSlip), 0.0, 1.0));
	}
	if (Brake > 0.05)
	{
		Out.LeftTrigger = static_cast<float>(FMath::Clamp(bAbsActive ? Pulse : TriggerGain * SlipFeel(LockSlip), 0.0, 1.0));
	}
	return Out;
}

void FSimCruiseHapticsModel::Quiet()
{
	for (double& S : Slip)
	{
		S = 0.0;
	}
	PulseTime = 0.0;
	bAbsActive = false;
	bHasLastTravel = false;
	for (bool& K : bOnKerb) K = false;
	SinceKerb[0] = SinceKerb[1] = 1e9;
}
