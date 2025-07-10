// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/MotionWarpingComponentBase.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"
#include "AnimNotify/RootMotionModifier/RootMotionModifier_SkewWarp_Base.h"

UMotionWarpingComponentBase::UMotionWarpingComponentBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
    
	// Default debug settings
	bDebugDrawWarpTargets = false;
	DebugDrawDuration = 0.0f; // Per-frame
	DebugArrowSize = 50.0f;
	DebugColor = FColor::Yellow;
}

void UMotionWarpingComponentBase::InitializeComponent()
{
	if (GetOwner()->GetLocalRole() == ROLE_Authority)
	{
		Super::InitializeComponent();
	}
}

void UMotionWarpingComponentBase::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bDebugDrawWarpTargets)
	{
		DrawDebugWarpTargets();
	}
}

void UMotionWarpingComponentBase::DrawDebugWarpTargets() const
{
	if (!GetWorld())
		return;

	for (const FMotionWarpingTarget& Target : WarpTargets)
	{
		const FTransform& TargetTransform = Target.GetTargetTrasform();
		const FVector Location = TargetTransform.GetLocation();
		const FRotator Rotation = TargetTransform.Rotator();

		// Draw position
		DrawDebugSphere(
			GetWorld(),
			Location,
			10.0f,
			12,
			DebugColor,
			false,
			DebugDrawDuration,
			0,
			2.0f
		);

		// Draw direction
		DrawDebugDirectionalArrow(
			GetWorld(),
			Location,
			Location + (Rotation.Vector() * DebugArrowSize),
			20.0f,
			DebugColor,
			false,
			DebugDrawDuration,
			0,
			2.0f
		);

		// Draw target name
		DrawDebugString(
			GetWorld(),
			Location + FVector(0, 0, 20.0f),
			Target.Name.ToString(),
			nullptr,
			DebugColor,
			DebugDrawDuration
		);
	}
}
void UMotionWarpingComponentBase::TriggerWarpStart(UAnimNotifyState_MotionWarping* NotifyMotionWarping, URootMotionModifier* Modifier)
{
	OnWarpStart.Broadcast(NotifyMotionWarping, Modifier);
}

void UMotionWarpingComponentBase::TriggerWarpUpdate(UAnimNotifyState_MotionWarping* NotifyMotionWarping, URootMotionModifier* Modifier)
{
	OnWarUpdate.Broadcast(NotifyMotionWarping, Modifier);
}

void UMotionWarpingComponentBase::TriggerWarpEnd(UAnimNotifyState_MotionWarping* NotifyMotionWarping, URootMotionModifier* Modifier)
{
	OnWarpEnd.Broadcast(NotifyMotionWarping, Modifier);
}

void UMotionWarpingComponentBase::RemoveModifierFromTemplate(URootMotionModifier* Modifier)
{
	if (Modifier)
	{
		if (const URootMotionModifier_SkewWarp_Base* ModifierCasteD = Cast<URootMotionModifier_SkewWarp_Base>(Modifier))
		{
			for (int i = Modifiers.Num() - 1; i >= 0; i--)
			{
				if (TObjectPtr<URootMotionModifier> TempModifier = Modifiers[i])
				{
					if (const URootMotionModifier_SkewWarp_Base* Casted = Cast<URootMotionModifier_SkewWarp_Base>(TempModifier))
					{
						if (ModifierCasteD->Notify == Casted->Notify && Casted->Notify)
							Modifiers.RemoveAt(i);
					}
				}
			}
		}
	}
}
