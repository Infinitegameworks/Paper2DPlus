// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/WeakObjectPtr.h"

class UPaper2DPlusClashGraphAsset;
class SGridPanel;

/**
 * TASK-77 U5 — the read-only computed outcome GRID: for every concrete category pair it runs the pure
 * Paper2DPlusClash::ResolveClash and renders the EClashOutcome as a color-coded cell, so a designer sees the
 * WHOLE resolved matrix the flat edge list produces (the validation/visualization companion to the node
 * graph). Conflict (authoring-desync) pairs from the validator get a red border. Rebuilt on the toolkit's
 * OnEditedAssetChanged signal + undo. Pure view — zero transactions, zero graph machinery.
 */
class PAPER2DPLUSEDITOR_API SClashOutcomeGrid : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SClashOutcomeGrid) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusClashGraphAsset>, Asset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Recompute + rebuild the grid from the current asset state. */
	void Rebuild();

private:
	TWeakObjectPtr<UPaper2DPlusClashGraphAsset> Asset;
	TSharedPtr<SGridPanel> GridPanel;
};
