#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "TexturePaintComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture;
class UTexture2D;
class UTexturePaintComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPaintedPercentageCalculated, float, Percentage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCanvasPixelsReady, const TArray<FColor>&, Pixels, int32, Width, int32, Height);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnAsyncCanvasPixelsOutput, const TArray<FColor>&, Pixels, int32, Width, int32, Height, int32, TotalPixels);

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
	UFUNCTION(BlueprintCallable, Category = "Painting", meta = (AdvancedDisplay = "InMaterialSlotIndex, InOverrideBaseTexture"))
	void SetupPainting(
		UStaticMeshComponent* InMeshComponent,
		UMaterialInterface* InBaseMaterial,
		FName InTextureParameterName = FName("PaintTexture"),
		int32 InMaterialSlotIndex = 0,
		UTexture* InOverrideBaseTexture = nullptr
	);

	/** Perform a paint stroke at a specific UV coordinate, with optional rotation override */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void PaintAtUV(FVector2D UVCoordinate, float BrushSize = 50.0f, FLinearColor PaintColor = FLinearColor::Red, float InRotationDegrees = -999.0f);

	/** Performs a paint stroke oriented along a specific 2D direction vector (ideal for Tick when you have movement delta) */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void PaintAtUVWithDirection(FVector2D UVCoordinate, FVector2D Direction, float BrushSize = 50.0f, FLinearColor PaintColor = FLinearColor::Red, float AngleOffsetDegrees = 0.0f);

	/** Performs a paint stroke and rotates the brush along the stroke path based on movement speed (scales turn speed by velocity, stops rotating when stationary) */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void PaintAtUVAutoOrient(FVector2D UVCoordinate, float BrushSize = 50.0f, FLinearColor PaintColor = FLinearColor::Red, float AngleOffsetDegrees = 0.0f, float DeltaTime = 0.0f);

	/** Sets the brush rotation angle directly in degrees */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void SetBrushRotation(float InRotationDegrees);

	/** Sets the brush rotation from a 2D direction vector (e.g. UV delta, mouse velocity) with optional angle offset */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void SetBrushRotationFromDirection2D(FVector2D Direction, float AngleOffsetDegrees = 0.0f);

	/** Sets the brush rotation from a 3D direction vector (e.g. actor velocity, trace delta) with optional angle offset */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void SetBrushRotationFromDirection(FVector Direction, float AngleOffsetDegrees = 0.0f);

	/** Sets or changes the brush material used for painting at runtime */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void SetBrushMaterial(UMaterialInterface* InBrushMaterial);

	/** Sets or changes the brush texture (mask) used for painting at runtime */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void SetBrushTexture(UTexture* InBrushTexture);

	/** Sets or changes the brush mask texture used for painting at runtime (alias for SetBrushTexture) */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void SetBrushMaskTexture(UTexture* InBrushMaskTexture);

	/** Clears or resets the Render Target canvas back to the base texture or specified clear color */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void ResetCanvas(FLinearColor ClearColor = FLinearColor::White, bool bRestoreBaseTexture = true);

	/** Returns the active Render Target */
	UFUNCTION(BlueprintPure, Category = "Painting")
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	/** Returns the dynamic material instance applied to the mesh */
	UFUNCTION(BlueprintPure, Category = "Painting")
	UMaterialInstanceDynamic* GetDynamicMaterial() const { return DynamicMaterial; }

	/** Sets or changes the texture parameter name to paint on (e.g. "PaintTexture", "Roughness", "BaseColor") and updates the dynamic material */
	UFUNCTION(BlueprintCallable, Category = "Painting")
	void SetTextureParameterName(FName InParameterName);

	/** Returns the texture parameter name currently painted on */
	UFUNCTION(BlueprintPure, Category = "Painting")
	FName GetTextureParameterName() const { return TextureParameterName; }

	/** Event triggered when asynchronous painted percentage calculation finishes */
	UPROPERTY(BlueprintAssignable, Category = "Painting|Stats")
	FOnPaintedPercentageCalculated OnPaintedPercentageCalculated;

	/**
	 * Asynchronously calculates the percentage of the canvas painted on a background worker thread.
	 * When finished, broadcasts the result via OnPaintedPercentageCalculated and updates LastPaintedPercentage.
	 * @param TargetColor Optional specific color to measure (e.g. Red). Leave Alpha=0 to measure ANY paint.
	 * @param ColorTolerance Color matching tolerance between 0.0 and 1.0 (default 0.1).
	 * @param SampleStep Step size for sampling (1 = every pixel, 2 = every 4th pixel, 4 = every 16th pixel).
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Stats")
	void CalculatePaintedPercentageAsync(FLinearColor TargetColor = FLinearColor(0.f, 0.f, 0.f, 0.f), float ColorTolerance = 0.1f, int32 SampleStep = 1);

	/** Returns true if a background thread is currently calculating the painted percentage */
	UFUNCTION(BlueprintPure, Category = "Painting|Stats")
	bool IsCalculatingPercentage() const { return bIsCalculatingPercentage; }

	/**
	 * Synchronously calculates the percentage of the canvas that has been painted (0.0% to 100.0%).
	 * If TargetColor has Alpha > 0, counts pixels matching TargetColor within ColorTolerance.
	 * If TargetColor has Alpha == 0, counts all pixels that differ from the initial canvas base color.
	 * @param TargetColor Optional specific color to measure (e.g. Red). Leave Alpha=0 to measure ANY paint.
	 * @param ColorTolerance Color matching tolerance between 0.0 and 1.0 (default 0.1).
	 * @param SampleStep Step size for fast sampling (1 = every pixel, 2 = every 4th pixel, 4 = every 16th pixel).
	 * @return Percentage of the canvas painted from 0.0% to 100.0%.
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Stats")
	float CalculatePaintedPercentage(FLinearColor TargetColor = FLinearColor(0.f, 0.f, 0.f, 0.f), float ColorTolerance = 0.1f, int32 SampleStep = 2);

	/** Returns the most recently calculated painted percentage without running a GPU readback */
	UFUNCTION(BlueprintPure, Category = "Painting|Stats")
	float GetLastCalculatedPercentage() const { return LastPaintedPercentage; }

	/** Dispatches async calculation and invokes callback on the Game Thread when complete */
	void CalculatePaintedPercentageInternal(FLinearColor TargetColor, float ColorTolerance, int32 SampleStep, TFunction<void(float)> OnCompleteCallback = nullptr);

	/** Event triggered when asynchronous GPU pixel extraction finishes */
	UPROPERTY(BlueprintAssignable, Category = "Painting|Stats")
	FOnCanvasPixelsReady OnCanvasPixelsReady;

	/**
	 * Asynchronously extracts all pixel colors from the GPU Render Target without freezing the game thread.
	 * When complete, broadcasts OnCanvasPixelsReady with the pixel array and dimensions.
	 * @param SampleStep Step size (1 = all pixels, 2 = 1/4th pixels, 4 = 1/16th pixels for fast BP loops).
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Stats")
	void GetCanvasPixelsAsync(int32 SampleStep = 1);

	/**
	 * Synchronously extracts all pixel colors from the Render Target immediately.
	 * Note: Can cause a short render pipeline stall on large textures; prefer GetCanvasPixelsAsync for smooth gameplay.
	 * @param OutPixels Extracted pixel array in FColor format (0-255 R, G, B, A).
	 * @param OutWidth Width of the returned pixel grid.
	 * @param OutHeight Height of the returned pixel grid.
	 * @param SampleStep Step size (1 = all pixels, 2 = 1/4th pixels, 4 = 1/16th pixels).
	 * @return True if pixels were successfully retrieved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Stats")
	bool GetCanvasPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, int32 SampleStep = 1);

	/** Internal handler for GPU texture readback to extract pixel array asynchronously */
	void GetCanvasPixelsInternal(int32 SampleStep, TFunction<void(TArray<FColor>, int32, int32)> OnCompleteCallback = nullptr);

	/** If true, automatically attempts to find the owner's StaticMeshComponent and initialize on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	bool bAutoInitializeOnBeginPlay;

	/** If true, prints all setup, stroke, warning, and error messages to the on-screen viewport and log. Disabled by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting|Debug")
	bool bEnableScreenLogging = false;

	/**
	 * Sets debug logging on or off. When false, stroke printing, readback info, and diagnostics are muted.
	 * @param bEnable If true, logs are printed; if false, logs are muted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Debug")
	void SetDebugLogging(bool bEnable);

	/**
	 * Turns debug logging ON. Shortcut for SetDebugLogging(true).
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Debug")
	void EnableDebug();

	/**
	 * Turns debug logging OFF. Shortcut for SetDebugLogging(false).
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Debug")
	void DisableDebug();

	/**
	 * Toggles all debug / on-screen logging on or off.
	 * @return The new state of debug logging (true = enabled, false = disabled).
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Debug")
	bool ToggleDebug();

	/**
	 * Toggles on-screen debug logging on or off.
	 * @return The new state of on-screen logging (true = enabled, false = disabled).
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Debug")
	bool ToggleScreenLogging();

	/**
	 * Enables or disables on-screen debug logging.
	 * @param bEnable If true, logs are displayed on screen; if false, logs are muted from the screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Painting|Debug")
	void SetEnableScreenLogging(bool bEnable);

	/** Returns true if on-screen debug logging is currently enabled */
	UFUNCTION(BlueprintPure, Category = "Painting|Debug")
	bool IsScreenLoggingEnabled() const { return bEnableScreenLogging; }

	/** Returns true if debug logging is currently enabled (alias for IsScreenLoggingEnabled) */
	UFUNCTION(BlueprintPure, Category = "Painting|Debug")
	bool IsDebugLoggingEnabled() const { return bEnableScreenLogging; }

	/** Current brush rotation angle in degrees (0 = default unrotated) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting|Rotation")
	float BrushRotation;

	/** Minimum UV speed (units/sec) below which the brush will not orient (stops rotating when stationary) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting|Rotation")
	float MinOrientSpeed;

	/** Rotation interpolation speed when moving slowly (lower = smoother, more gradual turn) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting|Rotation")
	float MinRotationInterpSpeed;

	/** Rotation interpolation speed when moving quickly (higher = faster, snappier turn) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting|Rotation")
	float MaxRotationInterpSpeed;

	/** UV speed threshold (units/sec) considered 'fast' movement for maximum rotation responsiveness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting|Rotation")
	float MaxSpeedThreshold;

	/** Configurable brush material (defaults to /Game/TextureDraw/M_Brush if null) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	UMaterialInterface* BrushMaterial;

	/** Configurable brush mask texture (defaults to /Game/TextureDraw/Bake_Mask_Image if null) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	UTexture* BrushMaskTexture;

	/** Name of the texture parameter inside the material to bind the Render Target to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	FName TextureParameterName;

	/** Material slot index on the static mesh to apply the dynamic material to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	int32 MaterialSlotIndex;

	/** Fallback resolution for the Render Target if no base texture can be extracted */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting")
	int32 DefaultRenderTargetResolution;

	/** The most recently calculated painted coverage percentage (0.0% to 100.0%) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Painting|Stats")
	float LastPaintedPercentage;

	/** If true, a background worker thread is currently calculating the painted percentage */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Painting|Stats")
	bool bIsCalculatingPercentage;

	/** Base clear color of the canvas, used to detect painted pixels */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Painting|Stats")
	FLinearColor CanvasBaseColor;

protected:
	virtual void BeginPlay() override;

private:
	/** Attempts to automatically locate owner's mesh and material to initialize */
	bool AutoInitialize();

	/** On-screen viewport and output log helper functions */
	void PrintScreenLog(const FString& Message, FColor Color = FColor::Cyan, float Duration = 5.0f, uint64 Key = (uint64)-1);
	void PrintScreenWarning(const FString& Message, float Duration = 5.0f, uint64 Key = (uint64)-1);
	void PrintScreenError(const FString& Message, float Duration = 6.0f, uint64 Key = (uint64)-1);

	/** Extracts the base texture from the given material */
	UTexture* ExtractBaseTexture(UMaterialInterface* InMaterial, FName InParamName);

	/** Copies a source texture across the entire Render Target canvas */
	void CopyTextureToRenderTarget(UTexture* InTexture);

	/** Tracks the last painted UV coordinate for automatic direction calculation */
	FVector2D LastPaintedUV;
	bool bHasLastPaintedUV;

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

/**
 * Asynchronous Blueprint action node to calculate painted canvas percentage on a worker thread.
 */
UCLASS()
class MARRYBYMIDNIGHT_API UAsyncCalculatePaintedPercentage : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Fires when the background thread completes calculating the painted percentage */
	UPROPERTY(BlueprintAssignable)
	FOnPaintedPercentageCalculated OnCompleted;

	/**
	 * Asynchronously calculates the percentage of the canvas painted on a background worker thread.
	 * Provides an 'On Completed' execution output pin with the calculated Percentage value.
	 * @param PaintComponent The TexturePaintComponent to calculate coverage for.
	 * @param TargetColor Optional specific color to measure (e.g. Red). Leave Alpha=0 to measure ANY paint.
	 * @param ColorTolerance Color matching tolerance between 0.0 and 1.0 (default 0.1).
	 * @param SampleStep Step size for sampling (1 = every pixel, 2 = every 4th pixel, 4 = every 16th pixel).
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DefaultToSelf = "PaintComponent"), Category = "Painting|Stats")
	static UAsyncCalculatePaintedPercentage* AsyncCalculatePaintedPercentage(
		UObject* WorldContextObject,
		UTexturePaintComponent* PaintComponent,
		FLinearColor TargetColor = FLinearColor(0.f, 0.f, 0.f, 0.f),
		float ColorTolerance = 0.1f,
		int32 SampleStep = 1);

	virtual void Activate() override;

private:
	TWeakObjectPtr<UTexturePaintComponent> TargetComponent;
	FLinearColor ColorToMatch;
	float Tolerance;
	int32 Step;
};

/**
 * Asynchronous Blueprint action node to read all pixel colors directly from GPU without stalling the game thread.
 */
UCLASS()
class MARRYBYMIDNIGHT_API UAsyncGetCanvasPixels : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Fires when GPU readback is complete, providing the array of pixel colors to Blueprint */
	UPROPERTY(BlueprintAssignable)
	FOnAsyncCanvasPixelsOutput OnCompleted;

	/**
	 * Reads all pixel colors from the GPU Render Target asynchronously on a background thread.
	 * Provides 'On Completed' execution output pin with the pixel array, width, height, and total count.
	 * @param PaintComponent The TexturePaintComponent to extract pixels from.
	 * @param SampleStep Step size (1 = full 1024x1024, 2 = 512x512, 4 = 256x256). For Blueprint ForEach loops, 2 or 4 is recommended to prevent Blueprint loop limits.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DefaultToSelf = "PaintComponent"), Category = "Painting|Stats")
	static UAsyncGetCanvasPixels* AsyncGetCanvasPixels(
		UObject* WorldContextObject,
		UTexturePaintComponent* PaintComponent,
		int32 SampleStep = 1);

	virtual void Activate() override;

private:
	TWeakObjectPtr<UTexturePaintComponent> TargetComponent;
	int32 Step;
};

