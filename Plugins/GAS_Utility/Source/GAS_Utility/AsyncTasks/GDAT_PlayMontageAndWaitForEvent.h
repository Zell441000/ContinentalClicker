// Copyright 2020 Dan Kestranek.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponentBase.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Animation/AnimInstance.h"
#include "GDAT_PlayMontageAndWaitForEvent.generated.h"

class UMyAbilitySystemComponent;

/** Delegate type used, EventTag and Payload may be empty if it came from the montage callbacks */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGDPlayMontageAndWaitForEventDelegate, FGameplayTag, EventTag, FGameplayEventData, EventData);

/**
 * This task combines PlayMontageAndWait and WaitForEvent into one task, so you can wait for multiple types of activations such as from a melee combo
 * Much of this code is copied from one of those two ability tasks
 * This is a good task to look at as an example when creating game-specific tasks
 * It is expected that each game will have a set of game-specific tasks to do what they want
 */
UCLASS()
class  UGDAT_PlayMontageAndWaitForEvent : public UAbilityTask
{
	GENERATED_BODY()
	
public:
	// Constructor and overrides
	UGDAT_PlayMontageAndWaitForEvent(const FObjectInitializer& ObjectInitializer);

	/**
	* The Blueprint node for this task, PlayMontageAndWaitForEvent, has some black magic from the plugin that automagically calls Activate()
	* inside of K2Node_LatentAbilityCall as stated in the AbilityTask.h. Ability logic written in C++ probably needs to call Activate() itself manually.
	*/
	virtual void Activate() override;
	virtual void ExternalCancel() override;
	virtual FString GetDebugString() const override;
	virtual void OnDestroy(bool AbilityEnded) override;

	/** The montage completely finished playing */
	UPROPERTY(BlueprintAssignable)
	FGDPlayMontageAndWaitForEventDelegate OnCompleted;

	/** The montage started blending out */
	UPROPERTY(BlueprintAssignable)
	FGDPlayMontageAndWaitForEventDelegate OnBlendOut;

	/** The montage was interrupted */
	UPROPERTY(BlueprintAssignable)
	FGDPlayMontageAndWaitForEventDelegate OnInterrupted;

	/** The ability task was explicitly cancelled by another ability */
	UPROPERTY(BlueprintAssignable)
	FGDPlayMontageAndWaitForEventDelegate OnCancelled;

	/** One of the triggering gameplay events happened */
	UPROPERTY(BlueprintAssignable)
	FGDPlayMontageAndWaitForEventDelegate EventReceived;

	/**
	 * Plays a montage and waits for it to finish. If a gameplay event occurs that matches EventTags (or if EventTags is empty), 
	 * the EventReceived delegate will be triggered with the event tag and data.
	 * 
	 * If bStopWhenAbilityEnds is true, the montage will be automatically stopped when the ability ends normally. 
	 * It will always be stopped if the ability is explicitly canceled.
	 * 
	 * - OnBlendOut is triggered when the montage starts blending out.
	 * - OnCompleted is triggered when the montage finishes playing.
	 * - OnInterrupted is triggered if another montage overrides this one.
	 * - OnCancelled is triggered if the ability or task is explicitly canceled.
	 *
	 * @param TaskInstanceName Custom name for this task instance, useful for later queries.
	 * @param MontageToPlay The montage to be played on the character.
	 * @param EventTags Gameplay events matching these tags will trigger the EventReceived callback. If empty, all events will trigger it.
	 * @param Mesh The skeletal mesh on which to play the montage (optional).
	 * @param SlotName The animation slot in which to play the montage (optional).
	 * @param Rate Playback speed modifier for the montage.
	 * @param StartTimeSeconds The time (in seconds) at which to start playing the montage.
	 * @param StartSection The section name to start the montage from.
	 * @param bStopWhenAbilityEnds If true, the montage will stop when the ability ends.
	 * @param AnimRootMotionTranslationScale Scale factor for root motion movement. Set to 0 to disable root motion.
	 * @param bReplicateMontage Whether to replicate the montage to clients.
	 * @param OverrideBlendOutTimeForCancelAbility Blend-out duration if the ability is canceled. Use -1 for default.
	 * @param OverrideBlendOutTimeForStopWhenEndAbility Blend-out duration if the ability ends normally. Use -1 for default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE", AdvancedDisplay = "Mesh,SlotName,Rate, StartTimeSeconds, StartSection,bStopWhenAbilityEnds, AnimRootMotionTranslationScale, bReplicateMontage, OverrideBlendOutTimeForCancelAbility, OverrideBlendOutTimeForStopWhenEndAbility"))
	static UGDAT_PlayMontageAndWaitForEvent* PlayMontageAndWaitForEvent(
			UGameplayAbility* OwningAbility,
			FName TaskInstanceName,
			UAnimMontage* MontageToPlay,
			FGameplayTagContainer EventTags,
			USkeletalMeshComponent* Mesh = nullptr,
			FName SlotName = NAME_None,
			float Rate = 1.f,
			float StartTimeSeconds = 0.f,
			FName StartSection = NAME_None,
			bool bStopWhenAbilityEnds = true,
			float AnimRootMotionTranslationScale = 1.f,
			bool bReplicateMontage = true,
			float OverrideBlendOutTimeForCancelAbility = -1.f,
			float OverrideBlendOutTimeForStopWhenEndAbility = -1.f);

private:
	/** Montage that is playing */
	UPROPERTY()
	UAnimMontage* MontageToPlay;

	/** List of tags to match against gameplay events */
	UPROPERTY()
	FGameplayTagContainer EventTags;

	/** Playback rate */
	UPROPERTY()
	float Rate;

	/** Section to start montage from */
	UPROPERTY()
	FName StartSection;

	UPROPERTY()
	USkeletalMeshComponent* Mesh = nullptr;
	
	UPROPERTY()
	FName SlotName = NAME_None;
	
	UPROPERTY()
	float StartTimeSeconds;
	
	/** Modifies how root motion movement to apply */
	UPROPERTY()
	float AnimRootMotionTranslationScale;

	/** Rather montage should be aborted if ability ends */
	UPROPERTY()
	bool bStopWhenAbilityEnds;
	
	UPROPERTY()
	bool bReplicateMontage;

	UPROPERTY()
	float OverrideBlendOutTimeForCancelAbility;

	UPROPERTY()
	float OverrideBlendOutTimeForStopWhenEndAbility;

	/** Checks if the ability is playing a montage and stops that montage, returns true if a montage was stopped, false if not. */
	bool StopPlayingMontage(float OverrideBlendOutTime = -1.f);

	USkeletalMeshComponent* GetMesh() const;

	/** Returns our ability system component */
	UAbilitySystemComponentBase* GetTargetASC();

	void OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);
	void OnAbilityCancelled();
	void OnMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void OnGameplayEvent(FGameplayTag EventTag, const FGameplayEventData* Payload);

	FOnMontageBlendingOutStarted BlendingOutDelegate;
	FOnMontageEnded MontageEndedDelegate;
	FDelegateHandle CancelledHandle;
	FDelegateHandle EventHandle;
	
};
