// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockBehavior_Core.h"

void UFlockBehavior_Core::Accumulate(const FFlockFrameContext& Context, const FFlockAgentView& Agent,
	const FFlockNeighbors& Neighbors, FFlockSteering& Out) const
{
	FVector3f Separation = FVector3f::ZeroVector;
	FVector3f VelocitySum = FVector3f::ZeroVector;
	FVector3f PositionSum = FVector3f::ZeroVector;
	int32 NumNeighbors = 0;

	for (int32 Index = 0; Index < Neighbors.NumFlockmates; ++Index)
	{
		const FFlockNeighbor& Other = Neighbors.Flockmates[Index];
		const float Bodies = Agent.BodyRadius + Other.BodyRadius;
		const float Dist = FMath::Sqrt(Other.DistSq);

		// separation sees every gathered agent, MaxNeighbors only caps alignment and cohesion
		const float SeparationReach = SeparationRadius + Bodies;
		if (Dist < SeparationReach)
		{
			FVector3f Away;
			if (Dist > 1e-2f)
			{
				Away = (Agent.Pos - Other.Pos) / Dist;
			}
			else
			{
				// on top of each other: a per-agent direction so the pair splits
				const float Seed = float(Agent.Slot);
				Away = FVector3f(FMath::Sin(Seed * 12.9898f), FMath::Cos(Seed * 78.233f), FMath::Sin(Seed * 37.719f)).GetSafeNormal();
			}

			// squared falloff so agents at the spacing border do not vibrate
			const float Strength = 1.f - Dist / SeparationReach;
			Separation += Away * (Strength * Strength);
		}

		if (NumNeighbors < Agent.MaxNeighbors && Dist <= Agent.NeighborRadius + Bodies)
		{
			VelocitySum += Other.Vel;
			PositionSum += Other.Pos;
			++NumNeighbors;
		}
	}

	if (NumNeighbors == 0)
	{
		return;
	}

	const float InvNum = 1.f / float(NumNeighbors);

	const FVector3f Alignment = (VelocitySum * InvNum).GetSafeNormal() * AlignmentWeight;

	// proportional to the distance to the centre, so it does not overshoot
	const FVector3f ToCentre = PositionSum * InvNum - Agent.Pos;
	const FVector3f Cohesion = (ToCentre / Agent.NeighborRadius).GetClampedToMaxSize(1.f) * CohesionWeight;

	Out.Steer += Alignment + Cohesion + Separation.GetClampedToMaxSize(1.f) * SeparationWeight;
}
