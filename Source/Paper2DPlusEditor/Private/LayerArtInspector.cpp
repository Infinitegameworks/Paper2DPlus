// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerArtInspector.h"

#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "LayerArtInspector"

namespace
{
	const FFlipbookProfileEntry* ResolveAnimation(
		const FCharacterProfileEditorModel& Model)
	{
		const UPaper2DPlusCharacterProfileAsset* Profile = Model.GetAsset();
		return Profile && Profile->Flipbooks.IsValidIndex(Model.GetSelectedFlipbookIndex())
			? &Profile->Flipbooks[Model.GetSelectedFlipbookIndex()] : nullptr;
	}

	bool MappingMatches(
		const FCharacterLayerAnimationMapping& Mapping,
		const FFlipbookProfileEntry& Animation)
	{
		const FSoftObjectPath TargetPath = Animation.Identity.Flipbook.ToSoftObjectPath();
		if (TargetPath.IsValid() && Mapping.Flipbook.ToSoftObjectPath().IsValid())
		{
			return Mapping.Flipbook.ToSoftObjectPath() == TargetPath;
		}
		return Mapping.AnimationName.Equals(Animation.Identity.FlipbookName, ESearchCase::IgnoreCase);
	}

	bool OffsetMatches(
		const FCharacterLayerAnimationOffset& Offset,
		const FFlipbookProfileEntry& Animation)
	{
		const FSoftObjectPath TargetPath = Animation.Identity.Flipbook.ToSoftObjectPath();
		if (TargetPath.IsValid() && Offset.Flipbook.ToSoftObjectPath().IsValid())
		{
			return Offset.Flipbook.ToSoftObjectPath() == TargetPath;
		}
		return Offset.AnimationName.Equals(Animation.Identity.FlipbookName, ESearchCase::IgnoreCase);
	}

	int32 ResolveFrameCount(const FFlipbookProfileEntry& Animation)
	{
		if (UPaperFlipbook* Flipbook = Animation.Identity.Flipbook.Get())
		{
			return Flipbook->GetNumKeyFrames();
		}
		return Animation.CombatData.Frames.Num();
	}
}

FVector2D SLayerArtInspector::GetPlacement(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	const FCharacterProfileEditorModel& InModel,
	ELayerArtOffsetScope Scope)
{
	const FCharacterLayer* Layer = Asset.GetLayerById(InModel.GetSelectedLayerId());
	if (!Layer) return FVector2D::ZeroVector;
	if (Scope == ELayerArtOffsetScope::AllAnimations) return Layer->DefaultOffsetPx;
	const FFlipbookProfileEntry* Animation = ResolveAnimation(InModel);
	if (!Animation) return Layer->DefaultOffsetPx;
	if (const FCharacterLayerAnimationOffset* Offset = Layer->AnimationOffsets.FindByPredicate(
		[Animation](const FCharacterLayerAnimationOffset& Candidate)
		{
			return OffsetMatches(Candidate, *Animation);
		}))
	{
		return Offset->OffsetPx;
	}
	return Layer->DefaultOffsetPx;
}

bool SLayerArtInspector::SetPlacement(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& InModel,
	ELayerArtOffsetScope Scope,
	const FVector2D& NewValue)
{
	FCharacterLayer* Layer = Asset.GetLayerByIdMutable(InModel.GetSelectedLayerId());
	if (!Layer) return false;
	if (Scope == ELayerArtOffsetScope::AllAnimations)
	{
		if (Layer->DefaultOffsetPx.Equals(NewValue)) return false;
		Layer->DefaultOffsetPx = NewValue;
		return true;
	}
	const FFlipbookProfileEntry* Animation = ResolveAnimation(InModel);
	if (!Animation) return false;
	FCharacterLayerAnimationOffset* Offset = Layer->AnimationOffsets.FindByPredicate(
		[Animation](const FCharacterLayerAnimationOffset& Candidate)
		{
			return OffsetMatches(Candidate, *Animation);
		});
	if (Offset && Offset->OffsetPx.Equals(NewValue)) return false;
	if (!Offset && Layer->DefaultOffsetPx.Equals(NewValue)) return false;
	if (!Offset)
	{
		Offset = &Layer->AnimationOffsets.AddDefaulted_GetRef();
		Offset->AnimationName = Animation->Identity.FlipbookName;
		Offset->Flipbook = Animation->Identity.Flipbook;
	}
	Offset->OffsetPx = NewValue;
	return true;
}

bool SLayerArtInspector::SetFrameSprite(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& InModel,
	UPaperSprite* Sprite)
{
	FCharacterLayer* Layer = Asset.GetLayerByIdMutable(InModel.GetSelectedLayerId());
	const FFlipbookProfileEntry* Animation = ResolveAnimation(InModel);
	if (!Layer || !Animation) return false;
	const int32 FrameIndex = InModel.GetSelectedFrameIndex();
	const int32 FrameCount = ResolveFrameCount(*Animation);
	if (FrameIndex < 0 || FrameIndex >= FrameCount) return false;
	FCharacterLayerAnimationMapping* Mapping = Layer->AnimationSprites.FindByPredicate(
		[Animation](const FCharacterLayerAnimationMapping& Candidate)
		{
			return MappingMatches(Candidate, *Animation);
		});
	UPaperSprite* CurrentSprite = Mapping && Mapping->Sprites.IsValidIndex(FrameIndex)
		? Mapping->Sprites[FrameIndex].Get() : nullptr;
	if (CurrentSprite == Sprite) return false;
	if (!Mapping)
	{
		Mapping = &Layer->AnimationSprites.AddDefaulted_GetRef();
		Mapping->AnimationName = Animation->Identity.FlipbookName;
		Mapping->Flipbook = Animation->Identity.Flipbook;
	}
	if (Mapping->Sprites.Num() < FrameCount) Mapping->Sprites.SetNum(FrameCount);
	Mapping->Sprites[FrameIndex] = Sprite;
	return true;
}

void SLayerArtInspector::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	LayerAsset = InArgs._LayerAsset;
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(5.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(this, &SLayerArtInspector::GetLayerTitle)
					.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)
				[
					SNew(STextBlock).Text(this, &SLayerArtInspector::GetMappingSummary)
					.AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					SNew(STextBlock).Text(LOCTEXT("FrameSpriteLabel", "Sprite for Current Frame"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UPaperSprite::StaticClass())
					.ObjectPath(this, &SLayerArtInspector::GetSpriteObjectPath)
					.OnObjectChanged(this, &SLayerArtInspector::HandleSpriteChanged)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					SNew(STextBlock).Text(LOCTEXT("PlacementLabel", "Layer-local Placement"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
						.IsChecked(this, &SLayerArtInspector::GetScopeState, ELayerArtOffsetScope::CurrentAnimation)
						.OnCheckStateChanged(this, &SLayerArtInspector::SetScope, ELayerArtOffsetScope::CurrentAnimation)
						[SNew(STextBlock).Text(LOCTEXT("CurrentAnimationScope", "This Animation"))]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
					[
						SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
						.IsChecked(this, &SLayerArtInspector::GetScopeState, ELayerArtOffsetScope::AllAnimations)
						.OnCheckStateChanged(this, &SLayerArtInspector::SetScope, ELayerArtOffsetScope::AllAnimations)
						[SNew(STextBlock).Text(LOCTEXT("AllAnimationScope", "Default"))]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<float>).LabelVAlign(VAlign_Center)
						.Label()[SNew(STextBlock).Text(LOCTEXT("OffsetX", "X"))]
						.Value(this, &SLayerArtInspector::GetOffsetComponent, true)
						.OnValueCommitted(this, &SLayerArtInspector::CommitOffsetComponent, true)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SNumericEntryBox<float>).LabelVAlign(VAlign_Center)
						.Label()[SNew(STextBlock).Text(LOCTEXT("OffsetY", "Y"))]
						.Value(this, &SLayerArtInspector::GetOffsetComponent, false)
						.OnValueCommitted(this, &SLayerArtInspector::CommitOffsetComponent, false)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(2.0f, 1.0f))
					.Text(LOCTEXT("ResetPlacement", "Reset placement"))
					.OnClicked(this, &SLayerArtInspector::ResetPlacement)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LocalGeometryNoteCompact", "Placement affects art only"))
					.ToolTipText(LOCTEXT("LocalGeometryNote", "Hitboxes, sockets, and Frame Cues remain owned by this source layer. Moving art changes placement only; local geometry is not rewritten."))
					.AccessibleText(LOCTEXT("LocalGeometryAccessible", "Placement affects art only. Hitboxes, sockets, and Frame Cues remain owned by this source layer; local geometry is not rewritten."))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
		]
	];
}

FText SLayerArtInspector::GetLayerTitle() const
{
	const FCharacterLayer* Layer = LayerAsset.IsValid() && Model.IsValid()
		? LayerAsset->GetLayerById(Model->GetSelectedLayerId()) : nullptr;
	return Layer ? FText::FromString(Layer->LayerName) : LOCTEXT("NoLayer", "No Layer Selected");
}

FText SLayerArtInspector::GetMappingSummary() const
{
	if (!Model.IsValid()) return LOCTEXT("NoProfile", "Choose a Character Profile and layer to author art.");
	const FFlipbookProfileEntry* Animation = ResolveAnimation(*Model);
	return Animation
		? FText::Format(LOCTEXT("MappingSummary", "{0} · frame {1}. Preview is temporary; the Default Appearance controls fixed publishing."),
			FText::FromString(Animation->Identity.FlipbookName), FText::AsNumber(Model->GetSelectedFrameIndex() + 1))
		: LOCTEXT("NoAnimation", "Choose an animation to edit this layer's art mapping.");
}

FString SLayerArtInspector::GetSpriteObjectPath() const
{
	const FCharacterLayer* Layer = LayerAsset.IsValid() && Model.IsValid()
		? LayerAsset->GetLayerById(Model->GetSelectedLayerId()) : nullptr;
	const FFlipbookProfileEntry* Animation = Model.IsValid() ? ResolveAnimation(*Model) : nullptr;
	if (!Layer || !Animation) return FString();
	const FCharacterLayerAnimationMapping* Mapping = Layer->AnimationSprites.FindByPredicate(
		[Animation](const FCharacterLayerAnimationMapping& Candidate)
		{
			return MappingMatches(Candidate, *Animation);
		});
	const int32 FrameIndex = Model->GetSelectedFrameIndex();
	return Mapping && Mapping->Sprites.IsValidIndex(FrameIndex)
		? Mapping->Sprites[FrameIndex].ToSoftObjectPath().ToString() : FString();
}

void SLayerArtInspector::HandleSpriteChanged(const FAssetData& AssetData)
{
	if (!LayerAsset.IsValid() || !Model.IsValid()) return;
	UPaperSprite* Sprite = Cast<UPaperSprite>(AssetData.GetAsset());
	const FString CurrentPath = GetSpriteObjectPath();
	const FString NewPath = Sprite ? Sprite->GetPathName() : FString();
	if (CurrentPath.Equals(NewPath, ESearchCase::CaseSensitive)) return;
	const FScopedTransaction Transaction(LOCTEXT("SetLayerFrameSprite", "Set Layer Frame Sprite"));
	LayerAsset->Modify();
	if (SetFrameSprite(*LayerAsset, *Model, Sprite))
	{
		LayerAsset->MarkPackageDirty();
		Model->NotifyAssetDataChanged();
	}
}

TOptional<float> SLayerArtInspector::GetOffsetComponent(bool bX) const
{
	if (!LayerAsset.IsValid() || !Model.IsValid()) return TOptional<float>();
	const FVector2D Value = GetPlacement(*LayerAsset, *Model, OffsetScope);
	return bX ? Value.X : Value.Y;
}

void SLayerArtInspector::CommitOffsetComponent(float Value, ETextCommit::Type, bool bX)
{
	if (!LayerAsset.IsValid() || !Model.IsValid()) return;
	FVector2D NewValue = GetPlacement(*LayerAsset, *Model, OffsetScope);
	if (bX) NewValue.X = Value; else NewValue.Y = Value;
	if (GetPlacement(*LayerAsset, *Model, OffsetScope).Equals(NewValue)) return;
	const FScopedTransaction Transaction(LOCTEXT("SetLayerPlacement", "Set Layer Placement"));
	LayerAsset->Modify();
	if (SetPlacement(*LayerAsset, *Model, OffsetScope, NewValue))
	{
		LayerAsset->MarkPackageDirty();
		Model->NotifyAssetDataChanged();
	}
}

FReply SLayerArtInspector::ResetPlacement()
{
	if (!LayerAsset.IsValid() || !Model.IsValid()) return FReply::Handled();
	if (GetPlacement(*LayerAsset, *Model, OffsetScope).IsNearlyZero()) return FReply::Handled();
	const FScopedTransaction Transaction(LOCTEXT("ResetLayerPlacementTransaction", "Reset Layer Placement"));
	LayerAsset->Modify();
	if (SetPlacement(*LayerAsset, *Model, OffsetScope, FVector2D::ZeroVector))
	{
		LayerAsset->MarkPackageDirty();
		Model->NotifyAssetDataChanged();
	}
	return FReply::Handled();
}

ECheckBoxState SLayerArtInspector::GetScopeState(ELayerArtOffsetScope Scope) const
{
	return OffsetScope == Scope ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SLayerArtInspector::SetScope(ECheckBoxState State, ELayerArtOffsetScope Scope)
{
	if (State == ECheckBoxState::Checked) OffsetScope = Scope;
}

#undef LOCTEXT_NAMESPACE
