#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NPCDebugWidget.generated.h"

class ANPCCharacter;

/**
 * Base UserWidget for NPC overhead debug UI.
 * Can be assigned to ANPCCharacter's DebugWidgetComponent, or subclassed in UMG.
 * Automatically synchronizes Hunger, Thirst, Power, Body Temp, Sickness, Mood, Money, and Formatted Text.
 */
UCLASS()
class MARRYBYMIDNIGHT_API UNPCDebugWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Owner NPC */
	UPROPERTY(BlueprintReadOnly, Category = "NPC|Debug")
	TWeakObjectPtr<ANPCCharacter> TargetNPC;

	/** Stat Values */
	UPROPERTY(BlueprintReadOnly, Category = "NPC|Debug")
	float Hunger = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Debug")
	float Thirst = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Debug")
	float Power = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Debug")
	float BodyTemperature = 37.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Debug")
	float Sickness = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Debug")
	FString MoodName = TEXT("Content");

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Debug")
	float Money = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Debug")
	FString FormattedStatsText;

	/** Called whenever stats update each frame so Blueprints can update custom bars or colors */
	UFUNCTION(BlueprintImplementableEvent, Category = "NPC|Debug")
	void OnStatsUpdated();

	UFUNCTION(BlueprintCallable, Category = "NPC|Debug")
	void SetTargetNPC(ANPCCharacter* InNPC);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
};
