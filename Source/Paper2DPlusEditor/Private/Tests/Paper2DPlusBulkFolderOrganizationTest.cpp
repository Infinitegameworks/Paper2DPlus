// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "BulkFolderOrganizationUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusBulkFolderAnimationGroups,
	"Paper2DPlus.BulkExtractor.Folders.AnimationGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusBulkFolderAnimationGroups::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlus::BulkFolderOrganization;

	const TArray<FString> TagPaths = {
		TEXT("Paper2DPlus.Animation"),
		TEXT("Paper2DPlus.Animation.Locomotion.Run"),
		TEXT("Paper2DPlus.Animation.Reaction.Hit"),
		TEXT("Paper2DPlus.Animation.Locomotion"),
		TEXT("Paper2DPlus.Animation.Combat"),
		TEXT("Paper2DPlus.Animation.Ability"),
		TEXT("Paper2DPlus.Animation.Combat.Attack"),
		TEXT("Paper2DPlus.Effect.Combat")
	};

	const TArray<FString> FolderNames = BuildAnimationGroupFolderNames(TagPaths);
	TestEqual(TEXT("Only direct animation groups become folders"), FolderNames.Num(), 3);
	if (FolderNames.Num() == 3)
	{
		TestEqual(TEXT("Groups are sorted: Ability"), FolderNames[0], FString(TEXT("Ability")));
		TestEqual(TEXT("Groups are sorted: Combat"), FolderNames[1], FString(TEXT("Combat")));
		TestEqual(TEXT("Groups are sorted: Locomotion"), FolderNames[2], FString(TEXT("Locomotion")));
	}

	const TArray<FString> ExistingRootFolders = {
		TEXT("combat"),
		TEXT("Custom")
	};
	const TArray<FString> Candidates = {
		TEXT("Ability"),
		TEXT("Combat"),
		TEXT("Locomotion"),
		TEXT("LOCOMOTION")
	};
	const TArray<FString> MissingFolders = FindMissingRootFolderNames(Candidates, ExistingRootFolders);
	TestEqual(TEXT("Existing and repeated names are filtered case-insensitively"), MissingFolders.Num(), 2);
	if (MissingFolders.Num() == 2)
	{
		TestEqual(TEXT("Ability remains missing"), MissingFolders[0], FString(TEXT("Ability")));
		TestEqual(TEXT("Only the first Locomotion spelling remains"), MissingFolders[1], FString(TEXT("Locomotion")));
	}
	return true;
}

#endif // WITH_EDITOR
