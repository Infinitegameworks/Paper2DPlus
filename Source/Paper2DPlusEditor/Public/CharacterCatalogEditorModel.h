// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "CharacterCatalogAuditService.h"
#include "CharacterCatalogSourceTypes.h"
#include "Containers/Ticker.h"
#include "ProfileItemPicker.h"

class IAssetRegistry;
class UObject;
class UPaper2DPlusCharacterCatalogAsset;
class UPaper2DPlusCharacterProfileAsset;

enum class EPaper2DPlusCatalogRequirementFilter : uint8
{
	Any,
	Layer,
	Effect,
	Combat
};

enum class EPaper2DPlusCatalogCompletionFilter : uint8
{
	Any,
	Complete,
	Incomplete,
	/** No progress data at all — the asset has not been resaved since progress tags shipped. */
	Unknown
};

enum class EPaper2DPlusCatalogSeverityFilter : uint8
{
	Any,
	Errors,
	Warnings,
	Clean
};

/** Explicit, non-color-only status projected once per Catalog row. */
enum class EPaper2DPlusCatalogRowStatus : uint8
{
	Complete,
	OptionalMissing,
	RequiredMissing,
	MissingAsset,
	Warning,
	Error
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusCharacterCatalogFilters
{
	FString SearchText;
	FGameplayTag Tag;
	FName Group = NAME_None;
	EPaper2DPlusCatalogRequirementFilter Requirement = EPaper2DPlusCatalogRequirementFilter::Any;
	EPaper2DPlusCatalogCompletionFilter Completion = EPaper2DPlusCatalogCompletionFilter::Any;
	EPaper2DPlusCatalogSeverityFilter Severity = EPaper2DPlusCatalogSeverityFilter::Any;

	bool IsDefault() const;
};

/**
 * Real authoring progress for one character: the designer's ticked checklist criteria summed across
 * its Character Profile and every assigned companion.
 *
 * Values come from hidden asset-registry tags (or a live read when the asset happens to be resident),
 * so building this performs no synchronous load. The criteria are manual ticks with no content-derived
 * fallback, so "no data" is a real state and is never presented as complete.
 */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusCatalogAuthoringProgress
{
	int32 Done = 0;
	int32 Total = 0;
	/** Sources (Character + assigned companions) that reported a self-consistent pair. */
	int32 SourcesWithData = 0;
	/** Present sources that reported nothing usable — typically not resaved since progress tags shipped. */
	int32 SourcesMissingData = 0;

	bool IsKnown() const { return SourcesWithData > 0 && Total > 0; }
	float GetFraction() const
	{
		return Total > 0 ? static_cast<float>(Done) / static_cast<float>(Total) : 0.0f;
	}
	/** Every reporting source fully ticked AND nothing unaccounted for. */
	bool IsFullyAuthored() const { return IsKnown() && Done >= Total && SourcesMissingData == 0; }
};

/**
 * Prepared, immutable-to-Slate row. Painting this value performs no synchronous asset load,
 * validation, or mutation. The shared editor thumbnail pool may resolve its saved thumbnail later.
 */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusCharacterCatalogEditorRow
{
	FSoftObjectPath CharacterPath;
	/** Registry metadata used by Slate's ordinary shared thumbnail pool without loading the Profile. */
	FAssetData CharacterAssetData;
	/** Already-resident Profile, when one exists. Never populated by a synchronous load. */
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> ResidentCharacterProfile;
	FPaper2DPlusCharacterCatalogEntry Entry;
	/** Required-companion presence. Structural, and deliberately NOT a measure of authoring work. */
	FPaper2DPlusCharacterCatalogCompletion Completion;
	/** Ticked-checklist progress across this character's assets. Drives the card meter and filters. */
	FPaper2DPlusCatalogAuthoringProgress AuthoringProgress;
	FPaper2DPlusValidationSummary IssueSummary;
	EPaper2DPlusCatalogRowStatus Status = EPaper2DPlusCatalogRowStatus::Complete;
	FText DisplayName;
	FText StatusText;
	FText StatusTooltip;
	/** True when the saved entry's Character Profile asset is absent from the Asset Registry. */
	bool bMissingAsset = false;
};

/** Outcome of one explicit Suggest Companions run. Ambiguity is reported, never guessed. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusCatalogCompanionSuggestion
{
	int32 NumAssigned = 0;
	/** Slots with more than one inward match: the designer must choose. */
	TArray<EPaper2DPlusCatalogCompanion> AmbiguousSlots;
	/** Slots with no inward match at all. */
	TArray<EPaper2DPlusCatalogCompanion> UnmatchedSlots;
	/** Slots skipped because an untagged legacy asset makes uniqueness unprovable. */
	TArray<EPaper2DPlusCatalogCompanion> LegacySlots;

	bool DidAnything() const { return NumAssigned > 0; }
};

DECLARE_MULTICAST_DELEGATE(FOnCharacterCatalogEditorModelChanged);

/**
 * Headless Character Catalog editor authority.
 *
 * Rows project the SAVED roster only — nothing scans the project or proposes entries. Characters
 * enter through the explicit AddCharacters intake and leave through RemoveCharacters; both are one
 * transaction. Refresh/filter/select/open are read-only. Selection is always a Character soft path.
 * Registry events only re-project saved rows (missing-asset state, renames); they never mutate.
 */
class PAPER2DPLUSEDITOR_API FCharacterCatalogEditorModel final
	: public IProfileItemPickerSource
	, public TSharedFromThis<FCharacterCatalogEditorModel>
{
public:
	using FAssetSnapshotProvider = Paper2DPlusCharacterCatalogSource::FAssetSnapshotProvider;
	using FSettingsSnapshotProvider = Paper2DPlusCharacterCatalogSource::FSettingsSnapshotProvider;
	using FNativeAssetResolver = FCharacterCatalogAuditService::FNativeAssetResolver;
	using FCookRegistrationInspector = FCharacterCatalogAuditService::FCookRegistrationInspector;
	using FOpenAssetAction = TFunction<bool(const FSoftObjectPath&, FText&)>;
	using FOpenSettingsAction = TFunction<bool(FName)>;
	using FSetAuthorityAction = TFunction<bool(UPaper2DPlusCharacterCatalogAsset*, FText&)>;
	using FCreateAssetAction = TFunction<UObject*(
		EPaper2DPlusCatalogCompanion,
		const FSoftObjectPath&,
		const FString&,
		const FString&,
		FText&)>;

	FCharacterCatalogEditorModel() = default;
	virtual ~FCharacterCatalogEditorModel() override;

	void Initialize(
		UPaper2DPlusCharacterCatalogAsset* InCatalog,
		FAssetSnapshotProvider InAssetProvider = FAssetSnapshotProvider(),
		FSettingsSnapshotProvider InSettingsProvider = FSettingsSnapshotProvider(),
		FNativeAssetResolver InAssetResolver = FNativeAssetResolver(),
		FCookRegistrationInspector InCookInspector = FCookRegistrationInspector());
	void Shutdown();

	UPaper2DPlusCharacterCatalogAsset* GetCatalog() const { return Catalog.Get(); }
	bool IsAuthoritative() const;
	bool CanAudit() const { return IsAuthoritative(); }
	FText GetAuthorityStatusText() const;
	bool SetAsProjectCatalog(FText& OutMessage);
	bool OpenProjectSettings() const;

	/** Non-mutating source refresh. Deep validation remains a separate explicit command. */
	void RefreshFromSources();
	/** Publish one live picker invalidation after an external Details/undo mutation, then reconcile. */
	void RefreshAfterExternalMutation();
	/** The only model command that consumes the native asset resolver; always designer-explicit. */
	bool RunAudit(FText& OutMessage);
	bool HasAuditReport() const { return bHasAuditReport; }
	const FPaper2DPlusCharacterCatalogAuditReport& GetAuditReport() const { return AuditReport; }

	const TArray<TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>>& GetRows() const { return Rows; }
	const TArray<TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>>& GetVisibleRows() const { return VisibleRows; }
	TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> FindRow(const FSoftObjectPath& CharacterPath) const;
	TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> GetSelectedRow() const;
	bool SelectCharacter(const FSoftObjectPath& CharacterPath);
	const FSoftObjectPath& GetSelectedCharacterPath() const { return SelectedCharacterPath; }

	const FPaper2DPlusCharacterCatalogFilters& GetFilters() const { return Filters; }
	void SetSearchText(const FString& Text);
	void SetTagFilter(FGameplayTag Tag);
	void SetGroupFilter(FName Group);
	void SetRequirementFilter(EPaper2DPlusCatalogRequirementFilter Filter);
	void SetCompletionFilter(EPaper2DPlusCatalogCompletionFilter Filter);
	void SetSeverityFilter(EPaper2DPlusCatalogSeverityFilter Filter);
	void ClearFilters();

	bool SetRequirement(const FSoftObjectPath& CharacterPath, EPaper2DPlusCatalogCompanion Companion, bool bRequired);
	bool SetCompanionAssignment(const FSoftObjectPath& CharacterPath, EPaper2DPlusCatalogCompanion Companion, const FSoftObjectPath& AssetPath);
	/**
	 * Fill only EMPTY Layer/Combat slots whose inward Character relationship resolves to exactly one
	 * asset, in ONE transaction. Ambiguous, unmatched, and legacy-uncertain slots are reported and left
	 * untouched; an already-assigned slot is never overwritten. Effect has no inward relationship tag,
	 * so it is deliberately never suggested.
	 */
	bool SuggestCompanions(
		const FSoftObjectPath& CharacterPath,
		FPaper2DPlusCatalogCompanionSuggestion& OutResult);
	bool SetTags(const FSoftObjectPath& CharacterPath, const FGameplayTagContainer& Tags);
	/** Replace the Catalog-wide expected animation-tag set in one explicit undoable mutation. */
	bool SetExpectedAnimationTags(const FGameplayTagContainer& Tags);
	/** Replace one authored group's additional expected animation-tag set. */
	bool SetGroupAdditionalExpectedAnimationTags(
		FName GroupName,
		const FGameplayTagContainer& Tags);

	bool AddGroup(FName GroupName, const FText& DisplayName, FText& OutError);
	bool RemoveGroup(FName GroupName);
	bool RenameGroup(FName OldName, FName NewName, const FText& DisplayName, FText& OutError);
	bool AddGroupMember(FName GroupName, const FSoftObjectPath& CharacterPath);
	/** Batch intake for a rail drop: one transaction and one refresh for the whole dragged cohort. */
	int32 AddGroupMembers(FName GroupName, const TArray<FSoftObjectPath>& CharacterPaths);
	bool RemoveGroupMember(FName GroupName, const FSoftObjectPath& CharacterPath);
	bool MoveGroupMember(FName GroupName, const FSoftObjectPath& CharacterPath, int32 Direction);
	/**
	 * Drag-reorder with insert-before semantics (TargetIndex may equal Num() to append), matching the
	 * playback queue's convention so a drop indicator lands where the designer aimed it.
	 *
	 * TargetIndex indexes the group's AUTHORED Members array. A view index is NOT that index — the
	 * roster grid is filtered before it is sorted into member order — so UI drop handlers must use
	 * MoveGroupMemberRelativeTo instead and let the model resolve the anchor itself.
	 */
	bool MoveGroupMemberToIndex(FName GroupName, const FSoftObjectPath& CharacterPath, int32 TargetIndex);
	/**
	 * Anchor-relative sibling of MoveGroupMemberToIndex: place CharacterPath immediately before (or
	 * after) AnchorPath in the group's authored order. THE drop-handler entry point — the anchor is a
	 * character the designer pointed at, so no view-to-authored index translation exists to get wrong
	 * under an active search/tag/requirement/completion filter.
	 */
	bool MoveGroupMemberRelativeTo(
		FName GroupName,
		const FSoftObjectPath& CharacterPath,
		const FSoftObjectPath& AnchorPath,
		bool bInsertAfter);
	bool MoveGroup(FName GroupName, int32 Direction);
	/** TargetIndex indexes the AUTHORED Groups array — see MoveGroupRelativeTo for drop handlers. */
	bool MoveGroupToIndex(FName GroupName, int32 TargetIndex);
	/**
	 * Anchor-relative sibling of MoveGroupToIndex. The Groups rail hides unnamed groups, so rail row N
	 * is not authored group N-1; resolving the anchor group's own index here keeps a rail drop correct
	 * however many unnamed rows the authored array carries.
	 */
	bool MoveGroupRelativeTo(FName GroupName, FName AnchorGroupName, bool bInsertAfter);
	/**
	 * Turn a designer-typed label into a stable, case-insensitively unique internal group name. Rail
	 * creation never asks for the internal name — this is why the old dual-field row could vanish.
	 */
	FName DeriveUniqueGroupName(const FText& DisplayLabel) const;

	/** True when the path already has a saved entry; the Add picker's default filter hides these. */
	bool IsCharacterInCatalog(const FSoftObjectPath& CharacterPath) const;
	/**
	 * Explicit intake: appends one saved entry per new character in ONE transaction, skipping null,
	 * duplicate, and already-saved paths. Selects the last added character. Returns the added count.
	 */
	int32 AddCharacters(const TArray<FSoftObjectPath>& CharacterPaths);
	/**
	 * Explicit removal that sticks: deletes the saved entries and every group membership in ONE
	 * transaction. Referenced assets are never deleted and nothing re-adds a removed character.
	 */
	bool RemoveCharacters(const TArray<FSoftObjectPath>& CharacterPaths);
	bool RemoveCharacter(const FSoftObjectPath& CharacterPath);

	FSoftObjectPath GetCompanionPath(const FSoftObjectPath& CharacterPath, EPaper2DPlusCatalogCompanion Companion) const;
	bool OpenCharacter(const FSoftObjectPath& CharacterPath, FText& OutError) const;
	bool OpenCompanion(const FSoftObjectPath& CharacterPath, EPaper2DPlusCatalogCompanion Companion, FText& OutError) const;
	/** Open an actionable warning's external target; Catalog-local targets stay in this editor. */
	bool OpenIssueTarget(const FPaper2DPlusValidationIssue& Issue, FText& OutError) const;
	UObject* CreateCompanion(
		const FSoftObjectPath& CharacterPath,
		EPaper2DPlusCatalogCompanion Companion,
		const FString& DestinationPackagePath,
		const FString& DesiredAssetName,
		FText& OutError);
	bool ActivateIssue(const FPaper2DPlusValidationIssue& Issue);
	FName GetLastNavigationField() const { return LastNavigationField; }
	FName GetLastNavigationTab() const { return LastNavigationTab; }

	void SetOpenAssetActionForTests(FOpenAssetAction Action) { OpenAssetAction = MoveTemp(Action); }
	void SetOpenSettingsActionForTests(FOpenSettingsAction Action) { OpenSettingsAction = MoveTemp(Action); }
	void SetAuthorityActionForTests(FSetAuthorityAction Action) { SetAuthorityAction = MoveTemp(Action); }
	void SetCreateAssetActionForTests(FCreateAssetAction Action) { CreateAssetAction = MoveTemp(Action); }
	int32 GetMutationCountForTests() const { return MutationCount; }
	int32 GetSourceDelegateCountForTests() const;
	static void GetStatusPresentation(
		EPaper2DPlusCatalogRowStatus Status,
		FText& OutText,
		FText& OutTooltip);

	FOnCharacterCatalogEditorModelChanged& OnModelChanged() { return ModelChanged; }

	// IProfileItemPickerSource
	virtual FName GetSourceType() const override;
	virtual FString GetLogicalCatalogScope() const override;
	virtual void GetItems(TArray<FProfilePickerItem>& OutItems) const override;
	virtual FProfileItemIdentity GetSelectedIdentity() const override;
	virtual bool SelectItem(const FProfileItemIdentity& Identity) override;
	virtual FOnProfileItemSourceChanged& OnSourceChanged() override { return SourceChanged; }

private:
	void BindSourceDelegates();
	void UnbindSourceDelegates();
	void HandleAssetChanged(const FAssetData& AssetData);
	void HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	void HandleSettingsChanged();
	void RequestRowRefresh();
	bool TickRowRefresh(float DeltaTime);
	void RebuildRows(bool bClearAudit);
	void RefilterRows();
	bool RevealCharacterForNavigation(const FSoftObjectPath& CharacterPath);
	/**
	 * GroupLabelsByCharacter is built ONCE per RefilterRows and holds each character's group labels
	 * pre-joined for search. Without it this asked "is this row in this group" for every row against
	 * every group, and each answer walked the group's members allocating two FStrings per comparison —
	 * tens of thousands of allocations per keystroke on a few-hundred-character Catalog.
	 */
	bool PassesFilters(
		const FPaper2DPlusCharacterCatalogEditorRow& Row,
		const TMap<FString, FString>& GroupLabelsByCharacter) const;
	TMap<FString, FString> BuildGroupLabelSearchIndex() const;
	void BroadcastChanged();

	FPaper2DPlusCharacterCatalogSettingsSnapshot GatherSettings() const;
	void GatherAssets(TArray<FAssetData>& OutAssets) const;
	int32 FindEntryIndex(const FSoftObjectPath& CharacterPath) const;
	int32 FindGroupIndex(FName GroupName) const;
	bool IsInGroup(FName GroupName, const FSoftObjectPath& CharacterPath) const;
	void RefreshAfterMutation();
	bool OpenPath(const FSoftObjectPath& Path, FText& OutError) const;

	TWeakObjectPtr<UPaper2DPlusCharacterCatalogAsset> Catalog;
	FAssetSnapshotProvider AssetProvider;
	FSettingsSnapshotProvider SettingsProvider;
	FNativeAssetResolver AssetResolver;
	FCookRegistrationInspector CookInspector;
	FOpenAssetAction OpenAssetAction;
	FOpenSettingsAction OpenSettingsAction;
	FSetAuthorityAction SetAuthorityAction;
	FCreateAssetAction CreateAssetAction;

	IAssetRegistry* BoundAssetRegistry = nullptr;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle AssetUpdatedHandle;
	FDelegateHandle SettingsChangedHandle;
	FTSTicker::FDelegateHandle RowRefreshTickerHandle;
	bool bRowRefreshPending = false;

	FPaper2DPlusCharacterCatalogAuditReport AuditReport;
	bool bHasAuditReport = false;
	FPaper2DPlusCharacterCatalogFilters Filters;
	TArray<TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>> Rows;
	TArray<TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>> VisibleRows;
	FSoftObjectPath SelectedCharacterPath;
	FName LastNavigationField = NAME_None;
	FName LastNavigationTab = NAME_None;
	FOnCharacterCatalogEditorModelChanged ModelChanged;
	FOnProfileItemSourceChanged SourceChanged;
	int32 MutationCount = 0;
};
