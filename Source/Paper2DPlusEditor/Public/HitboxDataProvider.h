// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "ProfileToolPanelProvider.h"

class FCharacterProfileEditorModel;
class UPaper2DPlusCharacterProfileAsset;

/**
 * Data-provider seam for SHitboxEditorPanel / SCharacterProfileEditorCanvas.
 *
 * The hitbox editor authors per-frame combat data (FFrameHitboxData rows, key-frame-parallel with the
 * profile flipbook at FlipbookIndex). WHERE those rows live depends on the hosting editor:
 *   - Character Profile editor: unmanaged Profiles write Flipbooks[i].CombatData.Frames; Profiles attached
 *     to a canonical Layer bake set write CharacterBaseline while compiled runtime rows remain read-only.
 *   - Character Layer editor: the model-selected stable LayerId's AuthoredAnimations[].Frames on the Layer
 *     Asset. The provider adapts its separate Attack/Hurt/socket channels to the shared editor frame view.
 *
 * Contract notes:
 *   - GetFrameMutable is FIND-ONLY. Display lambdas (spinbox Value_Lambda etc.) resolve through it on
 *     every paint, so it must never create data. Create-on-first-edit lives in EnsureFrameMutable,
 *     which callers invoke ONLY inside an open transaction at genuine first-write sites (canvas
 *     draw-create, socket create, Add Hitbox/Socket buttons, copy-op targets).
 *   - Flipbook identity, sprites, and frame COUNTS stay profile truths in both contexts — the provider
 *     only redirects the combat-data rows.
 */
class PAPER2DPLUSEDITOR_API FHitboxFrameDataProvider
{
public:
	virtual ~FHitboxFrameDataProvider() = default;

	// --- Stable animation identity ---
	/** Capture the selected animation before an extensible action/gesture; never retain Flipbooks[] indices. */
	virtual FProfileAnimationIdentity GetAnimationIdentity(int32 FlipbookIndex) const = 0;
	/** Resolve against the LIVE profile after reorder/undo/reimport. INDEX_NONE on deletion. */
	virtual int32 ResolveAnimationIndex(const FProfileAnimationIdentity& Identity) const = 0;
	virtual FProfileLayerScopeIdentity GetLayerScopeIdentity() const = 0;
	FProfileScopedAnimationIdentity GetScopedAnimationIdentity(int32 FlipbookIndex) const;
	int32 ResolveAnimationIndex(const FProfileScopedAnimationIdentity& Identity) const;

	// Identity-keyed delayed actions. Existing index-keyed methods remain the paint-time fast path and preserve
	// Profile behavior. These expire when the selected Layer changes, preventing an old
	// menu/gesture from writing an identical-looking row on a newly selected layer.
	const FFrameHitboxData* GetFrame(const FProfileScopedAnimationIdentity& Identity, int32 FrameIndex) const;
	FFrameHitboxData* GetFrameMutable(const FProfileScopedAnimationIdentity& Identity, int32 FrameIndex);
	FFrameHitboxData* EnsureFrameMutable(const FProfileScopedAnimationIdentity& Identity, int32 FrameIndex);
	bool CanEnsureFrame(const FProfileScopedAnimationIdentity& Identity, int32 FrameIndex) const;
	int32 GetAuthoredFrameCount(const FProfileScopedAnimationIdentity& Identity) const;
	bool CopyFrameDataToRange(const FProfileScopedAnimationIdentity& Identity, int32 SourceFrameIndex,
		int32 RangeStart, int32 RangeEnd, bool bIncludeSockets, bool bMerge);
	int32 MirrorHitboxesInRange(const FProfileScopedAnimationIdentity& Identity,
		int32 RangeStart, int32 RangeEnd, int32 PivotX);
	bool TryGetMergePolicy(const FProfileScopedAnimationIdentity& Identity, bool bAttack,
		EPaper2DPlusLayerSourceMerge& OutPolicy) const;
	bool SetMergePolicy(const FProfileScopedAnimationIdentity& Identity, bool bAttack,
		EPaper2DPlusLayerSourceMerge NewPolicy);

	// --- Frame data resolution ---
	virtual const FFrameHitboxData* GetFrame(int32 FlipbookIndex, int32 FrameIndex) const = 0;
	/** Find-only mutable resolve — never creates (see contract above). */
	virtual FFrameHitboxData* GetFrameMutable(int32 FlipbookIndex, int32 FrameIndex) = 0;
	/** Create-on-first-edit resolve. Call ONLY inside an open transaction. */
	virtual FFrameHitboxData* EnsureFrameMutable(int32 FlipbookIndex, int32 FrameIndex) = 0;
	/** True when EnsureFrameMutable would return a row — checked BEFORE opening a transaction so a
	 *  doomed create never commits an empty undo entry. */
	virtual bool CanEnsureFrame(int32 FlipbookIndex, int32 FrameIndex) const = 0;
	/** Number of authored data rows for the flipbook (may lag the flipbook's key-frame count). */
	virtual int32 GetAuthoredFrameCount(int32 FlipbookIndex) const = 0;

	// --- Transactions ---
	/** The UObject BeginTransaction must Modify() and EndTransaction must MarkPackageDirty(). */
	virtual UObject* GetTransactionTarget() const = 0;
	/** Called by the shared panel exactly once around an undo gesture. Layer providers use this boundary to
	 *  commit their transient FFrameHitboxData adapter back into source-local channel arrays. */
	virtual void BeginEdit() {}
	virtual void CommitEdit() {}
	virtual void DiscardEdit() {}
	/** Undo/reimport/external mutation invalidates any transient adapter without authoring a write. */
	virtual void InvalidateCachedViews() {}

	// --- Capabilities / scope ---
	/** Frame-level fields (bInvulnerable / DefenseClass) — base-profile-only by contract, hidden on
	 *  the layer surface. */
	virtual bool SupportsFrameLevelFields() const = 0;
	/** Ops that write across OTHER flipbooks/animations (clamp-all, propagate-to-group). Hidden in
	 *  layer scope, where the edit surface is one variant's per-animation overrides. */
	virtual bool SupportsCrossFlipbookOps() const = 0;
	virtual bool IsLayerScoped() const = 0;
	/** False when the source scope no longer resolves. Always true for a loaded Profile provider. */
	virtual bool HasResolvedScope() const = 0;
	/** Selected source-owner banner text for the layer surface; empty for the profile provider. */
	virtual FText GetScopeDisplayText() const { return FText::GetEmpty(); }
	/** Placement applied only for display/hit-testing. Stored Layer geometry remains source-local. */
	virtual FVector2D GetAuthoringDisplayOffsetPx(int32 FlipbookIndex, int32 FrameIndex) const
	{
		return FVector2D::ZeroVector;
	}

	// --- Ghost underlay (read-only base boxes rendered dimmed beneath the authored boxes) ---
	virtual void GetGhostBoxes(int32 FlipbookIndex, int32 FrameIndex, TArray<FHitboxData>& OutBoxes) const {}
	/** Optional final compiled projection, distinct from the lower-stack ghost. Never editable. */
	virtual void GetFinalGhostBoxes(int32 FlipbookIndex, int32 FrameIndex, TArray<FHitboxData>& OutBoxes) const {}

	// --- Range/batch ops (per-flipbook; semantics mirror the profile asset's batch methods) ---
	virtual bool CopyFrameDataToRange(int32 FlipbookIndex, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets, bool bMerge) = 0;
	virtual int32 MirrorHitboxesInRange(int32 FlipbookIndex, int32 RangeStart, int32 RangeEnd, int32 PivotX) = 0;

	// --- Per-animation merge policies (layer scope only; find-only — a dropdown must never create an
	//     Layer-authored animation entry) ---
	virtual bool TryGetMergePolicy(int32 FlipbookIndex, bool bAttack, EPaper2DPlusLayerSourceMerge& OutPolicy) const { return false; }
	virtual bool SetMergePolicy(int32 FlipbookIndex, bool bAttack, EPaper2DPlusLayerSourceMerge NewPolicy) { return false; }
};

/** Default provider — the Character Profile editor's own frame data. Byte-identical to the pre-seam
 *  direct-walk code paths (same asset resolution through the model, same index guards). */
class PAPER2DPLUSEDITOR_API FProfileHitboxDataProvider : public FHitboxFrameDataProvider
{
public:
	explicit FProfileHitboxDataProvider(TSharedPtr<FCharacterProfileEditorModel> InModel);
	using FHitboxFrameDataProvider::CanEnsureFrame;
	using FHitboxFrameDataProvider::CopyFrameDataToRange;
	using FHitboxFrameDataProvider::EnsureFrameMutable;
	using FHitboxFrameDataProvider::GetAuthoredFrameCount;
	using FHitboxFrameDataProvider::GetFrame;
	using FHitboxFrameDataProvider::GetFrameMutable;
	using FHitboxFrameDataProvider::MirrorHitboxesInRange;
	using FHitboxFrameDataProvider::ResolveAnimationIndex;
	using FHitboxFrameDataProvider::SetMergePolicy;
	using FHitboxFrameDataProvider::TryGetMergePolicy;

	virtual FProfileAnimationIdentity GetAnimationIdentity(int32 FlipbookIndex) const override;
	virtual int32 ResolveAnimationIndex(const FProfileAnimationIdentity& Identity) const override;
	virtual FProfileLayerScopeIdentity GetLayerScopeIdentity() const override
	{
		return FProfileLayerScopeIdentity::Profile();
	}
	virtual const FFrameHitboxData* GetFrame(int32 FlipbookIndex, int32 FrameIndex) const override;
	virtual FFrameHitboxData* GetFrameMutable(int32 FlipbookIndex, int32 FrameIndex) override;
	virtual FFrameHitboxData* EnsureFrameMutable(int32 FlipbookIndex, int32 FrameIndex) override;
	virtual bool CanEnsureFrame(int32 FlipbookIndex, int32 FrameIndex) const override;
	virtual int32 GetAuthoredFrameCount(int32 FlipbookIndex) const override;
	virtual UObject* GetTransactionTarget() const override;
	virtual bool SupportsFrameLevelFields() const override { return true; }
	virtual bool SupportsCrossFlipbookOps() const override { return true; }
	virtual bool IsLayerScoped() const override { return false; }
	virtual bool HasResolvedScope() const override { return true; }
	virtual bool CopyFrameDataToRange(int32 FlipbookIndex, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets, bool bMerge) override;
	virtual int32 MirrorHitboxesInRange(int32 FlipbookIndex, int32 RangeStart, int32 RangeEnd, int32 PivotX) override;
	virtual void GetGhostBoxes(int32 FlipbookIndex, int32 FrameIndex, TArray<FHitboxData>& OutBoxes) const override;

private:
	UPaper2DPlusCharacterProfileAsset* ResolveProfile() const;
	bool UsesCharacterBaseline() const;
	const struct FPaper2DPlusCharacterBaselineAnimation* ResolveBaseline(int32 FlipbookIndex) const;
	struct FPaper2DPlusCharacterBaselineAnimation* ResolveBaselineMutable(int32 FlipbookIndex);

	TWeakPtr<FCharacterProfileEditorModel> ModelWeak;
};

/** Canonical Layer provider — resolves the model's stable selected LayerId live on every call. Writes land
 *  on that real layer's AuthoredAnimations source; a transient shared-editor frame adapter separates Attack,
 *  Hurt, and socket channels again at the one transaction boundary. */
class PAPER2DPLUSEDITOR_API FLayerHitboxDataProvider : public FHitboxFrameDataProvider
{
public:
	FLayerHitboxDataProvider(TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> InLayerAsset, TSharedPtr<FCharacterProfileEditorModel> InModel);
	using FHitboxFrameDataProvider::CanEnsureFrame;
	using FHitboxFrameDataProvider::CopyFrameDataToRange;
	using FHitboxFrameDataProvider::EnsureFrameMutable;
	using FHitboxFrameDataProvider::GetAuthoredFrameCount;
	using FHitboxFrameDataProvider::GetFrame;
	using FHitboxFrameDataProvider::GetFrameMutable;
	using FHitboxFrameDataProvider::MirrorHitboxesInRange;
	using FHitboxFrameDataProvider::ResolveAnimationIndex;
	using FHitboxFrameDataProvider::SetMergePolicy;
	using FHitboxFrameDataProvider::TryGetMergePolicy;

	virtual FProfileAnimationIdentity GetAnimationIdentity(int32 FlipbookIndex) const override;
	virtual int32 ResolveAnimationIndex(const FProfileAnimationIdentity& Identity) const override;
	virtual FProfileLayerScopeIdentity GetLayerScopeIdentity() const override;
	virtual const FFrameHitboxData* GetFrame(int32 FlipbookIndex, int32 FrameIndex) const override;
	virtual FFrameHitboxData* GetFrameMutable(int32 FlipbookIndex, int32 FrameIndex) override;
	virtual FFrameHitboxData* EnsureFrameMutable(int32 FlipbookIndex, int32 FrameIndex) override;
	virtual bool CanEnsureFrame(int32 FlipbookIndex, int32 FrameIndex) const override;
	virtual int32 GetAuthoredFrameCount(int32 FlipbookIndex) const override;
	virtual UObject* GetTransactionTarget() const override;
	virtual bool SupportsFrameLevelFields() const override { return false; }
	virtual bool SupportsCrossFlipbookOps() const override { return false; }
	virtual bool IsLayerScoped() const override { return true; }
	virtual bool HasResolvedScope() const override;
	virtual FText GetScopeDisplayText() const override;
	virtual void BeginEdit() override;
	virtual void CommitEdit() override;
	virtual void DiscardEdit() override;
	virtual void InvalidateCachedViews() override;
	virtual FVector2D GetAuthoringDisplayOffsetPx(int32 FlipbookIndex, int32 FrameIndex) const override;
	virtual void GetGhostBoxes(int32 FlipbookIndex, int32 FrameIndex, TArray<FHitboxData>& OutBoxes) const override;
	virtual void GetFinalGhostBoxes(int32 FlipbookIndex, int32 FrameIndex, TArray<FHitboxData>& OutBoxes) const override;
	virtual bool CopyFrameDataToRange(int32 FlipbookIndex, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets, bool bMerge) override;
	virtual int32 MirrorHitboxesInRange(int32 FlipbookIndex, int32 RangeStart, int32 RangeEnd, int32 PivotX) override;
	virtual bool TryGetMergePolicy(int32 FlipbookIndex, bool bAttack, EPaper2DPlusLayerSourceMerge& OutPolicy) const override;
	virtual bool SetMergePolicy(int32 FlipbookIndex, bool bAttack, EPaper2DPlusLayerSourceMerge NewPolicy) override;

private:
	UPaper2DPlusCharacterProfileAsset* ResolveProfile() const;
	/** The stable animation key for a profile flipbook index (Identity.FlipbookName); empty on miss. */
	FString ResolveAnimationName(int32 FlipbookIndex) const;
	/** The canonical key-frame count used for grow-never-shrink source initialization. */
	int32 ResolveBaseFrameCount(int32 FlipbookIndex) const;
	const FCharacterLayer* ResolveScopedLayer() const;
	FCharacterLayer* ResolveScopedLayerMutable();
	const FCharacterLayerAuthoredAnimationData* FindAuthoredAnimation(int32 FlipbookIndex) const;
	FCharacterLayerAuthoredAnimationData* FindAuthoredAnimationMutable(int32 FlipbookIndex);
	FCharacterLayerAuthoredAnimationData* EnsureAuthoredAnimationMutable(int32 FlipbookIndex);
	const FCharacterLayerAuthoredAnimationData* FindAuthoredAnimation(
		const FCharacterLayer& Layer, const FProfileAnimationIdentity& Identity) const;
	FCharacterLayerAuthoredAnimationData* FindAuthoredAnimationMutable(
		FCharacterLayer& Layer, const FProfileAnimationIdentity& Identity);
	void RebuildFrameView(const FProfileScopedAnimationIdentity& Identity) const;
	void WriteFrameViewToSource();
	void BuildCompositionGhosts(int32 FlipbookIndex, int32 FrameIndex,
		TArray<FHitboxData>* OutLower, TArray<FHitboxData>* OutFinal) const;

	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TWeakPtr<FCharacterProfileEditorModel> ModelWeak;
	mutable bool bFrameViewValid = false;
	mutable bool bFrameViewBorrowedMutable = false;
	mutable bool bEditActive = false;
	mutable FProfileScopedAnimationIdentity FrameViewIdentity;
	mutable TArray<FFrameHitboxData> FrameView;
};
