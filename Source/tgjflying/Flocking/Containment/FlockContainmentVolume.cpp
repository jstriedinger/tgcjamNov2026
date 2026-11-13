// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockContainmentVolume.h"
#include "Components/BrushComponent.h"

bool AFlockContainmentVolume::GetContainmentBox(FVector& OutCenter, FQuat& OutRotation, FVector& OutExtent) const
{
	const UBrushComponent* BrushComp = GetBrushComponent();
	if (!BrushComp)
	{
		return false;
	}

	// brush box with the actor scale applied, then rotated and moved to the actor
	const FBoxSphereBounds Local = BrushComp->CalcBounds(FTransform(FQuat::Identity, FVector::ZeroVector, GetActorScale3D()));
	OutRotation = GetActorQuat();
	OutCenter = GetActorLocation() + OutRotation.RotateVector(Local.Origin);
	OutExtent = Local.BoxExtent;
	return true;
}
