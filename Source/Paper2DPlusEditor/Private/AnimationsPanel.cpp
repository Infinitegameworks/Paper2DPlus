// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationsPanel.h"

#include "OverviewPanel.h"
#include "ProfileDetailsPanel.h"
#include "AnimationMapPanel.h"
#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AnimationsPanel"

namespace
{
	const TCHAR* AnimationsPanel_ConfigSection = TEXT("Paper2DPlus.AnimationsTab");

	// Sanitize the asset path into an INI key (matches the migration-prompt key recipe so both stay legible).
	FString AnimationsPanel_ViewModeKey(const UObject* Asset)
	{
		FString AssetKey = Asset ? Asset->GetPathName() : TEXT("NoAsset");
		AssetKey.ReplaceInline(TEXT("/"), TEXT("_"));
		AssetKey.ReplaceInline(TEXT("."), TEXT("_"));
		AssetKey.ReplaceInline(TEXT(":"), TEXT("_"));
		AssetKey.ReplaceInline(TEXT(" "), TEXT("_"));
		return FString::Printf(TEXT("ViewMode_%s"), *AssetKey);
	}
}

TWeakPtr<SAnimationsPanel> SAnimationsPanel::GActiveAnimationsPanel;
const FName FAnimationsContextPanelProvider::DetailsPanelId(TEXT("Paper2DPlus.Animations.Details"));
const FName FAnimationsContextPanelProvider::TransitionsPanelId(TEXT("Paper2DPlus.Animations.Transitions"));
const FName FAnimationsContextPanelProvider::TagsPanelId(TEXT("Paper2DPlus.Animations.Tags"));

void SAnimationsPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	bPersistViewMode = InArgs._PersistViewMode;
	OnContextPanelRequested = InArgs._OnContextPanelRequested;

	// Restore the last-used view for THIS asset before the switcher first paints (ViewIndex() reads ViewMode).
	RestoreViewModeFromConfig();

	// The retired [Grid | List | Map] row consumed a whole line for one workspace switch. The switcher IS the
	// panel now; view selection lives in the shared Current Animation header (MakeHeaderViewControl).
	ChildSlot
	[
		// List/Grid/Map own the central canvas. Focused authoring controls live in the workspace's
		// contextual child docks, where designers may close, split, or float them independently.
		SAssignNew(ViewSwitcher, SWidgetSwitcher)
		.WidgetIndex_Lambda([this]() { return ViewIndex(); })

		// 0 = Grid/List: one browser, with rendering mode driven through the shared model.
		+ SWidgetSwitcher::Slot()
		[
			SNew(SOverviewPanel)
			.Model(Model)
			.ShowDetails(false)
		]

		// 1 = Map: the existing graph, using the same selection and mutation funnels.
		+ SWidgetSwitcher::Slot()
		[
			SNew(SAnimationMapPanel)
			.Model(Model)
		]
	];

	if (Model.IsValid())
	{
		ModelTransitionSelectionHandle = Model->OnTransitionSelectionChanged.AddSP(
			this, &SAnimationsPanel::HandleTransitionSelectionChanged);
	}

	GActiveAnimationsPanel = SharedThis(this);
}

SAnimationsPanel::~SAnimationsPanel()
{
	if (Model.IsValid())
	{
		Model->OnTransitionSelectionChanged.Remove(ModelTransitionSelectionHandle);
	}
	if (GActiveAnimationsPanel.Pin().Get() == this)
	{
		GActiveAnimationsPanel.Reset();
	}
}

void SAnimationsPanel::HandleTransitionSelectionChanged()
{
	// Only an edge is an explicit transition-authoring intent. Clearing the edge or selecting a move
	// must not steal whichever contextual tab the designer chose.
	if (Model.IsValid() && Model->HasSelectedTransition() && OnContextPanelRequested.IsBound())
	{
		OnContextPanelRequested.Execute(FAnimationsContextPanelProvider::TransitionsPanelId);
	}
}

int32 SAnimationsPanel::ViewIndex() const
{
	// Grid and List share switcher slot 0 (the flipbook browser; cards vs rows comes from
	// Model->IsFlipbookGroupGridView()). Map is slot 1.
	return (ViewMode == EAnimationsViewMode::Map) ? 1 : 0;
}

void SAnimationsPanel::SetViewMode(EAnimationsViewMode InViewMode)
{
	if (ViewMode == InViewMode)
	{
		return;
	}
	ViewMode = InViewMode;
	// Grid/List both live on switcher slot 0 — drive the browser's card-vs-row render via the model flag
	// (it broadcasts OnFlipbookGroupViewModeChanged so the browser refreshes). Map needs no flag.
	if (Model.IsValid() && InViewMode != EAnimationsViewMode::Map)
	{
		Model->SetFlipbookGroupGridView(InViewMode == EAnimationsViewMode::Grid);
	}
	if (ViewSwitcher.IsValid())
	{
		ViewSwitcher->SetActiveWidgetIndex(ViewIndex());
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	SaveViewModeToConfig();
}

void SAnimationsPanel::RestoreViewModeFromConfig()
{
	if (!bPersistViewMode || !GConfig || !Model.IsValid())
	{
		return;
	}
	FString Saved;
	if (GConfig->GetString(AnimationsPanel_ConfigSection, *AnimationsPanel_ViewModeKey(Model->GetAsset()), Saved, GEditorPerProjectIni))
	{
		if (Saved == TEXT("Map"))       { ViewMode = EAnimationsViewMode::Map; }
		else if (Saved == TEXT("List")) { ViewMode = EAnimationsViewMode::List; }
		else                            { ViewMode = EAnimationsViewMode::Grid; } // "Grid" + default
	}
	// Sync the browser's card-vs-row flag to the restored view BEFORE the browser is built in Construct, so
	// it paints in the right mode on first show. Map leaves the flag at its default (the browser is hidden).
	if (ViewMode != EAnimationsViewMode::Map)
	{
		Model->SetFlipbookGroupGridView(ViewMode == EAnimationsViewMode::Grid);
	}
}

void SAnimationsPanel::SaveViewModeToConfig() const
{
	if (!bPersistViewMode || !GConfig || !Model.IsValid())
	{
		return;
	}
	const TCHAR* Value =
		(ViewMode == EAnimationsViewMode::Map)  ? TEXT("Map")  :
		(ViewMode == EAnimationsViewMode::Grid) ? TEXT("Grid") : TEXT("List");
	GConfig->SetString(AnimationsPanel_ConfigSection, *AnimationsPanel_ViewModeKey(Model->GetAsset()), Value, GEditorPerProjectIni);
	// No explicit Flush (legacy-cleanup 2026-07): GConfig holds the value; the engine flushes
	// GEditorPerProjectIni itself. A manual Flush rewrites the whole multi-hundred-KB ini via
	// tmp+MoveFile and stalled ~8s per call under file-lock contention - the slow-close bug.
}

FText SAnimationsPanel::GetViewModeLabel(EAnimationsViewMode InViewMode)
{
	switch (InViewMode)
	{
	case EAnimationsViewMode::List: return LOCTEXT("AnimationsViewList", "List");
	case EAnimationsViewMode::Map:  return LOCTEXT("AnimationsViewMap", "Map");
	case EAnimationsViewMode::Grid:
	default:                        return LOCTEXT("AnimationsViewGrid", "Grid");
	}
}

TSharedRef<SWidget> SAnimationsPanel::BuildViewMenuContent()
{
	// Weak, because the menu outlives the click that opened it and the Animations tab may be closed while
	// the popup is up. A dead panel simply does nothing rather than reviving a destroyed switcher.
	const TWeakPtr<SAnimationsPanel> WeakSelf = SharedThis(this);
	FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	Menu.BeginSection(FName(TEXT("AnimationsView")), LOCTEXT("AnimationsViewSection", "View"));

	auto AddViewEntry = [&Menu, WeakSelf](EAnimationsViewMode Mode, const FText& ToolTip)
	{
		Menu.AddMenuEntry(
			GetViewModeLabel(Mode),
			ToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakSelf, Mode]()
				{
					if (const TSharedPtr<SAnimationsPanel> Panel = WeakSelf.Pin())
					{
						Panel->SetViewMode(Mode);
					}
				}),
				FCanExecuteAction(),
				// Radio check keeps the ACTIVE view identifiable inside the menu; the closed control's own
				// label keeps it identifiable without opening the menu at all.
				FIsActionChecked::CreateLambda([WeakSelf, Mode]()
				{
					const TSharedPtr<SAnimationsPanel> Panel = WeakSelf.Pin();
					return Panel.IsValid() && Panel->GetViewMode() == Mode;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	};

	AddViewEntry(EAnimationsViewMode::Grid,
		LOCTEXT("AnimationsViewGridTip", "Browse grouped animation cards while focused controls stay in the contextual panels."));
	AddViewEntry(EAnimationsViewMode::List,
		LOCTEXT("AnimationsViewListTip", "Browse grouped animation rows while focused controls stay in the contextual panels."));
	AddViewEntry(EAnimationsViewMode::Map,
		LOCTEXT("AnimationsViewMapTip", "The Animation Map graph: tag groups and per-move combo transitions."));

	Menu.EndSection();
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SAnimationsPanel::MakeHeaderViewControl()
{
	const TWeakPtr<SAnimationsPanel> WeakSelf = SharedThis(this);
	return SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
		.ContentPadding(FMargin(8.0f, 2.0f))
		.ToolTipText(LOCTEXT("AnimationsViewMenuTip", "Choose the Animations workspace view: Grid, List, or the Animation Map."))
		.AccessibleText(LOCTEXT("AnimationsViewMenuAccessible", "Animations workspace view"))
		.OnGetMenuContent(this, &SAnimationsPanel::BuildViewMenuContent)
		.ButtonContent()
		[
			// "View: <current>" states the active view without opening the menu (the compact-control rule:
			// collapsing a control must never hide which state it is in).
			SNew(STextBlock)
			.Text_Lambda([WeakSelf]()
			{
				const TSharedPtr<SAnimationsPanel> Panel = WeakSelf.Pin();
				return FText::Format(
					LOCTEXT("AnimationsViewMenuLabel", "View: {0}"),
					GetViewModeLabel(Panel.IsValid() ? Panel->GetViewMode() : EAnimationsViewMode::Grid));
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		];
}

FAnimationsContextPanelProvider::FAnimationsContextPanelProvider(
	TSharedPtr<FCharacterProfileEditorModel> InModel)
	: Model(InModel)
{
}

FProfileToolPanelHostContract FAnimationsContextPanelProvider::GetHostContract() const
{
	return FProfileToolPanelHostContract::External();
}

void FAnimationsContextPanelProvider::GetContextualPanels(
	TArray<FProfileToolPanelDescriptor>& OutPanels) const
{
	const TWeakPtr<FCharacterProfileEditorModel> WeakModel = Model;
	auto AddPanel = [&OutPanels, WeakModel](
		FName PanelId,
		const FText& Label,
		const FText& ToolTip,
		EProfileDetailsPaneMode PaneMode)
	{
		FProfileToolPanelDescriptor Descriptor;
		Descriptor.PanelId = PanelId;
		Descriptor.Label = Label;
		Descriptor.ToolTip = ToolTip;
		Descriptor.CapabilityId = PanelId;
		Descriptor.IsAvailable = [WeakModel]() { return WeakModel.IsValid(); };
		Descriptor.WidgetFactory = [WeakModel, PaneMode]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<FCharacterProfileEditorModel> PinnedModel = WeakModel.Pin();
			if (!PinnedModel.IsValid())
			{
				return SNullWidget::NullWidget;
			}
			return SNew(SProfileDetailsPanel)
				.Model(PinnedModel)
				.PaneMode(PaneMode);
		};
		OutPanels.Add(MoveTemp(Descriptor));
	};

	AddPanel(
		DetailsPanelId,
		LOCTEXT("AnimationsDetailsPanel", "Details"),
		LOCTEXT("AnimationsDetailsPanelTip", "Timing, combat metadata, and root-motion summary for the selected animation."),
		EProfileDetailsPaneMode::FlipbookDetails);
	AddPanel(
		TransitionsPanelId,
		LOCTEXT("AnimationsTransitionsPanel", "Transitions"),
		LOCTEXT("AnimationsTransitionsPanelTip", "Outgoing transitions or the selected Map edge, resolved from live profile data."),
		EProfileDetailsPaneMode::Transitions);
	AddPanel(
		TagsPanelId,
		LOCTEXT("AnimationsTagsPanel", "Tags"),
		LOCTEXT("AnimationsTagsPanelTip", "Animation category and phase tags, including inherited provenance."),
		EProfileDetailsPaneMode::Tags);
}

#undef LOCTEXT_NAMESPACE
