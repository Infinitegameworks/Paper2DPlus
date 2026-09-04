// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "GameplayTagContainer.h"

/** TASK-108 U4 — the per-animation category-tag DATA layer: the native `Paper2DPlus.Animation`
 *  taxonomy (Combat/Context dimension subtrees, Paper2DPlusAnimationTags.h) and the
 *  `FFlipbookEditorMetadata::AnimationTags` container (purely descriptive, never drives playback).
 *  Worldless tests pin:
 *   - native registration: every declared tag resolves via FGameplayTag::RequestGameplayTag headless;
 *   - JSON round-trip: the container rides the asset's reflected Flipbooks payload
 *     (ExportToJsonString → ImportFromJsonString preserves the tags);
 *   - hierarchy sanity: Combat.Heavy MatchesTag Combat (specialization within one dimension) and
 *     does NOT match the sibling Context dimension — the dimension rule's structural premise.
 *  Helpers are AnimTags_-prefixed per the unity-build file-unique-name rule. Tag params/locals are
 *  named InTag (C4458 shadow rule). */

namespace
{
	/** Add a minimal flipbook entry named MoveName (no live flipbook needed — tag data only). */
	FFlipbookProfileEntry& AnimTags_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		const int32 Index = Asset->Flipbooks.Add(Anim);
		return Asset->Flipbooks[Index];
	}

	/** Resolve a tag by full name WITHOUT erroring when missing (missing = test failure, not log spam). */
	FGameplayTag AnimTags_Request(const TCHAR* InTagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(InTagName), /*ErrorIfNotFound=*/false);
	}
}

// ---------------------------------------------------------------------------
// Native registration — every taxonomy tag resolves headless via the tag manager.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationTagsRegistration,
	"Paper2DPlus.AnimationTags.NativeRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationTagsRegistration::RunTest(const FString& Parameters)
{
	struct FExpectedTag
	{
		const TCHAR* Name;
		FGameplayTag Accessor; // by value — the native accessor converts from FNativeGameplayTag
	};

	const FExpectedTag Expected[] = {
		{ TEXT("Paper2DPlus.Animation"), Paper2DPlusAnimationTags::Animation },
		{ TEXT("Paper2DPlus.Animation.Combat"), Paper2DPlusAnimationTags::Combat },
		{ TEXT("Paper2DPlus.Animation.Combat.Combo"), Paper2DPlusAnimationTags::Combat_Combo },
		{ TEXT("Paper2DPlus.Animation.Combat.Heavy"), Paper2DPlusAnimationTags::Combat_Heavy },
		{ TEXT("Paper2DPlus.Animation.Combat.Light"), Paper2DPlusAnimationTags::Combat_Light },
		{ TEXT("Paper2DPlus.Animation.Combat.Block"), Paper2DPlusAnimationTags::Combat_Block },
		{ TEXT("Paper2DPlus.Animation.Combat.Grab"), Paper2DPlusAnimationTags::Combat_Grab },
		{ TEXT("Paper2DPlus.Animation.Context"), Paper2DPlusAnimationTags::Context },
		{ TEXT("Paper2DPlus.Animation.Context.Airborne"), Paper2DPlusAnimationTags::Context_Airborne },
		{ TEXT("Paper2DPlus.Animation.Context.Crouching"), Paper2DPlusAnimationTags::Context_Crouching },
		{ TEXT("Paper2DPlus.Animation.Context.Swimming"), Paper2DPlusAnimationTags::Context_Swimming },
	};

	for (const FExpectedTag& Row : Expected)
	{
		const FGameplayTag InTag = AnimTags_Request(Row.Name);
		TestTrue(FString::Printf(TEXT("'%s' resolves via RequestGameplayTag"), Row.Name), InTag.IsValid());
		TestTrue(FString::Printf(TEXT("'%s' accessor is valid"), Row.Name), Row.Accessor.IsValid());
		TestTrue(FString::Printf(TEXT("'%s' accessor matches the requested tag"), Row.Name), InTag == Row.Accessor);
	}

	return true;
}

// ---------------------------------------------------------------------------
// JSON round-trip — AnimationTags survives ExportToJsonString → ImportFromJsonString.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationTagsJsonRoundTrip,
	"Paper2DPlus.AnimationTags.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationTagsJsonRoundTrip::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Src = NewObject<UPaper2DPlusCharacterProfileAsset>();

	// A cross-dimension combination (the container IS the combination — the dimension rule).
	FFlipbookProfileEntry& Heavy = AnimTags_AddMove(Src, TEXT("AirHeavy"));
	Heavy.EditorMeta.AnimationTags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy);
	Heavy.EditorMeta.AnimationTags.AddTag(Paper2DPlusAnimationTags::Context_Airborne);

	// An untagged sibling (empty container must stay empty — additive default).
	AnimTags_AddMove(Src, TEXT("Idle"));

	FString Json;
	TestTrue(TEXT("Export to JSON succeeds"), Src->ExportToJsonString(Json));

	UPaper2DPlusCharacterProfileAsset* Dst = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestTrue(TEXT("Import from JSON succeeds"), Dst->ImportFromJsonString(Json));

	const FFlipbookProfileEntry* DstHeavy = Dst->Flipbooks.FindByPredicate(
		[](const FFlipbookProfileEntry& E) { return E.Identity.FlipbookName == TEXT("AirHeavy"); });
	const FFlipbookProfileEntry* DstIdle = Dst->Flipbooks.FindByPredicate(
		[](const FFlipbookProfileEntry& E) { return E.Identity.FlipbookName == TEXT("Idle"); });

	TestTrue(TEXT("AirHeavy entry survives the round-trip"), DstHeavy != nullptr);
	TestTrue(TEXT("Idle entry survives the round-trip"), DstIdle != nullptr);
	if (!DstHeavy || !DstIdle)
	{
		return false;
	}

	TestEqual(TEXT("AirHeavy carries exactly two tags after import"), DstHeavy->EditorMeta.AnimationTags.Num(), 2);
	TestTrue(TEXT("Combat.Heavy preserved exactly"),
		DstHeavy->EditorMeta.AnimationTags.HasTagExact(Paper2DPlusAnimationTags::Combat_Heavy));
	TestTrue(TEXT("Context.Airborne preserved exactly"),
		DstHeavy->EditorMeta.AnimationTags.HasTagExact(Paper2DPlusAnimationTags::Context_Airborne));
	TestTrue(TEXT("Untagged entry imports with an empty container"), DstIdle->EditorMeta.AnimationTags.IsEmpty());

	return true;
}

// ---------------------------------------------------------------------------
// Hierarchy sanity — specialization matches its own dimension, never a sibling dimension.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationTagsHierarchy,
	"Paper2DPlus.AnimationTags.HierarchySanity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationTagsHierarchy::RunTest(const FString& Parameters)
{
	const FGameplayTag InTagHeavy = Paper2DPlusAnimationTags::Combat_Heavy;

	// Within-dimension specialization: Combat.Heavy IS a Combat and IS an Animation.
	TestTrue(TEXT("Combat.Heavy matches Combat hierarchically"), InTagHeavy.MatchesTag(Paper2DPlusAnimationTags::Combat));
	TestTrue(TEXT("Combat.Heavy matches the Animation root hierarchically"), InTagHeavy.MatchesTag(Paper2DPlusAnimationTags::Animation));
	TestFalse(TEXT("Combat.Heavy does not exact-match Combat"), InTagHeavy.MatchesTagExact(Paper2DPlusAnimationTags::Combat));

	// Cross-dimension: Combat.Heavy is NOT a Context — dimensions are sibling subtrees.
	// (FNativeGameplayTag accessors convert to FGameplayTag; bind first for member calls.)
	const FGameplayTag InTagAirborne = Paper2DPlusAnimationTags::Context_Airborne;
	TestFalse(TEXT("Combat.Heavy does not match Context"), InTagHeavy.MatchesTag(Paper2DPlusAnimationTags::Context));
	TestFalse(TEXT("Context.Airborne does not match Combat"), InTagAirborne.MatchesTag(Paper2DPlusAnimationTags::Combat));

	// Container-level hierarchical query mirrors the single-tag semantics.
	FGameplayTagContainer Container;
	Container.AddTag(InTagHeavy);
	TestTrue(TEXT("Container with Combat.Heavy HasTag(Combat)"), Container.HasTag(Paper2DPlusAnimationTags::Combat));
	TestFalse(TEXT("Container with Combat.Heavy lacks Context"), Container.HasTag(Paper2DPlusAnimationTags::Context));

	return true;
}

// ---------------------------------------------------------------------------
// Shipped ini taxonomy — the plugin's Config/Tags/Paper2DPlusTags.ini source (registered by
// FPaper2DPlusModule::StartupModule via AddTagIniSearchPath) resolves in any host project, without
// entries copied into the project's DefaultGameplayTags.ini. Representative tags per dimension,
// including the seven leaves that exist ONLY in the shipped ini (never authored in host configs),
// so a silent registration failure cannot hide behind project-config copies.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationTagsIniTaxonomyRegistration,
	"Paper2DPlus.AnimationTags.IniTaxonomyRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationTagsIniTaxonomyRegistration::RunTest(const FString& Parameters)
{
	const TCHAR* Expected[] = {
		// One representative per shipped dimension/root.
		TEXT("Paper2DPlus.Animation.Ability.Cast"),
		TEXT("Paper2DPlus.Animation.Combat.Ranged"),
		TEXT("Paper2DPlus.Animation.Context.Sprinting"),
		TEXT("Paper2DPlus.Animation.Flavor.Emote"),
		TEXT("Paper2DPlus.Animation.Interaction.Use"),
		TEXT("Paper2DPlus.Animation.Lifecycle.Death"),
		TEXT("Paper2DPlus.Animation.Locomotion"),
		TEXT("Paper2DPlus.Animation.Locomotion.Idle"),
		TEXT("Paper2DPlus.Animation.Locomotion.WallSlide"),
		TEXT("Paper2DPlus.Animation.Reaction.Hit"),
		TEXT("Paper2DPlus.Phase"),
		TEXT("Paper2DPlus.Phase.Startup"),
		TEXT("Paper2DPlus.Phase.Recovery"),
		// Ini-only leaves added with the shipped taxonomy (absent from every host project config).
		TEXT("Paper2DPlus.Animation.Locomotion.Dash"),
		TEXT("Paper2DPlus.Animation.Locomotion.WallJump"),
		TEXT("Paper2DPlus.Animation.Locomotion.Hang"),
		TEXT("Paper2DPlus.Animation.Context.Injured"),
		TEXT("Paper2DPlus.Animation.Reaction.Grabbed"),
		TEXT("Paper2DPlus.Animation.Reaction.BlockHit"),
		TEXT("Paper2DPlus.Animation.Flavor.Defeat"),
	};

	for (const TCHAR* InTagName : Expected)
	{
		TestTrue(FString::Printf(TEXT("'%s' resolves via RequestGameplayTag"), InTagName),
			AnimTags_Request(InTagName).IsValid());
	}

	return true;
}

#endif // WITH_EDITOR
