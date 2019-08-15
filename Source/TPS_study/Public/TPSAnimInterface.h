// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TPS_studyCharacter.h"
#include "TPSAnimInterface.generated.h"



// This class does not need to be modified.
UINTERFACE(BlueprintType, Blueprintable)
class UTPSAnimInterface : public UInterface
{
	GENERATED_BODY()
};

class TPS_STUDY_API ITPSAnimInterface
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Weapon")
	void WeaponIndexIsSetTo(const int weaponIndex);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Navigation")
	void NormalizedForwardIsSetTo(const float normForward);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Navigation")
	void NormalizedRightIsSetTo(const float normRight);
};