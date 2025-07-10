// © 2024 Embers. All Rights Reserved


#include "GameplayEffectContextLibrary.h"

FGameplayEffectContextHandle UGameplayEffectContextLibrary::MakeVariableContainerHandle(UVariableContainer* VariableContainer)
{
	if (!VariableContainer)
	{
		return FGameplayEffectContextHandle();
	}

	// Creates a custom context that stores the container via the structure
	FGameplayEffectContext_VariableContainer* NewContext = new FGameplayEffectContext_VariableContainer(VariableContainer);
    
	// Wraps the context in a handle and returns it
	FGameplayEffectContextHandle Handle(NewContext);
	return Handle;
}

void UGameplayEffectContextLibrary::BreakVariableContainerHandle(const FGameplayEffectContextHandle& Handle, bool& bIsValid, UVariableContainer*& OutVariableContainer)
{
	bIsValid = false;
	OutVariableContainer = nullptr;

	// Retrieves the encapsulated context pointer
	const FGameplayEffectContext* BaseContext = Handle.Get();
	if (!BaseContext)
	{
		return;
	}

	// Checks if the context is of the custom type
	if (BaseContext->GetScriptStruct() == FGameplayEffectContext_VariableContainer::StaticStruct())
	{
		const FGameplayEffectContext_VariableContainer* VariableContext = static_cast<const FGameplayEffectContext_VariableContainer*>(BaseContext);
		// Assumes that FGAS_Utility_VariableContainerStruct has a GetContainer() method that returns the original UVariableContainer*
		AActor* InstigatorActor = BaseContext->GetInstigator();
		UObject* Outer = InstigatorActor ? static_cast<UObject*>(InstigatorActor) : GetTransientPackage();

		OutVariableContainer = NewObject<UVariableContainer>(Outer);
		OutVariableContainer->SetupFromArrays(VariableContext->VariableContainer.VariablesNames, VariableContext->VariableContainer.VariablesValues);
		bIsValid = (OutVariableContainer != nullptr);
	}
}
UVariableContainer* UGameplayEffectContextLibrary::CreateVariableContainerFromStruct(UObject* WorldContextObject, const FGAS_Utility_VariableContainerStruct& Data)
{
	UObject* Outer = WorldContextObject ? WorldContextObject : GetTransientPackage();

	UVariableContainer* NewVariableContainer = NewObject<UVariableContainer>(Outer);

	if (NewVariableContainer)
	{
		NewVariableContainer->SetupFromArrays(Data.VariablesNames, Data.VariablesValues);
	}

	return NewVariableContainer;
}
