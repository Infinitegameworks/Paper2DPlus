// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Runtime/Launch/Resources/Version.h"

#include "EffectProfileContextResolver.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "Paper2DPlusEffectLibraryIndex.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "PaperFlipbook.h"

namespace EffectContextTestPrivate
{
	UPaper2DPlusEffectProfileAsset* MakeLibrary(UObject* Outer, const TCHAR* Name, UPaperFlipbook*& OutFirst)
	{
		UPaper2DPlusEffectProfileAsset* Library = NewObject<UPaper2DPlusEffectProfileAsset>(Outer, FName(Name));
		OutFirst = NewObject<UPaperFlipbook>(Library, FName(*FString::Printf(TEXT("%s_Flipbook"), Name)));
		FPaper2DPlusEffectProfileEntry& Entry = Library->Effects.AddDefaulted_GetRef();
		Entry.EffectFlipbook = OutFirst;
		// Duplicate membership must not duplicate the exact allowlist.
		const FPaper2DPlusEffectProfileEntry DuplicateEntry = Entry;
		Library->Effects.Add(DuplicateEntry);
		return Library;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectContextIsolationTest,
	"Paper2DPlus.EffectProfile.Context.CatalogCharacterIsolationUniquePaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectContextIsolationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	UPaper2DPlusCharacterProfileAsset* Knight = NewObject<UPaper2DPlusCharacterProfileAsset>(Catalog, TEXT("Knight"));
	UPaper2DPlusCharacterProfileAsset* Mage = NewObject<UPaper2DPlusCharacterProfileAsset>(Catalog, TEXT("Mage"));
	UPaperFlipbook* KnightFlipbook = nullptr;
	UPaperFlipbook* MageFlipbook = nullptr;
	UPaper2DPlusEffectProfileAsset* KnightFx = EffectContextTestPrivate::MakeLibrary(Catalog, TEXT("KnightFx"), KnightFlipbook);
	UPaper2DPlusEffectProfileAsset* MageFx = EffectContextTestPrivate::MakeLibrary(Catalog, TEXT("MageFx"), MageFlipbook);
	FPaper2DPlusCharacterCatalogEntry KnightRow;
	KnightRow.CharacterProfile = Knight;
	KnightRow.EffectProfile = KnightFx;
	Catalog->Entries.Add(KnightRow);
	FPaper2DPlusCharacterCatalogEntry MageRow;
	MageRow.CharacterProfile = Mage;
	MageRow.EffectProfile = MageFx;
	Catalog->Entries.Add(MageRow);

	const FPaper2DPlusEffectProfileContext KnightContext =
		FEffectProfileContextResolver::ResolveLoadedContext(Catalog, Knight, true);
	const FPaper2DPlusEffectProfileContext MageContext =
		FEffectProfileContextResolver::ResolveLoadedContext(Catalog, Mage, true);
	TestTrue(TEXT("Knight context is scoped and ready"), KnightContext.bChoicesScoped && KnightContext.IsReady());
	TestEqual(TEXT("Duplicate library rows collapse to one path"), KnightContext.AllowedFlipbooks.Num(), 1);
	TestTrue(TEXT("Knight's library choice is allowed"), KnightContext.IsAllowed(FSoftObjectPath(KnightFlipbook)));
	TestFalse(TEXT("Mage's choice cannot leak into Knight's editor"), KnightContext.IsAllowed(FSoftObjectPath(MageFlipbook)));
	TestTrue(TEXT("Mage editor resolves its own exact library"), MageContext.IsAllowed(FSoftObjectPath(MageFlipbook)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectContextRetargetTest,
	"Paper2DPlus.EffectProfile.Context.LiveCharacterRetargetRebuildsExactAllowlist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectContextRetargetTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	UPaper2DPlusCharacterProfileAsset* CharacterA =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Catalog, TEXT("CharacterA"));
	UPaper2DPlusCharacterProfileAsset* CharacterB =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Catalog, TEXT("CharacterB"));
	UPaperFlipbook* EffectA = nullptr;
	UPaperFlipbook* EffectB = nullptr;
	UPaper2DPlusEffectProfileAsset* LibraryA =
		EffectContextTestPrivate::MakeLibrary(Catalog, TEXT("LibraryA"), EffectA);
	UPaper2DPlusEffectProfileAsset* LibraryB =
		EffectContextTestPrivate::MakeLibrary(Catalog, TEXT("LibraryB"), EffectB);
	FPaper2DPlusCharacterCatalogEntry& RowA = Catalog->Entries.AddDefaulted_GetRef();
	RowA.CharacterProfile = CharacterA;
	RowA.EffectProfile = LibraryA;
	FPaper2DPlusCharacterCatalogEntry& RowB = Catalog->Entries.AddDefaulted_GetRef();
	RowB.CharacterProfile = CharacterB;
	RowB.EffectProfile = LibraryB;

	UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
	const TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> PreviousCatalog =
		Settings->DefaultCharacterCatalog;
	Settings->DefaultCharacterCatalog = Catalog;
	TSharedRef<FPaper2DPlusEffectLibraryIndex> Index =
		MakeShared<FPaper2DPlusEffectLibraryIndex>(false);
	TSharedRef<FEffectProfileContextResolver> Resolver =
		MakeShared<FEffectProfileContextResolver>(CharacterA, Index);
	const FPaper2DPlusEffectProfileContext& ContextA = Resolver->ResolveForPicker();
	TestTrue(TEXT("Initial Character resolves its assigned Effect library"),
		ContextA.IsAllowed(FSoftObjectPath(EffectA)));
	TestFalse(TEXT("Initial Character cannot see the other Character's Effect"),
		ContextA.IsAllowed(FSoftObjectPath(EffectB)));
	const uint32 BeforeRetarget = Resolver->GetRevision();

	Resolver->SetCharacter(CharacterB);
	TestEqual(TEXT("A live Base-Profile swap invalidates exactly once"),
		Resolver->GetRevision(), BeforeRetarget + 1);
	TestFalse(TEXT("Retarget stays load-free until the picker asks"),
		Resolver->GetCachedContext().IsResolved());
	const FPaper2DPlusEffectProfileContext& ContextB = Resolver->ResolveForPicker();
	TestTrue(TEXT("Retargeted Character resolves its own exact Effect library"),
		ContextB.IsAllowed(FSoftObjectPath(EffectB)));
	TestFalse(TEXT("Old Character allowlist cannot survive retarget"),
		ContextB.IsAllowed(FSoftObjectPath(EffectA)));

	Settings->DefaultCharacterCatalog = PreviousCatalog;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectContextFailureModesTest,
	"Paper2DPlus.EffectProfile.Context.UnscopedAndScopedFailureModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectContextFailureModesTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Character = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Arbitrary = NewObject<UPaperFlipbook>();
	const FPaper2DPlusEffectProfileContext NoCatalog =
		FEffectProfileContextResolver::ResolveLoadedContext(nullptr, Character, false);
	TestEqual(TEXT("No default Catalog is explicitly unscoped"),
		NoCatalog.Status, EPaper2DPlusEffectContextStatus::UnscopedNoDefaultCatalog);
	TestTrue(TEXT("Missing Catalog keeps Character Effects explicitly scoped"), NoCatalog.bChoicesScoped);
	TestFalse(TEXT("Missing Catalog never broadens silently to arbitrary Flipbooks"),
		NoCatalog.IsAllowed(FSoftObjectPath(Arbitrary)));
	TestTrue(TEXT("Guidance exposes the explicit All Project escape hatch"),
		NoCatalog.Guidance.ToString().Contains(TEXT("All Project"), ESearchCase::IgnoreCase));

	const FPaper2DPlusEffectProfileContext Unreadable =
		FEffectProfileContextResolver::ResolveLoadedContext(nullptr, Character, true, FSoftObjectPath(TEXT("/Game/Missing.Catalog")), true);
	TestTrue(TEXT("Unreadable configured Catalog scopes to an empty result"), Unreadable.bChoicesScoped);
	TestFalse(TEXT("Unreadable configured Catalog permits no new choices"), Unreadable.IsAllowed(FSoftObjectPath(Arbitrary)));

	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	const FPaper2DPlusEffectProfileContext MissingRow =
		FEffectProfileContextResolver::ResolveLoadedContext(Catalog, Character, true);
	TestEqual(TEXT("Missing character row is actionable"),
		MissingRow.Status, EPaper2DPlusEffectContextStatus::ScopedCharacterMissing);
	FPaper2DPlusCharacterCatalogEntry Row;
	Row.CharacterProfile = Character;
	Catalog->Entries.Add(Row);
	const FPaper2DPlusEffectProfileContext MissingAssignment =
		FEffectProfileContextResolver::ResolveLoadedContext(Catalog, Character, true);
	TestEqual(TEXT("Missing Effect assignment is distinct"),
		MissingAssignment.Status, EPaper2DPlusEffectContextStatus::ScopedEffectUnassigned);
	TestFalse(TEXT("Every failure state has plain-language guidance"), MissingAssignment.Guidance.IsEmpty());
	// Rehomed from the retired Spawn Effect picker test. Every Effect picker filters candidates with
	// exactly this rule (bChoicesScoped && !IsAllowed), so the rule is asserted on the context itself
	// rather than on any one picker widget.
	TestTrue(TEXT("A configured Catalog with no Effect assignment keeps choices scoped"),
		MissingAssignment.bChoicesScoped);
	TestFalse(TEXT("Configured Catalog with missing assignment exposes an empty scoped picker"),
		MissingAssignment.IsAllowed(FSoftObjectPath(Arbitrary)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectContextIndexInvalidationTest,
	"Paper2DPlus.EffectProfile.Context.ReactsThroughSoleEffectIndexSignal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectContextIndexInvalidationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	UPaper2DPlusCharacterProfileAsset* Character =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Catalog, TEXT("Character"));
	UPaperFlipbook* Flipbook = nullptr;
	UPaper2DPlusEffectProfileAsset* Library =
		EffectContextTestPrivate::MakeLibrary(Catalog, TEXT("LiveLibrary"), Flipbook);
	FPaper2DPlusCharacterCatalogEntry& Row = Catalog->Entries.AddDefaulted_GetRef();
	Row.CharacterProfile = Character;
	Row.EffectProfile = Library;

	UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
	const TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> PreviousCatalog =
		Settings->DefaultCharacterCatalog;
	Settings->DefaultCharacterCatalog = Catalog;
	TSharedRef<FPaper2DPlusEffectLibraryIndex> Index =
		MakeShared<FPaper2DPlusEffectLibraryIndex>(false);
	TSharedPtr<FEffectProfileContextResolver> Resolver =
		MakeShared<FEffectProfileContextResolver>(Character, Index);
	TestTrue(TEXT("Fixture resolves before mutation"), Resolver->ResolveForPicker().IsReady());
	const uint32 Before = Resolver->GetRevision();
	Index->PublishChange(
		EPaper2DPlusEffectLibraryChangeDomain::Catalog,
		FSoftObjectPath(Catalog));
	TestTrue(TEXT("Fixture flushes the coalesced index event"),
		Index->FlushPendingInvalidationForTests());
	TestEqual(TEXT("Relevant Catalog commit invalidates the per-editor resolver once"),
		Resolver->GetRevision(), Before + 1);
	TestFalse(TEXT("Invalidation remains load-free until picker refresh"),
		Resolver->GetCachedContext().IsResolved());

	Resolver.Reset();
	Settings->DefaultCharacterCatalog = PreviousCatalog;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectContextDirectCueTest,
	"Paper2DPlus.EffectProfile.Context.OutOfLibraryDirectCueRemainsAuthoritative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectContextDirectCueTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	UPaper2DPlusCharacterProfileAsset* Character = NewObject<UPaper2DPlusCharacterProfileAsset>(Catalog);
	UPaperFlipbook* InLibrary = nullptr;
	UPaper2DPlusEffectProfileAsset* Library = EffectContextTestPrivate::MakeLibrary(Catalog, TEXT("Fx"), InLibrary);
	FPaper2DPlusCharacterCatalogEntry Row;
	Row.CharacterProfile = Character;
	Row.EffectProfile = Library;
	Catalog->Entries.Add(Row);
	UPaperFlipbook* ExistingExternal = NewObject<UPaperFlipbook>(Character);
	UPaper2DPlusEditorTestMomentCue* Cue =
		NewObject<UPaper2DPlusEditorTestMomentCue>(Character, NAME_None, RF_Transactional);
	Cue->EffectArt = ExistingExternal;
	const FPaper2DPlusEffectProfileContext Context =
		FEffectProfileContextResolver::ResolveLoadedContext(Catalog, Character, true);
	TestTrue(TEXT("Existing direct value is identified as outside the current library"),
		Context.IsOutOfLibrary(FSoftObjectPath(ExistingExternal)));
	TestTrue(TEXT("Library membership changes never rewrite the direct Cue value"), Cue->EffectArt == ExistingExternal);
	Library->Effects.Reset();
	TestTrue(TEXT("Removing library membership still does not rewrite the Cue"), Cue->EffectArt == ExistingExternal);

	TSharedRef<FEffectProfileContextResolver> Resolver = MakeShared<FEffectProfileContextResolver>(Character);
	const uint32 Before = Resolver->GetRevision();
	Resolver->Invalidate();
	TestEqual(TEXT("Invalidation is observable and remains load-free"), Resolver->GetRevision(), Before + 1);
	TestFalse(TEXT("Invalidation leaves the context unresolved until picker interaction"),
		Resolver->GetCachedContext().IsResolved());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
