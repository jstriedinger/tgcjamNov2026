// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Math/Interval.h"
#include "FlockTypes.h"
#include "FlockPlayer.generated.h"

class AFlockContainmentVolume;
class UFlockCallComponent;
class UFlockCallProfile;
class UFlockPlayerSettings;
class UFlockTypeDefinition;
class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;
struct FInputActionValue;

/** The player's bird */
UCLASS(Blueprintable)
class TGJFLYING_API AFlockPlayer : public APawn
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Plays the call effect and fires On Called */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFlockCallComponent> CallComponent;

public:

	AFlockPlayer();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** The flock type the bird joins */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
	TObjectPtr<UFlockTypeDefinition> FlockType;

	/** Keeps the bird inside this volume */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
	TObjectPtr<AFlockContainmentVolume> ContainmentVolume;

	/** Sound, bubble and answer settings of the player's call */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Call")
	TObjectPtr<UFlockCallProfile> CallProfile;

	/** The bird rolls its cruise speed (cm/s) in this range when it spawns */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock")
	FFloatInterval CruiseSpeedRange = FFloatInterval(500.f, 700.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock", meta = (ClampMin = 0, Units = "cm"))
	float BodyRadius = 60.f;

	/** Tunable numbers for the player and its group */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock")
	TObjectPtr<UFlockPlayerSettings> Settings;

	/** The settings in use: the asset, or the defaults when none is set */
	const UFlockPlayerSettings& GetSettings() const;

	/** Steers the bird toward a point as the stick would, ignoring input (for cinematics) */
	UFUNCTION(BlueprintCallable, Category="Flight")
	void SetAutopilotTarget(FVector Location);

	UFUNCTION(BlueprintCallable, Category="Flight")
	void ClearAutopilot();

	/** Tries to dash */
	UFUNCTION(BlueprintCallable, Category="Flight")
	bool TryDash();

	/** A dash just started */
	UFUNCTION(BlueprintImplementableEvent, Category="Flight")
	void OnDashStarted();

	/** The dash burst just ended */
	UFUNCTION(BlueprintImplementableEvent, Category="Flight")
	void OnDashEnded();

	/** The cooldown is over: the next dash is allowed */
	UFUNCTION(BlueprintImplementableEvent, Category="Flight")
	void OnDashReady();

	UFUNCTION(BlueprintPure, Category="Flight")
	bool IsDashing() const;

	/** Seconds until the next dash is allowed, 0 when ready */
	UFUNCTION(BlueprintPure, Category="Flight")
	float GetDashCooldownRemaining() const;

	/** Starts the tornado: the group circles a point in a funnel */
	UFUNCTION(BlueprintCallable, Category="Moves")
	bool TryTornado();

	/** The tornado just started */
	UFUNCTION(BlueprintImplementableEvent, Category="Moves")
	void OnTornadoStarted();

	/** Starts the loop: the group circles in a vertical wheel */
	UFUNCTION(BlueprintCallable, Category="Moves")
	bool TryLoop();

	UFUNCTION(BlueprintImplementableEvent, Category="Moves")
	void OnLoopStarted();

	/** Starts the corkscrew: the group spirals around a line that moves forward */
	UFUNCTION(BlueprintCallable, Category="Moves")
	bool TryCorkscrew();

	UFUNCTION(BlueprintImplementableEvent, Category="Moves")
	void OnCorkscrewStarted();

	/** Starts the halo: a spinning tube of your flock ahead that you fly through */
	UFUNCTION(BlueprintCallable, Category="Moves")
	bool TryHalo();

	UFUNCTION(BlueprintImplementableEvent, Category="Moves")
	void OnHaloStarted();

	/** Starts the ripple: a wave runs through the V behind you */
	UFUNCTION(BlueprintCallable, Category="Moves")
	bool TryRipple();

	UFUNCTION(BlueprintImplementableEvent, Category="Moves")
	void OnRippleStarted();

	/** Tier (1 = first) of the special move that is running or ran last */
	UFUNCTION(BlueprintPure, Category="Moves")
	int32 GetMoveTier() const { return ActiveMoveTier; }

	/** True while a special move is running, fades included */
	UFUNCTION(BlueprintPure, Category="Moves")
	bool IsInManeuver() const;

	/** Seconds until the next special move is allowed, 0 when ready */
	UFUNCTION(BlueprintPure, Category="Moves")
	float GetMoveCooldownRemaining() const;

	/** The handle of this bird in the flock simulation */
	FFlockAgentHandle GetFlockHandle() const { return Handle; }

	/** Used by the camera manager once per frame */
	bool ConsumeLook(float DeltaTime, FVector2D& OutDeltaDegrees);

	/** Birds in the player's group, the player included */
	UFUNCTION(BlueprintPure, Category="Flock")
	int32 GetGroupSize() const;

	/** Group size as 0 (alone) to 1 (FullGroupSize birds), shaped by the fullness curve and smoothed */
	UFUNCTION(BlueprintPure, Category="Flock")
	float GetFlockFullness() const { return Fullness; }

	/** The fullness before smoothing */
	UFUNCTION(BlueprintPure, Category="Flock")
	float GetFlockFullnessTarget() const { return FullnessTarget; }

	/** True while outside the containment volume (input is ignored) */
	UFUNCTION(BlueprintPure, Category="Flock")
	bool IsOutsideContainment() const { return bOutsideContainment; }

	/** The shaped steering input, X right, Y up on the stick */
	UFUNCTION(BlueprintPure, Category="Flight")
	FVector2D GetSteerInput() const { return SteerInput; }

	/** The real velocity in cm/s, boosts included */
	UFUNCTION(BlueprintPure, Category="Flight|Speed")
	FVector GetFlightVelocity() const;

	/** Actual speed divided by the bird's own cruise speed */
	UFUNCTION(BlueprintPure, Category="Flight|Speed")
	float GetSpeedRatio() const;

	/** The speed ratio mapped to 0..1 between Speed Feel Min and Max */
	UFUNCTION(BlueprintPure, Category="Flight|Speed")
	float GetSpeedAlpha() const;

	/** The speed multiplier from dash and dive, 1 = none */
	UFUNCTION(BlueprintPure, Category="Flight|Speed")
	float GetBoostScale() const { return BoostScale; }

private:

	void RegisterWithFlock();

	void OnKeysSteer(const FInputActionValue& Value);
	void OnKeysSteerEnd(const FInputActionValue& Value);
	void OnStickSteer(const FInputActionValue& Value);
	void OnStickSteerEnd(const FInputActionValue& Value);
	void OnLookStick(const FInputActionValue& Value);
	void OnLookStickEnd(const FInputActionValue& Value);
	void OnLookMouse(const FInputActionValue& Value);
	void OnDashInput(const FInputActionValue& Value);
	void OnTornadoInput(const FInputActionValue& Value);
	void OnLoopInput(const FInputActionValue& Value);
	void OnCorkscrewInput(const FInputActionValue& Value);
	void OnHaloInput(const FInputActionValue& Value);
	void OnRippleInput(const FInputActionValue& Value);
	void OnCallInput(const FInputActionValue& Value);

	/** Applies dead zone, rescale and response curve to one axis */
	float ShapeAxis(float Raw) const;

	/** The direction and strength the input asks for, false when it asks for nothing */
	bool Evaluate(const FVector& Heading, FVector& OutDirection, float& OutStrength) const;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> KeysAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StickAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookStickAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookMouseAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DashAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TornadoAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoopAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CorkscrewAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> HaloAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RippleAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CallAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	FFlockAgentHandle Handle;
	int32 FlockId = INDEX_NONE;

	FVector2D KeysTarget = FVector2D::ZeroVector;
	FVector2D KeysRamped = FVector2D::ZeroVector;
	FVector2D StickRaw = FVector2D::ZeroVector;

	FVector2D LookStickRaw = FVector2D::ZeroVector;

	/** Mouse counts collected since the camera last asked */
	FVector2D LookMouseAccumulated = FVector2D::ZeroVector;
	FVector2D SteerInput = FVector2D::ZeroVector;

	bool bAutopilot = false;
	FVector AutopilotTarget = FVector::ZeroVector;

	/** World time the up/down input was last non-zero */
	double LastPitchInputTime = 0.0;

	bool bOutsideContainment = false;

	float Fullness = 0.f;
	float FullnessTarget = 0.f;

	/** World times the current dash ends and the next one is allowed */
	double DashEndTime = -1.0;
	double DashReadyTime = -1.0;

	/** Make OnDashEnded / OnDashReady fire once per dash */
	bool bDashEndPending = false;
	bool bDashReadyPending = false;

	float BoostScale = 1.f;

	/** How much of the dash boost is on right now, 0..1 */
	float DashEnvelope = 0.f;

	/** Tier number and speed multiplier of the running (or last) special move */
	int32 ActiveMoveTier = 0;
	float ActiveMoveSpeed = 1.f;

	/** World time the next special move is allowed; the cooldown counts from the end of the move */
	double MoveReadyTime = -1.0;
};
