// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAppearanceRenderBackend.h"

#include "Paper2DPlusAppearanceRenderPolicy.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSpriteMaterialContract.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "CommonRenderResources.h"
// IsFeatureLevelSupported is only transitively included on some engines (C3861 on 5.2 without this).
// Its home header exists only on 5.1+ (fatal C1083 on 5.0, where RHI.h supplies it transitively) -
// __has_include is the version-drift-proof gate for the ShouldCompilePermutation feature-level checks.
#if __has_include("DataDrivenShaderPlatformInfo.h")
#include "DataDrivenShaderPlatformInfo.h"
#endif
#include "Components/StaticMeshComponent.h"
#include "DynamicRHI.h"
#include "EngineGlobals.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "GlobalShader.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/Crc.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/SecureHash.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ScreenRendering.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if UE_VERSION_OLDER_THAN(5, 8, 0)
#include "RendererInterface.h"
#else
#include "PooledRenderTarget.h"
#endif

class FPaper2DPlusAppearanceDrawVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPaper2DPlusAppearanceDrawVS);
	SHADER_USE_PARAMETER_STRUCT(FPaper2DPlusAppearanceDrawVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	END_SHADER_PARAMETER_STRUCT()

	// The compositor runs on GMaxRHIFeatureLevel with PF_FloatRGBA RDG raster passes and has no mobile/ES3.1
	// path (the plugin's PlatformAllowList is Win64/Mac/Linux). Gate to SM5 so these shaders are not compiled
	// for feature levels the backend never uses. IsFeatureLevelSupported/ERHIFeatureLevel::SM5 exist 5.0-5.8.
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FPaper2DPlusAppearanceDrawPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPaper2DPlusAppearanceDrawPS);
	SHADER_USE_PARAMETER_STRUCT(FPaper2DPlusAppearanceDrawPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PaletteTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, PaletteSampler)
		SHADER_PARAMETER(uint32, OpacityMode)
		SHADER_PARAMETER(float, OpacityMaskClipValue)
		SHADER_PARAMETER(uint32, RecolorEnabled)
		SHADER_PARAMETER(float, PaletteRow)
		SHADER_PARAMETER(float, PaletteInvHeight)
		SHADER_PARAMETER(float, RecolorIntensity)
		RDG_BUFFER_ACCESS(VertexBuffer, ERHIAccess::VertexOrIndexBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	// SM5 floor: see FPaper2DPlusAppearanceDrawVS. The compositor never runs below SM5.
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FPaper2DPlusAppearanceResolvePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPaper2DPlusAppearanceResolvePS);
	SHADER_USE_PARAMETER_STRUCT(FPaper2DPlusAppearanceResolvePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	// SM5 floor: see FPaper2DPlusAppearanceDrawVS. The compositor never runs below SM5.
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FPaper2DPlusAppearanceDrawVS,
	"/Plugin/Paper2DPlus/Private/Paper2DPlusAppearanceComposite.usf",
	"DrawMainVS",
	SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(
	FPaper2DPlusAppearanceDrawPS,
	"/Plugin/Paper2DPlus/Private/Paper2DPlusAppearanceComposite.usf",
	"DrawMainPS",
	SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(
	FPaper2DPlusAppearanceResolvePS,
	"/Plugin/Paper2DPlus/Private/Paper2DPlusAppearanceComposite.usf",
	"ResolveMainPS",
	SF_Pixel);

struct FPaper2DPlusAppearanceBuildSubmission::FCompletionState
{
	FCompletionState()
		: bRenderCommandComplete(false)
		, bSucceeded(false)
	{
	}

	mutable FCriticalSection FenceMutex;
	FGPUFenceRHIRef GPUFence;
	TAtomic<bool> bRenderCommandComplete;
	TAtomic<bool> bSucceeded;
};

FPaper2DPlusAppearanceBuildSubmission::FPaper2DPlusAppearanceBuildSubmission(
	UTextureRenderTarget2D* InOutputTarget)
	: OutputTarget(InOutputTarget)
	, CompletionState(MakeShared<FCompletionState, ESPMode::ThreadSafe>())
{
}

bool FPaper2DPlusAppearanceBuildSubmission::IsRenderCommandComplete() const
{
	return CompletionState->bRenderCommandComplete.Load();
}

bool FPaper2DPlusAppearanceBuildSubmission::DidRenderCommandSucceed() const
{
	return IsRenderCommandComplete() && CompletionState->bSucceeded.Load();
}

bool FPaper2DPlusAppearanceBuildSubmission::IsComplete() const
{
	if (!IsRenderCommandComplete())
	{
		return false;
	}
	if (!DidRenderCommandSucceed())
	{
		return true; // a render-command failure is terminal and lets the cache retire the target
	}
	FScopeLock Lock(&CompletionState->FenceMutex);
	return CompletionState->GPUFence.IsValid() && CompletionState->GPUFence->Poll();
}

bool FPaper2DPlusAppearanceBuildSubmission::DidRenderSucceed() const
{
	return IsComplete() && DidRenderCommandSucceed();
}

#if WITH_DEV_AUTOMATION_TESTS
bool FPaper2DPlusAppearanceBuildSubmission::DidRenderSucceedAfterGpuIdleForTests() const
{
	// D3D11 uses FGenericRHIGPUFence through UE 5.5. Its Poll() intentionally waits for later render-frame
	// numbers even after BlockUntilGPUIdle. A synchronous proof callback cannot advance real engine frames;
	// once its explicit GPU-idle command and render-command flush return, command success is authoritative.
	return DidRenderCommandSucceed();
}
#endif

namespace
{
	class FAppearanceKeyWriter
	{
	public:
		void Byte(uint8 Value) { Bytes.Add(Value); }

		void UInt32(uint32 Value)
		{
			for (int32 Shift = 0; Shift < 32; Shift += 8)
			{
				Bytes.Add(static_cast<uint8>((Value >> Shift) & 0xff));
			}
		}

		void Int32(int32 Value) { UInt32(static_cast<uint32>(Value)); }
		void Float(float Value) { UInt32(FPlatformMath::AsUInt(Value)); }

		void Guid(const FGuid& Value)
		{
			UInt32(Value.A); UInt32(Value.B); UInt32(Value.C); UInt32(Value.D);
		}

		void String(const FString& Value)
		{
			FTCHARToUTF8 Utf8(*Value);
			UInt32(static_cast<uint32>(Utf8.Length()));
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}

		void ObjectIdentity(const UObject* Object)
		{
			if (!Object)
			{
				String(FString());
				String(FString());
				return;
			}
			String(FSoftObjectPath(Object).ToString());
			// Package persistent GUIDs are editor-only. The runtime cache is transient, while the
			// full recipe below already serializes every render-affecting revision, geometry, and
			// material input. Pair the soft identity with its class so this seam remains available
			// in cooked game targets without weakening type separation.
			String(Object->GetClass()->GetPathName());
		}

		TArray<uint8> Bytes;
	};

	void AddDependency(const UObject* Object, TSet<FSoftObjectPath>& OutObjects, TSet<FName>& OutPackages)
	{
		if (!Object)
		{
			return;
		}
		OutObjects.Add(FSoftObjectPath(Object));
		if (const UPackage* Package = Object->GetOutermost())
		{
			// Package fallback is for persistent asset packages whose sibling edits may revise shared source state.
			// The global transient package is a process-wide bucket: actors, components, render targets, and unrelated
			// editor fixtures all live there. Treating it (or another explicitly transient package) as one dependency
			// domain makes any later Modify() invalidate every transient appearance recipe. Exact object paths remain
			// tracked above, so a real edit to a transient dependency still invalidates only its consumers.
			if (Package != GetTransientPackage() && !Package->HasAnyFlags(RF_Transient))
			{
				OutPackages.Add(Package->GetFName());
			}
		}
	}

	void WriteTexture(FAppearanceKeyWriter& Writer, const UTexture* Texture)
	{
		Writer.ObjectIdentity(Texture);
		Writer.Guid(Texture ? Texture->GetLightingGuid() : FGuid());
	}

	void WriteMaterial(FAppearanceKeyWriter& Writer, const UMaterialInterface* Material)
	{
		Writer.ObjectIdentity(Material);
		const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
		Writer.ObjectIdentity(Base);
		Writer.Guid(Base ? Base->StateId : FGuid());
	}

	void WriteAppearance(FAppearanceKeyWriter& Writer, FPaper2DPlusAppearanceDescriptor Appearance)
	{
			Paper2DPlusAppearanceResolver::Normalize(Appearance);
			Writer.Int32(Appearance.SemanticVersion);
			Writer.Byte(static_cast<uint8>(Appearance.DeliveryMode));
			Writer.Int32(Appearance.ActiveLayerIds.Num());
			for (const FGuid& LayerId : Appearance.ActiveLayerIds)
			{
				Writer.Guid(LayerId);
			}
	}

	void WriteSprite(FAppearanceKeyWriter& Writer, const UPaperSprite* Sprite)
	{
		Writer.ObjectIdentity(Sprite);
		if (!Sprite)
		{
			return;
		}
		WriteTexture(Writer, Sprite->GetBakedTexture());
		FAdditionalSpriteTextureArray AdditionalTextures;
		Sprite->GetBakedAdditionalSourceTextures(AdditionalTextures);
		Writer.Int32(AdditionalTextures.Num());
		for (const UTexture* Texture : AdditionalTextures)
		{
			WriteTexture(Writer, Texture);
		}
		WriteMaterial(Writer, Sprite->GetDefaultMaterial());
		WriteMaterial(Writer, Sprite->GetAlternateMaterial());
		Writer.Int32(Sprite->AlternateMaterialSplitIndex);
		Writer.Float(Sprite->GetPixelsPerUnrealUnit());
		Writer.Int32(Sprite->BakedRenderData.Num());
		for (const FVector4& Vertex : Sprite->BakedRenderData)
		{
			Writer.Float(static_cast<float>(Vertex.X));
			Writer.Float(static_cast<float>(Vertex.Y));
			Writer.Float(static_cast<float>(Vertex.Z));
			Writer.Float(static_cast<float>(Vertex.W));
		}
	}

	enum class EPreparedOpacityMode : uint32
	{
		Opaque = 0,
		Masked = 1,
		Translucent = 2
	};

	struct FPreparedAppearanceDraw
	{
		TArray<FFilterVertex> Vertices;
		FTextureRHIRef SourceTexture;
		FSamplerStateRHIRef SourceSampler;
		FTextureRHIRef PaletteTexture;
		EPreparedOpacityMode OpacityMode = EPreparedOpacityMode::Translucent;
		float OpacityMaskClipValue = 0.3333f;
		float PaletteRow = 0.0f;
		float PaletteInvHeight = 1.0f;
		float RecolorIntensity = 0.0f;
		bool bRecolorEnabled = false;
	};

	FPaper2DPlusAppearanceScratchStats GAppearanceScratchStats;
#if WITH_EDITOR
	FPaper2DPlusAppearanceValidationStats GAppearanceValidationStats;
#endif
	uint64 GAppearanceScratchStatsFrame = MAX_uint64;
	uint64 GAppearanceScratchBytesThisFrame = 0;
	int32 GAppearanceScratchRequestsThisFrame = 0;

	UTextureRenderTarget2D* CreateAppearanceTarget(
		UObject* Outer,
		const FIntPoint& Size,
		ETextureRenderTargetFormat Format)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(
			Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient | RF_DuplicateTransient);
		Target->RenderTargetFormat = Format;
		Target->ClearColor = FLinearColor::Transparent;
		Target->bAutoGenerateMips = false;
		Target->Filter = TF_Nearest;
		Target->InitAutoFormat(Size.X, Size.Y);
		// The compositor's final fullscreen resolve overwrites every output pixel. Resource creation only
		// needs to allocate the target; clearing here would be redundant GPU work before that resolve.
		Target->UpdateResourceImmediate(/*bClearRenderTarget=*/false);
		return Target;
	}

	bool ResolveOpacityMode(
		const UMaterialInterface* Material,
		bool bRecolorContract,
		EPreparedOpacityMode& OutMode,
		float& OutMaskClip,
		FString& OutError)
	{
		EPaper2DPlusSpriteOpacityMode ContractMode;
		if (!Paper2DPlusSpriteMaterialContract::ResolveOpacityMode(
			Material,
			/*bRequireKnownPaper2DMaterial=*/!bRecolorContract,
			ContractMode,
			OutMaskClip,
			OutError))
		{
			return false;
		}

		switch (ContractMode)
		{
		case EPaper2DPlusSpriteOpacityMode::Opaque:
			OutMode = EPreparedOpacityMode::Opaque;
			break;
		case EPaper2DPlusSpriteOpacityMode::Masked:
			OutMode = EPreparedOpacityMode::Masked;
			break;
		case EPaper2DPlusSpriteOpacityMode::Translucent:
			OutMode = EPreparedOpacityMode::Translucent;
			break;
		default:
			OutError = TEXT("The resolved sprite opacity mode is unsupported.");
			return false;
		}
		return true;
	}

	bool ValidateRecipeHeader(
		const FPaper2DPlusAppearanceBuildRecipe& Recipe,
		FString& OutError)
	{
		if (!Recipe.LayerAsset || !Recipe.CharacterProfile || !Recipe.Flipbook)
		{
			OutError = TEXT("Layer asset, character profile, and canonical flipbook must all be resident at the request boundary.");
			return false;
		}
		if (Recipe.IndependentChannelLayerNames.Num() > FPaper2DPlusAppearanceRenderPolicy::MaxIndependentChannels)
		{
			OutError = FString::Printf(TEXT("At most %d independent channels may bypass the base composite."),
				FPaper2DPlusAppearanceRenderPolicy::MaxIndependentChannels);
			return false;
		}
		if (Recipe.Frames.Num() <= 0)
		{
			OutError = TEXT("The canonical animation has no key frames to prepare.");
			return false;
		}
		if (Recipe.PixelSize.X <= 0 || Recipe.PixelSize.Y <= 0
			|| Recipe.PixelSize.X > FPaper2DPlusAppearanceRenderBackend::MaxCompositeDimension
			|| Recipe.PixelSize.Y > FPaper2DPlusAppearanceRenderBackend::MaxCompositeDimension)
		{
			OutError = FString::Printf(
				TEXT("Composite dimensions must be between 1 and %d pixels."),
				FPaper2DPlusAppearanceRenderBackend::MaxCompositeDimension);
			return false;
		}
		if (!FMath::IsFinite(Recipe.PixelsPerUnrealUnit) || Recipe.PixelsPerUnrealUnit <= 0.0f)
		{
			OutError = TEXT("Composite pixels-per-unit must be finite and positive.");
			return false;
		}
		return true;
	}

	bool ValidateFrameContents(
		const FPaper2DPlusAppearanceFrameRecipe& Frame,
		FString& OutError)
	{
		for (const FPaper2DPlusAppearanceLayerDraw& Draw : Frame.BaseDraws)
		{
			if (!Draw.Sprite || !Draw.Sprite->GetBakedTexture()
				|| Draw.Sprite->BakedRenderData.Num() == 0
				|| Draw.Sprite->BakedRenderData.Num() % 3 != 0)
			{
				OutError = FString::Printf(TEXT("Layer '%s' has no resident cooked sprite triangles."), *Draw.LayerName);
				return false;
			}
			FAdditionalSpriteTextureArray AdditionalTextures;
			Draw.Sprite->GetBakedAdditionalSourceTextures(AdditionalTextures);
			if (AdditionalTextures.Num() > 0)
			{
				OutError = FString::Printf(
					TEXT("Layer '%s' uses additional sprite textures whose opacity/color behavior cannot be proven exactly."),
					*Draw.LayerName);
				return false;
			}
			if (Draw.bRecolorEnabled && !Draw.RecolorMaterial)
			{
				OutError = FString::Printf(TEXT("Layer '%s' requests recolor without a resident recolor material."), *Draw.LayerName);
				return false;
			}
			if (Draw.bRecolorEnabled && !Draw.bCompositeRecolorContractVerified)
			{
				OutError = FString::Printf(
					TEXT("Layer '%s' recolor has not opted into the composite luminance/LUT/unchanged-alpha contract."),
					*Draw.LayerName);
				return false;
			}
			if (Draw.bRecolorEnabled && !Draw.PaletteLUT)
			{
				OutError = FString::Printf(TEXT("Layer '%s' requests recolor without a resident PaletteLUT."), *Draw.LayerName);
				return false;
			}

			EPreparedOpacityMode IgnoredMode;
			float IgnoredClip = 0.0f;
			UMaterialInterface* DefaultMaterial = Draw.bRecolorEnabled
				? Draw.RecolorMaterial.Get() : Draw.Sprite->GetDefaultMaterial();
			if (!ResolveOpacityMode(
				DefaultMaterial, Draw.bRecolorEnabled, IgnoredMode, IgnoredClip, OutError))
			{
				OutError = FString::Printf(TEXT("Layer '%s': %s"), *Draw.LayerName, *OutError);
				return false;
			}
			if (Draw.Sprite->AlternateMaterialSplitIndex != INDEX_NONE
				&& !ResolveOpacityMode(
					Draw.Sprite->GetAlternateMaterial(), false, IgnoredMode, IgnoredClip, OutError))
			{
				OutError = FString::Printf(TEXT("Layer '%s' alternate section: %s"), *Draw.LayerName, *OutError);
				return false;
			}
		}
		return true;
	}

	FTextureRHIRef GetResidentTextureRHI(UTexture* Texture)
	{
		FTextureResource* Resource = Texture ? Texture->GetResource() : nullptr;
		return Resource ? Resource->TextureRHI : FTextureRHIRef();
	}

	bool AppendPreparedDraw(
		const FPaper2DPlusAppearanceLayerDraw& Draw,
		int32 FirstVertex,
		int32 NumVertices,
		bool bAlternateMaterial,
		const FPaper2DPlusAppearanceBuildRecipe& Recipe,
		TArray<FPreparedAppearanceDraw>& OutDraws,
		FString& OutError)
	{
		UPaperSprite* Sprite = Draw.Sprite;
		if (!Sprite || NumVertices <= 0)
		{
			return true;
		}
		UTexture2D* SourceTexture = Sprite->GetBakedTexture();
		const bool bUseRecolor = Draw.bRecolorEnabled
			&& Draw.bCompositeRecolorContractVerified
			&& !bAlternateMaterial;
		UMaterialInterface* Material = bUseRecolor
			? Draw.RecolorMaterial.Get()
			: (bAlternateMaterial ? Sprite->GetAlternateMaterial() : Sprite->GetDefaultMaterial());

		FPreparedAppearanceDraw Prepared;
		if (!ResolveOpacityMode(
			Material, bUseRecolor, Prepared.OpacityMode, Prepared.OpacityMaskClipValue, OutError))
		{
			OutError = FString::Printf(TEXT("Layer '%s': %s"), *Draw.LayerName, *OutError);
			return false;
		}
		FTextureResource* SourceResource = SourceTexture ? SourceTexture->GetResource() : nullptr;
		Prepared.SourceTexture = SourceResource ? SourceResource->TextureRHI : FTextureRHIRef();
		Prepared.SourceSampler = SourceResource ? SourceResource->SamplerStateRHI : FSamplerStateRHIRef();
		if (!Prepared.SourceTexture.IsValid() || !Prepared.SourceSampler.IsValid())
		{
			OutError = FString::Printf(
				TEXT("Layer '%s' has no resident texture/sampler RHI."), *Draw.LayerName);
			return false;
		}
		Prepared.bRecolorEnabled = bUseRecolor;
		Prepared.RecolorIntensity = Draw.RecolorIntensity;
		Prepared.PaletteRow = static_cast<float>(Draw.PaletteRow);
		if (bUseRecolor)
		{
			Prepared.PaletteTexture = GetResidentTextureRHI(Draw.PaletteLUT);
			if (!Prepared.PaletteTexture.IsValid() || Draw.PaletteLUT->GetSizeY() <= 0)
			{
				OutError = FString::Printf(
					TEXT("Layer '%s' requests recolor without a resident non-empty PaletteLUT."), *Draw.LayerName);
				return false;
			}
			Prepared.PaletteInvHeight = 1.0f / static_cast<float>(Draw.PaletteLUT->GetSizeY());
		}
		else
		{
			Prepared.PaletteTexture = Prepared.SourceTexture;
		}

		Prepared.Vertices.Reserve(NumVertices);
		const float SpritePPU = FMath::Max(Sprite->GetPixelsPerUnrealUnit(), KINDA_SMALL_NUMBER);
		for (int32 VertexIndex = FirstVertex; VertexIndex < FirstVertex + NumVertices; ++VertexIndex)
		{
			const FVector4& XYUV = Sprite->BakedRenderData[VertexIndex];
			const float ScreenX = static_cast<float>(XYUV.X) * Recipe.PixelsPerUnrealUnit
				+ static_cast<float>(Draw.TotalOffsetPx.X) * Recipe.PixelsPerUnrealUnit / SpritePPU
				+ static_cast<float>(Recipe.PivotPixels.X);
			const float ScreenY = -static_cast<float>(XYUV.Y) * Recipe.PixelsPerUnrealUnit
				+ static_cast<float>(Draw.TotalOffsetPx.Y) * Recipe.PixelsPerUnrealUnit / SpritePPU
				+ static_cast<float>(Recipe.PivotPixels.Y);
			FFilterVertex Vertex;
			Vertex.Position = FVector4f(
				ScreenX * 2.0f / static_cast<float>(Recipe.PixelSize.X) - 1.0f,
				1.0f - ScreenY * 2.0f / static_cast<float>(Recipe.PixelSize.Y),
				0.0f,
				1.0f);
			Vertex.UV = FVector2f(static_cast<float>(XYUV.Z), static_cast<float>(XYUV.W));
			Prepared.Vertices.Add(Vertex);
		}
		OutDraws.Add(MoveTemp(Prepared));
		return true;
	}
}

uint64 FPaper2DPlusAppearanceBuildRecipe::EstimateCacheReservationBytes() const
{
	if (Frames.Num() <= 0 || PixelSize.X <= 0 || PixelSize.Y <= 0)
	{
		return 0;
	}
	constexpr uint64 BytesPerPixel = 4; // RTF_RGBA8_SRGB steady-state target
	constexpr uint64 PerTargetOverhead = 4096;
	return static_cast<uint64>(PixelSize.X) * static_cast<uint64>(PixelSize.Y) * BytesPerPixel
		* static_cast<uint64>(Frames.Num()) + PerTargetOverhead * static_cast<uint64>(Frames.Num());
}

FPaper2DPlusAppearanceCompositeKey FPaper2DPlusAppearanceRenderBackend::MakeCacheKey(
	const FPaper2DPlusAppearanceBuildRecipe& Recipe)
{
	FAppearanceKeyWriter Writer;
	Writer.String(TEXT("Paper2DPlus.AppearanceComposite.v1"));
	Writer.ObjectIdentity(Recipe.LayerAsset);
	Writer.UInt32(Recipe.LayerAsset ? Recipe.LayerAsset->LayerSchemaVersion : 0);
	Writer.ObjectIdentity(Recipe.CharacterProfile);
	Writer.ObjectIdentity(Recipe.Flipbook);
	Writer.String(Recipe.CanonicalAnimationName);
	WriteAppearance(Writer, Recipe.Appearance);
	Writer.Int32(Recipe.OrderedBaseLayerNames.Num());
	for (const FString& Name : Recipe.OrderedBaseLayerNames) { Writer.String(Name); }
	Writer.Int32(Recipe.IndependentChannelLayerNames.Num());
	for (const FString& Name : Recipe.IndependentChannelLayerNames) { Writer.String(Name); }
	Writer.Int32(Recipe.PixelSize.X);
	Writer.Int32(Recipe.PixelSize.Y);
	Writer.Float(static_cast<float>(Recipe.PivotPixels.X));
	Writer.Float(static_cast<float>(Recipe.PivotPixels.Y));
	Writer.Float(Recipe.PixelsPerUnrealUnit);
	Writer.Int32(Recipe.Frames.Num());
	for (const FPaper2DPlusAppearanceFrameRecipe& Frame : Recipe.Frames)
	{
		Writer.Int32(Frame.BaseDraws.Num());
		for (const FPaper2DPlusAppearanceLayerDraw& Draw : Frame.BaseDraws)
		{
			Writer.String(Draw.LayerName);
			Writer.Int32(Draw.PaintOrder);
			Writer.Float(static_cast<float>(Draw.TotalOffsetPx.X));
			Writer.Float(static_cast<float>(Draw.TotalOffsetPx.Y));
			Writer.Byte(Draw.bRecolorEnabled ? 1 : 0);
			Writer.Byte(Draw.bCompositeRecolorContractVerified ? 1 : 0);
			WriteMaterial(Writer, Draw.RecolorMaterial);
			WriteTexture(Writer, Draw.PaletteLUT);
			Writer.Int32(Draw.PaletteRow);
			Writer.Float(Draw.RecolorIntensity);
			WriteSprite(Writer, Draw.Sprite);
		}
	}

	FPaper2DPlusAppearanceCompositeKey Result;
	Result.CanonicalBytes = MoveTemp(Writer.Bytes);
	Result.FastHash = FCrc::MemCrc32(Result.CanonicalBytes.GetData(), Result.CanonicalBytes.Num());
	FSHA1 Sha;
	Sha.Update(Result.CanonicalBytes.GetData(), Result.CanonicalBytes.Num());
	Sha.Final();
	uint8 Digest[FSHA1::DigestSize];
	Sha.GetHash(Digest);
	Result.DebugIdentity = BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
	return Result;
}

bool FPaper2DPlusAppearanceRenderBackend::ValidateRecipe(
	FPaper2DPlusAppearanceBuildRecipe& Recipe,
	FString& OutError)
{
	OutError.Reset();
	Recipe.bWholeRecipeValidated = false;
#if WITH_EDITOR
	++GAppearanceValidationStats.WholeRecipeValidationRuns;
#endif
	if (!ValidateRecipeHeader(Recipe, OutError))
	{
		return false;
	}
	for (const FPaper2DPlusAppearanceFrameRecipe& Frame : Recipe.Frames)
	{
		if (!ValidateFrameContents(Frame, OutError))
		{
			return false;
		}
	}
	Recipe.bWholeRecipeValidated = true;
	return true;
}

bool FPaper2DPlusAppearanceRenderBackend::ValidateAdmittedFrame(
	const FPaper2DPlusAppearanceBuildRecipe& Recipe,
	int32 FrameIndex,
	FString& OutError)
{
	OutError.Reset();
#if WITH_EDITOR
	++GAppearanceValidationStats.FrameSafetyValidationRuns;
#endif
	if (!Recipe.bWholeRecipeValidated)
	{
		OutError = TEXT("The appearance recipe was not validated at its immutable admission boundary.");
		return false;
	}
	if (!ValidateRecipeHeader(Recipe, OutError))
	{
		return false;
	}
	if (!Recipe.Frames.IsValidIndex(FrameIndex))
	{
		OutError = TEXT("The requested appearance key frame is invalid.");
		return false;
	}
	return ValidateFrameContents(Recipe.Frames[FrameIndex], OutError);
}

void FPaper2DPlusAppearanceRenderBackend::CollectDependencies(
	const FPaper2DPlusAppearanceBuildRecipe& Recipe,
	TSet<FSoftObjectPath>& OutObjects,
	TSet<FName>& OutPackages)
{
	OutObjects.Reset();
	OutPackages.Reset();
	AddDependency(Recipe.LayerAsset, OutObjects, OutPackages);
	AddDependency(Recipe.CharacterProfile, OutObjects, OutPackages);
	AddDependency(Recipe.Flipbook, OutObjects, OutPackages);
	for (const FPaper2DPlusAppearanceFrameRecipe& Frame : Recipe.Frames)
	{
		for (const FPaper2DPlusAppearanceLayerDraw& Draw : Frame.BaseDraws)
		{
			AddDependency(Draw.Sprite, OutObjects, OutPackages);
			AddDependency(Draw.RecolorMaterial, OutObjects, OutPackages);
			AddDependency(Draw.PaletteLUT, OutObjects, OutPackages);
			if (Draw.Sprite)
			{
				AddDependency(Draw.Sprite->GetBakedTexture(), OutObjects, OutPackages);
				AddDependency(Draw.Sprite->GetDefaultMaterial(), OutObjects, OutPackages);
				AddDependency(Draw.Sprite->GetAlternateMaterial(), OutObjects, OutPackages);
				FAdditionalSpriteTextureArray AdditionalTextures;
				Draw.Sprite->GetBakedAdditionalSourceTextures(AdditionalTextures);
				for (const UTexture* Texture : AdditionalTextures)
				{
					AddDependency(Texture, OutObjects, OutPackages);
				}
			}
		}
	}
}

void FPaper2DPlusAppearanceRenderBackend::AddReferencedObjects(
	FReferenceCollector& Collector,
	FPaper2DPlusAppearanceBuildRecipe& Recipe)
{
	Collector.AddReferencedObject(Recipe.LayerAsset);
	Collector.AddReferencedObject(Recipe.CharacterProfile);
	Collector.AddReferencedObject(Recipe.Flipbook);
	for (FPaper2DPlusAppearanceFrameRecipe& Frame : Recipe.Frames)
	{
		for (FPaper2DPlusAppearanceLayerDraw& Draw : Frame.BaseDraws)
		{
			Collector.AddReferencedObject(Draw.Sprite);
			Collector.AddReferencedObject(Draw.RecolorMaterial);
			Collector.AddReferencedObject(Draw.PaletteLUT);
		}
	}
}

bool FPaper2DPlusAppearanceRenderBackend::BuildFrame(
	UObject* WorldContextObject,
	UPaper2DPlusAppearanceCompositeResource* Resource,
	const FPaper2DPlusAppearanceBuildRecipe& Recipe,
	int32 FrameIndex,
	TSharedPtr<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>& OutSubmission,
	FString& OutError)
{
	OutSubmission.Reset();
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("Appearance frames must be submitted from the game thread.");
		return false;
	}
	if (!FApp::CanEverRender())
	{
		OutError = TEXT("This process has no render-capable RHI; exact live rendering remains active.");
		return false;
	}
	if (!WorldContextObject || !WorldContextObject->GetWorld())
	{
		OutError = TEXT("A live world context is required to build an appearance frame.");
		return false;
	}
	if (!Resource || Resource->IsInvalidated())
	{
		OutError = TEXT("The composite resource is invalid.");
		return false;
	}
	// Admission already proved the immutable recipe as a whole. Recheck only the requested frame here because
	// resident textures/materials may disappear while a bounded multi-frame build is still in flight.
	if (!ValidateAdmittedFrame(Recipe, FrameIndex, OutError))
	{
		return false;
	}

	TArray<FPreparedAppearanceDraw> PreparedDraws;
	for (const FPaper2DPlusAppearanceLayerDraw& Draw : Recipe.Frames[FrameIndex].BaseDraws)
	{
		const int32 NumVertices = Draw.Sprite->BakedRenderData.Num();
		const int32 Split = Draw.Sprite->AlternateMaterialSplitIndex;
		const int32 DefaultVertexCount = Split == INDEX_NONE ? NumVertices : FMath::Clamp(Split, 0, NumVertices);
		if (!AppendPreparedDraw(
			Draw, 0, DefaultVertexCount, /*bAlternateMaterial=*/false,
			Recipe, PreparedDraws, OutError))
		{
			return false;
		}
		if (Split != INDEX_NONE && DefaultVertexCount < NumVertices
			&& !AppendPreparedDraw(
				Draw, DefaultVertexCount, NumVertices - DefaultVertexCount, /*bAlternateMaterial=*/true,
				Recipe, PreparedDraws, OutError))
		{
			return false;
		}
	}

	UTextureRenderTarget2D* FinalTarget = CreateAppearanceTarget(
		Resource, Recipe.PixelSize, RTF_RGBA8_SRGB);
	if (!FinalTarget || !FinalTarget->GameThread_GetRenderTargetResource())
	{
		OutError = TEXT("The transient GPU targets could not be created.");
		return false;
	}
	OutSubmission = MakeShared<FPaper2DPlusAppearanceBuildSubmission, ESPMode::ThreadSafe>(FinalTarget);

	// Target initialization is itself enqueued. Resolve its RHI inside our later FIFO render command instead of
	// flushing. The submission roots the UObject and reports both render completion and render-command failure.
	FTextureRenderTargetResource* FinalResource = FinalTarget->GameThread_GetRenderTargetResource();
	const TSharedRef<FPaper2DPlusAppearanceBuildSubmission::FCompletionState, ESPMode::ThreadSafe>
		CompletionState = OutSubmission->CompletionState;

	const FIntPoint TargetSize = Recipe.PixelSize;
	ENQUEUE_RENDER_COMMAND(Paper2DPlusBuildAppearanceFrame)(
		[FinalResource, TargetSize, CompletionState, Draws = MoveTemp(PreparedDraws)](
			FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* FinalRHI = FinalResource->GetRenderTargetTexture().GetReference();
			if (!FinalRHI)
			{
				CompletionState->bSucceeded.Store(false);
				CompletionState->bRenderCommandComplete.Store(true);
				return;
			}
			FRDGBuilder GraphBuilder(RHICmdList);
			const FRDGTextureDesc ScratchDesc = FRDGTextureDesc::Create2D(
				TargetSize,
				PF_FloatRGBA,
				FClearValueBinding(FLinearColor::Transparent),
				TexCreate_ShaderResource | TexCreate_RenderTargetable);
			FRDGTextureRef ScratchTexture = GraphBuilder.CreateTexture(
				ScratchDesc, TEXT("Paper2DPlus.AppearanceScratch"));
			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(FinalRHI, TEXT("Paper2DPlus.AppearanceOutput")));
			AddClearRenderTargetPass(GraphBuilder, ScratchTexture, FLinearColor::Transparent);

			const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FPaper2DPlusAppearanceDrawVS> VertexShader(ShaderMap);
			TShaderMapRef<FPaper2DPlusAppearanceDrawPS> PixelShader(ShaderMap);
			for (int32 DrawIndex = 0; DrawIndex < Draws.Num(); ++DrawIndex)
			{
				const FPreparedAppearanceDraw& Draw = Draws[DrawIndex];
				if (Draw.Vertices.Num() == 0)
				{
					continue;
				}
				FRDGBufferRef VertexBuffer = CreateVertexBuffer(
					GraphBuilder,
					TEXT("Paper2DPlus.AppearanceVertices"),
					FRDGBufferDesc::CreateBufferDesc(sizeof(FFilterVertex), Draw.Vertices.Num()),
					Draw.Vertices.GetData(),
					static_cast<uint64>(Draw.Vertices.Num()) * sizeof(FFilterVertex));
				FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(
					CreateRenderTarget(Draw.SourceTexture, TEXT("Paper2DPlus.AppearanceSource")));
				FRDGTextureRef PaletteTexture = GraphBuilder.RegisterExternalTexture(
					CreateRenderTarget(Draw.PaletteTexture, TEXT("Paper2DPlus.AppearancePalette")));

				FPaper2DPlusAppearanceDrawPS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FPaper2DPlusAppearanceDrawPS::FParameters>();
				Parameters->SourceTexture = SourceTexture;
				// Preserve the resident resource's fully-resolved filter/address state. In particular TF_Default
				// + TEXTUREGROUP_Pixels2D resolves to point sampling even though UTexture::Filter is not TF_Nearest.
				Parameters->SourceSampler = Draw.SourceSampler;
				Parameters->PaletteTexture = PaletteTexture;
				Parameters->PaletteSampler =
					TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				Parameters->OpacityMode = static_cast<uint32>(Draw.OpacityMode);
				Parameters->OpacityMaskClipValue = Draw.OpacityMaskClipValue;
				Parameters->RecolorEnabled = Draw.bRecolorEnabled ? 1u : 0u;
				Parameters->PaletteRow = Draw.PaletteRow;
				Parameters->PaletteInvHeight = Draw.PaletteInvHeight;
				Parameters->RecolorIntensity = Draw.RecolorIntensity;
				Parameters->VertexBuffer = VertexBuffer;
				Parameters->RenderTargets[0] = FRenderTargetBinding(
					ScratchTexture, ERenderTargetLoadAction::ELoad);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Paper2DPlus.AppearanceDraw(%d)", DrawIndex),
					Parameters,
					ERDGPassFlags::Raster,
					[Parameters, VertexBuffer, VertexShader, PixelShader, TargetSize, NumVertices = Draw.Vertices.Num()](
						FRHICommandList& CmdList)
					{
						CmdList.SetViewport(0.0f, 0.0f, 0.0f,
							static_cast<float>(TargetSize.X),
							static_cast<float>(TargetSize.Y), 1.0f);
						// RDG raster passes do not promise an inherited dynamic scissor state. A stale editor/view
						// scissor can make only geometry touching (0,0) survive, which silently drops inset sprites,
						// mixed-PPU layers, and authored pixel offsets. Disable it explicitly for the full target.
						CmdList.SetScissorRect(false, 0, 0, TargetSize.X, TargetSize.Y);
						FGraphicsPipelineStateInitializer PSO;
						CmdList.ApplyCachedRenderTargets(PSO);
						PSO.BlendState = TStaticBlendState<
							CW_RGBA,
							BO_Add, BF_One, BF_InverseSourceAlpha,
							BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
						PSO.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
						PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
						PSO.PrimitiveType = PT_TriangleList;
						PSO.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
						PSO.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
						PSO.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
						SetGraphicsPipelineState(CmdList, PSO, 0);
						SetShaderParameters(CmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
						CmdList.SetStreamSource(0, VertexBuffer->GetRHI(), 0);
						CmdList.DrawPrimitive(0, NumVertices / 3, 1);
					});
			}

			TShaderMapRef<FPaper2DPlusAppearanceResolvePS> ResolveShader(ShaderMap);
			FPaper2DPlusAppearanceResolvePS::FParameters* ResolveParameters =
				GraphBuilder.AllocParameters<FPaper2DPlusAppearanceResolvePS::FParameters>();
			ResolveParameters->ColorTexture = ScratchTexture;
			ResolveParameters->RenderTargets[0] = FRenderTargetBinding(
				OutputTexture, ERenderTargetLoadAction::ENoAction);
			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				ShaderMap,
				RDG_EVENT_NAME("Paper2DPlus.AppearanceResolve"),
				ResolveShader,
				ResolveParameters,
				FIntRect(FIntPoint::ZeroValue, TargetSize));
			GraphBuilder.Execute();
			FGPUFenceRHIRef GPUFence = RHICreateGPUFence(TEXT("Paper2DPlus.AppearanceCompositeComplete"));
			RHICmdList.WriteGPUFence(GPUFence);
			{
				FScopeLock Lock(&CompletionState->FenceMutex);
				CompletionState->GPUFence = MoveTemp(GPUFence);
			}
			CompletionState->bSucceeded.Store(true);
			CompletionState->bRenderCommandComplete.Store(true);
		});

	if (GAppearanceScratchStatsFrame != GFrameCounter)
	{
		GAppearanceScratchStatsFrame = GFrameCounter;
		GAppearanceScratchBytesThisFrame = 0;
		GAppearanceScratchRequestsThisFrame = 0;
	}
	++GAppearanceScratchStats.TransientScratchRequests;
	++GAppearanceScratchStats.FramesSubmitted;
	const uint64 SubmittedScratchBytes =
		static_cast<uint64>(TargetSize.X) * static_cast<uint64>(TargetSize.Y) * 8ull;
	GAppearanceScratchStats.LargestSubmissionScratchBytes = FMath::Max(
		GAppearanceScratchStats.LargestSubmissionScratchBytes, SubmittedScratchBytes);
	GAppearanceScratchBytesThisFrame += SubmittedScratchBytes;
	++GAppearanceScratchRequestsThisFrame;
	GAppearanceScratchStats.PeakSubmittedScratchBytesPerGameFrame = FMath::Max(
		GAppearanceScratchStats.PeakSubmittedScratchBytesPerGameFrame,
		GAppearanceScratchBytesThisFrame);
	GAppearanceScratchStats.PeakSubmittedScratchRequestsPerGameFrame = FMath::Max(
		GAppearanceScratchStats.PeakSubmittedScratchRequestsPerGameFrame,
		GAppearanceScratchRequestsThisFrame);
	return true;
}

bool FPaper2DPlusAppearanceRenderBackend::ConfigureCompositePrimitive(
	UStaticMeshComponent* Primitive,
	UStaticMesh* PlaneMesh,
	UMaterialInterface* DisplayMaterial,
	const UPaper2DPlusAppearanceCompositeResource* Resource,
	UMaterialInstanceDynamic*& OutMID,
	FString& OutError)
{
	OutMID = nullptr;
	OutError.Reset();
	if (!Primitive || !PlaneMesh || !DisplayMaterial || !Resource
		|| Resource->GetPixelsPerUnrealUnit() <= 0.0f)
	{
		OutError = TEXT("Primitive, hard-referenced engine plane/material, and a valid resource are required.");
		return false;
	}
	const FVector MeshExtent = PlaneMesh->GetBounds().BoxExtent;
	if (MeshExtent.X <= KINDA_SMALL_NUMBER || MeshExtent.Y <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("The supplied display mesh has invalid plane bounds.");
		return false;
	}

	OutMID = UMaterialInstanceDynamic::Create(DisplayMaterial, Primitive);
	if (!OutMID)
	{
		OutError = TEXT("The transient display material could not be created.");
		return false;
	}
	Primitive->SetStaticMesh(PlaneMesh);
	Primitive->SetMaterial(0, OutMID);
	Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Primitive->SetGenerateOverlapEvents(false);
	Primitive->SetCastShadow(false);
	Primitive->SetComponentTickEnabled(false);
	Primitive->SetTranslucentSortPriority(0);
	Primitive->SetRelativeRotation(FRotator(0.0, 0.0, 90.0));

	const float PPU = Resource->GetPixelsPerUnrealUnit();
	const FIntPoint PixelSize = Resource->GetPixelSize();
	const FVector2D Pivot = Resource->GetPivotPixels();
	const float WorldWidth = static_cast<float>(PixelSize.X) / PPU;
	const float WorldHeight = static_cast<float>(PixelSize.Y) / PPU;
	Primitive->SetRelativeScale3D(FVector(
		WorldWidth / (2.0f * static_cast<float>(MeshExtent.X)),
		WorldHeight / (2.0f * static_cast<float>(MeshExtent.Y)),
		1.0f));
	Primitive->SetRelativeLocation(FVector(
		(static_cast<float>(PixelSize.X) * 0.5f - static_cast<float>(Pivot.X)) / PPU,
		0.0f,
		(static_cast<float>(Pivot.Y) - static_cast<float>(PixelSize.Y) * 0.5f) / PPU));
	Primitive->SetVisibility(false, true);
	return true;
}

bool FPaper2DPlusAppearanceRenderBackend::BindPreparedFrame(
	UStaticMeshComponent* Primitive,
	UMaterialInstanceDynamic* MID,
	const UPaper2DPlusAppearanceCompositeResource* Resource,
	int32 FrameIndex)
{
	if (!Primitive || !MID || !Resource || Resource->IsInvalidated() || !Resource->IsFrameReady(FrameIndex))
	{
		if (Primitive) { Primitive->SetVisibility(false, true); }
		return false;
	}
	MID->SetTextureParameterValue(TEXT("SpriteTexture"), Resource->GetFrameTarget(FrameIndex));
	Primitive->SetVisibility(true, true);
	return true;
}

FPaper2DPlusAppearanceScratchStats FPaper2DPlusAppearanceRenderBackend::GetScratchStats()
{
	return GAppearanceScratchStats;
}

#if WITH_EDITOR
void FPaper2DPlusAppearanceRenderBackend::ResetScratchForTests()
{
	GAppearanceScratchStats = FPaper2DPlusAppearanceScratchStats();
	GAppearanceScratchStatsFrame = MAX_uint64;
	GAppearanceScratchBytesThisFrame = 0;
	GAppearanceScratchRequestsThisFrame = 0;
}

void FPaper2DPlusAppearanceRenderBackend::ResetValidationStatsForTests()
{
	GAppearanceValidationStats = FPaper2DPlusAppearanceValidationStats();
}

FPaper2DPlusAppearanceValidationStats FPaper2DPlusAppearanceRenderBackend::GetValidationStatsForTests()
{
	return GAppearanceValidationStats;
}

bool FPaper2DPlusAppearanceRenderBackend::ValidateAdmittedFrameForTests(
	const FPaper2DPlusAppearanceBuildRecipe& Recipe,
	int32 FrameIndex,
	FString& OutError)
{
	return ValidateAdmittedFrame(Recipe, FrameIndex, OutError);
}
#endif
