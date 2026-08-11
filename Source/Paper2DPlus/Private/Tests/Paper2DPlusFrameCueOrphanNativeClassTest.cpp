// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "FrameCues/Paper2DPlusFrameCue.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Tests/Paper2DPlusTestFrameCueTypes.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

/*
 * A saved placement whose NATIVE Cue class is no longer in the binary.
 *
 * The orphan safety net (dispatch skips it, validation reports it) was built and regression-tested
 * against a deleted Cue Type BLUEPRINT ASSET. A deleted native class is a DIFFERENT shape: the class
 * lives in a script package, so the linker resolves it as an import from /Script/... rather than by
 * loading a Blueprint package. That difference has to be proven empirically, not inferred, which is
 * what this file exists for.
 *
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * READ THIS BEFORE CONCLUDING THE PLUGIN SHIPS A CORE REDIRECT.
 *
 * The FCoreRedirects entry installed below is a TEST-LOCAL SIMULATION DEVICE and nothing else. A
 * native class that is absent from the running binary cannot be produced from inside a test — the
 * only way to make the linker meet an unresolvable /Script class import is to point a saved import at
 * a class path the binary does not define. The redirect is added immediately before one LoadPackage
 * call and removed immediately after it, on every exit path.
 *
 * It is NOT related to, and must not be read as a counterexample to, the decision that NO shipping
 * CoreRedirect rows are added for the deleted built-in Frame Cue classes. Nothing here is written to
 * Config/DefaultPaper2DPlus.ini, and this redirect never exists outside the body of one test.
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */
namespace Paper2DPlusOrphanNativeCueTest
{
	/** Fixture setup is part of the proof: an unavailable disk/linker path is a failed test. */
	void OrphanNative_AddFixtureError(
		FAutomationTestBase& Test,
		const FString& What,
		const FString& Why)
	{
		Test.AddError(FString::Printf(TEXT("%s — %s"), *What, *Why));
	}

	/**
	 * The class path the doomed placement's import is redirected to.
	 *
	 * Deliberately a name inside a package that DOES exist (/Script/Paper2DPlus), because that is the
	 * exact shape a deleted native class leaves behind: the module is still loaded, the class is not
	 * in it. Pointing at a missing package instead would test a different failure.
	 */
	const TCHAR* const OrphanNative_AbsentClassPath =
		TEXT("/Script/Paper2DPlus.Paper2DPlusAbsentNativeCueForOrphanTest");

	/** A real on-disk package under the automation temp root, removed however the test exits. */
	struct FScopedOrphanPackageFixture
	{
		FString Directory;
		FString PackageName;
		FString FilePath;

		FScopedOrphanPackageFixture()
		{
			const FString GuidString = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Directory = FString::Printf(
				TEXT("/Game/__AutomationTemp__/P2DPOrphanNativeCue_%s"), *GuidString);
			PackageName = Directory / TEXT("OrphanProfile");
			FilePath = FPackageName::LongPackageNameToFilename(
				PackageName, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
		}

		~FScopedOrphanPackageFixture()
		{
			PurgeFromMemory();

			// Refuse to delete anything that is not provably inside this fixture's own temp directory.
			const FString SafeRoot = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir() / TEXT("__AutomationTemp__"));
			const FString FixtureDirectory =
				FPaths::ConvertRelativePathToFull(FPaths::GetPath(FilePath));
			if (!Directory.StartsWith(TEXT("/Game/__AutomationTemp__/P2DPOrphanNativeCue_"))
				|| !FPaths::IsUnderDirectory(FixtureDirectory, SafeRoot))
			{
				return;
			}

			IFileManager& FileManager = IFileManager::Get();
			for (const FString& PackageFile : {
				FilePath,
				FPaths::ChangeExtension(FilePath, TEXT("uexp")),
				FPaths::ChangeExtension(FilePath, TEXT("ubulk")) })
			{
				if (FileManager.FileExists(*PackageFile))
				{
					FileManager.Delete(*PackageFile, /*RequireExists=*/false, /*EvenReadOnly=*/true,
						/*Quiet=*/true);
				}
			}
			FileManager.DeleteDirectory(*FixtureDirectory, /*RequireExists=*/false, /*Tree=*/true);
		}

		FScopedOrphanPackageFixture(const FScopedOrphanPackageFixture&) = delete;
		FScopedOrphanPackageFixture& operator=(const FScopedOrphanPackageFixture&) = delete;

		/**
		 * Evicts whatever is resident under this package name so a later load provably reads the FILE.
		 *
		 * Three steps, each load-bearing. Clearing the keep-alive flags and unrooting lets GC take the
		 * objects. ResetLoaders detaches the linker — the linker holds a strong reference to its root
		 * package and an open handle on the file, so without this the package survives collection. The
		 * rename is the belt: if anything still references the old package it stays in memory, and a
		 * name-keyed load would silently hand back the pre-purge objects and prove nothing at all.
		 */
		void PurgeFromMemory() const
		{
			if (UPackage* Resident = FindPackage(nullptr, *PackageName))
			{
				ForEachObjectWithOuter(Resident, [](UObject* Object)
				{
					Object->ClearFlags(RF_Public | RF_Standalone);
				},
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
					EGetObjectsFlags::IncludeNestedObjects);
#else
					/*bIncludeNestedObjects=*/true);
#endif
				Resident->ClearFlags(RF_Public | RF_Standalone);
				Resident->SetDirtyFlag(false);
				if (Resident->IsRooted())
				{
					Resident->RemoveFromRoot();
				}
				ResetLoaders(Resident);
				const FName TrashName = MakeUniqueObjectName(
					nullptr, UPackage::StaticClass(), TEXT("P2DPOrphanNativeCueTrash"));
				Resident->Rename(*TrashName.ToString(), nullptr,
					REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusOrphanDeletedNativeClassTest,
	"Paper2DPlus.FrameCues.Orphan.DeletedNativeClassPlacementLoadsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusOrphanDeletedNativeClassTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusOrphanNativeCueTest;
	Paper2DPlusBehaviorTestLog::Reset();

	FScopedOrphanPackageFixture Fixture;

	// ── 1. Author and save a Character Profile carrying two placements of DIFFERENT Cue classes. ──
	{
		UPackage* Package = CreatePackage(*Fixture.PackageName);
		if (!Package)
		{
			OrphanNative_AddFixtureError(
				*this,
				TEXT("Deleted native Cue class placement load"),
				TEXT("the automation temp package could not be created in this host"));
			return false;
		}
		Package->AddToRoot();
		ON_SCOPE_EXIT{ Package->RemoveFromRoot(); };

		UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			Package, TEXT("OrphanProfile"), RF_Public | RF_Standalone);
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
			Package, TEXT("OrphanFlipbook"), RF_Public | RF_Standalone);
		{
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.KeyFrames.AddDefaulted();
		}

		FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = TEXT("Attack");
		Entry.Identity.Flipbook = Flipbook;
		Entry.CombatData.Frames.SetNum(1);

		// Slot 0 is the placement whose class will be unresolvable on reload.
		UPaper2DPlusTestMomentCue* Doomed = NewObject<UPaper2DPlusTestMomentCue>(Profile);
		Doomed->TriggerFrame = 0;
		Doomed->CustomPayload = 5;
		Entry.FrameEventData.FrameCues.Add(Doomed);

		// Slot 1 is an ordinary sibling of a DIFFERENT class on the same animation. It must survive
		// untouched and still dispatch — a broken placement may not take its neighbours with it.
		UPaper2DPlusTestBehaviorMomentCue* Sibling = NewObject<UPaper2DPlusTestBehaviorMomentCue>(Profile);
		Sibling->TriggerFrame = 0;
		Sibling->Payload = 91;
		Entry.FrameEventData.FrameCues.Add(Sibling);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Profile, *Fixture.FilePath, SaveArgs))
		{
			OrphanNative_AddFixtureError(
				*this,
				TEXT("Deleted native Cue class placement load"),
				TEXT("the automation temp package could not be written to disk in this host"));
			return false;
		}
	}

	// ── 2. Evict it, so the next load provably comes off disk through the linker. ──
	Fixture.PurgeFromMemory();
	if (FindPackage(nullptr, *Fixture.PackageName) != nullptr)
	{
		OrphanNative_AddFixtureError(
			*this,
			TEXT("Deleted native Cue class placement load"),
			TEXT("the saved package could not be evicted from memory, so a reload would prove nothing"));
		return false;
	}

	// ── 3. Reload with slot 0's class made unresolvable. See the file header: SIMULATION DEVICE. ──
	const TArray<FCoreRedirect> AbsentClassRedirect = {
		FCoreRedirect(
			ECoreRedirectFlags::Type_Class,
			TEXT("/Script/Paper2DPlus.Paper2DPlusTestMomentCue"),
			OrphanNative_AbsentClassPath)
	};
	const FString RedirectSource(TEXT("Paper2DPlusOrphanNativeCueTest"));

	UPackage* Reloaded = nullptr;
	{
		FCoreRedirects::AddRedirectList(AbsentClassRedirect, RedirectSource);
		// Removed on EVERY exit path, including an early return or an exception: a leaked process-wide
		// class redirect would silently corrupt every later load in the same editor session.
		ON_SCOPE_EXIT{ FCoreRedirects::RemoveRedirectList(AbsentClassRedirect, RedirectSource); };
		Reloaded = LoadPackage(nullptr, *Fixture.PackageName, LOAD_None);
	}

	// Reaching this line at all is the first assertion: an unresolvable native Cue class is survivable.
	if (!TestNotNull(TEXT("A package holding a deleted-native-class placement still loads"), Reloaded))
	{
		return false;
	}
	Reloaded->AddToRoot();
	ON_SCOPE_EXIT{ if (Reloaded->IsRooted()) { Reloaded->RemoveFromRoot(); } };

	UPaper2DPlusCharacterProfileAsset* Profile =
		FindObject<UPaper2DPlusCharacterProfileAsset>(Reloaded, TEXT("OrphanProfile"));
	if (!TestNotNull(TEXT("The Character Profile itself survives the unresolvable placement"), Profile))
	{
		return false;
	}
	if (!TestEqual(TEXT("The reloaded profile keeps its animation"), Profile->Flipbooks.Num(), 1))
	{
		return false;
	}

	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Placements =
		Profile->Flipbooks[0].FrameEventData.FrameCues;
	if (!TestEqual(TEXT("Both placement slots survive; neither is silently compacted away"),
		Placements.Num(), 2))
	{
		return false;
	}

	// THE claim under test. The linker cannot resolve the class import, the export therefore fails,
	// and the placement deserializes to NULL — the same !IsValid arm an authored-empty slot takes.
	// If this ever starts failing, a deleted native class has begun deserializing to something else
	// (a placeholder type, say), and every consumer of IsPlacementResolvable needs re-auditing.
	TestNull(TEXT("A placement whose native Cue class is absent deserializes to null"),
		Placements[0].Get());

	UPaper2DPlusTestBehaviorMomentCue* Sibling =
		Cast<UPaper2DPlusTestBehaviorMomentCue>(Placements[1].Get());
	if (!TestNotNull(TEXT("The sibling placement of a surviving class is unharmed"), Sibling))
	{
		return false;
	}
	TestEqual(TEXT("The sibling placement keeps its authored payload"), Sibling->Payload, 91);

	// ── 4. The surviving sibling on the same animation still dispatches. ──
	UPaperFlipbook* ReloadedFlipbook = Profile->Flipbooks[0].Identity.Flipbook.Get();
	if (TestNotNull(TEXT("The reloaded animation keeps its flipbook identity"), ReloadedFlipbook))
	{
		AActor* Actor = NewObject<AActor>();
		UPaperFlipbookComponent* FlipbookComponent = NewObject<UPaperFlipbookComponent>(Actor);
		UPaper2DPlusCharacterProfileComponent* ProfileComponent =
			NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
		Actor->AddOwnedComponent(FlipbookComponent);
		Actor->AddOwnedComponent(ProfileComponent);
		FlipbookComponent->SetFlipbook(ReloadedFlipbook);
		ProfileComponent->CharacterProfile = Profile;
		ProfileComponent->FlipbookComponent = FlipbookComponent;
		ProfileComponent->HandleFlipbookChanged(ReloadedFlipbook);
		ProfileComponent->HandleFrameChanged(0);

		TestEqual(TEXT("The orphaned slot runs no behavior and the sibling runs exactly once"),
			Paper2DPlusBehaviorTestLog::Records().Num(), 1);
		if (Paper2DPlusBehaviorTestLog::Records().Num() == 1)
		{
			TestEqual(TEXT("The dispatch that ran was the surviving sibling's"),
				Paper2DPlusBehaviorTestLog::Records()[0].Payload, 91);
		}
	}

	// ── 5. Validation reports the orphan, attributably. ──
	TArray<FCharacterProfileValidationIssue> Issues;
	Profile->ValidateCharacterProfileAsset(Issues);
	const TArray<FCharacterProfileValidationIssue> OrphanErrors =
		Issues.FilterByPredicate([](const FCharacterProfileValidationIssue& Issue)
		{
			return Issue.Severity == ECharacterProfileValidationSeverity::Error
				&& Issue.Context.EndsWith(TEXT("FrameCue[0]"));
		});
	TestEqual(TEXT("The orphaned slot raises exactly one validation error"), OrphanErrors.Num(), 1);
	if (OrphanErrors.Num() == 1)
	{
		TestTrue(TEXT("The orphan error names the owning Character Profile"),
			OrphanErrors[0].Message.Contains(Profile->GetName()));
		TestTrue(TEXT("The orphan error names the affected animation"),
			OrphanErrors[0].Message.Contains(TEXT("Attack")));
	}
	TestEqual(TEXT("The healthy sibling slot raises no error of its own"),
		Issues.FilterByPredicate([](const FCharacterProfileValidationIssue& Issue)
		{
			return Issue.Severity == ECharacterProfileValidationSeverity::Error
				&& Issue.Context.EndsWith(TEXT("FrameCue[1]"));
		}).Num(), 0);

	return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
