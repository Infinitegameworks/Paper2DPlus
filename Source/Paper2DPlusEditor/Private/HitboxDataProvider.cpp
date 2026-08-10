// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "HitboxDataProvider.h"

#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusLayerDraw.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusLayerGameplayCompose.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"

namespace
{
	bool CopyFrameRange(
		TArray<FFrameHitboxData>& Frames,
		int32 SourceFrameIndex,
		int32 RangeStart,
		int32 RangeEnd,
		bool bIncludeSockets,
		bool bMerge)
	{
		if (!Frames.IsValidIndex(SourceFrameIndex) || Frames.IsEmpty()) return false;
		const int32 Start = FMath::Clamp(FMath::Min(RangeStart, RangeEnd), 0, Frames.Num() - 1);
		const int32 End = FMath::Clamp(FMath::Max(RangeStart, RangeEnd), 0, Frames.Num() - 1);
		const FFrameHitboxData Source = Frames[SourceFrameIndex];
		for (int32 Index = Start; Index <= End; ++Index)
		{
			if (Index == SourceFrameIndex) continue;
			if (bMerge) Frames[Index].Hitboxes.Append(Source.Hitboxes);
			else Frames[Index].Hitboxes = Source.Hitboxes;
			if (!bIncludeSockets) continue;
			if (!bMerge)
			{
				Frames[Index].Sockets = Source.Sockets;
				continue;
			}
			for (const FSocketData& Socket : Source.Sockets)
			{
				if (!Frames[Index].Sockets.ContainsByPredicate(
					[&Socket](const FSocketData& Existing) { return Existing.Name == Socket.Name; }))
				{
					Frames[Index].Sockets.Add(Socket);
				}
			}
		}
		return true;
	}

	int32 MirrorFrameRange(
		TArray<FFrameHitboxData>& Frames,
		int32 RangeStart,
		int32 RangeEnd,
		int32 PivotX)
	{
		if (Frames.IsEmpty()) return 0;
		const int32 Start = FMath::Clamp(FMath::Min(RangeStart, RangeEnd), 0, Frames.Num() - 1);
		const int32 End = FMath::Clamp(FMath::Max(RangeStart, RangeEnd), 0, Frames.Num() - 1);
		int32 Count = 0;
		for (int32 FrameIndex = Start; FrameIndex <= End; ++FrameIndex)
		{
			for (FHitboxData& Hitbox : Frames[FrameIndex].Hitboxes)
			{
				Hitbox.X = (2 * PivotX) - (Hitbox.X + Hitbox.Width);
				++Count;
			}
			for (FSocketData& Socket : Frames[FrameIndex].Sockets)
			{
				Socket.X = (2 * PivotX) - Socket.X;
			}
		}
		return Count;
	}

	bool AnimationMatches(
		const TSoftObjectPtr<UPaperFlipbook>& Flipbook,
		const FString& LegacyName,
		const FProfileAnimationIdentity& Identity)
	{
		if (Identity.FlipbookPath.IsValid() && Flipbook.ToSoftObjectPath().IsValid())
		{
			return Flipbook.ToSoftObjectPath() == Identity.FlipbookPath;
		}
		return !Identity.FallbackName.IsEmpty()
			&& LegacyName.Equals(Identity.FallbackName, ESearchCase::IgnoreCase);
	}

	FFrameHitboxData MakeFrameView(const FCharacterLayerAuthoredFrameData& Source)
	{
		FFrameHitboxData Result;
		for (FHitboxData Box : Source.AttackBoxes)
		{
			Box.Type = EHitboxType::Attack;
			Result.Hitboxes.Add(MoveTemp(Box));
		}
		for (FHitboxData Box : Source.HurtBoxes)
		{
			Box.Type = EHitboxType::Hurtbox;
			Result.Hitboxes.Add(MoveTemp(Box));
		}
		Result.Sockets = Source.Sockets;
		return Result;
	}

	void WriteFrameView(
		const FFrameHitboxData& View,
		FCharacterLayerAuthoredFrameData& Destination)
	{
		Destination.AttackBoxes.Reset();
		Destination.HurtBoxes.Reset();
		for (FHitboxData Box : View.Hitboxes)
		{
			if (Box.Type == EHitboxType::Attack)
			{
				Box.Type = EHitboxType::Attack;
				Destination.AttackBoxes.Add(MoveTemp(Box));
			}
			else if (Box.Type == EHitboxType::Hurtbox)
			{
				Box.Type = EHitboxType::Hurtbox;
				Destination.HurtBoxes.Add(MoveTemp(Box));
			}
		}
		Destination.Sockets = View.Sockets;
	}
}

// ==========================================
// FHitboxFrameDataProvider stable helpers
// ==========================================

FProfileScopedAnimationIdentity FHitboxFrameDataProvider::GetScopedAnimationIdentity(
	int32 FlipbookIndex) const
{
	FProfileScopedAnimationIdentity Identity;
	Identity.Animation = GetAnimationIdentity(FlipbookIndex);
	Identity.LayerScope = GetLayerScopeIdentity();
	return Identity;
}

int32 FHitboxFrameDataProvider::ResolveAnimationIndex(
	const FProfileScopedAnimationIdentity& Identity) const
{
	return Identity.IsValid() && Identity.LayerScope == GetLayerScopeIdentity()
		? ResolveAnimationIndex(Identity.Animation)
		: INDEX_NONE;
}

const FFrameHitboxData* FHitboxFrameDataProvider::GetFrame(
	const FProfileScopedAnimationIdentity& Identity, int32 FrameIndex) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetFrame(FlipbookIndex, FrameIndex) : nullptr;
}

FFrameHitboxData* FHitboxFrameDataProvider::GetFrameMutable(
	const FProfileScopedAnimationIdentity& Identity, int32 FrameIndex)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetFrameMutable(FlipbookIndex, FrameIndex) : nullptr;
}

FFrameHitboxData* FHitboxFrameDataProvider::EnsureFrameMutable(
	const FProfileScopedAnimationIdentity& Identity, int32 FrameIndex)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? EnsureFrameMutable(FlipbookIndex, FrameIndex) : nullptr;
}

bool FHitboxFrameDataProvider::CanEnsureFrame(
	const FProfileScopedAnimationIdentity& Identity, int32 FrameIndex) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE && CanEnsureFrame(FlipbookIndex, FrameIndex);
}

int32 FHitboxFrameDataProvider::GetAuthoredFrameCount(
	const FProfileScopedAnimationIdentity& Identity) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE ? GetAuthoredFrameCount(FlipbookIndex) : 0;
}

bool FHitboxFrameDataProvider::CopyFrameDataToRange(
	const FProfileScopedAnimationIdentity& Identity,
	int32 SourceFrameIndex,
	int32 RangeStart,
	int32 RangeEnd,
	bool bIncludeSockets,
	bool bMerge)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE && CopyFrameDataToRange(
		FlipbookIndex, SourceFrameIndex, RangeStart, RangeEnd, bIncludeSockets, bMerge);
}

int32 FHitboxFrameDataProvider::MirrorHitboxesInRange(
	const FProfileScopedAnimationIdentity& Identity,
	int32 RangeStart,
	int32 RangeEnd,
	int32 PivotX)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE
		? MirrorHitboxesInRange(FlipbookIndex, RangeStart, RangeEnd, PivotX)
		: 0;
}

bool FHitboxFrameDataProvider::TryGetMergePolicy(
	const FProfileScopedAnimationIdentity& Identity,
	bool bAttack,
	EPaper2DPlusLayerSourceMerge& OutPolicy) const
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE && TryGetMergePolicy(FlipbookIndex, bAttack, OutPolicy);
}

bool FHitboxFrameDataProvider::SetMergePolicy(
	const FProfileScopedAnimationIdentity& Identity,
	bool bAttack,
	EPaper2DPlusLayerSourceMerge NewPolicy)
{
	const int32 FlipbookIndex = ResolveAnimationIndex(Identity);
	return FlipbookIndex != INDEX_NONE && SetMergePolicy(FlipbookIndex, bAttack, NewPolicy);
}

// ==========================================
// FProfileHitboxDataProvider
// ==========================================

FProfileHitboxDataProvider::FProfileHitboxDataProvider(TSharedPtr<FCharacterProfileEditorModel> InModel)
	: ModelWeak(InModel)
{
}

UPaper2DPlusCharacterProfileAsset* FProfileHitboxDataProvider::ResolveProfile() const
{
	TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	return Model.IsValid() ? Model->GetAsset() : nullptr;
}

bool FProfileHitboxDataProvider::UsesCharacterBaseline() const
{
	const UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	return Profile && Profile->LayerBakeOwnerToken.IsValid();
}

const FPaper2DPlusCharacterBaselineAnimation* FProfileHitboxDataProvider::ResolveBaseline(
	int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!UsesCharacterBaseline() || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	const FFlipbookIdentity& Identity = Profile->Flipbooks[FlipbookIndex].Identity;
	return Profile->FindCharacterBaseline(Identity.Flipbook.ToSoftObjectPath(), Identity.FlipbookName);
}

FPaper2DPlusCharacterBaselineAnimation* FProfileHitboxDataProvider::ResolveBaselineMutable(
	int32 FlipbookIndex)
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!UsesCharacterBaseline() || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	const FFlipbookIdentity& Identity = Profile->Flipbooks[FlipbookIndex].Identity;
	return Profile->FindCharacterBaselineMutable(Identity.Flipbook.ToSoftObjectPath(), Identity.FlipbookName);
}

FProfileAnimationIdentity FProfileHitboxDataProvider::GetAnimationIdentity(int32 FlipbookIndex) const
{
	return Paper2DPlusProfileToolProvider::MakeAnimationIdentity(ResolveProfile(), FlipbookIndex);
}

int32 FProfileHitboxDataProvider::ResolveAnimationIndex(const FProfileAnimationIdentity& Identity) const
{
	return Paper2DPlusProfileToolProvider::ResolveAnimationIndex(ResolveProfile(), Identity);
}

const FFrameHitboxData* FProfileHitboxDataProvider::GetFrame(int32 FlipbookIndex, int32 FrameIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Asset = ResolveProfile();
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	const FPaper2DPlusCharacterBaselineAnimation* Baseline = ResolveBaseline(FlipbookIndex);
	const TArray<FFrameHitboxData>& Frames = Baseline
		? Baseline->Frames
		: Asset->Flipbooks[FlipbookIndex].CombatData.Frames;
	return Frames.IsValidIndex(FrameIndex) ? &Frames[FrameIndex] : nullptr;
}

FFrameHitboxData* FProfileHitboxDataProvider::GetFrameMutable(int32 FlipbookIndex, int32 FrameIndex)
{
	UPaper2DPlusCharacterProfileAsset* Asset = ResolveProfile();
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	FPaper2DPlusCharacterBaselineAnimation* Baseline = ResolveBaselineMutable(FlipbookIndex);
	if (UsesCharacterBaseline() && !Baseline) return nullptr;
	TArray<FFrameHitboxData>& Frames = Baseline
		? Baseline->Frames
		: Asset->Flipbooks[FlipbookIndex].CombatData.Frames;
	return Frames.IsValidIndex(FrameIndex) ? &Frames[FrameIndex] : nullptr;
}

FFrameHitboxData* FProfileHitboxDataProvider::EnsureFrameMutable(int32 FlipbookIndex, int32 FrameIndex)
{
	// Profile rows are pre-synced to the flipbook's key-frame count (SyncAllFramesToFlipbooks at model
	// init, grow-only) — ensure is a plain find, matching the pre-seam behavior exactly.
	return GetFrameMutable(FlipbookIndex, FrameIndex);
}

bool FProfileHitboxDataProvider::CanEnsureFrame(int32 FlipbookIndex, int32 FrameIndex) const
{
	return GetFrame(FlipbookIndex, FrameIndex) != nullptr;
}

int32 FProfileHitboxDataProvider::GetAuthoredFrameCount(int32 FlipbookIndex) const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = ResolveProfile();
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return 0;
	if (const FPaper2DPlusCharacterBaselineAnimation* Baseline = ResolveBaseline(FlipbookIndex))
	{
		return Baseline->Frames.Num();
	}
	return UsesCharacterBaseline() ? 0 : Asset->Flipbooks[FlipbookIndex].CombatData.Frames.Num();
}

UObject* FProfileHitboxDataProvider::GetTransactionTarget() const
{
	return ResolveProfile();
}

bool FProfileHitboxDataProvider::CopyFrameDataToRange(int32 FlipbookIndex, int32 SourceFrameIndex, int32 RangeStart, int32 RangeEnd, bool bIncludeSockets, bool bMerge)
{
	UPaper2DPlusCharacterProfileAsset* Asset = ResolveProfile();
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return false;
	if (FPaper2DPlusCharacterBaselineAnimation* Baseline = ResolveBaselineMutable(FlipbookIndex))
	{
		return CopyFrameRange(Baseline->Frames, SourceFrameIndex, RangeStart, RangeEnd, bIncludeSockets, bMerge);
	}
	if (UsesCharacterBaseline()) return false;
	return Asset->CopyFrameDataToRange(Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName,
		SourceFrameIndex, RangeStart, RangeEnd, bIncludeSockets, bMerge);
}

int32 FProfileHitboxDataProvider::MirrorHitboxesInRange(int32 FlipbookIndex, int32 RangeStart, int32 RangeEnd, int32 PivotX)
{
	UPaper2DPlusCharacterProfileAsset* Asset = ResolveProfile();
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return 0;
	if (FPaper2DPlusCharacterBaselineAnimation* Baseline = ResolveBaselineMutable(FlipbookIndex))
	{
		return MirrorFrameRange(Baseline->Frames, RangeStart, RangeEnd, PivotX);
	}
	if (UsesCharacterBaseline()) return 0;
	return Asset->MirrorHitboxesInRange(Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName, RangeStart, RangeEnd, PivotX);
}

void FProfileHitboxDataProvider::GetGhostBoxes(
	int32 FlipbookIndex,
	int32 FrameIndex,
	TArray<FHitboxData>& OutBoxes) const
{
	OutBoxes.Reset();
	const UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!UsesCharacterBaseline() || !Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return;
	const TArray<FFrameHitboxData>& Compiled = Profile->Flipbooks[FlipbookIndex].CombatData.Frames;
	if (Compiled.IsValidIndex(FrameIndex)) OutBoxes = Compiled[FrameIndex].Hitboxes;
}

// ==========================================
// FLayerHitboxDataProvider
// ==========================================

FLayerHitboxDataProvider::FLayerHitboxDataProvider(TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> InLayerAsset, TSharedPtr<FCharacterProfileEditorModel> InModel)
	: LayerAsset(InLayerAsset)
	, ModelWeak(InModel)
{
}

UPaper2DPlusCharacterProfileAsset* FLayerHitboxDataProvider::ResolveProfile() const
{
	TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	return Model.IsValid() ? Model->GetAsset() : nullptr;
}

FProfileAnimationIdentity FLayerHitboxDataProvider::GetAnimationIdentity(int32 FlipbookIndex) const
{
	return Paper2DPlusProfileToolProvider::MakeAnimationIdentity(ResolveProfile(), FlipbookIndex);
}

int32 FLayerHitboxDataProvider::ResolveAnimationIndex(const FProfileAnimationIdentity& Identity) const
{
	return Paper2DPlusProfileToolProvider::ResolveAnimationIndex(ResolveProfile(), Identity);
}

FProfileLayerScopeIdentity FLayerHitboxDataProvider::GetLayerScopeIdentity() const
{
	const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	return Model.IsValid()
		? FProfileLayerScopeIdentity::Layer(Model->GetSelectedLayerId())
		: FProfileLayerScopeIdentity::Layer(FGuid());
}

FString FLayerHitboxDataProvider::ResolveAnimationName(int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return FString();
	return Profile->Flipbooks[FlipbookIndex].Identity.FlipbookName;
}

int32 FLayerHitboxDataProvider::ResolveBaseFrameCount(int32 FlipbookIndex) const
{
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return 0;
	const FFlipbookProfileEntry& Anim = Profile->Flipbooks[FlipbookIndex];
	if (!Anim.Identity.Flipbook.IsNull())
	{
		if (UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.Get())
		{
			return Flipbook->GetNumKeyFrames();
		}
	}
	return Anim.CombatData.Frames.Num();
}

const FCharacterLayer* FLayerHitboxDataProvider::ResolveScopedLayer() const
{
	const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	if (!Model.IsValid() || !LayerAsset.IsValid()) return nullptr;
	return LayerAsset->GetLayerById(Model->GetSelectedLayerId());
}

FCharacterLayer* FLayerHitboxDataProvider::ResolveScopedLayerMutable()
{
	const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
	if (!Model.IsValid() || !LayerAsset.IsValid()) return nullptr;
	return LayerAsset->GetLayerByIdMutable(Model->GetSelectedLayerId());
}

const FCharacterLayerAuthoredAnimationData* FLayerHitboxDataProvider::FindAuthoredAnimation(
	const FCharacterLayer& Layer,
	const FProfileAnimationIdentity& Identity) const
{
	return Layer.AuthoredAnimations.FindByPredicate(
		[&Identity](const FCharacterLayerAuthoredAnimationData& Entry)
		{
			return AnimationMatches(Entry.Flipbook, Entry.LegacyAnimationName, Identity);
		});
}

FCharacterLayerAuthoredAnimationData* FLayerHitboxDataProvider::FindAuthoredAnimationMutable(
	FCharacterLayer& Layer,
	const FProfileAnimationIdentity& Identity)
{
	return Layer.AuthoredAnimations.FindByPredicate(
		[&Identity](const FCharacterLayerAuthoredAnimationData& Entry)
		{
			return AnimationMatches(Entry.Flipbook, Entry.LegacyAnimationName, Identity);
		});
}

const FCharacterLayerAuthoredAnimationData* FLayerHitboxDataProvider::FindAuthoredAnimation(
	int32 FlipbookIndex) const
{
	const FCharacterLayer* Layer = ResolveScopedLayer();
	return Layer ? FindAuthoredAnimation(*Layer, GetAnimationIdentity(FlipbookIndex)) : nullptr;
}

FCharacterLayerAuthoredAnimationData* FLayerHitboxDataProvider::FindAuthoredAnimationMutable(
	int32 FlipbookIndex)
{
	FCharacterLayer* Layer = ResolveScopedLayerMutable();
	return Layer ? FindAuthoredAnimationMutable(*Layer, GetAnimationIdentity(FlipbookIndex)) : nullptr;
}

FCharacterLayerAuthoredAnimationData* FLayerHitboxDataProvider::EnsureAuthoredAnimationMutable(
	int32 FlipbookIndex)
{
	FCharacterLayer* Layer = ResolveScopedLayerMutable();
	UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	if (!Layer || !Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
	const int32 FrameCount = ResolveBaseFrameCount(FlipbookIndex);
	if (FrameCount <= 0) return nullptr;
	const FProfileAnimationIdentity Identity = GetAnimationIdentity(FlipbookIndex);
	FCharacterLayerAuthoredAnimationData* Entry = FindAuthoredAnimationMutable(*Layer, Identity);
	if (!Entry)
	{
		Entry = &Layer->AuthoredAnimations.AddDefaulted_GetRef();
		Entry->Flipbook = Profile->Flipbooks[FlipbookIndex].Identity.Flipbook;
		Entry->LegacyAnimationName = Profile->Flipbooks[FlipbookIndex].Identity.FlipbookName;
	}
	if (Entry->Frames.Num() < FrameCount) Entry->Frames.SetNum(FrameCount);
	return Entry;
}

const FFrameHitboxData* FLayerHitboxDataProvider::GetFrame(int32 FlipbookIndex, int32 FrameIndex) const
{
	const FProfileScopedAnimationIdentity Identity = GetScopedAnimationIdentity(FlipbookIndex);
	if (!Identity.IsValid()) return nullptr;
	RebuildFrameView(Identity);
	return FrameView.IsValidIndex(FrameIndex) ? &FrameView[FrameIndex] : nullptr;
}

FFrameHitboxData* FLayerHitboxDataProvider::GetFrameMutable(int32 FlipbookIndex, int32 FrameIndex)
{
	const FProfileScopedAnimationIdentity Identity = GetScopedAnimationIdentity(FlipbookIndex);
	if (!Identity.IsValid()) return nullptr;
	RebuildFrameView(Identity);
	if (!FrameView.IsValidIndex(FrameIndex)) return nullptr;
	bFrameViewBorrowedMutable = true;
	return &FrameView[FrameIndex];
}

FFrameHitboxData* FLayerHitboxDataProvider::EnsureFrameMutable(int32 FlipbookIndex, int32 FrameIndex)
{
	if (!CanEnsureFrame(FlipbookIndex, FrameIndex)
		|| !EnsureAuthoredAnimationMutable(FlipbookIndex))
	{
		return nullptr;
	}
	InvalidateCachedViews();
	return GetFrameMutable(FlipbookIndex, FrameIndex);
}

bool FLayerHitboxDataProvider::CanEnsureFrame(int32 FlipbookIndex, int32 FrameIndex) const
{
	if (!ResolveScopedLayer()) return false;
	if (ResolveAnimationName(FlipbookIndex).IsEmpty()) return false;
	return FrameIndex >= 0 && FrameIndex < ResolveBaseFrameCount(FlipbookIndex);
}

int32 FLayerHitboxDataProvider::GetAuthoredFrameCount(int32 FlipbookIndex) const
{
	const FCharacterLayerAuthoredAnimationData* Entry = FindAuthoredAnimation(FlipbookIndex);
	return Entry ? Entry->Frames.Num() : 0;
}

UObject* FLayerHitboxDataProvider::GetTransactionTarget() const
{
	return LayerAsset.Get();
}

bool FLayerHitboxDataProvider::HasResolvedScope() const
{
	return ResolveScopedLayer() != nullptr;
}

FText FLayerHitboxDataProvider::GetScopeDisplayText() const
{
	const FCharacterLayer* Layer = ResolveScopedLayer();
	return Layer ? FText::FromString(Layer->LayerName) : FText::GetEmpty();
}

void FLayerHitboxDataProvider::BeginEdit()
{
	bEditActive = true;
}

void FLayerHitboxDataProvider::CommitEdit()
{
	if (bEditActive && bFrameViewBorrowedMutable)
	{
		WriteFrameViewToSource();
	}
	bEditActive = false;
	bFrameViewBorrowedMutable = false;
}

void FLayerHitboxDataProvider::DiscardEdit()
{
	bEditActive = false;
	InvalidateCachedViews();
}

void FLayerHitboxDataProvider::InvalidateCachedViews()
{
	bFrameViewValid = false;
	bFrameViewBorrowedMutable = false;
	FrameView.Reset();
	FrameViewIdentity = {};
}

void FLayerHitboxDataProvider::RebuildFrameView(
	const FProfileScopedAnimationIdentity& Identity) const
{
	if (bFrameViewValid
		&& FrameViewIdentity.Animation == Identity.Animation
		&& FrameViewIdentity.LayerScope == Identity.LayerScope)
	{
		return;
	}
	FrameView.Reset();
	FrameViewIdentity = Identity;
	bFrameViewValid = true;
	bFrameViewBorrowedMutable = false;
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	const FCharacterLayer* Layer = Asset
		? Asset->GetLayerById(Identity.LayerScope.LayerId)
		: nullptr;
	const FCharacterLayerAuthoredAnimationData* Entry = Layer
		? FindAuthoredAnimation(*Layer, Identity.Animation)
		: nullptr;
	if (!Entry) return;
	FrameView.Reserve(Entry->Frames.Num());
	for (const FCharacterLayerAuthoredFrameData& Frame : Entry->Frames)
	{
		FrameView.Add(MakeFrameView(Frame));
	}
}

void FLayerHitboxDataProvider::WriteFrameViewToSource()
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	FCharacterLayer* Layer = Asset
		? Asset->GetLayerByIdMutable(FrameViewIdentity.LayerScope.LayerId)
		: nullptr;
	FCharacterLayerAuthoredAnimationData* Entry = Layer
		? FindAuthoredAnimationMutable(*Layer, FrameViewIdentity.Animation)
		: nullptr;
	if (!Entry) return;
	if (Entry->Frames.Num() < FrameView.Num()) Entry->Frames.SetNum(FrameView.Num());
	const int32 Count = FMath::Min(Entry->Frames.Num(), FrameView.Num());
	for (int32 FrameIndex = 0; FrameIndex < Count; ++FrameIndex)
	{
		WriteFrameView(FrameView[FrameIndex], Entry->Frames[FrameIndex]);
	}
}

FVector2D FLayerHitboxDataProvider::GetAuthoringDisplayOffsetPx(
	int32 FlipbookIndex,
	int32 FrameIndex) const
{
	const UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	const FCharacterLayer* Layer = ResolveScopedLayer();
	if (!Profile || !Layer || !Profile->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return FVector2D::ZeroVector;
	}
	const FFlipbookProfileEntry& Entry = Profile->Flipbooks[FlipbookIndex];
	return Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
		&Entry, FrameIndex, Layer, Entry.Identity.FlipbookName);
}

void FLayerHitboxDataProvider::BuildCompositionGhosts(
	int32 FlipbookIndex,
	int32 FrameIndex,
	TArray<FHitboxData>* OutLower,
	TArray<FHitboxData>* OutFinal) const
{
	if (OutLower) OutLower->Reset();
	if (OutFinal) OutFinal->Reset();
	const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	const UPaper2DPlusCharacterProfileAsset* Profile = ResolveProfile();
	const FCharacterLayer* Selected = ResolveScopedLayer();
	if (!Asset || !Profile || !Selected || !Profile->Flipbooks.IsValidIndex(FlipbookIndex)) return;
	const FFlipbookProfileEntry& ProfileEntry = Profile->Flipbooks[FlipbookIndex];
	TArray<FFrameHitboxData> Baseline = ProfileEntry.CombatData.Frames;
	if (Profile->LayerBakeOwnerToken.IsValid())
	{
		if (const FPaper2DPlusCharacterBaselineAnimation* Source = Profile->FindCharacterBaseline(
			ProfileEntry.Identity.Flipbook.ToSoftObjectPath(), ProfileEntry.Identity.FlipbookName))
		{
			Baseline = Source->Frames;
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
	const FProfileAnimationIdentity AnimationIdentity = GetAnimationIdentity(FlipbookIndex);
	for (int32 LayerIndex : Included)
	{
		const FCharacterLayer& Layer = Asset->Layers[LayerIndex];
		const FCharacterLayerAuthoredAnimationData* Source =
			FindAuthoredAnimation(Layer, AnimationIdentity);
		if (!Source) continue;
		FPaper2DPlusLayerGameplayOperation Operation;
		Operation.AttackMerge = Source->AttackMerge;
		Operation.HurtMerge = Source->HurtMerge;
		Operation.SocketMerge = Source->SocketMerge;
		Operation.Frames.Reserve(Source->Frames.Num());
		for (int32 SourceFrameIndex = 0; SourceFrameIndex < Source->Frames.Num(); ++SourceFrameIndex)
		{
			const FCharacterLayerAuthoredFrameData& SourceFrame = Source->Frames[SourceFrameIndex];
			const FVector2D PlacementPx = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
				&ProfileEntry, SourceFrameIndex, &Layer, ProfileEntry.Identity.FlipbookName);
			const FIntPoint Placement(
				FMath::RoundToInt(PlacementPx.X), FMath::RoundToInt(PlacementPx.Y));
			FFrameHitboxData Frame = MakeFrameView(SourceFrame);
			for (FHitboxData& Box : Frame.Hitboxes)
			{
				Box.X += Placement.X;
				Box.Y += Placement.Y;
			}
			for (FSocketData& Socket : Frame.Sockets)
			{
				Socket.X += Placement.X;
				Socket.Y += Placement.Y;
			}
			Operation.Frames.Add(MoveTemp(Frame));
		}
		FinalOperations.Add(Operation);
		const bool bIsBelowSelected = LayerIndex < SelectedAssetIndex;
		if (bIsBelowSelected) LowerOperations.Add(MoveTemp(Operation));
	}

	TArray<FFrameHitboxData> LowerFrames = Baseline;
	TArray<FFrameHitboxData> FinalFrames = Baseline;
	if (!LowerOperations.IsEmpty())
	{
		Paper2DPlusLayerGameplayCompose::ComposeFrames(Baseline, LowerOperations, LowerFrames);
	}
	if (!FinalOperations.IsEmpty())
	{
		Paper2DPlusLayerGameplayCompose::ComposeFrames(Baseline, FinalOperations, FinalFrames);
	}
	if (OutLower && LowerFrames.IsValidIndex(FrameIndex)) *OutLower = LowerFrames[FrameIndex].Hitboxes;
	if (OutFinal && FinalFrames.IsValidIndex(FrameIndex)) *OutFinal = FinalFrames[FrameIndex].Hitboxes;
}

void FLayerHitboxDataProvider::GetGhostBoxes(
	int32 FlipbookIndex,
	int32 FrameIndex,
	TArray<FHitboxData>& OutBoxes) const
{
	BuildCompositionGhosts(FlipbookIndex, FrameIndex, &OutBoxes, nullptr);
}

void FLayerHitboxDataProvider::GetFinalGhostBoxes(
	int32 FlipbookIndex,
	int32 FrameIndex,
	TArray<FHitboxData>& OutBoxes) const
{
	BuildCompositionGhosts(FlipbookIndex, FrameIndex, nullptr, &OutBoxes);
}

bool FLayerHitboxDataProvider::CopyFrameDataToRange(
	int32 FlipbookIndex,
	int32 SourceFrameIndex,
	int32 RangeStart,
	int32 RangeEnd,
	bool bIncludeSockets,
	bool bMerge)
{
	if (!GetFrameMutable(FlipbookIndex, SourceFrameIndex)) return false;
	bFrameViewBorrowedMutable = true;
	return CopyFrameRange(FrameView, SourceFrameIndex, RangeStart, RangeEnd, bIncludeSockets, bMerge);
}

int32 FLayerHitboxDataProvider::MirrorHitboxesInRange(
	int32 FlipbookIndex,
	int32 RangeStart,
	int32 RangeEnd,
	int32 PivotX)
{
	if (!GetFrameMutable(FlipbookIndex, RangeStart)) return 0;
	bFrameViewBorrowedMutable = true;
	return MirrorFrameRange(FrameView, RangeStart, RangeEnd, PivotX);
}

bool FLayerHitboxDataProvider::TryGetMergePolicy(int32 FlipbookIndex, bool bAttack, EPaper2DPlusLayerSourceMerge& OutPolicy) const
{
	const FCharacterLayerAuthoredAnimationData* Entry = FindAuthoredAnimation(FlipbookIndex);
	if (!Entry) return false;
	OutPolicy = bAttack ? Entry->AttackMerge : Entry->HurtMerge;
	return true;
}

bool FLayerHitboxDataProvider::SetMergePolicy(int32 FlipbookIndex, bool bAttack, EPaper2DPlusLayerSourceMerge NewPolicy)
{
	FCharacterLayerAuthoredAnimationData* Entry = FindAuthoredAnimationMutable(FlipbookIndex);
	if (!Entry) return false;
	if (bAttack)
	{
		Entry->AttackMerge = NewPolicy;
	}
	else
	{
		Entry->HurtMerge = NewPolicy;
	}
	return true;
}
