// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FlockTypes.h"
#include "FlockBehavior.generated.h"

/** One steering rule of a flock type */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class TGJFLYING_API UFlockBehavior : public UObject
{
	GENERATED_BODY()

public:

	/** Game thread, once per frame before the simulation: copy what is needed from UObjects into plain data */
	virtual void PrepareFrame(FFlockFrameContext& Context) {}

	/** Add this behavior's force to Out.Steer (flocking) or Out.External (outside forces) */
	virtual void Accumulate(const FFlockFrameContext& Context, const FFlockAgentView& Agent,
		const FFlockNeighbors& Neighbors, FFlockSteering& Out) const PURE_VIRTUAL(UFlockBehavior::Accumulate, );
};
