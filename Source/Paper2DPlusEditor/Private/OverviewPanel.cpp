// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "OverviewPanel.h"
#include "CharacterProfileEditorModel.h"
#include "FlipbookBrowserPanel.h"
#include "ProfileDetailsPanel.h"
#include "CharacterProfileAssetEditor.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "Widgets/Layout/SSplitter.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
// UE 5.0 has no FAppStyle (AppStyle.h) — route static style access through FEditorStyle (audit, cross-version).
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#ifndef GetAppStyleSetName
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif
// Icon registry is version-agnostic — include on ALL versions (was wrongly only in the 5.1+ branch, breaking 5.0).
#include "Paper2DPlusEditorIcons.h"

#define LOCTEXT_NAMESPACE "OverviewPanel"

void SOverviewPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;

	TSharedPtr<SFlipbookBrowserPanel> BrowserPanel;

	if (InArgs._ShowDetails)
	{
		ChildSlot
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.70f)
			[
				SAssignNew(BrowserPanel, SFlipbookBrowserPanel)
				.Model(Model)
			]

			+ SSplitter::Slot()
			.Value(0.30f)
			[
				SNew(SProfileDetailsPanel)
				.Model(Model)
			]
		];
	}
	else
	{
		// Browser-only (TASK-96 P3): the details column lives in SAnimationsPanel's shared right pane.
		ChildSlot
		[
			SAssignNew(BrowserPanel, SFlipbookBrowserPanel)
			.Model(Model)
		];
	}

	if (BrowserPanel.IsValid())
	{
		BrowserPanel->OnShowFlipbookContextMenu.BindLambda([this](int32 FlipbookIndex)
		{
			ShowFlipbookContextMenu(FlipbookIndex);
		});
	}
}

void SOverviewPanel::ShowFlipbookContextMenu(int32 FlipbookIndex)
{
	if (!Model.IsValid()) return;
	UPaper2DPlusCharacterProfileAsset* Asset = Model->GetAsset();
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex)) return;

	Model->SetSelectedFlipbook(FlipbookIndex);

	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[FlipbookIndex];
	UPaperFlipbook* FB = !Anim.Identity.Flipbook.IsNull() ? Anim.Identity.Flipbook.LoadSynchronous() : nullptr;

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("EditHitboxes", "Edit Hitboxes"),
		LOCTEXT("EditHitboxesTip", "Edit this flipbook's hitboxes & sockets in the Hitbox Editor tab."),
		FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::MenuEditHitbox),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (Model.IsValid()) Model->BringTabToFront(FCharacterProfileAssetEditorToolkit::HitboxEditorTabId);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("EditSprites", "Edit Sprites"),
		LOCTEXT("EditSpritesTip", "Edit this flipbook's sprite alignment in the Sprite Editor tab."),
		FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::MenuEditSprite),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (Model.IsValid()) Model->BringTabToFront(FCharacterProfileAssetEditorToolkit::SpriteEditorTabId);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("EditTiming", "Edit Timing"),
		LOCTEXT("EditTimingTip", "Edit this flipbook's per-frame timing in the Frame Timing tab."),
		FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::MenuEditTiming), // custom clock
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (Model.IsValid()) Model->BringTabToFront(FCharacterProfileAssetEditorToolkit::FrameTimingTabId);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("EditEvents", "Edit Cues"),
		LOCTEXT("EditEventsTip", "Edit this flipbook's Frame Cues and curves in the Frame Cues tab."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::MenuEditEvents),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (Model.IsValid()) Model->BringTabToFront(FCharacterProfileAssetEditorToolkit::FrameEventsTabId);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("EditMotion", "Edit Root Motion"),
		LOCTEXT("EditMotionTip", "Edit this flipbook's root motion in the Root Motion tab."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::MenuEditRootMotion),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			if (Model.IsValid()) Model->BringTabToFront(FCharacterProfileAssetEditorToolkit::RootMotionTabId);
		}))
	);

	MenuBuilder.AddSeparator();

	if (FB)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenFlipbookEditor", "Open Flipbook Editor"),
			LOCTEXT("OpenFlipbookEditorTip", "Open this flipbook in the stock Paper2D flipbook editor."),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::MenuOpenFlipbook),
			FUIAction(FExecuteAction::CreateLambda([FB]()
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(FB);
			}))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("BrowseInCB", "Browse in Content Browser"),
			LOCTEXT("BrowseInCBTip", "Select this flipbook asset in the Content Browser."),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::MenuBrowse),
			FUIAction(FExecuteAction::CreateLambda([FB]()
			{
				TArray<FAssetData> Assets;
				Assets.Add(FAssetData(FB));
				FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				CBModule.Get().SyncBrowserToAssets(Assets);
			}))
		);
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddToQueue", "Add to Queue"),
		LOCTEXT("AddToQueueTip", "Add this flipbook to the playback queue."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), Paper2DPlusEditorIcons::MenuAddToQueue),
		FUIAction(FExecuteAction::CreateLambda([this, FlipbookIndex]()
		{
			if (Model.IsValid()) Model->AddToQueue(FlipbookIndex);
		}))
	);

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::ContextMenu);
}

#undef LOCTEXT_NAMESPACE
