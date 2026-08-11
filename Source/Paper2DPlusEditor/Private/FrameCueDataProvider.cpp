// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCueDataProvider.h"

#include "CharacterLayerBakeCore.h"
#include "CharacterProfileEditorModel.h"
#include "FrameCues/Paper2DPlusFrameCuePlacementAuthoring.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusLayerGameplayCompose.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	int32 FrameCueProvider_GetFrameCount(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		int32 FlipbookIndex)
	{
		if (!Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex))
		{
			return 0;
		}
		const FFlipbookProfileEntry& Entry = Profile->Flipbooks[FlipbookIndex];
		if (UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.Get())
		{
			return Flipbook->GetNumKeyFrames();
		}
		// Profile frame rows are cook-synced and remain a non-blocking count source when the soft flipbook
		// is unloaded. A provider read must never synchronously load an animation during Slate paint.
		return Entry.CombatData.Frames.Num();
	}

	bool FrameCueProvider_MatchesSignature(
		const FFrameCueStableIdentity& Identity,
		const UPaper2DPlusCueBase* Cue)
	{
		return Cue
			&& Identity.CueClassPath == FSoftClassPath(Cue->GetClass())
			&& Identity.DebugName == Cue->DebugName
			&& Identity.CueTag == Cue->CueTag
			&& Identity.PrimaryAnchorFrame == Cue->GetPrimaryAnchorFrame()
			&& Identity.CueFrameCount == Cue->GetCueFrameCount()
			&& Identity.SemanticDigest == CharacterLayerBakeCore::ComputeCueSemanticDigest(Cue);
	}

	bool FrameCueProvider_AnimationMatches(
		const FCharacterLayerAuthoredAnimationData& Entry,
		const FProfileAnimationIdentity& Identity)
	{
		if (Identity.FlipbookPath.IsValid() && Entry.Flipbook.ToSoftObjectPath().IsValid())
		{
			return Entry.Flipbook.ToSoftObjectPath() == Identity.FlipbookPath;
		}
		return !Identity.FallbackName.IsEmpty()
			&& Entry.LegacyAnimationName.Equals(Identity.FallbackName, ESearchCase::IgnoreCase);
	}

	FFrameCueStableIdentity FrameCueProvider_MakeCueIdentity(
		const FProfileScopedAnimationIdentity& Scope,
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues,
		UPaper2DPlusCueBase* Cue)
	{
		FFrameCueStableIdentity Identity;
		Identity.Scope = Scope;
		if (!IsValid(Cue) || !Scope.IsValid())
		{
			return Identity;
		}
		Identity.Object = Cue;
		Identity.CueClassPath = FSoftClassPath(Cue->GetClass());
		Identity.DebugName = Cue->DebugName;
		Identity.CueTag = Cue->CueTag;
		Identity.PrimaryAnchorFrame = Cue->GetPrimaryAnchorFrame();
		Identity.CueFrameCount = Cue->GetCueFrameCount();
		Identity.SemanticDigest = CharacterLayerBakeCore::ComputeCueSemanticDigest(Cue);

		int32 MatchingOccurrence = 0;
		for (const TObjectPtr<UPaper2DPlusCueBase>& Candidate : Cues)
		{
			if (Candidate == Cue)
			{
				Identity.MatchingOccurrence = MatchingOccurrence;
				break;
			}
			if (FrameCueProvider_MatchesSignature(Identity, Candidate.Get()))
			{
				++MatchingOccurrence;
			}
		}
		return Identity;
	}

	void FrameCueProvider_DiscardRolledBackCue(UPaper2DPlusCueBase*& Cue)
	{
		if (IsValid(Cue))
		{
			Cue->ClearFlags(RF_Transactional);
			Cue->MarkAsGarbage();
		}
		Cue = nullptr;
	}

	void FrameCueProvider_RestoreObjectState(
		UObject* Target,
		UPackage* Package,
		bool bWasTransactional,
		bool bWasPackageDirty)
	{
		if (Target && !bWasTransactional)
		{
			Target->ClearFlags(RF_Transactional);
		}
		if (Package)
		{
			Package->SetDirtyFlag(bWasPackageDirty);
		}
	}

	class FProfileFrameCuePlacementProviderSnapshot final
		: public FPaper2DPlusFrameCuePlacementProviderSnapshot
	{
	public:
		FProfileFrameCuePlacementProviderSnapshot(
			UPaper2DPlusCharacterProfileAsset* InProfile,
			const FProfileScopedAnimationIdentity& InScope,
			bool bInUsesCharacterBaseline)
			: Profile(InProfile)
			, Scope(InScope)
			, bUsesCharacterBaseline(bInUsesCharacterBaseline)
		{
		}

		virtual FPaper2DPlusFrameCuePlacementAppendResult AppendPlacementAtomically(
			UClass* CueClass,
			EPaper2DPlusFrameCuePlacementKind Kind,
			int32 CapturedFrame,
			FGuid CapturedTrackId,
			EPaper2DPlusFrameCuePlacementFailurePoint FailurePoint) override
		{
			FPaper2DPlusFrameCuePlacementAppendResult Result;
			UPaper2DPlusCharacterProfileAsset* Target = Profile.Get();
			const int32 FlipbookIndex = ResolveAnimationIndex();
			const int32 FrameCount = FrameCueProvider_GetFrameCount(Target, FlipbookIndex);
			TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = ResolveCueStorage(FlipbookIndex);
			FPaper2DPlusFrameCueTrackLayout* TrackLayout = ResolveTrackLayout(FlipbookIndex);
			if (!IsValid(Target)
				|| Target->LayerBakeOwnerToken.IsValid() != bUsesCharacterBaseline
				|| FlipbookIndex == INDEX_NONE
				|| CapturedFrame < 0
				|| CapturedFrame >= FrameCount
				|| !Cues
				|| !TrackLayout
				|| (CapturedTrackId.IsValid() && !TrackLayout->IsTrackIdValid(CapturedTrackId)))
			{
				return Result;
			}
			if (!Paper2DPlusFrameCuePlacementAuthoring::IsCueClassCompatible(CueClass, Kind))
			{
				Result.Status = EPaper2DPlusFrameCuePlacementAppendStatus::InvalidCueClass;
				return Result;
			}

			const TArray<TObjectPtr<UPaper2DPlusCueBase>> PreviousCues = *Cues;
			const FPaper2DPlusFrameCueTrackLayout PreviousTrackLayout = *TrackLayout;
			UPackage* Package = Target->GetOutermost();
			const bool bWasPackageDirty = Package && Package->IsDirty();
			const bool bWasTransactional = Target->HasAnyFlags(RF_Transactional);
			FScopedTransaction Transaction(NSLOCTEXT(
				"Paper2DPlusFrameCuePlacement",
				"PlaceProfileFrameCue",
				"Place Frame Cue"));
			if (!Transaction.IsOutstanding())
			{
				return Result;
			}

			UPaper2DPlusCueBase* NewCue = nullptr;
			auto RollBack = [&]()
			{
				if (TArray<TObjectPtr<UPaper2DPlusCueBase>>* RestoreCues =
					ResolveCueStorage(ResolveAnimationIndex()))
				{
					*RestoreCues = PreviousCues;
				}
				if (FPaper2DPlusFrameCueTrackLayout* RestoreLayout =
					ResolveTrackLayout(ResolveAnimationIndex()))
				{
					*RestoreLayout = PreviousTrackLayout;
				}
				FrameCueProvider_DiscardRolledBackCue(NewCue);
				Transaction.Cancel();
				FrameCueProvider_RestoreObjectState(
					Target, Package, bWasTransactional, bWasPackageDirty);
				Result.Status = EPaper2DPlusFrameCuePlacementAppendStatus::RolledBack;
				return Result;
			};

			Target->SetFlags(RF_Transactional);
			Target->Modify();
			if (FailurePoint == EPaper2DPlusFrameCuePlacementFailurePoint::AfterTargetModify)
			{
				return RollBack();
			}
			Cues = ResolveCueStorage(ResolveAnimationIndex());
			if (!Cues)
			{
				return RollBack();
			}
			if (FailurePoint == EPaper2DPlusFrameCuePlacementFailurePoint::AfterStorageEnsure)
			{
				return RollBack();
			}
			NewCue = Paper2DPlusFrameCuePlacementAuthoring::CreatePlacementFromClassDefaults(
				Target, CueClass, Kind, CapturedFrame, FrameCount);
			if (!NewCue
				|| FailurePoint ==
					EPaper2DPlusFrameCuePlacementFailurePoint::AfterPlacementConstruction)
			{
				return RollBack();
			}
			Cues = ResolveCueStorage(ResolveAnimationIndex());
			if (!Cues)
			{
				return RollBack();
			}
			Cues->Add(NewCue);
			TrackLayout = ResolveTrackLayout(ResolveAnimationIndex());
			if (!TrackLayout || !TrackLayout->AssignCue(NewCue, CapturedTrackId))
			{
				return RollBack();
			}
			if (FailurePoint == EPaper2DPlusFrameCuePlacementFailurePoint::AfterArrayAppend)
			{
				return RollBack();
			}
			Result.PlacementIdentity = FrameCueProvider_MakeCueIdentity(Scope, *Cues, NewCue);
			if (!Result.PlacementIdentity.IsValid()
				|| FailurePoint ==
					EPaper2DPlusFrameCuePlacementFailurePoint::AfterIdentityDerivation)
			{
				return RollBack();
			}
			Result.Status = EPaper2DPlusFrameCuePlacementAppendStatus::Success;
			return Result;
		}

	private:
		int32 ResolveAnimationIndex() const
		{
			return Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
				Profile.Get(), Scope.Animation);
		}

		TArray<TObjectPtr<UPaper2DPlusCueBase>>* ResolveCueStorage(int32 FlipbookIndex) const
		{
			UPaper2DPlusCharacterProfileAsset* Target = Profile.Get();
			if (!Target || !Target->Flipbooks.IsValidIndex(FlipbookIndex))
			{
				return nullptr;
			}
			if (!bUsesCharacterBaseline)
			{
				return &Target->Flipbooks[FlipbookIndex].FrameEventData.FrameCues;
			}
			const FFlipbookIdentity& Identity = Target->Flipbooks[FlipbookIndex].Identity;
			FPaper2DPlusCharacterBaselineAnimation* Baseline =
				Target->FindCharacterBaselineMutable(
					Identity.Flipbook.ToSoftObjectPath(), Identity.FlipbookName);
			return Baseline ? &Baseline->FrameCues : nullptr;
		}

		FPaper2DPlusFrameCueTrackLayout* ResolveTrackLayout(int32 FlipbookIndex) const
		{
			UPaper2DPlusCharacterProfileAsset* Target = Profile.Get();
			if (!Target || !Target->Flipbooks.IsValidIndex(FlipbookIndex))
			{
				return nullptr;
			}
			if (!bUsesCharacterBaseline)
			{
				return &Target->Flipbooks[FlipbookIndex].FrameEventData.CueTrackLayout;
			}
			const FFlipbookIdentity& Identity = Target->Flipbooks[FlipbookIndex].Identity;
			FPaper2DPlusCharacterBaselineAnimation* Baseline =
				Target->FindCharacterBaselineMutable(
					Identity.Flipbook.ToSoftObjectPath(), Identity.FlipbookName);
			return Baseline ? &Baseline->CueTrackLayout : nullptr;
		}

		TStrongObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile;
		FProfileScopedAnimationIdentity Scope;
		bool bUsesCharacterBaseline = false;
	};

	class FLayerFrameCuePlacementProviderSnapshot final
		: public FPaper2DPlusFrameCuePlacementProviderSnapshot
	{
	public:
		FLayerFrameCuePlacementProviderSnapshot(
			UPaper2DPlusCharacterLayerAsset* InLayerAsset,
			UPaper2DPlusCharacterProfileAsset* InProfile,
			const FProfileScopedAnimationIdentity& InScope)
			: LayerAsset(InLayerAsset)
			, Profile(InProfile)
			, Scope(InScope)
		{
		}

		virtual FPaper2DPlusFrameCuePlacementAppendResult AppendPlacementAtomically(
			UClass* CueClass,
			EPaper2DPlusFrameCuePlacementKind Kind,
			int32 CapturedFrame,
			FGuid CapturedTrackId,
			EPaper2DPlusFrameCuePlacementFailurePoint FailurePoint) override
		{
			FPaper2DPlusFrameCuePlacementAppendResult Result;
			UPaper2DPlusCharacterLayerAsset* Target = LayerAsset.Get();
			UPaper2DPlusCharacterProfileAsset* SourceProfile = Profile.Get();
			const int32 FlipbookIndex = ResolveAnimationIndex();
			const int32 FrameCount = FrameCueProvider_GetFrameCount(SourceProfile, FlipbookIndex);
			const FCharacterLayerAuthoredAnimationData* ExistingAuthored = FindAuthoredAnimation();
			if (!IsValid(Target)
				|| !IsValid(SourceProfile)
				|| !Scope.LayerScope.bLayerScoped
				|| !Scope.LayerScope.LayerId.IsValid()
				|| !Target->GetLayerById(Scope.LayerScope.LayerId)
				|| FlipbookIndex == INDEX_NONE
				|| CapturedFrame < 0
				|| CapturedFrame >= FrameCount
				|| (CapturedTrackId.IsValid()
					&& (!ExistingAuthored
						|| !ExistingAuthored->CueTrackLayout.IsTrackIdValid(CapturedTrackId))))
			{
				return Result;
			}
			if (!Paper2DPlusFrameCuePlacementAuthoring::IsCueClassCompatible(CueClass, Kind))
			{
				Result.Status = EPaper2DPlusFrameCuePlacementAppendStatus::InvalidCueClass;
				return Result;
			}

			const TArray<FCharacterLayer> PreviousLayers = Target->Layers;
			UPackage* Package = Target->GetOutermost();
			const bool bWasPackageDirty = Package && Package->IsDirty();
			const bool bWasTransactional = Target->HasAnyFlags(RF_Transactional);
			FScopedTransaction Transaction(NSLOCTEXT(
				"Paper2DPlusFrameCuePlacement",
				"PlaceLayerFrameCue",
				"Place Frame Cue"));
			if (!Transaction.IsOutstanding())
			{
				return Result;
			}

			UPaper2DPlusCueBase* NewCue = nullptr;
			auto RollBack = [&]()
			{
				Target->Layers = PreviousLayers;
				FrameCueProvider_DiscardRolledBackCue(NewCue);
				Transaction.Cancel();
				FrameCueProvider_RestoreObjectState(
					Target, Package, bWasTransactional, bWasPackageDirty);
				Result.Status = EPaper2DPlusFrameCuePlacementAppendStatus::RolledBack;
				return Result;
			};

			Target->SetFlags(RF_Transactional);
			Target->Modify();
			if (FailurePoint == EPaper2DPlusFrameCuePlacementFailurePoint::AfterTargetModify)
			{
				return RollBack();
			}
			FCharacterLayerAuthoredAnimationData* Authored = EnsureAuthoredAnimation();
			if (!Authored)
			{
				return RollBack();
			}
			if (FailurePoint == EPaper2DPlusFrameCuePlacementFailurePoint::AfterStorageEnsure)
			{
				return RollBack();
			}
			NewCue = Paper2DPlusFrameCuePlacementAuthoring::CreatePlacementFromClassDefaults(
				Target, CueClass, Kind, CapturedFrame, FrameCount);
			if (!NewCue
				|| FailurePoint ==
					EPaper2DPlusFrameCuePlacementFailurePoint::AfterPlacementConstruction)
			{
				return RollBack();
			}
			Authored = FindAuthoredAnimationMutable();
			if (!Authored)
			{
				return RollBack();
			}
			Authored->FrameCues.Add(NewCue);
			if (!Authored->CueTrackLayout.AssignCue(NewCue, CapturedTrackId))
			{
				return RollBack();
			}
			if (FailurePoint == EPaper2DPlusFrameCuePlacementFailurePoint::AfterArrayAppend)
			{
				return RollBack();
			}
			Result.PlacementIdentity =
				FrameCueProvider_MakeCueIdentity(Scope, Authored->FrameCues, NewCue);
			if (!Result.PlacementIdentity.IsValid()
				|| FailurePoint ==
					EPaper2DPlusFrameCuePlacementFailurePoint::AfterIdentityDerivation)
			{
				return RollBack();
			}
			Result.Status = EPaper2DPlusFrameCuePlacementAppendStatus::Success;
			return Result;
		}

	private:
		int32 ResolveAnimationIndex() const
		{
			return Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
				Profile.Get(), Scope.Animation);
		}

		FCharacterLayerAuthoredAnimationData* FindAuthoredAnimationMutable() const
		{
			UPaper2DPlusCharacterLayerAsset* Target = LayerAsset.Get();
			FCharacterLayer* Layer = Target
				? Target->GetLayerByIdMutable(Scope.LayerScope.LayerId)
				: nullptr;
			return Layer ? Layer->AuthoredAnimations.FindByPredicate(
				[this](const FCharacterLayerAuthoredAnimationData& Entry)
				{
					return FrameCueProvider_AnimationMatches(Entry, Scope.Animation);
				}) : nullptr;
		}

		const FCharacterLayerAuthoredAnimationData* FindAuthoredAnimation() const
		{
			const UPaper2DPlusCharacterLayerAsset* Target = LayerAsset.Get();
			const FCharacterLayer* Layer = Target
				? Target->GetLayerById(Scope.LayerScope.LayerId)
				: nullptr;
			return Layer ? Layer->AuthoredAnimations.FindByPredicate(
				[this](const FCharacterLayerAuthoredAnimationData& Entry)
				{
					return FrameCueProvider_AnimationMatches(Entry, Scope.Animation);
				}) : nullptr;
		}

		FCharacterLayerAuthoredAnimationData* EnsureAuthoredAnimation() const
		{
			if (FCharacterLayerAuthoredAnimationData* Existing = FindAuthoredAnimationMutable())
			{
				return Existing;
			}
			UPaper2DPlusCharacterLayerAsset* Target = LayerAsset.Get();
			UPaper2DPlusCharacterProfileAsset* SourceProfile = Profile.Get();
			const int32 FlipbookIndex = ResolveAnimationIndex();
			FCharacterLayer* Layer = Target
				? Target->GetLayerByIdMutable(Scope.LayerScope.LayerId)
				: nullptr;
			if (!Layer || !SourceProfile || !SourceProfile->Flipbooks.IsValidIndex(FlipbookIndex))
			{
				return nullptr;
			}
			FCharacterLayerAuthoredAnimationData& Entry =
				Layer->AuthoredAnimations.AddDefaulted_GetRef();
			Entry.Flipbook = SourceProfile->Flipbooks[FlipbookIndex].Identity.Flipbook;
			Entry.LegacyAnimationName =
				SourceProfile->Flipbooks[FlipbookIndex].Identity.FlipbookName;
			return &Entry;
		}

		TStrongObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
		TStrongObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile;
		FProfileScopedAnimationIdentity Scope;
	};
}

// =================================================================================================
// FFrameCueDataProvider stable helpers
// =================================================================================================

FProfileScopedAnimationIdentity FFrameCueDataProvider::GetScopedAnimationIdentity(
	int32 FlipbookIndex) const
{
	FProfileScopedAnimationIdentity Identity;
	Identity.Animation = GetAnimationIdentity(FlipbookIndex);
	Identity.LayerScope = GetLayerScopeIdentity();
	return Identity;
}

int32 FFrameCueDataProvider::ResolveAnimationIndex(
	const FProfileScopedAnimationIdentity& Identity) const
{
	return Identity.IsValid() && Identity.LayerScope == GetLayerScopeIdentity()
		? ResolveAnimationIndex(Identity.Animation)
		: INDEX_NONE;
}

const TArray<TObjectPtr<UPaper2DPlusCueBase>>* FFrameCueDataProvider::GetCues(
	const FProfileScopedAnimationIdentity& Identity) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetCues(FlipbookIndex) : nullptr;
}

TArray<TObjectPtr<UPaper2DPlusCueBase>>* FFrameCueDataProvider::GetCuesMutable(
	const FProfileScopedAnimationIdentity& Identity)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetCuesMutable(FlipbookIndex) : nullptr;
}

TArray<TObjectPtr<UPaper2DPlusCueBase>>* FFrameCueDataProvider::EnsureCuesMutable(
	const FProfileScopedAnimationIdentity& Identity)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? EnsureCuesMutable(FlipbookIndex) : nullptr;
}

bool FFrameCueDataProvider::CanEnsureCues(const FProfileScopedAnimationIdentity& Identity) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE && CanEnsureCues(FlipbookIndex);
}

const FPaper2DPlusFrameCueTrackLayout* FFrameCueDataProvider::GetTrackLayout(
	const FProfileScopedAnimationIdentity& Identity) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetTrackLayout(FlipbookIndex) : nullptr;
}

FPaper2DPlusFrameCueTrackLayout* FFrameCueDataProvider::GetTrackLayoutMutable(
	const FProfileScopedAnimationIdentity& Identity)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetTrackLayoutMutable(FlipbookIndex) : nullptr;
}

FPaper2DPlusFrameCueTrackLayout* FFrameCueDataProvider::EnsureTrackLayoutMutable(
	const FProfileScopedAnimationIdentity& Identity)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? EnsureTrackLayoutMutable(FlipbookIndex) : nullptr;
}

bool FFrameCueDataProvider::CanEnsureTrackLayout(
	const FProfileScopedAnimationIdentity& Identity) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE && CanEnsureTrackLayout(FlipbookIndex);
}

const FFlipbookCurveData* FFrameCueDataProvider::GetCurveData(
	const FProfileScopedAnimationIdentity& Identity) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetCurveData(FlipbookIndex) : nullptr;
}

FFlipbookCurveData* FFrameCueDataProvider::GetCurveDataMutable(
	const FProfileScopedAnimationIdentity& Identity)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetCurveDataMutable(FlipbookIndex) : nullptr;
}

int32 FFrameCueDataProvider::GetFrameCount(const FProfileScopedAnimationIdentity& Identity) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetFrameCount(FlipbookIndex) : 0;
}

int32 FFrameCueDataProvider::GetMissingCuePlacementCount(
	const FProfileScopedAnimationIdentity& Identity) const
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCues(Identity);
	if (!Cues)
	{
		return 0;
	}

	int32 MissingCount = 0;
	for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : *Cues)
	{
		MissingCount += Cue.Get() == nullptr ? 1 : 0;
	}
	return MissingCount;
}

FFrameCueStableIdentity FFrameCueDataProvider::GetCueIdentity(
	int32 FlipbookIndex,
	const UPaper2DPlusCueBase* Cue) const
{
	const FProfileScopedAnimationIdentity Scope = GetScopedAnimationIdentity(FlipbookIndex);
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCues(FlipbookIndex);
	if (!Cue || !Scope.IsValid() || !Cues)
	{
		return {};
	}
	return FrameCueProvider_MakeCueIdentity(
		Scope, *Cues, const_cast<UPaper2DPlusCueBase*>(Cue));
}

FFrameCueStableIdentity FFrameCueDataProvider::GetCueIdentity(
	const FProfileScopedAnimationIdentity& ScopeIdentity,
	const UPaper2DPlusCueBase* Cue) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(ScopeIdentity);
	return FlipbookIndex != INDEX_NONE ? GetCueIdentity(FlipbookIndex, Cue) : FFrameCueStableIdentity();
}

int32 FFrameCueDataProvider::ResolveCueIndex(const FFrameCueStableIdentity& Identity) const
{
	if (!Identity.IsValid())
	{
		return INDEX_NONE;
	}
	// A menu/Details callback opened for one layer must never fall through to an identical-looking cue on
	// a newly selected layer. The host rebuilds/rebinds after scope change; the stale action simply expires.
	if (Identity.Scope.LayerScope != GetLayerScopeIdentity())
	{
		return INDEX_NONE;
	}
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCues(Identity.Scope);
	if (!Cues)
	{
		return INDEX_NONE;
	}

	if (UPaper2DPlusCueBase* Object = Identity.Object.Get())
	{
		const int32 ObjectIndex = Cues->IndexOfByKey(Object);
		if (ObjectIndex != INDEX_NONE)
		{
			return ObjectIndex;
		}
	}

	if (Identity.MatchingOccurrence == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	int32 MatchingOccurrence = 0;
	for (int32 CueIndex = 0; CueIndex < Cues->Num(); ++CueIndex)
	{
		if (FrameCueProvider_MatchesSignature(Identity, (*Cues)[CueIndex].Get()))
		{
			if (MatchingOccurrence == Identity.MatchingOccurrence)
			{
				return CueIndex;
			}
			++MatchingOccurrence;
		}
	}
	return INDEX_NONE;
}

UPaper2DPlusCueBase* FFrameCueDataProvider::ResolveCue(const FFrameCueStableIdentity& Identity) const
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCues(Identity.Scope);
	const int32 CueIndex = ResolveCueIndex(Identity);
	return Cues && Cues->IsValidIndex(CueIndex) ? (*Cues)[CueIndex].Get() : nullptr;
}

void FFrameCueDataProvider::GatherCueCollections(
	int32 FlipbookIndex,
	TArray<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*>& OutCollections) const
{
	OutCollections.Reset();
	if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCues(FlipbookIndex))
	{
		OutCollections.Add(Cues);
	}
}

void FFrameCueDataProvider::GatherCueCollectionsMutable(
	int32 FlipbookIndex,
	TArray<TArray<TObjectPtr<UPaper2DPlusCueBase>>*>& OutCollections)
{
	OutCollections.Reset();
	if (TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCuesMutable(FlipbookIndex))
	{
		OutCollections.Add(Cues);
	}
}

void FFrameCueDataProvider::RetainValidCueAssignments(
	const FProfileScopedAnimationIdentity& Identity,
	FPaper2DPlusFrameCueTrackLayout& Layout) const
{
	if (Layout.CueTrackIds.Num() == 0)
	{
		return;
	}

	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	TArray<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*> Collections;
	GatherCueCollections(FlipbookIndex, Collections);
	TSet<const UPaper2DPlusCueBase*> ValidCues;
	for (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Collection : Collections)
	{
		if (!Collection)
		{
			continue;
		}
		for (const UPaper2DPlusCueBase* Cue : *Collection)
		{
			if (Cue)
			{
				ValidCues.Add(Cue);
			}
		}
	}
	Layout.RetainCueAssignments(ValidCues);
}

TArray<FPaper2DPlusFrameCueTrackDefinition> FFrameCueDataProvider::GetOptionalTracks(
	const FProfileScopedAnimationIdentity& Identity) const
{
	if (const FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayout(Identity))
	{
		return Layout->OptionalTracks;
	}
	return {};
}

FString FFrameCueDataProvider::SuggestTrackName(
	const FProfileScopedAnimationIdentity& Identity) const
{
	if (const FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayout(Identity))
	{
		return Layout->SuggestTrackName();
	}
	FPaper2DPlusFrameCueTrackLayout EmptyLayout;
	return EmptyLayout.SuggestTrackName();
}

FGuid FFrameCueDataProvider::ResolveCueTrackId(
	const FProfileScopedAnimationIdentity& Identity,
	const UPaper2DPlusCueBase* Cue) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	const FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayout(Identity);
	if (FlipbookIndex == INDEX_NONE || !Layout || !Cue || Cue->GetOuter() != GetCueOuter())
	{
		return FGuid();
	}
	const FGuid TrackId = Layout->ResolveStoredTrackId(Cue);
	if (!TrackId.IsValid())
	{
		return FGuid();
	}

	TArray<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*> Collections;
	GatherCueCollections(FlipbookIndex, Collections);
	int32 Occurrences = 0;
	for (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Collection : Collections)
	{
		if (!Collection)
		{
			continue;
		}
		for (const UPaper2DPlusCueBase* Candidate : *Collection)
		{
			Occurrences += Candidate == Cue ? 1 : 0;
		}
	}
	return Occurrences == 1 ? TrackId : FGuid();
}

FGuid FFrameCueDataProvider::AddTrack(
	const FProfileScopedAnimationIdentity& Identity,
	const FString& DisplayName,
	const FGuid& RequestedTrackId)
{
	if (!CanEnsureTrackLayout(Identity))
	{
		return FGuid();
	}
	FPaper2DPlusFrameCueTrackLayout Candidate = GetTrackLayout(Identity)
		? *GetTrackLayout(Identity)
		: FPaper2DPlusFrameCueTrackLayout();
	FGuid CandidateId;
	if (!Candidate.AddTrack(DisplayName, RequestedTrackId, CandidateId))
	{
		return FGuid();
	}

	UObject* Target = GetTransactionTarget();
	if (!IsValid(Target))
	{
		return FGuid();
	}
	UPackage* Package = Target->GetOutermost();
	const bool bWasPackageDirty = Package && Package->IsDirty();
	const bool bWasTransactional = Target->HasAnyFlags(RF_Transactional);
	FScopedTransaction Transaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTracks", "AddTrack", "Add Frame Cue Track"));
	if (!Transaction.IsOutstanding())
	{
		return FGuid();
	}
	Target->SetFlags(RF_Transactional);
	Target->Modify();
	FPaper2DPlusFrameCueTrackLayout* Layout = EnsureTrackLayoutMutable(Identity);
	FGuid AddedId;
	if (!Layout || !Layout->AddTrack(DisplayName, CandidateId, AddedId))
	{
		Transaction.Cancel();
		FrameCueProvider_RestoreObjectState(
			Target, Package, bWasTransactional, bWasPackageDirty);
		return FGuid();
	}
	return AddedId;
}

bool FFrameCueDataProvider::RenameTrack(
	const FProfileScopedAnimationIdentity& Identity,
	const FGuid& TrackId,
	const FString& DisplayName)
{
	const FPaper2DPlusFrameCueTrackLayout* Existing = GetTrackLayout(Identity);
	if (!Existing || !Existing->IsTrackIdValid(TrackId)
		|| !Existing->IsTrackNameAvailable(DisplayName, TrackId))
	{
		return false;
	}
	UObject* Target = GetTransactionTarget();
	if (!IsValid(Target))
	{
		return false;
	}
	FScopedTransaction Transaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTracks", "RenameTrack", "Rename Frame Cue Track"));
	if (!Transaction.IsOutstanding())
	{
		return false;
	}
	Target->SetFlags(RF_Transactional);
	Target->Modify();
	FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayoutMutable(Identity);
	if (!Layout || !Layout->RenameTrack(TrackId, DisplayName))
	{
		Transaction.Cancel();
		return false;
	}
	return true;
}

bool FFrameCueDataProvider::ReorderTrack(
	const FProfileScopedAnimationIdentity& Identity,
	const FGuid& TrackId,
	int32 NewOptionalTrackIndex)
{
	const FPaper2DPlusFrameCueTrackLayout* Existing = GetTrackLayout(Identity);
	if (!Existing || !Existing->IsTrackIdValid(TrackId)
		|| !Existing->OptionalTracks.IsValidIndex(NewOptionalTrackIndex))
	{
		return false;
	}
	UObject* Target = GetTransactionTarget();
	if (!IsValid(Target))
	{
		return false;
	}
	FScopedTransaction Transaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTracks", "ReorderTrack", "Reorder Frame Cue Track"));
	if (!Transaction.IsOutstanding())
	{
		return false;
	}
	Target->SetFlags(RF_Transactional);
	Target->Modify();
	FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayoutMutable(Identity);
	if (!Layout || !Layout->ReorderTrack(TrackId, NewOptionalTrackIndex))
	{
		Transaction.Cancel();
		return false;
	}
	return true;
}

FFrameCueTrackUsage FFrameCueDataProvider::GetTrackUsage(
	const FProfileScopedAnimationIdentity& Identity,
	const FGuid& TrackId) const
{
	FFrameCueTrackUsage Usage;
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	const FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayout(Identity);
	if (FlipbookIndex == INDEX_NONE || !Layout || !Layout->IsTrackIdValid(TrackId))
	{
		return Usage;
	}

	TArray<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*> Collections;
	GatherCueCollections(FlipbookIndex, Collections);
	TMap<const UPaper2DPlusCueBase*, int32> Occurrences;
	for (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Collection : Collections)
	{
		if (!Collection) continue;
		for (const UPaper2DPlusCueBase* Cue : *Collection)
		{
			if (Cue) ++Occurrences.FindOrAdd(Cue);
		}
	}
	for (int32 CollectionIndex = 0; CollectionIndex < Collections.Num(); ++CollectionIndex)
	{
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Collection = Collections[CollectionIndex];
		if (!Collection) continue;
		for (const UPaper2DPlusCueBase* Cue : *Collection)
		{
			if (Cue
				&& Cue->GetOuter() == GetCueOuter()
				&& Occurrences.FindRef(Cue) == 1
				&& Layout->ResolveStoredTrackId(Cue) == TrackId)
			{
				if (CollectionIndex == 0) ++Usage.ActiveCueCount;
				else ++Usage.StashedCueCount;
			}
		}
	}
	return Usage;
}

bool FFrameCueDataProvider::RemoveTrack(
	const FProfileScopedAnimationIdentity& Identity,
	const FGuid& TrackId,
	EPaper2DPlusFrameCueTrackRemovalMode RemovalMode)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	const FPaper2DPlusFrameCueTrackLayout* Existing = GetTrackLayout(Identity);
	if (FlipbookIndex == INDEX_NONE || !Existing || !Existing->IsTrackIdValid(TrackId))
	{
		return false;
	}
	UObject* Target = GetTransactionTarget();
	if (!IsValid(Target))
	{
		return false;
	}
	FScopedTransaction Transaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTracks", "RemoveTrack", "Remove Frame Cue Track"));
	if (!Transaction.IsOutstanding())
	{
		return false;
	}
	Target->SetFlags(RF_Transactional);
	Target->Modify();
	FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayoutMutable(Identity);
	if (!Layout || !Layout->IsTrackIdValid(TrackId))
	{
		Transaction.Cancel();
		return false;
	}

	if (RemovalMode == EPaper2DPlusFrameCueTrackRemovalMode::DeleteCues)
	{
		TArray<TArray<TObjectPtr<UPaper2DPlusCueBase>>*> Collections;
		GatherCueCollectionsMutable(FlipbookIndex, Collections);
		for (TArray<TObjectPtr<UPaper2DPlusCueBase>>* Collection : Collections)
		{
			if (!Collection) continue;
			for (int32 CueIndex = Collection->Num() - 1; CueIndex >= 0; --CueIndex)
			{
				UPaper2DPlusCueBase* Cue = (*Collection)[CueIndex].Get();
				if (Cue && Layout->FindStoredTrackId(Cue) == TrackId)
				{
					Layout->RemoveCue(Cue);
					Collection->RemoveAt(CueIndex);
				}
			}
		}
	}
	Layout->RemoveAssignmentsForTrack(TrackId);
	if (!Layout->RemoveTrackDefinition(TrackId))
	{
		Transaction.Cancel();
		return false;
	}
	return true;
}

bool FFrameCueDataProvider::AssignCueToTrack(
	const FFrameCueStableIdentity& CueIdentity,
	const FGuid& TrackId)
{
	UPaper2DPlusCueBase* Cue = ResolveCue(CueIdentity);
	const FPaper2DPlusFrameCueTrackLayout* Existing = GetTrackLayout(CueIdentity.Scope);
	if (!Cue || (TrackId.IsValid() && (!Existing || !Existing->IsTrackIdValid(TrackId))))
	{
		return false;
	}
	UObject* Target = GetTransactionTarget();
	if (!IsValid(Target))
	{
		return false;
	}
	FScopedTransaction Transaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTracks", "AssignCue", "Move Frame Cue to Track"));
	if (!Transaction.IsOutstanding())
	{
		return false;
	}
	Target->SetFlags(RF_Transactional);
	Target->Modify();
	Cue = ResolveCue(CueIdentity);
	FPaper2DPlusFrameCueTrackLayout* Layout = EnsureTrackLayoutMutable(CueIdentity.Scope);
	if (!Cue || !Layout || !Layout->AssignCue(Cue, TrackId))
	{
		Transaction.Cancel();
		return false;
	}
	return true;
}

FFrameCueStableIdentity FFrameCueDataProvider::DuplicateCue(
	const FFrameCueStableIdentity& SourceIdentity)
{
	UPaper2DPlusCueBase* Source = ResolveCue(SourceIdentity);
	UObject* Target = GetTransactionTarget();
	UObject* CueOuter = GetCueOuter();
	if (!Source || !IsValid(Target) || !IsValid(CueOuter))
	{
		return {};
	}
	const FGuid SourceTrackId = ResolveCueTrackId(SourceIdentity.Scope, Source);
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* ExistingCues = GetCuesMutable(SourceIdentity.Scope);
	FPaper2DPlusFrameCueTrackLayout* ExistingLayout = GetTrackLayoutMutable(SourceIdentity.Scope);
	if (!ExistingCues || !ExistingLayout)
	{
		return {};
	}
	const TArray<TObjectPtr<UPaper2DPlusCueBase>> PreviousCues = *ExistingCues;
	const FPaper2DPlusFrameCueTrackLayout PreviousLayout = *ExistingLayout;
	UPackage* Package = Target->GetOutermost();
	const bool bWasPackageDirty = Package && Package->IsDirty();
	const bool bWasTransactional = Target->HasAnyFlags(RF_Transactional);
	FScopedTransaction Transaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTracks", "DuplicateCue", "Duplicate Frame Cue"));
	if (!Transaction.IsOutstanding())
	{
		return {};
	}
	Target->SetFlags(RF_Transactional);
	Target->Modify();
	Source = ResolveCue(SourceIdentity);
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCuesMutable(SourceIdentity.Scope);
	FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayoutMutable(SourceIdentity.Scope);
	UPaper2DPlusCueBase* Duplicate = Source
		? DuplicateObject<UPaper2DPlusCueBase>(Source, CueOuter)
		: nullptr;
	auto RollBack = [&]() -> FFrameCueStableIdentity
	{
		if (TArray<TObjectPtr<UPaper2DPlusCueBase>>* RestoreCues =
			GetCuesMutable(SourceIdentity.Scope))
		{
			*RestoreCues = PreviousCues;
		}
		if (FPaper2DPlusFrameCueTrackLayout* RestoreLayout =
			GetTrackLayoutMutable(SourceIdentity.Scope))
		{
			*RestoreLayout = PreviousLayout;
		}
		FrameCueProvider_DiscardRolledBackCue(Duplicate);
		Transaction.Cancel();
		FrameCueProvider_RestoreObjectState(
			Target, Package, bWasTransactional, bWasPackageDirty);
		return {};
	};
	if (!Source || !Cues || !Layout || !Duplicate)
	{
		return RollBack();
	}
	Duplicate->SetFlags(RF_Transactional);
	Cues->Add(Duplicate);
	if (!Layout->AssignCue(Duplicate, SourceTrackId))
	{
		return RollBack();
	}
	const FFrameCueStableIdentity Result = GetCueIdentity(SourceIdentity.Scope, Duplicate);
	return Result.IsValid() ? Result : RollBack();
}

bool FFrameCueDataProvider::DeleteCue(const FFrameCueStableIdentity& CueIdentity)
{
	UPaper2DPlusCueBase* Cue = ResolveCue(CueIdentity);
	UObject* Target = GetTransactionTarget();
	if (!Cue || !IsValid(Target))
	{
		return false;
	}
	FScopedTransaction Transaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTracks", "DeleteCue", "Delete Frame Cue"));
	if (!Transaction.IsOutstanding())
	{
		return false;
	}
	Target->SetFlags(RF_Transactional);
	Target->Modify();
	Cue = ResolveCue(CueIdentity);
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCuesMutable(CueIdentity.Scope);
	FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayoutMutable(CueIdentity.Scope);
	const int32 CueIndex = ResolveCueIndex(CueIdentity);
	if (!Cue || !Cues || !Layout || !Cues->IsValidIndex(CueIndex)
		|| (*Cues)[CueIndex] != Cue)
	{
		Transaction.Cancel();
		return false;
	}
	Layout->RemoveCue(Cue);
	Cues->RemoveAt(CueIndex);
	return true;
}

int32 FFrameCueDataProvider::RemoveAllMissingCuePlacements(
	const FProfileScopedAnimationIdentity& Identity)
{
	if (GetMissingCuePlacementCount(Identity) == 0)
	{
		return 0;
	}

	UObject* Target = GetTransactionTarget();
	if (!IsValid(Target))
	{
		return 0;
	}
	UPackage* Package = Target->GetOutermost();
	const bool bWasPackageDirty = Package && Package->IsDirty();
	const bool bWasTransactional = Target->HasAnyFlags(RF_Transactional);
	FScopedTransaction Transaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTracks",
		"RemoveMissingCues",
		"Remove Missing Frame Cue Placements"));
	if (!Transaction.IsOutstanding())
	{
		return 0;
	}

	Target->SetFlags(RF_Transactional);
	Target->Modify();
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCuesMutable(Identity);
	if (!Cues)
	{
		Transaction.Cancel();
		FrameCueProvider_RestoreObjectState(
			Target, Package, bWasTransactional, bWasPackageDirty);
		return 0;
	}

	int32 RemovedCount = 0;
	for (int32 CueIndex = Cues->Num() - 1; CueIndex >= 0; --CueIndex)
	{
		if ((*Cues)[CueIndex].Get() == nullptr)
		{
			Cues->RemoveAt(CueIndex);
			++RemovedCount;
		}
	}
	if (RemovedCount == 0)
	{
		Transaction.Cancel();
		FrameCueProvider_RestoreObjectState(
			Target, Package, bWasTransactional, bWasPackageDirty);
		return 0;
	}

	// A deleted Cue Type can also leave a null object key in the editor-only track sidecar.
	// Retain every live active or stashed Cue assignment while pruning only keys that no longer
	// identify a placement in this animation.
	if (FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayoutMutable(Identity))
	{
		RetainValidCueAssignments(Identity, *Layout);
	}
	return RemovedCount;
}

bool FFrameCueDataProvider::ReplaceCueArrayAtomically(
	const FProfileScopedAnimationIdentity& Identity,
	TArray<TObjectPtr<UPaper2DPlusCueBase>>&& ReplacementCues,
	const FPaper2DPlusFrameCueReplacementMap& Replacements)
{
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* ExistingCues = GetCuesMutable(Identity);
	FPaper2DPlusFrameCueTrackLayout* ExistingLayout = GetTrackLayoutMutable(Identity);
	UObject* Target = GetTransactionTarget();
	UObject* CueOuter = GetCueOuter();
	if (!ExistingCues || !ExistingLayout || !IsValid(Target) || !IsValid(CueOuter))
	{
		return false;
	}
	for (const UPaper2DPlusCueBase* Cue : ReplacementCues)
	{
		if (Cue && Cue->GetOuter() != CueOuter)
		{
			return false;
		}
	}
	FScopedTransaction Transaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTracks", "ReplaceCueArray", "Replace Frame Cue Placements"));
	if (!Transaction.IsOutstanding())
	{
		return false;
	}
	Target->SetFlags(RF_Transactional);
	Target->Modify();
	TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = GetCuesMutable(Identity);
	FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayoutMutable(Identity);
	if (!Cues || !Layout)
	{
		Transaction.Cancel();
		return false;
	}
	*Cues = MoveTemp(ReplacementCues);
	Layout->RemapCueReferences(Replacements);
	RetainValidCueAssignments(Identity, *Layout);
	return true;
}

// =================================================================================================
// FProfileFrameCueDataProvider
// =================================================================================================

FProfileFrameCueDataProvider::FProfileFrameCueDataProvider(
	TSharedPtr<FCharacterProfileEditorModel> InModel)
	: ModelWeak(InModel)
{
}

UPaper2DPlusCharacterProfileAsset* FProfileFrameCueDataProvider::ResolveProfile() const
{
	const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	return Model.IsValid() ? Model->GetAsset() : nullptr;
}

bool FProfileFrameCueDataProvider::UsesCharacterBaseline() const
{
	const UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	return Profile && Profile->LayerBakeOwnerToken.IsValid();
}

const FPaper2DPlusCharacterBaselineAnimation* FProfileFrameCueDataProvider::ResolveBaseline(
	int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!UsesCharacterBaseline() || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	const FFlipbookIdentity& Identity = Profile->Flipbooks[FlipbookIndex].Identity;
	return Profile->FindCharacterBaseline(Identity.Flipbook.ToSoftObjectPath(), Identity.FlipbookName);
}

FPaper2DPlusCharacterBaselineAnimation* FProfileFrameCueDataProvider::ResolveBaselineMutable(
	int32 FlipbookIndex)
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!UsesCharacterBaseline() || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	const FFlipbookIdentity& Identity = Profile->Flipbooks[FlipbookIndex].Identity;
	return Profile->FindCharacterBaselineMutable(Identity.Flipbook.ToSoftObjectPath(), Identity.FlipbookName);
}

FProfileAnimationIdentity FProfileFrameCueDataProvider::GetAnimationIdentity(int32 FlipbookIndex) const
{
	return Paper2DPlusProfileToolProvider::MakeAnimationIdentity(ResolveProfile(), FlipbookIndex);
}

int32 FProfileFrameCueDataProvider::ResolveAnimationIndex(
	const FProfileAnimationIdentity& Identity) const
{
	return Paper2DPlusProfileToolProvider::ResolveAnimationIndex(ResolveProfile(), Identity);
}

const TArray<TObjectPtr<UPaper2DPlusCueBase>>* FProfileFrameCueDataProvider::GetCues(
	int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	if (const FPaper2DPlusCharacterBaselineAnimation* Baseline = ResolveBaseline(FlipbookIndex))
	{
		return &Baseline->FrameCues;
	}
	return UsesCharacterBaseline() ? nullptr : &Profile->Flipbooks[FlipbookIndex].FrameEventData.FrameCues;
}

TArray<TObjectPtr<UPaper2DPlusCueBase>>* FProfileFrameCueDataProvider::GetCuesMutable(
	int32 FlipbookIndex)
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	if (FPaper2DPlusCharacterBaselineAnimation* Baseline = ResolveBaselineMutable(FlipbookIndex))
	{
		return &Baseline->FrameCues;
	}
	return UsesCharacterBaseline() ? nullptr : &Profile->Flipbooks[FlipbookIndex].FrameEventData.FrameCues;
}

TArray<TObjectPtr<UPaper2DPlusCueBase>>* FProfileFrameCueDataProvider::EnsureCuesMutable(
	int32 FlipbookIndex)
{
	// The Profile row already owns the authoritative array. Keeping this a direct resolve preserves the
	// pre-provider profile path byte-for-byte; there is no synthetic row or migration on first add.
	return GetCuesMutable(FlipbookIndex);
}

bool FProfileFrameCueDataProvider::CanEnsureCues(int32 FlipbookIndex) const
{
	// Character Profile authoring historically allowed a placement on an entry whose flipbook/frame
	// count was temporarily unavailable; CreatePlacement clamps that structurally to frame zero. Keep
	// that byte-compatible behavior. Layer first-edit remains stricter because it would create a new row.
	return GetCues(FlipbookIndex) != nullptr;
}

const FPaper2DPlusFrameCueTrackLayout* FProfileFrameCueDataProvider::GetTrackLayout(
	int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	if (const FPaper2DPlusCharacterBaselineAnimation* Baseline = ResolveBaseline(FlipbookIndex))
	{
		return &Baseline->CueTrackLayout;
	}
	return UsesCharacterBaseline()
		? nullptr
		: &Profile->Flipbooks[FlipbookIndex].FrameEventData.CueTrackLayout;
}

FPaper2DPlusFrameCueTrackLayout* FProfileFrameCueDataProvider::GetTrackLayoutMutable(
	int32 FlipbookIndex)
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	if (FPaper2DPlusCharacterBaselineAnimation* Baseline = ResolveBaselineMutable(FlipbookIndex))
	{
		return &Baseline->CueTrackLayout;
	}
	return UsesCharacterBaseline()
		? nullptr
		: &Profile->Flipbooks[FlipbookIndex].FrameEventData.CueTrackLayout;
}

FPaper2DPlusFrameCueTrackLayout* FProfileFrameCueDataProvider::EnsureTrackLayoutMutable(
	int32 FlipbookIndex)
{
	return GetTrackLayoutMutable(FlipbookIndex);
}

bool FProfileFrameCueDataProvider::CanEnsureTrackLayout(int32 FlipbookIndex) const
{
	return GetTrackLayout(FlipbookIndex) != nullptr;
}

void FProfileFrameCueDataProvider::GatherCueCollections(
	int32 FlipbookIndex,
	TArray<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*>& OutCollections) const
{
	FFrameCueDataProvider::GatherCueCollections(FlipbookIndex, OutCollections);
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (UsesCharacterBaseline() || !Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}
	for (const FExcludedFlipbookFrameData& Excluded :
		Profile->Flipbooks[FlipbookIndex].CombatData.ExcludedFrames)
	{
		OutCollections.Add(&Excluded.StashedFrameCues);
	}
}

void FProfileFrameCueDataProvider::GatherCueCollectionsMutable(
	int32 FlipbookIndex,
	TArray<TArray<TObjectPtr<UPaper2DPlusCueBase>>*>& OutCollections)
{
	FFrameCueDataProvider::GatherCueCollectionsMutable(FlipbookIndex, OutCollections);
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (UsesCharacterBaseline() || !Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}
	for (FExcludedFlipbookFrameData& Excluded :
		Profile->Flipbooks[FlipbookIndex].CombatData.ExcludedFrames)
	{
		OutCollections.Add(&Excluded.StashedFrameCues);
	}
}

const FFlipbookCurveData* FProfileFrameCueDataProvider::GetCurveData(int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	return Profile && Profile->Flipbooks.IsValidIndex(FlipbookIndex)
		? &Profile->Flipbooks[FlipbookIndex].CurveData
		: nullptr;
}

FFlipbookCurveData* FProfileFrameCueDataProvider::GetCurveDataMutable(int32 FlipbookIndex)
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	return Profile && Profile->Flipbooks.IsValidIndex(FlipbookIndex)
		? &Profile->Flipbooks[FlipbookIndex].CurveData
		: nullptr;
}

int32 FProfileFrameCueDataProvider::GetFrameCount(int32 FlipbookIndex) const
{
	return FrameCueProvider_GetFrameCount(ResolveProfile(), FlipbookIndex);
}

UObject* FProfileFrameCueDataProvider::GetTransactionTarget() const
{
	return ResolveProfile();
}

UObject* FProfileFrameCueDataProvider::GetCueOuter() const
{
	return ResolveProfile();
}

bool FProfileFrameCueDataProvider::HasResolvedScope() const
{
	return ResolveProfile() != nullptr;
}

TSharedPtr<FPaper2DPlusFrameCuePlacementProviderSnapshot>
FProfileFrameCueDataProvider::CapturePlacementSnapshot(
	const FProfileScopedAnimationIdentity& Identity) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Profile
		|| Identity.LayerScope != FProfileLayerScopeIdentity::Profile()
		|| ResolveAnimationIndex(Identity) == INDEX_NONE
		|| !CanEnsureCues(Identity))
	{
		return nullptr;
	}
	return MakeShared<FProfileFrameCuePlacementProviderSnapshot>(
		Profile, Identity, UsesCharacterBaseline());
}

const TArray<TObjectPtr<UPaper2DPlusCueBase>>* FProfileFrameCueDataProvider::GetFinalCues(
	int32 FlipbookIndex) const
{
	const UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	return UsesCharacterBaseline() && Profile && Profile->Flipbooks.IsValidIndex(FlipbookIndex)
		? &Profile->Flipbooks[FlipbookIndex].FrameEventData.FrameCues
		: nullptr;
}

// =================================================================================================
// FLayerFrameCueDataProvider
// =================================================================================================

FLayerFrameCueDataProvider::FLayerFrameCueDataProvider(
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> InLayerAsset,
	TSharedPtr<FCharacterProfileEditorModel> InModel)
	: LayerAsset(InLayerAsset)
	, ModelWeak(InModel)
{
}

UPaper2DPlusCharacterProfileAsset* FLayerFrameCueDataProvider::ResolveProfile() const
{
	const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	return Model.IsValid() ? Model->GetAsset() : nullptr;
}

FProfileAnimationIdentity FLayerFrameCueDataProvider::GetAnimationIdentity(int32 FlipbookIndex) const
{
	return Paper2DPlusProfileToolProvider::MakeAnimationIdentity(ResolveProfile(), FlipbookIndex);
}

int32 FLayerFrameCueDataProvider::ResolveAnimationIndex(
	const FProfileAnimationIdentity& Identity) const
{
	return Paper2DPlusProfileToolProvider::ResolveAnimationIndex(ResolveProfile(), Identity);
}

FProfileLayerScopeIdentity FLayerFrameCueDataProvider::GetLayerScopeIdentity() const
{
	const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	return Model.IsValid()
		? FProfileLayerScopeIdentity::Layer(Model->GetSelectedLayerId())
		: FProfileLayerScopeIdentity::Layer(FGuid());
}

const FCharacterLayer* FLayerFrameCueDataProvider::ResolveScopedLayer() const
{
	const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	return Model.IsValid() && LayerAsset.IsValid()
		? LayerAsset->GetLayerById(Model->GetSelectedLayerId()) : nullptr;
}

FCharacterLayer* FLayerFrameCueDataProvider::ResolveScopedLayerMutable()
{
	const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	return Model.IsValid() && LayerAsset.IsValid()
		? LayerAsset->GetLayerByIdMutable(Model->GetSelectedLayerId()) : nullptr;
}

const FCharacterLayerAuthoredAnimationData* FLayerFrameCueDataProvider::FindAuthoredAnimation(
	int32 FlipbookIndex) const
{
	const FCharacterLayer* Layer = ResolveScopedLayer();
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Layer || !Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return nullptr;
	}
	const FProfileAnimationIdentity Identity = GetAnimationIdentity(FlipbookIndex);
	return Layer->AuthoredAnimations.FindByPredicate(
		[&Identity](const FCharacterLayerAuthoredAnimationData& Entry)
		{
			return FrameCueProvider_AnimationMatches(Entry, Identity);
		});
}

FCharacterLayerAuthoredAnimationData* FLayerFrameCueDataProvider::FindAuthoredAnimationMutable(
	int32 FlipbookIndex)
{
	FCharacterLayer* Layer = ResolveScopedLayerMutable();
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Layer || !Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return nullptr;
	}
	const FProfileAnimationIdentity Identity = GetAnimationIdentity(FlipbookIndex);
	return Layer->AuthoredAnimations.FindByPredicate(
		[&Identity](const FCharacterLayerAuthoredAnimationData& Entry)
		{
			return FrameCueProvider_AnimationMatches(Entry, Identity);
		});
}

FCharacterLayerAuthoredAnimationData* FLayerFrameCueDataProvider::EnsureAuthoredAnimationMutable(
	int32 FlipbookIndex,
	bool bRequirePositiveFrameCount)
{
	FCharacterLayer* Layer = ResolveScopedLayerMutable();
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Layer || !Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)
		|| (bRequirePositiveFrameCount && GetFrameCount(FlipbookIndex) <= 0))
	{
		return nullptr;
	}
	if (FCharacterLayerAuthoredAnimationData* Existing = FindAuthoredAnimationMutable(FlipbookIndex))
	{
		return Existing;
	}
	FCharacterLayerAuthoredAnimationData& Entry = Layer->AuthoredAnimations.AddDefaulted_GetRef();
	Entry.Flipbook = Profile->Flipbooks[FlipbookIndex].Identity.Flipbook;
	Entry.LegacyAnimationName = Profile->Flipbooks[FlipbookIndex].Identity.FlipbookName;
	return &Entry;
}

const TArray<TObjectPtr<UPaper2DPlusCueBase>>* FLayerFrameCueDataProvider::GetCues(
	int32 FlipbookIndex) const
{
	const FCharacterLayerAuthoredAnimationData* Entry = FindAuthoredAnimation(FlipbookIndex);
	return Entry ? &Entry->FrameCues : nullptr;
}

TArray<TObjectPtr<UPaper2DPlusCueBase>>* FLayerFrameCueDataProvider::GetCuesMutable(
	int32 FlipbookIndex)
{
	FCharacterLayerAuthoredAnimationData* Entry = FindAuthoredAnimationMutable(FlipbookIndex);
	return Entry ? &Entry->FrameCues : nullptr;
}

TArray<TObjectPtr<UPaper2DPlusCueBase>>* FLayerFrameCueDataProvider::EnsureCuesMutable(
	int32 FlipbookIndex)
{
	FCharacterLayerAuthoredAnimationData* Entry = EnsureAuthoredAnimationMutable(FlipbookIndex);
	return Entry ? &Entry->FrameCues : nullptr;
}

bool FLayerFrameCueDataProvider::CanEnsureCues(int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	return ResolveScopedLayer()
		&& Profile
		&& Profile->Flipbooks.IsValidIndex(FlipbookIndex)
		&& GetFrameCount(FlipbookIndex) > 0;
}

const FPaper2DPlusFrameCueTrackLayout* FLayerFrameCueDataProvider::GetTrackLayout(
	int32 FlipbookIndex) const
{
	const FCharacterLayerAuthoredAnimationData* Entry = FindAuthoredAnimation(FlipbookIndex);
	return Entry ? &Entry->CueTrackLayout : nullptr;
}

FPaper2DPlusFrameCueTrackLayout* FLayerFrameCueDataProvider::GetTrackLayoutMutable(
	int32 FlipbookIndex)
{
	FCharacterLayerAuthoredAnimationData* Entry = FindAuthoredAnimationMutable(FlipbookIndex);
	return Entry ? &Entry->CueTrackLayout : nullptr;
}

FPaper2DPlusFrameCueTrackLayout* FLayerFrameCueDataProvider::EnsureTrackLayoutMutable(
	int32 FlipbookIndex)
{
	FCharacterLayerAuthoredAnimationData* Entry =
		EnsureAuthoredAnimationMutable(FlipbookIndex, false);
	return Entry ? &Entry->CueTrackLayout : nullptr;
}

bool FLayerFrameCueDataProvider::CanEnsureTrackLayout(int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	return ResolveScopedLayer()
		&& Profile
		&& Profile->Flipbooks.IsValidIndex(FlipbookIndex);
}

const FFlipbookCurveData* FLayerFrameCueDataProvider::GetCurveData(int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	return Profile && Profile->Flipbooks.IsValidIndex(FlipbookIndex)
		? &Profile->Flipbooks[FlipbookIndex].CurveData
		: nullptr;
}

FFlipbookCurveData* FLayerFrameCueDataProvider::GetCurveDataMutable(int32 /*FlipbookIndex*/)
{
	return nullptr;
}

int32 FLayerFrameCueDataProvider::GetFrameCount(int32 FlipbookIndex) const
{
	return FrameCueProvider_GetFrameCount(ResolveProfile(), FlipbookIndex);
}

UObject* FLayerFrameCueDataProvider::GetTransactionTarget() const
{
	return LayerAsset.Get();
}

UObject* FLayerFrameCueDataProvider::GetCueOuter() const
{
	return LayerAsset.Get();
}

bool FLayerFrameCueDataProvider::HasResolvedScope() const
{
	return ResolveScopedLayer() != nullptr;
}

TSharedPtr<FPaper2DPlusFrameCuePlacementProviderSnapshot>
FLayerFrameCueDataProvider::CapturePlacementSnapshot(
	const FProfileScopedAnimationIdentity& Identity) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Profile
		|| !Asset
		|| Identity.LayerScope != GetLayerScopeIdentity()
		|| !Identity.LayerScope.LayerId.IsValid()
		|| ResolveAnimationIndex(Identity) == INDEX_NONE
		|| !CanEnsureCues(Identity))
	{
		return nullptr;
	}
	return MakeShared<FLayerFrameCuePlacementProviderSnapshot>(Asset, Profile, Identity);
}

FText FLayerFrameCueDataProvider::GetScopeDisplayText() const
{
	const FCharacterLayer* Layer = ResolveScopedLayer();
	return Layer ? FText::FromString(Layer->LayerName) : FText::GetEmpty();
}

void FLayerFrameCueDataProvider::RebuildCueProjections(int32 FlipbookIndex) const
{
	const FProfileScopedAnimationIdentity Identity = GetScopedAnimationIdentity(FlipbookIndex);
	ProjectionIdentity = Identity;
	LowerCueProjection.Reset();
	FinalCueProjection.Reset();
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	const UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	const FCharacterLayer* Selected = ResolveScopedLayer();
	if (!Asset || !Profile || !Selected || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return;
	const FFlipbookProfileEntry& ProfileEntry = Profile->Flipbooks[FlipbookIndex];
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Baseline = &ProfileEntry.FrameEventData.FrameCues;
	if (Profile->LayerBakeOwnerToken.IsValid())
	{
		if (const FPaper2DPlusCharacterBaselineAnimation* Source = Profile->FindCharacterBaseline(
			ProfileEntry.Identity.Flipbook.ToSoftObjectPath(), ProfileEntry.Identity.FlipbookName))
		{
			Baseline = &Source->FrameCues;
		}
	}
	TArray<int32> Included;
	FPaper2DPlusAppearanceDescriptor DefaultAppearance;
	Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(Asset, DefaultAppearance);
	const TSet<FGuid> DefaultLayerIds(DefaultAppearance.ActiveLayerIds);
	for (int32 Index = 0; Index < Asset->Layers.Num(); ++Index)
	{
		if (DefaultLayerIds.Contains(Asset->Layers[Index].LayerId)) Included.Add(Index);
	}
	const int32 SelectedAssetIndex = Asset->Layers.IndexOfByPredicate(
		[Selected](const FCharacterLayer& Layer) { return Layer.LayerId == Selected->LayerId; });
	TArray<FPaper2DPlusLayerGameplayOperation> LowerOperations;
	TArray<FPaper2DPlusLayerGameplayOperation> FinalOperations;
	for (int32 LayerIndex : Included)
	{
		const FCharacterLayer& Layer = Asset->Layers[LayerIndex];
		const FCharacterLayerAuthoredAnimationData* Entry = Layer.AuthoredAnimations.FindByPredicate(
			[&Identity](const FCharacterLayerAuthoredAnimationData& Candidate)
			{
				return FrameCueProvider_AnimationMatches(Candidate, Identity.Animation);
			});
		if (!Entry) continue;
		FPaper2DPlusLayerGameplayOperation Operation;
		Operation.FrameCues = Entry->FrameCues;
		const bool bIsBelowSelected = LayerIndex < SelectedAssetIndex;
		if (bIsBelowSelected) LowerOperations.Add(Operation);
		FinalOperations.Add(MoveTemp(Operation));
	}
	Paper2DPlusLayerGameplayCompose::ComposeFrameCues(
		*Baseline, LowerOperations, LowerCueProjection);
	Paper2DPlusLayerGameplayCompose::ComposeFrameCues(
		*Baseline, FinalOperations, FinalCueProjection);
}

const TArray<TObjectPtr<UPaper2DPlusCueBase>>* FLayerFrameCueDataProvider::GetLowerCues(
	int32 FlipbookIndex) const
{
	RebuildCueProjections(FlipbookIndex);
	return ProjectionIdentity.IsValid() ? &LowerCueProjection : nullptr;
}

const TArray<TObjectPtr<UPaper2DPlusCueBase>>* FLayerFrameCueDataProvider::GetFinalCues(
	int32 FlipbookIndex) const
{
	RebuildCueProjections(FlipbookIndex);
	return ProjectionIdentity.IsValid() ? &FinalCueProjection : nullptr;
}
