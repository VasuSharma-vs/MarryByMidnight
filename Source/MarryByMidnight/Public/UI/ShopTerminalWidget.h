#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "ShopTerminalWidget.generated.h"

class UShopTerminalWidget;
class UVerticalBox;
class UTextBlock;

UCLASS()
class MARRYBYMIDNIGHT_API UShopTerminalButton : public UButton
{
	GENERATED_BODY()
public:
	void Configure(UShopTerminalWidget* Owner, const FString& Label);
	EShopScannerAction Action = EShopScannerAction::Save;
	FGuid SessionID, CaseID;
	int32 ISID = 0, Denomination = 0;
	bool bClose = false;
private:
	TWeakObjectPtr<UShopTerminalWidget> Terminal;
	UFUNCTION() void HandleClick();
};

/** Shared checkout/ATM UI reads replicated state and sends actions through the owned player component. */
UCLASS(Blueprintable)
class MARRYBYMIDNIGHT_API UShopTerminalWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void Configure(UShopPlayerInteractionComponent* InActions, AActor* InStation);
	void HandleButton(const UShopTerminalButton* Button);
	void Close();
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Body;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Summary;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResultText;
	TWeakObjectPtr<UShopPlayerInteractionComponent> Actions;
	TWeakObjectPtr<AActor> Station;
	FString LastContents;
	float RefreshCountdown = 0;
	void Refresh();
	void AddText(const FString& Text);
	UShopTerminalButton* AddButton(const FString& Label);
	UFUNCTION() void ActionResult(int64 Sequence, EShopCheckoutResult Result);
};
