// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "SpriteExtractionUtils.h"
#include "UObject/StrongObjectPtr.h"
// TStrongObjectPtr<UPaper2DPlusCharacterProfileAsset> below needs the COMPLETE type: from UE 5.4 the
// template static_asserts through TPointerIsConvertibleFromTo, which cannot be instantiated with an
// incomplete type. A forward declaration compiles on 5.8 only because unity grouping happens to pull
// the definition in; it fails on 5.4/5.5 where the grouping differs.
#include "Paper2DPlusCharacterProfileAsset.h"
#include "VariantDebake.h"

#include "SpriteExtractorWindow.h"  // for ESpriteCanvasGridState

class UTexture2D;
class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;
class SSpriteExtractorCanvas;
class SInlineEditableTextBlock;
class SVerticalBox;
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
	DebakeSource, // Consumed by an accepted de-bake group — excluded from extraction
	Error         // Detection or pad failed
};

/**
 * How the bulk extractor decides a texture's frame layout.
 *
 * Frames is the default: FSpriteExtractionUtils::DetectFrameGrid answers "where are the animation
 * FRAMES" via the gutter rule, which survives scattered particle VFX that shatter island detection.
 * Island is the historical "where is the ART" flood fill, kept because the single-sheet extractor and
 * the reimport path still speak that language. ManualGrid is the plain columns/rows division.
 */
enum class EBulkDetectionMode : uint8
{
	Frames,
	Island,
	ManualGrid
};

/** Ordering applied to the bulk extractor's texture list by RefreshFilteredTextureList. */
enum class EBulkExtractorSortMode : uint8
{
	/** Display name, case-insensitive. */
	Name,
	/** Rows that block Extract All first, then display name inside each half. */
	BlockingFirst
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

	// ---- Frame-grid detection readout (EBulkDetectionMode::Frames only) ----

	/** Which rule decided the grid ("gutter", "hint", "sibling", "pitch", ...). Empty in the other
	 *  modes. Purely diagnostic — it is what makes a questionable sheet eyeballable. */
	FString FrameGridSource;
	/** Blank columns at the END of the sheet (VFX length padding — legitimate, but worth showing). */
	int32 FrameGridTrailingBlanks = 0;
	/** Total blank cells along the horizontal axis, trailing run included. */
	int32 FrameGridBlankCells = 0;
	/** False when the grid was inferred from a weaker signal than a gutter/hint/consensus, i.e. the
	 *  result wants a human pass before extraction. */
	bool bFrameGridConfident = false;

	/** Returns the grid currently presented to the user (override if set, else inferred). */
	FIntPoint GetEffectiveGrid() const { return bUserOverridden ? OverrideGrid : InferredGrid; }
};

/** Group-level de-bake lifecycle (widget-local; the per-texture status machine stays orthogonal —
 *  an un-accepted member keeps walking Pending→Inferred→Confirmed so abandoning a group degrades
 *  to ordinary extraction). */
enum class EDebakeGroupStatus : uint8
{
	Pending,    // Created; no valid preview yet
	Previewed,  // CachedResult + preview textures valid (possibly stale — see bPreviewStale)
	Accepted,   // Outputs materialised; members flipped to DebakeSource
	Error       // Last preview failed (LastError set)
};

/** Which de-bake preview image the canvas shows. */
enum class EDebakePreviewView : uint8
{
	Base,
	Overlay,
	Composite
};

/** One member (variant sheet) of a de-bake group, with its optional VFX contamination masks. */
struct FDebakeGroupMember
{
	TWeakPtr<FBulkExtractorTextureState> State; // dead == member removed from the list; dropped at preview time
	TSoftObjectPtr<UTexture2D> VfxFront;
	TSoftObjectPtr<UTexture2D> VfxBack;
};

/** One de-bake group: N baked variant sheets that share a recoverable base (FVariantDebake). */
struct FDebakeGroupState
{
	FString GroupName;
	FLinearColor GroupColor = FLinearColor::White;
	TArray<FDebakeGroupMember> Members;

	int32 ReferenceIndex = 0;               // tie-break owner (index into Members)
	int32 GridColumns = 1;
	int32 GridRows = 1;
	bool bOverrideVfxOffset = false;
	int32 VfxOffsetOverrideValue = 0;
	FString OutputPathOverride;             // empty = beside the first member

	EDebakeGroupStatus Status = EDebakeGroupStatus::Pending;
	FText LastError;

	/** Valid iff Status is Previewed/Accepted. Kept while stale so the canvas keeps displaying. */
	FDebakeResult CachedResult;
	bool bPreviewStale = false;             // a group setting changed since CachedResult was computed
	int32 PreviewW = 0;
	int32 PreviewH = 0;
	/** Member main-names captured when CachedResult was computed (labels for the stats/variant strip). */
	TArray<FString> PreviewMemberNames;

	/** Transient preview textures — TStrongObjectPtr keep-alives (the window is not an FGCObject;
	 *  the canvas roots only the currently-displayed texture). Overlay/composite build lazily. */
	TStrongObjectPtr<UTexture2D> BasePreviewTex;
	TArray<TStrongObjectPtr<UTexture2D>> OverlayPreviewTex;
	TArray<TStrongObjectPtr<UTexture2D>> CompositePreviewTex;

	/** Accept bookkeeping (for revert): the appended output entries + members' pre-accept statuses. */
	TArray<TWeakPtr<FBulkExtractorTextureState>> AcceptedEntries;
	TArray<TPair<TWeakPtr<FBulkExtractorTextureState>, EBulkExtractorTextureStatus>> PreAcceptStatuses;

	void InvalidatePreviewTextures()
	{
		BasePreviewTex.Reset();
		OverlayPreviewTex.Empty();
		CompositePreviewTex.Empty();
	}

	/** Whether this group consumes Path as a member main, front VFX, or back VFX texture. */
	bool ConsumesTexturePath(const FSoftObjectPath& Path) const;
};

/**
 * SBulkSpriteExtractorWindow — the bulk multi-texture extraction window for the cross-sheet
 * alignment pipeline. Self-contained SCompoundWidget hosted in its own SWindow (NOT inside
 * any SWidgetSwitcher; see docs/solutions/ue-ssplitter-onslotresized-widget-switcher-bug.md).
 * It owns the multi-texture detection/pad/rename/commit pipeline and optional de-bake groups;
 * Content Browser and console entry points reuse the same single live window/session.
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
	virtual ~SBulkSpriteExtractorWindow() override;

	// SWidget interface
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	/** Window-wide texture navigation (arrows) + F2 inline rename. Preview routing TUNNELS
	 *  window→leaf, so these keys reach the window before the list view's own row navigation or a
	 *  focused spinbox/combo can consume them. Bubbled shortcuts stay in OnKeyDown. */
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Factory: opens a new bulk extractor window, OR focuses the existing one.
	 *  Enforces the single-instance rule via module-level TWeakPtr<SWindow>. */
	static void OpenBulkExtractor(
		const TArray<TSoftObjectPtr<UTexture2D>>& InitialTextures,
		TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> TargetProfile = nullptr);

	/** Open the bulk extractor with the given sheets pre-grouped as ONE de-bake set (the
	 *  "De-bake Shared Base…" / Paper2DPlus.OpenDebakeVariants entry point). An already-open
	 *  window keeps its session state — the sheets are appended and grouped in place. */
	static void OpenBulkExtractorForDebake(const TArray<TSoftObjectPtr<UTexture2D>>& VariantSheets);

	/** True for statuses that satisfy the "ready to extract" gate without needing a confirmed grid.
	 *  Public: pinned by Paper2DPlusBulkDebakeAcceptTest. */
	static bool StatusCountsAsConfirmed(EBulkExtractorTextureStatus Status);

	/** True for statuses excluded from sprite/flipbook creation, cell-size maps, and source deletion.
	 *  Public: pinned by Paper2DPlusBulkDebakeAcceptTest. */
	static bool StatusExcludedFromExtract(EBulkExtractorTextureStatus Status);

private:
	TArray<TSharedPtr<FBulkExtractorTextureState>> TextureStates;
	TSharedPtr<FBulkExtractorTextureState> SelectedTexture;

	/** Search filter for the texture list (Unit 8: TASK-34). */
	FString TextureSearchFilter;
	TArray<TSharedPtr<FBulkExtractorTextureState>> FilteredTextureStates;

	/** Status filter for the texture list. Empty = show every status. */
	TSet<EBulkExtractorTextureStatus> StatusFilter;

	/** One-click "show only the rows that block Extract All". Its predicate is exactly
	 *  !StatusCountsAsConfirmed — the same one GetExtractionBlockerText counts — so the filter and
	 *  the blocker readout can never disagree about which rows are at fault. */
	bool bShowOnlyBlocking = false;

	EBulkExtractorSortMode SortMode = EBulkExtractorSortMode::Name;

	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> TargetProfile;

	FSpriteDetectionParams DetectionParams;

	/** Which detector decides a texture's frame layout. Frames (the gutter rule) is the default;
	 *  island detection stays available for sheets it suits better. */
	EBulkDetectionMode DetectionMode = EBulkDetectionMode::Frames;
	/** Smallest cell size the frame-grid search may consider. */
	int32 FrameGridMinCell = 16;
	/** Number of columns for grid detection mode. */
	int32 GridSpriteCount = 1;
	/** Number of rows for grid detection mode. */
	int32 GridRowCount = 1;

	/** Cached count of textures needing padding (-1 = stale, recompute on next read). */
	int32 CachedPadCount = -1;

	/** Memo for the extraction gate. IsReadyForExtraction() IS GetExtractionBlockerText().IsEmpty(),
	 *  and both the button tooltip and the bottom-bar status line read the same memo — otherwise the
	 *  gate would resolve every soft texture pointer three times per Slate paint. Invalidated only
	 *  through InvalidateDerivedCounts(), which is also the CachedPadCount seam, so a future mutation
	 *  site cannot refresh one token and forget the other. */
	mutable bool bCachedGateValid = false;
	mutable FText CachedBlockerText;

	/** Drop every derived per-batch readout (pad count + extraction gate). Call from any site that
	 *  changes a texture's status, grid, or membership in the batch. */
	void InvalidateDerivedCounts();

	TSharedPtr<SListView<TSharedPtr<FBulkExtractorTextureState>>> TextureListView;
	TSharedPtr<SSpriteExtractorCanvas> CenterCanvas;

	/** Per-row inline-rename widgets, keyed by state identity (rows are virtualized, so an index
	 *  key would go stale). Weak: a scrolled-away row is free to die. */
	TMap<TSharedPtr<FBulkExtractorTextureState>, TWeakPtr<SInlineEditableTextBlock>> TextureNameTexts;

	/** F2 target awaiting its row widget (the row may not be generated yet). */
	TSharedPtr<FBulkExtractorTextureState> PendingRenameTexture;

	/** Select the texture at the given index in TextureStates and update the list + canvas. */
	void SelectTextureByIndex(int32 Index);

	/** THE single selection entry point — select this state object and update the list + canvas.
	 *  Index-based callers funnel through here so an index is never reinterpreted across the
	 *  master (TextureStates) and visible (FilteredTextureStates) arrays. */
	void SelectTextureState(TSharedPtr<FBulkExtractorTextureState> State);

	/** Sort TextureStates by display name, then rebuild FilteredTextureStates using
	 *  TextureSearchFilter. Both key off the same resolved display name, so a renamed row re-sorts
	 *  AND becomes searchable under its new name in one pass.
	 *  INVARIANT: this reorders TextureStates, so it must never be called from inside a loop that
	 *  holds an index into TextureStates. */
	void RefreshFilteredTextureList();

	/** Put the selected row's name widget into inline edit mode (F2). */
	FReply BeginInlineRenameOnSelectedTexture();

	/** Re-run detection on the selected texture using current DetectionParams. */
	void ReRunDetectionOnSelected();

	/** Build the per-texture row in the left pane. */
	TSharedRef<class ITableRow> GenerateTextureRow(
		TSharedPtr<FBulkExtractorTextureState> InItem,
		const TSharedRef<class STableViewBase>& OwnerTable);

	void OnTextureSelectionChanged(TSharedPtr<FBulkExtractorTextureState> InItem, ESelectInfo::Type SelectInfo);

	/** Run detection + grid inference for a single texture state, per DetectionMode. Idempotent. */
	void RunDetectionAndInference(TSharedPtr<FBulkExtractorTextureState> State);

	/** Frame-grid pass over the WHOLE batch: one candidate per texture, then folder consensus (which
	 *  needs every sheet's occupancy at once and therefore has no per-texture home). Lazy per-texture
	 *  detection cannot do this, so it is an explicit action. */
	void RunFrameGridDetectionForAll();

	/** Advance the selected texture to Confirmed (or SkippedNoPad after pad). */
	FReply OnAcceptGridClicked();

	/** Handlers for Cols/Rows spinboxes. */
	void OnColsChanged(int32 NewValue);
	void OnRowsChanged(int32 NewValue);
	TOptional<int32> GetColsValue() const;
	TOptional<int32> GetRowsValue() const;

	/** Divisibility warning text (empty when grid divides cleanly). */
	FText GetDivisibilityWarning() const;

	/** Returns the label text for a texture's status chip. */
	static FText GetStatusLabel(EBulkExtractorTextureStatus Status);

	/** Returns the colored brush for a texture's status chip. */
	static FLinearColor GetStatusColor(EBulkExtractorTextureStatus Status);

	/** Legible label color for a chip filled with GetStatusColor — black on a light fill, near-white
	 *  on a dark one. Hard-coded black was unreadable on the darker statuses. */
	static FLinearColor GetStatusTextColor(EBulkExtractorTextureStatus Status);

	/** Map SelectedTexture->Status to the canvas grid-overlay color state. */
	ESpriteCanvasGridState GetGridState() const;

	// ---- Unit 5: cross-texture check + shared-texture guard + auto-pad with rollback ----

	/** "Auto-Pad if Needed" bottom-bar button handler. Runs the full cross-texture pipeline. */
	FReply OnAutoPadClicked();

	/** Returns true iff every texture in the batch has been Confirmed / Overridden (ready for pad). */
	bool AreAllGridsConfirmed() const;

	/** Live extraction gate: every non-de-bake texture has a valid current cell size and all of those
	 *  sizes match. This deliberately ignores cached Padded/Skipped chips.
	 *  Defined as GetExtractionBlockerText().IsEmpty() so the gate and the reason cannot drift. */
	bool IsReadyForExtraction() const;

	/** Empty when Extract All is allowed; otherwise the single most relevant reason it is blocked,
	 *  naming counts and the offending rows. Memoized — see bCachedGateValid. */
	FText GetExtractionBlockerText() const;

	/** Uncached body of GetExtractionBlockerText. */
	FText ComputeExtractionBlockerText() const;

	/** Compute per-texture confirmed cell size. Key = texture raw ptr (valid within one pad run).
	 *  OutSkipped, when supplied, receives the display name of every non-excluded row that had to be
	 *  dropped (unloadable, no grid, non-dividing grid, degenerate cell) — the rows that make the map
	 *  smaller than the batch and silently block extraction. */
	TMap<UTexture2D*, FIntPoint> BuildPerTextureCellSizeMap(TArray<FString>* OutSkipped = nullptr) const;

	/** Run the three-phase pad pipeline (preflight → snapshot-acquire → pad-apply) with rollback.
	 *  There is no ground-plane input: PadTextureInPlace anchors midpoint-to-midpoint. */
	bool RunThreePhasePadPipeline(const TArray<UTexture2D*>& ToPadList, FIntPoint MaxCellDims);

	/** Restore every active pad snapshot. Successful restores delete their sidecars; any failed
	 *  manifest remains available for recovery and the caller must fail closed. */
	bool RestoreCurrentPadSnapshots();
	/** Accept the padded textures as permanent full-cell sources and delete only the backups. */
	void DiscardCurrentPadSnapshots();

	/** Grid edits invalidate the entire group pad result. Restore first, then return every pad-derived
	 *  status to Confirmed so the edited row can move to Overridden. */
	bool InvalidatePaddingForGridEdit();

	/** Mutable pad-run state. Manifests live only for the current session until Accept / Undo. */
	FGuid CurrentPadRunGuid;
	TArray<FPadSnapshotManifest> CurrentPadManifests;
	/** Original cell size per padded texture — selects source cells independently of flipbook order. */
	TMap<UTexture2D*, FIntPoint> PerTextureSourceCellSizes;
	/** Row-major per-cell paste deltas per padded texture — handed to Unit 6's metadata remap. */
	TMap<UTexture2D*, TArray<FIntPoint>> PerTexturePasteOffsets;
	/** False only when the last rollback/restore attempt left at least one padded texture unresolved. */
	bool bLastPadRollbackComplete = true;

	// ---- Unit 7: commit + name prefix ----

	/** Prefix override applied to every generated asset name ({Prefix}_{TextureBase}_NN). */
	FString NamePrefix;

	/** When false, FlipbookName in the profile uses DisplayName only (no prefix). */
	bool bIncludePrefixInProfileName = true;

	/** Output path for extracted sprites/flipbooks/textures. Empty = derive from profile or first texture. */
	FString OutputPathOverride;

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
	/** Off by default: trimming is a deliberate choice, not a safe default. It packs every sprite to
	 *  its shared uniform bounds and BAKES the cell-midpoint pivot, which is correct but is also a
	 *  one-way transformation of the extracted output — a designer who did not ask for it should get
	 *  plain full-cell sprites. */
	bool bTrimSprites = false;

	/** When true, links extracted flipbooks to the selected CharacterProfile. */
	bool bLinkToProfile = true;

	/** Cached runtime-only PaperZD availability. No PaperZD headers or linked module are required. */
	bool bPaperZDAuthoringAvailable = false;

	/** Opt-in set from the PaperZD modal. When enabled, only flipbooks successfully committed by
	 *  this extraction run are offered to the shared sequence creator. */
	bool bCreatePaperZDSequencesAfterExtract = false;
	TStrongObjectPtr<UPaper2DPlusCharacterProfileAsset> PaperZDConfiguredProfile;

	/** When true, auto-creates flipbook groups on the profile from folder organizer structure. */
	bool bAutoCreateGroups = true;

	/** Returns true when any texture in the batch has a folder assignment (its SubfolderPath is
	 *  set by the folder organizer Apply). Folder assignments live on the per-texture state, not
	 *  an index-keyed map, so they survive texture removal/reindex (F13). */
	bool HasAnyFolderAssignment() const;

	/** Persisted collapse state for folder organizer (Unit 10: TASK-36). */
	TSet<FString> PersistentCollapsedFolders;

	/** Persisted batch rename field values across dialog reopens. */
	FString PersistentBatchFindStr;
	FString PersistentBatchReplaceStr;
	FString PersistentBatchRemoveStr;
	FString PersistentBatchSuffixStr;

	/** Commit button handler — creates sprites + flipbooks, applies alignment, updates profile. */
	FReply OnCommitClicked();

	/** Opens the batch rename modal window. */
	FReply OnBatchRenameClicked();

	/** Opens the folder organizer modal window. */
	FReply OnFolderOrganizerClicked();

	/** Write the current folder organization (naming settings + nested tree + per-texture rows) to a
	 *  JSON file the user picks. */
	FReply OnExportOrganizationClicked();

	/** Read a folder organization back from JSON, matching textures by display name (then by asset
	 *  path, then through the payload's rename map) and REPORTING everything it could not place. */
	FReply OnImportOrganizationClicked();

	/** Opens the Character Data PaperZD source/sequence surface for the selected profile. */
	FReply OnPaperZDSequencesClicked();

	/** Bulk-extract flow: creates one flipbook per texture, sprites under it, attaches to profile. */
	TOptional<TArray<UPaperFlipbook*>> CommitBulkExtract();

	// ---- De-bake groups (variant sheets → recovered base + overlays, FVariantDebake) ----

	TArray<TSharedPtr<FDebakeGroupState>> DebakeGroups;
	TSharedPtr<FDebakeGroupState> SelectedDebakeGroup;

	/** Canvas preview mode state (valid while a previewed group is selected + bShowDebakePreview). */
	bool bShowDebakePreview = false;
	EDebakePreviewView DebakePreviewView = EDebakePreviewView::Base;
	int32 DebakePreviewVariantIndex = 0;

	/** Hosts rebuilt imperatively (structure changes): the group rows + the selected group's editor. */
	/** De-bake lives in its OWN window, not the right panel. It is an occasional, multi-step tool
	 *  with its own preview; hosting it inline forced it to hijack the main center canvas, so
	 *  selecting a group silently replaced the texture the rest of the window was showing. The
	 *  window owns its preview canvas, so the extractor's canvas is never repurposed. */
	TWeakPtr<SWindow> DebakeWindowPtr;
	TSharedPtr<SSpriteExtractorCanvas> DebakePreviewCanvas;

	/** Open (or focus) the De-bake window. */
	void OpenDebakeWindow();

	/** Add the linked Character Profile to a Character Catalog, mirroring the Character Profile
	 *  editor's own intake. Extraction is where a character first exists, so this is the natural
	 *  moment to register it; doing it here saves a round trip through the Catalog editor.
	 *  Prompts for the catalog when the project has no configured default. */
	FReply OnAddToCatalogClicked();

	/** True when there is a linked profile that is not already in the resolved catalog. */
	bool CanAddToCatalog() const;

	/** Project-configured Catalog, loaded. Null when unset or unloadable. */
	class UPaper2DPlusCharacterCatalogAsset* ResolveTargetCatalog() const;

	/** Selected textures that are not already in a group — the de-bake entry condition. */
	int32 CountUngroupedSelectedTextures() const;

	TSharedPtr<SVerticalBox> DebakeGroupListBox;
	TSharedPtr<SVerticalBox> DebakeGroupEditorBox;

	/** Create a group from the current multi-selection (>= 2 ungrouped textures). */
	FReply OnCreateDebakeGroupClicked();

	/** Append textures (added to the list if missing) as a new pre-made group — the reroute seam. */
	void AddDebakeGroupFromTextures(const TArray<TSoftObjectPtr<UTexture2D>>& VariantSheets);

	void SelectDebakeGroup(TSharedPtr<FDebakeGroupState> Group);
	void RemoveDebakeGroup(TSharedPtr<FDebakeGroupState> Group);
	TSharedPtr<FDebakeGroupState> FindGroupContaining(const TSharedPtr<FBulkExtractorTextureState>& State) const;

	/** Create + select a group over the given (already-listed) states. Returns null when < 2 valid. */
	TSharedPtr<FDebakeGroupState> CreateDebakeGroupFromStates(const TArray<TSharedPtr<FBulkExtractorTextureState>>& States);

	/** Name-match VFX prefill from the asset registry (folder of the first member, recursive). */
	void PrefillGroupVfx(TSharedPtr<FDebakeGroupState> Group);

	/** Flag the cached preview stale after a group-setting change (never recomputes eagerly). */
	void MarkGroupStale(TSharedPtr<FDebakeGroupState> Group);

	/** Drop dead member weak-refs + clamp ReferenceIndex. Returns surviving member count. */
	int32 CompactGroupMembers(TSharedPtr<FDebakeGroupState> Group);

	/** Load members + VFX, run FVariantDebake, build the base preview texture. Synchronous. */
	bool PreviewDebakeGroup(TSharedPtr<FDebakeGroupState> Group);

	/** Materialise base + overlays as assets, append them as confirmed entries, flip members to
	 *  DebakeSource. Auto-(re)computes a stale/missing preview first. */
	bool AcceptDebakeGroup(TSharedPtr<FDebakeGroupState> Group);

	/** Undo an accept: remove appended entries, restore member statuses (assets stay on disk). */
	void RevertDebakeGroup(TSharedPtr<FDebakeGroupState> Group);

	/** True while the canvas should show the selected group's de-bake preview instead of a texture. */
	bool IsDebakePreviewActive() const;

	/** The preview texture for the current view/variant (lazily builds overlay/composite textures). */
	UTexture2D* GetOrBuildDebakePreviewTexture();

	/** Point the canvas at the active preview texture (or back at the selected list texture). */
	void UpdateCanvasForDebakePreview();

	/** Resolve a group's output folder (override else beside the first member). */
	FString ResolveGroupOutputPath(const TSharedPtr<FDebakeGroupState>& Group) const;

	/** Rebuild the de-bake section hosts (group rows + selected group editor). */
	void RefreshDebakeSection();

	TSharedRef<SWidget> BuildDebakeGroupsSection();
	TSharedRef<SWidget> BuildDebakeGroupRow(TSharedPtr<FDebakeGroupState> Group);
	TSharedRef<SWidget> BuildDebakeGroupEditor(TSharedPtr<FDebakeGroupState> Group);
	TSharedRef<SWidget> BuildDebakeCanvasStrip();
};
