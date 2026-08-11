// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationProfilePickerSource.h"

#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"

#define LOCTEXT_NAMESPACE "AnimationProfilePickerSource"

namespace AnimationProfilePickerSourcePrivate
{
	const FName SourceType(TEXT("Animation"));
}

FAnimationProfilePickerSource::FAnimationProfilePickerSource(TSharedPtr<FCharacterProfileEditorModel> InModel)
	: Model(MoveTemp(InModel))
{
	if (Model.IsValid())
	{
		SelectionChangedHandle = Model->OnFlipbookSelectionChanged.AddLambda([this](int32) { NotifySourceChanged(); });
		AssetDataChangedHandle = Model->OnAssetDataChanged.AddRaw(this, &FAnimationProfilePickerSource::NotifySourceChanged);
		ExternalModifiedHandle = Model->OnAssetExternallyModified.AddRaw(this, &FAnimationProfilePickerSource::NotifySourceChanged);
	}
}

FAnimationProfilePickerSource::~FAnimationProfilePickerSource()
{
	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(SelectionChangedHandle);
		Model->OnAssetDataChanged.Remove(AssetDataChangedHandle);
		Model->OnAssetExternallyModified.Remove(ExternalModifiedHandle);
	}
}

FName FAnimationProfilePickerSource::GetSourceType() const
{
	return AnimationProfilePickerSourcePrivate::SourceType;
}

FString FAnimationProfilePickerSource::GetLogicalCatalogScope() const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	return Asset
		? FString::Printf(TEXT("CharacterProfile:%s"), *Asset->GetPathName())
		: TEXT("CharacterProfile:<none>");
}

FProfileItemIdentity FAnimationProfilePickerSource::MakeIdentity(int32 Index) const
{
	FProfileItemIdentity Identity;
	Identity.SourceType = GetSourceType();
	const UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!Asset || !Asset->Flipbooks.IsValidIndex(Index))
	{
		return Identity;
	}
	const FFlipbookProfileEntry& Entry = Asset->Flipbooks[Index];
	Identity.ObjectPath = Entry.Identity.Flipbook.ToSoftObjectPath();
	Identity.FallbackKey = Entry.Identity.FlipbookName.TrimStartAndEnd();
	return Identity;
}

void FAnimationProfilePickerSource::GetItems(TArray<FProfilePickerItem>& OutItems) const
{
	OutItems.Reset();
	const UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	// One effective-tag batch for the entire projection; never one graph walk per row.
	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
	OutItems.Reserve(Asset->Flipbooks.Num());
	for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
	{
		const FFlipbookProfileEntry& Entry = Asset->Flipbooks[Index];
		FProfilePickerItem Item;
		Item.Identity = MakeIdentity(Index);
		if (!Item.Identity.IsValid())
		{
			continue;
		}
		Item.Label = FText::FromString(Entry.Identity.FlipbookName);
		Item.Group = Entry.FlipbookGroup.IsNone() ? FString() : Entry.FlipbookGroup.ToString();
		Item.CanonicalOrder = Index; // Profile array order is the canonical authoring order.

		const int32 FrameCount = Entry.CombatData.Frames.Num();
		Item.SecondaryText = FrameCount == 1
			? LOCTEXT("OneFrame", "1 frame")
			: FText::Format(LOCTEXT("FrameCount", "{0} frames"), FText::AsNumber(FrameCount));
		if (!Item.Identity.ObjectPath.IsNull())
		{
			Item.Aliases.Add(Item.Identity.ObjectPath.GetAssetName());
			Item.Aliases.Add(Item.Identity.ObjectPath.ToString());
		}
		if (Entry.Identity.PaperZDSequence)
		{
			Item.Aliases.Add(Entry.Identity.PaperZDSequence->GetName());
		}
		if (const Paper2DPlusAnimationTagQuery::FAnimationTagSet* Tags =
			TagMap.Find(Entry.Identity.FlipbookName.ToLower()))
		{
			Item.SearchTags = Tags->EffectiveTags;
		}
		OutItems.Add(MoveTemp(Item));
	}
}

FProfileItemIdentity FAnimationProfilePickerSource::GetSelectedIdentity() const
{
	return Model.IsValid() ? MakeIdentity(Model->GetSelectedFlipbookIndex()) : FProfileItemIdentity();
}

int32 FAnimationProfilePickerSource::ResolveIndex(const FProfileItemIdentity& Identity) const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!Asset || Identity.SourceType != GetSourceType())
	{
		return INDEX_NONE;
	}

	if (!Identity.ObjectPath.IsNull())
	{
		for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
		{
			if (Asset->Flipbooks[Index].Identity.Flipbook.ToSoftObjectPath() == Identity.ObjectPath)
			{
				return Index;
			}
		}
	}
	if (!Identity.FallbackKey.IsEmpty())
	{
		for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
		{
			if (Asset->Flipbooks[Index].Identity.FlipbookName.Equals(Identity.FallbackKey, ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}

bool FAnimationProfilePickerSource::SelectItem(const FProfileItemIdentity& Identity)
{
	const int32 Index = ResolveIndex(Identity);
	if (!Model.IsValid() || Index == INDEX_NONE)
	{
		return false;
	}
	Model->SetSelectedFlipbook(Index);
	return true;
}

void FAnimationProfilePickerSource::NotifySourceChanged()
{
	SourceChanged.Broadcast();
}

#undef LOCTEXT_NAMESPACE
