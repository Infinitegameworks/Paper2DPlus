// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfileToolPanelProvider.h"
#include "Widgets/SCompoundWidget.h"
#include "ScopedTransaction.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;
class FCharacterProfileEditorModel;
class SVerticalBox;
class SScrollBox;
class SBox;
class SButton;
struct FProfileSpriteBoundsReport;
struct FGameplayTag;
struct FGameplayTagContainer;

/** Why one occupied direction is affected by a proposed topology change. */
enum class EProfileDirectionalTopologyImpactDisposition : uint8
{
	/** The slot remains active, but its physical center bearing changes. */
	Reinterpreted,
	/** The slot would no longer be active under the proposed direction count. */
	Stranded,
};

/**
 * One deterministic, stable-owner row in a directional topology preflight.
 *
 * AnimationIndexAtPreflight is diagnostic only. Delayed actions must re-resolve AnimationIdentity
 * against the live Profile instead of writing through this observed index.
 */
struct PAPER2DPLUSEDITOR_API FProfileDirectionalTopologyImpactItem
{
	FProfileAnimationIdentity AnimationIdentity;
	int32 AnimationIndexAtPreflight = INDEX_NONE;
	FString AnimationName;
	int32 SlotIndex = INDEX_NONE;
	/** False when the author is repairing invalid current count/offset data. */
	bool bOldBearingValid = true;
	double OldBearingDegrees = 0.0;
	double NewBearingDegrees = 0.0;
	EProfileDirectionalTopologyImpactDisposition Disposition =
		EProfileDirectionalTopologyImpactDisposition::Reinterpreted;

	bool IsStranded() const
	{
		return Disposition == EProfileDirectionalTopologyImpactDisposition::Stranded;
	}

	/** Locale-independent summary used by prompts, issue projection, and behavioral tests. */
	FString ToDeterministicString() const;
};

/** Pure result of a proposed Profile-default or per-animation topology change. */
struct PAPER2DPLUSEDITOR_API FProfileDirectionalTopologyImpact
{
	/** True for a Profile-default proposal; false for one animation's inheritance/override proposal. */
	bool bProfileDefaultsChange = false;
	int32 CurrentDirectionCount = 0;
	float CurrentAngleOffsetDegrees = 0.0f;
	int32 ProposedDirectionCount = 0;
	float ProposedAngleOffsetDegrees = 0.0f;
	/** False for invalid settings, an expired owner, or structurally invalid source data. */
	bool bRequestValid = false;
	/** Stable blocking diagnostic. Empty for a valid, non-stranding request. */
	FString IssueText;
	/** Authored animation order, then stable slot-index order. */
	TArray<FProfileDirectionalTopologyImpactItem> Items;

	bool HasStrandedAssignments() const;
	bool HasReinterpretedAssignments() const;
	int32 CountStrandedAssignments() const;
	int32 CountReinterpretedAssignments() const;
};

/** Which slice of the shared authoring controller/view the panel shows. The Animations workspace uses
 *  focused Details, Transitions, and Tags instances; legacy embedded callers retain FlipbookFocus. The
 *  secondary Character Data editor uses two focused profile-wide slices. */
enum class EProfileDetailsPaneMode : uint8
{
	/** Compatibility surface for legacy embedded users: Details + Transitions + Tags. */
	FlipbookFocus,
	/** Focused contextual panel for selected-animation metadata. */
	FlipbookDetails,
	/** Focused contextual panel for outgoing rows or the selected Map edge. */
	Transitions,
	/** Focused contextual panel for animation-category and phase tags. */
	Tags,
	/** Profile-wide Sprite Bounds health and relative-transform authoring. */
	Character,
	/** PaperZD source, sequence creation, matching, and health authoring. */
	PaperZDSequences,
};

/**
 * Profile Details panel — focused animation metadata, transitions, tags, and profile properties.
 * Extracted from the Overview tab's right-hand sidebar to be an independent SCompoundWidget
 * that communicates through the shared FCharacterProfileEditorModel. The PaneMode arg selects which
 * sections it builds (see EProfileDetailsPaneMode).
 */
class SProfileDetailsPanel : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SProfileDetailsPanel)
		: _PaneMode(EProfileDetailsPaneMode::FlipbookFocus)
		{}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		/** Focused Animations or Character Data workspace slice, or legacy FlipbookFocus. */
		SLATE_ARGUMENT(EProfileDetailsPaneMode, PaneMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SProfileDetailsPanel();

	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Refresh the selected move's outgoing transitions list (TASK-76). */
	void RefreshTransitionsList();

	/** Rebuild the relocated Phase-tag picker for the current selection (TASK-96 P3). Uses a STATIC
	 *  SGameplayTagCombo Tag + rebuild — a bound .Tag_Lambda hangs the always-visible pane (the combo's
	 *  Tag attribute invalidates Layout, so a per-frame getter never lets layout settle). */
	void RefreshPhaseTagPicker();

	/** Rebuild the selected animation's own/effective tag chips and safe menu-hosted picker. */
	void RefreshAnimationTagsPanel();

	/** Stable-identity mutation funnels shared by the contextual Tags panel and its tests. */
	bool CommitAnimationTags(
		const FProfileAnimationIdentity& AnimationIdentity,
		const FGameplayTagContainer& NewTags);
	bool CommitPhaseTag(
		const FProfileAnimationIdentity& AnimationIdentity,
		const FGameplayTag& NewTag);

	// --- Directional Animation authoring (TASK-190) -------------------------------------------
	// Animation-local actions capture FProfileAnimationIdentity and re-resolve it against the live
	// Profile. Profile-default actions capture the exact Profile plus model generation, so they work
	// without a selected animation and cannot retarget after editor-model reuse. These seams are public
	// so headless tests exercise the same stable-owner transaction boundary as the Slate controls.

	FProfileAnimationIdentity GetSelectedDirectionalAnimationIdentity() const;
	bool EnableDirectionalSet(const FProfileAnimationIdentity& AnimationIdentity);
	bool RemoveDirectionalSet(const FProfileAnimationIdentity& AnimationIdentity);
	bool CommitDirectionalSlot(
		const FProfileAnimationIdentity& AnimationIdentity,
		int32 SlotIndex,
		const TSoftObjectPtr<UPaperFlipbook>& Flipbook);
	/**
	 * Pure name-suffix matcher behind Auto-fill from names: maps candidate asset names onto
	 * direction slots when a candidate is exactly Base + separator (_ - or space) + a compass
	 * label (per SDirectionalAnimationWheel::GetSlotDirectionLabel) or a bare slot index. A slot
	 * with more than one distinct candidate is skipped rather than guessed. Returns
	 * SlotIndex -> index into CandidateAssetNames.
	 */
	static TMap<int32, int32> MatchDirectionalSlotsByNameSuffix(
		const FString& BaseAssetName,
		const TArray<FString>& CandidateAssetNames,
		int32 DirectionCount,
		float AngleOffsetDegrees);
	/** Fill every EMPTY active slot from same-folder name-suffix matches in one transaction. */
	bool RunDirectionalNameSuffixAutoFill();
	/** Injectable-confirm twin: automation cannot answer the modal (unattended dialogs return No). */
	bool RunDirectionalNameSuffixAutoFillForTests(bool bConfirmProposals);
	bool ClearDirectionalSlot(
		const FProfileAnimationIdentity& AnimationIdentity,
		int32 SlotIndex);
	/** Toggle an occupied slot's mirrored presentation. Same-value writes are refused as no-ops. */
	bool CommitDirectionalSlotMirror(
		const FProfileAnimationIdentity& AnimationIdentity,
		int32 SlotIndex,
		bool bMirrorHorizontally);

	/** Pure, load-free preflight builders. They never mutate, transact, dirty, notify, or show UI. */
	static FProfileDirectionalTopologyImpact BuildDirectionalDefaultsImpactForTests(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		int32 DirectionCount,
		float AngleOffsetDegrees);
	static FProfileDirectionalTopologyImpact BuildDirectionalOverrideImpactForTests(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const FProfileAnimationIdentity& AnimationIdentity,
		bool bOverrideProfileSettings,
		int32 DirectionCount,
		float AngleOffsetDegrees);

	/** Test-decision seams avoid modal UI while preserving the production preflight/transaction path. */
	bool CommitDirectionalDefaultsForTests(
		int32 DirectionCount,
		float AngleOffsetDegrees,
		bool bConfirmReinterpretation);
	bool CommitDirectionalOverrideForTests(
		const FProfileAnimationIdentity& AnimationIdentity,
		bool bOverrideProfileSettings,
		int32 DirectionCount,
		float AngleOffsetDegrees,
		bool bConfirmReinterpretation);
	/** Behavioral seams for confirmation-boundary tests. The callback may simulate reorder, undo,
	 *  reimport, or model reinitialization while the production confirmation dialog is open. */
	bool CommitDirectionalDefaultsWithConfirmationForTests(
		int32 DirectionCount,
		float AngleOffsetDegrees,
		TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation);
	bool CommitDirectionalOverrideWithConfirmationForTests(
		const FProfileAnimationIdentity& AnimationIdentity,
		bool bOverrideProfileSettings,
		int32 DirectionCount,
		float AngleOffsetDegrees,
		TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation);

	/** Activate the actual rendered impact-row button so tests cover presence, wiring, and navigation. */
	bool ActivateDirectionalImpactRowForTests(int32 ImpactIndex);
	/** Exercise the same Profile-owner capture/consume path used by the Profile Count spin box. */
	bool BeginProfileDirectionalCountEditForTests();
	bool CommitProfileDirectionalCountEditForTests(
		int32 DirectionCount,
		bool bConfirmReinterpretation);
	bool BeginLocalDirectionalCountEditForTests();
	bool CommitLocalDirectionalCountEditForTests(
		int32 DirectionCount,
		bool bConfirmReinterpretation);
	/** Exercise the rendered Local Settings checkbox decision without opening a modal prompt. */
	bool CommitSelectedDirectionalOverrideEnabledForTests(
		bool bOverrideProfileSettings,
		bool bConfirmReinterpretation);
	/** The rendered Profile-default surface exists independently of animation selection. */
	bool IsDirectionalProfileDefaultsSurfaceVisibleForTests() const;

	const FProfileDirectionalTopologyImpact& GetLastDirectionalImpactForTests() const
	{
		return LastDirectionalImpact;
	}
	const FString& GetLastDirectionalIssueTextForTests() const
	{
		return LastDirectionalIssueText;
	}

	/** Resolve an animation to exactly one TagMappings entry and require that entry to be a Chain
	 *  Start. Duplicate membership in one or more groups fails closed. The returned index is only a
	 *  same-call observation; delayed writes must re-resolve from AnimationIdentity + GroupTag. */
	bool ResolveAnimationChainStart(
		const FProfileAnimationIdentity& AnimationIdentity,
		FGameplayTag& OutGroupTag,
		int32& OutEntryIndex) const;

	/** Replace the uniquely mapped opener's chain-identity container. The group captured when the
	 *  picker opened is checked after stable animation re-resolution at commit time, so regroup,
	 *  unmap, opener removal, or ambiguous membership cancels the delayed write. Exact-set-equal
	 *  writes are a no-op; a change is one transaction and one model notification. */
	bool CommitAnimationChainTags(
		const FProfileAnimationIdentity& AnimationIdentity,
		const FGameplayTag& ExpectedGroupTag,
		const FGameplayTagContainer& NewChainTags);

	// --- Edge mode (TASK-108 U3) — the model's selected-transition key drives an edge-centric Details
	// view. The pane holds NO edge state of its own: it re-resolves the model's VALUE key against the
	// asset on every render/refresh and falls back to flipbook mode when the row vanished (external
	// edit/undo). Public: these are also the headless test seams for the edge write paths.

	/** Resolve the model's selected transition to live indices: the From entry (first case-insensitive
	 *  name match), the row (FindTransitionRowIndex), and the TARGET entry (INDEX_NONE = stub/dangling
	 *  target — the edge still resolves). False = no key, or the From/row vanished. */
	bool ResolveSelectedTransition(int32& OutFromIndex, int32& OutRowIndex, int32& OutTargetIndex) const;

	/** Edge-mode phase commit through the standard transaction funnel. Same-value early-out; stub
	 *  targets are supported. One undo step. */
	bool CommitEdgePhaseTag(const FGameplayTag& InPhaseTag);

	/** Delete the selected transition's row (snapshot-validated, standard funnel; no confirm — single
	 *  row) and clear the model's edge key. Works on stub-target edges too. */
	bool DeleteSelectedTransition();

	/** The Move Transitions row target commit (extracted seam): '(none)' -> empty target. REFUSES (with
	 *  a toast) a non-empty target that duplicates another row's (From, To) pair, case-insensitive —
	 *  the one-row-per-pair invariant holds through the pane path (TASK-108 KTD). True = data changed. */
	bool CommitTransitionTargetChange(int32 FlipbookIdx, int32 TransIdx, const FString& NewTarget);

	/** Rebuild the edge-mode section's static-Tag phase picker for the current edge key (the
	 *  RefreshPhaseTagPicker idiom — never a bound .Tag_Lambda). */
	void RefreshEdgeModeSection();

	/** Refresh the PaperZD sequences list. */
	void RefreshPaperZDSequencesList();

	/** Refresh all content in the panel. */
	void RefreshAll();

	/** Focused-pane diagnostics / acceptance-test seams. */
	EProfileDetailsPaneMode GetPaneModeForTests() const { return PaneMode; }
	int32 GetSelectionRefreshCountForTests() const { return SelectionRefreshCount; }
	int32 GetTransitionSelectionRefreshCountForTests() const { return TransitionSelectionRefreshCount; }
	int32 GetAnimationTagsRefreshCountForTests() const { return AnimationTagsRefreshCount; }
	bool IsShowingResolvedEdgeForTests() const;
	bool HasAnimationTagsAuthoringSurfaceForTests() const
	{
		return AnimationTagsEditorBox.IsValid() && PhaseTagPickerBox.IsValid();
	}
	bool HasChainTagsAuthoringSurfaceForTests() const
	{
		return bHasAnimationChainTagsAuthoringSurface && ChainTagsEditorBox.IsValid();
	}
	bool CommitSelectedChainTagsForTests(const FGameplayTagContainer& NewChainTags);
	bool HasPaperZDSurfaceForTests() const { return PaperZDSequencesListBox.IsValid(); }
	bool HasSpriteBoundsSurfaceForTests() const { return SpriteBoundsResultsBox.IsValid(); }
	bool HasRelativeTransformSurfaceForTests() const { return bHasRelativeTransformEditorSurface; }
	bool HasPaperZDSequenceCreationActionForTests() const
	{
		return bHasPaperZDSequenceCreationAction;
	}
	FString BuildDefaultPaperZDSequenceNameForTests(
		const FString& FlipbookName,
		bool bIncludeProfilePrefix) const;
	TArray<FString> GetPendingPaperZDSequenceNamesForTests(
		bool bIncludeProfilePrefix) const;
	bool AssignPaperZDSequenceForTests(UPaperFlipbook* Flipbook, UObject* Sequence);
	bool CommitRelativeLocationForTests(const FVector& NewValue) { return CommitRelativeLocation(NewValue); }
	bool CommitRelativeRotationForTests(const FRotator& NewValue) { return CommitRelativeRotation(NewValue); }
	bool CommitRelativeScaleForTests(const FVector& NewValue) { return CommitRelativeScale(NewValue); }
	/** Null-safe optional-plugin resolver used by the PaperZD Source picker. A fully qualified path
	 *  avoids UE 5.8's short-type-name warning and remains null when PaperZD is unavailable. */
	static UClass* ResolveOptionalPaperZDAnimationSourceClass();
	/** Null-safe reflected class used by sequence creation; never exposes a PaperZD C++ type. */
	static UClass* ResolveOptionalPaperZDFlipbookSequenceClass();

	// --- Delegates for actions that need parent coordination ---

	DECLARE_DELEGATE(FOnRefreshOverviewFlipbookList);
	FOnRefreshOverviewFlipbookList OnRefreshOverviewFlipbookList;

	DECLARE_DELEGATE(FOnRefreshFlipbookGroupsPanel);
	FOnRefreshFlipbookGroupsPanel OnRefreshFlipbookGroupsPanel;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	TSharedPtr<FCharacterProfileEditorModel> Model;
	EProfileDetailsPaneMode PaneMode = EProfileDetailsPaneMode::FlipbookFocus;

	// Delegate handles for model subscriptions
	FDelegateHandle ModelFlipbookSelectionHandle;
	FDelegateHandle ModelAssetDataChangedHandle;
	FDelegateHandle ModelAssetExternallyModifiedHandle;
	FDelegateHandle ModelTransitionSelectionHandle;

	// Widget references
	TSharedPtr<SVerticalBox> TransitionsListBox;
	TArray<TSharedPtr<FString>> TransitionTargetNameOptions;
	TSharedPtr<SBox> AnimationTagsEditorBox;
	TSharedPtr<SBox> PhaseTagPickerBox;
	TSharedPtr<SBox> ChainTagsSectionBox;
	TSharedPtr<SBox> ChainTagsEditorBox;
	TSharedPtr<STextBlock> ComboPositionText;
	TSharedPtr<SBox> EdgePhaseTagPickerBox;
	TSharedPtr<SVerticalBox> PaperZDSequencesListBox;
	TSharedPtr<SVerticalBox> SpriteBoundsResultsBox;
	TSharedPtr<SVerticalBox> DirectionalImpactRowsBox;
	TSharedPtr<SBox> DirectionalProfileDefaultsSurface;
	TArray<TWeakPtr<SButton>> DirectionalImpactRowButtons;
	TSharedPtr<FProfileSpriteBoundsReport> SpriteBoundsReport;
	bool bHasRelativeTransformEditorSurface = false;
	bool bHasPaperZDSequenceCreationAction = false;
	bool bHasAnimationChainTagsAuthoringSurface = false;
	enum class EDirectionalNumericEdit : uint8
	{
		None,
		ProfileCount,
		ProfileOffset,
		LocalCount,
		LocalOffset,
	};
	EDirectionalNumericEdit ActiveDirectionalNumericEdit = EDirectionalNumericEdit::None;
	bool bDirectionalNumericSliderMovement = false;
	double DirectionalNumericDraftValue = 0.0;
	/** Selected-animation identity is used only by Local Count/Offset edits. */
	FProfileAnimationIdentity DirectionalNumericOwner;
	/** Profile Count/Offset edits are owned by this exact live Profile + panel model generation. */
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> DirectionalNumericProfileOwner;
	uint64 DirectionalNumericModelGeneration = 0;
	uint64 DirectionalModelGeneration = 0;
	FProfileDirectionalTopologyImpact LastDirectionalImpact;
	FString LastDirectionalIssueText;

	// Transaction support
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	void BeginTransaction(const FText& Description);
	void EndTransaction();
	bool CommitRelativeLocation(const FVector& NewValue);
	bool CommitRelativeRotation(const FRotator& NewValue);
	bool CommitRelativeScale(const FVector& NewValue);

	/** True while a transition mutation + its own Model->NotifyAssetDataChanged() broadcast are in
	 *  flight. The broadcast runs handlers INLINE (the house pattern broadcasts AFTER EndTransaction,
	 *  when no mutation scope suppresses it), so without this flag the panel's own OnAssetDataChanged
	 *  handler would re-enter RefreshTransitionsList and ClearChildren the committing widget from
	 *  inside its own callback — the documented label-commit crash class. Sites that need a rebuild
	 *  do it themselves exactly once after the broadcast. */
	bool bPanelWriteInProgress = false;
	int32 SelectionRefreshCount = 0;
	int32 TransitionSelectionRefreshCount = 0;
	int32 AnimationTagsRefreshCount = 0;
	void HandleFlipbookSelectionChanged(int32 NewIndex);
	void HandleAssetDataChanged();
	void HandleAssetExternallyModified();
	void HandleTransitionSelectionChanged();
	bool IsCharacterDataPane() const;
	void RefreshCharacterDataPane();

	// FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// UI Builders
	TSharedRef<SWidget> BuildDetailsPanel();
	TSharedRef<SWidget> BuildFlipbookDetailsView();
	TSharedRef<SWidget> BuildDirectionalAnimationSection();
	TSharedRef<SWidget> BuildEdgeModeView();
	TSharedRef<SWidget> BuildTransitionsContextPanel();
	TSharedRef<SWidget> BuildTagsPanel();

	/** Drop the model's edge key when it no longer resolves (external row delete/undo) — called from
	 *  the data-change/undo refresh paths, never from render lambdas. Keeps "no dangling state". */
	void ValidateTransitionSelection();
	TSharedRef<SWidget> BuildPaperZDAnimSourcePanel();
	TSharedRef<SWidget> BuildSpriteBoundsPanel();
	void RefreshAnimationChainTagsEditor();
	/** Opener-only contextual editor. Menu-open captures the live unique group identity; commit
	 *  re-resolves it so delayed picker callbacks cannot mutate a moved or ambiguous entry. */
	TSharedRef<SWidget> BuildAnimationChainTagsEditor(
		const FProfileAnimationIdentity& AnimationIdentity);
	TSharedRef<SWidget> BuildTransitionsPanel();
	TSharedRef<SWidget> BuildRelativeTransformSection();
	void RunProfileSpriteBoundsCheck();
	void RepairProfileSpriteBounds();
	void InvalidateProfileSpriteBoundsReport();
	void RefreshProfileSpriteBoundsResults();
	FText GetProfileSpriteBoundsSummary() const;

	// Optional PaperZD sequence creation shared with the PaperZD Sequences pane.
	void AutoCreateTagMappingSequences();

	// Helpers
	TArray<int32> GetSortedFlipbookIndices() const;
	UPaper2DPlusCharacterProfileAsset* GetLiveDirectionalProfile() const;
	void CancelDirectionalNumericEdit();
	bool StartDirectionalNumericEdit(
		EDirectionalNumericEdit EditKind,
		double InitialValue,
		bool bSliderMovement);
	bool ConsumeDirectionalNumericEdit(
		EDirectionalNumericEdit EditKind,
		FProfileAnimationIdentity& OutOwnerIdentity);
	bool ConsumeProfileDirectionalNumericEdit(
		EDirectionalNumericEdit EditKind,
		UPaper2DPlusCharacterProfileAsset*& OutProfileOwner,
		uint64& OutModelGeneration);
	void ValidateDirectionalNumericEditOwner();
	void ReconcileDirectionalTransientState();
	void ResetDirectionalIssue();
	void PublishDirectionalIssue(const FProfileDirectionalTopologyImpact& Impact);
	void RefreshDirectionalImpactRows();
	bool NavigateDirectionalImpactItem(int32 ImpactIndex);
	EActiveTimerReturnType HandleDeferredStrandedSlotClear(
		double InCurrentTime,
		float InDeltaTime,
		FProfileAnimationIdentity AnimationIdentity,
		int32 SlotIndex);
	/** One auto-fill body behind both the interactive prompt and the injectable test confirm. */
	bool RunDirectionalNameSuffixAutoFillInternal(
		TFunctionRef<bool(
			const FString& BaseAssetName,
			int32 DirectionCount,
			float AngleOffsetDegrees,
			const TArray<TPair<int32, FSoftObjectPath>>& Proposals)> ConfirmProposals);
	bool CommitCapturedProfileDirectionalCount(
		int32 DirectionCount,
		TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation);
	bool CommitCapturedLocalDirectionalCount(
		int32 DirectionCount,
		TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation);
	bool CommitCapturedLocalDirectionalOffset(
		float AngleOffsetDegrees,
		TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation);
	bool CommitSelectedDirectionalOverrideEnabled(
		bool bOverrideProfileSettings,
		TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation);
	bool ConfirmDirectionalReinterpretation(
		const FProfileDirectionalTopologyImpact& Impact,
		const FText& Title,
		const FText& Body) const;
	bool ExecuteDirectionalMutation(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const FText& TransactionDescription,
		TFunctionRef<bool()> Mutate);
	bool CommitDirectionalDefaultsInternal(
		UPaper2DPlusCharacterProfileAsset* ProfileOwner,
		uint64 ModelGeneration,
		int32 DirectionCount,
		float AngleOffsetDegrees,
		TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation);
	bool CommitDirectionalOverrideInternal(
		const FProfileAnimationIdentity& AnimationIdentity,
		bool bOverrideProfileSettings,
		int32 DirectionCount,
		float AngleOffsetDegrees,
		TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation);
};
