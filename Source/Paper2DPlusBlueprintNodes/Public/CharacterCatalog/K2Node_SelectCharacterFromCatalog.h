// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_SelectCharacterFromCatalog.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FCompilerResultsLog;
class FKismetCompilerContext;
class UPaper2DPlusCharacterCatalogAsset;

/**
 * Pure catalog lookup with a Character pin whose editor asset picker is restricted to Catalog members.
 * Expands to UPaper2DPlusCharacterCatalogAsset::FindEntryByCharacterProfile at compile time.
 */
UCLASS()
class PAPER2DPLUSBLUEPRINTNODES_API UK2Node_SelectCharacterFromCatalog : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName CatalogPinName;
	static const FName CharacterPinName;
	static const FName EntryPinName;
	static const FName FoundPinName;

	virtual void AllocateDefaultPins() override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool IsNodePure() const override { return true; }

	/** Resolves a literal Catalog pin, then the project default for editor-only picker population. */
	UPaper2DPlusCharacterCatalogAsset* ResolveCatalogForEditor() const;

	/** Returns the exact soft paths that the editor picker may display; companion assets are never loaded. */
	TSet<FSoftObjectPath> GetSelectableCharacterProfilePaths() const;

	/** True when the resolved editor Catalog contains the supplied Character Profile path. */
	bool IsCharacterProfileSelectable(const FSoftObjectPath& CharacterProfilePath) const;

	/** Returns the literal Character selection, or an empty path when the pin is linked or unset. */
	FSoftObjectPath GetLiteralCharacterProfilePath() const;
};
