// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileDetailsPanel.h"
#include "CharacterProfileEditorModel.h"
#include "EditorCanvasUtils.h"
#include "ProfilePropertyRow.h"
#include "ProfileSpriteBoundsService.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "AnimationMapCore.h"
#include "AnimationTagChipUtils.h"
#include "PaperZDSequenceAuthoring.h"
#include "ProfileToolPanelProvider.h"
#include "DestructiveActionUtils.h"
// SGameplayTagCombo was added in UE 5.3
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagCombo.h"
#include "SGameplayTagPicker.h"
#endif
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Application/SlateApplication.h"
#include "GameplayTagsEditorModule.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ProfileDetailsPanel"

namespace DestructiveActions = Paper2DPlusEditor::DestructiveActionUtils;

// ==========================================
// CONSTRUCT / DESTRUCT
// ==========================================

void SProfileDetailsPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	PaneMode = InArgs._PaneMode;
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();
	}

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	if (Model.IsValid())
	{
		if (!IsCharacterDataPane())
		{
			ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddSP(
				this, &SProfileDetailsPanel::HandleFlipbookSelectionChanged);
		}
		ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddSP(
			this, &SProfileDetailsPanel::HandleAssetDataChanged);
		ModelAssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddSP(
			this, &SProfileDetailsPanel::HandleAssetExternallyModified);
		if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
			|| PaneMode == EProfileDetailsPaneMode::Transitions)
		{
			ModelTransitionSelectionHandle = Model->OnTransitionSelectionChanged.AddSP(
				this, &SProfileDetailsPanel::HandleTransitionSelectionChanged);
		}
	}

	// Only the requested slice is constructed. The compatibility FlipbookFocus surface keeps all three
	// flipbook sections for legacy embedded hosts; the workspace descriptors use the focused modes.

	TSharedRef<SVerticalBox> Sections = SNew(SVerticalBox);

	if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus)
	{
		// Details (flipbook details)
		Sections->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.HeaderPadding(FMargin(4, 2))
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DetailsSection", "Details"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			.BodyContent()
			[
				BuildDetailsPanel()
			]
		];

		// Move Transitions (TASK-76)
		Sections->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.HeaderPadding(FMargin(4, 2))
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MoveTransitionsSection", "Move Transitions"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			.BodyContent()
			[
				BuildTransitionsContextPanel()
			]
		];

		Sections->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.HeaderPadding(FMargin(4, 2))
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AnimationTagsSection", "Tags"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			.BodyContent()
			[
				BuildTagsPanel()
			]
		];
	}
	else if (PaneMode == EProfileDetailsPaneMode::FlipbookDetails)
	{
		Sections->AddSlot()
		.FillHeight(1.0f)
		[
			BuildFlipbookDetailsView()
		];
	}
	else if (PaneMode == EProfileDetailsPaneMode::Transitions)
	{
		Sections->AddSlot()
		.FillHeight(1.0f)
		[
			BuildTransitionsContextPanel()
		];
	}
	else if (PaneMode == EProfileDetailsPaneMode::Tags)
	{
		Sections->AddSlot()
		.FillHeight(1.0f)
		[
			BuildTagsPanel()
		];
	}
	else if (PaneMode == EProfileDetailsPaneMode::Character)
	{
		// Profile-wide Sprite Bounds audit/repair. The expensive pixel scan runs only from the button;
		// the header lambda reads the cached result and is safe for paint/layout invalidation.
		Sections->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.HeaderPadding(FMargin(4, 2))
			.HeaderContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SpriteBoundsSection", "Sprite Bounds"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(this, &SProfileDetailsPanel::GetProfileSpriteBoundsSummary)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			.BodyContent()
			[
				BuildSpriteBoundsPanel()
			]
		];

		// Relative Transform
		Sections->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			BuildRelativeTransformSection()
		];
	}
	else if (PaneMode == EProfileDetailsPaneMode::PaperZDSequences)
	{
		// PaperZD Anim Source, sequence creation, and source/match health.
		Sections->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.HeaderPadding(FMargin(4, 2))
			.HeaderContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PZDAnimSourceSection", "PaperZD Anim Source"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() -> FText
					{
						if (!Asset.IsValid()) return FText::GetEmpty();
						int32 Matched = 0;
						const int32 Total = Asset->Flipbooks.Num();
						for (const FFlipbookProfileEntry& FB : Asset->Flipbooks)
						{
							if (FB.Identity.PaperZDSequence) Matched++;
						}
						return FText::FromString(FString::Printf(TEXT("%d / %d matched"), Matched, Total));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
				]
			]
			.BodyContent()
			[
				BuildPaperZDAnimSourcePanel()
			]
		];

	}
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(8)
		[
			Sections
		]
	];

	// Initial population
	RefreshPaperZDSequencesList();
	RefreshProfileSpriteBoundsResults();
	RefreshTransitionsList();
	RefreshAnimationTagsPanel();
	RefreshEdgeModeSection();
}

void SProfileDetailsPanel::HandleFlipbookSelectionChanged(int32)
{
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();
	}
	// A typed/arrowed numeric edit belongs to the owner captured when editing began. A selection
	// change must neither retarget nor cancel it while that stable owner still resolves. Impact rows
	// are likewise intentionally persistent so their Focus buttons can walk every affected owner.
	ReconcileDirectionalTransientState();
	++SelectionRefreshCount;
	if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
		|| PaneMode == EProfileDetailsPaneMode::Transitions)
	{
		RefreshTransitionsList();
		RefreshEdgeModeSection();
	}
	if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
		|| PaneMode == EProfileDetailsPaneMode::Tags)
	{
		RefreshAnimationTagsPanel();
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

bool SProfileDetailsPanel::IsCharacterDataPane() const
{
	return PaneMode == EProfileDetailsPaneMode::Character
		|| PaneMode == EProfileDetailsPaneMode::PaperZDSequences;
}

void SProfileDetailsPanel::RefreshCharacterDataPane()
{
	if (PaneMode == EProfileDetailsPaneMode::Character)
	{
		InvalidateProfileSpriteBoundsReport();
	}
	else if (PaneMode == EProfileDetailsPaneMode::PaperZDSequences)
	{
		RefreshPaperZDSequencesList();
	}
}

void SProfileDetailsPanel::HandleAssetDataChanged()
{
	// Self-echo from this panel's own write: the mutation site owns its single rebuild, and the
	// committing widget must survive its own callback (see bPanelWriteInProgress).
	if (bPanelWriteInProgress)
	{
		return;
	}
	++DirectionalModelGeneration;
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();
	}
	ReconcileDirectionalTransientState();
	if (IsCharacterDataPane())
	{
		RefreshCharacterDataPane();
	}
	if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
		|| PaneMode == EProfileDetailsPaneMode::Transitions)
	{
		RefreshTransitionsList();
		// The data changed under the value key — drop it if its row vanished, then rebuild the
		// edge section from live values (never a cached transition-row pointer).
		ValidateTransitionSelection();
		RefreshEdgeModeSection();
	}
	if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
		|| PaneMode == EProfileDetailsPaneMode::Tags)
	{
		RefreshAnimationTagsPanel();
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SProfileDetailsPanel::HandleAssetExternallyModified()
{
	if (ActiveTransaction.IsValid())
	{
		return;
	}
	++DirectionalModelGeneration;
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();
	}
	ReconcileDirectionalTransientState();
	if (IsCharacterDataPane())
	{
		RefreshCharacterDataPane();
	}
	if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
		|| PaneMode == EProfileDetailsPaneMode::Transitions)
	{
		RefreshTransitionsList();
		ValidateTransitionSelection();
		RefreshEdgeModeSection();
	}
	if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
		|| PaneMode == EProfileDetailsPaneMode::Tags)
	{
		RefreshAnimationTagsPanel();
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SProfileDetailsPanel::HandleTransitionSelectionChanged()
{
	++TransitionSelectionRefreshCount;
	RefreshEdgeModeSection();
	Invalidate(EInvalidateWidgetReason::Paint);
}

SProfileDetailsPanel::~SProfileDetailsPanel()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}

	if (Model.IsValid())
	{
		if (ModelFlipbookSelectionHandle.IsValid())
		{
			Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		}
		Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
		Model->OnAssetExternallyModified.Remove(ModelAssetExternallyModifiedHandle);
		if (ModelTransitionSelectionHandle.IsValid())
		{
			Model->OnTransitionSelectionChanged.Remove(ModelTransitionSelectionHandle);
		}
	}
}

// ==========================================
// TRANSACTION SUPPORT
// ==========================================

void SProfileDetailsPanel::BeginTransaction(const FText& Description)
{
	if (!ActiveTransaction.IsValid() && Asset.IsValid())
	{
		ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
		Asset->SetFlags(RF_Transactional);
		Asset->Modify();
	}
}

void SProfileDetailsPanel::EndTransaction()
{
	if (Asset.IsValid())
	{
		Asset->MarkPackageDirty();
	}
	ActiveTransaction.Reset();
}

bool SProfileDetailsPanel::CommitRelativeLocation(const FVector& NewValue)
{
	UPaper2DPlusCharacterProfileAsset* Profile = Asset.Get();
	if (!Profile || Profile->RelativeLocation.Equals(NewValue, 0.0))
	{
		return false;
	}
	TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
	BeginTransaction(LOCTEXT("SetRelativeLocation", "Set Relative Location"));
	Profile->RelativeLocation = NewValue;
	EndTransaction();
	if (Model.IsValid()) Model->NotifyAssetDataChanged();
	return true;
}

bool SProfileDetailsPanel::CommitRelativeRotation(const FRotator& NewValue)
{
	UPaper2DPlusCharacterProfileAsset* Profile = Asset.Get();
	if (!Profile || Profile->RelativeRotation.Equals(NewValue, 0.0))
	{
		return false;
	}
	TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
	BeginTransaction(LOCTEXT("SetRelativeRotation", "Set Relative Rotation"));
	Profile->RelativeRotation = NewValue;
	EndTransaction();
	if (Model.IsValid()) Model->NotifyAssetDataChanged();
	return true;
}

bool SProfileDetailsPanel::CommitRelativeScale(const FVector& NewValue)
{
	UPaper2DPlusCharacterProfileAsset* Profile = Asset.Get();
	if (!Profile || Profile->RelativeScale3D.Equals(NewValue, 0.0))
	{
		return false;
	}
	TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
	BeginTransaction(LOCTEXT("SetRelativeScale", "Set Relative Scale"));
	Profile->RelativeScale3D = NewValue;
	EndTransaction();
	if (Model.IsValid()) Model->NotifyAssetDataChanged();
	return true;
}

void SProfileDetailsPanel::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		++DirectionalModelGeneration;
		if (Model.IsValid())
		{
			Asset = Model->GetAsset();
			// Transaction replay restores the Profile bytes without routing through this panel's
			// normal commit seam. Recompute the shared projection now so all six tools immediately
			// display the restored base/variant art instead of retaining the pre-undo preview.
			Model->NotifyAssetDataChanged();
		}
		ReconcileDirectionalTransientState();
		if (IsCharacterDataPane())
		{
			RefreshCharacterDataPane();
		}
		if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
			|| PaneMode == EProfileDetailsPaneMode::Transitions)
		{
			RefreshTransitionsList();
			// Undo can remove the selected edge's row out from under the value key — fall back.
			ValidateTransitionSelection();
			RefreshEdgeModeSection();
		}
		if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
			|| PaneMode == EProfileDetailsPaneMode::Tags)
		{
			RefreshAnimationTagsPanel();
		}
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SProfileDetailsPanel::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

// ==========================================
// REFRESH
// ==========================================

void SProfileDetailsPanel::RefreshAll()
{
	++DirectionalModelGeneration;
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();
	}
	ResetDirectionalIssue();
	if (IsCharacterDataPane())
	{
		RefreshCharacterDataPane();
	}
	if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
		|| PaneMode == EProfileDetailsPaneMode::Transitions)
	{
		RefreshTransitionsList();
		ValidateTransitionSelection();
		RefreshEdgeModeSection();
	}
	if (PaneMode == EProfileDetailsPaneMode::FlipbookFocus
		|| PaneMode == EProfileDetailsPaneMode::Tags)
	{
		RefreshAnimationTagsPanel();
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SProfileDetailsPanel::RefreshAnimationTagsPanel()
{
	if (!AnimationTagsEditorBox.IsValid())
	{
		RefreshPhaseTagPicker();
		RefreshAnimationChainTagsEditor();
		return;
	}
	++AnimationTagsRefreshCount;

	const int32 SelectedIndex = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedIndex))
	{
		AnimationTagsEditorBox->SetContent(
			SNew(STextBlock)
			.Text(LOCTEXT("AnimationTagsNoSelection", "Select an animation to edit tags"))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground()));
		RefreshPhaseTagPicker();
		RefreshAnimationChainTagsEditor();
		return;
	}

	const FProfileAnimationIdentity AnimationIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Asset.Get(), SelectedIndex);
	const FGameplayTagContainer OwnTags = Asset->Flipbooks[SelectedIndex].EditorMeta.AnimationTags;
	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset.Get());
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* TagSet =
		TagMap.Find(Asset->Flipbooks[SelectedIndex].Identity.FlipbookName.ToLower());
	const TArray<Paper2DPlusAnimationTagChips::FAnimationTagChipItem> ChipItems = TagSet
		? Paper2DPlusAnimationTagChips::BuildChipItems(
			TagSet->OwnTags, TagSet->ChainInheritedTags, TagSet->GroupImpliedTags)
		: TArray<Paper2DPlusAnimationTagChips::FAnimationTagChipItem>();
	const TSharedRef<SWidget> Chips = ChipItems.IsEmpty()
		? StaticCastSharedRef<SWidget>(
			SNew(STextBlock)
			.Text(LOCTEXT("AnimationTagsNone", "(none)"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground()))
		: Paper2DPlusAnimationTagChips::MakeChipsRow(ChipItems, 4);

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	TSharedRef<TWeakPtr<SComboButton>> WeakComboHolder = MakeShared<TWeakPtr<SComboButton>>();
	const TWeakPtr<SProfileDetailsPanel> WeakSelf = SharedThis(this);
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.ContentPadding(FMargin(2.0f, 1.0f))
		.HasDownArrow(true)
		.ToolTipText(LOCTEXT(
			"AnimationTagsEditorTip",
			"Edit this animation's own Paper2DPlus.Animation tags. Solid tags are authored here; ghosted and outlined tags are inherited provenance."))
		.OnGetMenuContent_Lambda(
			[AnimationIdentity, OwnTags, WeakComboHolder, WeakSelf]() -> TSharedRef<SWidget>
			{
				return SNew(SBox)
					.MinDesiredWidth(320.0f)
					.Padding(2.0f)
					[
						// A gameplay-tag picker inside an auto-dismiss menu is selection-only. The guard
						// consumes RMB before engine tag-management menus can outlive this menu stack.
						SNew(SMenuHostedTagPickerGuard)
						[
							SNew(SGameplayTagPicker)
							.Filter(TEXT("Paper2DPlus.Animation"))
							.MultiSelect(true)
							.TagContainers(TArray<FGameplayTagContainer>{ OwnTags })
							.OnTagChanged_Lambda(
								[AnimationIdentity, WeakComboHolder, WeakSelf](
									const TArray<FGameplayTagContainer>& Containers)
								{
									const FGameplayTagContainer NewTags = Containers.Num() > 0
										? Containers[0]
										: FGameplayTagContainer();
									if (const TSharedPtr<SComboButton> ComboButton = WeakComboHolder->Pin())
									{
										ComboButton->SetIsOpen(false);
									}
									TFunction<void()> Commit = [AnimationIdentity, NewTags, WeakSelf]()
									{
										if (const TSharedPtr<SProfileDetailsPanel> Self = WeakSelf.Pin())
										{
											Self->CommitAnimationTags(AnimationIdentity, NewTags);
										}
									};
									// The commit rebuilds this widget. Let the picker callback unwind before
									// replacing its open menu, and drop the action if the panel closed first.
									if (GEditor)
									{
										GEditor->GetTimerManager()->SetTimerForNextTick(MoveTemp(Commit));
									}
									else
									{
										Commit();
									}
								})
						]
					];
			})
		.ButtonContent()
		[
			Chips
		];
	*WeakComboHolder = Combo;
	AnimationTagsEditorBox->SetContent(Combo);
#else
	AnimationTagsEditorBox->SetContent(Chips);
#endif

	RefreshPhaseTagPicker();
	RefreshAnimationChainTagsEditor();
}

bool SProfileDetailsPanel::CommitAnimationTags(
	const FProfileAnimationIdentity& AnimationIdentity,
	const FGameplayTagContainer& NewTags)
{
	const int32 LiveIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
		Asset.Get(), AnimationIdentity);
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(LiveIndex)
		|| Asset->Flipbooks[LiveIndex].EditorMeta.AnimationTags == NewTags)
	{
		return false;
	}

	{
		TGuardValue<bool> WriteGuard(bPanelWriteInProgress, true);
		BeginTransaction(LOCTEXT("SetAnimationTags", "Set Animation Tags"));
		Asset->Flipbooks[LiveIndex].EditorMeta.AnimationTags = NewTags;
		EndTransaction();
		if (Model.IsValid())
		{
			Model->NotifyAssetDataChanged();
		}
	}
	RefreshAnimationTagsPanel();
	return true;
}

bool SProfileDetailsPanel::CommitPhaseTag(
	const FProfileAnimationIdentity& AnimationIdentity,
	const FGameplayTag& NewTag)
{
	const int32 LiveIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
		Asset.Get(), AnimationIdentity);
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(LiveIndex)
		|| Asset->Flipbooks[LiveIndex].EditorMeta.PhaseTag == NewTag)
	{
		return false;
	}

	{
		TGuardValue<bool> WriteGuard(bPanelWriteInProgress, true);
		BeginTransaction(LOCTEXT("SetFlipbookPhaseTag", "Set Flipbook Phase Tag"));
		Asset->Flipbooks[LiveIndex].EditorMeta.PhaseTag = NewTag;
		EndTransaction();
		if (Model.IsValid())
		{
			Model->NotifyAssetDataChanged();
		}
	}
	RefreshAnimationTagsPanel();
	return true;
}

bool SProfileDetailsPanel::ResolveAnimationChainStart(
	const FProfileAnimationIdentity& AnimationIdentity,
	FGameplayTag& OutGroupTag,
	int32& OutEntryIndex) const
{
	OutGroupTag = FGameplayTag();
	OutEntryIndex = INDEX_NONE;

	const UPaper2DPlusCharacterProfileAsset* Profile = Asset.Get();
	const int32 AnimationIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
		Profile, AnimationIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(AnimationIndex))
	{
		return false;
	}

	const FString& AnimationName = Profile->Flipbooks[AnimationIndex].Identity.FlipbookName;
	int32 MatchCount = 0;
	bool bMatchedEntryIsChainStart = false;
	for (const TPair<FGameplayTag, FFlipbookTagMapping>& Pair : Profile->TagMappings)
	{
		for (int32 EntryIndex = 0; EntryIndex < Pair.Value.Entries.Num(); ++EntryIndex)
		{
			const FFlipbookTagMappingEntry& Entry = Pair.Value.Entries[EntryIndex];
			if (!Entry.FlipbookName.Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				continue;
			}

			++MatchCount;
			OutGroupTag = Pair.Key;
			OutEntryIndex = EntryIndex;
			bMatchedEntryIsChainStart = Entry.bIsChainStart;
		}
	}

	if (MatchCount != 1 || !OutGroupTag.IsValid() || !bMatchedEntryIsChainStart)
	{
		OutGroupTag = FGameplayTag();
		OutEntryIndex = INDEX_NONE;
		return false;
	}

	return true;
}

bool SProfileDetailsPanel::CommitAnimationChainTags(
	const FProfileAnimationIdentity& AnimationIdentity,
	const FGameplayTag& ExpectedGroupTag,
	const FGameplayTagContainer& NewChainTags)
{
	FGameplayTag LiveGroupTag;
	int32 LiveEntryIndex = INDEX_NONE;
	if (!ResolveAnimationChainStart(AnimationIdentity, LiveGroupTag, LiveEntryIndex)
		|| LiveGroupTag != ExpectedGroupTag)
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Profile = Asset.Get();
	FFlipbookTagMapping* Mapping = Profile
		? Profile->TagMappings.Find(LiveGroupTag)
		: nullptr;
	if (!Mapping || !Mapping->Entries.IsValidIndex(LiveEntryIndex)
		|| Paper2DPlusAnimationTagQuery::AreExactTagSetsEqual(
			Mapping->Entries[LiveEntryIndex].ChainTags, NewChainTags))
	{
		return false;
	}

	TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
	BeginTransaction(LOCTEXT("SetChainTags", "Set Chain Tags"));
	if (!Profile->SetTagMappingEntryChainTags(
		LiveGroupTag, LiveEntryIndex, NewChainTags))
	{
		ActiveTransaction.Reset();
		return false;
	}
	EndTransaction();
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
	RefreshAnimationChainTagsEditor();
	return true;
}

bool SProfileDetailsPanel::CommitSelectedChainTagsForTests(
	const FGameplayTagContainer& NewChainTags)
{
	const int32 SelectedIndex = Model.IsValid()
		? Model->GetSelectedFlipbookIndex()
		: INDEX_NONE;
	const FProfileAnimationIdentity AnimationIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Asset.Get(), SelectedIndex);
	FGameplayTag GroupTag;
	int32 EntryIndex = INDEX_NONE;
	return ResolveAnimationChainStart(AnimationIdentity, GroupTag, EntryIndex)
		&& CommitAnimationChainTags(AnimationIdentity, GroupTag, NewChainTags);
}

TSharedRef<SWidget> SProfileDetailsPanel::BuildAnimationChainTagsEditor(
	const FProfileAnimationIdentity& AnimationIdentity)
{
	auto GetChainTagsSummary = [this, AnimationIdentity]() -> FText
	{
		FGameplayTag GroupTag;
		int32 EntryIndex = INDEX_NONE;
		if (ResolveAnimationChainStart(AnimationIdentity, GroupTag, EntryIndex))
		{
			const FFlipbookTagMapping* Mapping = Asset->TagMappings.Find(GroupTag);
			if (Mapping && Mapping->Entries.IsValidIndex(EntryIndex)
				&& !Mapping->Entries[EntryIndex].ChainTags.IsEmpty())
			{
				TArray<FString> LeafNames;
				for (const FGameplayTag& ChainTag : Mapping->Entries[EntryIndex].ChainTags)
				{
					FString LeafName = ChainTag.ToString();
					int32 LastDotIndex = INDEX_NONE;
					if (LeafName.FindLastChar(TEXT('.'), LastDotIndex))
					{
						LeafName.MidInline(LastDotIndex + 1);
					}
					LeafNames.Add(LeafName);
				}
				LeafNames.Sort();
				return FText::FromString(FString::Join(LeafNames, TEXT(", ")));
			}
		}
		return LOCTEXT("AnimationChainTagsEmpty", "Chain tags…");
	};

	const TWeakPtr<SProfileDetailsPanel> WeakSelf = SharedThis(this);
	TSharedRef<TWeakPtr<SComboButton>> WeakComboHolder = MakeShared<TWeakPtr<SComboButton>>();
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.ContentPadding(FMargin(2.0f, 1.0f))
		.HasDownArrow(true)
		.ToolTipText(LOCTEXT(
			"AnimationChainTagsTip",
			"The chain's own identity container. It is available only for a selected move that belongs to exactly one group and is marked Chain Start in the Animation Map."))
		.OnGetMenuContent_Lambda(
			[AnimationIdentity, WeakComboHolder, WeakSelf]() -> TSharedRef<SWidget>
			{
				const TSharedPtr<SProfileDetailsPanel> Self = WeakSelf.Pin();
				FGameplayTag MenuGroupTag;
				int32 MenuEntryIndex = INDEX_NONE;
				if (!Self.IsValid()
					|| !Self->ResolveAnimationChainStart(
						AnimationIdentity, MenuGroupTag, MenuEntryIndex))
				{
					return SNew(STextBlock)
						.Text(LOCTEXT(
							"AnimationChainTagsNoLongerAvailable",
							"This animation is no longer a uniquely mapped Chain Start."))
						.AutoWrapText(true);
				}

				const FFlipbookTagMapping* Mapping =
					Self->Asset->TagMappings.Find(MenuGroupTag);
				if (!Mapping || !Mapping->Entries.IsValidIndex(MenuEntryIndex))
				{
					return SNullWidget::NullWidget;
				}

				TSharedPtr<FGameplayTagContainer> PickerValue =
					MakeShared<FGameplayTagContainer>(
						Mapping->Entries[MenuEntryIndex].ChainTags);
				FOnSetGameplayTagContainer OnSetTags =
					FOnSetGameplayTagContainer::CreateLambda(
						[AnimationIdentity, MenuGroupTag, WeakComboHolder, WeakSelf](
							const FGameplayTagContainer& InNewChainTags)
						{
							const FGameplayTagContainer NewChainTags = InNewChainTags;
							if (const TSharedPtr<SComboButton> ComboButton =
								WeakComboHolder->Pin())
							{
								ComboButton->SetIsOpen(false);
							}

							TFunction<void()> Commit =
								[AnimationIdentity, MenuGroupTag, NewChainTags, WeakSelf]()
								{
									if (const TSharedPtr<SProfileDetailsPanel> PinnedSelf =
										WeakSelf.Pin())
									{
										PinnedSelf->CommitAnimationChainTags(
											AnimationIdentity,
											MenuGroupTag,
											NewChainTags);
									}
								};
							// The module-level tag widget is the common UE 5.0-5.8 picker seam. Let its
							// callback and menu teardown finish before a successful commit rebuilds
							// this contextual editor.
							if (GEditor)
							{
								GEditor->GetTimerManager()->SetTimerForNextTick(
									MoveTemp(Commit));
							}
							else
							{
								Commit();
							}
						});

				return SNew(SBox)
					.MinDesiredWidth(320.0f)
					.MaxDesiredHeight(420.0f)
					.Padding(2.0f)
					[
						SNew(SMenuHostedTagPickerGuard)
						[
							IGameplayTagsEditorModule::Get()
								.MakeGameplayTagContainerWidget(
									OnSetTags,
									PickerValue,
									TEXT("Paper2DPlus.Animation"))
						]
					];
			})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text_Lambda(GetChainTagsSummary)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.78f)))
		];
	*WeakComboHolder = Combo;
	return Combo;
}

void SProfileDetailsPanel::RefreshAnimationChainTagsEditor()
{
	bHasAnimationChainTagsAuthoringSurface = false;
	if (!ChainTagsSectionBox.IsValid() || !ChainTagsEditorBox.IsValid())
	{
		return;
	}

	const int32 SelectedIndex = Model.IsValid()
		? Model->GetSelectedFlipbookIndex()
		: INDEX_NONE;
	const FProfileAnimationIdentity AnimationIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(
			Asset.Get(), SelectedIndex);
	FGameplayTag GroupTag;
	int32 EntryIndex = INDEX_NONE;
	if (!ResolveAnimationChainStart(AnimationIdentity, GroupTag, EntryIndex))
	{
		ChainTagsSectionBox->SetVisibility(EVisibility::Collapsed);
		ChainTagsEditorBox->SetContent(SNullWidget::NullWidget);
		return;
	}

	bHasAnimationChainTagsAuthoringSurface = true;
	ChainTagsSectionBox->SetVisibility(EVisibility::Visible);
	ChainTagsEditorBox->SetContent(
		BuildAnimationChainTagsEditor(AnimationIdentity));
}

void SProfileDetailsPanel::RefreshPhaseTagPicker()
{
	if (!PhaseTagPickerBox.IsValid())
	{
		return;
	}

	const int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
	const FGameplayTag CurrentTag = (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx))
		? Asset->Flipbooks[Idx].EditorMeta.PhaseTag : FGameplayTag();
	const FProfileAnimationIdentity AnimationIdentity =
		Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Asset.Get(), Idx);
	const TWeakPtr<SProfileDetailsPanel> WeakSelf = SharedThis(this);

	// Combo main-line readout rides the same refresh triggers (selection / data change / undo) —
	// computed HERE, never from a paint-time lambda (FindComboSpineForMove walks the asset).
	if (ComboPositionText.IsValid())
	{
		FText ComboText = LOCTEXT("ComboPositionNone", "Not part of a combo chain");
		if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx))
		{
			const FString& MoveName = Asset->Flipbooks[Idx].Identity.FlipbookName;
			Paper2DPlusComboChain::FScopedAnimationRoot ChainRoot;
			TArray<FString> Spine;
			switch (Paper2DPlusComboChain::FindComboSpineForMove(Asset.Get(), MoveName, ChainRoot, Spine))
			{
			case Paper2DPlusComboChain::EComboSpineFindResult::Found:
			{
				const int32 SpineIndex = Spine.IndexOfByPredicate([&MoveName](const FString& SpineMove)
				{
					return SpineMove.Equals(MoveName, ESearchCase::IgnoreCase);
				});
				const FText GroupLeaf = FText::FromString(
					Paper2DPlusAnimationTagChips::GetTagLeafString(ChainRoot.GroupTag));
				ComboText = SpineIndex >= 0
					? FText::Format(
						LOCTEXT("ComboPositionOnSpineFmt", "Index {0} of {1}  ({2} chain '{3}')"),
						FText::AsNumber(SpineIndex), FText::AsNumber(Spine.Num()),
						GroupLeaf, FText::FromString(ChainRoot.RootMove))
					: FText::Format(
						LOCTEXT("ComboPositionBranchFmt", "Branch of the {0} chain '{1}' (not on the main line)"),
						GroupLeaf, FText::FromString(ChainRoot.RootMove));
				break;
			}
			case Paper2DPlusComboChain::EComboSpineFindResult::AmbiguousChain:
				ComboText = LOCTEXT("ComboPositionAmbiguous", "In multiple combo chains — ambiguous");
				break;
			default:
				break;
			}
		}
		ComboPositionText->SetText(ComboText);
	}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	// STATIC Tag + rebuild. A bound .Tag_Lambda invalidates this always-visible layout continuously;
	// the change callback commits through stable animation identity and then rebuilds the control.
	PhaseTagPickerBox->SetContent(
		SNew(SGameplayTagCombo)
		.Filter(TEXT("Paper2DPlus.Phase"))
		.Tag(CurrentTag)
		.OnTagChanged_Lambda([WeakSelf, AnimationIdentity](const FGameplayTag NewTag)
		{
			if (const TSharedPtr<SProfileDetailsPanel> Self = WeakSelf.Pin())
			{
				Self->CommitPhaseTag(AnimationIdentity, NewTag);
			}
		}));
#else
	PhaseTagPickerBox->SetContent(
		SNew(STextBlock)
		.Text(CurrentTag.IsValid() ? FText::FromName(CurrentTag.GetTagName()) : LOCTEXT("PhaseTagNoneRO", "(none)"))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.78f))));
#endif
}

// ==========================================
// EDGE MODE (TASK-108 U3)
// ==========================================
// The transition row owns its optional phase override; an empty override inherits the target
// animation's EditorMeta.PhaseTag for compatibility. The pane holds NO cached edge state — the model
// owns the lowered (From, To) VALUE key; every read below re-resolves it against the asset, so
// an external rebuild/reorder can never dangle a widget (the Historical-strip liveness rule).

bool SProfileDetailsPanel::ResolveSelectedTransition(int32& OutFromIndex, int32& OutRowIndex, int32& OutTargetIndex) const
{
	OutFromIndex = INDEX_NONE;
	OutRowIndex = INDEX_NONE;
	OutTargetIndex = INDEX_NONE;
	if (!Model.IsValid() || !Asset.IsValid() || !Model->HasSelectedTransition())
	{
		return false;
	}

	const FString& FromLower = Model->GetSelectedTransitionFromLower();
	const FString& ToLower = Model->GetSelectedTransitionToLower();

	// First case-insensitive name match — the data layer's first-match entry contract (the same
	// resolution FindTransitionRowIndex uses for the row, so the two can't disagree).
	for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
	{
		const FString& Name = Asset->Flipbooks[Index].Identity.FlipbookName;
		if (OutFromIndex == INDEX_NONE && Name.Equals(FromLower, ESearchCase::IgnoreCase))
		{
			OutFromIndex = Index;
		}
		if (OutTargetIndex == INDEX_NONE && Name.Equals(ToLower, ESearchCase::IgnoreCase))
		{
			OutTargetIndex = Index;
		}
	}
	if (OutFromIndex == INDEX_NONE)
	{
		return false; // the From move vanished — no edge to show
	}

	OutRowIndex = Paper2DPlusAnimationMap::FindTransitionRowIndex(Asset.Get(), FromLower, ToLower);
	if (OutRowIndex == INDEX_NONE)
	{
		// The row vanished (external delete/undo) — edge mode falls back; OutTargetIndex is
		// irrelevant without a row.
		OutFromIndex = INDEX_NONE;
		OutTargetIndex = INDEX_NONE;
		return false;
	}

	// OutTargetIndex may legitimately be INDEX_NONE here: a stub/dangling target still resolves as an
	// edge and can author its own phase override.
	return true;
}

bool SProfileDetailsPanel::IsShowingResolvedEdgeForTests() const
{
	int32 FromIndex = INDEX_NONE;
	int32 RowIndex = INDEX_NONE;
	int32 TargetIndex = INDEX_NONE;
	return ResolveSelectedTransition(FromIndex, RowIndex, TargetIndex);
}

void SProfileDetailsPanel::ValidateTransitionSelection()
{
	if (!Model.IsValid() || !Model->HasSelectedTransition())
	{
		return;
	}
	int32 FromIdx = INDEX_NONE, RowIdx = INDEX_NONE, TargetIdx = INDEX_NONE;
	if (!ResolveSelectedTransition(FromIdx, RowIdx, TargetIdx))
	{
		// The clear broadcasts OnTransitionSelectionChanged; the model's BroadcastOrDefer queues it
		// safely when we are already inside a broadcast (clear-on-clear no-ops, so no loop).
		Model->ClearSelectedTransition();
	}
}

void SProfileDetailsPanel::RefreshEdgeModeSection()
{
	if (!EdgePhaseTagPickerBox.IsValid())
	{
		return;
	}

	int32 FromIdx = INDEX_NONE, RowIdx = INDEX_NONE, TargetIdx = INDEX_NONE;
	const bool bResolved = ResolveSelectedTransition(FromIdx, RowIdx, TargetIdx);
	const bool bTargetReal = bResolved && TargetIdx != INDEX_NONE;
	const FPaper2DPlusMoveTransition* Row = bResolved
		? &Asset->Flipbooks[FromIdx].TransitionData.Transitions[RowIdx]
		: nullptr;
	const FGameplayTag CurrentTag = Row
		? Row->GetEffectivePhaseTag(
			bTargetReal ? Asset->Flipbooks[TargetIdx].EditorMeta.PhaseTag : FGameplayTag())
		: FGameplayTag();
	const TWeakPtr<SProfileDetailsPanel> WeakSelf = SharedThis(this);

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	// STATIC Tag + rebuild — the RefreshPhaseTagPicker idiom (a bound .Tag_Lambda re-invalidates
	// Layout every frame in the always-visible pane). IsEnabled is static too: the box is rebuilt on
	// every key/data change. Stub targets can still author a row-owned override.
	EdgePhaseTagPickerBox->SetContent(
		SNew(SGameplayTagCombo)
		.Filter(TEXT("Paper2DPlus.Phase"))
		.Tag(CurrentTag)
		.IsEnabled(bResolved)
		.OnTagChanged_Lambda([WeakSelf](const FGameplayTag InTag)
		{
			if (const TSharedPtr<SProfileDetailsPanel> Self = WeakSelf.Pin())
			{
				Self->CommitEdgePhaseTag(InTag);
			}
		}));
#else
	EdgePhaseTagPickerBox->SetContent(
		SNew(STextBlock)
		.Text(CurrentTag.IsValid() ? FText::FromName(CurrentTag.GetTagName()) : LOCTEXT("EdgePhaseTagNoneRO", "(none)"))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.78f))));
#endif
}

bool SProfileDetailsPanel::CommitEdgePhaseTag(const FGameplayTag& InPhaseTag)
{
	int32 FromIdx = INDEX_NONE, RowIdx = INDEX_NONE, TargetIdx = INDEX_NONE;
	if (!ResolveSelectedTransition(FromIdx, RowIdx, TargetIdx) || !Asset.IsValid())
	{
		return false;
	}
	FPaper2DPlusMoveTransition& Row =
		Asset->Flipbooks[FromIdx].TransitionData.Transitions[RowIdx];
	if (Row.PhaseTagOverride == InPhaseTag)
	{
		return false; // same-value early-out — no transaction, no dirty
	}

	{
		TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
		BeginTransaction(LOCTEXT("SetTransitionPhase", "Set Transition Phase"));
		Row.PhaseTagOverride = InPhaseTag;
		EndTransaction();
		// The broadcast produces one edge-phase projection delta; the existing pill is re-stamped.
		if (Model.IsValid()) Model->NotifyAssetDataChanged();
	}
	RefreshEdgeModeSection();   // reflect the committed tag (static .Tag won't update on its own)
	return true;
}

bool SProfileDetailsPanel::DeleteSelectedTransition()
{
	int32 FromIdx = INDEX_NONE, RowIdx = INDEX_NONE, TargetIdx = INDEX_NONE;
	if (!ResolveSelectedTransition(FromIdx, RowIdx, TargetIdx) || !Asset.IsValid())
	{
		return false;
	}

	const FString FromName = Asset->Flipbooks[FromIdx].Identity.FlipbookName;
	// Snapshot-validated removal through the standard write funnel (§6): we resolved the row THIS call,
	// so the snapshot is live-equal by construction — the check guards against re-entrant surprises.
	const FPaper2DPlusMoveTransition Expected = Asset->Flipbooks[FromIdx].TransitionData.Transitions[RowIdx];

	bool bRemoved = false;
	{
		TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
		BeginTransaction(LOCTEXT("DeleteTransitionEdge", "Delete Transition"));
		bRemoved = Paper2DPlusAnimationMap::RemoveTransitionRowChecked(Asset.Get(), FromName, RowIdx, Expected);
		EndTransaction();
		if (bRemoved && Model.IsValid())
		{
			Model->NotifyAssetDataChanged(); // the Map reconciles the arrow away; one undo restores it
		}
	}
	if (Model.IsValid())
	{
		Model->ClearSelectedTransition(); // back to flipbook mode (the From move stays selected)
	}
	RefreshTransitionsList();
	RefreshEdgeModeSection();
	return bRemoved;
}

bool SProfileDetailsPanel::CommitTransitionTargetChange(int32 FlipbookIdx, int32 TransIdx, const FString& NewTarget)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIdx))
	{
		return false;
	}
	TArray<FPaper2DPlusMoveTransition>& Trans = Asset->Flipbooks[FlipbookIdx].TransitionData.Transitions;
	if (!Trans.IsValidIndex(TransIdx))
	{
		return false;
	}
	if (Trans[TransIdx].TargetMove == NewTarget)
	{
		return false; // no-op re-pick
	}

	// KTD invariant through the pane path: one row per (From, To) pair — committing a target another
	// row already carries (case-insensitive) is REFUSED with a toast, mirroring the graph's wire
	// refusal. Empty targets stay legal (the authoring intermediate). Note the same-value early-out
	// above is FString == (case-INSENSITIVE), so a case-only re-pick of the row's OWN target is a
	// no-op — matching the pre-seam behavior, and a case-only change is invisible to the lowered
	// projection anyway (pinned by the DuplicateTargetRefused test).
	if (!NewTarget.IsEmpty())
	{
		for (int32 Index = 0; Index < Trans.Num(); ++Index)
		{
			if (Index != TransIdx && Trans[Index].TargetMove.Equals(NewTarget, ESearchCase::IgnoreCase))
			{
				if (FSlateApplication::IsInitialized())
				{
					FNotificationInfo Info(FText::Format(
						LOCTEXT("PaneDuplicateTransitionRefused",
							"A transition {0} \x2192 {1} already exists — each move pair carries one arrow."),
						FText::FromString(Asset->Flipbooks[FlipbookIdx].Identity.FlipbookName),
						FText::FromString(NewTarget)));
					Info.ExpireDuration = 4.0f;
					FSlateNotificationManager::Get().AddNotification(Info);
				}
				return false;
			}
		}
	}

	{
		TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
		BeginTransaction(LOCTEXT("SetMoveTransitionTarget", "Set Move Transition Target"));
		Trans[TransIdx].TargetMove = NewTarget;
		EndTransaction();
		if (Model.IsValid()) Model->NotifyAssetDataChanged();
	}
	return true;
}

// ==========================================
// HELPERS
// ==========================================

TArray<int32> SProfileDetailsPanel::GetSortedFlipbookIndices() const
{
	if (Model.IsValid())
	{
		return Model->GetSortedFlipbookIndices();
	}
	return TArray<int32>();
}

// ==========================================
// DETAILS PANEL (DISPATCHER)
// ==========================================

TSharedRef<SWidget> SProfileDetailsPanel::BuildDetailsPanel()
{
	return BuildFlipbookDetailsView();
}

TSharedRef<SWidget> SProfileDetailsPanel::BuildTransitionsContextPanel()
{
	return SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda([this]()
		{
			// While the model's transition key RESOLVES to a live row, this focused Transitions panel
			// shows the edge-centric view. Re-resolved per paint (value key, never a
			// widget/pointer), so a vanished row falls back to flipbook mode on the very next frame —
			// no blank-out during wholesale graph rebuild churn (the data, not the graph, decides).
			int32 FromIdx = INDEX_NONE, RowIdx = INDEX_NONE, TargetIdx = INDEX_NONE;
			if (ResolveSelectedTransition(FromIdx, RowIdx, TargetIdx))
			{
				return 1;
			}
			return 0;
		})
		+ SWidgetSwitcher::Slot() [ BuildTransitionsPanel() ]
		+ SWidgetSwitcher::Slot() [ BuildEdgeModeView() ];
}

TSharedRef<SWidget> SProfileDetailsPanel::BuildTagsPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const int32 Index = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
				return Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Index)
					? FText::FromString(Asset->Flipbooks[Index].Identity.FlipbookName)
					: LOCTEXT("TagsNoAnimationHeader", "No animation selected");
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6.0f, 5.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AnimationTagsLabel", "Animation tags"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(AnimationTagsEditorBox, SBox)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT(
						"AnimationTagsProvenanceHelp",
						"Solid = authored here. Ghosted = inherited from a chain start. Outlined = implied by a tag mapping."))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6.0f, 5.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PhaseTagLabel", "Phase tag"))
					.ToolTipText(LOCTEXT(
						"PhaseTagTip",
						"Descriptive Paper2DPlus.Phase tag shown on Map pills, list tints, and Frame Data."))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(PhaseTagPickerBox, SBox)
				]
			]
		]

		// Chain identity belongs to the opener, not to every move. The complete section is hidden
		// unless the selected animation resolves to exactly one mapping entry and that exact entry is
		// a Chain Start. Start/End flags remain Map gestures.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 6.0f)
		[
			SAssignNew(ChainTagsSectionBox, SBox)
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("ChainTagsLabel", "Chain tags"),
					SAssignNew(ChainTagsEditorBox, SBox),
					LOCTEXT(
						"ChainTagsLabelTip",
						"Identity tags for this combo opener. Mark Chain Start and Chain End from the Animation Map."))
			]
		]

		// Read-only derived combo position — the SAME numbering Get Combo Chain Flipbook at Index returns (the
		// details answer to "what index is this move?"). Refreshed with the phase picker (selection /
		// data change / undo), never computed from paint.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6.0f, 5.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ComboPositionLabel", "Combo"))
					.ToolTipText(LOCTEXT(
						"ComboPositionTip",
						"Derived combo main-line position. The index matches the Get Combo Chain Flipbook at Index Blueprint node: the chain opener plus this index returns this animation."))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(ComboPositionText, STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.78f)))
					.AutoWrapText(true)
				]
			]
		];
}

// ==========================================
// EDGE MODE VIEW (TASK-108 U3)
// ==========================================

TSharedRef<SWidget> SProfileDetailsPanel::BuildEdgeModeView()
{
	// Everything below re-resolves the model's value key per paint — no captured indices, no captured
	// row pointers (liveness rule). The phase picker itself lives in EdgePhaseTagPickerBox and is
	// REBUILT (static .Tag) by RefreshEdgeModeSection on every key/data change.

	auto ResolveOrNone = [this](int32& FromIdx, int32& RowIdx, int32& TargetIdx) -> bool
	{
		FromIdx = INDEX_NONE; RowIdx = INDEX_NONE; TargetIdx = INDEX_NONE;
		return ResolveSelectedTransition(FromIdx, RowIdx, TargetIdx);
	};

	return SNew(SVerticalBox)

		// Header: "Transition" caption + From → To with jump buttons
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EdgeModeCaption", "Transition"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)

			// Jump to From
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("EdgeJumpFromTip", "Select the From flipbook (leaves edge mode)"))
				.OnClicked_Lambda([this, ResolveOrNone]()
				{
					int32 FromIdx, RowIdx, TargetIdx;
					if (ResolveOrNone(FromIdx, RowIdx, TargetIdx) && Model.IsValid())
					{
						// Clear FIRST for explicit intent (harmless redundancy: SetSelectedFlipbook
						// also clears the key up front since the PR #224 hoist).
						Model->ClearSelectedTransition();
						Model->SetSelectedFlipbook(FromIdx);
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.Text_Lambda([this, ResolveOrNone]()
					{
						int32 FromIdx, RowIdx, TargetIdx;
						if (ResolveOrNone(FromIdx, RowIdx, TargetIdx))
						{
							return FText::FromString(Asset->Flipbooks[FromIdx].Identity.FlipbookName);
						}
						return FText::GetEmpty();
					})
				]
			]

			// Arrow
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("\x2192")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]

			// Jump to To (stub target: no flipbook to select — just leave edge mode, sanely)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("EdgeJumpToTip", "Select the To flipbook (leaves edge mode). A missing target only clears the edge selection."))
				.OnClicked_Lambda([this, ResolveOrNone]()
				{
					int32 FromIdx, RowIdx, TargetIdx;
					if (ResolveOrNone(FromIdx, RowIdx, TargetIdx) && Model.IsValid())
					{
						Model->ClearSelectedTransition();
						if (TargetIdx != INDEX_NONE)
						{
							// The Map follows the model (HandleModelFlipbookSelectionChanged) and
							// focuses the target's node; a stub target has no flipbook — the clear
							// above already returned the pane to From-flipbook mode (sane minimum).
							Model->SetSelectedFlipbook(TargetIdx);
						}
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.Text_Lambda([this, ResolveOrNone]()
					{
						int32 FromIdx, RowIdx, TargetIdx;
						if (ResolveOrNone(FromIdx, RowIdx, TargetIdx))
						{
							// Stored case for stubs (there is no entry to read authored case from).
							return FText::FromString(
								Asset->Flipbooks[FromIdx].TransitionData.Transitions[RowIdx].TargetMove);
						}
						return FText::GetEmpty();
					})
				]
			]
		]

		// Transition-owned phase override; empty inherits the target animation phase.
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 4))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("EdgePhaseLabel", "Transition phase"))
						.ToolTipText(LOCTEXT("EdgePhaseTip", "The phase owned by this transition. Clearing it inherits the target animation's phase tag for compatibility."))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.40f, 0.45f)))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SAssignNew(EdgePhaseTagPickerBox, SBox)
					]
				]
				// Stub/dangling targets can still carry an explicit transition phase.
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EdgeTargetMissing", "Target missing — set a transition phase override, or leave it empty until the target exists."))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.7f, 0.35f)))
					.AutoWrapText(true)
					.Visibility_Lambda([this, ResolveOrNone]()
					{
						int32 FromIdx, RowIdx, TargetIdx;
						return (ResolveOrNone(FromIdx, RowIdx, TargetIdx) && TargetIdx == INDEX_NONE)
							? EVisibility::Visible : EVisibility::Collapsed;
					})
				]
			]
		]

		// Delete Transition (no confirm — a single row; one undo restores it)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("EdgeDeleteTip", "Remove this transition row (single row — undo restores it)"))
			.OnClicked_Lambda([this]()
			{
				DeleteSelectedTransition();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EdgeDeleteBtn", "Delete Transition"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.4f, 0.4f)))
			]
		];
}

// ==========================================
// FLIPBOOK DETAILS VIEW
// ==========================================

TSharedRef<SWidget> SProfileDetailsPanel::BuildFlipbookDetailsView()
{
	// These rows expose derived, read-only facts about the selected animation. They still use the
	// shared details row so the pane reads as one table with the authored surfaces around it —
	// only the value color marks them as non-authored (see FProfilePropertyRowUtils::MakeFactRow).
	auto MakeInfoRow = [this](const FText& Label, TFunction<FText()> ValueFunc, TFunction<EVisibility()> VisFunc = nullptr) -> TSharedRef<SWidget>
	{
		return FProfilePropertyRowUtils::MakeFactRow(
			Label,
			MakeAttributeLambda(MoveTemp(ValueFunc)),
			FText::GetEmpty(),
			TAttribute<EVisibility>::CreateLambda([this, VisFunc]()
			{
				int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
				if (!(Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx))) return EVisibility::Collapsed;
				return VisFunc ? VisFunc() : EVisibility::Visible;
			}));
	};

	return SNew(SVerticalBox)

		// Placeholder
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SelectFlipbookHint", "Select an animation to see its details."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Font(FProfilePropertyRowUtils::GetPropertyFont())
			.Margin(FMargin(8, 6))
			.AutoWrapText(true)
			.Visibility_Lambda([this]()
			{
				int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
				return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx)) ? EVisibility::Collapsed : EVisibility::Visible;
			})
		]

		// Flipbook name
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 2)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
				return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx))
					? FText::FromString(Asset->Flipbooks[Idx].Identity.FlipbookName) : FText::GetEmpty();
			})
			.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
			.Visibility_Lambda([this]()
			{
				int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
				return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx)) ? EVisibility::Visible : EVisibility::Collapsed;
			})
		]

		// Profile defaults and selected-animation Directional Set authoring. Every value/visibility
		// attribute resolves live data so selection, undo, and external edits never leave stale controls.
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildDirectionalAnimationSection()
		]

		// --- Timing section ---
		// No dark "shadow box" wrapper: the rows carry the shared grid, and the section title
		// plus the category header already scope them.
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
				return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx)) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					FProfilePropertyRowUtils::MakeSectionTitle(LOCTEXT("TimingSectionTitle", "Timing"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeInfoRow(LOCTEXT("LblFrames", "Frames"), [this]()
					{
						int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return FText::GetEmpty();
						const FFlipbookProfileEntry& FB = Asset->Flipbooks[Idx];
						UPaperFlipbook* LoadedFB = FB.Identity.Flipbook.IsValid() ? FB.Identity.Flipbook.Get() : nullptr;
						if (LoadedFB && LoadedFB->GetFramesPerSecond() > 0.0f)
							return FText::FromString(FString::Printf(TEXT("%d   %.0f FPS   %.2fs"), FB.CombatData.Frames.Num(), LoadedFB->GetFramesPerSecond(), LoadedFB->GetTotalDuration()));
						return FText::AsNumber(FB.CombatData.Frames.Num());
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblExcluded", "Excluded"), [this]()
					{
						int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return FText::GetEmpty();
						return FText::Format(LOCTEXT("ExclVal", "{0} frames"), FText::AsNumber(Asset->Flipbooks[Idx].CombatData.ExcludedFrames.Num()));
					},
					[this]()
					{
						int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
						return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx) && Asset->Flipbooks[Idx].CombatData.ExcludedFrames.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblHitboxes", "Hitboxes"), [this]()
					{
						int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return FText::GetEmpty();
						const FFlipbookProfileEntry& FB = Asset->Flipbooks[Idx];
						int32 Atk = 0, Hrt = 0, Sock = 0;
						for (const FFrameHitboxData& F : FB.CombatData.Frames) { for (const FHitboxData& H : F.Hitboxes) { if (H.Type == EHitboxType::Attack) Atk++; else if (H.Type == EHitboxType::Hurtbox) Hrt++; } Sock += F.Sockets.Num(); }
						if (Atk == 0 && Hrt == 0 && Sock == 0) return LOCTEXT("HBNone", "None");
						TArray<FString> P; if (Atk > 0) P.Add(FString::Printf(TEXT("%d ATK"), Atk)); if (Hrt > 0) P.Add(FString::Printf(TEXT("%d HRT"), Hrt)); if (Sock > 0) P.Add(FString::Printf(TEXT("%d sockets"), Sock));
						return FText::FromString(FString::Join(P, TEXT("   ")));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblDamage", "Damage"), [this]()
					{
						int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return FText::GetEmpty();
						const FFlipbookProfileEntry& FB = Asset->Flipbooks[Idx];
						float MinDmg = MAX_flt, MaxDmg = 0.f;
						bool bHasAtk = false;
						for (const FFrameHitboxData& F : FB.CombatData.Frames)
						{
							for (const FHitboxData& H : F.Hitboxes)
							{
								if (H.Type == EHitboxType::Attack && H.Damage > 0.f)
								{
									bHasAtk = true;
									MinDmg = FMath::Min(MinDmg, H.Damage);
									MaxDmg = FMath::Max(MaxDmg, H.Damage);
								}
							}
						}
						if (!bHasAtk) return FText::GetEmpty();
						// SanitizeFloat with 0 min fractional digits: whole values read as before ("12"),
						// fractional damage shows its decimals (TASK-146).
						if (MinDmg == MaxDmg) return FText::FromString(FString::SanitizeFloat(MaxDmg, 0));
						return FText::FromString(FString::Printf(TEXT("%s - %s"), *FString::SanitizeFloat(MinDmg, 0), *FString::SanitizeFloat(MaxDmg, 0)));
					})
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeInfoRow(
						LOCTEXT("LblInvul", "Invul Frames"),
						[this]() -> FText
						{
							int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
							if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return FText::GetEmpty();
							const FFlipbookProfileEntry& FB = Asset->Flipbooks[Idx];
							TArray<FString> FrameNums;
							for (int32 i = 0; i < FB.CombatData.Frames.Num(); i++) { if (FB.CombatData.Frames[i].bInvulnerable) FrameNums.Add(FString::FromInt(i)); }
							return FText::FromString(FString::Join(FrameNums, TEXT(", ")));
						},
						[this]() -> EVisibility
						{
							int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
							if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return EVisibility::Collapsed;
							const FFlipbookProfileEntry& FB = Asset->Flipbooks[Idx];
							for (int32 i = 0; i < FB.CombatData.Frames.Num(); i++) { if (FB.CombatData.Frames[i].bInvulnerable) return EVisibility::Visible; }
							return EVisibility::Collapsed;
						})
				]
			]
		]

		// --- Connections section ---
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
				return (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx)) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight()
				[
					FProfilePropertyRowUtils::MakeSectionTitle(LOCTEXT("ConnectionsSectionTitle", "Connections"))
				]

				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeInfoRow(LOCTEXT("LblTags", "Tags"), [this]()
					{
						int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return FText::GetEmpty();
						const FString& Name = Asset->Flipbooks[Idx].Identity.FlipbookName;
						TArray<FString> Tags;
						for (auto& Pair : Asset->TagMappings) { if (Pair.Value.Entries.ContainsByPredicate([&Name](const FFlipbookTagMappingEntry& E){ return E.FlipbookName == Name; })) { FString T = Pair.Key.IsValid() ? Pair.Key.ToString() : TEXT("(none)"); static const FString Pre = TEXT("Paper2DPlus.Animation."); if (T.StartsWith(Pre)) T = T.RightChop(Pre.Len()); Tags.Add(T); } }
						return Tags.Num() > 0 ? FText::FromString(FString::Join(Tags, TEXT(", "))) : LOCTEXT("TagNone", "---");
					})
				]
				// (Legacy "Effects" info row removed — FFlipbookEffectData is migrated to frame
				//  events at load (TASK-3), so the field is always empty; the Frame Events tab
				//  is the live surface for per-frame effects.)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblMotion", "Motion"), [this]()
					{
						int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return LOCTEXT("MotNone", "---");
						const FFlipbookProfileEntry& FB = Asset->Flipbooks[Idx];
						if (!FB.MotionData.HasRootMotion()) return LOCTEXT("MotNone", "---");
						FVector2D Tot = FVector2D::ZeroVector, Prev = FVector2D::ZeroVector;
						for (const FRootMotionFrameData& RM : FB.MotionData.RootMotion) { if (!RM.Position.IsNearlyZero()) { Tot += RM.Position - Prev; Prev = RM.Position; } }
						return FText::FromString(FString::Printf(TEXT("%.0f, %.0f px"), Tot.X, Tot.Y));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblPZD", "PaperZD Sequence"), [this]()
					{
						int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return FText::GetEmpty();
						UObject* Seq = Asset->Flipbooks[Idx].Identity.PaperZDSequence.Get();
						return Seq ? FText::FromString(Seq->GetName()) : LOCTEXT("PZDNone", "---");
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					MakeInfoRow(LOCTEXT("LblSource", "Source"), [this]()
					{
						int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return FText::GetEmpty();
						const auto& Tex = Asset->Flipbooks[Idx].SourceTexture;
						return !Tex.IsNull() ? FText::FromString(FPaths::GetBaseFilename(Tex.GetAssetName())) : LOCTEXT("SrcNone", "---");
					})
				]
			]
		]

		;
}

// ==========================================
// PAPERZD ANIM SOURCE PANEL
// ==========================================

UClass* SProfileDetailsPanel::ResolveOptionalPaperZDAnimationSourceClass()
{
	return Paper2DPlus::PaperZDSequenceAuthoring::ResolveAnimationSourceClass();
}

UClass* SProfileDetailsPanel::ResolveOptionalPaperZDFlipbookSequenceClass()
{
	return Paper2DPlus::PaperZDSequenceAuthoring::ResolveFlipbookSequenceClass();
}

FString SProfileDetailsPanel::BuildDefaultPaperZDSequenceNameForTests(
	const FString& FlipbookName,
	const bool bIncludeProfilePrefix) const
{
	return Asset.IsValid()
		? Paper2DPlus::PaperZDSequenceAuthoring::BuildDefaultSequenceName(
			Asset->GetName(), FlipbookName, bIncludeProfilePrefix)
		: FlipbookName;
}

TArray<FString> SProfileDetailsPanel::GetPendingPaperZDSequenceNamesForTests(
	const bool bIncludeProfilePrefix) const
{
	TArray<FString> Result;
	if (!Asset.IsValid())
	{
		return Result;
	}

	TMap<UPaperFlipbook*, UObject*> ResolvedSequences;
	TArray<TSharedPtr<Paper2DPlus::PaperZDSequenceAuthoring::FPendingSequence>>
		PendingSequences;
	Paper2DPlus::PaperZDSequenceAuthoring::GatherAllSequenceWork(
		*Asset.Get(), false, ResolvedSequences, PendingSequences);
	for (const TSharedPtr<
		Paper2DPlus::PaperZDSequenceAuthoring::FPendingSequence>& Pending :
		PendingSequences)
	{
		if (Pending.IsValid())
		{
			Result.Add(
				Paper2DPlus::PaperZDSequenceAuthoring::BuildDefaultSequenceName(
				Asset->GetName(), Pending->FlipbookName, bIncludeProfilePrefix));
		}
	}
	return Result;
}

bool SProfileDetailsPanel::AssignPaperZDSequenceForTests(
	UPaperFlipbook* Flipbook,
	UObject* Sequence)
{
	return Asset.IsValid()
		&& Paper2DPlus::PaperZDSequenceAuthoring::
			AssignSequenceToMissingReferences(
				*Asset.Get(),
				Flipbook,
				Sequence);
}

FText SProfileDetailsPanel::GetProfileSpriteBoundsSummary() const
{
	if (!SpriteBoundsReport.IsValid())
	{
		return LOCTEXT("SpriteBoundsNotChecked", "Not checked");
	}
	if (SpriteBoundsReport->RepairableFrames > 0 || SpriteBoundsReport->UnsupportedFrames > 0)
	{
		return FText::Format(
			LOCTEXT("SpriteBoundsNeedsAttentionFmt", "{0} need repair  ·  {1} unsupported"),
			FText::AsNumber(SpriteBoundsReport->RepairableFrames),
			FText::AsNumber(SpriteBoundsReport->UnsupportedFrames));
	}
	if (SpriteBoundsReport->EmptyFrames > 0)
	{
		return FText::Format(
			LOCTEXT("SpriteBoundsHealthyWithEmptyFmt", "{0} content frames healthy  ·  {1} empty"),
			FText::AsNumber(FMath::Max(0, SpriteBoundsReport->HealthyFrames - SpriteBoundsReport->EmptyFrames)),
			FText::AsNumber(SpriteBoundsReport->EmptyFrames));
	}
	return FText::Format(
		LOCTEXT("SpriteBoundsHealthyFmt", "{0} frames healthy"),
		FText::AsNumber(SpriteBoundsReport->HealthyFrames));
}

TSharedRef<SWidget> SProfileDetailsPanel::BuildSpriteBoundsPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 6)
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"SpriteBoundsHelp",
				"Checks every flipbook. Each animation is normalized from its maximum left, right, top, and bottom content extents around the frame anchor; empty frames do not inflate the result."))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 0, 4, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("CheckProfileSpriteBounds", "Check Profile"))
				.ToolTipText(LOCTEXT("CheckProfileSpriteBoundsTip", "Read source pixels and check every Profile flipbook without changing assets."))
				.IsEnabled_Lambda([this]() { return Asset.IsValid(); })
				.OnClicked_Lambda([this]()
				{
					RunProfileSpriteBoundsCheck();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RepairProfileSpriteBounds", "Repair All"))
				.ToolTipText(LOCTEXT("RepairProfileSpriteBoundsTip", "Normalize every repairable frame in one undoable operation. Authored SpriteOffset values are preserved."))
				.IsEnabled_Lambda([this]()
				{
					return Asset.IsValid() && SpriteBoundsReport.IsValid() && SpriteBoundsReport->HasRepairs();
				})
				.OnClicked_Lambda([this]()
				{
					RepairProfileSpriteBounds();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 0, 4, 4)
		[
			SAssignNew(SpriteBoundsResultsBox, SVerticalBox)
		];
}

void SProfileDetailsPanel::InvalidateProfileSpriteBoundsReport()
{
	SpriteBoundsReport.Reset();
	RefreshProfileSpriteBoundsResults();
}

void SProfileDetailsPanel::RunProfileSpriteBoundsCheck()
{
	if (!Asset.IsValid())
	{
		return;
	}
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("CheckingProfileSpriteBounds", "Checking Character Profile sprite bounds..."));
	SlowTask.MakeDialogDelayed(0.25f);
	SlowTask.EnterProgressFrame(1.0f);
	SpriteBoundsReport = MakeShared<FProfileSpriteBoundsReport>(
		FProfileSpriteBoundsService::AnalyzeProfile(Asset.Get()));
	RefreshProfileSpriteBoundsResults();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SProfileDetailsPanel::RepairProfileSpriteBounds()
{
	if (!Asset.IsValid() || !SpriteBoundsReport.IsValid() || !SpriteBoundsReport->HasRepairs())
	{
		return;
	}

	const FText Confirmation = FText::Format(
		LOCTEXT(
			"RepairProfileSpriteBoundsConfirm",
			"Repair {0} frame(s) across this Character Profile?\n\nThe operation updates sprite source regions and Profile trim metadata together and can be undone."),
		FText::AsNumber(SpriteBoundsReport->RepairableFrames));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Confirmation) != EAppReturnType::Yes)
	{
		return;
	}

	FScopedSlowTask SlowTask(1.0f, LOCTEXT("RepairingProfileSpriteBounds", "Repairing Character Profile sprite bounds..."));
	SlowTask.MakeDialogDelayed(0.25f);
	SlowTask.EnterProgressFrame(1.0f);

	FProfileSpriteBoundsReport PostRepairReport;
	const FProfileSpriteBoundsRepairStats Stats =
		FProfileSpriteBoundsService::RepairProfile(Asset.Get(), PostRepairReport, /*bCreateTransaction=*/true);

	{
		TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
		if (Model.IsValid())
		{
			Model->NotifyAssetDataChanged();
		}
	}
	OnRefreshOverviewFlipbookList.ExecuteIfBound();
	// Refresh delegates may rebuild Character Details and invalidate a stale diagnosis. Publish the
	// fresh post-repair report last so the section immediately shows the verified result.
	SpriteBoundsReport = MakeShared<FProfileSpriteBoundsReport>(MoveTemp(PostRepairReport));
	RefreshProfileSpriteBoundsResults();

	FNotificationInfo Notification(FText::Format(
		LOCTEXT("RepairProfileSpriteBoundsDone", "Repaired {0} frame(s) and {1} sprite asset(s)."),
		FText::AsNumber(Stats.FramesRepaired),
		FText::AsNumber(Stats.SpritesModified)));
	Notification.ExpireDuration = 5.0f;
	FSlateNotificationManager::Get().AddNotification(Notification);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SProfileDetailsPanel::RefreshProfileSpriteBoundsResults()
{
	if (!SpriteBoundsResultsBox.IsValid())
	{
		return;
	}
	SpriteBoundsResultsBox->ClearChildren();

	if (!SpriteBoundsReport.IsValid())
	{
		SpriteBoundsResultsBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SpriteBoundsRunCheck", "Run Check Profile to inspect every animation."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		return;
	}

	if (SpriteBoundsReport->Flipbooks.Num() == 0)
	{
		SpriteBoundsResultsBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SpriteBoundsNoFlipbooks", "This Profile has no flipbooks to check."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		return;
	}

	for (const FProfileSpriteBoundsFlipbookReport& Flipbook : SpriteBoundsReport->Flipbooks)
	{
		const bool bHasContentTarget = Flipbook.TargetSize.X > 0 && Flipbook.TargetSize.Y > 0;
		const FLinearColor StatusColor = Flipbook.UnsupportedFrames > 0
			? FLinearColor(0.95f, 0.30f, 0.25f)
			: (Flipbook.RepairableFrames > 0
				? FLinearColor(0.95f, 0.65f, 0.18f)
				: (bHasContentTarget
					? FLinearColor(0.30f, 0.80f, 0.42f)
					: FLinearColor(0.55f, 0.55f, 0.55f)));
		const FText SizeText = bHasContentTarget
			? FText::Format(LOCTEXT("SpriteBoundsSizeFmt", "{0} × {1}"),
				FText::AsNumber(Flipbook.TargetSize.X), FText::AsNumber(Flipbook.TargetSize.Y))
			: LOCTEXT("SpriteBoundsSizeUnavailable", "No content size");
		const FText StatusText = FText::Format(
			LOCTEXT("SpriteBoundsRowStatusFmt", "{0} healthy  ·  {1} need repair  ·  {2} empty (included)  ·  {3} unsupported"),
			FText::AsNumber(Flipbook.HealthyFrames),
			FText::AsNumber(Flipbook.RepairableFrames),
			FText::AsNumber(Flipbook.EmptyFrames),
			FText::AsNumber(Flipbook.UnsupportedFrames));

		FString FirstDiagnostic;
		for (const FProfileSpriteBoundsFrameReport& Frame : Flipbook.Frames)
		{
			if (Frame.Kind == EProfileSpriteBoundsFrameKind::Unsupported && !Frame.Diagnostic.IsEmpty())
			{
				FirstDiagnostic = Frame.Diagnostic;
				break;
			}
		}

		TSharedRef<SVerticalBox> Row = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Flipbook.FlipbookName.IsEmpty() ? TEXT("Unnamed Flipbook") : Flipbook.FlipbookName))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Text(SizeText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(StatusText)
				.ColorAndOpacity(StatusColor)
			];
		if (!FirstDiagnostic.IsEmpty())
		{
			Row->AddSlot()
			.AutoHeight()
			.Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FirstDiagnostic))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		}

		SpriteBoundsResultsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(6, 4))
			[
				Row
			]
		];
	}
}

TSharedRef<SWidget> SProfileDetailsPanel::BuildPaperZDAnimSourcePanel()
{
	bHasPaperZDSequenceCreationAction = true;
	UClass* PaperZDAnimationSourceClass = ResolveOptionalPaperZDAnimationSourceClass();
	TSharedRef<SWidget> PanelRoot = SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(8, 6))
		[
			SNew(SVerticalBox)

			// Source picker row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 6, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PZDSourceLabel2", "Source"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.6f)))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(PaperZDAnimationSourceClass
						? PaperZDAnimationSourceClass
						: UObject::StaticClass())
					.AllowClear(true)
					.ObjectPath_Lambda([this]() -> FString
					{
						if (!Asset.IsValid() || Asset->PaperZDAnimSource.IsNull()) return FString();
						return Asset->PaperZDAnimSource.ToSoftObjectPath().ToString();
					})
					.OnObjectChanged_Lambda([this](const FAssetData& AssetData)
					{
						if (!Asset.IsValid()) return;
						BeginTransaction(LOCTEXT("SetAnimSource3", "Set PaperZD Anim Source"));
						if (AssetData.IsValid())
						{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
							Asset->PaperZDAnimSource = TSoftObjectPtr<UObject>(AssetData.ToSoftObjectPath());
#else
							Asset->PaperZDAnimSource = TSoftObjectPtr<UObject>(AssetData.GetSoftObjectPath());
#endif
							Asset->AutoPopulatePaperZDSequences();
						}
						else
						{
							Asset->PaperZDAnimSource = nullptr;
						}
						EndTransaction();
						OnRefreshOverviewFlipbookList.ExecuteIfBound();
						RefreshPaperZDSequencesList();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("RescanPZDTip", "Re-scan flipbooks for matching PaperZD sequences"))
					.OnClicked_Lambda([this]()
					{
						if (!Asset.IsValid()) return FReply::Handled();
						BeginTransaction(LOCTEXT("RescanPZDSeqs2", "Re-scan PaperZD Sequences"));
						Asset->AutoPopulatePaperZDSequences();
						EndTransaction();
						RefreshPaperZDSequencesList();
						Invalidate(EInvalidateWidgetReason::Paint);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RescanBtn2", "Re-scan"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("CreatePZDTip", "Create PaperZD sequences for all flipbooks that don't have one"))
					.IsEnabled_Lambda([this]() { return Asset.IsValid() && !Asset->PaperZDAnimSource.IsNull(); })
					.OnClicked_Lambda([this]()
					{
						AutoCreateTagMappingSequences();
						RefreshPaperZDSequencesList();
						OnRefreshOverviewFlipbookList.ExecuteIfBound();
						Invalidate(EInvalidateWidgetReason::Paint);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CreatePZDBtn", "Create"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				]
			]

			// Sequences dropdown header
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4, 0, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PZDSequencesLabel", "Sequences in Source"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.6f)))
				.Visibility_Lambda([this]()
				{
					return (Asset.IsValid() && !Asset->PaperZDAnimSource.IsNull()) ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]

			// Scrollable list of all sequences
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(180.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(4))
				.Visibility_Lambda([this]()
				{
					return (Asset.IsValid() && !Asset->PaperZDAnimSource.IsNull()) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(PaperZDSequencesListBox, SVerticalBox)
					]
				]
			]
		];

	return PanelRoot;
}

void SProfileDetailsPanel::RefreshPaperZDSequencesList()
{
	if (!PaperZDSequencesListBox.IsValid()) return;
	PaperZDSequencesListBox->ClearChildren();

	if (!Asset.IsValid() || Asset->PaperZDAnimSource.IsNull()) return;

	UClass* SeqClass =
		Paper2DPlus::PaperZDSequenceAuthoring::ResolveFlipbookSequenceClass();
	if (!SeqClass) return;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	Filter.ClassNames.Add(SeqClass->GetFName());
#else
	Filter.ClassPaths.Add(SeqClass->GetClassPathName());
#endif
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> SequenceAssets;
	AssetRegistry.GetAssets(Filter, SequenceAssets);

	const FString DesiredSourcePath = Asset->PaperZDAnimSource.ToSoftObjectPath().ToString();

	TSet<UObject*> MatchedSequences;
	for (const FFlipbookProfileEntry& FB : Asset->Flipbooks)
	{
		if (FB.Identity.PaperZDSequence) MatchedSequences.Add(FB.Identity.PaperZDSequence.Get());
	}

	struct FSequenceRow
	{
		FString Name;
		bool bMatched;
		FString MatchedFlipbookName;
	};
	TArray<FSequenceRow> Rows;

	for (const FAssetData& AssetData : SequenceAssets)
	{
		FAssetDataTagMapSharedView::FFindTagResult TagResult = AssetData.TagsAndValues.FindTag(FName("AnimSource"));
		if (!TagResult.IsSet()) continue;

		FString TagObjectPath = FPackageName::ExportTextPathToObjectPath(TagResult.GetValue());
		if (TagObjectPath != DesiredSourcePath) continue;

		FSequenceRow Row;
		Row.Name = AssetData.AssetName.ToString();
		Row.bMatched = false;

		UObject* LoadedSeq = AssetData.GetAsset();
		if (LoadedSeq && MatchedSequences.Contains(LoadedSeq))
		{
			Row.bMatched = true;
			for (const FFlipbookProfileEntry& FB : Asset->Flipbooks)
			{
				if (FB.Identity.PaperZDSequence.Get() == LoadedSeq)
				{
					Row.MatchedFlipbookName = FB.Identity.FlipbookName;
					break;
				}
			}
		}
		Rows.Add(Row);
	}

	Rows.Sort([](const FSequenceRow& A, const FSequenceRow& B)
	{
		if (A.bMatched != B.bMatched) return A.bMatched;
		return A.Name < B.Name;
	});

	for (const FSequenceRow& Row : Rows)
	{
		FLinearColor DotColor = Row.bMatched ? FLinearColor(0.3f, 0.8f, 0.3f) : FLinearColor(0.5f, 0.5f, 0.5f);
		FLinearColor TextColor = Row.bMatched ? FLinearColor(0.9f, 0.9f, 0.9f) : FLinearColor(0.55f, 0.55f, 0.55f);

		PaperZDSequencesListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SBox).WidthOverride(8).HeightOverride(8)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.FilledCircle"))
					.ColorAndOpacity(DotColor)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Row.Name))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(TextColor))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(Row.bMatched ? FText::Format(LOCTEXT("SeqMatchedLabel", "\x2192 {0}"), FText::FromString(Row.MatchedFlipbookName)) : FText::GetEmpty())
				.Font(FProfilePropertyRowUtils::GetPropertyFont())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.6f, 0.4f)))
			]
		];
	}
}

// ==========================================
// OPTIONAL PAPERZD SEQUENCE CREATION
// ==========================================



void SProfileDetailsPanel::AutoCreateTagMappingSequences()
{
	if (!Asset.IsValid())
	{
		return;
	}

	TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
	const bool bProfileModified =
		Paper2DPlus::PaperZDSequenceAuthoring::CreateAndLinkMissingSequences(
			*Asset.Get(),
			AsShared());

	if (!bProfileModified)
	{
		return;
	}
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
	RefreshPaperZDSequencesList();
	OnRefreshOverviewFlipbookList.ExecuteIfBound();
	Invalidate(EInvalidateWidgetReason::Paint);
}

// ==========================================
// MOVE TRANSITIONS PANEL (TASK-76)
// ==========================================

TSharedRef<SWidget> SProfileDetailsPanel::BuildTransitionsPanel()
{
	return SNew(SVerticalBox)

		// Hint
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MoveTransitionsHint", "Transitions: the moves the selected move can transition into — pure From \x2192 To arrows (one per target). Stored as data only; the game/PaperZD performs every switch."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.AutoWrapText(true)
		]

		// Transitions list
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(TransitionsListBox, SVerticalBox)
		]

		// Add transition button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 6, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("AddTransitionTip", "Add an outgoing transition from the selected move"))
			.IsEnabled_Lambda([this]()
			{
				const int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
				return Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx);
			})
			.OnClicked_Lambda([this]()
			{
				const int32 Idx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Idx)) return FReply::Handled();
				{
					// Broadcast so every transition surface (e.g. the Animation Map tab) reconciles;
					// bPanelWriteInProgress suppresses this panel's own handler — we rebuild below, exactly once.
					TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
					BeginTransaction(LOCTEXT("AddMoveTransition", "Add Move Transition"));
					Asset->Flipbooks[Idx].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition());
					EndTransaction();
					if (Model.IsValid()) Model->NotifyAssetDataChanged();
				}
				RefreshTransitionsList();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AddTransitionBtn", "+ Add Transition"))
			]
		];
}

void SProfileDetailsPanel::RefreshTransitionsList()
{
	if (!TransitionsListBox.IsValid()) return;

	TransitionsListBox->ClearChildren();

	// Collect target-move names for the combo box: "(none)" sentinel first, then sorted names
	TransitionTargetNameOptions.Reset();
	TransitionTargetNameOptions.Add(MakeShared<FString>(TEXT("(none)")));
	if (Asset.IsValid())
	{
		for (int32 Idx : GetSortedFlipbookIndices())
		{
			TransitionTargetNameOptions.Add(MakeShared<FString>(Asset->Flipbooks[Idx].Identity.FlipbookName));
		}
	}

	const int32 FlipbookIdx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIdx))
	{
		TransitionsListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TransitionsSelectFlipbook", "Select a flipbook to edit its transitions."))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		];
		return;
	}

	const FFlipbookProfileEntry& Entry = Asset->Flipbooks[FlipbookIdx];

	if (Entry.TransitionData.Transitions.Num() == 0)
	{
		TransitionsListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoTransitions", "No transitions yet. Click + Add Transition to create one."))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			.AutoWrapText(true)
		];
		return;
	}

	for (int32 TransIdx = 0; TransIdx < Entry.TransitionData.Transitions.Num(); ++TransIdx)
	{
		const int32 CapturedTransIdx = TransIdx;

		TSharedPtr<FString> CurrentSelection;
		for (const TSharedPtr<FString>& Option : TransitionTargetNameOptions)
		{
			if (Option->Equals(Entry.TransitionData.Transitions[TransIdx].TargetMove, ESearchCase::IgnoreCase))
			{
				CurrentSelection = Option;
				break;
			}
		}
		// Only an EMPTY target maps to the '(none)' sentinel. A non-empty dangling target (renamed/deleted
		// move) leaves the selection null so clicking '(none)' IS a selection change and actually clears it.
		if (!CurrentSelection && Entry.TransitionData.Transitions[TransIdx].TargetMove.IsEmpty())
		{
			CurrentSelection = TransitionTargetNameOptions[0];
		}

		UPaperFlipbook* TargetFlipbook = nullptr;
		const FFlipbookProfileEntry* TargetData = Asset->FindFlipbookDataPtr(Entry.TransitionData.Transitions[TransIdx].TargetMove);
		if (TargetData) TargetFlipbook = TargetData->Identity.Flipbook.LoadSynchronous();

		TransitionsListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 1)
		[
			SNew(SHorizontalBox)

			// Arrow separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("\x2192")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]

			// Target thumbnail
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(SBox)
				.WidthOverride(24)
				.HeightOverride(24)
				[
					SNew(SFlipbookThumbnail)
					.Flipbook(TargetFlipbook)
				]
			]

			// Target move dropdown
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&TransitionTargetNameOptions)
				.InitiallySelectedItem(CurrentSelection)
				.IsEnabled_Lambda([this, FlipbookIdx, CapturedTransIdx]()
				{
					return Asset.IsValid()
						&& Asset->Flipbooks.IsValidIndex(FlipbookIdx)
						&& Asset->Flipbooks[FlipbookIdx].TransitionData.Transitions.IsValidIndex(
							CapturedTransIdx);
				})
				.OnSelectionChanged_Lambda([this, FlipbookIdx, CapturedTransIdx](TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
				{
					// Keyboard browsing in the open list must not commit a transaction per arrow-key.
					if (SelectInfo == ESelectInfo::OnNavigation) return;
					if (!NewValue.IsValid()) return;
					// Here '(none)' only clears the target; the remove button owns row removal.
					const FString NewTarget = NewValue->Equals(TEXT("(none)")) ? FString() : *NewValue;
					// CommitTransitionTargetChange owns the transaction AND the duplicate-(From, To)
					// refusal (TASK-108 KTD — one row per pair holds through the pane path too). Rebuild
					// on refusal as well: it snaps the combo's picked-but-refused item back to the
					// stored target (the rebuild-inside-own-callback shape is the shipped precedent here).
					const bool bChanged = CommitTransitionTargetChange(FlipbookIdx, CapturedTransIdx, NewTarget);
					RefreshTransitionsList();
					if (bChanged)
					{
						// A committed target can re-key a selected edge's row — re-check the value key.
						ValidateTransitionSelection();
						RefreshEdgeModeSection();
					}
				})
				.OnGenerateWidget_Lambda([this](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					UPaperFlipbook* OptionFB = nullptr;
					if (Asset.IsValid() && Item.IsValid())
					{
						const FFlipbookProfileEntry* FBData = Asset->FindFlipbookDataPtr(*Item);
						if (FBData) OptionFB = FBData->Identity.Flipbook.LoadSynchronous();
					}

					return SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 4, 0)
						[
							SNew(SBox)
							.WidthOverride(24)
							.HeightOverride(24)
							[
								SNew(SFlipbookThumbnail)
								.Flipbook(OptionFB)
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")))
						];
				})
				.Content()
				[
					SNew(STextBlock)
					.Text_Lambda([this, FlipbookIdx, CapturedTransIdx]()
					{
						// Re-reads the stored name so a renamed-away target still displays as stored.
						if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIdx)) return FText::GetEmpty();
						const TArray<FPaper2DPlusMoveTransition>& Trans = Asset->Flipbooks[FlipbookIdx].TransitionData.Transitions;
						if (!Trans.IsValidIndex(CapturedTransIdx) || Trans[CapturedTransIdx].TargetMove.IsEmpty())
						{
							return LOCTEXT("None", "(none)");
						}
						return FText::FromString(Trans[CapturedTransIdx].TargetMove);
					})
				]
			]

			// Remove transition
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("RemoveTransitionTooltip", "Remove this transition"))
				.IsEnabled_Lambda([this, FlipbookIdx, CapturedTransIdx]()
				{
					return Asset.IsValid()
						&& Asset->Flipbooks.IsValidIndex(FlipbookIdx)
						&& Asset->Flipbooks[FlipbookIdx].TransitionData.Transitions.IsValidIndex(
							CapturedTransIdx);
				})
				.OnClicked_Lambda([this, FlipbookIdx, CapturedTransIdx]()
				{
					if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIdx)) return FReply::Handled();
					TArray<FPaper2DPlusMoveTransition>& Trans = Asset->Flipbooks[FlipbookIdx].TransitionData.Transitions;
					if (!Trans.IsValidIndex(CapturedTransIdx)) return FReply::Handled();
					{
						TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
						BeginTransaction(LOCTEXT("RemoveMoveTransition", "Remove Move Transition"));
						Trans.RemoveAt(CapturedTransIdx);
						EndTransaction();
						if (Model.IsValid()) Model->NotifyAssetDataChanged();
					}
					RefreshTransitionsList();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\x2715")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.3f, 0.3f)))
				]
			]
		];
	}
}

// ==========================================
// RELATIVE TRANSFORM SECTION
// ==========================================

TSharedRef<SWidget> SProfileDetailsPanel::BuildRelativeTransformSection()
{
	if (!Asset.IsValid()) return SNullWidget::NullWidget;

	bHasRelativeTransformEditorSurface = false;
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// The EDITORS stay hand-rolled; only the framing is shared. Older ISinglePropertyView
	// implementations assert while constructing FVector/FRotator rows (UE 5.0 and 5.1 are both
	// affected), so these remain plugin-owned SNumericVectorInputBox / SNumericRotatorInputBox
	// controls rather than becoming a nested Details tree. What changes is that the label/value
	// framing now comes from the shared row instead of a private 72px box and a hand-picked 8pt
	// font, so this surface shares one drag-resizable column with every other Paper2D+ detail
	// panel. The shared row provides the framing, not the editor widget — that distinction is the
	// whole reason this conversion is safe on the oldest supported engine.
	auto AddCompactRow = [&Content](const FText& Label, const TSharedRef<SWidget>& Editor)
	{
		Content->AddSlot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(Label, Editor)
		];
	};

	TSharedRef<SNumericVectorInputBox<FVector::FReal>> LocationEditor =
		SNew(SNumericVectorInputBox<FVector::FReal>)
		.bColorAxisLabels(true)
		.AllowSpin(false)
		.X_Lambda([this]() -> TOptional<FVector::FReal>
		{
			return Asset.IsValid() ? TOptional<FVector::FReal>(Asset->RelativeLocation.X) : TOptional<FVector::FReal>();
		})
		.Y_Lambda([this]() -> TOptional<FVector::FReal>
		{
			return Asset.IsValid() ? TOptional<FVector::FReal>(Asset->RelativeLocation.Y) : TOptional<FVector::FReal>();
		})
		.Z_Lambda([this]() -> TOptional<FVector::FReal>
		{
			return Asset.IsValid() ? TOptional<FVector::FReal>(Asset->RelativeLocation.Z) : TOptional<FVector::FReal>();
		})
		.OnXCommitted_Lambda([this](FVector::FReal Value, ETextCommit::Type)
		{
			if (Asset.IsValid()) { FVector Next = Asset->RelativeLocation; Next.X = Value; CommitRelativeLocation(Next); }
		})
		.OnYCommitted_Lambda([this](FVector::FReal Value, ETextCommit::Type)
		{
			if (Asset.IsValid()) { FVector Next = Asset->RelativeLocation; Next.Y = Value; CommitRelativeLocation(Next); }
		})
		.OnZCommitted_Lambda([this](FVector::FReal Value, ETextCommit::Type)
		{
			if (Asset.IsValid()) { FVector Next = Asset->RelativeLocation; Next.Z = Value; CommitRelativeLocation(Next); }
		});

	TSharedRef<SNumericRotatorInputBox<FRotator::FReal>> RotationEditor =
		SNew(SNumericRotatorInputBox<FRotator::FReal>)
		.bColorAxisLabels(true)
		.AllowSpin(false)
		.Roll_Lambda([this]() -> TOptional<FRotator::FReal>
		{
			return Asset.IsValid() ? TOptional<FRotator::FReal>(Asset->RelativeRotation.Roll) : TOptional<FRotator::FReal>();
		})
		.Pitch_Lambda([this]() -> TOptional<FRotator::FReal>
		{
			return Asset.IsValid() ? TOptional<FRotator::FReal>(Asset->RelativeRotation.Pitch) : TOptional<FRotator::FReal>();
		})
		.Yaw_Lambda([this]() -> TOptional<FRotator::FReal>
		{
			return Asset.IsValid() ? TOptional<FRotator::FReal>(Asset->RelativeRotation.Yaw) : TOptional<FRotator::FReal>();
		})
		.OnRollCommitted_Lambda([this](FRotator::FReal Value, ETextCommit::Type)
		{
			if (Asset.IsValid()) { FRotator Next = Asset->RelativeRotation; Next.Roll = Value; CommitRelativeRotation(Next); }
		})
		.OnPitchCommitted_Lambda([this](FRotator::FReal Value, ETextCommit::Type)
		{
			if (Asset.IsValid()) { FRotator Next = Asset->RelativeRotation; Next.Pitch = Value; CommitRelativeRotation(Next); }
		})
		.OnYawCommitted_Lambda([this](FRotator::FReal Value, ETextCommit::Type)
		{
			if (Asset.IsValid()) { FRotator Next = Asset->RelativeRotation; Next.Yaw = Value; CommitRelativeRotation(Next); }
		});

	TSharedRef<SNumericVectorInputBox<FVector::FReal>> ScaleEditor =
		SNew(SNumericVectorInputBox<FVector::FReal>)
		.bColorAxisLabels(true)
		.AllowSpin(false)
		.X_Lambda([this]() -> TOptional<FVector::FReal>
		{
			return Asset.IsValid() ? TOptional<FVector::FReal>(Asset->RelativeScale3D.X) : TOptional<FVector::FReal>();
		})
		.Y_Lambda([this]() -> TOptional<FVector::FReal>
		{
			return Asset.IsValid() ? TOptional<FVector::FReal>(Asset->RelativeScale3D.Y) : TOptional<FVector::FReal>();
		})
		.Z_Lambda([this]() -> TOptional<FVector::FReal>
		{
			return Asset.IsValid() ? TOptional<FVector::FReal>(Asset->RelativeScale3D.Z) : TOptional<FVector::FReal>();
		})
		.OnXCommitted_Lambda([this](FVector::FReal Value, ETextCommit::Type)
		{
			if (Asset.IsValid()) { FVector Next = Asset->RelativeScale3D; Next.X = Value; CommitRelativeScale(Next); }
		})
		.OnYCommitted_Lambda([this](FVector::FReal Value, ETextCommit::Type)
		{
			if (Asset.IsValid()) { FVector Next = Asset->RelativeScale3D; Next.Y = Value; CommitRelativeScale(Next); }
		})
		.OnZCommitted_Lambda([this](FVector::FReal Value, ETextCommit::Type)
		{
			if (Asset.IsValid()) { FVector Next = Asset->RelativeScale3D; Next.Z = Value; CommitRelativeScale(Next); }
		});

	AddCompactRow(LOCTEXT("RelativeLocationRow", "Location"), LocationEditor);
	AddCompactRow(LOCTEXT("RelativeRotationRow", "Rotation"), RotationEditor);
	AddCompactRow(LOCTEXT("RelativeScaleRow", "Scale"), ScaleEditor);
	bHasRelativeTransformEditorSurface = true;

	// Affordance: whether these fields act at runtime depends on the consuming component —
	// bApplyProfileRelativeTransform on the Character Profile Component (default off) makes the plugin
	// apply them; otherwise game/BP code reads GetRelativeTransform() and applies it. Surface that on
	// sight so the data isn't mistaken for an always-live sizing control.
	Content->AddSlot().AutoHeight().Padding(4, 4, 4, 4)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("RelTransformNotApplied", "Applied at runtime only when the Character Profile Component's Apply Profile Relative Transform is enabled (off by default); otherwise read GetRelativeTransform() in your Blueprint to position the sprite."))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.7f, 0.35f)))
		.AutoWrapText(true)
	];

	return SNew(SExpandableArea)
		.InitiallyCollapsed(true)
		.HeaderPadding(FMargin(4, 2))
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RelativeTransformSection", "Relative Transform"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		.BodyContent()
		[
			Content
		];
}

#undef LOCTEXT_NAMESPACE
