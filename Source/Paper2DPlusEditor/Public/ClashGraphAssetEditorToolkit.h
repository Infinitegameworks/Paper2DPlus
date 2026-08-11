// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"

class IDetailsView;
class SClashGraphPanel;
class SClashOutcomeGrid;
class UPaper2DPlusClashGraphAsset;

/**
 * TASK-77 U5 — bespoke editor for UPaper2DPlusClashGraphAsset, replacing the stock details editor. Tabs
 * (Graph + Grid share the left stack as flip-tabs; Details on the right):
 *  - Graph: the clash NODE-GRAPH (tag nodes + directional "beats" wires over the flat FClashGraph.Edges).
 *  - Grid:  the read-only COMPUTED outcome grid (ResolveClash per concrete pair) + conflict highlights.
 *  - Details: the stock IDetailsView (DisplayName / Default / the raw Edges array kept VISIBLE — a valuable
 *    secondary edit path that the graph + grid reconcile off of, proving the signal seam).
 *
 * Plain FAssetEditorToolkit (no FCharacterProfileEditorModel) — the classic FScopedTransaction + Modify
 * pattern. A single OnObjectModified subscription filtered to the edited asset re-broadcasts the toolkit-
 * owned OnEditedAssetChanged, which both panels bind (graph -> RequestReconcile, grid -> Rebuild).
 */
class FClashGraphAssetEditorToolkit : public FAssetEditorToolkit, public FEditorUndoClient
{
public:
	virtual ~FClashGraphAssetEditorToolkit();

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaper2DPlusClashGraphAsset* InAsset);

	virtual FName GetToolkitFName() const override { return FName("ClashGraphAssetEditor"); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("ClashGraph "); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.86f, 0.35f, 0.35f, 0.5f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	/** Broadcast after the edited asset changes (Details edit / graph write / undo) — both panels bind it. */
	FSimpleMulticastDelegate& OnEditedAssetChanged() { return EditedAssetChangedEvent; }

	UPaper2DPlusClashGraphAsset* GetEditedAsset() const { return EditedAsset; }

	static const FName GraphTabId;
	static const FName GridTabId;
	static const FName DetailsTabId;

private:
	UPaper2DPlusClashGraphAsset* EditedAsset = nullptr;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SClashGraphPanel> GraphPanel;
	TSharedPtr<SClashOutcomeGrid> GridWidget;
	FSimpleMulticastDelegate EditedAssetChangedEvent;
	FDelegateHandle OnObjectModifiedHandle;
	bool bPendingChangedBroadcast = false;

	TSharedRef<class SDockTab> SpawnTab_Graph(const class FSpawnTabArgs& Args);
	TSharedRef<class SDockTab> SpawnTab_Grid(const class FSpawnTabArgs& Args);
	TSharedRef<class SDockTab> SpawnTab_Details(const class FSpawnTabArgs& Args);

	void HandleObjectModified(UObject* Object);
	void BroadcastChanged();   // deferred-coalesced re-broadcast + grid/graph refresh
};
