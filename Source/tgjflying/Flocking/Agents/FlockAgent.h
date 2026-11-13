// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/Interval.h"
#include "FlockTypes.h"
#include "FlockAgent.generated.h"

class UFlockCallComponent;
class AFlockContainmentVolume;
class UFlockCallProfile;
class UFlockTypeDefinition;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** One member of a flock */
UCLASS(Blueprintable)
class TGJFLYING_API AFlockAgent : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFlockCallComponent> CallComponent;

public:

	AFlockAgent();

	virtual void BeginPlay() override;

	/** For a bird placed by hand: the flock type it joins at start */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Placed")
	TObjectPtr<UFlockTypeDefinition> PlacedFlockType;

	/** Optional volume that keeps a placed bird inside */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Placed")
	TObjectPtr<AFlockContainmentVolume> PlacedContainmentVolume;

	/** Each agent rolls its cruise speed in this range when it joins a flock */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock Movement")
	FFloatInterval CruiseSpeedRange = FFloatInterval(500.f, 700.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock Movement", meta = (ClampMin = 0, Units = "cm"))
	float BodyRadius = 60.f;

	/** Each bird picks its own size when it starts */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Variation")
	bool bRandomizeScale = true;

	/** Random size as a multiple of the mesh scale, picked in steps of Scale Step */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Variation", meta = (EditCondition = "bRandomizeScale", ClampMin = 0.1))
	FFloatInterval ScaleRange = FFloatInterval(1.f, 1.5f);

	/** Step the random size is rounded to */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Variation", meta = (EditCondition = "bRandomizeScale", ClampMin = 0.01))
	float ScaleStep = 0.1f;

	/** Each bird picks one of these at random when it starts */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Variation")
	TArray<FLinearColor> Shades;

	/** Vector parameter of the mesh material that gets the shade */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Variation")
	FName ColorParameterName = TEXT("Color");

	/** Picks a random shade and puts it on the mesh, once */
	void ApplyRandomShade();

	/** Picks a random size and scales the mesh by it, once */
	void ApplyRandomScale();

	float GetScaledBodyRadius() const { return BodyRadius * ScaleFactor; }

	/** Sound, answer delay and bubble look of this kind's call */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Call")
	TObjectPtr<UFlockCallProfile> CallProfile;

	/** Range of seconds this bird waits before answering a call */
	FFloatInterval GetAnswerDelayRange() const;

	UFlockCallComponent* GetCallComponent() const { return CallComponent; }

	float RollCruiseSpeed() const;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void NotifyJoinedFlock(const FFlockAgentHandle& InHandle, int32 InFlockId);

	UStaticMeshComponent* GetMeshComponent() const;

	UFUNCTION(BlueprintPure, Category="Flock")
	bool IsInFlock() const;

	UFUNCTION(BlueprintPure, Category="Flock")
	int32 GetFlockId() const { return FlockId; }

	UFUNCTION(BlueprintPure, Category="Flock")
	FFlockAgentHandle GetFlockHandle() const { return Handle; }

	/** Cruise speed in cm/s, 0 when not in a flock */
	UFUNCTION(BlueprintPure, Category="Flock")
	float GetCruiseSpeed() const;

	UFUNCTION(BlueprintCallable, Category="Flock")
	void SetCruiseSpeed(float NewSpeed);

	/** The agent was registered with the simulation */
	UFUNCTION(BlueprintImplementableEvent, Category="Flock")
	void OnJoinedFlock(int32 InFlockId);

	/** The agent joined or left the player's group */
	UFUNCTION(BlueprintNativeEvent, Category="Flock")
	void OnPlayerGroupChanged(bool bInPlayerGroup);

	/** On while the player's call would recruit this bird */
	void SetCallableGlow(bool bOn);

	/** Tint the mesh while the agent is in the player's group */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Group Tint")
	bool bTintInPlayerGroup = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Group Tint")
	FLinearColor PlayerGroupTint = FLinearColor(0.1f, 0.9f, 0.2f);

	/** Vector parameter of the mesh material that gets the tint */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Group Tint")
	FName TintParameterName = TEXT("Color");

private:

	/** Created on first use, so plain agents cost nothing */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TintMaterial;

	FLinearColor OriginalTint = FLinearColor::White;

	FFlockAgentHandle Handle;
	int32 FlockId = INDEX_NONE;

	/** The flock made for a placed bird, INDEX_NONE for a spawned one */
	int32 PlacedFlockId = INDEX_NONE;

	float ScaleFactor = 1.f;
	bool bScaleApplied = false;
	bool bShadeApplied = false;

	bool bCallableGlow = false;
};
