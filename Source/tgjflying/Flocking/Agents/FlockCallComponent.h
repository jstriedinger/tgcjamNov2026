// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlockCallComponent.generated.h"

class UFlockCallProfile;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFlockCalledSignature);

/** A bird's call: squash and stretch, a bubble, and the On Called event */
UCLASS(ClassGroup=(Flock), meta=(BlueprintSpawnableComponent))
class TGJFLYING_API UFlockCallComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UFlockCallComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category="Call")
	bool Call();

	/** True until the bird is back to its shape and the bubble is gone */
	UFUNCTION(BlueprintPure, Category="Call")
	bool IsCalling() const { return bCalling; }

	/** Fires when the bubble pops out */
	UPROPERTY(BlueprintAssignable, Category="Call")
	FFlockCalledSignature OnCalled;

	/** The mesh that squashes and stretches */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Call")
	TObjectPtr<USceneComponent> StretchTarget;

private:

	/** The owner's call profile, or the defaults when it has none */
	const UFlockCallProfile& GetCallProfile() const;

	void EnsureBubble();

	void FinishCall();

	/** Length factor along the flight direction at this time into the call */
	float GetLengthScale(float Time, const UFlockCallProfile& S) const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Bubble;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	bool bCalling = false;
	bool bReleased = false;

	double CallStartTime = 0.0;
	double ReadyTime = -1.0;

	/** Target's own scale, and its local axis (0 X, 1 Y, 2 Z) along the flight direction */
	FVector RestScale = FVector::OneVector;
	int32 LengthAxis = 0;
};
