// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FlockTypes.generated.h"

namespace FlockLimits
{
	/** Hard cap of the per-agent flockmate list */
	constexpr int32 MaxNeighbors = 32;
}

/** Identifies one agent in the UFlockSubsystem */
USTRUCT(BlueprintType)
struct FFlockAgentHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Flock")
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="Flock")
	int32 Generation = 0;

	bool IsSet() const { return Index != INDEX_NONE; }
};

/** Read-only snapshot of the agent being simulated */
struct FFlockAgentView
{
	FVector3f Pos = FVector3f::ZeroVector;
	FVector3f Vel = FVector3f::ZeroVector;

	/** Unit heading: the velocity direction, or the last one when standing still */
	FVector3f Heading = FVector3f::XAxisVector;

	/** Cruise speed in cm/s, before any speed multiplier */
	float Speed = 0.f;

	/** Body size in cm, added to neighbor and separation distances */
	float BodyRadius = 0.f;

	/** The type's neighbor radius in cm, measured between bodies */
	float NeighborRadius = 0.f;

	/** The type's cap on neighbors used for alignment and cohesion */
	int32 MaxNeighbors = 0;

	int32 Slot = INDEX_NONE;
	int32 FlockId = INDEX_NONE;
	int32 TypeId = INDEX_NONE;
};

struct FFlockNeighbor
{
	FVector3f Pos;
	FVector3f Vel;
	float DistSq;
	float BodyRadius;
	int32 Slot;
};

/** Agents of the same type near this one, closest cells first */
struct FFlockNeighbors
{
	int32 NumFlockmates = 0;
	FFlockNeighbor Flockmates[FlockLimits::MaxNeighbors];
};

/** What behaviors write */
struct FFlockSteering
{
	/** Flocking forces, clamped to the type's MaxSteerForce */
	FVector3f Steer = FVector3f::ZeroVector;

	/** Forces from outside the flock, clamped to MaxSteerForce * ExternalForceMultiplier */
	FVector3f External = FVector3f::ZeroVector;

	/** Wanted speed as a multiple of the cruise speed */
	float SpeedMultiplier = 1.f;
};

/** How a containment volume pushes agents back */
enum class EFlockContainmentModeView : uint8
{
	InsideMargin,
	SoftReturn,
};

/** Snapshot of one AFlockContainmentVolume: an oriented box */
struct FFlockContainmentBox
{
	EFlockContainmentModeView Mode = EFlockContainmentModeView::InsideMargin;

	/** InsideMargin mode */
	float Margin = 500.f;
	float Weight = 4.f;

	/** SoftReturn mode: the push grows from zero at the face to TurnStrength at RampDistance outside */
	float RampDistance = 400.f;
	float TurnStrength = 1.f;
	FVector3f Center = FVector3f::ZeroVector;
	FVector3f Extent = FVector3f::ZeroVector;
	FQuat4f Rotation = FQuat4f::Identity;
	FQuat4f InverseRotation = FQuat4f::Identity;
};

/** Snapshot of a sphere cage (a prison): keeps a flock inside a ball */
struct FFlockContainmentSphere
{
	FVector3f Center = FVector3f::ZeroVector;
	float Radius = 0.f;

	/** The push starts this far (cm) inside the surface and is full at it */
	float Margin = 250.f;

	/** Strength of the push toward the center, at the surface */
	float Weight = 8.f;
};

/** Each group member gets a fixed random slot in a loose V behind the player and steers toward it */
struct FFlockFollowSettings
{
	bool bEnabled = false;

	/** Closest and farthest slot behind the player (cm) */
	float MinDepth = 250.f;
	float MaxDepth = 1200.f;

	/** Sideways and vertical offset of a slot per cm of depth */
	float SideSpread = 0.7f;
	float VerticalSpread = 0.2f;

	/** Steering strength toward the slot, full at FullForceDistance from it */
	float Weight = 2.f;
	float FullForceDistance = 600.f;

	/** Speeds up by up to this fraction when behind its slot, slows down when ahead */
	float SpeedAdjust = 0.25f;
	float SpeedAdjustDistance = 800.f;
};

/** While the flock is scared (storm), birds outside the player's group run from the player */
struct FFlockFleeSettings
{
	/** Closer than this (cm) to the player a scared bird runs away */
	float AvoidRadius = 1000.f;

	/** Steering strength away from the player */
	float Weight = 3.f;

	/** Speed multiplier while the player is inside the avoid radius */
	float SpeedMultiplier = 1.5f;
};

/** One obstacle sphere (a UFlockObstacleComponent), as read this frame */
struct FFlockObstacleSource
{
	FVector3f Center = FVector3f::ZeroVector;
	float Radius = 0.f;
	FGameplayTagContainer Tags;
};

/** Per-frame values shared by all behaviors */
struct FFlockFrameContext
{
	float DeltaTime = 0.f;
	float TimeSeconds = 0.f;

	/** Every obstacle sphere of the level */
	TArray<FFlockObstacleSource> Obstacles;

	/** Sphere cage (prison) of a flock, by flock id */
	TMap<int32, FFlockContainmentSphere> ContainmentSphereByFlock;

	/** Box of the flock's containment volume, by flock id */
	TMap<int32, FFlockContainmentBox> ContainmentByFlock;
};
