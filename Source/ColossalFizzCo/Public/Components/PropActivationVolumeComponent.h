#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "PropActivationVolumeComponent.generated.h"

class APropManager;

/** Server-owned residency policy: keep overlapping prop identities in their active representation. */
UCLASS(ClassGroup=(Props), meta=(BlueprintSpawnableComponent, DisplayName="Prop Activation Volume"))
class COLOSSALFIZZCO_API UPropActivationVolumeComponent : public UBoxComponent
{
	GENERATED_BODY()
public:
	UPropActivationVolumeComponent();

	/** Turning this off releases this volume's policy; other overlapping volumes still apply. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prop Activation")
	bool bKeepPropsActive = true;

	/** Spatial-query interval in seconds. No inactive-prop per-frame updates are required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prop Activation", meta=(ClampMin="0.02", ClampMax="5.0"))
	float RefreshInterval = .1f;

	/** Maximum ISM promotion attempts per refresh; failed allocations retry on later refreshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prop Activation", meta=(ClampMin="1", ClampMax="128"))
	int32 MaxPromotionsPerRefresh = 8;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Prop Activation")
	void SetKeepingPropsActive(bool bEnabled);

	/** Also callable after moving/resizing the volume when an immediate refresh is needed. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Prop Activation")
	void RefreshProps();

	UFUNCTION(BlueprintPure, Category="Prop Activation")
	int32 GetTrackedPropCount() const { return TrackedISIDs.Num(); }

	bool IsPolicyEnabled() const;
	bool ContainsActiveInstance(const APropManager* Manager, int32 ISID) const;
	virtual void Activate(bool bReset=false) override;
	virtual void Deactivate() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TWeakObjectPtr<APropManager> RegisteredManager;
	TSet<int32> TrackedISIDs;
	int32 PromotionCursor = 0;
	void ReleaseTrackedProps();
	void UnregisterPolicy();
};
