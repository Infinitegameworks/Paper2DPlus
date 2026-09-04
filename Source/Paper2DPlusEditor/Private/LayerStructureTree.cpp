// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "LayerStructureTree.h"

#include "CharacterProfileEditorModel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "LayerStructureTree"

namespace Paper2DPlusLayerStructurePrivate
{
	struct FRawModelMutation
	{
		explicit FRawModelMutation(FCharacterProfileEditorModel& InModel) : Model(InModel)
		{
			Model.BeginModelMutation();
		}
		~FRawModelMutation() { Model.EndModelMutation(); }
		FCharacterProfileEditorModel& Model;
	};

	FCharacterLayer* FindLayer(UPaper2DPlusCharacterLayerAsset& Asset, const FGuid& LayerId)
	{
		return Asset.Layers.FindByPredicate([&LayerId](const FCharacterLayer& Layer)
		{
			return Layer.LayerId == LayerId;
		});
	}

	const FCharacterLayer* FindLayer(const UPaper2DPlusCharacterLayerAsset& Asset, const FGuid& LayerId)
	{
		return Asset.Layers.FindByPredicate([&LayerId](const FCharacterLayer& Layer)
		{
			return Layer.LayerId == LayerId;
		});
	}

	ECheckBoxState Aggregate(int32 Enabled, int32 Total)
	{
		if (Total <= 0 || Enabled <= 0) return ECheckBoxState::Unchecked;
		return Enabled == Total ? ECheckBoxState::Checked : ECheckBoxState::Undetermined;
	}

	void NotifyAssetChanged(FCharacterProfileEditorModel& Model)
	{
		Model.ReconcileLayerSelectionIdentity();
		Model.NotifyAssetDataChanged();
	}
}

ECheckBoxState FLayerStructureController::GetGroupPreviewState(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	const FCharacterProfileEditorModel& Model,
	const FGuid& GroupId)
{
	int32 Total = 0;
	int32 Visible = 0;
	for (const FCharacterLayer& Layer : Asset.Layers)
	{
		if (Layer.GroupId != GroupId) continue;
		++Total;
		if (Model.IsLayerVisible(Layer.LayerName)) ++Visible;
	}
	return Paper2DPlusLayerStructurePrivate::Aggregate(Visible, Total);
}

void FLayerStructureController::SetLayerPreview(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& LayerId,
	bool bVisible)
{
	if (const FCharacterLayer* Layer = Paper2DPlusLayerStructurePrivate::FindLayer(Asset, LayerId))
		Model.SetLayerVisibility(Layer->LayerName, bVisible);
}

void FLayerStructureController::SetGroupPreview(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& GroupId,
	bool bVisible)
{
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	for (const FCharacterLayer& Layer : Asset.Layers)
		if (Layer.GroupId == GroupId) Model.SetLayerVisibility(Layer.LayerName, bVisible);
}

FGuid FLayerStructureController::AddGroup(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model)
{
	TSet<FString> Existing;
	for (const FCharacterLayerGroupInfo& Group : Asset.LayerGroups)
		Existing.Add(Group.DisplayName.ToString().ToLower());
	FString Name = TEXT("New Section");
	for (int32 Suffix = 2; Existing.Contains(Name.ToLower()); ++Suffix)
		Name = FString::Printf(TEXT("New Section %d"), Suffix);

	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("AddLayerGroup", "Add Layer Group"));
	Asset.Modify();
	FCharacterLayerGroupInfo& Group = Asset.LayerGroups.AddDefaulted_GetRef();
	Group.GroupId = FGuid::NewGuid();
	Group.DisplayName = FText::FromString(Name);
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return Group.GroupId;
}

bool FLayerStructureController::RenameGroup(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& GroupId,
	const FText& NewName)
{
	FCharacterLayerGroupInfo* Group = Asset.LayerGroups.FindByPredicate([&GroupId](const FCharacterLayerGroupInfo& Row)
	{
		return Row.GroupId == GroupId;
	});
	const FString Trimmed = NewName.ToString().TrimStartAndEnd();
	if (!Group || Trimmed.IsEmpty() || Group->DisplayName.ToString().Equals(Trimmed, ESearchCase::CaseSensitive))
		return false;
	if (Asset.LayerGroups.ContainsByPredicate([&](const FCharacterLayerGroupInfo& Row)
	{
		return Row.GroupId != GroupId && Row.DisplayName.ToString().Equals(Trimmed, ESearchCase::IgnoreCase);
	})) return false;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("RenameLayerGroup", "Rename Layer Group"));
	Asset.Modify();
	Group->DisplayName = FText::FromString(Trimmed);
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

bool FLayerStructureController::DeleteGroup(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& GroupId)
{
	const int32 Index = Asset.LayerGroups.IndexOfByPredicate([&GroupId](const FCharacterLayerGroupInfo& Row)
	{
		return Row.GroupId == GroupId;
	});
	if (!Asset.LayerGroups.IsValidIndex(Index)) return false;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("DeleteLayerGroup", "Delete Layer Group"));
	Asset.Modify();
	for (FCharacterLayer& Layer : Asset.Layers)
		if (Layer.GroupId == GroupId) Layer.GroupId.Invalidate();
	Asset.LayerGroups.RemoveAt(Index);
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

bool FLayerStructureController::MoveLayerToGroup(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& LayerId,
	const FGuid& GroupId)
{
	FCharacterLayer* Layer = Paper2DPlusLayerStructurePrivate::FindLayer(Asset, LayerId);
	if (!Layer || Layer->GroupId == GroupId) return false;
	if (GroupId.IsValid() && !Asset.LayerGroups.ContainsByPredicate([&GroupId](const FCharacterLayerGroupInfo& Row)
	{
		return Row.GroupId == GroupId;
	})) return false;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("MoveLayerGroup", "Move Layer to Group"));
	Asset.Modify();
	Layer->GroupId = GroupId;
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

bool FLayerStructureController::MoveLayerByDelta(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& LayerId,
	int32 Delta)
{
	const int32 From = Asset.Layers.IndexOfByPredicate([&LayerId](const FCharacterLayer& Layer)
	{
		return Layer.LayerId == LayerId;
	});
	if (!Asset.Layers.IsValidIndex(From)) return false;
	const int32 To = FMath::Clamp(From + Delta, 0, Asset.Layers.Num() - 1);
	if (From == To) return false;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("ReorderLayer", "Reorder Character Layer"));
	Asset.Modify();
	FCharacterLayer Moving = Asset.Layers[From];
	Asset.Layers.RemoveAt(From);
	Asset.Layers.Insert(MoveTemp(Moving), To);
	Model.ReconcileLayerSelectionIdentity();
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

TArray<FLayerSectionSuggestion> FLayerStructureController::DeriveSectionSuggestions(
	const TArray<FString>& LayerNames,
	const UPaper2DPlusCharacterLayerAsset* TargetAsset)
{
	TArray<FLayerSectionSuggestion> Suggestions;
	// TMap<FString, ...> is ALREADY case-insensitive in UE (Strihash + Stricmp), so "Armor" and
	// "armor" collapse to one suggestion without any ToLower() key allocation. The FIRST spelling the
	// file uses wins the name, which is the one the designer sees in Aseprite.
	TMap<FString, int32> IndexByFolder;

	for (const FString& RawLayerName : LayerNames)
	{
		const FString LayerName = RawLayerName.TrimStartAndEnd();
		if (LayerName.IsEmpty())
		{
			continue;
		}

		FString Folder;
		FString Remainder;
		// FromStart: one level. "Armor/Chest/Plate" is Armor — depth would be a second organization
		// system, which the Sections model deliberately does not have.
		if (!LayerName.Split(TEXT("/"), &Folder, &Remainder, ESearchCase::CaseSensitive, ESearchDir::FromStart))
		{
			continue; // A root-level layer stays ungrouped rather than joining an invented section.
		}
		Folder = Folder.TrimStartAndEnd();
		if (Folder.IsEmpty() || Remainder.TrimStartAndEnd().IsEmpty())
		{
			continue; // "/Leaf" and "Folder/" are malformed paths, not sections.
		}

		int32 Index = INDEX_NONE;
		if (const int32* Found = IndexByFolder.Find(Folder))
		{
			Index = *Found;
		}
		else
		{
			Index = Suggestions.AddDefaulted();
			Suggestions[Index].SourceFolder = Folder;
			Suggestions[Index].SectionName = Folder;
			IndexByFolder.Add(Folder, Index);
		}
		Suggestions[Index].LayerNames.AddUnique(LayerName);
	}

	if (TargetAsset)
	{
		for (FLayerSectionSuggestion& Suggestion : Suggestions)
		{
			Suggestion.bMatchesExistingSection = TargetAsset->LayerGroups.ContainsByPredicate(
				[&Suggestion](const FCharacterLayerGroupInfo& Row)
				{
					return Row.DisplayName.ToString().TrimStartAndEnd().Equals(
						Suggestion.SectionName, ESearchCase::IgnoreCase);
				});
		}
	}
	return Suggestions;
}

bool FLayerStructureController::ApplySectionSuggestions(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel* Model,
	const TArray<FLayerSectionSuggestion>& Suggestions,
	FLayerSectionApplyReport& OutReport)
{
	OutReport = FLayerSectionApplyReport();

	// ---- Phase 1: resolve the whole plan with ZERO mutation, so a no-op apply can bail out before
	// opening a transaction or dirtying the package.
	struct FPlannedSection
	{
		FString Name;
		FGuid ExistingGroupId;              // valid = merge into this row, invalid = mint one
		TArray<FString> MemberLayerNames;   // in first-seen order
	};
	TArray<FPlannedSection> Planned;
	TMap<FString, int32> PlannedIndexByName;

	for (const FLayerSectionSuggestion& Suggestion : Suggestions)
	{
		if (!Suggestion.bAccepted)
		{
			continue;
		}
		const FString Name = Suggestion.SectionName.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			continue; // A nameless Section is unusable in the dock; refuse rather than invent one.
		}

		int32 PlanIndex = INDEX_NONE;
		if (const int32* Found = PlannedIndexByName.Find(Name))
		{
			// Two suggestions renamed to the same thing are ONE Section. Minting two rows with equal
			// DisplayNames would produce a dock the designer cannot tell apart, and RenameGroup
			// already refuses that collision.
			PlanIndex = *Found;
		}
		else
		{
			PlanIndex = Planned.AddDefaulted();
			Planned[PlanIndex].Name = Name;
			if (const FCharacterLayerGroupInfo* ExistingRow = Asset.LayerGroups.FindByPredicate(
				[&Name](const FCharacterLayerGroupInfo& Row)
				{
					return Row.DisplayName.ToString().TrimStartAndEnd().Equals(Name, ESearchCase::IgnoreCase);
				}))
			{
				Planned[PlanIndex].ExistingGroupId = ExistingRow->GroupId;
			}
			PlannedIndexByName.Add(Name, PlanIndex);
		}

		for (const FString& MemberName : Suggestion.LayerNames)
		{
			Planned[PlanIndex].MemberLayerNames.AddUnique(MemberName.TrimStartAndEnd());
		}
	}

	// Which layer each plan wants, keyed by name (case-insensitive by FString map semantics). A name
	// claimed by two plans resolves to the last one, matching "the plan list is read in order".
	TMap<FString, int32> PlanIndexByLayerName;
	for (int32 PlanIndex = 0; PlanIndex < Planned.Num(); ++PlanIndex)
	{
		for (const FString& MemberName : Planned[PlanIndex].MemberLayerNames)
		{
			if (!MemberName.IsEmpty())
			{
				PlanIndexByLayerName.Add(MemberName, PlanIndex);
			}
		}
	}

	TSet<FString> MatchedMemberNames;
	TArray<int32> LayerIndicesToAssign;   // parallel with PlanIndexForLayer below
	TArray<int32> PlanIndexForLayer;
	TSet<int32> PlansThatPlaceSomething;
	for (int32 LayerIndex = 0; LayerIndex < Asset.Layers.Num(); ++LayerIndex)
	{
		const FCharacterLayer& Layer = Asset.Layers[LayerIndex];
		const int32* PlanIndex = PlanIndexByLayerName.Find(Layer.LayerName.TrimStartAndEnd());
		if (!PlanIndex)
		{
			continue;
		}
		MatchedMemberNames.Add(Layer.LayerName.TrimStartAndEnd());
		PlansThatPlaceSomething.Add(*PlanIndex);

		const FGuid& AlreadyThere = Planned[*PlanIndex].ExistingGroupId;
		if (AlreadyThere.IsValid() && Layer.GroupId == AlreadyThere)
		{
			continue; // Already in this Section — re-applying must not re-dirty the package.
		}
		LayerIndicesToAssign.Add(LayerIndex);
		PlanIndexForLayer.Add(*PlanIndex);
	}

	for (const FPlannedSection& Plan : Planned)
	{
		for (const FString& MemberName : Plan.MemberLayerNames)
		{
			if (!MemberName.IsEmpty() && !MatchedMemberNames.Contains(MemberName))
			{
				OutReport.UnmatchedLayerNames.AddUnique(MemberName);
			}
		}
	}

	// A Section whose every member is missing is NOT created: an empty row the designer never asked
	// for is noise, and the accepted set is re-applied once the import has produced the layers.
	TArray<int32> PlansNeedingNewRow;
	for (int32 PlanIndex = 0; PlanIndex < Planned.Num(); ++PlanIndex)
	{
		if (!Planned[PlanIndex].ExistingGroupId.IsValid() && PlansThatPlaceSomething.Contains(PlanIndex))
		{
			PlansNeedingNewRow.Add(PlanIndex);
		}
	}

	if (PlansNeedingNewRow.Num() == 0 && LayerIndicesToAssign.Num() == 0)
	{
		return false; // No transaction, no Modify(), no dirty package.
	}

	// ---- Phase 2: ONE transaction. The rows and the GroupIds that point at them are written
	// together — see the header for why splitting them silently un-sections everything.
	TUniquePtr<Paper2DPlusLayerStructurePrivate::FRawModelMutation> Mutation;
	if (Model)
	{
		Mutation = MakeUnique<Paper2DPlusLayerStructurePrivate::FRawModelMutation>(*Model);
	}
	FScopedTransaction Transaction(LOCTEXT("ApplyLayerSectionSuggestions", "Apply Section Suggestions"));
	Asset.Modify();

	for (const int32 PlanIndex : PlansNeedingNewRow)
	{
		FCharacterLayerGroupInfo& Row = Asset.LayerGroups.AddDefaulted_GetRef();
		Row.GroupId = FGuid::NewGuid();
		Row.DisplayName = FText::FromString(Planned[PlanIndex].Name);
		Planned[PlanIndex].ExistingGroupId = Row.GroupId;
		++OutReport.SectionsCreated;
	}
	OutReport.SectionsReused = PlansThatPlaceSomething.Num() - OutReport.SectionsCreated;

	for (int32 Slot = 0; Slot < LayerIndicesToAssign.Num(); ++Slot)
	{
		const FGuid& SectionId = Planned[PlanIndexForLayer[Slot]].ExistingGroupId;
		checkf(SectionId.IsValid(),
			TEXT("A Section assignment resolved no row; EnsureLayerAuthoringIdentity would invalidate it."));
		FCharacterLayer& Layer = Asset.Layers[LayerIndicesToAssign[Slot]];
		if (Layer.GroupId != SectionId)
		{
			Layer.GroupId = SectionId;
			++OutReport.LayersAssigned;
		}
	}

	if (Model)
	{
		Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(*Model);
	}
	return OutReport.ChangedAnything();
}

bool FLayerStructureController::DeleteLayer(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& LayerId)
{
	const int32 Index = Asset.Layers.IndexOfByPredicate([&LayerId](const FCharacterLayer& Layer)
	{
		return Layer.LayerId == LayerId;
	});
	if (!Asset.Layers.IsValidIndex(Index)) return false;
	const FGuid RemovedLayerId = Asset.Layers[Index].LayerId;
	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	FScopedTransaction Transaction(LOCTEXT("DeleteCharacterLayer", "Delete Character Layer"));
	Asset.Modify();
	Asset.Layers.RemoveAt(Index);
	for (FCharacterLayerAppearancePreset& Preset : Asset.AppearancePresets)
	{
		Preset.ActiveLayerIds.Remove(RemovedLayerId);
	}
	// Stable selection never jumps to an arbitrary neighbor. Deleting another row preserves the selected
	// LayerId; deleting the selected row leaves that identity explicit-but-unresolved until the designer
	// chooses another source. NotifyAssetChanged reconciles only the cached array index.
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

TSharedRef<FLayerStructureRowDragDropOp> FLayerStructureRowDragDropOp::New(
	const FGuid& InLayerId,
	const FText& InLabel,
	UPaper2DPlusCharacterLayerAsset* InSourceAsset)
{
	TSharedRef<FLayerStructureRowDragDropOp> Op = MakeShareable(new FLayerStructureRowDragDropOp());
	Op->LayerId = InLayerId;
	Op->SourceAsset = InSourceAsset;
	Op->HoverText = InLabel;
	Op->Construct();
	return Op;
}

TSharedPtr<SWidget> FLayerStructureRowDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(6.0f, 2.0f))
		[
			SNew(STextBlock).Text(HoverText)
		];
}

FLayerStructureDropPlan FLayerStructureController::PlanDrop(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	const FGuid& DraggedLayerId,
	ELayerStructureNodeKind TargetKind,
	const FGuid& TargetId,
	ELayerStructureDropZone Zone)
{
	FLayerStructureDropPlan Plan;
	const int32 From = Asset.Layers.IndexOfByPredicate([&DraggedLayerId](const FCharacterLayer& Layer)
	{
		return Layer.LayerId == DraggedLayerId;
	});
	if (!Asset.Layers.IsValidIndex(From)) return Plan;
	const FGuid CurrentGroup = Asset.Layers[From].GroupId;

	if (TargetKind == ELayerStructureNodeKind::Group)
	{
		// A Section header names a container, not a gap: membership changes and the global order is
		// deliberately left alone. An invalid TargetId is the synthesized Ungrouped row, i.e. "take this
		// layer out of its Section" — a real gesture, not a failure.
		if (Zone != ELayerStructureDropZone::Onto) return Plan;
		if (TargetId == CurrentGroup) return Plan;
		if (TargetId.IsValid() && !Asset.LayerGroups.ContainsByPredicate(
			[&TargetId](const FCharacterLayerGroupInfo& Row) { return Row.GroupId == TargetId; }))
		{
			return Plan;
		}
		Plan.bValid = true;
		Plan.bAssignGroup = true;
		Plan.GroupId = TargetId;
		return Plan;
	}

	// A layer row names a GAP above or below itself. Onto is never meaningful here — layers do not
	// contain layers — and the widget never offers it.
	if (Zone == ELayerStructureDropZone::Onto) return Plan;
	if (TargetId == DraggedLayerId) return Plan;
	const int32 TargetIndex = Asset.Layers.IndexOfByPredicate([&TargetId](const FCharacterLayer& Layer)
	{
		return Layer.LayerId == TargetId;
	});
	if (!Asset.Layers.IsValidIndex(TargetIndex)) return Plan;

	const int32 InsertBefore = Zone == ELayerStructureDropZone::Below ? TargetIndex + 1 : TargetIndex;
	// Where the layer actually lands once its own removal has closed the gap behind it.
	const int32 Landing = InsertBefore > From ? InsertBefore - 1 : InsertBefore;
	const FGuid DestinationGroup = Asset.Layers[TargetIndex].GroupId;
	const bool bGroupChanges = DestinationGroup != CurrentGroup;
	// Dropping a row back onto its own gap must not open a transaction or dirty the package.
	if (Landing == From && !bGroupChanges) return Plan;

	Plan.bValid = true;
	Plan.InsertBeforeIndex = InsertBefore;
	Plan.bAssignGroup = bGroupChanges;
	Plan.GroupId = DestinationGroup;
	return Plan;
}

bool FLayerStructureController::ApplyDropPlan(
	UPaper2DPlusCharacterLayerAsset& Asset,
	FCharacterProfileEditorModel& Model,
	const FGuid& LayerId,
	const FLayerStructureDropPlan& Plan)
{
	if (!Plan.bValid) return false;
	const int32 From = Asset.Layers.IndexOfByPredicate([&LayerId](const FCharacterLayer& Layer)
	{
		return Layer.LayerId == LayerId;
	});
	if (!Asset.Layers.IsValidIndex(From)) return false;

	Paper2DPlusLayerStructurePrivate::FRawModelMutation Mutation(Model);
	// ONE transaction for both halves. A cross-section drag that undid as two steps would leave the
	// layer sitting in a Section the designer never picked.
	FScopedTransaction Transaction(LOCTEXT("DropLayer", "Move Character Layer"));
	Asset.Modify();
	// Membership first, while From is still the moving layer's index.
	if (Plan.bAssignGroup)
	{
		Asset.Layers[From].GroupId = Plan.GroupId;
	}
	if (Plan.InsertBeforeIndex != INDEX_NONE)
	{
		const int32 To = FMath::Clamp(
			Plan.InsertBeforeIndex > From ? Plan.InsertBeforeIndex - 1 : Plan.InsertBeforeIndex,
			0, Asset.Layers.Num() - 1);
		if (To != From)
		{
			FCharacterLayer Moving = Asset.Layers[From];
			Asset.Layers.RemoveAt(From);
			Asset.Layers.Insert(MoveTemp(Moving), To);
		}
	}
	Model.ReconcileLayerSelectionIdentity();
	Paper2DPlusLayerStructurePrivate::NotifyAssetChanged(Model);
	return true;
}

void SLayerStructureTree::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	LayerAsset = InArgs._LayerAsset;
	if (Model.IsValid())
	{
		ModelLayerSelectionHandle = Model->OnLayerSelectionChanged.AddSP(
			this, &SLayerStructureTree::HandleModelLayerSelectionChanged);
		ModelAssetDataHandle = Model->OnAssetDataChanged.AddSP(this, &SLayerStructureTree::HandleExternalChange);
		ModelExternalHandle = Model->OnAssetExternallyModified.AddSP(this, &SLayerStructureTree::HandleExternalChange);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		// Header. "Structure" plus a bare "P" glyph said nothing about what the dock holds; the
		// count and the search are what make a 38-layer asset scannable.
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(3.0f, 2.0f, 3.0f, 0.0f))
		[
			SNew(SHorizontalBox)
			.ToolTipText(LOCTEXT("StructureHeaderTip", "Every layer, in the global paint / gameplay / publish order. Sections are organization only \x2014 they never change that order, which is why each row shows its global position. The checkbox is temporary Preview visibility and is never published."))
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StructureHeading", "Layers"))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text_Lambda([this]()
				{
					return SearchText.IsEmpty()
						? FText::AsNumber(TotalLayerCount)
						: FText::Format(LOCTEXT("LayerCountFiltered", "{0} / {1}"),
							FText::AsNumber(MatchingLayerCount), FText::AsNumber(TotalLayerCount));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(5.0f, 0.0f))
				.Text(LOCTEXT("AddGroup", "+ Section"))
				.ToolTipText(LOCTEXT("AddGroupTip", "Add a Section. Sections group layers for authoring only \x2014 they never change paint order, runtime state, appearance, or bake output."))
				.AccessibleText(LOCTEXT("AddGroupAccessible", "Add layer section"))
				.OnClicked(this, &SLayerStructureTree::AddGroupClicked)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(3.0f, 2.0f, 3.0f, 2.0f))
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search layers\x2026"))
			.ToolTipText(LOCTEXT("SearchTip", "Match layer names, their imported source paths, and Section names. Sections stay visible as ancestors of their matches."))
			.OnTextChanged_Lambda([this](const FText& NewText)
			{
				const FString Incoming = NewText.ToString().TrimStartAndEnd().ToLower();
				// Same-value guard: OnTextChanged fires on focus churn too, and Refresh() rebuilds
				// every row and re-asserts selection.
				if (Incoming.Equals(SearchText, ESearchCase::CaseSensitive))
				{
					return;
				}
				SearchText = Incoming;
				Refresh();
			})
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FLayerStructureNode>>)
				.TreeItemsSource(&Roots)
				.OnGenerateRow(this, &SLayerStructureTree::GenerateRow)
				.OnGetChildren(this, &SLayerStructureTree::HandleGetChildren)
				.OnSelectionChanged(this, &SLayerStructureTree::HandleSelectionChanged)
				.SelectionMode(ESelectionMode::Single)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Empty", "No layers yet. Import layered art or add a layer to begin."))
				.AutoWrapText(true)
				.Visibility_Lambda([this]()
				{
					const UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
					return !Asset || Asset->Layers.IsEmpty()
						? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
		]
	];
	Refresh();
}

SLayerStructureTree::~SLayerStructureTree()
{
	if (Model.IsValid())
	{
		Model->OnLayerSelectionChanged.Remove(ModelLayerSelectionHandle);
		Model->OnAssetDataChanged.Remove(ModelAssetDataHandle);
		Model->OnAssetExternallyModified.Remove(ModelExternalHandle);
	}
}

void SLayerStructureTree::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const float NewWidth = AllottedGeometry.GetLocalSize().X;
	if (!FMath::IsNearlyEqual(NewWidth, CachedDockWidth, 1.0f))
	{
		CachedDockWidth = NewWidth;
		// Row contents are bound to this through Visibility lambdas; a repaint is enough.
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SLayerStructureTree::Refresh()
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (!Asset || !TreeView.IsValid()) return;
	TSet<FGuid> Collapsed;
	for (const TSharedPtr<FLayerStructureNode>& Root : Roots)
		if (Root.IsValid() && !TreeView->IsItemExpanded(Root)) Collapsed.Add(Root->GroupId);
	Roots.Reset();
	GroupNameWidgets.Reset();
	TMap<FGuid, TSharedPtr<FLayerStructureNode>> Groups;
	// A section whose own NAME matches keeps all of its children, so searching "outfit" shows the
	// Outfit section intact rather than emptying it.
	TSet<FGuid> SectionNameMatches;
	for (const FCharacterLayerGroupInfo& Group : Asset->LayerGroups)
	{
		TSharedPtr<FLayerStructureNode> Node = MakeShared<FLayerStructureNode>();
		Node->Kind = ELayerStructureNodeKind::Group;
		Node->GroupId = Group.GroupId;
		Node->Label = Group.DisplayName;
		Roots.Add(Node);
		Groups.Add(Group.GroupId, Node);
		if (!SearchText.IsEmpty() && Group.DisplayName.ToString().ToLower().Contains(SearchText))
		{
			SectionNameMatches.Add(Group.GroupId);
		}
	}
	TSharedPtr<FLayerStructureNode> Ungrouped = MakeShared<FLayerStructureNode>();
	Ungrouped->Kind = ELayerStructureNodeKind::Group;
	Ungrouped->Label = LOCTEXT("Ungrouped", "Ungrouped");
	TSharedPtr<FLayerStructureNode> SelectedNode;
	TotalLayerCount = Asset->Layers.Num();
	MatchingLayerCount = 0;
	// Sections are roots and are NEVER filtered away: the tree's row count is the sum of the roots'
	// children, and an un-sectioned layer must stay under the synthesized Ungrouped root rather
	// than move to root level. Filtering therefore happens on CHILDREN only.
	for (int32 LayerIndex = 0; LayerIndex < Asset->Layers.Num(); ++LayerIndex)
	{
		const FCharacterLayer& Layer = Asset->Layers[LayerIndex];
		const bool bMatches = MatchesSearch(Layer.LayerName)
			|| SectionNameMatches.Contains(Layer.GroupId);

		TSharedPtr<FLayerStructureNode>* Parent = Groups.Find(Layer.GroupId);
		TSharedPtr<FLayerStructureNode> ParentNode = Parent ? *Parent : Ungrouped;
		++ParentNode->TotalChildCount;
		if (!bMatches)
		{
			continue;
		}
		++MatchingLayerCount;

		TSharedPtr<FLayerStructureNode> Node = MakeShared<FLayerStructureNode>();
		Node->Kind = ELayerStructureNodeKind::Layer;
		Node->LayerId = Layer.LayerId;
		Node->GroupId = Layer.GroupId;
		Node->Label = FText::FromString(Layer.LayerName);
		// The AUTHORITATIVE global position, not a section-local index.
		Node->GlobalOrder = LayerIndex;
		ParentNode->Children.Add(Node);
		if (Model.IsValid() && Layer.LayerId == Model->GetSelectedLayerId()) SelectedNode = Node;
	}
	if (Ungrouped->TotalChildCount > 0) Roots.Add(Ungrouped);
	TreeView->RequestTreeRefresh();
	for (const TSharedPtr<FLayerStructureNode>& Root : Roots)
		TreeView->SetItemExpansion(Root, !Collapsed.Contains(Root->GroupId));
	if (!SelectedNode.IsValid() && Model.IsValid() && !Asset->Layers.IsEmpty()
		&& !Model->GetSelectedLayerId().IsValid())
	{
		Model->SetSelectedLayerById(Asset->Layers[0].LayerId);
		return;
	}
	bSyncingSelection = true;
	TreeView->ClearSelection();
	if (SelectedNode.IsValid()) TreeView->SetItemSelection(SelectedNode, true, ESelectInfo::Direct);
	bSyncingSelection = false;
}

int32 SLayerStructureTree::GetLayerRowCountForTests() const
{
	int32 Count = 0;
	for (const TSharedPtr<FLayerStructureNode>& Root : Roots) if (Root.IsValid()) Count += Root->Children.Num();
	return Count;
}

bool SLayerStructureTree::MatchesSearch(const FString& LayerName) const
{
	// LayerName carries the imported source path (e.g. "Clothes/Jacket/Front"), so one Contains
	// covers both the designer-facing name and the source path the brief asks to search.
	return SearchText.IsEmpty() || LayerName.ToLower().Contains(SearchText);
}

void SLayerStructureTree::SetSearchTextForTests(const FString& InSearchText)
{
	SearchText = InSearchText.TrimStartAndEnd().ToLower();
	Refresh();
}

int32 SLayerStructureTree::GetMatchingLayerCountForTests() const
{
	return MatchingLayerCount;
}

void SLayerStructureTree::RevealLayer(const FGuid& LayerId)
{
	if (!LayerId.IsValid() || !TreeView.IsValid())
	{
		return;
	}
	// Selecting art the search is hiding would otherwise select a row that is not on screen. Clear
	// the filter first so the reveal always lands somewhere the user can see.
	if (!SearchText.IsEmpty() && !Roots.ContainsByPredicate([&LayerId](const TSharedPtr<FLayerStructureNode>& Root)
	{
		return Root.IsValid() && Root->Children.ContainsByPredicate(
			[&LayerId](const TSharedPtr<FLayerStructureNode>& Child)
			{
				return Child.IsValid() && Child->LayerId == LayerId;
			});
	}))
	{
		SearchText.Reset();
		Refresh();
	}

	if (Model.IsValid())
	{
		Model->SetSelectedLayerById(LayerId);
	}

	for (const TSharedPtr<FLayerStructureNode>& Root : Roots)
	{
		if (!Root.IsValid()) continue;
		for (const TSharedPtr<FLayerStructureNode>& Child : Root->Children)
		{
			if (!Child.IsValid() || Child->LayerId != LayerId) continue;
			// Expand the owning section BEFORE scrolling, or the row has no geometry to scroll to.
			TreeView->SetItemExpansion(Root, true);
			TreeView->RequestScrollIntoView(Child);
			return;
		}
	}
}

bool SLayerStructureTree::SelectLayerForTests(const FGuid& LayerId)
{
	if (!Model.IsValid()) return false;
	Model->SetSelectedLayerById(LayerId);
	return Model->GetSelectedLayerId() == LayerId;
}

TSharedRef<ITableRow> SLayerStructureTree::GenerateRow(
	TSharedPtr<FLayerStructureNode> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const bool bGroup = Item.IsValid() && Item->Kind == ELayerStructureNodeKind::Group;
	const FGuid GroupId = Item.IsValid() ? Item->GroupId : FGuid();
	const FGuid LayerId = Item.IsValid() ? Item->LayerId : FGuid();
	const FText Label = Item.IsValid() ? Item->Label : FText::GetEmpty();
	const TWeakPtr<SLayerStructureTree> WeakTree = SharedThis(this);

	TSharedRef<SWidget> LabelWidget = SNew(STextBlock).Text(Label);
	if (bGroup && GroupId.IsValid())
	{
		TSharedRef<SInlineEditableTextBlock> EditableName = SNew(SInlineEditableTextBlock)
			.Text(Label)
			.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			.ToolTipText(LOCTEXT("RenameGroupTip", "Double-click or press F2 to rename this Section."))
			.OnTextCommitted(this, &SLayerStructureTree::CommitGroupName, GroupId);
		GroupNameWidgets.Add(GroupId, EditableName);
		LabelWidget = EditableName;
	}
	else if (bGroup)
	{
		LabelWidget = SNew(STextBlock)
			.Text(Label)
			.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"));
	}

	// U+22EF (midline ellipsis) is not in the editor's default font and rendered as tofu; U+2026 is.
	// HasDownArrow(false) because the glyph already says "menu" - the combo's own arrow doubled it.
	TSharedRef<SWidget> ActionWidget = SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.HasDownArrow(false)
		.ContentPadding(FMargin(4.0f, 0.0f))
		.ButtonContent()
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("…")))
		]
		.ToolTipText(LOCTEXT("LayerActionsTip", "Move, reorder, or delete this layer."))
		.AccessibleText(LOCTEXT("LayerActionsAccessible", "Layer actions"))
		.OnGetMenuContent_Lambda([WeakTree, LayerId]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
			return Pinned.IsValid()
				? Pinned->BuildMoveMenu(LayerId)
				: StaticCastSharedRef<SWidget>(SNew(STextBlock));
		});
	if (bGroup)
	{
		ActionWidget = SNew(SComboButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.HasDownArrow(false)
			.ContentPadding(FMargin(4.0f, 0.0f))
			.ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("…")))]
			.ToolTipText(GroupId.IsValid()
				? LOCTEXT("GroupActionsTip", "Rename or delete this Section. Deleting it keeps every layer and returns them to Ungrouped.")
				: LOCTEXT("UngroupedTip", "Ungrouped is derived from layers with no Section, so it cannot be renamed or deleted."))
			.AccessibleText(LOCTEXT("GroupActionsAccessible", "Section actions"))
			.IsEnabled(GroupId.IsValid())
			.OnGetMenuContent_Lambda([WeakTree, GroupId]() -> TSharedRef<SWidget>
			{
				const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
				return Pinned.IsValid()
					? Pinned->BuildGroupMenu(GroupId)
					: StaticCastSharedRef<SWidget>(SNew(STextBlock));
			});
	}

	// The one thing that made this list unscannable: section headers and layer rows were the same
	// shape. A section now sits on its own band with a member count; a layer leads with its global
	// position. At narrow widths the ORDER NUMBER is the first thing dropped, never the controls.
	const bool bUngrouped = bGroup && !GroupId.IsValid();
	const int32 TotalChildren = Item.IsValid() ? Item->TotalChildCount : 0;
	const int32 ShownChildren = Item.IsValid() ? Item->Children.Num() : 0;

	TSharedRef<SWidget> LeadWidget = SNullWidget::NullWidget;
	if (!bGroup)
	{
		const int32 GlobalOrder = Item.IsValid() ? Item->GlobalOrder : INDEX_NONE;
		LeadWidget = SNew(SBox)
			.WidthOverride(22.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Visibility_Lambda([this]()
			{
				return CachedDockWidth >= 170.0f ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(STextBlock)
				.Text(GlobalOrder == INDEX_NONE
					? FText::GetEmpty()
					: FText::AsNumber(GlobalOrder + 1))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.ToolTipText(LOCTEXT("GlobalOrderTip", "Position in the asset's global layer order \x2014 the one order used for painting, gameplay and publishing. Sections do not change it, so moving a layer past a member of another section changes this number without moving the row."))
			];
	}

	TSharedRef<SWidget> CountWidget = SNullWidget::NullWidget;
	if (bGroup)
	{
		CountWidget = SNew(STextBlock)
			.Visibility_Lambda([this]()
			{
				return CachedDockWidth >= 170.0f ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.ColorAndOpacity(bUngrouped && TotalChildren > 0
				? FSlateColor(FLinearColor(0.85f, 0.65f, 0.30f))
				: FSlateColor::UseSubduedForeground())
			.Text_Lambda([this, TotalChildren, ShownChildren, bUngrouped]()
			{
				if (!SearchText.IsEmpty())
				{
					return FText::Format(LOCTEXT("SectionMatchCount", "{0} matches"), FText::AsNumber(ShownChildren));
				}
				// Ungrouped is where un-organised layers pile up after an import; say so instead of
				// showing a number that reads like every other section.
				return bUngrouped
					? FText::Format(LOCTEXT("UngroupedCount", "{0} to sort"), FText::AsNumber(TotalChildren))
					: FText::AsNumber(TotalChildren);
			});
	}

	return SNew(STableRow<TSharedPtr<FLayerStructureNode>>, OwnerTable)
	.Padding(bGroup ? FMargin(0.0f, 1.0f) : FMargin(2.0f, 0.0f))
	// Drag a layer row onto a Section header to change its Section, or into the gap between two rows to
	// place it precisely in the global order. STableRow owns the insertion marker, so the marker and the
	// commit both come from ONE plan and cannot disagree.
	.OnDragDetected_Lambda([WeakTree, bGroup, LayerId, Label](const FGeometry&, const FPointerEvent&) -> FReply
	{
		const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
		if (bGroup || !LayerId.IsValid() || !Pinned.IsValid()) return FReply::Unhandled();
		UPaper2DPlusCharacterLayerAsset* Asset = Pinned->LayerAsset.Get();
		if (!Asset) return FReply::Unhandled();
		return FReply::Handled().BeginDragDrop(FLayerStructureRowDragDropOp::New(LayerId, Label, Asset));
	})
	.OnCanAcceptDrop_Lambda([WeakTree](
		const FDragDropEvent& DragDropEvent,
		EItemDropZone DropZone,
		TSharedPtr<FLayerStructureNode> TargetItem) -> TOptional<EItemDropZone>
	{
		const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
		if (!Pinned.IsValid() || !TargetItem.IsValid()) return TOptional<EItemDropZone>();
		const bool bTargetIsGroup = TargetItem->Kind == ELayerStructureNodeKind::Group;
		const ELayerStructureDropZone Zone = bTargetIsGroup
			? ELayerStructureDropZone::Onto
			: (DropZone == EItemDropZone::BelowItem
				? ELayerStructureDropZone::Below
				: ELayerStructureDropZone::Above);
		if (!Pinned->PlanDropOnNode(DragDropEvent, TargetItem, Zone).bValid)
		{
			return TOptional<EItemDropZone>();
		}
		return bTargetIsGroup
			? EItemDropZone::OntoItem
			: (Zone == ELayerStructureDropZone::Below ? EItemDropZone::BelowItem : EItemDropZone::AboveItem);
	})
	.OnAcceptDrop_Lambda([WeakTree](
		const FDragDropEvent& DragDropEvent,
		EItemDropZone DropZone,
		TSharedPtr<FLayerStructureNode> TargetItem) -> FReply
	{
		const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
		if (!Pinned.IsValid() || !TargetItem.IsValid()) return FReply::Unhandled();
		UPaper2DPlusCharacterLayerAsset* Asset = Pinned->LayerAsset.Get();
		const TSharedPtr<FLayerStructureRowDragDropOp> Op =
			DragDropEvent.GetOperationAs<FLayerStructureRowDragDropOp>();
		if (!Asset || !Pinned->Model.IsValid() || !Op.IsValid()) return FReply::Unhandled();
		const ELayerStructureDropZone Zone = TargetItem->Kind == ELayerStructureNodeKind::Group
			? ELayerStructureDropZone::Onto
			: (DropZone == EItemDropZone::BelowItem
				? ELayerStructureDropZone::Below
				: ELayerStructureDropZone::Above);
		if (!FLayerStructureController::ApplyDropPlan(
			*Asset, *Pinned->Model, Op->LayerId, Pinned->PlanDropOnNode(DragDropEvent, TargetItem, Zone)))
		{
			return FReply::Unhandled();
		}
		// A drag that scrolls its own subject out of view is how a designer loses the thing they moved.
		Pinned->RevealLayer(Op->LayerId);
		return FReply::Handled();
	})
	[
		SNew(SBorder)
		.BorderImage(bGroup
			? FAppStyle::GetBrush("ToolPanel.GroupBorder")
			: FAppStyle::GetBrush("NoBorder"))
		.Padding(bGroup ? FMargin(3.0f, 2.0f) : FMargin(0.0f))
		[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			LeadWidget
		]
		// The name is the highest-priority element in the row and was previously the ONLY one that
		// could shrink, so at narrow widths it collapsed to nothing and left a column of bare numbers.
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(bGroup ? 0.0f : 4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox).MinDesiredWidth(48.0f)
			[
				LabelWidget
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f)
		[
			CountWidget
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)
		[
			SNew(SCheckBox)
			.ToolTipText(bGroup
				? LOCTEXT("GroupPreviewTip", "Preview only: show or hide every layer in this Section without changing the asset.")
				: LOCTEXT("LayerPreviewTip", "Preview only: show or hide this layer without changing the asset."))
			.AccessibleText(bGroup ? LOCTEXT("GroupPreviewAccessible", "Section Preview visibility")
				: LOCTEXT("LayerPreviewAccessible", "Layer Preview visibility"))
			.IsChecked_Lambda([this, Item, bGroup]()
			{
				UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
				if (!Asset || !Model.IsValid()) return ECheckBoxState::Unchecked;
				if (bGroup) return FLayerStructureController::GetGroupPreviewState(*Asset, *Model, Item->GroupId);
				const FCharacterLayer* Layer = Paper2DPlusLayerStructurePrivate::FindLayer(*Asset, Item->LayerId);
				return Layer && Model->IsLayerVisible(Layer->LayerName)
					? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Item, bGroup](ECheckBoxState State)
			{
				UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
				if (!Asset || !Model.IsValid()) return;
				const bool bVisible = State == ECheckBoxState::Checked;
				if (bGroup) FLayerStructureController::SetGroupPreview(*Asset, *Model, Item->GroupId, bVisible);
				else FLayerStructureController::SetLayerPreview(*Asset, *Model, Item->LayerId, bVisible);
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.0f, 0.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return CachedDockWidth >= 130.0f ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				ActionWidget
			]
		]
		]
	];
}

FLayerStructureDropPlan SLayerStructureTree::PlanDropOnNode(
	const FDragDropEvent& DragDropEvent,
	TSharedPtr<FLayerStructureNode> TargetItem,
	ELayerStructureDropZone Zone) const
{
	FLayerStructureDropPlan Plan;
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	const TSharedPtr<FLayerStructureRowDragDropOp> Op =
		DragDropEvent.GetOperationAs<FLayerStructureRowDragDropOp>();
	if (!Asset || !TargetItem.IsValid() || !Op.IsValid() || Op->SourceAsset.Get() != Asset)
	{
		return Plan;
	}
	const bool bTargetIsGroup = TargetItem->Kind == ELayerStructureNodeKind::Group;
	return FLayerStructureController::PlanDrop(
		*Asset,
		Op->LayerId,
		TargetItem->Kind,
		bTargetIsGroup ? TargetItem->GroupId : TargetItem->LayerId,
		Zone);
}

void SLayerStructureTree::HandleGetChildren(
	TSharedPtr<FLayerStructureNode> Item,
	TArray<TSharedPtr<FLayerStructureNode>>& OutChildren) const
{
	if (Item.IsValid()) OutChildren.Append(Item->Children);
}

void SLayerStructureTree::HandleSelectionChanged(
	TSharedPtr<FLayerStructureNode> Item,
	ESelectInfo::Type SelectInfo)
{
	if (bSyncingSelection || !Item.IsValid() || Item->Kind != ELayerStructureNodeKind::Layer || !Model.IsValid()) return;
	Model->SetSelectedLayerById(Item->LayerId);
}

void SLayerStructureTree::HandleModelLayerSelectionChanged(int32 NewIndex)
{
	Refresh();
	// Selecting art on the canvas must land somewhere the designer can see it: a layer inside a
	// collapsed section, or scrolled out of view, is otherwise selected invisibly. Refresh() has
	// already re-selected the row, so this only expands and scrolls.
	if (!bSyncingSelection && Model.IsValid() && TreeView.IsValid())
	{
		const FGuid SelectedId = Model->GetSelectedLayerId();
		for (const TSharedPtr<FLayerStructureNode>& Root : Roots)
		{
			if (!Root.IsValid()) continue;
			for (const TSharedPtr<FLayerStructureNode>& Child : Root->Children)
			{
				if (!Child.IsValid() || !SelectedId.IsValid() || Child->LayerId != SelectedId) continue;
				TreeView->SetItemExpansion(Root, true);
				TreeView->RequestScrollIntoView(Child);
				return;
			}
		}
	}
}

void SLayerStructureTree::HandleExternalChange()
{
	Refresh();
}

TSharedRef<SWidget> SLayerStructureTree::BuildMoveMenu(const FGuid& LayerId)
{
	const TWeakPtr<SLayerStructureTree> WeakTree = SharedThis(this);
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("MoveUp", "Move Up"), LOCTEXT("MoveUpTip", "Move one step earlier in global order."),
		FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin()) Pinned->MoveLayerClicked(LayerId, -1);
		}), FCanExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
			const UPaper2DPlusCharacterLayerAsset* Asset = Pinned.IsValid() ? Pinned->LayerAsset.Get() : nullptr;
			return Asset && Asset->Layers.IndexOfByPredicate([&LayerId](const FCharacterLayer& Layer) { return Layer.LayerId == LayerId; }) > 0;
		})));
	Menu.AddMenuEntry(LOCTEXT("MoveDown", "Move Down"), LOCTEXT("MoveDownTip", "Move one step later in global order."),
		FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin()) Pinned->MoveLayerClicked(LayerId, 1);
		}), FCanExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
			const UPaper2DPlusCharacterLayerAsset* Asset = Pinned.IsValid() ? Pinned->LayerAsset.Get() : nullptr;
			if (!Asset) return false;
			const int32 Index = Asset->Layers.IndexOfByPredicate([&LayerId](const FCharacterLayer& Layer) { return Layer.LayerId == LayerId; });
			return Index != INDEX_NONE && Index + 1 < Asset->Layers.Num();
		})));
	Menu.AddMenuSeparator();
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	if (Asset)
	{
		Menu.AddMenuEntry(LOCTEXT("MoveUngrouped", "Move to Ungrouped"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId]()
			{
				const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
				if (!Pinned.IsValid()) return;
				if (UPaper2DPlusCharacterLayerAsset* Current = Pinned->LayerAsset.Get(); Current && Pinned->Model.IsValid())
					FLayerStructureController::MoveLayerToGroup(*Current, *Pinned->Model, LayerId, FGuid());
				Pinned->Refresh();
			})));
		for (const FCharacterLayerGroupInfo& Group : Asset->LayerGroups)
		{
			Menu.AddMenuEntry(Group.DisplayName, FText::GetEmpty(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId, GroupId = Group.GroupId]()
				{
					const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin();
					if (!Pinned.IsValid()) return;
					if (UPaper2DPlusCharacterLayerAsset* Current = Pinned->LayerAsset.Get(); Current && Pinned->Model.IsValid())
						FLayerStructureController::MoveLayerToGroup(*Current, *Pinned->Model, LayerId, GroupId);
					Pinned->Refresh();
				})));
		}
	}
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(LOCTEXT("DeleteLayer", "Delete Layer…"),
		LOCTEXT("DeleteLayerTip", "Delete this layer and prune its stable ID from every appearance preset."),
		FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakTree, LayerId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin()) Pinned->DeleteLayerClicked(LayerId);
		})));
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SLayerStructureTree::BuildGroupMenu(const FGuid& GroupId)
{
	const TWeakPtr<SLayerStructureTree> WeakTree = SharedThis(this);
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(
		LOCTEXT("RenameGroup", "Rename Group"),
		LOCTEXT("RenameGroupMenuTip", "Edit this organization group's display name."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakTree, GroupId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin())
			{
				Pinned->BeginGroupRename(GroupId);
			}
		})));
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(
		LOCTEXT("DeleteGroup", "Delete Group…"),
		LOCTEXT("DeleteGroupTip", "Delete this group and move its layers to Ungrouped."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakTree, GroupId]()
		{
			if (const TSharedPtr<SLayerStructureTree> Pinned = WeakTree.Pin())
			{
				Pinned->DeleteGroupClicked(GroupId);
			}
		})));
	return Menu.MakeWidget();
}

FReply SLayerStructureTree::AddGroupClicked()
{
	FGuid NewGroupId;
	if (UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get(); Asset && Model.IsValid())
		NewGroupId = FLayerStructureController::AddGroup(*Asset, *Model);
	// A search filter would hide the section that was just created.
	SearchText.Reset();
	Refresh();
	// Land straight in inline rename: an unnamed "New Section" the designer has to hunt down and
	// rename separately is the thing that makes people stop using sections at all.
	if (NewGroupId.IsValid())
		BeginGroupRename(NewGroupId);
	return FReply::Handled();
}

FReply SLayerStructureTree::DeleteGroupClicked(FGuid GroupId)
{
	if (UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get(); Asset && Model.IsValid())
		FLayerStructureController::DeleteGroup(*Asset, *Model, GroupId);
	Refresh();
	return FReply::Handled();
}

FReply SLayerStructureTree::DeleteLayerClicked(FGuid LayerId)
{
	UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get();
	const FCharacterLayer* Layer = Asset ? Paper2DPlusLayerStructurePrivate::FindLayer(*Asset, LayerId) : nullptr;
	if (!Layer || !Model.IsValid()) return FReply::Handled();
	const FText Message = FText::Format(
		LOCTEXT("ConfirmDeleteLayer", "Delete layer “{0}”?\n\nIts art mappings, local gameplay, offsets, and every appearance-preset reference will be removed. This is undoable."),
		FText::FromString(Layer->LayerName));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes)
		FLayerStructureController::DeleteLayer(*Asset, *Model, LayerId);
	Refresh();
	return FReply::Handled();
}

FReply SLayerStructureTree::MoveLayerClicked(FGuid LayerId, int32 Delta)
{
	if (UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get(); Asset && Model.IsValid())
		FLayerStructureController::MoveLayerByDelta(*Asset, *Model, LayerId, Delta);
	Refresh();
	return FReply::Handled();
}

void SLayerStructureTree::BeginGroupRename(FGuid GroupId)
{
	PendingRenameGroupId = GroupId;
	if (RenameTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(RenameTimerHandle.Pin().ToSharedRef());
	}
	RenameTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this,
			&SLayerStructureTree::HandleDeferredGroupRename));
}

EActiveTimerReturnType SLayerStructureTree::HandleDeferredGroupRename(double, float)
{
	RenameTimerHandle.Reset();
	if (TWeakPtr<SInlineEditableTextBlock>* Found = GroupNameWidgets.Find(PendingRenameGroupId))
	{
		if (const TSharedPtr<SInlineEditableTextBlock> NameWidget = Found->Pin())
		{
			NameWidget->EnterEditingMode();
		}
	}
	PendingRenameGroupId.Invalidate();
	return EActiveTimerReturnType::Stop;
}

void SLayerStructureTree::CommitGroupName(
	const FText& Text,
	ETextCommit::Type CommitType,
	FGuid GroupId)
{
	if (CommitType == ETextCommit::OnCleared) return;
	if (UPaper2DPlusCharacterLayerAsset* Asset = LayerAsset.Get(); Asset && Model.IsValid())
		FLayerStructureController::RenameGroup(*Asset, *Model, GroupId, Text);
	Refresh();
}

#undef LOCTEXT_NAMESPACE
