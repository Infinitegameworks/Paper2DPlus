// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaperSprite;
class UTexture2D;

enum class EProfileSpriteBoundsFrameKind : uint8
{
	Content,
	Empty,
	Unsupported,
};

/** One frame's non-mutating Sprite Bounds diagnosis. */
struct PAPER2DPLUSEDITOR_API FProfileSpriteBoundsFrameReport
{
	int32 EntryIndex = INDEX_NONE;
	int32 FrameIndex = INDEX_NONE;
	EProfileSpriteBoundsFrameKind Kind = EProfileSpriteBoundsFrameKind::Unsupported;
	TWeakObjectPtr<UPaperSprite> Sprite;
	TWeakObjectPtr<UTexture2D> Texture;
	FIntRect CurrentBounds;
	FIntRect TightBounds;
	FIntRect TargetBounds;
	FIntPoint Anchor = FIntPoint::ZeroValue;
	FIntPoint HitboxDelta = FIntPoint::ZeroValue;
	bool bNeedsRepair = false;
	FString Diagnostic;
};

/** Per-animation summary. The Profile scan is global; max extents remain animation-local. */
struct PAPER2DPLUSEDITOR_API FProfileSpriteBoundsFlipbookReport
{
	int32 EntryIndex = INDEX_NONE;
	FString FlipbookName;
	FIntPoint TargetSize = FIntPoint::ZeroValue;
	int32 HealthyFrames = 0;
	int32 RepairableFrames = 0;
	int32 EmptyFrames = 0;
	int32 UnsupportedFrames = 0;
	/** Frames the designer must act on: repairable PLUS unsupported. "Unsupported" is not a synonym
	 *  for "fine" — it means this check cannot fix the frame, not that the frame is healthy. */
	int32 AttentionFrames = 0;
	TArray<FProfileSpriteBoundsFrameReport> Frames;
};

/** Complete Character Profile Sprite Bounds result. */
struct PAPER2DPLUSEDITOR_API FProfileSpriteBoundsReport
{
	int32 FlipbooksChecked = 0;
	int32 HealthyFrames = 0;
	int32 RepairableFrames = 0;
	int32 EmptyFrames = 0;
	int32 UnsupportedFrames = 0;
	/** Repairable PLUS unsupported — the honest "how many frames need the designer" number. Repair
	 *  can only address the repairable subset, so a surface that gates on HasRepairs() alone tells a
	 *  user with only unsupported frames that nothing is wrong. Gate attention UI on this. */
	int32 AttentionFrames = 0;
	TArray<FProfileSpriteBoundsFlipbookReport> Flipbooks;

	bool HasRepairs() const { return RepairableFrames > 0; }
	bool HasAttention() const { return AttentionFrames > 0; }
	bool IsClean() const { return AttentionFrames == 0; }
};

struct PAPER2DPLUSEDITOR_API FProfileSpriteBoundsRepairStats
{
	int32 FramesRepaired = 0;
	int32 SpritesModified = 0;
	int32 HitboxesRemapped = 0;
	int32 SocketsRemapped = 0;
};

/** Profile-wide diagnosis and explicit repair for missed uniform sprite trimming. */
class PAPER2DPLUSEDITOR_API FProfileSpriteBoundsService
{
public:
	/** Read-only scan. Loads source pixels only when called; safe to cache outside paint/layout. */
	static FProfileSpriteBoundsReport AnalyzeProfile(UPaper2DPlusCharacterProfileAsset* Profile);

	/** Re-scan, apply every repairable result in one optional editor transaction, then return a fresh
	 *  post-repair report. SpriteOffset is preserved. Repair bakes the shared per-flipbook anchor into
	 *  each sprite as a custom pivot and clears TrimOffset, so repaired flipbooks align in any renderer
	 *  (the same model the bulk extractor's trim path produces).
	 *
	 *  RETAINED — KTD9 COVERAGE GATE ANSWERED (TASK-156/157 U5, 2026-08-04).
	 *  Sprite Bounds is now verify-only and its surface offers no repair action, but this function is
	 *  DELIBERATELY NOT DELETED: re-extraction was compared against it line by line and does not cover
	 *  it. Deleting it would lose behavior rather than a button, and a MAJOR bump makes that
	 *  irreversible for already-shipped assets. Verdict per item, measured against
	 *  FTextureReimporter::ReimportFromTexture:
	 *
	 *    1. Bakes the shared per-flipbook anchor as a CUSTOM PIVOT   — NOT COVERED. The reimporter
	 *       calls UpdateSpriteSourceRegion and never sets a pivot mode.
	 *    2. Clears TrimOffset to zero                                 — NOT COVERED. It never touches
	 *       TrimOffset, so the "aligns in any renderer" property (which needs 1 and 2 together) is lost.
	 *    3. PRESERVES SpriteOffset                                    — CONTRADICTED, not merely
	 *       missing. The reimporter ZEROES SpriteOffset whenever the art shifted, which is the exact
	 *       opposite of repair, and destroys the pre-pack origin that cannot be reconstructed.
	 *    4. Remaps hitboxes                                           — covered.
	 *    5. Remaps sockets                                            — covered.
	 *    6. Fail-closed on a sprite shared by ANOTHER Character Profile — NOT COVERED. The Re-extract
	 *       tool warns about this rather than refusing, so the other profile's metadata can still be
	 *       left in the old coordinate space.
	 *    7. Fail-closed when two frames of THIS profile want different bounds for one sprite
	 *                                                                 — NOT COVERED.
	 *    8. Missing or stale stored detection params                  — NOT COVERED, as anticipated:
	 *       such an animation cannot be re-extracted at all. The Re-extract tool reports it explicitly
	 *       instead of failing silently, but reporting is not a repair route.
	 *
	 *  CONSEQUENCE: R13 ("repair happens through re-extraction") is recorded as CONDITIONAL. Sprite
	 *  Bounds no longer offers repair, and re-extraction is the taught fix, but this path remains
	 *  reachable from the Character Data surface for the cases above. Removing it needs the reimporter
	 *  to grow pivot baking, trim clearing, SpriteOffset preservation, and the two fail-closed
	 *  refusals first — that is a separate piece of work, not a deletion. */
	static FProfileSpriteBoundsRepairStats RepairProfile(
		UPaper2DPlusCharacterProfileAsset* Profile,
		FProfileSpriteBoundsReport& OutPostRepairReport,
		bool bCreateTransaction = true);
};
