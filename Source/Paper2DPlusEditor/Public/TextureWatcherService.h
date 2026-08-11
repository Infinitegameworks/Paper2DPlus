// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDirectoryWatcher.h"

class UTexture2D;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterLayerAsset;
struct FTimerHandle;

/**
 * Service that monitors source files (textures + Aseprite .ase) and reacts when they change on disk.
 *
 * When an artist re-saves a source texture, affected Character Profile flipbooks are reported; when an artist
 * re-saves a source .ase, the matching Character Layer asset is reimported additively
 * (FAsepriteReimporter::ReimportFromAseFile) with no manual editor step.
 *
 * Watch coverage (TASK-71):
 *  - In-Content source files are covered by the recursive watch on the project Content directory.
 *  - EXTERNAL source directories (where a .ase lives outside Content) are discovered from each layer asset's
 *    SourceAseFilePath and registered individually. Because that discovery happens at startup and on
 *    RefreshAssetMapping(), a brand-new external .ase imported mid-session is only watched once
 *    RefreshAssetMapping() runs again -- the importer calls it after a fresh import so the loop closes without
 *    an editor restart.
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

	/** Register an external directory for file watching (e.g., where .ase source files live) */
	void RegisterExternalDirectory(const FString& DirPath);

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
	void BuildSourceFileToAssetMaps();
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

	// .ase file path -> [CharacterLayerAsset, ...]
	// Maps external .ase source file paths to the layer assets that were imported from them
	TMap<FString, TArray<TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>>> AseFileToLayerAssetMap;

	// Pending changes (batch multiple rapid changes from the same file)
	// File path -> timestamp of last change
	TMap<FString, FDateTime> PendingChanges;
	FTimerHandle BatchTimerHandle;

	// Thread safety - protects PendingChanges access from watcher thread
	mutable FCriticalSection PendingChangesLock;
};
