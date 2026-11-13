// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FlockTypes.h"

/** Which special move is running */
enum class EFlockManeuver : uint8
{
	None,

	/** Circles a point in an upright funnel */
	Tornado,

	/** Circles a point in a vertical wheel */
	Loop,

	/** A wave runs through the V behind the player */
	Ripple,

	/** Circles a line that moves forward, steered by the player */
	Corkscrew,

	/** A spinning tube of birds ahead of the player, which the player flies through */
	Halo,
};

/** Orbit (tornado, loop, corkscrew, halo): every bird circles an axis through a center */
struct FFlockTornadoParams
{
	/** Unit vector the birds circle around */
	FVector3f Axis = FVector3f::ZAxisVector;

	/** Corkscrew and halo: speed of the center along the axis, as a fraction of the birds' speed */
	float ForwardSpeedFraction = 0.f;

	/** Halo: how far ahead of the player the tube center starts (cm) */
	float StartAhead = 0.f;

	/** Halo: the rest of the group flies this many times faster than the player */
	float MemberSpeedScale = 1.f;

	float RadiusBottom = 400.f;
	float RadiusTop = 900.f;

	/** Random spread of each bird's radius, as a fraction of it */
	float RadiusJitter = 0.15f;

	/** Length along the axis */
	float Height = 600.f;

	/** Each bird bobs along the axis by this much (cm) */
	float BobHeight = 60.f;

	/** How hard a bird turns toward its wanted direction */
	float SteerGain = 4.f;

	/** Push back toward the radius and toward the height */
	float RadialWeight = 1.2f;
	float VerticalWeight = 0.8f;

	/** A bird this far (cm) off its radius or height gets the full push */
	float CatchDistance = 800.f;

		bool bTurnRight = true;

	/** Finale (when FinaleRise is above 0): near the end the column tightens and shoots up */
	float FinaleRise = 0.f;
	float FinaleTime = 1.2f;
	float FinaleLeadTime = 0.8f;
	float FinaleRadiusScale = 0.6f;
	float FinaleVerticalWeight = 2.5f;
};

/** A wave runs through the V behind the player, front to back */
struct FFlockRippleParams
{
	/** How far up and down a place moves at the crest */
	float Amplitude = 250.f;

	/** Distance between two crests along the V */
	float Wavelength = 1200.f;

	/** Seconds a crest takes to pass a given place */
	float Period = 2.5f;

	/** The V's pull on the birds is this many times stronger during the ripple */
	float FollowBoost = 2.f;

	/** The player is not in the V: its flight is bent to rise and fall with the same wave */
	float PlayerGain = 4.f;
};

namespace FlockManeuvers
{
	/** How far up (+) or down (-) a place this deep (cm behind the player) is moved */
	inline float RippleOffset(const FFlockRippleParams& P, float Depth, float Time, float Weight)
	{
		return P.Amplitude * Weight * FMath::Sin(6.2831853f * (Time / FMath::Max(P.Period, 0.05f) - Depth / FMath::Max(P.Wavelength, 1.f)));
	}

	/** The bird's height inside the funnel, 0 (bottom) to 1 (top) */
	inline float TornadoHeightFraction(const FVector3f& Slot)
	{
		return FMath::Clamp(Slot.Z * 0.5f + 0.5f, 0.f, 1.f);
	}

	/** The radius this bird circles at: wider higher up, plus its own jitter */
	inline float TornadoRadius(const FFlockTornadoParams& P, const FVector3f& Slot)
	{
		const float Jitter = (Slot.X - 0.5f) * 2.f * P.RadiusJitter;
		return FMath::Lerp(P.RadiusBottom, P.RadiusTop, TornadoHeightFraction(Slot)) * (1.f + Jitter);
	}

	/** The steering force of one bird in an orbit */
	inline FVector3f TornadoSteer(const FFlockTornadoParams& P, const FVector3f& Center, const FFlockAgentView& Agent,
		const FVector3f& Slot, float CurrentSpeed, float Time, float TimeLeft, float AxialSpeed = 0.f)
	{
		// 0 until the finale starts, then rises to 1
		float Finale = 0.f;
		if (P.FinaleRise > 0.f && P.FinaleTime > 0.f)
		{
			Finale = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((P.FinaleTime + P.FinaleLeadTime - TimeLeft) / P.FinaleTime, 0.f, 1.f));
		}

		const FVector3f& Axis = P.Axis;
		const float Radius = TornadoRadius(P, Slot) * FMath::Lerp(1.f, P.FinaleRadiusScale, Finale);

		// wanted and current position along the axis, from the center
		const float TargetAxial = (TornadoHeightFraction(Slot) - 0.5f) * P.Height
			+ P.BobHeight * FMath::Sin(Time * 1.5f + Slot.X * 6.2831853f)
			+ P.FinaleRise * Finale;
		const float AxialWeight = FMath::Lerp(P.VerticalWeight, P.FinaleVerticalWeight, Finale);
		const FVector3f FromCenter = Agent.Pos - Center;
		const float Axial = FVector3f::DotProduct(FromCenter, Axis);

		// from the axis to the bird, without the axial part
		const FVector3f Out = FromCenter - Axis * Axial;
		const float Dist = Out.Size();
		FVector3f RadialDir;
		if (Dist > 1.f)
		{
			RadialDir = Out / Dist;
		}
		else
		{
			// on the axis: any direction across it will do
			RadialDir = FVector3f::CrossProduct(Axis, Agent.Heading).GetSafeNormal(1e-4f, FVector3f::XAxisVector);
		}

				const float Side = P.bTurnRight ? 1.f : -1.f;
		const FVector3f Tangent = FVector3f::CrossProduct(Axis, RadialDir) * Side;

		const float Catch = FMath::Max(P.CatchDistance, 1.f);
		const float RadialError = FMath::Clamp((Dist - Radius) / Catch, -1.f, 1.f);
		const float AxialError = FMath::Clamp((TargetAxial - Axial) / Catch, -1.f, 1.f);

		// part of the speed goes along the axis to keep up with a moving center
		const float AlongAxis = AxialSpeed > 0.f ? FMath::Clamp(AxialSpeed / FMath::Max(CurrentSpeed, 1.f), 0.f, 0.9f) : 0.f;
		const float RoundAxis = FMath::Sqrt(1.f - AlongAxis * AlongAxis);

		const FVector3f Wanted = (Tangent * RoundAxis + Axis * AlongAxis - RadialDir * (RadialError * P.RadialWeight)
			+ Axis * (AxialError * AxialWeight)).GetSafeNormal(1e-4f, Tangent);

		// inward turn a circle needs, not tighter than a 100 cm circle
		const float TurnRate = CurrentSpeed * RoundAxis * RoundAxis / FMath::Max(Dist, 100.f);

		return (Wanted - Agent.Heading) * P.SteerGain - RadialDir * TurnRate;
	}
}
