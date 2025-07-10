// © 2024 Embers. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VariableContainer/VariableContainer.h"
#include "GameplayEffectContextLibrary.generated.h"


USTRUCT()
struct GAS_UTILITY_API FGameplayEffectContext_VariableContainer : public FGameplayEffectContext
{
	GENERATED_BODY()

public:
	FGAS_Utility_VariableContainerStruct VariableContainer;

	FGameplayEffectContext_VariableContainer()
		: FGameplayEffectContext()
		, VariableContainer(nullptr)
	{
	}

	FGameplayEffectContext_VariableContainer(class UVariableContainer* InContainer)
		: FGameplayEffectContext()
		, VariableContainer(InContainer)
	{
	}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FGameplayEffectContext_VariableContainer::StaticStruct();
	}

	virtual FGameplayEffectContext* Duplicate() const override
	{
		FGameplayEffectContext_VariableContainer* NewContext = new FGameplayEffectContext_VariableContainer(*this);
		return NewContext;
	}

	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override
	{
		bOutSuccess = Super::NetSerialize(Ar, Map, bOutSuccess);
		Ar << VariableContainer;
		return true;
	}
	
};

// Spécialisation pour indiquer que notre struct possède un NetSerialize natif
template<>
struct TStructOpsTypeTraits<FGameplayEffectContext_VariableContainer> : public TStructOpsTypeTraitsBase2<FGameplayEffectContext_VariableContainer>
{
	enum
	{
		WithNetSerializer = true,
	};
};

UCLASS()
class GAS_UTILITY_API UGameplayEffectContextLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	/**
	 * Creates an FGameplayEffectContextHandle from a UVariableContainer.
	 * The UVariableContainer is converted into an FGAS_Utility_VariableContainerStruct
	 * structure and stored in an FGameplayEffectContext_VariableContainer.
	 */
	UFUNCTION(BlueprintCallable,BlueprintPure, Category="GAS|VariableContainer")
	static FGameplayEffectContextHandle MakeVariableContainerHandle(UVariableContainer* VariableContainer);

	/**
	 * Extracts the UVariableContainer from the FGameplayEffectContextHandle.
	 * Sets bIsValid to true if the context is indeed of type FGameplayEffectContext_VariableContainer.
	 */
	UFUNCTION(BlueprintCallable, Category="GAS|VariableContainer")
	static void BreakVariableContainerHandle(const FGameplayEffectContextHandle& Handle, bool& bIsValid, UVariableContainer*& OutVariableContainer);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "GAS_Utility|VariableContainer", meta = (WorldContext = "WorldContextObject"))
	static UVariableContainer* CreateVariableContainerFromStruct(UObject* WorldContextObject, const FGAS_Utility_VariableContainerStruct& Data);


};
