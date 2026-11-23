// Copyright OSDS contributors. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * The drive robot's throttle law (test tool, not car physics): the pedal a careful driver gives.
 * Pure function so a headless test can pin it (bug #102: full throttle in 2nd on gravel spun the
 * driven wheels to a slip ratio of 2.8, took the rear axle's side force away and the car swapped ends).
 */
struct FOsdsRobotThrottleInput
{
	/** Wanted speed minus speed, m/s. */
	double SpeedErrorMps = 0.0;
	/** Largest |slip ratio| of the driven wheels in contact. */
	double DrivenSlipRatio = 0.0;
	/** Slip ratio where a tyre's longitudinal force peaks (about 0.1). */
	double PeakSlipRatio = 0.1;
	/** Largest |slip angle| of the rear wheels, degrees. */
	double RearSlipAngleDeg = 0.0;
	/** Lowest surface grip multiplier under the driven wheels (1 paved, about 0.7 gravel). */
	double SurfaceGrip = 1.0;
	/** Stick deflection now, -1..1: a car turning hard has less grip left to drive with. */
	double Stick = 0.0;
	/** Pedal given last tick, and the tick length, s. */
	double PreviousThrottle = 0.0;
	double DeltaSeconds = 0.0;
};

namespace OsdsRobotThrottle
{
	/** The pedal: the speed error's share, eased for the surface, the driven wheels' spin, the rear
	 *  axle's slide and the steering, and opened no faster than a foot moves (1.5 per second). */
	inline double Compute(const FOsdsRobotThrottleInput& In)
	{
		double Want = In.SpeedErrorMps > 0.0 ? FMath::Clamp(0.2 + 0.35 * In.SpeedErrorMps, 0.0, 1.0) : 0.0;
		// Loose or low-grip ground takes a smaller share of the pedal (1 paved, about 0.8 on gravel).
		const double Grip = FMath::Clamp(In.SurfaceGrip, 0.3, 1.0);
		Want *= 0.3 + 0.7 * Grip * Grip;
		// Wheelspin: past the slip ratio of the peak, lift in proportion (to a quarter at 3x the peak).
		const double Peak = FMath::Max(In.PeakSlipRatio, 0.03);
		const double Spin = FMath::Max(0.0, FMath::Abs(In.DrivenSlipRatio) / Peak - 1.2);
		Want *= FMath::Clamp(1.0 - 0.4 * Spin, 0.0, 1.0);
		// The rear sliding: lift from 3 degrees, off at 7.
		Want *= FMath::Clamp(1.0 - (FMath::Abs(In.RearSlipAngleDeg) - 3.0) / 4.0, 0.0, 1.0);
		// Never full throttle with the wheel well over: up to 60 % off at full stick.
		Want *= 1.0 - 0.6 * FMath::Max(0.0, FMath::Abs(In.Stick) - 0.3) / 0.7;
		// A foot opens the pedal gradually and lifts at once.
		const double Opened = In.PreviousThrottle + 1.5 * FMath::Max(In.DeltaSeconds, 0.0);
		return FMath::Clamp(FMath::Min(Want, Opened), 0.0, 1.0);
	}
}
