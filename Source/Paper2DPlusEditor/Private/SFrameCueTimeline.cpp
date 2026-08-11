// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SFrameCueTimeline.h"

#include "FrameCueDataProvider.h"
#include "SFrameEventTimelineTrack.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace Paper2DPlusFrameCueTimeline
{
namespace
{
	int32 ComputeInclusiveEnd(const FPlacement& Placement)
	{
		const int64 SafeCount = FMath::Max<int64>(1, Placement.FrameCount);
		return static_cast<int32>(FMath::Clamp<int64>(
			static_cast<int64>(Placement.StartFrame) + SafeCount - 1,
			MIN_int32,
			MAX_int32));
	}
}
} // namespace Paper2DPlusFrameCueTimeline

#define LOCTEXT_NAMESPACE "SFrameCueTimeline"

float Paper2DPlusFrameCueTimeline::FGutterGeometry::ClampRequestedWidth(
	float RequestedWidth)
{
	return FMath::Clamp(RequestedWidth, MinimumWidth, MaximumWidth);
}

void SFrameCueTimeline::Construct(const FArguments& InArgs)
{
	DataProvider = InArgs._DataProvider;
	Asset = InArgs._Asset;
	CueSource = InArgs._Cues;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedCueIndex = InArgs._SelectedCueIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	RulerContent = InArgs._RulerContent;
	CurveContent = InArgs._CurveContent;
	CurveLegendContent = InArgs._CurveLegendContent;
	OnAddCue = InArgs._OnAddCue;
	GetAddCurveMenuContent = InArgs._GetAddCurveMenuContent;
	OnStructureChanged = InArgs._OnStructureChanged;
	OnPrimarySelectionChanged = InArgs._OnPrimarySelectionChanged;

	const TSharedRef<SWidget> SafeRuler = RulerContent.IsValid()
		? RulerContent.ToSharedRef()
		: SNullWidget::NullWidget;
	const TSharedRef<SWidget> SafeCurves = CurveContent.IsValid()
		? CurveContent.ToSharedRef()
		: StaticCastSharedRef<SWidget>(SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(8.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CurveRegionUnavailable", "Curve data is unavailable for this animation context."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(6.0f, 5.0f, 6.0f, 5.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 5.0f, 0.0f)
				[
					SAssignNew(AddCueButton, SButton)
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.IsEnabled_Lambda([this]() { return CanAddCue(); })
					.ToolTipText_Lambda([this]()
					{
						if (!CanAddCue())
						{
							return LOCTEXT(
								"AddCueUnavailableTip",
								"Choose an animation with at least one definitive key frame before placing a Cue.");
						}
						return HasNamedTracks()
							? FText::Format(
								LOCTEXT("AddCueToTrackTip", "Place a reusable Cue Type on {0}."),
								GetActiveTrackLabel())
							: LOCTEXT("AddCueTip", "Place a reusable Cue Type on the selected frame.");
					})
					.OnClicked(this, &SFrameCueTimeline::HandleAddCue)
					[
						SNew(STextBlock)
						// Named tracks are optional organization, so the target only earns space in the
						// primary action once the designer has actually created one.
						.Text_Lambda([this]()
						{
							return HasNamedTracks()
								? FText::Format(
									LOCTEXT("AddCueActiveTrack", "+ Add Cue  ·  {0}"),
									GetActiveTrackLabel())
								: LOCTEXT("AddCue", "+ Add Cue");
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 5.0f, 0.0f)
				[
					SNew(SComboButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.IsEnabled_Lambda([this]() { return CanEditCurves(); })
					.ToolTipText_Lambda([this]()
					{
						return CanEditCurves()
							? LOCTEXT("AddCurveTip", "Add a Profile-owned curve below the Cue tracks.")
							: LOCTEXT(
								"LayerCurveReadOnlyTip",
								"Curves belong to the Character Profile and are read-only from Layer scope.");
					})
					.OnGetMenuContent_Lambda([this]()
					{
						return GetAddCurveMenuContent
							? GetAddCurveMenuContent()
							: SNullWidget::NullWidget;
					})
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("AddCurve", "+ Add Curve"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNullWidget::NullWidget
				]
				// Named tracks are an inert editor-only organizational sidecar, so every track command —
				// create, rename, reorder, remove, and choosing the Add Cue target — lives behind this one
				// labeled overflow instead of spending a persistent button in the default chrome.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 5.0f, 0.0f)
				[
					SAssignNew(ManageTracksButton, SComboButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.IsEnabled_Lambda([this]() { return HasResolvedAnimation(); })
					.OnGetMenuContent(this, &SFrameCueTimeline::BuildManageTracksMenu)
					.ToolTipText(LOCTEXT(
						"ManageTracksTip",
						"Create, rename, reorder, or remove optional Cue tracks, and choose which one Add Cue "
						"targets. Tracks never change Cue timing or dispatch order."))
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("ManageTracks", "Manage tracks..."))
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(6.0f, 0.0f, 6.0f, 5.0f)
			[
				SAssignNew(MissingCueNotice, SBorder)
				.Visibility_Lambda([this]()
				{
					return GetMissingCuePlacementCount() > 0
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.38f, 0.22f, 0.04f, 1.0f))
				.Padding(FMargin(8.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() { return GetMissingCueNoticeText(); })
						.AutoWrapText(true)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.ToolTipText(LOCTEXT(
								"RemoveMissingCuePlacementsTip",
								"Remove only the empty placement slots from this animation. "
								"Valid Cues are preserved, and the action can be undone."))
							.OnClicked(
								this,
								&SFrameCueTimeline::HandleRemoveAllMissingCuePlacements)
							[
								SNew(STextBlock)
									.Text_Lambda([this]()
									{
										return FText::Format(
											LOCTEXT(
												"RemoveAllMissingCuePlacements",
												"Remove all ({0})"),
											FText::AsNumber(
												GetMissingCuePlacementCount()));
									})
							]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.MaxDesiredHeight(430.0f)
				[
					SAssignNew(VerticalScrollBox, SScrollBox)
					.Orientation(Orient_Vertical)
					+ SScrollBox::Slot()
					[
						SAssignNew(TimelineSplitter, SSplitter)
						.Orientation(Orient_Horizontal)
						.ResizeMode(ESplitterResizeMode::FixedPosition)
						+ SSplitter::Slot()
						.SizeRule(SSplitter::SizeToContent)
						.Resizable(true)
						.MinSize(
							Paper2DPlusFrameCueTimeline::FGutterGeometry::MinimumWidth)
						.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(
							this, &SFrameCueTimeline::HandleGutterSlotResized))
						[
							SNew(SBox)
							.WidthOverride_Lambda([this]() -> FOptionalSize
							{
								return FOptionalSize(RequestedGutterWidth);
							})
							[
								SAssignNew(TrackHeadersBox, SVerticalBox)
							]
						]
						+ SSplitter::Slot()
						.Value(1.0f)
						.MinSize(
							Paper2DPlusFrameCueTimeline::FGutterGeometry::MinimumBodyViewportWidth)
						[
							SAssignNew(HorizontalScrollBox, SScrollBox)
							.Orientation(Orient_Horizontal)
							+ SScrollBox::Slot()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SBox)
									.HeightOverride(RulerHeaderHeight)
									[
										SafeRuler
									]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SAssignNew(CueLane, SFrameEventTimelineTrack)
									.Asset(Asset)
									.DataProvider(DataProvider)
									.Cues(CueSource)
									.CharacterFrameCount_Lambda([this]() { return GetFrameCount(); })
									.SelectedFlipbookIndex(SelectedFlipbookIndex)
									.SelectedEventIndex(SelectedCueIndex)
									.SelectedFrameIndex(SelectedFrameIndex)
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 2.0f, 0.0f, 0.0f)
								[
									SNew(SBox)
									.MinDesiredHeight(72.0f)
									[
										SafeCurves
									]
								]
							]
						]
					]
				]
			]
		]
	];
	if (CueLane.IsValid())
	{
		CueLane->OnEdgeAutoscrollRequested.BindSP(
			this, &SFrameCueTimeline::HandleEdgeAutoscrollRequested);
	}

	RefreshTrackHeaders();
}

FProfileScopedAnimationIdentity SFrameCueTimeline::GetScopeIdentity() const
{
	return DataProvider.IsValid()
		? DataProvider->GetScopedAnimationIdentity(SelectedFlipbookIndex.Get(INDEX_NONE))
		: FProfileScopedAnimationIdentity();
}

bool SFrameCueTimeline::HasResolvedAnimation() const
{
	return DataProvider.IsValid() && DataProvider->HasResolvedScope() && GetScopeIdentity().IsValid();
}

int32 SFrameCueTimeline::GetFrameCount() const
{
	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	return DataProvider.IsValid() && Scope.IsValid()
		? FMath::Max(0, DataProvider->GetFrameCount(Scope))
		: 0;
}

bool SFrameCueTimeline::CanAddCue() const
{
	return HasResolvedAnimation() && GetFrameCount() > 0;
}

bool SFrameCueTimeline::CanEditCurves() const
{
	return HasResolvedAnimation() && DataProvider.IsValid() && DataProvider->SupportsCurveEditing();
}

int32 SFrameCueTimeline::GetMissingCuePlacementCount() const
{
	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	return DataProvider.IsValid() && Scope.IsValid()
		? DataProvider->GetMissingCuePlacementCount(Scope)
		: 0;
}

FText SFrameCueTimeline::GetMissingCueNoticeText() const
{
	const int32 MissingCount = GetMissingCuePlacementCount();
	if (MissingCount == 1)
	{
		return LOCTEXT(
			"MissingCuePlacementNoticeSingular",
			"This animation contains 1 missing Frame Cue placement. The empty slot has no Cue "
			"Type, timing, or payload; it may have been left unassigned or lost its Cue Type.");
	}
	return FText::Format(
		LOCTEXT(
			"MissingCuePlacementNoticePlural",
			"This animation contains {0} missing Frame Cue placements. These empty slots have no "
			"Cue Type, timing, or payload; they may have been left unassigned or lost their Cue "
			"Type."),
		FText::AsNumber(MissingCount));
}

bool SFrameCueTimeline::RemoveAllMissingCuePlacements()
{
	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	if (!DataProvider.IsValid() || !Scope.IsValid())
	{
		return false;
	}
	return RunTrackCommand(
		[this, Scope]()
		{
			return DataProvider->RemoveAllMissingCuePlacements(Scope) > 0;
		});
}

FReply SFrameCueTimeline::HandleRemoveAllMissingCuePlacements()
{
	RemoveAllMissingCuePlacements();
	return FReply::Handled();
}

const FPaper2DPlusFrameCueTrackLayout* SFrameCueTimeline::GetTrackLayoutForRead() const
{
	// GetOptionalTracks() returns the track array BY VALUE. Everything below runs from per-paint Slate
	// attribute bindings (+ Add Cue's label and tooltip, the Add-target line's visibility), so reading it
	// that way heap-allocated and copied the whole array on every paint just to look at Num() or one
	// display name. The layout pointer is the same public provider read with no copy; the provider's
	// contract is untouched, so the callers that genuinely want their own copy still get one.
	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	return DataProvider.IsValid() && Scope.IsValid() ? DataProvider->GetTrackLayout(Scope) : nullptr;
}

int32 SFrameCueTimeline::GetNamedTrackCount() const
{
	const FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayoutForRead();
	return Layout ? Layout->OptionalTracks.Num() : 0;
}

bool SFrameCueTimeline::HasNamedTracks() const
{
	return GetNamedTrackCount() > 0;
}

FText SFrameCueTimeline::GetActiveTrackLabel() const
{
	if (!ActiveTrackId.IsValid())
	{
		return LOCTEXT("DefaultTrack", "Default");
	}
	if (const FPaper2DPlusFrameCueTrackLayout* Layout = GetTrackLayoutForRead())
	{
		for (const FPaper2DPlusFrameCueTrackDefinition& Track : Layout->OptionalTracks)
		{
			if (Track.TrackId == ActiveTrackId)
			{
				return FText::FromString(Track.DisplayName);
			}
		}
	}
	return LOCTEXT("DefaultTrackFallback", "Default");
}

FReply SFrameCueTimeline::HandleAddCue()
{
	if (CanAddCue())
	{
		OnAddCue.ExecuteIfBound();
	}
	return FReply::Handled();
}

FReply SFrameCueTimeline::HandleAddTrack()
{
	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	if (!DataProvider.IsValid() || !Scope.IsValid())
	{
		return FReply::Handled();
	}
	FGuid NewTrackId;
	const FString SuggestedName = DataProvider->SuggestTrackName(Scope);
	RunTrackCommand(
		[this, Scope, SuggestedName, &NewTrackId]()
		{
			NewTrackId = DataProvider->AddTrack(Scope, SuggestedName);
			return NewTrackId.IsValid();
		});
	if (NewTrackId.IsValid())
	{
		SelectTrack(NewTrackId);
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SFrameCueTimeline::BuildManageTracksMenu()
{
	const TWeakPtr<SFrameCueTimeline> WeakSelf = SharedThis(this);
	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	const TArray<FPaper2DPlusFrameCueTrackDefinition> OptionalTracks =
		DataProvider.IsValid() && Scope.IsValid()
			? DataProvider->GetOptionalTracks(Scope)
			: TArray<FPaper2DPlusFrameCueTrackDefinition>();

	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("ManageTracksCreate", LOCTEXT("ManageTracksSection", "Cue tracks"));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("NewTrack", "New track"),
		LOCTEXT(
			"NewTrackTip",
			"Add an optional organizational track. Tracks never change Cue timing or dispatch order."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([WeakSelf]()
			{
				if (const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin())
				{
					Timeline->HandleAddTrack();
				}
			}),
			FCanExecuteAction::CreateLambda([WeakSelf]()
			{
				const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin();
				return Timeline.IsValid() && Timeline->HasResolvedAnimation();
			})));
	if (OptionalTracks.Num() == 0)
	{
		MenuBuilder.AddWidget(
			SNew(STextBlock)
			.Text(LOCTEXT(
				"NoNamedTracksHint",
				"Every Cue is on the Default track until you add one."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.WrapTextAt(240.0f),
			FText::GetEmpty(),
			true);
	}
	MenuBuilder.EndSection();

	if (OptionalTracks.Num() > 0)
	{
		// The Add Cue target used to be chosen only by clicking a track header. Keeping it here means the
		// overflow that owns track management also owns the one track decision that changes what + Add Cue
		// does, instead of that decision being reachable only from lane chrome.
		MenuBuilder.BeginSection("ManageTracksTarget", LOCTEXT("ManageTracksTargetSection", "Add Cue target"));
		const auto AddTargetEntry = [&MenuBuilder, WeakSelf](const FText& Label, const FGuid TargetId)
		{
			MenuBuilder.AddMenuEntry(
				Label,
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([WeakSelf, TargetId]()
					{
						if (const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin())
						{
							Timeline->SelectTrack(TargetId);
						}
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([WeakSelf, TargetId]()
					{
						const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin();
						return Timeline.IsValid() && Timeline->GetActiveTrackId() == TargetId;
					})),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		};
		AddTargetEntry(LOCTEXT("DefaultTrackTarget", "Default"), FGuid());
		for (const FPaper2DPlusFrameCueTrackDefinition& Track : OptionalTracks)
		{
			AddTargetEntry(FText::FromString(Track.DisplayName), Track.TrackId);
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("ManageTracksEdit", LOCTEXT("ManageTracksEditSection", "Edit track"));
		for (int32 Index = 0; Index < OptionalTracks.Num(); ++Index)
		{
			const FGuid TrackId = OptionalTracks[Index].TrackId;
			const FString DisplayName = OptionalTracks[Index].DisplayName;
			MenuBuilder.AddSubMenu(
				FText::FromString(DisplayName),
				LOCTEXT("EditTrackTip", "Rename, reorder, or remove this track."),
				// Submenu content is built when the designer hovers, so it weak-captures the panel
				// (Pattern 15) rather than keeping a closed tab alive through a menu delegate.
				FNewMenuDelegate::CreateLambda(
					[WeakSelf, TrackId, DisplayName, Index](FMenuBuilder& SubMenuBuilder)
					{
						if (const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin())
						{
							Timeline->PopulateTrackMenu(SubMenuBuilder, TrackId, DisplayName, Index);
						}
					}));
		}
		MenuBuilder.EndSection();
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SFrameCueTimeline::BuildTrackMenu(
	const FGuid TrackId,
	const FString DisplayName,
	const int32 OptionalIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	PopulateTrackMenu(MenuBuilder, TrackId, DisplayName, OptionalIndex);
	return MenuBuilder.MakeWidget();
}

void SFrameCueTimeline::PopulateTrackMenu(
	FMenuBuilder& MenuBuilder,
	const FGuid TrackId,
	const FString DisplayName,
	const int32 OptionalIndex)
{
	const TWeakPtr<SFrameCueTimeline> WeakSelf = SharedThis(this);
	MenuBuilder.BeginSection("TrackName", LOCTEXT("TrackNameSection", "Track name"));
	MenuBuilder.AddWidget(
		SNew(SEditableTextBox)
		.Text(FText::FromString(DisplayName))
		.SelectAllTextWhenFocused(true)
		.OnTextCommitted_Lambda([WeakSelf, TrackId](const FText& Text, ETextCommit::Type CommitType)
		{
			if (CommitType != ETextCommit::OnEnter)
			{
				return;
			}
			if (const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin())
			{
				const FProfileScopedAnimationIdentity Scope = Timeline->GetScopeIdentity();
				const FString NewName = Text.ToString();
				Timeline->RunTrackCommand(
					[Timeline, Scope, TrackId, NewName]()
					{
						return Timeline->DataProvider.IsValid()
							&& Timeline->DataProvider->RenameTrack(Scope, TrackId, NewName);
					});
				FSlateApplication::Get().DismissAllMenus();
			}
		}),
		FText::GetEmpty(),
		true);
	MenuBuilder.EndSection();

	const int32 OptionalTrackCount = GetNamedTrackCount();
	MenuBuilder.BeginSection("TrackOrder", LOCTEXT("TrackOrderSection", "Order"));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MoveTrackUp", "Move up"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([WeakSelf, TrackId, OptionalIndex]()
			{
				if (const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin())
				{
					const FProfileScopedAnimationIdentity Scope = Timeline->GetScopeIdentity();
					Timeline->RunTrackCommand(
						[Timeline, Scope, TrackId, OptionalIndex]()
						{
							return Timeline->DataProvider.IsValid()
								&& Timeline->DataProvider->ReorderTrack(
									Scope, TrackId, OptionalIndex - 1);
						});
				}
			}),
			FCanExecuteAction::CreateLambda([OptionalIndex]() { return OptionalIndex > 0; })));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MoveTrackDown", "Move down"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([WeakSelf, TrackId, OptionalIndex]()
			{
				if (const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin())
				{
					const FProfileScopedAnimationIdentity Scope = Timeline->GetScopeIdentity();
					Timeline->RunTrackCommand(
						[Timeline, Scope, TrackId, OptionalIndex]()
						{
							return Timeline->DataProvider.IsValid()
								&& Timeline->DataProvider->ReorderTrack(
									Scope, TrackId, OptionalIndex + 1);
						});
				}
			}),
			FCanExecuteAction::CreateLambda([OptionalIndex, OptionalTrackCount]()
			{
				return OptionalIndex >= 0 && OptionalIndex + 1 < OptionalTrackCount;
			})));
	MenuBuilder.EndSection();

	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	const FFrameCueTrackUsage Usage = DataProvider.IsValid() && Scope.IsValid()
		? DataProvider->GetTrackUsage(Scope, TrackId)
		: FFrameCueTrackUsage();
	MenuBuilder.BeginSection("TrackRemove", LOCTEXT("TrackRemoveSection", "Remove"));
	auto AddRemoveEntry = [&](const FText& Label, const FText& ToolTip,
		const EPaper2DPlusFrameCueTrackRemovalMode Mode)
	{
		MenuBuilder.AddMenuEntry(
			Label,
			ToolTip,
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakSelf, TrackId, Mode]()
			{
				if (const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin())
				{
					const FProfileScopedAnimationIdentity CurrentScope = Timeline->GetScopeIdentity();
					if (Timeline->RunTrackCommand(
						[Timeline, CurrentScope, TrackId, Mode]()
						{
							return Timeline->DataProvider.IsValid()
								&& Timeline->DataProvider->RemoveTrack(CurrentScope, TrackId, Mode);
						}))
					{
						Timeline->ActiveTrackId.Invalidate();
						Timeline->RefreshTrackHeaders();
					}
				}
			})));
	};
	if (Usage.TotalCueCount() == 0)
	{
		AddRemoveEntry(
			LOCTEXT("RemoveEmptyTrack", "Remove empty track"),
			FText::GetEmpty(),
			EPaper2DPlusFrameCueTrackRemovalMode::MoveCuesToDefault);
	}
	else
	{
		AddRemoveEntry(
			FText::Format(
				LOCTEXT("RemoveTrackMoveCues", "Remove track; move {0} Cues to Default"),
				Usage.TotalCueCount()),
			LOCTEXT("RemoveTrackMoveCuesTip", "Preserve every Cue and move only its organizational membership."),
			EPaper2DPlusFrameCueTrackRemovalMode::MoveCuesToDefault);
		AddRemoveEntry(
			FText::Format(
				LOCTEXT("RemoveTrackDeleteCues", "Remove track and delete {0} Cues"),
				Usage.TotalCueCount()),
			LOCTEXT("RemoveTrackDeleteCuesTip", "Delete every active and stashed Cue assigned to this track."),
			EPaper2DPlusFrameCueTrackRemovalMode::DeleteCues);
	}
	MenuBuilder.EndSection();
}

bool SFrameCueTimeline::RunTrackCommand(TFunctionRef<bool()> Command)
{
	if (!DataProvider.IsValid() || !GetScopeIdentity().IsValid())
	{
		return false;
	}
	// Provider commands own their one transaction and rollback. The Slate shell only refreshes after
	// a committed command; wrapping again here would create nested/empty undo entries.
	if (!Command())
	{
		return false;
	}
	NotifyStructureChanged();
	return true;
}

void SFrameCueTimeline::NotifyStructureChanged()
{
	if (CueLane.IsValid())
	{
		CueLane->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
	RefreshTrackHeaders();
	OnStructureChanged.ExecuteIfBound();
}

void SFrameCueTimeline::SetActiveTrackId(const FGuid& TrackId)
{
	ActiveTrackId.Invalidate();
	if (TrackId.IsValid())
	{
		const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
		if (DataProvider.IsValid() && Scope.IsValid()
			&& DataProvider->GetOptionalTracks(Scope).ContainsByPredicate(
				[TrackId](const FPaper2DPlusFrameCueTrackDefinition& Track)
				{
					return Track.TrackId == TrackId;
				}))
		{
			ActiveTrackId = TrackId;
		}
	}
	// Changing the Add Cue target is a value change, not a structural one: header rows are already
	// attribute-bound to the live active id, so a repaint is enough. A ClearChildren rebuild here
	// would destroy the very header button whose OnClicked routed us in. Track add/remove/rename
	// still rebuild through NotifyStructureChanged/RefreshTrackHeaders.
	ReconcilePrimarySelection();
	if (TrackHeadersBox.IsValid())
	{
		TrackHeadersBox->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

bool SFrameCueTimeline::IsTrackRowActive(const FGuid& TrackId) const
{
	return TrackId.IsValid() ? ActiveTrackId == TrackId : !ActiveTrackId.IsValid();
}

void SFrameCueTimeline::SelectTrack(const FGuid TrackId)
{
	// SetActiveTrackId reconciles the current primary selection against the new target. Clear the
	// old Cue first or that reconciliation can restore its containing track over the designer's click.
	PrimarySelection.Clear();
	SetActiveTrackId(TrackId);
	PrimarySelection.SelectTrack(ActiveTrackId);
	PrimarySelection.Scope = GetScopeIdentity();
	BroadcastPrimarySelection();
}

void SFrameCueTimeline::HandleCueSelected(const int32 CueIndex)
{
	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = CueSource.Get(nullptr);
	if (!DataProvider.IsValid() || !Scope.IsValid() || !Cues || !Cues->IsValidIndex(CueIndex))
	{
		if (PrimarySelection.Kind == Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Cue)
		{
			ClearPrimarySelection();
		}
		return;
	}
	const FGuid CueTrackId = DataProvider->ResolveCueTrackId(Scope, (*Cues)[CueIndex]);
	// Install the new identity before the target change. Reconciliation must observe this Cue, not
	// the previously selected Cue on another track, when it chooses the active Add Cue target.
	PrimarySelection.SelectCue((*Cues)[CueIndex], CueTrackId);
	PrimarySelection.Scope = Scope;
	SetActiveTrackId(CueTrackId);
	BroadcastPrimarySelection();
	RevealCueSelection(CueIndex);
}

void SFrameCueTimeline::HandleCurveSelected(const FName CurveName)
{
	if (CurveName.IsNone())
	{
		ClearPrimarySelection();
		return;
	}
	PrimarySelection.SelectCurve(CurveName);
	PrimarySelection.Scope = GetScopeIdentity();
	BroadcastPrimarySelection();
}

void SFrameCueTimeline::ClearPrimarySelection()
{
	if (PrimarySelection.Kind == Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::None)
	{
		return;
	}
	PrimarySelection.Clear();
	BroadcastPrimarySelection();
}

void SFrameCueTimeline::ReconcileCurveSelection(const TSet<FName>& ValidCurveNames)
{
	ReconcilePrimarySelection(&ValidCurveNames);
}

void SFrameCueTimeline::RefreshTrackHeaders()
{
	if (MissingCueNotice.IsValid())
	{
		MissingCueNotice->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
	if (!TrackHeadersBox.IsValid() || !CueLane.IsValid())
	{
		return;
	}
	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	const TArray<FPaper2DPlusFrameCueTrackDefinition> OptionalTracks =
		DataProvider.IsValid() && Scope.IsValid()
			? DataProvider->GetOptionalTracks(Scope)
			: TArray<FPaper2DPlusFrameCueTrackDefinition>();
	if (ActiveTrackId.IsValid()
		&& !OptionalTracks.ContainsByPredicate([this](const FPaper2DPlusFrameCueTrackDefinition& Track)
		{
			return Track.TrackId == ActiveTrackId;
		}))
	{
		ActiveTrackId.Invalidate();
	}
	ReconcilePrimarySelection();

	TMap<FGuid, FString> TrackNames;
	for (const FPaper2DPlusFrameCueTrackDefinition& Track : OptionalTracks)
	{
		TrackNames.Add(Track.TrackId, Track.DisplayName);
	}
	const TArray<Paper2DPlusFrameCueTimeline::FPackedTrack> PackedTracks =
		CueLane->GetPackedTracksForPresentation();
	VisibleTrackCount = PackedTracks.Num();
	TrackHeadersBox->ClearChildren();
	TrackHeadersBox->AddSlot()
	.AutoHeight()
	[
		SNew(SBox)
		.HeightOverride(RulerHeaderHeight)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(8.0f, 7.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CueTracksHeader", "CUE TRACKS"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				]
				// "Add target" only means something once more than the implicit Default lane exists.
				// With no named tracks it is a line that always reads "Default" and teaches nothing.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Visibility_Lambda([this]()
					{
						return HasNamedTracks() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text_Lambda([this]()
					{
						return FText::Format(
							LOCTEXT("ActiveCueTrackHeader", "Add target: {0}"),
							GetActiveTrackLabel());
					})
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
		]
	];

	for (int32 PackedIndex = 0; PackedIndex < PackedTracks.Num(); ++PackedIndex)
	{
		const Paper2DPlusFrameCueTimeline::FPackedTrack& Packed = PackedTracks[PackedIndex];
		const bool bDefault = Packed.IsDefault();
		const FString DisplayName = bDefault
			? LOCTEXT("DefaultTrackHeader", "Default").ToString()
			: TrackNames.FindRef(Packed.TrackId);
		const int32 OptionalIndex = bDefault
			? INDEX_NONE
			: OptionalTracks.IndexOfByPredicate([&Packed](const FPaper2DPlusFrameCueTrackDefinition& Track)
			{
				return Track.TrackId == Packed.TrackId;
			});
		const float TrackHeight = CueLane->GetPackedTrackHeightForPresentation(Packed);
		const FGuid TrackId = Packed.TrackId;
		const TWeakPtr<SFrameCueTimeline> WeakSelf = SharedThis(this);
		// Every active-state visual on this row is attribute-bound instead of baked at build time.
		// The row's own button changes the active target from inside its OnClicked, so the change
		// must never route back through a ClearChildren rebuild that would destroy the clicked
		// button while its handler is still on the stack — a repaint reads the new state instead.
		const auto IsRowActive = [WeakSelf, TrackId]()
		{
			const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin();
			return Timeline.IsValid() && Timeline->IsTrackRowActive(TrackId);
		};
		TrackHeadersBox->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(TrackHeight)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor_Lambda([IsRowActive]()
				{
					return IsRowActive()
						? FLinearColor(0.16f, 0.31f, 0.48f, 1.0f)
						: FLinearColor(0.10f, 0.10f, 0.10f, 1.0f);
				})
				.Padding(FMargin(4.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.ContentPadding(FMargin(3.0f, 1.0f))
						.AccessibleText_Lambda([IsRowActive, DisplayName]()
						{
							return FText::Format(
								IsRowActive()
									? LOCTEXT("ActiveTrackAccessible", "Active Cue track {0}")
									: LOCTEXT("InactiveTrackAccessible", "Cue track {0}"),
								FText::FromString(DisplayName));
						})
						.OnClicked_Lambda([WeakSelf, TrackId]()
						{
							if (const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin())
							{
								Timeline->SelectTrack(TrackId);
							}
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text_Lambda([IsRowActive, DisplayName]()
							{
								return FText::FromString(
									FString(IsRowActive() ? TEXT("●  ") : TEXT("")) + DisplayName);
							})
							.Font_Lambda([IsRowActive]()
							{
								return FCoreStyle::GetDefaultFontStyle(
									IsRowActive() ? "Bold" : "Regular", 8);
							})
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						bDefault
							? SNullWidget::NullWidget
							: StaticCastSharedRef<SWidget>(
								SNew(SComboButton)
								.ButtonStyle(FAppStyle::Get(), "SimpleButton")
								.ToolTipText(LOCTEXT("TrackOptionsTip", "Rename, reorder, or remove this track."))
								.OnGetMenuContent_Lambda([WeakSelf, TrackId, DisplayName, OptionalIndex]()
								{
									if (const TSharedPtr<SFrameCueTimeline> Timeline = WeakSelf.Pin())
									{
										return Timeline->BuildTrackMenu(TrackId, DisplayName, OptionalIndex);
									}
									return SNullWidget::NullWidget;
								})
								.ButtonContent()
								[
									SNew(STextBlock).Text(FText::FromString(TEXT("⋯")))
								])
					]
				]
			]
		];
	}

	TrackHeadersBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f, 0.0f, 0.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(CurveHeaderHeight)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(8.0f, 5.0f))
				[
					SNew(STextBlock)
					.Text(CanEditCurves()
						? LOCTEXT("CurvesHeader", "CURVES")
						: LOCTEXT("CurvesReadOnlyHeader", "CURVES  ·  PROFILE READ-ONLY"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			CurveLegendContent.IsValid()
				? CurveLegendContent.ToSharedRef()
				: SNullWidget::NullWidget
		]
	];
}

void SFrameCueTimeline::HandleHostDeactivated()
{
	if (CueLane.IsValid())
	{
		CueLane->CancelActiveInteraction();
	}
}

void SFrameCueTimeline::BroadcastPrimarySelection()
{
	OnPrimarySelectionChanged.ExecuteIfBound(PrimarySelection);
}

void SFrameCueTimeline::ReconcilePrimarySelection(const TSet<FName>* ValidCurveNames)
{
	TSet<FGuid> ValidTracks;
	TSet<TWeakObjectPtr<UPaper2DPlusCueBase>> ValidCues;
	const FProfileScopedAnimationIdentity Scope = GetScopeIdentity();
	const bool bScopeValid = DataProvider.IsValid() && Scope.IsValid();
	if (bScopeValid)
	{
		for (const FPaper2DPlusFrameCueTrackDefinition& Track : DataProvider->GetOptionalTracks(Scope))
		{
			ValidTracks.Add(Track.TrackId);
		}
		if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = CueSource.Get(nullptr))
		{
			for (UPaper2DPlusCueBase* Cue : *Cues)
			{
				if (IsValid(Cue))
				{
					ValidCues.Add(Cue);
				}
			}
		}
	}
	bool bSelectionChanged = false;
	if (!PrimarySelection.IsInScope(Scope))
	{
		PrimarySelection.Clear();
		bSelectionChanged = true;
	}
	bSelectionChanged = PrimarySelection.Reconcile(
		bScopeValid, ValidTracks, ValidCues, ValidCurveNames) || bSelectionChanged;
	if (bScopeValid
		&& PrimarySelection.Kind == Paper2DPlusFrameCueTimeline::EPrimarySelectionKind::Cue
		&& PrimarySelection.Cue.IsValid())
	{
		const FGuid ResolvedTrackId = DataProvider->ResolveCueTrackId(
			Scope, PrimarySelection.Cue.Get());
		if (PrimarySelection.TrackId != ResolvedTrackId)
		{
			PrimarySelection.TrackId = ResolvedTrackId;
			bSelectionChanged = true;
		}
		// A selected Cue makes its containing track the visible Add Cue target. Keep that invariant
		// after undo, track removal, or an external reassignment without coupling curve selection to it.
		ActiveTrackId = ResolvedTrackId;
	}
	if (bSelectionChanged)
	{
		BroadcastPrimarySelection();
	}
}

void SFrameCueTimeline::HandleGutterSlotResized(const float NewWidth)
{
	RequestedGutterWidth =
		Paper2DPlusFrameCueTimeline::FGutterGeometry::ClampRequestedWidth(
			NewWidth);
}

bool SFrameCueTimeline::HasResizableGutterForTests() const
{
	if (!TimelineSplitter.IsValid()
		|| !TimelineSplitter->GetChildren()
		|| TimelineSplitter->GetChildren()->Num() != 2)
	{
		return false;
	}
	const SSplitter::FSlot& GutterSlot = TimelineSplitter->SlotAt(0);
	return GutterSlot.GetSizingRule() == SSplitter::SizeToContent
		&& GutterSlot.IsResizable()
		&& GutterSlot.CanBeResized()
		&& GutterSlot.OnSlotResized().IsBound()
		&& FMath::IsNearlyEqual(
			GutterSlot.GetMinSize(),
			Paper2DPlusFrameCueTimeline::FGutterGeometry::MinimumWidth);
}

bool SFrameCueTimeline::ResizeGutterThroughSlotForTests(const float Width)
{
	if (!HasResizableGutterForTests())
	{
		return false;
	}
	SSplitter::FSlot& GutterSlot = TimelineSplitter->SlotAt(0);
	GutterSlot.OnSlotResized().Execute(Width);
	return true;
}

void SFrameCueTimeline::HandleEdgeAutoscrollRequested(
	const Paper2DPlusFrameCueTimeline::EDragAxis Axis,
	const FVector2D ScreenPosition)
{
	const TSharedPtr<SScrollBox> ScrollBox =
		Axis == Paper2DPlusFrameCueTimeline::EDragAxis::Horizontal
			? HorizontalScrollBox
			: Axis == Paper2DPlusFrameCueTimeline::EDragAxis::Vertical
				? VerticalScrollBox
				: nullptr;
	if (!ScrollBox.IsValid())
	{
		return;
	}
	const FGeometry Geometry = ScrollBox->GetCachedGeometry();
	const FVector2D Local = Geometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D Size = Geometry.GetLocalSize();
	const float Coordinate = Axis == Paper2DPlusFrameCueTimeline::EDragAxis::Horizontal
		? Local.X
		: Local.Y;
	const float Extent = Axis == Paper2DPlusFrameCueTimeline::EDragAxis::Horizontal
		? Size.X
		: Size.Y;
	const float Delta = Paper2DPlusFrameCueTimeline::ComputeEdgeAutoscrollDelta(
		Coordinate, Extent);
	if (!FMath::IsNearlyZero(Delta))
	{
		ScrollBox->SetScrollOffset(FMath::Max(0.0f, ScrollBox->GetScrollOffset() + Delta));
	}
}

void SFrameCueTimeline::RevealScrollRange(
	const TSharedPtr<SScrollBox>& ScrollBox,
	const float ItemStart,
	const float ItemEnd)
{
	if (!ScrollBox.IsValid())
	{
		return;
	}
	const float ViewportExtent = ScrollBox->GetCachedGeometry().GetLocalSize().X;
	if (ViewportExtent <= 0.0f)
	{
		return;
	}
	ScrollBox->SetScrollOffset(Paper2DPlusFrameCueTimeline::ResolveRevealScrollOffset(
		ScrollBox->GetScrollOffset(), ViewportExtent, ItemStart, ItemEnd));
}

void SFrameCueTimeline::RevealCueSelection(const int32 CueIndex)
{
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* Cues = CueSource.Get(nullptr);
	if (!CueLane.IsValid() || !Cues || !Cues->IsValidIndex(CueIndex) || !IsValid((*Cues)[CueIndex]))
	{
		return;
	}
	const int32 StartFrame = FMath::Max(0, (*Cues)[CueIndex]->GetPrimaryAnchorFrame());
	const int32 Duration = (*Cues)[CueIndex]->IsRangeCue()
		? FMath::Max(1, (*Cues)[CueIndex]->GetCueFrameCount())
		: 1;
	RevealScrollRange(
		HorizontalScrollBox,
		StartFrame * Paper2DPlusFrameCueTimeline::FTimingGeometry::PixelsPerKeyFrame,
		(StartFrame + Duration) * Paper2DPlusFrameCueTimeline::FTimingGeometry::PixelsPerKeyFrame);
}

#undef LOCTEXT_NAMESPACE

namespace Paper2DPlusFrameCueTimeline
{

ETimingContext FTimingGeometry::ResolveContext(
	const bool bHasResolvedAnimation,
	const int32 FrameCount)
{
	if (!bHasResolvedAnimation)
	{
		return ETimingContext::NoAnimation;
	}
	return FrameCount > 0
		? ETimingContext::Timed
		: ETimingContext::ResolvedZeroFrames;
}

EDragAxis ResolveDragAxis(
	const FVector2D ScreenDelta,
	const float TriggerDistance,
	const bool bAllowVertical)
{
	if (ScreenDelta.SizeSquared() < FMath::Square(FMath::Max(0.0f, TriggerDistance)))
	{
		return EDragAxis::Pending;
	}
	return bAllowVertical && FMath::Abs(ScreenDelta.Y) > FMath::Abs(ScreenDelta.X)
		? EDragAxis::Vertical
		: EDragAxis::Horizontal;
}

void FPrimarySelection::Clear()
{
	Kind = EPrimarySelectionKind::None;
	Scope = {};
	TrackId.Invalidate();
	Cue.Reset();
	CurveName = NAME_None;
}

void FPrimarySelection::SelectTrack(const FGuid InTrackId)
{
	Clear();
	Kind = EPrimarySelectionKind::Track;
	TrackId = InTrackId;
}

void FPrimarySelection::SelectCue(UPaper2DPlusCueBase* InCue, const FGuid InTrackId)
{
	Clear();
	if (IsValid(InCue))
	{
		Kind = EPrimarySelectionKind::Cue;
		Cue = InCue;
		TrackId = InTrackId;
	}
}

void FPrimarySelection::SelectCurve(const FName InCurveName)
{
	Clear();
	if (!InCurveName.IsNone())
	{
		Kind = EPrimarySelectionKind::Curve;
		CurveName = InCurveName;
	}
}

bool FPrimarySelection::IsInScope(
	const FProfileScopedAnimationIdentity& CurrentScope) const
{
	return Kind == EPrimarySelectionKind::None
		|| (CurrentScope.IsValid()
			&& Scope.Animation == CurrentScope.Animation
			&& Scope.LayerScope == CurrentScope.LayerScope);
}

bool FPrimarySelection::Reconcile(
	const bool bScopeValid,
	const TSet<FGuid>& ValidOptionalTrackIds,
	const TSet<TWeakObjectPtr<UPaper2DPlusCueBase>>& ValidCues,
	const TSet<FName>* ValidCurveNames)
{
	bool bValid = bScopeValid || Kind == EPrimarySelectionKind::None;
	if (bValid)
	{
		switch (Kind)
		{
		case EPrimarySelectionKind::Track:
			bValid = !TrackId.IsValid() || ValidOptionalTrackIds.Contains(TrackId);
			break;
		case EPrimarySelectionKind::Cue:
			bValid = Cue.IsValid() && ValidCues.Contains(Cue);
			break;
		case EPrimarySelectionKind::Curve:
			bValid = !CurveName.IsNone()
				&& (!ValidCurveNames || ValidCurveNames->Contains(CurveName));
			break;
		case EPrimarySelectionKind::None:
		default:
			bValid = true;
			break;
		}
	}
	if (!bValid)
	{
		Clear();
		return true;
	}
	return false;
}

float ComputeEdgeAutoscrollDelta(
	const float PointerCoordinate,
	const float ViewportExtent,
	const float EdgeBand,
	const float MaximumStep)
{
	const float SafeBand = FMath::Max(1.0f, EdgeBand);
	const float SafeStep = FMath::Max(0.0f, MaximumStep);
	if (ViewportExtent <= 0.0f || SafeStep <= 0.0f)
	{
		return 0.0f;
	}
	if (PointerCoordinate < SafeBand)
	{
		return -SafeStep * FMath::Clamp(
			(SafeBand - PointerCoordinate) / SafeBand, 0.0f, 1.0f);
	}
	if (PointerCoordinate > ViewportExtent - SafeBand)
	{
		return SafeStep * FMath::Clamp(
			(PointerCoordinate - (ViewportExtent - SafeBand)) / SafeBand, 0.0f, 1.0f);
	}
	return 0.0f;
}

float ResolveRevealScrollOffset(
	const float CurrentOffset,
	const float ViewportExtent,
	const float ItemStart,
	const float ItemEnd,
	const float Margin)
{
	const float SafeCurrent = FMath::Max(0.0f, CurrentOffset);
	const float SafeViewport = FMath::Max(0.0f, ViewportExtent);
	const float SafeMargin = FMath::Clamp(Margin, 0.0f, SafeViewport * 0.5f);
	const float Start = FMath::Min(ItemStart, ItemEnd);
	const float End = FMath::Max(ItemStart, ItemEnd);
	if (SafeViewport <= 0.0f)
	{
		return SafeCurrent;
	}
	if (Start < SafeCurrent + SafeMargin)
	{
		return FMath::Max(0.0f, Start - SafeMargin);
	}
	if (End > SafeCurrent + SafeViewport - SafeMargin)
	{
		return FMath::Max(0.0f, End - SafeViewport + SafeMargin);
	}
	return SafeCurrent;
}

float FTimingGeometry::GetBodyWidth(
	const bool bHasResolvedAnimation,
	const int32 FrameCount)
{
	return ResolveContext(bHasResolvedAnimation, FrameCount) == ETimingContext::Timed
		? FMath::Max(1, FrameCount) * PixelsPerKeyFrame
		: MinimumNonTimingBodyWidth;
}

bool FTimingGeometry::TryResolveFrameAtX(
	const bool bHasResolvedAnimation,
	const int32 FrameCount,
	const float LocalX,
	int32& OutFrameIndex)
{
	OutFrameIndex = INDEX_NONE;
	if (ResolveContext(bHasResolvedAnimation, FrameCount) != ETimingContext::Timed
		|| LocalX < 0.0f
		|| LocalX >= FrameCount * PixelsPerKeyFrame)
	{
		return false;
	}
	OutFrameIndex = FMath::FloorToInt(LocalX / PixelsPerKeyFrame);
	return OutFrameIndex >= 0 && OutFrameIndex < FrameCount;
}

TArray<FPackedTrack> PackPlacements(
	const TArray<FPlacement>& Placements,
	const TArray<FGuid>& OrderedOptionalTrackIds)
{
	TArray<FPackedTrack> Result;
	Result.AddDefaulted(); // Implicit Default is permanent and first.

	TMap<FGuid, int32> TrackToResultIndex;
	for (const FGuid& TrackId : OrderedOptionalTrackIds)
	{
		if (!TrackId.IsValid() || TrackToResultIndex.Contains(TrackId))
		{
			continue;
		}
		FPackedTrack& Track = Result.AddDefaulted_GetRef();
		Track.TrackId = TrackId;
		TrackToResultIndex.Add(TrackId, Result.Num() - 1);
	}

	for (const FPlacement& Placement : Placements)
	{
		const int32* OptionalTrackIndex = Placement.TrackId.IsValid()
			? TrackToResultIndex.Find(Placement.TrackId)
			: nullptr;
		FPackedTrack& Track = Result[OptionalTrackIndex ? *OptionalTrackIndex : 0];
		FPackedPlacement& Packed = Track.Placements.AddDefaulted_GetRef();
		Packed.CueIndex = Placement.CueIndex;
		Packed.TrackId = OptionalTrackIndex ? Placement.TrackId : FGuid();
		Packed.StartFrame = Placement.StartFrame;
		Packed.EndFrameInclusive = ComputeInclusiveEnd(Placement);
	}

	for (FPackedTrack& Track : Result)
	{
		Track.Placements.StableSort([](
			const FPackedPlacement& A,
			const FPackedPlacement& B)
		{
			if (A.StartFrame != B.StartFrame)
			{
				return A.StartFrame < B.StartFrame;
			}
			if (A.EndFrameInclusive != B.EndFrameInclusive)
			{
				return A.EndFrameInclusive < B.EndFrameInclusive;
			}
			return A.CueIndex < B.CueIndex;
		});

		TArray<int32> LaneEndFrames;
		for (FPackedPlacement& Placement : Track.Placements)
		{
			int32 LaneIndex = INDEX_NONE;
			for (int32 Candidate = 0; Candidate < LaneEndFrames.Num(); ++Candidate)
			{
				if (LaneEndFrames[Candidate] < Placement.StartFrame)
				{
					LaneIndex = Candidate;
					break;
				}
			}
			if (LaneIndex == INDEX_NONE)
			{
				LaneIndex = LaneEndFrames.Add(Placement.EndFrameInclusive);
			}
			else
			{
				LaneEndFrames[LaneIndex] = Placement.EndFrameInclusive;
			}
			Placement.Sublane = LaneIndex;
		}
		Track.SublaneCount = FMath::Max(1, LaneEndFrames.Num());
	}

	return Result;
}
}
