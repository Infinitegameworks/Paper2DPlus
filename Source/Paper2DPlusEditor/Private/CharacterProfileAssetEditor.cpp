// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"
#include "CharacterProfileEditorModel.h"
#include "AnimationsPanel.h"
#include "Styling/AppStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// ==========================================
// Per-asset last-selection persistence
// ==========================================
namespace CharacterProfileEditorPersistence
{
	static const TCHAR* SectionName = TEXT("Paper2DPlus.CharacterProfileEditor.AssetState");

	struct FState
	{
		FString FlipbookName;
		int32 FrameIndex = 0;
	};

	static FString MakeKey(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		if (!Asset || !Asset->GetPackage()) return FString();
		return Asset->GetPackage()->GetName();
	}

	static void Save(const UPaper2DPlusCharacterProfileAsset* Asset, const FState& State)
	{
		const FString Key = MakeKey(Asset);
		if (Key.IsEmpty()) return;
		const FString Value = FString::Printf(TEXT("Flipbook=%s;Frame=%d"),
			*State.FlipbookName, State.FrameIndex);
		GConfig->SetString(SectionName, *Key, *Value, GEditorPerProjectIni);
		// No explicit Flush (legacy-cleanup 2026-07): GConfig holds the value; the engine flushes
		// GEditorPerProjectIni itself. A manual Flush rewrites the whole multi-hundred-KB ini via
		// tmp+MoveFile and stalled ~8s per call under file-lock contention - the slow-close bug.
	}

	static FState Load(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		FState State;
		const FString Key = MakeKey(Asset);
		if (Key.IsEmpty()) return State;

		FString Raw;
		if (!GConfig->GetString(SectionName, *Key, Raw, GEditorPerProjectIni)) return State;

		TArray<FString> Pairs;
		Raw.ParseIntoArray(Pairs, TEXT(";"));
		for (const FString& Pair : Pairs)
		{
			FString K, V;
			if (!Pair.Split(TEXT("="), &K, &V)) continue;
			if      (K == TEXT("Flipbook")) State.FlipbookName = V;
			else if (K == TEXT("Frame"))    State.FrameIndex = FCString::Atoi(*V);
		}
		return State;
	}
}

// ==========================================
// Console commands — use GActiveEditorModel
// ==========================================

// TASK-96: the Overview + Animation Map tabs merged into one "Animations" tab — all the old aliases
// (plus list/map/animations) retarget to it. View-mode-via-alias is the in-tab [List|Map] toggle
// flip handled below; the aliases open the tab.
//
// MUST be a function-local static (built on first call), NOT a namespace-scope static initialized from
// FCharacterProfileAssetEditorToolkit::*TabId. Those tab-id FNames are static `const FName`s defined in
// CharacterProfileAssetEditorToolkit.cpp (a DIFFERENT translation unit); cross-TU static init order is
// unspecified, so a namespace-scope map here can capture them while they're still None — which silently
// breaks EVERY alias (SwitchTab resolves to a None tab id → "no spawner registered for 'None'"). A
// function-local static is constructed lazily at first console-command use, long after all static init
// completes, so the FNames are guaranteed populated.
static const TMap<FString, FName>& GetTabNameToId()
{
	static const TMap<FString, FName> TabNameToId = {
		{ TEXT("overview"),       FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("browser"),        FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("list"),           FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("grid"),           FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("animations"),     FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("flipbooks"),      FCharacterProfileAssetEditorToolkit::FlipbookListTabId },
		{ TEXT("navigator"),      FCharacterProfileAssetEditorToolkit::FlipbookListTabId },
		{ TEXT("details"),        FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("hitbox"),         FCharacterProfileAssetEditorToolkit::HitboxEditorTabId },
		{ TEXT("hitboxes"),       FCharacterProfileAssetEditorToolkit::HitboxEditorTabId },
		{ TEXT("sprite"),         FCharacterProfileAssetEditorToolkit::SpriteEditorTabId },
		{ TEXT("spriteeditor"),   FCharacterProfileAssetEditorToolkit::SpriteEditorTabId },
		{ TEXT("alignment"),      FCharacterProfileAssetEditorToolkit::SpriteEditorTabId },
		{ TEXT("frametiming"),    FCharacterProfileAssetEditorToolkit::FrameTimingTabId },
		{ TEXT("timing"),         FCharacterProfileAssetEditorToolkit::FrameTimingTabId },
		{ TEXT("frameevents"),    FCharacterProfileAssetEditorToolkit::FrameEventsTabId },
		{ TEXT("events"),         FCharacterProfileAssetEditorToolkit::FrameEventsTabId },
		{ TEXT("rootmotion"),     FCharacterProfileAssetEditorToolkit::RootMotionTabId },
		{ TEXT("motion"),         FCharacterProfileAssetEditorToolkit::RootMotionTabId },
		// Playback Queue moved out of the Navigator's collapsed expander into its own docked tab
		// beside Completion and Related Profiles (layout _v14).
		{ TEXT("queue"),          FCharacterProfileAssetEditorToolkit::PlaybackQueueTabId },
		{ TEXT("playbackqueue"),  FCharacterProfileAssetEditorToolkit::PlaybackQueueTabId },
		// The Curves tab retired (curves rework PR C) — curve authoring lives in the Frame Events tab's
		// per-curve track stack, so the long-standing aliases retarget there instead of breaking scripts.
		{ TEXT("curves"),         FCharacterProfileAssetEditorToolkit::FrameEventsTabId },
		{ TEXT("curve"),          FCharacterProfileAssetEditorToolkit::FrameEventsTabId },
		// Animation Map (formerly "Combo Graph") — the aliases open the merged Animations tab.
		{ TEXT("animationmap"),   FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("animmap"),        FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("combograph"),     FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("graph"),          FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("combo"),          FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("map"),            FCharacterProfileAssetEditorToolkit::AnimationsTabId },
		{ TEXT("completion"),     FCharacterProfileAssetEditorToolkit::CompletionTabId },
		{ TEXT("toolpanels"),     FCharacterProfileAssetEditorToolkit::ContextHostTabId },
		{ TEXT("context"),        FCharacterProfileAssetEditorToolkit::ContextHostTabId },
	};
	return TabNameToId;
}

static FAutoConsoleCommand GSwitchTabCommand(
	TEXT("Paper2DPlus.SwitchTab"),
	TEXT("Switch the Character Profile Editor to a tab by name (overview, flipbooks, hitbox, sprite, timing, cues/events, motion, animationmap; 'grid'/'list'/'map' also flip the merged Animations tab to that view; 'framedata' opens the floating Frame Data window; 'curves' aliases the Frame Cues tab, where the curve tracks live)"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.SwitchTab: requires a tab name"));
			return;
		}
		TSharedPtr<FCharacterProfileEditorModel> Model = FCharacterProfileEditorModel::GActiveEditorModel.Pin();
		if (!Model.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.SwitchTab: no active Character Profile Editor"));
			return;
		}
		const FString TabName = Args[0].ToLower();
		// The Frame Data tab retired (layout _v8) — the read-out now lives in a floating window opened
		// from the Flipbooks sidebar, so the long-standing aliases open that window instead of breaking.
		if (TabName == TEXT("framedata") || TabName == TEXT("data"))
		{
			Model->OpenFrameDataWindow();
			return;
		}
		if (const FName* TabId = GetTabNameToId().Find(TabName))
		{
			Model->BringTabToFront(*TabId);

			// 'list'/'map' also flip the merged Animations tab's left view (P5). BringTabToFront first
			// realizes/foregrounds the tab; SAnimationsPanel is a permanent layout tab so its widget (and
			// GActiveAnimationsPanel) already exists. The other Animations aliases just foreground the tab.
			if (TabName == TEXT("grid") || TabName == TEXT("list") || TabName == TEXT("map"))
			{
				if (TSharedPtr<SAnimationsPanel> Panel = SAnimationsPanel::GActiveAnimationsPanel.Pin())
				{
					const EAnimationsViewMode Mode =
						(TabName == TEXT("map"))  ? EAnimationsViewMode::Map  :
						(TabName == TEXT("grid")) ? EAnimationsViewMode::Grid : EAnimationsViewMode::List;
					Panel->SetViewMode(Mode);
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.SwitchTab: unknown tab '%s'."), *Args[0]);
		}
	})
);

static FAutoConsoleCommand GGetTabCommand(
	TEXT("Paper2DPlus.GetTab"),
	TEXT("Print the active editor model state"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		TSharedPtr<FCharacterProfileEditorModel> Model = FCharacterProfileEditorModel::GActiveEditorModel.Pin();
		if (!Model.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.GetTab: no active Character Profile Editor"));
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("Paper2DPlus.GetTab: Flipbook=%d Frame=%d"),
			Model->GetSelectedFlipbookIndex(), Model->GetSelectedFrameIndex());
	})
);

static FAutoConsoleCommand GWorkspaceProbeCommand(
	TEXT("Paper2DPlus.WorkspaceProbe"),
	TEXT("Read-only Character Profile workspace probe: active tool, contextual tabs, selection, Navigator, and Completion."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		const TSharedPtr<FCharacterProfileAssetEditorToolkit> Toolkit =
			FCharacterProfileAssetEditorToolkit::GActiveCharacterProfileToolkit.Pin();
		if (!Toolkit.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.WorkspaceProbe: no active Character Profile Editor"));
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("Paper2DPlus.WorkspaceProbe: %s"), *Toolkit->BuildWorkspaceProbeString());
	})
);

static FAutoConsoleCommand GReopenEditorCommand(
	TEXT("Paper2DPlus.ReopenEditor"),
	TEXT("Close and reopen the active Character Profile Editor (preserves asset selection)"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UAssetEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Subsystem) return;

		UObject* FoundAsset = nullptr;
		for (UObject* EditedAsset : Subsystem->GetAllEditedAssets())
		{
			if (EditedAsset && EditedAsset->IsA<UPaper2DPlusCharacterProfileAsset>())
			{
				FoundAsset = EditedAsset;
				break;
			}
		}
		if (!FoundAsset)
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.ReopenEditor: no open Character Profile Editor found"));
			return;
		}
		Subsystem->CloseAllEditorsForAsset(FoundAsset);
		Subsystem->OpenEditorForAsset(FoundAsset);
	})
);

static FAutoConsoleCommand GSelectFlipbookCommand(
	TEXT("Paper2DPlus.SelectFlipbook"),
	TEXT("Select a flipbook by index (0-based) in the active Character Profile Editor"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 0) return;
		TSharedPtr<FCharacterProfileEditorModel> Model = FCharacterProfileEditorModel::GActiveEditorModel.Pin();
		if (!Model.IsValid()) return;
		int32 Index = FCString::Atoi(*Args[0]);
		Model->SetSelectedFlipbook(Index);
	})
);

// ==========================================
// Drag-drop operations (shared across panels)
// ==========================================

TSharedRef<FGroupDragDropOp> FGroupDragDropOp::New(FName GroupName)
{
	TSharedRef<FGroupDragDropOp> Op = MakeShareable(new FGroupDragDropOp());
	Op->SourceGroupName = GroupName;
	Op->DefaultHoverText = FText::Format(LOCTEXT("DragGroup", "Move group: {0}"), FText::FromName(GroupName));
	Op->Construct();
	return Op;
}

TSharedPtr<SWidget> FGroupDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(6, 2))
		[
			SNew(STextBlock).Text(DefaultHoverText)
		];
}

TSharedRef<FFlipbookGroupDragDropOp> FFlipbookGroupDragDropOp::NewFromCardDrag(const TArray<int32>& InFlipbookIndices, FName FromGroup,
	UPaper2DPlusCharacterProfileAsset* InSourceAsset)
{
	TSharedRef<FFlipbookGroupDragDropOp> Op = MakeShareable(new FFlipbookGroupDragDropOp());
	Op->FlipbookIndices = InFlipbookIndices;
	Op->SourceAsset = InSourceAsset;

	if (InFlipbookIndices.Num() == 1)
	{
		Op->DefaultHoverText = FText::Format(LOCTEXT("DragSingle", "1 flipbook"), FText());
	}
	else if (FromGroup != NAME_None)
	{
		Op->DefaultHoverText = FText::Format(LOCTEXT("DragMultiFrom", "{0} flipbooks from {1}"),
			FText::AsNumber(InFlipbookIndices.Num()), FText::FromName(FromGroup));
	}
	else
	{
		Op->DefaultHoverText = FText::Format(LOCTEXT("DragMulti", "{0} flipbooks"),
			FText::AsNumber(InFlipbookIndices.Num()));
	}

	Op->Construct();
	return Op;
}

TSharedPtr<SWidget> FFlipbookGroupDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(6, 2))
		[
			SNew(STextBlock).Text(DefaultHoverText)
		];
}

#undef LOCTEXT_NAMESPACE
