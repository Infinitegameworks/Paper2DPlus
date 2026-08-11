// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ClashGraphAssetEditorToolkit.h"

#include "ClashGraph/ClashGraphPanel.h"
#include "ClashOutcomeGrid.h"
#include "Paper2DPlusClashGraphAsset.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ClashGraphAssetEditor"

const FName FClashGraphAssetEditorToolkit::GraphTabId(TEXT("ClashGraphEditor_Graph"));
const FName FClashGraphAssetEditorToolkit::GridTabId(TEXT("ClashGraphEditor_Grid"));
const FName FClashGraphAssetEditorToolkit::DetailsTabId(TEXT("ClashGraphEditor_Details"));

FClashGraphAssetEditorToolkit::~FClashGraphAssetEditorToolkit()
{
	if (OnObjectModifiedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectModifiedHandle);
	}
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

FText FClashGraphAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("BaseToolkitName", "Clash Graph Editor");
}

void FClashGraphAssetEditorToolkit::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusClashGraphAsset* InAsset)
{
	EditedAsset = InAsset;

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	DetailsView->SetObject(EditedAsset);
	// The Details tab keeps the raw Edges + Default VISIBLE — a valuable secondary edit path the graph + grid
	// reconcile off of. An edit there re-broadcasts via OnFinishedChangingProperties.
	DetailsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&) { BroadcastChanged(); });

	// Re-broadcast on any external modify of THIS asset (graph write-through, undo redo land here too).
	OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddRaw(this, &FClashGraphAssetEditorToolkit::HandleObjectModified);

	// _v2: Graph + Grid share the left stack as flip-tabs (Graph foreground — the headline view), Details on
	// the right. (Layout bumped from _v1, which had no Graph tab — a saved _v1 arrangement would reference a
	// stack without the now-primary Graph tab.)
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("ClashGraphAssetEditor_Layout_v2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(GraphTabId, ETabState::OpenedTab)
				->AddTab(GridTabId, ETabState::OpenedTab)
				->SetSizeCoefficient(0.7f)
				->SetForegroundTab(GraphTabId)
			)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(DetailsTabId, ETabState::OpenedTab)
				->SetSizeCoefficient(0.3f)
			)
		);

	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, TEXT("ClashGraphAssetEditorApp"), Layout, true, false, InAsset);
}

void FClashGraphAssetEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "Clash Graph Editor"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(GraphTabId, FOnSpawnTab::CreateSP(this, &FClashGraphAssetEditorToolkit::SpawnTab_Graph))
		.SetDisplayName(LOCTEXT("GraphTab", "Graph"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(GridTabId, FOnSpawnTab::CreateSP(this, &FClashGraphAssetEditorToolkit::SpawnTab_Grid))
		.SetDisplayName(LOCTEXT("GridTab", "Outcome Grid"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FClashGraphAssetEditorToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FClashGraphAssetEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(GraphTabId);
	InTabManager->UnregisterTabSpawner(GridTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
}

TSharedRef<SDockTab> FClashGraphAssetEditorToolkit::SpawnTab_Graph(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("GraphTabLabel", "Graph"))
		[
			SAssignNew(GraphPanel, SClashGraphPanel).Toolkit(this)
		];
}

TSharedRef<SDockTab> FClashGraphAssetEditorToolkit::SpawnTab_Grid(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("GridTabLabel", "Outcome Grid"))
		[
			SAssignNew(GridWidget, SClashOutcomeGrid).Asset(EditedAsset)
		];
}

TSharedRef<SDockTab> FClashGraphAssetEditorToolkit::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTabLabel", "Details"))
		[
			DetailsView.ToSharedRef()
		];
}

void FClashGraphAssetEditorToolkit::HandleObjectModified(UObject* Object)
{
	if (Object == EditedAsset)
	{
		BroadcastChanged();
	}
}

void FClashGraphAssetEditorToolkit::BroadcastChanged()
{
	// Coalesce a burst of edits into one next-tick refresh (a single FClashGraph edit can fire several
	// OnObjectModified/OnFinishedChangingProperties signals).
	if (bPendingChangedBroadcast || !GEditor)
	{
		return;
	}
	bPendingChangedBroadcast = true;
	// Weak-capture: the toolkit can be destroyed (editor closed) between scheduling and the tick — a raw
	// `this` here is a use-after-free on close-with-pending-edit. (Named WeakToolkit, NOT WeakThis:
	// TSharedFromThis has a member `WeakThis`, and the shadow is C4458 under -WarningsAsErrors on
	// pre-5.8 engines — the cross-version harness caught it.)
	TWeakPtr<FClashGraphAssetEditorToolkit> WeakToolkit = SharedThis(this);
	GEditor->GetTimerManager()->SetTimerForNextTick([WeakToolkit]()
	{
		TSharedPtr<FClashGraphAssetEditorToolkit> Pinned = WeakToolkit.Pin();
		if (!Pinned.IsValid())
		{
			return;
		}
		Pinned->bPendingChangedBroadcast = false;
		if (Pinned->GridWidget.IsValid()) { Pinned->GridWidget->Rebuild(); }
		Pinned->EditedAssetChangedEvent.Broadcast();
	});
}

void FClashGraphAssetEditorToolkit::PostUndo(bool bSuccess)
{
	if (GridWidget.IsValid()) { GridWidget->Rebuild(); }
	EditedAssetChangedEvent.Broadcast();
}

void FClashGraphAssetEditorToolkit::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

#undef LOCTEXT_NAMESPACE
