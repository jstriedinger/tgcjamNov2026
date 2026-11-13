// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "FlockContainmentVolume.generated.h"

UENUM(BlueprintType)
enum class EFlockContainmentMode : uint8
{
	/** Pushes back from inside, starting Margin cm inside each face */
	InsideMargin,

	/** Pushes back only outside the box, growing over RampDistance */
	SoftReturn,
};

/** A placed box (like a NavMeshBoundsVolume) that keeps a flock inside */
UCLASS()
class TGJFLYING_API AFlockContainmentVolume : public AVolume
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock")
	EFlockContainmentMode Mode = EFlockContainmentMode::InsideMargin;

	/** Distance from a face where the push starts, full strength at the face */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Inside Margin", meta = (EditCondition = "Mode == EFlockContainmentMode::InsideMargin", ClampMin = 0, Units = "cm"))
	float Margin = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Inside Margin", meta = (EditCondition = "Mode == EFlockContainmentMode::InsideMargin", ClampMin = 0))
	float Weight = 4.f;

	/** How far outside the box the push reaches full strength */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Soft Return", meta = (EditCondition = "Mode == EFlockContainmentMode::SoftReturn", ClampMin = 1, Units = "cm"))
	float RampDistance = 400.f;

	/** Turning force at full strength, roughly rad/s */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Soft Return", meta = (EditCondition = "Mode == EFlockContainmentMode::SoftReturn", ClampMin = 0))
	float TurnStrength = 1.f;

	bool GetContainmentBox(FVector& OutCenter, FQuat& OutRotation, FVector& OutExtent) const;
};
