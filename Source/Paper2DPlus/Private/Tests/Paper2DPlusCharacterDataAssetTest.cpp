// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Paper2DPlusCharacterDataAsset.h"
#include "PaperFlipbook.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataValidateDuplicateAnimations,
	"Paper2DPlus.CharacterData.Validation.DuplicateAnimationNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataValidateDuplicateAnimations::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData AnimA;
	AnimA.AnimationName = TEXT("Idle");
	FFrameHitboxData FrameA;
	FHitboxData HitboxA;
	HitboxA.Width = 10;
	HitboxA.Height = 12;
	FrameA.Hitboxes.Add(HitboxA);
	AnimA.Frames.Add(FrameA);
	Asset->Animations.Add(AnimA);

	FAnimationHitboxData AnimB;
	AnimB.AnimationName = TEXT("idle"); // duplicate (case-insensitive)
	FFrameHitboxData FrameB;
	FHitboxData HitboxB;
	HitboxB.Width = 8;
	HitboxB.Height = 9;
	FrameB.Hitboxes.Add(HitboxB);
	AnimB.Frames.Add(FrameB);
	Asset->Animations.Add(AnimB);

	TArray<FCharacterDataValidationIssue> Issues;
	const bool bValid = Asset->ValidateCharacterDataAsset(Issues);

	TestFalse(TEXT("Duplicate animation names should fail validation"), bValid);

	bool bFoundDuplicateError = false;
	for (const FCharacterDataValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == ECharacterDataValidationSeverity::Error &&
			Issue.Message.Contains(TEXT("Duplicate animation name")))
		{
			bFoundDuplicateError = true;
			break;
		}
	}
	TestTrue(TEXT("Expected duplicate animation error issue"), bFoundDuplicateError);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataTrimTrailingFrames,
	"Paper2DPlus.CharacterData.Cleanup.TrimTrailingFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataTrimTrailingFrames::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData Anim;
	Anim.AnimationName = TEXT("Run");
	Anim.Flipbook = NewObject<UPaperFlipbook>(Asset);

	Anim.Frames.SetNum(3);
	Anim.FrameExtractionInfo.SetNum(4);

	Asset->Animations.Add(Anim);

	const int32 Removed = Asset->TrimAllTrailingFrameData();

	TestEqual(TEXT("All trailing entries should be removed when flipbook has 0 keyframes"), Removed, 7);
	TestEqual(TEXT("Frames should be trimmed to 0"), Asset->Animations[0].Frames.Num(), 0);
	TestEqual(TEXT("Extraction info should be trimmed to 0"), Asset->Animations[0].FrameExtractionInfo.Num(), 0);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataJsonRoundTrip,
	"Paper2DPlus.CharacterData.Serialization.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataJsonRoundTrip::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Source = NewObject<UPaper2DPlusCharacterDataAsset>();
	Source->DisplayName = TEXT("Adventurer");
	Source->DefaultAlphaThreshold = 22;
	Source->DefaultPadding = 3;
	Source->DefaultMinSpriteSize = 5;

	FAnimationHitboxData Anim;
	Anim.AnimationName = TEXT("Idle");
	FFrameHitboxData Frame;
	Frame.FrameName = TEXT("Idle_00");
	FHitboxData Hitbox;
	Hitbox.Type = EHitboxType::Hurtbox;
	Hitbox.Width = 10;
	Hitbox.Height = 12;
	Frame.Hitboxes.Add(Hitbox);
	Anim.Frames.Add(Frame);
	Source->Animations.Add(Anim);

	FString Json;
	const bool bExported = Source->ExportToJsonString(Json);
	TestTrue(TEXT("ExportToJsonString should succeed"), bExported);
	TestTrue(TEXT("Exported JSON should contain animation name"), Json.Contains(TEXT("Idle")));

	UPaper2DPlusCharacterDataAsset* Loaded = NewObject<UPaper2DPlusCharacterDataAsset>();
	const bool bImported = Loaded->ImportFromJsonString(Json);
	TestTrue(TEXT("ImportFromJsonString should succeed"), bImported);

	TestEqual(TEXT("DisplayName should round-trip"), Loaded->DisplayName, Source->DisplayName);
	TestEqual(TEXT("Animation count should round-trip"), Loaded->Animations.Num(), 1);
	if (Loaded->Animations.Num() > 0)
	{
		TestEqual(TEXT("AnimationName should round-trip"), Loaded->Animations[0].AnimationName, TEXT("Idle"));
		TestEqual(TEXT("Frame count should round-trip"), Loaded->Animations[0].Frames.Num(), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataValidateInvalidHitboxSize,
	"Paper2DPlus.CharacterData.Validation.InvalidHitboxSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataValidateInvalidHitboxSize::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData Anim;
	Anim.AnimationName = TEXT("Attack");
	FFrameHitboxData Frame;
	Frame.FrameName = TEXT("Attack_00");
	FHitboxData BadHitbox;
	BadHitbox.Width = 0;
	BadHitbox.Height = 8;
	Frame.Hitboxes.Add(BadHitbox);
	Anim.Frames.Add(Frame);
	Asset->Animations.Add(Anim);

	TArray<FCharacterDataValidationIssue> Issues;
	const bool bValid = Asset->ValidateCharacterDataAsset(Issues);
	TestFalse(TEXT("Validation should fail for non-positive hitbox dimensions"), bValid);

	bool bFoundInvalidSizeError = false;
	for (const FCharacterDataValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == ECharacterDataValidationSeverity::Error &&
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
	FPaper2DPlusCharacterDataBatchCopyRange,
	"Paper2DPlus.CharacterData.Batch.CopyFrameDataToRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataBatchCopyRange::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData Anim;
	Anim.AnimationName = TEXT("Combo");
	Anim.Frames.SetNum(4);
	Anim.Frames[1].FrameName = TEXT("Combo_01");
	FHitboxData HB;
	HB.Type = EHitboxType::Attack;
	HB.X = 10;
	HB.Y = 20;
	HB.Width = 30;
	HB.Height = 40;
	Anim.Frames[1].Hitboxes.Add(HB);
	Asset->Animations.Add(Anim);

	const bool bOk = Asset->CopyFrameDataToRange(TEXT("Combo"), 1, 2, 3, true);
	TestTrue(TEXT("CopyFrameDataToRange should succeed"), bOk);
	TestEqual(TEXT("Frame 2 hitbox count should be copied"), Asset->Animations[0].Frames[2].Hitboxes.Num(), 1);
	TestEqual(TEXT("Frame 3 hitbox count should be copied"), Asset->Animations[0].Frames[3].Hitboxes.Num(), 1);
	if (Asset->Animations[0].Frames[3].Hitboxes.Num() > 0)
	{
		TestEqual(TEXT("Copied hitbox X should match source"), Asset->Animations[0].Frames[3].Hitboxes[0].X, 10);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataBatchMirrorRange,
	"Paper2DPlus.CharacterData.Batch.MirrorHitboxesInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataBatchMirrorRange::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData Anim;
	Anim.AnimationName = TEXT("Run");
	Anim.Frames.SetNum(2);
	FHitboxData HB;
	HB.X = 10;
	HB.Y = 5;
	HB.Width = 20;
	HB.Height = 10;
	Anim.Frames[0].Hitboxes.Add(HB);
	Anim.Frames[1].Hitboxes.Add(HB);
	Asset->Animations.Add(Anim);

	const int32 Mirrored = Asset->MirrorHitboxesInRange(TEXT("Run"), 0, 1, 50);
	TestEqual(TEXT("Both hitboxes should be mirrored"), Mirrored, 2);
	// Right edge = 30. Mirrored X = 100 - 30 = 70.
	TestEqual(TEXT("Mirrored X should match expected value"), Asset->Animations[0].Frames[0].Hitboxes[0].X, 70);
	TestEqual(TEXT("Mirrored X should match expected value (frame 1)"), Asset->Animations[0].Frames[1].Hitboxes[0].X, 70);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataImportInvalidJson,
	"Paper2DPlus.CharacterData.Serialization.ImportInvalidJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataImportInvalidJson::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();
	const bool bImported = Asset->ImportFromJsonString(TEXT("{ not-valid-json"));
	TestFalse(TEXT("Import should fail for malformed JSON"), bImported);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataBatchCopyRangeNoSockets,
	"Paper2DPlus.CharacterData.Batch.CopyFrameDataToRange_NoSockets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataBatchCopyRangeNoSockets::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData Anim;
	Anim.AnimationName = TEXT("Combo");
	Anim.Frames.SetNum(3);
	FHitboxData HB;
	HB.X = 8;
	HB.Y = 9;
	HB.Width = 10;
	HB.Height = 11;
	Anim.Frames[0].Hitboxes.Add(HB);
	FSocketData Sock;
	Sock.Name = TEXT("Hand");
	Sock.X = 3;
	Sock.Y = 4;
	Anim.Frames[0].Sockets.Add(Sock);
	FSocketData ExistingSock;
	ExistingSock.Name = TEXT("Existing");
	ExistingSock.X = 1;
	ExistingSock.Y = 2;
	Anim.Frames[2].Sockets.Add(ExistingSock);
	Asset->Animations.Add(Anim);

	const bool bOk = Asset->CopyFrameDataToRange(TEXT("Combo"), 0, 1, 2, false);
	TestTrue(TEXT("CopyFrameDataToRange should succeed"), bOk);
	TestEqual(TEXT("Hitboxes should copy to range"), Asset->Animations[0].Frames[2].Hitboxes.Num(), 1);
	TestEqual(TEXT("Sockets should remain unchanged when include-sockets is false"), Asset->Animations[0].Frames[2].Sockets.Num(), 1);
	TestEqual(TEXT("Existing socket should remain"), Asset->Animations[0].Frames[2].Sockets[0].Name, TEXT("Existing"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataBatchMirrorRangeClamped,
	"Paper2DPlus.CharacterData.Batch.MirrorHitboxesInRange_Clamped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataBatchMirrorRangeClamped::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData Anim;
	Anim.AnimationName = TEXT("Run");
	Anim.Frames.SetNum(2);
	FHitboxData HB;
	HB.X = 5;
	HB.Y = 0;
	HB.Width = 10;
	HB.Height = 10;
	Anim.Frames[0].Hitboxes.Add(HB);
	Anim.Frames[1].Hitboxes.Add(HB);
	Asset->Animations.Add(Anim);

	// Intentionally out-of-bounds range should clamp to [0,1]
	const int32 Mirrored = Asset->MirrorHitboxesInRange(TEXT("Run"), -10, 50, 20);
	TestEqual(TEXT("Both frame hitboxes should still mirror due to clamped range"), Mirrored, 2);
	// right=15 => x=(40-15)=25
	TestEqual(TEXT("Mirrored X frame 0"), Asset->Animations[0].Frames[0].Hitboxes[0].X, 25);
	TestEqual(TEXT("Mirrored X frame 1"), Asset->Animations[0].Frames[1].Hitboxes[0].X, 25);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataSetSpriteFlipRange,
	"Paper2DPlus.CharacterData.Batch.SetSpriteFlipInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataSetSpriteFlipRange::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData Anim;
	Anim.AnimationName = TEXT("Idle");
	Anim.Frames.SetNum(3);
	Anim.FrameExtractionInfo.SetNum(3);
	Asset->Animations.Add(Anim);

	const int32 Updated = Asset->SetSpriteFlipInRange(TEXT("Idle"), 1, 2, true, false);
	TestEqual(TEXT("Two frames should be updated"), Updated, 2);
	TestFalse(TEXT("Frame 0 should remain unflipped"), Asset->Animations[0].FrameExtractionInfo[0].bFlipX);
	TestTrue(TEXT("Frame 1 FlipX should be true"), Asset->Animations[0].FrameExtractionInfo[1].bFlipX);
	TestTrue(TEXT("Frame 2 FlipX should be true"), Asset->Animations[0].FrameExtractionInfo[2].bFlipX);
	TestFalse(TEXT("Frame 1 FlipY should be false"), Asset->Animations[0].FrameExtractionInfo[1].bFlipY);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataSetSpriteFlipForAnimation,
	"Paper2DPlus.CharacterData.Batch.SetSpriteFlipForAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataSetSpriteFlipForAnimation::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData Idle;
	Idle.AnimationName = TEXT("Idle");
	Idle.Frames.SetNum(2);
	Asset->Animations.Add(Idle);

	const int32 Updated = Asset->SetSpriteFlipForAnimation(TEXT("Idle"), false, true);
	TestEqual(TEXT("All Idle frames should be updated"), Updated, 2);
	TestTrue(TEXT("Idle frame 0 flip Y"), Asset->Animations[0].FrameExtractionInfo[0].bFlipY);
	TestTrue(TEXT("Idle frame 1 flip Y"), Asset->Animations[0].FrameExtractionInfo[1].bFlipY);
	TestFalse(TEXT("Idle frame 1 flip X"), Asset->Animations[0].FrameExtractionInfo[1].bFlipX);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataSetSpriteFlipForAllAnimations,
	"Paper2DPlus.CharacterData.Batch.SetSpriteFlipForAllAnimations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataSetSpriteFlipForAllAnimations::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	FAnimationHitboxData Idle;
	Idle.AnimationName = TEXT("Idle");
	Idle.Frames.SetNum(2);
	Asset->Animations.Add(Idle);

	FAnimationHitboxData Run;
	Run.AnimationName = TEXT("Run");
	Run.Frames.SetNum(1);
	Asset->Animations.Add(Run);

	const int32 Updated = Asset->SetSpriteFlipForAllAnimations(true, true);
	TestEqual(TEXT("All frames across all animations should be updated"), Updated, 3);
	TestTrue(TEXT("Idle frame 0 flip X"), Asset->Animations[0].FrameExtractionInfo[0].bFlipX);
	TestTrue(TEXT("Idle frame 1 flip Y"), Asset->Animations[0].FrameExtractionInfo[1].bFlipY);
	TestTrue(TEXT("Run frame 0 flip X"), Asset->Animations[1].FrameExtractionInfo[0].bFlipX);

	return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataImportLegacySchemaZero,
	"Paper2DPlus.CharacterData.Serialization.ImportLegacySchemaZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataImportLegacySchemaZero::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	const FString LegacyJson = TEXT("{\"SchemaVersion\":0,\"DisplayName\":\"Legacy\",\"Animations\":[]}");
	const bool bImported = Asset->ImportFromJsonString(LegacyJson);
	TestTrue(TEXT("Import should migrate legacy schema version 0 to current"), bImported);
	TestEqual(TEXT("DisplayName should import from legacy payload"), Asset->DisplayName, FString(TEXT("Legacy")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataImportMissingSchemaField,
	"Paper2DPlus.CharacterData.Serialization.ImportMissingSchemaField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataImportMissingSchemaField::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	const FString JsonWithoutSchema = TEXT("{\"DisplayName\":\"NoSchema\",\"Animations\":[]}");
	const bool bImported = Asset->ImportFromJsonString(JsonWithoutSchema);
	TestTrue(TEXT("Import should accept payload without explicit schema field"), bImported);
	TestEqual(TEXT("DisplayName should import when schema field is absent"), Asset->DisplayName, FString(TEXT("NoSchema")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataImportFutureSchemaRejected,
	"Paper2DPlus.CharacterData.Serialization.ImportFutureSchemaRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataImportFutureSchemaRejected::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Asset = NewObject<UPaper2DPlusCharacterDataAsset>();

	const FString JsonWithFutureSchema = TEXT("{\"SchemaVersion\":999,\"DisplayName\":\"Future\",\"Animations\":[]}");
	const bool bImported = Asset->ImportFromJsonString(JsonWithFutureSchema);
	TestFalse(TEXT("Import should fail for unsupported future schema version"), bImported);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterDataJsonFileRoundTrip,
	"Paper2DPlus.CharacterData.Serialization.JsonFileRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterDataJsonFileRoundTrip::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterDataAsset* Source = NewObject<UPaper2DPlusCharacterDataAsset>();
	Source->DisplayName = TEXT("FileRoundTripCharacter");

	FAnimationHitboxData Anim;
	Anim.AnimationName = TEXT("Idle");
	Anim.Frames.SetNum(1);
	Source->Animations.Add(Anim);

	const FString TempFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Paper2DPlus"), TEXT("CharacterDataJsonFileRoundTrip_Test.json"));

	const bool bExported = Source->ExportToJsonFile(TempFile);
	TestTrue(TEXT("ExportToJsonFile should succeed"), bExported);

	UPaper2DPlusCharacterDataAsset* Loaded = NewObject<UPaper2DPlusCharacterDataAsset>();
	const bool bImported = Loaded->ImportFromJsonFile(TempFile);
	TestTrue(TEXT("ImportFromJsonFile should succeed"), bImported);
	TestEqual(TEXT("DisplayName should round-trip from file"), Loaded->DisplayName, Source->DisplayName);
	TestEqual(TEXT("Animation count should round-trip from file"), Loaded->Animations.Num(), Source->Animations.Num());

	IFileManager::Get().Delete(*TempFile, false, true, true);
	return true;
}
