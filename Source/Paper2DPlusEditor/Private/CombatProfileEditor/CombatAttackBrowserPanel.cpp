// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileEditor/CombatAttackBrowserPanel.h"

#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "EditorCanvasUtils.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "ProfileCard.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STileView.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "CombatAttackBrowserPanel"

void SCombatAttackBrowserPanel::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;

	// Evicting a card's retention must also stop its load: releasing the handle alone would leave an
	// off-screen card's streaming request running with nothing waiting on it.
	ThumbnailBudget.SetOnRelease([](TSharedPtr<FStreamableHandle>& Handle)
	{
		if (Handle.IsValid() && !Handle->HasLoadCompleted())
		{
			Handle->CancelHandle();
		}
	});

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AttacksHeader", "Attacks"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchAttacks", "Search attacks by name or tag…"))
				.ToolTipText(LOCTEXT("SearchAttacksTip", "Filter the attack cards by move name or attack tag."))
				.OnTextChanged(this, &SCombatAttackBrowserPanel::HandleSearchChanged)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(CardTileView, STileView<FRowPtr>)
				.ListItemsSource(&FilteredRows)
				.SelectionMode(ESelectionMode::Single)
				.ItemWidth(SProfileCard::GetCanonicalCardWidth() + 6.0f)
				.ItemHeight(SProfileCard::GetCanonicalCompactCardHeight() + 6.0f)
				.OnGenerateTile(this, &SCombatAttackBrowserPanel::GenerateCardTile)
				.OnSelectionChanged_Lambda([this](FRowPtr Item, ESelectInfo::Type)
				{
					if (!bSynchronizingSelection && Item.IsValid() && Session.IsValid())
					{
						Session->SelectAttackByName(Item->MoveName);
					}
				})
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(12.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("NoAttacks", "No attacks yet. Link a Character Profile and give its attack animations an Attack Tag — tagged moves appear here automatically."))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				.Visibility_Lambda([this]()
				{
					return AllRows.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(12.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("NoSearchMatch", "No attacks match your search."))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				.Visibility_Lambda([this]()
				{
					return !AllRows.IsEmpty() && FilteredRows.IsEmpty()
						? EVisibility::HitTestInvisible
						: EVisibility::Collapsed;
				})
			]
		]
	];

	if (Session.IsValid())
	{
		DataChangedHandle = Session->OnDataChanged().AddSP(
			this, &SCombatAttackBrowserPanel::RefreshFromSession);
		SelectionChangedHandle = Session->OnSelectionChanged().AddLambda(
			[WeakThis = TWeakPtr<SCombatAttackBrowserPanel>(SharedThis(this))](const FProfileItemIdentity&)
			{
				if (const TSharedPtr<SCombatAttackBrowserPanel> Pinned = WeakThis.Pin())
				{
					Pinned->SynchronizeSelection();
				}
			});
	}
	RefreshFromSession();
}

SCombatAttackBrowserPanel::~SCombatAttackBrowserPanel()
{
	ResetThumbnailLoads();
	if (Session.IsValid())
	{
		Session->OnDataChanged().Remove(DataChangedHandle);
		Session->OnSelectionChanged().Remove(SelectionChangedHandle);
	}
}

void SCombatAttackBrowserPanel::RefreshFromSession()
{
	ResetThumbnailLoads();
	AllRows.Reset();
	if (Session.IsValid())
	{
		const TArray<FPaper2DPlusCombatAttackDerivedData>& Catalog = Session->GetCatalog();
		AllRows.Reserve(Catalog.Num());
		for (const FPaper2DPlusCombatAttackDerivedData& Row : Catalog)
		{
			AllRows.Add(MakeShared<FPaper2DPlusCombatAttackDerivedData>(Row));
		}
	}
	RebuildFilteredRows();
	SynchronizeSelection();
}

void SCombatAttackBrowserPanel::RebuildFilteredRows()
{
	FilteredRows.Reset();
	if (SearchText.IsEmpty())
	{
		FilteredRows = AllRows;
	}
	else
	{
		for (const FRowPtr& Row : AllRows)
		{
			if (!Row.IsValid())
			{
				continue;
			}
			const bool bNameMatch = Row->MoveName.ToString().Contains(SearchText, ESearchCase::IgnoreCase);
			const bool bTagMatch = Row->AttackTag.IsValid()
				&& Row->AttackTag.GetTagName().ToString().Contains(SearchText, ESearchCase::IgnoreCase);
			if (bNameMatch || bTagMatch)
			{
				FilteredRows.Add(Row);
			}
		}
	}

	if (CardTileView.IsValid())
	{
		CardTileView->RequestListRefresh();
	}
}

void SCombatAttackBrowserPanel::HandleSearchChanged(const FText& NewText)
{
	SearchText = NewText.ToString();
	RebuildFilteredRows();
	SynchronizeSelection();
}

FName SCombatAttackBrowserPanel::GetSelectedMoveName() const
{
	return Session.IsValid() && Session->GetSelectedAttack().IsValid()
		? FName(*Session->GetSelectedAttack().FallbackKey)
		: NAME_None;
}

void SCombatAttackBrowserPanel::SynchronizeSelection()
{
	if (bSynchronizingSelection || !CardTileView.IsValid())
	{
		return;
	}
	TGuardValue<bool> Guard(bSynchronizingSelection, true);
	const FName MoveName = GetSelectedMoveName();
	const FRowPtr* Match = FilteredRows.FindByPredicate([MoveName](const FRowPtr& Row)
	{
		return Row.IsValid() && Row->MoveName == MoveName;
	});
	if (Match)
	{
		CardTileView->SetSelection(*Match, ESelectInfo::Direct);
	}
	else
	{
		CardTileView->ClearSelection();
	}
	CardTileView->Invalidate(EInvalidateWidgetReason::Paint);
}

TSharedRef<ITableRow> SCombatAttackBrowserPanel::GenerateCardTile(
	FRowPtr Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<FRowPtr>, OwnerTable)
		.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("ContentBrowser.AssetListView.TileTableRow")))
		.ShowSelection(false)
		.Padding(3.f)
		[
			Item.IsValid()
				? BuildAttackCard(*Item)
				: StaticCastSharedRef<SWidget>(
					SNew(STextBlock).Text(LOCTEXT("InvalidAttackCard", "Invalid attack")))
		];
}

TSoftObjectPtr<UPaperFlipbook> SCombatAttackBrowserPanel::FindAttackFlipbookSoftRef(
	const UPaper2DPlusCombatProfileAsset* Asset,
	FName MoveName)
{
	if (!Asset || !Asset->CharacterProfile || MoveName.IsNone())
	{
		return TSoftObjectPtr<UPaperFlipbook>();
	}
	if (const FFlipbookProfileEntry* Entry =
		Asset->CharacterProfile->FindFlipbookDataPtr(MoveName.ToString()))
	{
		return Entry->Identity.Flipbook;
	}
	return TSoftObjectPtr<UPaperFlipbook>();
}

TSharedRef<SWidget> SCombatAttackBrowserPanel::BuildAttackCard(const FPaper2DPlusCombatAttackDerivedData& Row)
{
	const FName MoveName = Row.MoveName;
	const FText Title = FText::FromName(MoveName);

	// Compact tag LEAF (last segment) instead of the full path so it fits the card.
	FString TagLeaf;
	if (Row.AttackTag.IsValid())
	{
		const FString Full = Row.AttackTag.GetTagName().ToString();
		int32 DotIndex = INDEX_NONE;
		TagLeaf = Full.FindLastChar(TEXT('.'), DotIndex) ? Full.RightChop(DotIndex + 1) : Full;
	}
	const FText Subtitle = TagLeaf.IsEmpty() ? LOCTEXT("CardUntagged", "untagged") : FText::FromString(TagLeaf);

	// Bottom badge line: reach + non-default weight + a dot for moves customized on this profile.
	// Default values stay silent so a freshly generated roster doesn't read as hand-tuned.
	TArray<FString> Badges;
	if (!Row.ForwardRangeLocal.IsNearlyZero())
	{
		Badges.Add(FString::Printf(TEXT("reach %.0f-%.0f"), Row.ForwardRangeLocal.X, Row.ForwardRangeLocal.Y));
	}
	if (!FMath::IsNearlyEqual(Row.BaseWeight, 1.0f))
	{
		Badges.Add(FString::Printf(TEXT("w %.2f"), Row.BaseWeight));
	}
	FString BadgeLine = FString::Join(Badges, TEXT(" · "));
	if (Row.bHasCombatProfileOption)
	{
		BadgeLine = BadgeLine.IsEmpty() ? TEXT("●") : BadgeLine + TEXT("  ●");
	}

	// Tile generation is the virtualization boundary. Resident flipbooks render immediately; a visible
	// unloaded row requests its soft reference asynchronously. Refresh never synchronously walks and loads
	// every move merely to paint a card.
	const TSoftObjectPtr<UPaperFlipbook> FlipbookRef =
		FindAttackFlipbookSoftRef(Session.IsValid() ? Session->GetAsset() : nullptr, MoveName);
	UPaperFlipbook* Flipbook = FlipbookRef.Get();
	bool bPreviewLoadFinishedWithoutAsset = false;
	if (!FlipbookRef.IsNull())
	{
		const FSoftObjectPath FlipbookPath = FlipbookRef.ToSoftObjectPath();
		if (Flipbook)
		{
			TouchThumbnail(FlipbookPath);
		}
		else
		{
			RequestThumbnailAsync(FlipbookPath);
			if (const TSharedPtr<FStreamableHandle>* Handle = ThumbnailBudget.Find(FlipbookPath))
			{
				bPreviewLoadFinishedWithoutAsset = Handle->IsValid() && (*Handle)->HasLoadCompleted();
			}
		}
	}

	const TSharedRef<SWidget> ThumbWidget = Flipbook
		? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(Flipbook))
		: StaticCastSharedRef<SWidget>(
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FlipbookRef.IsNull() || bPreviewLoadFinishedWithoutAsset
					? LOCTEXT("CardNoPreview", "no preview")
					: LOCTEXT("CardLoadingPreview", "loading…"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]);

	const FText Tip = FText::Format(
		LOCTEXT("CardTipFmt", "{0}\nTag: {1}{2}"),
		Title,
		Row.AttackTag.IsValid() ? FText::FromString(Row.AttackTag.GetTagName().ToString()) : LOCTEXT("CardTipNoTag", "(none)"),
		Row.bHasCombatProfileOption
			? LOCTEXT("CardTipCustomized", "\n● Customized on this profile")
			: LOCTEXT("CardTipInherited", "\nUses its tag defaults — select it to customize"));
	return SNew(SBox)
		.WidthOverride(SProfileCard::GetCanonicalCardWidth())
		.HeightOverride(SProfileCard::GetCanonicalCompactCardHeight())
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(0.0f)
			.ToolTipText(Tip)
			.OnClicked_Lambda([this, MoveName]()
			{
				if (Session.IsValid())
				{
					Session->SelectAttackByName(MoveName);
				}
				return FReply::Handled();
			})
			[
				SNew(SProfileCard)
				.IsSelected_Lambda([this, MoveName]() { return GetSelectedMoveName() == MoveName; })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 1.f, 0.f, 2.f)
					[
						SNew(SBox)
						.WidthOverride(SProfileCard::GetCanonicalThumbnailSize())
						.HeightOverride(SProfileCard::GetCanonicalThumbnailSize())
						[
							ThumbWidget
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Text(Title).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)).Justification(ETextJustify::Center)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Text(Subtitle).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.78f, 0.86f))).Justification(ETextJustify::Center)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(BadgeLine)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f))).Justification(ETextJustify::Center)
					]
				]
			]
		];
}

void SCombatAttackBrowserPanel::RequestThumbnailAsync(const FSoftObjectPath& FlipbookPath)
{
	if (FlipbookPath.IsNull())
	{
		return;
	}
	if (ThumbnailBudget.Contains(FlipbookPath))
	{
		TouchThumbnail(FlipbookPath);
		return;
	}

	TSharedPtr<FStreamableHandle> Handle =
		UAssetManager::GetStreamableManager().RequestAsyncLoad(
			FlipbookPath,
			FStreamableDelegate::CreateSP(
				this,
				&SCombatAttackBrowserPanel::HandleThumbnailLoaded,
				FlipbookPath));
	if (Handle.IsValid())
	{
		const bool bCompletedBeforeRetention = Handle->HasLoadCompleted();
		// Retaining the handle keeps a completed visible thumbnail resident until the derived Catalog is
		// explicitly refreshed or the bounded LRU evicts it. Off-screen cards never call this method because
		// STileView is virtualized. Touch inserts and trims in one step.
		ThumbnailBudget.Touch(FlipbookPath) = MoveTemp(Handle);
		if (bCompletedBeforeRetention && CardTileView.IsValid())
		{
			// RequestAsyncLoad may invoke its delegate inline for an already-resident asset, before the handle
			// enters our map. Refresh explicitly so that harmless ordering cannot strand the loading placeholder.
			CardTileView->RequestListRefresh();
		}
	}
}

void SCombatAttackBrowserPanel::TouchThumbnail(const FSoftObjectPath& FlipbookPath)
{
	// Only re-rank something already retained: touching an absent key would insert an empty entry and
	// let it evict a real one.
	if (ThumbnailBudget.Contains(FlipbookPath))
	{
		ThumbnailBudget.Touch(FlipbookPath);
	}
}

void SCombatAttackBrowserPanel::HandleThumbnailLoaded(FSoftObjectPath FlipbookPath)
{
	if (!ThumbnailBudget.Contains(FlipbookPath))
	{
		return;
	}
	if (CardTileView.IsValid())
	{
		CardTileView->RequestListRefresh();
	}
}

void SCombatAttackBrowserPanel::ResetThumbnailLoads()
{
	// The budget's release hook cancels in-flight loads for both eviction and reset.
	ThumbnailBudget.Reset();
}

#if WITH_DEV_AUTOMATION_TESTS
void SCombatAttackBrowserPanel::SetSearchTextForTests(const FString& InText)
{
	SearchText = InText;
	RebuildFilteredRows();
	SynchronizeSelection();
}

int32 SCombatAttackBrowserPanel::GenerateCardsForViewportForTests(const FVector2D& ViewportSize)
{
	if (!CardTileView.IsValid())
	{
		return 0;
	}
	CardTileView->RequestListRefresh();
	CardTileView->SlatePrepass(1.0f);
	CardTileView->Tick(
		FGeometry::MakeRoot(ViewportSize, FSlateLayoutTransform()),
		FPlatformTime::Seconds(),
		0.0f);
	return CardTileView->GetNumGeneratedChildren();
}

bool SCombatAttackBrowserPanel::RequestThumbnailForTests(const FSoftObjectPath& FlipbookPath)
{
	RequestThumbnailAsync(FlipbookPath);
	return ThumbnailBudget.Contains(FlipbookPath);
}
#endif

#undef LOCTEXT_NAMESPACE
