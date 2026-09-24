#include "TexturePaintComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Async/Async.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "Containers/Ticker.h"
#include "Math/Float16Color.h"
#include "PixelFormat.h"

UTexturePaintComponent::UTexturePaintComponent()
	: bAutoInitializeOnBeginPlay(true)
	, bEnableScreenLogging(false)
	, BrushRotation(0.0f)
	, MinOrientSpeed(0.01f)
	, MinRotationInterpSpeed(3.0f)
	, MaxRotationInterpSpeed(30.0f)
	, MaxSpeedThreshold(1.5f)
	, BrushMaterial(nullptr)
	, BrushMaskTexture(nullptr)
	, TextureParameterName(FName("PaintTexture"))
	, MaterialSlotIndex(0)
	, DefaultRenderTargetResolution(1024)
	, LastPaintedPercentage(0.0f)
	, bIsCalculatingPercentage(false)
	, CanvasBaseColor(FLinearColor::White)
	, LastPaintedUV(FVector2D::ZeroVector)
	, bHasLastPaintedUV(false)
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

	// Explicitly ensure debug logging is disabled on BeginPlay
	bEnableScreenLogging = false;

	if (bAutoInitializeOnBeginPlay && !RenderTarget)
	{
		AutoInitialize();
	}
}

void UTexturePaintComponent::SetDebugLogging(bool bEnable)
{
	SetEnableScreenLogging(bEnable);
}

void UTexturePaintComponent::EnableDebug()
{
	SetEnableScreenLogging(true);
}

void UTexturePaintComponent::DisableDebug()
{
	SetEnableScreenLogging(false);
}

bool UTexturePaintComponent::ToggleDebug()
{
	return ToggleScreenLogging();
}

bool UTexturePaintComponent::ToggleScreenLogging()
{
	bEnableScreenLogging = !bEnableScreenLogging;
	const FString StatusStr = bEnableScreenLogging ? TEXT("ENABLED") : TEXT("DISABLED");
	const FColor StatusColor = bEnableScreenLogging ? FColor::Green : FColor::Red;
	UE_LOG(LogTemp, Log, TEXT("[TexturePaintComponent] Debug logging %s."), *StatusStr);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(999911, 3.0f, StatusColor, FString::Printf(TEXT("[TexturePaintComponent] Debug Logging: %s"), *StatusStr));
	}
	return bEnableScreenLogging;
}

void UTexturePaintComponent::SetEnableScreenLogging(bool bEnable)
{
	bEnableScreenLogging = bEnable;
	const FString StatusStr = bEnableScreenLogging ? TEXT("ENABLED") : TEXT("DISABLED");
	const FColor StatusColor = bEnableScreenLogging ? FColor::Green : FColor::Red;
	UE_LOG(LogTemp, Log, TEXT("[TexturePaintComponent] Debug logging set to %s."), *StatusStr);
	if (GEngine && bEnableScreenLogging)
	{
		GEngine->AddOnScreenDebugMessage(999911, 3.0f, StatusColor, FString::Printf(TEXT("[TexturePaintComponent] Debug Logging: %s"), *StatusStr));
	}
}

void UTexturePaintComponent::PrintScreenLog(const FString& Message, FColor Color, float Duration, uint64 Key)
{
	if (bEnableScreenLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(Key, Duration, Color, Message);
		}
	}
}

void UTexturePaintComponent::PrintScreenWarning(const FString& Message, float Duration, uint64 Key)
{
	if (bEnableScreenLogging)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(Key, Duration, FColor::Yellow, FString::Printf(TEXT("[Warning] %s"), *Message));
		}
	}
}

void UTexturePaintComponent::PrintScreenError(const FString& Message, float Duration, uint64 Key)
{
	UE_LOG(LogTemp, Error, TEXT("%s"), *Message);
	if (bEnableScreenLogging && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Duration, FColor::Red, FString::Printf(TEXT("[Error] %s"), *Message));
	}
}

bool UTexturePaintComponent::AutoInitialize()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		PrintScreenWarning(TEXT("AutoInitialize failed: Component has no Owner actor!"), 5.0f, 987650);
		return false;
	}

	UStaticMeshComponent* FoundMesh = TargetMesh;
	if (!FoundMesh)
	{
		FoundMesh = Owner->FindComponentByClass<UStaticMeshComponent>();
	}

	if (!FoundMesh)
	{
		PrintScreenWarning(FString::Printf(TEXT("AutoInitialize on '%s': No StaticMeshComponent found!"), *Owner->GetName()), 5.0f, 987651);
		return false;
	}

	UMaterialInterface* BaseMat = FoundMesh->GetMaterial(MaterialSlotIndex);
	if (!BaseMat)
	{
		BaseMat = FoundMesh->GetMaterial(0);
		if (BaseMat)
		{
			MaterialSlotIndex = 0;
		}
	}

	if (!BaseMat)
	{
		PrintScreenWarning(FString::Printf(TEXT("AutoInitialize on '%s': StaticMesh '%s' has no material at slot %d!"),
			*Owner->GetName(), *FoundMesh->GetName(), MaterialSlotIndex), 5.0f, 987652);
		return false;
	}

	PrintScreenLog(FString::Printf(TEXT("TexturePaintComponent: Auto-initializing on '%s' (Mesh: '%s', Material: '%s')..."),
		*Owner->GetName(), *FoundMesh->GetName(), *BaseMat->GetName()), FColor::Cyan, 4.0f);

	SetupPainting(FoundMesh, BaseMat, TextureParameterName, MaterialSlotIndex);
	return RenderTarget != nullptr;
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
			PrintScreenLog(FString::Printf(TEXT("ExtractBaseTexture: Found '%s' using param '%s'"), *ExtractedTexture->GetName(), *InParamName.ToString()), FColor::White, 3.0f);
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
			PrintScreenLog(FString::Printf(TEXT("ExtractBaseTexture: Found '%s' using common param '%s'"), *ExtractedTexture->GetName(), *ParamName.ToString()), FColor::White, 3.0f);
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
			PrintScreenLog(FString::Printf(TEXT("ExtractBaseTexture: Found '%s' from param info '%s'"), *ExtractedTexture->GetName(), *ParamInfo.Name.ToString()), FColor::White, 3.0f);
			return ExtractedTexture;
		}
	}

	PrintScreenWarning(FString::Printf(TEXT("ExtractBaseTexture: No texture parameter found in material '%s'."), *InMaterial->GetName()), 4.0f);
	return nullptr;
}

void UTexturePaintComponent::CopyTextureToRenderTarget(UTexture* InTexture)
{
	if (!RenderTarget)
	{
		PrintScreenWarning(TEXT("CopyTextureToRenderTarget: RenderTarget is null!"), 4.0f);
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

		PrintScreenLog(FString::Printf(TEXT("Copied base texture '%s' to Render Target (%0.0fx%0.0f)."),
			*InTexture->GetName(), CanvasSize.X, CanvasSize.Y), FColor::Green, 4.0f);
	}
	else
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, FLinearColor::White);
		PrintScreenLog(TEXT("No base texture found; cleared Render Target to white."), FColor::Yellow, 4.0f);
	}
}

void UTexturePaintComponent::SetupPainting(
	UStaticMeshComponent* InMeshComponent,
	UMaterialInterface* InBaseMaterial,
	FName InTextureParameterName,
	int32 InMaterialSlotIndex,
	UTexture* InOverrideBaseTexture)
{
	if (!InMeshComponent || !InBaseMaterial)
	{
		PrintScreenError(TEXT("SetupPainting aborted: MeshComponent or BaseMaterial is null!"), 6.0f);
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

	// 3. Create or reallocate Render Target (Use RTF_RGBA8 for 8-bit RGBA and direct PF_B8G8R8A8 memory layout)
	RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, TargetWidth, TargetHeight, RTF_RGBA8);
	if (!RenderTarget)
	{
		PrintScreenError(FString::Printf(TEXT("SetupPainting: Failed to create %dx%d Render Target!"), TargetWidth, TargetHeight), 6.0f);
		return;
	}

	PrintScreenLog(FString::Printf(TEXT("SetupPainting: Created %dx%d Render Target. Base texture: '%s'"),
		TargetWidth, TargetHeight, CachedBaseTexture ? *CachedBaseTexture->GetName() : TEXT("None (Cleared to white)")),
		FColor::Green, 5.0f);

	// 4. Copy the base texture onto the Render Target
	CopyTextureToRenderTarget(CachedBaseTexture);

	// 5. Create Dynamic Material Instance from base material
	DynamicMaterial = UMaterialInstanceDynamic::Create(InBaseMaterial, this);
	if (!DynamicMaterial)
	{
		PrintScreenError(FString::Printf(TEXT("SetupPainting: Failed to create Dynamic Material Instance from '%s'!"), *InBaseMaterial->GetName()), 6.0f);
		return;
	}

	// Bind the Render Target to the designated texture parameter
	DynamicMaterial->SetTextureParameterValue(TextureParameterName, RenderTarget);

	// 6. Apply to mesh component at designated slot
	TargetMesh->SetMaterial(MaterialSlotIndex, DynamicMaterial);
	PrintScreenLog(FString::Printf(TEXT("SetupPainting: Dynamic Material applied to %s (Slot %d, Param: '%s'). Ready to paint!"),
		*TargetMesh->GetName(), MaterialSlotIndex, *TextureParameterName.ToString()), FColor::Green, 5.0f);
}

void UTexturePaintComponent::SetTextureParameterName(FName InParameterName)
{
	if (!InParameterName.IsNone())
	{
		TextureParameterName = InParameterName;
		if (DynamicMaterial && RenderTarget)
		{
			DynamicMaterial->SetTextureParameterValue(TextureParameterName, RenderTarget);
			PrintScreenLog(FString::Printf(TEXT("SetTextureParameterName: Target parameter updated to '%s'."), *TextureParameterName.ToString()), FColor::Green, 3.0f);
		}
	}
}

void UTexturePaintComponent::PaintAtUV(FVector2D UVCoordinate, float BrushSize, FLinearColor PaintColor, float InRotationDegrees)
{
	if (!RenderTarget)
	{
		// Attempt on-demand auto initialization
		if (!AutoInitialize())
		{
			PrintScreenWarning(TEXT("PaintAtUV aborted: RenderTarget is null! Ensure SetupPainting is called or actor has a StaticMeshComponent with a material."), 4.0f, 987654);
			return;
		}
	}

	if (UVCoordinate.IsNearlyZero())
	{
		PrintScreenWarning(TEXT("PaintAtUV: UV is near (0,0). Ensure 'Find Collision UV' is enabled on your Line Trace node!"), 4.0f, 987655);
	}

	// Invalidate BrushMID if BrushMaterial was changed externally or does not match
	if (BrushMID && BrushMaterial && BrushMID->Parent != BrushMaterial)
	{
		BrushMID = nullptr;
	}

	// Initialize BrushMID if not yet cached
	if (!BrushMID)
	{
		UMaterialInterface* BaseBrushMat = BrushMaterial;
		if (!BaseBrushMat)
		{
			BaseBrushMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/TextureDraw/M_Brush.M_Brush")));
		}

		if (BaseBrushMat)
		{
			BrushMID = UMaterialInstanceDynamic::Create(BaseBrushMat, this);
		}

		if (!BrushMID)
		{
			PrintScreenWarning(TEXT("PaintAtUV: Could not create BrushMID from BrushMaterial; falling back to direct texture draw."), 4.0f);
		}
	}

	// Resolve the active brush texture from component property or default Bake_Mask_Image
	UTexture* ActiveBrushTexture = BrushMaskTexture;
	if (!ActiveBrushTexture)
	{
		ActiveBrushTexture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, TEXT("/Game/TextureDraw/Bake_Mask_Image.Bake_Mask_Image")));
	}

	// Determine rotation angle (Override parameter if specified, otherwise component's BrushRotation)
	const float EffectiveRotation = (InRotationDegrees > -900.0f) ? InRotationDegrees : BrushRotation;

	// Apply the active brush texture, rotation, and paint color to the brush dynamic material
	if (BrushMID)
	{
		if (ActiveBrushTexture)
		{
			UTexture* CurrentBoundTexture = nullptr;
			BrushMID->GetTextureParameterValue(FName("BrushTexture"), CurrentBoundTexture);
			if (CurrentBoundTexture != ActiveBrushTexture)
			{
				BrushMID->SetTextureParameterValue(FName("BrushTexture"), ActiveBrushTexture);
				BrushMID->SetTextureParameterValue(FName("BrushMask"), ActiveBrushTexture);
				BrushMID->SetTextureParameterValue(FName("Mask"), ActiveBrushTexture);
				BrushMID->SetTextureParameterValue(FName("Texture"), ActiveBrushTexture);
			}
		}

		BrushMID->SetVectorParameterValue(FName("PaintColor"), PaintColor);
		BrushMID->SetVectorParameterValue(FName("Color"), PaintColor);
		BrushMID->SetVectorParameterValue(FName("Tint"), PaintColor);
		BrushMID->SetVectorParameterValue(FName("BrushColor"), PaintColor);

		// Also pass rotation to material in case it has a CustomRotator node
		BrushMID->SetScalarParameterValue(FName("BrushRotation"), EffectiveRotation);
		BrushMID->SetScalarParameterValue(FName("Rotation"), EffectiveRotation);
		BrushMID->SetScalarParameterValue(FName("Angle"), EffectiveRotation);
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext DrawContext;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, DrawContext);

	if (Canvas)
	{
		const FVector2D ScreenPos = (UVCoordinate * CanvasSize) - FVector2D(BrushSize * 0.5f, BrushSize * 0.5f);
		const FVector2D ScreenSize(BrushSize, BrushSize);

		if (BrushMID)
		{
			Canvas->K2_DrawMaterial(BrushMID, ScreenPos, ScreenSize, FVector2D(0.f, 0.f), FVector2D(1.f, 1.f), EffectiveRotation, FVector2D(0.5f, 0.5f));
		}
		else if (ActiveBrushTexture)
		{
			// Fallback: draw mask texture directly tinted by PaintColor with translucent blending and rotation
			Canvas->K2_DrawTexture(ActiveBrushTexture, ScreenPos, ScreenSize, FVector2D(0.f, 0.f), FVector2D(1.f, 1.f), PaintColor, BLEND_Translucent, EffectiveRotation, FVector2D(0.5f, 0.5f));
		}

		// Live single-line status on viewport with color and rotation info
		PrintScreenLog(FString::Printf(TEXT("Painting at UV (%0.2f, %0.2f) | Rot: %0.0f deg | Color: (R=%0.1f, G=%0.1f, B=%0.1f) | Size: %0.0f"),
			UVCoordinate.X, UVCoordinate.Y, EffectiveRotation, PaintColor.R, PaintColor.G, PaintColor.B, BrushSize), FColor::Cyan, 1.0f, 987656);
	}
	else
	{
		PrintScreenError(TEXT("PaintAtUV: Canvas is null after BeginDrawCanvasToRenderTarget!"), 4.0f);
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, DrawContext);

	// Update last painted UV coordinate for automatic direction tracking
	LastPaintedUV = UVCoordinate;
	bHasLastPaintedUV = true;
}

void UTexturePaintComponent::ResetCanvas(FLinearColor ClearColor, bool bRestoreBaseTexture)
{
	bHasLastPaintedUV = false;
	LastPaintedUV = FVector2D::ZeroVector;
	LastPaintedPercentage = 0.0f;
	CanvasBaseColor = ClearColor;
	bIsCalculatingPercentage = false;

	if (!RenderTarget)
	{
		return;
	}

	if (bRestoreBaseTexture && CachedBaseTexture)
	{
		CopyTextureToRenderTarget(CachedBaseTexture);
		CanvasBaseColor = (ClearColor != FLinearColor::White) ? ClearColor : FLinearColor::Black;
		PrintScreenLog(TEXT("ResetCanvas: Restored canvas to original base texture."), FColor::Green, 3.0f);
	}
	else
	{
		CanvasBaseColor = ClearColor;
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, ClearColor);
		PrintScreenLog(TEXT("ResetCanvas: Cleared canvas color."), FColor::Yellow, 3.0f);
	}
}

void UTexturePaintComponent::SetBrushRotation(float InRotationDegrees)
{
	BrushRotation = FMath::Fmod(InRotationDegrees, 360.0f);
	if (BrushRotation < 0.0f)
	{
		BrushRotation += 360.0f;
	}
}

void UTexturePaintComponent::SetBrushRotationFromDirection2D(FVector2D Direction, float AngleOffsetDegrees)
{
	if (Direction.IsNearlyZero())
	{
		return; // Maintain current rotation when movement stops
	}

	// Calculate angle in degrees: (1, 0) = 0 deg, (0, 1) = 90 deg (downwards in UV space)
	const float AngleRadians = FMath::Atan2(Direction.Y, Direction.X);
	const float AngleDegrees = FMath::RadiansToDegrees(AngleRadians);
	SetBrushRotation(AngleDegrees + AngleOffsetDegrees);
}

void UTexturePaintComponent::SetBrushRotationFromDirection(FVector Direction, float AngleOffsetDegrees)
{
	if (Direction.IsNearlyZero())
	{
		return;
	}

	// Project to 2D plane (X, Y)
	const float AngleRadians = FMath::Atan2(Direction.Y, Direction.X);
	const float AngleDegrees = FMath::RadiansToDegrees(AngleRadians);
	SetBrushRotation(AngleDegrees + AngleOffsetDegrees);
}

void UTexturePaintComponent::PaintAtUVWithDirection(FVector2D UVCoordinate, FVector2D Direction, float BrushSize, FLinearColor PaintColor, float AngleOffsetDegrees)
{
	if (!Direction.IsNearlyZero(1e-4f))
	{
		SetBrushRotationFromDirection2D(Direction, AngleOffsetDegrees);
	}

	PaintAtUV(UVCoordinate, BrushSize, PaintColor, BrushRotation);
}

void UTexturePaintComponent::PaintAtUVAutoOrient(FVector2D UVCoordinate, float BrushSize, FLinearColor PaintColor, float AngleOffsetDegrees, float DeltaTime)
{
	const float ActualDeltaTime = (DeltaTime > 0.0f) ? DeltaTime : (GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f);

	if (bHasLastPaintedUV && ActualDeltaTime > 1e-5f)
	{
		const FVector2D DeltaUV = UVCoordinate - LastPaintedUV;
		const float DeltaDist = DeltaUV.Size();
		const float Speed = DeltaDist / ActualDeltaTime;

		// Only orient if moving at or above minimum speed; if stopped, don't orient!
		if (Speed >= MinOrientSpeed && DeltaDist > 1e-5f)
		{
			// Target angle along movement direction in degrees (atan2: X=right, Y=down in UV space)
			const float TargetAngle = FMath::RadiansToDegrees(FMath::Atan2(DeltaUV.Y, DeltaUV.X)) + AngleOffsetDegrees;

			// Scale interpolation speed: slow movement = slow orient, fast movement = quick orient
			const float SpeedAlpha = FMath::Clamp((Speed - MinOrientSpeed) / FMath::Max(MaxSpeedThreshold - MinOrientSpeed, 1e-4f), 0.0f, 1.0f);
			const float CurrentInterpSpeed = FMath::Lerp(MinRotationInterpSpeed, MaxRotationInterpSpeed, SpeedAlpha);

			// Smooth angle interpolation with proper 360-degree wrapping
			const FRotator CurrentRot(0.0f, BrushRotation, 0.0f);
			const FRotator TargetRot(0.0f, TargetAngle, 0.0f);
			const FRotator InterpRot = FMath::RInterpTo(CurrentRot, TargetRot, ActualDeltaTime, CurrentInterpSpeed);

			BrushRotation = FMath::Fmod(InterpRot.Yaw, 360.0f);
			if (BrushRotation < 0.0f)
			{
				BrushRotation += 360.0f;
			}
		}
	}

	PaintAtUV(UVCoordinate, BrushSize, PaintColor, BrushRotation);
}

void UTexturePaintComponent::SetBrushMaterial(UMaterialInterface* InBrushMaterial)
{
	BrushMaterial = InBrushMaterial;
	BrushMID = nullptr; // Invalidate cached MID

	if (BrushMaterial)
	{
		BrushMID = UMaterialInstanceDynamic::Create(BrushMaterial, this);
		if (BrushMID)
		{
			UTexture* MaskTex = BrushMaskTexture ? BrushMaskTexture : Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, TEXT("/Game/TextureDraw/Bake_Mask_Image.Bake_Mask_Image")));
			if (MaskTex)
			{
				BrushMID->SetTextureParameterValue(FName("BrushTexture"), MaskTex);
				BrushMID->SetTextureParameterValue(FName("BrushMask"), MaskTex);
				BrushMID->SetTextureParameterValue(FName("Mask"), MaskTex);
				BrushMID->SetTextureParameterValue(FName("Texture"), MaskTex);
			}
			PrintScreenLog(FString::Printf(TEXT("SetBrushMaterial: Switched brush material to '%s'."), *BrushMaterial->GetName()), FColor::Green, 3.0f);
		}
	}
	else
	{
		PrintScreenLog(TEXT("SetBrushMaterial: Reset to default brush material."), FColor::Yellow, 3.0f);
	}
}

void UTexturePaintComponent::SetBrushTexture(UTexture* InBrushTexture)
{
	BrushMaskTexture = InBrushTexture;
	if (BrushMID && BrushMaskTexture)
	{
		BrushMID->SetTextureParameterValue(FName("BrushTexture"), BrushMaskTexture);
		BrushMID->SetTextureParameterValue(FName("BrushMask"), BrushMaskTexture);
		BrushMID->SetTextureParameterValue(FName("Mask"), BrushMaskTexture);
		BrushMID->SetTextureParameterValue(FName("Texture"), BrushMaskTexture);
		PrintScreenLog(FString::Printf(TEXT("SetBrushTexture: Updated brush texture to '%s'."), *BrushMaskTexture->GetName()), FColor::Green, 3.0f);
	}
}

void UTexturePaintComponent::SetBrushMaskTexture(UTexture* InBrushMaskTexture)
{
	SetBrushTexture(InBrushMaskTexture);
}

float UTexturePaintComponent::CalculatePaintedPercentage(FLinearColor TargetColor, float ColorTolerance, int32 SampleStep)
{
	if (!RenderTarget)
	{
		PrintScreenWarning(TEXT("CalculatePaintedPercentage: RenderTarget is null!"), 3.0f);
		return 0.0f;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		PrintScreenWarning(TEXT("CalculatePaintedPercentage: RenderTarget resource is null!"), 3.0f);
		return 0.0f;
	}

	TArray<FColor> OutPixels;
	if (!RTResource->ReadPixels(OutPixels))
	{
		PrintScreenWarning(TEXT("CalculatePaintedPercentage: Failed to read pixels from Render Target!"), 3.0f);
		return 0.0f;
	}

	const int32 TotalPixelCount = OutPixels.Num();
	if (TotalPixelCount <= 0)
	{
		return 0.0f;
	}

	const int32 SafeStep = FMath::Clamp(SampleStep, 1, 16);
	const int32 Width = RenderTarget->SizeX;
	const int32 Height = RenderTarget->SizeY;

	int32 TotalSampled = 0;
	int32 PaintedSampled = 0;

	const bool bMatchSpecificColor = (TargetColor.A > 0.01f);
	const FColor TargetFColor = TargetColor.ToFColor(true);
	const FColor BaseFColor = CanvasBaseColor.ToFColor(true);

	// Per-channel tolerance threshold: ColorTolerance 0.0 = exact match, 1.0 = any color passes
	// e.g. ColorTolerance=0.1 -> threshold=25  (strict, only near-pure red counts)
	//      ColorTolerance=0.5 -> threshold=127 (medium, dark/translucent red counts)
	//      ColorTolerance=0.8 -> threshold=204 (loose, almost anything counts)
	const int32 ChannelThreshold = FMath::Clamp(FMath::RoundToInt(ColorTolerance * 255.0f), 0, 255);

	// Euclidean fallback distance used for "any paint" mode (TargetColor.A == 0)
	const float MaxDist = ColorTolerance * 441.67f;
	const float MaxDistSq = MaxDist * MaxDist;

	for (int32 Y = 0; Y < Height; Y += SafeStep)
	{
		for (int32 X = 0; X < Width; X += SafeStep)
		{
			const int32 Index = Y * Width + X;
			if (Index < TotalPixelCount)
			{
				const FColor& Pixel = OutPixels[Index];
				TotalSampled++;

				if (bMatchSpecificColor)
				{
					// Per-channel difference check:
					// Split pixel RGB and target RGB, subtract each channel.
					// If the absolute difference on ALL 3 channels is within threshold -> painted.
					const int32 DiffR = FMath::Abs((int32)Pixel.R - (int32)TargetFColor.R);
					const int32 DiffG = FMath::Abs((int32)Pixel.G - (int32)TargetFColor.G);
					const int32 DiffB = FMath::Abs((int32)Pixel.B - (int32)TargetFColor.B);

					const bool bRMatch = (DiffR <= ChannelThreshold);
					const bool bGMatch = (DiffG <= ChannelThreshold);
					const bool bBMatch = (DiffB <= ChannelThreshold);

					if (bRMatch && bGMatch && bBMatch)
					{
						PaintedSampled++;
					}
				}
				else
				{
					// Count any pixel that differs from the clean canvas base color
					const float DistFromBaseSq = FMath::Square((float)(Pixel.R - BaseFColor.R)) +
					                             FMath::Square((float)(Pixel.G - BaseFColor.G)) +
					                             FMath::Square((float)(Pixel.B - BaseFColor.B));
					if (DistFromBaseSq > MaxDistSq)
					{
						PaintedSampled++;
					}
				}
			}
		}
	}

	LastPaintedPercentage = (TotalSampled > 0) ? ((float)PaintedSampled / (float)TotalSampled) * 100.0f : 0.0f;

	PrintScreenLog(FString::Printf(TEXT("CalculatePaintedPercentage: %0.1f%% (%d / %d sampled)"),
		LastPaintedPercentage, PaintedSampled, TotalSampled), FColor::Green, 3.0f);

	return LastPaintedPercentage;
}

void UTexturePaintComponent::CalculatePaintedPercentageAsync(FLinearColor TargetColor, float ColorTolerance, int32 SampleStep)
{
	CalculatePaintedPercentageInternal(TargetColor, ColorTolerance, SampleStep, nullptr);
}

static void DecodeReadbackPixels(
	const void* MappedMemory,
	EPixelFormat Format,
	int32 Width,
	int32 Height,
	int32 RowPitchInPixels,
	TArray<FColor>& OutPixels)
{
	OutPixels.SetNumUninitialized(Width * Height);

	if (Format == PF_B8G8R8A8)
	{
		const FColor* SourceData = static_cast<const FColor*>(MappedMemory);
		if (RowPitchInPixels == Width)
		{
			FMemory::Memcpy(OutPixels.GetData(), SourceData, Width * Height * sizeof(FColor));
		}
		else
		{
			for (int32 Y = 0; Y < Height; ++Y)
			{
				FMemory::Memcpy(
					&OutPixels[Y * Width],
					SourceData + (Y * RowPitchInPixels),
					Width * sizeof(FColor)
				);
			}
		}
	}
	else if (Format == PF_R8G8B8A8)
	{
		const uint8* SrcBytes = static_cast<const uint8*>(MappedMemory);
		const int32 RowBytePitch = RowPitchInPixels * 4;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			const uint8* RowPtr = SrcBytes + (Y * RowBytePitch);
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 SrcIdx = X * 4;
				OutPixels[Y * Width + X] = FColor(RowPtr[SrcIdx + 0], RowPtr[SrcIdx + 1], RowPtr[SrcIdx + 2], RowPtr[SrcIdx + 3]);
			}
		}
	}
	else if (Format == PF_FloatRGBA)
	{
		const FFloat16Color* SrcHalf = static_cast<const FFloat16Color*>(MappedMemory);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			const FFloat16Color* RowPtr = SrcHalf + (Y * RowPitchInPixels);
			for (int32 X = 0; X < Width; ++X)
			{
				const FFloat16Color& HalfCol = RowPtr[X];
				const uint8 R = (uint8)FMath::Clamp(FMath::RoundToInt(HalfCol.R.GetFloat() * 255.0f), 0, 255);
				const uint8 G = (uint8)FMath::Clamp(FMath::RoundToInt(HalfCol.G.GetFloat() * 255.0f), 0, 255);
				const uint8 B = (uint8)FMath::Clamp(FMath::RoundToInt(HalfCol.B.GetFloat() * 255.0f), 0, 255);
				const uint8 A = (uint8)FMath::Clamp(FMath::RoundToInt(HalfCol.A.GetFloat() * 255.0f), 0, 255);
				OutPixels[Y * Width + X] = FColor(R, G, B, A);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[TexturePaintComponent] DecodeReadbackPixels: Unhandled pixel format %d (%s), attempting raw copy"),
			(int32)Format, GPixelFormats[Format].Name);
		const FColor* SourceData = static_cast<const FColor*>(MappedMemory);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			FMemory::Memcpy(
				&OutPixels[Y * Width],
				SourceData + (Y * RowPitchInPixels),
				Width * sizeof(FColor)
			);
		}
	}
}

void UTexturePaintComponent::CalculatePaintedPercentageInternal(
	FLinearColor TargetColor,
	float ColorTolerance,
	int32 SampleStep,
	TFunction<void(float)> OnCompleteCallback)
{
	if (bIsCalculatingPercentage)
	{
		PrintScreenWarning(TEXT("CalculatePaintedPercentageAsync: A calculation is already running in the background!"), 2.0f);
		if (OnCompleteCallback)
		{
			OnCompleteCallback(LastPaintedPercentage);
		}
		return;
	}

	if (!RenderTarget)
	{
		PrintScreenWarning(TEXT("CalculatePaintedPercentageAsync: RenderTarget is null!"), 3.0f);
		if (OnCompleteCallback)
		{
			OnCompleteCallback(0.0f);
		}
		return;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		PrintScreenWarning(TEXT("CalculatePaintedPercentageAsync: RenderTarget resource is null!"), 3.0f);
		if (OnCompleteCallback)
		{
			OnCompleteCallback(0.0f);
		}
		return;
	}

	FTextureRHIRef TextureRHI = RTResource->GetTextureRHI();
	if (!TextureRHI)
	{
		PrintScreenWarning(TEXT("CalculatePaintedPercentageAsync: Texture RHI resource is null!"), 3.0f);
		if (OnCompleteCallback)
		{
			OnCompleteCallback(0.0f);
		}
		return;
	}

	const int32 SafeStep = FMath::Clamp(SampleStep, 1, 16);
	const int32 Width = RenderTarget->SizeX;
	const int32 Height = RenderTarget->SizeY;
	const FColor BaseFColor = CanvasBaseColor.ToFColor(true);
	const EPixelFormat PixelFormat = TextureRHI->GetFormat();

	bIsCalculatingPercentage = true;

	// 1. Create asynchronous GPU texture readback without stalling Game Thread
	TSharedPtr<FRHIGPUTextureReadback> Readback = MakeShared<FRHIGPUTextureReadback>(FName("PaintCanvasAsyncReadback"));

	// 2. Enqueue the GPU-to-staging copy on Render Thread with explicit full-dimension resolve rect
	ENQUEUE_RENDER_COMMAND(PaintCanvasReadbackCopy)(
		[Readback, TextureRHI, Width, Height](FRHICommandListImmediate& RHICmdList)
		{
			Readback->EnqueueCopy(RHICmdList, TextureRHI, FResolveRect(0, 0, Width, Height));
		}
	);

	TWeakObjectPtr<UTexturePaintComponent> WeakThis(this);

	// 3. Poll each frame on Game Thread until GPU marks the fence as ready (takes ~1-2 frames, 0ms stall)
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis, Readback, Width, Height, SafeStep, TargetColor, ColorTolerance, BaseFColor, PixelFormat, OnCompleteCallback = MoveTemp(OnCompleteCallback)](float DeltaTime) mutable -> bool
		{
			if (!WeakThis.IsValid())
			{
				return false; // Component destroyed, stop ticking
			}

			if (!Readback.IsValid() || !Readback->IsReady())
			{
				return true; // Still pending GPU execution, check again next frame
			}

			// GPU fence completed! Enqueue Lock and Extract on Render Thread (where Readback->Lock is required)
			ENQUEUE_RENDER_COMMAND(LockAndExtractReadbackPixels)(
				[WeakThis, Readback, Width, Height, SafeStep, TargetColor, ColorTolerance, BaseFColor, PixelFormat, OnCompleteCallback = MoveTemp(OnCompleteCallback)](FRHICommandListImmediate& RHICmdList) mutable
				{
					int32 RowPitchInPixels = 0;
					int32 BufferHeight = 0;
					void* MappedMemory = Readback->Lock(RowPitchInPixels, &BufferHeight);
					if (!MappedMemory)
					{
						Readback->Unlock();
						AsyncTask(ENamedThreads::GameThread, [WeakThis, OnCompleteCallback = MoveTemp(OnCompleteCallback)]()
						{
							if (WeakThis.IsValid())
							{
								WeakThis->bIsCalculatingPercentage = false;
							}
							if (OnCompleteCallback)
							{
								OnCompleteCallback(0.0f);
							}
						});
						return;
					}

					// Copy and format-decode pixel data safely into CPU memory
					TArray<FColor> Pixels;
					DecodeReadbackPixels(MappedMemory, PixelFormat, Width, Height, RowPitchInPixels, Pixels);
					Readback->Unlock();

					// Offload the pixel counting loop to a worker background thread
					AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, Pixels = MoveTemp(Pixels), Width, Height, SafeStep, TargetColor, ColorTolerance, BaseFColor, RowPitchInPixels, OnCompleteCallback = MoveTemp(OnCompleteCallback)]() mutable
					{
						const int32 TotalPixelCount = Pixels.Num();
						int32 TotalSampled = 0;
						int32 PaintedSampled = 0;

						const bool bMatchSpecificColor = (TargetColor.A > 0.01f);
						const FColor TargetFColor = TargetColor.ToFColor(true);

						// Per-channel tolerance: ColorTolerance 0.0 = exact, 1.0 = anything matches
						const int32 ChannelThreshold = FMath::Clamp(FMath::RoundToInt(ColorTolerance * 255.0f), 0, 255);

						// Euclidean fallback for "any paint" mode (TargetColor.A == 0)
						const float MaxDist = ColorTolerance * 441.67f;
						const float MaxDistSq = MaxDist * MaxDist;

						for (int32 Y = 0; Y < Height; Y += SafeStep)
						{
							for (int32 X = 0; X < Width; X += SafeStep)
							{
								const int32 Index = Y * Width + X;
								if (Index < TotalPixelCount)
								{
									const FColor& Pixel = Pixels[Index];
									TotalSampled++;

									if (bMatchSpecificColor)
									{
										// Per-channel difference check:
										// Get pixel R,G,B and target R,G,B separately.
										// Subtract each channel - if all 3 diffs are within threshold, pixel is painted.
										const int32 DiffR = FMath::Abs((int32)Pixel.R - (int32)TargetFColor.R);
										const int32 DiffG = FMath::Abs((int32)Pixel.G - (int32)TargetFColor.G);
										const int32 DiffB = FMath::Abs((int32)Pixel.B - (int32)TargetFColor.B);

										const bool bRMatch = (DiffR <= ChannelThreshold);
										const bool bGMatch = (DiffG <= ChannelThreshold);
										const bool bBMatch = (DiffB <= ChannelThreshold);

										if (bRMatch && bGMatch && bBMatch)
										{
											PaintedSampled++;
										}
									}
									else
									{
										const float DistFromBaseSq = FMath::Square((float)(Pixel.R - BaseFColor.R)) +
										                             FMath::Square((float)(Pixel.G - BaseFColor.G)) +
										                             FMath::Square((float)(Pixel.B - BaseFColor.B));
										if (DistFromBaseSq > MaxDistSq)
										{
											PaintedSampled++;
										}
									}
								}
							}
						}

						const float ResultPercentage = (TotalSampled > 0) ? ((float)PaintedSampled / (float)TotalSampled) * 100.0f : 0.0f;

						// Sample a center pixel for live diagnostics
						const int32 CenterIdx = (Height / 2) * Width + (Width / 2);
						const FColor SamplePixel = (CenterIdx < TotalPixelCount) ? Pixels[CenterIdx] : FColor::Black;

						// Dispatch result back to Game Thread
						AsyncTask(ENamedThreads::GameThread, [WeakThis, ResultPercentage, PaintedSampled, TotalSampled, RowPitchInPixels, SamplePixel, OnCompleteCallback = MoveTemp(OnCompleteCallback)]()
						{
							if (WeakThis.IsValid())
							{
								WeakThis->bIsCalculatingPercentage = false;
								WeakThis->LastPaintedPercentage = ResultPercentage;

								WeakThis->PrintScreenLog(FString::Printf(TEXT("CalculatePaintedPercentage (GPU Async): %0.1f%% (%d / %d sampled) [Sample Pixel: R=%d G=%d B=%d]"),
									ResultPercentage, PaintedSampled, TotalSampled, SamplePixel.R, SamplePixel.G, SamplePixel.B), FColor::Green, 4.0f);

								WeakThis->OnPaintedPercentageCalculated.Broadcast(ResultPercentage);
							}

							if (OnCompleteCallback)
							{
								OnCompleteCallback(ResultPercentage);
							}
						});
					});
				}
			);

			return false; // Done ticking
		})
	);
}

// --------------------------------------------------------------------------------------------------
// UAsyncCalculatePaintedPercentage Implementation
// --------------------------------------------------------------------------------------------------

UAsyncCalculatePaintedPercentage* UAsyncCalculatePaintedPercentage::AsyncCalculatePaintedPercentage(
	UObject* WorldContextObject,
	UTexturePaintComponent* PaintComponent,
	FLinearColor TargetColor,
	float ColorTolerance,
	int32 SampleStep)
{
	if (!PaintComponent)
	{
		return nullptr;
	}

	UAsyncCalculatePaintedPercentage* Action = NewObject<UAsyncCalculatePaintedPercentage>();
	UObject* Context = WorldContextObject ? WorldContextObject : PaintComponent;
	Action->RegisterWithGameInstance(Context);
	Action->TargetComponent = PaintComponent;
	Action->ColorToMatch = TargetColor;
	Action->Tolerance = ColorTolerance;
	Action->Step = SampleStep;
	return Action;
}

void UAsyncCalculatePaintedPercentage::Activate()
{
	if (!TargetComponent.IsValid())
	{
		OnCompleted.Broadcast(0.0f);
		SetReadyToDestroy();
		return;
	}

	TargetComponent->CalculatePaintedPercentageInternal(
		ColorToMatch,
		Tolerance,
		Step,
		[this](float Percentage)
		{
			OnCompleted.Broadcast(Percentage);
			SetReadyToDestroy();
		}
	);
}

// --------------------------------------------------------------------------------------------------
// Canvas Pixel Extraction Functions
// --------------------------------------------------------------------------------------------------

bool UTexturePaintComponent::GetCanvasPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, int32 SampleStep)
{
	if (!RenderTarget)
	{
		PrintScreenWarning(TEXT("GetCanvasPixels: RenderTarget is null!"), 3.0f);
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		PrintScreenWarning(TEXT("GetCanvasPixels: RenderTarget resource is null!"), 3.0f);
		return false;
	}

	TArray<FColor> RawPixels;
	FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
	ReadPixelFlags.SetLinearToGamma(false);

	if (!RTResource->ReadPixels(RawPixels, ReadPixelFlags))
	{
		PrintScreenWarning(TEXT("GetCanvasPixels: Failed to read pixels from Render Target!"), 3.0f);
		return false;
	}

	const int32 Width = RenderTarget->SizeX;
	const int32 Height = RenderTarget->SizeY;
	const int32 SafeStep = FMath::Clamp(SampleStep, 1, 16);

	if (SafeStep <= 1)
	{
		OutPixels = MoveTemp(RawPixels);
		OutWidth = Width;
		OutHeight = Height;
		return true;
	}

	OutPixels.Reset();
	OutWidth = 0;
	OutHeight = 0;

	for (int32 Y = 0; Y < Height; Y += SafeStep)
	{
		OutHeight++;
		int32 RowCount = 0;
		for (int32 X = 0; X < Width; X += SafeStep)
		{
			RowCount++;
			OutPixels.Add(RawPixels[Y * Width + X]);
		}
		if (OutWidth == 0)
		{
			OutWidth = RowCount;
		}
	}

	return true;
}

void UTexturePaintComponent::GetCanvasPixelsAsync(int32 SampleStep)
{
	GetCanvasPixelsInternal(SampleStep, nullptr);
}

void UTexturePaintComponent::GetCanvasPixelsInternal(
	int32 SampleStep,
	TFunction<void(TArray<FColor>, int32, int32)> OnCompleteCallback)
{
	if (!RenderTarget)
	{
		PrintScreenWarning(TEXT("GetCanvasPixelsAsync: RenderTarget is null!"), 3.0f);
		if (OnCompleteCallback)
		{
			OnCompleteCallback(TArray<FColor>(), 0, 0);
		}
		return;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		PrintScreenWarning(TEXT("GetCanvasPixelsAsync: RenderTarget resource is null!"), 3.0f);
		if (OnCompleteCallback)
		{
			OnCompleteCallback(TArray<FColor>(), 0, 0);
		}
		return;
	}

	FTextureRHIRef TextureRHI = RTResource->GetTextureRHI();
	if (!TextureRHI)
	{
		PrintScreenWarning(TEXT("GetCanvasPixelsAsync: Texture RHI resource is null!"), 3.0f);
		if (OnCompleteCallback)
		{
			OnCompleteCallback(TArray<FColor>(), 0, 0);
		}
		return;
	}

	const int32 SafeStep = FMath::Clamp(SampleStep, 1, 16);
	const int32 Width = RenderTarget->SizeX;
	const int32 Height = RenderTarget->SizeY;
	const EPixelFormat PixelFormat = TextureRHI->GetFormat();

	TSharedPtr<FRHIGPUTextureReadback> Readback = MakeShared<FRHIGPUTextureReadback>(FName("PaintCanvasGetPixelsAsyncReadback"));

	ENQUEUE_RENDER_COMMAND(PaintCanvasGetPixelsCopy)(
		[Readback, TextureRHI, Width, Height](FRHICommandListImmediate& RHICmdList)
		{
			Readback->EnqueueCopy(RHICmdList, TextureRHI, FResolveRect(0, 0, Width, Height));
		}
	);

	TWeakObjectPtr<UTexturePaintComponent> WeakThis(this);

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis, Readback, Width, Height, SafeStep, PixelFormat, OnCompleteCallback = MoveTemp(OnCompleteCallback)](float DeltaTime) mutable -> bool
		{
			if (!WeakThis.IsValid())
			{
				return false;
			}

			if (!Readback.IsValid() || !Readback->IsReady())
			{
				return true;
			}

			ENQUEUE_RENDER_COMMAND(LockAndExtractCanvasPixels)(
				[WeakThis, Readback, Width, Height, SafeStep, PixelFormat, OnCompleteCallback = MoveTemp(OnCompleteCallback)](FRHICommandListImmediate& RHICmdList) mutable
				{
					int32 RowPitchInPixels = 0;
					int32 BufferHeight = 0;
					void* MappedMemory = Readback->Lock(RowPitchInPixels, &BufferHeight);
					if (!MappedMemory)
					{
						Readback->Unlock();
						AsyncTask(ENamedThreads::GameThread, [WeakThis, OnCompleteCallback = MoveTemp(OnCompleteCallback)]()
						{
							if (OnCompleteCallback)
							{
								OnCompleteCallback(TArray<FColor>(), 0, 0);
							}
						});
						return;
					}

					// Copy and format-decode pixel data safely into CPU memory
					TArray<FColor> Pixels;
					DecodeReadbackPixels(MappedMemory, PixelFormat, Width, Height, RowPitchInPixels, Pixels);
					Readback->Unlock();

					AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, Pixels = MoveTemp(Pixels), Width, Height, SafeStep, PixelFormat, OnCompleteCallback = MoveTemp(OnCompleteCallback)]() mutable
					{
						TArray<FColor> ResultPixels;
						int32 OutWidth = Width;
						int32 OutHeight = Height;

						if (SafeStep <= 1)
						{
							ResultPixels = MoveTemp(Pixels);
						}
						else
						{
							OutWidth = 0;
							OutHeight = 0;
							for (int32 Y = 0; Y < Height; Y += SafeStep)
							{
								OutHeight++;
								int32 RowCount = 0;
								for (int32 X = 0; X < Width; X += SafeStep)
								{
									RowCount++;
									ResultPixels.Add(Pixels[Y * Width + X]);
								}
								if (OutWidth == 0)
								{
									OutWidth = RowCount;
								}
							}
						}

						AsyncTask(ENamedThreads::GameThread, [WeakThis, ResultPixels = MoveTemp(ResultPixels), OutWidth, OutHeight, PixelFormat, OnCompleteCallback = MoveTemp(OnCompleteCallback)]()
						{
							if (WeakThis.IsValid())
							{
								const FColor P0 = ResultPixels.Num() > 0 ? ResultPixels[0] : FColor::Black;
								const int32 Center = (OutHeight / 2) * OutWidth + (OutWidth / 2);
								const FColor PCenter = (Center >= 0 && Center < ResultPixels.Num()) ? ResultPixels[Center] : FColor::Black;

								WeakThis->PrintScreenLog(FString::Printf(TEXT("GetCanvasPixelsAsync: Extracted %d pixels (%dx%d) [Format: %s] [Top-Left P(0,0): R=%d G=%d B=%d] [Center: R=%d G=%d B=%d]"),
									ResultPixels.Num(), OutWidth, OutHeight, GPixelFormats[PixelFormat].Name, P0.R, P0.G, P0.B, PCenter.R, PCenter.G, PCenter.B), FColor::Cyan, 4.0f);

								WeakThis->OnCanvasPixelsReady.Broadcast(ResultPixels, OutWidth, OutHeight);
							}

							if (OnCompleteCallback)
							{
								OnCompleteCallback(ResultPixels, OutWidth, OutHeight);
							}
						});
					});
				}
			);

			return false;
		})
	);
}

// --------------------------------------------------------------------------------------------------
// UAsyncGetCanvasPixels Implementation
// --------------------------------------------------------------------------------------------------

UAsyncGetCanvasPixels* UAsyncGetCanvasPixels::AsyncGetCanvasPixels(
	UObject* WorldContextObject,
	UTexturePaintComponent* PaintComponent,
	int32 SampleStep)
{
	if (!PaintComponent)
	{
		return nullptr;
	}

	UAsyncGetCanvasPixels* Action = NewObject<UAsyncGetCanvasPixels>();
	UObject* Context = WorldContextObject ? WorldContextObject : PaintComponent;
	Action->RegisterWithGameInstance(Context);
	Action->TargetComponent = PaintComponent;
	Action->Step = SampleStep;
	return Action;
}

void UAsyncGetCanvasPixels::Activate()
{
	if (!TargetComponent.IsValid())
	{
		OnCompleted.Broadcast(TArray<FColor>(), 0, 0, 0);
		SetReadyToDestroy();
		return;
	}

	TargetComponent->GetCanvasPixelsInternal(
		Step,
		[this](TArray<FColor> Pixels, int32 Width, int32 Height)
		{
			const int32 Total = Pixels.Num();
			OnCompleted.Broadcast(Pixels, Width, Height, Total);
			SetReadyToDestroy();
		}
	);
}



