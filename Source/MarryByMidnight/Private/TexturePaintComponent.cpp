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
	, bEnableScreenLogging(true)
	, BrushRotation(0.0f)
	, MinOrientSpeed(0.01f)
	, MinRotationInterpSpeed(3.0f)
	, MaxRotationInterpSpeed(30.0f)
	, MaxSpeedThreshold(1.5f)
	, BrushMaterial(nullptr)
	, BrushMaskTexture(nullptr)
	, CanvasModifierMaterial(nullptr)
	, BrushModifierMaterial(nullptr)
	, TextureParameterName(FName("PaintTexture"))
	, MaterialSlotIndex(0)
	, DefaultRenderTargetResolution(1024)
	, LastPaintedPercentage(0.0f)
	, bIsCalculatingPercentage(false)
	, CanvasBaseColor(FLinearColor::White)
	, LastPaintedUV(FVector2D::ZeroVector)
	, bHasLastPaintedUV(false)
	, RenderTarget(nullptr)
	, PingPongRenderTarget(nullptr)
	, DynamicMaterial(nullptr)
	, BrushMID(nullptr)
	, CanvasModifierMID(nullptr)
	, BrushModifierMID(nullptr)
	, CachedBaseTexture(nullptr)
	, TargetMesh(nullptr)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTexturePaintComponent::BeginPlay()
{
	Super::BeginPlay();

	// Enable debug logging for diagnostic debugging
	bEnableScreenLogging = true;
	UE_LOG(LogTemp, Log, TEXT("[TexturePaint] BeginPlay on '%s' (AutoInit=%s, DefaultRes=%d, Param='%s')"),
		*GetNameSafe(GetOwner()), bAutoInitializeOnBeginPlay ? TEXT("true") : TEXT("false"),
		DefaultRenderTargetResolution, *TextureParameterName.ToString());

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
	UE_LOG(LogTemp, Log, TEXT("[TexturePaint] Debug logging %s."), *StatusStr);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(999911, 3.0f, StatusColor, FString::Printf(TEXT("[TexturePaint] Debug Logging: %s"), *StatusStr));
	}
	return bEnableScreenLogging;
}

void UTexturePaintComponent::SetEnableScreenLogging(bool bEnable)
{
	bEnableScreenLogging = bEnable;
	const FString StatusStr = bEnableScreenLogging ? TEXT("ENABLED") : TEXT("DISABLED");
	const FColor StatusColor = bEnableScreenLogging ? FColor::Green : FColor::Red;
	UE_LOG(LogTemp, Log, TEXT("[TexturePaint] Debug logging set to %s."), *StatusStr);
	if (GEngine && bEnableScreenLogging)
	{
		GEngine->AddOnScreenDebugMessage(999911, 3.0f, StatusColor, FString::Printf(TEXT("[TexturePaint] Debug Logging: %s"), *StatusStr));
	}
}

void UTexturePaintComponent::PrintScreenLog(const FString& Message, FColor Color, float Duration, uint64 Key)
{
	UE_LOG(LogTemp, Log, TEXT("[TexturePaint] %s"), *Message);
	if (bEnableScreenLogging && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Duration, Color, FString::Printf(TEXT("[Paint] %s"), *Message));
	}
}

void UTexturePaintComponent::PrintScreenWarning(const FString& Message, float Duration, uint64 Key)
{
	UE_LOG(LogTemp, Warning, TEXT("[TexturePaint][WARNING] %s"), *Message);
	if (bEnableScreenLogging && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Duration, FColor::Yellow, FString::Printf(TEXT("[Warning] %s"), *Message));
	}
}

void UTexturePaintComponent::PrintScreenError(const FString& Message, float Duration, uint64 Key)
{
	UE_LOG(LogTemp, Error, TEXT("[TexturePaint][ERROR] %s"), *Message);
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
		PrintScreenWarning(TEXT("[AutoInit] FAILED: Component has no Owner actor!"));
		return false;
	}

	PrintScreenLog(FString::Printf(TEXT("[AutoInit] Step 1/4: Owner actor '%s' found. Searching for StaticMeshComponent..."), *Owner->GetName()), FColor::Cyan, 6.0f);

	UStaticMeshComponent* FoundMesh = TargetMesh;
	if (!FoundMesh)
	{
		FoundMesh = Owner->FindComponentByClass<UStaticMeshComponent>();
	}

	if (!FoundMesh)
	{
		PrintScreenWarning(FString::Printf(TEXT("[AutoInit] FAILED: Owner '%s' has NO StaticMeshComponent!"), *Owner->GetName()), 6.0f);
		return false;
	}

	PrintScreenLog(FString::Printf(TEXT("[AutoInit] Step 2/4: StaticMeshComponent '%s' found (NumMaterials=%d)."),
		*FoundMesh->GetName(), FoundMesh->GetNumMaterials()), FColor::Cyan, 6.0f);

	UMaterialInterface* BaseMat = FoundMesh->GetMaterial(MaterialSlotIndex);
	if (!BaseMat)
	{
		PrintScreenWarning(FString::Printf(TEXT("[AutoInit] No material at slot %d, trying slot 0..."), MaterialSlotIndex), 5.0f);
		BaseMat = FoundMesh->GetMaterial(0);
		if (BaseMat)
		{
			MaterialSlotIndex = 0;
		}
	}

	if (!BaseMat)
	{
		PrintScreenWarning(FString::Printf(TEXT("[AutoInit] FAILED: Mesh '%s' has NO material assigned at any slot!"),
			*FoundMesh->GetName()), 6.0f);
		return false;
	}

	PrintScreenLog(FString::Printf(TEXT("[AutoInit] Step 3/4: Material '%s' (%s) selected at slot %d."),
		*BaseMat->GetName(), *BaseMat->GetClass()->GetName(), MaterialSlotIndex), FColor::Cyan, 6.0f);

	PrintScreenLog(FString::Printf(TEXT("[AutoInit] Step 4/4: Calling SetupPainting(Mesh='%s', Mat='%s', Param='%s', Slot=%d)..."),
		*FoundMesh->GetName(), *BaseMat->GetName(), *TextureParameterName.ToString(), MaterialSlotIndex), FColor::Cyan, 6.0f);

	SetupPainting(FoundMesh, BaseMat, TextureParameterName, MaterialSlotIndex);
	return RenderTarget != nullptr;
}

UTexture* UTexturePaintComponent::ExtractBaseTexture(UMaterialInterface* InMaterial, FName InParamName)
{
	if (!InMaterial)
	{
		PrintScreenWarning(TEXT("[ExtractBaseTexture] InMaterial is null!"));
		return nullptr;
	}

	PrintScreenLog(FString::Printf(TEXT("[ExtractBaseTexture] Scanning material '%s' for base texture (TargetParam='%s')..."),
		*InMaterial->GetName(), *InParamName.ToString()), FColor::White, 5.0f);

	UTexture* ExtractedTexture = nullptr;

	// 1. Try specified parameter name
	if (!InParamName.IsNone())
	{
		InMaterial->GetTextureParameterValue(InParamName, ExtractedTexture);
		if (ExtractedTexture)
		{
			PrintScreenLog(FString::Printf(TEXT("[ExtractBaseTexture] FOUND MATCH via specified param '%s' -> Texture '%s' (%s)"),
				*InParamName.ToString(), *ExtractedTexture->GetName(), *ExtractedTexture->GetClass()->GetName()), FColor::Green, 5.0f);
			return ExtractedTexture;
		}
		else
		{
			PrintScreenLog(FString::Printf(TEXT("[ExtractBaseTexture] Specified param '%s' has no assigned texture. Scanning fallbacks..."),
				*InParamName.ToString()), FColor::Yellow, 4.0f);
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
		if (ParamName == InParamName)
		{
			continue;
		}
		InMaterial->GetTextureParameterValue(ParamName, ExtractedTexture);
		if (ExtractedTexture)
		{
			PrintScreenLog(FString::Printf(TEXT("[ExtractBaseTexture] FOUND MATCH via common param '%s' -> Texture '%s'"),
				*ParamName.ToString(), *ExtractedTexture->GetName()), FColor::Green, 5.0f);
			return ExtractedTexture;
		}
	}

	// 3. Search all texture parameters defined on this material
	TArray<FMaterialParameterInfo> OutParameterInfo;
	TArray<FGuid> OutParameterGuids;
	InMaterial->GetAllTextureParameterInfo(OutParameterInfo, OutParameterGuids);
	PrintScreenLog(FString::Printf(TEXT("[ExtractBaseTexture] Material '%s' defines %d texture parameter(s):"),
		*InMaterial->GetName(), OutParameterInfo.Num()), FColor::White, 5.0f);

	for (const FMaterialParameterInfo& ParamInfo : OutParameterInfo)
	{
		UTexture* CandidateTex = nullptr;
		InMaterial->GetTextureParameterValue(ParamInfo, CandidateTex);
		PrintScreenLog(FString::Printf(TEXT("   - Param '%s' = '%s'"),
			*ParamInfo.Name.ToString(), CandidateTex ? *CandidateTex->GetName() : TEXT("None")), FColor::White, 4.0f);

		if (!ExtractedTexture && CandidateTex)
		{
			ExtractedTexture = CandidateTex;
		}
	}

	if (ExtractedTexture)
	{
		PrintScreenLog(FString::Printf(TEXT("[ExtractBaseTexture] Selected candidate texture '%s' from parameters."),
			*ExtractedTexture->GetName()), FColor::Green, 5.0f);
		return ExtractedTexture;
	}

	PrintScreenWarning(FString::Printf(TEXT("[ExtractBaseTexture] No texture parameter found in material '%s'. Canvas will initialize with solid color."),
		*InMaterial->GetName()), 5.0f);
	return nullptr;
}

void UTexturePaintComponent::CopyTextureToTarget(UTexture* InTexture, UTextureRenderTarget2D* TargetRT)
{
	if (!TargetRT)
	{
		PrintScreenWarning(TEXT("[CopyTextureToTarget] TargetRT is null!"));
		return;
	}

	if (InTexture)
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize;
		FDrawToRenderTargetContext DrawContext;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, TargetRT, Canvas, CanvasSize, DrawContext);
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
		PrintScreenLog(FString::Printf(TEXT("[CopyTextureToTarget] Drawn texture '%s' onto RT (%0.0fx%0.0f)."),
			*InTexture->GetName(), (float)TargetRT->SizeX, (float)TargetRT->SizeY), FColor::White, 3.0f);
	}
	else
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, TargetRT, CanvasBaseColor);
		PrintScreenLog(FString::Printf(TEXT("[CopyTextureToTarget] Cleared RT (%0.0fx%0.0f) with color (R=%0.2f, G=%0.2f, B=%0.2f, A=%0.2f)."),
			(float)TargetRT->SizeX, (float)TargetRT->SizeY, CanvasBaseColor.R, CanvasBaseColor.G, CanvasBaseColor.B, CanvasBaseColor.A), FColor::White, 3.0f);
	}
}

void UTexturePaintComponent::CopyTextureToRenderTarget(UTexture* InTexture)
{
	if (!RenderTarget)
	{
		PrintScreenWarning(TEXT("[CopyTextureToRenderTarget] RenderTarget is null!"), 4.0f);
		return;
	}

	CopyTextureToTarget(InTexture, RenderTarget);

	if (InTexture)
	{
		PrintScreenLog(FString::Printf(TEXT("[CopyTextureToRenderTarget] Copied base texture '%s' to Render Target (%0.0fx%0.0f)."),
			*InTexture->GetName(), (float)RenderTarget->SizeX, (float)RenderTarget->SizeY), FColor::Green, 4.0f);
	}
	else
	{
		PrintScreenLog(TEXT("[CopyTextureToRenderTarget] No base texture; cleared Render Target."), FColor::Yellow, 4.0f);
	}
}

void UTexturePaintComponent::SetupPainting(
	UStaticMeshComponent* InMeshComponent,
	UMaterialInterface* InBaseMaterial,
	FName InTextureParameterName,
	int32 InMaterialSlotIndex,
	UTexture* InOverrideBaseTexture)
{
	PrintScreenLog(TEXT("[SetupPainting] === STEP 1: Validating Inputs ==="), FColor::Cyan, 6.0f);
	PrintScreenLog(FString::Printf(TEXT("   - InMeshComponent: %s"), InMeshComponent ? *InMeshComponent->GetName() : TEXT("NULL")), FColor::White, 5.0f);
	PrintScreenLog(FString::Printf(TEXT("   - InBaseMaterial: %s"), InBaseMaterial ? *InBaseMaterial->GetName() : TEXT("NULL (will auto-fallback)")), FColor::White, 5.0f);
	PrintScreenLog(FString::Printf(TEXT("   - InTextureParameterName: '%s'"), *InTextureParameterName.ToString()), FColor::White, 5.0f);
	PrintScreenLog(FString::Printf(TEXT("   - InMaterialSlotIndex: %d"), InMaterialSlotIndex), FColor::White, 5.0f);
	PrintScreenLog(FString::Printf(TEXT("   - InOverrideBaseTexture: %s"), InOverrideBaseTexture ? *InOverrideBaseTexture->GetName() : TEXT("None")), FColor::White, 5.0f);

	if (!InMeshComponent)
	{
		PrintScreenError(TEXT("[SetupPainting] ABORTED: InMeshComponent is null! Please pass a valid StaticMeshComponent."), 8.0f);
		return;
	}

	TargetMesh = InMeshComponent;
	MaterialSlotIndex = InMaterialSlotIndex;

	// Auto-fallback: If InBaseMaterial was left unconnected, grab it directly from the mesh component
	if (!InBaseMaterial)
	{
		PrintScreenLog(FString::Printf(TEXT("[SetupPainting] InBaseMaterial not provided; querying mesh '%s' slot %d..."), *TargetMesh->GetName(), MaterialSlotIndex), FColor::Yellow, 5.0f);
		InBaseMaterial = TargetMesh->GetMaterial(MaterialSlotIndex);
		if (!InBaseMaterial)
		{
			InBaseMaterial = TargetMesh->GetMaterial(0);
			if (InBaseMaterial)
			{
				MaterialSlotIndex = 0;
			}
		}
	}

	if (!InBaseMaterial)
	{
		PrintScreenError(FString::Printf(TEXT("[SetupPainting] ABORTED: Mesh '%s' has NO material assigned at slot %d or slot 0!"),
			*TargetMesh->GetName(), MaterialSlotIndex), 8.0f);
		return;
	}

	PrintScreenLog(FString::Printf(TEXT("[SetupPainting] Base material confirmed: '%s' (%s)"),
		*InBaseMaterial->GetName(), *InBaseMaterial->GetClass()->GetName()), FColor::Cyan, 5.0f);

	if (!InTextureParameterName.IsNone())
	{
		TextureParameterName = InTextureParameterName;
	}

	// 2. Resolve base texture
	PrintScreenLog(TEXT("[SetupPainting] === STEP 2: Resolving Base Texture ==="), FColor::Cyan, 5.0f);
	CachedBaseTexture = InOverrideBaseTexture ? InOverrideBaseTexture : ExtractBaseTexture(InBaseMaterial, TextureParameterName);
	if (CachedBaseTexture)
	{
		PrintScreenLog(FString::Printf(TEXT("   -> Base Texture: '%s' (%s)"), *CachedBaseTexture->GetName(), *CachedBaseTexture->GetClass()->GetName()), FColor::Green, 5.0f);
	}
	else
	{
		PrintScreenLog(TEXT("   -> Base Texture: NONE (canvas will start with CanvasBaseColor)"), FColor::Yellow, 5.0f);
	}

	// 3. Determine Render Target resolution
	PrintScreenLog(TEXT("[SetupPainting] === STEP 3: Determining Canvas Resolution ==="), FColor::Cyan, 5.0f);
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
				PrintScreenLog(FString::Printf(TEXT("   -> Matched resolution to base texture: %dx%d"), TargetWidth, TargetHeight), FColor::Cyan, 5.0f);
			}
		}
	}
	else
	{
		PrintScreenLog(FString::Printf(TEXT("   -> Using default resolution: %dx%d"), TargetWidth, TargetHeight), FColor::Cyan, 5.0f);
	}

	// 4. Create or reallocate Render Targets (Use RTF_RGBA8 for 8-bit RGBA and direct PF_B8G8R8A8 memory layout)
	PrintScreenLog(TEXT("[SetupPainting] === STEP 4: Creating Render Targets (RTF_RGBA8) ==="), FColor::Cyan, 5.0f);
	RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, TargetWidth, TargetHeight, RTF_RGBA8);
	PingPongRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, TargetWidth, TargetHeight, RTF_RGBA8);
	if (!RenderTarget || !PingPongRenderTarget)
	{
		PrintScreenError(FString::Printf(TEXT("[SetupPainting] FAILED to allocate %dx%d RenderTarget(s)!"), TargetWidth, TargetHeight), 8.0f);
		return;
	}
	PrintScreenLog(FString::Printf(TEXT("   -> RenderTarget: %dx%d (Addr: %p)"), TargetWidth, TargetHeight, (void*)RenderTarget), FColor::Green, 5.0f);
	PrintScreenLog(FString::Printf(TEXT("   -> PingPongRenderTarget: %dx%d (Addr: %p)"), TargetWidth, TargetHeight, (void*)PingPongRenderTarget), FColor::Green, 5.0f);

	// 5. Copy base texture onto Render Targets
	PrintScreenLog(TEXT("[SetupPainting] === STEP 5: Initializing Canvas Contents ==="), FColor::Cyan, 5.0f);
	CopyTextureToTarget(CachedBaseTexture, RenderTarget);
	CopyTextureToTarget(CachedBaseTexture, PingPongRenderTarget);

	// 6. Create Dynamic Material Instance from base material
	PrintScreenLog(TEXT("[SetupPainting] === STEP 6: Creating Dynamic Material Instance ==="), FColor::Cyan, 5.0f);
	DynamicMaterial = UMaterialInstanceDynamic::Create(InBaseMaterial, this);
	if (!DynamicMaterial)
	{
		PrintScreenError(FString::Printf(TEXT("[SetupPainting] FAILED to create Dynamic Material Instance from '%s'!"), *InBaseMaterial->GetName()), 8.0f);
		return;
	}

	// Bind the Render Target to the designated texture parameter
	DynamicMaterial->SetTextureParameterValue(TextureParameterName, RenderTarget);

	// Verify parameter binding
	UTexture* BoundTexture = nullptr;
	DynamicMaterial->GetTextureParameterValue(TextureParameterName, BoundTexture);
	const bool bParamBound = (BoundTexture == RenderTarget);
	PrintScreenLog(FString::Printf(TEXT("   -> DynamicMaterial '%s' created."), *DynamicMaterial->GetName()), FColor::Cyan, 5.0f);
	PrintScreenLog(FString::Printf(TEXT("   -> Parameter '%s' bound to RenderTarget? %s (Bound: '%s')"),
		*TextureParameterName.ToString(), bParamBound ? TEXT("YES [OK]") : TEXT("NO [WARNING]"),
		BoundTexture ? *BoundTexture->GetName() : TEXT("None")),
		bParamBound ? FColor::Green : FColor::Yellow, 5.0f);

	if (!bParamBound)
	{
		PrintScreenWarning(FString::Printf(TEXT("[SetupPainting] WARNING: Material '%s' does NOT have a texture parameter named '%s'! Ensure the material has a Texture Sample Parameter named '%s'!"),
			*InBaseMaterial->GetName(), *TextureParameterName.ToString(), *TextureParameterName.ToString()), 7.0f);
	}

	// 7. Apply to mesh component at designated slot
	PrintScreenLog(TEXT("[SetupPainting] === STEP 7: Applying Dynamic Material to Mesh ==="), FColor::Cyan, 5.0f);
	TargetMesh->SetMaterial(MaterialSlotIndex, DynamicMaterial);

	// Verify material on mesh slot
	UMaterialInterface* AppliedMat = TargetMesh->GetMaterial(MaterialSlotIndex);
	const bool bMeshMatMatches = (AppliedMat == DynamicMaterial);
	PrintScreenLog(FString::Printf(TEXT("   -> Mesh '%s' Slot %d material verified? %s (Applied: '%s')"),
		*TargetMesh->GetName(), MaterialSlotIndex,
		bMeshMatMatches ? TEXT("YES [OK]") : TEXT("NO [ERROR]"),
		AppliedMat ? *AppliedMat->GetName() : TEXT("None")),
		bMeshMatMatches ? FColor::Green : FColor::Red, 6.0f);

	PrintScreenLog(FString::Printf(TEXT("[SetupPainting] *** COMPLETE *** Ready to paint on '%s' (Slot %d, Res %dx%d, Param '%s')!"),
		*TargetMesh->GetName(), MaterialSlotIndex, TargetWidth, TargetHeight, *TextureParameterName.ToString()),
		FColor::Emerald, 7.0f);
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
	PrintScreenLog(FString::Printf(TEXT("[PaintAtUV] === START STROKE ===")), FColor::Cyan, 2.0f);
	PrintScreenLog(FString::Printf(TEXT("   - Target UV: (%0.4f, %0.4f)"), UVCoordinate.X, UVCoordinate.Y), FColor::White, 2.0f);
	PrintScreenLog(FString::Printf(TEXT("   - BrushSize: %0.1f px"), BrushSize), FColor::White, 2.0f);
	PrintScreenLog(FString::Printf(TEXT("   - PaintColor: (R=%0.2f, G=%0.2f, B=%0.2f, A=%0.2f)"), PaintColor.R, PaintColor.G, PaintColor.B, PaintColor.A), FColor::White, 2.0f);
	PrintScreenLog(FString::Printf(TEXT("   - InRotationDegrees: %0.1f"), InRotationDegrees), FColor::White, 2.0f);

	if (!RenderTarget)
	{
		PrintScreenWarning(TEXT("[PaintAtUV] RenderTarget is null! Attempting AutoInitialize..."), 4.0f);
		if (!AutoInitialize())
		{
			PrintScreenError(TEXT("[PaintAtUV] ABORTED: RenderTarget is null and AutoInitialize failed! Ensure mesh has material assigned."), 6.0f);
			return;
		}
	}

	if (UVCoordinate.IsNearlyZero())
	{
		PrintScreenWarning(TEXT("[PaintAtUV] WARNING: UV is (0,0)! If using Line Trace, verify 'Find Collision UV' is enabled on Line Trace node and complex collision is enabled on the mesh."), 5.0f);
	}
	if (UVCoordinate.X < 0.0f || UVCoordinate.X > 1.0f || UVCoordinate.Y < 0.0f || UVCoordinate.Y > 1.0f)
	{
		PrintScreenWarning(FString::Printf(TEXT("[PaintAtUV] WARNING: UV (%0.3f, %0.3f) is outside [0.0, 1.0]!"), UVCoordinate.X, UVCoordinate.Y), 5.0f);
	}

	// Invalidate BrushMID if BrushMaterial was changed externally or does not match
	if (BrushMID && BrushMaterial && BrushMID->Parent != BrushMaterial)
	{
		PrintScreenLog(TEXT("   - Invalidation: BrushMaterial changed; resetting BrushMID."), FColor::Yellow, 2.0f);
		BrushMID = nullptr;
	}

	// Initialize BrushMID if not yet cached
	if (!BrushMID)
	{
		UMaterialInterface* BaseBrushMat = BrushMaterial;
		if (!BaseBrushMat)
		{
			BaseBrushMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/TextureDraw/M_Brush.M_Brush")));
			PrintScreenLog(FString::Printf(TEXT("   - Loaded default brush material: '%s'"), BaseBrushMat ? *BaseBrushMat->GetName() : TEXT("FAILED TO LOAD")),
				BaseBrushMat ? FColor::White : FColor::Red, 3.0f);
		}

		if (BaseBrushMat)
		{
			BrushMID = UMaterialInstanceDynamic::Create(BaseBrushMat, this);
			PrintScreenLog(FString::Printf(TEXT("   - Created BrushMID from '%s'."), *BaseBrushMat->GetName()), FColor::Cyan, 3.0f);
		}

		if (!BrushMID)
		{
			PrintScreenWarning(TEXT("[PaintAtUV] Could not create BrushMID from BrushMaterial; falling back to direct texture draw."), 4.0f);
		}
	}

	// Resolve the active brush texture from component property or default Bake_Mask_Image
	UTexture* ActiveBrushTexture = BrushMaskTexture;
	if (!ActiveBrushTexture)
	{
		ActiveBrushTexture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, TEXT("/Game/TextureDraw/BrushMask/Bake_Mask_Image.Bake_Mask_Image")));
	}
	if (!ActiveBrushTexture)
	{
		ActiveBrushTexture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, TEXT("/Game/TextureDraw/Bake_Mask_Image.Bake_Mask_Image")));
	}

	PrintScreenLog(FString::Printf(TEXT("   - ActiveBrushTexture: '%s' (%s)"),
		ActiveBrushTexture ? *ActiveBrushTexture->GetName() : TEXT("NONE"),
		ActiveBrushTexture ? *ActiveBrushTexture->GetClass()->GetName() : TEXT("")), FColor::White, 2.0f);

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

		const float CoveragePercent = (CanvasSize.X > 0 && CanvasSize.Y > 0) ? ((BrushSize * BrushSize) / (CanvasSize.X * CanvasSize.Y)) * 100.0f : 0.0f;
		PrintScreenLog(FString::Printf(TEXT("   - Stamping: ScreenPos=(%0.1f, %0.1f), Size=(%0.1f, %0.1f) on Canvas (%0.0fx%0.0f) [%0.3f%% coverage]"),
			ScreenPos.X, ScreenPos.Y, ScreenSize.X, ScreenSize.Y, CanvasSize.X, CanvasSize.Y, CoveragePercent), FColor::White, 2.0f);

		if (BrushMID)
		{
			Canvas->K2_DrawMaterial(BrushMID, ScreenPos, ScreenSize, FVector2D(0.f, 0.f), FVector2D(1.f, 1.f), EffectiveRotation, FVector2D(0.5f, 0.5f));
			PrintScreenLog(TEXT("   - Canvas->K2_DrawMaterial executed with BrushMID."), FColor::Emerald, 2.0f);
		}
		else if (ActiveBrushTexture)
		{
			Canvas->K2_DrawTexture(ActiveBrushTexture, ScreenPos, ScreenSize, FVector2D(0.f, 0.f), FVector2D(1.f, 1.f), PaintColor, BLEND_Translucent, EffectiveRotation, FVector2D(0.5f, 0.5f));
			PrintScreenLog(TEXT("   - Canvas->K2_DrawTexture executed with fallback texture."), FColor::Emerald, 2.0f);
		}
	}
	else
	{
		PrintScreenError(TEXT("[PaintAtUV] Canvas is null after BeginDrawCanvasToRenderTarget!"), 5.0f);
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, DrawContext);

	// Re-verify that TargetMesh still has DynamicMaterial assigned with parameter pointing to RenderTarget
	if (TargetMesh)
	{
		UMaterialInterface* CurrentSlotMat = TargetMesh->GetMaterial(MaterialSlotIndex);
		if (CurrentSlotMat != DynamicMaterial)
		{
			PrintScreenWarning(FString::Printf(TEXT("[PaintAtUV] CAUTION: Mesh slot %d material is '%s', NOT DynamicMaterial! Re-applying DynamicMaterial..."),
				MaterialSlotIndex, CurrentSlotMat ? *CurrentSlotMat->GetName() : TEXT("None")), 5.0f);
			TargetMesh->SetMaterial(MaterialSlotIndex, DynamicMaterial);
		}

		if (DynamicMaterial)
		{
			UTexture* VerifiedTex = nullptr;
			DynamicMaterial->GetTextureParameterValue(TextureParameterName, VerifiedTex);
			if (VerifiedTex != RenderTarget)
			{
				PrintScreenWarning(FString::Printf(TEXT("[PaintAtUV] CAUTION: Parameter '%s' on DynamicMaterial points to '%s', NOT RenderTarget! Re-binding..."),
					*TextureParameterName.ToString(), VerifiedTex ? *VerifiedTex->GetName() : TEXT("None")), 5.0f);
				DynamicMaterial->SetTextureParameterValue(TextureParameterName, RenderTarget);
			}
		}
	}

	// Update last painted UV coordinate for automatic direction tracking
	LastPaintedUV = UVCoordinate;
	bHasLastPaintedUV = true;
	PrintScreenLog(FString::Printf(TEXT("[PaintAtUV] === STROKE COMPLETE at UV (%0.3f, %0.3f) ==="), UVCoordinate.X, UVCoordinate.Y), FColor::Green, 2.0f);
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
		CopyTextureToTarget(CachedBaseTexture, RenderTarget);
		if (PingPongRenderTarget)
		{
			CopyTextureToTarget(CachedBaseTexture, PingPongRenderTarget);
		}
		CanvasBaseColor = (ClearColor != FLinearColor::White) ? ClearColor : FLinearColor::Black;
		PrintScreenLog(TEXT("ResetCanvas: Restored canvas to original base texture."), FColor::Green, 3.0f);
	}
	else
	{
		CanvasBaseColor = ClearColor;
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, ClearColor);
		if (PingPongRenderTarget)
		{
			UKismetRenderingLibrary::ClearRenderTarget2D(this, PingPongRenderTarget, ClearColor);
		}
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
			UTexture* MaskTex = BrushMaskTexture;
			if (!MaskTex)
			{
				MaskTex = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, TEXT("/Game/TextureDraw/BrushMask/Bake_Mask_Image.Bake_Mask_Image")));
			}
			if (!MaskTex)
			{
				MaskTex = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, TEXT("/Game/TextureDraw/Bake_Mask_Image.Bake_Mask_Image")));
			}
			if (MaskTex)
			{
				BrushMID->SetTextureParameterValue(FName("BrushTexture"), MaskTex);
				BrushMID->SetTextureParameterValue(FName("BrushMask"), MaskTex);
				BrushMID->SetTextureParameterValue(FName("Mask"), MaskTex);
				BrushMID->SetTextureParameterValue(FName("Texture"), MaskTex);
			}
			PrintScreenLog(FString::Printf(TEXT("[SetBrushMaterial] Switched brush material to '%s'."), *BrushMaterial->GetName()), FColor::Green, 3.0f);
		}
	}
	else
	{
		PrintScreenLog(TEXT("[SetBrushMaterial] Reset to default brush material (M_Brush)."), FColor::Yellow, 3.0f);
	}
}

void UTexturePaintComponent::SetBrushTexture(UTexture* InBrushTexture)
{
	BrushMaskTexture = InBrushTexture;
	PrintScreenLog(FString::Printf(TEXT("[SetBrushTexture] Updated BrushMaskTexture to: '%s' (%s)"),
		InBrushTexture ? *InBrushTexture->GetName() : TEXT("NULL"),
		InBrushTexture ? *InBrushTexture->GetClass()->GetName() : TEXT("")), FColor::Cyan, 4.0f);

	if (BrushMID && BrushMaskTexture)
	{
		BrushMID->SetTextureParameterValue(FName("BrushTexture"), BrushMaskTexture);
		BrushMID->SetTextureParameterValue(FName("BrushMask"), BrushMaskTexture);
		BrushMID->SetTextureParameterValue(FName("Mask"), BrushMaskTexture);
		BrushMID->SetTextureParameterValue(FName("Texture"), BrushMaskTexture);
	}
	if (BrushModifierMID && BrushMaskTexture)
	{
		BrushModifierMID->SetTextureParameterValue(FName("BrushMask"), BrushMaskTexture);
		BrushModifierMID->SetTextureParameterValue(FName("BrushTexture"), BrushMaskTexture);
		BrushModifierMID->SetTextureParameterValue(FName("Mask"), BrushMaskTexture);
	}
}

void UTexturePaintComponent::SetBrushMaskTexture(UTexture* InBrushMaskTexture)
{
	SetBrushTexture(InBrushMaskTexture);
}

void UTexturePaintComponent::SetCanvasModifierMaterial(UMaterialInterface* InMaterial)
{
	CanvasModifierMaterial = InMaterial;
	CanvasModifierMID = nullptr;
	PrintScreenLog(FString::Printf(TEXT("[SetCanvasModifierMaterial] Assigned '%s'."), InMaterial ? *InMaterial->GetName() : TEXT("NULL")), FColor::Cyan, 3.0f);
}

void UTexturePaintComponent::SetBrushModifierMaterial(UMaterialInterface* InMaterial)
{
	BrushModifierMaterial = InMaterial;
	BrushModifierMID = nullptr;
	PrintScreenLog(FString::Printf(TEXT("[SetBrushModifierMaterial] Assigned '%s'."), InMaterial ? *InMaterial->GetName() : TEXT("NULL")), FColor::Cyan, 3.0f);
}

void UTexturePaintComponent::SwapRenderTargets()
{
	Swap(RenderTarget, PingPongRenderTarget);
	if (DynamicMaterial && RenderTarget)
	{
		DynamicMaterial->SetTextureParameterValue(TextureParameterName, RenderTarget);
	}
	PrintScreenLog(FString::Printf(TEXT("[SwapRenderTargets] Swapped RTs. Active RenderTarget: %p, PingPong: %p"),
		(void*)RenderTarget, (void*)PingPongRenderTarget), FColor::White, 1.0f);
}

UMaterialInstanceDynamic* UTexturePaintComponent::GetOrCreateCanvasModifierMID()
{
	if (CanvasModifierMID && CanvasModifierMaterial && CanvasModifierMID->Parent != CanvasModifierMaterial)
	{
		CanvasModifierMID = nullptr;
	}

	if (!CanvasModifierMID)
	{
		UMaterialInterface* BaseMat = CanvasModifierMaterial;
		if (!BaseMat)
		{
			BaseMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/TextureDraw/M_TextureCanvas.M_TextureCanvas")));
		}
		if (!BaseMat)
		{
			BaseMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/TextureDraw/M_Brush_calculate.M_Brush_calculate")));
		}

		if (BaseMat)
		{
			CanvasModifierMID = UMaterialInstanceDynamic::Create(BaseMat, this);
		}
	}

	return CanvasModifierMID;
}

UMaterialInstanceDynamic* UTexturePaintComponent::GetOrCreateBrushModifierMID()
{
	if (BrushModifierMID && BrushModifierMaterial && BrushModifierMID->Parent != BrushModifierMaterial)
	{
		BrushModifierMID = nullptr;
	}

	if (!BrushModifierMID)
	{
		UMaterialInterface* BaseMat = BrushModifierMaterial;
		if (!BaseMat)
		{
			BaseMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/TextureDraw/M_EraseBrush.M_EraseBrush")));
		}
		if (!BaseMat)
		{
			BaseMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/TextureDraw/M_Brush_calculate.M_Brush_calculate")));
		}
		if (!BaseMat)
		{
			BaseMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/TextureDraw/M_Brush_samlie.M_Brush_samlie")));
		}
		if (BaseMat)
		{
			BrushModifierMaterial = BaseMat;
			BrushModifierMID = UMaterialInstanceDynamic::Create(BaseMat, this);
			PrintScreenLog(FString::Printf(TEXT("[GetOrCreateBrushModifierMID] Auto-loaded BrushModifierMaterial: '%s'"), *BaseMat->GetName()), FColor::Green, 3.0f);
		}
	}

	return BrushModifierMID;
}

void UTexturePaintComponent::ApplyParametersToMID(
	UMaterialInstanceDynamic* MID,
	UTexture* InCanvasTexture,
	UTexture* InBrushMask,
	const FLinearColor& InDeltas,
	const FVector2D& InUV,
	float InBrushSize,
	float InRotation,
	bool bIsGlobal)
{
	if (!MID)
	{
		return;
	}

	// 1. Canvas Texture (Source)
	if (InCanvasTexture)
	{
		static const FName CanvasNames[] = {
			FName("CanvasTexture"),
			FName("RenderTarget"),
			FName("RenderTraget"),
			FName("SourceTexture"),
			FName("BaseTexture"),
			FName("Texture"),
			FName("PaintTexture")
		};
		for (const FName& Name : CanvasNames)
		{
			MID->SetTextureParameterValue(Name, InCanvasTexture);
		}
	}

	// 2. Brush Mask Texture
	if (InBrushMask)
	{
		static const FName MaskNames[] = {
			FName("BrushMask"),
			FName("BrushTexture"),
			FName("Mask"),
			FName("Brush"),
			FName("Erase target"),
			FName("EraseTarget")
		};
		for (const FName& Name : MaskNames)
		{
			MID->SetTextureParameterValue(Name, InBrushMask);
		}
	}

	// 3. Channel Deltas / Color
	static const FName DeltaNames[] = {
		FName("ChannelDeltas"),
		FName("Deltas"),
		FName("Delta"),
		FName("PaintColor"),
		FName("Color"),
		FName("Erase"),
		FName("EraseColor"),
		FName("Tint"),
		FName("BrushColor")
	};
	for (const FName& Name : DeltaNames)
	{
		MID->SetVectorParameterValue(Name, InDeltas);
	}

	// 4. Transforms and dimensions
	MID->SetScalarParameterValue(FName("BrushRotation"), InRotation);
	MID->SetScalarParameterValue(FName("Rotation"), InRotation);
	MID->SetScalarParameterValue(FName("Angle"), InRotation);

	MID->SetScalarParameterValue(FName("BrushSize"), InBrushSize);
	MID->SetScalarParameterValue(FName("Size"), InBrushSize);
	MID->SetScalarParameterValue(FName("Radius"), InBrushSize * 0.5f);

	MID->SetScalarParameterValue(FName("bIsGlobal"), bIsGlobal ? 1.0f : 0.0f);
	MID->SetScalarParameterValue(FName("IsGlobal"), bIsGlobal ? 1.0f : 0.0f);

	const FLinearColor UVVec(InUV.X, InUV.Y, 0.f, 0.f);
	MID->SetVectorParameterValue(FName("BrushCenter"), UVVec);
	MID->SetVectorParameterValue(FName("BrushPosition"), UVVec);
	MID->SetVectorParameterValue(FName("UVCoordinate"), UVVec);
}

void UTexturePaintComponent::ModifyCanvasChannels(FLinearColor ChannelDeltas)
{
	PrintScreenLog(FString::Printf(TEXT("[ModifyCanvasChannels] Global mod: Deltas=(R=%0.3f, G=%0.3f, B=%0.3f, A=%0.3f)"),
		ChannelDeltas.R, ChannelDeltas.G, ChannelDeltas.B, ChannelDeltas.A), FColor::Cyan, 3.0f);

	if (!RenderTarget)
	{
		if (!AutoInitialize())
		{
			PrintScreenWarning(TEXT("[ModifyCanvasChannels] ABORTED: RenderTarget is null!"), 4.0f);
			return;
		}
	}

	if (ChannelDeltas.R == 0.f && ChannelDeltas.G == 0.f && ChannelDeltas.B == 0.f && ChannelDeltas.A == 0.f)
	{
		return;
	}

	if (!PingPongRenderTarget && RenderTarget)
	{
		PingPongRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
			this,
			RenderTarget->SizeX,
			RenderTarget->SizeY,
			RTF_RGBA8
		);
		CopyTextureToTarget(RenderTarget, PingPongRenderTarget);
	}

	UMaterialInstanceDynamic* ModifierMID = GetOrCreateCanvasModifierMID();
	if (!ModifierMID)
	{
		PrintScreenWarning(TEXT("[ModifyCanvasChannels] FAILED: No CanvasModifierMaterial found! Assign one or ensure /Game/TextureDraw/M_TextureCanvas exists."), 5.0f);
		return;
	}

	// Apply parameters to dynamic material
	ApplyParametersToMID(ModifierMID, RenderTarget, nullptr, ChannelDeltas, FVector2D::ZeroVector, 0.f, 0.f, true);

	// Draw full screen quad to PingPongRenderTarget
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, PingPongRenderTarget, ModifierMID);

	// Swap targets and update dynamic material on target mesh
	SwapRenderTargets();
	PrintScreenLog(TEXT("[ModifyCanvasChannels] Global canvas modification complete."), FColor::Green, 3.0f);
}

void UTexturePaintComponent::ModifySingleChannel(EPaintChannel Channel, float DeltaValue)
{
	const TCHAR* ChannelNames[] = { TEXT("Red (Heat)"), TEXT("Green (Dirt/Oil)"), TEXT("Blue (Rust)"), TEXT("Alpha"), TEXT("All") };
	const int32 ChannelIdx = static_cast<int32>(Channel);
	const TCHAR* ChannelStr = (ChannelIdx >= 0 && ChannelIdx < 5) ? ChannelNames[ChannelIdx] : TEXT("Unknown");

	PrintScreenLog(FString::Printf(TEXT("[ModifySingleChannel] Global mod: Channel=%s | Delta=%0.4f"), ChannelStr, DeltaValue), FColor::Cyan, 3.0f);

	if (DeltaValue == 0.f)
	{
		return;
	}

	FLinearColor Deltas(0.f, 0.f, 0.f, 0.f);
	switch (Channel)
	{
	case EPaintChannel::Red:
		Deltas.R = DeltaValue;
		break;
	case EPaintChannel::Green:
		Deltas.G = DeltaValue;
		break;
	case EPaintChannel::Blue:
		Deltas.B = DeltaValue;
		break;
	case EPaintChannel::Alpha:
		Deltas.A = DeltaValue;
		break;
	case EPaintChannel::All:
		Deltas = FLinearColor(DeltaValue, DeltaValue, DeltaValue, DeltaValue);
		break;
	}

	ModifyCanvasChannels(Deltas);
}

void UTexturePaintComponent::PaintChannelsAtUV(FVector2D UVCoordinate, float BrushSize, FLinearColor ChannelDeltas, float InRotationDegrees)
{
	PrintScreenLog(FString::Printf(TEXT("[PaintChannelsAtUV] UV=(%0.4f, %0.4f) | BrushSize=%0.1f | Deltas=(R=%0.3f, G=%0.3f, B=%0.3f, A=%0.3f) | Rot=%0.1f"),
		UVCoordinate.X, UVCoordinate.Y, BrushSize, ChannelDeltas.R, ChannelDeltas.G, ChannelDeltas.B, ChannelDeltas.A, InRotationDegrees),
		FColor::Cyan, 2.0f);

	if (!RenderTarget)
	{
		PrintScreenWarning(TEXT("[PaintChannelsAtUV] RenderTarget is null! Attempting AutoInitialize..."), 4.0f);
		if (!AutoInitialize())
		{
			PrintScreenError(TEXT("[PaintChannelsAtUV] ABORTED: AutoInitialize failed!"), 6.0f);
			return;
		}
	}

	if (BrushSize <= 0.0f)
	{
		PrintScreenWarning(FString::Printf(TEXT("[PaintChannelsAtUV] BrushSize %0.1f <= 0! Skipping."), BrushSize), 3.0f);
		return;
	}

	if (ChannelDeltas.R == 0.f && ChannelDeltas.G == 0.f && ChannelDeltas.B == 0.f && ChannelDeltas.A == 0.f)
	{
		PrintScreenWarning(TEXT("[PaintChannelsAtUV] All channel deltas are 0.0, nothing to modify."), 2.0f);
		return;
	}

	if (UVCoordinate.IsNearlyZero())
	{
		PrintScreenWarning(TEXT("[PaintChannelsAtUV] WARNING: UV is (0,0)! If using Line Trace, verify 'Find Collision UV' is enabled on Line Trace node and complex collision is enabled on the mesh."), 5.0f);
	}
	if (UVCoordinate.X < 0.0f || UVCoordinate.X > 1.0f || UVCoordinate.Y < 0.0f || UVCoordinate.Y > 1.0f)
	{
		PrintScreenWarning(FString::Printf(TEXT("[PaintChannelsAtUV] WARNING: UV (%0.3f, %0.3f) is outside [0.0, 1.0]!"), UVCoordinate.X, UVCoordinate.Y), 5.0f);
	}

	if (!PingPongRenderTarget && RenderTarget)
	{
		PrintScreenLog(FString::Printf(TEXT("[PaintChannelsAtUV] Allocating PingPongRenderTarget (%dx%d)..."), RenderTarget->SizeX, RenderTarget->SizeY), FColor::White, 3.0f);
		PingPongRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
			this,
			RenderTarget->SizeX,
			RenderTarget->SizeY,
			RTF_RGBA8
		);
		CopyTextureToTarget(RenderTarget, PingPongRenderTarget);
	}

	// Resolve the active brush texture from component property or default Bake_Mask_Image
	UTexture* ActiveBrushTexture = BrushMaskTexture;
	if (!ActiveBrushTexture)
	{
		ActiveBrushTexture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, TEXT("/Game/TextureDraw/BrushMask/Bake_Mask_Image.Bake_Mask_Image")));
	}
	if (!ActiveBrushTexture)
	{
		ActiveBrushTexture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, TEXT("/Game/TextureDraw/Bake_Mask_Image.Bake_Mask_Image")));
	}

	PrintScreenLog(FString::Printf(TEXT("   - ActiveBrushTexture: '%s'"), ActiveBrushTexture ? *ActiveBrushTexture->GetName() : TEXT("NONE")), FColor::White, 2.0f);

	const float EffectiveRotation = (InRotationDegrees > -900.0f) ? InRotationDegrees : BrushRotation;

	const bool bHasNegativeDelta = (ChannelDeltas.R < 0.f || ChannelDeltas.G < 0.f || ChannelDeltas.B < 0.f);

	if (bHasNegativeDelta)
	{
		UMaterialInstanceDynamic* ModifierMID = GetOrCreateBrushModifierMID();
		if (ModifierMID)
		{
			// Erase color: which channels are being erased (positive mask, e.g. 1.0 for the channel to erase)
			const FLinearColor EraseColor(
				ChannelDeltas.R < 0.f ? FMath::Abs(ChannelDeltas.R) : 0.f,
				ChannelDeltas.G < 0.f ? FMath::Abs(ChannelDeltas.G) : 0.f,
				ChannelDeltas.B < 0.f ? FMath::Abs(ChannelDeltas.B) : 0.f,
				1.0f
			);

			// Apply parameters
			ApplyParametersToMID(ModifierMID, RenderTarget, ActiveBrushTexture, EraseColor, UVCoordinate, BrushSize, EffectiveRotation, false);
			ModifierMID->SetVectorParameterValue(FName("EraseColor"), EraseColor);

			const bool bIsModulate = (ModifierMID->GetBlendMode() == BLEND_Modulate);
			UTextureRenderTarget2D* DrawTarget = bIsModulate ? RenderTarget : PingPongRenderTarget;

			if (!bIsModulate)
			{
				CopyTextureToTarget(RenderTarget, PingPongRenderTarget);
			}

			PrintScreenLog(FString::Printf(TEXT("   - ERASE PATH: %s | EraseColor=(R=%0.2f, G=%0.2f, B=%0.2f)"),
				bIsModulate ? TEXT("Modulate Blend (Direct, Zero Hazard)") : TEXT("Ping-Pong Subtraction"),
				EraseColor.R, EraseColor.G, EraseColor.B), FColor::Emerald, 2.0f);

			UCanvas* Canvas = nullptr;
			FVector2D CanvasSize;
			FDrawToRenderTargetContext DrawContext;
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, DrawTarget, Canvas, CanvasSize, DrawContext);

			if (Canvas)
			{
				const FVector2D ScreenPos = (UVCoordinate * CanvasSize) - FVector2D(BrushSize * 0.5f, BrushSize * 0.5f);
				const FVector2D ScreenSize(BrushSize, BrushSize);

				PrintScreenLog(FString::Printf(TEXT("   - Erase quad: Pos=(%0.1f, %0.1f), Size=(%0.1f, %0.1f) on Canvas (%0.0fx%0.0f)"),
					ScreenPos.X, ScreenPos.Y, ScreenSize.X, ScreenSize.Y, CanvasSize.X, CanvasSize.Y), FColor::White, 2.0f);

				Canvas->K2_DrawMaterial(
					ModifierMID,
					ScreenPos,
					ScreenSize,
					FVector2D(0.f, 0.f),
					FVector2D(1.f, 1.f),
					EffectiveRotation,
					FVector2D(0.5f, 0.5f)
				);
			}
			else
			{
				PrintScreenError(TEXT("[PaintChannelsAtUV] Canvas is null after BeginDrawCanvasToRenderTarget!"), 5.0f);
			}
			UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, DrawContext);

			if (!bIsModulate)
			{
				SwapRenderTargets();
			}

			PrintScreenLog(TEXT("[PaintChannelsAtUV] Erase stroke complete!"), FColor::Green, 2.0f);
		}
		else
		{
			PrintScreenWarning(TEXT("[PaintChannelsAtUV] WARNING: Negative delta (erasing) requested, but BrushModifierMaterial is not assigned! Assign M_Brush_calculate to BrushModifierMaterial for erasing."), 6.0f);
		}
	}
	else
	{
		// Painting path: Native Additive blend mode using M_Brush
		PrintScreenLog(TEXT("   - PAINT PATH: Using M_Brush with Additive blending."), FColor::Cyan, 2.0f);
		FLinearColor PaintColor = ChannelDeltas;
		if (PaintColor.A <= 0.0f)
		{
			PaintColor.A = 1.0f;
		}

		PaintAtUV(UVCoordinate, BrushSize, PaintColor, InRotationDegrees);
	}
}

void UTexturePaintComponent::PaintSingleChannelAtUV(FVector2D UVCoordinate, float BrushSize, EPaintChannel Channel, float DeltaValue, float InRotationDegrees)
{
	const TCHAR* ChannelNames[] = { TEXT("Red (Heat)"), TEXT("Green (Dirt/Oil)"), TEXT("Blue (Rust)"), TEXT("Alpha"), TEXT("All") };
	const int32 ChannelIdx = static_cast<int32>(Channel);
	const TCHAR* ChannelStr = (ChannelIdx >= 0 && ChannelIdx < 5) ? ChannelNames[ChannelIdx] : TEXT("Unknown");

	PrintScreenLog(FString::Printf(TEXT("[PaintSingleChannelAtUV] Hit: UV=(%0.4f, %0.4f) | Channel=%s | Delta=%0.4f | BrushSize=%0.1f | Rot=%0.1f"),
		UVCoordinate.X, UVCoordinate.Y, ChannelStr, DeltaValue, BrushSize, InRotationDegrees),
		FColor::Cyan, 2.0f);

	if (DeltaValue == 0.f)
	{
		PrintScreenWarning(TEXT("[PaintSingleChannelAtUV] DeltaValue is 0.0, nothing to modify."), 2.0f);
		return;
	}

	FLinearColor Deltas(0.f, 0.f, 0.f, 1.0f);
	switch (Channel)
	{
	case EPaintChannel::Red:
		Deltas.R = DeltaValue;
		break;
	case EPaintChannel::Green:
		Deltas.G = DeltaValue;
		break;
	case EPaintChannel::Blue:
		Deltas.B = DeltaValue;
		break;
	case EPaintChannel::Alpha:
		Deltas.A = DeltaValue;
		break;
	case EPaintChannel::All:
		Deltas = FLinearColor(DeltaValue, DeltaValue, DeltaValue, 1.0f);
		break;
	}

	PrintScreenLog(FString::Printf(TEXT("   -> Converted Deltas: (R=%0.4f, G=%0.4f, B=%0.4f, A=%0.4f)"),
		Deltas.R, Deltas.G, Deltas.B, Deltas.A), FColor::White, 2.0f);

	PaintChannelsAtUV(UVCoordinate, BrushSize, Deltas, InRotationDegrees);
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



