// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockSpawner.h"
#include "FlockAgent.h"
#include "FlockContainmentVolume.h"
#include "FlockObstacleComponent.h"
#include "FlockSubsystem.h"
#include "FlockDataTypes.h"
#include "FlockSpatialHash.h"
#include "FlockCallComponent.h"
#include "FMODBlueprintStatics.h"
#include "FMODEvent.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

AFlockSpawner::AFlockSpawner()
{
	// ticks only while a prison pops
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SpawnBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnBox"));
	SpawnBox->SetBoxExtent(FVector(1000.0, 1000.0, 500.0));
	SpawnBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnBox->SetGenerateOverlapEvents(false);
	SpawnBox->SetHiddenInGame(true);
	SetRootComponent(SpawnBox);

	// a new spawner starts with the plain agent so it works out of the box
	Entries.AddDefaulted_GetRef().AgentClass = AFlockAgent::StaticClass();
}

void AFlockSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (bSpawnOnBeginPlay)
	{
		SpawnFlock();
	}

	// a prison listens for the player's call
	if (bContainInObstacleSphere)
	{
		SetActorTickEnabled(true);
		if (UFlockSubsystem* Flock = UFlockSubsystem::Get(this))
		{
			Flock->OnPlayerCalled.AddDynamic(this, &AFlockSpawner::HandlePlayerCall);
		}
	}
}

void AFlockSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFlockSubsystem* Flock = UFlockSubsystem::Get(this))
	{
		Flock->OnPlayerCalled.RemoveDynamic(this, &AFlockSpawner::HandlePlayerCall);
	}
	DespawnFlock();

	Super::EndPlay(EndPlayReason);
}

void AFlockSpawner::SpawnFlock()
{
	UWorld* World = GetWorld();
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!World || !Flock)
	{
		return;
	}
	if (!FlockType)
	{
		UE_LOG(LogFlock, Warning, TEXT("%s: no FlockType set, nothing spawned"), *GetName());
		return;
	}
	if (FlockId != INDEX_NONE)
	{
		return;
	}

	FlockId = Flock->RegisterFlock();
	Flock->SetFlockContainment(FlockId, ContainmentVolume);

	const UFlockObstacleComponent* Cage = nullptr;
	if (bContainInObstacleSphere)
	{
		UFlockObstacleComponent* Found = FindComponentByClass<UFlockObstacleComponent>();
		Cage = Found;
		CageComponent = Found;
		if (Cage)
		{
			Flock->SetFlockContainmentSphere(FlockId, Cage, CageMargin, CageWeight);
		}
		else
		{
			UE_LOG(LogFlock, Warning, TEXT("%s: Contain In Obstacle Sphere is on but there is no Flock Obstacle component, no cage"), *GetName());
		}
	}

	FRandomStream Stream(Seed >= 0 ? Seed : FMath::Rand());

	const FTransform BoxTransform = SpawnBox->GetComponentTransform();
	const FVector BoxExtent = SpawnBox->GetScaledBoxExtent();

	for (const FFlockSpawnEntry& Entry : Entries)
	{
		if (!Entry.AgentClass)
		{
			UE_LOG(LogFlock, Warning, TEXT("%s: a spawn entry has no AgentClass, skipped"), *GetName());
			continue;
		}

		for (int32 Index = 0; Index < Entry.Count; ++Index)
		{
			const FVector Local(
				Stream.FRandRange(-BoxExtent.X, BoxExtent.X),
				Stream.FRandRange(-BoxExtent.Y, BoxExtent.Y),
				Stream.FRandRange(-BoxExtent.Z, BoxExtent.Z));
			FVector Position = BoxTransform.TransformPositionNoScale(Local);
			const FVector Direction = Stream.VRand();

			// prison birds start well inside the ball, the cube root spreads them evenly
			if (Cage)
			{
				const float Radius = Cage->GetScaledSphereRadius() * 0.6f * FMath::Pow(Stream.FRand(), 1.f / 3.f);
				Position = Cage->GetComponentLocation() + Stream.VRand() * Radius;
			}

			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AFlockAgent* Agent = World->SpawnActor<AFlockAgent>(Entry.AgentClass, Position, Direction.Rotation(), Params);
			if (!Agent)
			{
				continue;
			}

			const FFlockAgentHandle Handle = Flock->RegisterAgent(Agent, FlockType, FlockId, Position, Direction);
			if (Handle.IsSet())
			{
				Agents.Add(Agent);
				if (Cage)
				{
					Flock->SetAgentCaged(Handle, true);
				}
			}
			else
			{
				Agent->Destroy();
			}
		}
	}
}

void AFlockSpawner::HandlePlayerCall(FVector PlayerLocation)
{
	const UFlockObstacleComponent* Cage = CageComponent.Get();
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!bContainInObstacleSphere || bReleased || FlockId == INDEX_NONE || !Cage || !Flock)
	{
		return;
	}

	// distance to the bubble's surface, not to its middle
	const float DistanceToSurface = FVector::Dist(PlayerLocation, Cage->GetComponentLocation()) - Cage->GetScaledSphereRadius();
	if (DistanceToSurface > ReleaseRadius)
	{
		return;
	}

	const int32 Friends = FMath::Max(Flock->GetPlayerGroupSize() - 1, 0);
	if (Friends < RequiredFriends)
	{
		OnCallTooWeak(Friends, RequiredFriends);
		return;
	}

	ReleaseFlock();
}

void AFlockSpawner::ReleaseFlock()
{
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (bReleased || !Flock)
	{
		return;
	}
	bReleased = true;

	if (FlockId != INDEX_NONE)
	{
		Flock->SetFlockContainmentSphere(FlockId, nullptr, 0.f, 0.f);
	}
	if (UFlockObstacleComponent* Cage = CageComponent.Get())
	{
		Flock->UnregisterObstacle(Cage);
	}

	for (AFlockAgent* Agent : Agents)
	{
		if (!IsValid(Agent))
		{
			continue;
		}
		const FFlockAgentHandle Handle = Agent->GetFlockHandle();
		Flock->SetAgentCaged(Handle, false);
		// bursts out of the bubble
		const FVector Center = CageComponent.IsValid() ? CageComponent->GetComponentLocation() : GetActorLocation();
		const FVector Out = (Agent->GetActorLocation() - Center).GetSafeNormal();
		Flock->SetAgentSteer(Handle, Out * ScatterStrength);
		if (UFlockCallComponent* Call = Agent->GetCallComponent())
		{
			Call->Call();
		}
	}

	if (ReleaseSound)
	{
		const FVector Where = CageComponent.IsValid() ? CageComponent->GetComponentLocation() : GetActorLocation();
		UFMODBlueprintStatics::PlayEventAtLocation(this, ReleaseSound, FTransform(Where), true);
	}

	OnPrisonReleased();

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(ScatterTimer, this, &AFlockSpawner::EndScatter, FMath::Max(ScatterTime, 0.05f), false);
	}

	// the material's Pop parameter fades the bubble, then the prison hides
	PopElapsed = 0.f;
	SetActorTickEnabled(true);
}

void AFlockSpawner::EndScatter()
{
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!Flock)
	{
		return;
	}
	for (AFlockAgent* Agent : Agents)
	{
		if (IsValid(Agent))
		{
			Flock->SetAgentSteer(Agent->GetFlockHandle(), FVector::ZeroVector);
		}
	}
}

void AFlockSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (PopElapsed < 0.f)
	{
		if (bReleased)
		{
			SetActorTickEnabled(false);
		}
		return;
	}

	PopElapsed += DeltaSeconds;
	const float Pop = FMath::Clamp(PopElapsed / FMath::Max(PopTime, 0.05f), 0.f, 1.f);
	TArray<UStaticMeshComponent*> Meshes;
	GetComponents<UStaticMeshComponent>(Meshes);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		Mesh->SetScalarParameterValueOnMaterials(TEXT("Pop"), Pop);
	}

	if (Pop >= 1.f)
	{
		SetActorHiddenInGame(true);
		PopElapsed = -1.f;
		SetActorTickEnabled(false);
	}
}

void AFlockSpawner::DespawnFlock()
{
	if (UFlockSubsystem* Flock = UFlockSubsystem::Get(this))
	{
		if (FlockId != INDEX_NONE)
		{
			Flock->UnregisterFlock(FlockId);
		}
	}
	FlockId = INDEX_NONE;

	for (AFlockAgent* Agent : Agents)
	{
		if (IsValid(Agent))
		{
			Agent->Destroy();
		}
	}
	Agents.Reset();
}
