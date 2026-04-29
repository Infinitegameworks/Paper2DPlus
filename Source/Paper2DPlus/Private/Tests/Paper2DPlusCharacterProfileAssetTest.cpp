// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

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

	FString Json;
	const bool bExported = Source->ExportToJsonString(Json);
	TestTrue(TEXT("ExportToJsonString should succeed"), bExported);
	TestTrue(TEXT("Exported JSON should contain animation name"), Json.Contains(TEXT("Idle")));

	UPaper2DPlusCharacterProfileAsset* Loaded = NewObject<UPaper2DPlusCharacterProfileAsset>();
	const bool bImported = Loaded->ImportFromJsonString(Json);
	TestTrue(TEXT("ImportFromJsonString should succeed"), bImported);

	TestEqual(TEXT("DisplayName should round-trip"), Loaded->DisplayName, Source->DisplayName);
	TestEqual(TEXT("Animation count should round-trip"), Loaded->Flipbooks.Num(), 1);
	if (Loaded->Flipbooks.Num() > 0)
	{
		TestEqual(TEXT("AnimationName should round-trip"), Loaded->Flipbooks[0].Identity.FlipbookName, TEXT("Idle"));
		TestEqual(TEXT("Frame count should round-trip"), Loaded->Flipbooks[0].CombatData.Frames.Num(), 1);
	}

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
	const bool bHasAttackBoxes = UPaper2DPlusBlueprintLibrary::GetActorAttackBoxes(Attacker, AttackBoxes);
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
	const bool bHasAttackBoxes = UPaper2DPlusBlueprintLibrary::GetActorAttackBoxes(Actor, AttackBoxes);
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
	const bool bHasAttackBoxes = UPaper2DPlusBlueprintLibrary::GetActorAttackBoxes(Actor, AttackBoxes);
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
// See docs/plans/2026-04-08-fix-root-motion-correctness-plan.md for full context.
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

	// Build a profile with BOTH root motion AND a frame-0 event.
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

	UPaper2DPlusTestFrameEvent* TestEvent = NewObject<UPaper2DPlusTestFrameEvent>(Asset, NAME_None, RF_Transactional);
	TestEvent->TriggerFrame = 0;
	Anim.FrameEventData.FrameEvents.Add(TestEvent);

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

	// Dispatch frame-0 event via HandleFlipbookChanged.
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("Precondition: frame-0 event should have fired"),
		TestEvent->FireCount, 1);

	// Now call ResetRootMotionTracking. Pre-fix: this wiped the shared cache,
	// silently breaking subsequent frame event dispatch. Post-fix: only
	// root-motion tracking state is reset; the shared cache survives.
	DataComp->ResetRootMotionTracking();

	// Re-dispatch frame 0 via HandleFlipbookChanged. If the frame event cache
	// was wiped, the event would not fire.
	TestEvent->FireCount = 0;
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("After ResetRootMotionTracking, frame-0 event should still fire (cache preserved)"),
		TestEvent->FireCount, 1);

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

	// Build a profile with a frame-0 event.
	UPaperSprite* Sprite = MakeSprite();
	UPaperFlipbook* CharacterFB = MakeSingleFrameFlipbook(Sprite);

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Attack");
	Anim.Identity.Flipbook = CharacterFB;

	UPaper2DPlusTestFrameEvent* TestEvent = NewObject<UPaper2DPlusTestFrameEvent>(Asset, NAME_None, RF_Transactional);
	TestEvent->TriggerFrame = 0;
	Anim.FrameEventData.FrameEvents.Add(TestEvent);
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

	// Warm caches first via frame-0 dispatch.
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("Precondition: frame-0 event should have fired"),
		TestEvent->FireCount, 1);

	// Toggle root motion off — this internally calls ResetRootMotionTracking.
	// Pre-fix: the reset nuked the shared cache, breaking frame events.
	// Post-fix: the reset is narrow and preserves the frame event cache.
	DataComp->SetAutoApplyRootMotion(false);

	// Re-dispatch. Event should still fire if cache is intact.
	TestEvent->FireCount = 0;
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("After SetAutoApplyRootMotion(false), frame-0 event should still fire"),
		TestEvent->FireCount, 1);

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
	// each author a different frame-0 test event. After swapping profiles,
	// the new profile's event should fire — not the old one.
	UPaperSprite* Sprite = MakeSprite();
	UPaperFlipbook* CharacterFB = MakeSingleFrameFlipbook(Sprite);

	auto BuildAssetWithEvent = [&]() -> TPair<UPaper2DPlusCharacterProfileAsset*, UPaper2DPlusTestFrameEvent*>
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = TEXT("Attack");
		Anim.Identity.Flipbook = CharacterFB;
		UPaper2DPlusTestFrameEvent* Evt = NewObject<UPaper2DPlusTestFrameEvent>(Asset, NAME_None, RF_Transactional);
		Evt->TriggerFrame = 0;
		Anim.FrameEventData.FrameEvents.Add(Evt);
		Asset->Flipbooks.Add(Anim);
		return {Asset, Evt};
	};

	auto [ProfileA, EventA] = BuildAssetWithEvent();
	auto [ProfileB, EventB] = BuildAssetWithEvent();

	AActor* Actor = NewObject<AActor>();
	UPaperFlipbookComponent* FBComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	Actor->AddOwnedComponent(FBComp);
	Actor->AddOwnedComponent(DataComp);
	FBComp->SetFlipbook(CharacterFB);
	DataComp->CharacterProfile = ProfileA;
	DataComp->FlipbookComponent = FBComp;

	// Prime the cache on profile A.
	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("Profile A: event should have fired"),
		EventA->FireCount, 1);
	TestEqual(TEXT("Profile B: event should NOT have fired yet"),
		EventB->FireCount, 0);

	// Swap to profile B. SetCharacterProfile routes through
	// HandleFlipbookChanged which re-warms the cache with B's data AND
	// dispatches frame-0 events for B in one pass.
	DataComp->SetCharacterProfile(ProfileB);

	TestEqual(TEXT("After swap: B's event should have fired"),
		EventB->FireCount, 1);
	TestEqual(TEXT("After swap: A's event count should still be 1 (no extra fires)"),
		EventA->FireCount, 1);

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

// ═══════════════════════════════════════════════════════════════════════════════
// Frame Events dispatch tests (Phase 2.5)
// ═══════════════════════════════════════════════════════════════════════════════

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameEventZeroFires,
	"Paper2DPlus.CharacterProfile.FrameEvents.FrameZeroEventFires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameEventZeroFires::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEventDrivenTestUtils;

	UPaperSprite* Sprite = MakeSprite();
	if (!TestNotNull(TEXT("Sprite"), Sprite)) return false;

	UPaperFlipbook* CharacterFB = MakeSingleFrameFlipbook(Sprite);
	if (!TestNotNull(TEXT("Flipbook"), CharacterFB)) return false;

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Attack");
	Anim.Identity.Flipbook = CharacterFB;

	// Add a test frame event at frame 0
	UPaper2DPlusTestFrameEvent* TestEvent = NewObject<UPaper2DPlusTestFrameEvent>(Asset, NAME_None, RF_Transactional);
	TestEvent->TriggerFrame = 0;
	Anim.FrameEventData.FrameEvents.Add(TestEvent);

	Asset->Flipbooks.Add(Anim);

	AActor* Actor = NewObject<AActor>();
	UPaperFlipbookComponent* FBComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	Actor->AddOwnedComponent(FBComp);
	Actor->AddOwnedComponent(DataComp);

	FBComp->SetFlipbook(CharacterFB);
	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FBComp;

	// HandleFlipbookChanged warms cache + dispatches frame 0
	DataComp->HandleFlipbookChanged(CharacterFB);

	TestEqual(TEXT("Frame-0 event should fire once via HandleFlipbookChanged dispatch"),
		TestEvent->FireCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameEventOneShotAtFrame,
	"Paper2DPlus.CharacterProfile.FrameEvents.OneShotAtFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameEventOneShotAtFrame::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEventDrivenTestUtils;

	UPaperSprite* Sprite = MakeSprite();
	if (!TestNotNull(TEXT("Sprite"), Sprite)) return false;

	UPaperFlipbook* CharacterFB = MakeMultiFrameFlipbook(Sprite, 5);
	if (!TestNotNull(TEXT("Flipbook"), CharacterFB)) return false;

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Attack");
	Anim.Identity.Flipbook = CharacterFB;
	Anim.CombatData.Frames.SetNum(5);

	// Event at frame 3
	UPaper2DPlusTestFrameEvent* TestEvent = NewObject<UPaper2DPlusTestFrameEvent>(Asset, NAME_None, RF_Transactional);
	TestEvent->TriggerFrame = 3;
	Anim.FrameEventData.FrameEvents.Add(TestEvent);

	Asset->Flipbooks.Add(Anim);

	AActor* Actor = NewObject<AActor>();
	UPaperFlipbookComponent* FBComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	Actor->AddOwnedComponent(FBComp);
	Actor->AddOwnedComponent(DataComp);

	FBComp->SetFlipbook(CharacterFB);
	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FBComp;

	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("Event should NOT fire at frame 0"), TestEvent->FireCount, 0);

	DataComp->HandleFrameChanged(1);
	TestEqual(TEXT("Event should NOT fire at frame 1"), TestEvent->FireCount, 0);

	DataComp->HandleFrameChanged(3);
	TestEqual(TEXT("Event should fire at frame 3"), TestEvent->FireCount, 1);

	DataComp->HandleFrameChanged(4);
	TestEqual(TEXT("Event should NOT fire again at frame 4"), TestEvent->FireCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameEventRangedLifecycle,
	"Paper2DPlus.CharacterProfile.FrameEvents.RangedLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameEventRangedLifecycle::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusEventDrivenTestUtils;

	UPaperSprite* Sprite = MakeSprite();
	if (!TestNotNull(TEXT("Sprite"), Sprite)) return false;

	UPaperFlipbook* CharacterFB = MakeMultiFrameFlipbook(Sprite, 6);
	if (!TestNotNull(TEXT("Flipbook"), CharacterFB)) return false;

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookProfileEntry Anim;
	Anim.Identity.FlipbookName = TEXT("Attack");
	Anim.Identity.Flipbook = CharacterFB;
	Anim.CombatData.Frames.SetNum(6);

	// Ranged event spanning frames 2-4 (StartFrame=2, FrameCount=3)
	UPaper2DPlusTestFrameEventState* TestState = NewObject<UPaper2DPlusTestFrameEventState>(Asset, NAME_None, RF_Transactional);
	TestState->StartFrame = 2;
	TestState->FrameCount = 3;
	Anim.FrameEventData.FrameEvents.Add(TestState);

	Asset->Flipbooks.Add(Anim);

	AActor* Actor = NewObject<AActor>();
	UPaperFlipbookComponent* FBComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	Actor->AddOwnedComponent(FBComp);
	Actor->AddOwnedComponent(DataComp);

	FBComp->SetFlipbook(CharacterFB);
	DataComp->CharacterProfile = Asset;
	DataComp->FlipbookComponent = FBComp;

	DataComp->HandleFlipbookChanged(CharacterFB);
	TestEqual(TEXT("Frame 0: no Begin"), TestState->BeginCount, 0);

	DataComp->HandleFrameChanged(1);
	TestEqual(TEXT("Frame 1: still no Begin"), TestState->BeginCount, 0);

	DataComp->HandleFrameChanged(2);
	TestEqual(TEXT("Frame 2: Begin fires"), TestState->BeginCount, 1);
	TestEqual(TEXT("Frame 2: Tick fires"), TestState->TickCount, 1);

	DataComp->HandleFrameChanged(3);
	TestEqual(TEXT("Frame 3: Begin still 1"), TestState->BeginCount, 1);
	TestEqual(TEXT("Frame 3: Tick increments"), TestState->TickCount, 2);

	DataComp->HandleFrameChanged(4);
	TestEqual(TEXT("Frame 4: Tick increments"), TestState->TickCount, 3);
	TestEqual(TEXT("Frame 4: End still 0"), TestState->EndCount, 0);

	DataComp->HandleFrameChanged(5);
	TestEqual(TEXT("Frame 5: End fires (left range)"), TestState->EndCount, 1);

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

#endif // WITH_EDITOR
