// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "TextureWatcherService.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
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
#include "Async/Async.h"

/** FTextureWatcherService — File system watcher for live texture reimport notifications in the editor. */

#define LOCTEXT_NAMESPACE "TextureWatcherService"

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

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Initializing..."));

	// Build initial asset mapping
	BuildSourceFileToAssetMaps();

	// Start watching directories
	RegisterDirectoryWatchers();

	bIsInitialized = true;

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Initialized. Tracking %d texture references, %d .ase source files."),
		TextureToAssetMap.Num(), AseFileToLayerAssetMap.Num());
}

void FTextureWatcherService::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Shutting down..."));

	// Clear any pending timer
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(BatchTimerHandle);
	}

	// Stop watching directories
	UnregisterDirectoryWatchers();

	// Clear state
	TextureToAssetMap.Empty();
	AseFileToLayerAssetMap.Empty();
	PendingChanges.Empty();

	bIsInitialized = false;

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Shutdown complete."));
}

void FTextureWatcherService::RefreshAssetMapping()
{
	BuildSourceFileToAssetMaps();

	// Register watchers for any new external directories discovered from .ase source paths
	for (const auto& Pair : AseFileToLayerAssetMap)
	{
		FString ParentDir = FPaths::GetPath(Pair.Key);
		if (!ParentDir.IsEmpty())
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

void FTextureWatcherService::RegisterDirectoryWatchers()
{
	FDirectoryWatcherModule& DWModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));

	IDirectoryWatcher* Watcher = DWModule.Get();
	if (!Watcher)
	{
		UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Could not get IDirectoryWatcher"));
		return;
	}

	// Watch the project's Content directory recursively
	FString ContentDir = FPaths::ProjectContentDir();

	// Normalize the path
	FPaths::NormalizeDirectoryName(ContentDir);
	ContentDir = FPaths::ConvertRelativePathToFull(ContentDir);

	FDelegateHandle Handle;
	bool bSuccess = Watcher->RegisterDirectoryChangedCallback_Handle(
		ContentDir,
		IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FTextureWatcherService::OnDirectoryChanged),
		Handle,
		IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges
	);

	if (bSuccess)
	{
		WatcherHandles.Add(Handle);
		WatchedDirectories.Add(ContentDir);
		UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Now watching directory: %s"), *ContentDir);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Failed to register watcher for: %s"), *ContentDir);
	}

	// Register watchers for external directories where .ase source files live
	TSet<FString> ExternalDirs;
	for (const auto& Pair : AseFileToLayerAssetMap)
	{
		FString ParentDir = FPaths::GetPath(Pair.Key);
		if (!ParentDir.IsEmpty() && !WatchedDirectories.Contains(ParentDir))
		{
			ExternalDirs.Add(ParentDir);
		}
	}

	for (const FString& ExtDir : ExternalDirs)
	{
		FDelegateHandle ExtHandle;
		bool bExtSuccess = Watcher->RegisterDirectoryChangedCallback_Handle(
			ExtDir,
			IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FTextureWatcherService::OnDirectoryChanged),
			ExtHandle,
			IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges
		);

		if (bExtSuccess)
		{
			WatcherHandles.Add(ExtHandle);
			WatchedDirectories.Add(ExtDir);
			UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Now watching external directory: %s"), *ExtDir);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Failed to register watcher for external directory: %s"), *ExtDir);
		}
	}
}

void FTextureWatcherService::UnregisterDirectoryWatchers()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher")))
	{
		WatcherHandles.Empty();
		WatchedDirectories.Empty();
		return;
	}

	FDirectoryWatcherModule& DWModule = FModuleManager::GetModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));

	IDirectoryWatcher* Watcher = DWModule.Get();
	if (!Watcher)
	{
		WatcherHandles.Empty();
		WatchedDirectories.Empty();
		return;
	}

	// Unregister all watchers
	int32 Index = 0;
	for (const FString& Dir : WatchedDirectories)
	{
		if (WatcherHandles.IsValidIndex(Index))
		{
			Watcher->UnregisterDirectoryChangedCallback_Handle(Dir, WatcherHandles[Index]);
		}
		Index++;
	}

	WatcherHandles.Empty();
	WatchedDirectories.Empty();
}

void FTextureWatcherService::RegisterExternalDirectory(const FString& DirPath)
{
	// Normalize the directory path
	FString NormalizedDir = FPaths::ConvertRelativePathToFull(DirPath);
	FPaths::NormalizeDirectoryName(NormalizedDir);

	// Skip if already watched
	if (WatchedDirectories.Contains(NormalizedDir))
	{
		return;
	}

	FDirectoryWatcherModule& DWModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* Watcher = DWModule.Get();
	if (!Watcher)
	{
		UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Could not get IDirectoryWatcher for external directory: %s"), *NormalizedDir);
		return;
	}

	FDelegateHandle Handle;
	bool bSuccess = Watcher->RegisterDirectoryChangedCallback_Handle(
		NormalizedDir,
		IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FTextureWatcherService::OnDirectoryChanged),
		Handle,
		IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges
	);

	if (bSuccess)
	{
		WatcherHandles.Add(Handle);
		WatchedDirectories.Add(NormalizedDir);
		UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Dynamically registered external directory: %s"), *NormalizedDir);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Failed to register watcher for external directory: %s"), *NormalizedDir);
	}
}

void FTextureWatcherService::OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
	// This callback can be invoked from a worker thread, so we need to
	// collect the changed files and dispatch processing to the game thread

	// Collect relevant file changes
	TArray<FString> ChangedTextures;
	for (const FFileChangeData& Change : Changes)
	{
		// Only care about file modifications
		if (Change.Action != FFileChangeData::FCA_Modified)
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

	// Dispatch to game thread for thread-safe processing
	AsyncTask(ENamedThreads::GameThread, [this, ChangedTextures = MoveTemp(ChangedTextures)]()
	{
		// Protect access to PendingChanges
		FScopeLock Lock(&PendingChangesLock);

		for (const FString& Path : ChangedTextures)
		{
			PendingChanges.Add(Path, FDateTime::Now());
			UE_LOG(LogTemp, Verbose, TEXT("TextureWatcherService: Detected change to: %s"), *Path);
		}

		// If we have pending changes, start/restart the batch timer
		if (PendingChanges.Num() > 0 && GEditor)
		{
			// Clear existing timer if any
			GEditor->GetTimerManager()->ClearTimer(BatchTimerHandle);

			// Set timer to process changes after 500ms of no new changes
			GEditor->GetTimerManager()->SetTimer(
				BatchTimerHandle,
				FTimerDelegate::CreateRaw(this, &FTextureWatcherService::ProcessPendingChanges),
				0.5f,
				false
			);
		}
	});
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
		// Only use already-loaded assets — don't force-load every layer asset into memory
		UPaper2DPlusCharacterLayerAsset* LayerAsset = Cast<UPaper2DPlusCharacterLayerAsset>(AssetData.FastGetAsset(/*bEvenIfPendingKill=*/false));
		if (!LayerAsset)
		{
			continue;
		}

		// Skip assets without a source path (created before auto-reimport feature)
		if (LayerAsset->SourceAseFilePath.IsEmpty())
		{
			continue;
		}

		// Normalize the path for consistent lookup
		FString NormalizedAsePath = FPaths::ConvertRelativePathToFull(LayerAsset->SourceAseFilePath);
		FPaths::NormalizeFilename(NormalizedAsePath);

		AseFileToLayerAssetMap.FindOrAdd(NormalizedAsePath).Add(TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>(LayerAsset));

		UE_LOG(LogTemp, Verbose, TEXT("TextureWatcherService: Mapped .ase %s -> %s"),
			*NormalizedAsePath, *LayerAsset->GetName());
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
	// Copy pending changes under lock, then process outside the lock
	TMap<FString, FDateTime> ChangesToProcess;
	{
		FScopeLock Lock(&PendingChangesLock);
		if (PendingChanges.Num() == 0)
		{
			return;
		}
		ChangesToProcess = MoveTemp(PendingChanges);
		PendingChanges.Empty();
	}

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

	for (const auto& Pair : ChangesToProcess)
	{
		const FString& FilePath = Pair.Key;

		FString Extension = FPaths::GetExtension(FilePath).ToLower();
		if (Extension != TEXT("ase") && Extension != TEXT("aseprite"))
		{
			continue;
		}

		// Look up affected CharacterLayerAssets
		const auto* FoundAssets = AseFileToLayerAssetMap.Find(FilePath);
		if (!FoundAssets)
		{
			continue;
		}

		bool bProcessedAny = false;

		for (const auto& WeakAsset : *FoundAssets)
		{
			UPaper2DPlusCharacterLayerAsset* LayerAsset = WeakAsset.Get();
			if (!LayerAsset)
			{
				continue;
			}

			UE_LOG(LogTemp, Log, TEXT("Auto-reimport: Reimporting .ase -> %s"), *LayerAsset->GetName());

			FAsepriteReimportResult Result = FAsepriteReimporter::ReimportFromAseFile(FilePath, LayerAsset);

			if (Result.bSuccess)
			{
				TotalLayersUpdated += Result.LayersUpdated + Result.LayersAdded;
				TotalSpritesUpdated += Result.SpritesUpdated;
				AffectedAssetNames.AddUnique(LayerAsset->DisplayName.IsEmpty()
					? LayerAsset->GetName() : LayerAsset->DisplayName);
				bProcessedAny = true;
			}
			else
			{
				bAnyFailure = true;
				UE_LOG(LogTemp, Warning, TEXT("Auto-reimport: Failed to reimport .ase for %s"), *LayerAsset->GetName());
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
			AseFilesProcessed++;
		}
	}

	// --- Process texture file changes ---

	for (const auto& Pair : ChangesToProcess)
	{
		const FString& FilePath = Pair.Key;

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
								for (const auto& WeakAsset : AsePair.Value)
								{
									if (UPaper2DPlusCharacterLayerAsset* LayerAsset = WeakAsset.Get())
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
								for (const auto& WeakAsset : AsePair.Value)
								{
									if (UPaper2DPlusCharacterLayerAsset* LayerAsset = WeakAsset.Get())
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
								for (const auto& WeakAsset : AsePair.Value)
								{
									if (UPaper2DPlusCharacterLayerAsset* LayerAsset = WeakAsset.Get())
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
