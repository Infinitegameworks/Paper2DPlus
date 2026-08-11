// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusSettings.h"
#include "GameplayTagContainer.h"

/** Tag Colors registry resolution tests (worldless). Covers the ResolveTagColor contract:
 *  exact match, ANCESTOR fall-up, nearest-wins between an exact and an ancestor entry, and the
 *  not-found path. Uses the native Combat.Var.* starter tags (always registered) so a real tag
 *  hierarchy (Paper2DPlus.Combat.Var -> .Aggression/.CanPunish) is available without a UWorld.
 *  Mutates the settings CDO's TagColors array in-memory and restores it (never writes config). */

namespace
{
	FGameplayTag TagColorTest_Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound*/ false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTagColorResolution,
	"Paper2DPlus.TagColors.Resolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTagColorResolution::RunTest(const FString& Parameters)
{
	UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
	if (!TestNotNull(TEXT("Settings CDO exists"), Settings))
	{
		return false;
	}

	const FGameplayTag Parent = TagColorTest_Tag(TEXT("Paper2DPlus.Combat.Var"));
	const FGameplayTag ChildA = TagColorTest_Tag(TEXT("Paper2DPlus.Combat.Var.Aggression"));
	const FGameplayTag ChildB = TagColorTest_Tag(TEXT("Paper2DPlus.Combat.Var.CanPunish"));
	if (!TestTrue(TEXT("Native Combat.Var tags are registered"),
		Parent.IsValid() && ChildA.IsValid() && ChildB.IsValid()))
	{
		return false;
	}

	// Snapshot + clear so the test runs from a known empty registry; restore at the end.
	const TArray<FPaper2DPlusTagColor> Saved = Settings->TagColors;
	Settings->TagColors.Reset();

	const FLinearColor Red(1.f, 0.f, 0.f, 1.f);
	const FLinearColor Blue(0.f, 0.f, 1.f, 1.f);

	// 1. Empty registry -> not found.
	{
		bool bFound = true;
		(void)UPaper2DPlusSettings::ResolveTagColor(ChildA, bFound);
		TestFalse(TEXT("Empty registry: no color found"), bFound);
		TestFalse(TEXT("HasAnyTagColors is false when empty"), UPaper2DPlusSettings::HasAnyTagColors());
	}

	// 2. Exact match.
	Settings->TagColors.Emplace(ChildA, Red);
	{
		bool bFound = false;
		const FLinearColor C = UPaper2DPlusSettings::ResolveTagColor(ChildA, bFound);
		TestTrue(TEXT("Exact match found"), bFound);
		TestTrue(TEXT("Exact match returns the exact color"), C.Equals(Red));
		TestTrue(TEXT("HasAnyTagColors is true when populated"), UPaper2DPlusSettings::HasAnyTagColors());
	}

	// 3. Ancestor fall-up: a sibling with NO own color resolves to the parent's color.
	Settings->TagColors.Reset();
	Settings->TagColors.Emplace(Parent, Blue);
	{
		bool bFound = false;
		const FLinearColor C = UPaper2DPlusSettings::ResolveTagColor(ChildB, bFound);
		TestTrue(TEXT("Ancestor color found for uncolored child"), bFound);
		TestTrue(TEXT("Child resolves to ancestor color"), C.Equals(Blue));
	}

	// 4. Nearest wins: an exact entry beats an ancestor entry.
	Settings->TagColors.Reset();
	Settings->TagColors.Emplace(Parent, Blue);
	Settings->TagColors.Emplace(ChildA, Red);
	{
		bool bFound = false;
		const FLinearColor C = UPaper2DPlusSettings::ResolveTagColor(ChildA, bFound);
		TestTrue(TEXT("Exact-over-ancestor found"), bFound);
		TestTrue(TEXT("Exact entry wins over ancestor"), C.Equals(Red));
	}

	// 5. Unrelated tag with no ancestor in the registry -> not found.
	{
		bool bFound = true;
		(void)UPaper2DPlusSettings::ResolveTagColor(TagColorTest_Tag(TEXT("Paper2DPlus.Combat")), bFound);
		TestFalse(TEXT("Unrelated/ancestor-only-below tag: not found"), bFound);
	}

	// 6. Invalid tag -> not found (no crash).
	{
		bool bFound = true;
		(void)UPaper2DPlusSettings::ResolveTagColor(FGameplayTag(), bFound);
		TestFalse(TEXT("Invalid tag: not found"), bFound);
	}

	Settings->TagColors = Saved;
	return true;
}

#endif // WITH_EDITOR
