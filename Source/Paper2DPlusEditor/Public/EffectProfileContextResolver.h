// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtr.h"

struct FPaper2DPlusEffectLibraryChange;
class FPaper2DPlusEffectLibraryIndex;
class UPaper2DPlusCharacterCatalogAsset;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusEffectProfileAsset;

enum class EPaper2DPlusEffectContextStatus : uint8
{
	Unresolved,
	UnscopedNoDefaultCatalog,
	ScopedCatalogUnavailable,
	ScopedCharacterMissing,
	ScopedEffectUnassigned,
	ScopedEffectUnavailable,
	ScopedReady
};

/** Value result consumed by the picker, validation, and tests. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusEffectProfileContext
{
	EPaper2DPlusEffectContextStatus Status = EPaper2DPlusEffectContextStatus::Unresolved;
	bool bChoicesScoped = false;
	FSoftObjectPath CatalogPath;
	FSoftObjectPath CharacterPath;
	FSoftObjectPath EffectProfilePath;
	TArray<FSoftObjectPath> AllowedFlipbooks;
	FText Guidance;

	bool IsResolved() const { return Status != EPaper2DPlusEffectContextStatus::Unresolved; }
	bool IsReady() const { return Status == EPaper2DPlusEffectContextStatus::ScopedReady; }
	bool IsAllowed(const FSoftObjectPath& Path) const;
	bool IsOutOfLibrary(const FSoftObjectPath& ExistingValue) const;
};

DECLARE_MULTICAST_DELEGATE(FOnPaper2DPlusEffectContextInvalidated);

/**
 * Per-editor, weak Character context for Catalog-scoped effect choices.
 *
 * Construction and invalidation never load assets. ResolveForPicker is the sole
 * synchronous load boundary and is called only by explicit picker interaction.
 */
class PAPER2DPLUSEDITOR_API FEffectProfileContextResolver final
	: public TSharedFromThis<FEffectProfileContextResolver>
{
public:
	explicit FEffectProfileContextResolver(
		UPaper2DPlusCharacterProfileAsset* InCharacter,
		TSharedPtr<FPaper2DPlusEffectLibraryIndex> InLibraryIndex = nullptr);
	~FEffectProfileContextResolver();

	/** Retarget an existing editor after a live Base-Profile swap. This never loads assets. */
	void SetCharacter(UPaper2DPlusCharacterProfileAsset* InCharacter);
	const FPaper2DPlusEffectProfileContext& ResolveForPicker();
	const FPaper2DPlusEffectProfileContext& GetCachedContext() const { return CachedContext; }
	void Invalidate();
	uint32 GetRevision() const { return Revision; }
	FOnPaper2DPlusEffectContextInvalidated& OnInvalidated() { return Invalidated; }
	TSharedPtr<FPaper2DPlusEffectLibraryIndex> GetLibraryIndex() const { return LibraryIndex; }

	/** Pure loaded-object seam used by validation and headless automation. */
	static FPaper2DPlusEffectProfileContext ResolveLoadedContext(
		const UPaper2DPlusCharacterCatalogAsset* Catalog,
		const UPaper2DPlusCharacterProfileAsset* Character,
		bool bCatalogConfigured,
		const FSoftObjectPath& ConfiguredCatalogPath = FSoftObjectPath(),
		bool bCatalogLoadFailed = false);

private:
	void BindDelegates();
	void UnbindDelegates();
	void HandleLibraryInvalidated(const FPaper2DPlusEffectLibraryChange& Change);
	bool IsRelevantPath(const FSoftObjectPath& Path) const;

	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Character;
	TWeakObjectPtr<UPaper2DPlusCharacterCatalogAsset> ResolvedCatalog;
	TWeakObjectPtr<UPaper2DPlusEffectProfileAsset> ResolvedEffect;
	TSharedPtr<FPaper2DPlusEffectLibraryIndex> LibraryIndex;
	FPaper2DPlusEffectProfileContext CachedContext;
	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle LibraryInvalidatedHandle;
	uint32 Revision = 0;
	FOnPaper2DPlusEffectContextInvalidated Invalidated;
};
