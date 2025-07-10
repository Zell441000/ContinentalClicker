// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameplayChangerBase.h"
#include "AttributeSet.h"
#include "GAS_Utility/VariableContainer/VariableContainer.h"
#include "AbilitySystemComponentBase.generated.h"


USTRUCT(BlueprintType)
struct GAS_UTILITY_API FObjectArray
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category="GAS_Utility")
	TArray<TObjectPtr<UObject>> ArrayList;
};

USTRUCT(BlueprintType)
struct GAS_UTILITY_API FTagPriorityArray
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="GAS_Utility")
	TArray<FGameplayTag> TagList;
};

USTRUCT(BlueprintType)
struct GAS_UTILITY_API FTagPriorityArrayContainer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="GAS_Utility")
	TMap<float, FTagPriorityArray> OverridePriorityFListMap;
};

USTRUCT(BlueprintType)
struct GAS_UTILITY_API FAbilitySystemComponentTagAccessor
{
	GENERATED_BODY()
	UPROPERTY()
	TSoftObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;
};

USTRUCT(BlueprintType)
struct FDatatableDamageRow : public FTableRowBase
{
	GENERATED_USTRUCT_BODY()
	FDatatableDamageRow();
	/* Row Name Should Be the Tag not FilterTags */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=DatatableDamageRow)
	FGameplayTagContainer FilterTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=DatatableDamageRow)
	float DamageValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=DatatableDamageRow)
	FGameplayTagContainer PayloadTags;
};

USTRUCT(BlueprintType)
struct FDebug_GAS_Utility_GroupBase
{
	GENERATED_BODY()

public:
	FDebug_GAS_Utility_GroupBase()
	{
		bIsCollapsedByDefault = true;
		GroupName = "Group Name";
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Debug")
	FName GroupName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Debug")
	bool bIsCollapsedByDefault;
};

USTRUCT(BlueprintType)
struct FDebug_GAS_Utility_TagGroup : public FDebug_GAS_Utility_GroupBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Debug")
	FGameplayTagContainer MatchTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Debug")
	FGameplayTagContainer MatchExactlyTags;
};

USTRUCT(BlueprintType)
struct FDebug_GAS_Utility_AttributeGroup : public FDebug_GAS_Utility_GroupBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Debug")
	TArray<FGameplayAttribute> Attributes;
};

class USkeletalMeshComponent;

/**
* Data about montages that were played locally (all montages in case of server. predictive montages in case of client). Never replicated directly.
*/
USTRUCT()
struct GAS_UTILITY_API FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot
{
	GENERATED_BODY()
	;

public:
	UPROPERTY()
	USkeletalMeshComponent* Mesh;

	UPROPERTY()
	FName SlotName;

	UPROPERTY()
	bool bReplicateMontage = true;

	UPROPERTY()
	FGameplayAbilityLocalAnimMontage LocalMontageInfo;

	UPROPERTY()
	TSubclassOf<UGameplayAbility> SourceAbility;

	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot() : Mesh(nullptr), SlotName(NAME_None), LocalMontageInfo()
	{
	}

	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot(USkeletalMeshComponent* InMesh)
		: Mesh(InMesh), SlotName(NAME_None), LocalMontageInfo()
	{
	}

	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot(USkeletalMeshComponent* InMesh, FName InSlot)
		: Mesh(InMesh), SlotName(InSlot), LocalMontageInfo()
	{
	}

	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot(USkeletalMeshComponent* InMesh, FName InSlot, FGameplayAbilityLocalAnimMontage& InLocalMontageInfo)
		: Mesh(InMesh), SlotName(InSlot), LocalMontageInfo(InLocalMontageInfo)
	{
	}
};

USTRUCT()
struct GAS_UTILITY_API FGameplayAbilityRepAnimMontageInfoByMeshAndSlot
{
	GENERATED_BODY()

	UPROPERTY()
	USkeletalMeshComponent* Mesh;

	UPROPERTY()
	FString MeshName;

	UPROPERTY()
	AActor* MeshOwner = nullptr;

	UPROPERTY()
	FName SlotName;

	UPROPERTY()
	FGameplayAbilityRepAnimMontage RepMontageInfo;

	UPROPERTY()
	TSubclassOf<UGameplayAbility> SourceAbility;

	FGameplayAbilityRepAnimMontageInfoByMeshAndSlot()
		: Mesh(nullptr), SlotName(NAME_None), RepMontageInfo()
	{
	}

	FGameplayAbilityRepAnimMontageInfoByMeshAndSlot(USkeletalMeshComponent* InMesh, FName InSlot)
		: Mesh(InMesh), SlotName(InSlot), RepMontageInfo()
	{
		if (IsValid(Mesh))
		{
			MeshName = Mesh->GetName();
			MeshOwner = Mesh->GetOwner();
		}
	}

	bool operator==(const FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& Other) const
	{
		return Mesh == Other.Mesh && SlotName == Other.SlotName;
	}
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStoredObjectChange, const FGameplayTag, Tag);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNewAbilitySystemComponentDelegateCpp, const UAbilitySystemComponentBase*, AbilitySystemComp);

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnStoredObjectChangeBP, const FGameplayTag, Tag);

DECLARE_DYNAMIC_DELEGATE_OneParam(FNewAbilitySystemComponentDelegate, const UAbilitySystemComponentBase*, AbilitySystemComp);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilitySystemComponentDestoyDelegate, const UAbilitySystemComponentBase*, AbilitySystemComp);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNewTagUpdateDelegate, const FGameplayTag, GameplayTag, bool, TagExist);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilityAddedOrRemoveDelegate, const UGameplayAbility*, GameplayAbility);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTagChange, const FGameplayTag, Tag);


/**
 * 
 */
UCLASS(Blueprintable, BlueprintType, meta=(BlueprintSpawnableComponent))
class GAS_UTILITY_API UAbilitySystemComponentBase : public UAbilitySystemComponent
{
	GENERATED_BODY()

public :
	UAbilitySystemComponentBase(const FObjectInitializer& ObjectInitializer);
	virtual ~UAbilitySystemComponentBase() override;
	virtual void BeginPlay() override;
	virtual void DestroyComponent(bool bPromoteChildren) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
	virtual void NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled) override;
	virtual bool GetShouldTick() const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Deactivate() override;
	virtual void BeginDestroy() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void ClearTimerHandlers();

	FCriticalSection StoreObjectsMutex;
	/* GAMEPLAY TAGS ------------------------------------------------------------- */

	void OnGameplayTagUpdate_ServerReplicate(const FGameplayTag GameplayTag, bool TagExist);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags", BlueprintAuthorityOnly)
	void SetReplicatedGameplayTag(FGameplayTag GameplayTag, int32 Count = 1);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags", BlueprintAuthorityOnly)
	void AddReplicatedGameplayTag(FGameplayTag GameplayTag);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags", BlueprintAuthorityOnly)
	void AddReplicatedGameplayTags(FGameplayTagContainer GameplayTags);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags", BlueprintAuthorityOnly)
	void RemoveReplicatedGameplayTags(FGameplayTagContainer GameplayTags);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags", BlueprintAuthorityOnly)
	void RemoveReplicatedGameplayTag(FGameplayTag GameplayTag);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gameplay Tags")
	bool HasGameplayTag(FGameplayTag GameplayTag);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags")
	void AddGameplayTag(FGameplayTag GameplayTag, int32 Count = 1);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags")
	void AddGameplayTags(const FGameplayTagContainer& GameplayTags, int32 Count = 1);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags")
	void RemoveGameplayTag(FGameplayTag GameplayTag, int32 Count = 1);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags")
	void RemoveGameplayTags(const FGameplayTagContainer& GameplayTags, int32 Count = 1);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags")
	void SetGameplayTagCount(FGameplayTag GameplayTag, int32 Count = 1);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags")
	void GetOwnedGameplayTagsThatMatch(const FGameplayTag& TagToMatch, FGameplayTagContainer& OutContainer) const;

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags", BlueprintAuthorityOnly)
	void RemoveGameplayTagsByMatch(const FGameplayTag& TagToMatch);

	/* GAMEPLAY DAMAGE  ------------------------------------------------------------- */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gameplay Damage")
	float GetDamageForAbilityAndTag(FGameplayTag AbilityTag, FGameplayTag DamageTag, FGameplayTag DamageQuantityTag, float DefaultDamage, FGameplayTagContainer& PayloadTags);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gameplay Damage")
	bool GetDamageDataForAbilityAndTag(FGameplayTag AbilityTag, FGameplayTag DamageTag, FGameplayTag DamageQuantityTag, FDatatableDamageRow& ResultData, float DefaultDamage);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category= "Gameplay Damage")
	UDataTable* DamageDataTable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category= "Gameplay Damage")
	FGameplayAttribute DamageSendFactorAttribute;

	/* GAMEPLAY CHANGERS  ------------------------------------------------------------- */

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GAS_Utility")
	TArray<TObjectPtr<UGameplayChangerBase>> GameplayChangers;

	UPROPERTY(BlueprintReadOnly, Category="GAS_Utility")
	TArray<FGameplayAttribute> DirtyAttributes;

	UPROPERTY(BlueprintReadOnly, Category="GAS_Utility")
	TArray<FGameplayAttribute> AttributesAlreadyInitialized;

	UFUNCTION(BlueprintCallable, Category="GameplayChanger", meta = (DeterminesOutputType = GameplayChangerClass))
	UGameplayChangerBase* AddGameplayChangerByClass(TSubclassOf<UGameplayChangerBase> GameplayChangerClass);

	UFUNCTION(BlueprintCallable, Category="GameplayChanger")
	UGameplayChangerBase* GetGameplayChangerByClass(TSubclassOf<UGameplayChangerBase> GameplayChangerClass);


	UFUNCTION(BlueprintCallable, Category="GameplayChanger")
	UGameplayChangerBase* GetGameplayChangerByTag(FGameplayTag GameplayChangerTag);

	UFUNCTION(BlueprintCallable, Category="GameplayChanger")
	bool AddGameplayChanger(UGameplayChangerBase* GameplayChanger);

	UFUNCTION(BlueprintCallable, Category="GameplayChanger")
	bool RemoveGameplayChanger(UGameplayChangerBase* GameplayChanger);

	UFUNCTION(BlueprintCallable, Category="GameplayChanger")
	bool RemoveGameplayChangerArray(TArray<UGameplayChangerBase*> GameplayChangerArray);

	UFUNCTION(BlueprintCallable, Category="GameplayChanger")
	bool RemoveGameplayChangerByTag(FGameplayTag GameplayChangerTag);

	UFUNCTION(BlueprintCallable, Category="GameplayChanger")
	bool RemoveGameplayChangerByMatchingTag(FGameplayTag GameplayChangerTag);

	UFUNCTION(BlueprintCallable, Category="Attributes")
	void RefreshAllDirtyAttributes();

	UFUNCTION(BlueprintCallable, Category="Attributes")
	void RefreshAttribute(FGameplayAttribute GameplayAttribute);

	UFUNCTION(BlueprintCallable, Category="Attribute")
	void AddAttributeSet(UAttributeSet* Attributes);

	void SetReplicatedAttributeValue(FGameplayAttribute Attribute, float NewValue, float OldValue);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="GAS_Utility")
	TMap<FGameplayAttribute, FTagPriorityArrayContainer> GameplayChangerOverridePriority;

	UFUNCTION(BlueprintCallable, Category=Attribute)
	UAttributeChanger* CreateLooseAttributeChanger(FGameplayAttribute Attribute, FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category=Attribute)
	UAttributeChanger* CreateLooseAttributeChangerWithData(FGameplayAttribute Attribute, FGameplayTag Tag, TEnumAsByte<EAttributeChanger::Type> Operation = EAttributeChanger::SetValue, float Value = 0.0f, float Priority = 0.0f);

	UFUNCTION(BlueprintCallable, Category=Attribute)
	bool RemoveLooseAttributeChanger(UAttributeChanger* AttributeChanger, bool bRemoveGameplayChangerParent = true);

	/* OBJECTS STORED -------------------------------------------------------------*/

	UFUNCTION(BlueprintPure, BlueprintCallable, Category="Stored Objects")
	UObject* GetFirstObjectStoredWithGameplayTag(const FGameplayTag Tag);

	UFUNCTION(BlueprintPure, BlueprintCallable, Category="Stored Objects")
	TArray<UObject*> GetObjectsStoredWithGameplayTag(const FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category="Stored Objects")
	void AddObjectsStoredWithGameplayTag(const FGameplayTag Tag, TArray<UObject*> Array);

	UFUNCTION(BlueprintCallable, Category="Stored Objects")
	void AddObjectStoredWithGameplayTag(const FGameplayTag Tag, UObject* Object);

	UFUNCTION(BlueprintCallable, Category="Stored Objects")
	void ClearObjectsStoredWithGameplayTag(const FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category="Stored Objects")
	void RemoveObjectsStoredWithGameplayTag(const FGameplayTag Tag, TArray<UObject*> Array);

	UFUNCTION(BlueprintCallable, Category="Stored Objects")
	void RemoveObjectStoredWithGameplayTag(const FGameplayTag Tag, UObject* Object);

	UFUNCTION(BlueprintCallable, Category="Stored Objects")
	void SetObjectStoredWithGameplayTag(const FGameplayTag Tag, UObject* Object);

	UPROPERTY(BlueprintReadWrite, Category="Stored Objects")
	TMap<FGameplayTag, FObjectArray> ObjectsStoredByGameplayTag;

	void TriggerObjectStoredChangeDelegates(const FGameplayTag Tag);

	FOnStoredObjectChange* GetStoredObjectChangeDelegates(FGameplayTag Tag);

	TMap<FGameplayTag, FOnStoredObjectChange> StoredObjectChangeDelegates;

	UFUNCTION(BlueprintCallable, Category="Stored Objects|Delegate")
	void BindObjectStoredChangeDelegate(FOnStoredObjectChangeBP Delegate, FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category="Stored Objects|Delegate")
	void UnbindObjectStoredChangeDelegate(FOnStoredObjectChangeBP Delegate, FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category="Stored Objects|Sort")
	TArray<AActor*> GetActorStoredWithGameplayTagSortByAngle(FVector Direction, FVector Origin, FGameplayTag Tag, FString SceneComponentTag = "");

	UFUNCTION(BlueprintCallable, Category="Stored Objects|Sort")
	TArray<AActor*> GetActorStoredWithGameplayTagSortByDistance(FVector Location, FGameplayTag Tag, FString SceneComponentTag = "");


	/* UTILITIES -------------------------------------------------------------*/

	UFUNCTION(BlueprintPure, Category = "Ability|GameplayAbility")
	UGameplayAbility* GetGameplayAbilityFromClass(TSubclassOf<UGameplayAbility> InAbilityToActivate);

	UFUNCTION(BlueprintPure, Category = "Ability|GameplayAbility")
	UGameplayAbility* GetGameplayAbilityFromTag(const FGameplayTag AbilityTag);

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (BlueprintThreadSafe), Category = "Ability|GameplayAttribute")
	float GetAttribute(FGameplayAttribute Attribute) const;


	UFUNCTION(BlueprintPure, BlueprintCallable, Category = "Ability|Accessor")
	FAbilitySystemComponentTagAccessor GetTagAccessor();


	UFUNCTION(BlueprintPure, BlueprintCallable, meta = (BlueprintThreadSafe), Category = "Ability|Accessor|GameplayTag")
	static bool AccessorHasMatchingTag(FAbilitySystemComponentTagAccessor Accessor, FGameplayTag Tag);

	UFUNCTION(BlueprintPure, BlueprintCallable, meta = (BlueprintThreadSafe), Category = "Ability|Accessor|GameplayTag")
	static float GetGameplayAttributeValueByAccessor(FAbilitySystemComponentTagAccessor Accessor, FGameplayAttribute Attribute);

	UFUNCTION(BlueprintPure, BlueprintCallable, meta = (BlueprintThreadSafe), Category = "Ability|Accessor|GameplayTag")
	static float GetGameplayCooldownByAccessor(FAbilitySystemComponentTagAccessor Accessor, FGameplayTag Tag);

	UFUNCTION(BlueprintPure, BlueprintCallable, meta = (BlueprintThreadSafe), Category = "Ability|Accessor|GameplayTag")
	static UObject* GetObjectStoreWithGameplayTagByAccessor(FAbilitySystemComponentTagAccessor Accessor, FGameplayTag Tag, bool& bIsValid);

	UFUNCTION(BlueprintPure, BlueprintCallable, meta = (BlueprintThreadSafe), Category = "Ability|Accessor|GameplayTag")
	static TArray<UObject*> GetObjectsStoreWithGameplayTagByAccessor(FAbilitySystemComponentTagAccessor Accessor, FGameplayTag Tag);

	UFUNCTION(BlueprintPure, BlueprintCallable, Category="GAS_Utiliy")
	AController* GetAvatarController();

	UFUNCTION(BlueprintPure, BlueprintCallable, Category="GAS_Utiliy")
	AActor* GetAvatar();

	/* Input -------------------------------------------------------------*/

	UFUNCTION(BlueprintPure, BlueprintCallable, Category="InputAction")
	FInputActionInstance GetInputActionInstance(UInputAction* InputAction);

	UFUNCTION(BlueprintPure, BlueprintCallable, Category="InputActionInstance")
	bool GetInputActionInstanceValueAsBool(UInputAction* InputAction);

	UFUNCTION(BlueprintPure, BlueprintCallable, Category="InputActionInstance")
	float GetInputActionInstanceValueAsFloat(UInputAction* InputAction);

	UFUNCTION(BlueprintPure, BlueprintCallable, Category="InputActionInstance")
	FVector2D GetInputActionInstanceValueAsVector2D(UInputAction* InputAction);

	UFUNCTION(BlueprintPure, BlueprintCallable, Category="InputActionInstance")
	FVector GetInputActionInstanceValueAsVector(UInputAction* InputAction);


	/* OVERRIDE -------------------------------------------------------------*/

	virtual AActor* GetGameplayTaskAvatar(const UGameplayTask* Task) const override;

	UFUNCTION(BlueprintPure, Category = Ability)
	static UAbilitySystemComponentBase* GetAbilitySystemComp(const AActor* Actor);

	virtual UGameplayAbility* CreateNewInstanceOfAbility(FGameplayAbilitySpec& Spec, const UGameplayAbility* Ability) override;

	virtual void InitializeComponent() override;

	virtual void OnTagUpdated(const FGameplayTag& Tag, bool TagExists) override;

	virtual void OnGiveAbility(FGameplayAbilitySpec& AbilitySpec) override;

	virtual void OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec) override;


	/* Cooldown Tag -------------------------------------------------------------*/

	UFUNCTION(BlueprintCallable, Category="CooldownTag")
	void AddCooldownTag(FGameplayTag Tag, float Duration);

	bool IsServer();

	UFUNCTION(BlueprintCallable, Category="CooldownTag")
	void RemoveCooldownTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="CooldownTag")
	float GetCooldownFromTag(FGameplayTag Tag);

	void RemoveCooldownTagAtEnd(const FGameplayEffectRemovalInfo& Info, FGameplayTag Tag);


	UPROPERTY()
	TMap<FGameplayTag, FActiveGameplayEffectHandle> CooldownHandlesByTag;

	/* BUFFER INPUT -------------------------------------------------------------*/

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BufferInput")
	TArray<FGameplayTag> ActionTagPriorityForBufferInput;

	UFUNCTION(BlueprintCallable, Category="BufferInput")
	bool IsActionTagHigherPriorityForBufferInput(FGameplayTag Tag);

	/* Delegates -------------------------------------------------------------*/
	UPROPERTY(BlueprintAssignable, Category="AbilitySystemComponent")
	FAbilitySystemComponentDestoyDelegate OnDestroyed;

	UPROPERTY(BlueprintAssignable, Category="AbilitySystemComponent")
	FNewTagUpdateDelegate OnTagUpdate;

	UFUNCTION()
	void OnTagUpdateEvent(FGameplayTag Tag, bool TagExist);

	UFUNCTION(BlueprintCallable, Category="Gameplay Tag|Delegate")
	void BindTagChangeDelegate(FOnStoredObjectChangeBP Delegate, FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category="Gameplay Tag|Delegate")
	void UnbindTagChangeDelegate(FOnStoredObjectChangeBP Delegate, FGameplayTag Tag);

	FOnStoredObjectChange* GetTagChangeDelegates(FGameplayTag Tag);

	TMap<FGameplayTag, FOnStoredObjectChange> TagChangeDelegates;

	UPROPERTY(BlueprintAssignable, Category="AbilitySystemComponent")
	FAbilityAddedOrRemoveDelegate OnAbilityAdded;

	UPROPERTY(BlueprintAssignable, Category="AbilitySystemComponent")
	FAbilityAddedOrRemoveDelegate OnAbilityRemoved;

	/* Static List of all Ability system Component -------------------------------------------------------------*/

	UFUNCTION(BlueprintCallable, Category="Ability|Delegate")
	static void BindNewAbilitySystemComponentDelegate(FNewAbilitySystemComponentDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category="Ability|Delegate")
	static void UnbindNewAbilitySystemComponentDelegate(FNewAbilitySystemComponentDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category="AbilitySystemComp")
	static TArray<UAbilitySystemComponentBase*> GetArrayAbilitySystemComponents();

	static TArray<TObjectPtr<UAbilitySystemComponentBase>> StaticArrayAbilitySystemComponents;

	static FNewAbilitySystemComponentDelegateCpp NewAbilitySystemComponentDelegate;

	/* Debug -------------------------------------------------------------*/

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Debug")
	TMap<FName, FDebug_GAS_Utility_TagGroup> DebugGameplayTagsGroups;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Debug")
	TMap<FName, FDebug_GAS_Utility_TagGroup> DebugAbilitiesGroups;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Debug")
	TMap<FName, FDebug_GAS_Utility_AttributeGroup> DebugAttributesGroups;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GAS|Debug")
	TArray<FString> DebugAbilityPrefixToRemove;

	// Force Ability

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="GAS|Ability|Behaviour")
	bool IsThereAbilityBehaviourTaskToForce();

	UPROPERTY(BlueprintReadWrite, BlueprintSetter="SetForceExecuteAbilityBehaviourTaskTag", Category="GAS|Ability|Behaviour")
	FGameplayTag ForceExecuteAbilityBehaviourTaskTag;

	UFUNCTION(BlueprintSetter, Category="GAS|Ability|Behaviour")
	void SetForceExecuteAbilityBehaviourTaskTag(const FGameplayTag NewTag);

	UPROPERTY(BlueprintAssignable, Category="GAS|Ability|Behaviour")
	FOnTagChange OnForceExecuteAbilityBehaviourTaskTagChange;


	/* Play Montage -------------------------------------------------------------*/

	virtual float PlayMontage(UGameplayAbility* AnimatingAbility, FGameplayAbilityActivationInfo ActivationInfo, UAnimMontage* Montage, float InPlayRate, FName StartSectionName, float StartTimeSeconds) override;

	UFUNCTION(BlueprintCallable, Category=Montage)
	void SetStopAllMontageWhenPlayNew(const bool bNewStopAllMontageWhenPlayNew);

	UPROPERTY(BlueprintReadOnly, Category=Montage)
	bool bStopAllMontageWhenPlayNew;


	/* Play Multiple Montage On Multiple Meshes ------------------------------*/


	// Plays a montage and handles replication and prediction based on passed in ability/activation info
	virtual float PlayMontageForMesh(UGameplayAbility* AnimatingAbility, class USkeletalMeshComponent* InMesh, FName SlotName, FGameplayAbilityActivationInfo ActivationInfo, UAnimMontage* Montage, float InPlayRate = 1.f, FName StartSectionName = NAME_None, bool bReplicateMontage = true, float StartTimeSeconds = 0.f);

	// Plays a montage without updating replication/prediction structures. Used by simulated proxies when replication tells them to play a montage.
	virtual float PlayMontageSimulatedForMesh(USkeletalMeshComponent* InMesh, FName SlotName, UAnimMontage* Montage, float InPlayRate, FName StartSectionName = NAME_None);

	virtual UAnimMontage* PlaySlotAnimationAsDynamicMontageSimulatedForMesh(UAnimSequenceBase* AnimAsset, USkeletalMeshComponent* InMesh, FName SlotNameMesh, FName SlotName, float BlendInTime, float BlendOutTime, float InPlayRate);

	// Stops whatever montage is currently playing. Expectation is caller should only be stopping it if they are the current animating ability (or have good reason not to check)
	virtual void CurrentMontageStopForMesh(USkeletalMeshComponent* InMesh, FName SlotName, float OverrideBlendOutTime = -1.0f);

	// Stops all montages currently playing
	virtual void StopAllCurrentMontages(float OverrideBlendOutTime = -1.0f);

	// Stops current montage if it's the one given as the Montage param
	virtual void StopMontageIfCurrentForMesh(USkeletalMeshComponent* InMesh, FName SlotName, const UAnimMontage& Montage, float OverrideBlendOutTime = -1.0f);

	// Clear the animating ability that is passed in, if it's still currently animating
	virtual void ClearAnimatingAbilityForAllMeshes(UGameplayAbility* Ability);

	// Jumps current montage to given section. Expectation is caller should only be stopping it if they are the current animating ability (or have good reason not to check)
	virtual void CurrentMontageJumpToSectionForMesh(USkeletalMeshComponent* InMesh, FName SlotName, FName SectionName);

	// Sets current montages next section name. Expectation is caller should only be stopping it if they are the current animating ability (or have good reason not to check)
	virtual void CurrentMontageSetNextSectionNameForMesh(USkeletalMeshComponent* InMesh, FName SlotName, FName FromSectionName, FName ToSectionName);

	// Sets current montage's play rate
	virtual void CurrentMontageSetPlayRateForMesh(USkeletalMeshComponent* InMesh, FName SlotName, float InPlayRate);

	// Returns true if the passed in ability is the current animating ability
	bool IsAnimatingAbilityForAnyMesh(UGameplayAbility* Ability) const;

	// Returns the current animating ability
	UGameplayAbility* GetAnimatingAbilityFromAnyMesh();

	// Returns montages that are currently playing
	TArray<UAnimMontage*> GetCurrentMontages() const;

	// Returns the montage that is playing for the mesh
	TArray<UAnimMontage*> GetCurrentMontageForMesh(USkeletalMeshComponent* InMesh);

	// Returns the montage that is playing for the mesh
	UAnimMontage* GetCurrentMontageForMeshAndSlot(USkeletalMeshComponent* InMesh, FName SlotName);

	// Get SectionID of currently playing AnimMontage
	int32 GetCurrentMontageSectionIDForMesh(USkeletalMeshComponent* InMesh, FName SlotName);

	// Get SectionName of currently playing AnimMontage
	FName GetCurrentMontageSectionNameForMesh(USkeletalMeshComponent* InMesh, FName SlotName);

	// Get length in time of current section
	float GetCurrentMontageSectionLengthForMesh(USkeletalMeshComponent* InMesh, FName SlotName);

	// Returns amount of time left in current section
	float GetCurrentMontageSectionTimeLeftForMesh(USkeletalMeshComponent* InMesh, FName SlotName);


	// Data structure for montages that were instigated locally (everything if server, predictive if client. replicated if simulated proxy)
	// Will be max one element per skeletal mesh on the AvatarActor
	UPROPERTY()
	TArray<FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot> LocalAnimMontageInfoForMeshes;

	// Data structure for replicating montage info to simulated clients
	// Will be max one element per skeletal mesh on the AvatarActor
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedAnimMontageForMesh)
	TArray<FGameplayAbilityRepAnimMontageInfoByMeshAndSlot> RepAnimMontageInfoForMeshes;

	// Finds the existing FGameplayAbilityLocalAnimMontageForMesh for the mesh or creates one if it doesn't exist
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& GetLocalAnimMontageInfoForMesh(USkeletalMeshComponent* InMesh, FName SlotName);
	// Finds the existing FGameplayAbilityRepAnimMontageForMesh for the mesh or creates one if it doesn't exist
	FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& GetGameplayAbilityRepAnimMontageForMesh(USkeletalMeshComponent* InMesh, FName SlotName);

	// Called when a prediction key that played a montage is rejected
	void OnPredictiveMontageRejectedForMesh(USkeletalMeshComponent* InMesh, FName SlotName, UAnimMontage* PredictiveMontage);

	// Copy LocalAnimMontageInfo into RepAnimMontageInfo
	void AnimMontage_UpdateReplicatedDataForMesh(USkeletalMeshComponent* InMesh, FName SlotName);
	void AnimMontage_UpdateReplicatedDataForMesh(FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& OutRepAnimMontageInfo);


	UFUNCTION()
	virtual void OnRep_ReplicatedAnimMontageForMesh();

	// Returns true if we are ready to handle replicated montage information
	virtual bool IsReadyForReplicatedMontageForMesh();

	// RPC function called from CurrentMontageSetNextSectionName, replicates to other clients
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerCurrentMontageSetNextSectionNameForMesh(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, float ClientPosition, FName SlotName, FName SectionName, FName NextSectionName);
	void ServerCurrentMontageSetNextSectionNameForMesh_Implementation(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, float ClientPosition, FName SlotName, FName SectionName, FName NextSectionName);
	bool ServerCurrentMontageSetNextSectionNameForMesh_Validate(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, float ClientPosition, FName SlotName, FName SectionName, FName NextSectionName);

	// RPC function called from CurrentMontageJumpToSection, replicates to other clients
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerCurrentMontageJumpToSectionNameForMesh(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, FName SectionName);
	void ServerCurrentMontageJumpToSectionNameForMesh_Implementation(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, FName SectionName);
	bool ServerCurrentMontageJumpToSectionNameForMesh_Validate(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, FName SectionName);

	// RPC function called from CurrentMontageSetPlayRate, replicates to other clients
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerCurrentMontageSetPlayRateForMesh(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, float InPlayRate);
	void ServerCurrentMontageSetPlayRateForMesh_Implementation(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, float InPlayRate);
	bool ServerCurrentMontageSetPlayRateForMesh_Validate(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, float InPlayRate);


	/* Network  -------------------------------------------------------------*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Network)
	bool bAutoReplicatedAllTags = true;
	
	UPROPERTY( )
	bool bForceMontageReplicationRefresh = false;
	

	UPROPERTY()
	TMap<FGameplayTag, int> IgnoreLocalGameplayEventCount;

	FORCEINLINE void SetReplicatedGameplayTagCount_Override(const FGameplayTag& GameplayTag, int32 NewCount)
	{
		if (!(NewCount == 0 && !GetReplicatedLooseTags_Mutable().TagMap.Find(GameplayTag)))
			GetReplicatedLooseTags_Mutable().SetTagCount(GameplayTag, NewCount);
		if (GetOwnerRole() == ROLE_Authority)
		{
			MulticastRefreshGameplayTagUpdateEvent(GameplayTag, NewCount);
		}
	}

	UFUNCTION(Reliable, NetMulticast, WithValidation, Category = "GameplayEvents")
	void MulticastRefreshGameplayTagUpdateEvent(FGameplayTag EventTag, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "GameplayEvents")
	void SendServerOnlyGameplayEvent(FGameplayTag EventTag, FGameplayEventData Payload);

	UFUNCTION(Reliable, Server, Category = "GameplayEvents")
	void RunOnServerOnly_SendGameplayEvent(FGameplayTag EventTag, FGameplayEventData Payload);

	UFUNCTION(Reliable, Server, Category = "GameplayEvents")
	void RunOnServerOnly_SendGameplayEvent_ToActor(AActor* Actor, FGameplayTag EventTag, FGameplayEventData Payload);

	UFUNCTION(BlueprintCallable, Category = "GameplayEvents", DisplayName=SendServerOnlyGameplayEvent_VariableContainer)
	void SendServerOnlyGameplayEvent_VC(FGameplayTag EventTag, UVariableContainer* Payload);

	UFUNCTION(Reliable, Server, Category = "GameplayEvents")
	void RunOnServerOnly_SendGameplayEvent_VC(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload);

	UFUNCTION(Reliable, Server, Category = "GameplayEvents")
	void RunOnServerOnly_SendGameplayEvent_ToActor_VC(AActor* Actor, FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload);

	UFUNCTION(BlueprintCallable, Category = "GameplayEvents")
	void SendServerGameplayEventMulticast(FGameplayTag EventTag, FGameplayEventData Payload);

	UFUNCTION(Reliable, Server, Category = "GameplayEvents")
	void MulticastOnServer_SendGameplayEvent(FGameplayTag EventTag, FGameplayEventData Payload);

	UFUNCTION(Reliable, Server, Category = "GameplayEvents")
	void MulticastOnServer_SendGameplayEvent_ToActor(AActor* Actor, FGameplayTag EventTag, FGameplayEventData Payload);

	UFUNCTION(Reliable, NetMulticast, Category = "GameplayEvents")
	void MulticastSendGameplayEvent(FGameplayTag EventTag, FGameplayEventData Payload);

	virtual void MulticastSendGameplayEvent_Implementation(FGameplayTag EventTag, FGameplayEventData Payload);

	UFUNCTION(BlueprintCallable, Category = "GameplayEvents")
	void SendServerGameplayEvent_VariableContainer_Multicast(FGameplayTag EventTag, UVariableContainer* Payload);

	UFUNCTION(Reliable, Server, Category = "GameplayEvents")
	void SendServerGameplayEvent_VC_Multicast(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload);

	virtual void SendServerGameplayEvent_VC_Multicast_Implementation(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload);

	UFUNCTION(Reliable, Server, Category = "GameplayEvents")
	void SendServerGameplayEvent_VC_Multicast_OnActor(AActor* Actor, FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload);

	UFUNCTION(Reliable, NetMulticast, Category = "GameplayEvents")
	void MulticastSendGameplayEvent_VariableContainer(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload);

	virtual void MulticastSendGameplayEvent_VariableContainer_Implementation(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload);

	virtual void HandleGameplayEvent_VariableContainer(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload);

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerRequestForceMontageReplication();
	
	UFUNCTION(Reliable, Server, WithValidation)
	void ReceiveServerRequestForceMontageReplicationForAnotherClient(UAbilitySystemComponentBase* TargetASC);

	UFUNCTION()
	void SendServerRequestForceMontageReplication();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastForceMontageReplication(const TArray<FGameplayAbilityRepAnimMontageInfoByMeshAndSlot>& MontageInfos);

};
