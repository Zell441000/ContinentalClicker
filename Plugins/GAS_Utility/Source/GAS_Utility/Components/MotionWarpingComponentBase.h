// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MotionWarpingComponent.h"
#include "MotionWarpingComponentBase.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNotifyMotionWarpingEventDelegate, UAnimNotifyState_MotionWarping*, NotifyMotionWarping,URootMotionModifier*, Modifier);

/**
 * 
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = Movement, meta=(BlueprintSpawnableComponent))
class GAS_UTILITY_API UMotionWarpingComponentBase : public UMotionWarpingComponent
{
	GENERATED_BODY()

public:
    UMotionWarpingComponentBase(const FObjectInitializer& ObjectInitializer);

	virtual void InitializeComponent() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Get all current warp targets
	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	const TArray<FMotionWarpingTarget>& GetWarpTargets() const { return WarpTargets; }

	// Debug settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Warping|Debug")
	bool bDebugDrawWarpTargets;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Warping|Debug", meta = (EditCondition = "bDebugDrawWarpTargets"))
	float DebugDrawDuration;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Warping|Debug", meta = (EditCondition = "bDebugDrawWarpTargets"))
	float DebugArrowSize;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Warping|Debug", meta = (EditCondition = "bDebugDrawWarpTargets"))
	FColor DebugColor;
	
	UPROPERTY(BlueprintAssignable, Category="MotionWarping")
	FNotifyMotionWarpingEventDelegate OnWarpStart;

	UPROPERTY(BlueprintAssignable, Category="MotionWarping")
	FNotifyMotionWarpingEventDelegate OnWarUpdate;

	UPROPERTY(BlueprintAssignable, Category="MotionWarping")
	FNotifyMotionWarpingEventDelegate OnWarpEnd;
	
	UFUNCTION(BlueprintCallable, Category="MotionWarping")
	void TriggerWarpStart(UAnimNotifyState_MotionWarping* NotifyMotionWarping,URootMotionModifier* Modifier);
	UFUNCTION(BlueprintCallable, Category="MotionWarping")
	void TriggerWarpUpdate(UAnimNotifyState_MotionWarping* NotifyMotionWarping,URootMotionModifier* Modifier);
	UFUNCTION(BlueprintCallable, Category="MotionWarping")
	void TriggerWarpEnd(UAnimNotifyState_MotionWarping* NotifyMotionWarping,URootMotionModifier* Modifier);

	UFUNCTION(BlueprintCallable,Category="RootMotionModifier")
	void RemoveModifierFromTemplate(URootMotionModifier* Modifier);
protected:
	virtual void DrawDebugWarpTargets() const;
};
