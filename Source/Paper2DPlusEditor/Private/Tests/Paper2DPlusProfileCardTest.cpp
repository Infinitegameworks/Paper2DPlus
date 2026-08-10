// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileCard.h"
#include "ProfileNavigatorPanel.h"

#include "Misc/AutomationTest.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	class FProfileCardTestSource : public IProfileItemPickerSource
	{
	public:
		FProfileCardTestSource()
		{
			for (int32 Index = 0; Index < 64; ++Index)
			{
				FProfilePickerItem Item;
				Item.Identity.SourceType = TEXT("ProfileCardTest");
				Item.Identity.FallbackKey = FString::Printf(TEXT("Item%d"), Index);
				Item.Label = FText::FromString(Item.Identity.FallbackKey);
				Item.CanonicalOrder = Index;
				Items.Add(MoveTemp(Item));
			}
			Selected = Items[0].Identity;
		}

		virtual FName GetSourceType() const override { return TEXT("ProfileCardTest"); }
		virtual FString GetLogicalCatalogScope() const override { return TEXT("ProfileCardTestScope"); }
		virtual void GetItems(TArray<FProfilePickerItem>& OutItems) const override { OutItems = Items; }
		virtual FProfileItemIdentity GetSelectedIdentity() const override { return Selected; }
		virtual bool SelectItem(const FProfileItemIdentity& Identity) override
		{
			Selected = Identity;
			return true;
		}
		virtual FOnProfileItemSourceChanged& OnSourceChanged() override { return SourceChanged; }

	private:
		TArray<FProfilePickerItem> Items;
		FProfileItemIdentity Selected;
		FOnProfileItemSourceChanged SourceChanged;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileCardStyleTest,
	"Paper2DPlus.Editor.ProfileCard.CharacterVisualContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusProfileCardStyleTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Selected border keeps the Character Profile accent"),
		SProfileCard::GetSelectionBorderColor(true),
		FLinearColor(0.28f, 0.44f, 0.68f, 1.0f));
	TestEqual(
		TEXT("Unselected border stays transparent"),
		SProfileCard::GetSelectionBorderColor(false),
		FLinearColor::Transparent);
	TestEqual(
		TEXT("Selected background keeps the Character Profile fill"),
		SProfileCard::GetCardBackgroundColor(true),
		FLinearColor(0.18f, 0.30f, 0.50f, 1.0f));
	TestEqual(
		TEXT("Unselected background keeps the Character Profile surface"),
		SProfileCard::GetCardBackgroundColor(false),
		FLinearColor(0.22f, 0.22f, 0.24f, 1.0f));
	TestEqual(TEXT("Profile cards share the Character card width"), SProfileCard::GetCanonicalCardWidth(), 150.0f);
	TestEqual(TEXT("Compact profile cards share one height"), SProfileCard::GetCanonicalCompactCardHeight(), 142.0f);
	TestEqual(TEXT("Profile card previews share one thumbnail size"), SProfileCard::GetCanonicalThumbnailSize(), 64.0f);

	const TSharedRef<SProfileCard> Card = SNew(SProfileCard)
		.IsSelected(true)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Profile card content")))
		];
	TestEqual(TEXT("Shared profile card constructs as its own Slate type"), Card->GetType(), FName(TEXT("SProfileCard")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileCardNavigatorVirtualizationTest,
	"Paper2DPlus.Editor.ProfileCard.NavigatorContentStaysVirtualized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusProfileCardNavigatorVirtualizationTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FProfileCardTestSource> Source = MakeShared<FProfileCardTestSource>();
	int32 ConstructedCardCount = 0;
	const TSharedRef<SProfileNavigatorPanel> Navigator = SNew(SProfileNavigatorPanel)
		.Source(Source)
		.Mode(EProfileNavigatorMode::Pinned)
		.OnGenerateItemContent(FOnGenerateProfileNavigatorItemContent::CreateLambda(
			[&ConstructedCardCount](const FProfileItemIdentity&, const TSharedRef<SWidget>& DefaultContent)
			{
				++ConstructedCardCount;
				return SNew(SProfileCard)[DefaultContent];
			}));

	const int32 GeneratedRows = Navigator->GenerateRowsForViewportForTests(FVector2D(320.0f, 180.0f));
	TestTrue(TEXT("A constrained viewport generates visible rows"), GeneratedRows > 0);
	TestTrue(TEXT("The custom card face is constructed for visible item rows"), ConstructedCardCount > 0);
	TestTrue(TEXT("The custom card face is not constructed for the full source"), ConstructedCardCount < 64);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
