#include "Components/NPCAffordanceComponent.h"

UNPCAffordanceComponent::UNPCAffordanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNPCAffordanceComponent::RegisterAffordance(const FAffordanceDefinition& InAffordance)
{
	ProvidedAffordances.Add(InAffordance);
}

void UNPCAffordanceComponent::UnregisterAffordance(FName ActionName)
{
	ProvidedAffordances.RemoveAll([ActionName](const FAffordanceDefinition& Def)
	{
		return Def.ActionName == ActionName;
	});
}

bool UNPCAffordanceComponent::HasAffordance(EAffordanceAction ActionType) const
{
	for (const FAffordanceDefinition& Def : ProvidedAffordances)
	{
		if (Def.ActionType == ActionType)
		{
			return true;
		}
	}
	return false;
}

bool UNPCAffordanceComponent::GetAffordance(EAffordanceAction ActionType, FAffordanceDefinition& OutAffordance) const
{
	for (const FAffordanceDefinition& Def : ProvidedAffordances)
	{
		if (Def.ActionType == ActionType)
		{
			OutAffordance = Def;
			return true;
		}
	}
	return false;
}

bool UNPCAffordanceComponent::ExecuteAffordance(EAffordanceAction ActionType, AActor* InstigatorActor)
{
	FAffordanceDefinition FoundDef;
	if (GetAffordance(ActionType, FoundDef))
	{
		OnAffordanceExecuted.Broadcast(FoundDef, InstigatorActor);
		return true;
	}
	return false;
}
