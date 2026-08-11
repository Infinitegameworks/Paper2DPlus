// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationMap/Paper2DPlusAnimationMapNode_Move.h"
#include "AnimationTagChipUtils.h" // SMenuHostedTagPickerGuard - menu-hosted pickers are selection-only

#include "AnimationMap/Paper2DPlusAnimationMap.h"
#include "AnimationMap/SAnimationMapMoveNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/EngineVersionComparison.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
// SGameplayTagPicker is not public before 5.3 — the "Change Group…" entry degrades away on older
// engines (group reassignment stays available via the browser card's Tag chip / details pane).
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagPicker.h"
#endif

#define LOCTEXT_NAMESPACE "Paper2DPlusAnimationMapNode_Move"

void UPaper2DPlusAnimationMapNode_Move::AllocateDefaultPins()
{
	// Clone of UAnimStateNode::AllocateDefaultPins (AnimStateNode.cpp:32-36): one visible input + one
	// visible output pin in a shared "Transition" category. STUBS allocate the input pin ONLY — a stub
	// cannot own rows (R3), so it must have no output pin to drag a wire from; GetOutputPin stays
	// null-safe for that case. bIsStub is set before Finalize() by the spawn funnel, so this branch
	// sees the final identity.
	CreatePin(EGPD_Input, TEXT("Transition"), TEXT("In"));
	if (!bIsStub)
	{
		CreatePin(EGPD_Output, TEXT("Transition"), TEXT("Out"));
	}
}

FText UPaper2DPlusAnimationMapNode_Move::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	// Stubs carry an explicit marker so the dangling state reads even without color (accessibility).
	if (bIsStub)
	{
		return FText::Format(LOCTEXT("StubMoveNodeTitle", "{0} (missing)"), FText::FromString(MoveName));
	}
	return FText::FromString(MoveName);
}

FLinearColor UPaper2DPlusAnimationMapNode_Move::GetNodeTitleColor() const
{
	// THE single source of the title-plate tint: SAnimationMapMoveNode::GetSpillColor binds this onto
	// the node's ColorSpill content plate (U6 full-body-pin shape). Stub = warning orange (the
	// validator's dangling-target Warning made visible); real = neutral dark (the anim state node's
	// inactive look, not the engine's default blueprint blue).
	if (bIsStub)
	{
		return FLinearColor(0.85f, 0.55f, 0.05f);
	}
	return FLinearColor(0.10f, 0.10f, 0.10f);
}

FLinearColor UPaper2DPlusAnimationMapNode_Move::GetNodeBodyTintColor() const
{
	// Mild warm cast over the whole stub body — visible at any zoom where the title text is not.
	// NOTE: since the U6 full-body-pin shape, SAnimationMapMoveNode owns its body brush and expresses
	// this signal directly in GetBodyBorderColor (base SGraphNode::GetNodeBodyColor no longer runs
	// for move nodes); the override is kept as the documented stub-tint contract + any future
	// base-shaped widget fallback.
	if (bIsStub)
	{
		return FLinearColor(1.0f, 0.82f, 0.45f);
	}
	return Super::GetNodeBodyTintColor();
}

void UPaper2DPlusAnimationMapNode_Move::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);
	if (!Menu)
	{
		return;
	}
	// Stubs own no editable animation row.
	if (bIsStub)
	{
		return;
	}
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	UPaper2DPlusAnimationMap* AnimationMap = Cast<UPaper2DPlusAnimationMap>(GetGraph());
	if (!AnimationMap)
	{
		return;
	}
	const FString MoveNameCapture = MoveName;
	FToolMenuSection& Section = Menu->AddSection(
		TEXT("Paper2DPlusAnimationMap"), LOCTEXT("AnimationMapSectionHeader", "Animation Map"));
	// TASK-108 U6 (R16): "Change Group…" — a Paper2DPlus.Animation tag picker (HybridMode so its
	// add-tag UI can mint a NEW group tag in place) that moves this animation's TagMappings membership.
	// The submenu widget captures the GRAPH weakly + the move NAME by value; the pick DISMISSES the
	// menu and DEFERS the commit one tick so the picker's
	// OnTagChanged fully unwinds before the panel's transaction rebuilds the board/graph (the
	// MakeCardTagChip reentrancy discipline, ue-gameplay-tag-color-registry-patterns.md §8).
	TWeakObjectPtr<UPaper2DPlusAnimationMap> WeakAnimationMap = AnimationMap;
	Section.AddSubMenu(
		TEXT("ChangeAnimationGroup"),
		LOCTEXT("ChangeGroupLabel", "Change Group…"),
		LOCTEXT("ChangeGroupTip", "Move this animation to another Animation Map tag group (pick an existing Paper2DPlus.Animation tag or add a new one). It is removed from its current group and appended at the END of the target group's list, so existing entries' first-match resolution is undisturbed. One undo step."),
		FNewToolMenuDelegate::CreateLambda([WeakAnimationMap, MoveNameCapture](UToolMenu* SubMenu)
		{
			if (!SubMenu)
			{
				return;
			}
			FToolMenuSection& PickerSection = SubMenu->AddSection(TEXT("Paper2DPlusChangeGroupPicker"));
			PickerSection.AddEntry(FToolMenuEntry::InitWidget(
				TEXT("ChangeGroupTagPicker"),
				SNew(SBox)
				.MinDesiredWidth(300.f)
				.Padding(2.f)
				[
					// Menu-hosted picker = selection-only: the guard eats right-clicks so the engine
					// row context menu can't crash the menu stack (SMenuHostedTagPickerGuard doc).
					// New group tags are still mintable via HybridMode's inline add-tag row.
					SNew(SMenuHostedTagPickerGuard)
					[
					SNew(SGameplayTagPicker)
					.Filter(TEXT("Paper2DPlus.Animation"))
					.MultiSelect(false)
					.GameplayTagPickerMode(EGameplayTagPickerMode::HybridMode)
					.TagContainers(TArray<FGameplayTagContainer>{ FGameplayTagContainer() })
					.OnTagChanged_Lambda([WeakAnimationMap, MoveNameCapture](const TArray<FGameplayTagContainer>& Containers)
					{
						FGameplayTag PickedTag;
						for (const FGameplayTagContainer& Container : Containers)
						{
							for (auto It = Container.CreateConstIterator(); It; ++It)
							{
								if (It->IsValid())
								{
									PickedTag = *It;
									break;
								}
							}
							if (PickedTag.IsValid())
							{
								break;
							}
						}
						if (!PickedTag.IsValid())
						{
							return; // an uncheck fires OnTagChanged with an empty container — ignore
						}
						// Close the whole context-menu stack first; the deferred tick then fires the
						// panel handler with nothing of this menu left on the stack.
						FSlateApplication::Get().DismissAllMenus();
						auto FireChangeGroup = [WeakAnimationMap, MoveNameCapture, PickedTag]()
						{
							if (UPaper2DPlusAnimationMap* PinnedMap = WeakAnimationMap.Get())
							{
								PinnedMap->OnChangeGroupRequested.ExecuteIfBound(MoveNameCapture, PickedTag);
							}
						};
						if (GEditor)
						{
							GEditor->GetTimerManager()->SetTimerForNextTick(FireChangeGroup);
						}
						else
						{
							FireChangeGroup();
						}
					})
					]
				],
				FText::GetEmpty(),
				/*bNoIndent=*/true));
		}));

	// Set Phase Tag… and Set Animation Tags… use explicit Apply/Clear buttons. That keeps the
	// multi-select tag picker open for several checks and turns the final exact replacement into one
	// panel transaction (including an intentional empty-container clear).
	FGameplayTagContainer InitialPhaseContainer;
	if (PhaseTag.IsValid())
	{
		InitialPhaseContainer.AddTag(PhaseTag);
	}
	const TSharedRef<FGameplayTagContainer> PendingPhase = MakeShared<FGameplayTagContainer>(InitialPhaseContainer);
	Section.AddSubMenu(
		TEXT("SetAnimationPhase"),
		LOCTEXT("SetPhaseLabel", "Set Phase Tag…"),
		LOCTEXT("SetPhaseTip", "Replace the phase tag on this animation, or on every selected animation when this animation is part of the selection."),
		FNewToolMenuDelegate::CreateLambda([WeakAnimationMap, MoveNameCapture, PendingPhase](UToolMenu* SubMenu)
		{
			if (!SubMenu)
			{
				return;
			}
			FToolMenuSection& PickerSection = SubMenu->AddSection(TEXT("Paper2DPlusSetPhasePicker"));
			PickerSection.AddEntry(FToolMenuEntry::InitWidget(
				TEXT("SetPhaseTagPicker"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MinDesiredWidth(300.f)
					.Padding(2.f)
					[
						SNew(SMenuHostedTagPickerGuard)
						[
							SNew(SGameplayTagPicker)
							.Filter(TEXT("Paper2DPlus.Phase"))
							.MultiSelect(false)
							.GameplayTagPickerMode(EGameplayTagPickerMode::SelectionMode)
							.TagContainers(TArray<FGameplayTagContainer>{ *PendingPhase })
							.OnTagChanged_Lambda([PendingPhase](const TArray<FGameplayTagContainer>& Containers)
							{
								*PendingPhase = Containers.Num() > 0 ? Containers[0] : FGameplayTagContainer();
							})
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(2.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ClearPhaseButton", "Clear"))
						.OnClicked_Lambda([WeakAnimationMap, MoveNameCapture]()
						{
							FSlateApplication::Get().DismissAllMenus();
							auto Fire = [WeakAnimationMap, MoveNameCapture]()
							{
								if (UPaper2DPlusAnimationMap* PinnedMap = WeakAnimationMap.Get())
								{
									PinnedMap->OnSetPhaseRequested.ExecuteIfBound(MoveNameCapture, FGameplayTag());
								}
							};
							if (GEditor) { GEditor->GetTimerManager()->SetTimerForNextTick(Fire); } else { Fire(); }
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("ApplyPhaseButton", "Apply"))
						.OnClicked_Lambda([WeakAnimationMap, MoveNameCapture, PendingPhase]()
						{
							FGameplayTag PickedPhase;
							for (auto It = PendingPhase->CreateConstIterator(); It; ++It)
							{
								PickedPhase = *It;
								break;
							}
							FSlateApplication::Get().DismissAllMenus();
							auto Fire = [WeakAnimationMap, MoveNameCapture, PickedPhase]()
							{
								if (UPaper2DPlusAnimationMap* PinnedMap = WeakAnimationMap.Get())
								{
									PinnedMap->OnSetPhaseRequested.ExecuteIfBound(MoveNameCapture, PickedPhase);
								}
							};
							if (GEditor) { GEditor->GetTimerManager()->SetTimerForNextTick(Fire); } else { Fire(); }
							return FReply::Handled();
						})
					]
				],
				FText::GetEmpty(),
				/*bNoIndent=*/true));
		}));

	const TSharedRef<FGameplayTagContainer> PendingAnimationTags =
		MakeShared<FGameplayTagContainer>(OwnAnimationTags);
	Section.AddSubMenu(
		TEXT("SetAnimationTags"),
		LOCTEXT("SetAnimationTagsLabel", "Set Animation Tags…"),
		LOCTEXT("SetAnimationTagsTip", "Replace the exact Animation Tags container on this animation, or on every selected animation when this animation is part of the selection."),
		FNewToolMenuDelegate::CreateLambda([WeakAnimationMap, MoveNameCapture, PendingAnimationTags](UToolMenu* SubMenu)
		{
			if (!SubMenu)
			{
				return;
			}
			FToolMenuSection& PickerSection = SubMenu->AddSection(TEXT("Paper2DPlusSetAnimationTagsPicker"));
			PickerSection.AddEntry(FToolMenuEntry::InitWidget(
				TEXT("SetAnimationTagsWidget"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MinDesiredWidth(300.f)
					.Padding(2.f)
					[
						SNew(SMenuHostedTagPickerGuard)
						[
							SNew(SGameplayTagPicker)
							.Filter(TEXT("Paper2DPlus.Animation"))
							.MultiSelect(true)
							.GameplayTagPickerMode(EGameplayTagPickerMode::SelectionMode)
							.TagContainers(TArray<FGameplayTagContainer>{ *PendingAnimationTags })
							.OnTagChanged_Lambda([PendingAnimationTags](const TArray<FGameplayTagContainer>& Containers)
							{
								PendingAnimationTags->Reset();
								for (const FGameplayTagContainer& Container : Containers)
								{
									PendingAnimationTags->AppendTags(Container);
								}
							})
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(2.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ClearAnimationTagsButton", "Clear"))
						.OnClicked_Lambda([WeakAnimationMap, MoveNameCapture]()
						{
							FSlateApplication::Get().DismissAllMenus();
							auto Fire = [WeakAnimationMap, MoveNameCapture]()
							{
								if (UPaper2DPlusAnimationMap* PinnedMap = WeakAnimationMap.Get())
								{
									PinnedMap->OnSetAnimationTagsRequested.ExecuteIfBound(
										MoveNameCapture, FGameplayTagContainer());
								}
							};
							if (GEditor) { GEditor->GetTimerManager()->SetTimerForNextTick(Fire); } else { Fire(); }
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("ApplyAnimationTagsButton", "Apply"))
						.OnClicked_Lambda([WeakAnimationMap, MoveNameCapture, PendingAnimationTags]()
						{
							const FGameplayTagContainer Tags = *PendingAnimationTags;
							FSlateApplication::Get().DismissAllMenus();
							auto Fire = [WeakAnimationMap, MoveNameCapture, Tags]()
							{
								if (UPaper2DPlusAnimationMap* PinnedMap = WeakAnimationMap.Get())
								{
									PinnedMap->OnSetAnimationTagsRequested.ExecuteIfBound(MoveNameCapture, Tags);
								}
							};
							if (GEditor) { GEditor->GetTimerManager()->SetTimerForNextTick(Fire); } else { Fire(); }
							return FReply::Handled();
						})
					]
				],
				FText::GetEmpty(),
				/*bNoIndent=*/true));
		}));
#endif // >= 5.3
}

TSharedPtr<SGraphNode> UPaper2DPlusAnimationMapNode_Move::CreateVisualWidget()
{
	return SNew(SAnimationMapMoveNode, this);
}

UEdGraphPin* UPaper2DPlusAnimationMapNode_Move::GetInputPin() const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			return Pin;
		}
	}
	return nullptr;
}

UEdGraphPin* UPaper2DPlusAnimationMapNode_Move::GetOutputPin() const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			return Pin;
		}
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
