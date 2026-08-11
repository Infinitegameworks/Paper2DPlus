// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalogDetailsPanel.h"

#include "AnimationTagChipUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "ProfilePropertyRow.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "ProfileRelationshipService.h"
#include "PropertyCustomizationHelpers.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagPicker.h"
#endif

#define LOCTEXT_NAMESPACE "Paper2DPlusCharacterCatalogDetails"

namespace
{
	FText CatalogDetailsCompanionLabel(EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer: return LOCTEXT("Layer", "Layer Profile");
		case EPaper2DPlusCatalogCompanion::Effect: return LOCTEXT("Effect", "Effect Profile");
		default: return LOCTEXT("Combat", "Combat Profile");
		}
	}

	FString CompanionSuffix(EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer: return TEXT("_Layers");
		case EPaper2DPlusCatalogCompanion::Effect: return TEXT("_Effects");
		default: return TEXT("_Combat");
		}
	}

	UClass* CompanionClass(EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer: return UPaper2DPlusCharacterLayerAsset::StaticClass();
		case EPaper2DPlusCatalogCompanion::Effect: return UPaper2DPlusEffectProfileAsset::StaticClass();
		default: return UPaper2DPlusCombatProfileAsset::StaticClass();
		}
	}

	FText AssignmentText(EPaper2DPlusCatalogCompanion Companion, const FSoftObjectPath& AssignedPath)
	{
		if (AssignedPath.IsNull())
		{
			return Companion == EPaper2DPlusCatalogCompanion::Effect
				? LOCTEXT("EffectUnassigned", "No Effect Profile assigned")
				: LOCTEXT("RelNone", "No profile assigned");
		}
		return LOCTEXT("CompanionAssigned", "Assigned by this Catalog row");
	}

	// The Catalog used to hand-roll its own nested-SBorder row with a fixed 38/62 split. It now
	// shares the plugin-wide details row, so the Catalog's column lines up (and drag-resizes)
	// with every other Paper2D+ detail surface.
	TSharedRef<SWidget> BuildPropertyRow(
		const FText& Label,
		const TSharedRef<SWidget>& Value,
		const FText& ToolTip = FText::GetEmpty())
	{
		return FProfilePropertyRowUtils::MakeRow(Label, Value, ToolTip);
	}

}

void SCharacterCatalogDetailsPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	check(Model.IsValid());
	if (Model->GetSelectedCharacterPath().IsNull() && !Model->GetVisibleRows().IsEmpty())
	{
		Model->SelectCharacter(Model->GetVisibleRows()[0]->CharacterPath);
	}
	ModelChangedHandle = Model->OnModelChanged().AddSP(
		this, &SCharacterCatalogDetailsPanel::HandleModelChanged);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(DetailsBox, SVerticalBox)
			]
		]
	];
	RebuildDetails();
}

SCharacterCatalogDetailsPanel::~SCharacterCatalogDetailsPanel()
{
	if (const TSharedPtr<FActiveTimerHandle> Timer = EntryTagsCommitTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	if (Model.IsValid() && ModelChangedHandle.IsValid())
	{
		Model->OnModelChanged().Remove(ModelChangedHandle);
	}
}

void SCharacterCatalogDetailsPanel::HandleModelChanged()
{
	RebuildDetails();
}

bool SCharacterCatalogDetailsPanel::FocusSelectedCharacter()
{
	const FName CharacterCategory(TEXT("Character"));
	if (const bool* CharacterExpanded = CategoryExpansion.Find(CharacterCategory);
		CharacterExpanded && !*CharacterExpanded)
	{
		CategoryExpansion.Add(CharacterCategory, true);
		RebuildDetails();
	}
	if (!StatusFocusTarget.IsValid() || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	const TWeakPtr<SCharacterCatalogDetailsPanel> WeakPanel = SharedThis(this);
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[WeakPanel](double, float)
		{
			if (const TSharedPtr<SCharacterCatalogDetailsPanel> Panel = WeakPanel.Pin();
				Panel.IsValid() && Panel->StatusFocusTarget.IsValid()
				&& FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetKeyboardFocus(
					Panel->StatusFocusTarget,
					EFocusCause::SetDirectly);
			}
			return EActiveTimerReturnType::Stop;
		}));
	return true;
}

void SCharacterCatalogDetailsPanel::RebuildDetails()
{
	if (!DetailsBox.IsValid())
	{
		return;
	}
	DetailsBox->ClearChildren();
	StatusFocusTarget.Reset();
	RemoveCharacterButton.Reset();
	const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> Row = Model->GetSelectedRow();
	if (!Row.IsValid())
	{
		const FText EmptyText = !Model->GetVisibleRows().IsEmpty()
			? LOCTEXT("SelectCharacter", "Select a character in the Roster to edit its Catalog details.")
			: LOCTEXT("EmptyRoster", "No characters match this view. Clear filters, or choose Add Characters to put Character Profiles into this Catalog.");
		DetailsBox->AddSlot().AutoHeight().Padding(12.0f)
		[
			SNew(STextBlock)
			.Text(EmptyText)
			.AutoWrapText(true)
		];
		return;
	}

	DetailsBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 1.0f)
	[
		BuildCharacterSection(*Row)
	];
	DetailsBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 1.0f)
	[
		BuildCompanionSection(*Row, EPaper2DPlusCatalogCompanion::Layer)
	];
	DetailsBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 1.0f)
	[
		BuildCompanionSection(*Row, EPaper2DPlusCatalogCompanion::Effect)
	];
	DetailsBox->AddSlot().AutoHeight()
	[
		BuildCompanionSection(*Row, EPaper2DPlusCatalogCompanion::Combat)
	];
}

TSharedRef<SWidget> SCharacterCatalogDetailsPanel::BuildCategory(
	FName CategoryId,
	const FText& Label,
	const FText& Summary,
	const TSharedRef<SWidget>& Body)
{
	const bool* SavedExpansion = CategoryExpansion.Find(CategoryId);
	return SNew(SExpandableArea)
		.AllowAnimatedTransition(false)
		.InitiallyCollapsed(SavedExpansion ? !*SavedExpansion : false)
		.OnAreaExpansionChanged_Lambda([this, CategoryId](bool bExpanded)
		{
			CategoryExpansion.Add(CategoryId, bExpanded);
		})
		.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryTop"))
		.HeaderPadding(FMargin(6.0f, 3.0f))
		.Padding(FMargin(0.0f, 1.0f, 0.0f, 4.0f))
		.HeaderContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Summary)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]
		.BodyContent()
		[
			Body
		];
}

TSharedRef<SWidget> SCharacterCatalogDetailsPanel::BuildCharacterSection(
	const FPaper2DPlusCharacterCatalogEditorRow& Row)
{
	TSharedPtr<SButton> StatusButton;
	const FSoftObjectPath CharacterPath = Row.CharacterPath;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	Rows->AddSlot().AutoHeight()
	[
		BuildPropertyRow(
			LOCTEXT("CharacterName", "Name"),
			SNew(STextBlock).Text(Row.DisplayName).ToolTipText(Row.DisplayName))
	];
	Rows->AddSlot().AutoHeight()
	[
		BuildPropertyRow(
			LOCTEXT("CharacterProfile", "Character Profile"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(CharacterPath.ToString()))
				.ToolTipText(FText::FromString(CharacterPath.ToString()))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenCharacter", "Open"))
				.OnClicked_Lambda([this, CharacterPath]()
				{
					FText Error;
					if (!Model->OpenCharacter(CharacterPath, Error)) ShowMessage(Error);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f)
			[
				SAssignNew(RemoveCharacterButton, SButton)
				.Text(LOCTEXT("RemoveCharacter", "Remove from Catalog…"))
				.ToolTipText(LOCTEXT(
					"RemoveCharacterTip",
					"Remove this Character from the Catalog without deleting its Character Profile or companion assets."))
				.AccessibleText(FText::Format(
					LOCTEXT("RemoveCharacterAccessible", "Remove {0} from the Character Catalog"),
					Row.DisplayName))
				.OnClicked_Lambda([this, CharacterPath, DisplayName = Row.DisplayName]()
				{
					RequestCharacterRemoval(CharacterPath, DisplayName);
					return FReply::Handled();
				})
			])
	];
	Rows->AddSlot().AutoHeight()
	[
		BuildPropertyRow(LOCTEXT("CharacterTags", "Tags"), BuildEntryTagsControl(Row))
	];
	Rows->AddSlot().AutoHeight()
	[
		BuildPropertyRow(
			LOCTEXT("Companions", "Companions"),
			SNew(SButton)
			.Text(LOCTEXT("SuggestCompanions", "Suggest Companions"))
			.ToolTipText(LOCTEXT(
				"SuggestCompanionsTip",
				"Fill only the empty Layer and Combat slots whose Character relationship resolves to exactly one asset. Existing assignments are never changed and ambiguous matches are reported rather than guessed."))
			.OnClicked_Lambda([this, CharacterPath]()
			{
				RequestCompanionSuggestion(CharacterPath);
				return FReply::Handled();
			}))
	];
	Rows->AddSlot().AutoHeight()
	[
		BuildPropertyRow(
			LOCTEXT("CharacterStatus", "Status"),
			SAssignNew(StatusButton, SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.Text(Row.StatusText)
			.ToolTipText(Row.StatusTooltip)
			.AccessibleText(FText::Format(
				LOCTEXT("StatusAccessible", "Character status: {0}. {1}"),
				Row.StatusText,
				Row.StatusTooltip)))
	];
	StatusFocusTarget = StatusButton;
	return BuildCategory(TEXT("Character"), LOCTEXT("CharacterCategory", "Character"), Row.StatusText, Rows);
}

bool SCharacterCatalogDetailsPanel::RequestCharacterRemoval(
	const FSoftObjectPath& CharacterPath,
	const FText& DisplayName)
{
	if (!Model.IsValid() || CharacterPath.IsNull())
	{
		return false;
	}
	const FText Confirmation = FText::Format(
		LOCTEXT(
			"ConfirmCharacterRemoval",
			"Remove {0} from this Character Catalog?\n\nThe Character Profile and all companion assets will be kept."),
		DisplayName);
	const bool bConfirmed = RemoveCharacterConfirmation
		? RemoveCharacterConfirmation(Confirmation)
		: FMessageDialog::Open(EAppMsgType::YesNo, Confirmation) == EAppReturnType::Yes;
	if (!bConfirmed)
	{
		return false;
	}
	if (!Model->RemoveCharacter(CharacterPath))
	{
		ShowMessage(LOCTEXT(
			"RemoveCharacterFailed",
			"The Character is no longer available in this Catalog. Refresh the Roster and try again."));
		return false;
	}
	return true;
}

bool SCharacterCatalogDetailsPanel::RequestCompanionSuggestion(const FSoftObjectPath& CharacterPath)
{
	if (!Model.IsValid() || CharacterPath.IsNull())
	{
		return false;
	}
	FPaper2DPlusCatalogCompanionSuggestion Result;
	if (!Model->SuggestCompanions(CharacterPath, Result))
	{
		ShowMessage(LOCTEXT(
			"SuggestCompanionsFailed",
			"This character is no longer in the Catalog. Refresh the Roster and try again."));
		return false;
	}

	const auto SlotNames = [](const TArray<EPaper2DPlusCatalogCompanion>& Slots)
	{
		TArray<FText> Names;
		for (const EPaper2DPlusCatalogCompanion Slot : Slots)
		{
			Names.Add(CatalogDetailsCompanionLabel(Slot));
		}
		return FText::Join(FText::FromString(TEXT(", ")), Names);
	};

	TArray<FText> Lines;
	Lines.Add(Result.NumAssigned > 0
		? FText::Format(
			LOCTEXT("SuggestAssigned", "Assigned {0} companion(s)."),
			FText::AsNumber(Result.NumAssigned))
		: LOCTEXT("SuggestAssignedNone", "No empty slot had exactly one matching companion."));
	if (!Result.AmbiguousSlots.IsEmpty())
	{
		Lines.Add(FText::Format(
			LOCTEXT("SuggestAmbiguous", "Left alone (more than one match): {0}. Choose one manually."),
			SlotNames(Result.AmbiguousSlots)));
	}
	if (!Result.LegacySlots.IsEmpty())
	{
		Lines.Add(FText::Format(
			LOCTEXT("SuggestLegacy", "Left alone (older relationship data): {0}. Resave those assets, then try again."),
			SlotNames(Result.LegacySlots)));
	}
	if (!Result.UnmatchedSlots.IsEmpty())
	{
		Lines.Add(FText::Format(
			LOCTEXT("SuggestUnmatched", "No candidate found for: {0}."),
			SlotNames(Result.UnmatchedSlots)));
	}
	ShowMessage(FText::Join(FText::FromString(TEXT("\n")), Lines));
	return Result.DidAnything();
}

bool SCharacterCatalogDetailsPanel::RequestSelectedCharacterRemovalForTests()
{
	const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> Row =
		Model.IsValid() ? Model->GetSelectedRow() : nullptr;
	return Row.IsValid() && RequestCharacterRemoval(Row->CharacterPath, Row->DisplayName);
}

TSharedRef<SWidget> SCharacterCatalogDetailsPanel::BuildCompanionSection(
	const FPaper2DPlusCharacterCatalogEditorRow& Row,
	EPaper2DPlusCatalogCompanion Companion)
{
	const FSoftObjectPath CharacterPath = Row.CharacterPath;
	const FSoftObjectPath AssignedPath = Model->GetCompanionPath(CharacterPath, Companion);
	const bool bRequired = Companion == EPaper2DPlusCatalogCompanion::Layer
		? Row.Entry.Requirements.bRequireLayer
		: Companion == EPaper2DPlusCatalogCompanion::Effect
			? Row.Entry.Requirements.bRequireEffect
			: Row.Entry.Requirements.bRequireCombat;
	const FText Relationship = AssignmentText(Companion, AssignedPath);
	const bool bHealthy = !AssignedPath.IsNull();
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	Rows->AddSlot().AutoHeight()
	[
		BuildPropertyRow(
			LOCTEXT("Required", "Required"),
			SNew(SCheckBox)
			.IsChecked(bRequired ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			.ToolTipText(LOCTEXT("RequiredTip", "Required profiles affect completion. Missing optional profiles remain neutral."))
			.OnCheckStateChanged_Lambda([WeakModel = TWeakPtr<FCharacterCatalogEditorModel>(Model), CharacterPath, Companion](ECheckBoxState State)
			{
				if (TSharedPtr<FCharacterCatalogEditorModel> Pinned = WeakModel.Pin())
				{
					Pinned->SetRequirement(CharacterPath, Companion, State == ECheckBoxState::Checked);
				}
			})
			[
				SNew(STextBlock).Text(bRequired ? LOCTEXT("RequiredYes", "Yes") : LOCTEXT("RequiredNo", "No"))
			])
	];
	Rows->AddSlot().AutoHeight()
	[
		BuildPropertyRow(
			LOCTEXT("AssignedProfile", "Profile"),
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(CompanionClass(Companion))
			.AllowClear(true)
			.ObjectPath(AssignedPath.ToString())
			.OnObjectChanged_Lambda([WeakModel = TWeakPtr<FCharacterCatalogEditorModel>(Model), CharacterPath, Companion](const FAssetData& AssetData)
			{
				if (TSharedPtr<FCharacterCatalogEditorModel> Pinned = WeakModel.Pin())
				{
					Pinned->SetCompanionAssignment(
						CharacterPath,
						Companion,
						AssetData.IsValid()
							? FProfileRelationshipService::GetAssetObjectPath(AssetData)
							: FSoftObjectPath());
				}
			}))
	];
	Rows->AddSlot().AutoHeight()
	[
		BuildPropertyRow(
			LOCTEXT("Relationship", "Relationship"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				// Ellipsis instead of wrapping (the same policy the Character Profile path row
				// above uses): a wrapped two- or three-line message stretched the whole row, and
				// the buttons stretched with it. The full text stays in the tooltip.
				SNew(STextBlock)
				.Text(Relationship)
				.ToolTipText(Relationship)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
			// VAlign_Center, not the default Fill: buttons keep their natural height instead of
			// growing to the row and leaving their label stranded at the top.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Open", "Open"))
				.IsEnabled(!AssignedPath.IsNull())
				.OnClicked_Lambda([this, CharacterPath, Companion]()
				{
					FText Error;
					if (!Model->OpenCompanion(CharacterPath, Companion, Error)) ShowMessage(Error);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("Create", "Create and Assign"))
				.ToolTipText(Companion == EPaper2DPlusCatalogCompanion::Effect
					? LOCTEXT("CreateEffectTip", "Create an Effect Profile and assign it to this Catalog row.")
					: LOCTEXT("CreateRelatedTip", "Create the companion with its Character relationship and assign it to this Catalog row."))
				.IsEnabled(AssignedPath.IsNull())
				.OnClicked_Lambda([this, CharacterPath, Companion]()
				{
					const FString PackageName = FPackageName::ObjectPathToPackageName(CharacterPath.ToString());
					const FString Destination = FPackageName::GetLongPackagePath(PackageName);
					const FString BaseName = FPackageName::GetShortName(PackageName) + CompanionSuffix(Companion);
					FText Error;
					if (!Model->CreateCompanion(CharacterPath, Companion, Destination, BaseName, Error)) ShowMessage(Error);
					return FReply::Handled();
				})
			])
	];
	const FText Summary = bHealthy
		? LOCTEXT("RelationshipHealthy", "Ready")
		: bRequired
			? LOCTEXT("RelationshipRequired", "Required — needs attention")
			: LOCTEXT("RelationshipOptional", "Optional");
	return BuildCategory(
		Companion == EPaper2DPlusCatalogCompanion::Layer
			? FName(TEXT("Layer"))
			: Companion == EPaper2DPlusCatalogCompanion::Effect
				? FName(TEXT("Effect"))
				: FName(TEXT("Combat")),
		CatalogDetailsCompanionLabel(Companion),
		Summary,
		Rows);
}

TSharedRef<SWidget> SCharacterCatalogDetailsPanel::BuildEntryTagsControl(
	const FPaper2DPlusCharacterCatalogEditorRow& Row)
{
	const FText Text = Row.Entry.Tags.IsEmpty()
		? LOCTEXT("NoTags", "No character tags")
		: FText::FromString(Row.Entry.Tags.ToStringSimple());
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	const FGameplayTagContainer Snapshot = Row.Entry.Tags;
	const FSoftObjectPath CharacterPath = Row.CharacterPath;
	TSharedRef<TWeakPtr<SComboButton>> WeakComboHolder = MakeShared<TWeakPtr<SComboButton>>();
	const TWeakPtr<SCharacterCatalogDetailsPanel> WeakPanel = SharedThis(this);
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ToolTipText(LOCTEXT("TagsTip", "Edit the project-defined classification tags stored on this Catalog row."))
		.AccessibleText(FText::Format(
			LOCTEXT("TagsAccessible", "Edit character tags. Current values: {0}"), Text))
		.OnGetMenuContent_Lambda([WeakPanel, WeakComboHolder, Snapshot, CharacterPath]()
		{
			return SNew(SBox).MinDesiredWidth(340.0f).Padding(2.0f)
			[
				SNew(SMenuHostedTagPickerGuard)
				[
					SNew(SGameplayTagPicker)
					.MultiSelect(true)
					.TagContainers(TArray<FGameplayTagContainer>{ Snapshot })
					.OnTagChanged_Lambda([WeakPanel, WeakComboHolder, CharacterPath](const TArray<FGameplayTagContainer>& Containers)
					{
						const FGameplayTagContainer NewTags = Containers.IsEmpty()
							? FGameplayTagContainer()
							: Containers[0];
						if (TSharedPtr<SComboButton> PinnedCombo = WeakComboHolder->Pin())
						{
							PinnedCombo->SetIsOpen(false);
						}
						if (TSharedPtr<SCharacterCatalogDetailsPanel> PinnedPanel = WeakPanel.Pin())
						{
							PinnedPanel->QueueEntryTagsCommit(CharacterPath, NewTags);
						}
					})
				]
			];
		})
		.ButtonContent()
		[
			SNew(STextBlock).Text(Text)
		];
	*WeakComboHolder = Combo;
	return Combo;
#else
	return SNew(SButton)
		.IsEnabled(false)
		.Text(Text)
		.ToolTipText(LOCTEXT("TagsFallback", "Edit Tags from Advanced Details on Unreal versions before 5.3."));
#endif
}

void SCharacterCatalogDetailsPanel::QueueEntryTagsCommitForTests(
	const FSoftObjectPath& CharacterPath,
	const FGameplayTagContainer& Tags)
{
	QueueEntryTagsCommit(CharacterPath, Tags);
}

void SCharacterCatalogDetailsPanel::FlushEntryTagsCommitForTests()
{
	if (const TSharedPtr<FActiveTimerHandle> Timer = EntryTagsCommitTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	EntryTagsCommitTimerHandle.Reset();
	HandleDeferredEntryTagsCommit(0.0, 0.0f);
}

void SCharacterCatalogDetailsPanel::QueueEntryTagsCommit(
	const FSoftObjectPath& CharacterPath,
	const FGameplayTagContainer& Tags)
{
	PendingEntryTagsCommit = FPendingEntryTagsCommit{ CharacterPath, Tags };
	if (!EntryTagsCommitTimerHandle.IsValid())
	{
		EntryTagsCommitTimerHandle = RegisterActiveTimer(
			0.0f,
			FWidgetActiveTimerDelegate::CreateSP(
				this,
				&SCharacterCatalogDetailsPanel::HandleDeferredEntryTagsCommit));
	}
}

EActiveTimerReturnType SCharacterCatalogDetailsPanel::HandleDeferredEntryTagsCommit(
	double CurrentTime,
	float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	EntryTagsCommitTimerHandle.Reset();
	if (PendingEntryTagsCommit.IsSet())
	{
		const FPendingEntryTagsCommit Commit = MoveTemp(PendingEntryTagsCommit.GetValue());
		PendingEntryTagsCommit.Reset();
		if (Model.IsValid())
		{
			Model->SetTags(Commit.CharacterPath, Commit.Tags);
		}
	}
	return EActiveTimerReturnType::Stop;
}

void SCharacterCatalogDetailsPanel::ShowMessage(const FText& Message) const
{
	if (Message.IsEmpty())
	{
		return;
	}
	if (MessageSink)
	{
		MessageSink(Message);
		return;
	}
	FMessageDialog::Open(EAppMsgType::Ok, Message);
}

#undef LOCTEXT_NAMESPACE
