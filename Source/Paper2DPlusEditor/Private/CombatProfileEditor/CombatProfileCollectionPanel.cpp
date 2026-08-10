// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileEditor/CombatProfileCollectionPanel.h"

#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "IDetailsView.h"
#include "InputCoreTypes.h"
#include "IStructureDetailsView.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "SlateShortcutUtils.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "CombatProfileCollectionPanel"

void SCombatProfileCollectionPanel::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	Collection = InArgs._Collection;
	if (Session.IsValid())
	{
		SessionChangedHandle = Session->OnDataChanged().AddSP(
			SharedThis(this), &SCombatProfileCollectionPanel::RefreshFromAsset);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(GetTitle())
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
			[
				SNew(SButton).Text(LOCTEXT("Add", "Add")).OnClicked(this, &SCombatProfileCollectionPanel::AddRow)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
			[
				SNew(SButton).Text(LOCTEXT("Remove", "Remove")).OnClicked(this, &SCombatProfileCollectionPanel::RemoveSelectedRow)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
			[
				SNew(SButton).Text(LOCTEXT("MoveUp", "Move up")).OnClicked(this, &SCombatProfileCollectionPanel::MoveSelectedRow, -1)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
			[
				SNew(SButton).Text(LOCTEXT("MoveDown", "Move down")).OnClicked(this, &SCombatProfileCollectionPanel::MoveSelectedRow, 1)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(6.f, 0.f, 6.f, 6.f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.35f)
			[
				SAssignNew(ListView, SListView<FRowPtr>)
				.ListItemsSource(&Rows)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SCombatProfileCollectionPanel::GenerateRow)
				.OnSelectionChanged(this, &SCombatProfileCollectionPanel::HandleSelectionChanged)
			]
			+ SSplitter::Slot().Value(0.65f)
			[
				SAssignNew(DetailsHost, SBox)
			]
		]
	];
	RefreshFromAsset();
}

SCombatProfileCollectionPanel::~SCombatProfileCollectionPanel()
{
	if (Session.IsValid())
	{
		Session->OnDataChanged().Remove(SessionChangedHandle);
	}
}

FText SCombatProfileCollectionPanel::GetTitle() const
{
	switch (Collection)
	{
	case ECombatProfileCollectionKind::Variables: return LOCTEXT("VariablesTitle", "Variables");
	case ECombatProfileCollectionKind::TagDefaults: return LOCTEXT("DefaultsTitle", "Attack-tag defaults");
	case ECombatProfileCollectionKind::ScoringProfiles: return LOCTEXT("ProfilesTitle", "Scoring profiles");
	default: return LOCTEXT("PresetsTitle", "Scenario presets");
	}
}

FText SCombatProfileCollectionPanel::GetEmptyText() const
{
	switch (Collection)
	{
	case ECombatProfileCollectionKind::Variables:
		return LOCTEXT("VariablesEmpty", "No variables. Add one, then choose its Paper2DPlus.Combat.Var tag and type.");
	case ECombatProfileCollectionKind::TagDefaults:
		return LOCTEXT("DefaultsEmpty", "No attack-tag defaults. Moves use built-in values until a tag default exists.");
	case ECombatProfileCollectionKind::ScoringProfiles:
		return LOCTEXT("ProfilesEmpty", "No named scoring profiles. Runtime queries use the built-in default scoring rules.");
	default:
		return LOCTEXT("PresetsEmpty", "No scenario presets. Presets store Score Playground context, not gameplay state.");
	}
}

UScriptStruct* SCombatProfileCollectionPanel::GetRowStruct() const
{
	switch (Collection)
	{
	case ECombatProfileCollectionKind::Variables: return FPaper2DPlusCombatVariableDefinition::StaticStruct();
	case ECombatProfileCollectionKind::TagDefaults: return FPaper2DPlusCombatTagDefaults::StaticStruct();
	case ECombatProfileCollectionKind::ScoringProfiles: return FPaper2DPlusCombatScoringProfile::StaticStruct();
	default: return FPaper2DPlusCombatScenarioPreset::StaticStruct();
	}
}

int32 SCombatProfileCollectionPanel::GetNumRows() const
{
	const UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (!Asset) return 0;
	switch (Collection)
	{
	case ECombatProfileCollectionKind::Variables: return Asset->VariableDefinitions.Num();
	case ECombatProfileCollectionKind::TagDefaults: return Asset->TagDefaults.Num();
	case ECombatProfileCollectionKind::ScoringProfiles: return Asset->ScoringProfiles.Num();
	default: return Asset->ScenarioPresets.Num();
	}
}

void* SCombatProfileCollectionPanel::GetMutableRowMemory(int32 Index) const
{
	UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (!Asset) return nullptr;
	switch (Collection)
	{
	case ECombatProfileCollectionKind::Variables: return Asset->VariableDefinitions.IsValidIndex(Index) ? &Asset->VariableDefinitions[Index] : nullptr;
	case ECombatProfileCollectionKind::TagDefaults: return Asset->TagDefaults.IsValidIndex(Index) ? &Asset->TagDefaults[Index] : nullptr;
	case ECombatProfileCollectionKind::ScoringProfiles: return Asset->ScoringProfiles.IsValidIndex(Index) ? &Asset->ScoringProfiles[Index] : nullptr;
	default: return Asset->ScenarioPresets.IsValidIndex(Index) ? &Asset->ScenarioPresets[Index] : nullptr;
	}
}

const void* SCombatProfileCollectionPanel::GetRowMemory(int32 Index) const
{
	return GetMutableRowMemory(Index);
}

FString SCombatProfileCollectionPanel::MakeIdentity(int32 Index) const
{
	const void* Memory = GetRowMemory(Index);
	if (!Memory) return FString();
	switch (Collection)
	{
	case ECombatProfileCollectionKind::Variables:
	{
		const FPaper2DPlusCombatVariableDefinition& Row = *static_cast<const FPaper2DPlusCombatVariableDefinition*>(Memory);
		return Row.VariableTag.IsValid() ? Row.VariableTag.ToString() : FString::Printf(TEXT("<unset-variable>:%d"), Index);
	}
	case ECombatProfileCollectionKind::TagDefaults:
	{
		const FPaper2DPlusCombatTagDefaults& Row = *static_cast<const FPaper2DPlusCombatTagDefaults*>(Memory);
		return Row.AttackTag.IsValid() ? Row.AttackTag.ToString() : FString::Printf(TEXT("<unset-default>:%d"), Index);
	}
	case ECombatProfileCollectionKind::ScoringProfiles:
	{
		const FName Name = static_cast<const FPaper2DPlusCombatScoringProfile*>(Memory)->ProfileName;
		return Name.IsNone() ? FString::Printf(TEXT("<unnamed-profile>:%d"), Index) : Name.ToString();
	}
	default:
	{
		const FName Name = static_cast<const FPaper2DPlusCombatScenarioPreset*>(Memory)->PresetName;
		return Name.IsNone() ? FString::Printf(TEXT("<unnamed-preset>:%d"), Index) : Name.ToString();
	}
	}
}

FText SCombatProfileCollectionPanel::MakeLabel(int32 Index) const
{
	const void* Memory = GetRowMemory(Index);
	if (!Memory) return FText::GetEmpty();
	if (Collection == ECombatProfileCollectionKind::Variables)
	{
		const FPaper2DPlusCombatVariableDefinition& Row = *static_cast<const FPaper2DPlusCombatVariableDefinition*>(Memory);
		if (!Row.DisplayName.IsEmpty()) return Row.DisplayName;
		return Row.VariableTag.IsValid() ? FText::FromName(Row.VariableTag.GetTagName()) : LOCTEXT("UnsetVariable", "New variable — choose a tag");
	}
	if (Collection == ECombatProfileCollectionKind::TagDefaults)
	{
		const FGameplayTag AttackTagValue = static_cast<const FPaper2DPlusCombatTagDefaults*>(Memory)->AttackTag;
		return AttackTagValue.IsValid() ? FText::FromName(AttackTagValue.GetTagName()) : LOCTEXT("UnsetDefault", "New default — choose an attack tag");
	}
	if (Collection == ECombatProfileCollectionKind::ScoringProfiles)
	{
		const FName Name = static_cast<const FPaper2DPlusCombatScoringProfile*>(Memory)->ProfileName;
		return Name.IsNone() ? LOCTEXT("UnnamedProfile", "Unnamed scoring profile") : FText::FromName(Name);
	}
	const FName Name = static_cast<const FPaper2DPlusCombatScenarioPreset*>(Memory)->PresetName;
	return Name.IsNone() ? LOCTEXT("UnnamedPreset", "Unnamed scenario preset") : FText::FromName(Name);
}

FText SCombatProfileCollectionPanel::MakeSummary(int32 Index) const
{
	const void* Memory = GetRowMemory(Index);
	if (!Memory) return FText::GetEmpty();
	if (Collection == ECombatProfileCollectionKind::Variables)
	{
		const FPaper2DPlusCombatVariableDefinition& Row = *static_cast<const FPaper2DPlusCombatVariableDefinition*>(Memory);
		return Row.Description.IsEmpty() ? LOCTEXT("VariableSummary", "Global, tag, and move scopes can override this value") : Row.Description;
	}
	if (Collection == ECombatProfileCollectionKind::TagDefaults)
	{
		const FPaper2DPlusCombatTagDefaults& Row = *static_cast<const FPaper2DPlusCombatTagDefaults*>(Memory);
		return FText::Format(LOCTEXT("DefaultSummary", "Weight {0} · {1} consideration(s)"), FText::AsNumber(Row.BaseWeight), FText::AsNumber(Row.Considerations.Num()));
	}
	if (Collection == ECombatProfileCollectionKind::ScoringProfiles)
	{
		const FPaper2DPlusCombatScoringProfile& Row = *static_cast<const FPaper2DPlusCombatScoringProfile*>(Memory);
		return FText::Format(LOCTEXT("ProfileSummary", "Minimum {0} · {1} global consideration(s)"), FText::AsNumber(Row.MinimumViableScore), FText::AsNumber(Row.GlobalConsiderations.Num()));
	}
	return LOCTEXT("PresetSummary", "Stored scoring context; transient playback is never saved");
}

int32 SCombatProfileCollectionPanel::ResolveIdentity(const FString& Identity) const
{
	if (Identity.IsEmpty()) return INDEX_NONE;
	int32 Match = INDEX_NONE;
	for (int32 Index = 0; Index < GetNumRows(); ++Index)
	{
		if (MakeIdentity(Index).Equals(Identity, ESearchCase::CaseSensitive))
		{
			if (Match != INDEX_NONE) return INDEX_NONE;
			Match = Index;
		}
	}
	return Match;
}

int32 SCombatProfileCollectionPanel::ResolveSelectedIndexForTests() const
{
	return ResolveIdentity(SelectedIdentity);
}

#if WITH_DEV_AUTOMATION_TESTS
int32 SCombatProfileCollectionPanel::AddRowForTests()
{
	const int32 Before = GetNumRows();
	AddRow();
	return GetNumRows() > Before ? GetNumRows() - 1 : INDEX_NONE;
}

bool SCombatProfileCollectionPanel::SelectIdentityForTests(const FString& Identity)
{
	if (ResolveIdentity(Identity) == INDEX_NONE) return false;
	SelectedIdentity = Identity;
	RebuildRows();
	RebuildDetails();
	return true;
}

bool SCombatProfileCollectionPanel::ApplyRowForTests(const void* RowMemory)
{
	UScriptStruct* Struct = GetRowStruct();
	if (!Struct || !RowMemory || !StructureScope.IsValid()) return false;
	Struct->CopyScriptStruct(StructureScope->GetStructMemory(), RowMemory);
	FPropertyChangedEvent Event(nullptr);
	ApplyStructEdit(Event);
	return true;
}

bool SCombatProfileCollectionPanel::MoveSelectedForTests(int32 Direction)
{
	const int32 Before = ResolveSelectedIndexForTests();
	MoveSelectedRow(Direction);
	return Before != INDEX_NONE && ResolveSelectedIndexForTests() == Before + Direction;
}

bool SCombatProfileCollectionPanel::RemoveSelectedForTests()
{
	const int32 Before = GetNumRows();
	RemoveSelectedRow();
	return GetNumRows() == Before - 1;
}
#endif

void SCombatProfileCollectionPanel::RebuildRows()
{
	Rows.Reset();
	for (int32 Index = 0; Index < GetNumRows(); ++Index)
	{
		FRowPtr Row = MakeShared<FCombatProfileCollectionRow>();
		Row->Identity = MakeIdentity(Index);
		Row->Label = MakeLabel(Index);
		Row->Summary = MakeSummary(Index);
		Rows.Add(MoveTemp(Row));
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
		const FRowPtr* Match = Rows.FindByPredicate([this](const FRowPtr& Row)
		{
			return Row.IsValid() && Row->Identity == SelectedIdentity;
		});
		if (Match) ListView->SetSelection(*Match, ESelectInfo::Direct);
		else ListView->ClearSelection();
	}
}

void SCombatProfileCollectionPanel::RefreshFromAsset()
{
	if (bApplyingEdit) return;
	if (ResolveIdentity(SelectedIdentity) == INDEX_NONE)
	{
		SelectedIdentity.Reset();
	}
	RebuildRows();
	RebuildDetails();
}

TSharedRef<ITableRow> SCombatProfileCollectionPanel::GenerateRow(
	FRowPtr Row,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<FRowPtr>, OwnerTable)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 3.f, 4.f, 0.f)
		[
			SNew(STextBlock).Text(Row.IsValid() ? Row->Label : FText::GetEmpty())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 3.f)
		[
			SNew(STextBlock)
			.Text(Row.IsValid() ? Row->Summary : FText::GetEmpty())
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];
}

void SCombatProfileCollectionPanel::HandleSelectionChanged(FRowPtr Row, ESelectInfo::Type)
{
	const FString NewIdentity = Row.IsValid() ? Row->Identity : FString();
	if (SelectedIdentity == NewIdentity) return;
	SelectedIdentity = NewIdentity;
	RebuildDetails();
}

void SCombatProfileCollectionPanel::RebuildDetails()
{
	if (!DetailsHost.IsValid()) return;
	StructureView.Reset();
	StructureScope.Reset();
	const int32 Index = ResolveIdentity(SelectedIdentity);
	const void* Live = GetRowMemory(Index);
	UScriptStruct* Struct = GetRowStruct();
	if (!Live || !Struct)
	{
		DetailsHost->SetContent(SNew(STextBlock).Text(GetEmptyText()).AutoWrapText(true));
		return;
	}

	StructureScope = MakeShared<FStructOnScope>(Struct);
	Struct->CopyScriptStruct(StructureScope->GetStructMemory(), Live);
	FDetailsViewArgs ViewArgs;
	ViewArgs.bHideSelectionTip = true;
	ViewArgs.bAllowSearch = true;
	ViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	FStructureDetailsViewArgs StructArgs;
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	StructureView = PropertyModule.CreateStructureDetailView(ViewArgs, StructArgs, StructureScope);
	StructureView->GetOnFinishedChangingPropertiesDelegate().AddSP(
		SharedThis(this), &SCombatProfileCollectionPanel::ApplyStructEdit);
	DetailsHost->SetContent(StructureView->GetWidget().ToSharedRef());
}

void SCombatProfileCollectionPanel::ApplyStructEdit(const FPropertyChangedEvent&)
{
	UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	UScriptStruct* Struct = GetRowStruct();
	const int32 Index = ResolveIdentity(SelectedIdentity);
	void* Live = GetMutableRowMemory(Index);
	if (!Asset || !Struct || !Live || !StructureScope.IsValid()) return;

	bApplyingEdit = true;
	const FScopedTransaction Transaction(LOCTEXT("EditCollectionRow", "Edit Combat Profile Setup Row"));
	Asset->Modify();
	if (Collection == ECombatProfileCollectionKind::Variables)
	{
		const FPaper2DPlusCombatVariableDefinition& Existing =
			*static_cast<const FPaper2DPlusCombatVariableDefinition*>(Live);
		const FPaper2DPlusCombatVariableDefinition& Edited =
			*reinterpret_cast<const FPaper2DPlusCombatVariableDefinition*>(StructureScope->GetStructMemory());
		Asset->RenameVariableTag(Existing.VariableTag, Edited.VariableTag);
	}
	Struct->CopyScriptStruct(Live, StructureScope->GetStructMemory());
	if (Collection == ECombatProfileCollectionKind::Variables) Asset->RebuildVariableBags();
	Asset->RefreshAttackOptionMoveBindings();
	Asset->MarkPackageDirty();
	SelectedIdentity = MakeIdentity(Index);
	bApplyingEdit = false;
	NotifyExplicitMutation();
}

void SCombatProfileCollectionPanel::NotifyExplicitMutation()
{
	if (Session.IsValid()) Session->RefreshFromAsset();
	else RefreshFromAsset();
}

FReply SCombatProfileCollectionPanel::AddRow()
{
	UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	if (!Asset) return FReply::Handled();
	const FScopedTransaction Transaction(LOCTEXT("AddCollectionRow", "Add Combat Profile Setup Row"));
	Asset->Modify();
	int32 NewIndex = INDEX_NONE;
	if (Collection == ECombatProfileCollectionKind::Variables)
	{
		NewIndex = Asset->VariableDefinitions.AddDefaulted();
		Asset->VariableDefinitions[NewIndex].DisplayName = LOCTEXT("NewVariableName", "New Variable");
		Asset->RebuildVariableBags();
	}
	else if (Collection == ECombatProfileCollectionKind::TagDefaults)
	{
		NewIndex = Asset->TagDefaults.AddDefaulted();
	}
	else if (Collection == ECombatProfileCollectionKind::ScoringProfiles)
	{
		NewIndex = Asset->ScoringProfiles.AddDefaulted();
		Asset->ScoringProfiles[NewIndex].ProfileName = FName(*FString::Printf(TEXT("Profile_%d"), NewIndex + 1));
	}
	else
	{
		NewIndex = Asset->ScenarioPresets.AddDefaulted();
		Asset->ScenarioPresets[NewIndex].PresetName = FName(*FString::Printf(TEXT("Scenario_%d"), NewIndex + 1));
	}
	Asset->MarkPackageDirty();
	SelectedIdentity = MakeIdentity(NewIndex);
	NotifyExplicitMutation();
	return FReply::Handled();
}

FReply SCombatProfileCollectionPanel::RemoveSelectedRow()
{
	UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	const int32 Index = ResolveIdentity(SelectedIdentity);
	if (!Asset || Index == INDEX_NONE) return FReply::Handled();
	const FScopedTransaction Transaction(LOCTEXT("RemoveCollectionRow", "Remove Combat Profile Setup Row"));
	Asset->Modify();
	if (Collection == ECombatProfileCollectionKind::Variables) Asset->VariableDefinitions.RemoveAt(Index);
	else if (Collection == ECombatProfileCollectionKind::TagDefaults) Asset->TagDefaults.RemoveAt(Index);
	else if (Collection == ECombatProfileCollectionKind::ScoringProfiles) Asset->ScoringProfiles.RemoveAt(Index);
	else Asset->ScenarioPresets.RemoveAt(Index);
	if (Collection == ECombatProfileCollectionKind::Variables) Asset->RebuildVariableBags();
	Asset->MarkPackageDirty();
	SelectedIdentity.Reset();
	NotifyExplicitMutation();
	return FReply::Handled();
}

FReply SCombatProfileCollectionPanel::MoveSelectedRow(int32 Direction)
{
	UPaper2DPlusCombatProfileAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
	const int32 From = ResolveIdentity(SelectedIdentity);
	const int32 To = From + Direction;
	if (!Asset || From == INDEX_NONE || To < 0 || To >= GetNumRows()) return FReply::Handled();
	const FScopedTransaction Transaction(LOCTEXT("MoveCollectionRow", "Reorder Combat Profile Setup Row"));
	Asset->Modify();
	if (Collection == ECombatProfileCollectionKind::Variables) Asset->VariableDefinitions.Swap(From, To);
	else if (Collection == ECombatProfileCollectionKind::TagDefaults) Asset->TagDefaults.Swap(From, To);
	else if (Collection == ECombatProfileCollectionKind::ScoringProfiles) Asset->ScoringProfiles.Swap(From, To);
	else Asset->ScenarioPresets.Swap(From, To);
	// Fallback identities encode the current array index. Rebind after the swap so an
	// unset/unnamed row remains selected instead of silently targeting its old neighbor.
	SelectedIdentity = MakeIdentity(To);
	Asset->MarkPackageDirty();
	NotifyExplicitMutation();
	return FReply::Handled();
}

FReply SCombatProfileCollectionPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		return RemoveSelectedRow();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
