// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/AbilitySystemComponentBase.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemLog.h"
#include "GameplayAbilityBase.h"
#include "Animation/AnimInstance.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "UnrealEngine.h"
#include "TimerManager.h"

#include "GAS_Utility.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

#include "AbilitySystem/NonReplicatedGameplayEffect.h"

static TAutoConsoleVariable<float> CVarReplayMontageErrorThreshold(
	TEXT("GS.replay.MontageErrorThreshold"),
	0.5f,
	TEXT("Tolerance level for when montage playback position correction occurs in replays")
);
static TAutoConsoleVariable<bool> CVarGasFixClientSideMontageBlendOutTime(
	TEXT("AbilitySystem.Fix.ClientSideMontageBlendOutTime"),
	true,
	TEXT("Enable a fix to replicate the Montage BlendOutTime for (recently) stopped Montages"));
static TAutoConsoleVariable<bool> CVarUpdateMontageSectionIdToPlay(
	TEXT("AbilitySystem.UpdateMontageSectionIdToPlay"),
	true,
	TEXT("During tick, update the section ID that replicated montages should use"));
static TAutoConsoleVariable<bool> CVarReplicateMontageNextSectionId(
	TEXT("AbilitySystem.ReplicateMontageNextSectionId"),
	true,
	TEXT("Apply the replicated next section Id to montages when skipping position replication"));
TArray<TObjectPtr<UAbilitySystemComponentBase>> UAbilitySystemComponentBase::StaticArrayAbilitySystemComponents = TArray<TObjectPtr<UAbilitySystemComponentBase>>();
FNewAbilitySystemComponentDelegateCpp UAbilitySystemComponentBase::NewAbilitySystemComponentDelegate = FNewAbilitySystemComponentDelegateCpp();

FDatatableDamageRow::FDatatableDamageRow(): FTableRowBase(), DamageValue(0.f)
{
}

UAbilitySystemComponentBase::UAbilitySystemComponentBase(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer), DamageDataTable(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bAutoActivate = true;
	bAutoReplicatedAllTags = true;
	bStopAllMontageWhenPlayNew = true;
}

UAbilitySystemComponentBase::~UAbilitySystemComponentBase()
{
	if (StaticArrayAbilitySystemComponents.Contains(this))
		StaticArrayAbilitySystemComponents.Remove(this);
}

void UAbilitySystemComponentBase::BeginPlay()
{
	Super::BeginPlay();
	
	StaticArrayAbilitySystemComponents.Add(this);
	NewAbilitySystemComponentDelegate.Broadcast(this);
	OnTagUpdate.AddUniqueDynamic(this, &UAbilitySystemComponentBase::OnTagUpdateEvent);
	//UAbilitySystemGlobals::Get().ReplicateActivationOwnedTags = false;
}

void UAbilitySystemComponentBase::DestroyComponent(bool bPromoteChildren)
{
	ClearTimerHandlers();
	StaticArrayAbilitySystemComponents.Remove(this);
	OnDestroyed.Broadcast(this);
	Super::DestroyComponent(bPromoteChildren);
}

void UAbilitySystemComponentBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTimerHandlers();
	Super::EndPlay(EndPlayReason);
}

void UAbilitySystemComponentBase::Deactivate()
{
	ClearTimerHandlers();
	Super::Deactivate();
}

void UAbilitySystemComponentBase::BeginDestroy()
{
	ClearTimerHandlers();
	Super::BeginDestroy();
}

void UAbilitySystemComponentBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAbilitySystemComponentBase, RepAnimMontageInfoForMeshes);

	/*FProperty* ReplicatedProperty = GetReplicatedProperty(StaticClass(), UAbilitySystemComponentBase::StaticClass(), GET_MEMBER_NAME_CHECKED(UAbilitySystemComponentBase, ActivatableAbilities));
	const FString ReplicatedPropertyName = ReplicatedProperty->GetName();
	NetworkingPrivate::FRepPropertyDescriptor PropDesc(*ReplicatedPropertyName, ReplicatedProperty->RepIndex, ReplicatedProperty->ArrayDim);
	for (int32 i = 0; i < PropDesc.ArrayDim; i++)
	{
		const uint16 RepIndex = PropDesc.RepIndex + i;
		FLifetimeProperty* RegisteredPropertyPtr = OutLifetimeProps.FindByPredicate([&RepIndex](const FLifetimeProperty& Var) { return Var.RepIndex == RepIndex; });
		if (RegisteredPropertyPtr)
		{
			RegisteredPropertyPtr->Condition = COND_None;
		}
	}*/
	//DOREPLIFETIME_CONDITION(UAbilitySystemComponentBase, ActivatableAbilities, COND_None);
}

void UAbilitySystemComponentBase::ClearTimerHandlers()
{
	if (!GetOuter()) return;
	auto WorldRef = GetWorld();
	if (!WorldRef)
		WorldRef = GEngine->GetWorld();
	if (!WorldRef)
		return;
	FTimerManager& TimerManager = WorldRef->GetTimerManager();

	for (auto Element : CooldownHandlesByTag)
	{
		FActiveGameplayEffect* GameplayEffect = ActiveGameplayEffects.GetActiveGameplayEffect(Element.Value);
		if (GameplayEffect)
		{
			if (GameplayEffect->PeriodHandle.IsValid())
			{
				TimerManager.ClearTimer(GameplayEffect->PeriodHandle);
				GameplayEffect->PeriodHandle.Invalidate();
			}

			if (GameplayEffect->DurationHandle.IsValid())
			{
				TimerManager.ClearTimer(GameplayEffect->DurationHandle);
				GameplayEffect->DurationHandle.Invalidate();
			}
		}
		Element.Value.RemoveFromGlobalMap();
		Element.Value.Invalidate();
		ActiveGameplayEffects.RemoveActiveGameplayEffect(Element.Value, 1);
	}
	CooldownHandlesByTag.Empty();
}

void UAbilitySystemComponentBase::OnGameplayTagUpdate_ServerReplicate(const FGameplayTag GameplayTag, bool TagExist)
{
	if (GetOwnerRole() == ROLE_Authority && bAutoReplicatedAllTags)
		SetReplicatedGameplayTagCount_Override(GameplayTag, GetGameplayTagCount(GameplayTag));
}

void UAbilitySystemComponentBase::SetReplicatedGameplayTag(FGameplayTag GameplayTag, int32 Count)
{
	if (GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_ListenServer)
	{
		SetGameplayTagCount(GameplayTag, Count);
		SetReplicatedGameplayTagCount_Override(GameplayTag, Count);
	}
}

void UAbilitySystemComponentBase::AddReplicatedGameplayTag(FGameplayTag GameplayTag)
{
	if (GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_ListenServer)
	{
		AddGameplayTag(GameplayTag);
		SetReplicatedGameplayTagCount_Override(GameplayTag, GetTagCount(GameplayTag));
	}
}

void UAbilitySystemComponentBase::AddReplicatedGameplayTags(FGameplayTagContainer GameplayTags)
{
	if (GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_ListenServer)
	{
		AddGameplayTags(GameplayTags);
		AddReplicatedLooseGameplayTags(GameplayTags);
	}
}

void UAbilitySystemComponentBase::RemoveReplicatedGameplayTags(FGameplayTagContainer GameplayTags)
{
	if (GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_ListenServer)
	{
		RemoveGameplayTags(GameplayTags);
		RemoveReplicatedLooseGameplayTags(GameplayTags);
	}
}

void UAbilitySystemComponentBase::RemoveReplicatedGameplayTag(FGameplayTag GameplayTag)
{
	if (GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_ListenServer)
	{
		RemoveGameplayTag(GameplayTag);
		SetReplicatedGameplayTagCount_Override(GameplayTag, GetTagCount(GameplayTag));
	}
}

void UAbilitySystemComponentBase::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	auto GameplayChangersClone = GameplayChangers;
	for (auto GameplayChanger : GameplayChangersClone)
	{
		if (IsValid(GameplayChanger))
		{
			if (GameplayChanger->bTickEnable)
			{
				GameplayChanger->TickGameplayChanger(DeltaTime);
			}
		}
	}
	bool bNeedReplicationRefreshForMontages = false;
	for (FGameplayAbilityRepAnimMontageInfoByMeshAndSlot RepMontageInfo : RepAnimMontageInfoForMeshes)
	{
		const bool bHasReplicatedMontageInfoToUpdate = (IsOwnerActorAuthoritative() && RepMontageInfo.RepMontageInfo.IsStopped == false);

		if (bHasReplicatedMontageInfoToUpdate)
		{
			bNeedReplicationRefreshForMontages = true;
		}
	}
	if (bNeedReplicationRefreshForMontages)
	{
		if (IsOwnerActorAuthoritative())
		{
			for (FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& MontageInfo : LocalAnimMontageInfoForMeshes)
			{
				if (MontageInfo.bReplicateMontage)
					AnimMontage_UpdateReplicatedDataForMesh(MontageInfo.Mesh, MontageInfo.SlotName);
			}
		}
	}
}

void UAbilitySystemComponentBase::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
	LocalAnimMontageInfoForMeshes = TArray<FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot>();
	RepAnimMontageInfoForMeshes = TArray<FGameplayAbilityRepAnimMontageInfoByMeshAndSlot>();

	if (bPendingMontageRep)
	{
		OnRep_ReplicatedAnimMontageForMesh();
	}
}

void UAbilitySystemComponentBase::NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled)
{
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);
	// If AnimatingAbility ended, clear the pointer
	ClearAnimatingAbilityForAllMeshes(Ability);
}

bool UAbilitySystemComponentBase::GetShouldTick() const
{
	return true;
}

bool UAbilitySystemComponentBase::HasGameplayTag(FGameplayTag GameplayTag)
{
	return HasMatchingGameplayTag(GameplayTag);
}

void UAbilitySystemComponentBase::AddGameplayTag(FGameplayTag GameplayTag, int32 Count)
{

	AddLooseGameplayTag(GameplayTag, Count);
	OnTagUpdate.Broadcast(GameplayTag, HasGameplayTag(GameplayTag));
}

void UAbilitySystemComponentBase::AddGameplayTags(const FGameplayTagContainer& GameplayTags, int32 Count)
{
	
	AddLooseGameplayTags(GameplayTags, Count);
	for (const FGameplayTag GameplayTag : GameplayTags)
	{
		OnTagUpdate.Broadcast(GameplayTag, HasGameplayTag(GameplayTag));
	}
}

void UAbilitySystemComponentBase::RemoveGameplayTag(FGameplayTag GameplayTag, int32 Count)
{
	
	RemoveLooseGameplayTag(GameplayTag, Count);
	OnTagUpdate.Broadcast(GameplayTag, HasGameplayTag(GameplayTag));
}

void UAbilitySystemComponentBase::RemoveGameplayTags(const FGameplayTagContainer& GameplayTags, int32 Count)
{
	RemoveLooseGameplayTags(GameplayTags, Count);
	for (const FGameplayTag GameplayTag : GameplayTags)
	{
		OnTagUpdate.Broadcast(GameplayTag, HasGameplayTag(GameplayTag));
	}
}

void UAbilitySystemComponentBase::SetGameplayTagCount(FGameplayTag GameplayTag, int32 Count)
{
	SetLooseGameplayTagCount(GameplayTag, Count);
	OnTagUpdate.Broadcast(GameplayTag, Count != 0);
}

float UAbilitySystemComponentBase::GetDamageForAbilityAndTag(FGameplayTag AbilityTag, FGameplayTag DamageTag, FGameplayTag DamageQuantityTag, float DefaultDamage, FGameplayTagContainer& PayloadTags)
{
	float Damage = DefaultDamage;
	FDatatableDamageRow ResultData;
	if (GetDamageDataForAbilityAndTag(AbilityTag, DamageTag, DamageQuantityTag, ResultData, DefaultDamage))
	{
		Damage = ResultData.DamageValue;
		PayloadTags = ResultData.PayloadTags;
	}
	return Damage;
}

bool UAbilitySystemComponentBase::GetDamageDataForAbilityAndTag(FGameplayTag AbilityTag, FGameplayTag DamageTag, FGameplayTag DamageQuantityTag, FDatatableDamageRow& ResultData, float DefaultDamage)
{
	FDatatableDamageRow* Result = nullptr;
	FDatatableDamageRow* MyRowData = nullptr;
	if (IsValid(DamageDataTable))
	{
		if (DamageQuantityTag.IsValid())
		{
			if (DamageDataTable->GetRowMap().Contains(DamageQuantityTag.GetTagName()))
			{
				MyRowData = DamageDataTable->FindRow<FDatatableDamageRow>(DamageQuantityTag.GetTagName(), "");
				if (MyRowData)
				{
					Result = MyRowData;
				}
			}
		}
		if (AbilityTag.IsValid())
		{
			if (DamageDataTable->GetRowMap().Contains(AbilityTag.GetTagName()))
			{
				MyRowData = DamageDataTable->FindRow<FDatatableDamageRow>(AbilityTag.GetTagName(), "");
				if (MyRowData)
				{
					if (MyRowData->FilterTags.IsEmpty() || MyRowData->FilterTags.HasTag(DamageTag) || MyRowData->FilterTags.HasTag(DamageQuantityTag))
						Result = MyRowData;
				}
			}
		}
		if (DamageTag.IsValid())
		{
			if (DamageDataTable->GetRowMap().Contains(DamageTag.GetTagName()))
			{
				MyRowData = DamageDataTable->FindRow<FDatatableDamageRow>(DamageTag.GetTagName(), "");
				if (MyRowData)
				{
					if (MyRowData->FilterTags.IsEmpty() || MyRowData->FilterTags.HasTag(DamageTag) || MyRowData->FilterTags.HasTag(DamageQuantityTag))
						Result = MyRowData;
				}
			}
		}
	}
	if (Result)
	{
		ResultData = *Result;
		const UAttributeSet* const AttributeSetOrNull = GetAttributeSubobject(DamageSendFactorAttribute.GetAttributeSetClass());
		if (AttributeSetOrNull != nullptr)
		{
			ResultData.DamageValue *= DamageSendFactorAttribute.GetNumericValue(AttributeSetOrNull);;
		}
	}
	else
	{
		ResultData = FDatatableDamageRow();
		ResultData.DamageValue = DefaultDamage;
	}
	return Result != nullptr;
}


UGameplayChangerBase* UAbilitySystemComponentBase::AddGameplayChangerByClass(TSubclassOf<UGameplayChangerBase> GameplayChangerClass)
{
	if (IsValid(GameplayChangerClass))
	{
		UGameplayChangerBase* GameplayChanger = NewObject<UGameplayChangerBase>(GetWorld(), GameplayChangerClass);
		if (GameplayChanger)
			AddGameplayChanger(GameplayChanger);

		return GameplayChanger;
	}
	return nullptr;
}

UGameplayChangerBase* UAbilitySystemComponentBase::GetGameplayChangerByClass(TSubclassOf<UGameplayChangerBase> GameplayChangerClass)
{
	for (auto GameplayChanger : GameplayChangers)
	{
		UClass* GameplayChangerClassObj = GameplayChanger->GetClass();

		if (IsValid(GameplayChanger) && (GameplayChangerClassObj == GameplayChangerClass || GameplayChangerClassObj->IsChildOf(GameplayChangerClass)))
		{
			return GameplayChanger;
		}
	}
	return nullptr;
}

UGameplayChangerBase* UAbilitySystemComponentBase::GetGameplayChangerByTag(FGameplayTag GameplayChangerTag)
{
	for (TObjectPtr<UGameplayChangerBase> GameplayChanger : GameplayChangers)
	{
		if (IsValid(GameplayChanger) && GameplayChanger->GameplayChangerTag == GameplayChangerTag)
		{
			return GameplayChanger;
		}
	}
	return nullptr;
}

bool UAbilitySystemComponentBase::AddGameplayChanger(UGameplayChangerBase* GameplayChanger)
{
	if (!GameplayChanger || !IsValid(GameplayChanger) || GameplayChangers.Contains(GameplayChanger))
		return false;

	GameplayChangers.Add(GameplayChanger);
	GameplayChanger->AbilitySystemComponentBase = this;
	GameplayChanger->InitAttributeChangers();
	GameplayChanger->OnAddedToGAS_Comp(this);
	// Loop through all AttributeChangers to mark them as Dirty
	for (auto KeyValue : GameplayChanger->AttributeChangers)
	{
		DirtyAttributes.AddUnique(KeyValue.Key);
	}
	RefreshAllDirtyAttributes();
	return true;
}

bool UAbilitySystemComponentBase::RemoveGameplayChanger(UGameplayChangerBase* GameplayChanger)
{
	if (!GameplayChanger || !IsValid(GameplayChanger) || !GameplayChangers.Contains(GameplayChanger))
		return false;
	for (auto KeyValue : GameplayChanger->AttributeChangers)
	{
		DirtyAttributes.AddUnique(KeyValue.Key);
	}
	GameplayChangers.Remove(GameplayChanger);
	GameplayChanger->OnRemovedToGAS_Comp(this);
	GameplayChanger->AbilitySystemComponentBase = nullptr;
	RefreshAllDirtyAttributes();
	return true;
}

bool UAbilitySystemComponentBase::RemoveGameplayChangerArray(TArray<UGameplayChangerBase*> GameplayChangerArray)
{
	TArray<TObjectPtr<UGameplayChangerBase>> ToDelete;
	for (auto GameplayChanger : GameplayChangerArray)
	{
		if (!GameplayChanger || !IsValid(GameplayChanger) || !GameplayChangers.Contains(GameplayChanger))
			continue;
		for (auto KeyValue : GameplayChanger->AttributeChangers)
		{
			DirtyAttributes.AddUnique(KeyValue.Key);
		}
		ToDelete.AddUnique(GameplayChanger);
		GameplayChanger->OnRemovedToGAS_Comp(this);
		GameplayChanger->AbilitySystemComponentBase = nullptr;
	}

	for (auto GameplayChangerBase : ToDelete)
	{
		GameplayChangers.Remove(GameplayChangerBase);
	}
	RefreshAllDirtyAttributes();
	return ToDelete.Num() != 0;
}

bool UAbilitySystemComponentBase::RemoveGameplayChangerByTag(FGameplayTag GameplayChangerTag)
{
	if (!GameplayChangerTag.IsValid())
		return false;
	for (auto GameplayChanger : GameplayChangers)
	{
		if (IsValid(GameplayChanger))
		{
			if (GameplayChanger->GameplayChangerTag.IsValid() && GameplayChanger->GameplayChangerTag == GameplayChangerTag)
			{
				for (auto KeyValue : GameplayChanger->AttributeChangers)
				{
					DirtyAttributes.AddUnique(KeyValue.Key);
				}
				GameplayChangers.Remove(GameplayChanger);
				GameplayChanger->OnRemovedToGAS_Comp(this);
				GameplayChanger->AbilitySystemComponentBase = nullptr;

				RefreshAllDirtyAttributes();
				return true;
			}
		}
	}
	return false;
}

bool UAbilitySystemComponentBase::RemoveGameplayChangerByMatchingTag(FGameplayTag GameplayChangerTag)
{
	if (!GameplayChangerTag.IsValid())
		return false;
	TArray<TObjectPtr<UGameplayChangerBase>> ToDelete;
	for (auto GameplayChanger : GameplayChangers)
	{
		if (IsValid(GameplayChanger))
		{
			if (GameplayChanger->GameplayChangerTag.IsValid() && GameplayChanger->GameplayChangerTag.MatchesTag(GameplayChangerTag))
			{
				for (auto KeyValue : GameplayChanger->AttributeChangers)
				{
					DirtyAttributes.AddUnique(KeyValue.Key);
				}
				ToDelete.AddUnique(GameplayChanger);
				GameplayChanger->OnRemovedToGAS_Comp(this);
				GameplayChanger->AbilitySystemComponentBase = nullptr;
			}
		}
	}
	for (auto GameplayChangerBase : ToDelete)
	{
		GameplayChangers.Remove(GameplayChangerBase);
	}
	RefreshAllDirtyAttributes();
	return ToDelete.Num() != 0;
}

void UAbilitySystemComponentBase::RefreshAllDirtyAttributes()
{
	//UE_LOG(LogGAS_Utility, Display, TEXT("RefreshAllDirtyAttributes"));
	TArray<FGameplayAttribute> DirtyAttributesClone = DirtyAttributes;
	for (FGameplayAttribute& Attribute : DirtyAttributesClone)
	{
		if (GetOwnerRole() == ROLE_Authority)
			RefreshAttribute(Attribute);
	}
}

struct FSortAttributeOperation
{
	explicit FSortAttributeOperation(TArray<UAttributeChanger*> BaseArray, FTagPriorityArrayContainer* InOverridePriorityArray)
		: MainArray(BaseArray), OverridePriorityArray(InOverridePriorityArray)
	{
	}

	TArray<UAttributeChanger*> MainArray;
	FTagPriorityArrayContainer* OverridePriorityArray;

	bool operator()(UAttributeChanger* A, UAttributeChanger* B) const
	{
		if (!A || !B)
			return false;
		float APriority = A->Priority;
		float BPriority = B->Priority;
		if (OverridePriorityArray)
		{
			if (A->UniqueAttributeChangerTag.IsValid() || B->UniqueAttributeChangerTag.IsValid())
			{
				for (TTuple<float, FTagPriorityArray> KeyValue : OverridePriorityArray->OverridePriorityFListMap)
				{
					FTagPriorityArray List = KeyValue.Get<FTagPriorityArray>();
					const int32 ResultA = List.TagList.Find(A->UniqueAttributeChangerTag);
					if (ResultA >= 0 && A->UniqueAttributeChangerTag.IsValid())
					{
						APriority = KeyValue.Key;
					}
					const int32 ResultB = List.TagList.Find(B->UniqueAttributeChangerTag);
					if (ResultB >= 0 && B->UniqueAttributeChangerTag.IsValid())
					{
						BPriority = KeyValue.Key;
					}
					if (ResultA >= 0 && ResultB >= 0 && A->UniqueAttributeChangerTag.IsValid() && B->UniqueAttributeChangerTag.IsValid())
					{
						return ResultA < ResultB;
					}
				}
			}
		}
		if (APriority == BPriority)
		{
			return MainArray.Find(A) < MainArray.Find(B);
		}
		else
		{
			return APriority < BPriority;
		}
	}
};

void UAbilitySystemComponentBase::RefreshAttribute(FGameplayAttribute GameplayAttribute)
{
	// Set the attribute directly: update the FProperty on the attribute set.

	if (!GameplayAttribute.GetUProperty())
	{
		UE_LOG(LogAbilitySystem, Error, TEXT("Attribute %s not found/valid"), *GameplayAttribute.GetName());
		return;
	}
	FProperty* Property = GameplayAttribute.GetUProperty();
	if (!(Property->GetPropertyFlags() & CPF_RepSkip))
	{
		if (GetOwnerRole() != ROLE_Authority)
		{
			DirtyAttributes.Remove(GameplayAttribute);
			return;
		}
	}
	if (UAttributeSet* AttributeSet = const_cast<UAttributeSet*>(GetAttributeSubobject(GameplayAttribute.GetAttributeSetClass())))
	{
		TArray<UAttributeChanger*> BaseChanger;
		TArray<UAttributeChanger*> ValueChanger;
		for (auto GameplayChanger : GameplayChangers)
		{
			if (IsValid(GameplayChanger))
			{
				if (!GameplayChanger->AttributeChangers.IsEmpty())
					if (const auto SearchChange = GameplayChanger->AttributeChangers.Find(GameplayAttribute))
					{
						for (auto AttributeChanger : SearchChange->ArrayList)
						{
							if (IsValid(AttributeChanger))
							{
								if (AttributeChanger->Operation == EAttributeChanger::Base)
								{
									BaseChanger.Add(AttributeChanger);
								}
								else
								{
									ValueChanger.Add(AttributeChanger);
								}
							}
						}
					}
			}
		}
		if (!BaseChanger.IsEmpty())
		{
			FTagPriorityArrayContainer* TagPriorityArrayContainer = GameplayChangerOverridePriority.Find(GameplayAttribute);
			Algo::Sort(BaseChanger, FSortAttributeOperation(BaseChanger, TagPriorityArrayContainer));
			float OldValue = ActiveGameplayEffects.GetAttributeBaseValue(GameplayAttribute);
			float NewValue = OldValue;
			for (const auto Changer : BaseChanger)
			{
				if (IsValid(Changer))
				{
					NewValue = Changer->ExecuteChangeOperation(NewValue, this);
				}
			}
			if (!AttributesAlreadyInitialized.Contains(GameplayAttribute) || NewValue != OldValue)
			{
				ActiveGameplayEffects.SetAttributeBaseValue(GameplayAttribute, NewValue);
				AttributesAlreadyInitialized.AddUnique(GameplayAttribute);
			}
		}
		float OldValue = GetNumericAttribute(GameplayAttribute);
		float NewValue = GetNumericAttributeBase(GameplayAttribute);
		if (!ValueChanger.IsEmpty() || OldValue != NewValue)
		{
			FTagPriorityArrayContainer* TagPriorityArrayContainer = GameplayChangerOverridePriority.Find(GameplayAttribute);

			Algo::Sort(ValueChanger, FSortAttributeOperation(ValueChanger, TagPriorityArrayContainer));


			float LastCumulativeMultiplierPriority = -999999.99f;

			for (int i = 0; i < ValueChanger.Num(); ++i)
			{
				const auto Changer = ValueChanger[i];
				if (IsValid(Changer))
				{
					if (Changer->Operation == EAttributeChanger::CumulativeMultiplier)
					{
						if (Changer->Priority > LastCumulativeMultiplierPriority)
						{
							float CumulativeMultiplier = 1.0;
							LastCumulativeMultiplierPriority = Changer->Priority;

							for (int y = i; y < ValueChanger.Num(); ++y)
							{
								const auto SearchCumulativeMultiplierChanger = ValueChanger[y];
								if (IsValid(SearchCumulativeMultiplierChanger))
								{
									if (SearchCumulativeMultiplierChanger->Priority != LastCumulativeMultiplierPriority)
										break;
									if (SearchCumulativeMultiplierChanger->Operation == EAttributeChanger::CumulativeMultiplier)
									{
										CumulativeMultiplier += SearchCumulativeMultiplierChanger->GetCumulativeMultiplier();
										NewValue = Changer->ExecuteChangeOperation(NewValue, this);
									}
								}
							}
							NewValue *= CumulativeMultiplier;
						}
						continue;
					}
					NewValue = Changer->ExecuteChangeOperation(NewValue, this);
				}
			}
			if (OldValue != NewValue)
			{
				SetNumericAttribute_Internal(GameplayAttribute, NewValue);
				FOnGameplayAttributeValueChange NewDelegate = GetGameplayAttributeValueChangeDelegate(GameplayAttribute);
				FOnAttributeChangeData CallbackData;
				CallbackData.Attribute = GameplayAttribute;
				CallbackData.NewValue = NewValue;
				CallbackData.OldValue = OldValue;
				NewDelegate.Broadcast(CallbackData);
			}
		}
		DirtyAttributes.Remove(GameplayAttribute);
	}
}

void UAbilitySystemComponentBase::AddAttributeSet(UAttributeSet* Attributes)
{
	AddAttributeSetSubobject<UAttributeSet>(Attributes);
	for (TFieldIterator<FProperty> PropertyIterator(Attributes->GetClass()); PropertyIterator; ++PropertyIterator)
	{
		if (const FProperty* Property = *PropertyIterator)
		{
			const FName AttributeName = FName(Property->GetName());
			FProperty* NProperty = Attributes->GetClass()->FindPropertyByName(AttributeName);
			if (!NProperty)
			{
				continue;
			}
			FGameplayAttribute Attribute = FGameplayAttribute(NProperty);
			if (Attribute.IsValid())
				DirtyAttributes.Add(Attribute);
		}
	}
}

void UAbilitySystemComponentBase::SetReplicatedAttributeValue(FGameplayAttribute Attribute, float NewValue, float OldValue)
{
	if (OldValue != NewValue)
	{
		if (GetAttributeSubobject(Attribute.GetAttributeSetClass()))
			SetNumericAttribute_Internal(Attribute, NewValue);
		FOnGameplayAttributeValueChange NewDelegate = GetGameplayAttributeValueChangeDelegate(Attribute);
		FOnAttributeChangeData CallbackData;
		CallbackData.Attribute = Attribute;
		CallbackData.NewValue = NewValue;
		CallbackData.OldValue = OldValue;
		NewDelegate.Broadcast(CallbackData);
	}
}

UAttributeChanger* UAbilitySystemComponentBase::CreateLooseAttributeChanger(FGameplayAttribute Attribute, FGameplayTag Tag)
{
	return CreateLooseAttributeChangerWithData(Attribute, Tag, EAttributeChanger::SetValue, 0.f, 0.f);
}

UAttributeChanger* UAbilitySystemComponentBase::CreateLooseAttributeChangerWithData(FGameplayAttribute Attribute, FGameplayTag Tag, TEnumAsByte<EAttributeChanger::Type> Operation, float Value, float Priority)
{
	UGameplayChangerBase* TempGameplayChanger = NewObject<UGameplayChangerBase>();
	UAttributeChanger* TempGameplayAttribute = NewObject<UAttributeChanger>();
	TMap<FGameplayAttribute, FAttributeChangerArray> Attributes;
	FAttributeChangerArray TempArray = FAttributeChangerArray();

	TempGameplayAttribute->UniqueAttributeChangerTag = Tag;
	TempGameplayAttribute->SetOperation(Operation);
	TempGameplayAttribute->SetFloatValue(Value);
	TempGameplayAttribute->SetPriorityValue(Priority);

	TempArray.ArrayList.Add(TempGameplayAttribute);
	Attributes.Add(Attribute, TempArray);
	TempGameplayChanger->SetAttributeChangers(Attributes);
	AddGameplayChanger(TempGameplayChanger);

	return TempGameplayAttribute;
}

bool UAbilitySystemComponentBase::RemoveLooseAttributeChanger(UAttributeChanger* AttributeChanger, bool bRemoveGameplayChangerParent)
{
	if (IsValid(AttributeChanger))
	{
		if (AttributeChanger->GameplayChangerParent.IsValid())
		{
			if (bRemoveGameplayChangerParent)
			{
				if (AttributeChanger->GameplayChangerParent->AbilitySystemComponentBase.IsValid())
				{
					AttributeChanger->GameplayChangerParent->AbilitySystemComponentBase->RemoveGameplayChanger(AttributeChanger->GameplayChangerParent.Get());
					return true;
				}
			}
			else
			{
				if (FAttributeChangerArray* Value = AttributeChanger->GameplayChangerParent->AttributeChangers.Find(AttributeChanger->GameplayAttribute))
				{
					Value->ArrayList.Remove(AttributeChanger);
					AttributeChanger->GameplayChangerParent->TriggerRefreshAttribute(AttributeChanger->GameplayAttribute);
					AttributeChanger->GameplayChangerParent = nullptr;
					return true;
				}
				AttributeChanger->GameplayChangerParent = nullptr;
				return false;
			}
		}
	}
	return false;
}

UObject* UAbilitySystemComponentBase::GetFirstObjectStoredWithGameplayTag(const FGameplayTag Tag)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	if (!Tag.IsValid())
		return nullptr;
	if (FObjectArray* Result = ObjectsStoredByGameplayTag.Find(Tag))
	{
		if (Result->ArrayList.Num())
		{
			const TObjectPtr<UObject> Object = Result->ArrayList[0];
			if (Object)
				return Object.Get();
		}
	}
	return nullptr;
}

TArray<UObject*> UAbilitySystemComponentBase::GetObjectsStoredWithGameplayTag(const FGameplayTag Tag)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	TArray<UObject*> OutResult;
	if (!Tag.IsValid())
		return OutResult;
	if (FObjectArray* Result = ObjectsStoredByGameplayTag.Find(Tag))
	{
		for (auto Item : Result->ArrayList)
		{
			if (IsValid(Item))
			{
				OutResult.Add(Item.Get());
			}
		}
	}
	return OutResult;
}

void UAbilitySystemComponentBase::AddObjectsStoredWithGameplayTag(const FGameplayTag Tag, TArray<UObject*> Array)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	if (!Tag.IsValid())
		return;
	FObjectArray* Result = ObjectsStoredByGameplayTag.Find(Tag);
	if (!Result)
	{
		ObjectsStoredByGameplayTag.Add(Tag);
		Result = ObjectsStoredByGameplayTag.Find(Tag);
	}
	if (!Result)
		return;
	for (auto Object : Array)
	{
		if (Object && IsValid(Object))
			Result->ArrayList.Add(Object);
	}
	TriggerObjectStoredChangeDelegates(Tag);
}

void UAbilitySystemComponentBase::TriggerObjectStoredChangeDelegates(const FGameplayTag Tag)
{
	if (StoredObjectChangeDelegates.Contains(Tag))
	{
		FOnStoredObjectChange* Result = GetStoredObjectChangeDelegates(Tag);
		if (Result)
			Result->Broadcast(Tag);
	}
}

FOnStoredObjectChange* UAbilitySystemComponentBase::GetStoredObjectChangeDelegates(FGameplayTag Tag)
{
	return &StoredObjectChangeDelegates.FindOrAdd(Tag);
}

void UAbilitySystemComponentBase::BindObjectStoredChangeDelegate(FOnStoredObjectChangeBP Delegate, FGameplayTag Tag)
{
	if (Tag.IsValid() && Delegate.IsBound())
	{
		auto StoredObjectDelegate = GetStoredObjectChangeDelegates(Tag);
		if (StoredObjectDelegate)
		{
			StoredObjectDelegate->AddUnique(Delegate);
		}
	}
}

void UAbilitySystemComponentBase::UnbindObjectStoredChangeDelegate(FOnStoredObjectChangeBP Delegate, FGameplayTag Tag)
{
	if (Tag.IsValid() && Delegate.IsBound())
	{
		auto StoredObjectDelegate = GetStoredObjectChangeDelegates(Tag);
		if (StoredObjectDelegate)
		{
			StoredObjectDelegate->Remove(Delegate);
		}
	}
}

TArray<AActor*> UAbilitySystemComponentBase::GetActorStoredWithGameplayTagSortByAngle(FVector Direction, FVector Origin, FGameplayTag Tag, FString SceneComponentTag)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	TArray<AActor*> Res = TArray<AActor*>();
	TArray<double> Angles = TArray<double>();
	auto Objects = GetObjectsStoredWithGameplayTag(Tag);
	for (auto Object : Objects)
	{
		if (Res.Contains(Object))
			continue;
		if (Object && IsValid(Object))
		{
			AActor* Actor = Cast<AActor>(Object);
			if (Actor && IsValid(Actor))
			{
				FVector Location = Actor->GetActorLocation();
				if (SceneComponentTag != "")
				{
					TArray<UActorComponent*> Comp = Actor->GetComponentsByTag(USceneComponent::StaticClass(), FName(SceneComponentTag));
					for (const auto ActorComponent : Comp)
					{
						const USceneComponent* SceneComp = Cast<USceneComponent>(ActorComponent);
						if (SceneComp && IsValid(SceneComp))
						{
							Location = SceneComp->GetComponentLocation();
							break;
						}
					}
				}
				const FVector CurrentDirection = (Location - Origin).GetSafeNormal();
				const double Angle = FMath::RadiansToDegrees(FMath::Acos(Direction.CosineAngle2D(CurrentDirection)));

				bool Added = false;

				for (int i = 0; i < Res.Num(); i++)
				{
					if (Angles[i] > Angle)
					{
						Res.Insert(Actor, i);
						Angles.Insert(Angle, i);
						Added = true;
						break;
					}
				}
				if (!Added)
				{
					Res.Add(Actor);
					Angles.Add(Angle);
				}
			}
		}
	}
	return Res;
}

TArray<AActor*> UAbilitySystemComponentBase::GetActorStoredWithGameplayTagSortByDistance(FVector Location, FGameplayTag Tag, FString SceneComponentTag)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	TArray<AActor*> Res = TArray<AActor*>();
	TArray<double> Distances = TArray<double>();
	auto Objects = GetObjectsStoredWithGameplayTag(Tag);
	for (auto Object : Objects)
	{
		if (Res.Contains(Object))
			continue;
		if (Object && IsValid(Object))
		{
			AActor* Actor = Cast<AActor>(Object);
			if (Actor && IsValid(Actor))
			{
				FVector ObjectLocation = Actor->GetActorLocation();
				if (SceneComponentTag != "")
				{
					TArray<UActorComponent*> Comp = Actor->GetComponentsByTag(USceneComponent::StaticClass(), FName(SceneComponentTag));
					for (const auto ActorComponent : Comp)
					{
						const USceneComponent* SceneComp = Cast<USceneComponent>(ActorComponent);
						if (SceneComp && IsValid(SceneComp))
						{
							ObjectLocation = SceneComp->GetComponentLocation();
							break;
						}
					}
				}

				const double Distance = FVector::Distance(Location, ObjectLocation);

				bool Added = false;

				for (int i = 0; i < Res.Num(); i++)
				{
					if (Distances[i] > Distance)
					{
						Res.Insert(Actor, i);
						Distances.Insert(Distance, i);
						Added = true;
						break;
					}
				}
				if (!Added)
				{
					Res.Add(Actor);
					Distances.Add(Distance);
				}
			}
		}
	}
	return Res;
}

void UAbilitySystemComponentBase::AddObjectStoredWithGameplayTag(const FGameplayTag Tag, UObject* Object)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	if (!Tag.IsValid())
		return;
	FObjectArray* Result = ObjectsStoredByGameplayTag.Find(Tag);
	if (!Result)
	{
		ObjectsStoredByGameplayTag.Add(Tag);
		Result = ObjectsStoredByGameplayTag.Find(Tag);
	}
	if (!Result)
		return;
	if (Object && IsValid(Object))
		Result->ArrayList.Add(Object);
	TriggerObjectStoredChangeDelegates(Tag);
}

void UAbilitySystemComponentBase::ClearObjectsStoredWithGameplayTag(const FGameplayTag Tag)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	ObjectsStoredByGameplayTag.Remove(Tag);
	TriggerObjectStoredChangeDelegates(Tag);
}

void UAbilitySystemComponentBase::RemoveObjectsStoredWithGameplayTag(const FGameplayTag Tag, TArray<UObject*> Array)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	if (!Tag.IsValid())
		return;
	FObjectArray* Result = ObjectsStoredByGameplayTag.Find(Tag);
	if (!Result)
		return;

	for (auto Object : Array)
	{
		if (Object && IsValid(Object))
			Result->ArrayList.Remove(Object);
	}
	TriggerObjectStoredChangeDelegates(Tag);
}

void UAbilitySystemComponentBase::RemoveObjectStoredWithGameplayTag(const FGameplayTag Tag, UObject* Object)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	if (!Tag.IsValid())
		return;
	FObjectArray* Result = ObjectsStoredByGameplayTag.Find(Tag);
	if (!Result)
		return;

	if (Object && IsValid(Object))
		Result->ArrayList.Remove(Object);
	TriggerObjectStoredChangeDelegates(Tag);
}

void UAbilitySystemComponentBase::SetObjectStoredWithGameplayTag(const FGameplayTag Tag, UObject* Object)
{
	FScopeLock MyLock(&StoreObjectsMutex);
	if (!Tag.IsValid())
		return;
	FObjectArray* Result = ObjectsStoredByGameplayTag.Find(Tag);
	if (!Result)
	{
		ObjectsStoredByGameplayTag.Add(Tag);
		Result = ObjectsStoredByGameplayTag.Find(Tag);
	}
	if (!Result)
		return;
	if (!Result->ArrayList.IsEmpty())
		Result->ArrayList.RemoveAt(0);
	Result->ArrayList.Insert(Object, 0);
	TriggerObjectStoredChangeDelegates(Tag);
}

UGameplayAbility* UAbilitySystemComponentBase::GetGameplayAbilityFromClass(TSubclassOf<UGameplayAbility> InAbilityToActivate)
{
	if (const auto Spec = FindAbilitySpecFromClass(InAbilityToActivate))
		return Spec->GetPrimaryInstance();
	return nullptr;
}

UGameplayAbility* UAbilitySystemComponentBase::GetGameplayAbilityFromTag(const FGameplayTag AbilityTag)
{
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(AbilityTag))
		{
			return Spec.GetPrimaryInstance();
		}
	}
	return nullptr;
}

float UAbilitySystemComponentBase::GetAttribute(FGameplayAttribute Attribute) const
{
	bool bSuccess = false;
	const float Res = GetGameplayAttributeValue(Attribute, bSuccess);
	if (bSuccess)
		return Res;
	return -1.0f;
}

FAbilitySystemComponentTagAccessor UAbilitySystemComponentBase::GetTagAccessor()
{
	FAbilitySystemComponentTagAccessor Accessor = FAbilitySystemComponentTagAccessor();
	Accessor.AbilitySystemComponent = this;
	return Accessor;
}

bool UAbilitySystemComponentBase::AccessorHasMatchingTag(FAbilitySystemComponentTagAccessor Accessor, FGameplayTag Tag)
{
	if (Accessor.AbilitySystemComponent.IsValid())
	{
		return Accessor.AbilitySystemComponent->HasMatchingGameplayTag(Tag);
	}
	return false;
}

float UAbilitySystemComponentBase::GetGameplayAttributeValueByAccessor(FAbilitySystemComponentTagAccessor Accessor, FGameplayAttribute Attribute)
{
	if (Accessor.AbilitySystemComponent.IsValid())
	{
		bool bSuccess = false;
		const float Res = Accessor.AbilitySystemComponent->GetGameplayAttributeValue(Attribute, bSuccess);
		if (bSuccess)
			return Res;
	}
	return -1.0f;
}

float UAbilitySystemComponentBase::GetGameplayCooldownByAccessor(FAbilitySystemComponentTagAccessor Accessor, FGameplayTag Tag)
{
	if (Accessor.AbilitySystemComponent.IsValid())
	{
		UAbilitySystemComponentBase* CastedComp = Cast<UAbilitySystemComponentBase>(Accessor.AbilitySystemComponent.Get());
		if (CastedComp && IsValid(CastedComp))
			return CastedComp->GetCooldownFromTag(Tag);
	}
	return -1.0f;
}

UObject* UAbilitySystemComponentBase::GetObjectStoreWithGameplayTagByAccessor(FAbilitySystemComponentTagAccessor Accessor, FGameplayTag Tag, bool& bIsValid)
{
	bIsValid = false;
	if (Accessor.AbilitySystemComponent.IsValid())
	{
		UAbilitySystemComponentBase* CastedComp = Cast<UAbilitySystemComponentBase>(Accessor.AbilitySystemComponent.Get());
		if (CastedComp && IsValid(CastedComp))
		{
			UObject* Result = CastedComp->GetFirstObjectStoredWithGameplayTag(Tag);
			bIsValid = IsValid(Result);
			return Result;
		}
	}
	return nullptr;
}

TArray<UObject*> UAbilitySystemComponentBase::GetObjectsStoreWithGameplayTagByAccessor(FAbilitySystemComponentTagAccessor Accessor, FGameplayTag Tag)
{
	if (Accessor.AbilitySystemComponent.IsValid())
	{
		UAbilitySystemComponentBase* CastedComp = Cast<UAbilitySystemComponentBase>(Accessor.AbilitySystemComponent.Get());
		if (CastedComp && IsValid(CastedComp))
			return CastedComp->GetObjectsStoredWithGameplayTag(Tag);
	}
	TArray<UObject*> Res;
	return Res;
}

AController* UAbilitySystemComponentBase::GetAvatarController()
{
	if (AActor* Avatar = GetAvatarActor())
	{
		if (const ACharacter* Character = Cast<ACharacter>(Avatar))
		{
			return Character->GetController();
		}
		if (GetOwnerActor()->GetInstigatorController())
			return GetOwnerActor()->GetInstigatorController();
	}
	if (GetOwnerActor())
	{
		return GetOwnerActor()->GetInstigatorController();
	}
	return nullptr;
}

AActor* UAbilitySystemComponentBase::GetAvatar()
{
	return GetAvatarActor();
}

FInputActionInstance UAbilitySystemComponentBase::GetInputActionInstance(UInputAction* InputAction)
{
	if (AController* Controller = GetAvatarController())
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
		{
			if (const TObjectPtr<UPlayerInput> PlayerInput = PlayerController->PlayerInput)
			{
				if (const UEnhancedPlayerInput* EnhancedPlayerInput = Cast<UEnhancedPlayerInput>(PlayerInput))
				{
					if (const FInputActionInstance* InputActionInstance = EnhancedPlayerInput->FindActionInstanceData(InputAction))
						return *(InputActionInstance);
				}
			}
		}
	}
	return FInputActionInstance();
}

bool UAbilitySystemComponentBase::GetInputActionInstanceValueAsBool(UInputAction* InputAction)
{
	const auto InInputActionInstance = GetInputActionInstance(InputAction);
	return InInputActionInstance.GetValue().Get<bool>();
}

float UAbilitySystemComponentBase::GetInputActionInstanceValueAsFloat(UInputAction* InputAction)
{
	const auto InInputActionInstance = GetInputActionInstance(InputAction);
	return InInputActionInstance.GetValue().Get<float>();
}

FVector2D UAbilitySystemComponentBase::GetInputActionInstanceValueAsVector2D(UInputAction* InputAction)
{
	const auto InInputActionInstance = GetInputActionInstance(InputAction);
	return InInputActionInstance.GetValue().Get<FVector2d>();
}

FVector UAbilitySystemComponentBase::GetInputActionInstanceValueAsVector(UInputAction* InputAction)
{
	const auto InInputActionInstance = GetInputActionInstance(InputAction);
	return InInputActionInstance.GetValue().Get<FVector>();
}

AActor* UAbilitySystemComponentBase::GetGameplayTaskAvatar(const UGameplayTask* Task) const
{
	const auto Owner = GetOwnerActor();
	if (const auto Character = Cast<ACharacter>(Owner))
		return Character;
	if (const auto PlayerState = Cast<APlayerState>(Owner))
	{
		if (IsValid(PlayerState->GetPawn()))
		{
			return Cast<ACharacter>(PlayerState->GetPawn());
		}
	}
	return Super::GetGameplayTaskAvatar(Task);
}

UAbilitySystemComponentBase* UAbilitySystemComponentBase::GetAbilitySystemComp(const AActor* Actor)
{
	if (!IsValid(Actor))
		return nullptr;
	if (const auto AbilitySystemComp = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
	{
		return Cast<UAbilitySystemComponentBase>(AbilitySystemComp);
	}
	return nullptr;
}

UGameplayAbility* UAbilitySystemComponentBase::CreateNewInstanceOfAbility(FGameplayAbilitySpec& Spec, const UGameplayAbility* Ability)
{
	if (!IsValid(Ability))
		return nullptr;
	auto AbilityInstance = Super::CreateNewInstanceOfAbility(Spec, Ability);
	if (auto MyAbilityInstance = Cast<UGameplayAbilityBase>(AbilityInstance))
	{
	}
	return AbilityInstance;
}

void UAbilitySystemComponentBase::InitializeComponent()
{
	Super::InitializeComponent();
}

void UAbilitySystemComponentBase::OnTagUpdated(const FGameplayTag& Tag, bool TagExists)
{
	Super::OnTagUpdated(Tag, TagExists);
	OnTagUpdate.Broadcast(Tag, TagExists);
}

void UAbilitySystemComponentBase::OnGiveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	Super::OnGiveAbility(AbilitySpec);

	if (AbilitySpec.GetPrimaryInstance())
	{
		OnAbilityAdded.Broadcast(AbilitySpec.GetPrimaryInstance());
	}
}

void UAbilitySystemComponentBase::OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	if (AbilitySpec.GetPrimaryInstance())
	{
		OnAbilityRemoved.Broadcast(AbilitySpec.GetPrimaryInstance());
	}
	Super::OnRemoveAbility(AbilitySpec);
}

void UAbilitySystemComponentBase::AddCooldownTag(FGameplayTag Tag, float Duration)
{
	if (IsServer())
	{
		if (Duration <= 0)
			return;
		UGameplayEffect* GameplayEffect = NewObject<UNonReplicatedGameplayEffect>(this, FName(TEXT("CooldownGameplayEffect")));
		GameplayEffect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
		GameplayEffect->DurationMagnitude = FGameplayEffectModifierMagnitude(Duration);


		FGameplayEffectContextHandle ContextHandle = MakeEffectContext();
		FGameplayEffectSpecHandle SpecHandle = FGameplayEffectSpecHandle(new FGameplayEffectSpec(GameplayEffect, ContextHandle));

		SpecHandle.Data->DynamicGrantedTags.AddTag(Tag);
		const FActiveGameplayEffectHandle ActiveGE = ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

		CooldownHandlesByTag.Add(Tag, ActiveGE);
		FOnActiveGameplayEffectRemoved_Info* Delegate = OnGameplayEffectRemoved_InfoDelegate(ActiveGE);
		if (Delegate && this)
		{
			Delegate->AddUObject(this, &UAbilitySystemComponentBase::RemoveCooldownTagAtEnd, Tag);
		}
	}
}

bool UAbilitySystemComponentBase::IsServer()
{
	UWorld* World = GetWorld();
	return World ? (World->GetNetMode() != NM_Client) : false;
}

void UAbilitySystemComponentBase::RemoveCooldownTagAtEnd(const FGameplayEffectRemovalInfo& Info, FGameplayTag Tag)
{
	CooldownHandlesByTag.Remove(Tag);
}

void UAbilitySystemComponentBase::RemoveCooldownTag(FGameplayTag Tag)
{
	auto ActiveHandle = CooldownHandlesByTag.Find(Tag);
	if (ActiveHandle)
	{
		RemoveActiveGameplayEffect(*ActiveHandle, 1);
		CooldownHandlesByTag.Remove(Tag);
	}
}

float UAbilitySystemComponentBase::GetCooldownFromTag(FGameplayTag Tag)
{
	auto ActiveHandle = CooldownHandlesByTag.Find(Tag);
	if (ActiveHandle)
	{
		auto ActiveGameplayEffect = GetActiveGameplayEffect(*ActiveHandle);
		if (ActiveGameplayEffect)
			return ActiveGameplayEffect->GetTimeRemaining(GetWorld()->GetTimeSeconds());
		CooldownHandlesByTag.Remove(Tag);
	}
	return -1.f;
}


bool UAbilitySystemComponentBase::IsActionTagHigherPriorityForBufferInput(const FGameplayTag Tag)
{
	for (const auto ActionTag : ActionTagPriorityForBufferInput)
	{
		if (HasMatchingGameplayTag(ActionTag))
		{
			return (Tag == ActionTag);
		}
	}
	return false;
}

void UAbilitySystemComponentBase::OnTagUpdateEvent(FGameplayTag Tag, bool TagExist)
{
	OnGameplayTagUpdate_ServerReplicate(Tag, TagExist);
	if (TagChangeDelegates.Contains(Tag))
	{
		FOnStoredObjectChange* Result = GetTagChangeDelegates(Tag);
		if (Result)
			Result->Broadcast(Tag);
	}
}

void UAbilitySystemComponentBase::BindTagChangeDelegate(FOnStoredObjectChangeBP Delegate, FGameplayTag Tag)
{
	if (Tag.IsValid() && Delegate.IsBound())
	{
		auto TagDelegate = GetTagChangeDelegates(Tag);
		if (TagDelegate)
		{
			TagDelegate->AddUnique(Delegate);
		}
	}
}

void UAbilitySystemComponentBase::UnbindTagChangeDelegate(FOnStoredObjectChangeBP Delegate, FGameplayTag Tag)
{
	if (Tag.IsValid() && Delegate.IsBound())
	{
		auto TagDelegate = GetTagChangeDelegates(Tag);
		if (TagDelegate)
		{
			TagDelegate->Remove(Delegate);
		}
	}
}

FOnStoredObjectChange* UAbilitySystemComponentBase::GetTagChangeDelegates(FGameplayTag Tag)
{
	return &TagChangeDelegates.FindOrAdd(Tag);
}


void UAbilitySystemComponentBase::BindNewAbilitySystemComponentDelegate(FNewAbilitySystemComponentDelegate Delegate)
{
	if (Delegate.IsBound())
	{
		NewAbilitySystemComponentDelegate.AddUnique(Delegate);
	}
}

void UAbilitySystemComponentBase::UnbindNewAbilitySystemComponentDelegate(FNewAbilitySystemComponentDelegate Delegate)
{
	if (Delegate.IsBound())
	{
		NewAbilitySystemComponentDelegate.Remove(Delegate);
	}
}

TArray<UAbilitySystemComponentBase*> UAbilitySystemComponentBase::GetArrayAbilitySystemComponents()
{
	TArray<UAbilitySystemComponentBase*> Result;
	for (TObjectPtr<UAbilitySystemComponentBase> Comp : StaticArrayAbilitySystemComponents)
	{
		if (Comp)
		{
			Result.Add(Comp.Get());
		}
	}
	return Result;
}

bool UAbilitySystemComponentBase::IsThereAbilityBehaviourTaskToForce()
{
	return ForceExecuteAbilityBehaviourTaskTag.IsValid();
}

void UAbilitySystemComponentBase::SetForceExecuteAbilityBehaviourTaskTag(const FGameplayTag NewTag)
{
	ForceExecuteAbilityBehaviourTaskTag = NewTag;
	OnForceExecuteAbilityBehaviourTaskTagChange.Broadcast(NewTag);
}

float UAbilitySystemComponentBase::PlayMontage(UGameplayAbility* InAnimatingAbility, FGameplayAbilityActivationInfo ActivationInfo, UAnimMontage* NewAnimMontage, float InPlayRate, FName StartSectionName, float StartTimeSeconds)
{
	float Duration = -1.f;

	UAnimInstance* AnimInstance = AbilityActorInfo.IsValid() ? AbilityActorInfo->GetAnimInstance() : nullptr;
	if (AnimInstance && NewAnimMontage)
	{
		Duration = AnimInstance->Montage_Play(NewAnimMontage, InPlayRate, EMontagePlayReturnType::MontageLength, StartTimeSeconds, bStopAllMontageWhenPlayNew);
		if (Duration > 0.f)
		{
			
			if (const UGameplayAbility* RawAnimatingAbility = LocalAnimMontageInfo.AnimatingAbility.Get())
			{
				if (RawAnimatingAbility != InAnimatingAbility)
				{
					// The ability that was previously animating will have already gotten the 'interrupted' callback.
					// It may be a good idea to make this a global policy and 'cancel' the ability.
					//
					// For now, we expect it to end itself when this happens.
				}
			}

			if (NewAnimMontage->HasRootMotion() && AnimInstance->GetOwningActor())
			{
				UE_LOG(LogRootMotion, Log, TEXT("UAbilitySystemComponent::PlayMontage %s, Role: %s")
				       , *GetNameSafe(NewAnimMontage)
				       , *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), AnimInstance->GetOwningActor()->GetLocalRole())
				);
			}

			LocalAnimMontageInfo.AnimMontage = NewAnimMontage;
			LocalAnimMontageInfo.AnimatingAbility = InAnimatingAbility;
			LocalAnimMontageInfo.PlayInstanceId = (LocalAnimMontageInfo.PlayInstanceId < UINT8_MAX ? LocalAnimMontageInfo.PlayInstanceId + 1 : 0);

			if (InAnimatingAbility)
			{
				InAnimatingAbility->SetCurrentMontage(NewAnimMontage);
			}

			// Start at a given Section.
			if (StartSectionName != NAME_None)
			{
				AnimInstance->Montage_JumpToSection(StartSectionName, NewAnimMontage);
			}

			// Replicate for non-owners and for replay recordings
			// The data we set from GetRepAnimMontageInfo_Mutable() is used both by the server to replicate to clients and by clients to record replays.
			// We need to set this data for recording clients because there exists network configurations where an abilities montage data will not replicate to some clients (for example: if the client is an autonomous proxy.)
			if (ShouldRecordMontageReplication())
			{
				FGameplayAbilityRepAnimMontage& MutableRepAnimMontageInfo = GetRepAnimMontageInfo_Mutable();

				// Those are static parameters, they are only set when the montage is played. They are not changed after that.
				MutableRepAnimMontageInfo.Animation = NewAnimMontage;
				MutableRepAnimMontageInfo.PlayInstanceId = (MutableRepAnimMontageInfo.PlayInstanceId < UINT8_MAX ? MutableRepAnimMontageInfo.PlayInstanceId + 1 : 0);

				MutableRepAnimMontageInfo.SectionIdToPlay = 0;
				if (MutableRepAnimMontageInfo.GetAnimMontage() && StartSectionName != NAME_None)
				{
					// we add one so INDEX_NONE can be used in the on rep
					MutableRepAnimMontageInfo.SectionIdToPlay = MutableRepAnimMontageInfo.GetAnimMontage()->GetSectionIndex(StartSectionName) + 1;
				}

				// Update parameters that change during Montage life time.
				AnimMontage_UpdateReplicatedData();
			}
		}
	}

	return Duration;
}

void UAbilitySystemComponentBase::SetStopAllMontageWhenPlayNew(const bool bNewStopAllMontageWhenPlayNew)
{
	bStopAllMontageWhenPlayNew = bNewStopAllMontageWhenPlayNew;
}

float UAbilitySystemComponentBase::PlayMontageForMesh(
	UGameplayAbility* InAnimatingAbility,
	USkeletalMeshComponent* InMesh,
	FName SlotName,
	FGameplayAbilityActivationInfo ActivationInfo,
	UAnimMontage* NewAnimMontage,
	float InPlayRate,
	FName StartSectionName,
	bool bReplicateMontage,
	float StartTimeSeconds)
{
	UGameplayAbilityBase* InAbility = Cast<UGameplayAbilityBase>(InAnimatingAbility);

	float Duration = -1.f;

	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	if (AnimInstance && NewAnimMontage)
	{
		Duration = AnimInstance->Montage_Play(NewAnimMontage, InPlayRate, EMontagePlayReturnType::MontageLength, StartTimeSeconds, bStopAllMontageWhenPlayNew);
		if (Duration > 0.f)
		{

			FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);

			if (AnimMontageInfo.LocalMontageInfo.AnimatingAbility.IsValid() && AnimMontageInfo.LocalMontageInfo.AnimatingAbility != InAnimatingAbility)
			{
				// The ability that was previously animating will have already gotten the 'interrupted' callback.
				// It may be a good idea to make this a global policy and 'cancel' the ability.
				//
				// For now, we expect it to end itself when this happens.
			}
			UAnimSequenceBase* Animation = NewAnimMontage->IsDynamicMontage() ? NewAnimMontage->GetFirstAnimReference() : NewAnimMontage;

			if (NewAnimMontage->HasRootMotion() && AnimInstance->GetOwningActor())
			{
				UE_LOG(LogRootMotion, Log, TEXT("UAbilitySystemComponent::PlayMontage %s, Role: %s")
				       , *GetNameSafe(Animation)
				       , *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), AnimInstance->GetOwningActor()->GetLocalRole())
				);
			}

			AnimMontageInfo.LocalMontageInfo.AnimMontage = NewAnimMontage;
			AnimMontageInfo.Mesh = InMesh;
			AnimMontageInfo.SlotName = SlotName;
			AnimMontageInfo.bReplicateMontage = bReplicateMontage;
			AnimMontageInfo.LocalMontageInfo.AnimatingAbility = InAnimatingAbility;
			AnimMontageInfo.SourceAbility = InAnimatingAbility->GetClass();
			AnimMontageInfo.LocalMontageInfo.PlayInstanceId = (AnimMontageInfo.LocalMontageInfo.PlayInstanceId < UINT8_MAX ? AnimMontageInfo.LocalMontageInfo.PlayInstanceId + 1 : 0);

			if (InAbility)
			{
				InAbility->SetCurrentMontageForMesh(InMesh, SlotName, NewAnimMontage);
			}

			// Start at a given Section.
			if (StartSectionName != NAME_None)
			{
				AnimInstance->Montage_JumpToSection(StartSectionName, NewAnimMontage);
			}

			// Replicate to non owners
			if (ShouldRecordMontageReplication() && bReplicateMontage)
			{
				// Those are static parameters, they are only set when the montage is played. They are not changed after that.
				FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& AbilityRepMontageInfo = GetGameplayAbilityRepAnimMontageForMesh(InMesh, SlotName);
				AbilityRepMontageInfo.RepMontageInfo.Animation = Animation;
				AbilityRepMontageInfo.MeshName = InMesh->GetName();
				AbilityRepMontageInfo.MeshOwner = InMesh->GetOwner();
				AbilityRepMontageInfo.RepMontageInfo.PlayInstanceId = (AbilityRepMontageInfo.RepMontageInfo.PlayInstanceId < UINT8_MAX ? AbilityRepMontageInfo.RepMontageInfo.PlayInstanceId + 1 : 0);
				AbilityRepMontageInfo.SourceAbility = InAnimatingAbility->GetClass();
				AbilityRepMontageInfo.RepMontageInfo.SectionIdToPlay = 0;

				if (AbilityRepMontageInfo.RepMontageInfo.Animation && StartSectionName != NAME_None)
				{
					// we add one so INDEX_NONE can be used in the on rep
					AbilityRepMontageInfo.RepMontageInfo.SectionIdToPlay = NewAnimMontage->GetSectionIndex(StartSectionName) + 1;
				}

				if (NewAnimMontage->IsDynamicMontage())
				{
					check(!NewAnimMontage->SlotAnimTracks.IsEmpty());
					AbilityRepMontageInfo.RepMontageInfo.SlotName = NewAnimMontage->SlotAnimTracks[0].SlotName;
					AbilityRepMontageInfo.RepMontageInfo.BlendOutTime = NewAnimMontage->GetDefaultBlendInTime();
				}

				// Update parameters that change during Montage life time.
				AnimMontage_UpdateReplicatedDataForMesh(InMesh, SlotName);

				// Force net update on our avatar actor
				if (AbilityActorInfo->AvatarActor != nullptr)
				{
					AbilityActorInfo->AvatarActor->ForceNetUpdate();
				}
			}
			else
			{
				// If this prediction key is rejected, we need to end the preview
				FPredictionKey PredictionKey = GetPredictionKeyForNewAction();
				if (PredictionKey.IsValidKey())
				{
					PredictionKey.NewRejectedDelegate().BindUObject(this, &UAbilitySystemComponentBase::OnPredictiveMontageRejectedForMesh, InMesh, SlotName, NewAnimMontage);
				}
			}
		}
	}

	return Duration;
}

UAnimMontage* UAbilitySystemComponentBase::PlaySlotAnimationAsDynamicMontageSimulatedForMesh(UAnimSequenceBase* AnimAsset, USkeletalMeshComponent* InMesh, FName SlotNameMesh, FName SlotName, float BlendInTime, float BlendOutTime, float InPlayRate)
{
	UAnimMontage* DynamicMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(AnimAsset, SlotName, BlendInTime, BlendOutTime, InPlayRate, 1, -1.0f, 0.0f);
	PlayMontageSimulatedForMesh(InMesh, SlotNameMesh, DynamicMontage, InPlayRate, NAME_None);
	return DynamicMontage;
}

float UAbilitySystemComponentBase::PlayMontageSimulatedForMesh(USkeletalMeshComponent* InMesh, FName SlotName, UAnimMontage* NewAnimMontage, float InPlayRate, FName StartSectionName)
{
	float Duration = -1.f;
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	if (AnimInstance && NewAnimMontage)
	{
		Duration = AnimInstance->Montage_Play(NewAnimMontage, InPlayRate);
		if (Duration > 0.f)
		{
			FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);
			AnimMontageInfo.LocalMontageInfo.AnimMontage = NewAnimMontage;
		}
	}

	return Duration;
}

void UAbilitySystemComponentBase::CurrentMontageStopForMesh(USkeletalMeshComponent* InMesh, FName SlotName, float OverrideBlendOutTime)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);
	UAnimMontage* MontageToStop = AnimMontageInfo.LocalMontageInfo.AnimMontage;
	bool bShouldStopMontage = AnimInstance && MontageToStop && !AnimInstance->Montage_GetIsStopped(MontageToStop);
	//UE_LOG(LogTemp, Warning,TEXT("Is server: %s"),IsOwnerActorAuthoritative() ? TEXT("true") : TEXT("false"));
	//UE_LOG(LogTemp, Warning, TEXT("	bShouldStopMontage = : AnimInstance valid ? %s and MontageToStop valid ? %s and bShouldStopMontage is %s"),  AnimInstance ? TEXT("true") : TEXT("false"), MontageToStop ? TEXT("true") : TEXT("false"), bShouldStopMontage ? TEXT("true") : TEXT("false"));
	//UE_LOG(LogTemp, Warning, TEXT("	CurrentMontageStopForMesh: %s and bShouldStopMontage is %s"), *GetNameSafe(MontageToStop), bShouldStopMontage ? TEXT("true") : TEXT("false"));
	if (bShouldStopMontage)
	{
		const float BlendOutTime = (OverrideBlendOutTime >= 0.0f ? OverrideBlendOutTime : MontageToStop->BlendOut.GetBlendTime());

		AnimInstance->Montage_Stop(BlendOutTime, MontageToStop);

		if (IsOwnerActorAuthoritative())
		{
			AnimMontage_UpdateReplicatedDataForMesh(InMesh, SlotName);
		}
	}
}

void UAbilitySystemComponentBase::StopAllCurrentMontages(float OverrideBlendOutTime)
{
	for (FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& GameplayAbilityLocalAnimMontageForMesh : LocalAnimMontageInfoForMeshes)
	{
		CurrentMontageStopForMesh(GameplayAbilityLocalAnimMontageForMesh.Mesh, GameplayAbilityLocalAnimMontageForMesh.SlotName, OverrideBlendOutTime);
	}
}

void UAbilitySystemComponentBase::StopMontageIfCurrentForMesh(USkeletalMeshComponent* InMesh, FName SlotName, const UAnimMontage& Montage, float OverrideBlendOutTime)
{
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);
	if (&Montage == AnimMontageInfo.LocalMontageInfo.AnimMontage)
	{
		CurrentMontageStopForMesh(InMesh, SlotName, OverrideBlendOutTime);
	}
}

void UAbilitySystemComponentBase::ClearAnimatingAbilityForAllMeshes(UGameplayAbility* Ability)
{
	UGameplayAbilityBase* GSAbility = Cast<UGameplayAbilityBase>(Ability);
	for (FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& GameplayAbilityLocalAnimMontageForMesh : LocalAnimMontageInfoForMeshes)
	{
		if (GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimatingAbility == Ability)
		{
			GSAbility->SetCurrentMontageForMesh(GameplayAbilityLocalAnimMontageForMesh.Mesh, GameplayAbilityLocalAnimMontageForMesh.SlotName, nullptr);
			GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimatingAbility = nullptr;
		}
	}
}

void UAbilitySystemComponentBase::CurrentMontageJumpToSectionForMesh(USkeletalMeshComponent* InMesh, FName SlotName, FName SectionName)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);
	if ((SectionName != NAME_None) && AnimInstance && AnimMontageInfo.LocalMontageInfo.AnimMontage)
	{
		AnimInstance->Montage_JumpToSection(SectionName, AnimMontageInfo.LocalMontageInfo.AnimMontage);
		// This data is needed for replication on the server and recording replays on clients.
		// We need to set GetRepAnimMontageInfo_Mutable on replay recording clients because this data is NOT replicated to all clients (for example, it is NOT replicated to autonomous proxy clients.)
		if (ShouldRecordMontageReplication())
		{
			FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& MutableRepAnimMontageInfo = GetGameplayAbilityRepAnimMontageForMesh(InMesh, SlotName);

			MutableRepAnimMontageInfo.RepMontageInfo.SectionIdToPlay = 0;
			if (MutableRepAnimMontageInfo.RepMontageInfo.Animation)
			{
				// Only change SectionIdToPlay if the anim montage's source is a montage. Dynamic montages have no sections.
				if (const UAnimMontage* RepAnimMontage = Cast<UAnimMontage>(MutableRepAnimMontageInfo.RepMontageInfo.Animation))
				{
					// we add one so INDEX_NONE can be used in the on rep
					MutableRepAnimMontageInfo.RepMontageInfo.SectionIdToPlay = RepAnimMontage->GetSectionIndex(SectionName) + 1;
				}
			}

			AnimMontage_UpdateReplicatedDataForMesh(InMesh, SlotName);
		}
		if (!IsOwnerActorAuthoritative())
		{
			UAnimSequenceBase* Animation = AnimMontageInfo.LocalMontageInfo.AnimMontage->IsDynamicMontage() ? AnimMontageInfo.LocalMontageInfo.AnimMontage->GetFirstAnimReference() : AnimMontageInfo.LocalMontageInfo.AnimMontage;
			ServerCurrentMontageJumpToSectionNameForMesh(InMesh, Animation, SlotName, SectionName);
		}
	}
}

void UAbilitySystemComponentBase::CurrentMontageSetNextSectionNameForMesh(USkeletalMeshComponent* InMesh, FName SlotName, FName FromSectionName, FName ToSectionName)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);
	if (AnimMontageInfo.LocalMontageInfo.AnimMontage && AnimInstance)
	{
		// Set Next Section Name.
		AnimInstance->Montage_SetNextSection(FromSectionName, ToSectionName, AnimMontageInfo.LocalMontageInfo.AnimMontage);

		// Update replicated version for Simulated Proxies if we are on the server.
		if (IsOwnerActorAuthoritative())
		{
			AnimMontage_UpdateReplicatedDataForMesh(InMesh, SlotName);
		}
		else
		{
			float CurrentPosition = AnimInstance->Montage_GetPosition(AnimMontageInfo.LocalMontageInfo.AnimMontage);
			UAnimSequenceBase* Animation = AnimMontageInfo.LocalMontageInfo.AnimMontage->IsDynamicMontage() ? AnimMontageInfo.LocalMontageInfo.AnimMontage->GetFirstAnimReference() : AnimMontageInfo.LocalMontageInfo.AnimMontage;
			ServerCurrentMontageSetNextSectionNameForMesh(InMesh, Animation, CurrentPosition, SlotName, FromSectionName, ToSectionName);
		}
	}
}

void UAbilitySystemComponentBase::CurrentMontageSetPlayRateForMesh(USkeletalMeshComponent* InMesh, FName SlotName, float InPlayRate)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);
	if (AnimMontageInfo.LocalMontageInfo.AnimMontage && AnimInstance)
	{
		// Set Play Rate
		AnimInstance->Montage_SetPlayRate(AnimMontageInfo.LocalMontageInfo.AnimMontage, InPlayRate);

		// Update replicated version for Simulated Proxies if we are on the server.
		if (IsOwnerActorAuthoritative())
		{
			AnimMontage_UpdateReplicatedDataForMesh(InMesh, SlotName);
		}
		else
		{
			UAnimSequenceBase* Animation = AnimMontageInfo.LocalMontageInfo.AnimMontage->IsDynamicMontage() ? AnimMontageInfo.LocalMontageInfo.AnimMontage->GetFirstAnimReference() : AnimMontageInfo.LocalMontageInfo.AnimMontage;
			ServerCurrentMontageSetPlayRateForMesh(InMesh, Animation, SlotName, InPlayRate);
		}
	}
}

bool UAbilitySystemComponentBase::IsAnimatingAbilityForAnyMesh(UGameplayAbility* InAbility) const
{
	for (FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot GameplayAbilityLocalAnimMontageForMesh : LocalAnimMontageInfoForMeshes)
	{
		if (GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimatingAbility == InAbility)
		{
			return true;
		}
	}

	return false;
}

UGameplayAbility* UAbilitySystemComponentBase::GetAnimatingAbilityFromAnyMesh()
{
	// Only one ability can be animating for all meshes
	for (FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& GameplayAbilityLocalAnimMontageForMesh : LocalAnimMontageInfoForMeshes)
	{
		if (GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimatingAbility.IsValid())
		{
			return GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimatingAbility.Get();
		}
	}

	return nullptr;
}
void UAbilitySystemComponentBase::GetOwnedGameplayTagsThatMatch(const FGameplayTag& TagToMatch, FGameplayTagContainer& OutContainer) const
{
	FGameplayTagContainer OwnedTags = GetOwnedGameplayTags();

	for (const FGameplayTag& OwnedTag : OwnedTags)
	{
		if (OwnedTag.MatchesTag(TagToMatch))
		{
			OutContainer.AddTag(OwnedTag);
		}
	}
}

void UAbilitySystemComponentBase::RemoveGameplayTagsByMatch(const FGameplayTag& TagToMatch)
{
	if (!TagToMatch.IsValid())
		return;

	FGameplayTagContainer TagsToRemove;
	GetOwnedGameplayTagsThatMatch(TagToMatch, TagsToRemove);

	if (!TagsToRemove.IsEmpty())
	{
		RemoveGameplayTags(TagsToRemove);
	}
}

TArray<UAnimMontage*> UAbilitySystemComponentBase::GetCurrentMontages() const
{
	TArray<UAnimMontage*> Montages;

	for (FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot GameplayAbilityLocalAnimMontageForMesh : LocalAnimMontageInfoForMeshes)
	{
		UAnimInstance* AnimInstance = IsValid(GameplayAbilityLocalAnimMontageForMesh.Mesh)
		                              && GameplayAbilityLocalAnimMontageForMesh.Mesh->GetOwner() == AbilityActorInfo->AvatarActor
			                              ? GameplayAbilityLocalAnimMontageForMesh.Mesh->GetAnimInstance()
			                              : nullptr;

		if (GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimMontage && AnimInstance
			&& AnimInstance->Montage_IsActive(GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimMontage))
		{
			Montages.Add(GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimMontage);
		}
	}

	return Montages;
}

TArray<UAnimMontage*> UAbilitySystemComponentBase::GetCurrentMontageForMesh(USkeletalMeshComponent* InMesh)
{
	TArray<UAnimMontage*> Montages;

	for (FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot GameplayAbilityLocalAnimMontageForMesh : LocalAnimMontageInfoForMeshes)
	{
		if (GameplayAbilityLocalAnimMontageForMesh.Mesh == InMesh)
		{
			UAnimInstance* AnimInstance = IsValid(GameplayAbilityLocalAnimMontageForMesh.Mesh)
			                              && GameplayAbilityLocalAnimMontageForMesh.Mesh->GetOwner() == AbilityActorInfo->AvatarActor
				                              ? GameplayAbilityLocalAnimMontageForMesh.Mesh->GetAnimInstance()
				                              : nullptr;

			if (GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimMontage && AnimInstance
				&& AnimInstance->Montage_IsActive(GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimMontage))
			{
				Montages.Add(GameplayAbilityLocalAnimMontageForMesh.LocalMontageInfo.AnimMontage);
			}
		}
	}

	return Montages;
}

UAnimMontage* UAbilitySystemComponentBase::GetCurrentMontageForMeshAndSlot(USkeletalMeshComponent* InMesh, FName SlotName)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);

	if (AnimMontageInfo.LocalMontageInfo.AnimMontage && AnimInstance
		&& AnimInstance->Montage_IsActive(AnimMontageInfo.LocalMontageInfo.AnimMontage))
	{
		return AnimMontageInfo.LocalMontageInfo.AnimMontage;
	}

	return nullptr;
}

int32 UAbilitySystemComponentBase::GetCurrentMontageSectionIDForMesh(USkeletalMeshComponent* InMesh, FName SlotName)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	UAnimMontage* CurrentAnimMontage = GetCurrentMontageForMeshAndSlot(InMesh, SlotName);

	if (CurrentAnimMontage && AnimInstance)
	{
		float MontagePosition = AnimInstance->Montage_GetPosition(CurrentAnimMontage);
		return CurrentAnimMontage->GetSectionIndexFromPosition(MontagePosition);
	}

	return INDEX_NONE;
}

FName UAbilitySystemComponentBase::GetCurrentMontageSectionNameForMesh(USkeletalMeshComponent* InMesh, FName SlotName)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	UAnimMontage* CurrentAnimMontage = GetCurrentMontageForMeshAndSlot(InMesh, SlotName);

	if (CurrentAnimMontage && AnimInstance)
	{
		float MontagePosition = AnimInstance->Montage_GetPosition(CurrentAnimMontage);
		int32 CurrentSectionID = CurrentAnimMontage->GetSectionIndexFromPosition(MontagePosition);

		return CurrentAnimMontage->GetSectionName(CurrentSectionID);
	}

	return NAME_None;
}

float UAbilitySystemComponentBase::GetCurrentMontageSectionLengthForMesh(USkeletalMeshComponent* InMesh, FName SlotName)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	UAnimMontage* CurrentAnimMontage = GetCurrentMontageForMeshAndSlot(InMesh, SlotName);

	if (CurrentAnimMontage && AnimInstance)
	{
		int32 CurrentSectionID = GetCurrentMontageSectionIDForMesh(InMesh, SlotName);
		if (CurrentSectionID != INDEX_NONE)
		{
			TArray<FCompositeSection>& CompositeSections = CurrentAnimMontage->CompositeSections;

			// If we have another section after us, then take delta between both start times.
			if (CurrentSectionID < (CompositeSections.Num() - 1))
			{
				return (CompositeSections[CurrentSectionID + 1].GetTime() - CompositeSections[CurrentSectionID].GetTime());
			}
			// Otherwise we are the last section, so take delta with Montage total time.
			else
			{
				return (CurrentAnimMontage->GetPlayLength() - CompositeSections[CurrentSectionID].GetTime());
			}
		}

		// if we have no sections, just return total length of Montage.
		return CurrentAnimMontage->GetPlayLength();
	}

	return 0.f;
}

float UAbilitySystemComponentBase::GetCurrentMontageSectionTimeLeftForMesh(USkeletalMeshComponent* InMesh, FName SlotName)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	UAnimMontage* CurrentAnimMontage = GetCurrentMontageForMeshAndSlot(InMesh, SlotName);

	if (CurrentAnimMontage && AnimInstance && AnimInstance->Montage_IsActive(CurrentAnimMontage))
	{
		const float CurrentPosition = AnimInstance->Montage_GetPosition(CurrentAnimMontage);
		return CurrentAnimMontage->GetSectionTimeLeftFromPos(CurrentPosition);
	}

	return -1.f;
}

FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& UAbilitySystemComponentBase::GetLocalAnimMontageInfoForMesh(USkeletalMeshComponent* InMesh, FName SlotName)
{
	for (FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& MontageInfo : LocalAnimMontageInfoForMeshes)
	{
		if (MontageInfo.Mesh == InMesh && MontageInfo.SlotName == SlotName)
		{
			return MontageInfo;
		}
	}

	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot MontageInfo = FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot(InMesh, SlotName);
	LocalAnimMontageInfoForMeshes.Add(MontageInfo);
	return LocalAnimMontageInfoForMeshes.Last();
}

FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& UAbilitySystemComponentBase::GetGameplayAbilityRepAnimMontageForMesh(USkeletalMeshComponent* InMesh, FName SlotName)
{
	for (FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& RepMontageInfo : RepAnimMontageInfoForMeshes)
	{
		if (RepMontageInfo.Mesh == InMesh && RepMontageInfo.SlotName == SlotName)
		{
			return RepMontageInfo;
		}
	}

	FGameplayAbilityRepAnimMontageInfoByMeshAndSlot RepMontageInfo = FGameplayAbilityRepAnimMontageInfoByMeshAndSlot(InMesh, SlotName);
	RepAnimMontageInfoForMeshes.Add(RepMontageInfo);
	return RepAnimMontageInfoForMeshes.Last();
}

void UAbilitySystemComponentBase::OnPredictiveMontageRejectedForMesh(USkeletalMeshComponent* InMesh, FName SlotName, UAnimMontage* PredictiveMontage)
{
	static const float MONTAGE_PREDICTION_REJECT_FADETIME = 0.25f;

	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	if (AnimInstance && PredictiveMontage)
	{
		// If this montage is still playing: kill it
		if (AnimInstance->Montage_IsPlaying(PredictiveMontage))
		{
			AnimInstance->Montage_Stop(MONTAGE_PREDICTION_REJECT_FADETIME, PredictiveMontage);
		}
	}
}

void UAbilitySystemComponentBase::AnimMontage_UpdateReplicatedDataForMesh(USkeletalMeshComponent* InMesh, FName SlotName)
{
	check(IsOwnerActorAuthoritative());

	AnimMontage_UpdateReplicatedDataForMesh(GetGameplayAbilityRepAnimMontageForMesh(InMesh, SlotName));
}

void UAbilitySystemComponentBase::AnimMontage_UpdateReplicatedDataForMesh(FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& OutRepAnimMontageInfo)
{
	UAnimInstance* AnimInstance = IsValid(OutRepAnimMontageInfo.Mesh) && OutRepAnimMontageInfo.Mesh->GetOwner()
	                              == AbilityActorInfo->AvatarActor
		                              ? OutRepAnimMontageInfo.Mesh->GetAnimInstance()
		                              : nullptr;
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(OutRepAnimMontageInfo.Mesh, OutRepAnimMontageInfo.SlotName);

	if (AnimInstance && AnimMontageInfo.LocalMontageInfo.AnimMontage)
	{
		if (OutRepAnimMontageInfo.Mesh == AnimMontageInfo.Mesh && IsValid(OutRepAnimMontageInfo.Mesh))
		{
			OutRepAnimMontageInfo.MeshName = OutRepAnimMontageInfo.Mesh->GetName();
			OutRepAnimMontageInfo.MeshOwner = OutRepAnimMontageInfo.Mesh->GetOwner();
		}
		if (AnimMontageInfo.LocalMontageInfo.AnimMontage->IsDynamicMontage())
		{
			OutRepAnimMontageInfo.RepMontageInfo.Animation = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetFirstAnimReference();
			OutRepAnimMontageInfo.RepMontageInfo.BlendOutTime = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetDefaultBlendOutTime();
		}
		else
		{
			OutRepAnimMontageInfo.RepMontageInfo.Animation = AnimMontageInfo.LocalMontageInfo.AnimMontage;
			OutRepAnimMontageInfo.RepMontageInfo.BlendOutTime = 0.0f;
		}

		// Compressed Flags
		bool bIsStopped = AnimInstance->Montage_GetIsStopped(AnimMontageInfo.LocalMontageInfo.AnimMontage);

		if (!bIsStopped)
		{
			OutRepAnimMontageInfo.RepMontageInfo.PlayRate = AnimInstance->Montage_GetPlayRate(AnimMontageInfo.LocalMontageInfo.AnimMontage);
			OutRepAnimMontageInfo.RepMontageInfo.Position = AnimInstance->Montage_GetPosition(AnimMontageInfo.LocalMontageInfo.AnimMontage);
			OutRepAnimMontageInfo.RepMontageInfo.BlendTime = AnimInstance->Montage_GetBlendTime(AnimMontageInfo.LocalMontageInfo.AnimMontage);
		}

		if (OutRepAnimMontageInfo.RepMontageInfo.IsStopped != bIsStopped)
		{
			// Set this prior to calling UpdateShouldTick, so we start ticking if we are playing a Montage
			OutRepAnimMontageInfo.RepMontageInfo.IsStopped = bIsStopped;
			if (bIsStopped)
			{
				// Use AnyThread because GetValueOnGameThread will fail check() when doing replays
				constexpr bool bForceGameThreadValue = true;
				if (CVarGasFixClientSideMontageBlendOutTime.GetValueOnAnyThread(bForceGameThreadValue))
				{
					// Replicate blend out time. This requires a manual search since Montage_GetBlendTime will fail
					// in GetActiveInstanceForMontage for Montages that are stopped.
					for (const FAnimMontageInstance* MontageInstance : AnimInstance->MontageInstances)
					{
						if (MontageInstance->Montage == LocalAnimMontageInfo.AnimMontage)
						{
							OutRepAnimMontageInfo.RepMontageInfo.BlendTime = MontageInstance->GetBlendTime();
							break;
						}
					}
				}
			}
			// When we start or stop an animation, update the clients right away for the Avatar Actor
			if (AbilityActorInfo->AvatarActor != nullptr)
			{
				AbilityActorInfo->AvatarActor->ForceNetUpdate();
			}

			// When this changes, we should update whether or not we should be ticking
			UpdateShouldTick();
		}

		// Replicate NextSectionID to keep it in sync.
		// We actually replicate NextSectionID+1 on a BYTE to put INDEX_NONE in there.
		int32 CurrentSectionID = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionIndexFromPosition(OutRepAnimMontageInfo.RepMontageInfo.Position);
		if (CurrentSectionID != INDEX_NONE)
		{
			constexpr bool bForceGameThreadValue = true;
			if (CVarUpdateMontageSectionIdToPlay.GetValueOnAnyThread(bForceGameThreadValue))
			{
				OutRepAnimMontageInfo.RepMontageInfo.SectionIdToPlay = uint8(CurrentSectionID + 1);
			}

			int32 NextSectionID = AnimInstance->Montage_GetNextSectionID(AnimMontageInfo.LocalMontageInfo.AnimMontage, CurrentSectionID);
			if (NextSectionID >= (256 - 1))
			{
				ABILITY_LOG(Error, TEXT("AnimMontage_UpdateReplicatedData. NextSectionID = %d.  RepAnimMontageInfo.Position: %.2f, CurrentSectionID: %d. LocalAnimMontageInfo.AnimMontage %s"),
				            NextSectionID, OutRepAnimMontageInfo.RepMontageInfo.Position, CurrentSectionID, *GetNameSafe(AnimMontageInfo.LocalMontageInfo.AnimMontage));
				ensure(NextSectionID < (256 - 1));
			}
			OutRepAnimMontageInfo.RepMontageInfo.NextSectionID = uint8(NextSectionID + 1);
		}
		else
		{
			OutRepAnimMontageInfo.RepMontageInfo.NextSectionID = 0;
		}
	}
}

void UAbilitySystemComponentBase::OnRep_ReplicatedAnimMontageForMesh()
{
	
	bool bLocalForceMontageReplicationRefresh = false;
	for (FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& NewRepMontageInfoForMesh : RepAnimMontageInfoForMeshes)
	{
		if (!IsValid(NewRepMontageInfoForMesh.Mesh))
		{
			UE_LOG(LogAbilitySystem, Warning, TEXT("[MontageReplication] Mesh NULL détecté. Tentative de récupération via Owner et MeshName : %s"), *NewRepMontageInfoForMesh.MeshName);

			AActor* OwnerToUse = nullptr;

			if (IsValid(NewRepMontageInfoForMesh.MeshOwner))
			{
				OwnerToUse = NewRepMontageInfoForMesh.MeshOwner;
			}
			else if (AbilityActorInfo.IsValid() && AbilityActorInfo->AvatarActor.IsValid())
			{
				OwnerToUse = AbilityActorInfo->AvatarActor.Get();
				UE_LOG(LogAbilitySystem, Warning, TEXT("[MontageReplication] MeshOwner invalide. Utilisation de AvatarActor : %s pour récupérer le Mesh."), *GetNameSafe(OwnerToUse));
			}
			else
			{
				UE_LOG(LogAbilitySystem, Warning, TEXT("[MontageReplication] Échec : MeshOwner et AvatarActor invalides pour récupérer le Mesh : %s"), *NewRepMontageInfoForMesh.MeshName);
				bLocalForceMontageReplicationRefresh = true;
				continue; // Aucun moyen valide de récupérer le Mesh pour l'instant
			}

			TArray<USkeletalMeshComponent*> MeshComponents;
			OwnerToUse->GetComponents<USkeletalMeshComponent>(MeshComponents);

			bool bFoundMesh = false;

			for (USkeletalMeshComponent* CandidateMesh : MeshComponents)
			{
				if (CandidateMesh && CandidateMesh->GetName() == NewRepMontageInfoForMesh.MeshName)
				{
					NewRepMontageInfoForMesh.Mesh = CandidateMesh;
					bFoundMesh = true;
					UE_LOG(LogAbilitySystem, Warning, TEXT("[MontageReplication] Mesh récupéré avec succès : %s (Owner utilisé : %s)"),
					       *CandidateMesh->GetName(), *GetNameSafe(OwnerToUse));
					break;
				}
			}

			if (!bFoundMesh)
			{
				UE_LOG(LogAbilitySystem, Warning, TEXT("[MontageReplication] Échec de récupération du Mesh : %s (Owner utilisé : %s)"),
				       *NewRepMontageInfoForMesh.MeshName, *GetNameSafe(OwnerToUse));
				bLocalForceMontageReplicationRefresh = true;
				continue; // Mesh introuvable pour l'instant
			}
		}
	}
	if (bLocalForceMontageReplicationRefresh)
	{		
		if (!bForceMontageReplicationRefresh)
		{
			bForceMontageReplicationRefresh = true;
			SendServerRequestForceMontageReplication();
			UE_LOG(LogAbilitySystem, Warning, TEXT("[MontageReplication] Force refresh montage replication"));
		}
	}

	UWorld* World = GetWorld();
	for (FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& NewRepMontageInfoForMesh : RepAnimMontageInfoForMeshes)
	{
		FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(NewRepMontageInfoForMesh.Mesh, NewRepMontageInfoForMesh.SlotName);


		if (NewRepMontageInfoForMesh.RepMontageInfo.bSkipPlayRate)
		{
			NewRepMontageInfoForMesh.RepMontageInfo.PlayRate = 1.f;
		}

		const bool bIsPlayingReplay = World && World->IsPlayingReplay();

		const float MONTAGE_REP_POS_ERR_THRESH = bIsPlayingReplay ? CVarReplayMontageErrorThreshold.GetValueOnGameThread() : 0.1f;

		UAnimInstance* AnimInstance = IsValid(NewRepMontageInfoForMesh.Mesh) && NewRepMontageInfoForMesh.Mesh->GetOwner()
		                              == AbilityActorInfo->AvatarActor
			                              ? NewRepMontageInfoForMesh.Mesh->GetAnimInstance()
			                              : nullptr;
		if (AnimInstance == nullptr || !IsReadyForReplicatedMontageForMesh())
		{
			// We can't handle this yet
			bPendingMontageRep = true;
			return;
		}
		bPendingMontageRep = false;
		

		if (!AbilityActorInfo->IsLocallyControlled() || (NewRepMontageInfoForMesh.SourceAbility && NewRepMontageInfoForMesh.SourceAbility.GetDefaultObject()->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::ServerOnly))
		{
			static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("net.Montage.Debug"));
			bool DebugMontage = (CVar && CVar->GetValueOnGameThread() == 1);
			if (DebugMontage)
			{
				ABILITY_LOG(Warning, TEXT("\n\nOnRep_ReplicatedAnimMontage, %s"), *GetNameSafe(this));
				ABILITY_LOG(Warning, TEXT("\tAnimMontage: %s\n\tPlayRate: %f\n\tPosition: %f\n\tBlendTime: %f\n\tNextSectionID: %d\n\tIsStopped: %d"),
				            *GetNameSafe(NewRepMontageInfoForMesh.RepMontageInfo.Animation),
				            NewRepMontageInfoForMesh.RepMontageInfo.PlayRate,
				            NewRepMontageInfoForMesh.RepMontageInfo.Position,
				            NewRepMontageInfoForMesh.RepMontageInfo.BlendTime,
				            NewRepMontageInfoForMesh.RepMontageInfo.NextSectionID,
				            NewRepMontageInfoForMesh.RepMontageInfo.IsStopped);
				ABILITY_LOG(Warning, TEXT("\tLocalAnimMontageInfo.AnimMontage: %s\n\tPosition: %f"),
				            *GetNameSafe(AnimMontageInfo.LocalMontageInfo.AnimMontage), AnimInstance->Montage_GetPosition(AnimMontageInfo.LocalMontageInfo.AnimMontage));
			}

			if (NewRepMontageInfoForMesh.RepMontageInfo.Animation)
			{
				// New Montage to play

				UAnimSequenceBase* LocalAnimation = AnimMontageInfo.LocalMontageInfo.AnimMontage && AnimMontageInfo.LocalMontageInfo.AnimMontage->IsDynamicMontage() ? AnimMontageInfo.LocalMontageInfo.AnimMontage->GetFirstAnimReference() : AnimMontageInfo.LocalMontageInfo.AnimMontage;
				if ((LocalAnimation != NewRepMontageInfoForMesh.RepMontageInfo.Animation) ||
					(AnimMontageInfo.LocalMontageInfo.PlayInstanceId != NewRepMontageInfoForMesh.RepMontageInfo.PlayInstanceId))
				{
					AnimMontageInfo.LocalMontageInfo.PlayInstanceId = NewRepMontageInfoForMesh.RepMontageInfo.PlayInstanceId;

					if (UAnimMontage* MontageToPlay = Cast<UAnimMontage>(NewRepMontageInfoForMesh.RepMontageInfo.Animation))
					{
						PlayMontageSimulatedForMesh(NewRepMontageInfoForMesh.Mesh, NewRepMontageInfoForMesh.SlotName, MontageToPlay, NewRepMontageInfoForMesh.RepMontageInfo.PlayRate);
					}
					else
					{
						PlaySlotAnimationAsDynamicMontageSimulatedForMesh(
							NewRepMontageInfoForMesh.RepMontageInfo.Animation,
							NewRepMontageInfoForMesh.Mesh,
							NewRepMontageInfoForMesh.SlotName,
							NewRepMontageInfoForMesh.RepMontageInfo.SlotName,
							NewRepMontageInfoForMesh.RepMontageInfo.BlendTime,
							NewRepMontageInfoForMesh.RepMontageInfo.BlendOutTime,
							NewRepMontageInfoForMesh.RepMontageInfo.PlayRate);
					}
				}

				if (AnimMontageInfo.LocalMontageInfo.AnimMontage == nullptr)
				{
					ABILITY_LOG(Warning, TEXT("OnRep_ReplicatedAnimMontage: PlayMontageSimulated failed. Name: %s, AnimMontage: %s"), *GetNameSafe(this), *GetNameSafe(NewRepMontageInfoForMesh.RepMontageInfo.Animation));
					return;
				}

				// Play Rate has changed
				if (AnimInstance->Montage_GetPlayRate(AnimMontageInfo.LocalMontageInfo.AnimMontage) != NewRepMontageInfoForMesh.RepMontageInfo.PlayRate)
				{
					AnimInstance->Montage_SetPlayRate(AnimMontageInfo.LocalMontageInfo.AnimMontage, NewRepMontageInfoForMesh.RepMontageInfo.PlayRate);
				}

				// Compressed Flags
				const bool bIsStopped = AnimInstance->Montage_GetIsStopped(AnimMontageInfo.LocalMontageInfo.AnimMontage);
				const bool bReplicatedIsStopped = bool(NewRepMontageInfoForMesh.RepMontageInfo.IsStopped);

				// Process stopping first, so we don't change sections and cause blending to pop.
				if (bReplicatedIsStopped)
				{
					if (!bIsStopped)
					{
						CurrentMontageStopForMesh(NewRepMontageInfoForMesh.Mesh, NewRepMontageInfoForMesh.SlotName, NewRepMontageInfoForMesh.RepMontageInfo.BlendTime);
					}
				}
				else if (!NewRepMontageInfoForMesh.RepMontageInfo.SkipPositionCorrection)
				{
					const int32 RepSectionID = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionIndexFromPosition(NewRepMontageInfoForMesh.RepMontageInfo.Position);
					const int32 RepNextSectionID = int32(NewRepMontageInfoForMesh.RepMontageInfo.NextSectionID) - 1;

					// And NextSectionID for the replicated SectionID.
					if (RepSectionID != INDEX_NONE)
					{
						const int32 NextSectionID = AnimInstance->Montage_GetNextSectionID(AnimMontageInfo.LocalMontageInfo.AnimMontage, RepSectionID);

						// If NextSectionID is different than the replicated one, then set it.
						if (NextSectionID != RepNextSectionID)
						{
							AnimInstance->Montage_SetNextSection(AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionName(RepSectionID), AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionName(RepNextSectionID), AnimMontageInfo.LocalMontageInfo.AnimMontage);
						}

						// Make sure we haven't received that update too late and the client hasn't already jumped to another section.
						const int32 CurrentSectionID = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionIndexFromPosition(AnimInstance->Montage_GetPosition(AnimMontageInfo.LocalMontageInfo.AnimMontage));
						if ((CurrentSectionID != RepSectionID) && (CurrentSectionID != RepNextSectionID))
						{
							// Client is in a wrong section, teleport him into the begining of the right section
							const float SectionStartTime = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetAnimCompositeSection(RepSectionID).GetTime();
							AnimInstance->Montage_SetPosition(AnimMontageInfo.LocalMontageInfo.AnimMontage, SectionStartTime);
						}
					}

					// Update Position. If error is too great, jump to replicated position.
					const float CurrentPosition = AnimInstance->Montage_GetPosition(AnimMontageInfo.LocalMontageInfo.AnimMontage);
					const int32 CurrentSectionID = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionIndexFromPosition(CurrentPosition);
					const float DeltaPosition = NewRepMontageInfoForMesh.RepMontageInfo.Position - CurrentPosition;

					// Only check threshold if we are located in the same section. Different sections require a bit more work as we could be jumping around the timeline.
					// And therefore DeltaPosition is not as trivial to determine.
					if ((CurrentSectionID == RepSectionID) && (FMath::Abs(DeltaPosition) > MONTAGE_REP_POS_ERR_THRESH) && (NewRepMontageInfoForMesh.RepMontageInfo.IsStopped == 0))
					{
						UAnimMontage* NewAnimMontageCasted = Cast<UAnimMontage>(NewRepMontageInfoForMesh.RepMontageInfo.Animation.Get());
						// fast forward to server position and trigger notifies
						if (FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(NewAnimMontageCasted))
						{
							// Skip triggering notifies if we're going backwards in time, we've already triggered them.
							const float DeltaTime = !FMath::IsNearlyZero(NewRepMontageInfoForMesh.RepMontageInfo.PlayRate) ? (DeltaPosition / NewRepMontageInfoForMesh.RepMontageInfo.PlayRate) : 0.f;
							if (DeltaTime >= 0.f)
							{
								MontageInstance->UpdateWeight(DeltaTime);
								MontageInstance->HandleEvents(CurrentPosition, NewRepMontageInfoForMesh.RepMontageInfo.Position, nullptr);
								AnimInstance->TriggerAnimNotifies(DeltaTime);
							}
						}
						AnimInstance->Montage_SetPosition(AnimMontageInfo.LocalMontageInfo.AnimMontage, NewRepMontageInfoForMesh.RepMontageInfo.Position);
					}
				}
				else
				{
					// here
					const float CurrentPosition = AnimInstance->Montage_GetPosition(AnimMontageInfo.LocalMontageInfo.AnimMontage);
					int32 CurrentSectionID = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionIndexFromPosition(CurrentPosition);
					const int32 RepSectionIdToPlay = (static_cast<int32>(NewRepMontageInfoForMesh.RepMontageInfo.SectionIdToPlay) - 1);
					FName CurrentSectionName = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionName(CurrentSectionID);

					// If RepSectionIdToPlay is valid and different from the current section, then jump to it
					if (RepSectionIdToPlay != INDEX_NONE && RepSectionIdToPlay != CurrentSectionID)
					{
						CurrentSectionName = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionName(RepSectionIdToPlay);
						if (CurrentSectionName != NAME_None)
						{
							AnimInstance->Montage_JumpToSection(CurrentSectionName);
							CurrentSectionID = RepSectionIdToPlay;
						}
						else
						{
							ABILITY_LOG(Warning, TEXT("OnRep_ReplicatedAnimMontage: Failed to replicate current section due to invalid name. Name: %s, Section ID: %i"),
							            *GetNameSafe(this),
							            CurrentSectionID);
						}
					}

					constexpr bool bForceGameThreadValue = true;
					if (CVarReplicateMontageNextSectionId.GetValueOnAnyThread(bForceGameThreadValue))
					{
						const int32 NextSectionID = AnimInstance->Montage_GetNextSectionID(AnimMontageInfo.LocalMontageInfo.AnimMontage, CurrentSectionID);
						const int32 RepNextSectionID = int32(NewRepMontageInfoForMesh.RepMontageInfo.NextSectionID) - 1;

						// If NextSectionID is different than the replicated one, then set it.
						if (RepNextSectionID != INDEX_NONE && NextSectionID != RepNextSectionID)
						{
							const FName NextSectionName = AnimMontageInfo.LocalMontageInfo.AnimMontage->GetSectionName(RepNextSectionID);
							if (CurrentSectionName != NAME_None && NextSectionName != NAME_None)
							{
								AnimInstance->Montage_SetNextSection(CurrentSectionName, NextSectionName, AnimMontageInfo.LocalMontageInfo.AnimMontage);
							}
							else
							{
								ABILITY_LOG(Warning, TEXT("OnRep_ReplicatedAnimMontage: Failed to replicate next section due to invalid name. Name: %s, Current Section ID: %i, Next Section ID: %i"),
								            *GetNameSafe(this),
								            CurrentSectionID,
								            RepNextSectionID);
							}
						}
					}
				}
			}
		}
	}
}

bool UAbilitySystemComponentBase::IsReadyForReplicatedMontageForMesh()
{
	// Si on est sur le serveur, on est toujours prêt
	if (IsOwnerActorAuthoritative())
	{
		return true;
	}

	// Sur le client, on vérifie que l'acteur existe depuis au moins 1 seconde
	if (!AbilityActorInfo.IsValid() || !AbilityActorInfo->AvatarActor.IsValid())
	{
		UE_LOG(LogAbilitySystem, Verbose, TEXT("[MontageReplication] IsReadyForReplicatedMontageForMesh - Client, ActorInfo ou AvatarActor invalide"));
		return false;
	}

	AActor* LOwnerActor = AbilityActorInfo->AvatarActor.Get();
	if (!LOwnerActor)
	{
		UE_LOG(LogAbilitySystem, Verbose, TEXT("[MontageReplication] IsReadyForReplicatedMontageForMesh - Client, OwnerActor NULL"));
		return false;
	}

	// Vérifier le temps écoulé depuis la création de l'acteur
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogAbilitySystem, Verbose, TEXT("[MontageReplication] IsReadyForReplicatedMontageForMesh - Client, World NULL"));
		return false;
	}

	float TimeSinceCreation = World->GetTimeSeconds() - LOwnerActor->CreationTime;
	bool bIsReady = TimeSinceCreation >= 1.0f;


	// Vérifier également que le SkeletalMeshComponent est valide
	if (bIsReady && (!AbilityActorInfo->SkeletalMeshComponent.IsValid() || !AbilityActorInfo->SkeletalMeshComponent->GetAnimInstance()))
	{
		UE_LOG(LogAbilitySystem, Verbose, TEXT("[MontageReplication] IsReadyForReplicatedMontageForMesh - Client, SkeletalMeshComponent ou AnimInstance invalide"));
		bIsReady = false;
	}

	return bIsReady;
}

void UAbilitySystemComponentBase::ServerCurrentMontageSetNextSectionNameForMesh_Implementation(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, float ClientPosition, FName SlotName, FName SectionName, FName NextSectionName)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);

	if (AnimInstance && AnimMontageInfo.LocalMontageInfo.AnimMontage && ClientAnim)
	{
		UAnimMontage* CurrentAnimMontage = AnimMontageInfo.LocalMontageInfo.AnimMontage;
		UAnimSequenceBase* CurrentAnimation = CurrentAnimMontage->IsDynamicMontage() ? CurrentAnimMontage->GetFirstAnimReference() : CurrentAnimMontage;

		if (ClientAnim == CurrentAnimation)
		{
			// Set NextSectionName
			AnimInstance->Montage_SetNextSection(SectionName, NextSectionName, CurrentAnimMontage);

			float CurrentPosition = AnimInstance->Montage_GetPosition(CurrentAnimMontage);
			int32 CurrentSectionID = CurrentAnimMontage->GetSectionIndexFromPosition(CurrentPosition);
			FName CurrentSectionName = CurrentAnimMontage->GetSectionName(CurrentSectionID);

			int32 ClientSectionID = CurrentAnimMontage->GetSectionIndexFromPosition(ClientPosition);
			FName ClientCurrentSectionName = CurrentAnimMontage->GetSectionName(ClientSectionID);
			if ((CurrentSectionName != ClientCurrentSectionName) || (CurrentSectionName != SectionName))
			{
				// We are in an invalid section, jump to client's position.
				AnimInstance->Montage_SetPosition(CurrentAnimMontage, ClientPosition);
			}

			// Update replicated version for Simulated Proxies if we are on the server.
			if (IsOwnerActorAuthoritative())
			{
				AnimMontage_UpdateReplicatedDataForMesh(InMesh, SlotName);
			}
		}
	}
}

bool UAbilitySystemComponentBase::ServerCurrentMontageSetNextSectionNameForMesh_Validate(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, float ClientPosition, FName SlotName, FName SectionName, FName NextSectionName)
{
	return true;
}

void UAbilitySystemComponentBase::ServerCurrentMontageJumpToSectionNameForMesh_Implementation(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, FName SectionName)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);

	UAnimSequenceBase* CurrentAnimation = AnimMontageInfo.LocalMontageInfo.AnimMontage->IsDynamicMontage() ? AnimMontageInfo.LocalMontageInfo.AnimMontage->GetFirstAnimReference() : AnimMontageInfo.LocalMontageInfo.AnimMontage;
	if (AnimInstance && CurrentAnimation)
	{
		if (ClientAnim == CurrentAnimation)
		{
			// Set NextSectionName
			AnimInstance->Montage_JumpToSection(SectionName, AnimMontageInfo.LocalMontageInfo.AnimMontage);

			// Update replicated version for Simulated Proxies if we are on the server.
			if (IsOwnerActorAuthoritative())
			{
				FGameplayAbilityRepAnimMontageInfoByMeshAndSlot& MutableRepAnimMontageInfo = GetGameplayAbilityRepAnimMontageForMesh(InMesh, SlotName);

				MutableRepAnimMontageInfo.RepMontageInfo.SectionIdToPlay = 0;
				if (MutableRepAnimMontageInfo.RepMontageInfo.Animation && SectionName != NAME_None)
				{
					// Only change SectionIdToPlay if the anim montage's source is a montage. Dynamic montages have no sections.
					if (const UAnimMontage* RepAnimMontage = Cast<UAnimMontage>(MutableRepAnimMontageInfo.RepMontageInfo.Animation))
					{
						// we add one so INDEX_NONE can be used in the on rep
						MutableRepAnimMontageInfo.RepMontageInfo.SectionIdToPlay = RepAnimMontage->GetSectionIndex(SectionName) + 1;
					}
				}

				AnimMontage_UpdateReplicatedDataForMesh(InMesh, SlotName);
			}
		}
	}
}

bool UAbilitySystemComponentBase::ServerCurrentMontageJumpToSectionNameForMesh_Validate(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, FName SectionName)
{
	return true;
}

void UAbilitySystemComponentBase::ServerCurrentMontageSetPlayRateForMesh_Implementation(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, float InPlayRate)
{
	if (!IsValid(InMesh))
		InMesh = AbilityActorInfo->SkeletalMeshComponent.Get();
	UAnimInstance* AnimInstance = IsValid(InMesh) && InMesh->GetOwner() == AbilityActorInfo->AvatarActor ? InMesh->GetAnimInstance() : nullptr;
	FGameplayAbilityLocalAnimMontageInfoByMeshAndSlot& AnimMontageInfo = GetLocalAnimMontageInfoForMesh(InMesh, SlotName);

	if (AnimInstance)
	{
		UAnimMontage* CurrentAnimMontage = AnimMontageInfo.LocalMontageInfo.AnimMontage;

		UAnimSequenceBase* CurrentAnimation = CurrentAnimMontage->IsDynamicMontage() ? CurrentAnimMontage->GetFirstAnimReference() : CurrentAnimMontage;
		if (ClientAnim == CurrentAnimation)
		{
			// Set PlayRate
			AnimInstance->Montage_SetPlayRate(AnimMontageInfo.LocalMontageInfo.AnimMontage, InPlayRate);

			// Update replicated version for Simulated Proxies if we are on the server.
			if (IsOwnerActorAuthoritative())
			{
				AnimMontage_UpdateReplicatedDataForMesh(InMesh, SlotName);
			}
		}
	}
}

bool UAbilitySystemComponentBase::ServerCurrentMontageSetPlayRateForMesh_Validate(USkeletalMeshComponent* InMesh, UAnimSequenceBase* ClientAnim, FName SlotName, float InPlayRate)
{
	return true;
}


void UAbilitySystemComponentBase::MulticastRefreshGameplayTagUpdateEvent_Implementation(FGameplayTag EventTag, int32 Count)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		if (HasGameplayTag(EventTag) != (Count != 0))
		{
			
			SetGameplayTagCount(EventTag, Count);
			//next tick
		}
		else
		{
			OnTagUpdate.Broadcast(EventTag, Count != 0);
		}
	}
}

bool UAbilitySystemComponentBase::MulticastRefreshGameplayTagUpdateEvent_Validate(FGameplayTag EventTag, int32 Count)
{
	return true;
}


void UAbilitySystemComponentBase::SendServerOnlyGameplayEvent(FGameplayTag EventTag, FGameplayEventData Payload)
{
	if (GetOwnerRole() == ROLE_Authority)
		HandleGameplayEvent(EventTag, &Payload);
	else if (GetOwnerRole() == ROLE_AutonomousProxy)
		RunOnServerOnly_SendGameplayEvent(EventTag, Payload);
	else
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
		auto Char = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		if (Char)
		{
			IAbilitySystemInterface* AsAbilitySysInterface = Cast<IAbilitySystemInterface>(Char);
			if (AsAbilitySysInterface)
			{
				if (UAbilitySystemComponent* GasComp = AsAbilitySysInterface->GetAbilitySystemComponent())
				{
					if (UAbilitySystemComponentBase* GasCompCasted = Cast<UAbilitySystemComponentBase>(GasComp))
					{
						GasCompCasted->RunOnServerOnly_SendGameplayEvent_ToActor(GetAvatar(), EventTag, Payload);
					}
				}
			}
		}
	}
}

void UAbilitySystemComponentBase::SendServerOnlyGameplayEvent_VC(FGameplayTag EventTag, UVariableContainer* Payload)
{
	if (GetOwnerRole() == ROLE_Authority)
		HandleGameplayEvent_VariableContainer(EventTag, FGAS_Utility_VariableContainerStruct(Payload));
	else if (GetOwnerRole() == ROLE_AutonomousProxy)
		RunOnServerOnly_SendGameplayEvent_VC(EventTag, FGAS_Utility_VariableContainerStruct(Payload));
	else
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
		auto Char = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		if (Char)
		{
			IAbilitySystemInterface* AsAbilitySysInterface = Cast<IAbilitySystemInterface>(Char);
			if (AsAbilitySysInterface)
			{
				if (UAbilitySystemComponent* GasComp = AsAbilitySysInterface->GetAbilitySystemComponent())
				{
					if (UAbilitySystemComponentBase* GasCompCasted = Cast<UAbilitySystemComponentBase>(GasComp))
					{
						GasCompCasted->RunOnServerOnly_SendGameplayEvent_ToActor_VC(GetAvatar(), EventTag, FGAS_Utility_VariableContainerStruct(Payload));
					}
				}
			}
		}
	}
}

void UAbilitySystemComponentBase::RunOnServerOnly_SendGameplayEvent_VC_Implementation(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload)
{
	if (GetOwnerRole() == ROLE_Authority)
		HandleGameplayEvent_VariableContainer(EventTag, Payload);
}

void UAbilitySystemComponentBase::RunOnServerOnly_SendGameplayEvent_Implementation(FGameplayTag EventTag, FGameplayEventData Payload)
{
	if (GetOwnerRole() == ROLE_Authority)
		HandleGameplayEvent(EventTag, &Payload);
}

void UAbilitySystemComponentBase::RunOnServerOnly_SendGameplayEvent_ToActor_Implementation(AActor* Actor, FGameplayTag EventTag, FGameplayEventData Payload)
{
	if (IsValid(Actor))
	{
		IAbilitySystemInterface* AsAbilitySysInterface = Cast<IAbilitySystemInterface>(Actor);
		if (AsAbilitySysInterface)
		{
			if (UAbilitySystemComponent* GasComp = AsAbilitySysInterface->GetAbilitySystemComponent())
			{
				if (UAbilitySystemComponentBase* GasCompCasted = Cast<UAbilitySystemComponentBase>(GasComp))
				{
					GasCompCasted->HandleGameplayEvent(EventTag, &Payload);
				}
			}
		}
	}
}

void UAbilitySystemComponentBase::RunOnServerOnly_SendGameplayEvent_ToActor_VC_Implementation(AActor* Actor, FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload)
{
	if (IsValid(Actor))
	{
		IAbilitySystemInterface* AsAbilitySysInterface = Cast<IAbilitySystemInterface>(Actor);
		if (AsAbilitySysInterface)
		{
			if (UAbilitySystemComponent* GasComp = AsAbilitySysInterface->GetAbilitySystemComponent())
			{
				if (UAbilitySystemComponentBase* GasCompCasted = Cast<UAbilitySystemComponentBase>(GasComp))
				{
					GasCompCasted->HandleGameplayEvent_VariableContainer(EventTag, Payload);
				}
			}
		}
	}
}

void UAbilitySystemComponentBase::SendServerGameplayEventMulticast(FGameplayTag EventTag, FGameplayEventData Payload)
{
	if (GetOwnerRole() == ROLE_Authority)
		MulticastSendGameplayEvent(EventTag, Payload);
	else
	{
		if (IgnoreLocalGameplayEventCount.Contains(EventTag))
			IgnoreLocalGameplayEventCount.Add(EventTag, IgnoreLocalGameplayEventCount[EventTag] + 1);
		else
			IgnoreLocalGameplayEventCount.Add(EventTag, 1);
		HandleGameplayEvent(EventTag, &Payload);
		if (GetOwnerRole() == ROLE_AutonomousProxy)
		{
			MulticastOnServer_SendGameplayEvent(EventTag, Payload);
		}
		else
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
			auto Char = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			if (Char)
			{
				IAbilitySystemInterface* AsAbilitySysInterface = Cast<IAbilitySystemInterface>(Char);
				if (AsAbilitySysInterface)
				{
					if (UAbilitySystemComponent* GasComp = AsAbilitySysInterface->GetAbilitySystemComponent())
					{
						if (UAbilitySystemComponentBase* GasCompCasted = Cast<UAbilitySystemComponentBase>(GasComp))
						{
							GasCompCasted->MulticastOnServer_SendGameplayEvent_ToActor(GetAvatar(), EventTag, Payload);
						}
					}
				}
			}
		}
	}
}

void UAbilitySystemComponentBase::MulticastOnServer_SendGameplayEvent_ToActor_Implementation(AActor* Actor, FGameplayTag EventTag, FGameplayEventData Payload)
{
	if (IsValid(Actor))
	{
		IAbilitySystemInterface* AsAbilitySysInterface = Cast<IAbilitySystemInterface>(Actor);
		if (AsAbilitySysInterface)
		{
			if (UAbilitySystemComponent* GasComp = AsAbilitySysInterface->GetAbilitySystemComponent())
			{
				if (UAbilitySystemComponentBase* GasCompCasted = Cast<UAbilitySystemComponentBase>(GasComp))
				{
					GasCompCasted->MulticastSendGameplayEvent(EventTag, Payload);
				}
			}
		}
	}
}

void UAbilitySystemComponentBase::MulticastOnServer_SendGameplayEvent_Implementation(FGameplayTag EventTag, FGameplayEventData Payload)
{
	MulticastSendGameplayEvent(EventTag, Payload);
}


void UAbilitySystemComponentBase::MulticastSendGameplayEvent_Implementation(FGameplayTag EventTag, FGameplayEventData Payload)
{
	if (IgnoreLocalGameplayEventCount.Contains(EventTag))
	{
		if (IgnoreLocalGameplayEventCount[EventTag] <= 1)
			IgnoreLocalGameplayEventCount.Remove(EventTag);
		else
			IgnoreLocalGameplayEventCount.Add(EventTag, IgnoreLocalGameplayEventCount[EventTag] - 1);
		return;
	}
	HandleGameplayEvent(EventTag, &Payload);
}

void UAbilitySystemComponentBase::SendServerGameplayEvent_VariableContainer_Multicast(FGameplayTag EventTag, UVariableContainer* Payload)
{
	if (GetOwnerRole() == ROLE_Authority)
		MulticastSendGameplayEvent_VariableContainer(EventTag, FGAS_Utility_VariableContainerStruct(Payload));
	else
	{
		if (IgnoreLocalGameplayEventCount.Contains(EventTag))
			IgnoreLocalGameplayEventCount.Add(EventTag, IgnoreLocalGameplayEventCount[EventTag] + 1);
		else
			IgnoreLocalGameplayEventCount.Add(EventTag, 1);
		HandleGameplayEvent_VariableContainer(EventTag, FGAS_Utility_VariableContainerStruct(Payload));

		if (GetOwnerRole() == ROLE_AutonomousProxy)
			SendServerGameplayEvent_VC_Multicast(EventTag, FGAS_Utility_VariableContainerStruct(Payload));
		else
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
			auto Char = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			if (Char)
			{
				IAbilitySystemInterface* AsAbilitySysInterface = Cast<IAbilitySystemInterface>(Char);
				if (AsAbilitySysInterface)
				{
					if (UAbilitySystemComponent* GasComp = AsAbilitySysInterface->GetAbilitySystemComponent())
					{
						if (UAbilitySystemComponentBase* GasCompCasted = Cast<UAbilitySystemComponentBase>(GasComp))
						{
							GasCompCasted->SendServerGameplayEvent_VC_Multicast_OnActor(GetAvatar(), EventTag, Payload);
						}
					}
				}
			}
		}
	}
}


void UAbilitySystemComponentBase::SendServerGameplayEvent_VC_Multicast_Implementation(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload)
{
	MulticastSendGameplayEvent_VariableContainer(EventTag, Payload);
}

void UAbilitySystemComponentBase::SendServerGameplayEvent_VC_Multicast_OnActor_Implementation(AActor* Actor, FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload)
{
	if (IsValid(Actor))
	{
		IAbilitySystemInterface* AsAbilitySysInterface = Cast<IAbilitySystemInterface>(Actor);
		if (AsAbilitySysInterface)
		{
			if (UAbilitySystemComponent* GasComp = AsAbilitySysInterface->GetAbilitySystemComponent())
			{
				if (UAbilitySystemComponentBase* GasCompCasted = Cast<UAbilitySystemComponentBase>(GasComp))
				{
					GasCompCasted->MulticastSendGameplayEvent_VariableContainer(EventTag, Payload);
				}
			}
		}
	}
}


void UAbilitySystemComponentBase::MulticastSendGameplayEvent_VariableContainer_Implementation(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload)
{
	if (IgnoreLocalGameplayEventCount.Contains(EventTag))
	{
		if (IgnoreLocalGameplayEventCount[EventTag] <= 1)
			IgnoreLocalGameplayEventCount.Remove(EventTag);
		else
			IgnoreLocalGameplayEventCount.Add(EventTag, IgnoreLocalGameplayEventCount[EventTag] - 1);
		return;
	}
	HandleGameplayEvent_VariableContainer(EventTag, Payload);
}

void UAbilitySystemComponentBase::HandleGameplayEvent_VariableContainer(FGameplayTag EventTag, FGAS_Utility_VariableContainerStruct Payload)
{
	UVariableContainer* VariableContainer = NewObject<UVariableContainer>(this);
	VariableContainer->SetupFromArrays(Payload.VariablesNames, Payload.VariablesValues);
	FGameplayEventData PayloadStruct;
	PayloadStruct.OptionalObject = VariableContainer;
	HandleGameplayEvent(EventTag, &PayloadStruct);
}

void UAbilitySystemComponentBase::ReceiveServerRequestForceMontageReplicationForAnotherClient_Implementation(UAbilitySystemComponentBase* TargetASC)
{
	if (IsValid(TargetASC))
	{
		TargetASC->ServerRequestForceMontageReplication();
	}
}

bool UAbilitySystemComponentBase::ReceiveServerRequestForceMontageReplicationForAnotherClient_Validate(UAbilitySystemComponentBase* TargetASC)
{
	return true;
}

void UAbilitySystemComponentBase::SendServerRequestForceMontageReplication()
{
	// Récupère le PlayerController local
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UAbilitySystemComponentBase::SendServerRequestForceMontageReplication);
		return;
	}

	// Récupère le PlayerState
	APlayerState* PS = PC->PlayerState;
	if (!PS)
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UAbilitySystemComponentBase::SendServerRequestForceMontageReplication);
		return;
	}

	// Récupère le GAS component du PlayerState
	IAbilitySystemInterface* AbilityInterface = Cast<IAbilitySystemInterface>(PS);
	if (!AbilityInterface)
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UAbilitySystemComponentBase::SendServerRequestForceMontageReplication);
		return;
	}

	UAbilitySystemComponentBase* PSGASComp = Cast<UAbilitySystemComponentBase>(AbilityInterface->GetAbilitySystemComponent());
	if (!PSGASComp)
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UAbilitySystemComponentBase::SendServerRequestForceMontageReplication);
		return;
	}
	PSGASComp->ReceiveServerRequestForceMontageReplicationForAnotherClient(this);
}

bool UAbilitySystemComponentBase::ServerRequestForceMontageReplication_Validate()
{
	return true;
}

void UAbilitySystemComponentBase::ServerRequestForceMontageReplication_Implementation()
{
    MulticastForceMontageReplication(RepAnimMontageInfoForMeshes);
}

void UAbilitySystemComponentBase::MulticastForceMontageReplication_Implementation(const TArray<FGameplayAbilityRepAnimMontageInfoByMeshAndSlot>& MontageInfos)
{
    if (bForceMontageReplicationRefresh)
    {
        RepAnimMontageInfoForMeshes = MontageInfos;
        bForceMontageReplicationRefresh = false;
        OnRep_ReplicatedAnimMontageForMesh();
    }
}