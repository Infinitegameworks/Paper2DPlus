// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfileToolPanelProvider.h"
#include "Widgets/SCompoundWidget.h"

class FCharacterProfileEditorModel;
class SWidgetSwitcher;

/** Left-pane view mode for the merged Animations tab. Grid + List share the flipbook browser (cards vs
 *  rows); Map is the Animation Map graph. */
enum class EAnimationsViewMode : uint8
{
	Grid,
	List,
	Map,
};

/**
 * SAnimationsPanel — the merged "Animations" tab host.
 *
 * Layout: a full-width two-slot SWidgetSwitcher and nothing else. The panel no longer owns a dedicated
 * [Grid | List | Map] row: view switching is one compact `View: <current>` combo that the toolkit hosts in
 * the SHARED Current Animation header (MakeHeaderViewControl()), so a simple workspace switch costs no
 * extra line. That control still funnels through SetViewMode, which stays the single view-state authority
 * for the header combo, the console aliases, the visual tour, and per-asset persistence.
 *  - Grid / List: the flipbook browser (SOverviewPanel with ShowDetails(false)) on switcher slot 0 — group
 *    headers, drag-to-group, the inline Animation/Phase tag chips. Grid vs List drives the browser's
 *    card-vs-row render via Model->SetFlipbookGroupGridView.
 *  - Map: the Animation Map graph (SAnimationMapPanel) on slot 1 — its in-graph details strip is retired;
 *    node selection drives Model->SetSelectedFlipbook and the Map follows OnFlipbookSelectionChanged.
 *
 * Both views share the one FCharacterProfileEditorModel, so a selection in either propagates to the
 * contextual Details / Transitions / Tags panels and the other tool tabs.
 */
class SAnimationsPanel : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnContextPanelRequested, FName);

	SLATE_BEGIN_ARGS(SAnimationsPanel)
		: _PersistViewMode(true)
	{}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		/** Tests and embedded probes may opt out of writing GEditorPerProjectIni. */
		SLATE_ARGUMENT(bool, PersistViewMode)
		/** Edge selection may foreground Transitions. Ordinary move selection never requests a panel. */
		SLATE_EVENT(FOnContextPanelRequested, OnContextPanelRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAnimationsPanel() override;

	/** Switch the active left-pane view. Also the seam the SwitchTab console aliases drive (P5). */
	void SetViewMode(EAnimationsViewMode InViewMode);
	EAnimationsViewMode GetViewMode() const { return ViewMode; }

	/** Build a FRESH compact `View: <current>` combo for ONE shared-header host.
	 *
	 *  The caller owns the returned widget and must not reparent or cache it — a Slate widget attached to a
	 *  second parent is a hard error, and the Animations tab may be respawned. Call it once per tab spawn,
	 *  immediately after the panel is constructed. The combo only reads GetViewMode() and calls SetViewMode,
	 *  so the existing state authority, config persistence, and console aliases are untouched. */
	TSharedRef<SWidget> MakeHeaderViewControl();

	/** Designer-facing name of a view. Shared by the header combo label and its menu entries so the control
	 *  and its menu can never disagree; also the read-only seam focused tests assert against. */
	static FText GetViewModeLabel(EAnimationsViewMode InViewMode);

	/** The live panel for console commands (Paper2DPlus.SwitchTab list/map). Mirrors the model's
	 *  GActiveEditorModel "last realized wins" convention; valid for the single-editor common case. */
	static TWeakPtr<SAnimationsPanel> GActiveAnimationsPanel;

private:
	TSharedPtr<FCharacterProfileEditorModel> Model;
	EAnimationsViewMode ViewMode = EAnimationsViewMode::Grid;
	TSharedPtr<SWidgetSwitcher> ViewSwitcher;
	FOnContextPanelRequested OnContextPanelRequested;
	FDelegateHandle ModelTransitionSelectionHandle;
	bool bPersistViewMode = true;

	int32 ViewIndex() const;
	TSharedRef<SWidget> BuildViewMenuContent();

	/** Per-asset List/Map view-mode persistence in GEditorPerProjectIni (the migration-prompt key recipe).
	 *  Restore runs once in Construct (before first paint); Save runs on every SetViewMode. */
	void RestoreViewModeFromConfig();
	void SaveViewModeToConfig() const;
	void HandleTransitionSelectionChanged();
};

/**
 * External contextual-panel provider for Animations. The provider retains only a weak model and each
 * descriptor constructs a fresh focused panel from current model state, so closing/reopening a child tab
 * never preserves stale row pointers or a second selection model.
 */
class PAPER2DPLUSEDITOR_API FAnimationsContextPanelProvider final : public IProfileToolPanelProvider
{
public:
	static const FName DetailsPanelId;
	static const FName TransitionsPanelId;
	static const FName TagsPanelId;

	explicit FAnimationsContextPanelProvider(TSharedPtr<FCharacterProfileEditorModel> InModel);

	virtual FProfileToolPanelHostContract GetHostContract() const override;
	virtual void GetContextualPanels(TArray<FProfileToolPanelDescriptor>& OutPanels) const override;

private:
	TWeakPtr<FCharacterProfileEditorModel> Model;
};
