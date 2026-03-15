// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDirectoryWatcher.h"

class UTexture2D;
class UPaper2DPlusCharacterProfileAsset;
struct FTimerHandle;

/**
 * Service that monitors source texture files and logs when they change.
 *
 * When an artist modifies a source texture file (PNG, TGA, etc.), this service
 * detects the change and logs affected Character Profile Asset flipbooks.
 */
class PAPER2DPLUSEDITOR_API FTextureWatcherService
{
public:
	/** Get the singleton instance */
	static FTextureWatcherService& Get();

	/** Initialize the service - call from module startup */
	void Initialize();

	/** Shutdown the service - call from module shutdown */
	void Shutdown();

	/** Manually refresh the texture-to-asset mapping (call after new assets are created) */
	void RefreshAssetMapping();

	/** Check if the service is currently active */
	bool IsInitialized() const { return bIsInitialized; }

	/** Check if a texture file path is being tracked by any Character Profile Asset */
	bool IsTextureWatched(const FString& TexturePath) const;

private:
	FTextureWatcherService() = default;
	~FTextureWatcherService() = default;

	// Non-copyable
	FTextureWatcherService(const FTextureWatcherService&) = delete;
	FTextureWatcherService& operator=(const FTextureWatcherService&) = delete;

	// Directory watching
	void RegisterDirectoryWatchers();
	void UnregisterDirectoryWatchers();
	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes);

	// Asset mapping
	void BuildTextureToAssetMap();
	TArray<TPair<UPaper2DPlusCharacterProfileAsset*, int32>> FindAffectedFlipbooks(const FString& TexturePath);

	// Convert UE asset path to file system path
	FString GetFileSystemPathForTexture(const FString& AssetPath);

	// Pending changes batching
	void ProcessPendingChanges();

	// State
	bool bIsInitialized = false;
	TArray<FDelegateHandle> WatcherHandles;
	TSet<FString> WatchedDirectories;

	// Texture file path -> [(Asset, FlipbookIndex), ...]
	// Maps file system paths to the assets/flipbooks that reference them
	TMap<FString, TArray<TPair<TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, int32>>> TextureToAssetMap;

	// Pending changes (batch multiple rapid changes from the same file)
	// File path -> timestamp of last change
	TMap<FString, FDateTime> PendingChanges;
	FTimerHandle BatchTimerHandle;

	// Thread safety - protects PendingChanges access from watcher thread
	mutable FCriticalSection PendingChangesLock;
};
