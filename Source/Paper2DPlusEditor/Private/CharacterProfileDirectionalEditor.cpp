// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "CharacterCoverage/SExpectedTagsPanel.h"
#include "CharacterProfileEditorModel.h"
#include "CharacterProfileJsonInteraction.h"
#include "Paper2DPlusDirectionalAnimationCommands.h"
#include "SDirectionalAnimationCommandRouter.h"
#include "SDirectionalAnimationWheel.h"
#include "SlateShortcutUtils.h"
#include "OverviewPanel.h"
#include "FlipbookListPanel.h"
#include "HitboxEditorPanel.h"
#include "SpriteEditorPanel.h"
#include "FrameTimingEditor.h"
#include "FrameEventEditor.h"
#include "RootMotionEditor.h"
#include "AnimationMapPanel.h"
#include "AnimationsPanel.h"
#include "Paper2DPlusEditorIcons.h"
#include "Paper2DPlusProfileEditorToolbar.h"
#include "ProfileValidationPanel.h"
#include "ProfileToolsWindow.h"
#include "RelatedProfileBar.h"
#include "AnimationProfilePickerSource.h"
#include "AnimationProfileSwitcher.h"
#include "CharacterCompletionPanel.h"
#include "PlaybackQueuePanel.h"
#include "ProfileToolPanelHost.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/IMenu.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "InputCoreTypes.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HAL/PlatformApplicationMisc.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

namespace Paper2DPlusDirectionalToolkitPrivate
{
	class SBehavioralButton final : public SButton
	{
	public:
		FReply ExecuteAssignedClickForTests()
		{
			return ExecuteOnClick();
		}
	};

	FSoftObjectPath AssetPath(const FAssetData& AssetData)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return AssetData.ToSoftObjectPath();
#else
		return AssetData.GetSoftObjectPath();
#endif
	}

	bool IsFlipbookAsset(const FAssetData& AssetData)
	{
		if (!AssetData.IsValid())
		{
			return false;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return AssetData.AssetClass == UPaperFlipbook::StaticClass()->GetFName();
#else
		return AssetData.AssetClassPath == UPaperFlipbook::StaticClass()->GetClassPathName();
#endif
	}

	bool GetRepresentableTopology(
		const TSharedPtr<FCharacterProfileEditorModel>& Model,
		int32& OutDirectionCount,
		float& OutAngleOffsetDegrees)
	{
		if (!Model.IsValid())
		{
			return false;
		}
		UPaper2DPlusCharacterProfileAsset* Profile = Model->GetAsset();
		const int32 OwnerIndex =
			FCharacterProfileAssetEditorToolkit::ResolveDirectionalSelectedOwnerIndex(Model);
		return Profile && OwnerIndex != INDEX_NONE
			&& Profile->GetEffectiveDirectionalSettings(
				OwnerIndex, OutDirectionCount, OutAngleOffsetDegrees);
	}

	TArray<FPaper2DPlusDirectionalWheelSegment> BuildSegments(
		const TSharedPtr<FCharacterProfileEditorModel>& Model)
	{
		TArray<FPaper2DPlusDirectionalWheelSegment> Segments;
		if (!Model.IsValid())
		{
			return Segments;
		}

		UPaper2DPlusCharacterProfileAsset* Profile = Model->GetAsset();
		const int32 OwnerIndex =
			FCharacterProfileAssetEditorToolkit::ResolveDirectionalSelectedOwnerIndex(Model);
		int32 DirectionCount = 0;
		float AngleOffsetDegrees = 0.0f;
		if (!Profile || OwnerIndex == INDEX_NONE
			|| !Profile->GetEffectiveDirectionalSettings(
				OwnerIndex, DirectionCount, AngleOffsetDegrees))
		{
			return Segments;
		}

		FPaper2DPlusDirectionalStructureResult Structure;
		const bool bStructureValid = Profile->CheckDirectionalAnimationStructure(
			OwnerIndex, Structure);
		Segments.Reserve(DirectionCount);
		for (int32 SlotIndex = 0; SlotIndex < DirectionCount; ++SlotIndex)
		{
			FPaper2DPlusDirectionalWheelSegment& Segment = Segments.AddDefaulted_GetRef();
			Segment.SlotIndex = SlotIndex;
			Segment.bValid = bStructureValid;
			TSoftObjectPtr<UPaperFlipbook> AssignedFlipbook;
			bool bMirrorHorizontally = false;
			Segment.bOccupied = Profile->GetDirectionalSlot(
				OwnerIndex, SlotIndex, AssignedFlipbook, bMirrorHorizontally);
			if (Segment.bOccupied)
			{
				Segment.AssetLabel = bMirrorHorizontally
					? FText::Format(
						NSLOCTEXT(
							"Paper2DPlusDirectionalEditor",
							"DirectionalWheelMirroredLabelFmt",
							"{0} (mirrored)"),
						FText::FromString(
							AssignedFlipbook.ToSoftObjectPath().GetAssetName()))
					: FText::FromString(
						AssignedFlipbook.ToSoftObjectPath().GetAssetName());
				if (Segment.bValid)
				{
					if (UPaperFlipbook* ResidentVariant = AssignedFlipbook.Get())
					{
						bool bAmbiguous = false;
						const FFlipbookProfileEntry* ResolvedOwner =
							Profile->ResolveLogicalAnimationOwner(
								ResidentVariant, bAmbiguous);
						FString CompatibilityFailure;
						Segment.bValid = !bAmbiguous
							&& ResolvedOwner == &Profile->Flipbooks[OwnerIndex]
							&& Profile->CheckDirectionalAnimationVariantCompatibility(
								OwnerIndex,
								ResidentVariant,
								CompatibilityFailure);
					}
				}
			}
		}
		return Segments;
	}
}

class FPaper2DPlusDirectionalWheelInputProcessor final : public IInputProcessor
{
public:
	FPaper2DPlusDirectionalWheelInputProcessor(
		TWeakPtr<FCharacterProfileAssetEditorToolkit> InOwner,
		TArray<FKey> InTriggerKeys)
		: Owner(MoveTemp(InOwner))
		, TriggerKeys(MoveTemp(InTriggerKeys))
	{
	}

	virtual void Tick(
		const float DeltaTime,
		FSlateApplication& SlateApp,
		TSharedRef<ICursor> Cursor) override
	{
		(void)DeltaTime;
		(void)SlateApp;
		(void)Cursor;
	}

	virtual bool HandleKeyDownEvent(
		FSlateApplication& SlateApp,
		const FKeyEvent& InKeyEvent) override
	{
		(void)SlateApp;
		if (const TSharedPtr<FCharacterProfileAssetEditorToolkit> Toolkit = Owner.Pin())
		{
			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				Toolkit->CancelDirectionalWheel();
				return true;
			}
			// Consume repeats of the shortcut's primary key while the held interaction is open.
			return TriggerKeys.Contains(InKeyEvent.GetKey());
		}
		return false;
	}

	virtual bool HandleKeyUpEvent(
		FSlateApplication& SlateApp,
		const FKeyEvent& InKeyEvent) override
	{
		(void)SlateApp;
		if (!TriggerKeys.Contains(InKeyEvent.GetKey()))
		{
			// Alt/Command/Control releases are modifiers, never directional commits.
			return false;
		}
		if (const TSharedPtr<FCharacterProfileAssetEditorToolkit> Toolkit = Owner.Pin())
		{
			Toolkit->HandleDirectionalShortcutPrimaryReleased();
			return true;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override
	{
		return TEXT("Paper2DPlusDirectionalWheel");
	}

private:
	TWeakPtr<FCharacterProfileAssetEditorToolkit> Owner;
	TArray<FKey> TriggerKeys;
};

bool FCharacterProfileAssetEditorToolkit::SupportsDirectionalHeader(FName ToolId)
{
	return ToolId == AnimationsTabId
		|| ToolId == HitboxEditorTabId
		|| ToolId == SpriteEditorTabId
		|| ToolId == FrameTimingTabId
		|| ToolId == FrameEventsTabId
		|| ToolId == RootMotionTabId;
}

FText FCharacterProfileAssetEditorToolkit::GetDirectionalHeaderGameplayDisclosure(FName ToolId)
{
	return SupportsDirectionalHeader(ToolId)
		? LOCTEXT(
			"DirectionalSharedGameplay",
			"Gameplay edits apply to all directions (base-owned)")
		: FText::GetEmpty();
}

void FCharacterProfileAssetEditorToolkit::EnableDirectionalPreviewForCharacterProfileHost(
	const TSharedRef<FCharacterProfileEditorModel>& Model)
{
	Model->SetDirectionalPreviewEnabled(true);
}

UPaperFlipbook* FCharacterProfileAssetEditorToolkit::ResolveDirectionalHeaderPreviewFlipbook(
	const FCharacterProfileDirectionalPreview& Preview)
{
	return Preview.State == ECharacterProfileDirectionalPreviewState::Base
		|| Preview.State == ECharacterProfileDirectionalPreviewState::OccupiedVariant
		? Preview.ResidentFlipbook.Get()
		: nullptr;
}

FText FCharacterProfileAssetEditorToolkit::GetDirectionalPreviewStateText(
	ECharacterProfileDirectionalPreviewState State)
{
	switch (State)
	{
	case ECharacterProfileDirectionalPreviewState::Base:
		return LOCTEXT("DirectionalStateBase", "Base");
	case ECharacterProfileDirectionalPreviewState::OccupiedVariant:
		return LOCTEXT("DirectionalStateOccupied", "Occupied");
	case ECharacterProfileDirectionalPreviewState::Empty:
		return LOCTEXT("DirectionalStateEmpty", "Empty");
	case ECharacterProfileDirectionalPreviewState::Resolving:
		return LOCTEXT("DirectionalStateResolving", "Loading");
	case ECharacterProfileDirectionalPreviewState::Unavailable:
		return LOCTEXT("DirectionalStateUnavailable", "Unavailable");
	default:
		return LOCTEXT("DirectionalStateUnknown", "Unknown");
	}
}

FKey FCharacterProfileAssetEditorToolkit::ResolveDirectionalWheelTriggerKey(
	const FInputChord& PrimaryChord,
	const FInputChord& SecondaryChord,
	const FKeyEvent& Event)
{
	for (const FInputChord* Chord : { &PrimaryChord, &SecondaryChord })
	{
		// Unreal's Input Binding editor and standalone toolkit host route keyboard command chords;
		// pointer buttons have no command-open route, so do not advertise an unsupported release path.
		if (MatchesDirectionalWheelChord(*Chord, Event))
		{
			return Event.GetKey();
		}
	}
	return FKey();
}

TArray<FKey> FCharacterProfileAssetEditorToolkit::ResolveDirectionalWheelFallbackTriggerKeys(
	const FInputChord& PrimaryChord,
	const FInputChord& SecondaryChord,
	const FModifierKeysState& ModifierKeys)
{
	TArray<FKey> TriggerKeys;
	for (const FInputChord* Chord : { &PrimaryChord, &SecondaryChord })
	{
		if (!Chord->IsValidChord() || !Chord->Key.IsValid() || Chord->Key.IsMouseButton()
			|| Chord->NeedsControl() != ModifierKeys.IsControlDown()
			|| Chord->NeedsAlt() != ModifierKeys.IsAltDown()
			|| Chord->NeedsShift() != ModifierKeys.IsShiftDown()
			|| Chord->NeedsCommand() != ModifierKeys.IsCommandDown())
		{
			continue;
		}
		TriggerKeys.AddUnique(Chord->Key);
	}
	// Without the originating key event, two same-modifier bindings are indistinguishable. Refuse
	// held release semantics in that case; the caller opens the ordinary direct-commit wheel instead.
	if (TriggerKeys.Num() != 1)
	{
		TriggerKeys.Reset();
	}
	return TriggerKeys;
}

bool FCharacterProfileAssetEditorToolkit::IsDirectionalWheelTriggerReleaseKey(
	const FKey& CapturedTriggerKey,
	const FKey& EventKey)
{
	return CapturedTriggerKey.IsValid() && EventKey == CapturedTriggerKey;
}

bool FCharacterProfileAssetEditorToolkit::MatchesDirectionalWheelChord(
	const FInputChord& Chord,
	const FKeyEvent& Event)
{
	return Chord.IsValidChord()
		&& Chord.Key.IsValid()
		&& !Chord.Key.IsMouseButton()
		&& Chord.Key == Event.GetKey()
		&& static_cast<bool>(Chord.bShift) == Event.IsShiftDown()
		&& static_cast<bool>(Chord.bCtrl) == Event.IsControlDown()
		&& static_cast<bool>(Chord.bAlt) == Event.IsAltDown()
		&& static_cast<bool>(Chord.bCmd) == Event.IsCommandDown();
}

int32 FCharacterProfileAssetEditorToolkit::ResolveDirectionalSelectedOwnerIndex(
	const TSharedPtr<FCharacterProfileEditorModel>& Model)
{
	if (!Model.IsValid())
	{
		return INDEX_NONE;
	}
	UPaper2DPlusCharacterProfileAsset* Profile = Model->GetAsset();
	const int32 SelectedIndex = Model->GetSelectedFlipbookIndex();
	if (!Profile || !Profile->Flipbooks.IsValidIndex(SelectedIndex))
	{
		return INDEX_NONE;
	}
	return Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
			Profile, Model->GetDirectionalPreview().BaseAnimation) == SelectedIndex
		? SelectedIndex
		: INDEX_NONE;
}

bool FCharacterProfileAssetEditorToolkit::IsDirectionalAssignmentTargetCurrent(
	const TSharedPtr<FCharacterProfileEditorModel>& Model,
	const FProfileAnimationIdentity& OwnerIdentity,
	const int32 ExpectedOwnerIndex,
	const int32 SlotIndex)
{
	if (!Model.IsValid())
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Profile = Model->GetAsset();
	const FCharacterProfileDirectionalPreview& Preview = Model->GetDirectionalPreview();
	return Profile
		&& Profile->Flipbooks.IsValidIndex(ExpectedOwnerIndex)
		&& Model->GetSelectedFlipbookIndex() == ExpectedOwnerIndex
		&& Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
			Profile, OwnerIdentity) == ExpectedOwnerIndex
		&& Preview.BaseAnimation == OwnerIdentity
		&& Preview.BaseAnimation.FallbackName.Equals(
			OwnerIdentity.FallbackName, ESearchCase::IgnoreCase)
		&& Preview.SlotIndex == SlotIndex;
}

bool FCharacterProfileAssetEditorToolkit::ActivateDirectionalHeaderForTests(FName ToolId)
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	const TSharedPtr<FTabManager> Manager = GetTabManager();
	if (!Manager.IsValid() || !Manager->TryInvokeTab(ToolId).IsValid())
	{
		return false;
	}
	FSlateApplication::Get().Tick();
	const TWeakPtr<SButton>* ButtonEntry = DirectionButtons.Find(ToolId);
	const TSharedPtr<SButton> Button = ButtonEntry ? ButtonEntry->Pin() : nullptr;
	if (!Button.IsValid())
	{
		return false;
	}
	// The behavioral button seam invokes OnClicked directly, while a real pointer click first focuses
	// its SButton. Reproduce that caller-visible opener state so the popup's focus lifecycle is tested
	// against the same starting point as designer interaction.
	FSlateApplication::Get().SetKeyboardFocus(Button, EFocusCause::SetDirectly);
	return StaticCastSharedPtr<Paper2DPlusDirectionalToolkitPrivate::SBehavioralButton>(
			Button)->ExecuteAssignedClickForTests().IsEventHandled()
		&& IsDirectionalWheelOpenForTests();
}

bool FCharacterProfileAssetEditorToolkit::IsDirectionalWheelOpenForTests() const
{
	return ActiveDirectionMenu.IsValid() && ActiveDirectionWheel.IsValid();
}

FText FCharacterProfileAssetEditorToolkit::GetActiveDirectionalWheelSummaryForTests() const
{
	const TSharedPtr<SDirectionalAnimationWheel> Wheel = ActiveDirectionWheel.Pin();
	return Wheel.IsValid() ? Wheel->GetAccessibleSummaryText() : FText::GetEmpty();
}

void FCharacterProfileAssetEditorToolkit::DismissDirectionalWheelForTests()
{
	CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/true);
}

bool FCharacterProfileAssetEditorToolkit::IsDirectionalHeaderFocusedForTests(
	const FName ToolId) const
{
	const TWeakPtr<SButton>* ButtonEntry = DirectionButtons.Find(ToolId);
	const TSharedPtr<SButton> Button = ButtonEntry ? ButtonEntry->Pin() : nullptr;
	return Button.IsValid() && Button->HasKeyboardFocus();
}

TSharedRef<SWidget> FCharacterProfileAssetEditorToolkit::WrapDirectionalShortcutScope(
	TSharedRef<SWidget> Content)
{
	return SNew(SDirectionalAnimationCommandRouter)
		.OnDirectionalAnimationPreviewKeyDown(
			FOnDirectionalAnimationPreviewKeyDown::CreateSP(
				this,
				&FCharacterProfileAssetEditorToolkit::RouteDirectionalWheelShortcut))
		[
			Content
		];
}

TSharedRef<SWidget> FCharacterProfileAssetEditorToolkit::BuildDirectionalHeaderControl(FName ToolId)
{
	// The full Direction/inspector/Assign/Clear cluster is earned chrome: it appears only for an
	// animation that actually carries a Directional Set. Everything else shows one compact entry
	// point instead of a permanently armed trio that means nothing for base-only animations.
	auto ConfiguredVisibility = [this]()
	{
		return IsDirectionalSetConfiguredForSelection()
			? EVisibility::Visible
			: EVisibility::Collapsed;
	};

	TSharedPtr<Paper2DPlusDirectionalToolkitPrivate::SBehavioralButton> DirectionButton;
	TSharedPtr<SComboButton> AssignButton;
	const TSharedRef<SHorizontalBox> Controls = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ContentPadding(FMargin(8.0f, 2.0f))
			.Text(LOCTEXT("DirectionalAddButton", "Add Directions…"))
			.ToolTipText(LOCTEXT(
				"DirectionalAddTip",
				"Create a configured-empty Directional Animation Set for this animation using the Profile's direction defaults."))
			.Visibility_Lambda([this]()
			{
				return !IsDirectionalSetConfiguredForSelection()
					&& CanOpenDirectionalWheel()
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			.OnClicked(this, &FCharacterProfileAssetEditorToolkit::AddDirectionsFromHeader)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SAssignNew(
				DirectionButton,
				Paper2DPlusDirectionalToolkitPrivate::SBehavioralButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ContentPadding(FMargin(8.0f, 2.0f))
			.Text(LOCTEXT("DirectionWheelButton", "Direction"))
			.ToolTipText(this, &FCharacterProfileAssetEditorToolkit::GetDirectionalWheelButtonTooltip)
			.AccessibleText(LOCTEXT(
				"DirectionWheelButtonAccessible",
				"Open directional animation wheel"))
			.Visibility_Lambda(ConfiguredVisibility)
			.IsEnabled(this, &FCharacterProfileAssetEditorToolkit::CanOpenDirectionalWheel)
			.OnClicked(this, &FCharacterProfileAssetEditorToolkit::OpenDirectionalWheelFromButton, ToolId)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.MaxDesiredWidth(220.0f)
			.Visibility_Lambda(ConfiguredVisibility)
			.ToolTipText(this, &FCharacterProfileAssetEditorToolkit::GetDirectionalInspectorTooltip)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(this, &FCharacterProfileAssetEditorToolkit::GetDirectionalInspectorText)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(GetDirectionalHeaderGameplayDisclosure(ToolId))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
				]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			SAssignNew(AssignButton, SComboButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ContentPadding(FMargin(7.0f, 2.0f))
			.HasDownArrow(false)
			.Visibility_Lambda(ConfiguredVisibility)
			.IsEnabled(this, &FCharacterProfileAssetEditorToolkit::CanAssignCurrentDirectionalSlot)
			.ToolTipText_Lambda([this]()
			{
				return FText::Format(
					LOCTEXT(
						"DirectionalAssignTargetTip",
						"Assign a Paper Flipbook to the inspected slot ({0}) without loading it."),
					GetDirectionalInspectorText());
			})
			.AccessibleText(LOCTEXT(
				"DirectionalAssignAccessible",
				"Assign flipbook to inspected direction slot"))
			.OnGetMenuContent(this, &FCharacterProfileAssetEditorToolkit::BuildDirectionalAssignmentPicker, ToolId)
			.ButtonContent()
			[
				SNew(STextBlock)
				// The button names its exact target so the designer never overwrites a slot blind.
				.Text_Lambda([this]()
				{
					const int32 SlotIndex = EditorModel.IsValid()
						? EditorModel->GetDirectionalPreview().SlotIndex
						: INDEX_NONE;
					int32 DirectionCount = 0;
					float AngleOffsetDegrees = 0.0f;
					if (SlotIndex == INDEX_NONE
						|| !Paper2DPlusDirectionalToolkitPrivate::GetRepresentableTopology(
							EditorModel, DirectionCount, AngleOffsetDegrees))
					{
						return LOCTEXT("DirectionalAssign", "Assign");
					}
					return FText::Format(
						LOCTEXT("DirectionalAssignTargetFmt", "Assign → {0}"),
						FText::FromString(SDirectionalAnimationWheel::GetSlotDirectionLabel(
							SlotIndex, DirectionCount, AngleOffsetDegrees)));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(3.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ContentPadding(FMargin(7.0f, 2.0f))
			.Text(LOCTEXT("DirectionalClear", "Clear"))
			.ToolTipText(LOCTEXT(
				"DirectionalClearTip",
				"Clear the exact inspected slot while preserving the configured Directional Animation Set."))
			.AccessibleText(LOCTEXT(
				"DirectionalClearAccessible",
				"Clear inspected direction slot"))
			.Visibility_Lambda(ConfiguredVisibility)
			.IsEnabled(this, &FCharacterProfileAssetEditorToolkit::CanClearCurrentDirectionalSlot)
			.OnClicked(this, &FCharacterProfileAssetEditorToolkit::ClearCurrentDirectionalSlot)
		];

	DirectionButtons.Add(ToolId, DirectionButton);
	DirectionAssignButtons.Add(ToolId, AssignButton);
	return Controls;
}

bool FCharacterProfileAssetEditorToolkit::IsDirectionalSetConfiguredForSelection() const
{
	UPaper2DPlusCharacterProfileAsset* Profile =
		EditorModel.IsValid() ? EditorModel->GetAsset() : nullptr;
	const int32 OwnerIndex = ResolveDirectionalSelectedOwnerIndex(EditorModel);
	return Profile && OwnerIndex != INDEX_NONE && Profile->HasDirectionalSet(OwnerIndex);
}

FReply FCharacterProfileAssetEditorToolkit::AddDirectionsFromHeader()
{
	UPaper2DPlusCharacterProfileAsset* Profile =
		EditorModel.IsValid() ? EditorModel->GetAsset() : nullptr;
	const int32 OwnerIndex = ResolveDirectionalSelectedOwnerIndex(EditorModel);
	if (!Profile || OwnerIndex == INDEX_NONE)
	{
		NotifyDirectionalActionRefused(LOCTEXT(
			"DirectionalAddNoSelection",
			"Add Directions requires a selected base animation."));
		return FReply::Handled();
	}
	if (Profile->HasDirectionalSet(OwnerIndex))
	{
		return FReply::Handled();
	}
	if (Profile->Flipbooks[OwnerIndex].Identity.Flipbook.IsNull())
	{
		NotifyDirectionalActionRefused(LOCTEXT(
			"DirectionalAddNoBase",
			"Add Directions requires a canonical base flipbook on the animation."));
		return FReply::Handled();
	}

	FScopedTransaction Transaction(LOCTEXT(
		"AddDirectionalSetTransaction",
		"Enable Directional Animation Set"));
	Profile->Modify();
	if (!Profile->EnableDirectionalSet(OwnerIndex))
	{
		Transaction.Cancel();
		NotifyDirectionalActionRefused(LOCTEXT(
			"DirectionalAddRefused",
			"The Directional Set could not be enabled; check the Profile's direction defaults."));
		return FReply::Handled();
	}
	Profile->MarkPackageDirty();
	EditorModel->NotifyAssetDataChanged();
	return FReply::Handled();
}

void FCharacterProfileAssetEditorToolkit::NotifyDirectionalActionRefused(
	const FText& Reason) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FNotificationInfo Info(Reason);
	Info.ExpireDuration = 4.0f;
	Info.bUseThrobber = false;
	FSlateNotificationManager::Get().AddNotification(Info);
}

FText FCharacterProfileAssetEditorToolkit::GetDirectionalWheelButtonTooltip() const
{
	const TSharedPtr<FUICommandInfo> Command =
		FPaper2DPlusDirectionalAnimationCommands::Get().OpenDirectionWheel;
	FText ChordText;
	if (Command.IsValid())
	{
		const TSharedRef<const FInputChord> ActiveChord =
			Command->GetActiveChord(EMultipleKeyBindingIndex::Primary);
		if (ActiveChord->IsValidChord())
		{
			ChordText = ActiveChord->GetInputText();
		}
	}
	return ChordText.IsEmpty()
		? LOCTEXT(
			"DirectionWheelButtonTipUnbound",
			"Open the shared direction wheel. Click or use the wheel keyboard controls to choose a slot.")
		: FText::Format(
			LOCTEXT(
				"DirectionWheelButtonTip",
				"Open the shared direction wheel. {0} opens it at the pointer; release its primary key over a slot to commit."),
			ChordText);
}

FReply FCharacterProfileAssetEditorToolkit::OpenDirectionalWheelFromButton(FName ToolId)
{
	OpenDirectionalWheel(ToolId, /*bShortcutHeld=*/false);
	return FReply::Handled();
}

void FCharacterProfileAssetEditorToolkit::OpenDirectionalWheelFromShortcut()
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return;
	}
	if (bDirectionWheelShortcutHeld && ActiveDirectionMenu.IsValid())
	{
		return;
	}
	if (PendingDirectionWheelTriggerKeys.IsEmpty()
		&& FSlateApplication::IsInitialized())
	{
		const TSharedPtr<FUICommandInfo> Command =
			FPaper2DPlusDirectionalAnimationCommands::Get().OpenDirectionWheel;
		if (Command.IsValid())
		{
			PendingDirectionWheelTriggerKeys = ResolveDirectionalWheelFallbackTriggerKeys(
				*Command->GetActiveChord(EMultipleKeyBindingIndex::Primary),
				*Command->GetActiveChord(EMultipleKeyBindingIndex::Secondary),
				FSlateApplication::Get().GetModifierKeys());
		}
	}
	OpenDirectionalWheel(
		ActiveToolId,
		/*bShortcutHeld=*/PendingDirectionWheelTriggerKeys.Num() == 1);
}

bool FCharacterProfileAssetEditorToolkit::RouteDirectionalWheelShortcut(
	const FKeyEvent& Event)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget()
		|| !CanOpenDirectionalWheel())
	{
		return false;
	}
	const TSharedPtr<FUICommandInfo> Command =
		FPaper2DPlusDirectionalAnimationCommands::Get().OpenDirectionWheel;
	if (!Command.IsValid())
	{
		return false;
	}
	const FKey TriggerKey = ResolveDirectionalWheelTriggerKey(
		*Command->GetActiveChord(EMultipleKeyBindingIndex::Primary),
		*Command->GetActiveChord(EMultipleKeyBindingIndex::Secondary),
		Event);
	if (!TriggerKey.IsValid())
	{
		return false;
	}
	PendingDirectionWheelTriggerKeys = { TriggerKey };
	const bool bHandled = GetToolkitCommands()->ProcessCommandBindings(Event);
	PendingDirectionWheelTriggerKeys.Reset();
	return bHandled;
}

bool FCharacterProfileAssetEditorToolkit::CanOpenDirectionalWheel() const
{
	if (!EditorModel.IsValid() || !EditorModel->IsDirectionalPreviewEnabled())
	{
		return false;
	}
	const FCharacterProfileDirectionalPreview& Preview = EditorModel->GetDirectionalPreview();
	UPaper2DPlusCharacterProfileAsset* Profile = EditorModel->GetAsset();
	const int32 OwnerIndex = ResolveDirectionalSelectedOwnerIndex(EditorModel);
	int32 DirectionCount = 0;
	float AngleOffsetDegrees = 0.0f;
	return Preview.BaseAnimation.IsValid()
		&& Profile && OwnerIndex != INDEX_NONE
		// A structural error is itself a wheel presentation state (invalid wedges). Effective topology
		// still has to be representable, but duplicate/inactive/missing records must not make R18's
		// validation state unreachable.
		&& Profile->GetEffectiveDirectionalSettings(
			OwnerIndex, DirectionCount, AngleOffsetDegrees)
		&& DirectionCount >= UPaper2DPlusCharacterProfileAsset::MinimumDirectionalCount
		&& DirectionCount <= UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount;
}

void FCharacterProfileAssetEditorToolkit::OpenDirectionalWheel(
	FName ToolId,
	bool bShortcutHeld)
{
	if (!CanOpenDirectionalWheel() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	TArray<FKey> ShortcutTriggerKeys;
	if (bShortcutHeld)
	{
		ShortcutTriggerKeys = MoveTemp(PendingDirectionWheelTriggerKeys);
		PendingDirectionWheelTriggerKeys.Reset();
		ShortcutTriggerKeys.RemoveAll([](const FKey& Key)
		{
			return !Key.IsValid() || Key.IsMouseButton();
		});
		if (ShortcutTriggerKeys.IsEmpty())
		{
			return;
		}
	}

	CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	FSlateApplication& SlateApp = FSlateApplication::Get();
	TSharedPtr<SWidget> ParentWidget;
	if (bShortcutHeld)
	{
		ParentWidget = SlateApp.GetKeyboardFocusedWidget();
	}
	if (!ParentWidget.IsValid())
	{
		if (const TWeakPtr<SButton>* Button = DirectionButtons.Find(ToolId))
		{
			ParentWidget = Button->Pin();
		}
	}
	if (!ParentWidget.IsValid())
	{
		return;
	}

	DirectionFocusRestoreTarget = bShortcutHeld
		? SlateApp.GetKeyboardFocusedWidget()
		: ParentWidget;
	bDirectionWheelShortcutHeld = bShortcutHeld;

	const TWeakPtr<FCharacterProfileEditorModel> WeakModel = EditorModel;
	const TSharedRef<SDirectionalAnimationWheel> Wheel =
		SNew(SDirectionalAnimationWheel)
		.DirectionCount_Lambda([WeakModel]()
		{
			int32 Count = 8;
			float Offset = 0.0f;
			Paper2DPlusDirectionalToolkitPrivate::GetRepresentableTopology(
				WeakModel.Pin(), Count, Offset);
			return Count;
		})
		.AngleOffsetDegrees_Lambda([WeakModel]()
		{
			int32 Count = 8;
			float Offset = 0.0f;
			Paper2DPlusDirectionalToolkitPrivate::GetRepresentableTopology(
				WeakModel.Pin(), Count, Offset);
			return Offset;
		})
		// Held shortcuts resolve only through the captured primary-key release. Pointer hover remains
		// live, but click/Enter/Space cannot race that release path.
		.DirectCommitEnabled(!bShortcutHeld)
		.SegmentStates_Lambda([WeakModel]()
		{
			return Paper2DPlusDirectionalToolkitPrivate::BuildSegments(WeakModel.Pin());
		})
		.CurrentSelection_Lambda([WeakModel]()
		{
			const TSharedPtr<FCharacterProfileEditorModel> Model = WeakModel.Pin();
			return Model.IsValid() ? Model->GetDirectionalPreview().SlotIndex : INDEX_NONE;
		})
		.BaseAnimationText_Lambda([WeakModel]()
		{
			const TSharedPtr<FCharacterProfileEditorModel> Model = WeakModel.Pin();
			if (!Model.IsValid())
			{
				return FText::GetEmpty();
			}
			const FProfileAnimationIdentity& Owner = Model->GetDirectionalPreview().BaseAnimation;
			return FText::FromString(
				!Owner.FallbackName.IsEmpty()
					? Owner.FallbackName
					: Owner.FlipbookPath.GetAssetName());
		})
		.OnSlotCommitted(SDirectionalAnimationWheel::FOnSlotCommitted::CreateSP(
			this,
			&FCharacterProfileAssetEditorToolkit::HandleDirectionalWheelSlotCommitted))
		.OnCancelled(SDirectionalAnimationWheel::FOnCancelled::CreateSP(
			this,
			&FCharacterProfileAssetEditorToolkit::HandleDirectionalWheelCancelled));
	ActiveDirectionWheel = Wheel;

	FVector2D SummonLocation;
	FPopupTransitionEffect Transition = FPopupTransitionEffect::ComboButton;
	if (bShortcutHeld)
	{
		// GetCursorPos is desktop pixels while ComputeDesiredSize is unscaled Slate units; center
		// on the cursor in desktop space or every DPI scale above 100% pre-hovers the NW wedge.
		const FVector2D CursorPos = SlateApp.GetCursorPos();
		const float SummonScale = SlateApp.GetApplicationScale()
			* FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(
				static_cast<float>(CursorPos.X), static_cast<float>(CursorPos.Y));
		SummonLocation = CursorPos - Wheel->ComputeDesiredSize(1.0f) * 0.5f * SummonScale;
		Transition = FPopupTransitionEffect::ContextMenu;
	}
	else
	{
		const FGeometry& Geometry = ParentWidget->GetCachedGeometry();
		SummonLocation = Geometry.LocalToAbsolute(FVector2D(0.0f, Geometry.GetLocalSize().Y));
	}

	ActiveDirectionMenu = SlateApp.PushMenu(
		ParentWidget.ToSharedRef(),
		FWidgetPath(),
		Wheel,
		SummonLocation,
		Transition,
		/*bFocusImmediately=*/true);
	if (!ActiveDirectionMenu.IsValid())
	{
		CancelDirectionalWheel(/*bDismissMenu=*/false);
		return;
	}

	DirectionMenuDismissedHandle = ActiveDirectionMenu->GetOnMenuDismissed().AddSP(
		this,
		&FCharacterProfileAssetEditorToolkit::HandleDirectionalMenuDismissed);
	DirectionAppActivationHandle = SlateApp.OnApplicationActivationStateChanged().AddSP(
		this,
		&FCharacterProfileAssetEditorToolkit::HandleDirectionalAppActivationChanged);
	DirectionSelectionChangedHandle = EditorModel->OnFlipbookSelectionChanged.AddSP(
		this,
		&FCharacterProfileAssetEditorToolkit::HandleDirectionalSelectionChanged);
	DirectionAssetDataChangedHandle = EditorModel->OnAssetDataChanged.AddSP(
		this,
		&FCharacterProfileAssetEditorToolkit::HandleDirectionalModelChanged);
	DirectionExternalModifiedHandle = EditorModel->OnAssetExternallyModified.AddSP(
		this,
		&FCharacterProfileAssetEditorToolkit::HandleDirectionalModelChanged);

	if (bShortcutHeld)
	{
		DirectionInputProcessor = MakeShared<FPaper2DPlusDirectionalWheelInputProcessor>(
			TWeakPtr<FCharacterProfileAssetEditorToolkit>(
				StaticCastSharedRef<FCharacterProfileAssetEditorToolkit>(AsShared())),
			MoveTemp(ShortcutTriggerKeys));
		SlateApp.RegisterInputPreProcessor(DirectionInputProcessor);
	}
	SlateApp.SetKeyboardFocus(Wheel, EFocusCause::SetDirectly);
}

void FCharacterProfileAssetEditorToolkit::HandleDirectionalWheelSlotCommitted(int32 SlotIndex)
{
	if (EditorModel.IsValid())
	{
		EditorModel->CommitDirectionalPreviewSlot(SlotIndex);
	}
	CancelDirectionalWheel();
}

void FCharacterProfileAssetEditorToolkit::HandleDirectionalWheelCancelled(bool bRestoreFocus)
{
	// CancelDirectionalWheel defers opener restoration until Slate's outer focus/menu transition ends.
	CancelDirectionalWheel(/*bDismissMenu=*/bRestoreFocus, bRestoreFocus);
}

void FCharacterProfileAssetEditorToolkit::HandleDirectionalMenuDismissed(
	TSharedRef<IMenu> DismissedMenu)
{
	if (ActiveDirectionMenu.Get() == &DismissedMenu.Get())
	{
		// An external dismissal (for example, clicking another control) already established the
		// user's next focus target. Clean up without scheduling opener focus over that interaction.
		CancelDirectionalWheel(/*bDismissMenu=*/false, /*bRestoreFocus=*/false);
	}
}

void FCharacterProfileAssetEditorToolkit::HandleDirectionalAppActivationChanged(bool bIsActive)
{
	if (!bIsActive)
	{
		CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/true);
	}
}

void FCharacterProfileAssetEditorToolkit::HandleDirectionalSelectionChanged(int32 NewIndex)
{
	(void)NewIndex;
	CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/true);
}

void FCharacterProfileAssetEditorToolkit::HandleDirectionalModelChanged()
{
	CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/true);
}

void FCharacterProfileAssetEditorToolkit::CancelDirectionalWheel(
	bool bDismissMenu,
	bool bRestoreFocus)
{
	if (bCancellingDirectionWheel)
	{
		return;
	}
	TGuardValue<bool> CancellingGuard(bCancellingDirectionWheel, true);

	const uint64 FocusRestoreGeneration = ++DirectionFocusRestoreGeneration;
	const TSharedPtr<IMenu> MenuToDismiss = ActiveDirectionMenu;
	const TSharedPtr<SDirectionalAnimationWheel> WheelToRelease = ActiveDirectionWheel.Pin();
	const TWeakPtr<SWidget> RestoreTarget = DirectionFocusRestoreTarget;
	if (WheelToRelease.IsValid())
	{
		// Menu teardown can move focus after this method returns. Disarm the detached wheel before
		// those late Slate callbacks can start a second cancellation and invalidate this focus policy.
		WheelToRelease->ResolveForHostTeardown();
	}
	if (EditorModel.IsValid())
	{
		if (DirectionSelectionChangedHandle.IsValid())
		{
			EditorModel->OnFlipbookSelectionChanged.Remove(DirectionSelectionChangedHandle);
		}
		if (DirectionAssetDataChangedHandle.IsValid())
		{
			EditorModel->OnAssetDataChanged.Remove(DirectionAssetDataChangedHandle);
		}
		if (DirectionExternalModifiedHandle.IsValid())
		{
			EditorModel->OnAssetExternallyModified.Remove(DirectionExternalModifiedHandle);
		}
	}
	DirectionSelectionChangedHandle.Reset();
	DirectionAssetDataChangedHandle.Reset();
	DirectionExternalModifiedHandle.Reset();

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& SlateApp = FSlateApplication::Get();
		if (DirectionAppActivationHandle.IsValid())
		{
			SlateApp.OnApplicationActivationStateChanged().Remove(DirectionAppActivationHandle);
		}
		if (DirectionInputProcessor.IsValid())
		{
			SlateApp.UnregisterInputPreProcessor(DirectionInputProcessor);
		}
		if (WheelToRelease.IsValid() && WheelToRelease->HasMouseCapture())
		{
			// Held shortcuts establish capture through focus rather than a mouse-down reply. Release it
			// explicitly so cross-window menu dismissal cannot leave a detached wheel as Slate's captor.
			SlateApp.ReleaseAllPointerCapture(SlateApp.GetUserIndexForKeyboard());
		}
	}
	DirectionAppActivationHandle.Reset();
	DirectionInputProcessor.Reset();

	if (MenuToDismiss.IsValid() && DirectionMenuDismissedHandle.IsValid())
	{
		MenuToDismiss->GetOnMenuDismissed().Remove(DirectionMenuDismissedHandle);
	}
	DirectionMenuDismissedHandle.Reset();
	ActiveDirectionWheel.Reset();
	ActiveDirectionMenu.Reset();
	DirectionFocusRestoreTarget.Reset();
	bDirectionWheelShortcutHeld = false;

	if (bDismissMenu && MenuToDismiss.IsValid())
	{
		MenuToDismiss->Dismiss();
	}
	if (bRestoreFocus && RestoreTarget.IsValid())
	{
		const TWeakPtr<FCharacterProfileAssetEditorToolkit> WeakToolkit(
			StaticCastSharedRef<FCharacterProfileAssetEditorToolkit>(AsShared()));
		const TSharedRef<int32> RestoreAttempts = MakeShared<int32>(0);
		RestoreTarget.Pin()->RegisterActiveTimer(
			0.0f,
			FWidgetActiveTimerDelegate::CreateLambda([
				WeakToolkit,
				RestoreTarget,
				FocusRestoreGeneration,
				RestoreAttempts](double, float)
			{
				const TSharedPtr<FCharacterProfileAssetEditorToolkit> Toolkit =
					WeakToolkit.Pin();
				const TSharedPtr<SWidget> Target = RestoreTarget.Pin();
				if (!Toolkit.IsValid() || !Target.IsValid()
					|| Toolkit->DirectionFocusRestoreGeneration != FocusRestoreGeneration
					|| Toolkit->ActiveDirectionMenu.IsValid()
					|| !FSlateApplication::IsInitialized()
					|| !Target->GetVisibility().IsVisible()
					|| !Target->IsEnabled())
				{
					return EActiveTimerReturnType::Stop;
				}
				if (Target->HasKeyboardFocus())
				{
					return EActiveTimerReturnType::Stop;
				}

				FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
				++*RestoreAttempts;
				// Some timeline panels request focus during the same outer menu transition. Retry for a
				// bounded number of Slate frames and stop as soon as opener focus survives one frame.
				return *RestoreAttempts < 3
					? EActiveTimerReturnType::Continue
					: EActiveTimerReturnType::Stop;
			}));
	}
}

bool FCharacterProfileAssetEditorToolkit::HandleDirectionalShortcutPrimaryReleased()
{
	if (!bDirectionWheelShortcutHeld)
	{
		return false;
	}
	const TSharedPtr<SDirectionalAnimationWheel> Wheel = ActiveDirectionWheel.Pin();
	if (!Wheel.IsValid() || !Wheel->CommitHeldSelection())
	{
		CancelDirectionalWheel();
		return false;
	}
	return true;
}

TSharedRef<SWidget> FCharacterProfileAssetEditorToolkit::BuildDirectionalAssignmentPicker(
	FName ToolId)
{
	CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	const FCharacterProfileDirectionalPreview Preview = EditorModel.IsValid()
		? EditorModel->GetDirectionalPreview()
		: FCharacterProfileDirectionalPreview();
	const FProfileAnimationIdentity OwnerIdentity = Preview.BaseAnimation;
	const int32 ExpectedOwnerIndex = ResolveDirectionalSelectedOwnerIndex(EditorModel);
	const int32 SlotIndex = Preview.SlotIndex;
	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	FAssetPickerConfig PickerConfig;
	PickerConfig.SelectionMode = ESelectionMode::Single;
	PickerConfig.InitialAssetViewType = EAssetViewType::List;
	PickerConfig.bFocusSearchBoxWhenOpened = true;
	PickerConfig.bAllowNullSelection = false;
	PickerConfig.bAllowDragging = false;
	// Seed the picker with the target's context: a reassignment opens on the slot's current
	// flipbook instead of an unranked project-wide list. (FAssetPickerConfig has no search-text
	// seed across 5.0-5.8; the bulk path for fresh assignment is Auto-fill from names.)
	{
		const FString CurrentAssignmentPath = GetCurrentDirectionalSlotObjectPath();
		if (!CurrentAssignmentPath.IsEmpty())
		{
			const IAssetRegistry& AssetRegistry =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
					TEXT("AssetRegistry")).Get();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
			const FAssetData CurrentAssignment = AssetRegistry.GetAssetByObjectPath(
				FName(*CurrentAssignmentPath));
#else
			const FAssetData CurrentAssignment = AssetRegistry.GetAssetByObjectPath(
				FSoftObjectPath(CurrentAssignmentPath));
#endif
			if (CurrentAssignment.IsValid())
			{
				PickerConfig.InitialAssetSelection = CurrentAssignment;
			}
		}
	}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	PickerConfig.Filter.ClassNames.Add(UPaperFlipbook::StaticClass()->GetFName());
#else
	PickerConfig.Filter.ClassPaths.Add(UPaperFlipbook::StaticClass()->GetClassPathName());
#endif
	PickerConfig.Filter.bRecursiveClasses = true;
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(
		this,
		&FCharacterProfileAssetEditorToolkit::HandleDirectionalAssignmentPicked,
		ToolId,
		OwnerIdentity,
		ExpectedOwnerIndex,
		SlotIndex);

	return SNew(SBox)
		.WidthOverride(380.0f)
		.HeightOverride(360.0f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(PickerConfig)
		];
}

void FCharacterProfileAssetEditorToolkit::HandleDirectionalAssignmentPicked(
	const FAssetData& AssetData,
	FName ToolId,
	FProfileAnimationIdentity OwnerIdentity,
	int32 ExpectedOwnerIndex,
	int32 SlotIndex)
{
	if (!Paper2DPlusDirectionalToolkitPrivate::IsFlipbookAsset(AssetData)
		|| !EditorModel.IsValid())
	{
		return;
	}
	const FSoftObjectPath FlipbookPath =
		Paper2DPlusDirectionalToolkitPrivate::AssetPath(AssetData);
	if (!OwnerIdentity.IsValid() || ExpectedOwnerIndex == INDEX_NONE
		|| SlotIndex == INDEX_NONE || !FlipbookPath.IsValid())
	{
		return;
	}

	// Copy picker-owned data above, then close the menu before queueing any UObject mutation. The live
	// Profile owner/topology is resolved again by CommitDirectionalAssignment after the picker is gone.
	if (const TWeakPtr<SComboButton>* Combo = DirectionAssignButtons.Find(ToolId))
	{
		if (const TSharedPtr<SComboButton> PinnedCombo = Combo->Pin())
		{
			PinnedCombo->SetIsOpen(false);
		}
	}
	const TWeakPtr<FCharacterProfileAssetEditorToolkit> WeakToolkit(
		StaticCastSharedRef<FCharacterProfileAssetEditorToolkit>(AsShared()));
	AsyncTask(ENamedThreads::GameThread, [
		WeakToolkit,
		OwnerIdentity,
		ExpectedOwnerIndex,
		SlotIndex,
		FlipbookPath]()
	{
		if (const TSharedPtr<FCharacterProfileAssetEditorToolkit> Toolkit = WeakToolkit.Pin())
		{
			Toolkit->CommitDirectionalAssignment(
				OwnerIdentity, ExpectedOwnerIndex, SlotIndex, FlipbookPath);
		}
	});
}

void FCharacterProfileAssetEditorToolkit::CommitDirectionalAssignment(
	FProfileAnimationIdentity OwnerIdentity,
	int32 ExpectedOwnerIndex,
	int32 SlotIndex,
	FSoftObjectPath FlipbookPath)
{
	UPaper2DPlusCharacterProfileAsset* Profile =
		EditorModel.IsValid() ? EditorModel->GetAsset() : nullptr;
	int32 DirectionCount = 0;
	float AngleOffsetDegrees = 0.0f;
	if (!Profile || !EditorModel.IsValid() || !FlipbookPath.IsValid()
		|| !IsDirectionalAssignmentTargetCurrent(
			EditorModel, OwnerIdentity, ExpectedOwnerIndex, SlotIndex)
		|| !Profile->GetEffectiveDirectionalSettings(
			ExpectedOwnerIndex, DirectionCount, AngleOffsetDegrees)
		|| SlotIndex < 0 || SlotIndex >= DirectionCount)
	{
		NotifyDirectionalActionRefused(LOCTEXT(
			"DirectionalAssignExpired",
			"The directional assignment expired: the selection, slot, or topology changed before it could commit. Point at the slot again and retry."));
		return;
	}
	TSoftObjectPtr<UPaperFlipbook> Existing;
	if (Profile->GetDirectionalSlot(ExpectedOwnerIndex, SlotIndex, Existing)
		&& Existing.ToSoftObjectPath() == FlipbookPath)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT(
		"AssignDirectionalAnimationSlotTransaction",
		"Assign Directional Animation Slot"));
	Profile->Modify();
	if (!Profile->SetDirectionalSlot(
		ExpectedOwnerIndex,
		SlotIndex,
		TSoftObjectPtr<UPaperFlipbook>(FlipbookPath)))
	{
		Transaction.Cancel();
		NotifyDirectionalActionRefused(LOCTEXT(
			"DirectionalAssignRefused",
			"The Profile refused the directional slot assignment; run Validate Character Profile for the structural reason."));
		return;
	}
	Profile->MarkPackageDirty();
	EditorModel->NotifyAssetDataChanged();
}

FReply FCharacterProfileAssetEditorToolkit::ClearCurrentDirectionalSlot()
{
	CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	UPaper2DPlusCharacterProfileAsset* Profile =
		EditorModel.IsValid() ? EditorModel->GetAsset() : nullptr;
	if (!Profile || !EditorModel.IsValid())
	{
		return FReply::Handled();
	}
	const FCharacterProfileDirectionalPreview Preview = EditorModel->GetDirectionalPreview();
	const int32 OwnerIndex = ResolveDirectionalSelectedOwnerIndex(EditorModel);
	TSoftObjectPtr<UPaperFlipbook> Existing;
	if (OwnerIndex == INDEX_NONE || Preview.SlotIndex == INDEX_NONE
		|| !Profile->GetDirectionalSlot(OwnerIndex, Preview.SlotIndex, Existing))
	{
		NotifyDirectionalActionRefused(LOCTEXT(
			"DirectionalClearExpired",
			"Clear expired: the inspected slot no longer holds an assignment."));
		return FReply::Handled();
	}

	FScopedTransaction Transaction(LOCTEXT(
		"ClearDirectionalAnimationSlotTransaction",
		"Clear Directional Animation Slot"));
	Profile->Modify();
	if (!Profile->ClearDirectionalSlot(OwnerIndex, Preview.SlotIndex))
	{
		Transaction.Cancel();
		NotifyDirectionalActionRefused(LOCTEXT(
			"DirectionalClearRefused",
			"The Profile refused to clear the directional slot; run Validate Character Profile for the structural reason."));
		return FReply::Handled();
	}
	Profile->MarkPackageDirty();
	EditorModel->NotifyAssetDataChanged();
	return FReply::Handled();
}

bool FCharacterProfileAssetEditorToolkit::CanAssignCurrentDirectionalSlot() const
{
	if (!EditorModel.IsValid())
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Profile = EditorModel->GetAsset();
	const FCharacterProfileDirectionalPreview& Preview = EditorModel->GetDirectionalPreview();
	const int32 OwnerIndex = ResolveDirectionalSelectedOwnerIndex(EditorModel);
	int32 DirectionCount = 0;
	float AngleOffsetDegrees = 0.0f;
	return Profile && OwnerIndex != INDEX_NONE
		&& Profile->Flipbooks.IsValidIndex(OwnerIndex)
		&& !Profile->Flipbooks[OwnerIndex].Identity.Flipbook.IsNull()
		&& Profile->GetEffectiveDirectionalSettings(
			OwnerIndex, DirectionCount, AngleOffsetDegrees)
		&& Preview.SlotIndex >= 0 && Preview.SlotIndex < DirectionCount;
}

bool FCharacterProfileAssetEditorToolkit::CanClearCurrentDirectionalSlot() const
{
	if (!EditorModel.IsValid())
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Profile = EditorModel->GetAsset();
	const FCharacterProfileDirectionalPreview& Preview = EditorModel->GetDirectionalPreview();
	const int32 OwnerIndex = ResolveDirectionalSelectedOwnerIndex(EditorModel);
	TSoftObjectPtr<UPaperFlipbook> Existing;
	return Profile && OwnerIndex != INDEX_NONE
		&& Preview.SlotIndex >= 0
		&& Preview.SlotIndex < UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount
		&& Profile->GetDirectionalSlot(OwnerIndex, Preview.SlotIndex, Existing);
}

FString FCharacterProfileAssetEditorToolkit::GetCurrentDirectionalSlotObjectPath() const
{
	if (!EditorModel.IsValid())
	{
		return FString();
	}
	UPaper2DPlusCharacterProfileAsset* Profile = EditorModel->GetAsset();
	const FCharacterProfileDirectionalPreview& Preview = EditorModel->GetDirectionalPreview();
	const int32 OwnerIndex = ResolveDirectionalSelectedOwnerIndex(EditorModel);
	TSoftObjectPtr<UPaperFlipbook> Assigned;
	return Profile && OwnerIndex != INDEX_NONE
		&& Preview.SlotIndex >= 0
		&& Preview.SlotIndex < UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount
		&& Profile->GetDirectionalSlot(OwnerIndex, Preview.SlotIndex, Assigned)
		? Assigned.ToSoftObjectPath().ToString()
		: FString();
}

FText FCharacterProfileAssetEditorToolkit::GetDirectionalInspectorText() const
{
	if (!EditorModel.IsValid())
	{
		return LOCTEXT("DirectionalInspectorUnavailable", "Direction unavailable");
	}
	const FCharacterProfileDirectionalPreview& Preview = EditorModel->GetDirectionalPreview();
	const FString OwnerName = !Preview.BaseAnimation.FallbackName.IsEmpty()
		? Preview.BaseAnimation.FallbackName
		: Preview.BaseAnimation.FlipbookPath.GetAssetName();
	// Name the direction the way the wheel and the Details rows do: compass first, raw index as
	// the fallback. The exact bearing stays available in the tooltip.
	FString SlotText = TEXT("-");
	int32 DirectionCount = 0;
	float AngleOffsetDegrees = 0.0f;
	if (Preview.SlotIndex != INDEX_NONE)
	{
		SlotText = Paper2DPlusDirectionalToolkitPrivate::GetRepresentableTopology(
			EditorModel, DirectionCount, AngleOffsetDegrees)
			? SDirectionalAnimationWheel::GetSlotDirectionLabel(
				Preview.SlotIndex, DirectionCount, AngleOffsetDegrees)
			: FString::FromInt(Preview.SlotIndex);
		if (SlotText != FString::FromInt(Preview.SlotIndex))
		{
			SlotText = FString::Printf(
				TEXT("%s (Slot %d)"), *SlotText, Preview.SlotIndex);
		}
		else
		{
			SlotText = FString::Printf(TEXT("Slot %d"), Preview.SlotIndex);
		}
	}
	else
	{
		SlotText = TEXT("Slot -");
	}
	return FText::Format(
		LOCTEXT("DirectionalInspectorFormat", "{0} · {1} · {2}"),
		FText::FromString(OwnerName.IsEmpty() ? FString(TEXT("No animation")) : OwnerName),
		FText::FromString(SlotText),
		GetDirectionalPreviewStateText(Preview.State));
}

FText FCharacterProfileAssetEditorToolkit::GetDirectionalInspectorTooltip() const
{
	if (!EditorModel.IsValid())
	{
		return LOCTEXT("DirectionalInspectorTooltipUnavailable", "No directional preview is available.");
	}
	const FCharacterProfileDirectionalPreview& Preview = EditorModel->GetDirectionalPreview();
	const FString AssignmentPath = GetCurrentDirectionalSlotObjectPath();
	return FText::Format(
		LOCTEXT(
			"DirectionalInspectorTooltipFormat",
			"{0}\nBearing: {1} deg clockwise from up\nAssignment: {2}\n{3}"),
		FText::FromString(Preview.Reason),
		FText::AsNumber(Preview.BearingDegrees),
		FText::FromString(AssignmentPath.IsEmpty() ? FString(TEXT("Empty")) : AssignmentPath),
		LOCTEXT(
			"DirectionalInspectorSharedGameplayDisclosure",
			"Gameplay edits apply to the base animation and are shared by all directions."));
}

#undef LOCTEXT_NAMESPACE
