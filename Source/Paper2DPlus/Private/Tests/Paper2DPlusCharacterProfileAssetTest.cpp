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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileValidateDuplicateFlipbooks,
	"Paper2DPlus.CharacterProfile.Validation.DuplicateFlipbookNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileValidateDuplicateFlipbooks::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookHitboxData AnimA;
	AnimA.FlipbookName = TEXT("Idle");
	FFrameHitboxData FrameA;
	FHitboxData HitboxA;
	HitboxA.Width = 10;
	HitboxA.Height = 12;
	FrameA.Hitboxes.Add(HitboxA);
	AnimA.Frames.Add(FrameA);
	Asset->Flipbooks.Add(AnimA);

	FFlipbookHitboxData AnimB;
	AnimB.FlipbookName = TEXT("idle"); // duplicate (case-insensitive)
	FFrameHitboxData FrameB;
	FHitboxData HitboxB;
	HitboxB.Width = 8;
	HitboxB.Height = 9;
	FrameB.Hitboxes.Add(HitboxB);
	AnimB.Frames.Add(FrameB);
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

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Run");
	Anim.Flipbook = NewObject<UPaperFlipbook>(Asset);

	Anim.Frames.SetNum(3);
	Anim.FrameExtractionInfo.SetNum(4);

	Asset->Flipbooks.Add(Anim);

	const int32 Removed = Asset->TrimAllTrailingFrameData();

	TestEqual(TEXT("All trailing entries should be removed when flipbook has 0 keyframes"), Removed, 7);
	TestEqual(TEXT("Frames should be trimmed to 0"), Asset->Flipbooks[0].Frames.Num(), 0);
	TestEqual(TEXT("Extraction info should be trimmed to 0"), Asset->Flipbooks[0].FrameExtractionInfo.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileExcludeRestoreFrame,
	"Paper2DPlus.CharacterProfile.Frames.ExcludeRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileExcludeRestoreFrame::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Idle");

	UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Asset);
	Anim.Flipbook = Flipbook;

	Anim.Frames.SetNum(3);
	Anim.Frames[0].FrameName = TEXT("Idle_00");
	Anim.Frames[1].FrameName = TEXT("Idle_01");
	Anim.Frames[2].FrameName = TEXT("Idle_02");
	Anim.FrameExtractionInfo.SetNum(3);

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
	TestEqual(TEXT("Active frame metadata should remove one frame"), Asset->Flipbooks[0].Frames.Num(), 2);
	TestEqual(TEXT("One frame should be stored as excluded"), Asset->GetExcludedFlipbookFrameCount(0), 1);

	const bool bRestored = Asset->RestoreExcludedFlipbookFrame(0, 0);
	TestTrue(TEXT("RestoreExcludedFlipbookFrame should succeed"), bRestored);
	TestEqual(TEXT("Live flipbook should restore removed keyframe"), Flipbook->GetNumKeyFrames(), 3);
	TestEqual(TEXT("Active frame metadata should restore removed frame"), Asset->Flipbooks[0].Frames.Num(), 3);
	TestEqual(TEXT("Excluded storage should be empty after restore"), Asset->GetExcludedFlipbookFrameCount(0), 0);
	TestEqual(TEXT("Restored frame name should return to original slot"), Asset->Flipbooks[0].Frames[1].FrameName, FString(TEXT("Idle_01")));
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

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Idle");
	FFrameHitboxData Frame;
	Frame.FrameName = TEXT("Idle_00");
	FHitboxData Hitbox;
	Hitbox.Type = EHitboxType::Hurtbox;
	Hitbox.Width = 10;
	Hitbox.Height = 12;
	Frame.Hitboxes.Add(Hitbox);
	Anim.Frames.Add(Frame);
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
		TestEqual(TEXT("AnimationName should round-trip"), Loaded->Flipbooks[0].FlipbookName, TEXT("Idle"));
		TestEqual(TEXT("Frame count should round-trip"), Loaded->Flipbooks[0].Frames.Num(), 1);
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

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Attack");
	FFrameHitboxData Frame;
	Frame.FrameName = TEXT("Attack_00");
	FHitboxData BadHitbox;
	BadHitbox.Width = 0;
	BadHitbox.Height = 8;
	Frame.Hitboxes.Add(BadHitbox);
	Anim.Frames.Add(Frame);
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

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Combo");
	Anim.Frames.SetNum(4);
	Anim.Frames[1].FrameName = TEXT("Combo_01");
	FHitboxData HB;
	HB.Type = EHitboxType::Attack;
	HB.X = 10;
	HB.Y = 20;
	HB.Width = 30;
	HB.Height = 40;
	Anim.Frames[1].Hitboxes.Add(HB);
	Asset->Flipbooks.Add(Anim);

	const bool bOk = Asset->CopyFrameDataToRange(TEXT("Combo"), 1, 2, 3, true);
	TestTrue(TEXT("CopyFrameDataToRange should succeed"), bOk);
	TestEqual(TEXT("Frame 2 hitbox count should be copied"), Asset->Flipbooks[0].Frames[2].Hitboxes.Num(), 1);
	TestEqual(TEXT("Frame 3 hitbox count should be copied"), Asset->Flipbooks[0].Frames[3].Hitboxes.Num(), 1);
	if (Asset->Flipbooks[0].Frames[3].Hitboxes.Num() > 0)
	{
		TestEqual(TEXT("Copied hitbox X should match source"), Asset->Flipbooks[0].Frames[3].Hitboxes[0].X, 10);
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

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Run");
	Anim.Frames.SetNum(2);
	FHitboxData HB;
	HB.X = 10;
	HB.Y = 5;
	HB.Width = 20;
	HB.Height = 10;
	Anim.Frames[0].Hitboxes.Add(HB);
	Anim.Frames[1].Hitboxes.Add(HB);
	Asset->Flipbooks.Add(Anim);

	const int32 Mirrored = Asset->MirrorHitboxesInRange(TEXT("Run"), 0, 1, 50);
	TestEqual(TEXT("Both hitboxes should be mirrored"), Mirrored, 2);
	// Right edge = 30. Mirrored X = 100 - 30 = 70.
	TestEqual(TEXT("Mirrored X should match expected value"), Asset->Flipbooks[0].Frames[0].Hitboxes[0].X, 70);
	TestEqual(TEXT("Mirrored X should match expected value (frame 1)"), Asset->Flipbooks[0].Frames[1].Hitboxes[0].X, 70);

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

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Combo");
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
	Asset->Flipbooks.Add(Anim);

	const bool bOk = Asset->CopyFrameDataToRange(TEXT("Combo"), 0, 1, 2, false);
	TestTrue(TEXT("CopyFrameDataToRange should succeed"), bOk);
	TestEqual(TEXT("Hitboxes should copy to range"), Asset->Flipbooks[0].Frames[2].Hitboxes.Num(), 1);
	TestEqual(TEXT("Sockets should remain unchanged when include-sockets is false"), Asset->Flipbooks[0].Frames[2].Sockets.Num(), 1);
	TestEqual(TEXT("Existing socket should remain"), Asset->Flipbooks[0].Frames[2].Sockets[0].Name, TEXT("Existing"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileBatchMirrorRangeClamped,
	"Paper2DPlus.CharacterProfile.Batch.MirrorHitboxesInRange_Clamped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileBatchMirrorRangeClamped::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Run");
	Anim.Frames.SetNum(2);
	FHitboxData HB;
	HB.X = 5;
	HB.Y = 0;
	HB.Width = 10;
	HB.Height = 10;
	Anim.Frames[0].Hitboxes.Add(HB);
	Anim.Frames[1].Hitboxes.Add(HB);
	Asset->Flipbooks.Add(Anim);

	// Intentionally out-of-bounds range should clamp to [0,1]
	const int32 Mirrored = Asset->MirrorHitboxesInRange(TEXT("Run"), -10, 50, 20);
	TestEqual(TEXT("Both frame hitboxes should still mirror due to clamped range"), Mirrored, 2);
	// right=15 => x=(40-15)=25
	TestEqual(TEXT("Mirrored X frame 0"), Asset->Flipbooks[0].Frames[0].Hitboxes[0].X, 25);
	TestEqual(TEXT("Mirrored X frame 1"), Asset->Flipbooks[0].Frames[1].Hitboxes[0].X, 25);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileSetSpriteFlipRange,
	"Paper2DPlus.CharacterProfile.Batch.SetSpriteFlipInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileSetSpriteFlipRange::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Idle");
	Anim.Frames.SetNum(3);
	Anim.FrameExtractionInfo.SetNum(3);
	Asset->Flipbooks.Add(Anim);

	const int32 Updated = Asset->SetSpriteFlipInRange(TEXT("Idle"), 1, 2, true, false);
	TestEqual(TEXT("Two frames should be updated"), Updated, 2);
	TestFalse(TEXT("Frame 0 should remain unflipped"), Asset->Flipbooks[0].FrameExtractionInfo[0].bFlipX);
	TestTrue(TEXT("Frame 1 FlipX should be true"), Asset->Flipbooks[0].FrameExtractionInfo[1].bFlipX);
	TestTrue(TEXT("Frame 2 FlipX should be true"), Asset->Flipbooks[0].FrameExtractionInfo[2].bFlipX);
	TestFalse(TEXT("Frame 1 FlipY should be false"), Asset->Flipbooks[0].FrameExtractionInfo[1].bFlipY);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileSetSpriteFlipForFlipbook,
	"Paper2DPlus.CharacterProfile.Batch.SetSpriteFlipForFlipbook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileSetSpriteFlipForFlipbook::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookHitboxData Idle;
	Idle.FlipbookName = TEXT("Idle");
	Idle.Frames.SetNum(2);
	Asset->Flipbooks.Add(Idle);

	const int32 Updated = Asset->SetSpriteFlipForFlipbook(TEXT("Idle"), false, true);
	TestEqual(TEXT("All Idle frames should be updated"), Updated, 2);
	TestTrue(TEXT("Idle frame 0 flip Y"), Asset->Flipbooks[0].FrameExtractionInfo[0].bFlipY);
	TestTrue(TEXT("Idle frame 1 flip Y"), Asset->Flipbooks[0].FrameExtractionInfo[1].bFlipY);
	TestFalse(TEXT("Idle frame 1 flip X"), Asset->Flipbooks[0].FrameExtractionInfo[1].bFlipX);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCharacterProfileSetSpriteFlipForAllFlipbooks,
	"Paper2DPlus.CharacterProfile.Batch.SetSpriteFlipForAllFlipbooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCharacterProfileSetSpriteFlipForAllFlipbooks::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();

	FFlipbookHitboxData Idle;
	Idle.FlipbookName = TEXT("Idle");
	Idle.Frames.SetNum(2);
	Asset->Flipbooks.Add(Idle);

	FFlipbookHitboxData Run;
	Run.FlipbookName = TEXT("Run");
	Run.Frames.SetNum(1);
	Asset->Flipbooks.Add(Run);

	const int32 Updated = Asset->SetSpriteFlipForAllFlipbooks(true, true);
	TestEqual(TEXT("All frames across all animations should be updated"), Updated, 3);
	TestTrue(TEXT("Idle frame 0 flip X"), Asset->Flipbooks[0].FrameExtractionInfo[0].bFlipX);
	TestTrue(TEXT("Idle frame 1 flip Y"), Asset->Flipbooks[0].FrameExtractionInfo[1].bFlipY);
	TestTrue(TEXT("Run frame 0 flip X"), Asset->Flipbooks[1].FrameExtractionInfo[0].bFlipX);

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

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("Idle");
	Anim.Frames.SetNum(1);
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

	FFlipbookHitboxData AttackerAnim;
	AttackerAnim.FlipbookName = TEXT("Attack");
	AttackerAnim.Flipbook = Flipbook;
	FFrameHitboxData AttackerFrame;
	AttackerFrame.Hitboxes.Add(AttackHitbox);
	AttackerAnim.Frames.Add(AttackerFrame);
	AttackerAsset->Flipbooks.Add(AttackerAnim);

	FFlipbookHitboxData DefenderAnim;
	DefenderAnim.FlipbookName = TEXT("Hurt");
	DefenderAnim.Flipbook = Flipbook;
	FFrameHitboxData DefenderFrame;
	DefenderFrame.Hitboxes.Add(Hurtbox);
	DefenderAnim.Frames.Add(DefenderFrame);
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

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("FlipAttack");
	Anim.Flipbook = Flipbook;
	FFrameHitboxData Frame;
	Frame.Hitboxes.Add(AttackHitbox);
	Anim.Frames.Add(Frame);
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

	FFlipbookHitboxData Anim;
	Anim.FlipbookName = TEXT("ScaledAttack");
	Anim.Flipbook = Flipbook;
	FFrameHitboxData Frame;
	Frame.Hitboxes.Add(AttackHitbox);
	Anim.Frames.Add(Frame);
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

#endif // WITH_EDITOR
