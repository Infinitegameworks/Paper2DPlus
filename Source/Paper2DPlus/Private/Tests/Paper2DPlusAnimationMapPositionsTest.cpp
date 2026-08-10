// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusCharacterProfileAsset.h"

/** Animation Map editor node-position store tests (worldless). AnimationMapNodePositions
 *  (WITH_EDITORONLY_DATA, asset-level) is the persistence substrate for graph node placements: keys
 *  are LOWERCASED flipbook names (the NameToFlipbookIndexCache convention), RenameFlipbookAndPropagate
 *  rewrites the key (renamed-move-wins on a stale-key collision; skipped for an empty OldName like the
 *  rest of the propagation block), JSON export/import never sees the map (the R10 neutrality ratchet —
 *  it is deliberately NOT part of FCharacterProfileAssetSerializablePayload), and DuplicateObject
 *  carries it. Helpers are AnimationMapPos_-prefixed per the unity-build file-unique-name rule. */

#if WITH_EDITORONLY_DATA

namespace
{
	/** Add a named flipbook entry. No live UPaperFlipbook is created — every scenario in this file
	 *  works on by-name identity, and null flipbook refs keep ExportToJsonString output identical
	 *  across two same-shaped assets (NewObject'd flipbooks would serialize unique transient object
	 *  paths and break the export byte-equality ratchet). Returns the entry index. */
	int32 AnimationMapPos_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		return Asset->Flipbooks.Add(Anim);
	}
}

// RenameFlipbookAndPropagate rewrites the position key (value preserved, key stored LOWERCASED); a
// case-only rename lands on the same lowercased key and leaves exactly one entry; an empty-OldName
// rename skips the propagation block entirely — no "" key and no new-name key appear.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapPosRenameRewritesKey,
	"Paper2DPlus.AnimationMap.Positions.RenameRewritesKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapPosRenameRewritesKey::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 JabIndex = AnimationMapPos_AddMove(Asset, TEXT("Jab"));
	AnimationMapPos_AddMove(Asset, TEXT("Idle"));
	const FVector2D JabPos(100.0, 200.0);
	Asset->AnimationMapNodePositions.Add(TEXT("jab"), JabPos);

	// Jab -> Punch: the LOWERCASED key moves, the value is preserved.
	TestTrue(TEXT("Rename Jab -> Punch succeeds"), Asset->RenameFlipbookAndPropagate(JabIndex, TEXT("Punch")));
	TestEqual(TEXT("Exactly one entry after the rename"), Asset->AnimationMapNodePositions.Num(), 1);
	TestFalse(TEXT("Old lowercased key removed"), Asset->AnimationMapNodePositions.Contains(TEXT("jab")));
	const FVector2D* Moved = Asset->AnimationMapNodePositions.Find(TEXT("punch"));
	TestTrue(TEXT("New key is the LOWERCASED new name"), Moved != nullptr);
	TestTrue(TEXT("Position value preserved across the rename"), Moved && *Moved == JabPos);

	// Case-only rename: Punch -> PUNCH lands on the SAME lowercased key — exactly one entry survives.
	TestTrue(TEXT("Case-only rename succeeds"), Asset->RenameFlipbookAndPropagate(JabIndex, TEXT("PUNCH")));
	TestEqual(TEXT("Case-only rename leaves exactly one entry"), Asset->AnimationMapNodePositions.Num(), 1);
	const FVector2D* CaseMoved = Asset->AnimationMapNodePositions.Find(TEXT("punch"));
	TestTrue(TEXT("Case-only rename keeps the lowercased key"), CaseMoved != nullptr);
	TestTrue(TEXT("Case-only rename preserves the value"), CaseMoved && *CaseMoved == JabPos);

	// Empty-OldName pin: the propagation block (and the key rewrite inside it) is skipped for an
	// unnamed entry — the map is byte-identical afterwards.
	const int32 UnnamedIndex = AnimationMapPos_AddMove(Asset, FString());
	TestTrue(TEXT("Renaming an unnamed entry succeeds"), Asset->RenameFlipbookAndPropagate(UnnamedIndex, TEXT("Named")));
	TestEqual(TEXT("Empty-OldName rename leaves the map unchanged"), Asset->AnimationMapNodePositions.Num(), 1);
	TestFalse(TEXT("No empty-string key created"), Asset->AnimationMapNodePositions.Contains(FString()));
	TestFalse(TEXT("No new-name key created from an empty OldName"), Asset->AnimationMapNodePositions.Contains(TEXT("named")));

	return true;
}

// Renaming a move onto a name that still holds a stale position entry (its move was deleted; stale
// keys are kept on purpose) makes the renamed move win on BOTH paths: with a stored position it
// overwrites the stale entry; WITHOUT one the stale entry is removed — a never-placed move must not
// silently inherit the dead move's placement.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapPosRenameStaleKeyOverwrite,
	"Paper2DPlus.AnimationMap.Positions.RenameOntoStaleKeyOverwrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapPosRenameStaleKeyOverwrite::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	const int32 JabIndex = AnimationMapPos_AddMove(Asset, TEXT("Jab"));
	const FVector2D JabPos(10.0, 20.0);
	const FVector2D StalePos(999.0, 999.0);
	Asset->AnimationMapNodePositions.Add(TEXT("jab"), JabPos);
	// "Punch" the move is gone; its placement survives as a kept-on-purpose stale key.
	Asset->AnimationMapNodePositions.Add(TEXT("punch"), StalePos);

	TestTrue(TEXT("Rename onto the stale name succeeds"), Asset->RenameFlipbookAndPropagate(JabIndex, TEXT("Punch")));
	TestEqual(TEXT("Stale entry consumed — one entry total"), Asset->AnimationMapNodePositions.Num(), 1);
	TestFalse(TEXT("Old key removed"), Asset->AnimationMapNodePositions.Contains(TEXT("jab")));
	const FVector2D* Result = Asset->AnimationMapNodePositions.Find(TEXT("punch"));
	TestTrue(TEXT("New key present"), Result != nullptr);
	TestTrue(TEXT("Renamed move WINS the collision (keeps its own position, not the stale one)"),
		Result && *Result == JabPos);

	// Position-LESS rename onto a stale key: "Kick" was never placed; renaming it onto dead "Slam"'s
	// kept key must CLEAR that entry, not bequeath Slam's placement to Kick.
	const int32 KickIndex = AnimationMapPos_AddMove(Asset, TEXT("Kick"));
	Asset->AnimationMapNodePositions.Add(TEXT("slam"), StalePos);
	TestTrue(TEXT("Position-less rename onto a stale name succeeds"),
		Asset->RenameFlipbookAndPropagate(KickIndex, TEXT("Slam")));
	TestFalse(TEXT("Stale entry cleared — a never-placed move does not inherit the dead move's position"),
		Asset->AnimationMapNodePositions.Contains(TEXT("slam")));
	TestEqual(TEXT("Only the placed move's entry remains"), Asset->AnimationMapNodePositions.Num(), 1);

	return true;
}

// F18 (cross-version, RESOLVED — worldless-test GC fragility, NOT a plugin/engine bug). These tests
// NewObject a profile then serialize/duplicate it. On UE 5.0 the GC cadence collects the UNROOTED test
// object mid-operation (the heavy FJsonObjectConverter / object-duplicator allocations tip GC over),
// causing a use-after-free access violation. 5.1+ GC happens not to collect at that moment, and real assets
// live in packages (always rooted) so a real 5.0 user never hits it. Proven by toggling AddToRoot on 5.0
// (rooted => passes, unrooted => the AV). Fix = root the test objects across the serialize calls; this is a
// latent worldless-test bug, correct to fix on EVERY version (no version guard).

// ExportToJsonString never mentions the map (the R10 neutrality ratchet), and a populated map is
// byte-invisible: two same-shaped assets export IDENTICAL JSON with and without positions.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapPosJsonExportNeutral,
	"Paper2DPlus.AnimationMap.Positions.JsonExportNeutral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapPosJsonExportNeutral::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* WithPositions = NewObject<UPaper2DPlusCharacterProfileAsset>();
	WithPositions->AddToRoot();
	AnimationMapPos_AddMove(WithPositions, TEXT("Jab"));
	AnimationMapPos_AddMove(WithPositions, TEXT("Idle"));
	WithPositions->AnimationMapNodePositions.Add(TEXT("jab"), FVector2D(64.0, 128.0));
	WithPositions->AnimationMapNodePositions.Add(TEXT("idle"), FVector2D(-32.0, 16.0));
	// Comment boxes are the same editor-only / JSON-neutral class as node positions — add one so export
	// neutrality is proven for both.
	FPaper2DPlusAnimationMapCommentList& CommentList = WithPositions->AnimationMapComments.FindOrAdd(TEXT("Paper2DPlus.Animation.Combo"));
	FPaper2DPlusAnimationMapComment& Comment = CommentList.Comments.AddDefaulted_GetRef();
	Comment.CommentId = FGuid::NewGuid();
	Comment.Text = TEXT("Combo notes");
	Comment.NodePos = FVector2D(10.0, 20.0);
	Comment.NodeSize = FVector2D(300.0, 90.0);

	UPaper2DPlusCharacterProfileAsset* WithoutPositions = NewObject<UPaper2DPlusCharacterProfileAsset>();
	WithoutPositions->AddToRoot();
	AnimationMapPos_AddMove(WithoutPositions, TEXT("Jab"));
	AnimationMapPos_AddMove(WithoutPositions, TEXT("Idle"));

	FString JsonWith;
	TestTrue(TEXT("Export with positions succeeds"), WithPositions->ExportToJsonString(JsonWith));
	TestFalse(TEXT("Export output never contains AnimationMapNodePositions (R10 ratchet)"),
		JsonWith.Contains(TEXT("AnimationMapNodePositions")));
	TestFalse(TEXT("Export output never contains AnimationMapComments (R10 ratchet)"),
		JsonWith.Contains(TEXT("AnimationMapComments")));
	TestFalse(TEXT("Comment text is byte-invisible to export"), JsonWith.Contains(TEXT("Combo notes")));

	FString JsonWithout;
	TestTrue(TEXT("Export without positions succeeds"), WithoutPositions->ExportToJsonString(JsonWithout));
	TestEqual(TEXT("Positions are byte-invisible to export (identical JSON with and without)"),
		JsonWith, JsonWithout);

	WithPositions->RemoveFromRoot();
	WithoutPositions->RemoveFromRoot();
	return true;
}

// ImportFromJsonString wholesale-replaces Flipbooks but never touches the position map: a surviving
// move keeps its placement, and the key for a move the import removed is kept (resurrect-friendly).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapPosImportLeavesMapUntouched,
	"Paper2DPlus.AnimationMap.Positions.ImportLeavesMapUntouched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapPosImportLeavesMapUntouched::RunTest(const FString& Parameters)
{
	// Source profile: moves Jab + Punch (no positions needed — export is position-blind).
	UPaper2DPlusCharacterProfileAsset* Src = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Src->AddToRoot();
	AnimationMapPos_AddMove(Src, TEXT("Jab"));
	AnimationMapPos_AddMove(Src, TEXT("Punch"));
	FString Json;
	TestTrue(TEXT("Export succeeds"), Src->ExportToJsonString(Json));

	// Destination: a different move set (Jab survives the import, Idle does not) with placed nodes.
	UPaper2DPlusCharacterProfileAsset* Dst = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Dst->AddToRoot();
	AnimationMapPos_AddMove(Dst, TEXT("Jab"));
	AnimationMapPos_AddMove(Dst, TEXT("Idle"));
	const FVector2D JabPos(40.0, 80.0);
	const FVector2D IdlePos(7.0, -9.0);
	Dst->AnimationMapNodePositions.Add(TEXT("jab"), JabPos);
	Dst->AnimationMapNodePositions.Add(TEXT("idle"), IdlePos);

	TestTrue(TEXT("Import succeeds"), Dst->ImportFromJsonString(Json));

	// Flipbooks were wholesale-replaced by the payload...
	TestEqual(TEXT("Import replaced the move set"), Dst->Flipbooks.Num(), 2);
	bool bHasPunch = false;
	bool bHasIdle = false;
	for (const FFlipbookProfileEntry& Anim : Dst->Flipbooks)
	{
		bHasPunch |= Anim.Identity.FlipbookName.Equals(TEXT("Punch"), ESearchCase::IgnoreCase);
		bHasIdle |= Anim.Identity.FlipbookName.Equals(TEXT("Idle"), ESearchCase::IgnoreCase);
	}
	TestTrue(TEXT("Imported move set contains Punch"), bHasPunch);
	TestFalse(TEXT("Imported move set no longer contains Idle"), bHasIdle);

	// ...while the position map was NOT touched: the surviving move keeps its spot and the
	// now-stale Idle key is kept (resurrect-friendly — re-adding Idle restores its placement).
	TestEqual(TEXT("Map entry count untouched by import"), Dst->AnimationMapNodePositions.Num(), 2);
	const FVector2D* JabKept = Dst->AnimationMapNodePositions.Find(TEXT("jab"));
	TestTrue(TEXT("Surviving move keeps its position"), JabKept && *JabKept == JabPos);
	const FVector2D* IdleKept = Dst->AnimationMapNodePositions.Find(TEXT("idle"));
	TestTrue(TEXT("Stale key kept across import (resurrect-friendly)"), IdleKept && *IdleKept == IdlePos);

	Src->RemoveFromRoot();
	Dst->RemoveFromRoot();
	return true;
}

// DuplicateObject of the asset carries the position map (plain non-Transient UPROPERTY).
// GUARDED to UE 5.1+ — this one is a GENUINE UE 5.0 engine DUPLICATOR bug, NOT the GC fragility that the
// export/curve tests have. Distinguished empirically: rooting the source object FIXED the GC tests (curve
// round-trip, JSON export) but does NOT fix this (it crashes inside StaticDuplicateObject with the source
// rooted). DuplicateObject deep-COPIES the AnimationMapNodePositions map; on 5.0 the CoreUObject duplicator
// AVs (recursive, reads 0xffff…f) on the flipbook-entries + positions-map shape — fixed by Epic in 5.1.
// The plugin uses default duplication, so there is no plugin-side fix that doesn't alter shipping
// serialization on the 8 working versions. 5.0 user impact is editor-cosmetic (duplicating a profile whose
// Animation-Map graph was arranged); workaround = JSON Export→Import, which is position-blind. The export
// tests below are NOT guarded (rooting fixes them). (ENGINE_*_VERSION, not UE_5_1_OR_LATER — the latter is
// undefined on 5.0 and trips C4668 under -WarningsAsErrors.)
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapPosDuplicateCarriesMap,
	"Paper2DPlus.AnimationMap.Positions.DuplicateCarriesMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationMapPosDuplicateCarriesMap::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Asset->AddToRoot();
	AnimationMapPos_AddMove(Asset, TEXT("Jab"));
	AnimationMapPos_AddMove(Asset, TEXT("Idle"));
	const FVector2D JabPos(5.0, 15.0);
	const FVector2D IdlePos(-25.0, 35.0);
	Asset->AnimationMapNodePositions.Add(TEXT("jab"), JabPos);
	Asset->AnimationMapNodePositions.Add(TEXT("idle"), IdlePos);

	UPaper2DPlusCharacterProfileAsset* Dup =
		DuplicateObject<UPaper2DPlusCharacterProfileAsset>(Asset, GetTransientPackage());
	TestTrue(TEXT("DuplicateObject succeeds"), Dup != nullptr);
	if (!Dup)
	{
		Asset->RemoveFromRoot();
		return false;
	}
	Dup->AddToRoot();

	TestEqual(TEXT("Duplicate carries both map entries"), Dup->AnimationMapNodePositions.Num(), 2);
	const FVector2D* DupJab = Dup->AnimationMapNodePositions.Find(TEXT("jab"));
	TestTrue(TEXT("Duplicate carries jab's position"), DupJab && *DupJab == JabPos);
	const FVector2D* DupIdle = Dup->AnimationMapNodePositions.Find(TEXT("idle"));
	TestTrue(TEXT("Duplicate carries idle's position"), DupIdle && *DupIdle == IdlePos);

	Asset->RemoveFromRoot();
	Dup->RemoveFromRoot();
	return true;
}
#endif // ENGINE >= 5.1 (DuplicateCarriesMap — genuine 5.0 engine DUPLICATOR bug; see the note above)

// Companion smoke test: a profile WITH flipbook entries but NO AnimationMapNodePositions also duplicates
// cleanly on EVERY engine incl. 5.0 (the moves+positions COMBINATION is what trips 5.0's duplicator).
// This is the 5.0-safe duplicate coverage that complements the 5.1+-guarded DuplicateCarriesMap above.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationMapPosDuplicateMovesNoMap,
	"Paper2DPlus.AnimationMap.Positions.DuplicateMovesNoMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaper2DPlusAnimationMapPosDuplicateMovesNoMap::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	AnimationMapPos_AddMove(Asset, TEXT("Jab"));
	AnimationMapPos_AddMove(Asset, TEXT("Idle"));
	UPaper2DPlusCharacterProfileAsset* Dup = DuplicateObject<UPaper2DPlusCharacterProfileAsset>(Asset, GetTransientPackage());
	TestNotNull(TEXT("Profile-with-moves duplicate succeeds (no node positions)"), Dup);
	if (Dup)
	{
		TestEqual(TEXT("Duplicate carries both moves"), Dup->Flipbooks.Num(), 2);
	}
	return true;
}

#endif // WITH_EDITORONLY_DATA

#endif // WITH_EDITOR
