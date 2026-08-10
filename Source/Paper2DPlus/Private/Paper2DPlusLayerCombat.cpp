// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusLayerCombat.h"

#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusLayerDraw.h"
#include "Paper2DPlusLayerGameplayCompose.h"
#include "Paper2DPlusTypes.h"

namespace
{
	const FCharacterLayerAuthoredAnimationData* LayerCombatImpl_FindCookedLayerEntry(
		const FCharacterLayer& Layer,
		const FFlipbookProfileEntry* Base,
		const FString& AnimationName)
	{
		const FSoftObjectPath TargetPath = Base
			? Base->Identity.Flipbook.ToSoftObjectPath()
			: FSoftObjectPath();
		if (TargetPath.IsValid())
		{
			for (const FCharacterLayerAuthoredAnimationData& Entry : Layer.CookedGameplayAnimations)
			{
				if (Entry.Flipbook.ToSoftObjectPath() == TargetPath)
				{
					return &Entry;
				}
			}
		}

		// The soft flipbook is authoritative when present. Name fallback exists only for migrated rows
		// that never acquired an object binding, matching the fixed-publishing compiler's identity rule.
		for (const FCharacterLayerAuthoredAnimationData& Entry : Layer.CookedGameplayAnimations)
		{
			if (Entry.Flipbook.IsNull()
				&& Entry.LegacyAnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	void LayerCombatImpl_AdaptCookedLayerOperation(
		const FCharacterLayerAuthoredAnimationData& Entry,
		const FCharacterLayer& Layer,
		const FFlipbookProfileEntry* Base,
		const FString& AnimationName,
		FPaper2DPlusLayerGameplayOperation& OutOperation)
	{
		OutOperation.AttackMerge = Entry.AttackMerge;
		OutOperation.HurtMerge = Entry.HurtMerge;
		OutOperation.SocketMerge = Entry.SocketMerge;
		OutOperation.FrameCues = Entry.FrameCues;

		// The layer source is authoritative for its own authored extent. Character Profile combat rows can
		// legitimately be empty or sparse relative to the flipbook; clipping here would silently discard
		// later layer-local hitboxes and sockets before the shared composer can grow default baseline rows.
		const int32 FrameCount = Entry.Frames.Num();
		OutOperation.Frames.SetNum(FrameCount);
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			const FVector2D Offset = Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
				Base, FrameIndex, &Layer, AnimationName);
			const FIntPoint Placement(
				FMath::RoundToInt(Offset.X),
				FMath::RoundToInt(Offset.Y));
			FFrameHitboxData& Destination = OutOperation.Frames[FrameIndex];
			const FCharacterLayerAuthoredFrameData& Source = Entry.Frames[FrameIndex];
			for (FHitboxData Box : Source.AttackBoxes)
			{
				Box.Type = EHitboxType::Attack;
				Box.X += Placement.X;
				Box.Y += Placement.Y;
				Destination.Hitboxes.Add(MoveTemp(Box));
			}
			for (FHitboxData Box : Source.HurtBoxes)
			{
				Box.Type = EHitboxType::Hurtbox;
				Box.X += Placement.X;
				Box.Y += Placement.Y;
				Destination.Hitboxes.Add(MoveTemp(Box));
			}
			for (FSocketData Socket : Source.Sockets)
			{
				Socket.X += Placement.X;
				Socket.Y += Placement.Y;
				Destination.Sockets.Add(MoveTemp(Socket));
			}
		}
	}

	void LayerCombatImpl_BuildGenericOperations(
		const FFlipbookProfileEntry* Base,
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const FPaper2DPlusAppearanceDescriptor& CommittedAppearance,
		const FString& AnimationName,
		TArray<FPaper2DPlusLayerGameplayOperation>& OutOperations)
	{
		OutOperations.Reset();
		for (const FGuid& LayerId : Paper2DPlusAppearanceResolver::ResolveContributingGameplayLayerIds(
			LayerAsset, CommittedAppearance, AnimationName))
		{
			const FCharacterLayer* Layer = LayerAsset->FindLayerById(LayerId);
			if (!Layer) continue;
			const FCharacterLayerAuthoredAnimationData* Entry =
				LayerCombatImpl_FindCookedLayerEntry(*Layer, Base, AnimationName);
			if (!Entry) continue;

			FPaper2DPlusLayerGameplayOperation& Operation = OutOperations.AddDefaulted_GetRef();
			LayerCombatImpl_AdaptCookedLayerOperation(
				*Entry, *Layer, Base, AnimationName, Operation);
		}
	}

}

bool Paper2DPlusLayerCombat::ComposeCombatFrames(
	const FFlipbookProfileEntry* Base,
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const FPaper2DPlusAppearanceDescriptor& CommittedAppearance,
	const FString& AnimationName,
	TArray<FFrameHitboxData>& OutFrames)
{
	if (!Base || !LayerAsset
		|| LayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable
		|| !Paper2DPlusAppearanceResolver::IsCompatible(CommittedAppearance, LayerAsset))
	{
		return false;
	}

	TArray<FPaper2DPlusLayerGameplayOperation> Operations;
	LayerCombatImpl_BuildGenericOperations(
		Base, LayerAsset, CommittedAppearance, AnimationName, Operations);
	return !Operations.IsEmpty()
		&& Paper2DPlusLayerGameplayCompose::ComposeFrames(Base->CombatData.Frames, Operations, OutFrames);
}

bool Paper2DPlusLayerCombat::ComposeFrameCues(
	const FFlipbookProfileEntry* Base,
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const FPaper2DPlusAppearanceDescriptor& CommittedAppearance,
	const FString& AnimationName,
	TArray<TObjectPtr<UPaper2DPlusCueBase>>& OutCues)
{
	OutCues.Reset();
	if (!LayerAsset
		|| LayerAsset->UsageMode != ECharacterLayerUsageMode::RuntimeCustomizable
		|| !Paper2DPlusAppearanceResolver::IsCompatible(CommittedAppearance, LayerAsset))
	{
		return false;
	}

	TArray<FPaper2DPlusLayerGameplayOperation> Operations;
	LayerCombatImpl_BuildGenericOperations(
		Base, LayerAsset, CommittedAppearance, AnimationName, Operations);
	const TArray<TObjectPtr<UPaper2DPlusCueBase>> EmptyBaseline;
	return !Operations.IsEmpty()
		&& Paper2DPlusLayerGameplayCompose::ComposeFrameCues(EmptyBaseline, Operations, OutCues);
}
