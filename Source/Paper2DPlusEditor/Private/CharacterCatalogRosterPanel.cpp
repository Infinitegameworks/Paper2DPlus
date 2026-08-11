// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalogRosterPanel.h"

#include "ContentBrowserModule.h"
#include "EditorCanvasUtils.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameplayTagsEditorModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "AnimationTagChipUtils.h" // SMenuHostedTagPickerGuard
#include "ProfileCard.h"
#include "ProfileRelationshipService.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STileView.h"

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagPicker.h"
#endif

#define LOCTEXT_NAMESPACE "Paper2DPlusCharacterCatalogRoster"

namespace
{
	// The retention cap moved onto the panel so the shared thumbnail budget can be sized from it.

	enum class ECatalogCardThumbnailSource : uint8
	{
		ResidentFlipbook,
		AsyncProfile,
		Placeholder
	};

	const UPaper2DPlusCharacterProfileAsset* FindResidentCharacterProfile(
		const FPaper2DPlusCharacterCatalogEditorRow& Row)
	{
		const UPaper2DPlusCharacterProfileAsset* Character = Row.ResidentCharacterProfile.Get();
		if (!Character)
		{
			Character = Row.Entry.CharacterProfile.Get();
		}
		return Character;
	}

	const FFlipbookProfileEntry* FindDesiredPreviewEntry(
		const UPaper2DPlusCharacterProfileAsset& Character)
	{
		if (!Character.ThumbnailFlipbookName.IsEmpty())
		{
			return Character.Flipbooks.FindByPredicate([&Character](const FFlipbookProfileEntry& Entry)
			{
				return Entry.Identity.FlipbookName.Equals(
					Character.ThumbnailFlipbookName, ESearchCase::IgnoreCase)
					&& !Entry.Identity.Flipbook.IsNull();
			});
		}

		return Character.Flipbooks.FindByPredicate([](const FFlipbookProfileEntry& Entry)
		{
			return !Entry.Identity.Flipbook.IsNull();
		});
	}

	UPaperFlipbook* FindResidentPreviewFlipbook(
		const FPaper2DPlusCharacterCatalogEditorRow& Row)
	{
		const UPaper2DPlusCharacterProfileAsset* Character = FindResidentCharacterProfile(Row);
		if (!Character)
		{
			return nullptr;
		}
		const FFlipbookProfileEntry* DesiredEntry = FindDesiredPreviewEntry(*Character);
		return DesiredEntry ? DesiredEntry->Identity.Flipbook.Get() : nullptr;
	}

	ECatalogCardThumbnailSource ResolveThumbnailSource(
		const FPaper2DPlusCharacterCatalogEditorRow& Row)
	{
		if (FindResidentPreviewFlipbook(Row))
		{
			return ECatalogCardThumbnailSource::ResidentFlipbook;
		}
		if (Row.CharacterAssetData.IsValid() && !Row.CharacterPath.IsNull())
		{
			return ECatalogCardThumbnailSource::AsyncProfile;
		}
		return ECatalogCardThumbnailSource::Placeholder;
	}

	FSlateColor CatalogStatusColor(EPaper2DPlusCatalogRowStatus Status)
	{
		switch (Status)
		{
		case EPaper2DPlusCatalogRowStatus::RequiredMissing:
		case EPaper2DPlusCatalogRowStatus::MissingAsset:
		case EPaper2DPlusCatalogRowStatus::Error:
			return FSlateColor(FLinearColor(0.95f, 0.36f, 0.30f));
		case EPaper2DPlusCatalogRowStatus::Warning:
			return FSlateColor(FLinearColor(0.95f, 0.68f, 0.25f));
		case EPaper2DPlusCatalogRowStatus::Complete:
			return FSlateColor(FLinearColor(0.55f, 0.82f, 0.58f));
		default:
			return FSlateColor(FLinearColor(0.70f, 0.70f, 0.74f));
		}
	}

	FText CompactStatusLabel(EPaper2DPlusCatalogRowStatus Status)
	{
		switch (Status)
		{
		case EPaper2DPlusCatalogRowStatus::Complete: return LOCTEXT("CardStatusComplete", "Complete");
		case EPaper2DPlusCatalogRowStatus::OptionalMissing: return LOCTEXT("CardStatusOptional", "Optional missing");
		case EPaper2DPlusCatalogRowStatus::RequiredMissing: return LOCTEXT("CardStatusRequired", "Required missing");
		case EPaper2DPlusCatalogRowStatus::MissingAsset: return LOCTEXT("CardStatusMissingAsset", "Missing asset");
		case EPaper2DPlusCatalogRowStatus::Warning: return LOCTEXT("CardStatusWarning", "Warning");
		default: return LOCTEXT("CardStatusError", "Error");
		}
	}

	/** "3/20 authored" — or an explicit unknown, never a silent 0% and never a vacuous complete. */
	FText AuthoringProgressLabel(const FPaper2DPlusCatalogAuthoringProgress& Progress)
	{
		if (!Progress.IsKnown())
		{
			return LOCTEXT("CardProgressUnknown", "Progress unknown");
		}
		return FText::Format(
			LOCTEXT("CardProgress", "{0}/{1} authored"),
			FText::AsNumber(Progress.Done),
			FText::AsNumber(Progress.Total));
	}

	FText AuthoringProgressTooltip(const FPaper2DPlusCatalogAuthoringProgress& Progress)
	{
		if (!Progress.IsKnown())
		{
			return LOCTEXT(
				"CardProgressUnknownTip",
				"No completion checklist data was found for this character's assets. Open each asset's Completion panel and tick its criteria, then save; the roster reads the saved checklist without loading the asset.");
		}
		const FText Base = FText::Format(
			LOCTEXT(
				"CardProgressTip",
				"{0} of {1} checklist criteria ticked across this character's Profile and assigned companions ({2}%)."),
			FText::AsNumber(Progress.Done),
			FText::AsNumber(Progress.Total),
			FText::AsNumber(FMath::RoundToInt(Progress.GetFraction() * 100.0f)));
		if (Progress.SourcesMissingData > 0)
		{
			return FText::Format(
				LOCTEXT(
					"CardProgressTipPartial",
					"{0}\n{1} assigned asset(s) reported no checklist data, so this total is incomplete."),
				Base,
				FText::AsNumber(Progress.SourcesMissingData));
		}
		return Base;
	}

	FSlateColor AuthoringProgressColor(const FPaper2DPlusCatalogAuthoringProgress& Progress)
	{
		if (!Progress.IsKnown())
		{
			return FSlateColor(FLinearColor(0.55f, 0.55f, 0.58f));
		}
		if (Progress.IsFullyAuthored())
		{
			return FSlateColor(FLinearColor(0.55f, 0.82f, 0.58f));
		}
		return Progress.GetFraction() >= 0.5f
			? FSlateColor(FLinearColor(0.85f, 0.78f, 0.42f))
			: FSlateColor(FLinearColor(0.80f, 0.58f, 0.38f));
	}

	FText CompactTagSummary(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> TagArray;
		Tags.GetGameplayTagArray(TagArray);
		if (TagArray.IsEmpty())
		{
			return LOCTEXT("CardNoTags", "No tags");
		}
		FString Leaf = TagArray[0].GetTagName().ToString();
		int32 DotIndex = INDEX_NONE;
		if (Leaf.FindLastChar(TEXT('.'), DotIndex))
		{
			Leaf = Leaf.Mid(DotIndex + 1);
		}
		return TagArray.Num() == 1
			? FText::FromString(Leaf)
			: FText::Format(LOCTEXT("CardTagsOverflow", "{0}  +{1}"), FText::FromString(Leaf), FText::AsNumber(TagArray.Num() - 1));
	}
}

TSharedRef<FCatalogCharacterDragDropOp> FCatalogCharacterDragDropOp::New(
	const TArray<FSoftObjectPath>& InCharacterPaths,
	FName InSourceGroup,
	UPaper2DPlusCharacterCatalogAsset* InSourceCatalog)
{
	TSharedRef<FCatalogCharacterDragDropOp> Op = MakeShareable(new FCatalogCharacterDragDropOp());
	Op->CharacterPaths = InCharacterPaths;
	Op->SourceGroup = InSourceGroup;
	Op->SourceCatalog = InSourceCatalog;
	Op->DefaultHoverText = InCharacterPaths.Num() == 1
		? LOCTEXT("DragOneCharacter", "Move 1 character")
		: FText::Format(LOCTEXT("DragManyCharacters", "Move {0} characters"), FText::AsNumber(InCharacterPaths.Num()));
	Op->Construct();
	return Op;
}

TSharedPtr<SWidget> FCatalogCharacterDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(6.0f, 2.0f))
		[
			SNew(STextBlock).Text(DefaultHoverText)
		];
}

TSharedRef<FCatalogGroupRowDragDropOp> FCatalogGroupRowDragDropOp::New(
	FName InGroupName,
	UPaper2DPlusCharacterCatalogAsset* InSourceCatalog)
{
	TSharedRef<FCatalogGroupRowDragDropOp> Op = MakeShareable(new FCatalogGroupRowDragDropOp());
	Op->SourceGroupName = InGroupName;
	Op->SourceCatalog = InSourceCatalog;
	Op->DefaultHoverText = FText::Format(
		LOCTEXT("DragGroupRow", "Move group: {0}"), FText::FromName(InGroupName));
	Op->Construct();
	return Op;
}

TSharedPtr<SWidget> FCatalogGroupRowDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(6.0f, 2.0f))
		[
			SNew(STextBlock).Text(DefaultHoverText)
		];
}

void SCharacterCatalogRosterPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	check(Model.IsValid());
	if (Model->GetSelectedCharacterPath().IsNull() && !Model->GetVisibleRows().IsEmpty())
	{
		Model->SelectCharacter(Model->GetVisibleRows()[0]->CharacterPath);
	}
	ModelChangedHandle = Model->OnModelChanged().AddSP(this, &SCharacterCatalogRosterPanel::HandleModelChanged);
	UpdateRosterSummary();

	const TArray<FCatalogRowPtr>* VisibleRows = &Model->GetVisibleRows();
	// Rail left, roster right. The tour asserts the tile grid sits LEFT of the Details focus target, so
	// the rail must stay a narrow left column inside this panel rather than a separate dock.
	TSharedRef<SWidget> RosterBody = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(5.0f, 3.0f, 5.0f, 1.0f)
		[
			BuildAuthorityBanner()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(5.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchCharacters", "Search characters…"))
				.InitialText(FText::FromString(Model->GetFilters().SearchText))
				.OnTextChanged_Lambda([WeakModel = TWeakPtr<FCharacterCatalogEditorModel>(Model)](const FText& Text)
				{
					if (const TSharedPtr<FCharacterCatalogEditorModel> Pinned = WeakModel.Pin())
					{
						Pinned->SetSearchText(Text.ToString());
					}
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				BuildCommandBar()
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(5.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				BuildFilters()
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SCharacterCatalogRosterPanel::GetRosterSummaryText)
				.Justification(ETextJustify::Right)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(2.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(CharacterTiles, STileView<FCatalogRowPtr>)
					.ListItemsSource(VisibleRows)
					.SelectionMode(ESelectionMode::Multi)
					.ItemWidth(SProfileCard::GetCanonicalCardWidth() + 8.0f)
					.ItemHeight(SProfileCard::GetCanonicalCompactCardHeight() + 14.0f)
					.OnGenerateTile(this, &SCharacterCatalogRosterPanel::GenerateCharacterTile)
					.OnSelectionChanged(this, &SCharacterCatalogRosterPanel::HandleCharacterTileSelected)
					.OnContextMenuOpening(this, &SCharacterCatalogRosterPanel::HandleTileContextMenu)
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoCharacterCards", "No characters match this view."))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
					.Visibility_Lambda([this]()
					{
						return Model->GetVisibleRows().IsEmpty()
							? EVisibility::HitTestInvisible
							: EVisibility::Collapsed;
					})
				]
			]
		];

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot().Value(0.2f)
		[
			BuildGroupRail()
		]
		+ SSplitter::Slot().Value(0.8f)
		[
			RosterBody
		]
	];
	SynchronizeCardSelection();
}

SCharacterCatalogRosterPanel::~SCharacterCatalogRosterPanel()
{
	if (const TSharedPtr<FActiveTimerHandle> Timer = ExpectedAnimationTagsCommitTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	ExpectedAnimationTagsCommitTimerHandle.Reset();
	PendingExpectedAnimationTagsCommit.Reset();
	if (Model.IsValid() && ModelChangedHandle.IsValid())
	{
		Model->OnModelChanged().Remove(ModelChangedHandle);
	}
}

void SCharacterCatalogRosterPanel::HandleModelChanged()
{
	UpdateRosterSummary();
	if (CharacterTiles.IsValid()) CharacterTiles->RequestListRefresh();
	RefreshGroupRail();
	RefreshExpectedAnimationTagsEditors();
	SynchronizeCardSelection();
}

void SCharacterCatalogRosterPanel::UpdateRosterSummary()
{
	// Counts REAL authoring progress. The old summary counted required-companion completion, which was
	// vacuously true for every row whose requirements were unconfigured.
	int32 FullyAuthored = 0;
	int32 Unknown = 0;
	int32 Done = 0;
	int32 Total = 0;
	for (const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>& Row : Model->GetRows())
	{
		if (!Row.IsValid())
		{
			continue;
		}
		if (!Row->AuthoringProgress.IsKnown())
		{
			++Unknown;
			continue;
		}
		Done += Row->AuthoringProgress.Done;
		Total += Row->AuthoringProgress.Total;
		if (Row->AuthoringProgress.IsFullyAuthored())
		{
			++FullyAuthored;
		}
	}

	FText ProgressPart;
	if (Total > 0)
	{
		ProgressPart = FText::Format(
			LOCTEXT("RosterSummaryProgress", "{0} fully authored | {1}% of criteria ticked"),
			FText::AsNumber(FullyAuthored),
			FText::AsNumber(FMath::RoundToInt(
				static_cast<float>(Done) / static_cast<float>(Total) * 100.0f)));
	}
	else
	{
		ProgressPart = LOCTEXT("RosterSummaryNoProgress", "no checklist data yet");
	}
	if (Unknown > 0)
	{
		ProgressPart = FText::Format(
			LOCTEXT("RosterSummaryUnknownSuffix", "{0} | {1} unknown"),
			ProgressPart,
			FText::AsNumber(Unknown));
	}

	CachedRosterSummary = FText::Format(
		LOCTEXT("RosterSummary", "Showing {0} of {1} characters | {2}"),
		FText::AsNumber(Model->GetVisibleRows().Num()),
		FText::AsNumber(Model->GetRows().Num()),
		ProgressPart);
}

void SCharacterCatalogRosterPanel::SynchronizeCardSelection()
{
	if (!CharacterTiles.IsValid()) return;
	const FSoftObjectPath SelectedPath = Model->GetSelectedCharacterPath();
	const FCatalogRowPtr* Match = Model->GetVisibleRows().FindByPredicate([&SelectedPath](const FCatalogRowPtr& Row)
	{
		return Row.IsValid() && Row->CharacterPath == SelectedPath;
	});
	if (Match)
	{
		// Preserve an in-flight multi-selection: only re-anchor the tiles when the model's primary
		// selection is not already part of it, so Ctrl-click cohorts survive model broadcasts.
		if (!CharacterTiles->IsItemSelected(*Match))
		{
			CharacterTiles->SetSelection(*Match, ESelectInfo::Direct);
		}
		CharacterTiles->RequestScrollIntoView(*Match);
		return;
	}
	CharacterTiles->ClearSelection();
}

void SCharacterCatalogRosterPanel::RefreshForTests()
{
	HandleModelChanged();
}

void SCharacterCatalogRosterPanel::RefreshSourcesForTests()
{
	HandleRefresh();
}

int32 SCharacterCatalogRosterPanel::GetVisibleCharacterCountForTests() const
{
	return Model.IsValid() ? Model->GetVisibleRows().Num() : 0;
}

int32 SCharacterCatalogRosterPanel::GenerateTilesForViewportForTests(
	const FVector2D& ViewportSize)
{
	if (!CharacterTiles.IsValid())
	{
		return 0;
	}
	// A table view needs a settled frame before it reports its virtualized surface. Construct queues
	// a scroll-into-view whenever a row is already selected, and STableViewBase defers that request
	// on any frame that has not generated a widget yet: that frame generates NOTHING and re-arms the
	// refresh for the next one (measured under UE 5.8: frame 0 = 0 cards, frame 1 = 3 cards at the
	// scrolled offset). Request the refresh the way the Combat attack-card seam does and pump
	// bounded frames the way the real editor does, so this probe measures the settled roster surface
	// rather than a mid-flight one.
	const FGeometry ViewportGeometry = FGeometry::MakeRoot(ViewportSize, FSlateLayoutTransform());
	CharacterTiles->RequestListRefresh();
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		CharacterTiles->SlatePrepass(1.0f);
		CharacterTiles->Tick(ViewportGeometry, FPlatformTime::Seconds(), 0.0f);
		if (CharacterTiles->GetNumGeneratedChildren() > 0 && !CharacterTiles->IsPendingRefresh())
		{
			break;
		}
	}
	return CharacterTiles->GetNumGeneratedChildren();
}

bool SCharacterCatalogRosterPanel::WasAsyncThumbnailRequestedForTests(
	const FSoftObjectPath& CharacterPath) const
{
	const FAsyncThumbnailLoadState* State = AsyncThumbnailLoads.Find(CharacterPath);
	return State && State->bRequested;
}

bool SCharacterCatalogRosterPanel::RequestThumbnailForPathForTests(
	const FSoftObjectPath& CharacterPath)
{
	const FCatalogRowPtr Row = Model.IsValid() ? Model->FindRow(CharacterPath) : nullptr;
	RequestVisibleThumbnailAsync(Row);
	return AsyncThumbnailLoads.Contains(CharacterPath);
}

bool SCharacterCatalogRosterPanel::HasLoadedAsyncThumbnailObjectsForTests(
	const FSoftObjectPath& CharacterPath) const
{
	const FAsyncThumbnailLoadState* State = AsyncThumbnailLoads.Find(CharacterPath);
	return State && State->ProfileKeepAlive.Get() && State->FlipbookKeepAlive.Get();
}

FSoftObjectPath SCharacterCatalogRosterPanel::GetDesiredThumbnailFlipbookPathForTests(
	const FSoftObjectPath& CharacterPath) const
{
	const FCatalogRowPtr Row = Model.IsValid() ? Model->FindRow(CharacterPath) : nullptr;
	const UPaper2DPlusCharacterProfileAsset* Character = Row.IsValid()
		? FindResidentCharacterProfile(*Row)
		: nullptr;
	const FFlipbookProfileEntry* DesiredEntry = Character
		? FindDesiredPreviewEntry(*Character)
		: nullptr;
	return DesiredEntry ? DesiredEntry->Identity.Flipbook.ToSoftObjectPath() : FSoftObjectPath();
}

SCharacterCatalogRosterPanel::FAsyncThumbnailLoadState&
SCharacterCatalogRosterPanel::TouchAsyncThumbnailLoad(const FSoftObjectPath& CharacterPath)
{
	// The shared budget inserts, re-ranks, and trims in one step.
	return AsyncThumbnailLoads.Touch(CharacterPath);
}

void SCharacterCatalogRosterPanel::RequestVisibleThumbnailAsync(const FCatalogRowPtr& Row)
{
	if (!Row.IsValid() || Row->CharacterPath.IsNull()
		|| ResolveThumbnailSource(*Row) != ECatalogCardThumbnailSource::AsyncProfile)
	{
		return;
	}

	FAsyncThumbnailLoadState& State = TouchAsyncThumbnailLoad(Row->CharacterPath);
	if (State.bRequested)
	{
		return;
	}
	State.bRequested = true;
	State.bFailed = false;

	if (Row->CharacterPath.ResolveObject())
	{
		HandleThumbnailProfileLoaded(Row->CharacterPath);
		return;
	}

	State.ProfileHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Row->CharacterPath,
		FStreamableDelegate::CreateSP(
			this,
			&SCharacterCatalogRosterPanel::HandleThumbnailProfileLoaded,
			Row->CharacterPath),
		FStreamableManager::DefaultAsyncLoadPriority,
		false,
		false,
		TEXT("Paper2DPlus Catalog visible Character thumbnail"));
	if (!State.ProfileHandle.IsValid())
	{
		State.bFailed = true;
	}
}

void SCharacterCatalogRosterPanel::HandleThumbnailProfileLoaded(
	FSoftObjectPath CharacterPath)
{
	FAsyncThumbnailLoadState* State = AsyncThumbnailLoads.Find(CharacterPath);
	UPaper2DPlusCharacterProfileAsset* Character =
		Cast<UPaper2DPlusCharacterProfileAsset>(CharacterPath.ResolveObject());
	if (!State || !Character)
	{
		if (State)
		{
			State->bFailed = true;
		}
		RefreshGeneratedTiles();
		return;
	}
	State->ProfileKeepAlive.Reset(Character);

	const FFlipbookProfileEntry* PreviewEntry = FindDesiredPreviewEntry(*Character);
	if (!PreviewEntry)
	{
		State->bFailed = true;
		RefreshGeneratedTiles();
		return;
	}

	const FSoftObjectPath FlipbookPath = PreviewEntry->Identity.Flipbook.ToSoftObjectPath();
	if (Cast<UPaperFlipbook>(FlipbookPath.ResolveObject()))
	{
		HandleThumbnailFlipbookLoaded(CharacterPath, FlipbookPath);
		return;
	}

	State->FlipbookHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		FlipbookPath,
		FStreamableDelegate::CreateSP(
			this,
			&SCharacterCatalogRosterPanel::HandleThumbnailFlipbookLoaded,
			CharacterPath,
			FlipbookPath),
		FStreamableManager::DefaultAsyncLoadPriority,
		false,
		false,
		TEXT("Paper2DPlus Catalog visible flipbook thumbnail"));
	if (!State->FlipbookHandle.IsValid())
	{
		State->bFailed = true;
		RefreshGeneratedTiles();
	}
}

void SCharacterCatalogRosterPanel::HandleThumbnailFlipbookLoaded(
	FSoftObjectPath CharacterPath,
	FSoftObjectPath FlipbookPath)
{
	if (FAsyncThumbnailLoadState* State = AsyncThumbnailLoads.Find(CharacterPath))
	{
		UPaperFlipbook* Flipbook = Cast<UPaperFlipbook>(FlipbookPath.ResolveObject());
		State->bFailed = Flipbook == nullptr;
		State->FlipbookKeepAlive.Reset(Flipbook);
	}
	RefreshGeneratedTiles();
}

void SCharacterCatalogRosterPanel::RefreshGeneratedTiles()
{
	if (CharacterTiles.IsValid())
	{
		CharacterTiles->RequestListRefresh();
	}
}

void SCharacterCatalogRosterPanel::ResetAsyncThumbnailLoads()
{
	AsyncThumbnailLoads.Reset();
	RefreshGeneratedTiles();
}

FName SCharacterCatalogRosterPanel::GetThumbnailSourceForTests(
	const FSoftObjectPath& CharacterPath) const
{
	const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> Row = Model.IsValid()
		? Model->FindRow(CharacterPath)
		: nullptr;
	if (!Row.IsValid())
	{
		return TEXT("MissingRow");
	}
	if (const FAsyncThumbnailLoadState* State = AsyncThumbnailLoads.Find(CharacterPath))
	{
		if (Cast<UPaperFlipbook>(State->FlipbookKeepAlive.Get()))
		{
			return TEXT("ResidentFlipbook");
		}
	}
	const ECatalogCardThumbnailSource Source = ResolveThumbnailSource(*Row);
	switch (Source)
	{
	case ECatalogCardThumbnailSource::ResidentFlipbook:
		return TEXT("ResidentFlipbook");
	case ECatalogCardThumbnailSource::AsyncProfile:
		return TEXT("AsyncProfile");
	default:
		return TEXT("Placeholder");
	}
}

TSharedRef<ITableRow> SCharacterCatalogRosterPanel::GenerateCharacterTile(
	FCatalogRowPtr Row,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!Row.IsValid())
	{
		return SNew(STableRow<FCatalogRowPtr>, OwnerTable)
		[
			SNew(STextBlock).Text(LOCTEXT("InvalidCharacterCard", "Invalid character"))
		];
	}

	TSharedRef<SWidget> Thumbnail = SNullWidget::NullWidget;
	FAsyncThumbnailLoadState* RetainedState = Row->CharacterPath.IsNull()
		? nullptr
		: &TouchAsyncThumbnailLoad(Row->CharacterPath);
	UPaperFlipbook* RetainedFlipbook = RetainedState
		? Cast<UPaperFlipbook>(RetainedState->FlipbookKeepAlive.Get())
		: nullptr;
	const ECatalogCardThumbnailSource ThumbnailSource = RetainedFlipbook
		? ECatalogCardThumbnailSource::ResidentFlipbook
		: ResolveThumbnailSource(*Row);
	if (ThumbnailSource == ECatalogCardThumbnailSource::ResidentFlipbook)
	{
		Thumbnail = SNew(SFlipbookThumbnail).Flipbook(
			RetainedFlipbook ? RetainedFlipbook : FindResidentPreviewFlipbook(*Row));
	}
	else if (ThumbnailSource == ECatalogCardThumbnailSource::AsyncProfile)
	{
		// Tile generation is the virtualization boundary: load only visible Profiles and their selected
		// thumbnail flipbooks, asynchronously, then rebuild the visible tile as the same direct
		// SFlipbookThumbnail used by Character Profile. The small retained LRU keeps scrolling bounded.
		RequestVisibleThumbnailAsync(Row);
		const FAsyncThumbnailLoadState* LoadState = AsyncThumbnailLoads.Find(Row->CharacterPath);
		if (UPaperFlipbook* LoadedFlipbook = LoadState
			? Cast<UPaperFlipbook>(LoadState->FlipbookKeepAlive.Get())
			: nullptr)
		{
			Thumbnail = SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook);
		}
		else
		{
			const FText LoadingText = LoadState && LoadState->bFailed
				? LOCTEXT("CharacterProfileThumbnailUnavailable", "No preview")
				: LOCTEXT("CharacterProfileThumbnailLoading", "Loading art…");
			Thumbnail = SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(LoadingText)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 7))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
				];
		}
	}
	else
	{
		Thumbnail = SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CharacterProfileThumbnailFallback", "No preview"))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 7))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
			];
	}

	const FSoftObjectPath CharacterPath = Row->CharacterPath;
	const FText RequirementText = Row->Completion.RequiredCount > 0
		? FText::Format(
			LOCTEXT("CardCompletion", "{0}/{1} required companions"),
			FText::AsNumber(Row->Completion.PresentRequiredCount),
			FText::AsNumber(Row->Completion.RequiredCount))
		: LOCTEXT("CardNoRequirements", "No required companions");
	const FText ProgressText = AuthoringProgressLabel(Row->AuthoringProgress);
	const FText ProgressTooltip = AuthoringProgressTooltip(Row->AuthoringProgress);
	const FText CompactStatus = CompactStatusLabel(Row->Status);
	// Complete/OptionalMissing describe companion slots, not authoring work: on those the card shows
	// progress instead, so a fully-assigned but unauthored character can never read as finished.
	const bool bStatusIsProblem = Row->Status != EPaper2DPlusCatalogRowStatus::Complete
		&& Row->Status != EPaper2DPlusCatalogRowStatus::OptionalMissing;
	const FText TagsText = CompactTagSummary(Row->Entry.Tags);
	const FText CardTooltip = FText::Format(
		LOCTEXT("CharacterCardTooltip", "{0}\n{1}\n{2}\n{3}\nTags: {4}\n{5}"),
		Row->DisplayName,
		FText::FromString(CharacterPath.ToString()),
		Row->StatusTooltip,
		ProgressTooltip,
		Row->Entry.Tags.IsEmpty() ? LOCTEXT("CharacterCardNoTagsTooltip", "(none)") : FText::FromString(Row->Entry.Tags.ToStringSimple()),
		RequirementText);
	auto IsSelected = [WeakModel = TWeakPtr<FCharacterCatalogEditorModel>(Model), CharacterPath]()
	{
		const TSharedPtr<FCharacterCatalogEditorModel> Pinned = WeakModel.Pin();
		return Pinned.IsValid() && Pinned->GetSelectedCharacterPath() == CharacterPath;
	};

	const TSharedRef<SWidget> Card = SNew(SBox)
		.WidthOverride(SProfileCard::GetCanonicalCardWidth())
		.HeightOverride(SProfileCard::GetCanonicalCompactCardHeight())
		.ToolTipText(CardTooltip)
		.AccessibleText(FText::Format(
			LOCTEXT("CharacterCardAccessible", "{0}. {1}. {2}. {3}. Tags: {4}."),
			Row->DisplayName, CompactStatus, ProgressText, RequirementText, TagsText))
		[
			SNew(SProfileCard)
			.IsSelected_Lambda([IsSelected]() { return IsSelected(); })
			[
				SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 4.0f, 0.0f, 4.0f)
					[
						SNew(SBox)
						.WidthOverride(SProfileCard::GetCanonicalThumbnailSize())
						.HeightOverride(SProfileCard::GetCanonicalThumbnailSize())
						[
							Thumbnail
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(2.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(Row->DisplayName)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
						.Justification(ETextJustify::Center)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
					// One text line, because the card is a fixed 150x142 with a 64px thumbnail: a real
					// problem outranks progress, otherwise this is the authoring meter's label.
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(2.0f, 2.0f, 2.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(bStatusIsProblem ? CompactStatus : ProgressText)
						.ToolTipText(bStatusIsProblem ? Row->StatusTooltip : ProgressTooltip)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
						.ColorAndOpacity(bStatusIsProblem
							? CatalogStatusColor(Row->Status)
							: AuthoringProgressColor(Row->AuthoringProgress))
						.Justification(ETextJustify::Center)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(10.0f, 3.0f, 10.0f, 0.0f)
					[
						SNew(SBox)
						.HeightOverride(3.0f)
						.ToolTipText(ProgressTooltip)
						.Visibility(Row->AuthoringProgress.IsKnown()
							? EVisibility::HitTestInvisible
							: EVisibility::Hidden)
						[
							SNew(SProgressBar)
							.Percent(Row->AuthoringProgress.GetFraction())
							.FillColorAndOpacity(AuthoringProgressColor(Row->AuthoringProgress))
						]
					]
			]
		];

	return SNew(STableRow<FCatalogRowPtr>, OwnerTable)
		.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("ContentBrowser.AssetListView.TileTableRow")))
		.ShowSelection(false)
		.Padding(4.0f)
		.OnDragDetected_Lambda([this, CharacterPath](const FGeometry&, const FPointerEvent&) -> FReply
		{
			// Drag the whole selection when the grabbed card is part of it, otherwise just this card —
			// the same rule the flipbook browser's card drag uses.
			if (CharacterTiles.IsValid() && Model.IsValid())
			{
				if (const FCatalogRowPtr Row = Model->FindRow(CharacterPath);
					Row.IsValid() && !CharacterTiles->IsItemSelected(Row))
				{
					CharacterTiles->SetSelection(Row, ESelectInfo::Direct);
					Model->SelectCharacter(CharacterPath);
				}
			}
			if (const TSharedPtr<FDragDropOperation> Operation = BeginCharacterDrag())
			{
				return FReply::Handled().BeginDragDrop(Operation.ToSharedRef());
			}
			return FReply::Unhandled();
		})
		.OnCanAcceptDrop_Lambda([this](
			const FDragDropEvent& DragDropEvent,
			EItemDropZone DropZone,
			FCatalogRowPtr) -> TOptional<EItemDropZone>
		{
			// Card-on-card is only meaningful while the grid is scoped to one group: that is the only
			// view whose order IS the authored member order (RefilterRows sorts it), so a reorder here
			// moves what the designer can actually see.
			const TSharedPtr<FCatalogCharacterDragDropOp> CardOp =
				DragDropEvent.GetOperationAs<FCatalogCharacterDragDropOp>();
			UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Model.IsValid() ? Model->GetCatalog() : nullptr;
			if (!CardOp.IsValid() || !CatalogAsset || CardOp->SourceCatalog.Get() != CatalogAsset
				|| CardOp->SourceGroup.IsNone() || SelectedRailGroup.IsNone()
				|| !CardOp->SourceGroup.IsEqual(SelectedRailGroup, ENameCase::IgnoreCase)
				|| CardOp->CharacterPaths.Num() != 1)
			{
				return TOptional<EItemDropZone>();
			}
			return DropZone == EItemDropZone::BelowItem
				? EItemDropZone::BelowItem
				: EItemDropZone::AboveItem;
		})
		.OnAcceptDrop_Lambda([this](
			const FDragDropEvent& DragDropEvent,
			EItemDropZone DropZone,
			FCatalogRowPtr TargetRow) -> FReply
		{
			const TSharedPtr<FCatalogCharacterDragDropOp> CardOp =
				DragDropEvent.GetOperationAs<FCatalogCharacterDragDropOp>();
			if (!CardOp.IsValid() || !TargetRow.IsValid() || CardOp->CharacterPaths.Num() != 1)
			{
				return FReply::Unhandled();
			}
			HandleCardOnCardDrop(
				CardOp->CharacterPaths[0],
				TargetRow->CharacterPath,
				DropZone == EItemDropZone::BelowItem);
			return FReply::Handled();
		})
		[
			Card
		];
}

void SCharacterCatalogRosterPanel::HandleCharacterTileSelected(
	FCatalogRowPtr Row,
	ESelectInfo::Type SelectInfo)
{
	if (Row.IsValid()) Model->SelectCharacter(Row->CharacterPath);
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildGroupRail()
{
	RefreshGroupRail();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 2.0f, 4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("GroupsRailHeading", "Groups"))
			.ToolTipText(LOCTEXT(
				"GroupsRailTip",
				"Explicit ordered rosters. Click to scope the grid, drag characters onto a group to add them, and right-click for New/Delete. Tags never change membership."))
			.Font(FAppStyle::GetFontStyle("HeadingSmall"))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(1.0f)
			[
				SAssignNew(GroupRail, SListView<TSharedPtr<FName>>)
				.ListItemsSource(&GroupRailItems)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SCharacterCatalogRosterPanel::GenerateGroupRailRow)
				.OnSelectionChanged(this, &SCharacterCatalogRosterPanel::HandleGroupRailSelectionChanged)
				.OnContextMenuOpening(this, &SCharacterCatalogRosterPanel::HandleGroupRailContextMenu)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 3.0f)
		[
			SAssignNew(ExpectedAnimationTagsEditorBox, SBox)
			[
				BuildExpectedAnimationTagsEditors()
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 2.0f, 4.0f, 4.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("NewGroupRail", "+ New Group"))
			.ToolTipText(LOCTEXT("NewGroupRailTip", "Create a group. Its internal Blueprint name is derived from the label you type."))
			.OnClicked_Lambda([this]()
			{
				// Create with a placeholder label, then drop straight into inline rename so the designer
				// types the label once — the retired tab made you fill an internal name AND a label.
				if (CreateGroupFromRail(LOCTEXT("NewGroupDefaultLabel", "New Group")))
				{
					if (const TWeakPtr<SInlineEditableTextBlock>* Widget =
						GroupRailNameWidgets.Find(SelectedRailGroup))
					{
						if (const TSharedPtr<SInlineEditableTextBlock> Pinned = Widget->Pin())
						{
							Pinned->EnterEditingMode();
						}
					}
				}
				return FReply::Handled();
			})
		];
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildExpectedAnimationTagsEditors()
{
	BaseExpectedAnimationTagsCombo.Reset();
	GroupExpectedAnimationTagsCombo.Reset();

	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset =
		Model.IsValid() ? Model->GetCatalog() : nullptr;
	if (!CatalogAsset)
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SVerticalBox> Editors = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BaseExpectedAnimationTagsLabel", "Base Expected Animations"))
			.ToolTipText(LOCTEXT(
				"BaseExpectedAnimationTagsLabelTip",
				"Animation tags every character in this Catalog is expected to author."))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 3.0f)
		[
			BuildExpectedAnimationTagsControl(
				NAME_None,
				CatalogAsset->ExpectedAnimationTags,
				LOCTEXT("NoBaseExpectedAnimationTags", "None required"),
				LOCTEXT(
					"BaseExpectedAnimationTagsTip",
					"Add animation tags every character in this Catalog should author. Pick one tag at a time; remove tags with ×. Each gesture is one undoable edit."))
		];

	if (!SelectedRailGroup.IsNone())
	{
		const FPaper2DPlusCharacterCatalogGroup* Group = CatalogAsset->Groups.FindByPredicate(
			[this](const FPaper2DPlusCharacterCatalogGroup& Candidate)
		{
			return Candidate.GroupName.IsEqual(SelectedRailGroup, ENameCase::IgnoreCase);
		});
		if (Group)
		{
			Editors->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"GroupExpectedAnimationTagsLabel",
					"Additional Expected Animations"))
				.ToolTipText(LOCTEXT(
					"GroupExpectedAnimationTagsLabelTip",
					"Additional expected animation tags for characters in the selected group."))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
			Editors->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				BuildExpectedAnimationTagsControl(
					Group->GroupName,
					Group->AdditionalExpectedAnimationTags,
					LOCTEXT("NoGroupExpectedAnimationTags", "No group extras"),
					LOCTEXT(
						"GroupExpectedAnimationTagsTip",
						"Add animation tags expected from members of this group. Pick one tag at a time; remove tags with ×. Each gesture is one undoable edit."))
			];
		}
	}

	return Editors;
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildExpectedAnimationTagsControl(
	FName GroupName,
	const FGameplayTagContainer& Snapshot,
	const FText& EmptyText,
	const FText& Tooltip)
{
	const TWeakPtr<SCharacterCatalogRosterPanel> WeakPanel = SharedThis(this);
	TArray<Paper2DPlusAnimationTagChips::FAnimationTagChipItem> ChipItems =
		Paper2DPlusAnimationTagChips::BuildChipItems(
			Snapshot,
			FGameplayTagContainer(),
			FGameplayTagContainer());
	TSharedRef<SWrapBox> TagRow = SNew(SWrapBox).UseAllottedSize(true);
	if (ChipItems.IsEmpty())
	{
		TagRow->AddSlot()
		.Padding(0.0f, 2.0f, 4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(EmptyText)
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
	else
	{
		for (const Paper2DPlusAnimationTagChips::FAnimationTagChipItem& Item : ChipItems)
		{
			const FGameplayTag AnimationTag = Item.Tag;
			TagRow->AddSlot()
			.Padding(0.0f, 1.0f, 3.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Paper2DPlusAnimationTagChips::MakeTagChip(Item)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(2.0f, 0.0f))
					.ToolTipText(FText::Format(
						LOCTEXT(
							"RemoveExpectedAnimationTagTip",
							"Remove expected animation tag {0}."),
						FText::FromName(AnimationTag.GetTagName())))
					.OnClicked_Lambda([WeakPanel, GroupName, Snapshot, AnimationTag]()
					{
						FGameplayTagContainer NewTags = Snapshot;
						NewTags.RemoveTag(AnimationTag);
						if (const TSharedPtr<SCharacterCatalogRosterPanel> Panel = WeakPanel.Pin())
						{
							Panel->QueueExpectedAnimationTagsCommit(GroupName, NewTags);
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RemoveExpectedAnimationTag", "×"))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			];
		}
	}

	const TSharedRef<FGameplayTag> PickerValue = MakeShared<FGameplayTag>();
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ContentPadding(FMargin(4.0f, 2.0f))
		.ToolTipText(Tooltip)
		.OnGetMenuContent_Lambda([WeakPanel, GroupName, Snapshot, PickerValue]()
		{
			*PickerValue = FGameplayTag();
			FOnSetGameplayTag OnSetTag =
				FOnSetGameplayTag::CreateLambda(
					[WeakPanel, GroupName, Snapshot](const FGameplayTag& NewTag)
					{
						if (!NewTag.IsValid())
						{
							return;
						}

						// Copy every menu/callback-owned value before closing the menu. The authored
						// Catalog is then replaced once through the panel's one-tick commit queue.
						FGameplayTagContainer NewTags = Snapshot;
						NewTags.AddTag(NewTag);
						const TSharedPtr<SCharacterCatalogRosterPanel> Panel = WeakPanel.Pin();
						FSlateApplication::Get().DismissAllMenus();
						if (Panel.IsValid())
						{
							Panel->QueueExpectedAnimationTagsCommit(GroupName, NewTags);
						}
					});
			return SNew(SBox)
				.MinDesiredWidth(300.0f)
				.MaxDesiredHeight(420.0f)
				.Padding(2.0f)
				[
					SNew(SMenuHostedTagPickerGuard)
					[
						// This one-tag Add picker is the public seam available unchanged on UE 5.0-5.8.
						IGameplayTagsEditorModule::Get().MakeGameplayTagWidget(
							OnSetTag,
							PickerValue,
							TEXT("Paper2DPlus.Animation"))
					]
				];
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AddExpectedAnimationTag", "+ Add tag"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
		];
	TagRow->AddSlot()
	.Padding(0.0f, 1.0f)
	[
		Combo
	];

	if (GroupName.IsNone())
	{
		BaseExpectedAnimationTagsCombo = Combo;
	}
	else
	{
		GroupExpectedAnimationTagsCombo = Combo;
	}
	return TagRow;
}

void SCharacterCatalogRosterPanel::RefreshExpectedAnimationTagsEditors()
{
	if (ExpectedAnimationTagsEditorBox.IsValid())
	{
		ExpectedAnimationTagsEditorBox->SetContent(BuildExpectedAnimationTagsEditors());
	}
}

void SCharacterCatalogRosterPanel::QueueExpectedAnimationTagsCommitForTests(
	FName GroupName,
	const FGameplayTagContainer& Tags)
{
	QueueExpectedAnimationTagsCommit(GroupName, Tags);
}

void SCharacterCatalogRosterPanel::FlushExpectedAnimationTagsCommitForTests()
{
	if (const TSharedPtr<FActiveTimerHandle> Timer = ExpectedAnimationTagsCommitTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	ExpectedAnimationTagsCommitTimerHandle.Reset();
	HandleDeferredExpectedAnimationTagsCommit(0.0, 0.0f);
}

void SCharacterCatalogRosterPanel::QueueExpectedAnimationTagsCommit(
	FName GroupName,
	const FGameplayTagContainer& Tags)
{
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset =
		Model.IsValid() ? Model->GetCatalog() : nullptr;
	if (!CatalogAsset)
	{
		return;
	}
	if (GroupName.IsNone())
	{
		if (CatalogAsset->ExpectedAnimationTags == Tags)
		{
			return;
		}
	}
	else
	{
		const FPaper2DPlusCharacterCatalogGroup* Group = CatalogAsset->Groups.FindByPredicate(
			[GroupName](const FPaper2DPlusCharacterCatalogGroup& Candidate)
		{
			return Candidate.GroupName.IsEqual(GroupName, ENameCase::IgnoreCase);
		});
		if (!Group || Group->AdditionalExpectedAnimationTags == Tags)
		{
			return;
		}
	}

	PendingExpectedAnimationTagsCommit =
		FPendingExpectedAnimationTagsCommit{ GroupName, Tags };
	if (!ExpectedAnimationTagsCommitTimerHandle.IsValid())
	{
		ExpectedAnimationTagsCommitTimerHandle = RegisterActiveTimer(
			0.0f,
			FWidgetActiveTimerDelegate::CreateSP(
				this,
				&SCharacterCatalogRosterPanel::HandleDeferredExpectedAnimationTagsCommit));
	}
}

EActiveTimerReturnType SCharacterCatalogRosterPanel::HandleDeferredExpectedAnimationTagsCommit(
	double CurrentTime,
	float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	ExpectedAnimationTagsCommitTimerHandle.Reset();
	if (PendingExpectedAnimationTagsCommit.IsSet())
	{
		const FPendingExpectedAnimationTagsCommit Commit =
			MoveTemp(PendingExpectedAnimationTagsCommit.GetValue());
		PendingExpectedAnimationTagsCommit.Reset();
		if (Model.IsValid())
		{
			if (Commit.GroupName.IsNone())
			{
				Model->SetExpectedAnimationTags(Commit.Tags);
			}
			else
			{
				Model->SetGroupAdditionalExpectedAnimationTags(
					Commit.GroupName,
					Commit.Tags);
			}
		}
	}
	return EActiveTimerReturnType::Stop;
}

void SCharacterCatalogRosterPanel::RefreshGroupRail()
{
	GroupRailItems.Reset();
	GroupRailNameWidgets.Reset();
	// NAME_None is the All Characters sentinel and always leads, so clearing the scope is one click.
	GroupRailItems.Add(MakeShared<FName>(NAME_None));
	if (const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Model->GetCatalog())
	{
		for (const FPaper2DPlusCharacterCatalogGroup& Group : CatalogAsset->Groups)
		{
			if (!Group.GroupName.IsNone())
			{
				GroupRailItems.Add(MakeShared<FName>(Group.GroupName));
			}
		}
	}
	if (GroupRail.IsValid())
	{
		GroupRail->RequestListRefresh();
	}
	SynchronizeGroupRailSelection();
}

int32 SCharacterCatalogRosterPanel::GetGroupMemberCount(FName GroupName) const
{
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Model.IsValid() ? Model->GetCatalog() : nullptr;
	if (!CatalogAsset)
	{
		return 0;
	}
	if (GroupName.IsNone())
	{
		return Model->GetRows().Num();
	}
	const FPaper2DPlusCharacterCatalogGroup* Group = CatalogAsset->Groups.FindByPredicate(
		[GroupName](const FPaper2DPlusCharacterCatalogGroup& Candidate)
	{
		return Candidate.GroupName.IsEqual(GroupName, ENameCase::IgnoreCase);
	});
	return Group ? Group->Members.Num() : 0;
}

TSharedRef<ITableRow> SCharacterCatalogRosterPanel::GenerateGroupRailRow(
	TSharedPtr<FName> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FName GroupName = Item.IsValid() ? *Item : NAME_None;
	const bool bIsAllRow = GroupName.IsNone();
	FText Label = LOCTEXT("AllCharactersRail", "All Characters");
	if (!bIsAllRow)
	{
		Label = FText::FromName(GroupName);
		if (const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Model->GetCatalog())
		{
			if (const FPaper2DPlusCharacterCatalogGroup* Group = CatalogAsset->Groups.FindByPredicate(
				[GroupName](const FPaper2DPlusCharacterCatalogGroup& Candidate)
			{
				return Candidate.GroupName.IsEqual(GroupName, ENameCase::IgnoreCase);
			}))
			{
				if (!Group->DisplayName.IsEmpty())
				{
					Label = Group->DisplayName;
				}
			}
		}
	}

	TSharedPtr<SInlineEditableTextBlock> NameText;
	TSharedRef<SHorizontalBox> RowContent = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4.0f, 2.0f)
		[
			SAssignNew(NameText, SInlineEditableTextBlock)
			.Text(Label)
			.ToolTipText(bIsAllRow
				? LOCTEXT("AllCharactersRailTip", "Show every character in the Catalog.")
				: FText::Format(
					LOCTEXT("GroupRailRowTip", "{0}\nInternal Blueprint name: {1}\nDrag characters here to add them."),
					Label,
					FText::FromName(GroupName)))
			.IsReadOnly(bIsAllRow)
			.OnTextCommitted_Lambda([this, GroupName](const FText& NewText, ETextCommit::Type)
			{
				RenameGroupFromRail(GroupName, NewText);
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 2.0f, 6.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::AsNumber(GetGroupMemberCount(GroupName)))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	if (!bIsAllRow)
	{
		GroupRailNameWidgets.Add(GroupName, NameText);
	}

	using FRailRow = STableRow<TSharedPtr<FName>>;
	return SNew(FRailRow, OwnerTable)
		.OnDragDetected_Lambda([this, GroupName, bIsAllRow](const FGeometry&, const FPointerEvent&) -> FReply
		{
			if (bIsAllRow || !Model.IsValid())
			{
				return FReply::Unhandled();
			}
			return FReply::Handled().BeginDragDrop(
				FCatalogGroupRowDragDropOp::New(GroupName, Model->GetCatalog()));
		})
		.OnCanAcceptDrop_Lambda([this, GroupName, bIsAllRow](
			const FDragDropEvent& DragDropEvent,
			EItemDropZone DropZone,
			TSharedPtr<FName>) -> TOptional<EItemDropZone>
		{
			UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Model.IsValid() ? Model->GetCatalog() : nullptr;
			if (const TSharedPtr<FCatalogCharacterDragDropOp> CardOp =
				DragDropEvent.GetOperationAs<FCatalogCharacterDragDropOp>())
			{
				// Characters land INTO a group, so the whole row is the target; All Characters owns no
				// membership and a foreign Catalog fails closed.
				if (bIsAllRow || !CatalogAsset || CardOp->SourceCatalog.Get() != CatalogAsset)
				{
					return TOptional<EItemDropZone>();
				}
				return EItemDropZone::OntoItem;
			}
			if (const TSharedPtr<FCatalogGroupRowDragDropOp> GroupOp =
				DragDropEvent.GetOperationAs<FCatalogGroupRowDragDropOp>())
			{
				if (!CatalogAsset || GroupOp->SourceCatalog.Get() != CatalogAsset
					|| GroupOp->SourceGroupName.IsEqual(GroupName, ENameCase::IgnoreCase))
				{
					return TOptional<EItemDropZone>();
				}
				// All Characters is a sentinel, not a group: only the gap BELOW it is a real position
				// ("make this the first group"). Above it there is nothing to be first of.
				if (bIsAllRow)
				{
					return DropZone == EItemDropZone::BelowItem
						? TOptional<EItemDropZone>(EItemDropZone::BelowItem)
						: TOptional<EItemDropZone>();
				}
				// Group reorder is positional, so only the gaps are meaningful targets.
				return DropZone == EItemDropZone::BelowItem
					? EItemDropZone::BelowItem
					: EItemDropZone::AboveItem;
			}
			return TOptional<EItemDropZone>();
		})
		.OnAcceptDrop_Lambda([this, GroupName, bIsAllRow](
			const FDragDropEvent& DragDropEvent,
			EItemDropZone DropZone,
			TSharedPtr<FName>) -> FReply
		{
			UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Model.IsValid() ? Model->GetCatalog() : nullptr;
			if (!CatalogAsset)
			{
				return FReply::Unhandled();
			}
			if (const TSharedPtr<FCatalogCharacterDragDropOp> CardOp =
				DragDropEvent.GetOperationAs<FCatalogCharacterDragDropOp>())
			{
				if (bIsAllRow || CardOp->SourceCatalog.Get() != CatalogAsset)
				{
					return FReply::Unhandled();
				}
				const int32 Added = Model->AddGroupMembers(GroupName, CardOp->CharacterPaths);
				if (Added == 0)
				{
					ShowMessage(LOCTEXT("GroupDropNoop", "Every dragged character is already in that group."));
				}
				return FReply::Handled();
			}
			if (const TSharedPtr<FCatalogGroupRowDragDropOp> GroupOp =
				DragDropEvent.GetOperationAs<FCatalogGroupRowDragDropOp>())
			{
				if (GroupOp->SourceCatalog.Get() != CatalogAsset)
				{
					return FReply::Unhandled();
				}
				HandleGroupRowDrop(
					GroupOp->SourceGroupName,
					GroupName,
					bIsAllRow,
					DropZone == EItemDropZone::BelowItem);
				return FReply::Handled();
			}
			return FReply::Unhandled();
		})
		[
			RowContent
		];
}

void SCharacterCatalogRosterPanel::HandleGroupRailSelectionChanged(
	TSharedPtr<FName> Item,
	ESelectInfo::Type SelectInfo)
{
	if (bSyncingRailSelection || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	ApplyGroupSelection(Item.IsValid() ? *Item : NAME_None);
}

bool SCharacterCatalogRosterPanel::SelectGroupRail(FName GroupName)
{
	return ApplyGroupSelection(GroupName);
}

bool SCharacterCatalogRosterPanel::ApplyGroupSelection(FName GroupName)
{
	if (!Model.IsValid())
	{
		return false;
	}
	SelectedRailGroup = GroupName;
	Model->SetGroupFilter(GroupName);
	return true;
}

void SCharacterCatalogRosterPanel::SynchronizeGroupRailSelection()
{
	if (!GroupRail.IsValid() || !Model.IsValid())
	{
		return;
	}
	// The rail MIRRORS the model's group filter and never pushes back during a sync, so a filter set
	// from anywhere else (a warning activation, Clear Filters) cannot ping-pong through this handler.
	SelectedRailGroup = Model->GetFilters().Group;
	const TSharedPtr<FName>* Match = GroupRailItems.FindByPredicate([this](const TSharedPtr<FName>& Candidate)
	{
		return Candidate.IsValid() && Candidate->IsEqual(SelectedRailGroup, ENameCase::IgnoreCase);
	});
	TGuardValue<bool> SyncGuard(bSyncingRailSelection, true);
	if (Match)
	{
		GroupRail->SetSelection(*Match, ESelectInfo::Direct);
		return;
	}
	GroupRail->ClearSelection();
}

TSharedPtr<SWidget> SCharacterCatalogRosterPanel::HandleGroupRailContextMenu()
{
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(
		LOCTEXT("RailNewGroup", "New Group"),
		LOCTEXT("RailNewGroupTip", "Create a group and name it inline."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			CreateGroupFromRail(LOCTEXT("NewGroupDefaultLabel", "New Group"));
		})));
	if (!SelectedRailGroup.IsNone())
	{
		const FName TargetGroup = SelectedRailGroup;
		Menu.AddMenuEntry(
			LOCTEXT("RailRenameGroup", "Rename"),
			LOCTEXT("RailRenameGroupTip", "Rename this group's label. Its internal Blueprint name stays stable."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TargetGroup]()
			{
				if (const TWeakPtr<SInlineEditableTextBlock>* Widget = GroupRailNameWidgets.Find(TargetGroup))
				{
					if (const TSharedPtr<SInlineEditableTextBlock> Pinned = Widget->Pin())
					{
						Pinned->EnterEditingMode();
					}
				}
			})));
		Menu.AddMenuEntry(
			LOCTEXT("RailDeleteGroup", "Delete Group…"),
			LOCTEXT("RailDeleteGroupTip", "Delete this group. Its characters stay in the Catalog."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TargetGroup]()
			{
				RemoveGroupFromRail(TargetGroup);
			})));
	}
	return Menu.MakeWidget();
}

bool SCharacterCatalogRosterPanel::CreateGroupFromRail(const FText& DisplayLabel)
{
	if (!Model.IsValid())
	{
		return false;
	}
	const FName DerivedName = Model->DeriveUniqueGroupName(DisplayLabel);
	FText Error;
	if (!Model->AddGroup(DerivedName, DisplayLabel, Error))
	{
		ShowMessage(Error);
		return false;
	}
	ApplyGroupSelection(DerivedName);
	return true;
}

bool SCharacterCatalogRosterPanel::RemoveGroupFromRail(FName GroupName)
{
	if (!Model.IsValid() || GroupName.IsNone())
	{
		return false;
	}
	const int32 MemberCount = GetGroupMemberCount(GroupName);
	const FText Confirmation = MemberCount > 0
		? FText::Format(
			LOCTEXT("ConfirmRemoveGroupWithMembers", "Delete the group '{0}'?\n\nIts {1} member(s) stay in the Catalog; only the grouping is removed."),
			FText::FromName(GroupName),
			FText::AsNumber(MemberCount))
		: FText::Format(
			LOCTEXT("ConfirmRemoveGroup", "Delete the empty group '{0}'?"),
			FText::FromName(GroupName));
	const bool bConfirmed = RemoveGroupConfirmation
		? RemoveGroupConfirmation(Confirmation)
		: FMessageDialog::Open(EAppMsgType::YesNo, Confirmation) == EAppReturnType::Yes;
	if (!bConfirmed)
	{
		return false;
	}
	if (!Model->RemoveGroup(GroupName))
	{
		return false;
	}
	// A vanished scope must not keep filtering the grid to nothing.
	if (SelectedRailGroup.IsEqual(GroupName, ENameCase::IgnoreCase))
	{
		ApplyGroupSelection(NAME_None);
	}
	return true;
}

bool SCharacterCatalogRosterPanel::RenameGroupFromRail(FName GroupName, const FText& NewLabel)
{
	if (!Model.IsValid() || GroupName.IsNone() || NewLabel.IsEmptyOrWhitespace())
	{
		return false;
	}
	// The rail renames the LABEL only: the internal name is a stable Blueprint identity, so renaming
	// cannot silently break GetEntriesInGroup callers the way an editable internal name would.
	FText Error;
	if (!Model->RenameGroup(GroupName, GroupName, NewLabel, Error))
	{
		if (!Error.IsEmpty())
		{
			ShowMessage(Error);
		}
		return false;
	}
	return true;
}

TArray<FName> SCharacterCatalogRosterPanel::GetGroupRailItemsForTests() const
{
	TArray<FName> Names;
	for (const TSharedPtr<FName>& Item : GroupRailItems)
	{
		Names.Add(Item.IsValid() ? *Item : NAME_None);
	}
	return Names;
}

bool SCharacterCatalogRosterPanel::SelectGroupRailItemForTests(FName GroupName)
{
	return SelectGroupRail(GroupName);
}

bool SCharacterCatalogRosterPanel::DropCharactersOnGroupForTests(
	FName GroupName,
	const TArray<FSoftObjectPath>& CharacterPaths)
{
	return Model.IsValid() && Model->AddGroupMembers(GroupName, CharacterPaths) > 0;
}

bool SCharacterCatalogRosterPanel::ReorderGroupMemberForTests(
	const FSoftObjectPath& CharacterPath,
	int32 TargetIndex)
{
	return Model.IsValid()
		&& !SelectedRailGroup.IsNone()
		&& Model->MoveGroupMemberToIndex(SelectedRailGroup, CharacterPath, TargetIndex);
}

bool SCharacterCatalogRosterPanel::HandleCardOnCardDrop(
	const FSoftObjectPath& MovedPath,
	const FSoftObjectPath& AnchorPath,
	bool bBelowAnchor)
{
	if (!Model.IsValid() || SelectedRailGroup.IsNone())
	{
		return false;
	}
	// Pass the character that was dropped ON, never a view index. The scoped grid is member-ordered
	// only among the rows that SURVIVED the filter (RefilterRows filters first, then sorts), so a
	// visible index silently addresses the wrong member whenever a search or completion filter is
	// active. The anchor has exactly one authored position and the model resolves it.
	return Model->MoveGroupMemberRelativeTo(
		SelectedRailGroup,
		MovedPath,
		AnchorPath,
		bBelowAnchor);
}

bool SCharacterCatalogRosterPanel::HandleGroupRowDrop(
	FName MovedGroup,
	FName AnchorGroup,
	bool bAnchorIsAllRow,
	bool bBelowAnchor)
{
	if (!Model.IsValid())
	{
		return false;
	}
	// Dropping below the All Characters sentinel means "make this the first group". It owns no
	// authored index, so it is the one position expressed absolutely.
	if (bAnchorIsAllRow)
	{
		return bBelowAnchor && Model->MoveGroupToIndex(MovedGroup, 0);
	}
	// Otherwise pass the group that was dropped ON. Rail index N is NOT authored index N-1:
	// RefreshGroupRail omits unnamed groups, so every unnamed row before this one shifts the mapping
	// and the group would land somewhere the designer never dropped it.
	return Model->MoveGroupRelativeTo(MovedGroup, AnchorGroup, bBelowAnchor);
}

bool SCharacterCatalogRosterPanel::CreateGroupFromRailForTests(const FText& DisplayLabel)
{
	return CreateGroupFromRail(DisplayLabel);
}

bool SCharacterCatalogRosterPanel::RemoveGroupFromRailForTests(FName GroupName)
{
	return RemoveGroupFromRail(GroupName);
}

bool SCharacterCatalogRosterPanel::RenameGroupFromRailForTests(FName GroupName, const FText& NewLabel)
{
	return RenameGroupFromRail(GroupName, NewLabel);
}

TSharedPtr<FDragDropOperation> SCharacterCatalogRosterPanel::BeginCharacterDrag()
{
	const TArray<FSoftObjectPath> Paths = GetSelectedCharacterPaths();
	if (Paths.IsEmpty() || !Model.IsValid())
	{
		return nullptr;
	}
	return FCatalogCharacterDragDropOp::New(Paths, SelectedRailGroup, Model->GetCatalog());
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildAuthorityBanner()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return Model->GetAuthorityStatusText(); })
			.ToolTipText(LOCTEXT(
				"AuthorityTip",
				"The Project Catalog is the roster used by the game: Blueprint defaults, the Catalog pin picker, and validation resolve it. Add Characters and Remove are the only roster edits."))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("SetAuthority", "Make Project Catalog"))
			.AccessibleText(LOCTEXT("SetAuthorityAccessible", "Set this asset as the project Character Catalog"))
			.IsEnabled_Lambda([this]() { return !Model->IsAuthoritative(); })
			.OnClicked(this, &SCharacterCatalogRosterPanel::HandleSetAuthority)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("Settings", "Project Settings"))
			.ToolTipText(LOCTEXT("SettingsTip", "Open Plugins > Paper2DPlus to choose the Project Catalog."))
			.OnClicked(this, &SCharacterCatalogRosterPanel::HandleOpenSettings)
		];
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildCommandBar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SComboButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HasDownArrow(true)
			.ToolTipText(LOCTEXT("AddCharactersTip", "Pick Character Profiles to add to this Catalog. By default the picker shows only profiles that are not in the Catalog yet."))
			.OnGetMenuContent(this, &SCharacterCatalogRosterPanel::BuildAddCharactersMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AddCharacters", "+ Add Characters…"))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("RefreshRoster", "Refresh"))
			.ToolTipText(LOCTEXT(
				"RefreshRosterTip",
				"Re-read the roster's presentation state and retry any card art that failed to load. This never changes the Catalog."))
			.OnClicked(this, &SCharacterCatalogRosterPanel::HandleRefresh)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("RemoveSelected", "Remove Selected…"))
			.ToolTipText(LOCTEXT("RemoveSelectedTip", "Remove the selected character(s) from this Catalog. Referenced assets are kept. Delete works too."))
			.Visibility_Lambda([this]()
			{
				return GetSelectedCharacterPaths().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
			})
			.OnClicked_Lambda([this]()
			{
				RemoveSelectedCharacters();
				return FReply::Handled();
			})
		];
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildAddCharactersMenu()
{
	FContentBrowserModule* ContentBrowserModule =
		FModuleManager::LoadModulePtr<FContentBrowserModule>(TEXT("ContentBrowser"));
	if (!ContentBrowserModule)
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("AddPickerUnavailable", "The Content Browser is unavailable in this process."));
	}

	// The refresh/selection delegate objects live exactly as long as the open menu: the widgets that
	// consume them capture the shared refs, and a fresh menu builds fresh delegates.
	TSharedRef<FRefreshAssetViewDelegate> RefreshPicker = MakeShared<FRefreshAssetViewDelegate>();
	TSharedRef<FGetCurrentSelectionDelegate> GetPickerSelection = MakeShared<FGetCurrentSelectionDelegate>();

	FAssetPickerConfig Config;
	Config.SelectionMode = ESelectionMode::Multi;
	Config.InitialAssetViewType = EAssetViewType::List;
	Config.bFocusSearchBoxWhenOpened = true;
	Config.bAllowNullSelection = false;
	Config.bAllowDragging = false;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	Config.Filter.ClassNames.Add(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetFName());
#else
	Config.Filter.ClassPaths.Add(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName());
#endif
	Config.Filter.bRecursiveClasses = true;
	Config.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(
		this, &SCharacterCatalogRosterPanel::ShouldHideAddCandidate);
	Config.OnAssetsActivated = FOnAssetsActivated::CreateLambda(
		[WeakPanel = TWeakPtr<SCharacterCatalogRosterPanel>(SharedThis(this))](
			const TArray<FAssetData>& ActivatedAssets, EAssetTypeActivationMethod::Type)
		{
			if (const TSharedPtr<SCharacterCatalogRosterPanel> Panel =
				StaticCastSharedPtr<SCharacterCatalogRosterPanel>(WeakPanel.Pin()))
			{
				Panel->AddCharactersFromAssets(ActivatedAssets);
			}
		});
	Config.RefreshAssetViewDelegates.Add(&RefreshPicker.Get());
	Config.GetCurrentSelectionDelegates.Add(&GetPickerSelection.Get());

	return SNew(SBox).WidthOverride(420.0f).HeightOverride(420.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 4.0f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]()
			{
				return bShowOnlyNewCharacters ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, RefreshPicker](ECheckBoxState State)
			{
				bShowOnlyNewCharacters = State == ECheckBoxState::Checked;
				RefreshPicker->ExecuteIfBound(true);
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OnlyNewCharacters", "Show only profiles not in this Catalog"))
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f, 0.0f)
		[
			ContentBrowserModule->Get().CreateAssetPicker(Config)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 4.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("AddSelectedProfiles", "Add Selected"))
			.ToolTipText(LOCTEXT("AddSelectedProfilesTip", "Add every Character Profile selected above. Double-clicking a profile adds it too."))
			.OnClicked_Lambda([this, GetPickerSelection]()
			{
				if (GetPickerSelection->IsBound())
				{
					AddCharactersFromAssets(GetPickerSelection->Execute());
				}
				return FReply::Handled();
			})
		]
	];
}

bool SCharacterCatalogRosterPanel::ShouldHideAddCandidate(const FAssetData& AssetData) const
{
	if (!bShowOnlyNewCharacters || !Model.IsValid())
	{
		return false;
	}
	return Model->IsCharacterInCatalog(FProfileRelationshipService::GetAssetObjectPath(AssetData));
}

void SCharacterCatalogRosterPanel::AddCharactersFromAssets(const TArray<FAssetData>& Assets)
{
	if (!Model.IsValid() || Assets.IsEmpty())
	{
		return;
	}
	TArray<FSoftObjectPath> Paths;
	Paths.Reserve(Assets.Num());
	for (const FAssetData& AssetData : Assets)
	{
		Paths.Add(FProfileRelationshipService::GetAssetObjectPath(AssetData));
	}
	Model->AddCharacters(Paths);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().DismissAllMenus();
	}
}

TArray<FSoftObjectPath> SCharacterCatalogRosterPanel::GetSelectedCharacterPaths() const
{
	TArray<FSoftObjectPath> Paths;
	if (!CharacterTiles.IsValid())
	{
		return Paths;
	}
	for (const FCatalogRowPtr& Row : CharacterTiles->GetSelectedItems())
	{
		if (Row.IsValid() && !Row->CharacterPath.IsNull())
		{
			Paths.Add(Row->CharacterPath);
		}
	}
	return Paths;
}

bool SCharacterCatalogRosterPanel::RemoveSelectedCharacters()
{
	const TArray<FSoftObjectPath> Paths = GetSelectedCharacterPaths();
	if (!Model.IsValid() || Paths.IsEmpty())
	{
		return false;
	}
	const FText Confirmation = Paths.Num() == 1
		? LOCTEXT("ConfirmRemoveOne", "Remove this character from the Catalog?\n\nThe Character Profile and all companion assets will be kept.")
		: FText::Format(
			LOCTEXT("ConfirmRemoveMany", "Remove {0} characters from the Catalog?\n\nThe Character Profiles and all companion assets will be kept."),
			FText::AsNumber(Paths.Num()));
	const bool bConfirmed = RemoveSelectedConfirmation
		? RemoveSelectedConfirmation(Confirmation)
		: FMessageDialog::Open(EAppMsgType::YesNo, Confirmation) == EAppReturnType::Yes;
	if (!bConfirmed)
	{
		return false;
	}
	return Model->RemoveCharacters(Paths);
}

TSharedPtr<SWidget> SCharacterCatalogRosterPanel::HandleTileContextMenu()
{
	const TArray<FSoftObjectPath> Paths = GetSelectedCharacterPaths();
	if (Paths.IsEmpty())
	{
		return nullptr;
	}
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(
		Paths.Num() == 1
			? LOCTEXT("ContextRemoveOne", "Remove from Catalog…")
			: FText::Format(LOCTEXT("ContextRemoveMany", "Remove {0} from Catalog…"), FText::AsNumber(Paths.Num())),
		LOCTEXT("ContextRemoveTip", "Remove the selected character(s) from this Catalog. Referenced assets are kept."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakPanel = TWeakPtr<SCharacterCatalogRosterPanel>(SharedThis(this))]()
		{
			if (const TSharedPtr<SCharacterCatalogRosterPanel> Panel =
				StaticCastSharedPtr<SCharacterCatalogRosterPanel>(WeakPanel.Pin()))
			{
				Panel->RemoveSelectedCharacters();
			}
		})));
	return Menu.MakeWidget();
}

FReply SCharacterCatalogRosterPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace)
	{
		if (RemoveSelectedCharacters())
		{
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SCharacterCatalogRosterPanel::SetTileSelectionForTests(const TArray<FSoftObjectPath>& CharacterPaths)
{
	if (!CharacterTiles.IsValid() || !Model.IsValid())
	{
		return;
	}
	CharacterTiles->ClearSelection();
	for (const FSoftObjectPath& Path : CharacterPaths)
	{
		if (const FCatalogRowPtr Row = Model->FindRow(Path))
		{
			CharacterTiles->SetItemSelection(Row, true, ESelectInfo::Direct);
		}
	}
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildFilters()
{
	return SNew(SWrapBox)
		.UseAllottedSize(true)
		+ SWrapBox::Slot().Padding(0.0f, 0.0f, 4.0f, 2.0f)
		[
			SNew(SComboButton).ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton").OnGetMenuContent(this, &SCharacterCatalogRosterPanel::BuildTagFilterMenu)
			.ButtonContent()[SNew(STextBlock).Text(this, &SCharacterCatalogRosterPanel::GetTagFilterText)]
		]
		+ SWrapBox::Slot().Padding(0.0f, 0.0f, 4.0f, 2.0f)
		[
			SNew(SComboButton).ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton").OnGetMenuContent(this, &SCharacterCatalogRosterPanel::BuildGroupMenu)
			.ButtonContent()[SNew(STextBlock).Text(this, &SCharacterCatalogRosterPanel::GetGroupFilterText)]
		]
		+ SWrapBox::Slot().Padding(0.0f, 0.0f, 4.0f, 2.0f)
		[
			SNew(SComboButton).ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton").OnGetMenuContent(this, &SCharacterCatalogRosterPanel::BuildRequirementMenu)
			.ButtonContent()[SNew(STextBlock).Text(this, &SCharacterCatalogRosterPanel::GetRequirementFilterText)]
		]
		+ SWrapBox::Slot().Padding(0.0f, 0.0f, 4.0f, 2.0f)
		[
			SNew(SComboButton).ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton").OnGetMenuContent(this, &SCharacterCatalogRosterPanel::BuildCompletionMenu)
			.ButtonContent()[SNew(STextBlock).Text(this, &SCharacterCatalogRosterPanel::GetCompletionFilterText)]
		]
		+ SWrapBox::Slot().Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("ClearFilters", "Clear Filters"))
			.IsEnabled_Lambda([this]() { return !Model->GetFilters().IsDefault(); })
			.OnClicked(this, &SCharacterCatalogRosterPanel::HandleClearFilters)
		];
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildRequirementMenu()
{
	FMenuBuilder Menu(true, nullptr);
	auto Add = [this, &Menu](const FText& Label, EPaper2DPlusCatalogRequirementFilter Filter)
	{
		Menu.AddMenuEntry(Label, FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Filter]()
		{
			Model->SetRequirementFilter(Filter);
		})));
	};
	Add(LOCTEXT("ReqAny", "Any requirement"), EPaper2DPlusCatalogRequirementFilter::Any);
	Add(LOCTEXT("ReqLayer", "Requires Layer"), EPaper2DPlusCatalogRequirementFilter::Layer);
	Add(LOCTEXT("ReqEffect", "Requires Effect"), EPaper2DPlusCatalogRequirementFilter::Effect);
	Add(LOCTEXT("ReqCombat", "Requires Combat"), EPaper2DPlusCatalogRequirementFilter::Combat);
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildCompletionMenu()
{
	FMenuBuilder Menu(true, nullptr);
	auto Add = [this, &Menu](const FText& Label, EPaper2DPlusCatalogCompletionFilter Filter)
	{
		Menu.AddMenuEntry(Label, FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Filter]()
		{
			Model->SetCompletionFilter(Filter);
		})));
	};
	Add(LOCTEXT("CompletionAny", "Any progress"), EPaper2DPlusCatalogCompletionFilter::Any);
	Add(LOCTEXT("CompletionComplete", "Fully authored"), EPaper2DPlusCatalogCompletionFilter::Complete);
	Add(LOCTEXT("CompletionIncomplete", "In progress"), EPaper2DPlusCatalogCompletionFilter::Incomplete);
	Add(LOCTEXT("CompletionUnknown", "No checklist data"), EPaper2DPlusCatalogCompletionFilter::Unknown);
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildGroupMenu()
{
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("AllGroups", "All groups"), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]()
	{
		Model->SetGroupFilter(NAME_None);
	})));
	if (const UPaper2DPlusCharacterCatalogAsset* Catalog = Model->GetCatalog())
	{
		for (const FPaper2DPlusCharacterCatalogGroup& Group : Catalog->Groups)
		{
			const FName Name = Group.GroupName;
			Menu.AddMenuEntry(Group.DisplayName.IsEmpty() ? FText::FromName(Name) : Group.DisplayName,
				FText::FromName(Name), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Name]()
			{
				Model->SetGroupFilter(Name);
			})));
		}
	}
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SCharacterCatalogRosterPanel::BuildTagFilterMenu()
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	const FGameplayTagContainer Snapshot(Model->GetFilters().Tag);
	return SNew(SBox).MinDesiredWidth(340.0f).Padding(2.0f)
	[
		SNew(SMenuHostedTagPickerGuard)
		[
			SNew(SGameplayTagPicker)
			.MultiSelect(false)
			.TagContainers(TArray<FGameplayTagContainer>{ Snapshot })
			.OnTagChanged_Lambda([WeakModel = TWeakPtr<FCharacterCatalogEditorModel>(Model)](const TArray<FGameplayTagContainer>& Containers)
			{
				if (TSharedPtr<FCharacterCatalogEditorModel> Pinned = WeakModel.Pin())
				{
					FGameplayTag Tag;
					if (!Containers.IsEmpty())
					{
						TArray<FGameplayTag> Tags;
						Containers[0].GetGameplayTagArray(Tags);
						if (!Tags.IsEmpty()) Tag = Tags[0];
					}
					Pinned->SetTagFilter(Tag);
				}
			})
		]
	];
#else
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("ClearTagFilter", "Clear tag filter"), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]()
	{
		Model->SetTagFilter(FGameplayTag());
	})));
	Menu.AddWidget(SNew(STextBlock).Text(LOCTEXT("TagFilterFallback", "Gameplay Tag picker requires Unreal 5.3+.")), FText());
	return Menu.MakeWidget();
#endif
}

FText SCharacterCatalogRosterPanel::GetRosterSummaryText() const
{
	return CachedRosterSummary;
}

FText SCharacterCatalogRosterPanel::GetRequirementFilterText() const
{
	switch (Model->GetFilters().Requirement)
	{
	case EPaper2DPlusCatalogRequirementFilter::Layer: return LOCTEXT("ReqLayerShort", "Requirement: Layer");
	case EPaper2DPlusCatalogRequirementFilter::Effect: return LOCTEXT("ReqEffectShort", "Requirement: Effect");
	case EPaper2DPlusCatalogRequirementFilter::Combat: return LOCTEXT("ReqCombatShort", "Requirement: Combat");
	default: return LOCTEXT("ReqAnyShort", "Requirement: Any");
	}
}

FText SCharacterCatalogRosterPanel::GetCompletionFilterText() const
{
	switch (Model->GetFilters().Completion)
	{
	case EPaper2DPlusCatalogCompletionFilter::Complete: return LOCTEXT("CompleteShort", "Progress: Fully authored");
	case EPaper2DPlusCatalogCompletionFilter::Incomplete: return LOCTEXT("IncompleteShort", "Progress: In progress");
	case EPaper2DPlusCatalogCompletionFilter::Unknown: return LOCTEXT("UnknownShort", "Progress: No data");
	default: return LOCTEXT("CompletionAnyShort", "Progress: Any");
	}
}

FText SCharacterCatalogRosterPanel::GetGroupFilterText() const
{
	return Model->GetFilters().Group.IsNone()
		? LOCTEXT("GroupAnyShort", "Group: Any")
		: FText::Format(LOCTEXT("GroupShort", "Group: {0}"), FText::FromName(Model->GetFilters().Group));
}

FText SCharacterCatalogRosterPanel::GetTagFilterText() const
{
	return Model->GetFilters().Tag.IsValid()
		? FText::Format(LOCTEXT("TagShort", "Tag: {0}"), FText::FromName(Model->GetFilters().Tag.GetTagName()))
		: LOCTEXT("TagAnyShort", "Tag: Any");
}

FReply SCharacterCatalogRosterPanel::HandleRefresh()
{
	// The source refresh is also the retry/invalidation boundary for thumbnail loads. This discards
	// terminal failures and successful art chosen from an older Character Profile thumbnail source.
	ResetAsyncThumbnailLoads();
	Model->RefreshFromSources();
	return FReply::Handled();
}

FReply SCharacterCatalogRosterPanel::HandleSetAuthority()
{
	FText Message;
	Model->SetAsProjectCatalog(Message);
	ShowMessage(Message);
	return FReply::Handled();
}

FReply SCharacterCatalogRosterPanel::HandleOpenSettings()
{
	Model->OpenProjectSettings();
	return FReply::Handled();
}

FReply SCharacterCatalogRosterPanel::HandleClearFilters()
{
	Model->ClearFilters();
	if (SearchBox.IsValid()) SearchBox->SetText(FText::GetEmpty());
	return FReply::Handled();
}

void SCharacterCatalogRosterPanel::ShowMessage(const FText& Message) const
{
	if (!Message.IsEmpty()) FMessageDialog::Open(EAppMsgType::Ok, Message);
}

#undef LOCTEXT_NAMESPACE
