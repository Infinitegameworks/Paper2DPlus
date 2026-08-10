// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/SPaper2DPlusFrameCueTypePicker.h"

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "FrameEventEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueTypePicker"

namespace Paper2DPlusFrameCueTypePicker
{
	FText ResolveRejectionReason(const FPaper2DPlusFrameCueTypeDescriptor& Type)
	{
		// Errors first, then warnings: DescribeCueType stops at the first blocking condition, so the
		// highest-severity entry is the one the designer has to act on.
		for (const FPaper2DPlusFrameCueAuthoringDiagnostic& Diagnostic : Type.Diagnostics)
		{
			if (Diagnostic.Severity == EPaper2DPlusFrameCueDiagnosticSeverity::Error
				&& !Diagnostic.Message.IsEmpty())
			{
				return Diagnostic.Message;
			}
		}
		for (const FPaper2DPlusFrameCueAuthoringDiagnostic& Diagnostic : Type.Diagnostics)
		{
			if (!Diagnostic.Message.IsEmpty())
			{
				return Diagnostic.Message;
			}
		}

		switch (Type.Availability)
		{
		case EPaper2DPlusFrameCueTypeAvailability::NeedsCompile:
			return LOCTEXT("RejectedNeedsCompile", "Compile this Cue Type before placing it.");
		case EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave:
			return LOCTEXT(
				"RejectedNeedsDurableSave",
				"Save this Cue Type in the Frame Cue Type editor before placing it.");
		case EPaper2DPlusFrameCueTypeAvailability::LegacyNeedsBaseline:
			return LOCTEXT(
				"RejectedLegacyNeedsBaseline",
				"This legacy Cue Blueprint needs an explicit baseline before it can be placed.");
		default:
			return LOCTEXT(
				"RejectedUnavailable",
				"This Cue Type is not ready to place.");
		}
	}

	bool IsSurfacedRejection(const FPaper2DPlusFrameCueTypeDescriptor& Type)
	{
		if (Type.Availability == EPaper2DPlusFrameCueTypeAvailability::Ready
			|| Type.Diagnostics.Num() == 0)
		{
			return false;
		}
		for (const FPaper2DPlusFrameCueAuthoringDiagnostic& Diagnostic : Type.Diagnostics)
		{
			switch (Diagnostic.Code)
			{
			case EPaper2DPlusFrameCueDiagnosticCode::NullClass:
			case EPaper2DPlusFrameCueDiagnosticCode::NotFrameCue:
			case EPaper2DPlusFrameCueDiagnosticCode::NonAuthorableClass:
			case EPaper2DPlusFrameCueDiagnosticCode::EditorOnlyClass:
			case EPaper2DPlusFrameCueDiagnosticCode::MissingBlueprint:
				return false;
			default:
				break;
			}
		}
		return true;
	}

	UBlueprint* ResolveRepairTarget(const FPaper2DPlusFrameCueTypeDescriptor& Type)
	{
		if (UClass* CueClass = Type.Class.Get())
		{
			UBlueprint* Blueprint = nullptr;
			const Paper2DPlusFrameCueEditorAuthoring::EPaper2DPlusFrameCueTypeEditRoute Route =
				Paper2DPlusFrameCueEditorAuthoring::ResolveCueTypeEditRoute(CueClass, Blueprint);
			return Route == Paper2DPlusFrameCueEditorAuthoring::
				EPaper2DPlusFrameCueTypeEditRoute::None
				? nullptr
				: Blueprint;
		}
		// A cold class that failed to load leaves only paths behind. When the owning Blueprint path
		// survived, the asset itself can still be opened and repaired.
		if (Type.BlueprintPath.IsValid())
		{
			return Cast<UBlueprint>(Type.BlueprintPath.TryLoad());
		}
		return nullptr;
	}

	TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> BuildItems(
		const FPaper2DPlusFrameCueTypeDiscoveryResult& Discovery)
	{
		TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> Items;
		Items.Reserve(Discovery.Types.Num() + Discovery.RejectedTypes.Num());

		const auto MakeItem = [](
			const FPaper2DPlusFrameCueTypeDescriptor& Type,
			const bool bPlaceable)
		{
			TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item =
				MakeShared<FPaper2DPlusFrameCueTypePickerItem>();
			Item->Type = Type;
			Item->bPlaceable = bPlaceable;
			const FString Label = (Type.PickerLabel.IsEmpty()
				? Type.DisplayName
				: Type.PickerLabel).ToString();
			Item->SearchText = (Label + TEXT(" ") + Type.ClassPath.ToString()).ToLower();
			if (!bPlaceable)
			{
				Item->Reason = ResolveRejectionReason(Type);
				Item->bRepairable = FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(
					ResolveRepairTarget(Type));
			}
			return Item;
		};

		for (const FPaper2DPlusFrameCueTypeDescriptor& Type : Discovery.Types)
		{
			if (!Type.Class.IsValid())
			{
				continue;
			}
			// Automation fixtures stay Ready for the placement gate but never reach a designer's
			// list (TASK-173).
			if (FPaper2DPlusFrameCueTypeAuthoring::IsAutomationFixtureCueClass(
				Type.Class.Get()))
			{
				continue;
			}
			Items.Add(MakeItem(Type, /*bPlaceable*/ true));
		}
		// Rejected rows sort after every placeable one so the ordinary path stays first-class and the
		// repair rows read as an appendix rather than as noise mixed through the results.
		for (const FPaper2DPlusFrameCueTypeDescriptor& Type : Discovery.RejectedTypes)
		{
			if (IsSurfacedRejection(Type))
			{
				Items.Add(MakeItem(Type, /*bPlaceable*/ false));
			}
		}
		return Items;
	}

	bool OpenForRepair(const FPaper2DPlusFrameCueTypeDescriptor& Type)
	{
		UBlueprint* Blueprint = ResolveRepairTarget(Type);
		if (!FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(Blueprint))
		{
			return false;
		}

		if (!Blueprint->IsA<UPaper2DPlusFrameCueBlueprint>())
		{
			// A generic legacy Cue Blueprint must never reach its ordinary asset action, which would
			// open the unrestricted graph editor. Construct the restricted inspector directly, exactly
			// as the timeline's Edit Cue Type route does.
			const TSharedRef<FPaper2DPlusFrameCueTypeEditor> CueTypeEditor =
				MakeShared<FPaper2DPlusFrameCueTypeEditor>();
			CueTypeEditor->InitFrameCueTypeEditor(
				EToolkitMode::Standalone,
				TSharedPtr<IToolkitHost>(),
				Blueprint,
				nullptr);
			return true;
		}

		if (!GEditor)
		{
			return false;
		}
		UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!AssetEditors)
		{
			return false;
		}
		AssetEditors->OpenEditorForAsset(Blueprint);
		IAssetEditorInstance* Instance =
			AssetEditors->FindEditorForAsset(Blueprint, /*bFocusIfOpen*/ false);
		if (!Instance)
		{
			return false;
		}
		return true;
	}
}

SPaper2DPlusFrameCueTypePicker::~SPaper2DPlusFrameCueTypePicker()
{
	if (FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<SWidget> ReturnTarget = FocusReturnTarget.Pin())
		{
			FSlateApplication::Get().SetKeyboardFocus(ReturnTarget, EFocusCause::SetDirectly);
		}
	}
}

void SPaper2DPlusFrameCueTypePicker::Construct(const FArguments& InArgs)
{
	OnPickType = InArgs._OnPickType;
	OnCreateType = InArgs._OnCreateType;
	OnRepairType = InArgs._OnRepairType;
	FocusReturnTarget = InArgs._FocusReturnTarget;
	RefreshDiscovery(true);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Menu.Background"))
		.Padding(8.0f)
		[
			SNew(SBox)
			.WidthOverride(440.0f)
			.HeightOverride(390.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.OnClicked(this, &SPaper2DPlusFrameCueTypePicker::CreateType)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PickerCreateCueType", "+ New Frame Cue Type..."))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SearchBox, SSearchBox)
						.HintText(LOCTEXT("SearchCueTypes", "Search Cue and Cue State Types..."))
						.AccessibleText(LOCTEXT(
							"SearchCueTypesAccessible",
							"Search reusable Frame Cue Types. Use Up and Down to choose, Enter to place, and Escape to cancel."))
						.OnTextChanged(this, &SPaper2DPlusFrameCueTypePicker::SetSearchText)
						.OnKeyDownHandler(this, &SPaper2DPlusFrameCueTypePicker::HandlePickerKeyDown)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.Text(LOCTEXT("RetryCueTypeDiscovery", "Refresh"))
						.ToolTipText(LOCTEXT(
							"RetryCueTypeDiscoveryTip",
							"Re-scan the project for reusable Cue Types."))
						.OnClicked(this, &SPaper2DPlusFrameCueTypePicker::RetryDiscovery)
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					// Discovery friction is carried by the ROWS — a non-Ready type is listed, disabled,
					// with the reason it cannot be placed and a click that opens its repair. A standing
					// status line under the list only restated them, so it now appears solely when there
					// is no row at all to speak for itself.
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SAssignNew(ListView, SListView<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>>)
						.AccessibleText(LOCTEXT(
							"CueTypeResultsAccessible",
							"Reusable Frame Cue Type results. Use Up and Down to choose and Enter to place. Rows marked needs fix cannot be placed; activating one opens it for repair."))
						.ListItemsSource(&FilteredItems)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SPaper2DPlusFrameCueTypePicker::GenerateRow)
					]
					// HAlign_Fill, not HAlign_Center: AutoWrapText wraps against the width the widget is
					// ARRANGED at, and a centered slot derives that width from the child's DESIRED size
					// (clamped to the parent). An auto-wrapping block reports its unwrapped width as its
					// desired size, so on the first arrange the longest status string had only that
					// chicken-and-egg width to wrap against and clipped or reflowed a frame late. Filling
					// ignores desired size entirely and hands the block the popup's own width, so the
					// constraint is real on the very first pass.
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					[
						// Visibility rides the BOX, not the text: an always-visible box would stay
						// hit-testable over the middle of a populated list and swallow a row click. It is
						// full-width now, which makes that gating load-bearing rather than incidental.
						SNew(SBox)
						.Visibility(this, &SPaper2DPlusFrameCueTypePicker::GetEmptyStateVisibility)
						.Padding(12.0f)
						[
							SNew(STextBlock)
							.Text(this, &SPaper2DPlusFrameCueTypePicker::GetStatusText)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Justification(ETextJustify::Center)
							.AutoWrapText(true)
						]
					]
				]
			]
		]
	];

	ApplyFilter();
	RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateLambda(
			[WeakSearch = TWeakPtr<SSearchBox>(SearchBox)](double, float)
			{
				if (const TSharedPtr<SSearchBox> Search = WeakSearch.Pin())
				{
					FSlateApplication::Get().SetKeyboardFocus(Search, EFocusCause::SetDirectly);
				}
				return EActiveTimerReturnType::Stop;
			}));
	DiscoveryRefreshTimer = RegisterActiveTimer(
		0.5f,
		FWidgetActiveTimerDelegate::CreateSP(
			this,
			&SPaper2DPlusFrameCueTypePicker::RefreshWhileOpen));
}

void SPaper2DPlusFrameCueTypePicker::RefreshDiscovery(const bool bForce)
{
	const FPaper2DPlusFrameCueTypeDiscoveryResult Discovery =
		FPaper2DPlusFrameCueTypeAuthoring::DiscoverCueTypes();
	FSoftObjectPath SelectedClassPath;
	if (ListView.IsValid())
	{
		const TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> SelectedItems =
			ListView->GetSelectedItems();
		if (SelectedItems.Num() > 0 && SelectedItems[0].IsValid())
		{
			SelectedClassPath = SelectedItems[0]->Type.ClassPath;
		}
	}

	FString Signature = FString::FromInt(Discovery.RejectedTypes.Num());
	for (const FPaper2DPlusFrameCueTypeDescriptor& Type : Discovery.Types)
	{
		Signature += FString::Printf(
			TEXT("|R:%s:%d"),
			*Type.ClassPath.ToString(),
			static_cast<int32>(Type.Availability));
	}
	DiscoveryErrorCount = 0;
	for (const FPaper2DPlusFrameCueTypeDescriptor& Rejected : Discovery.RejectedTypes)
	{
		bool bLoadFailure = false;
		Signature += FString::Printf(
			TEXT("|X:%s:%d"),
			*Rejected.ClassPath.ToString(),
			static_cast<int32>(Rejected.Availability));
		for (const FPaper2DPlusFrameCueAuthoringDiagnostic& Diagnostic : Rejected.Diagnostics)
		{
			Signature += FString::Printf(TEXT(":%d"), static_cast<int32>(Diagnostic.Code));
			bLoadFailure |= Diagnostic.Code
				== EPaper2DPlusFrameCueDiagnosticCode::DiscoveryLoadFailed;
		}
		DiscoveryErrorCount += bLoadFailure ? 1 : 0;
	}
	if (bForce || Signature != DiscoverySignature)
	{
		DiscoverySignature = MoveTemp(Signature);
		if (ListView.IsValid())
		{
			ListView->ClearSelection();
		}
		AllItems = Paper2DPlusFrameCueTypePicker::BuildItems(Discovery);
		ReadyTypeCount = 0;
		RejectedTypeCount = 0;
		for (const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>& Item : AllItems)
		{
			if (!Item.IsValid())
			{
				continue;
			}
			if (Item->bPlaceable)
			{
				++ReadyTypeCount;
			}
			else
			{
				++RejectedTypeCount;
			}
		}
		ApplyFilter();
		if (ListView.IsValid() && SelectedClassPath.IsValid())
		{
			for (const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>& Item : FilteredItems)
			{
				if (Item.IsValid() && Item->Type.ClassPath == SelectedClassPath)
				{
					ListView->SetSelection(Item, ESelectInfo::Direct);
					ListView->RequestScrollIntoView(Item);
					break;
				}
			}
		}
	}
}

void SPaper2DPlusFrameCueTypePicker::ApplyFilter()
{
	FilteredItems.Reset();
	const FString Needle = SearchText.ToLower();
	for (const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>& Item : AllItems)
	{
		if (Item.IsValid() && (Needle.IsEmpty() || Item->SearchText.Contains(Needle)))
		{
			FilteredItems.Add(Item);
		}
	}
	if (ListView.IsValid())
	{
		const TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> SelectedItems =
			ListView->GetSelectedItems();
		if (SelectedItems.Num() > 0 && !FilteredItems.Contains(SelectedItems[0]))
		{
			ListView->ClearSelection();
		}
		ListView->RequestListRefresh();
	}
}

void SPaper2DPlusFrameCueTypePicker::SetSearchText(const FText& Text)
{
	SearchText = Text.ToString();
	ApplyFilter();
}

int32 SPaper2DPlusFrameCueTypePicker::CountFiltered(const bool bPlaceable) const
{
	int32 Count = 0;
	for (const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>& Item : FilteredItems)
	{
		Count += Item.IsValid() && Item->bPlaceable == bPlaceable ? 1 : 0;
	}
	return Count;
}

int32 SPaper2DPlusFrameCueTypePicker::GenerateRowsForViewportForTests(
	const FVector2D& ViewportSize)
{
	if (!ListView.IsValid())
	{
		return 0;
	}
	ListView->SlatePrepass(1.0f);
	ListView->Tick(
		FGeometry::MakeRoot(ViewportSize, FSlateLayoutTransform()),
		FPlatformTime::Seconds(),
		0.0f);
	return ListView->GetNumGeneratedChildren();
}

TSharedRef<ITableRow> SPaper2DPlusFrameCueTypePicker::GenerateRow(
	TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const bool bPlaceable = Item.IsValid() && Item->bPlaceable;
	const bool bRepairable = Item.IsValid() && Item->bRepairable;
	const bool bRange = Item.IsValid()
		&& Item->Type.Kind == EPaper2DPlusFrameCueTypeKind::Range;
	const FText Label = Item.IsValid()
		? (Item->Type.PickerLabel.IsEmpty()
			? Item->Type.DisplayName
			: Item->Type.PickerLabel)
		: FText::GetEmpty();
	const FText Reason = Item.IsValid() ? Item->Reason : FText::GetEmpty();

	FText ToolTip = Item.IsValid()
		? FText::FromString(Item->Type.ClassPath.ToString())
		: FText::GetEmpty();
	if (!bPlaceable)
	{
		ToolTip = bRepairable
			? FText::Format(
				LOCTEXT(
					"RejectedRowRepairTooltip",
					"{0}\n\n{1}\n\nClick to open this Cue Type in the Frame Cue Type editor and fix it."),
				ToolTip,
				Reason)
			: FText::Format(
				LOCTEXT("RejectedRowTooltip", "{0}\n\n{1}"),
				ToolTip,
				Reason);
	}

	// A rejected row is presented disabled but stays clickable on purpose: Slate's disabled state
	// swallows input, and the whole point of listing the row is that activating it opens the repair.
	// Only a row with nothing to open is genuinely inert.
	return SNew(STableRow<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>>, OwnerTable)
		.Padding(FMargin(0.0f, 1.0f))
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.IsEnabled(bPlaceable || bRepairable)
			.ToolTipText(ToolTip)
			.OnClicked(this, &SPaper2DPlusFrameCueTypePicker::HandleRowActivated, Item)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(Label)
						.ColorAndOpacity(bPlaceable
							? FSlateColor::UseForeground()
							: FSlateColor::UseSubduedForeground())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(8.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(bPlaceable
							? (bRange
								? LOCTEXT("CueTypeRangeBadge", "CUE STATE")
								: LOCTEXT("CueTypeMomentBadge", "CUE"))
							: LOCTEXT("CueTypeNeedsFixBadge", "NEEDS FIX"))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(Reason)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility(bPlaceable ? EVisibility::Collapsed : EVisibility::Visible)
				]
			]
		];
}

FReply SPaper2DPlusFrameCueTypePicker::HandleRowActivated(
	TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item)
{
	if (!Item.IsValid())
	{
		return FReply::Handled();
	}
	return Item->bPlaceable ? PickType(Item) : RepairType(Item);
}

FReply SPaper2DPlusFrameCueTypePicker::PickType(
	TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item)
{
	// Fail closed: only a row discovery classified Ready may ever become a placement.
	if (!Item.IsValid() || !Item->bPlaceable)
	{
		return FReply::Handled();
	}
	UClass* CueClass = Item->Type.Class.Get();
	if (IsValid(CueClass)
		&& !CueClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)
		&& OnPickType)
	{
		// The menu owns this widget. Copy callback-owned state before dismissing because menu
		// teardown can synchronously destroy `this`; invoke only the local copy afterward.
		TFunction<void(UClass*)> PickCallback = OnPickType;
		FSlateApplication::Get().DismissAllMenus();
		PickCallback(CueClass);
	}
	return FReply::Handled();
}

FReply SPaper2DPlusFrameCueTypePicker::RepairType(
	TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item)
{
	if (!Item.IsValid() || !Item->bRepairable || !OnRepairType)
	{
		return FReply::Handled();
	}
	// Same menu-teardown discipline as PickType: copy the callback and the descriptor by value.
	TFunction<void(const FPaper2DPlusFrameCueTypeDescriptor&)> RepairCallback = OnRepairType;
	const FPaper2DPlusFrameCueTypeDescriptor Descriptor = Item->Type;
	FSlateApplication::Get().DismissAllMenus();
	RepairCallback(Descriptor);
	return FReply::Handled();
}

FReply SPaper2DPlusFrameCueTypePicker::CreateType()
{
	if (OnCreateType)
	{
		TFunction<void()> CreateCallback = OnCreateType;
		FSlateApplication::Get().DismissAllMenus();
		CreateCallback();
	}
	return FReply::Handled();
}

FReply SPaper2DPlusFrameCueTypePicker::HandlePickerKeyDown(
	const FGeometry& /*Geometry*/,
	const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		FSlateApplication::Get().DismissAllMenus();
		return FReply::Handled();
	}

	// SSearchBox may defer OnTextChanged until after this key event. Synchronize from
	// the live widget before navigation or commit so a fast type+Enter cannot place a
	// Cue Type from the stale, pre-search result set.
	if (SearchBox.IsValid())
	{
		const FString LiveSearchText = SearchBox->GetText().ToString();
		if (LiveSearchText != SearchText)
		{
			SearchText = LiveSearchText;
			ApplyFilter();
		}
	}

	if (!ListView.IsValid())
	{
		return FReply::Unhandled();
	}

	if (Key == EKeys::Enter)
	{
		const TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> SelectedItems =
			ListView->GetSelectedItems();
		const TSharedPtr<FPaper2DPlusFrameCueTypePickerItem> Item = SelectedItems.Num() > 0
			? SelectedItems[0]
			: (FilteredItems.Num() > 0 ? FilteredItems[0] : nullptr);
		return Item.IsValid() ? HandleRowActivated(Item) : FReply::Handled();
	}

	if (Key != EKeys::Up && Key != EKeys::Down)
	{
		return FReply::Unhandled();
	}

	const TArray<TSharedPtr<FPaper2DPlusFrameCueTypePickerItem>> SelectedItems =
		ListView->GetSelectedItems();
	const int32 CurrentIndex = SelectedItems.Num() > 0
		? FilteredItems.IndexOfByKey(SelectedItems[0])
		: INDEX_NONE;
	const int32 NextIndex =
		Paper2DPlusFrameCueEditorAuthoring::ResolveCueTypePickerNavigationIndex(
			CurrentIndex,
			FilteredItems.Num(),
			Key == EKeys::Down ? 1 : -1);
	if (FilteredItems.IsValidIndex(NextIndex))
	{
		ListView->SetSelection(FilteredItems[NextIndex], ESelectInfo::Direct);
		ListView->RequestScrollIntoView(FilteredItems[NextIndex]);
	}
	return FReply::Handled();
}

FReply SPaper2DPlusFrameCueTypePicker::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& KeyEvent)
{
	return HandlePickerKeyDown(MyGeometry, KeyEvent);
}

FReply SPaper2DPlusFrameCueTypePicker::RetryDiscovery()
{
	RefreshDiscovery(true);
	return FReply::Handled();
}

EVisibility SPaper2DPlusFrameCueTypePicker::GetEmptyStateVisibility() const
{
	return FilteredItems.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SPaper2DPlusFrameCueTypePicker::GetStatusText() const
{
	using namespace Paper2DPlusFrameCueEditorAuthoring;
	const int32 PlaceableFiltered = CountFiltered(/*bPlaceable*/ true);
	const int32 RejectedFiltered = CountFiltered(/*bPlaceable*/ false);
	switch (ResolveCueTypePickerStatus(
		ReadyTypeCount,
		PlaceableFiltered,
		RejectedTypeCount,
		DiscoveryErrorCount,
		!SearchText.IsEmpty()))
	{
	case EPaper2DPlusFrameCueTypePickerStatus::ReadyWithDiscoveryErrors:
		return FText::Format(
			LOCTEXT(
				"CueTypePickerReadyWithErrors",
				"{0} reusable Cue Types; {1} saved types could not be loaded. Refresh to retry."),
			PlaceableFiltered,
			DiscoveryErrorCount);
	case EPaper2DPlusFrameCueTypePickerStatus::Ready:
		return RejectedFiltered > 0
			? FText::Format(
				LOCTEXT(
					"CueTypePickerReadyCountWithRepairs",
					"{0} reusable Cue Types; {1} need a fix — click one to open it."),
				PlaceableFiltered,
				RejectedFiltered)
			: FText::Format(
				LOCTEXT("CueTypePickerReadyCount", "{0} reusable Cue Types"),
				PlaceableFiltered);
	case EPaper2DPlusFrameCueTypePickerStatus::NoSearchMatch:
		return RejectedFiltered > 0
			? FText::Format(
				LOCTEXT(
					"CueTypePickerNoSearchResultsWithRepairs",
					"No ready Cue Types match this search. {0} matching types need a fix — click one to open it."),
				RejectedFiltered)
			: LOCTEXT("CueTypePickerNoSearchResults", "No Cue Types match this search.");
	case EPaper2DPlusFrameCueTypePickerStatus::DiscoveryError:
		return FText::Format(
			LOCTEXT(
				"CueTypePickerDiscoveryError",
				"Cue Type discovery could not load {0} saved types. Refresh to retry or create a new type."),
			DiscoveryErrorCount);
	case EPaper2DPlusFrameCueTypePickerStatus::NeedsRepair:
		return FText::Format(
			LOCTEXT(
				"CueTypePickerRejectedOnly",
				"No ready Cue Types. {0} saved types need compile, save, or repair — click one to open it."),
			RejectedTypeCount);
	case EPaper2DPlusFrameCueTypePickerStatus::FirstRun:
	default:
		return LOCTEXT(
			"CueTypePickerFirstRun",
			"No reusable Cue Types yet. Create the first one above.");
	}
}

EActiveTimerReturnType SPaper2DPlusFrameCueTypePicker::RefreshWhileOpen(
	double /*CurrentTime*/,
	float /*DeltaTime*/)
{
	RefreshDiscovery(false);
	return EActiveTimerReturnType::Continue;
}

#undef LOCTEXT_NAMESPACE
