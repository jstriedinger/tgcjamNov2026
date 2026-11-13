// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "FlockFlightCameraManager.generated.h"

/** Chase camera that follows the player's bird and frames its flock */
UCLASS(Blueprintable)
class TGJFLYING_API AFlockFlightCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:

	AFlockFlightCameraManager();

	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

	// all camera numbers come from the player's settings (UFlockPlayerSettings, Camera|...)

private:

	/** Clears all remembered state so the next frame snaps to the right view */
	void ResetState();

	bool bInitialized = false;

	/** The group's heading after a short smoothing, a unit vector */
	FVector FilteredGroupHeading = FVector::XAxisVector;

	/** Yaw of FilteredGroupHeading, kept for frames where the heading points straight up or down */
	float GroupYaw = 0.f;

	/** The camera's own yaw, in degrees */
	float CameraYaw = 0.f;
	float CameraYawVelocity = 0.f;

	/** True while the camera catches up to the group's heading without steering */
	bool bFollowingGroupYaw = false;

	float CurrentDistance = 0.f;
	float DistanceVelocity = 0.f;

	/** How much of the pan toward the group is applied, 0..1 */
	float CenterBlend = 0.f;
	float CenterBlendVelocity = 0.f;

	/** Pan offset from the player toward the group: the goal (held by the dead radius) and the smoothed value */
	FVector OffsetGoal = FVector::ZeroVector;
	FVector SmoothedOffset = FVector::ZeroVector;
	FVector SmoothedOffsetVelocity = FVector::ZeroVector;

	/** Look-around offsets in degrees, relative to the resting view */
	float LookYaw = 0.f;
	float LookPitch = 0.f;
	float TimeSinceLookInput = 0.f;

	/** Extra field of view in degrees while dashing or diving */
	float SpeedKickFov = 0.f;
};
