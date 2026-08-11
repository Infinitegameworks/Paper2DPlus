// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UPaper2DPlusCharacterProfileAsset;
class UPaperSprite;

/** One animation's re-extraction eligibility and, after a run, its outcome. */
struct FProfileReExtractRow
{
	int32 EntryIndex = INDEX_NONE;
	FString AnimationName;
	FString SourceTextureName;
	/** False when this animation cannot be re-extracted; Reason says why. */
	bool bEligible = false;
	FString Reason;
	/** Sprites this animation shares with ANOTHER Character Profile. */
	TArray<FString> SharedWithProfiles;
	bool bSelected = false;

	/** Populated by a run. */
	bool bRan = false;
	bool bSucceeded = false;
	int32 SpritesUpdated = 0;
	FString Outcome;
};

/**
 * The Re-extract tool -- the manual door to FTextureReimporter, which was a complete, three-phase,
 * rollback-safe re-extraction path whose ONLY callers were on the texture-watcher's disk-change
 * path. Before this, the only way to invoke it was to touch the source PNG on disk and let the
 * watcher notice.
 *
 * Calls the existing reimporter rather than introducing extraction logic, and preserves its
 * three-phase discipline: preflight aborts with zero writes, and the run is per-animation so a
 * failure cannot leave a half-applied profile.
 *
 * Transaction scope is ONE PER ANIMATION, deliberately. Asset writes here are not undoable
 * regardless, and a whole-profile transaction would imply an all-or-nothing rollback that does not
 * exist: a partial multi-animation failure leaves a mixed state no single undo restores. Per-animation
 * scope makes the reported outcome per animation match what actually happened to it.
 */
class SProfileReExtractTool : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProfileReExtractTool) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Profile)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Selects one animation by name and reveals it. Used by the Sprite Bounds tool's routing. */
	bool SelectAnimation(const FString& AnimationName);

	/** Recomputes eligibility from the profile's current stored detection params. */
	void RefreshRows();

#if WITH_DEV_AUTOMATION_TESTS
	const TArray<TSharedPtr<FProfileReExtractRow>>& GetRowsForTests() const { return Rows; }
	TSharedPtr<FProfileReExtractRow> FindRowForTests(const FString& AnimationName) const;
	void RunSelectedForTests() { RunSelected(); }
#endif

private:
	void RunSelected();
	void RebuildList();
	FText GetSummaryText() const;
	bool HasSelection() const;

	/** Profiles other than this one that reference Sprite. Enumerated over loaded profiles rather
	 *  than trusted to the registry's referencer graph: that graph comes from SAVED package headers,
	 *  so a profile created this session has an asset entry but no dependency edge yet. */
	static TArray<FString> FindOtherProfilesUsingSprites(
		const UPaper2DPlusCharacterProfileAsset* Self,
		const TArray<UPaperSprite*>& Sprites);

	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile;
	TArray<TSharedPtr<FProfileReExtractRow>> Rows;
	TSharedPtr<SVerticalBox> ListBox;
	bool bRunning = false;
	FText LastRunSummary;
};
