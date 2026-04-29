// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// Phase 0 HARD GATE: Validates that UPROPERTY(Instanced) TArray<TObjectPtr<UBase>>
// nested inside a USTRUCT inside a UPrimaryDataAsset behaves correctly in UE 5.7
// for the frame events plan's storage design.
//
// If ANY of the 5 tests in this file fail, the frame events plan must pivot to
// the UFlipbookEventContainer UObject-wrapper fallback per plan Phase 0 fallback.
// See docs/plans/2026-04-08-feat-frame-events-and-data-layer-decomposition-plan.md.
//
// The probe stays in the test suite as a regression guard after the gate passes --
// UE 5.7 -> 5.8 migration should re-verify these properties still hold.

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tests/Paper2DPlusInstancedProbeTypes.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"

/** Instanced probe test suite — Sprite detection probe rendering and result verification. */

#define LOCTEXT_NAMESPACE "Paper2DPlusInstancedProbe"

namespace Paper2DPlusInstancedProbe
{
	/** Creates a probe asset in the transient package with RF_Transactional. */
	static UPaper2DPlusProbeAsset* MakeProbeAsset()
	{
		return NewObject<UPaper2DPlusProbeAsset>(
			GetTransientPackage(), UPaper2DPlusProbeAsset::StaticClass(), NAME_None, RF_Transactional);
	}

	/** Creates an Instanced probe subobject owned by the given asset. */
	static UPaper2DPlusProbeBase* MakeProbe(UPaper2DPlusProbeAsset* Owner, int32 Value)
	{
		UPaper2DPlusProbeBase* Probe = NewObject<UPaper2DPlusProbeBase>(Owner, NAME_None, RF_Transactional);
		Probe->ProbeValue = Value;
		return Probe;
	}
}


// =============================================================================
// TEST 1: Save/Load roundtrip preserves Instanced subobject identity and value
// =============================================================================
// Uses real UPackage::SavePackage -> LoadPackage disk roundtrip to validate
// the linker serialization path (the production asset save/load path).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusInstancedProbeSaveLoadRoundtrip,
	"Paper2DPlus.Instanced.SaveLoadRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusInstancedProbeSaveLoadRoundtrip::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusInstancedProbe;

	const FGuid TestGuid = FGuid::NewGuid();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	const FString GuidStr = TestGuid.ToString(EGuidFormats::Digits).ToLower();
#else
	const FString GuidStr = TestGuid.ToString(EGuidFormats::DigitsLower);
#endif
	// Use /Game/ mount so LoadPackage can resolve the package by name.
	// Files land under Content/__AutomationTemp__/ and are cleaned up after the test.
	const FString PackageName = FString::Printf(TEXT("/Game/__AutomationTemp__/Paper2DPlusProbe_%s"), *GuidStr);
	const FString FilePath = FPackageName::LongPackageNameToFilename(
		PackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);

	// --- Save phase ---
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (!TestNotNull(TEXT("CreatePackage succeeded"), Package))
		{
			return false;
		}
		Package->AddToRoot();

		UPaper2DPlusProbeAsset* Asset = NewObject<UPaper2DPlusProbeAsset>(
			Package, TEXT("ProbeAsset"), RF_Public | RF_Standalone | RF_Transactional);

		Asset->ProbeEntries.SetNum(2);
		Asset->ProbeEntries[0].ProbeArray.Add(MakeProbe(Asset, 11));
		Asset->ProbeEntries[1].ProbeArray.Add(MakeProbe(Asset, 22));

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		const bool bSaved = UPackage::SavePackage(Package, Asset, *FilePath, SaveArgs);
		Package->RemoveFromRoot();

		if (!TestTrue(TEXT("SavePackage succeeded"), bSaved))
		{
			IFileManager::Get().Delete(*FilePath, false, true, true);
			return false;
		}
	}

	// Force GC so the in-memory package is fully released
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	// --- Load phase (load by package name, not filesystem path) ---
	UPackage* LoadedPackage = LoadPackage(nullptr, *PackageName, LOAD_None);
	if (!TestNotNull(TEXT("LoadPackage returned valid package"), LoadedPackage))
	{
		IFileManager::Get().Delete(*FilePath, false, true, true);
		return false;
	}
	LoadedPackage->AddToRoot();

	UPaper2DPlusProbeAsset* Loaded = FindObject<UPaper2DPlusProbeAsset>(
		LoadedPackage, TEXT("ProbeAsset"));

	if (TestNotNull(TEXT("Loaded asset found in package"), Loaded))
	{
		TestEqual(TEXT("ProbeEntries count survived roundtrip"), Loaded->ProbeEntries.Num(), 2);

		if (Loaded->ProbeEntries.IsValidIndex(0) &&
			Loaded->ProbeEntries[0].ProbeArray.IsValidIndex(0))
		{
			UPaper2DPlusProbeBase* A = Loaded->ProbeEntries[0].ProbeArray[0];
			if (TestNotNull(TEXT("Loaded probe A is valid"), A))
			{
				TestEqual(TEXT("Loaded probe A value"), A->ProbeValue, 11);
				TestTrue(TEXT("Loaded probe A is inside loaded asset"), A->IsIn(Loaded));
			}
		}
		else
		{
			AddError(TEXT("Loaded ProbeEntries[0].ProbeArray[0] missing after roundtrip"));
		}

		if (Loaded->ProbeEntries.IsValidIndex(1) &&
			Loaded->ProbeEntries[1].ProbeArray.IsValidIndex(0))
		{
			UPaper2DPlusProbeBase* B = Loaded->ProbeEntries[1].ProbeArray[0];
			if (TestNotNull(TEXT("Loaded probe B is valid"), B))
			{
				TestEqual(TEXT("Loaded probe B value"), B->ProbeValue, 22);
				TestTrue(TEXT("Loaded probe B is inside loaded asset"), B->IsIn(Loaded));
			}
		}
		else
		{
			AddError(TEXT("Loaded ProbeEntries[1].ProbeArray[0] missing after roundtrip"));
		}
	}

	LoadedPackage->RemoveFromRoot();

	// Cleanup temp file and directory
	IFileManager::Get().Delete(*FilePath, false, true, true);
	IFileManager::Get().DeleteDirectory(*FPaths::GetPath(FilePath), false, true);

	return true;
}


// =============================================================================
// TEST 2: Add/Remove with FScopedTransaction undoes and redoes cleanly
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusInstancedProbeAddRemoveUndo,
	"Paper2DPlus.Instanced.AddRemoveUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusInstancedProbeAddRemoveUndo::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusInstancedProbe;

	if (!TestNotNull(TEXT("GEditor available for transaction tests"), GEditor))
	{
		return false;
	}

	// Reset undo buffer so prior tests don't interfere
	GEditor->ResetTransaction(LOCTEXT("ResetAddRemove", "Probe AddRemove Reset"));

	UPaper2DPlusProbeAsset* Asset = MakeProbeAsset();
	Asset->AddToRoot();
	Asset->ProbeEntries.SetNum(1);

	TestEqual(TEXT("Initial ProbeArray empty"), Asset->ProbeEntries[0].ProbeArray.Num(), 0);

	// Transaction 1: Add a probe
	{
		FScopedTransaction T(LOCTEXT("AddProbe", "Add Probe"));
		Asset->Modify();
		Asset->ProbeEntries[0].ProbeArray.Add(MakeProbe(Asset, 7));
	}
	TestEqual(TEXT("After Add: 1 probe"), Asset->ProbeEntries[0].ProbeArray.Num(), 1);

	// Transaction 2: Remove the probe
	{
		FScopedTransaction T(LOCTEXT("RemoveProbe", "Remove Probe"));
		Asset->Modify();
		Asset->ProbeEntries[0].ProbeArray.RemoveAt(0);
	}
	TestEqual(TEXT("After Remove: 0 probes"), Asset->ProbeEntries[0].ProbeArray.Num(), 0);

	// Undo Remove -> 1 probe
	TestTrue(TEXT("Undo Remove succeeded"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("After Undo Remove: 1 probe"), Asset->ProbeEntries[0].ProbeArray.Num(), 1);
	if (Asset->ProbeEntries[0].ProbeArray.IsValidIndex(0))
	{
		UPaper2DPlusProbeBase* Restored = Asset->ProbeEntries[0].ProbeArray[0];
		if (TestNotNull(TEXT("Restored probe valid"), Restored))
		{
			TestEqual(TEXT("Restored probe value"), Restored->ProbeValue, 7);
		}
	}

	// Undo Add -> 0 probes
	TestTrue(TEXT("Undo Add succeeded"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("After Undo Add: 0 probes"), Asset->ProbeEntries[0].ProbeArray.Num(), 0);

	// Redo Add -> 1 probe
	TestTrue(TEXT("Redo Add succeeded"), GEditor->RedoTransaction());
	TestEqual(TEXT("After Redo Add: 1 probe"), Asset->ProbeEntries[0].ProbeArray.Num(), 1);

	// Redo Remove -> 0 probes
	TestTrue(TEXT("Redo Remove succeeded"), GEditor->RedoTransaction());
	TestEqual(TEXT("After Redo Remove: 0 probes"), Asset->ProbeEntries[0].ProbeArray.Num(), 0);

	GEditor->ResetTransaction(LOCTEXT("EndAddRemove", "Probe AddRemove End"));
	Asset->RemoveFromRoot();
	return true;
}


// =============================================================================
// TEST 3: Property edit (ProbeValue change) undoes and redoes cleanly
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusInstancedProbePropertyEditUndo,
	"Paper2DPlus.Instanced.PropertyEditUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusInstancedProbePropertyEditUndo::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusInstancedProbe;

	if (!TestNotNull(TEXT("GEditor available for transaction tests"), GEditor))
	{
		return false;
	}

	GEditor->ResetTransaction(LOCTEXT("ResetPropertyEdit", "Probe PropertyEdit Reset"));

	UPaper2DPlusProbeAsset* Asset = MakeProbeAsset();
	Asset->AddToRoot();
	Asset->ProbeEntries.SetNum(1);

	UPaper2DPlusProbeBase* Probe = MakeProbe(Asset, 10);
	Asset->ProbeEntries[0].ProbeArray.Add(Probe);
	TestEqual(TEXT("Initial ProbeValue"), Probe->ProbeValue, 10);

	// Transaction: edit the probe's value
	{
		FScopedTransaction T(LOCTEXT("EditProbe", "Edit Probe"));
		Probe->Modify();
		Probe->ProbeValue = 99;
	}
	TestEqual(TEXT("After Edit: 99"), Probe->ProbeValue, 99);

	// Undo the edit
	TestTrue(TEXT("Undo Edit succeeded"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("After Undo: 10"), Probe->ProbeValue, 10);

	// Redo the edit
	TestTrue(TEXT("Redo Edit succeeded"), GEditor->RedoTransaction());
	TestEqual(TEXT("After Redo: 99"), Probe->ProbeValue, 99);

	GEditor->ResetTransaction(LOCTEXT("EndPropertyEdit", "Probe PropertyEdit End"));
	Asset->RemoveFromRoot();
	return true;
}


// =============================================================================
// TEST 4: Asset duplication produces unique subobjects (not shared references)
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusInstancedProbeDuplicationUniqueness,
	"Paper2DPlus.Instanced.DuplicationUniqueness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusInstancedProbeDuplicationUniqueness::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusInstancedProbe;

	UPaper2DPlusProbeAsset* Source = MakeProbeAsset();
	Source->AddToRoot();
	Source->ProbeEntries.SetNum(2);

	UPaper2DPlusProbeBase* ProbeA = MakeProbe(Source, 11);
	UPaper2DPlusProbeBase* ProbeB = MakeProbe(Source, 22);
	Source->ProbeEntries[0].ProbeArray.Add(ProbeA);
	Source->ProbeEntries[1].ProbeArray.Add(ProbeB);

	// StaticDuplicateObject should deep-copy the Instanced subobjects
	UPaper2DPlusProbeAsset* Dup = Cast<UPaper2DPlusProbeAsset>(
		StaticDuplicateObject(Source, GetTransientPackage(), NAME_None));

	if (!TestNotNull(TEXT("StaticDuplicateObject returned valid copy"), Dup))
	{
		Source->RemoveFromRoot();
		return false;
	}
	Dup->AddToRoot();

	TestEqual(TEXT("Duplicate ProbeEntries count"), Dup->ProbeEntries.Num(), 2);

	if (Dup->ProbeEntries.Num() == 2 &&
		Dup->ProbeEntries[0].ProbeArray.Num() == 1 &&
		Dup->ProbeEntries[1].ProbeArray.Num() == 1)
	{
		UPaper2DPlusProbeBase* DupA = Dup->ProbeEntries[0].ProbeArray[0];
		UPaper2DPlusProbeBase* DupB = Dup->ProbeEntries[1].ProbeArray[0];

		if (TestNotNull(TEXT("Duplicate probe A valid"), DupA) &&
			TestNotNull(TEXT("Duplicate probe B valid"), DupB))
		{
			// Values survived
			TestEqual(TEXT("Dup probe A value"), DupA->ProbeValue, 11);
			TestEqual(TEXT("Dup probe B value"), DupB->ProbeValue, 22);

			// Pointers are different (unique instances, not shared)
			TestTrue(TEXT("Dup probe A != source probe A"), DupA != ProbeA);
			TestTrue(TEXT("Dup probe B != source probe B"), DupB != ProbeB);

			// Outer reparented to the duplicate asset
			TestTrue(TEXT("Dup probe A is inside duplicate asset"), DupA->IsIn(Dup));
			TestTrue(TEXT("Dup probe B is inside duplicate asset"), DupB->IsIn(Dup));
			TestFalse(TEXT("Dup probe A is NOT inside source asset"), DupA->IsIn(Source));

			// Pointer isolation: mutating the copy doesn't affect source
			DupA->ProbeValue = 1000;
			TestEqual(TEXT("Source probe A unchanged after mutating copy"), ProbeA->ProbeValue, 11);
		}
	}
	else
	{
		AddError(TEXT("Duplicate structure mismatch"));
	}

	Dup->RemoveFromRoot();
	Source->RemoveFromRoot();
	return true;
}


// =============================================================================
// TEST 5: Outer chain validation -- subobject's Outer is the asset, not transient
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusInstancedProbeOuterChain,
	"Paper2DPlus.Instanced.OuterChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusInstancedProbeOuterChain::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusInstancedProbe;

	UPaper2DPlusProbeAsset* Asset = MakeProbeAsset();
	Asset->AddToRoot();
	Asset->ProbeEntries.SetNum(1);

	UPaper2DPlusProbeBase* Probe = MakeProbe(Asset, 1);
	Asset->ProbeEntries[0].ProbeArray.Add(Probe);

	// The probe's direct outer must be the asset (not the transient package).
	// This is critical: if the outer is wrong, SavePackage won't serialize
	// the subobject as part of the asset package.
	TestTrue(TEXT("Probe's direct Outer is the asset"),
	         Probe->GetOuter() == static_cast<UObject*>(Asset));
	TestTrue(TEXT("Probe IsIn asset"), Probe->IsIn(Asset));
	TestFalse(TEXT("Probe's direct Outer is NOT the transient package"),
	          Probe->GetOuter() == GetTransientPackage());

	// Asset's outer IS the transient package (sanity check for test setup)
	TestTrue(TEXT("Asset's Outer is the transient package"),
	         Asset->GetOuter() == GetTransientPackage());

	// Transitive: probe is in the transient package through the asset
	TestTrue(TEXT("Probe IsIn transient package transitively"),
	         Probe->IsIn(GetTransientPackage()));

	// Probe's outermost (package) is the transient package
	TestTrue(TEXT("Probe's outermost is the transient package"),
	         Probe->GetOutermost() == GetTransientPackage());

	Asset->RemoveFromRoot();
	return true;
}


#undef LOCTEXT_NAMESPACE

#endif  // WITH_EDITOR
