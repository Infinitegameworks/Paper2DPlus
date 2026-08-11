// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalogAuditService.h"

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/PackageName.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "PaperFlipbook.h"
#include "ProfileRelationshipService.h"
#include "UObject/Package.h"

namespace Paper2DPlusCatalogValidationTests
{
	template <typename AssetType>
	AssetType* NewAsset(const TCHAR* PackageName)
	{
		static int32 Sequence = 0;
		const FString UniquePackageName = FString::Printf(TEXT("%s_Run%d"), PackageName, ++Sequence);
		UPackage* Package = CreatePackage(*UniquePackageName);
		const FString AssetName = FPackageName::GetLongPackageAssetName(UniquePackageName);
		AssetType* Asset = NewObject<AssetType>(
			Package,
			*AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		Package->SetDirtyFlag(false);
		return Asset;
	}

	FAssetData MakeAssetData(
		const UObject* Asset,
		bool bIncludeRelationshipTag = false,
		const FSoftObjectPath& RelationshipPath = FSoftObjectPath())
	{
		check(Asset);
		const FString PackageName = Asset->GetOutermost()->GetName();
		const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);
		FAssetDataTagMap Tags;
		if (bIncludeRelationshipTag)
		{
			Tags.Add(
				FProfileRelationshipService::CharacterProfileRelationshipTag(),
				RelationshipPath.IsNull() ? FString(TEXT("None")) : RelationshipPath.ToString());
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return FAssetData(
			FName(*PackageName),
			FName(*PackagePath),
			Asset->GetFName(),
			Asset->GetClass()->GetFName(),
			MoveTemp(Tags));
#else
		return FAssetData(
			FName(*PackageName),
			FName(*PackagePath),
			Asset->GetFName(),
			Asset->GetClass()->GetClassPathName(),
			MoveTemp(Tags));
#endif
	}

	/** The snapshot now carries only the project authority; discovery content roots are gone. */
	FPaper2DPlusCharacterCatalogSettingsSnapshot MakeSettings(
		UPaper2DPlusCharacterCatalogAsset* Catalog)
	{
		FPaper2DPlusCharacterCatalogSettingsSnapshot Settings;
		Settings.DefaultCatalog = Catalog;
		return Settings;
	}

	FPaper2DPlusCharacterCatalogEntry MakeEntry(UPaper2DPlusCharacterProfileAsset* Profile)
	{
		FPaper2DPlusCharacterCatalogEntry Entry;
		Entry.CharacterProfile = Profile;
		return Entry;
	}

	/** Saved entry whose Character Profile is addressed by path only: no object, no registry row. */
	FPaper2DPlusCharacterCatalogEntry MakeEntryForMissingCharacter(const FSoftObjectPath& CharacterPath)
	{
		FPaper2DPlusCharacterCatalogEntry Entry;
		Entry.CharacterProfile = TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(CharacterPath);
		return Entry;
	}

	FCharacterCatalogAuditService::FNativeAssetResolver MakeResolver(
		const TArray<UObject*>& Assets,
		int32* ResolveCount = nullptr)
	{
		TMap<FString, UObject*> ByPath;
		for (UObject* Asset : Assets)
		{
			if (Asset)
			{
				ByPath.Add(FProfileRelationshipService::NormalizeObjectPath(FSoftObjectPath(Asset)), Asset);
			}
		}
		return [ByPath = MoveTemp(ByPath), ResolveCount](const FSoftObjectPath& Path) -> UObject*
		{
			if (ResolveCount)
			{
				++(*ResolveCount);
			}
			return ByPath.FindRef(FProfileRelationshipService::NormalizeObjectPath(Path));
		};
	}

	FCharacterCatalogAuditService::FCookRegistrationInspector ReadyCookInspector(int32* InspectCount = nullptr)
	{
		return [InspectCount](const FSoftObjectPath&) -> FPaper2DPlusCharacterCatalogCookInspection
		{
			if (InspectCount)
			{
				++(*InspectCount);
			}
			FPaper2DPlusCharacterCatalogCookInspection Result;
			Result.bTypeRegistered = true;
			Result.bCatalogPathRegistered = true;
			Result.RegisteredPrimaryAssetType =
				UPaper2DPlusCharacterCatalogAsset::CharacterCatalogPrimaryAssetType().GetName();
			return Result;
		};
	}

	FPaper2DPlusCharacterCatalogAuditReport Audit(
		UPaper2DPlusCharacterCatalogAsset* Catalog,
		const TArray<FAssetData>& Assets,
		const FPaper2DPlusCharacterCatalogSettingsSnapshot& Settings,
		const TArray<UObject*>& ResolverAssets,
		const FCharacterCatalogAuditService::FCookRegistrationInspector& CookInspector = ReadyCookInspector())
	{
		return FCharacterCatalogAuditService::BuildReport(
			Catalog,
			Assets,
			Settings,
			MakeResolver(ResolverAssets),
			CookInspector);
	}

	const FPaper2DPlusValidationIssue* FindCode(
		const FPaper2DPlusCharacterCatalogAuditReport& Report,
		FName Code)
	{
		return Report.Issues.FindByPredicate([Code](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code == Code;
		});
	}

	bool HasCodeWithSeverity(
		const FPaper2DPlusCharacterCatalogAuditReport& Report,
		FName Code,
		EPaper2DPlusValidationSeverity Severity)
	{
		return Report.Issues.ContainsByPredicate([Code, Severity](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code == Code && Issue.Severity == Severity;
		});
	}

	void ConfigureValidLayer(
		UPaper2DPlusCharacterLayerAsset* Layer,
		UPaper2DPlusCharacterProfileAsset* Profile)
	{
		Layer->BaseProfile = Profile;
		Layer->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		FCharacterLayer& SourceLayer = Layer->Layers.AddDefaulted_GetRef();
		SourceLayer.LayerId = FGuid::NewGuid();
		SourceLayer.LayerName = TEXT("Default Layer");
		FCharacterLayerAppearancePreset& Default = Layer->AppearancePresets.AddDefaulted_GetRef();
		Default.PresetId = FGuid::NewGuid();
		Default.DisplayName = TEXT("Default Appearance");
		Default.ActiveLayerIds.Add(SourceLayer.LayerId);
		Layer->DefaultAppearancePresetId = Default.PresetId;
#if WITH_EDITORONLY_DATA
		Layer->BakeSetId = FGuid::NewGuid();
#endif
	}

	void CompareStableKeys(
		FAutomationTestBase& Test,
		const FPaper2DPlusCharacterCatalogAuditReport& A,
		const FPaper2DPlusCharacterCatalogAuditReport& B)
	{
		Test.TestEqual(TEXT("Stable report issue count"), A.Issues.Num(), B.Issues.Num());
		const int32 SharedCount = FMath::Min(A.Issues.Num(), B.Issues.Num());
		for (int32 Index = 0; Index < SharedCount; ++Index)
		{
			Test.TestEqual(
				FString::Printf(TEXT("Stable issue identity %d"), Index),
				A.Issues[Index].StableKey,
				B.Issues[Index].StableKey);
		}
	}
}

namespace Paper2DPlusCatalogValidationTests
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogRequiredErrorAuditTest,
	"Paper2DPlus.CharacterCatalog.Validation.RequiredErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogRequiredErrorAuditTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U27/ErrorCatalog"));
	UPaper2DPlusCharacterProfileAsset* MissingLayer = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_A_MissingLayer"));
	UPaper2DPlusCharacterProfileAsset* CombatMismatch = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_B_CombatMismatch"));
	UPaper2DPlusCharacterProfileAsset* ManualMismatch = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_C_ManualMismatch"));
	UPaper2DPlusCharacterProfileAsset* MissingEffect = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_D_MissingEffect"));
	UPaper2DPlusCharacterProfileAsset* DeletedEffect = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_E_DeletedEffect"));
	UPaper2DPlusCharacterProfileAsset* OtherCharacter = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Outside/U27_OtherCharacter"));

	UPaper2DPlusCharacterLayerAsset* WrongLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/U27_WrongLayer"));
	ConfigureValidLayer(WrongLayer, OtherCharacter);
	UPaper2DPlusCombatProfileAsset* WrongCombat = NewAsset<UPaper2DPlusCombatProfileAsset>(
		TEXT("/Game/Combat/U27_WrongCombat"));
	WrongCombat->CharacterProfile = OtherCharacter;

	FPaper2DPlusCharacterCatalogEntry EntryA = MakeEntry(MissingLayer);
	EntryA.Requirements.bRequireLayer = true;
	FPaper2DPlusCharacterCatalogEntry EntryB = MakeEntry(CombatMismatch);
	EntryB.CombatProfile = WrongCombat;
	FPaper2DPlusCharacterCatalogEntry EntryC = MakeEntry(ManualMismatch);
	EntryC.LayerProfile = WrongLayer;
	FPaper2DPlusCharacterCatalogEntry EntryD = MakeEntry(MissingEffect);
	EntryD.Requirements.bRequireEffect = true;
	FPaper2DPlusCharacterCatalogEntry EntryE = MakeEntry(DeletedEffect);
	const FSoftObjectPath DeletedEffectPath(TEXT("/Game/Effects/U27_Deleted.U27_Deleted"));
	EntryE.EffectProfile = TSoftObjectPtr<UPaper2DPlusEffectProfileAsset>(DeletedEffectPath);
	const FSoftObjectPath AbsentCharacterPath(
		TEXT("/Game/Characters/U27_G_AbsentCharacter.U27_G_AbsentCharacter"));
	const FPaper2DPlusCharacterCatalogEntry EntryG = MakeEntryForMissingCharacter(AbsentCharacterPath);
	Catalog->Entries = { EntryA, EntryA, EntryB, EntryC, EntryD, EntryE, EntryG };

	FPaper2DPlusCharacterCatalogGroup DuplicateGroupA;
	DuplicateGroupA.GroupName = TEXT("DuplicateGroup");
	FPaper2DPlusCharacterCatalogGroup DuplicateGroupB;
	DuplicateGroupB.GroupName = TEXT("DuplicateGroup");
	Catalog->Groups = { DuplicateGroupA, DuplicateGroupB };

	// The absent character deliberately has no registry row; every other saved character has one.
	TArray<FAssetData> Assets = {
		MakeAssetData(DeletedEffect),
		MakeAssetData(MissingEffect),
		MakeAssetData(ManualMismatch),
		MakeAssetData(CombatMismatch),
		MakeAssetData(MissingLayer),
		MakeAssetData(WrongLayer, true, FSoftObjectPath(OtherCharacter)),
		MakeAssetData(WrongCombat, true, FSoftObjectPath(OtherCharacter))
	};
	const TArray<UObject*> ResolverAssets = {
		MissingLayer, CombatMismatch, ManualMismatch, MissingEffect, DeletedEffect,
		OtherCharacter, WrongLayer, WrongCombat
	};
	const FPaper2DPlusCharacterCatalogSettingsSnapshot Settings = MakeSettings(Catalog);
	const auto MissingCookInspector = [](const FSoftObjectPath&)
	{
		return FPaper2DPlusCharacterCatalogCookInspection();
	};

	const FPaper2DPlusCharacterCatalogAuditReport Report = Audit(
		Catalog, Assets, Settings, ResolverAssets, MissingCookInspector);

	TestTrue(TEXT("Missing required companion is Error"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Entry.MissingRequiredCompanion"), EPaper2DPlusValidationSeverity::Error));
	TestTrue(TEXT("Assigned Combat pointing at another Character is Error"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Relationship.Combat.ManualMismatch"), EPaper2DPlusValidationSeverity::Error));
	TestTrue(TEXT("Duplicate Character entry is Error"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Entry.DuplicateCharacter"), EPaper2DPlusValidationSeverity::Error));
	TestTrue(TEXT("Duplicate group is Error"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Group.DuplicateName"), EPaper2DPlusValidationSeverity::Error));
	TestTrue(TEXT("Applicable Manual mismatch is Error"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Relationship.Layer.ManualMismatch"), EPaper2DPlusValidationSeverity::Error));
	TestTrue(TEXT("Deleted Catalog-owned Effect assignment is Error"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Effect.DeletedAssignment"), EPaper2DPlusValidationSeverity::Error));
	TestTrue(TEXT("Missing cook registration is Error"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Cook.MissingRegistration"), EPaper2DPlusValidationSeverity::Error));

	const FPaper2DPlusValidationIssue* MissingCharacterAsset = FindCode(
		Report, TEXT("Paper2DPlus.Catalog.Entry.MissingCharacterAsset"));
	TestNotNull(TEXT("Saved character absent from the registry emits an issue"), MissingCharacterAsset);
	if (MissingCharacterAsset)
	{
		TestEqual(TEXT("Missing character asset is Error"),
			MissingCharacterAsset->Severity, EPaper2DPlusValidationSeverity::Error);
		TestEqual(TEXT("Missing character asset names the saved character"),
			MissingCharacterAsset->CharacterPath, AbsentCharacterPath);
		TestEqual(TEXT("Missing character asset scope is retained"),
			MissingCharacterAsset->Scope, FName(TEXT("Entry")));
		TestEqual(TEXT("Missing character asset field is retained"),
			MissingCharacterAsset->Field, FName(TEXT("CharacterProfile")));
		TestEqual(TEXT("Missing character asset repair targets the Catalog"),
			MissingCharacterAsset->GetNavigationAssetPath(), FSoftObjectPath(Catalog));
	}
	TestNotNull(TEXT("Absent character still projects a row"),
		Report.FindRow(AbsentCharacterPath));
	TestFalse(TEXT("Present characters raise no missing-asset Error"), Report.Issues.ContainsByPredicate(
		[&AbsentCharacterPath](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code == TEXT("Paper2DPlus.Catalog.Entry.MissingCharacterAsset")
				&& Issue.CharacterPath != AbsentCharacterPath;
		}));

	TArray<FAssetData> ReversedAssets = Assets;
	Algo::Reverse(ReversedAssets);
	const FPaper2DPlusCharacterCatalogAuditReport ReversedReport = Audit(
		Catalog, ReversedAssets, Settings, ResolverAssets, MissingCookInspector);
	CompareStableKeys(*this, Report, ReversedReport);

	UPaper2DPlusCharacterCatalogAsset* OtherCatalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U27/OtherAuthority"));
	FPaper2DPlusCharacterCatalogSettingsSnapshot WrongAuthority = Settings;
	WrongAuthority.DefaultCatalog = OtherCatalog;
	const FPaper2DPlusCharacterCatalogAuditReport AuthorityReport = Audit(
		Catalog, Assets, WrongAuthority, ResolverAssets, ReadyCookInspector());
	TestTrue(TEXT("Invalid project authority is Error"), HasCodeWithSeverity(
		AuthorityReport, TEXT("Paper2DPlus.Catalog.Settings.InvalidAuthority"), EPaper2DPlusValidationSeverity::Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogOptionalWarningAuditTest,
	"Paper2DPlus.CharacterCatalog.Validation.OptionalWarnings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogOptionalWarningAuditTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U27/WarningCatalog"));
	UPaper2DPlusCharacterProfileAsset* Profile = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_WarningCharacter"));
	UPaper2DPlusCharacterLayerAsset* LayerA = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/U27_OptionalLayerA"));
	UPaper2DPlusCharacterLayerAsset* LayerB = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/U27_OptionalLayerB"));
	ConfigureValidLayer(LayerA, Profile);
	ConfigureValidLayer(LayerB, Profile);
	Catalog->Entries.Add(MakeEntry(Profile));

	FPaper2DPlusCharacterCatalogGroup Group;
	Group.GroupName = TEXT("Warnings");
	Group.Members.Add(TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>());
	Group.Members.Add(Profile);
	Group.Members.Add(Profile);
	Group.Members.Add(TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(
		FSoftObjectPath(TEXT("/Game/Characters/U27_Outside.U27_Outside"))));
	Catalog->Groups.Add(Group);

	const TArray<FAssetData> Assets = {
		MakeAssetData(Profile),
		MakeAssetData(LayerB, true, FSoftObjectPath(Profile)),
		MakeAssetData(LayerA, true, FSoftObjectPath(Profile))
	};
	const FPaper2DPlusCharacterCatalogAuditReport Report = Audit(
		Catalog,
		Assets,
		MakeSettings(Catalog),
		{ Profile, LayerA, LayerB });

	// Two inward-tagged Layers with an EMPTY, OPTIONAL Layer slot. Nothing auto-assigns a companion any
	// more, but "several assets claim this Character" is still a fact the designer needs: it is the one
	// state where picking the wrong companion is easy and silent. Optional slot, so Warning.
	TestTrue(TEXT("Two claimants on an empty optional slot is a Warning"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Relationship.Layer.Ambiguous"), EPaper2DPlusValidationSeverity::Warning));
	TestTrue(TEXT("Empty group member is Warning"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Group.MissingMember"), EPaper2DPlusValidationSeverity::Warning));
	TestTrue(TEXT("Repeated group member is Warning"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Group.DuplicateMember"), EPaper2DPlusValidationSeverity::Warning));
	TestTrue(TEXT("Out-of-Catalog member is Warning"), HasCodeWithSeverity(
		Report, TEXT("Paper2DPlus.Catalog.Group.OutOfCatalogMember"), EPaper2DPlusValidationSeverity::Warning));
	TestEqual(TEXT("Warning-only fixture has no Errors"), Report.Summary.Issues.NumErrors, 0);
	TestFalse(TEXT("Absent optional Combat remains neutral"), Report.Issues.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Field == TEXT("CombatProfile");
		}));
	TestFalse(TEXT("Absent optional Effect remains neutral"), Report.Issues.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Field == TEXT("EffectProfile");
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogAuditSummaryTest,
	"Paper2DPlus.CharacterCatalog.Validation.SummaryCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogAuditSummaryTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U27/SummaryCatalog"));
	UPaper2DPlusCharacterProfileAsset* Clean = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_A_Clean"));
	UPaper2DPlusCharacterProfileAsset* Incomplete = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_B_Incomplete"));
	UPaper2DPlusCharacterProfileAsset* Warning = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_C_Warning"));
	UPaper2DPlusCharacterProfileAsset* Mismatch = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_D_Mismatch"));
	UPaper2DPlusCharacterProfileAsset* Outside = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Outside/U27_SummaryOutside"));

	UPaper2DPlusCharacterLayerAsset* WrongLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/U27_SummaryMismatch"));
	ConfigureValidLayer(WrongLayer, Outside);

	FPaper2DPlusCharacterCatalogEntry CleanEntry = MakeEntry(Clean);
	FPaper2DPlusCharacterCatalogEntry IncompleteEntry = MakeEntry(Incomplete);
	IncompleteEntry.Requirements.bRequireEffect = true;
	FPaper2DPlusCharacterCatalogEntry WarningEntry = MakeEntry(Warning);
	FPaper2DPlusCharacterCatalogEntry MismatchEntry = MakeEntry(Mismatch);
	MismatchEntry.LayerProfile = WrongLayer;
	Catalog->Entries = { CleanEntry, IncompleteEntry, WarningEntry, MismatchEntry };

	// A repeated ordered membership is the row-scoped Warning source: its issue carries the member's
	// Character path, so exactly the Warning row must count as a warning row.
	FPaper2DPlusCharacterCatalogGroup Group;
	Group.GroupName = TEXT("SummaryGroup");
	Group.Members.Add(Warning);
	Group.Members.Add(Warning);
	Catalog->Groups.Add(Group);

	const TArray<FAssetData> Assets = {
		MakeAssetData(Clean), MakeAssetData(Incomplete), MakeAssetData(Warning), MakeAssetData(Mismatch),
		MakeAssetData(WrongLayer, true, FSoftObjectPath(Outside))
	};
	const FPaper2DPlusCharacterCatalogAuditReport Report = Audit(
		Catalog,
		Assets,
		MakeSettings(Catalog),
		{ Clean, Incomplete, Warning, Mismatch, Outside, WrongLayer });

	int32 CountedInfo = 0;
	int32 CountedWarnings = 0;
	int32 CountedErrors = 0;
	for (const FPaper2DPlusValidationIssue& Issue : Report.Issues)
	{
		if (Issue.Severity == EPaper2DPlusValidationSeverity::Error) ++CountedErrors;
		else if (Issue.Severity == EPaper2DPlusValidationSeverity::Warning) ++CountedWarnings;
		else ++CountedInfo;
	}
	TestEqual(TEXT("Summary Character count"), Report.Summary.NumCharacters, 4);
	TestEqual(TEXT("Pure runtime completion count"), Report.Summary.NumRuntimeComplete, 3);
	TestEqual(TEXT("Audit-complete count accepts warnings but not Errors"), Report.Summary.NumAuditComplete, 2);
	TestEqual(TEXT("Rows with warnings"), Report.Summary.NumRowsWithWarnings, 1);
	TestEqual(TEXT("Rows with Errors"), Report.Summary.NumRowsWithErrors, 2);
	TestEqual(TEXT("Summary Info count derives from report"), Report.Summary.Issues.NumInfo, CountedInfo);
	TestEqual(TEXT("Summary Warning count derives from report"), Report.Summary.Issues.NumWarnings, CountedWarnings);
	TestEqual(TEXT("Summary Error count derives from report"), Report.Summary.Issues.NumErrors, CountedErrors);

	const FPaper2DPlusCharacterCatalogAuditRow* WarningRow = Report.FindRow(FSoftObjectPath(Warning));
	TestNotNull(TEXT("Warning row is projected"), WarningRow);
	if (WarningRow)
	{
		TestTrue(TEXT("Warning row owns the group warning"), WarningRow->IssueSummary.HasWarnings());
		TestFalse(TEXT("Warning row owns no Error"), WarningRow->IssueSummary.HasErrors());
		TestTrue(TEXT("Warning row remains audit-complete"), WarningRow->bAuditComplete);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogNativeIssueAuditTest,
	"Paper2DPlus.CharacterCatalog.Validation.NativeIssueProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogNativeIssueAuditTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U27/NativeCatalog"));
	UPaper2DPlusCharacterProfileAsset* Profile = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_NativeCharacter"));
	UPaper2DPlusCharacterProfileAsset* Other = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Outside/U27_NativeOther"));
	FFlipbookProfileEntry First;
	First.Identity.FlipbookName = TEXT("DuplicateMove");
	FFlipbookProfileEntry Second;
	Second.Identity.FlipbookName = TEXT("DuplicateMove");
	Profile->Flipbooks = { First, Second };

	UPaper2DPlusCharacterLayerAsset* Layer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/U27_NativeLayer"));
	ConfigureValidLayer(Layer, Profile);
	Layer->DefaultAppearancePresetId.Invalidate();
	UPaper2DPlusEffectProfileAsset* Effect = NewAsset<UPaper2DPlusEffectProfileAsset>(
		TEXT("/Game/Effects/U27_NativeEffect"));
	Effect->CharacterProfile = Other; // Retained transition data is deliberately irrelevant to Catalog audit.
	Effect->Effects.AddDefaulted();
	UPaper2DPlusCombatProfileAsset* Combat = NewAsset<UPaper2DPlusCombatProfileAsset>(
		TEXT("/Game/Combat/U27_NativeCombat"));
	Combat->CharacterProfile = nullptr;

	FPaper2DPlusCharacterCatalogEntry Entry = MakeEntry(Profile);
	Entry.LayerProfile = Layer;
	Entry.EffectProfile = Effect;
	Entry.CombatProfile = Combat;
	Catalog->Entries.Add(Entry);

	const TArray<FAssetData> Assets = {
		MakeAssetData(Profile),
		MakeAssetData(Layer, true, FSoftObjectPath(Profile)),
		MakeAssetData(Effect),
		MakeAssetData(Combat, true, FSoftObjectPath(Profile))
	};
	const FPaper2DPlusCharacterCatalogAuditReport Report = Audit(
		Catalog,
		Assets,
		MakeSettings(Catalog),
		{ Profile, Other, Layer, Effect, Combat });

	TArray<FCharacterProfileValidationIssue> CharacterNative;
	Profile->ValidateCharacterProfileAsset(CharacterNative);
	const FCharacterProfileValidationIssue* CharacterSource = CharacterNative.FindByPredicate(
		[](const FCharacterProfileValidationIssue& Issue)
		{
			return Issue.Message.Contains(TEXT("Duplicate flipbook name"));
		});
	const TArray<FCharacterLayerValidationIssue> LayerNative = Layer->ValidateLayerAsset();
	const FCharacterLayerValidationIssue* LayerSource = LayerNative.FindByPredicate(
		[](const FCharacterLayerValidationIssue& Issue)
		{
			return Issue.Message.Contains(TEXT("Default Appearance"));
		});
	TArray<FPaper2DPlusEffectProfileValidationIssue> EffectNative;
	Effect->ValidateEffectProfileAsset(EffectNative);
	TArray<FPaper2DPlusCombatValidationIssue> CombatNative;
	Combat->ValidateCombatProfileAsset(CombatNative);

	TestNotNull(TEXT("Character native fixture emits an issue"), CharacterSource);
	TestNotNull(TEXT("Layer native fixture emits an issue"), LayerSource);
	TestTrue(TEXT("Effect native fixture emits an issue"), !EffectNative.IsEmpty());
	TestTrue(TEXT("Combat native fixture emits an issue"), !CombatNative.IsEmpty());

	const auto Preserved = [&Report, Profile](const FSoftObjectPath& AssetPath, const FText& Message, EPaper2DPlusValidationSeverity Severity)
	{
		return Report.Issues.ContainsByPredicate([&AssetPath, &Message, Severity, Profile](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.AssetPath == AssetPath
				&& Issue.CharacterPath == FSoftObjectPath(Profile)
				&& Issue.Severity == Severity
				&& Issue.Message.EqualTo(Message);
		});
	};
	if (CharacterSource)
	{
		TestTrue(TEXT("Character severity/message is preserved with Catalog context"), Preserved(
			FSoftObjectPath(Profile), FText::FromString(CharacterSource->Message), EPaper2DPlusValidationSeverity::Error));
	}
	if (LayerSource)
	{
		TestTrue(TEXT("Layer severity/message is preserved with Catalog context"), Preserved(
			FSoftObjectPath(Layer), FText::FromString(LayerSource->Message), EPaper2DPlusValidationSeverity::Error));
	}
	if (!EffectNative.IsEmpty())
	{
		TestTrue(TEXT("Effect severity/message is preserved with Catalog context"), Preserved(
			FSoftObjectPath(Effect), EffectNative[0].Message, EPaper2DPlusValidationSeverity::Error));
	}
	if (!CombatNative.IsEmpty())
	{
		TestTrue(TEXT("Combat severity/message is preserved with Catalog context"), Preserved(
			FSoftObjectPath(Combat), CombatNative[0].Message, EPaper2DPlusValidationSeverity::Error));
	}
	TestFalse(TEXT("Audit invents no Effect inward-link mismatch code"), Report.Issues.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code.ToString().StartsWith(TEXT("Paper2DPlus.Catalog.Relationship.Effect"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogDeletedReferenceAuditTest,
	"Paper2DPlus.CharacterCatalog.Validation.DeletedReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogDeletedReferenceAuditTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U27/DeletedCatalog"));
	UPaper2DPlusCharacterProfileAsset* Profile = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_DeletedCharacter"));
	const FSoftObjectPath MissingLayerPath(TEXT("/Game/Layers/U27_DeletedLayer.U27_DeletedLayer"));
	const FSoftObjectPath MissingEffectPath(TEXT("/Game/Effects/U27_DeletedEffect.U27_DeletedEffect"));
	const FSoftObjectPath MissingMemberPath(TEXT("/Game/Characters/U27_DeletedMember.U27_DeletedMember"));

	FPaper2DPlusCharacterCatalogEntry Entry = MakeEntry(Profile);
	Entry.LayerProfile = TSoftObjectPtr<UPaper2DPlusCharacterLayerAsset>(MissingLayerPath);
	Entry.EffectProfile = TSoftObjectPtr<UPaper2DPlusEffectProfileAsset>(MissingEffectPath);
	Catalog->Entries.Add(Entry);
	FPaper2DPlusCharacterCatalogGroup Group;
	Group.GroupName = TEXT("Survivors");
	Group.Members.Add(Profile);
	Group.Members.Add(TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(MissingMemberPath));
	Catalog->Groups.Add(Group);

	const TArray<FAssetData> Assets = { MakeAssetData(Profile) };
	const FPaper2DPlusCharacterCatalogSettingsSnapshot Settings = MakeSettings(Catalog);
	const FPaper2DPlusCharacterCatalogAuditReport First = Audit(
		Catalog, Assets, Settings, { Profile });
	const FPaper2DPlusCharacterCatalogAuditReport Second = Audit(
		Catalog, Assets, Settings, { Profile });

	const FPaper2DPlusValidationIssue* LayerIssue = FindCode(
		First, TEXT("Paper2DPlus.Catalog.Relationship.Layer.ManualMissing"));
	const FPaper2DPlusValidationIssue* EffectIssue = FindCode(
		First, TEXT("Paper2DPlus.Catalog.Effect.DeletedAssignment"));
	const FPaper2DPlusValidationIssue* MemberIssue = FindCode(
		First, TEXT("Paper2DPlus.Catalog.Group.OutOfCatalogMember"));
	TestNotNull(TEXT("Deleted Manual companion emits path issue"), LayerIssue);
	TestNotNull(TEXT("Deleted Effect assignment emits path issue"), EffectIssue);
	TestNotNull(TEXT("Deleted group member emits path issue"), MemberIssue);
	if (LayerIssue)
	{
		TestEqual(TEXT("Deleted Layer path is retained"), LayerIssue->AssetPath, MissingLayerPath);
		TestEqual(TEXT("Deleted Layer type is retained"), LayerIssue->AssetType, UPaper2DPlusCharacterLayerAsset::StaticClass()->GetFName());
		TestEqual(TEXT("Deleted Layer Character context is retained"), LayerIssue->CharacterPath, FSoftObjectPath(Profile));
		TestEqual(TEXT("Deleted Layer scope is retained"), LayerIssue->Scope, FName(TEXT("Relationship")));
		TestEqual(TEXT("Deleted Layer field is retained"), LayerIssue->Field, FName(TEXT("LayerProfile")));
		TestEqual(TEXT("Deleted Layer repair targets the Catalog"), LayerIssue->GetNavigationAssetPath(), FSoftObjectPath(Catalog));
	}
	if (EffectIssue)
	{
		TestEqual(TEXT("Deleted Effect path is retained"), EffectIssue->AssetPath, MissingEffectPath);
		TestEqual(TEXT("Deleted Effect repair targets the Catalog"), EffectIssue->GetNavigationAssetPath(), FSoftObjectPath(Catalog));
	}
	if (MemberIssue)
	{
		TestEqual(TEXT("Deleted member path is retained"), MemberIssue->CharacterPath, MissingMemberPath);
		TestEqual(TEXT("Deleted member field is retained"), MemberIssue->Field, FName(TEXT("Members")));
		TestEqual(TEXT("Deleted member repair targets the Catalog"), MemberIssue->GetNavigationAssetPath(), FSoftObjectPath(Catalog));
		// The Groups TAB is retired: the Groups rail lives inside the Roster tab, so a group repair must
		// land the designer there. ToolId is what keeps a group issue distinguishable from a character one
		// now that both share a tab.
		TestEqual(TEXT("Deleted member repair targets the Roster tab that hosts the Groups rail"),
			MemberIssue->ToolTarget->TabId, FName(TEXT("CharacterCatalogEditor_Roster")));
		TestEqual(TEXT("Deleted member repair is scoped to the Groups tool, not the character roster"),
			MemberIssue->ToolTarget->ToolId, FName(TEXT("CharacterCatalogGroups")));
	}
	if (LayerIssue && LayerIssue->ToolTarget.IsSet() && MemberIssue && MemberIssue->ToolTarget.IsSet())
	{
		// Discriminating pair: a character-scoped repair keeps the plain Catalog ToolId, so the two kinds
		// of issue remain separable after the tab merge.
		TestEqual(TEXT("A character-scoped repair keeps the plain Catalog ToolId"),
			LayerIssue->ToolTarget->ToolId, FName(TEXT("CharacterCatalog")));
		TestNotEqual(TEXT("Group and character repairs never share one ToolId"),
			MemberIssue->ToolTarget->ToolId, LayerIssue->ToolTarget->ToolId);
		TestEqual(TEXT("Group and character repairs now share the Roster tab"),
			MemberIssue->ToolTarget->TabId, LayerIssue->ToolTarget->TabId);
	}
	TestNotNull(TEXT("Valid character row is not suppressed"), First.FindRow(FSoftObjectPath(Profile)));
	TestEqual(TEXT("Valid character remains in summary"), First.Summary.NumCharacters, 1);
	CompareStableKeys(*this, First, Second);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogExpectedTagCoverageAuditTest,
	"Paper2DPlus.CharacterCatalog.Validation.ExpectedTagCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogExpectedTagCoverageAuditTest::RunTest(const FString& Parameters)
{
	const FGameplayTag HeavyTag = Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	const FGameplayTag SwimmingTag = Paper2DPlusAnimationTags::Context_Swimming.GetTag();
	if (!TestTrue(
		TEXT("Native expected animation tags are registered"),
		HeavyTag.IsValid() && SwimmingTag.IsValid()))
	{
		return false;
	}

	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U13/CoverageCatalog"));
	UPaper2DPlusCharacterProfileAsset* Missing = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U13_A_Missing"));
	UPaper2DPlusCharacterProfileAsset* NearMiss = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U13_B_NearMiss"));
	UPaper2DPlusCharacterProfileAsset* Covered = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U13_C_Covered"));
	UPaper2DPlusCharacterProfileAsset* Superset = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U13_D_Superset"));

	auto AddMove = [](UPaper2DPlusCharacterProfileAsset* Profile, const TCHAR* Name)
	{
		FFlipbookProfileEntry& Move = Profile->Flipbooks.AddDefaulted_GetRef();
		Move.Identity.FlipbookName = Name;
		Move.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile);
		return Profile->Flipbooks.Num() - 1;
	};
	const int32 NearMissMove = AddMove(NearMiss, TEXT("NearMissHeavy"));
	FFlipbookTagMappingEntry NearMissMapping(NearMiss->Flipbooks[NearMissMove].Identity.FlipbookName);
	NearMiss->TagMappings.FindOrAdd(HeavyTag).Entries.Add(NearMissMapping);

	const int32 CoveredMove = AddMove(Covered, TEXT("CoveredHeavy"));
	Covered->Flipbooks[CoveredMove].EditorMeta.AnimationTags.AddTag(HeavyTag);
	const int32 SupersetMove = AddMove(Superset, TEXT("SupersetHeavy"));
	Superset->Flipbooks[SupersetMove].EditorMeta.AnimationTags.AddTag(HeavyTag);
	Superset->Flipbooks[SupersetMove].EditorMeta.AnimationTags.AddTag(SwimmingTag);

	Catalog->Entries = {
		MakeEntry(Missing),
		MakeEntry(NearMiss),
		MakeEntry(Covered),
		MakeEntry(Superset)
	};
	auto AddExpectedGroup = [Catalog](
		FName GroupName,
		UPaper2DPlusCharacterProfileAsset* Profile,
		const TArray<FGameplayTag>& ExpectedTags)
	{
		FPaper2DPlusCharacterCatalogGroup& Group = Catalog->Groups.AddDefaulted_GetRef();
		Group.GroupName = GroupName;
		Group.Members.Add(Profile);
		for (const FGameplayTag& ExpectedTag : ExpectedTags)
		{
			Group.AdditionalExpectedAnimationTags.AddTag(ExpectedTag);
		}
	};
	AddExpectedGroup(TEXT("Missing"), Missing, { HeavyTag, SwimmingTag });
	AddExpectedGroup(TEXT("NearMiss"), NearMiss, { HeavyTag });
	AddExpectedGroup(TEXT("Covered"), Covered, { HeavyTag });
	AddExpectedGroup(TEXT("Superset"), Superset, { HeavyTag });

	const TArray<UPaper2DPlusCharacterProfileAsset*> Profiles = {
		Missing, NearMiss, Covered, Superset
	};
	TArray<FAssetData> Assets;
	TMap<FString, UObject*> AssetsByPath;
	for (UPaper2DPlusCharacterProfileAsset* Profile : Profiles)
	{
		Assets.Add(MakeAssetData(Profile));
		AssetsByPath.Add(
			FProfileRelationshipService::NormalizeObjectPath(FSoftObjectPath(Profile)),
			Profile);
	}

	TArray<FString> Events;
	TMap<FString, int32> ResolveCounts;
	const FCharacterCatalogAuditService::FNativeAssetResolver Resolver =
		[&AssetsByPath, &Events, &ResolveCounts](const FSoftObjectPath& Path) -> UObject*
		{
			const FString Normalized = FProfileRelationshipService::NormalizeObjectPath(Path);
			Events.Add(FString::Printf(TEXT("Resolve:%s"), *Normalized));
			++ResolveCounts.FindOrAdd(Normalized);
			return AssetsByPath.FindRef(Normalized);
		};
	TArray<int32> ProgressIndices;
	TArray<int32> ProgressCounts;
	TArray<FSoftObjectPath> ProgressPaths;
	const FCharacterCatalogAuditService::FRowProgressCallback Progress =
		[&Events, &ProgressIndices, &ProgressCounts, &ProgressPaths](
			int32 RowIndex,
			int32 RowCount,
			const FSoftObjectPath& CharacterPath)
		{
			const FString Normalized =
				FProfileRelationshipService::NormalizeObjectPath(CharacterPath);
			Events.Add(FString::Printf(TEXT("Progress:%s"), *Normalized));
			ProgressIndices.Add(RowIndex);
			ProgressCounts.Add(RowCount);
			ProgressPaths.Add(CharacterPath);
		};

	const FPaper2DPlusCharacterCatalogAuditReport Report =
		FCharacterCatalogAuditService::BuildReport(
			Catalog,
			Assets,
			MakeSettings(Catalog),
			Resolver,
			ReadyCookInspector(),
			Progress);

	const FName CoverageCode(TEXT("Paper2DPlus.Catalog.Coverage.MissingExpectedTag"));
	TArray<const FPaper2DPlusValidationIssue*> CoverageIssues;
	for (const FPaper2DPlusValidationIssue& Issue : Report.Issues)
	{
		if (Issue.Code == CoverageCode)
		{
			CoverageIssues.Add(&Issue);
		}
	}
	TestEqual(TEXT("Missing and near-miss rows emit one warning per expected tag"),
		CoverageIssues.Num(), 3);
	TestEqual(TEXT("Coverage-only fixture has three warnings"),
		Report.Summary.Issues.NumWarnings, 3);
	TestEqual(TEXT("Coverage warnings do not become validation errors"),
		Report.Summary.Issues.NumErrors, 0);
	TestEqual(TEXT("Only the missing and near-miss characters own warnings"),
		Report.Summary.NumRowsWithWarnings, 2);
	TestTrue(TEXT("Warning-only coverage report remains Catalog-ready"),
		Report.Summary.IsCatalogReady());

	const auto FindCoverageIssue = [&CoverageIssues](
		const FSoftObjectPath& CharacterPath,
		const FGameplayTag& ExpectedTag) -> const FPaper2DPlusValidationIssue*
	{
		const FPaper2DPlusValidationIssue* const* Found = CoverageIssues.FindByPredicate(
			[&CharacterPath, &ExpectedTag](const FPaper2DPlusValidationIssue* Issue)
			{
				return Issue
					&& Issue->CharacterPath == CharacterPath
					&& Issue->StableDiscriminator == ExpectedTag.ToString();
			});
		return Found ? *Found : nullptr;
	};
	const FSoftObjectPath MissingPath(Missing);
	const FSoftObjectPath NearMissPath(NearMiss);
	TestNotNull(TEXT("The first missing expected tag survives normalization"),
		FindCoverageIssue(MissingPath, HeavyTag));
	TestNotNull(TEXT("The second missing expected tag does not collapse into the first"),
		FindCoverageIssue(MissingPath, SwimmingTag));
	TestNotNull(TEXT("A group-implied near miss still emits a coverage warning"),
		FindCoverageIssue(NearMissPath, HeavyTag));
	TestFalse(TEXT("An exact authored tag emits no coverage warning"), CoverageIssues.ContainsByPredicate(
		[Covered](const FPaper2DPlusValidationIssue* Issue)
		{
			return Issue && Issue->CharacterPath == FSoftObjectPath(Covered);
		}));
	TestFalse(TEXT("A superset authored tag set still counts as covered"), CoverageIssues.ContainsByPredicate(
		[Superset](const FPaper2DPlusValidationIssue* Issue)
		{
			return Issue && Issue->CharacterPath == FSoftObjectPath(Superset);
		}));

	for (const FPaper2DPlusValidationIssue* Issue : CoverageIssues)
	{
		if (!Issue)
		{
			continue;
		}
		TestEqual(TEXT("Coverage finding severity is Warning"),
			Issue->Severity, EPaper2DPlusValidationSeverity::Warning);
		TestEqual(TEXT("Coverage finding remains owned by the Catalog"),
			Issue->AssetPath, FSoftObjectPath(Catalog));
		TestEqual(TEXT("Coverage finding type remains the Catalog"),
			Issue->AssetType, UPaper2DPlusCharacterCatalogAsset::StaticClass()->GetFName());
		TestEqual(TEXT("Coverage finding scope is explicit"),
			Issue->Scope, FName(TEXT("Coverage")));
		TestEqual(TEXT("Coverage finding identifies the authored Profile field"),
			Issue->Field, FName(TEXT("AnimationTags")));
		if (TestTrue(TEXT("Coverage finding has a navigation target"), Issue->ToolTarget.IsSet()))
		{
			TestEqual(TEXT("Coverage navigation opens the Character Profile"),
				Issue->ToolTarget->AssetPath, Issue->CharacterPath);
			TestEqual(TEXT("Coverage activation retains Catalog Roster selection"),
				Issue->ToolTarget->ToolId, FName(TEXT("CharacterCatalog")));
			TestEqual(TEXT("Coverage activation targets the Catalog Roster tab"),
				Issue->ToolTarget->TabId, FName(TEXT("CharacterCatalogEditor_Roster")));
			TestEqual(TEXT("Coverage activation retains the roster row identity"),
				Issue->ToolTarget->ItemIdentity, Issue->CharacterPath.ToString());
		}
		const FString Message = Issue->Message.ToString();
		TestTrue(TEXT("Coverage message names the character"),
			Message.Contains(Issue->CharacterPath.GetAssetName()));
		TestTrue(TEXT("Coverage message names the expected tag"),
			Message.Contains(Issue->StableDiscriminator));
	}

	const bool bProgressPathCount = TestEqual(
		TEXT("Progress callback runs once per unique roster row"),
		ProgressPaths.Num(),
		Profiles.Num());
	const bool bProgressIndexCount = TestEqual(
		TEXT("Every progress callback records one row index"),
		ProgressIndices.Num(),
		Profiles.Num());
	const bool bProgressTotalCount = TestEqual(
		TEXT("Every progress callback records one row total"),
		ProgressCounts.Num(),
		Profiles.Num());
	if (!bProgressPathCount || !bProgressIndexCount || !bProgressTotalCount)
	{
		return false;
	}
	TArray<FString> ExpectedEvents;
	for (int32 Index = 0; Index < Profiles.Num(); ++Index)
	{
		const FSoftObjectPath ProfilePath(Profiles[Index]);
		const FString Normalized =
			FProfileRelationshipService::NormalizeObjectPath(ProfilePath);
		TestEqual(TEXT("Progress index follows saved roster order"),
			ProgressIndices[Index], Index);
		TestEqual(TEXT("Progress reports the complete row count"),
			ProgressCounts[Index], Profiles.Num());
		TestEqual(TEXT("Progress names the current Character Profile"),
			ProgressPaths[Index], ProfilePath);
		TestEqual(TEXT("Each Character Profile resolves exactly once"),
			ResolveCounts.FindRef(Normalized), 1);
		ExpectedEvents.Add(FString::Printf(TEXT("Progress:%s"), *Normalized));
		ExpectedEvents.Add(FString::Printf(TEXT("Resolve:%s"), *Normalized));
	}
	TestTrue(TEXT("Every progress callback fires before that row resolves"),
		Events == ExpectedEvents);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogExplicitResolverLoadFailureTest,
	"Paper2DPlus.CharacterCatalog.Validation.ExplicitResolverLoadFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogExplicitResolverLoadFailureTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U13/LoadFailureCatalog"));
	UPaper2DPlusCharacterProfileAsset* FirstProfile = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U13_LoadFailureFirst"));
	UPaper2DPlusCharacterProfileAsset* SecondProfile = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U13_LoadFailureSecond"));
	UPaper2DPlusEffectProfileAsset* SharedEffect = NewAsset<UPaper2DPlusEffectProfileAsset>(
		TEXT("/Game/Effects/U13_LoadFailureSharedEffect"));

	FPaper2DPlusCharacterCatalogEntry FirstEntry = MakeEntry(FirstProfile);
	FirstEntry.EffectProfile = SharedEffect;
	FPaper2DPlusCharacterCatalogEntry SecondEntry = MakeEntry(SecondProfile);
	SecondEntry.EffectProfile = SharedEffect;
	Catalog->Entries = { FirstEntry, SecondEntry };
	const TArray<FAssetData> Assets = {
		MakeAssetData(FirstProfile),
		MakeAssetData(SecondProfile),
		MakeAssetData(SharedEffect)
	};

	TMap<FString, UObject*> ResolvedAssets;
	ResolvedAssets.Add(
		FProfileRelationshipService::NormalizeObjectPath(FSoftObjectPath(FirstProfile)),
		FirstProfile);
	ResolvedAssets.Add(
		FProfileRelationshipService::NormalizeObjectPath(FSoftObjectPath(SecondProfile)),
		SecondProfile);
	TMap<FString, int32> ResolveCounts;
	const FCharacterCatalogAuditService::FNativeAssetResolver Resolver =
		[&ResolvedAssets, &ResolveCounts](const FSoftObjectPath& Path) -> UObject*
		{
			const FString Normalized =
				FProfileRelationshipService::NormalizeObjectPath(Path);
			++ResolveCounts.FindOrAdd(Normalized);
			return ResolvedAssets.FindRef(Normalized);
		};

	const FPaper2DPlusCharacterCatalogAuditReport Report =
		FCharacterCatalogAuditService::BuildReport(
			Catalog,
			Assets,
			MakeSettings(Catalog),
			Resolver,
			ReadyCookInspector());
	const FName LoadFailureCode(
		TEXT("Paper2DPlus.Catalog.Validation.AssetLoadFailed"));
	TArray<const FPaper2DPlusValidationIssue*> LoadFailures;
	for (const FPaper2DPlusValidationIssue& Issue : Report.Issues)
	{
		if (Issue.Code == LoadFailureCode)
		{
			LoadFailures.Add(&Issue);
		}
	}

	TestEqual(TEXT("A cached failed companion load is projected into both roster rows"),
		LoadFailures.Num(), 2);
	TestEqual(TEXT("Explicit load failures are report errors"),
		Report.Summary.Issues.NumErrors, 2);
	TestEqual(TEXT("Both affected roster rows own an error"),
		Report.Summary.NumRowsWithErrors, 2);
	const FString SharedEffectKey =
		FProfileRelationshipService::NormalizeObjectPath(FSoftObjectPath(SharedEffect));
	TestEqual(TEXT("The shared failing path is resolved only once"),
		ResolveCounts.FindRef(SharedEffectKey), 1);

	TSet<FSoftObjectPath> AffectedCharacters;
	for (const FPaper2DPlusValidationIssue* Issue : LoadFailures)
	{
		if (!Issue)
		{
			continue;
		}
		TestEqual(TEXT("Explicit load failure severity is Error"),
			Issue->Severity, EPaper2DPlusValidationSeverity::Error);
		TestEqual(TEXT("Load failure identifies the registry-present asset"),
			Issue->AssetPath, FSoftObjectPath(SharedEffect));
		TestEqual(TEXT("Load failure navigation targets the failing asset"),
			Issue->GetNavigationAssetPath(), FSoftObjectPath(SharedEffect));
		TestEqual(TEXT("Load failure has deterministic validation scope"),
			Issue->Scope, FName(TEXT("Validation")));
		TestTrue(TEXT("Load failure message names the failing path"),
			Issue->Message.ToString().Contains(FSoftObjectPath(SharedEffect).ToString()));
		TestTrue(TEXT("Load failure is projected with roster-row context"),
			Issue->CharacterPath.IsValid());
		AffectedCharacters.Add(Issue->CharacterPath);
	}
	TestTrue(TEXT("The first roster row receives the cached failure"),
		AffectedCharacters.Contains(FSoftObjectPath(FirstProfile)));
	TestTrue(TEXT("The second roster row receives the cached failure"),
		AffectedCharacters.Contains(FSoftObjectPath(SecondProfile)));
	if (LoadFailures.Num() == 2)
	{
		TestNotEqual(TEXT("Row context keeps projected failure identities distinct"),
			LoadFailures[0]->StableKey, LoadFailures[1]->StableKey);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogCoverageRequiresExplicitResolverTest,
	"Paper2DPlus.CharacterCatalog.Validation.CoverageRequiresExplicitResolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogCoverageRequiresExplicitResolverTest::RunTest(
	const FString& Parameters)
{
	const FGameplayTag HeavyTag = Paper2DPlusAnimationTags::Combat_Heavy.GetTag();
	if (!TestTrue(TEXT("Native expected animation tag is registered"), HeavyTag.IsValid()))
	{
		return false;
	}

	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U13/ResidentCoverageCatalog"));
	UPaper2DPlusCharacterProfileAsset* ResidentProfile =
		NewAsset<UPaper2DPlusCharacterProfileAsset>(
			TEXT("/Game/Characters/U13_ResidentCoverageProfile"));
	Catalog->Entries.Add(MakeEntry(ResidentProfile));
	Catalog->ExpectedAnimationTags.AddTag(HeavyTag);
	const FSoftObjectPath ResidentPath(ResidentProfile);
	if (!TestTrue(
		TEXT("Fixture Profile is already resident"),
		ResidentPath.ResolveObject() == ResidentProfile))
	{
		return false;
	}

	const FPaper2DPlusCharacterCatalogAuditReport Report =
		FCharacterCatalogAuditService::BuildReport(
			Catalog,
			{ MakeAssetData(ResidentProfile) },
			MakeSettings(Catalog),
			FCharacterCatalogAuditService::FNativeAssetResolver(),
			ReadyCookInspector());
	TestFalse(TEXT("Resident no-resolver audits never emit expected-tag coverage"), Report.Issues.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code == TEXT("Paper2DPlus.Catalog.Coverage.MissingExpectedTag");
		}));
	TestFalse(TEXT("A resident object is not reported as deferred"), Report.Issues.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code == TEXT("Paper2DPlus.Catalog.Validation.DeferredDeepValidation");
		}));
	TestEqual(TEXT("No-resolver resident fixture remains warning-free"),
		Report.Summary.Issues.NumWarnings, 0);
	TestEqual(TEXT("No-resolver resident fixture remains error-free"),
		Report.Summary.Issues.NumErrors, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogAuditNonMutatingTest,
	"Paper2DPlus.CharacterCatalog.Validation.NonMutatingPreparedReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogAuditNonMutatingTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/U27/ReadOnlyCatalog"));
	UPaper2DPlusCharacterProfileAsset* Profile = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/U27_ReadOnlyCharacter"));
	UPaper2DPlusCharacterLayerAsset* Layer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/U27_ReadOnlyLayer"));
	UPaper2DPlusEffectProfileAsset* Effect = NewAsset<UPaper2DPlusEffectProfileAsset>(
		TEXT("/Game/Effects/U27_ReadOnlyEffect"));
	UPaper2DPlusCombatProfileAsset* Combat = NewAsset<UPaper2DPlusCombatProfileAsset>(
		TEXT("/Game/Combat/U27_ReadOnlyCombat"));
	ConfigureValidLayer(Layer, Profile);
	Combat->CharacterProfile = Profile;

	FPaper2DPlusCharacterCatalogEntry Entry = MakeEntry(Profile);
	Entry.LayerProfile = Layer;
	Entry.EffectProfile = Effect;
	Entry.CombatProfile = Combat;
	Catalog->Entries.Add(Entry);
	const TArray<FAssetData> Assets = {
		MakeAssetData(Profile),
		MakeAssetData(Layer, true, FSoftObjectPath(Profile)),
		MakeAssetData(Effect),
		MakeAssetData(Combat, true, FSoftObjectPath(Profile))
	};
	const FPaper2DPlusCharacterCatalogSettingsSnapshot Settings = MakeSettings(Catalog);
	const int32 BeforeEntryCount = Catalog->Entries.Num();
	const int32 BeforeGroupCount = Catalog->Groups.Num();
	const FSoftObjectPath BeforeCharacter = Catalog->Entries[0].CharacterProfile.ToSoftObjectPath();
	const FSoftObjectPath BeforeLayer = Catalog->Entries[0].LayerProfile.ToSoftObjectPath();
	const FSoftObjectPath BeforeEffect = Catalog->Entries[0].EffectProfile.ToSoftObjectPath();
	const FSoftObjectPath BeforeCombat = Catalog->Entries[0].CombatProfile.ToSoftObjectPath();
	const TArray<UObject*> ResolvedAssets = { Profile, Layer, Effect, Combat };
	const TArray<UObject*> AllAssets = { Catalog, Profile, Layer, Effect, Combat };
	for (UObject* Asset : AllAssets)
	{
		Asset->GetOutermost()->SetDirtyFlag(false);
	}

	int32 ResolveCount = 0;
	int32 CookInspectCount = 0;
	const FPaper2DPlusCharacterCatalogAuditReport Report = FCharacterCatalogAuditService::BuildReport(
		Catalog,
		Assets,
		Settings,
		MakeResolver(ResolvedAssets, &ResolveCount),
		ReadyCookInspector(&CookInspectCount));
	const int32 CallsAfterAudit = ResolveCount;

	TestNotNull(TEXT("Prepared row can be read"), Report.FindRow(FSoftObjectPath(Profile)));
	TestTrue(TEXT("Prepared summary can be read"), Report.Summary.IsCatalogReady());
	for (int32 Index = 0; Index < 5; ++Index)
	{
		(void)Report.FindRow(FSoftObjectPath(Profile));
		(void)Report.Summary.NumCharacters;
		(void)Report.Issues.Num();
	}
	TestEqual(TEXT("All four native assets resolve once during explicit audit"), CallsAfterAudit, 4);
	TestEqual(TEXT("Prepared row reads perform no asset resolution"), ResolveCount, CallsAfterAudit);
	TestEqual(TEXT("Host cook policy is inspected once"), CookInspectCount, 1);
	TestEqual(TEXT("Saved roster size is unchanged"), Catalog->Entries.Num(), BeforeEntryCount);
	TestEqual(TEXT("Saved group list is unchanged"), Catalog->Groups.Num(), BeforeGroupCount);
	TestEqual(TEXT("Character identity is unchanged"),
		Catalog->Entries[0].CharacterProfile.ToSoftObjectPath(), BeforeCharacter);
	TestEqual(TEXT("Layer assignment is unchanged"), Catalog->Entries[0].LayerProfile.ToSoftObjectPath(), BeforeLayer);
	TestEqual(TEXT("Effect assignment is unchanged"), Catalog->Entries[0].EffectProfile.ToSoftObjectPath(), BeforeEffect);
	TestEqual(TEXT("Combat assignment is unchanged"), Catalog->Entries[0].CombatProfile.ToSoftObjectPath(), BeforeCombat);
	for (UObject* Asset : AllAssets)
	{
		TestFalse(
			FString::Printf(TEXT("Audit leaves %s package clean"), *Asset->GetName()),
			Asset->GetOutermost()->IsDirty());
	}
	return true;
}

/**
 * The three companion-relationship states that must NOT be a hard Error, and the one that must be.
 *
 * Layer and Combat assets always publish the inward relationship tag — writing the literal "None"
 * when their own link is unset — so an unfinished companion arrives as a tagged candidate with an
 * empty path. Treating that as "points at a different Character" made the Catalog report an Error,
 * and `validate-paper2dplus` exit 1, for a project whose only crime was a half-filled form.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCatalogRelationshipSoftStatesAuditTest,
	"Paper2DPlus.CharacterCatalog.Validation.RelationshipSoftStatesAreWarnings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusCatalogRelationshipSoftStatesAuditTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/Review/RelationshipCatalog"));

	// A: a companion assigned before its own Character link was filled in.
	UPaper2DPlusCharacterProfileAsset* UnlinkedOwner = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/Review_UnlinkedOwner"));
	UPaper2DPlusCombatProfileAsset* UnlinkedCombat = NewAsset<UPaper2DPlusCombatProfileAsset>(
		TEXT("/Game/Combat/Review_UnlinkedCombat"));

	// B: a companion that predates the inward tag entirely.
	UPaper2DPlusCharacterProfileAsset* LegacyOwner = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/Review_LegacyOwner"));
	UPaper2DPlusCharacterLayerAsset* LegacyLayer = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/Review_LegacyLayer"));
	ConfigureValidLayer(LegacyLayer, LegacyOwner);

	FPaper2DPlusCharacterCatalogEntry UnlinkedEntry = MakeEntry(UnlinkedOwner);
	UnlinkedEntry.CombatProfile = UnlinkedCombat;
	FPaper2DPlusCharacterCatalogEntry LegacyEntry = MakeEntry(LegacyOwner);
	LegacyEntry.LayerProfile = LegacyLayer;
	Catalog->Entries = { UnlinkedEntry, LegacyEntry };

	const TArray<FAssetData> Assets = {
		MakeAssetData(UnlinkedOwner),
		MakeAssetData(LegacyOwner),
		// Tag PRESENT but empty — the "None" shape every unset companion link publishes.
		MakeAssetData(UnlinkedCombat, /*bIncludeRelationshipTag=*/true, FSoftObjectPath()),
		// Tag ABSENT — the pre-tag legacy asset.
		MakeAssetData(LegacyLayer, /*bIncludeRelationshipTag=*/false)
	};
	const TArray<UObject*> ResolverAssets = {
		UnlinkedOwner, LegacyOwner, UnlinkedCombat, LegacyLayer
	};
	const FPaper2DPlusCharacterCatalogAuditReport Report =
		Audit(Catalog, Assets, MakeSettings(Catalog), ResolverAssets);

	TestTrue(TEXT("An unlinked companion is a Warning"), HasCodeWithSeverity(
		Report,
		TEXT("Paper2DPlus.Catalog.Relationship.Combat.Unlinked"),
		EPaper2DPlusValidationSeverity::Warning));
	TestFalse(TEXT("An unlinked companion is never reported as a mismatch"),
		Report.Issues.ContainsByPredicate([](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code == FName(TEXT("Paper2DPlus.Catalog.Relationship.Combat.ManualMismatch"));
		}));

	TestTrue(TEXT("An untagged legacy companion is a Warning"), HasCodeWithSeverity(
		Report,
		TEXT("Paper2DPlus.Catalog.Relationship.Layer.LegacyUnknown"),
		EPaper2DPlusValidationSeverity::Warning));

	// The whole point: RELATIONSHIP state must never be an Error for either soft case.
	//
	// Deliberately scoped to relationship codes rather than the report's total error count. An unlinked
	// Combat Profile ALSO trips that asset's own validator (`Paper2DPlus.Combat.CharacterProfile`,
	// Error — its Character link is required), which is a separate and correct pre-existing rule about
	// the companion asset itself. The defect this covers was the Catalog inventing a SECOND, false
	// Error claiming the companion "points to a different Character Profile" when it points at nothing.
	for (const FPaper2DPlusValidationIssue& Issue : Report.Issues)
	{
		if (Issue.Code.ToString().StartsWith(TEXT("Paper2DPlus.Catalog.Relationship."))
			&& Issue.Severity == EPaper2DPlusValidationSeverity::Error)
		{
			AddError(FString::Printf(
				TEXT("A soft relationship state was reported as an Error: %s"),
				*Issue.Code.ToString()));
		}
	}

	// The Layer half carries no asset-level error of its own, so it pins the clean shape end to end:
	// an untagged legacy companion contributes a Warning and nothing that can fail the gate.
	const bool bLayerErrorFree = !Report.Issues.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Severity == EPaper2DPlusValidationSeverity::Error
				&& Issue.Field == TEXT("LayerProfile");
		});
	TestTrue(TEXT("A legacy untagged Layer companion raises no Error at all"), bLayerErrorFree);

	// A REQUIRED slot with two claimants stays an Error, so relaxing the soft states did not relax
	// the state that genuinely blocks a designer.
	UPaper2DPlusCharacterCatalogAsset* RequiredCatalog = NewAsset<UPaper2DPlusCharacterCatalogAsset>(
		TEXT("/Game/Paper2DPlusTests/Review/RequiredAmbiguityCatalog"));
	UPaper2DPlusCharacterProfileAsset* Contested = NewAsset<UPaper2DPlusCharacterProfileAsset>(
		TEXT("/Game/Characters/Review_ContestedCharacter"));
	UPaper2DPlusCharacterLayerAsset* ClaimA = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/Review_ClaimA"));
	UPaper2DPlusCharacterLayerAsset* ClaimB = NewAsset<UPaper2DPlusCharacterLayerAsset>(
		TEXT("/Game/Layers/Review_ClaimB"));
	ConfigureValidLayer(ClaimA, Contested);
	ConfigureValidLayer(ClaimB, Contested);

	FPaper2DPlusCharacterCatalogEntry ContestedEntry = MakeEntry(Contested);
	ContestedEntry.Requirements.bRequireLayer = true;
	RequiredCatalog->Entries = { ContestedEntry };

	const TArray<FAssetData> RequiredAssets = {
		MakeAssetData(Contested),
		MakeAssetData(ClaimA, true, FSoftObjectPath(Contested)),
		MakeAssetData(ClaimB, true, FSoftObjectPath(Contested))
	};
	const FPaper2DPlusCharacterCatalogAuditReport RequiredReport = Audit(
		RequiredCatalog,
		RequiredAssets,
		MakeSettings(RequiredCatalog),
		{ Contested, ClaimA, ClaimB });

	TestTrue(TEXT("Two claimants on an empty REQUIRED slot is an Error"), HasCodeWithSeverity(
		RequiredReport,
		TEXT("Paper2DPlus.Catalog.Relationship.Layer.Ambiguous"),
		EPaper2DPlusValidationSeverity::Error));

	return true;
}

} // namespace Paper2DPlusCatalogValidationTests

#endif // WITH_DEV_AUTOMATION_TESTS
