// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetEditor.h"

// UE 5.0 compat: FAppStyle doesn't exist, use FEditorStyle
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#define FAppStyle FEditorStyle
#else
#include "Styling/AppStyle.h"
#endif
#include "SpriteExtractionUtils.h"
#include "FrameTimingEditor.h"
#include "FrameEventEditor.h"
#include "RootMotionEditor.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Paper2DPlusSettings.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"

/** SCharacterProfileAssetEditor — Main character profile editor: multi-tab layout, toolbar, persistence, context menus. Child tabs in SCharacterProfileAssetEditor_*.cpp files. */

#define LOCTEXT_NAMESPACE "CharacterProfileAssetEditor"

// ==========================================
// Per-asset last-selection persistence
// ==========================================
// Remembers tab / flipbook / frame across editor close-reopen so long
// authoring sessions don't reset to flipbook 0 every time the user reopens
// an asset. Stored in GEditorPerProjectIni keyed on the asset's package path.
// See docs/future-features.md AI-11 for scope decisions.
namespace CharacterProfileEditorPersistence
{
	static const TCHAR* SectionName = TEXT("Paper2DPlus.CharacterProfileEditor.AssetState");

	struct FState
	{
		FString FlipbookName;   // stored by name so reorder/rename doesn't strand us
		int32 FrameIndex = 0;
		int32 Tab = 0;          // static_cast<int32>(ECharacterProfileTab::Overview)
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
		const FString Value = FString::Printf(TEXT("Flipbook=%s;Frame=%d;Tab=%d"),
			*State.FlipbookName, State.FrameIndex, State.Tab);
		GConfig->SetString(SectionName, *Key, *Value, GEditorPerProjectIni);
		GConfig->Flush(false, GEditorPerProjectIni);
	}

	static FState Load(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		FState State;
		const FString Key = MakeKey(Asset);
		if (Key.IsEmpty()) return State;

		FString Raw;
		if (!GConfig->GetString(SectionName, *Key, Raw, GEditorPerProjectIni)) return State;

		// Parse "Flipbook=<name>;Frame=<idx>;Tab=<int>"
		TArray<FString> Pairs;
		Raw.ParseIntoArray(Pairs, TEXT(";"));
		for (const FString& Pair : Pairs)
		{
			FString K, V;
			if (!Pair.Split(TEXT("="), &K, &V)) continue;
			if      (K == TEXT("Flipbook")) State.FlipbookName = V;
			else if (K == TEXT("Frame"))    State.FrameIndex = FCString::Atoi(*V);
			else if (K == TEXT("Tab"))      State.Tab = FCString::Atoi(*V);
		}
		return State;
	}
}

// Static weak pointer to the active editor instance for console command access
static TWeakPtr<SCharacterProfileAssetEditor> GActiveCharacterProfileEditor;

static const TMap<FString, ECharacterProfileTab> GTabNameMap = {
	{ TEXT("overview"),        ECharacterProfileTab::Overview },
	{ TEXT("hitbox"),          ECharacterProfileTab::Hitboxes },
	{ TEXT("hitboxes"),        ECharacterProfileTab::Hitboxes },
	{ TEXT("sprite"),          ECharacterProfileTab::SpriteEditor },
	{ TEXT("spriteeditor"),    ECharacterProfileTab::SpriteEditor },
	{ TEXT("alignment"),       ECharacterProfileTab::SpriteEditor },
	{ TEXT("frametiming"),     ECharacterProfileTab::FrameTiming },
	{ TEXT("timing"),          ECharacterProfileTab::FrameTiming },
	{ TEXT("frameevents"),     ECharacterProfileTab::FrameEvents },
	{ TEXT("events"),          ECharacterProfileTab::FrameEvents },
	{ TEXT("rootmotion"),      ECharacterProfileTab::RootMotion },
	{ TEXT("motion"),          ECharacterProfileTab::RootMotion },
};

static FAutoConsoleCommand GSwitchTabCommand(
	TEXT("Paper2DPlus.SwitchTab"),
	TEXT("Switch the Character Profile Editor to a tab by name (overview, hitbox, sprite, timing, events, motion)"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.SwitchTab: requires a tab name (overview, hitbox, sprite, timing, events, motion)"));
			return;
		}
		TSharedPtr<SCharacterProfileAssetEditor> Editor = GActiveCharacterProfileEditor.Pin();
		if (!Editor.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.SwitchTab: no active Character Profile Editor"));
			return;
		}
		const FString TabName = Args[0].ToLower();
		if (const ECharacterProfileTab* Tab = GTabNameMap.Find(TabName))
		{
			Editor->SwitchToTab(static_cast<int32>(*Tab));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.SwitchTab: unknown tab '%s'. Use: overview, hitbox, sprite, timing, events, motion"), *Args[0]);
		}
	})
);

static FAutoConsoleCommand GGetTabCommand(
	TEXT("Paper2DPlus.GetTab"),
	TEXT("Print the current active tab name in the Character Profile Editor"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		TSharedPtr<SCharacterProfileAssetEditor> Editor = GActiveCharacterProfileEditor.Pin();
		if (!Editor.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.GetTab: no active Character Profile Editor"));
			return;
		}
		static const TCHAR* TabNames[] = {
			TEXT("Overview"), TEXT("Hitboxes"), TEXT("SpriteEditor"),
			TEXT("FrameTiming"), TEXT("FrameEvents"), TEXT("RootMotion")
		};
		int32 Idx = Editor->GetActiveTabIndex();
		if (Idx >= 0 && Idx < UE_ARRAY_COUNT(TabNames))
		{
			UE_LOG(LogTemp, Display, TEXT("Paper2DPlus.GetTab: %s"), TabNames[Idx]);
		}
	})
);

static FAutoConsoleCommand GReopenEditorCommand(
	TEXT("Paper2DPlus.ReopenEditor"),
	TEXT("Close and reopen the active Character Profile Editor (preserves asset selection)"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UAssetEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Subsystem) return;

		// Find any open CharacterProfile asset
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
		TSharedPtr<SCharacterProfileAssetEditor> Editor = GActiveCharacterProfileEditor.Pin();
		if (!Editor.IsValid()) return;
		int32 Index = FCString::Atoi(*Args[0]);
		Editor->SelectFlipbook(Index);
	})
);

namespace CharacterProfileEditorLayoutConfig
{
	static const TCHAR* SectionName = TEXT("Paper2DPlus.CharacterProfileEditor.Layout");
	static const TCHAR* HitboxSidebarOrderKey = TEXT("HitboxSidebarOrder");
	static const TCHAR* SpriteEditorLeftOrderKey = TEXT("SpriteEditorLeftOrder");
	static const TCHAR* OverviewSplitterLeftKey = TEXT("OverviewSplitterLeft");
	static const TCHAR* OverviewSplitterRightKey = TEXT("OverviewSplitterRight");
	static const TCHAR* HitboxSplitterLeftKey = TEXT("HitboxSplitterLeft");
	static const TCHAR* HitboxSplitterCenterKey = TEXT("HitboxSplitterCenter");
	static const TCHAR* HitboxSplitterRightKey = TEXT("HitboxSplitterRight");
	static const TCHAR* SpriteEditorSplitterLeftKey = TEXT("SpriteEditorSplitterLeft");
	static const TCHAR* SpriteEditorSplitterCenterKey = TEXT("SpriteEditorSplitterCenter");
	static const TCHAR* SpriteEditorSplitterRightKey = TEXT("SpriteEditorSplitterRight");
}

// SCharacterProfileEditorCanvas implementation -> SCharacterProfileEditorCanvas.cpp
// FHitbox3DViewportClient + SHitbox3DViewport implementations -> SHitbox3DViewport.cpp
// FCharacterProfileAssetEditorToolkit implementation -> CharacterProfileAssetEditorToolkit.cpp
// SSpriteEditorCanvas implementation -> SSpriteEditorCanvas.cpp

// ==========================================
// SCharacterProfileAssetEditor Implementation
// ==========================================

SCharacterProfileAssetEditor::~SCharacterProfileAssetEditor()
{
	// Persist current tab / flipbook / frame so next open lands where the user left off.
	// Runs BEFORE the other teardown so Asset is still bound.
	if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
	{
		CharacterProfileEditorPersistence::FState State;
		State.FlipbookName = Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName;
		State.FrameIndex = SelectedFrameIndex >= 0 ? SelectedFrameIndex : 0;
		State.Tab = static_cast<int32>(ActiveTab);
		CharacterProfileEditorPersistence::Save(Asset.Get(), State);
	}

	// Clear static pointer if this is the active editor
	if (GActiveCharacterProfileEditor.Pin().Get() == this)
	{
		GActiveCharacterProfileEditor.Reset();
	}

	StopPlayback();
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectModifiedHandle);
}

void SCharacterProfileAssetEditor::InitializeSectionLayouts()
{
	HitboxSidebarSectionOrder = {
		FName(TEXT("Hitboxes")),
		FName(TEXT("Properties")),
		FName(TEXT("FrameOps"))
	};
	LoadSectionOrder(CharacterProfileEditorLayoutConfig::HitboxSidebarOrderKey, HitboxSidebarSectionOrder, HitboxSidebarSectionOrder);

	SpriteEditorLeftSectionOrder = {
		FName(TEXT("Flipbooks")),
		FName(TEXT("Queue")),
		FName(TEXT("Frames"))
	};
	LoadSectionOrder(CharacterProfileEditorLayoutConfig::SpriteEditorLeftOrderKey, SpriteEditorLeftSectionOrder, SpriteEditorLeftSectionOrder);

	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::OverviewSplitterLeftKey, 0.7f, OverviewSplitterLeftRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::OverviewSplitterRightKey, 0.3f, OverviewSplitterRightRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::HitboxSplitterLeftKey, 0.2f, HitboxSplitterLeftRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::HitboxSplitterCenterKey, 0.55f, HitboxSplitterCenterRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::HitboxSplitterRightKey, 0.25f, HitboxSplitterRightRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::SpriteEditorSplitterLeftKey, 0.2f, SpriteEditorSplitterLeftRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::SpriteEditorSplitterCenterKey, 0.6f, SpriteEditorSplitterCenterRatio);
	LoadFloatLayoutValue(CharacterProfileEditorLayoutConfig::SpriteEditorSplitterRightKey, 0.2f, SpriteEditorSplitterRightRatio);
}

void SCharacterProfileAssetEditor::LoadSectionOrder(const FString& ConfigKey, const TArray<FName>& DefaultOrder, TArray<FName>& InOutOrder) const
{
	InOutOrder = DefaultOrder;

	if (!GConfig)
	{
		return;
	}

	FString StoredOrder;
	if (!GConfig->GetString(CharacterProfileEditorLayoutConfig::SectionName, *ConfigKey, StoredOrder, GEditorPerProjectIni)
		|| StoredOrder.IsEmpty())
	{
		return;
	}

	TArray<FString> Tokens;
	StoredOrder.ParseIntoArray(Tokens, TEXT(","), true);

	TArray<FName> ParsedOrder;
	for (const FString& Token : Tokens)
	{
		const FString Trimmed = Token.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			continue;
		}

		const FName SectionId(*Trimmed);
		if (DefaultOrder.Contains(SectionId) && !ParsedOrder.Contains(SectionId))
		{
			ParsedOrder.Add(SectionId);
		}
	}

	for (const FName& DefaultId : DefaultOrder)
	{
		if (!ParsedOrder.Contains(DefaultId))
		{
			ParsedOrder.Add(DefaultId);
		}
	}

	if (ParsedOrder.Num() == DefaultOrder.Num())
	{
		InOutOrder = MoveTemp(ParsedOrder);
	}
}

void SCharacterProfileAssetEditor::SaveSectionOrder(const FString& ConfigKey, const TArray<FName>& Order) const
{
	if (!GConfig)
	{
		return;
	}

	const FString StoredOrder = FString::JoinBy(Order, TEXT(","), [](const FName& SectionId)
	{
		return SectionId.ToString();
	});

	GConfig->SetString(CharacterProfileEditorLayoutConfig::SectionName, *ConfigKey, *StoredOrder, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SCharacterProfileAssetEditor::LoadFloatLayoutValue(const FString& ConfigKey, float DefaultValue, float& OutValue) const
{
	OutValue = DefaultValue;
	if (!GConfig)
	{
		return;
	}

	float StoredValue = DefaultValue;
	if (GConfig->GetFloat(CharacterProfileEditorLayoutConfig::SectionName, *ConfigKey, StoredValue, GEditorPerProjectIni))
	{
		OutValue = FMath::Clamp(StoredValue, 0.05f, 0.95f);
	}
}

void SCharacterProfileAssetEditor::SaveFloatLayoutValue(const FString& ConfigKey, float Value) const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetFloat(CharacterProfileEditorLayoutConfig::SectionName, *ConfigKey, FMath::Clamp(Value, 0.05f, 0.95f), GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

bool SCharacterProfileAssetEditor::CanMoveSection(const TArray<FName>& SectionOrder, FName SectionId, int32 Direction) const
{
	const int32 CurrentIndex = SectionOrder.IndexOfByKey(SectionId);
	if (CurrentIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 TargetIndex = CurrentIndex + Direction;
	return TargetIndex >= 0 && TargetIndex < SectionOrder.Num();
}

void SCharacterProfileAssetEditor::MoveSectionInOrder(TArray<FName>& SectionOrder, FName SectionId, int32 Direction, const FString& ConfigKey)
{
	const int32 CurrentIndex = SectionOrder.IndexOfByKey(SectionId);
	if (CurrentIndex == INDEX_NONE)
	{
		return;
	}

	const int32 TargetIndex = CurrentIndex + Direction;
	if (TargetIndex < 0 || TargetIndex >= SectionOrder.Num())
	{
		return;
	}

	SectionOrder.Swap(CurrentIndex, TargetIndex);
	SaveSectionOrder(ConfigKey, SectionOrder);
}

void SCharacterProfileAssetEditor::MoveHitboxSidebarSection(FName SectionId, int32 Direction)
{
	MoveSectionInOrder(HitboxSidebarSectionOrder, SectionId, Direction, CharacterProfileEditorLayoutConfig::HitboxSidebarOrderKey);
	RebuildHitboxSidebarSections();
}

void SCharacterProfileAssetEditor::MoveSpriteEditorLeftSection(FName SectionId, int32 Direction)
{
	MoveSectionInOrder(SpriteEditorLeftSectionOrder, SectionId, Direction, CharacterProfileEditorLayoutConfig::SpriteEditorLeftOrderKey);
	RebuildSpriteEditorLeftSections();
}

void SCharacterProfileAssetEditor::SetActivePanelSection(FName SectionId)
{
	ActivePanelSectionId = SectionId;
}

bool SCharacterProfileAssetEditor::IsActivePanelSection(FName SectionId) const
{
	return ActivePanelSectionId == SectionId;
}

const FLinearColor SCharacterProfileAssetEditor::ActivePanelHighlightColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
const FLinearColor SCharacterProfileAssetEditor::InactivePanelColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
const FLinearColor SCharacterProfileAssetEditor::SelectedFrameHighlightColor = FLinearColor(0.15f, 0.45f, 0.75f, 1.0f);
const FLinearColor SCharacterProfileAssetEditor::ActiveFrameColor = FLinearColor(0.20f, 0.60f, 0.30f, 1.0f);

void SCharacterProfileAssetEditor::ForEachSelectedFrame(TFunctionRef<void(int32)> Op)
{
	const int32 FrameCount = GetCurrentFrameCount();
	if (SelectedFrames.Num() > 0)
	{
		for (int32 Idx : SelectedFrames)
		{
			if (Idx >= 0 && Idx < FrameCount)
			{
				Op(Idx);
			}
		}
	}
	else
	{
		if (SelectedFrameIndex >= 0 && SelectedFrameIndex < FrameCount)
		{
			Op(SelectedFrameIndex);
		}
	}
}

void SCharacterProfileAssetEditor::ClearFrameSelection()
{
	SelectedFrames.Empty();
	FrameSelectionAnchorIndex = INDEX_NONE;
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::WrapWithActivePanelHighlight(FName SectionId, float InnerPadding, TSharedRef<SWidget> Content)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this, SectionId]()
		{
			return IsActivePanelSection(SectionId)
				? ActivePanelHighlightColor
				: InactivePanelColor;
		})
		.Padding(1)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(InnerPadding)
			.OnMouseButtonDown_Lambda([this, SectionId](const FGeometry&, const FPointerEvent&)
			{
				SetActivePanelSection(SectionId);
				return FReply::Unhandled();
			})
			[
				Content
			]
		];
}

void SCharacterProfileAssetEditor::TriggerPendingRenameIfNeeded(TMap<int32, TSharedPtr<SInlineEditableTextBlock>>& NameTexts)
{
	if (PendingRenameFlipbookIndex != INDEX_NONE)
	{
		int32 RenameIdx = PendingRenameFlipbookIndex;
		PendingRenameFlipbookIndex = INDEX_NONE;

		if (TSharedPtr<SInlineEditableTextBlock>* FoundText = NameTexts.Find(RenameIdx))
		{
			TWeakPtr<SInlineEditableTextBlock> WeakText = *FoundText;
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
				[WeakText](double, float) -> EActiveTimerReturnType
				{
					if (TSharedPtr<SInlineEditableTextBlock> Text = WeakText.Pin())
					{
						Text->EnterEditingMode();
					}
					return EActiveTimerReturnType::Stop;
				}));
		}
	}
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildReorderableSectionCard(
	FName SectionId,
	const FText& SectionTitle,
	const FText& SectionTooltip,
	TSharedRef<SWidget> ContentWidget,
	bool bStretchContent)
{
	TSharedRef<SVerticalBox> CardContent = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(STextBlock)
			.Text(SectionTitle)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];

	if (bStretchContent)
	{
		CardContent->AddSlot()
		.FillHeight(1.0f)
		[
			ContentWidget
		];
	}
	else
	{
		CardContent->AddSlot()
		.AutoHeight()
		[
			ContentWidget
		];
	}

	// Note: BuildReorderableSectionCard adds extra .ToolTipText and .Clipping on the inner border,
	// so we can't use WrapWithActivePanelHighlight directly here.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([this, SectionId]()
		{
			return IsActivePanelSection(SectionId)
				? ActivePanelHighlightColor
				: InactivePanelColor;
		})
		.Padding(1)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4)
			.ToolTipText(SectionTooltip)
			.Clipping(EWidgetClipping::ClipToBounds)
			.OnMouseButtonDown_Lambda([this, SectionId](const FGeometry&, const FPointerEvent&)
			{
				SetActivePanelSection(SectionId);
				return FReply::Unhandled();
			})
			[
				CardContent
			]
		];
}

void SCharacterProfileAssetEditor::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;

	if (Asset.IsValid())
	{
		Asset->SetFlags(RF_Transactional);

		// Auto-populate required groups from project settings
		if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
		{
			bool bAddedAny = false;
			for (const FGameplayTag& RequiredTag : Settings->RequiredTagMappings)
			{
				if (RequiredTag.IsValid() && !Asset->TagMappings.Contains(RequiredTag))
				{
					if (!bAddedAny)
					{
						Asset->Modify();
						bAddedAny = true;
					}
					Asset->TagMappings.Add(RequiredTag, FFlipbookTagMapping());
				}
			}
		}
	}

	InitializeSectionLayouts();

	ChildSlot
	[
		SNew(SVerticalBox)

		// Tab bar at top
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildTabBar()
		]

		// Tab content switcher
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(TabSwitcher, SWidgetSwitcher)
			.WidgetIndex_Lambda([this]() { return static_cast<int32>(ActiveTab); })

			// Tab 0: Overview
			+ SWidgetSwitcher::Slot()
			[
				BuildOverviewTab()
			]

			// Tab 1: Hitbox Editor
			+ SWidgetSwitcher::Slot()
			[
				BuildHitboxEditorTab()
			]

			// Tab 2: Sprite Alignment
			+ SWidgetSwitcher::Slot()
			[
				BuildSpriteEditorTab()
			]

			// Tab 3: Frame Timing Editor
			+ SWidgetSwitcher::Slot()
			[
				BuildFrameTimingTab()
			]

			// Tab 4: Frame Events (formerly Effect Alignment; Phases tab deleted)
			+ SWidgetSwitcher::Slot()
			[
				BuildFrameEventsTab()
			]

			// Tab 5: Root Motion
			+ SWidgetSwitcher::Slot()
			[
				BuildRootMotionTab()
			]
		]
	];

	// Auto-sync all frame arrays to flipbook frame counts on editor open
	if (Asset.IsValid())
	{
		Asset->SyncAllFramesToFlipbooks();
	}

	// Register for undo/redo notifications
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	// Listen for external modifications to the asset (e.g., sprite extractor adding flipbooks)
	OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(this, &SCharacterProfileAssetEditor::OnAssetExternallyModified);

	// Register as the active editor for console commands
	GActiveCharacterProfileEditor = SharedThis(this);

	// Restore the last session's tab / flipbook / frame selection for this asset.
	// Done BEFORE the refresh so downstream lambdas see the restored state on first paint.
	if (Asset.IsValid())
	{
		const CharacterProfileEditorPersistence::FState Saved = CharacterProfileEditorPersistence::Load(Asset.Get());

		// Tab: clamp to valid enum range; fall back to Overview on out-of-range.
		if (Saved.Tab >= 0 && Saved.Tab < static_cast<int32>(ECharacterProfileTab::NumTabs))
		{
			ActiveTab = static_cast<ECharacterProfileTab>(Saved.Tab);
		}

		// Flipbook: resolve name → index. Falls back to 0 if the stored flipbook was renamed/deleted.
		if (!Saved.FlipbookName.IsEmpty())
		{
			const int32 FoundIdx = Asset->Flipbooks.IndexOfByPredicate(
				[&](const FFlipbookProfileEntry& Entry) { return Entry.Identity.FlipbookName == Saved.FlipbookName; });
			if (FoundIdx != INDEX_NONE)
			{
				SelectedFlipbookIndex = FoundIdx;
			}
		}

		// Frame: clamp against the resolved flipbook's key-frame count.
		if (Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex) && Saved.FrameIndex >= 0)
		{
			UPaperFlipbook* FB = Asset->Flipbooks[SelectedFlipbookIndex].Identity.Flipbook.LoadSynchronous();
			const int32 MaxFrame = FB ? FB->GetNumKeyFrames() - 1 : 0;
			SelectedFrameIndex = FMath::Clamp(Saved.FrameIndex, 0, FMath::Max(0, MaxFrame));
		}
	}

	// Populate the overview tab on initial load
	RefreshOverviewFlipbookList(); // Also refreshes FlipbookGroupsPanel internally
	RefreshTagMappingsPanel();
	RefreshCurrentFrameFlipState();
	SetActivePanelSection(FName(TEXT("Overview.Flipbooks")));
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildTabBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8, 4))
		[
			SNew(SHorizontalBox)

			// Overview tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return (ActiveTab == ECharacterProfileTab::Overview) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(static_cast<int32>(ECharacterProfileTab::Overview)); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OverviewTab", "Overview"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			// Hitbox Editor tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return (ActiveTab == ECharacterProfileTab::Hitboxes) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(static_cast<int32>(ECharacterProfileTab::Hitboxes)); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HitboxEditorTab", "Hitbox Editor"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			// Sprite Editor tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return (ActiveTab == ECharacterProfileTab::SpriteEditor) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(static_cast<int32>(ECharacterProfileTab::SpriteEditor)); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SpriteAlignmentTab", "Sprite Alignment"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			// Frame Timing tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 16, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return (ActiveTab == ECharacterProfileTab::FrameTiming) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(static_cast<int32>(ECharacterProfileTab::FrameTiming)); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FrameTimingTab", "Frame Timing"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			// Frame Events tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 16, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return (ActiveTab == ECharacterProfileTab::FrameEvents) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(static_cast<int32>(ECharacterProfileTab::FrameEvents)); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FrameEventsTab", "Frame Events"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			// Root Motion tab button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 16, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]() { return (ActiveTab == ECharacterProfileTab::RootMotion) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) { SwitchToTab(static_cast<int32>(ECharacterProfileTab::RootMotion)); })
				.Padding(FMargin(8, 4))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RootMotionTab", "Root Motion"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			// Mark Complete checkbox — visible on tool tabs (not Overview)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 12, 0)
			[
				SNew(SCheckBox)
				.Visibility_Lambda([this]()
				{
					// Only show on tool tabs that have a completion bit
					return (ActiveTab >= ECharacterProfileTab::Hitboxes
						&& ActiveTab <= ECharacterProfileTab::RootMotion)
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.IsChecked_Lambda([this]()
				{
					if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return ECheckBoxState::Unchecked;
					// Map tab to completion bit: Hitboxes=0, SpriteEditor=1, FrameTiming=2, FrameEvents=4, RootMotion=5
					int32 Bit = -1;
					switch (ActiveTab)
					{
						case ECharacterProfileTab::Hitboxes: Bit = 0; break;
						case ECharacterProfileTab::SpriteEditor: Bit = 1; break;
						case ECharacterProfileTab::FrameTiming: Bit = 2; break;
						case ECharacterProfileTab::FrameEvents: Bit = 4; break;
						case ECharacterProfileTab::RootMotion: Bit = 5; break;
						default: return ECheckBoxState::Unchecked;
					}
					return (Asset->Flipbooks[SelectedFlipbookIndex].EditorMeta.CompletionFlags & (1 << Bit))
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState)
				{
					if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return;
					int32 Bit = -1;
					switch (ActiveTab)
					{
						case ECharacterProfileTab::Hitboxes: Bit = 0; break;
						case ECharacterProfileTab::SpriteEditor: Bit = 1; break;
						case ECharacterProfileTab::FrameTiming: Bit = 2; break;
						case ECharacterProfileTab::FrameEvents: Bit = 4; break;
						case ECharacterProfileTab::RootMotion: Bit = 5; break;
						default: return;
					}
					BeginTransaction(LOCTEXT("ToggleTabComplete", "Toggle Tab Completion"));
					Asset->Flipbooks[SelectedFlipbookIndex].EditorMeta.CompletionFlags ^= (1 << Bit);
					EndTransaction();
					MarkTabDirty(static_cast<int32>(ECharacterProfileTab::Overview));
				})
				.Padding(FMargin(0, 1))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MarkCompleteCheck", "Complete"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 6, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("OpenShortcutsTooltip", "Open keyboard shortcut reference"))
				.Text(LOCTEXT("ShortcutsButton", "? Shortcuts"))
				.OnClicked_Lambda([this]()
				{
					ShowShortcutReferenceDialog();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("OpenHelpTooltip", "Show help for the current tab"))
				.Text(LOCTEXT("HelpButton", "? Help"))
				.OnClicked_Lambda([this]() {
					FText HelpText;
					switch (GetActiveTab())
					{
					case ECharacterProfileTab::Overview:
						HelpText = LOCTEXT("HelpOverview",
							"OVERVIEW TAB\n\n"
							"Manage your character's flipbook library, PaperZD integration, and extraction.\n\n"
							"FLIPBOOK CARDS\n"
							"  Click: select. Ctrl+Click: multi-select. Right-click: context menu.\n"
							"  Drag cards into Tag Mappings or Phase Groups.\n\n"
							"PAPERZD PANEL (right sidebar)\n"
							"  Set your PaperZD Anim Source, then Re-scan to match existing sequences.\n"
							"  Create: auto-create sequences for all cards missing them.\n\n"
							"BULK EXTRACT\n"
							"  Drag textures from Content Browser onto the card grid, or use the\n"
							"  Bulk Extract button to open the multi-texture extractor.\n\n"
							"Tip: Right-click cards for rename, delete, re-extract, and properties.");
						break;
					case ECharacterProfileTab::Hitboxes:
						HelpText = LOCTEXT("HelpHitboxes",
							"HITBOX EDITOR TAB\n\n"
							"Draw and edit per-frame hitboxes, hurtboxes, collision boxes, and sockets.\n\n"
							"TOOLS\n"
							"  E: Hitbox Tool — drag on empty space to draw, click to select/move/resize.\n"
							"  Q: Socket Tool — click to place attachment points for VFX/projectiles.\n\n"
							"CANVAS\n"
							"  Middle-drag or Right-drag: pan. Scroll: zoom.\n"
							"  Arrow keys or < >: navigate frames. Up/Down: switch flipbooks.\n\n"
							"VIEWS\n"
							"  2D: standard top-down view for precise placement.\n"
							"  3D: perspective view for visualizing hitbox depth (Z axis).\n\n"
							"BATCH OPS\n"
							"  Copy/Paste frame data. Apply hitbox to range. Mirror hitboxes.\n\n"
							"Tip: Use the type filter buttons to show/hide Attack, Hurt, and Collision boxes.");
						break;
					case ECharacterProfileTab::SpriteEditor:
						HelpText = LOCTEXT("HelpSpriteEditor",
							"SPRITE EDITOR TAB\n\n"
							"Adjust per-frame sprite offsets, apply flips, and refine alignment.\n\n"
							"OFFSET EDITING\n"
							"  WASD or Arrow keys: nudge sprite offset by 1 pixel.\n"
							"  Shift+nudge: 5 pixel steps.\n"
							"  X/Y fields: type exact offset values.\n\n"
							"COPY/PASTE\n"
							"  Copy offset from one frame, paste to another or a range.\n\n"
							"CANVAS\n"
							"  Space: play/pause preview.\n"
							"  G: toggle grid. O: onion skin. F: forward onion skin.\n"
							"  R: toggle reference sprite overlay.\n\n"
							"Tip: Offset values are in pixels relative to the sprite's extraction origin.");
						break;
					case ECharacterProfileTab::FrameTiming:
						HelpText = LOCTEXT("HelpFrameTiming",
							"FRAME TIMING TAB\n\n"
							"Adjust playback duration for each frame of the selected flipbook.\n\n"
							"CONTROLS\n"
							"  Click a frame bar: select it. Drag: adjust duration.\n"
							"  FPS presets: quickly set common frame rates (8, 12, 15, 24).\n\n"
							"COLOR CODING\n"
							"  Duration bars are color-coded by FPS: green = fast, amber = normal, red = slow.\n\n"
							"Tip: Frame timing affects hitbox active windows — verify in the Hitbox tab after changes.");
						break;
					case ECharacterProfileTab::FrameEvents:
						HelpText = LOCTEXT("HelpFrameEvents",
							"FRAME EVENTS TAB\n\n"
							"Add and manage frame-triggered events: sounds, camera shakes, VFX, and custom logic.\n\n"
							"EVENT TYPES\n"
							"  One-shot: fires once at the trigger frame (sound, camera shake, spawn).\n"
							"  State: fires Begin/Tick/End across a frame range (gameplay tags, screen effects).\n\n"
							"TIMELINE\n"
							"  Drag events to reposition. Right-click to delete or duplicate.\n"
							"  Click '+' to add a new event at the current frame.\n\n"
							"PREVIEW\n"
							"  Preview canvas shows the current frame with event triggers visualized.\n\n"
							"Tip: Create custom Blueprint events by subclassing Paper2DPlusFrameEvent or Paper2DPlusFrameEventState.");
						break;
					case ECharacterProfileTab::RootMotion:
						HelpText = LOCTEXT("HelpRootMotion",
							"ROOT MOTION TAB\n\n"
							"Author per-frame root motion positions for character movement.\n\n"
							"HOW IT WORKS\n"
							"  Each frame has an (X, Y) position in pixels. The runtime computes\n"
							"  frame-to-frame deltas and applies them as world-space movement.\n\n"
							"CANVAS\n"
							"  Drag the position handle to set the frame's root motion position.\n"
							"  The trajectory line shows the full motion path.\n\n"
							"COORDINATES\n"
							"  X: horizontal (positive = right). Y: vertical (positive = down in Paper2D).\n"
							"  Values are scaled by the flipbook component's world scale at runtime.\n\n"
							"Tip: Use Consume Root Motion Delta in your movement component to apply the motion.");
						break;
					default:
						HelpText = LOCTEXT("HelpDefault",
							"Select a tab to see its specific help.\n\n"
							"Global shortcuts:\n"
							"  Ctrl+Z/Y: Undo/Redo. Left/Right: navigate frames. Up/Down: switch flipbooks.");
						break;
					}
					FMessageDialog::Open(EAppMsgType::Ok, HelpText);
					return FReply::Handled();
				})
			]
		];
}

void SCharacterProfileAssetEditor::ShowShortcutReferenceDialog() const
{
	FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CharacterProfileShortcutsDialog",
		"CharacterProfile Editor Shortcuts\n\n"
		"Global\n"
		"  Ctrl+Z: Undo\n"
		"  Ctrl+Y / Ctrl+Shift+Z: Redo\n"
		"  F2: Rename selected flipbook\n"
		"  Left/Right (or ,/.): Previous/Next frame\n"
		"  Up/Down: Previous/Next flipbook\n\n"
		"Hitbox Editor\n"
		"  E: Edit tool (draw + select/move/resize)\n"
		"  S: Socket tool\n"
		"  G: Toggle grid\n\n"
		"Sprite/Flipbook Tools\n"
		"  Space: Play/Pause\n"
		"  G: Toggle grid\n"
		"  O: Toggle onion skin\n"
		"  F: Toggle forward onion skin\n"
		"  P: Toggle ping-pong playback\n"
		"  R: Toggle/set reference sprite"));
}



TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildFrameTimingTab()
{
	TSharedRef<SFrameTimingEditor> Editor = SAssignNew(FrameTimingEditor, SFrameTimingEditor)
		.Asset(Asset.Get())
		.CollapsedFlipbookGroups(&CollapsedFlipbookGroups)
		.SelectedFrames(&SelectedFrames);

	Editor->OnFlipbookSelectedInList.BindLambda([this](int32 Index)
	{
		SelectedFlipbookIndex = Index;
		SelectedFlipbookCards.Empty();
		SelectedFlipbookCards.Add(Index);
		SelectionAnchorIndex = Index;
	});

	return Editor;
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildFrameEventsTab()
{
	TSharedRef<SFrameEventEditor> Editor = SAssignNew(FrameEventEditor, SFrameEventEditor)
		.Asset(Asset.Get())
		.BuildFlipbookListFunc([this](TSharedPtr<SVerticalBox> ListBox, TFunction<TSharedRef<SWidget>(int32)> ItemBuilder)
		{
			BuildGroupedFlipbookList(ListBox, ItemBuilder);
		});

	Editor->OnFlipbookSelectedInList.BindLambda([this](int32 Index)
	{
		SelectedFlipbookIndex = Index;
		SelectedFlipbookCards.Empty();
		SelectedFlipbookCards.Add(Index);
		SelectionAnchorIndex = Index;
	});

	return Editor;
}

TSharedRef<SWidget> SCharacterProfileAssetEditor::BuildRootMotionTab()
{
	TSharedRef<SRootMotionEditor> Editor = SAssignNew(RootMotionEditor, SRootMotionEditor)
		.Asset(Asset.Get())
		.BuildFlipbookListFunc([this](TSharedPtr<SVerticalBox> ListBox, TFunction<TSharedRef<SWidget>(int32)> ItemBuilder)
		{
			BuildGroupedFlipbookList(ListBox, ItemBuilder);
		});

	Editor->OnRootMotionDataModified.BindLambda([this]()
	{
		RefreshOverviewFlipbookList();
	});

	Editor->OnFlipbookSelectedInList.BindLambda([this](int32 Index)
	{
		SelectedFlipbookIndex = Index;
		SelectedFlipbookCards.Empty();
		SelectedFlipbookCards.Add(Index);
		SelectionAnchorIndex = Index;
	});

	return Editor;
}

void SCharacterProfileAssetEditor::OnEditTimingClicked(int32 FlipbookIndex)
{
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;
	if (FrameTimingEditor.IsValid())
	{
		FrameTimingEditor->SetSelectedFlipbook(FlipbookIndex);
	}
	SwitchToTab(static_cast<int32>(ECharacterProfileTab::FrameTiming));
}

void SCharacterProfileAssetEditor::SetReferenceSprite(int32 FlipbookIndex, int32 FrameIndex)
{
	ReferenceFlipbookIndex = FlipbookIndex;
	ReferenceFrameIndex = FrameIndex;
	bShowReferenceSprite = true;
}

void SCharacterProfileAssetEditor::ClearReferenceSprite()
{
	bShowReferenceSprite = false;
	ReferenceFlipbookIndex = INDEX_NONE;
	ReferenceFrameIndex = INDEX_NONE;
}

void SCharacterProfileAssetEditor::NavigateToFlipbookSpriteEditor(int32 FlipbookIndex)
{
	OnEditSpriteEditorClicked(FlipbookIndex);
}

void SCharacterProfileAssetEditor::SwitchToTab(int32 TabIndex)
{
	const ECharacterProfileTab NewTab = static_cast<ECharacterProfileTab>(TabIndex);

	// Stop playback when switching away from alignment tab
	if (ActiveTab == ECharacterProfileTab::SpriteEditor && NewTab != ECharacterProfileTab::SpriteEditor && bIsPlaying)
	{
		StopPlayback();
	}

	// Stop playback when switching away from frame timing tab
	if (ActiveTab == ECharacterProfileTab::FrameTiming && NewTab != ECharacterProfileTab::FrameTiming && FrameTimingEditor.IsValid())
	{
		FrameTimingEditor->StopPlayback();
	}

	// Stop playback when switching away from frame events tab
	if (ActiveTab == ECharacterProfileTab::FrameEvents && NewTab != ECharacterProfileTab::FrameEvents && FrameEventEditor.IsValid())
	{
		FrameEventEditor->StopPlayback();
	}

	// Stop playback when switching away from root motion tab
	if (ActiveTab == ECharacterProfileTab::RootMotion && NewTab != ECharacterProfileTab::RootMotion && RootMotionEditor.IsValid())
	{
		RootMotionEditor->StopPlayback();
	}

	ClearFrameSelection();
	ActiveTab = NewTab;
	if (TabIndex == static_cast<int32>(ECharacterProfileTab::Overview))
	{
		SetActivePanelSection(FName(TEXT("Overview.Flipbooks")));
	}
	else if (TabIndex == static_cast<int32>(ECharacterProfileTab::Hitboxes))
	{
		SetActivePanelSection(FName(TEXT("Hitbox.Canvas")));
	}
	else if (TabIndex == static_cast<int32>(ECharacterProfileTab::SpriteEditor))
	{
		SetActivePanelSection(FName(TEXT("SpriteEditor.Canvas")));
	}
	else
	{
		SetActivePanelSection(NAME_None);
	}

	if (TabSwitcher.IsValid())
	{
		TabSwitcher->SetActiveWidgetIndex(TabIndex);
	}

	// Refresh the new tab (always — either it's dirty or we just switched to it)
	RefreshTab(TabIndex);
}

void SCharacterProfileAssetEditor::OnEditHitboxesClicked(int32 FlipbookIndex)
{
	SelectedFlipbookIndex = FlipbookIndex;
	SelectedFrameIndex = 0;
	SwitchToTab(static_cast<int32>(ECharacterProfileTab::Hitboxes));
}

FReply SCharacterProfileAssetEditor::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FKey Key = InKeyEvent.GetKey();

	// Let editable text fields (e.g. F2 rename) receive arrow keys, Home, End, etc.
	{
		TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (FocusedWidget.IsValid())
		{
			const FName WidgetType = FocusedWidget->GetType();
			if (WidgetType == TEXT("SEditableText") || WidgetType == TEXT("SMultiLineEditableText") || WidgetType == TEXT("SEditableTextBox"))
			{
				return FReply::Unhandled();
			}
		}
	}

	// Frame Timing Editor handles its own Left/Right frame navigation internally
	if (ActiveTab == ECharacterProfileTab::FrameTiming && (Key == EKeys::Left || Key == EKeys::Right))
	{
		return FReply::Unhandled();
	}

	// Frame Events Editor handles Left/Right (frame nav), Space (playback), and Delete (remove effect) internally
	if (ActiveTab == ECharacterProfileTab::FrameEvents && (Key == EKeys::Left || Key == EKeys::Right ||
		Key == EKeys::SpaceBar || Key == EKeys::Delete || Key == EKeys::BackSpace))
	{
		return FReply::Unhandled();
	}

	// Root Motion Editor handles Left/Right (frame nav), WASD (nudge), and Space (playback) internally
	if (ActiveTab == ECharacterProfileTab::RootMotion && (Key == EKeys::Left || Key == EKeys::Right ||
		Key == EKeys::W || Key == EKeys::A || Key == EKeys::S || Key == EKeys::D || Key == EKeys::SpaceBar))
	{
		return FReply::Unhandled();
	}

	// ──────────────────────────────────────────────────────────────────────
	// Arrow key navigation — see CLAUDE.md "Playback Queue Architecture".
	//
	// Overview tab:  all arrows navigate between flipbook cards (visual order).
	// Tool tabs:     Left/Right = frame nav (wraps within current flipbook,
	//                              UNLESS a queue is active on sprite editor —
	//                              then it wraps to the queue neighbor).
	//                Up/Down = flipbook nav (visual order — or queue position
	//                         if queue is active on sprite editor).
	//
	// Queue state must ONLY affect behavior when IsSpriteEditorQueueActive().
	// If you find yourself writing `if (PlaybackQueue.Num() > 0)` here, stop
	// and use GetQueueAdjacentFlipbookIndex / IsSpriteEditorQueueActive
	// instead — otherwise the queue leaks into hitbox/timing/etc. tabs and
	// breaks the architecture (bugs 11 and 12 from Bugs.md).
	// ──────────────────────────────────────────────────────────────────────

	const bool bIsOverviewTab = (ActiveTab == ECharacterProfileTab::Overview);
	const bool bQueueActive = IsSpriteEditorQueueActive();

	// Helper: select a flipbook card on the overview tab without triggering
	// a full RefreshAll (Overview uses _Lambda bindings — Invalidate is enough).
	auto SelectOverviewCard = [this](int32 NewIdx)
	{
		SelectedFlipbookIndex = NewIdx;
		SelectedFrameIndex = 0;
		ClearFrameSelection();
		MarkAllTabsDirty();
		ClearTabDirty(0);
		Invalidate(EInvalidateWidgetReason::Paint);
	};

	if (Key == EKeys::Left || Key == EKeys::Comma)
	{
		if (bIsOverviewTab)
		{
			// Overview L: previous flipbook card (visual order)
			int32 PrevIdx = GetVisualAdjacentFlipbookIndex(-1);
			if (PrevIdx != INDEX_NONE && PrevIdx != SelectedFlipbookIndex)
			{
				SelectOverviewCard(PrevIdx);
			}
		}
		else if (SelectedFrameIndex > 0)
		{
			OnPrevFrameClicked();
		}
		else if (Asset.IsValid())
		{
			// At frame 0. Queue active? → jump to prev queue entry's LAST frame (wraps around).
			// No queue? → wrap within current flipbook to its last frame.
			int32 PrevIdx = GetQueueAdjacentFlipbookIndex(-1);
			if (PrevIdx != INDEX_NONE && PrevIdx != SelectedFlipbookIndex)
			{
				PlaybackQueueIndex = (PlaybackQueueIndex - 1 + PlaybackQueue.Num()) % PlaybackQueue.Num();
				OnFlipbookSelected(PrevIdx);
				int32 FrameCount = GetCurrentFrameCount();
				if (FrameCount > 0)
				{
					SelectedFrameIndex = FrameCount - 1;
				}
			}
			else
			{
				// Wrap within the same flipbook to its last frame.
				int32 FrameCount = GetCurrentFrameCount();
				if (FrameCount > 1)
				{
					SelectedFrameIndex = FrameCount - 1;
				}
			}
		}
		if (!bIsOverviewTab) RefreshAfterNavigation();
		return FReply::Handled();
	}

	if (Key == EKeys::Right || Key == EKeys::Period)
	{
		if (bIsOverviewTab)
		{
			// Overview R: next flipbook card (visual order)
			int32 NextIdx = GetVisualAdjacentFlipbookIndex(1);
			if (NextIdx != INDEX_NONE && NextIdx != SelectedFlipbookIndex)
			{
				SelectOverviewCard(NextIdx);
			}
		}
		else
		{
			int32 FrameCount = GetCurrentFrameCount();
			if (SelectedFrameIndex < FrameCount - 1)
			{
				OnNextFrameClicked();
			}
			else if (Asset.IsValid())
			{
				// At last frame. Queue active? → jump to next queue entry's FIRST frame (wraps around).
				// No queue? → wrap within current flipbook to frame 0.
				int32 NextIdx = GetQueueAdjacentFlipbookIndex(1);
				if (NextIdx != INDEX_NONE && NextIdx != SelectedFlipbookIndex)
				{
					PlaybackQueueIndex = (PlaybackQueueIndex + 1) % PlaybackQueue.Num();
					OnFlipbookSelected(NextIdx);
				}
				else
				{
					// Wrap within the same flipbook to frame 0.
					if (FrameCount > 1)
					{
						SelectedFrameIndex = 0;
					}
				}
			}
		}
		if (!bIsOverviewTab) RefreshAfterNavigation();
		return FReply::Handled();
	}

	if (Key == EKeys::Up)
	{
		if (!bIsOverviewTab && bQueueActive && PlaybackQueueIndex > 0)
		{
			// Sprite editor + queue: navigate queue position
			PlaybackQueueIndex--;
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
			SyncSelectionToQueueEntry(PlaybackQueueIndex);
		}
		else
		{
			// All other cases: visual flipbook card navigation
			int32 PrevIdx = GetVisualAdjacentFlipbookIndex(-1);
			if (PrevIdx != INDEX_NONE && PrevIdx != SelectedFlipbookIndex)
			{
				if (bIsOverviewTab)
				{
					SelectOverviewCard(PrevIdx);
				}
				else
				{
					OnFlipbookSelected(PrevIdx);
					RefreshAfterNavigation();
				}
			}
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Down)
	{
		if (!bIsOverviewTab && bQueueActive && PlaybackQueueIndex < PlaybackQueue.Num() - 1)
		{
			// Sprite editor + queue: navigate queue position
			PlaybackQueueIndex++;
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
			SyncSelectionToQueueEntry(PlaybackQueueIndex);
		}
		else
		{
			// All other cases: visual flipbook card navigation
			int32 NextIdx = GetVisualAdjacentFlipbookIndex(1);
			if (NextIdx != INDEX_NONE && NextIdx != SelectedFlipbookIndex)
			{
				if (bIsOverviewTab)
				{
					SelectOverviewCard(NextIdx);
				}
				else
				{
					OnFlipbookSelected(NextIdx);
					RefreshAfterNavigation();
				}
			}
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SCharacterProfileAssetEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Global shortcuts (all tabs)
	if (GEditor && InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::Z)
	{
		// On sprite editor tab, try queue undo first
		if (ActiveTab == ECharacterProfileTab::SpriteEditor && PopQueueUndo())
		{
			return FReply::Handled();
		}
		GEditor->UndoTransaction();
		// PostUndo fires RefreshAll for us — explicit call would double-rebuild and flicker.
		return FReply::Handled();
	}

	if (GEditor && InKeyEvent.IsControlDown() && (InKeyEvent.GetKey() == EKeys::Y || (InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::Z)))
	{
		// On sprite editor tab, try queue redo first
		if (ActiveTab == ECharacterProfileTab::SpriteEditor && PopQueueRedo())
		{
			return FReply::Handled();
		}
		GEditor->RedoTransaction();
		// PostRedo fires RefreshAll for us.
		return FReply::Handled();
	}

	// F2 - rename currently selected flipbook (when not editing text fields)
	if (!InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown() && !InKeyEvent.IsAltDown()
		&& InKeyEvent.GetKey() == EKeys::F2)
	{
		TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (FocusedWidget.IsValid())
		{
			const FName WidgetType = FocusedWidget->GetType();
			if (WidgetType == TEXT("SEditableText") || WidgetType == TEXT("SMultiLineEditableText"))
			{
				return FReply::Unhandled();
			}
		}

		if (ActiveTab <= ECharacterProfileTab::SpriteEditor && Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
		{
			TriggerFlipbookRename(SelectedFlipbookIndex);
			return FReply::Handled();
		}
	}

	// Space - toggle playback on any tool tab (not Ctrl+Space, not Overview)
	if (InKeyEvent.GetKey() == EKeys::SpaceBar && !InKeyEvent.IsControlDown()
		&& ActiveTab != ECharacterProfileTab::Overview)
	{
		TogglePlayback();
		return FReply::Handled();
	}

	// Sprite Editor Tab shortcuts
	if (ActiveTab == ECharacterProfileTab::SpriteEditor)
	{

		// O - toggle onion skin
		if (InKeyEvent.GetKey() == EKeys::O)
		{
			bShowOnionSkin = !bShowOnionSkin;
			return FReply::Handled();
		}

		// F - toggle forward onion skin
		if (InKeyEvent.GetKey() == EKeys::F)
		{
			bShowForwardOnionSkin = !bShowForwardOnionSkin;
			return FReply::Handled();
		}

		// P - toggle ping-pong playback
		if (InKeyEvent.GetKey() == EKeys::P)
		{
			bPingPongPlayback = !bPingPongPlayback;
			if (!bPingPongPlayback) { bPlaybackReversed = false; }
			return FReply::Handled();
		}

		// R - toggle reference sprite
		if (InKeyEvent.GetKey() == EKeys::R)
		{
			if (ReferenceFlipbookIndex == INDEX_NONE)
			{
				SetReferenceSprite(SelectedFlipbookIndex, SelectedFrameIndex);
			}
			else
			{
				bShowReferenceSprite = !bShowReferenceSprite;
			}
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	// Overview Tab shortcuts
	if (ActiveTab == ECharacterProfileTab::Overview)
	{
		if (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace)
		{
			if (Asset.IsValid() && Asset->Flipbooks.Num() > 1)
			{
				// Multi-select: delete all selected cards (highest index first to preserve indices)
				TArray<int32> ToDelete;
				if (SelectedFlipbookCards.Num() > 1)
				{
					ToDelete = SelectedFlipbookCards.Array();
				}
				else if (Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex))
				{
					ToDelete.Add(SelectedFlipbookIndex);
				}

				// Don't delete all flipbooks
				if (ToDelete.Num() >= Asset->Flipbooks.Num())
				{
					ToDelete.SetNum(Asset->Flipbooks.Num() - 1);
				}

				if (ToDelete.Num() > 0)
				{
					ToDelete.Sort([](int32 A, int32 B) { return A > B; }); // highest first

					BeginTransaction(LOCTEXT("DeleteFlipbooksTrans", "Delete Flipbooks"));
					for (int32 Idx : ToDelete)
					{
						if (Asset->Flipbooks.IsValidIndex(Idx))
						{
							Asset->RemoveFlipbookFromTagMappings(Asset->Flipbooks[Idx].Identity.FlipbookName);
							Asset->Flipbooks.RemoveAt(Idx);
						}
					}
					EndTransaction();

					SelectedFlipbookCards.Empty();
					SelectedFlipbookIndex = FMath::Clamp(SelectedFlipbookIndex, 0, Asset->Flipbooks.Num() - 1);
					// Survivor flipbook may have fewer frames than the one that was selected —
					// reset frame selection so a stale index doesn't leave the strip unhighlighted.
					SelectedFrameIndex = 0;
					ClearFrameSelection();
					RefreshAll();
				}
			}
			return FReply::Handled();
		}
	}

	// Hitbox Editor Tab shortcuts
	if (ActiveTab == ECharacterProfileTab::Hitboxes)
	{
		if (InKeyEvent.GetKey() == EKeys::E)
		{
			OnToolSelected(EHitboxEditorTool::Edit);
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::Q)
		{
			OnToolSelected(EHitboxEditorTool::Socket);
			return FReply::Handled();
		}

		if (!InKeyEvent.IsControlDown())
		{
			if (InKeyEvent.GetKey() == EKeys::One)
			{
				ActiveDrawType = EHitboxType::Attack;
				EnumAddFlags(HitboxVisibilityMask, EHitboxVisibility::Attack);
				RefreshHitboxList();
				return FReply::Handled();
			}
			if (InKeyEvent.GetKey() == EKeys::Two)
			{
				ActiveDrawType = EHitboxType::Hurtbox;
				EnumAddFlags(HitboxVisibilityMask, EHitboxVisibility::Hurtbox);
				RefreshHitboxList();
				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}

void SCharacterProfileAssetEditor::OnAssetExternallyModified(UObject* Object)
{
	if (Object && Object == Asset.Get())
	{
		// Skip if this editor or any self-contained tab editor is actively modifying the asset.
		// Child editors manage their own refresh during transactions.
		if (ActiveTransaction.IsValid()) return;
		if (FrameTimingEditor.IsValid() && FrameTimingEditor->HasActiveTransaction()) return;
		if (FrameEventEditor.IsValid() && FrameEventEditor->HasActiveTransaction()) return;
		if (RootMotionEditor.IsValid() && RootMotionEditor->HasActiveTransaction()) return;

		// Defer refresh to avoid re-entrant issues during the current transaction
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) {
				RefreshAll();
				return EActiveTimerReturnType::Stop;
			}));
	}
}

void SCharacterProfileAssetEditor::RefreshAfterNavigation()
{
	// Tab-aware refresh after arrow key frame/flipbook navigation
	switch (ActiveTab)
	{
		case ECharacterProfileTab::Hitboxes:
			RefreshFlipbookList();
			RefreshFrameList();
			RefreshHitboxList();
			RefreshPropertiesPanel();
			break;
		case ECharacterProfileTab::SpriteEditor:
			RefreshSpriteEditorFlipbookList();
			RefreshSpriteEditorFrameList();
			RefreshPlaybackQueueList();
			break;
		case ECharacterProfileTab::FrameTiming:
			if (FrameTimingEditor.IsValid())
			{
				FrameTimingEditor->SetSelectedFlipbook(SelectedFlipbookIndex);
			}
			break;
		case ECharacterProfileTab::FrameEvents:
			if (FrameEventEditor.IsValid())
			{
				FrameEventEditor->SetSelectedFlipbook(SelectedFlipbookIndex);
				// SetSelectedFlipbook already calls RefreshAll internally
			}
			break;
		case ECharacterProfileTab::RootMotion:
			if (RootMotionEditor.IsValid())
			{
				RootMotionEditor->SetSelectedFlipbook(SelectedFlipbookIndex);
				// SetSelectedFlipbook already calls RefreshAll internally
			}
			break;
		default:
			break;
	}
}

// ==========================================
// TAB DIRTY FLAGS — Only refresh what's visible, defer the rest
// ==========================================

void SCharacterProfileAssetEditor::MarkAllTabsDirty()
{
	DirtyTabMask = (1 << kNumCharacterProfileTabs) - 1; // All tabs
}

void SCharacterProfileAssetEditor::MarkTabDirty(int32 TabIndex)
{
	if (TabIndex >= 0 && TabIndex < kNumCharacterProfileTabs) DirtyTabMask |= (1 << TabIndex);
}

bool SCharacterProfileAssetEditor::IsTabDirty(int32 TabIndex) const
{
	if (TabIndex < 0 || TabIndex >= kNumCharacterProfileTabs) return false;
	return (DirtyTabMask & (1 << TabIndex)) != 0;
}

void SCharacterProfileAssetEditor::ClearTabDirty(int32 TabIndex)
{
	if (TabIndex >= 0 && TabIndex < kNumCharacterProfileTabs) DirtyTabMask &= ~(1 << TabIndex);
}

void SCharacterProfileAssetEditor::RefreshTab(int32 TabIndex)
{
	switch (TabIndex)
	{
	case static_cast<int32>(ECharacterProfileTab::Overview):
		RefreshOverviewFlipbookList();
		break;
	case static_cast<int32>(ECharacterProfileTab::Hitboxes):
		RefreshFlipbookList();
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();
		break;
	case static_cast<int32>(ECharacterProfileTab::SpriteEditor):
		RefreshSpriteEditorFlipbookList();
		RefreshSpriteEditorFrameList();
		break;
	case static_cast<int32>(ECharacterProfileTab::FrameTiming):
		if (FrameTimingEditor.IsValid())
		{
			FrameTimingEditor->SetSelectedFlipbook(SelectedFlipbookIndex);
			FrameTimingEditor->RefreshAll();
		}
		break;
	case static_cast<int32>(ECharacterProfileTab::FrameEvents):
		if (FrameEventEditor.IsValid())
		{
			FrameEventEditor->SetSelectedFlipbook(SelectedFlipbookIndex);
			FrameEventEditor->RefreshAll();
		}
		break;
	case static_cast<int32>(ECharacterProfileTab::RootMotion):
		if (RootMotionEditor.IsValid())
		{
			RootMotionEditor->SetSelectedFlipbook(SelectedFlipbookIndex);
			RootMotionEditor->RefreshAll();
		}
		break;
	default:
		break;
	}
	ClearTabDirty(TabIndex);
}

void SCharacterProfileAssetEditor::RefreshAll()
{
	// Refresh only the active tab; mark all others dirty for deferred refresh on switch
	MarkAllTabsDirty();
	ClearTabDirty(static_cast<int32>(ActiveTab));
	RefreshTab(static_cast<int32>(ActiveTab));

	// Shared state that's always needed regardless of tab
	RefreshCurrentFrameFlipState();
	RefreshTagMappingsPanel();

	// Purge queue entries referencing invalid flipbook indices (e.g., after undo)
	if (Asset.IsValid() && PlaybackQueue.Num() > 0)
	{
		bool bPurged = false;
		for (int32 i = PlaybackQueue.Num() - 1; i >= 0; i--)
		{
			if (!Asset->Flipbooks.IsValidIndex(PlaybackQueue[i]))
			{
				if (i < PlaybackQueueIndex) PlaybackQueueIndex--;
				else if (i == PlaybackQueueIndex) { PlaybackPosition = 0.0f; CachedPlaybackTiming = FFlipbookTimingData(); }
				PlaybackQueue.RemoveAt(i);
				bPurged = true;
			}
		}
		if (bPurged)
		{
			if (bIsPlaying) StopPlayback();
			PlaybackQueueIndex = FMath::Clamp(PlaybackQueueIndex, 0, FMath::Max(0, PlaybackQueue.Num() - 1));
			RefreshPlaybackQueueList();
		}
	}

	// Refresh 3D viewport (needed after undo/redo to update copied frame data)
	if (Viewport3D.IsValid())
	{
		const FFrameHitboxData* Frame = GetCurrentFrame();
		Viewport3D->SetFrameData(Frame);
		Viewport3D->SetSprite(GetCurrentSprite());
	}

}

void SCharacterProfileAssetEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		ClearFrameSelection();
		RefreshAll();
	}
}

void SCharacterProfileAssetEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		ClearFrameSelection();
		RefreshAll();
	}
}

void SCharacterProfileAssetEditor::SelectFlipbook(int32 Index)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(Index)) return;
	OnFlipbookSelected(Index);
}

void SCharacterProfileAssetEditor::OnFlipbookSelected(int32 Index)
{
	// Pause queue playback on manual selection
	if (bIsPlaying && PlaybackQueue.Num() > 0)
	{
		StopPlayback();
	}

	SelectedFlipbookIndex = Index;
	SelectedFrameIndex = 0;
	ClearFrameSelection();
	SelectedFlipbookCards.Empty();
	SelectedFlipbookCards.Add(Index);
	SelectionAnchorIndex = Index;
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}
	RefreshAll();
}

void SCharacterProfileAssetEditor::OnFrameSelected(int32 Index)
{
	SelectedFrameIndex = Index;
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
	RefreshCurrentFrameFlipState();

	// Update 3D viewport with new frame data
	if (Viewport3D.IsValid())
	{
		const FFrameHitboxData* Frame = GetCurrentFrame();
		Viewport3D->SetFrameData(Frame);
		Viewport3D->SetSprite(GetCurrentSprite());
		Viewport3D->SetSelectedHitbox(-1);
		Viewport3D->SetSelectedSocket(-1);
	}
}

void SCharacterProfileAssetEditor::OnToolSelected(EHitboxEditorTool Tool)
{
	CurrentTool = Tool;
}

bool SCharacterProfileAssetEditor::IsHitboxTypeVisible(EHitboxType Type) const
{
	const EHitboxVisibility Bit =
		(Type == EHitboxType::Attack)  ? EHitboxVisibility::Attack
	  : (Type == EHitboxType::Hurtbox) ? EHitboxVisibility::Hurtbox
	                                   : EHitboxVisibility::None;
	return Bit != EHitboxVisibility::None && EnumHasAnyFlags(HitboxVisibilityMask, Bit);
}

void SCharacterProfileAssetEditor::OnSelectionChanged(EHitboxSelectionType Type, int32 Index)
{
	RefreshHitboxList();
	RefreshPropertiesPanel();

	// Update 3D viewport selection
	if (Viewport3D.IsValid())
	{
		if (Type == EHitboxSelectionType::Hitbox)
		{
			Viewport3D->SetSelectedHitbox(Index);
			Viewport3D->SetSelectedSocket(-1);
		}
		else if (Type == EHitboxSelectionType::Socket)
		{
			Viewport3D->SetSelectedHitbox(-1);
			Viewport3D->SetSelectedSocket(Index);
		}
		else
		{
			Viewport3D->SetSelectedHitbox(-1);
			Viewport3D->SetSelectedSocket(-1);
		}
	}
}

void SCharacterProfileAssetEditor::OnHitboxDataModified()
{
	if (Asset.IsValid())
	{
		Asset->MarkPackageDirty();
	}
	RefreshHitboxList();

	// Refresh 3D viewport
	if (Viewport3D.IsValid())
	{
		const FFrameHitboxData* Frame = GetCurrentFrame();
		Viewport3D->SetFrameData(Frame);
		Viewport3D->SetSprite(GetCurrentSprite());
	}
	RefreshPropertiesPanel();
}

void SCharacterProfileAssetEditor::OnZoomChanged(float NewZoom)
{
	ZoomLevel = NewZoom;
}

void SCharacterProfileAssetEditor::OnPrevFrameClicked()
{
	if (SelectedFrameIndex > 0)
	{
		SelectedFrameIndex--;
		ClearFrameSelection();
		if (EditorCanvas.IsValid())
		{
			EditorCanvas->ClearSelection();
		}
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}

void SCharacterProfileAssetEditor::OnNextFrameClicked()
{
	int32 FrameCount = GetCurrentFrameCount();
	if (SelectedFrameIndex < FrameCount - 1)
	{
		SelectedFrameIndex++;
		ClearFrameSelection();
		if (EditorCanvas.IsValid())
		{
			EditorCanvas->ClearSelection();
		}
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();
	}
}

void SCharacterProfileAssetEditor::BeginTransaction(const FText& Description)
{
	if (Asset.IsValid())
	{
		if (ActiveTransaction.IsValid())
		{
			ActiveTransaction.Reset();
		}

		ActiveTransaction = MakeUnique<FScopedTransaction>(Description);

		Asset->SetFlags(RF_Transactional);
		Asset->Modify();
		Asset->PreEditChange(nullptr);
	}
}

void SCharacterProfileAssetEditor::EndTransaction()
{
	if (Asset.IsValid())
	{
		FPropertyChangedEvent ChangedEvent(nullptr);
		Asset->PostEditChangeProperty(ChangedEvent);
		Asset->MarkPackageDirty();
	}

	if (ActiveTransaction.IsValid())
	{
		ActiveTransaction.Reset();
	}
}

void SCharacterProfileAssetEditor::AddNewFlipbook()
{
	if (!Asset.IsValid()) return;

	// Show flipbook picker first — only create a card when a valid flipbook is selected
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig PickerConfig;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	PickerConfig.Filter.ClassNames.Add(UPaperFlipbook::StaticClass()->GetFName());
#else
	PickerConfig.Filter.ClassPaths.Add(UPaperFlipbook::StaticClass()->GetClassPathName());
#endif
	PickerConfig.bAllowNullSelection = false;
	PickerConfig.InitialAssetViewType = EAssetViewType::Tile;
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([this](const FAssetData& AssetData)
	{
		if (!Asset.IsValid() || !AssetData.IsValid()) return;

		BeginTransaction(LOCTEXT("AddFlipbookTrans", "Add Flipbook"));

		FFlipbookProfileEntry NewAnim;
		NewAnim.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(AssetData.ToSoftObjectPath());
		NewAnim.Identity.FlipbookName = AssetData.AssetName.ToString();

		FFrameHitboxData DefaultFrame;
		DefaultFrame.FrameName = TEXT("Frame_0");
		NewAnim.CombatData.Frames.Add(DefaultFrame);

		int32 NewIndex = Asset->Flipbooks.Add(NewAnim);
		Asset->SyncFramesToFlipbook(NewIndex);

		EndTransaction();

		FSlateApplication::Get().DismissAllMenus();

		SelectedFlipbookIndex = NewIndex;
		SelectedFrameIndex = 0;
		SelectedFlipbookCards.Empty();
		SelectedFlipbookCards.Add(NewIndex);
		SelectionAnchorIndex = NewIndex;

		RefreshOverviewFlipbookList();
		RefreshFlipbookList();
		RefreshSpriteEditorFlipbookList();
		RefreshFrameList();
		RefreshHitboxList();
		RefreshPropertiesPanel();

		TriggerFlipbookRename(NewIndex);
	});

	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("FlipbookPicker", LOCTEXT("AddFlipbookPickerHeader", "Select Flipbook to Add"));
	{
		TSharedRef<SWidget> PickerWidget = ContentBrowserModule.Get().CreateAssetPicker(PickerConfig);
		MenuBuilder.AddWidget(
			SNew(SBox)
			.WidthOverride(300.0f)
			.HeightOverride(400.0f)
			[
				PickerWidget
			],
			FText::GetEmpty(),
			true
		);
	}
	MenuBuilder.EndSection();

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::TypeInPopup
	);
}

void SCharacterProfileAssetEditor::OpenFlipbookPicker(int32 FlipbookIndex)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig PickerConfig;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	PickerConfig.Filter.ClassNames.Add(UPaperFlipbook::StaticClass()->GetFName());
#else
	PickerConfig.Filter.ClassPaths.Add(UPaperFlipbook::StaticClass()->GetClassPathName());
#endif
	PickerConfig.bAllowNullSelection = true;
	PickerConfig.InitialAssetViewType = EAssetViewType::Tile;
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([this, FlipbookIndex](const FAssetData& AssetData)
	{
		if (!Asset.IsValid()) return;
		if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

		FFlipbookProfileEntry& FBData = Asset->Flipbooks[FlipbookIndex];
		const bool bShouldAutoRename =
			(FBData.Identity.FlipbookName.StartsWith(TEXT("Flipbook_")) || FBData.Identity.FlipbookName.TrimStartAndEnd().IsEmpty()) &&
			AssetData.IsValid();

		BeginTransaction(LOCTEXT("ChangeFlipbook", "Change Flipbook"));
		if (AssetData.IsValid())
		{
			FBData.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(AssetData.ToSoftObjectPath());
		}
		else
		{
			FBData.Identity.Flipbook.Reset();
		}
		Asset->SyncFramesToFlipbook(FlipbookIndex);
		EndTransaction();

		FSlateApplication::Get().DismissAllMenus();
		RefreshOverviewFlipbookList();
		RefreshFlipbookList();
		RefreshSpriteEditorFlipbookList();

		if (bShouldAutoRename)
		{
			TriggerFlipbookRename(FlipbookIndex);
		}
	});

	// Initial selection
	const FFlipbookProfileEntry& FBData = Asset->Flipbooks[FlipbookIndex];
	if (!FBData.Identity.Flipbook.IsNull())
	{
		PickerConfig.InitialAssetSelection = FAssetData(FBData.Identity.Flipbook.LoadSynchronous());
	}

	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("FlipbookPicker", LOCTEXT("SelectFlipbook", "Select Flipbook"));
	{
		TSharedRef<SWidget> PickerWidget = ContentBrowserModule.Get().CreateAssetPicker(PickerConfig);
		MenuBuilder.AddWidget(
			SNew(SBox)
			.WidthOverride(400)
			.HeightOverride(500)
			[
				PickerWidget
			],
			FText::GetEmpty(),
			true
		);
	}
	MenuBuilder.EndSection();

	FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::ContextMenu
	);
}

void SCharacterProfileAssetEditor::RenameFlipbook(int32 FlipbookIndex, const FString& NewName)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FString TrimmedName = NewName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty()) return;

	FFlipbookProfileEntry& Anim = Asset->Flipbooks[FlipbookIndex];
	if (Anim.Identity.FlipbookName == TrimmedName) return;

	// Check for duplicate names
	for (int32 i = 0; i < Asset->Flipbooks.Num(); i++)
	{
		if (i != FlipbookIndex && Asset->Flipbooks[i].Identity.FlipbookName == TrimmedName)
		{
			return; // Name already in use
		}
	}

	BeginTransaction(LOCTEXT("RenameFlipbookTrans", "Rename Flipbook"));

	FString OldName = Anim.Identity.FlipbookName;
	Anim.Identity.FlipbookName = TrimmedName;
	Asset->UpdateTagMappingFlipbookName(OldName, TrimmedName);

	EndTransaction();

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshSpriteEditorFlipbookList();
	RefreshTagMappingsPanel();
}

void SCharacterProfileAssetEditor::DuplicateFlipbook(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	BeginTransaction(LOCTEXT("DuplicateFlipbookTrans", "Duplicate Flipbook"));

	FFlipbookProfileEntry NewAnim = Asset->Flipbooks[FlipbookIndex]; // Deep copy
	NewAnim.Identity.FlipbookName = NewAnim.Identity.FlipbookName + TEXT(" (Copy)");

	// Ensure unique name
	FString BaseName = NewAnim.Identity.FlipbookName;
	int32 Counter = 2;
	while (true)
	{
		bool bNameExists = false;
		for (const FFlipbookProfileEntry& Existing : Asset->Flipbooks)
		{
			if (Existing.Identity.FlipbookName == NewAnim.Identity.FlipbookName)
			{
				bNameExists = true;
				break;
			}
		}
		if (!bNameExists) break;
		NewAnim.Identity.FlipbookName = FString::Printf(TEXT("%s %d"), *BaseName, Counter++);
	}

	int32 InsertIndex = FlipbookIndex + 1;
	Asset->Flipbooks.Insert(NewAnim, InsertIndex);

	EndTransaction();

	// Keep queue entries bound to the same logical flipbooks after insertion.
	for (int32& QueueFlipbookIndex : PlaybackQueue)
	{
		if (QueueFlipbookIndex >= InsertIndex)
		{
			QueueFlipbookIndex++;
		}
	}

	SelectedFlipbookIndex = InsertIndex;
	SelectedFrameIndex = 0;

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshSpriteEditorFlipbookList();
	RefreshPlaybackQueueList();
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterProfileAssetEditor::MoveFlipbookUp(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (FlipbookIndex <= 0 || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	BeginTransaction(LOCTEXT("MoveFlipbookUpTrans", "Move Flipbook Up"));

	Asset->Flipbooks.Swap(FlipbookIndex, FlipbookIndex - 1);

	EndTransaction();

	// Remap queue indices so queued entries still point to the same flipbooks after swap.
	for (int32& QueueFlipbookIndex : PlaybackQueue)
	{
		if (QueueFlipbookIndex == FlipbookIndex)
		{
			QueueFlipbookIndex = FlipbookIndex - 1;
		}
		else if (QueueFlipbookIndex == (FlipbookIndex - 1))
		{
			QueueFlipbookIndex = FlipbookIndex;
		}
	}

	SelectedFlipbookIndex = FlipbookIndex - 1;

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshSpriteEditorFlipbookList();
	RefreshPlaybackQueueList();
}

void SCharacterProfileAssetEditor::MoveFlipbookDown(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex) || FlipbookIndex >= Asset->Flipbooks.Num() - 1) return;

	BeginTransaction(LOCTEXT("MoveFlipbookDownTrans", "Move Flipbook Down"));

	Asset->Flipbooks.Swap(FlipbookIndex, FlipbookIndex + 1);

	EndTransaction();

	// Remap queue indices so queued entries still point to the same flipbooks after swap.
	for (int32& QueueFlipbookIndex : PlaybackQueue)
	{
		if (QueueFlipbookIndex == FlipbookIndex)
		{
			QueueFlipbookIndex = FlipbookIndex + 1;
		}
		else if (QueueFlipbookIndex == (FlipbookIndex + 1))
		{
			QueueFlipbookIndex = FlipbookIndex;
		}
	}

	SelectedFlipbookIndex = FlipbookIndex + 1;

	RefreshOverviewFlipbookList();
	RefreshFlipbookList();
	RefreshSpriteEditorFlipbookList();
	RefreshPlaybackQueueList();
}

UPaperFlipbook* SCharacterProfileAssetEditor::GetFlipbookAssetForIndex(int32 FlipbookIndex) const
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return nullptr;
	}

	const FFlipbookProfileEntry& FlipbookData = Asset->Flipbooks[FlipbookIndex];
	if (FlipbookData.Identity.Flipbook.IsNull())
	{
		return nullptr;
	}

	return FlipbookData.Identity.Flipbook.LoadSynchronous();
}

void SCharacterProfileAssetEditor::OpenFlipbookAssetEditor(int32 FlipbookIndex)
{
	UPaperFlipbook* FlipbookAsset = GetFlipbookAssetForIndex(FlipbookIndex);
	if (!FlipbookAsset || !GEditor)
	{
		return;
	}

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(FlipbookAsset);
	}
}

void SCharacterProfileAssetEditor::BrowseToFlipbookAssetInContentBrowser(int32 FlipbookIndex)
{
	UPaperFlipbook* FlipbookAsset = GetFlipbookAssetForIndex(FlipbookIndex);
	if (!FlipbookAsset)
	{
		return;
	}

	TArray<UObject*> AssetsToSync;
	AssetsToSync.Add(FlipbookAsset);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync);
}

void SCharacterProfileAssetEditor::OpenSpriteAssetEditor(UPaperSprite* Sprite)
{
	if (!Sprite || !GEditor)
	{
		return;
	}

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(Sprite);
	}
}

void SCharacterProfileAssetEditor::BrowseToSpriteAssetInContentBrowser(UPaperSprite* Sprite)
{
	if (!Sprite)
	{
		return;
	}

	TArray<UObject*> AssetsToSync;
	AssetsToSync.Add(Sprite);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync);
}

void SCharacterProfileAssetEditor::ShowSpriteContextMenu(UPaperSprite* Sprite, const FVector2D& ScreenSpacePosition, int32 InReferenceFlipbookIndex, int32 InReferenceFrameIndex, int32 InContextFrameIndex, int32 InExcludedFrameIndex)
{
	TWeakObjectPtr<UPaperSprite> WeakSprite = Sprite;

	FMenuBuilder MenuBuilder(true, nullptr);

	if (InReferenceFlipbookIndex != INDEX_NONE && InReferenceFrameIndex != INDEX_NONE)
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("SetFrameAsRef", "Set as Reference Sprite (Frame {0})"), FText::AsNumber(InReferenceFrameIndex)),
			LOCTEXT("SetFrameAsRefTooltip", "Set this frame as the alignment reference sprite"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, InReferenceFlipbookIndex, InReferenceFrameIndex]()
				{
					if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(InReferenceFlipbookIndex))
					{
						SetReferenceSprite(InReferenceFlipbookIndex, InReferenceFrameIndex);
					}
				}),
				FCanExecuteAction::CreateLambda([this, InReferenceFlipbookIndex]()
				{
					return Asset.IsValid() && Asset->Flipbooks.IsValidIndex(InReferenceFlipbookIndex);
				})
			)
		);

		MenuBuilder.AddMenuSeparator();
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenSpriteAsset", "Open Sprite Asset"),
		LOCTEXT("OpenSpriteAssetTooltip", "Open this sprite asset in the Sprite Editor"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, WeakSprite]()
			{
				OpenSpriteAssetEditor(WeakSprite.Get());
			}),
			FCanExecuteAction::CreateLambda([WeakSprite]()
			{
				return WeakSprite.IsValid();
			})
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("BrowseToSpriteAsset", "Browse to Sprite in Content Browser"),
		LOCTEXT("BrowseToSpriteAssetTooltip", "Sync the Content Browser to this sprite asset"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, WeakSprite]()
			{
				BrowseToSpriteAssetInContentBrowser(WeakSprite.Get());
			}),
			FCanExecuteAction::CreateLambda([WeakSprite]()
			{
				return WeakSprite.IsValid();
			})
		)
	);

	// Delete Frame — only on Sprite Editor tab with valid frame
	if (ActiveTab == ECharacterProfileTab::SpriteEditor
		&& Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)
		&& InContextFrameIndex != INDEX_NONE)
	{
		const int32 CapturedFlipbookIndex = SelectedFlipbookIndex;
		const int32 CapturedFrameIndex = InContextFrameIndex;
		UPaperFlipbook* Flipbook = Asset->Flipbooks[CapturedFlipbookIndex].Identity.Flipbook.LoadSynchronous();
		const bool bCanDelete = Flipbook && Flipbook->GetNumKeyFrames() > 1;

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteFrame", "Delete Frame"),
			LOCTEXT("DeleteFrameTooltip", "Permanently delete this frame from the flipbook. Optionally removes the sprite region from the source texture."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, CapturedFlipbookIndex, CapturedFrameIndex]()
				{
					// Show confirmation dialog
					TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
						.Title(LOCTEXT("DeleteFrameTitle", "Delete Frame"))
						.SizingRule(ESizingRule::Autosized)
						.SupportsMaximize(false)
						.SupportsMinimize(false);

					bool bRemoveFromTexture = false;
					bool bConfirmed = false;

					ConfirmWindow->SetContent(
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(16)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("DeleteFrameWarning",
									"Are you sure you want to delete frame {0}?\n\nThis will permanently remove the keyframe from the flipbook\nand all associated hitbox, motion, and event data."),
									FText::AsNumber(CapturedFrameIndex)))
								.AutoWrapText(true)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
							[
								SNew(SCheckBox)
								.OnCheckStateChanged_Lambda([&bRemoveFromTexture](ECheckBoxState State)
								{
									bRemoveFromTexture = (State == ECheckBoxState::Checked);
								})
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RemoveFromTexture", "Also remove sprite region from texture"))
									.ToolTipText(LOCTEXT("RemoveFromTextureTip", "Clear this frame's pixel region in the source texture to transparent. The texture will be re-saved."))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.Text(LOCTEXT("DeleteConfirm", "Delete"))
									.ButtonColorAndOpacity(FLinearColor(0.7f, 0.15f, 0.15f))
									.OnClicked_Lambda([&bConfirmed, &ConfirmWindow]()
									{
										bConfirmed = true;
										ConfirmWindow->RequestDestroyWindow();
										return FReply::Handled();
									})
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.Text(LOCTEXT("DeleteCancel", "Cancel"))
									.OnClicked_Lambda([&ConfirmWindow]()
									{
										ConfirmWindow->RequestDestroyWindow();
										return FReply::Handled();
									})
								]
							]
						]
					);

					GEditor->EditorAddModalWindow(ConfirmWindow);

					if (bConfirmed)
					{
						DeleteFlipbookFrame(CapturedFlipbookIndex, CapturedFrameIndex, bRemoveFromTexture);
					}
				}),
				FCanExecuteAction::CreateLambda([bCanDelete]() { return bCanDelete; })
			)
		);
	}

	// Delete Excluded Frame — same dialog as regular delete, with texture removal option
	if (ActiveTab == ECharacterProfileTab::SpriteEditor
		&& Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)
		&& InExcludedFrameIndex != INDEX_NONE)
	{
		const int32 CapturedFlipbookIndex = SelectedFlipbookIndex;
		const int32 CapturedExcludedIndex = InExcludedFrameIndex;
		const auto& ExcludedFrames = Asset->Flipbooks[CapturedFlipbookIndex].CombatData.ExcludedFrames;
		const bool bValidExcluded = ExcludedFrames.IsValidIndex(CapturedExcludedIndex);

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteFrame", "Delete Frame"),
			LOCTEXT("DeleteExcludedFrameTooltip", "Permanently delete this excluded frame. Optionally removes the sprite region from the source texture."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, CapturedFlipbookIndex, CapturedExcludedIndex]()
				{
					if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(CapturedFlipbookIndex)) return;
					auto& Excluded = Asset->Flipbooks[CapturedFlipbookIndex].CombatData.ExcludedFrames;
					if (!Excluded.IsValidIndex(CapturedExcludedIndex)) return;

					UPaperSprite* TargetSprite = Excluded[CapturedExcludedIndex].KeyFrame.Sprite;

					// Show confirmation dialog
					TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
						.Title(LOCTEXT("DeleteFrameTitle", "Delete Frame"))
						.SizingRule(ESizingRule::Autosized)
						.SupportsMaximize(false)
						.SupportsMinimize(false);

					bool bRemoveFromTexture = false;
					bool bConfirmed = false;

					ConfirmWindow->SetContent(
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(16)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("DeleteExcludedFrameWarning",
									"Are you sure you want to delete this excluded frame?\n\nThis will permanently remove the frame and all associated data."))
								.AutoWrapText(true)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
							[
								SNew(SCheckBox)
								.OnCheckStateChanged_Lambda([&bRemoveFromTexture](ECheckBoxState State)
								{
									bRemoveFromTexture = (State == ECheckBoxState::Checked);
								})
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RemoveFromTexture", "Also remove sprite region from texture"))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.Text(LOCTEXT("DeleteConfirm", "Delete"))
									.ButtonColorAndOpacity(FLinearColor(0.7f, 0.15f, 0.15f))
									.OnClicked_Lambda([&bConfirmed, &ConfirmWindow]()
									{
										bConfirmed = true;
										ConfirmWindow->RequestDestroyWindow();
										return FReply::Handled();
									})
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
									.Text(LOCTEXT("DeleteCancel", "Cancel"))
									.OnClicked_Lambda([&ConfirmWindow]()
									{
										ConfirmWindow->RequestDestroyWindow();
										return FReply::Handled();
									})
								]
							]
						]
					);

					GEditor->EditorAddModalWindow(ConfirmWindow);

					if (bConfirmed)
					{
						BeginTransaction(LOCTEXT("DeleteExcludedFrameTxn", "Delete Excluded Frame"));

						if (bRemoveFromTexture)
						{
							RemoveSpriteRegionFromTexture(TargetSprite);
						}

						Excluded.RemoveAt(CapturedExcludedIndex);
						EndTransaction();

						SelectedExcludedFrameIndex = INDEX_NONE;
						RefreshAll();
					}
				}),
				FCanExecuteAction::CreateLambda([bValidExcluded]() { return bValidExcluded; })
			)
		);
	}

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		ScreenSpacePosition,
		FPopupTransitionEffect::ContextMenu
	);
}

void SCharacterProfileAssetEditor::ShowFlipbookContextMenu(int32 FlipbookIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RenameAnim", "Rename"),
		LOCTEXT("RenameFlipbookTooltip", "Rename this flipbook"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			TriggerFlipbookRename(FlipbookIndex);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DuplicateAnim", "Duplicate"),
		LOCTEXT("DuplicateFlipbookTooltip", "Create a copy of this flipbook"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			DuplicateFlipbook(FlipbookIndex);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteAnim", "Delete"),
		LOCTEXT("DeleteFlipbookTooltip", "Delete this flipbook"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				SelectedFlipbookIndex = FlipbookIndex;
				RemoveSelectedFlipbook();
				RefreshOverviewFlipbookList();
			}),
			FCanExecuteAction::CreateLambda([this]() { return Asset.IsValid() && Asset->Flipbooks.Num() > 1; })
		)
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenFlipbookAsset", "Open Flipbook Asset"),
		LOCTEXT("OpenFlipbookAssetTooltip", "Open this flipbook asset in its editor"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				OpenFlipbookAssetEditor(FlipbookIndex);
			}),
			FCanExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				return GetFlipbookAssetForIndex(FlipbookIndex) != nullptr;
			})
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("BrowseToFlipbookAsset", "Browse to Flipbook in Content Browser"),
		LOCTEXT("BrowseToFlipbookAssetTooltip", "Sync the Content Browser to this flipbook asset"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				BrowseToFlipbookAssetInContentBrowser(FlipbookIndex);
			}),
			FCanExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				return GetFlipbookAssetForIndex(FlipbookIndex) != nullptr;
			})
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SetAsThumbnail", "Set as Thumbnail"),
		LOCTEXT("SetAsThumbnailTooltip", "Use this flipbook's first-frame sprite as the Content Browser thumbnail for this asset"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;
				BeginTransaction(LOCTEXT("SetThumbnailTrans", "Set Thumbnail Flipbook"));
				Asset->ThumbnailFlipbookName = Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName;
				EndTransaction();
				Asset->MarkPackageDirty();
				// Nudge the thumbnail manager so the Content Browser re-renders on next paint.
				if (UPackage* Pkg = Asset->GetPackage())
				{
					Pkg->MarkPackageDirty();
				}
			}),
			FCanExecuteAction::CreateLambda([this, FlipbookIndex]()
			{
				return Asset.IsValid()
					&& Asset->Flipbooks.IsValidIndex(FlipbookIndex)
					&& Asset->ThumbnailFlipbookName != Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName;
			})
		)
	);

	if (ActiveTab == ECharacterProfileTab::SpriteEditor) // Sprite Editor tab (dup — kept for historic comment)
	{
		MenuBuilder.AddMenuSeparator();

		// Multi-select: add all selected flipbooks to queue
		const bool bHasMultiSelect = SpriteEditorSelectedFlipbooks.Num() > 1
			&& SpriteEditorSelectedFlipbooks.Contains(FlipbookIndex);

		MenuBuilder.AddMenuEntry(
			bHasMultiSelect
				? FText::Format(LOCTEXT("CTXAddSelectedToQueue", "Add {0} to Queue"), FText::AsNumber(SpriteEditorSelectedFlipbooks.Num()))
				: LOCTEXT("CTXAddToQueue", "Add to Queue"),
			bHasMultiSelect
				? LOCTEXT("CTXAddSelectedToQueueTooltip", "Add all selected flipbooks to the playback queue")
				: LOCTEXT("CTXAddToQueueTooltip", "Add this flipbook to the playback queue"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex, bHasMultiSelect]()
			{
				if (bHasMultiSelect)
				{
					// Add in visual order for predictable queue ordering
					PushQueueUndoSnapshot();
					TArray<int32> VisualOrder = GetVisualFlipbookOrder();
					for (int32 Idx : VisualOrder)
					{
						if (SpriteEditorSelectedFlipbooks.Contains(Idx)
							&& Asset.IsValid() && Asset->Flipbooks.IsValidIndex(Idx))
						{
							PlaybackQueue.Add(Idx);
						}
					}
					OnFlipbookSelected(FlipbookIndex);
				}
				else
				{
					AddToPlaybackQueue(FlipbookIndex);
				}
			}))
		);
	}

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXSetAsRefSprite", "Set as Reference Sprite"),
		LOCTEXT("CTXSetAsRefSpriteTooltip", "Set frame 0 of this flipbook as the alignment reference sprite"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			SetReferenceSprite(FlipbookIndex, 0);
		}))
	);

	MenuBuilder.AddMenuSeparator();

	// Shared: resolve source texture from stored value or first sprite's texture
	auto GetSourceTexture = [this, FlipbookIndex]() -> UTexture2D*
	{
		if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return nullptr;
		const FFlipbookProfileEntry& Entry = Asset->Flipbooks[FlipbookIndex];
		UTexture2D* SrcTex = Entry.SourceTexture.IsNull() ? nullptr : Entry.SourceTexture.LoadSynchronous();
		if (!SrcTex)
		{
			UPaperFlipbook* FB = Entry.Identity.Flipbook.IsNull() ? nullptr : Entry.Identity.Flipbook.LoadSynchronous();
			if (FB && FB->GetNumKeyFrames() > 0)
			{
				const FPaperFlipbookKeyFrame& FirstFrame = FB->GetKeyFrameChecked(0);
				if (FirstFrame.Sprite) { SrcTex = FirstFrame.Sprite->GetSourceTexture(); }
			}
		}
		return SrcTex;
	};

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXProperties", "Properties..."),
		LOCTEXT("CTXPropertiesTooltip", "View and edit flipbook metadata (source texture, extraction info, etc.)"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			ShowFlipbookPropertiesWindow(FlipbookIndex);
		}))
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CTXValidate", "Validate Asset"),
		LOCTEXT("CTXValidateTooltip", "Run CharacterProfile validation checks"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (!Asset.IsValid()) return;
			TArray<FCharacterProfileValidationIssue> Issues;
			const bool bValid = Asset->ValidateCharacterProfileAsset(Issues);
			const FText Msg = bValid
				? LOCTEXT("CTXValidatePass", "Validation passed with no errors.")
				: FText::Format(LOCTEXT("CTXValidateFail", "Validation found {0} issues."), FText::AsNumber(Issues.Num()));
			FNotificationInfo Info(Msg);
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}))
	);

	FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::ContextMenu
	);
}

void SCharacterProfileAssetEditor::ShowFlipbookPropertiesWindow(int32 FlipbookIndex)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	FFlipbookProfileEntry& Anim = Asset->Flipbooks[FlipbookIndex];
	UPaperFlipbook* LoadedFlipbook = Anim.Identity.Flipbook.IsNull() ? nullptr : Anim.Identity.Flipbook.LoadSynchronous();

	// Compute stats
	const int32 FrameCount = Anim.CombatData.Frames.Num();
	const int32 ExcludedCount = Anim.CombatData.ExcludedFrames.Num();
	const int32 ExtractionCount = Anim.CombatData.FrameExtractionInfo.Num();
	int32 TotalHitboxes = 0;
	int32 TotalSockets = 0;
	for (const FFrameHitboxData& Frame : Anim.CombatData.Frames)
	{
		TotalHitboxes += Frame.Hitboxes.Num();
		TotalSockets += Frame.Sockets.Num();
	}

	// Run validation for this flipbook only
	TArray<FCharacterProfileValidationIssue> AllIssues;
	Asset->ValidateCharacterProfileAsset(AllIssues);
	TArray<FCharacterProfileValidationIssue> FlipbookIssues;
	const FString AnimLabel = FString::Printf(TEXT("Flipbook[%d]"), FlipbookIndex);
	for (const FCharacterProfileValidationIssue& Issue : AllIssues)
	{
		if (Issue.Context.Contains(AnimLabel))
		{
			FlipbookIssues.Add(Issue);
		}
	}

	TSharedRef<SWindow> PropertiesWindow = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("FlipbookPropertiesTitle", "Properties: {0}"), FText::FromString(Anim.Identity.FlipbookName)))
		.ClientSize(FVector2D(420, 0))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	// We need a WeakPtr to asset for lambdas
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> WeakAsset = Asset;

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// === Info Section ===
	Content->AddSlot().AutoHeight().Padding(8, 8, 8, 4)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("PropsInfoHeader", "INFO"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
	];

	auto AddInfoRow = [&Content](const FText& Label, const FText& Value)
	{
		Content->AddSlot().AutoHeight().Padding(16, 2, 8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SBox).WidthOverride(120)
				[
					SNew(STextBlock).Text(Label)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock).Text(Value)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
	};

	AddInfoRow(LOCTEXT("PropsName", "Name"), FText::FromString(Anim.Identity.FlipbookName));
	AddInfoRow(LOCTEXT("PropsGroup", "Group"), Anim.FlipbookGroup.IsNone() ? LOCTEXT("Ungrouped", "(Ungrouped)") : FText::FromName(Anim.FlipbookGroup));
	AddInfoRow(LOCTEXT("PropsFrames", "Frames"), FText::AsNumber(FrameCount));
	if (ExcludedCount > 0)
	{
		AddInfoRow(LOCTEXT("PropsExcluded", "Excluded Frames"), FText::AsNumber(ExcludedCount));
	}
	AddInfoRow(LOCTEXT("PropsHitboxes", "Total Hitboxes"), FText::AsNumber(TotalHitboxes));
	AddInfoRow(LOCTEXT("PropsSockets", "Total Sockets"), FText::AsNumber(TotalSockets));
	AddInfoRow(LOCTEXT("PropsFlipbook", "Flipbook Asset"), LoadedFlipbook
		? FText::FromString(Anim.Identity.Flipbook.GetAssetName())
		: (Anim.Identity.Flipbook.IsNull() ? LOCTEXT("None", "(None)") : LOCTEXT("Unloaded", "(Not Loaded)")));
	AddInfoRow(LOCTEXT("PropsOutputPath", "Sprites Output"), Anim.SpritesOutputPath.IsEmpty()
		? LOCTEXT("NoOutputPath", "(Not Set)")
		: FText::FromString(Anim.SpritesOutputPath));

	// === Source Texture Section (editable) ===
	Content->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("PropsSourceHeader", "SOURCE TEXTURE"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
	];

	Content->AddSlot().AutoHeight().Padding(16, 2, 8, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(120)
			[
				SNew(STextBlock).Text(LOCTEXT("PropsSourceTex", "Source Texture"))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UTexture2D::StaticClass())
			.ObjectPath_Lambda([WeakAsset, FlipbookIndex]() -> FString
			{
				if (WeakAsset.IsValid() && WeakAsset->Flipbooks.IsValidIndex(FlipbookIndex))
				{
					return WeakAsset->Flipbooks[FlipbookIndex].SourceTexture.ToSoftObjectPath().ToString();
				}
				return FString();
			})
			.OnObjectChanged_Lambda([WeakAsset, FlipbookIndex, this](const FAssetData& NewAsset)
			{
				if (!WeakAsset.IsValid() || !WeakAsset->Flipbooks.IsValidIndex(FlipbookIndex)) return;
				BeginTransaction(LOCTEXT("SetSourceTexture", "Set Source Texture"));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
				WeakAsset->Flipbooks[FlipbookIndex].SourceTexture = TSoftObjectPtr<UTexture2D>(NewAsset.ToSoftObjectPath());
#else
				WeakAsset->Flipbooks[FlipbookIndex].SourceTexture = TSoftObjectPtr<UTexture2D>(NewAsset.GetSoftObjectPath());
#endif
				EndTransaction();
			})
		]
	];

	// === Sprites Output Path (editable) ===
	Content->AddSlot().AutoHeight().Padding(16, 6, 8, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(120)
			[
				SNew(STextBlock).Text(LOCTEXT("PropsOutputPathLabel", "Output Path"))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SEditableTextBox)
			.Text_Lambda([WeakAsset, FlipbookIndex]() -> FText
			{
				if (WeakAsset.IsValid() && WeakAsset->Flipbooks.IsValidIndex(FlipbookIndex))
				{
					return FText::FromString(WeakAsset->Flipbooks[FlipbookIndex].SpritesOutputPath);
				}
				return FText::GetEmpty();
			})
			.OnTextCommitted_Lambda([WeakAsset, FlipbookIndex, this](const FText& NewText, ETextCommit::Type CommitType)
			{
				if (CommitType == ETextCommit::OnCleared) return;
				if (!WeakAsset.IsValid() || !WeakAsset->Flipbooks.IsValidIndex(FlipbookIndex)) return;
				BeginTransaction(LOCTEXT("SetOutputPath", "Set Sprites Output Path"));
				WeakAsset->Flipbooks[FlipbookIndex].SpritesOutputPath = NewText.ToString();
				EndTransaction();
			})
			.Font(FAppStyle::GetFontStyle("SmallFont"))
		]
	];

	// === Validation Section ===
	if (FlipbookIssues.Num() > 0)
	{
		Content->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("PropsValidationHeader", "VALIDATION ({0} issues)"), FText::AsNumber(FlipbookIssues.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(1.0f, 0.7f, 0.3f))
		];

		for (const FCharacterProfileValidationIssue& Issue : FlipbookIssues)
		{
			FLinearColor IssueColor = Issue.Severity == ECharacterProfileValidationSeverity::Error
				? FLinearColor(1.0f, 0.4f, 0.4f)
				: FLinearColor(1.0f, 0.8f, 0.3f);

			Content->AddSlot().AutoHeight().Padding(16, 2, 8, 2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Issue.Message))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(IssueColor)
				.AutoWrapText(true)
			];
		}
	}
	else
	{
		Content->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PropsValidationOK", "VALIDATION: No issues"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(0.3f, 0.8f, 0.3f))
		];
	}

	// Bottom padding
	Content->AddSlot().AutoHeight().Padding(0, 8) [ SNew(SSpacer) ];

	PropertiesWindow->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0)
		[
			Content
		]
	);

	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));
	FSlateApplication::Get().AddModalWindow(PropertiesWindow, ParentWindow);
}

void SCharacterProfileAssetEditor::TriggerFlipbookRename(int32 FlipbookIndex)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}

	PendingRenameFlipbookIndex = FlipbookIndex;

	if (ActiveTab == ECharacterProfileTab::Overview)
	{
		// Try to reuse existing text widget (avoids full rebuild flicker)
		if (TSharedPtr<SInlineEditableTextBlock>* FoundText = FlipbookGroupFlipbookNameTexts.Find(FlipbookIndex))
		{
			if (FoundText->IsValid())
			{
				PendingRenameFlipbookIndex = INDEX_NONE;
				TWeakPtr<SInlineEditableTextBlock> WeakText = *FoundText;
				RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
					[WeakText](double, float) -> EActiveTimerReturnType
					{
						if (TSharedPtr<SInlineEditableTextBlock> Text = WeakText.Pin())
						{
							Text->EnterEditingMode();
						}
						return EActiveTimerReturnType::Stop;
					}));
				return;
			}
		}
		// Fallback: full rebuild to populate text widget map
		RefreshFlipbookGroupsPanel();
	}
	else if (ActiveTab == ECharacterProfileTab::Hitboxes)
	{
		RefreshFlipbookList();
	}
	else if (ActiveTab == ECharacterProfileTab::SpriteEditor)
	{
		RefreshSpriteEditorFlipbookList();
	}
}

void SCharacterProfileAssetEditor::RemoveSelectedFlipbook()
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return;

	if (Asset->Flipbooks.Num() <= 1) return;

	const int32 RemovedIndex = SelectedFlipbookIndex;
	const FString RemovedName = Asset->Flipbooks[SelectedFlipbookIndex].Identity.FlipbookName;

	BeginTransaction(LOCTEXT("RemoveFlipbookTrans", "Remove Flipbook"));

	// Clean up group bindings referencing this flipbook (inside same transaction for undo atomicity)
	Asset->RemoveFlipbookFromTagMappings(RemovedName);
	Asset->Flipbooks.RemoveAt(RemovedIndex);

	bool bQueueChanged = false;
	bool bRemovedActiveQueueEntry = false;
	for (int32 i = PlaybackQueue.Num() - 1; i >= 0; --i)
	{
		if (PlaybackQueue[i] == RemovedIndex)
		{
			if (i < PlaybackQueueIndex)
			{
				PlaybackQueueIndex--;
			}
			else if (i == PlaybackQueueIndex)
			{
				bRemovedActiveQueueEntry = true;
			}
			PlaybackQueue.RemoveAt(i);
			bQueueChanged = true;
		}
		else if (PlaybackQueue[i] > RemovedIndex)
		{
			PlaybackQueue[i]--;
			bQueueChanged = true;
		}
	}

	EndTransaction();

	if (bQueueChanged)
	{
		if (bRemovedActiveQueueEntry)
		{
			PlaybackPosition = 0.0f;
			CachedPlaybackTiming = FFlipbookTimingData();
			if (bIsPlaying)
			{
				StopPlayback();
			}
		}
		PlaybackQueueIndex = FMath::Clamp(PlaybackQueueIndex, 0, FMath::Max(0, PlaybackQueue.Num() - 1));
	}

	if (SelectedFlipbookIndex >= Asset->Flipbooks.Num())
	{
		SelectedFlipbookIndex = Asset->Flipbooks.Num() - 1;
	}
	SelectedFrameIndex = 0;

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	RefreshAll();
}

void SCharacterProfileAssetEditor::AddNewFrame()
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;

	BeginTransaction(LOCTEXT("AddFrame", "Add Frame"));

	FFrameHitboxData NewFrame;
	NewFrame.FrameName = FString::Printf(TEXT("Frame_%d"), Anim->CombatData.Frames.Num());

	int32 NewIndex = Anim->CombatData.Frames.Add(NewFrame);

	EndTransaction();

	SelectedFrameIndex = NewIndex;

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterProfileAssetEditor::RemoveSelectedFrame()
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return;
	if (!Anim->CombatData.Frames.IsValidIndex(SelectedFrameIndex)) return;

	if (Anim->CombatData.Frames.Num() <= 1) return;

	BeginTransaction(LOCTEXT("RemoveFrame", "Remove Frame"));

	Anim->CombatData.Frames.RemoveAt(SelectedFrameIndex);

	EndTransaction();

	if (SelectedFrameIndex >= Anim->CombatData.Frames.Num())
	{
		SelectedFrameIndex = Anim->CombatData.Frames.Num() - 1;
	}

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SCharacterProfileAssetEditor::PermanentlyDeleteFrame(int32 FrameIndex)
{
	if (!Asset.IsValid()) return;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return;

	FFlipbookProfileEntry& Anim = Asset->Flipbooks[SelectedFlipbookIndex];
	UPaperFlipbook* Flipbook = Anim.Identity.Flipbook.LoadSynchronous();
	if (!Flipbook) return;
	if (FrameIndex < 0 || FrameIndex >= Flipbook->GetNumKeyFrames()) return;

	// Must keep at least 1 keyframe
	if (Flipbook->GetNumKeyFrames() <= 1) return;

	// Confirmation dialog
	if (!bSkipDeleteFrameConfirmation)
	{
		TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
			.Title(LOCTEXT("DeleteFrameTitle", "Permanently Delete Frame"))
			.ClientSize(FVector2D(380, 130))
			.SupportsMinimize(false)
			.SupportsMaximize(false)
			.IsTopmostWindow(true);

		bool bConfirmed = false;
		TSharedRef<bool> bRemember = MakeShared<bool>(false);

		ConfirmWindow->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(12)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("DeleteFrameMessage",
						"This will permanently delete frame {0} from the flipbook.\nThis cannot be undone. Continue?"),
						FText::AsNumber(FrameIndex)))
					.AutoWrapText(true)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(SCheckBox)
					.OnCheckStateChanged_Lambda([bRemember](ECheckBoxState State) { *bRemember = (State == ECheckBoxState::Checked); })
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DeleteFrameRemember", "Don't ask again this session"))
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("DeleteFrameYes", "Delete"))
						.OnClicked_Lambda([&bConfirmed, WeakWindow = TWeakPtr<SWindow>(ConfirmWindow)]() -> FReply
						{
							bConfirmed = true;
							if (TSharedPtr<SWindow> W = WeakWindow.Pin()) { W->RequestDestroyWindow(); }
							return FReply::Handled();
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("DeleteFrameNo", "Cancel"))
						.OnClicked_Lambda([WeakWindow = TWeakPtr<SWindow>(ConfirmWindow)]() -> FReply
						{
							if (TSharedPtr<SWindow> W = WeakWindow.Pin()) { W->RequestDestroyWindow(); }
							return FReply::Handled();
						})
					]
				]
			]
		);

		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));
		FSlateApplication::Get().AddModalWindow(ConfirmWindow, ParentWindow);

		if (!bConfirmed) return;
		if (*bRemember) bSkipDeleteFrameConfirmation = true;
	}

	BeginTransaction(LOCTEXT("DeleteFramePermanently", "Delete Frame Permanently"));

	// Remove keyframe from flipbook
	{
		Flipbook->SetFlags(RF_Transactional);
		Flipbook->Modify();
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.KeyFrames.RemoveAt(FrameIndex);
	}
	Flipbook->MarkPackageDirty();

	// Remove frame hitbox data
	if (Anim.CombatData.Frames.IsValidIndex(FrameIndex))
	{
		Anim.CombatData.Frames.RemoveAt(FrameIndex);
	}
	if (Anim.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex))
	{
		Anim.CombatData.FrameExtractionInfo.RemoveAt(FrameIndex);
	}

	EndTransaction();

	// Adjust selection
	ClearFrameSelection();
	if (SelectedFrameIndex >= Flipbook->GetNumKeyFrames())
	{
		SelectedFrameIndex = FMath::Max(0, Flipbook->GetNumKeyFrames() - 1);
	}

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}

	RefreshAll();
}

const FFrameHitboxData* SCharacterProfileAssetEditor::GetCurrentFrame() const
{
	return GetCurrentFrame(SelectedFrameIndex);
}

const FFrameHitboxData* SCharacterProfileAssetEditor::GetCurrentFrame(int32 FrameIdx) const
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return nullptr;
	if (!Anim->CombatData.Frames.IsValidIndex(FrameIdx)) return nullptr;
	return &Anim->CombatData.Frames[FrameIdx];
}

FFrameHitboxData* SCharacterProfileAssetEditor::GetCurrentFrameMutable()
{
	return GetCurrentFrameMutable(SelectedFrameIndex);
}

FFrameHitboxData* SCharacterProfileAssetEditor::GetCurrentFrameMutable(int32 FrameIdx)
{
	FFlipbookProfileEntry* Anim = GetCurrentFlipbookDataMutable();
	if (!Anim) return nullptr;
	if (!Anim->CombatData.Frames.IsValidIndex(FrameIdx)) return nullptr;
	return &Anim->CombatData.Frames[FrameIdx];
}

const FFlipbookProfileEntry* SCharacterProfileAssetEditor::GetCurrentFlipbookData() const
{
	if (!Asset.IsValid()) return nullptr;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

FFlipbookProfileEntry* SCharacterProfileAssetEditor::GetCurrentFlipbookDataMutable()
{
	if (!Asset.IsValid()) return nullptr;
	if (!Asset->Flipbooks.IsValidIndex(SelectedFlipbookIndex)) return nullptr;
	return &Asset->Flipbooks[SelectedFlipbookIndex];
}

TArray<int32> SCharacterProfileAssetEditor::GetSortedFlipbookIndices() const
{
	TArray<int32> Indices;
	if (!Asset.IsValid()) return Indices;

	Indices.SetNum(Asset->Flipbooks.Num());
	for (int32 i = 0; i < Indices.Num(); i++) { Indices[i] = i; }
	Indices.Sort([this](int32 A, int32 B)
	{
		return Asset->Flipbooks[A].Identity.FlipbookName.Compare(Asset->Flipbooks[B].Identity.FlipbookName, ESearchCase::IgnoreCase) < 0;
	});
	return Indices;
}

void SCharacterProfileAssetEditor::BuildGroupedFlipbookList(TSharedPtr<SVerticalBox> ListBox, TFunction<TSharedRef<SWidget>(int32)> ItemBuilder, TFunction<bool(int32)> Filter)
{
	if (!ListBox.IsValid() || !Asset.IsValid()) return;

	// Partition sorted indices by group, applying filter
	TArray<int32> SortedIndices = GetSortedFlipbookIndices();
	TMap<FName, TArray<int32>> FlipbooksByGroup;
	for (int32 i : SortedIndices)
	{
		if (Filter && !Filter(i)) continue;
		FlipbooksByGroup.FindOrAdd(Asset->Flipbooks[i].FlipbookGroup).Add(i);
	}

	// If no groups exist (all ungrouped), render flat list
	if (FlipbooksByGroup.Num() <= 1 && FlipbooksByGroup.Contains(NAME_None))
	{
		const TArray<int32>& Indices = FlipbooksByGroup[NAME_None];
		for (int32 Idx : Indices)
		{
			ListBox->AddSlot().AutoHeight()[ItemBuilder(Idx)];
		}
		return;
	}

	// Build group tree for proper nesting
	TMap<FName, TArray<const FFlipbookGroupInfo*>> Tree = Asset->GetFlipbookGroupTree();

	// Recursive helper to render a group and its children
	TFunction<void(FName, int32)> RenderGroup = [&](FName GroupName, int32 NestLevel)
	{
		const TArray<int32>* GroupIndices = FlipbooksByGroup.Find(GroupName);
		// Only look up child groups for named groups — NAME_None children are root-level groups handled separately
		const TArray<const FFlipbookGroupInfo*>* ChildGroups = GroupName.IsNone() ? nullptr : Tree.Find(GroupName);
		int32 FlipbookCount = GroupIndices ? GroupIndices->Num() : 0;

		// Skip empty groups with no children that have content
		if (FlipbookCount == 0 && !ChildGroups)
		{
			return;
		}

		bool bCollapsed = !Filter && CollapsedFlipbookGroups.Contains(GroupName);
		FString DisplayName = GroupName.IsNone() ? TEXT("Ungrouped") : GroupName.ToString();
		float LeftIndent = static_cast<float>(NestLevel) * 12.0f;

		// Group header
		ListBox->AddSlot()
		.AutoHeight()
		.Padding(LeftIndent, 4, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, GroupName]()
			{
				if (CollapsedFlipbookGroups.Contains(GroupName))
				{
					CollapsedFlipbookGroups.Remove(GroupName);
				}
				else
				{
					CollapsedFlipbookGroups.Add(GroupName);
				}
				// Refresh the active tab's flipbook sidebar
				switch (ActiveTab)
				{
				case ECharacterProfileTab::Hitboxes:
					RefreshFlipbookList();
					break;
				case ECharacterProfileTab::SpriteEditor:
					RefreshSpriteEditorFlipbookList();
					break;
				case ECharacterProfileTab::FrameTiming:
					if (FrameTimingEditor.IsValid()) FrameTimingEditor->RefreshFlipbookList();
					break;
				case ECharacterProfileTab::FrameEvents:
					if (FrameEventEditor.IsValid()) FrameEventEditor->RefreshFlipbookList();
					break;
				case ECharacterProfileTab::RootMotion:
					if (RootMotionEditor.IsValid()) RootMotionEditor->RefreshFlipbookList();
					break;
				default:
					RefreshOverviewFlipbookList();
					break;
				}
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(bCollapsed ? TEXT("\x25B6") : TEXT("\x25BC")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("GroupHeaderFmt", "{0} ({1})"),
						FText::FromString(DisplayName), FText::AsNumber(FlipbookCount)))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]
		];

		if (bCollapsed)
		{
			return;
		}

		// Group items
		if (GroupIndices)
		{
			for (int32 Idx : *GroupIndices)
			{
				ListBox->AddSlot()
				.AutoHeight()
				.Padding(LeftIndent + 8.0f, 0, 0, 0)
				[
					ItemBuilder(Idx)
				];
			}
		}

		// Child groups (recursive)
		if (ChildGroups)
		{
			for (const FFlipbookGroupInfo* ChildGroup : *ChildGroups)
			{
				RenderGroup(ChildGroup->GroupName, NestLevel + 1);
			}
		}
	};

	// Ungrouped flipbooks first
	RenderGroup(NAME_None, 0);

	// Root-level groups
	if (const TArray<const FFlipbookGroupInfo*>* RootGroups = Tree.Find(NAME_None))
	{
		for (const FFlipbookGroupInfo* GroupInfo : *RootGroups)
		{
			RenderGroup(GroupInfo->GroupName, 0);
		}
	}
}

int32 SCharacterProfileAssetEditor::GetCurrentFrameCount() const
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return 0;

	// Prefer flipbook frame count as the authoritative source
	if (!Anim->Identity.Flipbook.IsNull())
	{
		if (UPaperFlipbook* FB = Anim->Identity.Flipbook.LoadSynchronous())
		{
			int32 FlipbookFrames = FB->GetNumKeyFrames();
			if (FlipbookFrames > 0)
			{
				return FlipbookFrames;
			}
		}
	}

	// Fallback to hitbox frames array
	return Anim->CombatData.Frames.Num();
}

UPaperSprite* SCharacterProfileAssetEditor::GetCurrentSprite() const
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim || Anim->Identity.Flipbook.IsNull()) return nullptr;

	UPaperFlipbook* FB = Anim->Identity.Flipbook.LoadSynchronous();
	if (!FB || SelectedFrameIndex < 0 || SelectedFrameIndex >= FB->GetNumKeyFrames()) return nullptr;

	return FB->GetKeyFrameChecked(SelectedFrameIndex).Sprite;
}

#undef LOCTEXT_NAMESPACE
