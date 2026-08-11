// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Widgets/SCompoundWidget.h"

struct FCharacterLayerBakeStatusSnapshot;

/** Cached, presentation-only projection of the deep bake status snapshot. */
struct PAPER2DPLUSEDITOR_API FCharacterLayerBakeStatusPresentation
{
	FText StatusText;
	FText SummaryText;
	FText DeliveryText;
	FText SelectionText;
	FText DiagnosticsText;
	FText LastReportText;
	FSlateColor StatusColor = FSlateColor::UseForeground();

	bool bBusy = false;
	bool bShowBakeCurrent = false;
	bool bShowBakeAll = false;
	bool bShowSaveBakeSet = false;
	bool bShowOverwrite = false;
	bool bShowRepair = false;
	bool bShowRebase = false;
	bool bShowDetach = false;
	bool bShowEnableRuntimeCustomization = false;
};

/**
 * Compact Layer Bake button and on-demand publishing menu. It never scans assets or computes hashes;
 * the owning toolkit pushes a cached presentation only at model/operation boundaries.
 */
class PAPER2DPLUSEDITOR_API SCharacterLayerBakeStatusWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCharacterLayerBakeStatusWidget) {}
		SLATE_ARGUMENT(FCharacterLayerBakeStatusPresentation, Presentation)
		SLATE_EVENT(FSimpleDelegate, OnBakeCurrent)
		SLATE_EVENT(FSimpleDelegate, OnBakeAll)
		SLATE_EVENT(FSimpleDelegate, OnSaveBakeSet)
		SLATE_EVENT(FSimpleDelegate, OnOverwrite)
		SLATE_EVENT(FSimpleDelegate, OnRepair)
		SLATE_EVENT(FSimpleDelegate, OnRebase)
		SLATE_EVENT(FSimpleDelegate, OnDetach)
		SLATE_EVENT(FSimpleDelegate, OnEnableRuntimeCustomization)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetPresentation(const FCharacterLayerBakeStatusPresentation& InPresentation);
	/** Stable exact-path projection shared by the widget and headless workflow tests. */
	static FText BuildDiagnosticsText(const FCharacterLayerBakeStatusSnapshot& Status);
	const FCharacterLayerBakeStatusPresentation& GetPresentationForTests() const { return Presentation; }

private:
	enum class EAction : uint8
	{
		None,
		BakeCurrent,
		BakeAll,
		SaveBakeSet,
		Overwrite,
		Repair,
		Rebase,
		Detach,
		EnableRuntimeCustomization
	};

	FText GetStatusText() const { return Presentation.StatusText; }
	FText GetSummaryText() const { return Presentation.SummaryText; }
	FText GetDeliveryText() const { return Presentation.DeliveryText; }
	FText GetSelectionText() const { return Presentation.SelectionText; }
	FText GetDiagnosticsText() const { return Presentation.DiagnosticsText; }
	FText GetLastReportText() const { return Presentation.LastReportText; }
	FSlateColor GetStatusColor() const { return Presentation.StatusColor; }
	EAction ResolvePrimaryAction() const;
	FText GetPrimaryActionText() const;
	FText GetPrimaryActionTooltip() const;
	TSharedRef<SWidget> BuildDetailsMenu();
	void ExecuteMenuAction(EAction Action);
	EVisibility GetDiagnosticsVisibility() const;
	EVisibility GetLastReportVisibility() const;
	bool AreActionsEnabled() const { return !Presentation.bBusy; }
	void Execute(const FSimpleDelegate& Delegate);

	FCharacterLayerBakeStatusPresentation Presentation;
	FSimpleDelegate OnBakeCurrent;
	FSimpleDelegate OnBakeAll;
	FSimpleDelegate OnSaveBakeSet;
	FSimpleDelegate OnOverwrite;
	FSimpleDelegate OnRepair;
	FSimpleDelegate OnRebase;
	FSimpleDelegate OnDetach;
	FSimpleDelegate OnEnableRuntimeCustomization;
};
