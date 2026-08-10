// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusFrameCurve.h"
#include "ProfileToolPanelProvider.h"

class FCharacterProfileEditorModel;
class FPaper2DPlusFrameCuePlacementProviderSnapshot;
class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;

/**
 * Transient editor identity for an instanced Frame Cue placement.
 *
 * UObject identity is preferred while the placement survives. Undo, Blueprint class reinstancing, and
 * nested-array reconstruction may replace that object, so resolution falls back to a deterministic authored
 * signature plus its occurrence among otherwise-identical placements. No stale array index is retained.
 */
struct PAPER2DPLUSEDITOR_API FFrameCueStableIdentity
{
	FProfileScopedAnimationIdentity Scope;
	TWeakObjectPtr<UPaper2DPlusCueBase> Object;
	FSoftClassPath CueClassPath;
	FName DebugName;
	FGameplayTag CueTag;
	int32 PrimaryAnchorFrame = INDEX_NONE;
	int32 CueFrameCount = 1;
	/** Class + complete persistent reflected payload; prevents same-anchor cues with different subtype data retargeting. */
	FString SemanticDigest;
	int32 MatchingOccurrence = INDEX_NONE;

	bool IsValid() const
	{
		return Scope.IsValid()
			&& (Object.IsValid() || !CueClassPath.IsNull());
	}
};

/** Explicit terminal policy for removing an optional named Cue track. */
enum class EPaper2DPlusFrameCueTrackRemovalMode : uint8
{
	MoveCuesToDefault,
	DeleteCues
};

/** Active and excluded/stashed membership counts used by the non-empty track confirmation. */
struct PAPER2DPLUSEDITOR_API FFrameCueTrackUsage
{
	int32 ActiveCueCount = 0;
	int32 StashedCueCount = 0;

	int32 TotalCueCount() const { return ActiveCueCount + StashedCueCount; }
};

/**
 * Data/capability seam for the full Frame Cue authoring surface.
 *
 * Unmanaged Profile scope points at FFlipbookProfileEntry::FrameEventData.FrameCues; attached Profiles route
 * Character-wide edits to CharacterBaseline and treat compiled runtime Cues as read-only output. Layer scope
 * points at the stable selected LayerId's AuthoredAnimations[].FrameCues. Layer scope never exposes mutable
 * Profile curves. Both scopes use the same stable animation/cue identity and the same full editor widget.
 */
class PAPER2DPLUSEDITOR_API FFrameCueDataProvider
{
public:
	virtual ~FFrameCueDataProvider() = default;

	// Stable animation identity — resolve against the live profile at action time.
	virtual FProfileAnimationIdentity GetAnimationIdentity(int32 FlipbookIndex) const = 0;
	virtual int32 ResolveAnimationIndex(const FProfileAnimationIdentity& Identity) const = 0;
	/** Profile() for Profile storage; stable LayerId snapshot for canonical Layer storage. */
	virtual FProfileLayerScopeIdentity GetLayerScopeIdentity() const = 0;
	FProfileScopedAnimationIdentity GetScopedAnimationIdentity(int32 FlipbookIndex) const;
	int32 ResolveAnimationIndex(const FProfileScopedAnimationIdentity& Identity) const;

	// Index-keyed paint-time resolution.
	virtual const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetCues(int32 FlipbookIndex) const = 0;
	/** Find-only. A Details value lambda or refresh must never create a layer override. */
	virtual TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetCuesMutable(int32 FlipbookIndex) = 0;
	/** Create-on-first-edit. Call only inside the owning panel's open transaction. */
	virtual TArray<TObjectPtr<UPaper2DPlusCueBase>>* EnsureCuesMutable(int32 FlipbookIndex) = 0;
	/** Preflight before opening a transaction, preventing empty undo entries. */
	virtual bool CanEnsureCues(int32 FlipbookIndex) const = 0;

	/** Editor-only organizational sidecar. Null reads project to an implicit empty/Default layout. */
	virtual const FPaper2DPlusFrameCueTrackLayout* GetTrackLayout(int32 FlipbookIndex) const = 0;
	virtual FPaper2DPlusFrameCueTrackLayout* GetTrackLayoutMutable(int32 FlipbookIndex) = 0;
	/** Create only inside an owning transaction; Layer scope may create its first authored animation row. */
	virtual FPaper2DPlusFrameCueTrackLayout* EnsureTrackLayoutMutable(int32 FlipbookIndex) = 0;
	virtual bool CanEnsureTrackLayout(int32 FlipbookIndex) const = 0;

	/** Profile curves are visible in either scope for context; only Profile scope returns a mutable pointer. */
	virtual const FFlipbookCurveData* GetCurveData(int32 FlipbookIndex) const = 0;
	virtual FFlipbookCurveData* GetCurveDataMutable(int32 FlipbookIndex) = 0;
	virtual bool SupportsCurveEditing() const = 0;

	virtual int32 GetFrameCount(int32 FlipbookIndex) const = 0;
	virtual UObject* GetTransactionTarget() const = 0;
	virtual UObject* GetCueOuter() const = 0;
	virtual bool IsLayerScoped() const = 0;
	virtual bool HasResolvedScope() const = 0;
	/** Freeze exact Profile/Layer/animation storage without retaining the editor model or mutable selection. */
	virtual TSharedPtr<FPaper2DPlusFrameCuePlacementProviderSnapshot> CapturePlacementSnapshot(
		const FProfileScopedAnimationIdentity& Identity) const = 0;
	virtual FText GetScopeDisplayText() const { return FText::GetEmpty(); }
	/** Read-only context projections. Null means no separate projection for this provider. */
	virtual const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetLowerCues(int32 FlipbookIndex) const
	{
		return nullptr;
	}
	virtual const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetFinalCues(int32 FlipbookIndex) const
	{
		return nullptr;
	}

	// Stable-identity action helpers.
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetCues(
		const FProfileScopedAnimationIdentity& Identity) const;
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetCuesMutable(
		const FProfileScopedAnimationIdentity& Identity);
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* EnsureCuesMutable(
		const FProfileScopedAnimationIdentity& Identity);
	bool CanEnsureCues(const FProfileScopedAnimationIdentity& Identity) const;
	const FPaper2DPlusFrameCueTrackLayout* GetTrackLayout(
		const FProfileScopedAnimationIdentity& Identity) const;
	FPaper2DPlusFrameCueTrackLayout* GetTrackLayoutMutable(
		const FProfileScopedAnimationIdentity& Identity);
	FPaper2DPlusFrameCueTrackLayout* EnsureTrackLayoutMutable(
		const FProfileScopedAnimationIdentity& Identity);
	bool CanEnsureTrackLayout(const FProfileScopedAnimationIdentity& Identity) const;
	const FFlipbookCurveData* GetCurveData(const FProfileScopedAnimationIdentity& Identity) const;
	FFlipbookCurveData* GetCurveDataMutable(const FProfileScopedAnimationIdentity& Identity);
	int32 GetFrameCount(const FProfileScopedAnimationIdentity& Identity) const;
	/** Literal empty slots in the current authoritative Cue array; invalid non-null objects are not included. */
	int32 GetMissingCuePlacementCount(const FProfileScopedAnimationIdentity& Identity) const;

	FFrameCueStableIdentity GetCueIdentity(int32 FlipbookIndex, const UPaper2DPlusCueBase* Cue) const;
	FFrameCueStableIdentity GetCueIdentity(
		const FProfileScopedAnimationIdentity& ScopeIdentity,
		const UPaper2DPlusCueBase* Cue) const;
	int32 ResolveCueIndex(const FFrameCueStableIdentity& Identity) const;
	UPaper2DPlusCueBase* ResolveCue(const FFrameCueStableIdentity& Identity) const;

	// Storage-shape-neutral named-track reads and atomic Cue/layout commands used by the timeline.
	TArray<FPaper2DPlusFrameCueTrackDefinition> GetOptionalTracks(
		const FProfileScopedAnimationIdentity& Identity) const;
	FString SuggestTrackName(const FProfileScopedAnimationIdentity& Identity) const;
	FGuid ResolveCueTrackId(
		const FProfileScopedAnimationIdentity& Identity,
		const UPaper2DPlusCueBase* Cue) const;
	FGuid AddTrack(
		const FProfileScopedAnimationIdentity& Identity,
		const FString& DisplayName,
		const FGuid& RequestedTrackId = FGuid());
	bool RenameTrack(
		const FProfileScopedAnimationIdentity& Identity,
		const FGuid& TrackId,
		const FString& DisplayName);
	bool ReorderTrack(
		const FProfileScopedAnimationIdentity& Identity,
		const FGuid& TrackId,
		int32 NewOptionalTrackIndex);
	FFrameCueTrackUsage GetTrackUsage(
		const FProfileScopedAnimationIdentity& Identity,
		const FGuid& TrackId) const;
	bool RemoveTrack(
		const FProfileScopedAnimationIdentity& Identity,
		const FGuid& TrackId,
		EPaper2DPlusFrameCueTrackRemovalMode RemovalMode);
	bool AssignCueToTrack(const FFrameCueStableIdentity& CueIdentity, const FGuid& TrackId);
	FFrameCueStableIdentity DuplicateCue(const FFrameCueStableIdentity& SourceIdentity);
	bool DeleteCue(const FFrameCueStableIdentity& CueIdentity);
	/** Remove every literal empty slot from the current authoritative Cue array in one transaction. */
	int32 RemoveAllMissingCuePlacements(const FProfileScopedAnimationIdentity& Identity);
	bool ReplaceCueArrayAtomically(
		const FProfileScopedAnimationIdentity& Identity,
		TArray<TObjectPtr<UPaper2DPlusCueBase>>&& ReplacementCues,
		const FPaper2DPlusFrameCueReplacementMap& Replacements);

protected:
	/** First collection is active; later collections are excluded/stashed. */
	virtual void GatherCueCollections(
		int32 FlipbookIndex,
		TArray<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*>& OutCollections) const;
	virtual void GatherCueCollectionsMutable(
		int32 FlipbookIndex,
		TArray<TArray<TObjectPtr<UPaper2DPlusCueBase>>*>& OutCollections);

private:
	void RetainValidCueAssignments(
		const FProfileScopedAnimationIdentity& Identity,
		FPaper2DPlusFrameCueTrackLayout& Layout) const;
};

/** Character Profile storage. This is the default provider and preserves the original storage/transaction path. */
class PAPER2DPLUSEDITOR_API FProfileFrameCueDataProvider final : public FFrameCueDataProvider
{
public:
	explicit FProfileFrameCueDataProvider(TSharedPtr<FCharacterProfileEditorModel> InModel);
	using FFrameCueDataProvider::CanEnsureCues;
	using FFrameCueDataProvider::CanEnsureTrackLayout;
	using FFrameCueDataProvider::EnsureCuesMutable;
	using FFrameCueDataProvider::EnsureTrackLayoutMutable;
	using FFrameCueDataProvider::GetCues;
	using FFrameCueDataProvider::GetCuesMutable;
	using FFrameCueDataProvider::GetTrackLayout;
	using FFrameCueDataProvider::GetTrackLayoutMutable;
	using FFrameCueDataProvider::GetCurveData;
	using FFrameCueDataProvider::GetCurveDataMutable;
	using FFrameCueDataProvider::GetFrameCount;
	using FFrameCueDataProvider::ResolveAnimationIndex;

	virtual FProfileAnimationIdentity GetAnimationIdentity(int32 FlipbookIndex) const override;
	virtual int32 ResolveAnimationIndex(const FProfileAnimationIdentity& Identity) const override;
	virtual FProfileLayerScopeIdentity GetLayerScopeIdentity() const override
	{
		return FProfileLayerScopeIdentity::Profile();
	}
	virtual const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetCues(int32 FlipbookIndex) const override;
	virtual TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetCuesMutable(int32 FlipbookIndex) override;
	virtual TArray<TObjectPtr<UPaper2DPlusCueBase>>* EnsureCuesMutable(int32 FlipbookIndex) override;
	virtual bool CanEnsureCues(int32 FlipbookIndex) const override;
	virtual const FPaper2DPlusFrameCueTrackLayout* GetTrackLayout(int32 FlipbookIndex) const override;
	virtual FPaper2DPlusFrameCueTrackLayout* GetTrackLayoutMutable(int32 FlipbookIndex) override;
	virtual FPaper2DPlusFrameCueTrackLayout* EnsureTrackLayoutMutable(int32 FlipbookIndex) override;
	virtual bool CanEnsureTrackLayout(int32 FlipbookIndex) const override;
	virtual const FFlipbookCurveData* GetCurveData(int32 FlipbookIndex) const override;
	virtual FFlipbookCurveData* GetCurveDataMutable(int32 FlipbookIndex) override;
	virtual bool SupportsCurveEditing() const override { return true; }
	virtual int32 GetFrameCount(int32 FlipbookIndex) const override;
	virtual UObject* GetTransactionTarget() const override;
	virtual UObject* GetCueOuter() const override;
	virtual bool IsLayerScoped() const override { return false; }
	virtual bool HasResolvedScope() const override;
	virtual TSharedPtr<FPaper2DPlusFrameCuePlacementProviderSnapshot> CapturePlacementSnapshot(
		const FProfileScopedAnimationIdentity& Identity) const override;
	virtual const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetFinalCues(int32 FlipbookIndex) const override;

private:
	UPaper2DPlusCharacterProfileAsset* ResolveProfile() const;
	bool UsesCharacterBaseline() const;
	const struct FPaper2DPlusCharacterBaselineAnimation* ResolveBaseline(int32 FlipbookIndex) const;
	struct FPaper2DPlusCharacterBaselineAnimation* ResolveBaselineMutable(int32 FlipbookIndex);
	virtual void GatherCueCollections(
		int32 FlipbookIndex,
		TArray<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*>& OutCollections) const override;
	virtual void GatherCueCollectionsMutable(
		int32 FlipbookIndex,
		TArray<TArray<TObjectPtr<UPaper2DPlusCueBase>>*>& OutCollections) override;
	TWeakPtr<FCharacterProfileEditorModel> ModelWeak;
};

/** Selected canonical Character Layer source storage. Curves remain Profile-owned and read-only. */
class PAPER2DPLUSEDITOR_API FLayerFrameCueDataProvider final : public FFrameCueDataProvider
{
public:
	FLayerFrameCueDataProvider(
		TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> InLayerAsset,
		TSharedPtr<FCharacterProfileEditorModel> InModel);
	using FFrameCueDataProvider::CanEnsureCues;
	using FFrameCueDataProvider::CanEnsureTrackLayout;
	using FFrameCueDataProvider::EnsureCuesMutable;
	using FFrameCueDataProvider::EnsureTrackLayoutMutable;
	using FFrameCueDataProvider::GetCues;
	using FFrameCueDataProvider::GetCuesMutable;
	using FFrameCueDataProvider::GetTrackLayout;
	using FFrameCueDataProvider::GetTrackLayoutMutable;
	using FFrameCueDataProvider::GetCurveData;
	using FFrameCueDataProvider::GetCurveDataMutable;
	using FFrameCueDataProvider::GetFrameCount;
	using FFrameCueDataProvider::ResolveAnimationIndex;

	virtual FProfileAnimationIdentity GetAnimationIdentity(int32 FlipbookIndex) const override;
	virtual int32 ResolveAnimationIndex(const FProfileAnimationIdentity& Identity) const override;
	virtual FProfileLayerScopeIdentity GetLayerScopeIdentity() const override;
	virtual const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetCues(int32 FlipbookIndex) const override;
	virtual TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetCuesMutable(int32 FlipbookIndex) override;
	virtual TArray<TObjectPtr<UPaper2DPlusCueBase>>* EnsureCuesMutable(int32 FlipbookIndex) override;
	virtual bool CanEnsureCues(int32 FlipbookIndex) const override;
	virtual const FPaper2DPlusFrameCueTrackLayout* GetTrackLayout(int32 FlipbookIndex) const override;
	virtual FPaper2DPlusFrameCueTrackLayout* GetTrackLayoutMutable(int32 FlipbookIndex) override;
	virtual FPaper2DPlusFrameCueTrackLayout* EnsureTrackLayoutMutable(int32 FlipbookIndex) override;
	virtual bool CanEnsureTrackLayout(int32 FlipbookIndex) const override;
	virtual const FFlipbookCurveData* GetCurveData(int32 FlipbookIndex) const override;
	virtual FFlipbookCurveData* GetCurveDataMutable(int32 FlipbookIndex) override;
	virtual bool SupportsCurveEditing() const override { return false; }
	virtual int32 GetFrameCount(int32 FlipbookIndex) const override;
	virtual UObject* GetTransactionTarget() const override;
	virtual UObject* GetCueOuter() const override;
	virtual bool IsLayerScoped() const override { return true; }
	virtual bool HasResolvedScope() const override;
	virtual TSharedPtr<FPaper2DPlusFrameCuePlacementProviderSnapshot> CapturePlacementSnapshot(
		const FProfileScopedAnimationIdentity& Identity) const override;
	virtual FText GetScopeDisplayText() const override;
	virtual const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetLowerCues(int32 FlipbookIndex) const override;
	virtual const TArray<TObjectPtr<UPaper2DPlusCueBase>>* GetFinalCues(int32 FlipbookIndex) const override;

private:
	UPaper2DPlusCharacterProfileAsset* ResolveProfile() const;
	const struct FCharacterLayer* ResolveScopedLayer() const;
	struct FCharacterLayer* ResolveScopedLayerMutable();
	const struct FCharacterLayerAuthoredAnimationData* FindAuthoredAnimation(int32 FlipbookIndex) const;
	struct FCharacterLayerAuthoredAnimationData* FindAuthoredAnimationMutable(int32 FlipbookIndex);
	struct FCharacterLayerAuthoredAnimationData* EnsureAuthoredAnimationMutable(
		int32 FlipbookIndex,
		bool bRequirePositiveFrameCount = true);
	void RebuildCueProjections(int32 FlipbookIndex) const;

	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TWeakPtr<FCharacterProfileEditorModel> ModelWeak;
	mutable FProfileScopedAnimationIdentity ProjectionIdentity;
	mutable TArray<TObjectPtr<UPaper2DPlusCueBase>> LowerCueProjection;
	mutable TArray<TObjectPtr<UPaper2DPlusCueBase>> FinalCueProjection;
};
