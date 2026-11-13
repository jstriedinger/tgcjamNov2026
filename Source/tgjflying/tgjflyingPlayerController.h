// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "tgjflyingPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;

/** Adds the input mapping contexts and the mobile controls */
UCLASS(abstract, config="Game")
class TGJFLYING_API AtgjflyingPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:

	AtgjflyingPlayerController();

protected:

	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Mapping contexts added only when not using touch controls */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** Use the touch controls widget even on desktop */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	virtual void BeginPlay() override;

	virtual void SetupInputComponent() override;

	bool ShouldUseTouchControls() const;
};
