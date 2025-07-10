// Fill out your copyright notice in the Description page of Project Settings.


#include "CharacterAttributeSet.h"
#include "Net/UnrealNetwork.h"

#include "GAS_Utility/AbilitySystem/AbilitySystemComponentBase.h"


#define SET_REPLICATED_ATTRIBUTE_VALUE(Attribute, NewValue, OldValue) \
{ \
if (GetAbilitySystemComponentBase()) \
{ \
GetAbilitySystemComponentBase()->SetReplicatedAttributeValue(Attribute, NewValue, OldValue); \
} \
}
void UCharacterAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, MaxSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, MaxAcceleration, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, AirControl, COND_None, REPNOTIFY_Always);
}

UCharacterAttributeSet::UCharacterAttributeSet(): MaxSpeed(200.f),GravityScale(1.f),MaxAcceleration(2048.f),AirControl(1.f),RotationRate(360.f)
{
}

void UCharacterAttributeSet::OnRep_MaxSpeed(const FGameplayAttributeData& OldValue)
{
	SET_REPLICATED_ATTRIBUTE_VALUE( GetMaxSpeedAttribute(), MaxSpeed.GetCurrentValue(), OldValue.GetCurrentValue());
}

void UCharacterAttributeSet::OnRep_MaxAcceleration(const FGameplayAttributeData& OldValue)
{
	SET_REPLICATED_ATTRIBUTE_VALUE( GetMaxAccelerationAttribute(), MaxAcceleration.GetCurrentValue(), OldValue.GetCurrentValue());
}

void UCharacterAttributeSet::OnRep_AirControl(const FGameplayAttributeData& OldValue)
{
	SET_REPLICATED_ATTRIBUTE_VALUE( GetAirControlAttribute(), AirControl.GetCurrentValue(), OldValue.GetCurrentValue());
}

UAbilitySystemComponentBase* UCharacterAttributeSet::GetAbilitySystemComponentBase() const
{
	return Cast<UAbilitySystemComponentBase>(GetOwningAbilitySystemComponent());
}
