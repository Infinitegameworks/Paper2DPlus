// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "CharacterDataAssetFactory.generated.h"

/**
 * Factory for creating Paper2DPlusCharacterDataAsset instances from the Content Browser.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UCharacterDataAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UCharacterDataAssetFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};
