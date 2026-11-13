// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockContainment.h"

/** The push starts Margin cm inside each face */
static void AccumulateInsideMargin(const FFlockContainmentBox& Box, const FFlockAgentView& Agent, FFlockSteering& Out)
{
	const FVector3f Local = Box.InverseRotation.RotateVector(Agent.Pos - Box.Center);

	FVector3f Push = FVector3f::ZeroVector;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		// a margin wider than the box would push from the middle
		const float AxisMargin = FMath::Min(Box.Margin, Box.Extent[Axis]);
		if (AxisMargin <= 0.f)
		{
			continue;
		}

		const float ToFace = Box.Extent[Axis] - FMath::Abs(Local[Axis]);
		if (ToFace < AxisMargin)
		{
			const float Strength = FMath::Min(1.f - ToFace / AxisMargin, 1.f);
			Push[Axis] = (Local[Axis] > 0.f ? -Strength : Strength);
		}
	}

	if (!Push.IsNearlyZero())
	{
		Out.External += Box.Rotation.RotateVector(Push.GetClampedToMaxSize(1.f)) * Box.Weight;
	}
}

/** A force against the heading never turns a constant-speed agent, so add a sideways part */
static FVector3f AddTurnSide(const FVector3f& Force, const FVector3f& Heading)
{
	const float ForceSize = Force.Size();
	if (ForceSize < 1e-6f || FVector3f::DotProduct(Force, Heading) >= 0.f)
	{
		return Force;
	}

	const FVector3f Sideways = Force - Heading * FVector3f::DotProduct(Force, Heading);
	if (Sideways.Size() >= 0.5f * ForceSize)
	{
		return Force;
	}

	// the side the force leans to, else the heading's right
	FVector3f Side = Sideways.GetSafeNormal(1e-4f, FVector3f::ZeroVector);
	if (Side.IsZero())
	{
		Side = FVector3f::CrossProduct(FVector3f::UpVector, Heading).GetSafeNormal(1e-4f, FVector3f::ZeroVector);
		if (Side.IsZero())
		{
			// straight up or down: a fixed side so every agent turns the same way
			Side = FVector3f::YAxisVector;
		}
	}
	return Force + Side * (0.5f * ForceSize);
}

/** Nothing inside the box, a turn back that grows over RampDistance outside it */
static void AccumulateSoftReturn(const FFlockContainmentBox& Box, const FFlockAgentView& Agent, FFlockSteering& Out)
{
	const FVector3f Back = FlockContainment::ToBox(Box, Agent.Pos);

	const float Distance = Back.Size();
	if (Distance <= FlockContainment::OutsideEpsilon)
	{
		return;
	}

	const float Overflow = FMath::Clamp(Distance / Box.RampDistance, 0.f, 1.f);
	const FVector3f Force = (Back / Distance) * (Box.TurnStrength * Overflow);
	Out.External += AddTurnSide(Force, Agent.Heading);
}

FVector3f FlockContainment::ToBox(const FFlockContainmentBox& Box, const FVector3f& Pos)
{
	const FVector3f Local = Box.InverseRotation.RotateVector(Pos - Box.Center);
	const FVector3f Closest = Local.BoundToBox(-Box.Extent, Box.Extent);
	return Box.Rotation.RotateVector(Closest - Local);
}

bool FlockContainment::IsOutside(const FFlockContainmentBox& Box, const FVector3f& Pos)
{
	return ToBox(Box, Pos).SizeSquared() > OutsideEpsilon * OutsideEpsilon;
}

void FlockContainment::AccumulateSphere(const FFlockContainmentSphere& Sphere, const FFlockAgentView& Agent, FFlockSteering& Out)
{
	const FVector3f Offset = Agent.Pos - Sphere.Center;
	const float Distance = Offset.Size();
	if (Distance < 1e-3f)
	{
		return;
	}

	// the body stays inside and the push starts Margin before that
	const float Reach = FMath::Max(Sphere.Radius - Agent.BodyRadius, 1.f);
	const float Inner = FMath::Max(Reach - Sphere.Margin, 0.f);
	if (Distance <= Inner)
	{
		return;
	}

	const float Strength = FMath::Min((Distance - Inner) / FMath::Max(Sphere.Margin, 1.f), 2.f);
	const FVector3f Force = (-Offset / Distance) * (Sphere.Weight * Strength);
	Out.External += AddTurnSide(Force, Agent.Heading);
}

void FlockContainment::Accumulate(const FFlockContainmentBox& Box, const FFlockAgentView& Agent, FFlockSteering& Out)
{
	if (Box.Mode == EFlockContainmentModeView::SoftReturn)
	{
		AccumulateSoftReturn(Box, Agent, Out);
	}
	else
	{
		AccumulateInsideMargin(Box, Agent, Out);
	}
}
