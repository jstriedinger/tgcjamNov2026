// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlockTypes.h"
#include "FlockManeuvers.h"
#include "FlockSpatialHash.h"
#include "FlockSubsystem.generated.h"

class AActor;
class USceneComponent;
class AFlockAgent;
class AFlockContainmentVolume;
class USphereComponent;
class UFlockBehavior;
class UFlockPlayerSettings;
class UFlockCallProfile;
class UFlockTypeDefinition;

/** The player called, with its location */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFlockPlayerCalledSignature, FVector, PlayerLocation);

/** Simulates every flock of the world */
UCLASS()
class TGJFLYING_API UFlockSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	static UFlockSubsystem* Get(const UObject* WorldContextObject);

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	int32 RegisterFlock();

	/** An obstacle sphere that birds may avoid */
	void RegisterObstacle(const USphereComponent* Sphere, const FGameplayTagContainer& Tags);
	void UnregisterObstacle(const USphereComponent* Sphere);

	void UnregisterFlock(int32 FlockId);

	FFlockAgentHandle RegisterAgent(AFlockAgent* Agent, UFlockTypeDefinition* Type, int32 FlockId,
		const FVector& Position, const FVector& Direction);

	/** Adds any actor (the player pawn, for example) */
	FFlockAgentHandle RegisterActor(AActor* Actor, UFlockTypeDefinition* Type, int32 FlockId,
		const FVector& Position, const FVector& Direction, float CruiseSpeed, float BodyRadius,
		USceneComponent* BankComponent = nullptr);

	void UnregisterAgent(const FFlockAgentHandle& Handle);

	bool IsHandleValid(const FFlockAgentHandle& Handle) const;

	/** Unit direction the agent flies in, zero vector for an invalid handle */
	FVector GetAgentHeading(const FFlockAgentHandle& Handle) const;

	/** A steering force added to the agent's flocking force until changed (zero clears it) */
	bool SetAgentSteer(const FFlockAgentHandle& Handle, const FVector& Steer);

	void SetPlayerAgent(const FFlockAgentHandle& Handle, float MemberGraceTime);

	/** The player's steering force, added to every member of its group */
	void SetPlayerUrge(const FVector& Urge);

	/** Distance (cm) from the player to join the group, and to leave it */
	void SetPlayerJoinRadius(float JoinRadius, float LeaveRadius);

	/** How the group follows the player in a loose V behind it */
	void SetPlayerFollow(const FFlockFollowSettings& Settings);

	/** Master switch for all flock debug drawing */
	void SetDebugDrawEnabled(bool bEnabled) { bDebugDrawEnabled = bEnabled; }
	bool IsDebugDrawEnabled() const { return bDebugDrawEnabled; }

	/** Multiplies the speed of every bird in the player's group */
	void SetPlayerSpeedScale(float Scale);

	/** Multiplies the steering limit of every bird in the player's group */
	void SetPlayerAgilityScale(float Scale);

	bool StartPlayerTornado(const FFlockTornadoParams& Params, float Duration, float BlendIn, float BlendOut);

	/** The group circles in a vertical wheel */
	bool StartPlayerLoop(const FFlockTornadoParams& Params, float Duration, float BlendIn, float BlendOut);

	/** The group spirals around a line that moves forward */
	bool StartPlayerCorkscrew(const FFlockTornadoParams& Params, float Duration, float BlendIn, float BlendOut);

	/** A spinning tube of the group ahead of the player, along its heading */
	bool StartPlayerHalo(const FFlockTornadoParams& Params, float Duration, float BlendIn, float BlendOut);

	/** Unit vector the guide line of a running corkscrew points along */
	FVector GetPlayerGuideHeading() const;

	/** A wave through the V behind the player */
	bool StartPlayerRipple(const FFlockRippleParams& Params, float Duration, float BlendIn, float BlendOut);

	EFlockManeuver GetPlayerManeuver() const { return ManeuverType; }

	/** 0 = no move, 1 = fully in it */
	float GetPlayerManeuverWeight() const { return ManeuverWeight; }

	FVector GetPlayerManeuverCenter() const { return FVector(ManeuverCenter); }

	const FFlockTornadoParams& GetPlayerTornadoParams() const { return TornadoParams; }

	/** True while a move that replaces the group's steering runs */
	bool IsPlayerManeuverOverriding() const
	{
		return ManeuverType == EFlockManeuver::Tornado || ManeuverType == EFlockManeuver::Loop || ManeuverType == EFlockManeuver::Corkscrew;
	}

	/** Members of the player's group last frame, the player included */
	int32 GetPlayerGroupSize() const { return PlayerGroupSize; }

	FVector GetPlayerGroupCenter() const { return FVector(PlayerGroupCenter); }

	/** Normalized sum of the group's velocities, the player's heading when that is zero */
	FVector GetPlayerGroupHeading() const { return FVector(PlayerGroupHeading); }

	/** Distance from the player to the member at that percentile of the group (1 = the farthest) */
	float GetPlayerGroupRadius(float Percentile);

	bool IsAgentInPlayerGroup(const FFlockAgentHandle& Handle) const;

	bool IsAgentOutsideContainment(const FFlockAgentHandle& Handle) const;

	/** The player's settings, so other actors can read them too */
	void SetPlayerSettings(const UFlockPlayerSettings* InSettings);
	const UFlockPlayerSettings* GetPlayerSettings() const;

	/** Cruise speed in cm/s, 0 for an invalid handle */
	float GetAgentSpeed(const FFlockAgentHandle& Handle) const;

	/** Velocity the agent flew with in the last step, boosts included (cm/s) */
	FVector GetAgentVelocity(const FFlockAgentHandle& Handle) const;

	bool SetAgentSpeed(const FFlockAgentHandle& Handle, float Speed);

	/** Starts a chain of answers from the birds of the player's group */
	void StartCallChain(const FFlockAgentHandle& Caller, const UFlockCallProfile* CallerProfile);

	/** Birds of the player's type outside the group and within the join radius answer and join, nearest first */
	void RecruitOnCall(const FFlockAgentHandle& Caller);

	/** Scared: every bird flees from the player, the group dissolves, nobody answers or joins */
	UFUNCTION(BlueprintCallable, Category="Flock", meta = (WorldContext = "WorldContextObject"))
	static void SetFlockScared(const UObject* WorldContextObject, bool bInScared);

	UFUNCTION(BlueprintPure, Category="Flock", meta = (WorldContext = "WorldContextObject"))
	static bool IsFlockScared(const UObject* WorldContextObject);

	void SetScared(bool bInScared);
	bool IsScared() const { return bScared; }

	/** Fired when the player calls */
	UPROPERTY(BlueprintAssignable, Category="Flock")
	FFlockPlayerCalledSignature OnPlayerCalled;

	void NotifyPlayerCalled(const FVector& PlayerLocation) { OnPlayerCalled.Broadcast(PlayerLocation); }

	/** Keeps the flock inside the volume (null removes it) */
	void SetFlockContainment(int32 FlockId, AFlockContainmentVolume* Volume);

	/** Keeps the flock inside a sphere component, re-read every frame */
	void SetFlockContainmentSphere(int32 FlockId, const USphereComponent* Sphere, float Margin, float Weight);

	/** A caged bird cannot be recruited by a call */
	void SetAgentCaged(const FFlockAgentHandle& Handle, bool bCaged);
	bool IsAgentCaged(const FFlockAgentHandle& Handle) const;

private:

	struct FTypeRuntime
	{
		const UFlockTypeDefinition* Definition = nullptr;
		float NeighborRadius = 400.f;
		int32 MaxNeighbors = 20;
		float MaxSteerForce = 5.f;
		float MaxExternalForce = 10.f;
		float SpeedBoostInRate = 8.f;
		float SpeedBoostOutRate = 3.f;
		float TurnSpeed = 10.f;
		bool bBanking = true;
		float MaxBankAngle = 40.f;
		float BankPerTurnRate = 0.4f;
		float BankSmoothTime = 0.3f;

		FFlockFleeSettings Flee;

		float MaxBodyRadius = 0.f;
		float QueryRadius = 400.f;

		TArray<UFlockBehavior*> Behaviors;

		/** Agent slots of this type and their positions, same order */
		TArray<int32> Slots;
		TArray<FVector3f> Positions;
		FFlockSpatialHash Hash;
	};

	struct FWorkItem
	{
		int32 TypeId;
		int32 Local;
	};

	int32 FindOrAddType(UFlockTypeDefinition* Type);
	void RemoveSlot(int32 Slot);
	void UpdatePlayerGroup(double Now);
	void UpdateManeuver(double Now, float DeltaTime);

	/** Shared start of the orbiting moves */
	bool StartOrbit(EFlockManeuver Type, const FFlockTornadoParams& Params, float Duration, float BlendIn, float BlendOut);
	void NotifyGroupChanges();

	/** Makes the birds a call would recruit right now glow */
	void UpdateCallableGlow();

	void UpdateCallChain(double Now);

	/** Birds that answered a call join the group when their answer time comes */
	void UpdateRecruits(double Now);

	/** Group birds near From roll an answer delay, the quickest answers next */
	void ScheduleNextAnswer(const FVector3f& From, double Now);
	void SimulateAgent(const FFlockFrameContext& Context, const FTypeRuntime& Type, int32 TypeId, int32 Local);
	void ApplyToActors();
	void DrawStats(double SimMs) const;

	void RecordStats(double PrepareMs, double GroupMs, double SimulateMs, double ApplyMs, float DeltaTime);
	void DrawDebugAgent(int32 Slot) const;

	UPROPERTY()
	TArray<TObjectPtr<UFlockTypeDefinition>> TypeDefinitions;

	TArray<FTypeRuntime> Types;
	TMap<const UFlockTypeDefinition*, int32> TypeIdByDefinition;

	// agent state, indexed by slot (struct of arrays)
	TArray<FVector3f> Positions;
	TArray<FVector3f> Velocities;
	/** Unit heading, kept when the velocity is zero */
	TArray<FVector3f> Headings;
	TArray<FVector3f> NextPositions;
	TArray<FVector3f> NextVelocities;
	/** Smoothed actor rotation (visual only) */
	TArray<FQuat4f> Rotations;
	TArray<float> Speeds;
	/** Extra steering force per agent (the player's input) */
	TArray<FVector3f> InputSteers;
	/** Fixed random place in the wedge: X depth 0..1, Y side -1..1, Z up/down -1..1 */
	TArray<FVector3f> FollowSlots;
	TArray<float> BodyRadii;
	/** Smoothed speed multiplier */
	TArray<float> SpeedMultipliers;
	/** Lean angle in degrees (right wing down is positive) and its smoothing speed */
	TArray<float> Banks;
	TArray<float> BankVelocities;
	/** Last lean written to the component, so an unchanged lean costs nothing */
	TArray<float> AppliedBanks;
	/** The component that leans (the mesh) and its rest rotation */
	TArray<TWeakObjectPtr<USceneComponent>> BankComponents;
	TArray<FQuat> BankRestRotations;
	TArray<int32> FlockIds;
	TArray<int32> TypeIds;
	TArray<uint32> Generations;
	TArray<uint8> Alive;
	TArray<TWeakObjectPtr<AActor>> Actors;

	FFlockAgentHandle PlayerHandle;
	float PlayerMemberGraceTime = 0.5f;
	float PlayerJoinRadius = 600.f;
	float PlayerLeaveRadius = 900.f;
	FFlockFollowSettings PlayerFollow;

	bool bScared = false;

	/** The player's position, forward and right (flat) this frame */
	FVector3f FollowOrigin = FVector3f::ZeroVector;
	FVector3f FollowForward = FVector3f::XAxisVector;
	FVector3f FollowRight = FVector3f::YAxisVector;
	FVector3f PlayerUrge = FVector3f::ZeroVector;
	TWeakObjectPtr<const UFlockPlayerSettings> PlayerSettings;

	float PlayerSpeedScale = 1.f;

	/** PlayerSpeedScale of the previous step, and this step's change (new / previous) */
	float PlayerSpeedScaleLast = 1.f;
	float PlayerSpeedScaleStep = 1.f;
	float PlayerAgilityScale = 1.f;

	/** The running special move */
	EFlockManeuver ManeuverType = EFlockManeuver::None;
	FFlockTornadoParams TornadoParams;
	FFlockRippleParams RippleParams;
	FVector3f ManeuverCenter = FVector3f::ZeroVector;
	/** How fast the center moves along the axis (cm/s) */
	float ManeuverAxialSpeed = 0.f;
	double ManeuverStartTime = 0.0;
	float ManeuverDuration = 0.f;
	float ManeuverBlendIn = 0.5f;
	float ManeuverBlendOut = 0.8f;
	float ManeuverWeight = 0.f;
	int32 PlayerGroupSize = 0;
	FVector3f PlayerGroupCenter = FVector3f::ZeroVector;
	FVector3f PlayerGroupHeading = FVector3f::XAxisVector;
	/** By slot */
	TArray<uint8> InPlayerGroup;
	TArray<double> LastConnectedTimes;

	/** Index of each slot in its type's Slots / Positions / hash this frame */
	TArray<int32> HashIndexBySlot;
	/** Membership changes found this frame: slot, joined */
	TArray<TPair<int32, bool>> GroupChanges;

	/** Distance of every group member from the player */
	TArray<float> PlayerGroupDistances;
	bool bPlayerGroupDistancesSorted = false;

	bool bDebugDrawEnabled = true;

	/** The running chain of answers to the player's call */
	struct FCallChain
	{
		bool bActive = false;
		/** Who called in this chain, so nobody answers twice */
		TArray<FFlockAgentHandle> Called;
		/** The bird that answers next, and when */
		FFlockAgentHandle Next;
		double NextTime = 0.0;
		/** Where the last call came from, in case the waiting bird left the group */
		FVector3f LastCallerPosition = FVector3f::ZeroVector;
		int32 Answers = 0;
		TWeakObjectPtr<const UFlockCallProfile> Profile;
	};
	FCallChain CallChain;

	/** A bird that heard the player's call and will answer and join at Time */
	struct FPendingJoin
	{
		FFlockAgentHandle Handle;
		double Time = 0.0;
	};
	TArray<FPendingJoin> PendingJoins;
	const UFlockCallProfile& GetChainProfile() const;

	struct FObstacleEntry
	{
		TWeakObjectPtr<const USphereComponent> Sphere;
		FGameplayTagContainer Tags;
	};
	TArray<FObstacleEntry> ObstacleEntries;

	/** Sphere cages by flock id */
	struct FSphereCage
	{
		TWeakObjectPtr<const USphereComponent> Sphere;
		float Margin = 250.f;
		float Weight = 8.f;
	};
	TMap<int32, FSphereCage> FlockSpheres;

	/** Per bird: caged (1) or not */
	TArray<uint8> Caged;

	/** Containment volume by flock id */
	TMap<int32, TWeakObjectPtr<AFlockContainmentVolume>> FlockVolumes;

	TArray<int32> FreeSlots;
	TArray<FWorkItem> Work;
	int32 NumAlive = 0;
	int32 NextFlockId = 0;
	double SmoothedSimMs = 0.0;

	/** Running totals for the periodic stats log */
	struct FStatsAccumulator
	{
		int32 Frames = 0;
		double PrepareMs = 0.0;
		double GroupMs = 0.0;
		double SimulateMs = 0.0;
		double ApplyMs = 0.0;
		double MaxTotalMs = 0.0;
		double FrameSeconds = 0.0;
		double LastLogTime = 0.0;
	};
	FStatsAccumulator Stats;
};
