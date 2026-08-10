// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCompletionPanel.h"

#include "CharacterProfileEditorModel.h"
#include "Editor.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
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

#define LOCTEXT_NAMESPACE "CharacterCompletionPanel"

namespace
{
	int32 CountSetBits(int32 Value)
	{
		int32 Count = 0;
		for (uint32 Remaining = static_cast<uint32>(Value); Remaining != 0; Remaining >>= 1)
		{
			Count += Remaining & 1u;
		}
		return Count;
	}
}

const TArray<SCharacterCompletionPanel::FTaskDescriptor>& SCharacterCompletionPanel::GetTasks()
{
	static const TArray<FTaskDescriptor> Tasks = {
		{ 0, LOCTEXT("HitboxesTask", "Hitboxes"), LOCTEXT("HitboxesTaskTip", "Hitbox authoring is complete for this animation.") },
		{ 1, LOCTEXT("AlignmentTask", "Alignment"), LOCTEXT("AlignmentTaskTip", "Sprite alignment is complete for this animation.") },
		{ 2, LOCTEXT("TimingTask", "Timing"), LOCTEXT("TimingTaskTip", "Frame timing is complete for this animation.") },
		{ 5, LOCTEXT("MotionTask", "Motion"), LOCTEXT("MotionTaskTip", "Root motion is complete for this animation.") },
		{ 6, LOCTEXT("TagsTask", "Tags"), LOCTEXT("TagsTaskTip", "Animation tags are complete for this animation.") },
	};
	return Tasks;
}

void SCharacterCompletionPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	if (Model.IsValid())
	{
		SelectionChangedHandle = Model->OnFlipbookSelectionChanged.AddSP(
			this, &SCharacterCompletionPanel::HandleSelectionChanged);
		AssetDataChangedHandle = Model->OnAssetDataChanged.AddSP(
			this, &SCharacterCompletionPanel::HandleModelRefresh);
		ExternalModifiedHandle = Model->OnAssetExternallyModified.AddSP(
			this, &SCharacterCompletionPanel::HandleModelRefresh);
	}

	TSharedRef<SUniformGridPanel> TaskGrid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(2.0f, 1.0f));
	const TArray<FTaskDescriptor>& Tasks = GetTasks();
	for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
	{
		const FTaskDescriptor& Task = Tasks[TaskIndex];
		TaskGrid->AddSlot(TaskIndex % 2, TaskIndex / 2)
		[
			SNew(SCheckBox)
			.IsEnabled(this, &SCharacterCompletionPanel::HasSelectedAnimation)
			.IsChecked(this, &SCharacterCompletionPanel::GetTaskCheckState, Task.Bit)
			.OnCheckStateChanged(this, &SCharacterCompletionPanel::HandleTaskCheckStateChanged, Task.Bit)
			.ToolTipText(Task.ToolTip)
			.Padding(FMargin(2.0f))
			[
				SNew(STextBlock)
				.Text(Task.Label)
			]
		];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(this, &SCharacterCompletionPanel::GetHeaderText)
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 6.0f)
				[
					SNew(STextBlock)
					.Text(this, &SCharacterCompletionPanel::GetProgressText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SProgressBar)
					.Percent(this, &SCharacterCompletionPanel::GetProgressFraction)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 7.0f, 0.0f, 4.0f)
				[
					TaskGrid
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.IsEnabled(this, &SCharacterCompletionPanel::HasSelectedAnimation)
						.Text(LOCTEXT("MarkAllButton", "All"))
						.ToolTipText(LOCTEXT("MarkAllButtonTip", "Mark all five completion tasks for the selected animation."))
						.OnClicked(this, &SCharacterCompletionPanel::HandleSetAllClicked, true)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.IsEnabled(this, &SCharacterCompletionPanel::HasSelectedAnimation)
						.Text(LOCTEXT("ClearAllButton", "None"))
						.ToolTipText(LOCTEXT("ClearAllButtonTip", "Clear all five completion tasks for the selected animation."))
						.OnClicked(this, &SCharacterCompletionPanel::HandleSetAllClicked, false)
					]
				]
			]
		]
	];
}

SCharacterCompletionPanel::~SCharacterCompletionPanel()
{
	Shutdown();
}

void SCharacterCompletionPanel::Shutdown()
{
	if (bShutdown)
	{
		return;
	}
	bShutdown = true;
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(SelectionChangedHandle);
		Model->OnAssetDataChanged.Remove(AssetDataChangedHandle);
		Model->OnAssetExternallyModified.Remove(ExternalModifiedHandle);
	}
	SelectionChangedHandle.Reset();
	AssetDataChangedHandle.Reset();
	ExternalModifiedHandle.Reset();
	Model.Reset();
}

void SCharacterCompletionPanel::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		HandleModelRefresh();
	}
}

void SCharacterCompletionPanel::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

int32 SCharacterCompletionPanel::GetDisplayedTaskCountForTests() const
{
	return GetTasks().Num();
}

int32 SCharacterCompletionPanel::GetSelectedCompletionMaskForTests() const
{
	return GetSelectedCompletionFlags() & UPaper2DPlusCharacterProfileAsset::LiveTaskBits;
}

bool SCharacterCompletionPanel::SetTaskCompletedForTests(int32 TaskBit, bool bCompleted)
{
	const bool bKnownTask = GetTasks().ContainsByPredicate(
		[TaskBit](const FTaskDescriptor& Task) { return Task.Bit == TaskBit; });
	if (!bKnownTask)
	{
		return false;
	}

	const int32 BitMask = 1 << TaskBit;
	const int32 CurrentFlags = GetSelectedCompletionFlags();
	const int32 NewFlags = bCompleted ? (CurrentFlags | BitMask) : (CurrentFlags & ~BitMask);
	return MutateSelectedCompletionFlags(NewFlags, LOCTEXT("ToggleCompletionTransaction", "Set Animation Completion"));
}

bool SCharacterCompletionPanel::SetAllCompletedForTests(bool bCompleted)
{
	return MutateSelectedCompletionFlags(
		bCompleted ? UPaper2DPlusCharacterProfileAsset::LiveTaskBits : 0,
		bCompleted
			? LOCTEXT("MarkAllCompleteTransaction", "Mark Animation Complete")
			: LOCTEXT("ClearAllCompleteTransaction", "Clear Animation Completion"));
}

int32 SCharacterCompletionPanel::GetModelSubscriptionCountForTests() const
{
	return static_cast<int32>(SelectionChangedHandle.IsValid())
		+ static_cast<int32>(AssetDataChangedHandle.IsValid())
		+ static_cast<int32>(ExternalModifiedHandle.IsValid());
}

FProfileAnimationIdentity SCharacterCompletionPanel::GetSelectedAnimationIdentity() const
{
	UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	return Paper2DPlusProfileToolProvider::MakeAnimationIdentity(
		Asset, Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE);
}

int32 SCharacterCompletionPanel::GetLiveSelectedAnimationIndex() const
{
	UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	const int32 SelectedIndex = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
	return Asset && Asset->Flipbooks.IsValidIndex(SelectedIndex) ? SelectedIndex : INDEX_NONE;
}

bool SCharacterCompletionPanel::HasSelectedAnimation() const
{
	return GetLiveSelectedAnimationIndex() != INDEX_NONE;
}

int32 SCharacterCompletionPanel::GetSelectedCompletionFlags() const
{
	UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	const int32 FlipbookIndex = GetLiveSelectedAnimationIndex();
	return Asset && Asset->Flipbooks.IsValidIndex(FlipbookIndex)
		? Asset->Flipbooks[FlipbookIndex].EditorMeta.CompletionFlags
		: 0;
}

int32 SCharacterCompletionPanel::GetCompletedTaskCount() const
{
	return CountSetBits(GetSelectedCompletionFlags() & UPaper2DPlusCharacterProfileAsset::LiveTaskBits);
}

FText SCharacterCompletionPanel::GetHeaderText() const
{
	if (!Model.IsValid() || !HasSelectedAnimation())
	{
		return LOCTEXT("NoAnimationHeader", "Completion");
	}
	const FName SelectedName = Model->GetSelectedFlipbookName();
	return SelectedName.IsNone()
		? LOCTEXT("CompletionHeader", "Completion")
		: FText::Format(LOCTEXT("CompletionHeaderFormat", "Completion — {0}"), FText::FromName(SelectedName));
}

FText SCharacterCompletionPanel::GetProgressText() const
{
	if (!HasSelectedAnimation())
	{
		return LOCTEXT("NoAnimationProgress", "Select an animation to track its authoring progress.");
	}
	return FText::Format(
		LOCTEXT("ProgressFormat", "{0} of {1} tasks complete"),
		FText::AsNumber(GetCompletedTaskCount()),
		FText::AsNumber(UPaper2DPlusCharacterProfileAsset::LiveTaskCount));
}

TOptional<float> SCharacterCompletionPanel::GetProgressFraction() const
{
	return HasSelectedAnimation()
		? TOptional<float>(GetCompletedTaskCount() / static_cast<float>(UPaper2DPlusCharacterProfileAsset::LiveTaskCount))
		: TOptional<float>(0.0f);
}

ECheckBoxState SCharacterCompletionPanel::GetTaskCheckState(int32 TaskBit) const
{
	return (GetSelectedCompletionFlags() & (1 << TaskBit)) != 0
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SCharacterCompletionPanel::HandleTaskCheckStateChanged(ECheckBoxState NewState, int32 TaskBit)
{
	SetTaskCompletedForTests(TaskBit, NewState == ECheckBoxState::Checked);
}

FReply SCharacterCompletionPanel::HandleSetAllClicked(bool bCompleted)
{
	SetAllCompletedForTests(bCompleted);
	return FReply::Handled();
}

bool SCharacterCompletionPanel::MutateSelectedCompletionFlags(int32 NewFlags, const FText& TransactionText)
{
	UPaper2DPlusCharacterProfileAsset* Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
	const FProfileAnimationIdentity Identity = GetSelectedAnimationIdentity();
	const int32 FlipbookIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Asset, Identity);
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return false;
	}

	int32& CompletionFlags = Asset->Flipbooks[FlipbookIndex].EditorMeta.CompletionFlags;
	if (CompletionFlags == NewFlags)
	{
		return false;
	}

	const FScopedTransaction Transaction(TransactionText);
	Asset->SetFlags(RF_Transactional);
	Asset->Modify();
	CompletionFlags = NewFlags;
	Asset->MarkPackageDirty();
	Model->NotifyAssetDataChanged();
	return true;
}

void SCharacterCompletionPanel::HandleSelectionChanged(int32 NewIndex)
{
	HandleModelRefresh();
}

void SCharacterCompletionPanel::HandleModelRefresh()
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

#undef LOCTEXT_NAMESPACE
