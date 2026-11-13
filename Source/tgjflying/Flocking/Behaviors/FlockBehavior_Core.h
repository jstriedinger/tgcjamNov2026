// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FlockBehavior.h"
#include "FlockBehavior_Core.generated.h"

/** Reynolds boids in 3D: separation, alignment and cohesion */
UCLASS(meta = (DisplayName = "Core (Separation / Alignment / Cohesion)"))
class TGJFLYING_API UFlockBehavior_Core : public UFlockBehavior
{
	GENERATED_BODY()

public:

	/** Neighbors closer than this (plus both body radii) push the agent away */
	UPROPERTY(EditAnywhere, Category="Core", meta = (ClampMin = 0, Units = "cm"))
	float SeparationRadius = 160.f;

	UPROPERTY(EditAnywhere, Category="Core", meta = (ClampMin = 0))
	float SeparationWeight = 3.f;

	UPROPERTY(EditAnywhere, Category="Core", meta = (ClampMin = 0))
	float AlignmentWeight = 1.f;

	UPROPERTY(EditAnywhere, Category="Core", meta = (ClampMin = 0))
	float CohesionWeight = 1.25f;

	virtual void Accumulate(const FFlockFrameContext& Context, const FFlockAgentView& Agent,
		const FFlockNeighbors& Neighbors, FFlockSteering& Out) const override;
};
