// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FlockTypes.h"

namespace FlockContainment
{
	/** Closer than this (cm) to the box counts as inside */
	constexpr float OutsideEpsilon = 0.8f;

	void Accumulate(const FFlockContainmentBox& Box, const FFlockAgentView& Agent, FFlockSteering& Out);

	/** Keeps the agent inside a sphere cage: the push grows over the margin */
	void AccumulateSphere(const FFlockContainmentSphere& Sphere, const FFlockAgentView& Agent, FFlockSteering& Out);

	/** Any thread */
	FVector3f ToBox(const FFlockContainmentBox& Box, const FVector3f& Pos);

	/** Any thread */
	bool IsOutside(const FFlockContainmentBox& Box, const FVector3f& Pos);
}
