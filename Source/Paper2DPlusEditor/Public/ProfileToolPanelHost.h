// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfileToolPanelProvider.h"
#include "Widgets/SCompoundWidget.h"

class FActiveTimerHandle;
class SBox;
class SDockTab;
class SExpandableArea;
class SScrollBox;

/** Why the contextual region is currently showing its explicit empty state. */
enum class EProfileToolPanelHostEmptyState : uint8
{
	None,
	NoActiveTool,
	NoProvider,
	NoPanels,
	IncompatibleProvider,
	ProviderExpired,
};

/**
 * Details-style host for the active Profile tool's contextual panels.
 *
 * Tool activation only queues a switch. The old per-tool category state is saved and rebuilt from a
 * deferred active timer, outside the outer SDockTab activation callback. Every tool gets a distinct
 * expansion-state section, so collapsing one tool's categories cannot rearrange another tool's Details view.
 */
class PAPER2DPLUSEDITOR_API SProfileToolPanelHost final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProfileToolPanelHost)
		: _LayoutScope(TEXT("Profile"))
	{}
		SLATE_ARGUMENT(TSharedPtr<SDockTab>, OwnerTab)
		SLATE_ARGUMENT(FName, LayoutScope)
		/** Empty uses GEditorLayoutIni. Tests inject an isolated config filename. */
		SLATE_ARGUMENT(FString, LayoutConfigPath)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SProfileToolPanelHost() override;

	/** Queue an idempotent active-tool change for deferred structural application. */
	void RequestActiveTool(FName ToolId, TSharedPtr<IProfileToolPanelProvider> Provider);
	/** Expand and reveal one category belonging to the active provider. */
	bool ForegroundPanel(FName PanelId);
	/** Idempotent early teardown used before an owning toolkit releases its model/providers. */
	void Shutdown();

	/** Read-only diagnostics and headless acceptance-test seams. */
	FName GetActiveToolId() const { return ActiveToolId; }
	FName GetActiveStateSection() const { return ActiveStateSection; }
	EProfileToolPanelHostEmptyState GetEmptyState() const { return EmptyState; }
	int32 GetAppliedToolChangeCountForTests() const { return AppliedToolChangeCount; }
	TArray<FName> GetRegisteredPanelIdsForTests() const { return RegisteredPanelIds; }
	bool HasDetailsSectionsForTests() const { return SectionsScrollBox.IsValid(); }
	bool ApplyPendingSwitchForTests();
	bool PollProviderLivenessForTests();
	bool ExpandSectionForTests(FName PanelId);
	bool CollapseSectionForTests(FName PanelId);
	bool IsSectionExpandedForTests(FName PanelId) const;
	int32 CountSectionDescendantWidgetsForTests(FName PanelId, FName WidgetType) const;
	void PersistExpansionStateForTests();
	FString GetExpansionStateStringForTests() const;
	FString BuildDiagnosticString() const;

private:
	EActiveTimerReturnType HandleDeferredToolSwitch(double CurrentTime, float DeltaTime);
	EActiveTimerReturnType HandleDeferredSectionScroll(double CurrentTime, float DeltaTime);
	EActiveTimerReturnType HandleProviderLivenessPoll(double CurrentTime, float DeltaTime);
	bool ApplyPendingSwitch();
	void RebuildForActiveProvider();
	void TearDownContextSections(bool bPersistExpansionState);
	void PersistCurrentExpansionState();
	void EnsureLayoutConfigBranch() const;
	void SaveSectionExpansionState(FName PanelId, bool bExpanded);
	bool LoadSectionExpansionState(FName PanelId) const;
	void HandleSectionExpansionChanged(bool bExpanded, FName PanelId);
	void ScheduleSectionScrollIntoView(const TSharedPtr<SExpandableArea>& Section);
	void StopDeferredSectionScroll();
	void SetEmptyState(EProfileToolPanelHostEmptyState NewState);
	void EnsureLivenessTimer();
	void StopLivenessTimer();
	TSharedRef<SWidget> BuildSectionHeader(const FProfileToolPanelDescriptor& Descriptor) const;
	TSharedRef<SWidget> BuildSectionBody(const FProfileToolPanelDescriptor& Descriptor) const;
	FName MakeStateSection(FName ToolId) const;
	FText GetEmptyStateTitle() const;
	FText GetEmptyStateDescription() const;

	FName LayoutScope;
	FString LayoutConfigPath;
	TSharedPtr<SBox> ContentBox;
	TSharedPtr<SScrollBox> SectionsScrollBox;

	TMap<FName, TSharedPtr<SExpandableArea>> PanelSections;
	TArray<FName> RegisteredPanelIds;
	FName ActiveToolId;
	FName ActiveStateSection;
	TWeakPtr<IProfileToolPanelProvider> ActiveProvider;
	bool bActiveProviderWasSupplied = false;

	FName PendingToolId;
	FName PendingForegroundPanelId;
	TWeakPtr<IProfileToolPanelProvider> PendingProvider;
	bool bPendingProviderWasSupplied = false;
	bool bSwitchPending = false;
	bool bShutdown = false;
	int32 AppliedToolChangeCount = 0;
	EProfileToolPanelHostEmptyState EmptyState = EProfileToolPanelHostEmptyState::NoActiveTool;
	TWeakPtr<FActiveTimerHandle> DeferredSwitchTimerHandle;
	TWeakPtr<FActiveTimerHandle> DeferredSectionScrollTimerHandle;
	TWeakPtr<SExpandableArea> PendingScrollSection;
	TWeakPtr<FActiveTimerHandle> LivenessTimerHandle;
};
