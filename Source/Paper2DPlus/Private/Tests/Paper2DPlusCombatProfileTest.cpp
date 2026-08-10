// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCombatTags.h"
#include "PaperFlipbook.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"

namespace
{
	FHitboxData CombatProfileTest_Attack(int32 X, int32 Y, int32 W, int32 H, int32 Damage)
	{
		FHitboxData Hitbox;
		Hitbox.Type = EHitboxType::Attack;
		Hitbox.X = X;
		Hitbox.Y = Y;
		Hitbox.Width = W;
		Hitbox.Height = H;
		Hitbox.Damage = Damage;
		return Hitbox;
	}

	FGameplayTag CombatProfileTest_AttackTag()
	{
		return FGameplayTag::RequestGameplayTag(FName("PlayerStates.Attacking.GroundAttack"), false);
	}

	FGameplayTag CombatProfileTest_ExtraAttackTag()
	{
		return FGameplayTag::RequestGameplayTag(FName("PlayerStates.Attacking.AirAttack"), false);
	}

	UPaper2DPlusCharacterProfileAsset* CombatProfileTest_MakeCharacterProfile()
	{
		UPaper2DPlusCharacterProfileAsset* CharacterProfile = NewObject<UPaper2DPlusCharacterProfileAsset>();

		FFlipbookProfileEntry Close;
		Close.Identity.FlipbookName = TEXT("CloseSlash");
		Close.Identity.Flipbook = NewObject<UPaperFlipbook>(CharacterProfile, TEXT("CloseSlashFlipbook"));
		Close.CombatData.Frames.SetNum(2);
		Close.CombatData.Frames[0].Hitboxes.Add(CombatProfileTest_Attack(10, -4, 20, 8, 5));
		CharacterProfile->Flipbooks.Add(Close);

		FFlipbookProfileEntry Far;
		Far.Identity.FlipbookName = TEXT("FarThrust");
		Far.Identity.Flipbook = NewObject<UPaperFlipbook>(CharacterProfile, TEXT("FarThrustFlipbook"));
		Far.CombatData.Frames.SetNum(3);
		Far.CombatData.Frames[1].Hitboxes.Add(CombatProfileTest_Attack(80, -4, 20, 8, 8));
		CharacterProfile->Flipbooks.Add(Far);

		FFlipbookProfileEntry Lunge;
		Lunge.Identity.FlipbookName = TEXT("LungeStrike");
		Lunge.Identity.Flipbook = NewObject<UPaperFlipbook>(CharacterProfile, TEXT("LungeStrikeFlipbook"));
		Lunge.CombatData.Frames.SetNum(2);
		Lunge.CombatData.Frames[1].Hitboxes.Add(CombatProfileTest_Attack(20, -4, 20, 8, 7));
		Lunge.MotionData.RootMotion.SetNum(2);
		Lunge.MotionData.RootMotion[0].Position = FVector2D::ZeroVector;
		Lunge.MotionData.RootMotion[1].Position = FVector2D(50.0f, 0.0f);
		CharacterProfile->Flipbooks.Add(Lunge);

		FFlipbookProfileEntry Unmapped;
		Unmapped.Identity.FlipbookName = TEXT("UnmappedUppercut");
		Unmapped.Identity.Flipbook = NewObject<UPaperFlipbook>(CharacterProfile, TEXT("UnmappedUppercutFlipbook"));
		Unmapped.CombatData.Frames.SetNum(1);
		Unmapped.CombatData.Frames[0].Hitboxes.Add(CombatProfileTest_Attack(45, -4, 20, 8, 6));
		CharacterProfile->Flipbooks.Add(Unmapped);

		FFlipbookProfileEntry Idle;
		Idle.Identity.FlipbookName = TEXT("Idle");
		Idle.Identity.Flipbook = NewObject<UPaperFlipbook>(CharacterProfile, TEXT("IdleFlipbook"));
		Idle.CombatData.Frames.SetNum(1);
		CharacterProfile->Flipbooks.Add(Idle);

		const FGameplayTag AttackTag = CombatProfileTest_AttackTag();
		FFlipbookTagMapping& AttackMapping = CharacterProfile->TagMappings.FindOrAdd(AttackTag);
		AttackMapping.Entries.Add(FFlipbookTagMappingEntry(TEXT("CloseSlash")));
		AttackMapping.Entries.Add(FFlipbookTagMappingEntry(TEXT("FarThrust")));
		AttackMapping.Entries.Add(FFlipbookTagMappingEntry(TEXT("LungeStrike")));

		return CharacterProfile;
	}

	UPaper2DPlusCombatProfileAsset* CombatProfileTest_MakeCombatProfile()
	{
		UPaper2DPlusCombatProfileAsset* CombatProfile = NewObject<UPaper2DPlusCombatProfileAsset>();
		CombatProfile->CharacterProfile = CombatProfileTest_MakeCharacterProfile();
		return CombatProfile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatProfileValidationTest,
	"Paper2DPlus.CombatProfile.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatProfileValidationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* MissingProfile = NewObject<UPaper2DPlusCombatProfileAsset>();
	TArray<FPaper2DPlusCombatValidationIssue> Issues;
	TestFalse(TEXT("Missing character profile is invalid"), MissingProfile->ValidateCombatProfileAsset(Issues));
	TestEqual(TEXT("Missing profile reports one issue"), Issues.Num(), 1);
	TestEqual(TEXT("Missing profile severity is Error"), Issues[0].Severity, EPaper2DPlusCombatValidationSeverity::Error);

	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatProfileTest_MakeCombatProfile();
	FPaper2DPlusCombatAttackOption Dangling;
	Dangling.MoveName = TEXT("NoSuchMove");
	CombatProfile->AttackOptions.Add(Dangling);

	FPaper2DPlusCombatVariableDefinition VariableA;
	VariableA.VariableTag = Paper2DPlusCombatTags::Var_Aggression;
	CombatProfile->VariableDefinitions.Add(VariableA);

	FPaper2DPlusCombatVariableDefinition VariableB;
	VariableB.VariableTag = Paper2DPlusCombatTags::Var_Aggression;
	CombatProfile->VariableDefinitions.Add(VariableB);

	Issues.Reset();
	TestFalse(TEXT("Dangling move + duplicate variable are invalid"), CombatProfile->ValidateCombatProfileAsset(Issues));
	TestTrue(TEXT("Validation reports multiple issues"), Issues.Num() >= 2);
	TestTrue(TEXT("Validation includes dangling move"), Issues.ContainsByPredicate([](const FPaper2DPlusCombatValidationIssue& Issue)
	{
		return Issue.MoveName == TEXT("NoSuchMove");
	}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatProfileCatalogTest,
	"Paper2DPlus.CombatProfile.Catalog.DerivesAttackFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatProfileCatalogTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatProfileTest_MakeCombatProfile();
	const FGameplayTag AttackTag = CombatProfileTest_AttackTag();
	const FGameplayTag ExtraAttackTag = CombatProfileTest_ExtraAttackTag();
	TestTrue(TEXT("Ground attack test tag is registered"), AttackTag.IsValid());
	TestTrue(TEXT("Extra attack test tag is registered"), ExtraAttackTag.IsValid());
	if (!AttackTag.IsValid() || !ExtraAttackTag.IsValid())
	{
		return false;
	}

	TArray<FPaper2DPlusCombatAttackDerivedData> Catalog;
	CombatProfile->BuildAttackCatalog(Catalog);
	TestEqual(TEXT("Catalog inherits tagged attacks without combat profile rows"), Catalog.Num(), 3);
	TestFalse(TEXT("Unmapped attack hitbox is not inherited by default"), Catalog.ContainsByPredicate([](const FPaper2DPlusCombatAttackDerivedData& Row)
	{
		return Row.MoveName == TEXT("UnmappedUppercut");
	}));

	const int32 AddedOptions = CombatProfile->GenerateAttackOptionsFromCharacterProfile();
	TestEqual(TEXT("Generated options only for tagged attack moves"), AddedOptions, 3);
	TestEqual(TEXT("Generated option count"), CombatProfile->AttackOptions.Num(), 3);

	CombatProfile->BuildAttackCatalog(Catalog);
	TestEqual(TEXT("Catalog has attack moves only"), Catalog.Num(), 3);

	const FPaper2DPlusCombatAttackDerivedData* Close = Catalog.FindByPredicate([](const FPaper2DPlusCombatAttackDerivedData& Row)
	{
		return Row.MoveName == TEXT("CloseSlash");
	});
	TestNotNull(TEXT("Catalog includes CloseSlash"), Close);
	if (Close)
	{
		TestTrue(TEXT("CloseSlash has attack"), Close->bHasAttack);
		TestTrue(TEXT("CloseSlash inherits from character profile tag"), Close->bInheritedFromCharacterProfile);
		TestTrue(TEXT("CloseSlash has generated tuning row"), Close->bHasCombatProfileOption);
		TestEqual(TEXT("CloseSlash attack tag comes from character profile"), Close->AttackTag, AttackTag);
		TestEqual(TEXT("CloseSlash active frames"), Close->FrameData.ActiveFrames, 1);
		TestEqual(TEXT("CloseSlash damage from frame data"), Close->FrameData.MaxDamage, 5.f);
		TestTrue(TEXT("CloseSlash forward min is 10"), FMath::IsNearlyEqual(Close->ForwardRangeLocal.X, 10.0f));
		TestTrue(TEXT("CloseSlash forward max is 30"), FMath::IsNearlyEqual(Close->ForwardRangeLocal.Y, 30.0f));
		TestTrue(TEXT("CloseSlash raw hitbox range matches effective range"), Close->HitboxForwardRangeLocal.Equals(Close->ForwardRangeLocal));
	}

	const FPaper2DPlusCombatAttackDerivedData* Lunge = Catalog.FindByPredicate([](const FPaper2DPlusCombatAttackDerivedData& Row)
	{
		return Row.MoveName == TEXT("LungeStrike");
	});
	TestNotNull(TEXT("Catalog includes LungeStrike"), Lunge);
	if (Lunge)
	{
		TestTrue(TEXT("LungeStrike has root motion"), Lunge->FrameData.bHasRootMotion);
		TestTrue(TEXT("LungeStrike raw hitbox min remains 20"), FMath::IsNearlyEqual(Lunge->HitboxForwardRangeLocal.X, 20.0f));
		TestTrue(TEXT("LungeStrike raw hitbox max remains 40"), FMath::IsNearlyEqual(Lunge->HitboxForwardRangeLocal.Y, 40.0f));
		TestTrue(TEXT("LungeStrike root motion offset min is 50"), FMath::IsNearlyEqual(Lunge->RootMotionAttackOffsetRangeLocal.X, 50.0f));
		TestTrue(TEXT("LungeStrike root motion offset max is 50"), FMath::IsNearlyEqual(Lunge->RootMotionAttackOffsetRangeLocal.Y, 50.0f));
		TestTrue(TEXT("LungeStrike effective forward min includes root motion"), FMath::IsNearlyEqual(Lunge->ForwardRangeLocal.X, 70.0f));
		TestTrue(TEXT("LungeStrike effective forward max includes root motion"), FMath::IsNearlyEqual(Lunge->ForwardRangeLocal.Y, 90.0f));
		TestTrue(TEXT("LungeStrike preferred range defaults to effective reach"), Lunge->PreferredRangeLocal.Equals(Lunge->ForwardRangeLocal));
	}

	FFlipbookTagMapping* AttackMapping = CombatProfile->CharacterProfile->TagMappings.Find(AttackTag);
	TestNotNull(TEXT("Attack mapping exists"), AttackMapping);
	if (AttackMapping)
	{
		AttackMapping->Entries.RemoveAll([](const FFlipbookTagMappingEntry& Entry)
		{
			return Entry.FlipbookName.Equals(TEXT("LungeStrike"), ESearchCase::IgnoreCase);
		});
	}
	CombatProfile->BuildAttackCatalog(Catalog);
	TestEqual(TEXT("Catalog reacts to character profile tag removal despite generated tuning row"), Catalog.Num(), 2);
	TestFalse(TEXT("Removed tag mapping hides LungeStrike"), Catalog.ContainsByPredicate([](const FPaper2DPlusCombatAttackDerivedData& Row)
	{
		return Row.MoveName == TEXT("LungeStrike");
	}));

	FPaper2DPlusCombatAttackOption ExtraOption;
	ExtraOption.MoveName = TEXT("UnmappedUppercut");
	ExtraOption.AttackTag = ExtraAttackTag;
	ExtraOption.BaseWeight = 2.0f;
	CombatProfile->AttackOptions.Add(ExtraOption);
	CombatProfile->BuildAttackCatalog(Catalog);
	TestEqual(TEXT("Combat profile row does not include untagged move until explicitly additive"), Catalog.Num(), 2);

	CombatProfile->AttackOptions.Last().bIncludeWhenNotTagged = true;
	CombatProfile->BuildAttackCatalog(Catalog);
	TestEqual(TEXT("Explicit additive row adds an untagged attack"), Catalog.Num(), 3);
	const FPaper2DPlusCombatAttackDerivedData* Extra = Catalog.FindByPredicate([](const FPaper2DPlusCombatAttackDerivedData& Row)
	{
		return Row.MoveName == TEXT("UnmappedUppercut");
	});
	TestNotNull(TEXT("Catalog includes explicitly additive UnmappedUppercut"), Extra);
	if (Extra)
	{
		TestFalse(TEXT("Extra row is not inherited from character profile"), Extra->bInheritedFromCharacterProfile);
		TestTrue(TEXT("Extra row comes from combat profile option"), Extra->bHasCombatProfileOption);
		TestEqual(TEXT("Extra row uses combat profile attack tag"), Extra->AttackTag, ExtraAttackTag);
		TestTrue(TEXT("Extra row keeps combat profile base weight"), FMath::IsNearlyEqual(Extra->BaseWeight, 2.0f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatProfileVariableBagTest,
	"Paper2DPlus.CombatProfile.Variables.SchemaPreservesValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatProfileVariableBagTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatProfileTest_MakeCombatProfile();
	CombatProfile->TagDefaults.AddDefaulted();
	CombatProfile->AttackOptions.AddDefaulted();
	CombatProfile->ScenarioPresets.AddDefaulted();

	FPaper2DPlusCombatVariableDefinition Aggression;
	Aggression.VariableTag = Paper2DPlusCombatTags::Var_Aggression;
	Aggression.Type = EPaper2DPlusCombatVariableType::Float;
	CombatProfile->VariableDefinitions.Add(Aggression);
	CombatProfile->RebuildVariableBags();

	TestEqual(TEXT("Global map has one value"), CombatProfile->GlobalVariables.Num(), 1);
	TestTrue(TEXT("tag variable map starts sparse and inherits"), CombatProfile->TagDefaults[0].Variables.IsEmpty());
	TestTrue(TEXT("move variable map starts sparse and inherits"), CombatProfile->AttackOptions[0].Variables.IsEmpty());
	TestTrue(TEXT("scenario variable map starts sparse and inherits"),
		CombatProfile->ScenarioPresets[0].Context.RuntimeVariables.IsEmpty());
	CombatProfile->GlobalVariables.FindOrAdd(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.75f;
	CombatProfile->TagDefaults[0].Variables.FindOrAdd(
		Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.5f;

	FPaper2DPlusCombatVariableDefinition CanPunish;
	CanPunish.VariableTag = Paper2DPlusCombatTags::Var_CanPunish;
	CanPunish.Type = EPaper2DPlusCombatVariableType::Bool;
	CombatProfile->VariableDefinitions.Add(CanPunish);
	CombatProfile->RebuildVariableBags();

	float AggressionValue = 0.0f;
	TestTrue(TEXT("Aggression value survives rebuild"), CombatProfile->TryGetFloatVariable(CombatProfile->GlobalVariables, Paper2DPlusCombatTags::Var_Aggression, AggressionValue));
	TestTrue(TEXT("Aggression preserved"), FMath::IsNearlyEqual(AggressionValue, 0.75f));
	TestEqual(TEXT("Global map has two values"), CombatProfile->GlobalVariables.Num(), 2);
	TestEqual(TEXT("existing sparse tag override survives while new definitions remain inherited"),
		CombatProfile->TagDefaults[0].Variables.Num(), 1);

	CombatProfile->GlobalVariables.FindOrAdd(Paper2DPlusCombatTags::Var_CanPunish).BoolValue = true;
	bool bCanPunish = false;
	TestTrue(TEXT("CanPunish reads through typed helper"), CombatProfile->TryGetBoolVariable(CombatProfile->GlobalVariables, Paper2DPlusCombatTags::Var_CanPunish, bCanPunish));
	TestTrue(TEXT("CanPunish value"), bCanPunish);

	// Cross-type reads fail closed (mirrors the old PropertyBag type checking): a float read of a Bool variable
	// — or a bool read of a Float variable — must NOT resolve, so a mis-typed consideration scores nothing
	// rather than silently reading a stale/default field.
	float MismatchFloat = -1.0f;
	TestFalse(TEXT("Float read of a Bool variable fails closed"), CombatProfile->TryGetFloatVariable(CombatProfile->GlobalVariables, Paper2DPlusCombatTags::Var_CanPunish, MismatchFloat));
	bool bMismatchBool = true;
	TestFalse(TEXT("Bool read of a Float variable fails closed"), CombatProfile->TryGetBoolVariable(CombatProfile->GlobalVariables, Paper2DPlusCombatTags::Var_Aggression, bMismatchBool));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatProfileNullPathsTest,
	"Paper2DPlus.CombatProfile.NullPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatProfileNullPathsTest::RunTest(const FString& Parameters)
{
	// The BP-facing combat entry point resolves its profile from an actor, so it must
	// fail closed on a null actor rather than crash. (The former asset-taking library
	// wrappers were removed — the asset methods are the single BP home for the quartet.)
	FPaper2DPlusCombatRuntimeContext Context;
	FPaper2DPlusCombatDecision Decision;
	TestFalse(TEXT("Null actor decision returns false"), UPaper2DPlusBlueprintLibrary::GetActorCombatDecision(nullptr, Context, Decision));
	TestFalse(TEXT("Null actor decision has no good attack"), Decision.bHasGoodAttack);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatProfileLegacyVariableOverrideReviewTest,
	"Paper2DPlus.CombatProfile.Variables.LegacyDenseSnapshotsPreservedThenReviewedSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatProfileLegacyVariableOverrideReviewTest::RunTest(const FString& Parameters)
{
	const UPaper2DPlusCombatProfileAsset* ClassDefaults = GetDefault<UPaper2DPlusCombatProfileAsset>();
	TestEqual(TEXT("class defaults retain the legacy delta-serialization sentinel"),
		ClassDefaults->VariableOverrideSchemaVersion, static_cast<uint8>(0));
	UPaper2DPlusCombatProfileAsset* Source = CombatProfileTest_MakeCombatProfile();
	TestEqual(TEXT("new assets start on sparse variable inheritance"),
		Source->VariableOverrideSchemaVersion,
		UPaper2DPlusCombatProfileAsset::CurrentVariableOverrideSchemaVersion);
	FPaper2DPlusCombatVariableDefinition Aggression;
	Aggression.VariableTag = Paper2DPlusCombatTags::Var_Aggression;
	Aggression.Type = EPaper2DPlusCombatVariableType::Float;
	Source->VariableDefinitions.Add(Aggression);
	Source->RebuildVariableBags();
	Source->GlobalVariables.FindChecked(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.75f;

	FPaper2DPlusCombatTagDefaults& Defaults = Source->TagDefaults.AddDefaulted_GetRef();
	Defaults.AttackTag = CombatProfileTest_AttackTag();
	Defaults.Variables.FindOrAdd(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.75f;
	FPaper2DPlusCombatAttackOption& Option = Source->AttackOptions.AddDefaulted_GetRef();
	Option.MoveName = TEXT("CloseSlash");
	Option.MoveFlipbook = Source->CharacterProfile->Flipbooks[0].Identity.Flipbook;
	Option.AttackTag = Defaults.AttackTag;
	Option.Variables.FindOrAdd(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.25f;
	FPaper2DPlusCombatScenarioPreset& Preset = Source->ScenarioPresets.AddDefaulted_GetRef();
	Preset.AttackerMove = TEXT("CloseSlash");
	Preset.Context.RuntimeVariables.FindOrAdd(
		Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.25f;
	Source->VariableOverrideSchemaVersion = 0;

	TArray<uint8> LegacyBytes;
	{
		FObjectWriter SaveLegacy(Source, LegacyBytes);
		TestFalse(TEXT("legacy default-delta archive is valid"), SaveLegacy.IsError());
	}
	Source->VariableOverrideSchemaVersion =
		UPaper2DPlusCombatProfileAsset::CurrentVariableOverrideSchemaVersion;
	TArray<uint8> CurrentBytes;
	{
		FObjectWriter SaveCurrent(Source, CurrentBytes);
		TestFalse(TEXT("current default-delta archive is valid"), SaveCurrent.IsError());
	}

	UPaper2DPlusCombatProfileAsset* CurrentLoaded = NewObject<UPaper2DPlusCombatProfileAsset>();
	// FObjectReader does not construct with RF_NeedLoad; seed the same sentinel used by package loading.
	CurrentLoaded->VariableOverrideSchemaVersion = 0;
	{
		FObjectReader LoadCurrent(CurrentLoaded, CurrentBytes);
		TestFalse(TEXT("current default-delta archive loads"), LoadCurrent.IsError());
	}
	TestEqual(TEXT("current schema survives default-delta serialization"),
		CurrentLoaded->VariableOverrideSchemaVersion,
		UPaper2DPlusCombatProfileAsset::CurrentVariableOverrideSchemaVersion);
	CurrentLoaded->PostLoad();
	TestFalse(TEXT("a current reload is not offered legacy inheritance review"),
		CurrentLoaded->HasLegacyDenseVariableOverrides());

	UPaper2DPlusCombatProfileAsset* Loaded = NewObject<UPaper2DPlusCombatProfileAsset>();
	Loaded->VariableOverrideSchemaVersion = 0;
	{
		FObjectReader LoadLegacy(Loaded, LegacyBytes);
		TestFalse(TEXT("legacy default-delta archive loads"), LoadLegacy.IsError());
	}
	TestEqual(TEXT("serialized legacy schema reaches the loaded fixture before PostLoad"),
		Loaded->VariableOverrideSchemaVersion, static_cast<uint8>(0));
	Loaded->PostLoad();
	TestTrue(TEXT("legacy-loaded schema remains explicitly pending review"),
		Loaded->HasLegacyDenseVariableOverrides());
	TestEqual(TEXT("PostLoad preserves a legacy tag snapshot instead of guessing intent"),
		Loaded->TagDefaults[0].Variables.Num(), 1);
	TestEqual(TEXT("PostLoad preserves a behavior-changing move snapshot"),
		Loaded->AttackOptions[0].Variables.Num(), 1);
	TestEqual(TEXT("PostLoad preserves a scenario snapshot"),
		Loaded->ScenarioPresets[0].Context.RuntimeVariables.Num(), 1);

	const int32 Removed = Loaded->AdoptSparseVariableOverrides();
	TestEqual(TEXT("review removes only the tag and scenario rows equal to inherited values"), Removed, 2);
	TestFalse(TEXT("review adopts sparse schema"), Loaded->HasLegacyDenseVariableOverrides());
	TestTrue(TEXT("no-op tag snapshot now inherits dynamically"), Loaded->TagDefaults[0].Variables.IsEmpty());
	TestEqual(TEXT("behavior-changing move override remains explicit"),
		Loaded->AttackOptions[0].Variables.Num(), 1);
	TestTrue(TEXT("no-op scenario snapshot now inherits the move"),
		Loaded->ScenarioPresets[0].Context.RuntimeVariables.IsEmpty());
	Loaded->GlobalVariables.FindChecked(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.9f;
	TestTrue(TEXT("the retained move override still wins after future global edits"),
		FMath::IsNearlyEqual(
			Loaded->AttackOptions[0].Variables.FindChecked(
				Paper2DPlusCombatTags::Var_Aggression).FloatValue,
			0.25f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatProfileVariableInheritanceTest,
	"Paper2DPlus.CombatProfile.Variables.RuntimeMoveTagGlobalInheritance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatProfileVariableInheritanceTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatProfileTest_MakeCombatProfile();
	FPaper2DPlusCombatVariableDefinition Aggression;
	Aggression.VariableTag = Paper2DPlusCombatTags::Var_Aggression;
	Aggression.Type = EPaper2DPlusCombatVariableType::Float;
	CombatProfile->VariableDefinitions.Add(Aggression);
	CombatProfile->RebuildVariableBags();
	CombatProfile->GlobalVariables.FindChecked(
		Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.75f;

	FPaper2DPlusCombatTagDefaults& TagDefaults = CombatProfile->TagDefaults.AddDefaulted_GetRef();
	TagDefaults.AttackTag = CombatProfileTest_AttackTag();
	FPaper2DPlusCombatConsideration& Probe = TagDefaults.Considerations.AddDefaulted_GetRef();
	Probe.ConsiderationName = TEXT("AggressionProbe");
	Probe.Source = EPaper2DPlusCombatConsiderationSource::CustomFloat;
	Probe.VariableTag = Paper2DPlusCombatTags::Var_Aggression;
	Probe.Operation = EPaper2DPlusCombatConsiderationOp::Linear;
	Probe.MinValue = 0.0f;
	Probe.MaxValue = 1.0f;
	Probe.CombineMode = EPaper2DPlusCombatScoreCombineMode::Add;
	CombatProfile->RebuildVariableBags();

	auto ReadProbe = [this, CombatProfile](const FPaper2DPlusCombatRuntimeContext& Context)
	{
		TArray<FPaper2DPlusCombatRankedOption> Ranked;
		CombatProfile->ScoreAttackOptions(Context, Ranked);
		const FPaper2DPlusCombatRankedOption* Close = Ranked.FindByPredicate(
			[](const FPaper2DPlusCombatRankedOption& Row)
			{
				return Row.Attack.MoveName == TEXT("CloseSlash");
			});
		if (!Close)
		{
			AddError(TEXT("Variable inheritance fixture did not score CloseSlash."));
			return -100.0f;
		}
		const FPaper2DPlusCombatScoreTerm* Term = Close->Breakdown.Terms.FindByPredicate(
			[](const FPaper2DPlusCombatScoreTerm& Candidate)
			{
				return Candidate.TermName == TEXT("AggressionProbe");
			});
		if (!Term)
		{
			AddError(TEXT("Variable inheritance fixture emitted no AggressionProbe term."));
			return -100.0f;
		}
		return Term->RawValue;
	};

	FPaper2DPlusCombatRuntimeContext Context;
	Context.DistanceToTarget = 20.0f;
	TestTrue(TEXT("missing tag/move/runtime entries inherit the global value"),
		FMath::IsNearlyEqual(ReadProbe(Context), 0.75f));

	TagDefaults.Variables.FindOrAdd(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.5f;
	TestTrue(TEXT("tag override wins over global"), FMath::IsNearlyEqual(ReadProbe(Context), 0.5f));

	FPaper2DPlusCombatAttackOption& Option = CombatProfile->AttackOptions.AddDefaulted_GetRef();
	Option.MoveName = TEXT("CloseSlash");
	Option.MoveFlipbook = CombatProfile->CharacterProfile->Flipbooks[0].Identity.Flipbook;
	Option.AttackTag = CombatProfileTest_AttackTag();
	CombatProfile->RebuildVariableBags();
	TestTrue(TEXT("empty move override stays sparse and inherits tag"),
		Option.Variables.IsEmpty() && FMath::IsNearlyEqual(ReadProbe(Context), 0.5f));

	Option.Variables.FindOrAdd(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.25f;
	TestTrue(TEXT("move override wins over tag"), FMath::IsNearlyEqual(ReadProbe(Context), 0.25f));

	Context.RuntimeVariables.FindOrAdd(Paper2DPlusCombatTags::Var_Aggression).FloatValue = 0.9f;
	TestTrue(TEXT("runtime override wins over every authored scope"),
		FMath::IsNearlyEqual(ReadProbe(Context), 0.9f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatProfileCanonicalMoveIdentityTest,
	"Paper2DPlus.CombatProfile.MoveIdentity.CanonicalSurvivesRenameAndReorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatProfileCanonicalMoveIdentityTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatProfileTest_MakeCombatProfile();
	FPaper2DPlusCombatAttackOption LegacyOption;
	LegacyOption.MoveName = TEXT("CloseSlash");
	LegacyOption.BaseWeight = 3.5f;
	CombatProfile->AttackOptions.Add(LegacyOption);

	FPaper2DPlusCombatRuntimeContext Context;
	Context.DistanceToTarget = 20.0f;
	TArray<FPaper2DPlusCombatRankedOption> BeforeMigration;
	CombatProfile->ScoreAttackOptions(Context, BeforeMigration);
	const FPaper2DPlusCombatRankedOption* BeforeClose = BeforeMigration.FindByPredicate([](const FPaper2DPlusCombatRankedOption& Row)
	{
		return Row.Attack.MoveName == TEXT("CloseSlash");
	});
	TestNotNull(TEXT("Legacy name-only row scores before migration"), BeforeClose);
	const float ScoreBeforeMigration = BeforeClose ? BeforeClose->Score : -1.0f;

	FRandomStream BeforeRandom(1337);
	FPaper2DPlusCombatDecision BeforeDecision;
	CombatProfile->PickWeightedCombatAttack(Context, BeforeRandom, BeforeDecision);

	TestEqual(TEXT("Unique legacy row is backfilled"), CombatProfile->RefreshAttackOptionMoveBindings(), 1);
	TestFalse(TEXT("Backfill writes canonical soft identity"), CombatProfile->AttackOptions[0].MoveFlipbook.IsNull());
	const FSoftObjectPath CanonicalPath = CombatProfile->AttackOptions[0].MoveFlipbook.ToSoftObjectPath();

	TArray<FPaper2DPlusCombatRankedOption> AfterMigration;
	CombatProfile->ScoreAttackOptions(Context, AfterMigration);
	const FPaper2DPlusCombatRankedOption* AfterClose = AfterMigration.FindByPredicate([](const FPaper2DPlusCombatRankedOption& Row)
	{
		return Row.Attack.MoveName == TEXT("CloseSlash");
	});
	TestNotNull(TEXT("Canonical row scores after migration"), AfterClose);
	if (AfterClose)
	{
		TestTrue(TEXT("Migration preserves score"), FMath::IsNearlyEqual(AfterClose->Score, ScoreBeforeMigration));
		TestTrue(TEXT("Migration preserves authored weight"), FMath::IsNearlyEqual(AfterClose->Attack.BaseWeight, 3.5f));
	}

	FRandomStream AfterRandom(1337);
	FPaper2DPlusCombatDecision AfterDecision;
	CombatProfile->PickWeightedCombatAttack(Context, AfterRandom, AfterDecision);
	TestEqual(TEXT("Deterministic weighted choice is unchanged by identity migration"), AfterDecision.BestMove, BeforeDecision.BestMove);
	TestTrue(TEXT("Deterministic weighted score is unchanged"), FMath::IsNearlyEqual(AfterDecision.BestScore, BeforeDecision.BestScore));

	FFlipbookProfileEntry& RenamedEntry = CombatProfile->CharacterProfile->Flipbooks[0];
	RenamedEntry.Identity.FlipbookName = TEXT("CloseSlashRenamed");
	if (FFlipbookTagMapping* Mapping = CombatProfile->CharacterProfile->TagMappings.Find(CombatProfileTest_AttackTag()))
	{
		for (FFlipbookTagMappingEntry& MappingEntry : Mapping->Entries)
		{
			if (MappingEntry.FlipbookName.Equals(TEXT("CloseSlash"), ESearchCase::IgnoreCase))
			{
				MappingEntry.FlipbookName = TEXT("CloseSlashRenamed");
			}
		}
	}
	CombatProfile->CharacterProfile->Flipbooks.Swap(0, 3);

	TestEqual(TEXT("Rename refresh changes one cached display name"), CombatProfile->RefreshAttackOptionMoveBindings(), 1);
	TestEqual(TEXT("Canonical path survives rename/reorder"), CombatProfile->AttackOptions[0].MoveFlipbook.ToSoftObjectPath().ToString(), CanonicalPath.ToString());
	TestEqual(TEXT("Cached display name follows the canonical move"), CombatProfile->AttackOptions[0].MoveName, FName(TEXT("CloseSlashRenamed")));

	TArray<FPaper2DPlusCombatAttackDerivedData> RenamedCatalog;
	CombatProfile->BuildAttackCatalog(RenamedCatalog);
	const FPaper2DPlusCombatAttackDerivedData* Renamed = RenamedCatalog.FindByPredicate([](const FPaper2DPlusCombatAttackDerivedData& Row)
	{
		return Row.MoveName == TEXT("CloseSlashRenamed");
	});
	TestNotNull(TEXT("Catalog follows canonical identity after rename"), Renamed);
	if (Renamed)
	{
		TestTrue(TEXT("Renamed move keeps tactical weight"), FMath::IsNearlyEqual(Renamed->BaseWeight, 3.5f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatProfileMoveIdentitySerializationTest,
	"Paper2DPlus.CombatProfile.MoveIdentity.SerializationAndPostLoadRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatProfileMoveIdentitySerializationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* Saved = CombatProfileTest_MakeCombatProfile();
	FPaper2DPlusCombatAttackOption Option;
	Option.MoveName = TEXT("CloseSlash");
	Option.BaseWeight = 4.25f;
	Saved->AttackOptions.Add(Option);
	TestEqual(TEXT("Pre-save migration binds the unique move"), Saved->RefreshAttackOptionMoveBindings(), 1);
	if (Saved->AttackOptions.Num() != 1 || Saved->AttackOptions[0].MoveFlipbook.IsNull())
	{
		AddError(TEXT("Pre-save fixture did not acquire a canonical MoveFlipbook."));
		return false;
	}

	const FSoftObjectPath SavedIdentity = Saved->AttackOptions[0].MoveFlipbook.ToSoftObjectPath();
	FPaper2DPlusCombatRuntimeContext Context;
	Context.DistanceToTarget = 20.0f;
	TArray<FPaper2DPlusCombatRankedOption> BeforeRows;
	Saved->ScoreAttackOptions(Context, BeforeRows);
	const FPaper2DPlusCombatRankedOption* Before = BeforeRows.FindByPredicate([](const FPaper2DPlusCombatRankedOption& Row)
	{
		return Row.Attack.MoveName == TEXT("CloseSlash");
	});
	TestNotNull(TEXT("Saved fixture scores before serialization"), Before);
	const float BeforeScore = Before ? Before->Score : -1.0f;

	TArray<uint8> SerializedBytes;
	FObjectWriter Writer(Saved, SerializedBytes);
	TestTrue(TEXT("Object serialization produced bytes"), SerializedBytes.Num() > 0);

	Saved->CharacterProfile->Flipbooks[0].Identity.FlipbookName = TEXT("CloseSlashReloaded");
	if (FFlipbookTagMapping* Mapping = Saved->CharacterProfile->TagMappings.Find(CombatProfileTest_AttackTag()))
	{
		for (FFlipbookTagMappingEntry& MappingEntry : Mapping->Entries)
		{
			if (MappingEntry.FlipbookName.Equals(TEXT("CloseSlash"), ESearchCase::IgnoreCase))
			{
				MappingEntry.FlipbookName = TEXT("CloseSlashReloaded");
			}
		}
	}
	Saved->CharacterProfile->Flipbooks.Swap(0, 2);

	UPaper2DPlusCombatProfileAsset* Loaded = NewObject<UPaper2DPlusCombatProfileAsset>();
	FObjectReader Reader(Loaded, SerializedBytes);
	if (Loaded->AttackOptions.Num() != 1)
	{
		AddError(TEXT("Serialized Combat option did not deserialize."));
		return false;
	}
	TestEqual(TEXT("Canonical soft path survives serialization"), Loaded->AttackOptions[0].MoveFlipbook.ToSoftObjectPath().ToString(), SavedIdentity.ToString());
	TestEqual(TEXT("Serialized cached name is the pre-rename value"), Loaded->AttackOptions[0].MoveName, FName(TEXT("CloseSlash")));

	Loaded->PostLoad();
	TestEqual(TEXT("PostLoad follows renamed canonical flipbook"), Loaded->AttackOptions[0].MoveName, FName(TEXT("CloseSlashReloaded")));
	TestEqual(TEXT("PostLoad migration is idempotent"), Loaded->RefreshAttackOptionMoveBindings(), 0);

	TArray<FPaper2DPlusCombatRankedOption> AfterRows;
	Loaded->ScoreAttackOptions(Context, AfterRows);
	const FPaper2DPlusCombatRankedOption* After = AfterRows.FindByPredicate([](const FPaper2DPlusCombatRankedOption& Row)
	{
		return Row.Attack.MoveName == TEXT("CloseSlashReloaded");
	});
	TestNotNull(TEXT("Reloaded canonical move still scores"), After);
	if (After)
	{
		TestTrue(TEXT("Reload preserves score semantics"), FMath::IsNearlyEqual(After->Score, BeforeScore));
		TestTrue(TEXT("Reload preserves authored tactical weight"), FMath::IsNearlyEqual(After->Attack.BaseWeight, 4.25f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCombatProfileLegacyMoveAmbiguityTest,
	"Paper2DPlus.CombatProfile.MoveIdentity.AmbiguousAndDanglingStayUnbound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCombatProfileLegacyMoveAmbiguityTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCombatProfileAsset* CombatProfile = CombatProfileTest_MakeCombatProfile();
	FFlipbookProfileEntry Duplicate = CombatProfile->CharacterProfile->Flipbooks[0];
	Duplicate.Identity.Flipbook = NewObject<UPaperFlipbook>(CombatProfile->CharacterProfile, TEXT("DuplicateCloseSlashFlipbook"));
	CombatProfile->CharacterProfile->Flipbooks.Add(Duplicate);

	FPaper2DPlusCombatAttackOption Ambiguous;
	Ambiguous.MoveName = TEXT("CloseSlash");
	CombatProfile->AttackOptions.Add(Ambiguous);
	TestEqual(TEXT("Ambiguous legacy name is not backfilled"), CombatProfile->RefreshAttackOptionMoveBindings(), 0);
	TestTrue(TEXT("Ambiguous legacy row remains name-only"), CombatProfile->AttackOptions[0].MoveFlipbook.IsNull());

	TArray<FPaper2DPlusCombatValidationIssue> Issues;
	TestFalse(TEXT("Ambiguous legacy row is invalid"), CombatProfile->ValidateCombatProfileAsset(Issues));
	TestTrue(TEXT("Ambiguity reports exact MoveName context"), Issues.ContainsByPredicate([](const FPaper2DPlusCombatValidationIssue& Issue)
	{
		return Issue.Field == TEXT("MoveName") && Issue.MoveName == TEXT("CloseSlash")
			&& Issue.Message.ToString().Contains(TEXT("more than one"));
	}));

	CombatProfile->AttackOptions.Reset();
	FPaper2DPlusCombatAttackOption DanglingCanonical;
	DanglingCanonical.MoveName = TEXT("DeletedMove");
	DanglingCanonical.MoveFlipbook = TSoftObjectPtr<UPaperFlipbook>(FSoftObjectPath(TEXT("/Game/Deleted/FB_Deleted.FB_Deleted")));
	CombatProfile->AttackOptions.Add(DanglingCanonical);
	TestNull(TEXT("Dangling canonical object starts unloaded"), CombatProfile->AttackOptions[0].MoveFlipbook.Get());
	Issues.Reset();
	TestFalse(TEXT("Dangling canonical row is invalid"), CombatProfile->ValidateCombatProfileAsset(Issues));
	TestTrue(TEXT("Dangling canonical reports exact object field"), Issues.ContainsByPredicate([](const FPaper2DPlusCombatValidationIssue& Issue)
	{
		return Issue.Field == TEXT("MoveFlipbook") && Issue.MoveName == TEXT("DeletedMove")
			&& Issue.Message.ToString().Contains(TEXT("FB_Deleted"));
	}));
	TestNull(TEXT("Validation never loads dangling canonical object"), CombatProfile->AttackOptions[0].MoveFlipbook.Get());
	return true;
}

#endif // WITH_EDITOR
