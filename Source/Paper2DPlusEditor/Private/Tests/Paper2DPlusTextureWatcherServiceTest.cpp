// Copyright 2026 Infinite Gameworks. All Rights Reserved.

/**
 * Narrow ownership tests for TextureWatcherService.
 *
 * Actual source-change delivery belongs to Paper2DPlusAseWatcherIntegrationTest: it observes saved
 * profile/layer/sprite output, not logs or pending-queue state. These cases retain only the two
 * lifecycle contracts that the engine exposes directly: duplicate registration is rejected and
 * Shutdown relinquishes the exact DirectoryWatcher callback handle.
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"

#include "DirectoryWatcherModule.h"
#include "Editor.h"
#include "IDirectoryWatcher.h"
#include "TextureWatcherService.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusTextureWatcherServiceTest
{
	class FDirectoryFixture final
	{
	public:
		explicit FDirectoryFixture(FAutomationTestBase& InTest, const TCHAR* Prefix)
			: Test(InTest)
		{
			const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			SafeRoot = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectIntermediateDir() / TEXT("Paper2DPlusAutomation"));
			Directory = FPaths::ConvertRelativePathToFull(
				SafeRoot / FString::Printf(TEXT("%s_%s"), Prefix, *Token));
			FPaths::NormalizeFilename(SafeRoot);
			FPaths::NormalizeFilename(Directory);
		}

		~FDirectoryFixture()
		{
			if (!Cleanup())
			{
				Test.AddError(FString::Printf(
					TEXT("Texture watcher fixture cleanup failed for '%s'."),
					*Directory));
			}
		}

		bool Create() const
		{
			return IFileManager::Get().MakeDirectory(*Directory, true);
		}

		bool Cleanup() const
		{
			if (!FPaths::IsUnderDirectory(Directory, SafeRoot))
			{
				return false;
			}
			IFileManager& Files = IFileManager::Get();
			return (!Files.DirectoryExists(*Directory)
				|| Files.DeleteDirectory(*Directory, /*RequireExists=*/false, /*Tree=*/false))
				&& !Files.DirectoryExists(*Directory);
		}

		const FString& GetDirectory() const { return Directory; }

	private:
		FAutomationTestBase& Test;
		FString SafeRoot;
		FString Directory;
	};

	static void RestoreService(
		const FDirectoryFixture& Fixture,
		FAutomationTestBase& Test)
	{
		FTextureWatcherService& Watcher = FTextureWatcherService::Get();
		Test.TestTrue(TEXT("fixture teardown releases every watcher callback"), Watcher.Shutdown());
		Test.TestTrue(TEXT("fixture teardown removes its exact directory"), Fixture.Cleanup());
		Watcher.Initialize();
		Test.TestTrue(TEXT("the watcher is restored for subsequent editor tests"), Watcher.IsInitialized());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTextureWatcherDuplicateExternalRegistrationTest,
	"Paper2DPlus.EditorServices.TextureWatcher.DuplicateExternalRegistrationIsRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTextureWatcherDuplicateExternalRegistrationTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusTextureWatcherServiceTest;
	if (!TestNotNull(TEXT("editor automation session exposes GEditor"), GEditor))
	{
		return false;
	}
	FTextureWatcherService& Watcher = FTextureWatcherService::Get();
	if (!TestTrue(TEXT("the editor module initialized the watcher"), Watcher.IsInitialized()))
	{
		return false;
	}
	FDirectoryFixture Fixture(*this, TEXT("TextureWatcherDuplicate"));
	if (!TestTrue(TEXT("the fixture creates its unique external directory"), Fixture.Create()))
	{
		return false;
	}
	if (!TestTrue(TEXT("the first external registration creates a callback"),
		Watcher.RegisterExternalDirectory(Fixture.GetDirectory())))
	{
		RestoreService(Fixture, *this);
		return false;
	}
	TestFalse(TEXT("the same external directory cannot create a duplicate callback"),
		Watcher.RegisterExternalDirectory(Fixture.GetDirectory()));
	RestoreService(Fixture, *this);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTextureWatcherShutdownReleasesEngineCallbackTest,
	"Paper2DPlus.EditorServices.TextureWatcher.ShutdownReleasesEngineCallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTextureWatcherShutdownReleasesEngineCallbackTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusTextureWatcherServiceTest;
	if (!TestNotNull(TEXT("editor automation session exposes GEditor"), GEditor))
	{
		return false;
	}
	FTextureWatcherService& Watcher = FTextureWatcherService::Get();
	if (!TestTrue(TEXT("the watcher is active before the ownership proof"), Watcher.IsInitialized()))
	{
		return false;
	}
	FDirectoryFixture Fixture(*this, TEXT("TextureWatcherOwnership"));
	if (!TestTrue(TEXT("the fixture creates its unique external directory"), Fixture.Create()))
	{
		return false;
	}
	if (!TestTrue(TEXT("the service registers the ownership-test directory"),
		Watcher.RegisterExternalDirectory(Fixture.GetDirectory())))
	{
		RestoreService(Fixture, *this);
		return false;
	}

	FDelegateHandle RegisteredHandle;
	if (!TestTrue(TEXT("the ownership proof captures the real engine callback handle"),
		Watcher.CopyDirectoryWatcherHandleForTests(Fixture.GetDirectory(), RegisteredHandle)))
	{
		RestoreService(Fixture, *this);
		return false;
	}
	if (!TestTrue(TEXT("Shutdown completes callback release"), Watcher.Shutdown()))
	{
		RestoreService(Fixture, *this);
		return false;
	}

	FDirectoryWatcherModule& Module =
		FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* DirectoryWatcher = Module.Get();
	if (!TestNotNull(TEXT("DirectoryWatcher remains available to verify ownership"), DirectoryWatcher))
	{
		RestoreService(Fixture, *this);
		return false;
	}
	TestFalse(
		TEXT("the engine no longer owns the captured callback after Shutdown"),
		DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(
			Fixture.GetDirectory(),
			RegisteredHandle));

	RestoreService(Fixture, *this);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
