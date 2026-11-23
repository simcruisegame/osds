// Copyright OSDS contributors. All Rights Reserved.

#include "Vehicle/OsdsDifferential.h"

#include "Math/UnrealMathUtility.h"

namespace
{
	/** Torque-sensing limited slip between two wheels (bias 1 = open). */
	TPair<double, double> SplitAxleTorque(
		double AxleTorqueNm,
		double LeftWheelAngularSpeed,
		double RightWheelAngularSpeed,
		double WheelInertiaKgM2,
		double DeltaTimeS,
		bool bIsDriveTorque,
		double BiasRatio)
	{
		const double HalfTorqueNm = AxleTorqueNm * 0.5;
		// The caller passes the drive or the coast bias: a torque-sensing (Torsen) differential biases
		// engine braking as well as drive.
		if (DeltaTimeS <= 0.0 || BiasRatio <= 1.0)
		{
			return TPair<double, double>(HalfTorqueNm, HalfTorqueNm);
		}

		const double TransferLimitNm = FMath::Abs(HalfTorqueNm) * (BiasRatio - 1.0) / (BiasRatio + 1.0);
		const double EqualizingTorqueNm = (RightWheelAngularSpeed - LeftWheelAngularSpeed) *
			WheelInertiaKgM2 / (2.0 * DeltaTimeS);
		const double TransferNm = FMath::Clamp(EqualizingTorqueNm, -TransferLimitNm, TransferLimitNm);
		return TPair<double, double>(HalfTorqueNm + TransferNm, HalfTorqueNm - TransferNm);
	}
}

void FOsdsDifferential::Configure(const FOsdsCarDefinition& Car)
{
	Layout = Car.DrivetrainLayout == TEXT("fwd") ? ELayout::Fwd
		: Car.DrivetrainLayout == TEXT("awd") ? ELayout::Awd : ELayout::Rwd;
	WheelInertiaKgM2 = Car.WheelRotationalInertiaKgM2;
	AwdFrontShare = Car.AwdFrontTorqueShareFraction;
	FrontBias = Car.FrontDifferentialBiasRatio;
	RearBias = Car.RearDifferentialBiasRatio;
	RearCoastBias = Car.RearDifferentialCoastBiasRatio;
	CenterBias = Car.CenterDifferentialBiasRatio;
	Reset();
}

void FOsdsDifferential::Reset()
{
	// No per-car state yet; active lock and coupling kinds keep theirs here.
}

void FOsdsDifferential::Step(const FOsdsDifferentialInput& Input, FOsdsDifferentialOutput& Output)
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Output.WheelTorqueNm[Index] = 0.0;
		Output.WheelBrakeNm[Index] = 0.0;
	}
	const double* W = Input.WheelOmegaRadPerSecond;
	const double AxleTorqueNm = Input.AxleTorqueNm;
	const double DeltaTimeS = Input.DeltaTimeS;
	const bool bIsDriveTorque = Input.bDrive;
	const double I = WheelInertiaKgM2;

	if (Layout == ELayout::Fwd)
	{
		const TPair<double, double> Front = SplitAxleTorque(AxleTorqueNm, W[0], W[1], I, DeltaTimeS, bIsDriveTorque,
			bIsDriveTorque ? FrontBias : 1.0);
		Output.WheelTorqueNm[0] = Front.Key;
		Output.WheelTorqueNm[1] = Front.Value;
		return;
	}

	double RearTorqueNm = AxleTorqueNm;
	if (Layout == ELayout::Awd)
	{
		double FrontTorqueNm = AxleTorqueNm * AwdFrontShare;
		RearTorqueNm = AxleTorqueNm * (1.0 - AwdFrontShare);
		if (bIsDriveTorque && DeltaTimeS > 0.0 && CenterBias > 1.0)
		{
			// Limited-slip centre: torque moves toward the slower axle within the bias.
			const double TransferLimitNm = FMath::Abs(AxleTorqueNm) * 0.5 *
				(CenterBias - 1.0) / (CenterBias + 1.0);
			const double AxleSpeedDifference = (W[2] + W[3] - W[0] - W[1]) * I / DeltaTimeS;
			const double TransferNm = FMath::Clamp(AxleSpeedDifference, -TransferLimitNm, TransferLimitNm);
			FrontTorqueNm += TransferNm;
			RearTorqueNm -= TransferNm;
		}

		const TPair<double, double> Front = SplitAxleTorque(FrontTorqueNm, W[0], W[1], I, DeltaTimeS, bIsDriveTorque,
			bIsDriveTorque ? FrontBias : 1.0);
		Output.WheelTorqueNm[0] = Front.Key;
		Output.WheelTorqueNm[1] = Front.Value;
	}

	const TPair<double, double> Rear = SplitAxleTorque(RearTorqueNm, W[2], W[3], I, DeltaTimeS, bIsDriveTorque,
		bIsDriveTorque ? RearBias : RearCoastBias);
	Output.WheelTorqueNm[2] = Rear.Key;
	Output.WheelTorqueNm[3] = Rear.Value;
}

double FOsdsDifferential::DrivenOmega(const double W[4]) const
{
	if (Layout == ELayout::Fwd)
	{
		return (W[0] + W[1]) * 0.5;
	}
	if (Layout == ELayout::Awd)
	{
		const double Share = AwdFrontShare;
		return Share * (W[0] + W[1]) * 0.5 + (1.0 - Share) * (W[2] + W[3]) * 0.5;
	}
	return (W[2] + W[3]) * 0.5;
}
