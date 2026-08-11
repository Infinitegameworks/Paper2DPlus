// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterLayerBakeCore.h"

#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusLayerDraw.h"
#include "Paper2DPlusLayerGameplayCompose.h"
#include "Paper2DPlusSpriteMaterialContract.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Paper2DPlusSpriteSourceUtils.h"

namespace Paper2DPlusCharacterLayerBakeCorePrivate
{
	class FSemanticDigestBuilder
	{
	public:
		void AddBool(bool bValue) { Bytes.Add(bValue ? 1 : 0); }

		void AddUInt32(uint32 Value)
		{
			Bytes.Add(static_cast<uint8>(Value & 0xff));
			Bytes.Add(static_cast<uint8>((Value >> 8) & 0xff));
			Bytes.Add(static_cast<uint8>((Value >> 16) & 0xff));
			Bytes.Add(static_cast<uint8>((Value >> 24) & 0xff));
		}

		void AddInt32(int32 Value) { AddUInt32(static_cast<uint32>(Value)); }

		void AddFloat(float Value)
		{
			uint32 Bits = 0;
			static_assert(sizeof(Bits) == sizeof(Value), "Unexpected float size");
			FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
			AddUInt32(Bits);
		}

		void AddDouble(double Value)
		{
			uint64 Bits = 0;
			static_assert(sizeof(Bits) == sizeof(Value), "Unexpected double size");
			FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
			AddUInt32(static_cast<uint32>(Bits & 0xffffffffull));
			AddUInt32(static_cast<uint32>((Bits >> 32) & 0xffffffffull));
		}

		void AddString(const FString& Value)
		{
			FTCHARToUTF8 Utf8(*Value);
			AddUInt32(static_cast<uint32>(Utf8.Length()));
			if (Utf8.Length() > 0)
			{
				Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			}
		}

		void AddGuid(const FGuid& Value)
		{
			AddUInt32(Value.A);
			AddUInt32(Value.B);
			AddUInt32(Value.C);
			AddUInt32(Value.D);
		}

		void AddVector2D(const FVector2D& Value)
		{
			AddDouble(Value.X);
			AddDouble(Value.Y);
		}

		void AddIntPoint(const FIntPoint& Value)
		{
			AddInt32(Value.X);
			AddInt32(Value.Y);
		}

		void AddBytes(const uint8* Data, int32 Count)
		{
			AddUInt32(static_cast<uint32>(FMath::Max(0, Count)));
			if (Data && Count > 0)
			{
				Bytes.Append(Data, Count);
			}
		}

		void AddColor(const FColor& Color)
		{
			Bytes.Add(Color.R);
			Bytes.Add(Color.G);
			Bytes.Add(Color.B);
			Bytes.Add(Color.A);
		}

		void AddHitbox(const FHitboxData& Box)
		{
			AddUInt32(static_cast<uint32>(Box.Type));
			AddInt32(Box.X);
			AddInt32(Box.Y);
			AddInt32(Box.Width);
			AddInt32(Box.Height);
			AddInt32(Box.Z);
			AddInt32(Box.Depth);
			// Float since TASK-146. Digest representation changed with the type, so pre-existing
			// bakes report NeedsBake once after upgrading — expected, not drift.
			AddFloat(Box.Damage);
			AddFloat(Box.Knockback);
			AddString(Box.ClashCategory.ToString());
		}

		void AddSocket(const FSocketData& Socket)
		{
			AddString(Socket.Name);
			AddInt32(Socket.X);
			AddInt32(Socket.Y);
		}

		void AddFrame(const FFrameHitboxData& Frame)
		{
			AddString(Frame.FrameName);
			AddBool(Frame.bInvulnerable);
			AddString(Frame.DefenseClass.ToString());
			AddUInt32(static_cast<uint32>(Frame.Hitboxes.Num()));
			for (const FHitboxData& Box : Frame.Hitboxes) AddHitbox(Box);
			AddUInt32(static_cast<uint32>(Frame.Sockets.Num()));
			for (const FSocketData& Socket : Frame.Sockets) AddSocket(Socket);
		}

		void AddRegisteredFrame(const FCharacterLayerRegisteredFrame& Frame)
		{
			AddString(Frame.SourceSprite.ToSoftObjectPath().ToString().ToLower());
			AddVector2D(Frame.SourceUV);
			AddVector2D(Frame.SourceDimension);
			AddVector2D(Frame.PivotLocal);
			AddFloat(Frame.PixelsPerUnrealUnit);
			AddString(Frame.MaterialPath.ToString().ToLower());
			AddString(Frame.NativeBehaviorDigest);
			AddBool(Frame.bUsesUnsupportedAtlasGroup);
			AddBool(Frame.bUsesUnsupportedAdditionalTextures);
		}

		FString Finalize() const
		{
			FMD5 Md5;
			if (!Bytes.IsEmpty())
			{
				Md5.Update(Bytes.GetData(), Bytes.Num());
			}
			uint8 Digest[16];
			Md5.Final(Digest);
			return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
		}

	private:
		TArray<uint8> Bytes;
	};

	FString SerializeObjectPayload(const UObject* Object, uint32 DigestVersion)
	{
		if (!Object)
		{
			return TEXT("null");
		}

		TArray<uint8> Serialized;
		FMemoryWriter Writer(Serialized, true);
		FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		Archive.ArNoDelta = true;
		Archive.ArIgnoreOuterRef = true;
		const_cast<UObject*>(Object)->Serialize(Archive);

		FSemanticDigestBuilder Digest;
		Digest.AddString(TEXT("Paper2DPlus.SemanticObject"));
		Digest.AddUInt32(DigestVersion);
		Digest.AddString(Object->GetClass()->GetPathName());
		Digest.AddBytes(Serialized.GetData(), Serialized.Num());
		return Digest.Finalize();
	}

	void AddDiagnostic(
		FCharacterLayerBakeAnimationPlan& Plan,
		ECharacterLayerBakeDiagnosticSeverity Severity,
		ECharacterLayerBakeDiagnosticCode Code,
		const FString& Message,
		const FGuid& LayerId = FGuid(),
		int32 FrameIndex = INDEX_NONE)
	{
		FCharacterLayerBakeDiagnostic& Diagnostic = Plan.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Severity = Severity;
		Diagnostic.Code = Code;
		Diagnostic.AnimationName = Plan.AnimationName;
		Diagnostic.LayerId = LayerId;
		Diagnostic.FrameIndex = FrameIndex;
		Diagnostic.Message = Message;
	}

	void AddGlobalDiagnostic(
		FCharacterLayerBakePlan& Plan,
		ECharacterLayerBakeDiagnosticCode Code,
		const FString& Message)
	{
		FCharacterLayerBakeDiagnostic& Diagnostic = Plan.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Severity = ECharacterLayerBakeDiagnosticSeverity::Error;
		Diagnostic.Code = Code;
		Diagnostic.Message = Message;
	}

	bool SoftFlipbookMatches(
		const TSoftObjectPtr<UPaperFlipbook>& Candidate,
		UPaperFlipbook* Target,
		const FSoftObjectPath& TargetPath)
	{
		return !Candidate.IsNull()
			&& (Candidate.Get() == Target || Candidate.ToSoftObjectPath() == TargetPath);
	}

	const FFlipbookProfileEntry* FindProfileEntry(
		const UPaper2DPlusCharacterProfileAsset& Profile,
		UPaperFlipbook* Target,
		const FSoftObjectPath& TargetPath,
		const FString& LegacyName,
		bool& bOutAmbiguous)
	{
		bOutAmbiguous = false;
		const FFlipbookProfileEntry* Exact = nullptr;
		for (const FFlipbookProfileEntry& Entry : Profile.Flipbooks)
		{
			if (SoftFlipbookMatches(Entry.Identity.Flipbook, Target, TargetPath))
			{
				if (Exact) bOutAmbiguous = true;
				Exact = &Entry;
			}
		}
		if (Exact || bOutAmbiguous) return Exact;

		const FFlipbookProfileEntry* Legacy = nullptr;
		for (const FFlipbookProfileEntry& Entry : Profile.Flipbooks)
		{
			if (!LegacyName.IsEmpty()
				&& Entry.Identity.FlipbookName.Equals(LegacyName, ESearchCase::IgnoreCase))
			{
				if (Legacy) bOutAmbiguous = true;
				Legacy = &Entry;
			}
		}
		return Legacy;
	}

	UPaperFlipbook* ResolveRegistrationTarget(
		const UPaper2DPlusCharacterProfileAsset& Profile,
		const FCharacterLayerAnimationRegistration& Registration,
		bool& bOutAmbiguous)
	{
		bOutAmbiguous = false;
		if (UPaperFlipbook* Target = Registration.Flipbook.LoadSynchronous())
		{
			return Target;
		}
		if (Registration.LegacyAnimationName.IsEmpty())
		{
			return nullptr;
		}

		UPaperFlipbook* UniqueMatch = nullptr;
		for (const FFlipbookProfileEntry& Entry : Profile.Flipbooks)
		{
			if (!Entry.Identity.FlipbookName.Equals(
				Registration.LegacyAnimationName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			UPaperFlipbook* Candidate = Entry.Identity.Flipbook.LoadSynchronous();
			if (UniqueMatch && Candidate != UniqueMatch)
			{
				bOutAmbiguous = true;
				return nullptr;
			}
			UniqueMatch = Candidate;
		}
		return UniqueMatch;
	}

	template <typename EntryType, typename FlipbookGetter, typename NameGetter>
	const EntryType* FindBoundEntry(
		const TArray<EntryType>& Entries,
		UPaperFlipbook* Target,
		const FSoftObjectPath& TargetPath,
		const FString& LegacyName,
		FlipbookGetter GetFlipbook,
		NameGetter GetName,
		bool& bOutAmbiguous)
	{
		bOutAmbiguous = false;
		const EntryType* Exact = nullptr;
		for (const EntryType& Entry : Entries)
		{
			const TSoftObjectPtr<UPaperFlipbook>& Bound = GetFlipbook(Entry);
			if (SoftFlipbookMatches(Bound, Target, TargetPath))
			{
				if (Exact) bOutAmbiguous = true;
				Exact = &Entry;
			}
		}
		if (Exact || bOutAmbiguous) return Exact;

		const EntryType* Legacy = nullptr;
		for (const EntryType& Entry : Entries)
		{
			const TSoftObjectPtr<UPaperFlipbook>& Bound = GetFlipbook(Entry);
			if (Bound.IsNull() && !LegacyName.IsEmpty()
				&& GetName(Entry).Equals(LegacyName, ESearchCase::IgnoreCase))
			{
				if (Legacy) bOutAmbiguous = true;
				Legacy = &Entry;
			}
		}
		return Legacy;
	}

	bool IsUsableRegistrationFrame(const FCharacterLayerRegisteredFrame& Frame)
	{
		FIntPoint Size;
		return CharacterLayerBakeCore::TryIntegralPoint(Frame.SourceDimension, Size)
			&& Size.X > 0 && Size.Y > 0
			&& Frame.PixelsPerUnrealUnit > KINDA_SMALL_NUMBER
			&& FMath::IsFinite(Frame.PivotLocal.X) && FMath::IsFinite(Frame.PivotLocal.Y);
	}

	bool HasUnsupportedLiveTopology(UPaperSprite* Sprite)
	{
		if (!Sprite) return false;
#if WITH_EDITORONLY_DATA
		// Source-region extraction is axis-aligned. Rotated source art needs a pixel rotation in addition
		// to a triangle mask, so fail closed until that transform is part of the exact image recipe.
		if (Sprite->GetAtlasGroup() || Sprite->IsRotatedInSourceImage()) return true;
#endif
		FAdditionalSpriteTextureArray AdditionalTextures;
		Sprite->GetBakedAdditionalSourceTextures(AdditionalTextures);
		return !AdditionalTextures.IsEmpty();
	}

	bool HasUnsupportedMaterialSections(const UPaperSprite* Sprite)
	{
		// The CPU bake reads source texels, not per-triangle material sections. Even two stock
		// materials cannot be flattened honestly without rasterizing the section assignment.
		return Sprite && Sprite->AlternateMaterialSplitIndex != INDEX_NONE;
	}

	bool IsDefaultSourceColorSettings(const FTextureSourceColorSettings& Settings)
	{
		const FTextureSourceColorSettings Defaults;
		return Settings.EncodingOverride == Defaults.EncodingOverride
			&& Settings.ColorSpace == Defaults.ColorSpace
			&& Settings.RedChromaticityCoordinate.Equals(Defaults.RedChromaticityCoordinate, 0.0f)
			&& Settings.GreenChromaticityCoordinate.Equals(Defaults.GreenChromaticityCoordinate, 0.0f)
			&& Settings.BlueChromaticityCoordinate.Equals(Defaults.BlueChromaticityCoordinate, 0.0f)
			&& Settings.WhiteChromaticityCoordinate.Equals(Defaults.WhiteChromaticityCoordinate, 0.0f)
			&& Settings.ChromaticAdaptationMethod == Defaults.ChromaticAdaptationMethod;
	}

	/**
	 * Exact bake consumes mip-zero Texture.Source bytes. Sampling must be explicit: group-default
	 * filter and mip policies can be overridden by a project or device profile, while managed bake
	 * output is always written as nearest/no-mips. Accepting inherited values would make an exact
	 * bake platform-dependent and could silently change the rendered result after commit.
	 */
	bool ValidateSourceTextureBuildSettings(const UTexture2D* Texture, FString& OutError)
	{
		OutError.Reset();
		if (!Texture)
		{
			OutError = TEXT("The sprite has no Texture2D source texture.");
			return false;
		}

#if WITH_EDITORONLY_DATA
		TArray<FString> IncompatibleSettings;
		if (Texture->AdjustBrightness != 1.0f) IncompatibleSettings.Add(TEXT("Brightness"));
		if (Texture->AdjustBrightnessCurve != 1.0f) IncompatibleSettings.Add(TEXT("Brightness Curve"));
		if (Texture->AdjustVibrance != 0.0f) IncompatibleSettings.Add(TEXT("Vibrance"));
		if (Texture->AdjustSaturation != 1.0f) IncompatibleSettings.Add(TEXT("Saturation"));
		if (Texture->AdjustRGBCurve != 1.0f) IncompatibleSettings.Add(TEXT("RGB Curve"));
		if (Texture->AdjustHue != 0.0f) IncompatibleSettings.Add(TEXT("Hue"));
		if (Texture->AdjustMinAlpha != 0.0f || Texture->AdjustMaxAlpha != 1.0f)
		{
			IncompatibleSettings.Add(TEXT("Alpha remap"));
		}
		if (Texture->bChromaKeyTexture) IncompatibleSettings.Add(TEXT("Chroma Key"));
		if (Texture->CompressionNoAlpha != 0) IncompatibleSettings.Add(TEXT("Compress Without Alpha"));
		if (Texture->CompressionYCoCg != 0) IncompatibleSettings.Add(TEXT("YCoCg Compression"));
		if (Texture->bUseLegacyGamma != 0) IncompatibleSettings.Add(TEXT("Legacy Gamma"));
		if (Texture->bFlipGreenChannel != 0) IncompatibleSettings.Add(TEXT("Flip Green Channel"));
		if (Texture->MaxTextureSize != 0) IncompatibleSettings.Add(TEXT("Maximum Texture Size"));
		if (Texture->PowerOfTwoMode != ETexturePowerOfTwoSetting::None)
		{
			IncompatibleSettings.Add(TEXT("Padding/Power-of-Two Resize"));
		}
		if (Texture->bDoScaleMipsForAlphaCoverage) IncompatibleSettings.Add(TEXT("Scale Mips for Alpha Coverage"));
		if (Texture->bPreserveBorder != 0) IncompatibleSettings.Add(TEXT("Preserve Border in Mips"));
		if (!IsDefaultSourceColorSettings(Texture->SourceColorSettings))
		{
			IncompatibleSettings.Add(TEXT("Source Color Settings"));
		}

		if (Texture->CompressionSettings != TC_EditorIcon)
		{
			IncompatibleSettings.Add(TEXT("Compression (expected UserInterface2D/TC_EditorIcon)"));
		}
		if (Texture->Filter != TF_Nearest)
		{
			IncompatibleSettings.Add(TEXT("Filter (expected explicit Nearest; inherited group defaults are not exact)"));
		}
		if (Texture->MipGenSettings != TMGS_NoMipmaps)
		{
			IncompatibleSettings.Add(TEXT("Mip Generation (expected explicit NoMipmaps; inherited group defaults are not exact)"));
		}
		if (Texture->LODGroup != TEXTUREGROUP_Pixels2D)
		{
			IncompatibleSettings.Add(TEXT("Texture Group (expected 2D Pixels)"));
		}
		if (Texture->LODBias != 0)
		{
			IncompatibleSettings.Add(TEXT("LOD Bias (expected zero)"));
		}

		if (IncompatibleSettings.IsEmpty())
		{
			return true;
		}

		OutError = FString::Printf(
			TEXT("Texture '%s' uses build/color settings that can make raw Texture.Source pixels differ from rendered texels: %s. Normalize it to the exact Paper2D Pixels2D/EditorIcon profile with explicit Nearest filtering, NoMipmaps, and default color adjustments, or keep this layer live."),
			*Texture->GetPathName(),
			*FString::Join(IncompatibleSettings, TEXT(", ")));
		return false;
#else
		OutError = TEXT("Editor-only texture build settings are unavailable, so exact source-pixel equivalence cannot be proven. Keep this layer live.");
		return false;
#endif
	}

	void AddSourceTextureBuildSettingsToDigest(
		FSemanticDigestBuilder& Digest,
		const UTexture2D* Texture)
	{
		Digest.AddBool(Texture != nullptr);
		if (!Texture) return;

		Digest.AddString(Texture->GetPathName().ToLower());
		Digest.AddBool(Texture->SRGB != 0);
#if WITH_EDITORONLY_DATA
		Digest.AddFloat(Texture->AdjustBrightness);
		Digest.AddFloat(Texture->AdjustBrightnessCurve);
		Digest.AddFloat(Texture->AdjustVibrance);
		Digest.AddFloat(Texture->AdjustSaturation);
		Digest.AddFloat(Texture->AdjustRGBCurve);
		Digest.AddFloat(Texture->AdjustHue);
		Digest.AddFloat(Texture->AdjustMinAlpha);
		Digest.AddFloat(Texture->AdjustMaxAlpha);
		Digest.AddBool(Texture->bChromaKeyTexture);
		Digest.AddColor(Texture->ChromaKeyColor);
		Digest.AddFloat(Texture->ChromaKeyThreshold);
		Digest.AddBool(Texture->CompressionNoAlpha != 0);
		Digest.AddBool(Texture->CompressionYCoCg != 0);
		Digest.AddBool(Texture->bUseLegacyGamma != 0);
		Digest.AddBool(Texture->bFlipGreenChannel != 0);
		Digest.AddInt32(Texture->MaxTextureSize);
		Digest.AddUInt32(static_cast<uint32>(Texture->PowerOfTwoMode));
		Digest.AddColor(Texture->PaddingColor);
		Digest.AddBool(Texture->bDoScaleMipsForAlphaCoverage);
		Digest.AddBool(Texture->bPreserveBorder != 0);
		Digest.AddUInt32(static_cast<uint32>(Texture->SourceColorSettings.EncodingOverride));
		Digest.AddUInt32(static_cast<uint32>(Texture->SourceColorSettings.ColorSpace));
		Digest.AddVector2D(Texture->SourceColorSettings.RedChromaticityCoordinate);
		Digest.AddVector2D(Texture->SourceColorSettings.GreenChromaticityCoordinate);
		Digest.AddVector2D(Texture->SourceColorSettings.BlueChromaticityCoordinate);
		Digest.AddVector2D(Texture->SourceColorSettings.WhiteChromaticityCoordinate);
		Digest.AddUInt32(static_cast<uint32>(Texture->SourceColorSettings.ChromaticAdaptationMethod));
		Digest.AddUInt32(static_cast<uint32>(Texture->CompressionSettings));
		Digest.AddUInt32(static_cast<uint32>(Texture->Filter));
		Digest.AddUInt32(static_cast<uint32>(Texture->MipGenSettings));
		Digest.AddUInt32(static_cast<uint32>(Texture->LODGroup));
		Digest.AddInt32(Texture->LODBias);
#endif
	}

	double EdgeFunction(const FVector2D& A, const FVector2D& B, const FVector2D& Point)
	{
		return (B.X - A.X) * (Point.Y - A.Y) - (B.Y - A.Y) * (Point.X - A.X);
	}

	bool BuildRenderCoverageMask(
		const UPaperSprite* Sprite,
		int32 Width,
		int32 Height,
		TArray<uint8>& OutCoverage,
		FString& OutError)
	{
		OutCoverage.Reset();
		OutError.Reset();
		if (!Sprite || Width <= 0 || Height <= 0
			|| static_cast<int64>(Width) * Height > MAX_int32)
		{
			OutError = TEXT("The source render-coverage dimensions are invalid.");
			return false;
		}
		if (Sprite->BakedRenderData.IsEmpty())
		{
			OutError = TEXT("Cooked render geometry contains no triangles; exact bake would silently erase non-empty source art.");
			return false;
		}
		if (Sprite->BakedRenderData.Num() % 3 != 0)
		{
			OutError = TEXT("Cooked render geometry is not a complete triangle list.");
			return false;
		}
		const float PixelsPerUnrealUnit = Sprite->GetPixelsPerUnrealUnit();
		if (!FMath::IsFinite(PixelsPerUnrealUnit) || PixelsPerUnrealUnit <= KINDA_SMALL_NUMBER)
		{
			OutError = TEXT("Cooked render geometry has invalid pixels-per-unit.");
			return false;
		}

		TArray<FVector2D> LocalVertices;
		LocalVertices.Reserve(Sprite->BakedRenderData.Num());
		const FVector2D SourceUV = Sprite->GetSourceUV();
		for (const FVector4& XYUV : Sprite->BakedRenderData)
		{
			if (!FMath::IsFinite(XYUV.X) || !FMath::IsFinite(XYUV.Y))
			{
				OutError = TEXT("Cooked render geometry contains a non-finite vertex.");
				return false;
			}
			const FVector2D PivotSpacePixels(
				static_cast<double>(XYUV.X) * PixelsPerUnrealUnit,
				static_cast<double>(XYUV.Y) * PixelsPerUnrealUnit);
			const FVector2D Local = Sprite->ConvertPivotSpaceToTextureSpace(PivotSpacePixels) - SourceUV;
			if (!FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y))
			{
				OutError = TEXT("Cooked render geometry cannot be mapped into source-pixel space.");
				return false;
			}
			LocalVertices.Add(Local);
		}

		// Away from a triangle boundary, sample-center coverage is independent of the GPU's edge tie-break.
		// Exactly on an OUTER edge, D3D's top-left rule decides ownership and an inclusive CPU test can add a
		// texel that the source sprite never drew. Shared triangulation edges are harmless because either adjacent
		// triangle covers the same union. Identify boundary edges by undirected multiplicity and reject only the
		// genuinely ambiguous outer-edge case; ordinary rectangular sprite bounds lie on whole-pixel boundaries
		// and therefore never cross a half-pixel texel center.
		struct FCoverageEdge
		{
			FVector2D A = FVector2D::ZeroVector;
			FVector2D B = FVector2D::ZeroVector;
			int32 UseCount = 0;
		};
		TArray<FCoverageEdge> CoverageEdges;
		auto AddCoverageEdge = [&CoverageEdges](const FVector2D& A, const FVector2D& B)
		{
			constexpr double VertexTolerance = 1.e-7;
			for (FCoverageEdge& Existing : CoverageEdges)
			{
				if ((Existing.A.Equals(A, VertexTolerance) && Existing.B.Equals(B, VertexTolerance))
					|| (Existing.A.Equals(B, VertexTolerance) && Existing.B.Equals(A, VertexTolerance)))
				{
					++Existing.UseCount;
					return;
				}
			}
			FCoverageEdge& Added = CoverageEdges.AddDefaulted_GetRef();
			Added.A = A;
			Added.B = B;
			Added.UseCount = 1;
		};
		for (int32 VertexIndex = 0; VertexIndex < LocalVertices.Num(); VertexIndex += 3)
		{
			AddCoverageEdge(LocalVertices[VertexIndex], LocalVertices[VertexIndex + 1]);
			AddCoverageEdge(LocalVertices[VertexIndex + 1], LocalVertices[VertexIndex + 2]);
			AddCoverageEdge(LocalVertices[VertexIndex + 2], LocalVertices[VertexIndex]);
		}
		for (const FCoverageEdge& Edge : CoverageEdges)
		{
			if (Edge.UseCount > 2)
			{
				OutError = TEXT("Cooked render geometry contains a non-manifold edge used by more than two triangles.");
				return false;
			}
			if (Edge.UseCount != 1)
			{
				continue;
			}

			const double MinX = FMath::Min(Edge.A.X, Edge.B.X);
			const double MinY = FMath::Min(Edge.A.Y, Edge.B.Y);
			const double MaxX = FMath::Max(Edge.A.X, Edge.B.X);
			const double MaxY = FMath::Max(Edge.A.Y, Edge.B.Y);
			const int32 StartX = FMath::Clamp(
				FMath::CeilToInt(static_cast<float>(MinX - 0.5)), 0, Width - 1);
			const int32 StartY = FMath::Clamp(
				FMath::CeilToInt(static_cast<float>(MinY - 0.5)), 0, Height - 1);
			const int32 EndX = FMath::Clamp(
				FMath::FloorToInt(static_cast<float>(MaxX - 0.5)), 0, Width - 1);
			const int32 EndY = FMath::Clamp(
				FMath::FloorToInt(static_cast<float>(MaxY - 0.5)), 0, Height - 1);
			const double EdgeScale = FMath::Max(
				1.0,
				FMath::Abs(Edge.B.X - Edge.A.X) + FMath::Abs(Edge.B.Y - Edge.A.Y));
			const double CenterTolerance = EdgeScale * 1.e-7;
			for (int32 Y = StartY; Y <= EndY; ++Y)
			{
				for (int32 X = StartX; X <= EndX; ++X)
				{
					const FVector2D PixelCenter(X + 0.5, Y + 0.5);
					if (FMath::Abs(EdgeFunction(Edge.A, Edge.B, PixelCenter)) <= CenterTolerance)
					{
						OutError = FString::Printf(
							TEXT("Cooked render geometry has an outer edge through texel center (%d.5, %d.5); exact GPU fill coverage is ambiguous. Normalize the custom polygon to pixel boundaries or keep this layer live."),
							X,
							Y);
						return false;
					}
				}
			}
		}

		OutCoverage.Init(0, Width * Height);
		for (int32 VertexIndex = 0; VertexIndex < LocalVertices.Num(); VertexIndex += 3)
		{
			const FVector2D& A = LocalVertices[VertexIndex];
			const FVector2D& B = LocalVertices[VertexIndex + 1];
			const FVector2D& C = LocalVertices[VertexIndex + 2];
			const double SignedArea = EdgeFunction(A, B, C);
			if (FMath::Abs(SignedArea) <= 1.e-12)
			{
				continue;
			}

			const double MinX = FMath::Min3(A.X, B.X, C.X);
			const double MinY = FMath::Min3(A.Y, B.Y, C.Y);
			const double MaxX = FMath::Max3(A.X, B.X, C.X);
			const double MaxY = FMath::Max3(A.Y, B.Y, C.Y);
			if (MaxX < 0.5 || MaxY < 0.5 || MinX > Width - 0.5 || MinY > Height - 0.5)
			{
				continue;
			}

			const int32 StartX = FMath::Clamp(
				FMath::CeilToInt(static_cast<float>(FMath::Max(MinX - 0.5, -1.0))), 0, Width - 1);
			const int32 StartY = FMath::Clamp(
				FMath::CeilToInt(static_cast<float>(FMath::Max(MinY - 0.5, -1.0))), 0, Height - 1);
			const int32 EndX = FMath::Clamp(
				FMath::FloorToInt(static_cast<float>(FMath::Min(MaxX - 0.5, static_cast<double>(Width)))),
				0, Width - 1);
			const int32 EndY = FMath::Clamp(
				FMath::FloorToInt(static_cast<float>(FMath::Min(MaxY - 0.5, static_cast<double>(Height)))),
				0, Height - 1);
			const double EdgeTolerance = FMath::Max(1.0, FMath::Abs(SignedArea)) * 1.e-7;
			for (int32 Y = StartY; Y <= EndY; ++Y)
			{
				for (int32 X = StartX; X <= EndX; ++X)
				{
					const FVector2D PixelCenter(X + 0.5, Y + 0.5);
					const double E0 = EdgeFunction(A, B, PixelCenter);
					const double E1 = EdgeFunction(B, C, PixelCenter);
					const double E2 = EdgeFunction(C, A, PixelCenter);
					const bool bCovered = SignedArea > 0.0
						? E0 >= -EdgeTolerance && E1 >= -EdgeTolerance && E2 >= -EdgeTolerance
						: E0 <= EdgeTolerance && E1 <= EdgeTolerance && E2 <= EdgeTolerance;
					if (bCovered)
					{
						OutCoverage[Y * Width + X] = 1;
					}
				}
			}
		}
		if (!OutCoverage.Contains(0))
		{
			OutCoverage.Reset(); // empty is the compact representation of full rectangular coverage
		}
		return true;
	}

	void TranslateFrameChannels(
		const FCharacterLayerAuthoredFrameData& Source,
		const FIntPoint& Placement,
		FFrameHitboxData& Destination)
	{
		for (FHitboxData Box : Source.AttackBoxes)
		{
			Box.Type = EHitboxType::Attack;
			Box.X += Placement.X;
			Box.Y += Placement.Y;
			Destination.Hitboxes.Add(MoveTemp(Box));
		}
		for (FHitboxData Box : Source.HurtBoxes)
		{
			Box.Type = EHitboxType::Hurtbox;
			Box.X += Placement.X;
			Box.Y += Placement.Y;
			Destination.Hitboxes.Add(MoveTemp(Box));
		}
		for (FSocketData Socket : Source.Sockets)
		{
			Socket.X += Placement.X;
			Socket.Y += Placement.Y;
			Destination.Sockets.Add(MoveTemp(Socket));
		}
	}

	void ValidateCues(
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues,
		int32 KeyFrameCount,
		FCharacterLayerBakeAnimationPlan& Plan,
		const FGuid& LayerId)
	{
		for (int32 CueIndex = 0; CueIndex < Cues.Num(); ++CueIndex)
		{
			const UPaper2DPlusCueBase* Cue = Cues[CueIndex];
			TArray<FPaper2DPlusFrameCueValidationIssue> CueIssues;
			Paper2DPlusFrameCueValidation::ValidateCue(Cue, KeyFrameCount, CueIssues);
			for (const FPaper2DPlusFrameCueValidationIssue& CueIssue : CueIssues)
			{
				const bool bPlacementError = CueIssue.Code == EPaper2DPlusFrameCueValidationCode::InvalidAnchor
					|| CueIssue.Code == EPaper2DPlusFrameCueValidationCode::RangePastAnimationEnd;
				AddDiagnostic(
					Plan,
					CueIssue.Severity == EPaper2DPlusFrameCueValidationSeverity::Error
						? ECharacterLayerBakeDiagnosticSeverity::Error
						: ECharacterLayerBakeDiagnosticSeverity::Warning,
					bPlacementError
						? ECharacterLayerBakeDiagnosticCode::OutOfRangeCueAnchor
						: ECharacterLayerBakeDiagnosticCode::InvalidFrameCue,
					FString::Printf(TEXT("Frame Cue[%d] %s"), CueIndex, *CueIssue.Message),
					LayerId,
					Cue ? Cue->GetPrimaryAnchorFrame() : INDEX_NONE);
			}
		}
	}

	void AppendCueProvenance(
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues,
		const FGuid& LayerId,
		bool bBaseline,
		uint32 DigestVersion,
		FCharacterLayerBakeAnimationPlan& Plan)
	{
		for (UPaper2DPlusCueBase* Cue : Cues)
		{
			if (!Cue) continue;
			FCharacterLayerBakeCueSource& Source = Plan.CueSources.AddDefaulted_GetRef();
			Source.Cue = Cue;
			Source.LayerId = LayerId;
			Source.bCharacterBaseline = bBaseline;
			Source.SourceOrder = Plan.CueSources.Num() - 1;
			Source.SemanticDigest = CharacterLayerBakeCore::ComputeCueSemanticDigest(Cue, DigestVersion);
		}
	}

	void AddRegistrationToDigest(
		FSemanticDigestBuilder& Digest,
		const FCharacterLayerAnimationRegistration& Registration,
		bool bIncludeSeal = true)
	{
		Digest.AddString(Registration.Flipbook.ToSoftObjectPath().ToString().ToLower());
		Digest.AddString(Registration.LegacyAnimationName.ToLower());
		Digest.AddFloat(Registration.FramesPerSecond);
		Digest.AddUInt32(static_cast<uint32>(Registration.FrameRuns.Num()));
		for (int32 Run : Registration.FrameRuns) Digest.AddInt32(Run);
		Digest.AddUInt32(static_cast<uint32>(Registration.Frames.Num()));
		for (const FCharacterLayerRegisteredFrame& Frame : Registration.Frames) Digest.AddRegisteredFrame(Frame);
		if (bIncludeSeal)
		{
			Digest.AddString(Registration.RegistrationDigest);
		}
	}

	FString ComputeSourceDigest(
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const UPaper2DPlusCharacterProfileAsset& Profile,
		const FCharacterLayerAnimationRegistration& Registration,
		UPaperFlipbook& Target,
		const FSoftObjectPath& TargetPath,
		const FString& AnimationName,
		const TArray<int32>& IncludedLayerIndices,
		const TArray<FCharacterLayerExactFrameInput>& ExactFrames,
		const TArray<FFrameHitboxData>& BaselineFrames,
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& BaselineCues,
		const TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>& BaselineLegacyEvents,
		const TArray<FPaper2DPlusLayerGameplayOperation>& Operations,
		uint32 DigestVersion)
	{
		FSemanticDigestBuilder Digest;
		Digest.AddString(TEXT("Paper2DPlus.CharacterLayerBake.Source"));
		Digest.AddUInt32(DigestVersion);
		Digest.AddGuid(LayerAsset.BakeSetId);
		Digest.AddUInt32(static_cast<uint32>(LayerAsset.BakeAttachmentState));
		Digest.AddGuid(Profile.LayerBakeOwnerToken);
		AddRegistrationToDigest(Digest, Registration);
		// Registration stores stable geometry/identity, while this live read seals the build settings
		// of every canonical source image against drift between planning and commit.
		Digest.AddUInt32(static_cast<uint32>(Registration.Frames.Num()));
		for (const FCharacterLayerRegisteredFrame& Registered : Registration.Frames)
		{
			UPaperSprite* RegisteredSprite = Registered.SourceSprite.LoadSynchronous();
			Digest.AddBool(RegisteredSprite != nullptr);
			if (RegisteredSprite)
			{
				AddSourceTextureBuildSettingsToDigest(Digest, RegisteredSprite->GetSourceTexture());
			}
		}

		Digest.AddUInt32(static_cast<uint32>(IncludedLayerIndices.Num()));
		for (int32 LayerIndex : IncludedLayerIndices)
		{
			const FCharacterLayer& Layer = LayerAsset.Layers[LayerIndex];
			Digest.AddGuid(Layer.LayerId);
			Digest.AddString(Layer.LayerName.ToLower());

			bool bAmbiguousMapping = false;
			const FCharacterLayerAnimationMapping* Mapping = FindBoundEntry(
				Layer.AnimationSprites,
				&Target,
				TargetPath,
				AnimationName,
				[](const FCharacterLayerAnimationMapping& Entry) -> const TSoftObjectPtr<UPaperFlipbook>& { return Entry.Flipbook; },
				[](const FCharacterLayerAnimationMapping& Entry) -> const FString& { return Entry.AnimationName; },
				bAmbiguousMapping);
			Digest.AddBool(bAmbiguousMapping);
			Digest.AddBool(Mapping != nullptr);
			if (Mapping)
			{
				Digest.AddString(Mapping->Flipbook.ToSoftObjectPath().ToString().ToLower());
				Digest.AddString(Mapping->AnimationName.ToLower());
				Digest.AddUInt32(static_cast<uint32>(Mapping->Sprites.Num()));
				for (const TSoftObjectPtr<UPaperSprite>& Sprite : Mapping->Sprites)
				{
					Digest.AddString(Sprite.ToSoftObjectPath().ToString().ToLower());
					UPaperSprite* LoadedSprite = Sprite.LoadSynchronous();
					Digest.AddBool(LoadedSprite != nullptr);
					if (LoadedSprite)
					{
						Digest.AddVector2D(LoadedSprite->GetSourceUV());
						Digest.AddVector2D(LoadedSprite->GetSourceSize());
						Digest.AddVector2D(LoadedSprite->GetPivotPosition() - LoadedSprite->GetSourceUV());
						Digest.AddFloat(LoadedSprite->GetPixelsPerUnrealUnit());
						Digest.AddBool(HasUnsupportedLiveTopology(LoadedSprite));
						AddSourceTextureBuildSettingsToDigest(Digest, LoadedSprite->GetSourceTexture());
					}
				}
			}

			bool bAmbiguousAuthored = false;
			const FCharacterLayerAuthoredAnimationData* Authored = FindBoundEntry(
				Layer.AuthoredAnimations,
				&Target,
				TargetPath,
				AnimationName,
				[](const FCharacterLayerAuthoredAnimationData& Entry) -> const TSoftObjectPtr<UPaperFlipbook>& { return Entry.Flipbook; },
				[](const FCharacterLayerAuthoredAnimationData& Entry) -> const FString& { return Entry.LegacyAnimationName; },
				bAmbiguousAuthored);
			Digest.AddBool(bAmbiguousAuthored);
			Digest.AddBool(Authored != nullptr);
			if (Authored)
			{
				Digest.AddString(Authored->Flipbook.ToSoftObjectPath().ToString().ToLower());
				Digest.AddString(Authored->LegacyAnimationName.ToLower());
				Digest.AddUInt32(static_cast<uint32>(Authored->AttackMerge));
				Digest.AddUInt32(static_cast<uint32>(Authored->HurtMerge));
				Digest.AddUInt32(static_cast<uint32>(Authored->SocketMerge));
				Digest.AddUInt32(static_cast<uint32>(Authored->Frames.Num()));
				for (const FCharacterLayerAuthoredFrameData& Frame : Authored->Frames)
				{
					Digest.AddUInt32(static_cast<uint32>(Frame.AttackBoxes.Num()));
					for (const FHitboxData& Box : Frame.AttackBoxes) Digest.AddHitbox(Box);
					Digest.AddUInt32(static_cast<uint32>(Frame.HurtBoxes.Num()));
					for (const FHitboxData& Box : Frame.HurtBoxes) Digest.AddHitbox(Box);
					Digest.AddUInt32(static_cast<uint32>(Frame.Sockets.Num()));
					for (const FSocketData& Socket : Frame.Sockets) Digest.AddSocket(Socket);
				}
				Digest.AddUInt32(static_cast<uint32>(Authored->FrameCues.Num()));
				for (const UPaper2DPlusCueBase* Cue : Authored->FrameCues)
				{
					Digest.AddString(CharacterLayerBakeCore::ComputeCueSemanticDigest(Cue, DigestVersion));
				}
			}
		}

		Digest.AddUInt32(static_cast<uint32>(ExactFrames.Num()));
		for (const FCharacterLayerExactFrameInput& Frame : ExactFrames)
		{
			Digest.AddIntPoint(Frame.CanonicalSourceSize);
			Digest.AddVector2D(Frame.CanonicalPivotLocal);
			Digest.AddBool(Frame.bCanonicalKeyFrameHadSprite);
			Digest.AddUInt32(static_cast<uint32>(Frame.OutputOpacityMode));
			Digest.AddUInt32(static_cast<uint32>(Frame.Images.Num()));
			for (const FCharacterLayerExactImage& Image : Frame.Images)
			{
				Digest.AddString(Image.SourceLabel.ToLower());
				Digest.AddIntPoint(Image.Size);
				Digest.AddIntPoint(Image.TopLeftFromCanonical);
				Digest.AddUInt32(static_cast<uint32>(Image.OpacityMode));
				Digest.AddFloat(Image.OpacityMaskClipValue);
				Digest.AddBool(Image.bSourcePixelsAreSRGB);
				Digest.AddBytes(Image.CoverageMask.GetData(), Image.CoverageMask.Num());
				Digest.AddUInt32(static_cast<uint32>(Image.Pixels.Num()));
				for (const FColor& Pixel : Image.Pixels) Digest.AddColor(Pixel);
			}
		}

		Digest.AddUInt32(static_cast<uint32>(BaselineFrames.Num()));
		for (const FFrameHitboxData& Frame : BaselineFrames) Digest.AddFrame(Frame);
		Digest.AddUInt32(static_cast<uint32>(BaselineCues.Num()));
		for (const UPaper2DPlusCueBase* Cue : BaselineCues)
		{
			Digest.AddString(CharacterLayerBakeCore::ComputeCueSemanticDigest(Cue, DigestVersion));
		}
		Digest.AddUInt32(static_cast<uint32>(BaselineLegacyEvents.Num()));
		for (const UPaper2DPlusFrameEventBase* Event : BaselineLegacyEvents)
		{
			Digest.AddString(SerializeObjectPayload(Event, DigestVersion));
		}

		Digest.AddUInt32(static_cast<uint32>(Operations.Num()));
		for (const FPaper2DPlusLayerGameplayOperation& Operation : Operations)
		{
			Digest.AddUInt32(static_cast<uint32>(Operation.AttackMerge));
			Digest.AddUInt32(static_cast<uint32>(Operation.HurtMerge));
			Digest.AddUInt32(static_cast<uint32>(Operation.SocketMerge));
			Digest.AddUInt32(static_cast<uint32>(Operation.Frames.Num()));
			for (const FFrameHitboxData& Frame : Operation.Frames) Digest.AddFrame(Frame);
			Digest.AddUInt32(static_cast<uint32>(Operation.FrameCues.Num()));
			for (const UPaper2DPlusCueBase* Cue : Operation.FrameCues)
			{
				Digest.AddString(CharacterLayerBakeCore::ComputeCueSemanticDigest(Cue, DigestVersion));
			}
		}
		return Digest.Finalize();
	}

	FString ComputeTargetPreconditionDigestInternal(
		UPaperFlipbook& Target,
		const FFlipbookProfileEntry& ProfileEntry,
		const UPaper2DPlusCharacterProfileAsset& Profile,
		uint32 DigestVersion)
	{
		FSemanticDigestBuilder Digest;
		Digest.AddString(TEXT("Paper2DPlus.CharacterLayerBake.TargetPrecondition"));
		Digest.AddUInt32(DigestVersion);
		Digest.AddGuid(Profile.LayerBakeOwnerToken);
		Digest.AddString(Target.GetPathName().ToLower());
		Digest.AddFloat(Target.GetFramesPerSecond());
		Digest.AddUInt32(static_cast<uint32>(Target.GetNumKeyFrames()));
		for (int32 FrameIndex = 0; FrameIndex < Target.GetNumKeyFrames(); ++FrameIndex)
		{
			const FPaperFlipbookKeyFrame& KeyFrame = Target.GetKeyFrameChecked(FrameIndex);
			Digest.AddInt32(KeyFrame.FrameRun);
			Digest.AddString(KeyFrame.Sprite ? KeyFrame.Sprite->GetPathName().ToLower() : TEXT("null"));
			if (KeyFrame.Sprite)
			{
				Digest.AddString(CharacterLayerBakeCore::ComputeNativeSpriteBehaviorDigest(KeyFrame.Sprite));
				Digest.AddVector2D(KeyFrame.Sprite->GetSourceUV());
				Digest.AddVector2D(KeyFrame.Sprite->GetSourceSize());
				Digest.AddVector2D(KeyFrame.Sprite->GetPivotPosition() - KeyFrame.Sprite->GetSourceUV());
				Digest.AddFloat(KeyFrame.Sprite->GetPixelsPerUnrealUnit());
				Digest.AddString(KeyFrame.Sprite->GetDefaultMaterial()
					? KeyFrame.Sprite->GetDefaultMaterial()->GetPathName().ToLower()
					: TEXT("null"));
				AddSourceTextureBuildSettingsToDigest(Digest, KeyFrame.Sprite->GetSourceTexture());
				TArray<FColor> Pixels;
				int32 Width = 0;
				int32 Height = 0;
				const bool bReadable = Paper2DPlusSpriteSourceUtils::ReadSourceRegion(
					KeyFrame.Sprite, Pixels, Width, Height);
				Digest.AddBool(bReadable);
				Digest.AddInt32(Width);
				Digest.AddInt32(Height);
				Digest.AddUInt32(static_cast<uint32>(Pixels.Num()));
				for (const FColor& Pixel : Pixels) Digest.AddColor(Pixel);
			}
		}
		Digest.AddUInt32(static_cast<uint32>(ProfileEntry.CombatData.Frames.Num()));
		for (const FFrameHitboxData& Frame : ProfileEntry.CombatData.Frames) Digest.AddFrame(Frame);
		Digest.AddUInt32(static_cast<uint32>(ProfileEntry.FrameEventData.FrameCues.Num()));
		for (const UPaper2DPlusCueBase* Cue : ProfileEntry.FrameEventData.FrameCues)
		{
			Digest.AddString(CharacterLayerBakeCore::ComputeCueSemanticDigest(Cue, DigestVersion));
		}
		Digest.AddUInt32(static_cast<uint32>(ProfileEntry.FrameEventData.FrameEvents.Num()));
		for (const UPaper2DPlusFrameEventBase* Event : ProfileEntry.FrameEventData.FrameEvents)
		{
			Digest.AddString(SerializeObjectPayload(Event, DigestVersion));
		}
		return Digest.Finalize();
	}

	FString ComputeOutputDigest(
		const FCharacterLayerBakeAnimationPlan& Plan,
		uint32 DigestVersion)
	{
		FSemanticDigestBuilder Digest;
		Digest.AddString(TEXT("Paper2DPlus.CharacterLayerBake.Output"));
		Digest.AddUInt32(DigestVersion);
		Digest.AddString(Plan.TargetFlipbookPath.ToString().ToLower());
		Digest.AddFloat(Plan.FramesPerSecond);
		Digest.AddUInt32(static_cast<uint32>(Plan.FrameRuns.Num()));
		for (int32 FrameRun : Plan.FrameRuns) Digest.AddInt32(FrameRun);
		Digest.AddIntPoint(Plan.Composite.CanonicalUnion.Min);
		Digest.AddIntPoint(Plan.Composite.CanonicalUnion.Max);
		Digest.AddIntPoint(Plan.Composite.CellSize);
		Digest.AddIntPoint(Plan.Composite.GridSize);
		Digest.AddIntPoint(Plan.Composite.SheetSize);
		Digest.AddUInt32(static_cast<uint32>(Plan.Composite.SheetPixels.Num()));
		for (const FColor& Pixel : Plan.Composite.SheetPixels) Digest.AddColor(Pixel);
		Digest.AddUInt32(static_cast<uint32>(Plan.Composite.Frames.Num()));
		for (const FCharacterLayerExactFrameOutput& Frame : Plan.Composite.Frames)
		{
			Digest.AddBool(Frame.bHasVisiblePixels);
			Digest.AddBool(Frame.bCanonicalKeyFrameHadSprite);
			Digest.AddInt32(Frame.CellIndex);
			Digest.AddIntPoint(Frame.SheetOrigin);
			Digest.AddVector2D(Frame.ManagedPivotLocal);
		}
		Digest.AddUInt32(static_cast<uint32>(Plan.NativeFrameRecipes.Num()));
		for (const FCharacterLayerRegisteredFrame& Frame : Plan.NativeFrameRecipes) Digest.AddRegisteredFrame(Frame);
		Digest.AddUInt32(static_cast<uint32>(Plan.OutputGameplayFrames.Num()));
		for (const FFrameHitboxData& Frame : Plan.OutputGameplayFrames) Digest.AddFrame(Frame);
		Digest.AddUInt32(static_cast<uint32>(Plan.OutputFrameCues.Num()));
		for (const UPaper2DPlusCueBase* Cue : Plan.OutputFrameCues)
		{
			Digest.AddString(CharacterLayerBakeCore::ComputeCueSemanticDigest(Cue, DigestVersion));
		}
		Digest.AddUInt32(static_cast<uint32>(Plan.BaselineLegacyFrameEvents.Num()));
		for (const UPaper2DPlusFrameEventBase* Event : Plan.BaselineLegacyFrameEvents)
		{
			Digest.AddString(SerializeObjectPayload(Event, DigestVersion));
		}
		return Digest.Finalize();
	}

	FCharacterLayerBakeAnimationPlan BuildAnimationPlanInternal(
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const UPaper2DPlusCharacterProfileAsset& Profile,
		const FCharacterLayerAnimationRegistration& Registration,
		UPaperFlipbook& Target,
		const FCharacterLayerBakeBuildOptions& Options)
	{
		FCharacterLayerBakeAnimationPlan Plan;
		Plan.TargetFlipbook = &Target;
		Plan.TargetFlipbookPath = FSoftObjectPath(Target.GetPathName());
		Plan.AnimationName = Registration.LegacyAnimationName.IsEmpty()
			? Target.GetName()
			: Registration.LegacyAnimationName;
		Plan.KeyFrameCount = Target.GetNumKeyFrames();
		Plan.FramesPerSecond = Target.GetFramesPerSecond();

		const FString ExpectedRegistrationDigest = CharacterLayerBakeCore::ComputeAnimationRegistrationDigest(
			Registration);
		if (Registration.RegistrationDigest.IsEmpty()
			|| Registration.RegistrationDigest != ExpectedRegistrationDigest)
		{
			AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
				ECharacterLayerBakeDiagnosticCode::InvalidRegistrationGeometry,
				TEXT("The canonical animation registration is unsealed or changed. Rebase registration explicitly before baking."));
		}

		bool bAmbiguousProfile = false;
		const FFlipbookProfileEntry* ProfileEntry = FindProfileEntry(
			Profile, &Target, Plan.TargetFlipbookPath, Plan.AnimationName, bAmbiguousProfile);
		if (bAmbiguousProfile || !ProfileEntry)
		{
			AddDiagnostic(
				Plan,
				ECharacterLayerBakeDiagnosticSeverity::Error,
				bAmbiguousProfile ? ECharacterLayerBakeDiagnosticCode::AmbiguousAnimation : ECharacterLayerBakeDiagnosticCode::InvalidTarget,
				bAmbiguousProfile
					? TEXT("The Character Profile contains multiple rows resolving to this canonical animation.")
					: TEXT("The canonical animation does not resolve to a Character Profile row."));
			return Plan;
		}
		if (!ProfileEntry->Identity.FlipbookName.IsEmpty())
		{
			Plan.AnimationName = ProfileEntry->Identity.FlipbookName;
		}

		if (Plan.KeyFrameCount <= 0)
		{
			AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
				ECharacterLayerBakeDiagnosticCode::InvalidTarget,
				TEXT("The canonical flipbook has no key frames."));
			return Plan;
		}

		Plan.FrameRuns.Reserve(Plan.KeyFrameCount);
		for (int32 FrameIndex = 0; FrameIndex < Plan.KeyFrameCount; ++FrameIndex)
		{
			Plan.FrameRuns.Add(Target.GetKeyFrameChecked(FrameIndex).FrameRun);
		}

		if (Registration.Frames.Num() != Plan.KeyFrameCount
			|| Registration.FrameRuns.Num() != Plan.KeyFrameCount)
		{
			AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
				ECharacterLayerBakeDiagnosticCode::FrameCountMismatch,
				FString::Printf(TEXT("Registration topology has %d sprite rows and %d duration rows, but the target has %d key frames."),
					Registration.Frames.Num(), Registration.FrameRuns.Num(), Plan.KeyFrameCount));
			return Plan;
		}

		if (!FMath::IsNearlyEqual(Registration.FramesPerSecond, Plan.FramesPerSecond))
		{
			AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
				ECharacterLayerBakeDiagnosticCode::FrameCountMismatch,
				TEXT("The target flipbook frame rate changed after registration. Rebase registration explicitly before baking."));
		}
		for (int32 FrameIndex = 0; FrameIndex < Plan.KeyFrameCount; ++FrameIndex)
		{
			if (Registration.FrameRuns[FrameIndex] != Plan.FrameRuns[FrameIndex])
			{
				AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
					ECharacterLayerBakeDiagnosticCode::FrameCountMismatch,
					TEXT("A target FrameRun changed after registration. Key-frame indexing is never expanded or silently rebased."),
					FGuid(), FrameIndex);
			}
		}

		int32 FallbackRegistrationIndex = INDEX_NONE;
		for (int32 FrameIndex = 0; FrameIndex < Registration.Frames.Num(); ++FrameIndex)
		{
			const FCharacterLayerRegisteredFrame& Registered = Registration.Frames[FrameIndex];
			if (Registered.bUsesUnsupportedAtlasGroup || Registered.bUsesUnsupportedAdditionalTextures)
			{
				AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
					ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteTopology,
					TEXT("Registration uses an atlas group or additional source texture, which fixed publishing cannot preserve safely."),
					FGuid(), FrameIndex);
			}
			UPaperSprite* RegisteredSprite = Registered.SourceSprite.LoadSynchronous();
			if (!Registered.SourceSprite.IsNull() && !RegisteredSprite)
			{
				AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
					ECharacterLayerBakeDiagnosticCode::InvalidRegistrationGeometry,
					TEXT("A non-null registered native sprite no longer resolves."),
					FGuid(), FrameIndex);
			}
			if (RegisteredSprite)
			{
				if (HasUnsupportedLiveTopology(RegisteredSprite))
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
						ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteTopology,
					TEXT("A registered native sprite now uses an atlas group, rotated source region, or additional source texture."),
						FGuid(), FrameIndex);
				}
				if (HasUnsupportedMaterialSections(RegisteredSprite))
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
						ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteMaterial,
						TEXT("A registered native sprite uses alternate material sections. Fixed publishing cannot infer per-pixel section behavior; keep it live or use one stock Paper2D material."),
						FGuid(), FrameIndex);
				}
				FString TextureBuildError;
				if (!ValidateSourceTextureBuildSettings(
					RegisteredSprite->GetSourceTexture(), TextureBuildError))
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
						ECharacterLayerBakeDiagnosticCode::UnsupportedTextureBuildSettings,
						FString::Printf(
							TEXT("Registered canonical frame %d cannot be baked from Texture.Source exactly: %s"),
							FrameIndex, *TextureBuildError),
						FGuid(), FrameIndex);
				}
				EPaper2DPlusSpriteOpacityMode RegisteredOpacityMode;
				float RegisteredMaskClip = 0.0f;
				FString MaterialError;
				if (!Paper2DPlusSpriteMaterialContract::ResolveOpacityMode(
					RegisteredSprite->GetDefaultMaterial(),
					/*bRequireKnownPaper2DMaterial=*/true,
					RegisteredOpacityMode,
					RegisteredMaskClip,
					MaterialError))
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
						ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteMaterial,
						FString::Printf(TEXT("Registered frame %d material is not bake-safe: %s"),
							FrameIndex, *MaterialError),
						FGuid(), FrameIndex);
				}
				const FString CurrentNativeDigest = CharacterLayerBakeCore::ComputeNativeSpriteBehaviorDigest(
					RegisteredSprite);
				if (Registered.NativeBehaviorDigest.IsEmpty()
					|| Registered.NativeBehaviorDigest != CurrentNativeDigest)
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
						ECharacterLayerBakeDiagnosticCode::InvalidRegistrationGeometry,
						TEXT("Registered native sprite behavior changed or was not captured. Rebase registration explicitly before baking."),
						FGuid(), FrameIndex);
				}
				const FSoftObjectPath CurrentMaterial = RegisteredSprite->GetDefaultMaterial()
					? FSoftObjectPath(RegisteredSprite->GetDefaultMaterial()->GetPathName())
					: FSoftObjectPath();
				if (!Registered.SourceUV.Equals(RegisteredSprite->GetSourceUV(), KINDA_SMALL_NUMBER)
					|| !Registered.SourceDimension.Equals(RegisteredSprite->GetSourceSize(), KINDA_SMALL_NUMBER)
					|| !Registered.PivotLocal.Equals(
						RegisteredSprite->GetPivotPosition() - RegisteredSprite->GetSourceUV(), KINDA_SMALL_NUMBER)
					|| !FMath::IsNearlyEqual(
						Registered.PixelsPerUnrealUnit, RegisteredSprite->GetPixelsPerUnrealUnit())
					|| Registered.MaterialPath != CurrentMaterial)
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
						ECharacterLayerBakeDiagnosticCode::InvalidRegistrationGeometry,
						TEXT("Captured sprite region, pivot, PPU, or material no longer matches its registered native source."),
						FGuid(), FrameIndex);
				}
				if (!IsUsableRegistrationFrame(Registered))
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
						ECharacterLayerBakeDiagnosticCode::InvalidRegistrationGeometry,
						TEXT("A non-null registered sprite is missing valid immutable geometry."),
						FGuid(), FrameIndex);
				}
			}
			if (IsUsableRegistrationFrame(Registered) && FallbackRegistrationIndex == INDEX_NONE)
			{
				FallbackRegistrationIndex = FrameIndex;
			}
		}
		if (FallbackRegistrationIndex == INDEX_NONE)
		{
			AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
				ECharacterLayerBakeDiagnosticCode::InvalidRegistrationGeometry,
				TEXT("The animation is all-null or has no explicit usable registration geometry. Capture or rebase registration before baking."));
			return Plan;
		}

		TArray<FCharacterLayerExactFrameInput> ExactFrames;
		ExactFrames.SetNum(Plan.KeyFrameCount);
		Plan.NativeFrameRecipes.SetNum(Plan.KeyFrameCount);
		for (int32 FrameIndex = 0; FrameIndex < Plan.KeyFrameCount; ++FrameIndex)
		{
			const FCharacterLayerRegisteredFrame& Original = Registration.Frames[FrameIndex];
			const FCharacterLayerRegisteredFrame& Recipe = IsUsableRegistrationFrame(Original)
				? Original
				: Registration.Frames[FallbackRegistrationIndex];
			Plan.NativeFrameRecipes[FrameIndex] = Recipe;

			FIntPoint SourceSize;
			if (!CharacterLayerBakeCore::TryIntegralPoint(Recipe.SourceDimension, SourceSize)
				|| SourceSize.X <= 0 || SourceSize.Y <= 0)
			{
				AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
					ECharacterLayerBakeDiagnosticCode::InvalidRegistrationGeometry,
					TEXT("Registered source dimensions are not positive whole pixels."), FGuid(), FrameIndex);
				continue;
			}
			ExactFrames[FrameIndex].CanonicalSourceSize = SourceSize;
			ExactFrames[FrameIndex].CanonicalPivotLocal = Recipe.PivotLocal;
			ExactFrames[FrameIndex].bCanonicalKeyFrameHadSprite = !Original.SourceSprite.IsNull();
			if (UPaperSprite* RecipeSprite = Recipe.SourceSprite.LoadSynchronous())
			{
				float IgnoredMaskClip = 0.0f;
				FString IgnoredError;
				Paper2DPlusSpriteMaterialContract::ResolveOpacityMode(
					RecipeSprite->GetDefaultMaterial(),
					/*bRequireKnownPaper2DMaterial=*/true,
					ExactFrames[FrameIndex].OutputOpacityMode,
					IgnoredMaskClip,
					IgnoredError);
			}
		}

		const FPaper2DPlusCharacterBaselineAnimation* Baseline = Profile.FindCharacterBaseline(
			Plan.TargetFlipbookPath, Plan.AnimationName);
		TArray<FFrameHitboxData> BaselineFrames;
		TArray<TObjectPtr<UPaper2DPlusCueBase>> BaselineCues;
		if (!Baseline)
		{
			AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
				ECharacterLayerBakeDiagnosticCode::MissingCharacterBaseline,
				TEXT("No Character Baseline exists for this animation. Runtime Profile output is never reused as canonical source."));
			BaselineFrames.SetNum(Plan.KeyFrameCount);
		}
		else
		{
			BaselineFrames = Baseline->Frames;
			BaselineCues = Baseline->FrameCues;
			Plan.BaselineLegacyFrameEvents = Baseline->LegacyFrameEvents;
			if (BaselineFrames.Num() > Plan.KeyFrameCount)
			{
				AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
					ECharacterLayerBakeDiagnosticCode::FrameCountMismatch,
					TEXT("Character Baseline has more rows than the canonical key-frame count."));
			}
			else if (BaselineFrames.Num() < Plan.KeyFrameCount)
			{
				AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Warning,
					ECharacterLayerBakeDiagnosticCode::MissingSourceFrame,
					TEXT("Character Baseline is shorter than the canonical animation; missing rows compile as empty character-wide data."));
				BaselineFrames.SetNum(Plan.KeyFrameCount);
			}
			ValidateCues(BaselineCues, Plan.KeyFrameCount, Plan, FGuid());
			AppendCueProvenance(BaselineCues, FGuid(), true, Options.DigestVersion, Plan);
		}

		TArray<int32> IncludedLayerIndices;
		FPaper2DPlusAppearanceDescriptor DefaultAppearance;
		FString DefaultReason;
		if (!Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(
			&LayerAsset, DefaultAppearance, &DefaultReason))
		{
			AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
				ECharacterLayerBakeDiagnosticCode::InvalidTarget,
				FString::Printf(TEXT("Default Appearance is invalid: %s"), *DefaultReason));
		}
		else
		{
			const TArray<FGuid> ContributingIds =
				Paper2DPlusAppearanceResolver::ResolveContributingArtLayerIds(
					&LayerAsset, DefaultAppearance, Plan.AnimationName);
			TSet<FGuid> ContributingSet(ContributingIds);
			for (const FGuid& GameplayLayerId :
				Paper2DPlusAppearanceResolver::ResolveContributingGameplayLayerIds(
					&LayerAsset, DefaultAppearance, Plan.AnimationName))
			{
				ContributingSet.Add(GameplayLayerId);
			}
			for (int32 LayerIndex = 0; LayerIndex < LayerAsset.Layers.Num(); ++LayerIndex)
			{
				if (ContributingSet.Contains(LayerAsset.Layers[LayerIndex].LayerId))
				{
					IncludedLayerIndices.Add(LayerIndex);
				}
			}
		}

		TArray<FPaper2DPlusLayerGameplayOperation> Operations;
		for (int32 LayerIndex : IncludedLayerIndices)
		{
			const FCharacterLayer& Layer = LayerAsset.Layers[LayerIndex];
			Plan.IncludedLayerIds.Add(Layer.LayerId);

			bool bAmbiguousMapping = false;
			const FCharacterLayerAnimationMapping* Mapping = FindBoundEntry(
				Layer.AnimationSprites,
				&Target,
				Plan.TargetFlipbookPath,
				Plan.AnimationName,
				[](const FCharacterLayerAnimationMapping& Entry) -> const TSoftObjectPtr<UPaperFlipbook>& { return Entry.Flipbook; },
				[](const FCharacterLayerAnimationMapping& Entry) -> const FString& { return Entry.AnimationName; },
				bAmbiguousMapping);
			if (bAmbiguousMapping)
			{
				AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
					ECharacterLayerBakeDiagnosticCode::AmbiguousAnimation,
					FString::Printf(TEXT("Layer '%s' has multiple art mappings for this animation."), *Layer.LayerName),
					Layer.LayerId);
			}
			else if (Mapping)
			{
				if (Mapping->Sprites.Num() > Plan.KeyFrameCount)
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
						ECharacterLayerBakeDiagnosticCode::FrameCountMismatch,
						FString::Printf(TEXT("Layer '%s' has %d art frames for a %d-key-frame animation."),
							*Layer.LayerName, Mapping->Sprites.Num(), Plan.KeyFrameCount),
						Layer.LayerId);
				}

				for (int32 FrameIndex = 0; FrameIndex < Plan.KeyFrameCount; ++FrameIndex)
				{
					if (!Mapping->Sprites.IsValidIndex(FrameIndex) || Mapping->Sprites[FrameIndex].IsNull())
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Warning,
							ECharacterLayerBakeDiagnosticCode::MissingSourceFrame,
							FString::Printf(TEXT("Layer '%s' has no art at frame %d; it compiles as transparent."),
								*Layer.LayerName, FrameIndex),
							Layer.LayerId, FrameIndex);
						continue;
					}

					UPaperSprite* Sprite = Mapping->Sprites[FrameIndex].LoadSynchronous();
					if (!Sprite)
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::UnreadableSourcePixels,
							FString::Printf(TEXT("Layer '%s' frame %d sprite could not be loaded."), *Layer.LayerName, FrameIndex),
							Layer.LayerId, FrameIndex);
						continue;
					}
					if (HasUnsupportedLiveTopology(Sprite))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteTopology,
							FString::Printf(TEXT("Layer '%s' frame %d uses an atlas group, rotated source region, or additional source texture."),
								*Layer.LayerName, FrameIndex),
							Layer.LayerId, FrameIndex);
						continue;
					}
					if (HasUnsupportedMaterialSections(Sprite))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteMaterial,
							FString::Printf(TEXT("Layer '%s' frame %d uses alternate material sections, which source-pixel bake cannot reproduce exactly."),
								*Layer.LayerName, FrameIndex),
							Layer.LayerId, FrameIndex);
						continue;
					}
					UTexture2D* SourceTexture = Sprite->GetSourceTexture();
					FString TextureBuildError;
					if (!ValidateSourceTextureBuildSettings(SourceTexture, TextureBuildError))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::UnsupportedTextureBuildSettings,
							FString::Printf(TEXT("Layer '%s' frame %d cannot be baked from Texture.Source exactly: %s"),
								*Layer.LayerName, FrameIndex, *TextureBuildError),
							Layer.LayerId, FrameIndex);
						continue;
					}

					EPaper2DPlusSpriteOpacityMode SourceOpacityMode;
					float SourceMaskClip = 0.0f;
					FString MaterialError;
					if (!Paper2DPlusSpriteMaterialContract::ResolveOpacityMode(
						Sprite->GetDefaultMaterial(),
						/*bRequireKnownPaper2DMaterial=*/true,
						SourceOpacityMode,
						SourceMaskClip,
						MaterialError))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteMaterial,
							FString::Printf(TEXT("Layer '%s' frame %d material is not bake-safe: %s"),
								*Layer.LayerName, FrameIndex, *MaterialError),
							Layer.LayerId, FrameIndex);
						continue;
					}
					const EPaper2DPlusSpriteOpacityMode OutputOpacityMode =
						ExactFrames[FrameIndex].OutputOpacityMode;
					if (!Paper2DPlusSpriteMaterialContract::CanRepresentFlattenedOpacity(
						OutputOpacityMode, SourceOpacityMode))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteMaterial,
							FString::Printf(
								TEXT("Layer '%s' frame %d uses %s opacity, which cannot be represented by the canonical %s material. Use a translucent canonical material or keep this layer live."),
								*Layer.LayerName,
								FrameIndex,
								Paper2DPlusSpriteMaterialContract::LexToString(SourceOpacityMode),
								Paper2DPlusSpriteMaterialContract::LexToString(OutputOpacityMode)),
							Layer.LayerId, FrameIndex);
						continue;
					}

					const FCharacterLayerRegisteredFrame& Recipe = Plan.NativeFrameRecipes[FrameIndex];
					if (!FMath::IsNearlyEqual(Sprite->GetPixelsPerUnrealUnit(), Recipe.PixelsPerUnrealUnit))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::IncompatiblePixelsPerUnit,
							FString::Printf(TEXT("Layer '%s' frame %d has %.4g PPU; canonical registration requires %.4g PPU."),
								*Layer.LayerName, FrameIndex, Sprite->GetPixelsPerUnrealUnit(), Recipe.PixelsPerUnrealUnit),
							Layer.LayerId, FrameIndex);
						continue;
					}

					FCharacterLayerExactImage Image;
					int32 Width = 0;
					int32 Height = 0;
					if (!Paper2DPlusSpriteSourceUtils::ReadSourceRegion(Sprite, Image.Pixels, Width, Height))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::UnreadableSourcePixels,
							FString::Printf(TEXT("Layer '%s' frame %d source pixels are unreadable."), *Layer.LayerName, FrameIndex),
							Layer.LayerId, FrameIndex);
						continue;
					}
					Image.Size = FIntPoint(Width, Height);
					FString CoverageError;
					if (!BuildRenderCoverageMask(
						Sprite, Width, Height, Image.CoverageMask, CoverageError))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteTopology,
							FString::Printf(TEXT("Layer '%s' frame %d render coverage cannot be reproduced exactly: %s"),
								*Layer.LayerName, FrameIndex, *CoverageError),
							Layer.LayerId, FrameIndex);
						continue;
					}
					Image.SourceLabel = FString::Printf(TEXT("%s|%s"), *Layer.LayerName, *Sprite->GetPathName());
					Image.OpacityMode = SourceOpacityMode;
					Image.OpacityMaskClipValue = SourceMaskClip;
					Image.bSourcePixelsAreSRGB = SourceTexture->SRGB != 0;

					const FVector2D LayerPivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
					const FVector2D TotalOffset = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
						ProfileEntry, FrameIndex, &Layer, Plan.AnimationName);
					const FVector2D RelativeTopLeft = Recipe.PivotLocal - LayerPivotLocal + TotalOffset;
					if (!CharacterLayerBakeCore::TryIntegralPoint(RelativeTopLeft, Image.TopLeftFromCanonical))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::NonIntegralPlacement,
							FString::Printf(TEXT("Layer '%s' frame %d lands at a fractional pixel; exact fixed publishing requires whole-pixel registration."),
								*Layer.LayerName, FrameIndex),
							Layer.LayerId, FrameIndex);
						continue;
					}
					ExactFrames[FrameIndex].Images.Add(MoveTemp(Image));
				}
			}

			bool bAmbiguousAuthored = false;
			const FCharacterLayerAuthoredAnimationData* Authored = FindBoundEntry(
				Layer.AuthoredAnimations,
				&Target,
				Plan.TargetFlipbookPath,
				Plan.AnimationName,
				[](const FCharacterLayerAuthoredAnimationData& Entry) -> const TSoftObjectPtr<UPaperFlipbook>& { return Entry.Flipbook; },
				[](const FCharacterLayerAuthoredAnimationData& Entry) -> const FString& { return Entry.LegacyAnimationName; },
				bAmbiguousAuthored);
			if (bAmbiguousAuthored)
			{
				AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
					ECharacterLayerBakeDiagnosticCode::AmbiguousAnimation,
					FString::Printf(TEXT("Layer '%s' has multiple gameplay rows for this animation."), *Layer.LayerName),
					Layer.LayerId);
			}
			else if (Authored)
			{
				FPaper2DPlusLayerGameplayOperation& Operation = Operations.AddDefaulted_GetRef();
				Operation.AttackMerge = Authored->AttackMerge;
				Operation.HurtMerge = Authored->HurtMerge;
				Operation.SocketMerge = Authored->SocketMerge;
				Operation.FrameCues = Authored->FrameCues;
				Operation.Frames.SetNum(FMath::Min(Authored->Frames.Num(), Plan.KeyFrameCount));

				if (Authored->Frames.Num() > Plan.KeyFrameCount)
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
						ECharacterLayerBakeDiagnosticCode::FrameCountMismatch,
						FString::Printf(TEXT("Layer '%s' gameplay has %d rows for a %d-key-frame animation."),
							*Layer.LayerName, Authored->Frames.Num(), Plan.KeyFrameCount),
						Layer.LayerId);
				}
				else if (Authored->Frames.Num() < Plan.KeyFrameCount)
				{
					AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Warning,
						ECharacterLayerBakeDiagnosticCode::MissingSourceFrame,
						FString::Printf(TEXT("Layer '%s' gameplay is shorter than the animation; missing frames contribute nothing."),
							*Layer.LayerName),
						Layer.LayerId);
				}

				for (int32 FrameIndex = 0; FrameIndex < Operation.Frames.Num(); ++FrameIndex)
				{
					FIntPoint Placement;
					const FVector2D TotalOffset = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
						ProfileEntry, FrameIndex, &Layer, Plan.AnimationName);
					if (!CharacterLayerBakeCore::TryIntegralPoint(TotalOffset, Placement))
					{
						AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error,
							ECharacterLayerBakeDiagnosticCode::NonIntegralPlacement,
							FString::Printf(TEXT("Layer '%s' gameplay at frame %d has a fractional placement."),
								*Layer.LayerName, FrameIndex),
							Layer.LayerId, FrameIndex);
						continue;
					}
					TranslateFrameChannels(Authored->Frames[FrameIndex], Placement, Operation.Frames[FrameIndex]);
				}

				ValidateCues(Authored->FrameCues, Plan.KeyFrameCount, Plan, Layer.LayerId);
				AppendCueProvenance(Authored->FrameCues, Layer.LayerId, false, Options.DigestVersion, Plan);
			}
		}

		Plan.SourceDigest = ComputeSourceDigest(
			LayerAsset,
			Profile,
			Registration,
			Target,
			Plan.TargetFlipbookPath,
			Plan.AnimationName,
			IncludedLayerIndices,
			ExactFrames,
			BaselineFrames,
			BaselineCues,
			Plan.BaselineLegacyFrameEvents,
			Operations,
			Options.DigestVersion);
		Plan.ExpectedTargetPreconditionDigest = ComputeTargetPreconditionDigestInternal(
			Target, *ProfileEntry, Profile, Options.DigestVersion);

		if (!CharacterLayerExactCompositor::Compose(ExactFrames, Options.MaxTextureDimension, Plan.Composite))
		{
			for (const FString& Error : Plan.Composite.Errors)
			{
				const ECharacterLayerBakeDiagnosticCode Code =
					Error.Contains(TEXT("texture limit"), ESearchCase::IgnoreCase)
						? ECharacterLayerBakeDiagnosticCode::TextureLimitExceeded
						: Error.Contains(TEXT("output material"), ESearchCase::IgnoreCase)
							? ECharacterLayerBakeDiagnosticCode::UnsupportedSpriteMaterial
							: ECharacterLayerBakeDiagnosticCode::CompositionFailure;
				AddDiagnostic(Plan, ECharacterLayerBakeDiagnosticSeverity::Error, Code, Error);
			}
		}

		Plan.OutputGameplayFrames = BaselineFrames;
		if (!Operations.IsEmpty())
		{
			Paper2DPlusLayerGameplayCompose::ComposeFrames(BaselineFrames, Operations, Plan.OutputGameplayFrames);
		}
		Paper2DPlusLayerGameplayCompose::ComposeFrameCues(BaselineCues, Operations, Plan.OutputFrameCues);
		Plan.OutputDigest = ComputeOutputDigest(Plan, Options.DigestVersion);
		return Plan;
	}
}

bool CharacterLayerBakeCore::TryIntegralPoint(const FVector2D& Value, FIntPoint& OutPoint)
{
	if (!FMath::IsFinite(Value.X) || !FMath::IsFinite(Value.Y)) return false;
	if (Value.X < static_cast<double>(MIN_int32) || Value.X > static_cast<double>(MAX_int32)
		|| Value.Y < static_cast<double>(MIN_int32) || Value.Y > static_cast<double>(MAX_int32))
	{
		return false;
	}
	const FIntPoint Candidate(FMath::RoundToInt(Value.X), FMath::RoundToInt(Value.Y));
	if (!FMath::IsNearlyEqual(Value.X, static_cast<double>(Candidate.X), KINDA_SMALL_NUMBER)
		|| !FMath::IsNearlyEqual(Value.Y, static_cast<double>(Candidate.Y), KINDA_SMALL_NUMBER))
	{
		return false;
	}
	OutPoint = Candidate;
	return true;
}

bool FCharacterLayerBakeAnimationPlan::HasErrors() const
{
	return Diagnostics.ContainsByPredicate([](const FCharacterLayerBakeDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == ECharacterLayerBakeDiagnosticSeverity::Error;
	});
}

bool FCharacterLayerBakePlan::HasErrors() const
{
	if (Diagnostics.ContainsByPredicate([](const FCharacterLayerBakeDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == ECharacterLayerBakeDiagnosticSeverity::Error;
	}))
	{
		return true;
	}
	return Animations.ContainsByPredicate([](const FCharacterLayerBakeAnimationPlan& Animation)
	{
		return Animation.HasErrors();
	});
}

bool FCharacterLayerBakePlan::CanCommit() const
{
	return !Animations.IsEmpty() && !HasErrors()
		&& !Animations.ContainsByPredicate([](const FCharacterLayerBakeAnimationPlan& Animation)
		{
			return !Animation.CanCommit();
		});
}

FString CharacterLayerBakeCore::ComputeCueSemanticDigest(
	const UPaper2DPlusCueBase* Cue,
	uint32 DigestVersion)
{
	return Paper2DPlusCharacterLayerBakeCorePrivate::SerializeObjectPayload(Cue, DigestVersion);
}

FString CharacterLayerBakeCore::ComputeTargetPreconditionDigest(
	const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
	const UPaperFlipbook* TargetFlipbook,
	uint32 DigestVersion)
{
	if (!CharacterProfile || !TargetFlipbook)
	{
		return FString();
	}

	const FSoftObjectPath TargetPath(TargetFlipbook->GetPathName());
	bool bAmbiguous = false;
	const FFlipbookProfileEntry* Entry = Paper2DPlusCharacterLayerBakeCorePrivate::FindProfileEntry(
		*CharacterProfile,
		const_cast<UPaperFlipbook*>(TargetFlipbook),
		TargetPath,
		TargetFlipbook->GetName(),
		bAmbiguous);
	if (!Entry || bAmbiguous)
	{
		return FString();
	}

	return Paper2DPlusCharacterLayerBakeCorePrivate::ComputeTargetPreconditionDigestInternal(
		*const_cast<UPaperFlipbook*>(TargetFlipbook),
		*Entry,
		*CharacterProfile,
		DigestVersion);
}

FString CharacterLayerBakeCore::ComputeNativeSpriteBehaviorDigest(
	const UPaperSprite* Sprite)
{
	return Paper2DPlusCharacterLayerBakeCorePrivate::SerializeObjectPayload(Sprite, NativeBehaviorDigestVersion);
}

FString CharacterLayerBakeCore::ComputeAnimationRegistrationDigest(
	const FCharacterLayerAnimationRegistration& Registration,
	uint32 DigestVersion)
{
	Paper2DPlusCharacterLayerBakeCorePrivate::FSemanticDigestBuilder Digest;
	Digest.AddString(TEXT("Paper2DPlus.CharacterLayerBake.Registration"));
	Digest.AddUInt32(DigestVersion);
	Paper2DPlusCharacterLayerBakeCorePrivate::AddRegistrationToDigest(Digest, Registration, false);
	return Digest.Finalize();
}

FCharacterLayerBakePlan CharacterLayerBakeCore::BuildPlan(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
	const FCharacterLayerBakeBuildOptions& Options)
{
	FCharacterLayerBakePlan Plan;
	Plan.LayerAsset = const_cast<UPaper2DPlusCharacterLayerAsset*>(LayerAsset);
	Plan.CharacterProfile = const_cast<UPaper2DPlusCharacterProfileAsset*>(CharacterProfile);
	Plan.DigestVersion = Options.DigestVersion;

	if (!LayerAsset)
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddGlobalDiagnostic(Plan, ECharacterLayerBakeDiagnosticCode::InvalidTarget,
			TEXT("No Character Layer Asset was supplied."));
		return Plan;
	}
	if (!CharacterProfile)
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddGlobalDiagnostic(Plan, ECharacterLayerBakeDiagnosticCode::InvalidTarget,
			TEXT("No Character Profile was supplied."));
		return Plan;
	}

	Plan.BakeSetId = LayerAsset->BakeSetId;
	if (!Plan.BakeSetId.IsValid())
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddGlobalDiagnostic(Plan, ECharacterLayerBakeDiagnosticCode::MissingRegistration,
			TEXT("The Layer Asset has no Bake Set identity. Attach it before planning fixed publishing."));
	}
	if (LayerAsset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::Attached)
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddGlobalDiagnostic(Plan, ECharacterLayerBakeDiagnosticCode::InvalidTarget,
			TEXT("The Layer Asset is not attached to an exclusive fixed-publishing bake set."));
	}
	if (CharacterProfile->LayerBakeOwnerToken != Plan.BakeSetId)
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddGlobalDiagnostic(Plan, ECharacterLayerBakeDiagnosticCode::InvalidTarget,
			TEXT("The Character Profile ownership token does not match this Layer Asset's Bake Set identity."));
	}
	if (!LayerAsset->BaseProfile.IsNull()
		&& LayerAsset->BaseProfile.Get() != CharacterProfile
		&& LayerAsset->BaseProfile.ToSoftObjectPath() != FSoftObjectPath(CharacterProfile->GetPathName()))
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddGlobalDiagnostic(Plan, ECharacterLayerBakeDiagnosticCode::InvalidTarget,
			TEXT("The supplied Character Profile is not the Layer Asset's bound Base Profile."));
	}
	if (LayerAsset->AnimationRegistration.IsEmpty())
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddGlobalDiagnostic(Plan, ECharacterLayerBakeDiagnosticCode::MissingRegistration,
			TEXT("The Layer Asset has no canonical animation registration."));
		return Plan;
	}

	TSet<FString> SeenIdentities;
	for (const FCharacterLayerAnimationRegistration& Registration : LayerAsset->AnimationRegistration)
	{
		bool bAmbiguousTarget = false;
		UPaperFlipbook* Target = Paper2DPlusCharacterLayerBakeCorePrivate::ResolveRegistrationTarget(
			*CharacterProfile, Registration, bAmbiguousTarget);

		if (!Target)
		{
			FCharacterLayerBakeAnimationPlan& Animation = Plan.Animations.AddDefaulted_GetRef();
			Animation.AnimationName = Registration.LegacyAnimationName;
			Paper2DPlusCharacterLayerBakeCorePrivate::AddDiagnostic(
				Animation,
				ECharacterLayerBakeDiagnosticSeverity::Error,
				bAmbiguousTarget
					? ECharacterLayerBakeDiagnosticCode::AmbiguousAnimation
					: ECharacterLayerBakeDiagnosticCode::InvalidTarget,
				bAmbiguousTarget
					? TEXT("Registration's legacy animation name resolves to multiple canonical flipbooks.")
					: TEXT("Registration does not resolve to one canonical flipbook."));
			continue;
		}

		const FString Identity = Target->GetPathName().ToLower();
		if (SeenIdentities.Contains(Identity))
		{
			FCharacterLayerBakeAnimationPlan& Animation = Plan.Animations.AddDefaulted_GetRef();
			Animation.TargetFlipbook = Target;
			Animation.TargetFlipbookPath = FSoftObjectPath(Target->GetPathName());
			Animation.AnimationName = Registration.LegacyAnimationName;
			Paper2DPlusCharacterLayerBakeCorePrivate::AddDiagnostic(Animation, ECharacterLayerBakeDiagnosticSeverity::Error,
				ECharacterLayerBakeDiagnosticCode::AmbiguousAnimation,
				TEXT("The canonical registration contains the same flipbook more than once."));
			continue;
		}
		SeenIdentities.Add(Identity);
		Plan.Animations.Add(Paper2DPlusCharacterLayerBakeCorePrivate::BuildAnimationPlanInternal(
			*LayerAsset, *CharacterProfile, Registration, *Target, Options));
	}

	return Plan;
}

FCharacterLayerBakeAnimationPlan CharacterLayerBakeCore::BuildAnimationPlan(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
	int32 RegistrationIndex,
	const FCharacterLayerBakeBuildOptions& Options)
{
	FCharacterLayerBakeAnimationPlan Plan;
	if (!LayerAsset || !CharacterProfile
		|| !LayerAsset->AnimationRegistration.IsValidIndex(RegistrationIndex))
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddDiagnostic(
			Plan,
			ECharacterLayerBakeDiagnosticSeverity::Error,
			ECharacterLayerBakeDiagnosticCode::InvalidTarget,
			TEXT("Layer Asset, Character Profile, or registration index is invalid."));
		return Plan;
	}
	if (!LayerAsset->BakeSetId.IsValid()
		|| LayerAsset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::Attached
		|| CharacterProfile->LayerBakeOwnerToken != LayerAsset->BakeSetId
		|| (!LayerAsset->BaseProfile.IsNull()
			&& LayerAsset->BaseProfile.Get() != CharacterProfile
			&& LayerAsset->BaseProfile.ToSoftObjectPath()
				!= FSoftObjectPath(CharacterProfile->GetPathName())))
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddDiagnostic(
			Plan,
			ECharacterLayerBakeDiagnosticSeverity::Error,
			ECharacterLayerBakeDiagnosticCode::InvalidTarget,
			TEXT("The Layer Asset/Profile pair is not an attached, exclusively owned fixed-publishing bake set."));
		return Plan;
	}

	const FCharacterLayerAnimationRegistration& Registration =
		LayerAsset->AnimationRegistration[RegistrationIndex];
	Plan.AnimationName = Registration.LegacyAnimationName;
	bool bAmbiguousTarget = false;
	UPaperFlipbook* Target = Paper2DPlusCharacterLayerBakeCorePrivate::ResolveRegistrationTarget(
		*CharacterProfile, Registration, bAmbiguousTarget);
	if (!Target)
	{
		Paper2DPlusCharacterLayerBakeCorePrivate::AddDiagnostic(
			Plan,
			ECharacterLayerBakeDiagnosticSeverity::Error,
			bAmbiguousTarget
				? ECharacterLayerBakeDiagnosticCode::AmbiguousAnimation
				: ECharacterLayerBakeDiagnosticCode::InvalidTarget,
			bAmbiguousTarget
				? TEXT("Registration's legacy animation name resolves to multiple canonical flipbooks.")
				: TEXT("Registration does not resolve to one canonical flipbook."));
		return Plan;
	}
	for (int32 OtherIndex = 0; OtherIndex < LayerAsset->AnimationRegistration.Num(); ++OtherIndex)
	{
		if (OtherIndex == RegistrationIndex) continue;
		bool bOtherAmbiguous = false;
		UPaperFlipbook* OtherTarget = Paper2DPlusCharacterLayerBakeCorePrivate::ResolveRegistrationTarget(
			*CharacterProfile, LayerAsset->AnimationRegistration[OtherIndex], bOtherAmbiguous);
		if (OtherTarget == Target)
		{
			Paper2DPlusCharacterLayerBakeCorePrivate::AddDiagnostic(
				Plan,
				ECharacterLayerBakeDiagnosticSeverity::Error,
				ECharacterLayerBakeDiagnosticCode::AmbiguousAnimation,
				TEXT("The canonical registration contains the same flipbook more than once."));
			return Plan;
		}
	}

	return Paper2DPlusCharacterLayerBakeCorePrivate::BuildAnimationPlanInternal(
		*LayerAsset, *CharacterProfile, Registration, *Target, Options);
}
