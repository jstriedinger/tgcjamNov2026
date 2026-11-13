// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "Math/Interval.h"
#include "FlockTypes.h"
#include "FlockDataTypes.generated.h"

class UFlockBehavior;
class UFMODEvent;
class UMaterialInterface;

/** A kind of flock: how neighbors are gathered and which behaviors steer it */
UCLASS(BlueprintType)
class TGJFLYING_API UFlockTypeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UFlockTypeDefinition();

	/** Agents closer than this, measured between bodies, are neighbors */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Neighbors", meta = (ClampMin = 1, Units = "cm"))
	float NeighborRadius = 400.f;

	/** At most this many neighbors feed alignment and cohesion */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Neighbors", meta = (ClampMin = 1, ClampMax = 32))
	int32 MaxNeighbors = 20;

	/** Cap of the summed flocking forces */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Steering", meta = (ClampMin = 0))
	float MaxSteerForce = 5.f;

	/** External forces (containment) are capped at MaxSteerForce times this */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Steering", meta = (ClampMin = 0))
	float ExternalForceMultiplier = 2.f;

	/** How fast the speed multiplier rises toward a boost, per second */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Steering", meta = (ClampMin = 0))
	float SpeedBoostInRate = 8.f;

	/** How fast the speed multiplier falls back to 1, per second */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Steering", meta = (ClampMin = 0))
	float SpeedBoostOutRate = 3.f;

	/** How fast the actor turns to face its velocity */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Steering", meta = (ClampMin = 0))
	float TurnSpeed = 10.f;

	/** Birds lean into turns; only the mesh rolls, never the actor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Banking")
	bool bBanking = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Banking", meta = (EditCondition = "bBanking", ClampMin = 0, ClampMax = 90, Units = "deg"))
	float MaxBankAngle = 40.f;

	/** Degrees of lean per degree/s of turning */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Banking", meta = (EditCondition = "bBanking", ClampMin = 0))
	float BankPerTurnRate = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Banking", meta = (EditCondition = "bBanking", ClampMin = 0.01, Units = "s"))
	float BankSmoothTime = 0.3f;

	/** While the flock is scared (storm), a bird runs away when the player is closer than this */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scared", meta = (ClampMin = 0, Units = "cm"))
	float ShyAvoidRadius = 3000.f;

	/** Steering strength away from the player while scared */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scared", meta = (ClampMin = 0))
	float ShyAvoidWeight = 6.f;

	/** Speed multiplier of a fleeing bird, applied to the faster of its own and the player's cruise speed */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scared", meta = (ClampMin = 1, ClampMax = 4))
	float ShySpeedMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category="Steering")
	TArray<TObjectPtr<UFlockBehavior>> Behaviors;
};

/** One level of the tornado */
USTRUCT(BlueprintType)
struct FFlockTornadoTier
{
	GENERATED_BODY()

	/** Birds needed in the group, you included, for this tier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 1))
	int32 MinGroupSize = 5;

	/** Group speed during the move, as a multiple of normal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0.1))
	float SpeedMultiplier = 1.2f;

	/** Scales the funnel radii */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0.1))
	float RadiusScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0.1))
	float HeightScale = 1.f;

	/** The move ends with a finale: the circles tighten and the group shoots up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado")
	bool bFinale = false;
};

/** The tornado: the group circles a point in a funnel, wide at the top */
USTRUCT(BlueprintType)
struct FFlockTornadoSettings
{
	GENERATED_BODY()

	FFlockTornadoSettings()
	{
		FFlockTornadoTier First;
		First.MinGroupSize = 5;
		First.SpeedMultiplier = 1.2f;
		First.RadiusScale = 1.f;
		First.HeightScale = 1.f;
		FFlockTornadoTier Second;
		Second.MinGroupSize = 10;
		Second.SpeedMultiplier = 1.6f;
		Second.RadiusScale = 1.25f;
		Second.HeightScale = 1.3f;
		FFlockTornadoTier Third;
		Third.MinGroupSize = 15;
		Third.SpeedMultiplier = 2.f;
		Third.RadiusScale = 1.5f;
		Third.HeightScale = 1.6f;
		Third.bFinale = true;
		Tiers = { First, Second, Third };
	}

	/** The levels */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado")
	TArray<FFlockTornadoTier> Tiers;

	/** Length of the move in seconds, fades included */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0.5, Units = "s"))
	float Duration = 5.f;

	/** Seconds the move takes to take over from normal flying */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0, Units = "s"))
	float BlendInTime = 0.5f;

	/** Seconds at the end it takes to give control back */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0, Units = "s"))
	float BlendOutTime = 0.8f;

	/** Seconds after the move ends before the next one is allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0, Units = "s"))
	float Cooldown = 8.f;

	/** Radius of the funnel at the bottom and at the top */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 50, Units = "cm"))
	float RadiusBottom = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 50, Units = "cm"))
	float RadiusTop = 900.f;

	/** Each bird's radius varies by this fraction, so the birds do not form clean rings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0, ClampMax = 0.5))
	float RadiusJitter = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0, Units = "cm"))
	float Height = 600.f;

	/** Each bird drifts up and down by this much */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0, Units = "cm"))
	float BobHeight = 60.f;

	/** How hard a bird turns toward its wanted direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0))
	float SteerGain = 4.f;

	/** Push back toward the radius, and toward the height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0))
	float RadialWeight = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 0))
	float VerticalWeight = 0.8f;

	/** A bird this far off gets the full push back */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado", meta = (ClampMin = 1, Units = "cm"))
	float CatchDistance = 800.f;

	/** Extra seconds added to the move for the finale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado|Finale", meta = (ClampMin = 0, Units = "s"))
	float FinaleTime = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado|Finale", meta = (ClampMin = 0, Units = "cm"))
	float FinaleRise = 1800.f;

	/** Fraction the circles shrink to in the finale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado|Finale", meta = (ClampMin = 0.1, ClampMax = 1))
	float FinaleRadiusScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado|Finale", meta = (ClampMin = 0))
	float FinaleClimbStrength = 2.5f;

	/** Extra camera distance during the move */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado|Camera", meta = (ClampMin = 0, Units = "cm"))
	float CameraExtraDistance = 1200.f;

	/** How much the camera looks at the tornado center instead of your bird */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado|Camera", meta = (ClampMin = 0, ClampMax = 1))
	float CameraFocusOnCenter = 0.7f;

	/** Draws the funnel circles while the move runs, when Show Debug is on */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tornado")
	bool bShowFunnel = true;
};

/** One level of the loop */
USTRUCT(BlueprintType)
struct FFlockLoopTier
{
	GENERATED_BODY()

	/** Birds needed in the group, you included, for this tier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 1))
	int32 MinGroupSize = 8;

	/** Group speed during the loop, as a multiple of normal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0.1))
	float SpeedMultiplier = 1.3f;

	/** Scales the loop radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0.1))
	float RadiusScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0.25))
	float Laps = 1.f;
};

/** The loop: the group circles in a vertical wheel */
USTRUCT(BlueprintType)
struct FFlockLoopSettings
{
	GENERATED_BODY()

	FFlockLoopSettings()
	{
		FFlockLoopTier First;
		First.MinGroupSize = 8;
		First.SpeedMultiplier = 1.3f;
		First.RadiusScale = 1.f;
		First.Laps = 1.f;
		FFlockLoopTier Second;
		Second.MinGroupSize = 12;
		Second.SpeedMultiplier = 1.6f;
		Second.RadiusScale = 1.2f;
		Second.Laps = 1.f;
		FFlockLoopTier Third;
		Third.MinGroupSize = 16;
		Third.SpeedMultiplier = 1.9f;
		Third.RadiusScale = 1.4f;
		Third.Laps = 2.f;
		Tiers = { First, Second, Third };
	}

	/** The levels */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop")
	TArray<FFlockLoopTier> Tiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 100, Units = "cm"))
	float Radius = 700.f;

	/** Each bird's radius varies by this fraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0, ClampMax = 0.5))
	float RadiusJitter = 0.1f;

	/** Width of the wheel across the flight direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0, Units = "cm"))
	float Width = 600.f;

	/** Each bird drifts sideways by this much */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0, Units = "cm"))
	float BobWidth = 50.f;

	/** Seconds added to the lap time for the fades */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0, Units = "s"))
	float ExtraTime = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0, Units = "s"))
	float BlendInTime = 0.3f;

	/** Seconds at the end it takes to give control back */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0, Units = "s"))
	float BlendOutTime = 0.5f;

	/** Seconds after the move ends before the next one is allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0, Units = "s"))
	float Cooldown = 8.f;

	/** How hard a bird turns toward its wanted direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0))
	float SteerGain = 4.f;

	/** Push back toward the radius, and toward the place across the wheel */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0))
	float RadialWeight = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 0))
	float SideWeight = 0.8f;

	/** A bird this far off gets the full push back */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop", meta = (ClampMin = 1, Units = "cm"))
	float CatchDistance = 800.f;

	/** Extra camera distance during the loop */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop|Camera", meta = (ClampMin = 0, Units = "cm"))
	float CameraExtraDistance = 1000.f;

	/** How much the camera looks at the wheel center instead of your bird */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop|Camera", meta = (ClampMin = 0, ClampMax = 1))
	float CameraFocusOnCenter = 0.6f;

	/** The camera turns to see the wheel from the side */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop|Camera")
	bool bCameraFacesWheel = true;

	/** Draws the wheel circles while the move runs, when Show Debug is on */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loop")
	bool bShowWheel = true;
};

/** One level of the ripple */
USTRUCT(BlueprintType)
struct FFlockRippleTier
{
	GENERATED_BODY()

	/** Birds needed in the group, you included, for this tier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 1))
	int32 MinGroupSize = 3;

	/** How far the wave moves the birds up and down at its crest */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 0, Units = "cm"))
	float Amplitude = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 50, Units = "cm"))
	float Wavelength = 1300.f;

	/** Seconds a crest takes to pass a place, smaller is faster */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 0.2, Units = "s"))
	float Period = 3.f;
};

/** The ripple: a wave runs through the V behind you, front to back */
USTRUCT(BlueprintType)
struct FFlockRippleSettings
{
	GENERATED_BODY()

	FFlockRippleSettings()
	{
		FFlockRippleTier First;
		First.MinGroupSize = 3;
		First.Amplitude = 220.f;
		First.Wavelength = 1300.f;
		First.Period = 3.f;
		FFlockRippleTier Second;
		Second.MinGroupSize = 7;
		Second.Amplitude = 350.f;
		Second.Wavelength = 1100.f;
		Second.Period = 2.4f;
		FFlockRippleTier Third;
		Third.MinGroupSize = 12;
		Third.Amplitude = 500.f;
		Third.Wavelength = 900.f;
		Third.Period = 1.9f;
		Tiers = { First, Second, Third };
	}

	/** The levels */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple")
	TArray<FFlockRippleTier> Tiers;

	/** Length of the move in seconds, fades included */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 0.5, Units = "s"))
	float Duration = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 0, Units = "s"))
	float BlendInTime = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 0, Units = "s"))
	float BlendOutTime = 1.f;

	/** Seconds after the move ends before the next one is allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 0, Units = "s"))
	float Cooldown = 6.f;

	/** The V pulls the birds this many times harder during the ripple */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 1))
	float FollowBoost = 2.f;

	/** How hard your bird steers to rise and fall with the wave */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ripple", meta = (ClampMin = 0))
	float PlayerWaveGain = 4.f;
};

/** One level of the corkscrew */
USTRUCT(BlueprintType)
struct FFlockCorkscrewTier
{
	GENERATED_BODY()

	/** Birds needed in the group, you included, for this tier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 1))
	int32 MinGroupSize = 10;

	/** Group speed during the move, as a multiple of normal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0.1))
	float SpeedMultiplier = 1.3f;

	/** Scales the spiral radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0.1))
	float RadiusScale = 1.f;

	/** The move ends with a finale: the spiral tightens and the group shoots forward */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew")
	bool bFinale = false;
};

/** The corkscrew: the group spirals around a line that keeps moving forward */
USTRUCT(BlueprintType)
struct FFlockCorkscrewSettings
{
	GENERATED_BODY()

	FFlockCorkscrewSettings()
	{
		FFlockCorkscrewTier First;
		First.MinGroupSize = 10;
		First.SpeedMultiplier = 1.3f;
		First.RadiusScale = 1.f;
		FFlockCorkscrewTier Second;
		Second.MinGroupSize = 14;
		Second.SpeedMultiplier = 1.6f;
		Second.RadiusScale = 1.3f;
		FFlockCorkscrewTier Third;
		Third.MinGroupSize = 18;
		Third.SpeedMultiplier = 1.9f;
		Third.RadiusScale = 1.5f;
		Third.bFinale = true;
		Tiers = { First, Second, Third };
	}

	/** The levels */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew")
	TArray<FFlockCorkscrewTier> Tiers;

	/** Length of the move in seconds, fades included */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0.5, Units = "s"))
	float Duration = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0, Units = "s"))
	float BlendInTime = 0.5f;

	/** Seconds at the end it takes to give control back */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0, Units = "s"))
	float BlendOutTime = 0.8f;

	/** Seconds after the move ends before the next one is allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0, Units = "s"))
	float Cooldown = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 50, Units = "cm"))
	float Radius = 500.f;

	/** Each bird's radius varies by this fraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0, ClampMax = 0.5))
	float RadiusJitter = 0.12f;

	/** Length of the spiral along the flight line */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0, Units = "cm"))
	float Length = 1200.f;

	/** Each bird drifts forward and back by this much */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0, Units = "cm"))
	float BobLength = 60.f;

	/** Forward speed of the spiral, as a fraction of the birds' speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0, ClampMax = 0.9))
	float ForwardSpeedFraction = 0.55f;

	/** How hard a bird turns toward its wanted direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0))
	float SteerGain = 4.f;

	/** Push back toward the radius, and toward the place along the line */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0))
	float RadialWeight = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 0))
	float AlongWeight = 0.8f;

	/** A bird this far off gets the full push back */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew", meta = (ClampMin = 1, Units = "cm"))
	float CatchDistance = 800.f;

	/** Extra seconds added to the move for the finale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew|Finale", meta = (ClampMin = 0, Units = "s"))
	float FinaleTime = 1.2f;

	/** How far ahead of the line the group is sent in the finale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew|Finale", meta = (ClampMin = 0, Units = "cm"))
	float FinaleBurst = 2000.f;

	/** Fraction the spiral shrinks to in the finale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew|Finale", meta = (ClampMin = 0.1, ClampMax = 1))
	float FinaleRadiusScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew|Finale", meta = (ClampMin = 0))
	float FinaleStrength = 2.5f;

	/** Extra camera distance during the move */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew|Camera", meta = (ClampMin = 0, Units = "cm"))
	float CameraExtraDistance = 700.f;

	/** How much the camera looks at the spiral center instead of your bird */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew|Camera", meta = (ClampMin = 0, ClampMax = 1))
	float CameraFocusOnCenter = 0.9f;

	/** Draws the end circles while the move runs, when Show Debug is on */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Corkscrew")
	bool bShowShape = true;
};

/** One level of the halo */
USTRUCT(BlueprintType)
struct FFlockHaloTier
{
	GENERATED_BODY()

	/** Birds needed in the group, you included, for this tier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 1))
	int32 MinGroupSize = 6;

	/** How much faster than you the rest of the group flies */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 1))
	float SpeedMultiplier = 1.5f;

	/** Scales the tube radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0.1))
	float RadiusScale = 1.f;

	/** Scales the tube length */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0.1))
	float LengthScale = 1.f;
};

/** The halo: a spinning tube of your flock ahead of you, which you fly through */
USTRUCT(BlueprintType)
struct FFlockHaloSettings
{
	GENERATED_BODY()

	FFlockHaloSettings()
	{
		FFlockHaloTier First;
		First.MinGroupSize = 6;
		First.SpeedMultiplier = 1.5f;
		First.RadiusScale = 1.f;
		First.LengthScale = 1.f;
		FFlockHaloTier Second;
		Second.MinGroupSize = 11;
		Second.SpeedMultiplier = 1.8f;
		Second.RadiusScale = 1.25f;
		Second.LengthScale = 1.3f;
		FFlockHaloTier Third;
		Third.MinGroupSize = 16;
		Third.SpeedMultiplier = 2.1f;
		Third.RadiusScale = 1.5f;
		Third.LengthScale = 1.6f;
		Tiers = { First, Second, Third };
	}

	/** The levels */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo")
	TArray<FFlockHaloTier> Tiers;

	/** Length of the move in seconds, fades included */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0.5, Units = "s"))
	float Duration = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0, Units = "s"))
	float BlendInTime = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0, Units = "s"))
	float BlendOutTime = 0.8f;

	/** Seconds after the move ends before the next one is allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0, Units = "s"))
	float Cooldown = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 50, Units = "cm"))
	float Radius = 400.f;

	/** Each bird's radius varies by this fraction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0, ClampMax = 0.5))
	float RadiusJitter = 0.15f;

	/** Length of the tube along the flight direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0, Units = "cm"))
	float Length = 1500.f;

	/** Each bird drifts forward and back by this much */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0, Units = "cm"))
	float BobLength = 80.f;

	/** Forward speed of the tube, as a fraction of your speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0, ClampMax = 0.9))
	float ForwardSpeedFraction = 0.5f;

	/** When you reach the middle of the tube, as a fraction of the move */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0.2, ClampMax = 0.9))
	float ReachMiddleAt = 0.6f;

	/** How hard a bird turns toward its wanted direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0))
	float SteerGain = 4.f;

	/** Push back toward the radius, and toward the place along the tube */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0))
	float RadialWeight = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 0))
	float AlongWeight = 0.8f;

	/** A bird this far off gets the full push back */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo", meta = (ClampMin = 1, Units = "cm"))
	float CatchDistance = 800.f;

	/** Extra camera distance during the move */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo|Camera", meta = (ClampMin = 0, Units = "cm"))
	float CameraExtraDistance = 400.f;

	/** How much the camera looks at the tube center instead of your bird */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo|Camera", meta = (ClampMin = 0, ClampMax = 1))
	float CameraFocusOnCenter = 0.3f;

	/** Draws the end circles while the move runs, when Show Debug is on */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Halo")
	bool bShowShape = true;
};

/** Everything about one kind of bird's call: sound, squash and stretch, bubble, answer timing */
UCLASS(BlueprintType)
class TGJFLYING_API UFlockCallProfile : public UDataAsset
{
	GENERATED_BODY()

public:

	UFlockCallProfile();

	/** FMOD event played when this kind of bird calls */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Call")
	TObjectPtr<UFMODEvent> CallSound;

	/** Seconds after a call before this bird can call again */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Call", meta = (ClampMin = 0, Units = "s"))
	float Cooldown = 0.5f;

	/** Seconds the bird squashes before the call comes out */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Squash and Stretch", meta = (ClampMin = 0, Units = "s"))
	float SquashTime = 0.08f;

	/** Length scale at the deepest squash */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Squash and Stretch", meta = (ClampMin = 0.2, ClampMax = 1))
	float SquashAmount = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Squash and Stretch", meta = (ClampMin = 0, Units = "s"))
	float StretchTime = 0.1f;

	/** Length scale at the full stretch */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Squash and Stretch", meta = (ClampMin = 1, ClampMax = 3))
	float StretchAmount = 1.4f;

	/** Seconds to wobble back to normal shape after the stretch */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Squash and Stretch", meta = (ClampMin = 0.01, Units = "s"))
	float SettleTime = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Squash and Stretch", meta = (ClampMin = 0))
	float SettleWobbles = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bubble")
	TObjectPtr<UMaterialInterface> BubbleMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bubble")
	FLinearColor BubbleColor = FLinearColor(1.f, 0.93f, 0.78f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bubble", meta = (ClampMin = 1, Units = "cm"))
	float BubbleStartRadius = 60.f;

	/** Radius the bubble grows to */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bubble", meta = (ClampMin = 1, Units = "cm"))
	float BubbleEndRadius = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bubble", meta = (ClampMin = 0.05, Units = "s"))
	float BubbleDuration = 0.9f;

	/** Random wait before this bird answers a call it hears while in your group */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Answers", meta = (Units = "s"))
	FFloatInterval AnswerDelayRange = FFloatInterval(0.2f, 0.6f);

	/** Player profile only: how far the last caller's call is heard by the next bird */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Answers", meta = (ClampMin = 0, Units = "cm"))
	float HearingRadius = 800.f;

	/** Player profile only: most birds that answer one call in a chain */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Answers", meta = (ClampMin = 0))
	int32 MaxChainLength = 6;
};

/** Every tunable number of the player's bird, flock growth and chase camera */
UCLASS(BlueprintType)
class TGJFLYING_API UFlockPlayerSettings : public UDataAsset
{
	GENERATED_BODY()

public:

	UFlockPlayerSettings();

	/** Master switch for all debug drawing while playing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	bool bShowDebug = true;

	/** Agents that left the group less than this long ago still count as members */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Group", meta = (ClampMin = 0, Units = "s"))
	float MemberGraceTime = 0.5f;

	/** A bird within this radius of you can join the group */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Group", meta = (ClampMin = 0, Units = "cm"))
	float MinJoinRadius = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Group", meta = (ClampMin = 0, Units = "cm"))
	float MaxJoinRadius = 1800.f;

	/** A member leaves when farther than join radius times this */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Group", meta = (ClampMin = 1))
	float LeaveRadiusMultiplier = 1.5f;

	/** Draws the join radius (green) and leave radius (orange) around the bird */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Group")
	bool bShowJoinRadius = true;

	/** Group size at which everything that grows with the flock is at its full value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Group", meta = (ClampMin = 2))
	int32 FullGroupSize = 100;

	/** Maps group size (0..1) to fullness (0..1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flock|Fullness")
	FRuntimeFloatCurve FullnessCurve;

	/** Roughly how many seconds fullness takes to catch up when birds join or leave */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Fullness", meta = (ClampMin = 0, Units = "s"))
	float FullnessSmoothTime = 1.25f;

	/** Group speed multiplier when the flock is full */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Fullness", meta = (ClampMin = 0.1))
	float SpeedAtFullGroup = 1.4f;

	/** Steer strength multiplier when the flock is full */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Fullness", meta = (ClampMin = 0.1))
	float SteerAtFullGroup = 1.5f;

	/** Seconds a key takes to reach full deflection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Input", meta = (ClampMin = 0, Units = "s"))
	float KeyboardRampTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Input", meta = (ClampMin = 0, ClampMax = 0.9))
	float DeadZone = 0.1f;

	/** Per axis, after the dead zone: 1 is linear, higher makes small moves gentler */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Input", meta = (ClampMin = 1))
	float ResponseExponent = 1.6f;

	/** Stick up dives, stick down climbs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Input")
	bool bInvertPitch = false;

	/** Force of the input while steering, roughly rad/s of turning */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Steering", meta = (ClampMin = 0))
	float SteerStrength = 3.f;

	/** How far the desired direction turns sideways at full stick */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Steering", meta = (ClampMin = 0, ClampMax = 90, Units = "deg"))
	float MaxYawAngle = 60.f;

	/** How far the desired direction tips up or down at full stick */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Steering", meta = (ClampMin = 0, ClampMax = 90, Units = "deg"))
	float MaxPitchAngle = 60.f;

	/** The desired direction never points steeper than this */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Steering", meta = (ClampMin = 0, ClampMax = 89, Units = "deg"))
	float MaxClimbAngle = 80.f;

	/** Letting go of up/down eases the nose back to horizontal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Auto Level")
	bool bAutoLevel = true;

	/** Seconds after releasing up/down before leveling starts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Auto Level", meta = (ClampMin = 0, Units = "s"))
	float AutoLevelDelay = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Auto Level", meta = (ClampMin = 0.1, Units = "s"))
	float AutoLevelTime = 1.5f;

	/** Leveling fades out when the nose is within this angle of horizontal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Auto Level", meta = (ClampMin = 0.1, Units = "deg"))
	float AutoLevelFadeAngle = 5.f;

	/** Look stick: degrees per second at full deflection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Look", meta = (ClampMin = 0, Units = "deg/s"))
	float LookStickSpeed = 90.f;

	/** Mouse look: degrees per mouse count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Look", meta = (ClampMin = 0))
	float MouseLookSensitivity = 0.15f;

	/** The look stick counts as untouched below this deflection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Look", meta = (ClampMin = 0, ClampMax = 0.9))
	float LookDeadZone = 0.05f;

	/** Look up by moving the stick or mouse down */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Look")
	bool bInvertLookPitch = false;


	/** Speed during a dash, as a multiple of cruise speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Dash", meta = (ClampMin = 1))
	float DashSpeedMultiplier = 1.8f;

	/** Dash length in seconds with the player alone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Dash", meta = (ClampMin = 0, Units = "s"))
	float DashDuration = 0.5f;

	/** Dash length in seconds with a full flock */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Dash", meta = (ClampMin = 0, Units = "s"))
	float DashDurationAtFullGroup = 1.f;

	/** Roughly how many seconds the speed takes to build up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Dash", meta = (ClampMin = 0, Units = "s"))
	float DashAttackTime = 0.15f;

	/** Roughly how many seconds the speed takes to settle back after the dash */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Dash", meta = (ClampMin = 0, Units = "s"))
	float DashFadeOutTime = 1.2f;

	/** Seconds after a dash ends before the next one is allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Dash", meta = (ClampMin = 0, Units = "s"))
	float DashCooldown = 1.5f;


	/** How much diving speeds the group up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Gravity", meta = (ClampMin = 0))
	float GravityStrength = 0.5f;

	/** Diving never speeds the group above this multiple of cruise speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Gravity", meta = (ClampMin = 1))
	float MaxGravitySpeedScale = 1.5f;


	/** Speed ratio (actual / normal speed alone) that counts as 0 in GetSpeedAlpha */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Speed Feel", meta = (ClampMin = 0))
	float SpeedFeelMin = 1.f;

	/** Speed ratio that counts as 1 in GetSpeedAlpha */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight|Speed Feel", meta = (ClampMin = 0))
	float SpeedFeelMax = 2.5f;

	/** Turns the wedge on */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow")
	bool bFollowWedge = true;

	/** Closest place behind you */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow", meta = (ClampMin = 0, Units = "cm"))
	float WedgeMinDepth = 250.f;

	/** How far back the wedge reaches when you are alone, and at full group size */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow", meta = (ClampMin = 0, Units = "cm"))
	float WedgeLengthAlone = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow", meta = (ClampMin = 0, Units = "cm"))
	float WedgeLengthFull = 2000.f;

	/** Half angle of the V */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow", meta = (ClampMin = 0, ClampMax = 80, Units = "deg"))
	float WedgeHalfAngle = 35.f;

	/** Vertical scatter as a fraction of the sideways spread */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow", meta = (ClampMin = 0))
	float WedgeVerticalScale = 0.25f;

	/** How hard a bird steers toward its place */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow", meta = (ClampMin = 0))
	float FollowWeight = 2.f;

	/** Distance from its place where the pull is at full strength */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow", meta = (ClampMin = 1, Units = "cm"))
	float FollowFullForceDistance = 600.f;

	/** A bird far behind its place flies up to this fraction faster, one ahead that much slower */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow", meta = (ClampMin = 0, ClampMax = 1))
	float FollowSpeedAdjust = 0.25f;

	/** Distance behind its place at which a bird gets the full speed adjustment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow", meta = (ClampMin = 1, Units = "cm"))
	float FollowSpeedAdjustDistance = 800.f;

	/** Draws the wedge outline (cyan) behind the bird */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock|Follow")
	bool bShowWedge = true;

	/** The ripple: key 1 / gamepad left bumper */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Moves|Ripple")
	FFlockRippleSettings Ripple;

	/** The tornado: key 2 / gamepad right bumper */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Moves|Tornado")
	FFlockTornadoSettings Tornado;

	/** The loop: key 4 / gamepad right trigger */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Moves|Loop")
	FFlockLoopSettings Loop;

	/** The corkscrew: key 5 / gamepad D-pad up */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Moves|Corkscrew")
	FFlockCorkscrewSettings Corkscrew;

	/** The halo: key 3 / gamepad left trigger */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Moves|Halo")
	FFlockHaloSettings Halo;


	/** Vertical field of view in degrees with the player alone */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 5, ClampMax = 170, Units = "deg"))
	float FieldOfView = 60.f;

	/** Vertical field of view in degrees with a full flock */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 5, ClampMax = 170, Units = "deg"))
	float FieldOfViewAtFullGroup = 72.f;

	/** Degrees the view widens per extra unit of speed from a dash or dive */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 0, Units = "deg"))
	float SpeedKickFovPerExtraSpeed = 15.f;

	/** The speed kick never adds more than this many degrees */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 0, Units = "deg"))
	float SpeedKickMaxFov = 15.f;

	/** Roughly how many seconds the view takes to widen */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 0.01, Units = "s"))
	float SpeedKickInTime = 0.15f;

	/** Roughly how many seconds the view takes to settle back */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 0.01, Units = "s"))
	float SpeedKickOutTime = 0.6f;

	/** How far above straight behind the camera rests with the player alone */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 0, ClampMax = 80, Units = "deg"))
	float RestingPitch = 17.5f;

	/** The same angle with a full flock */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 0, ClampMax = 80, Units = "deg"))
	float RestingPitchAtFullGroup = 25.f;

	/** The camera orbits a point this far ahead of the bird instead of the bird */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 0, ClampMax = 2))
	float LookAheadFraction = 0.f;

	/** How much the camera tips with the flight pitch */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 0, ClampMax = 1))
	float PitchFollowFraction = 0.f;

	/** Flight pitch used for PitchFollowFraction is limited to this angle */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Lens", meta = (ClampMin = 0, ClampMax = 89, Units = "deg"))
	float MaxPitchFollow = 60.f;


	/** Camera distance when the player is alone */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Distance", meta = (ClampMin = 0, Units = "cm"))
	float MinDistance = 1000.f;

	/** Camera distance with a full flock */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Distance", meta = (ClampMin = 0, Units = "cm"))
	float MaxDistance = 2400.f;

	/** Zoom out to fit the player's group around the player */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Distance")
	bool bFitGroup = false;

	/** Fraction of the group that must be in view, counted from the closest bird */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Distance", meta = (ClampMin = 0, ClampMax = 1))
	float GroupFitPercentile = 0.95f;

	/** Extra room around the group when fitting it */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Distance", meta = (ClampMin = 1))
	float GroupFitMargin = 1.25f;

	/** Farthest the camera goes when fitting the group */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Distance", meta = (ClampMin = 0, Units = "cm"))
	float MaxFitDistance = 6000.f;

	/** Smoothing time in seconds when zooming out */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Distance", meta = (ClampMin = 0.01, Units = "s"))
	float ZoomOutSmoothTime = 1.0f;

	/** Smoothing time in seconds when zooming in */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Distance", meta = (ClampMin = 0.01, Units = "s"))
	float ZoomInSmoothTime = 2.5f;


	/** Camera pan toward the group center by group size */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Framing")
	FRuntimeFloatCurve CenterBlendBySize;

	/** Smoothing time in seconds of the pan amount */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Framing", meta = (ClampMin = 0.01, Units = "s"))
	float CenterBlendSmoothTime = 1.5f;

	/** At full pan, fraction of the player-to-center distance the focus moves */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Framing", meta = (ClampMin = 0, ClampMax = 1))
	float PanFraction = 0.35f;

	/** The pan never moves the focus farther than this fraction of the camera distance */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Framing", meta = (ClampMin = 0, ClampMax = 0.5))
	float MaxPanFraction = 0.2f;

	/** The pan can drift this far before the camera reacts */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Framing", meta = (ClampMin = 0, Units = "cm"))
	float CenterDeadRadius = 60.f;

	/** Smoothing time in seconds once the pan is outside the dead radius */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Framing", meta = (ClampMin = 0.01, Units = "s"))
	float CenterSmoothTime = 0.6f;


	/** The stick counts as steering when left/right is beyond this */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Turning", meta = (ClampMin = 0, ClampMax = 1))
	float SteerInputThreshold = 0.1f;

	/** While steering, the camera aims this many degrees into the turn at full stick */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Turning", meta = (ClampMin = 0, ClampMax = 90, Units = "deg"))
	float PredictiveYawAngle = 30.f;

	/** Smoothing time in seconds of the camera turn while steering */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Turning", meta = (ClampMin = 0.01, Units = "s"))
	float SteerFollowTime = 0.6f;

	/** When not steering, the camera ignores the flock's turns until the heading differs by more than this */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Turning", meta = (ClampMin = 0, ClampMax = 90, Units = "deg"))
	float DeadZoneAngle = 15.f;

	/** Once following, the camera keeps going until it is this close to the group's heading */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Turning", meta = (ClampMin = 0, ClampMax = 90, Units = "deg"))
	float SettleAngle = 2.f;

	/** Smoothing time in seconds of the camera turn when not steering */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Turning", meta = (ClampMin = 0.01, Units = "s"))
	float IdleFollowTime = 2.f;

	/** The group heading is smoothed over this time so a one-frame spike cannot swing the camera */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Turning", meta = (ClampMin = 0, Units = "s"))
	float GroupHeadingFilterTime = 0.3f;


	/** Degrees you can look left and right of the resting view */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Look Around", meta = (ClampMin = 0, ClampMax = 180, Units = "deg"))
	float MaxLookYaw = 30.f;

	/** Degrees you can look up and down from the resting view */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Look Around", meta = (ClampMin = 0, ClampMax = 80, Units = "deg"))
	float MaxLookPitch = 30.f;

	/** Seconds without look input before the camera eases back to rest */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Look Around", meta = (ClampMin = 0, Units = "s"))
	float LookReturnDelay = 0.5f;

	/** The pull back fades in over this many seconds after the delay */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Look Around", meta = (ClampMin = 0.01, Units = "s"))
	float LookReturnBlendTime = 1.f;

	/** How fast the look offset shrinks once the pull back is fully on, per second */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera|Look Around", meta = (ClampMin = 0))
	float LookReturnSpeed = 2.f;
};
