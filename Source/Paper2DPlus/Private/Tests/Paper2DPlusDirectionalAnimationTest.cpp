// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "GameFramework/Actor.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusDirectionalAnimationLibrary.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "SpriteEditorOnlyTypes.h"

#include <limits>

namespace
{
	UPaperFlipbook* DirectionalAnimation_MakeFlipbook(UObject* Owner, const TCHAR* Name)
	{
		return NewObject<UPaperFlipbook>(Owner, Name);
	}

	UPaperFlipbook* DirectionalAnimation_AddMove(
		UPaper2DPlusCharacterProfileAsset* Asset,
		const TCHAR* MoveName)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = MoveName;
		UPaperFlipbook* Flipbook = DirectionalAnimation_MakeFlipbook(Asset, MoveName);
		Entry.Identity.Flipbook = Flipbook;
		Asset->Flipbooks.Add(MoveTemp(Entry));
		return Flipbook;
	}

	struct FDirectionalAnimationValidationFixture
	{
		UPaper2DPlusCharacterProfileAsset* Asset = nullptr;
		UPaperFlipbook* Base = nullptr;
		UPaperFlipbook* Variant = nullptr;
		TArray<UPaperSprite*> BaseSprites;
		TArray<UPaperSprite*> VariantSprites;
		int32 SlotIndex = 3;
	};

	UPaperSprite* DirectionalAnimation_MakeSprite(
		UObject* Owner,
		const TCHAR* Name,
		const FIntPoint& Dimension = FIntPoint(32, 48),
		float PixelsPerUnrealUnit = 1.0f)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(128, 128, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

		UPaperSprite* Sprite = NewObject<UPaperSprite>(Owner, Name);
		if (!Sprite)
		{
			return nullptr;
		}

		FSpriteAssetInitParameters Init;
		Init.Texture = Texture;
		Init.Offset = FIntPoint(8, 12);
		Init.Dimension = Dimension;
		Init.SetPixelsPerUnrealUnit(PixelsPerUnrealUnit);
		Sprite->InitializeSprite(Init, /*bRebuildData=*/true);
		Sprite->SetTrim(
			/*bTrimmed=*/true,
			FVector2D(5.0, 7.0),
			FVector2D(64.0, 80.0),
			/*bRebuildData=*/true);
		return Sprite;
	}

	UPaperFlipbook* DirectionalAnimation_MakeTimedFlipbook(
		UObject* Owner,
		const TCHAR* Name,
		const TArray<UPaperSprite*>& Sprites,
		const TArray<int32>& FrameRuns,
		float FramesPerSecond = 12.0f)
	{
		UPaperFlipbook* Flipbook = DirectionalAnimation_MakeFlipbook(Owner, Name);
		if (!Flipbook || Sprites.Num() != FrameRuns.Num())
		{
			return nullptr;
		}

		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = FramesPerSecond;
		Mutator.KeyFrames.Reset(FrameRuns.Num());
		for (int32 KeyFrameIndex = 0; KeyFrameIndex < FrameRuns.Num(); ++KeyFrameIndex)
		{
			FPaperFlipbookKeyFrame& KeyFrame = Mutator.KeyFrames.AddDefaulted_GetRef();
			KeyFrame.Sprite = Sprites[KeyFrameIndex];
			KeyFrame.FrameRun = FrameRuns[KeyFrameIndex];
		}
		return Flipbook;
	}

	FDirectionalAnimationValidationFixture DirectionalAnimation_MakeValidationFixture()
	{
		FDirectionalAnimationValidationFixture Fixture;
		Fixture.Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		const TArray<int32> FrameRuns = { 1, 3, 2, 4, 1 };
		for (int32 KeyFrameIndex = 0; KeyFrameIndex < FrameRuns.Num(); ++KeyFrameIndex)
		{
			UPaperSprite* BaseSprite = DirectionalAnimation_MakeSprite(
				Fixture.Asset,
				*FString::Printf(TEXT("DirectionalBaseSprite%d"), KeyFrameIndex));
			Fixture.BaseSprites.Add(BaseSprite);
			Fixture.VariantSprites.Add(DuplicateObject<UPaperSprite>(
				BaseSprite,
				Fixture.Asset,
				*FString::Printf(TEXT("DirectionalVariantSprite%d"), KeyFrameIndex)));
		}

		Fixture.Base = DirectionalAnimation_MakeTimedFlipbook(
			Fixture.Asset,
			TEXT("DirectionalBase"),
			Fixture.BaseSprites,
			FrameRuns);
		Fixture.Variant = DirectionalAnimation_MakeTimedFlipbook(
			Fixture.Asset,
			TEXT("DirectionalVariant"),
			Fixture.VariantSprites,
			FrameRuns);

		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = TEXT("Shoot");
		Entry.Identity.Flipbook = Fixture.Base;
		Entry.CombatData.Frames.SetNum(FrameRuns.Num());
		Fixture.Asset->Flipbooks.Add(MoveTemp(Entry));
		Fixture.Asset->SetDirectionalSlot(0, Fixture.SlotIndex, Fixture.Variant);
		return Fixture;
	}

	const FCharacterProfileValidationIssue* DirectionalAnimation_FindIssueOfSeverity(
		const TArray<FCharacterProfileValidationIssue>& Issues,
		ECharacterProfileValidationSeverity Severity,
		const FString& ContextNeedle,
		const FString& MessageNeedle)
	{
		for (const FCharacterProfileValidationIssue& Issue : Issues)
		{
			if (Issue.Severity == Severity
				&& Issue.Context.Contains(ContextNeedle)
				&& Issue.Message.Contains(MessageNeedle))
			{
				return &Issue;
			}
		}
		return nullptr;
	}

	const FCharacterProfileValidationIssue* DirectionalAnimation_FindIssue(
		const TArray<FCharacterProfileValidationIssue>& Issues,
		const FString& ContextNeedle,
		const FString& MessageNeedle)
	{
		return DirectionalAnimation_FindIssueOfSeverity(
			Issues,
			ECharacterProfileValidationSeverity::Error,
			ContextNeedle,
			MessageNeedle);
	}

	bool DirectionalAnimation_RunGeometryMismatch(
		FAutomationTestBase& Test,
		const TCHAR* ExpectedField,
		TFunctionRef<void(FDirectionalAnimationValidationFixture&)> Mutate)
	{
		FDirectionalAnimationValidationFixture Fixture =
			DirectionalAnimation_MakeValidationFixture();
		Mutate(Fixture);

		TArray<FCharacterProfileValidationIssue> Issues;
		Test.TestFalse(TEXT("A one-field directional geometry drift rejects the Profile"),
			Fixture.Asset->ValidateCharacterProfileAsset(Issues));
		const FString ContextNeedle = FString::Printf(
			TEXT("Flipbook[0] 'Shoot' Directional Slot[%d] KeyFrame[2]"),
			Fixture.SlotIndex);
		Test.TestNotNull(
			*FString::Printf(TEXT("The diagnostic names key frame 2 and geometry field %s"), ExpectedField),
			DirectionalAnimation_FindIssue(Issues, ContextNeedle, ExpectedField));
		return !Test.HasAnyErrors();
	}

	// Silhouette fields (trim rectangle, render bounds) legitimately differ on trimmed or
	// facing-specific art, so their drift is advisory: validation still PASSES and the field is
	// named in a Warning, never an Error.
	bool DirectionalAnimation_RunSilhouetteDelta(
		FAutomationTestBase& Test,
		const TCHAR* ExpectedField,
		TFunctionRef<void(FDirectionalAnimationValidationFixture&)> Mutate)
	{
		FDirectionalAnimationValidationFixture Fixture =
			DirectionalAnimation_MakeValidationFixture();
		Mutate(Fixture);

		TArray<FCharacterProfileValidationIssue> Issues;
		Test.TestTrue(TEXT("A silhouette-only directional delta keeps the Profile valid"),
			Fixture.Asset->ValidateCharacterProfileAsset(Issues));
		const FString ContextNeedle = FString::Printf(
			TEXT("Flipbook[0] 'Shoot' Directional Slot[%d] KeyFrame[2]"),
			Fixture.SlotIndex);
		Test.TestNotNull(
			*FString::Printf(
				TEXT("The advisory Warning names key frame 2 and silhouette field %s"),
				ExpectedField),
			DirectionalAnimation_FindIssueOfSeverity(
				Issues,
				ECharacterProfileValidationSeverity::Warning,
				ContextNeedle,
				ExpectedField));
		Test.TestNull(
			*FString::Printf(TEXT("Silhouette field %s never raises a blocking Error"), ExpectedField),
			DirectionalAnimation_FindIssue(Issues, ContextNeedle, ExpectedField));
		return !Test.HasAnyErrors();
	}

	struct FDirectionalAnimationQueryFixture
	{
		UPaper2DPlusCharacterProfileAsset* Asset = nullptr;
		UPaperFlipbook* Base = nullptr;
		UPaperFlipbook* SlotZero = nullptr;
		UPaperFlipbook* SlotThree = nullptr;
	};

	FDirectionalAnimationQueryFixture DirectionalAnimation_MakeQueryFixture()
	{
		FDirectionalAnimationQueryFixture Fixture;
		Fixture.Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
		Fixture.Base = DirectionalAnimation_AddMove(Fixture.Asset, TEXT("DirectionalQueryBase"));
		Fixture.SlotZero = DirectionalAnimation_MakeFlipbook(
			Fixture.Asset,
			TEXT("DirectionalQuerySlotZero"));
		Fixture.SlotThree = DirectionalAnimation_MakeFlipbook(
			Fixture.Asset,
			TEXT("DirectionalQuerySlotThree"));
		Fixture.Asset->SetDirectionalSlot(0, 0, Fixture.SlotZero);
		Fixture.Asset->SetDirectionalSlot(0, 3, Fixture.SlotThree);
		return Fixture;
	}

	void DirectionalAnimation_SeedResolveOutputs(
		UPaperFlipbook* Sentinel,
		UPaperFlipbook*& OutFlipbook,
		int32& OutSlotIndex,
		bool& bOutMirror)
	{
		OutFlipbook = Sentinel;
		OutSlotIndex = 11;
		bOutMirror = true;
	}

	void DirectionalAnimation_SeedOccupiedOutputs(
		UPaperFlipbook* Sentinel,
		TArray<FPaper2DPlusOccupiedDirectionSlot>& OutSlots)
	{
		OutSlots.Reset();
		FPaper2DPlusOccupiedDirectionSlot& Slot = OutSlots.AddDefaulted_GetRef();
		Slot.SlotIndex = 11;
		Slot.Flipbook = Sentinel;
	}

	void DirectionalAnimation_AssertInvalidProfileData(
		FAutomationTestBase& Test,
		const FString& CaseLabel,
		UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Key)
	{
		Test.TestFalse(
			*FString::Printf(TEXT("%s: Has Multi Direction fails closed"), *CaseLabel),
			UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(Asset, Key));

		UPaperFlipbook* ResolvedFlipbook = nullptr;
		int32 ResolvedSlotIndex = INDEX_NONE;
		bool bResolvedMirror = false;
		DirectionalAnimation_SeedResolveOutputs(
			Key,
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror);
		Test.TestEqual(
			*FString::Printf(TEXT("%s: resolve reports Invalid Profile Data"), *CaseLabel),
			UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
				Asset,
				Key,
				FVector2D(0.0, 1.0),
				ResolvedFlipbook,
				ResolvedSlotIndex,
				bResolvedMirror),
			EPaper2DPlusDirectionalAnimationResult::InvalidProfileData);
		Test.TestNull(
			*FString::Printf(TEXT("%s: resolve clears the flipbook"), *CaseLabel),
			ResolvedFlipbook);
		Test.TestEqual(
			*FString::Printf(TEXT("%s: resolve clears the slot index"), *CaseLabel),
			ResolvedSlotIndex,
			INDEX_NONE);
		Test.TestFalse(
			*FString::Printf(TEXT("%s: resolve clears the mirror flag"), *CaseLabel),
			bResolvedMirror);

		TArray<FPaper2DPlusOccupiedDirectionSlot> OccupiedSlots;
		DirectionalAnimation_SeedOccupiedOutputs(Key, OccupiedSlots);
		Test.TestEqual(
			*FString::Printf(TEXT("%s: enumeration reports Invalid Profile Data"), *CaseLabel),
			UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
				Asset,
				Key,
				OccupiedSlots),
			EPaper2DPlusDirectionalAnimationResult::InvalidProfileData);
		Test.TestTrue(
			*FString::Printf(TEXT("%s: enumeration clears every partial record"), *CaseLabel),
			OccupiedSlots.IsEmpty());
	}

	UPaperFlipbook* DirectionalAnimation_AddStoredVariant(
		UPaper2DPlusCharacterProfileAsset* Asset,
		int32 OwnerIndex,
		int32 SlotIndex,
		const TCHAR* Name)
	{
		UPaperFlipbook* Variant = DirectionalAnimation_MakeFlipbook(Asset, Name);
		FPaper2DPlusDirectionalAnimationSlot& Slot = Asset->Flipbooks[OwnerIndex]
			.DirectionalAnimationData.Slots.AddDefaulted_GetRef();
		Slot.SlotIndex = SlotIndex;
		Slot.Flipbook = Variant;
		return Variant;
	}

	void DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		FAutomationTestBase& Test,
		const FString& CaseLabel,
		UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Base,
		UPaperFlipbook* Variant)
	{
		DirectionalAnimation_AssertInvalidProfileData(
			Test,
			CaseLabel + TEXT(" (base key)"),
			Asset,
			Base);
		DirectionalAnimation_AssertInvalidProfileData(
			Test,
			CaseLabel + TEXT(" (stored variant key)"),
			Asset,
			Variant);
	}

	void DirectionalAnimation_AssertBaseQueryRejectsAmbiguousVariant(
		FAutomationTestBase& Test,
		const FString& CaseLabel,
		UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* CanonicalBase)
	{
		UPaperFlipbook* ResolvedFlipbook = nullptr;
		int32 ResolvedSlotIndex = INDEX_NONE;
		bool bResolvedMirror = false;
		DirectionalAnimation_SeedResolveOutputs(
			CanonicalBase,
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror);
		Test.TestEqual(
			*FString::Printf(TEXT("%s: base-key resolve rejects the ambiguous variant"), *CaseLabel),
			UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
				Asset,
				CanonicalBase,
				FVector2D(1.0, -1.0),
				ResolvedFlipbook,
				ResolvedSlotIndex,
				bResolvedMirror),
			EPaper2DPlusDirectionalAnimationResult::AmbiguousFlipbook);
		Test.TestNull(
			*FString::Printf(TEXT("%s: ambiguous variant clears the resolved flipbook"), *CaseLabel),
			ResolvedFlipbook);
		Test.TestEqual(
			*FString::Printf(TEXT("%s: ambiguous variant clears the resolved slot"), *CaseLabel),
			ResolvedSlotIndex,
			INDEX_NONE);

		TArray<FPaper2DPlusOccupiedDirectionSlot> OccupiedSlots;
		DirectionalAnimation_SeedOccupiedOutputs(CanonicalBase, OccupiedSlots);
		Test.TestEqual(
			*FString::Printf(TEXT("%s: base-key enumeration rejects the ambiguous variant"), *CaseLabel),
			UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
				Asset,
				CanonicalBase,
				OccupiedSlots),
			EPaper2DPlusDirectionalAnimationResult::AmbiguousFlipbook);
		Test.TestTrue(
			*FString::Printf(TEXT("%s: ambiguous enumeration remains atomic"), *CaseLabel),
			OccupiedSlots.IsEmpty());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationLegacySchemaEightTest,
	"Paper2DPlus.DirectionalAnimation.Schema.LegacySchemaEightDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationLegacySchemaEightTest::RunTest(const FString& Parameters)
{
	const FString SchemaEightJson = TEXT(
		"{\"SchemaVersion\":8,\"DisplayName\":\"Legacy\","
		"\"Flipbooks\":[{\"Identity\":{\"FlipbookName\":\"Idle\"}}]}");

	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	if (!TestTrue(TEXT("Schema 8 payload imports through the additive migration"),
		Asset->ImportFromJsonString(SchemaEightJson)))
	{
		return false;
	}

	TestEqual(TEXT("Legacy animation remains present without repair"), Asset->Flipbooks.Num(), 1);
	TestEqual(TEXT("Legacy base identity is unchanged"),
		Asset->Flipbooks[0].Identity.FlipbookName, FString(TEXT("Idle")));
	TestFalse(TEXT("Schema 8 animation has no configured directional set"),
		Asset->HasDirectionalSet(0));
	TestFalse(TEXT("Schema 8 animation remains non-directional"),
		Asset->HasActiveDirectionalSlots(0));

	int32 EffectiveCount = INDEX_NONE;
	float EffectiveOffset = 999.0f;
	TestTrue(TEXT("Legacy animation resolves the new Profile defaults"),
		Asset->GetEffectiveDirectionalSettings(0, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("Legacy effective count defaults to eight"), EffectiveCount, 8);
	TestEqual(TEXT("Legacy effective offset defaults to zero"), EffectiveOffset, 0.0f);

	FString MigratedJson;
	TestTrue(TEXT("Migrated legacy Profile exports"), Asset->ExportToJsonString(MigratedJson));
	TestTrue(TEXT("Migrated legacy Profile is stamped schema 9"),
		MigratedJson.Contains(TEXT("\"SchemaVersion\":9"), ESearchCase::IgnoreCase));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationPresenceRoundTripTest,
	"Paper2DPlus.DirectionalAnimation.Schema.PresenceRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationPresenceRoundTripTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestTrue(TEXT("Sixteen-direction Profile defaults are accepted"),
		Source->SetDirectionalDefaults(16, 0.0f));

	DirectionalAnimation_AddMove(Source, TEXT("Absent"));
	DirectionalAnimation_AddMove(Source, TEXT("ConfiguredEmpty"));
	DirectionalAnimation_AddMove(Source, TEXT("Populated"));

	TestTrue(TEXT("Enable creates a configured-empty directional set"),
		Source->EnableDirectionalSet(1));
	TestTrue(TEXT("Configured-empty state has explicit presence"),
		Source->HasDirectionalSet(1));
	TestFalse(TEXT("Configured-empty state is not multidirectional"),
		Source->HasActiveDirectionalSlots(1));

	const TSoftObjectPtr<UPaperFlipbook> SlotFifteen(
		FSoftObjectPath(TEXT("/Game/Paper2DPlusTests/DA_Populated_15.DA_Populated_15")));
	TestTrue(TEXT("First assignment enables an absent directional set"),
		Source->SetDirectionalSlot(2, 15, SlotFifteen));

	FString Json;
	if (!TestTrue(TEXT("All three presence states export"), Source->ExportToJsonString(Json)))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Loaded = NewObject<UPaper2DPlusCharacterProfileAsset>();
	if (!TestTrue(TEXT("All three presence states import"), Loaded->ImportFromJsonString(Json)))
	{
		return false;
	}

	TestFalse(TEXT("Absent state remains absent after round trip"), Loaded->HasDirectionalSet(0));
	TestTrue(TEXT("Configured-empty state remains present after round trip"), Loaded->HasDirectionalSet(1));
	TestFalse(TEXT("Configured-empty state remains non-directional after round trip"),
		Loaded->HasActiveDirectionalSlots(1));
	TestTrue(TEXT("Populated state remains present after round trip"), Loaded->HasDirectionalSet(2));
	TestTrue(TEXT("Populated state remains multidirectional after round trip"),
		Loaded->HasActiveDirectionalSlots(2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationProfileDefaultsRoundTripTest,
	"Paper2DPlus.DirectionalAnimation.Schema.ProfileDefaultsRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationProfileDefaultsRoundTripTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestTrue(TEXT("Non-default Profile settings are authored"),
		Source->SetDirectionalDefaults(12, 22.5f));
	DirectionalAnimation_AddMove(Source, TEXT("Idle"));
	TestTrue(TEXT("Animation is explicitly configured-empty"),
		Source->EnableDirectionalSet(0));

	FString Json;
	if (!TestTrue(TEXT("Profile defaults export"), Source->ExportToJsonString(Json)))
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Loaded = NewObject<UPaper2DPlusCharacterProfileAsset>();
	if (!TestTrue(TEXT("Profile defaults import"), Loaded->ImportFromJsonString(Json)))
	{
		return false;
	}

	int32 EffectiveCount = INDEX_NONE;
	float EffectiveOffset = 0.0f;
	TestTrue(TEXT("Loaded configured-empty animation resolves inherited settings"),
		Loaded->GetEffectiveDirectionalSettings(0, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("Loaded configured-empty animation inherits imported Profile count"),
		EffectiveCount, 12);
	TestEqual(TEXT("Loaded configured-empty animation inherits imported nonzero Profile offset"),
		EffectiveOffset, 22.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationCanonicalBaseMutationGuardTest,
	"Paper2DPlus.DirectionalAnimation.Schema.CanonicalBaseRequiredAndInvalidSetRepairable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationCanonicalBaseMutationGuardTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry& Entry = Asset->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("PaperZDOnly");
	UPaperFlipbook* Variant = DirectionalAnimation_MakeFlipbook(Asset, TEXT("OrphanVariant"));

	TestFalse(TEXT("A name-only entry cannot enable a set without canonical base art"),
		Asset->EnableDirectionalSet(0));
	TestFalse(TEXT("Failed enable leaves explicit set presence absent"), Asset->HasDirectionalSet(0));
	TestFalse(TEXT("First assignment cannot create an ownerless directional set"),
		Asset->SetDirectionalSlot(0, 0, Variant));
	TestFalse(TEXT("Failed assignment also leaves explicit set presence absent"),
		Asset->HasDirectionalSet(0));

	// Imported legacy data can still contain this invalid shape. Repair operations must remain available.
	Entry.DirectionalAnimationData.bHasDirectionalSet = true;
	FPaper2DPlusDirectionalAnimationSlot& ImportedSlot =
		Entry.DirectionalAnimationData.Slots.AddDefaulted_GetRef();
	ImportedSlot.SlotIndex = 0;
	ImportedSlot.Flipbook = Variant;
	TestTrue(TEXT("Clear repairs the occupied part of an imported invalid set"),
		Asset->ClearDirectionalSlot(0, 0));
	TestTrue(TEXT("Remove repairs the remaining configured-empty invalid presence"),
		Asset->RemoveDirectionalSet(0));
	TestFalse(TEXT("Repair restores a legacy absent animation"), Asset->HasDirectionalSet(0));

	FPaper2DPlusDirectionalAnimationSlot& AbsentPresenceSlot =
		Entry.DirectionalAnimationData.Slots.AddDefaulted_GetRef();
	AbsentPresenceSlot.SlotIndex = 3;
	AbsentPresenceSlot.Flipbook = Variant;
	TestTrue(TEXT("An absent-presence slot record can still be cleared for repair"),
		Asset->ClearDirectionalSlot(0, 3));
	TestTrue(TEXT("The repair removes the malformed stored slot record"),
		Entry.DirectionalAnimationData.Slots.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationOverrideRoundTripTest,
	"Paper2DPlus.DirectionalAnimation.Schema.OverrideRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationOverrideRoundTripTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Source, TEXT("Idle"));
	TestTrue(TEXT("Directional set is enabled"), Source->EnableDirectionalSet(0));
	TestTrue(TEXT("Local settings override is authored"),
		Source->SetDirectionalOverride(0, true, 5, -30.0f));

	FString Json;
	if (!TestTrue(TEXT("Local settings override exports"), Source->ExportToJsonString(Json)))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Loaded = NewObject<UPaper2DPlusCharacterProfileAsset>();
	if (!TestTrue(TEXT("Local settings override imports"), Loaded->ImportFromJsonString(Json)))
	{
		return false;
	}

	int32 EffectiveCount = INDEX_NONE;
	float EffectiveOffset = 0.0f;
	TestTrue(TEXT("Loaded animation resolves its local settings override"),
		Loaded->GetEffectiveDirectionalSettings(0, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("Local direction count round-trips"), EffectiveCount, 5);
	TestEqual(TEXT("Local angle offset round-trips"), EffectiveOffset, -30.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationSparseSlotRoundTripTest,
	"Paper2DPlus.DirectionalAnimation.Schema.SparseSlotRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationSparseSlotRoundTripTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestTrue(TEXT("Sixteen-direction Profile defaults are accepted"),
		Source->SetDirectionalDefaults(16, 0.0f));
	DirectionalAnimation_AddMove(Source, TEXT("Idle"));
	const TSoftObjectPtr<UPaperFlipbook> SlotFifteen(
		FSoftObjectPath(TEXT("/Game/Paper2DPlusTests/DA_Sparse_15.DA_Sparse_15")));
	TestTrue(TEXT("Stable slot fifteen can be assigned sparsely"),
		Source->SetDirectionalSlot(0, 15, SlotFifteen));

	TestFalse(TEXT("An unoccupied slot refuses a mirror flag"),
		Source->SetDirectionalSlotMirror(0, 3, true));
	TestFalse(TEXT("Writing the mirror flag it already holds is a no-op"),
		Source->SetDirectionalSlotMirror(0, 15, false));
	TestTrue(TEXT("The occupied slot accepts a mirror flag"),
		Source->SetDirectionalSlotMirror(0, 15, true));
	const TSoftObjectPtr<UPaperFlipbook> ReplacementArt(
		FSoftObjectPath(TEXT("/Game/Paper2DPlusTests/DA_Sparse_15b.DA_Sparse_15b")));
	TestTrue(TEXT("Re-picking the slot's art succeeds"),
		Source->SetDirectionalSlot(0, 15, ReplacementArt));

	FString Json;
	if (!TestTrue(TEXT("Sparse assignment exports"), Source->ExportToJsonString(Json)))
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Loaded = NewObject<UPaper2DPlusCharacterProfileAsset>();
	if (!TestTrue(TEXT("Sparse assignment imports"), Loaded->ImportFromJsonString(Json)))
	{
		return false;
	}

	TSoftObjectPtr<UPaperFlipbook> LoadedSlot;
	bool bLoadedMirror = true;
	TestFalse(TEXT("Sparse slot zero remains unoccupied"),
		Loaded->GetDirectionalSlot(0, 0, LoadedSlot, bLoadedMirror));
	TestFalse(TEXT("An unoccupied slot query clears the mirror flag"), bLoadedMirror);
	TestTrue(TEXT("Sparse slot fifteen keeps its authored index"),
		Loaded->GetDirectionalSlot(0, 15, LoadedSlot, bLoadedMirror));
	TestEqual(TEXT("Sparse slot fifteen keeps its re-picked flipbook path"),
		LoadedSlot.ToSoftObjectPath().ToString(),
		ReplacementArt.ToSoftObjectPath().ToString());
	TestTrue(TEXT("The mirror flag survives an art re-pick and the JSON round-trip"),
		bLoadedMirror);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationSetSlotFailureAtomicityTest,
	"Paper2DPlus.DirectionalAnimation.Schema.SetSlotInvalidRequestsAreAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationSetSlotFailureAtomicityTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Asset, TEXT("SetGuardBase"));
	UPaperFlipbook* Variant = DirectionalAnimation_MakeFlipbook(Asset, TEXT("SetGuardVariant"));
	TestTrue(TEXT("The fixture authors one known-good slot"),
		Asset->SetDirectionalSlot(0, 2, Variant));
	auto AssertUnchanged = [this, Asset, Variant](const TCHAR* Context)
	{
		const FPaper2DPlusDirectionalAnimationData& Data =
			Asset->Flipbooks[0].DirectionalAnimationData;
		TestTrue(*FString::Printf(TEXT("%s preserves configured presence"), Context),
			Data.bHasDirectionalSet);
		TestEqual(*FString::Printf(TEXT("%s preserves exactly one sparse record"), Context),
			Data.Slots.Num(), 1);
		if (Data.Slots.Num() == 1)
		{
			TestEqual(*FString::Printf(TEXT("%s preserves slot two"), Context),
				Data.Slots[0].SlotIndex, 2);
			TestEqual(*FString::Printf(TEXT("%s preserves the assigned asset"), Context),
				Data.Slots[0].Flipbook.ToSoftObjectPath(),
				TSoftObjectPtr<UPaperFlipbook>(Variant).ToSoftObjectPath());
		}
	};

	TestFalse(TEXT("A negative animation index is rejected"),
		Asset->SetDirectionalSlot(-1, 2, Variant));
	AssertUnchanged(TEXT("Negative animation index"));
	TestFalse(TEXT("A negative slot is rejected"),
		Asset->SetDirectionalSlot(0, -1, Variant));
	AssertUnchanged(TEXT("Negative slot"));
	TestFalse(TEXT("The global slot upper bound is rejected"),
		Asset->SetDirectionalSlot(0, 16, Variant));
	AssertUnchanged(TEXT("Global upper bound"));
	TestFalse(TEXT("A globally valid slot outside the effective count is rejected"),
		Asset->SetDirectionalSlot(0, 8, Variant));
	AssertUnchanged(TEXT("Effective-count upper bound"));
	TestFalse(TEXT("A null directional asset is rejected"),
		Asset->SetDirectionalSlot(0, 2, TSoftObjectPtr<UPaperFlipbook>()));
	AssertUnchanged(TEXT("Null assignment"));
	TestFalse(TEXT("An identical assignment is an atomic no-op"),
		Asset->SetDirectionalSlot(0, 2, Variant));
	AssertUnchanged(TEXT("Identical assignment"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationClearSlotFailureAtomicityTest,
	"Paper2DPlus.DirectionalAnimation.Schema.ClearSlotInvalidRequestsAreAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationClearSlotFailureAtomicityTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Asset, TEXT("ClearGuardBase"));
	UPaperFlipbook* Variant = DirectionalAnimation_MakeFlipbook(Asset, TEXT("ClearGuardVariant"));
	TestTrue(TEXT("The fixture authors one known-good slot"),
		Asset->SetDirectionalSlot(0, 2, Variant));
	auto AssertUnchanged = [this, Asset, Variant](const TCHAR* Context)
	{
		const FPaper2DPlusDirectionalAnimationData& Data =
			Asset->Flipbooks[0].DirectionalAnimationData;
		TestEqual(*FString::Printf(TEXT("%s preserves exactly one sparse record"), Context),
			Data.Slots.Num(), 1);
		if (Data.Slots.Num() == 1)
		{
			TestEqual(*FString::Printf(TEXT("%s preserves slot two"), Context),
				Data.Slots[0].SlotIndex, 2);
			TestEqual(*FString::Printf(TEXT("%s preserves the assigned asset"), Context),
				Data.Slots[0].Flipbook.ToSoftObjectPath(),
				TSoftObjectPtr<UPaperFlipbook>(Variant).ToSoftObjectPath());
		}
	};

	TestFalse(TEXT("A negative animation index cannot clear storage"),
		Asset->ClearDirectionalSlot(-1, 2));
	AssertUnchanged(TEXT("Negative animation index"));
	TestFalse(TEXT("A negative slot cannot clear storage"),
		Asset->ClearDirectionalSlot(0, -1));
	AssertUnchanged(TEXT("Negative slot"));
	TestFalse(TEXT("The global slot upper bound cannot clear storage"),
		Asset->ClearDirectionalSlot(0, 16));
	AssertUnchanged(TEXT("Global upper bound"));
	TestFalse(TEXT("Clearing an absent valid slot is an atomic no-op"),
		Asset->ClearDirectionalSlot(0, 7));
	AssertUnchanged(TEXT("Absent slot"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationLocalCountStrandingTest,
	"Paper2DPlus.DirectionalAnimation.Schema.LocalCountStranding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationLocalCountStrandingTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestTrue(TEXT("Sixteen-direction Profile defaults are accepted"),
		Asset->SetDirectionalDefaults(16, 0.0f));
	DirectionalAnimation_AddMove(Asset, TEXT("Idle"));
	const TSoftObjectPtr<UPaperFlipbook> SlotFifteen(
		FSoftObjectPath(TEXT("/Game/Paper2DPlusTests/DA_LocalStranding_15.DA_LocalStranding_15")));
	TestTrue(TEXT("Stable slot fifteen is assigned"),
		Asset->SetDirectionalSlot(0, 15, SlotFifteen));
	TestTrue(TEXT("Sixteen-direction local override is authored"),
		Asset->SetDirectionalOverride(0, true, 16, 45.0f));

	TArray<int32> StrandedSlots;
	TestFalse(TEXT("Reducing the local count reports slot fifteen before mutation"),
		Asset->CanSetDirectionalOverride(0, true, 8, StrandedSlots));
	TestEqual(TEXT("Count-reduction preflight reports one slot"), StrandedSlots.Num(), 1);
	if (StrandedSlots.Num() == 1)
	{
		TestEqual(TEXT("Count-reduction preflight identifies slot fifteen"),
			StrandedSlots[0], 15);
	}
	TestFalse(TEXT("A local count reduction that would deactivate slot fifteen is rejected atomically"),
		Asset->SetDirectionalOverride(0, true, 8, 45.0f));
	int32 EffectiveCount = INDEX_NONE;
	float EffectiveOffset = 0.0f;
	TestTrue(TEXT("Rejected local count reduction leaves effective settings valid"),
		Asset->GetEffectiveDirectionalSettings(0, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("Rejected local count reduction preserves direction count"), EffectiveCount, 16);
	TestEqual(TEXT("Rejected local count reduction preserves angle offset"), EffectiveOffset, 45.0f);
	TestTrue(TEXT("Rejected count reduction leaves slot fifteen active"),
		Asset->HasActiveDirectionalSlots(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationProfileCountStrandingTest,
	"Paper2DPlus.DirectionalAnimation.Schema.ProfileCountStranding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationProfileCountStrandingTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	TestTrue(TEXT("Sixteen-direction Profile defaults are accepted"),
		Asset->SetDirectionalDefaults(16, 22.5f));
	DirectionalAnimation_AddMove(Asset, TEXT("Idle"));
	const TSoftObjectPtr<UPaperFlipbook> SlotFifteen(
		FSoftObjectPath(TEXT("/Game/Paper2DPlusTests/DA_ProfileStranding_15.DA_ProfileStranding_15")));
	TestTrue(TEXT("Inheriting stable slot fifteen is assigned"),
		Asset->SetDirectionalSlot(0, 15, SlotFifteen));

	TArray<int32> StrandedAnimations;
	TestFalse(TEXT("Reducing Profile defaults reports the inheriting animation"),
		Asset->CanSetDirectionalDefaults(8, StrandedAnimations));
	TestEqual(TEXT("Profile count-reduction preflight reports one animation"),
		StrandedAnimations.Num(), 1);
	if (StrandedAnimations.Num() == 1)
	{
		TestEqual(TEXT("Profile count-reduction preflight identifies the animation"),
			StrandedAnimations[0], 0);
	}
	TestFalse(TEXT("Profile count reduction that strands slot fifteen is rejected atomically"),
		Asset->SetDirectionalDefaults(8, -10.0f));
	int32 EffectiveCount = INDEX_NONE;
	float EffectiveOffset = 0.0f;
	TestTrue(TEXT("Rejected Profile mutation leaves inherited settings valid"),
		Asset->GetEffectiveDirectionalSettings(0, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("Rejected Profile mutation preserves direction count"), EffectiveCount, 16);
	TestEqual(TEXT("Rejected Profile mutation preserves angle offset"), EffectiveOffset, 22.5f);
	TestTrue(TEXT("Rejected Profile mutation leaves slot fifteen active"),
		Asset->HasActiveDirectionalSlots(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationOverrideDisableStrandingTest,
	"Paper2DPlus.DirectionalAnimation.Schema.OverrideDisableStranding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationOverrideDisableStrandingTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Asset, TEXT("Idle"));
	TestTrue(TEXT("Directional set is enabled"), Asset->EnableDirectionalSet(0));
	TestTrue(TEXT("Sixteen-direction local override is authored"),
		Asset->SetDirectionalOverride(0, true, 16, -30.0f));
	const TSoftObjectPtr<UPaperFlipbook> SlotFifteen(
		FSoftObjectPath(TEXT("/Game/Paper2DPlusTests/DA_DisableStranding_15.DA_DisableStranding_15")));
	TestTrue(TEXT("Local stable slot fifteen is assigned"),
		Asset->SetDirectionalSlot(0, 15, SlotFifteen));

	TArray<int32> StrandedSlots;
	TestFalse(TEXT("Disabling into inherited count eight reports slot fifteen"),
		Asset->CanSetDirectionalOverride(0, false, 16, StrandedSlots));
	TestEqual(TEXT("Override-disable preflight reports one slot"), StrandedSlots.Num(), 1);
	if (StrandedSlots.Num() == 1)
	{
		TestEqual(TEXT("Override-disable preflight identifies slot fifteen"),
			StrandedSlots[0], 15);
	}
	TestFalse(TEXT("Disabling an override that would strand slot fifteen is rejected atomically"),
		Asset->SetDirectionalOverride(0, false, 16, -30.0f));
	int32 EffectiveCount = INDEX_NONE;
	float EffectiveOffset = 0.0f;
	TestTrue(TEXT("Rejected override disable leaves local settings valid"),
		Asset->GetEffectiveDirectionalSettings(0, EffectiveCount, EffectiveOffset));
	TestEqual(TEXT("Rejected override disable preserves local direction count"), EffectiveCount, 16);
	TestEqual(TEXT("Rejected override disable preserves local angle offset"), EffectiveOffset, -30.0f);
	TestTrue(TEXT("Rejected override disable leaves slot fifteen active"),
		Asset->HasActiveDirectionalSlots(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationClearFinalSlotTest,
	"Paper2DPlus.DirectionalAnimation.Schema.ClearFinalSlotPreservesPresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationClearFinalSlotTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Asset, TEXT("Idle"));
	const TSoftObjectPtr<UPaperFlipbook> SlotZero(
		FSoftObjectPath(TEXT("/Game/Paper2DPlusTests/DA_Clear_0.DA_Clear_0")));
	TestTrue(TEXT("First slot assignment enables the set"),
		Asset->SetDirectionalSlot(0, 0, SlotZero));

	TestTrue(TEXT("Clearing the final slot succeeds"), Asset->ClearDirectionalSlot(0, 0));
	TestTrue(TEXT("Clearing the final slot preserves configured presence"),
		Asset->HasDirectionalSet(0));
	TestFalse(TEXT("Clearing the final slot leaves configured-empty state"),
		Asset->HasActiveDirectionalSlots(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationRemovalTest,
	"Paper2DPlus.DirectionalAnimation.Schema.RemovalRequiresEmptySet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationRemovalTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Asset, TEXT("Idle"));
	const TSoftObjectPtr<UPaperFlipbook> SlotZero(
		FSoftObjectPath(TEXT("/Game/Paper2DPlusTests/DA_Remove_0.DA_Remove_0")));
	TestTrue(TEXT("First slot assignment enables the set"),
		Asset->SetDirectionalSlot(0, 0, SlotZero));

	TestFalse(TEXT("A populated directional set cannot be removed"),
		Asset->RemoveDirectionalSet(0));
	TestTrue(TEXT("Rejected removal preserves populated presence"),
		Asset->HasActiveDirectionalSlots(0));
	TestTrue(TEXT("Slot is cleared to make the set removable"),
		Asset->ClearDirectionalSlot(0, 0));
	TestTrue(TEXT("An empty directional set can be removed"),
		Asset->RemoveDirectionalSet(0));
	TestFalse(TEXT("Removal returns the animation to absent state"),
		Asset->HasDirectionalSet(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationSettingsInheritanceTest,
	"Paper2DPlus.DirectionalAnimation.Schema.SettingsInheritance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationSettingsInheritanceTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Asset, TEXT("Idle"));

	auto TestEffectiveSettings = [this, Asset](const TCHAR* Context, int32 ExpectedCount, float ExpectedOffset)
	{
		int32 EffectiveCount = INDEX_NONE;
		float EffectiveOffset = 999.0f;
		TestTrue(*FString::Printf(TEXT("%s resolves effective settings"), Context),
			Asset->GetEffectiveDirectionalSettings(0, EffectiveCount, EffectiveOffset));
		TestEqual(*FString::Printf(TEXT("%s keeps direction count"), Context),
			EffectiveCount, ExpectedCount);
		TestEqual(*FString::Printf(TEXT("%s keeps angle offset"), Context),
			EffectiveOffset, ExpectedOffset);
	};

	TestEffectiveSettings(TEXT("New Profile"), 8, 0.0f);
	TestTrue(TEXT("Valid Profile defaults are accepted"),
		Asset->SetDirectionalDefaults(12, 22.5f));
	TestEffectiveSettings(TEXT("Inherited Profile defaults"), 12, 22.5f);

	TestTrue(TEXT("Enable creates the set before local override authoring"),
		Asset->EnableDirectionalSet(0));
	TestTrue(TEXT("A valid local override is accepted"),
		Asset->SetDirectionalOverride(0, true, 5, -30.0f));
	TestEffectiveSettings(TEXT("Local override"), 5, -30.0f);
	TestTrue(TEXT("Disabling the override restores inheritance without zero sentinels"),
		Asset->SetDirectionalOverride(0, false, 5, -30.0f));
	TestEffectiveSettings(TEXT("Restored inheritance"), 12, 22.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationProfileSettingsMutationTest,
	"Paper2DPlus.DirectionalAnimation.Schema.ProfileSettingsMutationGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationProfileSettingsMutationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Asset, TEXT("Idle"));

	auto TestEffectiveSettings = [this, Asset](const TCHAR* Context, int32 ExpectedCount, float ExpectedOffset)
	{
		int32 EffectiveCount = INDEX_NONE;
		float EffectiveOffset = 999.0f;
		TestTrue(*FString::Printf(TEXT("%s resolves effective settings"), Context),
			Asset->GetEffectiveDirectionalSettings(0, EffectiveCount, EffectiveOffset));
		TestEqual(*FString::Printf(TEXT("%s keeps direction count"), Context),
			EffectiveCount, ExpectedCount);
		TestEqual(*FString::Printf(TEXT("%s keeps angle offset"), Context),
			EffectiveOffset, ExpectedOffset);
	};

	TestTrue(TEXT("Minimum Profile count and offset boundaries are accepted"),
		Asset->SetDirectionalDefaults(3, -45.0f));
	TestFalse(TEXT("Profile count below three is rejected"),
		Asset->SetDirectionalDefaults(2, 10.0f));
	TestEffectiveSettings(TEXT("Rejected low Profile settings"), 3, -45.0f);
	TestTrue(TEXT("Maximum Profile count and offset boundaries are accepted"),
		Asset->SetDirectionalDefaults(16, 45.0f));
	TestFalse(TEXT("Profile count above sixteen is rejected"),
		Asset->SetDirectionalDefaults(17, 10.0f));
	TestEffectiveSettings(TEXT("Rejected high Profile count"), 16, 45.0f);
	TestFalse(TEXT("Profile offset below minus forty-five is rejected"),
		Asset->SetDirectionalDefaults(16, -46.0f));
	TestEffectiveSettings(TEXT("Rejected low Profile offset"), 16, 45.0f);
	TestFalse(TEXT("Profile offset above forty-five is rejected"),
		Asset->SetDirectionalDefaults(16, 46.0f));
	TestEffectiveSettings(TEXT("Rejected high Profile offset"), 16, 45.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationOverrideSettingsMutationTest,
	"Paper2DPlus.DirectionalAnimation.Schema.OverrideSettingsMutationGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationOverrideSettingsMutationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Asset, TEXT("Idle"));
	TestTrue(TEXT("Directional set is enabled"), Asset->EnableDirectionalSet(0));
	TestTrue(TEXT("Valid local override is authored"),
		Asset->SetDirectionalOverride(0, true, 5, -30.0f));

	auto TestEffectiveOverride = [this, Asset](const TCHAR* Context)
	{
		int32 EffectiveCount = INDEX_NONE;
		float EffectiveOffset = 999.0f;
		TestTrue(*FString::Printf(TEXT("%s resolves effective settings"), Context),
			Asset->GetEffectiveDirectionalSettings(0, EffectiveCount, EffectiveOffset));
		TestEqual(*FString::Printf(TEXT("%s preserves local direction count"), Context),
			EffectiveCount, 5);
		TestEqual(*FString::Printf(TEXT("%s preserves local angle offset"), Context),
			EffectiveOffset, -30.0f);
	};

	TestFalse(TEXT("Local override count below three is rejected"),
		Asset->SetDirectionalOverride(0, true, 2, 0.0f));
	TestEffectiveOverride(TEXT("Rejected low local count"));
	TestFalse(TEXT("Local override count above sixteen is rejected"),
		Asset->SetDirectionalOverride(0, true, 17, 0.0f));
	TestEffectiveOverride(TEXT("Rejected high local count"));
	TestFalse(TEXT("Local override offset below minus forty-five is rejected"),
		Asset->SetDirectionalOverride(0, true, 5, -46.0f));
	TestEffectiveOverride(TEXT("Rejected low local offset"));
	TestFalse(TEXT("Local override offset above forty-five is rejected"),
		Asset->SetDirectionalOverride(0, true, 5, 46.0f));
	TestEffectiveOverride(TEXT("Rejected high local offset"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationSameOwnerReuseTest,
	"Paper2DPlus.DirectionalAnimation.Ownership.SameOwnerReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationSameOwnerReuseTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = DirectionalAnimation_AddMove(Asset, TEXT("Shoot"));
	UPaperFlipbook* Variant = DirectionalAnimation_MakeFlipbook(Asset, TEXT("ShootVariant"));
	TestTrue(TEXT("A variant can occupy the first slot"),
		Asset->SetDirectionalSlot(0, 0, Variant));
	TestTrue(TEXT("The same variant can be reused by another slot under one owner"),
		Asset->SetDirectionalSlot(0, 1, Variant));
	TestTrue(TEXT("The base can also occupy a slot under its own owner"),
		Asset->SetDirectionalSlot(0, 2, Base));

	bool bAmbiguous = true;
	const FFlipbookProfileEntry* VariantOwner =
		Asset->ResolveLogicalAnimationOwner(Variant, bAmbiguous);
	TestNotNull(TEXT("Repeated same-owner variant references resolve"), VariantOwner);
	TestFalse(TEXT("Repeated same-owner variant references are not ambiguous"), bAmbiguous);
	TestTrue(TEXT("A variant resolves to its canonical base entry"),
		VariantOwner && VariantOwner->Identity.Flipbook.Get() == Base);

	bAmbiguous = true;
	const FFlipbookProfileEntry* BaseOwner = Asset->ResolveLogicalAnimationOwner(Base, bAmbiguous);
	TestNotNull(TEXT("Same-owner base-plus-slot reuse resolves"), BaseOwner);
	TestFalse(TEXT("Same-owner base-plus-slot reuse is not ambiguous"), bAmbiguous);
	TestTrue(TEXT("The base input keeps canonical base identity"),
		BaseOwner && BaseOwner->Identity.Flipbook.Get() == Base);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationBaseVariantCollisionTest,
	"Paper2DPlus.DirectionalAnimation.Ownership.CrossOwnerBaseVariantFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationBaseVariantCollisionTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Shared = DirectionalAnimation_AddMove(Asset, TEXT("Idle"));
	DirectionalAnimation_AddMove(Asset, TEXT("Run"));
	TestTrue(TEXT("A different owner can author the conflicting reference for validation"),
		Asset->SetDirectionalSlot(1, 0, Shared));

	bool bAmbiguous = false;
	TestNull(TEXT("A cross-owner base-plus-variant collision has no logical owner"),
		Asset->ResolveLogicalAnimationOwner(Shared, bAmbiguous));
	TestTrue(TEXT("A cross-owner base-plus-variant collision is explicitly ambiguous"), bAmbiguous);
	TestNull(TEXT("The compatibility accessor also fails closed"), Asset->FindByFlipbookPtr(Shared));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationVariantCollisionTest,
	"Paper2DPlus.DirectionalAnimation.Ownership.CrossOwnerVariantsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationVariantCollisionTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	DirectionalAnimation_AddMove(Asset, TEXT("Idle"));
	DirectionalAnimation_AddMove(Asset, TEXT("Run"));
	UPaperFlipbook* SharedVariant =
		DirectionalAnimation_MakeFlipbook(Asset, TEXT("SharedVariant"));
	TestTrue(TEXT("The first owner accepts the variant"),
		Asset->SetDirectionalSlot(0, 0, SharedVariant));
	TestTrue(TEXT("The second owner can author the collision for validation"),
		Asset->SetDirectionalSlot(1, 0, SharedVariant));

	bool bAmbiguous = false;
	TestNull(TEXT("A cross-owner variant collision has no logical owner"),
		Asset->ResolveLogicalAnimationOwner(SharedVariant, bAmbiguous));
	TestTrue(TEXT("A cross-owner variant collision is explicitly ambiguous"), bAmbiguous);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationLegacyDuplicateBaseTest,
	"Paper2DPlus.DirectionalAnimation.Ownership.SchemaEightDuplicateBaseCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationLegacyDuplicateBaseTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Shared = DirectionalAnimation_MakeFlipbook(Source, TEXT("LegacyShared"));
	for (const TCHAR* MoveName : { TEXT("LegacyFirst"), TEXT("LegacySecond") })
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = MoveName;
		Entry.Identity.Flipbook = Shared;
		Source->Flipbooks.Add(MoveTemp(Entry));
	}

	FString SchemaEightJson;
	if (!TestTrue(TEXT("Duplicate-base fixture exports"), Source->ExportToJsonString(SchemaEightJson)))
	{
		return false;
	}
	TestEqual(TEXT("Fixture is restamped as schema 8"),
		SchemaEightJson.ReplaceInline(TEXT("\"SchemaVersion\":9"), TEXT("\"SchemaVersion\":8")),
		1);

	UPaper2DPlusCharacterProfileAsset* Loaded = NewObject<UPaper2DPlusCharacterProfileAsset>();
	if (!TestTrue(TEXT("Schema 8 duplicate-base fixture imports"),
		Loaded->ImportFromJsonString(SchemaEightJson)))
	{
		return false;
	}
	TestEqual(TEXT("Both legacy duplicate rows remain"), Loaded->Flipbooks.Num(), 2);
	TestFalse(TEXT("The first legacy candidate remains base-only"),
		Loaded->HasActiveDirectionalSlots(0));
	TestFalse(TEXT("The second legacy candidate remains base-only"),
		Loaded->HasActiveDirectionalSlots(1));

	bool bAmbiguous = true;
	const FFlipbookProfileEntry* LegacyWinner =
		Loaded->ResolveLogicalAnimationOwner(Shared, bAmbiguous);
	TestFalse(TEXT("Untouched base-only duplicates retain compatibility"), bAmbiguous);
	TestTrue(TEXT("The historical last-iteration candidate remains the winner"),
		LegacyWinner == &Loaded->Flipbooks[1]);

	UPaperFlipbook* OtherVariant =
		DirectionalAnimation_MakeFlipbook(Loaded, TEXT("LegacyFirstVariant"));
	TestTrue(TEXT("A same-sized directional edit populates one duplicate owner"),
		Loaded->SetDirectionalSlot(0, 0, OtherVariant));
	bAmbiguous = false;
	TestNull(TEXT("Directional involvement removes the duplicate-base compatibility exception"),
		Loaded->ResolveLogicalAnimationOwner(Shared, bAmbiguous));
	TestTrue(TEXT("The warmed duplicate key becomes ambiguous after the same-sized edit"), bAmbiguous);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationPaperZDBoundaryMathTest,
	"Paper2DPlus.DirectionalAnimation.Resolution.PaperZDBoundaryMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationPaperZDBoundaryMathTest::RunTest(const FString& Parameters)
{
	int32 SlotIndex = INDEX_NONE;
	TestTrue(TEXT("+Y is the zero-degree direction"),
		UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
			FVector2D(0.0, 1.0), 8, 0.0f, SlotIndex));
	TestEqual(TEXT("+Y resolves to slot zero"), SlotIndex, 0);

	// Four sectors make both half-sector boundaries exactly +/-45 degrees, represented by (+/-1,+1),
	// so this pins PaperZD's add-half-before-cast behavior without a trigonometric fixture tolerance.
	TestTrue(TEXT("Clockwise half-sector boundary resolves"),
		UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
			FVector2D(1.0, 1.0), 4, 0.0f, SlotIndex));
	TestEqual(TEXT("Clockwise half-sector boundary rounds into the next slot"), SlotIndex, 1);
	TestTrue(TEXT("Counterclockwise half-sector boundary resolves"),
		UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
			FVector2D(-1.0, 1.0), 4, 0.0f, SlotIndex));
	TestEqual(TEXT("Counterclockwise half-sector boundary keeps PaperZD cast-and-wrap behavior"),
		SlotIndex, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationTopologyMathTest,
	"Paper2DPlus.DirectionalAnimation.Resolution.AuthoredTopologyOffsetAndWrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationTopologyMathTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Label;
		FVector2D Direction;
		int32 DirectionCount;
		float OffsetDegrees;
		int32 ExpectedSlot;
	};
	const FCase Cases[] =
	{
		{ TEXT("minimum count"), FVector2D(0.0, -1.0), 3, 0.0f, 2 },
		{ TEXT("odd count"), FVector2D(1.0, 0.0), 5, 0.0f, 1 },
		{ TEXT("maximum count"), FVector2D(1.0, 0.0), 16, 0.0f, 4 },
		{ TEXT("negative-angle wrap"), FVector2D(-1.0, 0.0), 8, 0.0f, 6 },
		{ TEXT("positive offset before rounding"), FVector2D(0.0, 1.0), 8, 45.0f, 1 },
		{ TEXT("negative offset before wrapping"), FVector2D(0.0, 1.0), 8, -45.0f, 7 }
	};

	for (const FCase& Case : Cases)
	{
		int32 SlotIndex = INDEX_NONE;
		TestTrue(*FString::Printf(TEXT("%s resolves"), Case.Label),
			UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
				Case.Direction, Case.DirectionCount, Case.OffsetDegrees, SlotIndex));
		TestEqual(*FString::Printf(TEXT("%s uses the authored topology"), Case.Label),
			SlotIndex, Case.ExpectedSlot);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationVectorInputTest,
	"Paper2DPlus.DirectionalAnimation.Resolution.VectorInputContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationVectorInputTest::RunTest(const FString& Parameters)
{
	int32 SlotIndex = 9;
	TestFalse(TEXT("A zero vector is rejected"),
		UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
			FVector2D::ZeroVector, 8, 0.0f, SlotIndex));
	TestEqual(TEXT("A rejected zero vector clears the slot output"), SlotIndex, INDEX_NONE);

	const double QuietNaN = std::numeric_limits<double>::quiet_NaN();
	SlotIndex = 9;
	TestFalse(TEXT("A non-finite vector is rejected"),
		UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
			FVector2D(QuietNaN, 1.0), 8, 0.0f, SlotIndex));
	TestEqual(TEXT("A rejected non-finite vector clears the slot output"), SlotIndex, INDEX_NONE);
	SlotIndex = 9;
	TestFalse(TEXT("An infinite vector is rejected"),
		UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
			FVector2D(std::numeric_limits<double>::infinity(), 1.0), 8, 0.0f, SlotIndex));
	TestEqual(TEXT("A rejected infinite vector clears the slot output"), SlotIndex, INDEX_NONE);

	SlotIndex = INDEX_NONE;
	TestTrue(TEXT("Every finite nonzero magnitude is accepted"),
		UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
			FVector2D(1.0e-20, 0.0), 8, 0.0f, SlotIndex));
	TestEqual(TEXT("A tiny +X vector keeps its direction"), SlotIndex, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationStructuralFirstFaultTest,
	"Paper2DPlus.DirectionalAnimation.Validation.StructuralFirstFaultOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationStructuralFirstFaultTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry& Entry = Asset->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("Shoot");
	FPaper2DPlusDirectionalAnimationData& Data = Entry.DirectionalAnimationData;
	Data.bOverrideProfileSettings = true;
	Data.DirectionCount = 2;
	Data.AngleOffsetDegrees = std::numeric_limits<float>::infinity();
	FPaper2DPlusDirectionalAnimationSlot& Invalid = Data.Slots.AddDefaulted_GetRef();
	Invalid.SlotIndex = -1;
	FPaper2DPlusDirectionalAnimationSlot& DuplicateA = Data.Slots.AddDefaulted_GetRef();
	DuplicateA.SlotIndex = 7;
	FPaper2DPlusDirectionalAnimationSlot& DuplicateB = Data.Slots.AddDefaulted_GetRef();
	DuplicateB.SlotIndex = 7;
	FPaper2DPlusDirectionalAnimationSlot& Inactive = Data.Slots.AddDefaulted_GetRef();
	Inactive.SlotIndex = 9;
	Inactive.Flipbook = TSoftObjectPtr<UPaperFlipbook>(
		FSoftObjectPath(TEXT("/Game/__Paper2DPlusAutomationMissing__/DA_Inactive.DA_Inactive")));

	FPaper2DPlusDirectionalStructureResult Result;
	Asset->DefaultDirectionalCount = 2;
	Asset->DefaultDirectionalAngleOffset = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("The first malformed state is rejected"),
		Asset->CheckDirectionalAnimationStructure(0, Result));
	TestEqual(TEXT("Profile count is the first fault"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::InvalidProfileDirectionCount);

	Asset->DefaultDirectionalCount = 8;
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("Profile offset is next"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::InvalidProfileAngleOffset);
	Asset->DefaultDirectionalAngleOffset = 46.0f;
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("A finite Profile offset outside the declared range is also rejected"),
		Result.Fault,
		EPaper2DPlusDirectionalStructureFault::InvalidProfileAngleOffset);

	Asset->DefaultDirectionalAngleOffset = 0.0f;
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("Presence/override consistency precedes dormant local settings"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::InconsistentSetPresence);

	Data.bHasDirectionalSet = true;
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("Active local count is checked before slot storage"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::InvalidOverrideDirectionCount);

	Data.DirectionCount = 8;
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("Active local offset is checked before slot storage"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::InvalidOverrideAngleOffset);
	Data.AngleOffsetDegrees = -46.0f;
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("A finite local offset outside the declared range is also rejected"),
		Result.Fault,
		EPaper2DPlusDirectionalStructureFault::InvalidOverrideAngleOffset);

	Data.AngleOffsetDegrees = 0.0f;
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("Out-of-range slot keys precede duplicates"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::InvalidSlotIndex);

	Data.Slots.RemoveAt(0);
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("Duplicate slot keys precede inactive occupancy"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::DuplicateSlotIndex);

	Data.Slots.RemoveAt(1);
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("Inactive occupancy precedes base identity"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::OccupiedInactiveSlot);

	Data.Slots.RemoveAt(1);
	Asset->CheckDirectionalAnimationStructure(0, Result);
	TestEqual(TEXT("Missing canonical base is the final structural fault"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::MissingCanonicalBase);
	TArray<FCharacterProfileValidationIssue> StructuralIssues;
	TestFalse(TEXT("Native Profile validation consumes the shared structural fault"),
		Asset->ValidateCharacterProfileAsset(StructuralIssues));
	TestNotNull(TEXT("The projected structural diagnostic remains attributable to Shoot"),
		DirectionalAnimation_FindIssue(
			StructuralIssues,
			TEXT("Flipbook[0] 'Shoot' Directional Set"),
			TEXT("Identity.Flipbook")));

	Entry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(
		FSoftObjectPath(TEXT("/Game/__Paper2DPlusAutomationMissing__/DA_Base.DA_Base")));
	Data.Slots[0].Flipbook = TSoftObjectPtr<UPaperFlipbook>(
		FSoftObjectPath(TEXT("/Game/__Paper2DPlusAutomationMissing__/DA_Variant.DA_Variant")));
	TestNull(TEXT("The unresolved base starts cold"), Entry.Identity.Flipbook.Get());
	TestNull(TEXT("The unresolved variant starts cold"), Data.Slots[0].Flipbook.Get());
	TestTrue(TEXT("A structurally valid sparse set passes without loading either asset"),
		Asset->CheckDirectionalAnimationStructure(0, Result));
	TestNull(TEXT("The structural gate leaves the base cold"), Entry.Identity.Flipbook.Get());
	TestNull(TEXT("The structural gate leaves the variant cold"), Data.Slots[0].Flipbook.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationEmptyProfileCountValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.EmptyProfileRejectsInvalidDefaultCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationEmptyProfileCountValidationTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	Asset->DefaultDirectionalCount = 2;
	TArray<FCharacterProfileValidationIssue> Issues;
	TestFalse(TEXT("An empty Profile still validates its global direction count"),
		Asset->ValidateCharacterProfileAsset(Issues));
	TestNotNull(TEXT("The global diagnostic names DefaultDirectionalCount"),
		DirectionalAnimation_FindIssue(
			Issues, TEXT("Directional Defaults"), TEXT("DefaultDirectionalCount")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationEmptyProfileOffsetValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.EmptyProfileRejectsInvalidDefaultOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationEmptyProfileOffsetValidationTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	Asset->DefaultDirectionalAngleOffset =
		std::numeric_limits<float>::quiet_NaN();
	TArray<FCharacterProfileValidationIssue> Issues;
	TestFalse(TEXT("An empty Profile still validates its global angle offset"),
		Asset->ValidateCharacterProfileAsset(Issues));
	TestNotNull(TEXT("The global diagnostic names DefaultDirectionalAngleOffset"),
		DirectionalAnimation_FindIssue(
			Issues, TEXT("Directional Defaults"), TEXT("DefaultDirectionalAngleOffset")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationAbsentSlotsStructuralTest,
	"Paper2DPlus.DirectionalAnimation.Validation.AbsentSetCannotRetainSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationAbsentSlotsStructuralTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry& Entry = Asset->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("Shoot");
	Entry.Identity.Flipbook = DirectionalAnimation_MakeFlipbook(Asset, TEXT("ShootBase"));
	Entry.DirectionalAnimationData.Slots.AddDefaulted_GetRef().SlotIndex = 0;

	FPaper2DPlusDirectionalStructureResult Result;
	TestFalse(TEXT("Slot storage without explicit set presence is rejected"),
		Asset->CheckDirectionalAnimationStructure(0, Result));
	TestEqual(TEXT("The fault is attributable to set presence"), Result.Fault,
		EPaper2DPlusDirectionalStructureFault::InconsistentSetPresence);
	TestEqual(TEXT("The fault names the stored slots field"), Result.Field,
		FString(TEXT("DirectionalAnimationData.Slots")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationCompatibleSparseValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.CompatibleSparseNonuniform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationCompatibleSparseValidationTest::RunTest(const FString& Parameters)
{
	FDirectionalAnimationValidationFixture Fixture =
		DirectionalAnimation_MakeValidationFixture();
	TestTrue(TEXT("The same compatible art may be reused in another sparse active slot"),
		Fixture.Asset->SetDirectionalSlot(0, 6, Fixture.Variant));

	TArray<FCharacterProfileValidationIssue> Issues;
	TestTrue(TEXT("A partial set with nonuniform 1,3,2,4,1 holds validates"),
		Fixture.Asset->ValidateCharacterProfileAsset(Issues));
	TestNull(TEXT("No directional validation error is emitted"),
		DirectionalAnimation_FindIssue(Issues, TEXT("Directional"), TEXT("differs")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationPlaybackRateValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Timeline.PlaybackRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationPlaybackRateValidationTest::RunTest(const FString& Parameters)
{
	FDirectionalAnimationValidationFixture Fixture = DirectionalAnimation_MakeValidationFixture();
	{
		FScopedFlipbookMutator Mutator(Fixture.Variant);
		Mutator.FramesPerSecond = 13.0f;
	}
	TArray<FCharacterProfileValidationIssue> Issues;
	TestFalse(TEXT("A playback-rate mismatch rejects the Profile"),
		Fixture.Asset->ValidateCharacterProfileAsset(Issues));
	TestNotNull(TEXT("The slot diagnostic names FramesPerSecond"),
		DirectionalAnimation_FindIssue(
			Issues,
			TEXT("Flipbook[0] 'Shoot' Directional Slot[3]"),
			TEXT("FramesPerSecond")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationKeyCountValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Timeline.KeyFrameCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationKeyCountValidationTest::RunTest(const FString& Parameters)
{
	FDirectionalAnimationValidationFixture Fixture = DirectionalAnimation_MakeValidationFixture();
	{
		FScopedFlipbookMutator Mutator(Fixture.Variant);
		Mutator.KeyFrames.RemoveAt(Mutator.KeyFrames.Num() - 1);
	}
	TArray<FCharacterProfileValidationIssue> Issues;
	TestFalse(TEXT("A key-frame-count mismatch rejects the Profile"),
		Fixture.Asset->ValidateCharacterProfileAsset(Issues));
	TestNotNull(TEXT("The slot diagnostic names KeyFrameCount"),
		DirectionalAnimation_FindIssue(
			Issues,
			TEXT("Flipbook[0] 'Shoot' Directional Slot[3]"),
			TEXT("KeyFrameCount")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationFrameRunValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Timeline.FrameRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationFrameRunValidationTest::RunTest(const FString& Parameters)
{
	FDirectionalAnimationValidationFixture Fixture = DirectionalAnimation_MakeValidationFixture();
	const TSoftObjectPtr<UPaperFlipbook> AuthoredVariant =
		Fixture.Asset->Flipbooks[0].DirectionalAnimationData.Slots[0].Flipbook;
	{
		FScopedFlipbookMutator Mutator(Fixture.Variant);
		// Preserve total duration and key count while moving one held frame from key 1 to key 2.
		Mutator.KeyFrames[1].FrameRun = 2;
		Mutator.KeyFrames[2].FrameRun = 3;
	}

	TArray<FCharacterProfileValidationIssue> Issues;
	TestFalse(TEXT("An equal-duration FrameRun mismatch rejects the Profile"),
		Fixture.Asset->ValidateCharacterProfileAsset(Issues));
	TestNotNull(TEXT("The diagnostic names slot 3, key frame 1, and FrameRun"),
		DirectionalAnimation_FindIssue(
			Issues,
			TEXT("Flipbook[0] 'Shoot' Directional Slot[3] KeyFrame[1]"),
			TEXT("FrameRun")));
	TestTrue(TEXT("Validation preserves the incompatible directional assignment"),
		Fixture.Asset->Flipbooks[0].DirectionalAnimationData.Slots[0].Flipbook
			.ToSoftObjectPath() == AuthoredVariant.ToSoftObjectPath());
	TestEqual(TEXT("Validation does not normalize the incompatible hold"),
		Fixture.Variant->GetKeyFrameChecked(1).FrameRun, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationNullSpriteValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Timeline.EitherSideNullSprite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationNullSpriteValidationTest::RunTest(const FString& Parameters)
{
	for (const bool bNullBase : { true, false })
	{
		FDirectionalAnimationValidationFixture Fixture =
			DirectionalAnimation_MakeValidationFixture();
		UPaperFlipbook* Target = bNullBase ? Fixture.Base : Fixture.Variant;
		{
			FScopedFlipbookMutator Mutator(Target);
			Mutator.KeyFrames[4].Sprite = nullptr;
		}

		TArray<FCharacterProfileValidationIssue> Issues;
		TestFalse(
			bNullBase ? TEXT("A null base sprite rejects the Profile")
				: TEXT("A null variant sprite rejects the Profile"),
			Fixture.Asset->ValidateCharacterProfileAsset(Issues));
		TestNotNull(
			bNullBase ? TEXT("The diagnostic attributes the null base sprite")
				: TEXT("The diagnostic attributes the null variant sprite"),
			DirectionalAnimation_FindIssue(
				Issues,
				TEXT("Flipbook[0] 'Shoot' Directional Slot[3] KeyFrame[4]"),
				bNullBase ? TEXT("base null") : TEXT("variant null")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationCanvasGeometryValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Geometry.UntrimmedCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationCanvasGeometryValidationTest::RunTest(const FString& Parameters)
{
	return DirectionalAnimation_RunGeometryMismatch(
		*this,
		TEXT("UntrimmedCanvas"),
		[](FDirectionalAnimationValidationFixture& Fixture)
		{
			UPaperSprite* Sprite = Fixture.VariantSprites[2];
			Sprite->SetTrim(
				true,
				Sprite->GetOriginInSourceImageBeforeTrimming(),
				FVector2D(65.0, 80.0),
				true);
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationTrimOriginGeometryValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Silhouette.TrimOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationTrimOriginGeometryValidationTest::RunTest(const FString& Parameters)
{
	return DirectionalAnimation_RunSilhouetteDelta(
		*this,
		TEXT("TrimOrigin"),
		[](FDirectionalAnimationValidationFixture& Fixture)
		{
			// A real independent trim moves the trim origin and the pivot's region position
			// together, keeping the pivot fixed in the untrimmed canvas — mirror that here so
			// the blocking frame-space tier stays clean and only the silhouette differs.
			UPaperSprite* Sprite = Fixture.VariantSprites[2];
			const FVector2D Pivot = Sprite->GetPivotPosition();
			Sprite->SetTrim(
				true,
				Sprite->GetOriginInSourceImageBeforeTrimming() + FVector2D(1.0, 0.0),
				Sprite->GetSourceImageDimensionBeforeTrimming(),
				true);
			Sprite->SetPivotMode(
				ESpritePivotMode::Custom,
				Pivot - FVector2D(1.0, 0.0),
				true);
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationTrimSizeGeometryValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Silhouette.TrimSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationTrimSizeGeometryValidationTest::RunTest(const FString& Parameters)
{
	return DirectionalAnimation_RunSilhouetteDelta(
		*this,
		TEXT("TrimSize"),
		[](FDirectionalAnimationValidationFixture& Fixture)
		{
			// Re-initializing to a wider source region would drift the default centered pivot,
			// so pin the authored pivot back in place: only the trimmed rectangle may differ.
			UPaperSprite* Sprite = Fixture.VariantSprites[2];
			const FVector2D Pivot = Sprite->GetPivotPosition();
			FSpriteAssetInitParameters Init;
			Init.Texture = Sprite->GetSourceTexture();
			Init.Offset = FIntPoint(8, 12);
			Init.Dimension = FIntPoint(33, 48);
			Init.SetPixelsPerUnrealUnit(1.0f);
			Sprite->InitializeSprite(Init, true);
			Sprite->SetTrim(true, FVector2D(5.0, 7.0), FVector2D(64.0, 80.0), true);
			Sprite->SetPivotMode(ESpritePivotMode::Custom, Pivot, true);
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationPivotGeometryValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Geometry.Pivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationPivotGeometryValidationTest::RunTest(const FString& Parameters)
{
	return DirectionalAnimation_RunGeometryMismatch(
		*this,
		TEXT("PivotInUntrimmedSource"),
		[](FDirectionalAnimationValidationFixture& Fixture)
		{
			UPaperSprite* Sprite = Fixture.VariantSprites[2];
			Sprite->SetPivotMode(
				ESpritePivotMode::Custom,
				Sprite->GetPivotPosition() + FVector2D(1.0, 0.0),
				true);
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationPixelsPerUnitGeometryValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Geometry.PixelsPerUnit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationPixelsPerUnitGeometryValidationTest::RunTest(const FString& Parameters)
{
	return DirectionalAnimation_RunGeometryMismatch(
		*this,
		TEXT("PixelsPerUnrealUnit"),
		[](FDirectionalAnimationValidationFixture& Fixture)
		{
			UPaperSprite* Sprite = Fixture.VariantSprites[2];
			const FVector2D Pivot = Sprite->GetPivotPosition();
			FSpriteAssetInitParameters Init;
			Init.Texture = Sprite->GetSourceTexture();
			Init.Offset = FIntPoint(8, 12);
			Init.Dimension = FIntPoint(32, 48);
			Init.SetPixelsPerUnrealUnit(2.0f);
			Sprite->InitializeSprite(Init, true);
			Sprite->SetTrim(true, FVector2D(5.0, 7.0), FVector2D(64.0, 80.0), true);
			Sprite->SetPivotMode(ESpritePivotMode::Custom, Pivot, true);
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationRotatedGeometryValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Geometry.RotatedFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationRotatedGeometryValidationTest::RunTest(const FString& Parameters)
{
	return DirectionalAnimation_RunGeometryMismatch(
		*this,
		TEXT("RotatedInSourceImage"),
		[](FDirectionalAnimationValidationFixture& Fixture)
		{
			Fixture.VariantSprites[2]->SetRotated(true, true);
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationRenderSizeGeometryValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Silhouette.RenderSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationRenderSizeGeometryValidationTest::RunTest(const FString& Parameters)
{
	return DirectionalAnimation_RunSilhouetteDelta(
		*this,
		TEXT("RenderSize"),
		[](FDirectionalAnimationValidationFixture& Fixture)
		{
			UPaperSprite* Sprite = Fixture.VariantSprites[2];
			int32 MaxXVertex = INDEX_NONE;
			float MaxX = -std::numeric_limits<float>::infinity();
			for (int32 VertexIndex = 0; VertexIndex < Sprite->BakedRenderData.Num(); ++VertexIndex)
			{
				if (Sprite->BakedRenderData[VertexIndex].X > MaxX)
				{
					MaxX = Sprite->BakedRenderData[VertexIndex].X;
					MaxXVertex = VertexIndex;
				}
			}
			if (MaxXVertex != INDEX_NONE)
			{
				Sprite->BakedRenderData[MaxXVertex].X += 2.0f;
			}
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationRenderOriginGeometryValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.Silhouette.RenderBoundsOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationRenderOriginGeometryValidationTest::RunTest(const FString& Parameters)
{
	return DirectionalAnimation_RunSilhouetteDelta(
		*this,
		TEXT("RenderBoundsOrigin"),
		[](FDirectionalAnimationValidationFixture& Fixture)
		{
			UPaperSprite* Sprite = Fixture.VariantSprites[2];
			for (FVector4& Vertex : Sprite->BakedRenderData)
			{
				Vertex.X += 1.0f;
				Vertex.Y -= 2.0f;
			}
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationUnloadableReferenceValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.UnloadableBaseAndVariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationUnloadableReferenceValidationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry& Entry = Asset->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("Shoot");
	Entry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(
		FSoftObjectPath(TEXT("/Game/__Paper2DPlusAutomationMissing__/DA_MissingBase.DA_MissingBase")));
	Entry.DirectionalAnimationData.bHasDirectionalSet = true;
	FPaper2DPlusDirectionalAnimationSlot& Slot =
		Entry.DirectionalAnimationData.Slots.AddDefaulted_GetRef();
	Slot.SlotIndex = 2;
	Slot.Flipbook = TSoftObjectPtr<UPaperFlipbook>(
		FSoftObjectPath(TEXT("/Game/__Paper2DPlusAutomationMissing__/DA_MissingVariant.DA_MissingVariant")));

	TArray<FCharacterProfileValidationIssue> Issues;
	TestFalse(TEXT("Unresolved base and variant storage rejects the Profile"),
		Asset->ValidateCharacterProfileAsset(Issues));
	TestNotNull(TEXT("The base row reports its unloadable reference"),
		DirectionalAnimation_FindIssue(
			Issues,
			TEXT("Flipbook[0] 'Shoot'"),
			TEXT("Flipbook reference could not be loaded")));
	TestNotNull(TEXT("The directional slot reports its unloadable reference"),
		DirectionalAnimation_FindIssue(
			Issues,
			TEXT("Flipbook[0] 'Shoot' Directional Slot[2]"),
			TEXT("DA_MissingVariant")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationOwnershipValidationTest,
	"Paper2DPlus.DirectionalAnimation.Validation.CrossOwnerAmbiguities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationOwnershipValidationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Idle = DirectionalAnimation_AddMove(Asset, TEXT("Idle"));
	UPaperFlipbook* Run = DirectionalAnimation_AddMove(Asset, TEXT("Run"));
	UPaperFlipbook* SharedVariant = DirectionalAnimation_MakeFlipbook(Asset, TEXT("SharedVariant"));
	TestTrue(TEXT("Idle accepts the shared variant"),
		Asset->SetDirectionalSlot(0, 1, SharedVariant));
	TestTrue(TEXT("Run retains a cross-owner variant collision for validation"),
		Asset->SetDirectionalSlot(1, 5, SharedVariant));
	TestTrue(TEXT("Run retains a base-plus-variant collision for validation"),
		Asset->SetDirectionalSlot(1, 6, Idle));

	TArray<FCharacterProfileValidationIssue> Issues;
	TestFalse(TEXT("Every directional cross-owner collision rejects the Profile"),
		Asset->ValidateCharacterProfileAsset(Issues));
	int32 OwnershipIssueCount = 0;
	for (const FCharacterProfileValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == ECharacterProfileValidationSeverity::Error
			&& Issue.Context == TEXT("Directional Animation Ownership"))
		{
			++OwnershipIssueCount;
		}
	}
	TestEqual(TEXT("Variant-variant and base-variant paths are both reported"),
		OwnershipIssueCount, 2);
	TestNotNull(TEXT("The collision diagnostic names both logical owners and slots"),
		DirectionalAnimation_FindIssue(
			Issues,
			TEXT("Directional Animation Ownership"),
			TEXT("Directional Slot")));
	TestNotNull(TEXT("The base-plus-variant diagnostic names canonical base ownership"),
		DirectionalAnimation_FindIssue(
			Issues,
			TEXT("Directional Animation Ownership"),
			TEXT("canonical base")));
	bool bRuntimeAmbiguous = false;
	TestNull(TEXT("The variant collision rejected by validation also has no runtime owner"),
		Asset->ResolveLogicalAnimationOwner(SharedVariant, bRuntimeAmbiguous));
	TestTrue(TEXT("Validation and runtime agree that the resident-object key is ambiguous"),
		bRuntimeAmbiguous);
	TestNotNull(TEXT("The second canonical base remains available to keep the fixture live"), Run);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationBlueprintLegacyResolutionTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.LegacyAndConfiguredEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationBlueprintLegacyResolutionTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = DirectionalAnimation_AddMove(Asset, TEXT("LegacyBase"));

	TestFalse(TEXT("A legacy entry is not multidirectional"),
		UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(Asset, Base));
	UPaperFlipbook* ResolvedFlipbook = Base;
	int32 ResolvedSlotIndex = 11;
	bool bResolvedMirror = true;
	TestEqual(TEXT("A legacy entry resolves successfully"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Asset,
			Base,
			FVector2D(0.0, 1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("Legacy resolution returns the canonical base"), ResolvedFlipbook, Base);
	TestEqual(TEXT("Legacy resolution has no directional slot"), ResolvedSlotIndex, INDEX_NONE);
	TestFalse(TEXT("The canonical base never presents mirrored"), bResolvedMirror);

	TArray<FPaper2DPlusOccupiedDirectionSlot> OccupiedSlots;
	DirectionalAnimation_SeedOccupiedOutputs(Base, OccupiedSlots);
	TestEqual(TEXT("Legacy enumeration succeeds"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
			Asset,
			Base,
			OccupiedSlots),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestTrue(TEXT("Legacy enumeration clears its array to empty"), OccupiedSlots.IsEmpty());

	TestTrue(TEXT("The entry can explicitly retain an empty directional set"),
		Asset->EnableDirectionalSet(0));
	TestFalse(TEXT("A configured-empty entry is not multidirectional"),
		UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(Asset, Base));
	DirectionalAnimation_SeedResolveOutputs(
		Base, ResolvedFlipbook, ResolvedSlotIndex, bResolvedMirror);
	TestEqual(TEXT("A configured-empty entry resolves successfully"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Asset,
			Base,
			FVector2D(0.0, 1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("Configured-empty resolution returns the canonical base"), ResolvedFlipbook, Base);
	TestEqual(TEXT("Configured-empty resolution has no slot"), ResolvedSlotIndex, INDEX_NONE);
	TestFalse(TEXT("Configured-empty resolution never presents mirrored"), bResolvedMirror);
	DirectionalAnimation_SeedOccupiedOutputs(Base, OccupiedSlots);
	TestEqual(TEXT("Configured-empty enumeration succeeds"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
			Asset,
			Base,
			OccupiedSlots),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestTrue(TEXT("Configured-empty enumeration returns no records"), OccupiedSlots.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationBlueprintExactResolutionTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.ExactResolutionAndOwnerParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationBlueprintExactResolutionTest::RunTest(
	const FString& Parameters)
{
	const FDirectionalAnimationQueryFixture Fixture =
		DirectionalAnimation_MakeQueryFixture();
	TestTrue(TEXT("The populated base reports multidirectional"),
		UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(
			Fixture.Asset,
			Fixture.Base));
	TestTrue(TEXT("An active variant resolves the same multidirectional owner"),
		UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(
			Fixture.Asset,
			Fixture.SlotZero));

	UPaperFlipbook* ResolvedFlipbook = nullptr;
	int32 ResolvedSlotIndex = INDEX_NONE;
	bool bResolvedMirror = true;
	TestEqual(TEXT("A base key resolves the exact occupied sector"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Fixture.Asset,
			Fixture.Base,
			FVector2D(1.0, -1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("The clockwise southwest sector returns slot 3"), ResolvedSlotIndex, 3);
	TestFalse(TEXT("An unmirrored authored slot reports no mirror"), bResolvedMirror);
	TestEqual(TEXT("Slot 3 returns its exact authored variant"),
		ResolvedFlipbook,
		Fixture.SlotThree);

	DirectionalAnimation_SeedResolveOutputs(
		Fixture.Base,
		ResolvedFlipbook,
		ResolvedSlotIndex,
		bResolvedMirror);
	TestEqual(TEXT("A variant key resolves through the same logical owner"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Fixture.Asset,
			Fixture.SlotZero,
			FVector2D(1.0, -1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("Variant-key resolution returns the same slot"), ResolvedSlotIndex, 3);
	TestEqual(TEXT("Variant-key resolution returns the same art"),
		ResolvedFlipbook,
		Fixture.SlotThree);

	TestTrue(TEXT("The occupied sector accepts a mirror flag"),
		Fixture.Asset->SetDirectionalSlotMirror(0, 3, true));
	DirectionalAnimation_SeedResolveOutputs(
		Fixture.Base,
		ResolvedFlipbook,
		ResolvedSlotIndex,
		bResolvedMirror);
	bResolvedMirror = false;
	TestEqual(TEXT("A mirrored slot still resolves its exact art"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Fixture.Asset,
			Fixture.Base,
			FVector2D(1.0, -1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("The mirrored resolution selects the same slot"), ResolvedSlotIndex, 3);
	TestTrue(TEXT("The resolver returns the authored mirror flag beside the art"),
		bResolvedMirror);
	TestTrue(TEXT("The proof restores the unmirrored fixture"),
		Fixture.Asset->SetDirectionalSlotMirror(0, 3, false));

	DirectionalAnimation_SeedResolveOutputs(
		Fixture.Base,
		ResolvedFlipbook,
		ResolvedSlotIndex,
		bResolvedMirror);
	TestEqual(TEXT("A tiny finite nonzero magnitude is accepted"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Fixture.Asset,
			Fixture.Base,
			FVector2D(0.0, 1.0e-20),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("Bearing alone selects +Y slot 0"), ResolvedSlotIndex, 0);
	TestEqual(TEXT("Tiny +Y returns the occupied slot-0 art"),
		ResolvedFlipbook,
		Fixture.SlotZero);

	DirectionalAnimation_SeedResolveOutputs(
		Fixture.Base,
		ResolvedFlipbook,
		ResolvedSlotIndex,
		bResolvedMirror);
	TestEqual(TEXT("An empty exact sector is explicit"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Fixture.Asset,
			Fixture.Base,
			FVector2D(1.0, 0.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::DirectionUnoccupied);
	TestFalse(TEXT("An unoccupied sector clears the mirror flag"), bResolvedMirror);
	TestNull(TEXT("Exact-empty resolution never falls back to base or nearest art"),
		ResolvedFlipbook);
	// Deliberate contract change (TASK-190 review): the empty-sector failure reports WHICH sector
	// resolved so a Blueprint can implement its own fallback. +X is 90 degrees clockwise = slot 2.
	TestEqual(TEXT("Exact-empty resolution reports the resolved empty sector"),
		ResolvedSlotIndex,
		2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationTopologyQueryTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.TopologyAndOccupancyQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationTopologyQueryTest::RunTest(const FString& Parameters)
{
	const FDirectionalAnimationQueryFixture Fixture =
		DirectionalAnimation_MakeQueryFixture();

	// Topology exposure: with these two reads plus Resolve Direction Slot Index, a Blueprint can
	// author any nearest/base/hold fallback of its own without loading a single variant.
	int32 DirectionCount = -5;
	float AngleOffsetDegrees = 99.0f;
	bool bHasDirectionalSet = false;
	TestEqual(TEXT("Topology query succeeds for a populated base"),
		UPaper2DPlusDirectionalAnimationLibrary::GetDirectionSettings(
			Fixture.Asset,
			Fixture.Base,
			DirectionCount,
			AngleOffsetDegrees,
			bHasDirectionalSet),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("Topology query reports the effective direction count"),
		DirectionCount, 8);
	TestEqual(TEXT("Topology query reports the effective angle offset"),
		AngleOffsetDegrees, 0.0f);
	TestTrue(TEXT("Topology query reports explicit set presence"), bHasDirectionalSet);

	TArray<int32> OccupiedIndices;
	OccupiedIndices.Add(99);
	TestEqual(TEXT("Occupied-index query succeeds"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlotIndices(
			Fixture.Asset,
			Fixture.Base,
			OccupiedIndices),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("Occupied-index query returns exactly the sparse occupancy"),
		OccupiedIndices.Num(), 2);
	if (OccupiedIndices.Num() == 2)
	{
		TestEqual(TEXT("Occupied indices are ascending: slot zero first"),
			OccupiedIndices[0], 0);
		TestEqual(TEXT("Occupied indices are ascending: slot three second"),
			OccupiedIndices[1], 3);
	}

	int32 SlotIndex = INDEX_NONE;
	TestTrue(TEXT("The pure slot-index node accepts a valid facing"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionSlotIndex(
			FVector2D(1.0, 0.0), 8, 0.0f, SlotIndex));
	TestEqual(TEXT("+X resolves 90 degrees clockwise to slot 2"), SlotIndex, 2);
	TestFalse(TEXT("A zero vector fails closed"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionSlotIndex(
			FVector2D::ZeroVector, 8, 0.0f, SlotIndex));
	TestEqual(TEXT("A failed slot-index resolve clears its output"),
		SlotIndex, INDEX_NONE);

	for (int32 Slot = 0; Slot < 8; ++Slot)
	{
		SlotIndex = INDEX_NONE;
		TestTrue(
			*FString::Printf(TEXT("Bearing %d resolves"), Slot * 45),
			UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionSlotIndex(
				UPaper2DPlusDirectionalAnimationLibrary::MakeDirectionFromBearing(
					45.0f * Slot),
				8, 0.0f, SlotIndex));
		TestEqual(
			*FString::Printf(TEXT("Make Direction From Bearing round-trips slot %d"), Slot),
			SlotIndex,
			Slot);
	}

	DirectionCount = 7;
	AngleOffsetDegrees = 7.0f;
	bHasDirectionalSet = true;
	TestEqual(TEXT("A null profile fails the topology query closed"),
		UPaper2DPlusDirectionalAnimationLibrary::GetDirectionSettings(
			nullptr,
			Fixture.Base,
			DirectionCount,
			AngleOffsetDegrees,
			bHasDirectionalSet),
		EPaper2DPlusDirectionalAnimationResult::InvalidRequest);
	TestEqual(TEXT("A failed topology query clears its count"), DirectionCount, 0);
	TestEqual(TEXT("A failed topology query clears its angle offset"),
		AngleOffsetDegrees, 0.0f);
	TestFalse(TEXT("A failed topology query clears set presence"), bHasDirectionalSet);
	TestEqual(TEXT("A null profile fails the occupied-index query closed"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlotIndices(
			nullptr,
			Fixture.Base,
			OccupiedIndices),
		EPaper2DPlusDirectionalAnimationResult::InvalidRequest);
	TestTrue(TEXT("A failed occupied-index query clears its array"),
		OccupiedIndices.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationVariantWarmLifecycleTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.VariantWarmLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationVariantWarmLifecycleTest::RunTest(
	const FString& Parameters)
{
	// The async directional warm exists so a facing change never sync-loads mid-play. Its whole
	// contract is lifecycle: created when a directional animation's cache warms, released on the
	// next animation change, and never created for a set-less animation.
	const FDirectionalAnimationQueryFixture Fixture =
		DirectionalAnimation_MakeQueryFixture();
	AActor* Owner = NewObject<AActor>();
	UPaperFlipbookComponent* FlipbookComponent =
		NewObject<UPaperFlipbookComponent>(Owner);
	UPaper2DPlusCharacterProfileComponent* Component =
		NewObject<UPaper2DPlusCharacterProfileComponent>(Owner);
	Owner->AddOwnedComponent(FlipbookComponent);
	Owner->AddOwnedComponent(Component);
	Component->FlipbookComponent = FlipbookComponent;
	Component->CharacterProfile = Fixture.Asset;

	TestFalse(TEXT("A fresh component holds no directional warm"),
		Component->HasDirectionalVariantWarmForTests());
	Component->HandleFlipbookChanged(Fixture.Base);
	TestTrue(TEXT("Warming a directional animation retains its occupied variants"),
		Component->HasDirectionalVariantWarmForTests());

	UPaper2DPlusCharacterProfileAsset* LegacyAsset =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* LegacyBase =
		DirectionalAnimation_AddMove(LegacyAsset, TEXT("WarmLifecycleLegacyBase"));
	Component->CharacterProfile = LegacyAsset;
	Component->HandleFlipbookChanged(LegacyBase);
	TestFalse(TEXT("An animation change to a set-less entry releases the previous warm"),
		Component->HasDirectionalVariantWarmForTests());

	Component->CharacterProfile = Fixture.Asset;
	Component->HandleFlipbookChanged(Fixture.Base);
	TestTrue(TEXT("Returning to the directional animation re-creates the warm"),
		Component->HasDirectionalVariantWarmForTests());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationBlueprintLoadBoundaryTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.SelectedOnlyLoadAndAtomicEnumeration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationBlueprintLoadBoundaryTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = DirectionalAnimation_AddMove(Asset, TEXT("LoadBoundaryBase"));
	UPaperFlipbook* SlotZero = DirectionalAnimation_MakeFlipbook(
		Asset,
		TEXT("LoadBoundarySlotZero"));
	const TSoftObjectPtr<UPaperFlipbook> MissingSlotThree(
		FSoftObjectPath(TEXT("/Game/__Paper2DPlusAutomationMissing__/DA_QueryMissing.DA_QueryMissing")));
	TestTrue(TEXT("The selected resident slot is authored"),
		Asset->SetDirectionalSlot(0, 0, SlotZero));
	TestTrue(TEXT("The unselected missing path remains valid authored storage"),
		Asset->SetDirectionalSlot(0, 3, MissingSlotThree));
	TestNull(TEXT("The missing slot starts unloaded"), MissingSlotThree.Get());

	TestTrue(TEXT("Has Multi Direction is true without loading occupied paths"),
		UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(Asset, Base));
	TestNull(TEXT("Has Multi Direction leaves the missing path cold"), MissingSlotThree.Get());

	// The two load-free reads answer even for an UNLOADABLE slot: topology needs no art at all,
	// and the index query reports the broken slot as occupied — the deliberate divergence from the
	// loading enumeration below, which fails closed with Load Failed for the same fixture.
	int32 SettingsDirectionCount = 0;
	float SettingsAngleOffsetDegrees = 99.0f;
	bool bSettingsHasSet = false;
	TestEqual(TEXT("Get Direction Settings answers without loading occupied paths"),
		UPaper2DPlusDirectionalAnimationLibrary::GetDirectionSettings(
			Asset, Base,
			SettingsDirectionCount, SettingsAngleOffsetDegrees, bSettingsHasSet),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("Load-free settings report the effective count"),
		SettingsDirectionCount, 8);
	TestNull(TEXT("Get Direction Settings leaves the missing path cold"),
		MissingSlotThree.Get());
	TArray<int32> OccupiedIndices;
	TestEqual(TEXT("Get Occupied Direction Slot Indices answers without loading"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlotIndices(
			Asset, Base, OccupiedIndices),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("The index query reports both slots, broken art included"),
		OccupiedIndices.Num(), 2);
	if (OccupiedIndices.Num() == 2)
	{
		TestEqual(TEXT("Index query lists slot zero"), OccupiedIndices[0], 0);
		TestEqual(TEXT("Index query lists the broken slot three"), OccupiedIndices[1], 3);
	}
	TestNull(TEXT("The index query leaves the missing path cold"),
		MissingSlotThree.Get());

	UPaperFlipbook* ResolvedFlipbook = Base;
	int32 ResolvedSlotIndex = 11;
	bool bResolvedMirror = true;
	TestEqual(TEXT("Resolving slot 0 ignores the broken unselected slot"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Asset,
			Base,
			FVector2D(0.0, 1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("Selected-only loading returns slot 0"), ResolvedSlotIndex, 0);
	TestEqual(TEXT("Selected-only loading returns the resident slot"),
		ResolvedFlipbook,
		SlotZero);
	TestNull(TEXT("Resolving slot 0 leaves the broken slot cold"), MissingSlotThree.Get());

	DirectionalAnimation_SeedResolveOutputs(
		Base, ResolvedFlipbook, ResolvedSlotIndex, bResolvedMirror);
	TestEqual(TEXT("Selecting the broken occupied slot reports Load Failed"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Asset,
			Base,
			FVector2D(1.0, -1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::LoadFailed);
	TestNull(TEXT("Selected-load failure clears the flipbook"), ResolvedFlipbook);
	TestEqual(TEXT("Selected-load failure clears the slot index"),
		ResolvedSlotIndex,
		INDEX_NONE);
	TestFalse(TEXT("Selected-load failure clears the mirror flag"), bResolvedMirror);

	TArray<FPaper2DPlusOccupiedDirectionSlot> OccupiedSlots;
	DirectionalAnimation_SeedOccupiedOutputs(Base, OccupiedSlots);
	TestEqual(TEXT("Enumeration reports one broken required occupied slot"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
			Asset,
			Base,
			OccupiedSlots),
		EPaper2DPlusDirectionalAnimationResult::LoadFailed);
	TestTrue(TEXT("Enumeration failure is atomic, with no resident-slot prefix"),
		OccupiedSlots.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationBlueprintEnumerationTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.OccupiedEnumerationOrderAndOwnerParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationBlueprintEnumerationTest::RunTest(
	const FString& Parameters)
{
	const FDirectionalAnimationQueryFixture Fixture =
		DirectionalAnimation_MakeQueryFixture();
	UPaperFlipbook* SlotFive = DirectionalAnimation_MakeFlipbook(
		Fixture.Asset,
		TEXT("DirectionalQuerySlotFive"));
	TestTrue(TEXT("A third sparse occupied slot is authored"),
		Fixture.Asset->SetDirectionalSlot(0, 5, SlotFive));
	TestTrue(TEXT("The middle occupied slot is mirrored"),
		Fixture.Asset->SetDirectionalSlotMirror(0, 3, true));
	FPaper2DPlusDirectionalAnimationData& Data =
		Fixture.Asset->Flipbooks[0].DirectionalAnimationData;
	Data.Slots.Swap(0, 2);

	TArray<FPaper2DPlusOccupiedDirectionSlot> BaseSlots;
	DirectionalAnimation_SeedOccupiedOutputs(Fixture.Base, BaseSlots);
	TestEqual(TEXT("Base-key enumeration succeeds"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
			Fixture.Asset,
			Fixture.Base,
			BaseSlots),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("All active occupied slots are returned"), BaseSlots.Num(), 3);
	if (BaseSlots.Num() == 3)
	{
		TestEqual(TEXT("The first record is authored slot 0"), BaseSlots[0].SlotIndex, 0);
		TestEqual(TEXT("Slot 0 preserves its authored flipbook"),
			BaseSlots[0].Flipbook.Get(),
			Fixture.SlotZero);
		TestFalse(TEXT("An unmirrored record reports no mirror"),
			BaseSlots[0].bMirrorHorizontally);
		TestEqual(TEXT("The second record is authored slot 3"), BaseSlots[1].SlotIndex, 3);
		TestEqual(TEXT("Slot 3 preserves its authored flipbook"),
			BaseSlots[1].Flipbook.Get(),
			Fixture.SlotThree);
		TestTrue(TEXT("The mirrored record carries its authored mirror flag"),
			BaseSlots[1].bMirrorHorizontally);
		TestEqual(TEXT("The final record is authored slot 5"), BaseSlots[2].SlotIndex, 5);
		TestEqual(TEXT("Slot 5 preserves its authored flipbook"),
			BaseSlots[2].Flipbook.Get(),
			SlotFive);
	}

	TArray<FPaper2DPlusOccupiedDirectionSlot> VariantSlots;
	DirectionalAnimation_SeedOccupiedOutputs(Fixture.Base, VariantSlots);
	TestEqual(TEXT("Variant-key enumeration succeeds"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
			Fixture.Asset,
			Fixture.SlotThree,
			VariantSlots),
		EPaper2DPlusDirectionalAnimationResult::Success);
	TestEqual(TEXT("Base and variant keys enumerate the same record count"),
		VariantSlots.Num(),
		BaseSlots.Num());
	for (int32 SlotArrayIndex = 0;
		SlotArrayIndex < BaseSlots.Num() && SlotArrayIndex < VariantSlots.Num();
		++SlotArrayIndex)
	{
		TestEqual(TEXT("Base and variant keys preserve the same slot order"),
			VariantSlots[SlotArrayIndex].SlotIndex,
			BaseSlots[SlotArrayIndex].SlotIndex);
		TestEqual(TEXT("Base and variant keys preserve the same flipbook"),
			VariantSlots[SlotArrayIndex].Flipbook.Get(),
			BaseSlots[SlotArrayIndex].Flipbook.Get());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationBlueprintRequestAndOwnerFailureTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.RequestAndOwnerFailuresClearOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationBlueprintRequestAndOwnerFailureTest::RunTest(
	const FString& Parameters)
{
	const FDirectionalAnimationQueryFixture Fixture =
		DirectionalAnimation_MakeQueryFixture();
	UPaperFlipbook* ResolvedFlipbook = Fixture.Base;
	int32 ResolvedSlotIndex = 11;
	bool bResolvedMirror = true;
	TestFalse(TEXT("A null Profile fails Has Multi Direction"),
		UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(nullptr, Fixture.Base));
	TestEqual(TEXT("Null input reports Invalid Request"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			nullptr,
			Fixture.Base,
			FVector2D(0.0, 1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::InvalidRequest);
	TestNull(TEXT("Invalid Request clears the flipbook"), ResolvedFlipbook);
	TestEqual(TEXT("Invalid Request clears the slot"), ResolvedSlotIndex, INDEX_NONE);
	TestFalse(TEXT("Invalid Request clears the mirror flag"), bResolvedMirror);
	TArray<FPaper2DPlusOccupiedDirectionSlot> OccupiedSlots;
	DirectionalAnimation_SeedOccupiedOutputs(Fixture.Base, OccupiedSlots);
	TestEqual(TEXT("Null enumeration input reports Invalid Request"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
			nullptr,
			Fixture.Base,
			OccupiedSlots),
		EPaper2DPlusDirectionalAnimationResult::InvalidRequest);
	TestTrue(TEXT("Invalid Request clears occupied records"), OccupiedSlots.IsEmpty());

	TestFalse(TEXT("A null Flipbook fails Has Multi Direction"),
		UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(Fixture.Asset, nullptr));
	DirectionalAnimation_SeedResolveOutputs(
		Fixture.Base,
		ResolvedFlipbook,
		ResolvedSlotIndex,
		bResolvedMirror);
	TestEqual(TEXT("A null Flipbook reports Invalid Request"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Fixture.Asset,
			nullptr,
			FVector2D(0.0, 1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::InvalidRequest);
	TestNull(TEXT("Null Flipbook input clears the resolved flipbook"), ResolvedFlipbook);
	TestEqual(TEXT("Null Flipbook input clears the resolved slot"),
		ResolvedSlotIndex,
		INDEX_NONE);
	DirectionalAnimation_SeedOccupiedOutputs(Fixture.Base, OccupiedSlots);
	TestEqual(TEXT("Null Flipbook enumeration reports Invalid Request"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
			Fixture.Asset,
			nullptr,
			OccupiedSlots),
		EPaper2DPlusDirectionalAnimationResult::InvalidRequest);
	TestTrue(TEXT("Null Flipbook enumeration clears occupied records"), OccupiedSlots.IsEmpty());

	UPaperFlipbook* Foreign = DirectionalAnimation_MakeFlipbook(
		Fixture.Asset,
		TEXT("DirectionalQueryForeign"));
	TestFalse(TEXT("A foreign flipbook fails Has Multi Direction"),
		UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(Fixture.Asset, Foreign));
	DirectionalAnimation_SeedResolveOutputs(
		Fixture.Base,
		ResolvedFlipbook,
		ResolvedSlotIndex,
		bResolvedMirror);
	TestEqual(TEXT("A foreign flipbook reports Flipbook Not In Profile"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			Fixture.Asset,
			Foreign,
			FVector2D(0.0, 1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::FlipbookNotInProfile);
	TestNull(TEXT("Foreign-owner failure clears the flipbook"), ResolvedFlipbook);
	TestEqual(TEXT("Foreign-owner failure clears the slot"),
		ResolvedSlotIndex,
		INDEX_NONE);
	DirectionalAnimation_SeedOccupiedOutputs(Fixture.Base, OccupiedSlots);
	TestEqual(TEXT("Foreign enumeration reports Flipbook Not In Profile"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
			Fixture.Asset,
			Foreign,
			OccupiedSlots),
		EPaper2DPlusDirectionalAnimationResult::FlipbookNotInProfile);
	TestTrue(TEXT("Foreign enumeration clears occupied records"), OccupiedSlots.IsEmpty());

	UPaper2DPlusCharacterProfileAsset* AmbiguousAsset =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FirstBase = DirectionalAnimation_AddMove(AmbiguousAsset, TEXT("FirstOwner"));
	DirectionalAnimation_AddMove(AmbiguousAsset, TEXT("SecondOwner"));
	UPaperFlipbook* SharedVariant = DirectionalAnimation_MakeFlipbook(
		AmbiguousAsset,
		TEXT("DirectionalQuerySharedVariant"));
	AmbiguousAsset->SetDirectionalSlot(0, 0, SharedVariant);
	AmbiguousAsset->SetDirectionalSlot(1, 3, SharedVariant);
	TestFalse(TEXT("An ambiguous owner fails Has Multi Direction"),
		UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(
			AmbiguousAsset,
			SharedVariant));
	DirectionalAnimation_SeedResolveOutputs(
		FirstBase,
		ResolvedFlipbook,
		ResolvedSlotIndex,
		bResolvedMirror);
	TestEqual(TEXT("A cross-owner key reports Ambiguous Flipbook"),
		UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
			AmbiguousAsset,
			SharedVariant,
			FVector2D(0.0, 1.0),
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror),
		EPaper2DPlusDirectionalAnimationResult::AmbiguousFlipbook);
	TestNull(TEXT("Ambiguous owner clears the flipbook"), ResolvedFlipbook);
	TestEqual(TEXT("Ambiguous owner clears the slot"), ResolvedSlotIndex, INDEX_NONE);
	DirectionalAnimation_SeedOccupiedOutputs(FirstBase, OccupiedSlots);
	TestEqual(TEXT("Ambiguous enumeration reports Ambiguous Flipbook"),
		UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
			AmbiguousAsset,
			SharedVariant,
			OccupiedSlots),
		EPaper2DPlusDirectionalAnimationResult::AmbiguousFlipbook);
	TestTrue(TEXT("Ambiguous enumeration clears occupied records"), OccupiedSlots.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationBlueprintPublishedVariantCollisionTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.BaseQueriesRejectPublishedVariantCollisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationBlueprintPublishedVariantCollisionTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* VariantCollisionAsset =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FirstVariantCollisionBase =
		DirectionalAnimation_AddMove(VariantCollisionAsset, TEXT("FirstVariantCollisionOwner"));
	UPaperFlipbook* SecondVariantCollisionBase =
		DirectionalAnimation_AddMove(VariantCollisionAsset, TEXT("SecondVariantCollisionOwner"));
	UPaperFlipbook* SharedVariant = DirectionalAnimation_MakeFlipbook(
		VariantCollisionAsset,
		TEXT("PublishedSharedVariant"));
	UPaperFlipbook* FirstUniqueVariant = DirectionalAnimation_MakeFlipbook(
		VariantCollisionAsset,
		TEXT("FirstUniqueVariant"));
	UPaperFlipbook* SecondUniqueVariant = DirectionalAnimation_MakeFlipbook(
		VariantCollisionAsset,
		TEXT("SecondUniqueVariant"));
	TestTrue(TEXT("The first owner has a valid lower-index enumeration prefix"),
		VariantCollisionAsset->SetDirectionalSlot(0, 0, FirstUniqueVariant));
	TestTrue(TEXT("The second owner has a valid lower-index enumeration prefix"),
		VariantCollisionAsset->SetDirectionalSlot(1, 0, SecondUniqueVariant));
	TestTrue(TEXT("The first owner retains the shared variant for validation"),
		VariantCollisionAsset->SetDirectionalSlot(0, 3, SharedVariant));
	TestTrue(TEXT("The second owner retains the shared variant for validation"),
		VariantCollisionAsset->SetDirectionalSlot(1, 3, SharedVariant));
	DirectionalAnimation_AssertBaseQueryRejectsAmbiguousVariant(
		*this,
		TEXT("Variant-variant collision from first canonical base"),
		VariantCollisionAsset,
		FirstVariantCollisionBase);
	DirectionalAnimation_AssertBaseQueryRejectsAmbiguousVariant(
		*this,
		TEXT("Variant-variant collision from second canonical base"),
		VariantCollisionAsset,
		SecondVariantCollisionBase);

	UPaper2DPlusCharacterProfileAsset* BaseVariantCollisionAsset =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* VariantOwnerBase =
		DirectionalAnimation_AddMove(BaseVariantCollisionAsset, TEXT("VariantOwner"));
	UPaperFlipbook* OtherCanonicalBase =
		DirectionalAnimation_AddMove(BaseVariantCollisionAsset, TEXT("CanonicalBaseOwner"));
	UPaperFlipbook* UniqueVariant = DirectionalAnimation_MakeFlipbook(
		BaseVariantCollisionAsset,
		TEXT("BaseVariantUniquePrefix"));
	TestTrue(TEXT("The variant owner has a valid lower-index enumeration prefix"),
		BaseVariantCollisionAsset->SetDirectionalSlot(0, 0, UniqueVariant));
	TestTrue(TEXT("A canonical base can remain authored as another owner's variant for validation"),
		BaseVariantCollisionAsset->SetDirectionalSlot(0, 3, OtherCanonicalBase));
	DirectionalAnimation_AssertBaseQueryRejectsAmbiguousVariant(
		*this,
		TEXT("Base-variant collision from the variant owner's canonical base"),
		BaseVariantCollisionAsset,
		VariantOwnerBase);
	DirectionalAnimation_AssertBaseQueryRejectsAmbiguousVariant(
		*this,
		TEXT("Base-variant collision from the reused canonical base"),
		BaseVariantCollisionAsset,
		OtherCanonicalBase);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationBlueprintStructuralFailureTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.StructuralFailuresMapToInvalidProfileData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationBlueprintStructuralFailureTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* InvalidProfileCount =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* InvalidProfileCountKey =
		DirectionalAnimation_AddMove(InvalidProfileCount, TEXT("InvalidProfileCount"));
	UPaperFlipbook* InvalidProfileCountVariant = DirectionalAnimation_AddStoredVariant(
		InvalidProfileCount,
		0,
		0,
		TEXT("InvalidProfileCountVariant"));
	InvalidProfileCount->DefaultDirectionalCount = 2;
	DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		*this,
		TEXT("Invalid Profile count"),
		InvalidProfileCount,
		InvalidProfileCountKey,
		InvalidProfileCountVariant);

	UPaper2DPlusCharacterProfileAsset* InvalidProfileOffset =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* InvalidProfileOffsetKey =
		DirectionalAnimation_AddMove(InvalidProfileOffset, TEXT("InvalidProfileOffset"));
	UPaperFlipbook* InvalidProfileOffsetVariant = DirectionalAnimation_AddStoredVariant(
		InvalidProfileOffset,
		0,
		0,
		TEXT("InvalidProfileOffsetVariant"));
	InvalidProfileOffset->DefaultDirectionalAngleOffset =
		std::numeric_limits<float>::infinity();
	DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		*this,
		TEXT("Invalid Profile offset"),
		InvalidProfileOffset,
		InvalidProfileOffsetKey,
		InvalidProfileOffsetVariant);

	UPaper2DPlusCharacterProfileAsset* InvalidOverrideCount =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* InvalidOverrideCountKey =
		DirectionalAnimation_AddMove(InvalidOverrideCount, TEXT("InvalidOverrideCount"));
	FPaper2DPlusDirectionalAnimationData& InvalidOverrideCountData =
		InvalidOverrideCount->Flipbooks[0].DirectionalAnimationData;
	InvalidOverrideCountData.bHasDirectionalSet = true;
	InvalidOverrideCountData.bOverrideProfileSettings = true;
	InvalidOverrideCountData.DirectionCount = 17;
	UPaperFlipbook* InvalidOverrideCountVariant = DirectionalAnimation_AddStoredVariant(
		InvalidOverrideCount,
		0,
		0,
		TEXT("InvalidOverrideCountVariant"));
	DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		*this,
		TEXT("Invalid override count"),
		InvalidOverrideCount,
		InvalidOverrideCountKey,
		InvalidOverrideCountVariant);

	UPaper2DPlusCharacterProfileAsset* InvalidOverrideOffset =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* InvalidOverrideOffsetKey =
		DirectionalAnimation_AddMove(InvalidOverrideOffset, TEXT("InvalidOverrideOffset"));
	FPaper2DPlusDirectionalAnimationData& InvalidOverrideOffsetData =
		InvalidOverrideOffset->Flipbooks[0].DirectionalAnimationData;
	InvalidOverrideOffsetData.bHasDirectionalSet = true;
	InvalidOverrideOffsetData.bOverrideProfileSettings = true;
	InvalidOverrideOffsetData.AngleOffsetDegrees = -46.0f;
	UPaperFlipbook* InvalidOverrideOffsetVariant = DirectionalAnimation_AddStoredVariant(
		InvalidOverrideOffset,
		0,
		0,
		TEXT("InvalidOverrideOffsetVariant"));
	DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		*this,
		TEXT("Invalid override offset"),
		InvalidOverrideOffset,
		InvalidOverrideOffsetKey,
		InvalidOverrideOffsetVariant);

	UPaper2DPlusCharacterProfileAsset* OverrideWithoutSet =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* OverrideWithoutSetKey =
		DirectionalAnimation_AddMove(OverrideWithoutSet, TEXT("OverrideWithoutSet"));
	OverrideWithoutSet->Flipbooks[0].DirectionalAnimationData.bOverrideProfileSettings = true;
	UPaperFlipbook* OverrideWithoutSetVariant = DirectionalAnimation_AddStoredVariant(
		OverrideWithoutSet,
		0,
		0,
		TEXT("OverrideWithoutSetVariant"));
	DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		*this,
		TEXT("Override without set presence"),
		OverrideWithoutSet,
		OverrideWithoutSetKey,
		OverrideWithoutSetVariant);

	UPaper2DPlusCharacterProfileAsset* SlotsWithoutSet =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* SlotsWithoutSetKey =
		DirectionalAnimation_AddMove(SlotsWithoutSet, TEXT("SlotsWithoutSet"));
	UPaperFlipbook* SlotsWithoutSetVariant = DirectionalAnimation_AddStoredVariant(
		SlotsWithoutSet,
		0,
		0,
		TEXT("SlotsWithoutSetVariant"));
	bool bPublishedOwnerAmbiguous = false;
	TestNull(
		TEXT("Malformed stored variants remain unpublished to the general logical-owner surface"),
		SlotsWithoutSet->ResolveLogicalAnimationOwner(
			SlotsWithoutSetVariant,
			bPublishedOwnerAmbiguous));
	TestFalse(
		TEXT("An unpublished malformed variant is not a published ownership ambiguity"),
		bPublishedOwnerAmbiguous);
	DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		*this,
		TEXT("Slots without set presence"),
		SlotsWithoutSet,
		SlotsWithoutSetKey,
		SlotsWithoutSetVariant);

	UPaper2DPlusCharacterProfileAsset* InvalidSlotIndex =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* InvalidSlotIndexKey =
		DirectionalAnimation_AddMove(InvalidSlotIndex, TEXT("InvalidSlotIndex"));
	FPaper2DPlusDirectionalAnimationData& InvalidSlotIndexData =
		InvalidSlotIndex->Flipbooks[0].DirectionalAnimationData;
	InvalidSlotIndexData.bHasDirectionalSet = true;
	UPaperFlipbook* InvalidSlotIndexVariant = DirectionalAnimation_AddStoredVariant(
		InvalidSlotIndex,
		0,
		16,
		TEXT("InvalidSlotIndexVariant"));
	DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		*this,
		TEXT("Invalid slot key"),
		InvalidSlotIndex,
		InvalidSlotIndexKey,
		InvalidSlotIndexVariant);

	UPaper2DPlusCharacterProfileAsset* DuplicateSlotIndex =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* DuplicateSlotIndexKey =
		DirectionalAnimation_AddMove(DuplicateSlotIndex, TEXT("DuplicateSlotIndex"));
	FPaper2DPlusDirectionalAnimationData& DuplicateSlotIndexData =
		DuplicateSlotIndex->Flipbooks[0].DirectionalAnimationData;
	DuplicateSlotIndexData.bHasDirectionalSet = true;
	UPaperFlipbook* DuplicateSlotIndexVariant = DirectionalAnimation_AddStoredVariant(
		DuplicateSlotIndex,
		0,
		3,
		TEXT("DuplicateSlotIndexVariant"));
	DuplicateSlotIndexData.Slots.AddDefaulted_GetRef().SlotIndex = 3;
	DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		*this,
		TEXT("Duplicate slot key"),
		DuplicateSlotIndex,
		DuplicateSlotIndexKey,
		DuplicateSlotIndexVariant);

	UPaper2DPlusCharacterProfileAsset* InactiveOccupiedSlot =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* InactiveOccupiedSlotKey =
		DirectionalAnimation_AddMove(InactiveOccupiedSlot, TEXT("InactiveOccupiedSlot"));
	InactiveOccupiedSlot->DefaultDirectionalCount = 3;
	FPaper2DPlusDirectionalAnimationData& InactiveOccupiedSlotData =
		InactiveOccupiedSlot->Flipbooks[0].DirectionalAnimationData;
	InactiveOccupiedSlotData.bHasDirectionalSet = true;
	UPaperFlipbook* InactiveSlotVariant = DirectionalAnimation_AddStoredVariant(
		InactiveOccupiedSlot,
		0,
		3,
		TEXT("InactiveSlotArt"));
	DirectionalAnimation_AssertInvalidProfileDataForBaseAndVariant(
		*this,
		TEXT("Occupied inactive slot"),
		InactiveOccupiedSlot,
		InactiveOccupiedSlotKey,
		InactiveSlotVariant);

	UPaper2DPlusCharacterProfileAsset* MissingCanonicalBase =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry& MissingCanonicalBaseEntry =
		MissingCanonicalBase->Flipbooks.AddDefaulted_GetRef();
	MissingCanonicalBaseEntry.Identity.FlipbookName = TEXT("MissingCanonicalBase");
	MissingCanonicalBaseEntry.DirectionalAnimationData.bHasDirectionalSet = true;
	UPaperFlipbook* MissingCanonicalBaseKey = DirectionalAnimation_MakeFlipbook(
		MissingCanonicalBase,
		TEXT("MissingCanonicalBaseVariant"));
	FPaper2DPlusDirectionalAnimationSlot& MissingBaseSlot =
		MissingCanonicalBaseEntry.DirectionalAnimationData.Slots.AddDefaulted_GetRef();
	MissingBaseSlot.SlotIndex = 0;
	MissingBaseSlot.Flipbook = MissingCanonicalBaseKey;
	DirectionalAnimation_AssertInvalidProfileData(
		*this,
		TEXT("Missing canonical base"),
		MissingCanonicalBase,
		MissingCanonicalBaseKey);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationBlueprintInvalidDirectionTest,
	"Paper2DPlus.DirectionalAnimation.Blueprint.InvalidDirectionClearsOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationBlueprintInvalidDirectionTest::RunTest(
	const FString& Parameters)
{
	const FDirectionalAnimationQueryFixture Fixture =
		DirectionalAnimation_MakeQueryFixture();
	const double QuietNaN = std::numeric_limits<double>::quiet_NaN();
	const TArray<FVector2D> InvalidDirections = {
		FVector2D::ZeroVector,
		FVector2D(QuietNaN, 1.0),
		FVector2D(std::numeric_limits<double>::infinity(), 1.0)
	};
	for (int32 DirectionIndex = 0; DirectionIndex < InvalidDirections.Num(); ++DirectionIndex)
	{
		UPaperFlipbook* ResolvedFlipbook = nullptr;
		int32 ResolvedSlotIndex = INDEX_NONE;
		bool bResolvedMirror = false;
		DirectionalAnimation_SeedResolveOutputs(
			Fixture.Base,
			ResolvedFlipbook,
			ResolvedSlotIndex,
			bResolvedMirror);
		TestEqual(
			*FString::Printf(TEXT("Invalid vector %d reports Invalid Direction"), DirectionIndex),
			UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
				Fixture.Asset,
				Fixture.Base,
				InvalidDirections[DirectionIndex],
				ResolvedFlipbook,
				ResolvedSlotIndex,
				bResolvedMirror),
			EPaper2DPlusDirectionalAnimationResult::InvalidDirection);
		TestNull(
			*FString::Printf(TEXT("Invalid vector %d clears the flipbook"), DirectionIndex),
			ResolvedFlipbook);
		TestEqual(
			*FString::Printf(TEXT("Invalid vector %d clears the slot"), DirectionIndex),
			ResolvedSlotIndex,
			INDEX_NONE);
	}
	return true;
}

#endif // WITH_EDITOR
