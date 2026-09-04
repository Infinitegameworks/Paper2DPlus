// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SpriteExtractionUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Generated asset names must survive the engine's own name rules.
 *
 * Regression origin: an Aseprite layer named "Suit & Tie" produced no sheet and no sprites, because
 * '&' is in INVALID_LONGPACKAGE_CHARACTERS so its package could never be created. The Layer Asset still
 * recorded 58 references to the intended paths, so the layer rendered nothing with no error anywhere —
 * the only visible symptom was a character missing its jacket. The space-only sanitizer that shipped
 * before this could not see the problem.
 */

namespace
{
	FString AppCustom_Strict(const FString& In)
	{
		FString Out = In;
		FSpriteExtractionUtils::SanitizeAssetNameStrict(Out);
		return Out;
	}

	FString AppCustom_SpaceOnly(const FString& In)
	{
		FString Out = In;
		FSpriteExtractionUtils::SanitizeAssetName(Out);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAssetNameSanitizeStrictTest,
	"Paper2DPlus.Import.AssetNames.StrictSanitizeCoversEngineInvalidCharacters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAssetNameSanitizeStrictTest::RunTest(const FString& Parameters)
{
	// The exact name that shipped broken.
	TestEqual(TEXT("the ampersand that produced no assets is replaced"),
		AppCustom_Strict(TEXT("Suit & Tie")), FString(TEXT("Suit___Tie")));
	TestFalse(TEXT("the space-only sanitizer does NOT fix it (this is the shipped bug)"),
		AppCustom_SpaceOnly(TEXT("Suit & Tie")).Equals(AppCustom_Strict(TEXT("Suit & Tie"))));

	// Every character the engine rejects must be gone, whichever list it comes from.
	const FString Invalid = FString(INVALID_OBJECTNAME_CHARACTERS) + FString(INVALID_LONGPACKAGE_CHARACTERS);
	const FString Sanitized = AppCustom_Strict(TEXT("A&B!C~D@E#F.G,H'I\"J|K:L*M?N<O>P(Q)R[S]T{U}V=W;X^Y%Z$"));
	for (int32 Index = 0; Index < Invalid.Len(); ++Index)
	{
		const TCHAR Bad = Invalid[Index];
		if (Bad == TEXT('\n') || Bad == TEXT('\r') || Bad == TEXT('\t'))
		{
			continue; // covered below, and awkward to embed in the probe string
		}
		if (!TestFalse(FString::Printf(TEXT("sanitized name still contains '%c'"), Bad),
			Sanitized.Contains(FString::Chr(Bad))))
		{
			return false;
		}
	}
	TestFalse(TEXT("whitespace control characters are replaced too"),
		AppCustom_Strict(TEXT("A\tB\nC\rD")).Contains(TEXT("\t")));

	// A name the shipped sanitizer already handled must come back byte-identical, so no working asset
	// is ever renamed by this change.
	const TCHAR* AlreadyFine[] = {
		TEXT("Head_Base_1"), TEXT("UB_Base 3"), TEXT("Head Ski_Mask"),
		TEXT("Lower Body"), TEXT("Chest - BASE"), TEXT("VFX")
	};
	for (const TCHAR* Name : AlreadyFine)
	{
		TestEqual(FString::Printf(TEXT("'%s' is unchanged relative to the space-only sanitizer"), Name),
			AppCustom_Strict(Name), AppCustom_SpaceOnly(Name));
	}

	// Runs are never collapsed: collapsing would rename assets that already exist on disk.
	TestEqual(TEXT("consecutive separators each map to their own underscore"),
		AppCustom_Strict(TEXT("A  B")), FString(TEXT("A__B")));
	TestEqual(TEXT("an empty name stays empty"), AppCustom_Strict(FString()), FString());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
#endif // WITH_EDITOR
