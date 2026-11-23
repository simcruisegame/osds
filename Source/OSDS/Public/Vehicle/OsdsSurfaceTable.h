// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/OsdsSurfaceKind.h"

/**
 * What a surface does to a tyre: the ONE table (bug #102 / ticket #114). The world tile reads the grip
 * from it, the tyre model the curve shape, the sim the rolling resistance, and the audio tickets the
 * same EOsdsSurfaceKind key. Asphalt, Concrete and Unknown are exactly the old behaviour (grip 1.0,
 * shape and resistance multipliers 1.0), so every paved Bench is unchanged.
 */
struct FOsdsSurfaceTyre
{
	/** Peak friction relative to dry asphalt (the car's own mu). */
	double DryGrip = 1.0;
	/** Share of the grip lost at full wetness. */
	double WetLoss = 0.3;
	/** Multiplies the slip where the tyre curve peaks and where it reaches full slide (loose ground
	 *  shears over more travel: the peak comes at higher slip). */
	double PeakSlipScale = 1.0;
	/** Share of the car's peak-to-slide force drop that is kept (1 = the car's own curve, 0 = no
	 *  fall-off after the peak: loose ground's shear stress rises to and holds its maximum). */
	double FalloffKept = 1.0;
	/** Rolling resistance relative to the car's own coefficient (the car's value is its paved one, 0.012 typical). */
	double RollingResistanceScale = 1.0;
};

/** The curve-shape part, what the tyre solver takes. Identity (1, 1) leaves a car's curve untouched. */
struct FOsdsSurfaceShape
{
	double PeakSlipScale = 1.0;
	double FalloffKept = 1.0;
	bool IsIdentity() const { return PeakSlipScale == 1.0 && FalloffKept == 1.0; }
};

namespace OsdsSurface
{
	/**
	 * Sources, per surface (grip relative to dry asphalt 0.85, J.Y. Wong, Theory of Ground Vehicles 4th ed.,
	 * table 1.3 peak/sliding friction; paved Cr 0.012 as the car files):
	 *  - Asphalt, Concrete, Unknown: the baseline.
	 *  - Gravel: Wong table 1.3 "gravel" peak 0.6 -> 0.6/0.85 = 0.7; loose gravel over a firm road
	 *    reads 0.4-0.6, so 0.65 (estimate within the range). Wet loss 0.1 (Wong: little change wet).
	 *    Cr 0.03: rolled gravel road 0.02, unpaved road 0.05 (Wong; Gillespie, Fundamentals of Vehicle
	 *    Dynamics, rolling resistance table) -> 0.03 / 0.012 = 2.5. Shape: Wong ch.1 shear of loose
	 *    material rises with displacement to a plateau (Janosi-Hanamoto), so the peak comes later
	 *    (1.3x slip, estimate) and the fall-off is a quarter of a paved tyre's (estimate).
	 *  - Grass: dry turf over soil, tyre mu 0.35-0.55 (Wong ch.1 field values for turf and soil):
	 *    0.5/0.85 = 0.55 (estimate; below gravel). Wet loss 0.3 (estimate: wet turf is slick, below
	 *    Wong's earth road 0.19). Cr 0.08 (Gillespie, medium-hard soil) = 6.7x. Shape: 1.5x slip,
	 *    fall-off kept 0.4 (estimates, same Janosi-Hanamoto reasoning).
	 *  - Dirt: Wong table 1.3 earth road dry 0.68 -> 0.8, wet 0.55 -> loss 0.19; Cr 0.05 (unpaved
	 *    road, Wong) = 4.2x; shape 1.2x slip and fall-off kept 0.5 (estimates). Not yet fed by the world.
	 */
	inline FOsdsSurfaceTyre Tyre(EOsdsSurfaceKind Kind)
	{
		FOsdsSurfaceTyre T;
		switch (Kind)
		{
		case EOsdsSurfaceKind::Gravel:
			T.DryGrip = 0.65; T.WetLoss = 0.1; T.PeakSlipScale = 1.3; T.FalloffKept = 0.25; T.RollingResistanceScale = 2.5;
			break;
		case EOsdsSurfaceKind::Grass:
			T.DryGrip = 0.55; T.WetLoss = 0.3; T.PeakSlipScale = 1.5; T.FalloffKept = 0.4; T.RollingResistanceScale = 6.7;
			break;
		case EOsdsSurfaceKind::Dirt:
			T.DryGrip = 0.8; T.WetLoss = 0.19; T.PeakSlipScale = 1.2; T.FalloffKept = 0.5; T.RollingResistanceScale = 4.2;
			break;
		default:
			break; // Unknown, Asphalt, Concrete
		}
		return T;
	}
	inline FOsdsSurfaceTyre Tyre(uint8 Kind) { return Tyre(static_cast<EOsdsSurfaceKind>(Kind)); }
	inline FOsdsSurfaceShape Shape(uint8 Kind)
	{
		const FOsdsSurfaceTyre T = Tyre(Kind);
		return { T.PeakSlipScale, T.FalloffKept };
	}
	/** Grip under a tyre at a wetness 0..1, relative to dry asphalt. */
	inline double Grip(uint8 Kind, double Wetness)
	{
		const FOsdsSurfaceTyre T = Tyre(Kind);
		return T.DryGrip * (1.0 - T.WetLoss * Wetness);
	}
}
