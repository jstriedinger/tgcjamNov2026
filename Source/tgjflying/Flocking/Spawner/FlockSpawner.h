// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlockSpawner.generated.h"

class AFlockAgent;
class AFlockContainmentVolume;
class UBoxComponent;
class UFlockObstacleComponent;
class UFMODEvent;
class UFlockTypeDefinition;

/** One kind of agent a spawner creates */
USTRUCT(BlueprintType)
struct FFlockSpawnEntry
{
	GENERATED_BODY()

	/** Agent to spawn, a Blueprint child of AFlockAgent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
	TSubclassOf<AFlockAgent> AgentClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock", meta = (ClampMin = 0))
	int32 Count = 50;
};

/** Spawns a flock inside its box, or a prison when contain-in-sphere is on */
UCLASS(Blueprintable)
class TGJFLYING_API AFlockSpawner : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> SpawnBox;

public:

	AFlockSpawner();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category="Flock")
	void SpawnFlock();

	UFUNCTION(BlueprintCallable, Category="Flock")
	void DespawnFlock();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
	TObjectPtr<UFlockTypeDefinition> FlockType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
	TArray<FFlockSpawnEntry> Entries;

	/** Keeps this flock inside the volume */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
	TObjectPtr<AFlockContainmentVolume> ContainmentVolume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
	bool bSpawnOnBeginPlay = true;

	/** Spawns the birds inside this actor's obstacle sphere and keeps them there */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Prison")
	bool bContainInObstacleSphere = false;

	/** The cage pushes birds back starting this far inside the sphere surface */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Prison", meta = (EditCondition = "bContainInObstacleSphere", ClampMin = 1, Units = "cm"))
	float CageMargin = 250.f;

	/** Friends (group size without the player) needed for a call to pop the prison */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Prison", meta = (EditCondition = "bContainInObstacleSphere", ClampMin = 0))
	int32 RequiredFriends = 0;

	/** How close to the bubble surface the player must call */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Prison", meta = (EditCondition = "bContainInObstacleSphere", ClampMin = 0, Units = "cm"))
	float ReleaseRadius = 1200.f;

	/** FMOD one-shot played when the prison pops */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Prison", meta = (EditCondition = "bContainInObstacleSphere"))
	TObjectPtr<UFMODEvent> ReleaseSound;

	/** Seconds the bubble takes to fade away when it pops */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Prison", meta = (EditCondition = "bContainInObstacleSphere", ClampMin = 0.05, Units = "s"))
	float PopTime = 0.5f;

	/** How long freed birds are pushed outward from the bubble's middle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Prison", meta = (EditCondition = "bContainInObstacleSphere", ClampMin = 0, Units = "s"))
	float ScatterTime = 1.5f;

	/** Steering force of the outward push */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Prison", meta = (EditCondition = "bContainInObstacleSphere", ClampMin = 0))
	float ScatterStrength = 4.f;

	/** Removes the cage and the obstacle and frees the birds */
	UFUNCTION(BlueprintCallable, Category="Flock|Prison")
	void ReleaseFlock();

	UFUNCTION(BlueprintPure, Category="Flock|Prison")
	bool IsReleased() const { return bReleased; }

	/** The prison popped */
	UFUNCTION(BlueprintImplementableEvent, Category="Flock|Prison")
	void OnPrisonReleased();

	/** The player called close enough but has too few friends */
	UFUNCTION(BlueprintImplementableEvent, Category="Flock|Prison")
	void OnCallTooWeak(int32 Friends, int32 Needed);

	virtual void Tick(float DeltaSeconds) override;

	/** Called by the flock subsystem when the player calls */
	UFUNCTION()
	void HandlePlayerCall(FVector PlayerLocation);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Prison", meta = (EditCondition = "bContainInObstacleSphere", ClampMin = 0))
	float CageWeight = 8.f;

	/** Fixed seed for repeatable spawns, negative for random */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
	int32 Seed = -1;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Flock")
	TArray<TObjectPtr<AFlockAgent>> Agents;

private:

	int32 FlockId = INDEX_NONE;

	TWeakObjectPtr<UFlockObstacleComponent> CageComponent;
	bool bReleased = false;

	void EndScatter();
	FTimerHandle ScatterTimer;

	float PopElapsed = -1.f;
};
