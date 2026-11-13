// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockFlightCameraManager.h"
#include "FlockPlayer.h"
#include "FlockDataTypes.h"
#include "FlockSubsystem.h"

namespace
{
	double FlightSmoothDampDouble(double Current, double Target, double& Velocity, double SmoothTime, double DeltaTime)
	{
		SmoothTime = FMath::Max(SmoothTime, 0.0001);
		const double Omega = 2.0 / SmoothTime;
		const double X = Omega * DeltaTime;
		// cheap approximation of exp(-X)
		const double Falloff = 1.0 / (1.0 + X + 0.48 * X * X + 0.235 * X * X * X);
		const double Change = Current - Target;
		const double Temp = (Velocity + Omega * Change) * DeltaTime;
		Velocity = (Velocity - Omega * Temp) * Falloff;
		return Target + (Change + Temp) * Falloff;
	}

	float FlightSmoothDamp(float Current, float Target, float& Velocity, float SmoothTime, float DeltaTime)
	{
		double V = Velocity;
		const double Result = FlightSmoothDampDouble(Current, Target, V, SmoothTime, DeltaTime);
		Velocity = float(V);
		return float(Result);
	}

	/** Smooth damp for a vector, each axis on its own */
	FVector FlightSmoothDamp(const FVector& Current, const FVector& Target, FVector& Velocity, float SmoothTime, float DeltaTime)
	{
		return FVector(
			FlightSmoothDampDouble(Current.X, Target.X, Velocity.X, SmoothTime, DeltaTime),
			FlightSmoothDampDouble(Current.Y, Target.Y, Velocity.Y, SmoothTime, DeltaTime),
			FlightSmoothDampDouble(Current.Z, Target.Z, Velocity.Z, SmoothTime, DeltaTime));
	}

	/** Smooth damp for angles in degrees */
	float FlightSmoothDampAngle(float Current, float Target, float& Velocity, float SmoothTime, float DeltaTime)
	{
		const float ClosestTarget = Current + FMath::FindDeltaAngleDegrees(Current, Target);
		return float(FRotator::NormalizeAxis(FlightSmoothDamp(Current, ClosestTarget, Velocity, SmoothTime, DeltaTime)));
	}

	/** Yaw in degrees of a vector seen from above, or Fallback when it points straight up or down */
	float FlightYawOf(const FVector& Direction, float Fallback)
	{
		if (FVector2D(Direction.X, Direction.Y).SizeSquared() < 1e-6)
		{
			return Fallback;
		}
		return float(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)));
	}
}

AFlockFlightCameraManager::AFlockFlightCameraManager()
{
}

void AFlockFlightCameraManager::ResetState()
{
	bInitialized = false;
	FilteredGroupHeading = FVector::XAxisVector;
	GroupYaw = 0.f;
	CameraYaw = 0.f;
	CameraYawVelocity = 0.f;
	bFollowingGroupYaw = false;
	CurrentDistance = 0.f;
	DistanceVelocity = 0.f;
	CenterBlend = 0.f;
	CenterBlendVelocity = 0.f;
	OffsetGoal = FVector::ZeroVector;
	SmoothedOffset = FVector::ZeroVector;
	SmoothedOffsetVelocity = FVector::ZeroVector;
	LookYaw = 0.f;
	LookPitch = 0.f;
	TimeSinceLookInput = 0.f;
	SpeedKickFov = 0.f;
}

void AFlockFlightCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	AFlockPlayer* Player = Cast<AFlockPlayer>(OutVT.Target);
	UFlockSubsystem* Flock = Player ? UFlockSubsystem::Get(Player) : nullptr;
	if (!Player || !Flock || !Flock->IsHandleValid(Player->GetFlockHandle()))
	{
		ResetState();
		Super::UpdateViewTarget(OutVT, DeltaTime);
		return;
	}

	const UFlockPlayerSettings& S = Player->GetSettings();

	const float Fullness = Player->GetFlockFullness();
	const float FullnessTarget = Player->GetFlockFullnessTarget();

	// a long hitch must not make the camera fly off
	const float Dt = FMath::Clamp(DeltaTime, 0.f, 0.1f);

	// the first frame has nothing to ease from, so everything snaps to its goal
	const bool bFirstFrame = !bInitialized;

	const FVector PlayerPos = Player->GetActorLocation();
	const FVector PlayerHeading = Flock->GetAgentHeading(Player->GetFlockHandle());

	const bool bHasGroup = Flock->GetPlayerGroupSize() > 0;
	const int32 GroupSize = FMath::Max(Flock->GetPlayerGroupSize(), 1);
	const FVector GroupCenter = bHasGroup ? Flock->GetPlayerGroupCenter() : PlayerPos;
	const FVector GroupHeading = bHasGroup ? Flock->GetPlayerGroupHeading() : PlayerHeading;

	const FVector2D SteerInput = Player->GetSteerInput();

	// 0..1 while a move that takes over is running
	const EFlockManeuver Move = Flock->GetPlayerManeuver();
	const float ManeuverWeight = Flock->IsPlayerManeuverOverriding() ? Flock->GetPlayerManeuverWeight() : 0.f;

	// the halo does not take over but still pulls the camera back and toward the tube
	const float PullWeight = (Flock->IsPlayerManeuverOverriding() || Flock->GetPlayerManeuver() == EFlockManeuver::Halo)
		? Flock->GetPlayerManeuverWeight() : 0.f;
	const FFlockTornadoParams& MoveShape = Flock->GetPlayerTornadoParams();
	float MoveExtraDistance = 0.f;
	float MoveFocusOnCenter = 0.f;
	float MoveSizeRatio = 1.f;
	bool bFaceMoveAxis = false;
	bool bFollowMoveAxis = false;
	if (Move == EFlockManeuver::Tornado)
	{
		MoveExtraDistance = S.Tornado.CameraExtraDistance;
		MoveFocusOnCenter = S.Tornado.CameraFocusOnCenter;
		MoveSizeRatio = MoveShape.RadiusTop / FMath::Max(S.Tornado.RadiusTop, 1.f);
	}
	else if (Move == EFlockManeuver::Loop)
	{
		MoveExtraDistance = S.Loop.CameraExtraDistance;
		MoveFocusOnCenter = S.Loop.CameraFocusOnCenter;
		MoveSizeRatio = MoveShape.RadiusTop / FMath::Max(S.Loop.Radius, 1.f);
		bFaceMoveAxis = S.Loop.bCameraFacesWheel;
	}
	else if (Move == EFlockManeuver::Halo)
	{
		MoveExtraDistance = S.Halo.CameraExtraDistance;
		MoveFocusOnCenter = S.Halo.CameraFocusOnCenter;
		MoveSizeRatio = MoveShape.RadiusTop / FMath::Max(S.Halo.Radius, 1.f);
	}
	else if (Move == EFlockManeuver::Corkscrew)
	{
		MoveExtraDistance = S.Corkscrew.CameraExtraDistance;
		MoveFocusOnCenter = S.Corkscrew.CameraFocusOnCenter;
		MoveSizeRatio = MoveShape.RadiusTop / FMath::Max(S.Corkscrew.Radius, 1.f);
		bFollowMoveAxis = true;
	}

	// lens and resting angle open up with the flock
	const float CurrentFov = FMath::Lerp(S.FieldOfView, S.FieldOfViewAtFullGroup, Fullness);
	const float CurrentRestingPitch = FMath::Lerp(S.RestingPitch, S.RestingPitchAtFullGroup, Fullness);

	// the view widens while a dash or dive boosts the speed
	{
		const float TargetKick = FMath::Clamp((Player->GetBoostScale() - 1.f) * S.SpeedKickFovPerExtraSpeed, 0.f, S.SpeedKickMaxFov);
		if (bFirstFrame)
		{
			SpeedKickFov = TargetKick;
		}
		else
		{
			const float KickTime = TargetKick > SpeedKickFov ? S.SpeedKickInTime : S.SpeedKickOutTime;
			SpeedKickFov = FMath::Lerp(SpeedKickFov, TargetKick, 1.f - FMath::Exp(-Dt / KickTime));
		}
	}

	if (bFirstFrame)
	{
		FilteredGroupHeading = GroupHeading.GetSafeNormal(1e-4, PlayerHeading);
	}
	else
	{
		const float FilterAlpha = S.GroupHeadingFilterTime > 0.f ? 1.f - FMath::Exp(-Dt / S.GroupHeadingFilterTime) : 1.f;
		FilteredGroupHeading = FMath::Lerp(FilteredGroupHeading, GroupHeading, FilterAlpha).GetSafeNormal(1e-4, FilteredGroupHeading);
	}
	GroupYaw = FlightYawOf(FilteredGroupHeading, GroupYaw);

	// positive while the group climbs
	const float FlightPitch = float(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FilteredGroupHeading.Z, -1.0, 1.0))));

	if (bFirstFrame)
	{
		CameraYaw = GroupYaw;
		CameraYawVelocity = 0.f;
		bFollowingGroupYaw = false;
	}
	else if (ManeuverWeight > 0.001f)
	{
		// hold the view direction; smoothing against its own yaw fades out leftover turning
		bFollowingGroupYaw = false;
		float HoldYaw = CameraYaw;
		if (bFollowMoveAxis)
		{
			// the line turns with the player's steering
			HoldYaw = FlightYawOf(FVector(MoveShape.Axis), CameraYaw);
		}
		else if (bFaceMoveAxis)
		{
			const float AxisYaw = FlightYawOf(FVector(MoveShape.Axis), CameraYaw);
			const float OtherSideYaw = AxisYaw + 180.f;
			HoldYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(CameraYaw, AxisYaw)) <= FMath::Abs(FMath::FindDeltaAngleDegrees(CameraYaw, OtherSideYaw))
				? AxisYaw : OtherSideYaw;
		}
		CameraYaw = FlightSmoothDampAngle(CameraYaw, HoldYaw, CameraYawVelocity, (bFaceMoveAxis || bFollowMoveAxis) ? S.SteerFollowTime : S.IdleFollowTime, Dt);
	}
	else if (FMath::Abs(SteerInput.X) > S.SteerInputThreshold)
	{
		// steering: lead the turn
		bFollowingGroupYaw = false;
		const float AimYaw = GroupYaw + float(SteerInput.X) * S.PredictiveYawAngle;
		CameraYaw = FlightSmoothDampAngle(CameraYaw, AimYaw, CameraYawVelocity, S.SteerFollowTime, Dt);
	}
	else
	{
		const float Gap = FMath::Abs(FMath::FindDeltaAngleDegrees(CameraYaw, GroupYaw));

		// hysteresis: follow past the dead zone angle, stop below the smaller settle angle
		if (!bFollowingGroupYaw && Gap > S.DeadZoneAngle)
		{
			bFollowingGroupYaw = true;
		}
		else if (bFollowingGroupYaw && Gap < S.SettleAngle)
		{
			bFollowingGroupYaw = false;
		}

		if (bFollowingGroupYaw)
		{
			CameraYaw = FlightSmoothDampAngle(CameraYaw, GroupYaw, CameraYawVelocity, S.IdleFollowTime, Dt);
		}
		else
		{
			CameraYaw = FlightSmoothDampAngle(CameraYaw, CameraYaw, CameraYawVelocity, S.IdleFollowTime, Dt);
		}
	}

	float TargetDistance = FMath::Lerp(S.MinDistance, S.MaxDistance, FullnessTarget);

	// a special move pulls the camera back to keep it in view
	TargetDistance += MoveExtraDistance * MoveSizeRatio * PullWeight;

	// make sure the group fits in the picture
	if (S.bFitGroup && GroupSize > 1)
	{
		const float Radius = Flock->GetPlayerGroupRadius(S.GroupFitPercentile);
		const float HalfFovTangent = FMath::Tan(FMath::DegreesToRadians(CurrentFov * 0.5f));
		const float FitDistance = Radius * S.GroupFitMargin / FMath::Max(HalfFovTangent, 0.01f);
		TargetDistance = FMath::Min(FMath::Max(TargetDistance, FitDistance), FMath::Max(S.MaxFitDistance, S.MinDistance));
	}

	const float TargetCenterBlend = FMath::Clamp(S.CenterBlendBySize.GetRichCurveConst()->Eval(float(GroupSize)), 0.f, 1.f);

	if (bFirstFrame)
	{
		CurrentDistance = TargetDistance;
		DistanceVelocity = 0.f;
		CenterBlend = TargetCenterBlend;
		CenterBlendVelocity = 0.f;
	}
	else
	{
		// zoom out faster than in so the camera does not pump as birds join and leave
		const float ZoomTime = TargetDistance > CurrentDistance ? S.ZoomOutSmoothTime : S.ZoomInSmoothTime;
		CurrentDistance = FlightSmoothDamp(CurrentDistance, TargetDistance, DistanceVelocity, ZoomTime, Dt);
		CenterBlend = FlightSmoothDamp(CenterBlend, TargetCenterBlend, CenterBlendVelocity, S.CenterBlendSmoothTime, Dt);
	}

	// the wanted pan: part of the way from the player toward the group center
	const FVector ToGroupCenter = GroupCenter - PlayerPos;
	const FVector DesiredPan = (ToGroupCenter * (S.PanFraction * CenterBlend)).GetClampedToMaxSize(S.MaxPanFraction * CurrentDistance);

	// the goal only moves when the wanted pan leaves CenterDeadRadius, like a leash, so edge birds do not jitter the view
	if (bFirstFrame)
	{
		OffsetGoal = DesiredPan;
		SmoothedOffset = DesiredPan;
		SmoothedOffsetVelocity = FVector::ZeroVector;
	}
	else
	{
		const FVector Away = DesiredPan - OffsetGoal;
		const double AwayLength = Away.Size();
		if (AwayLength > S.CenterDeadRadius)
		{
			OffsetGoal = DesiredPan - (Away / AwayLength) * S.CenterDeadRadius;
		}
		SmoothedOffset = FlightSmoothDamp(SmoothedOffset, OffsetGoal, SmoothedOffsetVelocity, S.CenterSmoothTime, Dt);
	}

	// the point the camera orbits: the player plus the pan
	FVector FramedPoint = PlayerPos + SmoothedOffset;

	// during a special move the focus glides toward the middle of the move
	if (PullWeight > 0.f)
	{
		FramedPoint = FMath::Lerp(FramedPoint, Flock->GetPlayerManeuverCenter(), PullWeight * MoveFocusOnCenter);
	}

	FVector2D LookDelta = FVector2D::ZeroVector;
	const bool bLooking = Player->ConsumeLook(Dt, LookDelta);

	if (bFirstFrame)
	{
		LookYaw = 0.f;
		LookPitch = 0.f;
		TimeSinceLookInput = 0.f;
	}
	else if (bLooking)
	{
		LookYaw = FMath::Clamp(LookYaw + float(LookDelta.X), -S.MaxLookYaw, S.MaxLookYaw);
		LookPitch = FMath::Clamp(LookPitch + float(LookDelta.Y), -S.MaxLookPitch, S.MaxLookPitch);
		TimeSinceLookInput = 0.f;
	}
	else
	{
		TimeSinceLookInput += Dt;

		// 0 until LookReturnDelay has passed, then eases up to 1
		const float Weight = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((TimeSinceLookInput - S.LookReturnDelay) / S.LookReturnBlendTime, 0.f, 1.f));

		// close part of the remaining distance to the rest view
		const float Step = (1.f - FMath::Exp(-Dt * S.LookReturnSpeed)) * Weight;
		LookYaw = FMath::Lerp(LookYaw, 0.f, Step);
		LookPitch = FMath::Lerp(LookPitch, 0.f, Step);
	}

	const float AbovePitch = CurrentRestingPitch - S.PitchFollowFraction * FMath::Clamp(FlightPitch, -S.MaxPitchFollow, S.MaxPitchFollow);

	// pitch is negative when looking down
	const FRotator ViewRotation(-AbovePitch + LookPitch, CameraYaw + LookYaw, 0.f);

	// horizontal forward of the resting yaw, used to push the pivot ahead
	const FVector LevelForward = FRotator(0.f, CameraYaw, 0.f).Vector();
	const FVector Pivot = FramedPoint + LevelForward * (CurrentDistance * S.LookAheadFraction);

	const FVector ViewLocation = Pivot - ViewRotation.Vector() * CurrentDistance;

	OutVT.POV.Location = ViewLocation;
	OutVT.POV.Rotation = ViewRotation;
	// the kick is added last so group fitting uses the calm field of view
	OutVT.POV.FOV = CurrentFov + SpeedKickFov;

	bInitialized = true;
}
