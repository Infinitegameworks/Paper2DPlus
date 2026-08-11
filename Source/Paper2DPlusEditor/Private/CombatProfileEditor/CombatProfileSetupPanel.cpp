// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileEditor/CombatProfileSetupPanel.h"

#include "AssetRegistry/AssetData.h"
#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "Editor.h"
#include "Misc/App.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "CombatProfileSetupPanel"

void SCombatProfileSetupPanel::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	OnValidate = InArgs._OnValidate;
	if (Session.IsValid())
	{
		SessionChangedHandle = Session->OnDataChanged().AddLambda([WeakThis = TWeakPtr<SCombatProfileSetupPanel>(SharedThis(this))]()
		{
			if (const TSharedPtr<SCombatProfileSetupPanel> Pinned = WeakThis.Pin())
			{
				Pinned->Invalidate(EInvalidateWidgetReason::Layout);
			}
		});
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(4.f)
		.BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CharacterLabel", "Character"))
					.ToolTipText(LOCTEXT("CharacterLabelTip", "Character Profile that owns the physical animation and move facts."))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UPaper2DPlusCharacterProfileAsset::StaticClass())
					.ObjectPath(this, &SCombatProfileSetupPanel::GetCharacterProfilePath)
					.OnObjectChanged(this, &SCombatProfileSetupPanel::HandleCharacterProfileChanged)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), TEXT("FlatButton.Default"))
					.Text(LOCTEXT("OpenCharacter", "Open"))
					.OnClicked(this, &SCombatProfileSetupPanel::OpenCharacterProfile)
				]
				// Actions ride the Character row so the wrapping status sentence below owns full width;
				// sharing a row made the two collide at ordinary widths.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 4.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), TEXT("FlatButton.Default"))
					.Text(LOCTEXT("GenerateMissing", "Generate Missing"))
					.ToolTipText(LOCTEXT("GenerateMissingTip", "Create a tuning row for every attack that doesn't have one yet. You can also customize attacks one at a time in the inspector."))
					.OnClicked(this, &SCombatProfileSetupPanel::GenerateMissing)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), TEXT("FlatButton.Default"))
					.Text(LOCTEXT("Validation", "Validate"))
					.ToolTipText(LOCTEXT("ValidationTip", "Open the shared read-only Combat Profile validation panel."))
					.OnClicked(this, &SCombatProfileSetupPanel::RunValidation)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(this, &SCombatProfileSetupPanel::GetStatusText)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(SBorder)
				.Padding(4.f)
				.BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.DarkGroupBorder")))
				.Visibility(this, &SCombatProfileSetupPanel::GetLegacyVariableReviewVisibility)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"LegacyVariableSnapshots",
							"This asset was saved in an older format that kept a full copy of every inherited value. Review to clean up the copies that still match — anything that differs is kept."))
						.AutoWrapText(true)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ReviewVariableInheritance", "Review old values"))
						.ToolTipText(LOCTEXT(
							"ReviewVariableInheritanceTooltip",
							"Undoable. Removes only the copied rows that are identical to their current inherited value; rows that change behavior stay explicit."))
						.OnClicked(this, &SCombatProfileSetupPanel::AdoptSparseVariableOverrides)
					]
				]
			]
		]
	];
}

SCombatProfileSetupPanel::~SCombatProfileSetupPanel()
{
	if (Session.IsValid()) Session->OnDataChanged().Remove(SessionChangedHandle);
}

FString SCombatProfileSetupPanel::GetCharacterProfilePath() const
{
	const UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	return Asset && Asset->CharacterProfile ? Asset->CharacterProfile->GetPathName() : FString();
}

FText SCombatProfileSetupPanel::GetStatusText() const
{
	const UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (!Asset || !Asset->CharacterProfile)
	{
		return LOCTEXT("MissingCharacter", "Link a Character Profile to see its attacks. Move data (hitboxes, frames, damage) stays on the Character Profile — this asset only tunes how the AI ranks attacks.");
	}
	int32 Customized = 0;
	for (const FPaper2DPlusCombatAttackDerivedData& Row : Session->GetCatalog()) Customized += Row.bHasCombatProfileOption ? 1 : 0;
	return FText::Format(
		LOCTEXT("SetupStatus", "{0} attacks from the Character Profile · {1} customized here. Scoring only advises — it never plays a move."),
		FText::AsNumber(Session->GetCatalog().Num()),
		FText::AsNumber(Customized));
}

void SCombatProfileSetupPanel::SetCharacterProfileForTests(UPaper2DPlusCharacterProfileAsset* CharacterProfile)
{
	UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (!Asset || Asset->CharacterProfile == CharacterProfile) return;
	const FScopedTransaction Transaction(LOCTEXT("SetCharacterProfile", "Link Combat Character Profile"));
	Asset->Modify();
	Asset->CharacterProfile = CharacterProfile;
	Asset->RefreshAttackOptionMoveBindings();
	Asset->MarkPackageDirty();
	Session->RefreshFromAsset();
}

void SCombatProfileSetupPanel::HandleCharacterProfileChanged(const FAssetData& AssetData)
{
	SetCharacterProfileForTests(Cast<UPaper2DPlusCharacterProfileAsset>(AssetData.GetAsset()));
}

FReply SCombatProfileSetupPanel::OpenCharacterProfile()
{
	UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (Asset && Asset->CharacterProfile && GEditor && FApp::CanEverRender())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset->CharacterProfile);
	}
	return FReply::Handled();
}

FReply SCombatProfileSetupPanel::GenerateMissing()
{
	if (Session.IsValid()) Session->GenerateMissingAttackOptions();
	return FReply::Handled();
}

int32 SCombatProfileSetupPanel::AdoptSparseVariableOverridesForTests()
{
	UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (!Asset || !Asset->HasLegacyDenseVariableOverrides()) return 0;
	const FScopedTransaction Transaction(LOCTEXT(
		"AdoptSparseVariableOverrides",
		"Review Combat Variable Inheritance"));
	Asset->Modify();
	const int32 Removed = Asset->AdoptSparseVariableOverrides();
	Asset->MarkPackageDirty();
	Session->RefreshFromAsset();
	return Removed;
}

FReply SCombatProfileSetupPanel::AdoptSparseVariableOverrides()
{
	AdoptSparseVariableOverridesForTests();
	return FReply::Handled();
}

EVisibility SCombatProfileSetupPanel::GetLegacyVariableReviewVisibility() const
{
	const UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	return Asset && Asset->HasLegacyDenseVariableOverrides()
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FReply SCombatProfileSetupPanel::RunValidation()
{
	OnValidate.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
