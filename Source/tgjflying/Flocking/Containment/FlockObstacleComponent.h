// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "GameplayTagContainer.h"
#include "FlockObstacleComponent.generated.h"

/** A sphere that birds steer around */
UCLASS(ClassGroup=(Flock), meta=(BlueprintSpawnableComponent))
class TGJFLYING_API UFlockObstacleComponent : public USphereComponent
{
	GENERATED_BODY()

public:

	UFlockObstacleComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
	FGameplayTagContainer ObstacleTags;
};
