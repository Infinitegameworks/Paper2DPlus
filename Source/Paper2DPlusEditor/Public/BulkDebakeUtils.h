// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class UTexture2D;

/**
 * Shared de-bake editor helpers (TASK-107 core, extracted for the bulk-extractor integration).
 * The name/composite helpers are pure and worldless-testable; the asset-touching helpers wrap the
 * established texture/flipbook materialisation recipes (GetOrCreateTextureForName / Source.Init /
 * pixel-art settings / FullyLoad-reuse). Consumed by the Bulk Sprite Extractor's de-bake groups.
 */
struct PAPER2DPLUSEDITOR_API FBulkDebakeUtils
{
	// ── Pure name heuristics (the VFX prefill + output-naming currency) ──

	/** Lowercased, with the "vfx" token and separators stripped — the name-matching currency. */
	static FString NormalizeName(const FString& In);

	/** Remove the back/front sandwich classifiers so the remaining core names the variant. */
	static FString StripSandwichTokens(const FString& Norm);

	/** Longest common name prefix of the variant mains, raw (used by the per-variant label strip). */
	static FString RawCommonPrefix(const TArray<FString>& MainNames);

	/** RawCommonPrefix trimmed of trailing separators and a leading "T_"; "Debaked" when empty. */
	static FString CommonPrefix(const TArray<FString>& MainNames);

	/** Per-variant output-name token: the main's name with the shared prefix + leading separators stripped. */
	static FString VariantLabel(const FString& MainName, const FString& InRawCommonPrefix);

	/** One candidate VFX sheet for the name-match prefill. */
	struct FVfxPrefillCandidate
	{
		FSoftObjectPath Path;
		FString NameCore; // normalized, sandwich tokens stripped
		bool bBack = false;
	};

	/**
	 * Longest-matching-core-wins front/back VFX assignment per member. OutFront/OutBack are sized to
	 * MemberNames (parallel arrays); unmatched slots stay null. A candidate matches a member when
	 * either normalized name contains the other.
	 */
	static void PrefillVfxAssignments(const TArray<FString>& MemberNames, const TArray<FVfxPrefillCandidate>& Candidates,
		TArray<FSoftObjectPath>& OutFront, TArray<FSoftObjectPath>& OutBack);

	// ── Pure pixel helpers ──

	/** Per-pixel Overlay OVER Base (the de-bake preview's Composite view). False on size mismatch. */
	static bool BuildCompositeBuffer(const TArray<FColor>& Base, const TArray<FColor>& Overlay, TArray<FColor>& Out);

	/** True iff W and H are positive and divisible by the Columns x Rows grid (all >= 1). */
	static bool ValidateGridDims(int32 W, int32 H, int32 Columns, int32 Rows);

	/**
	 * Materialise a BGRA8 buffer as a TRANSIENT display texture (TF_Nearest, sRGB, UpdateResource) —
	 * the version-gated CreateTransient recipe shared with SFlipbookDrawCanvas. Returns nullptr on a
	 * size mismatch. NOT GC-rooted: the caller must hold a TStrongObjectPtr or root it via FGCObject.
	 */
	static UTexture2D* CreateTransientPreviewTexture(int32 W, int32 H, const TArray<FColor>& Pixels);

	// ── Asset-touching helpers (the established materialisation recipes) ──

	/**
	 * Read-only pixel load. Deliberately does NOT retry via ForceCPUAccess or apply Paper2D settings
	 * to the SOURCE textures: LoadTextureData reads Source mips, whose failure modes (no source mips,
	 * unsupported source format) a platform-data rebuild cannot fix — the retry would only Modify +
	 * dirty the input asset before failing again, breaking the tool's read-only-source guarantee.
	 */
	static bool LoadPixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutW, int32& OutH);

	/**
	 * Materialise a BGRA8 pixel buffer as a pixel-art texture asset (find-reuse recipe: a re-run
	 * overwrites the prior output in place). FAIL-CLOSED: refuses (error log + nullptr) when W/H are
	 * not positive or Pixels.Num() != W*H — the memcpy inside trusts the caller with W*H*4 bytes, so
	 * an undersized buffer would read out of bounds. /Temp packages skip MarkPackageDirty +
	 * AssetCreated (SCC guard; the headless-test seam).
	 */
	static UTexture2D* WriteSheetTexture(const FString& OutputPath, const FString& InName, int32 W, int32 H, const TArray<FColor>& Pixels);

};
