// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileToolPanelHost.h"

#include "Misc/ConfigCacheIni.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "ProfileToolPanelHost"

namespace
{
	FString EmptyStateToString(EProfileToolPanelHostEmptyState State)
	{
		switch (State)
		{
		case EProfileToolPanelHostEmptyState::None: return TEXT("None");
		case EProfileToolPanelHostEmptyState::NoActiveTool: return TEXT("NoActiveTool");
		case EProfileToolPanelHostEmptyState::NoProvider: return TEXT("NoProvider");
		case EProfileToolPanelHostEmptyState::NoPanels: return TEXT("NoPanels");
		case EProfileToolPanelHostEmptyState::IncompatibleProvider: return TEXT("IncompatibleProvider");
		case EProfileToolPanelHostEmptyState::ProviderExpired: return TEXT("ProviderExpired");
		default: return TEXT("Unknown");
		}
	}

	FString SanitizeConfigToken(const FString& InValue)
	{
		FString Result = InValue;
		for (TCHAR& Character : Result)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
			{
				Character = TEXT('_');
			}
		}
		return Result.IsEmpty() ? TEXT("None") : Result;
	}

	int32 ProfileToolPanelHost_CountDescendantWidgetsByType(SWidget& Widget, FName WidgetType)
	{
		int32 Count = Widget.GetType() == WidgetType ? 1 : 0;
		if (FChildren* Children = Widget.GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				Count += ProfileToolPanelHost_CountDescendantWidgetsByType(
					Children->GetChildAt(Index).Get(), WidgetType);
			}
		}
		return Count;
	}
}

void SProfileToolPanelHost::Construct(const FArguments& InArgs)
{
	LayoutScope = InArgs._LayoutScope.IsNone() ? FName(TEXT("Profile")) : InArgs._LayoutScope;
	LayoutConfigPath = InArgs._LayoutConfigPath.IsEmpty() ? GEditorLayoutIni : InArgs._LayoutConfigPath;
	EnsureLayoutConfigBranch();

	ChildSlot
	[
		SAssignNew(ContentBox, SBox)
	];

	SetEmptyState(EProfileToolPanelHostEmptyState::NoActiveTool);
}

SProfileToolPanelHost::~SProfileToolPanelHost()
{
	Shutdown();
}

void SProfileToolPanelHost::Shutdown()
{
	if (bShutdown)
	{
		return;
	}
	bShutdown = true;
	if (TSharedPtr<FActiveTimerHandle> Timer = DeferredSwitchTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	DeferredSwitchTimerHandle.Reset();
	StopDeferredSectionScroll();
	StopLivenessTimer();
	TearDownContextSections(true);
	ActiveProvider.Reset();
	PendingProvider.Reset();
	bActiveProviderWasSupplied = false;
	bPendingProviderWasSupplied = false;
	bSwitchPending = false;
	PendingForegroundPanelId = NAME_None;
}

void SProfileToolPanelHost::RequestActiveTool(
	FName ToolId,
	TSharedPtr<IProfileToolPanelProvider> Provider)
{
	if (bShutdown)
	{
		return;
	}
	const TSharedPtr<IProfileToolPanelProvider> CurrentProvider = ActiveProvider.Pin();
	const TSharedPtr<IProfileToolPanelProvider> QueuedProvider = PendingProvider.Pin();
	if (!bSwitchPending
		&& ToolId == ActiveToolId
		&& Provider == CurrentProvider
		&& Provider.IsValid() == bActiveProviderWasSupplied)
	{
		return;
	}
	if (bSwitchPending
		&& ToolId == PendingToolId
		&& Provider == QueuedProvider
		&& Provider.IsValid() == bPendingProviderWasSupplied)
	{
		return;
	}
	if (bSwitchPending)
	{
		// A focus request queued for the previous target must not leak into a later switch that
		// supersedes it before the deferred rebuild runs.
		PendingForegroundPanelId = NAME_None;
	}

	PendingToolId = ToolId;
	PendingProvider = Provider;
	bPendingProviderWasSupplied = Provider.IsValid();
	bSwitchPending = true;

	if (!DeferredSwitchTimerHandle.IsValid())
	{
		DeferredSwitchTimerHandle = RegisterActiveTimer(
			0.0f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SProfileToolPanelHost::HandleDeferredToolSwitch));
	}
}

bool SProfileToolPanelHost::ApplyPendingSwitchForTests()
{
	if (TSharedPtr<FActiveTimerHandle> Timer = DeferredSwitchTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	DeferredSwitchTimerHandle.Reset();
	return ApplyPendingSwitch();
}

bool SProfileToolPanelHost::PollProviderLivenessForTests()
{
	if (!bActiveProviderWasSupplied || ActiveProvider.IsValid())
	{
		return false;
	}

	TearDownContextSections(true);
	ActiveProvider.Reset();
	bActiveProviderWasSupplied = false;
	SetEmptyState(EProfileToolPanelHostEmptyState::ProviderExpired);
	StopLivenessTimer();
	return true;
}

bool SProfileToolPanelHost::ExpandSectionForTests(FName PanelId)
{
	return ForegroundPanel(PanelId);
}

bool SProfileToolPanelHost::ForegroundPanel(FName PanelId)
{
	if (bSwitchPending && !PanelId.IsNone())
	{
		// The category tree still belongs to the old tool until the deferred switch runs. Carry the
		// requested id into the incoming provider even when both tools happen to reuse that id.
		PendingForegroundPanelId = PanelId;
		return true;
	}
	if (TSharedPtr<SExpandableArea>* Section = PanelSections.Find(PanelId))
	{
		if (Section->IsValid())
		{
			if (!(*Section)->IsExpanded())
			{
				(*Section)->SetExpanded(true);
			}
			ScheduleSectionScrollIntoView(*Section);
			PendingForegroundPanelId = NAME_None;
			return true;
		}
	}
	return false;
}

bool SProfileToolPanelHost::CollapseSectionForTests(FName PanelId)
{
	TSharedPtr<SExpandableArea>* Section = PanelSections.Find(PanelId);
	if (!Section || !Section->IsValid())
	{
		return false;
	}
	(*Section)->SetExpanded(false);
	return true;
}

bool SProfileToolPanelHost::IsSectionExpandedForTests(FName PanelId) const
{
	const TSharedPtr<SExpandableArea>* Section = PanelSections.Find(PanelId);
	return Section && Section->IsValid() && (*Section)->IsExpanded();
}

int32 SProfileToolPanelHost::CountSectionDescendantWidgetsForTests(
	FName PanelId,
	FName WidgetType) const
{
	const TSharedPtr<SExpandableArea>* Section = PanelSections.Find(PanelId);
	return Section && Section->IsValid()
		? ProfileToolPanelHost_CountDescendantWidgetsByType(
			(*Section).ToSharedRef().Get(), WidgetType)
		: 0;
}

void SProfileToolPanelHost::PersistExpansionStateForTests()
{
	PersistCurrentExpansionState();
}

FString SProfileToolPanelHost::GetExpansionStateStringForTests() const
{
	TArray<FString> SectionStates;
	SectionStates.Reserve(RegisteredPanelIds.Num());
	for (const FName PanelId : RegisteredPanelIds)
	{
		SectionStates.Add(FString::Printf(
			TEXT("%s:%s"),
			*PanelId.ToString(),
			IsSectionExpandedForTests(PanelId) ? TEXT("Expanded") : TEXT("Collapsed")));
	}
	return FString::Join(SectionStates, TEXT(","));
}

FString SProfileToolPanelHost::BuildDiagnosticString() const
{
	return FString::Printf(
		TEXT("Tool=%s StateSection=%s State=%s DetailsSections=[%s]"),
		*ActiveToolId.ToString(),
		*ActiveStateSection.ToString(),
		*EmptyStateToString(EmptyState),
		*GetExpansionStateStringForTests());
}

EActiveTimerReturnType SProfileToolPanelHost::HandleDeferredToolSwitch(
	double CurrentTime,
	float DeltaTime)
{
	DeferredSwitchTimerHandle.Reset();
	ApplyPendingSwitch();
	return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType SProfileToolPanelHost::HandleDeferredSectionScroll(
	double CurrentTime,
	float DeltaTime)
{
	DeferredSectionScrollTimerHandle.Reset();
	const TSharedPtr<SExpandableArea> Section = PendingScrollSection.Pin();
	PendingScrollSection.Reset();
	if (SectionsScrollBox.IsValid() && Section.IsValid())
	{
		SectionsScrollBox->ScrollDescendantIntoView(
			Section,
			false,
			EDescendantScrollDestination::IntoView,
			4.0f);
	}
	return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType SProfileToolPanelHost::HandleProviderLivenessPoll(
	double CurrentTime,
	float DeltaTime)
{
	if (bActiveProviderWasSupplied && !ActiveProvider.IsValid())
	{
		// This callback owns the currently executing handle. Drop our weak reference before the shared
		// expiry funnel runs so it does not unregister an active timer from inside its own invocation.
		LivenessTimerHandle.Reset();
		PollProviderLivenessForTests();
		return EActiveTimerReturnType::Stop;
	}
	return bActiveProviderWasSupplied
		? EActiveTimerReturnType::Continue
		: EActiveTimerReturnType::Stop;
}

bool SProfileToolPanelHost::ApplyPendingSwitch()
{
	if (!bSwitchPending)
	{
		return false;
	}

	TearDownContextSections(true);
	StopLivenessTimer();

	ActiveToolId = PendingToolId;
	ActiveProvider = PendingProvider;
	bActiveProviderWasSupplied = bPendingProviderWasSupplied;
	PendingToolId = NAME_None;
	PendingProvider.Reset();
	bPendingProviderWasSupplied = false;
	bSwitchPending = false;
	++AppliedToolChangeCount;

	RebuildForActiveProvider();
	return true;
}

void SProfileToolPanelHost::RebuildForActiveProvider()
{
	if (ActiveToolId.IsNone())
	{
		SetEmptyState(EProfileToolPanelHostEmptyState::NoActiveTool);
		return;
	}

	const TSharedPtr<IProfileToolPanelProvider> Provider = ActiveProvider.Pin();
	if (!Provider.IsValid())
	{
		SetEmptyState(
			bActiveProviderWasSupplied
				? EProfileToolPanelHostEmptyState::ProviderExpired
				: EProfileToolPanelHostEmptyState::NoProvider);
		bActiveProviderWasSupplied = false;
		return;
	}
	const FProfileToolPanelHostContract HostContract = Provider->GetHostContract();
	if (!HostContract.IsValid() || !HostContract.UsesExternalNavigation())
	{
		SetEmptyState(EProfileToolPanelHostEmptyState::IncompatibleProvider);
		EnsureLivenessTimer();
		return;
	}

	TArray<FProfileToolPanelDescriptor> SuppliedPanels;
	Provider->GetContextualPanels(SuppliedPanels);
	TArray<FProfileToolPanelDescriptor> AvailablePanels;
	AvailablePanels.Reserve(SuppliedPanels.Num());
	for (FProfileToolPanelDescriptor& Descriptor : SuppliedPanels)
	{
		if (!Descriptor.IsAvailableNow()
			|| RegisteredPanelIds.Contains(Descriptor.PanelId))
		{
			continue;
		}
		RegisteredPanelIds.Add(Descriptor.PanelId);
		AvailablePanels.Add(MoveTemp(Descriptor));
	}
	if (AvailablePanels.IsEmpty())
	{
		SetEmptyState(EProfileToolPanelHostEmptyState::NoPanels);
		EnsureLivenessTimer();
		return;
	}

	ActiveStateSection = MakeStateSection(ActiveToolId);
	const TSharedRef<SScrollBox> NewScrollBox = SNew(SScrollBox)
		.Orientation(Orient_Vertical);
	SectionsScrollBox = NewScrollBox;
	for (const FProfileToolPanelDescriptor& Descriptor : AvailablePanels)
	{
		const FName PanelId = Descriptor.PanelId;
		TSharedPtr<SExpandableArea> Section;
		NewScrollBox->AddSlot()
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 1.0f))
		[
			SAssignNew(Section, SExpandableArea)
			.AllowAnimatedTransition(false)
			.InitiallyCollapsed(!LoadSectionExpansionState(PanelId))
			.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryTop"))
			.BodyBorderImage(FStyleDefaults::GetNoBrush())
			.HeaderPadding(FMargin(6.0f, 3.0f))
			.Padding(FMargin(6.0f, 4.0f, 4.0f, 8.0f))
			.OnAreaExpansionChanged(FOnBooleanValueChanged::CreateSP(
				this,
				&SProfileToolPanelHost::HandleSectionExpansionChanged,
				PanelId))
			.HeaderContent()
			[
				BuildSectionHeader(Descriptor)
			]
			.BodyContent()
			[
				BuildSectionBody(Descriptor)
			]
		];
		PanelSections.Add(PanelId, Section);
	}

	ContentBox->SetContent(NewScrollBox);
	EmptyState = EProfileToolPanelHostEmptyState::None;
	EnsureLivenessTimer();
	if (!PendingForegroundPanelId.IsNone())
	{
		const FName PanelToForeground = PendingForegroundPanelId;
		PendingForegroundPanelId = NAME_None;
		ForegroundPanel(PanelToForeground);
	}
}

void SProfileToolPanelHost::TearDownContextSections(bool bPersistExpansionState)
{
	StopDeferredSectionScroll();
	if (bPersistExpansionState)
	{
		PersistCurrentExpansionState();
	}
	if (ContentBox.IsValid())
	{
		ContentBox->SetContent(SNullWidget::NullWidget);
	}
	SectionsScrollBox.Reset();
	PanelSections.Reset();
	RegisteredPanelIds.Reset();
	ActiveStateSection = NAME_None;
}

void SProfileToolPanelHost::PersistCurrentExpansionState()
{
	for (const FName PanelId : RegisteredPanelIds)
	{
		const TSharedPtr<SExpandableArea>* Section = PanelSections.Find(PanelId);
		if (Section && Section->IsValid())
		{
			SaveSectionExpansionState(PanelId, (*Section)->IsExpanded());
		}
	}
}

void SProfileToolPanelHost::EnsureLayoutConfigBranch() const
{
	// GConfig only honors Set/Get for a config file it already tracks, and it only starts tracking a
	// path that already exists on disk. A layout ini that has never been written therefore swallows
	// every expansion write AND every read, so per-tool Details expansion silently stops persisting.
	// Materialize the branch once for whatever layout path this host was configured with.
	if (!GConfig || LayoutConfigPath.IsEmpty() || GConfig->FindConfigFile(LayoutConfigPath) != nullptr)
	{
		return;
	}
	const FConfigFile EmptyLayoutConfig;
	GConfig->LoadFile(LayoutConfigPath, &EmptyLayoutConfig);
}

void SProfileToolPanelHost::SaveSectionExpansionState(FName PanelId, bool bExpanded)
{
	if (!GConfig || ActiveStateSection.IsNone() || PanelId.IsNone())
	{
		return;
	}
	const FString StateSection = ActiveStateSection.ToString();
	const FString StateKey = PanelId.ToString();
	GConfig->SetBool(*StateSection, *StateKey, bExpanded, LayoutConfigPath);
}

bool SProfileToolPanelHost::LoadSectionExpansionState(FName PanelId) const
{
	bool bExpanded = true;
	if (GConfig && !ActiveStateSection.IsNone() && !PanelId.IsNone())
	{
		const FString StateSection = ActiveStateSection.ToString();
		const FString StateKey = PanelId.ToString();
		GConfig->GetBool(*StateSection, *StateKey, bExpanded, LayoutConfigPath);
	}
	return bExpanded;
}

void SProfileToolPanelHost::HandleSectionExpansionChanged(bool bExpanded, FName PanelId)
{
	SaveSectionExpansionState(PanelId, bExpanded);
}

void SProfileToolPanelHost::ScheduleSectionScrollIntoView(
	const TSharedPtr<SExpandableArea>& Section)
{
	StopDeferredSectionScroll();
	if (!Section.IsValid())
	{
		return;
	}
	PendingScrollSection = Section;
	DeferredSectionScrollTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this,
			&SProfileToolPanelHost::HandleDeferredSectionScroll));
}

void SProfileToolPanelHost::StopDeferredSectionScroll()
{
	if (TSharedPtr<FActiveTimerHandle> Timer = DeferredSectionScrollTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	DeferredSectionScrollTimerHandle.Reset();
	PendingScrollSection.Reset();
}

void SProfileToolPanelHost::SetEmptyState(EProfileToolPanelHostEmptyState NewState)
{
	EmptyState = NewState;
	SectionsScrollBox.Reset();
	PanelSections.Reset();
	if (!ContentBox.IsValid())
	{
		return;
	}
	ContentBox->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(this, &SProfileToolPanelHost::GetEmptyStateTitle)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SProfileToolPanelHost::GetEmptyStateDescription)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]);
}

void SProfileToolPanelHost::EnsureLivenessTimer()
{
	if (bActiveProviderWasSupplied && !LivenessTimerHandle.IsValid())
	{
		LivenessTimerHandle = RegisterActiveTimer(
			0.5f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SProfileToolPanelHost::HandleProviderLivenessPoll));
	}
}

void SProfileToolPanelHost::StopLivenessTimer()
{
	if (TSharedPtr<FActiveTimerHandle> Timer = LivenessTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	LivenessTimerHandle.Reset();
}

TSharedRef<SWidget> SProfileToolPanelHost::BuildSectionHeader(
	const FProfileToolPanelDescriptor& Descriptor) const
{
	return SNew(SBox)
		.MinDesiredHeight(22.0f)
		.VAlign(VAlign_Center)
		.ToolTipText(Descriptor.ToolTip)
		[
			SNew(STextBlock)
			.Text(Descriptor.Label)
			.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
		];
}

TSharedRef<SWidget> SProfileToolPanelHost::BuildSectionBody(
	const FProfileToolPanelDescriptor& Descriptor) const
{
	const TSharedPtr<SWidget> PanelWidget = ActiveProvider.IsValid()
		? Descriptor.TryCreateWidget()
		: nullptr;
	return PanelWidget.IsValid()
		? PanelWidget.ToSharedRef()
		: StaticCastSharedRef<SWidget>(
			SNew(SBorder)
			.Padding(FMargin(8.0f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("UnavailableContextPanel", "This section is no longer available for the active tool."))
				.AutoWrapText(true)
			]);
}

FName SProfileToolPanelHost::MakeStateSection(FName ToolId) const
{
	return FName(*FString::Printf(
		TEXT("Paper2DPlus.ProfileToolDetails.%s.%s.v1"),
		*SanitizeConfigToken(LayoutScope.ToString()),
		*SanitizeConfigToken(ToolId.ToString())));
}

FText SProfileToolPanelHost::GetEmptyStateTitle() const
{
	return ActiveToolId.IsNone()
		? LOCTEXT("NoActiveToolTitle", "Details")
		: FText::Format(LOCTEXT("ToolDetailsTitle", "{0} details"), FText::FromName(ActiveToolId));
}

FText SProfileToolPanelHost::GetEmptyStateDescription() const
{
	switch (EmptyState)
	{
	case EProfileToolPanelHostEmptyState::NoActiveTool:
		return LOCTEXT("NoActiveToolDescription", "Choose a tool to show its contextual details here.");
	case EProfileToolPanelHostEmptyState::NoProvider:
		return LOCTEXT("NoProviderDescription", "This tool does not expose contextual details yet.");
	case EProfileToolPanelHostEmptyState::NoPanels:
		return LOCTEXT("NoPanelsDescription", "No contextual details are available for the current selection.");
	case EProfileToolPanelHostEmptyState::IncompatibleProvider:
		return LOCTEXT("IncompatibleProviderDescription", "This provider owns an embedded layout and cannot also be hosted in the Details panel.");
	case EProfileToolPanelHostEmptyState::ProviderExpired:
		return LOCTEXT("ProviderExpiredDescription", "The active tool closed. Choose another tool to restore its details.");
	default:
		return FText::GetEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
