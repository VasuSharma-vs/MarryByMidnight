#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TexturePaintComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MARRYBYMIDNIGHT_API UTexturePaintComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTexturePaintComponent();

	// Set the target component and base material to initialize painting
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void SetupPainting(UStaticMeshComponent* InMeshComponent, UMaterialInterface* InBaseMaterial);

	// Perform a paint stroke at a specific UV coordinate
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void PaintAtUV(FVector2D UVCoordinate, float BrushSize, FLinearColor PaintColor);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* DynamicMaterial;

	UPROPERTY()
	UStaticMeshComponent* TargetMesh;
};
