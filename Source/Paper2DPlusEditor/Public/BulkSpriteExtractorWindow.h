// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "SpriteExtractionUtils.h"

#include "SpriteExtractorWindow.h"  // for ESpriteCanvasGridState

class UTexture2D;
class UPaper2DPlusCharacterProfileAsset;
class SSpriteExtractorCanvas;
class SWindow;
template <typename T> class SListView;

/** Per-texture state for a texture loaded in the bulk extractor. Widget-local. */
enum class EBulkExtractorTextureStatus : uint8
{
	Pending,      // Loaded; detection not run yet
	Inferred,     // Detection produced a grid; awaiting user confirmation
	Overridden,   // User manually set Cols/Rows
	Confirmed,    // User accepted the grid
	SkippedNoPad, // Confirmed, already at group max cell size
	Padded,       // Pad has been applied
	Error         // Detection or pad failed
};

struct FBulkExtractorTextureState
{
	TSoftObjectPtr<UTexture2D> Texture;
	FString DisplayName;
	FString SubfolderPath;
	FIntPoint InferredGrid = FIntPoint::ZeroValue;
	FIntPoint OverrideGrid = FIntPoint::ZeroValue;
	EBulkExtractorTextureStatus Status = EBulkExtractorTextureStatus::Pending;
	bool bUserOverridden = false;
	bool bDetectionRun = false;
	TArray<FDetectedSprite> DetectedSprites;
	FSpriteDetectionParams ConfirmedDetectionParams;

	/** Returns the grid currently presented to the user (override if set, else inferred). */
	FIntPoint GetEffectiveGrid() const { return bUserOverridden ? OverrideGrid : InferredGrid; }
};

/**
 * SBulkSpriteExtractorWindow — the bulk multi-texture extraction window for the cross-sheet
 * alignment pipeline. Self-contained SCompoundWidget hosted in its own SWindow (NOT inside
 * any SWidgetSwitcher; see docs/solutions/ue-ssplitter-onslotresized-widget-switcher-bug.md).
 *
 * Unit 3 scope (this commit): widget shell + single-instance enforcement + content-browser
 * menu entry. Detection, pad, rename, and commit are filled in by subsequent units.
 */
class PAPER2DPLUSEDITOR_API SBulkSpriteExtractorWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBulkSpriteExtractorWindow) {}
		/** Textures to load when the window opens. */
		SLATE_ARGUMENT(TArray<TSoftObjectPtr<UTexture2D>>, InitialTextures)
		/** Target CharacterProfile. If null, a picker is shown before the main layout enables. */
		SLATE_ARGUMENT(TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>, TargetProfile)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget interface
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Factory: opens a new bulk extractor window, OR focuses the existing one.
	 *  Enforces the single-instance rule via module-level TWeakPtr<SWindow>. */
	static void OpenBulkExtractor(
		const TArray<TSoftObjectPtr<UTexture2D>>& InitialTextures,
		TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> TargetProfile = nullptr);

private:
	TArray<TSharedPtr<FBulkExtractorTextureState>> TextureStates;
	TSharedPtr<FBulkExtractorTextureState> SelectedTexture;

	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> TargetProfile;

	FSpriteDetectionParams DetectionParams;

	/** When true, uses simple grid division instead of island detection. */
	bool bUseGridDetection = false;
	/** Number of columns for grid detection mode. */
	int32 GridSpriteCount = 1;
	/** Number of rows for grid detection mode. */
	int32 GridRowCount = 1;

	/** Cached count of textures needing padding (-1 = stale, recompute on next read). */
	int32 CachedPadCount = -1;

	TSharedPtr<SListView<TSharedPtr<FBulkExtractorTextureState>>> TextureListView;
	TSharedPtr<SSpriteExtractorCanvas> CenterCanvas;

	/** Select the texture at the given index in TextureStates and update the list + canvas. */
	void SelectTextureByIndex(int32 Index);

	/** Re-run detection on the selected texture using current DetectionParams. */
	void ReRunDetectionOnSelected();

	/** Build the per-texture row in the left pane. */
	TSharedRef<class ITableRow> GenerateTextureRow(
		TSharedPtr<FBulkExtractorTextureState> InItem,
		const TSharedRef<class STableViewBase>& OwnerTable);

	void OnTextureSelectionChanged(TSharedPtr<FBulkExtractorTextureState> InItem, ESelectInfo::Type SelectInfo);

	/** Run island detection + grid inference for a single texture state. Idempotent. */
	void RunDetectionAndInference(TSharedPtr<FBulkExtractorTextureState> State);

	/** Advance the selected texture to Confirmed (or SkippedNoPad after pad). */
	FReply OnAcceptGridClicked();

	/** Handlers for Cols/Rows spinboxes. */
	void OnColsChanged(int32 NewValue);
	void OnRowsChanged(int32 NewValue);
	TOptional<int32> GetColsValue() const;
	TOptional<int32> GetRowsValue() const;

	/** Returns true when a valid override/inferred grid divides the texture cleanly on both axes. */
	bool IsCurrentGridDivisible() const;

	/** Divisibility warning text (empty when grid divides cleanly). */
	FText GetDivisibilityWarning() const;

	/** Returns the label text for a texture's status chip. */
	static FText GetStatusLabel(EBulkExtractorTextureStatus Status);

	/** Returns the colored brush for a texture's status chip. */
	static FLinearColor GetStatusColor(EBulkExtractorTextureStatus Status);

	/** Map SelectedTexture->Status to the canvas grid-overlay color state. */
	ESpriteCanvasGridState GetGridState() const;

	// ---- Unit 5: cross-texture check + shared-texture guard + auto-pad with rollback ----

	/** "Auto-Pad if Needed" bottom-bar button handler. Runs the full cross-texture pipeline. */
	FReply OnAutoPadClicked();

	/** Returns true iff every texture in the batch has been Confirmed / Overridden (ready for pad). */
	bool AreAllGridsConfirmed() const;

	/** Compute per-texture confirmed cell size. Key = texture raw ptr (valid within one pad run). */
	TMap<UTexture2D*, FIntPoint> BuildPerTextureCellSizeMap() const;

	/** Run the three-phase pad pipeline (preflight → snapshot-acquire → pad-apply) with rollback. */
	bool RunThreePhasePadPipeline(const TArray<UTexture2D*>& ToPadList, FIntPoint MaxCellDims, int32 GroundPlaneOffset);

	/** Mutable pad-run state. Manifests live only for the current session until Accept / Undo. */
	FGuid CurrentPadRunGuid;
	TArray<FPadSnapshotManifest> CurrentPadManifests;
	/** Paste offset per padded texture — handed to Unit 6's hitbox remap. */
	TMap<UTexture2D*, FIntPoint> PerTexturePasteOffsets;
	/** Ground plane offset computed during auto-pad (median of reference cells). */
	int32 LastComputedGroundPlaneOffset = 0;

	// ---- Unit 7: commit + name prefix ----

	/** Prefix override applied to every generated asset name ({Prefix}_{TextureBase}_NN). */
	FString NamePrefix;

	/** When false, FlipbookName in the profile uses DisplayName only (no prefix). */
	bool bIncludePrefixInProfileName = true;

	/** Output path for extracted sprites/flipbooks/textures. Empty = derive from profile or first texture. */
	FString OutputPathOverride;

	/** When true, creates a subfolder per texture under the output path. */
	bool bCreateSubfolder = true;

	/** Last batch rename suffix (saved from batch rename Apply for folder organizer). */
	FString LastBatchSuffix;

	/** Folder naming options (set by folder organizer). */
	bool bFolderIncludePrefix = true;
	bool bFolderIncludeSuffix = false;
	bool bFolderIncludeBatchSuffix = false;
	FString FolderCustomPrefix;
	FString FolderCustomSuffix;
	FString FolderFindStr;
	FString FolderReplaceStr;
	FString FolderRemoveStr;

	/** When true, sprites are trimmed to tight content bounds with alignment offsets. */
	bool bTrimSprites = true;

	/** When true, links extracted flipbooks to the selected CharacterProfile. */
	bool bLinkToProfile = true;

	/** When true, auto-creates flipbook groups on the profile from folder organizer structure. */
	bool bAutoCreateGroups = true;

	/** Persisted folder assignments: maps texture index to folder path (from folder organizer Apply). */
	TMap<int32, FString> FolderAssignments;

	/** Commit button handler — creates sprites + flipbooks, applies alignment, updates profile. */
	FReply OnCommitClicked();

	/** Opens the batch rename modal window. */
	FReply OnBatchRenameClicked();

	/** Opens the folder organizer modal window. */
	FReply OnFolderOrganizerClicked();

	/** Bulk-extract flow: creates one flipbook per texture, sprites under it, attaches to profile. */
	bool CommitBulkExtract();
};
