// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockDataTypes.h"
#include "FlockBehavior_Core.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UFlockTypeDefinition::UFlockTypeDefinition()
{
	Behaviors.Add(CreateDefaultSubobject<UFlockBehavior_Core>(TEXT("Core")));
}

UFlockPlayerSettings::UFlockPlayerSettings()
{
	// fullness curve: x = group fill (0..1), y = fullness out (0..1)
	{
		FRichCurve& Curve = FullnessCurve.EditorCurveData;

		const FKeyHandle Start = Curve.AddKey(0.f, 0.f);
		const FKeyHandle Middle = Curve.AddKey(0.14f, 0.44f);
		const FKeyHandle End = Curve.AddKey(1.f, 1.f);

		for (const FKeyHandle Handle : { Start, Middle, End })
		{
			Curve.GetKey(Handle).InterpMode = RCIM_Cubic;
			Curve.GetKey(Handle).TangentMode = RCTM_User;
		}
		Curve.GetKey(Start).ArriveTangent = 0.f;
		Curve.GetKey(Start).LeaveTangent = 3.3f;
		Curve.GetKey(Middle).ArriveTangent = 2.2f;
		Curve.GetKey(Middle).LeaveTangent = 0.9f;
		Curve.GetKey(End).ArriveTangent = 0.37f;
		Curve.GetKey(End).LeaveTangent = 0.f;
	}

	// camera pan: x = bird count, y = 0 (frame player) to 1 (frame group center)
	{
		FRichCurve& Curve = CenterBlendBySize.EditorCurveData;
		Curve.AddKey(1.f, 0.f);
		Curve.AddKey(20.f, 1.f);
	}
}

UFlockCallProfile::UFlockCallProfile()
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultBubbleMaterial(TEXT("/Game/Flocking/effects/M_FlockCallBubble.M_FlockCallBubble"));
	if (DefaultBubbleMaterial.Succeeded())
	{
		BubbleMaterial = DefaultBubbleMaterial.Object;
	}
}
