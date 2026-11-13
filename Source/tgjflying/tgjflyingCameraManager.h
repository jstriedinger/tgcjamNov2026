// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "tgjflyingCameraManager.generated.h"

/** Camera manager that limits the look pitch */
UCLASS()
class AtgjflyingCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
	
public:
	AtgjflyingCameraManager();
};
