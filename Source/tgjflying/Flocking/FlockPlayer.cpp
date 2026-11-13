// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockPlayer.h"
#include "DrawDebugHelpers.h"
#include "FlockCallComponent.h"
#include "FlockContainmentVolume.h"
#include "FlockDataTypes.h"
#include "FlockSpatialHash.h"
#include "FlockSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Index of the tier with the highest MinGroupSize the group reaches, or INDEX_NONE */
	template <typename TTier>
	int32 PickTier(const TArray<TTier>& Tiers, int32 GroupSize, int32& OutSmallestMin)
	{
		int32 Best = INDEX_NONE;
		OutSmallestMin = TNumericLimits<int32>::Max();
		for (int32 Index = 0; Index < Tiers.Num(); ++Index)
		{
			OutSmallestMin = FMath::Min(OutSmallestMin, Tiers[Index].MinGroupSize);
			if (GroupSize >= Tiers[Index].MinGroupSize && (Best == INDEX_NONE || Tiers[Index].MinGroupSize > Tiers[Best].MinGroupSize))
			{
				Best = Index;
			}
		}
		return Best;
	}
}

AFlockPlayer::AFlockPlayer()
{
	PrimaryActorTick.bCanEverTick = true;
	// late in the frame so the camera sees where the simulation put the bird
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

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

	// the engine cone points along +Z, the bird heads along +X
	Mesh->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
	Mesh->SetRelativeScale3D(FVector(0.45, 0.45, 0.9));

	CallComponent = CreateDefaultSubobject<UFlockCallComponent>(TEXT("Call"));
	CallComponent->StretchTarget = Mesh;
}

void AFlockPlayer::BeginPlay()
{
	Super::BeginPlay();

	RegisterWithFlock();
}

void AFlockPlayer::RegisterWithFlock()
{
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!Flock)
	{
		return;
	}
	if (!FlockType)
	{
		UE_LOG(LogFlock, Error, TEXT("%s: no FlockType set, the player bird is not part of any flock"), *GetName());
		return;
	}

	// the other birds read these settings through the subsystem
	Flock->SetPlayerSettings(&GetSettings());

	FlockId = Flock->RegisterFlock();
	Flock->SetFlockContainment(FlockId, ContainmentVolume);

	const float Speed = FMath::FRandRange(CruiseSpeedRange.Min, CruiseSpeedRange.Max);
	Handle = Flock->RegisterActor(this, FlockType, FlockId, GetActorLocation(), GetActorForwardVector(), Speed, BodyRadius, Mesh);
	if (!Handle.IsSet())
	{
		UE_LOG(LogFlock, Error, TEXT("%s: could not register the player bird with the flock"), *GetName());
		return;
	}
	Flock->SetPlayerAgent(Handle, GetSettings().MemberGraceTime);
}

void AFlockPlayer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFlockSubsystem* Flock = UFlockSubsystem::Get(this))
	{
		Flock->SetPlayerAgent(FFlockAgentHandle(), GetSettings().MemberGraceTime);
		Flock->UnregisterAgent(Handle);
		if (FlockId != INDEX_NONE)
		{
			Flock->UnregisterFlock(FlockId);
		}
	}
	Handle = FFlockAgentHandle();
	FlockId = INDEX_NONE;

	Super::EndPlay(EndPlayReason);
}

void AFlockPlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// keys and stick are separate actions: only the keys get the ramp
	KeysAction = NewObject<UInputAction>(this, TEXT("IA_FlockSteerKeys"));
	KeysAction->ValueType = EInputActionValueType::Axis2D;
	StickAction = NewObject<UInputAction>(this, TEXT("IA_FlockSteerStick"));
	StickAction->ValueType = EInputActionValueType::Axis2D;

	// the right stick gives a held position, the mouse gives movement
	LookStickAction = NewObject<UInputAction>(this, TEXT("IA_FlockLookStick"));
	LookStickAction->ValueType = EInputActionValueType::Axis2D;
	LookMouseAction = NewObject<UInputAction>(this, TEXT("IA_FlockLookMouse"));
	LookMouseAction->ValueType = EInputActionValueType::Axis2D;

	// the remaining actions are plain button presses
	DashAction = NewObject<UInputAction>(this, TEXT("IA_FlockDash"));
	TornadoAction = NewObject<UInputAction>(this, TEXT("IA_FlockTornado"));
	LoopAction = NewObject<UInputAction>(this, TEXT("IA_FlockLoop"));
	CorkscrewAction = NewObject<UInputAction>(this, TEXT("IA_FlockCorkscrew"));
	HaloAction = NewObject<UInputAction>(this, TEXT("IA_FlockHalo"));
	RippleAction = NewObject<UInputAction>(this, TEXT("IA_FlockRipple"));
	CallAction = NewObject<UInputAction>(this, TEXT("IA_FlockCall"));

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_FlockFlight"));

	// D is +X
	MappingContext->MapKey(KeysAction, EKeys::D);
	MappingContext->MapKey(KeysAction, EKeys::A).Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
	{
		FEnhancedActionKeyMapping& W = MappingContext->MapKey(KeysAction, EKeys::W);
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(MappingContext);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		W.Modifiers.Add(Swizzle);

		FEnhancedActionKeyMapping& S = MappingContext->MapKey(KeysAction, EKeys::S);
		UInputModifierSwizzleAxis* SwizzleS = NewObject<UInputModifierSwizzleAxis>(MappingContext);
		SwizzleS->Order = EInputAxisSwizzle::YXZ;
		S.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
		S.Modifiers.Add(SwizzleS);
	}
	MappingContext->MapKey(StickAction, EKeys::Gamepad_Left2D);
	MappingContext->MapKey(LookStickAction, EKeys::Gamepad_Right2D);
	MappingContext->MapKey(LookMouseAction, EKeys::Mouse2D);
	MappingContext->MapKey(DashAction, EKeys::SpaceBar);
	MappingContext->MapKey(DashAction, EKeys::Gamepad_FaceButton_Bottom);
	MappingContext->MapKey(CallAction, EKeys::E);
	MappingContext->MapKey(CallAction, EKeys::Gamepad_FaceButton_Top);

	// special moves, ordered by how many birds each needs
	MappingContext->MapKey(RippleAction, EKeys::One);
	MappingContext->MapKey(RippleAction, EKeys::Gamepad_LeftShoulder);
	MappingContext->MapKey(TornadoAction, EKeys::Two);
	MappingContext->MapKey(TornadoAction, EKeys::Gamepad_RightShoulder);
	MappingContext->MapKey(HaloAction, EKeys::Three);
	MappingContext->MapKey(HaloAction, EKeys::Gamepad_LeftTrigger);
	MappingContext->MapKey(LoopAction, EKeys::Four);
	MappingContext->MapKey(LoopAction, EKeys::Gamepad_RightTrigger);
	MappingContext->MapKey(CorkscrewAction, EKeys::Five);
	MappingContext->MapKey(CorkscrewAction, EKeys::Gamepad_DPad_Up);

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(KeysAction, ETriggerEvent::Triggered, this, &AFlockPlayer::OnKeysSteer);
		Input->BindAction(KeysAction, ETriggerEvent::Completed, this, &AFlockPlayer::OnKeysSteerEnd);
		Input->BindAction(KeysAction, ETriggerEvent::Canceled, this, &AFlockPlayer::OnKeysSteerEnd);
		Input->BindAction(StickAction, ETriggerEvent::Triggered, this, &AFlockPlayer::OnStickSteer);
		Input->BindAction(StickAction, ETriggerEvent::Completed, this, &AFlockPlayer::OnStickSteerEnd);
		Input->BindAction(StickAction, ETriggerEvent::Canceled, this, &AFlockPlayer::OnStickSteerEnd);
		Input->BindAction(LookStickAction, ETriggerEvent::Triggered, this, &AFlockPlayer::OnLookStick);
		Input->BindAction(LookStickAction, ETriggerEvent::Completed, this, &AFlockPlayer::OnLookStickEnd);
		Input->BindAction(LookStickAction, ETriggerEvent::Canceled, this, &AFlockPlayer::OnLookStickEnd);
		Input->BindAction(LookMouseAction, ETriggerEvent::Triggered, this, &AFlockPlayer::OnLookMouse);

		// started fires on the frame the button goes down
		Input->BindAction(DashAction, ETriggerEvent::Started, this, &AFlockPlayer::OnDashInput);
		Input->BindAction(TornadoAction, ETriggerEvent::Started, this, &AFlockPlayer::OnTornadoInput);
		Input->BindAction(LoopAction, ETriggerEvent::Started, this, &AFlockPlayer::OnLoopInput);
		Input->BindAction(CorkscrewAction, ETriggerEvent::Started, this, &AFlockPlayer::OnCorkscrewInput);
		Input->BindAction(HaloAction, ETriggerEvent::Started, this, &AFlockPlayer::OnHaloInput);
		Input->BindAction(RippleAction, ETriggerEvent::Started, this, &AFlockPlayer::OnRippleInput);
		Input->BindAction(CallAction, ETriggerEvent::Started, this, &AFlockPlayer::OnCallInput);
	}
}

void AFlockPlayer::OnCallInput(const FInputActionValue& Value)
{
	if (CallComponent->Call())
	{
		if (UFlockSubsystem* Flock = UFlockSubsystem::Get(this))
		{
			Flock->StartCallChain(Handle, CallProfile);
			Flock->RecruitOnCall(Handle);
			Flock->NotifyPlayerCalled(GetActorLocation());
		}
	}
}

void AFlockPlayer::OnKeysSteer(const FInputActionValue& Value)
{
	KeysTarget = Value.Get<FVector2D>();
}

void AFlockPlayer::OnKeysSteerEnd(const FInputActionValue& Value)
{
	KeysTarget = FVector2D::ZeroVector;
}

void AFlockPlayer::OnStickSteer(const FInputActionValue& Value)
{
	StickRaw = Value.Get<FVector2D>();
}

void AFlockPlayer::OnStickSteerEnd(const FInputActionValue& Value)
{
	StickRaw = FVector2D::ZeroVector;
}

void AFlockPlayer::OnLookStick(const FInputActionValue& Value)
{
	LookStickRaw = Value.Get<FVector2D>();
}

void AFlockPlayer::OnLookStickEnd(const FInputActionValue& Value)
{
	LookStickRaw = FVector2D::ZeroVector;
}

void AFlockPlayer::OnLookMouse(const FInputActionValue& Value)
{
	// the mouse only fires while it moves, so add it up until the camera collects it
	LookMouseAccumulated += Value.Get<FVector2D>();
}

bool AFlockPlayer::ConsumeLook(float DeltaTime, FVector2D& OutDeltaDegrees)
{
	// the stick is a speed (degrees per second), so it needs the frame time
	const bool bStickActive = LookStickRaw.Size() > GetSettings().LookDeadZone;
	const FVector2D StickDelta = bStickActive ? LookStickRaw * (GetSettings().LookStickSpeed * DeltaTime) : FVector2D::ZeroVector;

	// the mouse is already a distance
	const bool bMouseActive = !LookMouseAccumulated.IsNearlyZero(1e-4);
	const FVector2D MouseDelta = LookMouseAccumulated * GetSettings().MouseLookSensitivity;
	LookMouseAccumulated = FVector2D::ZeroVector;

	OutDeltaDegrees = StickDelta + MouseDelta;
	if (GetSettings().bInvertLookPitch)
	{
		OutDeltaDegrees.Y = -OutDeltaDegrees.Y;
	}
	return bStickActive || bMouseActive;
}

void AFlockPlayer::OnDashInput(const FInputActionValue& Value)
{
	TryDash();
}

void AFlockPlayer::SetAutopilotTarget(FVector Location)
{
	AutopilotTarget = Location;
	bAutopilot = true;
}

void AFlockPlayer::ClearAutopilot()
{
	bAutopilot = false;
}

bool AFlockPlayer::TryDash()
{
	const double Now = GetWorld()->GetTimeSeconds();
	// a move that takes over sets its own speed
	const UFlockSubsystem* MoveFlock = UFlockSubsystem::Get(this);
	if (Now < DashReadyTime || (MoveFlock && MoveFlock->IsPlayerManeuverOverriding()))
	{
		return false;
	}

	// the bigger the flock, the longer the burst
	const float Duration = FMath::Lerp(GetSettings().DashDuration, GetSettings().DashDurationAtFullGroup, Fullness);
	DashEndTime = Now + Duration;
	DashReadyTime = DashEndTime + GetSettings().DashCooldown;
	bDashEndPending = true;
	bDashReadyPending = true;
	OnDashStarted();
	return true;
}

void AFlockPlayer::OnTornadoInput(const FInputActionValue& Value)
{
	TryTornado();
}

void AFlockPlayer::OnLoopInput(const FInputActionValue& Value)
{
	TryLoop();
}

void AFlockPlayer::OnRippleInput(const FInputActionValue& Value)
{
	TryRipple();
}

void AFlockPlayer::OnCorkscrewInput(const FInputActionValue& Value)
{
	TryCorkscrew();
}

void AFlockPlayer::OnHaloInput(const FInputActionValue& Value)
{
	TryHalo();
}

bool AFlockPlayer::TryTornado()
{
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!Flock || !Flock->IsHandleValid(Handle))
	{
		return false;
	}

	const FFlockTornadoSettings& T = GetSettings().Tornado;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < MoveReadyTime || IsInManeuver())
	{
		return false;
	}

	const int32 GroupSize = GetGroupSize();
	int32 SmallestMin = 0;
	const int32 TierIndex = PickTier(T.Tiers, GroupSize, SmallestMin);
	if (TierIndex == INDEX_NONE)
	{
		if (GEngine && GetSettings().bShowDebug && !T.Tiers.IsEmpty())
		{
			GEngine->AddOnScreenDebugMessage(7451, 2.f, FColor::Orange,
				FString::Printf(TEXT("Tornado needs %d birds in your group (you have %d)"), SmallestMin, GroupSize));
		}
		return false;
	}
	const FFlockTornadoTier& Tier = T.Tiers[TierIndex];

	FFlockTornadoParams P;
	P.RadiusBottom = T.RadiusBottom * Tier.RadiusScale;
	P.RadiusTop = FMath::Max(T.RadiusTop, T.RadiusBottom) * Tier.RadiusScale;
	P.RadiusJitter = T.RadiusJitter;
	P.Height = T.Height * Tier.HeightScale;
	P.BobHeight = T.BobHeight;
	P.SteerGain = T.SteerGain;
	P.RadialWeight = T.RadialWeight;
	P.VerticalWeight = T.VerticalWeight;
	P.CatchDistance = T.CatchDistance;
	// circles the way you steer, right when the stick is centered
	P.bTurnRight = SteerInput.X > -0.3f;

	// the finale adds time at the end: circles tighten and everyone shoots up
	P.FinaleRise = Tier.bFinale ? T.FinaleRise : 0.f;
	P.FinaleTime = T.FinaleTime;
	P.FinaleLeadTime = T.BlendOutTime;
	P.FinaleRadiusScale = T.FinaleRadiusScale;
	P.FinaleVerticalWeight = T.FinaleClimbStrength;
	const float TotalDuration = T.Duration + (P.FinaleRise > 0.f ? T.FinaleTime : 0.f);

	if (!Flock->StartPlayerTornado(P, TotalDuration, T.BlendInTime, T.BlendOutTime))
	{
		return false;
	}

	ActiveMoveTier = TierIndex + 1;
	ActiveMoveSpeed = Tier.SpeedMultiplier;

	if (GEngine && GetSettings().bShowDebug)
	{
		GEngine->AddOnScreenDebugMessage(7452, 3.f, FColor::Yellow,
			FString::Printf(TEXT("Tornado tier %d (%d birds)%s"), ActiveMoveTier, GroupSize, Tier.bFinale ? TEXT(" + finale") : TEXT("")));
	}

	// the cooldown starts when the move ends
	MoveReadyTime = Now + TotalDuration + T.Cooldown;
	OnTornadoStarted();
	return true;
}

bool AFlockPlayer::TryLoop()
{
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!Flock || !Flock->IsHandleValid(Handle))
	{
		return false;
	}

	const FFlockLoopSettings& L = GetSettings().Loop;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < MoveReadyTime || IsInManeuver())
	{
		return false;
	}

	const int32 GroupSize = GetGroupSize();
	int32 SmallestMin = 0;
	const int32 TierIndex = PickTier(L.Tiers, GroupSize, SmallestMin);
	if (TierIndex == INDEX_NONE)
	{
		if (GEngine && GetSettings().bShowDebug && !L.Tiers.IsEmpty())
		{
			GEngine->AddOnScreenDebugMessage(7451, 2.f, FColor::Orange,
				FString::Printf(TEXT("Loop needs %d birds in your group (you have %d)"), SmallestMin, GroupSize));
		}
		return false;
	}
	const FFlockLoopTier& Tier = L.Tiers[TierIndex];

	// a wheel is the tornado with a sideways axis and a constant radius; its height is the wheel width
	const float Radius = L.Radius * Tier.RadiusScale;
	FFlockTornadoParams P;
	P.RadiusBottom = Radius;
	P.RadiusTop = Radius;
	P.RadiusJitter = L.RadiusJitter;
	P.Height = L.Width;
	P.BobHeight = L.BobWidth;
	P.SteerGain = L.SteerGain;
	P.RadialWeight = L.RadialWeight;
	P.VerticalWeight = L.SideWeight;
	P.CatchDistance = L.CatchDistance;

	// long enough for the laps at the speed the group will fly
	const float GrowthScale = FMath::Lerp(1.f, GetSettings().SpeedAtFullGroup, Fullness);
	const float Speed = FMath::Max(Flock->GetAgentSpeed(Handle) * GrowthScale * Tier.SpeedMultiplier, 100.f);
	const float TotalDuration = Tier.Laps * (2.f * PI * Radius / Speed) + L.ExtraTime;

	if (!Flock->StartPlayerLoop(P, TotalDuration, L.BlendInTime, L.BlendOutTime))
	{
		return false;
	}

	ActiveMoveTier = TierIndex + 1;
	ActiveMoveSpeed = Tier.SpeedMultiplier;

	if (GEngine && GetSettings().bShowDebug)
	{
		GEngine->AddOnScreenDebugMessage(7452, 3.f, FColor::Magenta,
			FString::Printf(TEXT("Loop tier %d (%d birds), %.1f s"), ActiveMoveTier, GroupSize, TotalDuration));
	}

	MoveReadyTime = Now + TotalDuration + L.Cooldown;
	OnLoopStarted();
	return true;
}

bool AFlockPlayer::TryCorkscrew()
{
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!Flock || !Flock->IsHandleValid(Handle))
	{
		return false;
	}

	const FFlockCorkscrewSettings& C = GetSettings().Corkscrew;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < MoveReadyTime || IsInManeuver())
	{
		return false;
	}

	const int32 GroupSize = GetGroupSize();
	int32 SmallestMin = 0;
	const int32 TierIndex = PickTier(C.Tiers, GroupSize, SmallestMin);
	if (TierIndex == INDEX_NONE)
	{
		if (GEngine && GetSettings().bShowDebug && !C.Tiers.IsEmpty())
		{
			GEngine->AddOnScreenDebugMessage(7451, 2.f, FColor::Orange,
				FString::Printf(TEXT("Corkscrew needs %d birds in your group (you have %d)"), SmallestMin, GroupSize));
		}
		return false;
	}
	const FFlockCorkscrewTier& Tier = C.Tiers[TierIndex];

	// a tube around a line that moves forward; height is the tube length
	const float Radius = C.Radius * Tier.RadiusScale;
	FFlockTornadoParams P;
	P.RadiusBottom = Radius;
	P.RadiusTop = Radius;
	P.RadiusJitter = C.RadiusJitter;
	P.Height = C.Length;
	P.BobHeight = C.BobLength;
	P.SteerGain = C.SteerGain;
	P.RadialWeight = C.RadialWeight;
	P.VerticalWeight = C.AlongWeight;
	P.CatchDistance = C.CatchDistance;
	P.ForwardSpeedFraction = C.ForwardSpeedFraction;
	P.bTurnRight = SteerInput.X > -0.3f;

	// the finale adds time at the end: the spiral narrows and the group bursts forward
	P.FinaleRise = Tier.bFinale ? C.FinaleBurst : 0.f;
	P.FinaleTime = C.FinaleTime;
	P.FinaleLeadTime = C.BlendOutTime;
	P.FinaleRadiusScale = C.FinaleRadiusScale;
	P.FinaleVerticalWeight = C.FinaleStrength;
	const float TotalDuration = C.Duration + (P.FinaleRise > 0.f ? C.FinaleTime : 0.f);

	if (!Flock->StartPlayerCorkscrew(P, TotalDuration, C.BlendInTime, C.BlendOutTime))
	{
		return false;
	}

	ActiveMoveTier = TierIndex + 1;
	ActiveMoveSpeed = Tier.SpeedMultiplier;

	if (GEngine && GetSettings().bShowDebug)
	{
		GEngine->AddOnScreenDebugMessage(7452, 3.f, FColor::Orange,
			FString::Printf(TEXT("Corkscrew tier %d (%d birds)%s"), ActiveMoveTier, GroupSize, Tier.bFinale ? TEXT(" + finale") : TEXT("")));
	}

	MoveReadyTime = Now + TotalDuration + C.Cooldown;
	OnCorkscrewStarted();
	return true;
}

bool AFlockPlayer::TryHalo()
{
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!Flock || !Flock->IsHandleValid(Handle))
	{
		return false;
	}

	const FFlockHaloSettings& H = GetSettings().Halo;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < MoveReadyTime || IsInManeuver())
	{
		return false;
	}

	const int32 GroupSize = GetGroupSize();
	int32 SmallestMin = 0;
	const int32 TierIndex = PickTier(H.Tiers, GroupSize, SmallestMin);
	if (TierIndex == INDEX_NONE)
	{
		if (GEngine && GetSettings().bShowDebug && !H.Tiers.IsEmpty())
		{
			GEngine->AddOnScreenDebugMessage(7451, 2.f, FColor::Orange,
				FString::Printf(TEXT("Halo needs %d birds in your group (you have %d)"), SmallestMin, GroupSize));
		}
		return false;
	}
	const FFlockHaloTier& Tier = H.Tiers[TierIndex];

	// a tube around a line along your heading; height is the tube length
	const float Radius = H.Radius * Tier.RadiusScale;
	FFlockTornadoParams P;
	P.RadiusBottom = Radius;
	P.RadiusTop = Radius;
	P.RadiusJitter = H.RadiusJitter;
	P.Height = H.Length * Tier.LengthScale;
	P.BobHeight = H.BobLength;
	P.SteerGain = H.SteerGain;
	P.RadialWeight = H.RadialWeight;
	P.VerticalWeight = H.AlongWeight;
	P.CatchDistance = H.CatchDistance;
	P.ForwardSpeedFraction = H.ForwardSpeedFraction;
	P.MemberSpeedScale = Tier.SpeedMultiplier;
	P.bTurnRight = SteerInput.X > -0.3f;

	// start far enough ahead that you reach the middle at ReachMiddleAt of the move
	const float GrowthScale = FMath::Lerp(1.f, GetSettings().SpeedAtFullGroup, Fullness);
	const float YourSpeed = Flock->GetAgentSpeed(Handle) * GrowthScale;
	P.StartAhead = H.ReachMiddleAt * H.Duration * (1.f - H.ForwardSpeedFraction) * YourSpeed;

	if (!Flock->StartPlayerHalo(P, H.Duration, H.BlendInTime, H.BlendOutTime))
	{
		return false;
	}

	// the player is not part of the tube, so its speed stays
	ActiveMoveTier = TierIndex + 1;
	ActiveMoveSpeed = 1.f;

	if (GEngine && GetSettings().bShowDebug)
	{
		GEngine->AddOnScreenDebugMessage(7452, 3.f, FColor::Turquoise,
			FString::Printf(TEXT("Halo tier %d (%d birds)"), ActiveMoveTier, GroupSize));
	}

	MoveReadyTime = Now + H.Duration + H.Cooldown;
	OnHaloStarted();
	return true;
}

bool AFlockPlayer::TryRipple()
{
	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!Flock || !Flock->IsHandleValid(Handle))
	{
		return false;
	}

	const FFlockRippleSettings& R = GetSettings().Ripple;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < MoveReadyTime || IsInManeuver())
	{
		return false;
	}

	const int32 GroupSize = GetGroupSize();
	int32 SmallestMin = 0;
	const int32 TierIndex = PickTier(R.Tiers, GroupSize, SmallestMin);
	if (TierIndex == INDEX_NONE)
	{
		if (GEngine && GetSettings().bShowDebug && !R.Tiers.IsEmpty())
		{
			GEngine->AddOnScreenDebugMessage(7451, 2.f, FColor::Orange,
				FString::Printf(TEXT("Ripple needs %d birds in your group (you have %d)"), SmallestMin, GroupSize));
		}
		return false;
	}
	const FFlockRippleTier& Tier = R.Tiers[TierIndex];

	FFlockRippleParams P;
	P.Amplitude = Tier.Amplitude;
	P.Wavelength = Tier.Wavelength;
	P.Period = Tier.Period;
	P.FollowBoost = R.FollowBoost;
	P.PlayerGain = R.PlayerWaveGain;

	if (!Flock->StartPlayerRipple(P, R.Duration, R.BlendInTime, R.BlendOutTime))
	{
		return false;
	}

	ActiveMoveTier = TierIndex + 1;
	ActiveMoveSpeed = 1.f;

	if (GEngine && GetSettings().bShowDebug)
	{
		GEngine->AddOnScreenDebugMessage(7452, 3.f, FColor::Cyan,
			FString::Printf(TEXT("Ripple tier %d (%d birds)"), ActiveMoveTier, GroupSize));
	}

	MoveReadyTime = Now + R.Duration + R.Cooldown;
	OnRippleStarted();
	return true;
}

bool AFlockPlayer::IsInManeuver() const
{
	const UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	return Flock && Flock->GetPlayerManeuver() != EFlockManeuver::None;
}

float AFlockPlayer::GetMoveCooldownRemaining() const
{
	return GetWorld() ? float(FMath::Max(MoveReadyTime - GetWorld()->GetTimeSeconds(), 0.0)) : 0.f;
}

bool AFlockPlayer::IsDashing() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < DashEndTime;
}

float AFlockPlayer::GetDashCooldownRemaining() const
{
	return GetWorld() ? float(FMath::Max(DashReadyTime - GetWorld()->GetTimeSeconds(), 0.0)) : 0.f;
}

FVector AFlockPlayer::GetFlightVelocity() const
{
	const UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	return Flock ? Flock->GetAgentVelocity(Handle) : FVector::ZeroVector;
}

float AFlockPlayer::GetSpeedRatio() const
{
	const UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	const float Cruise = Flock ? Flock->GetAgentSpeed(Handle) : 0.f;
	return Cruise > 0.f ? float(Flock->GetAgentVelocity(Handle).Size()) / Cruise : 0.f;
}

float AFlockPlayer::GetSpeedAlpha() const
{
	const float Range = GetSettings().SpeedFeelMax - GetSettings().SpeedFeelMin;
	return Range > 0.f ? FMath::Clamp((GetSpeedRatio() - GetSettings().SpeedFeelMin) / Range, 0.f, 1.f) : 0.f;
}

const UFlockPlayerSettings& AFlockPlayer::GetSettings() const
{
	return Settings ? *Settings : *GetDefault<UFlockPlayerSettings>();
}

int32 AFlockPlayer::GetGroupSize() const
{
	const UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	return Flock && Flock->IsHandleValid(Handle) ? Flock->GetPlayerGroupSize() : 0;
}

float AFlockPlayer::ShapeAxis(float Raw) const
{
	const float Magnitude = FMath::Abs(Raw);
	if (Magnitude <= GetSettings().DeadZone)
	{
		return 0.f;
	}
	const float Rescaled = FMath::Clamp((Magnitude - GetSettings().DeadZone) / (1.f - GetSettings().DeadZone), 0.f, 1.f);
	return FMath::Sign(Raw) * FMath::Pow(Rescaled, GetSettings().ResponseExponent);
}

bool AFlockPlayer::Evaluate(const FVector& Heading, FVector& OutDirection, float& OutStrength) const
{
	const double Now = GetWorld()->GetTimeSeconds();
	const float ElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(float(Heading.Z), -1.f, 1.f)));

	// level out once up/down has been released for a moment; paused during the ripple so it keeps its wave
	const UFlockSubsystem* MoveFlock = UFlockSubsystem::Get(this);
	const bool bRippling = MoveFlock && MoveFlock->GetPlayerManeuver() == EFlockManeuver::Ripple;
	const bool bLeveling = GetSettings().bAutoLevel && !bRippling && SteerInput.Y == 0.f && (Now - LastPitchInputTime) >= GetSettings().AutoLevelDelay;
	const float LevelStrength = bLeveling
		? (3.f / GetSettings().AutoLevelTime) * FMath::Clamp(FMath::Abs(ElevationDeg) / GetSettings().AutoLevelFadeAngle, 0.f, 1.f)
		: 0.f;
	const float InputStrength = GetSettings().SteerStrength * FMath::Clamp(float(SteerInput.Size()), 0.f, 1.f);

	if (LevelStrength <= 0.f && InputStrength <= 0.f)
	{
		return false;
	}

	const FVector2D Flat(Heading.X, Heading.Y);
	const float HeadingYawDeg = Flat.SizeSquared() > 1e-6 ? float(FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X))) : float(GetActorRotation().Yaw);

	// positive yaw turns right in Unreal
	const float YawDeg = HeadingYawDeg + float(SteerInput.X) * GetSettings().MaxYawAngle;
	const float PitchSign = GetSettings().bInvertPitch ? 1.f : -1.f;
	const float WantedElevationDeg = bLeveling ? 0.f : ElevationDeg + PitchSign * float(SteerInput.Y) * GetSettings().MaxPitchAngle;
	const float ElevationRad = FMath::DegreesToRadians(FMath::Clamp(WantedElevationDeg, -GetSettings().MaxClimbAngle, GetSettings().MaxClimbAngle));
	const float YawRad = FMath::DegreesToRadians(YawDeg);

	OutDirection = FVector(FMath::Cos(ElevationRad) * FMath::Cos(YawRad), FMath::Cos(ElevationRad) * FMath::Sin(YawRad), FMath::Sin(ElevationRad));
	OutStrength = FMath::Max(InputStrength, LevelStrength);
	return true;
}

void AFlockPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UFlockSubsystem* Flock = UFlockSubsystem::Get(this);
	if (!Flock || !Flock->IsHandleValid(Handle))
	{
		return;
	}

	// keys ramp up like a stick
	const float RampRate = GetSettings().KeyboardRampTime > 0.f ? 1.f / GetSettings().KeyboardRampTime : 1e6f;
	KeysRamped.X = FMath::FInterpConstantTo(float(KeysRamped.X), float(KeysTarget.X), DeltaTime, RampRate);
	KeysRamped.Y = FMath::FInterpConstantTo(float(KeysRamped.Y), float(KeysTarget.Y), DeltaTime, RampRate);

	const FVector2D Raw = KeysRamped + StickRaw;
	SteerInput = FVector2D(ShapeAxis(Raw.X), ShapeAxis(Raw.Y)).GetClampedToMaxSize(1.0);
	if (SteerInput.Y != 0.f)
	{
		LastPitchInputTime = GetWorld()->GetTimeSeconds();
	}

	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (bDashEndPending && Now >= DashEndTime)
		{
			bDashEndPending = false;
			OnDashEnded();
		}
		if (bDashReadyPending && Now >= DashReadyTime)
		{
			bDashReadyPending = false;
			OnDashReady();
		}
	}

	const FVector Heading = Flock->GetAgentHeading(Handle);

	// autopilot turns the wanted direction into stick values and ignores input
	if (bAutopilot)
	{
		const FVector ToTarget = AutopilotTarget - GetActorLocation();
		SteerInput = FVector2D::ZeroVector;
		if (ToTarget.SizeSquared() > 500.0 * 500.0)
		{
			const FVector Wanted = ToTarget.GetSafeNormal();
			const float WantedYaw = FMath::RadiansToDegrees(FMath::Atan2(Wanted.Y, Wanted.X));
			const float WantedElevation = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(float(Wanted.Z), -1.f, 1.f)));
			const FVector2D Flat(Heading.X, Heading.Y);
			const float HeadingYaw = Flat.SizeSquared() > 1e-6 ? float(FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X))) : float(GetActorRotation().Yaw);
			const float HeadingElevation = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(float(Heading.Z), -1.f, 1.f)));
			const float PitchSign = GetSettings().bInvertPitch ? 1.f : -1.f;
			SteerInput.X = FMath::Clamp(FMath::FindDeltaAngleDegrees(HeadingYaw, WantedYaw) / FMath::Max(GetSettings().MaxYawAngle, 1.f), -1.f, 1.f);
			SteerInput.Y = FMath::Clamp(PitchSign * (WantedElevation - HeadingElevation) / FMath::Max(GetSettings().MaxPitchAngle, 1.f), -1.f, 1.f);
		}
		// keep auto-level off so the nose is not pulled to the horizon
		LastPitchInputTime = GetWorld()->GetTimeSeconds();
	}

	// containment wins: outside the volume the group ignores input and auto-level
	bOutsideContainment = Flock->IsAgentOutsideContainment(Handle);

	const float SizeAlpha = FMath::Clamp(float(Flock->GetPlayerGroupSize() - 1) / float(FMath::Max(GetSettings().FullGroupSize - 1, 1)), 0.f, 1.f);

	// shaped by the curve, then smoothed so a joining bird does not cause a jump
	{
		const FRichCurve* Curve = GetSettings().FullnessCurve.GetRichCurveConst();
		FullnessTarget = FMath::Clamp(Curve && Curve->GetNumKeys() > 0 ? Curve->Eval(SizeAlpha) : SizeAlpha, 0.f, 1.f);

		// frame rate independent exponential smoothing
		const float SmoothTime = GetSettings().FullnessSmoothTime;
		const float Alpha = SmoothTime > 0.f ? 1.f - FMath::Exp(-DeltaTime / SmoothTime) : 1.f;
		Fullness = FMath::Lerp(Fullness, FullnessTarget, Alpha);
	}

	const float ManeuverWeight = Flock->GetPlayerManeuverWeight();
	const float JoinRadius = FMath::Lerp(GetSettings().MinJoinRadius, GetSettings().MaxJoinRadius, SizeAlpha);
	const float LeaveRadius = JoinRadius * GetSettings().LeaveRadiusMultiplier;
	Flock->SetPlayerJoinRadius(JoinRadius, LeaveRadius);

	// the subsystem's own debug drawing follows this switch
	const bool bShowDebug = GetSettings().bShowDebug;
	Flock->SetDebugDrawEnabled(bShowDebug);

	if (bShowDebug && GetSettings().bShowJoinRadius)
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), JoinRadius, 32, FColor::Green, false, -1.f, 0, 2.f);
		DrawDebugSphere(GetWorld(), GetActorLocation(), LeaveRadius, 32, FColor::Orange, false, -1.f, 0, 2.f);
	}

	// the loose V behind the player
	{
		const UFlockPlayerSettings& S = GetSettings();
		const float SideSpread = FMath::Tan(FMath::DegreesToRadians(S.WedgeHalfAngle));
		FFlockFollowSettings Follow;
		Follow.bEnabled = S.bFollowWedge;
		Follow.MinDepth = S.WedgeMinDepth;
		Follow.MaxDepth = FMath::Max(FMath::Lerp(S.WedgeLengthAlone, S.WedgeLengthFull, SizeAlpha), S.WedgeMinDepth);
		Follow.SideSpread = SideSpread;
		Follow.VerticalSpread = SideSpread * S.WedgeVerticalScale;
		Follow.Weight = S.FollowWeight;
		Follow.FullForceDistance = S.FollowFullForceDistance;
		Follow.SpeedAdjust = S.FollowSpeedAdjust;
		Follow.SpeedAdjustDistance = S.FollowSpeedAdjustDistance;
		Flock->SetPlayerFollow(Follow);

		if (bShowDebug && S.bFollowWedge && S.bShowWedge)
		{
			const FVector Forward = Heading.GetSafeNormal();
			const FVector Right = FVector(-Forward.Y, Forward.X, 0.0).GetSafeNormal();
			const FVector Tip = GetActorLocation();
			const FVector FarLeft = Tip - Forward * Follow.MaxDepth - Right * (Follow.MaxDepth * SideSpread);
			const FVector FarRight = Tip - Forward * Follow.MaxDepth + Right * (Follow.MaxDepth * SideSpread);
			DrawDebugLine(GetWorld(), Tip, FarLeft, FColor::Cyan, false, -1.f, 0, 3.f);
			DrawDebugLine(GetWorld(), Tip, FarRight, FColor::Cyan, false, -1.f, 0, 3.f);
			DrawDebugLine(GetWorld(), FarLeft, FarRight, FColor::Cyan, false, -1.f, 0, 3.f);
		}
	}

	// debug shape of a running move: bottom and top circle and the axis
	{
		const EFlockManeuver Move = Flock->GetPlayerManeuver();
		const FFlockTornadoSettings& TornadoSettings = GetSettings().Tornado;
		const FFlockLoopSettings& LoopSettings = GetSettings().Loop;
		const bool bDrawShape = bShowDebug
			&& ((Move == EFlockManeuver::Tornado && TornadoSettings.bShowFunnel) || (Move == EFlockManeuver::Loop && LoopSettings.bShowWheel)
				|| (Move == EFlockManeuver::Corkscrew && GetSettings().Corkscrew.bShowShape)
				|| (Move == EFlockManeuver::Halo && GetSettings().Halo.bShowShape));
		if (bDrawShape)
		{
			const FVector Center = Flock->GetPlayerManeuverCenter();
			const FFlockTornadoParams& Shape = Flock->GetPlayerTornadoParams();
			const FVector Axis = FVector(Shape.Axis);
			const FVector Bottom = Center - Axis * (Shape.Height * 0.5f);
			const FVector Top = Center + Axis * (Shape.Height * 0.5f);

			// two directions across the axis to draw the circles in
			FVector AcrossA = FVector::CrossProduct(Axis, FVector::ZAxisVector);
			AcrossA = AcrossA.SizeSquared() > 0.01 ? AcrossA.GetSafeNormal() : FVector::XAxisVector;
			const FVector AcrossB = FVector::CrossProduct(Axis, AcrossA).GetSafeNormal();

			const FColor ShapeColor = Move == EFlockManeuver::Loop ? FColor::Magenta : (Move == EFlockManeuver::Corkscrew ? FColor::Orange : (Move == EFlockManeuver::Halo ? FColor::Turquoise : FColor::Yellow));
			DrawDebugCircle(GetWorld(), Bottom, Shape.RadiusBottom, 48, ShapeColor, false, -1.f, 0, 3.f, AcrossA, AcrossB, false);
			DrawDebugCircle(GetWorld(), Top, Shape.RadiusTop, 48, ShapeColor, false, -1.f, 0, 3.f, AcrossA, AcrossB, false);
			DrawDebugLine(GetWorld(), Bottom, Top, ShapeColor, false, -1.f, 0, 3.f);
		}
	}

	// the bigger the flock, the harder it turns
	const float Agility = FMath::Lerp(1.f, GetSettings().SteerAtFullGroup, Fullness);

	FVector Urge = FVector::ZeroVector;
	FVector Direction;
	float Strength;
	// during a corkscrew the input turns the guide line, not the bird spiraling around it
	const FVector SteerHeading = Flock->GetPlayerManeuver() == EFlockManeuver::Corkscrew ? Flock->GetPlayerGuideHeading() : Heading;
	if (!bOutsideContainment && Evaluate(SteerHeading, Direction, Strength))
	{
		Urge = Direction * (Strength * Agility);
	}

	// every group member gets the same urge
	Flock->SetPlayerUrge(Urge);
	Flock->SetPlayerAgilityScale(Agility);

	// group speed is the product of flock size, dash, dive and special move scales
	const float GrowthScale = FMath::Lerp(1.f, GetSettings().SpeedAtFullGroup, Fullness);
	// the dash eases in and out through an envelope instead of switching
	{
		const float TargetEnvelope = IsDashing() ? 1.f : 0.f;
		const float SmoothTime = TargetEnvelope > DashEnvelope ? GetSettings().DashAttackTime : GetSettings().DashFadeOutTime;
		const float Alpha = SmoothTime > 0.f ? 1.f - FMath::Exp(-3.f * DeltaTime / SmoothTime) : 1.f;
		DashEnvelope = FMath::Lerp(DashEnvelope, TargetEnvelope, Alpha);
		if (TargetEnvelope == 0.f && DashEnvelope < 0.002f)
		{
			DashEnvelope = 0.f;
		}
	}
	const float DashScale = 1.f + (GetSettings().DashSpeedMultiplier - 1.f) * DashEnvelope;
	// a move that takes over fades gravity out, or a circling group would speed up every lap
	const float DiveAmount = FMath::Max(-float(Heading.Z), 0.f);
	const float DiveScale = FMath::Min(1.f + DiveAmount * GetSettings().GravityStrength, GetSettings().MaxGravitySpeedScale);
	const float GravityScale = FMath::Lerp(DiveScale, 1.f, Flock->IsPlayerManeuverOverriding() ? ManeuverWeight : 0.f);
	const float ManeuverSpeedScale = FMath::Lerp(1.f, ActiveMoveSpeed, ManeuverWeight);
	BoostScale = DashScale * GravityScale;
	Flock->SetPlayerSpeedScale(GrowthScale * BoostScale * ManeuverSpeedScale);
}
