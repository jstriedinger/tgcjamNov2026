// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockObstacleComponent.h"
#include "FlockSubsystem.h"

UFlockObstacleComponent::UFlockObstacleComponent()
{
	// a marker sphere, the birds only read its centre and radius
	InitSphereRadius(300.f);
	SetCollisionProfileName(TEXT("NoCollision"));
	SetGenerateOverlapEvents(false);
	bHiddenInGame = true;
	ShapeColor = FColor(255, 120, 40);
}

void UFlockObstacleComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UFlockSubsystem* Flock = UFlockSubsystem::Get(this))
	{
		Flock->RegisterObstacle(this, ObstacleTags);
	}
}

void UFlockObstacleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFlockSubsystem* Flock = UFlockSubsystem::Get(this))
	{
		Flock->UnregisterObstacle(this);
	}

	Super::EndPlay(EndPlayReason);
}
