// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "AsepriteFactory.generated.h"

class UPaper2DPlusCharacterLayerAsset;

/**
 * Factory for importing Aseprite (.ase/.aseprite) files via drag-and-drop
 * into the Content Browser. Shows a modal layer import dialog, then creates
 * per-layer textures/sprites and a CharacterLayerAsset, or a flattened
 * sprite sheet + flipbooks when appropriate.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UAsepriteFactory : public UFactory
{
	GENERATED_BODY()

public:
	UAsepriteFactory();

	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
		const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual bool DoesSupportClass(UClass* Class) override;
	virtual UClass* ResolveSupportedClass() override;
	virtual FText GetDisplayName() const override;
};
