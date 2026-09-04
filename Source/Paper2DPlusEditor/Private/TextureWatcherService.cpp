// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "TextureWatcherService.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusSettings.h" // TASK-192 U1: the Live .ase Auto-Reimport gate + its change broadcast
#include "AsepriteImporter.h" // TASK-183: ResolveStoredAsePath / HashAseFileContent
#include "AsepriteImporter.h" // TASK-186: InitDefaultSelection + settings for the full auto-reimport
#include "AsepriteReimporter.h"
#include "TextureReimporter.h"
#include "ReimportConflictDialog.h"
#include "CharacterProfileEditorModel.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"
#include "TimerManager.h"

/** FTextureWatcherService — File system watcher for live texture reimport notifications in the editor. */

#define LOCTEXT_NAMESPACE "TextureWatcherService"

namespace
{
	bool IsCoveredByProjectContentWatch(const FString& Directory)
	{
		FString NormalizedDirectory = FPaths::ConvertRelativePathToFull(Directory);
		FString ProjectContentDirectory =
			FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
		FPaths::NormalizeDirectoryName(NormalizedDirectory);
		FPaths::NormalizeDirectoryName(ProjectContentDirectory);
		return NormalizedDirectory.Equals(ProjectContentDirectory, ESearchCase::IgnoreCase)
			|| FPaths::IsUnderDirectory(NormalizedDirectory, ProjectContentDirectory);
	}

	/**
	 * TASK-189: the per-source import context a watch entry came from. Exact stored-path match first
	 * (the form the registry tag carried), then a RESOLVED-path match so a differently spelled stored
	 * path naming the same file still resolves. Null on a legacy asset with no ImportedAseSources —
	 * the caller then falls back to the reflected single-source fields (which mirror element 0).
	 */
	const FAsepriteSourceContext* FindAseSourceContextForEntry(
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const FString& StoredSourcePath,
		const FString& ResolvedFilePath)
	{
		if (!StoredSourcePath.IsEmpty())
		{
			if (const FAsepriteSourceContext* Exact = LayerAsset.FindAseSourceContext(StoredSourcePath))
			{
				return Exact;
			}
		}
		for (const FAsepriteSourceContext& Context : LayerAsset.ImportedAseSources)
		{
			if (FAsepriteImporter::ResolveStoredAsePath(Context.StoredSourcePath)
				.Equals(ResolvedFilePath, ESearchCase::IgnoreCase))
			{
				return &Context;
			}
		}
		return nullptr;
	}
}

FTextureWatcherService& FTextureWatcherService::Get()
{
	static FTextureWatcherService Instance;
	return Instance;
}

void FTextureWatcherService::Initialize()
{
	if (bIsInitialized)
	{
		return;
	}
	if (WatcherRegistrationsByDirectory.Num() > 0 && !UnregisterDirectoryWatchers())
	{
		UE_LOG(LogTemp, Error,
			TEXT("TextureWatcherService: Refusing to restart while prior directory callbacks remain registered."));
		return;
	}
	const uint64 InitializationGeneration = ++LifecycleGeneration;
	bIsInitialized = true;

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Initializing..."));

	// Build initial asset mapping
	BuildSourceFileToAssetMaps();

	// Start watching directories
	if (!RegisterDirectoryWatchers())
	{
		++LifecycleGeneration;
		bIsInitialized = false;
		UnregisterDirectoryWatchers();
		TextureToAssetMap.Empty();
		AseFileToLayerAssetMap.Empty();
		UE_LOG(LogTemp, Error,
			TEXT("TextureWatcherService: Initialization failed because the project Content directory could not be watched."));
		return;
	}

	// TASK-192 U1: heal the gap when Live .ase Auto-Reimport is switched back on. While the setting is
	// off, tracked files can drift (Aseprite saves, git pulls) and their queued events are deliberately
	// dropped; the moment the setting turns on, run the same offline reconcile a fresh editor start
	// runs, so every drifted file re-queues through the standard pending-change path without an editor
	// restart. The watcher itself stays registered while the setting is off — only reactions are gated.
	AseLiveReimportSettingChangedHandle = UPaper2DPlusSettings::OnAseLiveReimportSettingChanged().AddLambda(
		[this, InitializationGeneration]()
	{
		if (!bIsInitialized || LifecycleGeneration != InitializationGeneration)
		{
			return;
		}
		if (UPaper2DPlusSettings::Get()->bEnableAseLiveReimport)
		{
			ReconcileOfflineAseChanges();
		}
	});

	// TASK-183: the registry-tag mapping and the offline reconcile both need the asset registry's
	// initial scan to have completed (module startup usually precedes it, so the map built above is
	// partial). Re-run once the scan finishes, then reconcile files changed while the editor was closed.
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistryFilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddLambda(
			[this, InitializationGeneration]()
		{
			if (!bIsInitialized || LifecycleGeneration != InitializationGeneration)
			{
				return;
			}
			if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
			{
				FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
					.Get().OnFilesLoaded().Remove(AssetRegistryFilesLoadedHandle);
			}
			AssetRegistryFilesLoadedHandle.Reset();

			RefreshAssetMapping();
			ReconcileOfflineAseChanges();
		});
	}
	else
	{
		ReconcileOfflineAseChanges();
	}

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Initialized. Tracking %d texture references, %d .ase source files."),
		TextureToAssetMap.Num(), AseFileToLayerAssetMap.Num());
}

bool FTextureWatcherService::Shutdown()
{
	if (!bIsInitialized)
	{
		const bool bInactiveCleanupSucceeded =
			WatcherRegistrationsByDirectory.Num() == 0 || UnregisterDirectoryWatchers();
		if (!bInactiveCleanupSucceeded)
		{
			UE_LOG(LogTemp, Error,
				TEXT("TextureWatcherService: Inactive shutdown retry could not unregister every retained directory callback."));
		}
		return bInactiveCleanupSucceeded && WatcherRegistrationsByDirectory.Num() == 0;
	}

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Shutting down..."));

	// Invalidate lifecycle-bound registry/timer work before touching any state. DirectoryWatcher
	// ingress itself is synchronous on the editor game-thread tick (see OnDirectoryChanged).
	++LifecycleGeneration;
	bIsInitialized = false;

	// Clear any pending timer
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(BatchTimerHandle);
	}

	// Unbind the one-shot registry hook if the initial scan never completed
	if (AssetRegistryFilesLoadedHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
			.Get().OnFilesLoaded().Remove(AssetRegistryFilesLoadedHandle);
	}
	AssetRegistryFilesLoadedHandle.Reset();

	// Unbind the Live .ase Auto-Reimport transition hook (TASK-192 U1)
	if (AseLiveReimportSettingChangedHandle.IsValid())
	{
		UPaper2DPlusSettings::OnAseLiveReimportSettingChanged().Remove(AseLiveReimportSettingChangedHandle);
	}
	AseLiveReimportSettingChangedHandle.Reset();

	// Stop watching directories
	const bool bAllWatchersUnregistered = UnregisterDirectoryWatchers();

	// Clear state
	TextureToAssetMap.Empty();
	AseFileToLayerAssetMap.Empty();
#if WITH_DEV_AUTOMATION_TESTS
	SuccessfulAseReimportEpochByFile.Empty();
	DroppedAseChangeEpochByFile.Empty();
#endif
	PendingChanges.Empty();

	if (!bAllWatchersUnregistered)
	{
		UE_LOG(LogTemp, Error,
			TEXT("TextureWatcherService: Shutdown left one or more still-owned directory callbacks registered; restart will fail closed until they unregister."));
	}
	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Shutdown complete."));
	return bAllWatchersUnregistered && WatcherRegistrationsByDirectory.Num() == 0;
}

void FTextureWatcherService::RefreshAssetMapping()
{
	BuildSourceFileToAssetMaps();

	// Register watchers for any new external directories discovered from .ase source paths
	for (const auto& Pair : AseFileToLayerAssetMap)
	{
		FString ParentDir = FPaths::GetPath(Pair.Key);
		if (!ParentDir.IsEmpty() && !IsCoveredByProjectContentWatch(ParentDir))
		{
			RegisterExternalDirectory(ParentDir);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Refreshed mapping. Now tracking %d texture references, %d .ase source files."),
		TextureToAssetMap.Num(), AseFileToLayerAssetMap.Num());
}

bool FTextureWatcherService::IsTextureWatched(const FString& TexturePath) const
{
	return TextureToAssetMap.Contains(TexturePath);
}

#if WITH_DEV_AUTOMATION_TESTS
bool FTextureWatcherService::IsPendingChangeDrainedForTests(const FString& FilePath) const
{
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(NormalizedPath);
	return !PendingChanges.Contains(NormalizedPath)
		&& GEditor
		&& !GEditor->GetTimerManager()->IsTimerActive(BatchTimerHandle);
}

uint64 FTextureWatcherService::GetSuccessfulAseReimportEpochForTests(const FString& FilePath) const
{
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(NormalizedPath);
	const uint64* Epoch = SuccessfulAseReimportEpochByFile.Find(NormalizedPath);
	return Epoch ? *Epoch : 0;
}

uint64 FTextureWatcherService::GetDroppedAseChangeEpochForTests(const FString& FilePath) const
{
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(NormalizedPath);
	const uint64* Epoch = DroppedAseChangeEpochByFile.Find(NormalizedPath);
	return Epoch ? *Epoch : 0;
}

bool FTextureWatcherService::IsAseChangePendingForTests(const FString& FilePath) const
{
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(NormalizedPath);
	return PendingChanges.Contains(NormalizedPath);
}

bool FTextureWatcherService::CopyDirectoryWatcherHandleForTests(
	const FString& Directory,
	FDelegateHandle& OutHandle) const
{
	FString NormalizedDirectory = FPaths::ConvertRelativePathToFull(Directory);
	FPaths::NormalizeDirectoryName(NormalizedDirectory);
	const FDirectoryWatcherRegistration* Registration =
		WatcherRegistrationsByDirectory.Find(NormalizedDirectory);
	if (!Registration || !Registration->Handle.IsValid())
	{
		OutHandle.Reset();
		return false;
	}
	OutHandle = Registration->Handle;
	return true;
}

#endif

bool FTextureWatcherService::RegisterDirectoryWatchers()
{
	FDirectoryWatcherModule& DWModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));

	IDirectoryWatcher* Watcher = DWModule.Get();
	if (!Watcher)
	{
		UE_LOG(LogTemp, Error, TEXT("TextureWatcherService: Could not get IDirectoryWatcher"));
		return false;
	}

	// Watch the project's Content directory recursively
	FString ContentDir = FPaths::ProjectContentDir();

	// Normalize the path
	FPaths::NormalizeDirectoryName(ContentDir);
	ContentDir = FPaths::ConvertRelativePathToFull(ContentDir);

	FDelegateHandle Handle;
	const uint64 RegistrationGeneration = LifecycleGeneration;
	TSharedRef<FDirectoryWatcherCallbackLifetime> CallbackLifetime =
		MakeShared<FDirectoryWatcherCallbackLifetime>();
	CallbackLifetime->RegistrationGeneration = RegistrationGeneration;
	bool bSuccess = Watcher->RegisterDirectoryChangedCallback_Handle(
		ContentDir,
		IDirectoryWatcher::FDirectoryChanged::CreateLambda(
			[this, CallbackLifetime](const TArray<FFileChangeData>& Changes)
			{
				OnDirectoryChanged(Changes, CallbackLifetime->RegistrationGeneration);
			}),
		Handle,
		IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges
	);

	if (bSuccess)
	{
		FDirectoryWatcherRegistration Registration;
		Registration.Handle = Handle;
		Registration.CallbackLifetime = CallbackLifetime;
		WatcherRegistrationsByDirectory.Add(ContentDir, MoveTemp(Registration));
		UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Now watching directory: %s"), *ContentDir);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TextureWatcherService: Failed to register watcher for: %s"), *ContentDir);
		return false;
	}

	// Register watchers for external directories where .ase source files live
	TSet<FString> ExternalDirs;
	for (const auto& Pair : AseFileToLayerAssetMap)
	{
		FString ParentDir = FPaths::GetPath(Pair.Key);
		if (!ParentDir.IsEmpty()
			&& !IsCoveredByProjectContentWatch(ParentDir)
			&& !WatcherRegistrationsByDirectory.Contains(ParentDir))
		{
			ExternalDirs.Add(ParentDir);
		}
	}

	for (const FString& ExtDir : ExternalDirs)
	{
		FDelegateHandle ExtHandle;
		TSharedRef<FDirectoryWatcherCallbackLifetime> ExternalCallbackLifetime =
			MakeShared<FDirectoryWatcherCallbackLifetime>();
		ExternalCallbackLifetime->RegistrationGeneration = RegistrationGeneration;
		bool bExtSuccess = Watcher->RegisterDirectoryChangedCallback_Handle(
			ExtDir,
			IDirectoryWatcher::FDirectoryChanged::CreateLambda(
				[this, ExternalCallbackLifetime](const TArray<FFileChangeData>& Changes)
				{
					OnDirectoryChanged(
						Changes,
						ExternalCallbackLifetime->RegistrationGeneration);
				}),
			ExtHandle,
			IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges
		);

		if (bExtSuccess)
		{
			FDirectoryWatcherRegistration Registration;
			Registration.Handle = ExtHandle;
			Registration.CallbackLifetime = ExternalCallbackLifetime;
			WatcherRegistrationsByDirectory.Add(ExtDir, MoveTemp(Registration));
			UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Now watching external directory: %s"), *ExtDir);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Failed to register watcher for external directory: %s"), *ExtDir);
		}
	}

	return true;
}

bool FTextureWatcherService::UnregisterDirectoryWatchers()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher")))
	{
		WatcherRegistrationsByDirectory.Empty();
		return true;
	}

	FDirectoryWatcherModule& DWModule = FModuleManager::GetModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));

	IDirectoryWatcher* Watcher = DWModule.Get();
	if (!Watcher)
	{
		bool bAnyCallbackStillOwned = false;
		for (const auto& Pair : WatcherRegistrationsByDirectory)
		{
			if (Pair.Value.CallbackLifetime.IsValid())
			{
				bAnyCallbackStillOwned = true;
				break;
			}
		}
		if (bAnyCallbackStillOwned)
		{
			UE_LOG(LogTemp, Error,
				TEXT("TextureWatcherService: DirectoryWatcher returned no interface while callbacks still require unregistration."));
			return false;
		}
		WatcherRegistrationsByDirectory.Empty();
		return true;
	}

	// Unregister every callback with its exact directory/handle pair. Unreal returns false both when
	// a live handle was not removed AND when a platform watcher already discarded an inaccessible
	// directory. The delegate-owned lifetime token distinguishes those cases without private-engine APIs.
	TMap<FString, FDirectoryWatcherRegistration> StillOwnedRegistrations;
	for (const auto& Registration : WatcherRegistrationsByDirectory)
	{
		const bool bEngineReportedRemoval = Watcher->UnregisterDirectoryChangedCallback_Handle(
			Registration.Key,
			Registration.Value.Handle);
		if (Registration.Value.CallbackLifetime.IsValid())
		{
			StillOwnedRegistrations.Add(Registration.Key, Registration.Value);
			UE_LOG(LogTemp, Error,
				TEXT("TextureWatcherService: DirectoryWatcher still owns callback for '%s' after unregister (reportedRemoval=%s)."),
				*Registration.Key,
				bEngineReportedRemoval ? TEXT("true") : TEXT("false"));
		}
		else if (!bEngineReportedRemoval)
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("TextureWatcherService: Callback for '%s' was already absent from DirectoryWatcher."),
				*Registration.Key);
		}
	}

	WatcherRegistrationsByDirectory = MoveTemp(StillOwnedRegistrations);
	return WatcherRegistrationsByDirectory.Num() == 0;
}

bool FTextureWatcherService::RegisterExternalDirectory(const FString& DirPath)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error,
			TEXT("TextureWatcherService: Refusing to register an external directory while the service is inactive: %s"),
			*DirPath);
		return false;
	}

	// Normalize the directory path
	FString NormalizedDir = FPaths::ConvertRelativePathToFull(DirPath);
	FPaths::NormalizeDirectoryName(NormalizedDir);
	if (IsCoveredByProjectContentWatch(NormalizedDir))
	{
		return false;
	}

	// Skip if already watched
	if (WatcherRegistrationsByDirectory.Contains(NormalizedDir))
	{
		return false;
	}

	FDirectoryWatcherModule& DWModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* Watcher = DWModule.Get();
	if (!Watcher)
	{
		UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Could not get IDirectoryWatcher for external directory: %s"), *NormalizedDir);
		return false;
	}

	FDelegateHandle Handle;
	const uint64 RegistrationGeneration = LifecycleGeneration;
	TSharedRef<FDirectoryWatcherCallbackLifetime> CallbackLifetime =
		MakeShared<FDirectoryWatcherCallbackLifetime>();
	CallbackLifetime->RegistrationGeneration = RegistrationGeneration;
	bool bSuccess = Watcher->RegisterDirectoryChangedCallback_Handle(
		NormalizedDir,
		IDirectoryWatcher::FDirectoryChanged::CreateLambda(
			[this, CallbackLifetime](const TArray<FFileChangeData>& Changes)
			{
				OnDirectoryChanged(Changes, CallbackLifetime->RegistrationGeneration);
			}),
		Handle,
		IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges
	);

	if (bSuccess)
	{
		FDirectoryWatcherRegistration Registration;
		Registration.Handle = Handle;
		Registration.CallbackLifetime = CallbackLifetime;
		WatcherRegistrationsByDirectory.Add(NormalizedDir, MoveTemp(Registration));
		UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Dynamically registered external directory: %s"), *NormalizedDir);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Failed to register watcher for external directory: %s"), *NormalizedDir);
	}
	return false;
}

void FTextureWatcherService::ReconcileOfflineAseChanges()
{
	// Compare each tracked file's on-disk content against the hash stamped at its last import/
	// reimport. A mismatch means the file changed while no watcher was running (editor closed —
	// typically a git pull of the artist's commit); queue it through the SAME pending-change path
	// a live edit takes so downstream behavior (batching, reimport, conflicts, toast) is identical.
	int32 QueuedCount = 0;
	check(IsInGameThread());

	// TASK-192 U1: while Live .ase Auto-Reimport is disabled the reconcile must queue nothing, or a
	// disabled project would still pay a full reimport per drifted file at startup. The off-to-on
	// setting transition re-runs this reconcile — that is what heals the files skipped here.
	if (!UPaper2DPlusSettings::Get()->bEnableAseLiveReimport)
	{
		UE_LOG(LogTemp, Log,
			TEXT("TextureWatcherService: Live .ase Auto-Reimport is disabled in Project Settings > Paper2DPlus — offline reconcile skipped for %d tracked .ase file(s); re-enable the setting to reimport any that changed."),
			AseFileToLayerAssetMap.Num());
		return;
	}

	for (const auto& Pair : AseFileToLayerAssetMap)
	{
		const FString& FilePath = Pair.Key;
		if (!FPaths::FileExists(FilePath))
		{
			continue;
		}

		// Compare against EVERY tracked (asset, source) stamp for this file: a multi-source Layer
		// Profile records one hash PER SOURCE, and two assets can be stamped at different revisions
		// of the same file. Any disagreeing stamp queues the file; ProcessPendingChanges then skips
		// the entries already up to date, so a redundant queue costs one hash, never a reimport.
		bool bAnySourceStamped = false;
		for (const FAseWatchEntry& Entry : Pair.Value)
		{
			if (!Entry.ImportedHash.IsEmpty())
			{
				bAnySourceStamped = true;
				break;
			}
		}
		if (!bAnySourceStamped)
		{
			continue; // pre-TASK-183 asset: no stamp to compare — live events only
		}

		const FString CurrentHash = FAsepriteImporter::HashAseFileContent(FilePath);
		if (CurrentHash.IsEmpty())
		{
			continue;
		}

		for (const FAseWatchEntry& Entry : Pair.Value)
		{
			if (Entry.ImportedHash.IsEmpty() || Entry.ImportedHash == CurrentHash)
			{
				continue;
			}
			PendingChanges.Add(FilePath);
			QueuedCount++;
			UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Offline change detected for %s (content hash differs from the stamp on %s) — queueing auto-reimport."),
				*FilePath, *Entry.AssetPath.ToString());
			break; // one queue + one count per FILE; per-entry skipping happens downstream
		}
	}

	if (QueuedCount > 0)
	{
		ArmBatchTimer(LifecycleGeneration);
	}
}

void FTextureWatcherService::OnDirectoryChanged(
	const TArray<FFileChangeData>& Changes,
	const uint64 RegistrationGeneration)
{
	// DirectoryWatcher executes delegates from its Tick. This editor service is registered only after
	// GEditor exists, so ingress is already on the game thread. Deferring through AsyncTask here leaves
	// module-owned code queued after ShutdownModule returns and the DLL can be unloaded.
	check(IsInGameThread());
	if (RegistrationGeneration != LifecycleGeneration)
	{
		return;
	}

	// Collect relevant file changes
	TArray<FString> ChangedTextures;
	for (const FFileChangeData& Change : Changes)
	{
		// Modifications AND additions: git materializes a pull as delete+recreate, so an updated
		// source file often arrives as FCA_Added — Modified-only silently missed pulled changes.
		if (Change.Action != FFileChangeData::FCA_Modified && Change.Action != FFileChangeData::FCA_Added)
		{
			continue;
		}

		// Check if this is a file we care about (textures or Aseprite source files)
		FString Extension = FPaths::GetExtension(Change.Filename).ToLower();
		if (Extension != TEXT("png") && Extension != TEXT("tga") &&
			Extension != TEXT("psd") && Extension != TEXT("bmp") &&
			Extension != TEXT("jpg") && Extension != TEXT("jpeg") &&
			Extension != TEXT("ase") && Extension != TEXT("aseprite"))
		{
			continue;
		}

		// Normalize the path for consistent comparison
		FString NormalizedPath = FPaths::ConvertRelativePathToFull(Change.Filename);
		FPaths::NormalizeFilename(NormalizedPath);
		ChangedTextures.Add(NormalizedPath);
	}

	if (ChangedTextures.Num() == 0)
	{
		return;
	}

	if (!bIsInitialized || RegistrationGeneration != LifecycleGeneration)
	{
		return;
	}

	for (const FString& Path : ChangedTextures)
	{
		PendingChanges.Add(Path);
		UE_LOG(LogTemp, Verbose, TEXT("TextureWatcherService: Detected change to: %s"), *Path);
	}

	ArmBatchTimer(RegistrationGeneration);
}

void FTextureWatcherService::ArmBatchTimer(const uint64 ExpectedGeneration)
{
	if (!GEditor || !bIsInitialized || ExpectedGeneration != LifecycleGeneration)
	{
		return;
	}
	GEditor->GetTimerManager()->ClearTimer(BatchTimerHandle);
	GEditor->GetTimerManager()->SetTimer(
		BatchTimerHandle,
		FTimerDelegate::CreateLambda([this, ExpectedGeneration]()
		{
			if (bIsInitialized && ExpectedGeneration == LifecycleGeneration)
			{
				ProcessPendingChanges();
			}
		}),
		0.5f,
		false);
}

void FTextureWatcherService::BuildSourceFileToAssetMaps()
{
	TextureToAssetMap.Empty();
	AseFileToLayerAssetMap.Empty();

	// Get the asset registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Find all Paper2DPlusCharacterProfileAsset instances
	TArray<FAssetData> AssetList;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	AssetRegistry.GetAssetsByClass(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetFName(), AssetList);
#else
	AssetRegistry.GetAssetsByClass(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName(), AssetList);
#endif

	for (const FAssetData& AssetData : AssetList)
	{
		// Only use already-loaded assets — don't force-load every character profile into memory
		UPaper2DPlusCharacterProfileAsset* Asset = Cast<UPaper2DPlusCharacterProfileAsset>(AssetData.FastGetAsset(/*bEvenIfPendingKill=*/false));
		if (!Asset)
		{
			continue;
		}

		// Iterate through all flipbooks
		for (int32 FlipbookIndex = 0; FlipbookIndex < Asset->Flipbooks.Num(); FlipbookIndex++)
		{
			const FFlipbookProfileEntry& FlipbookData = Asset->Flipbooks[FlipbookIndex];

			// Skip flipbooks without source texture
			if (FlipbookData.SourceTexture.IsNull())
			{
				continue;
			}

			// Get the file system path for this texture
			const FSoftObjectPath TexturePath = FlipbookData.SourceTexture.ToSoftObjectPath();
			FString PackagePath = TexturePath.GetLongPackageName();
			if (PackagePath.IsEmpty())
			{
				PackagePath = FPackageName::ObjectPathToPackageName(TexturePath.ToString());
			}

			FString FilePath = GetFileSystemPathForTexture(PackagePath);

			if (!FilePath.IsEmpty())
			{
				// Add to the map
				TextureToAssetMap.FindOrAdd(FilePath).Add(
					TPair<TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, int32>(Asset, FlipbookIndex)
				);

				UE_LOG(LogTemp, Verbose, TEXT("TextureWatcherService: Mapped %s -> %s::%s"),
					*FilePath, *Asset->GetName(), *FlipbookData.Identity.FlipbookName);
			}
		}
	}

	// --- Build .ase source file -> CharacterLayerAsset mapping ---

	TArray<FAssetData> LayerAssetList;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	AssetRegistry.GetAssetsByClass(UPaper2DPlusCharacterLayerAsset::StaticClass()->GetFName(), LayerAssetList);
#else
	AssetRegistry.GetAssetsByClass(UPaper2DPlusCharacterLayerAsset::StaticClass()->GetClassPathName(), LayerAssetList);
#endif

	for (const FAssetData& AssetData : LayerAssetList)
	{
		// TASK-183/189: map from ASSET REGISTRY TAGS so saved assets are tracked WITHOUT loading them.
		// Both tags are NEWLINE-joined LISTS for a multi-source Layer Profile (one element per
		// contributing .ase, index-parallel between the two tags); a legacy single-source asset emits
		// the bare value and parses as a one-element list. The asset loads lazily in
		// ProcessPendingChanges, only when one of its sources actually changes.
		FString StoredPathTagValue;
		FString StoredHashTagValue;
		AssetData.GetTagValue(TEXT("Paper2DPlus.SourceAseFile"), StoredPathTagValue);
		AssetData.GetTagValue(TEXT("Paper2DPlus.SourceAseHash"), StoredHashTagValue);

		TArray<FString> StoredPaths;
		TArray<FString> StoredHashes;
		UPaper2DPlusCharacterLayerAsset::ParseAseSourceTagList(StoredPathTagValue, StoredPaths);
		UPaper2DPlusCharacterLayerAsset::ParseAseSourceTagList(StoredHashTagValue, StoredHashes);

		if (StoredPaths.Num() == 0)
		{
			// Legacy fallback: assets saved before the registry tags existed expose their sources only
			// on the loaded object (never force-load for mapping). ImportedAseSources is authoritative
			// when populated; element 0 is the primary that the legacy fields mirror.
			const UPaper2DPlusCharacterLayerAsset* Loaded =
				Cast<UPaper2DPlusCharacterLayerAsset>(AssetData.FastGetAsset(/*bEvenIfPendingKill=*/false));
			if (!Loaded)
			{
				continue;
			}
			for (const FAsepriteSourceContext& Context : Loaded->ImportedAseSources)
			{
				StoredPaths.Add(Context.StoredSourcePath);
				StoredHashes.Add(Context.ContentHash);
			}
			if (StoredPaths.Num() == 0)
			{
				if (Loaded->SourceAseFilePath.IsEmpty())
				{
					continue;
				}
				StoredPaths.Add(Loaded->SourceAseFilePath);
				StoredHashes.Add(Loaded->ImportedAseContentHash);
			}
		}
		else if (StoredHashes.Num() != StoredPaths.Num())
		{
			// ONE unstamped source joins to an EMPTY hash tag, which parses to zero elements — the
			// legitimate pre-TASK-183 shape, not a desync (two or more empty hashes still keep their
			// count, because the joining newlines survive). Normalize exactly that case.
			if (StoredHashes.Num() == 0 && StoredPaths.Num() == 1)
			{
				StoredHashes.AddDefaulted();
			}
			else
			{
				// FAIL LOUDLY: index parity is the ONLY thing binding a source to its stamp. Guessing
				// would hand one source another source's hash and then silently reimport — or silently
				// skip — forever.
				UE_LOG(LogTemp, Error,
					TEXT("TextureWatcherService: '%s' declares %d source .ase path(s) but %d hash(es) in its registry tags — SKIPPING it; none of its source files will be watched until it is re-imported or re-saved."),
					*AssetData.PackageName.ToString(), StoredPaths.Num(), StoredHashes.Num());
				continue;
			}
		}

		for (int32 SourceIndex = 0; SourceIndex < StoredPaths.Num(); ++SourceIndex)
		{
			const FString& StoredPath = StoredPaths[SourceIndex];
			if (StoredPath.IsEmpty())
			{
				continue; // an empty slot preserves index parity but names no file
			}

			// Stored paths may be project-relative (the cross-machine form) — resolve against the project
			const FString NormalizedAsePath = FAsepriteImporter::ResolveStoredAsePath(StoredPath);
			if (NormalizedAsePath.IsEmpty())
			{
				continue;
			}

			FAseWatchEntry Entry;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
			Entry.AssetPath = AssetData.ToSoftObjectPath();
#else
			Entry.AssetPath = AssetData.GetSoftObjectPath();
#endif
			Entry.StoredSourcePath = StoredPath;
			Entry.ImportedHash = StoredHashes[SourceIndex];
			AseFileToLayerAssetMap.FindOrAdd(NormalizedAsePath).Add(MoveTemp(Entry));

			UE_LOG(LogTemp, Verbose, TEXT("TextureWatcherService: Mapped .ase %s -> %s (source %d/%d)"),
				*NormalizedAsePath, *AssetData.AssetName.ToString(), SourceIndex + 1, StoredPaths.Num());
		}
	}
}

FString FTextureWatcherService::GetFileSystemPathForTexture(const FString& AssetPath)
{
	// Convert UE package/object path to a long package name (e.g. /Game/Textures/MyTexture)
	FString PackagePath = AssetPath;
	if (PackagePath.Contains(TEXT(".")))
	{
		PackagePath = FPackageName::ObjectPathToPackageName(PackagePath);
	}

	if (PackagePath.IsEmpty())
	{
		return FString();
	}

	FString FilePath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackagePath, FilePath))
	{
		return FString();
	}

	// The FilePath now contains the path without extension
	// Try common source image extensions
	static const TArray<FString> Extensions = { TEXT(".png"), TEXT(".tga"), TEXT(".psd"), TEXT(".bmp"), TEXT(".jpg"), TEXT(".jpeg") };

	for (const FString& Ext : Extensions)
	{
		FString FullPath = FilePath + Ext;
		FullPath = FPaths::ConvertRelativePathToFull(FullPath);
		FPaths::NormalizeFilename(FullPath);

		if (FPaths::FileExists(FullPath))
		{
			return FullPath;
		}
	}

	// Also check if it's a .uasset and look for source file nearby
	FString UAssetPath = FPaths::ConvertRelativePathToFull(FilePath + TEXT(".uasset"));
	FPaths::NormalizeFilename(UAssetPath);
	if (FPaths::FileExists(UAssetPath))
	{
		// Source file might be in the same directory with image extension
		FString Directory = FPaths::GetPath(FilePath);
		FString BaseName = FPaths::GetBaseFilename(FilePath);

		for (const FString& Ext : Extensions)
		{
			FString SourcePath = Directory / BaseName + Ext;
			SourcePath = FPaths::ConvertRelativePathToFull(SourcePath);
			FPaths::NormalizeFilename(SourcePath);

			if (FPaths::FileExists(SourcePath))
			{
				return SourcePath;
			}
		}
	}

	return FString();
}

TArray<TPair<UPaper2DPlusCharacterProfileAsset*, int32>> FTextureWatcherService::FindAffectedFlipbooks(const FString& TexturePath)
{
	TArray<TPair<UPaper2DPlusCharacterProfileAsset*, int32>> Result;

	// Normalize the path for comparison
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(TexturePath);
	FPaths::NormalizeFilename(NormalizedPath);

	// Look up in our map
	if (const auto* Found = TextureToAssetMap.Find(NormalizedPath))
	{
		int32 InvalidCount = 0;
		for (const auto& Pair : *Found)
		{
			if (UPaper2DPlusCharacterProfileAsset* Asset = Pair.Key.Get())
			{
				// Validate that the flipbook index is still valid
				if (Asset->Flipbooks.IsValidIndex(Pair.Value))
				{
					Result.Add(TPair<UPaper2DPlusCharacterProfileAsset*, int32>(Asset, Pair.Value));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Flipbook index %d no longer valid for asset %s"),
						Pair.Value, *Asset->GetName());
					InvalidCount++;
				}
			}
			else
			{
				InvalidCount++;
			}
		}

		// Prune stale entries in-place instead of accumulating dead weak pointers
		if (InvalidCount > 0)
		{
			auto& Entries = const_cast<TArray<TPair<TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, int32>>&>(*Found);
			Entries.RemoveAll([](const auto& P) { return !P.Key.IsValid(); });
		}
	}

	return Result;
}

void FTextureWatcherService::ProcessPendingChanges()
{
	if (!bIsInitialized)
	{
		return;
	}
	check(IsInGameThread());
	TSet<FString> ChangesToProcess;
	if (PendingChanges.Num() == 0)
	{
		return;
	}
	ChangesToProcess = MoveTemp(PendingChanges);
	PendingChanges.Reset();

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Processing %d pending file changes"), ChangesToProcess.Num());

	// Master conflict list across all reimports in this batch
	TArray<FReimportConflict> AllConflicts;

	// Tracking for notification summary
	int32 TotalLayersUpdated = 0;
	int32 TotalSpritesUpdated = 0;
	int32 AseFilesProcessed = 0;
	int32 TextureFilesProcessed = 0;
	TArray<FString> AffectedAssetNames;
	bool bAnyFailure = false;

	// NOT a transaction: the reimport loops below rebuild texture Source bulk data in place and
	// create new asset packages (FAsepriteReimporter::ReimportFromAseFile / FTextureReimporter),
	// neither of which is undoable — an FScopedTransaction wrapping them (and the modal
	// SReimportConflictDialog) would make undo mismatch and orphan the created packages. The
	// conflict-apply path Modify()+MarkPackageDirty per asset below, so OnObjectModified still
	// fires for the editor reconcile.

	// --- Process .ase file changes ---

	// TASK-192 U1: the Live .ase Auto-Reimport setting gates ONLY this .ase branch — the texture
	// (non-.ase) loop below never consults it, and the watcher stays registered so re-enabling needs
	// no restart. Dropping queued changes here is safe: the off-to-on transition (and every editor
	// start once re-enabled) re-runs the offline reconcile, which re-queues any file whose on-disk
	// content hash disagrees with its imported stamp.
	const bool bLiveAseReimportEnabled = UPaper2DPlusSettings::Get()->bEnableAseLiveReimport;
	int32 DroppedAseFileCount = 0;

	for (const FString& FilePath : ChangesToProcess)
	{
		FString Extension = FPaths::GetExtension(FilePath).ToLower();
		if (Extension != TEXT("ase") && Extension != TEXT("aseprite"))
		{
			continue;
		}

		// Look up affected CharacterLayerAssets. ITERATE A COPY: the full-reimport path below runs
		// ImportAsLayeredAsset, whose tail calls RefreshAssetMapping() — which rebuilds this map and
		// would dangle a live pointer/iterator mid-loop.
		TArray<FAseWatchEntry> EntriesCopy;
		if (const TArray<FAseWatchEntry>* FoundAssets = AseFileToLayerAssetMap.Find(FilePath))
		{
			EntriesCopy = *FoundAssets;
		}
		if (EntriesCopy.Num() == 0)
		{
			continue;
		}

		// TASK-192 U1: a mapped .ase changed while the setting is off — drop it here, before hashing.
		// The reconcile on re-enable is the heal path; nothing is lost by discarding the queue entry.
		if (!bLiveAseReimportEnabled)
		{
			DroppedAseFileCount++;
#if WITH_DEV_AUTOMATION_TESTS
			DroppedAseChangeEpochByFile.Add(FilePath, ++DroppedAseChangeEpoch);
#endif
			continue;
		}

		// Hash once per changed file: a change event whose content matches an asset's last-import stamp
		// is an ECHO of our own work (the import's SourceArt copy, a duplicate FCA_Added+Modified pair)
		// and must not re-run the whole import the user just watched finish.
		const FString OnDiskHash = FAsepriteImporter::HashAseFileContent(FilePath);

		bool bProcessedAny = false;

		for (const FAseWatchEntry& WatchEntry : EntriesCopy)
		{
			if (!OnDiskHash.IsEmpty() && OnDiskHash == WatchEntry.ImportedHash)
			{
				UE_LOG(LogTemp, Verbose, TEXT("Auto-reimport: %s unchanged since last import (hash match) — skipping."), *FilePath);
				continue;
			}

			// Lazy load: the mapping is registry-tag based, so the asset may not be in memory yet —
			// this is the one place a tracked layer asset is loaded, and only because its file changed.
			UPaper2DPlusCharacterLayerAsset* LayerAsset =
				Cast<UPaper2DPlusCharacterLayerAsset>(WatchEntry.AssetPath.TryLoad());
			if (!LayerAsset)
			{
				continue;
			}

			// TASK-189: which of this asset's sources is the changed file? A multi-source Layer Profile
			// keeps a separate prefix / disabled-tag / hash record per source; reimporting with element
			// 0's record would regenerate this source's assets under another file's prefix. Copy the
			// values out NOW — the import below calls UpsertAseSourceContext, which can grow (and so
			// reallocate) ImportedAseSources, dangling any pointer held across it.
			const FAsepriteSourceContext* SourceContext =
				FindAseSourceContextForEntry(*LayerAsset, WatchEntry.StoredSourcePath, FilePath);
			const FString SourceLiveHash =
				SourceContext ? SourceContext->ContentHash : LayerAsset->ImportedAseContentHash;
			const FString SourceAssetPrefix =
				SourceContext ? SourceContext->AssetPrefix : LayerAsset->ImportAssetPrefix;
			const TArray<FString> SourceDisabledTagNames =
				SourceContext ? SourceContext->DisabledTagNames : LayerAsset->ImportDisabledTagNames;

			// The registry-tag hash can lag the loaded asset's live value (tags refresh on save) —
			// re-check against THIS SOURCE's authoritative in-memory stamp before reimporting.
			if (!OnDiskHash.IsEmpty() && OnDiskHash == SourceLiveHash)
			{
				UE_LOG(LogTemp, Verbose, TEXT("Auto-reimport: %s unchanged since last import (live hash match) — skipping."), *FilePath);
				continue;
			}

			bool bThisSucceeded = false;

			if (!SourceAssetPrefix.IsEmpty() && !LayerAsset->ImportOutputPath.IsEmpty())
			{
				// FULL auto-reimport (TASK-186): re-run the whole import pipeline with the context the
				// import stamped on the asset — sheet texture, per-frame sprites, per-tag flipbooks
				// (new tags become new flipbooks), additive profile delivery, and layer refresh all
				// update in place, so an Aseprite save reflects everywhere the art is used.
				UE_LOG(LogTemp, Log, TEXT("Auto-reimport: Full re-import of %s -> %s (and its sheet/sprites/flipbooks/profile)"),
					*FilePath, *LayerAsset->GetName());

				// TASK-192 U2: own the cost report for this file's whole reimport — parse, per-layer
				// composite, and everything ImportAsLayeredAsset does — and emit the one-line summary
				// when the scope closes, so every auto-reimport's cost is diagnosable from the log.
				FAsepriteImportCostScope CostScope;

				FAsepriteParsedData Parsed;
				FString ParseError;
				bool bParsedOk = false;
				{
					FAsepriteImportCostPhaseTimer ParsePhase(EAsepriteImportCostPhase::Parse);
					bParsedOk = FAsepriteImporter::ParseFile(FilePath, Parsed, ParseError);
				}
				if (bParsedOk)
				{
					TMap<int32, TArray<TArray<FColor>>> PerLayerBuffers;
					{
						FAsepriteImportCostPhaseTimer CompositePhase(EAsepriteImportCostPhase::Composite);
						PerLayerBuffers = FAsepriteImporter::CompositePerLayer(Parsed);
					}

					FAsepriteLayerImportSettings ReimportSettings;
					FAsepriteImporter::InitDefaultSelection(Parsed, ReimportSettings);

					// Re-apply THIS SOURCE's import-time tag de-selection BY NAME (stable across
					// reordering; per-source since TASK-189 — a sibling .ase's disabled tags must not
					// leak in here)
					for (int32 TagIdx = 0; TagIdx < Parsed.Tags.Num(); ++TagIdx)
					{
						if (SourceDisabledTagNames.Contains(Parsed.Tags[TagIdx].Name))
						{
							ReimportSettings.TagImportEnabled.Add(TagIdx, false);
						}
					}

					ReimportSettings.ImportMode = LayerAsset->BaseProfile.IsNull()
						? EAsepriteImportMode::LayerAssetNewProfile
						: EAsepriteImportMode::LayerAssetExistingProfile;
					ReimportSettings.ExistingProfile = LayerAsset->BaseProfile;
					ReimportSettings.ExistingLayerAsset = LayerAsset;
					ReimportSettings.OutputPath = LayerAsset->ImportOutputPath;
					ReimportSettings.AssetPrefix = SourceAssetPrefix;
					ReimportSettings.bOrganizeIntoSubfolders = LayerAsset->bImportOrganizeIntoSubfolders;
					ReimportSettings.bKeepSourceInProject = false; // the changed file IS the tracked source
					ReimportSettings.bUserConfirmed = true;
					ReimportSettings.SourceFilePath = FilePath;

					bThisSucceeded = FAsepriteImporter::ImportAsLayeredAsset(Parsed, PerLayerBuffers, ReimportSettings) != nullptr;
					if (bThisSucceeded)
					{
						TotalLayersUpdated += LayerAsset->Layers.Num();
						// TASK-192 U8: every auto-reimport leaves a walkable, non-modal audit page
						// naming each write/skip decision with its reason.
						FAsepriteImporter::PublishIncrementalAuditPage(
							LayerAsset->GetName(), CostScope.GetReport());
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Auto-reimport: parse failed for %s: %s"), *FilePath, *ParseError);
				}
			}
			else
			{
				// Legacy asset (no import context): the original layer-only diff reimport
				UE_LOG(LogTemp, Log, TEXT("Auto-reimport: Reimporting .ase -> %s"), *LayerAsset->GetName());

				FAsepriteReimportResult Result = FAsepriteReimporter::ReimportFromAseFile(FilePath, LayerAsset);
				bThisSucceeded = Result.bSuccess;
				if (Result.bSuccess)
				{
					TotalLayersUpdated += Result.LayersUpdated + Result.LayersAdded;
					TotalSpritesUpdated += Result.SpritesUpdated;
				}

				// Collect conflicts + warnings (legacy path only — the full path surfaces its own UI)
				AllConflicts.Append(Result.Conflicts);
				for (const FString& Warning : Result.Warnings)
				{
					UE_LOG(LogTemp, Warning, TEXT("Auto-reimport: %s"), *Warning);
				}
			}

			if (bThisSucceeded)
			{
				AffectedAssetNames.AddUnique(LayerAsset->DisplayName.IsEmpty()
					? LayerAsset->GetName() : LayerAsset->DisplayName);
				// Keep the live map entry (re-looked-up — the full path rebuilt the map) in step with
				// the hash the reimport just re-stamped for THIS source, so later checks compare fresh
				// state. Re-find the context: the import may have reallocated ImportedAseSources, so
				// the pointer taken before it is not safe to reuse.
				const FAsepriteSourceContext* RestampedContext =
					FindAseSourceContextForEntry(*LayerAsset, WatchEntry.StoredSourcePath, FilePath);
				const FString RestampedHash =
					RestampedContext ? RestampedContext->ContentHash : LayerAsset->ImportedAseContentHash;
				if (TArray<FAseWatchEntry>* LiveEntries = AseFileToLayerAssetMap.Find(FilePath))
				{
					for (FAseWatchEntry& LiveEntry : *LiveEntries)
					{
						if (LiveEntry.AssetPath == WatchEntry.AssetPath)
						{
							LiveEntry.ImportedHash = RestampedHash;
						}
					}
				}
				bProcessedAny = true;
			}
			else
			{
				bAnyFailure = true;
				UE_LOG(LogTemp, Warning, TEXT("Auto-reimport: Failed to reimport .ase for %s"), *LayerAsset->GetName());
			}
		}

		if (bProcessedAny)
		{
			AseFilesProcessed++;
#if WITH_DEV_AUTOMATION_TESTS
			SuccessfulAseReimportEpochByFile.Add(FilePath, ++SuccessfulAseReimportEpoch);
#endif
		}
	}

	// One line per batch, not per file — enough to make "nothing happened" diagnosable from the log
	// without spamming a disabled project's editor session (TASK-192 U1).
	if (DroppedAseFileCount > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("TextureWatcherService: Live .ase Auto-Reimport is disabled in Project Settings > Paper2DPlus — dropped %d changed .ase file(s) without reimporting; re-enable the setting to reconcile them."),
			DroppedAseFileCount);
	}

	// --- Process texture file changes ---

	for (const FString& FilePath : ChangesToProcess)
	{
		// Skip .ase files — already handled above
		FString Extension = FPaths::GetExtension(FilePath).ToLower();
		if (Extension == TEXT("ase") || Extension == TEXT("aseprite"))
		{
			continue;
		}

		TArray<TPair<UPaper2DPlusCharacterProfileAsset*, int32>> Affected = FindAffectedFlipbooks(FilePath);
		if (Affected.Num() == 0)
		{
			continue;
		}

		bool bProcessedAny = false;

		for (const auto& AffectedPair : Affected)
		{
			UPaper2DPlusCharacterProfileAsset* Profile = AffectedPair.Key;
			int32 FlipbookIndex = AffectedPair.Value;

			if (!Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex))
			{
				continue;
			}

			UE_LOG(LogTemp, Log, TEXT("Auto-reimport: Reimporting texture -> %s::%s"),
				*Profile->GetName(), *Profile->Flipbooks[FlipbookIndex].Identity.FlipbookName);

			FTextureReimportResult Result = FTextureReimporter::ReimportFromTexture(FilePath, Profile, FlipbookIndex);

			if (Result.bSuccess)
			{
				TotalSpritesUpdated += Result.SpritesUpdated;
				AffectedAssetNames.AddUnique(Profile->GetName());
				bProcessedAny = true;
			}
			else
			{
				bAnyFailure = true;
				UE_LOG(LogTemp, Warning, TEXT("Auto-reimport: Failed to reimport texture for %s::%s"),
					*Profile->GetName(), *Profile->Flipbooks[FlipbookIndex].Identity.FlipbookName);
			}

			// Collect conflicts
			AllConflicts.Append(Result.Conflicts);

			// Log warnings
			for (const FString& Warning : Result.Warnings)
			{
				UE_LOG(LogTemp, Warning, TEXT("Auto-reimport: %s"), *Warning);
			}
		}

		if (bProcessedAny)
		{
			TextureFilesProcessed++;
		}
	}

	// --- Conflict dialog ---

	if (AllConflicts.Num() > 0)
	{
		if (!SReimportConflictDialog::IsDialogOpen())
		{
			UE_LOG(LogTemp, Log, TEXT("Auto-reimport: %d conflicts detected, showing dialog"), AllConflicts.Num());

			bool bConfirmed = SReimportConflictDialog::ShowConflictDialog(AllConflicts);

			if (bConfirmed)
			{
				// Apply conflict resolutions. Tracks which flipbooks have already had a forced
				// bounds re-apply this batch so multiple per-frame conflicts on one flipbook
				// don't trigger redundant re-detections (U16).
				TSet<FString> ForcedReapplyKeys;
				for (const FReimportConflict& Conflict : AllConflicts)
				{
					switch (Conflict.Type)
					{
					case EReimportConflictType::LayerDeleted:
						if (Conflict.Resolution == EReimportConflictResolution::RemoveOld)
						{
							// Find and remove the orphaned layer from any matching LayerAsset
							for (auto& AsePair : AseFileToLayerAssetMap)
							{
								for (const FAseWatchEntry& WatchedEntry : AsePair.Value)
								{
									// ResolveObject, not TryLoad: conflict resolutions only apply to assets the
									// reimport loop above already loaded — never force-load the rest of the map.
									if (UPaper2DPlusCharacterLayerAsset* LayerAsset =
										Cast<UPaper2DPlusCharacterLayerAsset>(WatchedEntry.AssetPath.ResolveObject()))
									{
										int32 RemoveIdx = LayerAsset->Layers.IndexOfByPredicate(
											[&Conflict](const FCharacterLayer& L) { return L.LayerName == Conflict.OldName; });
										if (RemoveIdx != INDEX_NONE)
										{
											LayerAsset->Modify();
											LayerAsset->Layers.RemoveAt(RemoveIdx);
											LayerAsset->MarkPackageDirty();
											UE_LOG(LogTemp, Log, TEXT("Auto-reimport: Removed orphaned layer '%s'"), *Conflict.OldName);
										}
									}
								}
							}
						}
						// KeepOrphaned: no action (layer stays with orphaned data)
						break;

					case EReimportConflictType::LayerRenamed:
						if (Conflict.Resolution == EReimportConflictResolution::AcceptRename)
						{
							// Update LayerName on the old-named layer to the new name
							for (auto& AsePair : AseFileToLayerAssetMap)
							{
								for (const FAseWatchEntry& WatchedEntry : AsePair.Value)
								{
									// ResolveObject, not TryLoad: conflict resolutions only apply to assets the
									// reimport loop above already loaded — never force-load the rest of the map.
									if (UPaper2DPlusCharacterLayerAsset* LayerAsset =
										Cast<UPaper2DPlusCharacterLayerAsset>(WatchedEntry.AssetPath.ResolveObject()))
									{
										if (FCharacterLayer* Layer = LayerAsset->GetLayerByNameMutable(Conflict.OldName))
										{
											LayerAsset->Modify();
											Layer->LayerName = Conflict.NewName;
											LayerAsset->MarkPackageDirty();
											UE_LOG(LogTemp, Log, TEXT("Auto-reimport: Renamed layer '%s' -> '%s'"),
												*Conflict.OldName, *Conflict.NewName);
										}
									}
								}
							}
						}
						else if (Conflict.Resolution == EReimportConflictResolution::RemoveOld)
						{
							// Remove the old-named layer (new one was already added by reimporter)
							for (auto& AsePair : AseFileToLayerAssetMap)
							{
								for (const FAseWatchEntry& WatchedEntry : AsePair.Value)
								{
									// ResolveObject, not TryLoad: conflict resolutions only apply to assets the
									// reimport loop above already loaded — never force-load the rest of the map.
									if (UPaper2DPlusCharacterLayerAsset* LayerAsset =
										Cast<UPaper2DPlusCharacterLayerAsset>(WatchedEntry.AssetPath.ResolveObject()))
									{
										int32 RemoveIdx = LayerAsset->Layers.IndexOfByPredicate(
											[&Conflict](const FCharacterLayer& L) { return L.LayerName == Conflict.OldName; });
										if (RemoveIdx != INDEX_NONE)
										{
											LayerAsset->Modify();
											LayerAsset->Layers.RemoveAt(RemoveIdx);
											LayerAsset->MarkPackageDirty();
											UE_LOG(LogTemp, Log, TEXT("Auto-reimport: Removed old layer '%s' (renamed to '%s')"),
												*Conflict.OldName, *Conflict.NewName);
										}
									}
								}
							}
						}
						// KeepBoth: no action (old stays, new was already added by reimporter)
						break;

					case EReimportConflictType::UniformBoundsInstability:
						if (Conflict.Resolution == EReimportConflictResolution::AcceptNew)
						{
							// Re-run the texture reimport for the conflicting flipbook with the stability
							// gate bypassed so the user-accepted new bounds actually apply (U16).
							UPaper2DPlusCharacterProfileAsset* ConflictProfile = Conflict.SourceProfile.Get();
							if (ConflictProfile && ConflictProfile->Flipbooks.IsValidIndex(Conflict.SourceFlipbookIndex))
							{
								// A flipbook can raise one conflict per unstable frame, all carrying the
								// same source. The first forced re-apply rewrites every frame in one pass,
								// so coalesce repeats for the same flipbook to avoid redundant re-detection
								// and duplicate undo entries (U16).
								const FString ReapplyKey = FString::Printf(TEXT("%s#%d"),
									*ConflictProfile->GetPathName(), Conflict.SourceFlipbookIndex);
								if (!ForcedReapplyKeys.Contains(ReapplyKey))
								{
									ForcedReapplyKeys.Add(ReapplyKey);

									// Gather every frame the user ACCEPTED for this flipbook so sibling
									// frames they resolved as "Keep Current" are preserved, not overwritten (U16).
									TSet<int32> AcceptedFrames;
									for (const FReimportConflict& Sibling : AllConflicts)
									{
										if (Sibling.Type == EReimportConflictType::UniformBoundsInstability
											&& Sibling.Resolution == EReimportConflictResolution::AcceptNew
											&& Sibling.SourceProfile.Get() == ConflictProfile
											&& Sibling.SourceFlipbookIndex == Conflict.SourceFlipbookIndex
											&& Sibling.SourceFrameIndex != INDEX_NONE)
										{
											AcceptedFrames.Add(Sibling.SourceFrameIndex);
										}
									}

									FTextureReimportResult ForcedResult = FTextureReimporter::ReimportFromTexture(
										Conflict.SourceTextureFilePath, ConflictProfile, Conflict.SourceFlipbookIndex,
										/*bForceSkipStabilityCheck=*/true,
										AcceptedFrames.Num() > 0 ? &AcceptedFrames : nullptr);

									if (ForcedResult.bSuccess)
									{
										TotalSpritesUpdated += ForcedResult.SpritesUpdated;
										AffectedAssetNames.AddUnique(ConflictProfile->GetName());
										UE_LOG(LogTemp, Log, TEXT("Auto-reimport: Accepted new bounds for '%s' — %d sprite(s) updated"),
											*Conflict.OldName, ForcedResult.SpritesUpdated);
									}
									else
									{
										UE_LOG(LogTemp, Warning, TEXT("Auto-reimport: Force-reapply for '%s' updated no sprites"), *Conflict.OldName);
									}

									for (const FString& ForcedWarning : ForcedResult.Warnings)
									{
										UE_LOG(LogTemp, Warning, TEXT("Auto-reimport: %s"), *ForcedWarning);
									}
								}
							}
							else
							{
								UE_LOG(LogTemp, Warning, TEXT("Auto-reimport: Cannot re-apply accepted bounds for '%s' — source profile/flipbook no longer valid"), *Conflict.OldName);
							}
						}
						// KeepCurrent: no action (sprites keep current bounds)
						break;

					default:
						break;
					}
				}
			}
		}
		else
		{
			// Dialog already open — log conflicts, they will be picked up on next cycle
			UE_LOG(LogTemp, Log, TEXT("Auto-reimport: %d conflicts detected but dialog is already open. Conflicts logged only."),
				AllConflicts.Num());
			for (const FReimportConflict& Conflict : AllConflicts)
			{
				const TCHAR* TypeStr = TEXT("Unknown");
				switch (Conflict.Type)
				{
				case EReimportConflictType::LayerDeleted:             TypeStr = TEXT("LayerDeleted"); break;
				case EReimportConflictType::LayerRenamed:             TypeStr = TEXT("LayerRenamed"); break;
				case EReimportConflictType::TagRenamed:               TypeStr = TEXT("TagRenamed"); break;
				case EReimportConflictType::UniformBoundsInstability: TypeStr = TEXT("UniformBoundsInstability"); break;
				}
				UE_LOG(LogTemp, Warning, TEXT("  Conflict: [%s] %s -> %s: %s"),
					TypeStr, *Conflict.OldName, *Conflict.NewName, *Conflict.Description);
			}
		}
	}

	// --- Editor cascade via FScopedEditorModelMutation ---

	if (TotalLayersUpdated > 0 || TotalSpritesUpdated > 0)
	{
		TSharedPtr<FCharacterProfileEditorModel> EditorModel = FCharacterProfileEditorModel::GActiveEditorModel.Pin();
		if (EditorModel.IsValid())
		{
			FScopedEditorModelMutation MutationScope(EditorModel);
			EditorModel->OnAssetExternallyModified.Broadcast();
			UE_LOG(LogTemp, Log, TEXT("Auto-reimport: Fired OnAssetExternallyModified on active editor model"));
		}
	}

	// --- Toast notification ---

	if (AseFilesProcessed > 0 || TextureFilesProcessed > 0)
	{
		FString Summary;

		// Build asset names summary
		FString AssetNamesStr;
		if (AffectedAssetNames.Num() == 1)
		{
			AssetNamesStr = AffectedAssetNames[0];
		}
		else if (AffectedAssetNames.Num() <= 3)
		{
			AssetNamesStr = FString::Join(AffectedAssetNames, TEXT(", "));
		}
		else
		{
			AssetNamesStr = FString::Printf(TEXT("%s and %d more"),
				*AffectedAssetNames[0], AffectedAssetNames.Num() - 1);
		}

		if (TotalLayersUpdated > 0 && TotalSpritesUpdated > 0)
		{
			Summary = FString::Printf(TEXT("Auto-reimport: updated %d layers, %d sprites in %s"),
				TotalLayersUpdated, TotalSpritesUpdated, *AssetNamesStr);
		}
		else if (TotalLayersUpdated > 0)
		{
			Summary = FString::Printf(TEXT("Auto-reimport: updated %d layers in %s"),
				TotalLayersUpdated, *AssetNamesStr);
		}
		else if (TotalSpritesUpdated > 0)
		{
			Summary = FString::Printf(TEXT("Auto-reimport: updated %d sprites in %s"),
				TotalSpritesUpdated, *AssetNamesStr);
		}
		else
		{
			Summary = FString::Printf(TEXT("Auto-reimport: processed %s (no changes detected)"), *AssetNamesStr);
		}

		FNotificationInfo Info(FText::FromString(Summary));
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> NotifItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotifItem.IsValid())
		{
			NotifItem->SetCompletionState(bAnyFailure
				? SNotificationItem::CS_Fail
				: SNotificationItem::CS_Success);
		}

		UE_LOG(LogTemp, Log, TEXT("%s"), *Summary);
	}
	else if (bAnyFailure)
	{
		FNotificationInfo Info(LOCTEXT("AutoReimportFailed", "Auto-reimport: failed to process source file changes"));
		Info.ExpireDuration = 8.0f;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> NotifItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotifItem.IsValid())
		{
			NotifItem->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
}

#undef LOCTEXT_NAMESPACE
