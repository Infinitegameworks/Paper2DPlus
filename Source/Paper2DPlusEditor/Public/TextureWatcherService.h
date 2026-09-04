// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDirectoryWatcher.h"
#include "UObject/SoftObjectPath.h"

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
 *    SourceAseFilePath and registered individually; nested Content directories never receive a redundant
 *    callback. Because that discovery happens at startup and on
 *    RefreshAssetMapping(), a brand-new external .ase imported mid-session is only watched once
 *    RefreshAssetMapping() runs again -- the importer calls it after a fresh import so the loop closes without
 *    an editor restart.
 *
 * Load-free mapping + offline reconcile (TASK-183):
 *  - The .ase → layer-asset map is built from ASSET REGISTRY TAGS (Paper2DPlus.SourceAseFile/SourceAseHash),
 *    so every saved layer asset is tracked WITHOUT loading it; the asset loads lazily, only when its file
 *    actually changes. Assets saved before the tags existed fall back to the old loaded-assets-only behavior
 *    until their next save/reimport.
 *  - At startup (once the asset registry's initial scan completes) ReconcileOfflineAseChanges() compares each
 *    tracked file's content hash against the stamp recorded at its last import/reimport and queues an
 *    auto-reimport on mismatch — this is what makes "artist commits the .ase, teammate pulls with the editor
 *    CLOSED" propagate on next launch. Stored paths resolve through FAsepriteImporter::ResolveStoredAsePath
 *    (project-relative form travels between machines).
 */
class PAPER2DPLUSEDITOR_API FTextureWatcherService
{
public:
	/** Get the singleton instance */
	static FTextureWatcherService& Get();

	/** Initialize the service - call from module startup */
	void Initialize();

	/** Shutdown the service - call from module shutdown. Returns false if any callback could not be removed. */
	bool Shutdown();

	/** Manually refresh the texture-to-asset mapping (call after new assets are created) */
	void RefreshAssetMapping();

	/** Check if the service is currently active */
	bool IsInitialized() const { return bIsInitialized; }

	/** Check if a texture file path is being tracked by any Character Profile Asset */
	bool IsTextureWatched(const FString& TexturePath) const;

	/** Register an external directory. Returns true only when a new callback is registered. */
	bool RegisterExternalDirectory(const FString& DirPath);

#if WITH_DEV_AUTOMATION_TESTS
	/** True only after this exact path has left the debounce queue and its one-shot timer has stopped. */
	bool IsPendingChangeDrainedForTests(const FString& FilePath) const;

	/**
	 * Returns the production reimport-completion epoch for this exact .ase path. The epoch advances only
	 * after a mapped asset completed a successful reimport, so tests can wait without loading the asset
	 * they are trying to prove the watcher loaded lazily.
	 */
	uint64 GetSuccessfulAseReimportEpochForTests(const FString& FilePath) const;

	/**
	 * Returns the drop epoch for this exact .ase path: it advances only when a queued change for a
	 * MAPPED file was dropped because Live .ase Auto-Reimport is disabled in Project Settings. Lets a
	 * test prove the deliberate negative — the event travelled the whole watcher/debounce/processing
	 * pipeline and was refused at the setting gate — without racing the debounce timer.
	 */
	uint64 GetDroppedAseChangeEpochForTests(const FString& FilePath) const;

	/** True while this exact path sits in the debounce queue. Narrower than the drained probe: it
	 *  ignores the shared batch timer, so an unrelated file's event cannot flake a negative assert. */
	bool IsAseChangePendingForTests(const FString& FilePath) const;

	/**
	 * Copies the real engine registration handle so a test can ask DirectoryWatcher whether Shutdown
	 * actually released the callback. Consulting this service's map after shutdown would let a
	 * dropped-unregister bug certify its own false result.
	 */
	bool CopyDirectoryWatcherHandleForTests(const FString& Directory, FDelegateHandle& OutHandle) const;
#endif

private:
	FTextureWatcherService() = default;
	~FTextureWatcherService() = default;

	// Non-copyable
	FTextureWatcherService(const FTextureWatcherService&) = delete;
	FTextureWatcherService& operator=(const FTextureWatcherService&) = delete;

	// Directory watching
	bool RegisterDirectoryWatchers();
	bool UnregisterDirectoryWatchers();
	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes, uint64 RegistrationGeneration);

	// Asset mapping
	void BuildSourceFileToAssetMaps();
	TArray<TPair<UPaper2DPlusCharacterProfileAsset*, int32>> FindAffectedFlipbooks(const FString& TexturePath);

	/**
	 * Startup reconcile (TASK-183): hash every tracked .ase on disk against the stamp recorded at its
	 * last import/reimport and queue the standard pending-change auto-reimport for mismatches — catches
	 * edits that landed while the editor was closed (e.g. a git pull of the artist's commit). Entries
	 * without a stamp (pre-TASK-183 assets) are skipped; they still react to live change events.
	 */
	void ReconcileOfflineAseChanges();

	// Convert UE asset path to file system path
	FString GetFileSystemPathForTexture(const FString& AssetPath);

	// Pending changes batching
	void ArmBatchTimer(uint64 ExpectedGeneration);
	void ProcessPendingChanges();

	// State
	bool bIsInitialized = false;
	/** The engine delegate owns the only strong lifetime token; expiration proves it no longer owns our callback. */
	struct FDirectoryWatcherCallbackLifetime
	{
		uint64 RegistrationGeneration = 0;
	};
	struct FDirectoryWatcherRegistration
	{
		FDelegateHandle Handle;
		TWeakPtr<FDirectoryWatcherCallbackLifetime> CallbackLifetime;
	};
	/** Exact directory-to-registration pairing; parallel directory/handle collections are unsafe. */
	TMap<FString, FDirectoryWatcherRegistration> WatcherRegistrationsByDirectory;
	/** Invalidates registered callbacks, queued game-thread work, and timer delegates across shutdown. */
	uint64 LifecycleGeneration = 0;

	// Texture file path -> [(Asset, FlipbookIndex), ...]
	// Maps file system paths to the assets/flipbooks that reference them
	TMap<FString, TArray<TPair<TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, int32>>> TextureToAssetMap;

	/** ONE (layer asset, source .ase) pairing. A multi-source Layer Profile contributes one entry PER
	 *  source, so every source file triggers its own reimport with its own context: the soft path
	 *  (loaded lazily, only when the file actually changes), the STORED form of THIS source's path
	 *  (the key into the asset's ImportedAseSources — never re-derived from the resolved path), and
	 *  the content hash stamped at THIS source's last import/reimport (empty = unstamped). */
	struct FAseWatchEntry
	{
		FSoftObjectPath AssetPath;
		FString StoredSourcePath;
		FString ImportedHash;
	};

	// .ase file path (resolved absolute) -> layer assets imported from it. Built from asset registry
	// tags so mapping never force-loads assets (TASK-183).
	TMap<FString, TArray<FAseWatchEntry>> AseFileToLayerAssetMap;

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Test-only observation of successful mapped .ase reimports. It is deliberately separate from the
	 * debounce queue: queue drainage cannot prove that an import actually ran.
	 */
	uint64 SuccessfulAseReimportEpoch = 0;
	TMap<FString, uint64> SuccessfulAseReimportEpochByFile;

	/** Test-only observation of mapped .ase changes dropped by the Live .ase Auto-Reimport setting. */
	uint64 DroppedAseChangeEpoch = 0;
	TMap<FString, uint64> DroppedAseChangeEpochByFile;
#endif

	// One-shot hook: rebuild the map + run the offline reconcile once the registry's initial scan completes
	FDelegateHandle AssetRegistryFilesLoadedHandle;

	// TASK-192 U1: off-to-on transition hook. When Live .ase Auto-Reimport is re-enabled, run the same
	// offline reconcile an editor start runs, so files edited while it was off heal without a restart.
	FDelegateHandle AseLiveReimportSettingChangedHandle;

	// Pending changes (batch multiple rapid changes from the same file). DirectoryWatcher, registry,
	// and timer callbacks all enter on the editor game thread, so this state has one owner.
	TSet<FString> PendingChanges;
	FTimerHandle BatchTimerHandle;
};
