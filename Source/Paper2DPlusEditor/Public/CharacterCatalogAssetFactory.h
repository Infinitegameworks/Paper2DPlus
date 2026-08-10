// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "CharacterCatalogAssetFactory.generated.h"

/** Creates an intentionally empty Catalog; authority and content roots remain explicit project settings. */
UCLASS()
class PAPER2DPLUSEDITOR_API UCharacterCatalogAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UCharacterCatalogAssetFactory();
	virtual UObject* FactoryCreateNew(
		UClass* Class,
		UObject* InParent,
		FName Name,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	virtual TArray<FAssetCategoryPath> GetAssetMenuPathsForCategory(FName InCategory) const override;
#endif
};
