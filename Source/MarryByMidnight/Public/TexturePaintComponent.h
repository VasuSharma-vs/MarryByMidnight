#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TexturePaintComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture;
class UTexture2D;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UTexturePaintComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTexturePaintComponent();

	/**
	 * Initializes painting:
	 * 1. Extracts the base texture from InBaseMaterial (or uses InOverrideBaseTexture).
	 * 2. Creates a Render Target matching the texture resolution and copies the base texture onto it.
	 * 3. Creates a Dynamic Material Instance from InBaseMaterial and sets the texture parameter to the Render Target.
	 * 4. Applies the Dynamic Material Instance to InMeshComponent at InMaterialSlotIndex.
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting", meta = (AdvancedDisplay = "InTextureParameterName, InMaterialSlotIndex, InOverrideBaseTexture"))
	void SetupPainting(
		UStaticMeshComponent* InMeshComponent,
		UMaterialInterface* InBaseMaterial,
		FName InTextureParameterName = FName("BaseTexture"),
		int32 InMaterialSlotIndex = 0,
		UTexture* InOverrideBaseTexture = nullptr
	);

	/** Perform a paint stroke at a specific UV coordinate */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void PaintAtUV(FVector2D UVCoordinate, float BrushSize = 50.0f, FLinearColor PaintColor = FLinearColor::Red);

	/** Clears or resets the Render Target canvas back to the base texture or specified clear color */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void ResetCanvas(FLinearColor ClearColor = FLinearColor::White, bool bRestoreBaseTexture = true);

	/** Returns the active Render Target */
	UFUNCTION(BlueprintPure, Category = "Painting")
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	/** Returns the dynamic material instance applied to the mesh */
	UFUNCTION(BlueprintPure, Category = "Painting")
	UMaterialInstanceDynamic* GetDynamicMaterial() const { return DynamicMaterial; }

	/** Configurable brush material (defaults to /Game/TextureDraw/M_Brush if null) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	UMaterialInterface* BrushMaterial;

	/** Configurable brush mask texture (defaults to /Game/TextureDraw/Bake_Mask_Image if null) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	UTexture2D* BrushMaskTexture;

	/** Name of the texture parameter inside the material to bind the Render Target to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	FName TextureParameterName;

	/** Material slot index on the static mesh to apply the dynamic material to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	int32 MaterialSlotIndex;

	/** Fallback resolution for the Render Target if no base texture can be extracted */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	int32 DefaultRenderTargetResolution;

protected:
	virtual void BeginPlay() override;

private:
	/** Extracts the base texture from the given material */
	UTexture* ExtractBaseTexture(UMaterialInterface* InMaterial, FName InParamName);

	/** Copies a source texture across the entire Render Target canvas */
	void CopyTextureToRenderTarget(UTexture* InTexture);

	UPROPERTY(Transient)
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* DynamicMaterial;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* BrushMID;

	UPROPERTY(Transient)
	UTexture* CachedBaseTexture;

	UPROPERTY()
	UStaticMeshComponent* TargetMesh;
};
