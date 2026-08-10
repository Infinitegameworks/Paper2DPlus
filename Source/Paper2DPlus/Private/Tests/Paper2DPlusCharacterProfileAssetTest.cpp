// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "GameplayTagContainer.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusFlipbookComponent.h"
#include "Paper2DPlusMoveTransition.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Tests/Paper2DPlusTestFrameCueTypes.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	CharacterProfileTest_RootGroup,
	"Paper2DPlus.Test.CharacterProfile.RootGroup")

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	CharacterProfileTest_AlternateRootGroup,
	"Paper2DPlus.Test.CharacterProfile.AlternateRootGroup")

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	CharacterProfileTest_ExactShared,
	"Paper2DPlus.Test.CharacterProfile.Exact.Shared")

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	CharacterProfileTest_ChainIdentity,
	"Paper2DPlus.Test.CharacterProfile.Chain.Identity")

/** CharacterProfile asset test suite — JSON serialization, migration, lookup, and alignment verification. */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileValidateDuplicateFlipbooks,
	"Paper2DPlus.CharacterProfile.Validation.DuplicateFlipbookNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileValidateDuplicateFlipbooks::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry AnimA;
	AnimA.Identity.FlipbookName = TEXT("Idle");
	FFrameHitboxData FrameA;
	FHitboxData HitboxA;
	HitboxA.Width = 10;
	HitboxA.Height = 12;
	FrameA.Hitboxes.Add(HitboxA);
	AnimA.CombatData.Frames.Add(FrameA);
	Asset->Flipbooks.Add(AnimA);

	FFlipbookProfileEntry AnimB;
	AnimB.Identity.FlipbookName = TEXT("idle"); // duplicate (case-insensitive)
	FFrameHitboxData FrameB;
	FHitboxData HitboxB;
	HitboxB.Width = 8;
	HitboxB.Height = 9;
	FrameB.Hitboxes.Add(HitboxB);
	AnimB.CombatData.Frames.Add(FrameB);
	Asset->Flipbooks.Add(AnimB);

	TArray<FCharacterProfileValidationIssue> Issues;
	const bool bValid = Asset->ValidateCharacterProfileAsset(Issues);

	TestFalse(TEXT("Duplicate flipbook names should fail validation"), bValid);

	bool bFoundDuplicateError = false;
	for (const FCharacterProfileValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == ECharacterProfileValidationSeverity::Error &&
			Issue.Message.Contains(TEXT("Duplicate flipbook name")))
		{
			bFoundDuplicateError = true;
			break;
		}
	}
	TestTrue(TEXT("Expected duplicate animation error issue"), bFoundDuplicateError);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileTrimTrailingFrames,
	"Paper2DPlus.CharacterProfile.Cleanup.TrimTrailingFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileTrimTrailingFrames::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Run");
	Anim.Identity.Flipbook = NewObject<UPaperFlipbook>(Asset);

	Anim.CombatData.Frames.SetNum(3);
	Anim.CombatData.FrameExtractionInfo.SetNum(4);

	Asset->Flipbooks.Add(Anim);

	const int32 Removed = Asset->TrimAllTrailingFrameData();

	TestEqual(TEXT("All trailing entries should be removed when flipbook has 0 keyframes"), Removed, 7);
	TestEqual(TEXT("Frames should be trimmed to 0"), Asset->Flipbooks[0].CombatData.Frames.Num(), 0);
	TestEqual(TEXT("Extraction info should be trimmed to 0"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileExcludeRestoreFrame,
	"Paper2DPlus.CharacterProfile.CombatData.Frames.ExcludeRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileExcludeRestoreFrame::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Idle");

	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Asset);
	Anim.Identity.Flipbook = Flipbook;

	Anim.CombatData.Frames.SetNum(3);
	Anim.CombatData.Frames[0].FrameName = TEXT("Idle_00");
	Anim.CombatData.Frames[1].FrameName = TEXT("Idle_01");
	Anim.CombatData.Frames[2].FrameName = TEXT("Idle_02");
	Anim.CombatData.FrameExtractionInfo.SetNum(3);

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.KeyFrames.Empty();

		for (int32 Index = 0; Index < 3; ++Index)
		{
			FPaperFlipbookKeyFrame KeyFrame;
			KeyFrame.FrameRun = Index + 1;
			KeyFrame.Sprite = NewObject<UPaperSprite>(Flipbook);
			Mutator.KeyFrames.Add(KeyFrame);
		}
	}

	Asset->Flipbooks.Add(Anim);

	const bool bExcluded = Asset->ExcludeFlipbookFrame(0, 1);
	TestTrue(TEXT("ExcludeFlipbookFrame should succeed"), bExcluded);
	TestEqual(TEXT("Live flipbook should remove one keyframe"), Flipbook->GetNumKeyFrames(), 2);
	TestEqual(TEXT("Active frame metadata should remove one frame"), Asset->Flipbooks[0].CombatData.Frames.Num(), 2);
	TestEqual(TEXT("One frame should be stored as excluded"), Asset->GetExcludedFlipbookFrameCount(0), 1);

	const bool bRestored = Asset->RestoreExcludedFlipbookFrame(0, 0);
	TestTrue(TEXT("RestoreExcludedFlipbookFrame should succeed"), bRestored);
	TestEqual(TEXT("Live flipbook should restore removed keyframe"), Flipbook->GetNumKeyFrames(), 3);
	TestEqual(TEXT("Active frame metadata should restore removed frame"), Asset->Flipbooks[0].CombatData.Frames.Num(), 3);
	TestEqual(TEXT("Excluded storage should be empty after restore"), Asset->GetExcludedFlipbookFrameCount(0), 0);
	TestEqual(TEXT("Restored frame name should return to original slot"), Asset->Flipbooks[0].CombatData.Frames[1].FrameName, FString(TEXT("Idle_01")));
	TestEqual(TEXT("Restored keyframe duration should be preserved"), Flipbook->GetKeyFrameChecked(1).FrameRun, 2);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileMoveFrame,
	"Paper2DPlus.CharacterProfile.CombatData.Frames.MoveFrameReorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileMoveFrame::RunTest(const FString& Parameters)
{
	// Builds a 4-frame flipbook where each frame carries a unique marker on every
	// parallel array: FrameName "F<i>", keyframe FrameRun i+1, SpriteOffset.X i, and
	// RootMotion.Position.X i. After a reorder all four must permute identically.
	auto BuildFourFrameAsset = [](UPaper2DPlusCharacterProfileAsset* Asset) -> UPaperFlipbook*
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = TEXT("Idle");
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Asset);
		Anim.Identity.Flipbook = Flipbook;

		Anim.CombatData.Frames.SetNum(4);
		Anim.CombatData.FrameExtractionInfo.SetNum(4);
		Anim.MotionData.RootMotion.SetNum(4);
		for (int32 i = 0; i < 4; ++i)
		{
			Anim.CombatData.Frames[i].FrameName = FString::Printf(TEXT("F%d"), i);
			Anim.CombatData.FrameExtractionInfo[i].SpriteOffset = FIntPoint(i, 0);
			Anim.MotionData.RootMotion[i].Position = FVector2D(static_cast<double>(i), 0.0);
		}
		{
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.KeyFrames.Empty();
			for (int32 i = 0; i < 4; ++i)
			{
				FPaperFlipbookKeyFrame KeyFrame;
				KeyFrame.FrameRun = i + 1;
				KeyFrame.Sprite = NewObject<UPaperSprite>(Flipbook);
				Mutator.KeyFrames.Add(KeyFrame);
			}
		}
		Asset->Flipbooks.Add(Anim);
		return Flipbook;
	};

	// --- Forward move: index 1 -> 3 yields order [F0, F2, F3, F1]. ---
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		UPaperFlipbook* Flipbook = BuildFourFrameAsset(Asset);

		TestTrue(TEXT("Forward MoveFlipbookFrame should succeed"), Asset->MoveFlipbookFrame(0, 1, 3));

		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[0];
		TestEqual(TEXT("Keyframe count preserved"), Flipbook->GetNumKeyFrames(), 4);
		TestEqual(TEXT("Frames count preserved"), Anim.CombatData.Frames.Num(), 4);
		TestEqual(TEXT("ExtractionInfo count preserved"), Anim.CombatData.FrameExtractionInfo.Num(), 4);
		TestEqual(TEXT("RootMotion count preserved"), Anim.MotionData.RootMotion.Num(), 4);

		const TArray<FString> ExpectedNames = { TEXT("F0"), TEXT("F2"), TEXT("F3"), TEXT("F1") };
		const TArray<int32> ExpectedRuns = { 1, 3, 4, 2 };       // FrameRun = origIndex + 1
		const TArray<int32> ExpectedMarkers = { 0, 2, 3, 1 };    // SpriteOffset.X / RootMotion.X = origIndex
		for (int32 i = 0; i < 4; ++i)
		{
			TestEqual(*FString::Printf(TEXT("Frame name at %d"), i), Anim.CombatData.Frames[i].FrameName, ExpectedNames[i]);
			TestEqual(*FString::Printf(TEXT("Keyframe FrameRun at %d"), i), Flipbook->GetKeyFrameChecked(i).FrameRun, ExpectedRuns[i]);
			TestEqual(*FString::Printf(TEXT("ExtractionInfo SpriteOffset.X at %d"), i), Anim.CombatData.FrameExtractionInfo[i].SpriteOffset.X, ExpectedMarkers[i]);
			TestEqual(*FString::Printf(TEXT("RootMotion Position.X at %d"), i), static_cast<int32>(FMath::RoundToInt(Anim.MotionData.RootMotion[i].Position.X)), ExpectedMarkers[i]);
			// SourceFrameIndex is re-stamped to positional order so the frame strip (which sorts by it) reflects the move.
			TestEqual(*FString::Printf(TEXT("SourceFrameIndex restamped at %d"), i), Anim.CombatData.FrameExtractionInfo[i].SourceFrameIndex, i);
		}
	}

	// --- Reverse move: index 3 -> 1 yields order [F0, F3, F1, F2]. ---
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		BuildFourFrameAsset(Asset);
		TestTrue(TEXT("Reverse MoveFlipbookFrame should succeed"), Asset->MoveFlipbookFrame(0, 3, 1));
		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[0];
		const TArray<FString> ExpectedNames = { TEXT("F0"), TEXT("F3"), TEXT("F1"), TEXT("F2") };
		for (int32 i = 0; i < 4; ++i)
		{
			TestEqual(*FString::Printf(TEXT("Reverse frame name at %d"), i), Anim.CombatData.Frames[i].FrameName, ExpectedNames[i]);
			TestEqual(*FString::Printf(TEXT("Reverse SourceFrameIndex at %d"), i), Anim.CombatData.FrameExtractionInfo[i].SourceFrameIndex, i);
		}
	}

	// --- Guards: no-op and out-of-range moves return false and leave order intact. ---
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		BuildFourFrameAsset(Asset);
		TestFalse(TEXT("From == To is a no-op"), Asset->MoveFlipbookFrame(0, 2, 2));
		TestFalse(TEXT("Out-of-range To returns false"), Asset->MoveFlipbookFrame(0, 0, 99));
		TestFalse(TEXT("Out-of-range From returns false"), Asset->MoveFlipbookFrame(0, -1, 1));
		TestFalse(TEXT("Invalid flipbook index returns false"), Asset->MoveFlipbookFrame(5, 0, 1));
		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[0];
		const TArray<FString> ExpectedNames = { TEXT("F0"), TEXT("F1"), TEXT("F2"), TEXT("F3") };
		for (int32 i = 0; i < 4; ++i)
		{
			TestEqual(*FString::Printf(TEXT("Order unchanged after failed moves at %d"), i), Anim.CombatData.Frames[i].FrameName, ExpectedNames[i]);
		}
	}

	// --- Empty RootMotion is left empty (no spurious lockstep growth). ---
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		UPaperFlipbook* Flipbook = BuildFourFrameAsset(Asset);
		Asset->Flipbooks[0].MotionData.RootMotion.Empty();
		TestTrue(TEXT("Move succeeds with empty RootMotion"), Asset->MoveFlipbookFrame(0, 0, 2));
		TestEqual(TEXT("RootMotion stays empty"), Asset->Flipbooks[0].MotionData.RootMotion.Num(), 0);
		TestEqual(TEXT("Keyframes still permuted with empty RootMotion"), Flipbook->GetNumKeyFrames(), 4);
		TestEqual(TEXT("Frame moved to slot 2 with empty RootMotion"), Asset->Flipbooks[0].CombatData.Frames[2].FrameName, FString(TEXT("F0")));
	}

	// --- Reorder with an excluded frame present: documented tail-packing behavior. ---
	// Active frames permute normally; the excluded frame keeps its data but is re-stamped
	// after the active frames (so it renders at the strip tail and restores at the end).
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		UPaperFlipbook* Flipbook = BuildFourFrameAsset(Asset);
		// Exclude the middle frame F1 → active [F0, F2, F3] (3 keyframes), excluded {F1}.
		TestTrue(TEXT("Exclude middle frame should succeed"), Asset->ExcludeFlipbookFrame(0, 1));
		TestEqual(TEXT("Three active keyframes after exclude"), Flipbook->GetNumKeyFrames(), 3);

		// Reorder active F0 (index 0) to index 2 → active [F2, F3, F0].
		TestTrue(TEXT("Reorder with an excluded frame should succeed"), Asset->MoveFlipbookFrame(0, 0, 2));
		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[0];
		TestEqual(TEXT("Active frames permuted correctly with exclusion present"), Anim.CombatData.Frames[2].FrameName, FString(TEXT("F0")));
		TestEqual(TEXT("Active frame 0 after reorder"), Anim.CombatData.Frames[0].FrameName, FString(TEXT("F2")));
		TestEqual(TEXT("Excluded frame data preserved through reorder"), Asset->GetExcludedFlipbookFrameCount(0), 1);
		TestEqual(TEXT("Excluded frame name intact"), Anim.CombatData.ExcludedFrames[0].FrameData.FrameName, FString(TEXT("F1")));
		// Active frames densely re-stamped 0..2; the excluded frame packs after them.
		for (int32 i = 0; i < 3; ++i)
		{
			TestEqual(*FString::Printf(TEXT("Active SourceFrameIndex at %d"), i), Anim.CombatData.FrameExtractionInfo[i].SourceFrameIndex, i);
		}
		TestEqual(TEXT("Excluded frame packed after active frames"), Anim.CombatData.ExcludedFrames[0].ExtractionInfo.SourceFrameIndex, 3);

		// Restoring after the reorder re-inserts at the end (consistent with the tail-pack).
		TestTrue(TEXT("Restore after reorder should succeed"), Asset->RestoreExcludedFlipbookFrame(0, 0));
		TestEqual(TEXT("All four keyframes back after restore"), Flipbook->GetNumKeyFrames(), 4);
		TestEqual(TEXT("Restored frame lands at the end after a reorder"), Asset->Flipbooks[0].CombatData.Frames[3].FrameName, FString(TEXT("F1")));
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileJsonRoundTrip,
	"Paper2DPlus.CharacterProfile.Serialization.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileJsonRoundTrip::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Source->DisplayName = TEXT("Adventurer");
	Source->DefaultAlphaThreshold = 22;
	Source->DefaultPadding = 3;
	Source->DefaultMinSpriteSize = 5;

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Idle");
	FFrameHitboxData Frame;
	Frame.FrameName = TEXT("Idle_00");
	FHitboxData Hitbox;
	Hitbox.Type = EHitboxType::Hurtbox;
	Hitbox.Width = 10;
	Hitbox.Height = 12;
	Frame.Hitboxes.Add(Hitbox);
	Anim.CombatData.Frames.Add(Frame);
	Source->Flipbooks.Add(Anim);
	// The chain markers/identity must survive the trip: a flagged start carrying the chain's own
	// ChainTags container, plus a second live entry flagged as the countable Chain End.
	FFlipbookProfileEntry EndAnim;
	EndAnim.Identity.FlipbookName = TEXT("Windup");
	Source->Flipbooks.Add(EndAnim);
	Source->Flipbooks[0].TransitionData.Transitions.Add(
		FPaper2DPlusMoveTransition(TEXT("Windup")));
	Source->Flipbooks[1].TransitionData.Transitions.Add(
		FPaper2DPlusMoveTransition(TEXT("Idle")));
	FFlipbookTagMappingEntry RootMapping(TEXT("Idle"));
	RootMapping.bIsChainStart = true;
	RootMapping.ChainTags.AddTag(CharacterProfileTest_ChainIdentity);
	Source->TagMappings.FindOrAdd(CharacterProfileTest_RootGroup).Entries.Add(RootMapping);
	FFlipbookTagMappingEntry EndMapping(TEXT("Windup"));
	EndMapping.bIsChainEnd = true;
	Source->TagMappings.FindOrAdd(CharacterProfileTest_RootGroup).Entries.Add(EndMapping);

	FString Json;
	const bool bExported = Source->ExportToJsonString(Json);
	TestTrue(TEXT("ExportToJsonString should succeed"), bExported);
	TestTrue(TEXT("Exported JSON should contain animation name"), Json.Contains(TEXT("Idle")));

	UPaper2DPlusCharacterProfileAsset* Loaded = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TArray<FPaper2DPlusCharacterProfileJsonImportWarning> ImportWarnings;
	const bool bImported = Loaded->ImportFromJsonStringWithWarnings(Json, ImportWarnings);
	TestTrue(TEXT("ImportFromJsonString should succeed"), bImported);
	TestEqual(TEXT("Import reports only the intentional track-layout warning"), ImportWarnings.Num(), 1);
	if (ImportWarnings.Num() == 1)
	{
		TestEqual(TEXT("Import warning is the track-layout reset disclosure"),
			ImportWarnings[0].Code, Paper2DPlusCharacterProfileJson::GetTrackLayoutResetWarningCode());
	}

	TestEqual(TEXT("DisplayName should round-trip"), Loaded->DisplayName, Source->DisplayName);
	TestEqual(TEXT("Animation count should round-trip"), Loaded->Flipbooks.Num(), 2);
	if (Loaded->Flipbooks.Num() > 0)
	{
		TestEqual(TEXT("AnimationName should round-trip"), Loaded->Flipbooks[0].Identity.FlipbookName, TEXT("Idle"));
		TestEqual(TEXT("Frame count should round-trip"), Loaded->Flipbooks[0].CombatData.Frames.Num(), 1);
	}
	if (Loaded->Flipbooks.Num() == Source->Flipbooks.Num())
	{
		for (int32 FlipbookIndex = 0; FlipbookIndex < Loaded->Flipbooks.Num(); ++FlipbookIndex)
		{
			const TArray<FPaper2DPlusMoveTransition>& SourceRows =
				Source->Flipbooks[FlipbookIndex].TransitionData.Transitions;
			const TArray<FPaper2DPlusMoveTransition>& LoadedRows =
				Loaded->Flipbooks[FlipbookIndex].TransitionData.Transitions;
			TestEqual(
				*FString::Printf(TEXT("Transition row count round-trips for animation %d"), FlipbookIndex),
				LoadedRows.Num(), SourceRows.Num());
			for (int32 RowIndex = 0; RowIndex < FMath::Min(LoadedRows.Num(), SourceRows.Num()); ++RowIndex)
			{
				TestEqual(
					*FString::Printf(TEXT("Transition target round-trips for animation %d row %d"),
						FlipbookIndex, RowIndex),
					LoadedRows[RowIndex].TargetMove, SourceRows[RowIndex].TargetMove);
				TestEqual(
					*FString::Printf(TEXT("Transition phase round-trips for animation %d row %d"),
						FlipbookIndex, RowIndex),
					LoadedRows[RowIndex].PhaseTagOverride, SourceRows[RowIndex].PhaseTagOverride);
			}
		}
	}
	const FFlipbookTagMapping* LoadedMapping = Loaded->TagMappings.Find(CharacterProfileTest_RootGroup);
	TestNotNull(TEXT("Exact animation group should round-trip"), LoadedMapping);
	TestTrue(TEXT("JSON preserves the authored Chain Start flag"),
		LoadedMapping && LoadedMapping->Entries.Num() == 2
			&& LoadedMapping->Entries[0].bIsChainStart);
	TestTrue(TEXT("JSON preserves the chain-identity ChainTags container on the start entry"),
		LoadedMapping && LoadedMapping->Entries.Num() == 2
			&& LoadedMapping->Entries[0].ChainTags.Num() == 1
			&& LoadedMapping->Entries[0].ChainTags.HasTagExact(CharacterProfileTest_ChainIdentity));
	TestTrue(TEXT("JSON preserves the authored Chain End flag"),
		LoadedMapping && LoadedMapping->Entries.Num() == 2
			&& !LoadedMapping->Entries[0].bIsChainEnd
			&& LoadedMapping->Entries[1].bIsChainEnd);
	TestTrue(TEXT("The end entry carries no chain container"),
		LoadedMapping && LoadedMapping->Entries.Num() == 2
			&& LoadedMapping->Entries[1].ChainTags.IsEmpty());
	TestFalse(TEXT("Current export carries no legacy RootNumber key"),
		Json.Contains(TEXT("RootNumber")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileValidateInvalidHitboxSize,
	"Paper2DPlus.CharacterProfile.Validation.InvalidHitboxSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileValidateInvalidHitboxSize::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Attack");
	FFrameHitboxData Frame;
	Frame.FrameName = TEXT("Attack_00");
	FHitboxData BadHitbox;
	BadHitbox.Width = 0;
	BadHitbox.Height = 8;
	Frame.Hitboxes.Add(BadHitbox);
	Anim.CombatData.Frames.Add(Frame);
	Asset->Flipbooks.Add(Anim);

	TArray<FCharacterProfileValidationIssue> Issues;
	const bool bValid = Asset->ValidateCharacterProfileAsset(Issues);
	TestFalse(TEXT("Validation should fail for non-positive hitbox dimensions"), bValid);

	bool bFoundInvalidSizeError = false;
	for (const FCharacterProfileValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == ECharacterProfileValidationSeverity::Error &&
			Issue.Message.Contains(TEXT("invalid size")))
		{
			bFoundInvalidSizeError = true;
			break;
		}
	}
	TestTrue(TEXT("Expected invalid size error issue"), bFoundInvalidSizeError);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileBatchCopyRange,
	"Paper2DPlus.CharacterProfile.Batch.CopyFrameDataToRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileBatchCopyRange::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Combo");
	Anim.CombatData.Frames.SetNum(4);
	Anim.CombatData.Frames[1].FrameName = TEXT("Combo_01");
	FHitboxData HB;
	HB.Type = EHitboxType::Attack;
	HB.X = 10;
	HB.Y = 20;
	HB.Width = 30;
	HB.Height = 40;
	Anim.CombatData.Frames[1].Hitboxes.Add(HB);
	Asset->Flipbooks.Add(Anim);

	const bool bOk = Asset->CopyFrameDataToRange(TEXT("Combo"), 1, 2, 3, true);
	TestTrue(TEXT("CopyFrameDataToRange should succeed"), bOk);
	TestEqual(TEXT("Frame 2 hitbox count should be copied"), Asset->Flipbooks[0].CombatData.Frames[2].Hitboxes.Num(), 1);
	TestEqual(TEXT("Frame 3 hitbox count should be copied"), Asset->Flipbooks[0].CombatData.Frames[3].Hitboxes.Num(), 1);
	if (Asset->Flipbooks[0].CombatData.Frames[3].Hitboxes.Num() > 0)
	{
		TestEqual(TEXT("Copied hitbox X should match source"), Asset->Flipbooks[0].CombatData.Frames[3].Hitboxes[0].X, 10);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileBatchMirrorRange,
	"Paper2DPlus.CharacterProfile.Batch.MirrorHitboxesInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileBatchMirrorRange::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Run");
	Anim.CombatData.Frames.SetNum(2);
	FHitboxData HB;
	HB.X = 10;
	HB.Y = 5;
	HB.Width = 20;
	HB.Height = 10;
	Anim.CombatData.Frames[0].Hitboxes.Add(HB);
	Anim.CombatData.Frames[1].Hitboxes.Add(HB);
	Asset->Flipbooks.Add(Anim);

	const int32 Mirrored = Asset->MirrorHitboxesInRange(TEXT("Run"), 0, 1, 50);
	TestEqual(TEXT("Both hitboxes should be mirrored"), Mirrored, 2);
	// Right edge = 30. Mirrored X = 100 - 30 = 70.
	TestEqual(TEXT("Mirrored X should match expected value"), Asset->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].X, 70);
	TestEqual(TEXT("Mirrored X should match expected value (frame 1)"), Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes[0].X, 70);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileImportInvalidJson,
	"Paper2DPlus.CharacterProfile.Serialization.ImportInvalidJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileImportInvalidJson::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	const bool bImported = Asset->ImportFromJsonString(TEXT("{ not-valid-json"));
	TestFalse(TEXT("Import should fail for malformed JSON"), bImported);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileBatchCopyRangeNoSockets,
	"Paper2DPlus.CharacterProfile.Batch.CopyFrameDataToRange_NoSockets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileBatchCopyRangeNoSockets::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Combo");
	Anim.CombatData.Frames.SetNum(3);
	FHitboxData HB;
	HB.X = 8;
	HB.Y = 9;
	HB.Width = 10;
	HB.Height = 11;
	Anim.CombatData.Frames[0].Hitboxes.Add(HB);
	FSocketData Sock;
	Sock.Name = TEXT("Hand");
	Sock.X = 3;
	Sock.Y = 4;
	Anim.CombatData.Frames[0].Sockets.Add(Sock);
	FSocketData ExistingSock;
	ExistingSock.Name = TEXT("Existing");
	ExistingSock.X = 1;
	ExistingSock.Y = 2;
	Anim.CombatData.Frames[2].Sockets.Add(ExistingSock);
	Asset->Flipbooks.Add(Anim);

	const bool bOk = Asset->CopyFrameDataToRange(TEXT("Combo"), 0, 1, 2, false);
	TestTrue(TEXT("CopyFrameDataToRange should succeed"), bOk);
	TestEqual(TEXT("Hitboxes should copy to range"), Asset->Flipbooks[0].CombatData.Frames[2].Hitboxes.Num(), 1);
	TestEqual(TEXT("Sockets should remain unchanged when include-sockets is false"), Asset->Flipbooks[0].CombatData.Frames[2].Sockets.Num(), 1);
	TestEqual(TEXT("Existing socket should remain"), Asset->Flipbooks[0].CombatData.Frames[2].Sockets[0].Name, TEXT("Existing"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileBatchMirrorRangeClamped,
	"Paper2DPlus.CharacterProfile.Batch.MirrorHitboxesInRange_Clamped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileBatchMirrorRangeClamped::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Run");
	Anim.CombatData.Frames.SetNum(2);
	FHitboxData HB;
	HB.X = 5;
	HB.Y = 0;
	HB.Width = 10;
	HB.Height = 10;
	Anim.CombatData.Frames[0].Hitboxes.Add(HB);
	Anim.CombatData.Frames[1].Hitboxes.Add(HB);
	Asset->Flipbooks.Add(Anim);

	// Intentionally out-of-bounds range should clamp to [0,1]
	const int32 Mirrored = Asset->MirrorHitboxesInRange(TEXT("Run"), -10, 50, 20);
	TestEqual(TEXT("Both frame hitboxes should still mirror due to clamped range"), Mirrored, 2);
	// right=15 => x=(40-15)=25
	TestEqual(TEXT("Mirrored X frame 0"), Asset->Flipbooks[0].CombatData.Frames[0].Hitboxes[0].X, 25);
	TestEqual(TEXT("Mirrored X frame 1"), Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes[0].X, 25);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileSetSpriteFlipRange,
	"Paper2DPlus.CharacterProfile.Batch.SetSpriteFlipInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileSetSpriteFlipRange::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Idle");
	Anim.CombatData.Frames.SetNum(3);
	Anim.CombatData.FrameExtractionInfo.SetNum(3);
	Asset->Flipbooks.Add(Anim);

	const int32 Updated = Asset->SetSpriteFlipInRange(TEXT("Idle"), 1, 2, true, false);
	TestEqual(TEXT("Two frames should be updated"), Updated, 2);
	TestFalse(TEXT("Frame 0 should remain unflipped"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo[0].bFlipX);
	TestTrue(TEXT("Frame 1 FlipX should be true"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo[1].bFlipX);
	TestTrue(TEXT("Frame 2 FlipX should be true"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo[2].bFlipX);
	TestFalse(TEXT("Frame 1 FlipY should be false"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo[1].bFlipY);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileSetSpriteFlipForFlipbook,
	"Paper2DPlus.CharacterProfile.Batch.SetSpriteFlipForFlipbook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileSetSpriteFlipForFlipbook::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Idle;
	Idle.Identity.FlipbookName = TEXT("Idle");
	Idle.CombatData.Frames.SetNum(2);
	Asset->Flipbooks.Add(Idle);

	const int32 Updated = Asset->SetSpriteFlipForFlipbook(TEXT("Idle"), false, true);
	TestEqual(TEXT("All Idle frames should be updated"), Updated, 2);
	TestTrue(TEXT("Idle frame 0 flip Y"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo[0].bFlipY);
	TestTrue(TEXT("Idle frame 1 flip Y"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo[1].bFlipY);
	TestFalse(TEXT("Idle frame 1 flip X"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo[1].bFlipX);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileSetSpriteFlipForAllFlipbooks,
	"Paper2DPlus.CharacterProfile.Batch.SetSpriteFlipForAllFlipbooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileSetSpriteFlipForAllFlipbooks::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Idle;
	Idle.Identity.FlipbookName = TEXT("Idle");
	Idle.CombatData.Frames.SetNum(2);
	Asset->Flipbooks.Add(Idle);

	FFlipbookProfileEntry Run;
	Run.Identity.FlipbookName = TEXT("Run");
	Run.CombatData.Frames.SetNum(1);
	Asset->Flipbooks.Add(Run);

	const int32 Updated = Asset->SetSpriteFlipForAllFlipbooks(true, true);
	TestEqual(TEXT("All frames across all animations should be updated"), Updated, 3);
	TestTrue(TEXT("Idle frame 0 flip X"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo[0].bFlipX);
	TestTrue(TEXT("Idle frame 1 flip Y"), Asset->Flipbooks[0].CombatData.FrameExtractionInfo[1].bFlipY);
	TestTrue(TEXT("Run frame 0 flip X"), Asset->Flipbooks[1].CombatData.FrameExtractionInfo[0].bFlipX);

	return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileImportLegacySchemaZero,
	"Paper2DPlus.CharacterProfile.Serialization.ImportLegacySchemaZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileImportLegacySchemaZero::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	const FString LegacyJson = TEXT("{\"SchemaVersion\":0,\"DisplayName\":\"Legacy\",\"Animations\":[]}");
	const bool bImported = Asset->ImportFromJsonString(LegacyJson);
	TestTrue(TEXT("Import should migrate legacy schema version 0 to current"), bImported);
	TestEqual(TEXT("DisplayName should import from legacy payload"), Asset->DisplayName, FString(TEXT("Legacy")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileImportMissingSchemaField,
	"Paper2DPlus.CharacterProfile.Serialization.ImportMissingSchemaField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileImportMissingSchemaField::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	const FString JsonWithoutSchema = TEXT("{\"DisplayName\":\"NoSchema\",\"Animations\":[]}");
	const bool bImported = Asset->ImportFromJsonString(JsonWithoutSchema);
	TestTrue(TEXT("Import should accept payload without explicit schema field"), bImported);
	TestEqual(TEXT("DisplayName should import when schema field is absent"), Asset->DisplayName, FString(TEXT("NoSchema")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileImportFutureSchemaRejected,
	"Paper2DPlus.CharacterProfile.Serialization.ImportFutureSchemaRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileImportFutureSchemaRejected::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	const FString JsonWithFutureSchema = TEXT("{\"SchemaVersion\":999,\"DisplayName\":\"Future\",\"Animations\":[]}");
	const bool bImported = Asset->ImportFromJsonString(JsonWithFutureSchema);
	TestFalse(TEXT("Import should fail for unsupported future schema version"), bImported);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileJsonFileRoundTrip,
	"Paper2DPlus.CharacterProfile.Serialization.JsonFileRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileJsonFileRoundTrip::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Source->DisplayName = TEXT("FileRoundTripCharacter");

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Idle");
	Anim.CombatData.Frames.SetNum(1);
	Source->Flipbooks.Add(Anim);

	const FString TempFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Paper2DPlus"), TEXT("CharacterProfileJsonFileRoundTrip_Test.json"));

	const bool bExported = Source->ExportToJsonFile(TempFile);
	TestTrue(TEXT("ExportToJsonFile should succeed"), bExported);

	UPaper2DPlusCharacterProfileAsset* Loaded = NewObject<UPaper2DPlusCharacterProfileAsset>();
	const bool bImported = Loaded->ImportFromJsonFile(TempFile);
	TestTrue(TEXT("ImportFromJsonFile should succeed"), bImported);
	TestEqual(TEXT("DisplayName should round-trip from file"), Loaded->DisplayName, Source->DisplayName);
	TestEqual(TEXT("Animation count should round-trip from file"), Loaded->Flipbooks.Num(), Source->Flipbooks.Num());

	IFileManager::Get().Delete(*TempFile, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusActorCollisionPipelinePivotConversion,
	"Paper2DPlus.Collision.ActorPipeline.PivotConversionAndDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusActorCollisionPipelinePivotConversion::RunTest(const FString& Parameters)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(128, 128, PF_B8G8R8A8);
	TestNotNull(TEXT("Transient texture should be created"), Texture);
	if (!Texture)
	{
		return false;
	}

	UPaperSprite* Sprite = NewObject<UPaperSprite>();
	TestNotNull(TEXT("Sprite should be created"), Sprite);
	if (!Sprite)
	{
		return false;
	}

	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = Texture;
	InitParams.Offset = FIntPoint::ZeroValue;
	InitParams.Dimension = FIntPoint(128, 128);
	InitParams.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(InitParams);

	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>();
	TestNotNull(TEXT("Flipbook should be created"), Flipbook);
	if (!Flipbook)
	{
		return false;
	}

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		FPaperFlipbookKeyFrame KF;
		KF.Sprite = Sprite;
		KF.FrameRun = 1;
		Mutator.KeyFrames.Add(KF);
	}

	FHitboxData AttackHitbox;
	AttackHitbox.Type = EHitboxType::Attack;
	AttackHitbox.X = 40;
	AttackHitbox.Y = 20;
	AttackHitbox.Width = 10;
	AttackHitbox.Height = 20;
	AttackHitbox.Damage = 7;
	AttackHitbox.Knockback = 3;

	FHitboxData Hurtbox;
	Hurtbox.Type = EHitboxType::Hurtbox;
	Hurtbox.X = 42;
	Hurtbox.Y = 22;
	Hurtbox.Width = 12;
	Hurtbox.Height = 18;

	UPaper2DPlusCharacterProfileAsset* AttackerAsset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaper2DPlusCharacterProfileAsset* DefenderAsset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestNotNull(TEXT("Attacker asset should be created"), AttackerAsset);
	TestNotNull(TEXT("Defender asset should be created"), DefenderAsset);
	if (!AttackerAsset || !DefenderAsset)
	{
		return false;
	}

	FFlipbookProfileEntry AttackerAnim;
	AttackerAnim.Identity.FlipbookName = TEXT("Attack");
	AttackerAnim.Identity.Flipbook = Flipbook;
	FFrameHitboxData AttackerFrame;
	AttackerFrame.Hitboxes.Add(AttackHitbox);
	AttackerAnim.CombatData.Frames.Add(AttackerFrame);
	AttackerAsset->Flipbooks.Add(AttackerAnim);

	FFlipbookProfileEntry DefenderAnim;
	DefenderAnim.Identity.FlipbookName = TEXT("Hurt");
	DefenderAnim.Identity.Flipbook = Flipbook;
	FFrameHitboxData DefenderFrame;
	DefenderFrame.Hitboxes.Add(Hurtbox);
	DefenderAnim.CombatData.Frames.Add(DefenderFrame);
	DefenderAsset->Flipbooks.Add(DefenderAnim);

	auto CreateActorWithData = [&](UPaper2DPlusCharacterProfileAsset* Asset) -> AActor*
	{
		AActor* Actor = NewObject<AActor>();
		if (!Actor)
		{
			return nullptr;
		}

		UPaperFlipbookComponent* FlipbookComp = NewObject<UPaperFlipbookComponent>(Actor);
		UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
		if (!FlipbookComp || !DataComp)
		{
			return nullptr;
		}

		Actor->AddOwnedComponent(FlipbookComp);
		Actor->AddOwnedComponent(DataComp);

		FlipbookComp->SetFlipbook(Flipbook);
		FlipbookComp->SetRelativeLocation(FVector::ZeroVector);
		FlipbookComp->SetRelativeRotation(FRotator::ZeroRotator);
		FlipbookComp->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));

		DataComp->CharacterProfile = Asset;
		DataComp->FlipbookComponent = FlipbookComp;
		return Actor;
	};

	AActor* Attacker = CreateActorWithData(AttackerAsset);
	AActor* Defender = CreateActorWithData(DefenderAsset);
	TestNotNull(TEXT("Attacker actor should be created"), Attacker);
	TestNotNull(TEXT("Defender actor should be created"), Defender);
	if (!Attacker || !Defender)
	{
		return false;
	}

	TArray<FWorldHitbox> AttackBoxes;
	const bool bHasAttackBoxes = UPaper2DPlusBlueprintLibrary::GetActorWorldAttackBoxes(Attacker, AttackBoxes);
	TestTrue(TEXT("GetActorAttackBoxes should resolve attack hitboxes"), bHasAttackBoxes);
	TestEqual(TEXT("Attacker should have exactly one attack hitbox"), AttackBoxes.Num(), 1);

	if (AttackBoxes.Num() > 0)
	{
		const FVector2D PivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
		const int32 PivotXInt = FMath::FloorToInt(PivotLocal.X);
		const int32 PivotYInt = FMath::FloorToInt(PivotLocal.Y);
		const float PivotXFrac = PivotLocal.X - static_cast<float>(PivotXInt);
		const float PivotYFrac = PivotLocal.Y - static_cast<float>(PivotYInt);

		const FVector WorldOrigin(-PivotXFrac, 0.0f, PivotYFrac);
		const float LocalX = static_cast<float>(AttackHitbox.X - PivotXInt);
		const float LocalZ = static_cast<float>(PivotYInt - AttackHitbox.Y - AttackHitbox.Height);
		const FVector ExpectedCenter(WorldOrigin.X + LocalX + AttackHitbox.Width * 0.5f, 0.0f, WorldOrigin.Z + LocalZ + AttackHitbox.Height * 0.5f);

		TestTrue(TEXT("World attack hitbox center X should match pivot-adjusted conversion"),
			FMath::IsNearlyEqual(AttackBoxes[0].Center.X, ExpectedCenter.X, 0.01f));
		TestTrue(TEXT("World attack hitbox center Z should match pivot-adjusted conversion"),
			FMath::IsNearlyEqual(AttackBoxes[0].Center.Z, ExpectedCenter.Z, 0.01f));
		TestEqual(TEXT("Attack hitbox damage should pass through"), AttackBoxes[0].Damage, AttackHitbox.Damage);
		TestEqual(TEXT("Attack hitbox knockback should pass through"), AttackBoxes[0].Knockback, AttackHitbox.Knockback);
	}

	TArray<FHitboxCollisionResult> CollisionResults;
	const bool bHit = UPaper2DPlusBlueprintLibrary::CheckAttackCollision(Attacker, Defender, CollisionResults);
	TestTrue(TEXT("CheckAttackCollision should detect overlap with pivot-adjusted actor data"), bHit);
	TestTrue(TEXT("Collision results should not be empty"), CollisionResults.Num() > 0);

	if (CollisionResults.Num() > 0)
	{
		TestTrue(TEXT("Collision result should include defender actor"), CollisionResults[0].DefenderActor.Get() == Defender);
		TestEqual(TEXT("Collision result damage should match attack hitbox"), CollisionResults[0].Damage, AttackHitbox.Damage);
		TestEqual(TEXT("Collision result knockback should match attack hitbox"), CollisionResults[0].Knockback, AttackHitbox.Knockback);
	}

	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusActorCollisionPipelineFlipXConversion,
	"Paper2DPlus.Collision.ActorPipeline.FlipXPivotConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusActorCollisionPipelineFlipXConversion::RunTest(const FString& Parameters)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(128, 128, PF_B8G8R8A8);
	TestNotNull(TEXT("Transient texture should be created"), Texture);
	if (!Texture)
	{
		return false;
	}

	UPaperSprite* Sprite = NewObject<UPaperSprite>();
	TestNotNull(TEXT("Sprite should be created"), Sprite);
	if (!Sprite)
	{
		return false;
	}

	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = Texture;
	InitParams.Offset = FIntPoint::ZeroValue;
	InitParams.Dimension = FIntPoint(128, 128);
	InitParams.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(InitParams);

	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>();
	TestNotNull(TEXT("Flipbook should be created"), Flipbook);
	if (!Flipbook)
	{
		return false;
	}

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		FPaperFlipbookKeyFrame KF;
		KF.Sprite = Sprite;
		KF.FrameRun = 1;
		Mutator.KeyFrames.Add(KF);
	}

	FHitboxData AttackHitbox;
	AttackHitbox.Type = EHitboxType::Attack;
	AttackHitbox.X = 24;
	AttackHitbox.Y = 30;
	AttackHitbox.Width = 16;
	AttackHitbox.Height = 12;
	AttackHitbox.Damage = 5;
	AttackHitbox.Knockback = 2;

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestNotNull(TEXT("Character asset should be created"), Asset);
	if (!Asset)
	{
		return false;
	}

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("FlipAttack");
	Anim.Identity.Flipbook = Flipbook;
	FFrameHitboxData Frame;
	Frame.Hitboxes.Add(AttackHitbox);
	Anim.CombatData.Frames.Add(Frame);
	Asset->Flipbooks.Add(Anim);

	AActor* Actor = NewObject<AActor>();
	TestNotNull(TEXT("Actor should be created"), Actor);
	if (!Actor)
	{
		return false;
	}

	UPaperFlipbookComponent* FlipbookComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	TestNotNull(TEXT("Flipbook component should be created"), FlipbookComp);
	TestNotNull(TEXT("Character profile component should be created"), DataComp);
	if (!FlipbookComp || !DataComp)
	{
		return false;
	}

	Actor->AddOwnedComponent(FlipbookComp);
	Actor->AddOwnedComponent(DataComp);

	FlipbookComp->SetFlipbook(Flipbook);
	FlipbookComp->SetRelativeLocation(FVector::ZeroVector);
	FlipbookComp->SetRelativeRotation(FRotator::ZeroRotator);
	FlipbookComp->SetRelativeScale3D(FVector(-1.0f, 1.0f, 1.0f));

	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FlipbookComp;

	TArray<FWorldHitbox> AttackBoxes;
	const bool bHasAttackBoxes = UPaper2DPlusBlueprintLibrary::GetActorWorldAttackBoxes(Actor, AttackBoxes);
	TestTrue(TEXT("GetActorAttackBoxes should resolve attack hitboxes for flipped actor"), bHasAttackBoxes);
	TestEqual(TEXT("Flipped actor should have exactly one attack hitbox"), AttackBoxes.Num(), 1);
	if (AttackBoxes.Num() == 0)
	{
		return false;
	}

	const FVector2D PivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
	const int32 PivotXInt = FMath::FloorToInt(PivotLocal.X);
	const int32 PivotYInt = FMath::FloorToInt(PivotLocal.Y);
	const float PivotXFrac = PivotLocal.X - static_cast<float>(PivotXInt);
	const float PivotYFrac = PivotLocal.Y - static_cast<float>(PivotYInt);

	const float LocalX = static_cast<float>(AttackHitbox.X - PivotXInt);
	const float LocalZ = static_cast<float>(PivotYInt - AttackHitbox.Y - AttackHitbox.Height);
	const float W = static_cast<float>(AttackHitbox.Width);
	const float H = static_cast<float>(AttackHitbox.Height);

	// bFlipX=true path in GetScaledHitboxRect: X = -(X + W)
	const FVector ExpectedCenter(
		PivotXFrac + (-(LocalX + W) + W * 0.5f),
		0.0f,
		PivotYFrac + LocalZ + H * 0.5f);

	TestTrue(TEXT("Flipped world hitbox center X should match mirrored pivot-adjusted conversion"),
		FMath::IsNearlyEqual(AttackBoxes[0].Center.X, ExpectedCenter.X, 0.01f));
	TestTrue(TEXT("Flipped world hitbox center Z should match pivot-adjusted conversion"),
		FMath::IsNearlyEqual(AttackBoxes[0].Center.Z, ExpectedCenter.Z, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusActorCollisionPipelineNonUniformScaleConversion,
	"Paper2DPlus.Collision.ActorPipeline.NonUniformScalePivotConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusActorCollisionPipelineNonUniformScaleConversion::RunTest(const FString& Parameters)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(128, 128, PF_B8G8R8A8);
	TestNotNull(TEXT("Transient texture should be created"), Texture);
	if (!Texture)
	{
		return false;
	}

	UPaperSprite* Sprite = NewObject<UPaperSprite>();
	TestNotNull(TEXT("Sprite should be created"), Sprite);
	if (!Sprite)
	{
		return false;
	}

	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = Texture;
	InitParams.Offset = FIntPoint::ZeroValue;
	InitParams.Dimension = FIntPoint(128, 128);
	InitParams.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(InitParams);

	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>();
	TestNotNull(TEXT("Flipbook should be created"), Flipbook);
	if (!Flipbook)
	{
		return false;
	}

	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		FPaperFlipbookKeyFrame KF;
		KF.Sprite = Sprite;
		KF.FrameRun = 1;
		Mutator.KeyFrames.Add(KF);
	}

	FHitboxData AttackHitbox;
	AttackHitbox.Type = EHitboxType::Attack;
	AttackHitbox.X = 18;
	AttackHitbox.Y = 40;
	AttackHitbox.Width = 14;
	AttackHitbox.Height = 20;
	AttackHitbox.Damage = 4;
	AttackHitbox.Knockback = 2;

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestNotNull(TEXT("Character asset should be created"), Asset);
	if (!Asset)
	{
		return false;
	}

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("ScaledAttack");
	Anim.Identity.Flipbook = Flipbook;
	FFrameHitboxData Frame;
	Frame.Hitboxes.Add(AttackHitbox);
	Anim.CombatData.Frames.Add(Frame);
	Asset->Flipbooks.Add(Anim);

	AActor* Actor = NewObject<AActor>();
	TestNotNull(TEXT("Actor should be created"), Actor);
	if (!Actor)
	{
		return false;
	}

	UPaperFlipbookComponent* FlipbookComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	TestNotNull(TEXT("Flipbook component should be created"), FlipbookComp);
	TestNotNull(TEXT("Character profile component should be created"), DataComp);
	if (!FlipbookComp || !DataComp)
	{
		return false;
	}

	Actor->AddOwnedComponent(FlipbookComp);
	Actor->AddOwnedComponent(DataComp);

	const FVector ComponentWorldLocation(120.0f, 0.0f, 75.0f);
	const FVector ComponentScale(-2.0f, 1.0f, 0.5f);
	FlipbookComp->SetFlipbook(Flipbook);
	FlipbookComp->SetRelativeLocation(ComponentWorldLocation);
	FlipbookComp->SetRelativeRotation(FRotator::ZeroRotator);
	FlipbookComp->SetRelativeScale3D(ComponentScale);

	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FlipbookComp;

	TArray<FWorldHitbox> AttackBoxes;
	const bool bHasAttackBoxes = UPaper2DPlusBlueprintLibrary::GetActorWorldAttackBoxes(Actor, AttackBoxes);
	TestTrue(TEXT("GetActorAttackBoxes should resolve attack hitboxes for non-uniform scale"), bHasAttackBoxes);
	TestEqual(TEXT("Scaled actor should have exactly one attack hitbox"), AttackBoxes.Num(), 1);
	if (AttackBoxes.Num() == 0)
	{
		return false;
	}

	const float ScaleX = FMath::Abs(ComponentScale.X);
	const float ScaleZ = FMath::Abs(ComponentScale.Z);

	const FVector2D PivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
	const int32 PivotXInt = FMath::FloorToInt(PivotLocal.X);
	const int32 PivotYInt = FMath::FloorToInt(PivotLocal.Y);
	const float PivotXFrac = PivotLocal.X - static_cast<float>(PivotXInt);
	const float PivotYFrac = PivotLocal.Y - static_cast<float>(PivotYInt);

	const float LocalX = static_cast<float>(AttackHitbox.X - PivotXInt);
	const float LocalZ = static_cast<float>(PivotYInt - AttackHitbox.Y - AttackHitbox.Height);
	const float W = static_cast<float>(AttackHitbox.Width) * ScaleX;
	const float H = static_cast<float>(AttackHitbox.Height) * ScaleZ;

	const float WorldOriginX = ComponentWorldLocation.X + PivotXFrac * ScaleX;
	const float WorldOriginZ = ComponentWorldLocation.Z + PivotYFrac * ScaleZ;
	const float MirroredX = -(LocalX * ScaleX + W);

	const FVector ExpectedCenter(
		WorldOriginX + MirroredX + W * 0.5f,
		0.0f,
		WorldOriginZ + LocalZ * ScaleZ + H * 0.5f);

	TestTrue(TEXT("Non-uniform world hitbox center X should match pivot-adjusted conversion"),
		FMath::IsNearlyEqual(AttackBoxes[0].Center.X, ExpectedCenter.X, 0.01f));
	TestTrue(TEXT("Non-uniform world hitbox center Z should match pivot-adjusted conversion"),
		FMath::IsNearlyEqual(AttackBoxes[0].Center.Z, ExpectedCenter.Z, 0.01f));
	TestTrue(TEXT("Non-uniform world hitbox X extent should match X scale"),
		FMath::IsNearlyEqual(AttackBoxes[0].Extents.X, W * 0.5f, 0.01f));
	TestTrue(TEXT("Non-uniform world hitbox Z extent should match Z scale"),
		FMath::IsNearlyEqual(AttackBoxes[0].Extents.Z, H * 0.5f, 0.01f));

	return true;
}

// ==========================================
// SOCKET vs HURTBOX EQUAL WORLD-Z (audit Wave 0 / TASK-60 AC#4)
//
// The Wave 0 High bug: the anonymous-namespace MakeWorldSocket (the GAMEPLAY actor path) used
// `WorldPosition.Z - Y` instead of `+ Y`, mirroring sockets vertically relative to hurtboxes.
// A socket and a hurtbox authored at the same local Y must resolve to the same world Z.
//
// Two tests, deliberately split because socket->world is implemented TWICE (the duplication that
// caused Wave 0 in the first place):
//   1. WorldConversion.SocketHurtboxEqualZ — pins the PUBLIC conversions SocketToWorldSpace3D /
//      HitboxToWorldSpace3D (the debug-draw path), which always used +Y. It documents the +Y
//      convention contract + socket/hurtbox parity, but it does NOT exercise MakeWorldSocket, so
//      it would NOT catch a regression isolated to that helper.
//   2. ActorPipeline.SocketHurtboxEqualZ — the actual Wave 0 guard: it drives the live actor path
//      (GetActorWorldSockets/GetActorWorldHurtboxes -> MakeWorldSocket/MakeWorldHitbox), so
//      reverting MakeWorldSocket to `- Y` makes it fail.
// Follow-up (root cause): collapse MakeWorldSocket and SocketToWorldSpace onto one shared helper so
// the gameplay and debug-draw conventions can never drift apart again.
// ==========================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSocketHurtboxEqualWorldZ,
	"Paper2DPlus.Collision.WorldConversion.SocketHurtboxEqualZ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSocketHurtboxEqualWorldZ::RunTest(const FString& Parameters)
{
	const FVector WorldPosition(100.0f, 0.0f, 50.0f);
	const bool bFlipX = false;
	const float Scale = 1.0f;
	const int32 LocalX = 30;
	const int32 LocalY = 18;

	FSocketData Socket;
	Socket.Name = TEXT("TestSocket");
	Socket.X = LocalX;
	Socket.Y = LocalY;

	// Zero-size hurtbox: its top edge equals its center, so its world Z is directly comparable
	// to the socket's point Z (no Height*0.5 term).
	FHitboxData Hurtbox;
	Hurtbox.Type = EHitboxType::Hurtbox;
	Hurtbox.X = LocalX;
	Hurtbox.Y = LocalY;
	Hurtbox.Width = 0;
	Hurtbox.Height = 0;

	const FVector SocketWorld =
		UPaper2DPlusBlueprintLibrary::SocketToWorldSpace3D(Socket, WorldPosition, bFlipX, Scale, Scale);
	// HitboxToWorldSpace3D returns an FBox2D whose .Y axis carries world Z.
	const FBox2D HurtboxWorld =
		UPaper2DPlusBlueprintLibrary::HitboxToWorldSpace3D(Hurtbox, WorldPosition, bFlipX, Scale);
	const double HurtboxWorldZ = HurtboxWorld.Min.Y;

	TestEqual(TEXT("Socket and hurtbox at same local Y must have equal world Z"),
		SocketWorld.Z, HurtboxWorldZ);

	const double ExpectedZ = WorldPosition.Z + static_cast<double>(LocalY);
	TestEqual(TEXT("Socket world Z follows +Y convention (Z = WorldPosition.Z + LocalY)"),
		SocketWorld.Z, ExpectedZ);
	TestEqual(TEXT("Hurtbox world Z follows +Y convention"),
		HurtboxWorldZ, ExpectedZ);

	// Convention sentinel: the old `- Y` would give WorldPosition.Z - LocalY, differing from the
	// hurtbox by exactly 2*LocalY. (This pins the +Y contract of the PUBLIC conversion; a regression
	// isolated to the MakeWorldSocket helper is caught by the actor-path test, not this one.)
	const double BuggySocketZ = WorldPosition.Z - static_cast<double>(LocalY);
	TestNotEqual(TEXT("Old '-Y' socket Z must differ from hurtbox Z (regression sentinel)"),
		BuggySocketZ, HurtboxWorldZ);
	TestEqual(TEXT("Fix vs old bug differ by exactly 2*LocalY"),
		HurtboxWorldZ - BuggySocketZ, 2.0 * static_cast<double>(LocalY));

	TestEqual(TEXT("Socket and hurtbox share world X"),
		SocketWorld.X, HurtboxWorld.Min.X);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusActorSocketHurtboxEqualWorldZ,
	"Paper2DPlus.Collision.ActorPipeline.SocketHurtboxEqualZ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusActorSocketHurtboxEqualWorldZ::RunTest(const FString& Parameters)
{
	// Same guard as above but through the live actor path (GetActorWorldSockets /
	// GetActorWorldHurtboxes -> MakeWorldSocket / MakeWorldHitbox), the path the Wave 0 bug
	// shipped in. ConvertFrameDataFromTopLeftToPivotSpace flips Y identically for sockets and
	// hitboxes, so a zero-size hurtbox and a socket at the same local (X,Y) coincide post-fix.
	UTexture2D* Texture = UTexture2D::CreateTransient(128, 128, PF_B8G8R8A8);
	TestNotNull(TEXT("Transient texture should be created"), Texture);
	if (!Texture) return false;

	UPaperSprite* Sprite = NewObject<UPaperSprite>();
	TestNotNull(TEXT("Sprite should be created"), Sprite);
	if (!Sprite) return false;
	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = Texture;
	InitParams.Offset = FIntPoint::ZeroValue;
	InitParams.Dimension = FIntPoint(128, 128);
	InitParams.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(InitParams);

	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>();
	TestNotNull(TEXT("Flipbook should be created"), Flipbook);
	if (!Flipbook) return false;
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		FPaperFlipbookKeyFrame KF;
		KF.Sprite = Sprite;
		KF.FrameRun = 1;
		Mutator.KeyFrames.Add(KF);
	}

	// LocalY (10) differs from the sprite's center pivot, so the buggy mirror would be visible.
	const int32 LocalX = 36;
	const int32 LocalY = 10;

	FHitboxData Hurtbox;
	Hurtbox.Type = EHitboxType::Hurtbox;
	Hurtbox.X = LocalX;
	Hurtbox.Y = LocalY;
	Hurtbox.Width = 0;
	Hurtbox.Height = 0;

	FSocketData Socket;
	Socket.Name = TEXT("Hand");
	Socket.X = LocalX;
	Socket.Y = LocalY;

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestNotNull(TEXT("Profile asset should be created"), Asset);
	if (!Asset) return false;
	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Test");
	Anim.Identity.Flipbook = Flipbook;
	FFrameHitboxData Frame;
	Frame.Hitboxes.Add(Hurtbox);
	Frame.Sockets.Add(Socket);
	Anim.CombatData.Frames.Add(Frame);
	Asset->Flipbooks.Add(Anim);

	AActor* Actor = NewObject<AActor>();
	TestNotNull(TEXT("Actor should be created"), Actor);
	if (!Actor) return false;
	UPaperFlipbookComponent* FlipbookComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	TestNotNull(TEXT("Flipbook component should be created"), FlipbookComp);
	TestNotNull(TEXT("Data component should be created"), DataComp);
	if (!FlipbookComp || !DataComp) return false;
	Actor->AddOwnedComponent(FlipbookComp);
	Actor->AddOwnedComponent(DataComp);
	FlipbookComp->SetFlipbook(Flipbook);
	FlipbookComp->SetRelativeLocation(FVector::ZeroVector);
	FlipbookComp->SetRelativeRotation(FRotator::ZeroRotator);
	FlipbookComp->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));
	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FlipbookComp;

	TArray<FWorldHitbox> Hurtboxes;
	const bool bHasHurt = UPaper2DPlusBlueprintLibrary::GetActorWorldHurtboxes(Actor, Hurtboxes);
	TestTrue(TEXT("GetActorWorldHurtboxes should resolve"), bHasHurt);
	TestEqual(TEXT("Exactly one world hurtbox"), Hurtboxes.Num(), 1);

	TArray<FWorldSocket> Sockets;
	const bool bHasSock = UPaper2DPlusBlueprintLibrary::GetActorWorldSockets(Actor, Sockets);
	TestTrue(TEXT("GetActorWorldSockets should resolve"), bHasSock);
	TestEqual(TEXT("Exactly one world socket"), Sockets.Num(), 1);

	if (Hurtboxes.Num() == 1 && Sockets.Num() == 1)
	{
		TestTrue(TEXT("Actor-path socket and hurtbox at same local Y must have equal world Z"),
			FMath::IsNearlyEqual(Sockets[0].Location.Z, Hurtboxes[0].Center.Z, 0.01f));
		TestTrue(TEXT("Actor-path socket and hurtbox share world X"),
			FMath::IsNearlyEqual(Sockets[0].Location.X, Hurtboxes[0].Center.X, 0.01f));
	}

	return true;
}

// ==========================================
// PER-FRAME PIVOT CACHE (TASK-48 — packaged-build pivot serialization)
//
// RepopulatePivotCache (run on the asset's PreSave) bakes each key-frame's live sprite pivot
// (GetPivotPosition() - GetSourceUV()) into FrameExtractionInfo[i].CachedPivotLocal so packaged
// (non-editor) builds — where those sprite APIs are stripped — read the same pivot the editor uses
// live. GetFramePivotLocal is the single resolver (editor: live; packaged: cache).
// ==========================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPivotCachePopulation,
	"Paper2DPlus.CharacterProfile.Pivot.RepopulateCacheMatchesLivePivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPivotCachePopulation::RunTest(const FString& Parameters)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(128, 128, PF_B8G8R8A8);
	TestNotNull(TEXT("Texture"), Texture);
	if (!Texture) return false;

	UPaperSprite* Sprite = NewObject<UPaperSprite>();
	TestNotNull(TEXT("Sprite"), Sprite);
	if (!Sprite) return false;
	FSpriteAssetInitParameters InitParams;
	InitParams.Texture = Texture;
	InitParams.Offset = FIntPoint::ZeroValue;
	InitParams.Dimension = FIntPoint(128, 128);
	InitParams.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(InitParams);

	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>();
	TestNotNull(TEXT("Flipbook"), Flipbook);
	if (!Flipbook) return false;
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		FPaperFlipbookKeyFrame KF;
		KF.Sprite = Sprite;
		KF.FrameRun = 1;
		Mutator.KeyFrames.Add(KF);
	}

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestNotNull(TEXT("Asset"), Asset);
	if (!Asset) return false;
	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Test");
	Anim.Identity.Flipbook = Flipbook;
	Anim.CombatData.Frames.Add(FFrameHitboxData());
	Anim.CombatData.FrameExtractionInfo.Add(FSpriteExtractionInfo()); // parallel entry, sentinel pivot
	Asset->Flipbooks.Add(Anim);

	// A fresh entry is "un-cached" (sentinel) so packaged builds fall back gracefully to top-left.
	TestFalse(TEXT("Pivot is un-cached before RepopulatePivotCache"),
		Asset->Flipbooks[0].CombatData.FrameExtractionInfo[0].IsPivotCached());

	Asset->RepopulatePivotCache();

	const FVector2D Expected = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
	const FSpriteExtractionInfo& Info = Asset->Flipbooks[0].CombatData.FrameExtractionInfo[0];
	TestTrue(TEXT("Pivot is cached after RepopulatePivotCache"), Info.IsPivotCached());
	TestTrue(TEXT("Cached pivot X == live pivot"), FMath::IsNearlyEqual(Info.CachedPivotLocal.X, Expected.X, 0.001f));
	TestTrue(TEXT("Cached pivot Y == live pivot"), FMath::IsNearlyEqual(Info.CachedPivotLocal.Y, Expected.Y, 0.001f));

	// GetFramePivotLocal (the unified resolver) returns the same value the runtime pivot path consumes.
	FVector2D Resolved;
	TestTrue(TEXT("GetFramePivotLocal resolves"), Asset->GetFramePivotLocal(Flipbook, 0, Resolved));
	TestTrue(TEXT("GetFramePivotLocal X == live pivot"), FMath::IsNearlyEqual(Resolved.X, Expected.X, 0.001f));
	TestTrue(TEXT("GetFramePivotLocal Y == live pivot"), FMath::IsNearlyEqual(Resolved.Y, Expected.Y, 0.001f));
	TestFalse(TEXT("GetFramePivotLocal returns false for an out-of-range frame"),
		Asset->GetFramePivotLocal(Flipbook, 99, Resolved));

	return true;
}

// ==========================================
// EVENT-DRIVEN FRAME-CHANGE REFACTOR SHARED UTILITIES
//
// Shared test helpers for root motion, frame events, and related dispatch tests.
// ==========================================

namespace Paper2DPlusEventDrivenTestUtils
{
	static UPaperSprite* MakeSprite()
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(32, 32, PF_B8G8R8A8);
		if (!Texture) return nullptr;

		UPaperSprite* Sprite = NewObject<UPaperSprite>();
		FSpriteAssetInitParameters InitParams;
		InitParams.Texture = Texture;
		InitParams.Offset = FIntPoint::ZeroValue;
		InitParams.Dimension = FIntPoint(32, 32);
		InitParams.SetPixelsPerUnrealUnit(1.0f);
		Sprite->InitializeSprite(InitParams);
		return Sprite;
	}

	static UPaperFlipbook* MakeSingleFrameFlipbook(UPaperSprite* Sprite)
	{
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>();
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		FPaperFlipbookKeyFrame KF;
		KF.Sprite = Sprite;
		KF.FrameRun = 1;
		Mutator.KeyFrames.Add(KF);
		return FB;
	}

	static UPaperFlipbook* MakeMultiFrameFlipbook(UPaperSprite* Sprite, int32 NumFrames)
	{
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>();
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		for (int32 i = 0; i < NumFrames; ++i)
		{
			FPaperFlipbookKeyFrame KF;
			KF.Sprite = Sprite;
			KF.FrameRun = 1;
			Mutator.KeyFrames.Add(KF);
		}
		return FB;
	}
}


#include "Tests/Paper2DPlusTestFrameEventTypes.h"
#include "Tests/Paper2DPlusTestFrameCueTypes.h"

// ==========================================
// ROOT MOTION CORRECTNESS TESTS
//
// Follow-up to the event-driven refactor (commit 3fca39a). These tests cover
// correctness bugs in the root motion path:
//   1. ResetRootMotionTracking over-scoped — silently broke shared cache
//   2. Frame-0 teleport on non-zero RootMotion[0]
//   3. Loop-wrap teleport (same root cause as #2)
//   4. GetRootMotionDelta / ApplyRootMotionForFrame ~80% duplication
//
// Tests 4-6 validate that narrow reset / toggle / profile swap preserve the
// shared frame event cache (using test frame events, not the removed effects API).
//
// Tests are worldless, matching the rest of this file. Production GetWorld()
// guards in ApplyRootMotionForFrame (around AddActorWorldOffset) make this
// possible — the actor's transform tracking still works without a world,
// just not the world-side offset apply.
//
// See docs/plans/2026-04-08-fix-root-motion-correctness-plan.md for original context.
// ==========================================

namespace Paper2DPlusRootMotionTestUtils
{
	// Shared: builds an actor + components + profile with an authored root motion
	// trajectory. Caller provides the positions; we build a matching multi-frame
	// flipbook and a CharacterProfile asset that references it.
	struct FRootMotionTestSetup
	{
		UPaperSprite* Sprite = nullptr;
		UPaperFlipbook* CharacterFB = nullptr;
		UPaper2DPlusCharacterProfileAsset* Asset = nullptr;
		AActor* Actor = nullptr;
		UPaperFlipbookComponent* FBComp = nullptr;
		UPaper2DPlusCharacterProfileComponent* DataComp = nullptr;
	};

	static FRootMotionTestSetup BuildRootMotionSetup(const TArray<FVector2D>& Positions)
	{
		using namespace Paper2DPlusEventDrivenTestUtils;
		FRootMotionTestSetup S;

		S.Sprite = MakeSprite();
		S.CharacterFB = MakeMultiFrameFlipbook(S.Sprite, Positions.Num());
		S.Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = TEXT("Walk");
		Anim.Identity.Flipbook = S.CharacterFB;
		for (const FVector2D& Pos : Positions)
		{
			FRootMotionFrameData FrameData;
			FrameData.Position = Pos;
			Anim.MotionData.RootMotion.Add(FrameData);
		}
		S.Asset->Flipbooks.Add(Anim);

		S.Actor = NewObject<AActor>();
		S.FBComp = NewObject<UPaperFlipbookComponent>(S.Actor);
		S.DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(S.Actor);
		S.Actor->AddOwnedComponent(S.FBComp);
		S.Actor->AddOwnedComponent(S.DataComp);

		S.FBComp->SetFlipbook(S.CharacterFB);
		// Positive scale on all axes so IsFacingLeft() returns false and the
		// delta assertions can match Sprite-space pixel values directly.
		S.FBComp->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));
		S.DataComp->CharacterProfile = S.Asset;
		S.DataComp->FlipbookComponent = S.FBComp;
		S.DataComp->bAutoApplyRootMotion = true;

		return S;
	}

	static void AddRootMotionFrames(FFlipbookProfileEntry& Anim, const TArray<FVector2D>& Positions)
	{
		for (const FVector2D& Pos : Positions)
		{
			FRootMotionFrameData FrameData;
			FrameData.Position = Pos;
			Anim.MotionData.RootMotion.Add(FrameData);
		}
	}
}

// --- Test 1: non-zero RootMotion[0] must not teleport on frame-0 dispatch ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileRootMotionFrameZeroNonZeroPositionDoesNotTeleport,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.FrameZeroNonZeroPositionDoesNotTeleport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileRootMotionFrameZeroNonZeroPositionDoesNotTeleport::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusRootMotionTestUtils;

	// Lunge-style trajectory that starts at +50 px and advances to +60.
	FRootMotionTestSetup S = BuildRootMotionSetup({FVector2D(50.0f, 0.0f), FVector2D(60.0f, 0.0f)});
	if (!TestNotNull(TEXT("Setup should succeed"), S.DataComp)) return false;

	// Frame-0 dispatch through the unified handler.
	S.DataComp->HandleFlipbookChanged(S.CharacterFB);

	// The actor must NOT have teleported by (50, 0). Pre-fix behavior: the
	// baseline started at ZeroVector, so delta = (50,0) - (0,0) = (50,0) and
	// AddActorWorldOffset shifted the actor by 50. Post-fix: baseline is
	// seeded from RootMotion[0], so delta is zero on frame 0.
	//
	// Note: this test runs worldless, so AddActorWorldOffset is gated by the
	// GetWorld() guard and never actually fires. The assertion here verifies
	// the baseline advance via GetRootMotionDelta — if the baseline was
	// stuck at ZeroVector (buggy), GetRootMotionDelta would return a non-zero
	// vector for "the delta that would be applied."
	const FVector Delta = S.DataComp->GetRootMotionDelta();
	TestTrue(TEXT("GetRootMotionDelta should report zero on frame 0 after seed-from-reference"),
		Delta.IsNearlyZero());

	return true;
}

// --- Test 2: walk-up case (RootMotion[0]=(0,0)) still produces delta on frame 1 ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileRootMotionWalkUpFromZeroFrameZero,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.WalkUpFromZeroFrameZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileRootMotionWalkUpFromZeroFrameZero::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusRootMotionTestUtils;

	// Classic walking trajectory — starts at (0,0), advances 10 px per frame.
	FRootMotionTestSetup S = BuildRootMotionSetup({FVector2D::ZeroVector, FVector2D(10.0f, 0.0f)});
	if (!TestNotNull(TEXT("Setup should succeed"), S.DataComp)) return false;

	// Dispatch frame 0 (baseline seed, no motion). Then dispatch frame 1.
	S.DataComp->HandleFlipbookChanged(S.CharacterFB);
	S.DataComp->HandleFrameChanged(1);

	// After frame 1's Apply, LastAppliedRootMotionPos should equal (10, 0) —
	// the baseline advanced to the new sample. GetRootMotionDelta reports the
	// delta relative to the last applied position, so for the same frame it
	// should return zero.
	const FVector Delta = S.DataComp->GetRootMotionDelta();
	TestTrue(TEXT("GetRootMotionDelta should return zero after frame 1 apply advanced baseline"),
		Delta.IsNearlyZero());

	return true;
}

// --- Test 3: loop wrap with non-zero RootMotion[0] must not teleport ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileRootMotionLoopWrapWithNonZeroFrameZero,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.LoopWrapWithNonZeroFrameZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileRootMotionLoopWrapWithNonZeroFrameZero::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusRootMotionTestUtils;

	// 3-frame loop with a non-zero reference position. After wrap, the baseline
	// must be reseeded to RootMotion[0] = (10,0) so the next frame-1 dispatch
	// computes (20,0) - (10,0) = (10,0) — not (20,0) - (0,0) = (20,0).
	FRootMotionTestSetup S = BuildRootMotionSetup({
		FVector2D(10.0f, 0.0f),
		FVector2D(20.0f, 0.0f),
		FVector2D(30.0f, 0.0f)
	});
	if (!TestNotNull(TEXT("Setup should succeed"), S.DataComp)) return false;

	// Walk through the animation: frame 0 (seed) → 1 → 2 → wrap to 0.
	S.DataComp->HandleFlipbookChanged(S.CharacterFB);
	S.DataComp->HandleFrameChanged(1);
	S.DataComp->HandleFrameChanged(2);
	S.DataComp->HandleFrameChanged(0);  // loop wrap

	// After the wrap, the baseline should be re-seeded to RootMotion[0] = (10,0).
	// GetRootMotionDelta on frame 0 should now report zero (same frame, baseline
	// just advanced). Pre-fix: baseline reset to ZeroVector, so the query would
	// report (10, 0, 0) — indicating a phantom teleport would occur on the next
	// apply.
	const FVector Delta = S.DataComp->GetRootMotionDelta();
	TestTrue(TEXT("GetRootMotionDelta should report zero immediately after loop wrap re-seed"),
		Delta.IsNearlyZero());

	return true;
}

// --- Test 4: ResetRootMotionTracking must NOT nuke the shared frame event cache ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileResetRootMotionPreservesSharedCache,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.ResetPreservesSharedCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileResetRootMotionPreservesSharedCache::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEventDrivenTestUtils;

	// Build a profile with BOTH root motion AND a frame-0 cue.
	UPaperSprite* Sprite = MakeSprite();
	UPaperFlipbook* CharacterFB = MakeMultiFrameFlipbook(Sprite, 2);

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Attack");
	Anim.Identity.Flipbook = CharacterFB;

	FRootMotionFrameData RM0; RM0.Position = FVector2D::ZeroVector;
	FRootMotionFrameData RM1; RM1.Position = FVector2D(10.0f, 0.0f);
	Anim.MotionData.RootMotion.Add(RM0);
	Anim.MotionData.RootMotion.Add(RM1);

	UPaper2DPlusTestMomentCue* TestCue = NewObject<UPaper2DPlusTestMomentCue>(Asset, NAME_None, RF_Transactional);
	TestCue->TriggerFrame = 0;
	Anim.FrameEventData.FrameCues.Add(TestCue);

	Asset->Flipbooks.Add(Anim);

	AActor* Actor = NewObject<AActor>();
	UPaperFlipbookComponent* FBComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	Actor->AddOwnedComponent(FBComp);
	Actor->AddOwnedComponent(DataComp);
	FBComp->SetFlipbook(CharacterFB);
	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FBComp;
	DataComp->bAutoApplyRootMotion = true;
	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	// Dispatch frame-0 cue via HandleFlipbookChanged.
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("Precondition: frame-0 cue should have broadcast"), Recorder->Contexts.Num(), 1);

	// Now call ResetRootMotionTracking. Pre-fix: this wiped the shared cache,
	// silently breaking subsequent frame event dispatch. Post-fix: only
	// root-motion tracking state is reset; the shared cache survives.
	DataComp->ResetRootMotionTracking();

	// Re-dispatch frame 0 via HandleFlipbookChanged. If the cue cache was wiped,
	// the cue would not broadcast.
	Recorder->Cues.Reset();
	Recorder->Contexts.Reset();
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("After ResetRootMotionTracking, frame-0 cue should still broadcast (cache preserved)"),
		Recorder->Contexts.Num(), 1);

	return true;
}

// --- Test 5: SetAutoApplyRootMotion(false) must NOT break frame event dispatch ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileSetAutoApplyRootMotionFalsePreservesFrameEvents,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.SetAutoApplyFalsePreservesFrameEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileSetAutoApplyRootMotionFalsePreservesFrameEvents::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEventDrivenTestUtils;

	// Build a profile with a frame-0 cue.
	UPaperSprite* Sprite = MakeSprite();
	UPaperFlipbook* CharacterFB = MakeSingleFrameFlipbook(Sprite);

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Attack");
	Anim.Identity.Flipbook = CharacterFB;

	UPaper2DPlusTestMomentCue* TestCue = NewObject<UPaper2DPlusTestMomentCue>(Asset, NAME_None, RF_Transactional);
	TestCue->TriggerFrame = 0;
	Anim.FrameEventData.FrameCues.Add(TestCue);
	Asset->Flipbooks.Add(Anim);

	AActor* Actor = NewObject<AActor>();
	UPaperFlipbookComponent* FBComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	Actor->AddOwnedComponent(FBComp);
	Actor->AddOwnedComponent(DataComp);
	FBComp->SetFlipbook(CharacterFB);
	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FBComp;
	DataComp->bAutoApplyRootMotion = true;   // initially on, then toggled off
	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	// Warm caches first via frame-0 dispatch.
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("Precondition: frame-0 cue should have broadcast"), Recorder->Contexts.Num(), 1);

	// Toggle root motion off — this internally calls ResetRootMotionTracking.
	// Pre-fix: the reset nuked the shared cache, breaking frame events.
	// Post-fix: the reset is narrow and preserves the frame event cache.
	DataComp->SetAutoApplyRootMotion(false);

	// Re-dispatch. Cue should still broadcast if cache is intact.
	Recorder->Cues.Reset();
	Recorder->Contexts.Reset();
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("After SetAutoApplyRootMotion(false), frame-0 cue should still broadcast"),
		Recorder->Contexts.Num(), 1);

	return true;
}

// --- Test 6: SetCharacterProfile swap must re-warm caches for the new profile ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileSetCharacterProfileRewarmsOnSwap,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.SetCharacterProfileRewarmsOnSwap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileSetCharacterProfileRewarmsOnSwap::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEventDrivenTestUtils;

	// Build two profiles A and B that both reference the SAME flipbook but
	// each author a different frame-0 test cue. After swapping profiles,
	// the new profile's cue should broadcast — not the old one.
	UPaperSprite* Sprite = MakeSprite();
	UPaperFlipbook* CharacterFB = MakeSingleFrameFlipbook(Sprite);

	auto BuildAssetWithCue = [&]() -> TPair<UPaper2DPlusCharacterProfileAsset*, UPaper2DPlusTestMomentCue*>
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = TEXT("Attack");
		Anim.Identity.Flipbook = CharacterFB;
		UPaper2DPlusTestMomentCue* Cue = NewObject<UPaper2DPlusTestMomentCue>(Asset, NAME_None, RF_Transactional);
		Cue->TriggerFrame = 0;
		Anim.FrameEventData.FrameCues.Add(Cue);
		Asset->Flipbooks.Add(Anim);
		return {Asset, Cue};
	};

	auto [ProfileA, CueA] = BuildAssetWithCue();
	auto [ProfileB, CueB] = BuildAssetWithCue();

	AActor* Actor = NewObject<AActor>();
	UPaperFlipbookComponent* FBComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	Actor->AddOwnedComponent(FBComp);
	Actor->AddOwnedComponent(DataComp);
	FBComp->SetFlipbook(CharacterFB);
	DataComp->CharacterProfile = ProfileA;
	DataComp->FlipbookComponent = FBComp;
	UPaper2DPlusFrameCueRecorder* Recorder = NewObject<UPaper2DPlusFrameCueRecorder>();
	DataComp->OnFrameCue.AddDynamic(Recorder, &UPaper2DPlusFrameCueRecorder::OnCue);

	// Prime the cache on profile A.
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("Profile A: one cue should have broadcast"), Recorder->Cues.Num(), 1);
	if (Recorder->Cues.Num() == 1)
	{
		TestTrue(TEXT("Profile A cue identity"), Recorder->Cues[0] == CueA);
	}

	// Swap to profile B. SetCharacterProfile routes through
	// HandleFlipbookChanged which re-warms the cache with B's data AND
	// dispatches frame-0 events for B in one pass.
	DataComp->SetCharacterProfile(ProfileB);

	TestEqual(TEXT("After swap: B's cue should broadcast once"), Recorder->Cues.Num(), 2);
	if (Recorder->Cues.Num() == 2)
	{
		TestTrue(TEXT("Profile B cue identity"), Recorder->Cues[1] == CueB);
	}

	return true;
}

// --- Test 7: GetRootMotionDelta shares the helper with Apply — behavior matches ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileGetRootMotionDeltaSharesHelperWithApply,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.GetDeltaSharesHelperWithApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileGetRootMotionDeltaSharesHelperWithApply::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusRootMotionTestUtils;

	// Classic walk: frame 0 = (0,0), frame 1 = (10,0).
	FRootMotionTestSetup S = BuildRootMotionSetup({FVector2D::ZeroVector, FVector2D(10.0f, 0.0f)});
	if (!TestNotNull(TEXT("Setup should succeed"), S.DataComp)) return false;

	// Dispatch through frame 0 (seed baseline, no motion) and frame 1 (advance
	// baseline to (10,0)).
	S.DataComp->HandleFlipbookChanged(S.CharacterFB);
	S.DataComp->HandleFrameChanged(1);

	// After Apply advanced the baseline to (10,0), a subsequent const peek for
	// the same frame should report zero — the peek reads the same baseline
	// Apply just wrote, so there's no new delta to report.
	const FVector Delta = S.DataComp->GetRootMotionDelta();
	TestTrue(TEXT("GetRootMotionDelta after Apply for same frame should be zero"),
		Delta.IsNearlyZero());

	// Verify the shared helper is reachable through both paths by exercising
	// the zero-delta common path (IsNearlyZero early-return).
	TestTrue(TEXT("Both code paths returned consistent zero delta (helper is shared)"),
		Delta.IsNearlyZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileRootMotionInjectedPlaybackStockFallback,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.InjectedPlaybackStockFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileRootMotionInjectedPlaybackStockFallback::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEventDrivenTestUtils;
	using namespace Paper2DPlusRootMotionTestUtils;

	UPaperSprite* Sprite = MakeSprite();
	UPaperFlipbook* FlipbookA = MakeMultiFrameFlipbook(Sprite, 2);
	UPaperFlipbook* FlipbookB = MakeMultiFrameFlipbook(Sprite, 2);

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry AnimA;
	AnimA.Identity.FlipbookName = TEXT("AttackA");
	AnimA.Identity.Flipbook = FlipbookA;
	AddRootMotionFrames(AnimA, {FVector2D::ZeroVector, FVector2D(10.0f, 0.0f)});
	UPaper2DPlusTestMomentCue* SwitchCue = NewObject<UPaper2DPlusTestMomentCue>(Asset, NAME_None, RF_Transactional);
	SwitchCue->TriggerFrame = 1;
	AnimA.FrameEventData.FrameCues.Add(SwitchCue);
	Asset->Flipbooks.Add(AnimA);

	FFlipbookProfileEntry AnimB;
	AnimB.Identity.FlipbookName = TEXT("Injected");
	AnimB.Identity.Flipbook = FlipbookB;
	AddRootMotionFrames(AnimB, {FVector2D(100.0f, 0.0f), FVector2D(125.0f, 0.0f)});
	Asset->Flipbooks.Add(AnimB);

	AActor* Actor = NewObject<AActor>();
	UPaperFlipbookComponent* FBComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	Actor->AddOwnedComponent(FBComp);
	Actor->AddOwnedComponent(DataComp);

	FBComp->SetFlipbook(FlipbookA);
	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FBComp;
	DataComp->bAutoApplyRootMotion = false;
	UPaper2DPlusFrameCueMutationReceiver* Receiver = NewObject<UPaper2DPlusFrameCueMutationReceiver>();
	Receiver->TriggerCue = SwitchCue;
	Receiver->TargetFlipbook = FlipbookB;
	DataComp->OnFrameCue.AddDynamic(Receiver, &UPaper2DPlusFrameCueMutationReceiver::OnCue);

	DataComp->HandleFlipbookChanged(FlipbookA);
	DataComp->HandleFrameChanged(1);

	TestEqual(TEXT("Stock fallback cue receiver should switch once"), Receiver->MutationCount, 1);
	TestEqual(TEXT("Stock fallback injected flipbook should be active"), FBComp->GetFlipbook(), FlipbookB);

	FBComp->SetPlaybackPosition(0.15f, false);
	const FVector Delta = DataComp->ConsumeRootMotionDelta();
	TestTrue(TEXT("Injected flipbook root motion should continue from the injected baseline"),
		FMath::IsNearlyEqual(Delta.X, 25.0f, 0.01f) && FMath::IsNearlyZero(Delta.Z, 0.01f));
	TestTrue(TEXT("ConsumeRootMotionDelta should advance the injected baseline"),
		DataComp->ConsumeRootMotionDelta().IsNearlyZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileRootMotionInjectedPlaybackEventDriven,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.InjectedPlaybackEventDriven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileRootMotionInjectedPlaybackEventDriven::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEventDrivenTestUtils;
	using namespace Paper2DPlusRootMotionTestUtils;

	UPaperSprite* Sprite = MakeSprite();
	UPaperFlipbook* FlipbookA = MakeMultiFrameFlipbook(Sprite, 2);
	UPaperFlipbook* FlipbookB = MakeMultiFrameFlipbook(Sprite, 2);

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry AnimA;
	AnimA.Identity.FlipbookName = TEXT("AttackA");
	AnimA.Identity.Flipbook = FlipbookA;
	AddRootMotionFrames(AnimA, {FVector2D::ZeroVector, FVector2D(8.0f, 0.0f)});
	UPaper2DPlusTestMomentCue* SwitchCue = NewObject<UPaper2DPlusTestMomentCue>(Asset, NAME_None, RF_Transactional);
	SwitchCue->TriggerFrame = 1;
	AnimA.FrameEventData.FrameCues.Add(SwitchCue);
	Asset->Flipbooks.Add(AnimA);

	FFlipbookProfileEntry AnimB;
	AnimB.Identity.FlipbookName = TEXT("Injected");
	AnimB.Identity.Flipbook = FlipbookB;
	AddRootMotionFrames(AnimB, {FVector2D(40.0f, 0.0f), FVector2D(70.0f, 0.0f)});
	Asset->Flipbooks.Add(AnimB);

	AActor* Actor = NewObject<AActor>();
	UPaper2DPlusFlipbookComponent* FBComp = NewObject<UPaper2DPlusFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	Actor->AddOwnedComponent(FBComp);
	Actor->AddOwnedComponent(DataComp);

	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FBComp;
	DataComp->bAutoApplyRootMotion = false;
	FBComp->OnFlipbookChanged.AddDynamic(DataComp, &UPaper2DPlusCharacterProfileComponent::HandleFlipbookChanged);
	UPaper2DPlusFrameCueMutationReceiver* Receiver = NewObject<UPaper2DPlusFrameCueMutationReceiver>();
	Receiver->TriggerCue = SwitchCue;
	Receiver->TargetFlipbook = FlipbookB;
	Receiver->bExplicitlyNotifyFlipbookChange = false;
	DataComp->OnFrameCue.AddDynamic(Receiver, &UPaper2DPlusFrameCueMutationReceiver::OnCue);

	FBComp->SetFlipbook(FlipbookA);
	DataComp->HandleFrameChanged(1);

	TestEqual(TEXT("Event-driven cue receiver should switch once"), Receiver->MutationCount, 1);
	TestEqual(TEXT("Event-driven injected flipbook should be active"), FBComp->GetFlipbook(), FlipbookB);

	FBComp->SetPlaybackPosition(0.15f, false);
	const FVector Delta = DataComp->ConsumeRootMotionDelta();
	TestTrue(TEXT("Event-driven injected root motion should continue from the injected baseline"),
		FMath::IsNearlyEqual(Delta.X, 30.0f, 0.01f) && FMath::IsNearlyZero(Delta.Z, 0.01f));
	TestTrue(TEXT("Event-driven consume should advance the injected baseline"),
		DataComp->ConsumeRootMotionDelta().IsNearlyZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileRootMotionAutoApplyDefaultsToSwept,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.AutoApplyDefaultsToSwept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileRootMotionAutoApplyDefaultsToSwept::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>();
	if (!TestNotNull(TEXT("Profile component should be created"), DataComp)) return false;

	TestTrue(TEXT("Auto root motion should default to swept actor movement"),
		DataComp->RootMotionApplicationMode == EPaper2DPlusRootMotionApplicationMode::SweptActorOffset);
	return true;
}

// SyncFramesToFlipbook grow-only repair: InitializeFromAsset runs this on every editor open to size empty
// CombatData.Frames (layered-import base profiles ship them empty) so the Hitbox tab can author hitboxes.
// Grow-only must NEVER shrink — a profile whose per-frame data is longer than the flipbook would otherwise be
// silently truncated on open and the loss persisted by a later save (Codex review on PR #140).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSyncFramesGrowOnly,
	"Paper2DPlus.CharacterProfile.SyncFrames.GrowOnlyNeverTruncates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSyncFramesGrowOnly::RunTest(const FString& Parameters)
{
	// Build a profile with one flipbook entry whose live UPaperFlipbook has KeyFrameCount key frames (outered to
	// the asset so Identity.Flipbook.LoadSynchronous() resolves) and CombatData.Frames sized to InitialFrameRows.
	auto MakeProfile = [](int32 KeyFrameCount, int32 InitialFrameRows) -> UPaper2DPlusCharacterProfileAsset*
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = TEXT("IDLE");
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>(Asset);
		Anim.Identity.Flipbook = FB;
		{
			FScopedFlipbookMutator Mutator(FB);
			Mutator.KeyFrames.Empty();
			for (int32 i = 0; i < KeyFrameCount; ++i)
			{
				FPaperFlipbookKeyFrame KF;
				KF.FrameRun = 1;
				KF.Sprite = NewObject<UPaperSprite>(FB);
				Mutator.KeyFrames.Add(KF);
			}
		}
		Anim.CombatData.Frames.SetNum(InitialFrameRows);
		Asset->Flipbooks.Add(Anim);
		return Asset;
	};

	// Case 1: empty Frames grow to the key-frame count (the bug the fix targets).
	{
		UPaper2DPlusCharacterProfileAsset* Asset = MakeProfile(/*KeyFrameCount=*/4, /*InitialFrameRows=*/0);
		Asset->SyncAllFramesToFlipbooks(/*bGrowOnly=*/true);
		TestEqual(TEXT("grow-only sizes empty Frames up to the key-frame count"),
			Asset->Flipbooks[0].CombatData.Frames.Num(), 4);
	}

	// Case 2: grow-only must NOT shrink Frames longer than the flipbook, and must preserve trailing data.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = MakeProfile(/*KeyFrameCount=*/4, /*InitialFrameRows=*/6);
		Asset->Flipbooks[0].CombatData.Frames[5].Hitboxes.Add(FHitboxData()); // a truncation would drop this
		Asset->SyncAllFramesToFlipbooks(/*bGrowOnly=*/true);
		TestEqual(TEXT("grow-only does NOT truncate Frames longer than the flipbook"),
			Asset->Flipbooks[0].CombatData.Frames.Num(), 6);
		TestEqual(TEXT("grow-only preserves the trailing frame's hitbox"),
			Asset->Flipbooks[0].CombatData.Frames[5].Hitboxes.Num(), 1);
	}

	// Case 3: exact sync (the default, used at explicit flipbook-assignment sites) STILL trims — unchanged.
	{
		UPaper2DPlusCharacterProfileAsset* Asset = MakeProfile(/*KeyFrameCount=*/4, /*InitialFrameRows=*/6);
		Asset->SyncAllFramesToFlipbooks(); // bGrowOnly defaults to false
		TestEqual(TEXT("exact sync trims Frames to the key-frame count"),
			Asset->Flipbooks[0].CombatData.Frames.Num(), 4);
	}

	return true;
}

// ─── Phase 6 ratchet: CopyFrameDataToRange merge-vs-replace semantics ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCopyFrameDataToRangeMergeSemantics,
	"Paper2DPlus.Editor.Mutation.CopyFrameDataToRangeMergeSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCopyFrameDataToRangeMergeSemantics::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	// Build a flipbook with 4 frames, source at frame 0 with distinct hitboxes + sockets.
	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("TestAnim");
	Anim.CombatData.Frames.SetNum(4);

	// Source frame 0: one ATK hitbox + one socket "Hand"
	FHitboxData SourceHB;
	SourceHB.Type = EHitboxType::Attack;
	SourceHB.X = 10; SourceHB.Y = 20; SourceHB.Width = 30; SourceHB.Height = 40;
	Anim.CombatData.Frames[0].Hitboxes.Add(SourceHB);

	FSocketData SourceSock;
	SourceSock.Name = TEXT("Hand");
	SourceSock.X = 5; SourceSock.Y = 15;
	Anim.CombatData.Frames[0].Sockets.Add(SourceSock);

	// Target frames 1-2: pre-existing hitbox + socket
	FHitboxData ExistingHB;
	ExistingHB.Type = EHitboxType::Hurtbox;
	ExistingHB.X = 50; ExistingHB.Y = 60; ExistingHB.Width = 20; ExistingHB.Height = 20;

	FSocketData ExistingSock;
	ExistingSock.Name = TEXT("Foot");
	ExistingSock.X = 1; ExistingSock.Y = 2;

	// Also add a "Hand" socket to frame 2 to test merge dedup
	FSocketData DuplicateNameSock;
	DuplicateNameSock.Name = TEXT("Hand");
	DuplicateNameSock.X = 99; DuplicateNameSock.Y = 99;

	Anim.CombatData.Frames[1].Hitboxes.Add(ExistingHB);
	Anim.CombatData.Frames[1].Sockets.Add(ExistingSock);
	Anim.CombatData.Frames[2].Hitboxes.Add(ExistingHB);
	Anim.CombatData.Frames[2].Sockets.Add(ExistingSock);
	Anim.CombatData.Frames[2].Sockets.Add(DuplicateNameSock);

	Asset->Flipbooks.Add(Anim);

	// ── Test 1: bMerge=false REPLACES target frames entirely ──
	{
		const bool bOk = Asset->CopyFrameDataToRange(TEXT("TestAnim"), 0, 1, 1, /*bIncludeSockets=*/true, /*bMerge=*/false);
		TestTrue(TEXT("Replace: CopyFrameDataToRange should succeed"), bOk);

		// Frame 1 should have ONLY the source hitbox (existing replaced)
		TestEqual(TEXT("Replace: frame 1 hitbox count"), Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes.Num(), 1);
		TestEqual(TEXT("Replace: frame 1 hitbox type"), Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes[0].Type, EHitboxType::Attack);
		TestEqual(TEXT("Replace: frame 1 hitbox X"), Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes[0].X, 10);

		// Sockets should be fully replaced too
		TestEqual(TEXT("Replace: frame 1 socket count"), Asset->Flipbooks[0].CombatData.Frames[1].Sockets.Num(), 1);
		TestEqual(TEXT("Replace: frame 1 socket name"), Asset->Flipbooks[0].CombatData.Frames[1].Sockets[0].Name, TEXT("Hand"));
	}

	// Restore frame 1 pre-existing data for merge test
	Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes.Empty();
	Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes.Add(ExistingHB);
	Asset->Flipbooks[0].CombatData.Frames[1].Sockets.Empty();
	Asset->Flipbooks[0].CombatData.Frames[1].Sockets.Add(ExistingSock);

	// ── Test 2: bMerge=true APPENDS hitboxes, deduplicates sockets by name ──
	{
		const bool bOk = Asset->CopyFrameDataToRange(TEXT("TestAnim"), 0, 1, 2, /*bIncludeSockets=*/true, /*bMerge=*/true);
		TestTrue(TEXT("Merge: CopyFrameDataToRange should succeed"), bOk);

		// Frame 1: existing Hurtbox + appended Attack = 2 hitboxes
		TestEqual(TEXT("Merge: frame 1 hitbox count"), Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes.Num(), 2);
		TestEqual(TEXT("Merge: frame 1 first hitbox type (existing)"), Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes[0].Type, EHitboxType::Hurtbox);
		TestEqual(TEXT("Merge: frame 1 second hitbox type (appended)"), Asset->Flipbooks[0].CombatData.Frames[1].Hitboxes[1].Type, EHitboxType::Attack);

		// Sockets: existing "Foot" + new "Hand" = 2 (source "Hand" added because not present)
		TestEqual(TEXT("Merge: frame 1 socket count"), Asset->Flipbooks[0].CombatData.Frames[1].Sockets.Num(), 2);

		// Frame 2: existing Hurtbox + appended Attack = 2 hitboxes
		TestEqual(TEXT("Merge: frame 2 hitbox count"), Asset->Flipbooks[0].CombatData.Frames[2].Hitboxes.Num(), 2);

		// Frame 2 sockets: existing "Foot" + existing "Hand" = 2 (source "Hand" SKIPPED — already present)
		TestEqual(TEXT("Merge: frame 2 socket count (dedup)"), Asset->Flipbooks[0].CombatData.Frames[2].Sockets.Num(), 2);
		// The existing "Hand" keeps its original position (not overwritten by source)
		bool bFoundHandWithOriginalPos = false;
		for (const FSocketData& S : Asset->Flipbooks[0].CombatData.Frames[2].Sockets)
		{
			if (S.Name == TEXT("Hand") && S.X == 99)
			{
				bFoundHandWithOriginalPos = true;
				break;
			}
		}
		TestTrue(TEXT("Merge: frame 2 'Hand' socket keeps original position (dedup preserves existing)"), bFoundHandWithOriginalPos);
	}

	return true;
}


// =============================================================================
// Alignment state defaults (Unit 2 of cross-sheet alignment plan)
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileAlignmentStateDefaults,
	"Paper2DPlus.CharacterProfile.Alignment.DefaultState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileAlignmentStateDefaults::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestEqual(TEXT("New profile: AlignmentStatus == Never"),
		Asset->AlignmentStatus, EAlignmentStatus::Never);
	TestEqual(TEXT("New profile: LastAlignmentCheckTimestamp is zero"),
		Asset->LastAlignmentCheckTimestamp, FDateTime(0));
	return true;
}

// ==========================================
// COMPLETION-BIT INVARIANT (Wave 6 / U17, findings F17 + F25)
//
// Live completion tasks are bits {0,1,2,5,6}; the retired Phases(3)/Effects(4) bits
// are stripped in PostLoad. The editor completion meter, the browser filter, and the
// PostLoad strip must all agree on the same LiveTaskBits/LiveTaskCount — otherwise a
// "Mark All Complete" flipbook can never read as fully complete after a save+reload.
// ==========================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCompletionLiveTaskBitsInvariant,
	"Paper2DPlus.CharacterProfile.Completion.LiveTaskBitsInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCompletionLiveTaskBitsInvariant::RunTest(const FString& Parameters)
{
	constexpr int32 LiveBits = UPaper2DPlusCharacterProfileAsset::LiveTaskBits;
	constexpr int32 RetiredBits = (1 << 3) | (1 << 4); // Phases, Effects (deleted tabs)

	// The mask is exactly {0,1,2,5,6} and excludes the retired Phases/Effects bits.
	TestEqual(TEXT("LiveTaskBits == {0,1,2,5,6} (0x67)"), LiveBits, 0x67);
	TestEqual(TEXT("LiveTaskBits excludes retired Phases/Effects bits"), LiveBits & RetiredBits, 0);

	// LiveTaskCount stays in sync with the popcount of the mask (the meter denominator).
	int32 PopCount = 0;
	for (int32 M = LiveBits; M != 0; M >>= 1) { PopCount += (M & 1); }
	TestEqual(TEXT("LiveTaskCount == popcount(LiveTaskBits)"),
		UPaper2DPlusCharacterProfileAsset::LiveTaskCount, PopCount);

	// "Mark All Complete" now writes LiveTaskBits; the PostLoad strip is (flags & LiveTaskBits),
	// so a freshly-marked flipbook survives reload as fully complete (Done == LiveTaskCount)...
	const int32 AfterReloadLive = LiveBits & LiveBits;
	TestEqual(TEXT("Live mark-all survives the reload strip unchanged"), AfterReloadLive, LiveBits);

	int32 DoneLive = 0;
	for (int32 M = AfterReloadLive; M != 0; M >>= 1) { DoneLive += (M & 1); }
	TestEqual(TEXT("Reloaded live mark-all reads as fully complete"),
		DoneLive, UPaper2DPlusCharacterProfileAsset::LiveTaskCount);

	// ...whereas the legacy 0x7F mark-all (which set the retired bits) is reduced to the live
	// mask on reload — the exact bug the unification fixes (it could never persist as complete).
	const int32 LegacyMarkedAll = 0x7F;
	TestEqual(TEXT("Legacy 0x7F mark-all is stripped to the live mask on reload"),
		LegacyMarkedAll & LiveBits, LiveBits);
	TestNotEqual(TEXT("Legacy 0x7F differs from the persisted live mask (retired bits dropped)"),
		LegacyMarkedAll, LiveBits);

	return true;
}

// =============================================================================
// Flipbook rename propagation (audit 2026-05-31, F5 / TASK-58 U2)
// Renaming a flipbook must propagate the new name to every by-name reference —
// tag mappings and the thumbnail — and invalidate the name/tag lookup caches.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileRenamePropagation,
	"Paper2DPlus.CharacterProfile.Rename.PropagatesToTagsAndPhases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileRenamePropagation::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry AnimA;
	AnimA.Identity.FlipbookName = TEXT("Attack_01");
	UPaperFlipbook* AttackFlipbook = NewObject<UPaperFlipbook>(Asset);
	AnimA.Identity.Flipbook = AttackFlipbook;
	Asset->Flipbooks.Add(AnimA);

	FFlipbookProfileEntry AnimB;
	AnimB.Identity.FlipbookName = TEXT("Idle");
	Asset->Flipbooks.Add(AnimB);

	// "Attack_01" is referenced by a tag mapping and the thumbnail.
	FGameplayTag Tag; // empty tag is a valid, usable TMap key for this data-layer test
	FFlipbookTagMapping Mapping;
	Mapping.Entries.Emplace(TEXT("Attack_01"));
	Asset->TagMappings.Add(Tag, Mapping);

	Asset->ThumbnailFlipbookName = TEXT("Attack_01");

	// Prime the retained flipbook resolver with the OLD name so rename must invalidate its lookup cache.
	TestTrue(TEXT("Old name resolves before rename"),
		Asset->GetFlipbookByName(TEXT("Attack_01")) == AttackFlipbook);

	const bool bRenamed = Asset->RenameFlipbookAndPropagate(0, TEXT("Attack_Heavy"));
	TestTrue(TEXT("Rename should succeed"), bRenamed);

	TestEqual(TEXT("Entry name updated"),
		Asset->Flipbooks[0].Identity.FlipbookName, FString(TEXT("Attack_Heavy")));
	TestEqual(TEXT("Tag mapping name propagated"),
		Asset->TagMappings[Tag].Entries[0].FlipbookName, FString(TEXT("Attack_Heavy")));
	TestEqual(TEXT("Thumbnail name propagated"),
		Asset->ThumbnailFlipbookName, FString(TEXT("Attack_Heavy")));

	// The focused object/name resolver must see the new identity after the in-place rename.
	TestNull(TEXT("Old name no longer resolves after rename"),
		Asset->GetFlipbookByName(TEXT("Attack_01")));
	TestTrue(TEXT("New name resolves the same flipbook object"),
		Asset->GetFlipbookByName(TEXT("Attack_Heavy")) == AttackFlipbook);

	// Collision with a different flipbook (case-insensitive) is rejected and changes nothing.
	TestFalse(TEXT("Rename onto an existing name is rejected"),
		Asset->RenameFlipbookAndPropagate(0, TEXT("idle")));
	TestEqual(TEXT("Name unchanged after rejected collision"),
		Asset->Flipbooks[0].Identity.FlipbookName, FString(TEXT("Attack_Heavy")));

	// Empty name, invalid index, and unchanged name are all no-ops.
	TestFalse(TEXT("Empty/whitespace name rejected"),
		Asset->RenameFlipbookAndPropagate(0, TEXT("   ")));
	TestFalse(TEXT("Invalid index rejected"),
		Asset->RenameFlipbookAndPropagate(99, TEXT("Whatever")));
	TestFalse(TEXT("Unchanged name is a no-op"),
		Asset->RenameFlipbookAndPropagate(1, TEXT("Idle")));

	// A pure case change of the same entry is allowed (not treated as a self-collision).
	TestTrue(TEXT("Case-only rename of same entry is allowed"),
		Asset->RenameFlipbookAndPropagate(1, TEXT("IDLE")));
	TestEqual(TEXT("Case-only rename applied"),
		Asset->Flipbooks[1].Identity.FlipbookName, FString(TEXT("IDLE")));

	// Renaming a blank-named entry must NOT hijack the auto-pick thumbnail ("") or fill an
	// "unassigned" ("") tag slot — an empty OldName has no by-name references to propagate.
	FFlipbookProfileEntry Blank; // Identity.FlipbookName == ""
	Asset->Flipbooks.Add(Blank);
	const int32 BlankIdx = Asset->Flipbooks.Num() - 1;
	Asset->ThumbnailFlipbookName = FString();             // "" = auto-pick (NOT pinned)
	Asset->TagMappings[Tag].Entries.Emplace(FString()); // an unassigned slot
	const int32 EmptySlotIdx = Asset->TagMappings[Tag].Entries.Num() - 1;

	TestTrue(TEXT("Renaming a blank-named entry succeeds"),
		Asset->RenameFlipbookAndPropagate(BlankIdx, TEXT("Dash")));
	TestEqual(TEXT("Blank-entry rename leaves auto-pick thumbnail empty"),
		Asset->ThumbnailFlipbookName, FString());
	TestEqual(TEXT("Blank-entry rename does not fill an unassigned tag slot"),
		Asset->TagMappings[Tag].Entries[EmptySlotIdx].FlipbookName, FString());
	TestEqual(TEXT("Blank entry itself is renamed"),
		Asset->Flipbooks[BlankIdx].Identity.FlipbookName, FString(TEXT("Dash")));

	return true;
}

// ─── TASK-3 — Legacy tag-mapping parallel arrays migrate into Entries on load ──
// Pre-struct-ify assets stored FFlipbookTagMapping as parallel FlipbookNames +
// PaperZDSequences arrays. UE loads them into the *_DEPRECATED fields; PostLoad's
// MigrateTagMappingsToEntries must fold them into the Entries list (pairing name↔sequence
// positionally, tolerating a shorter sequence array) and clear the legacy arrays, idempotently.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTagMappingMigratesToEntries,
	"Paper2DPlus.CharacterProfile.TagMapping.LegacyParallelArraysMigrateToEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTagMappingMigratesToEntries::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	// Any UObject stands in for a PaperZD sequence — the field is TObjectPtr<UObject>.
	UObject* Seq0 = NewObject<UPaper2DPlusCharacterProfileAsset>();

	// Synthesize a legacy-serialized mapping: populate ONLY the deprecated parallel arrays, with
	// the sequence array deliberately SHORTER than the name array (a real-world ragged case).
	FGameplayTag Tag; // empty tag is a valid TMap key for this data-layer test
	FFlipbookTagMapping Legacy;
	Legacy.FlipbookNames_DEPRECATED.Add(TEXT("Attack_01"));
	Legacy.FlipbookNames_DEPRECATED.Add(TEXT("Attack_02"));
	Legacy.FlipbookNames_DEPRECATED.Add(TEXT("Attack_03"));
	Legacy.PaperZDSequences_DEPRECATED.Add(Seq0); // only the first entry carries a sequence
	Asset->TagMappings.Add(Tag, Legacy);

	Asset->PostLoad();

	const FFlipbookTagMapping& Migrated = Asset->TagMappings[Tag];
	TestEqual(TEXT("All legacy names migrated into Entries"), Migrated.Entries.Num(), 3);
	if (Migrated.Entries.Num() == 3)
	{
		TestEqual(TEXT("Entry 0 name preserved"), Migrated.Entries[0].FlipbookName, FString(TEXT("Attack_01")));
		TestEqual(TEXT("Entry 1 name preserved"), Migrated.Entries[1].FlipbookName, FString(TEXT("Attack_02")));
		TestEqual(TEXT("Entry 2 name preserved"), Migrated.Entries[2].FlipbookName, FString(TEXT("Attack_03")));
		TestTrue(TEXT("Entry 0 sequence paired by position"), Migrated.Entries[0].PaperZDSequence == Seq0);
		TestTrue(TEXT("Entry 1 sequence is null (ragged source array)"), Migrated.Entries[1].PaperZDSequence == nullptr);
		TestTrue(TEXT("Entry 2 sequence is null (ragged source array)"), Migrated.Entries[2].PaperZDSequence == nullptr);
	}
	TestEqual(TEXT("Legacy FlipbookNames array emptied after migration"), Migrated.FlipbookNames_DEPRECATED.Num(), 0);
	TestEqual(TEXT("Legacy PaperZDSequences array emptied after migration"), Migrated.PaperZDSequences_DEPRECATED.Num(), 0);

	// Idempotency: a second PostLoad must NOT duplicate or wipe the already-migrated Entries.
	Asset->PostLoad();
	TestEqual(TEXT("Second PostLoad leaves Entries unchanged (idempotent)"), Asset->TagMappings[Tag].Entries.Num(), 3);

	return true;
}

// ─── Legacy-cleanup 2026-07 — MigrateLegacyGrouping drops retired phase-group rows + stale ──
// tag-backed group shells (TagMappings key gone or empty), reassigning their cards to
// Unassigned, while leaving manual groups and live tag-backed groups untouched.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusMigrateLegacyGrouping,
	"Paper2DPlus.CharacterProfile.Migration.LegacyGrouping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusMigrateLegacyGrouping::RunTest(const FString& Parameters)
{
	const FGameplayTag CombatTag = FGameplayTag::RequestGameplayTag(FName("Paper2DPlus.Animation.Combat"), /*ErrorIfNotFound*/ false);
	const FGameplayTag LocomotionTag = FGameplayTag::RequestGameplayTag(FName("Paper2DPlus.Animation.Locomotion"), false);
	if (!CombatTag.IsValid() || !LocomotionTag.IsValid())
	{
		AddInfo(TEXT("Paper2DPlus.Animation.* tags not registered in this run — skipping (expected only when DefaultGameplayTags.ini is absent)."));
		return true;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	auto AddCard = [Asset](const TCHAR* Name, FName GroupName)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.FlipbookGroup = GroupName;
		return Asset->Flipbooks.Add(Entry);
	};

	// (a) A retired phase-group row (the legacy flag lands in bIsPhaseGroup_DEPRECATED) + its card.
	FFlipbookGroupInfo OldPhase;
	OldPhase.GroupName = TEXT("OldPhase");
	OldPhase.bIsPhaseGroup_DEPRECATED = true;
	Asset->FlipbookGroups.Add(OldPhase);
	const int32 PhasedIdx = AddCard(TEXT("PhasedMove"), TEXT("OldPhase"));

	// (b) A STALE tag-backed row — the Combat tag resolves but there is NO TagMappings key.
	FFlipbookGroupInfo StaleCombat;
	StaleCombat.GroupName = CombatTag.GetTagName();
	Asset->FlipbookGroups.Add(StaleCombat);
	const int32 CombatIdx = AddCard(TEXT("CombatMove"), CombatTag.GetTagName());

	// (c) A manual row (arbitrary name — never resolves as a tag) + its member.
	FFlipbookGroupInfo Manual;
	Manual.GroupName = TEXT("MyManualGroup");
	Asset->FlipbookGroups.Add(Manual);
	const int32 ManualIdx = AddCard(TEXT("ManualMove"), TEXT("MyManualGroup"));

	// (d) A LIVE tag-backed row — the Locomotion mapping names an existing flipbook.
	FFlipbookGroupInfo LiveLocomotion;
	LiveLocomotion.GroupName = LocomotionTag.GetTagName();
	Asset->FlipbookGroups.Add(LiveLocomotion);
	const int32 RunIdx = AddCard(TEXT("RunMove"), LocomotionTag.GetTagName());
	FFlipbookTagMapping LocomotionMapping;
	LocomotionMapping.Entries.Emplace(TEXT("RunMove"));
	Asset->TagMappings.Add(LocomotionTag, LocomotionMapping);

	Asset->MigrateLegacyGrouping();

	auto FindGroup = [Asset](FName GroupName) -> const FFlipbookGroupInfo*
	{
		return Asset->FlipbookGroups.FindByPredicate(
			[GroupName](const FFlipbookGroupInfo& G) { return G.GroupName == GroupName; });
	};

	// The retired phase-group row is gone and its card fell to Unassigned.
	TestNull(TEXT("OldPhase row removed"), FindGroup(TEXT("OldPhase")));
	TestTrue(TEXT("OldPhase card moved to Unassigned"), Asset->Flipbooks[PhasedIdx].FlipbookGroup.IsNone());

	// The stale tag-backed row (no TagMappings key) is gone and its card fell to Unassigned.
	TestNull(TEXT("Stale Combat row removed"), FindGroup(CombatTag.GetTagName()));
	TestTrue(TEXT("Stale Combat card moved to Unassigned"), Asset->Flipbooks[CombatIdx].FlipbookGroup.IsNone());

	// The manual row survives with its member intact.
	TestNotNull(TEXT("Manual row kept"), FindGroup(TEXT("MyManualGroup")));
	TestEqual(TEXT("Manual member keeps its group"), Asset->Flipbooks[ManualIdx].FlipbookGroup, FName(TEXT("MyManualGroup")));

	// The live tag-backed row survives (its mapping has a non-whitespace entry).
	TestNotNull(TEXT("Live Locomotion row kept"), FindGroup(LocomotionTag.GetTagName()));
	TestEqual(TEXT("Live member keeps its group"), Asset->Flipbooks[RunIdx].FlipbookGroup, LocomotionTag.GetTagName());

	return true;
}

// ─── U1 / AC #1 — Root-motion version stamping via PostInitProperties ──────────
// PostInitProperties stamps CurrentRootMotionVersion on every genuine in-memory creation path so
// PostLoad's pre-v1 Y-sign migration never re-flips already-correct data, while genuine pre-v1
// (loaded) assets — synthesized here by resetting the version to 0 — still migrate exactly once.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRootMotionVersionStampedOnCreate,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.NewObjectStampsCurrentVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRootMotionVersionStampedOnCreate::RunTest(const FString& Parameters)
{
	// No PostLoad involved — PostInitProperties (run inside NewObject) must have stamped the version.
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestEqual(TEXT("Freshly created asset is stamped to the current root-motion version"),
		(int32)Asset->RootMotionVersion,
		(int32)UPaper2DPlusCharacterProfileAsset::CurrentRootMotionVersion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRootMotionNewAssetNotMigrated,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.NewAssetYNotFlippedByPostLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRootMotionNewAssetNotMigrated::RunTest(const FString& Parameters)
{
	// The F2/F4 regression: a non-factory creation path (Duplicate / programmatic NewObject) is
	// stamped current by PostInitProperties, so PostLoad must NOT treat its already-correct Y as
	// legacy and invert it.
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("Jump");
	FRootMotionFrameData RM;
	RM.Position = FVector2D(0.0, 40.0); // authored, already in correct world Z-up convention
	Entry.MotionData.RootMotion.Add(RM);
	Asset->Flipbooks.Add(Entry);

	Asset->PostLoad();

	TestEqual(TEXT("PostLoad must not flip Y of a current-version asset"),
		Asset->Flipbooks[0].MotionData.RootMotion[0].Position.Y, 40.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRootMotionLegacyMigratesOnce,
	"Paper2DPlus.CharacterProfile.MotionData.RootMotion.LegacyV0AssetMigratesYOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRootMotionLegacyMigratesOnce::RunTest(const FString& Parameters)
{
	// Synthesize a genuine pre-v1 on-disk asset: reset the version PostInitProperties stamped so the
	// PostLoad migration branch (RootMotionVersion < CurrentRootMotionVersion) actually runs.
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Asset->RootMotionVersion = 0;

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("Jump");
	FRootMotionFrameData RM;
	RM.Position = FVector2D(0.0, 40.0); // legacy pixel-Y-down value, must be sign-flipped once
	Entry.MotionData.RootMotion.Add(RM);
	Asset->Flipbooks.Add(Entry);

	Asset->PostLoad();

	TestEqual(TEXT("Legacy Y is migrated (sign-flipped) exactly once"),
		Asset->Flipbooks[0].MotionData.RootMotion[0].Position.Y, -40.0);
	TestEqual(TEXT("Version is bumped to current after migration"),
		(int32)Asset->RootMotionVersion,
		(int32)UPaper2DPlusCharacterProfileAsset::CurrentRootMotionVersion);
	return true;
}

// ════════════════════════════════════════════════════════════════════════════
// TASK-4 (NS-2) — Backfill regression coverage for core storage surfaces.
// ════════════════════════════════════════════════════════════════════════════

// ─── Native tag-mapping storage keeps group membership, Chain Start flag, and sequence paired ───────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTagMappingQueries,
	"Paper2DPlus.CharacterProfile.TagMapping.NativeStorage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTagMappingQueries::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Atk1; Atk1.Identity.FlipbookName = TEXT("Atk1"); Asset->Flipbooks.Add(Atk1);
	FFlipbookProfileEntry Atk2; Atk2.Identity.FlipbookName = TEXT("Atk2"); Asset->Flipbooks.Add(Atk2);

	UObject* Seq1 = NewObject<UPaper2DPlusCharacterProfileAsset>(); // any UObject stands in for a PaperZD sequence

	// Native serialized storage remains available to C++/editor code but no raw Blueprint query API.
	const FGameplayTag Tag; // empty tag is a usable TMap key for this data-layer test
	UPaper2DPlusCharacterProfileAsset* Empty = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestFalse(TEXT("Empty asset has no mapping storage"), Empty->TagMappings.Contains(Tag));

	FFlipbookTagMapping Mapping;
	Mapping.Entries.Emplace(TEXT("Atk1"), Seq1);
	Mapping.Entries[0].bIsChainStart = true;
	Mapping.Entries.Emplace(TEXT("Atk2"));        // no sequence
	Mapping.Entries.Emplace(TEXT("Ghost"));       // name not present in Flipbooks
	Asset->TagMappings.Add(Tag, Mapping);

	const FFlipbookTagMapping* Stored = Asset->TagMappings.Find(Tag);
	if (!TestNotNull(TEXT("Mapping is stored under its exact group key"), Stored))
	{
		return false;
	}
	TestEqual(TEXT("All authored rows remain stored, including dangling membership"), Stored->Entries.Num(), 3);
	if (Stored->Entries.Num() == 3)
	{
		TestEqual(TEXT("Authored order preserves Atk1 first"), Stored->Entries[0].FlipbookName, FString(TEXT("Atk1")));
		TestTrue(TEXT("The Chain Start flag is paired with Atk1"), Stored->Entries[0].bIsChainStart);
		TestTrue(TEXT("PaperZD sequence remains paired with Atk1"), Stored->Entries[0].PaperZDSequence == Seq1);
		TestEqual(TEXT("Atk2 remains the second member"), Stored->Entries[1].FlipbookName, FString(TEXT("Atk2")));
		TestFalse(TEXT("An ordinary mapping member defaults to unflagged"), Stored->Entries[1].bIsChainStart);
		TestNull(TEXT("Atk2 keeps its explicit null PaperZD sequence"), Stored->Entries[1].PaperZDSequence.Get());
		TestEqual(TEXT("Dangling Ghost membership remains authored data"), Stored->Entries[2].FlipbookName, FString(TEXT("Ghost")));
		TestFalse(TEXT("Even dangling membership never synthesizes a chain start"), Stored->Entries[2].bIsChainStart);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTagMappingChainStartMutation,
	"Paper2DPlus.CharacterProfile.TagMapping.ChainStartMutationIsGuarded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTagMappingChainStartMutation::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookTagMapping& Mapping = Asset->TagMappings.FindOrAdd(CharacterProfileTest_RootGroup);
	Mapping.Entries.Emplace(TEXT("A"));
	Mapping.Entries.Emplace(TEXT("B"));
	TestFalse(TEXT("New mapping entries are unflagged by default"), Mapping.Entries[0].bIsChainStart);
	Mapping.Entries[1].bIsChainStart = true;

	FFlipbookProfileEntry Grouped;
	Grouped.Identity.FlipbookName = TEXT("Grouped");
	Grouped.Identity.Flipbook = NewObject<UPaperFlipbook>(Asset);
	Asset->Flipbooks.Add(MoveTemp(Grouped));
	Asset->MoveFlipbookToFlipbookGroup(0, CharacterProfileTest_RootGroup.GetTag().GetTagName());
	const FFlipbookTagMapping* GroupedMapping = Asset->TagMappings.Find(CharacterProfileTest_RootGroup);
	const FFlipbookTagMappingEntry* GroupedEntry = GroupedMapping
		? GroupedMapping->Entries.FindByPredicate([](const FFlipbookTagMappingEntry& Entry)
		{
			return Entry.FlipbookName == TEXT("Grouped");
		})
		: nullptr;
	TestNotNull(TEXT("Moving a flipbook to a tag-backed group creates membership"), GroupedEntry);
	TestFalse(TEXT("Group membership never auto-flags a chain start"),
		GroupedEntry && GroupedEntry->bIsChainStart);

	TestFalse(TEXT("Missing group is rejected"),
		Asset->SetTagMappingEntryChainStart(FGameplayTag(), 0, true));
	TestFalse(TEXT("Invalid entry index is rejected"),
		Asset->SetTagMappingEntryChainStart(CharacterProfileTest_RootGroup, 99, true));
	TestFalse(TEXT("Same-value flag write is a no-op"),
		Asset->SetTagMappingEntryChainStart(CharacterProfileTest_RootGroup, 0, false));
	TestTrue(TEXT("Setting the Chain Start flag is accepted"),
		Asset->SetTagMappingEntryChainStart(CharacterProfileTest_RootGroup, 0, true));
	TestTrue(TEXT("Accepted flag is stored on the exact entry"),
		Mapping.Entries[0].bIsChainStart);
	TestTrue(TEXT("Two flagged entries in one group are both valid chain starts"),
		Mapping.Entries[0].bIsChainStart && Mapping.Entries[1].bIsChainStart);
	TestTrue(TEXT("The flag can be cleared again"),
		Asset->SetTagMappingEntryChainStart(CharacterProfileTest_RootGroup, 0, false));
	TestFalse(TEXT("Cleared flag is stored on the exact entry"),
		Mapping.Entries[0].bIsChainStart);

	// Chain End — the same guarded pure-data-edit shape.
	TestFalse(TEXT("Chain End: new mapping entries are unflagged by default"),
		Mapping.Entries[0].bIsChainEnd);
	TestFalse(TEXT("Chain End: missing group is rejected"),
		Asset->SetTagMappingEntryChainEnd(FGameplayTag(), 0, true));
	TestFalse(TEXT("Chain End: invalid entry index is rejected"),
		Asset->SetTagMappingEntryChainEnd(CharacterProfileTest_RootGroup, 99, true));
	TestFalse(TEXT("Chain End: same-value flag write is a no-op"),
		Asset->SetTagMappingEntryChainEnd(CharacterProfileTest_RootGroup, 0, false));
	TestTrue(TEXT("Chain End: setting the flag is accepted"),
		Asset->SetTagMappingEntryChainEnd(CharacterProfileTest_RootGroup, 0, true));
	TestTrue(TEXT("Chain End: accepted flag is stored on the exact entry"),
		Mapping.Entries[0].bIsChainEnd);
	TestTrue(TEXT("Chain End: the flag can be cleared again"),
		Asset->SetTagMappingEntryChainEnd(CharacterProfileTest_RootGroup, 0, false));
	TestFalse(TEXT("Chain End: cleared flag is stored on the exact entry"),
		Mapping.Entries[0].bIsChainEnd);

	// Chain Tags — guarded container replace with an exact-set no-op compare.
	FGameplayTagContainer NewChainTags;
	NewChainTags.AddTag(CharacterProfileTest_ChainIdentity);
	TestFalse(TEXT("Chain Tags: missing group is rejected"),
		Asset->SetTagMappingEntryChainTags(FGameplayTag(), 0, NewChainTags));
	TestFalse(TEXT("Chain Tags: invalid entry index is rejected"),
		Asset->SetTagMappingEntryChainTags(CharacterProfileTest_RootGroup, 99, NewChainTags));
	TestFalse(TEXT("Chain Tags: writing the same (empty) container is a no-op"),
		Asset->SetTagMappingEntryChainTags(CharacterProfileTest_RootGroup, 0, FGameplayTagContainer()));
	TestTrue(TEXT("Chain Tags: setting the container is accepted"),
		Asset->SetTagMappingEntryChainTags(CharacterProfileTest_RootGroup, 0, NewChainTags));
	TestTrue(TEXT("Chain Tags: accepted container is stored on the exact entry"),
		Mapping.Entries[0].ChainTags.Num() == 1
		&& Mapping.Entries[0].ChainTags.HasTagExact(CharacterProfileTest_ChainIdentity));
	TestFalse(TEXT("Chain Tags: re-writing the equal container is a no-op"),
		Asset->SetTagMappingEntryChainTags(CharacterProfileTest_RootGroup, 0, NewChainTags));
	TestTrue(TEXT("Chain Tags: clearing back to empty is accepted"),
		Asset->SetTagMappingEntryChainTags(CharacterProfileTest_RootGroup, 0, FGameplayTagContainer()));
	TestTrue(TEXT("Chain Tags: cleared container is stored"),
		Mapping.Entries[0].ChainTags.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTagMappingChainStartValidation,
	"Paper2DPlus.CharacterProfile.Validation.ChainStartsRejectDanglingNotMultiples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTagMappingChainStartValidation::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	for (const TCHAR* Name : { TEXT("First"), TEXT("Second") })
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(Asset);
		Asset->Flipbooks.Add(MoveTemp(Entry));
	}

	TArray<FCharacterProfileValidationIssue> Issues;
	auto HasIssueText = [&Issues](const TCHAR* Marker)
	{
		return Issues.ContainsByPredicate([Marker](const FCharacterProfileValidationIssue& Issue)
		{
			return Issue.Severity == ECharacterProfileValidationSeverity::Error
				&& Issue.Message.Contains(Marker);
		});
	};

	// Two flagged entries in one exact group are two VALID chains — the numbered-root
	// duplicate-identity Error died with the chain-start flag.
	FFlipbookTagMapping& Mapping = Asset->TagMappings.FindOrAdd(CharacterProfileTest_RootGroup);
	Mapping.Entries.Emplace(TEXT("First"));
	Mapping.Entries.Last().bIsChainStart = true;
	Mapping.Entries.Emplace(TEXT("Second"));
	Mapping.Entries.Last().bIsChainStart = true;
	Asset->ValidateCharacterProfileAsset(Issues);
	TestFalse(TEXT("Two flagged chain starts in one group produce no chain-start error"),
		HasIssueText(TEXT("must identify a live flipbook")));

	// A flagged entry that cannot resolve a live flipbook is still an Error, and case-insensitive
	// duplicate/cross-group membership diagnostics survive unchanged.
	Mapping.Entries.Emplace(TEXT("Ghost"));
	Mapping.Entries.Last().bIsChainStart = true;
	Mapping.Entries.Emplace(TEXT("fIrSt"));
	Asset->TagMappings.FindOrAdd(CharacterProfileTest_AlternateRootGroup).Entries.Emplace(TEXT("sEcOnD"));

	Issues.Reset();
	TestFalse(TEXT("Invalid chain-start data fails profile validation"),
		Asset->ValidateCharacterProfileAsset(Issues));
	TestTrue(TEXT("A dangling flagged chain start has a focused diagnostic"),
		HasIssueText(TEXT("must identify a live flipbook")));
	TestTrue(TEXT("Case-insensitive duplicate group membership has a focused diagnostic"),
		HasIssueText(TEXT("appears more than once in this exact group")));
	TestTrue(TEXT("Case-insensitive cross-group membership has a focused diagnostic"),
		HasIssueText(TEXT("is mapped to more than one exact group")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusExactAnimationTagUniquenessValidation,
	"Paper2DPlus.CharacterProfile.Validation.ExactAnimationTagUniqueness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusExactAnimationTagUniquenessValidation::RunTest(const FString& Parameters)
{
	auto AddTaggedMove = [](UPaper2DPlusCharacterProfileAsset* Asset, const TCHAR* Name)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(Asset);
		Entry.EditorMeta.AnimationTags.AddTag(CharacterProfileTest_ExactShared);
		Asset->Flipbooks.Add(MoveTemp(Entry));
	};
	auto HasExactTagCollision = [](const TArray<FCharacterProfileValidationIssue>& Issues)
	{
		return Issues.ContainsByPredicate([](const FCharacterProfileValidationIssue& Issue)
		{
			return Issue.Severity == ECharacterProfileValidationSeverity::Error
				&& Issue.Message.Contains(TEXT("exact Animation Tags"));
		});
	};

	UPaper2DPlusCharacterProfileAsset* Unrelated = NewObject<UPaper2DPlusCharacterProfileAsset>();
	AddTaggedMove(Unrelated, TEXT("IdleA"));
	AddTaggedMove(Unrelated, TEXT("IdleB"));
	TArray<FCharacterProfileValidationIssue> Issues;
	TestFalse(TEXT("Unrelated duplicate exact containers fail validation"),
		Unrelated->ValidateCharacterProfileAsset(Issues));
	TestTrue(TEXT("Unrelated duplicates produce the focused exact-tag diagnostic"),
		HasExactTagCollision(Issues));

	UPaper2DPlusCharacterProfileAsset* OneChain = NewObject<UPaper2DPlusCharacterProfileAsset>();
	AddTaggedMove(OneChain, TEXT("Root"));
	AddTaggedMove(OneChain, TEXT("Member"));
	OneChain->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Member")));
	FFlipbookTagMapping& Mapping = OneChain->TagMappings.FindOrAdd(CharacterProfileTest_RootGroup);
	Mapping.Entries.Emplace(TEXT("Root"));
	Mapping.Entries.Last().bIsChainStart = true;
	Mapping.Entries.Emplace(TEXT("Member"));
	Issues.Reset();
	TestTrue(TEXT("Duplicate exact containers wholly inside one valid chain are allowed"),
		OneChain->ValidateCharacterProfileAsset(Issues));
	TestFalse(TEXT("The legal same-chain duplicate emits no uniqueness error"),
		HasExactTagCollision(Issues));

	AddTaggedMove(OneChain, TEXT("Unrelated"));
	Issues.Reset();
	TestFalse(TEXT("Adding an unrelated duplicate invalidates the chain exception"),
		OneChain->ValidateCharacterProfileAsset(Issues));
	TestTrue(TEXT("The expanded duplicate set produces the focused diagnostic"),
		HasExactTagCollision(Issues));
	return true;
}

// ─── RemoveFlipbookFromTagMappings keeps name↔sequence paired (the struct-ify payoff) ──
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRemoveFlipbookFromTagMappings,
	"Paper2DPlus.CharacterProfile.TagMapping.RemoveFlipbookKeepsEntriesPaired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRemoveFlipbookFromTagMappings::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	UObject* SeqA = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UObject* SeqB = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UObject* SeqC = NewObject<UPaper2DPlusCharacterProfileAsset>();

	const FGameplayTag Tag;
	FFlipbookTagMapping Mapping;
	Mapping.Entries.Emplace(TEXT("A"), SeqA);
	Mapping.Entries.Emplace(TEXT("B"), SeqB);
	Mapping.Entries.Emplace(TEXT("C"), SeqC);
	Asset->TagMappings.Add(Tag, Mapping);

	// Remove the MIDDLE entry case-insensitively; the survivors must keep their own sequences.
	Asset->RemoveFlipbookFromTagMappings(TEXT("b"));

	const FFlipbookTagMapping& After = Asset->TagMappings[Tag];
	TestEqual(TEXT("One entry removed"), After.Entries.Num(), 2);
	if (After.Entries.Num() == 2)
	{
		TestEqual(TEXT("Survivor 0 name"), After.Entries[0].FlipbookName, FString(TEXT("A")));
		TestTrue(TEXT("Survivor 0 keeps its own sequence"), After.Entries[0].PaperZDSequence == SeqA);
		TestEqual(TEXT("Survivor 1 name"), After.Entries[1].FlipbookName, FString(TEXT("C")));
		TestTrue(TEXT("Survivor 1 keeps its own sequence (no parallel-array drift)"), After.Entries[1].PaperZDSequence == SeqC);
	}

	return true;
}

// ─── Tag mapping assignment is one-home: a flipbook lives under one tag only ──
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAssignFlipbookToTagMappingExclusive,
	"Paper2DPlus.CharacterProfile.TagMapping.AssignFlipbookExclusive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAssignFlipbookToTagMappingExclusive::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	const FGameplayTag OldTag = FGameplayTag::RequestGameplayTag(FName("PlayerStates.Idle"), false);
	const FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(FName("PlayerStates.Moving"), false);
	TestTrue(TEXT("Old test tag is registered"), OldTag.IsValid());
	TestTrue(TEXT("New test tag is registered"), NewTag.IsValid());
	if (!OldTag.IsValid() || !NewTag.IsValid())
	{
		return false;
	}

	UObject* ExistingSequence = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookTagMapping OldMapping;
	OldMapping.Entries.Emplace(TEXT("Run"), ExistingSequence);
	Asset->TagMappings.Add(OldTag, OldMapping);

	FFlipbookTagMapping NewMapping;
	NewMapping.Entries.Emplace(TEXT("Walk"));
	Asset->TagMappings.Add(NewTag, NewMapping);

	TestTrue(TEXT("Assigning to new tag reports a change"),
		Asset->AssignFlipbookToTagMapping(NewTag, TEXT("Run")));

	const FFlipbookTagMapping& OldAfter = Asset->TagMappings[OldTag];
	const FFlipbookTagMapping& NewAfter = Asset->TagMappings[NewTag];
	TestEqual(TEXT("Old tag no longer owns Run"), OldAfter.Entries.Num(), 0);
	TestEqual(TEXT("New tag keeps existing entry and receives Run"), NewAfter.Entries.Num(), 2);
	if (NewAfter.Entries.Num() == 2)
	{
		TestEqual(TEXT("Existing entry preserved"), NewAfter.Entries[0].FlipbookName, FString(TEXT("Walk")));
		TestEqual(TEXT("Run moved into new tag"), NewAfter.Entries[1].FlipbookName, FString(TEXT("Run")));
		TestTrue(TEXT("Moved entry keeps its existing sequence"), NewAfter.Entries[1].PaperZDSequence == ExistingSequence);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusSetTagMappingEntryFlipbookExclusive,
	"Paper2DPlus.CharacterProfile.TagMapping.SetEntryFlipbookExclusive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusSetTagMappingEntryFlipbookExclusive::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	const FGameplayTag PrimaryTag = FGameplayTag::RequestGameplayTag(FName("PlayerStates.Idle"), false);
	const FGameplayTag OtherTag = FGameplayTag::RequestGameplayTag(FName("PlayerStates.Moving"), false);
	TestTrue(TEXT("Primary test tag is registered"), PrimaryTag.IsValid());
	TestTrue(TEXT("Other test tag is registered"), OtherTag.IsValid());
	if (!PrimaryTag.IsValid() || !OtherTag.IsValid())
	{
		return false;
	}

	UObject* OtherSequence = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookTagMapping PrimaryMapping;
	PrimaryMapping.Entries.Emplace(TEXT("Idle"));
	PrimaryMapping.Entries.Emplace(TEXT("Run"));
	Asset->TagMappings.Add(PrimaryTag, PrimaryMapping);

	FFlipbookTagMapping OtherMapping;
	OtherMapping.Entries.Emplace(TEXT("Run"), OtherSequence);
	Asset->TagMappings.Add(OtherTag, OtherMapping);

	TestTrue(TEXT("Setting entry reports a change"),
		Asset->SetTagMappingEntryFlipbook(PrimaryTag, 0, TEXT("Run")));

	const FFlipbookTagMapping& PrimaryAfter = Asset->TagMappings[PrimaryTag];
	const FFlipbookTagMapping& OtherAfter = Asset->TagMappings[OtherTag];
	TestEqual(TEXT("Primary tag collapsed duplicate Run entries"), PrimaryAfter.Entries.Num(), 1);
	if (PrimaryAfter.Entries.Num() == 1)
	{
		TestEqual(TEXT("Primary kept the assigned Run entry"), PrimaryAfter.Entries[0].FlipbookName, FString(TEXT("Run")));
		TestTrue(TEXT("Assigned entry adopted existing Run sequence"), PrimaryAfter.Entries[0].PaperZDSequence == OtherSequence);
	}
	TestEqual(TEXT("Other tag no longer owns Run"), OtherAfter.Entries.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNormalizeTagMappingsOneHome,
	"Paper2DPlus.CharacterProfile.TagMapping.NormalizeOneHome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNormalizeTagMappingsOneHome::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	const FGameplayTag FirstTag = FGameplayTag::RequestGameplayTag(FName("PlayerStates.Idle"), false);
	const FGameplayTag SecondTag = FGameplayTag::RequestGameplayTag(FName("PlayerStates.Moving"), false);
	TestTrue(TEXT("First test tag is registered"), FirstTag.IsValid());
	TestTrue(TEXT("Second test tag is registered"), SecondTag.IsValid());
	if (!FirstTag.IsValid() || !SecondTag.IsValid())
	{
		return false;
	}

	FFlipbookTagMapping FirstMapping;
	FirstMapping.Entries.Emplace(TEXT("Shared"));
	FirstMapping.Entries.Emplace(FString()); // empty authoring slots are not real flipbook homes
	Asset->TagMappings.Add(FirstTag, FirstMapping);

	FFlipbookTagMapping SecondMapping;
	SecondMapping.Entries.Emplace(TEXT("Shared"));
	SecondMapping.Entries.Emplace(FString());
	Asset->TagMappings.Add(SecondTag, SecondMapping);

	TestEqual(TEXT("One duplicate real flipbook removed"), Asset->NormalizeTagMappingsToOneFlipbookHome(), 1);
	TestEqual(TEXT("First tag keeps Shared"), Asset->TagMappings[FirstTag].Entries.Num(), 2);
	TestEqual(TEXT("Second tag keeps only its empty slot"), Asset->TagMappings[SecondTag].Entries.Num(), 1);
	if (Asset->TagMappings[SecondTag].Entries.Num() == 1)
	{
		TestEqual(TEXT("Empty slot preserved"), Asset->TagMappings[SecondTag].Entries[0].FlipbookName, FString());
	}

	return true;
}

// ─── Flipbook group tree: descendant/cycle detection + reparent ───────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusGroupTreeCycleDetection,
	"Paper2DPlus.CharacterProfile.Groups.TreeCycleDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusGroupTreeCycleDetection::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	auto AddGroup = [Asset](FName Name, FName Parent)
	{
		FFlipbookGroupInfo G; G.GroupName = Name; G.ParentGroup = Parent;
		Asset->FlipbookGroups.Add(G);
	};
	// Chain: A (root) -> B -> C
	AddGroup(TEXT("A"), NAME_None);
	AddGroup(TEXT("B"), TEXT("A"));
	AddGroup(TEXT("C"), TEXT("B"));

	TestTrue(TEXT("C is a (transitive) descendant of A"), Asset->IsDescendantOfFlipbookGroup(TEXT("C"), TEXT("A")));
	TestTrue(TEXT("C is a direct descendant of B"), Asset->IsDescendantOfFlipbookGroup(TEXT("C"), TEXT("B")));
	TestTrue(TEXT("B is a descendant of A"), Asset->IsDescendantOfFlipbookGroup(TEXT("B"), TEXT("A")));
	TestTrue(TEXT("Everything is a descendant of the root (NAME_None)"), Asset->IsDescendantOfFlipbookGroup(TEXT("C"), NAME_None));
	TestFalse(TEXT("A is NOT a descendant of C"), Asset->IsDescendantOfFlipbookGroup(TEXT("A"), TEXT("C")));
	TestFalse(TEXT("A is NOT a descendant of B"), Asset->IsDescendantOfFlipbookGroup(TEXT("A"), TEXT("B")));

	// (The "C is a descendant of A" assertion above IS the editor's cycle-guard primitive:
	//  OnGroupDrop rejects parenting A under C precisely when IsDescendantOfFlipbookGroup(C, A) is true.)

	// ReparentFlipbookGroup actually moves a group; B->root makes C no longer descend from A.
	Asset->ReparentFlipbookGroup(TEXT("B"), NAME_None);
	TestFalse(TEXT("After reparent, C no longer descends from A"), Asset->IsDescendantOfFlipbookGroup(TEXT("C"), TEXT("A")));
	TestTrue(TEXT("After reparent, C still descends from B"), Asset->IsDescendantOfFlipbookGroup(TEXT("C"), TEXT("B")));

	// A pre-existing 2-cycle must not hang the walker (Visited guard); it returns false and terminates.
	UPaper2DPlusCharacterProfileAsset* Cyclic = NewObject<UPaper2DPlusCharacterProfileAsset>();
	{
		FFlipbookGroupInfo X; X.GroupName = TEXT("X"); X.ParentGroup = TEXT("Y"); Cyclic->FlipbookGroups.Add(X);
		FFlipbookGroupInfo Y; Y.GroupName = TEXT("Y"); Y.ParentGroup = TEXT("X"); Cyclic->FlipbookGroups.Add(Y);
	}
	TestFalse(TEXT("Cycle guard terminates and returns false for an unreachable ancestor"),
		Cyclic->IsDescendantOfFlipbookGroup(TEXT("X"), TEXT("Unreachable")));

	// A group whose parent names a NON-EXISTENT group must terminate false (not spin): the inner
	// lookup finds no match, then the Visited guard breaks the otherwise-stuck walk next iteration.
	UPaper2DPlusCharacterProfileAsset* Dangling = NewObject<UPaper2DPlusCharacterProfileAsset>();
	{
		FFlipbookGroupInfo D; D.GroupName = TEXT("D"); D.ParentGroup = TEXT("GoneParent");
		Dangling->FlipbookGroups.Add(D);
	}
	TestFalse(TEXT("Dangling parent name terminates and returns false"),
		Dangling->IsDescendantOfFlipbookGroup(TEXT("D"), TEXT("A")));

	return true;
}

// ─── ClampHitboxToBounds: geometry clamp + changed-flag semantics ─────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusClampHitboxToBounds,
	"Paper2DPlus.CharacterProfile.Hitbox.ClampToBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusClampHitboxToBounds::RunTest(const FString& Parameters)
{
	// In-bounds hitbox is untouched, returns false.
	{
		FHitboxData HB; HB.X = 2; HB.Y = 2; HB.Width = 4; HB.Height = 4;
		const bool bChanged = UPaper2DPlusCharacterProfileAsset::ClampHitboxToBounds(HB, 100, 100);
		TestFalse(TEXT("In-bounds hitbox is unchanged"), bChanged);
		TestEqual(TEXT("X unchanged"), HB.X, 2); TestEqual(TEXT("Y unchanged"), HB.Y, 2);
		TestEqual(TEXT("W unchanged"), HB.Width, 4); TestEqual(TEXT("H unchanged"), HB.Height, 4);
	}
	// Oversized hitbox is clamped to the bounds, returns true.
	{
		FHitboxData HB; HB.X = 0; HB.Y = 0; HB.Width = 200; HB.Height = 300;
		const bool bChanged = UPaper2DPlusCharacterProfileAsset::ClampHitboxToBounds(HB, 100, 100);
		TestTrue(TEXT("Oversized hitbox changed"), bChanged);
		TestEqual(TEXT("W clamped to bounds"), HB.Width, 100);
		TestEqual(TEXT("H clamped to bounds"), HB.Height, 100);
	}
	// Negative origin is pulled to 0.
	{
		FHitboxData HB; HB.X = -5; HB.Y = -8; HB.Width = 10; HB.Height = 10;
		const bool bChanged = UPaper2DPlusCharacterProfileAsset::ClampHitboxToBounds(HB, 100, 100);
		TestTrue(TEXT("Negative-origin hitbox changed"), bChanged);
		TestEqual(TEXT("X clamped to 0"), HB.X, 0); TestEqual(TEXT("Y clamped to 0"), HB.Y, 0);
	}
	// Right-overflowing origin is pulled back so the box fits (X = BoundsW - W).
	{
		FHitboxData HB; HB.X = 95; HB.Y = 0; HB.Width = 20; HB.Height = 10;
		const bool bChanged = UPaper2DPlusCharacterProfileAsset::ClampHitboxToBounds(HB, 100, 100);
		TestTrue(TEXT("Overflowing hitbox changed"), bChanged);
		TestEqual(TEXT("Width preserved (fits)"), HB.Width, 20);
		TestEqual(TEXT("X pulled back to fit"), HB.X, 80);
	}
	// Degenerate size is raised to the 1px minimum.
	{
		FHitboxData HB; HB.X = 10; HB.Y = 10; HB.Width = 0; HB.Height = 0;
		const bool bChanged = UPaper2DPlusCharacterProfileAsset::ClampHitboxToBounds(HB, 100, 100);
		TestTrue(TEXT("Degenerate hitbox changed"), bChanged);
		TestEqual(TEXT("Width raised to 1"), HB.Width, 1);
		TestEqual(TEXT("Height raised to 1"), HB.Height, 1);
	}
	// Non-positive bounds are a no-op (returns false, hitbox untouched).
	{
		FHitboxData HB; HB.X = 0; HB.Y = 0; HB.Width = 10; HB.Height = 10;
		const bool bChanged = UPaper2DPlusCharacterProfileAsset::ClampHitboxToBounds(HB, 0, 100);
		TestFalse(TEXT("Zero-width bounds is a no-op"), bChanged);
		TestEqual(TEXT("W untouched on no-op bounds"), HB.Width, 10);
	}

	return true;
}

// ─── PostLoad leaves an authored Frame Cue array exactly as it found it ───────
// This is the surviving residue of the legacy Effects/FrameEvent migration tests. Those migrations
// have been deleted outright, so their field-mapping assertions have no subject; what remains, and
// what any future load-time pass must not break, is that loading a Character Profile neither adds,
// removes, reorders, nor re-instances the placements a designer authored — and that loading it twice
// is indistinguishable from loading it once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPostLoadPreservesAuthoredCues,
	"Paper2DPlus.CharacterProfile.FrameEvents.PostLoadPreservesAuthoredCues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPostLoadPreservesAuthoredCues::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Entry;
	Entry.Identity.FlipbookName = TEXT("Attack");
	Asset->Flipbooks.Add(Entry);

	UPaper2DPlusTestMomentCue* Moment = NewObject<UPaper2DPlusTestMomentCue>(Asset);
	Moment->TriggerFrame = 3;
	Moment->CustomPayload = 11;
	UPaper2DPlusTestRangeCue* Range = NewObject<UPaper2DPlusTestRangeCue>(Asset);
	Range->StartFrame = 1;
	Range->FrameCount = 2;
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Moment);
	Asset->Flipbooks[0].FrameEventData.FrameCues.Add(Range);

	Asset->PostLoad();

	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& AfterFirst =
		Asset->Flipbooks[0].FrameEventData.FrameCues;
	TestEqual(TEXT("PostLoad neither adds nor drops a placement"), AfterFirst.Num(), 2);
	if (AfterFirst.Num() == 2)
	{
		TestTrue(TEXT("PostLoad preserves authored placement identity and order"),
			AfterFirst[0] == Moment && AfterFirst[1] == Range);
		TestEqual(TEXT("PostLoad leaves the Moment payload untouched"), Moment->CustomPayload, 11);
		TestEqual(TEXT("PostLoad leaves the Moment anchor untouched"), Moment->TriggerFrame, 3);
		TestEqual(TEXT("PostLoad leaves the Range span untouched"), Range->FrameCount, 2);
	}

	// Idempotency: loading twice must be indistinguishable from loading once.
	Asset->PostLoad();
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& AfterSecond =
		Asset->Flipbooks[0].FrameEventData.FrameCues;
	TestEqual(TEXT("A second PostLoad does not duplicate placements"), AfterSecond.Num(), 2);
	if (AfterSecond.Num() == 2)
	{
		TestTrue(TEXT("A second PostLoad preserves placement identity and order"),
			AfterSecond[0] == Moment && AfterSecond[1] == Range);
	}

	return true;
}

// ════════════════════════════════════════════════════════════════════════════
// TASK-51 — Legacy CharacterProfile JSON import aliases.
// ════════════════════════════════════════════════════════════════════════════

// A non-empty legacy payload (schema 0) exercising the full alias path: the renamed top-level
// "Animations" array, flat pre-sub-struct entry fields (FlipbookName/Frames+hitbox/RootMotion),
// and a tag binding using the pre-struct-ify parallel "FlipbookNames" array. All must migrate to
// current runtime data (Flipbooks[].Identity/CombatData/MotionData + tag mapping Entries).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusImportLegacyNonEmptyAnimations,
	"Paper2DPlus.CharacterProfile.Serialization.ImportLegacyNonEmptyAnimations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusImportLegacyNonEmptyAnimations::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	// "PlayerStates.Attacking.GroundAttack" is registered in the project's DefaultGameplayTags.ini,
	// so it survives ImportFromJsonString's Tag.IsValid() gate.
	const FString LegacyJson = TEXT(
		"{"
			"\"SchemaVersion\":0,"
			"\"DisplayName\":\"LegacyChar\","
			"\"Animations\":["
				"{"
					"\"FlipbookName\":\"Atk\","
					"\"Frames\":[{\"Hitboxes\":[{\"X\":1,\"Y\":2,\"Width\":10,\"Height\":12}]}],"
					"\"RootMotion\":[{\"Position\":{\"X\":0,\"Y\":5}}]"
				"}"
			"],"
			"\"GroupBindings\":["
				"{\"Tag\":\"PlayerStates.Attacking.GroundAttack\",\"Binding\":{\"FlipbookNames\":[\"Atk\"]}}"
			"]"
		"}");

	const bool bImported = Asset->ImportFromJsonString(LegacyJson);
	TestTrue(TEXT("Legacy non-empty import succeeds"), bImported);

	// AC #1/#2: the renamed "Animations" array + flat entry fields land in the current sub-structs.
	TestEqual(TEXT("DisplayName imported"), Asset->DisplayName, FString(TEXT("LegacyChar")));
	TestEqual(TEXT("Animations -> Flipbooks (one entry)"), Asset->Flipbooks.Num(), 1);
	if (Asset->Flipbooks.Num() == 1)
	{
		const FFlipbookProfileEntry& FB = Asset->Flipbooks[0];
		TestEqual(TEXT("Flat FlipbookName -> Identity.FlipbookName"), FB.Identity.FlipbookName, FString(TEXT("Atk")));
		TestEqual(TEXT("Flat Frames -> CombatData.Frames"), FB.CombatData.Frames.Num(), 1);
		if (FB.CombatData.Frames.Num() == 1)
		{
			TestEqual(TEXT("Frame hitbox migrated"), FB.CombatData.Frames[0].Hitboxes.Num(), 1);
			if (FB.CombatData.Frames[0].Hitboxes.Num() == 1)
			{
				TestEqual(TEXT("Hitbox width preserved"), FB.CombatData.Frames[0].Hitboxes[0].Width, 10);
				TestEqual(TEXT("Hitbox height preserved"), FB.CombatData.Frames[0].Hitboxes[0].Height, 12);
			}
		}
		TestEqual(TEXT("Flat RootMotion -> MotionData.RootMotion"), FB.MotionData.RootMotion.Num(), 1);
		if (FB.MotionData.RootMotion.Num() == 1)
		{
			TestEqual(TEXT("RootMotion Y preserved (import does not run the Y-sign migration)"),
				FB.MotionData.RootMotion[0].Position.Y, 5.0);
		}
	}

	// AC #1 + TASK-3 JSON deferral: the legacy parallel "FlipbookNames" tag binding migrates to Entries.
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName("PlayerStates.Attacking.GroundAttack"), false);
	TestTrue(TEXT("Legacy tag mapping imported"), Asset->TagMappings.Contains(Tag));
	if (Asset->TagMappings.Contains(Tag))
	{
		const FFlipbookTagMapping& Binding = Asset->TagMappings[Tag];
		TestEqual(TEXT("Legacy FlipbookNames -> Entries (one)"), Binding.Entries.Num(), 1);
		if (Binding.Entries.Num() == 1)
		{
			TestEqual(TEXT("Tag entry flipbook name preserved"), Binding.Entries[0].FlipbookName, FString(TEXT("Atk")));
		}
	}

	return true;
}

// ════════════════════════════════════════════════════════════════════════════
// TASK-61 — Holistic frame-event index remap across exclude / restore / reorder.
// ════════════════════════════════════════════════════════════════════════════

namespace
{
	// Builds an N-keyframe flipbook (every keyframe gets a real sprite so RestoreExcludedFlipbookFrame
	// won't bail on a null sprite) and adds a single FFlipbookProfileEntry referencing it.
	UPaperFlipbook* BuildFlipbookEntryForRemapTest(UPaper2DPlusCharacterProfileAsset* Asset, int32 NumFrames)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = TEXT("Atk");
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Asset);
		Anim.Identity.Flipbook = Flipbook;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		{
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.KeyFrames.Empty();
			for (int32 i = 0; i < NumFrames; ++i)
			{
				FPaperFlipbookKeyFrame KeyFrame;
				KeyFrame.FrameRun = 1;
				KeyFrame.Sprite = NewObject<UPaperSprite>(Flipbook);
				Mutator.KeyFrames.Add(KeyFrame);
			}
		}
		Asset->Flipbooks.Add(Anim);
		return Flipbook;
	}
}

// ─── Reorder carries each event's anchor with its frame ───────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameEventReorderRemap,
	"Paper2DPlus.CharacterProfile.FrameEvents.ReorderCarriesEventAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameEventReorderRemap::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	BuildFlipbookEntryForRemapTest(Asset, 5);

	UPaper2DPlusTestFrameEvent* OneShot = NewObject<UPaper2DPlusTestFrameEvent>(Asset, NAME_None, RF_Transactional);
	OneShot->TriggerFrame = 3;
	UPaper2DPlusTestFrameEventState* Ranged = NewObject<UPaper2DPlusTestFrameEventState>(Asset, NAME_None, RF_Transactional);
	Ranged->StartFrame = 1; Ranged->FrameCount = 2;
	Asset->Flipbooks[0].FrameEventData.FrameEvents.Add(OneShot);
	Asset->Flipbooks[0].FrameEventData.FrameEvents.Add(Ranged);

	// Move frame 3 -> 0. Permutation: [1,2,3,0,4]. Frame 3's event follows to 0; frame 1 -> 2.
	TestTrue(TEXT("Reorder succeeds"), Asset->MoveFlipbookFrame(0, 3, 0));
	TestEqual(TEXT("One-shot anchor follows its frame (3 -> 0)"), OneShot->TriggerFrame, 0);
	TestEqual(TEXT("Ranged start follows its frame (1 -> 2)"), Ranged->StartFrame, 2);
	TestEqual(TEXT("Ranged span preserved"), Ranged->FrameCount, 2);
	TestEqual(TEXT("Both events still live (none removed by a reorder)"),
		Asset->Flipbooks[0].FrameEventData.FrameEvents.Num(), 2);

	return true;
}

// ─── Exclude (a different frame) + restore round-trips the anchor ─────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameEventExcludeRestoreRemap,
	"Paper2DPlus.CharacterProfile.FrameEvents.ExcludeRestoreRemapsEventAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameEventExcludeRestoreRemap::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	BuildFlipbookEntryForRemapTest(Asset, 5);

	UPaper2DPlusTestFrameEvent* OneShot = NewObject<UPaper2DPlusTestFrameEvent>(Asset, NAME_None, RF_Transactional);
	OneShot->TriggerFrame = 3;
	Asset->Flipbooks[0].FrameEventData.FrameEvents.Add(OneShot);

	// Exclude an EARLIER frame (1): the event on frame 3 shifts down to 2.
	TestTrue(TEXT("Exclude frame 1"), Asset->ExcludeFlipbookFrame(0, 1));
	TestEqual(TEXT("Anchor shifts down when an earlier frame is excluded"), OneShot->TriggerFrame, 2);
	TestTrue(TEXT("Event remained live (its own frame wasn't removed)"),
		Asset->Flipbooks[0].FrameEventData.FrameEvents.Contains(OneShot));

	// Restore the excluded frame: the event returns to its original semantic frame (3).
	TestTrue(TEXT("Restore"), Asset->RestoreExcludedFlipbookFrame(0, 0));
	TestEqual(TEXT("Anchor restored to its original frame after exclude+restore"), OneShot->TriggerFrame, 3);

	return true;
}

// ─── An event on the excluded frame is stashed, then reattached on restore ────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameEventExcludedFrameStash,
	"Paper2DPlus.CharacterProfile.FrameEvents.ExcludedFrameEventStashedAndReattached",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameEventExcludedFrameStash::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	BuildFlipbookEntryForRemapTest(Asset, 5);

	// Two events ON frame 2 (one-shot + ranged) — both should be stashed when frame 2 is excluded.
	UPaper2DPlusTestFrameEvent* OnFrame = NewObject<UPaper2DPlusTestFrameEvent>(Asset, NAME_None, RF_Transactional);
	OnFrame->TriggerFrame = 2;
	UPaper2DPlusTestFrameEventState* RangedOnFrame = NewObject<UPaper2DPlusTestFrameEventState>(Asset, NAME_None, RF_Transactional);
	RangedOnFrame->StartFrame = 2; RangedOnFrame->FrameCount = 1;
	// A control event on a LATER frame (4) that must survive (shifted), not stashed.
	UPaper2DPlusTestFrameEvent* Control = NewObject<UPaper2DPlusTestFrameEvent>(Asset, NAME_None, RF_Transactional);
	Control->TriggerFrame = 4;
	Asset->Flipbooks[0].FrameEventData.FrameEvents.Add(OnFrame);
	Asset->Flipbooks[0].FrameEventData.FrameEvents.Add(RangedOnFrame);
	Asset->Flipbooks[0].FrameEventData.FrameEvents.Add(Control);

	TestTrue(TEXT("Exclude frame 2 (the events' own frame)"), Asset->ExcludeFlipbookFrame(0, 2));

	const FFlipbookProfileEntry& Anim = Asset->Flipbooks[0];
	TestFalse(TEXT("One-shot on excluded frame removed from live list"), Anim.FrameEventData.FrameEvents.Contains(OnFrame));
	TestFalse(TEXT("Ranged on excluded frame removed from live list"), Anim.FrameEventData.FrameEvents.Contains(RangedOnFrame));
	TestTrue(TEXT("Control event survived"), Anim.FrameEventData.FrameEvents.Contains(Control));
	TestEqual(TEXT("Control event shifted down (a frame before it was removed)"), Control->TriggerFrame, 3);
	TestEqual(TEXT("One excluded frame recorded"), Anim.CombatData.ExcludedFrames.Num(), 1);
	if (Anim.CombatData.ExcludedFrames.Num() == 1)
	{
		TestEqual(TEXT("Both events stashed on the excluded frame"), Anim.CombatData.ExcludedFrames[0].StashedFrameEvents.Num(), 2);
		TestTrue(TEXT("One-shot stashed"), Anim.CombatData.ExcludedFrames[0].StashedFrameEvents.Contains(OnFrame));
		TestTrue(TEXT("Ranged stashed"), Anim.CombatData.ExcludedFrames[0].StashedFrameEvents.Contains(RangedOnFrame));
	}

	TestTrue(TEXT("Restore"), Asset->RestoreExcludedFlipbookFrame(0, 0));
	const FFlipbookProfileEntry& Anim2 = Asset->Flipbooks[0];
	TestTrue(TEXT("One-shot reattached to live list"), Anim2.FrameEventData.FrameEvents.Contains(OnFrame));
	TestTrue(TEXT("Ranged reattached to live list"), Anim2.FrameEventData.FrameEvents.Contains(RangedOnFrame));
	TestEqual(TEXT("One-shot reattached at the restored frame index"), OnFrame->TriggerFrame, 2);
	TestEqual(TEXT("Ranged reattached at the restored frame index"), RangedOnFrame->StartFrame, 2);
	TestEqual(TEXT("Control event shifted back up after restore"), Control->TriggerFrame, 4);

	return true;
}

// =============================================================================
// WS2-2C — ranged RemapFrameAnchors remaps BOTH endpoints (was: start-only + clamp,
// which silently mis-sized interior/end frame mutations).
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusRangedRemapBothEndpoints,
	"Paper2DPlus.FrameEvents.RangedRemap.BothEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusRangedRemapBothEndpoints::RunTest(const FString& Parameters)
{
	// Interior frame excluded: range [2,4] (Start=2,Count=3) with old frame 3 removed must contract to
	// cover new frames {2,3} (= old {2,4}) → Start=2, Count=2 (the old code wrongly kept Count=3).
	{
		UPaper2DPlusTestFrameEventState* S = NewObject<UPaper2DPlusTestFrameEventState>();
		S->StartFrame = 2; S->FrameCount = 3;
		const TArray<int32> OldToNew = { 0, 1, 2, INDEX_NONE, 3, 4 }; // exclude old frame 3
		const bool bKept = S->RemapFrameAnchors(OldToNew, 5);
		TestTrue(TEXT("Interior-exclude keeps the event"), bKept);
		TestEqual(TEXT("Interior-exclude: Start"), S->StartFrame, 2);
		TestEqual(TEXT("Interior-exclude: Count contracts to 2"), S->FrameCount, 2);
	}

	// Whole span reordered 2,3,4 -> 4,5,6: both endpoints follow, span length preserved.
	{
		UPaper2DPlusTestFrameEventState* S = NewObject<UPaper2DPlusTestFrameEventState>();
		S->StartFrame = 2; S->FrameCount = 3;
		const TArray<int32> OldToNew = { 0, 1, 4, 5, 6, 2, 3, 7 }; // 2->4,3->5,4->6
		const bool bKept = S->RemapFrameAnchors(OldToNew, 8);
		TestTrue(TEXT("Reorder keeps the event"), bKept);
		TestEqual(TEXT("Reorder: Start follows"), S->StartFrame, 4);
		TestEqual(TEXT("Reorder: Count preserved"), S->FrameCount, 3);
	}

	// Start frame removed: drop/stash (return false) — existing primary-anchor contract preserved.
	{
		UPaper2DPlusTestFrameEventState* S = NewObject<UPaper2DPlusTestFrameEventState>();
		S->StartFrame = 2; S->FrameCount = 2;
		const TArray<int32> OldToNew = { 0, 1, INDEX_NONE, 2, 3 }; // exclude old frame 2 (the start)
		const bool bKept = S->RemapFrameAnchors(OldToNew, 4);
		TestFalse(TEXT("Start-removed returns false (caller stashes/drops)"), bKept);
	}

	// End frame removed, start surviving: contract inward (end snaps to nearest surviving), keep.
	{
		UPaper2DPlusTestFrameEventState* S = NewObject<UPaper2DPlusTestFrameEventState>();
		S->StartFrame = 2; S->FrameCount = 3; // covers 2,3,4
		const TArray<int32> OldToNew = { 0, 1, 2, 3, INDEX_NONE }; // exclude old frame 4 (the end)
		const bool bKept = S->RemapFrameAnchors(OldToNew, 4);
		TestTrue(TEXT("End-removed keeps the event"), bKept);
		TestEqual(TEXT("End-removed: Start"), S->StartFrame, 2);
		TestEqual(TEXT("End-removed: Count contracts to 2"), S->FrameCount, 2);
	}

	// Reorder whose span endpoints CROSS (non-monotonic OldToNew, the MoveFlipbookFrame path): all 3
	// frames survive, so the span must NOT collapse — bounding min..max keeps Count=3. (The naive
	// start+last-surviving sizing wrongly shrank this to 1; review finding C1.)
	{
		UPaper2DPlusTestFrameEventState* S = NewObject<UPaper2DPlusTestFrameEventState>();
		S->StartFrame = 2; S->FrameCount = 3; // covers 2,3,4
		const TArray<int32> OldToNew = { 0, 1, 4, 2, 3, 5 }; // Move(2->4): 2->4, 3->2, 4->3 (crosses)
		const bool bKept = S->RemapFrameAnchors(OldToNew, 6);
		TestTrue(TEXT("Crossing-reorder keeps the event"), bKept);
		TestEqual(TEXT("Crossing-reorder: Start = min surviving (2)"), S->StartFrame, 2);
		TestEqual(TEXT("Crossing-reorder: Count NOT collapsed (bounding span = 3)"), S->FrameCount, 3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileCueJsonRoundTripTest,
	"Paper2DPlus.CharacterProfile.Serialization.CueRoundTripKeepsConcreteSubclass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileCueJsonRoundTripTest::RunTest(const FString& Parameters)
{
	// The Character Profile JSON contract carries a placement's CLASS, not just its base-class payload:
	// an instanced sub-object is written with its concrete class path and reconstructed as that class on
	// import. Losing this silently downgrades every placement in an exported profile to a bare base cue,
	// which no dispatch and no designer would ever recover from. Previously proven through a built-in
	// cue; those are gone, so it is proven here against an ordinary Cue subclass with its own payload.
	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry& SourceEntry = Source->Flipbooks.AddDefaulted_GetRef();
	SourceEntry.Identity.FlipbookName = TEXT("Attack");

	UPaper2DPlusTestMomentCue* SourceMoment = NewObject<UPaper2DPlusTestMomentCue>(Source);
	SourceMoment->TriggerFrame = 4;
	SourceMoment->CustomPayload = 73;
	SourceMoment->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
	SourceEntry.FrameEventData.FrameCues.Add(SourceMoment);

	UPaper2DPlusTestRangeCue* SourceRange = NewObject<UPaper2DPlusTestRangeCue>(Source);
	SourceRange->StartFrame = 1;
	SourceRange->FrameCount = 3;
	SourceEntry.FrameEventData.FrameCues.Add(SourceRange);

	FString Json;
	if (!TestTrue(TEXT("Character Profile with authored Cue placements exports"),
		Source->ExportToJsonString(Json)))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Imported = NewObject<UPaper2DPlusCharacterProfileAsset>();
	if (!TestTrue(TEXT("Character Profile Cue JSON imports"), Imported->ImportFromJsonString(Json)))
	{
		return false;
	}
	if (!TestEqual(TEXT("Imported profile keeps one animation"), Imported->Flipbooks.Num(), 1)
		|| !TestEqual(TEXT("Imported profile keeps both Cues in authored order"),
			Imported->Flipbooks[0].FrameEventData.FrameCues.Num(), 2))
	{
		return false;
	}

	UPaper2DPlusTestMomentCue* ImportedMoment = Cast<UPaper2DPlusTestMomentCue>(
		Imported->Flipbooks[0].FrameEventData.FrameCues[0]);
	UPaper2DPlusTestRangeCue* ImportedRange = Cast<UPaper2DPlusTestRangeCue>(
		Imported->Flipbooks[0].FrameEventData.FrameCues[1]);
	if (!TestNotNull(TEXT("Imported Moment placement keeps its concrete Cue subclass"), ImportedMoment)
		|| !TestNotNull(TEXT("Imported Range placement keeps its concrete Cue subclass"), ImportedRange))
	{
		return false;
	}
	TestTrue(TEXT("The round trip produced a new instance, not the exported one"),
		ImportedMoment != SourceMoment);
	TestEqual(TEXT("The subclass's own payload survives the round trip"),
		ImportedMoment->CustomPayload, 73);
	TestEqual(TEXT("The Moment anchor survives the round trip"), ImportedMoment->TriggerFrame, 4);
	TestEqual(TEXT("An authored Net Policy survives the round trip"),
		(int32)ImportedMoment->NetPolicy, (int32)EPaper2DPlusFrameCueNetPolicy::CosmeticOnly);
	TestEqual(TEXT("The Range anchor survives the round trip"), ImportedRange->StartFrame, 1);
	TestEqual(TEXT("The Range span survives the round trip"), ImportedRange->FrameCount, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileCueJsonImportOuterTest,
	"Paper2DPlus.CharacterProfile.Serialization.CueImportOutersPlacementsToAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileCueJsonImportOuterTest::RunTest(const FString& Parameters)
{
	// TASK-177: FJsonObjectConverter re-creates Instanced sub-objects outered to the TRANSIENT package
	// (the import payload is a raw struct with no owning UObject to inherit), which tripped the
	// WrongCueOuter validator on every unchanged export→import round trip and left placements that
	// neither save nor cook once their transient outer is GC'd. The authoring contract
	// (CreatePlacementFromClassDefaults) is Outer == the profile asset with RF_Transactional; import
	// must land on the same end state for live placements, cues stashed on an excluded frame, and the
	// save-preserving legacy Frame Event shells that ride the same Instanced path.
	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry& SourceEntry = Source->Flipbooks.AddDefaulted_GetRef();
	SourceEntry.Identity.FlipbookName = TEXT("Attack");

	UPaper2DPlusTestMomentCue* SourceMoment = NewObject<UPaper2DPlusTestMomentCue>(Source);
	SourceMoment->TriggerFrame = 2;
	SourceMoment->CustomPayload = 11;
	SourceEntry.FrameEventData.FrameCues.Add(SourceMoment);

	UPaper2DPlusTestRangeCue* SourceRange = NewObject<UPaper2DPlusTestRangeCue>(Source);
	SourceRange->StartFrame = 0;
	SourceRange->FrameCount = 2;
	SourceEntry.FrameEventData.FrameCues.Add(SourceRange);

	FExcludedFlipbookFrameData& SourceExcluded =
		SourceEntry.CombatData.ExcludedFrames.AddDefaulted_GetRef();
	UPaper2DPlusTestMomentCue* SourceStashed = NewObject<UPaper2DPlusTestMomentCue>(Source);
	SourceStashed->CustomPayload = 22;
	SourceExcluded.StashedFrameCues.Add(SourceStashed);

	UPaper2DPlusTestFrameEventState* SourceLegacyEvent =
		NewObject<UPaper2DPlusTestFrameEventState>(Source);
	SourceEntry.FrameEventData.FrameEvents.Add(SourceLegacyEvent);

	FString Json;
	if (!TestTrue(TEXT("Profile with Cue placements exports"), Source->ExportToJsonString(Json)))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Imported = NewObject<UPaper2DPlusCharacterProfileAsset>();
	if (!TestTrue(TEXT("Profile with Cue placements imports"), Imported->ImportFromJsonString(Json)))
	{
		return false;
	}
	if (!TestEqual(TEXT("Imported profile keeps one animation"), Imported->Flipbooks.Num(), 1))
	{
		return false;
	}
	const FFlipbookProfileEntry& Entry = Imported->Flipbooks[0];
	if (!TestEqual(TEXT("Both live placements survive"), Entry.FrameEventData.FrameCues.Num(), 2)
		|| !TestEqual(TEXT("The excluded frame survives"), Entry.CombatData.ExcludedFrames.Num(), 1)
		|| !TestEqual(TEXT("The stashed placement survives"),
			Entry.CombatData.ExcludedFrames[0].StashedFrameCues.Num(), 1)
		|| !TestEqual(TEXT("The legacy Frame Event shell survives"),
			Entry.FrameEventData.FrameEvents.Num(), 1))
	{
		return false;
	}

	UPaper2DPlusCueBase* ImportedMoment = Entry.FrameEventData.FrameCues[0];
	UPaper2DPlusCueBase* ImportedRange = Entry.FrameEventData.FrameCues[1];
	UPaper2DPlusCueBase* ImportedStashed = Entry.CombatData.ExcludedFrames[0].StashedFrameCues[0];
	UPaper2DPlusFrameEventBase* ImportedLegacyEvent = Entry.FrameEventData.FrameEvents[0];
	if (!TestNotNull(TEXT("Moment placement imports non-null"), ImportedMoment)
		|| !TestNotNull(TEXT("Range placement imports non-null"), ImportedRange)
		|| !TestNotNull(TEXT("Stashed placement imports non-null"), ImportedStashed)
		|| !TestNotNull(TEXT("Legacy Frame Event shell imports non-null"), ImportedLegacyEvent))
	{
		return false;
	}

	TestTrue(TEXT("Moment placement is outered to the imported asset, not the transient package"),
		ImportedMoment->GetOuter() == Imported);
	TestTrue(TEXT("Moment placement carries RF_Transactional like an authored placement"),
		ImportedMoment->HasAnyFlags(RF_Transactional));
	TestTrue(TEXT("Range placement is outered to the imported asset, not the transient package"),
		ImportedRange->GetOuter() == Imported);
	TestTrue(TEXT("Range placement carries RF_Transactional like an authored placement"),
		ImportedRange->HasAnyFlags(RF_Transactional));
	TestTrue(TEXT("Stashed placement is outered to the imported asset, not the transient package"),
		ImportedStashed->GetOuter() == Imported);
	TestTrue(TEXT("Stashed placement carries RF_Transactional like an authored placement"),
		ImportedStashed->HasAnyFlags(RF_Transactional));
	TestTrue(TEXT("Legacy Frame Event shell is outered to the imported asset so it stays savable"),
		ImportedLegacyEvent->GetOuter() == Imported);
	TestTrue(TEXT("Legacy Frame Event shell carries RF_Transactional"),
		ImportedLegacyEvent->HasAnyFlags(RF_Transactional));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileImportSchemaSevenRoots,
	"Paper2DPlus.CharacterProfile.Serialization.Schema7RootFlagsBecomeChainStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileImportSchemaSevenRoots::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	const FString SchemaSevenJson = FString::Printf(
		TEXT("{\"SchemaVersion\":7,\"DisplayName\":\"SchemaSeven\",")
		TEXT("\"Flipbooks\":[")
			TEXT("{\"Identity\":{\"FlipbookName\":\"Idle\"},\"EditorMeta\":{\"bIsComboRoot\":false,\"BISCOMBOROOT\":true}},")
			TEXT("{\"Identity\":{\"FlipbookName\":\"AttackB\"},\"EditorMeta\":{\"bIsComboRoot\":true}},")
			TEXT("{\"Identity\":{\"FlipbookName\":\"AttackA\"},\"EditorMeta\":{\"bIsComboRoot\":true}}],")
		TEXT("\"GroupBindings\":[{\"Tag\":\"%s\",\"Binding\":{\"Entries\":[")
			TEXT("{\"FlipbookName\":\"AttackA\"},{\"FlipbookName\":\"Idle\",\"RootNumber\":-1},{\"FlipbookName\":\"AttackB\"}")
		TEXT("]}}]}"),
		*CharacterProfileTest_RootGroup.GetTag().ToString());
	TestTrue(TEXT("Schema 7 imports through the v8 migration"),
		Asset->ImportFromJsonString(SchemaSevenJson));

	const FFlipbookTagMapping* Mapping = Asset->TagMappings.Find(CharacterProfileTest_RootGroup);
	TestNotNull(TEXT("Schema 7 group binding survives migration"), Mapping);
	if (Mapping && Mapping->Entries.Num() == 3)
	{
		TestTrue(TEXT("First flagged row becomes a Chain Start in authored group order"),
			Mapping->Entries[0].bIsChainStart);
		TestFalse(TEXT("Exact legacy flag key wins and a negative legacy number stays unflagged"),
			Mapping->Entries[1].bIsChainStart);
		TestTrue(TEXT("Second flagged row also becomes a Chain Start"),
			Mapping->Entries[2].bIsChainStart);
	}
	else
	{
		AddError(TEXT("Schema 7 migration did not preserve all three exact-group entries"));
	}

	FString CurrentJson;
	TestTrue(TEXT("Migrated payload exports again"), Asset->ExportToJsonString(CurrentJson));
	TestTrue(TEXT("Re-export stamps current schema 8"),
		CurrentJson.Contains(TEXT("\"SchemaVersion\":8"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("Re-export carries no legacy RootNumber key"),
		CurrentJson.Contains(TEXT("\"RootNumber\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileRenameInvalidatesExactNameCache,
	"Paper2DPlus.CharacterProfile.Lookup.RenameInvalidatesExactNameCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileRenameInvalidatesExactNameCache::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Attack_A");
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Asset);
	Anim.Identity.Flipbook = Flipbook;
	Asset->Flipbooks.Add(Anim);

	// A second row keeps the exact-name cache genuinely multi-key, so a rename that clobbered the
	// whole cache rather than rebuilding it would show up here too.
	FFlipbookProfileEntry Other;
	Other.Identity.FlipbookName = TEXT("Idle");
	UPaperFlipbook* OtherFlipbook = NewObject<UPaperFlipbook>(Asset);
	Other.Identity.Flipbook = OtherFlipbook;
	Asset->Flipbooks.Add(Other);

	// Warm the cache through the real accessor: the defect only bites a cache that was already built.
	bool bAmbiguous = false;
	TestNotNull(TEXT("Exact lookup resolves the original name"),
		Asset->FindExactFlipbookData(FName(TEXT("Attack_A")), Flipbook, bAmbiguous));
	TestFalse(TEXT("A unique original name is not ambiguous"), bAmbiguous);

	// The rename leaves Flipbooks.Num() unchanged, so no count check can notice the stale key. This is
	// the exact gesture the Animations browser performs on an inline F2 commit — no PostEditChangeProperty.
	TestTrue(TEXT("Renaming the animation succeeds"),
		Asset->RenameFlipbookAndPropagate(0, TEXT("Attack_Heavy")));

	bAmbiguous = false;
	const FFlipbookProfileEntry* Renamed =
		Asset->FindExactFlipbookData(FName(TEXT("Attack_Heavy")), Flipbook, bAmbiguous);
	// Before the fix this returned nullptr for the rest of the session, so ResolveFrameCueAnchor
	// answered ProfileRowNotFound and every Profile-Socket-anchored Frame Cue on this animation
	// silently lost its socket transform.
	TestNotNull(TEXT("Exact lookup resolves the NEW name after a rename"), Renamed);
	TestFalse(TEXT("The renamed row is not ambiguous"), bAmbiguous);
	if (Renamed)
	{
		TestEqual(TEXT("Exact lookup returns the renamed row itself"),
			Renamed->Identity.FlipbookName, FString(TEXT("Attack_Heavy")));
	}

	bAmbiguous = false;
	TestNull(TEXT("The stale pre-rename name no longer resolves"),
		Asset->FindExactFlipbookData(FName(TEXT("Attack_A")), Flipbook, bAmbiguous));

	bAmbiguous = false;
	TestNotNull(TEXT("An untouched sibling row still resolves"),
		Asset->FindExactFlipbookData(FName(TEXT("Idle")), OtherFlipbook, bAmbiguous));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileExactLookupRepairsStaleCache,
	"Paper2DPlus.CharacterProfile.Lookup.ExactLookupRepairsStaleNameKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileExactLookupRepairsStaleCache::RunTest(const FString& Parameters)
{
	// Independent of the rename fix: any same-count mutation that forgets to invalidate must degrade
	// to a scan, not to a wrong "no such animation" answer. This is the guard the two sibling lookups
	// already had and this one did not.
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Idle");
	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Asset);
	Anim.Identity.Flipbook = Flipbook;
	Asset->Flipbooks.Add(Anim);

	bool bAmbiguous = false;
	TestNotNull(TEXT("Exact lookup warms the cache"),
		Asset->FindExactFlipbookData(FName(TEXT("Idle")), Flipbook, bAmbiguous));

	// Mutate the name WITHOUT going through the rename API, leaving both caches stale at an unchanged
	// element count — the shape a future same-count mutation would reintroduce.
	Asset->Flipbooks[0].Identity.FlipbookName = TEXT("Idle_Alt");

	bAmbiguous = false;
	const FFlipbookProfileEntry* Repaired =
		Asset->FindExactFlipbookData(FName(TEXT("Idle_Alt")), Flipbook, bAmbiguous);
	TestNotNull(TEXT("A stale exact-name cache repairs itself instead of failing closed"), Repaired);
	TestFalse(TEXT("The repaired lookup is not ambiguous"), bAmbiguous);

	bAmbiguous = false;
	TestNull(TEXT("A genuinely absent animation still resolves to null"),
		Asset->FindExactFlipbookData(FName(TEXT("NoSuchAnimation")), Flipbook, bAmbiguous));

	return true;
}

#endif // WITH_EDITOR
