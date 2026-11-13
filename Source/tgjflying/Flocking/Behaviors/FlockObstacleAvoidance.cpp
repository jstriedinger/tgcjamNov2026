// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockObstacleAvoidance.h"
#include "FlockSpatialHash.h"

void UFlockObstacleAvoidance::PrepareFrame(FFlockFrameContext& Context)
{
	Spheres.Reset();
	if (!AvoidTags.IsEmpty())
	{
		for (const FFlockObstacleSource& Source : Context.Obstacles)
		{
			// a child tag matches its parent
			bool bAvoided = false;
			for (const FGameplayTag& Tag : Source.Tags)
			{
				if (Tag.MatchesAny(AvoidTags))
				{
					bAvoided = true;
					break;
				}
			}
			if (bAvoided)
			{
				Spheres.Add({ Source.Center, Source.Radius });
			}
		}
	}

	// logs only when the counts change
	if (Context.Obstacles.Num() != LoggedObstacles || Spheres.Num() != LoggedAvoided)
	{
		LoggedObstacles = Context.Obstacles.Num();
		LoggedAvoided = Spheres.Num();
		UE_LOG(LogFlock, Log, TEXT("Obstacle avoidance: %d obstacles in the level, %d avoided (avoid tags [%s])"), LoggedObstacles, LoggedAvoided, *AvoidTags.ToStringSimple());
	}
}

void UFlockObstacleAvoidance::Accumulate(const FFlockFrameContext& Context, const FFlockAgentView& Agent,
	const FFlockNeighbors& Neighbors, FFlockSteering& Out) const
{
	if (Spheres.Num() == 0)
	{
		return;
	}

	const float Speed = FMath::Max(Agent.Vel.Size(), Agent.Speed);
	const float Length = FMath::Max(Speed * LookAheadTime, 1.f);

	bool bFound = false;
	float BestForward = TNumericLimits<float>::Max();
	FVector3f BestAway = FVector3f::ZeroVector;
	float BestStrength = 0.f;

	for (const FSphere& Sphere : Spheres)
	{
		const FVector3f ToCenter = Sphere.Center - Agent.Pos;
		const float Reach = Sphere.Radius + Agent.BodyRadius + Margin;

		// a bird inside the sphere (a prison bird) is kept in by its cage
		if (ToCenter.SizeSquared() < Sphere.Radius * Sphere.Radius)
		{
			continue;
		}

		// touching the sphere: leave straight away from the centre
		const float Touch = Sphere.Radius + Agent.BodyRadius;
		if (ToCenter.SizeSquared() < Touch * Touch)
		{
			Out.External += (-ToCenter).GetSafeNormal(1e-4f, -Agent.Heading) * Weight;
			continue;
		}

		const float Forward = FVector3f::DotProduct(ToCenter, Agent.Heading);
		if (Forward <= 0.f || Forward - Reach > Length)
		{
			continue;
		}

		// sideways offset of the centre from our path
		const FVector3f Lateral = ToCenter - Agent.Heading * Forward;
		const float LateralDistance = Lateral.Size();
		if (LateralDistance >= Reach)
		{
			continue;
		}

		// only the nearest obstacle in front counts
		if (Forward >= BestForward)
		{
			continue;
		}
		BestForward = Forward;
		bFound = true;

		if (LateralDistance > 1e-2f)
		{
			BestAway = -Lateral / LateralDistance;
		}
		else
		{
			BestAway = FVector3f::CrossProduct(Agent.Heading, FVector3f::ZAxisVector);
			if (BestAway.SizeSquared() < 1e-4f)
			{
				BestAway = FVector3f::YAxisVector;
			}
			BestAway.Normalize();
		}

		// stronger when closer and when the path goes through the middle
		const float Gap = FMath::Max(Forward - Reach, 0.f);
		const float Urgency = 1.f - FMath::Clamp(Gap / Length, 0.f, 1.f);
		const float Centered = 1.f - LateralDistance / Reach;
		BestStrength = Urgency * (0.5f + 0.5f * Centered);
	}

	if (bFound)
	{
		Out.External += BestAway * (BestStrength * Weight);
	}
}
