// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusFrameData.generated.h"

class UPaper2DPlusCharacterProfileAsset;
struct FFlipbookProfileEntry;

/**
 * Computed fighting-game frame-data summary for ONE flipbook/move (TASK-15).
 *
 * This is a COMPUTED, runtime-queryable view — NOT a serialized asset field. Every value is derived
 * on demand from existing CharacterProfile data (key frames, per-frame durations, FHitboxData
 * Damage/Knockback/type, FFrameHitboxData::bInvulnerable, the per-flipbook PhaseTag, root motion).
 * There is no schema bump and nothing here is stored on disk.
 *
 * The single source of truth for the math is FPaper2DPlusFrameData::ComputeMoveFrameData /
 * ComputeAllMoveFrameData (below). The Blueprint query, the CSV/JSON export, and the editor's
 * read-only Frame Data tab all call those — the numbers can never diverge between surfaces.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusMoveFrameData
{
	GENERATED_BODY()

	/** Flipbook/move name (FFlipbookIdentity::FlipbookName). */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	FString FlipbookName;

	/** Number of live key frames in the flipbook. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	int32 TotalKeyFrames = 0;

	/** Sum of per-frame durations expressed in display frames (total FrameRun ticks). */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	int32 TotalDurationFrames = 0;

	/** Sum of per-frame durations in milliseconds (sum(FrameRun) / FPS * 1000). 0 when FPS is unset. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	float TotalDurationMs = 0.f;

	/** Count of key frames that carry at least one ATTACK hitbox (the "active" window). */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	int32 ActiveFrames = 0;

	/** Count of key frames flagged FFrameHitboxData::bInvulnerable (i-frames). */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	int32 IFrameCount = 0;

	/** Highest single-hitbox Damage across all attack hitboxes in the move. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	float MaxDamage = 0.f;

	/** Highest single-hitbox Knockback across all attack hitboxes in the move. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	float MaxKnockback = 0.f;

	/** Max attack reach in pixels (furthest attack-hitbox corner from sprite origin). Reuses
	 *  UPaper2DPlusBlueprintLibrary::GetMaxAttackReach so the value matches AI range queries. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	float MaxReach = 0.f;

	/** True if the flipbook has authored per-frame root motion. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	bool bHasRootMotion = false;

	/** The flipbook's phase (Startup/Active/Recovery) from its own PhaseTag
	 *  (`Paper2DPlus.Phase.*` leaf on EditorMeta — the one phase identity since phase groups were
	 *  removed, legacy-cleanup 2026-07), or None when untagged/unrecognized. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	EAnimationPhase Phase = EAnimationPhase::None;

	/** Number of distinct authored Cancel_<Category> step curves on the move. This is reporting-only reserved
	 *  design data; Paper2DPlus has no runtime transition driver or cancel-window reader. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	int32 CancelWindowCount = 0;

	/** TASK-108 U6 (R9): the move's EFFECTIVE animation tags (own ∪ chain-inherited ∪ group-implied,
	 *  exactly what bIncludeInherited=true tag queries match) as semicolon-separated full tag names.
	 *  APPENDED LAST in the CSV/JSON exports so existing column indices never shift. Stamped by
	 *  ComputeMoveFrameData / ComputeAllMoveFrameData via one BuildAnimationTagMap batch per call;
	 *  the direct per-entry form (ComputeMoveFrameDataForEntry) leaves it empty unless handed the
	 *  batch — callers that don't need tags (e.g. BuildAttackCatalog) skip the O(V+E) batch cost. */
	UPROPERTY(BlueprintReadOnly, Category = "Frame Data")
	FString AnimationTags;
};

/**
 * Pure, worldless frame-data compute + export helpers (TASK-15). THE single source of truth for the
 * fighting-game frame-data math. Everything is static and constructs no UWorld — directly unit-testable.
 *
 * On-block / on-hit frame ADVANTAGE is intentionally absent: it requires opponent hitstun/blockstun
 * values that do not exist in FHitboxData (only Damage/Knockback). Advantage lands with the
 * combat-resolution work (TASK-7 / TASK-77). The self-side Recovery metric IS available via Phase.
 */
class PAPER2DPLUS_API FPaper2DPlusFrameData
{
public:
	/** Compute the frame-data summary for one flipbook by name (case-insensitive).
	 *  @return false (and leaves Out default-constructed) when Asset is null or the name is unknown. */
	static bool ComputeMoveFrameData(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& FlipbookName, FPaper2DPlusMoveFrameData& Out);

	/** Compute the frame-data summary for every flipbook in the asset, in Flipbooks[] order.
	 *  Out is reset first; no-op (empty Out) when Asset is null. */
	static void ComputeAllMoveFrameData(const UPaper2DPlusCharacterProfileAsset* Asset, TArray<FPaper2DPlusMoveFrameData>& Out);

	/** Compute directly from an entry (used internally + by callers that already hold the entry).
	 *  Phase resolves from the entry's own PhaseTag (phase groups were removed 2026-07); Asset is
	 *  kept for signature stability and any asset-scoped derivations. Explicit frame-data/export
	 *  callers keep the default synchronous timing lookup. Large soft catalogs pass false so an
	 *  unloaded flipbook uses the authored-frame fallback instead of defeating UI virtualization. */
	static FPaper2DPlusMoveFrameData ComputeMoveFrameDataForEntry(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FFlipbookProfileEntry& Entry,
		bool bAllowSynchronousFlipbookLoad = true);

	/** Export the table as CSV: a header row + one row per move. Deterministic column order matching
	 *  the struct fields. Strings are quoted/escaped. */
	static FString ExportFrameDataToCsv(const TArray<FPaper2DPlusMoveFrameData>& Rows);

	/** Export the table as a pretty-printed JSON object: { "Moves": [ { ...row... } ] }.
	 *  Field names match the struct field names so the output round-trips back through the fields. */
	static FString ExportFrameDataToJson(const TArray<FPaper2DPlusMoveFrameData>& Rows);

	/** CSV column header line (also the canonical field order). Public so tests/UI can assert it. */
	static FString GetCsvHeader();
};
