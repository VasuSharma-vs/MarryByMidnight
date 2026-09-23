#include "TexturePaintComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"

UTexturePaintComponent::UTexturePaintComponent() 
{ 
	PrimaryComponentTick.bCanEverTick = false; 
}

void UTexturePaintComponent::BeginPlay() 
{ 
	Super::BeginPlay(); 
}

void UTexturePaintComponent::SetupPainting(UStaticMeshComponent* InMeshComponent, UMaterialInterface* InBaseMaterial)
{
	UE_LOG(LogTemp, Log, TEXT("SetupPainting started. MeshComponent: %s, BaseMaterial: %s"), 
		InMeshComponent ? *InMeshComponent->GetName() : TEXT("NULL"), 
		InBaseMaterial ? *InBaseMaterial->GetName() : TEXT("NULL"));

	if (!InMeshComponent || !InBaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("SetupPainting aborted: MeshComponent or BaseMaterial is null!"));
		return;
	}

	TargetMesh = InMeshComponent;

	// 1. Create Render Target
	RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, 1024, 1024);
	if (!RenderTarget)
	{
		UE_LOG(LogTemp, Error, TEXT("SetupPainting: Failed to create 1024x1024 Render Target."));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("SetupPainting: Render Target created successfully. Name: %s"), *RenderTarget->GetName());
	}

	// 2. Create DMI
	DynamicMaterial = UMaterialInstanceDynamic::Create(InBaseMaterial, this);
	if (!DynamicMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("SetupPainting: Failed to create Dynamic Material Instance from %s"), *InBaseMaterial->GetName());
	}
	else
	{
		DynamicMaterial->SetTextureParameterValue(FName("PaintTexture"), RenderTarget);
		UE_LOG(LogTemp, Log, TEXT("SetupPainting: Dynamic Material Instance created and PaintTexture set to Render Target."));
	}
	
	// 3. Apply to mesh
	if (TargetMesh)
	{
		TargetMesh->SetMaterial(0, DynamicMaterial);
		UE_LOG(LogTemp, Log, TEXT("SetupPainting: Set material on %s at index 0 to %s"), *TargetMesh->GetName(), *DynamicMaterial->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SetupPainting: TargetMesh is null during material application. This should not happen."));
	}
}

void UTexturePaintComponent::PaintAtUV(FVector2D UV, float BrushSize, FLinearColor Color)
{
	UE_LOG(LogTemp, Log, TEXT("PaintAtUV called. UV: %s, BrushSize: %f, Color: %s"), *UV.ToString(), BrushSize, *Color.ToString());

	if (!RenderTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("PaintAtUV aborted: RenderTarget is null! Did SetupPainting complete successfully?"));
		return;
	}

	if (UV.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("PaintAtUV: UV is nearly (0,0). Note: (0,0) is also the fallback value when Line Trace 'Find Collision UV' is not enabled or fails. Ensure 'Find Collision UV' is set to TRUE on your line trace node."));
	}

	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, Size, Context);

	if (Canvas)
	{
		UE_LOG(LogTemp, Log, TEXT("PaintAtUV: BeginDrawCanvasToRenderTarget succeeded. Canvas Size: %s"), *Size.ToString());

		static UMaterialInterface* BrushMaterialBase = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/TextureDraw/M_Brush.M_Brush")));
		if (BrushMaterialBase)
		{
			UMaterialInstanceDynamic* BrushMID = UMaterialInstanceDynamic::Create(BrushMaterialBase, this);
			if (BrushMID)
			{
				static UTexture2D* BrushTexture = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, TEXT("/Game/TextureDraw/Bake_Mask_Image.Bake_Mask_Image")));
				if (BrushTexture)
				{
					BrushMID->SetTextureParameterValue(FName("BrushTexture"), BrushTexture);
					UE_LOG(LogTemp, Log, TEXT("PaintAtUV: Loaded BrushTexture successfully and set to BrushMID."));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("PaintAtUV: Failed to load BrushTexture Bake_Mask_Image at path: /Game/TextureDraw/Bake_Mask_Image"));
				}
				
				// Calculate draw location
				FVector2D ScreenPos = (UV * Size) - FVector2D(BrushSize / 2.0f, BrushSize / 2.0f);
				FVector2D ScreenSize(BrushSize, BrushSize);
				
				UE_LOG(LogTemp, Log, TEXT("PaintAtUV: Drawing at calculated ScreenPos: %s, ScreenSize: %s"), *ScreenPos.ToString(), *ScreenSize.ToString());

				// Draw onto the canvas
				Canvas->K2_DrawMaterial(BrushMID, ScreenPos, ScreenSize, FVector2D(0.f, 0.f), FVector2D(1.f, 1.f), 0.f, FVector2D(0.5f, 0.5f));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("PaintAtUV: Failed to create Dynamic Material Instance of BrushMaterialBase!"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("PaintAtUV: Failed to load BrushMaterialBase at path: /Game/TextureDraw/M_Brush"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("PaintAtUV: Canvas is null after BeginDrawCanvasToRenderTarget!"));
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	UE_LOG(LogTemp, Log, TEXT("PaintAtUV completed EndDrawCanvasToRenderTarget."));
}
