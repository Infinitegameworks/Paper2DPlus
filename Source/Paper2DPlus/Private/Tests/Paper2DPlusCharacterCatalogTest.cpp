// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"

namespace
{
	template <typename T>
	TSoftObjectPtr<T> CatalogTest_Soft(const TCHAR* ObjectPath)
	{
		return TSoftObjectPtr<T>(FSoftObjectPath(ObjectPath));
	}

	FPaper2DPlusCharacterCatalogEntry CatalogTest_Entry(const TCHAR* CharacterPath)
	{
		FPaper2DPlusCharacterCatalogEntry Entry;
		Entry.CharacterProfile = CatalogTest_Soft<UPaper2DPlusCharacterProfileAsset>(CharacterPath);
		return Entry;
	}

	FString CatalogTest_CharacterPath(const FPaper2DPlusCharacterCatalogEntry& Entry)
	{
		return Entry.CharacterProfile.ToSoftObjectPath().ToString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterCatalogEmptyAndCompletionTest,
	"Paper2DPlus.CharacterCatalog.Runtime.EmptyPrimaryAssetAndCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterCatalogEmptyAndCompletionTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	TestEqual(
		TEXT("Catalog Primary Asset type is stable"),
		Catalog->GetPrimaryAssetId().PrimaryAssetType,
		UPaper2DPlusCharacterCatalogAsset::CharacterCatalogPrimaryAssetType());
	TestEqual(TEXT("Empty enumeration"), Catalog->GetCatalogEntries().Num(), 0);
	TestEqual(TEXT("Empty group names"), Catalog->GetCatalogGroupNames().Num(), 0);

	TArray<FPaper2DPlusCharacterCatalogIssue> Issues;
	TestTrue(TEXT("Empty Catalog is structurally valid"), Catalog->ValidateCharacterCatalogAsset(Issues));
	TestEqual(TEXT("Empty Catalog emits no issues"), Issues.Num(), 0);

	FPaper2DPlusCharacterCatalogEntry Entry = CatalogTest_Entry(TEXT("/Game/Catalog/Hero.Hero"));
	Entry.Requirements.bRequireLayer = true;
	Entry.LayerProfile = CatalogTest_Soft<UPaper2DPlusCharacterLayerAsset>(TEXT("/Game/Catalog/HeroLayers.HeroLayers"));
	const FPaper2DPlusCharacterCatalogCompletion Complete = Catalog->GetEntryCompletion(Entry);
	TestTrue(TEXT("Assigned required Layer is complete"), Complete.bComplete);
	TestEqual(TEXT("One required slot"), Complete.RequiredCount, 1);
	TestEqual(TEXT("One present required slot"), Complete.PresentRequiredCount, 1);
	TestEqual(TEXT("Absent optional Effect and Combat are neutral"), Complete.MissingRequiredCompanions.Num(), 0);

	Entry.Requirements.bRequireCombat = true;
	const FPaper2DPlusCharacterCatalogCompletion Incomplete = Catalog->GetEntryCompletion(Entry);
	TestFalse(TEXT("Missing required Combat is incomplete"), Incomplete.bComplete);
	TestEqual(TEXT("Only required Combat is missing"), Incomplete.MissingRequiredCompanions.Num(), 1);
	if (Incomplete.MissingRequiredCompanions.Num() == 1)
	{
		TestEqual(TEXT("Missing companion identity"), Incomplete.MissingRequiredCompanions[0], EPaper2DPlusCatalogCompanion::Combat);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterCatalogDuplicateAndTagTest,
	"Paper2DPlus.CharacterCatalog.Runtime.DuplicateIdentityAndTagQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterCatalogDuplicateAndTagTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	FPaper2DPlusCharacterCatalogEntry First = CatalogTest_Entry(TEXT("/Game/Catalog/Hero.Hero"));
	First.LayerProfile = CatalogTest_Soft<UPaper2DPlusCharacterLayerAsset>(TEXT("/Game/Catalog/FirstLayer.FirstLayer"));
	First.Tags.AddTag(Paper2DPlusAnimationTags::Context_Airborne.GetTag());
	First.Tags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy.GetTag());

	FPaper2DPlusCharacterCatalogEntry Duplicate = CatalogTest_Entry(TEXT("/game/catalog/hero.hero"));
	Duplicate.LayerProfile = CatalogTest_Soft<UPaper2DPlusCharacterLayerAsset>(TEXT("/Game/Catalog/SecondLayer.SecondLayer"));
	Catalog->Entries = { First, Duplicate };

	TArray<FPaper2DPlusCharacterCatalogIssue> Issues;
	TestFalse(TEXT("Duplicate character row is an Error"), Catalog->ValidateCharacterCatalogAsset(Issues));
	TestTrue(TEXT("Stable duplicate code is present"), Issues.ContainsByPredicate([](const FPaper2DPlusCharacterCatalogIssue& Issue)
	{
		return Issue.Severity == EPaper2DPlusCharacterCatalogIssueSeverity::Error
			&& Issue.Code == TEXT("Entry.DuplicateCharacter");
	}));
	TestEqual(TEXT("Enumeration deduplicates by normalized path"), Catalog->GetCatalogEntries().Num(), 1);

	FPaper2DPlusCharacterCatalogEntry Found;
	TestTrue(TEXT("Lookup finds normalized path"), Catalog->FindEntryByCharacterProfile(Duplicate.CharacterProfile, Found));
	TestEqual(
		TEXT("First valid duplicate wins"),
		Found.LayerProfile.ToSoftObjectPath().ToString(),
		First.LayerProfile.ToSoftObjectPath().ToString());

	const FGameplayTag ContextParent = Paper2DPlusAnimationTags::Context.GetTag();
	const FGameplayTag Airborne = Paper2DPlusAnimationTags::Context_Airborne.GetTag();
	TestEqual(TEXT("Hierarchical parent query matches descendant"), Catalog->GetEntriesWithTag(ContextParent, false).Num(), 1);
	TestEqual(TEXT("Exact parent query does not match descendant"), Catalog->GetEntriesWithTag(ContextParent, true).Num(), 0);
	TestEqual(TEXT("Exact leaf query matches leaf"), Catalog->GetEntriesWithTag(Airborne, true).Num(), 1);
	TestEqual(TEXT("Invalid single tag matches nothing"), Catalog->GetEntriesWithTag(FGameplayTag(), false).Num(), 0);

	FGameplayTagContainer AllQuery;
	AllQuery.AddTag(ContextParent);
	AllQuery.AddTag(Paper2DPlusAnimationTags::Combat.GetTag());
	TestEqual(TEXT("All hierarchical dimensions match"), Catalog->GetEntriesWithAllTags(AllQuery, false).Num(), 1);

	FGameplayTagContainer AnyQuery;
	AnyQuery.AddTag(Paper2DPlusAnimationTags::Context_Swimming.GetTag());
	AnyQuery.AddTag(Paper2DPlusAnimationTags::Combat_Heavy.GetTag());
	TestEqual(TEXT("Any query matches one dimension"), Catalog->GetEntriesWithAnyTags(AnyQuery, false).Num(), 1);
	TestEqual(TEXT("Empty All matches nothing"), Catalog->GetEntriesWithAllTags(FGameplayTagContainer(), false).Num(), 0);
	TestEqual(TEXT("Empty Any matches nothing"), Catalog->GetEntriesWithAnyTags(FGameplayTagContainer(), false).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterCatalogOrderedGroupTest,
	"Paper2DPlus.CharacterCatalog.Runtime.OrderedExplicitGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterCatalogOrderedGroupTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	FPaper2DPlusCharacterCatalogEntry A = CatalogTest_Entry(TEXT("/Game/Catalog/A.A"));
	FPaper2DPlusCharacterCatalogEntry B = CatalogTest_Entry(TEXT("/Game/Catalog/B.B"));
	FPaper2DPlusCharacterCatalogEntry C = CatalogTest_Entry(TEXT("/Game/Catalog/C.C"));
	Catalog->Entries = { A, B, C };

	FPaper2DPlusCharacterCatalogGroup Group;
	Group.GroupName = TEXT("NemesisPool");
	Group.Members = {
		B.CharacterProfile,
		A.CharacterProfile,
		A.CharacterProfile,
		CatalogTest_Soft<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Catalog/Missing.Missing")),
		C.CharacterProfile
	};
	Catalog->Groups.Add(Group);

	TArray<FPaper2DPlusCharacterCatalogEntry> Ordered = Catalog->GetEntriesInGroup(TEXT("NemesisPool"));
	TestEqual(TEXT("Repeated and out-of-Catalog members are skipped"), Ordered.Num(), 3);
	if (Ordered.Num() == 3)
	{
		TestEqual(TEXT("B remains first"), CatalogTest_CharacterPath(Ordered[0]), CatalogTest_CharacterPath(B));
		TestEqual(TEXT("A remains second"), CatalogTest_CharacterPath(Ordered[1]), CatalogTest_CharacterPath(A));
		TestEqual(TEXT("C remains third"), CatalogTest_CharacterPath(Ordered[2]), CatalogTest_CharacterPath(C));
	}

	Catalog->Entries[0].Tags.AddTag(Paper2DPlusAnimationTags::Context_Crouching.GetTag());
	Ordered = Catalog->GetEntriesInGroup(TEXT("NemesisPool"));
	if (Ordered.Num() == 3)
	{
		TestEqual(TEXT("Tag edits never change explicit group order"), CatalogTest_CharacterPath(Ordered[0]), CatalogTest_CharacterPath(B));
		TestEqual(TEXT("Tag edits retain A position"), CatalogTest_CharacterPath(Ordered[1]), CatalogTest_CharacterPath(A));
	}

	TArray<FPaper2DPlusCharacterCatalogIssue> Issues;
	Catalog->ValidateCharacterCatalogAsset(Issues);
	TestTrue(TEXT("Repeated membership remains visible to validation"), Issues.ContainsByPredicate([](const FPaper2DPlusCharacterCatalogIssue& Issue)
	{
		return Issue.Code == TEXT("Group.DuplicateMember");
	}));
	TestTrue(TEXT("Out-of-Catalog membership remains visible to validation"), Issues.ContainsByPredicate([](const FPaper2DPlusCharacterCatalogIssue& Issue)
	{
		return Issue.Code == TEXT("Group.OutOfCatalogMember");
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterCatalogExpectedAnimationTagsTest,
	"Paper2DPlus.CharacterCatalog.Runtime.ExpectedAnimationTagsFollowMembership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterCatalogExpectedAnimationTagsTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	const FPaper2DPlusCharacterCatalogEntry Hero = CatalogTest_Entry(TEXT("/Game/Catalog/Hero.Hero"));
	Catalog->Entries.Add(Hero);
	Catalog->ExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy.GetTag());
	Catalog->ExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Context_Airborne.GetTag());

	FPaper2DPlusCharacterCatalogGroup Playable;
	Playable.GroupName = TEXT("Playable");
	Playable.Members.Add(CatalogTest_Soft<UPaper2DPlusCharacterProfileAsset>(TEXT("/game/catalog/hero.hero")));
	Playable.AdditionalExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy.GetTag());
	Playable.AdditionalExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Combo.GetTag());

	FPaper2DPlusCharacterCatalogGroup Aquatic;
	Aquatic.GroupName = TEXT("Aquatic");
	Aquatic.Members.Add(Hero.CharacterProfile);
	Aquatic.AdditionalExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Context_Airborne.GetTag());
	Aquatic.AdditionalExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Context_Swimming.GetTag());

	FPaper2DPlusCharacterCatalogGroup NonMember;
	NonMember.GroupName = TEXT("NonMember");
	NonMember.Members.Add(CatalogTest_Soft<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Catalog/Other.Other")));
	NonMember.AdditionalExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Light.GetTag());
	Catalog->Groups = { Playable, Aquatic, NonMember };

	FGameplayTagContainer ExpectedTags;
	ExpectedTags.AddTag(Paper2DPlusAnimationTags::Combat_Grab.GetTag());
	TestTrue(
		TEXT("Normalized roster identity resolves expected tags"),
		Catalog->GetExpectedAnimationTagsForCharacter(Hero.CharacterProfile, ExpectedTags));
	TestEqual(TEXT("Base and all member-group extras form one deduped union"), ExpectedTags.Num(), 4);
	TestTrue(TEXT("Base Heavy tag is present"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy.GetTag()));
	TestTrue(TEXT("Base Airborne tag is present"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Context_Airborne.GetTag()));
	TestTrue(TEXT("First member-group Combo tag is present"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Combo.GetTag()));
	TestTrue(TEXT("Second member-group Swimming tag is present"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Context_Swimming.GetTag()));
	TestFalse(TEXT("Output is reset before resolution"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Grab.GetTag()));
	TestFalse(TEXT("A non-member group's extras are excluded"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Light.GetTag()));

	Catalog->Groups[0].GroupName = TEXT("Heroes");
	ExpectedTags.Reset();
	TestTrue(
		TEXT("Renaming a group preserves membership-aware resolution"),
		Catalog->GetExpectedAnimationTagsForCharacter(Hero.CharacterProfile, ExpectedTags));
	TestTrue(TEXT("Renaming carries the group's extras with the group struct"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Combo.GetTag()));

	Catalog->Groups.RemoveAt(0);
	ExpectedTags.Reset();
	TestTrue(
		TEXT("Deleting a group keeps the roster member resolvable"),
		Catalog->GetExpectedAnimationTagsForCharacter(Hero.CharacterProfile, ExpectedTags));
	TestFalse(TEXT("Deleting the group removes its extras"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Combo.GetTag()));

	UPaper2DPlusCharacterCatalogAsset* EmptyExpectations = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	EmptyExpectations->Entries.Add(Hero);
	ExpectedTags.AddTag(Paper2DPlusAnimationTags::Combat_Block.GetTag());
	TestTrue(
		TEXT("A roster member with no declarations still resolves"),
		EmptyExpectations->GetExpectedAnimationTagsForCharacter(Hero.CharacterProfile, ExpectedTags));
	TestTrue(TEXT("Empty base and no groups yield an empty expected set"), ExpectedTags.IsEmpty());

	ExpectedTags.AddTag(Paper2DPlusAnimationTags::Combat_Block.GetTag());
	TestFalse(
		TEXT("A character absent from the normalized roster does not resolve"),
		Catalog->GetExpectedAnimationTagsForCharacter(
			CatalogTest_Soft<UPaper2DPlusCharacterProfileAsset>(TEXT("/Game/Catalog/Absent.Absent")),
			ExpectedTags));
	TestTrue(TEXT("Absent-member failure still resets output"), ExpectedTags.IsEmpty());

	// Group validity mirrors GetEntriesInGroup: an unnamed group and a later duplicate-named
	// group are unaddressable, so membership in them contributes no expectations.
	UPaper2DPlusCharacterCatalogAsset* ValidityCatalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	ValidityCatalog->Entries.Add(Hero);
	ValidityCatalog->ExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy.GetTag());

	FPaper2DPlusCharacterCatalogGroup ValidGroup;
	ValidGroup.GroupName = TEXT("Valid");
	ValidGroup.Members.Add(Hero.CharacterProfile);
	ValidGroup.AdditionalExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Combo.GetTag());

	FPaper2DPlusCharacterCatalogGroup UnnamedGroup;
	UnnamedGroup.Members.Add(Hero.CharacterProfile);
	UnnamedGroup.AdditionalExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Light.GetTag());

	FPaper2DPlusCharacterCatalogGroup DuplicateNamedGroup;
	DuplicateNamedGroup.GroupName = TEXT("Valid");
	DuplicateNamedGroup.Members.Add(Hero.CharacterProfile);
	DuplicateNamedGroup.AdditionalExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Grab.GetTag());

	ValidityCatalog->Groups = { ValidGroup, UnnamedGroup, DuplicateNamedGroup };
	ExpectedTags.Reset();
	TestTrue(
		TEXT("Membership in unaddressable groups never blocks resolution"),
		ValidityCatalog->GetExpectedAnimationTagsForCharacter(Hero.CharacterProfile, ExpectedTags));
	TestEqual(TEXT("Only the base container and the valid group contribute"), ExpectedTags.Num(), 2);
	TestTrue(TEXT("Base tag survives group-validity filtering"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy.GetTag()));
	TestTrue(TEXT("The addressable group's extras are included"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Combo.GetTag()));
	TestFalse(TEXT("An unnamed group's extras are excluded"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Light.GetTag()));
	TestFalse(TEXT("A duplicate-named group's extras are excluded"),
		ExpectedTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Grab.GetTag()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterCatalogSoftQueryTest,
	"Paper2DPlus.CharacterCatalog.Runtime.QueriesStaySoftAndPassive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterCatalogSoftQueryTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	FPaper2DPlusCharacterCatalogEntry Entry = CatalogTest_Entry(TEXT("/Game/Catalog/UnloadedCharacter.UnloadedCharacter"));
	Entry.LayerProfile = CatalogTest_Soft<UPaper2DPlusCharacterLayerAsset>(TEXT("/Game/Catalog/UnloadedLayer.UnloadedLayer"));
	Entry.Requirements.bRequireLayer = true;
	Entry.Tags.AddTag(Paper2DPlusAnimationTags::Context_Airborne.GetTag());
	Catalog->Entries.Add(Entry);

	FPaper2DPlusCharacterCatalogGroup Group;
	Group.GroupName = TEXT("VisibleEnemies");
	Group.Members.Add(Entry.CharacterProfile);
	Group.AdditionalExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Light.GetTag());
	Catalog->Groups.Add(Group);
	Catalog->ExpectedAnimationTags.AddTag(Paper2DPlusAnimationTags::Context_Crouching.GetTag());

	TestNull(TEXT("Character starts unloaded"), Entry.CharacterProfile.Get());
	TestNull(TEXT("Layer starts unloaded"), Entry.LayerProfile.Get());
	const bool bWasPackageDirty = Catalog->GetOutermost()->IsDirty();

	FPaper2DPlusCharacterCatalogEntry Found;
	FPaper2DPlusCharacterCatalogCompletion Completion;
	Catalog->GetCatalogEntries();
	Catalog->FindEntryByCharacterProfile(Entry.CharacterProfile, Found);
	Catalog->GetEntriesWithTag(Paper2DPlusAnimationTags::Context.GetTag());
	Catalog->GetEntriesWithAllTags(Entry.Tags);
	Catalog->GetEntriesWithAnyTags(Entry.Tags);
	Catalog->GetEntriesInGroup(Group.GroupName);
	Catalog->GetCharacterCompletion(Entry.CharacterProfile, Completion);
	FGameplayTagContainer ExpectedTags;
	Catalog->GetExpectedAnimationTagsForCharacter(Entry.CharacterProfile, ExpectedTags);
	TArray<FPaper2DPlusCharacterCatalogIssue> Issues;
	Catalog->ValidateCharacterCatalogAsset(Issues);

	TestNull(TEXT("Queries do not load Character Profile"), Entry.CharacterProfile.Get());
	TestNull(TEXT("Queries do not load Layer Profile"), Entry.LayerProfile.Get());
	TestTrue(TEXT("Soft required reference still counts as complete"), Completion.bComplete);
	TestEqual(TEXT("Expected-tag query returns base plus group extras"), ExpectedTags.Num(), 2);
	TestEqual(TEXT("Queries do not change package dirty state"), Catalog->GetOutermost()->IsDirty(), bWasPackageDirty);
	return true;
}

#endif // WITH_EDITOR
