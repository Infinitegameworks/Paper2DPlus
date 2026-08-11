// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileTools/SProfileReExtractTool.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Texture2D.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "ProfilePropertyRow.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "TextureReimporter.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusProfileTools"

void SProfileReExtractTool::Construct(const FArguments& InArgs)
{
	Profile = InArgs._Profile;

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			FProfilePropertyRowUtils::MakeSectionTitle(LOCTEXT("ReExtractTitle", "Re-extract"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			FProfilePropertyRowUtils::MakeSectionHint(
				LOCTEXT("ReExtractHint",
					"Re-cuts sprites from their source texture using the detection settings stored when they were first extracted. Each animation runs on its own, so one failure cannot leave the rest half-applied."))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ReExtractRun", "Re-extract Selected"))
				.IsEnabled_Lambda([this]() { return !bRunning && HasSelection(); })
				.OnClicked_Lambda([this]() { RunSelected(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ReExtractRefresh", "Refresh"))
				.IsEnabled_Lambda([this]() { return !bRunning; })
				.OnClicked_Lambda([this]() { RefreshRows(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SProfileReExtractTool::GetSummaryText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ListBox, SVerticalBox)
			]
		]
	];

	RefreshRows();
}

bool SProfileReExtractTool::HasSelection() const
{
	for (const TSharedPtr<FProfileReExtractRow>& Row : Rows)
	{
		if (Row.IsValid() && Row->bSelected && Row->bEligible)
		{
			return true;
		}
	}
	return false;
}

FText SProfileReExtractTool::GetSummaryText() const
{
	if (bRunning)
	{
		return LOCTEXT("ReExtractRunning", "Re-extracting…");
	}
	if (!LastRunSummary.IsEmpty())
	{
		return LastRunSummary;
	}

	int32 Eligible = 0;
	for (const TSharedPtr<FProfileReExtractRow>& Row : Rows)
	{
		if (Row.IsValid() && Row->bEligible)
		{
			++Eligible;
		}
	}
	return FText::Format(
		LOCTEXT("ReExtractSummaryFmt", "{0} of {1} animation(s) can be re-extracted."),
		FText::AsNumber(Eligible), FText::AsNumber(Rows.Num()));
}

TArray<FString> SProfileReExtractTool::FindOtherProfilesUsingSprites(
	const UPaper2DPlusCharacterProfileAsset* Self,
	const TArray<UPaperSprite*>& Sprites)
{
	TArray<FString> Out;
	if (Sprites.Num() == 0)
	{
		return Out;
	}

	TSet<UPaperSprite*> Wanted(Sprites);

	// Walk loaded profiles rather than asking the registry who references the texture. The registry's
	// referencer graph is built from SAVED package headers, so a profile created in this session --
	// exactly the case the path-keyed texture watcher also misses -- has an asset entry but no
	// dependency edge, and would be invisible to a referencer query.
	for (TObjectIterator<UPaper2DPlusCharacterProfileAsset> It; It; ++It)
	{
		UPaper2DPlusCharacterProfileAsset* Other = *It;
		if (!Other || Other == Self || Other->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}
		bool bShares = false;
		for (const FFlipbookProfileEntry& Entry : Other->Flipbooks)
		{
			UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.Get();
			if (!Flipbook)
			{
				continue;
			}
			for (int32 i = 0; i < Flipbook->GetNumKeyFrames() && !bShares; ++i)
			{
				if (Wanted.Contains(Flipbook->GetKeyFrameChecked(i).Sprite))
				{
					bShares = true;
				}
			}
			if (bShares)
			{
				break;
			}
		}
		if (bShares)
		{
			Out.AddUnique(Other->GetName());
		}
	}
	return Out;
}

void SProfileReExtractTool::RefreshRows()
{
	Rows.Reset();
	LastRunSummary = FText::GetEmpty();

	UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset)
	{
		RebuildList();
		return;
	}

	for (int32 EntryIndex = 0; EntryIndex < Asset->Flipbooks.Num(); ++EntryIndex)
	{
		const FFlipbookProfileEntry& Entry = Asset->Flipbooks[EntryIndex];
		TSharedRef<FProfileReExtractRow> Row = MakeShared<FProfileReExtractRow>();
		Row->EntryIndex = EntryIndex;
		Row->AnimationName = Entry.Identity.FlipbookName;

		UTexture2D* SourceTexture = Entry.SourceTexture.Get();
		Row->SourceTextureName = SourceTexture ? SourceTexture->GetName() : FString();

		// Eligibility is decided from the STORED detection params, and a missing or degenerate set
		// fails loudly here rather than as a silent no-op inside the reimporter.
		const FAlignmentMetadata& Alignment = Entry.AlignmentData;
		if (!SourceTexture)
		{
			Row->Reason = TEXT("No source texture recorded for this animation.");
		}
		else if (Alignment.GridDims.X <= 0 || Alignment.GridDims.Y <= 0)
		{
			Row->Reason = TEXT("Stored detection settings are missing or incomplete (no grid recorded), so this animation cannot be re-cut. Re-extract it from the sprite extractor instead.");
		}
		else if (Alignment.OriginalCellSize.X <= 0 || Alignment.OriginalCellSize.Y <= 0)
		{
			Row->Reason = TEXT("Stored detection settings record no cell size, so this animation cannot be re-cut.");
		}
		else
		{
			Row->bEligible = true;
		}

		// Shared-sprite warning: the same condition the in-place repair path refuses on.
		if (UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.Get())
		{
			TArray<UPaperSprite*> Sprites;
			for (int32 i = 0; i < Flipbook->GetNumKeyFrames(); ++i)
			{
				if (UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(i).Sprite)
				{
					Sprites.AddUnique(Sprite);
				}
			}
			Row->SharedWithProfiles = FindOtherProfilesUsingSprites(Asset, Sprites);
		}

		Rows.Add(Row);
	}

	RebuildList();
}

bool SProfileReExtractTool::SelectAnimation(const FString& AnimationName)
{
	bool bFound = false;
	for (const TSharedPtr<FProfileReExtractRow>& Row : Rows)
	{
		if (!Row.IsValid())
		{
			continue;
		}
		const bool bMatch = Row->AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase);
		Row->bSelected = bMatch;
		bFound |= bMatch;
	}
	RebuildList();
	return bFound;
}

void SProfileReExtractTool::RunSelected()
{
	UPaper2DPlusCharacterProfileAsset* Asset = Profile.Get();
	if (!Asset || bRunning)
	{
		return;
	}

	bRunning = true;
	int32 Succeeded = 0;
	int32 Failed = 0;
	int32 TotalSprites = 0;

	for (const TSharedPtr<FProfileReExtractRow>& Row : Rows)
	{
		if (!Row.IsValid() || !Row->bSelected || !Row->bEligible)
		{
			continue;
		}

		Row->bRan = true;

		// ONE transaction per animation. See the header: a whole-profile transaction would imply an
		// all-or-nothing rollback that does not exist here.
		FScopedTransaction Transaction(FText::Format(
			LOCTEXT("ReExtractTransactionFmt", "Re-extract {0}"),
			FText::FromString(Row->AnimationName)));
		Asset->Modify();

		const FTextureReimportResult Result = FTextureReimporter::ReimportFromTexture(
			FString(),
			Asset,
			Row->EntryIndex);

		Row->bSucceeded = Result.bSuccess;
		Row->SpritesUpdated = Result.SpritesUpdated;

		if (Result.bSuccess)
		{
			++Succeeded;
			TotalSprites += Result.SpritesUpdated;
			Row->Outcome = Result.SpritesUpdated > 0
				? FString::Printf(TEXT("Updated %d sprite(s)."), Result.SpritesUpdated)
				: TEXT("Already up to date — nothing changed.");
			if (Result.Conflicts.Num() > 0)
			{
				Row->Outcome += FString::Printf(
					TEXT(" %d conflict(s) reported."), Result.Conflicts.Num());
			}
		}
		else
		{
			++Failed;
			Row->Outcome = Result.Warnings.Num() > 0
				? FString::Join(Result.Warnings, TEXT(" "))
				: TEXT("Re-extraction failed; nothing was changed for this animation.");
		}
	}

	bRunning = false;
	LastRunSummary = FText::Format(
		LOCTEXT("ReExtractRunSummaryFmt", "{0} succeeded, {1} failed, {2} sprite(s) updated."),
		FText::AsNumber(Succeeded), FText::AsNumber(Failed), FText::AsNumber(TotalSprites));

	RebuildList();
}

void SProfileReExtractTool::RebuildList()
{
	if (!ListBox.IsValid())
	{
		return;
	}
	ListBox->ClearChildren();

	if (Rows.Num() == 0)
	{
		ListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ReExtractNoAnimations", "This profile has no animations to re-extract."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		return;
	}

	for (const TSharedPtr<FProfileReExtractRow>& Row : Rows)
	{
		if (!Row.IsValid())
		{
			continue;
		}
		TSharedPtr<FProfileReExtractRow> RowRef = Row;

		ListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			FProfilePropertyRowUtils::MakeCustomRow(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsEnabled(RowRef->bEligible)
					.IsChecked_Lambda([RowRef]()
					{
						return RowRef->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([RowRef](ECheckBoxState NewState)
					{
						RowRef->bSelected = (NewState == ECheckBoxState::Checked);
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(RowRef->AnimationName))
				],

				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(RowRef->SourceTextureName.IsEmpty()
						? LOCTEXT("ReExtractNoTexture", "No source texture")
						: FText::FromString(RowRef->SourceTextureName))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(RowRef->Reason))
					.Visibility(RowRef->Reason.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.65f, 0.18f)))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::Format(
						LOCTEXT("ReExtractSharedFmt", "Shared with: {0}. Re-extracting changes their sprites too."),
						FText::FromString(FString::Join(RowRef->SharedWithProfiles, TEXT(", ")))))
					.Visibility(RowRef->SharedWithProfiles.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.30f, 0.25f)))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(RowRef->Outcome))
					.Visibility(RowRef->bRan && !RowRef->Outcome.IsEmpty()
						? EVisibility::Visible : EVisibility::Collapsed)
					.ColorAndOpacity(RowRef->bSucceeded
						? FSlateColor(FLinearColor(0.30f, 0.80f, 0.42f))
						: FSlateColor(FLinearColor(0.95f, 0.30f, 0.25f)))
					.AutoWrapText(true)
				])
		];
	}
}

#if WITH_DEV_AUTOMATION_TESTS
TSharedPtr<FProfileReExtractRow> SProfileReExtractTool::FindRowForTests(const FString& AnimationName) const
{
	for (const TSharedPtr<FProfileReExtractRow>& Row : Rows)
	{
		if (Row.IsValid() && Row->AnimationName.Equals(AnimationName, ESearchCase::IgnoreCase))
		{
			return Row;
		}
	}
	return nullptr;
}
#endif

#undef LOCTEXT_NAMESPACE
