// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FlockBehavior.h"
#include "GameplayTagContainer.h"
#include "FlockObstacleAvoidance.generated.h"

/** Reynolds' obstacle avoidance for spheres */
UCLASS(meta = (DisplayName = "Obstacle Avoidance (spheres, by tag)"))
class TGJFLYING_API UFlockObstacleAvoidance : public UFlockBehavior
{
	GENERATED_BODY()

public:

	/** Obstacles with one of these tags (or a child of one) are avoided */
	UPROPERTY(EditAnywhere, Category="Obstacle Avoidance")
	FGameplayTagContainer AvoidTags;

	/** How far ahead an agent looks, in seconds of flight */
	UPROPERTY(EditAnywhere, Category="Obstacle Avoidance", meta = (ClampMin = 0.1, Units = "s"))
	float LookAheadTime = 1.5f;

	/** Strength of the sideways push at the closest */
	UPROPERTY(EditAnywhere, Category="Obstacle Avoidance", meta = (ClampMin = 0))
	float Weight = 4.f;

	/** Extra room kept from the sphere surface, besides the body radius */
	UPROPERTY(EditAnywhere, Category="Obstacle Avoidance", meta = (ClampMin = 0, Units = "cm"))
	float Margin = 100.f;

	virtual void PrepareFrame(FFlockFrameContext& Context) override;

	virtual void Accumulate(const FFlockFrameContext& Context, const FFlockAgentView& Agent,
		const FFlockNeighbors& Neighbors, FFlockSteering& Out) const override;

private:

	/** Obstacles picked by tag in PrepareFrame, read by the workers */
	struct FSphere
	{
		FVector3f Center;
		float Radius;
	};
	TArray<FSphere> Spheres;

	int32 LoggedObstacles = -1;
	int32 LoggedAvoided = -1;
};
