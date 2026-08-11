// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "AssetTypeActions/AssetTypeActions_Blueprint.h"
#include "CoreMinimal.h"

/** Most-derived asset actions that route only specialized Cue Types into the restricted editor. */
class PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueTypeAssetActions
	: public FAssetTypeActions_Blueprint
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual void OpenAssetEditor(
		const TArray<UObject*>& InObjects,
		TSharedPtr<class IToolkitHost> EditWithinLevelEditor =
			TSharedPtr<IToolkitHost>()) override;

#if WITH_DEV_AUTOMATION_TESTS
	bool CanCreateDerivedBlueprintForTests() const
	{
		return CanCreateNewDerivedBlueprint();
	}
#endif

protected:
	/** A Cue Type's parent is permanently Cue or Cue State; child Blueprint creation is not authoring. */
	virtual bool CanCreateNewDerivedBlueprint() const override { return false; }
};
