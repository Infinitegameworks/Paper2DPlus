// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "TextureWatcherService.h"
#include "Paper2DPlusCharacterDataAsset.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Editor.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "Async/Async.h"

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
	BuildTextureToAssetMap();

	// Start watching directories
	RegisterDirectoryWatchers();

	bIsInitialized = true;

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Initialized. Tracking %d texture references."), TextureToAssetMap.Num());
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
	PendingChanges.Empty();

	bIsInitialized = false;

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Shutdown complete."));
}

void FTextureWatcherService::RefreshAssetMapping()
{
	BuildTextureToAssetMap();
	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Refreshed mapping. Now tracking %d texture references."), TextureToAssetMap.Num());
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

		// Check if this is an image file we care about
		FString Extension = FPaths::GetExtension(Change.Filename).ToLower();
		if (Extension != TEXT("png") && Extension != TEXT("tga") &&
			Extension != TEXT("psd") && Extension != TEXT("bmp") &&
			Extension != TEXT("jpg") && Extension != TEXT("jpeg"))
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

void FTextureWatcherService::BuildTextureToAssetMap()
{
	TextureToAssetMap.Empty();

	// Get the asset registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Find all Paper2DPlusCharacterDataAsset instances
	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UPaper2DPlusCharacterDataAsset::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& AssetData : AssetList)
	{
		// Load the asset to access its data
		UPaper2DPlusCharacterDataAsset* Asset = Cast<UPaper2DPlusCharacterDataAsset>(AssetData.GetAsset());
		if (!Asset)
		{
			continue;
		}

		// Iterate through all animations
		for (int32 AnimIndex = 0; AnimIndex < Asset->Animations.Num(); AnimIndex++)
		{
			const FAnimationHitboxData& Anim = Asset->Animations[AnimIndex];

			// Skip animations without source texture
			if (Anim.SourceTexture.IsNull())
			{
				continue;
			}

			// Get the file system path for this texture
			FString AssetPath = Anim.SourceTexture.ToSoftObjectPath().GetAssetPathString();
			FString FilePath = GetFileSystemPathForTexture(AssetPath);

			if (!FilePath.IsEmpty())
			{
				// Add to the map
				TextureToAssetMap.FindOrAdd(FilePath).Add(
					TPair<TWeakObjectPtr<UPaper2DPlusCharacterDataAsset>, int32>(Asset, AnimIndex)
				);

				UE_LOG(LogTemp, Verbose, TEXT("TextureWatcherService: Mapped %s -> %s::%s"),
					*FilePath, *Asset->GetName(), *Anim.AnimationName);
			}
		}
	}
}

FString FTextureWatcherService::GetFileSystemPathForTexture(const FString& AssetPath)
{
	// Convert UE asset path (e.g., /Game/Textures/MyTexture) to file system path

	FString FilePath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, FilePath))
	{
		return FString();
	}

	// The FilePath now contains the path without extension
	// Try common source image extensions
	static const TArray<FString> Extensions = { TEXT(".png"), TEXT(".tga"), TEXT(".psd"), TEXT(".bmp") };

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
	FString UAssetPath = FilePath + TEXT(".uasset");
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

TArray<TPair<UPaper2DPlusCharacterDataAsset*, int32>> FTextureWatcherService::FindAffectedAnimations(const FString& TexturePath)
{
	TArray<TPair<UPaper2DPlusCharacterDataAsset*, int32>> Result;

	// Normalize the path for comparison
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(TexturePath);
	FPaths::NormalizeFilename(NormalizedPath);

	// Look up in our map
	if (const auto* Found = TextureToAssetMap.Find(NormalizedPath))
	{
		int32 InvalidCount = 0;
		for (const auto& Pair : *Found)
		{
			if (UPaper2DPlusCharacterDataAsset* Asset = Pair.Key.Get())
			{
				// Validate that the animation index is still valid
				if (Asset->Animations.IsValidIndex(Pair.Value))
				{
					Result.Add(TPair<UPaper2DPlusCharacterDataAsset*, int32>(Asset, Pair.Value));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("TextureWatcherService: Animation index %d no longer valid for asset %s"),
						Pair.Value, *Asset->GetName());
					InvalidCount++;
				}
			}
			else
			{
				InvalidCount++;
			}
		}

		// If we had invalid entries, suggest refreshing the mapping
		if (InvalidCount > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: %d stale references found for %s. Consider calling RefreshAssetMapping()."),
				InvalidCount, *FPaths::GetBaseFilename(TexturePath));
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

	UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: Processing %d pending texture changes"), ChangesToProcess.Num());

	// Collect all affected animations across all changed textures
	TArray<TPair<UPaper2DPlusCharacterDataAsset*, int32>> AllAffected;
	TArray<FString> ChangedTextureNames;

	for (const auto& Pair : ChangesToProcess)
	{
		const FString& TexturePath = Pair.Key;

		TArray<TPair<UPaper2DPlusCharacterDataAsset*, int32>> Affected = FindAffectedAnimations(TexturePath);
		if (Affected.Num() > 0)
		{
			AllAffected.Append(Affected);
			ChangedTextureNames.Add(FPaths::GetBaseFilename(TexturePath));
		}
	}

	// If we found affected animations, show notification
	if (AllAffected.Num() > 0)
	{
		// Build texture names string
		FString TextureNamesStr;
		if (ChangedTextureNames.Num() == 1)
		{
			TextureNamesStr = ChangedTextureNames[0];
		}
		else if (ChangedTextureNames.Num() <= 3)
		{
			TextureNamesStr = FString::Join(ChangedTextureNames, TEXT(", "));
		}
		else
		{
			TextureNamesStr = FString::Printf(TEXT("%s and %d more"),
				*ChangedTextureNames[0], ChangedTextureNames.Num() - 1);
		}

		// Texture change detected - log only (re-extraction system removed)
		UE_LOG(LogTemp, Log, TEXT("TextureWatcherService: %s changed, %d animations affected"),
			*TextureNamesStr, AllAffected.Num());
	}
}

#undef LOCTEXT_NAMESPACE
