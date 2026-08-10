// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "FrameCueDataProvider.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "ProfilePanelFocusSeat.h"
#include "ProfileToolPanelProvider.h"
#include "AnimationTimeline.h"
#include "Containers/Ticker.h"
#include "Editor/EditorEngine.h"
#include "IDetailsView.h"
#include "Misc/NotifyHook.h"
#include "UObject/StrongObjectPtr.h"

class SVerticalBox;
class SHorizontalBox;
class SFrameEventPreviewCanvas;
class SFrameEventTimelineTrack;
class SFrameCueTimeline;
class SCurveTrackStack;
class SEditableTextBox;
class FCharacterProfileEditorModel;
class FPaper2DPlusFrameCuePreviewHost;
class FPaper2DPlusFrameCuePendingPlacement;
class FPaper2DPlusFrameCueSelectionDrivenDetailsTest;
struct FPaper2DPlusFrameCuePlacementTarget;
struct FPaper2DPlusFrameCuePlacementResult;
enum class EPaper2DPlusFrameCuePlacementKind : uint8;
class UBlueprint;
class UPaper2DPlusFrameCuePreviewContext;

namespace Paper2DPlusFrameCueTimeline
{
	struct FPrimarySelection;
}

/**
 * Worldless placement-mutation seam shared by profile tools, Layer tools, and editor automation.
 * Cue placements are always transactional instanced subobjects owned directly by their data asset.
 */
namespace Paper2DPlusFrameCueEditorAuthoring
{
	enum class EPaper2DPlusFrameCueTypePickerStatus : uint8
	{
		Ready,
		ReadyWithDiscoveryErrors,
		NoSearchMatch,
		DiscoveryError,
		NeedsRepair,
		FirstRun
	};

	/** Normal Edit is restricted to specialized Cue assets; generic legacy assets require recovery. */
	enum class EPaper2DPlusFrameCueTypeEditRoute : uint8
	{
		None,
		RestrictedSpecialized,
		LegacyRecoveryRequired
	};

	/** Declarative create-action model consumed by both Slate and automation. */
	PAPER2DPLUSEDITOR_API TArray<FText> GetCreateCueTypeActionLabels();

	/**
	 * Pure picker state used by Slate and focused automation. Discovery is synchronous, so there is
	 * deliberately no transient "refreshing" state: every resolve describes a settled result.
	 */
	PAPER2DPLUSEDITOR_API EPaper2DPlusFrameCueTypePickerStatus ResolveCueTypePickerStatus(
		int32 ReadyTypeCount,
		int32 FilteredTypeCount,
		int32 RejectedTypeCount,
		int32 DiscoveryErrorCount,
		bool bHasSearchText);

	/** Pure clamped Up/Down navigation used while keyboard focus remains in the search box. */
	PAPER2DPLUSEDITOR_API int32 ResolveCueTypePickerNavigationIndex(
		int32 CurrentIndex,
		int32 ItemCount,
		int32 Direction);

	/** Classifies a selected placement without opening an editor or mutating its Blueprint. */
	PAPER2DPLUSEDITOR_API EPaper2DPlusFrameCueTypeEditRoute ResolveCueTypeEditRoute(
		const UClass* CueClass,
		UBlueprint*& OutBlueprint);

	PAPER2DPLUSEDITOR_API UPaper2DPlusCueBase* CreatePlacement(
		UObject* Owner,
		UClass* CueClass,
		int32 AnchorFrame,
		int32 AnimationKeyFrameCount);

	PAPER2DPLUSEDITOR_API UPaper2DPlusCueBase* DuplicatePlacement(
		UObject* Owner,
		const UPaper2DPlusCueBase* SourceCue,
		int32 AnimationKeyFrameCount);

	PAPER2DPLUSEDITOR_API void ClampPlacementToAnimation(
		UPaper2DPlusCueBase* Cue,
		int32 AnimationKeyFrameCount);

	/** Resolve a placement by object identity after a callback or transaction may have reordered its array. */
	PAPER2DPLUSEDITOR_API int32 ResolvePlacementIndex(
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues,
		TWeakObjectPtr<UPaper2DPlusCueBase> CueIdentity);

	/**
	 * Ends one active Cue State before an authoring mutation. Removing it from the active set before
	 * notifying makes the End exactly-once even if preview callbacks re-enter. By default the host is
	 * reset after notification; callers coordinating several overlapping ranges may defer that single
	 * global reset until all Ends have been delivered.
	 *
	 * The active-range ledger is a non-owning observation set (see SFrameEventEditor), so it is keyed
	 * by weak pointer: a placement destroyed under an open tab simply stops being findable in it.
	 */
	PAPER2DPLUSEDITOR_API bool EndActiveRangePreview(
		UPaper2DPlusCueBase* Cue,
		EPaper2DPlusFrameCueEndReason EndReason,
		int32 PreviousFrame,
		TSet<TWeakObjectPtr<UPaper2DPlusCueBase>>& ActiveRangeCues,
		FPaper2DPlusFrameCuePreviewHost* PreviewHost,
		FPaper2DPlusFrameCueContext* OutEndContext = nullptr,
		bool bResetPreviewHost = true,
		EPaper2DPlusFrameCueEvaluationMode EvaluationMode =
			EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek);
}

/**
 * Frame Cues editor tab. The current external workspace keeps preview and the unified Cue/curve
 * timeline central; direct timeline selection drives one contextual Details panel plus Preview.
 * The embedded compatibility host mounts those same Details/Preview bodies beside the timeline.
 */
class SFrameEventEditor : public SCompoundWidget,
	public FEditorUndoClient,
	public FNotifyHook,
	public IProfileToolPanelProvider
{
public:
	/** Retired external panel ID retained only so stale saved layouts can be recognized and ignored. */
	static const FName CuesPanelId;
	static const FName DetailsPanelId;
	static const FName PreviewPanelId;

	SLATE_BEGIN_ARGS(SFrameEventEditor) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		/** Optional storage seam. Omitted = the byte-compatible Character Profile provider. */
		SLATE_ARGUMENT(TSharedPtr<FFrameCueDataProvider>, DataProvider)
		/** Exactly one layout/navigation owner for this panel instance. */
		SLATE_ARGUMENT(FProfileToolPanelHostContract, HostContract)
	SLATE_END_ARGS()

	SFrameEventEditor();
	void Construct(const FArguments& InArgs);
	virtual ~SFrameEventEditor();

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
	virtual void NotifyPostChange(
		const FPropertyChangedEvent& PropertyChangedEvent,
		FProperty* PropertyThatChanged) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// IProfileToolPanelProvider
	virtual FProfileToolPanelHostContract GetHostContract() const override { return HostContract; }
	virtual void GetContextualPanels(TArray<FProfileToolPanelDescriptor>& OutPanels) const override;
	void HandleHostActivated();
	void HandleHostDeactivated();

	void StopPlayback();
	void RefreshAll();
	void RefreshFlipbookList();
	/** True while this panel transacts, while the cue-details PropertyEditor owns a live gesture, OR
	 *  while a curve-track gesture is live. The curve-track
	 *  stack's SCurveEditors open their OWN engine transactions (the one sanctioned exception to the
	 *  panel transaction template), so the model's deferred external-modify broadcast must see this
	 *  gate report true mid-gesture or it triggers a RefreshAll rebuild cascade under a live drag.
	 *  Implemented in the .cpp (needs the SCurveTrackStack definition). */
	bool HasActiveTransaction() const;
	bool HasPendingRefreshForTests() const { return bNeedsRefresh; }
	void FinishEventDetailsPropertyChangeForTests();
	void SelectCueForTests(int32 EventIndex) { OnEventSelected(EventIndex); }
	TSharedPtr<FFrameCueDataProvider> GetDataProviderForTests() const { return DataProvider; }
	TSharedPtr<SFrameCueTimeline> GetUnifiedTimelineForTests() const { return UnifiedTimeline; }
	TSharedPtr<SCurveTrackStack> GetCurveStackForTests() const { return CurveStack; }
	FProfileToolPanelHostContract GetHostContractForTests() const { return HostContract; }
	const FPaper2DPlusFrameCuePreviewHost* GetPreviewHostForTests() const { return PreviewHost.Get(); }
	UPaper2DPlusFrameCuePreviewContext* GetPreviewContextForTests() const;
	UPaper2DPlusCueBase* GetSelectedCueForTests() const { return ResolveCueIdentity(SelectedCueIdentity); }
	bool IsHostActiveForTests() const { return bHostActive; }
	bool IsPlayingForTests() const { return bIsPlaying; }
	int32 GetSelectedFrameIndexForTests() const { return SelectedFrameIndex; }
	int32 GetSelectedCueIndexForTests() const { return SelectedEventIndex; }
	int32 GetPreviewDisplayFrameIndexForTests() const { return PreviewDisplayFrameIndex; }
	EPaper2DPlusFrameCueEvaluationMode GetPreviewEvaluationModeForTests() const
	{
		return PreviewEvaluationMode;
	}
	EPaper2DPlusFrameCueEvaluationMode GetLastPreviewEndEvaluationModeForTests() const
	{
		return LastPreviewEndEvaluationMode;
	}
	EPaper2DPlusFrameCueEndReason GetLastPreviewEndReasonForTests() const
	{
		return LastPreviewEndReason;
	}
	int32 GetLastPreviewEndFrameForTests() const { return LastPreviewEndFrame; }
	void ApplyDeferredHostFocusForTests();
	int32 GetPreviewResourceCountForTests() const;
	void PreviewSelectedCueForTests() { PreviewSelectedCue(); }
	int32 GetContextPanelBuildCountForTests(FName PanelId) const
	{
		return ContextPanelBuildCounts.FindRef(PanelId);
	}
	int32 GetContextPanelResolvedFrameForTests(FName PanelId) const
	{
		return ContextPanelResolvedFrames.FindRef(PanelId);
	}
	UPaper2DPlusCueBase* GetContextPanelResolvedCueForTests(FName PanelId) const
	{
		return ContextPanelResolvedCues.FindRef(PanelId).Get();
	}
	void DuplicateSelectedCueForTests() { DuplicateEvent(SelectedEventIndex); }
	void RemoveSelectedCueForTests() { RemoveEvent(SelectedEventIndex); }
	void AddCueForTests(UClass* CueClass)
	{
		if (DataProvider.IsValid())
		{
			AddFrameCue(CueClass, DataProvider->GetScopedAnimationIdentity(SelectedFlipbookIndex));
		}
	}
	int32 GetCueCountForTests() const
	{
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetFrameCues();
		return Cues ? Cues->Num() : 0;
	}
	/**
	 * The exact predicate the Cue details view is wired to, evaluated against the live selection.
	 *
	 * Not a parallel copy: the bound FIsPropertyVisible delegate unwraps its FPropertyAndParent and
	 * calls straight into this, so there is only ever one filter body to be right or wrong.
	 */
	bool IsCuePlacementPropertyVisibleForTests(const FProperty* Property) const;
	/** The details-pane header the tab renders above the Cue property rows. */
	FText GetDetailsSummaryForTests() const { return GetTimelineDetailsSummary(); }
	FName GetSelectedCurveForTests() const { return GetSelectedCurveName(); }
	FText GetSelectedCurveModeTextForTests() const { return GetSelectedCurveModeText(); }
	FText GetSelectedCurveOrphanTextForTests() const { return GetSelectedCurveOrphanText(); }
	void BeginSelectedCueTransactionForTests();
	void BeginTimelineInteractionForTests(bool bResize = false);
	bool HasTimelineInteractionForTests() const;
	/** Preview-viewport offset-gizmo gesture seams (Spawn Flipbook Cue). */
	void BeginOffsetGizmoInteractionForTests();
	bool HasOffsetGizmoInteractionForTests() const;
	void ApplyOffsetGizmoDeltaForTests(const FVector& WorldDelta);
	void EndOffsetGizmoInteractionForTests();
	void SeekFrameForTests(int32 FrameIndex) { OnFrameClicked(FrameIndex); }
	/** The exact transition the 60Hz playback ticker performs, driven without a wall clock. */
	void AdvancePlaybackFrameForTests(int32 FrameIndex)
	{
		SelectedFrameIndex = FrameIndex;
		DispatchPreviewTransition(FrameIndex, /*bFromPlayback*/ true);
	}
	int32 GetActiveRangeCountForTests() const { return CountLiveActiveRangeCues(); }
	TSharedRef<FPaper2DPlusFrameCuePendingPlacement> CreatePendingPlacementForTests(
		const FPaper2DPlusFrameCuePlacementTarget& Target,
		EPaper2DPlusFrameCuePlacementKind Kind,
		bool bReadyToPlace);
	int32 GetPendingPlacementCompletionCountForTests() const
	{
		return PendingPlacementCompletionCountForTests;
	}

	/** F8 (ADV-5): true when a model broadcast is landing within 1-2 frames of the curve stack's
	 *  self-write stamp — the deferred-tick analogue of the Rule A self-echo flag. Such broadcasts
	 *  are CURVE-ONLY: the handlers skip the full RefreshAll (event-list/details ClearChildren
	 *  rebuilds = visible click flicker) and do RefreshTracks + invalidate instead. Implemented in
	 *  the .cpp (needs the SCurveTrackStack definition). */
	bool IsCurveOnlySelfEcho() const;

	/** The single picker-visible Frame Cue class collection seam shared by profile and Layer tools.
	 *  Collects the concrete UPaper2DPlusCueBase subclasses (C++ and loaded BP), split by the internal
	 *  Moment/Range timing kind and sorted by display name. Excludes Abstract/Deprecated/Hidden AND
	 *  HideDropdown classes — matching
	 *  how engine class pickers respect UCLASS(HideDropdown), so test-only event types never leak into
	 *  a designer-facing menu. Public static = the worldless test seam. */
	static void CollectFrameCueClasses(TArray<UClass*>& OutMomentClasses, TArray<UClass*>& OutRangeClasses);

private:
	TSharedPtr<FCharacterProfileEditorModel> Model;
	TSharedPtr<FFrameCueDataProvider> DataProvider;
	FProfileToolPanelHostContract HostContract = FProfileToolPanelHostContract::Embedded();
	bool bHostActive = false;
	FProfilePanelFocusSeat HostFocusSeat;
	EActiveTimerReturnType ApplyDeferredHostFocus(double CurrentTime, float DeltaTime);
	mutable TMap<FName, int32> ContextPanelBuildCounts;
	mutable TMap<FName, int32> ContextPanelResolvedFrames;
	mutable TMap<FName, TWeakObjectPtr<UPaper2DPlusCueBase>> ContextPanelResolvedCues;

	// Model delegate handles
	FDelegateHandle ModelFlipbookSelectionHandle;
	FDelegateHandle ModelFrameSelectionHandle;
	FDelegateHandle ModelGroupCollapseHandle;
	FDelegateHandle ModelSearchTextHandle;
	FDelegateHandle ModelExternalModifiedHandle;
	FDelegateHandle ModelAssetDataChangedHandle;
	FDelegateHandle ModelLayerSelectionHandle;
	FDelegateHandle ModelLayerVisibilityHandle;
	/** Reinstancing channel: placements can be replaced under an open tab by a Cue Type edit or delete. */
	FDelegateHandle CuePlacementsReplacedHandle;
	void HandleCuePlacementsReplaced(const TMap<UObject*, UObject*>& Replacements);

	// Selection state
	int32 SelectedFlipbookIndex = INDEX_NONE;
	int32 SelectedFrameIndex = 0;
	/** Frame rendered by the preview subject. Preview Selected pins this to the Cue anchor until a real
	 *  frame-selection/playback change, even when the timeline playhead stays elsewhere. */
	int32 PreviewDisplayFrameIndex = 0;
	int32 SelectedEventIndex = INDEX_NONE;
	/** Stable selection identity. Array indices are only a view coordinate and can change after undo,
	 *  reimport, profile replacement, or another editor mutating the cue list. */
	FFrameCueStableIdentity SelectedCueIdentity;
	/** Placement pinned for one timeline drag/nudge. Every write resolves it against the live source. */
	FFrameCueStableIdentity TimelineDragCueIdentity;
	/** Placement pinned for one preview-viewport offset-gizmo drag (Spawn Flipbook Cue only). */
	FFrameCueStableIdentity OffsetDragCueIdentity;

	// Playback state
	bool bIsPlaying = false;
	double PlaybackTime = 0.0;
	FFlipbookTimingData CachedTiming;
	FTSTicker::FDelegateHandle PlaybackTickerHandle;
	/** Advances finite preview resources created by a selection/scrub while playback is stopped. */
	FTSTicker::FDelegateHandle PreviewResourceTickerHandle;
	int32 LastReportedPreviewResourceCount = INDEX_NONE;

	// Editor-only host owns the isolated preview world plus every optional adapter resource.
	TUniquePtr<FPaper2DPlusFrameCuePreviewHost> PreviewHost;
	/** Explicitly warmed current-animation Cue flipbooks; Slate paint/layout paths are no-load. */
	TArray<TStrongObjectPtr<UPaperFlipbook>> WarmedAnimationCueEffectFlipbooks;
	// Last key-frame the preview dispatch ran for (playback OR scrub). INDEX_NONE = unseeded (no prior
	// frame). On the FIRST dispatch from an unseeded state, DispatchPreviewTransition seeds the previous
	// frame to the CURRENT target frame (a no-op span N->N) so only events anchored on N evaluate — NOT
	// a -1..N span that would replay every one-shot on frames 0..N. Mirrors the runtime's
	// PreviousFrameIndex once seeded.
	int32 LastPreviewFrame = INDEX_NONE;
	/** Current callback frame while a transition snapshot is in flight; otherwise INDEX_NONE. */
	int32 PreviewDispatchFrame = INDEX_NONE;
	/** Evaluation source for the currently open preview lifecycle session. */
	EPaper2DPlusFrameCueEvaluationMode PreviewEvaluationMode =
		EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek;
	/** Last completed End snapshot, retained only as a narrow automation observation seam. */
	EPaper2DPlusFrameCueEvaluationMode LastPreviewEndEvaluationMode =
		EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek;
	EPaper2DPlusFrameCueEndReason LastPreviewEndReason =
		EPaper2DPlusFrameCueEndReason::None;
	int32 LastPreviewEndFrame = INDEX_NONE;
	// Editor-side mirror of the runtime component's active Cue States — tracks which ranges
	// have Begun but not yet Ended during preview, so Begin/Update/End pair correctly while scrubbing,
	// playing, looping, and on teardown.
	//
	// WEAK by contract. This is a Slate widget, not an FGCObject: state held here is invisible to the
	// garbage collector AND to the reference replacement ObjectTools::ForceDeleteObjects performs, so
	// a strong-shaped entry (raw pointer or TObjectPtr) would dangle the moment a Content Browser
	// force delete of a Cue Type destroyed a placement under an open tab — and IsValid() guards
	// pending-kill, not freed memory. The runtime component's own ledger keeps its GC-safe UPROPERTY
	// semantics; only this editor mirror observes without owning. Every hand-off to the shared
	// dispatch helpers goes through ResolveActiveRangeCues/StoreActiveRangeCues so those helpers only
	// ever see placements that still resolve.
	TSet<TWeakObjectPtr<UPaper2DPlusCueBase>> EditorActiveRangeCues;
	/** The live placements of the weak ledger, in the form the shared dispatch helpers take. */
	TSet<TObjectPtr<UPaper2DPlusCueBase>> ResolveActiveRangeCues() const;
	/** Writes a dispatch helper's post-call active set back to the weak ledger; stale entries drop. */
	void StoreActiveRangeCues(const TSet<TObjectPtr<UPaper2DPlusCueBase>>& LiveActiveRanges);
	/** Ledger entries whose placement still exists — the only ones an End can ever reach. */
	int32 CountLiveActiveRangeCues() const;
	// Re-entry guard mirroring the runtime's bDispatchingFrameEvents: a preview BP handler that
	// synchronously re-enters dispatch (or a force-end) must not mutate EditorActiveRangedEvents
	// out from under the outer call.
	bool bDispatchingPreview = false;
	/** First re-entrant teardown boundary wins and drains after the current snapshot is stored. */
	bool bPreviewForceEndPending = false;
	bool bPendingPreviewHostReset = false;
	EPaper2DPlusFrameCueEndReason PendingPreviewEndReason =
		EPaper2DPlusFrameCueEndReason::None;
	EPaper2DPlusFrameCueEvaluationMode PendingPreviewEndEvaluationMode =
		EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek;
	int32 PendingPreviewEndFrame = INDEX_NONE;

	bool bNeedsRefresh = false;
	/** True from the details view's first pre-change until its finished-change callback. PropertyEditor
	 *  owns the UObject transaction; this flag owns only refresh liveness. */
	bool bEventDetailsPropertyChangeActive = false;
	int32 PendingPlacementCompletionCountForTests = 0;

	// Transaction helpers
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	void BeginTransaction(const FText& Description, UPaper2DPlusCueBase* CueToModify = nullptr);
	void EndTransaction();

	// Sub-widgets
	TSharedPtr<SVerticalBox> FlipbookListBox;
	TSharedPtr<SHorizontalBox> FrameStripBox;
	TSharedPtr<IDetailsView> EventDetailsView;
	TWeakObjectPtr<UPaper2DPlusCueBase> DetailsCueSubject;
	TWeakPtr<SWidget> DetailsPanelWidget;
	TWeakPtr<SWidget> PreviewPanelWidget;
	TSharedPtr<SFrameEventPreviewCanvas> PreviewCanvasWidget;
	TSharedPtr<SFrameEventTimelineTrack> TimelineTrack;
	TSharedPtr<SFrameCueTimeline> UnifiedTimeline;
	/** Per-curve track stack: identity-only legend rows plus one shared graph, under the Cue lanes in
	 *  the same timing scroller. Curve controls live in contextual Details. */
	TSharedPtr<SCurveTrackStack> CurveStack;
	TSharedPtr<SEditableTextBox> SelectedCurveRenameEditor;
	FName CurveDetailsSelection;
	/** Curve identity the current rename text belongs to, even if selection changes before focus loss. */
	FName CurveRenameSubject;
	/** Exact Profile/Layer/animation scope captured with CurveRenameSubject. */
	FProfileScopedAnimationIdentity CurveRenameScope;
	FText CurveDetailsError;

	// UI builders
	TSharedRef<SWidget> BuildUnifiedTimeline();
	TSharedRef<SWidget> BuildCentralWorkspace();
	TSharedRef<SWidget> BuildCueDetailsPanel();
	TSharedRef<SWidget> BuildPreviewPanel();
	TSharedRef<SWidget> BuildCurveTrackContent();
	TSharedRef<SWidget> BuildSelectedCurveModeMenu();
	FText GetTimelineDetailsSummary() const;
	FName GetSelectedCurveName() const;
	bool IsCuePrimarySelection() const;
	bool IsCurvePrimarySelection() const;
	FText GetSelectedCurveModeText() const;
	FText GetSelectedCurveOrphanText() const;
	FProfileScopedAnimationIdentity GetCurrentCurveRenameScope() const;
	bool IsCurveRenameScopeCurrent() const;
	void ResetCurveRenameEditorToSelection();
	void SettleCurveRenameBeforeScopeChange();
	void CommitSelectedCurveRename(const FText& NewText, ETextCommit::Type CommitType);
	/** Cue details filter: timing belongs to the timeline, migration chrome only to migrated placements. */
	bool IsCuePlacementPropertyVisible(const FPropertyAndParent& PropertyAndParent) const;
	void ReconcileTimelineCurveSelection();

	// Refresh
	void RefreshFrameStrip();
	void RefreshDetailsPanel();
	void WarmCurrentAnimationCueEffects();
	void InvalidatePreviewPanel();
	void HandleEventDetailsFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);
	void FinishEventDetailsPropertyChange();

	// Event management
	void ShowAddEventPicker();
	void ShowAddEventPicker(const FPaper2DPlusFrameCuePlacementTarget& Target);
	void QueueAddEventPicker(int32 TargetFrameIndex, FGuid TargetTrackId);
	void CreateNewFrameCueType(
		const FPaper2DPlusFrameCuePlacementTarget& Target);
	TSharedRef<FPaper2DPlusFrameCuePendingPlacement> CreatePendingPlacement(
		const FPaper2DPlusFrameCuePlacementTarget& Target,
		EPaper2DPlusFrameCuePlacementKind Kind,
		bool bReadyToPlace);
	void HandlePendingPlacementCompleted(
		const FPaper2DPlusFrameCuePlacementTarget& Target,
		const FPaper2DPlusFrameCuePlacementResult& Result);
	bool CanEditSelectedCueType() const;
	void EditSelectedCueType();
	void AddFrameCue(
		UClass* CueClass,
		const FProfileScopedAnimationIdentity& TargetScope);
	void AddFrameCue(
		UClass* CueClass,
		const FPaper2DPlusFrameCuePlacementTarget& Target);
	void DuplicateEvent(int32 EventIndex);
	void RemoveEvent(int32 EventIndex);
	void OnEventSelected(int32 EventIndex);
	void HandleTimelinePrimarySelectionChanged(
		const Paper2DPlusFrameCueTimeline::FPrimarySelection& Selection);
	void ResetPreviewForCueMutation(
		UPaper2DPlusCueBase* Cue,
		EPaper2DPlusFrameCueEndReason EndReason);

	// Preview-viewport offset gizmo (Spawn Flipbook Cue). The canvas converts the world drag into
	// authored-offset space; these handlers own the identity pin, the transaction, and the write.
	void OnPreviewCueOffsetDragStarted(UPaper2DPlusCueBase* Cue);
	void OnPreviewCueOffsetChanged(FVector2D NewOffset);
	void OnPreviewCueOffsetDragEnded();

	// Frame selection
	void OnFrameClicked(int32 FrameIndex);

	// Playback
	void TogglePlayback();
	void StopPlaybackForBoundary(EPaper2DPlusFrameCueEndReason EndReason);
	void StopPlaybackTicker();
	bool OnPlaybackTick(float DeltaTime);
	bool OnPreviewResourceTick(float DeltaTime);
	void EnsurePreviewResourceTicker();
	void StopPreviewResourceTicker();
	int32 GetFrameFromTime() const;
	/** Preview the selected placement's real behavior through the same isolated host used by playback. */
	void PreviewSelectedCue();

	// Editor-preview cue dispatch, shared with runtime and backed by the host's isolated preview actor.
	// Drives both the playback ticker and frame-scrub paths; optional adapters run after behavior.
	//
	// bFromPlayback distinguishes a genuine PLAYBACK loop wrap (advancing past the last frame back to
	// 0 while playing — the only case that should fire wrap-crossing events) from a MANUAL backward
	// scrub (jumping 10 -> 3 by clicking/arrow), which is a fresh seek, NOT a loop. A manual backward
	// scrub force-ends active ranged events and reseeds Prev=target so only the target frame's events
	// evaluate, instead of firing every start-of-animation one-shot.
	void DispatchPreviewTransition(int32 NewFrame, bool bFromPlayback = false);
	// Force-End every active Cue State (mirrors the runtime teardown on flipbook change). Called on
	// stop, manual seek, flipbook/scope change, and tab close so no Begin is left without an End.
	void ForceEndAllActiveRangedEvents(
		EPaper2DPlusFrameCueEndReason EndReason =
			EPaper2DPlusFrameCueEndReason::EditorReset,
		bool bResetPreviewHost = true);
	void ExecuteForceEndAllActiveRangedEvents(
		EPaper2DPlusFrameCueEndReason EndReason,
		bool bResetPreviewHost,
		EPaper2DPlusFrameCueEvaluationMode EvaluationMode,
		int32 EndFrame);
	void FinishPreviewDispatchScope();

	// Helpers
	FFlipbookProfileEntry* GetSelectedFlipbookData() const;
	UPaperFlipbook* GetSelectedFlipbook() const;
	FPaper2DPlusFrameCueContext MakePreviewContext(
		int32 CurrentFrame,
		int32 PreviousFrame,
		bool bWasLoopWrap,
		EPaper2DPlusFrameCueEvaluationMode EvaluationMode) const;
	int32 GetFrameCount() const;
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetFrameCues() const;
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetFrameCuesArray() const;
	UPaper2DPlusCueBase* ResolveCueIdentity(
		const FFrameCueStableIdentity& CueIdentity,
		int32* OutCurrentIndex = nullptr) const;
	void ReconcileSelectedCueIdentity();
	bool CanMutateLiveSelection() const;

	friend class FPaper2DPlusFrameCueSelectionDrivenDetailsTest;
};
