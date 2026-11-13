// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockAgent.h"
#include "FlockCallComponent.h"
#include "FlockContainmentVolume.h"
#include "FlockDataTypes.h"
#include "FlockSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AFlockAgent::AFlockAgent()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(ConeMesh.Object);
	}

	// the cone's default material has no color parameter
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, ShapeMaterial.Object);
	}

	// the engine cone points along +Z, the agent heads along +X
	Mesh->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
	Mesh->SetRelativeScale3D(FVector(0.3, 0.3, 0.6));

	CallComponent = CreateDefaultSubobject<UFlockCallComponent>(TEXT("Call"));
	CallComponent->StretchTarget = Mesh;
}

float AFlockAgent::RollCruiseSpeed() const
{
	return FMath::Max(FMath::FRandRange(CruiseSpeedRange.Min, CruiseSpeedRange.Max), 0.f);
}

void AFlockAgent::BeginPlay()
{
	Super::BeginPlay();

	// a bird placed in the level joins its flock itself
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!PlacedFlockType || !Flock || Handle.IsSet())
	{
		return;
	}

	PlacedFlockId = Flock->RegisterFlock();
	Flock->SetFlockContainment(PlacedFlockId, PlacedContainmentVolume);
	if (!Flock->RegisterAgent(this, PlacedFlockType, PlacedFlockId, GetActorLocation(), GetActorForwardVector()).IsSet())
	{
		UE_LOG(LogFlock, Error, TEXT("%s: could not register the placed bird with the flock"), *GetName());
	}
}

void AFlockAgent::ApplyRandomScale()
{
	if (bScaleApplied)
	{
		return;
	}
	bScaleApplied = true;

	if (!bRandomizeScale || !Mesh)
	{
		return;
	}

	const float Step = FMath::Max(ScaleStep, 0.01f);
	const float Low = FMath::Max(ScaleRange.Min, 0.1f);
	const float High = FMath::Max(ScaleRange.Max, Low);
	const int32 NumSteps = FMath::RoundToInt((High - Low) / Step);
	ScaleFactor = Low + float(FMath::RandRange(0, NumSteps)) * Step;

	// multiplies the mesh's own scale, the call reads it when it starts
	Mesh->SetRelativeScale3D(Mesh->GetRelativeScale3D() * ScaleFactor);
}

void AFlockAgent::ApplyRandomShade()
{
	if (bShadeApplied)
	{
		return;
	}
	bShadeApplied = true;

	if (Shades.Num() == 0 || !Mesh)
	{
		return;
	}

	const FLinearColor Shade = Shades[FMath::RandRange(0, Shades.Num() - 1)];
	TintMaterial = Mesh->CreateDynamicMaterialInstance(0);
	if (TintMaterial)
	{
		TintMaterial->SetVectorParameterValue(ColorParameterName, Shade);
		OriginalTint = Shade;
	}
}

void AFlockAgent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Handle.IsSet())
	{
		if (UWorld* World = GetWorld())
		{
			if (UFlockSubsystem* Flock = World->GetSubsystem<UFlockSubsystem>())
			{
				Flock->UnregisterAgent(Handle);
				if (PlacedFlockId != INDEX_NONE)
				{
					Flock->UnregisterFlock(PlacedFlockId);
					PlacedFlockId = INDEX_NONE;
				}
			}
		}
		Handle = FFlockAgentHandle();
	}

	Super::EndPlay(EndPlayReason);
}

void AFlockAgent::NotifyJoinedFlock(const FFlockAgentHandle& InHandle, int32 InFlockId)
{
	Handle = InHandle;
	FlockId = InFlockId;
	OnJoinedFlock(InFlockId);
}

bool AFlockAgent::IsInFlock() const
{
	const UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	return Flock && Flock->IsHandleValid(Handle);
}

float AFlockAgent::GetCruiseSpeed() const
{
	const UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	return Flock ? Flock->GetAgentSpeed(Handle) : 0.f;
}

FFloatInterval AFlockAgent::GetAnswerDelayRange() const
{
	return CallProfile ? CallProfile->AnswerDelayRange : FFloatInterval(0.2f, 0.6f);
}

void AFlockAgent::SetCruiseSpeed(float NewSpeed)
{
	if (UFlockSubsystem* Flock = UFlockSubsystem::Get(this))
	{
		Flock->SetAgentSpeed(Handle, NewSpeed);
	}
}

void AFlockAgent::OnPlayerGroupChanged_Implementation(bool bInPlayerGroup)
{
	if (!bTintInPlayerGroup || !Mesh)
	{
		return;
	}

	if (!TintMaterial)
	{
		TintMaterial = Mesh->CreateDynamicMaterialInstance(0);
		if (!TintMaterial)
		{
			return;
		}
		TintMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TintParameterName), OriginalTint);
	}
	TintMaterial->SetVectorParameterValue(TintParameterName, bInPlayerGroup ? PlayerGroupTint : OriginalTint);
}

void AFlockAgent::SetCallableGlow(bool bOn)
{
	if (bOn == bCallableGlow || !Mesh || !GetWorld())
	{
		return;
	}
	bCallableGlow = bOn;

	// shares the bird's own material with the shade and the tint
	if (!TintMaterial)
	{
		TintMaterial = Mesh->CreateDynamicMaterialInstance(0);
		if (!TintMaterial)
		{
			return;
		}
	}
	TintMaterial->SetScalarParameterValue(TEXT("Callable"), bOn ? 1.f : 0.f);
	TintMaterial->SetScalarParameterValue(TEXT("CallableChange"), float(GetWorld()->GetTimeSeconds()));
}

UStaticMeshComponent* AFlockAgent::GetMeshComponent() const
{
	return Mesh;
}
