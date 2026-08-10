// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION macros for the cross-version guards (D9) - explicit, not PCH-order-dependent
#include "Widgets/SCompoundWidget.h"
#include "Curves/CurveOwnerInterface.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusSettings.h"
#include "Editor/EditorEngine.h"

class SCurveEditor;
class SVerticalBox;
class SCurveTrackRow;
class FScopedTransaction;
class FActiveTimerHandle;
class UPaperFlipbook;

/**
 * Curve tracks under Frame Events (2026-06-11 brainstorm D1-D11): one taller shared engine
 * SCurveEditor renders every named auxiliary curve directly below the Frame Events timeline, with
 * compact per-curve identity rows underneath. The flat FFlipbookProfileEntry::CurveData stays
 * the single source of truth; the engine widget edits the embedded FRichCurve directly and the COERCE
 * FUNNEL (below) re-establishes the integer-frame + per-curve-Mode invariants after every engine edit.
 */

// =====================================================================================================
// Paper2DPlusCurveTracks — PURE coerce-funnel + orphan-detector + color hash (worldless-tested in
// Paper2DPlusCurveTracksTest.cpp; no Slate, no UObject construction)
// =====================================================================================================
namespace Paper2DPlusCurveTracks
{
	/** What the coerce funnel changed. bChanged also covers Mode re-stamps with zero key motion. */
	struct FCurveCoerceSummary
	{
		bool bChanged = false;
		/** Surviving keys whose time needed rounding to the nearest integer frame. */
		int32 NumRounded = 0;
		/** Keys DELETED because another key won their target frame (collision dedupe). */
		int32 NumDeduped = 0;
		/** Surviving FRACTIONAL-time keys whose rounded frame needed clamping into [0, FrameCount-1].
		 *  Exactly-integral out-of-range keys are NEVER clamped (legacy orphans — see the funnel doc). */
		int32 NumClamped = 0;
	};

	/**
	 * THE coerce funnel (brainstorm D4) — restores the data-model invariants after SCurveEditor pokes
	 * the FRichCurve directly: rounds every key time to the nearest integer frame, clamps into
	 * [0, FrameCount-1], dedupes frame collisions, and re-stamps Mode over every key (SetMode
	 * semantics — the engine's per-key RMB interp menu must not desync the per-curve Mode).
	 *
	 * Clamping applies ONLY to keys whose PRE-coerce time was FRACTIONAL — i.e. keys actively moved
	 * this gesture (fix F4/CT-5). Exactly-integral out-of-range keys are LEGACY ORPHANS (flipbook
	 * shrink/reimport) and are left untouched for the orphan detector / prune path — otherwise any
	 * unrelated edit on a curve carrying legacy orphans would cascade clamp+dedupe and silently
	 * destroy the authored last-frame key. Clamping is also skipped entirely when FrameCount <= 0
	 * (no valid band to clamp into — no flipbook resolved).
	 *
	 * Dedupe heuristic (documented per the locked design): when two keys land on one frame, KEEP the
	 * one whose PRE-coerce time was FURTHER from the integer — the engine gives no direct "which key
	 * moved" signal, but a mid-gesture/free-dragged key carries a fractional time while a stationary
	 * key sits exactly on its integer frame, so further-from-integer deterministically identifies the
	 * just-dragged key ("last-moved wins"). Tie-break (both exactly integral, e.g. a snapped drag onto
	 * an occupied frame): keep the EARLIER key in iteration order (FRichCurve inserts an equal-time
	 * key BEFORE existing ones, so the most recently moved key is the earlier entry).
	 *
	 * MID-GESTURE CONTRACT (fix F1/CT-1): this funnel deletes keys (dedupe), so it must NEVER run
	 * while an SCurveEditor drag is live — SCurveEditor::ProcessDrag → MoveSelectedKeys →
	 * OnCurveChanged fires on EVERY mouse-move, and a mid-drag funnel pass would destroy bystander
	 * keys as the dragged key passes over their frames. SCurveTrackStack::HandleCurveMutated owns
	 * the inline-vs-pend decision; callers must not invoke this directly under a live gesture.
	 */
	PAPER2DPLUSEDITOR_API FCurveCoerceSummary CoerceCurveKeysToFrames(
		FRichCurve& Curve, int32 FrameCount, EPaper2DPlusCurveInterp Mode);

	/** Pure orphan detector (bug E17 — orphans must be SURFACED, not hidden): rounded key frames that
	 *  fall outside [0, FrameCount-1] (>= FrameCount from flipbook shrink/reimport; negative from bad
	 *  data), sorted ascending. Drives contextual Details diagnostics/repair and the probe.
	 *  FrameCount <= 0 means "orphan state UNKNOWN" (no flipbook resolved), NOT "everything is an
	 *  orphan" — returns empty; Details shows a distinct frames-unavailable state instead
	 *  (fix F3/CT-4). */
	PAPER2DPLUSEDITOR_API TArray<int32> FindOrphanKeyFrames(const FRichCurve& Curve, int32 FrameCount);

	/** Deterministic, stable per-name color (case-insensitive). THE single implementation — row
	 *  swatches and SCurveEditor curve tints both read it. (The retired Curves tab's
	 *  Paper2DPlusCurveGraph::NameToColor used to forward here; it died with the tab, curves PR C.) */
	PAPER2DPLUSEDITOR_API FLinearColor NameToColor(FName CurveName);

	/**
	 * Transient, scope-local graph presentation. This never serializes into a Character Profile:
	 * visibility, solo, and primary selection are editor-view state only.
	 *
	 * BaseVisibleCurves deliberately stays independent from SoloCurve. Leaving solo therefore
	 * restores the exact pre-solo visibility set instead of making every authored curve visible.
	 * Reconcile treats newly authored curves as visible and removes stale identities from every
	 * channel. Rename preserves all three channels atomically.
	 */
	struct PAPER2DPLUSEDITOR_API FCurvePresentationState
	{
		void Reconcile(const TArray<FName>& AuthoredCurveNames);
		void ToggleVisibility(FName CurveName);
		void ToggleSolo(FName CurveName);
		void Select(FName CurveName);
		void Rename(FName OldName, FName NewName);
		void Add(FName CurveName);
		void Remove(FName CurveName);
		void ExitSoloForRetarget();

		bool IsKnown(FName CurveName) const { return KnownCurves.Contains(CurveName); }
		bool IsBaseVisible(FName CurveName) const { return BaseVisibleCurves.Contains(CurveName); }
		bool IsVisible(FName CurveName) const;
		bool IsSoloed(FName CurveName) const { return SoloCurve.IsSet() && SoloCurve.GetValue() == CurveName; }
		FName GetSelectedCurve() const { return SelectedCurve; }
		TArray<FName> GetVisibleCurves(const TArray<FName>& AuthoredCurveNames) const;

	private:
		TSet<FName> KnownCurves;
		TSet<FName> BaseVisibleCurves;
		TOptional<FName> SoloCurve;
		FName SelectedCurve;
	};
}

// =====================================================================================================
// FPaper2DPlusCurvePickerUtils — the ONE add-curve picker implementation (U2): exactly one
// naming/seeding flow. SCurveTrackStack is its sole consumer since the Curves tab retired (PR C).
// =====================================================================================================
struct PAPER2DPLUSEDITOR_API FPaper2DPlusCurvePickerUtils
{
	/** Seed a new curve for CurveName from semantic metadata. No keys either way (a keyless StepWindow
	 *  curve keeps the fail-closed validation warning until the designer authors the window). */
	static FPaper2DPlusFrameCurve MakeSeededCurve(FName CurveName);

	/** Resolve semantic editor metadata for CurveName. KnownCurves win; Cancel_* keeps a safe fallback. */
	static FPaper2DPlusKnownCurve ResolveCurveMetadata(FName CurveName);

	/** Shared SCurveEditor has one output snap setting; enable it only when every curve agrees. */
	static bool ResolveSharedOutputSnap(const TArray<FName>& CurveNames, float& OutOutputSnap);

	/** The KnownCurves registry menu (UPaper2DPlusSettings::KnownCurves): one entry per known name with
	 *  its description tooltip, disabled when the move already authors it. GetExistingCurves may return
	 *  null (no move selected). */
	static TSharedRef<SWidget> BuildKnownCurvesMenu(
		TFunction<const FFlipbookCurveData*()> GetExistingCurves,
		TFunction<void(FName)> OnAddCurve);

	/** The full compact add-curve control row: [+ Add Curve (registry combo)] [free-form name box] [Add].
	 *  Free-form names stay first-class (the registry is a discoverability aid, not a whitelist). The
	 *  row owns its own text state internally; OnAddCurve receives the trimmed FName. */
	static TSharedRef<SWidget> BuildAddCurveControls(
		TFunction<const FFlipbookCurveData*()> GetExistingCurves,
		TFunction<void(FName)> OnAddCurve);
};

// =====================================================================================================
// FPaper2DPlusCurveTrackOwner — shared FCurveOwnerInterface adapter (U3)
// =====================================================================================================

/**
 * One shared adapter per visible stack, handing the engine SCurveEditor every authored bare FRichCurve
 * straight off the asset (no UCurveBase — the FCurveStructCustomization precedent). Resolves each curve
 * LIVE from (weak asset, SNAPSHOT flipbook index, curve name) on every engine query, so it never caches
 * a FRichCurve pointer that a map mutation could invalidate.
 *
 * SNAPSHOT SEMANTICS (fix F2/CT-3): the flipbook INDEX and the resolved UPaperFlipbook* are captured
 * BY VALUE at row-build time — rows are rebuilt per move and the stack's structural fingerprint
 * includes the index, so a mid-gesture selection change can never retarget the resolve onto the
 * WRONG move's same-named curve. The flipbook POINTER is cached weakly but its key-frame COUNT is
 * read live off it each call, so frame-count edits reflect without per-paint
 * TSoftObjectPtr::LoadSynchronous churn (perf residual CT-R3).
 *
 * LIFETIME CONTRACT (research risk 5): SCurveEditor stores this adapter as a RAW pointer. The adapter is
 * owned by SCurveTrackStack and is destroyed strictly AFTER the shared editor is detached
 * (TeardownRows: release live pointer capture -> SetCurveOwner(nullptr) -> drop legend rows -> destroy
 * the adapter). Never hand an adapter to a widget the stack does not own.
 *
 * TRANSACTIONS: SCurveEditor opens its OWN engine transactions (Mouse Drag / Add Key(s) / Delete
 * Key(s) / New Value Entered) and calls ModifyOwner() inside them — the ONE sanctioned exception to
 * the house "panel-owned BeginTransaction/EndTransaction" rule. The exception is gated where it can
 * cascade: the host panel's HasActiveTransaction() also reports true while a curve gesture is live
 * (SCurveTrackStack::IsCurveGestureLive), so the model's deferred external-modify broadcast cannot
 * trigger a rebuild under a live drag.
 */
class PAPER2DPLUSEDITOR_API FPaper2DPlusCurveTrackOwner : public FCurveOwnerInterface
{
public:
	/** Stack-provided seams. All captured `this` pointers are the owning stack, which strictly
	 *  outlives every adapter (see the lifetime contract above). */
	struct FCallbacks
	{
		/** Asset->Modify() routed through the stack so dirty/undo enrollment stays at one site. */
		TFunction<void()> ModifyOwner;
		/** Fired when the engine widget poked the curve (OnCurveChanged). The STACK owns the coerce
		 *  decision (fix F1): inline funnel for click gestures, PEND-until-gesture-end for drags —
		 *  the adapter never mutates anything itself here. */
		TFunction<void(FName /*CurveName*/)> OnCurveMutated;
	};

	FPaper2DPlusCurveTrackOwner(
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> InAsset,
		int32 InFlipbookIndex,
		TWeakObjectPtr<UPaperFlipbook> InFlipbookSnapshot,
		int32 InFallbackFrameCount,
		TArray<FName> InCurveNames,
		FCallbacks InCallbacks);

	const TArray<FName>& GetCurveNames() const { return CurveNames; }
	bool OwnsCurve(FName InCurveName) const { return CurveNames.Contains(InCurveName); }

	/** Live resolve off (weak asset, snapshot flipbook index, curve name); null when anything is
	 *  stale. Both the engine queries and the coerce funnel go through here — no cached FRichCurve
	 *  pointers. */
	FPaper2DPlusFrameCurve* ResolveFrameCurve(FName InCurveName) const;

	/** Key-frame COUNT read live off the snapshot flipbook pointer, with the row-build Profile frame
	 *  count as the non-loading fallback. The pointer is never resolved from paint. */
	int32 GetSnapshotFrameCount() const;

	// --- FCurveOwnerInterface ---
	// CROSS-VERSION GUARD (brainstorm D9, verified against the real installed 5.0-5.7 headers):
	// GetCurves(TAdderReserverRef<FRichCurveEditInfoConst>) const is a pure virtual that exists ONLY
	// in 5.7 (CurveOwnerInterface.h:23; absent 5.0-5.6); the classic const overload is pure virtual in
	// ALL versions but carries a (backdated) UE_DEPRECATED(5.6) tag in 5.7 only — so the deprecation
	// tag on our override is guarded too, mirroring how CurveBase.h:62-75 wraps its own override.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	UE_DEPRECATED(5.6, "Use the TAdderReserverRef overload")
#endif
	virtual TArray<FRichCurveEditInfoConst> GetCurves() const override;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	virtual void GetCurves(TAdderReserverRef<FRichCurveEditInfoConst> Curves) const override;
#endif
	virtual TArray<FRichCurveEditInfo> GetCurves() override;
	virtual void ModifyOwner() override;
	virtual TArray<const UObject*> GetOwners() const override;
	virtual void MakeTransactional() override;
	virtual void OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos) override;
	virtual bool IsValidCurve(FRichCurveEditInfo CurveInfo) override;
	virtual FLinearColor GetCurveColor(FRichCurveEditInfo CurveInfo) const override;

private:
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	/** Row-build SNAPSHOT, by value (F2) — never a live attribute read. */
	int32 FlipbookIndex = INDEX_NONE;
	/** Row-build snapshot of the resolved flipbook — count read live off it, never LoadSynchronous. */
	TWeakObjectPtr<UPaperFlipbook> FlipbookSnapshot;
	/** Cook-synchronized fallback used when the soft flipbook is unresolved; captured with the row. */
	int32 FallbackFrameCount = 0;
	TArray<FName> CurveNames;
	FCallbacks Callbacks;
};

// =====================================================================================================
// SCurveTrackRow — one Blueprint-Timeline-style track row (U4)
// =====================================================================================================

DECLARE_DELEGATE_OneParam(FOnCurveTrackSelectionChanged, FName /*Curve, NAME_None when cleared*/);

/**
 * One compact identity row per curve: color swatch and curve name. Selection and keyboard navigation
 * live in the stack's focus shell; every curve control and diagnostic lives in the host Details panel.
 * The actual curves are edited together in the stack's one shared SCurveEditor.
 */
class PAPER2DPLUSEDITOR_API SCurveTrackRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCurveTrackRow) {}
		SLATE_ARGUMENT(FName, CurveName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	FName GetCurveName() const { return CurveName; }

private:
	FName CurveName;
};

// =====================================================================================================
// SCurveTrackStack — per-animation shared graph + detachable fixed legend (U4/U3 integration)
// =====================================================================================================

/**
 * Hosts one shared SCurveEditor for the graph and one compact SCurveTrackRow legend per authored
 * curve in stable sorted name order. The unified timeline owns Add Curve and may detach the legend
 * into its fixed header column while leaving the graph in the shared horizontal scroller. Curves use
 * the same 52px timing geometry as Cues; zero-frame scopes show the shared non-timing placeholder.
 *
 * Asset is a BINDABLE attribute (the SFrameEventPreviewCanvas Asset_Lambda precedent) so a
 * Base-Profile swap in the Character Layer editor retargets the stack without reconstruction.
 *
 * REFRESH MODEL: value edits paint live (the engine widget reads the curve each paint) — only
 * structural changes (curve added/removed/flipbook switched/profile swapped) rebuild rows. Structural
 * rebuilds requested from broadcast contexts are DEFERRED one active-timer beat: the rows' SCurveEditors
 * are themselves FEditorUndoClients, and destroying them inside a PostUndo broadcast would mutate the
 * undo-client list mid-iteration. Active timers run before the frame's paint, so no stale paint lands.
 */
class PAPER2DPLUSEDITOR_API SCurveTrackStack : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SCurveTrackStack) {}
		/** Bindable: re-resolved each read so Base-Profile swaps retarget live. */
		SLATE_ATTRIBUTE(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ATTRIBUTE(int32, SelectedFlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
		/** False mounts the real Profile curves through SCurveEditor in read-only mode. Presentation
		 *  controls remain available; every asset write path still fail-closes independently. */
		SLATE_ATTRIBUTE(bool, CanEditCurves)
		/** Fired AFTER a structural curve mutation (add/remove/mode/prune) commits — and AFTER any
		 *  coerce-funnel pass that actually changed data (F7: sibling panels must converge on final
		 *  values, closing the old drag-end staleness bug D14) — INSIDE the stack's write-in-progress
		 *  self-echo window. The host forwards to Model->NotifyAssetDataChanged() so sibling panels
		 *  refresh while the host's own gated handler early-outs (Rule A). */
		SLATE_EVENT(FSimpleDelegate, OnCurveListChanged)
		/** F7 flush seam: fired by the gesture-end poll AFTER the pended coerce funnel has run and the
		 *  self-echo window has closed. The host checks its bNeedsRefresh (set by broadcasts that
		 *  arrived under the widened HasActiveTransaction gate mid-gesture — pend, never drop) and
		 *  runs its refresh when set. */
		SLATE_EVENT(FSimpleDelegate, OnGestureSettled)
		/** Primary curve selection relay for the unified Cue/Curve timeline. */
		SLATE_EVENT(FOnCurveTrackSelectionChanged, OnCurveSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCurveTrackStack();

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	/** F8 (ADV-5): GFrameCounter at the most recent self-originated curve write (engine-gesture edit,
	 *  deferred funnel pass, or stack structural mutation). The host's broadcast handlers treat a
	 *  broadcast landing within 1-2 frames of this stamp as CURVE-ONLY — the deferred-tick analogue
	 *  of the Rule A self-echo flag — and skip the full RefreshAll rebuild. 0 = never written. */
	uint64 GetLastSelfCurveWriteFrame() const { return LastSelfCurveWriteFrame; }

	/** Structural-only refresh: rebuilds rows when the authored curve SET changed (or bForceRebuild),
	 *  else just repaints. Safe to call from any context — actual row teardown defers to an active
	 *  timer when requested mid-broadcast. */
	void RefreshTracks(bool bForceRebuild = false);

	/** Cheap playhead repaint for the scrub/playback/strip-click fan-out sites. */
	void InvalidateScrub();

	/** Fresh add controls for the unified timeline toolbar; this stack remains the sole mutation owner. */
	TSharedRef<SWidget> BuildAddCurveMenu();

	/** Detach the live legend rows from the graph body so the unified timeline can mount them in its
	 *  fixed header column. Idempotent; the returned widget remains owned/refreshed by this stack. */
	TSharedRef<SWidget> DetachLegendContentForExternalLayout();

	/** Transient presentation commands. They never require CanEditCurves and never dirty the asset. */
	void ToggleCurveVisibility(FName CurveName);
	void ToggleCurveSolo(FName CurveName);
	void SelectCurve(FName CurveName);
	/** Mirror the unified timeline's selection without feeding the change back to it. */
	void SynchronizeSelection(FName CurveName);
	TArray<FName> GetAuthoredCurveNames() const { return GetSortedCurveNames(); }
	FName GetSelectedCurve() const;
	bool IsCurveBaseVisible(FName CurveName) const;
	bool IsCurveVisible(FName CurveName) const;
	bool IsCurveSoloed(FName CurveName) const;
	EPaper2DPlusCurveInterp GetCurveMode(FName CurveName) const;
	TArray<int32> GetCurveOrphanFrames(FName CurveName) const;
	bool HasCurveOrphans(FName CurveName) const;
	bool HasResolvedCurveTiming() const;
	bool CanMutateCurve(FName CurveName) const;

	/** Rename a curve in one transaction. The requested name is trimmed and empty, unchanged,
	 *  case-only, and case-insensitive collisions are rejected before row teardown or Modify(). */
	bool RenameCurve(FName OldName, const FString& RequestedName, FText* OutError = nullptr);
	void RemoveCurve(FName CurveName);
	void SetCurveMode(FName CurveName, EPaper2DPlusCurveInterp NewMode);
	void PruneOrphanKeys(FName CurveName);

	/** Settle capture, pending coercion, timers, and any panel transaction before the owning tool hides. */
	void HandleHostDeactivated();
	bool HasPendingCoerceForTests() const { return PendingCoerceCurves.Num() > 0; }
	bool HasActiveTimerForTests() const
	{
		return RowsRebuildTimerHandle.IsValid()
			|| GestureEndPollHandle.IsValid();
	}
	int32 GetRowCountForTests() const { return Rows.Num(); }
	TArray<FName> GetAuthoredCurveNamesForTests() const { return GetAuthoredCurveNames(); }
	TArray<FName> GetGraphCurveNamesForTests() const { return VisibleCurveNames; }
	bool IsCurveGraphInteractiveForTests() const { return bSharedCurveEditorAcceptsEdits; }
	bool HasCurveRowKeyboardTargetForTests(FName CurveName) const;
	bool MoveCurveSelectionForTests(int32 Direction) { return MoveCurveSelection(Direction, false); }
	float GetCurveBodyWidthForTests() const;
	void RebuildRowsImmediatelyForTests();
	void QueuePendingCoerceForTests(FName CurveName) { PendingCoerceCurves.Add(CurveName); }
	void FlushPendingCoerceForTests() { FlushPendingCoerce(); }
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> GetLastTransactionAssetForTests() const
	{
		return LastTransactionAsset;
	}

	/** True while the stack itself runs one of its panel-template transactions (add/remove/mode/prune). */
	bool HasActiveTransaction() const { return ActiveTransaction.IsValid(); }

	/** The precise gesture-liveness gate (research risk 3): TRUE while the coerce funnel / a stack
	 *  structural write is in flight (Rule A self-echo flag), or while an ENGINE transaction is active
	 *  AND one of this stack's SCurveEditors holds mouse capture (a live key drag). The host panel ORs
	 *  this into its HasActiveTransaction() so the model's deferred external-modify broadcast cannot
	 *  rebuild the tab under a live curve gesture. */
	bool IsCurveGestureLive() const;

	/** Paper2DPlus.CurveTracksProbe payload: one greppable line per curve — name, mode, key
	 *  (frame,value) pairs, orphan frames — plus a header line with asset name (the registry logs one
	 *  block per live stack, F10), move name, row count, and gesture-live state. */
	void LogProbe() const;

private:
	TAttribute<TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>> AssetAttr;
	TAttribute<int32> SelectedFlipbookIndex;
	TAttribute<int32> SelectedFrameIndex;
	TAttribute<bool> CanEditCurvesAttr;
	FSimpleDelegate OnCurveListChanged;
	FSimpleDelegate OnGestureSettled;
	FOnCurveTrackSelectionChanged OnCurveSelectionChanged;

	/** Rule A write-in-progress self-echo flag — spans every stack-side mutate+notify. */
	bool bCurveWriteInProgress = false;

	/** F8 stamp — see GetLastSelfCurveWriteFrame. */
	uint64 LastSelfCurveWriteFrame = 0;

	// --- F1 (BLOCKER CT-1): NO curve mutations mid-gesture ------------------------------------------
	/** Curves the engine edited while a gesture was LIVE, awaiting the deferred coerce funnel. The
	 *  funnel deletes keys (dedupe) — running it per mouse-move would destroy bystander keys as the
	 *  dragged key passes over their frames, so mid-gesture edits only PEND here. */
	TSet<FName> PendingCoerceCurves;
	/** One poll active-timer at a time (0.1s period, waits for !IsCurveGestureLive()). */
	bool bGestureEndPollActive = false;
	TWeakPtr<FActiveTimerHandle> GestureEndPollHandle;
	void ArmGestureEndPoll();
	EActiveTimerReturnType OnGestureEndPoll(double InCurrentTime, float InDeltaTime);
	/** Run the FULL funnel over every pending curve. Writes (if any curve actually changes) are
	 *  wrapped in ONE panel-style transaction ("Snap curve keys to frames") — a DELIBERATE second
	 *  undo entry after the engine's own drag entry (deletions must never happen mid-gesture; undo
	 *  order funnel-then-drag is coherent). Fires OnCurveListChanged inside the self-echo window. */
	void FlushPendingCoerce();
	/** Resolve through the named row's ADAPTER (snapshot index/flipbook — F2) and run the funnel. */
	Paper2DPlusCurveTracks::FCurveCoerceSummary RunCoerceFunnel(FName CurveName);

	// --- F6 (BLOCKER ADV-1): out-of-band writers --------------------------------------------------
	/** FCoreUObjectDelegates::OnObjectModified subscription: an out-of-band writer (second editor
	 *  over the same asset, Content-Browser JSON import, ECABridge set_asset_property) mutates curve
	 *  storage with only a NEXT-TICK deferred model broadcast — the engine widgets' cached
	 *  FRichCurve* view models would paint freed memory THIS tick. Modify() precedes the mutation in
	 *  all trigger paths, so reattaching here lands before paint. */
	FDelegateHandle ObjectModifiedHandle;
	void HandleObjectModified(UObject* Object);
	/** Guard: repeated broadcasts in one tick don't re-do the reattach work. */
	uint64 LastExternalReattachFrame = MAX_uint64;

	/** Panel transaction template (mirrors the tool-tab panels): BeginTransaction sets this AND calls
	 *  Asset->Modify(); never a bare Modify in a lambda. The engine widget's self-owned transactions
	 *  are the documented exception, gated via IsCurveGestureLive. */
	TUniquePtr<FScopedTransaction> ActiveTransaction;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> LastTransactionAsset;
	void BeginTransaction(const FText& Description, UPaper2DPlusCharacterProfileAsset* TransactionAsset = nullptr);
	void EndTransaction();

	/** Adapter storage (risk 5): destroyed only by TeardownRows AFTER the shared editor is detached. */
	TUniquePtr<FPaper2DPlusCurveTrackOwner> SharedAdapter;
	TSharedPtr<SCurveEditor> SharedCurveEditorWidget;
	TSharedPtr<SWidget> SharedPlayheadWidget;
	bool bSharedCurveEditorAcceptsEdits = false;
	float SharedViewMinOutput = 0.0f;
	float SharedViewMaxOutput = 1.0f;
	float SharedOutputSnap = 1.0f;
	bool bSharedOutputSnappingEnabled = false;

	TArray<TSharedPtr<SCurveTrackRow>> Rows;
	/** Structural fingerprint — sorted curve names + asset + flipbook index the current rows were
	 *  built for. Asset/index are part of the fingerprint because two moves (or two Base Profiles)
	 *  routinely author the SAME curve names (HitStop, Cancel_Normal …) — a names-only diff would
	 *  leave the engine widgets' cached FRichCurve* pointing at the PREVIOUS move's curves (the D13
	 *  mid-switch-writes-the-wrong-move bug class). */
	TArray<FName> RowCurveNames;
	/** Authored names filtered by the current scope's visibility/solo presentation. */
	TArray<FName> VisibleCurveNames;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> RowAsset;
	int32 RowFlipbookIndex = INDEX_NONE;
	bool bRowCanEditCurves = true;
	TSharedPtr<SVerticalBox> StackBox;
	TSharedPtr<SVerticalBox> RowsBox;
	bool bLegendDetached = false;
	/** Focus targets stay separate from presentation selection: keyboard Up/Down moves both, while
	 *  programmatic selection can change without stealing focus from another editor control. */
	TMap<FName, TWeakPtr<SWidget>> CurveRowFocusTargets;

	/** Per stable Profile/animation identity; changing scope exits the old scope's solo before the
	 *  new scope is resolved, so solo can never leak or unexpectedly restore across retargets. */
	TMap<FString, Paper2DPlusCurveTracks::FCurvePresentationState> PresentationByScope;
	FString CurrentPresentationScope;

	/** Deferred-rebuild plumbing (see the class comment's REFRESH MODEL note). */
	bool bRowsRebuildPending = false;
	TWeakPtr<FActiveTimerHandle> RowsRebuildTimerHandle;
	void RequestRowsRebuild();
	EActiveTimerReturnType OnRowsRebuildTimer(double InCurrentTime, float InDeltaTime);
	bool SetCurveSelectionState(FName CurveName);

	void TeardownRows();
	void BuildRows();
	void RebuildRowsNow();
	bool MoveCurveSelection(int32 Direction, bool bSetKeyboardFocus);
	bool ValidateCurveRename(
		FName OldName, const FString& RequestedName, FName& OutNewName, FText& OutError) const;

	// Structural mutations (all transaction-template-wrapped, all inside the Rule A write window)
	void AddCurve(FName CurveName);

	// Adapter callback — THE inline-vs-pend coerce decision point (F1): gesture live -> pend +
	// targeted invalidate + arm the poll; not live (click gestures — Shift+Click add, RMB delete,
	// numeric commit: the engine opens+closes its transaction within the input event) -> run the
	// funnel inline, riding the engine's still-open transaction, self-echo flag around it.
	void HandleCurveMutated(FName CurveName);

	// Helpers (live reads — never cached). The funnel itself resolves through the ADAPTER's
	// snapshots (F2), not these.
	UPaper2DPlusCharacterProfileAsset* ResolveAsset() const;
	FFlipbookProfileEntry* GetSelectedFlipbookData() const;
	int32 GetFrameCount() const;
	float GetCurveBodyWidth() const;
	bool HasResolvedAnimationForTiming() const;
	TArray<FName> GetSortedCurveNames() const;
	FString MakePresentationScopeKey() const;
	Paper2DPlusCurveTracks::FCurvePresentationState* ReconcileCurrentPresentation(
		const TArray<FName>& AuthoredCurveNames);
	const Paper2DPlusCurveTracks::FCurvePresentationState* FindCurrentPresentation() const;
	bool IsRowTargetCurrent() const;
	FPaper2DPlusCurveTrackOwner* FindAdapter(FName CurveName) const;
	bool SharedCurveEditorHasMouseCapture() const;
	void DetachSharedCurveOwner();
	void ReattachSharedCurveOwner();
	void InvalidateSharedCurveEditor();
	void InvalidateSharedPlayhead();
	void HandleSetSharedOutputViewRange(float NewMin, float NewMax);
	void UpdateSharedOutputViewRange();
	void InvalidateRowFor(FName CurveName);
};
