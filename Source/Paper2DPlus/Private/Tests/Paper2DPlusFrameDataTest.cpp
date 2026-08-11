// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "Paper2DPlusFrameData.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusMoveTransition.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"

/** TASK-15 frame-data compute + CSV/JSON export tests (worldless). Helpers are FrameData_-prefixed
 *  per the unity-build file-unique-name rule. */

namespace
{
	/** Build a flipbook with the given per-key-frame FrameRun ticks at a fixed FPS, owned by Owner. */
	UPaperFlipbook* FrameData_MakeFlipbook(UObject* Owner, const TArray<int32>& FrameRuns, float Fps)
	{
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>(Owner);
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = Fps;
		Mutator.KeyFrames.Empty();
		for (int32 Run : FrameRuns)
		{
			FPaperFlipbookKeyFrame KF;
			KF.FrameRun = FMath::Max(Run, 1);
			KF.Sprite = NewObject<UPaperSprite>(FB);
			Mutator.KeyFrames.Add(KF);
		}
		return FB;
	}

	/** Add an attack hitbox to a frame. */
	FHitboxData FrameData_Attack(int32 X, int32 Y, int32 W, int32 H, int32 Damage, int32 Knockback)
	{
		FHitboxData HB;
		HB.Type = EHitboxType::Attack;
		HB.X = X; HB.Y = Y; HB.Width = W; HB.Height = H;
		HB.Damage = Damage; HB.Knockback = Knockback;
		return HB;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeMoveFrameData — single move with known hitboxes/durations/root-motion/phase
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameDataComputeMove,
	"Paper2DPlus.FrameData.Compute.SingleMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameDataComputeMove::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Slash");
	// 4 key frames; total ticks = 1+2+3+1 = 7; at 70 fps => 100 ms.
	Anim.Identity.Flipbook = FrameData_MakeFlipbook(Asset, { 1, 2, 3, 1 }, 70.0f);

	Anim.CombatData.Frames.SetNum(4);
	// Frame 1: attack 5 dmg / 10 kb. Frame 2: attack 9 dmg / 4 kb (max dmg), reach via geometry.
	Anim.CombatData.Frames[1].Hitboxes.Add(FrameData_Attack(0, 0, 10, 10, 5, 10));
	Anim.CombatData.Frames[2].Hitboxes.Add(FrameData_Attack(20, 0, 10, 10, 9, 4)); // far corner at x=30
	// Frame 0 has a hurtbox only -> NOT active.
	{
		FHitboxData Hurt; Hurt.Type = EHitboxType::Hurtbox; Hurt.Width = 8; Hurt.Height = 8;
		Anim.CombatData.Frames[0].Hitboxes.Add(Hurt);
	}
	// I-frames on frames 0 and 3.
	Anim.CombatData.Frames[0].bInvulnerable = true;
	Anim.CombatData.Frames[3].bInvulnerable = true;

	// Root motion authored.
	Anim.MotionData.RootMotion.SetNum(4);
	Anim.MotionData.RootMotion[1].Position = FVector2D(5.0, 0.0);

	// Phase: derived from the flipbook's OWN EditorMeta.PhaseTag (leaf name, case-insensitive).
	const FGameplayTag StartupTag = FGameplayTag::RequestGameplayTag(FName("Paper2DPlus.Phase.Startup"), /*ErrorIfNotFound*/ false);
	if (StartupTag.IsValid())
	{
		Anim.EditorMeta.PhaseTag = StartupTag;
	}

	Asset->Flipbooks.Add(Anim);

	FPaper2DPlusMoveFrameData Data;
	const bool bOk = FPaper2DPlusFrameData::ComputeMoveFrameData(Asset, TEXT("slash"), Data); // case-insensitive
	TestTrue(TEXT("ComputeMoveFrameData should find the move"), bOk);

	TestEqual(TEXT("TotalKeyFrames"), Data.TotalKeyFrames, 4);
	TestEqual(TEXT("TotalDurationFrames (ticks)"), Data.TotalDurationFrames, 7);
	TestEqual(TEXT("TotalDurationMs"), FMath::RoundToInt(Data.TotalDurationMs), 100);
	TestEqual(TEXT("ActiveFrames (frames with an attack hitbox)"), Data.ActiveFrames, 2);
	TestEqual(TEXT("IFrameCount"), Data.IFrameCount, 2);
	TestEqual(TEXT("MaxDamage"), Data.MaxDamage, 9.f);
	TestEqual(TEXT("MaxKnockback"), Data.MaxKnockback, 10.f);
	// Furthest attack corner is (30,10) -> sqrt(900+100) ~= 31.62
	TestTrue(TEXT("MaxReach ~= 31.62"), FMath::IsNearlyEqual(Data.MaxReach, FMath::Sqrt(1000.0f), 0.1f));
	TestTrue(TEXT("bHasRootMotion"), Data.bHasRootMotion);
	if (StartupTag.IsValid())
	{
		TestEqual(TEXT("Phase == Startup (derived from the flipbook's own PhaseTag)"), Data.Phase, EAnimationPhase::Startup);
	}
	else
	{
		AddInfo(TEXT("Paper2DPlus.Phase.Startup not registered in this run — skipping the Phase assertion (expected only when DefaultGameplayTags.ini is absent)."));
	}
	TestEqual(TEXT("CancelWindowCount placeholder is 0"), Data.CancelWindowCount, 0);

	// Unknown move returns false + default.
	FPaper2DPlusMoveFrameData Missing;
	TestFalse(TEXT("Unknown move returns false"), FPaper2DPlusFrameData::ComputeMoveFrameData(Asset, TEXT("Nope"), Missing));
	TestEqual(TEXT("Missing row is default (0 key frames)"), Missing.TotalKeyFrames, 0);

	// Null asset returns false.
	FPaper2DPlusMoveFrameData NullRow;
	TestFalse(TEXT("Null asset returns false"), FPaper2DPlusFrameData::ComputeMoveFrameData(nullptr, TEXT("Slash"), NullRow));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeAllMoveFrameData — row count + BP query parity
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameDataComputeAll,
	"Paper2DPlus.FrameData.Compute.AllMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameDataComputeAll::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	for (int32 i = 0; i < 3; ++i)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = FString::Printf(TEXT("Move%d"), i);
		Anim.Identity.Flipbook = FrameData_MakeFlipbook(Asset, { 1, 1 }, 60.0f);
		Anim.CombatData.Frames.SetNum(2);
		Asset->Flipbooks.Add(Anim);
	}

	TArray<FPaper2DPlusMoveFrameData> All;
	FPaper2DPlusFrameData::ComputeAllMoveFrameData(Asset, All);
	TestEqual(TEXT("Row count == flipbook count"), All.Num(), 3);
	TestEqual(TEXT("First row name preserves order"), All[0].FlipbookName, FString(TEXT("Move0")));

	// BP library wrappers funnel the same compute.
	TArray<FPaper2DPlusMoveFrameData> ViaBp = UPaper2DPlusBlueprintLibrary::GetAllMoveFrameData(Asset);
	TestEqual(TEXT("BP GetAllMoveFrameData row count matches"), ViaBp.Num(), 3);

	FPaper2DPlusMoveFrameData One;
	TestTrue(TEXT("BP GetMoveFrameData finds Move1"), UPaper2DPlusBlueprintLibrary::GetMoveFrameData(Asset, TEXT("Move1"), One));
	TestEqual(TEXT("BP single-row name"), One.FlipbookName, FString(TEXT("Move1")));

	// Null asset -> empty.
	TArray<FPaper2DPlusMoveFrameData> Empty;
	FPaper2DPlusFrameData::ComputeAllMoveFrameData(nullptr, Empty);
	TestEqual(TEXT("Null asset yields no rows"), Empty.Num(), 0);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// CSV export — header + one line per move
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameDataExportCsv,
	"Paper2DPlus.FrameData.Export.Csv",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameDataExportCsv::RunTest(const FString& Parameters)
{
	TArray<FPaper2DPlusMoveFrameData> Rows;
	{
		FPaper2DPlusMoveFrameData R;
		R.FlipbookName = TEXT("Jab");
		R.TotalKeyFrames = 5;
		R.TotalDurationFrames = 7;
		R.TotalDurationMs = 116.6f;
		R.ActiveFrames = 2;
		R.IFrameCount = 0;
		R.MaxDamage = 12;
		R.MaxKnockback = 3;
		R.MaxReach = 40.0f;
		R.bHasRootMotion = true;
		R.Phase = EAnimationPhase::Startup;
		Rows.Add(R);
	}
	{
		FPaper2DPlusMoveFrameData R;
		R.FlipbookName = TEXT("Idle");
		Rows.Add(R);
	}

	const FString Csv = FPaper2DPlusFrameData::ExportFrameDataToCsv(Rows);

	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines, /*bCullEmpty=*/true);

	TestEqual(TEXT("CSV has header + 2 data lines"), Lines.Num(), 3);
	TestEqual(TEXT("Header is the canonical column order"), Lines[0], FPaper2DPlusFrameData::GetCsvHeader());
	TestTrue(TEXT("First data line carries the move name"), Lines[1].Contains(TEXT("\"Jab\"")));
	TestTrue(TEXT("First data line carries MaxDamage"), Lines[1].Contains(TEXT(",12,")));
	TestTrue(TEXT("First data line carries Phase"), Lines[1].Contains(TEXT("\"Startup\"")));
	TestTrue(TEXT("First data line carries HasRootMotion"), Lines[1].Contains(TEXT(",true,")));

	// Column count of a data row matches header column count. 13 since the legacy-cleanup dropped
	// the PhaseGroupName column; AnimationTags stays LAST (TASK-108 U6).
	TArray<FString> HeaderCols; Lines[0].ParseIntoArray(HeaderCols, TEXT(","), /*bCullEmpty=*/false);
	TestEqual(TEXT("Header has 13 columns"), HeaderCols.Num(), 13);
	TestEqual(TEXT("AnimationTags is the LAST header column"), HeaderCols.Last(), FString(TEXT("AnimationTags")));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON export — parses + carries expected fields
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameDataExportJson,
	"Paper2DPlus.FrameData.Export.Json",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameDataExportJson::RunTest(const FString& Parameters)
{
	TArray<FPaper2DPlusMoveFrameData> Rows;
	FPaper2DPlusMoveFrameData R;
	R.FlipbookName = TEXT("Slash");
	R.TotalKeyFrames = 4;
	R.TotalDurationFrames = 7;
	R.TotalDurationMs = 100.0f;
	R.ActiveFrames = 2;
	R.IFrameCount = 2;
	R.MaxDamage = 9;
	R.MaxKnockback = 10;
	R.MaxReach = 31.6f;
	R.bHasRootMotion = true;
	R.Phase = EAnimationPhase::Active;
	R.CancelWindowCount = 0;
	Rows.Add(R);

	const FString Json = FPaper2DPlusFrameData::ExportFrameDataToJson(Rows);

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid();
	TestTrue(TEXT("JSON parses"), bParsed);
	if (!bParsed) return false;

	const TArray<TSharedPtr<FJsonValue>>* Moves = nullptr;
	TestTrue(TEXT("JSON has a Moves array"), Root->TryGetArrayField(TEXT("Moves"), Moves));
	if (!Moves || Moves->Num() != 1)
	{
		AddError(TEXT("Expected exactly one move in JSON"));
		return false;
	}

	const TSharedPtr<FJsonObject> Move = (*Moves)[0]->AsObject();
	TestTrue(TEXT("Move object is valid"), Move.IsValid());
	if (!Move.IsValid()) return false;

	TestEqual(TEXT("JSON FlipbookName"), Move->GetStringField(TEXT("FlipbookName")), FString(TEXT("Slash")));
	TestEqual(TEXT("JSON MaxDamage"), (int32)Move->GetNumberField(TEXT("MaxDamage")), 9);
	TestEqual(TEXT("JSON MaxKnockback"), (int32)Move->GetNumberField(TEXT("MaxKnockback")), 10);
	TestEqual(TEXT("JSON ActiveFrames"), (int32)Move->GetNumberField(TEXT("ActiveFrames")), 2);
	TestEqual(TEXT("JSON IFrameCount"), (int32)Move->GetNumberField(TEXT("IFrameCount")), 2);
	TestEqual(TEXT("JSON TotalKeyFrames"), (int32)Move->GetNumberField(TEXT("TotalKeyFrames")), 4);
	TestEqual(TEXT("JSON Phase"), Move->GetStringField(TEXT("Phase")), FString(TEXT("Active")));
	TestTrue(TEXT("JSON HasRootMotion"), Move->GetBoolField(TEXT("HasRootMotion")));
	TestEqual(TEXT("JSON CancelWindowCount"), (int32)Move->GetNumberField(TEXT("CancelWindowCount")), 0);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TASK-108 U6 (R9): the AnimationTags column — effective (own + inherited) tags, semicolon-separated,
// appended as the LAST CSV column + a JSON field; untagged move -> empty cell.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameDataExportAnimationTags,
	"Paper2DPlus.FrameData.Export.AnimationTagsColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameDataExportAnimationTags::RunTest(const FString& Parameters)
{
	const FGameplayTag HeavyTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Paper2DPlus.Animation.Combat.Heavy")), false);
	const FGameplayTag AirborneTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Paper2DPlus.Animation.Context.Airborne")), false);
	if (!TestTrue(TEXT("Native taxonomy tags are registered"), HeavyTag.IsValid() && AirborneTag.IsValid()))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	// Root "Opener" carries an OWN tag and confirm-chains into "Finisher" (which has its own second
	// tag) — Finisher's EFFECTIVE tags = own (Airborne) + chain-inherited (Heavy). "Idle" is untagged.
	auto AddMove = [Asset](const FString& Name) -> FFlipbookProfileEntry&
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = Name;
		Anim.Identity.Flipbook = FrameData_MakeFlipbook(Asset, { 1, 1 }, 60.0f);
		Anim.CombatData.Frames.SetNum(2);
		return Asset->Flipbooks[Asset->Flipbooks.Add(Anim)];
	};
	FFlipbookProfileEntry& Opener = AddMove(TEXT("Opener"));
	Opener.EditorMeta.AnimationTags.AddTag(HeavyTag);
	Opener.TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Finisher")));
	FFlipbookProfileEntry& Finisher = AddMove(TEXT("Finisher"));
	Finisher.EditorMeta.AnimationTags.AddTag(AirborneTag);
	AddMove(TEXT("Idle"));

	// Root inheritance is defined only inside an exact animation group. Reuse Airborne as the group
	// tag so the effective set still contains exactly two unique tags and Heavy remains inheritance-only.
	FFlipbookTagMapping& AnimationGroup = Asset->TagMappings.FindOrAdd(AirborneTag);
	FFlipbookTagMappingEntry RootEntry(TEXT("Opener"));
	RootEntry.bIsChainStart = true;
	AnimationGroup.Entries.Add(RootEntry);
	AnimationGroup.Entries.Add(FFlipbookTagMappingEntry(TEXT("Finisher")));

	TArray<FPaper2DPlusMoveFrameData> Rows;
	FPaper2DPlusFrameData::ComputeAllMoveFrameData(Asset, Rows);
	TestEqual(TEXT("Three rows"), Rows.Num(), 3);
	if (Rows.Num() != 3)
	{
		return false;
	}

	// Row-level: own + inherited joined with ';', full tag names; untagged -> empty.
	TestTrue(TEXT("Finisher carries its OWN tag"), Rows[1].AnimationTags.Contains(TEXT("Paper2DPlus.Animation.Context.Airborne")));
	TestTrue(TEXT("Finisher carries the chain-INHERITED root tag"), Rows[1].AnimationTags.Contains(TEXT("Paper2DPlus.Animation.Combat.Heavy")));
	TestTrue(TEXT("Two tags are semicolon-separated"), Rows[1].AnimationTags.Contains(TEXT(";")));
	TestEqual(TEXT("Untagged move -> empty tags string"), Rows[2].AnimationTags, FString());

	// CSV: the tags land in the LAST cell of the row (existing indices unshifted).
	const FString Csv = FPaper2DPlusFrameData::ExportFrameDataToCsv(Rows);
	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines, /*bCullEmpty=*/true);
	TestEqual(TEXT("CSV has header + 3 data lines"), Lines.Num(), 4);
	if (Lines.Num() == 4)
	{
		TestTrue(TEXT("Finisher's CSV line ENDS with the quoted tags cell"),
			Lines[2].EndsWith(FString::Printf(TEXT(",\"%s\""), *Rows[1].AnimationTags)));
		TestTrue(TEXT("Idle's CSV line ends with an EMPTY quoted cell"), Lines[3].EndsWith(TEXT(",\"\"")));
	}

	// JSON: the field is present with the same joined value.
	const FString Json = FPaper2DPlusFrameData::ExportFrameDataToJson(Rows);
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid();
	TestTrue(TEXT("JSON parses"), bParsed);
	if (bParsed)
	{
		const TArray<TSharedPtr<FJsonValue>>* Moves = nullptr;
		if (Root->TryGetArrayField(TEXT("Moves"), Moves) && Moves && Moves->Num() == 3)
		{
			TestEqual(TEXT("JSON AnimationTags matches the row"),
				(*Moves)[1]->AsObject()->GetStringField(TEXT("AnimationTags")), Rows[1].AnimationTags);
			TestEqual(TEXT("JSON untagged move -> empty string"),
				(*Moves)[2]->AsObject()->GetStringField(TEXT("AnimationTags")), FString());
		}
		else
		{
			AddError(TEXT("Expected exactly three moves in JSON"));
		}
	}

	return true;
}

#endif // WITH_EDITOR
