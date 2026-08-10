// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/SCompoundWidget.h"

class SComboButton;
class SWidget;

/**
 * Stable, serializable identity used by every Paper2DPlus editor catalog.
 *
 * ObjectPath is authoritative when the item owns an Unreal asset. FallbackKey is a
 * deliberately weaker, source-defined identity for name-only rows and old config
 * entries. It is never an array index: reorder, filtering, and virtualization must
 * not change what a selection means.
 */
struct PAPER2DPLUSEDITOR_API FProfileItemIdentity
{
	FName SourceType;
	FSoftObjectPath ObjectPath;
	FString FallbackKey;

	bool IsValid() const;
	FString ToStableString() const;
	static bool FromStableString(const FString& Serialized, FProfileItemIdentity& OutIdentity);
	bool Matches(const FProfileItemIdentity& Other) const;

	friend bool operator==(const FProfileItemIdentity& A, const FProfileItemIdentity& B)
	{
		return A.Matches(B);
	}
};

/** Read-only row projected by a domain source into the common picker. */
struct PAPER2DPLUSEDITOR_API FProfilePickerItem
{
	FProfileItemIdentity Identity;
	FText Label;
	TArray<FString> Aliases;
	FText SecondaryText;
	FString Group;
	FGameplayTagContainer SearchTags;
	int32 CanonicalOrder = INDEX_NONE;

	/** Lower-cased aggregate used by the result model. Sources do not need to cache it. */
	FString BuildSearchDocument() const;
};

/** Optional, domain-owned visual projected only for instantiated result rows. */
DECLARE_DELEGATE_RetVal_OneParam(TSharedPtr<SWidget>, FOnGenerateProfileNavigatorItemPreview, const FProfilePickerItem&);

DECLARE_MULTICAST_DELEGATE(FOnProfileItemSourceChanged);

/**
 * Type-agnostic data/selection contract. Refresh and search are read-only; the only
 * mutation seam is SelectItem, which delegates to the owning editor model.
 */
class PAPER2DPLUSEDITOR_API IProfileItemPickerSource
{
public:
	virtual ~IProfileItemPickerSource() = default;

	virtual FName GetSourceType() const = 0;
	virtual FString GetLogicalCatalogScope() const = 0;
	virtual void GetItems(TArray<FProfilePickerItem>& OutItems) const = 0;
	virtual FProfileItemIdentity GetSelectedIdentity() const = 0;
	virtual bool SelectItem(const FProfileItemIdentity& Identity) = 0;
	virtual FOnProfileItemSourceChanged& OnSourceChanged() = 0;
};

/** One flattened virtual-list row. Headers are deliberately non-selectable. */
struct PAPER2DPLUSEDITOR_API FProfilePickerDisplayRow
{
	enum class EKind : uint8
	{
		Header,
		Item
	};

	EKind Kind = EKind::Item;
	FText Header;
	TSharedPtr<FProfilePickerItem> Item;
	bool bPinnedProjection = false;
	bool bRecentProjection = false;
};

/**
 * Pure result projection shared by popup and pinned hosts. It owns query state, so
 * two open popups never overwrite each other's text or the editor's global search.
 */
class PAPER2DPLUSEDITOR_API FProfilePickerResultModel : public TSharedFromThis<FProfilePickerResultModel>
{
public:
	explicit FProfilePickerResultModel(TSharedPtr<IProfileItemPickerSource> InSource);

	void SetQuery(const FString& InQuery);
	const FString& GetQuery() const { return Query; }
	void Refresh();

	const TArray<TSharedPtr<FProfilePickerDisplayRow>>& GetRows() const { return Rows; }
	const TArray<TSharedPtr<FProfilePickerItem>>& GetMatchingItems() const { return MatchingItems; }
	TSharedPtr<FProfilePickerItem> Resolve(const FProfileItemIdentity& Identity) const;
	bool HasAnySourceItems() const { return bHasAnySourceItems; }

private:
	bool MatchesQuery(const FProfilePickerItem& Item) const;
	void AppendSection(const FText& Header, const TArray<TSharedPtr<FProfilePickerItem>>& Items,
		bool bPinned, bool bRecent, TSet<FString>& InOutAlreadyProjected);

	TSharedPtr<IProfileItemPickerSource> Source;
	FString Query;
	TArray<TSharedPtr<FProfilePickerItem>> AllItems;
	TArray<TSharedPtr<FProfilePickerItem>> MatchingItems;
	TArray<TSharedPtr<FProfilePickerDisplayRow>> Rows;
	bool bHasAnySourceItems = false;
};

/**
 * One process-local, config-backed pin/recent store. Logical catalog scope (not
 * toolkit/layout) is the key, so Character and Layer editors share animation state
 * while retaining independent dock layouts. Interactive writes intentionally do not
 * call GConfig->Flush.
 */
class PAPER2DPLUSEDITOR_API FProfilePickerCatalogStore
{
public:
	static FProfilePickerCatalogStore& Get();

	const TArray<FProfileItemIdentity>& GetPins(const FString& Scope);
	const TArray<FProfileItemIdentity>& GetRecents(const FString& Scope);
	bool IsPinned(const FString& Scope, const FProfileItemIdentity& Identity);
	/** Visible/non-color-only command label shared by rows, tooltips, and accessibility text. */
	FText GetPinActionLabel(const FString& Scope, const FProfileItemIdentity& Identity);
	void TogglePin(const FString& Scope, const FProfileItemIdentity& Identity);
	void AddRecent(const FString& Scope, const FProfileItemIdentity& Identity);
	void Prune(const FString& Scope, const TArray<FProfilePickerItem>& ValidItems);

	FSimpleMulticastDelegate& OnStoreChanged() { return StoreChanged; }

	/** Tests use isolated scopes and can clear both memory and in-memory config state. */
	void ResetScopeForTests(const FString& Scope);

	static constexpr int32 MaxPins = 32;
	static constexpr int32 MaxRecents = 16;

private:
	struct FCatalogState
	{
		bool bLoaded = false;
		TArray<FProfileItemIdentity> Pins;
		TArray<FProfileItemIdentity> Recents;
	};

	FCatalogState& FindOrLoad(const FString& Scope);
	void Save(const FString& Scope, const FCatalogState& State);
	static FString SectionForScope(const FString& Scope);
	static void Normalize(TArray<FProfileItemIdentity>& Identities, int32 MaxCount);

	TMap<FString, FCatalogState> States;
	FSimpleMulticastDelegate StoreChanged;
};

/** Compact current-item control. Its menu creates an independent popup result model. */
class PAPER2DPLUSEDITOR_API SProfileItemPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProfileItemPicker) {}
		SLATE_ARGUMENT(TSharedPtr<IProfileItemPickerSource>, Source)
		SLATE_ATTRIBUTE(FText, EmptySelectionText)
		SLATE_EVENT(FOnGenerateProfileNavigatorItemPreview, OnGenerateItemPreview)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SProfileItemPicker() override;

private:
	FText GetCurrentLabel() const;
	void RefreshCurrentLabel();
	TSharedRef<SWidget> BuildMenuContent();

	TSharedPtr<IProfileItemPickerSource> Source;
	TSharedPtr<SComboButton> ComboButton;
	TAttribute<FText> EmptySelectionText;
	FOnGenerateProfileNavigatorItemPreview OnGenerateItemPreview;
	FText CachedCurrentLabel;
	FDelegateHandle SourceChangedHandle;
};
