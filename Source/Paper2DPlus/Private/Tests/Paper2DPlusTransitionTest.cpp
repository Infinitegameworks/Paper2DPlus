// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusComboChain.h"
#include "Paper2DPlusSettings.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "UObject/UnrealType.h"

/** TASK-108 U1 — move->move transitions are PURE From->To DATA: each animation stores its outgoing
 *  links on FFlipbookTransitionData, and the asset exposes two BlueprintPure queries keyed off a
 *  authored transition rows plus the focused group/root resolver. The per-row
 *  Tag/Condition/CancelCategory fields are soft-deprecated (*_DEPRECATED) and DROPPED by the shared
 *  load/import migration (MigrateMoveTransitions = DedupeTransitionRows + value drop), which these
 *  worldless tests pin:
 *   - transition-data row order and authoring-in-progress rows;
 *   - migration drop + idempotency (a second run changes nothing — so no second log);
 *   - dedupe (first-in-array wins, case-insensitive, self-loop kept once, empty-target rows exempt);
 *   - rename re-creating a duplicate pair re-dedupes (UpdateTransitionFlipbookName tail);
 *   - legacy JSON with "Tag"/"Condition"/"CancelCategory" keys imports clean, values dropped;
 *   - the slim JSON round-trip (no deprecated keys exported);
 *   - chain semantics: every non-empty-target row is a chain edge (ex-OnBlock rows included).
 *  Helpers are Transition_-prefixed per the unity-build file-unique-name rule. */

namespace
{
	/** Build a flipbook with NumFrames single-run key frames (frame index 0..N-1), owned by Owner. */
	UPaperFlipbook* Transition_MakeFlipbook(UObject* Owner, int32 NumFrames)
	{
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>(Owner);
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		for (int32 i = 0; i < NumFrames; ++i)
		{
			FPaperFlipbookKeyFrame KF;
			KF.FrameRun = 1;
			KF.Sprite = NewObject<UPaperSprite>(FB);
			Mutator.KeyFrames.Add(KF);
		}
		return FB;
	}

	/** Add a flipbook entry named MoveName with a NumFrames-key flipbook; returns the live flipbook. */
	UPaperFlipbook* Transition_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = Transition_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		Asset->Flipbooks.Add(Anim);
		return FB;
	}

	/** Find the transition data for the entry authored under MoveName (case-insensitive), or nullptr. */
	const FFlipbookTransitionData* Transition_FindData(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		for (const FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			if (Anim.Identity.FlipbookName.Equals(MoveName, ESearchCase::IgnoreCase))
			{
				return &Anim.TransitionData;
			}
		}
		return nullptr;
	}
}

// ─── Data layer: authored transition rows remain ordered pure From -> To data ────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionTargetsQuery,
	"Paper2DPlus.Transitions.Data.AuthoredRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionTargetsQuery::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Transition_AddMove(Asset, TEXT("Jab"), 4);
	Transition_AddMove(Asset, TEXT("Jab2"), 4);
	Transition_AddMove(Asset, TEXT("Slam"), 4);

	// A NAME-ONLY entry (no flipbook object) proves the "target can't resolve to an object" skip path.
	{
		FFlipbookProfileEntry NameOnly;
		NameOnly.Identity.FlipbookName = TEXT("GhostMove");
		Asset->Flipbooks.Add(NameOnly);
	}

	FFlipbookTransitionData& TD = Asset->Flipbooks[0].TransitionData;
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Jab2")));       // row 0: resolves
	TD.Transitions.Add(FPaper2DPlusMoveTransition(FString()));          // row 1: authoring-in-progress
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("slam")));       // row 2: case-insensitive resolution
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("NoSuchMove"))); // row 3: dangling -> skipped
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("GhostMove")));  // row 4: name-only entry -> skipped
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("JAB2")));       // row 5: duplicate (in-memory) -> deduped
	TestTrue(TEXT("Authored row storage reports non-empty"), TD.HasTransitions());
	TestEqual(TEXT("Every authored row remains present until an explicit edit/migration"),
		TD.Transitions.Num(), 6);
	if (TD.Transitions.Num() == 6)
	{
		TestEqual(TEXT("First target preserves authored spelling"),
			TD.Transitions[0].TargetMove, FString(TEXT("Jab2")));
		TestTrue(TEXT("Empty target remains an authoring-in-progress row"),
			TD.Transitions[1].TargetMove.IsEmpty());
		TestEqual(TEXT("Dangling target remains diagnosable source data"),
			TD.Transitions[3].TargetMove, FString(TEXT("NoSuchMove")));
		TestEqual(TEXT("Case-variant duplicate remains source data until normalization"),
			TD.Transitions[5].TargetMove, FString(TEXT("JAB2")));
	}
	TestFalse(TEXT("A target-only move has no authored outgoing rows"),
		Asset->Flipbooks[1].TransitionData.HasTransitions());

	return true;
}

// ─── Migration: drop deprecated values + idempotency ─────────────────────────────────────────────

// Rows carrying legacy names/conditions/cancel categories migrate clean: MigrateMoveTransitions drops
// each value (one Info log per drop — visibility is pinned via the returned change count), and a
// second run changes NOTHING (so a re-save/reload emits no second log).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionMigrationDrop,
	"Paper2DPlus.Transitions.Migration.DropDeprecatedValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionMigrationDrop::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Transition_AddMove(Asset, TEXT("Jab"), 4);
	Transition_AddMove(Asset, TEXT("Jab2"), 4);
	Transition_AddMove(Asset, TEXT("Slam"), 4);

	// Simulate a legacy load: the deprecated members are populated exactly as the loader would
	// (UHT registers *_DEPRECATED under bare names, so old .uassets land values here).
	FFlipbookTransitionData& TD = Asset->Flipbooks[0].TransitionData;
	{
		FPaper2DPlusMoveTransition Row(TEXT("Jab2"));
		Row.Tag_DEPRECATED = FName(TEXT("Light"));
		Row.CancelCategory_DEPRECATED = FName(TEXT("Normal"));
		TD.Transitions.Add(Row);
	}
	{
		FPaper2DPlusMoveTransition Row(TEXT("Slam"));
		Row.Condition_DEPRECATED = EPaper2DPlusTransitionCondition::OnBlock; // non-Always -> chain-semantics note
		TD.Transitions.Add(Row);
	}

	const int32 FirstRun = Asset->MigrateMoveTransitions();
	TestEqual(TEXT("First run drops exactly the three legacy values (no dupes in this fixture)"), FirstRun, 3);

	// Values gone; the From->To halves untouched.
	TestTrue(TEXT("Tag dropped"), TD.Transitions[0].Tag_DEPRECATED.IsNone());
	TestTrue(TEXT("CancelCategory dropped"), TD.Transitions[0].CancelCategory_DEPRECATED.IsNone());
	TestTrue(TEXT("Condition reset to Always"),
		TD.Transitions[1].Condition_DEPRECATED == EPaper2DPlusTransitionCondition::Always);
	TestEqual(TEXT("Row count unchanged"), TD.Transitions.Num(), 2);
	TestEqual(TEXT("Row 0 target intact"), TD.Transitions[0].TargetMove, FString(TEXT("Jab2")));
	TestEqual(TEXT("Row 1 target intact"), TD.Transitions[1].TargetMove, FString(TEXT("Slam")));

	// Idempotent: the second run finds nothing to migrate (== no second log on re-save/reload).
	TestEqual(TEXT("Second run is a no-op (idempotent, no second log)"), Asset->MigrateMoveTransitions(), 0);

	return true;
}

// ─── Migration: dedupe (one row per From->To pair) ───────────────────────────────────────────────

// Three A->B rows collapse to ONE, first-in-array wins (pinned by the surviving row's exact case);
// A->B + A->C stay untouched; the dedupe key is case-insensitive; a self-loop survives as a single
// edge; empty-target authoring rows are exempt (two drafts coexist).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionMigrationDedupe,
	"Paper2DPlus.Transitions.Migration.Dedupe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionMigrationDedupe::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Transition_AddMove(Asset, TEXT("Jab"), 4);
	Transition_AddMove(Asset, TEXT("Jab2"), 4);
	Transition_AddMove(Asset, TEXT("Slam"), 4);

	FFlipbookTransitionData& TD = Asset->Flipbooks[0].TransitionData;
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Jab2"))); // survivor (first in array)
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("JAB2"))); // case-variant duplicate -> dropped
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Slam"))); // different pair -> untouched
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("jab2"))); // duplicate -> dropped
	TD.Transitions.Add(FPaper2DPlusMoveTransition(FString()));    // authoring draft -> exempt
	TD.Transitions.Add(FPaper2DPlusMoveTransition(FString()));    // second draft -> exempt too
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Jab")));  // self-loop -> kept once
	TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("jab")));  // self-loop duplicate -> dropped

	const int32 Changes = Asset->MigrateMoveTransitions();
	TestEqual(TEXT("Three duplicate rows dropped"), Changes, 3);
	TestEqual(TEXT("Five rows survive (Jab2, Slam, 2 drafts, self-loop)"), TD.Transitions.Num(), 5);
	if (TD.Transitions.Num() == 5)
	{
		TestTrue(TEXT("First-in-array wins (the survivor keeps its exact case)"),
			TD.Transitions[0].TargetMove.Equals(TEXT("Jab2"), ESearchCase::CaseSensitive));
		TestEqual(TEXT("A->Slam untouched"), TD.Transitions[1].TargetMove, FString(TEXT("Slam")));
		TestTrue(TEXT("Draft rows survive"), TD.Transitions[2].TargetMove.IsEmpty() && TD.Transitions[3].TargetMove.IsEmpty());
		TestTrue(TEXT("Self-loop survives as one edge (first spelling)"),
			TD.Transitions[4].TargetMove.Equals(TEXT("Jab"), ESearchCase::CaseSensitive));
	}

	TestEqual(TEXT("Re-run is a no-op"), Asset->MigrateMoveTransitions(), 0);

	return true;
}

// ─── Migration: a rename can re-create a duplicate pair ──────────────────────────────────────────

// UpdateTransitionFlipbookName re-runs the dedupe at its tail: with rows A->B and A->C, renaming the
// C move to B collapses the two rows to one (first-in-array wins). Covers both the direct helper and
// the full RenameFlipbookAndPropagate path.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionRenameRededupe,
	"Paper2DPlus.Transitions.Migration.RenameRededupe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionRenameRededupe::RunTest(const FString& Parameters)
{
	// Direct helper path.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		Transition_AddMove(Asset, TEXT("Jab"), 4);
		Transition_AddMove(Asset, TEXT("Jab2"), 4);
		Transition_AddMove(Asset, TEXT("Slam"), 4);
		FFlipbookTransitionData& TD = Asset->Flipbooks[0].TransitionData;
		TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Jab2")));
		TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Slam")));

		Asset->UpdateTransitionFlipbookName(TEXT("Slam"), TEXT("Jab2"));
		TestEqual(TEXT("Rename collapsing two pairs re-dedupes to one row"), TD.Transitions.Num(), 1);
		if (TD.Transitions.Num() == 1)
		{
			TestEqual(TEXT("The FIRST authored row survives"), TD.Transitions[0].TargetMove, FString(TEXT("Jab2")));
		}
	}

	// Full rename path: the target name "Finisher" is DANGLING (no entry), so renaming the real move
	// "Slam" to "Finisher" passes the collision check and re-creates the duplicate pair.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		Transition_AddMove(Asset, TEXT("Jab"), 4);
		UPaperFlipbook* SlamFB = Transition_AddMove(Asset, TEXT("Slam"), 4); // index 1 — the rename target
		FFlipbookTransitionData& TD = Asset->Flipbooks[0].TransitionData;
		TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Finisher"))); // dangling
		TD.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Slam")));

		TestTrue(TEXT("RenameFlipbookAndPropagate succeeds"), Asset->RenameFlipbookAndPropagate(1, TEXT("Finisher")));
		TestEqual(TEXT("Renamed entry reports the new name"), Asset->GetFlipbookName(SlamFB), FString(TEXT("Finisher")));
		TestEqual(TEXT("Propagation re-created the pair; dedupe leaves one row"), TD.Transitions.Num(), 1);
		if (TD.Transitions.Num() == 1)
		{
			TestEqual(TEXT("The FIRST authored row (already 'Finisher') survives"),
				TD.Transitions[0].TargetMove, FString(TEXT("Finisher")));
		}
	}

	return true;
}

// ─── JSON: legacy keys import clean, slim round-trip ─────────────────────────────────────────────

// A legacy export carrying "Tag"/"CancelCategory"/"Condition" keys imports without error: the values
// land in the bare-name-registered *_DEPRECATED members and the import-path migration drops them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionJsonLegacyKeys,
	"Paper2DPlus.Transitions.Json.LegacyKeysImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionJsonLegacyKeys::RunTest(const FString& Parameters)
{
	// Handcrafted legacy payload: flat legacy "FlipbookName" keys (the proven alias path) + a
	// transitions row carrying all three removed fields. SchemaVersion omitted = legacy, migrated up.
	const FString LegacyJson = TEXT(R"({
		"Flipbooks": [
			{
				"FlipbookName": "Jab",
				"TransitionData": {
					"BufferGraceFramesOverride": 42,
					"Transitions": [
						{ "Tag": "Light", "TargetMove": "Jab2", "CancelCategory": "Normal", "Condition": "OnBlock" },
						{ "Tag": "Light", "TargetMove": "JAB2" }
					]
				}
			},
			{ "FlipbookName": "Jab2" }
		]
	})");

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestTrue(TEXT("Legacy JSON imports without error"), Asset->ImportFromJsonString(LegacyJson));

	const FFlipbookTransitionData* JabTD = Transition_FindData(Asset, TEXT("Jab"));
	TestTrue(TEXT("The Jab entry imported"), JabTD != nullptr);
	if (!JabTD)
	{
		return false;
	}

	// The duplicate row deduped on import; the surviving row's legacy values dropped.
	TestEqual(TEXT("Duplicate legacy rows deduped to one on import"), JabTD->Transitions.Num(), 1);
	TestEqual(TEXT("Legacy buffer-grace value is absorbed for compatibility"),
		JabTD->BufferGraceFramesOverride_DEPRECATED, 42);
	if (JabTD->Transitions.Num() == 1)
	{
		TestEqual(TEXT("Target intact"), JabTD->Transitions[0].TargetMove, FString(TEXT("Jab2")));
		TestTrue(TEXT("Legacy Tag dropped on import"), JabTD->Transitions[0].Tag_DEPRECATED.IsNone());
		TestTrue(TEXT("Legacy CancelCategory dropped on import"), JabTD->Transitions[0].CancelCategory_DEPRECATED.IsNone());
		TestTrue(TEXT("Legacy Condition dropped on import"),
			JabTD->Transitions[0].Condition_DEPRECATED == EPaper2DPlusTransitionCondition::Always);
	}

	// Import already migrated — a manual re-run changes nothing.
	TestEqual(TEXT("Post-import migration is a no-op"), Asset->MigrateMoveTransitions(), 0);

	FString SlimJson;
	TestTrue(TEXT("Imported payload exports again"), Asset->ExportToJsonString(SlimJson));
	TestFalse(TEXT("Deprecated buffer-grace key does not re-export"),
		SlimJson.Contains(TEXT("\"BufferGraceFramesOverride\":")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusInputBufferDeprecationReflection,
	"Paper2DPlus.Transitions.Deprecation.InputBufferProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusInputBufferDeprecationReflection::RunTest(const FString& Parameters)
{
	FIntProperty* ProjectSetting = FindFProperty<FIntProperty>(
		UPaper2DPlusSettings::StaticClass(), TEXT("InputBufferGraceFrames"));
	TestNotNull(TEXT("historical project setting keeps its original reflected/config name"), ProjectSetting);
	if (ProjectSetting)
	{
		TestTrue(TEXT("historical project setting is deprecated"),
			ProjectSetting->HasAnyPropertyFlags(CPF_Deprecated));
		TestTrue(TEXT("historical project setting remains config-loadable"),
			ProjectSetting->HasAnyPropertyFlags(CPF_Config));
		TestFalse(TEXT("historical project setting is no longer designer-editable"),
			ProjectSetting->HasAnyPropertyFlags(CPF_Edit));

		UPaper2DPlusSettings* Settings = NewObject<UPaper2DPlusSettings>();
		ProjectSetting->SetPropertyValue_InContainer(Settings, 17);
		TestEqual(TEXT("legacy reflected import can still populate the compatibility field"),
			ProjectSetting->GetPropertyValue_InContainer(Settings), 17);
	}

	FIntProperty* PerMoveSetting = FindFProperty<FIntProperty>(
		FFlipbookTransitionData::StaticStruct(), TEXT("BufferGraceFramesOverride"));
	TestNotNull(TEXT("historical per-move override keeps its original reflected name"), PerMoveSetting);
	if (PerMoveSetting)
	{
		TestTrue(TEXT("historical per-move override is deprecated"),
			PerMoveSetting->HasAnyPropertyFlags(CPF_Deprecated));
		TestFalse(TEXT("historical per-move override is no longer designer-editable"),
			PerMoveSetting->HasAnyPropertyFlags(CPF_Edit));
	}

	return true;
}

// The slim payload round-trips: TargetMove + optional phase override survive in authored order, and
// the export carries NO deprecated transition keys (the exporter skips CPF_Deprecated).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionJsonRoundTrip,
	"Paper2DPlus.Transitions.Json.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionJsonRoundTrip::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Src = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Transition_AddMove(Src, TEXT("Jab"), 4);
	Transition_AddMove(Src, TEXT("Jab2"), 4);
	Transition_AddMove(Src, TEXT("Slam"), 4);
	Src->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Jab2")));
	Src->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Slam")));
	const FGameplayTag ActivePhase = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Paper2DPlus.Phase.Active")), /*ErrorIfNotFound=*/false);
	TestTrue(TEXT("Native Active phase tag is registered"), ActivePhase.IsValid());
	Src->Flipbooks[0].TransitionData.Transitions[0].PhaseTagOverride = ActivePhase;

	FString Json;
	TestTrue(TEXT("Export to JSON succeeds"), Src->ExportToJsonString(Json));

	// Slim payload ratchet: the deprecated per-row keys never re-enter the export (the exporter skips
	// CPF_Deprecated properties — ue-coreredirect-rename-patterns.md §13). Assert the QUOTED-KEY form
	// ("Tag":) — the bare string "Tag" legitimately appears inside OTHER keys ("PhaseTag",
	// "TagMappings", "AnimationTags", gameplay-tag "TagName" payloads), so a bare Contains would
	// false-hit; the quoted-key form can only match the deprecated transition-row key itself.
	// NOTE: the exporter's GroupBindings rows carry a legitimate "Tag" key (FSerializableTagMapping)
	// — this asset deliberately authors NO TagMappings, so keep it that way in this test.
	TestFalse(TEXT("Export carries no CancelCategory key"), Json.Contains(TEXT("\"CancelCategory\":")));
	TestFalse(TEXT("Export carries no Tag key"), Json.Contains(TEXT("\"Tag\":")));
	TestFalse(TEXT("Export carries no Condition key"), Json.Contains(TEXT("\"Condition\":")));

	UPaper2DPlusCharacterProfileAsset* Dst = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestTrue(TEXT("Import from JSON succeeds"), Dst->ImportFromJsonString(Json));

	const FFlipbookTransitionData* JabTD = Transition_FindData(Dst, TEXT("Jab"));
	TestTrue(TEXT("The Jab entry survives the round-trip"), JabTD != nullptr);
	if (!JabTD)
	{
		return false;
	}
	TestTrue(TEXT("Imported Jab still has transitions"), JabTD->HasTransitions());
	TestEqual(TEXT("Both transition rows survive"), JabTD->Transitions.Num(), 2);
	if (JabTD->Transitions.Num() == 2)
	{
		TestEqual(TEXT("Authored order intact: row 0 targets Jab2"), JabTD->Transitions[0].TargetMove, FString(TEXT("Jab2")));
		TestEqual(TEXT("Authored order intact: row 1 targets Slam"), JabTD->Transitions[1].TargetMove, FString(TEXT("Slam")));
		TestEqual(TEXT("Row-owned phase override survives JSON"),
			JabTD->Transitions[0].PhaseTagOverride, ActivePhase);
		TestFalse(TEXT("Unset phase override stays unset"),
			JabTD->Transitions[1].PhaseTagOverride.IsValid());
	}

	// A non-source move imports with no transitions of its own.
	const FFlipbookTransitionData* Jab2TD = Transition_FindData(Dst, TEXT("Jab2"));
	TestTrue(TEXT("The Jab2 entry survives the round-trip"), Jab2TD != nullptr);
	if (Jab2TD)
	{
		TestFalse(TEXT("Imported Jab2 carries no transitions"), Jab2TD->HasTransitions());
	}

	return true;
}

// ─── Chain semantics: every non-empty-target row is a chain edge (TASK-108 KTD) ──────────────────

// An ex-OnBlock row (a still-populated Condition_DEPRECATED, as loaded pre-migration) counts as a
// chain edge inside the selected exact group/root; empty-target rows stay ignored.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionChainSemantics,
	"Paper2DPlus.Transitions.ChainSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionChainSemantics::RunTest(const FString& Parameters)
{
	const FGameplayTag GroupTag = FGameplayTag::RequestGameplayTag(
		FName("Paper2DPlus.Animation.Combat.Attack"), false);
	if (!GroupTag.IsValid())
	{
		AddInfo(TEXT("Animation Map fixture group is unavailable; skipping scoped chain coverage."));
		return true;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Transition_AddMove(Asset, TEXT("A"), 2); // 0
	Transition_AddMove(Asset, TEXT("B"), 2); // 1
	Transition_AddMove(Asset, TEXT("C"), 2); // 2
	Transition_AddMove(Asset, TEXT("D"), 2); // 3
	FFlipbookTagMapping& Mapping = Asset->TagMappings.FindOrAdd(GroupTag);
	Mapping.Entries.Emplace(TEXT("A"));
	Mapping.Entries.Emplace(TEXT("B"));
	Mapping.Entries.Emplace(TEXT("C"));
	Mapping.Entries.Emplace(TEXT("D"));
	Mapping.Entries[3].bIsChainStart = true;

	// A -> B (plain); B -> C carrying a pre-migration OnBlock condition; B -> "" (draft, ignored).
	Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("B")));
	{
		FPaper2DPlusMoveTransition BlockRow(TEXT("C"));
		BlockRow.Condition_DEPRECATED = EPaper2DPlusTransitionCondition::OnBlock;
		Asset->Flipbooks[1].TransitionData.Transitions.Add(BlockRow);
	}
	Asset->Flipbooks[1].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(FString()));

	// IsConfirmTransition = "non-empty target" — the condition value is irrelevant even pre-migration.
	TestTrue(TEXT("Ex-OnBlock row IS a chain edge"),
		Paper2DPlusComboChain::IsConfirmTransition(Asset->Flipbooks[1].TransitionData.Transitions[0]));
	TestFalse(TEXT("Empty-target row is NOT a chain edge"),
		Paper2DPlusComboChain::IsConfirmTransition(Asset->Flipbooks[1].TransitionData.Transitions[1]));

	// D -> A carries a pre-migration condition too. Root identity comes only from the exact mapping.
	{
		FPaper2DPlusMoveTransition EntryRow(TEXT("A"));
		EntryRow.Condition_DEPRECATED = EPaper2DPlusTransitionCondition::OnBlock;
		Asset->Flipbooks[3].TransitionData.Transitions.Add(EntryRow);
	}

	const TArray<Paper2DPlusComboChain::FScopedAnimationRoot> Roots =
		Paper2DPlusComboChain::GetAnimationRoots(Asset);
	TestEqual(TEXT("One exact group/root identity"), Roots.Num(), 1);
	TestTrue(TEXT("Root identity is (Attack, D)"),
		Roots.Num() == 1 && Roots[0].GroupTag == GroupTag
		&& Roots[0].RootMove == TEXT("D"));

	const TArray<FString> Chain =
		Paper2DPlusComboChain::DeriveComboChain(Asset, GroupTag, TEXT("D"));
	TestEqual(TEXT("Scoped chain includes every non-empty legacy-conditioned edge"), Chain.Num(), 4);
	if (Chain.Num() == 4)
	{
		TestEqual(TEXT("Root is first"), Chain[0], FString(TEXT("D")));
		TestEqual(TEXT("Entry edge reaches A"), Chain[1], FString(TEXT("A")));
		TestEqual(TEXT("Plain edge reaches B"), Chain[2], FString(TEXT("B")));
		TestEqual(TEXT("Ex-OnBlock edge reaches C"), Chain[3], FString(TEXT("C")));
	}

	return true;
}

// ─── Chain-start traversal: self-loops, incoming edges, and chain-start boundaries ───────────────

// Chain-start identity is explicit `(GroupTag, flagged entry)` authoring. These fixtures pin cycle
// termination, prove that incoming arrows cannot redefine a chain start, and stop traversal before
// another flagged chain start.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionSelfLoopRoots,
	"Paper2DPlus.Transitions.ChainStartCyclesAndBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionSelfLoopRoots::RunTest(const FString& Parameters)
{
	const FGameplayTag GroupTag = FGameplayTag::RequestGameplayTag(
		FName("Paper2DPlus.Animation.Combat.Attack"), false);
	if (!GroupTag.IsValid())
	{
		AddInfo(TEXT("Animation Map fixture group is unavailable; skipping scoped self-loop coverage."));
		return true;
	}

	// 1. A->A + A->B terminates at the revisit while preserving authored row order.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		Transition_AddMove(Asset, TEXT("A"), 2);
		Transition_AddMove(Asset, TEXT("B"), 2);
		FFlipbookTagMapping& Mapping = Asset->TagMappings.FindOrAdd(GroupTag);
		Mapping.Entries.Emplace(TEXT("A"));
		Mapping.Entries.Last().bIsChainStart = true;
		Mapping.Entries.Emplace(TEXT("B"));
		Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("a"))); // self-loop
		Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("B")));

		const TArray<FString> Chain = Paper2DPlusComboChain::DeriveComboChain(Asset, GroupTag, TEXT("A"));
		TestEqual(TEXT("Chain from A is A,B (self-loop terminates at the revisit)"), Chain.Num(), 2);
		TestTrue(TEXT("B is in A's chain"), Chain.Contains(TEXT("B")));
	}

	// 2. A pure self-loop is a valid one-move chain start.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		Transition_AddMove(Asset, TEXT("A"), 2);
		FFlipbookTagMappingEntry RootEntry(TEXT("A"));
		RootEntry.bIsChainStart = true;
		Asset->TagMappings.FindOrAdd(GroupTag).Entries.Add(RootEntry);
		Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("A")));

		TestEqual(TEXT("Its chain is length 1 (just itself)"),
			Paper2DPlusComboChain::DeriveComboChain(Asset, GroupTag, TEXT("A")).Num(), 1);
	}

	// 3. An incoming arrow never changes explicitly authored chain-start identity.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		Transition_AddMove(Asset, TEXT("A"), 2);
		Transition_AddMove(Asset, TEXT("B"), 2);
		Transition_AddMove(Asset, TEXT("C"), 2);
		FFlipbookTagMapping& Mapping = Asset->TagMappings.FindOrAdd(GroupTag);
		Mapping.Entries.Emplace(TEXT("A"));
		Mapping.Entries.Last().bIsChainStart = true;
		Mapping.Entries.Emplace(TEXT("B"));
		Mapping.Entries.Emplace(TEXT("C"));
		Asset->Flipbooks[1].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("A"))); // B->A
		Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("C"))); // A->C

		Paper2DPlusComboChain::FScopedAnimationRoot Root;
		TestTrue(TEXT("A remains the chain start despite B's incoming edge"),
			Paper2DPlusComboChain::FindAnimationRoot(Asset, GroupTag, TEXT("A"), Root));
		TestEqual(TEXT("The chain start stays A"), Root.RootMove, FString(TEXT("A")));
	}

	// 4. A different flagged chain start is a traversal boundary even when an edge points to it.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		Transition_AddMove(Asset, TEXT("A"), 2);
		Transition_AddMove(Asset, TEXT("B"), 2);
		FFlipbookTagMapping& Mapping = Asset->TagMappings.FindOrAdd(GroupTag);
		Mapping.Entries.Emplace(TEXT("A"));
		Mapping.Entries.Last().bIsChainStart = true;
		Mapping.Entries.Emplace(TEXT("B"));
		Mapping.Entries.Last().bIsChainStart = true;
		Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("A"))); // self-loop
		Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("B")));

		TestEqual(TEXT("Two flagged chain starts are enumerated as two valid chains"),
			Paper2DPlusComboChain::GetAnimationRoots(Asset).Num(), 2);
		const TArray<FString> FirstChain =
			Paper2DPlusComboChain::DeriveComboChain(Asset, GroupTag, TEXT("A"));
		TestEqual(TEXT("A's chain stops before B's chain start"), FirstChain.Num(), 1);
		TestFalse(TEXT("B's chain is not absorbed by A's chain"), FirstChain.Contains(TEXT("B")));
	}

	return true;
}

// ─── Rename propagation ───────────────────────────────────────────────────────────────────────────

// Renaming a TARGET move via RenameFlipbookAndPropagate rewrites the entry name AND propagates into
// transition targets: the same target flipbook object still resolves afterward.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionRenamePropagation,
	"Paper2DPlus.Transitions.RenamePropagation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionRenamePropagation::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* JabFB = Transition_AddMove(Asset, TEXT("Jab"), 4);
	UPaperFlipbook* Jab2FB = Transition_AddMove(Asset, TEXT("Jab2"), 4); // index 1 — the rename target
	Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Jab2")));

	TestEqual(TEXT("Row targets Jab2 before the rename"),
		Asset->Flipbooks[0].TransitionData.Transitions[0].TargetMove, FString(TEXT("Jab2")));

	TestTrue(TEXT("RenameFlipbookAndPropagate succeeds"), Asset->RenameFlipbookAndPropagate(1, TEXT("Jab2Renamed")));

	// The entry itself was renamed (same flipbook object, new authored name)...
	TestEqual(TEXT("Renamed entry reports the new name"), Asset->GetFlipbookName(Jab2FB), FString(TEXT("Jab2Renamed")));
	// ...and the transition target string followed, while the retained name/object resolver still
	// returns the same flipbook object under its new identity.
	TestEqual(TEXT("Transition target string followed the rename"),
		Asset->Flipbooks[0].TransitionData.Transitions[0].TargetMove, FString(TEXT("Jab2Renamed")));
	TestTrue(TEXT("Renamed target identity resolves to the same flipbook object"),
		Asset->GetFlipbookByName(TEXT("Jab2Renamed")) == Jab2FB);
	(void)JabFB;

	return true;
}

// ─── Validation ──────────────────────────────────────────────────────────────────────────────────

// A non-empty dangling target is a Warning (validation still passes); an empty-target row is exempt
// (authoring-in-progress); fixing the target clears the issue.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTransitionValidationDanglingTarget,
	"Paper2DPlus.Transitions.Validation.DanglingTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTransitionValidationDanglingTarget::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Transition_AddMove(Asset, TEXT("Jab"), 4);
	Transition_AddMove(Asset, TEXT("Jab2"), 4);
	Asset->Flipbooks[0].TransitionData.Transitions.Add(
		FPaper2DPlusMoveTransition(TEXT("DoesNotExist")));
	// Empty targets are authoring-in-progress rows and must NOT be flagged.
	Asset->Flipbooks[0].TransitionData.Transitions.Add(
		FPaper2DPlusMoveTransition(FString()));

	TArray<FCharacterProfileValidationIssue> Issues;
	TestTrue(TEXT("Dangling transition target is not an Error (validation still passes)"),
		Asset->ValidateCharacterProfileAsset(Issues));

	int32 DanglingWarnings = 0;
	int32 TransitionIssues = 0;
	for (const FCharacterProfileValidationIssue& Issue : Issues)
	{
		if (Issue.Message.Contains(TEXT("targets flipbook")))
		{
			++TransitionIssues;
		}
		if (Issue.Message.Contains(TEXT("DoesNotExist")))
		{
			++DanglingWarnings;
			TestTrue(TEXT("Dangling-target issue severity is Warning"),
				Issue.Severity == ECharacterProfileValidationSeverity::Warning);
		}
	}
	TestEqual(TEXT("Exactly one dangling-target warning"), DanglingWarnings, 1);
	TestEqual(TEXT("Empty-target row is not flagged (one transition issue total)"), TransitionIssues, 1);

	// Fix the target to a real move -> the transition issue disappears.
	Asset->Flipbooks[0].TransitionData.Transitions[0].TargetMove = TEXT("Jab2");
	TArray<FCharacterProfileValidationIssue> IssuesAfterFix;
	TestTrue(TEXT("Validation passes after the fix"), Asset->ValidateCharacterProfileAsset(IssuesAfterFix));
	int32 RemainingTransitionIssues = 0;
	for (const FCharacterProfileValidationIssue& Issue : IssuesAfterFix)
	{
		if (Issue.Message.Contains(TEXT("targets flipbook")) || Issue.Message.Contains(TEXT("DoesNotExist")))
		{
			++RemainingTransitionIssues;
		}
	}
	TestEqual(TEXT("No transition issue after the fix"), RemainingTransitionIssues, 0);

	return true;
}

#endif // WITH_EDITOR
