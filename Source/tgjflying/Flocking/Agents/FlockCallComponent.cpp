// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockCallComponent.h"
#include "FlockAgent.h"
#include "FlockPlayer.h"
#include "FlockDataTypes.h"
#include "FlockSubsystem.h"
#include "FMODBlueprintStatics.h"
#include "FMODEvent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UFlockCallComponent::UFlockCallComponent()
{
	// only ticks while a call plays
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		SphereMesh = Sphere.Object;
	}
}

void UFlockCallComponent::BeginPlay()
{
	Super::BeginPlay();

	// default target is the owner's first static mesh
	if (!StretchTarget && GetOwner())
	{
		StretchTarget = GetOwner()->FindComponentByClass<UStaticMeshComponent>();
	}
}

const UFlockCallProfile& UFlockCallComponent::GetCallProfile() const
{
	const UFlockCallProfile* Profile = nullptr;
	if (const AFlockAgent* Agent = Cast<AFlockAgent>(GetOwner()))
	{
		Profile = Agent->CallProfile;
	}
	else if (const AFlockPlayer* Player = Cast<AFlockPlayer>(GetOwner()))
	{
		Profile = Player->CallProfile;
	}
	return Profile ? *Profile : *GetDefault<UFlockCallProfile>();
}

bool UFlockCallComponent::Call()
{
	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() < ReadyTime)
	{
		return false;
	}

	const UFlockCallProfile& S = GetCallProfile();
	const double Now = World->GetTimeSeconds();
	ReadyTime = Now + S.Cooldown;

	// keep the rest shape, unless a call is still playing and the mesh is mid-squash
	if (!bCalling && StretchTarget)
	{
		RestScale = StretchTarget->GetRelativeScale3D();

		const FVector LocalForward = StretchTarget->GetRelativeRotation().UnrotateVector(FVector::XAxisVector).GetAbs();
		LengthAxis = LocalForward.X >= LocalForward.Y && LocalForward.X >= LocalForward.Z ? 0 : (LocalForward.Y >= LocalForward.Z ? 1 : 2);
	}

	CallStartTime = Now;
	bCalling = true;
	bReleased = false;
	SetComponentTickEnabled(true);
	return true;
}

float UFlockCallComponent::GetLengthScale(float Time, const UFlockCallProfile& S) const
{
	const float SquashEnd = S.SquashTime;
	const float StretchEnd = S.SquashTime + S.StretchTime;

	if (Time < SquashEnd)
	{
		return FMath::Lerp(1.f, S.SquashAmount, FMath::SmoothStep(0.f, SquashEnd, Time));
	}
	if (Time < StretchEnd)
	{
		return FMath::Lerp(S.SquashAmount, S.StretchAmount, FMath::SmoothStep(SquashEnd, StretchEnd, Time));
	}
	const float Settle = (Time - StretchEnd) / FMath::Max(S.SettleTime, 0.01f);
	if (Settle >= 1.f)
	{
		return 1.f;
	}
	const float Fade = FMath::Square(1.f - Settle);
	return 1.f + (S.StretchAmount - 1.f) * Fade * FMath::Cos(2.f * UE_PI * S.SettleWobbles * Settle);
}

void UFlockCallComponent::EnsureBubble()
{
	AActor* Owner = GetOwner();
	if (Bubble || !SphereMesh || !Owner || !Owner->GetRootComponent())
	{
		return;
	}

	Bubble = NewObject<UStaticMeshComponent>(Owner, TEXT("CallBubble"));
	Bubble->SetStaticMesh(SphereMesh);
	Bubble->SetMobility(EComponentMobility::Movable);
	Bubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bubble->SetGenerateOverlapEvents(false);
	Bubble->SetCastShadow(false);
	Bubble->SetReceivesDecals(false);
	// absolute scale so the bird's own scale does not change the bubble size
	Bubble->SetUsingAbsoluteScale(true);
	Bubble->SetupAttachment(Owner->GetRootComponent());
	Bubble->SetVisibility(false);
	Bubble->RegisterComponent();
}

void UFlockCallComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bCalling || !GetWorld())
	{
		return;
	}

	const UFlockCallProfile& S = GetCallProfile();
	const float Time = float(GetWorld()->GetTimeSeconds() - CallStartTime);

	// keeps the volume: length x, width 1 / sqrt(x)
	if (StretchTarget)
	{
		const float Length = GetLengthScale(Time, S);
		FVector Scale = RestScale * (1.f / FMath::Sqrt(FMath::Max(Length, 0.01f)));
		Scale[LengthAxis] = RestScale[LengthAxis] * Length;
		StretchTarget->SetRelativeScale3D(Scale);
	}

	// the call comes out at the end of the squash
	if (!bReleased && Time >= S.SquashTime)
	{
		bReleased = true;
		EnsureBubble();
		// no material, no bubble: the default sphere material is a solid ball
		if (Bubble && S.BubbleMaterial)
		{
			const FLinearColor& Color = S.BubbleColor;
			Bubble->SetMaterial(0, S.BubbleMaterial);
			Bubble->SetCustomPrimitiveDataVector4(1, FVector4(Color.R, Color.G, Color.B, Color.A));
			Bubble->SetVisibility(true);
		}
		if (S.CallSound && GetOwner())
		{
			UFMODBlueprintStatics::PlayEventAtLocation(this, S.CallSound, FTransform(GetOwner()->GetActorLocation()), true);
		}
		OnCalled.Broadcast();
	}

	// ease-out growth, the material fades it with the progress
	const float Progress = bReleased ? (Time - S.SquashTime) / FMath::Max(S.BubbleDuration, 0.05f) : 0.f;
	if (Bubble && bReleased && Bubble->IsVisible())
	{
		if (Progress < 1.f)
		{
			const float Grow = 1.f - FMath::Pow(1.f - Progress, 3.f);
			const float Radius = FMath::Lerp(S.BubbleStartRadius, S.BubbleEndRadius, Grow);
			// the engine sphere has a 50 cm radius
			Bubble->SetRelativeScale3D(FVector(Radius / 50.f));
			Bubble->SetCustomPrimitiveDataFloat(0, Progress);
		}
		else
		{
			Bubble->SetVisibility(false);
		}
	}

	const float ShapeEnd = S.SquashTime + S.StretchTime + S.SettleTime;
	if (bReleased && Time >= ShapeEnd && Progress >= 1.f)
	{
		FinishCall();
	}
}

void UFlockCallComponent::FinishCall()
{
	if (StretchTarget)
	{
		StretchTarget->SetRelativeScale3D(RestScale);
	}
	if (Bubble)
	{
		Bubble->SetVisibility(false);
	}
	bCalling = false;
	SetComponentTickEnabled(false);
}
