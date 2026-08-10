// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusEffectTags.h"
#include "PaperFlipbook.h"
#include "UObject/UnrealType.h"

namespace
{
	/**
	 * A library row authored the old way: a name plus the per-row spawn defaults.
	 *
	 * The Effect Profile ASSET and its deprecated-but-callable Blueprint resolvers are still shipped
	 * surface. The Frame Cue classes that used to consume them are gone, so this fixture now serves the
	 * resolver coverage directly instead of via a cue.
	 */
	UPaper2DPlusEffectProfileAsset* MakeLegacyResolverProfile()
	{
		UPaper2DPlusEffectProfileAsset* Profile = NewObject<UPaper2DPlusEffectProfileAsset>();
		FPaper2DPlusEffectProfileEntry& Entry = Profile->Effects.AddDefaulted_GetRef();
		Entry.EffectName = TEXT("SlashSpark");
		Entry.CategoryTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
		Entry.TypeTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
		Entry.EffectFlipbook = NewObject<UPaperFlipbook>(Profile, TEXT("FB_SlashSpark"));
		Entry.Offset = FVector2D(18.0, -4.0);
		Entry.Rotation = 12.0f;
		Entry.Scale = FVector2D(1.5, 0.75);
		Entry.bFlipWithCharacter = false;
		Entry.Tint = FLinearColor(0.9f, 0.8f, 1.0f, 1.0f);
		return Profile;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectProfileLibraryQueryTest,
	"Paper2DPlus.EffectProfile.Library.SchemaAndQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectProfileLibraryQueryTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Fresh = NewObject<UPaper2DPlusEffectProfileAsset>();
	TestEqual(TEXT("Fresh profile is stamped to the active library schema"),
		Fresh->EffectLibrarySchemaVersion,
		UPaper2DPlusEffectProfileAsset::CurrentEffectLibrarySchemaVersion);
	TestNull(TEXT("Fresh profile has no active Character ownership"), Fresh->CharacterProfile.Get());
	TestNull(TEXT("Fresh profile has no active Layer ownership"), Fresh->CharacterLayerAsset.Get());
	const FProperty* CharacterBridge = FindFProperty<FProperty>(
		UPaper2DPlusEffectProfileAsset::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusEffectProfileAsset, CharacterProfile));
	const FProperty* LayerBridge = FindFProperty<FProperty>(
		UPaper2DPlusEffectProfileAsset::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusEffectProfileAsset, CharacterLayerAsset));
	const FProperty* SpawnDefaultBridge = FindFProperty<FProperty>(
		FPaper2DPlusEffectProfileEntry::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, Offset));
	const FProperty* ActiveType = FindFProperty<FProperty>(
		FPaper2DPlusEffectProfileEntry::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, TypeTag));
	const FProperty* StoredEffectFlipbook = FindFProperty<FProperty>(
		FPaper2DPlusEffectProfileEntry::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, EffectFlipbook));
	const FProperty* LoadedEffectFlipbook = FindFProperty<FProperty>(
		FPaper2DPlusEffectProfileEntry::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, LoadedEffectFlipbook));
	const FProperty* EffectsProperty = FindFProperty<FProperty>(
		UPaper2DPlusEffectProfileAsset::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusEffectProfileAsset, Effects));
	const FProperty* ResolvedEffectFlipbook = FindFProperty<FProperty>(
		FPaper2DPlusEffectSpawnSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectSpawnSettings, EffectFlipbook));
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const FProperty* LegacyCompatibleEffectFlipbook = FindFProperty<FProperty>(
		FFlipbookEffectData::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FFlipbookEffectData, EffectFlipbook));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	TestTrue(TEXT("Deprecated Effect Profile relationships keep their hard Blueprint pin contracts"),
		CastField<FObjectPropertyBase>(CharacterBridge) != nullptr
			&& CharacterBridge->HasAnyPropertyFlags(CPF_BlueprintVisible)
			&& CharacterBridge->HasMetaData(TEXT("DeprecatedProperty"))
			&& !CharacterBridge->HasAnyPropertyFlags(CPF_Edit)
			&& CastField<FObjectPropertyBase>(LayerBridge) != nullptr
			&& LayerBridge->HasAnyPropertyFlags(CPF_BlueprintVisible)
			&& LayerBridge->HasMetaData(TEXT("DeprecatedProperty"))
			&& !LayerBridge->HasAnyPropertyFlags(CPF_Edit));
	TestTrue(TEXT("Profile spawn defaults survive only as deprecated non-editable bridges"),
		SpawnDefaultBridge && !SpawnDefaultBridge->HasAnyPropertyFlags(CPF_Edit));
	TestTrue(TEXT("Primary Type is active editable library metadata"),
		ActiveType && ActiveType->HasAnyPropertyFlags(CPF_Edit));
	TestTrue(TEXT("Effect Profile rows serialize flipbook identity as an internal soft reference"),
		CastField<FSoftObjectProperty>(StoredEffectFlipbook) != nullptr);
	TestTrue(TEXT("Effect Profile row soft references are not exposed to Blueprint callers"),
		StoredEffectFlipbook && !StoredEffectFlipbook->HasAnyPropertyFlags(CPF_BlueprintVisible));
	TestTrue(TEXT("Effect Profile row queries retain a loaded flipbook for the native Blueprint break"),
		CastField<FObjectPropertyBase>(LoadedEffectFlipbook) != nullptr
			&& LoadedEffectFlipbook->HasAnyPropertyFlags(CPF_Transient)
			&& !LoadedEffectFlipbook->HasAnyPropertyFlags(CPF_BlueprintVisible));
	TestTrue(TEXT("The legacy raw Effects array remains Blueprint-readable without exposing soft row pins"),
		EffectsProperty && EffectsProperty->HasAnyPropertyFlags(CPF_BlueprintVisible));
	TestEqual(TEXT("Effect Profile entries use the native hard-object break boundary"),
		FPaper2DPlusEffectProfileEntry::StaticStruct()->GetMetaData(TEXT("HasNativeBreak")),
		FString(TEXT("/Script/Paper2DPlus.Paper2DPlusBlueprintLibrary.BreakEffectProfileEntry")));
	TestTrue(TEXT("Resolved spawn settings keep a Blueprint-visible loaded flipbook object"),
		CastField<FObjectPropertyBase>(ResolvedEffectFlipbook) != nullptr
			&& ResolvedEffectFlipbook->HasAnyPropertyFlags(CPF_BlueprintVisible));
	TestTrue(TEXT("The retired effect struct keeps its hard Blueprint Set-Members pin"),
		CastField<FObjectPropertyBase>(LegacyCompatibleEffectFlipbook) != nullptr
			&& LegacyCompatibleEffectFlipbook->HasAnyPropertyFlags(CPF_BlueprintVisible));
	TestEqual(TEXT("Fresh library is empty"), Fresh->GetEffectFlipbooks().Num(), 0);
	TestEqual(TEXT("Fresh indexed library count is zero without loading"), Fresh->GetEffectEntryCount(), 0);
	TArray<FPaper2DPlusEffectProfileValidationIssue> FreshIssues;
	TestTrue(TEXT("An empty library is valid"), Fresh->ValidateEffectProfileAsset(FreshIssues));
	TestEqual(TEXT("An empty library reports no issues"), FreshIssues.Num(), 0);

	UPaper2DPlusEffectProfileAsset* Profile = NewObject<UPaper2DPlusEffectProfileAsset>();
	UPaperFlipbook* ProjectileFlipbook = NewObject<UPaperFlipbook>(Profile, TEXT("FB_VenomBolt"));
	UPaperFlipbook* ImpactFlipbook = NewObject<UPaperFlipbook>(Profile, TEXT("FB_FireImpact"));

	FPaper2DPlusEffectProfileEntry& Projectile = Profile->Effects.AddDefaulted_GetRef();
	Projectile.EffectFlipbook = ProjectileFlipbook;
	Projectile.DisplayLabel = FText::FromString(TEXT("Venom Bolt"));
	Projectile.TypeTag = Paper2DPlusEffectTags::Type_Projectile.GetTag();
	Projectile.DescriptorTags.AddTag(Paper2DPlusEffectTags::Descriptor_Poison.GetTag());
	Projectile.DescriptorTags.AddTag(Paper2DPlusEffectTags::Descriptor_Electric.GetTag());

	FPaper2DPlusEffectProfileEntry& Impact = Profile->Effects.AddDefaulted_GetRef();
	Impact.EffectFlipbook = ImpactFlipbook;
	Impact.TypeTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
	Impact.DescriptorTags.AddTag(Paper2DPlusEffectTags::Descriptor_Fire.GetTag());

	FPaper2DPlusEffectProfileEntry Duplicate = Projectile;
	Duplicate.DisplayLabel = FText::FromString(TEXT("Presentation-only duplicate"));
	Duplicate.TypeTag = Paper2DPlusEffectTags::Type_Impact.GetTag(); // conflicting metadata: first row wins
	Profile->Effects.Add(Duplicate);
	Profile->Effects.AddDefaulted(); // Null rows never leak into a query.
	TestEqual(TEXT("Indexed library count includes every authored row"), Profile->GetEffectEntryCount(), 4);

	FPaper2DPlusEffectProfileEntry ProjectedEntry;
	TestTrue(TEXT("Entry lookup resolves a valid authored row"),
		Profile->GetEffectEntryByIndex(0, ProjectedEntry));
	TestEqual(TEXT("Entry lookup exposes the loaded flipbook projection"),
		ProjectedEntry.LoadedEffectFlipbook.Get(), ProjectileFlipbook);
	TestEqual(TEXT("Entry lookup preserves the row's internal soft identity"),
		ProjectedEntry.EffectFlipbook.ToSoftObjectPath().ToString(),
		ProjectileFlipbook->GetPathName());
	TestEqual(TEXT("Indexed flipbook lookup returns a loaded object"),
		Profile->GetEffectFlipbookByIndex(1), ImpactFlipbook);
	TestNull(TEXT("Indexed flipbook lookup fails safely for an invalid index"),
		Profile->GetEffectFlipbookByIndex(INDEX_NONE));
	UPaperFlipbook* BrokenFlipbook = nullptr;
	FText BrokenDisplayLabel;
	FGameplayTag BrokenTypeTag;
	FGameplayTagContainer BrokenDescriptorTags;
	FGameplayTag BrokenLegacyCategory;
	FName BrokenEffectName;
	FGameplayTag BrokenCategoryTag;
	FVector2D BrokenOffset;
	float BrokenRotation = 0.0f;
	FVector2D BrokenScale;
	bool bBrokenFlipWithCharacter = false;
	FLinearColor BrokenTint;
	FName BrokenSocketName;
	FString BrokenLayerScope;
	UPaper2DPlusBlueprintLibrary::BreakEffectProfileEntry(
		Projectile,
		BrokenFlipbook,
		BrokenDisplayLabel,
		BrokenTypeTag,
		BrokenDescriptorTags,
		BrokenLegacyCategory,
		BrokenEffectName,
		BrokenCategoryTag,
		BrokenOffset,
		BrokenRotation,
		BrokenScale,
		bBrokenFlipWithCharacter,
		BrokenTint,
		BrokenSocketName,
		BrokenLayerScope);
	TestEqual(TEXT("The native compatibility break projects the old hard Flipbook pin"),
		BrokenFlipbook, ProjectileFlipbook);
	TestEqual(TEXT("The native compatibility break preserves active metadata pins"),
		BrokenTypeTag, Projectile.TypeTag);

	TArray<UPaperFlipbook*> Results = Profile->GetEffectFlipbooks();
	TestEqual(TEXT("Membership is ordered and deduplicated by flipbook path"), Results.Num(), 2);
	if (Results.Num() == 2)
	{
		TestEqual(TEXT("First authored identity remains first"), Results[0], ProjectileFlipbook);
		TestEqual(TEXT("Second authored identity remains second"), Results[1], ImpactFlipbook);
	}
	TestTrue(TEXT("Membership finds an included flipbook"), Profile->ContainsEffectFlipbook(ProjectileFlipbook));
	TestFalse(TEXT("Membership is null-safe"), Profile->ContainsEffectFlipbook(nullptr));

	Results = Profile->GetEffectFlipbooksByType(Paper2DPlusEffectTags::Type.GetTag(), false);
	TestEqual(TEXT("Hierarchical Type-root query includes strict children"), Results.Num(), 2);
	Results = Profile->GetEffectFlipbooksByType(Paper2DPlusEffectTags::Type.GetTag(), true);
	TestEqual(TEXT("Exact Type-root query excludes child types"), Results.Num(), 0);
	Results = Profile->GetEffectFlipbooksByType(Paper2DPlusEffectTags::Type_Projectile.GetTag(), true);
	TestEqual(TEXT("Exact primary Type query deduplicates"), Results.Num(), 1);
	Results = Profile->GetEffectFlipbooksByType(Paper2DPlusEffectTags::Type_Impact.GetTag(), true);
	TestEqual(TEXT("A later duplicate cannot override the first row's Type metadata"), Results.Num(), 1);
	if (Results.Num() == 1)
	{
		TestEqual(TEXT("First-row metadata keeps the duplicate projectile out of Impact results"),
			Results[0], ImpactFlipbook);
	}

	Results = Profile->GetEffectFlipbooksByDescriptor(Paper2DPlusEffectTags::Descriptor.GetTag(), false);
	TestEqual(TEXT("Hierarchical Descriptor-root query includes children"), Results.Num(), 2);
	Results = Profile->GetEffectFlipbooksByDescriptor(Paper2DPlusEffectTags::Descriptor.GetTag(), true);
	TestEqual(TEXT("Exact Descriptor-root query excludes children"), Results.Num(), 0);

	FGameplayTagContainer AllDescriptors;
	AllDescriptors.AddTag(Paper2DPlusEffectTags::Descriptor_Poison.GetTag());
	AllDescriptors.AddTag(Paper2DPlusEffectTags::Descriptor_Electric.GetTag());
	Results = Profile->GetEffectFlipbooksWithAllDescriptors(AllDescriptors, true);
	TestEqual(TEXT("All-descriptor query requires every requested descriptor"), Results.Num(), 1);

	FGameplayTagContainer AnyDescriptors;
	AnyDescriptors.AddTag(Paper2DPlusEffectTags::Descriptor_Poison.GetTag());
	AnyDescriptors.AddTag(Paper2DPlusEffectTags::Descriptor_Fire.GetTag());
	Results = Profile->GetEffectFlipbooksWithAnyDescriptors(AnyDescriptors, true);
	TestEqual(TEXT("Any-descriptor query returns both matching identities"), Results.Num(), 2);
	TestEqual(TEXT("Empty descriptor input is not an accidental match-all"),
		Profile->GetEffectFlipbooksWithAnyDescriptors(FGameplayTagContainer(), false).Num(), 0);

	Results.Add(ProjectileFlipbook);
	UPaper2DPlusBlueprintLibrary::GetEffectProfileFlipbooks(nullptr, Results);
	TestEqual(TEXT("Null Blueprint library query clears output"), Results.Num(), 0);
	TestFalse(TEXT("Null Blueprint membership fails safely"),
		UPaper2DPlusBlueprintLibrary::EffectProfileContainsFlipbook(nullptr, ProjectileFlipbook));
	UPaper2DPlusBlueprintLibrary::GetEffectProfileFlipbooksByType(
		Profile, Paper2DPlusEffectTags::Type_Projectile.GetTag(), true, Results);
	TestEqual(TEXT("Blueprint Type wrapper exposes the direct library"), Results.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectProfileMigrationValidationTest,
	"Paper2DPlus.EffectProfile.Library.MigrationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectProfileMigrationValidationTest::RunTest(const FString& Parameters)
{
	const FGameplayTag OffTaxonomy = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Paper2DPlus.Animation.Combat")), false);
	TestTrue(TEXT("Off-taxonomy fixture tag is registered"), OffTaxonomy.IsValid());

	UPaper2DPlusEffectProfileAsset* Legacy = NewObject<UPaper2DPlusEffectProfileAsset>();
	Legacy->EffectLibrarySchemaVersion = 0;
	FPaper2DPlusEffectProfileEntry& TypeRow = Legacy->Effects.AddDefaulted_GetRef();
	TypeRow.EffectName = TEXT("Bolt");
	TypeRow.CategoryTag = Paper2DPlusEffectTags::Type_Projectile.GetTag();
	TypeRow.EffectFlipbook = NewObject<UPaperFlipbook>(Legacy, TEXT("FB_Bolt"));
	FPaper2DPlusEffectProfileEntry& DescriptorRow = Legacy->Effects.AddDefaulted_GetRef();
	DescriptorRow.EffectName = TEXT("PoisonCloud");
	DescriptorRow.CategoryTag = Paper2DPlusEffectTags::Descriptor_Poison.GetTag();
	DescriptorRow.EffectFlipbook = NewObject<UPaperFlipbook>(Legacy, TEXT("FB_PoisonCloud"));
	FPaper2DPlusEffectProfileEntry& RemapRow = Legacy->Effects.AddDefaulted_GetRef();
	RemapRow.EffectName = TEXT("OldCombatFx");
	RemapRow.CategoryTag = OffTaxonomy;
	RemapRow.EffectFlipbook = NewObject<UPaperFlipbook>(Legacy, TEXT("FB_OldCombatFx"));

	Legacy->PostLoad();
	TestEqual(TEXT("Effect Profile PostLoad stamps the migrated library schema"),
		Legacy->EffectLibrarySchemaVersion,
		UPaper2DPlusEffectProfileAsset::CurrentEffectLibrarySchemaVersion);
	TestEqual(TEXT("Legacy effect name becomes an optional display label"),
		Legacy->Effects[0].DisplayLabel.ToString(), FString(TEXT("Bolt")));
	TestEqual(TEXT("Compatible legacy Type category migrates"),
		Legacy->Effects[0].TypeTag, Paper2DPlusEffectTags::Type_Projectile.GetTag());
	TestTrue(TEXT("Compatible legacy Descriptor category migrates"),
		Legacy->Effects[1].DescriptorTags.HasTagExact(Paper2DPlusEffectTags::Descriptor_Poison.GetTag()));
	TestEqual(TEXT("Off-taxonomy category remains visible for explicit remap"),
		Legacy->Effects[2].LegacyCategoryAwaitingRemap, OffTaxonomy);
	TestEqual(TEXT("Deprecated category source is retained"), Legacy->Effects[2].CategoryTag, OffTaxonomy);
	TestEqual(TEXT("Second profile migration pass is stable"), Legacy->MigrateLegacyLibrarySchema(), 0);

	UPaper2DPlusEffectProfileAsset* Invalid = NewObject<UPaper2DPlusEffectProfileAsset>();
	Invalid->Effects.AddDefaulted();
	UPaperFlipbook* SharedFlipbook = NewObject<UPaperFlipbook>(Invalid, TEXT("FB_Shared"));
	FPaper2DPlusEffectProfileEntry& MissingType = Invalid->Effects.AddDefaulted_GetRef();
	MissingType.EffectFlipbook = SharedFlipbook;
	FPaper2DPlusEffectProfileEntry& BrokenDuplicate = Invalid->Effects.AddDefaulted_GetRef();
	BrokenDuplicate.EffectFlipbook = SharedFlipbook;
	BrokenDuplicate.TypeTag = OffTaxonomy;
	BrokenDuplicate.DescriptorTags.AddTag(Paper2DPlusEffectTags::Type_Impact.GetTag());
	BrokenDuplicate.LegacyCategoryAwaitingRemap = OffTaxonomy;
	const FSoftObjectPath MissingFlipbookPath(
		TEXT("/Game/__Paper2DPlusAutomationMissing__/FB_MissingEffect.FB_MissingEffect"));
	FPaper2DPlusEffectProfileEntry& MissingAsset = Invalid->Effects.AddDefaulted_GetRef();
	MissingAsset.EffectFlipbook = TSoftObjectPtr<UPaperFlipbook>(MissingFlipbookPath);
	MissingAsset.TypeTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
	UObject* WrongClassObject = NewObject<UPaper2DPlusEffectProfileAsset>(
		Invalid, TEXT("NotAFlipbook"));
	const FSoftObjectPath WrongClassPath(WrongClassObject);
	FPaper2DPlusEffectProfileEntry& WrongClassAsset = Invalid->Effects.AddDefaulted_GetRef();
	WrongClassAsset.EffectFlipbook = TSoftObjectPtr<UPaperFlipbook>(WrongClassPath);
	WrongClassAsset.TypeTag = Paper2DPlusEffectTags::Type_Impact.GetTag();
	TestEqual(TEXT("A dangling soft path is rejected without loading"),
		UPaper2DPlusEffectProfileAsset::InspectEffectFlipbookPathNoLoad(MissingFlipbookPath),
		EPaper2DPlusEffectFlipbookPathStatus::Missing);
	TestEqual(TEXT("A resident wrong-class soft path is rejected without loading"),
		UPaper2DPlusEffectProfileAsset::InspectEffectFlipbookPathNoLoad(WrongClassPath),
		EPaper2DPlusEffectFlipbookPathStatus::WrongClass);

	TArray<FPaper2DPlusEffectProfileValidationIssue> Issues;
	TestFalse(TEXT("Null and duplicate flipbook rows are invalid"), Invalid->ValidateEffectProfileAsset(Issues));
	auto HasField = [&Issues](FName Field, EPaper2DPlusEffectProfileValidationSeverity Severity)
	{
		return Issues.ContainsByPredicate([Field, Severity](const FPaper2DPlusEffectProfileValidationIssue& Issue)
		{
			return Issue.Field == Field && Issue.Severity == Severity;
		});
	};
	TestTrue(TEXT("Null/duplicate flipbooks report deterministic Errors"),
		HasField(TEXT("EffectFlipbook"), EPaper2DPlusEffectProfileValidationSeverity::Error));
	TestTrue(TEXT("Missing/off-root primary Types report Warnings"),
		HasField(TEXT("TypeTag"), EPaper2DPlusEffectProfileValidationSeverity::Warning));
	TestTrue(TEXT("Off-root descriptors report Warnings"),
		HasField(TEXT("DescriptorTags"), EPaper2DPlusEffectProfileValidationSeverity::Warning));
	TestTrue(TEXT("Retained legacy categories report remap Warnings"),
		HasField(TEXT("LegacyCategoryAwaitingRemap"), EPaper2DPlusEffectProfileValidationSeverity::Warning));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectProfileDeprecatedResolverTest,
	"Paper2DPlus.EffectProfile.Library.DeprecatedResolversRemainCallable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectProfileDeprecatedResolverTest::RunTest(const FString& Parameters)
{
	// These Blueprint entry points are deprecated but still shipped, so a project that calls them keeps
	// compiling and keeps getting the same answers. They used to be exercised at the tail of the Spawn
	// Effect Cue migration tests; those cue classes are gone and this coverage is not, so it is pinned
	// here directly against the Effect Profile asset it actually belongs to.
	UPaper2DPlusEffectProfileAsset* Profile = MakeLegacyResolverProfile();

	FPaper2DPlusEffectSpawnSettings OldSettings;
	TestTrue(TEXT("Deprecated name resolver remains callable"),
		UPaper2DPlusBlueprintLibrary::ResolveEffectProfileEntry(Profile, TEXT("slashspark"), OldSettings));
	TestTrue(TEXT("Deprecated name resolver retains case-insensitive behavior"),
		OldSettings.EffectFlipbook == Profile->Effects[0].EffectFlipbook.Get());
	TestTrue(TEXT("Deprecated name resolver still projects the row's spawn defaults"),
		OldSettings.Offset.Equals(FVector2D(18.0, -4.0))
			&& FMath::IsNearlyEqual(OldSettings.Rotation, 12.0f)
			&& OldSettings.Scale.Equals(FVector2D(1.5, 0.75))
			&& OldSettings.bFlipWithCharacter == false);
	TestFalse(TEXT("Deprecated name resolver fails safely on an unknown name"),
		UPaper2DPlusBlueprintLibrary::ResolveEffectProfileEntry(
			Profile, TEXT("DoesNotExist"), OldSettings));
	TestFalse(TEXT("Deprecated name resolver fails safely on a null profile"),
		UPaper2DPlusBlueprintLibrary::ResolveEffectProfileEntry(
			nullptr, TEXT("slashspark"), OldSettings));

	TArray<FPaper2DPlusEffectProfileEntry> OldEntries;
	UPaper2DPlusBlueprintLibrary::GetEffectProfileEntriesForCategory(
		Profile, Paper2DPlusEffectTags::Type.GetTag(), OldEntries);
	TestEqual(TEXT("Deprecated hierarchical category wrapper remains callable"), OldEntries.Num(), 1);

	TArray<UPaperFlipbook*> NewResults;
	UPaper2DPlusBlueprintLibrary::GetEffectProfileFlipbooksByType(
		Profile, Paper2DPlusEffectTags::Type_Impact.GetTag(), true, NewResults);
	TestEqual(TEXT("New Type wrapper reflects the tagged library"), NewResults.Num(), 1);
	return true;
}

#endif // WITH_EDITOR
