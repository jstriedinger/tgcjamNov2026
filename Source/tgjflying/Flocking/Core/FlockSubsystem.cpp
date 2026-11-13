// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockSubsystem.h"
#include "FlockAgent.h"
#include "FlockBehavior.h"
#include "FlockCallComponent.h"
#include "FlockContainment.h"
#include "FlockContainmentVolume.h"
#include "FlockDataTypes.h"
#include "Async/ParallelFor.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

static TAutoConsoleVariable<bool> CVarFlockShowStats(TEXT("Flock.ShowStats"), false,
	TEXT("Print the agent count and the simulation time on screen"));

static TAutoConsoleVariable<bool> CVarFlockParallel(TEXT("Flock.Parallel"), true,
	TEXT("Simulate the agents with ParallelFor. 0 runs them serially, useful to compare timings"));

static TAutoConsoleVariable<float> CVarFlockLogStatsInterval(TEXT("Flock.LogStatsInterval"), 0.f,
	TEXT("Every this many seconds, print the average simulation time per phase to the log (LogFlock). 0 is off. ")
	TEXT("Can be set in DefaultEngine.ini under [SystemSettings] to measure without typing in the console"));

static TAutoConsoleVariable<int32> CVarFlockDebugAgent(TEXT("Flock.DebugAgent"), -1,
	TEXT("Draw the neighbor reach and body of the agent in this slot, and lines to its neighbors. -1 is off"));

/** A long hitch must not launch the agents across the level */
static constexpr float MaxSimDeltaTime = 0.1f;

static bool MakeContainmentBox(const AFlockContainmentVolume& Volume, FFlockContainmentBox& Box)
{
	FVector Center, Extent;
	FQuat Rotation;
	if (!Volume.GetContainmentBox(Center, Rotation, Extent))
	{
		return false;
	}
	Box.Mode = Volume.Mode == EFlockContainmentMode::SoftReturn ? EFlockContainmentModeView::SoftReturn : EFlockContainmentModeView::InsideMargin;
	Box.Margin = Volume.Margin;
	Box.Weight = Volume.Weight;
	Box.RampDistance = FMath::Max(Volume.RampDistance, 1.f);
	Box.TurnStrength = Volume.TurnStrength;
	Box.Center = FVector3f(Center);
	Box.Extent = FVector3f(Extent);
	Box.Rotation = FQuat4f(Rotation);
	Box.InverseRotation = Box.Rotation.Inverse();
	return true;
}

UFlockSubsystem* UFlockSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UFlockSubsystem>() : nullptr;
}

void UFlockSubsystem::Deinitialize()
{
	Types.Reset();
	TypeIdByDefinition.Reset();
	TypeDefinitions.Reset();
	Actors.Reset();
	FlockVolumes.Reset();
	FlockSpheres.Reset();
	Alive.Reset();
	FreeSlots.Reset();
	NumAlive = 0;
	CallChain = FCallChain();

	Super::Deinitialize();
}

TStatId UFlockSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFlockSubsystem, STATGROUP_Tickables);
}

void UFlockSubsystem::SetFlockScared(const UObject* WorldContextObject, bool bInScared)
{
	if (UFlockSubsystem* Flock = Get(WorldContextObject))
	{
		Flock->SetScared(bInScared);
	}
}

bool UFlockSubsystem::IsFlockScared(const UObject* WorldContextObject)
{
	const UFlockSubsystem* Flock = Get(WorldContextObject);
	return Flock && Flock->bScared;
}

void UFlockSubsystem::SetScared(bool bInScared)
{
	bScared = bInScared;
	if (!bScared)
	{
		return;
	}

	// cut everyone loose so they leave after the grace time, and drop pending joins and answers
	for (int32 Slot = 0; Slot < Alive.Num(); ++Slot)
	{
		if (Alive[Slot] && Slot != PlayerHandle.Index)
		{
			LastConnectedTimes[Slot] = -1e9;
		}
	}
	PendingJoins.Reset();
	CallChain.bActive = false;
}

void UFlockSubsystem::SetFlockContainmentSphere(int32 FlockId, const USphereComponent* Sphere, float Margin, float Weight)
{
	if (Sphere)
	{
		FSphereCage& Cage = FlockSpheres.FindOrAdd(FlockId);
		Cage.Sphere = Sphere;
		Cage.Margin = FMath::Max(Margin, 1.f);
		Cage.Weight = FMath::Max(Weight, 0.f);
	}
	else
	{
		FlockSpheres.Remove(FlockId);
	}
}

void UFlockSubsystem::SetAgentCaged(const FFlockAgentHandle& Handle, bool bCaged)
{
	if (IsHandleValid(Handle))
	{
		Caged[Handle.Index] = bCaged ? 1 : 0;
	}
}

bool UFlockSubsystem::IsAgentCaged(const FFlockAgentHandle& Handle) const
{
	return IsHandleValid(Handle) && Caged[Handle.Index] != 0;
}

void UFlockSubsystem::SetFlockContainment(int32 FlockId, AFlockContainmentVolume* Volume)
{
	if (Volume)
	{
		FlockVolumes.Add(FlockId, Volume);
	}
	else
	{
		FlockVolumes.Remove(FlockId);
	}
}

void UFlockSubsystem::RegisterObstacle(const USphereComponent* Sphere, const FGameplayTagContainer& Tags)
{
	if (!Sphere)
	{
		return;
	}
	UE_LOG(LogFlock, Log, TEXT("Obstacle registered: %s, radius %.0f, tags [%s]"), *Sphere->GetOwner()->GetName(), Sphere->GetScaledSphereRadius(), *Tags.ToStringSimple());
	FObstacleEntry& Entry = ObstacleEntries.AddDefaulted_GetRef();
	Entry.Sphere = Sphere;
	Entry.Tags = Tags;
}

void UFlockSubsystem::UnregisterObstacle(const USphereComponent* Sphere)
{
	ObstacleEntries.RemoveAll([Sphere](const FObstacleEntry& Entry) { return !Entry.Sphere.IsValid() || Entry.Sphere.Get() == Sphere; });
}

int32 UFlockSubsystem::RegisterFlock()
{
	return NextFlockId++;
}

void UFlockSubsystem::UnregisterFlock(int32 FlockId)
{
	FlockVolumes.Remove(FlockId);
	FlockSpheres.Remove(FlockId);

	for (int32 Slot = 0; Slot < Alive.Num(); ++Slot)
	{
		if (Alive[Slot] && FlockIds[Slot] == FlockId)
		{
			RemoveSlot(Slot);
		}
	}
}

int32 UFlockSubsystem::FindOrAddType(UFlockTypeDefinition* Type)
{
	if (const int32* Found = TypeIdByDefinition.Find(Type))
	{
		return *Found;
	}

	const int32 TypeId = Types.AddDefaulted();
	Types[TypeId].Definition = Type;
	TypeDefinitions.Add(Type);
	TypeIdByDefinition.Add(Type, TypeId);
	return TypeId;
}

FFlockAgentHandle UFlockSubsystem::RegisterAgent(AFlockAgent* Agent, UFlockTypeDefinition* Type, int32 FlockId,
	const FVector& Position, const FVector& Direction)
{
	if (!Agent)
	{
		return FFlockAgentHandle();
	}

	// bigger birds keep more room
	Agent->ApplyRandomScale();
	Agent->ApplyRandomShade();
	const FFlockAgentHandle Handle = RegisterActor(Agent, Type, FlockId, Position, Direction, Agent->RollCruiseSpeed(), Agent->GetScaledBodyRadius(),
		Agent->GetMeshComponent());
	if (Handle.IsSet())
	{
		Agent->NotifyJoinedFlock(Handle, FlockId);
	}
	return Handle;
}

FFlockAgentHandle UFlockSubsystem::RegisterActor(AActor* Actor, UFlockTypeDefinition* Type, int32 FlockId,
	const FVector& Position, const FVector& Direction, float CruiseSpeed, float BodyRadius, USceneComponent* BankComponent)
{
	if (!Actor || !Type)
	{
		return FFlockAgentHandle();
	}

	const float Speed = FMath::Max(CruiseSpeed, 0.f);

	int32 Slot;
	if (FreeSlots.Num() > 0)
	{
		Slot = FreeSlots.Pop(EAllowShrinking::No);
	}
	else
	{
		Slot = Alive.Num();
		Positions.AddDefaulted();
		Velocities.AddDefaulted();
		Headings.AddDefaulted();
		NextPositions.AddDefaulted();
		NextVelocities.AddDefaulted();
		Rotations.AddDefaulted();
		Speeds.AddDefaulted();
		InputSteers.AddDefaulted();
		FollowSlots.AddDefaulted();
		InPlayerGroup.AddZeroed();
		Caged.AddZeroed();
		LastConnectedTimes.AddDefaulted();
		HashIndexBySlot.AddDefaulted();
		BodyRadii.AddDefaulted();
		SpeedMultipliers.AddDefaulted();
		Banks.AddDefaulted();
		BankVelocities.AddDefaulted();
		AppliedBanks.AddDefaulted();
		BankComponents.AddDefaulted();
		BankRestRotations.AddDefaulted();
		FlockIds.AddDefaulted();
		TypeIds.AddDefaulted();
		Generations.AddZeroed();
		Alive.AddZeroed();
		Actors.AddDefaulted();
	}

	FVector3f Heading = FVector3f(Direction.GetSafeNormal());
	if (Heading.IsNearlyZero())
	{
		Heading = FVector3f(Actor->GetActorForwardVector());
	}
	Positions[Slot] = FVector3f(Position);
	Velocities[Slot] = Heading * Speed;
	Headings[Slot] = Heading;
	NextPositions[Slot] = Positions[Slot];
	NextVelocities[Slot] = Velocities[Slot];
	Rotations[Slot] = FQuat4f(FRotationMatrix44f::MakeFromX(Heading));
	Speeds[Slot] = Speed;
	InputSteers[Slot] = FVector3f::ZeroVector;
	FollowSlots[Slot] = FVector3f(FMath::FRand(), (FMath::RandBool() ? 1.f : -1.f) * FMath::Lerp(0.3f, 1.f, FMath::FRand()),
		FMath::FRandRange(-1.f, 1.f));
	InPlayerGroup[Slot] = 0;
	Caged[Slot] = 0;
	LastConnectedTimes[Slot] = -1e9;
	BodyRadii[Slot] = FMath::Max(BodyRadius, 0.f);
	SpeedMultipliers[Slot] = 1.f;
	Banks[Slot] = 0.f;
	BankVelocities[Slot] = 0.f;
	AppliedBanks[Slot] = 0.f;
	BankComponents[Slot] = BankComponent;
	BankRestRotations[Slot] = BankComponent ? BankComponent->GetRelativeRotation().Quaternion() : FQuat::Identity;
	FlockIds[Slot] = FlockId;
	TypeIds[Slot] = FindOrAddType(Type);
	Alive[Slot] = 1;
	Actors[Slot] = Actor;
	++NumAlive;

	FFlockAgentHandle Handle;
	Handle.Index = Slot;
	Handle.Generation = int32(Generations[Slot]);
	return Handle;
}

void UFlockSubsystem::RemoveSlot(int32 Slot)
{
	// only flags the slot, never resizes, so it is safe while iterating slots
	check(Alive[Slot]);
	Alive[Slot] = 0;
	InPlayerGroup[Slot] = 0;
	++Generations[Slot];
	Actors[Slot].Reset();
	BankComponents[Slot].Reset();
	FreeSlots.Add(Slot);
	--NumAlive;
}

void UFlockSubsystem::UnregisterAgent(const FFlockAgentHandle& Handle)
{
	if (IsHandleValid(Handle))
	{
		RemoveSlot(Handle.Index);
	}
}

bool UFlockSubsystem::IsHandleValid(const FFlockAgentHandle& Handle) const
{
	return Alive.IsValidIndex(Handle.Index) && Alive[Handle.Index] && int32(Generations[Handle.Index]) == Handle.Generation;
}

FVector UFlockSubsystem::GetAgentHeading(const FFlockAgentHandle& Handle) const
{
	return IsHandleValid(Handle) ? FVector(Headings[Handle.Index]) : FVector::ZeroVector;
}

bool UFlockSubsystem::SetAgentSteer(const FFlockAgentHandle& Handle, const FVector& Steer)
{
	if (!IsHandleValid(Handle))
	{
		return false;
	}
	InputSteers[Handle.Index] = FVector3f(Steer);
	return true;
}

void UFlockSubsystem::SetPlayerAgent(const FFlockAgentHandle& Handle, float MemberGraceTime)
{
	PlayerHandle = Handle;
	PlayerMemberGraceTime = FMath::Max(MemberGraceTime, 0.f);
	PlayerUrge = FVector3f::ZeroVector;
	PlayerSpeedScale = 1.f;
	PlayerSpeedScaleLast = 1.f;
	PlayerSpeedScaleStep = 1.f;
	PlayerAgilityScale = 1.f;
	ManeuverType = EFlockManeuver::None;
	ManeuverWeight = 0.f;
}

bool UFlockSubsystem::StartPlayerTornado(const FFlockTornadoParams& Params, float Duration, float BlendIn, float BlendOut)
{
	return StartOrbit(EFlockManeuver::Tornado, Params, Duration, BlendIn, BlendOut);
}

bool UFlockSubsystem::StartPlayerLoop(const FFlockTornadoParams& Params, float Duration, float BlendIn, float BlendOut)
{
	return StartOrbit(EFlockManeuver::Loop, Params, Duration, BlendIn, BlendOut);
}

bool UFlockSubsystem::StartPlayerHalo(const FFlockTornadoParams& Params, float Duration, float BlendIn, float BlendOut)
{
	return StartOrbit(EFlockManeuver::Halo, Params, Duration, BlendIn, BlendOut);
}

bool UFlockSubsystem::StartPlayerCorkscrew(const FFlockTornadoParams& Params, float Duration, float BlendIn, float BlendOut)
{
	return StartOrbit(EFlockManeuver::Corkscrew, Params, Duration, BlendIn, BlendOut);
}

FVector UFlockSubsystem::GetPlayerGuideHeading() const
{
	if (ManeuverType == EFlockManeuver::Corkscrew)
	{
		return FVector(TornadoParams.Axis);
	}
	return IsHandleValid(PlayerHandle) ? FVector(Headings[PlayerHandle.Index]) : FVector::XAxisVector;
}

bool UFlockSubsystem::StartPlayerRipple(const FFlockRippleParams& Params, float Duration, float BlendIn, float BlendOut)
{
	if (!IsHandleValid(PlayerHandle) || Duration <= 0.f)
	{
		return false;
	}

	RippleParams = Params;
	ManeuverType = EFlockManeuver::Ripple;
	ManeuverStartTime = GetWorld()->GetTimeSeconds();
	ManeuverDuration = Duration;
	ManeuverBlendIn = FMath::Max(BlendIn, 0.f);
	ManeuverBlendOut = FMath::Max(BlendOut, 0.f);
	ManeuverWeight = 0.f;
	return true;
}

bool UFlockSubsystem::StartOrbit(EFlockManeuver Type, const FFlockTornadoParams& InParams, float Duration, float BlendIn, float BlendOut)
{
	if (!IsHandleValid(PlayerHandle) || Duration <= 0.f)
	{
		return false;
	}

	const int32 PlayerSlot = PlayerHandle.Index;
	const FVector3f PlayerPos = Positions[PlayerSlot];
	const FVector3f Heading = Headings[PlayerSlot];

	FFlockTornadoParams Params = InParams;

	// circling axis and the direction from the player to the center
	FVector3f FlatRight(-Heading.Y, Heading.X, 0.f);
	FlatRight = FlatRight.SizeSquared() > 1e-6f ? FlatRight.GetSafeNormal() : FVector3f::YAxisVector;
	FVector3f ToCenter;
	if (Type == EFlockManeuver::Loop)
	{
		Params.Axis = FlatRight;
		Params.bTurnRight = false;
		ToCenter = FVector3f::CrossProduct(Heading, FlatRight).GetSafeNormal(1e-4f, FVector3f::ZAxisVector);
	}
	else if (Type == EFlockManeuver::Halo)
	{
		// tube along the player's heading, center ahead of the player
		Params.Axis = Heading;
		ToCenter = FVector3f::ZeroVector;
	}
	else if (Type == EFlockManeuver::Corkscrew)
	{
		// wheel axis is the heading, center one radius above the player
		Params.Axis = Heading;
		ToCenter = FVector3f::CrossProduct(Heading, FlatRight).GetSafeNormal(1e-4f, FVector3f::ZAxisVector);
	}
	else
	{
		Params.Axis = FVector3f::ZAxisVector;
		ToCenter = FlatRight * (Params.bTurnRight ? 1.f : -1.f);
	}

	// the center sits a radius from the player, shifted along the axis by the player's place on it
	const FVector3f PlayerSlotValue = FollowSlots[PlayerSlot];
	const float Radius = FlockManeuvers::TornadoRadius(Params, PlayerSlotValue);
	FVector3f Center = PlayerPos + ToCenter * Radius
		- Params.Axis * ((FlockManeuvers::TornadoHeightFraction(PlayerSlotValue) - 0.5f) * Params.Height);
	if (Type == EFlockManeuver::Halo)
	{
		Center = PlayerPos + Heading * Params.StartAhead;
	}

	float FinaleRise = Params.FinaleRise;

	// keep the whole shape inside the containment volume
	if (const TWeakObjectPtr<AFlockContainmentVolume>* Volume = FlockVolumes.Find(FlockIds[PlayerSlot]))
	{
		FFlockContainmentBox Box;
		if (Volume->Get() && MakeContainmentBox(*Volume->Get(), Box))
		{
			const float WidestRadius = Params.RadiusTop * (1.f + Params.RadiusJitter);
			float PadFlat;
			float PadUp;
			if (Type == EFlockManeuver::Loop)
			{
				// a wheel reaches a radius up, down and ahead, half its width sideways
				PadFlat = WidestRadius + Params.Height * 0.5f + 200.f;
				PadUp = WidestRadius + Params.BobHeight + 200.f;
			}
			else if (Type == EFlockManeuver::Corkscrew || Type == EFlockManeuver::Halo)
			{
				// a tube reaches a radius all round, its length is not counted
				PadFlat = WidestRadius + 250.f;
				PadUp = WidestRadius + 250.f;
			}
			else
			{
				PadFlat = WidestRadius + 200.f;
				PadUp = Params.Height * 0.5f + Params.BobHeight + 200.f;
			}

			FVector3f Local = Box.InverseRotation.RotateVector(Center - Box.Center);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float Limit = FMath::Max(Box.Extent[Axis] - (Axis == 2 ? PadUp : PadFlat), 0.f);
				Local[Axis] = FMath::Clamp(Local[Axis], -Limit, Limit);
			}
			Center = Box.Center + Box.Rotation.RotateVector(Local);

			// the finale climbs only as far as the room under the ceiling
			if (FinaleRise > 0.f && Type == EFlockManeuver::Tornado)
			{
				const float TopOfColumn = Local.Z + Params.Height * 0.5f + Params.BobHeight;
				FinaleRise = FMath::Clamp(Box.Extent.Z - TopOfColumn - 300.f, 0.f, FinaleRise);
			}
		}
	}

	TornadoParams = Params;
	TornadoParams.FinaleRise = FinaleRise;
	ManeuverCenter = Center;
	ManeuverAxialSpeed = 0.f;
	ManeuverType = Type;
	ManeuverStartTime = GetWorld()->GetTimeSeconds();
	ManeuverDuration = Duration;
	ManeuverBlendIn = FMath::Max(BlendIn, 0.f);
	ManeuverBlendOut = FMath::Max(BlendOut, 0.f);
	ManeuverWeight = 0.f;
	return true;
}

void UFlockSubsystem::UpdateManeuver(double Now, float DeltaTime)
{
	if (ManeuverType == EFlockManeuver::None)
	{
		ManeuverWeight = 0.f;
		return;
	}
	if (!IsHandleValid(PlayerHandle))
	{
		ManeuverType = EFlockManeuver::None;
		ManeuverWeight = 0.f;
		return;
	}

	const float Elapsed = float(Now - ManeuverStartTime);
	if (Elapsed >= ManeuverDuration)
	{
		ManeuverType = EFlockManeuver::None;
		ManeuverWeight = 0.f;
		return;
	}

	// up over BlendIn, down over the last BlendOut, whichever is lower
	const float In = ManeuverBlendIn > 0.f ? FMath::Clamp(Elapsed / ManeuverBlendIn, 0.f, 1.f) : 1.f;
	const float Out = ManeuverBlendOut > 0.f ? FMath::Clamp((ManeuverDuration - Elapsed) / ManeuverBlendOut, 0.f, 1.f) : 1.f;
	ManeuverWeight = FMath::SmoothStep(0.f, 1.f, FMath::Min(In, Out));

	// the axis turns with the player's steering, the center moves forward along it
	if (ManeuverType == EFlockManeuver::Corkscrew || ManeuverType == EFlockManeuver::Halo)
	{
		// the halo keeps its axis fixed and only its center moves
		const bool bSteered = ManeuverType == EFlockManeuver::Corkscrew;
		const int32 PlayerSlot = PlayerHandle.Index;
		FVector3f Guide = bSteered
			? (TornadoParams.Axis + PlayerUrge * DeltaTime).GetSafeNormal(1e-4f, TornadoParams.Axis)
			: TornadoParams.Axis;
		const float PlayerSpeed = Speeds[PlayerSlot] * SpeedMultipliers[PlayerSlot];
		ManeuverAxialSpeed = PlayerSpeed * TornadoParams.ForwardSpeedFraction;
		FVector3f NewCenter = ManeuverCenter + Guide * (ManeuverAxialSpeed * DeltaTime);

		// the guide stays inside the volume: at a wall it slides along it
		if (const TWeakObjectPtr<AFlockContainmentVolume>* Volume = FlockVolumes.Find(FlockIds[PlayerSlot]))
		{
			FFlockContainmentBox Box;
			if (Volume->Get() && MakeContainmentBox(*Volume->Get(), Box))
			{
				const float Pad = TornadoParams.RadiusTop * (1.f + TornadoParams.RadiusJitter) + 250.f;
				FVector3f Local = Box.InverseRotation.RotateVector(NewCenter - Box.Center);
				FVector3f LocalDir = Box.InverseRotation.RotateVector(Guide);
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					const float Limit = FMath::Max(Box.Extent[Axis] - Pad, 0.f);
					if (Local[Axis] > Limit)
					{
						Local[Axis] = Limit;
						LocalDir[Axis] = FMath::Min(LocalDir[Axis], 0.f);
					}
					else if (Local[Axis] < -Limit)
					{
						Local[Axis] = -Limit;
						LocalDir[Axis] = FMath::Max(LocalDir[Axis], 0.f);
					}
				}
				NewCenter = Box.Center + Box.Rotation.RotateVector(Local);
				Guide = Box.Rotation.RotateVector(LocalDir).GetSafeNormal(1e-4f, Guide);
			}
		}

		if (bSteered)
		{
			TornadoParams.Axis = Guide;
		}
		ManeuverCenter = NewCenter;
	}
}

float UFlockSubsystem::GetPlayerGroupRadius(float Percentile)
{
	if (PlayerGroupDistances.IsEmpty())
	{
		return 0.f;
	}

	// sorted lazily on the first call each frame
	if (!bPlayerGroupDistancesSorted)
	{
		PlayerGroupDistances.Sort();
		bPlayerGroupDistancesSorted = true;
	}

	const int32 Last = PlayerGroupDistances.Num() - 1;
	const int32 Index = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Percentile, 0.f, 1.f) * float(Last)), 0, Last);
	return PlayerGroupDistances[Index];
}

void UFlockSubsystem::SetPlayerJoinRadius(float JoinRadius, float LeaveRadius)
{
	PlayerJoinRadius = FMath::Max(JoinRadius, 0.f);
	PlayerLeaveRadius = FMath::Max(LeaveRadius, PlayerJoinRadius);
}

void UFlockSubsystem::SetPlayerFollow(const FFlockFollowSettings& Settings)
{
	PlayerFollow = Settings;
}

void UFlockSubsystem::SetPlayerSpeedScale(float Scale)
{
	// never zero: a stopped bird has no heading
	PlayerSpeedScale = FMath::Max(Scale, 0.05f);
}

void UFlockSubsystem::SetPlayerAgilityScale(float Scale)
{
	PlayerAgilityScale = FMath::Max(Scale, 0.1f);
}

void UFlockSubsystem::SetPlayerUrge(const FVector& Urge)
{
	PlayerUrge = FVector3f(Urge);
}

bool UFlockSubsystem::IsAgentInPlayerGroup(const FFlockAgentHandle& Handle) const
{
	return IsHandleValid(Handle) && InPlayerGroup[Handle.Index] != 0;
}

bool UFlockSubsystem::IsAgentOutsideContainment(const FFlockAgentHandle& Handle) const
{
	if (!IsHandleValid(Handle))
	{
		return false;
	}
	const TWeakObjectPtr<AFlockContainmentVolume>* Volume = FlockVolumes.Find(FlockIds[Handle.Index]);
	FFlockContainmentBox Box;
	if (!Volume || !Volume->Get() || !MakeContainmentBox(*Volume->Get(), Box))
	{
		return false;
	}
	return FlockContainment::IsOutside(Box, Positions[Handle.Index]);
}

float UFlockSubsystem::GetAgentSpeed(const FFlockAgentHandle& Handle) const
{
	return IsHandleValid(Handle) ? Speeds[Handle.Index] : 0.f;
}

void UFlockSubsystem::SetPlayerSettings(const UFlockPlayerSettings* InSettings)
{
	PlayerSettings = InSettings;
}

const UFlockPlayerSettings* UFlockSubsystem::GetPlayerSettings() const
{
	return PlayerSettings.Get();
}

FVector UFlockSubsystem::GetAgentVelocity(const FFlockAgentHandle& Handle) const
{
	return IsHandleValid(Handle) ? FVector(Velocities[Handle.Index]) : FVector::ZeroVector;
}

bool UFlockSubsystem::SetAgentSpeed(const FFlockAgentHandle& Handle, float Speed)
{
	if (!IsHandleValid(Handle))
	{
		return false;
	}
	Speeds[Handle.Index] = FMath::Max(Speed, 0.f);
	return true;
}


/** The call profile of the running chain, or the defaults */
const UFlockCallProfile& UFlockSubsystem::GetChainProfile() const
{
	const UFlockCallProfile* Profile = CallChain.Profile.Get();
	return Profile ? *Profile : *GetDefault<UFlockCallProfile>();
}

static bool SameHandle(const FFlockAgentHandle& A, const FFlockAgentHandle& B)
{
	return A.Index == B.Index && A.Generation == B.Generation;
}

void UFlockSubsystem::StartCallChain(const FFlockAgentHandle& Caller, const UFlockCallProfile* CallerProfile)
{
	// a new call replaces a running chain
	CallChain = FCallChain();
	if (bScared)
	{
		return;
	}
	CallChain.Profile = CallerProfile;
	if (!IsHandleValid(Caller) || GetChainProfile().MaxChainLength <= 0)
	{
		return;
	}

	CallChain.bActive = true;
	CallChain.Called.Add(Caller);
	ScheduleNextAnswer(Positions[Caller.Index], GetWorld()->GetTimeSeconds());
}

void UFlockSubsystem::ScheduleNextAnswer(const FVector3f& From, double Now)
{
	const float HearingRadiusSquared = FMath::Square(GetChainProfile().HearingRadius);
	CallChain.LastCallerPosition = From;
	CallChain.Next = FFlockAgentHandle();

	// every bird that hears the call rolls a delay, the quickest answers
	float BestDelay = TNumericLimits<float>::Max();
	for (int32 Slot = 0; Slot < Alive.Num(); ++Slot)
	{
		if (!Alive[Slot] || !InPlayerGroup[Slot] || Slot == PlayerHandle.Index)
		{
			continue;
		}
		if (FVector3f::DistSquared(Positions[Slot], From) > HearingRadiusSquared)
		{
			continue;
		}

		FFlockAgentHandle Candidate;
		Candidate.Index = Slot;
		Candidate.Generation = int32(Generations[Slot]);
		if (CallChain.Called.ContainsByPredicate([&Candidate](const FFlockAgentHandle& H) { return SameHandle(H, Candidate); }))
		{
			continue;
		}

		// only flock birds have a call and an answer delay
		const AFlockAgent* Agent = Cast<AFlockAgent>(Actors[Slot].Get());
		if (!Agent || !Agent->GetCallComponent())
		{
			continue;
		}

		const FFloatInterval DelayRange = Agent->GetAnswerDelayRange();
		const float Delay = FMath::FRandRange(FMath::Max(DelayRange.Min, 0.f), FMath::Max(DelayRange.Max, DelayRange.Min));
		if (Delay < BestDelay)
		{
			BestDelay = Delay;
			CallChain.Next = Candidate;
		}
	}

	if (!CallChain.Next.IsSet())
	{
		// nobody heard it, the chain ends
		CallChain.bActive = false;
		return;
	}
	CallChain.NextTime = Now + BestDelay;
}

void UFlockSubsystem::RecruitOnCall(const FFlockAgentHandle& Caller)
{
	if (bScared)
	{
		return;
	}
	if (!IsHandleValid(Caller) || !IsHandleValid(PlayerHandle) || Caller.Index != PlayerHandle.Index)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const FVector3f PlayerPos = Positions[PlayerHandle.Index];
	const int32 PlayerTypeId = TypeIds[PlayerHandle.Index];
	const float JoinRadiusSquared = FMath::Square(PlayerJoinRadius);

	for (int32 Slot = 0; Slot < Alive.Num(); ++Slot)
	{
		if (!Alive[Slot] || InPlayerGroup[Slot] || TypeIds[Slot] != PlayerTypeId || Slot == PlayerHandle.Index)
		{
			continue;
		}
		// caged birds can only be freed
		if (Caged[Slot])
		{
			continue;
		}
		const float DistSquared = FVector3f::DistSquared(Positions[Slot], PlayerPos);
		if (DistSquared > JoinRadiusSquared)
		{
			continue;
		}

		FFlockAgentHandle Candidate;
		Candidate.Index = Slot;
		Candidate.Generation = int32(Generations[Slot]);
		if (PendingJoins.ContainsByPredicate([Slot](const FPendingJoin& P) { return P.Handle.Index == Slot; }))
		{
			continue;
		}

		// only flock birds have a call and an answer delay
		const AFlockAgent* Agent = Cast<AFlockAgent>(Actors[Slot].Get());
		if (!Agent || !Agent->GetCallComponent())
		{
			continue;
		}

		const FFloatInterval DelayRange = Agent->GetAnswerDelayRange();
		const float Roll = FMath::FRandRange(FMath::Max(DelayRange.Min, 0.f), FMath::Max(DelayRange.Max, DelayRange.Min));
		const float DistanceDelay = 0.5f * FMath::Sqrt(DistSquared) / FMath::Max(PlayerJoinRadius, 1.f);

		FPendingJoin Pending;
		Pending.Handle = Candidate;
		Pending.Time = Now + Roll + DistanceDelay;
		PendingJoins.Add(Pending);
	}
}

void UFlockSubsystem::UpdateCallableGlow()
{
	const bool bHavePlayer = IsHandleValid(PlayerHandle);
	const FVector3f PlayerPos = bHavePlayer ? Positions[PlayerHandle.Index] : FVector3f::ZeroVector;
	const int32 PlayerTypeId = bHavePlayer ? TypeIds[PlayerHandle.Index] : INDEX_NONE;
	const float JoinRadiusSquared = FMath::Square(PlayerJoinRadius);

	// same test as RecruitOnCall
	for (int32 Slot = 0; Slot < Alive.Num(); ++Slot)
	{
		if (!Alive[Slot] || (bHavePlayer && Slot == PlayerHandle.Index))
		{
			continue;
		}
		AFlockAgent* Agent = Cast<AFlockAgent>(Actors[Slot].Get());
		if (!Agent)
		{
			continue;
		}

		const bool bCallable = bHavePlayer && !bScared && !InPlayerGroup[Slot] && !Caged[Slot]
			&& TypeIds[Slot] == PlayerTypeId
			&& FVector3f::DistSquared(Positions[Slot], PlayerPos) <= JoinRadiusSquared
			&& Agent->GetCallComponent() != nullptr;
		Agent->SetCallableGlow(bCallable);
	}
}

void UFlockSubsystem::UpdateRecruits(double Now)
{
	for (int32 Index = PendingJoins.Num() - 1; Index >= 0; --Index)
	{
		const FPendingJoin Pending = PendingJoins[Index];
		if (Now < Pending.Time)
		{
			continue;
		}
		PendingJoins.RemoveAtSwap(Index);

		// gone or already a member
		if (!IsHandleValid(Pending.Handle) || InPlayerGroup[Pending.Handle.Index])
		{
			continue;
		}

		// it answers with its own call and joins
		LastConnectedTimes[Pending.Handle.Index] = Now;
		if (const AFlockAgent* Agent = Cast<AFlockAgent>(Actors[Pending.Handle.Index].Get()))
		{
			Agent->GetCallComponent()->Call();
		}
	}
}

void UFlockSubsystem::UpdateCallChain(double Now)
{
	if (!CallChain.bActive || Now < CallChain.NextTime)
	{
		return;
	}

	// the waiting bird left: someone else who heard the last call answers instead
	if (!IsAgentInPlayerGroup(CallChain.Next))
	{
		ScheduleNextAnswer(CallChain.LastCallerPosition, Now);
		return;
	}

	const FFlockAgentHandle Answering = CallChain.Next;
	CallChain.Called.Add(Answering);
	++CallChain.Answers;
	if (const AFlockAgent* Agent = Cast<AFlockAgent>(Actors[Answering.Index].Get()))
	{
		Agent->GetCallComponent()->Call();
	}

	if (CallChain.Answers >= GetChainProfile().MaxChainLength)
	{
		CallChain.bActive = false;
		return;
	}

	ScheduleNextAnswer(Positions[Answering.Index], Now);
}

void UFlockSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (NumAlive == 0)
	{
		return;
	}

	DeltaTime = FMath::Min(DeltaTime, MaxSimDeltaTime);
	if (DeltaTime <= 0.f)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(Flock_Tick);
	const double StartTime = FPlatformTime::Seconds();
	double GroupStartTime = StartTime;
	double GroupEndTime = StartTime;

	FFlockFrameContext Context;
	Context.DeltaTime = DeltaTime;
	Context.TimeSeconds = GetWorld()->GetTimeSeconds();

	// volumes move and scale, so re-read them every frame
	for (const TPair<int32, TWeakObjectPtr<AFlockContainmentVolume>>& Pair : FlockVolumes)
	{
		const AFlockContainmentVolume* Volume = Pair.Value.Get();
		FFlockContainmentBox Box;
		if (Volume && MakeContainmentBox(*Volume, Box))
		{
			Context.ContainmentByFlock.Add(Pair.Key, Box);
		}
	}

	// sphere cages move and scale too
	for (const TPair<int32, FSphereCage>& Pair : FlockSpheres)
	{
		if (const USphereComponent* Sphere = Pair.Value.Sphere.Get())
		{
			FFlockContainmentSphere& Cage = Context.ContainmentSphereByFlock.Add(Pair.Key);
			Cage.Center = FVector3f(Sphere->GetComponentLocation());
			Cage.Radius = Sphere->GetScaledSphereRadius();
			Cage.Margin = Pair.Value.Margin;
			Cage.Weight = Pair.Value.Weight;
		}
	}

	// obstacle spheres move and grow too
	for (const FObstacleEntry& Entry : ObstacleEntries)
	{
		if (const USphereComponent* Sphere = Entry.Sphere.Get())
		{
			FFlockObstacleSource& Source = Context.Obstacles.AddDefaulted_GetRef();
			Source.Center = FVector3f(Sphere->GetComponentLocation());
			Source.Radius = Sphere->GetScaledSphereRadius();
			Source.Tags = Entry.Tags;
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Flock_Prepare);

		// refresh from the definition so edits show up live
		for (FTypeRuntime& Type : Types)
		{
			const UFlockTypeDefinition* Definition = Type.Definition;
			Type.NeighborRadius = FMath::Max(Definition->NeighborRadius, 1.f);
			Type.MaxNeighbors = FMath::Clamp(Definition->MaxNeighbors, 1, FlockLimits::MaxNeighbors);
			Type.MaxSteerForce = FMath::Max(Definition->MaxSteerForce, 0.f);
			Type.MaxExternalForce = Type.MaxSteerForce * FMath::Max(Definition->ExternalForceMultiplier, 0.f);
			Type.SpeedBoostInRate = FMath::Max(Definition->SpeedBoostInRate, 0.f);
			Type.SpeedBoostOutRate = FMath::Max(Definition->SpeedBoostOutRate, 0.f);
			Type.TurnSpeed = FMath::Max(Definition->TurnSpeed, 0.f);
			Type.bBanking = Definition->bBanking;
			Type.Flee.AvoidRadius = Definition->ShyAvoidRadius;
			Type.Flee.Weight = Definition->ShyAvoidWeight;
			Type.Flee.SpeedMultiplier = Definition->ShySpeedMultiplier;
			Type.MaxBankAngle = FMath::Max(Definition->MaxBankAngle, 0.f);
			Type.BankPerTurnRate = FMath::Max(Definition->BankPerTurnRate, 0.f);
			Type.BankSmoothTime = FMath::Max(Definition->BankSmoothTime, 0.01f);
			Type.MaxBodyRadius = 0.f;
			Type.Slots.Reset();
			Type.Positions.Reset();
			Type.Behaviors.Reset();
			for (UFlockBehavior* Behavior : Definition->Behaviors)
			{
				if (Behavior)
				{
					Behavior->PrepareFrame(Context);
					Type.Behaviors.Add(Behavior);
				}
			}
		}

		Work.Reset();
		for (int32 Slot = 0; Slot < Alive.Num(); ++Slot)
		{
			if (Alive[Slot])
			{
				FTypeRuntime& Type = Types[TypeIds[Slot]];
				HashIndexBySlot[Slot] = Type.Slots.Num();
				Work.Add({ TypeIds[Slot], Type.Slots.Num() });
				Type.Slots.Add(Slot);
				Type.Positions.Add(Positions[Slot]);
				Type.MaxBodyRadius = FMath::Max(Type.MaxBodyRadius, BodyRadii[Slot]);
			}
		}

		// the query must reach two of the biggest bodies at the neighbor radius
		for (FTypeRuntime& Type : Types)
		{
			Type.QueryRadius = Type.NeighborRadius + 2.f * Type.MaxBodyRadius;
			Type.Hash.Build(Type.Positions, Type.QueryRadius);
		}

		UpdateManeuver(Context.TimeSeconds, DeltaTime);

		GroupStartTime = FPlatformTime::Seconds();
		UpdatePlayerGroup(Context.TimeSeconds);
		GroupEndTime = FPlatformTime::Seconds();
	}

	const double SimulateStartTime = FPlatformTime::Seconds();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Flock_Simulate);

		// the group follows the player's speed scale change at once, the player smooths it itself
		PlayerSpeedScaleStep = PlayerSpeedScale / FMath::Max(PlayerSpeedScaleLast, 0.05f);
		PlayerSpeedScaleLast = PlayerSpeedScale;

		const EParallelForFlags Flags = CVarFlockParallel.GetValueOnGameThread() ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread;
		ParallelFor(TEXT("Flock.Simulate"), Work.Num(), 32, [this, &Context](int32 WorkIndex)
		{
			const FWorkItem& Item = Work[WorkIndex];
			SimulateAgent(Context, Types[Item.TypeId], Item.TypeId, Item.Local);
		}, Flags);
	}

	const double ApplyStartTime = FPlatformTime::Seconds();
	ApplyToActors();
	NotifyGroupChanges();
	UpdateCallChain(Context.TimeSeconds);
	UpdateRecruits(Context.TimeSeconds);
	UpdateCallableGlow();
	const double EndTime = FPlatformTime::Seconds();

	RecordStats((GroupStartTime - StartTime) * 1000.0, (GroupEndTime - GroupStartTime) * 1000.0,
		(ApplyStartTime - SimulateStartTime) * 1000.0, (EndTime - ApplyStartTime) * 1000.0, DeltaTime);

	const double SimMs = (EndTime - StartTime) * 1000.0;
	SmoothedSimMs = FMath::Lerp(SmoothedSimMs, SimMs, 0.05);
	// debug drawing, all behind the master switch
	if (bDebugDrawEnabled && CVarFlockShowStats.GetValueOnGameThread())
	{
		DrawStats(SmoothedSimMs);
	}

	const int32 DebugSlot = CVarFlockDebugAgent.GetValueOnGameThread();
	if (bDebugDrawEnabled && Alive.IsValidIndex(DebugSlot) && Alive[DebugSlot])
	{
		DrawDebugAgent(DebugSlot);
	}
}

/** Critically damped follow of a target, without a speed cap */
static float SmoothDamp(float Current, float Target, float& Velocity, float SmoothTime, float DeltaTime)
{
	const float Omega = 2.f / SmoothTime;
	const float X = Omega * DeltaTime;
	const float Falloff = 1.f / (1.f + X + 0.48f * X * X + 0.235f * X * X * X);
	const float Change = Current - Target;
	const float Temp = (Velocity + Omega * Change) * DeltaTime;
	Velocity = (Velocity - Omega * Temp) * Falloff;
	return Target + (Change + Temp) * Falloff;
}

/** Reads the current buffers, writes only its own slot of the next-step arrays */
void UFlockSubsystem::SimulateAgent(const FFlockFrameContext& Context, const FTypeRuntime& Type, int32 TypeId, int32 Local)
{
	const int32 Slot = Type.Slots[Local];
	const float DeltaTime = Context.DeltaTime;

	FFlockAgentView Agent;
	Agent.Pos = Positions[Slot];
	Agent.Vel = Velocities[Slot];
	Agent.Heading = Agent.Vel.GetSafeNormal(1e-6f, Headings[Slot]);
	Agent.Speed = Speeds[Slot];
	Agent.BodyRadius = BodyRadii[Slot];
	Agent.NeighborRadius = Type.NeighborRadius;
	Agent.MaxNeighbors = Type.MaxNeighbors;
	Agent.Slot = Slot;
	Agent.FlockId = FlockIds[Slot];
	Agent.TypeId = TypeId;

	FFlockNeighbors Neighbors;
	Type.Hash.QueryRadius(Agent.Pos, Type.QueryRadius, [&](int32 HashIndex, const FVector3f& Pos, float DistSq)
	{
		const int32 OtherSlot = Type.Slots[HashIndex];
		FFlockNeighbor& Other = Neighbors.Flockmates[Neighbors.NumFlockmates++];
		Other.Pos = Pos;
		Other.Vel = Velocities[OtherSlot];
		Other.DistSq = DistSq;
		Other.BodyRadius = BodyRadii[OtherSlot];
		Other.Slot = OtherSlot;
		return Neighbors.NumFlockmates < FlockLimits::MaxNeighbors;
	}, Local);

	FFlockSteering Steering;
	for (const UFlockBehavior* Behavior : Type.Behaviors)
	{
		Behavior->Accumulate(Context, Agent, Neighbors, Steering);
	}

	Steering.Steer += InputSteers[Slot];
	if (InPlayerGroup[Slot])
	{
		Steering.Steer += PlayerUrge;
	}

	// each member has its own fixed place behind the player
	float FollowSpeedFactor = 1.f;
	if (InPlayerGroup[Slot] && PlayerFollow.bEnabled && Slot != PlayerHandle.Index)
	{
		const FVector3f& Place = FollowSlots[Slot];
		const float Depth = FMath::Lerp(PlayerFollow.MinDepth, PlayerFollow.MaxDepth, Place.X);
		FVector3f SlotPos = FollowOrigin - FollowForward * Depth
			+ FollowRight * (Place.Y * Depth * PlayerFollow.SideSpread)
			+ FVector3f(0.f, 0.f, Place.Z * Depth * PlayerFollow.VerticalSpread);

		// a wave runs through the V, moving the places up and down
		float FollowWeight = PlayerFollow.Weight;
		if (ManeuverType == EFlockManeuver::Ripple && ManeuverWeight > 0.f)
		{
			SlotPos.Z += FlockManeuvers::RippleOffset(RippleParams, Depth, Context.TimeSeconds, ManeuverWeight);
			FollowWeight *= FMath::Lerp(1.f, RippleParams.FollowBoost, ManeuverWeight);
		}

		const FVector3f ToSlot = SlotPos - Agent.Pos;

		Steering.Steer += (ToSlot / FMath::Max(PlayerFollow.FullForceDistance, 1.f)).GetClampedToMaxSize(1.f) * FollowWeight;

		// positive when the bird is behind its place along the heading
		const float Lag = FVector3f::DotProduct(ToSlot, FollowForward);
		FollowSpeedFactor = 1.f + FMath::Clamp(Lag / FMath::Max(PlayerFollow.SpeedAdjustDistance, 1.f), -1.f, 1.f) * PlayerFollow.SpeedAdjust;
	}

	// scared birds flee the player, harder the closer it is, and bolt while inside the radius
	if (bScared && IsHandleValid(PlayerHandle) && Slot != PlayerHandle.Index)
	{
		const FVector3f Away = Agent.Pos - Positions[PlayerHandle.Index];
		const float AwayDistance = Away.Size();
		if (AwayDistance < Type.Flee.AvoidRadius && AwayDistance > 1.f)
		{
			const float Closeness = 1.f - AwayDistance / FMath::Max(Type.Flee.AvoidRadius, 1.f);
			// fleeing replaces flocking: 40% at the edge of the radius, all of it on top of the bird
			const float Replace = FMath::Lerp(0.4f, 1.f, Closeness);
			Steering.Steer = FMath::Lerp(Steering.Steer, (Away / AwayDistance) * Type.Flee.Weight, Replace);
			// full bolt speed anywhere inside the radius
			const float OwnSpeed = FMath::Max(Agent.Speed, 1.f);
			const float PlayerSpeed = Speeds[PlayerHandle.Index];
			const float FleeSpeed = Type.Flee.SpeedMultiplier * FMath::Max(OwnSpeed, PlayerSpeed);
			Steering.SpeedMultiplier = FMath::Max(Steering.SpeedMultiplier, FleeSpeed / OwnSpeed);
		}
	}

	// ripple: the player is not in the V, so it rides the wave at the front
	if (ManeuverType == EFlockManeuver::Ripple && ManeuverWeight > 0.f && Slot == PlayerHandle.Index)
	{
		const float Omega = 6.2831853f / FMath::Max(RippleParams.Period, 0.05f);
		const float Speed = FMath::Max(Agent.Speed * SpeedMultipliers[Slot], 100.f);
		// capped at 60% of the speed so a big wave does not throw the player into a dive
		const float WantedClimb = FMath::Clamp(RippleParams.Amplitude * ManeuverWeight * Omega * FMath::Cos(Omega * Context.TimeSeconds), -0.6f * Speed, 0.6f * Speed);
		Steering.Steer.Z += RippleParams.PlayerGain * (WantedClimb - Agent.Vel.Z) / Speed;
	}

	// special move of the player's group
	if (InPlayerGroup[Slot] && ManeuverWeight > 0.f && (IsPlayerManeuverOverriding() || (ManeuverType == EFlockManeuver::Halo && Slot != PlayerHandle.Index)))
	{
		const float CurrentSpeed = Agent.Speed * SpeedMultipliers[Slot];
		const float TimeLeft = ManeuverDuration - float(Context.TimeSeconds - ManeuverStartTime);
		const FVector3f ManeuverSteer = FlockManeuvers::TornadoSteer(TornadoParams, ManeuverCenter, Agent, FollowSlots[Slot], CurrentSpeed, Context.TimeSeconds, TimeLeft, ManeuverAxialSpeed);
		Steering.Steer = FMath::Lerp(Steering.Steer, ManeuverSteer, ManeuverWeight);
		FollowSpeedFactor = FMath::Lerp(FollowSpeedFactor, 1.f, ManeuverWeight);
	}

	if (const FFlockContainmentBox* Box = Context.ContainmentByFlock.Find(Agent.FlockId))
	{
		FlockContainment::Accumulate(*Box, Agent, Steering);
	}

	if (const FFlockContainmentSphere* Cage = Context.ContainmentSphereByFlock.Find(Agent.FlockId))
	{
		FlockContainment::AccumulateSphere(*Cage, Agent, Steering);
	}

	// constant speed: the force only turns the heading (agility raises the limit for the group)
	const float SteerLimit = Type.MaxSteerForce * (InPlayerGroup[Slot] ? PlayerAgilityScale : 1.f);
	const FVector3f Force = Steering.Steer.GetClampedToMaxSize(SteerLimit)
		+ Steering.External.GetClampedToMaxSize(Type.MaxExternalForce);
	const FVector3f Direction = (Agent.Heading + Force * DeltaTime).GetSafeNormal(1e-6f, Agent.Heading);

	float CurrentMultiplier = SpeedMultipliers[Slot];
	// behaviors can only raise the multiplier
	if (InPlayerGroup[Slot])
	{
		CurrentMultiplier *= PlayerSpeedScaleStep;
	}
	// group birds fly at the player's cruise speed
	float GroupScale = 1.f;
	if (InPlayerGroup[Slot])
	{
		const float OwnSpeed = FMath::Max(Agent.Speed, 1.f);
		const float MatchPlayer = IsHandleValid(PlayerHandle) ? Speeds[PlayerHandle.Index] / OwnSpeed : 1.f;
		GroupScale = MatchPlayer * PlayerSpeedScale * FollowSpeedFactor;

		// the tube flies faster than the player, who is not part of it
		if (ManeuverType == EFlockManeuver::Halo && Slot != PlayerHandle.Index)
		{
			GroupScale *= FMath::Lerp(1.f, TornadoParams.MemberSpeedScale, ManeuverWeight);
		}
	}
	const float TargetMultiplier = FMath::Max(Steering.SpeedMultiplier * GroupScale, 0.05f);
	const float Rate = TargetMultiplier > CurrentMultiplier ? Type.SpeedBoostInRate : Type.SpeedBoostOutRate;
	const float Multiplier = FMath::FInterpConstantTo(CurrentMultiplier, TargetMultiplier, DeltaTime, Rate);
	SpeedMultipliers[Slot] = Multiplier;

	const FVector3f NewVelocity = Direction * (Agent.Speed * Multiplier);
	NextVelocities[Slot] = NewVelocity;
	NextPositions[Slot] = Agent.Pos + NewVelocity * DeltaTime;
	Headings[Slot] = Direction;

	// ease toward facing the velocity, no roll
	const FQuat4f TargetRotation(FRotationMatrix44f::MakeFromX(Direction));
	const float Alpha = 1.f - FMath::Exp(-Type.TurnSpeed * DeltaTime);
	Rotations[Slot] = FQuat4f::Slerp(Rotations[Slot], TargetRotation, Alpha).GetNormalized();

	// banking, visual only
	if (Type.bBanking)
	{
		const FVector2f PrevFlat(Agent.Vel.X, Agent.Vel.Y);
		const FVector2f NextFlat(NewVelocity.X, NewVelocity.Y);
		const float MinFlat = Agent.Speed * 0.02f;

		float TargetBank = 0.f;
		if (PrevFlat.Size() > MinFlat && NextFlat.Size() > MinFlat)
		{
			// positive when turning right
			const float TurnRad = FMath::Atan2(PrevFlat.X * NextFlat.Y - PrevFlat.Y * NextFlat.X, FVector2f::DotProduct(PrevFlat, NextFlat));
			const float TurnRateDeg = FMath::RadiansToDegrees(TurnRad) / DeltaTime;
			TargetBank = FMath::Clamp(TurnRateDeg * Type.BankPerTurnRate, -Type.MaxBankAngle, Type.MaxBankAngle);
		}
		Banks[Slot] = SmoothDamp(Banks[Slot], TargetBank, BankVelocities[Slot], Type.BankSmoothTime, DeltaTime);
	}
	else
	{
		Banks[Slot] = 0.f;
		BankVelocities[Slot] = 0.f;
	}
}

void UFlockSubsystem::ApplyToActors()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Flock_Apply);

	for (int32 Slot = 0; Slot < Alive.Num(); ++Slot)
	{
		if (!Alive[Slot])
		{
			continue;
		}

		Positions[Slot] = NextPositions[Slot];
		Velocities[Slot] = NextVelocities[Slot];

		AActor* Agent = Actors[Slot].Get();
		if (!Agent)
		{
			// destroyed without unregistering
			RemoveSlot(Slot);
			continue;
		}

		// no sweep, the simulation already decided where the agent goes
		Agent->SetActorLocationAndRotation(FVector(Positions[Slot]), FQuat(Rotations[Slot]), false, nullptr, ETeleportType::None);

		// lean only the mesh around the forward axis, on top of its own rotation
		if (!FMath::IsNearlyEqual(Banks[Slot], AppliedBanks[Slot], 0.05f))
		{
			if (USceneComponent* BankComponent = BankComponents[Slot].Get())
			{
				BankComponent->SetRelativeRotation(FQuat(FRotator(0.0, 0.0, Banks[Slot])) * BankRestRotations[Slot]);
				AppliedBanks[Slot] = Banks[Slot];
			}
		}
	}
}

void UFlockSubsystem::RecordStats(double PrepareMs, double GroupMs, double SimulateMs, double ApplyMs, float DeltaTime)
{
	const float Interval = CVarFlockLogStatsInterval.GetValueOnGameThread();
	if (Interval <= 0.f)
	{
		Stats = FStatsAccumulator();
		return;
	}

	const double Now = FPlatformTime::Seconds();
	if (Stats.Frames == 0)
	{
		Stats.LastLogTime = Now;
	}

	++Stats.Frames;
	Stats.PrepareMs += PrepareMs;
	Stats.GroupMs += GroupMs;
	Stats.SimulateMs += SimulateMs;
	Stats.ApplyMs += ApplyMs;
	Stats.FrameSeconds += DeltaTime;
	Stats.MaxTotalMs = FMath::Max(Stats.MaxTotalMs, PrepareMs + GroupMs + SimulateMs + ApplyMs);

	if (Now - Stats.LastLogTime >= Interval)
	{
		const double N = double(Stats.Frames);
		UE_LOG(LogFlock, Display,
			TEXT("FlockStats: %d agents, %d frames, avg ms: prepare+hash %.3f, group %.3f, simulate %.3f, apply %.3f, TOTAL %.3f (worst frame %.3f), avg frame %.2f ms, player group %d, parallel %d"),
			NumAlive, Stats.Frames, Stats.PrepareMs / N, Stats.GroupMs / N, Stats.SimulateMs / N, Stats.ApplyMs / N,
			(Stats.PrepareMs + Stats.GroupMs + Stats.SimulateMs + Stats.ApplyMs) / N, Stats.MaxTotalMs,
			Stats.FrameSeconds / N * 1000.0, PlayerGroupSize, CVarFlockParallel.GetValueOnGameThread() ? 1 : 0);
		Stats = FStatsAccumulator();
	}
}

void UFlockSubsystem::DrawStats(double SimMs) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0x464C4B, 0.f, FColor::Cyan,
			FString::Printf(TEXT("Flock: %d agents, %d types, sim+apply %.3f ms, player group %d"), NumAlive, Types.Num(), SimMs, PlayerGroupSize));
	}
}

void UFlockSubsystem::DrawDebugAgent(int32 Slot) const
{
	const UWorld* World = GetWorld();
	const FTypeRuntime& Type = Types[TypeIds[Slot]];
	const FVector Pos(Positions[Slot]);
	const float Body = BodyRadii[Slot];

	// neighbor radius plus both bodies
	DrawDebugSphere(World, Pos, Type.NeighborRadius + 2.f * Body, 24, FColor::Cyan);
	DrawDebugSphere(World, Pos, Body, 12, FColor::White);
	DrawDebugDirectionalArrow(World, Pos, Pos + FVector(Velocities[Slot]), 40.f, FColor::Yellow, false, -1.f, 0, 3.f);

	// brute force, debug only
	for (int32 Other = 0; Other < Alive.Num(); ++Other)
	{
		if (Other == Slot || !Alive[Other] || TypeIds[Other] != TypeIds[Slot])
		{
			continue;
		}
		const float Reach = Type.NeighborRadius + Body + BodyRadii[Other];
		if (FVector3f::DistSquared(Positions[Slot], Positions[Other]) <= Reach * Reach)
		{
			DrawDebugLine(World, Pos, FVector(Positions[Other]), FColor::Green);
		}
	}
}

void UFlockSubsystem::UpdatePlayerGroup(double Now)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Flock_PlayerGroup);

	GroupChanges.Reset();
	PlayerGroupDistances.Reset();
	bPlayerGroupDistancesSorted = false;
	const bool bHasPlayer = IsHandleValid(PlayerHandle);
	const int32 PlayerSlot = PlayerHandle.Index;
	const int32 PlayerTypeId = bHasPlayer ? TypeIds[PlayerSlot] : INDEX_NONE;

	// nobody joins or leaves
	const bool bFrozen = bHasPlayer && ManeuverType != EFlockManeuver::None;

	if (bHasPlayer)
	{
		// only the player recruits
		const FTypeRuntime& Type = Types[PlayerTypeId];
		const FVector3f PlayerPos = Positions[PlayerSlot];
		LastConnectedTimes[PlayerSlot] = Now;

		// player position and heading, for the wedge
		FollowOrigin = PlayerPos;
		FollowForward = Velocities[PlayerSlot].GetSafeNormal(1e-4f, Headings[PlayerSlot]);
		const FVector3f FlatRight(-FollowForward.Y, FollowForward.X, 0.f);
		FollowRight = FlatRight.SizeSquared() > 1e-6f ? FlatRight.GetSafeNormal() : FVector3f::YAxisVector;

		if (!bFrozen && !bScared)
		{
			// only members are refreshed here
			Type.Hash.QueryRadius(PlayerPos, PlayerLeaveRadius, [&](int32 HashIndex, const FVector3f&, float DistSq)
			{
				const int32 Slot = Type.Slots[HashIndex];
				if (InPlayerGroup[Slot] && DistSq <= PlayerLeaveRadius * PlayerLeaveRadius)
				{
					LastConnectedTimes[Slot] = Now;
				}
				return true;
			}, HashIndexBySlot[PlayerSlot]);
		}
	}

	int32 Size = 0;
	FVector3f PositionSum = FVector3f::ZeroVector;
	FVector3f VelocitySum = FVector3f::ZeroVector;
	for (int32 Slot = 0; Slot < Alive.Num(); ++Slot)
	{
		if (!Alive[Slot])
		{
			continue;
		}

		if (bFrozen && InPlayerGroup[Slot])
		{
			LastConnectedTimes[Slot] = Now;
		}
		const bool bIn = bFrozen
			? InPlayerGroup[Slot] != 0
			: bHasPlayer && TypeIds[Slot] == PlayerTypeId && (Now - LastConnectedTimes[Slot]) <= PlayerMemberGraceTime;
		if (bIn != (InPlayerGroup[Slot] != 0))
		{
			InPlayerGroup[Slot] = bIn ? 1 : 0;
			GroupChanges.Emplace(Slot, bIn);
		}
		if (bIn)
		{
			++Size;
			PositionSum += Positions[Slot];
			PlayerGroupDistances.Add(FVector3f::Dist(Positions[Slot], Positions[PlayerSlot]));
			VelocitySum += Velocities[Slot];
		}
	}

	PlayerGroupSize = Size;

	if (Size > 0)
	{
		PlayerGroupCenter = PositionSum / float(Size);
		PlayerGroupHeading = VelocitySum.GetSafeNormal(1e-4f, Headings[PlayerSlot]);
	}
}

void UFlockSubsystem::NotifyGroupChanges()
{
	for (const TPair<int32, bool>& Change : GroupChanges)
	{
		if (Alive[Change.Key])
		{
			if (AFlockAgent* Agent = Cast<AFlockAgent>(Actors[Change.Key].Get()))
			{
				Agent->OnPlayerGroupChanged(Change.Value);
			}
		}
	}
	GroupChanges.Reset();
}
