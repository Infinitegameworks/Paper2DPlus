// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "EffectProfileEditor/EffectProfileDetailsPanel.h"

#include "AnimationTagChipUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "PaperFlipbook.h"
#include "ProfilePropertyRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagCombo.h"
#include "SGameplayTagPicker.h"
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "EffectProfileDetailsPanel"

void SEffectProfileDetailsPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	OnOpenAdvancedDetails = InArgs._OnOpenAdvancedDetails;
	if (Model.IsValid())
	{
		SourceChangedHandle = Model->OnSourceChanged().AddSP(
			this, &SEffectProfileDetailsPanel::RequestRefresh);
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(5.0f)
		.AccessibleText(LOCTEXT("DetailsAccessible", "Selected effect details. Edit display label, primary type, descriptors, ordering, membership, or open the source flipbook."))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ContentBox, SVerticalBox)
			]
		]
	];
	Refresh();
}

SEffectProfileDetailsPanel::~SEffectProfileDetailsPanel()
{
	if (Model.IsValid())
	{
		Model->OnSourceChanged().Remove(SourceChangedHandle);
	}
}

void SEffectProfileDetailsPanel::RequestRefresh()
{
	if (RefreshTimerHandle.IsValid())
	{
		return;
	}
	RefreshTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SEffectProfileDetailsPanel::HandleDeferredRefresh));
}

EActiveTimerReturnType SEffectProfileDetailsPanel::HandleDeferredRefresh(double, float)
{
	RefreshTimerHandle.Reset();
	Refresh();
	return EActiveTimerReturnType::Stop;
}

void SEffectProfileDetailsPanel::FlushPendingRefreshForTests()
{
	if (RefreshTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(RefreshTimerHandle.Pin().ToSharedRef());
		RefreshTimerHandle.Reset();
	}
	Refresh();
}

bool SEffectProfileDetailsPanel::HasSelectionForTests() const
{
	return Model.IsValid() && Model->HasResolvedSelection();
}

void SEffectProfileDetailsPanel::Refresh()
{
	if (!ContentBox.IsValid())
	{
		return;
	}
	ContentBox->ClearChildren();
	LabelEditor.Reset();
	TypeFocusWidget.Reset();
	DescriptorFocusWidget.Reset();
	OpenSourceFocusWidget.Reset();
	AdvancedFocusWidget.Reset();

	const FPaper2DPlusEffectProfileEntry* Entry = Model.IsValid()
		? Model->GetSelectedEntry() : nullptr;
	UPaperFlipbook* Flipbook = Model.IsValid() ? Model->GetSelectedFlipbook() : nullptr;
	if (!Entry || !Flipbook)
	{
		ContentBox->AddSlot().AutoHeight().Padding(5.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoSelection", "No effect selected. Choose a library row; search and descriptor filters never change the current selection."))
			.AutoWrapText(true)
		];
		return;
	}

	ContentBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Entry->DisplayLabel.IsEmpty() ? FText::FromString(Flipbook->GetName()) : Entry->DisplayLabel)
			.ToolTipText(FText::FromString(Model->GetSelectedEffectPath().ToString()))
			.AccessibleText(FText::Format(
				LOCTEXT("SelectedEffectAccessible", "Selected effect {0}. Source: {1}"),
				Entry->DisplayLabel.IsEmpty() ? FText::FromString(Flipbook->GetName()) : Entry->DisplayLabel,
				FText::FromString(Model->GetSelectedEffectPath().ToString())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.Clipping(EWidgetClipping::ClipToBounds)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SAssignNew(AdvancedFocusWidget, SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(3.0f, 1.0f))
			.Text(LOCTEXT("Advanced", "Advanced…"))
			.ToolTipText(LOCTEXT("AdvancedTip", "Compatibility escape hatch for the raw array, migration payload, and uncommon profile fields."))
			.AccessibleText(LOCTEXT("AdvancedAccessible", "Open advanced Effect Profile details"))
			.OnClicked(this, &SEffectProfileDetailsPanel::HandleOpenAdvanced)
		]
	];

	// Shared details rows: the label sits in the name column beside its control instead of a
	// stacked bold caption above it, so this panel reads like every other Paper2D+ surface.
	ContentBox->AddSlot().AutoHeight()
	[
		FProfilePropertyRowUtils::MakeRow(
			LOCTEXT("DisplayLabel", "Display Label"),
			SAssignNew(LabelEditor, SEditableTextBox)
			.Text(Entry->DisplayLabel)
			.HintText(FText::FromString(Flipbook->GetName()))
			.Font(FProfilePropertyRowUtils::GetPropertyFont())
			.AccessibleText(LOCTEXT("DisplayLabelAccessible", "Optional presentation label for the selected effect"))
			.OnTextCommitted(this, &SEffectProfileDetailsPanel::HandleLabelCommitted),
			LOCTEXT("DisplayLabelTip", "Optional. Presentation only — runtime identity and selection remain the flipbook object path."))
	];

	ContentBox->AddSlot().AutoHeight()
	[
		FProfilePropertyRowUtils::MakeRow(
			LOCTEXT("PrimaryType", "Effect Type"),
			BuildTypeControl(*Entry),
			LOCTEXT("PrimaryTypeTip", "The primary classification tag for this effect."))
	];

	ContentBox->AddSlot().AutoHeight()
	[
		FProfilePropertyRowUtils::MakeRow(
			LOCTEXT("Descriptors", "Descriptors"),
			BuildDescriptorControl(*Entry),
			LOCTEXT("DescriptorsTip", "Optional secondary tags used by the library filters."))
	];

	if (Entry->LegacyCategoryAwaitingRemap.IsValid())
	{
		ContentBox->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 7.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(6.0f)
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT("LegacyCategoryWarning", "Migration warning: legacy category {0} still awaits explicit remap. Assign current type/descriptors, then review the retained value in Advanced Details."),
					FText::FromName(Entry->LegacyCategoryAwaitingRemap.GetTagName())))
				.AutoWrapText(true)
			]
		];
	}

	ContentBox->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SAssignNew(OpenSourceFocusWidget, SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(3.0f, 1.0f))
			.Text(LOCTEXT("OpenSource", "Open Flipbook…"))
			.AccessibleText(LOCTEXT("OpenSourceAccessible", "Open selected source Paper Flipbook"))
			.OnClicked(this, &SEffectProfileDetailsPanel::HandleOpenSource)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SComboButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(4.0f, 1.0f))
			.ToolTipText(LOCTEXT("ActionsTip", "Reorder or remove the selected library entry."))
			.AccessibleText(LOCTEXT("ActionsAccessible", "Selected effect actions"))
			.OnGetMenuContent(this, &SEffectProfileDetailsPanel::BuildActionsMenu)
			.ButtonContent()[SNew(STextBlock).Text(LOCTEXT("Actions", "Actions"))]
		]
	];

	if (!PendingFocusField.IsNone())
	{
		const FName FieldToFocus = PendingFocusField;
		PendingFocusField = NAME_None;
		ScheduleFieldFocus(FieldToFocus);
	}
}

void SEffectProfileDetailsPanel::HandleLabelCommitted(
	const FText& NewText,
	ETextCommit::Type CommitType)
{
	if (Model.IsValid())
	{
		Model->SetSelectedDisplayLabel(NewText);
	}
}

TSharedRef<SWidget> SEffectProfileDetailsPanel::BuildTypeControl(
	const FPaper2DPlusEffectProfileEntry& Entry)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	TSharedRef<SGameplayTagCombo> Combo = SNew(SGameplayTagCombo)
		.Filter(TEXT("Paper2DPlus.Effect.Type"))
		.Tag(Entry.TypeTag)
		.OnTagChanged_Lambda([WeakPanel = TWeakPtr<SEffectProfileDetailsPanel>(SharedThis(this))](const FGameplayTag NewTag)
		{
			if (TSharedPtr<SEffectProfileDetailsPanel> Self = WeakPanel.Pin())
			{
				Self->Model->SetSelectedTypeTag(NewTag);
			}
		});
	TypeFocusWidget = Combo;
	return Combo;
#else
	TSharedRef<SButton> AdvancedButton = SNew(SButton)
		.Text(Entry.TypeTag.IsValid()
			? FText::FromName(Entry.TypeTag.GetTagName())
			: LOCTEXT("NoType", "(unclassified) - edit in Advanced Details"))
		.OnClicked(this, &SEffectProfileDetailsPanel::HandleOpenAdvanced);
	TypeFocusWidget = AdvancedButton;
	return AdvancedButton;
#endif
}

TSharedRef<SWidget> SEffectProfileDetailsPanel::BuildDescriptorControl(
	const FPaper2DPlusEffectProfileEntry& Entry)
{
	const FText DescriptorText = Entry.DescriptorTags.IsEmpty()
		? LOCTEXT("NoDescriptors", "No descriptors")
		: FText::FromString(Entry.DescriptorTags.ToStringSimple());
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	TSharedRef<TWeakPtr<SComboButton>> WeakComboHolder = MakeShared<TWeakPtr<SComboButton>>();
	const TWeakPtr<SEffectProfileDetailsPanel> WeakPanel = SharedThis(this);
	const FGameplayTagContainer Snapshot = Entry.DescriptorTags;
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.HasDownArrow(true)
		.ToolTipText(LOCTEXT("DescriptorTip", "Choose zero or more Paper2DPlus.Effect.Descriptor tags. Right-click management is intentionally disabled in this menu."))
		.AccessibleText(FText::Format(LOCTEXT("DescriptorAccessible", "Edit descriptors. Current values: {0}"), DescriptorText))
		.OnGetMenuContent_Lambda([WeakPanel, WeakComboHolder, Snapshot]()
		{
			return SNew(SBox)
				.MinDesiredWidth(330.0f)
				.Padding(2.0f)
				[
					SNew(SMenuHostedTagPickerGuard)
					[
						SNew(SGameplayTagPicker)
						.Filter(TEXT("Paper2DPlus.Effect.Descriptor"))
						.MultiSelect(true)
						.TagContainers(TArray<FGameplayTagContainer>{ Snapshot })
						.OnTagChanged_Lambda([WeakPanel, WeakComboHolder](const TArray<FGameplayTagContainer>& Containers)
						{
							if (TSharedPtr<SComboButton> PinnedCombo = WeakComboHolder->Pin())
							{
								PinnedCombo->SetIsOpen(false);
							}
							if (TSharedPtr<SEffectProfileDetailsPanel> Self = WeakPanel.Pin())
							{
								Self->Model->SetSelectedDescriptorTags(
									Containers.Num() > 0 ? Containers[0] : FGameplayTagContainer());
							}
						})
					]
				];
		})
		.ButtonContent()
		[
			SNew(STextBlock).Text(DescriptorText)
		];
	*WeakComboHolder = Combo;
	DescriptorFocusWidget = Combo;
	return Combo;
#else
	TSharedRef<SButton> AdvancedButton = SNew(SButton)
		.Text(DescriptorText)
		.ToolTipText(LOCTEXT("DescriptorAdvancedFallback", "Descriptor editing is available through Advanced Details on this engine version."))
		.OnClicked(this, &SEffectProfileDetailsPanel::HandleOpenAdvanced);
	DescriptorFocusWidget = AdvancedButton;
	return AdvancedButton;
#endif
}

TSharedRef<SWidget> SEffectProfileDetailsPanel::BuildActionsMenu()
{
	const TWeakPtr<SEffectProfileDetailsPanel> WeakPanel = SharedThis(this);
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(
		LOCTEXT("MoveUp", "Move Up"),
		LOCTEXT("MoveUpTip", "Move this entry one place earlier in library order."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([WeakPanel]()
			{
				if (const TSharedPtr<SEffectProfileDetailsPanel> Self = WeakPanel.Pin()) Self->HandleMoveUp();
			}),
			FCanExecuteAction::CreateLambda([WeakPanel]()
			{
				const TSharedPtr<SEffectProfileDetailsPanel> Self = WeakPanel.Pin();
				return Self.IsValid() && Self->Model.IsValid() && Self->Model->CanMoveSelected(-1);
			})));
	Menu.AddMenuEntry(
		LOCTEXT("MoveDown", "Move Down"),
		LOCTEXT("MoveDownTip", "Move this entry one place later in library order."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([WeakPanel]()
			{
				if (const TSharedPtr<SEffectProfileDetailsPanel> Self = WeakPanel.Pin()) Self->HandleMoveDown();
			}),
			FCanExecuteAction::CreateLambda([WeakPanel]()
			{
				const TSharedPtr<SEffectProfileDetailsPanel> Self = WeakPanel.Pin();
				return Self.IsValid() && Self->Model.IsValid() && Self->Model->CanMoveSelected(1);
			})));
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(
		LOCTEXT("Remove", "Remove from Library…"),
		LOCTEXT("RemoveTip", "Remove membership only. Existing assets are not searched or rewritten."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakPanel]()
		{
			if (const TSharedPtr<SEffectProfileDetailsPanel> Self = WeakPanel.Pin()) Self->HandleRemove();
		})));
	return Menu.MakeWidget();
}

FReply SEffectProfileDetailsPanel::HandleMoveUp()
{
	if (Model.IsValid()) Model->MoveSelected(-1);
	return FReply::Handled();
}

FReply SEffectProfileDetailsPanel::HandleMoveDown()
{
	if (Model.IsValid()) Model->MoveSelected(1);
	return FReply::Handled();
}

FReply SEffectProfileDetailsPanel::HandleRemove()
{
	if (Model.IsValid()) Model->RemoveSelected();
	return FReply::Handled();
}

FReply SEffectProfileDetailsPanel::HandleOpenSource()
{
	if (Model.IsValid()) Model->OpenSelectedSource();
	return FReply::Handled();
}

FReply SEffectProfileDetailsPanel::HandleOpenAdvanced()
{
	OnOpenAdvancedDetails.ExecuteIfBound();
	return FReply::Handled();
}

void SEffectProfileDetailsPanel::FocusField(FName FieldName)
{
	LastFocusedField = FieldName;
	PendingFocusField = FieldName;
	RequestRefresh();
}

void SEffectProfileDetailsPanel::ScheduleFieldFocus(FName FieldName)
{
	TSharedPtr<SWidget> Target;
	if (FieldName == GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, DisplayLabel))
	{
		Target = LabelEditor;
	}
	else if (FieldName == GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, TypeTag))
	{
		Target = TypeFocusWidget;
	}
	else if (FieldName == GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, DescriptorTags))
	{
		Target = DescriptorFocusWidget;
	}
	else if (FieldName == GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, EffectFlipbook))
	{
		Target = OpenSourceFocusWidget;
	}
	else
	{
		Target = AdvancedFocusWidget;
	}
	if (!Target.IsValid())
	{
		return;
	}
	if (FocusTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(FocusTimerHandle.Pin().ToSharedRef());
	}
	FocusTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateLambda(
			[WeakTarget = TWeakPtr<SWidget>(Target), WeakPanel = TWeakPtr<SEffectProfileDetailsPanel>(SharedThis(this))](double, float)
			{
				if (TSharedPtr<SWidget> PinnedTarget = WeakTarget.Pin())
				{
					FSlateApplication::Get().SetKeyboardFocus(PinnedTarget, EFocusCause::SetDirectly);
				}
				if (TSharedPtr<SEffectProfileDetailsPanel> Self = WeakPanel.Pin())
				{
					Self->FocusTimerHandle.Reset();
				}
				return EActiveTimerReturnType::Stop;
			}));
}

#undef LOCTEXT_NAMESPACE
