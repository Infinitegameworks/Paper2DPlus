// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ClashGraph/ClashGraphPanel.h"
#include "AnimationTagChipUtils.h" // SMenuHostedTagPickerGuard - menu-hosted pickers are selection-only

#include "ClashGraph/Paper2DPlusClashGraph.h"
#include "ClashGraph/Paper2DPlusClashGraphNode_Tag.h"
#include "ClashGraph/Paper2DPlusClashGraphSchema.h"
#include "ClashGraphAssetEditorToolkit.h"
#include "Paper2DPlusClash.h"
#include "Paper2DPlusClashGraphAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Misc/EngineVersionComparison.h"
#include "ScopedTransaction.h"
// SGameplayTagPicker is not public before 5.3 — the "+ Add Category…" dropdown degrades to a hint on
// older engines (categories still appear via edges / the Details panel); the harness caught the include.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagPicker.h"
#endif
#include "SGraphPanel.h"
// UE 5.0 compat: FAppStyle statics do not exist; use the established FEditorStyle alias.
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ClashGraphPanel"

namespace
{
	/** The live clash-graph panel — set in Construct, cleared on destruction; the console command's seam. */
	TWeakPtr<SClashGraphPanel> GActiveClashGraphPanel;

	// INI persistence (the AnimationMapPanel recipe: a section in GEditorPerProjectIni; no explicit Flush).
	const TCHAR* ClashGraphPanel_ConfigSection = TEXT("ClashGraphEditor");
	const TCHAR* ClashGraphPanel_ConfigKeyZoom = TEXT("ClashGraphZoom");
	const TCHAR* ClashGraphPanel_ConfigKeyViewX = TEXT("ClashGraphViewX");
	const TCHAR* ClashGraphPanel_ConfigKeyViewY = TEXT("ClashGraphViewY");

	/** 5.6 FVector2f sweep: SGraphEditor gained FVector2f Get/SetViewLocation overloads in 5.6 where the
	 *  FVector2D forms are deprecated virtuals; 5.0-5.5 have ONLY the FVector2D forms. */
	void ClashGraphPanel_GetViewLocation(const TSharedPtr<SGraphEditor>& GraphEditor, FVector2f& OutLocation, float& OutZoom)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		GraphEditor->GetViewLocation(OutLocation, OutZoom);
#else
		FVector2D Location = FVector2D::ZeroVector;
		GraphEditor->GetViewLocation(Location, OutZoom);
		OutLocation = FVector2f(Location);
#endif
	}

	void ClashGraphPanel_SetViewLocation(const TSharedPtr<SGraphEditor>& GraphEditor, const FVector2f& Location, float Zoom)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		GraphEditor->SetViewLocation(Location, Zoom);
#else
		GraphEditor->SetViewLocation(FVector2D(Location), Zoom);
#endif
	}

	FString ClashGraphPanel_ShortName(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return TEXT("(invalid)");
		}
		const FString Full = Tag.ToString();
		const FString Prefix = TEXT("Paper2DPlus.Clash.Category.");
		return Full.StartsWith(Prefix) ? Full.RightChop(Prefix.Len()) : Full;
	}

	FAutoConsoleCommand GClashGraphProbeCommand(
		TEXT("Paper2DPlus.ClashGraphProbe"),
		TEXT("Log the open Clash Graph editor's node/edge counts, per-node positions (name@x,y) and per-edge rows (edge <Winner> -> <Loser>)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			TSharedPtr<SClashGraphPanel> Panel = GActiveClashGraphPanel.Pin();
			if (!Panel.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.ClashGraphProbe: no open Clash Graph panel"));
				return;
			}
			Panel->LogProbe();
		}));

	// Create a seeded clash-graph asset and open its bespoke editor — the ECABridge/headless verification
	// seam (mirrors Paper2DPlus.OpenDrawEditor). Optional arg = the package path (default /Game/ClashGraphVerify).
	FAutoConsoleCommand GClashGraphCreateTestCommand(
		TEXT("Paper2DPlus.CreateClashGraphTestAsset"),
		TEXT("Create a seeded UPaper2DPlusClashGraphAsset (default /Game/ClashGraphVerify) and open its editor."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FString PackagePath = Args.Num() > 0 ? Args[0] : TEXT("/Game/ClashGraphVerify");
			const FString AssetName = FPackageName::GetShortName(PackagePath);
			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.CreateClashGraphTestAsset: package create failed for %s"), *PackagePath);
				return;
			}
			UPaper2DPlusClashGraphAsset* Asset = NewObject<UPaper2DPlusClashGraphAsset>(Package, *AssetName, RF_Public | RF_Standalone);
			if (!Asset)
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.CreateClashGraphTestAsset: asset create failed"));
				return;
			}
			Asset->DisplayName = AssetName;
			Asset->Graph = Paper2DPlusClash::MakeDefaultClashGraph();
			FAssetRegistryModule::AssetCreated(Asset);
			Package->MarkPackageDirty();
			if (GEditor)
			{
				if (UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					Sub->OpenEditorForAsset(Asset);
				}
			}
			UE_LOG(LogTemp, Display, TEXT("Paper2DPlus.CreateClashGraphTestAsset: created+opened %s (%d seed edges)"),
				*PackagePath, Asset->Graph.Edges.Num());
		}));
}

void SClashGraphPanel::Construct(const FArguments& InArgs)
{
	Toolkit = InArgs._Toolkit;

	// --- The transient projection graph (storage + cook/JSON safety) ---
	GraphObj = NewObject<UPaper2DPlusClashGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	GraphObj->ClearFlags(RF_Transactional); // explicit: the asset must be the ONLY transacted object
	GraphObj->Schema = UPaper2DPlusClashGraphSchema::StaticClass();
	GraphObj->AddToRoot();

	// --- The graph-owned delegate seam ---
	GraphObj->OnWireCreateRequested.BindSP(this, &SClashGraphPanel::HandleWireCreateRequested);
	GraphObj->OnWireRemoveRequested.BindSP(this, &SClashGraphPanel::HandleWireRemoveRequested);
	GraphObj->OnNodeMoveCommitted.BindSP(this, &SClashGraphPanel::HandleNodeMoveCommitted);

	// --- Graph commands: Delete (the single row-deletion funnel) + SelectAll ---
	GraphEditorCommands = MakeShared<FUICommandList>();
	GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SClashGraphPanel::DeleteSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SClashGraphPanel::CanDeleteSelectedNodes));
	GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateSP(this, &SClashGraphPanel::SelectAllNodes),
		FCanExecuteAction::CreateSP(this, &SClashGraphPanel::CanSelectAllNodes));

	// --- Reconcile trigger: the toolkit's coalesced "edited asset changed" (Details edits, undo, AND the
	// panel's own write echo all land here) ---
	if (Toolkit)
	{
		EditedAssetChangedHandle = Toolkit->OnEditedAssetChanged().AddSP(this, &SClashGraphPanel::HandleEditedAssetChanged);
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ClashGraphToolbarTitle", "Clash Graph"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0, 0, 0)
			[
				// Add a category node with ZERO edges (a TagNodePositions placement). Picking a tag adds the
				// node + dismisses the dropdown.
				SAssignNew(AddCategoryCombo, SComboButton)
				.ToolTipText(LOCTEXT("AddCategoryTip", "Place a clash category on the graph with no edges yet — then drag from its right edge to another category to author a 'beats' relationship."))
				.OnGetMenuContent(this, &SClashGraphPanel::BuildAddCategoryMenu)
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("AddCategory", "+ Add Category…"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("ClashZoomToFitTip", "Re-center the graph on its nodes (Zoom to Fit)."))
				.Text(LOCTEXT("ClashZoomToFit", "Zoom to Fit"))
				.OnClicked_Lambda([this]()
				{
					if (GraphEditorWidget.IsValid())
					{
						GraphEditorWidget->ZoomToFit(/*bOnlySelection=*/false);
					}
					return FReply::Handled();
				})
			]
		]

		// The graph.
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(0)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				SAssignNew(GraphEditorWidget, SGraphEditor)
				.GraphToEdit(GraphObj)
				.IsEditable(true)
				.AdditionalCommands(GraphEditorCommands)
				.Appearance(TAttribute<FGraphAppearanceInfo>::Create(
					TAttribute<FGraphAppearanceInfo>::FGetter::CreateSP(this, &SClashGraphPanel::GetGraphAppearance)))
			]
		]
	];

	GActiveClashGraphPanel = SharedThis(this);

	// Frame the seeded graph on first open only when there is no saved view to honor.
	bPendingInitialFit = !RestoreViewFromConfig();
	RequestReconcile(); // initial reconcile (applies on the tab's first paint via the timer)
}

SClashGraphPanel::~SClashGraphPanel()
{
	SaveViewToConfig();

	if (Toolkit)
	{
		Toolkit->OnEditedAssetChanged().Remove(EditedAssetChangedHandle);
	}

	// Unroot behind the engine's own guard (the SReferenceViewer pattern): during exit purge the rooted
	// object may already be torn down.
	if (!GExitPurge)
	{
		if (ensure(GraphObj))
		{
			GraphObj->OnWireCreateRequested.Unbind();
			GraphObj->OnWireRemoveRequested.Unbind();
			GraphObj->OnNodeMoveCommitted.Unbind();
			GraphObj->RemoveFromRoot();
		}
	}
}

// =====================================================================================================
// TRANSACTION HELPERS
// =====================================================================================================

void SClashGraphPanel::BeginTransaction(const FText& Description)
{
	ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
	if (UPaper2DPlusClashGraphAsset* Asset = GetAsset())
	{
		Asset->Modify();
	}
}

void SClashGraphPanel::EndTransaction()
{
	ActiveTransaction.Reset();
	// Flush a reconcile that PENDED while the transaction was open (pend, never drop) — unless a graph
	// write scope is still up (it flushes on exit).
	if (bReconcilePending && !bGraphWriteInProgress)
	{
		RequestReconcile();
	}
}

// =====================================================================================================
// RECONCILE ENGINE (full-diff early-out -> wholesale node + wire rebuild)
// =====================================================================================================

void SClashGraphPanel::HandleEditedAssetChanged()
{
	RequestReconcile();
}

void SClashGraphPanel::RequestReconcile()
{
	bReconcilePending = true;
	// Coalesced: one live timer. Slate executes widget active timers from SWidget::Paint, so a backgrounded
	// tab's pending reconcile waits, then applies on the first paint after foregrounding.
	if (!ReconcileTimerHandle.IsValid())
	{
		ReconcileTimerHandle = RegisterActiveTimer(0.0f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SClashGraphPanel::OnReconcileTimer));
	}
}

bool SClashGraphPanel::IsGestureLive() const
{
	// Gesture-liveness poll (a rebuild under a live FDragConnection is a crash; an escape-cancelled drag
	// produces NO drop event, hence poll-until-applied).
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping())
	{
		return true;
	}
	if (GraphEditorWidget.IsValid())
	{
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			if (GraphPanel->HasMouseCapture())
			{
				return true;
			}
		}
	}
	if (GEditor && GEditor->IsTransactionActive())
	{
		return true;
	}
	if (GIsTransacting)
	{
		return true;
	}
	return HasActiveTransaction() || bGraphWriteInProgress;
}

EActiveTimerReturnType SClashGraphPanel::OnReconcileTimer(double /*InCurrentTime*/, float /*InDeltaTime*/)
{
	if (!bReconcilePending)
	{
		return EActiveTimerReturnType::Stop;
	}
	if (IsGestureLive())
	{
		return EActiveTimerReturnType::Continue; // poll until quiet
	}
	ReconcileNow();
	return bReconcilePending ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
}

void SClashGraphPanel::CollectLiveNodes(TMap<FGameplayTag, UPaper2DPlusClashGraphNode_Tag*>& OutNodesByTag,
	TArray<UPaper2DPlusClashGraphNode_Tag*>& OutOrdered) const
{
	if (!GraphObj)
	{
		return;
	}
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		if (UPaper2DPlusClashGraphNode_Tag* TagNode = Cast<UPaper2DPlusClashGraphNode_Tag>(Node))
		{
			OutNodesByTag.Add(TagNode->CategoryTag, TagNode);
			OutOrdered.Add(TagNode);
		}
	}
}

void SClashGraphPanel::ReconcileNow()
{
	if (GIsTransacting)
	{
		bReconcilePending = true; // never mutate the projection while a transaction restore is in flight
		return;
	}

	bReconcilePending = false; // cleared at START (a trigger landing mid-reconcile re-pends)

	if (!GraphObj)
	{
		return;
	}

	using namespace Paper2DPlusClashGraph;
	UPaper2DPlusClashGraphAsset* Asset = GetAsset(); // strictly per-use
	FClashProjection Current = ProjectCurrent(Asset); // null asset -> empty projection (graph empties)

	TMap<FGameplayTag, UPaper2DPlusClashGraphNode_Tag*> LiveByTag;
	TArray<UPaper2DPlusClashGraphNode_Tag*> LiveOrdered;
	CollectLiveNodes(LiveByTag, LiveOrdered);

	// Position drift: any in-place node whose NodePosX/Y differs from map-else-session-cache.
	bool bAnyPositionDrift = false;
	for (const TPair<FGameplayTag, UPaper2DPlusClashGraphNode_Tag*>& Pair : LiveByTag)
	{
		const FVector2D* Expected = Asset ? Asset->TagNodePositions.Find(Pair.Key) : nullptr;
		if (!Expected)
		{
			Expected = SessionPositionCache.Find(Pair.Key);
		}
		if (Expected
			&& (Pair.Value->NodePosX != FMath::RoundToInt32(Expected->X)
				|| Pair.Value->NodePosY != FMath::RoundToInt32(Expected->Y)))
		{
			bAnyPositionDrift = true;
			break;
		}
	}

	const FClashProjectionDiff Diff = DiffProjection(LastProjection, Current, bAnyPositionDrift);
	const TSet<FGameplayTag> NewlyAdded(Diff.NodesToAdd);

	// FULL-diff early-out (the own-echo no-op path). Position reapply runs anyway (idempotent).
	if (Diff.IsEmpty())
	{
		ReapplyPositions(LiveByTag, NewlyAdded);
		LastProjection = MoveTemp(Current);
		return;
	}

	// Capture selection (by tag) + view around the rebuild.
	TSet<FGameplayTag> SelectedTags;
	if (GraphEditorWidget.IsValid())
	{
		for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
		{
			if (const UPaper2DPlusClashGraphNode_Tag* TagNode = Cast<UPaper2DPlusClashGraphNode_Tag>(SelectedObject))
			{
				SelectedTags.Add(TagNode->CategoryTag);
			}
		}
	}

	FVector2f ViewLocation = FVector2f::ZeroVector;
	float ZoomAmount = 1.0f;
	if (GraphEditorWidget.IsValid())
	{
		ClashGraphPanel_GetViewLocation(GraphEditorWidget, ViewLocation, ZoomAmount);
	}

	{
		// bRebuildInProgress for the WHOLE reconcile scope: the schema's wire hooks early-out while the
		// projection is half-built, and the raw pin teardown below can't re-enter the write-through.
		TGuardValue<bool> RebuildGuard(GraphObj->bRebuildInProgress, true);

		// Removals (a removed node's wires die with it — raw teardown via RemoveNode).
		for (const FGameplayTag& RemovedTag : Diff.NodesToRemove)
		{
			if (UPaper2DPlusClashGraphNode_Tag* Node = LiveByTag.FindRef(RemovedTag))
			{
				GraphObj->RemoveNode(Node);
				LiveByTag.Remove(RemovedTag);
			}
		}

		// Adds (positions land in the reapply pass below).
		for (const FGameplayTag& AddedTag : Diff.NodesToAdd)
		{
			UPaper2DPlusClashGraphNode_Tag* NewNode =
				UPaper2DPlusClashGraph::SpawnNodeUntransactional<UPaper2DPlusClashGraphNode_Tag>(*GraphObj,
					[&AddedTag](UPaper2DPlusClashGraphNode_Tag& Node)
					{
						Node.CategoryTag = AddedTag;
					});
			LiveByTag.Add(AddedTag, NewNode);
		}

		// Wires rebuilt WHOLESALE: tear ALL existing pin links (raw — never the schema break methods), then
		// re-link from the current projection's edges. Cheap at clash scale; guarantees the visual wires
		// exactly match Current.Edges (and a survivor's wires that died with a removed neighbor get restored).
		for (const TPair<FGameplayTag, UPaper2DPlusClashGraphNode_Tag*>& Pair : LiveByTag)
		{
			if (UEdGraphPin* OutputPin = Pair.Value->GetOutputPin())
			{
				OutputPin->BreakAllPinLinks();
			}
		}
		for (const FClashProjectedEdge& Edge : Current.Edges)
		{
			UPaper2DPlusClashGraphNode_Tag* WinnerNode = LiveByTag.FindRef(Edge.Winner);
			UPaper2DPlusClashGraphNode_Tag* LoserNode = LiveByTag.FindRef(Edge.Loser);
			if (!WinnerNode || !LoserNode)
			{
				continue; // projection guarantees both ends; defensive against a half-failed spawn
			}
			UEdGraphPin* OutputPin = WinnerNode->GetOutputPin();
			UEdGraphPin* InputPin = LoserNode->GetInputPin();
			if (OutputPin && InputPin && !OutputPin->LinkedTo.Contains(InputPin))
			{
				// (Redundant after the BreakAllPinLinks teardown above, but cheap belt-and-braces: never
				// double-link a pair even if a future change reorders the teardown/relink.)
				OutputPin->MakeLinkTo(InputPin);
			}
		}

		ReapplyPositions(LiveByTag, NewlyAdded);
	} // RebuildGuard released

	// Notify + best-effort reselect by tag.
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->ClearSelectionSet();
	}
	GraphObj->NotifyGraphChanged();
	if (GraphEditorWidget.IsValid())
	{
		for (const FGameplayTag& SelTag : SelectedTags)
		{
			if (UPaper2DPlusClashGraphNode_Tag* Node = LiveByTag.FindRef(SelTag))
			{
				GraphEditorWidget->SetNodeSelection(Node, true);
			}
		}
		ClashGraphPanel_SetViewLocation(GraphEditorWidget, ViewLocation, ZoomAmount);
	}

	// First-open framing (no saved view): frame the seeded nodes once. ZoomToFit defers internally to the
	// next tick, so it works even though topology nodes are created on the deferred purge.
	if (bPendingInitialFit && GraphObj->Nodes.Num() > 0 && GraphEditorWidget.IsValid())
	{
		bPendingInitialFit = false;
		GraphEditorWidget->ZoomToFit(/*bOnlySelection=*/false);
	}

	LastProjection = MoveTemp(Current);
}

void SClashGraphPanel::ReapplyPositions(const TMap<FGameplayTag, UPaper2DPlusClashGraphNode_Tag*>& LiveNodesByTag,
	const TSet<FGameplayTag>& NewlyAdded)
{
	UPaper2DPlusClashGraphAsset* Asset = GetAsset();

	// Occupied = stored map UNION session cache (stored wins — a user gesture beats an auto-layout spot).
	TMap<FGameplayTag, FVector2D> Occupied;
	if (Asset)
	{
		Occupied = Asset->TagNodePositions;
	}
	for (const TPair<FGameplayTag, FVector2D>& Pair : SessionPositionCache)
	{
		if (!Occupied.Contains(Pair.Key))
		{
			Occupied.Add(Pair.Key, Pair.Value);
		}
	}

	// Position-less nodes get a fresh grid layout -> SESSION-TRANSIENT cache ONLY (never the asset; opening
	// the tab never dirties).
	TArray<FGameplayTag> MissingTags;
	for (const TPair<FGameplayTag, UPaper2DPlusClashGraphNode_Tag*>& Pair : LiveNodesByTag)
	{
		if (!Occupied.Contains(Pair.Key))
		{
			MissingTags.Add(Pair.Key);
		}
	}
	if (MissingTags.Num() > 0)
	{
		const TMap<FGameplayTag, FVector2D> Fresh = FirstLayout(MissingTags, Occupied);
		for (const TPair<FGameplayTag, FVector2D>& Pair : Fresh)
		{
			SessionPositionCache.Add(Pair.Key, Pair.Value);
			if (!Occupied.Contains(Pair.Key))
			{
				Occupied.Add(Pair.Key, Pair.Value);
			}
		}
	}

	// Apply with USER-MOVE STICKINESS (the "nodes shuffle themselves" fix). RE-ASSERT the stored position
	// only when (a) the node was freshly spawned / never applied, or (b) the stored value legitimately
	// CHANGED since we last applied it (undo / external edit / committed drag). Otherwise a drifted live
	// position is a USER MOVE — KEEP it (and adopt session-only nodes into the session cache).
	bool bAnyApplied = false;
	for (const TPair<FGameplayTag, UPaper2DPlusClashGraphNode_Tag*>& Pair : LiveNodesByTag)
	{
		const FGameplayTag& Key = Pair.Key;
		UPaper2DPlusClashGraphNode_Tag* Node = Pair.Value;

		const bool bDurable = Asset && Asset->TagNodePositions.Contains(Key);
		const FVector2D* Stored = Occupied.Find(Key);
		const FVector2D* LastApplied = LastAppliedPositions.Find(Key);

		const bool bFreshlySpawned = NewlyAdded.Contains(Key) || LastApplied == nullptr;
		const bool bStoredChanged = Stored && LastApplied
			&& (FMath::RoundToInt32(Stored->X) != FMath::RoundToInt32(LastApplied->X)
				|| FMath::RoundToInt32(Stored->Y) != FMath::RoundToInt32(LastApplied->Y));

		if (Stored && (bFreshlySpawned || bStoredChanged))
		{
			const int32 NewPosX = FMath::RoundToInt32(Stored->X);
			const int32 NewPosY = FMath::RoundToInt32(Stored->Y);
			if (Node->NodePosX != NewPosX || Node->NodePosY != NewPosY)
			{
				Node->NodePosX = NewPosX;
				Node->NodePosY = NewPosY;
				bAnyApplied = true;
			}
			LastAppliedPositions.Add(Key, FVector2D(Node->NodePosX, Node->NodePosY));
		}
		else
		{
			const FVector2D Live(Node->NodePosX, Node->NodePosY);
			if (!bDurable)
			{
				SessionPositionCache.Add(Key, Live);
			}
			LastAppliedPositions.Add(Key, Live);
		}
	}

	if (bAnyApplied && GraphEditorWidget.IsValid())
	{
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			GraphPanel->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
		}
	}
}

TMap<FGameplayTag, FVector2D> SClashGraphPanel::FirstLayout(const TArray<FGameplayTag>& Tags,
	const TMap<FGameplayTag, FVector2D>& Occupied) const
{
	// Simple deterministic grid for position-less nodes (clash graphs are small). Start BELOW any existing
	// occupied node so fresh nodes don't land on top of placed ones.
	float StartY = 0.0f;
	for (const TPair<FGameplayTag, FVector2D>& Pair : Occupied)
	{
		StartY = FMath::Max(StartY, static_cast<float>(Pair.Value.Y) + 150.0f);
	}

	const float ColStep = 260.0f;
	const float RowStep = 150.0f;
	const int32 Columns = 4;

	TMap<FGameplayTag, FVector2D> Result;
	int32 Index = 0;
	for (const FGameplayTag& LayoutTag : Tags)
	{
		if (!LayoutTag.IsValid() || Occupied.Contains(LayoutTag))
		{
			continue;
		}
		const int32 Col = Index % Columns;
		const int32 Row = Index / Columns;
		Result.Add(LayoutTag, FVector2D(Col * ColStep, StartY + Row * RowStep));
		++Index;
	}
	return Result;
}

// =====================================================================================================
// GRAPH-DELEGATE HANDLERS
// =====================================================================================================

bool SClashGraphPanel::HandleWireCreateRequested(const FGameplayTag& Winner, const FGameplayTag& Loser)
{
	// WIRE CREATE (HIGH #1). The schema's hook calls this INSIDE the engine's open GraphEd_CreateConnection
	// transaction. Do NOT ReconcileNow here (the drag machinery holds live pin pointers; a wholesale rebuild
	// under them is the mid-gesture crash class). Append the row, MAKE THE VISUAL LINK DIRECTLY, then set
	// LastProjection so the deferred Modify echo diffs empty and the wire survives.
	if (bGraphWriteInProgress || !GraphObj)
	{
		return false;
	}
	UPaper2DPlusClashGraphAsset* Asset = GetAsset();
	if (!Asset)
	{
		return false;
	}

	UPaper2DPlusClashGraphNode_Tag* WinnerNode = FindLiveNode(Winner);
	UPaper2DPlusClashGraphNode_Tag* LoserNode = FindLiveNode(Loser);
	if (!WinnerNode || !LoserNode)
	{
		return false;
	}
	UEdGraphPin* OutputPin = WinnerNode->GetOutputPin();
	UEdGraphPin* InputPin = LoserNode->GetInputPin();
	if (!OutputPin || !InputPin)
	{
		return false;
	}

	// Pre-validate BEFORE opening the transaction (never open+Modify for a refusal — a spurious empty undo
	// step otherwise): these are exactly AppendEdge's rejection conditions. The schema already disallowed the
	// self-edge case, so an existing exact pair is the only live refusal here.
	if (Winner == Loser)
	{
		return false;
	}
	for (const FClashEdge& Existing : Asset->Graph.Edges)
	{
		if (Existing.Winner == Winner && Existing.Loser == Loser)
		{
			return false; // already present — the wire is already drawn
		}
	}

	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		BeginTransaction(LOCTEXT("CreateClashEdge", "Create Clash Relationship"));
		const bool bAppended = Paper2DPlusClashGraph::AppendEdge(Asset->Graph, Winner, Loser);
		EndTransaction(); // pend-flush suppressed by the guard
		if (!bAppended)
		{
			return false; // unreachable after the pre-check; an empty nested transaction is dropped
		}

		// HIGH #1: make the visual link DIRECTLY (the nodes are non-transactional by the spawn-funnel
		// invariant, so this records nothing; undo restores via the asset's Edges + the reconcile). Guard
		// against a stale link still present from a break whose deferred teardown reconcile has not yet run
		// (break-then-recreate within one paint) — re-link only if not already linked, else two pin links
		// would back one data edge (a desync the identity-blind diff can't see).
		if (!OutputPin->LinkedTo.Contains(InputPin))
		{
			OutputPin->MakeLinkTo(InputPin);
		}

		// Lockstep invariant: refresh the diff baseline BEFORE releasing the guard so the gesture's own
		// Modify echo diffs empty (no wholesale rebuild of the just-made wire). The mid-gesture-pend
		// exception: if an external edit pended, keep the stale baseline so the armed reconcile can't
		// swallow it (the reconcile then takes the topology path — only cost is respawning this wire).
		if (!bReconcilePending)
		{
			LastProjection = ProjectCurrent(Asset);
		}
	}

	return true;
}

void SClashGraphPanel::HandleWireRemoveRequested(const FGameplayTag& Winner, const FGameplayTag& Loser)
{
	// WIRE BREAK (HIGH #2). Reached from the schema's Break* hooks for a USER break. Remove the row and
	// RequestReconcile WITHOUT setting LastProjection — the reconcile then diffs LastProjection (still has
	// the edge) against the current data (no edge), takes the topology path, and tears the orphaned wire.
	if (bGraphWriteInProgress || !GraphObj)
	{
		return;
	}
	UPaper2DPlusClashGraphAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}

	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);
		BeginTransaction(LOCTEXT("RemoveClashEdge", "Remove Clash Relationship"));
		// Remove EVERY row matching the pair, not just the first: the projection dedupes duplicate rows into
		// ONE wire, so breaking that wire must clear the whole logical relationship (else a hidden duplicate
		// row reprojects the wire and the break appears to do nothing).
		Asset->Graph.Edges.RemoveAll([&Winner, &Loser](const FClashEdge& Edge)
		{
			return Edge.Winner == Winner && Edge.Loser == Loser;
		});
		EndTransaction();
		// Deliberately NO LastProjection update (forces the rebuild to clean up the orphaned wire).
	}

	RequestReconcile();
}

void SClashGraphPanel::HandleNodeMoveCommitted(UPaper2DPlusClashGraphNode_Tag* TagNode)
{
	// NODE-MOVE persistence. Reached from SClashGraphNode_Tag::MoveTo on bMarkDirty=true — always inside an
	// ENGINE-opened transaction (drag finalize / arrow nudge): one undo step, one map write per gesture.
	if (!TagNode || !TagNode->CategoryTag.IsValid() || !GraphObj || GraphObj->bRebuildInProgress
		|| bGraphWriteInProgress)
	{
		return;
	}
	UPaper2DPlusClashGraphAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}

	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		// THE one sanctioned direct Asset->Modify() outside BeginTransaction (an engine transaction is
		// already open around this call, so the Modify records into it — one undo step per gesture).
		Asset->Modify();
		Asset->TagNodePositions.Add(TagNode->CategoryTag, FVector2D(TagNode->NodePosX, TagNode->NodePosY));

		// Lockstep: the map now equals the node's live NodePosX/Y (map wins over the session cache in
		// ReapplyPositions), so the Modify echo diffs empty. Same mid-gesture-pend exception as wire-create.
		if (!bReconcilePending)
		{
			LastProjection = ProjectCurrent(Asset);
		}
	}

	if (bReconcilePending)
	{
		RequestReconcile();
	}
}

// =====================================================================================================
// DELETE COMMAND — the single category-removal funnel (removes edges + position, the reconcile drops node)
// =====================================================================================================

bool SClashGraphPanel::CanDeleteSelectedNodes() const
{
	if (!GraphEditorWidget.IsValid())
	{
		return false;
	}
	for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
	{
		const UEdGraphNode* Node = Cast<UEdGraphNode>(SelectedObject);
		if (Node && Node->CanUserDeleteNode())
		{
			return true;
		}
	}
	return false;
}

void SClashGraphPanel::SelectAllNodes()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->SelectAllNodes();
	}
}

void SClashGraphPanel::DeleteSelectedNodes()
{
	if (bGraphWriteInProgress || !GraphObj || !GraphEditorWidget.IsValid())
	{
		return;
	}
	UPaper2DPlusClashGraphAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}

	TArray<FGameplayTag> TagsToDelete;
	for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
	{
		if (const UPaper2DPlusClashGraphNode_Tag* TagNode = Cast<UPaper2DPlusClashGraphNode_Tag>(SelectedObject))
		{
			if (TagNode->CategoryTag.IsValid())
			{
				TagsToDelete.AddUnique(TagNode->CategoryTag);
			}
		}
	}
	if (TagsToDelete.Num() == 0)
	{
		return;
	}

	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);
		BeginTransaction(LOCTEXT("DeleteClashCategories", "Delete Clash Categories"));
		for (const FGameplayTag& DelTag : TagsToDelete)
		{
			// Remove every edge touching this EXACT tag (no hierarchy match — a parent/child is a different
			// node) + its stored placement. The tag then appears in neither edges nor positions, so the
			// reconcile drops its node + wires.
			Asset->Graph.Edges.RemoveAll([&DelTag](const FClashEdge& Edge)
			{
				return Edge.Winner == DelTag || Edge.Loser == DelTag;
			});
			Asset->TagNodePositions.Remove(DelTag);
			// Prune the editor-transient position bookkeeping so a re-added category lays out fresh (and the
			// maps don't accumulate dead tag entries across add/delete cycles).
			SessionPositionCache.Remove(DelTag);
			LastAppliedPositions.Remove(DelTag);
		}
		EndTransaction();
		// No LastProjection update (force the rebuild to drop the node + its wires).
	}

	RequestReconcile();
}

// =====================================================================================================
// ADD CATEGORY
// =====================================================================================================

TSharedRef<SWidget> SClashGraphPanel::BuildAddCategoryMenu()
{
#if ENGINE_MAJOR_VERSION < 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 3)
	// Pre-5.3 fallback (no public SGameplayTagPicker): point at the Details panel's Edges array instead.
	return SNew(SBox)
		.Padding(8.f)
		[
			SNew(STextBlock)
			.WrapTextAt(280.f)
			.Text(LOCTEXT("AddCategoryNoPicker",
				"The tag picker needs UE 5.3+. Add a 'beats' edge in the Details panel instead — its categories appear here as nodes."))
		];
#else
	TWeakPtr<SComboButton> WeakCombo = AddCategoryCombo;

	TArray<FGameplayTagContainer> EditableContainers;
	EditableContainers.Add(FGameplayTagContainer());

	return SNew(SBox)
		.MinDesiredWidth(300.f)
		.Padding(2.f)
		[
			// Menu-hosted picker = selection-only: the guard eats right-clicks so the engine row
			// context menu can't crash the menu stack (SMenuHostedTagPickerGuard doc).
			SNew(SMenuHostedTagPickerGuard)
			[
			SNew(SGameplayTagPicker)
			.Filter(TEXT("Paper2DPlus.Clash.Category"))
			.MultiSelect(false)
			.TagContainers(EditableContainers)
			.OnTagChanged_Lambda([this, WeakCombo](const TArray<FGameplayTagContainer>& Containers)
			{
				FGameplayTag Picked;
				for (const FGameplayTagContainer& Container : Containers)
				{
					for (auto It = Container.CreateConstIterator(); It; ++It)
					{
						if (It->IsValid())
						{
							Picked = *It;
							break;
						}
					}
					if (Picked.IsValid())
					{
						break;
					}
				}
				if (Picked.IsValid())
				{
					AddCategoryNode(Picked);
				}
				// Single-select: dismiss the dropdown once a tag is picked.
				if (TSharedPtr<SComboButton> Combo = WeakCombo.Pin())
				{
					Combo->SetIsOpen(false);
				}
			})
			]
		];
#endif // >= 5.3
}

void SClashGraphPanel::AddCategoryNode(const FGameplayTag& InTag)
{
	if (bGraphWriteInProgress || !InTag.IsValid())
	{
		return;
	}
	UPaper2DPlusClashGraphAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}
	// Already on the graph (from an edge or a prior placement) — nothing to add.
	if (FindLiveNode(InTag) || Asset->TagNodePositions.Contains(InTag))
	{
		return;
	}

	// Place near the top-left of the visible viewport.
	FVector2D NewPos(100.0, 100.0);
	if (GraphEditorWidget.IsValid())
	{
		FVector2f ViewLocation = FVector2f::ZeroVector;
		float ZoomAmount = 1.0f;
		ClashGraphPanel_GetViewLocation(GraphEditorWidget, ViewLocation, ZoomAmount);
		NewPos = FVector2D(ViewLocation.X + 80.0f, ViewLocation.Y + 80.0f);
	}

	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);
		BeginTransaction(LOCTEXT("AddClashCategory", "Add Clash Category"));
		Asset->TagNodePositions.Add(InTag, NewPos);
		EndTransaction();
		// No LastProjection update (force the rebuild to spawn the new node).
	}

	RequestReconcile();
}

// =====================================================================================================
// HELPERS
// =====================================================================================================

UPaper2DPlusClashGraphAsset* SClashGraphPanel::GetAsset() const
{
	return Toolkit ? Toolkit->GetEditedAsset() : nullptr;
}

UPaper2DPlusClashGraphNode_Tag* SClashGraphPanel::FindLiveNode(const FGameplayTag& InTag) const
{
	if (!GraphObj || !InTag.IsValid())
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		if (UPaper2DPlusClashGraphNode_Tag* TagNode = Cast<UPaper2DPlusClashGraphNode_Tag>(Node))
		{
			if (TagNode->CategoryTag == InTag)
			{
				return TagNode;
			}
		}
	}
	return nullptr;
}

Paper2DPlusClashGraph::FClashProjection SClashGraphPanel::ProjectCurrent(const UPaper2DPlusClashGraphAsset* Asset) const
{
	if (!Asset)
	{
		return Paper2DPlusClashGraph::FClashProjection();
	}
	return Paper2DPlusClashGraph::ProjectGraph(Asset->Graph, &Asset->TagNodePositions);
}

FGraphAppearanceInfo SClashGraphPanel::GetGraphAppearance() const
{
	FGraphAppearanceInfo Appearance;
	if (GraphObj && GraphObj->Nodes.Num() == 0)
	{
		Appearance.InstructionText = LOCTEXT("ClashGraphEmptyInstruction",
			"Use '+ Add Category' to place clash categories, then drag from a node's right edge to another to author 'beats'.");
	}
	return Appearance;
}

bool SClashGraphPanel::RestoreViewFromConfig()
{
	if (!GConfig || !GraphEditorWidget.IsValid())
	{
		return false;
	}
	float Zoom = 1.0f;
	float ViewX = 0.0f;
	float ViewY = 0.0f;
	const bool bHasZoom = GConfig->GetFloat(ClashGraphPanel_ConfigSection, ClashGraphPanel_ConfigKeyZoom, Zoom, GEditorPerProjectIni);
	const bool bHasX = GConfig->GetFloat(ClashGraphPanel_ConfigSection, ClashGraphPanel_ConfigKeyViewX, ViewX, GEditorPerProjectIni);
	const bool bHasY = GConfig->GetFloat(ClashGraphPanel_ConfigSection, ClashGraphPanel_ConfigKeyViewY, ViewY, GEditorPerProjectIni);
	if (bHasZoom && bHasX && bHasY)
	{
		ClashGraphPanel_SetViewLocation(GraphEditorWidget, FVector2f(ViewX, ViewY), Zoom);
		return true;
	}
	return false;
}

void SClashGraphPanel::SaveViewToConfig() const
{
	if (!GConfig || !GraphEditorWidget.IsValid())
	{
		return;
	}
	FVector2f ViewLocation = FVector2f::ZeroVector;
	float ZoomAmount = 1.0f;
	ClashGraphPanel_GetViewLocation(GraphEditorWidget, ViewLocation, ZoomAmount);
	GConfig->SetFloat(ClashGraphPanel_ConfigSection, ClashGraphPanel_ConfigKeyZoom, ZoomAmount, GEditorPerProjectIni);
	GConfig->SetFloat(ClashGraphPanel_ConfigSection, ClashGraphPanel_ConfigKeyViewX, ViewLocation.X, GEditorPerProjectIni);
	GConfig->SetFloat(ClashGraphPanel_ConfigSection, ClashGraphPanel_ConfigKeyViewY, ViewLocation.Y, GEditorPerProjectIni);
	// No explicit Flush (legacy-cleanup 2026-07): GConfig holds the value; the engine flushes
	// GEditorPerProjectIni itself. A manual Flush rewrites the whole multi-hundred-KB ini via
	// tmp+MoveFile and stalled ~8s per call under file-lock contention - the slow-close bug.
}

// =====================================================================================================
// PROBE (Paper2DPlus.ClashGraphProbe — greppable headless/ECABridge verification)
// =====================================================================================================

void SClashGraphPanel::LogProbe() const
{
	if (!GraphObj)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClashGraph] Probe: no graph object"));
		return;
	}

	TArray<UPaper2DPlusClashGraphNode_Tag*> Nodes;
	int32 EdgeCount = 0;
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		if (UPaper2DPlusClashGraphNode_Tag* TagNode = Cast<UPaper2DPlusClashGraphNode_Tag>(Node))
		{
			Nodes.Add(TagNode);
			if (const UEdGraphPin* OutputPin = TagNode->GetOutputPin())
			{
				EdgeCount += OutputPin->LinkedTo.Num();
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[ClashGraph] Probe: nodes=%d edges=%d"), Nodes.Num(), EdgeCount);
	for (const UPaper2DPlusClashGraphNode_Tag* TagNode : Nodes)
	{
		UE_LOG(LogTemp, Display, TEXT("[ClashGraph]   node %s@%d,%d"),
			*TagNode->GetShortName(), TagNode->NodePosX, TagNode->NodePosY);
	}
	for (const UPaper2DPlusClashGraphNode_Tag* TagNode : Nodes)
	{
		const UEdGraphPin* OutputPin = TagNode->GetOutputPin();
		if (!OutputPin)
		{
			continue;
		}
		for (const UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
		{
			const UPaper2DPlusClashGraphNode_Tag* LoserNode =
				LinkedPin ? Cast<UPaper2DPlusClashGraphNode_Tag>(LinkedPin->GetOwningNodeUnchecked()) : nullptr;
			UE_LOG(LogTemp, Display, TEXT("[ClashGraph]   edge %s -> %s"),
				*ClashGraphPanel_ShortName(TagNode->CategoryTag),
				LoserNode ? *ClashGraphPanel_ShortName(LoserNode->CategoryTag) : TEXT("<?>"));
		}
	}
}

#undef LOCTEXT_NAMESPACE
