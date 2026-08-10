// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ProfileItemPicker.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"

struct FAssetData;
class UPaper2DPlusEffectProfileAsset;
class UPaperFlipbook;
struct FPaper2DPlusEffectProfileEntry;

/** Non-color-only validation state projected beside one library row. */
enum class EEffectProfileRowIssueState : uint8
{
	None,
	Info,
	Warning,
	Error
};

/** Explicit intake report shared by picker, drag/drop, notifications, and headless tests. */
struct PAPER2DPLUSEDITOR_API FEffectProfileIntakeResult
{
	int32 RequestedCount = 0;
	int32 AddedCount = 0;
	int32 DuplicateCount = 0;
	int32 RejectedCount = 0;
	TArray<FSoftObjectPath> AddedPaths;
	TArray<FString> DuplicateNames;
	TArray<FString> RejectedNames;

	bool ChangedAsset() const { return AddedCount > 0; }
	bool HasRejections() const { return DuplicateCount > 0 || RejectedCount > 0; }
	FText BuildSummary() const;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnEffectProfileSelectedEffectChanged, UPaperFlipbook*);

/**
 * Transient Effect Profile editor authority. Selection is an object path, never an array index.
 * Refresh/filter/playback are read-only; every asset mutation is one explicit FScopedTransaction.
 */
class PAPER2DPLUSEDITOR_API FEffectProfileEditorModel final : public IProfileItemPickerSource
{
public:
	FEffectProfileEditorModel() = default;
	virtual ~FEffectProfileEditorModel() override = default;

	void Initialize(UPaper2DPlusEffectProfileAsset* InAsset, bool bRestoreSelection = true);
	UPaper2DPlusEffectProfileAsset* GetAsset() const { return Asset.Get(); }

	/** Mutation-free reconciliation after undo/redo, Advanced Details, rename, or external refresh. */
	void RefreshFromAsset();
	/** Publish one live picker invalidation after an external Details/undo mutation, then reconcile. */
	void RefreshAfterExternalMutation();

	const FSoftObjectPath& GetSelectedEffectPath() const { return SelectedEffectPath; }
	UPaperFlipbook* GetSelectedFlipbook() const;
	const FPaper2DPlusEffectProfileEntry* GetSelectedEntry() const;
	int32 ResolveSelectedIndex() const;
	bool HasResolvedSelection() const { return ResolveSelectedIndex() != INDEX_NONE; }
	bool SelectEffectPath(const FSoftObjectPath& EffectPath);
	bool SelectEffectIdentityString(const FString& Identity);
	void ClearSelection();

	/** Future layered-effect seam: observers consume the selected MAIN flipbook, not an index/copy. */
	FOnEffectProfileSelectedEffectChanged& OnSelectedEffectChanged() { return SelectedEffectChanged; }

	FEffectProfileIntakeResult AddFlipbooks(const TArray<UPaperFlipbook*>& Flipbooks);
	FEffectProfileIntakeResult AddAssetData(const TArray<FAssetData>& Assets);
	bool RemoveSelected();
	bool MoveSelected(int32 Direction);
	bool CanMoveSelected(int32 Direction) const;
	bool SetSelectedDisplayLabel(const FText& NewLabel);
	bool SetSelectedTypeTag(FGameplayTag NewType);
	bool SetSelectedDescriptorTags(const FGameplayTagContainer& NewDescriptors);
	bool OpenSelectedSource() const;

	void SetDescriptorFilter(const FGameplayTagContainer& NewFilter);
	const FGameplayTagContainer& GetDescriptorFilter() const { return DescriptorFilter; }
	void ClearDescriptorFilter();
	int32 GetLibraryEntryCount() const;
	int32 GetVisibleEntryCount() const;

	UPaperFlipbook* ResolveFlipbook(const FProfileItemIdentity& Identity) const;
	const FPaper2DPlusEffectProfileEntry* ResolveEntry(const FProfileItemIdentity& Identity) const;
	EEffectProfileRowIssueState GetRowIssueState(const FSoftObjectPath& EffectPath) const;
	FText GetRowIssueStateText(const FSoftObjectPath& EffectPath) const;

	int32 GetRefreshRevisionForTests() const { return RefreshRevision; }
	int32 GetMutationCountForTests() const { return MutationCount; }
	static void ResetSelectionConfigForTests(const UPaper2DPlusEffectProfileAsset* Asset);

	// IProfileItemPickerSource
	virtual FName GetSourceType() const override;
	virtual FString GetLogicalCatalogScope() const override;
	virtual void GetItems(TArray<FProfilePickerItem>& OutItems) const override;
	virtual FProfileItemIdentity GetSelectedIdentity() const override;
	virtual bool SelectItem(const FProfileItemIdentity& Identity) override;
	virtual FOnProfileItemSourceChanged& OnSourceChanged() override { return SourceChanged; }

private:
	static FSoftObjectPath MakeEffectPath(const UPaperFlipbook* Flipbook);
	static FString NormalizedPath(const UPaperFlipbook* Flipbook);
	static FString NormalizedPath(const FSoftObjectPath& Path);
	static bool IsStrictChildOf(FGameplayTag Tag, FGameplayTag Root);
	static FString SelectionConfigSection(const UPaper2DPlusEffectProfileAsset* InAsset);

	int32 ResolveIndex(const FSoftObjectPath& EffectPath) const;
	bool PassesDescriptorFilter(const FPaper2DPlusEffectProfileEntry& Entry) const;
	void RebuildValidationProjection();
	void SetSelectionInternal(const FSoftObjectPath& NewPath, UPaperFlipbook* NewFlipbook, bool bPersist);
	void LoadSelectionFromConfig();
	void SaveSelectionToConfig() const;
	void CommitMutationFinished();

	TWeakObjectPtr<UPaper2DPlusEffectProfileAsset> Asset;
	FSoftObjectPath SelectedEffectPath;
	/** The selected row is the editor's explicit load boundary and remains resident while selected. */
	TStrongObjectPtr<UPaperFlipbook> SelectedFlipbook;
	FGameplayTagContainer DescriptorFilter;
	TMap<FString, EEffectProfileRowIssueState> RowIssueStates;
	FOnProfileItemSourceChanged SourceChanged;
	FOnEffectProfileSelectedEffectChanged SelectedEffectChanged;
	int32 RefreshRevision = 0;
	int32 MutationCount = 0;
	bool bUseSelectionConfig = true;
};

/** Pure, worldless key-frame playback used by Preview and its headless tests. */
class PAPER2DPLUSEDITOR_API FEffectProfilePlaybackState
{
public:
	void SetFlipbook(UPaperFlipbook* InFlipbook);
	UPaperFlipbook* GetFlipbook() const { return Flipbook.Get(); }
	void Play();
	void Pause() { bPlaying = false; }
	void TogglePlay() { bPlaying ? Pause() : Play(); }
	bool IsPlaying() const { return bPlaying; }
	void SetLooping(bool bInLooping) { bLooping = bInLooping; }
	bool IsLooping() const { return bLooping; }
	void SeekKeyFrame(int32 KeyFrameIndex);
	void Tick(float DeltaSeconds);

	int32 GetCurrentKeyFrame() const { return CurrentKeyFrame; }
	int32 GetNumKeyFrames() const;
	FText GetFrameStatusText() const;

private:
	TWeakObjectPtr<UPaperFlipbook> Flipbook;
	int32 CurrentKeyFrame = 0;
	int32 FrameRunProgress = 0;
	float TimeAccumulator = 0.0f;
	bool bPlaying = false;
	bool bLooping = true;
};
