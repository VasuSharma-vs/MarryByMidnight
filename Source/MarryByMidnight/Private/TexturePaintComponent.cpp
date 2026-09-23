#include "TexturePaintComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture.h"
#include "Components/StaticMeshComponent.h"

UTexturePaintComponent::UTexturePaintComponent()
	: BrushMaterial(nullptr)
	, BrushMaskTexture(nullptr)
	, TextureParameterName(FName("BaseTexture"))
	, MaterialSlotIndex(0)
	, DefaultRenderTargetResolution(1024)
	, RenderTarget(nullptr)
	, DynamicMaterial(nullptr)
	, BrushMID(nullptr)
	, CachedBaseTexture(nullptr)
	, TargetMesh(nullptr)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTexturePaintComponent::BeginPlay()
{
	Super::BeginPlay();
}

UTexture* UTexturePaintComponent::ExtractBaseTexture(UMaterialInterface* InMaterial, FName InParamName)
{
	if (!InMaterial)
	{
		return nullptr;
	}

	UTexture* ExtractedTexture = nullptr;

	// 1. Try specified parameter name
	if (!InParamName.IsNone())
	{
		InMaterial->GetTextureParameterValue(InParamName, ExtractedTexture);
		if (ExtractedTexture)
		{
			UE_LOG(LogTemp, Log, TEXT("ExtractBaseTexture: Found texture '%s' using parameter '%s'"), *ExtractedTexture->GetName(), *InParamName.ToString());
			return ExtractedTexture;
		}
	}

	// 2. Try common texture parameter conventions
	const TArray<FName> CommonParamNames = {
		FName("BaseTexture"),
		FName("PaintTexture"),
		FName("Texture"),
		FName("BaseColor"),
		FName("Albedo"),
		FName("Diffuse")
	};

	for (const FName& ParamName : CommonParamNames)
	{
		InMaterial->GetTextureParameterValue(ParamName, ExtractedTexture);
		if (ExtractedTexture)
		{
			UE_LOG(LogTemp, Log, TEXT("ExtractBaseTexture: Found texture '%s' using common parameter '%s'"), *ExtractedTexture->GetName(), *ParamName.ToString());
			return ExtractedTexture;
		}
	}

	// 3. Search all texture parameters defined on this material
	TArray<FMaterialParameterInfo> OutParameterInfo;
	TArray<FGuid> OutParameterGuids;
	InMaterial->GetAllTextureParameterInfo(OutParameterInfo, OutParameterGuids);
	for (const FMaterialParameterInfo& ParamInfo : OutParameterInfo)
	{
		InMaterial->GetTextureParameterValue(ParamInfo, ExtractedTexture);
		if (ExtractedTexture)
		{
			UE_LOG(LogTemp, Log, TEXT("ExtractBaseTexture: Found texture '%s' from parameter info '%s'"), *ExtractedTexture->GetName(), *ParamInfo.Name.ToString());
			return ExtractedTexture;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ExtractBaseTexture: No texture found in material '%s'."), *InMaterial->GetName());
	return nullptr;
}

void UTexturePaintComponent::CopyTextureToRenderTarget(UTexture* InTexture)
{
	if (!RenderTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("CopyTextureToRenderTarget: RenderTarget is null."));
		return;
	}

	if (InTexture)
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize;
		FDrawToRenderTargetContext DrawContext;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, DrawContext);
		if (Canvas)
		{
			Canvas->K2_DrawTexture(
				InTexture,
				FVector2D::ZeroVector,
				CanvasSize,
				FVector2D(0.f, 0.f),
				FVector2D(1.f, 1.f),
				FLinearColor::White,
				BLEND_Opaque
			);
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, DrawContext);
		UE_LOG(LogTemp, Log, TEXT("CopyTextureToRenderTarget: Successfully copied texture '%s' onto Render Target (%s)."),
			*InTexture->GetName(), *CanvasSize.ToString());
	}
	else
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, FLinearColor::White);
		UE_LOG(LogTemp, Log, TEXT("CopyTextureToRenderTarget: Cleared Render Target to white (no source texture)."));
	}
}

void UTexturePaintComponent::SetupPainting(
	UStaticMeshComponent* InMeshComponent,
	UMaterialInterface* InBaseMaterial,
	FName InTextureParameterName,
	int32 InMaterialSlotIndex,
	UTexture* InOverrideBaseTexture)
{
	UE_LOG(LogTemp, Log, TEXT("SetupPainting started. MeshComponent: %s, BaseMaterial: %s, ParamName: %s, Slot: %d"),
		InMeshComponent ? *InMeshComponent->GetName() : TEXT("NULL"),
		InBaseMaterial ? *InBaseMaterial->GetName() : TEXT("NULL"),
		*InTextureParameterName.ToString(),
		InMaterialSlotIndex);

	if (!InMeshComponent || !InBaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("SetupPainting aborted: MeshComponent or BaseMaterial is null!"));
		return;
	}

	TargetMesh = InMeshComponent;
	MaterialSlotIndex = InMaterialSlotIndex;

	if (!InTextureParameterName.IsNone())
	{
		TextureParameterName = InTextureParameterName;
	}

	// 1. Resolve base texture
	CachedBaseTexture = InOverrideBaseTexture ? InOverrideBaseTexture : ExtractBaseTexture(InBaseMaterial, TextureParameterName);

	// 2. Determine Render Target resolution
	int32 TargetWidth = DefaultRenderTargetResolution > 0 ? DefaultRenderTargetResolution : 1024;
	int32 TargetHeight = TargetWidth;

	if (CachedBaseTexture)
	{
		if (UTexture2D* Tex2D = Cast<UTexture2D>(CachedBaseTexture))
		{
			const int32 TexWidth = Tex2D->GetSizeX();
			const int32 TexHeight = Tex2D->GetSizeY();
			if (TexWidth > 0 && TexHeight > 0)
			{
				TargetWidth = FMath::Clamp(TexWidth, 128, 4096);
				TargetHeight = FMath::Clamp(TexHeight, 128, 4096);
			}
		}
	}

	// 3. Create or reallocate Render Target
	RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, TargetWidth, TargetHeight);
	if (!RenderTarget)
	{
		UE_LOG(LogTemp, Error, TEXT("SetupPainting: Failed to create %dx%d Render Target."), TargetWidth, TargetHeight);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("SetupPainting: Render Target created (%dx%d)."), TargetWidth, TargetHeight);

	// 4. Copy the base texture onto the Render Target
	CopyTextureToRenderTarget(CachedBaseTexture);

	// 5. Create Dynamic Material Instance from base material
	DynamicMaterial = UMaterialInstanceDynamic::Create(InBaseMaterial, this);
	if (!DynamicMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("SetupPainting: Failed to create Dynamic Material Instance from %s"), *InBaseMaterial->GetName());
		return;
	}

	// Bind the Render Target to both the requested parameter and fallback parameter names
	DynamicMaterial->SetTextureParameterValue(TextureParameterName, RenderTarget);
	if (TextureParameterName != FName("PaintTexture"))
	{
		DynamicMaterial->SetTextureParameterValue(FName("PaintTexture"), RenderTarget);
	}
	if (TextureParameterName != FName("BaseTexture"))
	{
		DynamicMaterial->SetTextureParameterValue(FName("BaseTexture"), RenderTarget);
	}

	// 6. Apply to mesh component at designated slot
	TargetMesh->SetMaterial(MaterialSlotIndex, DynamicMaterial);
	UE_LOG(LogTemp, Log, TEXT("SetupPainting: Applied dynamic material to %s at slot %d."), *TargetMesh->GetName(), MaterialSlotIndex);
}

void UTexturePaintComponent::PaintAtUV(FVector2D UVCoordinate, float BrushSize, FLinearColor PaintColor)
{
	if (!RenderTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("PaintAtUV aborted: RenderTarget is null! Did SetupPainting complete successfully?"));
		return;
	}

	if (UVCoordinate.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("PaintAtUV: UV is nearly (0,0). Ensure 'Find Collision UV' is enabled on your Line Trace node."));
	}

	// Initialize BrushMID if not yet cached
	if (!BrushMID)
	{
		UMaterialInterface* BaseBrushMat = BrushMaterial;
		if (!BaseBrushMat)
		{
			BaseBrushMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/TextureDraw/M_Brush.M_Brush")));
		}

		if (!BaseBrushMat)
		{
			UE_LOG(LogTemp, Error, TEXT("PaintAtUV: Failed to find or load BrushMaterial (/Game/TextureDraw/M_Brush)!"));
			return;
		}

		BrushMID = UMaterialInstanceDynamic::Create(BaseBrushMat, this);
		if (!BrushMID)
		{
			UE_LOG(LogTemp, Error, TEXT("PaintAtUV: Failed to create BrushMID from BrushMaterial!"));
			return;
		}

		UTexture2D* MaskTex = BrushMaskTexture;
		if (!MaskTex)
		{
			MaskTex = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, TEXT("/Game/TextureDraw/Bake_Mask_Image.Bake_Mask_Image")));
		}

		if (MaskTex)
		{
			BrushMID->SetTextureParameterValue(FName("BrushTexture"), MaskTex);
		}
	}

	// Forward paint color to the brush material
	BrushMID->SetVectorParameterValue(FName("PaintColor"), PaintColor);
	BrushMID->SetVectorParameterValue(FName("Color"), PaintColor);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext DrawContext;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, DrawContext);

	if (Canvas)
	{
		const FVector2D ScreenPos = (UVCoordinate * CanvasSize) - FVector2D(BrushSize * 0.5f, BrushSize * 0.5f);
		const FVector2D ScreenSize(BrushSize, BrushSize);

		Canvas->K2_DrawMaterial(BrushMID, ScreenPos, ScreenSize, FVector2D(0.f, 0.f), FVector2D(1.f, 1.f), 0.f, FVector2D(0.5f, 0.5f));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("PaintAtUV: Canvas is null after BeginDrawCanvasToRenderTarget!"));
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, DrawContext);
}

void UTexturePaintComponent::ResetCanvas(FLinearColor ClearColor, bool bRestoreBaseTexture)
{
	if (!RenderTarget)
	{
		return;
	}

	if (bRestoreBaseTexture && CachedBaseTexture)
	{
		CopyTextureToRenderTarget(CachedBaseTexture);
	}
	else
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, ClearColor);
	}
}
