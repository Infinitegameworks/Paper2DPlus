// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FlipbookListBuilder.h"
#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FlipbookListBuilder"

void FFlipbookListBuilder::Build(
	TSharedPtr<SVerticalBox> ListBox,
	TSharedPtr<FCharacterProfileEditorModel> Model,
	TFunction<TSharedRef<SWidget>(int32)> ItemBuilder,
	TFunction<void()> OnCollapseChanged,
	TFunction<bool(int32)> Filter)
{
	if (!ListBox.IsValid() || !Model.IsValid()) return;
	UPaper2DPlusCharacterProfileAsset* Asset = Model->GetAsset();
	if (!Asset) return;

	TArray<int32> SortedIndices = Model->GetSortedFlipbookIndices();
	TMap<FName, TArray<int32>> FlipbooksByGroup;
	for (int32 i : SortedIndices)
	{
		if (Filter && !Filter(i)) continue;
		FlipbooksByGroup.FindOrAdd(Asset->Flipbooks[i].FlipbookGroup).Add(i);
	}

	if (FlipbooksByGroup.Num() <= 1 && FlipbooksByGroup.Contains(NAME_None))
	{
		const TArray<int32>& Indices = FlipbooksByGroup[NAME_None];
		for (int32 Idx : Indices)
		{
			ListBox->AddSlot().AutoHeight()[ItemBuilder(Idx)];
		}
		return;
	}

	TMap<FName, TArray<const FFlipbookGroupInfo*>> Tree = Asset->GetFlipbookGroupTree();

	TFunction<int32(FName)> CountNestedFlipbooks = [&](FName GName) -> int32
	{
		int32 Count = 0;
		if (const TArray<int32>* Indices = FlipbooksByGroup.Find(GName))
		{
			Count = Indices->Num();
		}
		if (const TArray<const FFlipbookGroupInfo*>* Children = Tree.Find(GName))
		{
			for (const FFlipbookGroupInfo* Child : *Children)
			{
				Count += CountNestedFlipbooks(Child->GroupName);
			}
		}
		return Count;
	};

	TWeakPtr<FCharacterProfileEditorModel> WeakModel = Model;

	TFunction<void(FName, int32)> RenderGroup = [&](FName GroupName, int32 NestLevel)
	{
		const TArray<int32>* GroupIndices = FlipbooksByGroup.Find(GroupName);
		const TArray<const FFlipbookGroupInfo*>* ChildGroups = GroupName.IsNone() ? nullptr : Tree.Find(GroupName);
		int32 FlipbookCount = GroupIndices ? GroupIndices->Num() : 0;

		if (FlipbookCount == 0 && !ChildGroups)
		{
			return;
		}

		bool bCollapsed = !Filter && Model->GetCollapsedFlipbookGroups().Contains(GroupName);
		FString DisplayName = GroupName.IsNone() ? TEXT("Ungrouped") : GroupName.ToString();
		float LeftIndent = static_cast<float>(NestLevel) * 12.0f;

		FText HeaderText;
		int32 SubGroupCount = ChildGroups ? ChildGroups->Num() : 0;
		if (SubGroupCount > 0)
		{
			int32 TotalCount = CountNestedFlipbooks(GroupName);
			HeaderText = FText::Format(LOCTEXT("GroupHeaderNestedFmt", "{0} ({1} groups, {2} total)"),
				FText::FromString(DisplayName), FText::AsNumber(SubGroupCount), FText::AsNumber(TotalCount));
		}
		else
		{
			HeaderText = FText::Format(LOCTEXT("GroupHeaderFmt", "{0} ({1})"),
				FText::FromString(DisplayName), FText::AsNumber(FlipbookCount));
		}

		ListBox->AddSlot()
		.AutoHeight()
		.Padding(LeftIndent, 4, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([WeakModel, GroupName, OnCollapseChanged]()
			{
				TSharedPtr<FCharacterProfileEditorModel> PinnedModel = WeakModel.Pin();
				if (PinnedModel.IsValid())
				{
					PinnedModel->ToggleGroupCollapse(GroupName);
					if (OnCollapseChanged) OnCollapseChanged();
				}
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(bCollapsed ? TEXT("\x25B6") : TEXT("\x25BC")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(HeaderText)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]
		];

		if (bCollapsed)
		{
			return;
		}

		if (GroupIndices)
		{
			for (int32 Idx : *GroupIndices)
			{
				ListBox->AddSlot()
				.AutoHeight()
				.Padding(LeftIndent + 8.0f, 0, 0, 0)
				[
					ItemBuilder(Idx)
				];
			}
		}

		if (ChildGroups)
		{
			for (const FFlipbookGroupInfo* ChildGroup : *ChildGroups)
			{
				RenderGroup(ChildGroup->GroupName, NestLevel + 1);
			}
		}
	};

	if (FlipbooksByGroup.Contains(NAME_None))
	{
		RenderGroup(NAME_None, 0);
	}

	if (const TArray<const FFlipbookGroupInfo*>* RootGroups = Tree.Find(NAME_None))
	{
		for (const FFlipbookGroupInfo* GroupInfo : *RootGroups)
		{
			RenderGroup(GroupInfo->GroupName, 0);
		}
	}
}

void FEditorContextMenuUtils::ShowSpriteContextMenu(const FSpriteContextMenuParams& Params)
{
	TWeakObjectPtr<UPaperSprite> WeakSprite = Params.Sprite;

	FMenuBuilder MenuBuilder(true, nullptr);

	if (Params.ReferenceFlipbookIndex != INDEX_NONE && Params.ReferenceFrameIndex != INDEX_NONE)
	{
		auto OnSetRef = Params.OnSetReferenceSprite;
		int32 RefFBIdx = Params.ReferenceFlipbookIndex;
		int32 RefFrameIdx = Params.ReferenceFrameIndex;
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> WeakAsset = Params.Asset;

		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("SetFrameAsRef", "Set as Reference Sprite (Frame {0})"), FText::AsNumber(RefFrameIdx)),
			LOCTEXT("SetFrameAsRefTooltip", "Set this frame as the alignment reference sprite"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([OnSetRef, RefFBIdx, RefFrameIdx, WeakAsset]()
				{
					if (WeakAsset.IsValid() && WeakAsset->Flipbooks.IsValidIndex(RefFBIdx) && OnSetRef)
					{
						OnSetRef(RefFBIdx, RefFrameIdx);
					}
				}),
				FCanExecuteAction::CreateLambda([WeakAsset, RefFBIdx]()
				{
					return WeakAsset.IsValid() && WeakAsset->Flipbooks.IsValidIndex(RefFBIdx);
				})
			)
		);

		MenuBuilder.AddMenuSeparator();
	}

	auto OnOpenSprite = Params.OnOpenSpriteEditor;
	auto OnBrowse = Params.OnBrowseInContentBrowser;

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenSpriteAsset", "Open Sprite Asset"),
		LOCTEXT("OpenSpriteAssetTooltip", "Open this sprite asset in the Sprite Editor"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([OnOpenSprite, WeakSprite]()
			{
				if (OnOpenSprite) OnOpenSprite(WeakSprite.Get());
			}),
			FCanExecuteAction::CreateLambda([WeakSprite]()
			{
				return WeakSprite.IsValid();
			})
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("BrowseToSpriteAsset", "Browse to Sprite in Content Browser"),
		LOCTEXT("BrowseToSpriteAssetTooltip", "Sync the Content Browser to this sprite asset"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([OnBrowse, WeakSprite]()
			{
				if (OnBrowse) OnBrowse(WeakSprite.Get());
			}),
			FCanExecuteAction::CreateLambda([WeakSprite]()
			{
				return WeakSprite.IsValid();
			})
		)
	);

	if (Params.OnDeleteFrame
		&& Params.Asset.IsValid() && Params.Asset->Flipbooks.IsValidIndex(Params.SelectedFlipbookIndex)
		&& Params.ContextFrameIndex != INDEX_NONE)
	{
		const int32 CapturedFlipbookIndex = Params.SelectedFlipbookIndex;
		const int32 CapturedFrameIndex = Params.ContextFrameIndex;
		UPaperFlipbook* Flipbook = Params.Asset->Flipbooks[CapturedFlipbookIndex].Identity.Flipbook.LoadSynchronous();
		const bool bCanDelete = Flipbook && Flipbook->GetNumKeyFrames() > 1;
		auto OnDelete = Params.OnDeleteFrame;

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteFrame", "Delete Frame"),
			LOCTEXT("DeleteFrameTooltip", "Permanently delete this frame from the flipbook. Optionally removes the sprite region from the source texture."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([OnDelete, CapturedFlipbookIndex, CapturedFrameIndex]()
				{
					TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
						.Title(LOCTEXT("DeleteFrameTitle", "Delete Frame"))
						.SizingRule(ESizingRule::Autosized)
						.SupportsMaximize(false)
						.SupportsMinimize(false);

					bool bRemoveFromTexture = false;
					bool bConfirmed = false;

					ConfirmWindow->SetContent(
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
						.Padding(16)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("DeleteFrameWarning",
									"Are you sure you want to delete frame {0}?\n\nThis will permanently remove the keyframe from the flipbook\nand all associated hitbox, motion, and event data."),
									FText::AsNumber(CapturedFrameIndex)))
								.AutoWrapText(true)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
							[
								SNew(SCheckBox)
								.OnCheckStateChanged_Lambda([&bRemoveFromTexture](ECheckBoxState State)
								{
									bRemoveFromTexture = (State == ECheckBoxState::Checked);
								})
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RemoveFromTexture", "Also remove sprite region from texture"))
									.ToolTipText(LOCTEXT("RemoveFromTextureTip", "Clear this frame's pixel region in the source texture to transparent. The texture will be re-saved."))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.Text(LOCTEXT("DeleteConfirm", "Delete"))
									.ButtonColorAndOpacity(FLinearColor(0.7f, 0.15f, 0.15f))
									.OnClicked_Lambda([&bConfirmed, &ConfirmWindow]()
									{
										bConfirmed = true;
										ConfirmWindow->RequestDestroyWindow();
										return FReply::Handled();
									})
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.Text(LOCTEXT("DeleteCancel", "Cancel"))
									.OnClicked_Lambda([&ConfirmWindow]()
									{
										ConfirmWindow->RequestDestroyWindow();
										return FReply::Handled();
									})
								]
							]
						]
					);

					GEditor->EditorAddModalWindow(ConfirmWindow);

					if (bConfirmed)
					{
						OnDelete(CapturedFlipbookIndex, CapturedFrameIndex, bRemoveFromTexture);
					}
				}),
				FCanExecuteAction::CreateLambda([bCanDelete]() { return bCanDelete; })
			)
		);
	}

	if (Params.OnDeleteExcludedFrame
		&& Params.Asset.IsValid() && Params.Asset->Flipbooks.IsValidIndex(Params.SelectedFlipbookIndex)
		&& Params.ExcludedFrameIndex != INDEX_NONE)
	{
		const int32 CapturedFlipbookIndex = Params.SelectedFlipbookIndex;
		const int32 CapturedExcludedIndex = Params.ExcludedFrameIndex;
		const auto& ExcludedFrames = Params.Asset->Flipbooks[CapturedFlipbookIndex].CombatData.ExcludedFrames;
		const bool bValidExcluded = ExcludedFrames.IsValidIndex(CapturedExcludedIndex);
		auto OnDeleteExcluded = Params.OnDeleteExcludedFrame;

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteExcludedFrame2", "Delete Frame"),
			LOCTEXT("DeleteExcludedFrameTooltip", "Permanently delete this excluded frame. Optionally removes the sprite region from the source texture."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([OnDeleteExcluded, CapturedFlipbookIndex, CapturedExcludedIndex]()
				{
					TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
						.Title(LOCTEXT("DeleteFrameTitle", "Delete Frame"))
						.SizingRule(ESizingRule::Autosized)
						.SupportsMaximize(false)
						.SupportsMinimize(false);

					bool bRemoveFromTexture = false;
					bool bConfirmed = false;

					ConfirmWindow->SetContent(
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
						.Padding(16)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("DeleteExcludedFrameWarning",
									"Are you sure you want to delete this excluded frame?\n\nThis will permanently remove the frame and all associated data."))
								.AutoWrapText(true)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
							[
								SNew(SCheckBox)
								.OnCheckStateChanged_Lambda([&bRemoveFromTexture](ECheckBoxState State)
								{
									bRemoveFromTexture = (State == ECheckBoxState::Checked);
								})
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RemoveFromTexture", "Also remove sprite region from texture"))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.Text(LOCTEXT("DeleteConfirm", "Delete"))
									.ButtonColorAndOpacity(FLinearColor(0.7f, 0.15f, 0.15f))
									.OnClicked_Lambda([&bConfirmed, &ConfirmWindow]()
									{
										bConfirmed = true;
										ConfirmWindow->RequestDestroyWindow();
										return FReply::Handled();
									})
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.Text(LOCTEXT("DeleteCancel", "Cancel"))
									.OnClicked_Lambda([&ConfirmWindow]()
									{
										ConfirmWindow->RequestDestroyWindow();
										return FReply::Handled();
									})
								]
							]
						]
					);

					GEditor->EditorAddModalWindow(ConfirmWindow);

					if (bConfirmed)
					{
						OnDeleteExcluded(CapturedFlipbookIndex, CapturedExcludedIndex, bRemoveFromTexture);
					}
				}),
				FCanExecuteAction::CreateLambda([bValidExcluded]() { return bValidExcluded; })
			)
		);
	}

	if (Params.AnchorWidget.IsValid())
	{
		FSlateApplication::Get().PushMenu(
			Params.AnchorWidget.ToSharedRef(),
			FWidgetPath(),
			MenuBuilder.MakeWidget(),
			Params.ScreenSpacePosition,
			FPopupTransitionEffect::ContextMenu
		);
	}
}

#undef LOCTEXT_NAMESPACE
