// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// Worldless coverage for the TASK-189 structural-diff/merge pure cores (AsepriteStructuralDiff.h):
// rename pairing (unambiguous + pixel-identical only), removal dispositions (shared beats authored
// beats clean), the per-animation AnimationSprites merge, and the content-hash identity contracts.

#include "AsepriteStructuralDiff.h"

#include "AsepriteImporter.h"
#include "Misc/AutomationTest.h"
#include "Paper2DPlusCharacterLayerAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusAsepriteStructuralDiffTest
{
	FAseDiffOldItem AseDiffTest_Old(const FString& Name, const FString& Hash,
		bool bAuthored = false, bool bShared = false)
	{
		FAseDiffOldItem Item;
		Item.Name = Name;
		Item.ContentHash = Hash;
		Item.bHasAuthoredData = bAuthored;
		Item.bSharedWithOtherSources = bShared;
		return Item;
	}

	FAseDiffNewItem AseDiffTest_New(const FString& Name, const FString& Hash)
	{
		FAseDiffNewItem Item;
		Item.Name = Name;
		Item.ContentHash = Hash;
		return Item;
	}

	FCharacterLayerAnimationMapping AseDiffTest_Mapping(const FString& AnimName,
		const TArray<FString>& SpritePaths)
	{
		FCharacterLayerAnimationMapping Mapping;
		Mapping.AnimationName = AnimName;
		for (const FString& Path : SpritePaths)
		{
			Mapping.Sprites.Add(TSoftObjectPtr<UPaperSprite>(FSoftObjectPath(Path)));
		}
		return Mapping;
	}

	// A tiny parsed-data fixture: N canvas frames of the given color, 2x2.
	FAsepriteParsedData AseDiffTest_ParsedFrames(const TArray<FColor>& FrameColors)
	{
		FAsepriteParsedData Data;
		Data.Width = 2;
		Data.Height = 2;
		for (const FColor& Color : FrameColors)
		{
			FAsepriteFrame Frame;
			Frame.Width = 2;
			Frame.Height = 2;
			Frame.Duration = 100;
			Frame.Pixels.Init(Color, 4);
			Data.Frames.Add(MoveTemp(Frame));
		}
		return Data;
	}

	FAsepriteTag AseDiffTest_Tag(const FString& Name, int32 From, int32 To)
	{
		FAsepriteTag Tag;
		Tag.Name = Name;
		Tag.FromFrame = From;
		Tag.ToFrame = To;
		return Tag;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffRenamePairsTest,
	"Paper2DPlus.AsepriteStructuralDiff.RenamePairsUnambiguousIdenticalHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffRenamePairsTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	// "Body" renamed to "Torso" (identical pixels = identical hash); "Head" matched by name.
	const TArray<FAseDiffOldItem> OldItems = {
		AseDiffTest_Old(TEXT("Body"), TEXT("hashA"), /*bAuthored=*/true),
		AseDiffTest_Old(TEXT("Head"), TEXT("hashH"))
	};
	const TArray<FAseDiffNewItem> NewItems = {
		AseDiffTest_New(TEXT("Torso"), TEXT("hashA")),
		AseDiffTest_New(TEXT("Head"), TEXT("hashH2"))
	};

	const FAseDiffResult Result = FAsepriteStructuralDiff::DiffItems(OldItems, NewItems);

	TestEqual(TEXT("Exactly one rename pairs"), Result.Renames.Num(), 1);
	if (Result.Renames.Num() == 1)
	{
		TestEqual(TEXT("Rename old name"), Result.Renames[0].OldName, FString(TEXT("Body")));
		TestEqual(TEXT("Rename new name"), Result.Renames[0].NewName, FString(TEXT("Torso")));
	}
	// Authored data does NOT block a rename — renames PRESERVE authored data by rebinding it.
	TestEqual(TEXT("No removals when the sole removed item paired"), Result.Removals.Num(), 0);
	TestEqual(TEXT("No adds when the sole added item paired"), Result.Added.Num(), 0);
	TestEqual(TEXT("Name-matched item is matched, not renamed"), Result.MatchedNames.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffRenameRefusalTest,
	"Paper2DPlus.AsepriteStructuralDiff.RenameRefusedOnAmbiguityMismatchOrLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffRenameRefusalTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	// Ambiguous: TWO removed layers share the added item's hash -> nothing may pair.
	{
		const TArray<FAseDiffOldItem> OldItems = {
			AseDiffTest_Old(TEXT("ArmL"), TEXT("same")),
			AseDiffTest_Old(TEXT("ArmR"), TEXT("same"))
		};
		const TArray<FAseDiffNewItem> NewItems = { AseDiffTest_New(TEXT("Arm"), TEXT("same")) };
		const FAseDiffResult Result = FAsepriteStructuralDiff::DiffItems(OldItems, NewItems);
		TestEqual(TEXT("Ambiguous hash pairs nothing"), Result.Renames.Num(), 0);
		TestEqual(TEXT("Both ambiguous removed items fall to removals"), Result.Removals.Num(), 2);
		TestEqual(TEXT("Ambiguous added item stays added"), Result.Added.Num(), 1);
	}

	// Hash mismatch: removed and added differ in pixels -> delete+add, never a guess.
	{
		const TArray<FAseDiffOldItem> OldItems = { AseDiffTest_Old(TEXT("Cape"), TEXT("hash1")) };
		const TArray<FAseDiffNewItem> NewItems = { AseDiffTest_New(TEXT("Cloak"), TEXT("hash2")) };
		const FAseDiffResult Result = FAsepriteStructuralDiff::DiffItems(OldItems, NewItems);
		TestEqual(TEXT("Mismatched hash pairs nothing"), Result.Renames.Num(), 0);
		TestEqual(TEXT("Mismatched removed item falls to removals"), Result.Removals.Num(), 1);
		TestEqual(TEXT("Mismatched added item stays added"), Result.Added.Num(), 1);
	}

	// Legacy: the old item was never hash-stamped -> pairing is impossible by construction.
	{
		const TArray<FAseDiffOldItem> OldItems = { AseDiffTest_Old(TEXT("Cape"), FString()) };
		const TArray<FAseDiffNewItem> NewItems = { AseDiffTest_New(TEXT("Cloak"), TEXT("hash2")) };
		const FAseDiffResult Result = FAsepriteStructuralDiff::DiffItems(OldItems, NewItems);
		TestEqual(TEXT("Legacy empty hash pairs nothing"), Result.Renames.Num(), 0);
		TestEqual(TEXT("Legacy removed item falls to removals"), Result.Removals.Num(), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffRenameCollisionTest,
	"Paper2DPlus.AsepriteStructuralDiff.RenameRefusedWhenNewNameAlreadyOwned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffRenameCollisionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	// Source B renames its own "Body" to "Torso" — but sibling source A already contributes a layer
	// called "Torso". The hash pairing is unique and pixel-identical, yet accepting the rename would
	// rename B's generated assets onto a name A owns (on-disk collision, or a silent merge into A's
	// layer data). Fail closed: delete + add, reported.
	const TArray<FAseDiffOldItem> OldItems = { AseDiffTest_Old(TEXT("Body"), TEXT("hashX")) };

	FAseDiffNewItem Incoming = AseDiffTest_New(TEXT("Torso"), TEXT("hashX"));
	Incoming.bCollidesWithExistingItem = true;
	const TArray<FAseDiffNewItem> NewItems = { Incoming };

	const FAseDiffResult Result = FAsepriteStructuralDiff::DiffItems(OldItems, NewItems);

	TestEqual(TEXT("Colliding new name blocks the rename"), Result.Renames.Num(), 0);
	TestEqual(TEXT("The removed item is reported"), Result.Removals.Num(), 1);
	TestEqual(TEXT("The added item stays an add"), Result.Added.Num(), 1);

	// Same pairing WITHOUT the collision flag still renames — proving the flag is what refused it.
	const TArray<FAseDiffNewItem> FreeNewItems = { AseDiffTest_New(TEXT("Torso"), TEXT("hashX")) };
	const FAseDiffResult FreeResult = FAsepriteStructuralDiff::DiffItems(OldItems, FreeNewItems);
	TestEqual(TEXT("Without the collision the rename is accepted"), FreeResult.Renames.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffRemovalDispositionTest,
	"Paper2DPlus.AsepriteStructuralDiff.RemovalDispositionsAndSharedGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffRemovalDispositionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	// A sibling-shared removed item must NOT pair as a rename even when the hash match is unique
	// and pixel-identical — and its removal reports the shared ownership (beats authored).
	const TArray<FAseDiffOldItem> OldItems = {
		AseDiffTest_Old(TEXT("Walk"), TEXT("hashW"), /*bAuthored=*/true, /*bShared=*/true),
		AseDiffTest_Old(TEXT("Idle"), TEXT("hashI"), /*bAuthored=*/true),
		AseDiffTest_Old(TEXT("Jump"), TEXT("hashJ"))
	};
	const TArray<FAseDiffNewItem> NewItems = { AseDiffTest_New(TEXT("Stride"), TEXT("hashW")) };

	const FAseDiffResult Result = FAsepriteStructuralDiff::DiffItems(OldItems, NewItems);

	TestEqual(TEXT("Shared removed item never pairs"), Result.Renames.Num(), 0);
	TestEqual(TEXT("All three removed items classified"), Result.Removals.Num(), 3);

	// Require each item BY NAME: an if/else chain over the results would silently assert nothing at
	// all if production ever emitted a removal under a different name.
	auto RequireDisposition = [this, &Result](const TCHAR* Name, EAseDiffRemovalDisposition Expected, const TCHAR* What)
	{
		const FAseDiffRemoval* Removal = Result.Removals.FindByPredicate(
			[Name](const FAseDiffRemoval& Candidate) { return Candidate.Name == Name; });
		TestNotNull(What, Removal);
		if (Removal)
		{
			TestTrue(What, Removal->Disposition == Expected);
		}
	};
	RequireDisposition(TEXT("Walk"), EAseDiffRemovalDisposition::KeepSharedWithOtherSource,
		TEXT("Shared wins the disposition"));
	RequireDisposition(TEXT("Idle"), EAseDiffRemovalDisposition::KeepAuthoredData,
		TEXT("Authored data keeps"));
	RequireDisposition(TEXT("Jump"), EAseDiffRemovalDisposition::RemoveClean,
		TEXT("Pristine removes clean"));
	TestEqual(TEXT("Unpaired added item stays added"), Result.Added.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffMergeUnionTest,
	"Paper2DPlus.AsepriteStructuralDiff.MergeAnimationSpritesUnion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffMergeUnionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	// File A contributed Idle+Walk; file B arrives with Walk (new art) + Attack (new animation).
	// The exact BJ failure was Walk's refresh wiping Idle — the union must keep it.
	TArray<FCharacterLayerAnimationMapping> Existing;
	Existing.Add(AseDiffTest_Mapping(TEXT("Idle"), { TEXT("/Temp/AseDiff/IdleA.IdleA") }));
	Existing.Add(AseDiffTest_Mapping(TEXT("Walk"), { TEXT("/Temp/AseDiff/WalkA.WalkA") }));

	TArray<FCharacterLayerAnimationMapping> Incoming;
	Incoming.Add(AseDiffTest_Mapping(TEXT("Walk"), { TEXT("/Temp/AseDiff/WalkB.WalkB") }));
	Incoming.Add(AseDiffTest_Mapping(TEXT("Attack"), { TEXT("/Temp/AseDiff/AttackB.AttackB") }));

	int32 Refreshed = 0;
	int32 Added = 0;
	const bool bChanged = FAsepriteStructuralDiff::MergeAnimationSprites(Existing, Incoming, &Refreshed, &Added);

	TestTrue(TEXT("Merge reports a change"), bChanged);
	TestEqual(TEXT("One mapping refreshed"), Refreshed, 1);
	TestEqual(TEXT("One mapping added"), Added, 1);
	TestEqual(TEXT("Union holds all three animations"), Existing.Num(), 3);

	const FCharacterLayerAnimationMapping* Idle = Existing.FindByPredicate(
		[](const FCharacterLayerAnimationMapping& M) { return M.AnimationName == TEXT("Idle"); });
	const FCharacterLayerAnimationMapping* Walk = Existing.FindByPredicate(
		[](const FCharacterLayerAnimationMapping& M) { return M.AnimationName == TEXT("Walk"); });
	TestNotNull(TEXT("File A's Idle survives"), Idle);
	if (Idle)
	{
		TestEqual(TEXT("Idle still points at A's sprite"),
			Idle->Sprites[0].ToSoftObjectPath().ToString(), FString(TEXT("/Temp/AseDiff/IdleA.IdleA")));
	}
	TestNotNull(TEXT("Walk present"), Walk);
	if (Walk)
	{
		TestEqual(TEXT("Walk refreshed to B's sprite"),
			Walk->Sprites[0].ToSoftObjectPath().ToString(), FString(TEXT("/Temp/AseDiff/WalkB.WalkB")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffMergeNoOpTest,
	"Paper2DPlus.AsepriteStructuralDiff.MergeNoOpAndAppendOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffMergeNoOpTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	TArray<FCharacterLayerAnimationMapping> Existing;
	Existing.Add(AseDiffTest_Mapping(TEXT("Idle"), { TEXT("/Temp/AseDiff/Idle.Idle") }));
	Existing.Add(AseDiffTest_Mapping(TEXT("Walk"), { TEXT("/Temp/AseDiff/Walk.Walk") }));

	// Identical incoming set (reordered) = a true no-op: returns false, zero counts, same state.
	TArray<FCharacterLayerAnimationMapping> Incoming;
	Incoming.Add(AseDiffTest_Mapping(TEXT("Walk"), { TEXT("/Temp/AseDiff/Walk.Walk") }));
	Incoming.Add(AseDiffTest_Mapping(TEXT("Idle"), { TEXT("/Temp/AseDiff/Idle.Idle") }));

	int32 Refreshed = 0;
	int32 Added = 0;
	const bool bChanged = FAsepriteStructuralDiff::MergeAnimationSprites(Existing, Incoming, &Refreshed, &Added);

	TestFalse(TEXT("Identical incoming is a no-op"), bChanged);
	TestEqual(TEXT("No refreshes on no-op"), Refreshed, 0);
	TestEqual(TEXT("No adds on no-op"), Added, 0);
	TestEqual(TEXT("Mapping count unchanged"), Existing.Num(), 2);
	TestEqual(TEXT("Original order preserved"), Existing[0].AnimationName, FString(TEXT("Idle")));

	// Empty incoming is also a no-op.
	const bool bEmptyChanged = FAsepriteStructuralDiff::MergeAnimationSprites(
		Existing, TArray<FCharacterLayerAnimationMapping>(), &Refreshed, &Added);
	TestFalse(TEXT("Empty incoming is a no-op"), bEmptyChanged);

	// Order contract: existing entries never move, and appends follow the INCOMING order. Merging
	// the same two new animations in opposite orders yields the same counts and the same set, with
	// each call's appends in its own order — this fails if appends ever stop tracking input order.
	TArray<FCharacterLayerAnimationMapping> BaseA;
	BaseA.Add(AseDiffTest_Mapping(TEXT("Idle"), { TEXT("/Temp/AseDiff/Idle.Idle") }));
	TArray<FCharacterLayerAnimationMapping> BaseB = BaseA;

	int32 RefreshedA = 0;
	int32 AddedA = 0;
	int32 RefreshedB = 0;
	int32 AddedB = 0;
	FAsepriteStructuralDiff::MergeAnimationSprites(BaseA,
		{ AseDiffTest_Mapping(TEXT("Attack"), { TEXT("/Temp/AseDiff/Attack.Attack") }),
		  AseDiffTest_Mapping(TEXT("Jump"), { TEXT("/Temp/AseDiff/Jump.Jump") }) },
		&RefreshedA, &AddedA);
	FAsepriteStructuralDiff::MergeAnimationSprites(BaseB,
		{ AseDiffTest_Mapping(TEXT("Jump"), { TEXT("/Temp/AseDiff/Jump.Jump") }),
		  AseDiffTest_Mapping(TEXT("Attack"), { TEXT("/Temp/AseDiff/Attack.Attack") }) },
		&RefreshedB, &AddedB);

	TestEqual(TEXT("Both orders add the same count"), AddedA, AddedB);
	TestEqual(TEXT("Both orders refresh the same count"), RefreshedA, RefreshedB);
	TestEqual(TEXT("Existing entry stays first (A)"), BaseA[0].AnimationName, FString(TEXT("Idle")));
	TestEqual(TEXT("Existing entry stays first (B)"), BaseB[0].AnimationName, FString(TEXT("Idle")));
	TestEqual(TEXT("Appends follow the incoming order (A)"), BaseA[1].AnimationName, FString(TEXT("Attack")));
	TestEqual(TEXT("Appends follow the incoming order (B)"), BaseB[1].AnimationName, FString(TEXT("Jump")));

	// Empty diff inputs produce empty results without error.
	const FAseDiffResult EmptyResult = FAsepriteStructuralDiff::DiffItems({}, {});
	TestEqual(TEXT("Empty diff: no renames"), EmptyResult.Renames.Num(), 0);
	TestEqual(TEXT("Empty diff: no removals"), EmptyResult.Removals.Num(), 0);
	TestEqual(TEXT("Empty diff: no adds"), EmptyResult.Added.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffPresetJoinTest,
	"Paper2DPlus.AsepriteStructuralDiff.NewLayersJoinDefaultAppearanceFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffPresetJoinTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	const FGuid HatGroupId = FGuid::NewGuid();

	// Size the array ONCE and assign by index: holding element references across further
	// AddDefaulted_GetRef calls dangles them the moment the array reallocates.
	TArray<FCharacterLayer> Layers;
	Layers.AddDefaulted(4);
	Layers[0].LayerName = TEXT("Body");
	Layers[1].LayerName = TEXT("StrawHat");
	Layers[1].ExclusiveGroupId = HatGroupId;
	Layers[2].LayerName = TEXT("TopHat");
	Layers[2].ExclusiveGroupId = HatGroupId;
	Layers[3].LayerName = TEXT("Cape");

	const FGuid BodyId = Layers[0].LayerId;
	const FGuid StrawHatId = Layers[1].LayerId;
	const FGuid TopHatId = Layers[2].LayerId;
	const FGuid CapeId = Layers[3].LayerId;

	// The default look already wears Body + StrawHat; a reimport adds TopHat (same Exclusive Group)
	// and Cape (ungrouped).
	FCharacterLayerAppearancePreset Preset;
	Preset.ActiveLayerIds = { BodyId, StrawHatId };

	TArray<FString> Refused;
	const int32 Joined = FAsepriteStructuralDiff::JoinNewLayersIntoPreset(
		Layers, { TEXT("TopHat"), TEXT("Cape") }, Preset, &Refused);

	TestEqual(TEXT("Only the ungrouped layer joins"), Joined, 1);
	TestEqual(TEXT("Preset gained exactly one entry"), Preset.ActiveLayerIds.Num(), 3);
	TestTrue(TEXT("Cape joined"), Preset.ActiveLayerIds.Contains(CapeId));
	TestFalse(TEXT("TopHat refused — its group already has an active member"),
		Preset.ActiveLayerIds.Contains(TopHatId));
	TestEqual(TEXT("The refusal is reported"), Refused.Num(), 1);
	if (Refused.Num() == 1)
	{
		TestEqual(TEXT("Refusal names the layer"), Refused[0], FString(TEXT("TopHat")));
	}

	// Two new members of one EMPTY group: the first joins, the second is refused against the
	// preset's live contents (never both).
	FCharacterLayerAppearancePreset EmptyGroupPreset;
	EmptyGroupPreset.ActiveLayerIds = { BodyId };
	TArray<FString> RefusedBoth;
	const int32 JoinedBoth = FAsepriteStructuralDiff::JoinNewLayersIntoPreset(
		Layers, { TEXT("StrawHat"), TEXT("TopHat") }, EmptyGroupPreset, &RefusedBoth);
	TestEqual(TEXT("Exactly one member of the group joins"), JoinedBoth, 1);
	TestEqual(TEXT("The second is refused"), RefusedBoth.Num(), 1);

	// Unknown names and already-present layers are silent no-ops.
	TArray<FString> RefusedNoop;
	const int32 JoinedNoop = FAsepriteStructuralDiff::JoinNewLayersIntoPreset(
		Layers, { TEXT("DoesNotExist"), TEXT("Body") }, Preset, &RefusedNoop);
	TestEqual(TEXT("Nothing joins"), JoinedNoop, 0);
	TestEqual(TEXT("Nothing is reported as refused"), RefusedNoop.Num(), 0);
	TestEqual(TEXT("Preset unchanged"), Preset.ActiveLayerIds.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffContentHashTest,
	"Paper2DPlus.AsepriteStructuralDiff.ContentHashPixelIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffContentHashTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	// Layer-buffer hash: identical pixels hash equal; one pixel change or a dim change differs.
	TArray<TArray<FColor>> BuffersA;
	BuffersA.Add(TArray<FColor>());
	BuffersA[0].Init(FColor::Red, 4);
	TArray<TArray<FColor>> BuffersB = BuffersA;

	const FString HashA = FAsepriteStructuralDiff::ComputeLayerBuffersHash(BuffersA, 2, 2);
	const FString HashB = FAsepriteStructuralDiff::ComputeLayerBuffersHash(BuffersB, 2, 2);
	TestTrue(TEXT("Hash is non-empty"), !HashA.IsEmpty());
	TestEqual(TEXT("Identical buffers hash equal"), HashA, HashB);

	BuffersB[0][3] = FColor::Blue;
	TestNotEqual(TEXT("A pixel change changes the hash"),
		HashA, FAsepriteStructuralDiff::ComputeLayerBuffersHash(BuffersB, 2, 2));
	TestNotEqual(TEXT("A dimension change changes the hash"),
		HashA, FAsepriteStructuralDiff::ComputeLayerBuffersHash(BuffersA, 4, 1));

	// Tag hash: pixel identity only — a retimed tag hashes equal; a range change differs.
	FAsepriteParsedData Data = AseDiffTest_ParsedFrames({ FColor::Red, FColor::Green, FColor::Blue });
	const FString TagHash01 = FAsepriteStructuralDiff::ComputeTagFramesHash(Data, AseDiffTest_Tag(TEXT("Walk"), 0, 1));

	FAsepriteParsedData Retimed = AseDiffTest_ParsedFrames({ FColor::Red, FColor::Green, FColor::Blue });
	Retimed.Frames[0].Duration = 250;
	Retimed.Frames[1].Duration = 40;
	TestEqual(TEXT("Durations do not enter the tag hash"),
		TagHash01, FAsepriteStructuralDiff::ComputeTagFramesHash(Retimed, AseDiffTest_Tag(TEXT("Walk"), 0, 1)));

	TestNotEqual(TEXT("A different frame range changes the tag hash"),
		TagHash01, FAsepriteStructuralDiff::ComputeTagFramesHash(Data, AseDiffTest_Tag(TEXT("Walk"), 0, 2)));

	// A reversed authored range hashes like its ascending form (identity is content, not direction).
	TestEqual(TEXT("Reversed From/To hashes like ascending"),
		TagHash01, FAsepriteStructuralDiff::ComputeTagFramesHash(Data, AseDiffTest_Tag(TEXT("Walk"), 1, 0)));
	return true;
}


// ────────────────────────────────────────────────────────────────────────────────
// TASK-189 U4 — the APPLY layer's pure seams. These decide whether a removal destroys
// authored work and whether a rename can rebind it, so each one is proved able to FAIL:
// every assertion below discriminates a specific production mutation.
// ────────────────────────────────────────────────────────────────────────────────

namespace Paper2DPlusAsepriteStructuralDiffTest
{
	FCharacterLayer AseDiffTest_Layer(const FString& LayerName)
	{
		FCharacterLayer Layer;
		Layer.LayerId = FGuid::NewGuid();
		Layer.LayerName = LayerName;
		return Layer;
	}

	FCharacterLayerAnimationOffset AseDiffTest_Offset(const FString& AnimName, const FVector2D& Px)
	{
		FCharacterLayerAnimationOffset Offset;
		Offset.AnimationName = AnimName;
		Offset.OffsetPx = Px;
		return Offset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffAuthoredLayerDataTest,
	"Paper2DPlus.AsepriteStructuralDiff.AuthoredLayerDataDetectsEveryCuratedSignal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffAuthoredLayerDataTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	TArray<FCharacterLayerAppearancePreset> NoPresets;

	// A layer straight out of an import carries nothing curated: it is safe to drop.
	FCharacterLayer Pristine = AseDiffTest_Layer(TEXT("Body"));
	Pristine.AnimationSprites.Add(AseDiffTest_Mapping(TEXT("Idle"), { TEXT("/Game/S.S") }));
	TestFalse(TEXT("Generated sprite mappings alone are NOT authored data"),
		FAsepriteStructuralDiff::HasAuthoredLayerData(Pristine, NoPresets));

	// Each curated signal INDEPENDENTLY makes the layer un-droppable.
	{
		FCharacterLayer Layer = Pristine;
		Layer.DefaultOffsetPx = FVector2D(3.0, 0.0);
		TestTrue(TEXT("A placement offset is authored data"),
			FAsepriteStructuralDiff::HasAuthoredLayerData(Layer, NoPresets));
	}
	{
		FCharacterLayer Layer = Pristine;
		Layer.AnimationOffsets.Add(AseDiffTest_Offset(TEXT("Idle"), FVector2D(1.0, 1.0)));
		TestTrue(TEXT("A per-animation offset is authored data"),
			FAsepriteStructuralDiff::HasAuthoredLayerData(Layer, NoPresets));
	}
	{
		FCharacterLayer Layer = Pristine;
		Layer.CompositionMode = ECharacterLayerCompositionMode::Replace;
		TestTrue(TEXT("A non-default composition mode is authored data"),
			FAsepriteStructuralDiff::HasAuthoredLayerData(Layer, NoPresets));
	}
	{
		FCharacterLayer Layer = Pristine;
		Layer.ExclusiveGroupId = FGuid::NewGuid();
		TestTrue(TEXT("An Exclusive Group binding is authored data"),
			FAsepriteStructuralDiff::HasAuthoredLayerData(Layer, NoPresets));
	}
	{
		// Preset membership is the cross-object signal: the layer itself looks pristine.
		FCharacterLayerAppearancePreset Preset;
		Preset.PresetId = FGuid::NewGuid();
		Preset.ActiveLayerIds.Add(Pristine.LayerId);
		TArray<FCharacterLayerAppearancePreset> Presets = { Preset };
		TestTrue(TEXT("Appearance-preset membership is authored data"),
			FAsepriteStructuralDiff::HasAuthoredLayerData(Pristine, Presets));

		// ...and it is keyed by the STABLE id, not the name: a preset naming a DIFFERENT layer
		// must not protect this one.
		Presets[0].ActiveLayerIds.Reset();
		Presets[0].ActiveLayerIds.Add(FGuid::NewGuid());
		TestFalse(TEXT("A preset holding some OTHER layer does not protect this one"),
			FAsepriteStructuralDiff::HasAuthoredLayerData(Pristine, Presets));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffAuthoredAnimationDataTest,
	"Paper2DPlus.AsepriteStructuralDiff.AuthoredAnimationDataIgnoresGeneratedRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffAuthoredAnimationDataTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	TArray<FCharacterLayer> Layers;
	Layers.Add(AseDiffTest_Layer(TEXT("Body")));
	Layers[0].AnimationSprites.Add(AseDiffTest_Mapping(TEXT("Idle"), { TEXT("/Game/S.S") }));
	Layers.Add(AseDiffTest_Layer(TEXT("Head")));

	TestFalse(TEXT("A generated sprite mapping is not authored data"),
		FAsepriteStructuralDiff::HasAuthoredAnimationData(Layers, TEXT("Idle")));
	TestFalse(TEXT("An animation nothing references is not authored data"),
		FAsepriteStructuralDiff::HasAuthoredAnimationData(Layers, TEXT("Walk")));

	// An offset authored on the SECOND layer still protects the animation — the scan is asset-wide.
	Layers[1].AnimationOffsets.Add(AseDiffTest_Offset(TEXT("Idle"), FVector2D(2.0, 0.0)));
	TestTrue(TEXT("A per-animation offset on ANY layer is authored data"),
		FAsepriteStructuralDiff::HasAuthoredAnimationData(Layers, TEXT("Idle")));
	TestTrue(TEXT("Authored-data matching is case-insensitive"),
		FAsepriteStructuralDiff::HasAuthoredAnimationData(Layers, TEXT("IDLE")));
	TestFalse(TEXT("A different animation is unaffected"),
		FAsepriteStructuralDiff::HasAuthoredAnimationData(Layers, TEXT("Run")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffRenameAnimationOnLayersTest,
	"Paper2DPlus.AsepriteStructuralDiff.RenameAnimationRekeysEveryLayerRowAndRefusesCollisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffRenameAnimationOnLayersTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	TArray<FCharacterLayer> Layers;
	Layers.Add(AseDiffTest_Layer(TEXT("Body")));
	Layers[0].AnimationSprites.Add(AseDiffTest_Mapping(TEXT("Idle"), { TEXT("/Game/A.A") }));
	Layers[0].AnimationSprites.Add(AseDiffTest_Mapping(TEXT("Walk"), { TEXT("/Game/B.B") }));
	Layers[0].AnimationOffsets.Add(AseDiffTest_Offset(TEXT("idle"), FVector2D(5.0, 7.0)));
	Layers.Add(AseDiffTest_Layer(TEXT("Head")));
	Layers[1].AnimationSprites.Add(AseDiffTest_Mapping(TEXT("IDLE"), { TEXT("/Game/C.C") }));

	// Three rows across two layers, matched case-insensitively, rewritten with the EXACT new spelling.
	const int32 Renamed = FAsepriteStructuralDiff::RenameAnimationOnLayers(
		Layers, TEXT("Idle"), TEXT("Stand"));
	TestEqual(TEXT("Every row referencing the old name is re-keyed"), Renamed, 3);
	TestEqual(TEXT("Layer 0 mapping took the new spelling"),
		Layers[0].AnimationSprites[0].AnimationName, FString(TEXT("Stand")));
	TestEqual(TEXT("The per-animation offset followed the name"),
		Layers[0].AnimationOffsets[0].AnimationName, FString(TEXT("Stand")));
	TestEqual(TEXT("Layer 1's differently-cased row followed too"),
		Layers[1].AnimationSprites[0].AnimationName, FString(TEXT("Stand")));
	// The untouched animation keeps BOTH its name and its sprites — a rename is not a rebuild.
	TestEqual(TEXT("An unrelated animation keeps its name"),
		Layers[0].AnimationSprites[1].AnimationName, FString(TEXT("Walk")));
	TestEqual(TEXT("An unrelated animation keeps its sprites"),
		Layers[0].AnimationSprites[1].Sprites.Num(), 1);

	// A layer that already owns the NEW name refuses: two rows claiming one animation would
	// silently merge their authored data.
	TArray<FCharacterLayer> Colliding;
	Colliding.Add(AseDiffTest_Layer(TEXT("Body")));
	Colliding[0].AnimationSprites.Add(AseDiffTest_Mapping(TEXT("Idle"), { TEXT("/Game/A.A") }));
	Colliding[0].AnimationSprites.Add(AseDiffTest_Mapping(TEXT("Stand"), { TEXT("/Game/D.D") }));
	TArray<FString> Collided;
	const int32 RefusedCount = FAsepriteStructuralDiff::RenameAnimationOnLayers(
		Colliding, TEXT("Idle"), TEXT("Stand"), &Collided);
	TestEqual(TEXT("A colliding layer re-keys nothing"), RefusedCount, 0);
	TestEqual(TEXT("The colliding layer is reported by name"), Collided.Num(), 1);
	if (Collided.Num() == 1)
	{
		TestEqual(TEXT("Collision names the layer"), Collided[0], FString(TEXT("Body")));
	}
	TestEqual(TEXT("The colliding layer's original row is untouched"),
		Colliding[0].AnimationSprites[0].AnimationName, FString(TEXT("Idle")));

	// Degenerate inputs are no-ops, not corruption.
	TestEqual(TEXT("Renaming to the same name is a no-op"),
		FAsepriteStructuralDiff::RenameAnimationOnLayers(Layers, TEXT("Stand"), TEXT("Stand")), 0);
	TestEqual(TEXT("An empty new name is refused"),
		FAsepriteStructuralDiff::RenameAnimationOnLayers(Layers, TEXT("Stand"), FString()), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffRemovalSeamsTest,
	"Paper2DPlus.AsepriteStructuralDiff.RemovalDropsGeneratedRowsAndCleansPresetMembership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffRemovalSeamsTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	TArray<FCharacterLayer> Layers;
	Layers.Add(AseDiffTest_Layer(TEXT("Body")));
	Layers[0].AnimationSprites.Add(AseDiffTest_Mapping(TEXT("Idle"), { TEXT("/Game/A.A") }));
	Layers[0].AnimationSprites.Add(AseDiffTest_Mapping(TEXT("Walk"), { TEXT("/Game/B.B") }));
	Layers[0].AnimationOffsets.Add(AseDiffTest_Offset(TEXT("Idle"), FVector2D(5.0, 7.0)));
	Layers.Add(AseDiffTest_Layer(TEXT("Head")));
	Layers[1].AnimationSprites.Add(AseDiffTest_Mapping(TEXT("idle"), { TEXT("/Game/C.C") }));

	const int32 Removed = FAsepriteStructuralDiff::RemoveAnimationFromLayers(Layers, TEXT("Idle"));
	TestEqual(TEXT("The generated mapping is dropped from every layer"), Removed, 2);
	TestEqual(TEXT("Layer 0 keeps its other animation"), Layers[0].AnimationSprites.Num(), 1);
	TestEqual(TEXT("Layer 1 has no mappings left"), Layers[1].AnimationSprites.Num(), 0);
	// The AUTHORED offset survives: removal drops generated rows only, and the caller only reaches
	// it after HasAuthoredAnimationData said the animation was unedited.
	TestEqual(TEXT("An authored offset is NOT collateral damage"), Layers[0].AnimationOffsets.Num(), 1);

	// Layer removal also cleans preset membership, so no preset can keep a dangling LayerId.
	const FGuid BodyId = Layers[0].LayerId;
	const FGuid HeadId = Layers[1].LayerId;
	FCharacterLayerAppearancePreset Preset;
	Preset.PresetId = FGuid::NewGuid();
	Preset.ActiveLayerIds.Add(BodyId);
	Preset.ActiveLayerIds.Add(HeadId);
	TArray<FCharacterLayerAppearancePreset> Presets = { Preset };

	FGuid RemovedId;
	TestTrue(TEXT("The named layer is removed"),
		FAsepriteStructuralDiff::RemoveLayerByName(Layers, Presets, TEXT("BODY"), &RemovedId));
	TestEqual(TEXT("Removal reports the STABLE id, not the name"), RemovedId, BodyId);
	TestEqual(TEXT("Only that layer went"), Layers.Num(), 1);
	TestEqual(TEXT("The surviving layer is the other one"), Layers[0].LayerName, FString(TEXT("Head")));
	TestFalse(TEXT("The preset no longer holds the removed layer"),
		Presets[0].ActiveLayerIds.Contains(BodyId));
	TestTrue(TEXT("The preset still holds the surviving layer"),
		Presets[0].ActiveLayerIds.Contains(HeadId));

	TestFalse(TEXT("Removing a name nothing owns changes nothing"),
		FAsepriteStructuralDiff::RemoveLayerByName(Layers, Presets, TEXT("Nope")));
	TestEqual(TEXT("...and leaves the layer list alone"), Layers.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffApplyReportTest,
	"Paper2DPlus.AsepriteStructuralDiff.ApplyReportStaysSilentUntilSomethingHappens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffApplyReportTest::RunTest(const FString& Parameters)
{
	FAseDiffApplyReport Report;
	// A pure-refresh reimport must not raise a notification or dirty anything (R13).
	TestTrue(TEXT("A fresh report is empty"), Report.IsEmpty());
	TestTrue(TEXT("An empty report summarises to nothing"), Report.ToSummaryText().IsEmpty());

	// Merge counters alone are NOT an event: refreshing art on every save is the normal case.
	Report.MappingsRefreshed = 12;
	TestTrue(TEXT("Refresh counts alone leave the report empty"), Report.IsEmpty());

	FAseDiffRename Rename;
	Rename.OldName = TEXT("Body");
	Rename.NewName = TEXT("Torso");
	Report.LayerRenames.Add(Rename);
	Report.OrphanedAssets.Add(TEXT("/Game/Art/Old_Sheet"));
	TestFalse(TEXT("A rename makes the report non-empty"), Report.IsEmpty());

	const FString Summary = Report.ToSummaryText();
	TestTrue(TEXT("The summary counts the rename"), Summary.Contains(TEXT("1 rename")));
	TestTrue(TEXT("The summary counts the orphan"), Summary.Contains(TEXT("1 orphan")));

	TArray<FString> Lines;
	Report.AppendDetailLines(Lines);
	TestEqual(TEXT("One line per decision"), Lines.Num(), 2);
	TestTrue(TEXT("The rename line names both spellings"),
		Lines[0].Contains(TEXT("Body")) && Lines[0].Contains(TEXT("Torso")));
	TestTrue(TEXT("The orphan line says nothing was deleted"),
		Lines[1].Contains(TEXT("nothing was deleted")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseDiffApplyOrderTest,
	"Paper2DPlus.AsepriteStructuralDiff.RenameMustApplyBeforeTheCreationPassWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseDiffApplyOrderTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAsepriteStructuralDiffTest;

	// THE ordering rule the diff-apply layer turns on. Every creator in the pipeline is
	// FindOrCreate-IN-PLACE, so a rename has to be decided BEFORE the creation pass writes. Both
	// orders are played out against the same starting asset through the SAME production decision,
	// so the failure the rule prevents is executed rather than described.
	auto MakeLayers = []() -> TArray<FCharacterLayer>
	{
		TArray<FCharacterLayer> Layers;
		FCharacterLayer& Body = Layers.AddDefaulted_GetRef();
		Body.LayerId = FGuid::NewGuid();
		Body.LayerName = TEXT("Body");
		Body.DefaultOffsetPx = FVector2D(3.0f, 5.0f); // authored placement, must survive a rename
		return Layers;
	};

	// The rename the reimport resolved: renamed in Aseprite, pixels unchanged, so it pairs.
	const FAseDiffResult Diff = FAsepriteStructuralDiff::DiffItems(
		{ AseDiffTest_Old(TEXT("Body"), TEXT("hash-1")) },
		{ AseDiffTest_New(TEXT("Torso"), TEXT("hash-1")) });
	if (!TestEqual(TEXT("the fixture's rename pairs"), Diff.Renames.Num(), 1))
	{
		return false;
	}

	// ---- CORRECT ORDER: decide while the asset still holds only the OLD name.
	{
		TArray<FCharacterLayer> Layers = MakeLayers();
		const FGuid OriginalId = Layers[0].LayerId;

		int32 Index = INDEX_NONE;
		const EAseLayerRenamePlan Plan = FAsepriteStructuralDiff::PlanLayerRename(
			Layers, Diff.Renames[0].OldName, Diff.Renames[0].NewName, &Index);

		TestTrue(TEXT("the rename is applicable before the creation pass writes"),
			Plan == EAseLayerRenamePlan::Applicable);
		if (TestEqual(TEXT("it resolves the layer to rebind"), Index, 0))
		{
			Layers[Index].LayerName = Diff.Renames[0].NewName; // what the apply layer then writes
		}

		TestEqual(TEXT("applying first leaves exactly one layer"), Layers.Num(), 1);
		TestEqual(TEXT("it carries the new name"), Layers[0].LayerName, FString(TEXT("Torso")));
		TestTrue(TEXT("the stable id never moved, so presets and placement follow"),
			Layers[0].LayerId == OriginalId);
		TestTrue(TEXT("authored placement survived the rename"),
			Layers[0].DefaultOffsetPx.Equals(FVector2D(3.0f, 5.0f)));
	}

	// ---- WRONG ORDER: the creation pass minted "Torso" first. Production must now REFUSE the
	// rename rather than merge two layers, which is precisely the degradation the rule prevents.
	{
		TArray<FCharacterLayer> Layers = MakeLayers();
		FCharacterLayer& Created = Layers.AddDefaulted_GetRef();
		Created.LayerId = FGuid::NewGuid();
		Created.LayerName = TEXT("Torso");

		int32 Index = INDEX_NONE;
		const EAseLayerRenamePlan Plan = FAsepriteStructuralDiff::PlanLayerRename(
			Layers, Diff.Renames[0].OldName, Diff.Renames[0].NewName, &Index);

		TestTrue(TEXT("deciding too late REFUSES the rename"),
			Plan == EAseLayerRenamePlan::NewNameAlreadyOwned);
		TestEqual(TEXT("a refused rename reports no layer to rebind"), Index, (int32)INDEX_NONE);

		// The consequence the caller then has to live with: the old layer strands beside the new one,
		// carrying authored placement the new layer does not have.
		TestEqual(TEXT("the asset is left holding both"), Layers.Num(), 2);
		TestTrue(TEXT("the authored placement is stranded on the dead name"),
			Layers[0].DefaultOffsetPx.Equals(FVector2D(3.0f, 5.0f))
				&& Layers[1].DefaultOffsetPx.IsNearlyZero());
	}

	// A stamp naming a layer this asset never had is not a refusal — the ordinary add path covers it,
	// and conflating the two would report a degraded rename on every genuinely new layer.
	{
		TArray<FCharacterLayer> Layers = MakeLayers();
		TestTrue(TEXT("an unknown old name is reported distinctly, not as a collision"),
			FAsepriteStructuralDiff::PlanLayerRename(Layers, TEXT("Ghost"), TEXT("Torso"))
				== EAseLayerRenamePlan::OldNameNotFound);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
