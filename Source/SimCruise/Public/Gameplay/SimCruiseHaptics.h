// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "GenericPlatform/IInputInterface.h"

struct FSimCruiseCarDefinition;

/** The four Xbox motors, 0..1 before the player's strength preference. */
struct FSimCruiseRumble
{
	/** Left body motor, low frequency ("strong"): rear tyres, wheelspin, ABS, kerbs. */
	float LowFrequency = 0.f;
	/** Right body motor, high frequency ("weak"): front scrub, road texture, gravel, engine. */
	float HighFrequency = 0.f;
	/** Impulse trigger motors (GameInput only; XInput has no trigger motors). */
	float LeftTrigger = 0.f;
	float RightTrigger = 0.f;

	bool IsQuiet() const { return LowFrequency <= 0.001f && HighFrequency <= 0.001f && LeftTrigger <= 0.001f && RightTrigger <= 0.001f; }
};

/** How Unreal's four force-feedback channels reach the pad. XInput sends
 *  max(LeftLarge, RightLarge) to the low motor and max(LeftSmall, RightSmall)
 *  to the high one; GameInput maps LeftLarge low, RightLarge high and the two
 *  Small channels to the trigger motors. */
enum class ESimCruiseRumbleBackend : uint8
{
	XInput,
	GameInput,
};

/**
 * Grip you can feel, ported from feel/slip-haptics-audio:game/Haptics.gd.
 * Read-only: it reads the vehicle's tyre state after the physics step and adds
 * no force. Normalised slip per tyre, 1 = the end of the full-grip plateau:
 *   s = sqrt((kappa/kappa*)^2 + (alpha/alpha*)^2)
 * Slip rather than friction-circle use, because the lateral law saturates and
 * falls past its peak: a front tyre sliding at 25 deg uses less of its circle
 * than one at the peak, so a use-based cue would fade as understeer grows.
 * Motors: high (weak) = front tyres, broken road, loose surface, engine load;
 * low (strong) = rear tyres and driven-wheel longitudinal slip, the 17 Hz ABS
 * pulse, a kerb under a left wheel, coarse surfaces (a kerb under a right wheel is a sharp pulse on the high motor). New for Unreal: the impulse triggers carry the
 * pedals' own story, right = driven-wheel spin under throttle, left = a
 * locking wheel under braking or the ABS pulse. Coasting straight on smooth
 * asphalt only a light road hum remains; in the air nothing.
 */
namespace SimCruiseHaptics
{
	constexpr double Plateau = 0.98;
	/** Slip is felt from 80 % of the peak (the tyre is still gripping, the edge is near). */
	constexpr double SlipOnset = 0.8;
	constexpr double SlipLimit = 1.15;
	constexpr double SlipSlide = 2.5;
	constexpr double LimitShare = 0.65;
	/** The ABS pump and valves: a 17 Hz pulse on the main motors (15-20 Hz feels like the pedal's). */
	constexpr double AbsHz = 17.0;
	constexpr double AbsOn = 0.6;
	constexpr double AbsSlipShare = 0.3;
	/** A kerb strike: a sharp pulse this strong and this long on the motor of the wheel's side. */
	constexpr double KerbGain = 0.85;
	constexpr double KerbPulseS = 0.06;
	/** Without a per-wheel kerb flag: a kerb is road this rough under a wheel whose suspension moves this fast. */
	constexpr double KerbFrom = 0.45;
	constexpr double KerbTravelMps = 0.4;
	/** Concrete slabs: a joint every 4.5 m, ticked on the fine (high) motor. */
	constexpr double ConcreteJointM = 4.5;
	constexpr double TextureGain = 0.2;
	constexpr double TextureFrom = 0.15;
	constexpr double TextureTo = 0.7;
	constexpr double LooseGain = 0.25;
	constexpr double EngineGain = 0.06;
	constexpr double TriggerGain = 0.8;
	constexpr double DefaultPeakSlipAngleRad = 0.18;
	/** Body speed (m/s) over which slip angles fade in as felt scrub. */
	constexpr double LateralFeelFromMps = 0.5;
	constexpr double LateralFeelToMps = 2.0;

	/** Slip angle (rad) where the definition's lateral law
	 *  tanh(alpha k)(1 - falloff smoothstep(from, to, alpha)) first reaches 98 %
	 *  of its maximum (10.6 deg on the 1999 roadster). For vehicles to report as
	 *  FSimCruiseDriveWheelTelemetry::PeakSlipAngleRad. */
	SIMCRUISE_API double PlateauSlipAngleRad(const FSimCruiseCarDefinition& Car);

	SIMCRUISE_API double NormalizedSlip(double Kappa, double PeakKappa, double Alpha, double PeakAlpha);

	/** 0 below the onset, LimitShare at the plateau, 1 deep in a slide; never falls as slip grows. */
	SIMCRUISE_API double SlipFeel(double S);

	/** AbsHz square pulse while ABS acts: pump and valves, felt through the pedal. */
	SIMCRUISE_API double AbsPulse(bool bActive, double TimeS);

	/** The two body motors from the summarised state (Haptics.gd motors()). */
	SIMCRUISE_API FSimCruiseRumble Motors(double FrontSlip, double RearSlip, double Pulse, double Roughness, double Loose,
		double SpeedMps, double EngineLoad, bool bAbsOn);

	/** Write a rumble into Unreal's channels for the given backend. */
	SIMCRUISE_API FForceFeedbackValues ToChannels(const FSimCruiseRumble& Rumble, ESimCruiseRumbleBackend Backend);

	/**
	 * What a tyre stands on, as ESimCruiseSurfaceKind (Vehicle/SimCruiseSurfaceKind.h, the driving
	 * lane's): 0 in the air, 1 asphalt, 2 concrete, 3 gravel, 4 grass, 5 dirt.
	 */
	enum ESurface : uint8 { Air = 0, Asphalt = 1, Concrete = 2, Gravel = 3, Grass = 4, Dirt = 5 };

	/**
	 * One wheel's surface texture this frame, added to the low (coarse) and high (fine) motors:
	 * asphalt a light hum with speed; concrete a fine joint tick every ConcreteJointM; gravel and
	 * dirt a coarse random low rumble; grass a soft low wobble; nothing in the air. Each is 0 at
	 * a standstill. TimeS is a running clock (the pattern moves with it).
	 */
	SIMCRUISE_API void SurfaceTexture(uint8 Surface, double SpeedMps, double TimeS, double& InOutLow, double& InOutHigh);

	/** The kerb pulse SinceS seconds after the strike: KerbGain for KerbPulseS, then nothing. */
	SIMCRUISE_API double KerbPulse(double SinceS);

	/**
	 * The surface and the kerb flag of a wheel. The per-wheel fields (SurfaceKind, bKerb) come
	 * with the driving lane's robot-curves branch; until they are in this build the surface
	 * comes from the grip (about 1.0 paved: asphalt; lower: loose, felt as gravel) and the kerb
	 * from the fallback. When the fields exist these read them, with no change here.
	 */
	template <typename TWheel>
	uint8 SurfaceOf(const TWheel& Wheel)
	{
		if (!Wheel.bContact) return Air;
		// The wheel's own surface when it reports one; a wheel on the ground whose surface is not
		// known (Unknown, 0) is judged by its grip, never taken for one in the air.
		if constexpr (requires { Wheel.SurfaceKind; }) { if (static_cast<uint8>(Wheel.SurfaceKind) != Air) return static_cast<uint8>(Wheel.SurfaceKind); }
		return Wheel.SurfaceGrip >= 0.92 ? Asphalt : Gravel;
	}
	template <typename TWheel>
	bool KerbOf(const TWheel& Wheel, bool bFallback)
	{
		// The wheel's kerb flag, and the rough-road jump as before (a kerb the flag misses is still felt).
		if constexpr (requires { Wheel.bKerb; }) return Wheel.bKerb || bFallback;
		else return bFallback;
	}
}

/** Per-frame sampler: holds the ABS pulse clock. */
class SIMCRUISE_API FSimCruiseHapticsModel
{
public:
	/** Compute this frame's motors from telemetry. Throttle and brake are the
	 *  pedal positions sent this frame (used by the trigger motors). */
	FSimCruiseRumble Sample(const FSimCruiseDriveTelemetry& Telemetry, double Throttle, double Brake, double DeltaTimeS);
	void Quiet();

	bool IsAbsActive() const { return bAbsActive; }
	/** A shift was refused (the gearbox's message): two short buzzes on the next samples. */
	void NotifyShiftRefused() { SinceRefusedS = 0.0; }
	/** Normalised slip per tyre from the last sample. */
	double GetWheelSlip(int32 Index) const { return Slip[FMath::Clamp(Index, 0, 3)]; }

private:
	double Slip[4] = {0.0, 0.0, 0.0, 0.0};
	double PulseTime = 0.0;
	bool bAbsActive = false;
	/** The surface textures' clock, each wheel's last suspension travel and kerb state, and the time since each side's last kerb strike. */
	double SurfaceTime = 0.0;
	double LastTravel[4] = {0.0, 0.0, 0.0, 0.0};
	bool bOnKerb[4] = {false, false, false, false};
	bool bHasLastTravel = false;
	double SinceKerb[2] = {1e9, 1e9};
	/** Manual shifts: the last gear seen, time since the last change and its direction, time since a refusal. */
	int32 LastGear = MIN_int32;
	double SinceShiftS = 1e9;
	int32 ShiftDirection = 0;
	double SinceRefusedS = 1e9;
};
