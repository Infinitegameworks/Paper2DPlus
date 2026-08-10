// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

/** Severity shared by every editor-facing Paper2D+ validation surface. */
enum class EPaper2DPlusValidationSeverity : uint8
{
	Info,
	Warning,
	Error
};

/**
 * A navigation promise made by the host that presents an issue.
 *
 * An issue without this value remains fully readable, but the UI must not imply that it can focus a
 * field.  Hosts opt in only when they can truthfully activate the named tool/tab and stable item.
 */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusValidationToolTarget
{
	/** Asset to open. May differ from the asset that owns the reported problem. */
	FSoftObjectPath AssetPath;
	FName ToolId;
	FName TabId;
	FString ItemIdentity;
	FName Field;

	bool IsTruthful() const
	{
		return AssetPath.IsValid() || !ToolId.IsNone() || !TabId.IsNone();
	}
};

/** One deterministic, presentation-neutral issue projected from a domain validator. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusValidationIssue
{
	EPaper2DPlusValidationSeverity Severity = EPaper2DPlusValidationSeverity::Info;

	/** Stable machine-facing family, for example Paper2DPlus.Effect.EffectFlipbook. */
	FName Code;

	/** Optional non-localized subtype when one code/item/field can emit multiple distinct violations. */
	FString StableDiscriminator;

	/** Deterministic identity derived from machine-facing path/code/scope/item/field data, never localized text. */
	FString StableKey;

	/** Soft identity only. Validation projection never needs to load an asset to populate this field. */
	FSoftObjectPath AssetPath;
	FName AssetType;

	/** Catalog character context. Empty for standalone validation outside a Character Catalog audit. */
	FSoftObjectPath CharacterPath;

	/** Domain context retained without trying to merge domain validation models. */
	FName Scope;
	FString ItemIdentity;
	FName Field;

	/** The domain validator's message, unchanged apart from FString-to-FText projection. */
	FText Message;
	FText Remediation;

	/** Present only when a hosting editor has a real focus target for this issue. */
	TOptional<FPaper2DPlusValidationToolTarget> ToolTarget;

	bool CanActivate() const
	{
		return ToolTarget.IsSet()
			&& ToolTarget->IsTruthful()
			&& GetNavigationAssetPath().IsValid();
	}

	FSoftObjectPath GetNavigationAssetPath() const
	{
		return ToolTarget.IsSet() && ToolTarget->AssetPath.IsValid()
			? ToolTarget->AssetPath
			: AssetPath;
	}
};

/** Compact status value shared by issue panels, toolbar badges, and future Catalog rows. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusValidationSummary
{
	int32 NumInfo = 0;
	int32 NumWarnings = 0;
	int32 NumErrors = 0;
	bool bValidating = false;

	int32 NumIssues() const { return NumInfo + NumWarnings + NumErrors; }
	bool HasErrors() const { return NumErrors > 0; }
	bool HasWarnings() const { return NumWarnings > 0; }

	FText GetStatusText() const;
	FText GetAccessibleText() const;
	FName GetStatusIconName() const;

	static FPaper2DPlusValidationSummary FromIssues(
		const TArray<FPaper2DPlusValidationIssue>& Issues,
		bool bInValidating = false);
};

DECLARE_DELEGATE_TwoParams(
	FPaper2DPlusValidationAdapterDelegate,
	const UObject&,
	TArray<FPaper2DPlusValidationIssue>&);

DECLARE_MULTICAST_DELEGATE(FOnPaper2DPlusValidationAdaptersChanged);

/**
 * Editor-only validation projection and late-bound adapter registry.
 *
 * Domain assets keep their native validators and issue types.  This service selects one registered
 * adapter for the loaded object's most-derived matching class, normalizes its values, removes exact
 * duplicates, and sorts deterministically. Registration is class-path based so commandlets and Data
 * Validation discover Character, Layer, Effect, Combat, Catalog, and future types from one registry.
 */
class PAPER2DPLUSEDITOR_API FPaper2DPlusValidationService
{
public:
	static FPaper2DPlusValidationService& Get();

	/** Register or replace a named adapter. Both identifiers must be stable and non-empty. */
	bool RegisterAdapter(
		FName AdapterId,
		FName AssetClassPath,
		FPaper2DPlusValidationAdapterDelegate Adapter);

	bool UnregisterAdapter(FName AdapterId);
	bool HasAdapterFor(const UObject* Asset) const;

	/** Deterministic class-path snapshot used by the commandlet and Data Validation bridge. */
	void GetRegisteredAssetClassPaths(TArray<FName>& OutClassPaths) const;

	/** Returns false only when Asset is null or no adapter is registered for its class hierarchy. */
	bool ValidateObject(const UObject* Asset, TArray<FPaper2DPlusValidationIssue>& OutIssues) const;

	/** Public for late adapters and commandlet projection; does not inspect or mutate any UObject. */
	static void NormalizeAndSort(TArray<FPaper2DPlusValidationIssue>& InOutIssues);
	static FString MakeStableKey(const FPaper2DPlusValidationIssue& Issue);
	static FText SeverityText(EPaper2DPlusValidationSeverity Severity);
	static FName SeverityIconName(EPaper2DPlusValidationSeverity Severity);

	FOnPaper2DPlusValidationAdaptersChanged& OnAdaptersChanged() { return AdaptersChanged; }

private:
	struct FAdapterEntry
	{
		FName AdapterId;
		FName AssetClassPath;
		FPaper2DPlusValidationAdapterDelegate Adapter;
	};

	FPaper2DPlusValidationService();
	const FAdapterEntry* FindAdapter(const UObject* Asset) const;

	TArray<FAdapterEntry> Adapters;
	FOnPaper2DPlusValidationAdaptersChanged AdaptersChanged;
};
